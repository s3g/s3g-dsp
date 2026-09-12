#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include S3G_RESONATOR_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace VSTGUI;
using namespace resonator_encoder_canvas;
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
  if (const auto *dir = std::getenv("S3G_RESONATOR_CAPTURE_DIR")) {
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
#if S3G_RESONATOR_KIND == 1
constexpr auto outputId = kParamOutputGain;
constexpr auto toneId = kParamSize;
#elif S3G_RESONATOR_KIND == 2
constexpr auto outputId = kOutputGainParamId;
constexpr auto toneId = kSpeedParamId;
#else
constexpr auto outputId = kOutputParamId;
constexpr auto toneId = kTuneParamId;
#endif
void audio(Plugin &p, Editor &v, Events &e, int key = -1,
           unsigned blocks = 32) {
  std::array<std::array<float, 128>, 16> output{};
  std::array<float *, 16> ptrs{};
  for (unsigned i = 0; i < 16; ++i)
    ptrs[i] = output[i].data();
  clap_audio_buffer_t buffer{};
  buffer.channel_count = 16;
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
void menu(Editor &v, Plugin &p, Events &e, CPoint anchor, clap_id id, int index,
          double expected) {
  click(v, anchor);
  check(v._openMenu == id, "original menu hit map");
  render(v, "menu-" + std::to_string(id));
#if S3G_RESONATOR_KIND == 1
  auto box = id == kParamPreset ? D::cocoaRect(kTitleBand.presetMenu)
                                : menuAnchorRect(v._openMenuLocation);
  const unsigned rows =
      D::multiColumnMenuRows(menuCount(id), menuColumnCount(menuCount(id)));
  click(v, {box.origin.x + (index / rows + .5) * box.size.width,
            D::maxY(box) + 2. + (index % rows + .5) * 19.});
#else
  const auto box = v.openMenuRect();
  click(v, {box.origin.x + box.size.width * .5,
            box.origin.y + (index + .5) * 18.});
#endif
  check(v._openMenu == CLAP_INVALID_ID, "menu dismisses on selection");
  flush(p, v, e);
  check(near(value(p, id), expected), "menu chosen value");
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
#if S3G_RESONATOR_KIND == 1
  float distanceSum = 0;
  for (unsigned n = 0; n < 8; ++n)
    distanceSum += p.bodyDistance[n].load();
  check(distanceSum > 4., "Modal initialized body geometry before audio");
  auto skin = skinPadRect();
  down(v, {skin.origin.x + 10, skin.origin.y + 20});
  move(v, {skin.origin.x + skin.size.width * .7,
           skin.origin.y + skin.size.height * .2});
  up(v);
  flush(p, v, e);
  check(near(value(p, bodySkinParamId(0, true)), .7) &&
            near(value(p, bodySkinParamId(0, false)), .8),
        "per-body skin XY and inverted vertical map");
  down(v, center(skin), 2);
  up(v);
  flush(p, v, e);
  check(near(value(p, bodySkinParamId(0, true)), .43), "skin reset");
  for (int camera = 0; camera < 3; ++camera) {
    click(v, center(D::topologyProcessorCameraButtonRect(fieldPanelRect(),
                                                         camera)));
    check(v._viewMode == camera, "Modal camera button");
    render(v, "camera-" + std::to_string(camera));
  }
  v.setViewPreset(0);
  const auto dir = guiBodyDirection(p, 0);
  const float distance = p.bodyDistance[0].load();
  auto pt = v.projectWorldPoint_rect_depth(
      {dir.x * distance, dir.y * distance, dir.z * distance}, fieldPlotRect(),
      nullptr);
  down(v, {pt.x, pt.y});
  move(v, {pt.x + 20, pt.y + 15});
  up(v);
  flush(p, v, e);
  check(v._selectedBody == 0 &&
            std::abs(value(
                p, bodyAedParamId(0, BodyAedParamKind::AzimuthOffset))) > 1.,
        "TOP body drag writes selected AED");
  click(v, center(modalResetLayoutButtonRect()));
  flush(p, v, e);
  for (unsigned n = 0; n < 8; ++n) {
    check(near(value(p, bodyAedParamId(n, BodyAedParamKind::Distance)), 1.),
          "reset all body distances");
    check(
        near(value(p, bodyAedParamId(n, BodyAedParamKind::AzimuthOffset)), 0.),
        "reset all body offsets");
  }
  slider(v, p, e, kParamSize, {750, controlRowY(structurePanelRect(), 2)},
         {795, controlRowY(structurePanelRect(), 2)});
  menu(v, p, e, center(D::cocoaRect(kTitleBand.presetMenu)), kParamPreset, 24,
       24);
  for (unsigned index = 0; index < kFactoryPresetCount; ++index) {
    set(p, v, e, kParamPreset, index);
    render(v, "factory-" + std::to_string(index));
  }
  menu(v, p, e, {770, controlRowY(structurePanelRect(), 0)}, kParamBodyCount, 4,
       8);
  menu(v, p, e, {770, controlRowY(outputPanelRect(), 3)}, kParamOutputMode, 1,
       menuValueForIndex(kParamOutputMode, 1));
  set(p, v, e, kParamPreset, 0);
#elif S3G_RESONATOR_KIND == 2
  const auto title = D::encoderTitleBand(kGuiWidth, kGuiHeight);
  click(v, center(D::cocoaRect(title.presetMenu)));
  check(v._openMenu == kFactoryPresetMenuId && v._menuItemCount == 7,
        "Medium factory menu opens");
  auto factoryBox = v.openMenuRect();
  click(v, {D::midX(factoryBox), factoryBox.origin.y + 27.});
  flush(p, v, e);
  check(near(value(p, kSpeedParamId), 210.),
        "Medium factory menu applies original voice");
  for (int camera = 0; camera < 3; ++camera) {
    click(v, center(D::topologyProcessorCameraButtonRect(fieldPanelRect(),
                                                         camera)));
    check(v._viewMode == camera, "Medium camera button");
    render(v, "camera-" + std::to_string(camera));
  }
  v.setViewPreset(2);
  auto node = projectedNodePoint(7, v._viewAzDeg, v._viewElDeg, v._viewZoom);
  click(v, {node.x, node.y});
  flush(p, v, e);
  check(near(value(p, kActuatorNodeParamId), 8) &&
            p.previewStrikeNode.load() == 7,
        "node select plus queued strike");
  slider(v, p, e, kSpeedParamId, {710, rowY(mediumPanelRect(), 0)},
         {810, rowY(mediumPanelRect(), 0)});
  for (unsigned index = 0; index < 7; ++index) {
    v.applyFactoryPreset(index);
    flush(p, v, e);
    render(v, "factory-" + std::to_string(index));
  }
  for (int page = 0; page < 3; ++page) {
    click(v, center(excitationPageButtonRect(page)));
    check(v._excitationPage == page, "all three excitation pages");
    render(v, "page-" + std::to_string(page));
  }
  menu(v, p, e, center(menuBoxRect(excitationPanelRect(), 0)), kMidiModeParamId,
       3, 3);
  click(v, center(excitationPageButtonRect(1)));
  set(p, v, e, kActuatorNodeParamId, 8);
  slider(v, p, e, euclideanPulsesParamId(7),
         {710, rowY(excitationPanelRect(), 4)},
         {810, rowY(excitationPanelRect(), 4)});
  menu(v, p, e, center(menuBoxRect(excitationPanelRect(), 6)),
       kSequencerScaleParamId, 7, 7);
  check(v.openMenuRect().size.height == 0, "menu reset");
  set(p, v, e, kMidiModeParamId, 3);
#else
  const auto title = membraneTitleBand();
  click(v, center(D::cocoaRect(title.presetMenu)));
  check(v._openMenu == kFactoryPresetMenuId && v._menuItemCount == 14,
        "Membrane shifted factory menu opens");
  auto factoryBox = v.openMenuRect();
  click(v, {D::midX(factoryBox), factoryBox.origin.y + 27.});
  flush(p, v, e);
  check(v._factoryPresetIndex == 1, "Membrane factory menu applies voice");
  for (unsigned index = 0; index < s3g::kAmbiMembraneKickFactoryPresetCount;
       ++index) {
    v.applyFactoryPreset(index);
    flush(p, v, e);
    render(v, "factory-" + std::to_string(index));
  }
  set(p, v, e, kOutputParamId, -25);
  set(p, v, e, kOrderParamId, 4);
  set(p, v, e, kMidiReceiveParamId, 3);
  v.applyFactoryPreset(2);
  flush(p, v, e);
  check(near(value(p, kOutputParamId), -25) &&
            near(value(p, kOrderParamId), 4) &&
            near(value(p, kMidiReceiveParamId), 3),
        "factory preserves OUT FORMAT MIDI");
  for (unsigned shape = 0; shape < 5; ++shape) {
    menu(v, p, e, {740, 184}, kShapeParamId, shape, shape);
    set(p, v, e, kShapeAmountParamId, 1.);
    render(v, "shape-" + std::to_string(shape));
  }
  click(v, center(membranePageButtonRect(1)));
  check(v._membranePage == 1, "strike page");
  menu(v, p, e, {740, 184}, kStrikeModeParamId, 2, 2);
  click(v, {291 - 202 * .25, 338 + 202 * .3});
  flush(p, v, e);
  check(near(value(p, kStrikeModeParamId), 0) &&
            near(value(p, kStrikeXParamId), .25) &&
            near(value(p, kStrikeYParamId), -.3),
        "membrane click places fixed XY and triggers");
  check(p.active && p.triggerGate,
        "manual strike consumed through audio-service queue");
  slider(v, p, e, kTuneParamId, {710, 314}, {810, 314});
  menu(v, p, e, {740, 80}, kOrderParamId, 4, 5);
  render(v, "stereo");
  set(p, v, e, kMidiReceiveParamId, 0);
#endif
  check(activate(&p.plugin, 48000, 1, 128) && startProcessing(&p.plugin),
        "activate");
  audio(p, v, e, 60);
#if S3G_RESONATOR_KIND == 1
  check(p.actuatorActivity.load() > 0., "Modal live actuator meter");
  v.lastAnimationTime = drawing::uptime() - .04;
  const auto phase = v._actuatorFlowPhase;
  v.service();
  check(v._actuatorFlowPhase != phase, "Modal moving actuator markers");
#elif S3G_RESONATOR_KIND == 2
  bool held = false;
  for (auto &key : p.midiNodeKey)
    held = held || key.load() == 60;
  check(held, "Medium live MIDI note labels");
#else
  check(p.visualActivity.load() > 0., "Membrane live strike activity");
#endif
  render(v, "live");
  e.balanced();
  auto folder = std::filesystem::temp_directory_path() / "s3g-resonator-parity";
  std::filesystem::create_directories(folder);
  const auto path = F::pathToUtf8(
      folder /
      F::pathFromUtf8(
          (std::string(portablePresetDirectory) + "-é-音.s3gpreset").c_str()));
  set(p, v, e, outputId, -21);
  const auto tone = value(p, toneId);
#if S3G_RESONATOR_KIND != 1
  const auto selectedFactory = v._factoryPresetIndex;
#endif
  check(v.presetFile(path, true), "Unicode original-format preset save");
#if S3G_RESONATOR_KIND != 1
  check(v._factoryPresetIndex == selectedFactory,
        "saving preserves factory-menu selection");
#endif
  set(p, v, e, outputId, -26);
  set(p, v, e, toneId, tone * .83);
  check(v.presetFile(path, false),
        "Unicode original-format preset load while active");
  flush(p, v, e);
  check(near(value(p, outputId), -26) && near(value(p, toneId), tone),
        "preset restores voice and preserves OUT");
#if S3G_RESONATOR_KIND == 1
  check(near(value(p, kParamPreset), kCustomPresetIndex),
        "Modal OUT-preserving user load marks voice Custom");
#elif S3G_RESONATOR_KIND == 2
  check(p.activeMidiNote == -1 && p.selfExcitationStep == 0,
        "Medium user load retains original MIDI/sequencer reset");
#else
  check(!p.triggerGate && near(value(p, kTriggerParamId), 0.),
        "Membrane user load retains original trigger release");
#endif
  // Rejected files leave all current controls intact.
  std::ofstream bad(folder / "truncated.s3gpreset", std::ios::binary);
  bad.put(char(kStateVersion));
  bad.close();
  check(!v.presetFile(F::pathToUtf8(folder / "truncated.s3gpreset"), false),
        "reject truncated preset");
  check(near(value(p, toneId), tone), "failed load leaves voice intact");
  Stream saved;
  check(stateSave(&p.plugin, &saved.output), "project save with short writes");
  set(p, v, e, outputId, -10);
  stopProcessing(&p.plugin);
  deactivate(&p.plugin);
  check(stateLoad(&p.plugin, &saved.input), "project restore with short reads");
  check(near(value(p, outputId), -26), "project restores OUT");
  render(v, "restored");
  e.balanced();
  const auto capacity = p.guiParamEvents.available();
  e.reject = true;
  v.pointerEditing = true;
  for (unsigned n = 0; n < 4500; ++n)
    v.editValue(outputId, -20. - (n % 3));
  v.stopRefresh();
  flush(p, v, e);
  check(p.guiParamEvents.available() < 32, "host backpressure retained");
  e.reject = false;
  flush(p, v, e);
  e.balanced();
  check(p.guiParamEvents.available() == capacity,
        "queue drains with balanced gesture ends");
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
  host.name = "Resonator encoder parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = host.request_restart =
      host.request_callback = [](const clap_host_t *) {};
#if S3G_RESONATOR_KIND == 3
  auto *plugin = create(&host);
#else
  auto *plugin = createPlugin(nullptr, &host, descriptor.id);
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
    std::cout << "Resonator encoder canvas parity passed: " << descriptor.id
              << '\n';
  return ok ? 0 : 1;
}
