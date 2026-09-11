// Audio owns the processor. Main owns guiPaths; a bounded SPSC handoff
// publishes edits without locking, allocating or retaining per-drag records.
double readEncoderScalar(const s3g::AmbiPathEncoderParams &p, clap_id id) {
  switch (id) {
  case kInputsParamId:
    return p.activeInputs;
  case kOrderParamId:
    return p.order;
  case kPathsParamId:
    return p.activePaths;
  case kSelectedPathParamId:
    return p.selectedPath + 1u;
  case kSelectedSourceParamId:
    return p.selectedSource + 1u;
  case kAssignParamId:
    return static_cast<uint32_t>(p.assignMode);
  case kPlaybackParamId:
    return static_cast<uint32_t>(p.playback);
  case kSyncParamId:
    return static_cast<uint32_t>(p.syncMode);
  case kLoopParamId:
    return static_cast<uint32_t>(p.loopMode);
  case kInterpParamId:
    return static_cast<uint32_t>(p.interpolation);
  case kRateParamId:
    return p.rateHz;
  case kDivisionParamId:
    return p.syncDivisionBeats;
  case kPhaseParamId:
    return p.phase;
  case kPhaseSpreadParamId:
    return p.phaseSpread;
  case kSmoothParamId:
    return p.smooth;
  case kEaseParamId:
    return p.ease;
  case kDistanceScaleParamId:
    return p.distanceScale;
  case kDopplerParamId:
    return p.doppler;
  case kAirParamId:
    return p.air;
  case kOutputParamId:
    return p.outputGainDb;
  default:
    return 0.;
  }
}
void setEncoderScalar(s3g::AmbiPathEncoderParams &p, clap_id id, double value) {
  switch (id) {
  case kInputsParamId:
    p.activeInputs = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 1u, 64u);
    break;
  case kOrderParamId:
    p.order =
        std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, 7u);
    break;
  case kPathsParamId:
    p.activePaths = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 1u, 16u);
    break;
  case kSelectedPathParamId:
    p.selectedPath = std::clamp<uint32_t>(
                         static_cast<uint32_t>(std::lround(value)), 1u, 16u) -
                     1u;
    break;
  case kSelectedSourceParamId:
    p.selectedSource = std::clamp<uint32_t>(
                           static_cast<uint32_t>(std::lround(value)), 1u, 64u) -
                       1u;
    break;
  case kAssignParamId:
    p.assignMode = static_cast<s3g::AmbiPathAssignMode>(std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 0u, 2u));
    break;
  case kPlaybackParamId:
    p.playback = static_cast<s3g::AmbiPathPlaybackMode>(std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 0u, 2u));
    break;
  case kSyncParamId:
    p.syncMode = static_cast<s3g::AmbiPathSyncMode>(std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 0u, 1u));
    break;
  case kLoopParamId:
    p.loopMode = static_cast<s3g::AmbiPathLoopMode>(std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 0u, 2u));
    break;
  case kInterpParamId:
    p.interpolation =
        static_cast<s3g::AmbiPathInterpolation>(std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 0u, 2u));
    break;
  case kRateParamId:
    p.rateHz = static_cast<float>(value);
    break;
  case kDivisionParamId:
    p.syncDivisionBeats = static_cast<float>(value);
    break;
  case kPhaseParamId:
    p.phase = static_cast<float>(value);
    break;
  case kPhaseSpreadParamId:
    p.phaseSpread = static_cast<float>(value);
    break;
  case kSmoothParamId:
    p.smooth = static_cast<float>(value);
    break;
  case kEaseParamId:
    p.ease = static_cast<float>(value);
    break;
  case kDistanceScaleParamId:
    p.distanceScale = static_cast<float>(value);
    break;
  case kDopplerParamId:
    p.doppler = static_cast<float>(value);
    break;
  case kAirParamId:
    p.air = static_cast<float>(value);
    break;
  case kOutputParamId:
    p.outputGainDb = static_cast<float>(value);
    break;
  default:
    break;
  }
}
double readEncoderValue(const Plugin &p, clap_id id) {
  return id > 0 && id <= 20
             ? p.encoderValues[id].load(std::memory_order_acquire)
             : 0.;
}
auto snapshotEncoderParams(const Plugin &p) {
  s3g::AmbiPathEncoderParams params{};
  for (clap_id id = 1; id <= 20; ++id)
    setEncoderScalar(params, id, readEncoderValue(p, id));
  return params;
}
void publishEncoderParams(Plugin &p, const s3g::AmbiPathEncoderParams &params) {
  for (clap_id id = 1; id <= 20; ++id)
    p.encoderValues[id].store(readEncoderScalar(params, id));
}
void publishEncoderPaths(Plugin &p, const EncoderPaths &paths) {
  p.guiPaths = paths;
  const auto slot = p.pendingPaths.begin();
  p.pendingPaths.data[slot] = {++p.pathRevision, paths};
  p.pendingPaths.publish(slot);
}
void syncEncoderParams(Plugin &p) {
  p.pendingPaths.read([&](const PathEdit &edit) {
    if (edit.revision != p.appliedPathRevision) {
      p.encoder.setPaths(edit.paths);
      p.appliedPathRevision = edit.revision;
    }
  });
  p.params = snapshotEncoderParams(p);
  p.encoder.setParams(p.params);
  p.params = p.encoder.params();
}
void applyParam(Plugin &p, clap_id id, double value) {
  if (id < 1 || id > 20 || !std::isfinite(value))
    return;
  syncEncoderParams(p);
  rawEncoderApplyParam(p, id, value);
  p.encoderValues[id].store(readEncoderScalar(p.params, id));
  if (id == kInputsParamId)
    p.encoderValues[kSelectedSourceParamId].store(p.params.selectedSource + 1u);
  if (id == kPathsParamId)
    p.encoderValues[kSelectedPathParamId].store(p.params.selectedPath + 1u);
}
void serviceEncoderGui(Plugin &p, const clap_output_events_t *out) {
  s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
                                    [](clap_id, double) {});
}
void publishEncoderDisplay(Plugin &p) {
  const auto slot = p.encoderDisplay.begin();
  for (unsigned i = 0; i < kInputChannels; ++i)
    p.encoderDisplay.data[slot][i] = p.encoder.sourcePositionForDisplay(i);
  p.encoderDisplay.publish(slot);
}
