#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_SCORE_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace VSTGUI;
using namespace score_encoder_canvas;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::resonator_drawing;
bool ok = true;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
    ok = false;
  }
}
bool near(double a, double b) { return std::abs(a - b) < 1.e-4; }
struct Events {
  std::vector<s3g::clap_gui::ParamEvent> events;
  bool reject = false;
  clap_output_events_t output{
      this, [](const clap_output_events_t *o, const clap_event_header_t *h) {
        auto &e = *static_cast<Events *>(o->ctx);
        if (e.reject)
          return false;
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
          const auto *v = reinterpret_cast<const clap_event_param_value_t *>(h);
          e.events.push_back({Kind::Value, v->param_id, v->value});
        } else {
          const auto *v =
              reinterpret_cast<const clap_event_param_gesture_t *>(h);
          e.events.push_back({h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                                  ? Kind::GestureBegin
                                  : Kind::GestureEnd,
                              v->param_id, 0.});
        }
        return true;
      }};
  void balanced() {
    std::map<clap_id, int> depth;
    for (auto e : events) {
      if (e.kind == Kind::GestureBegin)
        check(++depth[e.paramId] == 1, "nested edit gesture");
      else if (e.kind == Kind::GestureEnd)
        check(--depth[e.paramId] == 0, "unbalanced edit end");
      else
        check(depth[e.paramId] == 1, "value without edit gesture");
    }
    for (auto e : depth)
      check(e.second == 0, "unterminated edit gesture");
    events.clear();
  }
};
struct Stream {
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  clap_ostream_t output{
      this, [](const clap_ostream_t *o, const void *p, uint64_t n) -> int64_t {
        auto &s = *static_cast<Stream *>(o->ctx);
        n = std::min<uint64_t>(n, 23);
        const auto *b = static_cast<const uint8_t *>(p);
        s.bytes.insert(s.bytes.end(), b, b + n);
        return n;
      }};
  clap_istream_t input{
      this, [](const clap_istream_t *i, void *p, uint64_t n) -> int64_t {
        auto &s = *static_cast<Stream *>(i->ctx);
        n = std::min<uint64_t>({n, 17, s.bytes.size() - s.pos});
        std::memcpy(p, s.bytes.data() + s.pos, n);
        s.pos += n;
        return n;
      }};
};
CPoint center(D::Rect r) { return {D::midX(r), D::midY(r)}; }
void down(Editor &v, CPoint p, int clicks = 1) {
  MouseDownEvent e;
  e.mousePosition = p;
  e.clickCount = clicks;
  e.buttonState.add(MouseButton::Left);
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
void click(Editor &v, CPoint p) {
  down(v, p);
  up(v);
}
void flush(Plugin &p, Editor &v, Events &e) {
  paramsFlush(&p.plugin, nullptr, &e.output);
  v.service();
}
double value(Plugin &p, clap_id id) {
  double x = 0;
  check(paramsGetValue(&p.plugin, id, &x), "parameter read");
  return x;
}
void render(Editor &v, const std::string &suffix) {
  auto c = COffscreenContext::create(
      {v.getViewSize().getWidth(), v.getViewSize().getHeight()});
  check(bool(c), "offscreen context");
  if (!c)
    return;
  c->beginDraw();
  v.draw(c);
  c->endDraw();
  if (const auto *dir = std::getenv("S3G_SCORE_CAPTURE_DIR")) {
    const auto folder = F::pathFromUtf8(dir);
    std::filesystem::create_directories(folder);
    auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(
        c->getBitmap()->getPlatformBitmap());
    std::ofstream file(folder /
                           (std::string(descriptor.id) + "-" + suffix + ".png"),
                       std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), png.size());
    check(bool(file), "PNG capture");
  }
}
void set(Plugin &p, Editor &v, Events &e, clap_id id, double x) {
  v.editValue(id, x);
  flush(p, v, e);
}
void audio(Plugin &p, Editor &v, Events &e, int key = -1,
           unsigned blocks = 32) {
  std::array<std::array<float, 128>, kOutputChannels> output{};
  std::array<float *, kOutputChannels> ptrs{};
  for (unsigned i = 0; i < kOutputChannels; ++i)
    ptrs[i] = output[i].data();
  clap_audio_buffer_t buffer{};
  buffer.channel_count = kOutputChannels;
  buffer.data32 = ptrs.data();
  clap_process_t proc{};
  proc.frames_count = 128;
  proc.audio_outputs_count = 1;
  proc.audio_outputs = &buffer;
  proc.out_events = &e.output;
  clap_event_note_t note{};
  note.header = {sizeof(note), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON,
                 0};
  note.note_id = -1;
  note.port_index = 0;
  note.channel = 0;
  note.key = key;
  note.velocity = .8;
  clap_input_events_t notes{
      &note, [](const clap_input_events_t *) -> uint32_t { return 1; },
      [](const clap_input_events_t *x,
         uint32_t) -> const clap_event_header_t * {
        return &static_cast<clap_event_note_t *>(x->ctx)->header;
      }};
  for (unsigned b = 0; b < blocks; ++b) {
    proc.in_events = key >= 0 && b == 0 ? &notes : nullptr;
    check(process(&p.plugin, &proc) != CLAP_PROCESS_ERROR, "audio process");
    for (const auto &ch : output)
      for (float sample : ch)
        check(std::isfinite(sample), "finite audio");
    v.service();
  }
}
void slider(Editor &v, Plugin &p, Events &e, clap_id id, CPoint from,
            CPoint to) {
  const double before = value(p, id);
  down(v, from);
  move(v, to);
  up(v);
  flush(p, v, e);
  check(!near(before, value(p, id)), "slider drag changes correct parameter");
  down(v, from, 2);
  up(v);
  flush(p, v, e);
  clap_param_info_t info{};
  for (uint32_t n = 0; n < paramsCount(&p.plugin); ++n) {
    paramsGetInfo(&p.plugin, n, &info);
    if (info.id == id)
      break;
  }
  check(near(value(p, id), info.default_value), "slider double-click default");
}
void run(Plugin &p, Editor &v, Events &e) {
  render(v, "first-open");
#if S3G_SCORE_KIND == 1
  for (int page = 0; page < 4; ++page) {
    click(v, center(controlPageButtonRect(page)));
    check(v._controlPage == page, "Acid control page hit map");
    render(v, "page-" + std::to_string(page));
    for (const auto &row : kUiRows)
      if (row.page == unsigned(page) && !discreteUiMenuParam(row.id) &&
          row.id != kScaleParamId) {
        slider(v, p, e, row.id, {200., row.y + 5.}, {140., row.y + 5.});
      }
  }
  click(v, center(controlPageButtonRect(0)));
  const clap_id note = kStepParamBase + 3 * kStepParamStride;
  down(v, center(stepNoteRect(3)));
  move(v, {210., 110.});
  up(v);
  flush(p, v, e);
  check(v._selectedStep == 3, "Acid selected step");
  for (auto kind :
       {StepParamKind::Gate, StepParamKind::Accent, StepParamKind::Slide}) {
    const auto id = note + uint32_t(kind);
    const double old = value(p, id);
    click(v, center(stepToggleRect(3, kind)));
    flush(p, v, e);
    check(value(p, id) != old, "Acid G/A/S toggle");
  }
  KeyboardEvent key;
  key.type = EventType::KeyDown;
  key.virt = VirtualKey::Right;
  v.onKeyboardEvent(key);
  check(v._selectedStep == 4, "Acid keyboard selection");
  key.virt = VirtualKey::Up;
  key.modifiers.add(ModifierKey::Shift);
  v.onKeyboardEvent(key);
  flush(p, v, e);
  check(key.consumed, "Acid octave shortcut");
  for (unsigned view = 0; view < 2; ++view) {
    const auto box = spatialPathFieldRect(view);
    const auto at = center(box);
    down(v, at);
    move(v, {at.x + 45., at.y - 32.});
    up(v);
    flush(p, v, e);
    const auto base = kSpatialParamBase + v._selectedStep * kSpatialParamStride;
    check(std::abs(value(p, base + (view == 0 ? 1 : 2))) > .05,
          "Acid spatial editor drag");
    down(v, at, 2);
    up(v);
    flush(p, v, e);
    check(near(value(p, base),
               s3g::kAmbiAcidDefaultSpatialPath[v._selectedStep].x),
          "Acid spatial reset");
  }
  for (unsigned i = 0; i < s3g::kAmbiAcidPatternPresets.size(); ++i) {
    click(v, center(patternPresetButtonRect()));
    render(v, "preset-menu");
    const auto box = patternPresetMenuRect();
    click(v, {box.origin.x + 20., box.origin.y + 18. * (i + .5)});
    flush(p, v, e);
    check(publishedPatternPresetIndex(p) == int(i), "Acid factory pattern");
    render(v, "factory-" + std::to_string(i));
  }
  for (clap_id id : {kTransportSyncParamId, kDivisionParamId, kSubOctaveParamId,
                     kDriveCircuitParamId, kFormatMenuId}) {
    for (const auto &row : kUiRows)
      if (row.id == id)
        click(v, center(controlPageButtonRect(row.page)));
    v._parameterMenuId = id;
    render(v, "menu-" + std::to_string(id));
    const auto box = discreteUiMenuRect(id);
    click(v, {box.origin.x + 20., box.origin.y + 27.});
    flush(p, v, e);
    check(v._parameterMenuId == CLAP_INVALID_ID, "Acid discrete menu closes");
  }
  v._scaleMenuOpen = true;
  render(v, "scale-menu");
  v._scaleMenuOpen = false;
  v.randomizePattern();
  flush(p, v, e);
  v.resetSpatialPath();
  flush(p, v, e);
  constexpr clap_id toneId = kCutoffParamId;
#elif S3G_SCORE_KIND == 2
  for (unsigned i = 0; i < s3g::kAmbiHorizonFactoryPresetCount; ++i) {
    const auto out = value(p, kOutputParamId), order = value(p, kOrderParamId),
               listen = value(p, kFieldListenModeParamId);
    click(v, center(v.presetMenuRect()));
    render(v, "preset-menu");
    const auto box = v._openMenuRect;
    click(v, {box.origin.x + 20., box.origin.y + 21. * (i + .5)});
    flush(p, v, e);
    check(near(value(p, kPresetParamId), i), "Horizon factory menu");
    check(near(out, value(p, kOutputParamId)) &&
              near(order, value(p, kOrderParamId)) &&
              near(listen, value(p, kFieldListenModeParamId)),
          "Horizon factory safeguards");
    render(v, "factory-" + std::to_string(i));
  }
  for (int ecology = 0; ecology < 9; ++ecology) {
    set(p, v, e, kEcologyParamId, ecology);
    render(v, "ecology-" + std::to_string(ecology));
    const auto controls = generatorPanelControls(v._snapshot.ecology);
    for (unsigned row = 0; row < controls.rows.size(); ++row) {
      const auto id = controls.rows[row].id;
      if (id == CLAP_INVALID_ID)
        continue;
      const double x = kGeneratorsPanel.frame.x + 108,
                   y = layout::rowY(kGeneratorsPanel, row) + 5;
      slider(v, p, e, id, {x + 30, y}, {x + 70, y});
    }
  }
  for (const auto &spec : kGuiSliders) {
    const double x = spec.panel->frame.x + 108,
                 y = layout::rowY(*spec.panel, spec.row) + 5;
    slider(v, p, e, spec.id, {x + 20, y}, {x + 70, y});
  }
  for (int menu = 2; menu <= 6; ++menu) {
    v.openMenu(menu);
    render(v, "menu-" + std::to_string(menu));
    click(v, {v._openMenuRect.origin.x + 20., v._openMenuRect.origin.y + 31.5});
    flush(p, v, e);
    check(v._openMenu == 0, "Horizon menu closes");
  }
  for (int mode = 0; mode < 3; ++mode) {
    click(v, center(v.viewButtonRect(mode)));
    check(v._viewMode == mode, "Horizon camera");
  }
  const double zoom = v._viewZoom;
  click(v, center(v.zoomButtonRect(1)));
  check(v._viewZoom > zoom, "Horizon zoom");
  down(v, center(v.fieldRect()));
  move(v, {280., 270.});
  up(v);
  check(v._viewMode == -1, "Horizon orbit");
  v.randomize();
  flush(p, v, e);
  e.balanced();
  constexpr clap_id toneId = kRangeParamId;
#else
  for (int page = 0; page < 3; ++page) {
    click(v, center(v.pageButtonRect(page)));
    check(v._leftPage == page, "VOT page");
    render(v, "page-" + std::to_string(page));
  }
  click(v, center(v.pageButtonRect(1)));
  struct TestSlider {
    clap_id param;
    double x, y;
    int area;
  };
  static constexpr TestSlider sliders[]{
      {kOutputParamId, 638, 78, 1},
      {kVoicesParamId, 638, kVoiceCountRowY, 1},
      {kBaseNoteParamId, 638, 248, 1},
      {kTuneParamId, 638, 274, 1},
      {kPitchSpreadParamId, 638, 366, 1},
      {kDetuneParamId, 638, 392, 1},
      {kHarmonicsParamId, 638, 418, 1},
      {kSubharmonicsParamId, 638, 444, 1},
      {kAttackParamId, 638, 510, 1},
      {kDecayParamId, 638, 536, 1},
      {kSustainParamId, 638, 562, 1},
      {kReleaseParamId, 638, 588, 1},
      {kMotionRateParamId, 904, 130, 7},
      {kSyncDivisionParamId, 904, 156, 7},
      {kMotionAmountParamId, 904, 182, 7},
      {kSpreadParamId, 904, 208, 7},
      {kCoherenceParamId, 904, 234, 7},
      {kChaosParamId, 904, 260, 7},
      {kLinkParamId, 904, 286, 7},
      {kNeighborRadiusParamId, 904, 312, 7},
      {kRequiredNeighborsParamId, 904, 338, 7},
      {kSmoothParamId, 904, 364, 7},
      {kCenterAzimuthParamId, 904, 390, 7},
      {kCenterElevationParamId, 904, 416, 7},
      {kCenterDistanceParamId, 904, 442, 7},
      {kScoreDurationParamId, 904, 534, 7},
      {kScoreDepthParamId, 904, 560, 7},
  };

  for (const auto &row : sliders)
    slider(v, p, e, row.param, {row.x + 135, row.y + 5},
           {row.x + 166, row.y + 5});

  down(v, {105., 200.});
  move(v, {160., 140.});
  up(v);
  flush(p, v, e);
  check(value(p, kVectorXParamId) > .4 && value(p, kVectorYParamId) > .6,
        "VOT vector pad XY");
  click(v, center(v.pageButtonRect(2)));
  const auto before = loadScore(p);
  const auto content = v.leftContentRect();
  const auto timeline =
      D::makeRect(content.origin.x + 8, content.origin.y + 8, 540, 306);
  for (int lane = 0; lane < 2; ++lane) {
    const auto node = loadScore(p).nodes[2];
    CPoint from{timeline.origin.x + 14 + node.time * 512.,
                timeline.origin.y + (lane == 0 ? 34 : 174) +
                    (1. - (lane == 0 ? node.u : node.v)) * 112.};
    down(v, from);
    move(v, {from.x + 5., from.y - 12.});
    up(v);
  }
  const auto after = loadScore(p);
  check(after.nodes[2].u != before.nodes[2].u &&
            after.nodes[2].v != before.nodes[2].v,
        "VOT both score lanes");
  click(v, center(v.scoreAddButtonRect()));
  check(loadScore(p).nodeCount == before.nodeCount + 1, "VOT add score node");
  click(v, center(v.scoreRemoveButtonRect()));
  check(loadScore(p).nodeCount == before.nodeCount, "VOT remove score node");
  click(v, center(v.scoreResetButtonRect()));
  check(near(loadScore(p).nodes[2].u, before.nodes[2].u), "VOT reset score");
  for (int menu = 1; menu <= 8; ++menu) {
    unsigned count =
        menu == 6
            ? s3g::kMusicalScaleCount
            : menu == 1
                  ? 3
                  : menu == 2
                        ? 5
                        : menu == 3 ? 7 : menu == 4 ? 5 : menu == 5 ? 2 : 4;
    v.openMenu_count_x_y_width(menu, count, 738., 200., 124.);
    render(v, "menu-" + std::to_string(menu));
    auto box = v.openMenuRect();
    click(v, {box.origin.x + 10., box.origin.y + 5.});
    flush(p, v, e);
    check(v._openMenu == 0, "VOT menu selection");
  }
  const auto wav = F::pathToUtf8(std::filesystem::temp_directory_path() /
                                 "s3g-vot-é-波-atlas.wav");
  {
    std::ofstream file(F::pathFromUtf8(wav.c_str()), std::ios::binary);
    auto word = [&](uint32_t n, int bytes) {
      for (int i = 0; i < bytes; ++i)
        file.put(char(n >> (8 * i)));
    };
    file.write("RIFF", 4);
    word(36 + 4096 * 4, 4);
    file.write("WAVEfmt ", 8);
    word(16, 4);
    word(3, 2);
    word(1, 2);
    word(48000, 4);
    word(192000, 4);
    word(4, 2);
    word(32, 2);
    file.write("data", 4);
    word(4096 * 4, 4);
    for (int i = 0; i < 4096; ++i) {
      float f = std::sin(float(i % 256) * 2.f * s3g::kPi / 256.f);
      file.write(reinterpret_cast<char *>(&f), 4);
    }
  }
  check(v.loadWavePath(wav), "VOT Unicode WAV import");
  flush(p, v, e);
  check(value(p, kPresetParamId) == 4 && bool(std::atomic_load(&p.userBank)),
        "VOT USER bank selected");
  const auto bank = std::atomic_load(&p.userBank);
  check(!v.loadWavePath(wav + "missing") &&
            std::atomic_load(&p.userBank) == bank,
        "VOT invalid file preserves bank");
  std::filesystem::remove(F::pathFromUtf8(wav.c_str()));
  constexpr clap_id toneId = kVectorXParamId;
#endif
  v.finishGestures();
  flush(p, v, e);
  e.balanced();
  const auto preset =
      F::pathToUtf8(std::filesystem::temp_directory_path() /
                    (std::string(descriptor.id) + "-é-波.s3gpreset"));
  const double saved = value(p, toneId);
  check(v.presetFile(preset, true), "save Unicode user preset");
  set(p, v, e, toneId, saved * .7 + .01);
  set(p, v, e, kOutputParamId, -21.);
  check(v.presetFile(preset, false), "load Unicode user preset");
  flush(p, v, e);
  check(near(value(p, toneId), saved), "user preset restores voice");
  check(near(value(p, kOutputParamId), -21.), "user preset preserves OUT");
  {
    std::ofstream file(F::pathFromUtf8(preset.c_str()),
                       std::ios::binary | std::ios::trunc);
    file.put(6);
  }
  check(!v.presetFile(preset, false) && near(value(p, toneId), saved),
        "truncated preset rejected without mutation");
  std::filesystem::remove(F::pathFromUtf8(preset.c_str()));
  e.reject = true;
  v.singleEdit(toneId, saved);
  const auto room = v.events().available();
  flush(p, v, e);
  check(v.events().available() == room, "host backpressure retains events");
  e.reject = false;
  flush(p, v, e);
  e.balanced();
  check(activate(&p.plugin, 48000., 1, 128), "activate DSP");
  audio(p, v, e, 60, 32);
  render(v, "live");
#if S3G_SCORE_KIND == 2
  check(p.guiEntityCount.load() > 0, "Horizon live entities published");
#elif S3G_SCORE_KIND == 3
  check(v._trailCount > 0, "VOT voice trails published");
#endif
  Stream savedState;
  check(stateSave(&p.plugin, &savedState.output), "project save");
#if S3G_SCORE_KIND != 1
  const auto savedView = v._viewMode;
  v.setViewPreset((savedView + 1) % 3);
#endif
  check(stateLoad(&p.plugin, &savedState.input), "project load");
  v.service();
  render(v, "restored");
#if S3G_SCORE_KIND != 1
  check(v._viewMode == savedView, "camera recall while editor remains open");
#endif
#if S3G_SCORE_KIND == 3
  check(bool(std::atomic_load(&p.userBank)),
        "VOT USER atlas embedded in project");
#endif
  deactivate(&p.plugin);
  v.stopRefresh();
  flush(p, v, e);
  e.balanced();
}
} // namespace

int main() {
#if defined(__APPLE__)
  [NSApplication sharedApplication];
  NSWindow *window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)
                styleMask:NSWindowStyleMaskTitled
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [window setReleasedWhenClosed:NO];
#elif defined(_WIN32)
  HWND window =
      CreateWindowExW(0, L"STATIC", L"s3g environmental GUI test",
                      WS_OVERLAPPEDWINDOW, 0, 0, kGuiWidth, kGuiHeight, nullptr,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Score encoder parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = host.request_restart =
      host.request_callback = [](const clap_host_t *) {};
#if S3G_SCORE_KIND == 1
  auto *plugin = createPlugin(nullptr, &host, descriptor.id);
#else
  auto *plugin = create(&host);
#endif
  check(plugin && plugin->init(plugin), "create plugin");
  if (!plugin)
    return 1;
  auto &p = *self(plugin);
  Events events;
  const auto *gui = static_cast<const clap_plugin_gui_t *>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  check(gui && gui->create(plugin, s3g::clap_gui::portable::windowApi(), false),
        "portable GUI create");
  if (p.portableGuiEditor) {
    auto &editor = *static_cast<Editor *>(p.portableGuiEditor->contentView());
    clap_window_t parent{};
#if defined(__APPLE__)
    parent.api = CLAP_WINDOW_API_COCOA;
    parent.cocoa = [window contentView];
#elif defined(_WIN32)
    parent.api = CLAP_WINDOW_API_WIN32;
    parent.win32 = window;
#endif
    check(gui->set_parent(plugin, &parent) && gui->show(plugin),
          "native parent/show");
    for (double scale : {.65, 1., 1.5, 2.}) {
      uint32_t w = std::lround(kGuiWidth * scale),
               h = std::lround(kGuiHeight * scale);
      check(gui->adjust_size(plugin, &w, &h) && gui->set_size(plugin, w, h),
            "65-200 percent resizing");
      check(std::abs(double(w) / h - double(kGuiWidth) / kGuiHeight) < .002,
            "proportional resize");
    }
    gui->set_size(plugin, kGuiWidth, kGuiHeight);
    run(p, editor, events);
    check(gui->hide(plugin), "GUI hide");
    gui->destroy(plugin);
    gui->destroy(plugin);
  }
  plugin->destroy(plugin);
#if defined(__APPLE__)
  [window close];
  [window release];
#elif defined(_WIN32)
  DestroyWindow(window);
#endif
  if (ok)
    std::cout << "Score encoder canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
