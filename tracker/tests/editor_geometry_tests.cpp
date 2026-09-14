#include "s3g/tracker/editor_geometry.h"
#include <cmath>
#include <iostream>
using namespace s3g::tracker;
using namespace s3g::tracker::editor;
namespace {
Point center(Rect r) { return {r.x + r.width * .5, r.y + r.height * .5}; }
bool equal(const BurstDefinition &a, const BurstDefinition &b) {
  if (a.name != b.name || a.eventCount != b.eventCount)
    return false;
  for (std::size_t i = 0; i < a.eventCount; ++i) {
    auto x = a.events[i], y = b.events[i];
    if (x.position != y.position || x.note != y.note ||
        x.velocity != y.velocity || x.gatePercent != y.gatePercent)
      return false;
  }
  return true;
}
} // namespace
int main() {
  int failures = 0, checks = 0, publications = 0, auditions = 0,
      timerStarts = 0, timerStops = 0, reveals = 0;
  auto check = [&](bool ok, const char *why) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << why << '\n';
    }
  };
  app::TrackerViewState state;
  state.session.pattern = state.patternBank.entries.front().pattern;
  state.session.pattern.tracks.resize(3);
  auto &track = state.session.pattern.tracks[0];
  track.noteColumn.length = 16;
  track.notes.assign(16, NoteCell::rest());
  for (std::size_t row = 0; row < 16; row += 4)
    track.notes[row] = NoteCell::withNote(static_cast<uint8_t>(48 + row));
  app::WorkspaceCallbacks callbacks;
  callbacks.patternChanged = [&] { ++publications; };
  callbacks.previewBurst = [&](const BurstDefinition &, uint8_t, double,
                               uint32_t) { ++auditions; };
  callbacks.showTrackerPage = [&] { ++reveals; };
  double now = 1, period = 0;
  GeometryServices services;
  services.monotonicTime = [&] { return now; };
  services.startAuditionTimer = [&](double seconds) {
    ++timerStarts;
    period = seconds;
  };
  services.stopAuditionTimer = [&] { ++timerStops; };
  GeometryEditor geometry(state, callbacks, false, services),
      burst(state, callbacks, true, services);
  geometry.paint();
  check(geometry.itemsForGeometryMenu(GeometryMenuView).size() == 7 &&
            burst.itemsForGeometryMenu(GeometryMenuView).empty(),
        "Geometry menu excludes embedded Burst mode");
  for (auto mode : {GeometryModeRingField, GeometryModeActivePulses,
                    GeometryModeAllStepsUnderlay, GeometryModePhaseSpokes,
                    GeometryModeLaneFocus, GeometryModeCompositeRing,
                    GeometryModePitchMap}) {
    geometry.viewModePopup.indexOfSelectedItem = mode;
    geometry.viewModeChanged(geometry.viewModePopup);
    auto list = geometry.paint();
    check(list.commands().size() > 60, "all seven Geometry modes draw");
    for (const auto &c : list.commands()) {
      check(std::isfinite(c.rect.x) && std::isfinite(c.rect.y) &&
                std::isfinite(c.baseline),
            "finite layout");
      for (auto p : c.points)
        check(std::isfinite(p.x) && std::isfinite(p.y), "finite path");
    }
  }
  geometry.viewModePopup.indexOfSelectedItem = 0;
  geometry.viewModeChanged(geometry.viewModePopup);
  const auto lengthBefore = state.session.pattern.tracks[0].noteColumn.length;
  auto r = geometry.lengthSliderTrack();
  auto p = Point{r.x + r.width * .4, r.y + r.height * .5};
  auto before = publications;
  geometry.pointerDown({p});
  geometry.pointerMove({{r.x + r.width * .7, p.y}}, true);
  check(publications == before, "length preview never publishes mid drag");
  geometry.cancelGesture();
  check(state.session.pattern.tracks[0].noteColumn.length == lengthBefore &&
            publications == before,
        "cancel restores length");
  geometry.pointerDown({p});
  geometry.pointerUp({p});
  check(publications == before + 1 &&
            state.session.pattern.tracks[0].noteColumn.length != lengthBefore,
        "length commit publishes once");
  const auto committed = state.session.pattern.tracks[0].noteColumn.length;
  geometry.pointerDown({{r.x + r.width * .9, p.y}});
  state.songPlaybackActive = true;
  geometry.pointerUp({p});
  state.songPlaybackActive = false;
  check(state.session.pattern.tracks[0].noteColumn.length == committed,
        "Song transition cancels in-flight edits");
  geometry.pointerDown({p});
  state.session.pattern.tracks[0].noteColumn.length = 19;
  geometry.reloadModel();
  geometry.pointerUp({p});
  check(state.session.pattern.tracks[0].noteColumn.length == 19,
        "reload never restores stale document snapshot");
  geometry.setGeometryZoomAndRedraw(99);
  check(geometry.geometryZoom == 1.8, "independent zoom maximum");
  geometry.setGeometryZoomAndRedraw(0);
  check(geometry.geometryZoom == .65, "independent zoom minimum");
  geometry.openGeometryMenu(GeometryMenuDirection);
  geometry.menuKey(GridKey::Down);
  geometry.menuKey(GridKey::None, true);
  check(state.session.pattern.tracks[0].noteColumn.direction ==
            Direction::Reverse,
        "direction menu keyboard");
  geometry.openGeometryMenu(GeometryMenuPitchScale);
  auto list = geometry.paint();
  int separators = 0;
  for (auto &c : list.commands())
    if (c.primitive == Primitive::Polyline && c.color.red == 0x3a &&
        c.points.size() == 2)
      ++separators;
  check(separators >=
            static_cast<int>(
                geometry.itemsForGeometryMenu(GeometryMenuPitchScale).size()) -
                4,
        "four-column menu separators");
  geometry.suspend();
  geometry.setGeometryZoomAndRedraw(1);
  auto laneRadius = geometry.ringRadiusForLane(0);
  const double rowAngle =
      -3.141592653589793 / 2 + 2. * 2 * 3.141592653589793 / 19;
  auto notePoint = geometry.geometryPointAtRadius(laneRadius, rowAngle);
  geometry.toolChanged(geometry.toolButtons[1]);
  before = publications;
  geometry.pointerDown({notePoint});
  geometry.pointerUp({notePoint});
  check(noteCellIsActivePulse(state.session.pattern.tracks[0].notes[2]) &&
            publications == before + 1,
        "ring paint commits a note");
  geometry.pointerDown({notePoint, 1, Alt});
  geometry.pointerUp({notePoint});
  check(state.session.pattern.tracks[0].notes[2].state == NoteCellState::Rest,
        "Option-paint erases");
  geometry.pointerDown({notePoint});
  geometry.pointerUp({notePoint});
  geometry.toolChanged(geometry.toolButtons[3]);
  geometry.pointerDown({notePoint});
  geometry.pointerMove(
      {geometry.geometryPointAtRadius(laneRadius - 20, rowAngle)}, true);
  geometry.pointerUp({notePoint});
  check(state.session.pattern.tracks[0].velocities.size() > 2 &&
            state.session.pattern.tracks[0].velocities[2].state ==
                ValueCellState::Value,
        "radial velocity writes tracker value");
  geometry.toolChanged(geometry.toolButtons[0]);
  geometry.pointerDown({notePoint, 2});
  geometry.pointerUp({notePoint});
  check(reveals == 1, "double-click bead reveals Tracker");
  auto click = [&](Rect box) {
    burst.pointerDown({center(box)});
    burst.pointerUp({center(box)});
  };
  click(burst.burstActionRectForRow(5, 0, 3));
  check(burst.selectedBurstSlot() == 0 &&
            state.session.burstLibrary.bursts[0].eventCount == 4,
        "new Burst default");
  check(burst.saveBurstName("\xc2\xa0 CAFE 日本 \xe3\x80\x80") &&
            state.session.burstLibrary.bursts[0].name == "CAFE 日本",
        "Unicode Burst name trimming");
  before = publications;
  check(!burst.saveBurstName(std::string(kMaximumBurstNameBytes + 1, 'X')) &&
            !burst.saveBurstName("\xff") && publications == before,
        "invalid Burst names atomic");
  check(burst.saveBurstName("") &&
            state.session.burstLibrary.bursts[0].name == "BURST B01",
        "empty name default");
  click(burst.burstActionRectForRow(3, 1, 2));
  check(state.session.burstLibrary.bursts[0].eventCount == 5, "add event");
  click(burst.burstActionRectForRow(3, 0, 2));
  check(state.session.burstLibrary.bursts[0].eventCount == 4, "remove event");
  click(burst.burstActionRectForRow(6, 1, 3));
  auto accelerated = state.session.burstLibrary.bursts[0];
  click(burst.burstActionRectForRow(6, 2, 3));
  check(!equal(accelerated, state.session.burstLibrary.bursts[0]),
        "accelerate/decelerate distinct");
  click(burst.burstActionRectForRow(6, 0, 3));
  // Matrix note drag and cancel, then a breakpoint drag edits both time and
  // velocity.
  auto snapshot = state.session.burstLibrary.bursts[0];
  auto cell = burst.burstMatrixCellRect(1, 1);
  before = publications;
  burst.pointerDown({center(cell)});
  burst.pointerMove({{cell.x + cell.width * .8, cell.y + 8}}, true);
  check(publications == before, "matrix preview does not publish");
  burst.cancelGesture();
  check(equal(snapshot, state.session.burstLibrary.bursts[0]),
        "matrix cancellation restores original Burst");
  auto graph = burst.burstBreakpointRect();
  const auto event = snapshot.events[1];
  Point marker{graph.x + 12 + event.position / 65535. * (graph.width - 24),
               graph.y + graph.height - 14 -
                   (event.velocity - 1) / 126. * (graph.height - 28)};
  check(burst.burstBreakpointEventAtPoint(marker) == 1,
        "breakpoint hit matches drawn marker");
  burst.pointerDown({marker});
  burst.pointerMove({{marker.x + 10, marker.y + 40}}, true);
  burst.pointerUp({marker});
  auto moved = state.session.burstLibrary.bursts[0].events[1];
  check(moved.position > event.position && moved.velocity < event.velocity &&
            publications == before + 1,
        "breakpoint edits time and velocity once");
  auto radial = burst.burstRadialPlotRect();
  auto rc = center(radial);
  double radius = std::max(50., std::min(radial.width, radial.height) * .34 *
                                    burst.geometryZoom);
  double angle =
      -3.141592653589793 / 2 + moved.position / 65536. * 2 * 3.141592653589793;
  radius += (moved.note / 127. - .5) * 30;
  Point bead{rc.x + std::cos(angle) * radius, rc.y + std::sin(angle) * radius};
  check(burst.burstEventAtPoint(bead) == 1, "radial hit matches drawn center");
  burst.pointerDown({bead});
  burst.pointerMove({bead}, true);
  burst.pointerUp({bead});
  check(state.session.burstLibrary.bursts[0].events[1].position ==
            moved.position,
        "radial click never jumps position");
  // Audition has a separate clock. Painting at any rate cannot generate MIDI.
  state.burstLoopPreview = true;
  state.hostBpm = 120;
  state.session.transport.ticksPerBeat = 4;
  burst.startBurstPreview();
  check(auditions == 1 && timerStarts == 1 && std::abs(period - .125) < 1e-9,
        "audition row timing");
  for (int i = 0; i < 120; ++i) {
    now += 1. / 120;
    burst.refreshPlaybackDisplay();
    burst.paint();
  }
  check(auditions == 1, "120 display frames do not emit MIDI");
  burst.auditionTick();
  check(auditions == 2, "only audition timer repeats MIDI");
  burst.selectBurstSlot(0);
  check(!burst.auditionActive() && timerStops == 1,
        "slot change stops audition");
  burst.startBurstPreview();
  burst.suspend();
  check(!burst.auditionActive() && timerStops == 2,
        "hidden page stops audition");
  burst.startBurstPreview();
  state.playing = true;
  burst.refreshPlaybackDisplay();
  check(!burst.auditionActive() && timerStops == 3,
        "transport stops local audition");
  state.playing = false;
  snapshot = state.session.burstLibrary.bursts[0];
  state.songPlaybackActive = true;
  before = publications;
  click(burst.burstActionRectForRow(3, 1, 2));
  burst.keyDown({{}, 1, 0, GridKey::Right, {}});
  check(equal(snapshot, state.session.burstLibrary.bursts[0]) &&
            before == publications,
        "Song-follow controls cannot mutate Burst");
  state.songPlaybackActive = false;
  int imports = 0, exports = 0, libraryExports = 0, copies = 0, purges = 0,
      deletes = 0;
  callbacks.importAssetPack = [&] { ++imports; };
  callbacks.exportBurstAssetPack = [&](std::size_t) { ++exports; };
  callbacks.exportBurstLibraryAssetPack = [&] { ++libraryExports; };
  callbacks.copyBurstToProject = [&](std::size_t slot) {
    ++copies;
    return slot;
  };
  callbacks.deleteUnusedBursts = [&] { ++purges; };
  callbacks.deleteBurstBank = [&] { ++deletes; };
  for (uint32_t row : {8u, 9u, 10u})
    for (std::size_t i = 0; i < 2; ++i)
      click(burst.burstActionRectForRow(row, i, 2));
  check(imports == 1 && exports == 1 && libraryExports == 1 && copies == 1 &&
            purges == 1 && deletes == 1,
        "all Burst library operations wired");
  // Pitch proposals are nondestructive; each original contour remains
  // available.
  auto &pt = state.session.pattern.tracks[0];
  pt.noteColumn.length = 16;
  pt.notes.assign(16, NoteCell::rest());
  pt.notes[0] = NoteCell::withNotes({48, 55, 60}, 3);
  pt.notes[4] = NoteCell::withNote(52);
  pt.notes[8] = NoteCell::hold();
  pt.notes[12] = NoteCell::withNote(59);
  geometry.openPitchMapFirstRow(0, 15);
  before = publications;
  for (int contour = 0; contour < 7; ++contour) {
    geometry.openGeometryMenu(GeometryMenuPitchContour);
    geometry.applyGeometryMenuSelection(contour);
    geometry.refreshPitchMapPreview();
    check(!geometry.pitchPreview().assignments.empty() &&
              publications == before,
          "all contours preview without publication");
  }
  geometry.applyCurrentPitchMap();
  check(state.session.pattern.tracks[0].notes[8].state == NoteCellState::Hold &&
            state.session.pattern.tracks[0].notes[1].state ==
                NoteCellState::Rest &&
            state.session.pattern.tracks[0].notes[0].noteVoiceCount() == 3,
        "Pitch Map preserves non-note cells and polyphony");
  for (int fps : {30, 60, 120}) {
    now = 10;
    GeometryEditor timed(state, callbacks, false, services);
    state.playing = true;
    state.noteHits[0] = true;
    timed.refreshPlaybackDisplay();
    state.noteHits[0] = false;
    for (int i = 0; i < fps; ++i) {
      now = 10 + .14 * (i + 1) / fps;
      timed.refreshPlaybackDisplay();
    }
    check(std::abs(timed.halo(0) - .5) < 1e-8,
          "140 ms half-life independent of FPS");
    now += 2;
    timed.refreshPlaybackDisplay();
    check(timed.halo(0) == 0, "stalled UI cannot leave stale MIDI halo");
  }
  state.playing = false;
  burst.selectBurstSlot(0);
  state.session.pattern.tracks[0].notes[0] =
      NoteCell::withBurst(0, kProjectAssetBankId);
  state.notePlayheads[0] = 0;
  state.noteHits.fill(false);
  auto noCursor = burst.paint().commands().size();
  state.playing = true;
  state.subrowPlaybackPhase = .4f;
  auto withCursor = burst.paint().commands().size();
  state.session.pattern.tracks[0].notes[0].burstBankId = 42;
  auto foreignCursor = burst.paint().commands().size();
  check(withCursor > noCursor && foreignCursor == noCursor,
        "Burst cursors require matching bank and slot");
  state.playing = false;
  burst.startBurstPreview();
  click(burst.burstActionRectForRow(5, 0, 3));
  check(!burst.auditionActive(), "NEW stops previous slot's audition");
  burst.saveBurstName(std::string(kMaximumBurstNameBytes, 'X'));
  click(burst.burstActionRectForRow(5, 1, 3));
  check(state.session.burstLibrary.bursts[burst.selectedBurstSlot()]
                .name.size() <= kMaximumBurstNameBytes,
        "duplicate honors name byte limit");
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
