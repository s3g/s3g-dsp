#include "editor_geometry_support.h"
#include "s3g/tracker/editor_geometry.h"

namespace s3g::tracker::editor {
using namespace geometry_support;

layout::TrackerGeometryFamilyLayout GeometryEditor::geometryLayout() {
  return layout::trackerGeometryFamilyLayout(
      {
          static_cast<double>(rWidth(bounds_)),
          static_cast<double>(rHeight(bounds_)),
      },
      geometryViewMode == GeometryModePitchMap ? 7u : 4u,
      geometryViewMode == GeometryModeBurst ? 11u : 7u, !burstLibraryOnly);
}

Rect GeometryEditor::canvasRect() {
  return logicalRect(geometryLayout().fieldPanel);
}

Rect GeometryEditor::canvasPlotRect() {
  const Rect canvas = canvasRect();
  return makeRect(
      rMinX(canvas) + 1.0,
      rMinY(canvas) + layout::kStandardMetrics.headerHeight + 1.0,
      std::max<double>(1.0, rWidth(canvas) - 2.0),
      std::max<double>(1.0, rHeight(canvas) -
                                layout::kStandardMetrics.headerHeight - 2.0));
}

Rect GeometryEditor::inspectorRect() {
  const auto geometry = geometryLayout();
  return makeRect(geometry.inspectorColumn.x, geometry.inspectorColumn.top,
                  geometry.inspectorColumn.width,
                  rHeight(bounds_) - geometry.inspectorColumn.top - 18.0);
}

Rect GeometryEditor::laneCyclePanelRect() {
  return logicalRect(geometryLayout().laneCycle.frame);
}

Rect GeometryEditor::editPanelRect() {
  return logicalRect(geometryLayout().editShape.frame);
}

Rect GeometryEditor::viewPanelRect() {
  return logicalRect(geometryLayout().view.frame);
}

Rect GeometryEditor::bridgePanelRect() {
  return logicalRect(geometryLayout().trackerBridge.frame);
}

layout::Panel GeometryEditor::burstAuditionPanelLayout() {
  layout::Panel panel = geometryLayout().trackerBridge;
  panel.frame.height = layout::toolboxHeightForRows(1u);
  panel.rowCount = 1u;
  return panel;
}

layout::Panel GeometryEditor::burstPlacementPanelLayout() {
  const auto bridge = geometryLayout().trackerBridge;
  const auto audition = burstAuditionPanelLayout();
  layout::Panel panel = bridge;
  panel.frame.y = audition.frame.y + audition.frame.height +
                  layout::kStandardMetrics.panelGap;
  panel.frame.height = bridge.frame.y + bridge.frame.height - panel.frame.y;
  panel.rowCount = 4u;
  return panel;
}

Rect GeometryEditor::burstPreviewChannelMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(burstAuditionPanelLayout(), 0u));
}

Rect GeometryEditor::geometrySliderTrackForRow(uint32_t row) {
  const auto panel = geometryLayout().laneCycle;
  const double panelX = static_cast<double>(panel.frame.x);
  return makeRect(
      static_cast<double>(layout::processorControlX(panelX)),
      static_cast<double>(layout::rowY(panel, row)) + 1.0,
      static_cast<double>(layout::processorTrackWidth(panel.frame.width)), 9.0);
}

Rect GeometryEditor::lengthSliderTrack() {
  return geometrySliderTrackForRow(2u);
}

Rect GeometryEditor::defaultNoteSliderTrack() {
  const auto panel = geometryLayout().laneCycle;
  const double panelX = static_cast<double>(panel.frame.x);
  return makeRect(static_cast<double>(layout::processorControlX(panelX)),
                  static_cast<double>(layout::rowY(panel, 1u)) + 1.0,
                  static_cast<double>(layout::processorTrackWidth(
                      panel.frame.width, kGeometryNoteValueWidth)),
                  9.0);
}

Rect GeometryEditor::rotateSliderTrack() {
  return geometrySliderTrackForRow(4u);
}

Rect GeometryEditor::densitySliderTrack() {
  return geometrySliderTrackForRow(5u);
}

Rect GeometryEditor::sliderHitRect(Rect track) {
  const auto panel = geometryLayout().laneCycle;
  uint32_t row = 1u;
  if (rEqual(track, lengthSliderTrack()))
    row = 2u;
  else if (rEqual(track, rotateSliderTrack()))
    row = 4u;
  else if (rEqual(track, densitySliderTrack()))
    row = 5u;
  return logicalRect(layout::sliderHitRect(panel, row));
}

Rect GeometryEditor::laneMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 0u));
}

Rect GeometryEditor::directionMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 3u));
}

Rect GeometryEditor::viewMenuBoxRect() {
  return logicalRect(layout::processorMenuBoxRect(geometryLayout().view, 0u));
}

Rect GeometryEditor::morphTargetMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().editShape, 2u));
}

Rect GeometryEditor::linkVelocityLengthToggleRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 6u));
}

Rect GeometryEditor::burstSlotMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 1u));
}

Rect GeometryEditor::burstBankMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 0u));
}

Rect GeometryEditor::burstEventMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().editShape, 0u));
}

Rect GeometryEditor::burstNameBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 2u));
}

Rect GeometryEditor::pitchScopeMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 1u));
}

Rect GeometryEditor::pitchRootMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 2u));
}

Rect GeometryEditor::pitchScaleMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().laneCycle, 3u));
}

Rect GeometryEditor::pitchMinimumSliderTrack() {
  const auto panel = geometryLayout().laneCycle;
  return makeRect(static_cast<double>(layout::processorControlX(panel.frame.x)),
                  static_cast<double>(layout::rowY(panel, 4u)) + 1.0,
                  static_cast<double>(layout::processorTrackWidth(
                      panel.frame.width, kGeometryNoteValueWidth)),
                  9.0);
}

Rect GeometryEditor::pitchMaximumSliderTrack() {
  Rect track = pitchMinimumSliderTrack();
  track.y =
      static_cast<double>(layout::rowY(geometryLayout().laneCycle, 5u)) + 1.0;
  return track;
}

Rect GeometryEditor::pitchContourMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().editShape, 0u));
}

Rect GeometryEditor::pitchLeapMenuBoxRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().editShape, 1u));
}

Rect GeometryEditor::pitchVariationSliderTrack() {
  const auto panel = geometryLayout().editShape;
  return makeRect(
      static_cast<double>(layout::processorControlX(panel.frame.x)),
      static_cast<double>(layout::rowY(panel, 2u)) + 1.0,
      static_cast<double>(layout::processorTrackWidth(panel.frame.width)), 9.0);
}

Rect GeometryEditor::pitchTransposeSliderTrack() {
  const auto panel = geometryLayout().editShape;
  return makeRect(
      static_cast<double>(layout::processorControlX(panel.frame.x)),
      static_cast<double>(layout::rowY(panel, 4u)) + 1.0,
      static_cast<double>(layout::processorTrackWidth(panel.frame.width)), 9.0);
}

Rect GeometryEditor::pitchInvertToggleRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().editShape, 5u));
}

Rect GeometryEditor::pitchReverseToggleRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().editShape, 6u));
}

Rect GeometryEditor::pitchAnchorToggleRect() {
  return logicalRect(
      layout::processorMenuBoxRect(geometryLayout().editShape, 3u));
}

Rect GeometryEditor::pitchAnalyzeHeaderButtonRect() {
  const Rect header = laneCyclePanelRect();
  return makeRect(rMaxX(header) - 86.0, rMinY(header) + 3.0, 74.0, 15.0);
}

Rect GeometryEditor::pitchNewSeedHeaderButtonRect() {
  const Rect header = editPanelRect();
  return makeRect(rMaxX(header) - 88.0, rMinY(header) + 3.0, 76.0, 15.0);
}

Rect GeometryEditor::pitchPreviewHeaderButtonRect() {
  const Rect header = editPanelRect();
  return makeRect(rMaxX(header) - 164.0, rMinY(header) + 3.0, 70.0, 15.0);
}

Rect GeometryEditor::pitchApplyHeaderButtonRect() {
  const Rect header = bridgePanelRect();
  return makeRect(rMaxX(header) - 76.0, rMinY(header) + 3.0, 64.0, 15.0);
}

Rect GeometryEditor::pitchGraphRect() {
  const Rect plot = canvasPlotRect();
  const double top = rMinY(plot) + 42.0;
  const double bottom = rMaxY(plot) - 42.0;
  constexpr double gap = 62.0;
  const double available = std::max<double>(120.0, bottom - top - gap);
  const double height = std::floor(available * 0.58);
  return makeRect(rMinX(plot) + 54.0, top,
                  std::max<double>(1.0, rWidth(plot) - 108.0), height);
}

Rect GeometryEditor::pitchIntervalGraphRect() {
  const Rect plot = canvasPlotRect();
  const Rect contour = pitchGraphRect();
  constexpr double gap = 62.0;
  const double y = rMaxY(contour) + gap;
  return makeRect(rMinX(contour), y, rWidth(contour),
                  std::max<double>(80.0, rMaxY(plot) - 42.0 - y));
}

bool GeometryEditor::pitchMapNoteMatchesScale(uint8_t note) {
  const auto &scale = s3g::musicalScaleDefinition(_pitchSettings.scale);
  const int effectiveRoot =
      (static_cast<int>(_pitchSettings.rootPitchClass) +
       static_cast<int>(_pitchSettings.transposeSemitones) + 120) %
      12;
  const uint8_t relative = static_cast<uint8_t>(
      (note + 12u - static_cast<uint8_t>(effectiveRoot)) % 12u);
  for (uint32_t degree = 0u; degree < scale.size; ++degree)
    if (static_cast<uint8_t>(scale.semitones[degree]) == relative)
      return true;
  return false;
}

int GeometryEditor::pitchScaleOrdinalForNote(int note) {
  note = std::clamp(note, 0, 127);
  int ordinal = 0;
  int nearestOrdinal = 0;
  int nearestDistance = 128;
  for (int candidate = 0; candidate <= 127; ++candidate) {
    if (!pitchMapNoteMatchesScale(static_cast<uint8_t>(candidate)))
      continue;
    const int distance = std::abs(candidate - note);
    if (distance < nearestDistance) {
      nearestDistance = distance;
      nearestOrdinal = ordinal;
    }
    if (candidate == note)
      return ordinal;
    ++ordinal;
  }
  return nearestOrdinal;
}

int GeometryEditor::pitchScaleNoteForOrdinal(int requestedOrdinal) {
  int noteCount = 0;
  for (int note = 0; note <= 127; ++note)
    if (pitchMapNoteMatchesScale(static_cast<uint8_t>(note)))
      ++noteCount;
  if (noteCount == 0)
    return 60;
  requestedOrdinal = std::clamp(requestedOrdinal, 0, noteCount - 1);
  int ordinal = 0;
  for (int note = 0; note <= 127; ++note) {
    if (!pitchMapNoteMatchesScale(static_cast<uint8_t>(note)))
      continue;
    if (ordinal == requestedOrdinal)
      return note;
    ++ordinal;
  }
  return 127;
}

int GeometryEditor::pitchIntervalAtIndex(std::size_t index, bool original) {
  if (index == 0u || index >= _pitchPreview.assignments.size())
    return 0;
  const auto &previous = _pitchPreview.assignments[index - 1u];
  const auto &current = _pitchPreview.assignments[index];
  const int previousNote = original ? previous.originalNote : previous.note;
  const int currentNote = original ? current.originalNote : current.note;
  return pitchScaleOrdinalForNote(currentNote) -
         pitchScaleOrdinalForNote(previousNote);
}

int GeometryEditor::pitchIntervalExtent() {
  int extent = std::max<int>(4, _pitchSettings.maximumLeapDegrees);
  for (std::size_t index = 1u; index < _pitchPreview.assignments.size();
       ++index) {
    extent = std::max(extent, std::abs(pitchIntervalAtIndex(index, false)));
    extent = std::max(extent, std::abs(pitchIntervalAtIndex(index, true)));
  }
  return extent;
}

int GeometryEditor::pitchMapDisplayMinimum() {
  int minimum = std::min<int>(
      _pitchSettings.minimumNote,
      std::clamp<int>(static_cast<int>(_pitchSettings.minimumNote) +
                          _pitchSettings.transposeSemitones,
                      0, 127));
  for (const auto &assignment : _pitchPreview.assignments) {
    for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice) {
      minimum = std::min<int>(minimum, assignment.originalNotes[voice]);
      minimum = std::min<int>(minimum, assignment.notes[voice]);
    }
  }
  return minimum;
}

int GeometryEditor::pitchMapDisplayMaximum() {
  int maximum = std::max<int>(
      _pitchSettings.maximumNote,
      std::clamp<int>(static_cast<int>(_pitchSettings.maximumNote) +
                          _pitchSettings.transposeSemitones,
                      0, 127));
  for (const auto &assignment : _pitchPreview.assignments) {
    for (std::size_t voice = 0u; voice < assignment.voiceCount; ++voice) {
      maximum = std::max<int>(maximum, assignment.originalNotes[voice]);
      maximum = std::max<int>(maximum, assignment.notes[voice]);
    }
  }
  return maximum;
}

Point GeometryEditor::pitchMapPointForAssignment(
    const PitchMapAssignment &assignment) {
  std::size_t first = 0u;
  std::size_t last = 0u;
  pitchMapRowsFirst(&first, &last);
  const Rect graph = pitchGraphRect();
  const double rowSpan =
      static_cast<double>(std::max<std::size_t>(1u, last - first));
  const int displayMinimum = pitchMapDisplayMinimum();
  const int displayMaximum = pitchMapDisplayMaximum();
  const double pitchSpan =
      static_cast<double>(std::max<int>(1, displayMaximum - displayMinimum));
  return makePoint(rMinX(graph) + static_cast<double>(assignment.row - first) /
                                      rowSpan * rWidth(graph),
                   rMaxY(graph) -
                       static_cast<double>(std::clamp(
                           static_cast<int>(assignment.note) - displayMinimum,
                           0, displayMaximum - displayMinimum)) /
                           pitchSpan * rHeight(graph));
}

Point GeometryEditor::pitchMapPointForAssignmentAtIndex(std::size_t index,
                                                        bool original) {
  return pitchMapPointForAssignmentAtIndex(index, original, false);
}

Point GeometryEditor::pitchMapPointForAssignmentAtIndex(std::size_t index,
                                                        bool original,
                                                        bool interval) {
  if (index >= _pitchPreview.assignments.size())
    return Point{};
  if (!interval) {
    PitchMapAssignment assignment = _pitchPreview.assignments[index];
    if (original)
      assignment.note = assignment.originalNote;
    return pitchMapPointForAssignment(assignment);
  }
  std::size_t first = 0u;
  std::size_t last = 0u;
  pitchMapRowsFirst(&first, &last);
  const Rect graph = pitchIntervalGraphRect();
  const auto row = _pitchPreview.assignments[index].row;
  const double x =
      rMinX(graph) +
      static_cast<double>(row - first) /
          static_cast<double>(std::max<std::size_t>(1u, last - first)) *
          rWidth(graph);
  const int extent = pitchIntervalExtent();
  const int degreeInterval = pitchIntervalAtIndex(index, original);
  const double usableHalfHeight =
      std::max<double>(1.0, rHeight(graph) * 0.5 - 12.0);
  const double y = rMidY(graph) - static_cast<double>(degreeInterval) /
                                      static_cast<double>(std::max(1, extent)) *
                                      usableHalfHeight;
  return makePoint(x, y);
}

int GeometryEditor::pitchMapAssignmentAtPoint(Point point) {
  refreshPitchMapPreview();
  const bool interval = rContains(point, pitchIntervalGraphRect());
  if (!interval && !rContains(point, pitchGraphRect()))
    return -1;
  int result = -1;
  double best = 12.0;
  for (std::size_t index = 0u; index < _pitchPreview.assignments.size();
       ++index) {
    const Point marker =
        pitchMapPointForAssignmentAtIndex(index, false, interval);
    const double distance = std::hypot(point.x - marker.x, point.y - marker.y);
    if (distance >= best)
      continue;
    best = distance;
    result = static_cast<int>(index);
  }
  if (result >= 0)
    _pitchEditingIntervals = interval;
  return result;
}

std::string GeometryEditor::pitchSelectedPointFlagText() {
  auto *model = trackerState;
  if (!model)
    return 0;
  refreshPitchMapPreview();
  for (const auto &assignment : _pitchPreview.assignments) {
    if (assignment.row != model->session.selectedRow)
      continue;
    return pitchAssignmentText(assignment);
  }
  return 0;
}

void GeometryEditor::updatePitchMapPointAtPoint(Point point) {
  if (_pitchDragAssignment < 0 ||
      static_cast<std::size_t>(_pitchDragAssignment) >=
          _pitchPreview.assignments.size())
    return;
  const auto assignmentIndex = static_cast<std::size_t>(_pitchDragAssignment);
  if (_pitchEditingIntervals) {
    if (assignmentIndex == 0u) {
      _pitchStatus = "FIRST NOTE IS THE INTERVAL ANCHOR";
      invalidate();
      return;
    }
    const Rect intervalGraph = pitchIntervalGraphRect();
    const double usableHalfHeight =
        std::max<double>(1.0, rHeight(intervalGraph) * 0.5 - 12.0);
    const int extent = pitchIntervalExtent();
    const int requestedInterval =
        std::clamp<int>(static_cast<int>(std::lround(
                            (rMidY(intervalGraph) - point.y) /
                            usableHalfHeight * static_cast<double>(extent))),
                        -extent, extent);
    const int previousOrdinal = pitchScaleOrdinalForNote(
        _pitchPreview.assignments[assignmentIndex - 1u].note);
    const int currentOrdinal = pitchScaleOrdinalForNote(
        _pitchPreview.assignments[assignmentIndex].note);
    int ordinalShift = previousOrdinal + requestedInterval - currentOrdinal;

    int minimumAllowedOrdinal = 128;
    int maximumAllowedOrdinal = -1;
    for (int note = pitchMapDisplayMinimum(); note <= pitchMapDisplayMaximum();
         ++note) {
      if (!pitchMapNoteMatchesScale(static_cast<uint8_t>(note)))
        continue;
      const int ordinal = pitchScaleOrdinalForNote(note);
      minimumAllowedOrdinal = std::min(minimumAllowedOrdinal, ordinal);
      maximumAllowedOrdinal = std::max(maximumAllowedOrdinal, ordinal);
    }
    int minimumTailOrdinal = 128;
    int maximumTailOrdinal = -1;
    for (std::size_t index = assignmentIndex;
         index < _pitchPreview.assignments.size(); ++index) {
      const int ordinal =
          pitchScaleOrdinalForNote(_pitchPreview.assignments[index].note);
      minimumTailOrdinal = std::min(minimumTailOrdinal, ordinal);
      maximumTailOrdinal = std::max(maximumTailOrdinal, ordinal);
    }
    if (maximumAllowedOrdinal >= minimumAllowedOrdinal &&
        maximumTailOrdinal >= minimumTailOrdinal) {
      ordinalShift =
          std::clamp(ordinalShift, minimumAllowedOrdinal - minimumTailOrdinal,
                     maximumAllowedOrdinal - maximumTailOrdinal);
    }
    if (ordinalShift != 0) {
      for (std::size_t index = assignmentIndex;
           index < _pitchPreview.assignments.size(); ++index) {
        const int ordinal =
            pitchScaleOrdinalForNote(_pitchPreview.assignments[index].note);
        const int note = pitchScaleNoteForOrdinal(ordinal + ordinalShift);
        const auto row = _pitchPreview.assignments[index].row;
        if (row < _pitchOverrides.size())
          _pitchOverrides[row] = static_cast<int16_t>(note);
      }
      _geometryGestureChanged = true;
      refreshPitchMapPreview();
    }
    const int resolvedInterval = pitchIntervalAtIndex(assignmentIndex, false);
    _pitchStatus =
        format("INTERVAL %+d DEG · FOLLOWING NOTES SHIFTED", resolvedInterval);
    invalidate();
    return;
  }
  const Rect graph = pitchGraphRect();
  const double normalized = std::clamp(
      (rMaxY(graph) - point.y) / std::max<double>(1.0, rHeight(graph)), 0.0,
      1.0);
  const int displayMinimum = pitchMapDisplayMinimum();
  const int displayMaximum = pitchMapDisplayMaximum();
  const int raw = static_cast<int>(std::lround(
      static_cast<double>(displayMinimum) +
      normalized * static_cast<double>(displayMaximum - displayMinimum)));
  int nearest = raw;
  int best = 128;
  for (int note = displayMinimum; note <= displayMaximum; ++note) {
    if (!pitchMapNoteMatchesScale(static_cast<uint8_t>(note)))
      continue;
    const int distance = std::abs(note - raw);
    if (distance >= best)
      continue;
    best = distance;
    nearest = note;
  }
  if (best == 128)
    return;
  const auto row = _pitchPreview.assignments[assignmentIndex].row;
  if (row < _pitchOverrides.size())
    _pitchOverrides[row] = static_cast<int16_t>(nearest);
  _pitchStatus = "POINT OVERRIDE · SCALE SNAPPED";
  refreshPitchMapPreview();
  invalidate();
}

Rect GeometryEditor::burstSliderTrackForRow(uint32_t row) {
  const auto panel = geometryLayout().editShape;
  return makeRect(
      static_cast<double>(layout::processorControlX(panel.frame.x)),
      static_cast<double>(layout::rowY(panel, row)) + 1.0,
      static_cast<double>(layout::processorTrackWidth(panel.frame.width, 72.0)),
      9.0);
}

Rect GeometryEditor::burstActionRectForRow(uint32_t row, std::size_t index,
                                           std::size_t count) {
  const auto panel = geometryLayout().laneCycle;
  const Rect controls = logicalRect(layout::processorMenuBoxRect(panel, row));
  const double gap = 3.0;
  const double width =
      (rWidth(controls) - gap * static_cast<double>(count - 1u)) /
      static_cast<double>(count);
  return makeRect(rMinX(controls) + static_cast<double>(index) * (width + gap),
                  rMinY(controls), width, rHeight(controls));
}

Rect GeometryEditor::editToolButtonRect(std::size_t index) {
  const auto panel = geometryLayout().editShape;
  const Rect controls = logicalRect(layout::processorMenuBoxRect(panel, 0u));
  const double gap = 3.0;
  const double width = (rWidth(controls) - gap * 3.0) * 0.25;
  return makeRect(rMinX(controls) + (width + gap) * index, rMinY(controls),
                  width, rHeight(controls));
}

Rect GeometryEditor::reverseButtonRect() {
  const auto panel = geometryLayout().editShape;
  Rect controls = logicalRect(layout::processorMenuBoxRect(panel, 1u));
  controls.width = (controls.width - 4.0) * 0.5;
  return controls;
}

Rect GeometryEditor::reflectButtonRect() {
  Rect rect = reverseButtonRect();
  rect.x = rMaxX(rect) + 4.0;
  return rect;
}

Rect GeometryEditor::morphAmountButtonRect(std::size_t index) {
  const auto panel = geometryLayout().editShape;
  const Rect controls = logicalRect(layout::processorMenuBoxRect(panel, 3u));
  const double gap = 3.0;
  const double width = (rWidth(controls) - gap * 3.0) * 0.25;
  return makeRect(rMinX(controls) + (width + gap) * index, rMinY(controls),
                  width, rHeight(controls));
}

Rect GeometryEditor::revealHeaderButtonRect() {
  const Rect header = geometryViewMode == GeometryModeBurst
                          ? logicalRect(burstPlacementPanelLayout().frame)
                          : bridgePanelRect();
  return makeRect(rMaxX(header) - 154.0, rMinY(header) + 3.0, 142.0, 15.0);
}

Rect GeometryEditor::fitBurstGatesHeaderButtonRect() {
  const Rect header = editPanelRect();
  return makeRect(rMaxX(header) - 128.0, rMinY(header) + 3.0, 116.0, 15.0);
}

Rect GeometryEditor::burstPreviewHeaderButtonRect() {
  const Rect header = logicalRect(burstAuditionPanelLayout().frame);
  return makeRect(rMaxX(header) - 90.0, rMinY(header) + 3.0, 78.0, 15.0);
}

Rect GeometryEditor::burstLoopHeaderButtonRect() {
  Rect rect = burstPreviewHeaderButtonRect();
  rect.x -= rWidth(rect) + 4.0;
  return rect;
}

Rect GeometryEditor::burstRenameHeaderButtonRect() {
  const Rect header = laneCyclePanelRect();
  return makeRect(rMaxX(header) - 82.0, rMinY(header) + 3.0, 70.0, 15.0);
}

} // namespace s3g::tracker::editor
