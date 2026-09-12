#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_ENVIRONMENT_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace VSTGUI;
using namespace environment_encoder_canvas;
namespace F = s3g::portable_gui::foundation;
namespace D = s3g::portable_gui::environment_drawing;
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
  if (const auto *dir = std::getenv("S3G_ENVIRONMENT_CAPTURE_DIR")) {
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
void choosePreset(Plugin &p, Editor &v, Events &e, unsigned preset) {
  click(v, center(v.presetMenuRect()));
  check(v._openMenu == 1, "custom preset menu opens");
  click(v, {v._openMenuRect.origin.x + 10,
            v._openMenuRect.origin.y + (preset + .5) * 21});
  flush(p, v, e);
  check(value(p, kPresetParamId) == preset, "factory preset chosen");
}
void run(Plugin &p, Editor &v, Events &e) {
  check(p.guiVoiceCount.load() == v._paramsSnapshot.voices,
        "complete voice field before activation");
  check(p.guiDistance[0].load() > 0, "initial points have nonzero positions");
  render(v, "first-open");
  v.editValue(kOutputParamId, -19.);
  flush(p, v, e);
  clap_param_info_t preset{};
  paramsGetInfo(&p.plugin, 0, &preset);
  for (unsigned i = 0; i <= static_cast<unsigned>(preset.max_value); ++i) {
    choosePreset(p, v, e, i);
    check(near(value(p, kOutputParamId), -19.), "factory recalls preserve OUT");
    render(v, "preset-" + std::to_string(i));
  }
  choosePreset(p, v, e, 0);
  for (const auto &s : kGuiSliders) {
#if S3G_ENVIRONMENT_KIND == 2
    if (!guiParamVisible(v._paramsSnapshot.regime, s.id))
      continue;
    const double y = guiSliderY(v._paramsSnapshot.regime, s);
#else
    const double y = s.y;
#endif
    const auto before = value(p, s.id);
    down(v, {s.panelX + 108 + 82 * .2, y + 5});
    move(v, {s.panelX + 108 + 82 * .75, y + 5});
    up(v);
    flush(p, v, e);
    check(std::isfinite(value(p, s.id)), "slider produces finite value");
    double expected = sliderValue(s, {s.panelX + 108 + 82 * .75, y + 5});
    for (unsigned i = 0; i < paramsCount(nullptr); ++i) {
      clap_param_info_t info{};
      if (paramsGetInfo(&p.plugin, i, &info) && info.id == s.id &&
          (info.flags & CLAP_PARAM_IS_STEPPED))
        expected = std::round(expected);
    }
    check(near(value(p, s.id), expected),
          "slider moves the correct parameter to the pointer position");
    check(v._openMenu == 0, "slider hit does not open menu");
    v.editValue(s.id, before);
    flush(p, v, e);
  }
  e.balanced();
  for (int menu = 2; menu <= 10; ++menu) {
    const auto count = v.menuCount(menu);
    if (!count)
      continue;
    for (unsigned item = 0; item < count; ++item) {
      click(v, center(v.menuBoxRect(menu)));
      check(v._openMenu == menu, "custom toolbox menu opens");
      render(v, "menu-" + std::to_string(menu));
      click(v, {v._openMenuRect.origin.x + 8,
                v._openMenuRect.origin.y + (item + .5) * 21});
      flush(p, v, e);
      check(v._openMenu == 0, "custom menu selection dismisses");
      clap_id id = CLAP_INVALID_ID;
#if S3G_ENVIRONMENT_KIND == 0 || S3G_ENVIRONMENT_KIND == 4
      switch (menu) {
      case 3:
        id = kRateModeBParamId;
        break;
      case 4:
        id = kPlaceParamId;
        break;
      case 5:
        id = kFieldListenModeParamId;
        break;
      case 6:
        id = kFieldListenResponseParamId;
        break;
      case 9:
        id = kGustEdgeParamId;
        break;
      case 10:
        id = kOrderParamId;
        break;
      }
#elif S3G_ENVIRONMENT_KIND == 1 || S3G_ENVIRONMENT_KIND == 3
      switch (menu) {
      case 2:
        id = kRegimeParamId;
        break;
      case 3:
        id = kEnvironmentParamId;
        break;
      case 4:
        id = kPlaceParamId;
        break;
      case 5:
        id = kFieldListenModeParamId;
        break;
      case 6:
        id = kFieldListenResponseParamId;
        break;
      case 10:
        id = kOrderParamId;
        break;
      }
#else
      switch (menu) {
      case 2:
        id = kOrderParamId;
        break;
      case 3:
        id = kRegimeParamId;
        break;
      case 4:
        id = kCallTypeParamId;
        break;
      case 5:
        id = kPlaceParamId;
        break;
      case 6:
        id = kFieldListenResponseParamId;
        break;
      }
#endif
      check(id != CLAP_INVALID_ID &&
                near(value(p, id), item + (id == kOrderParamId ? 1 : 0)),
            "menu writes the selected parameter value");
    }
  }
  choosePreset(p, v, e, 0);
  for (int camera = 0; camera < 3; ++camera) {
    v.setViewPreset(camera);
    render(v, "camera-" + std::to_string(camera));
  }
  click(v, center(v.pageButtonRect(1)));
  click(v, center(v.surfaceButtonRect(2)));
  flush(p, v, e);
  choosePreset(p, v, e, 1);
  click(v, center(v.surfaceButtonRect(2)));
  flush(p, v, e);
  check(v._surfaceSnapshot.cellCount == 2,
        "SURF ADD preserves two distinct captures");
  // No process/flush between these edits: a stale GUI snapshot must not drop
  // the first capture while the audio thread is still handling it.
  click(v, center(v.surfaceButtonRect(2)));
  v.service();
  click(v, center(v.surfaceButtonRect(2)));
  flush(p, v, e);
  check(v._surfaceSnapshot.cellCount == 4,
        "rapid ADD survives deferred snapshot publication");
  click(v, center(v.surfaceButtonRect(3)));
  flush(p, v, e);
  click(v, center(v.surfaceButtonRect(3)));
  flush(p, v, e);
  check(v._surfaceSnapshot.cellCount == 2,
        "DEL removes only the selected cells");
  click(v, center(v.surfaceButtonRect(1)));
  flush(p, v, e);
  check(v._surfaceSnapshot.enabled, "SURF ON with two cells");
  const auto plot = v.surfacePlotRect();
  const CPoint a{plot.origin.x + plot.size.width * .2,
                 plot.origin.y + plot.size.height * .3};
  const CPoint b{plot.origin.x + plot.size.width * .8,
                 plot.origin.y + plot.size.height * .7};
  e.events.clear();
  down(v, a);
  move(v, b);
  up(v);
  flush(p, v, e);
  check(near(value(p, kSurfaceXParamId), .8) &&
            near(value(p, kSurfaceYParamId), .3),
        "SURF X/Y pointer mapping");
  for (auto id : {kSurfaceXParamId, kSurfaceYParamId}) {
    unsigned begins = 0, ends = 0;
    for (auto ev : e.events)
      if (ev.paramId == id) {
        begins += ev.kind == Kind::GestureBegin;
        ends += ev.kind == Kind::GestureEnd;
      }
    check(begins == 1 && ends == 1, "one X/Y edit gesture per complete drag");
  }
  e.balanced();
  click(v, center(v.surfaceButtonRect(0)));
  flush(p, v, e);
  check(v._surfaceEdit, "SURF EDIT");
  auto cell = v._surfaceSnapshot.cells[0];
  down(v, {plot.origin.x + cell.x * plot.size.width,
           D::maxY(plot) - cell.y * plot.size.height});
  move(v, a);
  up(v);
  flush(p, v, e);
  check(near(v._surfaceSnapshot.cells[0].x, .2) &&
            near(v._surfaceSnapshot.cells[0].y, .7),
        "EDIT moves selected cell");
  v.editValue(kVoicesParamId, 9);
  flush(p, v, e);
  click(v, center(v.surfaceButtonRect(4)));
  flush(p, v, e);
  check(v._surfaceSnapshot.cells[0].params.voices == 9,
        "CAP stores base rather than interpolated state");
  const auto curve = v._surfaceSnapshot.curve;
  click(v, center(v.surfaceCurveRect()));
  flush(p, v, e);
  check(curve != v._surfaceSnapshot.curve, "CURVE cycling");
  click(v, center(v.surfaceFocusRect(1)));
  flush(p, v, e);
  click(v, center(v.surfaceGlideRect(1)));
  flush(p, v, e);
  check(v._surfaceSnapshot.glideMs > 0, "GLIDE control");
  render(v, "surface-edit");
  click(v, center(v.surfaceButtonRect(0)));
  flush(p, v, e);
  render(v, "surface-play");
  const auto cells = v._surfaceSnapshot.cellCount;
  const auto out = value(p, kOutputParamId), order = value(p, kOrderParamId);
  click(v, center(v.randomizeButtonRect()));
  flush(p, v, e);
  check(!v._surfaceSnapshot.enabled && v._surfaceSnapshot.cellCount == cells,
        "RANDOM bypasses SURF without deleting cells");
  check(near(out, value(p, kOutputParamId)) &&
            near(order, value(p, kOrderParamId)),
        "RANDOM preserves OUT/ORDER");
  click(v, center(v.surfaceButtonRect(1)));
  flush(p, v, e);
  Stream saved;
  check(stateSave(&p.plugin, &saved.output), "chunked project save");
  for (auto len :
       {size_t(0), size_t(3), saved.bytes.size() / 2, saved.bytes.size() - 1}) {
    Stream bad;
    bad.bytes.assign(saved.bytes.begin(), saved.bytes.begin() + len);
    check(!stateLoad(&p.plugin, &bad.input), "truncated state rejected");
  }
  check(stateLoad(&p.plugin, &saved.input), "chunked project recall");
  flush(p, v, e);
  check(v._surfaceSnapshot.enabled && v._surfaceSnapshot.cellCount == cells,
        "SURF survives state recall");
  const auto folder =
      std::getenv("S3G_ENVIRONMENT_CAPTURE_DIR")
          ? F::pathFromUtf8(std::getenv("S3G_ENVIRONMENT_CAPTURE_DIR"))
          : std::filesystem::temp_directory_path() / "s3g-environment-test";
  std::filesystem::create_directories(folder);
  const auto file =
      folder / (std::string("étang-氷-") + portablePresetExtension + "." +
                portablePresetExtension);
  check(v.savePresetPath(F::pathToUtf8(file)), "UTF-8 custom preset save");
  flush(p, v, e);
  v.editValue(kOutputParamId, -23.);
  flush(p, v, e);
  check(v.loadPresetPath(F::pathToUtf8(file)), "UTF-8 custom preset recall");
  flush(p, v, e);
  check(near(value(p, kOutputParamId), -23.), "custom recall preserves OUT");
  saved.pos = 0;
  check(stateLoad(&p.plugin, &saved.input), "restore populated surface");
  flush(p, v, e);
  {
    std::ofstream state(folder / (std::string(descriptor.id) + ".state"),
                        std::ios::binary);
    state.write(reinterpret_cast<const char *>(saved.bytes.data()),
                saved.bytes.size());
  }
  click(v, center(v.surfaceButtonRect(5)));
  check(v._surfacePanel && v._surfacePanel->visible() && v._fieldPage == 0,
        "SURF pop-out opens and main returns to FIELD");
  if (v._surfacePopupView) {
    auto &child = *v._surfacePopupView;
    // Add through both views without an intervening host flush.
    auto add = center(child.surfaceButtonRect(2));
    add.offset(-18, -42);
    click(child, add);
    v._fieldPage = 1;
    click(v, center(v.surfaceButtonRect(2)));
    flush(p, v, e);
    child.service();
    check(v._surfaceSnapshot.cellCount == cells + 2 &&
              child._surfaceSnapshot.cellCount == cells + 2,
          "both SURF views share pending edits");
    v._fieldPage = 0;
    auto button = center(child.surfaceButtonRect(0));
    button.offset(-18, -42);
    click(child, button);
    flush(p, child, e);
    check(child._surfaceEdit == v._surfaceEdit,
          "PLAY/EDIT synchronized with pop-out");
    render(child, "surface-popout");
    v.hideSurfacePopup();
    check(!v._surfacePanel->visible(), "SURF pop-out hides");
  }
  e.reject = true;
  const auto before = value(p, kOutputParamId);
  v.editValue(kOutputParamId, -31.);
  flush(p, v, e);
  check(value(p, kOutputParamId) == before,
        "host backpressure defers parameter application");
  e.reject = false;
  flush(p, v, e);
  check(near(value(p, kOutputParamId), -31.), "host backpressure recovery");
  e.balanced();
  check(activate(&p.plugin, 48000, 1, 64), "activate");
  startProcessing(&p.plugin);
  std::array<std::array<float, 64>, 64> audio{};
  std::array<float *, 64> ptr{};
  for (unsigned ch = 0; ch < 64; ++ch)
    ptr[ch] = audio[ch].data();
  clap_audio_buffer_t bus{};
  bus.channel_count = 64;
  bus.data32 = ptr.data();
  clap_process_t proc{};
  proc.frames_count = 64;
  proc.audio_outputs_count = 1;
  proc.audio_outputs = &bus;
  proc.out_events = &e.output;
  for (unsigned i = 0; i < 400; ++i)
    check(process(&p.plugin, &proc) != CLAP_PROCESS_ERROR,
          "live audio processing");
  v.service();
  render(v, "live-field");
  check(std::isfinite(p.guiAzimuth[0].load()) && p.guiVoiceCount.load() > 0,
        "live telemetry");
  stopProcessing(&p.plugin);
  deactivate(&p.plugin);
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
  host.name = "Environmental parity";
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
    check(!editor._surfacePanel || !editor._surfacePanel->visible(),
          "hide closes auxiliary panel");
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
    std::cout << "Environmental canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
