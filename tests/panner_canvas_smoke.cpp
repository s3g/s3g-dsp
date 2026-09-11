#include S3G_TEST_PANNER_SOURCE
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include <chrono>
#include <iostream>
#include <map>

namespace test {
using namespace VSTGUI;
using panner_canvas::Editor;
namespace R = s3g::portable_gui::routing;
namespace F = s3g::portable_gui::foundation;
bool ok = true;
void expect(bool v, const char *m) {
  if (!v) {
    ok = false;
    std::cerr << m << '\n';
  }
}
bool near(double a, double b) { return std::abs(a - b) < 1e-4; }
struct Events {
  std::vector<s3g::clap_gui::ParamEvent> values;
  clap_output_events_t out{
      this, [](const clap_output_events_t *o, const clap_event_header_t *h) {
        auto &v = static_cast<Events *>(o->ctx)->values;
        using K = s3g::clap_gui::ParamEventKind;
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
          auto *e = reinterpret_cast<const clap_event_param_value_t *>(h);
          v.push_back({K::Value, e->param_id, e->value});
        } else {
          auto *e = reinterpret_cast<const clap_event_param_gesture_t *>(h);
          v.push_back({h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                           ? K::GestureBegin
                           : K::GestureEnd,
                       e->param_id, 0.});
        }
        return true;
      }};
  void balanced() {
    using K = s3g::clap_gui::ParamEventKind;
    std::map<clap_id, int> depth;
    for (auto e : values) {
      if (e.kind == K::GestureBegin)
        expect(++depth[e.paramId] == 1, "nested gesture");
      else if (e.kind == K::GestureEnd)
        expect(--depth[e.paramId] == 0, "unbalanced END");
      else
        expect(depth[e.paramId] == 1, "value outside gesture");
    }
    for (auto e : depth)
      expect(e.second == 0, "unterminated gesture");
    values.clear();
  }
};
void flush(Plugin &p, Events &e) { paramsFlush(&p.plugin, nullptr, &e.out); }
void down(Editor &v, CPoint p, int count = 1) {
  MouseDownEvent e;
  e.mousePosition = p;
  e.clickCount = count;
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
void click(Editor &v, CPoint p, int count = 1) {
  down(v, p, count);
  up(v);
}
CPoint center(R::Box b) {
  return {b.origin.x + b.size.width * .5, b.origin.y + b.size.height * .5};
}
void render(Editor &v, const std::string &suffix = "") {
  auto c = COffscreenContext::create({900., 720.});
  expect(bool(c), "offscreen context");
  if (!c)
    return;
  c->beginDraw();
  v.draw(c);
  c->endDraw();
  if (auto *root = std::getenv("S3G_ROUTING_CAPTURE_DIR");
      root && !suffix.empty()) {
    std::filesystem::create_directories(F::pathFromUtf8(root));
    const auto data = getPlatformFactory().createBitmapMemoryPNGRepresentation(
        c->getBitmap()->getPlatformBitmap());
    std::ofstream file(F::pathFromUtf8(root) /
                           (std::string(descriptor.id) + "-" + suffix + ".png"),
                       std::ios::binary);
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
    expect(bool(file), "write reference");
  }
}
void geometry(Editor &v, Plugin &p, Events &events) {
#if defined(__APPLE__)
  auto *cocoa = [[S3G_TEST_PANNER_COCOA_VIEW alloc] initWithPlugin:&p];
#endif
  for (unsigned preset = 0; preset < kLayoutCount; ++preset) {
    v.set(kLayoutParamId, layoutPresetForMenuIndex(preset));
    flush(p, events);
    v.refreshState();
    for (int mode = 0; mode < 3; ++mode) {
      v.setViewPreset(mode);
#if defined(__APPLE__)
      [cocoa setViewPreset:mode];
#endif
      const auto field = R::box(panner_canvas::family.field);
      for (unsigned sp = 0; sp < v.state.params.activeSpeakers; ++sp) {
        const auto &s = v.state.speakers[sp];
        auto d = s3g::directionFromAed(s.azimuthDeg, s.elevationDeg);
        s3g::Vec3 world{d.x * s.distance, d.y * s.distance, d.z * s.distance};
        auto point = v.projectWorldPoint(world, field);
        auto roundtrip = v.worldFromPoint(point, field, world);
        expect(near(world.x, roundtrip.x) && near(world.y, roundtrip.y) &&
                   near(world.z, roundtrip.z),
               "camera projection inverse");
#if defined(__APPLE__)
        auto original = [cocoa
            projectWorldPoint:world
                         rect:NSMakeRect(field.origin.x, field.origin.y,
                                         field.size.width, field.size.height)
                        depth:nil];
        expect(near(point.x, original.x) && near(point.y, original.y),
               "all preset coordinates match Cocoa camera");
#endif
      }
      render(v, preset == 0 && mode == 2 ? "field" : "");
    }
  }
#if defined(__APPLE__)
  [cocoa release];
#endif
  events.balanced();
}
void controls(Editor &v, Plugin &p, Events &events) {
  v._page = 0;
  v.set(kActiveSourcesParamId, 64);
  flush(p, events);
  const auto main = R::box(panner_canvas::family.mainPanel);
  for (int page = 0; page < 3; ++page) {
    render(v);
    click(v, center(Editor::pageButtonRect(page, main)));
    expect(v._page == page, "page button");
    render(v, page == 1 ? "mixer" : page == 2 ? "design" : "");
  }
  // Every paged source fader and its mute/solo, including source 64.
  v._page = 1;
  for (unsigned page = 0; page < 4; ++page) {
    v._mixerPage = page;
    for (unsigned lane = 0; lane < 16; ++lane) {
      render(v);
      const auto slot =
          Editor::mixerSourceRect(lane, R::box(panner_canvas::family.field));
      down(v, center(slot));
      move(v, {slot.origin.x + 6., slot.origin.y + slot.size.height * .25});
      up(v);
      flush(p, events);
      expect(
          near(v.value(sourceParamId(page * 16 + lane, kSourceGainOffset)), 3.),
          "all source gain faders");
      for (bool solo : {false, true}) {
        render(v);
        auto b = solo ? Editor::mixerSoloRect(
                            lane, R::box(panner_canvas::family.field))
                      : Editor::mixerMuteRect(
                            lane, R::box(panner_canvas::family.field));
        click(v, center(b));
        flush(p, events);
        expect(v.value(sourceParamId(page * 16 + lane,
                                     solo ? kSourceSoloOffset
                                          : kSourceMuteOffset)) == 1.,
               "all source mute/solo");
      }
    }
  }
  events.balanced();
  // Actual custom menu, not a platform popup.
  v._page = 0;
  render(v);
  click(v, {650., 196.});
  flush(p, events);
  expect(near(v.value(kFocusParamId), v.info(kFocusParamId).min_value),
         "slider labels retain full-row Cocoa hit regions");
  render(v);
  click(v, {850., 196.});
  flush(p, events);
  expect(near(v.value(kFocusParamId), v.info(kFocusParamId).max_value),
         "slider readouts retain full-row Cocoa hit regions");
  v.set(kFocusParamId, v.info(kFocusParamId).default_value);
  flush(p, events);
  events.balanced();
  render(v);
  click(v, {750., 151.});
  render(v);
  click(v, {745., 154.});
  flush(p, events);
  events.balanced();
  // Source and speaker dragging retain the original camera-plane equations.
  v.set(kLayoutParamId, 0);
  v.set(kActiveSourcesParamId, 4);
  v.set(sourceParamId(0, kSourceMuteOffset), 0);
  flush(p, events);
  v.refreshState();
  const auto field = R::box(panner_canvas::family.field);
  auto position = s3g::layoutPannerEffectiveSourcePosition(v.state.sources[0],
                                                           v.state.params);
  auto pt = v.projectWorldPoint(position, field);
  render(v);
  down(v, pt);
  expect(v.spatialDrag == 1, "source hit");
  move(v, {pt.x + 15., pt.y + 10.});
  up(v);
  flush(p, events);
  events.balanced();
  v._page = 2;
  v.refreshState();
  auto sp = v.state.speakers[0];
  auto dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
  pt = v.projectWorldPoint(
      {dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance}, field);
  render(v);
  down(v, pt);
  expect(v.spatialDrag == 2, "speaker hit");
  move(v, {pt.x + 9., pt.y + 8.});
  up(v);
  flush(p, events);
  expect(v.state.params.layout == s3g::LayoutPannerPreset::Custom,
         "speaker drag makes custom layout");
}
void files(Editor &v, Plugin &p, Events &events) {
  auto root =
      std::filesystem::temp_directory_path() /
      ("s3g-panner-parity-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  auto json = F::pathToUtf8(root / "écho_波_Δ.json"),
       preset = F::pathToUtf8(root / "écho_波_Δ.s3gpreset");
  v.refreshState();
  auto before = v.state;
  expect(v.layoutFile(json, true), "Unicode layout export");
  v.set(kLayoutParamId, 1);
  flush(p, events);
  expect(v.layoutFile(json, false), "Unicode layout import");
  flush(p, events);
  expect(v.state.params.activeSpeakers == before.params.activeSpeakers,
         "JSON count");
  for (unsigned i = 0; i < before.params.activeSpeakers; ++i)
    expect(
        near(v.state.speakers[i].azimuthDeg, before.speakers[i].azimuthDeg) &&
            near(v.state.speakers[i].elevationDeg,
                 before.speakers[i].elevationDeg) &&
            near(v.state.speakers[i].distance, before.speakers[i].distance),
        "JSON coordinates not regenerated");
  v.setViewPreset(1);
  expect(v.presetFile(preset, true), "preset save");
  v.set(kOutputParamId, -17.);
  v.set(kFocusParamId, 3.);
  flush(p, events);
  expect(v.presetFile(preset, false), "preset load");
  flush(p, events);
  expect(v.value(kOutputParamId) == -17. && v._viewMode == 1,
         "preset keeps global OUT and recalls camera");
  events.balanced();
  std::ofstream invalid(F::pathFromUtf8(json.c_str()));
  invalid << "{\"speakers\":[{},{}],\"speaker_count\":\"bad\"}";
  invalid.close();
  before = v.state;
  expect(!v.layoutFile(json, false) &&
             v.state.params.activeSpeakers == before.params.activeSpeakers,
         "malformed JSON load is transactional");
  std::filesystem::remove_all(
      root); // Only this test's unique fixture directory.
}
} // namespace test
int main() {
  using namespace test;
#if defined(__APPLE__)
  [NSApplication sharedApplication];
#endif
  if (!F::acquireRuntime())
    return 2;
  clap_host_t host{};
  host.clap_version = CLAP_VERSION_INIT;
  host.name = "Routing parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *) {};
  const auto *plugin = createPlugin(nullptr, &host, descriptor.id);
  expect(plugin && plugin->init(plugin) &&
             plugin->activate(plugin, 48000., 1, 128),
         "plugin lifecycle");
  if (plugin) {
    {
      auto &p = *self(plugin);
      Editor v(p);
      Events e;
      geometry(v, p, e);
      controls(v, p, e);
      files(v, p, e);
    }
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
  }
  F::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " canvas parity passed\n";
  return ok ? 0 : 1;
}
