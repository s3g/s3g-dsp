void captureFamily(Editor &v, Plugin &p, Events &e) {
  struct Slider {
    const s3g::gui_layout::Panel *panel;
    unsigned row;
    clap_id id;
  };
  std::vector<Slider> sliders;
#if S3G_EFFECT_FAMILY == 3
  sliders = {
      {&A::kOutputPanel, 0, kParamOutput},
      {&A::kCapturePanel, 1, kParamCaptureSeconds},
      {&A::kCapturePanel, 2, kParamSensitivity},
      {&A::kResynthesisPanel, 0, kParamTranspose},
      {&A::kResynthesisPanel, 1, kParamHarmonicPull},
      {&A::kResynthesisPanel, 2, kParamHarmonicStretch},
      {&A::kResynthesisPanel, 3, kParamDecay},
      {&A::kResynthesisPanel, 4, kParamDecayTilt},
      {&A::kResynthesisPanel, 5, kParamDrive},
      {&A::kResynthesisPanel, 6, kParamSpread},
      {&A::kResynthesisPanel, 7, kParamDeviation},
      {&A::kResynthesisPanel, 8, kParamMix},
      {&A::kTopologyPanel, 1, kParamTopologyAmount},
      {&A::kTopologyPanel, 2, kParamRoamingRate},
      {&A::kMaskPanel, 0, kParamMaskAmount},
      {&A::kMaskPanel, 1, kParamMaskAzimuth},
      {&A::kMaskPanel, 2, kParamMaskElevation},
      {&A::kMaskPanel, 3, kParamMaskWidth},
      {&A::kMaskPanel, 4, kParamMaskDry},
  };
#elif S3G_EFFECT_FAMILY == 4
  for (auto slider : A::engineSliders) {
    const s3g::gui_layout::Panel *panel = nullptr;
    unsigned row = 0;
    expect(A::sliderLocation(slider.id, panel, row), "engine slider location");
    sliders.push_back({panel, row, slider.id});
  }
  for (auto slider : A::maskSliders) {
    const s3g::gui_layout::Panel *panel = nullptr;
    unsigned row = 0;
    A::sliderLocation(slider.id, panel, row);
    sliders.push_back({panel, row, slider.id});
  }
  for (auto slider : A::topologySliders) {
    const s3g::gui_layout::Panel *panel = nullptr;
    unsigned row = 0;
    A::sliderLocation(slider.id, panel, row);
    sliders.push_back({panel, row, slider.id});
  }
  sliders.push_back({&A::kOutputPanel, 0, kParamOutput});
  sliders.push_back({&A::kOutputPanel, 1, kParamMix});
#else
  for (const auto &def : kParamDefs)
    if (!def.stepped)
      sliders.push_back({&A::displacementPanelForParam(def.id),
                         A::displacementRowForParam(def.id), def.id});
#endif
  for (const auto &s : sliders) {
    const auto &panel = *s.panel;
    const double x = s3g::gui_layout::processorControlX(panel.frame.x);
    const double w = s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double y = s3g::gui_layout::rowY(panel, s.row) + 3.;
    render(v);
    down(v, {x + w * .2, y});
    move(v, {x + w * .8, y});
    up(v);
    flush(p, e);
    if (near(v.value(s.id), v.info(s.id).default_value))
      std::cerr << "slider unchanged " << s.id << "\n";
    expect(!near(v.value(s.id), v.info(s.id).default_value),
           "capture slider edits own parameter");
    click(v, {x + w * .5, y}, 2);
    flush(p, e);
    expect(near(v.value(s.id), v.info(s.id).default_value),
           "capture slider double-click default");
    e.balanced();
  }
#if S3G_EFFECT_FAMILY == 5
  for (int mode = 0; mode < 4; ++mode) {
    v.setViewPreset(mode);
    render(v, "view-" + std::to_string(mode));
  }
  v.setViewPreset(2);
  render(v);
  const auto timeline = v.timelineRect();
  down(v,
       {timeline.origin.x + timeline.size.width * .25, timeline.origin.y + 5});
  move(v,
       {timeline.origin.x + timeline.size.width * .75, timeline.origin.y + 5});
  up(v);
  flush(p, e);
  expect(near(v.value(kParamPosition), .75) &&
             near(v.value(kParamTransport), 2),
         "timeline scrub and clock");
  v.loadDocumentationScore();
  flush(p, e);
  render(v, "score");
  expect(p.score.sceneCount == 3, "score import retains scenes");
  const auto previous = p.score;
  std::string error, name;
  auto parsed = previous;
  float a = 0, az = 0, el = 0, r = 0;
  expect(!A::parsePortableScore("{broken", parsed, a, az, el, r, name, error),
         "invalid JSON rejected");
  expect(
      A::parsePortableScore(
          R"({"format":"s3g-mc-displacement-score","version":1,"name":"écho 波","amount":0.7})",
          parsed, a, az, el, r, name, error) &&
          near(a, .7) && name == "écho 波" && parsed.sceneCount == 1,
      "portable score schema and UTF-8");
#if defined(__APPLE__)
  std::string points = "[";
  for (unsigned n = 0; n < 24; ++n) {
    if (n)
      points += ",";
    points += "{\"channel\":" + std::to_string(n + 1) +
              ",\"azimuth\":" + std::to_string(int(n) * 17 - 180) +
              ",\"elevation\":21,\"radius\":1.4}";
  }
  points += "]";
  const std::string json =
      "{\"format\":\"s3g-mc-displacement-score\",\"version\":1,"
      "\"name\":\"écho 波\",\"amount\":0.7,\"target\":" +
      points + "}";
  auto cocoaScore = previous;
  float ca = 0, caz = 0, cel = 0, cr = 0;
  std::string cname, cerror;
  NSData *data = [NSData dataWithBytes:json.data() length:json.size()];
  expect(parseScoreData(data, cocoaScore, ca, caz, cel, cr, cname, cerror) &&
             A::parsePortableScore(json, parsed, a, az, el, r, name, error) &&
             std::memcmp(&parsed, &cocoaScore, sizeof(parsed)) == 0 &&
             ca == a && caz == az && cel == el && cr == r && cname == name,
         "portable score parser matches original Cocoa schema");
#endif
#else
  for (int mode = 0; mode < 3; ++mode) {
    v.setViewPreset(mode);
    render(v, "view-" + std::to_string(mode));
  }
  struct Menu {
    const s3g::gui_layout::Panel *panel;
    unsigned row;
    clap_id id;
    std::vector<double> values;
  };
  std::vector<Menu> menus;
#if S3G_EFFECT_FAMILY == 3
  menus = {{&A::kOutputPanel, 1, kParamOrder, {1, 2, 3, 4, 5, 6, 7}},
           {&A::kCapturePanel, 0, kParamBody, {0, 3, 4, 5}},
           {&A::kCapturePanel,
            3,
            kParamModalCount,
            {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
           {&A::kTopologyPanel, 0, kParamTopology, {0, 1, 2, 3}}};
#else
  menus = {{&A::kOutputPanel, 2, kParamOrder, {1, 2, 3, 4, 5, 6, 7}},
           {&A::kEnginePanel,
            0,
            kParamBody,
#if S3G_PARTIAL_TRACE
            {0, 3, 4, 5}
#else
            {0, 1, 2, 3, 4, 5}
#endif
           },
           {&A::kTopologyPanel, 0, kParamTopology, {0, 1, 2, 3}}};
#endif
  for (const auto &m : menus)
    for (unsigned n = 0; n < m.values.size(); ++n) {
      const auto rect = s3g::gui_layout::sliderHitRect(*m.panel, m.row);
      render(v);
      click(v, {rect.x + 10, rect.y + rect.height * .5});
      render(v);
      expect(v._openMenu != 0, "capture custom menu opens");
      click(v, {v._menuOrigin.x + 10, v._menuOrigin.y + (n + .5) * 18.});
      flush(p, e);
      expect(near(v.value(m.id), m.values[n]), "capture custom menu mapping");
    }
#if S3G_EFFECT_FAMILY == 3
  // Each pickup retains separate TUNE and DECAY rails, including pick-up 24.
  v.set(kParamBody, 5);
  flush(p, e);
  for (unsigned node = 0; node < 24; ++node) {
    v._selectedPickup = node;
    render(v);
    for (auto axis :
         {std::make_pair(A::tuneAxisRect(), kParamPickupTuneFirst),
          std::make_pair(A::decayAxisRect(), kParamPickupDecayFirst)}) {
      down(v, {axis.first.origin.x + axis.first.size.width * .25,
               axis.first.origin.y + 4});
      move(v, {axis.first.origin.x + axis.first.size.width * .75,
               axis.first.origin.y + 4});
      up(v);
      flush(p, e);
      expect(near(v.value(axis.second + node), .5), "per-pickup capture rail");
      click(v,
            {axis.first.origin.x + axis.first.size.width * .5,
             axis.first.origin.y + 4},
            2);
      flush(p, e);
    }
  }
#endif
#endif
  const auto press = [&](D::Rect r) {
    click(v, {D::midX(r), D::midY(r)});
    flush(p, e);
  };
#if S3G_EFFECT_FAMILY == 3
  press(A::captureButtonRect());
  expect(p.capturing.load(), "capture button starts analysis");
  press(A::captureButtonRect());
  expect(!p.capturing.load(), "capture button cancels analysis");
  press(A::clearButtonRect());
  expect(!p.hasPrint.load(), "clear button removes print");
#elif S3G_EFFECT_FAMILY == 4
  for (double norm : {0., .25, .75, 1.}) {
    const auto& panel = A::kMaskPanel;
    click(v, {s3g::gui_layout::processorControlX(panel.frame.x)
                  + s3g::gui_layout::processorTrackWidth(panel.frame.width)*norm,
              s3g::gui_layout::rowY(panel,2)+3.});
    flush(p,e);
    expect(near(v.value(kParamMaskElevation),90.-180.*norm),
           "Trace elevation runs +90 left to -90 right");
  }
#if S3G_PARTIAL_TRACE
  const auto beforeFreeze = v.value(kParamFreeze);
  press(A::actionRect(1));
  expect(!near(v.value(kParamFreeze), beforeFreeze), "freeze button toggles");
  press(A::actionRect(1));
  expect(near(v.value(kParamFreeze), beforeFreeze), "unfreeze button toggles");
  v.set(kParamMaximumFrequency, 200);
  v.set(kParamMinimumFrequency, 4000);
  flush(p, e);
  expect(near(v.value(kParamMaximumFrequency), 4020),
         "coupled frequency range publishes canonical high bound");
#else
  press(A::actionRect(0));
  expect(p.capturing.load(), "response capture button starts analysis");
  press(A::actionRect(2));
  expect(!p.capturing.load(), "response clear button cancels capture");
#endif
#endif
  v.resetParameters();
  flush(p, e);
  e.balanced();
}
