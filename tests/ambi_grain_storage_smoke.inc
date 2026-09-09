// Included by the actual Ambi Grain canvas test; no alternate storage model.
namespace canvas_test {
struct StorageHostFixture {
  inline static StorageHostFixture *active = nullptr;
  int project = 0, otherProject = 0, fx = 0;
  int additions = 0, removals = 0, dirty = 0;
  bool saved = false;
  std::filesystem::path root;
  std::string registeredPath;
  void *owner = nullptr;
  intptr_t (*renameCallback)(void *, int, void *) = nullptr;
  std::thread::id mainThread = std::this_thread::get_id();
  storage::ReaperHostBridge bridge{0, nullptr, registerObject, function};
  clap_host_state_t state{[](const clap_host_t *) { ++active->dirty; }};
  static void checkThread() {
    expect(std::this_thread::get_id() == active->mainThread,
           "project host calls must not run on the file worker");
  }
  static void *context(const clap_host_t *, int kind) {
    checkThread();
    return kind == 3 ? &active->project : kind == 4 ? &active->fx : nullptr;
  }
  static void *projects(int index, char *path, int capacity) {
    checkThread();
    if (index > 1)
      return nullptr;
    const auto file =
        index == 0
            ? (active->root / "other.rpp").u8string()
            : active->saved ? (active->root / "actual.rpp").u8string() : "";
    std::snprintf(path, size_t(capacity), "%s", file.c_str());
    return index == 0 ? &active->otherProject : &active->project;
  }
  static void projectPath(void *project, char *path, int capacity) {
    checkThread();
    expect(project == &active->project,
           "collection uses owning project, not active tab");
    std::snprintf(path, size_t(capacity), "%s", "Media");
  }
  static int registerObject(const char *name, void *arguments) {
    checkThread();
    auto **args = static_cast<void **>(arguments);
    if (std::strcmp(name, "file_in_project_ex2") == 0) {
      ++active->additions;
      active->registeredPath = static_cast<const char *>(args[0]);
      expect(args[1] == &active->project, "register media to correct project");
      active->owner = args[2];
      active->renameCallback =
          reinterpret_cast<decltype(renameCallback)>(args[3]);
      return 1;
    }
    if (std::strcmp(name, "-file_in_project_ex2") == 0) {
      ++active->removals;
      expect(active->registeredPath == static_cast<const char *>(args[0]),
             "unregister the current, possibly renamed media path");
      active->owner = nullptr;
      active->renameCallback = nullptr;
      return 1;
    }
    return 0;
  }
  static void *function(const char *name) {
    if (std::strcmp(name, "clap_get_reaper_context") == 0)
      return reinterpret_cast<void *>(context);
    if (std::strcmp(name, "EnumProjects") == 0)
      return reinterpret_cast<void *>(projects);
    if (std::strcmp(name, "GetProjectPathEx") == 0)
      return reinterpret_cast<void *>(projectPath);
    return nullptr;
  }
  static const void *extension(const clap_host_t *, const char *name) {
    checkThread();
    if (std::strcmp(name, "cockos.reaper_extension") == 0)
      return &active->bridge;
    if (std::strcmp(name, CLAP_EXT_STATE) == 0)
      return &active->state;
    return nullptr;
  }
};

void exerciseAmbiStorage(const clap_plugin_t *plugin, clap_host_t &host,
                         const std::filesystem::path &fixtureWave,
                         const std::filesystem::path &directory,
                         std::atomic<unsigned> &callbacks) {
  auto &p = *self(plugin);
  const auto root = directory / "storage-tests";
  std::filesystem::create_directory(root);
  const auto source = root / std::filesystem::u8path(u8"field 音 é.wav");
  std::filesystem::copy_file(fixtureWave, source);
  const auto service = [&] { servicePortableSampleLoad(p); };
  const auto drain = [&] {
    expect(waitFor([&] { return !p.sampleLoad.valid(); }, service),
           "storage worker completes");
  };
  const auto recall = [&](Memory &memory) {
    memory.offset = 0;
    return stateLoad(plugin, &memory.input);
  };
  const auto save = [&] {
    Memory memory;
    expect(stateSave(plugin, &memory.output), "save storage state");
    return memory;
  };
  // Memory contains callback self-pointers; refresh them after any copy/move.
  const auto bind = [](Memory &memory) {
    memory.input.ctx = &memory;
    memory.output.ctx = &memory;
  };
  const auto header = [](const Memory &memory) {
    StorageStateHeader value;
    std::memcpy(&value, memory.bytes.data() + sizeof(SavedState),
                sizeof(value));
    return value;
  };
  const auto legacyState = [&](const std::string &path) {
    SavedState legacy;
    legacy.version = kLegacyStateVersion;
    legacy.order = 3;
    legacy.density = 71;
    legacy.gainDb = -17;
    std::snprintf(legacy.samplePath, sizeof(legacy.samplePath), "%s",
                  path.c_str());
    Memory memory;
    memory.output.write(&memory.output, &legacy, sizeof(legacy));
    return memory;
  };
  expect(setStorageMode(p, StorageMode::Link), "select LINK");
  drain();
  auto legacy = legacyState(source.u8string());
  bind(legacy);
  expect(recall(legacy) && p.storageMode == StorageMode::Link &&
             p.samplePath == source.u8string(),
         "legacy path-only state recalls as LINK");
  expect(p.targets.order.load() == 3 && p.targets.density.load() == 71,
         "recall preserves saved order and parameters instead of "
         "auto-detecting again");
  const auto original = std::atomic_load(&p.sample);
  expect(original && original->channels == 4, "legacy audio loaded");
  auto linked = save();
  bind(linked);
  expect(!header(linked).embedded && linked.bytes.size() < 4096,
         "LINK stores path, not PCM");

  auto missing = legacyState((root / "missing.wav").u8string());
  bind(missing);
  expect(recall(missing) && !std::atomic_load(&p.sample) &&
             p.sampleStatus.find("MISSING") != std::string::npos,
         "missing-file recall clears previous audio and reports failure");
  expect(!setStorageMode(p, StorageMode::Embed),
         "cannot claim missing audio is embedded");
  auto offline = save();
  bind(offline);
  const auto offlinePath = p.samplePath;
  expect(recall(offline) && p.samplePath == offlinePath,
         "missing locator survives repeated saves");
  {
    ambi_grain_canvas::Editor view(p);
    render(view, "ambi-grain-missing");
  }
  queuePortableSampleLoad(p, source.u8string(), true);
  expect(waitFor([&] { return !p.sampleLoad.valid(); },
                 [&] {
                   if (callbacks.exchange(0))
                     plugin->on_main_thread(plugin);
                 }),
         "relink completes through host callback with no open GUI");
  expect(std::atomic_load(&p.sample) && p.targets.order.load() == 3 &&
             p.targets.gainDb.load() == -17,
         "relink keeps existing parameter settings");
  {
    ambi_grain_canvas::Editor view(p);
    render(view, "ambi-grain-storage-link");
    click(view, 65, 588);
    render(view, "ambi-grain-storage-menu");
    click(view, 65, 568);
    expect(p.storageMode == StorageMode::Embed,
           "STORE canvas menu selects EMBED");
    render(view, "ambi-grain-storage-embed");
    click(view, 65, 588);
    click(view, 65, 548);
    expect(p.storageMode == StorageMode::Link,
           "STORE canvas menu selects LINK");
  }

  // A long UTF-8 offline locator is not truncated to the old 1023-byte limit.
  p.samplePath = (root / "offline").u8string();
  for (unsigned i = 0; i < 250; ++i)
    p.samplePath += u8"/深い-é";
  p.samplePath += "/field.wav";
  const auto longPath = p.samplePath;
  auto longState = save();
  bind(longState);
  expect(recall(longState) && p.samplePath == longPath &&
             !std::atomic_load(&p.sample),
         "long Unicode locators survive offline save/recall");

  Memory embedded;
  for (uint32_t channels : {4u, 9u, 16u}) {
    auto asset = std::make_shared<s3g::AmbiGrainSample>();
    asset->channels = channels;
    asset->frames = 512;
    asset->sampleRate = 48000;
    asset->audio.resize(size_t(channels) * asset->frames);
    for (uint32_t frame = 0; frame < asset->frames; ++frame)
      for (uint32_t ch = 0; ch < channels; ++ch)
        asset->audio[size_t(frame) * channels + ch] =
            float(std::sin(frame * .05) * .2 / (ch + 1));
    s3g::normalizeAmbiGrainSample(*asset);
    publishStorageSample(p, asset, (root / "absent-source.wav").u8string(), "",
                         true);
    expect(setStorageMode(p, StorageMode::Embed), "select EMBED");
    embedded = save();
    bind(embedded);
    expect(header(embedded).embedded == 1 && recall(embedded),
           "embedded state restores without source file");
    const auto restored = std::atomic_load(&p.sample);
    expect(restored && restored->channels == channels &&
               restored->audio == asset->audio &&
               restored->normalizationGain == asset->normalizationGain &&
               restored->sourcePeak == asset->sourcePeak,
           "EMBED preserves every channel, PCM sample and normalization "
           "metadata exactly");
  }
  expect(!setStorageMode(p, StorageMode::Link),
         "embedded-only audio cannot silently become a broken LINK");

  const auto unchanged = std::atomic_load(&p.sample);
  const auto rejects = [&](Memory bad, const char *message) {
    bind(bad);
    expect(!recall(bad) && std::atomic_load(&p.sample) == unchanged &&
               p.storageMode == StorageMode::Embed,
           message);
  };
  auto bad = embedded;
  bad.bytes.pop_back();
  rejects(bad, "truncated PCM is rejected transactionally");
  bad = embedded;
  auto h = header(bad);
  h.frames = UINT32_MAX;
  std::memcpy(bad.bytes.data() + sizeof(SavedState), &h, sizeof(h));
  rejects(bad, "oversized embedded allocation is rejected before allocation");
  bad = embedded;
  h = header(bad);
  h.pathBytes = kMaximumSamplePathBytes + 1;
  std::memcpy(bad.bytes.data() + sizeof(SavedState), &h, sizeof(h));
  rejects(bad, "oversized path is rejected");
  bad = embedded;
  h = header(bad);
  h.mode = 99;
  std::memcpy(bad.bytes.data() + sizeof(SavedState), &h, sizeof(h));
  rejects(bad, "invalid storage mode is rejected");
  bad = embedded;
  h = header(bad);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::memcpy(bad.bytes.data() + sizeof(SavedState) + sizeof(h) + h.pathBytes,
              &nan, sizeof(nan));
  rejects(bad, "non-finite PCM is rejected");
  bad = embedded;
  const double badParam = std::numeric_limits<double>::quiet_NaN();
  std::memcpy(bad.bytes.data() + offsetof(SavedState, density), &badParam,
              sizeof(badParam));
  rejects(bad, "non-finite parameters are rejected");
  bad = embedded;
  h = header(bad);
  h.mode = 0;
  std::string traversal = "../outside.wav";
  bad.bytes.erase(bad.bytes.begin() + sizeof(SavedState) + sizeof(h),
                  bad.bytes.begin() + sizeof(SavedState) + sizeof(h) +
                      h.pathBytes);
  bad.bytes.insert(bad.bytes.begin() + sizeof(SavedState) + sizeof(h),
                   traversal.begin(), traversal.end());
  h.pathBytes = uint32_t(traversal.size());
  std::memcpy(bad.bytes.data() + sizeof(SavedState), &h, sizeof(h));
  rejects(bad, "unsafe project-relative traversal is rejected");

  const auto preset =
      root / std::filesystem::u8path(u8"field preset 音.s3gpreset");
  expect(saveAmbiPresetFile(plugin, stateExt, preset.u8string()),
         "save embedded Unicode preset file");
  std::string beforeHash, afterHash, presetError;
  uint64_t beforeBytes = 0, afterBytes = 0;
  expect(
      storage::detail::hashFile(preset, beforeHash, beforeBytes, presetError),
      "hash saved preset");
  const clap_plugin_state_t failedWriter{
      [](const clap_plugin_t *, const clap_ostream_t *output) {
        output->write(output, "bad", 3);
        return false;
      },
      nullptr};
  expect(!saveAmbiPresetFile(plugin, failedWriter, preset.u8string()) &&
             storage::detail::hashFile(preset, afterHash, afterBytes,
                                       presetError) &&
             beforeHash == afterHash && beforeBytes == afterBytes,
         "failed preset save leaves the previous preset intact");
  expect(s3g::clap_gui::portable::loadStateFile(plugin, stateExt,
                                                preset.u8string().c_str()) &&
             std::atomic_load(&p.sample)->audio == unchanged->audio,
         "saved embedded preset remains loadable after failed replacement");

  StorageHostFixture fake;
  fake.root = root / "project";
  std::filesystem::create_directory(fake.root);
  StorageHostFixture::active = &fake;
  const auto previousExtension = host.get_extension;
  host.get_extension = StorageHostFixture::extension;
  expect(setStorageMode(p, StorageMode::Project),
         "select PROJECT for embedded audio");
  expect(p.projectCopyPending && !p.sampleLoad.valid(),
         "unsaved PROJECT collection stays pending");
  auto pending = save();
  bind(pending);
  expect(header(pending).mode == 0 && header(pending).embedded == 1,
         "pending PROJECT retains source-less embedded audio in state");
  fake.saved = true;
  p.nextProjectCopyProbe = {};
  service();
  drain();
  expect(!p.projectCopyPending && p.projectFileRegistration.registered() &&
             p.samplePath.find("s3g Samples/") == 0 &&
             sampleFileExists(p.resolvedSamplePath),
         "embedded audio materializes into registered project media");
  const auto materialized = readSampleFromPath(p.resolvedSamplePath);
  expect(materialized && materialized->channels == 16,
         "PROJECT export preserves all 16 channels");
  if (materialized && unchanged) {
    float largest = 0;
    for (size_t i = 0; i < materialized->audio.size(); ++i)
      largest = std::max(
          largest, std::abs(materialized->audio[i] - unchanged->audio[i]));
    expect(largest < .000001f, "materialized field is audio-equivalent");
  }
  auto collectedEmbed = save();
  bind(collectedEmbed);
  expect(!header(collectedEmbed).embedded,
         "collected PROJECT state no longer contains PCM");

  queuePortableSampleLoad(p, source.u8string());
  drain();
  const auto collectedPath = p.resolvedSamplePath;
  expect(p.samplePath.find("s3g Samples/") == 0 && !p.projectCopyPending,
         "file import is collected");
  std::string originalHash, copyHash, error;
  uint64_t originalBytes = 0, copyBytes = 0;
  expect(
      storage::detail::hashFile(source, originalHash, originalBytes, error) &&
          storage::detail::hashFile(std::filesystem::u8path(collectedPath),
                                    copyHash, copyBytes, error) &&
          originalHash == copyHash && originalBytes == copyBytes,
      "PROJECT copies the unchanged source bytes");
  queuePortableSampleLoad(p, source.u8string());
  drain();
  expect(p.resolvedSamplePath == collectedPath,
         "identical source reuses collected media");
  auto projectState = save();
  bind(projectState);
  expect(!header(projectState).embedded && projectState.bytes.size() < 4096,
         "collected PROJECT state stays small");
  // A host may not expose project context until after initial state recall.
  host.get_extension = previousExtension;
  expect(recall(projectState) && !std::atomic_load(&p.sample) &&
             p.projectRecallPending,
         "unavailable project context retains a pending relative locator");
  host.get_extension = StorageHostFixture::extension;
  p.nextProjectCopyProbe = {};
  service();
  drain();
  expect(std::atomic_load(&p.sample) && !p.projectRecallPending,
         "relative recall recovers when the owning project context becomes "
         "available");
  const auto movedProject =
      root / std::filesystem::u8path(u8"moved project 音");
  std::filesystem::rename(fake.root, movedProject);
  fake.root = movedProject;
  std::filesystem::rename(source, root / "source-away.wav");
  expect(recall(projectState) && std::atomic_load(&p.sample) &&
             p.resolvedSamplePath.find(movedProject.u8string()) == 0,
         "project-relative media recalls after moving project and removing "
         "original file");
  const auto renamed =
      movedProject / "Media" / "s3g Samples" / "save-as-renamed.wav";
  std::filesystem::rename(std::filesystem::u8path(p.resolvedSamplePath),
                          renamed);
  fake.registeredPath = renamed.u8string();
  expect(fake.renameCallback != nullptr,
         "registered media has Save As callback");
  if (fake.renameCallback)
    fake.renameCallback(fake.owner, 0,
                        const_cast<char *>(fake.registeredPath.c_str()));
  auto renamedState = save();
  bind(renamedState);
  expect(p.samplePath == "s3g Samples/save-as-renamed.wav" &&
             recall(renamedState) && std::atomic_load(&p.sample),
         "Save As callback persists updated relative locator");
  expect(setStorageMode(p, StorageMode::Link) &&
             !p.projectFileRegistration.registered() &&
             p.samplePath == renamed.u8string(),
         "LINK clears media registration and keeps resolved path");
  // Changing STORE while the user import is still queued must keep that load.
  queuePortableSampleLoad(p, renamed.u8string());
  expect(setStorageMode(p, StorageMode::Embed), "change mode during import");
  drain();
  auto switched = save();
  bind(switched);
  expect(header(switched).embedded && p.samplePath == renamed.u8string(),
         "in-flight import honors latest STORE choice");

  // A copy that started in the old project must not save a stale relative
  // locator if Save As changes the effective media root before publication.
  expect(setStorageMode(p, StorageMode::Project), "return to PROJECT");
  drain();
  storage::ProjectCopyResult oldCopy;
  oldCopy.success = true;
  oldCopy.absolutePath = p.resolvedSamplePath;
  oldCopy.relativePath = p.samplePath;
  fake.root = root / "save-as-during-copy";
  std::filesystem::create_directory(fake.root);
  finishProjectCollection(p, oldCopy);
  expect(p.projectCopyPending && p.samplePath == oldCopy.absolutePath,
         "old collection keeps readable absolute fallback after project "
         "relocation");
  service();
  drain();
  expect(!p.projectCopyPending &&
             p.resolvedSamplePath.find(fake.root.u8string()) == 0,
         "old collection is recollected into the new project root");

  // In an unsaved project, an ordinary file-backed source stays small and
  // usable while collection waits; it is not silently embedded.
  fake.saved = false;
  expect(setStorageMode(p, StorageMode::Link) &&
             setStorageMode(p, StorageMode::Project),
         "select pending file-backed PROJECT");
  auto pendingLinked = save();
  bind(pendingLinked);
  expect(p.projectCopyPending && !header(pendingLinked).embedded &&
             std::atomic_load(&p.sample),
         "pending file-backed PROJECT keeps its absolute file locator without "
         "PCM");

  // Empty states actually unload, rather than retaining unrelated old audio.
  auto empty = legacyState("");
  bind(empty);
  expect(recall(empty) && !std::atomic_load(&p.sample) &&
             p.samplePath.empty() && p.storageMode == StorageMode::Project,
         "empty legacy state unloads audio and uses default PROJECT");
  p.projectFileRegistration.clear();
  expect(fake.additions > 0 && fake.additions == fake.removals &&
             fake.dirty > 0,
         "project registrations balance and storage changes dirty host state");
  host.get_extension = previousExtension;
  StorageHostFixture::active = nullptr;
  setStorageMode(p, StorageMode::Link);
  std::filesystem::remove_all(root);
}
} // namespace canvas_test
