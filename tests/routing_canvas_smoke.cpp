#include "routing_canvas_test_support.inc"
void family(Editor &v, Plugin &p, Events &e) {
#if S3G_ROUTING_FAMILY == 1
  for (unsigned mode = 0; mode < 3; ++mode) {
    v.set(kParamGroupSize, mode);
    flush(p, e);
    render(v);
    const auto groups = v.groups();
    expect(groups == (16u >> mode), "matrix group count");
    for (unsigned src = 0; src < groups; ++src)
      for (unsigned dst = 0; dst < groups; ++dst) {
        auto b = v.cellBox(src, dst);
        down(v, {b.origin.x + 1., b.origin.y + 1.});
        move(v, {b.origin.x + 2.,
                 b.origin.y +
                     group_canvas::family.matrixGrid.width / groups * .5});
        up(v);
        flush(p, e);
        expect(near(v.value(kCrosspointBase + v.index(src, dst)), -34.),
               "every crosspoint cell editable");
      }
    render(v, "groups-" + std::to_string(groups));
  }
  for (unsigned shape = 0; shape < 6; ++shape) {
    v.set(kParamShape, shape);
    v.set(kParamMotion, 1.);
    flush(p, e);
    p.routingPhase.store(.37f);
    render(v, "shape-" + std::to_string(shape));
  }
  v.randomState = group_canvas::initialRandomSeed;
  v.randomDev = .5;
  v.randomizeMatrix();
  flush(p, e);
#if defined(__APPLE__)
  auto original = std::make_unique<Plugin>();
  original->plugin.plugin_data = original.get();
  init(&original->plugin);
  applyParam(*original, kParamGroupSize, v.value(kParamGroupSize));
  auto *cocoa =
      [[S3G_TEST_ROUTING_COCOA_VIEW alloc] initWithPlugin:original.get()];
  [cocoa randomizeMatrix];
  [cocoa release];
  for (unsigned i = 0; i < kCrosspointCount; ++i)
    expect(near(v.value(kCrosspointBase + i), original->params.crosspointDb[i]),
           "random matrix matches Cocoa seed/distribution");
#endif
  render(v);
  const auto glossary = group_canvas::family.glossary;
  click(v, {glossary.x + 10., glossary.y + 10.});
  expect(!v.showGlossary, "glossary collapse");
  click(v, {glossary.x + 10., glossary.y + 10.});
  expect(v.showGlossary, "glossary expand");
#elif S3G_ROUTING_FAMILY == 2
  v.set(kParamLockZ, 0);
  flush(p, e);
  v.refreshState();
#if defined(__APPLE__)
  auto *cocoa = [[S3GNodeBusMixerView alloc] initWithPlugin:&p];
  for (int mode = 0; mode < 3; ++mode) {
    v.setView(mode);
    [cocoa setValue:@(mode) forKey:@"viewMode"];
    for (float x : {-1.f, 0.f, 1.f})
      for (float y : {-.7f, .8f})
        for (float z : {-.3f, .5f}) {
          auto r = R::box(node_canvas::family.fieldPlot);
          auto a = v.project(x, y, z, r);
          auto b = [cocoa projectX:x
                                 y:y
                                 z:z
                              rect:NSMakeRect(r.origin.x, r.origin.y,
                                              r.size.width, r.size.height)];
          expect(near(a.x, b.x) && near(a.y, b.y),
                 "node projection matches Cocoa");
        }
  }
  [cocoa release];
#endif
  v.set(kParamNodeCount, 16);
  v.set(kParamCursorX, -1.9);
  v.set(kParamCursorY, -1.9);
  flush(p, e);
  v.setView(0);
  for (unsigned i = 0; i < 16; ++i) {
    v.set(kParamNodeBase + i * 10 + 5, -1.4 + (i / 4) * .8);
    v.set(kParamNodeBase + i * 10 + 6, -1.4 + (i % 4) * .8);
  }
  flush(p, e);
  v.refreshState();
  for (unsigned i = 0; i < 16; ++i) {
    render(v);
    auto a = v.nodePoint(i);
    down(v, a);
    expect(v.selectedNode == i && v.spatialDrag == 1, "every node selectable");
    move(v, {a.x + 5., a.y + 4.});
    up(v);
    flush(p, e);
  }
  for (unsigned shape = 0; shape < s3g::kNodeTrackRegularLayoutCount; ++shape) {
    v.batch({{kParamNodeBase + 2, double(shape)},
             {kParamNodeBase + 3,
              double(s3g::nodeTrackDefaultChannelsForLayout(
                  s3g::nodeTrackRegularLayoutFromIndex(shape)))}});
    flush(p, e);
    render(v, shape == 6 ? "cube" : shape == 13 ? "double-ring" : "");
  }
  render(v);
  down(v, {80., 100.}, 1, true);
  move(v, {100., 110.});
  up(v);
  expect(v.viewMode == 2 && near(v.viewYaw, -28.) && near(v.viewPitch, 33.5),
         "shift camera rotation");
  v.set(kParamLockZ, 1);
  flush(p, e);
  v.refreshState();
  expect(v.state.cursorZ == 0.f && v.state.nodes[0].z == 0.f,
         "lock Z consistency");
  render(v, "field");
#elif S3G_ROUTING_FAMILY == 3
  v.set(kActiveParamId, kChannelCount);
  flush(p, e);
  render(v, "hpf");
  for (unsigned poles = 1; poles <= 4; ++poles)
    for (double cutoff : {20., 70., 240.})
      for (unsigned i = 0; i <= 160; ++i) {
        const auto hz = Editor::normFreq(i / 160.);
#if defined(__APPLE__)
        expect(near(Editor::magnitudeDb(hz, cutoff, poles),
                    arrayCalibrateMagnitudeDb(hz, cutoff, poles)),
               "filter graph formula matches Cocoa");
#endif
      }
  for (unsigned tab = 1; tab <= 2; ++tab) {
    v.tab = tab;
    for (unsigned page = 0; page < v.pageCount(); ++page) {
      v.page = page;
      render(v);
      for (unsigned row = 0;
           row < kRowsPerPage && page * kRowsPerPage + row < kChannelCount;
           ++row) {
        const auto plot = calibrate_canvas::family.channelPlot;
        const CPoint point{plot.x + plot.width * .5,
                           plot.y + row * kRowHeight + 4.};
        down(v, point);
        up(v);
        flush(p, e);
        const auto id = (tab == 1 ? kDelayParamBaseId : kGainParamBaseId) +
                        page * kRowsPerPage + row;
        expect(near(v.value(id), tab == 1 ? 2000. : -21.),
               "every paged delay/trim row editable");
      }
      render(v, (tab == 1 ? "delay-" : "trim-") + std::to_string(page));
    }
  }
  v.tab = 2;
  v.page = 0;
  render(v);
  const double y = calibrate_canvas::family.channelPlot.y;
  click(v, {calibrate_canvas::family.channelMuteColumn.x + 4., y + 3.});
  click(v, {calibrate_canvas::family.channelInvertColumn.x + 4., y + 3.});
  flush(p, e);
  expect(v.value(kMuteParamBaseId) == 1. && v.value(kInvertParamBaseId) == 1.,
         "mute and polarity");
  v.set(kBypassParamId, 1);
  flush(p, e);
  render(v);
  click(v, {calibrate_canvas::family.editor.frame.x + 350.,
            calibrate_canvas::family.editor.frame.y + 10.});
  flush(p, e);
  expect(v.value(kTrimBypassParamId) == 0.,
         "stage control inactive in global bypass");
  v.set(kBypassParamId, 0);
  flush(p, e);
  render(v);
  click(v, {calibrate_canvas::family.editor.frame.x + 350.,
            calibrate_canvas::family.editor.frame.y + 10.});
  flush(p, e);
  expect(v.value(kTrimBypassParamId) == 1. &&
             near(v.value(kGainParamBaseId), -21.),
         "stage bypass retains trim");
#else
  for (unsigned layout = 0; layout < kLayoutCount; ++layout) {
    expect(v.chooseLayout(layout), "speaker layout choice");
    flush(p, e);
    render(v, layout == 0 ? "speaker-map" : "");
    const unsigned count = unsigned(v.value(kParamHighChannels));
    expect(v.value(kParamSubOffset) == std::min(64u, count + 1),
           "sub offset follows selected speaker layout");
  }
  for (unsigned subs = 1; subs <= 8; ++subs) {
    v.set(kParamSubCount, subs);
    flush(p, e);
    render(v, subs == 8 ? "eight-subs" : "");
  }
  v.set(kParamHighChannels, 12);
  flush(p, e);
  expect(v.value(kParamSubOffset) == 13., "HIGH CH relocates subs");
  v.set(kParamSubOffset, 32);
  flush(p, e);
  expect(v.value(kParamSubOffset) == 32., "explicit sub offset retained");
  v.chooseLayout(
      menuIndexForLayoutPreset(unsigned(s3g::LayoutPannerPreset::Cube8)));
  flush(p, e);
  render(v);
  expect(v.groupedLabels.size() < 8, "coincident top-view labels grouped");
#endif
  e.balanced();
}
#if defined(__APPLE__) && S3G_ROUTING_FAMILY == 3
void numericEntry(Plugin &p, Events &events) {
  auto *window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)
                styleMask:NSWindowStyleMaskTitled
                  backing:NSBackingStoreBuffered
                    defer:NO];
  [window setReleasedWhenClosed:NO];
  {
    F::EditorHost host(kGuiWidth, kGuiHeight, kGuiWidth, kGuiHeight);
    auto *editor = new Editor(p);
    editor->tab = 1;
    expect(host.ready() && host.attach(editor) &&
               host.setParent([window contentView]) && host.setVisible(true),
           "numeric editor native frame");
    for (unsigned tab = 1; tab <= 2; ++tab) {
      editor->tab = tab;
      editor->page = 0;
      render(*editor);
      const auto &family = calibrate_canvas::family;
      click(*editor,
            {family.channelValueColumn.x + 10., family.channelPlot.y + 4.});
      CTextEdit *field = nullptr;
      auto *frame = editor->getFrame();
      for (unsigned i = 0; frame && i < frame->getNbViews(); ++i)
        if (auto *found = dynamic_cast<CTextEdit *>(frame->getView(i)))
          field = found;
      expect(field != nullptr, "numeric field opens");
      if (field) {
        field->setText(tab == 1 ? "123.456" : "-12.25");
        editor->valueChanged(field);
        editor->finishNumeric();
        flush(p, events);
        expect(
            near(editor->value(tab == 1 ? kDelayParamBaseId : kGainParamBaseId),
                 tab == 1 ? 123.456 : -12.25),
            "typed delay/trim committed");
      }
    }
    host.setVisible(false);
  }
  [window close];
  [window release];
  events.balanced();
}
#endif
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
  auto *plugin = factory.create_plugin(&factory, &host, descriptor.id);
  expect(plugin && plugin->init(plugin) &&
             plugin->activate(plugin, 48000., 1, 64),
         "plugin lifecycle");
  if (plugin) {
    {
      auto &p = *self(plugin);
      Editor v(p);
      Events e;
      render(v, "initial");
      parameters(v, p, e);
      family(v, p, e);
#if S3G_ROUTING_FAMILY == 2
      nodeCursorPublication(v, p, e);
#endif
      presets(v, p, e);
#if defined(__APPLE__) && S3G_ROUTING_FAMILY == 3
      numericEntry(p, e);
#endif
    }
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
  }
  F::releaseRuntime();
  if (ok)
    std::cout << descriptor.id << " routing canvas parity passed\n";
  return ok ? 0 : 1;
}
