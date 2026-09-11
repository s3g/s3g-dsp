// Scalar authority is atomic on both host/audio and editor paths. Only
// processTyped applies controls to the active DSP runtime.
void setEncoderScalar(EncoderParams &params, clap_id id, double value) {
  switch (id) {
  case kParamOrder:
    params.order = static_cast<uint32_t>(std::lround(value));
    break;
  case kParamSourceX:
    params.sourceX = static_cast<float>(value);
    break;
  case kParamSourceY:
    params.sourceY = static_cast<float>(value);
    break;
  case kParamSourceZ:
    params.sourceZ = static_cast<float>(value);
    break;
  case kParamListenerX:
    params.listenerX = static_cast<float>(value);
    break;
  case kParamListenerY:
    params.listenerY = static_cast<float>(value);
    break;
  case kParamListenerZ:
    params.listenerZ = static_cast<float>(value);
    break;
  case kParamDirect:
    params.direct = static_cast<float>(value);
    break;
  case kParamEarly:
    params.early = static_cast<float>(value);
    break;
  case kParamLate:
    params.late = static_cast<float>(value);
    break;
  case kParamSize:
    params.size = static_cast<float>(value);
    break;
  case kParamScatter:
    params.scatter = static_cast<float>(value);
    break;
  case kParamWidth:
    params.width = static_cast<float>(value);
    break;
  case kParamAir:
    params.air = static_cast<float>(value);
    break;
  case kParamMovement:
    params.movementMs = static_cast<float>(value);
    break;
  case kParamDoppler:
    params.doppler = static_cast<float>(value);
    break;
  case kParamFieldListen:
    params.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
        static_cast<uint32_t>(std::lround(value)));
    break;
  case kParamOutput:
    params.outputGainDb = static_cast<float>(value);
    break;
  case kParamBypass:
    params.bypassRoom = value >= 0.5;
    break;
  default:
    return;
  }
  params = s3g::sanitizeAmbiRayEncoderParams(params);
}
double readEncoderScalar(const EncoderParams &params, clap_id id) {
  switch (id) {
  case kParamOrder:
    return params.order;
  case kParamSourceX:
    return params.sourceX;
  case kParamSourceY:
    return params.sourceY;
  case kParamSourceZ:
    return params.sourceZ;
  case kParamListenerX:
    return params.listenerX;
  case kParamListenerY:
    return params.listenerY;
  case kParamListenerZ:
    return params.listenerZ;
  case kParamDirect:
    return params.direct;
  case kParamEarly:
    return params.early;
  case kParamLate:
    return params.late;
  case kParamSize:
    return params.size;
  case kParamScatter:
    return params.scatter;
  case kParamWidth:
    return params.width;
  case kParamAir:
    return params.air;
  case kParamMovement:
    return params.movementMs;
  case kParamDoppler:
    return params.doppler;
  case kParamFieldListen:
    return static_cast<uint32_t>(params.fieldListenMode);
  case kParamOutput:
    return params.outputGainDb;
  case kParamBypass:
    return params.bypassRoom ? 1.0 : 0.0;
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

bool parseRayData(const std::string &data, s3g::AmbiRayDescriptor &descriptor,
                  GuiSpaceGeometry &geometry, std::string &json,
                  std::string &error) {
  s3g::portable_gui::ray_field_files::VisualGeometry visual;
  if (!s3g::portable_gui::ray_field_files::parse(data, descriptor, visual, json,
                                                 error))
    return false;
  GuiSpaceGeometry candidate;
  candidate.family = visual.family;
  for (const auto &r : visual.regions)
    candidate.branches.push_back(
        {r.family, r.attachment, r.baseHeight, r.height, r.polygon});
  for (const auto &p : visual.portalDetails)
    candidate.portals.push_back(
        {{p.center.x, p.center.y, p.center.z}, p.width, p.height});
  geometry = std::move(candidate);
  return true;
}
