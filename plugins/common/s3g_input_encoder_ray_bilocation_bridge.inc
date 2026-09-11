// Scalar authority is atomic on both host/audio and editor paths. Only
// processTyped applies controls to the active DSP runtime.
void setEncoderScalar(EncoderParams &params, clap_id id, double value) {
  auto &p = params;
  switch (id) {
  case kParamOrder:
    p.order = static_cast<uint32_t>(std::lround(value));
    break;
  case kParamSourceX:
    p.sourceX = static_cast<float>(value);
    break;
  case kParamSourceY:
    p.sourceY = static_cast<float>(value);
    break;
  case kParamSourceZ:
    p.sourceZ = static_cast<float>(value);
    break;
  case kParamListenerX:
    p.listenerX = static_cast<float>(value);
    break;
  case kParamListenerY:
    p.listenerY = static_cast<float>(value);
    break;
  case kParamListenerZ:
    p.listenerZ = static_cast<float>(value);
    break;
  case kParamPlace:
    p.place = static_cast<float>(value);
    break;
  case kParamPermeability:
    p.permeability = static_cast<float>(value);
    break;
  case kParamMemory:
    p.memorySeconds = static_cast<float>(value);
    break;
  case kParamSeparation:
    p.separationDeg = static_cast<float>(value);
    break;
  case kParamMapMode:
    p.mapMode = static_cast<s3g::AmbiRayBilocationMapMode>(
        static_cast<uint32_t>(std::lround(value)));
    break;
  case kParamDirect:
    p.direct = static_cast<float>(value);
    break;
  case kParamEarly:
    p.early = static_cast<float>(value);
    break;
  case kParamLate:
    p.late = static_cast<float>(value);
    break;
  case kParamSizeA:
    p.sizeA = static_cast<float>(value);
    break;
  case kParamSizeB:
    p.sizeB = static_cast<float>(value);
    break;
  case kParamScatterA:
    p.scatterA = static_cast<float>(value);
    break;
  case kParamScatterB:
    p.scatterB = static_cast<float>(value);
    break;
  case kParamWidthA:
    p.widthA = static_cast<float>(value);
    break;
  case kParamWidthB:
    p.widthB = static_cast<float>(value);
    break;
  case kParamAirA:
    p.airA = static_cast<float>(value);
    break;
  case kParamAirB:
    p.airB = static_cast<float>(value);
    break;
  case kParamMovement:
    p.movementMs = static_cast<float>(value);
    break;
  case kParamDoppler:
    p.doppler = static_cast<float>(value);
    break;
  case kParamOutput:
    p.outputGainDb = static_cast<float>(value);
    break;
  case kParamBypass:
    p.bypassRoom = value >= 0.5;
    break;
  case kParamFieldListen:
    p.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
        static_cast<uint32_t>(std::lround(value)));
    break;
  default:
    return;
  }
  p = s3g::sanitizeAmbiRayBilocationParams(p);
}
double readEncoderScalar(const EncoderParams &params, clap_id id) {
  const auto &p = params;
  switch (id) {
  case kParamOrder:
    return p.order;
  case kParamSourceX:
    return p.sourceX;
  case kParamSourceY:
    return p.sourceY;
  case kParamSourceZ:
    return p.sourceZ;
  case kParamListenerX:
    return p.listenerX;
  case kParamListenerY:
    return p.listenerY;
  case kParamListenerZ:
    return p.listenerZ;
  case kParamPlace:
    return p.place;
  case kParamPermeability:
    return p.permeability;
  case kParamMemory:
    return p.memorySeconds;
  case kParamSeparation:
    return p.separationDeg;
  case kParamMapMode:
    return static_cast<uint32_t>(p.mapMode);
  case kParamDirect:
    return p.direct;
  case kParamEarly:
    return p.early;
  case kParamLate:
    return p.late;
  case kParamSizeA:
    return p.sizeA;
  case kParamSizeB:
    return p.sizeB;
  case kParamScatterA:
    return p.scatterA;
  case kParamScatterB:
    return p.scatterB;
  case kParamWidthA:
    return p.widthA;
  case kParamWidthB:
    return p.widthB;
  case kParamAirA:
    return p.airA;
  case kParamAirB:
    return p.airB;
  case kParamMovement:
    return p.movementMs;
  case kParamDoppler:
    return p.doppler;
  case kParamOutput:
    return p.outputGainDb;
  case kParamBypass:
    return p.bypassRoom ? 1.0 : 0.0;
  case kParamFieldListen:
    return static_cast<uint32_t>(p.fieldListenMode);
  default:
    return 0.0;
  }
}
EncoderParams snapshotEncoderParams(const Plugin &p) {
  EncoderParams params{};
  for (clap_id id = 1; id <= kParamFieldListen; ++id)
    setEncoderScalar(params, id,
                     p.encoderValues[id].load(std::memory_order_acquire));
  return params;
}
void publishEncoderParams(Plugin &p, const EncoderParams &params) {
  for (clap_id id = 1; id <= kParamFieldListen; ++id)
    p.encoderValues[id].store(readEncoderScalar(params, id),
                              std::memory_order_release);
}
void applyParam(Plugin &p, clap_id id, double value) {
  if (id < 1 || id > kParamFieldListen || !std::isfinite(value))
    return;
  auto params = snapshotEncoderParams(p);
  setEncoderScalar(params, id, value);
  p.encoderValues[id].store(readEncoderScalar(params, id),
                            std::memory_order_release);
}
double getParam(const Plugin &p, clap_id id) {
  return id > 0 && id <= kParamFieldListen
             ? p.encoderValues[id].load(std::memory_order_acquire)
             : 0.;
}
void serviceEncoderGui(Plugin &p, const clap_output_events_t *out) {
  s3g::clap_gui::serviceParamEvents(p.guiParamEvents, out,
                                    [](clap_id, double) {});
}
