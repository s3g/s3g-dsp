// Included after the original scalar mapper. Main-thread editors publish
// canonical values only; process/flush is the sole owner of live DSP updates.
using EffectParams = decltype(Plugin::params);
uint32_t paramsCount(const clap_plugin_t *);
bool paramsGetInfo(const clap_plugin_t *, uint32_t, clap_param_info_t *);
bool effectIsParam(clap_id);
void applyRawEffectParam(Plugin &, clap_id, double);
double readEffectScalar(const EffectParams &, clap_id);
void setEffectScalar(EffectParams &, clap_id, double);

void publishEffectParams(Plugin &p) {
  for (clap_id id = 1; id < p.effectValues.size(); ++id) {
    if (effectIsParam(id))
      p.effectValues[id].store(readEffectScalar(p.params, id),
                               std::memory_order_release);
  }
}
EffectParams snapshotEffectParams(const Plugin &p) {
  EffectParams params = p.effectStateBase;
  for (clap_id id = 1; id < p.effectValues.size(); ++id) {
    if (effectIsParam(id))
      setEffectScalar(params, id,
                      p.effectValues[id].load(std::memory_order_acquire));
  }
  return params;
}
double getParam(const Plugin &p, clap_id id) {
  return effectIsParam(id) ? p.effectValues[id].load(std::memory_order_acquire)
                           : 0.;
}
void applyParam(Plugin &p, clap_id id, double value) {
  if (!effectIsParam(id) || !std::isfinite(value))
    return;
  applyRawEffectParam(p, id, value);
  p.effectValues[id].store(readEffectScalar(p.params, id),
                           std::memory_order_release);
}
void syncEffectParams(Plugin &p) {
  const auto *pending = p.effectPendingState.load(std::memory_order_acquire);
  const bool newState = pending && pending != p.effectAppliedState;
  if (newState)
    p.params = *pending;
  for (clap_id id = 1; id < p.effectValues.size(); ++id) {
    if (!effectIsParam(id))
      continue;
    const double v = p.effectValues[id].load(std::memory_order_acquire);
    if (readEffectScalar(p.params, id) != v)
      applyRawEffectParam(p, id, v);
  }
  if (newState) {
    refreshEffectProcessor(p);
    p.effectAppliedState = pending;
  }
}
void publishEffectStateBase(Plugin &p, const EffectParams &params) {
  // Main-thread owned immutable state records also retain non-parameter legacy
  // fields. The audio thread never allocates, deletes, or locks these records.
  p.effectStateBase = params;
  auto state = std::make_unique<EffectParams>(params);
  const auto *pointer = state.get();
  p.effectLoadedStates.push_back(std::move(state));
  p.effectPendingState.store(pointer, std::memory_order_release);
}
void flushEffectGui(Plugin &p, const clap_output_events_t *out) {
  s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
                                    [](clap_id, double) {});
  syncEffectParams(p);
}
