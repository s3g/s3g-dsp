// Capture editors share the parameter queue, but command edges are latched
// separately so a CAPTURE 1/0 pair cannot disappear through coalescing.
uint32_t paramsCount(const clap_plugin_t *);
bool paramsGetInfo(const clap_plugin_t *, uint32_t, clap_param_info_t *);
void publishEffectParams(Plugin &p) {
  for (clap_id id = 1; id < p.effectValues.size(); ++id) {
    if (isParam(id))
      p.effectValues[id].store(readEffectScalar(p.params, id),
                               std::memory_order_release);
  }
}
EffectParams snapshotEffectParams(const Plugin &p) {
  EffectParams result{};
  for (clap_id id = 1; id < p.effectValues.size(); ++id) {
    if (isParam(id))
      setEffectScalar(result, id,
                      p.effectValues[id].load(std::memory_order_acquire));
  }
  return result;
}
double getParam(const Plugin &p, clap_id id) {
  return isParam(id) ? p.effectValues[id].load(std::memory_order_acquire) : 0.;
}
void applyParam(Plugin &p, clap_id id, double v, bool defer = false) {
  if (!isParam(id) || !std::isfinite(v))
    return;
#if defined(S3G_PARTIAL_TRACE)
  applyRawEffectParam(p, id, v, defer);
#else
  (void)defer;
  applyRawEffectParam(p, id, v);
#endif
  p.effectValues[id].store(readEffectScalar(p.params, id),
                           std::memory_order_release);
}
void syncEffectParams(Plugin &p) {
  for (clap_id id = 1; id < p.effectValues.size(); ++id) {
    if (!isParam(id))
      continue;
    const double v = p.effectValues[id].load(std::memory_order_acquire);
    if (readEffectScalar(p.params, id) != v)
      applyRawEffectParam(p, id, v);
  }
}
void flushEffectGui(Plugin &p, const clap_output_events_t *out) {
  s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
                                    [](clap_id, double) {});
  syncEffectParams(p);
}
