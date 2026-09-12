constexpr clap_id kPortableVoxNotify = 0x40000000u;
void publishVoxParams(Plugin &p) {
  p.publishedSnapshot.store(
      {p.params, p.vox, p.lyric, loadScore(p), p.popFilter});
  for (const auto &def : kParams) {
    double value = 0.;
    if (ownedVoxParamValue(&p.plugin, def.id, &value) &&
        def.id < p.publishedParams.size())
      p.publishedParams[def.id].store(value, std::memory_order_release);
  }
}
bool paramsGetValue(const clap_plugin_t *plugin, clap_id id, double *value) {
  if (!value)
    return false;
  const auto *p = self(plugin);
  for (const auto &def : kParams)
    if (def.id == id) {
      *value = p->publishedParams[id].load(std::memory_order_acquire);
      return true;
    }
  return false;
}
VoxPresetSnapshot visibleVoxSnapshot(const Plugin &p) {
  auto snapshot = makeVoxDefaultSnapshot();
  p.publishedSnapshot.load(snapshot);
  for (const auto &def : kParams)
    assignVoxPresetParameter(
        snapshot, def.id,
        p.publishedParams[def.id].load(std::memory_order_acquire));
  snapshot.score = loadScore(p);
  return snapshot;
}
bool queuePortableVoxPreset(Plugin &p, const VoxPresetSnapshot &preset) {
  std::vector<s3g::clap_gui::ParamEvent> events;
  events.push_back({s3g::clap_gui::ParamEventKind::Value, CLAP_INVALID_ID, 0.});
  // Reuse the exact host parameter mapping without touching the live engine.
  auto mapping = std::make_unique<Plugin>();
  mapping->plugin.plugin_data = mapping.get();
  mapping->params = preset.params;
  mapping->vox = preset.vox;
  mapping->lyric = preset.lyric;
  mapping->popFilter = preset.popFilter;
  for (const auto &def : kParams) {
    if (isLegacyHiddenParam(def.id))
      continue;
    double value = 0.;
    if (!ownedVoxParamValue(&mapping->plugin, def.id, &value))
      continue;
    events.push_back({s3g::clap_gui::ParamEventKind::GestureBegin, def.id, 0.});
    events.push_back({s3g::clap_gui::ParamEventKind::Value,
                      def.id | kPortableVoxNotify, value});
    events.push_back({s3g::clap_gui::ParamEventKind::GestureEnd, def.id, 0.});
  }
  if (p.guiParamEvents.available() < events.size() ||
      !p.presetMailbox.push(preset))
    return false;
  p.guiParamEvents.pushBatch(events.data(),
                             static_cast<uint32_t>(events.size()));
  for (const auto &def : kParams) {
    double value = 0.;
    if (ownedVoxParamValue(&mapping->plugin, def.id, &value))
      p.publishedParams[def.id].store(value, std::memory_order_release);
  }
  storeScore(p, preset.score);
  s3g::clap_gui::requestParamEventService(p.host, p.hostParams);
  return true;
}
void servicePortableVox(Plugin &p, const clap_output_events_t *output) {
  if (p.sourceResetPending.exchange(false, std::memory_order_acq_rel)) {
    syncWorldSmoothing(p);
    resetWorldPhases(p);
  }
  s3g::clap_gui::ParamEvent event;
  while (p.guiParamEvents.peek(event)) {
    if (event.paramId == CLAP_INVALID_ID) {
      applyPendingVoxPreset(p);
      publishVoxParams(p);
      p.guiParamEvents.pop();
      continue;
    }
    const bool notification = (event.paramId & kPortableVoxNotify) != 0;
    event.paramId &= ~kPortableVoxNotify;
    if (!s3g::clap_gui::pushParamEvent(output, event))
      break;
    if (event.kind == s3g::clap_gui::ParamEventKind::Value && !notification)
      applyParam(p, event.paramId, event.value);
    p.guiParamEvents.pop();
  }
}
