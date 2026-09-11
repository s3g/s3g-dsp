// FIELD/MIXER/DESIGN are literal adaptations of the retained Cocoa panners.
namespace panner_canvas {
using namespace s3g::portable_gui::routing;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &family = layout::kPannerFamilyLayout;
constexpr const char *names[] = {"Panner Layout", "Panner DBAP", "Panner LBAP",
                                 "Panner VBAP"};
constexpr s3g::LayoutPannerCustomShape shapes[] = {
    s3g::LayoutPannerCustomShape::Auto, s3g::LayoutPannerCustomShape::Ring,
    s3g::LayoutPannerCustomShape::Dome, s3g::LayoutPannerCustomShape::Geo,
    s3g::LayoutPannerCustomShape::Stack};
class Editor final : public Canvas {
public:
  explicit Editor(Plugin &instance)
      : Canvas(kGuiWidth, kGuiHeight), p(instance) {
    if (p.host && p.host->get_extension)
      p.hostParams = static_cast<const clap_host_params_t *>(
          p.host->get_extension(p.host, CLAP_EXT_PARAMS));
    _viewMode = p.guiViewMode;
    _viewAzDeg = p.guiViewAzDeg;
    _viewElDeg = p.guiViewElDeg;
    _viewZoom = p.guiViewZoom;
    preview.prepare(48000.);
    refreshState();
  }
  ~Editor() override {
    stopRefresh();
    end();
    storeViewState();
  }
  double refreshPeriodMilliseconds() const override { return 50.; }
  void stopRefresh() override {
    Canvas::stopRefresh();
    end();
    spatialDrag = 0;
  }
  void service() override {
    _viewMode = p.guiViewMode;
    _viewAzDeg = p.guiViewAzDeg;
    _viewElDeg = p.guiViewElDeg;
    _viewZoom = p.guiViewZoom;
    refreshState();
  }
  void refreshState() {
    state = controlSnapshot(p);
    preview.setParams(state.params);
    for (unsigned i = 0; i < s3g::kLayoutPannerSources; ++i)
      preview.setSource(i, state.sources[i]);
    if (state.params.layout == s3g::LayoutPannerPreset::Custom &&
        state.customSpeakersExplicit)
      preview.setSpeakers(state.speakers, state.params.activeSpeakers);
    preview.setParams(state.params);
    state.params = preview.params();
    state.speakers = preview.speakers();
    state.sources = preview.sources();
    state.customSpeakersExplicit = true;
  }
  void storeViewState() {
    p.guiViewMode = _viewMode;
    p.guiViewAzDeg = _viewAzDeg;
    p.guiViewElDeg = _viewElDeg;
    p.guiViewZoom = _viewZoom;
  }
  clap_param_info_t info(clap_id id) const {
    clap_param_info_t i{};
    for (unsigned n = 0; n < paramsCount(&p.plugin); ++n)
      if (paramsGetInfo(&p.plugin, n, &i) && i.id == id)
        return i;
    return {};
  }
  double value(clap_id id) const {
    double v = 0.;
    paramsGetValue(&p.plugin, id, &v);
    return v;
  }
  void request() {
    s3g::clap_gui::requestParamEventService(p.host, p.hostParams);
  }
  void dirtyState() {
    if (p.host && p.host->get_extension) {
      auto *ext = static_cast<const clap_host_state_t *>(
          p.host->get_extension(p.host, CLAP_EXT_STATE));
      if (ext && ext->mark_dirty)
        ext->mark_dirty(p.host);
    }
  }
  bool begin(std::initializer_list<clap_id> ids) {
    end();
    if (p.guiParamEvents.available() < ids.size() * 3)
      return false;
    for (auto id : ids) {
      p.guiParamEvents.push({Kind::GestureBegin, id, 0.});
      gestures.push_back(id);
    }
    request();
    return true;
  }
  void end() {
    for (auto id : gestures)
      p.guiParamEvents.push({Kind::GestureEnd, id, 0.});
    if (!gestures.empty())
      request();
    gestures.clear();
  }
  bool change(clap_id id, double v) {
    if (!std::isfinite(v) ||
        std::find(gestures.begin(), gestures.end(), id) == gestures.end() ||
        p.guiParamEvents.available() <= gestures.size())
      return false;
    const auto i = info(id);
    v = std::clamp(v, i.min_value, i.max_value);
    if (i.flags & CLAP_PARAM_IS_STEPPED)
      v = std::round(v);
    applyParam(p, id, v);
    p.guiParamEvents.push({Kind::Value, id, value(id)});
    request();
    refreshState();
    return true;
  }
  bool set(clap_id id, double v) {
    if (!begin({id}))
      return false;
    const bool ok = change(id, v);
    end();
    return ok;
  }
  void editState(const std::function<void(PannerState &)> &edit) {
    refreshState();
    auto next = state;
    edit(next);
    queueControlState(p, next);
    dirtyState();
    refreshState();
    invalid();
  }
  bool presetFile(const std::string &path, bool save) {
    if (save)
      return s3g::clap_gui::portable::saveStateFile(&p.plugin, stateExt,
                                                    path.c_str());
    Plugin candidate;
    candidate.plugin.plugin_data = &candidate;
    candidate.panner.prepare(48000.);
    candidate.panner.setParams(candidate.params);
    candidate.controlState = stateFromPanner(candidate);
    candidate.controlToAudio.initialize(candidate.controlState);
    candidate.audioToControl.initialize(candidate.controlState);
    if (!s3g::clap_gui::portable::loadStateFile(&candidate.plugin, stateExt,
                                                path.c_str()))
      return false;
    auto next = controlSnapshot(candidate);
    next.params.outputGainDb = value(kOutputParamId);
    if (!applyPreset(next))
      return false;
    _viewMode = candidate.guiViewMode;
    _viewAzDeg = candidate.guiViewAzDeg;
    _viewElDeg = candidate.guiViewElDeg;
    _viewZoom = candidate.guiViewZoom;
    storeViewState();
    presetName =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    return true;
  }
  bool applyPreset(PannerState next) {
    end();
    const auto count = paramsCount(&p.plugin);
    if (p.guiParamEvents.available() < count * 3 + gestures.size())
      return false;
    next.params.outputGainDb = value(kOutputParamId);
    queueControlState(p, next);
    std::vector<s3g::clap_gui::ParamEvent> events;
    events.reserve(count * 3);
    for (unsigned n = 0; n < count; ++n) {
      clap_param_info_t i{};
      paramsGetInfo(&p.plugin, n, &i);
      if (i.id == kOutputParamId || i.flags & CLAP_PARAM_IS_READONLY)
        continue;
      events.push_back({Kind::GestureBegin, i.id, 0.});
      events.push_back({Kind::Value, i.id, value(i.id)});
      events.push_back({Kind::GestureEnd, i.id, 0.});
    }
    p.guiParamEvents.pushBatch(events.data(),
                               static_cast<uint32_t>(events.size()));
    dirtyState();
    request();
    refreshState();
    return true;
  }
  void presetDialog(bool save) {
    foundation::FileDialogOptions o;
    o.save = save;
    o.title = std::string(save ? "Save " : "Load ") +
              names[S3G_PANNER_VARIANT] + " Preset";
    o.extension = "s3gpreset";
    o.extensionDescription = "s3g Preset";
    o.defaultSaveName = "Preset.s3gpreset";
    o.initialDirectory = foundation::presetDirectory(names[S3G_PANNER_VARIANT]);
    const auto path = foundation::runFileDialog(getFrame(), o);
    if (!path.empty())
      error = presetFile(path, save) ? "" : "PRESET FILE ERROR";
  }
  bool layoutFile(const std::string &path, bool save) {
    refreshState();
    if (save)
      return writeSpeakerJson(path, state.speakers, state.params.activeSpeakers,
                              state.params.customShape);
    auto next = state;
    if (!readSpeakerJson(path, next.speakers, next.params.activeSpeakers,
                         next.params.customShape))
      return false;
    next.params.layout = s3g::LayoutPannerPreset::Custom;
    next.params.selectedSpeaker =
        std::min(next.params.selectedSpeaker, next.params.activeSpeakers - 1);
    next.customSpeakersExplicit = true;
    queueControlState(p, next);
    dirtyState();
    refreshState();
    _page = 2;
    return true;
  }
  void layoutDialog(bool save) {
    foundation::FileDialogOptions o;
    o.save = save;
    o.title = save ? "Save Speaker Layout" : "Load Speaker Layout";
    o.extension = "json";
    o.extensionDescription = "Speaker Layout JSON";
    o.defaultSaveName = "s3g-layout-speakers.json";
    const auto path = foundation::runFileDialog(getFrame(), o);
    if (!path.empty())
      error = layoutFile(path, save) ? "" : "LAYOUT FILE ERROR";
  }
  static Box pageButtonRect(int index, Box r) {
    return box(r.origin.x + 132. + index * 61., r.origin.y + 4., 56., 13.);
  }
  static Box viewButtonRect(int index, Box r) {
    return box(maxX(r) - 10. - (3. - index) * 38. - (2. - index) * 5.,
               r.origin.y + 4., 38., 13.);
  }
  static Box zoomButtonRect(int index, Box r) {
    return box(viewButtonRect(0, r).origin.x - 12. - (2. - index) * 18. -
                   (1. - index) * 4.,
               r.origin.y + 4., 18., 13.);
  }
  static Box mixerSourceRect(unsigned i, Box r) {
    return box(r.origin.x + 23. + i * 34., r.origin.y + 118., 12.,
               r.size.height - 212.);
  }
  static Box mixerMuteRect(unsigned i, Box r) {
    return box(r.origin.x + 14. + i * 34., maxY(r) - 58., 14., 14.);
  }
  static Box mixerSoloRect(unsigned i, Box r) {
    return box(r.origin.x + 30. + i * 34., maxY(r) - 58., 14., 14.);
  }
  static Box mixerOutputTrackRect(Box r) {
    return box(r.origin.x + 88., r.origin.y + 42., r.size.width - 178., 9.);
  }
  void setViewPreset(int mode) {
    _viewMode = mode;
    _viewAzDeg = mode == 2 ? 35. : 90.;
    _viewElDeg = mode == 0 ? 0. : mode == 1 ? 90. : 34.;
    storeViewState();
    invalid();
  }
  double viewScaleForRect(Box r) const {
    double scale = 1.;
    if (_viewMode != 0 && _viewMode != 1 &&
        (state.params.layout == s3g::LayoutPannerPreset::Cube8 ||
         state.params.layout == s3g::LayoutPannerPreset::Cube17))
      scale = .82;
    return std::min(r.size.width, r.size.height) * .34 * scale *
           std::clamp(_viewZoom, .55, 2.20);
  }
  CPoint projectWorldPoint(s3g::Vec3 v, Box r, double *depth = nullptr) const {
    const float az = float(_viewAzDeg * M_PI / 180.),
                el = float(_viewElDeg * M_PI / 180.);
    const float ca = std::cos(az), sa = std::sin(az), ce = std::cos(el),
                se = std::sin(el);
    const float x1 = ca * v.x - sa * v.y, y1 = sa * v.x + ca * v.y,
                y2 = ce * y1 + se * v.z, z2 = -se * y1 + ce * v.z;
    if (depth)
      *depth = z2;
    return {r.origin.x + r.size.width * .50 + x1 * viewScaleForRect(r),
            r.origin.y + r.size.height * .54 - y2 * viewScaleForRect(r)};
  }
  s3g::Vec3 worldFromPoint(CPoint pt, Box r, s3g::Vec3 previous) const {
    const float sx =
        float((pt.x - r.origin.x - r.size.width * .50) / viewScaleForRect(r));
    const float sy =
        float((r.origin.y + r.size.height * .54 - pt.y) / viewScaleForRect(r));
    double depth = 0.;
    projectWorldPoint(previous, r, &depth);
    const float az = float(_viewAzDeg * M_PI / 180.),
                el = float(_viewElDeg * M_PI / 180.);
    const float ca = std::cos(az), sa = std::sin(az), ce = std::cos(el),
                se = std::sin(el);
    const float y1 = ce * sy - se * float(depth),
                z = se * sy + ce * float(depth);
    return {ca * sx + sa * y1, -sa * sx + ca * y1, z};
  }
  void drawField(Box r) {
    fill(r, calibratedGray(0x11));
    framed(r, style.fill);
    const auto &prm = state.params;
    const auto &speakers = state.speakers;
    const auto n = prm.activeSpeakers;
    std::array<CPoint, s3g::kLayoutPannerMaxSpeakers> points{};
    for (unsigned i = 0; i < n; ++i) {
      const auto &sp = speakers[i];
      auto dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
      points[i] = projectWorldPoint(
          {dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance}, r);
    }
    std::array<std::array<bool, s3g::kLayoutPannerMaxSpeakers>,
               s3g::kLayoutPannerMaxSpeakers>
        seen{};
    const auto edge = [&](unsigned a, unsigned b) {
      if (a >= n || b >= n || a == b)
        return;
      auto lo = std::min(a, b), hi = std::max(a, b);
      if (seen[lo][hi])
        return;
      seen[lo][hi] = true;
      line(points[a], points[b], calibratedGray(0x6e, .68));
    };
    pannerMesh(
        prm, speakers, n, [&](s3g::Vec3 v) { return projectWorldPoint(v, r); },
        edge,
        [&](CPoint a, CPoint b) { line(a, b, calibratedGray(0x6e, .68)); });
#if S3G_PANNER_VARIANT >= 2
    std::array<s3g::LayoutPannerSolverSpeaker,
               s3g::kLayoutPannerMaxSolverSpeakers>
        solver{};
    const auto count = preview.vbapSolverSpeakers(solver);
    const auto topology = preview.vbapTopology(solver, count);
    std::array<CPoint, s3g::kLayoutPannerMaxSolverSpeakers> solverPoints{};
    std::array<std::array<bool, s3g::kLayoutPannerMaxSolverSpeakers>,
               s3g::kLayoutPannerMaxSolverSpeakers>
        solverSeen{};
    for (unsigned i = 0; i < count; ++i)
      solverPoints[i] = solver[i].real && solver[i].realIndex < n
                            ? points[solver[i].realIndex]
                            : projectWorldPoint(solver[i].dir, r);
    const auto solverEdge = [&](unsigned a, unsigned b) {
      if (a >= count || b >= count || a == b)
        return;
      auto lo = std::min(a, b), hi = std::max(a, b);
      if (solverSeen[lo][hi])
        return;
      solverSeen[lo][hi] = true;
      line(solverPoints[a], solverPoints[b], rgba(.404, .851, .875, .13), .55);
    };
    for (unsigned i = 0; i < topology.pairCount; ++i)
      solverEdge(topology.pairs[i].a, topology.pairs[i].b);
    for (unsigned i = 0; i < topology.facetCount; ++i) {
      auto f = topology.facets[i];
      solverEdge(f.a, f.b);
      solverEdge(f.b, f.c);
      solverEdge(f.c, f.a);
    }
    for (unsigned i = 0; i < count; ++i)
      if (!solver[i].real)
        fill(rect(solverPoints[i].x - 2., solverPoints[i].y - 2., 4., 4.),
             rgba(.404, .851, .875, .18));
#endif
    auto idFont = foundation::makeUiFont(7.);
    for (unsigned i = 0; i < n; ++i) {
      const auto pt = points[i];
      const bool selected = _page == 2 && i == prm.selectedSpeaker;
      const auto marker = box(pt.x - 4.2, pt.y - 4.2, 8.4, 8.4);
      fill(marker, calibratedGray(0x8a, .82));
      framed(marker, calibratedGray(0x8a, .82));
      if (selected)
        framed(box(pt.x - 8., pt.y - 8., 16., 16.), calibratedGray(0x8a, .82));
      text(format("%u", i + 1), rect(pt.x - 15., pt.y - 5., 30., 9.),
           calibratedGray(selected ? 0xc8 : 0x15), kCenterText, idFont);
    }
    idFont = foundation::makeUiFont(7.5);
    if (_page != 2)
      for (unsigned i = 0; i < prm.activeSources; ++i) {
        const auto &source = state.sources[i];
        if (source.muted)
          continue;
        const auto effective = s3g::layoutPannerEffectiveSource(source, prm);
        const auto pt =
            projectWorldPoint(s3g::layoutPannerSourcePosition(effective), r);
        const bool selected = i == prm.selectedSource;
        const double radius = selected ? 8.5 : 7.;
        const auto ink =
            pannerAedColor(effective.azimuthDeg, effective.elevationDeg,
                           effective.distance, selected);
        const auto marker =
            box(pt.x - radius, pt.y - radius, radius * 2., radius * 2.);
        fill(marker, ink);
        framed(marker, ink);
        if (selected)
          framed(box(pt.x - 12., pt.y - 12., 24., 24.), ink);
        text(format("S%u", i + 1), rect(pt.x - 15., pt.y - 5., 30., 9.),
             calibratedGray(selected ? 0xc8 : 0x15), kCenterText, idFont);
      }
    const char *hint =
        _viewMode == 0 ? "TOP VIEW   DRAG SOURCES / BLANK DRAG ROTATES"
                       : _viewMode == 1 ? "SIDE VIEW   SOURCE DRAG EDITS HEIGHT"
                                        : "3/4 VIEW   CLICK SOURCE TO SELECT";
    if (_page == 2)
      hint = _viewMode == 0
                 ? "TOP VIEW   DRAG SPEAKERS / BLANK DRAG ROTATES"
                 : _viewMode == 1 ? "SIDE VIEW   SPEAKER DRAG EDITS HEIGHT"
                                  : "3/4 VIEW   CLICK SPEAKER TO SELECT";
    at(hint, r.origin.x + 12., maxY(r) - 23., style.value);
  }
  void paramSlider(clap_id id, const char *label, double y, Box track = {}) {
    const auto i = info(id);
    double v = value(id), lo = i.min_value, hi = i.max_value;
    if (id == kSelectedSourceParamId)
      hi = state.params.activeSources;
    const bool az =
        id == kGlobalAzimuthParamId || id == kSelectedAzimuthParamId;
    double norm = az ? s3g::aedAzimuthSliderNorm(float(v))
                     : hi > lo ? (v - lo) / (hi - lo) : 0.;
    const bool mixer = track.size.width > 0.;
    if (!mixer)
      track = box(738., y + 1., family.sliderTrackWidth, 9.);
    const auto labelBox =
        mixer ? box(track.origin.x - 70., track.origin.y - 3., 64., 15.)
              : box(646., y - 2., 86., 15.);
    const auto valueBox =
        mixer ? box(maxX(track) + 10., track.origin.y - 3., 90., 15.)
              : box(826., y - 2., 36., 15.);
    std::string readout;
    if (az || id == kSelectedElevationParamId || id == kGlobalElevationParamId)
      readout = format("%+.0f", v);
    else if (id == kOutputParamId || id == kSelectedGainParamId)
      readout = format("%+.1f dB", v);
    else if (id == kSmoothingParamId || id == kActiveSourcesParamId ||
             id == kSelectedSourceParamId)
      readout = format(id == kSelectedSourceParamId ? "S%.0f" : "%.0f", v);
    else
      readout = format(id == kRolloffParamId ? "%.1f" : "%.2f", v);
    slider(
        label, readout, norm, track,
        mixer ? inset(track, -8., -8.) : box(638., y - 8., 234., 24.), labelBox,
        valueBox,
        [this, id, az, lo, hi](double n) {
          change(id, az ? s3g::aedAzimuthFromSliderNorm(float(n))
                        : lo + n * (hi - lo));
        },
        [this, id] { begin({id}); }, [this] { end(); },
        [this, id, i] { set(id, i.default_value); });
  }
  void drawMixer(Box r) {
    const auto &prm = state.params;
    const auto pages = std::max(1u, (prm.activeSources + 15) / 16);
    _mixerPage = std::min(_mixerPage, pages - 1);
    const unsigned start = _mixerPage * 16,
                   count = std::min(16u, prm.activeSources - start);
    if (pages > 1) {
      header(
          box(r.origin.x + 12., r.origin.y + 12., 24., 18.), "<",
          [this] {
            if (_mixerPage)
              --_mixerPage;
          },
          _mixerPage > 0);
      header(
          box(r.origin.x + 40., r.origin.y + 12., 24., 18.), ">",
          [this, pages] {
            if (_mixerPage + 1 < pages)
              ++_mixerPage;
          },
          _mixerPage + 1 < pages);
      at(format("PAGE %u/%u   S%u-%u", _mixerPage + 1, pages, start + 1,
                start + count),
         r.origin.x + 78., r.origin.y + 15., style.value);
    }
    paramSlider(kOutputParamId, "OUT", r.origin.y + 36.,
                mixerOutputTrackRect(r));
    bool anySolo = false;
    for (unsigned i = 0; i < prm.activeSources; ++i)
      anySolo |= state.sources[i].solo;
    for (unsigned lane = 0; lane < count; ++lane) {
      const unsigned i = start + lane;
      const auto &s = state.sources[i];
      const bool selected = i == prm.selectedSource,
                 audible = !s.muted && (!anySolo || s.solo);
      const double x = r.origin.x + 13. + lane * 34.;
      if (selected) {
        const auto b = box(x, r.origin.y + 92., 32., r.size.height - 108.);
        fill(b, calibratedGray(0x24));
        framed(b, calibratedGray(0x24));
      }
      text(format("S%u", i + 1), rect(x - 1., r.origin.y + 96., 34., 15.),
           style.value, kCenterText);
      const auto slot = mixerSourceRect(lane, r);
      fill(slot, calibratedGray(audible ? 0x18 : 0x0d));
      framed(slot, calibratedGray(audible ? 0x18 : 0x0d));
      const double norm = std::clamp((s.gainDb + 60.) / 84., 0., 1.);
      auto filling = inset(slot, 2., 2.);
      filling.origin.y += filling.size.height * (1. - norm);
      filling.size.height = std::max(1., filling.size.height * norm);
      const auto eff = s3g::layoutPannerEffectiveSource(s, prm);
      fill(filling, pannerAedColor(eff.azimuthDeg, eff.elevationDeg,
                                   eff.distance, selected));
      fill(rect(slot.origin.x - 2.,
                slot.origin.y + slot.size.height * (1. - norm) - 1.,
                slot.size.width + 4., 3.),
           calibratedGray(selected ? 0xf2 : 0x9a));
      const auto id = sourceParamId(i, kSourceGainOffset);
      drag(
          inset(slot, -8., -7.),
          [this, id, slot](CPoint pt) {
            change(id, -60. + std::clamp((maxY(slot) - pt.y) / slot.size.height,
                                         0., 1.) *
                                  84.);
          },
          [this, id] { begin({id}); }, [this] { end(); },
          [this, id] { set(id, info(id).default_value); });
      for (bool solo : {false, true}) {
        const auto b = solo ? mixerSoloRect(lane, r) : mixerMuteRect(lane, r);
        const bool on = solo ? s.solo : s.muted;
        const auto ink =
            calibratedGray(solo ? (on ? 0xd1 : 0x15) : (on ? 0x3a : 0x15));
        fill(b, ink);
        framed(b, ink);
        text(solo ? "S" : "M", b,
             solo && on ? calibratedGray(0x11) : style.value, kCenterText);
        hit(inset(b, -4., -4.), [this, i, solo, on] {
          set(sourceParamId(i, solo ? kSourceSoloOffset : kSourceMuteOffset),
              on ? 0. : 1.);
        });
      }
      text(format("%+.0f", s.gainDb), rect(x - 1., maxY(slot) + 8., 34., 15.),
           style.value, kCenterText);
    }
  }
  void menuRow(clap_id id, const char *label, double y,
               std::vector<std::string> items, int selected,
               std::function<void(int)> apply, double width = 102.,
               const std::string &shown = "") {
    const auto b = box(738., y - 1., width, 15.);
    at(label, 646., y - 2., style.label);
    foundation::drawMenuBox(
        *context, b,
        shown.empty()
            ? items[size_t(std::clamp(selected, 0, int(items.size()) - 1))]
            : shown,
        font, style);
    hit(b, [this, b, items = std::move(items), selected,
            apply = std::move(apply)] {
      const double h = items.size() * 18.;
      openPopup(b, items, selected, apply, 1,
                rect(b.origin.x, std::clamp(maxY(b) + 2., 28., 676. - h),
                     b.size.width, h),
                18.);
    });
    (void)id;
  }
  void layoutMenu() {
    std::vector<std::string> items;
    for (unsigned i = 0; i < kLayoutCount; ++i)
      items.push_back(upper(s3g::layoutPannerPresetName(
          static_cast<s3g::LayoutPannerPreset>(layoutPresetForMenuIndex(i)))));
    menuRow(
        kLayoutParamId, "LAYOUT", 144., items,
        int(menuIndexForLayoutPreset(unsigned(state.params.layout))),
        [this](int i) { set(kLayoutParamId, layoutPresetForMenuIndex(i)); },
        family.menuWidth,
        upper(s3g::layoutPannerPresetName(state.params.layout)));
  }
  void designSlider(int id, const char *label, double y, double v,
                    double norm) {
    const auto track = box(738., y + 1., family.sliderTrackWidth, 9.);
    slider(
        label,
        format(id == 27 ? "%.2f" : id == 25 || id == 26 ? "%+.0f" : "%.0f", v),
        norm, track, box(638., y - 8., 234., 24.), box(646., y - 2., 86., 15.),
        box(826., y - 2., 36., 15.),
        [this, id](double n) {
          editState([&](PannerState &s) {
            auto &prm = s.params;
            prm.layout = s3g::LayoutPannerPreset::Custom;
            if (id == 21) {
              prm.activeSpeakers = unsigned(std::lround(2. + n * 62.));
              prm.selectedSpeaker =
                  std::min(prm.selectedSpeaker, prm.activeSpeakers - 1);
              s.customSpeakersExplicit = false;
            } else if (id == 24)
              prm.selectedSpeaker =
                  unsigned(std::lround(n * (prm.activeSpeakers - 1)));
            else {
              auto &sp = s.speakers[prm.selectedSpeaker];
              if (id == 25)
                sp.azimuthDeg = s3g::aedAzimuthFromSliderNorm(float(n));
              else if (id == 26)
                sp.elevationDeg = float(-90. + n * 180.);
              else
                sp.distance = float(.1 + n * 2.9);
            }
          });
        },
        {}, {},
        [this, id] {
          editState([&](PannerState &s) {
            s.params.layout = s3g::LayoutPannerPreset::Custom;
            if (id == 21) {
              s.params.activeSpeakers =
                  s3g::LayoutPannerParams{}.activeSpeakers;
              s.customSpeakersExplicit = false;
              s.params.selectedSpeaker = std::min(s.params.selectedSpeaker,
                                                  s.params.activeSpeakers - 1);
            } else if (id == 24)
              s.params.selectedSpeaker = 0;
            else {
              auto &sp = s.speakers[s.params.selectedSpeaker];
              if (id == 25)
                sp.azimuthDeg = 0;
              else if (id == 26)
                sp.elevationDeg = 0;
              else
                sp.distance = 1;
            }
          });
        });
  }
  void paint() override {
    refreshState();
    const auto &prm = state.params;
    const auto band = layout::encoderTitleBand({kGuiWidth, kGuiHeight});
    foundation::drawPluginTitle(*context,
                                std::string("s3g ") + names[S3G_PANNER_VARIANT],
                                rect(band.titleX, 13., 200., 15.), titleFont);
    at("PRESET", band.presetLabelX, 13., style.label);
    foundation::drawMenuBox(*context, box(band.presetMenu), upper(presetName),
                            font, style);
    hit(box(band.presetMenu), [this] {
      s3g::LayoutPanner defaults;
      defaults.prepare(48000.);
      auto next = state;
      next.params = s3g::LayoutPannerParams{};
#if S3G_PANNER_VARIANT != 0
      next.params.method = kFixedMethod;
#endif
      defaults.setParams(next.params);
      next.params = defaults.params();
      next.sources = defaults.sources();
      next.speakers = defaults.speakers();
      next.customSpeakersExplicit = true;
      if (applyPreset(next))
        presetName = "INIT";
    });
    header(box(band.loadButton), "LOAD", [this] { presetDialog(false); });
    header(box(band.saveButton), "SAVE", [this] { presetDialog(true); });
    text(error.empty()
             ? format("PK %+.1f",
                      20. * std::log10(std::max(.000001f, p.outputPeak.load())))
             : error,
         rect(maxX(box(band.saveButton)) + 8., 13.,
              kGuiWidth - maxX(box(band.saveButton)) - 24., 15.),
         style.value, kRightText);
    const auto main = box(family.mainPanel);
    panel(main, _page == 0 ? "LAYOUT FIELD"
                           : _page == 1 ? "SOURCE MIXER" : "LAYOUT DESIGN");
    const char *pages[] = {"FIELD", "MIXER", "DESIGN"};
    for (int i = 0; i < 3; ++i)
      header(
          pageButtonRect(i, main), pages[i],
          [this, i] {
            end();
            _page = i;
          },
          _page == i);
    if (_page != 1) {
      for (int i = 0; i < 2; ++i)
        header(zoomButtonRect(i, main), i ? "+" : "-", [this, i] {
          _viewZoom = std::clamp(_viewZoom + (i ? .15 : -.15), .55, 2.20);
          storeViewState();
        });
      const char *views[] = {"TOP", "SIDE", "3/4"};
      for (int i = 0; i < 3; ++i)
        header(
            viewButtonRect(i, main), views[i], [this, i] { setViewPreset(i); },
            _viewMode == i);
      drawField(box(family.field));
    } else
      drawMixer(box(family.field));
    panel(box(family.output.frame), "OUTPUT");
    paramSlider(kOutputParamId, "OUT", family.outputRowY);
    if (_page == 2) {
      panel(box(family.designPanner.frame), "PANNER");
      panel(box(family.speaker.frame), "SPEAKER");
      layoutMenu();
      std::vector<std::string> items;
      int selected = 0;
      for (int i = 0; i < 5; ++i) {
        items.push_back(upper(s3g::layoutPannerCustomShapeName(shapes[i])));
        if (shapes[i] == prm.customShape)
          selected = i;
      }
      menuRow(
          CLAP_INVALID_ID, "SHAPE", 170., items, selected,
          [this](int i) {
            editState([&](PannerState &s) {
              s.params.layout = s3g::LayoutPannerPreset::Custom;
              s.params.customShape = shapes[i];
              s.customSpeakersExplicit = false;
            });
          },
          92.);
      designSlider(21, "COUNT", 196., prm.activeSpeakers,
                   (prm.activeSpeakers - 2.) / 62.);
      header(box(646., 222., 64., 18.), "COPY", [this] {
        editState([](PannerState &s) {
          s.params.layout = s3g::LayoutPannerPreset::Custom;
        });
      });
      header(box(718., 222., 64., 18.), "LOAD",
             [this] { layoutDialog(false); });
      header(box(790., 222., 64., 18.), "SAVE", [this] { layoutDialog(true); });
      const auto &sp = state.speakers[prm.selectedSpeaker];
      designSlider(24, "SEL", 306., prm.selectedSpeaker + 1,
                   prm.activeSpeakers > 1
                       ? double(prm.selectedSpeaker) / (prm.activeSpeakers - 1)
                       : 0.);
      designSlider(25, "AZ", 332., sp.azimuthDeg,
                   s3g::aedAzimuthSliderNorm(sp.azimuthDeg));
      designSlider(26, "EL", 358., sp.elevationDeg,
                   (sp.elevationDeg + 90.) / 180.);
      designSlider(27, "DST", 384., sp.distance, (sp.distance - .1) / 2.9);
    } else {
      panel(box(family.panner.frame), "PANNER");
      panel(box(family.source.frame), "SOURCE");
      layoutMenu();
#if S3G_PANNER_VARIANT == 0
      menuRow(
          kMethodParamId, "METHOD", 170., {"DIST", "COS"}, int(prm.method),
          [this](int i) { set(kMethodParamId, i); }, 78.);
#else
      at("METHOD", 646., 170., style.label);
      at(upper(s3g::layoutPannerMethodName(prm.method)), 738., 170.,
         style.value);
#endif
      const clap_id ids[] = {kFocusParamId,           kRolloffParamId,
                             kSmoothingParamId,       kGlobalAzimuthParamId,
                             kGlobalElevationParamId, kGlobalDistanceParamId,
                             kDiffusionParamId};
      const char *labels[] = {"FOC", "ROLL", "SMTH", "GAZ",
                              "GEL", "GDST", "DIF"};
      for (unsigned i = 0; i < 7; ++i)
        paramSlider(ids[i], labels[i], 196. + 26. * i);
      menuRow(kInsideModeParamId, "IN", 378.,
              {"HOLD", "ATTENUATE", "DIFFUSE", "CENTER"}, int(prm.insideMode),
              [this](int i) { set(kInsideModeParamId, i); });
      paramSlider(kActiveSourcesParamId, "SRC", 404.);
      const clap_id sourceIds[] = {
          kSelectedSourceParamId, kSelectedAzimuthParamId,
          kSelectedElevationParamId, kSelectedDistanceParamId,
          kSelectedGainParamId};
      const char *sourceLabels[] = {"SEL", "AZ", "EL", "DST", "GAIN"};
      for (unsigned i = 0; i < 5; ++i)
        paramSlider(sourceIds[i], sourceLabels[i], 470. + 26. * i);
    }
  }
  bool pointerDown(MouseDownEvent &event) override {
    const auto pt = event.mousePosition;
    const auto field = box(family.field);
    if (!event.buttonState.isLeft() || _page == 1 ||
        !foundation::contains(field, pt))
      return false;
    refreshState();
    double best = 999999.;
    unsigned chosen =
        _page == 2 ? state.params.selectedSpeaker : state.params.selectedSource;
    const auto count =
        _page == 2 ? state.params.activeSpeakers : state.params.activeSources;
    for (unsigned i = 0; i < count; ++i) {
      s3g::Vec3 world;
      if (_page == 2) {
        const auto &s = state.speakers[i];
        world = s3g::directionFromAed(s.azimuthDeg, s.elevationDeg);
        world.x *= s.distance;
        world.y *= s.distance;
        world.z *= s.distance;
      } else
        world = s3g::layoutPannerEffectiveSourcePosition(state.sources[i],
                                                         state.params);
      const auto pos = projectWorldPoint(world, field);
      double d = std::hypot(pt.x - pos.x, pt.y - pos.y);
      if (d < best) {
        best = d;
        chosen = i;
      }
    }
    if (best < (_page == 2 ? 18. : 20.)) {
      if (_page == 2) {
        editState([&](PannerState &s) { s.params.selectedSpeaker = chosen; });
        spatialDrag = 2;
      } else {
        set(kSelectedSourceParamId, chosen + 1);
        dragSource = chosen;
        spatialDrag = begin({sourceParamId(chosen, kSourceAzOffset),
                             sourceParamId(chosen, kSourceElOffset),
                             sourceParamId(chosen, kSourceDistanceOffset)})
                          ? 1
                          : 0;
      }
    } else {
      spatialDrag = 3;
      lastPoint = pt;
    }
    return true;
  }
  void pointerMove(MouseMoveEvent &event) override {
    if (!spatialDrag)
      return;
    const auto pt = event.mousePosition;
    const auto field = box(family.field);
    refreshState();
    if (spatialDrag == 3) {
      _viewAzDeg += (pt.x - lastPoint.x) * .35;
      _viewElDeg =
          std::clamp(_viewElDeg + (pt.y - lastPoint.y) * .35, -85., 85.);
      _viewMode = -1;
      lastPoint = pt;
      storeViewState();
    } else if (spatialDrag == 1) {
      auto source = state.sources[dragSource];
      auto world = worldFromPoint(
          pt, field,
          s3g::layoutPannerEffectiveSourcePosition(source, state.params));
      auto raw = s3g::layoutPannerRawSourcePositionFromEffectivePosition(
          world, state.params);
      source.x = raw.x;
      source.y = raw.y;
      source.z = raw.z;
      s3g::layoutPannerSyncSourceAedFromPosition(source);
      if (p.guiParamEvents.available() >= gestures.size() + 3) {
        change(sourceParamId(dragSource, kSourceAzOffset), source.azimuthDeg);
        change(sourceParamId(dragSource, kSourceElOffset), source.elevationDeg);
        change(sourceParamId(dragSource, kSourceDistanceOffset),
               source.distance);
      }
    } else
      editState([&](PannerState &s) {
        auto &sp = s.speakers[s.params.selectedSpeaker];
        auto dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        auto world = worldFromPoint(
            pt, field,
            {dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance});
        const float d = std::sqrt(world.x * world.x + world.y * world.y +
                                  world.z * world.z);
        sp.azimuthDeg = s3g::layoutPannerWrapDeg(std::atan2(world.y, world.x) *
                                                 180.f / s3g::kPi);
        sp.elevationDeg =
            std::asin(std::clamp(world.z / std::max(.000001f, d), -1.f, 1.f)) *
            180.f / s3g::kPi;
        sp.distance = std::clamp(d, .1f, 3.f);
        s.params.layout = s3g::LayoutPannerPreset::Custom;
      });
    invalid();
  }
  void pointerUp(MouseUpEvent &) override {
    spatialDrag = 0;
    end();
  }
  Plugin &p;
  PannerState state;
  s3g::LayoutPanner preview;
  int _page = 0, _viewMode = 2;
  double _viewAzDeg = 35., _viewElDeg = 34., _viewZoom = 1.;
  unsigned _mixerPage = 0;
  std::string presetName = "CURRENT", error;
  std::vector<clap_id> gestures;
  int spatialDrag = 0;
  unsigned dragSource = 0;
  CPoint lastPoint;
  using Kind = s3g::clap_gui::ParamEventKind;
};
} // namespace panner_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new panner_canvas::Editor(p);
}
