// Version 1's 1160-byte ABI is retained verbatim as the parameter prefix.
// Version 2 appends explicit storage metadata, a length-prefixed UTF-8 locator,
// then optional interleaved float PCM. macOS/Windows targets are little-endian.
static_assert(sizeof(SavedState) == 1160 &&
                  offsetof(SavedState, samplePath) == 132,
              "Preserve the legacy Ambi Grain state layout");
struct StorageStateHeader {
  uint32_t mode = 0, embedded = 0, pathBytes = 0, channels = 0;
  uint32_t frames = 0, reserved = 0;
  double sampleRate = 0;
  float sourcePeak = 0, normalizationGain = 1;
};
static_assert(sizeof(StorageStateHeader) == 40 &&
                  offsetof(StorageStateHeader, sampleRate) == 24,
              "Storage metadata must have identical macOS/Windows layout");

bool validSavedParameters(const SavedState &s) {
  for (double v :
       {s.order, s.mode, s.density, s.grainMs, s.sourcePosition, s.scanSpeed,
        s.positionJitter, s.rate, s.rateJitter, s.reverse, s.freeze,
        s.jumpSteps, s.gainDb, s.sync, s.envelope})
    if (!std::isfinite(v))
      return false;
  return s.playing <= 1;
}

bool stateSave(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
  if (!stream || !stream->write)
    return false;
  auto &p = *self(plugin);
  servicePortableSampleLoad(p);
  if (p.samplePath.size() > kMaximumSamplePathBytes)
    return false;
  SavedState s;
  std::memset(&s, 0, sizeof(s));
  s.version = kStateVersion;
  paramsGetValue(plugin, kOrderParamId, &s.order);
  paramsGetValue(plugin, kModeParamId, &s.mode);
  paramsGetValue(plugin, kDensityParamId, &s.density);
  paramsGetValue(plugin, kGrainMsParamId, &s.grainMs);
  paramsGetValue(plugin, kSourcePosParamId, &s.sourcePosition);
  paramsGetValue(plugin, kScanSpeedParamId, &s.scanSpeed);
  paramsGetValue(plugin, kJitterParamId, &s.positionJitter);
  paramsGetValue(plugin, kRateParamId, &s.rate);
  paramsGetValue(plugin, kRateJitterParamId, &s.rateJitter);
  paramsGetValue(plugin, kReverseParamId, &s.reverse);
  paramsGetValue(plugin, kFreezeParamId, &s.freeze);
  paramsGetValue(plugin, kJumpStepsParamId, &s.jumpSteps);
  paramsGetValue(plugin, kSyncParamId, &s.sync);
  paramsGetValue(plugin, kEnvelopeParamId, &s.envelope);
  paramsGetValue(plugin, kGainParamId, &s.gainDb);
  s.playing = p.playing.load(std::memory_order_acquire) ? 1u : 0u;
  if (!validSavedParameters(s))
    return false;
  // The v1 path field is unused in v2; do not silently truncate long locators.
  StorageStateHeader h;
  h.mode = static_cast<uint32_t>(p.storageMode);
  h.pathBytes = static_cast<uint32_t>(p.samplePath.size());
  const auto sample = std::atomic_load(&p.sample);
  if (sample) {
    h.channels = sample->channels;
    h.frames = sample->frames;
    h.sampleRate = sample->sampleRate;
    h.sourcePeak = sample->sourcePeak;
    h.normalizationGain = sample->normalizationGain;
    const uint64_t bytes = uint64_t(h.channels) * h.frames * sizeof(float);
    const bool needsPayload = p.storageMode == StorageMode::Embed ||
                              p.sampleFromEmbedded || p.samplePath.empty();
    if (needsPayload && bytes > kMaximumEmbeddedAudioBytes) {
      p.storageStatus = "EMBED EXCEEDS 1 GIB / USE PROJECT OR LINK";
      return false;
    }
    if (uint64_t(sample->audio.size()) * sizeof(float) != bytes)
      return false;
    h.embedded = needsPayload ? 1u : 0u;
  }
  if (!s3g::clap_state::writeAll(stream, &s, sizeof(s)) ||
      !s3g::clap_state::writeAll(stream, &h, sizeof(h)) ||
      !s3g::clap_state::writeAll(stream, p.samplePath.data(),
                                 p.samplePath.size()))
    return false;
  return !h.embedded ||
         s3g::clap_state::writeAll(stream, sample->audio.data(),
                                   sample->audio.size() * sizeof(float));
}

bool stateLoad(const clap_plugin_t *plugin, const clap_istream_t *stream) {
  if (!stream || !stream->read)
    return false;
  SavedState s{};
  if (!s3g::clap_state::readAll(stream, &s, sizeof(s)) ||
      (s.version != kLegacyStateVersion && s.version != kStateVersion) ||
      !validSavedParameters(s))
    return false;
  StorageStateHeader h;
  std::string locator, resolved, error;
  StorageMode mode = StorageMode::Project;
  std::shared_ptr<const s3g::AmbiGrainSample> sample;
  auto &p = *self(plugin);
  bool projectRelativeReady = false;
  try {
    if (s.version == kLegacyStateVersion) {
      locator.assign(
          s.samplePath,
          std::find(s.samplePath, s.samplePath + sizeof(s.samplePath), '\0'));
      mode = locator.empty() ? StorageMode::Project : StorageMode::Link;
    } else {
      if (!s3g::clap_state::readAll(stream, &h, sizeof(h)) ||
          h.mode > static_cast<uint32_t>(StorageMode::Embed) ||
          h.embedded > 1 || h.reserved != 0 ||
          h.pathBytes > kMaximumSamplePathBytes)
        return false;
      mode = static_cast<StorageMode>(h.mode);
      locator.resize(h.pathBytes);
      if (!s3g::clap_state::readAll(stream, locator.data(), locator.size()) ||
          locator.find('\0') != std::string::npos)
        return false;
      if (h.embedded) {
        const uint64_t count = uint64_t(h.channels) * h.frames;
        if (h.channels == 0 || h.channels > kChannelCount || h.frames < 8 ||
            !std::isfinite(h.sampleRate) || h.sampleRate < 1 ||
            h.sampleRate > 768000 || !std::isfinite(h.sourcePeak) ||
            h.sourcePeak < 0 || !std::isfinite(h.normalizationGain) ||
            h.normalizationGain <= 0 ||
            count * sizeof(float) > kMaximumEmbeddedAudioBytes)
          return false;
        auto decoded = std::make_shared<s3g::AmbiGrainSample>();
        decoded->channels = h.channels;
        decoded->frames = h.frames;
        decoded->sampleRate = h.sampleRate;
        decoded->sourcePeak = h.sourcePeak;
        decoded->normalizationGain = h.normalizationGain;
        decoded->path = locator;
        decoded->audio.resize(static_cast<size_t>(count));
        if (!s3g::clap_state::readAll(stream, decoded->audio.data(),
                                      decoded->audio.size() * sizeof(float)))
          return false;
        for (float v : decoded->audio)
          if (!std::isfinite(v))
            return false;
        // PCM is already field-normalized. Re-normalizing here would
        // alter saved audio and discard the original gain readout.
        sample = std::move(decoded);
      }
    }
    if (!locator.empty()) {
      const auto path = std::filesystem::u8path(locator);
      if (path.is_absolute())
        resolved = locator;
      else if (mode == StorageMode::Project) {
        if (!storage::relativePathIsSafe(path))
          return false;
        storage::ProjectLocation location;
        projectRelativeReady = queryStorageProject(p, location, error) &&
                               storage::resolveProjectRelativePath(
                                   location, locator, resolved, &error);
      }
      if (!h.embedded && !resolved.empty())
        sample = readSampleFromPath(resolved);
    }
  } catch (...) {
    return false;
  }
  // Parse/validate completely before touching live state. A valid missing
  // file restores parameters and its locator, but never leaves old audio
  // sounding as if it belonged to the recalled preset.
  cancelStorageLoads(p);
  p.storageMode = mode;
  publishStorageSample(p, sample, locator, resolved, h.embedded != 0);
  p.projectCopyPending = mode == StorageMode::Project && sample &&
                         (!projectRelativeReady || h.embedded != 0);
  p.projectRecallPending =
      mode == StorageMode::Project && !sample && !locator.empty() &&
      storage::relativePathIsSafe(std::filesystem::u8path(locator));
  setParam(p, kOrderParamId, s.order);
  setParam(p, kModeParamId, s.mode);
  setParam(p, kDensityParamId, s.density);
  setParam(p, kGrainMsParamId, s.grainMs);
  setParam(p, kSourcePosParamId, s.sourcePosition);
  setParam(p, kScanSpeedParamId, s.scanSpeed);
  setParam(p, kJitterParamId, s.positionJitter);
  setParam(p, kRateParamId, s.rate);
  setParam(p, kRateJitterParamId, s.rateJitter);
  setParam(p, kReverseParamId, s.reverse);
  setParam(p, kFreezeParamId, s.freeze);
  setParam(p, kJumpStepsParamId, s.jumpSteps);
  setParam(p, kSyncParamId, s.sync);
  setParam(p, kEnvelopeParamId, s.envelope);
  setParam(p, kGainParamId, s.gainDb);
  p.playing.store(s.playing != 0, std::memory_order_release);
  p.sampleStatus = !sample && !locator.empty() ? "MISSING SAMPLE / RELINK" : "";
  p.storageStatus.clear();
  if (sample && mode == StorageMode::Project) {
    if (projectRelativeReady && !h.embedded)
      p.storageStatus =
          registerProjectSample(p)
              ? "PROJECT SAMPLE RESTORED"
              : "PROJECT RESTORED / MEDIA REGISTRATION UNAVAILABLE";
    else {
      p.storageStatus = "PROJECT COLLECTION PENDING";
      maybeQueuePendingProjectCopy(p);
    }
  }
  return true;
}

const clap_plugin_state_t stateExt{stateSave, stateLoad};

// EMBED can produce large preset files. Write a private sibling first so a
// failed save (including disk-full or the embed cap) leaves an old preset
// intact.
bool saveAmbiPresetFile(const clap_plugin_t *plugin,
                        const clap_plugin_state_t &state,
                        const std::string &path) {
  if (!state.save || path.empty())
    return false;
  try {
    const auto target = std::filesystem::u8path(path);
    const auto staging = storage::detail::uniqueTemporaryPath(target);
    std::error_code error;
    if (!std::filesystem::create_directory(staging, error))
      return false;
    const auto temporary = staging / "state.s3gpreset";
    struct Cleanup {
      std::filesystem::path file, directory;
      ~Cleanup() {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        std::filesystem::remove(directory, ignored);
      }
    } cleanup{temporary, staging};
    std::ofstream file(temporary, std::ios::binary);
    if (!file)
      return false;
    clap_ostream_t output{&file,
                          [](const clap_ostream_t *stream, const void *bytes,
                             uint64_t count) -> int64_t {
                            auto &out =
                                *static_cast<std::ofstream *>(stream->ctx);
                            out.write(static_cast<const char *>(bytes),
                                      static_cast<std::streamsize>(count));
                            return out ? static_cast<int64_t>(count) : -1;
                          }};
    const bool saved = state.save(plugin, &output);
    file.close();
    if (!saved || !file)
      return false;
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), target.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, target, error);
    return !error;
#endif
  } catch (...) {
    return false;
  }
}
