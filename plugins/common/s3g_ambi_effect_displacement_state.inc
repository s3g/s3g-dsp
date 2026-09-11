void copyDisplacementPresentation(const Plugin &source, Plugin &target) {
  target.score = source.score;
  target.scoreName = source.scoreName;
  target.status = source.status;
  target.guiViewMode = source.guiViewMode;
  target.guiViewAzimuthDeg = source.guiViewAzimuthDeg;
  target.guiViewElevationDeg = source.guiViewElevationDeg;
  target.guiViewZoom = source.guiViewZoom;
}
bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  if (!stream || !stream->write)
    return false;
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  candidate->params = snapshotEffectParams(*self(plugin));
  copyDisplacementPresentation(*self(plugin), *candidate);
  return rawEffectStateSave(&candidate->plugin, stream);
}
void publishDisplacementState(Plugin &p, const Plugin &candidate) {
  copyDisplacementPresentation(candidate, p);
  for (const auto &def : kParamDefs)
    p.effectValues[def.id].store(readEffectScalar(candidate.params, def.id),
                                 std::memory_order_release);
  if (p.active)
    installRuntime(p);
  else
    syncEffectParams(p);
  p.effectResetPhase.store(true, std::memory_order_release);
  notifyParamValuesChanged(p);
}
bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  if (!stream || !stream->read)
    return false;
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  if (!rawEffectStateLoad(&candidate->plugin, stream))
    return false;
  if (!std::isfinite(candidate->guiViewAzimuthDeg))
    candidate->guiViewAzimuthDeg = 38.;
  if (!std::isfinite(candidate->guiViewElevationDeg))
    candidate->guiViewElevationDeg = 28.;
  if (!std::isfinite(candidate->guiViewZoom))
    candidate->guiViewZoom = 1.;
  publishDisplacementState(*self(plugin), *candidate);
  return true;
}
