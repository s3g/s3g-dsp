#pragma once
#include "s3g/tracker/clap_midi_engine.h"
#include "s3g/tracker/asset_pack.h"
#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/editor_song.h"
#include "s3g/tracker/editor_shell.h"
#include "s3g/tracker/editor_reference.h"
#include <chrono>

namespace s3g::tracker {
struct ClapEditorServices {
    std::function<void()> refresh = [] {}, refreshPlayback = [] {}, focusTracker = [] {};
    std::function<void(editor::ShellPage)> showPage = [](auto) {};
    std::function<std::optional<bool>()> hostPlaying = [] { return std::optional<bool>{}; };
    std::function<std::optional<double>()> hostTempo = [] { return std::optional<double>{}; };
    std::function<bool()> hostToggle = [] { return false; },
        hostContinue = [] { return false; }, hostStop = [] { return false; };
    std::function<std::string(bool, const std::string&, const std::string&,
        const std::string&)> fileDialog = [](auto, const auto&, const auto&, const auto&) {
            return std::string{};
        };
    std::function<std::optional<std::string>(const std::string&, const std::string&)>
        promptText = [](const auto&, const auto&) { return std::optional<std::string>{}; };
};

struct VisualPlaybackFrame {
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        notePlayheads {};
    std::array<bool, s3g::tracker::kMaximumTrackCount> noteHits {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount> noteHitRows {};
    std::array<uint64_t, s3g::tracker::kMaximumTrackCount>
        noteHitSampleTimes {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        instrumentPlayheads {};
    std::array<std::size_t, s3g::tracker::kMaximumTrackCount>
        velocityPlayheads {};
    std::array<std::array<std::size_t, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxActionPlayheads {};
    std::array<std::array<std::size_t, s3g::tracker::kFxPairCount>,
        s3g::tracker::kMaximumTrackCount> fxValuePlayheads {};
    float subrowPhase = 0.0f;
    uint64_t timingWarpTick = 0u;
    int32_t songRow = -1;
    int32_t pendingSongRow = -1;
    uint32_t pendingSongQuantization = 0u;
};

// UI-thread coordinator extracted from the native CLAP. Native services only
// own windows, focus, dialogs and host requests; musical/edit policy stays here.
class ClapEditorCoordinator {
public:
    ClapEditorCoordinator(midi::Engine&, editor::SongEditor&, ClapEditorServices);
    ~ClapEditorCoordinator();
    app::TrackerViewState& state() { return *_state; }
    app::WorkspaceCallbacks& callbacks() { return *_callbacks; }
    std::shared_ptr<editor::ConsoleModel> console = std::make_shared<editor::ConsoleModel>();
    ProjectDocument currentDocument();
    void applyDocument(const ProjectDocument&);
    void updateHistoryAvailability();
    void resetHistory(const ProjectDocument&);
    void recordHistory(const ProjectDocument&);
    void undoProject();
    void redoProject();
    void consumeMidiStepCaptures();
    void scheduleRuntimePublication();
    void cancelRuntimePublication();
    void flushRuntimePublication();
    void disarmMidiStepRecording();
    void updateMidiMonitorChannel();
    void commitProject(bool dirty);
    void commitProjectWithoutRuntime(bool dirty);
    void commitSongProjectEdit(bool dirty);
    void refreshSongPatterns();
    void refreshSongWarps();
    void presentSaveSongProject();
    void presentLoadSongProject();
    void presentImportAssetPack();
    void presentExportAssetPack(const TrackerAssetPack&);
    bool installPatternVariation(const PatternVariationRequest&);
    void selectPattern(const std::string&);
    void addPattern(bool duplicate);
    void renamePattern();
    void deletePattern();
    void executeCommand(const std::string&);
    void pollDisplay();
    void message(const std::string&, bool error = false);
    void stop();
private:
    midi::Engine* _plugin;
    editor::SongEditor& song;
    ClapEditorServices platform;
    std::unique_ptr<app::TrackerViewState> _state;
    std::unique_ptr<app::WorkspaceCallbacks> _callbacks;
    std::unique_ptr<ClapDocumentController> _documentController;
    std::array<uint64_t, kMaximumTrackCount> _consumedNoteHitSequences {};
    VisualPlaybackFrame _pendingVisualFrame;
    bool _visualFramePrimed = false;
    uint64_t _reportedStepRecordDrops = 0;
    MidiLiveRecordState _midiLiveRecordState;
    bool _runtimePublicationPending = false;
    bool _deferredSongRuntimePublication = false;
    bool _transportWasPlaying = false;
    bool _playingSongArrangementValid = false;
    SongArrangement _playingSongArrangement;
    std::chrono::steady_clock::time_point publicationDue {};
    void requestProcess();
};
} // namespace s3g::tracker
