// Main-thread source editing. The existing cell controls are the selected
// layer's editing view; performance selection is an independent saved choice.
unsigned stackCount(const Plugin& p, std::size_t slot) {
    unsigned count = 0u;
    for (unsigned n = 0u; n < 32u; ++n)
        if (p.sources[slot][n].asset || !p.sources[slot][n].path.empty() || p.pendingSourceLoads[slot][n]) count = n + 1u;
    return count;
}
const SampleAsset* renderBase(const s3g::sample::NeonStack* stack) noexcept {
    if (stack) for (unsigned n = 0u; n < stack->count; ++n)
        if (stack->layers[n].asset) return stack->layers[n].asset;
    return nullptr;
}
void saveLayerEdit(Plugin& p, std::size_t slot) {
    auto& s = p.sources[slot][p.selectedLayers[slot].load()];
    s.start = paramValue(p, slotParamId(slot, kSlotStart));
    s.end = paramValue(p, slotParamId(slot, kSlotEnd));
    s.cursor = p.editPositions[slot].load(); s.bpm = p.sourceBpms[slot].load();
    s.chopMode = p.chopModes[slot].load(); s.beatDivision = p.chopBeatDivisions[slot].load();
    s.transientLimit = p.transientSliceLimits[slot].load(); s.preRoll = p.transientPreRollMs[slot].load();
    s.slices.sliceCount = p.sliceCounts[slot].load();
    for (unsigned n = 0u; n < 33u; ++n) s.slices.boundaries[n] = p.sliceBoundaries[slot][n].load();
    s.analysis = p.slotAnalyses[slot];
}
template<class Source> void appendMosaicFragments(s3g::sample::NeonStack& stack, const Source& source, unsigned layer) {
    if (!source.asset) return;
    const auto slices = source.slices.valid() ? source.slices : s3g::sample::equalSampleNeonSliceLayout(1);
    for (unsigned slice = 0; slice < slices.sliceCount; ++slice) {
        const double a = source.start + (source.end - source.start) * slices.boundaries[slice];
        const double b = source.start + (source.end - source.start) * slices.boundaries[slice + 1];
        stack.fragments[stack.fragmentCount++] = s3g::sample::neonDescribeFragment(stack.layers[layer], layer, a, b);
    }
}
void publishStack(Plugin& p, std::size_t slot) {
    uint32_t occupied = 0;
    for (unsigned n = 0; n < 32; ++n)
        if (p.sources[slot][n].asset || !p.sources[slot][n].path.empty() || p.pendingSourceLoads[slot][n]) occupied |= 1u << n;
    p.captureOccupied[slot].store(occupied, std::memory_order_release);
    refreshCaptureBudget(p);
    auto snapshot = std::make_shared<s3g::sample::NeonStack>();
    snapshot->count = static_cast<uint8_t>(stackCount(p, slot));
    for (unsigned n = 0u; n < snapshot->count; ++n) {
        auto& source = p.sources[slot][n];
        // Superseded maps stay in retainedWavesets until audio deactivation,
        // but must not keep obsolete PCM alive in the editable layer cache.
        if (source.wavesets && source.wavesets->asset != source.asset) source.wavesets.reset();
        snapshot->layers[n] = {source.asset.get(), source.start, source.end};
        snapshot->layers[n].wavesets = source.wavesets && source.wavesets->asset == source.asset ? source.wavesets.get() : nullptr;
        snapshot->cutupsMetadata[n] = source.analysis;
        snapshot->sourceBpms[n] = static_cast<float>(source.bpm);
        appendMosaicFragments(*snapshot, source, n);
        if (snapshot->layers[n].wavesets
            && std::find(p.retainedWavesets.begin(), p.retainedWavesets.end(), source.wavesets) == p.retainedWavesets.end())
            p.retainedWavesets.push_back(source.wavesets);
    }
    snapshot->primarySliceCount = p.sources[slot][0].slices.sliceCount;
    snapshot->primarySlices = p.sources[slot][0].slices.boundaries;
    p.retainedStacks.push_back(snapshot);
    p.publishedStacks[slot].store(snapshot.get(), std::memory_order_release);
    const auto& primary = p.sources[slot][0];
    p.controlWavesets[slot] = primary.wavesets && primary.wavesets->asset == primary.asset ? primary.wavesets : nullptr;
    p.publishedWavesets[slot].store(p.controlWavesets[slot].get(), std::memory_order_release);
}
void serviceMosaic(Plugin& p) {
    if (p.resetAllPhase.load()) return;
    for (unsigned slot = 0; slot < 32; ++slot) {
        if (p.textureOptions[slot].load() != kSequenceOption || p.familyControls[slot][neonFamilyIndex(NeonFamily::MosaicMode)].load() == 0) continue;
        const auto& source = p.sources[slot][p.selectedLayers[slot].load()];
        bool changed = source.start != paramValue(p, slotParamId(slot, kSlotStart))
            || source.end != paramValue(p, slotParamId(slot, kSlotEnd)) || source.slices.sliceCount != p.sliceCounts[slot].load();
        for (unsigned n = 0; n <= p.sliceCounts[slot].load(); ++n)
            changed |= source.slices.boundaries[n] != p.sliceBoundaries[slot][n].load();
        if (changed) { saveLayerEdit(p, slot); publishStack(p, slot); }
    }
}
void showLayer(Plugin& p, std::size_t slot, unsigned layer, bool savePrevious = true) {
    if (slot >= 32u || layer >= 32u) return;
    if (savePrevious) saveLayerEdit(p, slot);
    const auto& s = p.sources[slot][layer];
    p.selectedLayers[slot].store(static_cast<uint8_t>(layer));
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        p.controlAssets[slot] = s.asset; p.samplePaths[slot] = s.path;
        p.statuses[slot] = s.asset ? (s.path.empty() ? "CELL AUDIO" : sampleDisplayName(s.path)) : "MISSING / EMPTY LAYER";
        p.slotAnalyses[slot] = s.analysis;
        p.embeddedAssets[slot] = s.asset && s.path.empty();
        p.publishedAssets[slot].store(s.asset.get(), std::memory_order_release);
    }
    const auto base = kGlobalParamCount + slot * kSlotParameterCount;
    p.parameters[base + kSlotStart].store(s.start); p.parameters[base + kSlotEnd].store(s.end);
    p.editPositions[slot].store(s.cursor); p.sourceBpms[slot].store(s.bpm);
    p.chopModes[slot].store(s.chopMode); p.chopBeatDivisions[slot].store(s.beatDivision);
    p.transientPreRollMs[slot].store(s.preRoll); p.transientSliceLimits[slot].store(s.transientLimit);
    p.sliceCounts[slot].store(s.slices.sliceCount);
    for (unsigned n = 0u; n < 33u; ++n) p.sliceBoundaries[slot][n].store(s.slices.boundaries[n]);
    p.visibleSlice.store(0u); p.chopBank.store(0u); p.waveformZooms[slot].store(1.0f);
    publishStack(p, slot);
    p.ledFeedbackDirty.store(true); markStateDirty(p); requestProcess(p);
    if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
}
void invalidateLayerLoads(Plugin& p, std::size_t slot) {
    p.pendingSourceLoads[slot].fill(false);
#if defined(S3G_SAMPLE_FILE_WORKER)
    ++p.loadGenerations[slot];
    for (unsigned n = 0u; n < 32u; ++n) {
        ++p.layerGenerations[slot][n]; p.collectionPending[slot][n] = false;
        p.sources[slot][n].waveAnalysisPending = false;
        if (!p.sources[slot][n].wavesets) p.sources[slot][n].waveAnalysisAttempted = nullptr;
    }
#else
    (void)p; (void)slot;
#endif
}
void removeLayer(Plugin& p, std::size_t slot) {
    saveLayerEdit(p, slot);
    const unsigned layer = p.selectedLayers[slot].load(), count = stackCount(p, slot);
    if (!count) return;
    NeonHistoryEdit edit(p, "REMOVE " + std::string(1, static_cast<char>('A' + slot / 8))
        + std::to_string(slot % 8 + 1) + " L" + std::to_string(layer + 1), 1u << slot);
    if (!edit) return;
    invalidateLayerLoads(p, slot);
    for (auto& registration : p.registrations[slot]) registration.clear();
    for (unsigned n = layer; n + 1u < count; ++n) p.sources[slot][n] = std::move(p.sources[slot][n + 1u]);
    p.sources[slot][count - 1u] = {};
    showLayer(p, slot, std::min(layer, count > 1u ? count - 2u : 0u), false);
    p.pendingCellStops.fetch_or(1u << slot);
    p.workflowMessage = "LAYER REMOVED; FILES ON DISK KEPT";
    edit.commit();
}
bool layerWidthCompatible(const Plugin& p, std::size_t slot, unsigned layer, const SampleAsset& asset) {
    for (unsigned n = 0u; n < 32u; ++n)
        if (n != layer && p.sources[slot][n].asset
            && p.sources[slot][n].asset->channelCount != asset.channelCount) return false;
    return true;
}
bool fitsEmbeddedBudget(const Plugin& p, unsigned replacePad = 32u, unsigned replaceLayer = 32u,
    const SampleAsset* replacement = nullptr) {
    std::array<const SampleAsset*, 1025u> seen {}; unsigned count = 0u; uint64_t samples = 0u;
    const auto add = [&](const SampleAsset* asset) {
        if (!asset || std::find(seen.begin(), seen.begin() + count, asset) != seen.begin() + count) return;
        seen[count++] = asset; samples += static_cast<uint64_t>(asset->channelCount) * asset->frameCount();
    };
    for (unsigned pad = 0u; pad < 32u; ++pad) for (unsigned layer = 0u; layer < 32u; ++layer)
        if (pad != replacePad || layer != replaceLayer) add(p.sources[pad][layer].asset.get());
    add(p.captureAsset.get()); add(replacement);
    return samples <= 64u * 1024u * 1024u;
}
void chooseStorage(Plugin& p, StorageMode mode) {
    if (p.captureState.load() == Plugin::CaptureState::Recording || p.captureReady.load()) {
        p.workflowMessage = "STOP / FINISH RECORDING BEFORE CHANGING STORAGE"; return;
    }
    if (mode == StorageMode::Embed && !fitsEmbeddedBudget(p)) {
        p.workflowMessage = "EMBED EXCEEDS 256 MiB; STORAGE UNCHANGED"; return;
    }
    p.storageMode = mode; p.nextCollection = {};
    refreshCaptureBudget(p);
    if (mode != StorageMode::Project)
        for (auto& pad : p.registrations) for (auto& r : pad) r.clear();
    markStateDirty(p);
}
