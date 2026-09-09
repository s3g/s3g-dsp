// Shared Sample-family collection/registration, with this instrument's
// interleaved, field-normalized audio. All host calls run on the main thread.
namespace storage = s3g::sample_storage;

void markStorageDirty(Plugin &p) {
  const auto *state = p.host && p.host->get_extension
                          ? static_cast<const clap_host_state_t *>(
                                p.host->get_extension(p.host, CLAP_EXT_STATE))
                          : nullptr;
  if (state && state->mark_dirty)
    state->mark_dirty(p.host);
}

bool queryStorageProject(Plugin &p, storage::ProjectLocation &location,
                         std::string &error) {
  p.reaperContext = storage::reaperContext(p.host);
  return storage::queryProjectLocation(p.reaperContext, location, &error);
}

bool sampleFileExists(const std::string &path) {
  std::error_code error;
  return !path.empty() &&
         std::filesystem::is_regular_file(std::filesystem::u8path(path), error);
}

void projectSampleRenamed(void *owner, const std::string &) {
  auto &p = *static_cast<Plugin *>(owner);
  p.projectRenamePending.store(true, std::memory_order_release);
  if (p.host && p.host->request_callback)
    p.host->request_callback(p.host);
}

bool registerProjectSample(Plugin &p) {
  p.reaperContext = storage::reaperContext(p.host);
  return p.projectFileRegistration.reset(p.reaperContext, p.resolvedSamplePath,
                                         &p, projectSampleRenamed,
                                         "s3g Processor Ambi Grain 16");
}

void serviceProjectSampleRename(Plugin &p) {
  if (!p.projectRenamePending.exchange(false, std::memory_order_acq_rel) ||
      p.storageMode != StorageMode::Project)
    return;
  const auto absolute = p.projectFileRegistration.absolutePath();
  if (absolute.empty())
    return;
  storage::ProjectLocation location;
  std::string relative, error;
  p.resolvedSamplePath = absolute;
  if (queryStorageProject(p, location, error) &&
      storage::makeProjectRelativePath(location, absolute, relative, &error)) {
    p.samplePath = std::move(relative);
    p.projectCopyPending = false;
    p.storageStatus = "PROJECT SAMPLE MOVED";
  } else {
    p.samplePath = absolute;
    p.projectCopyPending = true;
    p.storageStatus = "PROJECT SAMPLE MOVE PENDING";
  }
  markStorageDirty(p);
}

void publishStorageSample(Plugin &p,
                          std::shared_ptr<const s3g::AmbiGrainSample> sample,
                          std::string locator, std::string resolved,
                          bool embedded) {
  p.projectFileRegistration.clear();
  p.projectRenamePending.store(false, std::memory_order_release);
  p.samplePath = std::move(locator);
  p.resolvedSamplePath = std::move(resolved);
  p.sampleFromEmbedded = embedded;
  p.projectRecallPending = false;
  std::atomic_store_explicit(&p.sample, std::move(sample),
                             std::memory_order_release);
  p.resetRequested.store(true, std::memory_order_release);
  if (p.host && p.host->request_process)
    p.host->request_process(p.host);
}

void cancelStorageLoads(Plugin &p) {
  ++p.sampleGeneration;
  p.pendingSampleRequest.reset();
  p.nextProjectCopyProbe = {};
}

// An embedded source can become PROJECT media even if its old source path no
// longer exists. Reconstruct decoded source PCM with one shared field gain;
// never normalize individual channels or read a changed external file instead.
storage::ProjectCopyResult
materializeProjectSample(const SampleLoadRequest &request) {
  storage::ProjectCopyResult result;
#if defined(__APPLE__) || defined(_WIN32)
  const auto &asset = request.asset;
  if (!asset || asset->channels == 0 || asset->channels > kChannelCount ||
      asset->frames == 0) {
    result.error = "NO AUDIO AVAILABLE FOR PROJECT STORAGE";
    return result;
  }
  const auto temporaryDirectory = storage::detail::uniqueTemporaryPath(
      std::filesystem::temp_directory_path() / "s3g-ambi-grain");
  std::error_code error;
  if (!std::filesystem::create_directory(temporaryDirectory, error)) {
    result.error = "COULD NOT PREPARE PROJECT AUDIO";
    return result;
  }
  const auto wave = temporaryDirectory / "ambi-grain.wav";
  struct Cleanup {
    std::filesystem::path wave, directory;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove(wave, ignored);
      std::filesystem::remove(directory, ignored);
    }
  } cleanup{wave, temporaryDirectory};
  std::array<std::vector<float>, kChannelCount> channels;
  std::array<const float *, kChannelCount> pointers{};
  const double gain =
      std::isfinite(asset->normalizationGain) && asset->normalizationGain > 0
          ? asset->normalizationGain
          : 1.0;
  for (uint32_t ch = 0; ch < asset->channels; ++ch) {
    channels[ch].resize(asset->frames);
    for (uint32_t i = 0; i < asset->frames; ++i) {
      channels[ch][i] = static_cast<float>(
          asset->audio[size_t(i) * asset->channels + ch] / gain);
      if (!std::isfinite(channels[ch][i])) {
        result.error = "PROJECT AUDIO OUT OF RANGE / KEEP EMBED";
        return result;
      }
    }
    pointers[ch] = channels[ch].data();
  }
  if (!s3g::audio_file::writePlanarFloatWaveAtomically(
          wave.u8string(), asset->sampleRate, asset->channels, asset->frames,
          pointers.data(), result.error))
    return result;
  return storage::copyFileIntoProject(request.project, wave.u8string());
#else
  (void)request;
  result.error = "PROJECT AUDIO EXPORT IS NOT AVAILABLE ON THIS PLATFORM";
  return result;
#endif
}

SampleLoadResult performStorageLoad(SampleLoadRequest request) {
  SampleLoadResult result;
  result.request = std::move(request);
  const auto &work = result.request;
  try {
    if (work.copyOnly) {
      result.copy = work.materialize
                        ? materializeProjectSample(work)
                        : storage::copyFileIntoProject(work.project, work.path);
      result.error = result.copy.error;
      return result;
    }
    if (work.mode == StorageMode::Project) {
      if (work.project.available())
        result.copy = storage::copyFileIntoProject(work.project, work.path);
      result.error = result.copy.success
                         ? ""
                         : work.project.available() ? result.copy.error
                                                    : work.projectError;
    }
    // Read the verified project copy when available, so playback and
    // subsequent recall use the same bytes even if the original changes.
    result.asset = readSampleFromPath(
        result.copy.success ? result.copy.absolutePath : work.path);
    if (!result.asset)
      result.error = "COULD NOT LOAD SAMPLE / CHECK FILE";
  } catch (...) {
    result.asset.reset();
    result.error = "SAMPLE STORAGE RAN OUT OF RESOURCES";
  }
  return result;
}

void startPendingSampleLoad(Plugin &p) {
  if (p.sampleLoad.valid() || !p.pendingSampleRequest)
    return;
  p.activeSampleRequest = std::move(p.pendingSampleRequest);
  p.pendingSampleRequest.reset();
  try {
    if (p.sampleLoadTask.valid())
      p.sampleLoadTask.get();
    std::promise<SampleLoadResult> result;
    p.sampleLoad = result.get_future();
    p.sampleLoadTask =
        std::async(std::launch::async, [&p, request = *p.activeSampleRequest,
                                        result = std::move(result)]() mutable {
          auto completed = performStorageLoad(std::move(request));
          // Readiness precedes the callback, so no GUI timer is needed
          // to complete an import or collection after closing the editor.
          result.set_value(std::move(completed));
          if (!p.sampleLoaderStopping.load(std::memory_order_acquire) &&
              p.host && p.host->request_callback)
            p.host->request_callback(p.host);
        });
  } catch (...) {
    p.sampleLoad = {};
    p.activeSampleRequest.reset();
    p.sampleStatus = "COULD NOT START SAMPLE LOADER";
  }
}

void queuePortableSampleLoad(Plugin &p, std::string path, bool relink = false) {
  if (path.empty())
    return;
  SampleLoadRequest request;
  request.path = std::move(path);
  request.mode = p.storageMode;
  request.generation = ++p.sampleGeneration;
  request.preserveSettings =
      relink || (!std::atomic_load(&p.sample) && !p.samplePath.empty());
  if (request.mode == StorageMode::Project)
    (void)queryStorageProject(p, request.project, request.projectError);
  p.pendingSampleRequest = std::move(request);
  p.sampleStatus = "LOADING SAMPLE";
  startPendingSampleLoad(p);
}

void maybeQueuePendingProjectCopy(Plugin &p) {
  if (p.storageMode != StorageMode::Project ||
      (!p.projectCopyPending && !p.projectRecallPending) ||
      p.sampleLoad.valid() || p.pendingSampleRequest)
    return;
  const auto sample = std::atomic_load(&p.sample);
  if (!sample && !p.projectRecallPending)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < p.nextProjectCopyProbe)
    return;
  p.nextProjectCopyProbe = now + std::chrono::seconds(1);
  SampleLoadRequest request;
  if (!queryStorageProject(p, request.project, p.storageStatus))
    return;
  if (!sample) {
    std::string resolved;
    if (storage::resolveProjectRelativePath(request.project, p.samplePath,
                                            resolved) &&
        sampleFileExists(resolved))
      queuePortableSampleLoad(p, resolved, true);
    else
      p.storageStatus = "MISSING PROJECT SAMPLE / RELINK";
    return;
  }
  request.mode = StorageMode::Project;
  request.copyOnly = true;
  request.generation = ++p.sampleGeneration;
  request.path = p.resolvedSamplePath;
  request.asset = sample;
  request.materialize = p.sampleFromEmbedded || !sampleFileExists(request.path);
  // Keep the PCM in state while a pathless/previously embedded source is
  // waiting for collection, including a save while the worker is busy.
  if (request.materialize)
    p.sampleFromEmbedded = true;
  p.pendingSampleRequest = std::move(request);
  p.storageStatus = "COPYING SAMPLE INTO PROJECT";
  startPendingSampleLoad(p);
}

void finishProjectCollection(Plugin &p,
                             const storage::ProjectCopyResult &copy) {
  p.resolvedSamplePath = copy.absolutePath;
  p.sampleFromEmbedded = false;
  storage::ProjectLocation location;
  std::string relative, error;
  if (!queryStorageProject(p, location, error) ||
      !storage::makeProjectRelativePath(location, copy.absolutePath, relative,
                                        &error)) {
    // Save As can move the owning project while a long copy is running.
    // Retain a readable absolute locator until collection in its new media
    // directory completes; never attach an old-relative path to a new root.
    p.samplePath = copy.absolutePath;
    p.projectCopyPending = true;
    p.projectFileRegistration.clear();
    p.storageStatus = "PROJECT LOCATION CHANGED / COPY PENDING";
    p.nextProjectCopyProbe = {};
    return;
  }
  p.samplePath = std::move(relative);
  p.projectCopyPending = false;
  p.storageStatus = registerProjectSample(p)
                        ? "PROJECT SAMPLE READY"
                        : "PROJECT READY / MEDIA REGISTRATION UNAVAILABLE";
}

void servicePortableSampleLoad(Plugin &p) {
  serviceProjectSampleRename(p);
  if (p.sampleLoad.valid() && p.sampleLoad.wait_for(std::chrono::seconds(0)) ==
                                  std::future_status::ready) {
    auto result = p.sampleLoad.get();
    p.activeSampleRequest.reset();
    if (result.request.generation == p.sampleGeneration) {
      if (result.request.copyOnly) {
        if (result.copy.success) {
          finishProjectCollection(p, result.copy);
          markStorageDirty(p);
        } else {
          p.storageStatus = result.error;
          p.nextProjectCopyProbe =
              std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
      } else if (!result.asset) {
        p.sampleStatus = result.error;
      } else if (p.storageMode == StorageMode::Embed &&
                 result.asset->audio.size() * sizeof(float) >
                     kMaximumEmbeddedAudioBytes) {
        // Do not replace a usable sample with one that cannot be saved in
        // the selected mode. The user can choose PROJECT/LINK and load again.
        p.sampleStatus = "EMBED EXCEEDS 1 GIB / SELECT PROJECT OR LINK";
      } else {
        publishStorageSample(p, result.asset, result.request.path,
                             result.request.path, false);
        if (!result.request.preserveSettings)
          setParam(p, kOrderParamId,
                   s3g::ambiGrainOrderForChannels(result.asset->channels));
        p.sampleStatus.clear();
        p.storageStatus.clear();
        p.projectCopyPending =
            p.storageMode == StorageMode::Project && !result.copy.success;
        if (p.storageMode == StorageMode::Project) {
          if (result.copy.success)
            finishProjectCollection(p, result.copy);
          else {
            p.storageStatus = result.error;
            p.nextProjectCopyProbe =
                std::chrono::steady_clock::now() + std::chrono::seconds(1);
          }
        }
        markStorageDirty(p);
      }
    }
    startPendingSampleLoad(p);
  }
  maybeQueuePendingProjectCopy(p);
}

bool setStorageMode(Plugin &p, StorageMode mode) {
  mode = storage::sanitizeStorageMode(static_cast<uint8_t>(mode));
  if (mode == p.storageMode)
    return true;
  const auto sample = std::atomic_load(&p.sample);
  if (mode == StorageMode::Embed && !sample && !p.samplePath.empty()) {
    p.storageStatus = "MISSING SAMPLE / RELINK BEFORE EMBEDDING";
    return false;
  }
  if (mode == StorageMode::Embed && sample &&
      sample->audio.size() * sizeof(float) > kMaximumEmbeddedAudioBytes) {
    p.storageStatus = "EMBED EXCEEDS 1 GIB / USE PROJECT OR LINK";
    return false;
  }
  if (mode == StorageMode::Link && p.sampleFromEmbedded &&
      !sampleFileExists(p.resolvedSamplePath)) {
    p.storageStatus = "LINK NEEDS A SOURCE FILE / USE PROJECT OR RELINK";
    return false;
  }
  if (mode == StorageMode::Link && !p.samplePath.empty() &&
      p.resolvedSamplePath.empty()) {
    p.storageStatus = "LINK NEEDS A RESOLVED FILE / RELINK FIRST";
    return false;
  }
  // Changing STORE during a file read restarts the latest user request in
  // that mode; it must not resurrect a superseded read or lose the import.
  auto import =
      p.pendingSampleRequest ? p.pendingSampleRequest : p.activeSampleRequest;
  if (import && (import->copyOnly || import->generation != p.sampleGeneration))
    import.reset();
  cancelStorageLoads(p);
  p.projectFileRegistration.clear();
  p.projectRenamePending.store(false, std::memory_order_release);
  p.storageMode = mode;
  if (mode != StorageMode::Project)
    p.projectRecallPending = false;
  if (!p.resolvedSamplePath.empty())
    p.samplePath = p.resolvedSamplePath;
  if (mode == StorageMode::Link)
    p.sampleFromEmbedded = false;
  p.projectCopyPending = mode == StorageMode::Project && sample != nullptr;
  p.storageStatus = std::string("STORE ") + storage::storageModeName(mode);
  if (import)
    queuePortableSampleLoad(p, import->path, import->preserveSettings);
  else
    maybeQueuePendingProjectCopy(p);
  markStorageDirty(p);
  return true;
}

std::string storageSummary(const Plugin &p) {
  if (!p.storageStatus.empty())
    return p.storageStatus;
  const auto sample = std::atomic_load(&p.sample);
  if (!sample)
    return p.samplePath.empty() ? "LOAD A SAMPLE" : "MISSING SAMPLE / RELINK";
  if (p.storageMode == StorageMode::Embed &&
      sample->audio.size() * sizeof(float) > kMaximumEmbeddedAudioBytes)
    return "EMBED EXCEEDS 1 GIB / USE PROJECT OR LINK";
  return storage::formatByteCount(sample->audio.size() * sizeof(float)) +
         " DECODED" +
         (p.storageMode == StorageMode::Embed ? " / AUDIO IN STATE" : "");
}
