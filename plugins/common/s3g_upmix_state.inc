bool finiteUpmixState(const SavedState &s) {
  const float values[] = {
      s.params.amountPercent,        s.params.rotationDegrees,
      s.params.spreadPercent,        s.params.delayMs,
      s.params.decorrelationPercent, s.params.smoothingMs,
      s.params.outputGainDb};
  for (float value : values)
    if (!std::isfinite(value))
      return false;
  for (const auto *layout : {&s.customInput, &s.customOutput})
    for (const auto &speaker : layout->speakers)
      if (!std::isfinite(speaker.azimuthDeg) ||
          !std::isfinite(speaker.elevationDeg) ||
          !std::isfinite(speaker.distance))
        return false;
  for (float value : s.manualWeights)
    if (!std::isfinite(value))
      return false;
  return true;
}
bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  auto &p = *self(plugin);
  syncUpmixModel(p);
  return p.monitorModel && encodeUpmixState(&p.monitorModel->plugin, stream);
}
bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  auto &p = *self(plugin);
  if (!p.monitorModel)
    return false;
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  syncUpmixModel(p);
  restoreUpmixModel(*candidate, captureUpmix(*p.monitorModel));
  if (!decodeUpmixState(&candidate->plugin, stream))
    return false;
  const auto state = captureUpmix(*candidate);
  if (!finiteUpmixState(state))
    return false;
  restoreUpmixModel(*p.monitorModel, state);
  for (clap_id id = 1; id <= kParamCount; ++id)
    p.monitorValues[id - 1].store(getParamValue(*p.monitorModel, id),
                                  std::memory_order_release);
  publishUpmixRouting(p);
  return true;
}
