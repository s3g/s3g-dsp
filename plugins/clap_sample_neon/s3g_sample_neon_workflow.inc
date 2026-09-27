// Included in the CLAP bridge's anonymous namespace. Main-thread asset edits
// are separate from bounded controller/audio commands.
enum class WorkflowCommand : uint8_t {
    MapSlice = 1u, MapAll, DiscardCapture, AssignCapture, Equal, Transients,
    CropCapture, MapAllReplace, ResetAll, Normalize,
};
constexpr uint64_t kMaximumEmbeddedSamples = 64u * 1024u * 1024u;
void initializeSoundDefaults(Plugin* instance);

struct NeonAudioBudget {
    std::array<const SampleAsset*, 1057u> seen {};
    std::size_t count = 0u;
    uint64_t samples = 0u;
    void add(const SampleAsset* asset) {
        if (!asset || std::find(seen.begin(), seen.begin() + count, asset) != seen.begin() + count) return;
        seen[count++] = asset;
        samples += static_cast<uint64_t>(asset->channelCount) * asset->frameCount();
    }
    void pad(const Plugin& p, unsigned slot, int skipLayer = -1) {
        for (unsigned layer = 0u; layer < 32u; ++layer) {
            const auto& s = p.sources[slot][layer];
            if (static_cast<int>(layer) != skipLayer && (p.storageMode == StorageMode::Embed || s.path.empty())) add(s.asset.get());
        }
    }
};

// Main-thread clipboard: immutable PCM is shared, all editable state is copied.
// This inventory includes hidden legacy fields so a cell is a complete snapshot.
#define S3G_NEON_CELL_FIELDS(X) \
    X(sliceTriggers) X(sliceOptions) X(sliceCounts) X(sliceBoundaries) \
    X(chopModes) X(chopBeatDivisions) X(sourceBpms) X(transientPreRollMs) X(transientSliceLimits) \
    X(slicerDomainIndices) X(slicerQuantizeIndices) X(slotOptions) X(chokeGroups) \
    X(hotCuePositions) X(hotCueEnabled) X(savedLoopStarts) X(savedLoopEnds) X(savedLoopEnabled) \
    X(editPositions) X(textureOptions) X(motionPaths) X(playbackClocks) X(motionSeconds) X(shotSeconds) \
    X(techniqueAttackSeconds) X(techniqueReleaseSeconds) X(grainIntervals) X(fxParameters) X(techniqueParameters) \
    X(motionRates) X(motionLoci) X(motionFields) X(lanePositions) X(laneMotionDepths) \
    X(grainDensities) X(grainSizes) X(grainPositions) X(grainSprays) X(grainPitchSprays) X(grainReverseChances) \
    X(selectedLayers) X(sourceModes) X(stackPaths) X(stackSeconds) X(stackBeats) X(familyControls)
template<class T> T readCellValue(const std::atomic<T>& value) { return value.load(); }
template<class T, std::size_t N> auto readCellValue(const std::array<T, N>& value) {
    std::array<decltype(readCellValue(value[0])), N> result;
    for (std::size_t n = 0u; n < N; ++n) result[n] = readCellValue(value[n]);
    return result;
}
template<class T> void writeCellValue(std::atomic<T>& target, T value) { target.store(value); }
template<class T, class V, std::size_t N> void writeCellValue(std::array<T, N>& target, const std::array<V, N>& value) {
    for (std::size_t n = 0u; n < N; ++n) writeCellValue(target[n], value[n]);
}
struct CellClipboard {
    std::array<LayerSource, 32u> sources;
    std::shared_ptr<const SampleAsset> asset;
    std::string path;
    bool embedded = false;
    std::array<double, kSlotParameterCount> parameters {};
    s3g::sample::CutupsLaneMetadata analysis;
#define S3G_CELL_DECLARE(field) decltype(readCellValue(std::declval<Plugin&>().field[0])) field {};
    S3G_NEON_CELL_FIELDS(S3G_CELL_DECLARE)
#undef S3G_CELL_DECLARE
};
bool copyCell(Plugin& p, std::size_t slot) {
    if (slot >= 32u || p.resetAllPhase.load()) return false;
    try {
        auto copy = std::make_shared<CellClipboard>();
        saveLayerEdit(p, slot);
        copy->sources = p.sources[slot];
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            if (!p.controlAssets[slot]) { p.workflowMessage = "COPY NEEDS A LOADED CELL"; return false; }
            copy->asset = p.controlAssets[slot]; copy->path = p.samplePaths[slot];
            copy->embedded = p.embeddedAssets[slot]; copy->analysis = p.slotAnalyses[slot];
        }
        for (clap_id n = 0u; n < kSlotParameterCount; ++n)
            copy->parameters[n] = paramValue(p, slotParamId(slot, static_cast<SlotParamOffset>(n)));
#define S3G_CELL_READ(field) copy->field = readCellValue(p.field[slot]);
        S3G_NEON_CELL_FIELDS(S3G_CELL_READ)
#undef S3G_CELL_READ
        p.cellClipboard = std::move(copy);
        p.workflowMessage = "CELL COPIED: AUDIO + SLICES + PLAYBACK + FX + ROUTING";
        return true;
    } catch (...) { p.workflowMessage = "COPY FAILED; CLIPBOARD KEPT"; return false; }
}
bool pasteCell(Plugin& p, std::size_t slot) {
    const auto copy = p.cellClipboard;
    if (slot >= 32u || !copy || p.resetAllPhase.load()) return false;
    NeonAudioBudget stackBudget;
    for (unsigned pad = 0u; pad < 32u; ++pad) if (pad != slot) stackBudget.pad(p, pad);
    stackBudget.add(p.captureAsset.get());
    for (const auto& layer : copy->sources)
        if (p.storageMode == StorageMode::Embed || layer.path.empty()) stackBudget.add(layer.asset.get());
    if (stackBudget.samples > kMaximumEmbeddedSamples) { p.workflowMessage = "SET AUDIO LIMIT REACHED; NOTHING REPLACED"; return false; }
    try {
        auto analysis = copy->analysis;
        std::string path = copy->path;
        { std::lock_guard<std::mutex> lock(p.statusMutex); p.retainedAssets.reserve(p.retainedAssets.size() + 1u); }
        clearSample(p, slot); // Cancel stale file loads before publishing snapshot.
        for (clap_id n = 0u; n < kSlotParameterCount; ++n)
            p.parameters[kGlobalParamCount + slot * kSlotParameterCount + n].store(copy->parameters[n]);
#define S3G_CELL_WRITE(field) writeCellValue(p.field[slot], copy->field);
        S3G_NEON_CELL_FIELDS(S3G_CELL_WRITE)
#undef S3G_CELL_WRITE
        { std::lock_guard<std::mutex> lock(p.statusMutex); p.slotAnalyses[slot] = std::move(analysis); }
        publishAsset(p, slot, copy->asset, std::move(path), true);
        p.embeddedAssets[slot] = copy->embedded; p.waveformZooms[slot].store(1.0f);
        p.sources[slot] = copy->sources;
        for (auto& layer : p.sources[slot]) if (!layer.wavesets) {
            layer.waveAnalysisAttempted = nullptr; layer.waveAnalysisPending = false;
        }
        for (const auto& layer : p.sources[slot]) if (layer.asset) p.retainedAssets.push_back(layer.asset);
        publishStack(p, slot);
        p.pendingCellStops.fetch_or(1u << slot);
        p.selectedSlot.store(static_cast<uint8_t>(slot)); p.cellBank.store(static_cast<uint8_t>(slot / 8u));
        // Put the copied values after any earlier queued GUI gestures.
        for (clap_id n = 0u; n < kSlotParameterCount; ++n)
            queueGuiParamGesture(p, slotParamId(slot, static_cast<SlotParamOffset>(n)), copy->parameters[n]);
        p.workflowMessage = "CELL PASTED; SOURCE KEPT / EDITS ARE INDEPENDENT";
        if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
        markStateDirty(p); requestProcess(p); return true;
    } catch (...) { p.workflowMessage = "PASTE FAILED"; return false; }
}
#undef S3G_NEON_CELL_FIELDS

unsigned playbackIndex(const Plugin& p, std::size_t slot) noexcept {
    const auto option = normalizedStageOptions(p.textureOptions[slot].load());
    for (unsigned n = 1u; n < kPlaybackOptions.size(); ++n) if (option == kPlaybackOptions[n]) return n;
    return 0u;
}
bool fxAllowed(const Plugin& p, std::size_t slot, unsigned effect) noexcept {
    return effect < 8u && (paramValue(p, slotParamId(slot, kSlotSourceFormat)) < 0.5
        || s3g::sample::sampleNeonFxAmbisonicSafe(static_cast<SampleNeonMangleCharacter>(effect)));
}
uint8_t playbackEditPage(unsigned method) noexcept { return static_cast<uint8_t>(method == 6u ? 9u : method + 1u); }
uint8_t effectiveEditPage(const Plugin& p) noexcept {
    const auto page = p.editPage.load();
    if (page == 0u || page == 7u || page == 8u) return page;
    // Playback Settings is a view of the selected cell, not a stale engine
    // page left over from the previously selected pad.
    return playbackEditPage(playbackIndex(p, p.selectedSlot.load()));
}
void choosePlayback(Plugin& p, unsigned method) noexcept {
    const auto slot = p.selectedSlot.load();
    if (method >= kPlaybackOptions.size() || (method == 5u && paramValue(p, slotParamId(slot, kSlotSourceFormat)) >= 0.5)) return;
    p.textureOptions[slot].store(kPlaybackOptions[method]);
    if (method == 0u && p.sourceModes[slot].load() == 4u) p.sourceModes[slot].store(1u);
    if (method == 3u || (method == 5u && p.sourceModes[slot].load() != 4u)) p.sourceModes[slot].store(0u);
    if (method == 3u && p.selectedLayers[slot].load() != 0u)
        p.pendingLayerSelection.store(static_cast<uint32_t>(slot << 8u));
    p.editPage.store(method ? playbackEditPage(method) : 0u);
    markStateDirty(p); requestProcess(p);
    if (p.host && p.host->request_callback) p.host->request_callback(p.host);
}
double advancedKnobValue(Plugin& p, bool loop) noexcept {
    const auto slot = p.selectedSlot.load(), page = effectiveEditPage(p);
    if (page == 9u) return loop ? p.familyControls[slot][neonFamilyIndex(NeonFamily::LaneAuto)].load() != 0
        ? p.stackPositions[slot].load() : p.familyControls[slot][neonFamilyIndex(NeonFamily::LanePosition)].load()
        : (p.familyControls[slot][neonFamilyIndex(NeonFamily::LaneRate)].load() - .25) / 3.75;
    if (page == 8u) {
        const auto pending = p.pendingLayerSelection.load();
        const auto selected = pending != UINT32_MAX && (pending >> 8u) == slot ? pending & 31u : p.selectedLayers[slot].load();
        return loop ? selected / 31.0 : p.playbackClocks[slot].load()
            ? nearestMotionCycleIndex(p.stackBeats[slot].load()) / 7.0 : (p.stackSeconds[slot].load() - 0.05) / 29.95;
    }
    if (page == 7u) {
        if (p.fxPair.load() && !loop) return paramValue(p, slotParamId(slot, kSlotMangle));
        const auto effect = static_cast<unsigned>(paramValue(p, slotParamId(slot, kSlotCharacter)));
        return p.fxParameters[slot][effect][p.fxPair.load() ? 2u : loop ? 0u : 1u].load();
    }
    if (page == 5u) return loop ? (p.playbackClocks[slot].load()
        ? nearestMotionCycleIndex(p.motionRates[slot].load()) / 7.0 : (p.motionSeconds[slot].load() - 0.05) / 29.95)
        : (paramValue(p, slotParamId(slot, kSlotTune)) + 24.0) / 48.0;
    if (page == 4u && loop && p.playbackClocks[slot].load()) return nearestMotionCycleIndex(p.grainIntervals[slot].load() * 8.0f) / 7.0;
    return p.techniqueParameters[slot][page == 6u ? 2u : 0u][loop ? 0u : page == 4u ? 2u : 1u].load();
}
void setAdvancedKnob(Plugin& p, bool loop, double value) noexcept {
    value = std::clamp(value, 0.0, 1.0);
    const auto slot = p.selectedSlot.load(), page = effectiveEditPage(p);
    if (page == 9u) {
        p.familyControls[slot][neonFamilyIndex(loop ? NeonFamily::LanePosition : NeonFamily::LaneRate)].store(
            static_cast<float>(loop ? value : .25 + value * 3.75));
        if (loop) p.familyControls[slot][neonFamilyIndex(NeonFamily::LaneAuto)].store(0);
    } else if (page == 8u) {
        // Layer changes allocate source snapshots and are main-thread work.
        if (loop) {
            const auto* stack = p.publishedStacks[slot].load(std::memory_order_acquire);
            if (stack && stack->count) p.pendingLayerSelection.store((static_cast<uint32_t>(slot) << 8u)
                | std::min<unsigned>(stack->count - 1u, static_cast<unsigned>(std::lround(value * 31.0))));
        } else if (p.playbackClocks[slot].load()) p.stackBeats[slot].store(kMotionCycleBeats[static_cast<unsigned>(std::lround(value * 7.0))]);
        else p.stackSeconds[slot].store(static_cast<float>(0.05 + value * 29.95));
    } else if (page == 7u) {
        if (p.fxPair.load() && !loop) setParam(p, slotParamId(slot, kSlotMangle), value);
        else {
            const auto effect = static_cast<unsigned>(paramValue(p, slotParamId(slot, kSlotCharacter)));
            p.fxParameters[slot][effect][p.fxPair.load() ? 2u : loop ? 0u : 1u].store(static_cast<float>(value));
        }
    } else if (page == 5u) {
        if (!loop) setParam(p, slotParamId(slot, kSlotTune), value * 48.0 - 24.0);
        else if (p.playbackClocks[slot].load()) p.motionRates[slot].store(kMotionCycleBeats[static_cast<unsigned>(std::lround(value * 7.0))]);
        else p.motionSeconds[slot].store(static_cast<float>(0.05 + value * 29.95));
    } else if (page == 4u && loop && p.playbackClocks[slot].load())
        p.grainIntervals[slot].store(kMotionCycleBeats[static_cast<unsigned>(std::lround(value * 7.0))] / 8.0f);
    else p.techniqueParameters[slot][page == 6u ? 2u : 0u][loop ? 0u : page == 4u ? 2u : 1u].store(static_cast<float>(value));
    markStateDirty(p); requestProcess(p);
}
void serviceWavesets(Plugin& p) {
    if (p.resetAllPhase.load()) return;
    for (unsigned slot = 0u; slot < 32u; ++slot) {
        if (playbackIndex(p, slot) != 5u || paramValue(p, slotParamId(slot, kSlotSourceFormat)) >= 0.5) continue;
        const unsigned count = p.sourceModes[slot].load() == 4u ? stackCount(p, slot) : 1u;
        for (unsigned layer = 0u; layer < count; ++layer) {
            auto& source = p.sources[slot][layer];
            if (!source.asset || source.waveAnalysisAttempted == source.asset.get()) continue;
#if defined(S3G_SAMPLE_FILE_WORKER)
            LoadRequest request;
            request.slot = static_cast<uint8_t>(slot); request.layer = static_cast<uint8_t>(layer);
            request.generation = p.layerGenerations[slot][layer]; request.dirty = false;
            request.wavesetsOnly = true; request.asset = source.asset;
            { std::lock_guard<std::mutex> lock(p.loaderMutex); p.loadRequests.push_back(std::move(request)); }
            source.waveAnalysisAttempted = source.asset.get();
            source.waveAnalysisPending = true;
            p.loaderCondition.notify_one();
#endif
        }
    }
}

void requestWorkflow(Plugin& p, WorkflowCommand command) noexcept
{
    if (p.resetAllPhase.load()) return;
    // Snapshot destination and distribution with the source/slice. Hardware
    // requests may wait for the main thread while the user changes menus.
    const unsigned destination = p.chopDestination.load();
    const uint32_t message = static_cast<uint32_t>(command)
        | (static_cast<uint32_t>(p.selectedSlot.load()) << 8u)
        | (static_cast<uint32_t>(p.visibleSlice.load()) << 16u)
        | (static_cast<uint32_t>(destination < 32u ? destination + 1u : 0u) << 24u)
        | (static_cast<uint32_t>(p.slicesToStack.load()) << 30u);
    uint32_t empty = 0u;
    if (p.pendingEditCommand.compare_exchange_strong(empty, message)
        && p.host && p.host->request_callback) p.host->request_callback(p.host);
}

void selectSurface(Plugin& p, NeonMode mode, uint8_t layer) noexcept
{
    const auto previous = static_cast<NeonMode>(p.visibleMode.load());
    if (previous == NeonMode::Slicer) p.chopBank.store(p.visibleBank.load());
    else p.cellBank.store(p.visibleBank.load());
    p.visibleMode.store(static_cast<uint8_t>(mode));
    p.visibleLayer.store(std::min<uint8_t>(1u, layer));
    if (mode == NeonMode::Slicer) {
        const auto count = sliceCount(p, p.selectedSlot.load());
        p.chopBank.store(std::min<uint8_t>(p.chopBank.load(), static_cast<uint8_t>((count - 1u) / 8u)));
        p.visibleSlice.store(std::min<uint8_t>(p.visibleSlice.load(), static_cast<uint8_t>(count - 1u)));
    }
    p.visibleBank.store(mode == NeonMode::Slicer ? p.chopBank.load() : p.cellBank.load());
    p.ledFeedbackDirty.store(true);
    requestProcess(p);
}

void selectSurfaceBank(Plugin& p, uint8_t bank) noexcept
{
    if (bank > 3u) return;
    const bool chop = p.visibleMode.load() == static_cast<uint8_t>(NeonMode::Slicer);
    if (chop && bank * 8u >= sliceCount(p, p.selectedSlot.load())) return;
    (chop ? p.chopBank : p.cellBank).store(bank);
    p.visibleBank.store(bank);
    if (chop) p.visibleSlice.store(std::min<uint8_t>(
        static_cast<uint8_t>(bank * 8u + p.visibleSlice.load() % 8u),
        static_cast<uint8_t>(sliceCount(p, p.selectedSlot.load()) - 1u)));
    p.ledFeedbackDirty.store(true);
    requestProcess(p);
}

bool emptyCell(const Plugin& p, std::size_t slot)
{
    return slot < 32u && stackCount(p, slot) == 0u;
}

std::string chopPadName(std::size_t pad) {
    return std::string(1u, static_cast<char>('A' + pad / 8u)) + std::to_string(pad % 8u + 1u);
}
s3g::sample::NeonChopDestinationPlan chopDestinationPlan(const Plugin& p, unsigned source,
    unsigned slices, bool stack, unsigned target, bool replaceSourceAuto = false) {
    uint32_t occupied = 0u;
    for (unsigned pad = 0u; pad < 32u; ++pad) if (!emptyCell(p, pad)) occupied |= 1u << pad;
    return s3g::sample::neonChopDestinationPlan(occupied, source, slices, stack, target, replaceSourceAuto);
}
std::string chopDestinationSummary(const s3g::sample::NeonChopDestinationPlan& plan) {
    using Error = s3g::sample::NeonChopDestinationError;
    if (plan.error == Error::Occupied) return chopPadName(plan.blockedPad) + " OCCUPIED";
    if (plan.error == Error::PastLastPad) return "RANGE PAST D8";
    if (plan.error == Error::NotEnoughEmpty) return "NOT ENOUGH EMPTY PADS";
    if (!plan.valid()) return "INVALID DESTINATION";
    std::string result;
    for (unsigned n = 0u; n < plan.count; ++n) {
        if (!result.empty()) result += ", ";
        result += chopPadName(plan.pads[n]);
        const unsigned first = n;
        while (n + 1u < plan.count && plan.pads[n + 1u] == plan.pads[n] + 1u) ++n;
        if (n != first) result += "-" + chopPadName(plan.pads[n]);
    }
    return result;
}

void mapChopToBanks(Plugin& p, std::size_t source, bool all = true, uint8_t selected = 0u,
    bool replaceSourceAuto = false, unsigned target = s3g::sample::kNeonChopAutoDestination,
    bool stackDestination = false)
{
    const auto asset = p.controlAssets[source];
    if (!asset) return;
    const auto layout = sliceLayout(p, source);
    const std::size_t required = all ? layout.sliceCount : 1u;
    const auto plan = chopDestinationPlan(p, static_cast<unsigned>(source), static_cast<unsigned>(required),
        stackDestination, target, replaceSourceAuto);
    if (!plan.valid()) {
        p.workflowMessage = chopDestinationSummary(plan) + "; NOTHING CHANGED";
        return;
    }
    const auto& destinations = plan.pads;
    const bool replaceSource = plan.replacesSource;
    const auto settings = settingsSnapshot(p).slots[source];
    // Prepare every standalone sample before replacing any cell, including
    // the source. Adjacent slices share one rounded, integer-frame boundary.
    std::array<uint32_t, 33u> edges {};
    const double start = settings.start, range = settings.end - start;
    for (std::size_t marker = 0u; marker <= layout.sliceCount; ++marker)
        edges[marker] = static_cast<uint32_t>(std::llround(
            std::clamp(start + range * layout.boundaries[marker], 0.0, 1.0) * asset->frameCount()));
    std::array<std::shared_ptr<const SampleAsset>, 32u> samples {};
    std::array<s3g::sample::CutupsLaneMetadata, 32u> analyses {};
    NeonAudioBudget stackBudget;
    for (unsigned pad = 0u; pad < 32u; ++pad)
        if (std::find(destinations.begin(), destinations.begin() + plan.count, pad) == destinations.begin() + plan.count) stackBudget.pad(p, pad);
    stackBudget.add(p.captureAsset.get());
    uint64_t storedSamples = stackBudget.samples;
    for (std::size_t index = 0u; index < required; ++index) {
        const auto slice = all ? index : std::min<std::size_t>(selected, layout.sliceCount - 1u);
        if (edges[slice] >= edges[slice + 1u]) {
            p.workflowMessage = "SLICE SHORTER THAN ONE FRAME; NOTHING CHANGED";
            return;
        }
        storedSamples += static_cast<uint64_t>(edges[slice + 1u] - edges[slice]) * asset->channelCount;
    }
    if (storedSamples > kMaximumEmbeddedSamples) {
        p.workflowMessage = "SET AUDIO LIMIT REACHED; NOTHING CHANGED";
        return;
    }
    try {
        for (std::size_t index = 0u; index < required; ++index) {
            const auto slice = all ? index : std::min<std::size_t>(selected, layout.sliceCount - 1u);
            samples[index] = std::make_shared<SampleAsset>(
                s3g::sample::sampleNeonCrop(*asset, edges[slice], edges[slice + 1u]));
            analyses[index] = s3g::sample::analyzeCutupsAsset(*samples[index], 32u, 5.0, 1000u, 20.0);
        }
        std::lock_guard<std::mutex> lock(p.statusMutex);
        p.retainedAssets.reserve(p.retainedAssets.size() + required);
    } catch (...) {
        p.workflowMessage = "SLICE COPY FAILED; ORIGINAL CELLS KEPT";
        return;
    }
    // Snapshot parameters before replacing source with slice 1.
    const double zeroCross = paramValue(p, slotParamId(source, kSlotZeroCross));
    std::array<double, kSlotParameterCount> sourceParameters {};
    std::array<std::array<float, 3u>, 8u> sourceFx {};
    for (unsigned effect = 0u; effect < 8u; ++effect)
        for (unsigned n = 0u; n < 3u; ++n) sourceFx[effect][n] = p.fxParameters[source][effect][n].load();
    for (clap_id offset = 0u; offset < kSlotParameterCount; ++offset)
        sourceParameters[offset] = paramValue(p, slotParamId(source, static_cast<SlotParamOffset>(offset)));
    for (std::size_t index = 0u; index < required; ++index) {
        const auto destination = destinations[stackDestination ? 0u : index];
        // The committed cell owns only this slice, not the original file plus
        // trim offsets. Reopening CHOP can now slice this new sample again.
        if (!stackDestination || index == 0u) clearSample(p, destination);
        if (stackDestination) p.selectedLayers[destination].store(static_cast<uint8_t>(index));
        for (clap_id offset = 0u; offset < kSlotParameterCount; ++offset)
            setParam(p, slotParamId(destination, static_cast<SlotParamOffset>(offset)),
                kSlotParamDefs[offset].defaultValue);
        // Keep the audible tuning/envelope/filter/character and route from
        // CHOP preview. Generative stages/repeat remain explicit cell edits.
        for (clap_id offset = 0u; offset < kSlotParameterCount; ++offset)
            if ((offset <= kSlotOutputBus && offset != kSlotStart && offset != kSlotEnd)
                || offset == kSlotSourceFormat)
                setParam(p, slotParamId(destination, static_cast<SlotParamOffset>(offset)), sourceParameters[offset]);
        setParam(p, slotParamId(destination, kSlotZeroCross), 0.0);
        setParam(p, slotParamId(destination, kSlotStart), 0.0);
        setParam(p, slotParamId(destination, kSlotEnd), 1.0);
        setParam(p, slotParamId(destination, kSlotTriggerMode), static_cast<double>(TriggerMode::OneShot));
        p.textureOptions[destination].store(0u);
        for (unsigned effect = 0u; effect < 8u; ++effect)
            for (unsigned n = 0u; n < 3u; ++n) p.fxParameters[destination][effect][n].store(sourceFx[effect][n]);
        p.editPositions[destination].store(0.0);
        p.waveformZooms[destination].store(1.0f);
        p.slotOptions[destination].store(kSlotVelocityOption);
        p.chokeGroups[destination].store(0u);
        p.chopModes[destination].store(static_cast<uint8_t>(SampleNeonChopMode::Equal));
        p.transientPreRollMs[destination].store(0.0f);
        p.transientSliceLimits[destination].store(32u);
        p.sourceBpms[destination].store(settings.sourceTempoBpm);
        for (auto& trigger : p.sliceTriggers[destination])
            trigger.store(static_cast<uint8_t>(SampleNeonSliceTriggerMode::OneShot));
        for (auto& options : p.sliceOptions[destination]) options.store(0u);
        storeSliceLayout(p, destination, s3g::sample::equalSampleNeonSliceLayout(1u), false);
        publishAsset(p, destination, samples[index], "", false);
        p.embeddedAssets[destination] = true;
        {
            std::lock_guard<std::mutex> lock(p.statusMutex);
            p.slotAnalyses[destination] = analyses[index];
        }
        // Already snapped in the source map: preserve exactly shared edges.
        p.parameters[kGlobalParamCount + destination * kSlotParameterCount + kSlotZeroCross].store(
            zeroCross);
        saveLayerEdit(p, destination);
        publishStack(p, destination);
    }
    if (stackDestination) {
        saveLayerEdit(p, destinations[0]);
        showLayer(p, destinations[0], 0u, false);
        p.selectedSlot.store(static_cast<uint8_t>(destinations[0]));
        p.cellBank.store(static_cast<uint8_t>(destinations[0] / 8u));
    }
    p.workflowMessage = "CREATED " + std::to_string(required)
        + (stackDestination ? " LAYERS > " : " SLICES > ") + chopDestinationSummary(plan)
        + (replaceSource ? "; SOURCE REPLACED" : "; SOURCE KEPT");
    if (replaceSource) {
        p.visibleSlice.store(0u); p.chopBank.store(0u);
        p.visibleBank.store(0u);
    }
    markStateDirty(p);
    if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
}

void setCaptureTrim(Plugin& p, bool endHandle, double position)
{
    const auto* asset = p.publishedCapture.load(std::memory_order_acquire);
    if (!asset) return;
    const uint32_t total = asset->frameCount();
    const auto lower = endHandle ? static_cast<uint32_t>(std::llround(p.captureStart.load() * total)) + 1u : 0u;
    const auto upper = endHandle ? total : static_cast<uint32_t>(std::llround(p.captureEnd.load() * total)) - 1u;
    uint32_t frame = std::clamp(static_cast<uint32_t>(std::llround(std::clamp(position, 0.0, 1.0) * total)), lower, upper);
    if (p.captureZeroCross.load()) frame = s3g::sample::sampleNeonBoundary(*asset, frame, lower, upper);
    (endHandle ? p.captureEnd : p.captureStart).store(static_cast<double>(frame) / total);
    markStateDirty(p);
}

void stopSoundForReset(Plugin& p) noexcept
{
    uint8_t requested = 1u;
    if (!p.resetAllPhase.compare_exchange_strong(requested, 3u, std::memory_order_acq_rel)) return;
    p.engine.reset(); p.capturePreview.reset();
    for (auto& layer : p.stackWaveformLayers) layer.store(-1);
    p.heldGestures = {}; p.neonState = {};
    p.neonPadInput.clear();
    p.pendingAuditions.store(0u); p.pendingEditAuditions.store(0u);
    p.pendingCellStops.store(0u);
    p.pendingGuiReleases.store(0u); p.pendingGuiFx.store(UINT32_MAX);
    p.pendingSliceAudition.store(UINT32_MAX); p.pendingPerformanceAudition.store(UINT32_MAX);
    p.pendingLayerAuditions.store(0u); p.pendingLayerSelection.store(UINT32_MAX);
    p.captureCommand.store(0); p.captureAudition.store(false);
    p.captureState.store(Plugin::CaptureState::Empty);
    p.killRequested.store(false);
    p.resetAllPhase.store(2u, std::memory_order_release);
}

void clearAllSound(Plugin& p)
{
    if (p.resetAllPhase.load(std::memory_order_acquire) != 2u) return;
    // Audio acknowledged silence before we touch assets. Imported files are
    // unlinked only; no filesystem deletion occurs. Keep host/controller setup.
    if (p.guiParamConsumer.test_and_set(std::memory_order_acquire)) {
        if (p.host && p.host->request_callback) p.host->request_callback(p.host);
        return;
    }
    s3g::clap_gui::ParamEvent pending;
    while (p.guiParamEvents.peek(pending)) p.guiParamEvents.pop();
    p.guiParamConsumer.clear(std::memory_order_release);
    constexpr std::array<std::size_t, 4u> connections {{ 0u, 3u, 4u, 6u }};
    std::array<double, 4u> retained {};
    for (std::size_t n = 0u; n < connections.size(); ++n) retained[n] = p.parameters[connections[n]].load();
    for (std::size_t slot = 0u; slot < 32u; ++slot) clearSample(p, slot);
    {
        std::lock_guard<std::mutex> lock(p.statusMutex);
        initializeSoundDefaults(&p);
        for (auto& analysis : p.slotAnalyses) analysis = {};
    }
    for (std::size_t n = 0u; n < connections.size(); ++n) p.parameters[connections[n]].store(retained[n]);
    p.publishedCapture.store(nullptr); p.captureAsset.reset();
    // Both engines are quiescent until phase returns to zero. Detach their
    // raw pointers before reclaiming retired audio on this main thread.
    for (std::size_t slot = 0u; slot < 32u; ++slot) p.engine.setPreparedAsset(slot, nullptr);
    for (std::size_t slot = 0u; slot < 32u; ++slot) {
        p.engine.setPreparedWavesets(slot, nullptr); p.publishedWavesets[slot].store(nullptr);
        p.controlWavesets[slot].reset(); p.waveAnalysisAttempted[slot] = nullptr;
    }
    p.retainedWavesets.clear();
    p.retainedStacks.clear();
    for (auto& stack : p.publishedStacks) stack.store(nullptr);
    p.audioAssets.fill(nullptr);
    p.capturePreview.setPreparedAsset(nullptr); p.audioCapture = nullptr;
    { std::lock_guard<std::mutex> lock(p.statusMutex); p.retainedAssets.clear(); }
    p.captureFrames.store(0u); p.capturePlayhead.store(-1.0f);
    p.captureStart.store(0.0); p.captureEnd.store(1.0); p.captureCursor.store(0.0);
    p.captureZoom.store(1.0f); p.captureZeroCross.store(true);
    p.captureTarget.store(255u); p.captureBus.store(0u); p.captureLayout.store(0u);
    p.flipRecording.store(false); p.flipPlaying.store(false); p.flipLooping.store(true);
    p.selectedFlipPattern.store(0u);
    p.chopDestination.store(s3g::sample::kNeonChopAutoDestination);
    p.slicesToStack.store(false); p.pendingEditCommand.store(0u);
    p.selectedSlot.store(0u); p.visibleSlice.store(0u); p.visibleBank.store(0u);
    p.cellBank.store(0u); p.chopBank.store(0u); p.editPage.store(0u);
    p.visibleMode.store(0u); p.visibleLayer.store(0u);
    p.activeVoices.store(0u); p.outputPeak.store(0.0f);
    p.workflowMessage = "ALL CELLS + CAPTURE CLEARED; OUTPUT / MIDI KEPT";
    p.sendNeonInitialization.store(true); p.ledFeedbackDirty.store(true);
    p.resetAllPhase.store(0u, std::memory_order_release);
    if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
    markStateDirty(p); requestProcess(p);
}

void cropCapture(Plugin& p)
{
    if (p.captureState.load() != Plugin::CaptureState::Review || !p.captureAsset) return;
    const auto total = p.captureAsset->frameCount();
    const auto start = std::min(total - 1u, static_cast<uint32_t>(std::llround(p.captureStart.load() * total)));
    const auto end = std::clamp(static_cast<uint32_t>(std::llround(p.captureEnd.load() * total)), start + 1u, total);
    try {
        auto cropped = std::make_shared<SampleAsset>(s3g::sample::sampleNeonCrop(*p.captureAsset, start, end));
        std::lock_guard<std::mutex> lock(p.statusMutex);
        p.retainedAssets.push_back(cropped);
        p.captureAsset = std::move(cropped);
        p.captureStart.store(0.0); p.captureEnd.store(1.0); p.captureCursor.store(0.0);
        p.captureZoom.store(1.0f); p.captureFrames.store(end - start); p.capturePlayhead.store(-1.0f);
        p.publishedCapture.store(p.captureAsset.get(), std::memory_order_release);
        p.captureAudition.store(true);
        p.workflowMessage = "CROPPED TO SELECTION; OUTSIDE AUDIO DISCARDED FROM TAKE";
        markStateDirty(p); requestProcess(p);
    } catch (...) { p.workflowMessage = "CROP FAILED; ORIGINAL TAKE KEPT"; }
}

void normalizeCell(Plugin& p, uint8_t slot)
{
    const auto source = p.controlAssets[slot];
    if (!source) { p.workflowMessage = "NO AUDIO TO NORMALIZE"; return; }
    const double gain = s3g::sample::sampleNeonNormalizeGain(*source);
    if (gain == 0.0) { p.workflowMessage = "SILENT OR INVALID SOURCE; UNCHANGED"; return; }
    if (std::abs(gain - 1.0) < 1.0e-6) { p.workflowMessage = "SOURCE ALREADY AT -1 dBFS"; return; }
    NeonAudioBudget stackBudget;
    for (unsigned pad = 0u; pad < 32u; ++pad) stackBudget.pad(p, pad,
        pad == slot ? static_cast<int>(p.selectedLayers[slot].load()) : -1);
    stackBudget.add(p.captureAsset.get());
    if (stackBudget.samples + static_cast<uint64_t>(source->frameCount()) * source->channelCount > kMaximumEmbeddedSamples) {
        p.workflowMessage = "SET AUDIO LIMIT REACHED; SOURCE KEPT"; return;
    }
    try {
        auto normalized = std::make_shared<SampleAsset>(s3g::sample::sampleNeonNormalize(*source));
        auto analysis = s3g::sample::analyzeCutupsAsset(*normalized, 32u, 5.0, 1000u, 20.0);
        { std::lock_guard<std::mutex> lock(p.statusMutex); p.retainedAssets.reserve(p.retainedAssets.size() + 1u); }
        invalidateLayerLoads(p, slot); // Keep other stack layers and their edits.
        publishAsset(p, slot, std::move(normalized), "", true);
        p.embeddedAssets[slot] = true;
        { std::lock_guard<std::mutex> lock(p.statusMutex); p.slotAnalyses[slot] = std::move(analysis); }
        p.pendingCellStops.fetch_or(1u << slot);
        p.workflowMessage = "SOURCE NORMALIZED TO -1 dBFS";
        // Trim, slices, playback, gain, FX and routing are intentionally kept.
        markStateDirty(p); requestProcess(p);
    } catch (...) { p.workflowMessage = "NORMALIZE FAILED"; }
}

void serviceWorkflow(Plugin& p)
{
    if (p.resetAllPhase.load() == 1u && !p.processing.load()) stopSoundForReset(p);
    if (p.resetAllPhase.load() == 2u) { clearAllSound(p); return; }
    if (p.resetAllPhase.load()) return;
    const auto selection = p.pendingLayerSelection.exchange(UINT32_MAX);
    if (selection != UINT32_MAX) showLayer(p, (selection >> 8u) & 31u, selection & 31u);
    if (p.captureState.load(std::memory_order_acquire) == Plugin::CaptureState::Ready) {
        if (p.recorder.count() > 0u) {
            try {
                auto asset = std::make_shared<SampleAsset>(p.recorder.finish());
                std::lock_guard<std::mutex> lock(p.statusMutex);
                p.captureAsset = asset;
                p.retainedAssets.push_back(asset);
                p.publishedCapture.store(asset.get(), std::memory_order_release);
                p.captureStart.store(0.0); p.captureEnd.store(1.0); p.captureCursor.store(0.0);
                p.captureZoom.store(1.0f);
                p.captureLayout.store(static_cast<uint8_t>(p.recordingLayout));
                p.captureState.store(Plugin::CaptureState::Review, std::memory_order_release);
                p.workflowMessage = "CAPTURE READY: REVIEW, THEN ASSIGN AN EMPTY CELL";
            } catch (...) {
                p.captureState.store(Plugin::CaptureState::Empty);
                p.workflowMessage = "CAPTURE COULD NOT BE ALLOCATED";
            }
            setCaptureTrim(p, false, 0.0); setCaptureTrim(p, true, 1.0);
            markStateDirty(p);
        } else p.captureState.store(Plugin::CaptureState::Empty);
    }
    const uint32_t encoded = p.pendingEditCommand.exchange(0u);
    if (!encoded) return;
    const auto command = static_cast<WorkflowCommand>(encoded & 0xffu);
    const auto slot = static_cast<uint8_t>((encoded >> 8u) & 0x1fu);
    const auto slice = static_cast<uint8_t>((encoded >> 16u) & 0x1fu);
    const unsigned destination = (encoded >> 24u) & 0x3fu;
    const unsigned chopTarget = destination ? destination - 1u : s3g::sample::kNeonChopAutoDestination;
    const bool stackDestination = (encoded & (1u << 30u)) != 0u;
    switch (command) {
    case WorkflowCommand::MapSlice: mapChopToBanks(p, slot, false, slice, false, chopTarget, stackDestination); break;
    case WorkflowCommand::MapAll: mapChopToBanks(p, slot, true, 0u, false, chopTarget, stackDestination); break;
    case WorkflowCommand::MapAllReplace: mapChopToBanks(p, slot, true, 0u, true,
        s3g::sample::kNeonChopAutoDestination, stackDestination); break;
    case WorkflowCommand::CropCapture: cropCapture(p); break;
    case WorkflowCommand::Normalize: normalizeCell(p, slot); break;
    case WorkflowCommand::ResetAll:
        p.resetAllPhase.store(1u);
        if (!p.processing.load()) { stopSoundForReset(p); clearAllSound(p); }
        else requestProcess(p);
        break;
    case WorkflowCommand::Equal: setChopMode(p, slot, static_cast<uint8_t>(SampleNeonChopMode::Equal)); break;
    case WorkflowCommand::Transients: setChopMode(p, slot, static_cast<uint8_t>(SampleNeonChopMode::Transient)); break;
    case WorkflowCommand::DiscardCapture:
        if (p.captureState.load() == Plugin::CaptureState::Recording) break;
        p.publishedCapture.store(nullptr);
        p.captureAsset.reset();
        p.captureState.store(Plugin::CaptureState::Empty);
        p.captureFrames.store(0u); p.captureStart.store(0.0); p.captureEnd.store(1.0);
        p.captureCursor.store(0.0); p.capturePlayhead.store(-1.0f);
        p.workflowMessage = "CAPTURE DISCARDED";
        markStateDirty(p);
        break;
    case WorkflowCommand::AssignCapture: {
        const auto target = p.captureTarget.load();
        if (!p.captureAsset || p.captureState.load() != Plugin::CaptureState::Review) break;
        if (!emptyCell(p, target)) {
            p.workflowMessage = "SELECT AN EMPTY CAPTURE TARGET; NOTHING REPLACED";
            break;
        }
        clearSample(p, target);
        for (clap_id offset = 0u; offset < kSlotParameterCount; ++offset)
            setParam(p, slotParamId(target, static_cast<SlotParamOffset>(offset)), kSlotParamDefs[offset].defaultValue);
        setParam(p, slotParamId(target, kSlotZeroCross), 0.0);
        setParam(p, slotParamId(target, kSlotStart), p.captureStart.load());
        setParam(p, slotParamId(target, kSlotEnd), p.captureEnd.load());
        setParam(p, slotParamId(target, kSlotGain), 0.0);
        setParam(p, slotParamId(target, kSlotSourceFormat),
            p.captureLayout.load() >= static_cast<uint8_t>(SampleNeonOutputLayout::Ambisonic1) ? 1.0 : 0.0);
        setParam(p, slotParamId(target, kSlotTriggerMode), static_cast<double>(TriggerMode::OneShot));
        p.textureOptions[target].store(0u); p.editPositions[target].store(0.0);
        for (unsigned effect = 0u; effect < 8u; ++effect)
            for (unsigned n = 0u; n < 3u; ++n)
                p.fxParameters[target][effect][n].store(s3g::sample::kSampleNeonFxDefaults[effect][n]);
        p.slotOptions[target].store(kSlotVelocityOption);
        storeSliceLayout(p, target, s3g::sample::equalSampleNeonSliceLayout(1u), false);
        publishAsset(p, target, p.captureAsset, "", false);
        p.embeddedAssets[target] = true;
        p.parameters[kGlobalParamCount + target * kSlotParameterCount + kSlotZeroCross].store(
            p.captureZeroCross.load() ? 1.0 : 0.0);
        p.chokeGroups[target].store(0u);
        p.statuses[target] = "RESAMPLED / " + std::to_string(p.captureAsset->channelCount) + " CH";
        p.selectedSlot.store(target);
        p.cellBank.store(target / 8u);
        p.workflowMessage = "CAPTURE ASSIGNED / READY TO PLAY";
        if (p.hostParams && p.hostParams->rescan) p.hostParams->rescan(p.host, CLAP_PARAM_RESCAN_VALUES);
        markStateDirty(p);
        break;
    }
    }
}

void capturePadAction(Plugin& p, uint8_t pad, bool secondary, bool shifted = false) noexcept
{
    if (shifted && pad == 0u) {
        // REC on the printed SHIFT layer: do not trigger pad 1 or change target.
        p.captureCommand.store(3);
        requestProcess(p);
        return;
    }
    if (secondary) {
        const auto target = static_cast<uint8_t>(p.visibleBank.load() * 8u + pad);
        if (p.publishedAssets[target].load()) {
            // The same performance path as PLAY: do not choke Toggle, alter
            // the capture destination, or feed review audio into the recorder.
            p.selectedSlot.store(target);
            auditionSlot(p, target);
        } else {
            p.captureTarget.store(target);
            if (p.captureState.load() == Plugin::CaptureState::Review)
                p.captureAudition.store(true);
        }
        requestProcess(p);
        return;
    }
    switch (pad) {
    case 0u: p.captureCommand.store(1); break;
    case 1u: p.captureCommand.store(2); break;
    case 2u: p.captureAudition.store(true); break;
    case 3u: requestWorkflow(p, WorkflowCommand::DiscardCapture); break;
    case 4u: if (p.captureBus.load() > 0u) p.captureBus.fetch_sub(1u); break;
    case 5u: {
        const auto layout = static_cast<SampleNeonOutputLayout>(static_cast<uint8_t>(paramValue(p, kOutputLayoutParamId)));
        if (p.captureBus.load() + 1u < s3g::sample::sampleNeonBusCount(layout)) p.captureBus.fetch_add(1u);
        break;
    }
    case 6u:
        if (shifted) requestWorkflow(p, WorkflowCommand::CropCapture);
        else { selectSurface(p, NeonMode::HotLoop, 1u); p.captureAudition.store(true); }
        break;
    case 7u: requestWorkflow(p, WorkflowCommand::AssignCapture); break;
    }
    requestProcess(p);
}

void editToolAction(Plugin& p, uint8_t pad) noexcept
{
    const auto slot = p.selectedSlot.load();
    if (pad == 0u) p.editPage.store(0u); // Source editing never changes technique.
    else if (pad < 4u) {
        choosePlayback(p, pad - 1u);
    } else if (pad < 7u) {
        setParam(p, slotParamId(slot, kSlotTriggerMode), pad == 4u ? 2.0 : pad == 5u ? 1.0 : 3.0, true);
    } else {
        // Stop is a momentary choke, never a persistent mute or solo mask.
        p.pendingCellStops.fetch_or(1u << slot);
    }
    requestProcess(p);
}

void chopToolAction(Plugin& p, uint8_t pad, bool shifted = false) noexcept
{
    const auto slot = p.selectedSlot.load();
    const auto cursor = p.editPositions[slot].load();
    switch (pad) {
    case 0u: addLiveSliceMarker(p, slot, cursor); break;
    case 1u: removeNearestSliceMarker(p, slot, cursor); break;
    case 2u: auditionSlice(p, slot, p.visibleSlice.load()); break;
    case 3u: setParam(p, slotParamId(slot, kSlotZeroCross),
        paramValue(p, slotParamId(slot, kSlotZeroCross)) < 0.5 ? 1.0 : 0.0, true); break;
    case 4u: requestWorkflow(p, WorkflowCommand::Equal); break;
    case 5u: requestWorkflow(p, WorkflowCommand::Transients); break;
    case 6u: requestWorkflow(p, WorkflowCommand::MapSlice); break;
    case 7u: requestWorkflow(p, shifted ? WorkflowCommand::MapAllReplace : WorkflowCommand::MapAll); break;
    }
}

template<std::size_t N> bool writeEmbeddedAudio(const clap_ostream_t* stream,
    const std::array<const SampleAsset*, N>& assets)
{
    uint64_t samples = 0u;
    for (std::size_t index = 0u; index < assets.size(); ++index) {
        const auto* asset = assets[index];
        uint32_t channels = asset ? asset->channelCount : 0u;
        uint32_t frames = asset ? asset->frameCount() : 0u;
        double rate = asset ? asset->sampleRate : 0.0;
        for (std::size_t previous = 0u; asset && previous < index; ++previous)
            if (assets[previous] == asset) {
                channels = UINT32_MAX; frames = static_cast<uint32_t>(previous); rate = 0.0;
                break;
            }
        if (channels != UINT32_MAX) {
            samples += static_cast<uint64_t>(channels) * frames;
            if (samples > kMaximumEmbeddedSamples) return false;
        }
        if (!s3g::clap_state::writeAll(stream, &channels, sizeof(channels))
            || !s3g::clap_state::writeAll(stream, &frames, sizeof(frames))
            || !s3g::clap_state::writeAll(stream, &rate, sizeof(rate))) return false;
        if (channels != UINT32_MAX)
            for (uint32_t channel = 0u; channel < channels; ++channel)
                if (!s3g::clap_state::writeAll(stream, asset->channels[channel].data(),
                    frames * sizeof(float))) return false;
    }
    return true;
}

template<std::size_t N> bool readEmbeddedAudio(const clap_istream_t* stream,
    std::array<std::shared_ptr<const SampleAsset>, N>& assets)
{
    uint64_t samples = 0u;
    try {
        for (std::size_t index = 0u; index < assets.size(); ++index) {
            uint32_t channels = 0u, frames = 0u;
            double rate = 0.0;
            if (!s3g::clap_state::readAll(stream, &channels, sizeof(channels))
                || !s3g::clap_state::readAll(stream, &frames, sizeof(frames))
                || !s3g::clap_state::readAll(stream, &rate, sizeof(rate))) return false;
            if (channels == UINT32_MAX) {
                if (frames >= index || !assets[frames] || rate != 0.0) return false;
                assets[index] = assets[frames];
                continue;
            }
            if (!channels) {
                if (frames || rate != 0.0) return false;
                continue;
            }
            samples += static_cast<uint64_t>(channels) * frames;
            if (!s3g::sample::sampleNeonChannelCountSupported(channels)
                || !frames
                || !std::isfinite(rate) || rate < 8000.0 || rate > 384000.0
                || samples > kMaximumEmbeddedSamples) return false;
            auto asset = std::make_shared<SampleAsset>();
            asset->channelCount = static_cast<uint8_t>(channels); asset->sampleRate = rate;
            for (uint32_t channel = 0u; channel < channels; ++channel) {
                asset->channels[channel].resize(frames);
                if (!s3g::clap_state::readAll(stream, asset->channels[channel].data(),
                    frames * sizeof(float))) return false;
            }
            if (!asset->valid()) return false;
            assets[index] = std::move(asset);
        }
    } catch (...) { return false; }
    return true;
}
