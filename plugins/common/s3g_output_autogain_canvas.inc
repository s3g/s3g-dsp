// Literal Cocoa Output Autogain 2/4 editors; shared interaction, distinct maps.
namespace monitor_canvas {
using namespace s3g::portable_gui::canvas;
namespace foundation = s3g::portable_gui::foundation;
namespace layout = s3g::gui_layout;
#if defined(S3G_MONITOR_STEREO_PORT)
constexpr const auto &stateExtension = state;
constexpr const char *pluginName = "Output Autogain Stereo";
constexpr const char *pluginTitle = "s3g OUTPUT AUTOGAIN STEREO";
constexpr unsigned outputCount = 2;
#else
constexpr const auto &stateExtension = stateExt;
constexpr const char *pluginName = "Output Autogain Quad";
constexpr const char *pluginTitle = "s3g OUTPUT AUTOGAIN QUAD";
constexpr unsigned outputCount = 4;
#endif
constexpr clap_id preservedOutput = kParamOutputGain;
constexpr const char *initialPresetName = "INIT";
double clampParamValue(clap_id id, double value) {
  clap_param_info_t info{};
  const auto index = (id >= 1 && id <= 9 ? id - 1 : 9);
  if (index >= 9u || !std::isfinite(value))
    return 0.;
  paramsGetInfo(nullptr, index, &info);
  value = std::clamp(value, info.min_value, info.max_value);
  if (id == kParamInputChannels || (info.flags & CLAP_PARAM_IS_STEPPED))
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
    for (uint32_t i = 0; i < 9u; ++i)
      if (paramsGetInfo(&p.plugin, i, &result) && result.id == id)
        return result;
    return {};
  }
  std::string display(clap_id id) const {
    char result[64]{};
    paramsValueToText(&p.plugin, id, value(id), result, sizeof(result));
    return result;
  }
  void publish(clap_id id, double v) {
    const auto index = (id >= 1 && id <= 9 ? id - 1 : 9);
    if (index < 9u)
      p.monitorValues[index].store(v, std::memory_order_release);
  }
  void requestService() {
    s3g::clap_gui::requestParamEventService(p.host, p.monitorHostParams);
  }
  virtual bool parameterEnabled(clap_id) const { return true; }
  void service() override {
    if (active != CLAP_INVALID_ID && !parameterEnabled(active))
      end();
  }
  void begin(clap_id id) {
    // Reserve an END slot before accepting BEGIN. No gesture can be left
    // open if the host temporarily stops draining its parameter events.
    if (!parameterEnabled(id) ||
        (id >= 1 && id <= 9 ? id - 1 : 9) >= 9u || active != CLAP_INVALID_ID ||
        p.monitorGuiEvents.available() < 3u)
      return;
    if (p.monitorGuiEvents.push({Kind::GestureBegin, id, 0.})) {
      active = id;
      requestService();
    }
  }
  void setValue(clap_id id, double v) {
    // A host layout change can arrive during a drag, before the next repaint.
    if (active == id && !parameterEnabled(id)) {
      end();
      return;
    }
    if (!std::isfinite(v) || active != id ||
        p.monitorGuiEvents.available() < 2u)
      return;
    v = clampParamValue(id, v);
    publish(id, v);
    if (p.monitorGuiEvents.push({Kind::Value, id, v})) {
      requestService();
    }
  }
  void end() {
    if (active == CLAP_INVALID_ID)
      return;
    p.monitorGuiEvents.push({Kind::GestureEnd, active, 0.});
    active = CLAP_INVALID_ID;
    requestService();
  }
  bool set(clap_id id, double v) {
    if (!std::isfinite(v) || (id >= 1 && id <= 9 ? id - 1 : 9) >= 9u)
      return false;
    v = clampParamValue(id, v);
    const s3g::clap_gui::ParamEvent events[]{{Kind::GestureBegin, id, 0.},
                                             {Kind::Value, id, v},
                                             {Kind::GestureEnd, id, 0.}};
    if (p.monitorGuiEvents.available() < 3u + (active != CLAP_INVALID_ID))
      return false;
    publish(id, v);
    if (!p.monitorGuiEvents.pushBatch(events, 3u))
      return false;
    requestService();
    return true;
  }
  bool applySnapshot(Plugin *snapshot) {
    std::array<s3g::clap_gui::ParamEvent, 9u * 3u> events{};
    uint32_t count = 0;
    for (uint32_t i = 0; i < 9u; ++i) {
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
    if (p.monitorGuiEvents.available() < count + (active != CLAP_INVALID_ID))
      return false;
    for (uint32_t i = 1; i < count; i += 3) {
      events[i].value = clampParamValue(events[i].paramId, events[i].value);
      publish(events[i].paramId, events[i].value);
    }
    if (!p.monitorGuiEvents.pushBatch(events.data(), count))
      return false;
    if (snapshot)
      p.monitorResetGains.store(true, std::memory_order_release);
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
      for (uint32_t i = 0; i < 9u; ++i) {
        clap_param_info_t param{};
        paramsGetInfo(&p.plugin, i, &param);
        setParamValue(*snapshot, param.id, value(param.id));
      }
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
        s3g::gui_layout::outputUtilityTitleBand({kGuiWidth, kGuiHeight});
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
  bool parameterEnabled(clap_id id) const override {
    const auto model = static_cast<s3g::McStereoLayout>(roundedUint(value(kParamLayout)));
    const bool projection = model == s3g::McStereoLayout::SphereProjection ||
                            model == s3g::McStereoLayout::HemisphereProjection ||
                            model == s3g::McStereoLayout::CubeProjection;
    if (id == kParamDistance3d)
      return projection;
#if defined(S3G_MONITOR_STEREO_PORT)
    if (id == kParamAttenuation3d)
      return projection;
    if (id == kParamRotation)
      return model != s3g::McStereoLayout::OddEvenStereo &&
             model != s3g::McStereoLayout::CenterOut &&
             model != s3g::McStereoLayout::PairPreserving;
#endif
    // Quad rotates every layout, and ATT affects rear gain even at zero height.
    return true;
  }
  foundation::Palette sliderStyle(clap_id id) const {
    auto result = style;
    if (!parameterEnabled(id)) {
      result.label = result.value = result.text = color(0x686868);
      result.fill = color(0x424242);
    }
    return result;
  }
  double refreshPeriodMilliseconds() const override { return 1000. / 24.; }
  static constexpr auto outputPanel = layout::outputUtilityPanel(
      layout::PanelRole::Output, 42., outputCount + 2);
  static constexpr auto routingPanel =
      layout::fittedStackPanel(layout::PanelRole::Routing, outputPanel, 7u);
  static CColor gray(unsigned rgb, double alpha = 1.) {
    auto c = color(s3g::portable_gui::topology_colors::gray[rgb & 255u]);
    c.alpha = static_cast<uint8_t>(std::clamp(alpha, 0., 1.) * 255.);
    return c;
  }
  void at(const std::string &label, double x, double y, bool isLabel = false) {
    text(label, rect(x, y, 600., 15.), isLabel ? style.label : style.value);
  }
  // NSFrameRect uses the current fill color and paints inside its bounds.
  void insideFrame(CRect b, CColor ink) {
    fill(rect(b.left, b.top, b.getWidth(), 1.), ink);
    fill(rect(b.left, b.bottom - 1., b.getWidth(), 1.), ink);
    fill(rect(b.left, b.top, 1., b.getHeight()), ink);
    fill(rect(b.right - 1., b.top, 1., b.getHeight()), ink);
  }
  decltype(Plugin::params) readParams() const {
    decltype(Plugin::params) prm{};
    prm.inputChannels = roundedUint(value(1));
    prm.widthPercent = value(2);
    prm.rotationDegrees = value(3);
    prm.autogain = static_cast<s3g::McStereoAutogain>(roundedUint(value(4)));
    prm.outputGainDb = value(5);
    prm.layout = static_cast<s3g::McStereoLayout>(roundedUint(value(6)));
    prm.layoutWeightPercent = value(7);
    prm.attenuation3dPercent = value(8);
    prm.distance3dPercent = value(9);
    return sanitizeParams(prm);
  }
  void row(clap_id id, const char *name, const layout::Panel &panel,
           unsigned index) {
    const bool enabled = parameterEnabled(id);
    const double y = layout::rowY(panel, index);
    const double x = layout::processorControlX(panel.frame.x);
    const auto label = rect(layout::processorLabelX(panel.frame.x), y - 2.,
                            x - panel.frame.x - 20., 15.);
    const auto param = info(id);
    if (id == kParamAutogain || id == kParamLayout) {
      fittedText(name, label, style.label);
      const auto bounds = box(layout::menuBoxRect(panel, index));
      std::vector<std::string> items;
      const unsigned count = id == kParamAutogain ? 3 : 8;
      for (unsigned i = 0; i < count; ++i)
        items.push_back(
            upper(id == kParamAutogain ? autogainName(i) : layoutName(i)));
      foundation::drawMenuBox(*context, bounds, upper(display(id)), font,
                              style);
      hit(bounds, [this, id, bounds, items] {
        openPopup(
            bounds, items, roundedUint(value(id)),
            [this, id](int i) { set(id, i); }, 1,
            rect(bounds.left, bounds.bottom + 3., bounds.getWidth(),
                 items.size() * 18.),
            18.);
      });
      return;
    }
    const auto track =
        rect(x, y + 1., layout::processorTrackWidth(panel.frame.width), 9.);
    const auto readout =
        rect(layout::processorValueX(panel.frame.x, panel.frame.width), y - 2.,
             layout::kStandardMetrics.processorValueWidth, 15.);
    const double normalized = (value(id) - param.min_value) /
                              (param.max_value - param.min_value);
    const auto rowStyle = sliderStyle(id);
    fittedText(name, label, rowStyle.label);
    foundation::drawHorizontalSlider(*context, track, normalized,
                                     track.top - 2., 13., rowStyle);
    const auto formatted =
        !enabled
            ? std::string("OFF")
            : id == kParamRotation ? format("%+.0f°", value(id)) : display(id);
    fittedText(foundation::sliderValueTextToFit(*context, formatted,
                                                readout.getWidth(), font),
               readout, rowStyle.value, kRightText);
    drag(
        box(layout::sliderHitRect(panel, index)),
        [this, id, track](CPoint point) {
          setValue(id, fromNormalized(id, (point.x - track.left) /
                                              track.getWidth()));
        },
        [this, id] { begin(id); }, [this] { end(); },
        [this, id] {
          if (parameterEnabled(id))
            set(id, info(id).default_value);
        }, enabled);
  }
  float peak(unsigned channel) const {
#if defined(S3G_MONITOR_STEREO_PORT)
    return (channel ? p.outputPeakRight : p.outputPeakLeft)
        .load(std::memory_order_relaxed);
#else
    return p.outputPeaks[channel].load(std::memory_order_relaxed);
#endif
  }
  void meters() {
    const char *names[] = {"L", "R", "RB", "LB"};
    for (unsigned ch = 0; ch < outputCount; ++ch) {
#if defined(S3G_MONITOR_STEREO_PORT)
      auto &stored = ch ? p.outputPeakRight : p.outputPeakLeft;
#else
      auto &stored = p.outputPeaks[ch];
#endif
      // Match Cocoa's per-render decay. CAS preserves a newer audio peak.
      float pk = stored.load(std::memory_order_relaxed);
      while (!stored.compare_exchange_weak(pk, pk * .92f,
                                           std::memory_order_relaxed)) {
      }
      const double db = 20. * std::log10(std::max(.000001f, pk));
      const double norm = std::clamp((db + 60.) / 60., 0., 1.);
      const double y = layout::rowY(outputPanel, 1u) + 24. + ch * 22.;
      const auto b = rect(outputPanel.frame.x + 42., y,
                          outputPanel.frame.width - 110., 15.);
      fill(b, style.strip);
      fill(rect(b.left + 1., b.top + 1., (b.getWidth() - 2.) * norm, 13.),
           style.fill);
      insideFrame(b, style.fill);
      at(names[ch], outputPanel.frame.x + 16., y - 1.);
      fittedText(format("%+4.1f", db),
                 rect(outputPanel.frame.x + outputPanel.frame.width - 52.,
                      y - 1., 40., 15.),
                 style.value, kRightText);
    }
  }
#if defined(S3G_MONITOR_STEREO_PORT)
  void map(const s3g::McStereoParams &prm) {
    const unsigned count = prm.inputChannels;
    const unsigned model = static_cast<unsigned>(prm.layout);
    const auto mapPanel = box(layout::kOutputUtilityFamilyLayout.fieldPanel);
    panel(mapPanel, "DOWNMIX MAP");
    fittedText(format("%u IN / %s / W %.0f%% / D %.0f%% / ROT %.0f / %s", count,
                      layoutName(model), double(prm.widthPercent),
                      double(prm.distance3dPercent),
                      double(prm.rotationDegrees),
                      autogainName(static_cast<unsigned>(prm.autogain))),
               rect(mapPanel.left + 132., mapPanel.top + 5.,
                    mapPanel.getWidth() - 144., 15.),
               style.value, kRightText);
    const auto field = box(layout::kOutputUtilityFamilyLayout.field);
    fill(field, gray(0x101010));
    const double leftX = field.left + 54., rightX = field.right - 54.;
    const double speakerY = field.bottom - 46.;
    const double mapLeft = field.left + 74., mapW = field.getWidth() - 148.;
    const double centerX = field.left + field.getWidth() * .5,
                 mapY = field.top + 120.;
    const bool projection = model >= 5 && model <= 7;
    fill(rect(leftX - 15., speakerY - 15., 30., 30.), gray(0x2e2e2e));
    fill(rect(rightX - 15., speakerY - 15., 30., 30.), gray(0x2e2e2e));
    at("L", leftX - 4., speakerY - 7., true);
    at("R", rightX - 4., speakerY - 7., true);
    line({mapLeft, mapY}, {mapLeft + mapW, mapY}, gray(0x747474, .25));
    line({centerX, mapY - 28.}, {centerX, mapY + 28.}, gray(0x747474, .18));
    if (projection) {
      line({mapLeft, mapY - 62.}, {mapLeft + mapW, mapY - 62.},
           gray(0x747474, .12));
      line({mapLeft, mapY + 62.}, {mapLeft + mapW, mapY + 62.},
           gray(0x747474, .12));
      at("HIGH", mapLeft - 42., mapY - 69.);
      at("MID", mapLeft - 35., mapY - 7.);
      at("LOW", mapLeft - 35., mapY + 55.);
    }
    const auto positionInfo = [&](unsigned i) {
      if (prm.layout == s3g::McStereoLayout::SphereProjection)
        return s3g::spherePosition(i, count);
      if (prm.layout == s3g::McStereoLayout::HemisphereProjection)
        return s3g::hemispherePosition(i, count);
      if (prm.layout == s3g::McStereoLayout::CubeProjection)
        return s3g::cubePosition(i);
      return s3g::ringPosition(i, count, 0.f, 0.f);
    };
    for (unsigned i = 0; i < count; ++i) {
      const auto pg = s3g::panGainForChannel(i, count, prm);
      auto reference = prm;
      reference.layoutWeightPercent = 100.f;
      reference.attenuation3dPercent = 0.f;
      reference.distance3dPercent = 100.f;
      const auto refPg = s3g::panGainForChannel(i, count, reference);
      const double srcX = mapLeft + ((pg.pan + 1.f) * .5f) * mapW;
      const double refX = mapLeft + ((refPg.pan + 1.f) * .5f) * mapW;
      double srcY = mapY - 24. + (i % 3u) * 18.;
      if (projection) {
        const auto pos = positionInfo(i);
        const double azr =
            (pos.azimuthDegrees + prm.rotationDegrees) * M_PI / 180.;
        const double frontness = (std::cos(azr) + 1.) * .5;
        const double distanceScale =
            s3g::clampf(prm.distance3dPercent / 100.f, 0.f, 2.f);
        const double baseY = -double(pos.elevationDegrees / 90.f) * 62.f;
        const double rearDrop = (1. - frontness) * 16.;
        srcY = mapY + (baseY + rearDrop) * distanceScale;
      }
      const float theta = (pg.pan + 1.f) * float(M_PI) / 4.f;
      const float leftGain = std::cos(theta), rightGain = std::sin(theta);
      const double dot = count <= 16 ? 5. : count <= 32 ? 4. : 3.1;
      const double visibleGain = std::clamp(double(pg.gain), .15, 1.);
      ellipse(rect(refX - dot - 2., srcY - dot - 2., (dot + 2.) * 2.,
                   (dot + 2.) * 2.),
              gray(0xd8d8d8, .18), false);
      if (std::abs(refX - srcX) > 2.)
        line({refX, srcY}, {srcX, srcY}, gray(0xd8d8d8, .18));
      line({srcX, srcY + 5.}, {leftX, speakerY - 16.},
           gray(0xbdbdbd, .14 + .34 * leftGain * visibleGain));
      line({srcX, srcY + 5.}, {rightX, speakerY - 16.},
           gray(0xe4e4e4, .12 + .34 * rightGain * visibleGain));
      fill(rect(srcX - dot * visibleGain, srcY - dot * visibleGain,
                dot * 2. * visibleGain, dot * 2. * visibleGain),
           style.fill);
      if (shouldLabel(i, count))
        at(format("%u", i + 1), srcX + 6., srcY - 8.);
    }
    at("L", mapLeft - 7., mapY + 39.);
    at("C", centerX - 4., mapY + 39.);
    at("R", mapLeft + mapW - 7., mapY + 39.);
    const double railY = field.bottom - 92.;
    at("WEIGHT", mapLeft, railY - 22.);
    rail(rect(mapLeft, railY - 6., mapW * .44, 7.),
         prm.layoutWeightPercent / 100.f);
    at("3D ATT", mapLeft + mapW * .53, railY - 22.);
    rail(rect(mapLeft + mapW * .53, railY - 6., mapW * .44, 7.),
         prm.attenuation3dPercent / 100.f);
    at("3D DIST", mapLeft, railY + 11.);
    rail(rect(mapLeft + 62., railY + 15., mapW * .32, 7.),
         prm.distance3dPercent / 200.f);
    const float gain = s3g::autogainForMode(prm.autogain, count) *
                       s3g::mcStereoDbToGain(prm.outputGainDb);
    at(format("ACTIVE %u / BASE GAIN %.3f / ZERO UNMAPPED OUTPUTS IN REAPER",
              count, gain),
       28., 488.);
  }
#else
  void map(const s3g::McQuadParams &prm) {
    const unsigned count = prm.inputChannels;
    const auto mapPanel = box(layout::kOutputUtilityFamilyLayout.fieldPanel);
    panel(mapPanel, "QUAD MAP");
    fittedText(format("%u IN / %s / W %.0f%% / ROT %.0f / %s", count,
                      layoutName(static_cast<unsigned>(prm.layout)),
                      double(prm.widthPercent), double(prm.rotationDegrees),
                      autogainName(static_cast<unsigned>(prm.autogain))),
               rect(mapPanel.left + 132., mapPanel.top + 5.,
                    mapPanel.getWidth() - 144., 15.),
               style.value, kRightText);
    const auto field = box(layout::kOutputUtilityFamilyLayout.field);
    fill(field, gray(0x101010));
    const double cx = field.left + field.getWidth() * .5,
                 cy = field.top + field.getHeight() * .5;
    const double radius = std::min(field.getWidth(), field.getHeight()) * .39;
    const CPoint speakers[] = {{cx - radius * .72, cy - radius * .72},
                               {cx + radius * .72, cy - radius * .72},
                               {cx + radius * .72, cy + radius * .72},
                               {cx - radius * .72, cy + radius * .72}};
    const char *names[] = {"L", "R", "RB", "LB"};
    line({cx - radius, cy}, {cx + radius, cy}, gray(0x747474, .18));
    line({cx, cy - radius}, {cx, cy + radius}, gray(0x747474, .18));
    ellipse(rect(cx - radius, cy - radius, radius * 2., radius * 2.),
            gray(0x747474, .18), false);
    for (unsigned i = 0; i < 4; ++i) {
      fill(rect(speakers[i].x - 16., speakers[i].y - 16., 32., 32.),
           gray(0x2e2e2e));
      text(names[i], rect(speakers[i].x - 16., speakers[i].y - 7., 32., 15.),
           style.label, kCenterText);
    }
    for (unsigned i = 0; i < count; ++i) {
      const auto pos = s3g::mcQuadPositionForChannel(i, count, prm);
      const double az =
          (pos.azimuthDegrees + prm.rotationDegrees - 90.) * M_PI / 180.;
      const double height =
          std::clamp(std::abs(pos.elevationDegrees) / 90., 0., 1.);
      const double dotRadius = radius * (.62 + height * .28);
      const double x = cx + std::cos(az) * dotRadius,
                   y = cy + std::sin(az) * dotRadius;
      const auto gains = s3g::quadGainsForChannel(i, count, prm);
      const float values[] = {gains.left, gains.right, gains.rightBack,
                              gains.leftBack};
      float maximum = 0.f;
      for (float g : values)
        maximum = std::max(maximum, std::abs(g));
      const double dot = count <= 16 ? 5. : count <= 32 ? 4. : 3.;
      const double visible = std::clamp(double(maximum), .16, 1.);
      for (unsigned sp = 0; sp < 4; ++sp)
        line(
            {x, y}, speakers[sp],
            gray(sp < 2 ? 0xd8d8d8 : 0xb8b8b8,
                 .08 + .30 * std::clamp(double(std::abs(values[sp])), 0., 1.)));
      ellipse(rect(x - dot * visible, y - dot * visible, dot * 2. * visible,
                   dot * 2. * visible),
              style.fill);
      if (shouldLabel(i, count))
        at(format("%u", i + 1), x + 6., y - 8.);
    }
    at("L R RB LB", 42., 488., true);
    at("output order", 110., 488.);
    at("WGT", 300., 486.);
    rail(rect(334., 489., 86., 7.), prm.layoutWeightPercent / 100.f);
    at("3D", 436., 486.);
    rail(rect(462., 489., 72., 7.), prm.attenuation3dPercent / 100.f);
  }
#endif
  static bool shouldLabel(unsigned i, unsigned count) {
    const unsigned interval =
        count <= 16 ? 1 : count <= 32 ? 4 : count <= 64 ? 8 : 16;
    return i == 0 || i + 1 == count || i % interval == 0;
  }
  void rail(CRect b, double norm) {
    insideFrame(b, style.fill);
    fill(rect(b.left + 1., b.top + 1.,
              (b.getWidth() - 2.) * std::clamp(norm, 0., 1.),
              b.getHeight() - 2.),
         style.fill);
  }
  void paint() override {
    const auto prm = readParams();
    float maximum = 0.f;
    for (unsigned ch = 0; ch < outputCount; ++ch)
      maximum = std::max(maximum, peak(ch));
    paintTitle(peakText(maximum));
    map(prm);
    panel(box(outputPanel.frame), "OUTPUT");
    panel(box(routingPanel.frame), "ROUTING");
    row(kParamOutputGain, "OUT", outputPanel, 0);
    row(kParamAutogain, "AGN", outputPanel, 1);
    meters();
    row(kParamInputChannels, "IN", routingPanel, 0);
    row(kParamWidth, "WIDTH", routingPanel, 1);
    row(kParamRotation, "ROT", routingPanel, 2);
    row(kParamLayout, "LAY", routingPanel, 3);
    row(kParamLayoutWeight, "WGT", routingPanel, 4);
    row(kParamAttenuation3d, "ATT", routingPanel, 5);
    row(kParamDistance3d, "DST", routingPanel, 6);
  }
};
} // namespace monitor_canvas
s3g::portable_gui::foundation::ContentView *makePortableCanvas(Plugin &p) {
  return new monitor_canvas::Editor(p);
}
