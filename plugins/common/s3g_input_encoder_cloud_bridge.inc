// Canonical cloud coordinates are independent atomics. Selected-cloud
// controls are aliases, so changing selection never overwrites another cloud.
double readEncoderScalar(const s3g::AmbiCloudEncoderParams &p, clap_id id) {
  switch (id) {
  case kInputsParamId:
    return p.activeInputs;
  case kCloudsParamId:
    return p.activeClouds;
  case kCloudParamId:
    return p.selectedCloud + 1u;
  case kOrderParamId:
    return p.order;
  case kAzimuthParamId:
    return p.selectedAzimuthDeg;
  case kElevationParamId:
    return p.selectedElevationDeg;
  case kDistanceParamId:
    return p.selectedDistance;
  case kCloudGainParamId:
    return p.selectedGain;
  case kSpreadParamId:
    return p.spread;
  case kElevationSpreadParamId:
    return p.elevationSpread;
  case kJitterParamId:
    return p.jitter;
  case kDriftParamId:
    return p.drift;
  case kRateParamId:
    return p.rateHz;
  case kDecorrelateParamId:
    return p.decorrelate;
  case kShapeParamId:
    return static_cast<uint32_t>(p.shape);
  case kForceParamId:
    return static_cast<uint32_t>(p.force);
  case kOutputParamId:
    return p.outputGainDb;
  case kDopplerParamId:
    return p.doppler;
  case kAirParamId:
    return p.air;
  default:
    return 0.;
  }
}
void setEncoderScalar(s3g::AmbiCloudEncoderParams &p, clap_id id,
                      double value) {
  switch (id) {
  case kInputsParamId:
    p.activeInputs = std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 1u, kInputChannels);
    break;
  case kCloudsParamId:
    p.activeClouds =
        std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u,
                             s3g::kAmbiCloudEncoderMaxClouds);
    break;
  case kCloudParamId:
    p.selectedCloud =
        std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u,
                             s3g::kAmbiCloudEncoderMaxClouds) -
        1u;
    break;
  case kOrderParamId:
    p.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                                   1u, s3g::kAmbiCloudEncoderMaxOrder);
    break;
  case kAzimuthParamId:
    p.selectedAzimuthDeg = static_cast<float>(value);
    break;
  case kElevationParamId:
    p.selectedElevationDeg = static_cast<float>(value);
    break;
  case kDistanceParamId:
    p.selectedDistance = static_cast<float>(value);
    break;
  case kCloudGainParamId:
    p.selectedGain = static_cast<float>(value);
    break;
  case kSpreadParamId:
    p.spread = static_cast<float>(value);
    break;
  case kElevationSpreadParamId:
    p.elevationSpread = static_cast<float>(value);
    break;
  case kJitterParamId:
    p.jitter = static_cast<float>(value);
    break;
  case kDriftParamId:
    p.drift = static_cast<float>(value);
    break;
  case kRateParamId:
    p.rateHz = static_cast<float>(value);
    break;
  case kDecorrelateParamId:
    p.decorrelate = static_cast<float>(value);
    break;
  case kShapeParamId:
    p.shape = static_cast<s3g::AmbiCloudShape>(std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 0u, 4u));
    break;
  case kForceParamId:
    p.force = static_cast<s3g::AmbiCloudForce>(std::clamp<uint32_t>(
        static_cast<uint32_t>(std::lround(value)), 0u, 4u));
    break;
  case kOutputParamId:
    p.outputGainDb = static_cast<float>(value);
    break;
  case kDopplerParamId:
    p.doppler = static_cast<float>(value);
    break;
  case kAirParamId:
    p.air = static_cast<float>(value);
    break;
  default:
    break;
  }
}
clap_id canonicalEncoderId(const Plugin &p, clap_id id) {
  if (id >= kAzimuthParamId && id <= kCloudGainParamId) {
    const auto cloud =
        std::clamp(int(p.encoderValues[kCloudParamId].load()), 1, 4) - 1;
    return 100 + cloud * 4 + (id - kAzimuthParamId);
  }
  return id;
}
double readEncoderValue(const Plugin &p, clap_id id) {
  return id < 128 ? p.encoderValues[canonicalEncoderId(p, id)].load(
                        std::memory_order_acquire)
                  : 0.;
}
auto snapshotEncoderClouds(const Plugin &p) {
  std::array<s3g::AmbiCloud, s3g::kAmbiCloudEncoderMaxClouds> clouds{};
  for (unsigned i = 0; i < clouds.size(); ++i) {
    auto &c = clouds[i];
    c.azimuthDeg = p.encoderValues[100 + i * 4].load();
    c.elevationDeg = p.encoderValues[101 + i * 4].load();
    c.distance = p.encoderValues[102 + i * 4].load();
    c.gain = p.encoderValues[103 + i * 4].load();
  }
  return clouds;
}
auto snapshotEncoderParams(const Plugin &p) {
  s3g::AmbiCloudEncoderParams params{};
  for (clap_id id = 1; id <= 19; ++id)
    setEncoderScalar(params, id, readEncoderValue(p, id));
  return params;
}
void publishEncoderState(
    Plugin &p, const s3g::AmbiCloudEncoderParams &params,
    const std::array<s3g::AmbiCloud, s3g::kAmbiCloudEncoderMaxClouds> &clouds) {
  for (clap_id id = 1; id <= 19; ++id)
    if (id < 5 || id > 8)
      p.encoderValues[id].store(readEncoderScalar(params, id),
                                std::memory_order_release);
  for (unsigned i = 0; i < clouds.size(); ++i) {
    const auto &c = clouds[i];
    p.encoderValues[100 + i * 4].store(c.azimuthDeg);
    p.encoderValues[101 + i * 4].store(c.elevationDeg);
    p.encoderValues[102 + i * 4].store(c.distance);
    p.encoderValues[103 + i * 4].store(c.gain);
  }
}
void syncEncoderParams(Plugin &p) {
  p.params = snapshotEncoderParams(p);
  p.encoder.setParams(p.params);
  p.encoder.setClouds(snapshotEncoderClouds(p));
  p.params = p.encoder.params();
}
void applyParam(Plugin &p, clap_id id, double value) {
  if (!std::isfinite(value) || id >= 128)
    return;
  syncEncoderParams(p);
  rawEncoderApplyParam(p, id, value);
  const auto canonical = canonicalEncoderId(p, id);
  if (canonical >= 100 && canonical < 116) {
    const auto &c = p.encoder.clouds()[(canonical - 100) / 4];
    const float v[] = {c.azimuthDeg, c.elevationDeg, c.distance, c.gain};
    p.encoderValues[canonical].store(v[(canonical - 100) % 4]);
  } else
    p.encoderValues[canonical].store(readEncoderScalar(p.params, id));
  if (id == kCloudsParamId || id == kCloudParamId)
    p.encoderValues[kCloudParamId].store(p.params.selectedCloud + 1u);
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
