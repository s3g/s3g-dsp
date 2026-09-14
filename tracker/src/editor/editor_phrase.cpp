#include "editor_geometry_support.h"
#include "s3g/tracker/editor_authoring.h"
#include "s3g/tracker/fx_catalog.h"
#include <locale>
#include <sstream>

namespace s3g::tracker::editor {
using namespace geometry_support;
uint8_t phraseGridFieldType(std::size_t field) {
  if (field == 0u)
    return 0u;
  if (field == 1u)
    return 1u;
  if (field == 6u)
    return 4u;
  return (field == 2u || field == 4u) ? 2u : 3u;
}

PhraseGridCell phraseGridCellAt(const PhraseDefinition &phrase,
                                std::size_t field, std::size_t row) {
  if (field == 0u)
    return row < phrase.notes.size() ? phrase.notes[row] : NoteCell::rest();
  if (field == 1u)
    return row < phrase.velocities.size() ? phrase.velocities[row]
                                          : ValueCell::defaultValue();
  if (field == 6u)
    return row < phrase.gates.size() ? phrase.gates[row]
                                     : GateCell::defaultValue();
  const auto pair = (field - 2u) / 2u;
  if ((field % 2u) == 0u)
    return row < phrase.fxPairs[pair].actions.size()
               ? phrase.fxPairs[pair].actions[row]
               : FxActionCell::empty();
  return row < phrase.fxPairs[pair].values.size()
             ? phrase.fxPairs[pair].values[row]
             : FxValueCell::previous();
}

void writePhraseGridCell(PhraseDefinition &phrase, std::size_t field,
                         std::size_t row, const PhraseGridCell &cell) {
  if (field == 0u) {
    if (phrase.notes.size() <= row)
      phrase.notes.resize(row + 1u, NoteCell::rest());
    phrase.notes[row] = std::get<NoteCell>(cell);
    return;
  }
  if (field == 1u) {
    if (phrase.velocities.size() <= row)
      phrase.velocities.resize(row + 1u, ValueCell::defaultValue());
    phrase.velocities[row] = std::get<ValueCell>(cell);
    return;
  }
  if (field == 6u) {
    if (phrase.gates.size() <= row)
      phrase.gates.resize(row + 1u, GateCell::defaultValue());
    phrase.gates[row] = std::get<GateCell>(cell);
    return;
  }
  auto &pair = phrase.fxPairs[(field - 2u) / 2u];
  if ((field % 2u) == 0u) {
    if (pair.actions.size() <= row)
      pair.actions.resize(row + 1u, FxActionCell::empty());
    pair.actions[row] = std::get<FxActionCell>(cell);
  } else {
    if (pair.values.size() <= row)
      pair.values.resize(row + 1u, FxValueCell::previous());
    pair.values[row] = std::get<FxValueCell>(cell);
  }
}

PhraseGridCell blankPhraseGridCell(std::size_t field) {
  if (field == 0u)
    return NoteCell::rest();
  if (field == 1u)
    return ValueCell::defaultValue();
  if (field == 6u)
    return GateCell::defaultValue();
  if (field == 2u || field == 4u)
    return FxActionCell::empty();
  return FxValueCell::previous();
}
namespace {
std::vector<std::string> split(const std::string &text, char sep) {
  std::vector<std::string> out;
  std::size_t at = 0;
  do {
    auto end = text.find(sep, at);
    out.push_back(text.substr(at, end == std::string::npos ? end : end - at));
    if (end == std::string::npos)
      break;
    at = end + 1;
  } while (true);
  return out;
}
bool number(const std::string &text, double &v) {
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  if (!(in >> v) || !std::isfinite(v))
    return false;
  in >> std::ws;
  return in.eof();
}
const BurstLibrary *bursts(const app::TrackerViewState &s, AssetBankId id) {
  if (id == s.activeBurstBankId)
    return &s.session.burstLibrary;
  const auto *b = findBurstBank(s.burstBanks, id);
  return b ? &b->library : nullptr;
}
} // namespace
std::string phraseGridCellText(const PhraseDefinition &p, std::size_t f,
                               std::size_t r) {
  auto cell = phraseGridCellAt(p, f, r);
  std::vector<std::string> values;
  if (f == 0) {
    const auto &c = std::get<NoteCell>(cell);
    switch (c.state) {
    case NoteCellState::Rest:
      return "---";
    case NoteCellState::RetriggerPrevious:
      return "RPT";
    case NoteCellState::Hold:
      return "HLD";
    case NoteCellState::Kill:
      return "KIL";
    case NoteCellState::Burst:
      return format("%s:B%02u", assetBankToken(c.burstBankId), c.note + 1u);
    default:
      break;
    }
    for (std::size_t i = 0; i < c.noteVoiceCount(); ++i)
      values.push_back(std::to_string(c.noteVoice(i)));
  } else if (f == 1) {
    const auto &c = std::get<ValueCell>(cell);
    if (c.state == ValueCellState::Default)
      return "DEF";
    if (c.state == ValueCellState::Previous)
      return "PRV";
    for (std::size_t i = 0; i < c.valueVoiceCount(); ++i)
      values.push_back(
          format("%.3f", double(std::clamp(c.valueVoice(i), 0.f, 1.f))));
  } else if (f == 6) {
    const auto &c = std::get<GateCell>(cell);
    if (!c.voiceCount)
      return "DEF";
    for (std::size_t i = 0; i < c.gateVoiceCount(); ++i) {
      auto g = c.gateVoice(i);
      values.push_back(g.mode == GateVoiceMode::Default
                           ? "DEF"
                           : g.mode == GateVoiceMode::Tie
                                 ? "TIE"
                                 : format("%.3g", double(g.rows)));
    }
  } else if (f == 2 || f == 4) {
    const auto &c = std::get<FxActionCell>(cell);
    if (c.state == FxActionCellState::Previous)
      return "PRV";
    if (c.state == FxActionCellState::MidiControlChange)
      return format("CC%u", c.midiController);
    if (c.state == FxActionCellState::Sequencer) {
      const auto *action = findSequencerAction(c.sequencerAction);
      return action ? std::string(action->mnemonic) : "???";
    }
    return "---";
  } else {
    const auto &c = std::get<FxValueCell>(cell);
    if (c.state == FxValueCellState::Previous)
      return "PRV";
    for (std::size_t i = 0; i < c.valueVoiceCount(); ++i)
      values.push_back(
          format("%.3f", double(std::clamp(c.valueVoice(i), 0.f, 1.f))));
  }
  return joined(values, "+");
}
bool applyPhraseCellText(std::string token, PhraseDefinition &p,
                         const app::TrackerViewState &s, std::size_t r,
                         std::size_t f) {
  if (r >= p.length || f >= 7)
    return false;
  token = uppercase(token);
  auto write = [&](const PhraseGridCell &c) {
    writePhraseGridCell(p, f, r, c);
    return true;
  };
  if (token.empty())
    return write(blankPhraseGridCell(f));
  if (f == 0) {
    if (token == "---")
      return write(NoteCell::rest());
    if (token == "RPT")
      return write(NoteCell::retriggerPrevious());
    if (token == "HLD")
      return write(NoteCell::hold());
    if (token == "KIL")
      return write(NoteCell::kill());
    std::size_t slot = 0;
    auto bank = s.activeBurstBankId;
    if (parseQualifiedBurstToken(token, bank, slot) ||
        parseBurstSlot(token, slot)) {
      auto *lib = bursts(s, bank);
      if (!lib || slot >= lib->bursts.size() || lib->bursts[slot].empty())
        return false;
      return write(NoteCell::withBurst(uint8_t(slot), bank));
    }
    auto parts = split(token, '+');
    if (parts.size() > kMaximumNoteVoices)
      return false;
    std::array<uint8_t, kMaximumNoteVoices> notes{};
    for (std::size_t i = 0; i < parts.size(); ++i)
      if (!parseMidiNote(parts[i], notes[i]))
        return false;
    std::sort(notes.begin(), notes.begin() + parts.size());
    if (std::adjacent_find(notes.begin(), notes.begin() + parts.size()) !=
        notes.begin() + parts.size())
      return false;
    return write(NoteCell::withNotes(notes, parts.size()));
  }
  if (f == 2 || f == 4) {
    if (token == "---")
      return write(FxActionCell::empty());
    if (token == "PRV")
      return write(FxActionCell::previous());
    uint8_t cc = 0;
    if (parseMidiControlChange(token, cc))
      return write(FxActionCell::midiControlChange(cc));
    if (auto *a = findSequencerAction(token))
      return write(FxActionCell::sequencer(a->action));
    return false;
  }
  if (f == 6) {
    if (token == "DEF")
      return write(GateCell::defaultValue());
    auto parts = split(token, '+');
    if (parts.size() > kMaximumNoteVoices)
      return false;
    std::array<GateVoice, kMaximumNoteVoices> gates{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
      if (parts[i] == "DEF")
        gates[i] = {GateVoiceMode::Default, 1.f};
      else if (parts[i] == "TIE" || parts[i] == "T")
        gates[i] = {GateVoiceMode::Tie, 1.f};
      else {
        double v = 0;
        if (!number(parts[i], v) || v < .01 || v > 64.)
          return false;
        gates[i] = {GateVoiceMode::Rows, float(v)};
      }
    }
    return write(GateCell::withVoices(gates, parts.size()));
  }
  if (f == 1 && token == "DEF")
    return write(ValueCell::defaultValue());
  if (token == "PRV")
    return f == 1 ? write(ValueCell::previous())
                  : write(FxValueCell::previous());
  auto parts = split(token, '+');
  if (parts.size() > kMaximumNoteVoices)
    return false;
  std::array<float, kMaximumNoteVoices> values{};
  for (std::size_t i = 0; i < parts.size(); ++i) {
    double v = 0;
    if (!number(parts[i], v))
      return false;
    if (v > 1)
      v /= 127.;
    if (v < 0 || v > 1)
      return false;
    values[i] = float(v);
  }
  return f == 1 ? write(ValueCell::withValues(values, parts.size()))
                : write(FxValueCell::withValues(values, parts.size()));
}
PhraseDefinition &PhraseEditor::phrase() {
  state.selectedPhrase =
      std::min(state.selectedPhrase, kPhraseLibrarySlots - 1);
  return state.phraseLibrary.phrases[state.selectedPhrase];
}
void PhraseEditor::reload() {
  auto &p = phrase();
  p.length = std::clamp(p.length, kMinimumPhraseRows, kMaximumPhraseRows);
  p.notes.resize(p.length, NoteCell::rest());
  p.velocities.resize(p.length, ValueCell::defaultValue());
  p.gates.resize(p.length, GateCell::defaultValue());
  for (auto &pair : p.fxPairs) {
    pair.actions.resize(p.length, FxActionCell::empty());
    pair.values.resize(p.length, FxValueCell::previous());
    pair.actionColumn.length = pair.valueColumn.length = p.length;
  }
  row = std::min(row, p.length - 1);
  field = std::min<std::size_t>(field, 6);
  selection.anchorRow = std::min(selection.anchorRow, p.length - 1);
  selection.focusRow = std::min(selection.focusRow, p.length - 1);
  selection.active = selection.active && !selection.isSingleCell();
}
void PhraseEditor::changed() {
  if (callbacks.patternChanged)
    callbacks.patternChanged();
}
void PhraseEditor::resetSelection() {
  selection = {};
  selection.anchorField = selection.focusField = field;
  selection.anchorRow = selection.focusRow = row;
}
void PhraseEditor::selectAll() {
  selection = {};
  selection.focusField = 6;
  selection.focusRow = phrase().length - 1;
  selection.active = true;
}
app::GridSelectionRange PhraseEditor::range() const {
  return selection.active
             ? selection.range()
             : app::GridSelectionRange{0, 0, 0, field, field, row, row};
}
void PhraseEditor::clear() {
  auto r = range();
  auto &p = phrase();
  for (auto y = r.firstRow; y <= r.lastRow; ++y)
    for (auto f = r.firstField; f <= r.lastField; ++f)
      writePhraseGridCell(p, f, y, blankPhraseGridCell(f));
  changed();
}
bool PhraseEditor::edit(std::string text) {
  if (!applyPhraseCellText(std::move(text), phrase(), state, row, field))
    return false;
  changed();
  return true;
}
std::string PhraseEditor::copy() {
  auto r = range();
  copied_.clear();
  types_.clear();
  std::string text;
  for (auto y = r.firstRow; y <= r.lastRow; ++y) {
    if (y != r.firstRow)
      text += '\n';
    for (auto f = r.firstField; f <= r.lastField; ++f) {
      if (f != r.firstField)
        text += '\t';
      copied_.push_back(phraseGridCellAt(phrase(), f, y));
      text += phraseGridCellText(phrase(), f, y);
    }
  }
  for (auto f = r.firstField; f <= r.lastField; ++f)
    types_.push_back(phraseGridFieldType(f));
  copiedRows_ = r.rowCount();
  copiedFields_ = r.fieldCount();
  return text;
}
bool PhraseEditor::paste(std::string text, bool same) {
  for (std::size_t at = 0; (at = text.find('\r', at)) != std::string::npos;
       ++at) {
    if (at + 1 < text.size() && text[at + 1] == '\n')
      text.erase(at, 1);
    else
      text[at] = '\n';
  }
  auto lines = split(text, '\n');
  while (lines.size() > 1 && lines.back().empty())
    lines.pop_back();
  std::vector<std::vector<std::string>> cells;
  std::size_t widest = 0;
  for (auto &line : lines) {
    cells.push_back(split(line, '\t'));
    widest = std::max(widest, cells.back().size());
  }
  auto r = range();
  bool fill = cells.size() == 1 && widest == 1 && selection.active;
  bool structured = same && copiedRows_ == cells.size() &&
                    copiedFields_ == widest && types_.size() == widest &&
                    copied_.size() == cells.size() * widest;
  auto candidate = phrase();
  if (!fill && (r.firstRow + cells.size() > candidate.length ||
                r.firstField + widest > 7))
    return false;
  const auto height = fill ? r.rowCount() : cells.size(),
             width = fill ? r.fieldCount() : widest;
  for (std::size_t y = 0; y < height; ++y)
    for (std::size_t x = 0; x < width; ++x) {
      auto sy = fill ? 0 : y, sx = fill ? 0 : x;
      if (sx >= cells[sy].size())
        continue;
      auto f = r.firstField + x, at = r.firstRow + y;
      if (structured) {
        if (types_[sx] != phraseGridFieldType(f))
          return false;
        writePhraseGridCell(candidate, f, at, copied_[sy * widest + sx]);
      } else if (!applyPhraseCellText(cells[sy][sx], candidate, state, at, f))
        return false;
    }
  phrase() = std::move(candidate);
  changed();
  reload();
  return true;
}
bool PhraseEditor::save(std::string name, std::string bpm) {
  if (name.size() > kMaximumPhraseNameBytes) {
    status = "PHRASE NAME TOO LONG";
    return false;
  }
  std::optional<double> tempo;
  if (bpm.find_first_not_of(" \t\r\n") != std::string::npos) {
    double v = 0;
    if (!number(bpm, v) || v < kMinimumPhraseRecommendedBpm ||
        v > kMaximumPhraseRecommendedBpm) {
      status = "BPM MUST BE 20–400 OR BLANK";
      return false;
    }
    tempo = v;
  }
  phrase().name = std::move(name);
  phrase().recommendedBpm = tempo;
  changed();
  return true;
}
bool PhraseEditor::duplicate() {
  auto source = phrase();
  if (source.empty() && source.name.empty() && !source.recommendedBpm)
    return false;
  auto &a = state.phraseLibrary.phrases;
  auto found = std::find_if(a.begin(), a.end(), [](const auto &p) {
    return p.empty() && p.name.empty() && !p.recommendedBpm;
  });
  if (found == a.end())
    return false;
  if (source.name.empty())
    source.name = "PHRASE COPY";
  else if (source.name.size() + 5 <= kMaximumPhraseNameBytes)
    source.name += " COPY";
  *found = std::move(source);
  state.selectedPhrase = std::size_t(found - a.begin());
  changed();
  reload();
  return true;
}
void PhraseEditor::erase() {
  phrase() = makeBlankPhrase(16);
  changed();
  reload();
}
void PhraseEditor::length(std::size_t n) {
  phrase().length = std::clamp(n, kMinimumPhraseRows, kMaximumPhraseRows);
  reload();
  changed();
}
bool PhraseEditor::capture(std::size_t t, std::size_t first, std::size_t last) {
  if (state.songPlaybackActive || t >= state.session.pattern.tracks.size())
    return false;
  if (!capturePhrase(state.session.pattern, t, first, last, phrase()))
    return false;
  if (phrase().name.empty())
    phrase().name = "Captured phrase";
  status = format("CAPTURED T%02lu · ROWS %03lu–%03lu",
                  static_cast<unsigned long>(t + 1),
                  static_cast<unsigned long>(first + 1),
                  static_cast<unsigned long>(last + 1));
  changed();
  reload();
  return true;
}
bool PhraseEditor::place(std::size_t t, std::size_t at, bool merge) {
  if (state.songPlaybackActive || t >= state.session.pattern.tracks.size())
    return false;
  auto candidate = state.session.pattern;
  if (!placePhrase(candidate, t, phrase(), at,
                   merge ? PhrasePlacementMode::MergeIntoEmpty
                         : PhrasePlacementMode::Replace))
    return false;
  candidate.visibleRows = std::max(candidate.visibleRows, at + phrase().length);
  state.session.pattern = std::move(candidate);
  state.lastPlacedPhrase = state.selectedPhrase;
  status = format("COPIED P%02lu TO T%02lu · ROW %03lu",
                  static_cast<unsigned long>(state.selectedPhrase + 1),
                  static_cast<unsigned long>(t + 1),
                  static_cast<unsigned long>(at + 1));
  changed();
  return true;
}
void PhraseEditor::action(std::string token) {
  if (field != 2 && field != 4)
    return;
  if (!applyPhraseCellText(token, phrase(), state, row, field))
    return;
  auto &pair = phrase().fxPairs[(field - 2) / 2];
  auto &c = pair.actions[row];
  if (c.state == FxActionCellState::Sequencer ||
      c.state == FxActionCellState::MidiControlChange) {
    if (pair.values[row].state == FxValueCellState::Previous)
      pair.values[row] = FxValueCell::withValue(
          c.state == FxActionCellState::Sequencer &&
                  c.sequencerAction == SequencerAction::Condition
              ? normalizedFromSequencerCondition(SequencerCondition::FirstOf2)
              : .5f);
    pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1);
  }
  pair.actionColumn.length = std::max(pair.actionColumn.length, row + 1);
  resetSelection();
  changed();
}
void PhraseEditor::condition(std::size_t index) {
  auto *c = sequencerCondition(index);
  if (!c || (field != 3 && field != 5))
    return;
  auto &pair = phrase().fxPairs[(field - 3) / 2];
  pair.values[row] =
      FxValueCell::withValue(normalizedFromSequencerCondition(c->condition));
  pair.valueColumn.length = std::max(pair.valueColumn.length, row + 1);
  resetSelection();
  changed();
}
double AuthoringAudition::rowSeconds() const {
  return 60. / (std::max(1., bpm) * std::clamp(ticksPerBeat, 1u, 96u));
}
AuthoringAudition phraseAudition(const app::TrackerViewState &s,
                                 const PhraseDefinition &p) {
  AuthoringAudition a;
  a.channel = std::clamp<uint8_t>(p.previewMidiChannel, 1, 16);
  a.bpm = s.hostBpm > 0 ? s.hostBpm : s.session.transport.bpm;
  a.ticksPerBeat = s.session.transport.ticksPerBeat;
  float velocity = 100.f / 127.f;
  for (std::size_t r = 0; r < p.length; ++r) {
    if (r < p.velocities.size() &&
        p.velocities[r].state == ValueCellState::Value)
      velocity = std::clamp(p.velocities[r].normalized, 0.f, 1.f);
    if (r >= p.notes.size())
      continue;
    auto &n = p.notes[r];
    if (n.state == NoteCellState::Burst) {
      auto *lib = bursts(s, n.burstBankId);
      if (!lib || n.note >= lib->bursts.size())
        continue;
      auto &b = lib->bursts[n.note];
      for (std::size_t i = 0; i < b.eventCount; ++i) {
        auto &e = b.events[i];
        a.events.push_back(
            {uint16_t(r), e.note, e.velocity, e.gatePercent, e.position});
      }
    } else if (n.state == NoteCellState::Note)
      for (std::size_t v = 0; v < n.noteVoiceCount(); ++v) {
        uint8_t gate = 70;
        if (r < p.gates.size()) {
          auto g = p.gates[r].gateVoice(v);
          if (g.mode == GateVoiceMode::Rows)
            gate = uint8_t(std::clamp(std::lround(g.rows * 100.f), 1l, 100l));
          else if (g.mode == GateVoiceMode::Tie)
            gate = 100;
        }
        a.events.push_back({uint16_t(r), n.noteVoice(v),
                            uint8_t(std::lround(velocity * 127.f)), gate});
      }
  }
  if (!a.events.empty()) {
    a.lastRow = p.length - 1;
  }
  return a;
}
} // namespace s3g::tracker::editor
