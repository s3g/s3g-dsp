// Existing on-disk codecs remain authoritative. Decode privately, then restore
// capture banks only while inactive (the host restarts an active instance).
// Saves read an audio-published immutable snapshot, not mutable DSP storage.
struct EffectMemoryStream {
  std::vector<uint8_t> bytes;
  size_t position = 0;
  clap_ostream_t out{
      this, [](const clap_ostream_t *s, const void *p, uint64_t n) -> int64_t {
        auto &b = static_cast<EffectMemoryStream *>(s->ctx)->bytes;
        const auto *first = static_cast<const uint8_t *>(p);
        b.insert(b.end(), first, first + n);
        return static_cast<int64_t>(n);
      }};
  clap_istream_t in{
      this, [](const clap_istream_t *s, void *p, uint64_t n) -> int64_t {
        auto &stream = *static_cast<EffectMemoryStream *>(s->ctx);
        const size_t count =
            std::min<size_t>(n, stream.bytes.size() - stream.position);
        std::memcpy(p, stream.bytes.data() + stream.position, count);
        stream.position += count;
        return static_cast<int64_t>(count);
      }};
};
void copyCapturePayload(const EffectCapturePayload &data, Plugin &candidate) {
#if !defined(S3G_PARTIAL_TRACE)
  candidate.processor.setPrint(data.print);
#elif S3G_PARTIAL_TRACE
  candidate.pendingFrozenVoices = data.voices;
  candidate.pendingFrozenBody = data.body;
  candidate.pendingFrozenValid = data.frozen;
#else
  candidate.pendingResponse.assign(
      data.samples.begin(),
      data.samples.begin() + static_cast<size_t>(data.pickups) * data.frames);
  candidate.pendingResponsePickupCount = data.pickups;
  candidate.pendingResponseBody = data.body;
  candidate.pendingResponseSampleRate = data.rate;
#endif
}
void captureIntoCandidate(const Plugin &p, Plugin &candidate) {
  candidate.plugin.plugin_data = &candidate;
  if (!p.effectPendingCaptureState.empty()) {
    EffectMemoryStream stream;
    stream.bytes = p.effectPendingCaptureState;
    rawEffectStateLoad(&candidate.plugin, &stream.in);
  } else if (p.effectActive) {
    p.effectCaptureSnapshot.read(
        [&](const auto &data) { copyCapturePayload(data, candidate); });
  } else {
    EffectMemoryStream stream;
    rawEffectStateSave(&p.plugin, &stream.out);
    rawEffectStateLoad(&candidate.plugin, &stream.in);
  }
}
void prepareCapturePresetSave(const Plugin &p, Plugin &candidate) {
  const auto params = snapshotEffectParams(candidate);
  captureIntoCandidate(p, candidate);
  candidate.params = params;
  publishEffectParams(candidate);
#if !defined(S3G_PARTIAL_TRACE)
  publishAllParams(candidate);
#endif
  candidate.guiViewMode = p.guiViewMode;
  candidate.guiViewAzDeg = p.guiViewAzDeg;
  candidate.guiViewElDeg = p.guiViewElDeg;
}
bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  if (!stream || !stream->write)
    return false;
  const auto &p = *self(plugin);
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  candidate->params = snapshotEffectParams(p);
  publishEffectParams(*candidate);
  prepareCapturePresetSave(p, *candidate);
  return rawEffectStateSave(&candidate->plugin, stream);
}
bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  if (!stream || !stream->read)
    return false;
  auto candidate = std::make_unique<Plugin>();
  candidate->plugin.plugin_data = candidate.get();
  if (!rawEffectStateLoad(&candidate->plugin, stream))
    return false;
  auto &p = *self(plugin);
  EffectMemoryStream encoded;
  if (!rawEffectStateSave(&candidate->plugin, &encoded.out))
    return false;
  if (p.effectActive) {
    p.effectPendingCaptureState = std::move(encoded.bytes);
  } else {
    p.effectPendingCaptureState.clear();
    if (!rawEffectStateLoad(plugin, &encoded.in))
      return false;
  }
  for (uint32_t n = 0; n < paramsCount(plugin); ++n) {
    clap_param_info_t i{};
    if (paramsGetInfo(plugin, n, &i))
      p.effectValues[i.id].store(readEffectScalar(candidate->params, i.id),
                                 std::memory_order_release);
  }
  p.guiViewMode = candidate->guiViewMode;
  p.guiViewAzDeg = candidate->guiViewAzDeg;
  p.guiViewElDeg = candidate->guiViewElDeg;
  if (p.effectActive && p.host && p.host->request_restart)
    p.host->request_restart(p.host);
  return true;
}
void finishCapturePresetLoad(Plugin &p, const Plugin &candidate) {
  auto restored = std::make_unique<Plugin>();
  restored->plugin.plugin_data = restored.get();
  restored->params = snapshotEffectParams(p); // Preset OUT stays unchanged.
  publishEffectParams(*restored);
  prepareCapturePresetSave(candidate, *restored);
  EffectMemoryStream stream;
  if (rawEffectStateSave(&restored->plugin, &stream.out))
    stateLoad(&p.plugin, &stream.in);
}
void applyPendingCaptureState(Plugin &p) {
  if (p.effectPendingCaptureState.empty())
    return;
  const auto latest = snapshotEffectParams(p);
  EffectMemoryStream stream;
  stream.bytes = std::move(p.effectPendingCaptureState);
  rawEffectStateLoad(&p.plugin, &stream.in);
  p.params = latest;
  publishEffectParams(p);
}
void publishCaptureSnapshot(Plugin &p) {
#if !defined(S3G_PARTIAL_TRACE) || S3G_PARTIAL_TRACE
  const int slot = p.effectCaptureSnapshot.begin();
  auto &data = p.effectCaptureSnapshot.data[slot];
#if !defined(S3G_PARTIAL_TRACE)
  data.print = p.processor.printData();
#else
  data.body = p.processor.resolvedBody();
  data.frozen = p.params.freeze != 0;
  for (uint32_t n = 0; n < data.voices.size(); ++n)
    data.voices[n] = p.processor.frozenVoiceState(n);
#endif
  p.effectCaptureSnapshot.publish(slot);
#else
  (void)p; // Samples are mirrored by the bounded preparation observer.
#endif
}
void prepareCaptureSnapshot(Plugin &p) {
#if defined(S3G_PARTIAL_TRACE) && !S3G_PARTIAL_TRACE
  const size_t capacity =
      std::min<size_t>(s3g::kResponseTraceMaximumFrames,
                       static_cast<size_t>(std::ceil(
                           p.sampleRate * s3g::kResponseTraceMaximumSeconds))) *
      kPickups;
  for (auto &data : p.effectCaptureSnapshot.data)
    data.samples.resize(capacity);
  p.processor.setStateObserver(
      {&p,
       [](void *context, uint32_t frames, uint32_t pickups,
          s3g::AmbiEffectBody body, double rate) {
         auto &p = *static_cast<Plugin *>(context);
         p.effectResponseWriting = p.effectCaptureSnapshot.begin();
         auto &data = p.effectCaptureSnapshot.data[p.effectResponseWriting];
         data.frames = frames;
         data.pickups = pickups;
         data.body = body;
         data.rate = rate;
       },
       [](void *context, uint32_t node, uint32_t frame, float value) {
         auto &p = *static_cast<Plugin *>(context);
         if (p.effectResponseWriting < 0)
           return;
         auto &data = p.effectCaptureSnapshot.data[p.effectResponseWriting];
         const size_t i = static_cast<size_t>(node) * data.frames + frame;
         if (i < data.samples.size())
           data.samples[i] = value;
       },
       [](void *context) {
         auto &p = *static_cast<Plugin *>(context);
         p.effectCaptureSnapshot.publish(p.effectResponseWriting);
         p.effectResponseWriting = -1;
       }});
  const int slot = p.effectCaptureSnapshot.begin();
  auto &data = p.effectCaptureSnapshot.data[slot];
  data.frames = p.processor.responseFrames();
  data.pickups = p.processor.capturedPickupCount();
  data.rate = p.processor.responseSampleRate();
  data.body = p.processor.capturedBody();
  for (uint32_t n = 0; n < data.pickups; ++n)
    for (uint32_t f = 0; f < data.frames; ++f)
      data.samples[static_cast<size_t>(n) * data.frames + f] =
          p.processor.responseSample(n, f);
  p.effectCaptureSnapshot.publish(slot);
#else
  publishCaptureSnapshot(p);
#endif
}
