void serviceStorage(Plugin& p) {
#if defined(S3G_SAMPLE_FILE_WORKER)
    if (p.resetAllPhase.load() || p.storageMode != StorageMode::Project
        || std::chrono::steady_clock::now() < p.nextCollection) return;
    p.nextCollection = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto context = s3g::sample_storage::reaperContext(p.host);
    s3g::sample_storage::ProjectLocation project;
    std::string error;
    if (!s3g::sample_storage::queryProjectLocation(context, project, &error)) {
        p.workflowMessage = "PROJECT PENDING / SAVE PROJECT; GENERATED AUDIO STAYS EMBEDDED";
        return;
    }
    for (unsigned pad = 0u; pad < 32u; ++pad) for (unsigned layer = 0u; layer < stackCount(p, pad); ++layer) {
        auto& s = p.sources[pad][layer];
        auto& registration = p.registrations[pad][layer];
        if (registration.registered()) {
            s.path = registration.absolutePath(); s.relative = false;
            if (p.selectedLayers[pad].load() == layer) p.samplePaths[pad] = s.path;
        }
        if (s.relative) {
            std::string absolute;
            if (s3g::sample_storage::resolveProjectRelativePath(project, s.path, absolute)) {
                s.path = absolute; s.relative = false;
                if (!s.asset) queueSampleLoad(p, pad, absolute, false, layer);
            }
        }
        if (!s.asset || p.collectionPending[pad][layer]) continue;
        std::string relative;
        if (!s.path.empty() && s3g::sample_storage::makeProjectRelativePath(project, s.path, relative)) {
            if (!registration.registered()) (void)registration.reset(context, s.path, nullptr, nullptr, "s3g Sample Neon 32");
            continue;
        }
        LoadRequest request;
        request.slot = static_cast<uint8_t>(pad); request.layer = static_cast<uint8_t>(layer);
        request.generation = p.layerGenerations[pad][layer];
        request.path = s.path; request.asset = s.asset; request.project = project;
        request.collectOnly = true; request.dirty = false;
        p.collectionPending[pad][layer] = true;
        { std::lock_guard<std::mutex> lock(p.loaderMutex); p.loadRequests.push_back(std::move(request)); }
        p.loaderCondition.notify_one();
    }
#else
    (void)p;
#endif
}

struct StackState {
    StorageMode mode = StorageMode::Link;
    std::array<std::array<LayerSource, 32u>, 32u> sources;
    std::array<uint8_t, 32u> counts {}, selected {}, modes {}, paths {};
    std::array<float, 32u> seconds {}, beats {};
    std::shared_ptr<const SampleAsset> capture;
};
// Explicit scalar fields: no pointers, platform padding, or mutable PCM.
template<class T> bool writeStackValue(const clap_ostream_t* stream, const T& value) {
    return s3g::clap_state::writeAll(stream, &value, sizeof(value));
}
template<class T> bool readStackValue(const clap_istream_t* stream, T& value) {
    return s3g::clap_state::readAll(stream, &value, sizeof(value));
}
bool writeStackState(Plugin& p, const clap_ostream_t* stream) {
    const auto mode = static_cast<uint8_t>(p.storageMode);
    if (!writeStackValue(stream, mode)) return false;
    std::array<const SampleAsset*, 1025u> embedded {};
    s3g::sample_storage::ProjectLocation project;
    (void)s3g::sample_storage::queryProjectLocation(s3g::sample_storage::reaperContext(p.host), project);
    for (unsigned pad = 0u; pad < 32u; ++pad) {
        saveLayerEdit(p, pad);
        const auto count = static_cast<uint8_t>(stackCount(p, pad));
        const std::array<uint8_t, 4u> choices {{count, p.selectedLayers[pad].load(), p.sourceModes[pad].load(), p.stackPaths[pad].load()}};
        if (!writeStackValue(stream, choices) || !writeStackValue(stream, p.stackSeconds[pad].load())
            || !writeStackValue(stream, p.stackBeats[pad].load())) return false;
        for (unsigned layer = 0u; layer < count; ++layer) {
            const auto& s = p.sources[pad][layer];
            const std::array<double, 4u> edit {{s.start, s.end, s.cursor, s.bpm}};
            const std::array<uint8_t, 4u> chop {{s.slices.sliceCount, s.chopMode, s.beatDivision, s.transientLimit}};
            std::string path = p.registrations[pad][layer].registered()
                ? p.registrations[pad][layer].absolutePath() : s.path;
            uint8_t relative = s.relative ? 1u : 0u;
            if (p.storageMode == StorageMode::Project && !relative && !path.empty()) {
                std::string local;
                if (s3g::sample_storage::makeProjectRelativePath(project, path, local)) { path = local; relative = 1u; }
            }
            const bool embed = p.storageMode == StorageMode::Embed || path.empty();
            if (embed) embedded[pad * 32u + layer] = s.asset.get();
            const auto length = static_cast<uint32_t>(path.size());
            if (length > 32768u || !writeStackValue(stream, edit) || !writeStackValue(stream, chop)
                || !writeStackValue(stream, s.preRoll) || !writeStackValue(stream, s.slices.boundaries)
                || !writeStackValue(stream, relative) || !writeStackValue(stream, length)
                || !s3g::clap_state::writeAll(stream, path.data(), length)) return false;
        }
    }
    embedded.back() = p.captureAsset.get();
    return writeEmbeddedAudio(stream, embedded);
}
bool readStackState(const clap_istream_t* stream, StackState& state) {
    uint8_t mode = 0u;
    if (!readStackValue(stream, mode) || mode > 2u) return false;
    state.mode = static_cast<StorageMode>(mode);
    for (unsigned pad = 0u; pad < 32u; ++pad) {
        std::array<uint8_t, 4u> choices {};
        if (!readStackValue(stream, choices) || choices[0] > 32u || choices[1] >= std::max<unsigned>(1u, choices[0])
            || choices[2] > 4u || choices[3] > 3u || !readStackValue(stream, state.seconds[pad])
            || !readStackValue(stream, state.beats[pad]) || !std::isfinite(state.seconds[pad])
            || !std::isfinite(state.beats[pad]) || state.seconds[pad] < 0.05f || state.seconds[pad] > 30.0f
            || state.beats[pad] < 0.25f || state.beats[pad] > 32.0f) return false;
        state.counts[pad] = choices[0]; state.selected[pad] = choices[1]; state.modes[pad] = choices[2]; state.paths[pad] = choices[3];
        for (unsigned layer = 0u; layer < choices[0]; ++layer) {
            auto& s = state.sources[pad][layer];
            std::array<double, 4u> edit {}; std::array<uint8_t, 4u> chop {};
            uint8_t relative = 0u; uint32_t length = 0u;
            if (!readStackValue(stream, edit) || !readStackValue(stream, chop)
                || !readStackValue(stream, s.preRoll) || !readStackValue(stream, s.slices.boundaries)
                || !readStackValue(stream, relative) || relative > 1u || !readStackValue(stream, length) || length > 32768u) return false;
            for (double x : edit) if (!std::isfinite(x)) return false;
            if (edit[0] < 0.0 || edit[1] > 1.0 || edit[0] >= edit[1] || edit[2] < 0.0 || edit[2] > 1.0
                || edit[3] < 20.0 || edit[3] > 999.0 || chop[1] > 3u || chop[2] > 4u || !chop[3] || chop[3] > 32u
                || !std::isfinite(s.preRoll) || s.preRoll < 0.0f || s.preRoll > 50.0f) return false;
            s.start = edit[0]; s.end = edit[1]; s.cursor = edit[2]; s.bpm = edit[3];
            s.slices.sliceCount = chop[0]; s.chopMode = chop[1]; s.beatDivision = chop[2]; s.transientLimit = chop[3];
            if (!s.slices.valid()) return false;
            s.path.resize(length); s.relative = relative != 0u;
            if (!s3g::clap_state::readAll(stream, s.path.data(), length) || s.path.find('\0') != std::string::npos
                || (s.relative && !s3g::sample_storage::relativePathIsSafe(std::filesystem::u8path(s.path)))) return false;
        }
    }
    std::array<std::shared_ptr<const SampleAsset>, 1025u> audio {};
    if (!readEmbeddedAudio(stream, audio)) return false;
    for (unsigned pad = 0u; pad < 32u; ++pad) {
        unsigned channels = 0u;
        for (unsigned layer = 0u; layer < 32u; ++layer) {
            auto& s = state.sources[pad][layer]; s.asset = audio[pad * 32u + layer];
            if (layer >= state.counts[pad] && s.asset) return false;
            if (s.asset) {
                if (channels && channels != s.asset->channelCount) return false;
                channels = s.asset->channelCount;
                s.analysis = s3g::sample::analyzeCutupsAsset(*s.asset, 64u, 5.0, 1000u, 20.0);
            }
            // A rejected multi-file import may leave an empty interior layer.
            // Only the final counted layer must actually have audio/a locator.
            if (layer + 1u == state.counts[pad] && !s.asset && s.path.empty()) return false;
        }
    }
    state.capture = audio.back(); return true;
}
void applyStackState(Plugin& p, StackState& state) {
    p.storageMode = state.mode; p.nextCollection = {};
    for (unsigned pad = 0u; pad < 32u; ++pad) {
        invalidateLayerLoads(p, pad);
        for (auto& r : p.registrations[pad]) r.clear();
        p.sources[pad] = std::move(state.sources[pad]);
        p.sourceModes[pad].store(state.modes[pad]); p.stackPaths[pad].store(state.paths[pad]);
        p.stackSeconds[pad].store(state.seconds[pad]); p.stackBeats[pad].store(state.beats[pad]);
        for (unsigned layer = 0u; layer < state.counts[pad]; ++layer) {
            auto& s = p.sources[pad][layer];
            if (s.asset) p.retainedAssets.push_back(s.asset);
#if defined(S3G_SAMPLE_FILE_WORKER)
            if (s.relative) {
                std::string absolute;
                if (s3g::sample_storage::resolveProjectRelativePath(s3g::sample_storage::reaperContext(p.host), s.path, absolute)) {
                    s.path = absolute; s.relative = false;
                }
            }
            if (!s.asset && !s.relative && !s.path.empty()) queueSampleLoad(p, pad, s.path, false, layer);
#endif
        }
        if (!state.counts[pad]) {
            // Empty pads still have automatable trim/edit controls, restored
            // from the common state prefix. No layer record overrides them.
            p.selectedLayers[pad].store(0u);
            saveLayerEdit(p, pad);
        }
        showLayer(p, pad, state.selected[pad], false);
    }
}
