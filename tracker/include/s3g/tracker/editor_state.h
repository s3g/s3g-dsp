#pragma once

#include "s3g/tracker/command.h"
#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/midi_step_recorder.h"
#include "s3g/tracker/pattern_bank.h"
#include "s3g/tracker/phrase_library.h"
#include "s3g/tracker/pitch_map.h"
#include "s3g/tracker/project_document.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace s3g::tracker::app {

constexpr std::size_t kVisibleLaneCount = kMaximumTrackCount;

struct TrackerViewState {
    TrackerSession session;
    // TrackerSession remains the command engine's mutable active-pattern
    // workspace. Pattern changes are synchronized to this ordered bank by the
    // app coordinator before selection, persistence, and playback publication.
    PatternBank patternBank = makeDefaultPatternBank();
    PhraseLibrary phraseLibrary;
    std::vector<BurstBank> burstBanks { makeProjectBurstBank() };
    std::vector<PhraseBank> phraseBanks { makeProjectPhraseBank() };
    AssetBankId activeBurstBankId = kProjectAssetBankId;
    AssetBankId activePhraseBankId = kProjectAssetBankId;
    std::size_t selectedPhrase = 0u;
    std::size_t lastPlacedPhrase = 0u;
    // Audition controls are transient workspace preferences. Phrase routing
    // remains stored with each Phrase; Burst routing has no effect on placed
    // cells, which continue to use the destination lane's MIDI channel.
    uint8_t burstPreviewMidiChannel = 1u;
    bool burstLoopPreview = false;
    bool phraseLoopPreview = false;
    PhraseAssemblyDraft assembly;
    InstrumentRackState instrumentRack = makeDefaultInstrumentRack();
    uint32_t selectedRackInstrument = 0u;
    std::array<std::size_t, kVisibleLaneCount> notePlayheads {};
    std::array<bool, kVisibleLaneCount> noteHits {};
    std::array<std::size_t, kVisibleLaneCount> noteHitRows {};
    std::array<uint64_t, kVisibleLaneCount> noteHitSampleTimes {};
    // Normalized position inside the currently sounding Tracker row. Burst
    // uses this shared clock for its matrix, radial, and breakpoint cursors.
    float subrowPlaybackPhase = 0.0f;
    std::array<std::size_t, kVisibleLaneCount> instrumentPlayheads {};
    std::array<std::size_t, kVisibleLaneCount> velocityPlayheads {};
    std::array<std::array<std::size_t, kFxPairCount>, kVisibleLaneCount>
        fxActionPlayheads {};
    std::array<std::array<std::size_t, kFxPairCount>, kVisibleLaneCount>
        fxValuePlayheads {};
    // View-only timing-warp playback state. The GUI receives the last audible
    // logical tick plus the exact curve currently owned by Pattern or Song
    // playback, so the Warps diagram can follow the sounding sequence without
    // reading mutable scheduler state from the audio thread.
    uint64_t timingWarpPlaybackTick = 0u;
    uint32_t timingWarpPlaybackCycleTicks = 1u;
    TimingWarpStack timingWarpPlaybackStack;
    bool timingWarpPlaybackActive = false;
    bool timingWarpPlaybackFromSong = false;
    bool playing = false;
    bool paused = false;
    // Transient performance gate used by SEQ CD FILL / !FILL. It is not
    // serialized into the project document.
    bool fillActive = false;
    bool songPlaybackEnabled = false;
    bool songPlaybackActive = false;
    std::size_t songPlaybackRow = 0u;
    bool songPlaybackRowValid = false;
    // View-only identity of the pattern currently sounding through Song mode.
    // The audio scheduler publishes the row; the coordinator resolves that
    // row through the arrangement so Geometry can follow it without touching
    // real-time state or changing the editor's selected pattern.
    std::string songPlaybackPatternId;
    // Per-lane mute mask belonging to that active Song row. Geometry combines
    // it with the pattern's NOTE-column mutes; the scheduler applies the same
    // zero-based bit positions during playback.
    uint32_t songPlaybackMutedTracks = 0u;
    bool audioAvailable = false;
    bool audioProcessing = false;
    double audioSampleRate = 0.0;
    float audioPeak = 0.0f;
    // Mixer page state. Track strips store their event trim in Track; MAIN
    // OUT is a real internal-audio post-decode fader and does not affect MIDI.
    float mainOutputGain = 1.0f;
    bool mainOutputMuted = false;
    bool mixerPageVisible = false;
    // View-only tracker density. Compact lanes expose NOTE and VOL; expanded
    // lanes additionally expose both sequencing action/value pairs.
    bool sequenceColumnsExpanded = false;
    // NOTE formatting. Pattern storage remains MIDI 0..127 in both modes;
    // false renders pitch names and true renders decimal MIDI values. The
    // coordinator persists this project-scoped presentation preference.
    bool showMidiNoteValues = true;
    // Vertical Tracker navigation increment. The View toolbox exposes 1..16;
    // Up/Down use this many rows while retaining boundary clamping.
    uint32_t trackerRowJump = 1u;
    TrackerFollowSettings trackerFollow;
    // Loading a document (including undo/redo) resets transient manual hold.
    uint64_t trackerFollowRevision = 0u;
    // UI-only Live Code / VIEW control bridge. Neither belongs in project
    // files or the audio runtime. Resume requests are consumed outside text
    // callbacks, so a command never destroys its own active text editor.
    double trackerGridZoom = 1.0;
    uint64_t trackerFollowResumeRevision = 0u;
    // MIDI recording is deliberately transient host/UI state. It is OFF when
    // an editor is created and is not embedded in project files.
    bool midiStepInputAvailable = false;
    MidiStepRecordMode midiStepRecordMode = MidiStepRecordMode::Off;
    // Transient, explicitly armed MIDI-record destination. This is independent
    // of the editing cursor and is deliberately not saved with the project.
    std::size_t midiRecordTrack = 0u;
    // Project-level history availability is published by the coordinator;
    // view controls do not own or mutate snapshots directly.
    bool canUndo = false;
    bool canRedo = false;
    std::size_t mixerSelectedStrip = 0u;
    bool mixerSoloActive = false;
    std::size_t mixerSoloTrack = kVisibleLaneCount;
    std::vector<bool> mixerSoloRestoreMutes;
    uint64_t audioCallbackCount = 0u;
    uint64_t audioLateEventCount = 0u;
    uint64_t audioDroppedEventCount = 0u;
    uint64_t audioClockFaultCount = 0u;
    uint64_t audioRenderErrorCount = 0u;
    std::string midiRoute = "1 MIDI DEVICE";
    std::string audioOutputDevice = "DEFAULT OUTPUT";
    // The embedded CLAP build displays the host clock and applies this
    // persisted musical ratio to the tracker scheduler.
    double hostBpm = 0.0;
    double tempoScale = 1.0;
    std::string status = "MIDI ready";
    std::string lastEvent = "No MIDI sent yet";
    uint64_t sentEventCount = 0u;
    uint64_t droppedEventCount = 0u;
};

struct WorkspaceCallbacks {
    std::function<void()> togglePlayback;
    std::function<void()> restartPlayback;
    std::function<void(std::size_t)> resyncTrack;
    std::function<void()> panic;
    std::function<void()> showSongWindow;
    // CLAP page navigation; the host shell also owns detachable tool windows.
    std::function<void()> showGeometryPage;
    std::function<void()> showBurstPage;
    std::function<void()> showReshapePage;
    std::function<void()> showPhrasePage;
    std::function<void()> showAssemblePage;
    std::function<void()> importAssetPack;
    std::function<void(std::size_t)> exportBurstAssetPack;
    std::function<void()> exportBurstLibraryAssetPack;
    std::function<void(std::size_t)> exportPhraseAssetPack;
    std::function<void()> exportPhraseLibraryAssetPack;
    std::function<std::size_t(std::size_t)> copyBurstToProject;
    std::function<std::size_t(std::size_t)> copyPhraseToProject;
    std::function<void(AssetBankId)> selectBurstBank;
    std::function<void(AssetBankId)> selectPhraseBank;
    std::function<void()> clearPhraseBank;
    std::function<void()> deletePhraseBank;
    std::function<void()> deleteUnusedBursts;
    std::function<void()> deleteBurstBank;
    std::function<void()> showTrackerPage;
    std::function<void()> showWarpPage;
    std::function<void()> showInstrumentWindow;
    std::function<void(uint32_t)> editRackInstrument;
    std::function<void()> showConsoleHelp;
    std::function<void(uint32_t, uint32_t, float)>
        instrumentParameterChanged;
    std::function<void(uint32_t, uint8_t, float)> auditionInstrument;
    // Audition one reusable Burst while host transport is stopped. The lane
    // channel and project clock are captured with the immutable definition so
    // the audio thread never reaches back into the editor model.
    std::function<void(const BurstDefinition&, uint8_t, double, uint32_t)>
        previewBurst;
    // Audition the non-destructive Pitch Map proposal while transport is
    // stopped. Events are published as an immutable audio-thread plan.
    std::function<void(const std::vector<PitchPreviewEvent>&, uint8_t,
        double, uint32_t)> previewPitchSequence;
    // Full-length audio-clock audition. Token ownership prevents a hidden page
    // from stopping another page's preview; position reads never schedule MIDI.
    std::function<uint32_t(const std::vector<PitchPreviewEvent>&, uint8_t,
        double, uint32_t, uint32_t, bool)> startAuthoringPreview;
    std::function<void(uint32_t)> stopAuthoringPreview;
    std::function<void(uint32_t, bool)> loopAuthoringPreview;
    std::function<int64_t(uint32_t)> authoringPreviewPosition;
    // Reshape audition swaps only the immutable audio-thread runtime. It does
    // not touch the stored document until the page commits with patternChanged.
    std::function<void(const Pattern&)> previewPattern;
    std::function<void()> clearPatternPreview;
    // Stores and selects an immutable Reshape result as a new pattern-bank
    // entry, preserving the source pattern.
    std::function<void(const Pattern&)> createPatternVariant;
    std::function<void(uint32_t)> resetInstrumentPatch;
    std::function<void()> instrumentRackChanged;
    // Republishes derived runtime assets (for example decoded sample PCM)
    // without marking the native project document as edited.
    std::function<void()> instrumentRackReloaded;
    std::function<void(const std::string&)> reportError;
    std::function<void()> selectionChanged;
    std::function<void()> patternChanged;
    std::function<void(const std::string&)> selectPattern;
    std::function<void(bool)> addPattern;
    std::function<void()> renamePattern;
    std::function<void()> deletePattern;
    std::function<void()> transportChanged;
    std::function<void(bool)> fillChanged;
    std::function<void()> outputChanged;
    std::function<void(float)> mainOutputGainChanged;
    std::function<void()> viewPreferencesChanged;
    std::function<void(MidiStepRecordMode)> midiStepRecordModeChanged;
    std::function<void(std::size_t)> midiRecordTrackChanged;
    // Called after a complete lane moves so Song rows can remap their mute
    // bit positions before the project snapshot is committed.
    std::function<void(const std::string&, std::size_t, std::size_t)>
        tracksReordered;
    std::function<void(const std::string&)> executeCommand;
};

} // namespace s3g::tracker::app
