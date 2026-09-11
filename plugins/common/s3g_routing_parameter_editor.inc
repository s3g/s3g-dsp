// Included in each editor namespace, after pluginName/pluginTitle,
// preservedOutput and stateExtension have been declared.
#if !defined(S3G_ROUTING_CUSTOM_ACCESS)
double routingReadValue(const Plugin &p, clap_id id) {
  return p.routingValues[id].load(std::memory_order_acquire);
}
void routingPublish(Plugin &p,
                    const std::vector<std::pair<clap_id, double>> &values) {
  for (auto [id, v] : values)
    p.routingValues[id].store(v, std::memory_order_release);
}
void routingApply(Plugin &p, clap_id id, double v) { applyParam(p, id, v); }
double routingRawValue(Plugin &p, clap_id id) {
  double v = 0.;
  rawParamsGetValue(&p.plugin, id, &v);
  return v;
}
void routingInitialize(Plugin &p) {
  p.plugin.plugin_data = &p;
  init(&p.plugin);
}
#endif
class ParameterEditor : public s3g::portable_gui::routing::Canvas {
public:
  explicit ParameterEditor(Plugin &instance)
      : Canvas(kGuiWidth, kGuiHeight), p(instance) {
    if (p.host && p.host->get_extension)
      p.hostParams = static_cast<const clap_host_params_t *>(
          p.host->get_extension(p.host, CLAP_EXT_PARAMS));
    for (unsigned n = 0; n < paramsCount(&p.plugin); ++n) {
      clap_param_info_t i{};
      if (paramsGetInfo(&p.plugin, n, &i))
        infos.push_back(i);
    }
  }
  ~ParameterEditor() override { stopRefresh(); }
  void stopRefresh() override {
    Canvas::stopRefresh();
    end();
  }
  clap_param_info_t info(clap_id id) const {
    for (auto i : infos)
      if (i.id == id)
        return i;
    return {};
  }
  bool valid(clap_id id) const {
    return std::any_of(infos.begin(), infos.end(),
                       [id](const auto &i) { return i.id == id; });
  }
  double value(clap_id id) const { return routingReadValue(p, id); }
  std::string display(clap_id id) const {
    char b[96]{};
    paramsValueToText(&p.plugin, id, value(id), b, sizeof(b));
    return b;
  }
  double canonical(clap_id id, double v) const {
    const auto i = info(id);
    v = std::clamp(v, i.min_value, i.max_value);
    return i.flags & CLAP_PARAM_IS_STEPPED ? std::round(v) : v;
  }
  void request() {
    s3g::clap_gui::requestParamEventService(p.host, p.hostParams);
  }
  bool begin(clap_id id) {
    if (!valid(id))
      return false;
    if (std::find(gestures.begin(), gestures.end(), id) != gestures.end())
      return true;
    if (p.guiParamEvents.available() < 3 + gestures.size())
      return false;
    p.guiParamEvents.push({Kind::GestureBegin, id, 0.});
    gestures.push_back(id);
    active = id;
    request();
    return true;
  }
  void end() {
    for (auto id : gestures)
      p.guiParamEvents.push({Kind::GestureEnd, id, 0.});
    if (!gestures.empty())
      request();
    gestures.clear();
    active = CLAP_INVALID_ID;
  }
  bool change(clap_id id, double v) {
    if (!std::isfinite(v) ||
        std::find(gestures.begin(), gestures.end(), id) == gestures.end() ||
        p.guiParamEvents.available() <= gestures.size())
      return false;
    v = canonical(id, v);
    routingPublish(p, {{id, v}});
    p.guiParamEvents.push({Kind::Value, id, v});
    request();
    return true;
  }
  bool batch(const std::vector<std::pair<clap_id, double>> &values) {
    if (p.guiParamEvents.available() < values.size() * 3 + gestures.size())
      return false;
    std::vector<s3g::clap_gui::ParamEvent> events;
    events.reserve(values.size() * 3);
    for (auto [id, v] : values) {
      if (!std::isfinite(v) || !valid(id) ||
          std::find(gestures.begin(), gestures.end(), id) != gestures.end())
        return false;
      v = canonical(id, v);
      events.push_back({Kind::GestureBegin, id, 0.});
      events.push_back({Kind::Value, id, v});
      events.push_back({Kind::GestureEnd, id, 0.});
    }
    std::vector<std::pair<clap_id, double>> canonicalValues;
    for (size_t n = 1; n < events.size(); n += 3)
      canonicalValues.emplace_back(events[n].paramId, events[n].value);
    routingPublish(p, canonicalValues);
    if (!p.guiParamEvents.pushBatch(events.data(),
                                    static_cast<uint32_t>(events.size())))
      return false;
    request();
    invalid();
    return true;
  }
  bool set(clap_id id, double v) { return batch({{id, v}}); }
  void resetParameters() {
    std::vector<std::pair<clap_id, double>> values;
    for (auto i : infos)
      if (i.id != preservedOutput && !(i.flags & CLAP_PARAM_IS_READONLY))
        values.emplace_back(i.id, i.default_value);
    if (batch(values))
      presetName = "INIT";
  }
  bool presetFile(const std::string &path, bool save) {
    auto candidate = std::make_unique<Plugin>();
    routingInitialize(*candidate);
    bool result = false;
    if (save) {
      for (auto i : infos)
        routingApply(*candidate, i.id, value(i.id));
      preparePresetSave(*candidate);
      result = s3g::clap_gui::portable::saveStateFile(
          &candidate->plugin, stateExtension, path.c_str());
    } else {
      result = s3g::clap_gui::portable::loadStateFile(
          &candidate->plugin, stateExtension, path.c_str());
      if (result) {
        std::vector<std::pair<clap_id, double>> values;
        for (auto i : infos)
          if (i.id != preservedOutput && !(i.flags & CLAP_PARAM_IS_READONLY)) {
            values.emplace_back(i.id, routingRawValue(*candidate, i.id));
          }
        result = batch(values);
        if (result)
          finishPresetLoad(*candidate);
      }
    }
    if (result)
      presetName =
          foundation::pathToUtf8(foundation::pathFromUtf8(path.c_str()).stem());
    return result;
  }
  virtual void preparePresetSave(Plugin &) {}
  virtual void finishPresetLoad(const Plugin &) {}
  void presetDialog(bool save) {
    foundation::FileDialogOptions o;
    o.save = save;
    o.title = std::string(save ? "Save " : "Load ") + pluginName + " Preset";
    o.extension = "s3gpreset";
    o.extensionDescription = "s3g Preset";
    o.defaultSaveName = "Preset.s3gpreset";
    o.initialDirectory = foundation::presetDirectory(pluginName);
    auto path = foundation::runFileDialog(getFrame(), o);
    if (!path.empty())
      error = presetFile(path, save) ? "" : "PRESET FILE ERROR";
  }
  void titleBand(const s3g::gui_layout::EncoderTitleBand &b,
                 std::function<void()> randomize = {},
                 const std::string &status = "") {
    foundation::drawPluginTitle(
        *context, pluginTitle,
        rect(b.titleX, 13., b.presetLabelX - b.titleX - 10., 15.), titleFont);
    text("PRESET", rect(b.presetLabelX, 13., 60., 15.), style.label);
    foundation::drawMenuBox(*context, box(b.presetMenu), upper(presetName),
                            font, style);
    hit(box(b.presetMenu), [this] { resetParameters(); });
    header(box(b.loadButton), "LOAD", [this] { presetDialog(false); });
    header(box(b.saveButton), "SAVE", [this] { presetDialog(true); });
    if (randomize)
      header(box(b.randomButton), "RANDOM", randomize);
    const float peak = p.outputPeak.load(std::memory_order_relaxed);
    const double x = (randomize ? b.randomButton.x + b.randomButton.width
                                : b.saveButton.x + b.saveButton.width) +
                     12.;
    text(error.empty() ? (status.empty()
                              ? format("PK %+4.1f",
                                       20. * std::log10(std::max(1.e-6f, peak)))
                              : status)
                       : error,
         rect(x, 13., kGuiWidth - x - b.statusRightInset, 15.), style.value,
         kRightText);
  }
  void sliderRow(const std::string &label, clap_id id,
                 const s3g::gui_layout::Panel &panel, unsigned row,
                 const std::string &shown = "") {
    const auto i = info(id);
    const double y = s3g::gui_layout::rowY(panel, row),
                 x = s3g::gui_layout::processorControlX(panel.frame.x),
                 w = s3g::gui_layout::processorTrackWidth(panel.frame.width);
    slider(
        label, shown.empty() ? display(id) : shown,
        (value(id) - i.min_value) / (i.max_value - i.min_value),
        box(x, y + 1., w, 9.), box(s3g::gui_layout::sliderHitRect(panel, row)),
        box(panel.frame.x + 16., y - 2., x - panel.frame.x - 24., 15.),
        box(x + w + 8., y - 2.,
            panel.frame.width - (x - panel.frame.x) - w - 24., 15.),
        [this, id, i](double n) {
          change(id, i.min_value + n * (i.max_value - i.min_value));
        },
        [this, id] { begin(id); }, [this] { end(); },
        [this, id, i] { set(id, i.default_value); });
  }
  void menuRow(const std::string &label, clap_id id,
               const s3g::gui_layout::Panel &panel, unsigned row,
               std::vector<std::string> items, int firstValue = 0) {
    const double y = s3g::gui_layout::rowY(panel, row),
                 x = s3g::gui_layout::processorControlX(panel.frame.x),
                 w = s3g::gui_layout::processorMenuWidth(panel.frame.width);
    text(label, rect(panel.frame.x + 16., y - 2., x - panel.frame.x - 24., 17.),
         style.label);
    menu(rect(x, y - 1., w, 17.), upper(display(id)), std::move(items),
         int(value(id)) - firstValue,
         [this, id, firstValue](int v) { set(id, v + firstValue); });
  }
  Plugin &p;
  clap_id active = CLAP_INVALID_ID;
  std::vector<clap_id> gestures;
  std::vector<clap_param_info_t> infos;
  std::string presetName = "INIT", error;
};
