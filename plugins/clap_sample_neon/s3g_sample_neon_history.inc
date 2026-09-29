// Main-thread destructive-edit journal. Immutable PCM is shared, never copied
// or freed by history on the process thread. This is not host automation undo.
constexpr unsigned kNeonHistoryLimit = 32;
constexpr uint64_t kNeonHistoryAudioBytes = 512ull * 1024 * 1024;

std::shared_ptr<CellClipboard> snapshotHistoryCell(Plugin& p, unsigned slot) {
    saveLayerEdit(p, slot);
    auto cell = std::make_shared<CellClipboard>();
    cell->sources = p.sources[slot]; cell->asset = p.controlAssets[slot];
    cell->path = p.samplePaths[slot]; cell->embedded = p.embeddedAssets[slot];
    cell->analysis = p.slotAnalyses[slot];
    for (unsigned n = 0; n < kSlotParameterCount; ++n)
        cell->parameters[n] = p.parameters[kGlobalParamCount + slot * kSlotParameterCount + n].load();
#define S3G_HISTORY_READ(field) cell->field = readCellValue(p.field[slot]);
    S3G_NEON_CELL_FIELDS(S3G_HISTORY_READ)
#undef S3G_HISTORY_READ
    return cell;
}

struct NeonHistoryCapture {
    std::shared_ptr<const SampleAsset> asset;
    double start = 0, end = 1, cursor = 0;
    uint8_t layout = 0, bus = 0, target = 255;
    bool zeroCross = true;
    uint8_t savedPad = 255, savedLayer = 255;
};
struct NeonHistoryImage {
    uint32_t pads = 0;
    std::array<std::shared_ptr<CellClipboard>, 32> cells;
    bool capture = false, reset = false;
    NeonHistoryCapture take;
    std::array<double, 3> soundGlobals {}; // gain, Mangle, tempo sync; not connections
    std::array<float, 3> fill {};
    decltype(readCellValue(std::declval<Plugin&>().flipSteps)) flipSteps {};
    decltype(readCellValue(std::declval<Plugin&>().flipVelocities)) flipVelocities {};
    decltype(readCellValue(std::declval<Plugin&>().flipStepCounts)) flipCounts {};
    uint8_t flipPattern = 0;
    bool flipLoop = true;
};
struct NeonHistoryEntry { std::string label; NeonHistoryImage image; };
struct NeonHistoryRestore {
    NeonHistoryImage target;
    std::shared_ptr<NeonHistoryEntry> inverse;
    std::array<std::shared_ptr<const s3g::sample::NeonStack>, 32> stacks;
    std::array<std::string, 32> statuses;
    std::string message;
    bool redo = false;
};
struct NeonEditHistory {
    std::vector<std::shared_ptr<NeonHistoryEntry>> undo, redo;
    std::vector<const SampleAsset*> scratch;
    std::shared_ptr<NeonHistoryEntry> deferred;
    std::shared_ptr<NeonHistoryRestore> restore;
    NeonEditHistory() {
        undo.reserve(kNeonHistoryLimit + 1); redo.reserve(kNeonHistoryLimit + 1);
        scratch.reserve((kNeonHistoryLimit + 1) * 1057u);
    }
};

NeonHistoryImage snapshotHistory(Plugin& p, uint32_t pads, bool capture, bool reset) {
    NeonHistoryImage image; image.pads = pads; image.capture = capture; image.reset = reset;
    for (unsigned slot = 0; slot < 32; ++slot)
        if (pads & (1u << slot)) image.cells[slot] = snapshotHistoryCell(p, slot);
    if (capture) image.take = {p.captureAsset, p.captureStart.load(), p.captureEnd.load(),
        p.captureCursor.load(), p.captureLayout.load(), p.captureBus.load(), p.captureTarget.load(), p.captureZeroCross.load()};
    if (capture && savedCaptureValid(p)) { image.take.savedPad = p.captureSavedPad; image.take.savedLayer = p.captureSavedLayer; }
    if (reset) {
        image.soundGlobals = {{paramValue(p, kMasterGainParamId), paramValue(p, kGlobalMangleParamId), paramValue(p, kTempoSyncParamId)}};
        image.fill = {{p.fillBuffer.load(), p.fillRepeat.load(), p.fillBreakup.load()}};
        image.flipSteps = readCellValue(p.flipSteps); image.flipVelocities = readCellValue(p.flipVelocities);
        image.flipCounts = readCellValue(p.flipStepCounts);
        image.flipPattern = p.selectedFlipPattern.load(); image.flipLoop = p.flipLooping.load();
    }
    return image;
}
uint64_t historyFootprint(NeonEditHistory& history,
    const std::vector<std::shared_ptr<NeonHistoryEntry>>& entries, unsigned first = 0,
    const NeonHistoryImage* extra = nullptr,
    const std::vector<std::shared_ptr<NeonHistoryEntry>>* other = nullptr) {
    auto& assets = history.scratch; assets.clear();
    const auto add = [&](const NeonHistoryImage& image) {
        for (const auto& cell : image.cells) if (cell) {
            for (const auto& source : cell->sources) if (source.asset) assets.push_back(source.asset.get());
            if (cell->asset) assets.push_back(cell->asset.get());
        }
        if (image.capture && image.take.asset) assets.push_back(image.take.asset.get());
    };
    for (unsigned n = first; n < entries.size(); ++n) add(entries[n]->image);
    if (extra) add(*extra);
    if (other) for (const auto& entry : *other) add(entry->image);
    std::sort(assets.begin(), assets.end(), std::less<const SampleAsset*>{});
    uint64_t bytes = 0;
    const SampleAsset* previous = nullptr;
    for (const auto* asset : assets) if (asset != previous) {
        bytes += static_cast<uint64_t>(asset->channelCount) * asset->frameCount() * sizeof(float);
        previous = asset;
    }
    return bytes;
}
void commitHistoryEntry(Plugin& p, std::shared_ptr<NeonHistoryEntry> entry) {
    if (!entry) return;
    auto& history = *p.editHistory;
    history.redo.clear();
    history.undo.push_back(std::move(entry)); // capacity reserved before the edit
    while (history.undo.size() > kNeonHistoryLimit
        || historyFootprint(history, history.undo) > kNeonHistoryAudioBytes)
        history.undo.erase(history.undo.begin());
}
NeonHistoryEdit::NeonHistoryEdit(Plugin& plugin, std::string label, uint32_t pads, bool capture, bool reset, bool recordingCommit)
    : p(plugin) {
    if ((!pads && !capture && !reset) || p.resetAllPhase.load()) return;
    if (!recordingCommit && pads && (p.captureState.load() == Plugin::CaptureState::Recording || p.captureReady.load())) {
        p.workflowMessage = "STOP / FINISH RECORDING BEFORE DESTRUCTIVE SOURCE EDITS"; return;
    }
    if (!recordingCommit && (capture || reset) && (p.captureState.load() == Plugin::CaptureState::Recording
        || p.captureCommand.load() != 0 || p.captureReady.load())) { p.workflowMessage = "STOP / FINISH RECORDING BEFORE EDITING THE TAKE"; return; }
    const unsigned recordingPad = p.captureRecordingPad.load();
    if (!recordingCommit && recordingPad < 32 && (pads & (1u << recordingPad))) {
        p.workflowMessage = "STOP STACK RECORDING BEFORE REPLACING ITS SOURCES"; return;
    }
    try {
        if (!p.editHistory) p.editHistory = std::make_shared<NeonEditHistory>();
        auto next = std::make_shared<NeonHistoryEntry>(); next->label = std::move(label);
        next->image = snapshotHistory(p, pads, capture, reset);
        if (historyFootprint(*p.editHistory, {}, 0, &next->image) > kNeonHistoryAudioBytes) {
            p.workflowMessage = "EDIT EXCEEDS 512 MiB UNDO LIMIT; NOTHING CHANGED / SAVE A SET"; return;
        }
        entry = std::move(next);
    } catch (...) { p.workflowMessage = "CANNOT RETAIN UNDO; NOTHING CHANGED"; }
}
void NeonHistoryEdit::commit() { commitHistoryEntry(p, std::move(entry)); }
void NeonHistoryEdit::defer() { p.editHistory->deferred = std::move(entry); }
void commitDeferredHistory(Plugin& p) {
    if (p.editHistory) commitHistoryEntry(p, std::move(p.editHistory->deferred));
}
void clearEditHistory(Plugin& p) {
    p.editHistory.reset(); p.historyRestorePending.store(false);
}
bool historyBusy(const Plugin& p) {
    if (p.resetAllPhase.load() || p.pendingEditCommand.load() || p.captureCommand.load() || p.captureReady.load()
        || p.captureState.load() == Plugin::CaptureState::Recording
        || p.captureState.load() == Plugin::CaptureState::Ready) return true;
    for (const auto& pad : p.pendingSourceLoads) for (bool pending : pad) if (pending) return true;
    return false;
}
bool historyAvailable(const Plugin& p, bool redo) {
    return p.editHistory && !(redo ? p.editHistory->redo : p.editHistory->undo).empty() && !historyBusy(p);
}
std::string historyLabel(const Plugin& p, bool redo) {
    if (!p.editHistory) return "NONE";
    const auto& entries = redo ? p.editHistory->redo : p.editHistory->undo;
    return entries.empty() ? "NONE" : entries.back()->label;
}

// Build all allocating structures before pausing audio or altering live state.
// The acknowledged reset gate also prevents partially restored multi-pad edits
// from being heard. Undo/redo deliberately do not restart notes or a recorder.
bool requestHistoryRestore(Plugin& p, bool redo) {
    if (!historyAvailable(p, redo)) { p.workflowMessage = "UNDO / REDO UNAVAILABLE OR EDIT BUSY"; return false; }
    try {
        auto& history = *p.editHistory;
        auto& entries = redo ? history.redo : history.undo;
        const auto& entry = *entries.back();
        // Storage policy can change after an edit. Never restore a set that
        // cannot be saved under the currently selected policy.
        NeonAudioBudget budget;
        for (unsigned slot = 0; slot < 32; ++slot) {
            if (const auto& cell = entry.image.cells[slot]) {
                for (const auto& source : cell->sources)
                    if (p.storageMode == StorageMode::Embed || source.path.empty()) budget.add(source.asset.get());
            } else budget.pad(p, slot);
        }
        budget.add(entry.image.capture ? entry.image.take.asset.get() : p.captureAsset.get());
        if (budget.samples > kMaximumEmbeddedSamples) {
            p.workflowMessage = "UNDO / REDO EXCEEDS SET AUDIO LIMIT; CHANGE STORAGE OR SAVE FIRST"; return false;
        }
        auto plan = std::make_shared<NeonHistoryRestore>(); plan->target = entry.image; plan->redo = redo;
        plan->inverse = std::make_shared<NeonHistoryEntry>(); plan->inverse->label = entry.label;
        plan->inverse->image = snapshotHistory(p, entry.image.pads, entry.image.capture, entry.image.reset);
        if (historyFootprint(history, {}, 0, &plan->inverse->image) > kNeonHistoryAudioBytes) {
            p.workflowMessage = "REDO SNAPSHOT EXCEEDS 512 MiB; NOTHING CHANGED"; return false;
        }
        for (unsigned slot = 0; slot < 32; ++slot) if (entry.image.cells[slot]) {
            auto cell = std::make_shared<CellClipboard>(*entry.image.cells[slot]);
            auto stack = std::make_shared<s3g::sample::NeonStack>();
            for (unsigned n = 0; n < 32; ++n) {
                auto& source = cell->sources[n]; source.waveAnalysisPending = false;
                if (source.wavesets && source.wavesets->asset != source.asset) source.wavesets.reset();
                if (!source.wavesets) source.waveAnalysisAttempted = nullptr;
                if (source.asset || !source.path.empty()) stack->count = static_cast<uint8_t>(n + 1);
                stack->layers[n] = {source.asset.get(), source.start, source.end};
                stack->layers[n].wavesets = source.wavesets.get();
                stack->cutupsMetadata[n] = source.analysis; stack->sourceBpms[n] = static_cast<float>(source.bpm);
                appendMosaicFragments(*stack, source, n);
            }
            stack->primarySliceCount = cell->sources[0].slices.sliceCount;
            stack->primarySlices = cell->sources[0].slices.boundaries;
            plan->statuses[slot] = cell->asset ? cell->path.empty() ? "CELL AUDIO" : sampleDisplayName(cell->path) : "EMPTY / OFFLINE";
            plan->target.cells[slot] = std::move(cell); plan->stacks[slot] = std::move(stack);
        }
        plan->message = std::string(redo ? "REDONE: " : "UNDONE: ") + entry.label;
        p.retainedAssets.reserve(p.retainedAssets.size() + 1057);
        p.retainedStacks.reserve(p.retainedStacks.size() + 32);
        p.retainedWavesets.reserve(p.retainedWavesets.size() + 1024);
        history.restore = std::move(plan);
        p.historyRestorePending.store(true);
        p.resetAllPhase.store(1, std::memory_order_release);
        requestProcess(p);
        if (p.host && p.host->request_callback) p.host->request_callback(p.host);
        return true;
    } catch (...) { p.workflowMessage = "UNDO / REDO PREPARATION FAILED; NOTHING CHANGED"; return false; }
}
void applyHistoryRestore(Plugin& p) {
    auto& history = *p.editHistory;
    auto plan = history.restore;
    if (!plan || p.resetAllPhase.load() != 2) return;
    if (p.guiParamConsumer.test_and_set(std::memory_order_acquire)) {
        if (p.host && p.host->request_callback) p.host->request_callback(p.host);
        return;
    }
    s3g::clap_gui::ParamEvent pending;
    while (p.guiParamEvents.peek(pending)) p.guiParamEvents.pop();
    p.guiParamConsumer.clear(std::memory_order_release);
    auto& image = plan->target;
    {
    std::lock_guard<std::mutex> lock(p.statusMutex);
    for (unsigned slot = 0; slot < 32; ++slot) if (image.cells[slot]) {
        auto& cell = *image.cells[slot];
        invalidateLayerLoads(p, slot);
        for (auto& registration : p.registrations[slot]) registration.clear();
        p.sources[slot] = std::move(cell.sources);
        uint32_t occupied = 0;
        for (unsigned n = 0; n < 32; ++n) if (p.sources[slot][n].asset || !p.sources[slot][n].path.empty()) occupied |= 1u << n;
        p.captureOccupied[slot].store(occupied);
        for (unsigned n = 0; n < kSlotParameterCount; ++n)
            p.parameters[kGlobalParamCount + slot * kSlotParameterCount + n].store(cell.parameters[n]);
#define S3G_HISTORY_WRITE(field) writeCellValue(p.field[slot], cell.field);
        S3G_NEON_CELL_FIELDS(S3G_HISTORY_WRITE)
#undef S3G_HISTORY_WRITE
        p.controlAssets[slot] = cell.asset; p.samplePaths[slot] = std::move(cell.path);
        p.slotAnalyses[slot] = std::move(cell.analysis); p.statuses[slot] = std::move(plan->statuses[slot]);
        p.embeddedAssets[slot] = cell.embedded;
        for (const auto& source : p.sources[slot]) {
            if (source.asset) p.retainedAssets.push_back(source.asset);
            if (source.wavesets) p.retainedWavesets.push_back(source.wavesets);
        }
        p.controlWavesets[slot] = p.sources[slot][0].wavesets;
        p.retainedStacks.push_back(plan->stacks[slot]);
        p.publishedAssets[slot].store(cell.asset.get(), std::memory_order_release);
        p.publishedWavesets[slot].store(p.controlWavesets[slot].get(), std::memory_order_release);
        p.publishedStacks[slot].store(plan->stacks[slot].get(), std::memory_order_release);
        p.waveformZooms[slot].store(1);
    }
    if (image.capture) {
        const auto& take = image.take; p.captureAsset = take.asset;
        rememberSavedCapture(p, take.savedPad, take.savedLayer);
        if (take.asset) p.retainedAssets.push_back(take.asset);
        p.publishedCapture.store(take.asset.get(), std::memory_order_release);
        p.captureStart.store(take.start); p.captureEnd.store(take.end); p.captureCursor.store(take.cursor);
        p.captureLayout.store(take.layout); p.captureBus.store(take.bus); p.captureTarget.store(take.target);
        p.captureZeroCross.store(take.zeroCross); p.captureZoom.store(1); p.capturePlayhead.store(-1);
        p.captureFrames.store(take.asset ? take.asset->frameCount() : 0);
        p.captureState.store(take.asset ? Plugin::CaptureState::Review : Plugin::CaptureState::Empty);
    }
    }
    if (image.reset) {
        p.parameters[kMasterGainParamId-1].store(image.soundGlobals[0]);
        p.parameters[kGlobalMangleParamId-1].store(image.soundGlobals[1]);
        p.parameters[kTempoSyncParamId-1].store(image.soundGlobals[2]);
        p.fillBuffer.store(image.fill[0]); p.fillRepeat.store(image.fill[1]); p.fillBreakup.store(image.fill[2]);
        writeCellValue(p.flipSteps, image.flipSteps); writeCellValue(p.flipVelocities, image.flipVelocities);
        writeCellValue(p.flipStepCounts, image.flipCounts); p.selectedFlipPattern.store(image.flipPattern);
        p.flipLooping.store(image.flipLoop);
    }
    p.flipPlaying.store(false); p.flipRecording.store(false);
    p.clearStackPerformance.store(true); p.musicalHeld = {}; p.heldSlots.fill(255);
    p.nextCollection = {}; p.ledFeedbackDirty.store(true);
    p.activeVoices.store(0); p.outputPeak.store(0);
    p.workflowMessage = std::move(plan->message);
    auto& from = plan->redo ? history.redo : history.undo;
    auto& to = plan->redo ? history.undo : history.redo;
    from.pop_back(); to.push_back(std::move(plan->inverse));
    // Bound both directions together; trim the farthest steps, never the next.
    while (historyFootprint(history, history.undo, 0, nullptr, &history.redo) > kNeonHistoryAudioBytes) {
        auto& trim = from.empty() ? to : from;
        if (trim.size() == 1 && from.empty()) break;
        trim.erase(trim.begin());
    }
    history.restore.reset(); p.historyRestorePending.store(false);
    refreshCaptureBudget(p);
    p.resetAllPhase.store(0, std::memory_order_release);
    if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
    markStateDirty(p); requestProcess(p);
}
