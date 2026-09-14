#include "s3g/tracker/clap_document_controller.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace s3g::tracker {
using app::TrackerViewState;

double normalizedTempoScale(double value) noexcept {
  constexpr std::array<double, 7u> choices{
      0.25, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0,
  };
  if (!std::isfinite(value))
    return 1.0;
  double best = 1.0;
  double distance = std::numeric_limits<double>::infinity();
  for (const double choice : choices) {
    const double candidate = std::abs(value - choice);
    if (candidate < distance) {
      distance = candidate;
      best = choice;
    }
  }
  return best;
}

bool syncSessionToActivePattern(TrackerViewState &state) {
  auto *entry = state.patternBank.findEntry(state.patternBank.activePatternId);
  if (!entry)
    return false;
  entry->pattern = state.session.pattern;
  entry->laneDefaultNotes = state.session.laneDefaultNotes;
  entry->aliases = state.session.aliases;
  return true;
}

bool loadActivePatternIntoSession(TrackerViewState &state) {
  const auto *entry =
      state.patternBank.findEntry(state.patternBank.activePatternId);
  if (!entry)
    return false;
  state.session.pattern = entry->pattern;
  state.session.laneDefaultNotes = entry->laneDefaultNotes;
  state.session.aliases = entry->aliases;
  state.session.selectedTrack =
      std::min<std::size_t>(state.session.selectedTrack,
                            state.session.pattern.tracks.empty()
                                ? 0u
                                : state.session.pattern.tracks.size() - 1u);
  state.session.selectedRow = std::min<std::size_t>(
      state.session.selectedRow,
      std::max<std::size_t>(state.session.pattern.visibleRows, 1u) - 1u);
  return true;
}

bool syncActiveAssetBanks(TrackerViewState &state) {
  auto *burst =
      s3g::tracker::findBurstBank(state.burstBanks, state.activeBurstBankId);
  auto *phrase =
      s3g::tracker::findPhraseBank(state.phraseBanks, state.activePhraseBankId);
  if (!burst || !phrase)
    return false;
  burst->library = state.session.burstLibrary;
  phrase->library = state.phraseLibrary;
  state.session.activeBurstBankId = state.activeBurstBankId;
  return true;
}

bool loadActiveAssetBanks(TrackerViewState &state) {
  const auto *burst =
      s3g::tracker::findBurstBank(state.burstBanks, state.activeBurstBankId);
  const auto *phrase =
      s3g::tracker::findPhraseBank(state.phraseBanks, state.activePhraseBankId);
  if (!burst || !phrase)
    return false;
  state.session.burstLibrary = burst->library;
  state.session.activeBurstBankId = burst->id;
  state.phraseLibrary = phrase->library;
  state.selectedPhrase = std::min<std::size_t>(
      state.selectedPhrase, state.phraseLibrary.phrases.size() - 1u);
  return true;
}

void normalizeMidiOnlyDocument(ProjectDocument &document) {
  document.session.tempoScale =
      normalizedTempoScale(document.session.tempoScale);
  auto rack = s3g::tracker::makeDefaultInstrumentRack();
  for (auto &instrument : rack.instruments)
    instrument = s3g::tracker::RackInstrument{};
  const auto midiNode = s3g::tracker::midiOutNodeForRackSlot(0u);
  if (const auto *definition = s3g::tracker::defaultRackInstrument(midiNode)) {
    rack.instruments[0u] = *definition;
  }
  rack.midiRoutes[0u].kind =
      s3g::tracker::MidiInstrumentRouteKind::VirtualSource;
  rack.midiRoutes[0u].destinationId = 0;
  rack.midiRoutes[0u].virtualSource = 1u;
  rack.midiRoutes[0u].channel = 1u;
  rack.selectedNode = midiNode;
  document.instrumentRack = rack;
  for (auto &row : document.song.rows)
    row.bpm.reset();

  for (auto &entry : document.patternBank.entries) {
    for (std::size_t lane = 0u; lane < entry.pattern.tracks.size(); ++lane) {
      auto &track = entry.pattern.tracks[lane];
      track.initialInstrumentNodeId = midiNode;
      track.destination = EventDestination::Midi;
      track.midiChannel =
          static_cast<uint8_t>(std::clamp<int>(track.midiChannel, 1, 16));
      // Routing is owned by the lane's channel header. Older bus and INS
      // assignments collapse onto the plug-in's single CLAP note port.
      std::fill(track.instruments.begin(), track.instruments.end(),
                s3g::tracker::InstrumentCell::empty());
      for (auto &pair : track.fxPairs) {
        for (auto &action : pair.actions) {
          if (action.state == s3g::tracker::FxActionCellState::Parameter) {
            action = s3g::tracker::FxActionCell::empty();
          }
        }
      }
    }
  }
}

ProjectDocument ClapDocumentController::snapshot(const SongArrangement &song) {
  ProjectDocument document;
  (void)syncSessionToActivePattern(state_);
  (void)syncActiveAssetBanks(state_);
  document.patternBank = state_.patternBank;
  document.burstBanks = state_.burstBanks;
  document.phraseBanks = state_.phraseBanks;
  document.transport = state_.session.transport;
  document.warpLibrary = state_.session.warpLibrary;
  document.session.gateMilliseconds = state_.session.gateMilliseconds;
  document.session.tempoScale = state_.tempoScale;
  document.session.songPlaybackEnabled = state_.songPlaybackEnabled;
  document.session.showMidiNoteValues = state_.showMidiNoteValues;
  document.session.trackerRowJump = state_.trackerRowJump;
  document.session.trackerFollow = state_.trackerFollow;
  document.session.commandRngState = state_.session.commandRngState;
  document.session.playbackSeed = state_.session.playbackSeed;
  document.session.activeBurstBankId = state_.activeBurstBankId;
  document.session.activePhraseBankId = state_.activePhraseBankId;
  document.session.assembly = state_.assembly;
  document.instrumentRack = state_.instrumentRack;
  document.song = song;
  normalizeMidiOnlyDocument(document);
  return document;
}

ProjectDocument ClapDocumentController::apply(const ProjectDocument &document) {
  ProjectDocument midiDocument = document;
  normalizeMidiOnlyDocument(midiDocument);
  state_.patternBank = midiDocument.patternBank;
  state_.burstBanks = midiDocument.burstBanks;
  state_.phraseBanks = midiDocument.phraseBanks;
  state_.activeBurstBankId = midiDocument.session.activeBurstBankId;
  state_.activePhraseBankId = midiDocument.session.activePhraseBankId;
  state_.assembly = midiDocument.session.assembly;
  (void)loadActiveAssetBanks(state_);
  (void)loadActivePatternIntoSession(state_);
  state_.session.transport = midiDocument.transport;
  state_.session.warpLibrary = midiDocument.warpLibrary;
  state_.session.gateMilliseconds = midiDocument.session.gateMilliseconds;
  state_.tempoScale = midiDocument.session.tempoScale;
  state_.session.commandRngState = midiDocument.session.commandRngState;
  state_.session.playbackSeed = midiDocument.session.playbackSeed;
  state_.instrumentRack = midiDocument.instrumentRack;
  state_.selectedRackInstrument = state_.instrumentRack.selectedNode;
  state_.songPlaybackEnabled = midiDocument.session.songPlaybackEnabled;
  state_.showMidiNoteValues = midiDocument.session.showMidiNoteValues;
  state_.trackerRowJump = midiDocument.session.trackerRowJump;
  state_.trackerFollow = midiDocument.session.trackerFollow;
  ++state_.trackerFollowRevision;
  return midiDocument;
}

void ClapDocumentController::updateHistoryAvailability() {
  state_.canUndo = history_.canUndo();
  state_.canRedo = history_.canRedo();
}
ProjectResult
ClapDocumentController::resetHistory(const ProjectDocument &document) {
  auto normalized = document;
  normalizeMidiOnlyDocument(normalized);
  auto result = history_.reset(normalized);
  updateHistoryAvailability();
  return result;
}
ProjectResult
ClapDocumentController::recordHistory(const ProjectDocument &document) {
  auto result = history_.record(document);
  updateHistoryAvailability();
  return result;
}
ProjectResult ClapDocumentController::undo(ProjectDocument &document) {
  auto result = history_.undo(document);
  updateHistoryAvailability();
  return result;
}
ProjectResult ClapDocumentController::redo(ProjectDocument &document) {
  auto result = history_.redo(document);
  updateHistoryAvailability();
  return result;
}
} // namespace s3g::tracker
