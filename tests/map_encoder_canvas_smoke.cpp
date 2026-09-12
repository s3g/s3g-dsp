#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_MAP_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace VSTGUI;
using namespace map_encoder_canvas;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::map_drawing;
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
  if (const auto *dir = std::getenv("S3G_MAP_CAPTURE_DIR")) {
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

void restore(Plugin& p, Editor& v, Events& events, Stream& state) {
  state.pos = 0;
  check(stateLoad(&p.plugin, &state.input), "restore original state");
  flush(p, v, events);
}
void set(Plugin& p, Editor& v, Events& events, clap_id id, double x) {
  v.editSet(id, x);
  flush(p, v, events);
#if S3G_MAP_KIND != 3
  v.refreshControlSnapshot();
#endif
}
void drag(Plugin& p, Editor& v, Events& events, clap_id id, CPoint a, CPoint b) {
  down(v, a);
  check(v._dragParam == id, "original slider hit map");
  v.service();
  flush(p, v, events);
  const auto old = value(p, id);
  for (unsigned i = 0; i < 20; ++i)
    move(v, {a.x + (b.x - a.x) * i / 19., b.y});
  up(v);
  flush(p, v, events);
  check(value(p, id) != old, "slider drag reaches host");
  events.balanced();
}
void specifics(Plugin& p, Editor& v, Events& e) {
#if S3G_MAP_KIND == 1
  // Every original slider hit row on every tab (including logarithmic RATE).
  for (int page = 0; page < 5; ++page) {
    click(v, center(v.pathTabRect(page)));
    check(v._surfacePage == page, "original terrain tabs");
    render(v, "page-" + std::to_string(page));
    for (double y = 78; y < 750; y += 1) {
      const clap_id id = v.paramAtPoint({760, y});
      if (!id || (y > 78 && v.paramAtPoint({760, y - 1}) == id)) continue;
      drag(p, v, e, id, {750, y + 7}, {794, y + 7});
    }
  }
  const int pages[] = {0, 0, 2, 0, 0, 1, 4};
  const clap_id ids[] = {kOrderParamId, kOrbitParamId, kPaletteParamId,
      kPlaybackParamId, kSyncParamId, kTerrainFormParamId, kTerrainReadParamId};
  for (int menu = 1; menu <= 7; ++menu) {
    click(v, center(v.pathTabRect(pages[menu - 1])));
    click(v, center(v.menuControlRect(menu)));
    check(v._openMenu == menu, "original Surface menu hit");
    render(v, "menu-" + std::to_string(menu));
    const auto count = v._menuItemCount;
    click(v, {746, v.menuY() + (count - .5) * 18});
    flush(p, v, e);
    check(value(p, ids[menu - 1]) == count - (menu == 1 ? 0 : 1), "Surface menu mapping");
  }
  const auto previousAzimuth = v._viewAzDeg;
  down(v, {320, 340}); move(v, {350, 365}); up(v);
  check(v._viewMode == -1 && v._viewAzDeg != previousAzimuth, "Surface camera drag");
  for (int camera = 0; camera < 3; ++camera) {
    click(v, center(v.viewButtonRect_inRect(camera, v.fieldPanelRect())));
    check(v._viewMode == camera, "Surface camera presets");
    const auto generation = v._surfaceRenderCoordinator->generation.load();
    for (int wait = 0; wait < 200; ++wait) {
      v.service();
      if (v._surfaceImageCacheValid) {
        // A short settling period also exercises the asynchronous cache tail.
        if (wait >= 20) break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(generation == v._surfaceRenderCoordinator->generation.load() &&
        v._surfaceImageCacheValid, "background mesh published");
    render(v, "camera-" + std::to_string(camera));
  }
#elif S3G_MAP_KIND == 2
  for (int motion = 0; motion < 2; ++motion)
    for (int trace : {0, 4})
      for (int page = 0; page < 4; ++page) {
        set(p, v, e, kMotionModeParamId, motion);
        set(p, v, e, kTraceParamId, trace);
        click(v, center(v.terrainTabRect(page)));
        check(v._terrainPage == page, "Wave terrain tab");
        render(v, "motion-" + std::to_string(motion) + "-trace-" +
            std::to_string(trace) + "-page-" + std::to_string(page));
        for (const auto& row : kGuiRows) {
          if (row.menu || !guiRowVisible(row, v._paramsSnapshot.motionMode, page, v._paramsSnapshot.trace)) continue;
          const auto layout = waveGuiLayout(v._paramsSnapshot.motionMode, page, v._paramsSnapshot.trace);
          const double y = effectiveGuiRowY(row, layout, v._paramsSnapshot.motionMode, page, v._paramsSnapshot.trace);
          drag(p, v, e, row.param, {row.panelX + 119, y + 5}, {row.panelX + 172, y + 5});
        }
      }
  for (const auto& row : kGuiRows) {
    if (!row.menu) continue;
    set(p, v, e, kMotionModeParamId,
        static_cast<int>(s3g::AmbiWaveTerrainMotionMode::Field));
    const int page = terrainPageForParam(row.param);
    v._terrainPage = std::max(0, page);
    const auto layout = waveGuiLayout(v._paramsSnapshot.motionMode, v._terrainPage, v._paramsSnapshot.trace);
    const double y = effectiveGuiRowY(row, layout, v._paramsSnapshot.motionMode, v._terrainPage, v._paramsSnapshot.trace);
    click(v, {row.panelX + 120, y + 5});
    check(v._openMenuParam == row.param, "Wave menu hit");
    render(v, "menu-" + std::to_string(row.param));
    const auto rect = v.openMenuRect();
    const auto count = v._menuItemCount;
    if (row.param == kPitchScaleParamId) {
      check(count == 102u, "all 102 musical scale choices retained");
      const auto rows = D::multiColumnMenuRows(count, 4);
      for (uint32_t item = 0; item < count; ++item) {
        const D::Point point{rect.origin.x + (item / rows) * 180 + 30,
            rect.origin.y + (item % rows + .5) * 18};
        check(v.openMenuHit(point) == int(item), "scale column-major hit map");
      }
      const unsigned item = count - 1;
      click(v, {rect.origin.x + (item / rows) * 180 + 30,
          rect.origin.y + (item % rows + .5) * 18});
      flush(p, v, e);
      check(value(p, row.param) == s3g::musicalScaleValueForMenuIndex(item - 1) + 1u, "stable scale ID");
    } else {
      click(v, {rect.origin.x + 10, rect.origin.y + (count - .5) * 18});
      flush(p, v, e);
      check(value(p, row.param) == count - (row.param == kOrderParamId ? 0 : 1), "Wave menu mapping");
    }
  }
  v.refreshControlSnapshot();
  auto region = v.activeRegion(0);
  auto uv = v.contourUv_phase(region, .25f);
  auto point = v.project_rect_depth(v.worldPoint(v.displaySurfacePointU_v(uv[0], uv[1])), v.fieldRect(), nullptr);
  const int hit = v.hitVoice(point);
  check(hit >= 0, "visible scan contour hit");
  click(v, point);
  check(v._selectedVoice == uint32_t(hit), "contour click selects voice");
  down(v, point); move(v, {point.x + 30, point.y + 20}); up(v);
  check(v._viewMode == -1, "Wave camera drag");
#else
  set(p, v, e, kOutputParamId, -13.);
  set(p, v, e, kOrderParamId, 4.);
  for (unsigned preset = 0; preset < kFactoryPresetCount; ++preset) {
    const auto band = D::encoderTitleBand(kGuiWidth, kGuiHeight);
    click(v, center(D::cocoaRect(band.presetMenu)));
    check(v._openMenu == kFactoryPresetMenuId, "Cartography factory menu opens");
    const auto rect = v.openMenuRect();
    if (!preset) render(v, "factory-menu");
    click(v, {rect.origin.x + 10, rect.origin.y + (preset + .5) * 18});
    flush(p, v, e);
    check(v._factoryPresetIndex == int(preset), "all Cartography factory scenes");
    check(p.params.activeSites == kFactoryPresets[preset].sites, "factory site count");
    check(near(value(p, kOutputParamId), -13.) && value(p, kOrderParamId) == 4.,
        "factory recall preserves OUT and ORDER");
    render(v, "preset-" + std::to_string(preset));
  }
  for (int engine = 0; engine < 8; ++engine) {
    set(p, v, e, kMacroEngineParamId, engine);
    for (int page = 0; page < 2; ++page) {
      click(v, center(processPageButtonRect(page)));
      check(v._processPage == page, "CORE / REL pages");
      render(v, "engine-" + std::to_string(engine) + "-page-" + std::to_string(page));
    }
  }
  for (int page = 0; page < 3; ++page) {
    click(v, center(landscapePageButtonRect(page)));
    check(v._landscapePage == page, "MAP / PATH / FIELD pages");
    render(v, "landscape-" + std::to_string(page));
  }
  for (const auto& control : kGuiControls) {
    v._processPage = std::max(0, processPageForParam(control.id));
    v._landscapePage = std::max(0, landscapePageForParam(control.id));
    if (control.id == kShredCircuitParamId) set(p, v, e, kMacroEngineParamId, 3);
    if (control.id == kFractureProcessorParamId) set(p, v, e, kMacroEngineParamId, 4);
    const auto count = menuItemCount(control.id);
    if (count) {
      click(v, center(controlMenuBoxRect(control)));
      check(v._openMenu == control.id, "Cartography menu hit");
      render(v, "menu-" + std::to_string(control.id));
      auto rect = v.openMenuRect();
      click(v, {rect.origin.x + 10, rect.origin.y + (count - .5) * 18});
      flush(p, v, e);
      check(value(p, control.id) == menuValue(control.id, count - 1), "Cartography menu mapping");
    } else if (control.kind == CartographyControlKind::Toggle) {
      const auto before = value(p, control.id);
      click(v, center(controlHitRect(control)));
      flush(p, v, e);
      check(value(p, control.id) != before, "toggle reaches host");
    } else {
      auto panel = panelRect(control.panel);
      const double x = s3g::gui_layout::processorControlX(panel.origin.x);
      drag(p, v, e, control.id, {x + 8, controlY(control) + 5},
          {x + s3g::gui_layout::processorTrackWidth(panel.size.width) * .7, controlY(control) + 5});
    }
  }
  set(p, v, e, kMotionParamId, 0);
  set(p, v, e, kListenerXParamId, .1);
  set(p, v, e, kListenerYParamId, .2);
  set(p, v, e, kListenerZParamId, .3);
  for (int view = 0; view < 2; ++view) {
    v.setViewPreset(view); v.service();
    const auto saved = v._snapshot.params;
    auto point = v.projectWorldPointX_y_z(saved.listenerX, saved.listenerY, saved.listenerZ);
    down(v, point);
    check(v._dragListener, "listener cursor hit");
    move(v, {point.x + 24, point.y - 16}); up(v);
    flush(p, v, e);
    check(value(p, kListenerXParamId) != saved.listenerX, "listener X drag");
    check(near(value(p, view ? kListenerYParamId : kListenerZParamId),
        view ? saved.listenerY : saved.listenerZ), "drag preserves hidden coordinate");
    render(v, "camera-" + std::to_string(view));
  }
  // Authored site edits use the same XY/XZ planes as the original Cocoa map.
  set(p, v, e, kSitesParamId, 1.);
  set(p, v, e, kSiteParamId, 1.);
  for (int view = 0; view < 2; ++view) {
    set(p, v, e, kSiteXParamId, -.5);
    set(p, v, e, kSiteYParamId, .6);
    set(p, v, e, kSiteZParamId, .5);
    v.setViewPreset(view); v.service();
    const auto saved = v._snapshot.authored[0];
    auto point = v.projectWorldPointX_y_z(saved.x, saved.y, saved.z);
    down(v, point);
    check(v._dragSite == 0, "authored site cursor hit");
    move(v, {point.x + 25, point.y - 14}); up(v);
    flush(p, v, e);
    check(value(p, kSiteXParamId) != saved.x, "site X drag");
    check(near(value(p, view ? kSiteYParamId : kSiteZParamId),
        view ? saved.y : saved.z), "site drag preserves hidden coordinate");
  }
  // No flush between two factory scenes and a manual edit: layout resets must
  // remain in their original position in the queue, not overtake that edit.
  v.applyFactoryPreset(1);
  v.applyFactoryPreset(2);
  v.editSet(kSiteXParamId, .31);
  flush(p, v, e);
  check(near(value(p, kSiteXParamId), .31), "ordered factory reset / manual edit");
#endif
  e.balanced();
}
void run(Plugin& p, Editor& v, Events& e) {
  Stream initial;
  check(stateSave(&p.plugin, &initial.output), "initial state");
  render(v, "first-open");
#if S3G_MAP_KIND == 1
  // Wait for the latest asynchronous mesh, not a stale image from a prior tab.
  for (int i = 0; i < 100; ++i) {
    v.refreshControlSnapshot(); v.service();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  check(v._surfaceImageCacheValid, "first-open terrain cache settles");
  render(v, "settled-default");
#endif
  specifics(p, v, e);
  restore(p, v, e, initial);
#if S3G_MAP_KIND != 3
  const auto band = D::encoderTitleBand(kGuiWidth, kGuiHeight);
  set(p, v, e, kOutputParamId, -13.);
  set(p, v, e, kOrderParamId, 4.);
#if S3G_MAP_KIND == 2
  set(p, v, e, kFieldListenModeParamId, 1.);
#endif
  for (const auto button : {band.randomButton, band.presetMenu}) {
    click(v, center(D::cocoaRect(button))); flush(p, v, e);
    check(near(value(p, kOutputParamId), -13.) && value(p, kOrderParamId) == 4.,
        "RANDOM / INIT preserve OUT and ORDER");
#if S3G_MAP_KIND == 2
    check(value(p, kFieldListenModeParamId) == 1., "RANDOM / INIT preserve LISTEN");
#endif
  }
  restore(p, v, e, initial);
#endif
  for (uint32_t i = 0; i < paramsCount(&p.plugin); ++i) {
    clap_param_info_t info{};
    paramsGetInfo(&p.plugin, i, &info);
    for (double norm : {0., .57, 1.}) {
      set(p, v, e, info.id, info.min_value + norm * (info.max_value - info.min_value));
      const double got = value(p, info.id);
      check(std::isfinite(got) && got >= info.min_value - 1.e-4 &&
          got <= info.max_value + 1.e-4, "legal parameter automation");
    }
  }
  e.balanced();
  restore(p, v, e, initial);
  auto path = F::pathToUtf8(F::pathFromUtf8(std::getenv("S3G_MAP_CAPTURE_DIR")
      ? std::getenv("S3G_MAP_CAPTURE_DIR") : "/private/tmp") /
      (std::string(descriptor.id) + "-écho_波_Δ.s3gpreset"));
  check(v.presetFile(path, true), "Unicode preset save");
  set(p, v, e, kOutputParamId, -17.);
  check(v.presetFile(path, false), "Unicode preset load");
  flush(p, v, e);
  check(near(value(p, kOutputParamId), -17.), "file recall preserves OUT");
  Stream state;
  check(stateSave(&p.plugin, &state.output), "chunked state save");
  const auto bytes = state.bytes;
  state.bytes.resize(bytes.size() - 1);
  check(!stateLoad(&p.plugin, &state.input), "truncated state rejected");
  state.bytes = bytes;
  state.pos = 0;
  check(stateLoad(&p.plugin, &state.input), "chunked state load");
#if S3G_MAP_KIND == 3
  Stream reserialized;
  check(stateSave(&p.plugin, &reserialized.output) && reserialized.bytes == bytes,
      "Cartography state round trip is byte reproducible");
  constexpr auto padding = offsetof(s3g::AmbiCartographyEncoderParams,
      selectedEnabled) + sizeof(bool);
  std::memset(reinterpret_cast<unsigned char*>(&p.params) + padding,
      0xA5, sizeof(p.params) - padding);
  publishMapSnapshot(p);
  Stream paddingPoisoned;
  check(stateSave(&p.plugin, &paddingPoisoned.output) && paddingPoisoned.bytes == bytes,
      "snapshot padding cannot leak into the legacy state format");
#endif
  e.balanced();
  restore(p, v, e, initial);
  // Queue backpressure: a long drag and a rejected host must retain its END.
  down(v, {749, 83});
  for (int i = 0; i < 2000; ++i) move(v, {760. + i % 40, 83});
  e.reject = true; flush(p, v, e); up(v);
  e.reject = false; flush(p, v, e); v.service(); flush(p, v, e);
  e.balanced();
  restore(p, v, e, initial);
  check(activate(&p.plugin, 48000., 1, 64), "activate");
  startProcessing(&p.plugin);
  std::array<std::array<float, 64>, 64> input{}, output{};
  std::array<float*, 64> in{}, out{};
  for (unsigned ch = 0; ch < 64; ++ch) {
    in[ch] = input[ch].data(); out[ch] = output[ch].data();
  }
  clap_audio_buffer_t ib{}, ob{};
  ib.channel_count = 64; ib.data32 = in.data();
  ob.channel_count = 64; ob.data32 = out.data();
  clap_process_t proc{};
  proc.frames_count = 64;
#if S3G_MAP_KIND != 2
  proc.audio_inputs_count = 1; proc.audio_inputs = &ib;
#endif
  proc.audio_outputs_count = 1; proc.audio_outputs = &ob;
  proc.out_events = &e.output;
  initial.pos = 0;
  check(stateLoad(&p.plugin, &initial.input), "queue state during playback");
  for (unsigned block = 0; block < 120; ++block) {
    for (unsigned ch = 0; ch < 64; ++ch)
      for (unsigned i = 0; i < 64; ++i)
        input[ch][i] = .02f * std::sin((block * 64 + i) * (.03 + ch * .001));
    check(process(&p.plugin, &proc) != CLAP_PROCESS_ERROR, "process live audio");
    for (const auto& channel : output)
      for (float sample : channel) check(std::isfinite(sample), "finite audio");
    v.service();
  }
  render(v, "live");
#if S3G_MAP_KIND == 3
  std::atomic<bool> done{false}, audioOK{true};
  std::thread audio([&] {
    for (unsigned block = 0; block < 160; ++block) {
      if (process(&p.plugin, &proc) == CLAP_PROCESS_ERROR) audioOK = false;
      for (const auto& channel : output)
        for (float sample : channel)
          if (!std::isfinite(sample)) audioOK = false;
    }
    done.store(true, std::memory_order_release);
  });
  unsigned reads = 0;
  do {
    v.service();
    Stream saved;
    check(stateSave(&p.plugin, &saved.output), "state save alongside audio");
    if (reads < 4) {
      initial.pos = 0;
      check(stateLoad(&p.plugin, &initial.input), "concurrent queued state load");
    }
    ++reads;
    std::this_thread::yield();
  } while (!done.load(std::memory_order_acquire) || reads < 100);
  audio.join();
  check(audioOK, "audio remains finite during GUI snapshots / state recall");
#endif
  stopProcessing(&p.plugin); deactivate(&p.plugin);
  e.balanced();
}
} // namespace
int main() {
#if defined(__APPLE__)
  [NSApplication sharedApplication];
  NSWindow *window =
      [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1160, 858)
                                  styleMask:NSWindowStyleMaskTitled
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  [window setReleasedWhenClosed:NO];
#elif defined(_WIN32)
  HWND window = CreateWindowExW(0, L"STATIC", L"s3g environmental GUI test",
                                WS_OVERLAPPEDWINDOW, 0, 0, 1160, 858, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
#endif
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Map encoder parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = host.request_restart =
      host.request_callback = [](const clap_host_t *) {};
  auto *plugin = create(&host);
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
    std::cout << "Map encoder canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
