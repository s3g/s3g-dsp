#include "editor_geometry_support.h"
#include "s3g/tracker/editor_geometry.h"

namespace s3g::tracker::editor {
using namespace geometry_support;

std::size_t GeometryEditor::allStepsUnderlayNodeCount() {
  if (geometryViewMode != GeometryModeAllStepsUnderlay)
    return 0u;
  const auto *pattern = geometryPattern(trackerState);
  const auto visible = visibleGeometryLanes(trackerState);
  if (!pattern)
    return 0u;
  std::size_t count = 0u;
  for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
    const auto lane = visible.indices[ordinal];
    if (lane >= pattern->tracks.size())
      continue;
    count += std::clamp<std::size_t>(pattern->tracks[lane].noteColumn.length,
                                     1u, 256u);
  }
  return static_cast<std::size_t>(count);
}

void GeometryEditor::drawBurstWorkspace() {
  auto *model = trackerState;
  if (!model)
    return;
  const auto style = softTextStyle();
  TextStyle labels = softLabelAttrs();
  TextStyle values = softValueAttrs();
  const auto geometry = geometryLayout();
  const Rect canvas = canvasRect();
  auto &pattern = model->session.pattern;
  auto &burst = model->session.burstLibrary.bursts[_selectedBurstSlot];
  _selectedBurstEvent =
      burst.empty()
          ? 0u
          : std::min<std::size_t>(_selectedBurstEvent, burst.eventCount - 1u);

  const Rect matrix = burstMatrixRect();
  const Rect overview = burstOverviewRect();
  const Rect breakpoints = burstBreakpointRect();
  fillRect(matrix, themeColor(ThemeRole::Canvas, 0.72));
  strokeRect(rInsetRect(matrix, 0.5, 0.5), themeColor(ThemeRole::Border));
  const std::vector<std::string> matrixHeaders = {"#", "TIME", "NOTE",
                                                  "VELOCITY", "GATE % ROW"};
  const std::array<int, 5u> headerFields{{-1, 0, 1, 2, 3}};
  for (std::size_t index = 0u; index < headerFields.size(); ++index) {
    Rect header = burstMatrixCellRect(0u, headerFields[index]);
    header.y = rMinY(matrix);
    header.height = 22.0;
    drawCenteredText(matrixHeaders[index], rInsetRect(header, 4.0, 0.0),
                     themeColor(ThemeRole::TextMuted), 6.8,
                     FontWeight::Semibold,
                     index == 0u ? Alignment::Center : Alignment::Left);
  }
  for (std::size_t row = 0u; row < kMaximumBurstEvents; ++row) {
    const Rect rowRect = burstMatrixRowRect(row);
    const bool active = row < burst.eventCount;
    const bool selected = active && row == _selectedBurstEvent;
    fillRect(rowRect, themeColor(selected ? ThemeRole::Selection
                                          : row % 2u == 0u ? ThemeRole::Raised
                                                           : ThemeRole::Canvas,
                                 selected ? 0.40 : 0.66));
    strokeRect(rInsetRect(rowRect, 0.5, 0.5),
               themeColor(ThemeRole::Grid, 0.72));
    drawCenteredText(
        format("%02lu", static_cast<unsigned long>(row + 1u)),
        burstMatrixCellRect(row, -1),
        themeColor(selected ? ThemeRole::TextPrimary : ThemeRole::TextFaint),
        7.4, selected ? FontWeight::Semibold : FontWeight::Medium);
    for (int field = 0; field < 4; ++field) {
      const Rect cell = burstMatrixCellRect(row, field);
      strokeRect(rInsetRect(cell, 0.5, 0.5), themeColor(ThemeRole::Grid, 0.48));
      if (selected && field == _selectedBurstField)
        strokeRect(rInsetRect(cell, 1.5, 1.5), themeColor(ThemeRole::Selection),
                   1.4);
    }
    if (!active) {
      drawCenteredText(row == burst.eventCount ? "+ ADD EVENT" : "—",
                       makeRect(rMinX(rowRect) + 38.0, rMinY(rowRect),
                                rWidth(rowRect) - 38.0, rHeight(rowRect)),
                       themeColor(ThemeRole::TextFaint), 7.0,
                       FontWeight::Medium);
      continue;
    }
    const auto &authored = burst.events[row];
    const double time = static_cast<double>(authored.position) / 65535.0;
    const std::array<std::string, 4u> cellText{{
        format("%05.1f%%", time * 100.0),
        format("%s · %03u", midiNoteName(authored.note), authored.note),
        format("%03u", authored.velocity),
        format("%03u%%", authored.gatePercent),
    }};
    const double velocityFraction =
        static_cast<double>(authored.velocity) / 127.0;
    const double gateFraction =
        static_cast<double>(authored.gatePercent) / 100.0;
    const double noteFraction = static_cast<double>(authored.note) / 127.0;
    const std::array<double, 4u> sliderFractions{{
        time,
        noteFraction,
        velocityFraction,
        gateFraction,
    }};
    for (int field = 0; field < 4; ++field) {
      const Rect cell = burstMatrixCellRect(row, field);
      Color color =
          field == 1
              ? trackerColor(kLaneColors[authored.note % kLaneColors.size()])
              : field == 0 ? themeColor(ThemeRole::Focus)
                           : field == 2 ? themeColor(ThemeRole::Value)
                                        : themeColor(ThemeRole::TextSecondary);
      const Rect miniTrack = makeRect(rMinX(cell) + 4.0, rMaxY(cell) - 6.0,
                                      rWidth(cell) - 8.0, 2.5);
      fillRect(miniTrack, themeColor(ThemeRole::Control, 0.96));
      fillRect(
          makeRect(
              rMinX(miniTrack), rMinY(miniTrack),
              std::max<double>(
                  1.0, rWidth(miniTrack) *
                           sliderFractions[static_cast<std::size_t>(field)]),
              rHeight(miniTrack)),
          withAlpha(color, selected ? 1.0 : 0.88));
      drawCenteredText(cellText[static_cast<std::size_t>(field)],
                       rInsetRect(cell, 7.0, 0.0), color, 8.0,
                       selected ? FontWeight::Semibold : FontWeight::Medium,
                       Alignment::Left);
    }
  }

  fillRect(overview, themeColor(ThemeRole::Canvas, 0.72));
  strokeRect(rInsetRect(overview, 0.5, 0.5), themeColor(ThemeRole::Border));
  drawText("RADIAL OVERVIEW",
           makeRect(rMinX(overview) + 8.0, rMinY(overview) + 7.0,
                    rWidth(overview) - 16.0, 11.0),
           themeColor(ThemeRole::TextMuted), 6.8, FontWeight::Semibold);
  const Rect radialPlot = burstRadialPlotRect();
  const Point center = makePoint(rMidX(radialPlot), rMidY(radialPlot));
  const double radius =
      std::max<double>(50.0, std::min(rWidth(radialPlot), rHeight(radialPlot)) *
                                 0.34 * geometryZoom);
  for (std::size_t index = 0u; index < 16u; ++index) {
    const double angle = -static_cast<double>(kGeometryHalfPi) +
                         static_cast<double>(index) * 2.0 *
                             static_cast<double>(kGeometryPi) / 16.0;
    GeometryPath spoke = GeometryPath{};
    spoke.moveToPoint(makePoint(center.x + std::cos(angle) * (radius - 8.0),
                                center.y + std::sin(angle) * (radius - 8.0)));
    spoke.lineToPoint(makePoint(
        center.x + std::cos(angle) * (radius + (index % 4u == 0u ? 9.0 : 4.0)),
        center.y +
            std::sin(angle) * (radius + (index % 4u == 0u ? 9.0 : 4.0))));
    spoke.lineWidth = index % 4u == 0u ? 1.0 : 0.55;
    (strokeColor_ =
         themeColor(ThemeRole::Grid, index % 4u == 0u ? 0.90 : 0.55));
    strokePath(spoke);
  }
  GeometryPath ring = GeometryPath::ellipse(makeRect(
      center.x - radius, center.y - radius, radius * 2.0, radius * 2.0));
  ring.lineWidth = 1.2;
  (strokeColor_ = themeColor(ThemeRole::BorderStrong));
  strokePath(ring);
  if (burst.empty()) {
    drawCenteredText("SELECT AN EMPTY MATRIX ROW TO CREATE", radialPlot,
                     themeColor(ThemeRole::TextFaint), 11.0,
                     FontWeight::Medium);
  } else {
    GeometryPath phrase = GeometryPath{};
    for (std::size_t index = 0u; index < burst.eventCount; ++index) {
      const auto &event = burst.events[index];
      const double phase = static_cast<double>(event.position) / 65536.0;
      const double angle = -static_cast<double>(kGeometryHalfPi) +
                           phase * 2.0 * static_cast<double>(kGeometryPi);
      const double eventRadius =
          radius + (static_cast<double>(event.note) / 127.0 - 0.5) * 30.0;
      const Point point = makePoint(center.x + std::cos(angle) * eventRadius,
                                    center.y + std::sin(angle) * eventRadius);
      if (index == 0u)
        phrase.moveToPoint(point);
      else
        phrase.lineToPoint(point);
      const double bead =
          3.4 + static_cast<double>(event.velocity) / 127.0 * 4.2;
      GeometryPath marker = GeometryPath::ellipse(
          makeRect(point.x - bead, point.y - bead, bead * 2.0, bead * 2.0));
      Color identity =
          trackerColor(kLaneColors[event.note % kLaneColors.size()], 1.0);
      (fillColor_ = themeColor(ThemeRole::Canvas, 0.94));
      fillPath(GeometryPath::ellipse(rInsetRect(marker.bounds, -1.8, -1.8)));
      (fillColor_ = identity);
      fillPath(marker);
      if (index == _selectedBurstEvent) {
        GeometryPath halo =
            GeometryPath::ellipse(rInsetRect(marker.bounds, -4.0, -4.0));
        halo.lineWidth = 1.4;
        (strokeColor_ = themeColor(ThemeRole::TextPrimary));
        strokePath(halo);
      }
      drawCenteredText(format("%lu", static_cast<unsigned long>(index + 1u)),
                       makeRect(point.x - 8.0, point.y - 7.0, 16.0, 14.0),
                       themeColor(ThemeRole::Canvas), 6.4,
                       FontWeight::Semibold);
    }
    phrase.lineWidth = 1.2;
    (strokeColor_ = themeColor(ThemeRole::TextMuted, 0.65));
    strokePath(phrase);
    drawCenteredText(
        nsString(burst.name),
        makeRect(rMinX(radialPlot) + 12.0, rMaxY(radialPlot) - 22.0,
                 rWidth(radialPlot) - 24.0, 16.0),
        themeColor(ThemeRole::TextSecondary), 9.0, FontWeight::Semibold);
  }

  fillRect(breakpoints, themeColor(ThemeRole::Canvas, 0.72));
  strokeRect(rInsetRect(breakpoints, 0.5, 0.5), themeColor(ThemeRole::Border));
  drawText(
      "VELOCITY BREAKPOINTS",
      makeRect(rMinX(breakpoints) + 9.0, rMinY(breakpoints) + 7.0, 190.0, 11.0),
      themeColor(ThemeRole::TextMuted), 6.8, FontWeight::Semibold);
  drawText("DRAG POINT = TIME + VELOCITY",
           makeRect(rMaxX(breakpoints) - 230.0, rMinY(breakpoints) + 7.0, 220.0,
                    11.0),
           themeColor(ThemeRole::TextFaint), 6.5, FontWeight::Medium,
           Alignment::Right);
  const Rect graph = rInsetRect(breakpoints, 12.0, 14.0);
  for (std::size_t division = 0u; division <= 4u; ++division) {
    const double x =
        rMinX(graph) + rWidth(graph) * static_cast<double>(division) / 4.0;
    GeometryPath line = GeometryPath{};
    line.moveToPoint(makePoint(x, rMinY(graph) + 10.0));
    line.lineToPoint(makePoint(x, rMaxY(graph)));
    line.lineWidth = division == 0u || division == 4u ? 1.0 : 0.6;
    (strokeColor_ = themeColor(ThemeRole::Grid, 0.72));
    strokePath(line);
  }
  for (std::size_t division = 0u; division <= 4u; ++division) {
    const double y =
        rMinY(graph) + 10.0 +
        (rHeight(graph) - 10.0) * static_cast<double>(division) / 4.0;
    GeometryPath line = GeometryPath{};
    line.moveToPoint(makePoint(rMinX(graph), y));
    line.lineToPoint(makePoint(rMaxX(graph), y));
    line.lineWidth = 0.6;
    (strokeColor_ = themeColor(ThemeRole::Grid, 0.55));
    strokePath(line);
  }
  if (!burst.empty()) {
    GeometryPath velocityPath = GeometryPath{};
    GeometryPath pitchPath = GeometryPath{};
    for (std::size_t index = 0u; index < burst.eventCount; ++index) {
      const auto &authored = burst.events[index];
      const double x = rMinX(graph) + static_cast<double>(authored.position) /
                                          65535.0 * rWidth(graph);
      const double velocityY =
          rMaxY(graph) -
          static_cast<double>(authored.velocity - 1u) / 126.0 * rHeight(graph);
      const double gateEnd =
          x + static_cast<double>(authored.gatePercent) / 100.0 * rWidth(graph);
      GeometryPath gateSpan = GeometryPath{};
      gateSpan.moveToPoint(makePoint(x, velocityY));
      gateSpan.lineToPoint(
          makePoint(std::min(rMaxX(graph), gateEnd), velocityY));
      gateSpan.lineWidth = 3.0;
      (strokeColor_ = themeColor(ThemeRole::TextSecondary, 0.25));
      strokePath(gateSpan);
      if (gateEnd > rMaxX(graph)) {
        drawText("›", makeRect(rMaxX(graph) - 7.0, velocityY - 7.0, 8.0, 14.0),
                 themeColor(ThemeRole::Warning), 8.0, FontWeight::Semibold,
                 Alignment::Right);
      }
      const double pitchY = rMaxY(graph) - static_cast<double>(authored.note) /
                                               127.0 * rHeight(graph);
      if (index == 0u) {
        velocityPath.moveToPoint(makePoint(x, velocityY));
        pitchPath.moveToPoint(makePoint(x, pitchY));
      } else {
        velocityPath.lineToPoint(makePoint(x, velocityY));
        pitchPath.lineToPoint(makePoint(x, pitchY));
      }
    }
    pitchPath.lineWidth = 0.8;
    (strokeColor_ = themeColor(ThemeRole::Note, 0.34));
    strokePath(pitchPath);
    velocityPath.lineWidth = 1.6;
    (strokeColor_ = themeColor(ThemeRole::Value, 0.82));
    strokePath(velocityPath);
    for (std::size_t index = 0u; index < burst.eventCount; ++index) {
      const auto &authored = burst.events[index];
      const double x = rMinX(graph) + static_cast<double>(authored.position) /
                                          65535.0 * rWidth(graph);
      const double y =
          rMaxY(graph) -
          static_cast<double>(authored.velocity - 1u) / 126.0 * rHeight(graph);
      const double pointRadius = index == _selectedBurstEvent ? 5.5 : 4.0;
      GeometryPath point =
          GeometryPath::ellipse(makeRect(x - pointRadius, y - pointRadius,
                                         pointRadius * 2.0, pointRadius * 2.0));
      (fillColor_ = themeColor(ThemeRole::Canvas));
      fillPath(point);
      point.lineWidth = index == _selectedBurstEvent ? 2.0 : 1.2;
      (strokeColor_ =
           themeColor(index == _selectedBurstEvent ? ThemeRole::TextPrimary
                                                   : ThemeRole::Value));
      strokePath(point);
    }
  }
  std::vector<std::string> timeLabels = {"0", "1/4", "1/2", "3/4", "1X"};
  for (std::size_t index = 0u; index < timeLabels.size(); ++index) {
    const double x =
        rMinX(graph) + rWidth(graph) * static_cast<double>(index) / 4.0;
    drawCenteredText(timeLabels[index],
                     makeRect(x - 16.0, rMaxY(graph) - 4.0, 32.0, 12.0),
                     themeColor(ThemeRole::TextFaint), 6.2, FontWeight::Medium);
  }
  drawCenteredText("DRAG CELLS  •  ↑/↓ EVENT  •  TAB FIELD  •  GATE = % OF ROW "
                   " •  PALE TAIL = DURATION  •  › SPILLS",
                   makeRect(rMinX(canvas) + 10.0, rMaxY(canvas) - 28.0,
                            rWidth(canvas) - 20.0, 12.0),
                   themeColor(ThemeRole::TextFaint), 6.8, FontWeight::Medium);

  const double libraryX = geometry.laneCycle.frame.x;
  const double libraryWidth = geometry.laneCycle.frame.width;
  const auto drawBurstInfo = [&](std::string name, std::string value,
                                 const layout::Panel &toolbox, uint32_t row,
                                 Color infoColor) {
    const double x = static_cast<double>(toolbox.frame.x);
    const double width = static_cast<double>(toolbox.frame.width);
    const double y = static_cast<double>(layout::rowY(toolbox, row));
    const double labelX = static_cast<double>(layout::processorLabelX(x));
    const double controlX = static_cast<double>(layout::processorControlX(x));
    const double controlWidth =
        static_cast<double>(layout::processorMenuWidth(width));
    drawAtPoint(uppercase(name), makePoint(labelX, y - 2.0), labels);
    Color color = infoColor;
    TextStyle infoAttrs{color, uiFont(10.0)};
    (fillColor_ = color);
    fillCurrentRect(makeRect(controlX, y, 2.0, 12.0));
    std::string display = menuDisplayText(
        value, std::max<double>(0.0, controlWidth - 12.0), infoAttrs);
    drawAtPoint(display, makePoint(controlX + 8.0, y - 2.0), infoAttrs);
  };
  const auto *activeBurstBank =
      findBurstBank(trackerState->burstBanks, trackerState->activeBurstBankId);
  drawTrackerProcessorMenu("BANK",
                           activeBurstBank ? nsString(activeBurstBank->name)
                                           : "PROJECT BURSTS",
                           layout::rowY(geometry.laneCycle, 0u), libraryX,
                           libraryWidth, labels, values, style);
  drawTrackerProcessorMenu(
      "BURST",
      format("%s  ·  %s", nsString(burstSlotToken(_selectedBurstSlot)),
             burst.empty() ? "EMPTY" : nsString(burst.name)),
      layout::rowY(geometry.laneCycle, 1u), libraryX, libraryWidth, labels,
      values, style);
  drawTrackerProcessorMenu("NAME", burst.empty() ? "—" : nsString(burst.name),
                           layout::rowY(geometry.laneCycle, 2u), libraryX,
                           libraryWidth, labels, values, style);
  drawAtPoint("EVENTS",
              makePoint(layout::processorLabelX(libraryX),
                        layout::rowY(geometry.laneCycle, 3u) - 2.0),
              labels);
  for (std::size_t index = 0u; index < 2u; ++index)
    S3GTrackerDrawSuiteActionButton(
        burstActionRectForRow(3u, index, 2u),
        index == 0u ? format("−  %u", burst.eventCount)
                    : format("%u  +", burst.eventCount),
        true, false, false, false, false, false, false, true);
  drawBurstInfo("USAGE",
                format("%lu NOTE CELLS",
                       static_cast<unsigned long>(projectBurstUsageCount(
                           *trackerState, _selectedBurstSlot))),
                geometry.laneCycle, 4u, themeColor(ThemeRole::TextMuted));
  const std::vector<std::vector<std::string>> actionRows = {
      {"NEW", "DUP", "DELETE"},        {"EVEN", "ACCEL", "DECEL"},
      {"REVERSE", "ROT <", "ROT >"},   {"IMPORT PACK", "EXPORT ONE"},
      {"EXPORT BANK", "COPY PROJECT"}, {"PURGE UNUSED", "DELETE BANK"},
  };
  for (std::size_t rowIndex = 0u; rowIndex < actionRows.size(); ++rowIndex) {
    const auto row = static_cast<uint32_t>(5u + rowIndex);
    const auto rowLabels = actionRows[rowIndex];
    for (std::size_t index = 0u; index < rowLabels.size(); ++index) {
      const bool danger =
          (rowIndex == 0u && index == 2u) || (rowIndex == 5u && index == 1u);
      S3GTrackerDrawSuiteActionButton(
          burstActionRectForRow(row, index, rowLabels.size()), rowLabels[index],
          true, false, false, false, false, false, danger, true);
    }
  }

  const double subX = geometry.editShape.frame.x;
  const double subWidth = geometry.editShape.frame.width;
  const auto audition = burstAuditionPanelLayout();
  const auto placement = burstPlacementPanelLayout();
  drawToolboxHeaderActionButton(fitBurstGatesHeaderButtonRect(),
                                editPanelRect(), "FIT GATES TO ROW", values,
                                style);
  TextStyle previewAttrs =
      trackerState->playing || burst.empty() ? labels : values;
  const Rect previewButton = burstPreviewHeaderButtonRect();
  drawToolboxHeaderActionButton(previewButton, logicalRect(audition.frame),
                                "LISTEN ▶", previewAttrs, style);
  const Rect loopButton = burstLoopHeaderButtonRect();
  drawToolboxHeaderActionButton(
      loopButton, logicalRect(audition.frame),
      trackerState->burstLoopPreview ? "LOOP: ON" : "LOOP: OFF", values, style);
  if (_burstPreviewFeedbackActive) {
    fillRect(rInsetRect(previewButton, 1.0, 1.0),
             themeColor(ThemeRole::Success, 0.28));
    strokeRect(rInsetRect(previewButton, 0.5, 0.5),
               themeColor(ThemeRole::Success), 1.25);
    drawCenteredText("LISTENING", previewButton,
                     themeColor(ThemeRole::TextPrimary), 6.7,
                     FontWeight::Semibold);
  }
  drawTrackerProcessorMenu(
      "MIDI CH",
      format("%02u", static_cast<unsigned int>(std::clamp<uint8_t>(
                         trackerState->burstPreviewMidiChannel, 1u, 16u))),
      layout::rowY(audition, 0u), audition.frame.x, audition.frame.width,
      labels, values, style);
  std::string eventTitle =
      burst.empty()
          ? "—"
          : format("STEP %lu OF %u",
                   static_cast<unsigned long>(_selectedBurstEvent + 1u),
                   static_cast<unsigned int>(burst.eventCount));
  drawTrackerProcessorMenu("SUBSTEP", eventTitle,
                           layout::rowY(geometry.editShape, 0u), subX, subWidth,
                           labels, values, style);
  const BurstEvent event =
      burst.empty() ? BurstEvent{} : burst.events[_selectedBurstEvent];
  drawProcessorSliderWithValueWidth(
      "NOTE",
      format("%s · %03u", midiNoteName(event.note),
             static_cast<unsigned int>(event.note)),
      static_cast<double>(event.note) / 127.0,
      layout::rowY(geometry.editShape, 1u), subX, subWidth, 72.0, labels,
      values, style);
  drawProcessorSliderWithValueWidth("VELOCITY", format("%03u", event.velocity),
                                    static_cast<double>(event.velocity) / 127.0,
                                    layout::rowY(geometry.editShape, 2u), subX,
                                    subWidth, 72.0, labels, values, style);
  drawProcessorSliderWithValueWidth(
      "GATE / ROW", format("%03u%%", event.gatePercent),
      static_cast<double>(event.gatePercent) / 100.0,
      layout::rowY(geometry.editShape, 3u), subX, subWidth, 72.0, labels,
      values, style);

  if (!burstLibraryOnly) {
    std::string selectedViewTitle = viewModePopup.titleOfSelectedItem();
    drawTrackerProcessorMenu(
        "MODE", !selectedViewTitle.empty() ? selectedViewTitle : "BURST EDITOR",
        layout::rowY(geometry.view, 0u), geometry.view.frame.x,
        geometry.view.frame.width, labels, values, style);
  }
  const Rect placeButton = revealHeaderButtonRect();
  drawToolboxHeaderActionButton(placeButton, logicalRect(placement.frame),
                                "PLACE IN TRACKER", values, style);
  if (_burstPlaceFeedbackActive) {
    fillRect(rInsetRect(placeButton, 1.0, 1.0),
             themeColor(ThemeRole::Success, 0.28));
    strokeRect(rInsetRect(placeButton, 0.5, 0.5),
               themeColor(ThemeRole::Success), 1.25);
    drawCenteredText("PLACED ✓", placeButton,
                     themeColor(ThemeRole::TextPrimary), 7.0,
                     FontWeight::Semibold);
  }
  const auto lane =
      pattern.tracks.empty()
          ? 0u
          : std::min(model->session.selectedTrack, pattern.tracks.size() - 1u);
  drawBurstInfo(
      "TARGET",
      pattern.tracks.empty()
          ? "NO LANES"
          : format("T%02lu · ROW %03lu", static_cast<unsigned long>(lane + 1u),
                   static_cast<unsigned long>(model->session.selectedRow + 1u)),
      placement, 0u, themeColor(ThemeRole::TextSecondary));
  drawBurstInfo("SEQ", "CD / EN / PR / SK / EU GATE WHOLE BURST", placement, 1u,
                themeColor(ThemeRole::TextMuted));
  drawBurstInfo("TIMING", "MT / DL SHIFT WHOLE BURST", placement, 2u,
                themeColor(ThemeRole::TextMuted));
  drawBurstInfo("EXPAND", "RR / ST / FL / GL DISABLED", placement, 3u,
                themeColor(ThemeRole::Warning));
}

void GeometryEditor::drawPitchMapWorkspace() {
  auto *model = trackerState;
  if (!model || model->session.pattern.tracks.empty())
    return;
  refreshPitchMapPreview();
  const auto style = softTextStyle();
  TextStyle labels = softLabelAttrs();
  TextStyle values = softValueAttrs();
  const auto geometry = geometryLayout();
  const auto lane = std::min(model->session.selectedTrack,
                             model->session.pattern.tracks.size() - 1u);
  std::size_t first = 0u;
  std::size_t last = 0u;
  pitchMapRowsFirst(&first, &last);

  drawToolboxHeaderActionButton(pitchAnalyzeHeaderButtonRect(),
                                laneCyclePanelRect(), "ANALYZE", values, style);
  std::string laneTitle = lanePopup.titleOfSelectedItem();
  drawTrackerProcessorMenu(
      "LANE", !laneTitle.empty() ? laneTitle : "—",
      layout::rowY(geometry.laneCycle, 0u), geometry.laneCycle.frame.x,
      geometry.laneCycle.frame.width, labels, values, style);
  std::string scope =
      _pitchUseFullCycle
          ? format("FULL · %03lu ROWS",
                   static_cast<unsigned long>(last - first + 1u))
          : format("%03lu–%03lu", static_cast<unsigned long>(first + 1u),
                   static_cast<unsigned long>(last + 1u));
  drawTrackerProcessorMenu("ROWS", scope, layout::rowY(geometry.laneCycle, 1u),
                           geometry.laneCycle.frame.x,
                           geometry.laneCycle.frame.width, labels, values,
                           style);
  static std::vector<std::string> roots = {"C",  "C#", "D",  "D#", "E",  "F",
                                           "F#", "G",  "G#", "A",  "A#", "B"};
  drawTrackerProcessorMenu(
      "ROOT", roots[_pitchSettings.rootPitchClass % 12u],
      layout::rowY(geometry.laneCycle, 2u), geometry.laneCycle.frame.x,
      geometry.laneCycle.frame.width, labels, values, style);
  drawTrackerProcessorMenu(
      "SCALE", nsString(s3g::musicalScaleDefinition(_pitchSettings.scale).name),
      layout::rowY(geometry.laneCycle, 3u), geometry.laneCycle.frame.x,
      geometry.laneCycle.frame.width, labels, values, style);
  drawProcessorSliderWithValueWidth(
      "LOW",
      format("%s · %03u", midiNoteName(_pitchSettings.minimumNote),
             static_cast<unsigned int>(_pitchSettings.minimumNote)),
      static_cast<double>(_pitchSettings.minimumNote) / 127.0,
      layout::rowY(geometry.laneCycle, 4u), geometry.laneCycle.frame.x,
      geometry.laneCycle.frame.width, kGeometryNoteValueWidth, labels, values,
      style);
  drawProcessorSliderWithValueWidth(
      "HIGH",
      format("%s · %03u", midiNoteName(_pitchSettings.maximumNote),
             static_cast<unsigned int>(_pitchSettings.maximumNote)),
      static_cast<double>(_pitchSettings.maximumNote) / 127.0,
      layout::rowY(geometry.laneCycle, 5u), geometry.laneCycle.frame.x,
      geometry.laneCycle.frame.width, kGeometryNoteValueWidth, labels, values,
      style);
  const double evidenceY =
      static_cast<double>(layout::rowY(geometry.laneCycle, 6u));
  drawAtPoint("EVIDENCE",
              makePoint(layout::processorLabelX(geometry.laneCycle.frame.x),
                        evidenceY - 2.0),
              labels);
  std::string evidence =
      _pitchAnalysis.noteCount == 0u
          ? "NO NOTE DATA"
          : format(
                "%lu NOTES · %lu PC · %d%%",
                static_cast<unsigned long>(_pitchAnalysis.noteCount),
                static_cast<unsigned long>(_pitchAnalysis.uniquePitchClasses),
                static_cast<int>(
                    std::lround(_pitchAnalysis.confidence * 100.0f)));
  drawAtPoint(evidence,
              makePoint(layout::processorControlX(geometry.laneCycle.frame.x),
                        evidenceY - 2.0),
              values);

  drawToolboxHeaderActionButton(pitchNewSeedHeaderButtonRect(), editPanelRect(),
                                "NEW SEED", values, style);
  drawToolboxHeaderActionButton(pitchPreviewHeaderButtonRect(), editPanelRect(),
                                "PREVIEW", model->playing ? labels : values,
                                style);
  if (_pitchPreviewFeedbackActive) {
    const Rect previewButton = pitchPreviewHeaderButtonRect();
    fillRect(rInsetRect(previewButton, 1.0, 1.0),
             themeColor(ThemeRole::Success, 0.28));
    strokeRect(rInsetRect(previewButton, 0.5, 0.5),
               themeColor(ThemeRole::Success), 1.25);
    drawCenteredText("PLAYING", previewButton,
                     themeColor(ThemeRole::TextPrimary), 7.0,
                     FontWeight::Semibold);
  }
  drawTrackerProcessorMenu(
      "CONTOUR", nsString(pitchContourName(_pitchSettings.contour)),
      layout::rowY(geometry.editShape, 0u), geometry.editShape.frame.x,
      geometry.editShape.frame.width, labels, values, style);
  drawTrackerProcessorMenu(
      "LEAP",
      format("%u DEGREE%s",
             static_cast<unsigned int>(_pitchSettings.maximumLeapDegrees),
             _pitchSettings.maximumLeapDegrees == 1u ? "" : "S"),
      layout::rowY(geometry.editShape, 1u), geometry.editShape.frame.x,
      geometry.editShape.frame.width, labels, values, style);
  const bool variationActive = _pitchSettings.contour != PitchContour::Fit &&
                               _pitchSettings.contour != PitchContour::Manual;
  drawProcessorSlider(
      "VAR",
      variationActive
          ? format("%03d%%", static_cast<int>(std::lround(
                                 _pitchSettings.variation * 100.0f)))
          : "N/A",
      variationActive ? _pitchSettings.variation : 0.0,
      layout::rowY(geometry.editShape, 2u), geometry.editShape.frame.x,
      geometry.editShape.frame.width, labels, values, style);
  drawProcessorToggle("ANCHORS", _pitchSettings.preserveEndpoints,
                      layout::rowY(geometry.editShape, 3u),
                      geometry.editShape.frame.x,
                      geometry.editShape.frame.width, labels, values, style);
  drawProcessorSlider(
      "TRANSPOSE",
      format("%+03d ST", static_cast<int>(_pitchSettings.transposeSemitones)),
      (static_cast<double>(_pitchSettings.transposeSemitones) + 24.0) / 48.0,
      layout::rowY(geometry.editShape, 4u), geometry.editShape.frame.x,
      geometry.editShape.frame.width, labels, values, style);
  drawProcessorToggle("INVERT", _pitchSettings.invertScaleDegrees,
                      layout::rowY(geometry.editShape, 5u),
                      geometry.editShape.frame.x,
                      geometry.editShape.frame.width, labels, values, style);
  drawProcessorToggle("REVERSE", _pitchSettings.reversePitchOrder,
                      layout::rowY(geometry.editShape, 6u),
                      geometry.editShape.frame.x,
                      geometry.editShape.frame.width, labels, values, style);
  drawTrackerProcessorMenu("MODE", "PITCH MAP", layout::rowY(geometry.view, 0u),
                           geometry.view.frame.x, geometry.view.frame.width,
                           labels, values, style);

  const bool editable = canEditDisplayedPattern();
  drawToolboxHeaderActionButton(pitchApplyHeaderButtonRect(), bridgePanelRect(),
                                "APPLY", editable ? values : labels, style);
  const double bridgeX = geometry.trackerBridge.frame.x;
  const double labelX = layout::processorLabelX(bridgeX);
  const double valueX = layout::processorControlX(bridgeX);
  const auto drawInfo = [&](std::string name, std::string value, uint32_t row,
                            Color color) {
    const double y = layout::rowY(geometry.trackerBridge, row);
    drawAtPoint(name, makePoint(labelX, y - 2.0), labels);
    TextStyle attrs{color, uiFont(10.0)};
    drawAtPoint(value, makePoint(valueX, y - 2.0), attrs);
  };
  drawInfo("TARGET",
           format("T%02lu · ROWS %03lu–%03lu",
                  static_cast<unsigned long>(lane + 1u),
                  static_cast<unsigned long>(first + 1u),
                  static_cast<unsigned long>(last + 1u)),
           0u, themeColor(ThemeRole::TextSecondary));
  drawInfo("SOURCE", "EXPLICIT NOTE CELLS ONLY", 1u,
           themeColor(ThemeRole::Note));
  drawInfo("PREVIEW",
           format("%lu HITS · %lu CHANGES",
                  static_cast<unsigned long>(_pitchPreview.assignments.size()),
                  static_cast<unsigned long>(_pitchPreview.changed)),
           2u, themeColor(ThemeRole::Live));
  drawInfo("STATE", !_pitchStatus.empty() ? _pitchStatus : "READY", 3u,
           themeColor(editable ? ThemeRole::TextMuted : ThemeRole::Warning));

  const int low = pitchMapDisplayMinimum();
  const int high = pitchMapDisplayMaximum();
  const double pitchSpan = static_cast<double>(std::max(1, high - low));
  if (_pitchPreview.assignments.empty()) {
    for (const Rect graph : {pitchGraphRect(), pitchIntervalGraphRect()}) {
      fillRect(graph, themeColor(ThemeRole::Canvas, 0.86));
      strokeRect(rInsetRect(graph, 0.5, 0.5), themeColor(ThemeRole::Border));
      drawCenteredText("NO EXPLICIT NOTE HITS IN THIS RANGE", graph,
                       themeColor(ThemeRole::TextFaint), 8.0,
                       FontWeight::Medium);
    }
    return;
  }
  for (std::size_t graphIndex = 0u; graphIndex < 2u; ++graphIndex) {
    const bool drawingInterval = graphIndex == 1u;
    const Rect graph =
        drawingInterval ? pitchIntervalGraphRect() : pitchGraphRect();
    fillRect(graph, themeColor(ThemeRole::Canvas, 0.86));
    strokeRect(rInsetRect(graph, 0.5, 0.5), themeColor(ThemeRole::Border));
    if (drawingInterval) {
      const int extent = pitchIntervalExtent();
      const double usableHalfHeight =
          std::max<double>(1.0, rHeight(graph) * 0.5 - 12.0);
      int previousDegree = std::numeric_limits<int>::min();
      for (int guideIndex = 0; guideIndex <= 4; ++guideIndex) {
        const int degree = static_cast<int>(
            std::lround(-static_cast<double>(extent) +
                        static_cast<double>(extent * 2 * guideIndex) / 4.0));
        if (degree == previousDegree)
          continue;
        previousDegree = degree;
        const double y = rMidY(graph) - static_cast<double>(degree) /
                                            static_cast<double>(extent) *
                                            usableHalfHeight;
        GeometryPath guide = GeometryPath{};
        guide.moveToPoint(makePoint(rMinX(graph), y));
        guide.lineToPoint(makePoint(rMaxX(graph), y));
        guide.lineWidth = degree == 0 ? 1.15 : 0.5;
        (strokeColor_ =
             themeColor(degree == 0 ? ThemeRole::BorderStrong : ThemeRole::Grid,
                        degree == 0 ? 0.82 : 0.42));
        strokePath(guide);
        drawText(degree == 0 ? "0 DEG" : format("%+d DEG", degree),
                 makeRect(rMinX(graph) - 50.0, y - 6.0, 46.0, 12.0),
                 themeColor(degree == 0 ? ThemeRole::TextSecondary
                                        : ThemeRole::TextFaint),
                 6.4, FontWeight::Regular, Alignment::Right);
      }
    } else {
      for (int note = low; note <= high; ++note) {
        if (!pitchMapNoteMatchesScale(static_cast<uint8_t>(note)))
          continue;
        const double y = rMaxY(graph) - static_cast<double>(note - low) /
                                            pitchSpan * rHeight(graph);
        const int effectiveRoot =
            (static_cast<int>(_pitchSettings.rootPitchClass) +
             static_cast<int>(_pitchSettings.transposeSemitones) + 120) %
            12;
        const bool root = note % 12 == effectiveRoot;
        GeometryPath guide = GeometryPath{};
        guide.moveToPoint(makePoint(rMinX(graph), y));
        guide.lineToPoint(makePoint(rMaxX(graph), y));
        guide.lineWidth = root ? 0.9 : 0.45;
        (strokeColor_ =
             themeColor(root ? ThemeRole::BorderStrong : ThemeRole::Grid,
                        root ? 0.62 : 0.38));
        strokePath(guide);
        if (root || note == low || note == high) {
          drawText(format("%s · %03d", midiNoteName(static_cast<uint8_t>(note)),
                          note),
                   makeRect(rMinX(graph) - 50.0, y - 6.0, 46.0, 12.0),
                   themeColor(root ? ThemeRole::TextSecondary
                                   : ThemeRole::TextFaint),
                   6.4, FontWeight::Regular, Alignment::Right);
        }
      }
    }
    const std::size_t rowCount = last - first + 1u;
    const std::size_t rowLabelStride =
        std::max<std::size_t>(1u, (rowCount + 15u) / 16u);
    for (std::size_t row = first; row <= last; ++row) {
      const double x =
          rMinX(graph) +
          static_cast<double>(row - first) /
              static_cast<double>(std::max<std::size_t>(1u, last - first)) *
              rWidth(graph);
      const bool selected = row == model->session.selectedRow;
      if (selected)
        fillRect(makeRect(x - 2.0, rMinY(graph), 4.0, rHeight(graph)),
                 themeColor(ThemeRole::Selection, 0.30));
      if ((row - first) % rowLabelStride != 0u && row != last)
        continue;
      GeometryPath guide = GeometryPath{};
      guide.moveToPoint(makePoint(x, rMinY(graph)));
      guide.lineToPoint(makePoint(x, rMaxY(graph)));
      guide.lineWidth = row % 4u == 0u ? 0.8 : 0.4;
      (strokeColor_ =
           themeColor(ThemeRole::Grid, row % 4u == 0u ? 0.52 : 0.28));
      strokePath(guide);
      drawCenteredText(format("%03lu", static_cast<unsigned long>(row + 1u)),
                       makeRect(x - 18.0, rMaxY(graph) + 8.0, 36.0, 12.0),
                       themeColor(ThemeRole::TextFaint), 6.4,
                       FontWeight::Regular);
    }
    GeometryPath contour = GeometryPath{};
    bool started = false;
    Color laneColor =
        trackerColor(kLaneColors[lane % kLaneColors.size()], 0.92);
    for (std::size_t index = 0u; index < _pitchPreview.assignments.size();
         ++index) {
      const auto &assignment = _pitchPreview.assignments[index];
      const Point previewPoint =
          pitchMapPointForAssignmentAtIndex(index, false, drawingInterval);
      const std::size_t originalVoiceCount =
          drawingInterval ? 1u : assignment.voiceCount;
      for (std::size_t voice = 0u; voice < originalVoiceCount; ++voice) {
        Point originalPoint = Point{};
        if (drawingInterval) {
          originalPoint = pitchMapPointForAssignmentAtIndex(index, true, true);
        } else {
          PitchMapAssignment original = assignment;
          original.note = original.originalNotes[voice];
          originalPoint = pitchMapPointForAssignment(original);
        }
        GeometryPath originalMarker = GeometryPath::ellipse(
            makeRect(originalPoint.x - 3.0, originalPoint.y - 3.0, 6.0, 6.0));
        (strokeColor_ = themeColor(ThemeRole::TextFaint, 0.72));
        originalMarker.lineWidth = 1.0;
        strokePath(originalMarker);
      }
      if (!started) {
        contour.moveToPoint(previewPoint);
        started = true;
      } else
        contour.lineToPoint(previewPoint);
    }
    contour.lineWidth = 1.35;
    (strokeColor_ = laneColor);
    strokePath(contour);
    const PitchMapAssignment *selectedAssignment = nullptr;
    Point selectedPoint = Point{};
    for (std::size_t index = 0u; index < _pitchPreview.assignments.size();
         ++index) {
      const auto &assignment = _pitchPreview.assignments[index];
      const Point point =
          pitchMapPointForAssignmentAtIndex(index, false, drawingInterval);
      const bool selected = assignment.row == model->session.selectedRow;
      if (!drawingInterval && assignment.voiceCount > 1u) {
        PitchMapAssignment highest = assignment;
        highest.note = assignment.notes[assignment.voiceCount - 1u];
        const Point highestPoint = pitchMapPointForAssignment(highest);
        GeometryPath voicingStem = GeometryPath{};
        voicingStem.moveToPoint(point);
        voicingStem.lineToPoint(highestPoint);
        voicingStem.lineWidth = selected ? 1.2 : 0.8;
        (strokeColor_ = trackerColor(kLaneColors[lane % kLaneColors.size()],
                                     selected ? 0.72 : 0.42));
        strokePath(voicingStem);
        for (std::size_t voice = 1u; voice < assignment.voiceCount; ++voice) {
          PitchMapAssignment voiced = assignment;
          voiced.note = assignment.notes[voice];
          const Point voicedPoint = pitchMapPointForAssignment(voiced);
          const double voicedRadius = selected ? 4.0 : 3.2;
          GeometryPath voicedMarker = GeometryPath::ellipse(makeRect(
              voicedPoint.x - voicedRadius, voicedPoint.y - voicedRadius,
              voicedRadius * 2.0, voicedRadius * 2.0));
          (fillColor_ = themeColor(ThemeRole::Canvas));
          fillPath(voicedMarker);
          voicedMarker.lineWidth = selected ? 1.7 : 1.1;
          (strokeColor_ = laneColor);
          strokePath(voicedMarker);
          if (assignment.notes[voice] != assignment.originalNotes[voice]) {
            (fillColor_ = laneColor);
            fillPath(GeometryPath::ellipse(
                rInsetRect(voicedMarker.bounds, 1.9, 1.9)));
          }
        }
      }
      const double radius = selected ? 5.0 : 4.0;
      GeometryPath marker = GeometryPath::ellipse(makeRect(
          point.x - radius, point.y - radius, radius * 2.0, radius * 2.0));
      (fillColor_ = themeColor(ThemeRole::Canvas));
      fillPath(marker);
      marker.lineWidth = selected ? 2.0 : 1.25;
      (strokeColor_ = laneColor);
      strokePath(marker);
      if (assignment.note != assignment.originalNote) {
        GeometryPath center =
            GeometryPath::ellipse(rInsetRect(marker.bounds, 2.1, 2.1));
        (fillColor_ = laneColor);
        fillPath(center);
      }
      if (selected) {
        selectedAssignment = &assignment;
        selectedPoint = point;
      }
    }
    if (selectedAssignment) {
      std::string flagText = pitchAssignmentText(*selectedAssignment);
      if (drawingInterval) {
        const auto selectedIndex = static_cast<std::size_t>(
            selectedAssignment - _pitchPreview.assignments.data());
        flagText = selectedIndex == 0u
                       ? (flagText + " · ANCHOR")
                       : (flagText +
                          format(" · %+d DEG",
                                 pitchIntervalAtIndex(selectedIndex, false)));
      }
      const double voiceWidth =
          104.0 +
          static_cast<double>(selectedAssignment->voiceCount - 1u) * 112.0;
      const double flagWidth = std::min<double>(
          rWidth(graph) - 8.0, voiceWidth + (drawingInterval ? 54.0 : 0.0));
      constexpr double flagHeight = 18.0;
      double flagX = selectedPoint.x + 11.0;
      if (flagX + flagWidth > rMaxX(graph) - 4.0)
        flagX = selectedPoint.x - flagWidth - 11.0;
      double flagY = selectedPoint.y - flagHeight - 10.0;
      if (flagY < rMinY(graph) + 4.0)
        flagY = selectedPoint.y + 10.0;
      flagX =
          std::clamp(flagX, rMinX(graph) + 4.0, rMaxX(graph) - flagWidth - 4.0);
      flagY = std::clamp(flagY, rMinY(graph) + 4.0,
                         rMaxY(graph) - flagHeight - 4.0);
      const Rect flag =
          makeRect(std::floor(flagX), std::floor(flagY), flagWidth, flagHeight);
      GeometryPath leader = GeometryPath{};
      leader.moveToPoint(selectedPoint);
      leader.lineToPoint(
          makePoint(selectedPoint.x < rMidX(flag) ? rMinX(flag) : rMaxX(flag),
                    rMidY(flag)));
      leader.lineWidth = 1.0;
      (strokeColor_ = laneColor);
      strokePath(leader);
      fillRect(flag, themeColor(ThemeRole::Raised, 0.98));
      strokeRect(rInsetRect(flag, 0.5, 0.5), laneColor, 1.1);
      drawCenteredText(flagText, rInsetRect(flag, 5.0, 0.0),
                       themeColor(ThemeRole::TextPrimary), 7.4,
                       FontWeight::Semibold);
    }
    drawText(!drawingInterval ? "HOLLOW = ORIGINAL  ·  LANE COLOR = PREVIEW  · "
                                " DRAG POINTS TO SCALE DEGREES"
                              : "0 = REPEAT  ·  + / − = SCALE-DEGREE MOTION  · "
                                " DRAG SHIFTS THIS NOTE + FOLLOWING PHRASE",
             makeRect(rMinX(graph), rMinY(graph) - 26.0, rWidth(graph), 12.0),
             themeColor(ThemeRole::TextFaint), 6.6, FontWeight::Medium,
             Alignment::Center);
  }
}

void GeometryEditor::drawRect(Rect dirtyRect) {
  (void)dirtyRect;
  const auto style = softTextStyle();
  TextStyle labels = softLabelAttrs();
  TextStyle values = softValueAttrs();
  (fillColor_ = style.bg);
  fillCurrentRect(bounds_);

  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);

  const auto geometry = geometryLayout();
  const Rect canvas = canvasRect();
  std::vector<std::string> fieldTitles = {
      "RING FIELD  /  EDITABLE MIDI GEOMETRY",
      "ACTIVE PULSES  /  NOTE POLYGONS",
      "ALL STEPS UNDERLAY  /  ROW LATTICE + PULSES",
      "PHASE SPOKES  /  PLAYHEAD ALIGNMENT",
      "LANE FOCUS  /  SELECTED CYCLE",
      "COMPOSITE RING  /  PHASE COMPARISON",
      "BURST EDITOR  /  SUB-ROW MIDI PHRASES",
      "PITCH MAP  /  CONTOUR + SCALE-DEGREE INTERVALS",
  };
  std::string fieldTitle = fieldTitles[static_cast<std::size_t>(
      std::clamp<int>(geometryViewMode, 0, 7))];
  drawPanelFrame(rMinX(canvas), rMinY(canvas), rWidth(canvas), rHeight(canvas),
                 style);
  drawPanelHeader(fieldTitle, true, rMinX(canvas), rMinY(canvas),
                  rWidth(canvas), layout::kStandardMetrics.headerHeight, labels,
                  style);
  const struct {
    const layout::Panel *panel;
    std::string title;
  } panels[]{
      {&geometry.laneCycle, geometryViewMode == GeometryModeBurst
                                ? "BURST LIBRARY"
                                : geometryViewMode == GeometryModePitchMap
                                      ? "PITCH SOURCE"
                                      : "LANE / CYCLE"},
      {&geometry.editShape, geometryViewMode == GeometryModeBurst
                                ? "SUBSTEPS"
                                : geometryViewMode == GeometryModePitchMap
                                      ? "CONTOUR"
                                      : "EDIT / SHAPE"},
      {&geometry.view, burstLibraryOnly ? "" : "VIEW"},
      {&geometry.trackerBridge,
       geometryViewMode == GeometryModeBurst ? "" : "TRACKER BRIDGE"},
  };
  for (const auto &panel : panels) {
    if (panel.title.empty())
      continue;
    drawPanelFrame(*panel.panel, style);
    drawPanelHeader(panel.title, true, *panel.panel, labels, style);
  }
  if (geometryViewMode == GeometryModeBurst) {
    const auto audition = burstAuditionPanelLayout();
    const auto placement = burstPlacementPanelLayout();
    drawPanelFrame(audition, style);
    drawPanelHeader("AUDITION", true, audition, labels, style);
    drawPanelFrame(placement, style);
    drawPanelHeader("PLACEMENT", true, placement, labels, style);
  }
  const std::array<Rect, 3u> zoomRects{{
      zoomOutRect(),
      zoomResetRect(),
      zoomInRect(),
  }};
  std::vector<std::string> zoomLabels = {
      "−",
      format("%d%%", static_cast<int>(std::lround(geometryZoom * 100.0))),
      "+",
  };
  for (std::size_t index = 0u; index < zoomRects.size(); ++index) {
    drawToolboxHeaderButton(zoomRects[index], canvas, zoomLabels[index],
                            index == 1u, labels, style);
  }
  syncToolboxControls();
  if (geometryViewMode == GeometryModeBurst) {
    drawBurstWorkspace();
    drawOpenGeometryMenu();
    return;
  }
  if (geometryViewMode == GeometryModePitchMap) {
    drawPitchMapWorkspace();
    drawOpenGeometryMenu();
    return;
  }

  const bool editable = canEditDisplayedPattern();
  for (std::size_t index = 0u; index < toolButtons.size(); ++index)
    toolButtons[index].enabled =
        index == 0u || (editable && geometryViewMode == GeometryModeRingField);
  for (auto *button :
       {&rotateBackButton, &rotateForwardButton, &densityDownButton,
        &densityUpButton, &reverseButton, &reflectButton})
    button->enabled = editable;
  for (auto &button : morphButtons)
    button.enabled = editable;

  if (!model || !pattern || pattern->tracks.empty()) {
    drawCenteredText("NO PATTERN LANES", canvasPlotRect(),
                     trackerColor(0x8f8f8f), 10.0);
    return;
  }
  _lastDisplayedPatternId = geometryPatternId(model);
  _lastDisplayedSongMuteMask =
      model->songPlaybackActive ? model->songPlaybackMutedTracks : 0u;
  const auto lanes = geometryLanes(pattern);
  const auto visible = visibleGeometryLanes(model);

  std::size_t selectedLane = visible.count > 0u
                                 ? visible.indices[0u]
                                 : std::numeric_limits<std::size_t>::max();
  for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
    if (visible.indices[ordinal] == model->session.selectedTrack) {
      selectedLane = visible.indices[ordinal];
      break;
    }
  }
  const Point center = geometryCenter();
  const double maximum = geometryMaximumRadius();
  const double fullCircle = static_cast<double>(kGeometryPi) * 2.0;
  const bool ringField = geometryViewMode == GeometryModeRingField;
  const bool allSteps = allStepsUnderlayNodeCount() > 0u;
  const bool phaseSpokes = geometryViewMode == GeometryModePhaseSpokes;
  const bool laneFocus = geometryViewMode == GeometryModeLaneFocus;
  const bool composite = geometryViewMode == GeometryModeCompositeRing;

  for (std::size_t ordinal = lanes.count; ordinal-- > 0u;) {
    const auto lane = lanes.indices[ordinal];
    const auto &track = pattern->tracks[lane];
    const bool muted = geometryLaneMuted(model, pattern, lane);
    const bool selected = lane == selectedLane;
    const bool lengthPreview = _geometryGestureActive &&
                               _geometryGestureKind == GeometryGestureLength &&
                               _gestureLane == lane;
    const auto length =
        lengthPreview
            ? _gesturePreviewLength
            : std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
    const bool rotationPreview =
        _geometryGestureActive &&
        _geometryGestureKind == GeometryGestureRotate && _gestureLane == lane;
    const bool densityPreview =
        _geometryGestureActive &&
        _geometryGestureKind == GeometryGestureDensity && _gestureLane == lane;
    const auto displayedPhase =
        lengthPreview ? _gesturePreviewPhase : track.noteColumn.phase % length;
    if (laneFocus && !selected && !muted)
      continue;
    double radius = ringRadiusForOrdinal(ordinal, lanes.count);
    if (composite)
      radius = maximum * 0.70;
    if (laneFocus)
      radius = maximum * 0.70;
    Color identity = trackerColor(kLaneColors[lane % kLaneColors.size()],
                                  selected ? 0.94 : ringField ? 0.42 : 0.66);
    Color pointIdentity =
        trackerColor(kLaneColors[lane % kLaneColors.size()],
                     selected ? 1.0 : ringField ? 0.90 : 0.78);
    GeometryPath ring = GeometryPath::ellipse(makeRect(
        center.x - radius, center.y - radius, radius * 2.0, radius * 2.0));
    ring.lineWidth =
        muted ? 1.15
              : allSteps ? (selected ? 4.8 : 3.2) : selected ? 1.35 : 0.72;
    if (muted) {
      const double dash[] = {3.0, 4.0};
      ring.setLineDash(dash, 2, 0.0);
      (strokeColor_ = themeColor(ThemeRole::Canvas, 0.94));
    } else if (allSteps) {
      (strokeColor_ = withAlpha(identity, selected ? 0.13 : 0.08));
    } else {
      (strokeColor_ =
           themeColor(selected ? ThemeRole::BorderStrong : ThemeRole::Grid,
                      ringField ? 0.88 : 0.58));
    }
    strokePath(ring);
    if (muted) {
      drawCenteredText(
          "M", makeRect(center.x + radius - 5.0, center.y - 6.0, 10.0, 12.0),
          themeColor(ThemeRole::Border, 0.72), 7.0, FontWeight::Medium);
      continue;
    }

    GeometryPath polygon = GeometryPath{};
    bool polygonStarted = false;
    for (std::size_t row = 0u; row < length; ++row) {
      const double angle =
          -static_cast<double>(kGeometryHalfPi) +
          static_cast<double>(row) * fullCircle / static_cast<double>(length);
      const double cosine = std::cos(angle);
      const double sine = std::sin(angle);
      const bool originalHit =
          row < track.notes.size() && noteCellIsActivePulse(track.notes[row]);
      const bool rotatedHit = rotationPreview &&
                              row < _gesturePreviewNotes.size() &&
                              noteCellIsActivePulse(_gesturePreviewNotes[row]);
      const bool previewHit = densityPreview &&
                              row < _gesturePreviewNotes.size() &&
                              noteCellIsActivePulse(_gesturePreviewNotes[row]);
      const bool hit = densityPreview
                           ? originalHit || previewHit
                           : rotationPreview ? rotatedHit : originalHit;
      if (allSteps) {
        const double nodeRadius = length <= 32u
                                      ? (selected ? 2.8 : 2.1)
                                      : length <= 64u ? (selected ? 2.0 : 1.55)
                                                      : (selected ? 1.35 : 1.0);
        const Rect nodeRect = makeRect(center.x + cosine * radius - nodeRadius,
                                       center.y + sine * radius - nodeRadius,
                                       nodeRadius * 2.0, nodeRadius * 2.0);
        (fillColor_ = style.bg);
        fillCurrentRect(nodeRect);
        (strokeColor_ = withAlpha(identity, selected ? 0.78 : 0.48));
        strokeCurrentRect(rInsetRect(nodeRect, 0.5, 0.5));
        if (row % 4u == 0u) {
          GeometryPath beatMark = GeometryPath{};
          beatMark.moveToPoint(makePoint(center.x + cosine * (radius - 7.0),
                                         center.y + sine * (radius - 7.0)));
          beatMark.lineToPoint(makePoint(center.x + cosine * (radius + 7.0),
                                         center.y + sine * (radius + 7.0)));
          beatMark.lineWidth = selected ? 1.2 : 0.8;
          (strokeColor_ = withAlpha(identity, selected ? 0.72 : 0.42));
          strokePath(beatMark);
        }
      } else if (ringField && (selected || length <= 32u)) {
        const double tick = selected && row % 4u == 0u ? 4.5 : 2.2;
        GeometryPath mark = GeometryPath{};
        mark.moveToPoint(makePoint(center.x + cosine * (radius - tick),
                                   center.y + sine * (radius - tick)));
        mark.lineToPoint(makePoint(center.x + cosine * (radius + tick),
                                   center.y + sine * (radius + tick)));
        mark.lineWidth = selected ? 0.8 : 0.45;
        (strokeColor_ = themeColor(ThemeRole::Grid, selected ? 0.82 : 0.46));
        strokePath(mark);
      }
      if (!hit)
        continue;
      const double velocity = resolvedVelocity(track, row);
      const double eventRadius =
          ringField ? radius + (velocity - 0.5) * 12.0 : radius;
      const Point eventPoint = makePoint(center.x + cosine * eventRadius,
                                         center.y + sine * eventRadius);
      if (!ringField) {
        if (!polygonStarted) {
          polygon.moveToPoint(eventPoint);
          polygonStarted = true;
        } else
          polygon.lineToPoint(eventPoint);
      }
      const double bead = ringField ? (selected ? 3.4 : 2.4) + velocity * 2.2
                                    : allSteps ? (selected ? 3.5 : 2.8) : 2.0;
      GeometryPath point = GeometryPath::ellipse(makeRect(
          eventPoint.x - bead, eventPoint.y - bead, bead * 2.0, bead * 2.0));
      if (ringField) {
        (fillColor_ = themeColor(ThemeRole::Canvas, 0.88));
        fillPath(GeometryPath::ellipse(rInsetRect(point.bounds, -1.4, -1.4)));
      }
      Color eventColor =
          rotationPreview ? themeColor(ThemeRole::Live, 0.92)
                          : densityPreview && previewHit && !originalHit
                                ? themeColor(ThemeRole::Live, 0.82)
                                : densityPreview && originalHit && !previewHit
                                      ? withAlpha(pointIdentity, 0.24)
                                      : pointIdentity;
      (fillColor_ = eventColor);
      fillPath(point);
      if (densityPreview && previewHit != originalHit) {
        GeometryPath ghost =
            GeometryPath::ellipse(rInsetRect(point.bounds, -3.0, -3.0));
        ghost.lineWidth = 1.1;
        (strokeColor_ =
             themeColor(previewHit ? ThemeRole::Live : ThemeRole::TextFaint,
                        previewHit ? 0.92 : 0.62));
        strokePath(ghost);
      }
      if (selected && row == model->session.selectedRow % length) {
        GeometryPath halo =
            GeometryPath::ellipse(rInsetRect(point.bounds, -3.4, -3.4));
        halo.lineWidth = 1.2;
        (strokeColor_ = themeColor(ThemeRole::TextPrimary));
        strokePath(halo);
      }
    }
    if (polygonStarted) {
      polygon.closePath();
      polygon.lineWidth = selected ? 1.8 : 1.0;
      (strokeColor_ = identity);
      strokePath(polygon);
    }
    if (phaseSpokes || composite) {
      const double phaseAngle = -static_cast<double>(kGeometryHalfPi) +
                                static_cast<double>(displayedPhase) *
                                    fullCircle / static_cast<double>(length);
      const double phaseCosine = std::cos(phaseAngle);
      const double phaseSine = std::sin(phaseAngle);
      GeometryPath phaseMark = GeometryPath{};
      const double inner = phaseSpokes ? maximum * 0.10 : radius - 7.0;
      phaseMark.moveToPoint(makePoint(center.x + phaseCosine * inner,
                                      center.y + phaseSine * inner));
      phaseMark.lineToPoint(makePoint(center.x + phaseCosine * (radius + 7.0),
                                      center.y + phaseSine * (radius + 7.0)));
      phaseMark.lineWidth = selected ? 1.4 : 0.65;
      (strokeColor_ = identity);
      strokePath(phaseMark);
    }
    if (ringField && selected) {
      const Point rotateHandle = rotateHandlePoint();
      GeometryPath diamond = GeometryPath{};
      diamond.moveToPoint(makePoint(rotateHandle.x, rotateHandle.y - 7.0));
      diamond.lineToPoint(makePoint(rotateHandle.x + 7.0, rotateHandle.y));
      diamond.lineToPoint(makePoint(rotateHandle.x, rotateHandle.y + 7.0));
      diamond.lineToPoint(makePoint(rotateHandle.x - 7.0, rotateHandle.y));
      diamond.closePath();
      (fillColor_ =
           themeColor(rotationPreview ? ThemeRole::Live : ThemeRole::Raised));
      fillPath(diamond);
      diamond.lineWidth = 1.2;
      (strokeColor_ = themeColor(ThemeRole::Live));
      strokePath(diamond);
      drawCenteredText(
          "R", makeRect(rotateHandle.x - 7.0, rotateHandle.y - 7.0, 14.0, 14.0),
          themeColor(rotationPreview ? ThemeRole::Canvas : ThemeRole::Live),
          6.5, FontWeight::Semibold);

      const auto displayedDensity = densityPreview
                                        ? _gesturePreviewDensity
                                        : s3g::tracker::geometryHitCount(track);
      const double densityRadius = radius + 18.0;
      const double densityFraction =
          static_cast<double>(displayedDensity) / static_cast<double>(length);
      if (displayedDensity > 0u) {
        GeometryPath densityArc = GeometryPath{};
        const auto segments = std::max<std::size_t>(
            2u, static_cast<std::size_t>(std::ceil(densityFraction * 64.0)));
        for (std::size_t segment = 0u; segment <= segments; ++segment) {
          const double fraction = densityFraction *
                                  static_cast<double>(segment) /
                                  static_cast<double>(segments);
          const double angle =
              -static_cast<double>(kGeometryHalfPi) + fraction * fullCircle;
          const Point arcPoint = geometryPointAtRadius(densityRadius, angle);
          if (segment == 0u)
            densityArc.moveToPoint(arcPoint);
          else
            densityArc.lineToPoint(arcPoint);
        }
        densityArc.lineWidth = densityPreview ? 2.3 : 1.5;
        densityArc.roundCaps = true;
        (strokeColor_ =
             themeColor(ThemeRole::Live, densityPreview ? 0.92 : 0.58));
        strokePath(densityArc);
      }
      const double densityAngle =
          -static_cast<double>(kGeometryHalfPi) + densityFraction * fullCircle;
      const Point densityHandle =
          geometryPointAtRadius(densityRadius, densityAngle);
      GeometryPath densityKnob = GeometryPath::ellipse(
          makeRect(densityHandle.x - 6.5, densityHandle.y - 6.5, 13.0, 13.0));
      (fillColor_ =
           themeColor(densityPreview ? ThemeRole::Live : ThemeRole::Raised));
      fillPath(densityKnob);
      densityKnob.lineWidth = 1.2;
      (strokeColor_ = themeColor(ThemeRole::Live));
      strokePath(densityKnob);
      drawCenteredText(
          "D",
          makeRect(densityHandle.x - 6.5, densityHandle.y - 6.5, 13.0, 13.0),
          themeColor(densityPreview ? ThemeRole::Canvas : ThemeRole::Live), 6.2,
          FontWeight::Semibold);
    }
  }

  fillRect(makeRect(center.x - 2.0, center.y - 2.0, 4.0, 4.0),
           themeColor(ThemeRole::TextFaint));
  std::string fieldGuide =
      ringField ? "DRAG R: ROTATE ROWS  •  DRAG D: DENSITY  •  DOUBLE BEAD: "
                  "TRACKER  •  M: MUTED  •  ⌥ ERASE"
                : allSteps ? "EVERY TRACKER ROW = HOLLOW CELL  •  FILLED CELLS "
                             "= ACTIVE PULSES"
                           : "DIAGNOSTIC VIEW  •  SWITCH TO RING FIELD TO EDIT";
  drawText(fieldGuide,
           makeRect(rMinX(canvas) + 10.0, rMinY(canvas) + 28.0,
                    rWidth(canvas) - 20.0, 12.0),
           themeColor(ThemeRole::TextFaint), 6.8, FontWeight::Medium,
           Alignment::Center);

  if (visible.count == 0u) {
    drawCenteredText("ALL NOTE LANES MUTED",
                     makeRect(rMinX(canvas) + 12.0, rMinY(canvas) + 43.0,
                              rWidth(canvas) - 24.0, 18.0),
                     themeColor(ThemeRole::TextFaint), 8.0, FontWeight::Medium);
    drawOpenGeometryMenu();
    return;
  }

  const auto &selectedTrack = pattern->tracks[selectedLane];
  const bool selectedLengthPreview =
      _geometryGestureActive && _geometryGestureKind == GeometryGestureLength &&
      _gestureLane == selectedLane;
  const auto selectedLength =
      selectedLengthPreview
          ? _gesturePreviewLength
          : std::clamp<std::size_t>(selectedTrack.noteColumn.length, 1u, 256u);
  const auto selectedRow = model->session.selectedRow % selectedLength;
  const bool selectedRotationPreview =
      _geometryGestureActive && _geometryGestureKind == GeometryGestureRotate &&
      _gestureLane == selectedLane;
  const bool selectedDensityPreview =
      _geometryGestureActive &&
      _geometryGestureKind == GeometryGestureDensity &&
      _gestureLane == selectedLane;
  const int displayedRotation =
      selectedRotationPreview ? _gesturePreviewRotation : 0;
  std::size_t hits = selectedDensityPreview ? _gesturePreviewDensity : 0u;
  if (!selectedDensityPreview) {
    for (std::size_t row = 0u; row < selectedLength; ++row) {
      if (row < selectedTrack.notes.size() &&
          noteCellIsActivePulse(selectedTrack.notes[row]))
        ++hits;
    }
  }

  const double laneX = static_cast<double>(geometry.laneCycle.frame.x);
  const double laneWidth = static_cast<double>(geometry.laneCycle.frame.width);
  std::string selectedLaneTitle = lanePopup.titleOfSelectedItem();
  drawTrackerProcessorMenu(
      "LANE", !selectedLaneTitle.empty() ? selectedLaneTitle : "—",
      static_cast<double>(layout::rowY(geometry.laneCycle, 0u)), laneX,
      laneWidth, labels, values, style);
  const bool selectedNotePreview =
      _geometryGestureActive &&
      _geometryGestureKind == GeometryGestureDefaultNote &&
      _gestureLane == selectedLane;
  const uint8_t displayedDefaultNote =
      selectedNotePreview
          ? _gesturePreviewDefaultNote
          : s3g::tracker::laneDefaultNote(model->session, selectedLane);
  drawProcessorSliderWithValueWidth(
      "DEFAULT NOTE",
      format("%s · %03u%s", midiNoteName(displayedDefaultNote),
             static_cast<unsigned int>(displayedDefaultNote),
             selectedNotePreview ? "*" : ""),
      static_cast<double>(displayedDefaultNote) / 127.0,
      static_cast<double>(layout::rowY(geometry.laneCycle, 1u)), laneX,
      laneWidth, kGeometryNoteValueWidth, labels, values, style);
  drawProcessorSlider(
      "LENGTH",
      format("%03lu%s", static_cast<unsigned long>(selectedLength),
             selectedLengthPreview ? "*" : ""),
      std::sqrt(static_cast<double>(selectedLength - 1u) / 255.0),
      static_cast<double>(layout::rowY(geometry.laneCycle, 2u)), laneX,
      laneWidth, labels, values, style);
  std::string selectedDirectionTitle = directionPopup.titleOfSelectedItem();
  drawTrackerProcessorMenu(
      "DIRECTION",
      !selectedDirectionTitle.empty() ? selectedDirectionTitle : "—",
      static_cast<double>(layout::rowY(geometry.laneCycle, 3u)), laneX,
      laneWidth, labels, values, style);
  const double rotationFraction =
      selectedLength <= 1u ? 0.0
                           : static_cast<double>(displayedRotation) /
                                 static_cast<double>(selectedLength - 1u);
  const double rotationPosition =
      0.5 +
      std::copysign(std::sqrt(std::abs(rotationFraction)), rotationFraction) *
          0.5;
  drawProcessorSlider(
      "ROTATE ROWS",
      format("%+03d%s", displayedRotation, selectedRotationPreview ? "*" : ""),
      rotationPosition,
      static_cast<double>(layout::rowY(geometry.laneCycle, 4u)), laneX,
      laneWidth, labels, values, style);
  drawProcessorSlider("DENSITY",
                      format("%03lu%s", static_cast<unsigned long>(hits),
                             selectedDensityPreview ? "*" : ""),
                      static_cast<double>(hits) /
                          static_cast<double>(selectedLength),
                      static_cast<double>(layout::rowY(geometry.laneCycle, 5u)),
                      laneX, laneWidth, labels, values, style);
  drawProcessorToggle("VOL LINK", linkVelocityLength,
                      static_cast<double>(layout::rowY(geometry.laneCycle, 6u)),
                      laneX, laneWidth, labels, values, style);

  const double editLabelX =
      static_cast<double>(layout::processorLabelX(geometry.editShape.frame.x));
  std::vector<std::string> editLabels = {"TOOL", "SHAPE"};
  for (uint32_t row = 0u; row < 2u; ++row)
    drawAtPoint(editLabels[row],
                makePoint(editLabelX, static_cast<double>(layout::rowY(
                                          geometry.editShape, row)) -
                                          2.0),
                labels);
  std::vector<std::string> toolNames = {"SEL", "PNT", "ERS", "VEL"};
  for (std::size_t index = 0u; index < toolNames.size(); ++index) {
    S3GTrackerDrawSuiteActionButton(
        editToolButtonRect(index), toolNames[index], toolButtons[index].enabled,
        false, false, static_cast<std::size_t>(geometryTool) == index, false,
        false, false, true);
  }
  S3GTrackerDrawSuiteActionButton(reverseButtonRect(), "REVERSE",
                                  reverseButton.enabled, false, false, false,
                                  false, false, false, true);
  S3GTrackerDrawSuiteActionButton(reflectButtonRect(), "REFLECT",
                                  reflectButton.enabled, false, false, false,
                                  false, false, false, true);
  std::string morphTarget = morphTargetPopup.titleOfSelectedItem();
  drawTrackerProcessorMenu(
      "MORPH TO", !morphTarget.empty() ? morphTarget : "NEXT LANE",
      static_cast<double>(layout::rowY(geometry.editShape, 2u)),
      static_cast<double>(geometry.editShape.frame.x),
      static_cast<double>(geometry.editShape.frame.width), labels, values,
      style);
  drawAtPoint("AMOUNT",
              makePoint(editLabelX, static_cast<double>(
                                        layout::rowY(geometry.editShape, 3u)) -
                                        2.0),
              labels);
  std::vector<std::string> morphAmounts = {"25", "50", "75", "100"};
  for (std::size_t index = 0u; index < morphAmounts.size(); ++index) {
    S3GTrackerDrawSuiteActionButton(morphAmountButtonRect(index),
                                    morphAmounts[index],
                                    morphButtons[index].enabled, false, false,
                                    false, false, false, false, true);
  }

  const double viewX = static_cast<double>(geometry.view.frame.x);
  const double viewWidth = static_cast<double>(geometry.view.frame.width);
  std::string selectedViewTitle = viewModePopup.titleOfSelectedItem();
  drawTrackerProcessorMenu(
      "MODE", !selectedViewTitle.empty() ? selectedViewTitle : "RING FIELD",
      static_cast<double>(layout::rowY(geometry.view, 0u)), viewX, viewWidth,
      labels, values, style);

  const Rect bridgePanel = bridgePanelRect();
  drawToolboxHeaderActionButton(revealHeaderButtonRect(), bridgePanel,
                                "REVEAL IN TRACKER", values, style);
  std::string cellName = "REST";
  if (selectedRow < selectedTrack.notes.size()) {
    const auto &cell = selectedTrack.notes[selectedRow];
    if (cell.state == NoteCellState::Note) {
      cellName = format("%s · %03u", midiNoteName(cell.note),
                        static_cast<unsigned int>(cell.note));
    } else if (cell.state == NoteCellState::Burst)
      cellName = nsString(assetBankToken(cell.burstBankId) + ":" +
                          burstSlotToken(cell.note));
    else if (cell.state == NoteCellState::RetriggerPrevious)
      cellName = "RTR";
    else if (cell.state == NoteCellState::Hold)
      cellName = "HLD";
    else if (cell.state == NoteCellState::Kill)
      cellName = "KIL";
  }
  const double bridgeX = static_cast<double>(geometry.trackerBridge.frame.x);
  const double bridgeWidth =
      static_cast<double>(geometry.trackerBridge.frame.width);
  const double bridgeLabelX =
      static_cast<double>(layout::processorLabelX(bridgeX));
  const double bridgeControlX =
      static_cast<double>(layout::processorControlX(bridgeX));
  const double bridgeControlWidth =
      static_cast<double>(layout::processorMenuWidth(bridgeWidth));
  const auto drawBridgeValue = [&](std::string name, std::string value,
                                   uint32_t row, Color infoColor) {
    const double y =
        static_cast<double>(layout::rowY(geometry.trackerBridge, row));
    drawAtPoint(uppercase(name), makePoint(bridgeLabelX, y - 2.0), labels);
    Color color = infoColor;
    TextStyle infoAttrs{color, uiFont(10.0)};
    (fillColor_ = color);
    fillCurrentRect(makeRect(bridgeControlX, y, 2.0, 12.0));
    std::string display = menuDisplayText(
        value, std::max<double>(0.0, bridgeControlWidth - 12.0), infoAttrs);
    drawAtPoint(display, makePoint(bridgeControlX + 8.0, y - 2.0), infoAttrs);
  };
  drawBridgeValue("LANE",
                  format("T%02lu  %s",
                         static_cast<unsigned long>(selectedLane + 1u),
                         nsString(selectedTrack.name)),
                  0u, themeColor(ThemeRole::TextSecondary));
  drawBridgeValue("ROW",
                  format("%03lu  %s",
                         static_cast<unsigned long>(selectedRow + 1u),
                         cellName),
                  1u, themeColor(ThemeRole::Note));
  drawBridgeValue(
      "VELOCITY",
      format("%03d",
             static_cast<int>(std::lround(
                 resolvedVelocity(selectedTrack, selectedRow) * 127.0f))),
      2u, themeColor(ThemeRole::Value));
  drawBridgeValue(
      "STATE",
      editable ? "SHARED TRACKER SELECTION" : "SONG FOLLOW · EDITING LOCKED",
      3u, themeColor(editable ? ThemeRole::TextMuted : ThemeRole::Warning));
  drawOpenGeometryMenu();
}

void GeometryEditor::drawBurstPlaybackOverlay() {
  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);
  if (!model || !model->playing || !pattern ||
      _selectedBurstSlot >= model->session.burstLibrary.bursts.size())
    return;
  const auto &burst = model->session.burstLibrary.bursts[_selectedBurstSlot];
  if (burst.empty())
    return;
  bool sounding = false;
  const auto laneCount = std::min<std::size_t>(pattern->tracks.size(),
                                               model->notePlayheads.size());
  for (std::size_t lane = 0u; lane < laneCount; ++lane) {
    if (geometryLaneMuted(model, pattern, lane))
      continue;
    const auto &track = pattern->tracks[lane];
    const auto length =
        std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
    const auto row = model->notePlayheads[lane] % length;
    if (row >= track.notes.size())
      continue;
    const auto &cell = track.notes[row];
    if (cell.state == NoteCellState::Burst && cell.note == _selectedBurstSlot &&
        cell.burstBankId == model->activeBurstBankId) {
      sounding = true;
      break;
    }
  }
  if (!sounding)
    return;

  const double phase = std::clamp<double>(model->subrowPlaybackPhase, 0.0, 1.0);
  std::size_t activeEvent = 0u;
  bool eventStarted = false;
  for (std::size_t index = 0u; index < burst.eventCount; ++index) {
    const double onset =
        static_cast<double>(burst.events[index].position) / 65535.0;
    if (onset > phase)
      break;
    activeEvent = index;
    eventStarted = true;
  }
  if (eventStarted) {
    const Rect row = burstMatrixRowRect(activeEvent);
    fillRect(rInsetRect(row, 1.0, 1.0), trackerColor(0xffdf3f, 0.10));
    fillRect(
        makeRect(rMinX(row) + 1.0, rMinY(row) + 2.0, 3.0, rHeight(row) - 4.0),
        trackerColor(0xffdf3f, 0.92));
    strokeRect(rInsetRect(row, 1.5, 1.5), trackerColor(0xffdf3f, 0.55), 1.0);
  }

  const Rect graph = rInsetRect(burstBreakpointRect(), 12.0, 14.0);
  const double graphX = rMinX(graph) + phase * rWidth(graph);
  GeometryPath cursor = GeometryPath{};
  cursor.moveToPoint(makePoint(graphX, rMinY(graph) + 10.0));
  cursor.lineToPoint(makePoint(graphX, rMaxY(graph)));
  cursor.lineWidth = 1.4;
  (strokeColor_ = trackerColor(0xffdf3f, 0.88));
  strokePath(cursor);
  fillRect(makeRect(graphX - 2.0, rMinY(graph) + 7.0, 4.0, 4.0),
           trackerColor(0xffdf3f, 0.96));

  const Rect radialPlot = burstRadialPlotRect();
  const Point center = makePoint(rMidX(radialPlot), rMidY(radialPlot));
  const double radius =
      std::max<double>(50.0, std::min(rWidth(radialPlot), rHeight(radialPlot)) *
                                 0.34 * geometryZoom);
  const double angle = -static_cast<double>(kGeometryHalfPi) +
                       phase * 2.0 * static_cast<double>(kGeometryPi);
  drawGeometryReadHead(makePoint(center.x + std::cos(angle) * radius,
                                 center.y + std::sin(angle) * radius),
                       1.0, true, true);
}

void GeometryEditor::drawPlaybackOverlay() {
  if (geometryViewMode == GeometryModeBurst) {
    drawBurstPlaybackOverlay();
    return;
  }
  if (geometryViewMode == GeometryModePitchMap) {
    auto *model = trackerState;
    if (!model || !model->playing || model->session.pattern.tracks.empty())
      return;
    const auto lane = std::min(model->session.selectedTrack,
                               model->session.pattern.tracks.size() - 1u);
    std::size_t first = 0u;
    std::size_t last = 0u;
    pitchMapRowsFirst(&first, &last);
    const auto row = model->notePlayheads[lane];
    if (row < first || row > last)
      return;
    for (std::size_t graphIndex = 0u; graphIndex < 2u; ++graphIndex) {
      const bool interval = graphIndex == 1u;
      const Rect graph = interval ? pitchIntervalGraphRect() : pitchGraphRect();
      const double x =
          rMinX(graph) +
          static_cast<double>(row - first) /
              static_cast<double>(std::max<std::size_t>(1u, last - first)) *
              rWidth(graph);
      fillRect(makeRect(x - 1.0, rMinY(graph), 2.0, rHeight(graph)),
               trackerColor(0xffdf3f, 0.62));
      for (std::size_t index = 0u; index < _pitchPreview.assignments.size();
           ++index) {
        const auto &assignment = _pitchPreview.assignments[index];
        if (assignment.row != row)
          continue;
        const Point point =
            pitchMapPointForAssignmentAtIndex(index, false, interval);
        drawGeometryReadHead(point, 1.0, model->noteHits[lane], true);
        break;
      }
    }
    return;
  }
  auto *model = trackerState;
  const auto *pattern = geometryPattern(model);
  if (!model || !pattern || pattern->tracks.empty())
    return;
  const auto lanes = geometryLanes(pattern);
  const auto visible = visibleGeometryLanes(model);
  if (visible.count == 0u)
    return;
  const Point center = geometryCenter();
  const double cx = center.x;
  const double cy = center.y;
  const double maximum = geometryMaximumRadius();
  std::size_t focusLane = visible.indices[0u];
  for (std::size_t ordinal = 0u; ordinal < visible.count; ++ordinal) {
    if (visible.indices[ordinal] == model->session.selectedTrack) {
      focusLane = visible.indices[ordinal];
      break;
    }
  }
  const bool phaseSpokesMode = geometryViewMode == GeometryModePhaseSpokes;
  const bool laneFocusMode = geometryViewMode == GeometryModeLaneFocus;
  const bool compositeMode = geometryViewMode == GeometryModeCompositeRing;
  const double normalizedRadius = maximum * 0.72;
  for (std::size_t ordinal = lanes.count; ordinal-- > 0u;) {
    const auto lane = lanes.indices[ordinal];
    if (geometryLaneMuted(model, pattern, lane))
      continue;
    const auto &track = pattern->tracks[lane];
    const auto length =
        std::clamp<std::size_t>(track.noteColumn.length, 1u, 256u);
    const double regularRadius = ringRadiusForOrdinal(ordinal, lanes.count);
    const bool selected = lane == focusLane;
    const double radius =
        compositeMode
            ? normalizedRadius
            : laneFocusMode && selected ? normalizedRadius : regularRadius;
    if (phaseSpokesMode) {
      const auto phasePosition = model->notePlayheads[lane] % length;
      const double phaseAngle = -static_cast<double>(kGeometryHalfPi) +
                                static_cast<double>(phasePosition) * 2.0 *
                                    static_cast<double>(kGeometryPi) /
                                    static_cast<double>(length);
      const double cosine = std::cos(phaseAngle);
      const double sine = std::sin(phaseAngle);
      const double innerRadius = std::max<double>(7.0, radius * 0.16);
      GeometryPath spoke = GeometryPath{};
      spoke.moveToPoint(
          makePoint(cx + cosine * innerRadius, cy + sine * innerRadius));
      spoke.lineToPoint(makePoint(cx + cosine * radius, cy + sine * radius));
      spoke.lineWidth = selected ? 1.7 : 1.0;
      Color spokeColor = trackerColor(kLaneColors[lane % kLaneColors.size()],
                                      selected ? 0.92 : 0.48);
      (strokeColor_ = spokeColor);
      strokePath(spoke);
      const double markerRadius = selected ? 2.6 : 1.8;
      (fillColor_ = spokeColor);
      fillPath(GeometryPath::ellipse(
          makeRect(cx + cosine * radius - markerRadius,
                   cy + sine * radius - markerRadius, markerRadius * 2.0,
                   markerRadius * 2.0)));
    }
    if (laneFocusMode && !selected)
      continue;
    const bool documentationHit =
        _documentationPlaybackSnapshot && _documentationHitLanes[lane];
    const bool currentHit =
        documentationHit || (model->playing && model->noteHits[lane]);
    const double haloStrength = currentHit ? 1.0 : _readHeadHaloStrength[lane];
    if (haloStrength <= 0.0)
      continue;
    auto position = (documentationHit ? _readHeadHaloRows[lane]
                                      : currentHit ? model->noteHitRows[lane]
                                                   : _readHeadHaloRows[lane]) %
                    length;
    if (geometryViewMode == GeometryModeRingField)
      position = (position + length - track.noteColumn.phase % length) % length;
    const double angle = -static_cast<double>(kGeometryHalfPi) +
                         static_cast<double>(position) * 2.0 *
                             static_cast<double>(kGeometryPi) /
                             static_cast<double>(length);
    const Point point =
        makePoint(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
    const double alpha = selected ? 1.0 : compositeMode ? 0.72 : 0.76;
    drawGeometryReadHead(point, haloStrength * alpha, currentHit, selected);
  }
}

void GeometryEditor::drawGeometryReadHead(Point point, double intensity,
                                          bool currentHit, bool selected) {
  intensity = std::clamp<double>(intensity, 0.0, 1.0);
  if (intensity <= 0.0)
    return;
  Color yellow =
      withAlpha(trackerColor(0xffdf3f), intensity * (currentHit ? 1.0 : 0.78));
  const double coreRadius =
      currentHit ? (selected ? 5.4 : 4.8) : (selected ? 4.6 : 4.0);
  GeometryPath outline = GeometryPath::ellipse(
      makeRect(point.x - coreRadius - 1.0, point.y - coreRadius - 1.0,
               (coreRadius + 1.0) * 2.0, (coreRadius + 1.0) * 2.0));
  (fillColor_ = trackerColor(0x161300, currentHit ? 0.88 : 0.58 * intensity));
  fillPath(outline);
  GeometryPath core =
      GeometryPath::ellipse(makeRect(point.x - coreRadius, point.y - coreRadius,
                                     coreRadius * 2.0, coreRadius * 2.0));
  (fillColor_ = yellow);
  fillPath(core);
  GeometryPath center =
      GeometryPath::ellipse(makeRect(point.x - 1.7, point.y - 1.7, 3.4, 3.4));
  (fillColor_ =
       withAlpha(trackerColor(0xffef91), currentHit ? 1.0 : intensity * 0.72));
  fillPath(center);
}

} // namespace s3g::tracker::editor
