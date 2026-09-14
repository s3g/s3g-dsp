#include "editor_geometry_support.h"
#include "s3g/tracker/editor_geometry.h"
#include <random>

namespace s3g::tracker::editor {
using namespace geometry_support;

std::string GeometryEditor::displayedPatternId() {
  return nsString(geometryPatternId(trackerState));
}

std::size_t GeometryEditor::displayedLaneCount() {
  return static_cast<std::size_t>(
      geometryLanes(geometryPattern(trackerState)).count);
}

std::size_t GeometryEditor::displayedMutedLaneCount() {
  const auto *pattern = geometryPattern(trackerState);
  const auto lanes = geometryLanes(pattern);
  std::size_t muted = 0u;
  for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal)
    muted += geometryLaneMuted(trackerState, pattern, lanes.indices[ordinal])
                 ? 1u
                 : 0u;
  return static_cast<std::size_t>(muted);
}

int GeometryEditor::directionPopupIndex(Direction direction) {
  switch (direction) {
  case Direction::Reverse:
    return 1;
  case Direction::Palindrome:
    return 2;
  case Direction::Random:
    return 3;
  case Direction::Forward:
  default:
    return 0;
  }
}

Rect GeometryEditor::sourceRectForGeometryMenu(GeometryMenu menu) {
  if (menu == GeometryMenuPitchScope)
    return pitchScopeMenuBoxRect();
  if (menu == GeometryMenuPitchRoot)
    return pitchRootMenuBoxRect();
  if (menu == GeometryMenuPitchScale)
    return pitchScaleMenuBoxRect();
  if (menu == GeometryMenuPitchContour)
    return pitchContourMenuBoxRect();
  if (menu == GeometryMenuPitchLeap)
    return pitchLeapMenuBoxRect();
  if (menu == GeometryMenuBurstSlot)
    return burstSlotMenuBoxRect();
  if (menu == GeometryMenuBurstBank)
    return burstBankMenuBoxRect();
  if (menu == GeometryMenuBurstEvent)
    return burstEventMenuBoxRect();
  if (menu == GeometryMenuBurstPreviewChannel)
    return burstPreviewChannelMenuBoxRect();
  if (menu == GeometryMenuLane)
    return laneMenuBoxRect();
  if (menu == GeometryMenuDirection)
    return directionMenuBoxRect();
  if (menu == GeometryMenuMorphTarget)
    return morphTargetMenuBoxRect();
  return viewMenuBoxRect();
}

Rect GeometryEditor::dropdownRectForGeometryMenu(GeometryMenu menu) {
  constexpr double itemHeight = 21.0;
  const Rect source = sourceRectForGeometryMenu(menu);
  const std::size_t itemCount = itemsForGeometryMenu(menu).size();
  const uint32_t columns = menu == GeometryMenuPitchScale
                               ? 4u
                               : menu == GeometryMenuBurstSlot ? 2u : 1u;
  const double height =
      itemHeight * static_cast<double>((itemCount + columns - 1u) / columns);
  const double width =
      columns > 1u
          ? std::min<double>(menu == GeometryMenuPitchScale ? 760.0 : 600.0,
                             rWidth(bounds_) - 16.0)
          : rWidth(source);
  double y = rMaxY(source) + 2.0;
  if (y + height > rHeight(bounds_) - 8.0)
    y = rMinY(source) - height - 2.0;
  y = std::clamp<double>(
      y, 8.0, std::max<double>(8.0, rHeight(bounds_) - height - 8.0));
  double x = columns > 1u ? std::max<double>(8.0, rMaxX(source) - width)
                          : rMinX(source);
  return makeRect(x, y, width, height);
}

void GeometryEditor::openGeometryMenu(GeometryMenu menu) {
  if (menu == GeometryMenuView && burstLibraryOnly)
    return;
  syncToolboxControls();
  _openGeometryMenu = _openGeometryMenu == menu ? GeometryMenuNone : menu;
  _geometryMenuHoverIndex = -1;
  syncBurstNameControls();
  invalidate();
}

void GeometryEditor::selectBurstSlot(std::size_t slot) {
  cancelGesture();
  _openGeometryMenu = GeometryMenuNone;
  stopBurstPreview();
  _selectedBurstSlot = std::min<std::size_t>(slot, kBurstDefinitionCount - 1u);
  _selectedBurstEvent = 0u;
  geometryViewMode = GeometryModeBurst;
  (viewModePopup.indexOfSelectedItem = GeometryModeBurst);
  syncBurstNameControls();
  invalidate();
}

void GeometryEditor::pitchMapRowsFirst(std::size_t *first, std::size_t *last) {
  auto *model = trackerState;
  if (!model || model->session.pattern.tracks.empty()) {
    if (first)
      *first = 0u;
    if (last)
      *last = 0u;
    return;
  }
  const auto lane = std::min(model->session.selectedTrack,
                             model->session.pattern.tracks.size() - 1u);
  const auto length = std::clamp<std::size_t>(
      model->session.pattern.tracks[lane].noteColumn.length, 1u, 256u);
  const std::size_t resolvedFirst =
      _pitchUseFullCycle ? 0u : std::min(_pitchFirstRow, length - 1u);
  const std::size_t resolvedLast =
      _pitchUseFullCycle
          ? length - 1u
          : std::clamp(_pitchLastRow, resolvedFirst, length - 1u);
  if (first)
    *first = resolvedFirst;
  if (last)
    *last = resolvedLast;
}

void GeometryEditor::refreshPitchMapPreview() {
  auto *model = trackerState;
  if (!model || model->session.pattern.tracks.empty()) {
    _pitchPreview = {};
    _pitchAnalysis = {};
    return;
  }
  const auto lane = std::min(model->session.selectedTrack,
                             model->session.pattern.tracks.size() - 1u);
  std::size_t first = 0u;
  std::size_t last = 0u;
  pitchMapRowsFirst(&first, &last);
  _pitchAnalysis =
      s3g::tracker::analyzePitchMap(model->session.pattern, lane, first, last);
  _pitchPreview = s3g::tracker::previewPitchMap(model->session.pattern, lane,
                                                first, last, _pitchSettings);
  _pitchPreview.changed = 0u;
  for (auto &assignment : _pitchPreview.assignments) {
    if (assignment.row < _pitchOverrides.size() &&
        _pitchOverrides[assignment.row] >= 0)
      assignment.note = static_cast<uint8_t>(_pitchOverrides[assignment.row]);
    s3g::tracker::retargetPitchMapVoicing(assignment, _pitchSettings);
    bool changed = false;
    for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice)
      changed |= assignment.notes[voice] != assignment.originalNotes[voice];
    if (changed)
      ++_pitchPreview.changed;
  }
}

void GeometryEditor::freezePitchPreviewForManualEditing() {
  refreshPitchMapPreview();
  const bool generated = _pitchSettings.contour != PitchContour::Manual ||
                         _pitchSettings.transposeSemitones != 0 ||
                         _pitchSettings.invertScaleDegrees ||
                         _pitchSettings.reversePitchOrder;
  if (!generated)
    return;
  const auto frozen = _pitchPreview.assignments;
  _pitchSettings.contour = PitchContour::Manual;
  _pitchSettings.transposeSemitones = 0;
  _pitchSettings.invertScaleDegrees = false;
  _pitchSettings.reversePitchOrder = false;
  _pitchOverrides.fill(-1);
  for (const auto &assignment : frozen) {
    if (assignment.row < _pitchOverrides.size())
      _pitchOverrides[assignment.row] = assignment.note;
  }
  refreshPitchMapPreview();
  _pitchStatus = "GENERATED CONTOUR FROZEN FOR MANUAL EDIT";
}

void GeometryEditor::analyzePitchMap() {
  refreshPitchMapPreview();
  if (_pitchAnalysis.noteCount == 0u) {
    _pitchStatus = "NO EXPLICIT NOTE PITCHES";
    error();
    invalidate();
    return;
  }
  _pitchSettings.rootPitchClass = _pitchAnalysis.rootPitchClass;
  _pitchSettings.scale = _pitchAnalysis.scale;
  _pitchSettings.minimumNote = static_cast<uint8_t>(
      _pitchAnalysis.minimumNote > 12u ? _pitchAnalysis.minimumNote - 12u : 0u);
  _pitchSettings.maximumNote = static_cast<uint8_t>(std::min<uint32_t>(
      127u, static_cast<uint32_t>(_pitchAnalysis.maximumNote) + 12u));
  _pitchOverrides.fill(-1);
  const int percent =
      static_cast<int>(std::lround(_pitchAnalysis.confidence * 100.0f));
  _pitchStatus =
      format("ANALYZED · %ld%% CONFIDENCE", static_cast<long>(percent));
  refreshPitchMapPreview();
  invalidate();
}

void GeometryEditor::openPitchMapFirstRow(std::size_t firstRow,
                                          std::size_t lastRow) {
  cancelGesture();
  stopBurstPreview();
  _openGeometryMenu = GeometryMenuNone;
  auto *model = trackerState;
  if (!model || model->session.pattern.tracks.empty())
    return;
  const auto lane = std::min(model->session.selectedTrack,
                             model->session.pattern.tracks.size() - 1u);
  const auto length = std::clamp<std::size_t>(
      model->session.pattern.tracks[lane].noteColumn.length, 1u, 256u);
  _pitchFirstRow = std::min(firstRow, length - 1u);
  _pitchLastRow = std::clamp(lastRow, _pitchFirstRow, length - 1u);
  _pitchUseFullCycle = _pitchFirstRow == 0u && _pitchLastRow + 1u == length;
  _pitchOverrides.fill(-1);
  geometryViewMode = GeometryModePitchMap;
  (viewModePopup.indexOfSelectedItem = GeometryModePitchMap);
  analyzePitchMap();
  syncBurstNameControls();
  invalidate();
  invalidate();
}

void GeometryEditor::applyCurrentPitchMap() {
  if (!canEditDisplayedPattern() || !trackerState)
    return;
  refreshPitchMapPreview();
  auto &pattern = trackerState->session.pattern;
  if (pattern.tracks.empty())
    return;
  const auto lane =
      std::min(trackerState->session.selectedTrack, pattern.tracks.size() - 1u);
  auto &notes = pattern.tracks[lane].notes;
  std::size_t changed = 0u;
  for (const auto &assignment : _pitchPreview.assignments) {
    if (assignment.row >= notes.size() ||
        notes[assignment.row].state != NoteCellState::Note)
      continue;
    const auto &cell = notes[assignment.row];
    bool rowChanged = cell.noteVoiceCount() != assignment.voiceCount;
    for (std::size_t voice = 0u; voice < assignment.voiceCount && !rowChanged;
         ++voice)
      rowChanged = cell.noteVoice(voice) != assignment.notes[voice];
    if (!rowChanged)
      continue;
    notes[assignment.row] =
        NoteCell::withNotes(assignment.notes, assignment.voiceCount);
    ++changed;
  }
  _pitchSettings.contour = PitchContour::Manual;
  _pitchSettings.transposeSemitones = 0;
  _pitchSettings.invertScaleDegrees = false;
  _pitchSettings.reversePitchOrder = false;
  _pitchOverrides.fill(-1);
  if (changed > 0u) {
    _pitchStatus =
        format("APPLIED · %lu NOTES", static_cast<unsigned long>(changed));
    publish();
  } else {
    _pitchStatus = "NO PITCH CHANGES";
    invalidate();
  }
  refreshPitchMapPreview();
}

void GeometryEditor::applyPitchMapContour(PitchContour contour,
                                          std::size_t firstRow,
                                          std::size_t lastRow) {
  if (!canEditDisplayedPattern() || !trackerState ||
      trackerState->session.pattern.tracks.empty())
    return;
  const auto lane = std::min(trackerState->session.selectedTrack,
                             trackerState->session.pattern.tracks.size() - 1u);
  PitchMapSettings settings = _pitchSettings;
  settings.contour = contour;
  const auto evidence = s3g::tracker::analyzePitchMap(
      trackerState->session.pattern, lane, firstRow, lastRow);
  if (contour == PitchContour::Fit) {
    settings.minimumNote = 0u;
    settings.maximumNote = 127u;
  } else if (evidence.noteCount > 0u) {
    settings.minimumNote = static_cast<uint8_t>(
        evidence.minimumNote > 12u ? evidence.minimumNote - 12u : 0u);
    settings.maximumNote = static_cast<uint8_t>(std::min<uint32_t>(
        127u, static_cast<uint32_t>(evidence.maximumNote) + 12u));
  }
  const auto changed = s3g::tracker::applyPitchMap(
      trackerState->session.pattern, lane, firstRow, lastRow, settings);
  if (changed > 0u)
    publish();
}

void GeometryEditor::initializeBurstAtSlot(std::size_t slot) {
  if (!trackerState || slot >= kBurstDefinitionCount)
    return;
  auto &burst = trackerState->session.burstLibrary.bursts[slot];
  burst = {};
  burst.name = "BURST " + burstSlotToken(slot);
  burst.eventCount = 4u;
  uint8_t note = 60u;
  if (!trackerState->session.pattern.tracks.empty())
    note = laneDefaultNote(
        trackerState->session,
        std::min(trackerState->session.selectedTrack,
                 trackerState->session.pattern.tracks.size() - 1u));
  for (std::size_t index = 0u; index < burst.eventCount; ++index)
    burst.events[index] = {
        static_cast<uint16_t>(index * 65536u / burst.eventCount),
        note,
        127u,
        70u,
    };
}

std::size_t GeometryEditor::firstEmptyBurstSlot() {
  const auto &bursts = trackerState->session.burstLibrary.bursts;
  const auto found =
      std::find_if(bursts.begin(), bursts.end(),
                   [](const BurstDefinition &burst) { return burst.empty(); });
  return found == bursts.end()
             ? bursts.size()
             : static_cast<std::size_t>(found - bursts.begin());
}

bool GeometryEditor::emitBurstPreview() {
  if (!trackerState || trackerState->playing || !callbacks_ ||
      !callbacks_->previewBurst)
    return false;
  const auto &burst =
      trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  if (burst.empty())
    return false;
  const double projectBpm = trackerState->hostBpm > 0.0
                                ? trackerState->hostBpm
                                : trackerState->session.transport.bpm;
  callbacks_->previewBurst(
      burst,
      std::clamp<uint8_t>(trackerState->burstPreviewMidiChannel, 1u, 16u),
      projectBpm, trackerState->session.transport.ticksPerBeat);
  return true;
}

bool GeometryEditor::handleBurstToolboxClickAtPoint(Point point) {
  if (rContains(point, burstBankMenuBoxRect())) {
    openGeometryMenu(GeometryMenuBurstBank);
    return true;
  }
  if (rContains(point, burstSlotMenuBoxRect())) {
    openGeometryMenu(GeometryMenuBurstSlot);
    return true;
  }
  if (rContains(point, burstEventMenuBoxRect())) {
    const auto &burst =
        trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
    if (!burst.empty())
      openGeometryMenu(GeometryMenuBurstEvent);
    return true;
  }
  if (rContains(point, burstPreviewChannelMenuBoxRect())) {
    openGeometryMenu(GeometryMenuBurstPreviewChannel);
    return true;
  }
  auto &burst = trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  bool changed = false;
  if (rContains(point, burstPreviewHeaderButtonRect())) {
    if (burstPreviewTimer)
      stopBurstPreview();
    else
      startBurstPreview();
    return true;
  }
  if (rContains(point, burstLoopHeaderButtonRect())) {
    trackerState->burstLoopPreview = !trackerState->burstLoopPreview;
    if (!trackerState->burstLoopPreview)
      stopBurstPreview();
    else
      invalidate();
    return true;
  }
  if (rContains(point, fitBurstGatesHeaderButtonRect())) {
    if (burst.empty() || !canEditDisplayedPattern())
      return true;
    const auto previous = burst.events;
    fitBurstGatesToRow(burst);
    changed = !std::equal(previous.begin(), previous.begin() + burst.eventCount,
                          burst.events.begin(),
                          [](const BurstEvent &left, const BurstEvent &right) {
                            return left.gatePercent == right.gatePercent;
                          });
    if (changed)
      publish();
    invalidate();
    return true;
  }
  // Match the disabled controls while following a sounding Song pattern.
  if (!canEditDisplayedPattern())
    return rContains(point, inspectorRect());
  for (std::size_t index = 0u; index < 2u; ++index) {
    if (!rContains(point, burstActionRectForRow(3u, index, 2u)))
      continue;
    if (index == 0u && burst.eventCount > 1u) {
      const auto remove =
          std::min<std::size_t>(_selectedBurstEvent, burst.eventCount - 1u);
      std::move(burst.events.begin() + static_cast<std::ptrdiff_t>(remove + 1u),
                burst.events.begin() +
                    static_cast<std::ptrdiff_t>(burst.eventCount),
                burst.events.begin() + static_cast<std::ptrdiff_t>(remove));
      --burst.eventCount;
      _selectedBurstEvent =
          std::min<std::size_t>(_selectedBurstEvent, burst.eventCount - 1u);
      changed = true;
    } else if (index == 1u && !burst.empty() &&
               burst.eventCount < kMaximumBurstEvents) {
      auto &event = burst.events[burst.eventCount];
      event = burst.events[burst.eventCount - 1u];
      ++burst.eventCount;
      _selectedBurstEvent = burst.eventCount - 1u;
      changed = true;
    }
    if (changed) {
      setGeometryBurstTiming(burst, "even");
      publish();
    }
    invalidate();
    return true;
  }
  for (std::size_t index = 0u; index < 3u; ++index) {
    if (!rContains(point, burstActionRectForRow(5u, index, 3u)))
      continue;
    stopBurstPreview();
    if (index == 0u) {
      const auto slot = firstEmptyBurstSlot();
      if (slot < trackerState->session.burstLibrary.bursts.size()) {
        _selectedBurstSlot = slot;
        _selectedBurstEvent = 0u;
        initializeBurstAtSlot(slot);
        changed = true;
      }
    } else if (index == 1u && !burst.empty()) {
      const auto slot = firstEmptyBurstSlot();
      if (slot < trackerState->session.burstLibrary.bursts.size()) {
        trackerState->session.burstLibrary.bursts[slot] = burst;
        auto &copyName = trackerState->session.burstLibrary.bursts[slot].name;
        if (copyName.size() + 5u <= kMaximumBurstNameBytes)
          copyName += " COPY";
        _selectedBurstSlot = slot;
        _selectedBurstEvent = 0u;
        changed = true;
      }
    } else if (index == 2u && !burst.empty() &&
               projectBurstUsageCount(*trackerState, _selectedBurstSlot) ==
                   0u) {
      burst = {};
      _selectedBurstEvent = 0u;
      changed = true;
    }
    if (changed)
      publish();
    invalidate();
    return true;
  }
  for (std::size_t index = 0u; index < 2u; ++index) {
    if (!rContains(point, burstActionRectForRow(8u, index, 2u)))
      continue;
    if (index == 0u && callbacks_ && callbacks_->importAssetPack)
      callbacks_->importAssetPack();
    else if (index == 1u && !burst.empty() && callbacks_ &&
             callbacks_->exportBurstAssetPack)
      callbacks_->exportBurstAssetPack(_selectedBurstSlot);
    return true;
  }
  for (std::size_t index = 0u; index < 2u; ++index) {
    if (!rContains(point, burstActionRectForRow(9u, index, 2u)))
      continue;
    if (index == 0u && callbacks_ && callbacks_->exportBurstLibraryAssetPack)
      callbacks_->exportBurstLibraryAssetPack();
    else if (index == 1u && !burst.empty() && callbacks_ &&
             callbacks_->copyBurstToProject) {
      stopBurstPreview();
      const auto destination =
          callbacks_->copyBurstToProject(_selectedBurstSlot);
      if (destination < kBurstDefinitionCount) {
        _selectedBurstSlot = destination;
        _selectedBurstEvent = 0u;
      } else {
        error();
      }
    }
    invalidate();
    return true;
  }
  for (std::size_t index = 0u; index < 2u; ++index) {
    if (!rContains(point, burstActionRectForRow(10u, index, 2u)))
      continue;
    if (index == 0u && callbacks_ && callbacks_->deleteUnusedBursts)
      callbacks_->deleteUnusedBursts();
    else if (index == 1u && callbacks_ && callbacks_->deleteBurstBank)
      callbacks_->deleteBurstBank();
    return true;
  }
  if (burst.empty())
    return false;
  for (std::size_t index = 0u; index < 3u; ++index) {
    if (!rContains(point, burstActionRectForRow(6u, index, 3u)))
      continue;
    const auto shape =
        index == 0u ? "even" : index == 1u ? "accelerate" : "decelerate";
    setGeometryBurstTiming(burst, shape);
    publish();
    invalidate();
    return true;
  }
  for (std::size_t index = 0u; index < 3u; ++index) {
    if (!rContains(point, burstActionRectForRow(7u, index, 3u)))
      continue;
    if (index == 0u) {
      std::reverse(burst.events.begin(),
                   burst.events.begin() + burst.eventCount);
    } else {
      const auto count = static_cast<std::size_t>(burst.eventCount);
      const auto amount = index == 1u ? count - 1u : 1u;
      std::rotate(burst.events.begin(),
                  burst.events.begin() + static_cast<std::ptrdiff_t>(amount),
                  burst.events.begin() + static_cast<std::ptrdiff_t>(count));
    }
    setGeometryBurstTiming(burst, "even");
    publish();
    invalidate();
    return true;
  }
  if (rContains(point, revealHeaderButtonRect())) {
    if (trackerState->session.pattern.tracks.empty())
      return true;
    auto &session = trackerState->session;
    const auto lane =
        std::min(session.selectedTrack, session.pattern.tracks.size() - 1u);
    auto &track = session.pattern.tracks[lane];
    const auto row = session.selectedRow;
    if (track.notes.size() <= row)
      track.notes.resize(row + 1u, NoteCell::rest());
    track.notes[row] =
        NoteCell::withBurst(static_cast<uint8_t>(_selectedBurstSlot),
                            trackerState->activeBurstBankId);
    track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
    publish();
    pulseBurstPlaceFeedback();
    return true;
  }
  return false;
}

bool GeometryEditor::handleToolboxClickAtPoint(Point point) {
  if (_openGeometryMenu != GeometryMenuNone) {
    const auto menu = _openGeometryMenu;
    const std::vector<std::string> items = itemsForGeometryMenu(menu);
    const uint32_t columns = menu == GeometryMenuPitchScale
                                 ? 4u
                                 : menu == GeometryMenuBurstSlot ? 2u : 1u;
    const int hit =
        columns > 1u
            ? multiColumnDropdownHitIndex(
                  point, dropdownRectForGeometryMenu(menu), 21.0,
                  static_cast<uint32_t>(items.size()), columns)
            : dropdownHitIndex(point, dropdownRectForGeometryMenu(menu), 21.0,
                               static_cast<uint32_t>(items.size()));
    if (hit >= 0) {
      applyGeometryMenuSelection(hit);
      _openGeometryMenu = GeometryMenuNone;
      _geometryMenuHoverIndex = -1;
      invalidate();
      return true;
    }
    _openGeometryMenu = GeometryMenuNone;
    _geometryMenuHoverIndex = -1;
    syncBurstNameControls();
    invalidate();
  }
  if (!burstLibraryOnly && rContains(point, viewMenuBoxRect())) {
    openGeometryMenu(GeometryMenuView);
    return true;
  }
  if (geometryViewMode == GeometryModeBurst)
    return handleBurstToolboxClickAtPoint(point);
  if (geometryViewMode == GeometryModePitchMap) {
    if (rContains(point, laneMenuBoxRect())) {
      openGeometryMenu(GeometryMenuLane);
      return true;
    }
    if (rContains(point, pitchScopeMenuBoxRect())) {
      openGeometryMenu(GeometryMenuPitchScope);
      return true;
    }
    if (rContains(point, pitchRootMenuBoxRect())) {
      openGeometryMenu(GeometryMenuPitchRoot);
      return true;
    }
    if (rContains(point, pitchScaleMenuBoxRect())) {
      openGeometryMenu(GeometryMenuPitchScale);
      return true;
    }
    if (rContains(point, pitchContourMenuBoxRect())) {
      openGeometryMenu(GeometryMenuPitchContour);
      return true;
    }
    if (rContains(point, pitchLeapMenuBoxRect())) {
      openGeometryMenu(GeometryMenuPitchLeap);
      return true;
    }
    if (rContains(point, pitchAnchorToggleRect())) {
      _pitchSettings.preserveEndpoints = !_pitchSettings.preserveEndpoints;
      _pitchOverrides.fill(-1);
      invalidate();
      return true;
    }
    if (rContains(point, pitchInvertToggleRect())) {
      _pitchSettings.invertScaleDegrees = !_pitchSettings.invertScaleDegrees;
      _pitchOverrides.fill(-1);
      _pitchStatus = "PREVIEW UPDATED · SCALE INVERSION";
      invalidate();
      return true;
    }
    if (rContains(point, pitchReverseToggleRect())) {
      _pitchSettings.reversePitchOrder = !_pitchSettings.reversePitchOrder;
      _pitchOverrides.fill(-1);
      _pitchStatus = "PREVIEW UPDATED · PITCH ORDER";
      invalidate();
      return true;
    }
    if (rContains(point, pitchAnalyzeHeaderButtonRect())) {
      analyzePitchMap();
      return true;
    }
    if (rContains(point, pitchPreviewHeaderButtonRect())) {
      refreshPitchMapPreview();
      if (_pitchPreview.assignments.empty() || trackerState->playing)
        return true;
      const auto &track = trackerState->session.pattern.tracks[std::min(
          trackerState->session.selectedTrack,
          trackerState->session.pattern.tracks.size() - 1u)];
      std::vector<PitchPreviewEvent> events;
      events.reserve(_pitchPreview.assignments.size() *
                     s3g::tracker::kMaximumNoteVoices);
      const auto firstHit = _pitchPreview.assignments.front().row;
      for (const auto &assignment : _pitchPreview.assignments) {
        const auto rowVelocity = resolvedVelocity(track, assignment.row);
        const ValueCell *authoredVelocity =
            assignment.row < track.velocities.size() &&
                    track.velocities[assignment.row].state ==
                        ValueCellState::Value
                ? &track.velocities[assignment.row]
                : nullptr;
        for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice) {
          PitchPreviewEvent event;
          event.row = static_cast<uint16_t>(
              std::min<std::size_t>(255u, assignment.row - firstHit));
          event.note = assignment.notes[voice];
          const float velocity =
              authoredVelocity
                  ? authoredVelocity->valueVoice(std::min<std::size_t>(
                        voice, authoredVelocity->valueVoiceCount() - 1u))
                  : rowVelocity;
          event.velocity = static_cast<uint8_t>(std::clamp<int>(
              static_cast<int>(std::lround(velocity * 127.0f)), 1, 127));
          event.gatePercent = 70u;
          events.push_back(event);
        }
      }
      if (callbacks_ && callbacks_->previewPitchSequence) {
        const double projectBpm = trackerState->hostBpm > 0.0
                                      ? trackerState->hostBpm
                                      : trackerState->session.transport.bpm;
        callbacks_->previewPitchSequence(
            events, track.midiChannel, projectBpm,
            trackerState->session.transport.ticksPerBeat);
        _pitchStatus =
            format("PREVIEW · %lu VOICES @ %.1f BPM",
                   static_cast<unsigned long>(events.size()), projectBpm);
        pulsePitchPreviewFeedback();
      }
      return true;
    }
    if (rContains(point, pitchNewSeedHeaderButtonRect())) {
      _pitchSettings.seed = std::random_device{}();
      _pitchOverrides.fill(-1);
      _pitchStatus = "NEW DETERMINISTIC VARIATION";
      invalidate();
      return true;
    }
    if (rContains(point, pitchApplyHeaderButtonRect())) {
      applyCurrentPitchMap();
      return true;
    }
    return false;
  }
  const bool editable = canEditDisplayedPattern();
  if (editable && rContains(point, laneMenuBoxRect())) {
    openGeometryMenu(GeometryMenuLane);
    return true;
  }
  if (editable && rContains(point, directionMenuBoxRect())) {
    openGeometryMenu(GeometryMenuDirection);
    return true;
  }
  if (editable && rContains(point, morphTargetMenuBoxRect())) {
    openGeometryMenu(GeometryMenuMorphTarget);
    return true;
  }
  if (editable && rContains(point, linkVelocityLengthToggleRect())) {
    linkVelocityLength = !linkVelocityLength;
    invalidate();
    return true;
  }
  if (rContains(point, revealHeaderButtonRect())) {
    revealInTracker(0);
    return true;
  }
  for (std::size_t index = 0u; index < 4u; ++index) {
    if (!rContains(point, editToolButtonRect(index)))
      continue;
    if (index == 0u || (editable && geometryViewMode == GeometryModeRingField))
      toolChanged(toolButtons[index]);
    return true;
  }
  if (!editable)
    return false;
  if (rContains(point, reverseButtonRect())) {
    reverse(0);
    return true;
  }
  if (rContains(point, reflectButtonRect())) {
    reflect(0);
    return true;
  }
  for (std::size_t index = 0u; index < morphButtons.size(); ++index) {
    if (!rContains(point, morphAmountButtonRect(index)))
      continue;
    morphAmount(morphButtons[index]);
    return true;
  }
  return false;
}

void GeometryEditor::mouseMoved(const GeometryInput &event) {
  if (_openGeometryMenu == GeometryMenuNone)
    return;
  const Point point = event.point;
  const auto count =
      static_cast<uint32_t>(itemsForGeometryMenu(_openGeometryMenu).size());
  const uint32_t columns =
      _openGeometryMenu == GeometryMenuPitchScale
          ? 4u
          : _openGeometryMenu == GeometryMenuBurstSlot ? 2u : 1u;
  const int hover =
      columns > 1u
          ? multiColumnDropdownHitIndex(
                point, dropdownRectForGeometryMenu(_openGeometryMenu), 21.0,
                count, columns)
          : dropdownHitIndex(point,
                             dropdownRectForGeometryMenu(_openGeometryMenu),
                             21.0, count);
  if (hover == _geometryMenuHoverIndex)
    return;
  _geometryMenuHoverIndex = hover;
  invalidate();
}

void GeometryEditor::advancePlaybackAnimation() {
  const double now = now_;
  const double elapsed = _lastReadHeadAnimationTime > 0.0
                             ? std::max(now - _lastReadHeadAnimationTime, 0.0)
                             : 1.0 / 60.0;
  _lastReadHeadAnimationTime = now;
  // Roughly 140 ms to halve: the onset stays crisp while its smaller tail
  // remains readable between edge-triggered GUI updates.
  const double decay =
      static_cast<double>(std::exp(-elapsed * std::log(2.0) / 0.14));
  auto *model = trackerState;
  for (std::size_t lane = 0u; lane < _readHeadHaloStrength.size(); ++lane) {
    _readHeadHaloStrength[lane] *= decay;
    if (_readHeadHaloStrength[lane] < 0.012)
      _readHeadHaloStrength[lane] = 0.0;
    if (model && model->playing && model->noteHits[lane]) {
      _readHeadHaloRows[lane] = model->noteHitRows[lane];
      _readHeadHaloStrength[lane] = 1.0;
    }
  }
}

void GeometryEditor::refreshPlaybackDisplay() {
  now_ = services_.monotonicTime();
  if (now_ >= placeFeedbackUntil_)
    _burstPlaceFeedbackActive = false;
  if (!burstPreviewTimer && now_ >= burstFeedbackUntil_)
    _burstPreviewFeedbackActive = false;
  if (now_ >= pitchFeedbackUntil_)
    _pitchPreviewFeedbackActive = false;
  if (burstPreviewTimer &&
      (trackerState->playing || !trackerState->burstLoopPreview))
    stopBurstPreview();
  const auto currentPatternId = geometryPatternId(trackerState);
  const uint32_t currentSongMuteMask =
      trackerState && trackerState->songPlaybackActive
          ? trackerState->songPlaybackMutedTracks
          : 0u;
  if (_lastDisplayedPatternId != currentPatternId ||
      _lastDisplayedSongMuteMask != currentSongMuteMask) {
    _lastDisplayedPatternId = currentPatternId;
    _lastDisplayedSongMuteMask = currentSongMuteMask;
    invalidate();
  }
  advancePlaybackAnimation();
  invalidate();
}

Rect GeometryEditor::zoomOutRect() {
  const Rect canvas = canvasRect();
  return makeRect(
      std::max<double>(rMinX(canvas) + 180.0, rMaxX(canvas) - 118.0),
      rMinY(canvas) + 3.0, 28.0, 15.0);
}

Rect GeometryEditor::zoomResetRect() {
  const Rect previous = zoomOutRect();
  return makeRect(rMaxX(previous) + 2.0, rMinY(previous), 56.0, 15.0);
}

Rect GeometryEditor::zoomInRect() {
  const Rect previous = zoomResetRect();
  return makeRect(rMaxX(previous) + 2.0, rMinY(previous), 26.0, 15.0);
}

void GeometryEditor::setGeometryZoomAndRedraw(double value) {
  geometryZoom = std::clamp(value, 0.65, 1.8);
  invalidate();
  invalidate();
}

bool GeometryEditor::canEditDisplayedPattern() {
  return trackerState && !trackerState->songPlaybackActive;
}

Track *GeometryEditor::selectedEditableTrack() {
  auto *model = trackerState;
  if (!canEditDisplayedPattern() || model->session.pattern.tracks.empty())
    return nullptr;
  auto lane = std::min(model->session.selectedTrack,
                       model->session.pattern.tracks.size() - 1u);
  if (model->session.pattern.tracks[lane].noteColumn.muted) {
    const auto visible = visibleGeometryLanes(&model->session.pattern);
    if (visible.count == 0u)
      return nullptr;
    lane = visible.indices[0u];
    model->session.selectedTrack = lane;
  }
  return &model->session.pattern.tracks[lane];
}

void GeometryEditor::commitGeometryChange(bool changed) {
  if (!changed)
    return;
  auto *model = trackerState;
  if (model)
    model->session.pattern.visibleRows = std::max(
        model->session.pattern.visibleRows, model->session.selectedRow + 1u);
  publish();
}

void GeometryEditor::rotateBack(int sender) {
  (void)sender;
  Track *track = selectedEditableTrack();
  commitGeometryChange(track && s3g::tracker::rotateGeometryRows(*track, -1));
}

void GeometryEditor::rotateForward(int sender) {
  (void)sender;
  Track *track = selectedEditableTrack();
  commitGeometryChange(track && s3g::tracker::rotateGeometryRows(*track, 1));
}

void GeometryEditor::densityDown(int sender) {
  (void)sender;
  auto *model = trackerState;
  Track *track = selectedEditableTrack();
  if (!model || !track)
    return;
  const auto hits = s3g::tracker::geometryHitCount(*track);
  commitGeometryChange(s3g::tracker::setGeometryDensity(
      *track, hits > 0u ? hits - 1u : 0u,
      s3g::tracker::laneDefaultNote(model->session,
                                    model->session.selectedTrack)));
}

void GeometryEditor::densityUp(int sender) {
  (void)sender;
  auto *model = trackerState;
  Track *track = selectedEditableTrack();
  if (!model || !track)
    return;
  const auto length =
      std::clamp<std::size_t>(track->noteColumn.length, 1u, 256u);
  const auto hits = s3g::tracker::geometryHitCount(*track);
  commitGeometryChange(s3g::tracker::setGeometryDensity(
      *track, std::min(hits + 1u, length),
      s3g::tracker::laneDefaultNote(model->session,
                                    model->session.selectedTrack)));
}

void GeometryEditor::reverse(int sender) {
  (void)sender;
  Track *track = selectedEditableTrack();
  commitGeometryChange(track && s3g::tracker::reverseGeometry(*track));
}

void GeometryEditor::reflect(int sender) {
  (void)sender;
  auto *model = trackerState;
  Track *track = selectedEditableTrack();
  commitGeometryChange(
      model && track &&
      s3g::tracker::reflectGeometry(*track, model->session.selectedRow));
}

void GeometryEditor::morphAmount(const GeometryButton &sender) {
  auto *model = trackerState;
  Track *track = selectedEditableTrack();
  if (!model || !track)
    return;
  const auto visible = visibleGeometryLanes(&model->session.pattern);
  if (visible.count < 2u)
    return;
  std::size_t ordinal = 0u;
  for (; ordinal < visible.count; ++ordinal)
    if (visible.indices[ordinal] == model->session.selectedTrack)
      break;
  if (ordinal == visible.count)
    return;
  const bool previous = morphTargetPopup.indexOfSelectedItem == 0;
  const auto targetOrdinal =
      previous ? (ordinal + visible.count - 1u) % visible.count
               : (ordinal + 1u) % visible.count;
  const auto targetLane = visible.indices[targetOrdinal];
  const Track target = model->session.pattern.tracks[targetLane];
  const float amount =
      static_cast<float>(std::clamp<int>(sender.tag, 0, 100)) / 100.0f;
  commitGeometryChange(s3g::tracker::morphGeometry(
      *track, target, amount,
      s3g::tracker::laneDefaultNote(model->session,
                                    model->session.selectedTrack)));
}

void GeometryEditor::revealInTracker(int sender) {
  (void)sender;
  auto *model = trackerState;
  if (!model)
    return;
  model->session.selectedPage = 0u;
  selectionChanged();
  if (callbacks_ && callbacks_->showTrackerPage) {
    callbacks_->showTrackerPage();
  } else {
    revealTracker();
    revealTracker();
  }
}

void GeometryEditor::selectLane(std::size_t lane, std::size_t row,
                                std::size_t field) {
  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);
  if (!model || !pattern || pattern->tracks.empty())
    return;
  const auto lanes = std::min<std::size_t>(s3g::tracker::kMaximumTrackCount,
                                           pattern->tracks.size());
  model->session.selectedTrack = std::min(lane, lanes - 1u);
  const auto &track = pattern->tracks[model->session.selectedTrack];
  const auto length =
      std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
  model->session.selectedRow = row % length;
  model->session.selectedField = std::min<std::size_t>(field, 1u);
  accessibilityValue =
      format("Lane %lu, %s, row %lu",
             static_cast<unsigned long>(model->session.selectedTrack + 1u),
             nsString(track.name),
             static_cast<unsigned long>(model->session.selectedRow + 1u));
  selectionChanged();
}

void GeometryEditor::selectLane(std::size_t lane) {
  auto *model = trackerState;
  selectLane(lane, model ? model->session.selectedRow : 0u,
             model ? model->session.selectedField : 0u);
}

Point GeometryEditor::geometryCenter() {
  const Rect plot = canvasPlotRect();
  return makePoint(rMidX(plot), rMidY(plot));
}

double GeometryEditor::geometryMaximumRadius() {
  const Rect canvas = canvasPlotRect();
  return std::max<double>(30.0,
                          std::min(rWidth(canvas), rHeight(canvas)) * 0.43) *
         geometryZoom;
}

double GeometryEditor::ringRadiusForOrdinal(std::size_t ordinal,
                                            std::size_t count) {
  const double maximum = geometryMaximumRadius();
  if (count <= 1u)
    return maximum * 0.64;
  const double inner = maximum * 0.22;
  return inner + (maximum - inner) * static_cast<double>(ordinal) /
                     static_cast<double>(count - 1u);
}

double GeometryEditor::ringRadiusForLane(std::size_t lane) {
  const auto lanes = geometryLanes(geometryPattern(trackerState));
  for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
    if (lanes.indices[ordinal] == lane)
      return ringRadiusForOrdinal(ordinal, lanes.count);
  }
  return 0.0;
}

bool GeometryEditor::selectedRingLane(std::size_t *lane, double *radius) {
  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);
  const auto visible = visibleGeometryLanes(model);
  const auto lanes = geometryLanes(pattern);
  if (!model || !pattern || visible.count == 0u)
    return false;
  std::size_t selectedLane = visible.indices[0u];
  for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
    if (visible.indices[ordinal] == model->session.selectedTrack) {
      selectedLane = visible.indices[ordinal];
      break;
    }
  }
  if (lane)
    *lane = selectedLane;
  if (radius) {
    *radius = 0.0;
    for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
      if (lanes.indices[ordinal] != selectedLane)
        continue;
      *radius = ringRadiusForOrdinal(ordinal, lanes.count);
      break;
    }
  }
  return true;
}

double GeometryEditor::geometryAngleForPoint(Point point) {
  const Point center = geometryCenter();
  return std::atan2(point.y - center.y, point.x - center.x);
}

Point GeometryEditor::geometryPointAtRadius(double radius, double angle) {
  const Point center = geometryCenter();
  return makePoint(center.x + std::cos(angle) * radius,
                   center.y + std::sin(angle) * radius);
}

Point GeometryEditor::rotateHandlePoint() {
  std::size_t lane = 0u;
  double radius = 0.0;
  const auto *pattern = geometryPattern(trackerState);
  if (!selectedRingLane(&lane, &radius) || !pattern ||
      lane >= pattern->tracks.size())
    return Point{};
  const auto &track = pattern->tracks[lane];
  const auto length =
      _geometryGestureActive && _geometryGestureKind == GeometryGestureLength &&
              _gestureLane == lane
          ? _gesturePreviewLength
          : std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
  const int rotation = _geometryGestureActive &&
                               _geometryGestureKind == GeometryGestureRotate &&
                               _gestureLane == lane
                           ? _gesturePreviewRotation
                           : 0;
  const auto signedLength = static_cast<long long>(length);
  const auto displayRow = static_cast<std::size_t>(
      (static_cast<long long>(rotation) % signedLength + signedLength) %
      signedLength);
  const double angle = -static_cast<double>(kGeometryHalfPi) +
                       static_cast<double>(displayRow) * 2.0 *
                           static_cast<double>(kGeometryPi) /
                           static_cast<double>(length);
  // Keep the rotate control inside the ring so it never masks a note bead.
  return geometryPointAtRadius(std::max<double>(0.0, radius - 13.0), angle);
}

Point GeometryEditor::densityHandlePoint() {
  std::size_t lane = 0u;
  double radius = 0.0;
  const auto *pattern = geometryPattern(trackerState);
  if (!selectedRingLane(&lane, &radius) || !pattern ||
      lane >= pattern->tracks.size())
    return Point{};
  const auto &track = pattern->tracks[lane];
  const auto length =
      _geometryGestureActive && _geometryGestureKind == GeometryGestureLength &&
              _gestureLane == lane
          ? _gesturePreviewLength
          : std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
  std::size_t density = 0u;
  for (std::size_t row = 0u; row < length; ++row) {
    if (row < track.notes.size() && noteCellIsActivePulse(track.notes[row]))
      ++density;
  }
  density = _geometryGestureActive &&
                    _geometryGestureKind == GeometryGestureDensity &&
                    _gestureLane == lane
                ? _gesturePreviewDensity
                : density;
  const double angle = -static_cast<double>(kGeometryHalfPi) +
                       static_cast<double>(density) * 2.0 *
                           static_cast<double>(kGeometryPi) /
                           static_cast<double>(length);
  return geometryPointAtRadius(radius + 18.0, angle);
}

void GeometryEditor::prepareGeometryGesture(GeometryGesture kind,
                                            std::size_t lane) {
  auto &track = trackerState->session.pattern.tracks[lane];
  const auto length =
      std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
  _geometryGestureActive = true;
  _geometryGestureChanged = false;
  _geometryGestureKind = kind;
  _geometrySliderGesture = false;
  _gestureLane = lane;
  _gestureOriginalDefaultNote =
      s3g::tracker::laneDefaultNote(trackerState->session, lane);
  _gesturePreviewDefaultNote = _gestureOriginalDefaultNote;
  _gestureOriginalLength = length;
  _gesturePreviewLength = length;
  _gestureOriginalPhase = track.noteColumn.phase % length;
  _gesturePreviewPhase = _gestureOriginalPhase;
  _gesturePreviewRotation = 0;
  _gestureOriginalDensity = s3g::tracker::geometryHitCount(track);
  _gesturePreviewDensity = _gestureOriginalDensity;
  _gestureOriginalNotes = track.notes;
  _gesturePreviewNotes = track.notes;
}

void GeometryEditor::updateRotationPreviewForTrack(const Track &track) {
  Track preview = track;
  preview.notes = _gestureOriginalNotes;
  _geometryGestureChanged =
      s3g::tracker::rotateGeometryRows(preview, _gesturePreviewRotation);
  _gesturePreviewNotes = _geometryGestureChanged ? std::move(preview.notes)
                                                 : _gestureOriginalNotes;
}

void GeometryEditor::updateDensityPreviewForTrack(const Track &track) {
  _geometryGestureChanged = _gesturePreviewDensity != _gestureOriginalDensity;
  if (!_geometryGestureChanged) {
    _gesturePreviewNotes = _gestureOriginalNotes;
    return;
  }
  Track preview = track;
  preview.notes = _gestureOriginalNotes;
  (void)s3g::tracker::setGeometryDensity(
      preview, _gesturePreviewDensity,
      s3g::tracker::laneDefaultNote(trackerState->session, _gestureLane));
  _gesturePreviewNotes = std::move(preview.notes);
}

bool GeometryEditor::beginSliderGestureAtPoint(Point point) {
  if (!canEditDisplayedPattern())
    return false;
  if (geometryViewMode == GeometryModePitchMap) {
    GeometryGesture kind = GeometryGestureNone;
    const auto lanePanel = geometryLayout().laneCycle;
    const auto contourPanel = geometryLayout().editShape;
    if (rContains(point, logicalRect(layout::sliderHitRect(lanePanel, 4u))))
      kind = GeometryGesturePitchMinimum;
    else if (rContains(point,
                       logicalRect(layout::sliderHitRect(lanePanel, 5u))))
      kind = GeometryGesturePitchMaximum;
    else if (rContains(point,
                       logicalRect(layout::sliderHitRect(contourPanel, 2u))) &&
             _pitchSettings.contour != PitchContour::Fit &&
             _pitchSettings.contour != PitchContour::Manual)
      kind = GeometryGesturePitchVariation;
    else if (rContains(point,
                       logicalRect(layout::sliderHitRect(contourPanel, 4u))))
      kind = GeometryGesturePitchTranspose;
    if (kind == GeometryGestureNone)
      return false;
    _geometryGestureActive = true;
    _geometryGestureChanged = false;
    _geometrySliderGesture = true;
    _geometryGestureKind = kind;
    updateSliderGestureAtPoint(point);
    return true;
  }
  if (geometryViewMode == GeometryModeBurst) {
    const auto &burst =
        trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
    if (burst.empty())
      return false;
    const std::array<GeometryGesture, 3u> kinds{{
        GeometryGestureBurstNote,
        GeometryGestureBurstVelocity,
        GeometryGestureBurstGate,
    }};
    for (std::size_t index = 0u; index < kinds.size(); ++index) {
      const auto row = static_cast<uint32_t>(index + 1u);
      const Rect hit =
          logicalRect(layout::sliderHitRect(geometryLayout().editShape, row));
      if (!rContains(point, hit))
        continue;
      _geometryGestureActive = true;
      _geometryGestureChanged = false;
      _geometrySliderGesture = true;
      _geometryGestureKind = kinds[index];
      updateSliderGestureAtPoint(point);
      return true;
    }
    return false;
  }
  GeometryGesture kind = GeometryGestureNone;
  if (rContains(point, sliderHitRect(defaultNoteSliderTrack())))
    kind = GeometryGestureDefaultNote;
  else if (rContains(point, sliderHitRect(lengthSliderTrack())))
    kind = GeometryGestureLength;
  else if (rContains(point, sliderHitRect(rotateSliderTrack())))
    kind = GeometryGestureRotate;
  else if (rContains(point, sliderHitRect(densitySliderTrack())))
    kind = GeometryGestureDensity;
  if (kind == GeometryGestureNone)
    return false;
  std::size_t lane = 0u;
  if (!selectedRingLane(&lane, nullptr))
    return false;
  if (trackerState->session.selectedTrack != lane)
    selectLane(lane);
  prepareGeometryGesture(kind, lane);
  _geometrySliderGesture = true;
  updateSliderGestureAtPoint(point);
  return true;
}

void GeometryEditor::updateSliderGestureAtPoint(Point point) {
  if (!_geometryGestureActive || !_geometrySliderGesture || !trackerState)
    return;
  auto &pattern = trackerState->session.pattern;
  if (_geometryGestureKind == GeometryGesturePitchMinimum ||
      _geometryGestureKind == GeometryGesturePitchMaximum ||
      _geometryGestureKind == GeometryGesturePitchVariation ||
      _geometryGestureKind == GeometryGesturePitchTranspose) {
    Rect slider =
        _geometryGestureKind == GeometryGesturePitchMinimum
            ? pitchMinimumSliderTrack()
            : _geometryGestureKind == GeometryGesturePitchMaximum
                  ? pitchMaximumSliderTrack()
                  : _geometryGestureKind == GeometryGesturePitchTranspose
                        ? pitchTransposeSliderTrack()
                        : pitchVariationSliderTrack();
    const double normalized = std::clamp(
        (point.x - rMinX(slider)) / std::max<double>(1.0, rWidth(slider)), 0.0,
        1.0);
    if (_geometryGestureKind == GeometryGesturePitchMinimum) {
      _pitchSettings.minimumNote = static_cast<uint8_t>(std::min<long>(
          std::lround(normalized * 127.0), _pitchSettings.maximumNote));
    } else if (_geometryGestureKind == GeometryGesturePitchMaximum) {
      _pitchSettings.maximumNote = static_cast<uint8_t>(std::max<long>(
          std::lround(normalized * 127.0), _pitchSettings.minimumNote));
    } else if (_geometryGestureKind == GeometryGesturePitchVariation) {
      _pitchSettings.variation = static_cast<float>(normalized);
    } else {
      _pitchSettings.transposeSemitones = static_cast<int8_t>(
          std::clamp<long>(std::lround(normalized * 48.0) - 24, -24, 24));
    }
    _pitchOverrides.fill(-1);
    _pitchStatus = "PREVIEW UPDATED";
    invalidate();
    return;
  }
  if (_geometryGestureKind == GeometryGestureBurstNote ||
      _geometryGestureKind == GeometryGestureBurstVelocity ||
      _geometryGestureKind == GeometryGestureBurstGate) {
    auto &burst = trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
    if (burst.empty())
      return;
    auto &event = burst.events[std::min<std::size_t>(_selectedBurstEvent,
                                                     burst.eventCount - 1u)];
    const uint32_t row =
        _geometryGestureKind == GeometryGestureBurstNote
            ? 1u
            : _geometryGestureKind == GeometryGestureBurstVelocity ? 2u : 3u;
    const Rect slider = burstSliderTrackForRow(row);
    const double normalized = std::clamp(
        (point.x - rMinX(slider)) / std::max<double>(1.0, rWidth(slider)), 0.0,
        1.0);
    if (_geometryGestureKind == GeometryGestureBurstNote) {
      const auto value = static_cast<uint8_t>(std::lround(normalized * 127.0));
      _geometryGestureChanged |= event.note != value;
      event.note = value;
    } else if (_geometryGestureKind == GeometryGestureBurstVelocity) {
      const auto value = static_cast<uint8_t>(
          std::clamp<long>(std::lround(normalized * 127.0), 1l, 127l));
      _geometryGestureChanged |= event.velocity != value;
      event.velocity = value;
    } else {
      const auto value = static_cast<uint8_t>(
          std::clamp<long>(std::lround(normalized * 100.0), 1l, 100l));
      _geometryGestureChanged |= event.gatePercent != value;
      event.gatePercent = value;
    }
    invalidate();
    return;
  }
  if (_gestureLane >= pattern.tracks.size())
    return;
  auto &track = pattern.tracks[_gestureLane];
  Rect slider = _geometryGestureKind == GeometryGestureDefaultNote
                    ? defaultNoteSliderTrack()
                    : lengthSliderTrack();
  if (_geometryGestureKind == GeometryGestureRotate)
    slider = rotateSliderTrack();
  else if (_geometryGestureKind == GeometryGestureDensity)
    slider = densitySliderTrack();
  const double normalized = std::clamp(
      (point.x - rMinX(slider)) / std::max<double>(1.0, rWidth(slider)), 0.0,
      1.0);
  if (_geometryGestureKind == GeometryGestureDefaultNote) {
    _gesturePreviewDefaultNote =
        static_cast<uint8_t>(std::lround(normalized * 127.0));
    _geometryGestureChanged =
        _gesturePreviewDefaultNote != _gestureOriginalDefaultNote ||
        std::any_of(track.notes.begin(), track.notes.end(),
                    [&](const NoteCell &cell) {
                      return cell.state == NoteCellState::Note &&
                             cell.note != _gesturePreviewDefaultNote;
                    });
  } else if (_geometryGestureKind == GeometryGestureLength) {
    // A square response gives common short cycles enough physical travel
    // while retaining every legal 1–256-step length.
    _gesturePreviewLength =
        1u +
        static_cast<std::size_t>(std::lround(normalized * normalized * 255.0));
    _gesturePreviewPhase = _gestureOriginalPhase % _gesturePreviewLength;
    _geometryGestureChanged =
        _gesturePreviewLength != _gestureOriginalLength ||
        (linkVelocityLength &&
         track.velocityColumn.length != _gesturePreviewLength);
  } else if (_geometryGestureKind == GeometryGestureRotate) {
    const auto length =
        std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
    const double bipolar = normalized * 2.0 - 1.0;
    const double shaped = std::copysign(bipolar * bipolar, bipolar);
    _gesturePreviewRotation =
        length <= 1u ? 0
                     : static_cast<int>(std::lround(
                           shaped * static_cast<double>(length - 1u)));
    updateRotationPreviewForTrack(track);
  } else if (_geometryGestureKind == GeometryGestureDensity) {
    const auto length =
        std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
    _gesturePreviewDensity = static_cast<std::size_t>(
        std::lround(normalized * static_cast<double>(length)));
    updateDensityPreviewForTrack(track);
  }
  invalidate();
}

bool GeometryEditor::beginShapeGestureAtPoint(Point point) {
  if (geometryViewMode != GeometryModeRingField || !canEditDisplayedPattern())
    return false;
  auto *model = trackerState;
  std::size_t lane = 0u;
  double radius = 0.0;
  if (!model || !selectedRingLane(&lane, &radius) ||
      lane >= model->session.pattern.tracks.size())
    return false;
  const Point rotatePoint = rotateHandlePoint();
  const Point densityPoint = densityHandlePoint();
  const double rotateDistance =
      std::hypot(point.x - rotatePoint.x, point.y - rotatePoint.y);
  const double densityDistance =
      std::hypot(point.x - densityPoint.x, point.y - densityPoint.y);
  if (rotateDistance > 11.0 && densityDistance > 11.0)
    return false;

  const auto kind = rotateDistance <= densityDistance ? GeometryGestureRotate
                                                      : GeometryGestureDensity;
  if (model->session.selectedTrack != lane)
    selectLane(lane);
  prepareGeometryGesture(kind, lane);
  _gestureLastAngle = geometryAngleForPoint(point);
  _gestureAccumulatedAngle = 0.0;
  invalidate();
  return true;
}

void GeometryEditor::updateShapeGestureAtPoint(Point point) {
  if (!_geometryGestureActive || !trackerState ||
      (_geometryGestureKind != GeometryGestureRotate &&
       _geometryGestureKind != GeometryGestureDensity))
    return;
  auto &pattern = trackerState->session.pattern;
  if (_gestureLane >= pattern.tracks.size())
    return;
  auto &track = pattern.tracks[_gestureLane];
  const auto length =
      std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
  const double fullCircle = static_cast<double>(kGeometryPi) * 2.0;
  const double currentAngle = geometryAngleForPoint(point);
  double delta = currentAngle - _gestureLastAngle;
  if (delta > static_cast<double>(kGeometryPi))
    delta -= fullCircle;
  if (delta < -static_cast<double>(kGeometryPi))
    delta += fullCircle;
  _gestureAccumulatedAngle += delta;
  _gestureLastAngle = currentAngle;
  const auto stepDelta = static_cast<long long>(std::lround(
      _gestureAccumulatedAngle / fullCircle * static_cast<double>(length)));
  if (_geometryGestureKind == GeometryGestureRotate) {
    _gesturePreviewRotation = static_cast<int>(
        std::clamp<long long>(stepDelta, -static_cast<long long>(length - 1u),
                              static_cast<long long>(length - 1u)));
    updateRotationPreviewForTrack(track);
  } else {
    const auto density = std::clamp<long long>(
        static_cast<long long>(_gestureOriginalDensity) + stepDelta, 0ll,
        static_cast<long long>(length));
    _gesturePreviewDensity = static_cast<std::size_t>(density);
    updateDensityPreviewForTrack(track);
  }
  invalidate();
}

void GeometryEditor::finishGeometryGesture() {
  if (!_geometryGestureActive)
    return;
  if (_geometryGestureKind == GeometryGesturePitchMinimum ||
      _geometryGestureKind == GeometryGesturePitchMaximum ||
      _geometryGestureKind == GeometryGesturePitchVariation ||
      _geometryGestureKind == GeometryGesturePitchTranspose ||
      _geometryGestureKind == GeometryGesturePitchPoint) {
    _geometryGestureActive = false;
    _geometrySliderGesture = false;
    _geometryGestureKind = GeometryGestureNone;
    _geometryGestureChanged = false;
    _pitchDragAssignment = -1;
    invalidate();
    return;
  }
  if (_geometryGestureKind == GeometryGestureBurstPosition ||
      _geometryGestureKind == GeometryGestureBurstNote ||
      _geometryGestureKind == GeometryGestureBurstVelocity ||
      _geometryGestureKind == GeometryGestureBurstGate ||
      _geometryGestureKind == GeometryGestureBurstMatrixPosition ||
      _geometryGestureKind == GeometryGestureBurstMatrixNote ||
      _geometryGestureKind == GeometryGestureBurstMatrixVelocity ||
      _geometryGestureKind == GeometryGestureBurstMatrixGate ||
      _geometryGestureKind == GeometryGestureBurstVelocityPoint) {
    const bool changed = _geometryGestureChanged;
    _geometryGestureActive = false;
    _geometrySliderGesture = false;
    _geometryGestureKind = GeometryGestureNone;
    _geometryGestureChanged = false;
    if (changed)
      publish();
    invalidate();
    return;
  }
  auto *model = trackerState;
  bool changed = _geometryGestureChanged;
  if (model && _gestureLane < model->session.pattern.tracks.size()) {
    auto &track = model->session.pattern.tracks[_gestureLane];
    if (changed && _geometryGestureKind == GeometryGestureDefaultNote) {
      changed = s3g::tracker::setLaneDefaultNote(model->session, _gestureLane,
                                                 _gesturePreviewDefaultNote);
    } else if (changed && _geometryGestureKind == GeometryGestureLength) {
      changed = s3g::tracker::setGeometryNoteLength(
          track, _gesturePreviewLength, linkVelocityLength);
      model->session.pattern.visibleRows =
          std::max(model->session.pattern.visibleRows, _gesturePreviewLength);
    } else if (changed && _geometryGestureKind == GeometryGestureRotate) {
      changed =
          s3g::tracker::rotateGeometryRows(track, _gesturePreviewRotation);
    } else if (changed && _geometryGestureKind == GeometryGestureDensity) {
      changed = s3g::tracker::setGeometryDensity(
          track, _gesturePreviewDensity,
          s3g::tracker::laneDefaultNote(model->session, _gestureLane));
    }
  }
  _geometryGestureActive = false;
  _geometrySliderGesture = false;
  _geometryGestureKind = GeometryGestureNone;
  _geometryGestureChanged = false;
  _gestureOriginalNotes.clear();
  _gesturePreviewNotes.clear();
  commitGeometryChange(changed);
  selectionChanged();
  invalidate();
}

Rect GeometryEditor::burstMatrixRect() {
  const Rect plot = canvasPlotRect();
  const double width = std::floor((rWidth(plot) - 54.0) * 0.64);
  return makeRect(rMinX(plot) + 18.0, rMinY(plot) + 34.0,
                  std::max<double>(360.0, width), 254.0);
}

Rect GeometryEditor::burstOverviewRect() {
  const Rect plot = canvasPlotRect();
  const Rect matrix = burstMatrixRect();
  return makeRect(rMaxX(matrix) + 18.0, rMinY(matrix),
                  std::max<double>(180.0, rMaxX(plot) - rMaxX(matrix) - 36.0),
                  rHeight(matrix));
}

Rect GeometryEditor::burstBreakpointRect() {
  const Rect plot = canvasPlotRect();
  const Rect matrix = burstMatrixRect();
  const double top = rMaxY(matrix) + 38.0;
  return makeRect(rMinX(plot) + 18.0, top, rWidth(plot) - 36.0,
                  std::max<double>(84.0, rMaxY(plot) - top - 42.0));
}

Rect GeometryEditor::burstRadialPlotRect() {
  const Rect overview = burstOverviewRect();
  return rInsetRect(makeRect(rMinX(overview), rMinY(overview) + 19.0,
                             rWidth(overview), rHeight(overview) - 19.0),
                    10.0, 8.0);
}

Rect GeometryEditor::burstMatrixRowRect(std::size_t row) {
  const Rect matrix = burstMatrixRect();
  constexpr double headerHeight = 22.0;
  constexpr double rowHeight = 29.0;
  return makeRect(rMinX(matrix),
                  rMinY(matrix) + headerHeight +
                      static_cast<double>(row) * rowHeight,
                  rWidth(matrix), rowHeight);
}

Rect GeometryEditor::burstMatrixCellRect(std::size_t row, int field) {
  const Rect rowRect = burstMatrixRowRect(row);
  const double numberWidth = 38.0;
  const double available = rWidth(rowRect) - numberWidth;
  const std::array<double, 5u> edges{{
      rMinX(rowRect),
      rMinX(rowRect) + numberWidth,
      rMinX(rowRect) + numberWidth + available * 0.25,
      rMinX(rowRect) + numberWidth + available * 0.52,
      rMinX(rowRect) + numberWidth + available * 0.78,
  }};
  if (field < 0)
    return makeRect(edges[0], rMinY(rowRect), numberWidth, rHeight(rowRect));
  const auto index = static_cast<std::size_t>(std::clamp<int>(field, 0, 3));
  const double right = index == 3u ? rMaxX(rowRect) : edges[index + 2u];
  return makeRect(edges[index + 1u], rMinY(rowRect), right - edges[index + 1u],
                  rHeight(rowRect));
}

int GeometryEditor::burstMatrixRowAtPoint(Point point) {
  const Rect matrix = burstMatrixRect();
  if (!rContains(point, matrix) || point.y < rMinY(matrix) + 22.0)
    return -1;
  const int row = static_cast<int>((point.y - rMinY(matrix) - 22.0) / 29.0);
  return row >= 0 && row < static_cast<int>(kMaximumBurstEvents) ? row : -1;
}

int GeometryEditor::burstMatrixFieldAtPoint(Point point, std::size_t row) {
  for (int field = 0; field < 4; ++field)
    if (rContains(point, burstMatrixCellRect(row, field)))
      return field;
  return -1;
}

int GeometryEditor::burstBreakpointEventAtPoint(Point point) {
  if (!trackerState)
    return -1;
  const auto &burst =
      trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  const Rect graph = burstBreakpointRect();
  if (burst.empty() || !rContains(point, rInsetRect(graph, -8.0, -8.0)))
    return -1;
  int result = -1;
  double best = 14.0;
  const Rect inner = rInsetRect(graph, 12.0, 14.0);
  for (std::size_t index = 0u; index < burst.eventCount; ++index) {
    const auto &authored = burst.events[index];
    const Point marker =
        makePoint(rMinX(inner) + static_cast<double>(authored.position) /
                                     65535.0 * rWidth(inner),
                  rMaxY(inner) - static_cast<double>(authored.velocity - 1u) /
                                     126.0 * rHeight(inner));
    const double distance = std::hypot(point.x - marker.x, point.y - marker.y);
    if (distance >= best)
      continue;
    best = distance;
    result = static_cast<int>(index);
  }
  return result;
}

void GeometryEditor::updateBurstMatrixGestureAtPoint(Point point) {
  if (!trackerState || _selectedBurstEvent >= kMaximumBurstEvents)
    return;
  auto &burst = trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  if (burst.empty() || _selectedBurstEvent >= burst.eventCount)
    return;
  auto &authored = burst.events[_selectedBurstEvent];
  if (_geometryGestureKind == GeometryGestureBurstVelocityPoint) {
    const Rect inner = rInsetRect(_burstGestureRect, 12.0, 14.0);
    const double x = std::clamp((point.x - rMinX(inner)) /
                                    std::max<double>(1.0, rWidth(inner)),
                                0.0, 1.0);
    const double y = std::clamp((rMaxY(inner) - point.y) /
                                    std::max<double>(1.0, rHeight(inner)),
                                0.0, 1.0);
    uint16_t position = static_cast<uint16_t>(std::lround(x * 65535.0));
    const uint16_t minimum =
        _selectedBurstEvent == 0u
            ? 0u
            : burst.events[_selectedBurstEvent - 1u].position;
    const uint16_t maximum =
        _selectedBurstEvent + 1u >= burst.eventCount
            ? 65535u
            : burst.events[_selectedBurstEvent + 1u].position;
    position = std::clamp(position, minimum, maximum);
    const auto velocity = static_cast<uint8_t>(
        std::clamp<long>(std::lround(1.0 + y * 126.0), 1l, 127l));
    _geometryGestureChanged |=
        authored.position != position || authored.velocity != velocity;
    authored.position = position;
    authored.velocity = velocity;
    invalidate();
    return;
  }
  const double normalized =
      std::clamp((point.x - rMinX(_burstGestureRect)) /
                     std::max<double>(1.0, rWidth(_burstGestureRect)),
                 0.0, 1.0);
  if (_geometryGestureKind == GeometryGestureBurstMatrixPosition) {
    uint16_t position =
        static_cast<uint16_t>(std::lround(normalized * 65535.0));
    const uint16_t minimum =
        _selectedBurstEvent == 0u
            ? 0u
            : burst.events[_selectedBurstEvent - 1u].position;
    const uint16_t maximum =
        _selectedBurstEvent + 1u >= burst.eventCount
            ? 65535u
            : burst.events[_selectedBurstEvent + 1u].position;
    position = std::clamp(position, minimum, maximum);
    _geometryGestureChanged |= authored.position != position;
    authored.position = position;
  } else if (_geometryGestureKind == GeometryGestureBurstMatrixNote) {
    const auto value = static_cast<uint8_t>(std::lround(normalized * 127.0));
    _geometryGestureChanged |= authored.note != value;
    authored.note = value;
  } else if (_geometryGestureKind == GeometryGestureBurstMatrixVelocity) {
    const auto value = static_cast<uint8_t>(
        std::clamp<long>(std::lround(1.0 + normalized * 126.0), 1l, 127l));
    _geometryGestureChanged |= authored.velocity != value;
    authored.velocity = value;
  } else if (_geometryGestureKind == GeometryGestureBurstMatrixGate) {
    const auto value = static_cast<uint8_t>(
        std::clamp<long>(std::lround(1.0 + normalized * 99.0), 1l, 100l));
    _geometryGestureChanged |= authored.gatePercent != value;
    authored.gatePercent = value;
  }
  invalidate();
}

bool GeometryEditor::beginBurstCanvasGestureAtPoint(Point point) {
  if (!trackerState)
    return false;
  auto &burst = trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  const int matrixRow = burstMatrixRowAtPoint(point);
  if (matrixRow >= 0) {
    const auto row = static_cast<std::size_t>(matrixRow);
    if (row >= burst.eventCount) {
      if (!canEditDisplayedPattern())
        return true;
      if (burst.empty())
        initializeBurstAtSlot(_selectedBurstSlot);
      const auto previousCount = static_cast<std::size_t>(burst.eventCount);
      const auto targetCount = row + 1u;
      const BurstEvent seed =
          previousCount > 0u ? burst.events[previousCount - 1u] : BurstEvent{};
      for (std::size_t index = previousCount; index < targetCount; ++index)
        burst.events[index] = seed;
      burst.eventCount = static_cast<uint8_t>(targetCount);
      setGeometryBurstTiming(burst, "even");
      publish();
    }
    _selectedBurstEvent = row;
    const int field = burstMatrixFieldAtPoint(point, row);
    if (field < 0 || !canEditDisplayedPattern()) {
      invalidate();
      return true;
    }
    _selectedBurstField = field;
    const std::array<GeometryGesture, 4u> kinds{{
        GeometryGestureBurstMatrixPosition,
        GeometryGestureBurstMatrixNote,
        GeometryGestureBurstMatrixVelocity,
        GeometryGestureBurstMatrixGate,
    }};
    _burstGestureRect = rInsetRect(burstMatrixCellRect(row, field), 5.0, 0.0);
    _geometryGestureActive = true;
    _geometryGestureChanged = false;
    _geometrySliderGesture = false;
    _geometryGestureKind = kinds[static_cast<std::size_t>(field)];
    invalidate();
    return true;
  }
  const int breakpoint = burstBreakpointEventAtPoint(point);
  if (breakpoint >= 0) {
    _selectedBurstEvent = static_cast<std::size_t>(breakpoint);
    _selectedBurstField = 2;
    if (canEditDisplayedPattern()) {
      _burstGestureRect = burstBreakpointRect();
      _geometryGestureActive = true;
      _geometryGestureChanged = false;
      _geometrySliderGesture = false;
      _geometryGestureKind = GeometryGestureBurstVelocityPoint;
    }
    invalidate();
    return true;
  }
  return false;
}

int GeometryEditor::burstEventAtPoint(Point point) {
  const auto &burst =
      trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  if (burst.empty() || !rContains(point, burstOverviewRect()))
    return -1;
  const Rect plot = burstRadialPlotRect();
  const Point center = makePoint(rMidX(plot), rMidY(plot));
  const double radius = std::max<double>(
      50.0, std::min(rWidth(plot), rHeight(plot)) * 0.34 * geometryZoom);
  double best = 14.0;
  int result = -1;
  for (std::size_t index = 0u; index < burst.eventCount; ++index) {
    const auto &event = burst.events[index];
    const double angle = -static_cast<double>(kGeometryHalfPi) +
                         static_cast<double>(event.position) / 65536.0 * 2.0 *
                             static_cast<double>(kGeometryPi);
    const double eventRadius =
        radius + (static_cast<double>(event.note) / 127.0 - 0.5) * 30.0;
    const Point marker = makePoint(center.x + std::cos(angle) * eventRadius,
                                   center.y + std::sin(angle) * eventRadius);
    const double distance = std::hypot(point.x - marker.x, point.y - marker.y);
    if (distance >= best)
      continue;
    best = distance;
    result = static_cast<int>(index);
  }
  return result;
}

void GeometryEditor::updateBurstPositionAtPoint(Point point) {
  auto &burst = trackerState->session.burstLibrary.bursts[_selectedBurstSlot];
  if (burst.empty() || _selectedBurstEvent >= burst.eventCount)
    return;
  const Rect plot = burstRadialPlotRect();
  const Point center = makePoint(rMidX(plot), rMidY(plot));
  double angle = std::atan2(point.y - center.y, point.x - center.x) +
                 static_cast<double>(kGeometryHalfPi);
  const double fullCircle = 2.0 * static_cast<double>(kGeometryPi);
  while (angle < 0.0)
    angle += fullCircle;
  while (angle >= fullCircle)
    angle -= fullCircle;
  uint16_t position = static_cast<uint16_t>(
      std::clamp<long>(std::lround(angle / fullCircle * 65536.0), 0l, 65535l));
  const uint16_t minimum =
      _selectedBurstEvent == 0u
          ? 0u
          : burst.events[_selectedBurstEvent - 1u].position;
  const uint16_t maximum =
      _selectedBurstEvent + 1u >= burst.eventCount
          ? 65535u
          : burst.events[_selectedBurstEvent + 1u].position;
  position = std::clamp(position, minimum, maximum);
  auto &authored = burst.events[_selectedBurstEvent].position;
  _geometryGestureChanged |= authored != position;
  authored = position;
  invalidate();
}

bool GeometryEditor::geometryCellAtPoint(Point point, std::size_t *lane,
                                         std::size_t *row, double *hitRadius) {
  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);
  const auto lanes = geometryLanes(pattern);
  const Rect canvas = canvasRect();
  if (!pattern || lanes.count == 0u || !rContains(point, canvas))
    return false;
  const Point center = geometryCenter();
  const double distance = std::hypot(point.x - center.x, point.y - center.y);
  double best = std::numeric_limits<double>::max();
  std::size_t bestOrdinal = 0u;
  bool found = false;
  for (std::size_t ordinal = 0u; ordinal < lanes.count; ++ordinal) {
    if (geometryLaneMuted(model, pattern, lanes.indices[ordinal]))
      continue;
    const double candidate = ringRadiusForOrdinal(ordinal, lanes.count);
    const double delta = std::abs(distance - candidate);
    if (delta < best) {
      best = delta;
      bestOrdinal = ordinal;
      found = true;
    }
  }
  if (!found)
    return false;
  const double spacing = lanes.count > 1u
                             ? std::abs(ringRadiusForOrdinal(1u, lanes.count) -
                                        ringRadiusForOrdinal(0u, lanes.count))
                             : 24.0;
  if (best > std::max<double>(8.0, spacing * 0.44))
    return false;
  const auto selectedLane = lanes.indices[bestOrdinal];
  const auto length = std::clamp<std::size_t>(
      pattern->tracks[selectedLane].noteColumn.length, 1u, 256u);
  double angle = std::atan2(point.y - center.y, point.x - center.x) +
                 static_cast<double>(kGeometryHalfPi);
  const double fullCircle = static_cast<double>(kGeometryPi) * 2.0;
  while (angle < 0.0)
    angle += fullCircle;
  while (angle >= fullCircle)
    angle -= fullCircle;
  auto selectedRow = static_cast<std::size_t>(std::lround(
                         angle / fullCircle * static_cast<double>(length))) %
                     length;
  if (lane)
    *lane = selectedLane;
  if (row)
    *row = selectedRow;
  if (hitRadius)
    *hitRadius = ringRadiusForOrdinal(bestOrdinal, lanes.count);
  return true;
}

bool GeometryEditor::beadNear(Point point, std::size_t lane, std::size_t row,
                              double radius) {
  const auto *pattern = geometryPattern(trackerState);
  if (geometryViewMode != GeometryModeRingField || !pattern ||
      lane >= pattern->tracks.size())
    return false;
  const auto &track = pattern->tracks[lane];
  const auto length =
      std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
  row %= length;
  if (!s3g::tracker::geometryCellIsHit(track, row))
    return false;
  const double angle = -static_cast<double>(kGeometryHalfPi) +
                       static_cast<double>(row) * 2.0 *
                           static_cast<double>(kGeometryPi) /
                           static_cast<double>(length);
  const double eventRadius =
      radius + (resolvedVelocity(track, row) - 0.5) * 12.0;
  const Point bead = geometryPointAtRadius(eventRadius, angle);
  return std::hypot(point.x - bead.x, point.y - bead.y) <= 11.0;
}

bool GeometryEditor::revealBeadAtPoint(Point point) {
  std::size_t lane = 0u;
  std::size_t row = 0u;
  double radius = 0.0;
  if (!geometryCellAtPoint(point, &lane, &row, &radius) ||
      !beadNear(point, lane, row, radius))
    return false;
  selectLane(lane, row, 0u);
  revealInTracker(0);
  return true;
}

void GeometryEditor::mouseDown(const GeometryInput &event) {
  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);
  if (!model || !pattern || pattern->tracks.empty())
    return;
  const Point point = event.point;
  if (handleToolboxClickAtPoint(point))
    return;
  if (rContains(point, zoomOutRect())) {
    setGeometryZoomAndRedraw(geometryZoom / 1.15);
    return;
  }
  if (rContains(point, zoomResetRect())) {
    setGeometryZoomAndRedraw(1.0);
    return;
  }
  if (rContains(point, zoomInRect())) {
    setGeometryZoomAndRedraw(geometryZoom * 1.15);
    return;
  }
  if (geometryViewMode == GeometryModePitchMap && event.clickCount >= 2) {
    const auto lanePanel = geometryLayout().laneCycle;
    const auto contourPanel = geometryLayout().editShape;
    if (rContains(point, logicalRect(layout::sliderHitRect(lanePanel, 4u)))) {
      _pitchSettings.minimumNote =
          std::min<uint8_t>(36u, _pitchSettings.maximumNote);
      _pitchOverrides.fill(-1);
      invalidate();
      return;
    }
    if (rContains(point, logicalRect(layout::sliderHitRect(lanePanel, 5u)))) {
      _pitchSettings.maximumNote =
          std::max<uint8_t>(84u, _pitchSettings.minimumNote);
      _pitchOverrides.fill(-1);
      invalidate();
      return;
    }
    if (rContains(point,
                  logicalRect(layout::sliderHitRect(contourPanel, 2u))) &&
        _pitchSettings.contour != PitchContour::Fit &&
        _pitchSettings.contour != PitchContour::Manual) {
      _pitchSettings.variation = 0.5f;
      _pitchOverrides.fill(-1);
      invalidate();
      return;
    }
    if (rContains(point,
                  logicalRect(layout::sliderHitRect(contourPanel, 4u)))) {
      _pitchSettings.transposeSemitones = 0;
      _pitchOverrides.fill(-1);
      _pitchStatus = "TRANSPOSE RESET";
      invalidate();
      return;
    }
  }
  if (geometryViewMode == GeometryModeBurst &&
      beginBurstCanvasGestureAtPoint(point)) {
    focus();
    return;
  }
  if (beginSliderGestureAtPoint(point))
    return;
  if (geometryViewMode == GeometryModePitchMap) {
    focus();
    const int hit = pitchMapAssignmentAtPoint(point);
    if (hit >= 0) {
      _pitchDragAssignment = hit;
      const auto &assignment =
          _pitchPreview.assignments[static_cast<std::size_t>(hit)];
      model->session.selectedRow = assignment.row;
      selectionChanged();
      if (canEditDisplayedPattern()) {
        _geometryGestureActive = true;
        _geometryGestureChanged = false;
        _geometrySliderGesture = false;
        _geometryGestureKind = GeometryGesturePitchPoint;
      }
    }
    return;
  }
  if (geometryViewMode == GeometryModeBurst) {
    focus();
    const int hit = burstEventAtPoint(point);
    if (hit >= 0) {
      _selectedBurstEvent = static_cast<std::size_t>(hit);
      if (!canEditDisplayedPattern()) {
        invalidate();
        return;
      }
      _geometryGestureActive = true;
      _geometryGestureChanged = false;
      _geometrySliderGesture = false;
      _geometryGestureKind = GeometryGestureBurstPosition;
      invalidate();
    }
    return;
  }
  const auto visible = visibleGeometryLanes(model);
  if (visible.count == 0u)
    return;
  focus();
  if (event.clickCount >= 2 && revealBeadAtPoint(point))
    return;
  if (beginShapeGestureAtPoint(point))
    return;
  std::size_t lane = 0u;
  std::size_t row = 0u;
  double radius = 0.0;
  if (!geometryCellAtPoint(point, &lane, &row, &radius))
    return;
  const std::size_t field = geometryTool == GeometryToolVelocity ? 1u : 0u;
  selectLane(lane, row, field);
  if (geometryViewMode != GeometryModeRingField ||
      geometryTool == GeometryToolSelect || !canEditDisplayedPattern())
    return;

  _geometryGestureActive = true;
  _geometryGestureChanged = false;
  _geometrySliderGesture = false;
  _geometryGestureKind = geometryTool == GeometryToolVelocity
                             ? GeometryGestureVelocity
                             : geometryTool == GeometryToolErase ||
                                       (geometryTool == GeometryToolPaint &&
                                        (event.modifiers & Alt) != 0u)
                                   ? GeometryGestureErase
                                   : GeometryGesturePaint;
  _lastGestureRow = static_cast<int>(row);
  _gestureLane = lane;
  _gestureRow = row;
  _velocityStartRadius =
      std::hypot(point.x - geometryCenter().x, point.y - geometryCenter().y);
  auto &editable = model->session.pattern.tracks[lane];
  _velocityStartValue = resolvedVelocity(editable, row);
  if (_geometryGestureKind == GeometryGesturePaint) {
    _geometryGestureChanged = s3g::tracker::setGeometryHit(
        editable, row, true,
        s3g::tracker::laneDefaultNote(model->session, lane));
  } else if (_geometryGestureKind == GeometryGestureErase) {
    _geometryGestureChanged = s3g::tracker::setGeometryHit(
        editable, row, false,
        s3g::tracker::laneDefaultNote(model->session, lane));
  }
  invalidate();
}

void GeometryEditor::mouseDragged(const GeometryInput &event) {
  if (!_geometryGestureActive || !trackerState)
    return;
  const Point point = event.point;
  if (_geometrySliderGesture) {
    updateSliderGestureAtPoint(point);
    return;
  }
  if (_geometryGestureKind == GeometryGesturePitchPoint) {
    freezePitchPreviewForManualEditing();
    updatePitchMapPointAtPoint(point);
    return;
  }
  if (_geometryGestureKind == GeometryGestureBurstPosition) {
    updateBurstPositionAtPoint(point);
    return;
  }
  if (_geometryGestureKind == GeometryGestureBurstMatrixPosition ||
      _geometryGestureKind == GeometryGestureBurstMatrixNote ||
      _geometryGestureKind == GeometryGestureBurstMatrixVelocity ||
      _geometryGestureKind == GeometryGestureBurstMatrixGate ||
      _geometryGestureKind == GeometryGestureBurstVelocityPoint) {
    updateBurstMatrixGestureAtPoint(point);
    return;
  }
  if (_geometryGestureKind == GeometryGestureRotate ||
      _geometryGestureKind == GeometryGestureDensity) {
    updateShapeGestureAtPoint(point);
    return;
  }
  auto &pattern = trackerState->session.pattern;
  if (_gestureLane >= pattern.tracks.size())
    return;
  auto &track = pattern.tracks[_gestureLane];
  if (_geometryGestureKind == GeometryGestureVelocity) {
    const Point center = geometryCenter();
    const double currentRadius =
        std::hypot(point.x - center.x, point.y - center.y);
    float value = std::clamp(
        _velocityStartValue +
            static_cast<float>((currentRadius - _velocityStartRadius) / 72.0),
        0.0f, 1.0f);
    _geometryGestureChanged |=
        s3g::tracker::setGeometryVelocity(track, _gestureRow, value);
    invalidate();
    return;
  }
  std::size_t lane = 0u;
  std::size_t row = 0u;
  if (!geometryCellAtPoint(point, &lane, &row, nullptr))
    return;
  if (static_cast<int>(row) == _lastGestureRow && lane == _gestureLane)
    return;
  _gestureLane = lane;
  _gestureRow = row;
  _lastGestureRow = static_cast<int>(row);
  if (lane >= pattern.tracks.size())
    return;
  auto &destination = pattern.tracks[lane];
  const bool paint = _geometryGestureKind == GeometryGesturePaint;
  _geometryGestureChanged |= s3g::tracker::setGeometryHit(
      destination, row, paint,
      s3g::tracker::laneDefaultNote(trackerState->session, lane));
  trackerState->session.selectedTrack = lane;
  trackerState->session.selectedRow = row;
  invalidate();
}

void GeometryEditor::mouseUp(const GeometryInput &event) {
  (void)event;
  finishGeometryGesture();
}

void GeometryEditor::keyDown(const GeometryInput &event) {
  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);
  if (!model || !pattern || pattern->tracks.empty()) {
    /* unhandled host key */;
    return;
  }
  const auto editingModifiers = event.modifiers & (Command | Control | Alt);
  if (editingModifiers != 0u) {
    /* unhandled host key */;
    return;
  }
  std::string key = event.text;
  if ((key == " ")) {
    togglePlayback();
    return;
  }
  if (geometryViewMode == GeometryModeBurst) {
    auto &burst = model->session.burstLibrary.bursts[_selectedBurstSlot];
    if (!burst.empty()) {
      if (event.key == GridKey::Tab) {
        const bool reverse = (event.modifiers & Shift) != 0u;
        _selectedBurstField = reverse ? (_selectedBurstField + 3) % 4
                                      : (_selectedBurstField + 1) % 4;
        invalidate();
        return;
      }
      if (event.key == GridKey::Down || event.key == GridKey::Up) {
        if (event.key == GridKey::Down)
          _selectedBurstEvent = std::min<std::size_t>(_selectedBurstEvent + 1u,
                                                      burst.eventCount - 1u);
        else if (_selectedBurstEvent > 0u)
          --_selectedBurstEvent;
        invalidate();
        return;
      }
      if (event.key == GridKey::Left || event.key == GridKey::Right) {
        if (!canEditDisplayedPattern())
          return;
        const int direction = event.key == GridKey::Left ? -1 : 1;
        const bool coarse = (event.modifiers & Shift) != 0u;
        auto &authored = burst.events[_selectedBurstEvent];
        bool changed = false;
        if (_selectedBurstField == 0) {
          const int delta = direction * (coarse ? 4096 : 1024);
          const int minimum =
              _selectedBurstEvent == 0u
                  ? 0
                  : burst.events[_selectedBurstEvent - 1u].position;
          const int maximum =
              _selectedBurstEvent + 1u >= burst.eventCount
                  ? 65535
                  : burst.events[_selectedBurstEvent + 1u].position;
          const auto value = static_cast<uint16_t>(std::clamp(
              static_cast<int>(authored.position) + delta, minimum, maximum));
          changed = value != authored.position;
          authored.position = value;
        } else if (_selectedBurstField == 1) {
          const int value = std::clamp(static_cast<int>(authored.note) +
                                           direction * (coarse ? 12 : 1),
                                       0, 127);
          changed = value != authored.note;
          authored.note = static_cast<uint8_t>(value);
        } else if (_selectedBurstField == 2) {
          const int value = std::clamp(static_cast<int>(authored.velocity) +
                                           direction * (coarse ? 10 : 1),
                                       1, 127);
          changed = value != authored.velocity;
          authored.velocity = static_cast<uint8_t>(value);
        } else {
          const int value = std::clamp(static_cast<int>(authored.gatePercent) +
                                           direction * (coarse ? 10 : 1),
                                       1, 100);
          changed = value != authored.gatePercent;
          authored.gatePercent = static_cast<uint8_t>(value);
        }
        if (changed)
          publish();
        invalidate();
        return;
      }
    }
  }
  std::vector<std::string> toolKeys = {"s", "p", "e", "v"};
  const auto toolIndex = indexOf(toolKeys, key);
  if (geometryViewMode == GeometryModeRingField &&
      toolIndex != std::string::npos && toolIndex < toolButtons.size() &&
      toolButtons[toolIndex].enabled) {
    toolChanged(toolButtons[toolIndex]);
    return;
  }
  if ((key == "-")) {
    setGeometryZoomAndRedraw(geometryZoom / 1.15);
    return;
  }
  if ((key == "+") || (key == "=")) {
    setGeometryZoomAndRedraw(geometryZoom * 1.15);
    return;
  }
  const auto visible = visibleGeometryLanes(model);
  if (visible.count == 0u) {
    /* unhandled host key */;
    return;
  }
  auto selectedOrdinal = visible.count;
  for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
    if (visible.indices[ordinal] == model->session.selectedTrack) {
      selectedOrdinal = ordinal;
      break;
    }
  }
  if (event.key == GridKey::Left || event.key == GridKey::Up) {
    if (selectedOrdinal == visible.count)
      selectedOrdinal = 0u;
    else if (selectedOrdinal > 0u)
      --selectedOrdinal;
    selectLane(visible.indices[selectedOrdinal]);
    return;
  }
  if (event.key == GridKey::Right || event.key == GridKey::Down) {
    if (selectedOrdinal == visible.count)
      selectedOrdinal = 0u;
    else
      selectedOrdinal = std::min(selectedOrdinal + 1u, visible.count - 1u);
    selectLane(visible.indices[selectedOrdinal]);
    return;
  }
  /* unhandled host key */;
}

} // namespace s3g::tracker::editor
