#include "s3g/tracker/clap_editor_coordinator.h"
#include "s3g/tracker/clap_command_controller.h"
#include "s3g/tracker/atomic_project_store.h"
#include <sstream>
#include <utility>

namespace s3g::tracker {
using app::TrackerViewState;
using app::WorkspaceCallbacks;
namespace {
std::size_t longestPatternColumnLength(
    const s3g::tracker::Pattern& pattern) noexcept
{
    std::size_t longest = 1u;
    const auto include = [&](const s3g::tracker::ColumnDefinition& column) {
        longest = std::max(longest, column.length);
    };
    for (const auto& track : pattern.tracks) {
        include(track.noteColumn);
        include(track.instrumentColumn);
        include(track.velocityColumn);
        for (const auto& pair : track.fxPairs) {
            include(pair.actionColumn);
            include(pair.valueColumn);
        }
    }
    return std::clamp(longest, std::size_t { 1u },
        static_cast<std::size_t>(
            s3g::tracker::kMaximumSongPatternRows));
}

using s3g::tracker::refreshProjectBurstUsageCounts;

std::string nextPatternId(const s3g::tracker::PatternBank& bank)
{
    for (std::size_t index = 1u;
         index <= s3g::tracker::kMaximumPatternBankEntries; ++index) {
        std::string id = "A";
        if (index < 10u) id += "0";
        id += std::to_string(index);
        if (!bank.findEntry(id)) return id;
    }
    return {};
}

PatternBankEntry newPatternEntry(const PatternBankEntry& source,
    std::string id, bool duplicate)
{
    PatternBankEntry entry = source;
    entry.id = std::move(id);
    entry.pattern.name = duplicate
        ? source.pattern.name + " COPY" : "PATTERN " + entry.id;
    if (duplicate) return entry;
    constexpr std::size_t kBlankPatternRows = 64u;
    entry.pattern.visibleRows = kBlankPatternRows;
    for (auto& track : entry.pattern.tracks) {
        track.notes.assign(kBlankPatternRows,
            s3g::tracker::NoteCell::rest());
        track.instruments.assign(kBlankPatternRows,
            s3g::tracker::InstrumentCell::empty());
        track.velocities.assign(kBlankPatternRows,
            s3g::tracker::ValueCell::defaultValue());
        track.noteColumn = {};
        track.instrumentColumn = {};
        track.velocityColumn = {};
        track.noteColumn.length = kBlankPatternRows;
        track.instrumentColumn.length = kBlankPatternRows;
        track.velocityColumn.length = kBlankPatternRows;
        for (auto& pair : track.fxPairs) {
            pair.actions.assign(kBlankPatternRows,
                s3g::tracker::FxActionCell::empty());
            pair.values.assign(kBlankPatternRows,
                s3g::tracker::FxValueCell::previous());
            pair.actionColumn = {};
            pair.valueColumn = {};
            pair.actionColumn.length = kBlankPatternRows;
            pair.valueColumn.length = kBlankPatternRows;
        }
    }
    return entry;
}

PatternBankEntry variationPatternEntry(const PatternBankEntry& source,
    std::string id,
    const s3g::tracker::PatternVariationRequest& variation)
{
    PatternBankEntry entry = source;
    entry.id = std::move(id);
    entry.pattern = variation.generatedSession.pattern;
    entry.laneDefaultNotes = variation.generatedSession.laneDefaultNotes;
    entry.aliases = variation.generatedSession.aliases;
    entry.pattern.name = source.pattern.name.empty()
        ? "VAR " + entry.id
        : source.pattern.name + " VAR " + entry.id;
    return entry;
}




}
ClapEditorCoordinator::ClapEditorCoordinator(midi::Engine& plugin,
    editor::SongEditor& songModel, ClapEditorServices services)
    : _plugin(&plugin), song(songModel), platform(std::move(services)),
      _state(std::make_unique<TrackerViewState>()),
      _callbacks(std::make_unique<WorkspaceCallbacks>()),
      _documentController(std::make_unique<ClapDocumentController>(*_state))
{
    _state->midiStepInputAvailable = true;
    _state->midiStepRecordMode = MidiStepRecordMode::Off;
    _state->midiRecordTrack = 0;
    _state->fillActive = _plugin->fillActive.load(std::memory_order_acquire);
    _state->midiRoute = "1 OUT • 1 REC IN • CH 1–16";
    _state->audioOutputDevice = "REAPER HOST AUDIO";
    _state->audioAvailable = false;
    _plugin->midiStepRecordMode = uint8_t(MidiStepRecordMode::Off);
    _plugin->midiRecordTrack = 0;
    _callbacks->togglePlayback = [this] {
        auto* owner = this;
        if (!owner) return;
        if (!owner->platform.hostToggle()) {
            owner->_state->status = "Use REAPER transport controls";
            owner->platform.refresh();
        }
    };
    _callbacks->restartPlayback = [this] {
        auto* owner = this;
        if (!owner) return;
        owner->_plugin->requestRestart.store(true, std::memory_order_release);
        owner->requestProcess();
        owner->message("SYNC ALL queued for row 1; phase offsets ignored and REAPER transport unchanged"
           , false);
    };
    _callbacks->resyncTrack = [this](std::size_t track) {
        auto* owner = this;
        if (!owner || track >= s3g::tracker::kMaximumTrackCount) return;
        owner->_plugin->requestTrackResyncMask.fetch_or(
            uint32_t { 1u } << track, std::memory_order_release);
        owner->requestProcess();
        owner->message("Lane " + std::to_string(track + 1) + " SYNC queued for next tick; REAPER transport unchanged", false);
    };
    _callbacks->panic = [this] {
        auto* owner = this;
        if (!owner) return;
        owner->_plugin->requestPanic.store(true, std::memory_order_release);
        owner->requestProcess();
        owner->message("MIDI panic queued on channels 1–16", false);
    };
    _callbacks->previewBurst = [this](
        const s3g::tracker::BurstDefinition& burst, uint8_t midiChannel,
        double bpm, uint32_t ticksPerBeat) {
        auto* owner = this;
        if (!owner || burst.empty()) return;
        const bool transportRunning = owner->platform.hostPlaying().value_or(owner->_state->playing);
        if (transportRunning) {
            owner->_state->status =
                "Burst Preview is available while REAPER is stopped";
            owner->platform.refresh();
            return;
        }
        const double projectBpm = owner->platform.hostTempo()
            .value_or(bpm);
        owner->_plugin->pitchPreview.cancel();
        owner->_plugin->burstPreviewMailbox.publish(
            burst, midiChannel, projectBpm, ticksPerBeat);
        owner->requestProcess();
    };
    _callbacks->previewPitchSequence = [this](
        const std::vector<s3g::tracker::PitchPreviewEvent>& events,
        uint8_t midiChannel, double bpm, uint32_t ticksPerBeat) {
        auto* owner = this;
        if (!owner || events.empty()) return;
        const bool transportRunning = owner->platform.hostPlaying().value_or(owner->_state->playing);
        if (transportRunning) {
            owner->_state->status =
                "Pitch Map Preview is available while REAPER is stopped";
            owner->platform.refresh();
            return;
        }
        const double projectBpm = owner->platform.hostTempo()
            .value_or(bpm);
        s3g::tracker::BurstDefinition emptyBurst;
        owner->_plugin->burstPreviewMailbox.publish(
            emptyBurst, midiChannel, projectBpm, ticksPerBeat);
        owner->_plugin->pitchPreview.publish(
            events, midiChannel, projectBpm, ticksPerBeat);
        owner->requestProcess();
    };
    _callbacks->startAuthoringPreview = [this](
        const std::vector<s3g::tracker::PitchPreviewEvent>& events,
        uint8_t channel, double bpm, uint32_t ticks, uint32_t rows,
        bool loop) -> uint32_t {
        auto* owner = this;
        if (!owner || events.empty() || owner->platform.hostPlaying().value_or(owner->_state->playing)) return 0;
        const double tempo = owner->platform.hostTempo().value_or(bpm);
        owner->_plugin->burstPreviewMailbox.publish({}, channel, tempo, ticks);
        const auto token = owner->_plugin->pitchPreview.publish(
            events, channel, tempo, ticks, rows, loop, true);
        owner->requestProcess();
        return token;
    };
    _callbacks->stopAuthoringPreview = [this](uint32_t token) {
        auto* owner = this;
        if (!owner) return;
        owner->_plugin->pitchPreview.cancel(token);
        owner->requestProcess();
    };
    _callbacks->authoringPreviewPosition = [this](uint32_t token) -> int64_t {
        auto* owner = this;
        return owner ? owner->_plugin->pitchPreview.position(token) : -1;
    };
    _callbacks->loopAuthoringPreview = [this](uint32_t token, bool loop) {
        auto* owner = this;
        if (owner) owner->_plugin->pitchPreview.setLoop(token, loop);
    };
    _callbacks->showSongWindow = [this] {
        platform.showPage(editor::ShellPage::Song);
    };
    _callbacks->showGeometryPage = [this] {
        platform.showPage(editor::ShellPage::Geometry);
    };
    _callbacks->showBurstPage = [this] {
        platform.showPage(editor::ShellPage::Bursts);
    };
    _callbacks->showReshapePage = [this] {
        platform.showPage(editor::ShellPage::Reshape);
    };
    _callbacks->showPhrasePage = [this] {
        platform.showPage(editor::ShellPage::Phrases);
    };
    _callbacks->showAssemblePage = [this] {
        platform.showPage(editor::ShellPage::Assemble);
    };
    _callbacks->importAssetPack = [this] {
        presentImportAssetPack();
    };
    _callbacks->exportBurstAssetPack = [this](std::size_t slot) {
        auto* owner = this;
        if (!owner || !owner->_state
            || slot >= owner->_state->session.burstLibrary.bursts.size())
            return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto& burst = owner->_state->session.burstLibrary.bursts[slot];
        owner->presentExportAssetPack(s3g::tracker::makeBurstAssetPack(
            burst.name.empty() ? "BURST PACK" : burst.name,
            owner->_state->session.burstLibrary, slot));
    };
    _callbacks->exportBurstLibraryAssetPack = [this] {
        auto* owner = this;
        if (!owner || !owner->_state) return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* bank = s3g::tracker::findBurstBank(
            owner->_state->burstBanks, owner->_state->activeBurstBankId);
        const std::string name = bank && !bank->name.empty()
            ? bank->name : "BURST LIBRARY PACK";
        owner->presentExportAssetPack(s3g::tracker::makeBurstLibraryAssetPack(
            name, owner->_state->session.burstLibrary));
    };
    _callbacks->exportPhraseAssetPack = [this](std::size_t slot) {
        auto* owner = this;
        if (!owner || !owner->_state
            || slot >= owner->_state->phraseLibrary.phrases.size()) return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto& phrase = owner->_state->phraseLibrary.phrases[slot];
        owner->presentExportAssetPack(s3g::tracker::makePhraseAssetPack(
            phrase.name.empty() ? "PHRASE PACK" : phrase.name,
            owner->_state->phraseLibrary, slot,
            owner->_state->burstBanks));
    };
    _callbacks->exportPhraseLibraryAssetPack = [this] {
        auto* owner = this;
        if (!owner || !owner->_state) return;
        (void)syncActiveAssetBanks(*owner->_state);
        owner->presentExportAssetPack(s3g::tracker::makePhraseLibraryAssetPack(
                "PHRASE LIBRARY PACK", owner->_state->phraseLibrary,
                owner->_state->burstBanks));
    };
    _callbacks->copyBurstToProject = [this](std::size_t sourceSlot) {
        auto* owner = this;
        if (!owner || !owner->_state) return s3g::tracker::kBurstDefinitionCount;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* sourceBank = s3g::tracker::findBurstBank(
            owner->_state->burstBanks, owner->_state->activeBurstBankId);
        auto* projectBank = s3g::tracker::findBurstBank(
            owner->_state->burstBanks, s3g::tracker::kProjectAssetBankId);
        if (!sourceBank || !projectBank
            || sourceSlot >= sourceBank->library.bursts.size()
            || sourceBank->library.bursts[sourceSlot].empty())
            return s3g::tracker::kBurstDefinitionCount;
        const auto empty = std::find_if(projectBank->library.bursts.begin(),
            projectBank->library.bursts.end(), [](const auto& definition) {
                return definition.empty();
            });
        if (empty == projectBank->library.bursts.end()) {
            owner->_state->status = "Project Burst bank is full";
            owner->platform.refresh();
            return s3g::tracker::kBurstDefinitionCount;
        }
        const auto destination = static_cast<std::size_t>(
            empty - projectBank->library.bursts.begin());
        *empty = sourceBank->library.bursts[sourceSlot];
        if (sourceBank->id == s3g::tracker::kProjectAssetBankId)
            empty->name += " COPY";
        owner->_state->activeBurstBankId = s3g::tracker::kProjectAssetBankId;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        owner->commitProject(true);
        owner->platform.refresh();
        return destination;
    };
    _callbacks->copyPhraseToProject = [this](std::size_t sourceSlot) {
        auto* owner = this;
        if (!owner || !owner->_state) return s3g::tracker::kPhraseLibrarySlots;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* sourceBank = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, owner->_state->activePhraseBankId);
        auto* projectBank = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, s3g::tracker::kProjectAssetBankId);
        if (!sourceBank || !projectBank
            || sourceSlot >= sourceBank->library.phrases.size())
            return s3g::tracker::kPhraseLibrarySlots;
        const auto& source = sourceBank->library.phrases[sourceSlot];
        if (source.empty() && source.name.empty())
            return s3g::tracker::kPhraseLibrarySlots;
        const auto empty = std::find_if(projectBank->library.phrases.begin(),
            projectBank->library.phrases.end(), [](const auto& definition) {
                return definition.empty() && definition.name.empty();
            });
        if (empty == projectBank->library.phrases.end()) {
            owner->_state->status = "Project Phrase bank is full";
            owner->platform.refresh();
            return s3g::tracker::kPhraseLibrarySlots;
        }
        const auto destination = static_cast<std::size_t>(
            empty - projectBank->library.phrases.begin());
        *empty = source;
        if (sourceBank->id == s3g::tracker::kProjectAssetBankId)
            empty->name += " COPY";
        owner->_state->activePhraseBankId = s3g::tracker::kProjectAssetBankId;
        owner->_state->activeBurstBankId = s3g::tracker::kProjectAssetBankId;
        owner->_state->selectedPhrase = destination;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        owner->commitProject(true);
        owner->platform.refresh();
        return destination;
    };
    _callbacks->selectBurstBank = [this](s3g::tracker::AssetBankId id) {
        auto* owner = this;
        if (!owner || !owner->_state || id == owner->_state->activeBurstBankId)
            return;
        (void)syncActiveAssetBanks(*owner->_state);
        if (!s3g::tracker::findBurstBank(owner->_state->burstBanks, id)) return;
        owner->_state->activeBurstBankId = id;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        owner->commitProject(true);
        owner->platform.refresh();
    };
    _callbacks->selectPhraseBank = [this](s3g::tracker::AssetBankId id) {
        auto* owner = this;
        if (!owner || !owner->_state || id == owner->_state->activePhraseBankId)
            return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto* phrase = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, id);
        if (!phrase) return;
        owner->_state->activePhraseBankId = id;
        owner->_state->activeBurstBankId = phrase->companionBurstBankId;
        owner->_state->selectedPhrase = 0u;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        owner->commitProject(true);
        owner->platform.refresh();
    };
    _callbacks->clearPhraseBank = [this] {
        auto* owner = this;
        if (!owner || !owner->_state) return;
        owner->_state->phraseLibrary = {};
        owner->_state->selectedPhrase = 0u;
        owner->commitProject(true);
        owner->platform.refresh();
    };
    _callbacks->deletePhraseBank = [this] {
        auto* owner = this;
        if (!owner || !owner->_state
            || owner->_state->activePhraseBankId
                == s3g::tracker::kProjectAssetBankId) return;
        (void)syncActiveAssetBanks(*owner->_state);
        const auto id = owner->_state->activePhraseBankId;
        owner->_state->phraseBanks.erase(std::remove_if(
            owner->_state->phraseBanks.begin(), owner->_state->phraseBanks.end(),
            [id](const s3g::tracker::PhraseBank& bank) { return bank.id == id; }),
            owner->_state->phraseBanks.end());
        owner->_state->activePhraseBankId = s3g::tracker::kProjectAssetBankId;
        const auto* project = s3g::tracker::findPhraseBank(
            owner->_state->phraseBanks, s3g::tracker::kProjectAssetBankId);
        owner->_state->activeBurstBankId = project
            ? project->companionBurstBankId
            : s3g::tracker::kProjectAssetBankId;
        owner->_state->selectedPhrase = 0u;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        owner->commitProject(true);
        owner->platform.refresh();
    };
    _callbacks->deleteUnusedBursts = [this] {
        auto* owner = this;
        if (!owner || !owner->_state) return;
        refreshProjectBurstUsageCounts(*owner->_state);
        for (std::size_t slot = 0u;
             slot < owner->_state->session.burstLibrary.bursts.size(); ++slot)
            if (owner->_state->session.projectBurstUsageCounts[slot] == 0u)
                owner->_state->session.burstLibrary.bursts[slot] = {};
        owner->commitProject(true);
        owner->platform.refresh();
    };
    _callbacks->deleteBurstBank = [this] {
        auto* owner = this;
        if (!owner || !owner->_state
            || owner->_state->activeBurstBankId
                == s3g::tracker::kProjectAssetBankId) return;
        refreshProjectBurstUsageCounts(*owner->_state);
        const auto id = owner->_state->activeBurstBankId;
        const bool inUse = std::any_of(
            owner->_state->session.projectBurstUsageCounts.begin(),
            owner->_state->session.projectBurstUsageCounts.end(),
            [](std::size_t count) { return count != 0u; });
        const bool isCompanion = std::any_of(owner->_state->phraseBanks.begin(),
            owner->_state->phraseBanks.end(), [id](const auto& bank) {
                return bank.companionBurstBankId == id;
            });
        if (inUse || isCompanion) {
            owner->_state->status = inUse
                ? "Burst bank is still referenced by Patterns or Phrases"
                : "Delete its companion Phrase bank before deleting this Burst bank";
            owner->platform.refresh();
            return;
        }
        (void)syncActiveAssetBanks(*owner->_state);
        owner->_state->burstBanks.erase(std::remove_if(
            owner->_state->burstBanks.begin(), owner->_state->burstBanks.end(),
            [id](const auto& bank) { return bank.id == id; }),
            owner->_state->burstBanks.end());
        owner->_state->activeBurstBankId = s3g::tracker::kProjectAssetBankId;
        (void)loadActiveAssetBanks(*owner->_state);
        refreshProjectBurstUsageCounts(*owner->_state);
        owner->commitProject(true);
        owner->platform.refresh();
    };
    _callbacks->showTrackerPage = [this] {
        platform.showPage(editor::ShellPage::Tracker);
        platform.focusTracker();
    };
    _callbacks->showWarpPage = [this] {
        platform.showPage(editor::ShellPage::Warps);
    };
    _callbacks->previewPattern = [this](
        const s3g::tracker::Pattern& pattern) {
        auto* owner = this;
        if (!owner) return;
        owner->cancelRuntimePublication();
        ProjectDocument document = owner->currentDocument();
        auto* entry = document.patternBank.findEntry(
            document.patternBank.activePatternId);
        if (!entry) return;
        entry->pattern = pattern;
        if (!publishPreviewDocumentRuntime(*owner->_plugin,
                std::move(document))) {
            owner->_state->status = "Pattern reshape preview failed";
            owner->platform.refresh();
        }
    };
    _callbacks->clearPatternPreview = [this] {
        auto* owner = this;
        if (!owner) return;
        owner->cancelRuntimePublication();
        if (!publishStoredDocumentRuntime(*owner->_plugin)) {
            owner->_state->status = "Could not restore stored pattern";
            owner->platform.refresh();
        }
    };
    _callbacks->createPatternVariant = [this](
        const s3g::tracker::Pattern& pattern) {
        auto* owner = this;
        if (!owner) return;
        owner->cancelRuntimePublication();
        if (owner->_state->patternBank.entries.size()
                >= s3g::tracker::kMaximumPatternBankEntries
            || !syncSessionToActivePattern(*owner->_state)) {
            owner->_state->status = "Pattern bank is full; variant not created";
            owner->platform.refresh();
            return;
        }
        const std::string sourceId = owner->_state->patternBank.activePatternId;
        const auto* source = owner->_state->patternBank.findEntry(sourceId);
        const std::string id = nextPatternId(owner->_state->patternBank);
        if (!source || id.empty()) return;
        auto entry = newPatternEntry(*source, id, true);
        entry.pattern = pattern;
        entry.pattern.name = source->pattern.name.empty()
            ? "VAR " + id : source->pattern.name + " VAR " + id;
        owner->_state->patternBank.entries.push_back(std::move(entry));
        owner->_state->patternBank.activePatternId = id;
        if (!loadActivePatternIntoSession(*owner->_state)) return;
        owner->_state->session.selectedRow = 0u;
        owner->_state->status = "Created and selected variation " + id;
        owner->refreshSongPatterns();
        owner->commitProject(true);
    };
    _callbacks->showConsoleHelp = [this] {
        platform.showPage(editor::ShellPage::Help);
    };
    _callbacks->instrumentRackChanged = [this] {
        commitProjectWithoutRuntime(true);
    };
    _callbacks->instrumentRackReloaded = [this] {
        commitProject(false);
    };
    _callbacks->reportError = [this](const std::string& message) {
        auto* owner = this;
        if (!owner) return;
        owner->_state->status = message;
        owner->message(message, true);
    };
    _callbacks->selectionChanged = [] {
        // Editing selection is intentionally independent from REC LANE.
    };
    _callbacks->patternChanged = [this] {
        commitProject(true);
    };
    _callbacks->selectPattern = [this](const std::string& id) {
        selectPattern(id);
    };
    _callbacks->addPattern = [this](bool duplicate) {
        addPattern(duplicate);
    };
    _callbacks->renamePattern = [this] {
        renamePattern();
    };
    _callbacks->deletePattern = [this] {
        deletePattern();
    };
    _callbacks->transportChanged = [this] {
        refreshSongWarps();
        commitProject(true);
    };
    _callbacks->fillChanged = [this](bool active) {
        auto* owner = this;
        if (!owner) return;
        owner->_plugin->fillActive.store(active, std::memory_order_release);
        owner->requestProcess();
    };
    _callbacks->outputChanged = [this] {
        commitProject(true);
    };
    _callbacks->mainOutputGainChanged = [this](float) {
        commitProjectWithoutRuntime(true);
    };
    _callbacks->viewPreferencesChanged = [this] {
        commitProjectWithoutRuntime(true);
    };
    _callbacks->midiStepRecordModeChanged = [this](
        MidiStepRecordMode mode) {
        auto* owner = this;
        if (!owner) return;
        owner->updateMidiMonitorChannel();
        const auto previous = static_cast<MidiStepRecordMode>(
            owner->_plugin->midiStepRecordMode.exchange(
                static_cast<uint8_t>(mode), std::memory_order_acq_rel));
        if (mode != previous) owner->_midiLiveRecordState.clear();
        if (mode == MidiStepRecordMode::Off
            && previous != MidiStepRecordMode::Off) {
            owner->_plugin->requestMidiMonitorRelease.store(
                true, std::memory_order_release);
            owner->requestProcess();
        }
    };
    _callbacks->midiRecordTrackChanged = [this](std::size_t track) {
        auto* owner = this;
        if (!owner || !owner->_state
            || owner->_state->session.pattern.tracks.empty()) return;
        owner->_state->midiRecordTrack = std::min(track,
            owner->_state->session.pattern.tracks.size() - 1u);
        owner->_plugin->midiRecordTrack.store(static_cast<uint32_t>(
            owner->_state->midiRecordTrack), std::memory_order_release);
        owner->updateMidiMonitorChannel();
    };
    _callbacks->tracksReordered = [this](const std::string& patternId,
        std::size_t source, std::size_t destination) {
        auto* owner = this;
        if (!owner) return;
        owner->song.remapMute(static_cast<uint32_t>(source), static_cast<uint32_t>(destination), patternId);
    };
    _callbacks->executeCommand = [this](const std::string& command) {
        executeCommand(command);
    };


    console->execute = [this](const std::string& text) { executeCommand(text); };
    song.callbacks.changed = [this] {
        message("Song updated"); commitSongProjectEdit(true);
    };
    song.callbacks.modeChanged = [this](bool enabled) {
        _state->songPlaybackEnabled = enabled;
        _deferredSongRuntimePublication = false;
        commitProject(true);
    };
    song.callbacks.loopChanged = [this](bool enabled) {
        const bool running = platform.hostPlaying().value_or(_state->playing);
        if (running) {
            commitProjectWithoutRuntime(true);
            if (_playingSongArrangementValid) _playingSongArrangement.loop = enabled;
        } else {
            _deferredSongRuntimePublication = false;
            commitProject(true);
        }
        _plugin->songLoopEnabled.store(enabled, std::memory_order_relaxed);
        _plugin->songLoopRevision.fetch_add(1, std::memory_order_release);
        requestProcess();
        _state->status = enabled ? "Song loop enabled" : "Song loop disabled";
        message(_state->status);
        platform.refreshPlayback();
    };
    song.callbacks.launch = [this](std::size_t row, SongLaunchQuantization launch) {
        _plugin->songLaunchRow.store(uint32_t(row), std::memory_order_relaxed);
        _plugin->songLaunchQuantization.store(uint32_t(launch), std::memory_order_relaxed);
        cancelRuntimePublication();
        _plugin->songArrangementUpdatePending.store(false, std::memory_order_release);
        auto document = currentDocument();
        if (!queueSongDocument(*_plugin, document, row, launch)) {
            _state->status = "Could not prepare the selected Song row for queueing";
            message(_state->status, true); platform.refresh(); return;
        }
        _deferredSongRuntimePublication = false;
        _playingSongArrangement = document.song;
        _playingSongArrangementValid = true;
        song.pendingRow = row; song.pendingQuantization = launch;
        _state->status = "Queued quantized Song row " + std::to_string(row + 1);
        message(_state->status);
    };
    song.callbacks.saveProject = [this] { presentSaveSongProject(); };
    song.callbacks.loadProject = [this] { presentLoadSongProject(); };
    ProjectDocument initial;
    {
        std::lock_guard<std::mutex> lock(_plugin->documentMutex);
        initial = _plugin->document;
    }
    applyDocument(initial);
    resetHistory(currentDocument());
}

ClapEditorCoordinator::~ClapEditorCoordinator()
{
    stop();
    song.callbacks = {};
    console->execute = {};
}
void ClapEditorCoordinator::requestProcess()
{
    if (_plugin->services.requestProcess)
        _plugin->services.requestProcess(_plugin->services.context);
}
void ClapEditorCoordinator::message(const std::string& text, bool error)
{
    console->append(text, error);
}
void ClapEditorCoordinator::stop()
{
    disarmMidiStepRecording();
    _plugin->pitchPreview.cancel();
    consumeMidiStepCaptures();
    flushRuntimePublication();
}
void ClapEditorCoordinator::scheduleRuntimePublication()
{
    _runtimePublicationPending = true;
    publicationDue = std::chrono::steady_clock::now()
        + std::chrono::microseconds(16667);
}
void ClapEditorCoordinator::cancelRuntimePublication()
{
    _runtimePublicationPending = false;
}
void ClapEditorCoordinator::refreshSongPatterns()
{
    std::vector<editor::SongPatternInfo> patterns;
    for (const auto& entry : _state->patternBank.entries)
        patterns.push_back({entry.id, entry.pattern.name,
            uint32_t(longestPatternColumnLength(entry.pattern)),
            uint32_t(entry.pattern.tracks.size())});
    song.setPatterns(std::move(patterns), _state->patternBank.activePatternId);
}
void ClapEditorCoordinator::refreshSongWarps()
{
    song.warps = _state->session.warpLibrary;
}
void ClapEditorCoordinator::renamePattern()
{
    const auto id = _state->patternBank.activePatternId;
    const auto* entry = _state->patternBank.findEntry(id);
    if (!entry) return;
    const auto name = platform.promptText("Rename Pattern", entry->pattern.name);
    if (!name) return;
    const auto start = name->find_first_not_of(" \r\n\t");
    if (start == std::string::npos) return;
    const auto end = name->find_last_not_of(" \r\n\t");
    auto* edited = _state->patternBank.findEntry(id);
    if (!edited) return;
    edited->pattern.name = name->substr(start, end - start + 1);
    if (_state->patternBank.activePatternId == id)
        _state->session.pattern.name = edited->pattern.name;
    refreshSongPatterns();
    commitProjectWithoutRuntime(true);
}
void ClapEditorCoordinator::presentSaveSongProject()
{
    const auto path = platform.fileDialog(true, "Save Tracker Song + Patterns", "s3gt", "Tracker Song.s3gt");
    if (path.empty()) return;
    const auto result = saveProjectDocumentAtomically(currentDocument(), path);
    _state->status = result.ok() ? "Saved Song + Patterns"
        : "Could not save Song + Patterns: " + result.message;
    message(_state->status, !result.ok()); platform.refresh();
}
void ClapEditorCoordinator::presentLoadSongProject()
{
    const auto path = platform.fileDialog(false, "Load Tracker Song + Patterns", "s3gt", {});
    if (path.empty()) return;
    ProjectDocument document;
    const auto result = loadProjectDocument(path, document);
    if (result.ok()) { applyDocument(document); commitProject(true); }
    _state->status = result.ok() ? "Loaded Song + Patterns"
        : "Could not load Song + Patterns: " + result.message;
    message(_state->status, !result.ok()); platform.refresh();
}
void ClapEditorCoordinator::presentExportAssetPack(const TrackerAssetPack& pack)
{
    std::string encoded;
    const auto validation = encodeTrackerAssetPack(pack, encoded);
    if (!validation.ok()) { message("Could not encode asset pack: " + validation.message, true); return; }
    const auto snapshot = pack;
    const auto path = platform.fileDialog(true, "Export Tracker Phrase + Burst Pack", "s3gpack",
        (snapshot.name.empty() ? "Tracker Assets" : snapshot.name) + ".s3gpack");
    if (path.empty()) return;
    const auto result = saveTrackerAssetPackAtomically(snapshot, path);
    _state->status = result.ok() ? "Exported Tracker asset pack"
        : "Could not export asset pack: " + result.message;
    message(_state->status, !result.ok()); platform.refresh();
}
void ClapEditorCoordinator::presentImportAssetPack()
{
    const auto path = platform.fileDialog(false, "Import Tracker Phrase + Burst Pack", "s3gpack", {});
    if (path.empty()) return;
    TrackerAssetPack pack;
    auto result = loadTrackerAssetPack(path, pack);
    auto document = currentDocument();
    AssetPackImportReport report;
    if (result.ok()) result = importTrackerAssetPack(pack, document, &report);
    if (!result.ok()) {
        _state->status = "Could not import asset pack"
            + (result.location.empty() ? std::string{} : " at " + result.location)
            + ": " + result.message;
        message(_state->status, true); platform.refresh(); return;
    }
    applyDocument(document); commitProject(true);
    std::ostringstream summary;
    summary << "Imported asset pack: " << report.burstsAdded << " Bursts + "
        << report.phrasesAdded << " Phrases";
    if (report.burstsReused || report.phrasesReused)
        summary << " (reused " << report.burstsReused << " Bursts, " << report.phrasesReused << " Phrases)";
    _state->status = summary.str(); message(_state->status); platform.refresh();
}
void ClapEditorCoordinator::consumeMidiStepCaptures()
{
    if (!_state) return;
    MidiStepCapture capture;
    bool changed = false;
    bool reportedSongConflict = false;
    while (_plugin->midiStepCaptures.pop(capture)) {
        const bool live = capture.mode == MidiStepRecordMode::LiveQuantized
            || capture.mode == MidiStepRecordMode::LiveUnquantized;
        if (live && _state->songPlaybackEnabled) {
            if (!reportedSongConflict) {
                reportedSongConflict = true;
                message("LIVE recording targets one selected pattern; turn SONG TRANSPORT off first"
                   , true);
            }
            continue;
        }
        const auto result = s3g::tracker::recordMidiStep(_state->session,
            capture.mode, capture, _plugin->sampleRate,
            &_midiLiveRecordState, _state->trackerRowJump);
        if (result.recorded()) {
            changed = true;
            std::ostringstream message;
            const char* label = result.release
                ? (capture.mode == MidiStepRecordMode::LiveQuantized
                        ? "LIVE Q REL" : "LIVE MT REL")
                : capture.mode == MidiStepRecordMode::Step
                ? "STEP REC" : capture.mode
                    == MidiStepRecordMode::LiveQuantized
                ? "LIVE Q REC" : "LIVE MT REC";
            message << label << " CH" << static_cast<unsigned>(capture.channel)
                    << " note " << static_cast<unsigned>(capture.note)
                    << " → lane " << (result.track + 1u)
                    << ", row " << (result.row + 1u);
            if (result.holdRows > 0u)
                message << ", " << result.holdRows << " HLD";
            if (capture.mode == MidiStepRecordMode::LiveUnquantized
                && result.fxPair < s3g::tracker::kFxPairCount) {
                message << ", MT " << std::lround(
                    static_cast<double>(result.microTime) * 100.0) << '%';
                if (result.timingClamped) message << " (clamped)";
            }
            this->message(message.str(), false);
        } else if (result.code == MidiStepRecordCode::FxUnavailable) {
            message("LIVE MT needs an empty SEQ1 or SEQ2 cell on the captured row"
               , true);
        } else if (result.code == MidiStepRecordCode::TimingUnavailable) {
            message(capture.rowKnown
                    ? "LIVE MT requires a nonzero Micro Time range"
                    : "LIVE recording requires running REAPER transport and a known tracker row"
               , true);
        }
    }
    const uint64_t dropped = _plugin->midiStepCaptures.droppedCount();
    if (dropped != _reportedStepRecordDrops) {
        _reportedStepRecordDrops = dropped;
        message("MIDI record input overflowed; reduce controller density"
           , true);
    }
    if (changed) commitProject(true);
}

void ClapEditorCoordinator::disarmMidiStepRecording()
{
    if (!_state) return;
    _midiLiveRecordState.clear();
    _state->midiStepRecordMode = MidiStepRecordMode::Off;
    const auto previous = static_cast<MidiStepRecordMode>(
        _plugin->midiStepRecordMode.exchange(
            static_cast<uint8_t>(MidiStepRecordMode::Off),
            std::memory_order_acq_rel));
    if (previous != MidiStepRecordMode::Off) {
        _plugin->requestMidiMonitorRelease.store(
            true, std::memory_order_release);
        requestProcess();
    }
    platform.refresh();
}

void ClapEditorCoordinator::updateMidiMonitorChannel()
{
    if (!_state || _state->session.pattern.tracks.empty()) {
        if (_state) _state->midiRecordTrack = 0u;
        _plugin->midiRecordTrack.store(0u, std::memory_order_release);
        _plugin->midiMonitorChannel.store(0u, std::memory_order_release);
        return;
    }
    const auto lane = std::min(_state->midiRecordTrack,
        _state->session.pattern.tracks.size() - 1u);
    _state->midiRecordTrack = lane;
    _plugin->midiRecordTrack.store(
        static_cast<uint32_t>(lane), std::memory_order_release);
    const uint8_t channel = static_cast<uint8_t>(std::clamp<int>(
        _state->session.pattern.tracks[lane].midiChannel, 1, 16) - 1);
    _plugin->midiMonitorChannel.store(channel, std::memory_order_release);
}

ProjectDocument ClapEditorCoordinator::currentDocument()
{
    return _documentController->snapshot(song.snapshot());
}

void ClapEditorCoordinator::applyDocument(const ProjectDocument& document)
{
    _midiLiveRecordState.clear();
    const auto midiDocument = _documentController->apply(document);
    _state->status = "REAPER host sync • MIDI output ready";
    refreshSongWarps();
    // Song rows validate their mute masks against the pattern catalog. Load
    // that catalog first so rows referencing anything other than the initial
    // A01 placeholder do not have their saved lane mutes pruned as unknown.
    refreshSongPatterns();
    song.setArrangement(midiDocument.song);
    song.playbackEnabled = _state->songPlaybackEnabled;
    updateMidiMonitorChannel();
    platform.refresh();
}

void ClapEditorCoordinator::updateHistoryAvailability()
{
    if (!_state) return;
    _documentController->updateHistoryAvailability();
}

void ClapEditorCoordinator::resetHistory(const ProjectDocument& document)
{
    const auto result = _documentController->resetHistory(document);
    if (!result.ok()) {
        message("Could not initialize Tracker edit history: " + result.message
           , true);
    }
    updateHistoryAvailability();
    platform.refresh();
}

void ClapEditorCoordinator::recordHistory(const ProjectDocument& document)
{
    const auto result = _documentController->recordHistory(document);
    if (!result.ok()) {
        message("Could not record Tracker edit history: " + result.message
           , true);
    }
    updateHistoryAvailability();
}

void ClapEditorCoordinator::undoProject()
{
    ProjectDocument document;
    const auto result = _documentController->undo(document);
    if (!result.ok()) {
        message(result.message, true);
        updateHistoryAvailability();
        platform.refresh();
        return;
    }
    cancelRuntimePublication();
    applyDocument(document);
    publishDocument(*_plugin, std::move(document), true);
    updateHistoryAvailability();
    _state->status = "Undid Tracker edit";
    message(_state->status, false);
    platform.refresh();
}

void ClapEditorCoordinator::redoProject()
{
    ProjectDocument document;
    const auto result = _documentController->redo(document);
    if (!result.ok()) {
        message(result.message, true);
        updateHistoryAvailability();
        platform.refresh();
        return;
    }
    cancelRuntimePublication();
    applyDocument(document);
    publishDocument(*_plugin, std::move(document), true);
    updateHistoryAvailability();
    _state->status = "Redid Tracker edit";
    message(_state->status, false);
    platform.refresh();
}

void ClapEditorCoordinator::commitProject(bool dirty)
{
    if (!_state) return;
    ProjectDocument document = currentDocument();
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    updateMidiMonitorChannel();
    refreshSongPatterns();
    if (dirty) recordHistory(document);
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    scheduleRuntimePublication();
    platform.refresh();
}

void ClapEditorCoordinator::commitProjectWithoutRuntime(bool dirty)
{
    if (!_state) return;
    ProjectDocument document = currentDocument();
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    updateMidiMonitorChannel();
    refreshSongPatterns();
    if (dirty) recordHistory(document);
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    platform.refresh();
}

void ClapEditorCoordinator::commitSongProjectEdit(bool dirty)
{
    if (!_state) return;
    const bool transportRunning = platform.hostPlaying()
        .value_or(_state->playing);
    if (!transportRunning) {
        _deferredSongRuntimePublication = false;
        commitProject(dirty);
        return;
    }

    // Hand an edited arrangement over at the next Song-row boundary. A fresh
    // runtime starts on the row that naturally follows the one currently
    // sounding, so future-row edits are heard without restarting the Song or
    // mutating scheduler-owned vectors on the audio thread.
    cancelRuntimePublication();
    const int32_t priorPendingRow = _plugin->visualPendingSongRow.load(
        std::memory_order_acquire);
    const auto priorQuantization = static_cast<SongLaunchQuantization>(
        std::min<uint32_t>(_plugin->songLaunchQuantization.load(
            std::memory_order_acquire), static_cast<uint32_t>(
                SongLaunchQuantization::NextSongRow)));
    cancelQueuedVariation(*_plugin);
    ProjectDocument document = currentDocument();
    _state->patternBank = document.patternBank;
    _state->instrumentRack = document.instrumentRack;
    _state->selectedRackInstrument = document.instrumentRack.selectedNode;
    (void)loadActivePatternIntoSession(*_state);
    updateMidiMonitorChannel();
    refreshSongPatterns();
    if (dirty) recordHistory(document);

    const int32_t audibleRow = _plugin->visualSongRow.load(
        std::memory_order_acquire);
    const std::size_t currentRow = audibleRow >= 0
        ? static_cast<std::size_t>(audibleRow)
        : _state->songPlaybackRow;
    // Row drag can change numeric indices while the old immutable runtime is
    // still sounding. Resolve that row through its stable ID before choosing
    // the successor in the edited arrangement.
    std::size_t editedCurrentRow = currentRow;
    if (_playingSongArrangementValid
        && currentRow < _playingSongArrangement.rows.size()) {
        const uint32_t currentId = _playingSongArrangement.rows[currentRow].id;
        if (currentId != 0u) {
            const auto found = std::find_if(document.song.rows.begin(),
                document.song.rows.end(), [currentId](
                    const s3g::tracker::SongRow& row) {
                    return row.id == currentId;
                });
            if (found != document.song.rows.end())
                editedCurrentRow = static_cast<std::size_t>(
                    found - document.song.rows.begin());
        }
    }
    const bool hasNextRow = editedCurrentRow + 1u < document.song.rows.size();
    const bool wraps = !document.song.rows.empty() && document.song.loop;
    std::optional<std::size_t> preservedPendingRow;
    if (priorPendingRow >= 0 && _playingSongArrangementValid
        && static_cast<std::size_t>(priorPendingRow)
            < _playingSongArrangement.rows.size()) {
        const uint32_t pendingId = _playingSongArrangement.rows[
            static_cast<std::size_t>(priorPendingRow)].id;
        const auto found = std::find_if(document.song.rows.begin(),
            document.song.rows.end(), [pendingId](
                const s3g::tracker::SongRow& row) {
                return pendingId != 0u && row.id == pendingId;
            });
        if (found != document.song.rows.end())
            preservedPendingRow = static_cast<std::size_t>(
                found - document.song.rows.begin());
    }
    if (preservedPendingRow || hasNextRow || wraps) {
        const std::size_t launchRow = preservedPendingRow
            ? *preservedPendingRow : hasNextRow ? editedCurrentRow + 1u : 0u;
        const auto quantization = preservedPendingRow
            ? priorQuantization : SongLaunchQuantization::NextSongRow;
        _plugin->songLaunchRow.store(static_cast<uint32_t>(launchRow),
            std::memory_order_relaxed);
        _plugin->songLaunchQuantization.store(static_cast<uint32_t>(
            quantization),
            std::memory_order_relaxed);
        const SongArrangement updatedArrangement = document.song;
        _plugin->songArrangementUpdatePending.store(true,
            std::memory_order_release);
        if (queueSongDocument(*_plugin, std::move(document), launchRow,
                quantization, dirty)) {
            _playingSongArrangement = updatedArrangement;
            _playingSongArrangementValid = true;
            _deferredSongRuntimePublication = false;
            _state->status = preservedPendingRow
                ? "Song update preserved queued row "
                    + std::to_string(launchRow + 1u)
                : "Song update scheduled for row "
                    + std::to_string(launchRow + 1u);
            message(_state->status, false);
            platform.refresh();
            return;
        }
        _plugin->songArrangementUpdatePending.store(false,
            std::memory_order_release);
        document = currentDocument();
    }

    // A non-looping final row has no future boundary to hand over to. Keep
    // the edit persistent and prepare it once REAPER stops.
    storeDocumentWithoutRuntime(*_plugin, std::move(document), dirty);
    _deferredSongRuntimePublication = true;
    _state->status = "Song edit saved • no future row before REAPER stop";
    platform.refresh();
}

void ClapEditorCoordinator::flushRuntimePublication()
{
    if (!_runtimePublicationPending) return;
    _runtimePublicationPending = false;
    if (!publishStoredDocumentRuntime(*_plugin)) {
        message("Could not prepare the edited Tracker playback runtime"
           , true);
    }
}

bool ClapEditorCoordinator::installPatternVariation(const PatternVariationRequest& variation)
{
    cancelRuntimePublication();
    if (_state->patternBank.entries.size()
            >= s3g::tracker::kMaximumPatternBankEntries
        || !syncSessionToActivePattern(*_state)) return false;
    const std::string sourceId = _state->patternBank.activePatternId;
    const auto* source = _state->patternBank.findEntry(sourceId);
    const std::string id = nextPatternId(_state->patternBank);
    if (!source || id.empty()) return false;
    _state->patternBank.entries.push_back(variationPatternEntry(
        *source, id, variation));

    if (variation.launch == PatternVariationLaunch::None) {
        _state->status = "Created variation " + id
            + "; active pattern remains " + sourceId;
        refreshSongPatterns();
        commitProjectWithoutRuntime(true);
        message(_state->status, false);
        return true;
    }

    _state->patternBank.activePatternId = id;
    if (!loadActivePatternIntoSession(*_state)) return false;
    _state->session.selectedRow = 0u;
    ProjectDocument document = currentDocument();
    _state->patternBank = document.patternBank;
    (void)loadActivePatternIntoSession(*_state);
    const ProjectDocument historyDocument = document;
    const bool playing = _plugin->visualPlaying.load(
        std::memory_order_acquire);
    bool installed = true;
    if (playing) {
        installed = queueVariationDocument(*_plugin, std::move(document),
            variation.launch, true);
    } else {
        publishDocument(*_plugin, std::move(document), true);
    }
    if (!installed) {
        _state->patternBank.entries.pop_back();
        _state->patternBank.activePatternId = sourceId;
        (void)loadActivePatternIntoSession(*_state);
        _state->status = "Could not prepare variation runtime";
        refreshSongPatterns();
        platform.refresh();
        return false;
    }
    recordHistory(historyDocument);
    _state->status = playing
        ? "Queued variation " + id + " for quantized launch"
        : "Selected variation " + id;
    refreshSongPatterns();
    platform.refresh();
    message(_state->status, false);
    return true;
}

void ClapEditorCoordinator::selectPattern(const std::string& patternId)
{
    if (patternId == _state->patternBank.activePatternId
        || !_state->patternBank.findEntry(patternId)) return;
    if (!syncSessionToActivePattern(*_state)
        || !_state->patternBank.selectPattern(patternId)
        || !loadActivePatternIntoSession(*_state)) return;
    _state->status = "Selected pattern " + patternId;
    refreshSongPatterns();
    commitProject(true);
}

void ClapEditorCoordinator::addPattern(bool duplicate)
{
    if (_state->patternBank.entries.size()
        >= s3g::tracker::kMaximumPatternBankEntries) return;
    if (!syncSessionToActivePattern(*_state)) return;
    const auto* source = _state->patternBank.findEntry(
        _state->patternBank.activePatternId);
    const std::string id = nextPatternId(_state->patternBank);
    if (!source || id.empty()) return;
    _state->patternBank.entries.push_back(newPatternEntry(
        *source, id, duplicate));
    _state->patternBank.activePatternId = id;
    (void)loadActivePatternIntoSession(*_state);
    _state->session.selectedRow = 0u;
    _state->status = duplicate ? "Duplicated pattern " + id
                               : "Created pattern " + id;
    refreshSongPatterns();
    commitProject(true);
}

void ClapEditorCoordinator::deletePattern()
{
    if (_state->patternBank.entries.size() <= 1u) return;
    const std::string id = _state->patternBank.activePatternId;
    const auto arrangement = song.snapshot();
    for (const auto& row : arrangement.rows) {
        if (row.patternId == id) {
            _state->status = "Cannot delete a pattern used by Song mode";
            message(_state->status, true);
            return;
        }
    }
    const auto found = std::find_if(_state->patternBank.entries.begin(),
        _state->patternBank.entries.end(), [&](const auto& entry) {
            return entry.id == id;
        });
    if (found == _state->patternBank.entries.end()) return;
    const auto index = static_cast<std::size_t>(std::distance(
        _state->patternBank.entries.begin(), found));
    _state->patternBank.entries.erase(found);
    _state->patternBank.activePatternId = _state->patternBank.entries[
        std::min(index, _state->patternBank.entries.size() - 1u)].id;
    (void)loadActivePatternIntoSession(*_state);
    refreshSongPatterns();
    commitProject(true);
}

void ClapEditorCoordinator::executeCommand(const std::string& command)
{
    s3g::tracker::ClapCommandServices services;
    services.message = [this](const std::string& message, bool error) {
        this->message(message, error);
    };
    services.showHelp = [this] { platform.showPage(editor::ShellPage::Help); };
    services.undo = [this] { undoProject(); };
    services.redo = [this] { redoProject(); };
    services.commitRuntime = [this] { commitProject(true); };
    services.commitDocument = [this] { commitProjectWithoutRuntime(true); };
    services.refreshWarps = [this] { refreshSongWarps(); };
    services.refreshUI = [this] { platform.refresh(); };
    services.updateMonitor = [this] { updateMidiMonitorChannel(); };
    services.installVariation = [this](const s3g::tracker::PatternVariationRequest& variation) {
        return bool(installPatternVariation(variation));
    };
    services.requestHostContinue = [this] { return platform.hostContinue(); };
    services.requestHostStop = [this] { return platform.hostStop(); };
    services.panic = [this] { _plugin->requestPanic.store(true, std::memory_order_release); };
    s3g::tracker::executeClapCommand(*_state, command, services);
}

void ClapEditorCoordinator::pollDisplay()
{
    if (_runtimePublicationPending && std::chrono::steady_clock::now() >= publicationDue)
        flushRuntimePublication();
    drainRetiredRuntimes(*_plugin);
    consumeMidiStepCaptures();
    const bool playing = platform.hostPlaying().value_or(
        _plugin->visualPlaying.load(std::memory_order_relaxed));
    if (!playing)
        _plugin->visualPlaying.store(false, std::memory_order_relaxed);
    if (playing && !_transportWasPlaying) {
        _playingSongArrangement = song.snapshot();
        _playingSongArrangementValid = true;
    } else if (!playing && _transportWasPlaying) {
        _playingSongArrangementValid = false;
        if (_deferredSongRuntimePublication) {
            _deferredSongRuntimePublication = false;
            if (publishStoredDocumentRuntime(*_plugin)) {
                message("Song edits applied for the next transport start"
                   , false);
            } else {
                message("Could not prepare the edited Song playback runtime"
                   , true);
            }
        }
    }
    _transportWasPlaying = playing;
    _state->playing = playing;
    const double callbackTempo = _plugin->visualHostTempo.load(
        std::memory_order_relaxed);
    // REAPER can suspend audio processing while stopped, so no new CLAP
    // transport snapshot arrives after its master tempo field changes. Query
    // that host value on the GUI/main thread only in the stopped state. While
    // playing, keep the sample-accurate process transport authoritative so a
    // tempo map is represented at the actual playback position.
    _state->hostBpm = playing ? callbackTempo
        : platform.hostTempo().value_or(callbackTempo);
    (void)_state->hostBpm;
    _state->paused = false;
    VisualPlaybackFrame captured;
    for (std::size_t track = 0u;
         track < s3g::tracker::kMaximumTrackCount; ++track) {
        captured.notePlayheads[track] = _plugin->notePlayheads[track].load(
            std::memory_order_relaxed);
        VisualNoteHitEvent hit;
        const bool newHit = _plugin->visualNoteHits[track].readLatest(
            _consumedNoteHitSequences[track], hit);
        captured.noteHits[track] = playing && newHit;
        if (newHit) {
            captured.noteHitRows[track] = hit.row;
            captured.noteHitSampleTimes[track] = hit.absoluteSampleTime;
        }
        captured.instrumentPlayheads[track]
            = _plugin->instrumentPlayheads[track].load(
                std::memory_order_relaxed);
        captured.velocityPlayheads[track]
            = _plugin->velocityPlayheads[track].load(
                std::memory_order_relaxed);
        for (std::size_t pair = 0u; pair < s3g::tracker::kFxPairCount; ++pair) {
            captured.fxActionPlayheads[track][pair]
                = _plugin->fxActionPlayheads[track][pair].load(
                    std::memory_order_relaxed);
            captured.fxValuePlayheads[track][pair]
                = _plugin->fxValuePlayheads[track][pair].load(
                    std::memory_order_relaxed);
        }
    }
    captured.songRow = _plugin->visualSongRow.load(
        std::memory_order_relaxed);
    captured.pendingSongRow = _plugin->visualPendingSongRow.load(
        std::memory_order_relaxed);
    captured.pendingSongQuantization
        = _plugin->visualPendingSongQuantization.load(
            std::memory_order_relaxed);
    captured.subrowPhase = _plugin->visualSubrowPhase.load(
        std::memory_order_relaxed);
    captured.timingWarpTick = _plugin->visualTimingWarpTick.load(
        std::memory_order_relaxed);
    if (playing && _visualFramePrimed) {
        _state->notePlayheads = _pendingVisualFrame.notePlayheads;
        _state->noteHits = _pendingVisualFrame.noteHits;
        _state->noteHitRows = _pendingVisualFrame.noteHitRows;
        _state->noteHitSampleTimes
            = _pendingVisualFrame.noteHitSampleTimes;
        _state->instrumentPlayheads
            = _pendingVisualFrame.instrumentPlayheads;
        _state->velocityPlayheads = _pendingVisualFrame.velocityPlayheads;
        _state->fxActionPlayheads = _pendingVisualFrame.fxActionPlayheads;
        _state->fxValuePlayheads = _pendingVisualFrame.fxValuePlayheads;
        _state->subrowPlaybackPhase = _pendingVisualFrame.subrowPhase;
        _state->timingWarpPlaybackTick
            = _pendingVisualFrame.timingWarpTick;
    } else {
        _state->noteHits.fill(false);
        if (!playing) {
            _state->subrowPlaybackPhase = 0.0f;
            _state->timingWarpPlaybackTick = 0u;
        }
    }
    const int32_t songRow = playing && _visualFramePrimed
        ? _pendingVisualFrame.songRow : captured.songRow;
    const int32_t pendingSongRow = playing && _visualFramePrimed
        ? _pendingVisualFrame.pendingSongRow : captured.pendingSongRow;
    const uint32_t pendingSongQuantization = playing && _visualFramePrimed
        ? _pendingVisualFrame.pendingSongQuantization
        : captured.pendingSongQuantization;
    _pendingVisualFrame = std::move(captured);
    _visualFramePrimed = playing;
    _state->songPlaybackActive = _state->playing
        && _state->songPlaybackEnabled && songRow >= 0;
    _state->songPlaybackRowValid = songRow >= 0;
    _state->songPlaybackRow = songRow >= 0
        ? static_cast<std::size_t>(songRow) : 0u;
    _state->songPlaybackPatternId.clear();
    _state->songPlaybackMutedTracks = 0u;
    _state->timingWarpPlaybackActive = false;
    _state->timingWarpPlaybackFromSong = false;
    _state->timingWarpPlaybackCycleTicks = 1u;
    _state->timingWarpPlaybackStack.clear();
    if (_state->playing && !_state->songPlaybackEnabled
        && _state->session.transport.timingWarpEnabled) {
        _state->timingWarpPlaybackActive = true;
        _state->timingWarpPlaybackCycleTicks = std::max<uint32_t>(1u,
            _state->session.transport.warpCycleTicks);
        _state->timingWarpPlaybackStack
            = _state->session.transport.timingWarp;
    }
    if (_state->songPlaybackActive) {
        const auto arrangement = _playingSongArrangementValid
            ? _playingSongArrangement : song.snapshot();
        if (_state->songPlaybackRow < arrangement.rows.size()) {
            const auto& activeRow = arrangement.rows[
                _state->songPlaybackRow];
            _state->songPlaybackPatternId = activeRow.patternId;
            _state->songPlaybackMutedTracks = activeRow.mutedTracks;
            if (activeRow.timingWarpLibraryIndex) {
                const auto* entry = _state->session.warpLibrary.entry(
                    *activeRow.timingWarpLibraryIndex);
                if (entry) {
                    _state->timingWarpPlaybackActive = true;
                    _state->timingWarpPlaybackFromSong = true;
                    _state->timingWarpPlaybackCycleTicks
                        = std::max<uint32_t>(1u, entry->cycleTicks);
                    _state->timingWarpPlaybackStack = entry->stack;
                }
            }
        }
    }
    song.playbackRow = _state->songPlaybackRowValid ? std::optional<std::size_t>(_state->songPlaybackRow) : std::nullopt;
    song.pendingRow = pendingSongRow >= 0 ? std::optional<std::size_t>(pendingSongRow) : std::nullopt;
    song.pendingQuantization = static_cast<SongLaunchQuantization>(std::min<uint32_t>(pendingSongQuantization, 3u));
    // Queueing follows the REAPER clock, not the presence of an active Song
    // row. This keeps it available for looping/non-looping playback and after
    // a non-looping arrangement reaches its end while REAPER keeps running.
    song.playing = _state->playing;
    _state->sentEventCount = _plugin->sentEvents.load(
        std::memory_order_relaxed);
    _state->droppedEventCount = _plugin->droppedEvents.load(
        std::memory_order_relaxed);
    _state->status = _state->playing
        ? "REAPER PLAYING • sample-accurate CLAP MIDI"
        : "REAPER STOPPED • MIDI output ready";
    _state->lastEvent = std::to_string(_state->sentEventCount)
        + " MIDI EVENTS";
    platform.refreshPlayback();
}


} // namespace s3g::tracker
