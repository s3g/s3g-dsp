void testLayerClipboard() {
    auto instance = historyFixture(); auto& p = *instance;
    auto a = historyAudio(4), b = historyAudio(4, .5f);
    historySource(p, 0, 0, a); historySource(p, 0, 1, b);
    setParam(p, slotParamId(0, kSlotZeroCross), 0);
    setParam(p, slotParamId(0, kSlotStart), .2);
    setParam(p, slotParamId(0, kSlotEnd), .7);
    p.sourceBpms[0].store(137); p.editPositions[0].store(.4);
    storeSliceLayout(p, 0, s3g::sample::equalSampleNeonSliceLayout(4), false);
    check(copyCell(p, 0) && copyLayer(p, 0, 1), "independent cell/layer clipboards");
    const auto cellCopy = p.cellClipboard; const auto layerCopy = p.layerClipboard;
    const auto original = historyState(p);
    check(clearLayer(p, 0, 1), "clear selected layer");
    check(!p.sources[0][1].asset && p.sources[0][0].asset == a && p.controlAssets[0] == a, "clear exact layer; move editor off trailing empty slot");
    check(!p.pendingAuditions.load() && !p.pendingLayerAuditions.load(), "clear does not audition");
    const auto cleared = historyState(p);
    const auto recalledState = [&](const std::vector<uint8_t>& bytes) {
        auto recalled = historyFixture();
        struct Input { const std::vector<uint8_t>& bytes; size_t at = 0; } input {bytes};
        clap_istream_t stream {&input, [](const clap_istream_t* stream, void* data, uint64_t length) -> int64_t {
            auto& in = *static_cast<Input*>(stream->ctx);
            const auto count = std::min<uint64_t>(length, in.bytes.size() - in.at);
            std::memcpy(data, in.bytes.data() + in.at, count); in.at += count; return static_cast<int64_t>(count);
        }};
        check(stateLoad(&recalled->plugin, &stream), "clipboard edited state recalls");
        check(historyState(*recalled) == bytes, "clipboard state roundtrip exact");
    };
    recalledState(cleared);
    historyStep(p); check(historyState(p) == original, "layer clear undo exact");
    historyStep(p, true); check(historyState(p) == cleared, "layer clear redo exact");
    check(p.layerClipboard == layerCopy && p.cellClipboard == cellCopy, "clear/undo retain both clipboards");
    p.textureOptions[8].store(kGrainsOption); p.fxParameters[8][2][1].store(.67f);
    setParam(p, slotParamId(8, kSlotOutputBus), 2);
    const auto beforePaste = historyState(p);
    check(pasteLayer(p, 8, 31), "paste into empty layer 32 of another pad");
    check(stackCount(p, 8) == 32 && p.selectedLayers[8].load() == 31 && p.sources[8][31].asset == b, "sparse stack paste shares PCM");
    const auto& source = p.sources[8][31];
    check(source.start == .2 && source.end == .7 && source.bpm == 137 && source.cursor == .4
        && source.slices.sliceCount == 4, "layer paste retains trim slices cursor BPM");
    check(p.textureOptions[8].load() == kGrainsOption && p.fxParameters[8][2][1].load() == .67f
        && paramValue(p, slotParamId(8, kSlotOutputBus)) == 2, "layer paste keeps pad sound/routing");
    const auto pasted = historyState(p);
    recalledState(pasted);
    historyStep(p); check(historyState(p) == beforePaste, "layer paste undo exact");
    historyStep(p, true); check(historyState(p) == pasted, "layer paste redo exact");
    check(pasteLayer(p, 8, 2), "paste second sparse layer");
    check(clearLayer(p, 8, 2) && p.sources[8][31].asset == b && !p.sources[8][2].asset,
        "clearing middle keeps later layer numbers");
    recalledState(historyState(p));
    check(copyCell(p, 8) && pasteCell(p, 9), "copy entire sparse stack with empty selected layer");
    check(p.sources[9][31].asset == b && !p.sources[9][0].asset, "cell copy preserves sparse indices");
    const auto beforeClear = historyState(p);
    check(clearCell(p, 8) && stackCount(p, 8) == 0 && !p.publishedAssets[8].load()
        && p.captureOccupied[8].load() == 0, "clear cell clears all stack audio/occupancy");
    check(p.sources[9][31].asset == b && p.fxParameters[8][2][1].load() == .67f, "clear cell preserves other pads and pad settings");
    historyStep(p); check(historyState(p) == beforeClear, "whole cell clear undo exact");
    // Two four-channel formats are not interchangeable within one stack.
    historySource(p, 10, 0, historyAudio(4)); setParam(p, slotParamId(10, kSlotSourceFormat), 1);
    const auto beforeReject = historyState(p); const auto historyCount = p.editHistory->undo.size();
    check(!pasteLayer(p, 10, 1) && historyState(p) == beforeReject, "quad into FOA stack rejected");
    historySource(p, 11, 0, historyAudio(2));
    check(!pasteLayer(p, 11, 1), "channel width mismatch rejected");
    check(p.editHistory->undo.size() == historyCount, "rejected edits add no undo entry");
    check(copyLayer(p, 10, 0) && pasteLayer(p, 12, 0)
        && paramValue(p, slotParamId(12, kSlotSourceFormat)) == 1, "empty pad adopts ACN/SN3D format");
    p.captureState.store(Plugin::CaptureState::Recording);
    const auto busy = historyState(p);
    check(!clearCell(p, 8) && !clearLayer(p, 10, 0) && !pasteLayer(p, 12, 1)
        && historyState(p) == busy, "recording blocks destructive clipboard commands");
    p.captureState.store(Plugin::CaptureState::Empty);
    p.captureCommand.store(1);
    check(!clearLayer(p, 10, 0), "queued recording command blocks clear"); p.captureCommand.store(0);
    check(!clearCell(p, 32) && !clearLayer(p, 0, 32) && !pasteLayer(p, 32, 0)
        && !copyLayer(p, 32, 0) && !copyLayer(p, 0, 32), "clipboard bounds guarded");
    const auto preservedCopy = p.layerClipboard;
    check(!copyLayer(p, 0, 30) && p.layerClipboard == preservedCopy, "empty copy preserves clipboard");
    // A layer edit invalidates only its own worker generation; stale results
    // cannot refill a cleared slot, and another layer's generation stays put.
    const auto otherGeneration = p.layerGenerations[10][1];
    LoadResult stale; stale.slot = 10; stale.layer = 0; stale.asset = a;
    stale.generation = p.layerGenerations[10][0]; stale.dirty = true;
    check(clearLayer(p, 10, 0), "clear with possible stale analysis result");
    p.loadResults.push_back(stale); serviceLoads(p);
    check(!p.sources[10][0].asset && p.layerGenerations[10][1] == otherGeneration, "stale layer completion rejected");
    // Clearing a recorded source retains its review, with reversible link.
    p.captureAsset = p.sources[12][0].asset; p.publishedCapture.store(p.captureAsset.get());
    p.captureState.store(Plugin::CaptureState::Review); p.captureFrames.store(1024);
    rememberSavedCapture(p, 12, 0);
    check(savedCaptureValid(p) && clearLayer(p, 12, 0) && p.captureAsset && !savedCaptureValid(p), "clear keeps review without unsafe deletion link");
    historyStep(p); check(savedCaptureValid(p) && p.sources[12][0].asset == p.captureAsset, "undo restores recorded layer and review link");
    check(clearCell(p, 12) && p.captureAsset && !savedCaptureValid(p), "cell clear keeps recording review");
    historyStep(p); check(savedCaptureValid(p), "undo cell clear restores review link");
}
