#include "s3g/tracker/editor_authoring.h"
#include "s3g/tracker/fx_catalog.h"
#include <cmath>
#include <iostream>
using namespace s3g::tracker;
using namespace s3g::tracker::editor;
int main() {
  int failures = 0, checks = 0, changed = 0, preview = 0, cleared = 0,
      variants = 0;
  auto check = [&](bool ok, const char *msg) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << msg << '\n';
    }
  };
  app::TrackerViewState s;
  app::WorkspaceCallbacks c;
  c.patternChanged = [&] { ++changed; };
  c.previewPattern = [&](const Pattern &) { ++preview; };
  c.clearPatternPreview = [&] { ++cleared; };
  c.createPatternVariant = [&](const Pattern &) { ++variants; };
  s.session.pattern.tracks.resize(2);
  s.session.pattern.visibleRows = 16;
  PhraseEditor p(s, c);
  check(p.phrase().length == 16, "default phrase");
  for (auto token : {"60+64+67", "C4", "RPT", "HLD", "KIL", "---"})
    check(p.edit(token), "note tokens");
  check(!p.edit("60+60") && !p.edit("128") && !p.edit("B01"),
        "invalid notes/burst");
  p.edit("60+64+67");
  p.field = 1;
  check(p.edit("0.123456+0.876543"), "poly velocity");
  auto precise = p.phrase().velocities[0].valueVoice(0);
  auto copied = p.copy();
  p.row = 1;
  check(p.paste(copied, true) &&
            p.phrase().velocities[1].valueVoice(0) == precise,
        "typed clipboard exact values");
  p.field = 0;
  check(!p.paste(copied, true), "clipboard field type guard");
  p.field = 6;
  p.row = 0;
  check(p.edit("TIE+0.5+DEF"), "poly gates");
  check(!p.edit("0") && !p.edit("nan"), "invalid gate");
  p.field = 2;
  p.action("CD");
  check(p.phrase().fxPairs[0].values[0].state == FxValueCellState::Value,
        "context menu seeds value");
  p.field = 3;
  p.condition(3);
  check(p.phrase().fxPairs[0].values[0].normalized ==
            normalizedFromSequencerCondition(sequencerCondition(3)->condition),
        "condition selection");
  p.field = 4;
  p.action("CC74");
  check(p.phrase().fxPairs[1].actions[0].midiController == 74, "CC menu");
  p.field = 0;
  p.row = 0;
  p.resetSelection();
  auto before = p.phrase();
  int publications = changed;
  check(!p.paste("61\t0.5\n62\tinvalid", false) &&
            p.phrase().notes[0].note == before.notes[0].note &&
            changed == publications,
        "failed multi-cell paste atomic");
  p.selection.focusRow = 3;
  p.selection.active = true;
  check(p.paste("62", false) && p.phrase().notes[3].note == 62,
        "fill selection");
  p.clear();
  check(p.phrase().notes[0].state == NoteCellState::Rest &&
            p.phrase().notes[3].state == NoteCellState::Rest,
        "clear selection");
  check(p.save("é phrase 日本", " 123.25 "), "UTF-8 metadata");
  publications = changed;
  check(!p.save("bad", "401") &&
            !p.save(std::string(kMaximumPhraseNameBytes + 1, 'X'), "") &&
            changed == publications,
        "metadata validation atomic");
  check(p.duplicate() && s.selectedPhrase == 1 &&
            p.phrase().name == "é phrase 日本 COPY",
        "duplicate metadata");
  p.length(64);
  check(p.phrase().notes.size() == 64 &&
            p.phrase().fxPairs[1].valueColumn.length == 64,
        "length all columns");
  p.erase();
  check(p.phrase().empty() && p.phrase().name.empty() &&
            !p.phrase().recommendedBpm,
        "delete reset");
  auto &t = s.session.pattern.tracks[0];
  t.notes.assign(16, NoteCell::rest());
  t.notes[2] = NoteCell::withNote(48);
  t.notes[4] = NoteCell::withNote(55);
  check(p.capture(0, 2, 5) && p.phrase().length == 4 &&
            p.phrase().notes[0].note == 48,
        "capture original rows");
  check(p.place(1, 7, false) && s.session.pattern.tracks[1].notes[7].note == 48,
        "place pattern");
  s.songPlaybackActive = true;
  check(!p.capture(0, 0, 3) && !p.place(0, 0, false),
        "song follow placement guards");
  s.songPlaybackActive = false;
  auto audition = phraseAudition(s, p.phrase());
  check(audition.events.size() == 2 && audition.firstRow == 0 &&
            audition.lastRow == 3,
        "phrase preview full duration");
  auto withRests = p.phrase();
  withRests.notes[0] = NoteCell{};
  withRests.length = 8;
  auto restPlan = phraseAudition(s, withRests);
  check(restPlan.firstRow == 0 && restPlan.lastRow == 7 &&
            !restPlan.events.empty() && restPlan.events.front().row > 0,
        "phrase audition retains leading and trailing rests");
#if defined(_WIN32)
  const auto initialAssemblyTarget = s.assembly.targetPatternId;
#endif
  AssembleEditor a(s, c);
#if defined(_WIN32)
  check(s.assembly.targetPatternId == initialAssemblyTarget,
        "opening Assemble preserves the stored target");
  {
    auto refreshState = s;
    refreshState.assembly.targetPatternId = "B02";
    refreshState.assembly.targetTrack = 7;
    const auto publicationsBeforeRefresh = changed;
    AssembleEditor refreshed(refreshState, c);
    refreshed.reload();
    check(refreshState.assembly.targetPatternId == "B02" &&
              refreshState.assembly.targetTrack == 7 &&
              changed == publicationsBeforeRefresh,
          "Assemble refresh preserves saved target and lane without publishing");
    check(refreshed.append() &&
              refreshState.assembly.targetPatternId == refreshState.patternBank.activePatternId &&
              refreshState.assembly.targetTrack == refreshState.session.pattern.tracks.size() - 1,
          "assembly edits resolve the current pattern and available lane");
    refreshState.assembly.targetTrack = 7;
    check(refreshed.place() && refreshState.session.selectedTrack == 1 &&
              refreshState.assembly.targetTrack == 1,
          "placement resolves a lane that disappeared after a pattern switch");
  }
#endif
  check(a.append() && a.rows() == 4, "append phrase");
  a.repeats = 2;
  check(a.append() && a.rows() == 12, "append repeats");
  a.selected = {1};
  a.up();
  check(s.assembly.blocks[0].repeats == 2 && a.selected.count(0), "move up");
  a.down();
  check(s.assembly.blocks[1].repeats == 2, "move down");
  a.duplicate();
  check(a.rows() == 20 && s.assembly.blocks.size() == 3, "duplicate block");
  a.selected = {0, 2};
  check(a.move(3, false) && a.selected == std::set<std::size_t>{1, 2},
        "multi-block stable reorder");
  check(a.blockAt(27, false) == -1 && a.blockAt(28, false) == 0 &&
            a.blockAt(29, true) == 0,
        "block hit geometry");
  auto plan = a.audition();
  check(plan.events.size() == 10 && plan.lastRow == 19,
        "assembly immutable preview");
  {
    auto longState = s;
    auto& phrase = longState.phraseLibrary.phrases[longState.selectedPhrase];
    phrase = makeBlankPhrase(64);
    phrase.notes[2] = NoteCell::withNote(60);
    longState.assembly.blocks.assign(64,
        {longState.activePhraseBankId, uint32_t(longState.selectedPhrase), 64});
    AssembleEditor longAssembly(longState, c);
    const auto longPlan = longAssembly.audition();
    check(longPlan.firstRow == 0 && longPlan.lastRow == 262143 &&
              longPlan.events.size() == 4096 &&
              longPlan.events.front().row == 2 &&
              longPlan.events.back().row == 262082,
          "assembly retains full rests, repeats and rows beyond 65535");
  }
  check(a.savePhrase() && p.phrase().length == 20 &&
            p.phrase().name.find("ASSEMBLY") == 0,
        "save flattened assembly");
  s.assembly.targetTrack = 1;
  s.assembly.targetRow = 250;
  s.assembly.fitMode = AssemblyFitMode::ExtendPattern;
  auto original = s.session.pattern.tracks[1].notes;
  publications = changed;
  check(!a.place() &&
            s.session.pattern.tracks[1].notes.size() == original.size() &&
            changed == publications,
        "extend overflow atomic");
  s.assembly.fitMode = AssemblyFitMode::Crop;
  check(a.place() && s.session.pattern.visibleRows == 256, "crop at256");
  s.assembly.fitMode = AssemblyFitMode::Wrap;
  check(a.place(), "wrap at256");
  s.songPlaybackActive = true;
  check(!a.place(), "assembly song guard");
  s.songPlaybackActive = false;
  a.clear();
  check(a.rows() == 0 && a.selected.empty(), "clear tray");
  ReshapeEditor r(s, c);
  check(r.settings.mutationAmount == .55f &&
            r.settings.microTimingWrite == PatternReshapeWriteMode::FillMissing,
        "native reshape defaults");
  r.toggleLane(1);
  check(r.settings.laneMask == 2 && r.laneTitle() == "L02 ONLY", "single lane");
  r.toggleLane(1);
  check(r.settings.laneMask == 2, "last included lane retained");
  r.toggleLane(0);
  check(r.settings.laneMask == 0xffffffffu, "all lanes normalized");
  r.togglePreview();
  check(preview == 1 && r.preview, "non-destructive preview callback");
  r.showingReshaped = false;
  r.syncPreview();
  check(cleared == 1 && r.preview, "original A/B preview");
  r.clearPreview();
  check(cleared == 2 && !r.preview, "clear own preview");
  auto seed = r.settings.mutationSeed;
  r.reseed();
  check(r.settings.mutationSeed != seed, "reseed deterministic sequence");
  r.togglePreview();
  s.patternBank.activePatternId = "external";
  r.reload();
  check(!r.preview, "pattern switch releases preview");
  s.songPlaybackActive = true;
  check(!r.apply() && !r.variant(), "reshape song guard");
  s.songPlaybackActive = false;
  if (r.result.changed()) {
    check(r.variant() && variants == 1, "create variant callback");
    check(r.apply(), "apply commit");
  }
  r.clearPreview();
  int reentrant = 0;
  c.previewPattern = [&](const Pattern &snapshot) {
    ++reentrant;
    auto count = snapshot.tracks.size();
    r.reload();
    check(snapshot.tracks.size() == count, "preview snapshot survives reload");
  };
  r.togglePreview();
  check(reentrant == 1, "host-error reload must not recursively publish");
  r.clearPreview();
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
