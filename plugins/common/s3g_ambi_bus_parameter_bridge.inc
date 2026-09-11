// Atomic parameter authority for the bus/depth/order ports. GUI edits publish
// values immediately; queued events only notify the host. Never replay an old
// GUI value over newer host automation, or touch live DSP from the editor.
void publishRoutingParameters(Plugin &p) {
  for (uint32_t n = 0; n < paramsCount(&p.plugin); ++n) {
    clap_param_info_t i{};
    double v = 0.;
    if (paramsGetInfo(&p.plugin, n, &i) &&
        rawParamsGetValue(&p.plugin, i.id, &v))
      p.routingValues[i.id].store(v, std::memory_order_release);
  }
}
void syncRoutingParameters(Plugin &p) {
  for (uint32_t n = 0; n < paramsCount(&p.plugin); ++n) {
    clap_param_info_t i{};
    double old = 0.;
    if (!paramsGetInfo(&p.plugin, n, &i))
      continue;
    const double v = p.routingValues[i.id].load(std::memory_order_acquire);
    // These codecs store continuous parameters as float. Do not rebuild DSP
    // targets every block just because the GUI's double has extra precision.
    if (rawParamsGetValue(&p.plugin, i.id, &old) &&
        old != static_cast<double>(static_cast<float>(v)))
      applyRawParam(p, i.id, v);
  }
}
void flushRoutingParameters(Plugin &p, const clap_output_events_t *out) {
  s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
                                    [](clap_id, double) {});
}
bool paramsGetValue(const clap_plugin_t *plugin, clap_id id, double *value) {
  if (!value || id >= self(plugin)->routingValues.size())
    return false;
  clap_param_info_t i{};
  for (uint32_t n = 0; n < paramsCount(plugin); ++n)
    if (paramsGetInfo(plugin, n, &i) && i.id == id) {
      *value = self(plugin)->routingValues[id].load(std::memory_order_acquire);
      return true;
    }
  return false;
}
