#include "routing_canvas_test_support.inc"
namespace A = ambi_effect_canvas;
namespace D = s3g::portable_gui::ambi_effect_drawing;
#if S3G_EFFECT_FAMILY >= 3
#include "ambi_effect_capture_canvas_tests.inc"
#endif
void family(Editor &v, Plugin &p, Events &e) {
#if S3G_EFFECT_FAMILY < 3
#if S3G_EFFECT_FAMILY == 0
  struct Hit {
    s3g::gui_layout::Rect rect;
    clap_id id;
  };
  const Hit sliders[]{
      {s3g::gui_layout::sliderHitRect(A::kLayout.output, 0u), kParamOutput},
      {s3g::gui_layout::sliderHitRect(A::kFilterPanel, 1u), kParamFilter},
      {s3g::gui_layout::sliderHitRect(A::kFilterPanel, 2u), kParamResonance},
      {s3g::gui_layout::sliderHitRect(A::kFilterPanel, 3u), kParamSpread},
      {s3g::gui_layout::sliderHitRect(A::kFilterPanel, 4u), kParamDeviation},
      {s3g::gui_layout::sliderHitRect(A::kFilterPanel, 5u), kParamMix},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 1u),
       kParamTopologyAmount},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 2u),
       kParamRoamingRate},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 0u), kParamMaskAmount},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 1u), kParamMaskAzimuth},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 2u), kParamMaskElevation},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 3u), kParamMaskWidth},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 4u), kParamMaskCurve},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 5u), kParamMaskDry},
  };
  struct MenuHit {
    s3g::gui_layout::Rect rect;
    int menu;
    unsigned count;
  };
  const MenuHit menus[]{
      {s3g::gui_layout::sliderHitRect(A::kLayout.output, 1u), 1, 7u},
      {s3g::gui_layout::sliderHitRect(A::kFilterPanel, 0u), 2, 4u},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 0u), 3, 4u},
  };
  const D::Rect axes[]{A::pickupFilterAxisRect(), A::pickupResonanceAxisRect()};
  const clap_id axesId[]{kParamPickupFilterFirst, kParamPickupResonanceFirst};
#elif S3G_EFFECT_FAMILY == 1
  struct Hit {
    s3g::gui_layout::Rect rect;
    clap_id id;
  };
  const Hit sliders[]{
      {s3g::gui_layout::sliderHitRect(A::kLayout.output, 0u), kParamOutput},
      {s3g::gui_layout::sliderHitRect(A::kDelayPanel, 1u), kParamTime},
      {s3g::gui_layout::sliderHitRect(A::kDelayPanel, 2u), kParamFeedback},
      {s3g::gui_layout::sliderHitRect(A::kDelayPanel, 3u), kParamTone},
      {s3g::gui_layout::sliderHitRect(A::kDelayPanel, 4u), kParamSpread},
      {s3g::gui_layout::sliderHitRect(A::kDelayPanel, 5u), kParamDeviation},
      {s3g::gui_layout::sliderHitRect(A::kDelayPanel, 6u), kParamMix},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 1u),
       kParamTopologyAmount},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 2u),
       kParamRoamingRate},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 0u), kParamMaskAmount},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 1u), kParamMaskAzimuth},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 2u), kParamMaskElevation},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 3u), kParamMaskWidth},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 4u), kParamMaskCurve},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 5u), kParamMaskDry},
  };
  struct MenuHit {
    s3g::gui_layout::Rect rect;
    int menu;
    unsigned count;
  };
  const MenuHit menus[]{
      {s3g::gui_layout::sliderHitRect(A::kLayout.output, 1u), 1, 7u},
      {s3g::gui_layout::sliderHitRect(A::kDelayPanel, 0u), 2, 4u},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 0u), 3, 4u},
  };
  const D::Rect axes[]{A::timeAxisRect(), A::feedbackAxisRect()};
  const clap_id axesId[]{kParamPickupTimeFirst, kParamPickupFeedbackFirst};
#elif S3G_EFFECT_FAMILY == 2
  struct Hit {
    s3g::gui_layout::Rect rect;
    clap_id id;
  };
  const Hit sliders[]{
      {s3g::gui_layout::sliderHitRect(A::kLayout.output, 0), kParamOutput},
      {s3g::gui_layout::sliderHitRect(A::kEnginePanel, 1), kParamPrimary},
      {s3g::gui_layout::sliderHitRect(A::kEnginePanel, 2),
       kPitch ? kParamWindow : kParamSpread},
      {s3g::gui_layout::sliderHitRect(A::kEnginePanel, 3),
       kPitch ? kParamGlide : kParamDeviation},
      {s3g::gui_layout::sliderHitRect(A::kEnginePanel, 4),
       kPitch ? kParamSpread : kParamMix},
      {s3g::gui_layout::sliderHitRect(A::kEnginePanel, 5),
       kPitch ? kParamDeviation : CLAP_INVALID_ID},
      {s3g::gui_layout::sliderHitRect(A::kEnginePanel, 6),
       kPitch ? kParamMix : CLAP_INVALID_ID},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 1),
       kParamTopologyAmount},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 2), kParamRoamingRate},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 0), kParamMaskAmount},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 1), kParamMaskAzimuth},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 2), kParamMaskElevation},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 3), kParamMaskWidth},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 4), kParamMaskCurve},
      {s3g::gui_layout::sliderHitRect(A::kMaskPanel, 5), kParamMaskDry},
  };
  struct MenuHit {
    s3g::gui_layout::Rect rect;
    int menu;
    unsigned count;
  };
  const MenuHit menus[]{
      {s3g::gui_layout::sliderHitRect(A::kLayout.output, 1u), 1, 7u},
      {s3g::gui_layout::sliderHitRect(A::kEnginePanel, 0u), 2, 4u},
      {s3g::gui_layout::sliderHitRect(A::kTopologyPanel, 0u), 3, 4u},
  };
  const D::Rect axes[]{A::primaryAxisRect(), A::secondaryAxisRect()};
  const clap_id axesId[]{kParamPickupPrimaryFirst, kParamPickupSecondaryFirst};
#endif

  for (const auto &h : sliders) {
    if (h.id == CLAP_INVALID_ID)
      continue;
    const double x = s3g::gui_layout::processorControlX(536.);
    const double w = s3g::gui_layout::processorTrackWidth(266.);
    const double y = h.rect.y + h.rect.height * .5;
    render(v);
    down(v, {x + w * .2, y});
    move(v, {x + w * .8, y});
    up(v);
    flush(p, e);
    const double edited = v.value(h.id);
    expect(std::isfinite(edited), "slider drag finite");
    expect(!near(edited, v.info(h.id).default_value),
           "slider changes its own parameter");
    click(v, {x + w * .5, y}, 2);
    flush(p, e);
    expect(near(v.value(h.id), v.info(h.id).default_value),
           "slider resets its own parameter");
    e.balanced();
  }
  for (const auto &h : menus) {
    for (unsigned n = 0; n < h.count; ++n) {
      render(v);
      click(v, {h.rect.x + 8., h.rect.y + h.rect.height * .5});
      expect(v._openMenu == h.menu, "correct custom menu opens");
      render(v);
      click(v, {v._menuOrigin.x + 8., v._menuOrigin.y + (n + .5) * 18.});
      flush(p, e);
      const clap_id id =
          h.menu == 1 ? kParamOrder : h.menu == 2 ? kParamBody : kParamTopology;
      const double expected =
          h.menu == 1 ? n + 1. : h.menu == 2 ? (n ? n + 2. : 0.) : n;
      expect(near(v.value(id), expected), "custom menu value mapping");
    }
  }
  for (unsigned a = 0; a < 2; ++a) {
#if S3G_EFFECT_FAMILY == 2
    if (!kPitch && a == 1)
      continue;
#endif
    v._selectedPickup = 0;
    render(v);
    auto r = axes[a];
    down(v, {r.origin.x + r.size.width * .25, r.origin.y + 4.});
    move(v, {r.origin.x + r.size.width * .75, r.origin.y + 4.});
    up(v);
    flush(p, e);
    expect(near(v.value(axesId[a]), .5), "pickup rail continuous drag");
    click(v, {r.origin.x + r.size.width * .5, r.origin.y + 4.}, 2);
    flush(p, e);
    expect(near(v.value(axesId[a]), 0), "pickup rail double-click");
  }
  e.balanced();
  for (int mode = 0; mode < 3; ++mode) {
    v.setViewPreset(mode);
    render(v, "view-" + std::to_string(mode));
    expect(p.guiViewMode == mode, "camera stored");
  }
  v.resetParameters();
  flush(p, e);
  e.balanced();
#else
  captureFamily(v, p, e);
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
  constexpr unsigned count = 64, frames = 64;
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
  for (unsigned i = 0; i < 32; ++i)
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
  unsigned restarts = 0;
  host.host_data = &restarts;
  host.request_restart = [](const clap_host_t *h) {
    ++*static_cast<unsigned *>(h->host_data);
  };
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
#if S3G_EFFECT_FAMILY != 5
      audio<double>(v, p, events);
#endif

#if S3G_ROUTING_FAMILY == 2
      nodeCursorPublication(v, p, events);
#endif
      presets(v, p, events);
#if S3G_EFFECT_FAMILY == 3 || S3G_EFFECT_FAMILY == 4
      expect(restarts > 0,
             "active capture-state load requests safe host restart");
      plugin->deactivate(plugin);
      expect(plugin->activate(plugin, 48000., 1, 64), "capture-state restart");
      render(v, "restored");
#endif
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
    std::cout << descriptor.id << " ambisonic effect canvas parity passed\n";
  return ok ? 0 : 1;
}
