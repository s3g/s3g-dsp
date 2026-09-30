// White-box main-thread edit regressions. This executable has no GUI and is
// never an installable plugin; the real VSTGUI bundle has separate GUI tests.
#include "../plugins/clap_sample_neon/s3g_sample_neon_clap.cpp"
#include <iostream>
#include "generated_sample_media_checks.h"

namespace {
unsigned checks = 0;
void check(bool result, const char* message) {
    ++checks;
    if (!result) throw std::runtime_error(message);
}
std::unique_ptr<Plugin> historyFixture() {
    auto p = std::make_unique<Plugin>(); initializeSoundDefaults(p.get());
    p->storageMode = StorageMode::Embed; p->plugin.plugin_data = p.get();
    return p;
}
std::shared_ptr<const SampleAsset> historyAudio(unsigned channels = 2, float level = .2f) {
    auto audio = std::make_shared<SampleAsset>(); audio->channelCount = static_cast<uint8_t>(channels);
    for (unsigned ch = 0; ch < channels; ++ch) {
        audio->channels[ch].resize(1024);
        for (unsigned n = 0; n < 1024; ++n)
            audio->channels[ch][n] = level * std::sin(static_cast<float>(n) * .1f) * (ch % 2 ? -1.f : 1.f);
    }
    return audio;
}
void historySource(Plugin& p, unsigned pad, unsigned layer, std::shared_ptr<const SampleAsset> audio) {
    if (layer) saveLayerEdit(p, pad);
    p.selectedLayers[pad].store(static_cast<uint8_t>(layer));
    p.slotAnalyses[pad] = s3g::sample::analyzeCutupsAsset(*audio, 64, 5, 1000, 20);
    check(publishAsset(p, pad, std::move(audio), "", false), "publish fixture");
}
std::vector<uint8_t> historyState(Plugin& p) {
    std::vector<uint8_t> bytes;
    clap_ostream_t stream {&bytes, [](const clap_ostream_t* out, const void* data, uint64_t size) -> int64_t {
        auto& result = *static_cast<std::vector<uint8_t>*>(out->ctx);
        const auto* begin = static_cast<const uint8_t*>(data); result.insert(result.end(), begin, begin + size);
        return static_cast<int64_t>(size);
    }};
    check(stateSave(&p.plugin, &stream), "save state"); return bytes;
}
void historyStep(Plugin& p, bool redo = false) {
    check(requestHistoryRestore(p, redo), "history step available");
    serviceWorkflow(p);
    check(p.resetAllPhase.load() == 0 && !p.historyRestorePending.load(), "history restored without processing");
}
void workflow(Plugin& p, WorkflowCommand command) { requestWorkflow(p, command); serviceWorkflow(p); }

void testNormalizeRemovePaste() {
    auto instance = historyFixture(); auto& p = *instance;
    auto a = historyAudio(16), b = historyAudio(16, .4f);
    historySource(p, 0, 0, a); historySource(p, 0, 1, b);
    setParam(p, slotParamId(0, kSlotSourceFormat), 1);
    setParam(p, slotParamId(0, kSlotStart), .12);
    setParam(p, slotParamId(0, kSlotEnd), .86);
    p.textureOptions[0].store(kCutupsOption);
    p.familyControls[0][neonFamilyIndex(NeonFamily::CutJoin)].store(9);
    p.fxParameters[0][2][3].store(.7f);
    const auto original = historyState(p);
    normalizeCell(p, 0, 2, 0);
    check(p.editHistory->undo.size() == 1, "stack normalize is one operation");
    const auto normalized = historyState(p);
    check(normalized != original, "normalize changes audio");
    auto normalizedAudio = p.sources[0][1].asset;
    historyStep(p);
    check(historyState(p) == original, "normalize undo exact full state");
    check(p.sources[0][0].asset == a && p.sources[0][1].asset == b, "undo shares original PCM");
    check(p.publishedStacks[0].load()->layers[1].asset == b.get(), "DSP stack restored");
    historyStep(p, true);
    check(historyState(p) == normalized && p.sources[0][1].asset == normalizedAudio, "redo exact PCM and settings");
    removeLayer(p, 0);
    check(stackCount(p, 0) == 1, "remove layer");
    historyStep(p); check(historyState(p) == normalized, "remove undo exact stack ordering");
    historyStep(p, true); check(stackCount(p, 0) == 1, "remove redo");
    historyStep(p);
    check(copyCell(p, 0), "copy for paste");
    historySource(p, 9, 0, historyAudio());
    const auto beforePaste = historyState(p);
    check(pasteCell(p, 9), "paste replacement");
    check(p.editHistory->redo.empty(), "new destructive branch clears redo");
    const auto pasted = historyState(p);
    historyStep(p); check(historyState(p) == beforePaste, "paste undo exact destination and unrelated cells");
    historyStep(p, true); check(historyState(p) == pasted, "paste redo exact");
}
void testCropLayer() {
    for (unsigned channels : {1u, 2u, 4u, 16u}) {
        auto instance = historyFixture(); auto& p = *instance;
        auto original = historyAudio(channels);
        historySource(p, 0, 0, original); historySource(p, 0, 1, original);
        historySource(p, 8, 0, original); // Shared PCM must not be modified.
        setParam(p, slotParamId(0, kSlotZeroCross), 0);
        setParam(p, slotParamId(0, kSlotStart), .25);
        setParam(p, slotParamId(0, kSlotEnd), .75);
        p.editPositions[0].store(.375); p.sourceBpms[0].store(123);
        storeSliceLayout(p, 0, s3g::sample::equalSampleNeonSliceLayout(4), false);
        p.textureOptions[0].store(kCutupsOption); p.fxParameters[0][2][3].store(.7f);
        p.selectedSlot.store(0);
        const auto before = historyState(p);
        workflow(p, WorkflowCommand::CropLayer);
        const auto cropped = p.sources[0][1].asset;
        check(cropped != original && cropped->frameCount() == 512 && cropped->channelCount == channels, "crop selected layer width/length");
        for (unsigned ch = 0; ch < channels; ++ch) for (unsigned n = 0; n < 512; ++n)
            check(cropped->channels[ch][n] == original->channels[ch][n + 256], "crop preserves exact linked-channel PCM");
        check(p.sources[0][0].asset == original && p.sources[8][0].asset == original && original->frameCount() == 1024,
            "crop leaves other layers/pads and original audio untouched");
        check(paramValue(p, slotParamId(0, kSlotStart)) == 0 && paramValue(p, slotParamId(0, kSlotEnd)) == 1
            && p.editPositions[0].load() == .375 && p.sources[0][1].slices.sliceCount == 4
            && p.sources[0][1].bpm == 123 && p.sources[0][1].path.empty(), "crop resets bounds and retains relative cursor/markers/BPM");
        check(p.publishedStacks[0].load()->layers[1].asset == cropped.get()
            && p.publishedAssets[0].load() == cropped.get(), "crop publishes render/edit sources");
        const auto after = historyState(p);
        workflow(p, WorkflowCommand::CropLayer);
        check(p.editHistory->undo.size() == 1 && historyState(p) == after, "full-length crop is a no-op");
        historyStep(p); check(historyState(p) == before && p.sources[0][1].asset == original, "crop Undo restores exact original selection/audio/settings");
        historyStep(p, true); check(historyState(p) == after && p.sources[0][1].asset == cropped, "crop Redo exact");
        historyStep(p);
        p.captureState.store(Plugin::CaptureState::Recording);
        workflow(p, WorkflowCommand::CropLayer);
        check(p.sources[0][1].asset == original && p.editHistory->redo.size() == 1, "recording blocks crop without losing Redo");
        p.captureState.store(Plugin::CaptureState::Empty);
        requestWorkflow(p, WorkflowCommand::CropLayer); // Capture L2, then change view before callback.
        showLayer(p, 0, 0); p.selectedSlot.store(8); serviceWorkflow(p);
        check(p.sources[0][1].asset->frameCount() == 512 && p.sources[0][0].asset == original
            && p.sources[8][0].asset == original && p.selectedLayers[0].load() == 0 && p.selectedSlot.load() == 8,
            "queued crop retains requested pad/layer without changing current selection");
    }
}

void testChop() {
    for (bool layers : {false, true}) {
        auto instance = historyFixture(); auto& p = *instance;
        historySource(p, 7, 0, historyAudio(4));
        p.selectedSlot.store(7);
        storeSliceLayout(p, 7, s3g::sample::equalSampleNeonSliceLayout(3), false);
        const auto before = historyState(p);
        mapChopToBanks(p, 7, true, 0, true, s3g::sample::kNeonChopAutoDestination, layers);
        check(p.editHistory && p.editHistory->undo.size() == 1, "assign all is one history step");
        const auto after = historyState(p);
        check(after != before, "slice assignment changed sources");
        historyStep(p); check(historyState(p) == before, "restore original after slice replacement");
        historyStep(p, true); check(historyState(p) == after, "redo slice assignment exact");
        historySource(p, 20, 0, historyAudio(4));
        const auto failedBefore = historyState(p);
        const auto sizeBefore = p.editHistory->undo.size();
        mapChopToBanks(p, 7, true, 0, false, 20, false);
        check(historyState(p) == failedBefore && p.editHistory->undo.size() == sizeBefore, "failed assignment leaves history and cells alone");
    }
}
void testCaptureAndReset() {
    auto instance = historyFixture(); auto& p = *instance;
    historySource(p, 31, 0, historyAudio(16));
    p.captureAsset = historyAudio(16); p.retainedAssets.push_back(p.captureAsset);
    p.publishedCapture.store(p.captureAsset.get()); p.captureState.store(Plugin::CaptureState::Review);
    p.captureLayout.store(6); p.captureStart.store(.25); p.captureEnd.store(.75);
    p.captureFrames.store(1024); p.captureTarget.store(0);
    const auto before = historyState(p); auto originalTake = p.captureAsset;
    cropCapture(p); const auto cropped = historyState(p);
    check(p.captureAsset->frameCount() == 512, "crop range");
    historyStep(p); check(historyState(p) == before && p.captureAsset == originalTake, "crop undo keeps discarded audio");
    check(!p.captureAudition.load(), "undo never automatically auditions");
    historyStep(p, true); check(historyState(p) == cropped, "crop redo exact");
    workflow(p, WorkflowCommand::DiscardCapture);
    check(!p.captureAsset, "discard take");
    historyStep(p); check(historyState(p) == cropped, "restore discarded take");
    workflow(p, WorkflowCommand::AssignCapture); const auto assigned = historyState(p);
    check(p.sources[0][0].asset == p.captureAsset, "assign shares take");
    historyStep(p); check(historyState(p) == cropped && p.captureAsset, "undo assign leaves review intact");
    historyStep(p, true); check(historyState(p) == assigned, "redo assign");
    setParam(p, kOutputLayoutParamId, 6); setParam(p, kBaseNoteParamId, 48);
    setParam(p, kMidiReceiveParamId, 7); setParam(p, kGlobalMangleParamId, .4);
    p.fillRepeat.store(3); p.familyControls[31][neonFamilyIndex(NeonFamily::CutRate)].store(13);
    const auto preReset = historyState(p); auto clipboard = p.cellClipboard;
    workflow(p, WorkflowCommand::ResetAll);
    check(!p.captureAsset && stackCount(p, 31) == 0, "reset clears all audio");
    check(paramValue(p, kOutputLayoutParamId) == 6 && paramValue(p, kMidiReceiveParamId) == 7, "reset preserves routing/MIDI");
    const auto reset = historyState(p);
    historyStep(p); check(historyState(p) == preReset, "reset undo restores full audio/settings");
    check(p.cellClipboard == clipboard, "history leaves clipboard intact");
    historyStep(p, true); check(historyState(p) == reset, "reset redo exact");
    historyStep(p);
    // A no-op and rejected destructive action must not consume redo/history.
    const auto redoCount = p.editHistory->redo.size();
    p.captureState.store(Plugin::CaptureState::Recording);
    check(!requestHistoryRestore(p, false), "undo blocked during record");
    workflow(p, WorkflowCommand::ResetAll);
    check(p.editHistory->redo.size() == redoCount && p.sources[31][0].asset, "recording blocks destructive reset");
    p.captureState.store(Plugin::CaptureState::Review);
}
void testLoadsAndLimit() {
    auto instance = historyFixture(); auto& p = *instance;
    auto oldAudio = historyAudio(), replacement = historyAudio(2, .5f);
    historySource(p, 0, 0, oldAudio);
    const auto before = historyState(p);
    queueSampleLoad(p, 0, "/not-read-by-this-test.wav");
    check(historyBusy(p), "pending load blocks undo");
    LoadResult result; result.slot = 0; result.layer = 0; result.generation = p.layerGenerations[0][0];
    result.asset = replacement; result.dirty = true; result.path = "/not-read-by-this-test.wav";
    p.loadResults.push_back(result); serviceLoads(p);
    check(p.sources[0][0].asset == replacement && !historyBusy(p), "async load commits");
    const auto loaded = historyState(p);
    historyStep(p); check(historyState(p) == before, "undo async replace");
    p.loadResults.push_back(result); serviceLoads(p);
    check(p.sources[0][0].asset == oldAudio, "stale completion cannot undo restore");
    historyStep(p, true); check(historyState(p) == loaded, "redo load does not reread disk");
    queueSampleLoad(p, 0, "/append.wav", true, 1);
    result.layer = 1; result.generation = p.layerGenerations[0][1]; result.path = "/append.wav";
    p.loadResults.push_back(result); serviceLoads(p);
    check(stackCount(p, 0) == 2, "append layer");
    historyStep(p); check(stackCount(p, 0) == 1 && p.sources[0][1].path.empty(), "append undo leaves no placeholder");
    queueSampleLoad(p, 0, "/missing.wav");
    const auto count = p.editHistory->undo.size(); result.layer = 0;
    result.generation = p.layerGenerations[0][0]; result.asset.reset(); result.error = "EXPECTED DECODE FAILURE";
    p.loadResults.push_back(result); serviceLoads(p);
    check(p.editHistory->undo.size() == count && p.sources[0][0].asset == replacement, "failed load keeps source/history");
    for (unsigned n = 0; n < 40; ++n) {
        NeonHistoryEdit edit(p, "BOUNDED EDIT", 1); check(bool(edit), "snapshot ready");
        p.grainSizes[0].store(10.f + n); edit.commit();
    }
    check(p.editHistory->undo.size() == kNeonHistoryLimit, "history bounded to 32 operations");
    check(historyFootprint(*p.editHistory, p.editHistory->undo) == 1024 * 2 * sizeof(float), "shared PCM counted once");
    historyStep(p);
    check(historyFootprint(*p.editHistory, p.editHistory->undo, 0, nullptr, &p.editHistory->redo)
        == 1024 * 2 * sizeof(float), "both history directions share one PCM budget");
    historyStep(p, true);
    for (unsigned n = 0; n < kNeonHistoryLimit; ++n) historyStep(p);
    check(!historyAvailable(p, false) && p.editHistory->redo.size() == kNeonHistoryLimit, "undo to bounded start");
    for (unsigned n = 0; n < kNeonHistoryLimit; ++n) historyStep(p, true);
    const auto saved = historyState(p);
    std::size_t position = 0;
    struct Input { const std::vector<uint8_t>* bytes; std::size_t* position; } input {&saved, &position};
    clap_istream_t stream {&input, [](const clap_istream_t* in, void* dst, uint64_t bytes) -> int64_t {
        auto& input = *static_cast<Input*>(in->ctx);
        auto size = std::min<std::size_t>(bytes, input.bytes->size() - *input.position);
        std::memcpy(dst, input.bytes->data() + *input.position, size); *input.position += size; return static_cast<int64_t>(size);
    }};
    check(stateLoad(&p.plugin, &stream) && !p.editHistory, "successful host recall clears session history");
}
void testRecordedTake() {
    auto instance = historyFixture(); auto& p = *instance;
    const auto before = historyState(p);
    check(p.recorder.prepare(48000), "recorder prepared"); p.recorder.start(2);
    std::array<float, 64> left, right; left.fill(.2f); right.fill(-.2f);
    std::array<float*, 2> samples {{left.data(), right.data()}};
    p.recorder.append(samples.data(), 64, 0); p.captureFrames.store(64);
    p.captureReady.store(1); p.captureState.store(Plugin::CaptureState::Ready); serviceWorkflow(p);
    check(historyLabel(p, false) == "RECORD TAKE" && p.captureAsset, "finalized take journaled");
    auto take = p.captureAsset; const auto after = historyState(p);
    historyStep(p); check(historyState(p) == before && !p.captureAsset, "undo recorded take");
    historyStep(p, true); check(historyState(p) == after && p.captureAsset == take, "redo recorded take exact");
    // An invalid host recall must not discard recovery history.
    clap_istream_t invalid {nullptr, [](const clap_istream_t*, void*, uint64_t) -> int64_t { return 0; }};
    check(!stateLoad(&p.plugin, &invalid) && historyAvailable(p, false), "failed recall keeps history");
}
void testAudioAcknowledgment() {
    auto instance = historyFixture(); auto& p = *instance;
    historySource(p, 0, 0, historyAudio()); normalizeCell(p, 0, 0, 0);
    check(pluginActivate(&p.plugin, 48000, 1, 64) && pluginStartProcessing(&p.plugin), "activate gated restore test");
    check(requestHistoryRestore(p, false), "request while processing");
    serviceWorkflow(p); check(p.resetAllPhase.load() == 1, "main thread waits for audio acknowledgment");
    std::array<std::array<float, 64>, 32> audio;
    std::array<float*, 32> channels;
    for (unsigned n = 0; n < 32; ++n) { audio[n].fill(1); channels[n] = audio[n].data(); }
    clap_audio_buffer_t output {}; output.data32 = channels.data(); output.channel_count = 32;
    clap_process_t process {}; process.frames_count = 64; process.audio_outputs_count = 1; process.audio_outputs = &output;
    check(pluginProcess(&p.plugin, &process) == CLAP_PROCESS_CONTINUE, "process pause");
    check(p.resetAllPhase.load() == 2, "audio acknowledged pause");
    for (const auto& channel : audio) for (float v : channel) check(v == 0, "all channels silent during restore");
    serviceWorkflow(p); check(p.resetAllPhase.load() == 0, "main thread publishes restored state");
    check(pluginProcess(&p.plugin, &process) == CLAP_PROCESS_CONTINUE, "processing resumes");
    pluginStopProcessing(&p.plugin); pluginDeactivate(&p.plugin);
    historyStep(p, true); check(bool(p.sources[0][0].asset), "redo survives deactivate/reactivate boundary");
}
#include "sample_neon_capture_checks.inc"
#include "sample_neon_clipboard_checks.inc"
void testPerformanceBanks() {
    auto instance=historyFixture(); auto& p=*instance;
    p.selectedSlot.store(5);
    storeSliceLayout(p,5,s3g::sample::equalSampleNeonSliceLayout(32),false);
    for (uint8_t page=0;page<4;++page) for (uint8_t layer=0;layer<2;++layer) {
        selectSurface(p,static_cast<NeonMode>(page),layer);
        for (uint8_t bank=0;bank<4;++bank) {
            selectSurfaceBank(p,bank); saveSurface(p);
            check(p.visibleMode.load()==page && p.visibleLayer.load()==layer && p.visibleBank.load()==bank,
                "Bank change retains selected performance mode and tool layer");
            check(p.selectedSlot.load()==5,"Editing bank change retains selected source cell");
            const auto frame=neonFeedbackFrame(p,p.surfaces[0]);
            check(static_cast<uint8_t>(frame.mode)==page && static_cast<uint8_t>(frame.layer)==layer && frame.bank==bank,
                "Feedback frame preserves mode across bank changes");
        }
    }
    selectSurface(p,NeonMode::HotCue,0); selectSurfaceBank(p,2); saveSurface(p);
    focusSurface(p,1); selectSurface(p,NeonMode::HotLoop,1); selectSurfaceBank(p,1); saveSurface(p);
    focusSurface(p,0);
    check(p.visibleMode.load()==2 && p.visibleLayer.load()==0 && p.visibleBank.load()==2,
        "Unit one retains STACK bank C while unit two uses RESAMPLE bank B");
    focusSurface(p,1);
    check(p.visibleMode.load()==3 && p.visibleLayer.load()==1 && p.visibleBank.load()==1,
        "Unit two retains independent mode, layer and bank");
}
void testRawBankModeInvariant() {
    auto instance=historyFixture();auto& p=*instance;
    setParam(p,kNeonActiveParamId,1);
    storeSliceLayout(p,0,s3g::sample::equalSampleNeonSliceLayout(32),false);
    struct Input {
        std::array<clap_event_midi_t,6> events {};
        clap_input_events_t list {this,
            [](const clap_input_events_t*)->uint32_t{return 6;},
            [](const clap_input_events_t* in,uint32_t n)->const clap_event_header_t* {
                return n<6 ? &static_cast<const Input*>(in->ctx)->events[n].header : nullptr;
            }};
        void put(unsigned n,uint32_t time,NeonMidi m) {
            auto& e=events[n];e.header={sizeof(e),time,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};
            e.data[0]=m.status;e.data[1]=m.data1;e.data[2]=m.data2;
        }
    } in;
    namespace neon=s3g::controller::reloop_neon;
    for(unsigned page=0;page<8;++page) for(uint8_t bank=0;bank<4;++bank) for(unsigned stale=0;stale<8;++stale) {
        p.selectedSlot.store(0);
        selectSurface(p,static_cast<NeonMode>(page%4),page/4);
        const auto recall=neon::modeLedMessage(bank,static_cast<NeonMode>(stale%4),static_cast<neon::Layer>(stale/4));
        auto pad=neon::padSurfaceMessage(bank,static_cast<NeonMode>(stale%4),static_cast<neon::Layer>(stale/4),7,101);
        in.put(0,0,{static_cast<uint8_t>(0x93+bank),1,127});in.put(1,0,recall);
        in.put(2,0,{recall.status,recall.data1,0});in.put(3,1,pad);
        in.put(4,2,{static_cast<uint8_t>(0xa0|(pad.status&15)),pad.data1,47});
        in.put(5,3,{static_cast<uint8_t>(pad.status-0x10),pad.data1,0});
        collectEvents(p,&in.list,nullptr,64);
        check(p.visibleMode.load()==page%4 && p.visibleLayer.load()==page/4 && p.visibleBank.load()==bank,
            "raw bank recall, stale pad/pressure/release never change chosen page, layer or bank");
    }
    // A GUI page selection must also override an old hardware pad address.
    selectSurface(p,NeonMode::Slicer,0);selectSurfaceBank(p,1);
    in.put(0,0,{0x98,0x10,127});in.put(1,1,{0x88,0x10,0});
    for(unsigned n=2;n<6;++n) in.put(n,n,{0xfe,0,0});
    collectEvents(p,&in.list,nullptr,64);
    check(p.visibleMode.load()==1 && p.visibleBank.load()==1 && p.visibleSlice.load()==8,
        "on-screen CHOP stays authoritative and stale bank-B HOT CUE pad auditions slice 9");
}
}
#include "sample_neon_note_routing_checks.inc"
#include "sample_neon_generated_media_checks.inc"
int main() {
    setenv("S3G_SAMPLE_NEON_DISABLE_DIRECT_MIDI", "1", 1);
    try {
        testNormalizeRemovePaste(); testChop(); testCaptureAndReset(); testLoadsAndLimit(); testRecordedTake(); testAudioAcknowledgment();
        testLiveCapture(); testRetainedCaptureReview(); testCropLayer(); testLayerClipboard(); testPerformanceBanks(); testRawBankModeInvariant();
        testPinnedNoteRouting();
        testGeneratedAuditionMedia();
        std::cout << "Neon destructive history: " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "History failure after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
