// The GUI publishes controls; only process/flush touches the audio gain ramp.
void monitorPublishAll(Plugin &p) {
  for (clap_id id = 1; id <= 9; ++id)
    p.monitorValues[id - 1].store(getParamValue(p, id),
                                  std::memory_order_release);
}
bool monitorFiniteParams(const decltype(Plugin::params) &prm) {
  return std::isfinite(prm.widthPercent) &&
         std::isfinite(prm.rotationDegrees) &&
         std::isfinite(prm.outputGainDb) &&
         std::isfinite(prm.layoutWeightPercent) &&
         std::isfinite(prm.attenuation3dPercent) &&
         std::isfinite(prm.distance3dPercent);
}
void monitorSync(Plugin &p) {
  for (clap_id id = 1; id <= 9; ++id) {
    double requested = p.monitorValues[id - 1].load(std::memory_order_acquire);
    if (requested == getParamValue(p, id))
      continue;
    setParamValue(p, id, requested, false);
    p.monitorValues[id - 1].compare_exchange_strong(
        requested, getParamValue(p, id), std::memory_order_acq_rel);
  }
  if (p.monitorResetGains.exchange(false, std::memory_order_acq_rel)) {
    p.gainsDirty = true;
#if defined(S3G_MONITOR_STEREO_PORT)
    p.gainsInitialized = false;
    p.gainRampRemaining = p.gainRampTotal = 0;
#endif
  }
}
void serviceMonitorGui(Plugin &p, const clap_output_events_t *out) {
  monitorSync(p);
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
