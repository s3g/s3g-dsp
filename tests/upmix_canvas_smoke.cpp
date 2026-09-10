// Exercise the real editors and original codecs/DSP, with Cocoa captures.
#include "../plugins/clap_format_upscale/s3g_format_upscale_clap.cpp"
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
using upmix_canvas::Editor;
namespace M = upmix_canvas;
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
    auto *view = [[S3GFormatUpscaleMapView alloc] initWithPlugin:v._plugin];
    const auto setScalar = [&](const char *name, const auto &value) {
      Ivar ivar = class_getInstanceVariable([view class], name);
      std::memcpy(reinterpret_cast<char *>(view) + ivar_getOffset(ivar), &value,
                  sizeof(value));
    };
    setScalar("_selectedInput", v._selectedInput);
    setScalar("_selectedOutput", v._selectedOutput);
    setScalar("_selectionIsOutput", v._selectionIsOutput);
    setScalar("_page", v._page);
    setScalar("_layoutOrigami", v._layoutOrigami);
    setScalar("_matrixPreset", v._matrixPreset);
    const NSInteger design = v._designMap;
    setScalar("_designMap", design);
    char title[64]{};
    std::snprintf(title, sizeof(title), "%s", v._titlePresetName);
    setScalar("_titlePresetName", title);
    const auto path = foundation::pathToUtf8(
        directory / (std::string(descriptor.id) + "-cocoa-" + name + ".pdf"));
    expect([[view dataWithPDFInsideRect:[view bounds]]
               writeToFile:[NSString stringWithUTF8String:path.c_str()]
                atomically:YES],
           "write Cocoa reference");
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

CPoint center(M::Box b) { return {M::midX(b), M::midY(b)}; }
void choose(Editor &v, clap_id id, unsigned index) {
  click(v, center(M::menuValueRectForParam(id)));
  expect(v._openMenu == id, "custom menu opens");
  const auto g = M::formatMenuGeometry(id);
  const bool second = index >= g.firstCount;
  const auto b = second ? g.second : g.first;
  click(v, {b.origin.x + 20.,
            b.origin.y + (index - (second ? g.firstCount : 0)) * g.itemHeight +
                g.itemHeight * .5});
  expect(v._openMenu == CLAP_INVALID_ID, "custom menu closes");
}
void sameModel(const Plugin &a, const Plugin &b, const char *message) {
  const auto x = captureUpmix(a), y = captureUpmix(b);
  expect(std::memcmp(&x, &y, sizeof(x)) == 0, message);
}
void factories(Editor &v, Plugin &p, Events &events) {
  for (unsigned preset = 0; preset < M::kMatrixPresetCount; ++preset) {
#if defined(__APPLE__)
    auto reference = std::make_unique<Plugin>();
    restoreUpmixModel(*reference, captureUpmix(*v._plugin));
    auto *cocoa =
        [[S3GFormatUpscaleMapView alloc] initWithPlugin:reference.get()];
    [cocoa applyMatrixPreset:preset];
#endif
    choose(v, M::kUiFactoryPreset, preset);
    flush(p, events);
    expect(v._matrixPreset == preset, "every factory preset selected");
#if defined(__APPLE__)
    sameModel(*v._plugin, *reference, "factory exact Cocoa state/coefficients");
    [cocoa release];
#endif
    sameModel(p, *v._plugin, "GUI routing reaches audio owner");
    render(v, "factory-" + std::to_string(preset));
    if (preset >= 6) {
      expect(usesOrderedTierGroups(*v._plugin),
             "ordered expansion tier groups");
      auto weights = audibleMatrix(*v._plugin);
      for (unsigned i = 0; i < v._plugin->dsp.activeInputs(); ++i) {
        double power = 0.;
        for (unsigned o = 0; o < v._plugin->dsp.activeOutputs(); ++o)
          power += std::pow(weights[i * kChannels + o], 2);
        expect(near(power, 1.), "balanced expansion input power");
      }
    }
  }
  events.balanced();
}
void formats(Editor &v, Plugin &p, Events &events) {
  for (clap_id id : {clap_id(kParamInputLayout), clap_id(kParamOutputLayout)}) {
    for (unsigned i = 0; i < s3g::kFormatUpscaleLayoutCount; ++i) {
      choose(v, id, i);
      flush(p, events);
      expect(getParamValue(*v._plugin, id) == i && getParamValue(p, id) == i,
             "all 42 entries in both format menus");
    }
  }
  choose(v, M::kUiFactoryPreset, 0);
  flush(p, events);
  for (unsigned recipe = 0; recipe < 4; ++recipe) {
    choose(v, kParamPlacement, recipe);
    flush(p, events);
    expect(v._plugin->params.placement == M::recipePlacement(recipe),
           "recipe menu mapping");
    for (unsigned shape = 0; shape < 4; ++shape) {
      choose(v, kParamAutoRowShape, shape);
      flush(p, events);
      expect(uint32_t(v._plugin->autoRowShape) == shape, "weight shape menu");
      click(v, center(M::sideAutoActionRect(0)));
      flush(p, events);
      sameModel(p, *v._plugin, "all recipe/shape matrices reach DSP");
    }
  }
  render(v);
  click(v, center(M::menuValueRectForParam(kParamInputLayout)));
  auto before = render(v);
  MouseMoveEvent hover;
  hover.mousePosition = center(M::formatMenuGeometry(kParamInputLayout).first);
  v.onMouseMoveEvent(hover);
  expect(render(v) != before, "two-column menu hover");
  click(v, {4, 700});
  expect(v._openMenu == CLAP_INVALID_ID, "outside click dismisses menu");
  events.balanced();
}
CPoint cell(Editor &v, unsigned input, unsigned output) {
  const auto g = M::matrixGeometry(v._plugin->dsp.activeInputs(),
                                   v._plugin->dsp.activeOutputs());
  return {g.grid.origin.x + (output + .5) * g.cell,
          g.grid.origin.y + (input + .5) * g.cell};
}
void crosspoints(Editor &v, Plugin &p, Events &events) {
  choose(v, M::kUiFactoryPreset, 3);
  flush(p, events);
  const auto negative = std::find_if(v._plugin->dsp.manualWeights().begin(),
                                     v._plugin->dsp.manualWeights().end(),
                                     [](float w) { return w < 0.; });
  expect(negative != v._plugin->dsp.manualWeights().end(),
         "stereo difference includes inverse polarity");
  const unsigned index =
      std::distance(v._plugin->dsp.manualWeights().begin(), negative);
  click(v, cell(v, index / 64, index % 64));
  const auto rail = M::selectedWeightHitRect();
  down(v, {rail.origin.x + rail.size.width * .5, M::midY(rail)});
  up(v);
  flush(p, events);
  expect(near(v._plugin->dsp.manualWeight(index / 64, index % 64), -.5),
         "derived gain retains sign");
  click(v, center(M::crosspointActionRect(0)));
  flush(p, events);
  expect(v._plugin->dsp.manualWeight(index / 64, index % 64) == 0.f,
         "delete derived cell");
  choose(v, M::kUiFactoryPreset, 0);
  flush(p, events);
  click(v, center(M::sideAutoActionRect(1)));
  flush(p, events);
  expect(v._plugin->dsp.manualRoutesActive(),
         "CLEAR enters exact manual graph");
  const auto a = cell(v, 0, 0);
  down(v, a);
  move(v, {a.x, a.y + 13.});
  up(v);
  flush(p, events);
  expect(near(v._plugin->dsp.manualWeight(0, 0), .75),
         "vertical cell gain drag");
  down(v, a);
  move(v, cell(v, 0, 1));
  move(v, cell(v, 1, 1));
  up(v);
  flush(p, events);
  expect(v._plugin->dsp.manualWeight(0, 1) == 1.f &&
             v._plugin->dsp.manualWeight(1, 1) == 1.f,
         "crosspoint drag painting");
  MouseDownEvent right;
  right.buttonState.add(MouseButton::Right);
  right.mousePosition = cell(v, 0, 0);
  v.onMouseDownEvent(right);
  flush(p, events);
  expect(v._plugin->dsp.manualWeight(0, 0) == 0.f, "right-click delete");
  click(v, cell(v, 0, 0), 2);
  flush(p, events);
  expect(v._plugin->dsp.manualWeight(0, 0) == 1.f,
         "double-click enables inactive cell");
  click(v, cell(v, 0, 0), 2);
  flush(p, events);
  expect(v._plugin->dsp.manualWeight(0, 0) == 0.f,
         "double-click disables active cell");
  for (unsigned action = 0; action < 2; ++action) {
    click(v, center(M::levelActionRect(action)));
    flush(p, events);
    const auto w = audibleMatrix(*v._plugin);
    for (unsigned i = 0; i < (action ? 4u : 2u); ++i) {
      double power = 0;
      for (unsigned j = 0; j < (action ? 2u : 4u); ++j) {
        const float g = action ? w[j * 64 + i] : w[i * 64 + j];
        power += g * g;
      }
      expect(action ? power <= 1.00001 : (power < 1e-8 || near(power, 1.)),
             "visible coefficient power operations");
    }
  }
  choose(v, kParamInputLayout, unsigned(s3g::FormatUpscaleLayout::Ring64));
  choose(v, kParamOutputLayout, unsigned(s3g::FormatUpscaleLayout::Ring64));
  const auto geometry = M::matrixGeometry(64, 64);
  expect(geometry.cell < 10., "dense matrix");
  for (unsigned i = 0; i < 64; ++i)
    for (unsigned o = 0; o < 64; ++o) {
      unsigned hitIn = 0, hitOut = 0;
      expect(M::matrixCellAtPoint(cell(v, i, o), 64, 64, hitIn, hitOut) &&
                 hitIn == i && hitOut == o,
             "all 4096 crosspoints remain addressable");
    }
  click(v, cell(v, 63, 63));
  flush(p, events);
  expect(v._selectedInput == 63 && v._selectedOutput == 63 &&
             v._plugin->dsp.manualWeight(63, 63) == 1.f,
         "last dense crosspoint clickable");
  render(v, "dense-64x64");
  events.balanced();
}
void customLayouts(Editor &v, Plugin &p, Events &events) {
  choose(v, M::kUiFactoryPreset, 0);
  flush(p, events);
  for (unsigned output = 0; output < 2; ++output) {
    click(v, center(M::formatEditButtonRect(output)));
    expect(v._designMap == int(output), "custom coordinate editor opens");
    for (unsigned column = 0; column < 5; ++column) {
      const auto rail = M::customEditorSliderHitRect(column);
      for (double n : {0., .5, .999999}) {
        down(v, {rail.origin.x + rail.size.width * n, M::midY(rail)});
        up(v);
        flush(p, events);
        auto layout = v.editableLayout();
        const auto &speaker =
            layout.speakers[std::min(v.editableSelection(), layout.count - 1)];
        if (column == 2)
          expect(near(speaker.azimuthDeg, s3g::aedAzimuthFromSliderNorm(n)),
                 "custom AZ slider");
        if (column == 3)
          expect(near(speaker.elevationDeg, n * 180. - 90.),
                 "custom EL slider");
        if (column == 4)
          expect(near(speaker.distance, .1 + 2.9 * n), "custom DST slider");
        sameModel(p, *v._plugin, "custom layout reaches audio owner");
      }
    }
    render(v, output ? "custom-output" : "custom-input");
    click(v, center(M::customEditorActionRect(0)));
    flush(p, events);
    expect(v.editableLayout().count == (output ? 9u : 3u),
           "custom factory layout");
    click(v, center(M::customEditorActionRect(1)));
    flush(p, events);
    expect(v.editableLayout().count == (output
                                            ? v._plugin->dsp.activeInputs()
                                            : v._plugin->dsp.activeOutputs()),
           "COPY OTHER MAP");
    click(v, center(M::customEditorActionRect(2)));
    expect(v._designMap == -1, "DONE closes custom controls");
  }
  for (unsigned output = 0; output < s3g::kFormatUpscaleLayoutCount; ++output) {
    v._page = 0;
    choose(v, kParamOutputLayout, output);
    flush(p, events);
    click(v, center(M::viewSelectorRect(1)));
    expect(v._page == 1 && v._layoutOrigami, "LAYOUT opens Tier Rings");
    const auto &layout = v._plugin->dsp.outputLayout();
    std::array<M::Point, kChannels> portable{};
    M::layoutNodePoints(layout, M::layoutProjectionRect(true), true, portable);
#if defined(__APPLE__)
    std::array<NSPoint, kChannels> cocoa{};
    ::layoutNodePoints(layout, ::layoutProjectionRect(true), true, cocoa);
    for (unsigned i = 0; i < layout.count; ++i)
      expect(near(portable[i].x, cocoa[i].x) && near(portable[i].y, cocoa[i].y),
             "all Tier Rings coordinates exact Cocoa projection");
#endif
    click(v, portable[0]);
    expect(v._selectionIsOutput && v._selectedOutput == 0,
           "visible node/hit position agrees");
    if (output == 35 || output == 36 || output == 38 || output == 14)
      render(v, "rings-" + std::to_string(output));
    click(v, center(M::viewSelectorRect(0)));
  }
  v.edit([&] { v.loadDocumentationThreeTierLayout(); });
  flush(p, events);
  render(v, "documentation-matrix");
  v._page = 1;
  render(v, "documentation-layout");
  v.edit([&] { v.loadDocumentationCube41Layout(); });
  flush(p, events);
  render(v, "documentation-cube41");
  v._page = 0;
  events.balanced();
}
template <class State> void legacy(Editor &v, Plugin &p, State s) {
  Bytes data;
  const auto *bytes = reinterpret_cast<const uint8_t *>(&s);
  data.data.assign(bytes, bytes + sizeof(s));
  auto reference = std::make_unique<Plugin>();
  reference->plugin.plugin_data = reference.get();
  restoreUpmixModel(*reference, captureUpmix(*v._plugin));
  expect(decodeUpmixState(&reference->plugin, &data.in), "Cocoa legacy reader");
  data.offset = 0;
  expect(stateLoad(&p.plugin, &data.in), "portable legacy reader");
  sameModel(*v._plugin, *reference, "legacy migration equals original codec");
}
void presets(Editor &v, Plugin &p, Events &events) {
  v._page = 0;
  choose(v, M::kUiFactoryPreset, 13);
  flush(p, events);
  v.edit([&] { setParamValue(*v._plugin, kParamOutputGain, -7.); });
  flush(p, events);
  Bytes encoded;
  expect(stateSave(&p.plugin, &encoded.out), "project save");
  const auto before = captureUpmix(*v._plugin);
  char temp[] = "/tmp/s3g-upmix-presets.XXXXXX";
#if defined(__APPLE__)
  const auto root = std::filesystem::path(mkdtemp(temp));
#else
  const auto root =
      std::filesystem::temp_directory_path() /
      ("s3g-upmix-presets-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
#endif
  const auto path = foundation::pathToUtf8(root / u8"空間 — Ω.s3gpreset");
  expect(v.presetFile(path, true), "Unicode preset save");
  v.edit([&] {
    v.applyMatrixPreset(0);
    setParamValue(*v._plugin, kParamOutputGain, -13.);
  });
  flush(p, events);
  expect(
      v.edit([&] { expect(v.presetFile(path, false), "Unicode preset load"); }),
      "preset publishes transaction");
  flush(p, events);
  expect(v._plugin->params.outputGainDb == -13.f &&
             v._plugin->dsp.manualWeights() == before.manualWeights,
         "preset preserves OUT and complete matrix");
  expect(stateLoad(&p.plugin, &encoded.in), "project restore");
  flush(p, events);
  sameModel(*v._plugin, p, "project state publication");
  expect(v._plugin->params.outputGainDb == -7.f, "project recall restores OUT");
  for (size_t n :
       {size_t(0), size_t(4), size_t(1024), encoded.data.size() - 1}) {
    Bytes truncated;
    truncated.data.assign(encoded.data.begin(), encoded.data.begin() + n);
    const auto snapshot = captureUpmix(*v._plugin);
    expect(!stateLoad(&p.plugin, &truncated.in),
           "truncated matrix state rejected");
    const auto after = captureUpmix(*v._plugin);
    expect(std::memcmp(&snapshot, &after, sizeof(snapshot)) == 0,
           "failed recall leaves live state intact");
  }
  for (unsigned field = 0; field < 3; ++field) {
    auto bad = before;
    if (field == 0)
      bad.params.outputGainDb = NAN;
    if (field == 1)
      bad.manualWeights[1] = NAN;
    if (field == 2)
      bad.customOutput.speakers[0].azimuthDeg = NAN;
    Bytes invalid;
    const auto *b = reinterpret_cast<const uint8_t *>(&bad);
    invalid.data.assign(b, b + sizeof(bad));
    expect(!stateLoad(&p.plugin, &invalid.in),
           "NaN state rejected transactionally");
  }
  legacy(v, p, SavedStateV1{});
  legacy(v, p, SavedStateV2{});
  legacy(v, p, SavedStateV3{});
  SavedStateV4 v4;
  v4.manualRoutesActive = 1;
  v4.manualWeights[0] = 1.;
  v4.manualWeights[1] = .5;
  legacy(v, p, v4);
  SavedStateV5 v5;
  v5.manualRoutesActive = 1;
  v5.manualWeights[0] = .75;
  v5.manualWeights[64] = .5;
  legacy(v, p, v5);
  SavedStateV6 v6;
  v6.manualRoutesActive = 1;
  v6.manualWeights[0] = .75;
  v6.manualWeights[1] = .5;
  legacy(v, p, v6);
  legacy(v, p, SavedState{});
  flush(p, events);
  events.balanced();
  std::filesystem::remove_all(root);
}
void pressure(Editor &v, Plugin &p, Events &events) {
  v._page = 0;
  v.edit([&] { v.applyMatrixPreset(0); });
  flush(p, events);
  const auto rail = M::sliderHitRectForParam(kParamOutputGain);
  down(v, center(rail));
  const double trackX = layout::processorControlX(M::kExtensionPanel.frame.x);
  const double trackW =
      layout::processorTrackWidth(M::kExtensionPanel.frame.width);
  for (unsigned i = 0; i < 1500; ++i)
    move(v, {trackX + trackW * ((i % 100) / 100.), M::midY(rail)});
  up(v);
  expect(p.monitorGuiEvents.available() <= 1, "queue saturation");
  const auto before = captureUpmix(*v._plugin);
  expect(!v.edit([&] { v.applyMatrixPreset(13); }),
         "full preset refused at saturation");
  const auto after = captureUpmix(*v._plugin);
  expect(std::memcmp(&before, &after, sizeof(before)) == 0,
         "rejected preset preserves routing and values");
  events.accept = false;
  flush(p, events);
  setParamValue(p, kParamOutputGain, -9.);
  events.accept = true;
  flush(p, events);
  v.service();
  expect(v._plugin->params.outputGainDb == -9.f,
         "late notifications never replay older automation");
  events.balanced();
  down(v, center(rail));
  v.stopRefresh();
  flush(p, events);
  events.balanced();
  // Mailbox remains lossless for latest routing even without an audio callback
  // between rapid edits; no parameter queue capacity is consumed.
  for (unsigned i = 0; i < 200; ++i)
    v.edit([&] { v._plugin->dsp.setManualWeight(0, 0, (i % 100) / 100.f); });
  flush(p, events);
  expect(near(p.dsp.manualWeight(0, 0), .99),
         "routing triple-buffer latest publication");
  v.edit([&] { v.applyMatrixPreset(3); });
  flush(p, events);
  setParamValue(p, kParamInputLayout, double(s3g::FormatUpscaleLayout::Quad));
  setParamValue(p, kParamInputLayout, double(s3g::FormatUpscaleLayout::Stereo));
  v.service();
  sameModel(
      p, *v._plugin,
      "intermediate host format changes clear derived polarity in both models");
  events.balanced();
}
template <class Sample> void audio(Editor &v, Plugin &p, Events &events) {
  constexpr unsigned frames = 128, channels = 66;
  std::array<std::array<Sample, frames>, channels> input{}, output{};
  std::array<Sample *, channels> ins{}, outs{};
  for (unsigned c = 0; c < channels; ++c) {
    ins[c] = input[c].data();
    outs[c] = output[c].data();
    for (unsigned i = 0; i < frames; ++i)
      input[c][i] = Sample(.01 * (c + 1) * std::sin(i * .21 + c));
  }
  clap_audio_buffer_t in{}, out{};
  in.channel_count = out.channel_count = channels;
  if constexpr (std::is_same_v<Sample, float>) {
    in.data32 = ins.data();
    out.data32 = outs.data();
  } else {
    in.data64 = ins.data();
    out.data64 = outs.data();
  }
  clap_process_t proc{};
  proc.frames_count = frames;
  proc.audio_inputs_count = proc.audio_outputs_count = 1;
  proc.audio_inputs = &in;
  proc.audio_outputs = &out;
  proc.out_events = &events.out;
  for (unsigned preset = 0; preset < M::kMatrixPresetCount; ++preset) {
    v.edit([&] { v.applyMatrixPreset(preset); });
    flush(p, events);
    for (bool extension : {false, true}) {
      v.edit([&] {
        setParamValue(*v._plugin, kParamDelay, extension ? 13. : 0.);
        setParamValue(*v._plugin, kParamDecor, extension ? 48. : 0.);
      });
      flush(p, events);
      auto reference = std::make_unique<Plugin>();
      restoreUpmixModel(*reference, captureUpmix(*v._plugin));
      reference->dsp.prepare(48000.);
      p.dsp.reset();
      expect(process(&p.plugin, &proc) == CLAP_PROCESS_CONTINUE,
             "process output");
      for (unsigned i = 0; i < frames; ++i) {
        std::array<float, 64> frameIn{}, frameOut{};
        for (unsigned c = 0; c < 64; ++c)
          frameIn[c] = float(input[c][i]);
        reference->dsp.processFrame(frameIn.data(), 64, frameOut.data(), 64);
        for (unsigned c = 0; c < channels; ++c)
          expect(output[c][i] == Sample(c < 64 ? frameOut[c] : 0.f),
                 "bit-identical original DSP, including unused lanes");
      }
    }
  }
  events.balanced();
}
void nativeWindows(Plugin &p) {
#if defined(__APPLE__)
  if (!std::getenv("S3G_MONITOR_NATIVE_WINDOWS"))
    return;
  auto main = std::make_unique<foundation::AuxiliaryWindow>(
      "Matrix parity main", kGuiWidth, kGuiHeight);
  auto *v = new Editor(p);
  expect(main->attach(v) && main->show(), "native main window");
  v->edit([&] { v->applyMatrixPreset(0); });
  click(*v, center(M::formatEditButtonRect(false)));
  const double typed[] = {72.5, -24.25, 1.73};
  for (unsigned field = 0; field < 3; ++field) {
    render(*v);
    click(*v, center(M::customEditorFieldRect(field + 2)));
    CTextEdit *edit = nullptr;
    for (int32_t i = 0; i < v->getFrame()->getNbViews(); ++i)
      if (auto *candidate =
              dynamic_cast<CTextEdit *>(v->getFrame()->getView(i)))
        edit = candidate;
    expect(edit != nullptr, "native numeric text editor opens");
    if (edit) {
      edit->setText(M::stringFormat("%.9g", typed[field]).c_str());
      v->valueChanged(edit);
      v->finishNumeric();
      const auto speaker = v->editableLayout().speakers[v->editableSelection()];
      const double actual[] = {speaker.azimuthDeg, speaker.elevationDeg,
                               speaker.distance};
      expect(near(actual[field], typed[field]),
             "typed AZ/EL/DST commits exact value");
    }
  }
  v->_page = 1;
  click(*v, center(M::layoutPopupActionRect()));
  expect(v->_layoutPanel && v->_layoutPanel->visible() && v->_page == 0,
         "POP OUT leaves main on matrix");
  auto *child = v->_layoutPopupView;
  expect(child && child->_page == 1, "detached layout editor");
  if (child) {
    const auto &layout = child->_plugin->dsp.outputLayout();
    std::array<M::Point, 64> nodes{};
    M::layoutNodePoints(layout, M::layoutProjectionRect(true), true, nodes);
    click(*child, nodes[0]);
    v->service();
    expect(v->_selectionIsOutput && v->_selectedOutput == 0,
           "detached selection synchronized");
    click(*child, center(M::layoutPopupActionRect()));
    v->service();
    expect(!v->_layoutPanel->visible() && v->_page == 1,
           "DOCK returns layout without destroying callback");
    v->openLayoutPopup();
    v->_layoutPanel->closed();
    expect(!v->_layoutPanel->visible() && v->_page == 0,
           "native close hides without docking");
    v->openLayoutPopup();
    main->hide();
    expect(!v->_layoutPanel->visible(), "host hide also hides detached window");
    main->show();
    v->openLayoutPopup();
  }
  main.reset(); // destruction with open child, no timer/native-parent survivors
#else
  (void)p;
#endif
}
} // namespace test
int main() {
  using namespace test;
#if defined(__APPLE__)
  [NSApplication sharedApplication];
#endif
  if (!foundation::acquireRuntime())
    return 2;
  expect(foundation::usingBundledFont() ==
             (std::getenv("S3G_MONITOR_EXPECT_FONT_FALLBACK") == nullptr),
         "font/fallback");
  unsigned requests = 0;
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Matrix parity";
  host.vendor = "s3g";
  host.url = "";
  host.version = "1";
  host.host_data = &requests;
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *h) {
    ++*static_cast<unsigned *>(h->host_data);
  };
  const auto *plugin = createPlugin(nullptr, &host, descriptor.id);
  expect(plugin && init(plugin) && activate(plugin, 48000., 128, 128),
         "plugin lifecycle");
  if (plugin) {
    auto &p = *self(plugin);
    {
      Editor v(p);
      Events events;
      render(v, "initial");
      factories(v, p, events);
      formats(v, p, events);
      crosspoints(v, p, events);
      customLayouts(v, p, events);
      presets(v, p, events);
      pressure(v, p, events);
      audio<float>(v, p, events);
      audio<double>(v, p, events);
      expect(requests > 0, "host notification requested");
    }
    nativeWindows(p);
    deactivate(plugin);
    destroy(plugin);
  }
  foundation::releaseRuntime();
  if (ok)
    std::cout << "Matrix Upmix canvas parity passed\n";
  return ok ? 0 : 1;
}
