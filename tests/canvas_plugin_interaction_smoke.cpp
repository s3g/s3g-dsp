// Exercise the actual migrated editors and original state/worker/DSP actions.
#if defined(S3G_TEST_SLICER_CANVAS)
#include "../plugins/clap_breakbeat_slicer/s3g_breakbeat_slicer_clap.cpp"
#else
#include "../plugins/clap_ambi_grain_processor/s3g_ambi_grain_processor_clap.cpp"
#include "../plugins/common/s3g_audio_file_export.h"
#endif
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace canvas_test {
using namespace VSTGUI;
namespace foundation = s3g::portable_gui::foundation;
bool ok = true;
void expect(bool value, const char *message) {
  if (!value) {
    ok = false;
    std::cerr << message << '\n';
  }
}
struct Memory {
  std::vector<uint8_t> bytes;
  size_t offset = 0;
  clap_ostream_t output{
      this, [](const clap_ostream_t *s, const void *p, uint64_t n) -> int64_t {
        auto &m = *static_cast<Memory *>(s->ctx);
        const auto *b = static_cast<const uint8_t *>(p);
        m.bytes.insert(m.bytes.end(), b, b + n);
        return int64_t(n);
      }};
  clap_istream_t input{
      this, [](const clap_istream_t *s, void *p, uint64_t n) -> int64_t {
        auto &m = *static_cast<Memory *>(s->ctx);
        n = std::min<uint64_t>(n, m.bytes.size() - m.offset);
        std::memcpy(p, m.bytes.data() + m.offset, size_t(n));
        m.offset += size_t(n);
        return int64_t(n);
      }};
};
struct Events {
  std::vector<uint16_t> types;
  clap_output_events_t output{
      this, [](const clap_output_events_t *q, const clap_event_header_t *e) {
        static_cast<Events *>(q->ctx)->types.push_back(e->type);
        return true;
      }};
};
template <class View>
void click(View &v, double x, double y, int count = 1, bool right = false) {
  MouseDownEvent e;
  e.mousePosition = {x, y};
  e.clickCount = count;
  e.buttonState.add(right ? MouseButton::Right : MouseButton::Left);
  v.onMouseDownEvent(e);
  MouseUpEvent up;
  up.mousePosition = {x, y};
  v.onMouseUpEvent(up);
}
template <class View> void drag(View &v, CPoint a, CPoint b) {
  MouseDownEvent e;
  e.mousePosition = a;
  e.buttonState.add(MouseButton::Left);
  v.onMouseDownEvent(e);
  MouseMoveEvent move;
  move.mousePosition = b;
  move.buttonState.add(MouseButton::Left);
  v.onMouseMoveEvent(move);
  MouseUpEvent up;
  up.mousePosition = b;
  v.onMouseUpEvent(up);
}
template <class View> void render(View &view, const char *name) {
  auto context = COffscreenContext::create(CPoint(kGuiWidth, kGuiHeight));
  expect(bool(context), "offscreen context");
  if (!context)
    return;
  context->beginDraw();
  view.draw(context);
  context->endDraw();
  const char *capture = std::getenv("S3G_CANVAS_CAPTURE_DIR");
  if (capture && capture[0]) {
    auto bytes = getPlatformFactory().createBitmapMemoryPNGRepresentation(
        context->getBitmap()->getPlatformBitmap());
    const auto path =
        std::filesystem::u8path(capture) / (std::string(name) + ".png");
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes.data()),
               std::streamsize(bytes.size()));
    expect(bool(file), "write capture");
  }
}
bool waitFor(const std::function<bool()> &ready,
             const std::function<void()> &service) {
  for (unsigned i = 0; i < 3000; ++i) {
    service();
    if (ready())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}
} // namespace canvas_test

#if !defined(S3G_TEST_SLICER_CANVAS)
#include "ambi_grain_storage_smoke.inc"
#endif

int main() {
  using namespace canvas_test;
  if (!foundation::acquireRuntime())
    return 2;
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("s3g-canvas-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(directory))
    return 2;
  const auto wave = directory / std::filesystem::u8path(u8"音 démo.wav");
  std::array<std::vector<float>, 4> audio;
  std::array<const float *, 4> pointers{};
  for (size_t ch = 0; ch < 4; ++ch) {
    audio[ch].resize(24000);
    for (size_t i = 0; i < audio[ch].size(); ++i)
      audio[ch][i] = float(std::sin(double(i) * .09) *
                           (1. - double(i % 3000) / 3000.) * .4 / (ch + 1));
    pointers[ch] = audio[ch].data();
  }
  std::string error;
  expect(s3g::audio_file::writePlanarFloatWaveAtomically(
             wave.u8string(), 48000, 4, 24000, pointers.data(), error),
         "write multichannel unicode fixture");
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "canvas test";
  host.vendor = "s3g";
  host.url = "";
  host.version = "1";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *) {};
  std::atomic<unsigned> callbackRequests{0};
  host.host_data = &callbackRequests;
  host.request_callback = [](const clap_host_t *h) {
    static_cast<std::atomic<unsigned> *>(h->host_data)->fetch_add(1);
  };
  const auto *plugin = factory.create_plugin(
      &factory, &host, factory.get_plugin_descriptor(&factory, 0)->id);
  expect(plugin && plugin->init(plugin), "plugin init");
  if (!plugin)
    return 2;
  auto &p = *self(plugin);
  const auto *stateExt = static_cast<const clap_plugin_state_t *>(
      plugin->get_extension(plugin, CLAP_EXT_STATE));
  const auto *gui = static_cast<const clap_plugin_gui_t *>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  clap_gui_resize_hints_t hints{};
  expect(gui && gui->get_resize_hints(plugin, &hints) &&
             hints.preserve_aspect_ratio,
         "proportional hints");
  uint32_t w = 1, h = 1;
  expect(gui->adjust_size(plugin, &w, &h) &&
             w == uint32_t(std::lround(kGuiWidth * .65)) &&
             h == uint32_t(std::lround(kGuiHeight * .65)),
         "65 percent minimum");
  w = kGuiWidth * 4;
  h = kGuiHeight * 4;
  expect(gui->adjust_size(plugin, &w, &h) && w == kGuiWidth * 2 &&
             h == kGuiHeight * 2,
         "200 percent maximum");
#if defined(S3G_TEST_SLICER_CANVAS)
  {
    p.storageMode = s3g::sample_storage::StorageMode::Link;
    slicer_canvas::Editor v(p);
    v.loadPaths({wave.u8string()}, 0);
    expect(waitFor([&] { return bool(p.controlBank->slots[0].asset); },
                   [&] { v.service(); }),
           "Slicer async import");
    expect(p.controlBank->slots[0].asset &&
               p.controlBank->slots[0].asset->channelCount == 4,
           "Slicer channel count");
    v.makeEqual(8);
    expect(p.controlBank->slots[0].sliceCount == 8 &&
               !slotHasCompleteMap(p.controlBank->slots[0]),
           "equal slicing invalidates map");
    expect(automapSlot(p, 0), "Slicer auto map");
    p.voicePlayheads[0].store(.12f);
    p.voicePlayheadSlots[0].store(0);
    p.voicePlayheadKeys[0].store(48);
    render(v, "slicer-overview");
    click(v, 420, 54);
    render(v, "slicer-break-edit");
    const auto count = p.controlBank->slots[0].sliceCount;
    click(v, 420, 449);
    click(v, 420, 469); // select EQUAL 4; do not execute yet
    expect(p.controlBank->slots[0].sliceCount == count,
           "method menu must not slice immediately");
    click(v, 550, 422);
    expect(p.controlBank->slots[0].sliceCount == 4,
           "SLICE applies selected method");
    expect(automapSlot(p, 0), "remap");
    v.setNumeric(slicer_canvas::kDetailNumericPreRoll, 12345);
    v.setNumeric(slicer_canvas::kDetailNumericMinimumSlice, 37);
    expect(p.transientPreRollMicroseconds == 12345 &&
               p.minimumTransientSliceMilliseconds == 37,
           "exact transient fields");
    v.setNumeric(slicer_canvas::kDetailNumericSlicePitch, -72.5);
    expect(p.controlBank->slots[0].slices[0].transposeSemitones == -72.5f,
           "exact pitch wider than slider range");
    v.setNumeric(slicer_canvas::kDetailNumericSlicePan, .8);
    expect(p.controlBank->slots[0].slices[0].pan == 0, "multichannel pan lock");
    const auto marker = p.controlBank->slots[0].slices[1].startFrame;
    drag(v, {v.xForFrame(marker, 24000), 200},
         {v.xForFrame(marker + 600, 24000), 200});
    expect(p.controlBank->slots[0].slices[1].startFrame != marker &&
               slotHasCompleteMap(p.controlBank->slots[0]),
           "marker drag preserves map");
    const auto before = p.controlBank->slots[0].sliceCount;
    click(v, 350, 210, 2);
    expect(p.controlBank->slots[0].sliceCount == before + 1 &&
               !slotHasCompleteMap(p.controlBank->slots[0]),
           "double-click marker add");
    const auto inserted = p.controlBank->slots[0].slices[1].startFrame;
    click(v, v.xForFrame(inserted, 24000), 210, 1, true);
    expect(p.controlBank->slots[0].sliceCount == before,
           "right-click marker removal");
    expect(automapSlot(p, 0), "map after marker delete");
    auto bank = editableBank(p);
    bank->slots[0].slices[0].launchMode = s3g::breakbeat::LaunchMode::Loop;
    publishBank(p, bank);
    click(v, 305, 210);
    drag(v, {294, 200}, {315, 200});
    expect(p.controlBank->slots[0].slices[0].loopStartFrame > 0,
           "loop start drag");
    MouseWheelEvent wheel;
    wheel.mousePosition = {500, 200};
    wheel.deltaY = 1;
    v.onMouseWheelEvent(wheel);
    expect(v.visibleFrames(24000) < 24000, "waveform zoom");
    drag(v, {310, 353}, {1000, 353});
    expect(v.clampedStart(24000) > 0, "waveform navigator pan");
    drag(v, {80, 577}, {200, 577});
    expect(p.controlBank->slots[0].envelope.attackProportion > 0,
           "break envelope drag");
    render(v, "slicer-break-edit-interactions");
    click(v, 528, 54);
    render(v, "slicer-mixer");
    drag(v, {60, 610}, {60, 520});
    expect(p.controlBank->slots[0].mixerGain > 0, "mixer fader");
    click(v, 50, 413);
    expect(p.controlBank->slots[0].muted, "mixer mute");
    click(v, 50, 413);
    click(v, 135, 303);
    click(v, 1000, 268); // Resonator assignment in insert 1
    expect(p.controlBank->slots[0].inserts[0].type == InsertType::Resonator,
           "insert assignment");
    drag(v, {740, 451}, {970, 451});
    expect(p.controlBank->slots[0].inserts[0].values[0] > .5,
           "insert parameter drag");
    render(v, "slicer-insert");
    click(v, 1000, 332); // Close editor
    const auto oldSat = p.controlBank->auxSaturation;
    click(v, 963, 705);
    drag(v, {745, 438}, {970, 438});
    expect(p.controlBank->auxFieldSafe &&
               p.controlBank->auxSaturation == oldSat,
           "field-safe nonlinear controls disabled");
    Events events;
    drag(v, {750, 123}, {995, 123});
    serviceGuiParams(p, &events.output);
    expect(events.types.size() >= 3 &&
               events.types.front() == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
               events.types.back() == CLAP_EVENT_PARAM_GESTURE_END,
           "balanced output automation gesture");
    // Every operation and alternate mode uses the existing render worker.
    for (uint32_t op = 0; op < 5; ++op)
      for (uint32_t alt = 0; alt < 2; ++alt) {
        p.selectedSlot = 0;
        for (uint32_t i = 1; i < 4; ++i) {
          p.selectedSlot = i;
          v.clearSelectedSlot();
        }
        p.selectedSlot = 0;
        p.structuralMutationOperation =
            static_cast<StructuralMutationOperation>(op);
        p.structuralMutationAlternate = alt != 0;
        const auto queued = fillEmptySlotsWithMutations(p, 0);
        expect(queued == 3, "mutation queues all empty slots");
        expect(waitFor(
                   [&] {
                     return std::none_of(p.pendingMutationSlots.begin(),
                                         p.pendingMutationSlots.end(),
                                         [](bool pending) { return pending; });
                   },
                   [&] { v.service(); }),
               "mutation worker completion");
        for (uint32_t i = 1; i < 4; ++i)
          expect(p.controlBank->slots[i].asset &&
                     slotHasCompleteMap(p.controlBank->slots[i]),
                 "mutation retains rendered audio and complete map");
        expect(fillEmptySlotsWithMutations(p, 0) == 0,
               "mutation never overwrites occupied slots");
      }
    click(v, 604, 54);
    render(v, "slicer-mutate");
    click(v, 600, 338);
    expect(p.pendingPlaythroughSlot.load() == 1, "play through command");
    const auto exported = directory / "rendered.wav";
    expect(queueMutationExport(p, 0, exported.u8string()), "queue WAV export");
    expect(waitFor([&] { return std::filesystem::exists(exported); },
                   [&] { v.service(); }),
           "atomic WAV export completes");
    std::shared_ptr<const SampleAsset> decoded;
    std::shared_ptr<const SampleAnalysis> analysis;
    expect(decodeSampleFile(exported.u8string(), decoded, analysis, error) &&
               decoded->channelCount == 4,
           "export preserves source channel count");
    std::filesystem::remove(exported);
    p.storageMode = s3g::sample_storage::StorageMode::Embed;
    Memory memory;
    expect(stateExt->save(plugin, &memory.output),
           "save migrated Slicer state");
    v.clearSelectedSlot();
    expect(stateExt->load(plugin, &memory.input) &&
               p.controlBank->slots[0].asset,
           "restore migrated Slicer state");
    expect(p.transientPreRollMicroseconds == 12345 &&
               p.minimumTransientSliceMilliseconds == 37,
           "restore exact slicing settings");
  }
#if defined(__APPLE__)
  // Native AppKit text controls require a desktop session; the default
  // interaction suite remains usable in headless/sandboxed CI.
  if (std::getenv("S3G_CANVAS_NATIVE_TEXT_TEST")) {
    NSView *parent = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, kGuiWidth * 1.5, kGuiHeight * 1.5)];
    clap_window_t window{};
    window.api = CLAP_WINDOW_API_COCOA;
    window.cocoa = (__bridge void *)parent;
    expect(gui->create(plugin, CLAP_WINDOW_API_COCOA, false) &&
               gui->set_size(plugin, kGuiWidth * 3 / 2, kGuiHeight * 3 / 2) &&
               gui->set_parent(plugin, &window) && gui->show(plugin),
           "scaled Slicer hosted editor");
    auto *view = static_cast<slicer_canvas::Editor *>(
        p.portableGuiEditor->contentView());
    click(*view, 420, 54);
    render(*view, "slicer-numeric-entry");
    click(*view, 630, 477);
    CTextEdit *field = nullptr;
    for (int32_t i = 0; i < view->getFrame()->getNbViews(); ++i)
      if (auto *candidate =
              dynamic_cast<CTextEdit *>(view->getFrame()->getView(i)))
        field = candidate;
    expect(field != nullptr,
           "click exact value opens shared text editor at 150 percent scale");
    if (field) {
      field->setText("7654");
      view->valueChanged(field);
      expect(p.transientPreRollMicroseconds == 7654,
             "typed exact number commits");
      field->setText("not a number");
      view->valueChanged(field);
      expect(p.transientPreRollMicroseconds == 7654,
             "invalid numeric input retains prior value");
      view->finishNumeric();
    }
    expect(gui->hide(plugin), "hide after numeric edit");
    gui->destroy(plugin);
  }
#endif
#else
  {
    expect(p.storageMode == StorageMode::Project,
           "new Ambi Grain defaults to PROJECT");
    queuePortableSampleLoad(p, wave.u8string());
    expect(waitFor([&] { return bool(std::atomic_load(&p.sample)); },
                   [&] {
                     if (callbackRequests.exchange(0))
                       plugin->on_main_thread(plugin);
                   }),
           "Ambi Grain async unicode import completes without an editor");
    ambi_grain_canvas::Editor v(p);
    auto sample = std::atomic_load(&p.sample);
    expect(sample && sample->channels == 4 && sample->frames == 24000,
           "Ambi Grain sample topology");
    if (sample)
      expect(std::abs(sample->audio[4] / sample->audio[5] - 2) < .001,
             "common field normalization preserves channel ratio");
    render(v, "ambi-grain");
    Events events;
    drag(v, {770, 196}, {820, 196});
    serviceGuiParams(p, &events.output);
    expect(events.types.size() >= 3 &&
               events.types.front() == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
               events.types.back() == CLAP_EVENT_PARAM_GESTURE_END,
           "Ambi Grain balanced slider gesture");
    for (int mode = 0; mode < 4; ++mode) {
      v.change(kModeParamId, mode);
      render(v, ("ambi-grain-mode-" + std::to_string(mode)).c_str());
    }
    v.change(kModeParamId, 1);
    serviceGuiParams(p, &events.output);
    render(v, "ambi-grain-cloud");
    const double old = v.value(kSourcePosParamId);
    click(v, 810, 342);
    expect(v.value(kSourcePosParamId) == old,
           "inactive cloud source position cannot edit");
    Memory memory;
    expect(stateExt->save(plugin, &memory.output), "Ambi Grain state save");
    v.change(kModeParamId, 0);
    serviceGuiParams(p, &events.output);
    expect(stateExt->load(plugin, &memory.input) && v.value(kModeParamId) == 1,
           "Ambi Grain preset recall");
    queuePortableSampleLoad(p, (directory / "missing.wav").u8string());
    expect(waitFor([&] { return !p.sampleLoad.valid(); }, [&] { v.service(); }),
           "failed import completes");
    expect(std::atomic_load(&p.sample) != nullptr,
           "failed import retains current sample");
    queuePortableSampleLoad(p, wave.u8string());
    queuePortableSampleLoad(p, (directory / "missing-latest.wav").u8string());
    expect(waitFor([&] { return !p.sampleLoad.valid(); }, [&] { v.service(); }),
           "latest queued load completes");
    expect(!p.sampleStatus.empty() && std::atomic_load(&p.sample) != nullptr,
           "superseded file read cannot replace the current asset");
    queuePortableSampleLoad(p, wave.u8string());
    memory.offset = 0;
    expect(stateExt->load(plugin, &memory.input),
           "recall while import pending");
    const auto recalled = std::atomic_load(&p.sample);
    expect(waitFor([&] { return !p.sampleLoad.valid(); }, [&] { v.service(); }),
           "discard obsolete import after recall");
    expect(std::atomic_load(&p.sample) == recalled,
           "preset recall wins over a pending import");
  }
  exerciseAmbiStorage(plugin, host, wave, directory, callbackRequests);
  queuePortableSampleLoad(p, wave.u8string()); // Destruction joins the reader.
#endif
  plugin->destroy(plugin);
  std::filesystem::remove(wave);
  std::filesystem::remove(directory);
  foundation::releaseRuntime();
  std::cout << (ok ? "Canvas interaction smoke passed\n"
                   : "Canvas interaction smoke FAILED\n");
  return ok ? 0 : 1;
}
