// Literal Cocoa adaptation shared by the three Topology interface designs.
// The local Plugin/state codecs remain authoritative. See
// TOPOLOGY_VSTGUI_PARITY.md.
namespace topology_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
using Kind = s3g::clap_gui::ParamEventKind;
using Event = s3g::clap_gui::ParamEvent;

#if defined(S3G_TOPOLOGY_DELAY_PORT)
constexpr clap_id outputId = kOutputTrimParamId;
constexpr clap_id topologyIds[] = {
    kTopologyShapeParamId,         kTopologySpreadParamId,
    kDisplaceCollapseParamId,      kDisplaceDirXParamId,
    kDisplaceDirYParamId,          kDisplaceDirZParamId,
    kDisplaceTwistParamId,         kDisplaceFlareParamId,
    kTopologyJitterParamId,        kTopologyMotionModeParamId,
    kTopologyMotionVariantParamId, kTopologyMotionRateParamId,
    kTopologyMotionDepthParamId,   kTopologyNeighborCountParamId,
    kTopologyRadiusParamId,        kTopologyCentroidParamId};
constexpr const char *presetDirectoryName = "Processor Delay";
constexpr const char *titleName = "s3g PROCESSOR DELAY";
constexpr const auto &stateExtension = state;
void publish(Plugin &p, clap_id id, double v) { setParam(p, id, v); }
#else
constexpr clap_id outputId = kGainParamId;
constexpr clap_id topologyIds[] = {
    kTopologyShapeParamId,   kTopologyAmountParamId,    kTopologyPullParamId,
    kTopologyXParamId,       kTopologyYParamId,         kTopologyZParamId,
    kTopologyTwistParamId,   kTopologyFlareParamId,     kTopologySeedParamId,
    kTopologyMotionParamId,  kTopologyVariantParamId,   kTopologyRateParamId,
    kTopologyDepthParamId,   kTopologyNeighborsParamId, kTopologyRadiusParamId,
    kTopologyCentroidParamId};
constexpr const auto &stateExtension = stateExt;
void publish(Plugin &p, clap_id id, double v) { applyParam(p, id, v); }
#if defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
constexpr const char *presetDirectoryName = "Processor Wave Geometry";
constexpr const char *titleName = "s3g PROCESSOR WAVE GEOMETRY";
#else
constexpr const char *presetDirectoryName = "Processor Spectral";
constexpr const char *titleName = "s3g PROCESSOR SPECTRAL";
#endif
#endif

class Editor final : public View {
public:
  explicit Editor(Plugin &instance, const foundation::FontMetrics &metrics =
                                        foundation::fontMetrics())
      : View(kGuiWidth, kGuiHeight), p(instance) {
    font = foundation::makeUiFont(metrics.body);
    titleFont = foundation::makeUiFont(metrics.title);
    smallFont = foundation::makeUiFont(metrics.channel);
  }
  ~Editor() override { stopRefresh(); }
  static CRect box(layout::Rect b) { return rect(b.x, b.y, b.width, b.height); }
  static CColor gray(unsigned v, double alpha = 1.) {
    auto c = color(s3g::portable_gui::topology_colors::gray[std::min(v, 255u)]);
    c.alpha = static_cast<uint8_t>(std::clamp(alpha, 0., 1.) * 255.);
    return c;
  }
  static CRect square(CPoint c, double size) {
    return rect(c.x - size * .5, c.y - size * .5, size, size);
  }
  double value(clap_id id) const {
    double v = 0.;
    paramsGetValue(&p.plugin, id, &v);
    return v;
  }
  clap_param_info_t info(clap_id id) const {
    clap_param_info_t result{};
    for (uint32_t i = 0; i < paramsCount(&p.plugin); ++i)
      if (paramsGetInfo(&p.plugin, i, &result) && result.id == id)
        return result;
    return {};
  }
  void requestService() {
    s3g::clap_gui::requestParamEventService(p.host, p.hostParams);
  }
  void markDirty() {
    if (!p.host || !p.host->get_extension)
      return;
    const auto *state = static_cast<const clap_host_state_t *>(
        p.host->get_extension(p.host, CLAP_EXT_STATE));
    if (state && state->mark_dirty)
      state->mark_dirty(p.host);
  }
  void begin(clap_id id) {
    if (active != CLAP_INVALID_ID || p.guiParamEvents.available() < 3u)
      return;
    if (p.guiParamEvents.push({Kind::GestureBegin, id, 0.})) {
      active = id;
      requestService();
    }
  }
  void setValue(clap_id id, double v) {
    if (active != id || p.guiParamEvents.available() < 2u || !std::isfinite(v))
      return;
    publish(p, id, v);
    p.guiParamEvents.push({Kind::Value, id, value(id)});
    requestService();
  }
  void end() {
    if (active == CLAP_INVALID_ID)
      return;
    p.guiParamEvents.push({Kind::GestureEnd, active, 0.});
    active = CLAP_INVALID_ID;
    requestService();
  }
  bool set(clap_id id, double v) {
    if (!std::isfinite(v) ||
        p.guiParamEvents.available() < 3u + (active != CLAP_INVALID_ID))
      return false;
    publish(p, id, v);
    const Event events[]{{Kind::GestureBegin, id, 0.},
                         {Kind::Value, id, value(id)},
                         {Kind::GestureEnd, id, 0.}};
    p.guiParamEvents.pushBatch(events, 3u);
    requestService();
    return true;
  }
  // All three original codecs publish controls/patch rows through atomic
  // banks. No GUI call prepares, resets, or reads mutable DSP audio state.
  bool presetFile(const std::string &path, bool save) {
    if (!save && p.guiParamEvents.available() <
                     paramsCount(&p.plugin) * 3u + (active != CLAP_INVALID_ID))
      return false;
    const double trim = value(outputId);
    const bool ok = save ? s3g::clap_gui::portable::saveStateFile(
                               &p.plugin, stateExtension, path.c_str())
                         : s3g::clap_gui::portable::loadStateFile(
                               &p.plugin, stateExtension, path.c_str());
    if (!ok)
      return false;
    if (!save) {
      publish(p, outputId, trim); // Cocoa LOAD deliberately preserves OUT.
      notifySnapshot();
      markDirty();
    }
    presetName =
        foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    fileError.clear();
    return true;
  }
  void notifySnapshot() {
    std::vector<Event> events;
    for (uint32_t i = 0; i < paramsCount(&p.plugin); ++i) {
      clap_param_info_t param{};
      paramsGetInfo(&p.plugin, i, &param);
      if (param.id == outputId)
        continue;
      events.push_back({Kind::GestureBegin, param.id, 0.});
      events.push_back({Kind::Value, param.id, value(param.id)});
      events.push_back({Kind::GestureEnd, param.id, 0.});
    }
    p.guiParamEvents.pushBatch(events.data(), events.size());
    requestService();
  }
  bool resetPreset() {
    if (p.guiParamEvents.available() <
        paramsCount(&p.plugin) * 3u + (active != CLAP_INVALID_ID))
      return false;
    for (uint32_t i = 0; i < paramsCount(&p.plugin); ++i) {
      clap_param_info_t param{};
      paramsGetInfo(&p.plugin, i, &param);
      if (param.id != outputId)
        publish(p, param.id, param.default_value);
    }
    notifySnapshot();
    markDirty();
    presetName = "INIT";
    fileError.clear();
    return true; // As in Cocoa, INIT leaves the matrix alone.
  }
  void choosePresetFile(bool save) {
    foundation::FileDialogOptions options;
    options.save = save;
    options.title =
        std::string(save ? "Save " : "Load ") + presetDirectoryName + " Preset";
    options.extension = "s3gpreset";
    options.extensionDescription = "s3g Preset";
    options.defaultSaveName = "Preset.s3gpreset";
    options.initialDirectory = foundation::presetDirectory(presetDirectoryName);
    const auto path = foundation::runFileDialog(getFrame(), options);
    if (!path.empty() && !presetFile(path, save))
      fileError = "PRESET FILE ERROR";
  }
  void title() {
    const auto band = layout::encoderTitleBand({kGuiWidth, kGuiHeight});
    const double titleWidth = band.presetLabelX - band.titleX - 10.;
    foundation::drawPluginTitle(
        *context, format("%s %uCH", titleName, kChannelCount),
        rect(band.titleX, 13., titleWidth, 15.), titleFont);
    text("PRESET", rect(band.presetLabelX, 13., 60., 15.), style.label);
    foundation::drawMenuBox(*context, box(band.presetMenu), upper(presetName),
                            font, style);
    hit(box(band.presetMenu), [this] { resetPreset(); });
    for (bool save : {false, true}) {
      const auto b = box(save ? band.saveButton : band.loadButton);
      foundation::drawButton(*context, b, save ? "SAVE" : "LOAD", font, false,
                             color(0x2b2b2b));
      hit(b, [this, save] { choosePresetFile(save); });
    }
    std::string status = format(
        "PK %+4.1f",
        20. * std::log10(std::max(
                  .000001f, p.outputPeak.load(std::memory_order_relaxed))));
#if defined(S3G_TOPOLOGY_DELAY_PORT)
    if (p.outputClip.exchange(false, std::memory_order_relaxed))
      status += " CLIP";
#endif
    const double x = band.saveButton.x + band.saveButton.width + 10.;
    text(fileError.empty() ? status : fileError,
         rect(x, 13., kGuiWidth - x - band.statusRightInset, 15.), style.value,
         kRightText);
  }
  // Coordinates below are the original logical content coordinates +8px,
  // matching the established 42px shared title-band clearance.
  void row(clap_id id, const char *label, double x, double y,
           const std::string &readout = {}, double minOverride = NAN,
           double maxOverride = NAN) {
    y += 8.;
    const auto param = info(id);
    const double lo =
        std::isfinite(minOverride) ? minOverride : param.min_value;
    const double hi =
        std::isfinite(maxOverride) ? maxOverride : param.max_value;
    const double v = value(id), n = std::clamp((v - lo) / (hi - lo), 0., 1.);
    const double tx = layout::processorControlX(x);
    const double tw = layout::processorTrackWidth(panelWidth);
    const double vx = layout::processorValueX(x, panelWidth);
    text(label, rect(layout::processorLabelX(x), y - 2., tx - x - 22., 15.),
         style.label);
    foundation::drawHorizontalSlider(*context, rect(tx, y + 1., tw, 9.), n,
                                     y - 1., 13., style);
    const auto val = readout.empty() ? format("%.0f%%", v * 100.) : readout;
    text(foundation::sliderValueTextToFit(*context, val, 42., font),
         rect(vx, y - 2., 42., 15.), style.value, kRightText);
    drag(
        rect(x, y - 8., panelWidth, 24.),
        [this, id, tx, tw, lo, hi](CPoint pt) {
          setValue(id, lo + std::clamp((pt.x - tx) / tw, 0., 1.) * (hi - lo));
        },
        [this, id] { begin(id); }, [this] { end(); },
        [this, id] { set(id, info(id).default_value); });
  }
  void menuRow(clap_id id, const char *label, double y,
               const std::vector<std::string> &items, int first = 0) {
    y += 8.;
    const double x = layout::processorControlX(secondaryX);
    text(label, rect(layout::processorLabelX(secondaryX), y - 2., 80., 15.),
         style.label);
    const CRect b =
        rect(x, y - 1., layout::processorMenuWidth(panelWidth), 15.);
    const int selected = std::clamp(static_cast<int>(value(id)) - first, 0,
                                    static_cast<int>(items.size()) - 1);
    foundation::drawMenuBox(*context, b, items[selected], font, style);
    hit(rect(secondaryX, y - 8., panelWidth, 24.), [this, id, b, items,
                                                    selected, first] {
      // Original in-canvas, 178px-wide popup, 18px rows. Never an OS menu.
      const double height = items.size() * 18.;
      const double top =
          std::clamp(b.top - 1., 36., double(kGuiHeight) - 10. - height);
      openPopup(
          b, items, selected, [this, id, first](int i) { set(id, i + first); },
          1, rect(b.left, top, 178., height), 18.);
    });
  }
  void topologyRows() {
    panel(rect(secondaryX, 42., panelWidth, layout::toolboxHeightForRows(16u)),
          "TOPOLOGY");
    const char *labels[]{"SHAP", "AMT",  "PULL", "X",    "Y",   "Z",
                         "TWST", "FLAR", "SEED", "ANIM", "VAR", "RATE",
                         "DPTH", "NBR",  "RAD",  "CENT"};
    for (uint32_t i = 0; i < 16u; ++i) {
      const clap_id id = topologyIds[i];
      const double y = 70. + i * 26.;
      if (i == 0 || i == 9 || i == 10 || i == 13) {
        std::vector<std::string> items;
        if (i == 0)
          for (uint32_t n = 0; n < s3g::kTopologyShapeCount; ++n)
            items.emplace_back(s3g::topologyShapeName(n));
        if (i == 9)
          for (uint32_t n = 0; n < s3g::kTopologyMotionModeCount; ++n)
            items.emplace_back(s3g::topologyMotionModeName(n));
        if (i == 10)
          for (uint32_t n = 0; n < s3g::kTopologyVariantCount; ++n)
            items.emplace_back(s3g::topologyVariantName(n));
        if (i == 13)
          items = {"1NN", "2NN", "3NN"};
        menuRow(id, labels[i], y, items, i == 13 ? 1 : 0);
      } else {
        const auto v = value(id);
        row(id, labels[i], secondaryX, y,
            i == 11 ? format("%4.2f", v)
                    : i >= 3 && i <= 7 ? format("%+3.0f%%", v * 100.)
                                       : format("%3.0f%%", v * 100.));
      }
    }
#if defined(S3G_TOPOLOGY_DELAY_PORT)
    const auto b = rect(secondaryX + panelWidth - 64., 46., 54., 15.);
    fill(b, style.strip);
    text("RESET", b, style.dim, kCenterText);
    hit(b, [this] { resetTopology(); });
#endif
  }
  void resetTopology() {
#if defined(S3G_TOPOLOGY_DELAY_PORT)
    if (p.guiParamEvents.available() < 52u + (active != CLAP_INVALID_ID))
      return;
    const double defaults[]{0, 0, 0, 0,   0, 1, 0,   0,
                            0, 0, 0, .10, 0, 2, .65, .22};
    for (size_t i = 0; i < 16; ++i)
      set(topologyIds[i], defaults[i]);
    set(kTopologySkewParamId, 0.);
    camera(2);
#endif
  }
  uint64_t patchRow(uint32_t input) const {
#if defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
    return p.publishedPatchRows[input].load(std::memory_order_relaxed);
#else
    return p.patch.rowMask(input);
#endif
  }
  void matrix(double y) {
    const bool compact = kChannelCount > 8;
    panel(rect(primaryX, y + 8., panelWidth, compact ? 354. : 248.),
          "PATCH MATRIX");
    const double left = compact ? 686. : 718.,
                 top = y + 8. + (compact ? 34. : 42.);
    const double cell = compact ? 12. : 24., gap = compact ? 2. : 4.,
                 inset = compact ? 2. : 5.;
    CFontRef face = compact ? smallFont.get() : font.get();
    for (uint32_t i = 0; i < kChannelCount; ++i) {
      text(format("%u", i + 1),
           rect(left + i * cell + (compact ? 0. : 8.),
                top - (compact ? 13. : 18.), 24., compact ? 11. : 15.),
           style.dim, kLeftText, face);
      text(format("I%u", i + 1),
           rect(654., top + i * cell + (compact ? 2. : 6.), 30.,
                compact ? 11. : 15.),
           style.dim, kLeftText, face);
      for (uint32_t o = 0; o < kChannelCount; ++o) {
        auto b = rect(left + o * cell, top + i * cell, cell - gap, cell - gap);
        fill(b, style.strip);
        if (patchRow(i) & (uint64_t{1} << o))
          fill(CRect(b).inset(inset, inset), style.accent);
        hit(rect(left + o * cell, top + i * cell, cell, cell), [this, i, o] {
          togglePatchCellFromGui(p, i, o);
          markDirty();
        });
      }
    }
#if defined(S3G_TOPOLOGY_DELAY_PORT)
    const char *footer = kLockUnusedChannelsToPassThrough
                             ? "UNUSED: PASS LOCK"
                             : p.clearUnused ? "UNUSED: CLEAR" : "UNUSED: PASS";
#elif defined(S3G_TOPOLOGY_WAVE_GEOMETRY_PORT)
    const char *footer = "UNUSED: CLEAR";
#else
    const char *footer = "PATCH: DIRECT IN / EMPTY: MESH RECEIVE";
#endif
    text(footer, rect(650., top + cell * kChannelCount + 18., 338., 15.),
         style.dim);
  }
  void camera(int view) {
    cameraView = view;
    yaw = view == 1 ? -1.57079632679 : view == 0 ? 0. : -.52;
    pitch = view == 0 ? .95 : view == 1 ? 0. : .34;
  }
  CPoint project(double x, double y, double z, CRect b,
                 double depth = .82) const {
    const double xr = x * std::cos(yaw) - z * std::sin(yaw);
    const double zr = x * std::sin(yaw) + z * std::cos(yaw);
    const double yr = y * std::cos(pitch) - zr * std::sin(pitch);
    const double zz = y * std::sin(pitch) + zr * std::cos(pitch);
    const double scale = depth + zz * .08;
    return {b.left + b.getWidth() * .5 + xr * b.getWidth() * .25 * scale,
            b.top + b.getHeight() * .52 - yr * b.getHeight() * .38 * scale};
  }
  CPoint project(const s3g::TopologyPoint &pt, CRect b) const {
    return project(pt.x, pt.y, pt.z, b);
  }
  void headerButton(CRect b, const char *label, bool selected,
                    std::function<void()> action) {
    // Cocoa toolbox buttons have a separate, brighter header palette.
    fill(b, gray(selected ? 0x50 : 0x38));
    text(label, b, style.dim, kCenterText);
    hit(b, std::move(action));
  }
  void field() {
    const auto b = box(layout::kTopologyProcessorColumns.field);
    panel(b, "TOPOLOGY");
    for (uint32_t i = 0; i < 2; ++i) {
      auto tab = rect(b.left + (b.getWidth() - 124.) * .5 + i * 66., b.top + 3.,
                      58., 15.);
#if defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
      const char *name = i ? "SONO" : "TOPO";
#else
      const char *name = i ? "SCOPE" : "TOPO";
#endif
      headerButton(tab, name, fieldPage == int(i),
                   [this, i] { fieldPage = i; });
    }
    if (fieldPage == 0)
      for (uint32_t i = 0; i < 3; ++i) {
        const char *labels[]{"TOP", "SIDE", "3/4"};
        headerButton(rect(b.right - 16. - 138. + i * 48., b.top + 3., 42., 15.),
                     labels[i], cameraView == int(i), [this, i] { camera(i); });
      }
    const auto f = rect(b.left + 10., b.top + 28., b.getWidth() - 20.,
                        b.getHeight() - 38.);
    fill(f, style.strip);
    if (fieldPage == 1) {
      scope(f);
      return;
    }
    // Registered BEFORE LST: the button must win over the background drag.
    drag(
        f,
        [this](CPoint pt) {
          if (!cameraDragging) {
            lastPoint = pt;
            cameraDragging = true;
            return;
          }
          yaw += (pt.x - lastPoint.x) * .015;
          pitch = std::clamp(pitch + (pt.y - lastPoint.y) * .012, -.75, .95);
          cameraView = -1;
          lastPoint = pt;
        },
        [this] { cameraDragging = false; }, [this] { cameraDragging = false; });
    graph(f);
  }
  CRect channelRect(CRect b, uint32_t lane) const {
    const uint32_t cols = kChannelCount <= 8 ? 2 : 4,
                   rows = (kChannelCount + cols - 1) / cols;
    const double gap = kChannelCount <= 8 ? 8. : 5.;
    const double w = (b.getWidth() - gap * (cols + 1)) / cols;
    const double h = (b.getHeight() - 24. - gap * (rows + 1)) / rows;
    return rect(b.left + gap + (lane % cols) * (w + gap),
                b.top + 24. + gap + (lane / cols) * (h + gap), w, h);
  }
  void heatmap(const s3g::TopologyState &state, uint32_t lanes, CRect b) {
    std::array<double, 54 * 18> heat{};
    const double peak =
        s3g::fillTopologyHeatmap(state, lanes, 54, 18, heat.data());
    const double w = b.getWidth() / 54., h = b.getHeight() / 18.;
    for (uint32_t y = 0; y < 18; ++y)
      for (uint32_t x = 0; x < 54; ++x) {
        const double v =
            std::pow(std::clamp(heat[y * 54 + x] / peak, 0., 1.), .72);
        const auto index = static_cast<size_t>(std::lround(v * 1024.));
        fill(rect(b.left + x * w, b.top + y * h, w, h),
             color(s3g::portable_gui::topology_colors::heat[index]));
      }
  }
  void readoutButton(CRect field) {
    const auto b = rect(field.right - 42., field.top + 54., 32., 15.);
    fill(b, style.strip);
    text(showReadout ? "X" : "LST", b, style.dim, kCenterText);
    hit(b, [this] { showReadout = !showReadout; });
  }
  void smallText(const std::string &s, double x, double y, double w = 188.) {
    text(s, rect(x, y, w, 15.), style.dim);
  }
  void controls();
  void graph(CRect);
  void scope(CRect);
  void paint() override {
#if !defined(S3G_TOPOLOGY_SPECTRAL_TOPOLOGY_PORT)
    syncGuiSettings(p);
#endif
    title();
    field();
    controls();
    topologyRows();
  }
  Plugin &p;
  clap_id active = CLAP_INVALID_ID;
  std::string presetName = "CURRENT", fileError;
  int fieldPage = 0, cameraView = 2;
  double yaw = -.52, pitch = .34;
  bool showReadout = false, cameraDragging = false;
  CPoint lastPoint{};
  static constexpr double primaryX = layout::kTopologyProcessorColumns.first.x;
  static constexpr double secondaryX =
      layout::kTopologyProcessorColumns.second.x;
  static constexpr double panelWidth =
      layout::kTopologyProcessorColumns.first.width;
};

#include "s3g_topology_controls.inc"
#if defined(S3G_TOPOLOGY_DELAY_PORT)
#include "s3g_topology_delay_graph.inc"
#else
#include "s3g_topology_mesh_graph.inc"
#endif
#include "s3g_topology_scope.inc"
} // namespace topology_canvas

s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new topology_canvas::Editor(p);
}
