#include "routing_canvas_test_support.inc"

void menuChoice(Editor &v, Plugin &p, Events &e,
                const s3g::gui_layout::Panel &panel, unsigned row,
                unsigned choice, clap_id id, unsigned first = 0) {
  render(v);
  click(v, {s3g::gui_layout::processorControlX(panel.frame.x) + 8.,
            s3g::gui_layout::rowY(panel, row) + 4.});
  const double rowY = s3g::gui_layout::rowY(panel, row),
               height =
                   (v.info(id).max_value - v.info(id).min_value + 1.) * 20.,
               top = rowY + 16. + height <= kGuiHeight - 6.
                         ? rowY + 16.
                         : rowY - 1. - height;
  click(v,
        {s3g::gui_layout::processorControlX(panel.frame.x) + 8.,
         std::clamp(top, 6., kGuiHeight - height - 6.) + (choice + .5) * 20.});
  flush(p, e);
  expect(near(v.value(id), choice + first), "in-canvas menu parameter mapping");
}
void sliderChoice(Editor &v, Plugin &p, Events &e,
                  const s3g::gui_layout::Panel &panel, unsigned row,
                  clap_id id) {
  render(v);
  const double x = s3g::gui_layout::processorControlX(panel.frame.x),
               w = s3g::gui_layout::processorTrackWidth(panel.frame.width),
               y = s3g::gui_layout::rowY(panel, row);
  const auto i = v.info(id);
  down(v, {x + w * .25, y + 5.});
  move(v, {x + w * .75, y + 5.});
  up(v);
  flush(p, e);
  expect(near(v.value(id),
              v.canonical(id, i.min_value + .75 * (i.max_value - i.min_value))),
         "slider hit and drag");
  click(v, {x + w * .5, y + 5.}, 2);
  flush(p, e);
  expect(near(v.value(id), i.default_value), "slider double-click default");
}
void family(Editor &v, Plugin &p, Events &e) {
#if S3G_ROUTING_FAMILY == 1
  render(v);
  const unsigned groups = S3G_AMBI_GROUP_MATRIX_CHANNELS / 16;
  expect(v.groups() == groups, "fixed 16-channel ambisonic groups");
  for (unsigned src = 0; src < groups; ++src)
    for (unsigned dst = 0; dst < groups; ++dst) {
      auto b = v.cellBox(src, dst);
      down(v, {b.origin.x + 1., b.origin.y + 1.});
      move(v,
           {b.origin.x + 2.,
            b.origin.y + group_canvas::family.matrixGrid.width / groups * .5});
      up(v);
      flush(p, e);
      expect(near(v.value(kCrosspointBase + v.index(src, dst)), -34.),
             "every ambisonic matrix cell editable");
    }
  for (unsigned shape = 0; shape < 6; ++shape) {
    menuChoice(v, p, e, group_canvas::patternPanel, 0, shape, kParamShape);
    v.set(kParamMotion, 1.);
    flush(p, e);
    p.routingPhase.store(.37f);
    render(v, "shape-" + std::to_string(shape));
  }
  menuChoice(v, p, e, group_canvas::patternPanel, 1, 1, kParamMode);
  v.randomState = group_canvas::initialRandomSeed;
  v.randomDev = .5;
  v.randomizeMatrix();
  flush(p, e);
#if defined(__APPLE__)
  auto original = std::make_unique<Plugin>();
  original->plugin.plugin_data = original.get();
  init(&original->plugin);
  auto *cocoa =
      [[S3G_TEST_ROUTING_COCOA_VIEW alloc] initWithPlugin:original.get()];
  [cocoa randomizeMatrix];
  [cocoa release];
  for (unsigned i = 0; i < kCrosspointCount; ++i)
    expect(near(v.value(kCrosspointBase + i), original->params.crosspointDb[i]),
           "ambisonic random seed/distribution matches Cocoa");
#endif
  const auto glossary = group_canvas::family.glossary;
  click(v, {glossary.x + 10., glossary.y + 10.});
  expect(!v.showGlossary, "glossary collapses");
  render(v);
  click(v, {glossary.x + 10., glossary.y + 10.});
  expect(v.showGlossary, "glossary expands");
#elif S3G_ROUTING_FAMILY == 2
  expect(kParamNodeLimit == 8 && kParamNodeStride == 7,
         "ambisonic node parameter map");
  v.set(kParamLockZ, 0);
  v.set(kParamNodeCount, 8);
  v.set(kParamCursorX, -1.9);
  v.set(kParamCursorY, -1.9);
  for (unsigned i = 0; i < 8; ++i) {
    v.set(kParamNodeBase + i * 7 + 2, -1.2 + (i / 4) * 1.2);
    v.set(kParamNodeBase + i * 7 + 3, -1.2 + (i % 4) * .8);
  }
  flush(p, e);
  v.setView(0);
  for (unsigned i = 0; i < 8; ++i) {
    render(v);
    auto a = v.nodePoint(i);
    down(v, a);
    expect(v.selectedNode == i && v.spatialDrag == 1,
           "all eight ambisonic nodes selectable");
    move(v, {a.x + 5., a.y + 4.});
    up(v);
    flush(p, e);
  }
#if defined(__APPLE__)
  auto *cocoa = [[S3GAmbiNodeBusMixerView alloc] initWithPlugin:&p];
  for (int mode = 0; mode < 3; ++mode) {
    v.setView(mode);
    [cocoa setValue:@(mode) forKey:@"viewMode"];
    auto r = R::box(node_canvas::family.fieldPlot);
    for (float x : {-.8f, .6f})
      for (float z : {-.5f, .4f}) {
        auto a = v.project(x, .3f, z, r);
        auto b = [cocoa projectX:x
                               y:.3f
                               z:z
                            rect:NSMakeRect(r.origin.x, r.origin.y,
                                            r.size.width, r.size.height)];
        expect(near(a.x, b.x) && near(a.y, b.y),
               "ambisonic node projection matches Cocoa");
      }
  }
  [cocoa release];
#endif
  v.set(kParamLockZ, 1);
  flush(p, e);
  render(v, "field");
  expect(v.state.cursorZ == 0.f && v.state.nodes[0].z == 0.f, "node Z locking");
#elif S3G_ROUTING_FAMILY == 5
  const auto &f = ambi_rotate_canvas::family;
#if !S3G_AMBI_ROTATE_GROUP
  for (unsigned order = 0; order < 7; ++order)
    menuChoice(v, p, e, f.output, 1, order, kParamOrder, 1);
#endif
  const auto &rotation =
      ambi_rotate_canvas::grouped ? f.primarySix : f.primarySeven;
  sliderChoice(v, p, e, rotation, 0, kParamYaw);
  v.set(kParamSpread, .61);
  v.set(kParamTilt, -.37);
  v.set(kParamTwist, .48);
  flush(p, e);
#if defined(__APPLE__)
  auto *cocoa = [[S3G_TEST_ROUTING_COCOA_VIEW alloc] initWithPlugin:&p];
#endif
  for (int mode = 0; mode < 3; ++mode) {
    render(v);
    click(v, center(v.viewButton(mode)));
    expect(v.viewMode == mode, "camera view button");
#if defined(__APPLE__)
    [cocoa setViewPreset:mode];
    const auto r = R::box(f.fieldPlot);
    for (float x : {-.8f, .7f})
      for (float z : {-.2f, .6f}) {
        s3g::Vec3 vec{x, .3f, z};
        auto a = v.project(vec, 122.);
        auto b = [cocoa project:vec
                           rect:NSMakeRect(r.origin.x, r.origin.y, r.size.width,
                                           r.size.height)
                          scale:122.];
        expect(near(a.x, b.x) && near(a.y, b.y),
               "rotation projection matches Cocoa");
      }
#endif
    render(v, "view-" + std::to_string(mode));
  }
#if defined(__APPLE__)
  [cocoa release];
#endif
  down(v, {100., 100.});
  move(v, {120., 110.});
  up(v);
  expect(v.viewMode == -1 && near(v.viewAz, -38.) && near(v.viewEl, 29.5),
         "free camera drag parity");
  v.setView(1);
#elif S3G_ROUTING_FAMILY == 6
  const auto &f = ambi_depth_canvas::family;
  const auto &primary = kSingleField ? f.primaryFour : f.primarySix;
  sliderChoice(v, p, e, primary, 0, kParamDepth);
  if constexpr (kSingleField) {
    const clap_id ids[] = {kParamTail, kParamEnvironmentSize,
                           kParamEnvironmentDecay, kParamEnvironmentDamping};
    for (unsigned i = 0; i < 4; ++i)
      sliderChoice(v, p, e, f.secondaryFour, i, ids[i]);
  }
  for (double depth : {-1., 0., 1.}) {
    v.set(kParamDepth, depth);
    v.set(kParamAir, .7);
    v.set(kParamTail, .8);
    if constexpr (!kSingleField)
      v.set(kParamSpread, .75);
    flush(p, e);
    for (unsigned g = 0; g < kGroups; ++g) {
      const auto state = p.processor.groupState(g);
      const auto r = R::box(f.fieldPlot);
      const double u = kGroups <= 1 ? .5 : (g + .5) / kGroups;
      const auto point = v.groupPoint(g);
      expect(near(point.x, r.origin.x + 34. + u * (r.size.width - 68.)) &&
                 near(point.y, r.origin.y + 42. +
                                   state.depth * (r.size.height - 86.) +
                                   std::sin((g + .25) * 1.9) * 18. *
                                       std::abs(p.params.spread)),
             "depth graphic uses exact DSP group depth");
    }
    render(v, "depth-" + std::to_string(int(depth)));
  }
#else
  const auto &f = ambi_order_canvas::family;
  for (unsigned order = 0; order < 7; ++order)
    menuChoice(v, p, e, f.output, 1, order, kParamOrder, 1);
  for (unsigned weighting = 0; weighting < 4; ++weighting) {
    menuChoice(v, p, e, f.weighting, 0, weighting, kParamWeighting);
    for (unsigned i = 0; i < 8; ++i) {
      sliderChoice(v, p, e, f.orderBands, i, kParamOrder0 + i);
      const auto &params = p.params;
      const float standard = s3g::ambiUtilityStandardOrderWeight(
          params.weighting, i, params.order);
      expect(near(v.weight(i),
                  s3g::lerp(params.orderGain[i], standard * params.orderGain[i],
                            params.blend)),
             "order weight formula parity");
    }
    render(v, "weight-" + std::to_string(weighting));
  }
#endif
  e.balanced();
}

void legacyState(Editor &v, Plugin &p, Events &events) {
#if S3G_ROUTING_FAMILY == 5 || S3G_ROUTING_FAMILY == 6
  // Exercise the actual old binary structs and short stream reads. Restore
  // the current state afterwards so the later preset/camera checks are
  // independent.
  struct Memory {
    std::vector<uint8_t> data;
    size_t position = 0;
    clap_ostream_t out{this,
                       [](const clap_ostream_t *s, const void *buffer,
                          uint64_t size) -> int64_t {
                         auto &m = *static_cast<Memory *>(s->ctx);
                         const auto *b = static_cast<const uint8_t *>(buffer);
                         m.data.insert(m.data.end(), b, b + size);
                         return int64_t(size);
                       }};
    clap_istream_t in{
        this,
        [](const clap_istream_t *s, void *buffer, uint64_t size) -> int64_t {
          auto &m = *static_cast<Memory *>(s->ctx);
          size = std::min<uint64_t>({size, 7, m.data.size() - m.position});
          std::memcpy(buffer, m.data.data() + m.position, size);
          m.position += size;
          return int64_t(size);
        }};
  } current;
  expect(stateSave(&p.plugin, &current.out), "save before legacy recall");
  const auto loadOld = [&](const auto &old) {
    Memory m;
    const auto *data = reinterpret_cast<const uint8_t *>(&old);
    m.data.assign(data, data + sizeof(old));
    expect(stateLoad(&p.plugin, &m.in), "legacy state decoder");
    flush(p, events);
  };
#if S3G_ROUTING_FAMILY == 5
#if S3G_AMBI_ROTATE_GROUP
  SavedStateV1 v1{};
  v1.params.yawDeg = 37.f;
  v1.params.spread = .63f;
  loadOld(v1);
  expect(near(v.value(kParamYaw), 37.) && near(v.value(kParamSpread), .63),
         "group rotation v1 recall");
#else
  OldSavedStateV1 v1{};
  v1.params.order = 3;
  v1.params.yawDeg = 37.f;
  loadOld(v1);
  expect(near(v.value(kParamYaw), 37.) && near(v.value(kParamOrder), 3.) &&
             near(v.value(kParamSpread), 0.),
         "rotation v1 defaults new relationships");
  SavedStateV2 v2{};
  v2.params.order = 5;
  v2.params.spread = .63f;
  loadOld(v2);
  expect(near(v.value(kParamOrder), 5.) && near(v.value(kParamSpread), .63),
         "rotation v2 recall");
#endif
#else
  OldSavedStateV1 v1{};
  v1.params.depth = .37f;
  v1.params.focus = -.48f;
  loadOld(v1);
  expect(near(v.value(kParamDepth), .37) && near(v.value(kParamFocus), -.48) &&
             near(v.value(kParamTail), 0.),
         "depth v1 defaults tail");
  OldSavedStateV2 v2{};
  v2.params.tail = .72f;
  v2.params.depth = -.33f;
  loadOld(v2);
  expect(near(v.value(kParamDepth), -.33) && near(v.value(kParamTail), .72),
         "depth v2 recall");
  if constexpr (kSingleField)
    expect(near(v.value(kParamEnvironmentSize), .5) &&
               near(v.value(kParamEnvironmentDecay), .5) &&
               near(v.value(kParamEnvironmentDamping), .5),
           "legacy environment defaults");
#endif
  expect(stateLoad(&p.plugin, &current.in), "restore after legacy checks");
  flush(p, events);
#endif
}

template <typename Sample> void audio(Editor &v, Plugin &p, Events &events) {
#if S3G_ROUTING_FAMILY == 2
  // The cursor deliberately ended outside all node radii in the hit tests.
  // Put it on an active node before asserting audible output.
  v.refreshState();
  v.set(kParamCursorX, v.state.nodes[0].x);
  v.set(kParamCursorY, v.state.nodes[0].y);
  v.set(kParamCursorZ, v.state.nodes[0].z);
  flush(p, events);
#endif
  constexpr unsigned count = 128, frames = 64;
  std::array<std::array<Sample, frames>, count> in{}, out{};
  std::array<Sample *, count> ip{}, op{};
  for (unsigned c = 0; c < count; ++c) {
    ip[c] = in[c].data();
    op[c] = out[c].data();
    for (unsigned f = 0; f < frames; ++f)
      in[c][f] = Sample(.01 * std::sin(f * .13 + c * .2));
  }
  clap_audio_buffer_t ib{}, ob{};
  ib.channel_count = ob.channel_count = count;
  if constexpr (std::is_same_v<Sample, float>) {
    ib.data32 = ip.data();
    ob.data32 = op.data();
  } else {
    ib.data64 = ip.data();
    ob.data64 = op.data();
  }
  clap_process_t block{};
  block.frames_count = frames;
  block.audio_inputs = &ib;
  block.audio_outputs = &ob;
  block.audio_inputs_count = block.audio_outputs_count = 1;
  block.out_events = &events.out;
  for (unsigned i = 0; i < 8; ++i)
    expect(process(&p.plugin, &block) == CLAP_PROCESS_CONTINUE,
           "audio processing");
  double energy = 0.;
  for (auto &channel : out)
    for (auto sample : channel) {
      expect(std::isfinite(sample), "finite audio");
      energy += std::abs(sample);
    }
  expect(energy > 0., "non-silent audio");
  // Host changes win over older, backpressured GUI notifications.
  const auto id = S3G_TEST_OUTPUT_PARAM;
  v.set(id, -12.);
  clap_event_param_value_t automation{};
  automation.header = {sizeof(automation), 0, CLAP_CORE_EVENT_SPACE_ID,
                       CLAP_EVENT_PARAM_VALUE, 0};
  automation.param_id = id;
  automation.value = -3.;
  clap_input_events_t input{
      &automation, [](const clap_input_events_t *) -> uint32_t { return 1; },
      [](const clap_input_events_t *e,
         uint32_t) -> const clap_event_header_t * {
        return &static_cast<clap_event_param_value_t *>(e->ctx)->header;
      }};
  block.in_events = &input;
  events.limit = 0;
  process(&p.plugin, &block);
  block.in_events = nullptr;
  events.limit = 100000;
  process(&p.plugin, &block);
  expect(near(v.value(id), -3.),
         "delayed GUI event cannot replay over host automation");
  events.balanced();
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
  host.name = "Ambisonic parity";
  host.vendor = "s3g";
  host.version = "1";
  host.url = "";
  host.get_extension = [](const clap_host_t *, const char *) -> const void * {
    return nullptr;
  };
  host.request_process = [](const clap_host_t *) {};
  auto *plugin = factory.create_plugin(&factory, &host, descriptor.id);
  expect(plugin && plugin->init(plugin) &&
             plugin->activate(plugin, 48000., 1, 64),
         "plugin lifecycle");
  if (plugin) {
    {
      auto &p = *self(plugin);
      Editor v(p);
      Events events;
      render(v, "initial");
      parameters(v, p, events);
      family(v, p, events);
      audio<float>(v, p, events);
      audio<double>(v, p, events);
      legacyState(v, p, events);
#if S3G_ROUTING_FAMILY == 2
      nodeCursorPublication(v, p, events);
#endif
      presets(v, p, events);
#if S3G_ROUTING_FAMILY == 5
      expect(v.viewMode == 1 && p.guiViewMode == 1,
             "preset camera mode roundtrip");
#endif
    }
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
  }
  F::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " ambisonic canvas parity passed\n";
  return ok ? 0 : 1;
}
