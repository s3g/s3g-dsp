// Requires rawParamsGetValue, paramsCount/GetInfo, and applyParam. Include
// after the original parameter implementation. GUI edits reach DSP through
// process/flush; init and the existing host state-load path publish separately.
void publishRoutingParameters(Plugin &p) {
  for (uint32_t n = 0; n < paramsCount(&p.plugin); ++n) {
    clap_param_info_t info{};
    double value = 0.;
    if (paramsGetInfo(&p.plugin, n, &info) &&
        info.id < p.routingValues.size() &&
        rawParamsGetValue(&p.plugin, info.id, &value))
      p.routingValues[info.id].store(value, std::memory_order_release);
  }
}
void flushRoutingParameters(Plugin &p, const clap_output_events_t *out) {
  s3g::clap_gui::serviceParamEvents(
      p.guiParamEvents, out,
      [&](clap_id id, double v) { applyParam(p, id, v); });
}
bool paramsGetValue(const clap_plugin_t *plugin, clap_id id, double *value) {
  if (!value || id >= self(plugin)->routingValues.size())
    return false;
  clap_param_info_t info{};
  for (uint32_t n = 0; n < paramsCount(plugin); ++n)
    if (paramsGetInfo(plugin, n, &info) && info.id == id) {
      *value = self(plugin)->routingValues[id].load(std::memory_order_acquire);
      return true;
    }
  return false;
}
