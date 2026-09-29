// Destructive edit of one layer; original PCM stays immutable for Undo/voices.
void cropLayer(Plugin& p, unsigned slot, unsigned layer)
{
    if (slot >= 32 || layer >= 32 || historyBusy(p)) return;
    const auto original = p.sources[slot][layer].asset;
    if (!original || !original->frameCount()) return;
    const bool selected = p.selectedLayers[slot].load() == layer;
    const double a = selected ? paramValue(p, slotParamId(slot, kSlotStart)) : p.sources[slot][layer].start;
    const double b = selected ? paramValue(p, slotParamId(slot, kSlotEnd)) : p.sources[slot][layer].end;
    const auto total = original->frameCount();
    const auto first = std::min(total - 1u, static_cast<uint32_t>(std::llround(std::clamp(a, 0., 1.) * total)));
    const auto last = std::clamp(static_cast<uint32_t>(std::llround(std::clamp(b, 0., 1.) * total)), first + 1u, total);
    if (!first && last == total) { p.workflowMessage = "LAYER ALREADY FULL LENGTH; NOTHING CHANGED"; return; }
    NeonAudioBudget budget;
    for (unsigned pad = 0; pad < 32; ++pad) budget.pad(p, pad, pad == slot ? static_cast<int>(layer) : -1);
    budget.add(p.captureAsset.get());
    if (budget.samples + static_cast<uint64_t>(last - first) * original->channelCount > kMaximumEmbeddedSamples) {
        p.workflowMessage = "SET AUDIO LIMIT; ORIGINAL LAYER KEPT"; return;
    }
    NeonHistoryEdit edit(p, "CROP " + chopPadName(slot) + " L" + std::to_string(layer + 1), 1u << slot);
    if (!edit) return;
    try {
        saveLayerEdit(p, slot);
        auto sources = p.sources[slot];
        auto& source = sources[layer];
        source.asset = std::make_shared<SampleAsset>(s3g::sample::sampleNeonCrop(*original, first, last));
        source.analysis = s3g::sample::analyzeCutupsAsset(*source.asset, 64u, 5., 1000u, 20.);
        source.path.clear(); source.relative = false; source.wavesets.reset();
        source.waveAnalysisAttempted = nullptr; source.waveAnalysisPending = false;
        source.start = 0; source.end = 1;
        // Cursor and slice markers are relative to the old Start–End selection.
        // Keep them on the retained audio, along with BPM and slicing settings.
        auto snapshot = std::make_shared<s3g::sample::NeonStack>();
        snapshot->count = static_cast<uint8_t>(stackCount(p, slot));
        for (unsigned n = 0; n < 32; ++n) {
            const auto& s = sources[n];
            snapshot->layers[n] = {s.asset.get(), s.start, s.end};
            snapshot->layers[n].wavesets = s.wavesets.get();
            snapshot->cutupsMetadata[n] = s.analysis;
            snapshot->sourceBpms[n] = static_cast<float>(s.bpm);
            appendMosaicFragments(*snapshot, s, n);
        }
        snapshot->primarySliceCount = sources[0].slices.sliceCount;
        snapshot->primarySlices = sources[0].slices.boundaries;
        auto analysis = source.analysis;
        std::string status = "CROPPED LAYER";
        std::string message = "CROPPED " + chopPadName(slot) + " L" + std::to_string(layer + 1) + "; UNDO RESTORES OUTSIDE AUDIO";
        p.retainedAssets.reserve(p.retainedAssets.size() + 1);
        p.retainedStacks.reserve(p.retainedStacks.size() + 1);
        // All allocating preparation completes before replacing the source.
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
#if defined(S3G_SAMPLE_FILE_WORKER)
            ++p.layerGenerations[slot][layer]; p.collectionPending[slot][layer] = false;
#endif
            p.registrations[slot][layer].clear();
            p.retainedAssets.push_back(source.asset); p.retainedStacks.push_back(snapshot);
            p.sources[slot] = std::move(sources);
            if (selected) {
                p.controlAssets[slot] = p.sources[slot][layer].asset;
                p.samplePaths[slot].clear(); p.embeddedAssets[slot] = true;
                p.statuses[slot] = std::move(status); p.slotAnalyses[slot] = std::move(analysis);
                p.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotStart].store(0);
                p.parameters[kGlobalParamCount + slot * kSlotParameterCount + kSlotEnd].store(1);
                p.editPositions[slot].store(p.sources[slot][layer].cursor); p.waveformZooms[slot].store(1);
                p.publishedAssets[slot].store(p.controlAssets[slot].get(), std::memory_order_release);
            }
            p.controlWavesets[slot] = p.sources[slot][0].wavesets;
            p.publishedWavesets[slot].store(p.controlWavesets[slot].get(), std::memory_order_release);
            p.publishedStacks[slot].store(snapshot.get(), std::memory_order_release);
            p.pendingCellStops.fetch_or(1u << slot); p.ledFeedbackDirty.store(true);
            p.workflowMessage = std::move(message);
        }
        edit.commit(); refreshCaptureBudget(p);
        if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
        markStateDirty(p); requestProcess(p);
    } catch (...) { p.workflowMessage = "CROP FAILED; ORIGINAL LAYER KEPT"; }
}
