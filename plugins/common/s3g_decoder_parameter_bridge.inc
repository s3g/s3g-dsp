// Scalar UI publication. The audio processor alone applies its live params;
// the editor renders a separate model. Original unexposed state fields are
// retained in immutable main-thread-owned records until plugin destruction.
bool decoderIsParam(clap_id id) { return id > 0 && id <= paramsCount(nullptr); }
void publishDecoderParams(Plugin &p) {
  for (clap_id id = 1; decoderIsParam(id); ++id)
    p.decoderValues[id].store(readDecoderScalar(p.params, id),
                              std::memory_order_release);
  p.decoderLayoutCount.store(decoderHiddenCount(p.params),
                             std::memory_order_release);
}
DecoderParams snapshotDecoderParams(const Plugin &p) {
  auto params = p.decoderStateBase;
  for (clap_id id = 1; decoderIsParam(id); ++id)
    setDecoderScalar(params, id,
                     p.decoderValues[id].load(std::memory_order_acquire));
  setDecoderHiddenCount(params,
                        p.decoderLayoutCount.load(std::memory_order_acquire));
  return params;
}
void applyParam(Plugin &p, clap_id id, double value) {
  if (!decoderIsParam(id) || !std::isfinite(value))
    return;
  rawDecoderApplyParam(p, id, value);
  p.decoderValues[id].store(readDecoderScalar(p.params, id),
                            std::memory_order_release);
  if (decoderHasLayoutCount)
    p.decoderLayoutCount.store(decoderHiddenCount(p.params),
                               std::memory_order_release);
}
void syncDecoderParams(Plugin &p) {
  const auto *pending = p.decoderPendingState.load(std::memory_order_acquire);
  const bool changed = pending && pending != p.decoderAppliedState;
  if (changed)
    p.params = *pending;
  for (clap_id id = 1; decoderIsParam(id); ++id) {
    const auto value = p.decoderValues[id].load(std::memory_order_acquire);
    if (readDecoderScalar(p.params, id) != value)
      rawDecoderApplyParam(p, id, value);
  }
  const auto count = p.decoderLayoutCount.load(std::memory_order_acquire);
  const bool countChanged =
      decoderHasLayoutCount && decoderHiddenCount(p.params) != count;
  setDecoderHiddenCount(p.params, count);
  if (changed || countChanged)
    refreshPortableDecoder(p);
  if (changed)
    p.decoderAppliedState = pending;
}
void publishDecoderStateBase(Plugin &p, const DecoderParams &params) {
  p.decoderStateBase = params;
  auto record = std::make_unique<DecoderParams>(params);
  const auto *pointer = record.get();
  p.decoderLoadedStates.push_back(std::move(record));
  p.decoderPendingState.store(pointer, std::memory_order_release);
}
void serviceDecoderGui(Plugin &p, const clap_output_events_t *out) {
  s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
                                    [](clap_id, double) {});
}
