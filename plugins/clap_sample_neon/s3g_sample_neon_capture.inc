// Block-boundary recording with a bounded two-buffer handoff. Finished buffers
// are immutable until the main thread has copied/published them and clears Ready.
unsigned captureInputWidth(const Plugin& p) noexcept {
    constexpr unsigned widths[] {1, 2, 4, 8, 4, 9, 16};
    return widths[std::min<unsigned>(6, p.captureInputFormat.load())];
}
uint8_t captureInputLayout(const Plugin& p) noexcept {
    constexpr uint8_t layouts[] {0, 0, 2, 3, 4, 5, 6};
    return layouts[std::min<unsigned>(6, p.captureInputFormat.load())];
}
s3g::sample::SampleNeonRecorder& captureRecorderAt(Plugin& p, unsigned index) noexcept {
    return index ? p.nextRecorder : p.recorder;
}
bool captureRunning(const Plugin& p) noexcept {
    return p.captureState.load() == Plugin::CaptureState::Recording;
}
bool captureSetupBusy(const Plugin& p) noexcept {
    const int command = p.captureCommand.load();
    return captureRunning(p) || p.captureReady.load() || (command != 0 && command != 2) || p.resetAllPhase.load();
}
int nextCaptureLayer(const Plugin& p, unsigned pad, unsigned first) noexcept {
    const uint32_t occupied = p.captureOccupied[pad].load(std::memory_order_acquire) | p.captureReserved.load();
    for (unsigned n = first; n < 32; ++n) if (!(occupied & (1u << n))) return static_cast<int>(n);
    return -1;
}
void captureNotify(Plugin& p, uint8_t notice = 0) noexcept {
    if (notice) p.captureNotice.store(notice);
    if (p.host && p.host->request_callback) p.host->request_callback(p.host);
}
void finishCaptureBuffer(Plugin& p) noexcept {
    const unsigned index = p.captureRecorder.load();
    p.captureState.store(Plugin::CaptureState::Ready, std::memory_order_release);
    p.captureReady.fetch_or(static_cast<uint8_t>(1u << index), std::memory_order_release);
    captureNotify(p);
}
// Copy input before writing any output, including hosts that alias buffers.
// Missing pins refuse input recording; a connected silent pin records silence.
bool prepareCaptureInput(Plugin& p, const clap_process_t& process) noexcept {
    const unsigned width = captureInputWidth(p), first = p.captureInputGroup.load() * width;
    const clap_audio_buffer_t* input = process.audio_inputs_count && process.audio_inputs ? &process.audio_inputs[0] : nullptr;
    bool connected = input && input->channel_count >= first + width && (input->data32 || input->data64);
    float peak = 0;
    for (unsigned ch = 0; ch < width; ++ch) {
        const auto* f = connected && input->data32 ? input->data32[first + ch] : nullptr;
        const auto* d = connected && input->data64 ? input->data64[first + ch] : nullptr;
        if (!f && !d) connected = false;
        for (unsigned n = 0; n < process.frames_count; ++n) {
            const double value = f ? f[n] : d ? d[n] : 0;
            const float safe = std::isfinite(value) ? static_cast<float>(std::clamp(value, -1.e6, 1.e6)) : 0.f;
            p.captureInputScratch[ch][n] = safe; peak = std::max(peak, std::abs(safe));
        }
    }
    p.captureInputPeak.store(peak, std::memory_order_relaxed);
    return connected;
}
void processCapture(Plugin& p, const SampleNeonSettings& settings, float* const* output,
    uint32_t frames, bool inputConnected) noexcept {
    int command = p.captureCommand.exchange(0);
    if (command == 3) command = captureRunning(p) ? 2 : 1;
    if (command == 1 && (p.captureState.load() == Plugin::CaptureState::Empty || p.captureState.load() == Plugin::CaptureState::Review) && !p.captureReady.load()
        && (p.captureToStack.load() || p.captureState.load() == Plugin::CaptureState::Empty)) {
        if (p.pendingEditCommand.load() || p.resetAllPhase.load()) { captureNotify(p, 9); return; }
        if (p.captureLoadsPending.load()) { captureNotify(p, 8); return; }
        p.recordingInput = p.captureSource.load() == 1;
        if (p.recordingInput && !inputConnected) { captureNotify(p, 1); return; }
        const uint8_t layout = p.recordingInput ? captureInputLayout(p) : static_cast<uint8_t>(settings.outputLayout);
        const unsigned width = p.recordingInput ? captureInputWidth(p) : s3g::sample::sampleNeonBusWidth(settings.outputLayout);
        p.recordingSamplesLeft = p.captureAvailableSamples.load();
        if (p.recordingSamplesLeft < width) { captureNotify(p, 7); return; }
        Plugin::CaptureSegment segment; segment.layout = layout; segment.stack = p.captureToStack.load();
        p.captureReserved.store(0);
        if (segment.stack) {
            segment.pad = p.captureTarget.load();
            if (segment.pad >= 32) { captureNotify(p, 2); return; }
            const unsigned desired = p.captureLayer.load();
            const int layer = nextCaptureLayer(p, segment.pad, desired < 32 ? desired : 0);
            if (layer < 0 || (desired < 32 && unsigned(layer) != desired)) { captureNotify(p, 3); return; }
            const auto* stack = p.publishedStacks[segment.pad].load(std::memory_order_acquire);
            const auto* existing = renderBase(stack);
            if (existing && (existing->channelCount != width
                || (paramValue(p, slotParamId(segment.pad, kSlotSourceFormat)) >= .5) != (layout >= 4))) {
                captureNotify(p, 4); return;
            }
            segment.layer = static_cast<uint8_t>(layer);
            p.captureReserved.store(1u << segment.layer);
            p.captureRecordingPad.store(segment.pad);
            p.captureRecordingLayer.store(segment.layer);
        }
        segment.order = ++p.captureOrder;
        p.captureRecorder.store(0); p.captureSegments[0] = segment;
        p.recordingLayout = static_cast<SampleNeonOutputLayout>(layout);
        p.recordingBus = std::min<uint8_t>(p.captureBus.load(),
            static_cast<uint8_t>(s3g::sample::sampleNeonBusCount(settings.outputLayout) - 1));
        p.recorder.start(width); p.captureFrames.store(0);
        p.captureAudition.store(false); p.capturePreview.killAll();
        p.captureState.store(Plugin::CaptureState::Recording, std::memory_order_release);
        p.captureNotice.store(0);
    }
    if (!captureRunning(p)) return;
    const bool lostInput = p.recordingInput && !inputConnected;
    if (command == 2 || lostInput || (!p.recordingInput && settings.outputLayout != p.recordingLayout)) {
        finishCaptureBuffer(p); if (lostInput) captureNotify(p, 1); return;
    }
    unsigned index = p.captureRecorder.load();
    if (command == 4 && p.captureSegments[index].stack && captureRecorderAt(p, index).count()) {
        const auto segment = p.captureSegments[index];
        const int next = nextCaptureLayer(p, segment.pad, segment.layer + 1u);
        if (next < 0) { finishCaptureBuffer(p); captureNotify(p, 3); return; }
        const unsigned other = 1u - index;
        if (p.captureReady.load(std::memory_order_acquire) & (1u << other)) captureNotify(p, 5);
        else {
            const unsigned width = captureRecorderAt(p, index).waveChannels.load();
            p.captureSegments[other] = segment;
            p.captureSegments[other].layer = static_cast<uint8_t>(next);
            p.captureSegments[other].order = ++p.captureOrder;
            captureRecorderAt(p, other).start(width);
            p.captureReserved.fetch_or(1u << next);
            p.captureRecordingLayer.store(static_cast<uint8_t>(next));
            p.captureRecorder.store(static_cast<uint8_t>(other), std::memory_order_release);
            p.captureReady.fetch_or(static_cast<uint8_t>(1u << index), std::memory_order_release);
            index = other; captureNotify(p);
        }
    }
    std::array<float*, 16> input {};
    for (unsigned ch = 0; ch < input.size(); ++ch) input[ch] = p.captureInputScratch[ch].data();
    auto& recorder = captureRecorderAt(p, index);
    const auto width = recorder.waveChannels.load();
    const uint32_t allowed = static_cast<uint32_t>(std::min<uint64_t>(frames, p.recordingSamplesLeft / width));
    const uint32_t before = recorder.count();
    const bool full = recorder.append(p.recordingInput ? input.data() : output, allowed,
        p.recordingInput ? 0 : p.recordingBus * s3g::sample::sampleNeonBusWidth(p.recordingLayout));
    p.recordingSamplesLeft -= static_cast<uint64_t>(recorder.count() - before) * width;
    p.captureFrames.store(recorder.count());
    if (full || allowed < frames || p.recordingSamplesLeft < width) { finishCaptureBuffer(p); captureNotify(p, full ? 6 : 7); }
}

bool assignCaptureLayer(Plugin& p, unsigned pad, unsigned layer, std::shared_ptr<const SampleAsset> asset,
    uint8_t layout, bool recording = false) {
    if (pad >= 32 || layer >= 32 || !asset || p.sources[pad][layer].asset
        || !p.sources[pad][layer].path.empty() || p.pendingSourceLoads[pad][layer]) {
        p.workflowMessage = "CHOOSE AN EMPTY LAYER; EXISTING AUDIO KEPT"; return false;
    }
    if (!layerWidthCompatible(p, pad, layer, *asset)) {
        p.workflowMessage = "TAKE WIDTH DOES NOT MATCH STACK"; return false;
    }
    if (renderBase(p.publishedStacks[pad].load(std::memory_order_acquire))
        && (paramValue(p, slotParamId(pad, kSlotSourceFormat)) >= .5) != (layout >= 4)) {
        p.workflowMessage = "TAKE FORMAT DOES NOT MATCH STACK"; return false;
    }
    NeonAudioBudget budget;
    for (unsigned n = 0; n < 32; ++n) budget.pad(p, n);
    budget.add(p.captureAsset.get()); budget.add(asset.get());
    if (budget.samples > kMaximumEmbeddedSamples) { p.workflowMessage = "SET AUDIO LIMIT; TAKE KEPT FOR REVIEW"; return false; }
    auto analysis = s3g::sample::analyzeCutupsAsset(*asset, 64, 5, 1000, 20);
    NeonHistoryEdit edit(p, "RECORD / ASSIGN " + chopPadName(pad) + " L" + std::to_string(layer + 1),
        1u << pad, recording, false, recording);
    if (!edit) return false;
    saveLayerEdit(p, pad);
    const bool first = !p.captureOccupied[pad].load();
    p.retainedAssets.reserve(p.retainedAssets.size() + 1);
    // Keep the checkpoint even if a subsequent publication runs out of memory.
    edit.commit();
    auto& source = p.sources[pad][layer]; source = {};
    source.asset = asset; source.analysis = std::move(analysis);
    source.start = 0; source.end = 1; source.slices = s3g::sample::equalSampleNeonSliceLayout(1);
    p.retainedAssets.push_back(asset);
    if (first) {
        p.parameters[kGlobalParamCount + pad * kSlotParameterCount + kSlotSourceFormat].store(layout >= 4 ? 1 : 0);
        p.parameters[kGlobalParamCount + pad * kSlotParameterCount + kSlotGain].store(0);
        if (layer) p.sourceModes[pad].store(1); // A sparse first take is the playable selected layer.
    }
    showLayer(p, pad, layer, false);
    if (recording) {
        // Review and saved layer share immutable PCM, not an extra audio copy.
        // Take/Next continues drawing the active recorder; Stop reveals this.
        p.captureAsset = asset; p.publishedCapture.store(asset.get(), std::memory_order_release);
        p.captureStart.store(0); p.captureEnd.store(1); p.captureCursor.store(0); p.captureZoom.store(1);
        p.capturePlayhead.store(-1); p.captureLayout.store(layout);
        rememberSavedCapture(p, pad, layer);
    }
    p.selectedSlot.store(static_cast<uint8_t>(pad));
    p.workflowMessage = "TAKE > " + chopPadName(pad) + " / LAYER " + std::to_string(layer + 1);
    markStateDirty(p); requestProcess(p); return true;
}
void serviceCapture(Plugin& p) {
    uint8_t ready = p.captureReady.load(std::memory_order_acquire);
    if (!ready) return;
    unsigned index = (ready & 1) ? 0 : 1;
    if (ready == 3 && p.captureSegments[1].order < p.captureSegments[0].order) index = 1;
    const auto segment = p.captureSegments[index];
    auto& recorder = captureRecorderAt(p, index);
    if (recorder.count()) {
        try {
            auto asset = std::make_shared<SampleAsset>(recorder.finish());
            bool assigned = segment.stack && assignCaptureLayer(p, segment.pad, segment.layer, asset, segment.layout, true);
            if (!assigned) {
                // If assignment fails, stop first, then preserve the take as an
                // undoable review. Never overwrite a source or drop captured PCM.
                if (captureRunning(p)) { p.captureCommand.store(2); requestProcess(p); return; }
                NeonHistoryEdit edit(p, segment.stack ? "UNASSIGNED STACK TAKE" : "RECORD TAKE", 0, true, false, true);
                if (!edit) return;
                p.retainedAssets.push_back(asset);
                p.captureAsset = asset; forgetSavedCapture(p); p.publishedCapture.store(asset.get(), std::memory_order_release);
                p.captureStart.store(0); p.captureEnd.store(1); p.captureCursor.store(0); p.captureZoom.store(1);
                p.captureLayout.store(segment.layout);
                setCaptureTrim(p, false, 0); setCaptureTrim(p, true, 1);
                p.workflowMessage = segment.stack ? "STACK ASSIGNMENT FAILED; TAKE KEPT IN REVIEW / UNDO RETAINS PRIOR REVIEW"
                    : "CAPTURE READY: REVIEW, THEN ASSIGN";
                edit.commit();
            }
            markStateDirty(p);
        } catch (...) {
            p.workflowMessage = "TAKE BUFFER KEPT; CANNOT PUBLISH AUDIO";
            if (captureRunning(p)) { p.captureCommand.store(2); requestProcess(p); }
            return;
        }
    }
    if (segment.stack) p.captureReserved.fetch_and(~(1u << segment.layer));
    p.captureReady.fetch_and(static_cast<uint8_t>(~(1u << index)), std::memory_order_release);
    // Only the last active buffer may finish the session. The audio thread can
    // be between its Ready-state store and Ready-bit publication while main is
    // releasing an older take; that older take must not clear the session.
    if (index == p.captureRecorder.load() && !captureRunning(p) && !p.captureReady.load()) {
        p.captureRecordingPad.store(255); p.captureReserved.store(0);
        p.captureFrames.store(p.captureAsset ? p.captureAsset->frameCount() : 0);
        p.captureState.store(p.captureAsset ? Plugin::CaptureState::Review : Plugin::CaptureState::Empty, std::memory_order_release);
    }
    if (p.captureReady.load()) captureNotify(p);
}
