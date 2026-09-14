#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/editor_shell.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace s3g::tracker;
using namespace s3g::tracker::editor;
int main() {
  int checks = 0, failures = 0;
  auto check = [&](bool ok, const char *label) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << label << '\n';
    }
  };
  ShellController shell;
  check(shell.selected() == ShellPage::Tracker, "initial page");
  check(shell.adjacent(false) == ShellPage::Help, "previous wraps");
  check(!shell.select(ShellPage::Count) &&
            shell.selected() == ShellPage::Tracker,
        "invalid page rejected");
  for (std::size_t i = 0; i < kShellPageCount; ++i) {
    auto p = ShellPage(i);
    check(shell.select(p), "select page");
    check(shell.canDetach(p) == (i >= 2), "native detach eligibility");
    check(shell.setDetached(p, true) == (i >= 2), "detach state confirmation");
    check(shell.detached(p) == (i >= 2), "detached independent of selection");
    check(shell.title(p)[0] && shell.windowTitle(p)[0], "native titles");
  }
  shell.select(ShellPage::Help);
  check(shell.adjacent(true) == ShellPage::Tracker, "next wraps");
  shell.resetDetached();
  check(!shell.detached(ShellPage::Help), "close resets windows");
  shell.select(ShellPage::Tracker);
  auto l = shell.layout(1320, 860);
  check(l.tabs[0].x == 12 && l.tabs[9].x == 634 && l.content.y == 40 &&
            l.content.height == 820,
        "native shell geometry");
  check(l.events.x + l.events.width < l.bpm.x && l.eventsVisible,
        "status separation");
  shell.select(ShellPage::Phrases);
  auto detached = shell.layout(1320, 860);
  check(detached.bpm.x == l.bpm.x - 44, "detach control reserves BPM space");
  check(!shell.layout(760, 620).eventsVisible, "narrow status hidden");
  shell.setHostBpm(97.5);
  check(shell.bpmText() == "HOST BPM  97.50", "tempo formatting");
  shell.setHostBpm(std::numeric_limits<double>::quiet_NaN());
  check(shell.bpmText() == "HOST BPM  —", "invalid tempo safe");
  shell.setEventText("");
  check(shell.eventText() == "0 MIDI EVENTS", "empty event fallback");
  for (double ratio : {.25, .5, 2. / 3, 1., 1.5, 2., 4.})
    check(normalizedTempoScale(ratio) == ratio, "rate preserved");
  check(normalizedTempoScale(INFINITY) == 1, "invalid rate fallback");

  app::TrackerViewState state;
  (void)CommandEngine::execute(state.session, "kit superior basic");
  state.session.pattern.name = "Original";
  state.tempoScale = 1.5;
  state.trackerRowJump = 3;
  state.showMidiNoteValues = false;
  state.phraseLibrary.phrases[0].name = "A phrase";
  SongArrangement song;
  song.name = "Persistent song";
  song.loop = true;
  SongRow row;
  row.patternId = state.patternBank.activePatternId;
  row.durationTicks = 16;
  row.bpm = 89.;
  song.rows.push_back(row);
  ClapDocumentController controller(state);
  auto original = controller.snapshot(song);
  check(original.patternBank.findEntry(original.patternBank.activePatternId)
                ->pattern.name == "Original",
        "snapshot synchronizes active pattern");
  check(original.phraseBanks[0].library.phrases[0].name == "A phrase",
        "snapshot synchronizes assets");
  check(!original.song.rows[0].bpm && original.song.loop,
        "host owns tempo; song preserved");
  check(original.session.trackerRowJump == 3 &&
            !original.session.showMidiNoteValues &&
            original.session.tempoScale == 1.5,
        "persistent view settings");
  for (auto &e : original.patternBank.entries)
    for (auto &track : e.pattern.tracks)
      check(track.destination == EventDestination::Midi &&
                track.initialInstrumentNodeId == midiOutNodeForRackSlot(0),
            "MIDI-only routing normalized");
  check(controller.resetHistory(original).ok() && !state.canUndo &&
            !state.canRedo,
        "reset history");
  state.session.pattern.name = "Changed";
  state.phraseLibrary.phrases[0].name = "Changed phrase";
  auto changed = controller.snapshot(song);
  check(controller.recordHistory(changed).ok() && state.canUndo &&
            !state.canRedo,
        "record history");
  ProjectDocument restored;
  check(controller.undo(restored).ok() && !state.canUndo && state.canRedo,
        "undo state");
  state.playing = true;
  state.session.selectedRow = 999;
  state.session.selectedTrack = 999;
  auto applied = controller.apply(restored);
  check(state.session.pattern.name == "Original" &&
            state.phraseLibrary.phrases[0].name == "A phrase",
        "apply restores document not drafts");
  check(state.playing &&
            state.session.selectedRow < state.session.pattern.visibleRows &&
            state.session.selectedTrack < state.session.pattern.tracks.size(),
        "transient transport retained; cursor clamped");
  check(applied.song.name == song.name && applied.song.rows.size() == 1,
        "adapter receives Song after pattern catalog");
  check(controller.redo(restored).ok() && state.canUndo && !state.canRedo,
        "redo state");
  controller.apply(restored);
  check(state.session.pattern.name == "Changed", "redo document");
  std::string json;
  check(encodeProjectDocument(controller.snapshot(song), json).ok(),
        "shared snapshot encodes");
  ProjectDocument decoded;
  check(decodeProjectDocument(json, decoded).ok(), "shared snapshot roundtrip");
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
