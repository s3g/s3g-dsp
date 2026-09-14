#include "s3g/tracker/clap_editor_coordinator.h"
#include "s3g/tracker/project_codec.h"
#include <iostream>
#include <memory>

using namespace s3g::tracker;
namespace {
int checks=0, failures=0;
void check(bool value, const char* label) {
    ++checks;
    if(!value){++failures;std::cerr<<label<<'\n';}
}
std::string encoded(const ProjectDocument& d) {
    std::string json;
    check(encodeProjectDocument(d,json).ok(),"encode valid coordinator document");
    return json;
}
struct Harness {
    midi::Engine engine;
    editor::SongEditor song;
    std::unique_ptr<ClapEditorCoordinator> ui;
    unsigned dirty=0, requests=0, refreshes=0, displays=0, toggles=0;
    bool playing=false;
    double bpm=120;
    std::optional<std::string> name;
    editor::ShellPage page=editor::ShellPage::Tracker;
    Harness() {
        engine.services.context=this;
        engine.services.markDirty=[](const void* p) noexcept {++((Harness*)p)->dirty;};
        engine.services.requestProcess=[](const void* p) noexcept {++((Harness*)p)->requests;};
        check(midi::initialize(engine),"initialize coordinator engine");
        ClapEditorServices s;
        s.refresh=[this]{++refreshes;};
        s.refreshPlayback=[this]{++displays;};
        s.hostPlaying=[this]{return std::optional<bool>(playing);};
        s.hostTempo=[this]{return std::optional<double>(bpm);};
        s.hostToggle=[this]{++toggles;return true;};
        s.showPage=[this](auto p){page=p;};
        s.promptText=[this](const auto&,const auto&){return name;};
        ui=std::make_unique<ClapEditorCoordinator>(engine,song,std::move(s));
    }
};
void bindingsAndEdits() {
    auto h=std::make_unique<Harness>();
    auto& ui=*h->ui; auto& state=ui.state();auto& cb=ui.callbacks();
    auto canonical=h->engine.document;
    // The original Song editor assigns identities to legacy zero-ID rows.
    for(std::size_t i=0;i<canonical.song.rows.size();++i)canonical.song.rows[i].id=uint32_t(i+1);
    check(encoded(ui.currentDocument())==encoded(canonical),
        "opening editor preserves initial state apart from native legacy row-ID initialization");
    check(bool(cb.togglePlayback),"native callback retained: togglePlayback");
    check(bool(cb.restartPlayback),"native callback retained: restartPlayback");
    check(bool(cb.resyncTrack),"native callback retained: resyncTrack");
    check(bool(cb.panic),"native callback retained: panic");
    check(bool(cb.previewBurst),"native callback retained: previewBurst");
    check(bool(cb.previewPitchSequence),"native callback retained: previewPitchSequence");
    check(bool(cb.startAuthoringPreview),"native callback retained: startAuthoringPreview");
    check(bool(cb.stopAuthoringPreview),"native callback retained: stopAuthoringPreview");
    check(bool(cb.authoringPreviewPosition),"native callback retained: authoringPreviewPosition");
    check(bool(cb.loopAuthoringPreview),"native callback retained: loopAuthoringPreview");
    check(bool(cb.showSongWindow),"native callback retained: showSongWindow");
    check(bool(cb.showGeometryPage),"native callback retained: showGeometryPage");
    check(bool(cb.showBurstPage),"native callback retained: showBurstPage");
    check(bool(cb.showReshapePage),"native callback retained: showReshapePage");
    check(bool(cb.showPhrasePage),"native callback retained: showPhrasePage");
    check(bool(cb.showAssemblePage),"native callback retained: showAssemblePage");
    check(bool(cb.importAssetPack),"native callback retained: importAssetPack");
    check(bool(cb.exportBurstAssetPack),"native callback retained: exportBurstAssetPack");
    check(bool(cb.exportBurstLibraryAssetPack),"native callback retained: exportBurstLibraryAssetPack");
    check(bool(cb.exportPhraseAssetPack),"native callback retained: exportPhraseAssetPack");
    check(bool(cb.exportPhraseLibraryAssetPack),"native callback retained: exportPhraseLibraryAssetPack");
    check(bool(cb.copyBurstToProject),"native callback retained: copyBurstToProject");
    check(bool(cb.copyPhraseToProject),"native callback retained: copyPhraseToProject");
    check(bool(cb.selectBurstBank),"native callback retained: selectBurstBank");
    check(bool(cb.selectPhraseBank),"native callback retained: selectPhraseBank");
    check(bool(cb.clearPhraseBank),"native callback retained: clearPhraseBank");
    check(bool(cb.deletePhraseBank),"native callback retained: deletePhraseBank");
    check(bool(cb.deleteUnusedBursts),"native callback retained: deleteUnusedBursts");
    check(bool(cb.deleteBurstBank),"native callback retained: deleteBurstBank");
    check(bool(cb.showTrackerPage),"native callback retained: showTrackerPage");
    check(bool(cb.showWarpPage),"native callback retained: showWarpPage");
    check(bool(cb.previewPattern),"native callback retained: previewPattern");
    check(bool(cb.clearPatternPreview),"native callback retained: clearPatternPreview");
    check(bool(cb.createPatternVariant),"native callback retained: createPatternVariant");
    check(bool(cb.showConsoleHelp),"native callback retained: showConsoleHelp");
    check(bool(cb.instrumentRackChanged),"native callback retained: instrumentRackChanged");
    check(bool(cb.instrumentRackReloaded),"native callback retained: instrumentRackReloaded");
    check(bool(cb.reportError),"native callback retained: reportError");
    check(bool(cb.selectionChanged),"native callback retained: selectionChanged");
    check(bool(cb.patternChanged),"native callback retained: patternChanged");
    check(bool(cb.selectPattern),"native callback retained: selectPattern");
    check(bool(cb.addPattern),"native callback retained: addPattern");
    check(bool(cb.renamePattern),"native callback retained: renamePattern");
    check(bool(cb.deletePattern),"native callback retained: deletePattern");
    check(bool(cb.transportChanged),"native callback retained: transportChanged");
    check(bool(cb.fillChanged),"native callback retained: fillChanged");
    check(bool(cb.outputChanged),"native callback retained: outputChanged");
    check(bool(cb.mainOutputGainChanged),"native callback retained: mainOutputGainChanged");
    check(bool(cb.viewPreferencesChanged),"native callback retained: viewPreferencesChanged");
    check(bool(cb.midiStepRecordModeChanged),"native callback retained: midiStepRecordModeChanged");
    check(bool(cb.midiRecordTrackChanged),"native callback retained: midiRecordTrackChanged");
    check(bool(cb.tracksReordered),"native callback retained: tracksReordered");
    check(bool(cb.executeCommand),"native callback retained: executeCommand");
    check(!cb.showInstrumentWindow&&!cb.auditionInstrument,"MIDI-only CLAP does not invent retired audio/rack UI");
    cb.togglePlayback();check(h->toggles==1,"transport uses host service");
    cb.showSongWindow();check(h->page==editor::ShellPage::Song,"Song navigation");
    cb.showGeometryPage();check(h->page==editor::ShellPage::Geometry,"Geometry navigation");
    cb.showBurstPage();check(h->page==editor::ShellPage::Bursts,"Bursts navigation");
    cb.showPhrasePage();check(h->page==editor::ShellPage::Phrases,"Phrases navigation");
    cb.showAssemblePage();check(h->page==editor::ShellPage::Assemble,"Assemble navigation");
    cb.showReshapePage();check(h->page==editor::ShellPage::Reshape,"Reshape navigation");
    cb.showWarpPage();check(h->page==editor::ShellPage::Warps,"Warps navigation");
    cb.executeCommand("help");check(h->page==editor::ShellPage::Help,"console help navigation");
    cb.showTrackerPage();check(h->page==editor::ShellPage::Tracker,"Tracker navigation");
    const auto original=state.patternBank.activePatternId;
    const auto count=state.patternBank.entries.size();
    cb.addPattern(true);
    const auto copy=state.patternBank.activePatternId;
    check(copy!=original&&state.patternBank.entries.size()==count+1,"duplicate selects independent pattern");
    h->name="  Pattern 雨 café  ";cb.renamePattern();
    check(state.session.pattern.name=="Pattern 雨 café","rename preserves Unicode and trims surrounding spaces");
    const auto renamed=encoded(ui.currentDocument());
    cb.executeCommand("undo");
    check(state.session.pattern.name!="Pattern 雨 café","undo rename");
    cb.executeCommand("redo");
    check(encoded(ui.currentDocument())==renamed,"redo exact project");
    cb.selectPattern(original);
    check(state.patternBank.activePatternId==original,"select original");
    cb.selectPattern(copy);cb.deletePattern();
    check(state.patternBank.entries.size()==count,"delete unused copy");
    cb.deletePattern();
    check(!state.patternBank.entries.empty(),"cannot delete last pattern");
    cb.addPattern(false);
    check(state.session.pattern.visibleRows==64&&state.session.pattern.tracks.front().notes.size()==64,
        "new blank pattern retains native 64-row policy");
    ui.flushRuntimePublication();
    check(h->engine.pendingRuntime.load()!=nullptr,"coalesced edits publish playback runtime");
    const auto* beforeFollowRuntime = h->engine.pendingRuntime.load();
    const auto beforeFollowDirty = h->dirty;
    state.trackerFollow = { TrackerFollowMode::Center, false, 2 };
    cb.viewPreferencesChanged();
    ui.flushRuntimePublication();
    check(ui.currentDocument().session.trackerFollow == state.trackerFollow
        && h->engine.document.session.trackerFollow == state.trackerFollow,
        "follow preferences reach both project and host state");
    check(h->dirty > beforeFollowDirty && h->engine.pendingRuntime.load() == beforeFollowRuntime,
        "follow preferences dirty the host without replacing the playback runtime");
    h->name="";cb.renamePattern();
    check(h->dirty>0&&h->requests>0,"edits notify dirty host and processing");

    auto recalled=ui.currentDocument();
    auto& entry=recalled.patternBank.entries.back();
    entry.pattern.tracks.resize(3);
    recalled.patternBank.activePatternId=entry.id;
    recalled.song.rows.clear();
    SongRow row;row.id=123;row.patternId=entry.id;row.mutedTracks=4;
    recalled.song.rows.push_back(row);
    ui.cancelRuntimePublication();ui.applyDocument(recalled);ui.resetHistory(recalled);
    check(h->song.snapshot().rows.front().mutedTracks==4,
        "state recall loads pattern catalog before Song lane masks");
    check(state.midiStepInputAvailable&&!state.audioAvailable,"MIDI-only editor capabilities");
    cb.midiRecordTrackChanged(2);
    state.session.pattern.tracks[2].midiChannel=9;cb.outputChanged();
    check(h->engine.midiRecordTrack==2&&h->engine.midiMonitorChannel==8,"record lane and channel mapping");
    cb.midiStepRecordModeChanged(MidiStepRecordMode::Step);
    MidiStepCapture capture;capture.mode=MidiStepRecordMode::Step;
    capture.targetTrack=2;capture.note=73;capture.velocity=100;
    state.session.selectedRow=0;
    check(h->engine.midiStepCaptures.push(capture),"queue input capture");
    ui.consumeMidiStepCaptures();
    check(state.session.pattern.tracks[2].notes[0].note==73,"audio capture reaches selected editor lane");
    cb.fillChanged(true);check(h->engine.fillActive,"fill mailbox");
    cb.resyncTrack(2);check(h->engine.requestTrackResyncMask==4,"lane sync mailbox");
    cb.restartPlayback();check(h->engine.requestRestart,"restart mailbox");
    cb.panic();check(h->engine.requestPanic,"panic mailbox");
    auto model=ui.console;
    h->ui.reset();
    check(h->engine.midiStepRecordMode==uint8_t(MidiStepRecordMode::Off)
        &&h->engine.requestMidiMonitorRelease,"close disarms and releases monitored notes");
    check(!h->song.callbacks.changed&&!model->execute,"close clears borrowed model callbacks");
}
void displayAndPreview() {
    auto h=std::make_unique<Harness>();
    auto& ui=*h->ui;auto& cb=ui.callbacks();auto& state=ui.state();
    h->bpm=137.5;h->engine.visualHostTempo=91;
    ui.pollDisplay();check(state.hostBpm==137.5,"stopped host tempo query");
    h->playing=true;h->engine.visualPlaying=true;
    h->engine.notePlayheads[0]=4;h->engine.visualNoteHits[0].publish(4,12345);
    ui.pollDisplay();
    check(!state.noteHits[0]&&state.hostBpm==91,"first display primes; playing tempo comes from audio");
    h->engine.notePlayheads[0]=5;h->engine.visualNoteHits[0].publish(5,23456);
    ui.pollDisplay();
    check(state.notePlayheads[0]==4&&state.noteHits[0]&&state.noteHitRows[0]==4
        &&state.noteHitSampleTimes[0]==12345,"retains native one-frame visual delay and audio timestamp");
    ui.pollDisplay();
    check(state.notePlayheads[0]==5&&state.noteHitSampleTimes[0]==23456,"next audio snapshot displayed");
    ui.pollDisplay();check(!state.noteHits[0],"visual hit consumed only once");
    const std::vector<PitchPreviewEvent> events{{0,60,100,50,0},{2,64,100,50,0}};
    check(cb.startAuthoringPreview(events,1,120,4,4,true)==0,"preview blocked during host playback");
    h->playing=false;ui.pollDisplay();
    check(!state.noteHits[0]&&state.subrowPlaybackPhase==0,"stop clears transient display");
    const auto token=cb.startAuthoringPreview(events,3,80,4,4,true);
    check(token!=0,"start looped authoring preview while stopped");
    check(midi::activate(h->engine,48000),"activate preview engine");
    // activate resets preview plans: publish only after activation.
    const auto active=cb.startAuthoringPreview(events,3,80,4,4,true);
    struct Notes {midi::Engine* engine;std::vector<uint64_t> onsets;};
    Notes notes{&h->engine,{}};
    midi::MidiOutput output{&notes,[](const void* p,uint32_t frame,uint8_t status,uint8_t,uint8_t) noexcept {
        auto& n=*const_cast<Notes*>(static_cast<const Notes*>(p));
        if((status&0xf0)==0x90)n.onsets.push_back(n.engine->processFrame+frame);
        return true;
    }};
    h->bpm=120;
    // Do not call pollDisplay at all: missed graphics frames cannot delay loops.
    for(unsigned i=0;i<200;++i) {
        midi::ProcessData data;data.frames_count=512;data.transport={false,true,true,0,120,0};
        data.out_events=&output;midi::process(h->engine,data);
    }
    check(notes.onsets.size()>=8,"preview continues without GUI polling");
    for(std::size_t i=1;i<notes.onsets.size();++i)
        check(notes.onsets[i]-notes.onsets[i-1]==12000,"host BPM preview loop has no GUI-clock gap");
    check(cb.authoringPreviewPosition(active)>=0,"preview position is audio-owned");
    cb.loopAuthoringPreview(active,false);cb.stopAuthoringPreview(active);
    ui.stop();midi::deactivate(h->engine);
}
void songPublication() {
    auto h=std::make_unique<Harness>();auto& ui=*h->ui;
    h->song.add();h->song.add();h->song.toggleMode();ui.flushRuntimePublication();
    h->playing=true;h->engine.visualSongRow=0;ui.pollDisplay();
    h->song.selected=1;h->song.duplicate();
    check(h->engine.songArrangementUpdatePending&&h->engine.queuedVariationRuntime.load(),
        "playing Song edit queues immutable next-row handover");
    check(h->engine.songLaunchQuantization==uint32_t(SongLaunchQuantization::NextSongRow),
        "Song edit retains next-row quantization");
    const auto revision=h->engine.songLoopRevision.load();
    h->song.toggleLoop();
    check(h->engine.songLoopRevision==revision+1,"loop changes use realtime mailbox");
    // Final non-looping row: persist now, prepare runtime only after host stop.
    h->song.arrangement.loop=false;
    h->engine.visualSongRow=int32_t(h->song.arrangement.rows.size()-1);
    h->engine.visualPendingSongRow=-1;ui.pollDisplay();ui.pollDisplay();
    ui.commitSongProjectEdit(true);
    check(ui.state().status.find("no future row")!=std::string::npos,"final row edits defer runtime swap");
    h->playing=false;ui.pollDisplay();
    check(h->engine.pendingRuntime.load()!=nullptr,"deferred final-row edit publishes after host stop");
}
}
int main() {
    bindingsAndEdits();displayAndPreview();songPublication();
    std::cout<<checks<<" coordinator checks; "<<failures<<" failures\n";
    return failures?1:0;
}
