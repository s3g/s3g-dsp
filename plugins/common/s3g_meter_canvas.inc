// Analyzer Meter: original GRID/FIELD/HEAT geometry and history, not a
// replacement scope.
namespace meter_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
static uint32_t autoLayoutForWidth(uint32_t count) {
  if (count <= 4u)
    return static_cast<uint32_t>(MeterLayout::Quad);
  if (count == 6u)
    return static_cast<uint32_t>(MeterLayout::QuadOh);
  if (count == 8u)
    return static_cast<uint32_t>(MeterLayout::Cube8);
  if (count == 12u)
    return static_cast<uint32_t>(MeterLayout::Dodeca12);
  if (count == 16u)
    return static_cast<uint32_t>(MeterLayout::Ring16);
  if (count == 20u)
    return static_cast<uint32_t>(MeterLayout::DoubleRing20);
  if (count == 17u)
    return static_cast<uint32_t>(MeterLayout::Cube17);
  if (count == 25u)
    return static_cast<uint32_t>(MeterLayout::Dome25);
  return static_cast<uint32_t>(MeterLayout::Dome24);
}

struct MeterFieldLayout {
  uint32_t count = 0;
  std::array<s3g::Vec3, kChannelCount> points{};
};

static s3g::Vec3 speakerVec(const s3g::LayoutPannerSpeaker &speaker) {
  const auto dir =
      s3g::directionFromAed(speaker.azimuthDeg, speaker.elevationDeg);
  return {dir.x * speaker.distance, dir.y * speaker.distance,
          dir.z * speaker.distance};
}

static MeterFieldLayout makeFieldLayout(uint32_t requestedLayout,
                                        uint32_t visible) {
  MeterFieldLayout out{};
  const uint32_t layout =
      requestedLayout == 0u ? autoLayoutForWidth(visible) : requestedLayout;
  if (layout == static_cast<uint32_t>(MeterLayout::Sphere24)) {
    out.count = s3g::kAmbisonicSphere24PointCount;
    for (uint32_t i = 0; i < out.count; ++i)
      out.points[i] = s3g::kAmbisonicSphere24Points[i];
    return out;
  }
  s3g::LayoutPanner panner;
  panner.prepare(48000.0);
  s3g::LayoutPannerParams params{};
  switch (static_cast<MeterLayout>(layout)) {
  case MeterLayout::Quad:
    params.layout = s3g::LayoutPannerPreset::Quad;
    break;
  case MeterLayout::QuadOh:
    params.layout = s3g::LayoutPannerPreset::QuadOverhead6;
    break;
  case MeterLayout::Cube8:
    params.layout = s3g::LayoutPannerPreset::Cube8;
    break;
  case MeterLayout::Cube17:
    params.layout = s3g::LayoutPannerPreset::Cube17;
    break;
  case MeterLayout::Dome25:
    params.layout = s3g::LayoutPannerPreset::Dome25;
    break;
  case MeterLayout::Dodeca12:
    params.layout = s3g::LayoutPannerPreset::Dodeca12;
    break;
  case MeterLayout::OctoRing:
    params.layout = s3g::LayoutPannerPreset::OctophonicRing;
    break;
  case MeterLayout::Ring12:
    params.layout = s3g::LayoutPannerPreset::Ring12;
    break;
  case MeterLayout::Ring16:
    params.layout = s3g::LayoutPannerPreset::Ring16;
    break;
  case MeterLayout::DoubleRing16:
    params.layout = s3g::LayoutPannerPreset::DoubleRing16;
    break;
  case MeterLayout::DoubleRing20:
    params.layout = s3g::LayoutPannerPreset::DoubleRing20;
    break;
  case MeterLayout::Dome24:
  default:
    params.layout = s3g::LayoutPannerPreset::Dome24NoOverhead;
    break;
  }
  panner.setParams(params);
  out.count = std::min<uint32_t>(panner.activeSpeakers(), kChannelCount);
  const auto &speakers = panner.speakers();
  for (uint32_t i = 0; i < out.count; ++i)
    out.points[i] = speakerVec(speakers[i]);
  return out;
}

class Editor final : public View {
public:
  explicit Editor(Plugin &plugin) : View(kGuiWidth, kGuiHeight), p(plugin) {}
  ~Editor() override { stopRefresh(); }
  bool hasResponsiveLayout() const override { return true; }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  void service() override { advanceHeatHistory(); }
  static CRect box(layout::Rect b) { return rect(b.x, b.y, b.width, b.height); }
  static CColor gray(unsigned rgb, double alpha = 1.) {
    auto c = color(s3g::portable_gui::topology_colors::gray[rgb & 255]);
    c.alpha = uint8_t(std::clamp(alpha, 0., 1.) * 255.);
    return c;
  }
  static double normDb(float value) {
    const float db = 20.f * std::log10(std::max(.000001f, value));
    return std::clamp((double(db) + 60.) / 60., 0., 1.);
  }
  static CColor heatColor(double value, double alpha) {
    auto c = color(s3g::portable_gui::topology_colors::heat[std::lround(
        std::clamp(value, 0., 1.) * 1024.)]);
    c.alpha = uint8_t(alpha * 255.);
    return c;
  }
  static CColor spatialColor(double peak, double rms) {
    // Original calibrated colors converted by
    // generate-monitoring-color-reference.swift.
    auto c = peak > .965 ? color(0xff4325)
                         : peak > .86 ? color(0xff931d)
                                      : rms > .66 ? color(0xedd75b)
                                                  : rms > .36 ? color(0x74db8b)
                                                              : color(0x378f5c);
    c.alpha = uint8_t(
        255. *
        (peak > .965
             ? .96
             : peak > .86 ? .94 : rms > .66 ? .92 : rms > .36 ? .90 : .82));
    return c;
  }
  static const char *viewName(unsigned mode) {
    static const char *names[] = {"GRID", "FIELD", "HEAT"};
    return names[std::min(mode, 2u)];
  }
  static const char *layoutName(unsigned selected) {
    static const char *names[] = {
        "AUTO",    "CUBE 8",      "CUBE 17",     "DODECA 12", "DOME 24",
        "DOME 25", "DBL RING 16", "DBL RING 20", "OCTO RING", "QUAD",
        "QUAD+OH", "RING 12",     "RING 16",     "SPHERE 24"};
    return names[std::min(selected, kMeterLayoutCount - 1)];
  }
  double value(clap_id id) const {
    double v = 0.;
    paramsGetValue(&p.plugin, id, &v);
    return v;
  }
  clap_param_info_t info(clap_id id) const {
    clap_param_info_t result{};
    if (id >= 1 && id <= 3)
      paramsGetInfo(&p.plugin, id - 1, &result);
    return result;
  }
  void requestService() {
    s3g::clap_gui::requestParamEventService(p.host, p.monitorHostParams);
  }
  std::array<double, 3> values() const {
    return {value(1), value(2), value(3)};
  }
  void publish(const std::array<double, 3> &next) {
    p.visibleChannels.store(uint32_t(next[0]), std::memory_order_relaxed);
    p.viewMode.store(uint32_t(next[1]), std::memory_order_relaxed);
    p.layout.store(uint32_t(next[2]), std::memory_order_relaxed);
  }
  bool applyValues(const std::array<double, 3> &next) {
    if (p.monitorGuiEvents.available() < 9u + (active != CLAP_INVALID_ID))
      return false;
    std::array<s3g::clap_gui::ParamEvent, 9> events{};
    for (clap_id id = 1; id <= 3; ++id) {
      const auto param = info(id);
      if (!std::isfinite(next[id - 1]) || next[id - 1] < param.min_value ||
          next[id - 1] > param.max_value)
        return false;
      events[(id - 1) * 3] = {Kind::GestureBegin, id, 0.};
      events[(id - 1) * 3 + 1] = {Kind::Value, id, next[id - 1]};
      events[(id - 1) * 3 + 2] = {Kind::GestureEnd, id, 0.};
    }
    publish(next);
    if (!p.monitorGuiEvents.pushBatch(events.data(), events.size()))
      return false;
    requestService();
    return true;
  }
  bool set(clap_id id, double next) {
    if (id < 1 || id > 3 || !std::isfinite(next) || active != CLAP_INVALID_ID)
      return false;
    auto state = values();
    const auto param = info(id);
    state[id - 1] =
        std::round(std::clamp(next, param.min_value, param.max_value));
    // Preserve the original coupled WIDTH update on FIELD / LAYOUT changes.
    if (id != 1 && state[1] == 1.) {
      const auto count = meterLayoutSpeakerCount(uint32_t(state[2]));
      if (count)
        state[0] = count;
    }
    return applyValues(state);
  }
  bool applySnapshot(Plugin *snapshot) {
    std::array<double, 3> state{};
    for (clap_id id = 1; id <= 3; ++id) {
      state[id - 1] = info(id).default_value;
      if (snapshot)
        paramsGetValue(&snapshot->plugin, id, &state[id - 1]);
    }
    return applyValues(state);
  }
  void beginWidth() {
    if (active != CLAP_INVALID_ID || p.monitorGuiEvents.available() < 3)
      return;
    p.monitorGuiEvents.push({Kind::GestureBegin, 1, 0.});
    active = 1;
    requestService();
  }
  void setWidth(double next) {
    if (active != 1 || p.monitorGuiEvents.available() < 2)
      return;
    next = std::round(std::clamp(next, 1., 64.));
    applyVisibleChannels(p, next);
    p.monitorGuiEvents.push({Kind::Value, 1, next});
    requestService();
  }
  void endWidth() {
    if (active != 1)
      return;
    p.monitorGuiEvents.push({Kind::GestureEnd, 1, 0.});
    active = CLAP_INVALID_ID;
    requestService();
  }
  bool presetFile(const std::string &filename, bool save) {
    bool result;
    if (save)
      result = s3g::clap_gui::portable::saveStateFile(&p.plugin, stateExt,
                                                      filename.c_str());
    else {
      auto snapshot = std::make_unique<Plugin>();
      snapshot->plugin.plugin_data = snapshot.get();
      result = s3g::clap_gui::portable::loadStateFile(
                   &snapshot->plugin, stateExt, filename.c_str()) &&
               applySnapshot(snapshot.get());
    }
    if (result)
      presetName = foundation::pathToUtf8(
          foundation::pathFromUtf8(filename.c_str()).stem());
    return result;
  }
  void choosePreset(bool save) {
    foundation::FileDialogOptions options;
    options.save = save;
    options.title =
        save ? "Save Analyzer Meter Preset" : "Load Analyzer Meter Preset";
    options.extension = "s3gpreset";
    options.extensionDescription = "s3g Preset";
    options.defaultSaveName = "Preset.s3gpreset";
    options.initialDirectory = foundation::presetDirectory("Analyzer Meter");
    const auto filename = foundation::runFileDialog(getFrame(), options);
    if (!filename.empty())
      fileError = presetFile(filename, save) ? "" : "PRESET FILE ERROR";
  }
  void fitted(const std::string &label, CRect bounds, CColor ink,
              CHoriTxtAlign alignment = kLeftText, CFontRef face = nullptr) {
    if (!face)
      face = font;
    auto fittedFont = makeOwned<CFontDesc>(*face);
    context->setFont(face);
    const double width = context->getStringWidth(label.c_str());
    if (width > bounds.getWidth() && width > 0.)
      fittedFont->setSize(face->getSize() * bounds.getWidth() / width);
    text(label, bounds, ink, alignment, fittedFont);
  }
  void at(const std::string &label, double x, double y) {
    text(label, rect(x, y, 600., 15.), style.value);
  }
  void insideFrame(CRect b, CColor ink) {
    fill(rect(b.left, b.top, b.getWidth(), 1.), ink);
    fill(rect(b.left, b.bottom - 1., b.getWidth(), 1.), ink);
    fill(rect(b.left, b.top, 1., b.getHeight()), ink);
    fill(rect(b.right - 1., b.top, 1., b.getHeight()), ink);
  }
  void title(double width, double height, unsigned activeChannels,
             unsigned visible) {
    const auto band = layout::analyzerTitleBand({width, height});
    fitted("s3g ANALYZER METER 64CH",
           rect(band.titleX, 13., band.presetLabelX - band.titleX - 10., 15.),
           style.text, kLeftText, titleFont);
    text("PRESET", rect(band.presetLabelX, 13., 60., 15.), style.label);
    foundation::drawMenuBox(*context, box(band.presetMenu), upper(presetName),
                            font, style);
    hit(box(band.presetMenu), [this] {
      if (applySnapshot(nullptr)) {
        presetName = "INIT";
        fileError.clear();
      }
    });
    for (bool save : {false, true}) {
      const auto bounds = box(save ? band.saveButton : band.loadButton);
      foundation::drawButton(*context, bounds, save ? "SAVE" : "LOAD", font,
                             false, color(0x2b2b2b));
      hit(bounds, [this, save] { choosePreset(save); });
    }
    const double x = band.saveButton.x + band.saveButton.width + 10.;
    fitted(fileError.empty() ? format("%u/%uCH", activeChannels, visible)
                             : fileError,
           rect(x, 13., width - x - band.statusRightInset, 15.), style.value,
           kRightText);
  }
  CRect viewButton(unsigned i, CRect bar) const {
    return rect(bar.right - 12. - (3. - i) * 38. - (2. - i) * 5., bar.top + 9.,
                38., 14.);
  }
  CRect zoomButton(unsigned i, CRect bar) const {
    return rect(viewButton(0, bar).left - 12. - (2. - i) * 18. - (1. - i) * 4.,
                bar.top + 9., 18., 14.);
  }
  void setViewPreset(unsigned mode) {
    cameraMode = mode;
    azimuth = mode < 2 ? 90. : 35.;
    elevation = mode == 0 ? 0. : mode == 1 ? 90. : 34.;
  }
  CPoint project(s3g::Vec3 point, CRect bounds) const {
    const double scale =
        std::min(bounds.getWidth(), bounds.getHeight()) * .34 * zoom;
    const double cx = bounds.left + bounds.getWidth() * .5,
                 cy = bounds.top + bounds.getHeight() * .54;
    if (cameraMode == 0)
      return {cx - point.y * scale, cy - point.x * scale};
    if (cameraMode == 1)
      return {cx - point.y * scale, cy - point.z * scale};
    const double yaw = azimuth * M_PI / 180., pitch = elevation * M_PI / 180.;
    const double x1 = point.x * std::cos(yaw) - point.y * std::sin(yaw);
    const double y1 = point.x * std::sin(yaw) + point.y * std::cos(yaw);
    return {cx + x1 * scale,
            cy - (y1 * std::cos(pitch) + point.z * std::sin(pitch)) * scale};
  }
  void advanceHeatHistory() {
    for (unsigned ch = 0; ch < 64; ++ch)
      history[ch][historyWrite] =
          normDb(std::max(p.rms[ch].load(std::memory_order_relaxed),
                          p.peak[ch].load(std::memory_order_relaxed) * .35f));
    historyWrite = (historyWrite + 1) % 144;
  }
  void controlMenu(clap_id id, const char *name, CRect bar, double labelX,
                   double menuX, double menuWidth) {
    const double y = bar.top + 10.;
    at(name, labelX, y - 2.);
    const auto bounds = rect(menuX, y - 1., menuWidth, 15.);
    fill(bounds, gray(0x202020));
    fill(rect(bounds.left + 1., bounds.top + 1., 2., 13.), style.fill);
    text(id == 2 ? viewName(value(id)) : layoutName(value(id)),
         rect(bounds.left + 8., y - 2., bounds.getWidth() - 23., 15.),
         style.value);
    text("v", rect(bounds.right - 12., y - 2., 12., 15.), style.value);
    hit(rect(bounds.left, bounds.top, bounds.getWidth(), 18.),
        [this, id, bounds, bar] {
          std::vector<std::string> items;
          for (unsigned i = 0; i < (id == 2 ? 3 : kMeterLayoutCount); ++i)
            items.push_back(id == 2 ? viewName(i) : layoutName(i));
          openPopup(
              bounds, items, value(id), [this, id](int i) { set(id, i); }, 1,
              rect(bounds.left, bar.bottom + 3., id == 2 ? 82. : 132.,
                   items.size() * 20.),
              20.);
        });
  }
  void paintGrid(CRect bounds, unsigned visible, unsigned activeChannels);
  void paintField(CRect bounds, unsigned visible, unsigned activeChannels,
                  unsigned selectedLayout);
  void paintHeat(CRect bounds, unsigned visible, unsigned activeChannels);
  void paint() override;
  Plugin &p;
  std::string presetName = "INIT", fileError;
  clap_id active = CLAP_INVALID_ID;
  using Kind = s3g::clap_gui::ParamEventKind;
  unsigned cameraMode = 2, historyWrite = 0;
  double azimuth = 35., elevation = 34., zoom = 1.;
  CPoint lastDrag{};
  bool firstDrag = true;
  std::array<std::array<float, 144>, 64> history{};
};

void Editor::paintField(CRect bounds, unsigned visible, unsigned activeChannels,
                        unsigned selectedLayout) {
  fill(bounds, gray(0x111111));
  const auto fieldLayout = makeFieldLayout(selectedLayout, visible);
  const uint32_t fieldCount =
      std::min<uint32_t>(fieldLayout.count, kChannelCount);
  std::array<CPoint, kChannelCount> pts{};
  for (unsigned ch = 0; ch < fieldCount; ++ch)
    pts[ch] = project(fieldLayout.points[ch], bounds);
  auto edge = [&](unsigned a, unsigned b) {
    if (a < fieldCount && b < fieldCount)
      line(pts[a], pts[b], gray(0x575757, .46));
  };
  auto ring = [&](uint32_t base, uint32_t count) {
    if (count < 2u || base >= fieldCount)
      return;
    count = std::min<uint32_t>(count, fieldCount - base);
    for (uint32_t i = 0; i < count; ++i)
      edge(base + i, base + ((i + 1u) % count));
  };
  auto meshEqualDistanceEdges = [&](uint32_t count) {
    struct Candidate {
      uint32_t a;
      uint32_t b;
      float d2;
    };
    std::array<Candidate, kChannelCount * kChannelCount> candidates{};
    uint32_t candidateCount = 0;
    count = std::min<uint32_t>(count, fieldCount);
    for (uint32_t a = 0; a < count; ++a) {
      for (uint32_t b = a + 1u; b < count; ++b) {
        const float dx = fieldLayout.points[a].x - fieldLayout.points[b].x;
        const float dy = fieldLayout.points[a].y - fieldLayout.points[b].y;
        const float dz = fieldLayout.points[a].z - fieldLayout.points[b].z;
        candidates[candidateCount++] = {a, b, dx * dx + dy * dy + dz * dz};
      }
    }
    if (candidateCount == 0u)
      return;
    std::sort(candidates.begin(), candidates.begin() + candidateCount,
              [](const Candidate &lhs, const Candidate &rhs) {
                return lhs.d2 < rhs.d2;
              });
    const float threshold = candidates[0].d2 * 1.18f;
    for (uint32_t i = 0; i < candidateCount; ++i) {
      if (candidates[i].d2 > threshold)
        break;
      edge(candidates[i].a, candidates[i].b);
    }
  };
  const uint32_t resolvedLayout =
      selectedLayout == 0u ? autoLayoutForWidth(visible) : selectedLayout;
  switch (static_cast<MeterLayout>(resolvedLayout)) {
  case MeterLayout::Quad:
    ring(0, 4);
    break;
  case MeterLayout::QuadOh:
    ring(0, 4);
    edge(4, 0);
    edge(4, 3);
    edge(5, 1);
    edge(5, 2);
    edge(4, 5);
    break;
  case MeterLayout::Cube8:
    ring(0, 4);
    ring(4, 4);
    for (uint32_t i = 0; i < 4; ++i)
      edge(i, i + 4u);
    break;
  case MeterLayout::Cube17:
    ring(0, 4);
    edge(4, 5);
    edge(5, 6);
    edge(6, 7);
    edge(7, 8);
    edge(8, 9);
    edge(9, 10);
    edge(10, 11);
    edge(11, 4);
    ring(12, 4);
    edge(0, 4);
    edge(1, 6);
    edge(2, 8);
    edge(3, 10);
    edge(4, 12);
    edge(6, 13);
    edge(8, 14);
    edge(10, 15);
    edge(12, 16);
    edge(13, 16);
    edge(14, 16);
    edge(15, 16);
    break;
  case MeterLayout::Dome24:
  case MeterLayout::Dome25:
    ring(0, 12);
    ring(12, 8);
    ring(20, 4);
    for (uint32_t i = 0; i < 8; ++i) {
      const uint32_t lowerA = (i * 3u) / 2u;
      const uint32_t lowerB = (lowerA + 1u) % 12u;
      edge(12u + i, lowerA);
      edge(12u + i, lowerB);
    }
    for (uint32_t i = 0; i < 4; ++i) {
      edge(20u + i, 12u + i * 2u);
      edge(20u + i, 12u + ((i * 2u + 1u) % 8u));
    }
    if (resolvedLayout == static_cast<uint32_t>(MeterLayout::Dome25)) {
      for (uint32_t i = 0; i < 4; ++i)
        edge(24, 20u + i);
    }
    break;
  case MeterLayout::Dodeca12:
    meshEqualDistanceEdges(12);
    break;
  case MeterLayout::OctoRing:
    ring(0, 8);
    break;
  case MeterLayout::Ring12:
    ring(0, 12);
    break;
  case MeterLayout::Ring16:
    ring(0, 16);
    break;
  case MeterLayout::DoubleRing16:
    ring(0, 8);
    ring(8, 8);
    for (uint32_t i = 0; i < 8; ++i)
      edge(i, i + 8u);
    break;
  case MeterLayout::DoubleRing20:
    ring(0, 12);
    ring(12, 8);
    for (uint32_t i = 0; i < 8; ++i)
      edge(12u + i, (i * 3u / 2u) % 12u);
    break;
  case MeterLayout::Sphere24:
    meshEqualDistanceEdges(fieldCount);
    break;
  case MeterLayout::Auto:
  default:
    if (fieldCount > 2u)
      ring(0, fieldCount);
    break;
  }

  for (unsigned ch = 0; ch < fieldCount; ++ch) {
    const double peakNorm = normDb(p.peak[ch].load(std::memory_order_relaxed));
    const double rmsNorm = normDb(p.rms[ch].load(std::memory_order_relaxed));
    const double level = std::max(rmsNorm, peakNorm * .72),
                 size = 10. + 24. * level;
    const auto square =
        rect(pts[ch].x - size * .5, pts[ch].y - size * .5, size, size);
    fill(square, gray(ch < activeChannels ? 0x1c1c1c : 0x101010));
    const auto ink = spatialColor(peakNorm, rmsNorm);
    fill(CRect(square).inset(2., 2.), ink);
    insideFrame(square, ink);
    if (fieldCount <= 32)
      at(format("%u", ch + 1), square.left + 17., square.top - 1.);
  }
  const auto label = selectedLayout == 0
                         ? format("%s->%s", layoutName(0),
                                  layoutName(autoLayoutForWidth(visible)))
                         : std::string(layoutName(selectedLayout));
  at(format("%s  %uPT", label.c_str(), fieldCount), bounds.left + 10.,
     bounds.top + 8.);
}

void Editor::paintGrid(CRect bounds, unsigned visible,
                       unsigned activeChannels) {
  const unsigned cols =
      std::clamp(unsigned(std::ceil(std::sqrt(double(visible)))), 1u, 8u);
  const unsigned rows = unsigned(std::ceil(double(visible) / cols));
  const double cellW = (bounds.getWidth() - 8. * (cols - 1)) / cols;
  const double cellH = (bounds.getHeight() - 7. * (rows - 1)) / rows;
  for (unsigned ch = 0; ch < visible; ++ch) {
    const auto cell =
        rect(bounds.left + (ch % cols) * (cellW + 8.),
             bounds.top + (ch / cols) * (cellH + 7.), cellW, cellH);
    fill(cell, gray(ch < activeChannels ? 0x151515 : 0x0f0f0f));
    const float pk = p.peak[ch].load(std::memory_order_relaxed),
                rms = p.rms[ch].load(std::memory_order_relaxed);
    const double peakNorm = normDb(pk), rmsNorm = normDb(rms);
    const auto track =
        rect(cell.left + 31., cell.top + 12., cell.getWidth() - 43., 9.);
    fill(track, style.strip);
    fill(rect(track.left + 1., track.top + 1.,
              std::max(1., (track.getWidth() - 2.) * rmsNorm), 7.),
         gray(0x8b8b8b));
    const double x = std::clamp(track.left + track.getWidth() * peakNorm,
                                track.left + 1., track.right - 2.);
    fill(rect(x, track.top - 2., 2., 13.), style.text);
    at(format("%02u", ch + 1), cell.left + 8., cell.top + 9.);
    at(pk > .000001f
           ? format("%+.0f", 20.f * std::log10(std::max(.000001f, pk)))
           : "-inf",
       cell.right - 36., cell.top + 27.);
  }
  at("RMS  PK", bounds.left, bounds.bottom - 8.);
}
void Editor::paintHeat(CRect bounds, unsigned visible,
                       unsigned activeChannels) {
  fill(bounds, gray(0x101010));
  const double laneH = bounds.getHeight() / visible,
               colW = bounds.getWidth() / 144.;
  for (unsigned ch = 0; ch < visible; ++ch) {
    for (unsigned x = 0; x < 144; ++x) {
      const double value =
          std::clamp(double(history[ch][(historyWrite + x) % 144]), 0., 1.);
      fill(rect(bounds.left + x * colW, bounds.top + ch * laneH,
                std::ceil(colW) + .5, std::ceil(laneH) + .5),
           heatColor(value, ch < activeChannels ? .96 : .25));
    }
    if (laneH >= 10. && ch < 32) {
      fill(rect(bounds.left, bounds.top + ch * laneH, 26., laneH),
           gray(0x121212, .72));
      at(format("%02u", ch + 1), bounds.left + 5.,
         bounds.top + ch * laneH + 1.);
    }
  }
}
void Editor::paint() {
  const double width = std::max(720., getViewSize().getWidth()),
               height = std::max(420., getViewSize().getHeight());
  const unsigned visible = unsigned(value(1)), mode = unsigned(value(2)),
                 selectedLayout = unsigned(value(3));
  const unsigned activeChannels =
      p.activeChannels.load(std::memory_order_relaxed);
  title(width, height, activeChannels, visible);
  const auto bar = box(layout::analyzerToolbarRect({width, height}));
  fill(bar, style.strip);
  fill(rect(bar.left, bar.top, bar.getWidth(), 2.), style.accent);
  controlMenu(2, "VIEW", bar, bar.left + 14., bar.left + 72., 82.);
  controlMenu(3, "LAYOUT", bar, bar.left + 172., bar.left + 238., 116.);
  at("WIDTH", bar.left + 374., bar.top + 8.);
  const auto track = rect(bar.left + 430., bar.top + 11., 92., 9.);
  foundation::drawHorizontalSlider(*context, track, (visible - 1.) / 63.,
                                   track.top - 2., 13., style);
  text(format("%u", visible), rect(bar.left + 530., bar.top + 8., 40., 15.),
       style.value);
  drag(
      rect(bar.left + 424., bar.top + 6., 126., 24.),
      [this, track](CPoint point) {
        setWidth(1. + std::clamp((point.x - track.left) / 92., 0., 1.) * 63.);
      },
      [this] { beginWidth(); }, [this] { endWidth(); },
      [this] { set(1, info(1).default_value); });
  if (mode == 1) {
    for (unsigned i = 0; i < 2; ++i)
      button(zoomButton(i, bar), i == 0 ? "-" : "+", [this, i] {
        zoom = std::clamp(zoom * (i == 0 ? .86 : 1.16), .45, 2.75);
      });
    const char *names[] = {"TOP", "SIDE", "3/4"};
    for (unsigned i = 0; i < 3; ++i)
      button(
          viewButton(i, bar), names[i], [this, i] { setViewPreset(i); },
          i == cameraMode);
  }
  const auto content = box(layout::analyzerContentRect({width, height}));
  panel(content, "METER");
  const auto bounds = rect(content.left + 14., content.top + 38.,
                           content.getWidth() - 28., content.getHeight() - 52.);
  if (mode == 0)
    paintGrid(bounds, visible, activeChannels);
  else if (mode == 1) {
    paintField(bounds, visible, activeChannels, selectedLayout);
    drag(
        content,
        [this](CPoint point) {
          if (firstDrag) {
            firstDrag = false;
            lastDrag = point;
            return;
          }
          azimuth += (point.x - lastDrag.x) * .35;
          elevation =
              std::clamp(elevation + (point.y - lastDrag.y) * .25, -88., 88.);
          cameraMode = 2;
          lastDrag = point;
        },
        [this] { firstDrag = true; });
  } else
    paintHeat(bounds, visible, activeChannels);
}
} // namespace meter_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new meter_canvas::Editor(p);
}
