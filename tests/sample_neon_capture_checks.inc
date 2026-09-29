// Same actual bridge as the journal test: no fake recorder implementation.
struct LiveCaptureFixture {
    std::unique_ptr<Plugin> instance = historyFixture();
    std::array<std::array<float, 64>, 32> input {}, output {};
    std::array<std::array<double, 64>, 32> wideInput {};
    std::array<float*, 32> inputPointers {}, outputPointers {};
    std::array<double*, 32> widePointers {};
    clap_audio_buffer_t in {}, out {};
    clap_process_t block {};
    unsigned offset = 0;
    LiveCaptureFixture() {
        for (unsigned ch = 0; ch < 32; ++ch) {
            inputPointers[ch] = input[ch].data(); outputPointers[ch] = output[ch].data(); widePointers[ch] = wideInput[ch].data();
        }
        in.data32 = inputPointers.data(); in.channel_count = 32;
        out.data32 = outputPointers.data(); out.channel_count = 32;
        block.audio_inputs = &in; block.audio_inputs_count = 1;
        block.audio_outputs = &out; block.audio_outputs_count = 1; block.frames_count = 64;
        auto& p = *instance;
        check(pluginActivate(&p.plugin, 48000, 1, 64) && pluginStartProcessing(&p.plugin), "live activate");
        p.captureSource.store(1); p.captureToStack.store(true); p.captureTarget.store(24);
    }
    ~LiveCaptureFixture() { pluginStopProcessing(&instance->plugin); pluginDeactivate(&instance->plugin); }
    static float value(unsigned ch, unsigned frame) { return (ch % 2 ? -1.f : 1.f) * (.01f * (ch + 1) + .00001f * frame); }
    void process(bool refill = true) {
        if (refill) for (unsigned ch = 0; ch < 32; ++ch) for (unsigned n = 0; n < 64; ++n) {
            input[ch][n] = value(ch, offset + n); wideInput[ch][n] = value(ch, offset + n);
        }
        check(pluginProcess(&instance->plugin, &block) != CLAP_PROCESS_ERROR, "live process"); offset += 64;
    }
    void service() { serviceWorkflow(*instance); }
    void stopped() { pluginStopProcessing(&instance->plugin); pluginDeactivate(&instance->plugin); }
};

void testLiveCapture() {
    {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        p.captureInputFormat.store(6); // 16-channel third order, independent of stereo output.
        const auto before = historyState(p);
        capturePadAction(p, 0, false); fixture.process(); fixture.process();
        check(captureRunning(p) && p.captureFrames.load() == 128, "record latches without a held button");
        for (const auto& channel : fixture.output) for (float x : channel) check(x == 0, "input monitor defaults off");
        capturePadAction(p, 1, false, true); fixture.process(); // Shift+2 / Take.
        check(captureRunning(p) && p.captureRecordingLayer.load() == 1 && p.captureReady.load() == 1, "take advances without stopping");
        capturePadAction(p, 1, false, true); fixture.process(); // Consumer deliberately stalled.
        check(p.captureRecordingLayer.load() == 1 && p.captureFrames.load() == 128, "busy take continues current recording");
        fixture.service(); check(p.sources[24][0].asset && p.captureReady.load() == 0, "first take published while next records");
        removeLayer(p, 24); check(p.sources[24][0].asset && captureRunning(p), "recording destination protected from destructive edit");
        capturePadAction(p, 1, false, true); fixture.process(); fixture.service();
        check(p.captureRecordingLayer.load() == 2 && p.sources[24][1].asset, "third layer running");
        capturePadAction(p, 0, false); fixture.process(); fixture.service();
        check(!captureRunning(p) && !p.captureReady.load() && p.captureRecordingPad.load() == 255, "same Record press stops and finalizes");
        unsigned cursor = 0;
        for (unsigned layer = 0; layer < 3; ++layer) {
            const auto& audio = p.sources[24][layer].asset;
            check(audio && audio->channelCount == 16 && audio->frameCount() == (layer < 2 ? 128 : 64), "take length and width");
            for (unsigned ch = 0; ch < 16; ++ch) for (unsigned n = 0; n < audio->frameCount(); ++n)
                check(audio->channels[ch][n] == LiveCaptureFixture::value(ch, cursor + n), "gapless consecutive takes preserve channel order/polarity");
            cursor += audio->frameCount();
        }
        check(cursor == 320 && paramValue(p, slotParamId(24, kSlotSourceFormat)) == 1, "ACN SN3D flagged, never converted");
        check(p.captureLayer.load() == 255, "next-empty preference stays next-empty");
        fixture.stopped(); const auto after = historyState(p);
        for (unsigned n = 0; n < 3; ++n) historyStep(p);
        check(historyState(p) == before, "all stack takes undo exactly");
        for (unsigned n = 0; n < 3; ++n) historyStep(p, true);
        check(historyState(p) == after, "all stack takes redo exactly");
    }
    {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        p.captureToStack.store(false); p.captureInputGroup.store(3); p.captureMonitor.store(true);
        fixture.in.data32 = nullptr; fixture.in.data64 = fixture.widePointers.data();
        for (unsigned ch = 0; ch < 32; ++ch) for (unsigned n = 0; n < 64; ++n) fixture.wideInput[ch][n] = LiveCaptureFixture::value(ch, n);
        fixture.wideInput[6][3] = NAN; fixture.wideInput[7][4] = INFINITY;
        capturePadAction(p, 0, false); fixture.process(false);
        check(fixture.output[6][0] == LiveCaptureFixture::value(6, 0) && fixture.output[0][0] == 0, "explicit monitor retains channel pins");
        capturePadAction(p, 0, false); fixture.process(); fixture.service();
        check(p.captureAsset && p.captureAsset->channelCount == 2 && p.captureAsset->frameCount() == 64, "double input records selected pair to review");
        check(p.captureAsset->channels[0][3] == 0 && p.captureAsset->channels[1][4] == 0, "nonfinite input sanitized");
        check(p.captureAsset->channels[0][0] == LiveCaptureFixture::value(6, 0), "input capture is pre-monitor/pre-master");
        auto take = p.captureAsset;
        capturePadAction(p, 0, false); fixture.process(); fixture.service();
        check(!captureRunning(p) && p.captureAsset == take, "review cannot be silently overwritten by Record");
        fixture.stopped();
        const auto saved = historyState(p); check(saved[4] == 26, "record setup state extension");
        struct Read {const std::vector<uint8_t>& bytes; size_t pos = 0;} read {saved};
        clap_istream_t stream {&read, [](const clap_istream_t* in, void* dst, uint64_t size) -> int64_t {
            auto& r = *static_cast<Read*>(in->ctx); size = std::min<uint64_t>(size, r.bytes.size() - r.pos);
            std::memcpy(dst, r.bytes.data() + r.pos, size); r.pos += size; return size;
        }};
        check(stateLoad(&p.plugin, &stream) && historyState(p) == saved, "record setup + PCM roundtrip");
    }
    for (unsigned format : {0u, 2u, 3u, 4u, 5u}) {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        p.captureInputFormat.store(static_cast<uint8_t>(format)); p.captureLayer.store(4);
        capturePadAction(p, 0, false); fixture.process();
        capturePadAction(p, 0, false); fixture.process(); fixture.service();
        check(p.sources[24][4].asset && p.sources[24][4].asset->channelCount == captureInputWidth(p), "mono/quad/octo/1OA/2OA explicit layer");
        check(!p.sources[24][0].asset && p.sources[24][4].asset->frameCount() == 64, "chosen layer respected without filling earlier layers");
    }
    {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        fixture.block.audio_inputs_count = 0;
        capturePadAction(p, 0, false); fixture.process();
        check(!captureRunning(p) && p.captureNotice.load() == 1, "unconnected input refuses recording");
        fixture.block.audio_inputs_count = 1;
        historySource(p, 24, 0, historyAudio(4));
        capturePadAction(p, 0, false); fixture.process();
        check(!captureRunning(p) && p.captureNotice.load() == 4, "mismatched stack width protected");
        p.captureInputFormat.store(2); p.captureLayer.store(31);
        capturePadAction(p, 0, false); fixture.process();
        capturePadAction(p, 1, false, true); fixture.process(); fixture.service();
        check(!captureRunning(p) && p.sources[24][31].asset && !p.sources[24][1].asset, "last layer Take stops without wrapping/overwriting");
        capturePadAction(p, 0, false); fixture.process();
        check(!captureRunning(p), "occupied explicit layer refuses recording");
    }
    {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        p.captureAvailableSamples.store(2 * 65); // Simulated remaining set budget.
        capturePadAction(p, 0, false); fixture.process(); fixture.process();
        check(!captureRunning(p) && p.captureNotice.load() == 7, "recording stops at remaining PCM budget");
        fixture.service();
        check(p.sources[24][0].asset && p.sources[24][0].asset->frameCount() == 65, "budget stop retains exact permitted frames");
    }
    {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        capturePadAction(p, 0, false); fixture.process();
        capturePadAction(p, 1, false, true); fixture.process();
        // Exercise the publication interleave deterministically: main finishes
        // the older take after audio stores Ready but before publishing its bit.
        p.captureState.store(Plugin::CaptureState::Ready);
        fixture.service();
        check(p.captureState.load() == Plugin::CaptureState::Ready && p.captureRecordingPad.load() == 24,
            "older buffer cannot clear an in-flight final publication");
        p.captureReady.fetch_or(1u << p.captureRecorder.load()); fixture.service();
        check(p.captureState.load() == Plugin::CaptureState::Review && p.captureAsset == p.sources[24][1].asset,
            "last active buffer completes the recording session");
    }
}

void testRetainedCaptureReview() {
    for (bool internal : {false, true}) for (unsigned format : {1u, 2u, 6u}) {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        p.captureInputFormat.store(static_cast<uint8_t>(format));
        setParam(p, kOutputLayoutParamId, captureInputLayout(p));
        if (internal) {
            historySource(p, 0, 0, historyAudio(captureInputWidth(p)));
            setParam(p, slotParamId(0, kSlotSourceFormat), format == 6 ? 1 : 0);
            setParam(p, kMasterGainParamId, 0); setParam(p, slotParamId(0, kSlotGain), 0);
            p.captureSource.store(0); auditionSlot(p, 0);
        }
        capturePadAction(p, 0, false);
        for (unsigned n = 0; n < 4; ++n) fixture.process();
        capturePadAction(p, 0, false); fixture.process(); fixture.service();
        check(p.captureState.load() == Plugin::CaptureState::Review && p.captureAsset == p.sources[24][0].asset
            && p.publishedCapture.load() == p.captureAsset.get() && p.captureFrames.load() == 256
            && savedCaptureValid(p), "stopped stack take remains visible as shared review PCM");
        auto take = p.captureAsset;
        p.pendingCellStops.fetch_or(1u); fixture.process(); // Silence any internal source voice.
        capturePadAction(p, 2, false); fixture.process();
        float peak = 0; for (const auto& ch : fixture.output) for (float value : ch) peak = std::max(peak, std::abs(value));
        check(peak > 1e-6f && p.capturePlayhead.load() >= 0, "saved stereo/quad/3OA take auditions from Resample");
        capturePadAction(p, 1, false); fixture.process();
        peak = 0; for (const auto& ch : fixture.output) for (float value : ch) peak = std::max(peak, std::abs(value));
        check(peak == 0 && p.capturePlayhead.load() < 0 && p.captureAsset == take, "Stop ends audition without clearing review");
        fixture.stopped();
        // Changing the currently selected pad/target never redirects Delete Take.
        historySource(p, 8, 0, historyAudio(captureInputWidth(p)));
        p.selectedSlot.store(8); p.captureTarget.store(8);
        const auto beforeDelete = historyState(p); const auto other = p.sources[8][0].asset;
        workflow(p, WorkflowCommand::DiscardCapture);
        check(!p.captureAsset && !p.sources[24][0].asset && p.sources[8][0].asset == other
            && p.captureState.load() == Plugin::CaptureState::Empty, "Delete Take removes only recorded layer and its review");
        const auto deleted = historyState(p);
        historyStep(p);
        check(historyState(p) == beforeDelete && p.captureAsset == take && p.sources[24][0].asset == take
            && savedCaptureValid(p), "Undo Delete restores layer, review and exact deletion link");
        historyStep(p, true); check(historyState(p) == deleted, "Redo Delete exact");
        historyStep(p);
        workflow(p, WorkflowCommand::ClearCaptureReview);
        check(!p.captureAsset && p.sources[24][0].asset == take, "Keep Layer / Clear Review preserves recorded layer");
        historyStep(p);
        check(historyState(p) == beforeDelete && savedCaptureValid(p), "Undo Clear restores review and saved link");
        // Replacing that layer invalidates the deletion link, even while the
        // old recording remains available for independent review/audition.
        auto replacement = historyAudio(captureInputWidth(p), .5f);
        historySource(p, 24, 0, replacement);
        check(!savedCaptureValid(p), "changed source cannot inherit recorded-take deletion target");
        workflow(p, WorkflowCommand::DiscardCapture);
        check(!p.captureAsset && p.sources[24][0].asset == replacement && p.sources[8][0].asset == other,
            "unlinked Discard only clears review; replacement and other pad are kept");
    }
    {
        LiveCaptureFixture fixture; auto& p = *fixture.instance;
        capturePadAction(p, 0, false); fixture.process(); fixture.process();
        capturePadAction(p, 0, false); fixture.process(); fixture.service(); fixture.stopped();
        const auto bytes = historyState(p);
        struct Reader { const std::vector<uint8_t>& bytes; size_t position = 0; } reader {bytes};
        clap_istream_t stream {&reader, [](const clap_istream_t* in, void* out, uint64_t count) -> int64_t {
            auto& r = *static_cast<Reader*>(in->ctx); count = std::min<uint64_t>(count, r.bytes.size() - r.position);
            std::memcpy(out, r.bytes.data() + r.position, count); r.position += count; return count;
        }};
        check(stateLoad(&p.plugin, &stream) && historyState(p) == bytes && p.captureAsset
            && p.captureState.load() == Plugin::CaptureState::Review && !savedCaptureValid(p),
            "saved set retains completed review PCM without inheriting a session-only deletion target");
        auto saved = p.sources[24][0].asset; workflow(p, WorkflowCommand::DiscardCapture);
        check(!p.captureAsset && p.sources[24][0].asset == saved, "recalled review Discard never removes saved layer");
    }
}
