// Preserve the original state bytes and camera data without drawing from or
// mutating the active audio processor on the main thread.
bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  const auto &p = *self(plugin);
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  candidate->params = snapshotEffectParams(p);
  candidate->guiViewMode = p.guiViewMode;
  candidate->guiViewAzDeg = p.guiViewAzDeg;
  candidate->guiViewElDeg = p.guiViewElDeg;
  return rawEffectStateSave(&candidate->plugin, stream);
}
bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  auto &p = *self(plugin);
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  if (!rawEffectStateLoad(&candidate->plugin, stream))
    return false;
  for (uint32_t n = 0; n < paramsCount(plugin); ++n) {
    clap_param_info_t i{};
    if (!paramsGetInfo(plugin, n, &i))
      return false;
    const double v = readEffectScalar(candidate->params, i.id);
    if (!std::isfinite(v))
      return false;
  }
  for (uint32_t n = 0; n < paramsCount(plugin); ++n) {
    clap_param_info_t i{};
    paramsGetInfo(plugin, n, &i);
    p.effectValues[i.id].store(readEffectScalar(candidate->params, i.id),
                               std::memory_order_release);
  }
  p.guiViewMode = candidate->guiViewMode;
  p.guiViewAzDeg = candidate->guiViewAzDeg;
  p.guiViewElDeg = candidate->guiViewElDeg;
  publishEffectStateBase(p, candidate->params);
  if (!p.effectActive)
    syncEffectParams(p);
  return true;
}
