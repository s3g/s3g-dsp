// Main-thread, instance-local layer clipboard. Layer operations leave pad
// playback/FX/routing alone; cell and layer clipboards are independent.
struct LayerClipboard {
    LayerSource source;
    double format = 0;
};
bool layerOccupied(const Plugin& p, std::size_t pad, unsigned layer) {
    return pad < 32 && layer < 32 && (p.sources[pad][layer].asset
        || !p.sources[pad][layer].path.empty() || p.pendingSourceLoads[pad][layer]);
}
std::string clipboardTarget(std::size_t pad, int layer = -1) {
    return std::string(1, static_cast<char>('A' + pad / 8)) + std::to_string(pad % 8 + 1)
        + (layer < 0 ? "" : " L" + std::to_string(layer + 1));
}
bool clipboardEditReady(Plugin& p) {
    if (!historyBusy(p)) return true;
    p.workflowMessage = "FINISH RECORDING / LOADING / PENDING EDIT BEFORE SOURCE EDITS";
    return false;
}
void invalidateClipboardLayer(Plugin& p, std::size_t pad, unsigned layer) {
    p.pendingSourceLoads[pad][layer] = false;
#if defined(S3G_SAMPLE_FILE_WORKER)
    ++p.layerGenerations[pad][layer];
#endif
    p.collectionPending[pad][layer] = false;
    p.registrations[pad][layer].clear();
}
bool copyLayer(Plugin& p, std::size_t pad, unsigned layer) {
    if (pad >= 32 || layer >= 32 || p.resetAllPhase.load()) return false;
    if (!p.sources[pad][layer].asset || p.pendingSourceLoads[pad][layer]) {
        p.workflowMessage = "COPY NEEDS A LOADED LAYER"; return false;
    }
    try {
        saveLayerEdit(p, pad);
        auto copy = std::make_shared<LayerClipboard>();
        copy->source = p.sources[pad][layer];
        // Worker requests belong to the original cell, not to the clipboard.
        copy->source.waveAnalysisPending = false;
        if (!copy->source.wavesets) copy->source.waveAnalysisAttempted = nullptr;
        copy->format = paramValue(p, slotParamId(pad, kSlotSourceFormat));
        p.layerClipboard = std::move(copy);
        p.workflowMessage = "LAYER COPIED: AUDIO + TRIM + SLICES + BPM";
        return true;
    } catch (...) { p.workflowMessage = "COPY FAILED; LAYER CLIPBOARD KEPT"; return false; }
}
bool pasteLayer(Plugin& p, std::size_t pad, unsigned layer) {
    const auto copy = p.layerClipboard;
    if (pad >= 32 || layer >= 32 || !copy || !clipboardEditReady(p)) return false;
    bool otherLayers = false;
    for (unsigned n = 0; n < 32; ++n) if (n != layer) otherLayers |= layerOccupied(p, pad, n);
    if (!layerWidthCompatible(p, pad, layer, *copy->source.asset)
        || (otherLayers && paramValue(p, slotParamId(pad, kSlotSourceFormat)) != copy->format)) {
        p.workflowMessage = "LAYER CHANNEL COUNT / FORMAT MUST MATCH THE STACK"; return false;
    }
    NeonAudioBudget budget;
    for (unsigned n = 0; n < 32; ++n) budget.pad(p, n, n == pad ? static_cast<int>(layer) : -1);
    budget.add(p.captureAsset.get());
    if (p.storageMode == StorageMode::Embed || copy->source.path.empty()) budget.add(copy->source.asset.get());
    if (budget.samples > kMaximumEmbeddedSamples) {
        p.workflowMessage = "SET AUDIO LIMIT REACHED; NOTHING REPLACED"; return false;
    }
    const bool linkedTake = savedCaptureValid(p) && p.captureSavedPad == pad && p.captureSavedLayer == layer;
    NeonHistoryEdit edit(p, "PASTE " + clipboardTarget(pad, static_cast<int>(layer)), 1u << pad, linkedTake);
    if (!edit) return false;
    try {
        auto source = copy->source;
        p.retainedAssets.push_back(source.asset);
        edit.commit(); // Recoverable even if a later publication allocation fails.
        invalidateClipboardLayer(p, pad, layer);
        p.sources[pad][layer] = std::move(source);
        if (linkedTake) forgetSavedCapture(p); // Review remains auditionable.
        // A sole/new source adopts its format; existing mixed-format stacks
        // are rejected above. The normal ACN/SN3D safety clamps still apply.
        if (!otherLayers) queueGuiParamGesture(p, slotParamId(pad, kSlotSourceFormat), copy->format);
        showLayer(p, pad, layer, false);
        p.pendingCellStops.fetch_or(1u << pad);
        p.workflowMessage = "LAYER PASTED; PAD SETTINGS KEPT / UNDO AVAILABLE";
        return true;
    } catch (...) { p.workflowMessage = "LAYER PASTE FAILED / USE UNDO TO RESTORE"; return false; }
}
bool clearCell(Plugin& p, std::size_t pad) {
    if (pad >= 32 || !stackCount(p, pad) || !clipboardEditReady(p)) return false;
    const bool linkedTake = savedCaptureValid(p) && p.captureSavedPad == pad;
    NeonHistoryEdit edit(p, "CLEAR " + clipboardTarget(pad), 1u << pad, linkedTake);
    if (!edit) return false;
    edit.commit();
    invalidateLayerLoads(p, pad);
    for (auto& registration : p.registrations[pad]) registration.clear();
    p.sources[pad] = {};
    if (linkedTake) forgetSavedCapture(p);
    showLayer(p, pad, 0, false);
    p.pendingCellStops.fetch_or(1u << pad);
    uint8_t previous = static_cast<uint8_t>(pad);
    p.lastPlayedCells[pad / 8].compare_exchange_strong(previous, 0xffu);
    p.workflowMessage = "CELL AUDIO CLEARED / UNDO AVAILABLE / FILES ON DISK KEPT";
    return true;
}
bool clearLayer(Plugin& p, std::size_t pad, unsigned layer) {
    if (!layerOccupied(p, pad, layer) || !clipboardEditReady(p)) return false;
    const bool linkedTake = savedCaptureValid(p) && p.captureSavedPad == pad && p.captureSavedLayer == layer;
    NeonHistoryEdit edit(p, "CLEAR " + clipboardTarget(pad, static_cast<int>(layer)), 1u << pad, linkedTake);
    if (!edit) return false;
    edit.commit();
    invalidateClipboardLayer(p, pad, layer);
    p.sources[pad][layer] = {};
    if (linkedTake) forgetSavedCapture(p);
    // Keep layer numbers stable, including an empty primary or middle layer.
    const auto count = stackCount(p, pad);
    showLayer(p, pad, std::min<unsigned>(p.selectedLayers[pad].load(), count ? count - 1 : 0), false);
    p.pendingCellStops.fetch_or(1u << pad);
    p.workflowMessage = "LAYER CLEARED / OTHER LAYER NUMBERS KEPT / UNDO AVAILABLE";
    return true;
}
