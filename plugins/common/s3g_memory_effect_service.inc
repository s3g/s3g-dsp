// Included after the original parameter definitions. Only process/flush (or
// inactive activation) copies the GUI's atomic controls into audio-owned DSP.
uint32_t memoryParamIndex(clap_id id) {
  for (uint32_t i = 0; i < kParamCount; ++i)
    if (kParamDefs[i].id == id)
      return i;
  return kParamCount;
}
double memoryValue(const Plugin &p, clap_id id) {
  const auto index = memoryParamIndex(id);
  return index < kParamCount
             ? p.memoryValues[index].load(std::memory_order_acquire)
             : 0.;
}
void memoryPublishParam(Plugin &p, clap_id id) {
  const auto index = memoryParamIndex(id);
  if (index < kParamCount)
    p.memoryValues[index].store(memoryRawValue(p, id),
                                std::memory_order_release);
}
void memoryPublishAll(Plugin &p) {
  for (const auto &def : kParamDefs)
    memoryPublishParam(p, def.id);
#if defined(S3G_MEMORY_SPRAY_PORT)
  p.memoryDamage.store(p.params.damage, std::memory_order_release);
  p.memoryRepeat.store(p.params.repeat, std::memory_order_release);
#endif
}
void memoryCopyToRaw(Plugin &destination, const Plugin &source) {
  for (const auto &def : kParamDefs)
    applyParam(destination, def.id, memoryValue(source, def.id), false);
#if defined(S3G_MEMORY_SPRAY_PORT)
  destination.params.damage =
      source.memoryDamage.load(std::memory_order_acquire);
  destination.params.repeat =
      source.memoryRepeat.load(std::memory_order_acquire);
  destination.spray.setParams(destination.params);
  destination.params = destination.spray.params();
#endif
}
void memorySync(Plugin &p) {
  for (uint32_t i = 0; i < kParamCount; ++i) {
    const auto id = kParamDefs[i].id;
    double requested = p.memoryValues[i].load(std::memory_order_acquire);
    if (requested == memoryRawValue(p, id))
      continue;
    applyParam(p, id, requested, false);
    // Reflect the original engine's sanitization without overwriting a more
    // recent GUI publication made while this audio block was being serviced.
    p.memoryValues[i].compare_exchange_strong(requested, memoryRawValue(p, id),
                                              std::memory_order_acq_rel);
  }
#if defined(S3G_MEMORY_SPRAY_PORT)
  p.params.damage = p.memoryDamage.load(std::memory_order_acquire);
  p.params.repeat = p.memoryRepeat.load(std::memory_order_acquire);
  p.spray.setParams(p.params);
#endif
}
void serviceMemoryGui(Plugin &p, const clap_output_events_t *out) {
  memorySync(p);
  s3g::clap_gui::MemoryEffectEvent pending{};
  while (p.memoryGuiEvents.peek(pending)) {
    using Action = s3g::clap_gui::MemoryEffectAction;
    if (pending.action != Action::Parameter) {
#if defined(S3G_MEMORY_BUFFER_PORT)
      if (pending.action == Action::Capture)
        p.processor.captureMemory();
      if (pending.action == Action::Clear)
        p.processor.clearMemory();
#endif
    } else {
      // Values already reached the DSP through the bank. Never replay old
      // edits after host-output backpressure or newer host automation. Drop
      // superseded notifications but retain their balanced BEGIN/END pair.
      const bool superseded =
          pending.kind == s3g::clap_gui::ParamEventKind::Value &&
          pending.value != memoryValue(p, pending.paramId);
      if (!superseded && !s3g::clap_gui::pushParamEvent(out, pending))
        break;
    }
    p.memoryGuiEvents.pop();
  }
}
