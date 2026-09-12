// Audio-owner publication and ordered GUI commands. No drawing/platform API
// runs here. File bytes are decoded on the GUI thread before handoff.
void publishParam(Plugin& p, clap_id id, double value) {
  if (id < kPortableFaultParamSlots) p.publishedParams[id].store(value, std::memory_order_release);
}
void faultSourceStatus(const Plugin& p, char* text, size_t capacity) {
  if (!p.rawSource) {
    if (p.sourcePath.empty())
      std::snprintf(text, capacity, "SOURCE GENERATED [%s] | 64.0 KB",
          codecModeName(static_cast<uint32_t>(p.params.fieldCodecMode)));
    else
      std::snprintf(text, capacity, "%s | %s",
          p.sourceError.empty() ? "SOURCE MISSING" : p.sourceError.c_str(), p.sourceName.c_str());
  } else if (p.rawSource->waveform) {
    std::snprintf(text, capacity, "WAVE %u>8 | %.1fK %u-BIT | %s%s",
        p.rawSource->sourceChannelCount, double(p.rawSource->sourceSampleRate) / 1000.,
        p.rawSource->sourceBitsPerSample, p.sourceName.c_str(),
        p.rawSource->truncated ? " | PARTIAL" : "");
  } else {
    char count[32]{};
    const auto bytes = p.rawSource->originalByteCount;
    if (bytes >= 1024u * 1024u) std::snprintf(count, sizeof(count), "%.1f MB", double(bytes) / (1024. * 1024.));
    else if (bytes >= 1024u) std::snprintf(count, sizeof(count), "%.1f KB", double(bytes) / 1024.);
    else std::snprintf(count, sizeof(count), "%llu B", static_cast<unsigned long long>(bytes));
    std::snprintf(text, capacity, "RAW | %s | %s%s", p.sourceName.c_str(), count,
        p.rawSource->truncated ? " | FIRST 64 MB" : "");
  }
  // snprintf can stop inside a multibyte filename. Keep the published text
  // valid UTF-8, without allocating a temporary string on the audio owner.
  const auto length = std::strlen(text);
  if (length && length == capacity - 1u) {
    size_t start = length - 1u;
    while (start && (static_cast<unsigned char>(text[start]) & 0xc0u) == 0x80u) --start;
    const auto lead = static_cast<unsigned char>(text[start]);
    const size_t required = lead < 0x80u ? 1u : lead < 0xe0u ? 2u : lead < 0xf0u ? 3u : 4u;
    if (length - start < required) text[start] = 0;
  }
}
void publishFault(Plugin& p) {
  PortableFaultSnapshot snapshot{};
  snapshot.state = faultOwnerState(&p);
  snapshot.sampleRate = p.sampleRate;
  snapshot.hasSource = bool(p.rawSource);
  if (p.rawSource) snapshot.source = {p.rawSource->waveform,
      p.rawSource->sourceSampleRate, p.rawSource->sourceChannelCount, p.rawSource->sourceBitsPerSample};
  faultSourceStatus(p, snapshot.sourceStatus, sizeof(snapshot.sourceStatus));
  for (const auto& def : kParamDefs) {
    double value = 0.; faultOwnerParamValue(&p.plugin, def.id, &value); publishParam(p, def.id, value);
  }
  p.portableSnapshot.store(snapshot);
}
void requestFaultRescan(Plugin& p) {
  p.portableRescan.store(true, std::memory_order_release);
  if (p.host && p.host->request_callback) p.host->request_callback(p.host);
}
bool queuePortableFaultCommand(Plugin& p, std::unique_ptr<PortableFaultCommand> command) {
  if (!command || p.portableCommandPending.load(std::memory_order_acquire)
      || p.guiParamEvents.available() < 8u) return false;
  // The previous object is reclaimed here on the GUI thread, after the audio
  // owner has stopped using it. The plugin owns pending data across GUI close.
  p.portableCommand = std::move(command);
  p.portableCommandPending.store(true, std::memory_order_release);
  p.guiParamEvents.push({s3g::clap_gui::ParamEventKind::Value, kPortableFaultCommandId, 0.});
  const auto* params = p.host && p.host->get_extension
      ? static_cast<const clap_host_params_t*>(p.host->get_extension(p.host, CLAP_EXT_PARAMS)) : nullptr;
  s3g::clap_gui::requestParamEventService(p.host, params);
  return true;
}
bool queuePortableFaultPreset(Plugin& p, std::unique_ptr<Plugin> staged, clap_id preserve) {
  auto command = std::make_unique<PortableFaultCommand>();
  command->kind = PortableFaultCommand::Preset;
  command->staged = std::move(staged); command->preserve = preserve;
  return queuePortableFaultCommand(p, std::move(command));
}
void servicePortableFault(Plugin& p, const clap_output_events_t* output) {
  s3g::clap_gui::ParamEvent event{};
  while (p.guiParamEvents.peek(event)) {
    if (event.paramId == kPortableFaultCommandId) {
      auto& command = *p.portableCommand;
      if (command.kind == PortableFaultCommand::Import) {
        installRawSource(p, command.source, command.path, command.interpretation, true);
      } else if (command.kind == PortableFaultCommand::Algorithm) {
        applyCuratedAlgorithm(p, command.algorithm);
      } else {
        const auto& staged = *command.staged;
        const float previousGain = p.params.gainDb;
        p.params = staged.params;
        if (command.preserve == kGainParamId) p.params.gainDb = previousGain;
        p.selectedPreset = staged.selectedPreset;
        p.rawSource = staged.rawSource;
        p.sourcePath = staged.sourcePath; p.sourceName = staged.sourceName;
        p.sourceError = staged.sourceError; p.sourceInterpretation = staged.sourceInterpretation;
        p.performanceMode = staged.performanceMode; p.midiReceive = staged.midiReceive;
        p.outputFormat = staged.outputFormat; p.outputRotationDeg = staged.outputRotationDeg;
        p.attackMs = staged.attackMs; p.decayMs = staged.decayMs;
        p.sustain = staged.sustain; p.releaseMs = staged.releaseMs;
        const bool running = staged.playing.load(std::memory_order_relaxed);
        p.playing.store(running, std::memory_order_relaxed); p.runGain = running ? 1.f : 0.f;
        p.hasUndo = false; p.undoSource.reset(); p.undoSourceInterpretation = SourceInterpretation::Generated;
        resetMidiPerformance(p);
        p.field.setSource(p.rawSource); p.field.setParams(p.params); p.field.reset();
      }
      publishFault(p); requestFaultRescan(p);
      p.portableCommandPending.store(false, std::memory_order_release);
    } else {
      if (output && output->try_push && !s3g::clap_gui::pushParamEvent(output, event)) break;
      if (event.kind == s3g::clap_gui::ParamEventKind::Value) {
        applyParam(p, event.paramId, event.value);
        if (event.paramId == kPresetParamId || event.paramId == kRandomizePatchParamId
            || event.paramId == kRandomizeFieldParamId || event.paramId == kMutateParamId
            || event.paramId == kUndoParamId) requestFaultRescan(p);
      }
    }
    p.guiParamEvents.pop();
  }
}
bool refreshFaultReadout(const Plugin& p, FaultReadout& view) {
  PortableFaultSnapshot snapshot{};
  if (!p.portableSnapshot.load(snapshot)) return false;
  auto state = snapshot.state;
  for (const auto& def : kParamDefs)
    assignFaultSavedValue(state, def.id, p.publishedParams[def.id].load(std::memory_order_acquire));
  view.plugin = p.plugin; view.params = state.params; view.sampleRate = snapshot.sampleRate;
  view.selectedPreset = state.selectedPreset;
  view.performanceMode = static_cast<PerformanceMode>(state.performanceMode);
  view.outputFormat = static_cast<OutputFormat>(state.outputFormat);
  view.outputRotationDeg = state.outputRotationDeg; view.midiReceive = state.midiReceive;
  view.attackMs = state.attackMs; view.decayMs = state.decayMs;
  view.sustain = state.sustain; view.releaseMs = state.releaseMs;
  view.playing.data = state.runState != 0;
  view.outputPeak.data = p.outputPeak.load(std::memory_order_relaxed);
  view.displayNote.data = p.displayNote.load(std::memory_order_relaxed);
  view.displayEnvelope.data = p.displayEnvelope.load(std::memory_order_relaxed);
  view.displayEnvelopeStage.data = p.displayEnvelopeStage.load(std::memory_order_relaxed);
  view.waveWrite.data = p.waveWrite.load(std::memory_order_acquire);
  for (uint32_t ch = 0; ch < kOutputChannels; ++ch)
    for (uint32_t n = 0; n < kWaveHistory; ++n)
      view.waveHistory[ch][n] = p.waveHistory[ch][n].load(std::memory_order_relaxed);
  view.rawSource = snapshot.hasSource ? std::optional<PortableFaultSourceInfo>(snapshot.source) : std::nullopt;
  view.sourceStatus = snapshot.sourceStatus;
  return true;
}
double normalizedPerformanceParam(const FaultReadout& p, clap_id id) {
  switch (id) {
    case kAttackParamId: return normalizedEnvelopeTime(p.attackMs, 1., 5000.);
    case kDecayParamId: return normalizedEnvelopeTime(p.decayMs, 5., 8000.);
    case kSustainParamId: return p.sustain;
    case kReleaseParamId: return normalizedEnvelopeTime(p.releaseMs, 5., 12000.);
    case kOutputRotationParamId: return (p.outputRotationDeg + 180.) / 360.;
    default: return 0.;
  }
}
std::string sourceStatusText(const FaultReadout& p) { return p.sourceStatus; }
