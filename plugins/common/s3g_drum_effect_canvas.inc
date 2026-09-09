// Included inside fx_canvas. Plugin/state/parameter definitions stay local.
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
    return result;
  }
  void publish(clap_id id, double v) {
    for (uint32_t i = 0; i < kParamCount; ++i) {
      clap_param_info_t param{};
      paramsGetInfo(&p.plugin, i, &param);
      if (param.id == id)
        p.publishedParams[i].store(v, std::memory_order_release);
    }
  }
  void requestService() {
    s3g::clap_gui::requestParamEventService(p.host, p.hostParams);
  }
  void begin(clap_id id) {
    // Reserve an END slot before accepting BEGIN. No gesture can be left
    // open if the host temporarily stops draining its parameter events.
    if (active != CLAP_INVALID_ID || p.guiParamEvents.available() < 3u)
      return;
    if (p.guiParamEvents.push({Kind::GestureBegin, id, 0.})) {
      active = id;
      requestService();
    }
  }
  void setValue(clap_id id, double v) {
    if (active != id || p.guiParamEvents.available() < 2u)
      return;
    v = clampParamValue(id, v);
    if (p.guiParamEvents.push({Kind::Value, id, v})) {
      publish(id, v);
      requestService();
    }
  }
  void end() {
    if (active == CLAP_INVALID_ID)
      return;
    p.guiParamEvents.push({Kind::GestureEnd, active, 0.});
    active = CLAP_INVALID_ID;
    requestService();
  }
  bool set(clap_id id, double v) {
    v = clampParamValue(id, v);
    const s3g::clap_gui::ParamEvent events[]{{Kind::GestureBegin, id, 0.},
                                             {Kind::Value, id, v},
                                             {Kind::GestureEnd, id, 0.}};
    if (p.guiParamEvents.available() < 3u + (active != CLAP_INVALID_ID))
      return false;
    if (!p.guiParamEvents.pushBatch(events, 3u))
      return false;
    publish(id, v);
    requestService();
    return true;
  }
  bool applySnapshot(Plugin *snapshot, bool resetDsp = false) {
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
    if (p.guiParamEvents.available() <
        count + resetDsp + (active != CLAP_INVALID_ID))
      return false;
    if (!p.guiParamEvents.pushBatch(events.data(), count, resetDsp))
      return false;
    for (uint32_t i = 1; i < count; i += 3)
      publish(events[i].paramId, events[i].value);
    requestService();
    return true;
  }
  bool presetFile(const std::string &path, bool save) {
    // Parse/load/save on an isolated instance using the ORIGINAL state
    // codec. The live DSP is only touched by the queued audio-thread batch.
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
      result = s3g::clap_gui::portable::saveStateFile(
          &snapshot->plugin, stateExtension, path.c_str());
    } else {
      result = s3g::clap_gui::portable::loadStateFile(
          &snapshot->plugin, stateExtension, path.c_str());
      if (result)
        result = applySnapshot(snapshot.get(), true);
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
  std::string presetName = "INIT", fileError;
  clap_id active = CLAP_INVALID_ID;
  using Kind = s3g::clap_gui::ParamEventKind;
};
