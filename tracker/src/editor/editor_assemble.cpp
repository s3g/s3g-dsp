#include "editor_geometry_support.h"
#include "s3g/tracker/editor_authoring.h"
#include "s3g/tracker/fx_catalog.h"

namespace s3g::tracker::editor {
using app::TrackerViewState;
using namespace geometry_support;
namespace {
bool noteIsEmpty(const NoteCell &cell) {
  return cell.state == NoteCellState::Rest;
}
bool valueIsEmpty(const ValueCell &cell) {
  return cell.state == ValueCellState::Default ||
         cell.state == ValueCellState::Previous;
}
bool actionIsEmpty(const FxActionCell &cell) {
  return cell.state == FxActionCellState::Empty ||
         cell.state == FxActionCellState::Previous;
}
bool fxValueIsEmpty(const FxValueCell &cell) {
  return cell.state == FxValueCellState::Previous;
}
bool gateIsEmpty(const GateCell &cell) { return cell.voiceCount == 0u; }

void copyPhraseRow(Track &track, const PhraseDefinition &phrase,
                   std::size_t source, std::size_t destination, bool merge) {
  const auto size = destination + 1u;
  track.notes.resize(std::max(track.notes.size(), size), NoteCell::rest());
  track.velocities.resize(std::max(track.velocities.size(), size),
                          ValueCell::defaultValue());
  track.gates.resize(std::max(track.gates.size(), size),
                     GateCell::defaultValue());
  if (source < phrase.notes.size() &&
      (!merge || noteIsEmpty(track.notes[destination])))
    track.notes[destination] = phrase.notes[source];
  if (source < phrase.velocities.size() &&
      (!merge || valueIsEmpty(track.velocities[destination])))
    track.velocities[destination] = phrase.velocities[source];
  if (source < phrase.gates.size() &&
      (!merge || gateIsEmpty(track.gates[destination])))
    track.gates[destination] = phrase.gates[source];
  for (std::size_t pairIndex = 0u; pairIndex < kFxPairCount; ++pairIndex) {
    auto &destinationPair = track.fxPairs[pairIndex];
    const auto &sourcePair = phrase.fxPairs[pairIndex];
    destinationPair.actions.resize(
        std::max(destinationPair.actions.size(), size), FxActionCell::empty());
    destinationPair.values.resize(std::max(destinationPair.values.size(), size),
                                  FxValueCell::previous());
    if (source < sourcePair.actions.size() &&
        (!merge || actionIsEmpty(destinationPair.actions[destination])))
      destinationPair.actions[destination] = sourcePair.actions[source];
    if (source < sourcePair.values.size() &&
        (!merge || fxValueIsEmpty(destinationPair.values[destination])))
      destinationPair.values[destination] = sourcePair.values[source];
  }
}

uint16_t previewPosition(const PhraseDefinition &phrase, std::size_t row,
                         std::size_t voice) {
  for (const auto &pair : phrase.fxPairs) {
    if (row >= pair.actions.size() || row >= pair.values.size())
      continue;
    const auto &action = pair.actions[row];
    const auto &value = pair.values[row];
    if (action.state == FxActionCellState::Sequencer &&
        action.sequencerAction == SequencerAction::MicroTime &&
        value.state == FxValueCellState::Value) {
      const float mt = std::clamp(value.valueVoice(voice), 0.0f, 1.0f);
      return static_cast<uint16_t>(std::lround(mt * 65535.0f));
    }
  }
  return 0u;
}

void appendPhrasePreviewEvents(const TrackerViewState *state,
                               const PhraseDefinition &phrase,
                               std::size_t rowOffset,
                               std::vector<PitchPreviewEvent> &events) {
  if (!state)
    return;
  float velocity = 100.0f / 127.0f;
  for (std::size_t row = 0u; row < phrase.length; ++row) {
    if (row < phrase.velocities.size() &&
        phrase.velocities[row].state == ValueCellState::Value)
      velocity = std::clamp(phrase.velocities[row].normalized, 0.0f, 1.0f);
    if (row >= phrase.notes.size())
      continue;
    const auto &note = phrase.notes[row];
    if (note.state == NoteCellState::Burst) {
      const auto *bank =
          state ? findBurstBank(state->burstBanks, note.burstBankId) : nullptr;
      const auto *library = note.burstBankId == state->activeBurstBankId
                                ? &state->session.burstLibrary
                                : bank ? &bank->library : nullptr;
      if (!library || note.note >= library->bursts.size())
        continue;
      const auto &burst = library->bursts[note.note];
      for (std::size_t index = 0u; index < burst.eventCount; ++index) {
        const auto &event = burst.events[index];
        events.push_back({static_cast<uint32_t>(rowOffset + row), event.note,
                          event.velocity, event.gatePercent, event.position});
      }
    } else if (note.state == NoteCellState::Note) {
      for (std::size_t voice = 0u; voice < note.noteVoiceCount(); ++voice) {
        uint8_t gate = 70u;
        if (row < phrase.gates.size()) {
          const auto value = phrase.gates[row].gateVoice(voice);
          if (value.mode == GateVoiceMode::Rows)
            gate = static_cast<uint8_t>(
                std::clamp(std::lround(value.rows * 100.0f), 1l, 100l));
          else if (value.mode == GateVoiceMode::Tie)
            gate = 100u;
        }
        const float voiceVelocity =
            row < phrase.velocities.size() &&
                    phrase.velocities[row].state == ValueCellState::Value
                ? phrase.velocities[row].valueVoice(voice)
                : velocity;
        events.push_back(
            {static_cast<uint32_t>(rowOffset + row), note.noteVoice(voice),
             static_cast<uint8_t>(
                 std::lround(std::clamp(voiceVelocity, 0.0f, 1.0f) * 127.0f)),
             gate, previewPosition(phrase, row, voice)});
      }
    }
  }
}

} // namespace
const PhraseDefinition *assemblyPhrase(const TrackerViewState &s,
                                       const PhraseAssemblyBlock &b) {
  const auto *bank = findPhraseBank(s.phraseBanks, b.phraseBankId);
  const auto *lib = b.phraseBankId == s.activePhraseBankId
                        ? &s.phraseLibrary
                        : bank ? &bank->library : nullptr;
  if (!lib || b.phraseSlot >= lib->phrases.size())
    return nullptr;
  auto &p = lib->phrases[b.phraseSlot];
  return p.empty() && p.name.empty() && !p.recommendedBpm ? nullptr : &p;
}
std::size_t assemblyBlockRows(const TrackerViewState &s,
                              const PhraseAssemblyBlock &b) {
  auto *p = assemblyPhrase(s, b);
  return p ? p->length * std::clamp(b.repeats, 1u, kMaximumAssemblyBlockRepeats)
           : 0;
}
double assemblyBlockHeight(const TrackerViewState &s,
                           const PhraseAssemblyBlock &b) {
  return std::max(22., double(assemblyBlockRows(s, b)) * 6.);
}
std::size_t AssembleEditor::rows() const {
  std::size_t n = 0;
  for (auto &b : state.assembly.blocks)
    n += assemblyBlockRows(state, b);
  return n;
}
double AssembleEditor::contentHeight() const {
  double y = 40;
  for (auto &b : state.assembly.blocks)
    y += assemblyBlockHeight(state, b);
  return y;
}
double AssembleEditor::pixelYForRow(std::size_t row) const {
  double y = 28;
  std::size_t first = 0;
  for (auto &b : state.assembly.blocks) {
    auto n = assemblyBlockRows(state, b);
    if (row < first + n)
      return y + double(row - first) * 6;
    first += n;
    y += assemblyBlockHeight(state, b);
  }
  return y;
}
int AssembleEditor::blockAt(double point, bool insertion) const {
  if (point < 28)
    return insertion ? 0 : -1;
  double y = 28;
  for (std::size_t i = 0; i < state.assembly.blocks.size(); ++i) {
    double h = assemblyBlockHeight(state, state.assembly.blocks[i]);
    if (insertion ? point < y + h * .5 : point >= y && point < y + h)
      return int(i);
    y += h;
  }
  return insertion ? int(state.assembly.blocks.size()) : -1;
}
void AssembleEditor::reload() {
  auto &a = state.assembly;
  a.targetPatternId = state.patternBank.activePatternId;
  if (!state.session.pattern.tracks.empty())
    a.targetTrack = std::min(a.targetTrack,
                             uint32_t(state.session.pattern.tracks.size() - 1));
  selected.erase(selected.lower_bound(a.blocks.size()), selected.end());
}
void AssembleEditor::changed() {
  if (callbacks.patternChanged)
    callbacks.patternChanged();
  reload();
}
bool AssembleEditor::append() {
  auto &a = state.assembly;
  if (a.blocks.size() >= kMaximumAssemblyBlocks)
    return false;
  PhraseAssemblyBlock b{state.activePhraseBankId,
                        uint32_t(state.selectedPhrase),
                        std::clamp(repeats, 1u, kMaximumAssemblyBlockRepeats)};
  if (!assemblyPhrase(state, b) || rows() + assemblyBlockRows(state, b) > 256)
    return false;
  a.blocks.push_back(b);
  selected = {a.blocks.size() - 1};
  changed();
  return true;
}
bool AssembleEditor::move(std::size_t at, bool copy) {
  reload();
  if (selected.empty())
    return false;
  auto &a = state.assembly.blocks;
  std::vector<PhraseAssemblyBlock> moving;
  for (auto i : selected)
    moving.push_back(a[i]);
  if (copy && a.size() + moving.size() > kMaximumAssemblyBlocks)
    return false;
  if (!copy) {
    auto before =
        std::size_t(std::distance(selected.begin(), selected.lower_bound(at)));
    for (auto i = selected.rbegin(); i != selected.rend(); ++i)
      a.erase(a.begin() + std::ptrdiff_t(*i));
    at -= std::min(at, before);
  }
  at = std::min(at, a.size());
  a.insert(a.begin() + std::ptrdiff_t(at), moving.begin(), moving.end());
  selected.clear();
  for (std::size_t i = 0; i < moving.size(); ++i)
    selected.insert(at + i);
  changed();
  return true;
}
void AssembleEditor::up() {
  if (!selected.empty() && *selected.begin() > 0)
    move(*selected.begin() - 1, false);
}
void AssembleEditor::down() {
  if (!selected.empty() &&
      *selected.rbegin() + 1 < state.assembly.blocks.size())
    move(*selected.rbegin() + 2, false);
}
void AssembleEditor::duplicate() {
  if (!selected.empty())
    move(*selected.rbegin() + 1, true);
}
void AssembleEditor::erase() {
  auto &a = state.assembly.blocks;
  for (auto i = selected.rbegin(); i != selected.rend(); ++i)
    if (*i < a.size())
      a.erase(a.begin() + std::ptrdiff_t(*i));
  selected.clear();
  changed();
}
void AssembleEditor::clear() {
  state.assembly.blocks.clear();
  selected.clear();
  changed();
}
bool AssembleEditor::place() {
  auto &a = state.assembly;
  if (state.songPlaybackActive ||
      a.targetTrack >= state.session.pattern.tracks.size() || a.blocks.empty())
    return false;
  auto candidate = state.session.pattern;
  auto &track = candidate.tracks[a.targetTrack];
  bool merge = a.placementMode == AssemblyPlacementMode::MergeIntoEmpty;
  std::size_t sequence = 0, maxWritten = candidate.visibleRows;
  for (auto &block : a.blocks) {
    auto *p = assemblyPhrase(state, block);
    if (!p)
      continue;
    for (uint32_t repeat = 0;
         repeat < std::clamp(block.repeats, 1u, kMaximumAssemblyBlockRepeats);
         ++repeat)
      for (std::size_t row = 0; row < p->length; ++row, ++sequence) {
        std::size_t destination = a.targetRow + sequence;
        if (destination >= 256) {
          if (a.fitMode == AssemblyFitMode::Crop)
            goto placed;
          if (a.fitMode == AssemblyFitMode::Wrap)
            destination %= 256;
          else {
            status = "ASSEMBLY EXCEEDS ROW 256";
            return false;
          }
        }
        copyPhraseRow(track, *p, row, destination, merge);
        maxWritten = std::max(maxWritten, destination + 1);
      }
  }
placed:
  candidate.visibleRows = std::min<std::size_t>(256, maxWritten);
  track.noteColumn.length =
      std::max(track.noteColumn.length, candidate.visibleRows);
  track.velocityColumn.length =
      std::max(track.velocityColumn.length, candidate.visibleRows);
  track.gateColumn.length =
      std::max(track.gateColumn.length, candidate.visibleRows);
  for (auto &pair : track.fxPairs) {
    pair.actionColumn.length =
        std::max(pair.actionColumn.length, candidate.visibleRows);
    pair.valueColumn.length =
        std::max(pair.valueColumn.length, candidate.visibleRows);
  }
  state.session.pattern = std::move(candidate);
  state.session.selectedTrack = a.targetTrack;
  state.session.selectedRow = a.targetRow;
  status = format("PLACED %lu ROWS IN L%02u",
                  static_cast<unsigned long>(sequence), a.targetTrack + 1);
  changed();
  return true;
}
void AssembleEditor::reveal() {
  state.session.selectedTrack = state.assembly.targetTrack;
  state.session.selectedRow = state.assembly.targetRow;
  if (callbacks.showTrackerPage)
    callbacks.showTrackerPage();
}
bool AssembleEditor::savePhrase() {
  const auto n = rows();
  if (n < kMinimumPhraseRows || n > kMaximumPhraseRows)
    return false;
  auto *bank = findPhraseBank(state.phraseBanks, kProjectAssetBankId);
  auto *lib = state.activePhraseBankId == kProjectAssetBankId
                  ? &state.phraseLibrary
                  : bank ? &bank->library : nullptr;
  if (!lib)
    return false;
  auto found =
      std::find_if(lib->phrases.begin(), lib->phrases.end(), [](const auto &p) {
        return p.empty() && p.name.empty() && !p.recommendedBpm;
      });
  if (found == lib->phrases.end())
    return false;
  Track track;
  std::size_t at = 0;
  for (auto &b : state.assembly.blocks) {
    auto *p = assemblyPhrase(state, b);
    if (!p)
      continue;
    for (uint32_t repeat = 0;
         repeat < std::clamp(b.repeats, 1u, kMaximumAssemblyBlockRepeats);
         ++repeat)
      for (std::size_t row = 0; row < p->length; ++row)
        copyPhraseRow(track, *p, row, at++, false);
  }
  auto result = makeBlankPhrase(n);
  if (!capturePhrase(track, 0, n - 1, result))
    return false;
  auto slot = std::size_t(found - lib->phrases.begin());
  result.name = "ASSEMBLY " + std::to_string(slot + 1);
  result.previewMidiChannel = state.assembly.previewMidiChannel;
  *found = std::move(result);
  if (state.activePhraseBankId != kProjectAssetBankId) {
    // Preserve edited assets before switching the active library mirror.
    if (auto *old = findPhraseBank(state.phraseBanks, state.activePhraseBankId))
      old->library = state.phraseLibrary;
    state.phraseLibrary = *lib;
  }
  state.activePhraseBankId = kProjectAssetBankId;
  state.selectedPhrase = slot;
  status = "SAVED TO PROJECT PHRASES";
  changed();
  return true;
}
AuthoringAudition AssembleEditor::audition() const {
  AuthoringAudition a;
  a.channel = state.assembly.previewMidiChannel;
  a.bpm = state.hostBpm > 0 ? state.hostBpm : state.session.transport.bpm;
  a.ticksPerBeat = state.session.transport.ticksPerBeat;
  std::size_t offset = 0;
  if (!wholePreview) {
    if (auto *p = assemblyPhrase(state, {state.activePhraseBankId,
                                         uint32_t(state.selectedPhrase), 1})) {
      appendPhrasePreviewEvents(&state, *p, 0, a.events);
      offset = p->length;
    }
  } else
    for (auto &b : state.assembly.blocks) {
      auto *p = assemblyPhrase(state, b);
      if (!p)
        continue;
      for (uint32_t repeat = 0;
           repeat < std::clamp(b.repeats, 1u, kMaximumAssemblyBlockRepeats);
           ++repeat) {
        appendPhrasePreviewEvents(&state, *p, offset, a.events);
        offset += p->length;
      }
    }
  if (!a.events.empty()) {
    a.lastRow = offset - 1;
  }
  return a;
}
} // namespace s3g::tracker::editor
