#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_CIRCUIT_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace VSTGUI;
using namespace circuit_encoder_canvas;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::circuit_drawing;
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
  if (const auto *dir = std::getenv("S3G_CIRCUIT_CAPTURE_DIR")) {
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
void page(Editor &v, int index) {
#if S3G_CIRCUIT_KIND == 2
  click(v, center(v.scorePageButtonRect(index)));
  check(v._scorePage == bool(index), "score page hit map");
#else
  click(v, center(v.pageButtonRect(index)));
#if S3G_CIRCUIT_KIND == 1
  check(v._visualPage == index, "Pulsar page hit map");
#else
  check(v._fieldPage == index, "Wrangler page hit map");
#endif
#endif
}
void audio(Plugin &p, Editor &v, Events &e, unsigned blocks = 16) {
  std::array<std::array<float, 128>, 64> output{};
  std::array<float *, 64> ptrs{};
  for (unsigned i = 0; i < 64; ++i)
    ptrs[i] = output[i].data();
  clap_audio_buffer_t buffer{};
  buffer.channel_count = 64;
  buffer.data32 = ptrs.data();
  clap_process_t proc{};
  proc.frames_count = 128;
  proc.audio_outputs_count = 1;
  proc.audio_outputs = &buffer;
  proc.out_events = &e.output;
  for (unsigned b = 0; b < blocks; ++b) {
    check(process(&p.plugin, &proc) != CLAP_PROCESS_ERROR, "audio process");
    for (const auto &ch : output)
      for (float sample : ch)
        check(std::isfinite(sample), "finite audio");
    v.service();
  }
}
void run(Plugin &p, Editor &v, Events &e) {
  check(p.guiDistance[0].load() > 0, "initialized first-open point geometry");
#if S3G_CIRCUIT_KIND == 2
  check(p.guiActivation[0].load() > 0, "first-open nodes visible before audio");
#endif
  render(v, "first-open");
  Stream initial;
  check(stateSave(&p.plugin, &initial.output), "initial state save");
  check(activate(&p.plugin, 48000, 1, 128) && startProcessing(&p.plugin),
        "activate");
  audio(p, v, e);
  set(p, v, e, kOutputParamId, -19);
  const int pageCount = S3G_CIRCUIT_KIND == 2 ? 2 : 4;
  for (int i = 0; i < pageCount; ++i) {
    page(v, i);
    render(v, "page-" + std::to_string(i));
  }
  // Exercise every menu entry, using the original custom menu and row geometry.
  const int menuMax =
      S3G_CIRCUIT_KIND == 1 ? 13 : S3G_CIRCUIT_KIND == 2 ? 6 : 14;
  for (int m = 1; m <= menuMax; ++m) {
    if (!v.menuCount(m))
      continue;
#if S3G_CIRCUIT_KIND == 1
    page(v, m >= 10 ? 3 : 0);
#elif S3G_CIRCUIT_KIND == 3
    page(v, m >= 10 ? 2 : 0);
#else
    page(v, 0);
#endif
#if S3G_CIRCUIT_KIND == 2
    click(v, center(v.menuRect(m)));
#else
    click(v, center(v.menuBoxRect(m)));
#endif
    check(v._openMenu == m, "original menu button hit map");
    const auto count = v._menuItemCount;
    render(v, "menu-" + std::to_string(m));
    for (unsigned n = 0; n < count; ++n) {
      v.openMenu(m);
      click(v, {v._openMenuRect.origin.x + 8,
                v._openMenuRect.origin.y + (n + .5) * 21});
      flush(p, v, e);
      check(v._openMenu == 0, "menu item dismisses");
      if (m == 1) {
        check(value(p, kPresetParamId) == n, "factory preset index");
        check(near(value(p, kOutputParamId), -19), "preset preserves OUT");
      }
    }
  }
  e.balanced();
  initial.pos = 0;
  check(stateLoad(&p.plugin, &initial.input), "restore initial scene");
  audio(p, v, e);
  // Every original slider, including page- and lane-dependent controls.
  for (const auto &s : kGuiSliders) {
#if S3G_CIRCUIT_KIND == 1
    page(v, listeningGuiParam(s.id) ? 3 : 0);
    const int lane = guiLaneForParam(s.id);
    if (lane >= 0)
      click(v, center(v.laneButtonRect(lane)));
    const auto rect = guiSliderHitRect(s);
    CPoint a{D::midX(rect) - 20, s.y + 5}, b{D::midX(rect) + 20, s.y + 5};
#elif S3G_CIRCUIT_KIND == 2
    page(v, (s.id == kScoreVariationParamId || s.id == kScoreRecombineParamId ||
             s.id == kScoreMemoryParamId)
                ? 1
                : 0);
    CPoint a{s.x + 108 + 82 * .2, s.y + 5}, b{s.x + 108 + 82 * .8, s.y + 5};
#else
    page(v, isListenerGuiParam(s.id) ? 2 : 0);
    set(p, v, e, kListenerResponseParamId,
        s.id == kSettleAmountParamId || s.id == kSettleTargetParamId ||
                s.id == kSettleRecoveryParamId
            ? 1
            : 0);
    CPoint a{s.panelX + 108 + 82 * .2, s.y + 5},
        b{s.panelX + 108 + 82 * .8, s.y + 5};
#endif
    const double before = value(p, s.id);
    down(v, a);
    const bool hit = v._dragParam == s.id;
    if (!hit)
      std::cerr << "slider not hit " << s.id << "\n";
    check(hit, "slider original hit map");
    move(v, b);
    up(v);
    flush(p, v, e);
    check(std::isfinite(value(p, s.id)), "finite slider value");
    down(v, a, 2);
    up(v);
    flush(p, v, e);
    clap_param_info_t info{};
    for (unsigned n = 0; n < paramsCount(nullptr); ++n) {
      paramsGetInfo(&p.plugin, n, &info);
      if (info.id == s.id)
        break;
    }
    check(near(value(p, s.id), info.default_value), "double click default");
    set(p, v, e, s.id, before);
  }
  e.balanced();
#if S3G_CIRCUIT_KIND == 1
  page(v, 1);
  for (unsigned lane = 0; lane < 3; ++lane) {
    click(v, center(v.pulsaretLaneRect(lane)));
    check(v._selectedLane == lane, "trace selects lane");
    render(v, "lane-" + std::to_string(lane));
  }
  const auto request = p.guiCaptureRequestSerial.load();
  click(v, center(v.captureRect()));
  check(p.guiCaptureRequestSerial.load() == request + 1,
        "capture action serial");
  audio(p, v, e, 800);
  render(v, "capture");
  check(p.guiCaptureGeneration.load() > 0 &&
            p.guiCaptureCompletedSerial.load() == request + 1,
        "CAPTURE completes and publishes table");
#elif S3G_CIRCUIT_KIND == 2
  page(v, 1);
  for (unsigned planes = 0; planes < 4; ++planes) {
    click(v, center(v.scorePlaneCountButtonRect(planes)));
    flush(p, v, e);
    audio(p, v, e);
    click(v, center(v.scoreGrowRect()));
    audio(p, v, e);
    check(p.guiLatticePlaneCount.load() == (1u << planes),
          "lattice plane count");
    click(v, center(v.scoreStopRect()));
    check(!p.scoreTransportRunning.load(), "STOP transport");
    for (unsigned plane = 0; plane < (1u << planes); ++plane) {
      click(v, center(v.scorePlaneTabRect(plane)));
      for (unsigned cell = 0; cell < 16; ++cell) {
        click(v, center(v.scoreCellRect(cell)));
        check(v._selectedLatticeCell == plane * 16 + cell,
              "lattice cell hit map");
      }
      render(v, "lattice-" + std::to_string(planes) + "-plane-" +
                    std::to_string(plane));
    }
    click(v, center(v.scoreGoRect()));
    check(p.scoreTransportRunning.load(), "GO transport");
  }
#else
  set(p, v, e, kVoicesParamId, 64);
  page(v, 1);
  for (unsigned bank = 0; bank < 2; ++bank) {
    click(v, center(v.curveBankButtonRect(bank)));
    for (unsigned dim = 0; dim < (bank ? 5 : 19); ++dim) {
      const auto before =
          controlStateSnapshot(p).params.voiceBreakpointsEnabled;
      click(v, center(v.curveParameterButtonRect(dim)));
      check(controlStateSnapshot(p).params.voiceBreakpointsEnabled == before,
            "dimension label only arms; no enable/write");
      if (!before)
        click(v, center(v.curvesToggleRect()));
      for (unsigned voices = 0; voices < 4; ++voices) {
        click(v, center(v.curveVoiceBankButtonRect(voices)));
        const auto graph = v.curveGraphRect();
        down(v, {graph.origin.x + graph.size.width * .2, D::midY(graph)});
        const int row = v._dragBreakpointRow, voice = v._dragBreakpointVoice;
        check(row >= 0 && voice >= 0, "curve drag locks target");
        move(v, {D::maxX(graph), graph.origin.y + graph.size.height * .2});
        check(v._dragBreakpointRow == row && v._dragBreakpointVoice == voice,
              "sideways curve drag cannot change target");
        auto edited = controlStateSnapshot(p).params;
        const auto dimension = static_cast<WranglerCurveDimension>(row);
        check(near((*breakpointArray(edited, dimension))[voice],
                   breakpointStorageForEffective(edited, dimension, .8)),
              "curve stores original inverse-mapped value");
        up(v);
        const auto stored = *breakpointArray(edited, dimension);
        click(v, center(v.curvesToggleRect()));
        auto disabled = controlStateSnapshot(p).params;
        check(!disabled.voiceBreakpointsEnabled &&
                  *breakpointArray(disabled, dimension) == stored,
              "CURVES OFF preserves stored values");
        click(v, center(v.curvesToggleRect()));
      }
    }
    render(v, "curve-bank-" + std::to_string(bank));
  }
  page(v, 3);
  for (unsigned n = 0; n < 24; ++n) {
    click(v, center(v.surfaceAddRect()));
    v.service();
    check(v._surfaceSnapshot.cellCount == n + 1, "SURF rapid ADD");
  }
  click(v, center(v.surfaceEnableRect()));
  click(v, center(v.surfaceEditRect()));
  const auto plot = v.surfacePlotRect();
  down(v, center(plot));
  move(v, {plot.origin.x + plot.size.width * .7,
           plot.origin.y + plot.size.height * .3});
  up(v);
  flush(p, v, e);
  check(near(value(p, kSurfaceXParamId), .7), "SURF X automation");
  render(v, "surface");
  v.openSurfacePopup();
  check(v._surfacePanel && v._surfacePanel->visible(), "SURF popout");
  if (v._surfacePopupView) {
    v._surfacePopupView->service();
    render(*v._surfacePopupView, "popout");
    v._surfacePopupView->syncSurfaceEditMode(true);
    check(v._surfaceEdit, "POP edit mode synchronized");
    for (int menu : {15, 16}) {
      auto &pop = *v._surfacePopupView;
      pop._selectedSurfaceCell = 0;
      auto pos = center(pop.menuBoxRect(menu));
      pos.offset(-18, -42);
      click(pop, pos);
      check(pop._openMenu == menu, "POP custom menu hit map");
      render(pop, "pop-menu-" + std::to_string(menu));
      const auto count = pop._menuItemCount;
      click(pop, {pop._openMenuRect.origin.x - 18 + 8,
                  pop._openMenuRect.origin.y - 42 + (count - .5) * 21});
      check(pop._openMenu == 0, "POP menu selection dismisses");
    }
  }
  v.hideSurfacePopup();
#endif
  audio(p, v, e);
  e.balanced();
  // Original preset codec, Unicode path and OUT preservation.
  auto folder = std::filesystem::temp_directory_path() / "s3g-circuit-parity";
  std::filesystem::create_directories(folder);
  const auto path = F::pathToUtf8(
      folder / F::pathFromUtf8((std::string(portablePresetDirectory) +
                                "-é-音." + portablePresetExtension)
                                   .c_str()));
  set(p, v, e, kOutputParamId, -21);
  check(v.savePresetPath(path), "Unicode preset save");
  set(p, v, e, kOutputParamId, -26);
  check(v.loadPresetPath(path), "Unicode preset load");
  check(near(value(p, kOutputParamId), -26), "custom preset preserves OUT");
  audio(p, v, e);
  Stream saved;
  check(stateSave(&p.plugin, &saved.output), "full project state save");
  set(p, v, e, kOutputParamId, -10);
  check(stateLoad(&p.plugin, &saved.input), "full project recall");
  check(near(value(p, kOutputParamId), -26), "project restores OUT");
  audio(p, v, e);
  render(v, "restored-live");
  e.balanced();
  e.reject = true;
  v.pointerEditing = true;
  for (unsigned n = 0; n < 4500; ++n)
    v.editValue(kOutputParamId, -20. - (n % 3));
  v.stopRefresh();
  flush(p, v, e);
  check(!p.guiParamEvents.available() || p.guiParamEvents.available() < 32,
        "backpressure retained");
  e.reject = false;
  flush(p, v, e);
  e.balanced();
  check(p.guiParamEvents.available() == 4095,
        "queue drains with balanced ends");
  stopProcessing(&p.plugin);
  deactivate(&p.plugin);
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
  host.name = "Circuit encoder parity";
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
    std::cout << "Circuit encoder canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
