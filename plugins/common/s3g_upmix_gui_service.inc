SavedState captureUpmix(const Plugin &p) {
  return {kStateVersion,
          p.params,
          p.dsp.customInputLayout(),
          p.dsp.customOutputLayout(),
          p.dsp.manualRoutesActive() ? 1u : 0u,
          p.dsp.manualWeights(),
          static_cast<uint32_t>(p.autoRowShape),
          static_cast<uint32_t>(p.normalization)};
}
void applyUpmixRouting(Plugin &p, const SavedState &state) {
  p.dsp.setCustomInputLayout(state.customInput);
  p.dsp.setCustomOutputLayout(state.customOutput);
  p.dsp.setManualWeights(state.manualWeights, state.manualRoutesActive != 0u);
}
void restoreUpmixModel(Plugin &p, const SavedState &state) {
  p.params = state.params;
  p.autoRowShape = static_cast<s3g::FormatUpscaleRowShape>(state.autoRowShape);
  p.normalization =
      static_cast<s3g::FormatUpscaleNormalization>(state.normalization);
  applyUpmixRouting(p, state);
  applyParams(p);
}
bool initializeUpmixGui(Plugin &p) {
  p.monitorModel.reset(new (std::nothrow) Plugin());
  if (!p.monitorModel)
    return false;
  restoreUpmixModel(*p.monitorModel, captureUpmix(p));
  p.monitorModel->plugin.plugin_data = p.monitorModel.get();
  for (clap_id id = 1; id <= kParamCount; ++id)
    p.monitorValues[id - 1].store(getParamValue(p, id),
                                  std::memory_order_relaxed);
  return true;
}
void syncUpmixModel(Plugin &p) {
  if (!p.monitorModel)
    return;
  // A host can leave and return to Stereo between GUI ticks. Retain the
  // intermediate format change's destructive polarity constraint too.
  if (p.monitorClearNegative.exchange(false, std::memory_order_acq_rel)) {
    auto weights = p.monitorModel->dsp.manualWeights();
    bool changed = false;
    for (float &value : weights)
      if (value < 0.f) {
        value = 0.f;
        changed = true;
      }
    if (changed)
      p.monitorModel->dsp.setManualWeights(
          weights, p.monitorModel->dsp.manualRoutesActive());
  }
  for (clap_id id = 1; id <= kParamCount; ++id) {
    const double requested =
        p.monitorValues[id - 1].load(std::memory_order_acquire);
    if (requested != getParamValue(*p.monitorModel, id))
      setParamValue(*p.monitorModel, id, requested, false);
  }
}
void publishUpmixRouting(Plugin &p) {
  p.monitorRouting[p.monitorWriteSlot] = captureUpmix(*p.monitorModel);
  p.monitorWriteSlot = p.monitorMiddleSlot.exchange(p.monitorWriteSlot | 4u,
                                                    std::memory_order_acq_rel) &
                       3u;
}
void syncUpmixAudio(Plugin &p) {
  if (p.monitorMiddleSlot.load(std::memory_order_acquire) & 4u) {
    p.monitorReadSlot = p.monitorMiddleSlot.exchange(
                            p.monitorReadSlot, std::memory_order_acq_rel) &
                        3u;
    applyUpmixRouting(p, p.monitorRouting[p.monitorReadSlot]);
  }
  // Parameters always come from the current bank, never an older matrix
  // transaction. Host automation cannot be replayed by a delayed GUI edit.
  for (clap_id id = 1; id <= kParamCount; ++id) {
    double requested = p.monitorValues[id - 1].load(std::memory_order_acquire);
    if (requested == getParamValue(p, id))
      continue;
    setParamValue(p, id, requested, false);
    p.monitorValues[id - 1].compare_exchange_strong(
        requested, getParamValue(p, id), std::memory_order_acq_rel);
  }
  constrainManualPolarityToStereo(p);
}
void serviceUpmixGui(Plugin &p, const clap_output_events_t *out) {
  syncUpmixAudio(p);
  s3g::clap_gui::ParamEvent event{};
  while (p.monitorGuiEvents.peek(event)) {
    const bool superseded =
        event.kind == s3g::clap_gui::ParamEventKind::Value &&
        event.value !=
            p.monitorValues[event.paramId - 1].load(std::memory_order_acquire);
    if (!superseded && !s3g::clap_gui::pushParamEvent(out, event))
      break;
    p.monitorGuiEvents.pop();
  }
}
