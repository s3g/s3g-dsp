// Keep the existing byte format, decode transactionally off the audio thread,
// and publish only after a complete successful read. The isolated candidate
// owns its DSP; live state-save never reads audio-owned parameter structs.
bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  for (uint32_t n = 0; n < paramsCount(plugin); ++n) {
    clap_param_info_t i{};
    double v = 0.;
    if (paramsGetInfo(plugin, n, &i) && paramsGetValue(plugin, i.id, &v))
      applyRawParam(*candidate, i.id, v);
  }
  return rawStateSave(&candidate->plugin, stream);
}
bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  if (!rawStateLoad(&candidate->plugin, stream))
    return false;
  std::vector<std::pair<clap_id, double>> values;
  for (uint32_t n = 0; n < paramsCount(plugin); ++n) {
    clap_param_info_t i{};
    double v = 0.;
    if (!paramsGetInfo(plugin, n, &i) ||
        !rawParamsGetValue(&candidate->plugin, i.id, &v) || !std::isfinite(v))
      return false;
    v = std::clamp(v, i.min_value, i.max_value);
    values.emplace_back(i.id, v);
  }
  auto &p = *self(plugin);
  for (auto [id, v] : values)
    p.routingValues[id].store(v, std::memory_order_release);
  if (!p.routingActive.load(std::memory_order_acquire))
    syncRoutingParameters(p);
  if (p.host && p.host->request_process)
    p.host->request_process(p.host);
  return true;
}
