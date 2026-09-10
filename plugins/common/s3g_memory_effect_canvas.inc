// Spectral Spray 2/8, Buffer 8, Delay Field 16: original Cocoa geometry.
namespace memory_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
constexpr const auto &stateExtension = stateExt;
#if defined(S3G_MEMORY_SPRAY_PORT)
constexpr const char *pluginName = "Effect Spectral Spray";
constexpr const char *pluginTitle = kChannelCount == 2
                                        ? "s3g EFFECT SPECTRAL SPRAY 2CH"
                                        : "s3g EFFECT SPECTRAL SPRAY 8CH";
constexpr clap_id preservedOutput = kGainParamId;
constexpr const char *initialPresetName = "INIT";
#elif defined(S3G_MEMORY_BUFFER_PORT)
constexpr const char *pluginName = "Processor Buffer";
constexpr const char *pluginTitle = "s3g PROCESSOR BUFFER 8CH";
constexpr clap_id preservedOutput = kOutputParamId;
constexpr const char *initialPresetName = "CURRENT";
#else
constexpr const char *pluginName = "Effect Delay Field 16";
constexpr const char *pluginTitle = "s3g EFFECT DELAY FIELD";
// Cocoa resets/restores all four model gains; there is no global OUT param.
constexpr clap_id preservedOutput = CLAP_INVALID_ID;
constexpr const char *initialPresetName = "INIT";
#endif

double clampParamValue(clap_id id, double value) {
  clap_param_info_t info{};
  const auto index = memoryParamIndex(id);
  if (index >= kParamCount || !std::isfinite(value))
    return 0.;
  paramsGetInfo(nullptr, index, &info);
  value = std::clamp(value, info.min_value, info.max_value);
  if (info.flags & CLAP_PARAM_IS_STEPPED)
    return std::round(value);
  return static_cast<float>(value);
}
// Shared interaction primitives; layouts/graphics below are literal Cocoa
// ports.
class EffectEditor : public View {
public:
  explicit EffectEditor(
      Plugin &instance,
      const foundation::FontMetrics &metrics = foundation::fontMetrics())
      : View(kGuiWidth, kGuiHeight), p(instance) {
    font = foundation::makeUiFont(metrics.body);
    titleFont = foundation::makeUiFont(metrics.title);
  }
  ~EffectEditor() override { stopRefresh(); }
  static CRect box(s3g::gui_layout::Rect r) {
    return rect(r.x, r.y, r.width, r.height);
  }
  double value(clap_id id) const {
    double v = 0.;
    paramsGetValue(&p.plugin, id, &v);
    return v;
  }
  clap_param_info_t info(clap_id id) const {
    clap_param_info_t result{};
    for (uint32_t i = 0; i < kParamCount; ++i)
      if (paramsGetInfo(&p.plugin, i, &result) && result.id == id)
        return result;
    return {};
  }
  std::string display(clap_id id) const {
    char result[64]{};
    paramsValueToText(&p.plugin, id, value(id), result, sizeof(result));
#if defined(S3G_MEMORY_SPRAY_PORT)
    if (id == kSprayBinsParamId)
      std::snprintf(result, sizeof(result), "%.0f", value(id));
#elif defined(S3G_MEMORY_BUFFER_PORT)
    if (id == kSizeParamId || id == kGlideParamId)
      std::snprintf(result, sizeof(result), "%.0f", value(id));
#endif
    return result;
  }
  void publish(clap_id id, double v) {
    const auto index = memoryParamIndex(id);
    if (index < kParamCount)
      p.memoryValues[index].store(v, std::memory_order_release);
  }
  void requestService() {
    s3g::clap_gui::requestParamEventService(p.host, p.memoryHostParams);
  }
  void begin(clap_id id) {
    // Reserve an END slot before accepting BEGIN. No gesture can be left
    // open if the host temporarily stops draining its parameter events.
    if (memoryParamIndex(id) >= kParamCount || active != CLAP_INVALID_ID ||
        p.memoryGuiEvents.available() < 3u)
      return;
    if (p.memoryGuiEvents.push({Kind::GestureBegin, id, 0.})) {
      active = id;
      requestService();
    }
  }
  void setValue(clap_id id, double v) {
    if (!std::isfinite(v) || active != id || p.memoryGuiEvents.available() < 2u)
      return;
    v = clampParamValue(id, v);
    publish(id, v);
    if (p.memoryGuiEvents.push({Kind::Value, id, v})) {
      requestService();
    }
  }
  void end() {
    if (active == CLAP_INVALID_ID)
      return;
    p.memoryGuiEvents.push({Kind::GestureEnd, active, 0.});
    active = CLAP_INVALID_ID;
    requestService();
  }
  bool set(clap_id id, double v) {
    if (!std::isfinite(v) || memoryParamIndex(id) >= kParamCount)
      return false;
    v = clampParamValue(id, v);
    const s3g::clap_gui::ParamEvent events[]{{Kind::GestureBegin, id, 0.},
                                             {Kind::Value, id, v},
                                             {Kind::GestureEnd, id, 0.}};
    if (p.memoryGuiEvents.available() < 3u + (active != CLAP_INVALID_ID))
      return false;
    publish(id, v);
    if (!p.memoryGuiEvents.pushBatch(events, 3u))
      return false;
    requestService();
    return true;
  }
  bool applySnapshot(Plugin *snapshot) {
    std::array<s3g::clap_gui::ParamEvent, kParamCount * 3u> events{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < kParamCount; ++i) {
      clap_param_info_t param{};
      paramsGetInfo(&p.plugin, i, &param);
      if (param.id == preservedOutput)
        continue;
      double v = param.default_value;
      if (snapshot)
        paramsGetValue(&snapshot->plugin, param.id, &v);
      events[count++] = {Kind::GestureBegin, param.id, 0.};
      events[count++] = {Kind::Value, param.id, v};
      events[count++] = {Kind::GestureEnd, param.id, 0.};
    }
    if (p.memoryGuiEvents.available() < count + (active != CLAP_INVALID_ID))
      return false;
    for (uint32_t i = 1; i < count; i += 3) {
      events[i].value = clampParamValue(events[i].paramId, events[i].value);
      publish(events[i].paramId, events[i].value);
    }
#if defined(S3G_MEMORY_SPRAY_PORT)
    if (snapshot) {
      p.memoryDamage.store(snapshot->memoryDamage.load());
      p.memoryRepeat.store(snapshot->memoryRepeat.load());
    }
#endif
    if (!p.memoryGuiEvents.pushBatch(events.data(), count))
      return false;
    requestService();
    return true;
  }
  bool presetFile(const std::string &path, bool save) {
    // Parse/load/save on an isolated instance using the original state codec.
    // The live DSP remains owned by process/flush.
    auto snapshot = std::make_unique<Plugin>();
    snapshot->plugin.plugin_data = snapshot.get();
    init(&snapshot->plugin);
    bool result = false;
    if (save) {
      for (uint32_t i = 0; i < kParamCount; ++i) {
        clap_param_info_t param{};
        paramsGetInfo(&p.plugin, i, &param);
        applyParam(*snapshot, param.id, value(param.id));
      }
#if defined(S3G_MEMORY_SPRAY_PORT)
      snapshot->memoryDamage.store(p.memoryDamage.load());
      snapshot->memoryRepeat.store(p.memoryRepeat.load());
#endif
      result = s3g::clap_gui::portable::saveStateFile(
          &snapshot->plugin, stateExtension, path.c_str());
    } else {
      result = s3g::clap_gui::portable::loadStateFile(
          &snapshot->plugin, stateExtension, path.c_str());
      if (result)
        result = applySnapshot(snapshot.get());
    }
    if (result)
      presetName =
          foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    return result;
  }
  void choosePresetFile(bool save) {
    foundation::FileDialogOptions options;
    options.save = save;
    options.title =
        std::string(save ? "Save " : "Load ") + pluginName + " Preset";
    options.extension = "s3gpreset";
    options.extensionDescription = "s3g Preset";
    options.defaultSaveName = "Preset.s3gpreset";
    options.initialDirectory = foundation::presetDirectory(pluginName);
    const auto path = foundation::runFileDialog(getFrame(), options);
    if (!path.empty())
      fileError = presetFile(path, save) ? "" : "PRESET FILE ERROR";
  }
  void fittedText(const std::string &value, CRect bounds, CColor c,
                  CHoriTxtAlign align = kLeftText, CFontRef face = nullptr) {
    if (!face)
      face = font;
    context->setFont(face);
    const double width = context->getStringWidth(value.c_str());
    auto fitted = makeOwned<CFontDesc>(*face);
    if (width > bounds.getWidth() && width > 0.)
      fitted->setSize(face->getSize() * bounds.getWidth() / width);
    text(value, bounds, c, align, fitted);
  }
  static std::string peakText(float peak) {
    return format("PK %+4.1f", 20. * std::log10(std::max(.000001f, peak)));
  }
  void paintTitle(const std::string &status) {
    const auto band =
        s3g::gui_layout::encoderTitleBand({kGuiWidth, kGuiHeight});
    auto titleFace = makeOwned<CFontDesc>(*titleFont);
    context->setFont(titleFace);
    const auto title = foundation::pluginTitleText(pluginTitle);
    const double titleWidth = band.presetLabelX - band.titleX - 10.;
    const double measured = context->getStringWidth(title.c_str());
    if (measured > titleWidth)
      titleFace->setSize(titleFace->getSize() * titleWidth / measured);
    foundation::drawPluginTitle(
        *context, title, rect(band.titleX, 13., titleWidth, 15.), titleFace);
    text("PRESET", rect(band.presetLabelX, 13., 60., 15.), style.label);
    const auto menu = box(band.presetMenu);
    foundation::drawMenuBox(*context, menu, upper(presetName), font, style);
    // Cocoa's INIT field is a reset action, not a nonexistent factory bank.
    hit(menu, [this] {
      if (applySnapshot(nullptr)) {
        presetName = "INIT";
        fileError.clear();
      }
    });
    for (bool save : {false, true}) {
      const auto b = box(save ? band.saveButton : band.loadButton);
      foundation::drawButton(*context, b, save ? "SAVE" : "LOAD", font, false,
                             color(0x2b2b2b));
      hit(b, [this, save] { choosePresetFile(save); });
    }
    const double x = band.saveButton.x + band.saveButton.width + 10.;
    fittedText(fileError.empty() ? status : fileError,
               rect(x, 13., kGuiWidth - x - band.statusRightInset, 15.),
               style.value, kRightText);
  }
  void slider(clap_id id, const char *label, CRect hitBounds, CRect track,
              CRect labelBounds, CRect valueBounds, double normalized) {
    fittedText(label, labelBounds, style.label);
    foundation::drawHorizontalSlider(*context, track, normalized,
                                     track.top - 2., 13., style);
    const auto readout = foundation::sliderValueTextToFit(
        *context, display(id), valueBounds.getWidth(), font);
    fittedText(readout, valueBounds, style.value, kRightText);
    drag(
        hitBounds,
        [this, id, track](CPoint pt) {
          setValue(id,
                   fromNormalized(id, (pt.x - track.left) / track.getWidth()));
        },
        [this, id] { begin(id); }, [this] { end(); },
        [this, id] { set(id, info(id).default_value); });
  }
  virtual double fromNormalized(clap_id id, double norm) const {
    auto param = info(id);
    return clampParamValue(id, param.min_value +
                                   std::clamp(norm, 0., 1.) *
                                       (param.max_value - param.min_value));
  }
  void controlMenu(clap_id id, CRect bounds,
                   const std::vector<std::string> &items) {
    foundation::drawMenuBox(*context, bounds, upper(display(id)), font, style);
    hit(bounds, [this, id, bounds, items] {
      const double height = items.size() * 18.;
      double top = bounds.bottom + 3.;
      if (top + height > kGuiHeight - 8.)
        top = bounds.top - height - 3.;
      openPopup(
          bounds, items, static_cast<int>(value(id)),
          [this, id](int index) { set(id, index); }, 1,
          rect(bounds.left, top, bounds.getWidth(), height), 18.);
    });
  }
  Plugin &p;
  std::string presetName = initialPresetName, fileError;
  clap_id active = CLAP_INVALID_ID;
  using Kind = s3g::clap_gui::ParamEventKind;
};

class Editor final : public EffectEditor {
public:
  using EffectEditor::EffectEditor;
  static CColor gray(unsigned rgb, double alpha = 1.) {
    auto c = color(s3g::portable_gui::topology_colors::gray[rgb & 255u]);
    c.alpha = static_cast<uint8_t>(std::clamp(alpha, 0., 1.) * 255.);
    return c;
  }
  void row(clap_id id, const char *label, double y, double panelX,
           double panelWidth, std::vector<std::string> items = {},
           double popupTop = -1., bool toggle = false) {
    const double x = layout::processorControlX(panelX);
    const auto labelBounds =
        rect(layout::processorLabelX(panelX), y - 2., x - panelX - 20., 15.);
    const auto hitBounds = rect(panelX, y - 8., panelWidth, 24.);
    const auto controlBounds =
        rect(x, y - 1., layout::processorMenuWidth(panelWidth), 15.);
    if (!items.empty()) {
      fittedText(label, labelBounds, style.label);
      foundation::drawMenuBox(*context, controlBounds, upper(display(id)), font,
                              style);
      // Buffer menus originally respond only on the control; compact effects
      // use their complete row. Keep both original hit conventions.
#if defined(S3G_MEMORY_BUFFER_PORT)
      const auto menuHit = rect(x, y - 1., controlBounds.getWidth(), 18.);
#else
      const auto menuHit = hitBounds;
#endif
      hit(menuHit, [this, id, controlBounds, items, popupTop] {
        const double top =
            popupTop >= 0. ? popupTop : controlBounds.bottom + 3.;
        openPopup(
            controlBounds, items, static_cast<int>(value(id)),
            [this, id](int index) { set(id, index); }, 1,
            rect(controlBounds.left, top, controlBounds.getWidth(),
                 18. * items.size()),
            18.);
      });
    } else if (toggle) {
      fittedText(label, labelBounds, style.label);
      const bool on = value(id) >= .5;
      fill(controlBounds, gray(on ? 0x303030 : 0x151515));
      fill(rect(controlBounds.left + 1., controlBounds.top + 1., 2., 13.),
           style.fill);
      text(on ? "ON" : "OFF", controlBounds, style.value, kCenterText);
      hit(hitBounds, [this, id, on] { set(id, on ? 0. : 1.); });
    } else {
      const auto param = info(id);
      double norm =
          (value(id) - param.min_value) / (param.max_value - param.min_value);
#if defined(S3G_MEMORY_SPRAY_PORT)
      // Preserve Cocoa's HI display normalization, distinct from its drag map.
      if (id == kHiFreqParamId)
        norm = value(id) / 24000.;
#endif
      slider(id, label, hitBounds,
             rect(x, y + 1., layout::processorTrackWidth(panelWidth), 9.),
             labelBounds,
             rect(layout::processorValueX(panelX, panelWidth), y - 2.,
                  layout::kStandardMetrics.processorValueWidth, 15.),
             norm);
    }
  }
  void row(clap_id id, const char *label, const layout::Panel &panel,
           unsigned index, std::vector<std::string> items = {},
           bool toggle = false) {
    row(id, label, layout::rowY(panel, index), panel.frame.x, panel.frame.width,
        items, -1., toggle);
  }
#if defined(S3G_MEMORY_BUFFER_PORT)
  void action(s3g::clap_gui::MemoryEffectAction kind) {
    if (p.memoryGuiEvents.available() <= (active != CLAP_INVALID_ID))
      return;
    if (p.memoryGuiEvents.pushAction(kind))
      requestService();
  }
  void actionButton(CRect bounds, const char *label,
                    s3g::clap_gui::MemoryEffectAction kind) {
    fill(bounds, gray(0x202020));
    // Cocoa NSFrameRect uses the current fill, not setStroke; both frames
    // are the button background in the rendered reference.
    text(label, bounds, style.value, kCenterText);
    hit(bounds, [this, kind] { action(kind); });
  }
  void bufferWindows() {
    s3g::BufferProcessorParams params{};
    params.sizeMs = static_cast<float>(value(kSizeParamId));
    params.rate = static_cast<float>(value(kRateParamId));
    params.crossfade = static_cast<float>(value(kXfadeParamId));
    params.repeat = static_cast<float>(value(kRepeatParamId));
    params.skip = static_cast<float>(value(kSkipParamId));
    params.skipGrid = static_cast<float>(value(kSkipGridParamId));
    params.skipJam = static_cast<float>(value(kSkipJamParamId));
    params.skipChase = static_cast<float>(value(kSkipChaseParamId));
    params.skipSync = static_cast<float>(value(kSkipSyncParamId));
    params.reverse = static_cast<float>(value(kReverseParamId));
    params.crush = static_cast<float>(value(kCrushParamId));
    params.error = static_cast<float>(value(kErrorParamId));
    params.errorMode = static_cast<float>(value(kErrorModeParamId));
    params.memory = static_cast<float>(value(kMemoryParamId));
    params.spread = static_cast<float>(value(kSpreadParamId));
    params.deviation = static_cast<float>(value(kDeviationParamId));
    params.skew = static_cast<float>(value(kSkewParamId));
    params.center = static_cast<float>(value(kCenterParamId));
    params.glideMs = static_cast<float>(value(kGlideParamId));
    params.mix = static_cast<float>(value(kMixParamId));
    params.outputGainDb = static_cast<float>(value(kOutputParamId));
    const auto bounds = rect(30., 340., 330., 204.);
    CColor ink;

    ink = gray(0x111111);
    fill(bounds, ink);
    const double labelX = bounds.left + 10.0;
    const double laneX = bounds.left + 48.0;
    const double laneW = bounds.getWidth() - 66.0;
    const double rowH =
        (bounds.getHeight() - 46.0) /
        static_cast<double>(std::max<uint32_t>(1u, kChannelCount - 1u));
    const double baseY = bounds.top + 32.0;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
      const float u =
          static_cast<float>(ch) /
          static_cast<float>(std::max<uint32_t>(1u, kChannelCount - 1u));
      const float centered =
          std::clamp((u - params.center) * 2.0f, -1.0f, 1.0f);
      uint32_t x = ch * 747796405u + 2891336453u;
      x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
      x = (x >> 22u) ^ x;
      const float dev = static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
      const float ratio = std::pow(2.0f, centered * params.spread) *
                          std::pow(2.0f, dev * params.deviation * 0.65f) *
                          std::pow(2.0f, params.skew * u);
      const double y = baseY + static_cast<double>(ch) * rowH;
      text(format("L%u", ch + 1u), rect(labelX, y - 4., 30., 13.), style.value);
      const auto track = rect(laneX, y, laneW, 7.0);
      ink = gray(0x171717);
      fill(track, ink);
      const double windowW =
          std::clamp<double>(laneW * 0.20 * ratio, 8.0, laneW);
      const double pos =
          laneX + std::fmod(static_cast<double>(ch) * 31.0 +
                                static_cast<double>(params.repeat) * 96.0,
                            laneW - windowW + 1.0);
      ink = gray(params.skip > 0.01f
                     ? 0xd8d8d8
                     : (params.reverse > 0.01f ? 0xc8c8c8 : 0x8f8f8f));
      fill(rect(pos, y + 1.0, windowW, 5.0), ink);
      if (params.skip > 0.001f) {
        ink = gray(0xffffff, 0.35 + params.skip * 0.35);
        const double skipW = std::clamp<double>(
            windowW * (0.12 + params.skip * 0.22), 2.0, windowW);
        for (uint32_t marker = 0; marker < 3u; ++marker) {
          const double mx =
              pos + std::fmod(static_cast<double>(marker) * skipW * 2.1 +
                                  static_cast<double>(ch) * 5.0,
                              std::max<double>(1.0, windowW - skipW));
          fill(rect(mx, y + 1.0, skipW, 5.0), ink);
        }
        if (params.skipChase > 0.001f) {
          ink = gray(0x9f9f9f, 0.35 + params.skipChase * 0.35);
          const double chaseX =
              laneX + std::fmod(static_cast<double>(ch) * laneW * 0.11 +
                                    static_cast<double>(params.skipChase) *
                                        laneW * 0.35,
                                laneW - 2.0);
          fill(rect(chaseX, y - 1.0, 2.0, 11.0), ink);
        }
      }
      if (params.error > 0.001f) {
        ink = gray(0x101010, 0.35 + params.error * 0.45);
        const double blockW =
            std::max<double>(2.0, laneW * (0.012 + params.error * 0.025));
        for (uint32_t marker = 0; marker < 5u; ++marker) {
          const double mx =
              laneX + std::fmod(static_cast<double>(marker * 37u + ch * 19u) *
                                    (1.0 + params.error),
                                laneW - blockW + 1.0);
          fill(rect(mx, y + 1.0, blockW, 5.0), ink);
        }
      }
      if (params.memory > 0.001f) {
        ink = gray(0xffffff, 0.05 + params.memory * 0.12);
        const double ghostW =
            std::max<double>(4.0, laneW * (0.08 + params.memory * 0.20));
        const double ghostX =
            laneX + std::fmod(static_cast<double>(ch) * 17.0 +
                                  static_cast<double>(params.memory) * 41.0,
                              laneW - ghostW + 1.0);
        fill(rect(ghostX, y + 2.0, ghostW, 3.0), ink);
      }
      if (params.crossfade > 0.001f) {
        ink = gray(0xffffff, 0.55);
        const double xf = std::max<double>(1.0, windowW * params.crossfade);
        fill(rect(pos, y + 1.0, xf, 5.0), ink);
        fill(rect(pos + windowW - xf, y + 1.0, xf, 5.0), ink);
      }
    }
  }

  void paintBuffer() {
    panel(rect(18., 42., 352., 254.), "ENGINE");
    panel(rect(18., 308., 352., 248.), "BUFFER WINDOWS");
    panel(rect(388., 42., 354., 80.), "OUTPUT");
    panel(rect(388., 134., 354., 166.), "RELATIONSHIPS");
    panel(rect(388., 312., 354., 208.), "CORRUPT");
    const clap_id engine[] = {kSizeParamId,   kRateParamId, kXfadeParamId,
                              kRepeatParamId, kSkipParamId, kReverseParamId,
                              kCrushParamId,  kErrorParamId};
    const char *engineLabels[] = {"SIZE", "RATE", "XFD",  "RPT",
                                  "SKIP", "REV",  "CRSH", "ERR"};
    for (unsigned i = 0; i < 8; ++i)
      row(engine[i], engineLabels[i], 78. + 26. * i, 18., 352.);
    row(kOutputParamId, "OUT", 78., 388., 354.);
    row(kMixParamId, "MIX", 104., 388., 354.);
    const clap_id rel[] = {kSpreadParamId, kDeviationParamId, kSkewParamId,
                           kCenterParamId, kGlideParamId};
    const char *relLabels[] = {"SPRD", "DEV", "SKW", "CTR", "GLD"};
    for (unsigned i = 0; i < 5; ++i)
      row(rel[i], relLabels[i], 170. + 26. * i, 388., 354.);
    row(kSkipGridParamId, "GRID", 348., 388., 354.,
        {"OFF", "1/8", "1/12", "1/16", "1/32"}, 364.);
    row(kSkipSyncParamId, "SYNC", 374., 388., 354., {"FREE", "SYNC"}, 390.);
    row(kSkipJamParamId, "JAM", 400., 388., 354.);
    row(kSkipChaseParamId, "CHAS", 426., 388., 354.);
    row(kErrorModeParamId, "ERRM", 452., 388., 354.,
        {"PCM", "BLK", "CRC", "HDR", "RAW"}, 468.);
    row(kMemoryParamId, "GHST", 478., 388., 354.);
    actionButton(rect(496., 497., 54., 18.), "CAP",
                 s3g::clap_gui::MemoryEffectAction::Capture);
    actionButton(rect(558., 497., 54., 18.), "CLR",
                 s3g::clap_gui::MemoryEffectAction::Clear);
    bufferWindows();
  }
#endif

#if defined(S3G_MEMORY_SPRAY_PORT)
  void paintSpray() {
    constexpr auto output = layout::compactEffectOutputPanel(3u);
    constexpr auto range = layout::compactEffectLeftPanel(
        output, layout::PanelRole::ToneShape, 4u);
    constexpr auto motion =
        layout::compactEffectRightPanel(layout::PanelRole::Motion, 7u);
    panel(box(output.frame), "OUTPUT");
    panel(box(range.frame), "RANGE / PHASE");
    panel(box(motion.frame), "SPECTRAL MOTION");
    row(kGainParamId, "OUT", output, 0);
    row(kMixParamId, "MIX", output, 1);
    row(kSafetyParamId, "SAFE", output, 2);
    row(kLoFreqParamId, "LO", range, 0);
    row(kHiFreqParamId, "HI", range, 1);
    row(kTiltParamId, "TILT", range, 2);
    row(kPhaseBlurParamId, "PHAS", range, 3);
    const clap_id ids[] = {kSprayBinsParamId, kDriftParamId,    kHoldParamId,
                           kFreezeParamId,    kFeedbackParamId, kSmearParamId,
                           kHolesParamId};
    const char *labels[] = {"BINS", "DRFT", "HOLD", "FRZ",
                            "FDBK", "SMR",  "HOLE"};
    for (unsigned i = 0; i < 7; ++i)
      row(ids[i], labels[i], motion, i);
    text("FFT 4096 / 8x OLA",
         rect(motion.frame.x + 16., motion.frame.y + motion.frame.height + 12.,
              motion.frame.width - 32., 15.),
         style.value);
  }
#endif
#if defined(S3G_MEMORY_FIELD_PORT)
  static constexpr auto fieldPanel = layout::compactEffectOutputPanel(3u);
  static constexpr auto modelLeft = layout::compactEffectLeftPanel(
      fieldPanel, layout::PanelRole::EventTiming, 7u);
  static constexpr auto modelRight =
      layout::compactEffectRightPanel(layout::PanelRole::Projection, 7u);
  void fieldRow(const ParamDef &def, unsigned index,
                const layout::Panel &panel) {
    std::vector<std::string> items;
    if (def.id == 1u)
      items = {"SHARD", "ORBIT", "CASCADE", "ITERATE"};
    if (def.id == 2u)
      items = {"16CH DIRECT", "8CH RING", "QUAD RING", "STEREO RING"};
    if (def.id == 410u)
      items = {"SEQUENTIAL", "REVERSE", "PALINDROME", "RANDOM", "RANDOM CYCLE"};
    row(def.id, def.label, panel, index, items,
        def.id >= 411u && def.id <= 413u);
  }
  void vu(unsigned channel) {
    const float peak = p.sourcePeaks[channel].load(std::memory_order_relaxed);
    const float db = peak > .000001f ? 20.f * std::log10(peak) : -60.f;
    const double norm = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
    const auto bounds = rect(34. + 42. * channel, 414., 24., 52.);
    fill(bounds, style.strip);
    const auto inner = rect(bounds.left + 1., bounds.top + 1., 22., 50.);
    CColor frameInk = style.strip;
    const auto range = [&](double low, double high, CColor ink) {
      const double top = std::min(norm, high);
      if (top > low) {
        fill(rect(inner.left, inner.bottom - inner.getHeight() * top,
                  inner.getWidth(), inner.getHeight() * (top - low)),
             ink);
        frameInk = ink;
      }
    };
    // Original calibrated Cocoa meter colors, converted to sRGB.
    range(0., .76, color(0x5fa876));
    range(.76, .92, color(0xd0aa4d));
    range(.92, 1., color(0xcb675f));
    // NSFrameRect paints one pixel INSIDE with the last active fill color.
    fill(rect(bounds.left, bounds.top, bounds.getWidth(), 1.), frameInk);
    fill(rect(bounds.left, bounds.bottom - 1., bounds.getWidth(), 1.),
         frameInk);
    fill(rect(bounds.left, bounds.top, 1., bounds.getHeight()), frameInk);
    fill(rect(bounds.right - 1., bounds.top, 1., bounds.getHeight()), frameInk);
    text(format("%u", channel + 1), rect(bounds.left, 469., 24., 13.),
         style.value, kCenterText);
  }
  void paintField() {
    const unsigned model = static_cast<unsigned>(std::clamp(value(1u), 0., 3.));
    const auto formatMode =
        s3g::sanitizeFixedBusRingFormat(static_cast<uint32_t>(value(2u)));
    const auto modelName = display(1u);
    panel(box(fieldPanel.frame), "FIELD");
    panel(box(modelLeft.frame), modelName + " I");
    panel(box(modelRight.frame), modelName + " II");
    for (unsigned i = 0; i < kGlobalParamCount; ++i)
      fieldRow(kParamDefs[i], i, fieldPanel);
    for (unsigned i = 0; i < kModelCounts[model]; ++i)
      fieldRow(kParamDefs[kModelStarts[model] + i], i >= 7 ? i - 7 : i,
               i >= 7 ? modelRight : modelLeft);
    panel(rect(18., 382., 724., 120.),
          std::string("16-LANE DELAY ACTIVITY  >  ") +
              s3g::fixedBusRingFormatName(formatMode));
    fittedText("IN " + peakText(p.inputPeak.load(std::memory_order_relaxed)) +
                   "   FOLDED OUT " +
                   peakText(p.outputPeak.load(std::memory_order_relaxed)),
               rect(514., 387., 218., 13.), style.value);
    for (unsigned i = 0; i < kOutputChannels; ++i)
      vu(i);
    text(format("all 16 delayed lanes remain live; FORMAT folds them to %u "
                "host lanes",
                s3g::fixedBusRingActiveChannels(formatMode)),
         rect(34., 487., 690., 15.), style.value);
  }
#endif
  void paint() override {
    paintTitle(peakText(p.outputPeak.load(std::memory_order_relaxed)));
#if defined(S3G_MEMORY_SPRAY_PORT)
    paintSpray();
#elif defined(S3G_MEMORY_BUFFER_PORT)
    paintBuffer();
#else
    paintField();
#endif
  }
};
} // namespace memory_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new memory_canvas::Editor(p);
}
