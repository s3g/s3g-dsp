#include "s3g/tracker/editor_grid_painter.h"
#include "s3g/tracker/editor_palette.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace s3g::tracker::editor {
namespace {
    constexpr double kGridHeaderHeight = app::kTrackerGridHeaderHeight;
    constexpr double kGridRowHeight = app::kTrackerGridRowHeight;
    constexpr double kGridRowNumberWidth = app::kTrackerRowNumberWidth;
    constexpr double kGridLaneGutter = app::kTrackerLaneGutter;
    constexpr double kGridColumnLabelTop = 21, kGridColumnLabelHeight = 13;
    constexpr double kGridColumnLengthTop = 34, kGridColumnLengthHeight = 13;
    constexpr double kGridColumnReadStartTop = 47, kGridColumnReadStartHeight = 13;
    constexpr double kGridColumnDirectionTop = 60, kGridColumnDirectionHeight = 13;
    constexpr double kGridColumnMuteTop = 73, kGridColumnMuteHeight = 13;
    constexpr std::array<uint32_t, 8> kLaneColors { 0x78918c, 0x9a826c, 0x817a99, 0x956f73,
        0x71889a, 0x87916f, 0x987b6d, 0x748c7b };
    Rect makeRect(double x, double y, double w, double h) { return { x, y, w, h }; }
    Point makePoint(double x, double y) { return { x, y }; }
    double rMinX(Rect r) { return r.x; }
    double rMinY(Rect r) { return r.y; }
    double rMaxX(Rect r) { return r.x + r.width; }
    double rMaxY(Rect r) { return r.y + r.height; }
    double rWidth(Rect r) { return r.width; }
    double rHeight(Rect r) { return r.height; }
    Rect rInsetRect(Rect r, double x, double y)
    {
        return { r.x + x, r.y + y, r.width - 2 * x, r.height - 2 * y };
    }
    bool rIntersectsRect(Rect a, Rect b)
    {
        return rMinX(a) < rMaxX(b) && rMaxX(a) > rMinX(b) && rMinY(a) < rMaxY(b)
            && rMaxY(a) > rMinY(b);
    }
    Rect rIntersectionRect(Rect a, Rect b)
    {
        const auto x = std::max(a.x, b.x), y = std::max(a.y, b.y);
        return { x, y, std::max(0., std::min(rMaxX(a), rMaxX(b)) - x),
            std::max(0., std::min(rMaxY(a), rMaxY(b)) - y) };
    }
    const char* arg(const std::string& s) { return s.c_str(); }
    template <class T> T arg(T v) { return v; }
    template <class... A> std::string format(const char* pattern, const A&... args)
    {
        const auto size = std::snprintf(nullptr, 0, pattern, arg(args)...);
        if (size < 0)
            return {};
        std::vector<char> data(static_cast<std::size_t>(size) + 1);
        std::snprintf(data.data(), data.size(), pattern, arg(args)...);
        return { data.data(), static_cast<std::size_t>(size) };
    }
}

GridPainter::GridPainter(const app::TrackerViewState& state, const app::GridSelection& selection,
    GridPaintServices services)
    : trackerState(&state)
    , selection_(selection)
    , services_(std::move(services))
{
}
Color GridPainter::literal(uint32_t rgb, double alpha) const
{
    if (services_.color)
        return services_.color(rgb, alpha);
    return { uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb),
        static_cast<uint8_t>(std::lround(std::clamp(alpha, 0., 1.) * 255.)) };
}
Color GridPainter::trackerColor(uint32_t rgb, double alpha) const
{
    auto r = uint8_t(rgb >> 16), g = uint8_t(rgb >> 8), b = uint8_t(rgb);
    if (std::max({ r, g, b }) - std::min({ r, g, b }) <= 8)
        rgb = nightNeutral(static_cast<uint8_t>((unsigned(r) + g + b) / 3));
    return literal(rgb, alpha);
}
void GridPainter::fillRect(Rect r, Color c) { list_.shape(Primitive::FillRect, r, c); }
void GridPainter::strokeRect(Rect r, Color c, double w)
{
    list_.shape(Primitive::StrokeRect, r, c, w);
}
void GridPainter::fillTrackerEllipse(Rect r, Color c) { list_.shape(Primitive::FillEllipse, r, c); }
void GridPainter::strokeTrackerPolyline(const std::vector<Point>& p, Color c, double w)
{
    list_.polyline(p, c, w);
}
void GridPainter::drawText(
    const std::string& text, Rect r, Color c, double size, FontWeight w, Alignment a)
{
    auto f = services_.font ? services_.font(size, w, false) : GridFont {};
    list_.text(text, r, c, f.name, f.size, r.y + f.baseline, a);
}
void GridPainter::drawCenteredText(
    const std::string& text, Rect r, Color c, double size, FontWeight w, Alignment a)
{
    auto f = services_.font ? services_.font(size, w, true) : GridFont {};
    r.y = std::floor(r.y + r.height * .5 - f.lineHeight * .5);
    r.height = f.lineHeight;
    list_.text(text, r, c, f.name, f.size, r.y + f.baseline, a);
}
DisplayList GridPainter::grid(Rect bounds, Rect dirty)
{
    list_ = {};
    bounds_ = bounds;
    paintGrid(dirty);
    return std::move(list_);
}
DisplayList GridPainter::envelope(Rect bounds)
{
    list_ = {};
    bounds_ = bounds;
    paintEnvelope();
    return std::move(list_);
}
DisplayList GridPainter::envelopePlayback(Rect bounds)
{
    list_ = {};
    bounds_ = bounds;
    paintEnvelopePlayback();
    return std::move(list_);
}

DisplayList GridPainter::gutter(Rect bounds, double scrollY, double zoom, bool wholeRows)
{
    list_ = {};
    bounds_ = bounds;
    auto pinned = [=](Rect r) {
        return Rect { r.x * zoom, (r.y - scrollY) * zoom, r.width * zoom, r.height * zoom };
    };
    const auto paint = [&]() {
        auto* model = trackerState;

        fillRect(bounds_, literal(themeRGB(ThemeRole::Workspace)));
        if (!model || !playbackFollowPattern(model))
            return;
        const double scale = std::max<double>(0.01, zoom);
        const Rect header = pinned(makeRect(0.0, 0.0, kGridRowNumberWidth, kGridHeaderHeight));
        if (rIntersectsRect(bounds_, header)) {
            fillRect(rIntersectionRect(bounds_, header), literal(themeRGB(ThemeRole::Panel)));
            const Rect focus = pinned(makeRect(0.0, 0.0, kGridRowNumberWidth, 2.0));
            fillRect(rIntersectionRect(bounds_, focus), literal(themeRGB(ThemeRole::Focus)));
            drawCenteredText("ROW",
                makeRect(2.0, rMaxY(header) - 16.0 * scale,
                    std::max<double>(1.0, rWidth(bounds_) - 7.0), 14.0 * scale),
                literal(themeRGB(ThemeRole::TextMuted)), 7.5 * scale, FontWeight::Semibold,
                Alignment::Right);
        }

        const auto rows = playbackFollowVisibleRows(model);
        const auto selectedRow = std::min(model->session.selectedRow, rows - 1u);
        Color gridColor = literal(themeRGB(ThemeRole::Grid), 0.70);
        for (std::size_t row = 0u; row < rows; ++row) {
            const double y = kGridHeaderHeight + static_cast<double>(row) * kGridRowHeight;
            const Rect rowRect = pinned(makeRect(0.0, y, kGridRowNumberWidth, kGridRowHeight));
            if (!rIntersectsRect(bounds_, rowRect))
                continue;
            fillRect(rowRect,
                (row % 4u) == 0u ? literal(themeRGB(ThemeRole::Control))
                                 : literal(themeRGB(ThemeRole::Raised)));
            fillRect(makeRect(rMaxX(rowRect) - std::max<double>(1.0, scale), rMinY(rowRect),
                         std::max<double>(1.0, scale), rHeight(rowRect)),
                gridColor);
            if (row >= model->session.transport.loopStartRow
                && row < model->session.transport.loopEndRow) {
                fillRect(rowRect,
                    literal(themeRGB(ThemeRole::Live),
                        model->session.transport.loopEnabled ? 0.075 : 0.035));
            }
            if ((wholeRows && selection_.active && row >= selection_.range().firstRow
                    && row <= selection_.range().lastRow)) {
                fillRect(rowRect, literal(themeRGB(ThemeRole::Selection), 0.52));
            }
            if (row == selectedRow) {
                fillRect(rowRect, literal(themeRGB(ThemeRole::Focus), 0.11));
            } else if ((row % 4u) == 0u) {
                fillRect(makeRect(rMinX(rowRect), rMinY(rowRect), rWidth(rowRect),
                             std::max<double>(1.0, scale)),
                    literal(themeRGB(ThemeRole::Border), 0.72));
            }
            drawCenteredText(format("%02lu", static_cast<unsigned long>(row + 1u)),
                rInsetRect(rowRect, 3.0 * scale, 0.0),
                row == selectedRow ? literal(themeRGB(ThemeRole::Focus))
                                   : (row % 4u) == 0u ? literal(themeRGB(ThemeRole::TextMuted))
                                                      : literal(themeRGB(ThemeRole::TextFaint)),
                9.6 * scale, row == selectedRow ? FontWeight::Semibold : FontWeight::Medium,
                Alignment::Right);
        }

        for (const uint32_t boundary :
            { model->session.transport.loopStartRow, model->session.transport.loopEndRow }) {
            if (boundary > rows)
                continue;
            const double y = kGridHeaderHeight + static_cast<double>(boundary) * kGridRowHeight;
            const Rect line = pinned(
                makeRect(0.0, y - (boundary == model->session.transport.loopEndRow ? 2.0 : 0.0),
                    kGridRowNumberWidth, 2.0));
            if (rIntersectsRect(bounds_, line)) {
                fillRect(line,
                    literal(themeRGB(ThemeRole::Live),
                        model->session.transport.loopEnabled ? 0.85 : 0.42));
            }
        }
    };
    paint();
    return std::move(list_);
}

void GridPainter::paintGrid(Rect dirtyRect)
{
    auto* model = trackerState;
    const auto* pattern = playbackFollowPattern(model);

    fillRect(bounds_, literal(themeRGB(ThemeRole::Workspace)));
    fillRect(makeRect(0.0, 0.0, rWidth(bounds_), 2.0), literal(themeRGB(ThemeRole::Focus)));
    if (!model || !pattern || pattern->tracks.empty()) {
        drawText("NO LANES", makeRect(20.0, 20.0, 200.0, 20.0), trackerColor(0x737a80), 10.0);
        return;
    }
    const auto& session = model->session;
    const auto laneCount
        = std::min<std::size_t>(s3g::tracker::kMaximumTrackCount, pattern->tracks.size());
    const auto rows = playbackFollowVisibleRows(model);
    const auto selectedLane = std::min(session.selectedTrack, laneCount - 1u);
    const auto selectedRow = std::min(session.selectedRow, rows - 1u);
    const double laneWidth = gridLaneWidth(model->sequenceColumnsExpanded);
    const double fieldWidth = gridLaneFieldWidth(laneWidth);
    Color grid = literal(themeRGB(ThemeRole::Grid), 0.70);
    Color dim = literal(themeRGB(ThemeRole::TextFaint));
    Color text = literal(themeRGB(ThemeRole::TextPrimary));
    Color focus = literal(themeRGB(ThemeRole::Focus));
    Color note = literal(themeRGB(ThemeRole::Note));
    Color value = literal(themeRGB(ThemeRole::Value));

    fillRect(makeRect(0.0, 0.0, rWidth(bounds_), kGridHeaderHeight),
        literal(themeRGB(ThemeRole::Panel)));
    for (std::size_t row = 0u; row < rows; ++row) {
        const double y = kGridHeaderHeight + static_cast<double>(row) * kGridRowHeight;
        if (!rIntersectsRect(dirtyRect, makeRect(0.0, y, rWidth(bounds_), kGridRowHeight)))
            continue;
        fillRect(makeRect(0.0, y, rWidth(bounds_), kGridRowHeight),
            (row % 4u) == 0u ? literal(themeRGB(ThemeRole::Raised))
                             : literal(themeRGB(ThemeRole::Panel)));
        fillRect(makeRect(0.0, y, kGridRowNumberWidth - 1.0, kGridRowHeight),
            (row % 4u) == 0u ? literal(themeRGB(ThemeRole::Control))
                             : literal(themeRGB(ThemeRole::Raised)));
        fillRect(makeRect(kGridRowNumberWidth - 1.0, y, 1.0, kGridRowHeight), grid);
        if (row >= session.transport.loopStartRow && row < session.transport.loopEndRow) {
            fillRect(makeRect(0.0, y, rWidth(bounds_), kGridRowHeight),
                literal(themeRGB(ThemeRole::Live), session.transport.loopEnabled ? 0.075 : 0.035));
        }
        if (row == selectedRow) {
            fillRect(makeRect(0.0, y, rWidth(bounds_), kGridRowHeight),
                literal(themeRGB(ThemeRole::Focus), 0.11));
        } else if ((row % 4u) == 0u) {
            fillRect(
                makeRect(0.0, y, rWidth(bounds_), 1.0), literal(themeRGB(ThemeRole::Border), 0.72));
        }
        // The frozen overlay is the sole row-label renderer. A second copy in
        // the scrolling document creates fuzzy overdraw at the origin and
        // exposes moving glyphs during horizontal scrolling.
    }

    if (session.transport.loopStartRow < rows) {
        const double y = kGridHeaderHeight
            + static_cast<double>(session.transport.loopStartRow) * kGridRowHeight;
        fillRect(makeRect(0.0, y, rWidth(bounds_), 2.0),
            literal(themeRGB(ThemeRole::Live), session.transport.loopEnabled ? 0.85 : 0.42));
    }
    if (session.transport.loopEndRow <= rows) {
        const double y = kGridHeaderHeight
            + static_cast<double>(session.transport.loopEndRow) * kGridRowHeight - 2.0;
        fillRect(makeRect(0.0, y, rWidth(bounds_), 2.0),
            literal(themeRGB(ThemeRole::Live), session.transport.loopEnabled ? 0.85 : 0.42));
    }

    const double laneHeight = kGridHeaderHeight + static_cast<double>(rows) * kGridRowHeight;
    for (std::size_t lane = 0u; lane + 1u < laneCount; ++lane) {
        const double gutterX = gridLaneX(lane, laneWidth) + laneWidth;
        fillRect(makeRect(gutterX, 0.0, kGridLaneGutter, laneHeight), trackerColor(0x090b0c));
    }

    for (std::size_t lane = 0u; lane < laneCount; ++lane) {
        const auto& track = pattern->tracks[lane];
        const bool recordArmed = model->midiStepRecordMode != MidiStepRecordMode::Off
            && lane == std::min(model->midiRecordTrack, laneCount - 1u);
        const bool songMuted = model->songPlaybackActive
            && (model->songPlaybackMutedTracks & (uint32_t { 1u } << lane)) != 0u;
        const auto page = 0u;
        const auto fieldCount = gridFieldCount(model->sequenceColumnsExpanded);
        std::array<const ColumnDefinition*, 7u> columns { {
            &track.noteColumn,
            &track.velocityColumn,
            &track.fxPairs[0u].actionColumn,
            &track.fxPairs[0u].valueColumn,
            &track.fxPairs[1u].actionColumn,
            &track.fxPairs[1u].valueColumn,
            &track.gateColumn,
        } };
        const double laneX = gridLaneX(lane, laneWidth);
        const double x = gridLaneFieldX(lane, laneWidth);
        if (!rIntersectsRect(dirtyRect, makeRect(laneX, 0.0, laneWidth, laneHeight)))
            continue;
        const auto identityColor = trackerColor(kLaneColors[lane % kLaneColors.size()],
            track.noteColumn.muted || songMuted ? 0.35 : lane == selectedLane ? 1.0 : 0.72);
        bool allMuted = songMuted;
        if (!songMuted) {
            allMuted = true;
            for (std::size_t field = 0u; field < fieldCount; ++field)
                allMuted = allMuted && columns[field]->muted;
        }

        fillRect(makeRect(laneX, 2.0, laneWidth, kGridHeaderHeight - 2.0),
            lane == selectedLane ? literal(themeRGB(ThemeRole::Selection))
                                 : literal(themeRGB(ThemeRole::Raised)));
        fillRect(makeRect(laneX, 2.0, 3.0, std::max<double>(0.0, laneHeight - 2.0)), identityColor);
        double nameInset = 6.0;
        if (recordArmed) {
            const Rect armedRect = makeRect(x + 5.0, 5.0, 25.0, 13.0);
            fillRect(armedRect, literal(themeRGB(ThemeRole::Danger), 0.24));
            strokeRect(armedRect, literal(themeRGB(ThemeRole::Danger)));
            drawCenteredText("REC", armedRect, literal(themeRGB(ThemeRole::Danger)), 6.5,
                FontWeight::Semibold, Alignment::Center);
            nameInset = 34.0;
        }
        drawCenteredText(
            std::string(track.name.empty() ? "LANE " + std::to_string(lane + 1u) : track.name),
            makeRect(
                x + nameInset, 3.0, std::max<double>(1.0, fieldWidth - 88.0 - nameInset), 18.0),
            allMuted ? dim : text, 9.5, FontWeight::Semibold, Alignment::Left);
        const Rect channelRect = gridLaneChannelRect(x, fieldWidth);
        const Rect resyncRect = gridLaneResyncRect(x, fieldWidth);
        fillRect(resyncRect, literal(themeRGB(ThemeRole::Control)));
        fillRect(channelRect, literal(themeRGB(ThemeRole::Control)));
        strokeRect(resyncRect, lane == selectedLane ? focus : literal(themeRGB(ThemeRole::Border)));
        strokeRect(channelRect, lane == selectedLane ? note : literal(themeRGB(ThemeRole::Border)));
        drawCenteredText("SYNC", resyncRect, allMuted ? dim : focus, 6.2, FontWeight::Semibold,
            Alignment::Center);
        drawCenteredText(
            format("CH%02u", static_cast<unsigned int>(std::clamp<int>(track.midiChannel, 1, 16))),
            rInsetRect(channelRect, 2.0, 0.0), allMuted ? dim : note, 8.0, FontWeight::Semibold,
            Alignment::Center);
        for (std::size_t field = 0u; field < fieldCount; ++field) {
            const auto* column = columns[field];
            const Rect headerField = gridFieldRect(x, kGridColumnLabelTop, fieldWidth,
                kGridHeaderHeight - kGridColumnLabelTop, model->sequenceColumnsExpanded, field);
            constexpr std::array<const char*, 7u> labels {
                "NOTE",
                "VOL",
                "SEQ1",
                "V1",
                "SEQ2",
                "V2",
                "GATE",
            };
            std::string label = std::string(labels[field]);
            const auto followLane = model->trackerFollow.selectedLane
                ? model->session.selectedTrack : std::size_t(model->trackerFollow.lane);
            if (field == 0 && lane == followLane
                && model->trackerFollow.mode != TrackerFollowMode::Static)
                label = "NOTE >";
            if (gridFieldIsSequence(field) && !gridFieldIsSequenceAction(field)) {
                const auto mode = track.fxPairs[gridSequencePair(field)].valueInterpolation;
                label = format("%s %s", label, mode == ValueInterpolation::Linear ? "LIN" : "STP");
            }
            std::string stride = column->stride == 1u ? "" : format("×%u", column->stride);
            std::string state
                = format("L%lu%s", static_cast<unsigned long>(column->length), stride);
            const auto columnLength = std::max<std::size_t>(1u, column->length);
            std::string readStart = format(
                "READ %02lu", static_cast<unsigned long>(column->phase % columnLength + 1u));
            std::string direction = format("DIR %s", directionMark(column->direction));
            Color fieldColor = field == 0u
                ? note
                : gridFieldIsSequenceAction(field) ? literal(themeRGB(ThemeRole::Warning)) : value;
            drawCenteredText(label,
                rInsetRect(makeRect(rMinX(headerField), kGridColumnLabelTop, rWidth(headerField),
                               kGridColumnLabelHeight),
                    2.0, 0.0),
                column->muted ? dim : fieldColor,
                gridFieldIsSequence(field) && !gridFieldIsSequenceAction(field) ? 6.8 : 7.8,
                FontWeight::Semibold, Alignment::Center);
            drawCenteredText(state,
                rInsetRect(makeRect(rMinX(headerField), kGridColumnLengthTop, rWidth(headerField),
                               kGridColumnLengthHeight),
                    2.0, 0.0),
                literal(themeRGB(ThemeRole::TextMuted)), 6.8, FontWeight::Medium,
                Alignment::Center);
            drawCenteredText(readStart,
                rInsetRect(makeRect(rMinX(headerField), kGridColumnReadStartTop,
                               rWidth(headerField), kGridColumnReadStartHeight),
                    2.0, 0.0),
                column->muted ? literal(themeRGB(ThemeRole::TextFaint))
                              : literal(themeRGB(ThemeRole::TextMuted)),
                5.9, FontWeight::Medium, Alignment::Center);
            const Rect directionButton
                = rInsetRect(makeRect(rMinX(headerField), kGridColumnDirectionTop,
                                 rWidth(headerField), kGridColumnDirectionHeight),
                    2.0, 1.0);
            fillRect(directionButton, literal(themeRGB(ThemeRole::Control)));
            strokeRect(directionButton, literal(themeRGB(ThemeRole::Border)));
            drawCenteredText(direction, directionButton,
                column->muted ? literal(themeRGB(ThemeRole::TextFaint))
                              : literal(themeRGB(ThemeRole::TextSecondary)),
                6.4, FontWeight::Medium, Alignment::Center);
            const Rect muteButton = rInsetRect(makeRect(rMinX(headerField), kGridColumnMuteTop,
                                                   rWidth(headerField), kGridColumnMuteHeight),
                2.0, 1.0);
            fillRect(muteButton,
                column->muted ? literal(themeRGB(ThemeRole::Danger), 0.24)
                              : literal(themeRGB(ThemeRole::Control)));
            strokeRect(muteButton,
                column->muted ? literal(themeRGB(ThemeRole::Danger))
                              : literal(themeRGB(ThemeRole::Border)));
            drawCenteredText("MUTE", muteButton,
                column->muted ? literal(themeRGB(ThemeRole::Danger))
                              : literal(themeRGB(ThemeRole::TextMuted)),
                6.4, column->muted ? FontWeight::Semibold : FontWeight::Medium, Alignment::Center);
            if (field > 0u) {
                fillRect(makeRect(rMinX(headerField), kGridColumnLabelTop + 1.0, 1.0,
                             kGridHeaderHeight - kGridColumnLabelTop - 3.0),
                    grid);
            }
        }
        if (lane == selectedLane)
            fillRect(makeRect(laneX + 1.0, kGridHeaderHeight - 3.0, laneWidth - 2.0, 3.0), focus);

        for (std::size_t row = 0u; row < rows; ++row) {
            const double y = kGridHeaderHeight + static_cast<double>(row) * kGridRowHeight;
            if (!rIntersectsRect(dirtyRect, makeRect(laneX, y, laneWidth, kGridRowHeight)))
                continue;
            const bool selected = lane == selectedLane && row == selectedRow;
            for (std::size_t field = 0u; field < fieldCount; ++field) {
                const auto* column = columns[field];
                const Rect fieldRect = gridFieldRect(
                    x, y, fieldWidth, kGridRowHeight, model->sequenceColumnsExpanded, field);
                const auto pairIndex = gridFieldIsSequence(field) ? gridSequencePair(field) : 0u;
                const bool head = model->playing
                    && (field == 0u ? row == model->notePlayheads[lane]
                                    : field == 1u
                                ? row == model->velocityPlayheads[lane]
                                : gridFieldIsGate(field) ? row == model->notePlayheads[lane]
                                                         : gridFieldIsSequenceAction(field)
                                        ? row == model->fxActionPlayheads[lane][pairIndex]
                                        : row == model->fxValuePlayheads[lane][pairIndex]);
                Color activeColor = field == 0u
                    ? text
                    : gridFieldIsSequenceAction(field) ? literal(themeRGB(ThemeRole::Warning))
                                                       : value;
                if (head) {
                    fillRect(rInsetRect(fieldRect, 1.0, 1.0),
                        literal(themeRGB(ThemeRole::GridPlayback)));
                }
                const bool unavailable = songMuted || column->muted || row >= column->length;
                if (unavailable) {
                    fillRect(rInsetRect(fieldRect, 1.0, 1.0), trackerColor(0x090b0c, 0.70));
                }
                fillRect(
                    makeRect(rMaxX(fieldRect) - 1.0, rMinY(fieldRect), 1.0, rHeight(fieldRect)),
                    grid);
                fillRect(makeRect(rMinX(fieldRect), rMaxY(fieldRect) - 1.0, rWidth(fieldRect), 1.0),
                    grid);
                const bool inSelection = selection_.active
                    && selection_.containsLinear(page, lane, field, row, fieldCount);
                if (inSelection) {
                    fillRect(rInsetRect(fieldRect, 1.0, 1.0),
                        literal(themeRGB(ThemeRole::GridSelection)));
                }
                const bool cursor
                    = selected && field == std::min(session.selectedField, fieldCount - 1u);
                if (cursor) {
                    fillRect(
                        rInsetRect(fieldRect, 1.0, 1.0), literal(themeRGB(ThemeRole::GridCursor)));
                }

                std::string cellValue = "---";
                bool active = false;
                if (field == 0u) {
                    const NoteCell noteCell
                        = row < track.notes.size() ? track.notes[row] : NoteCell::rest();
                    cellValue = noteText(noteCell, model->showMidiNoteValues);
                    active = noteCell.state != NoteCellState::Rest;
                } else if (field == 1u) {
                    cellValue = volumeText(track, row);
                    active = row < track.velocities.size()
                        && track.velocities[row].state == ValueCellState::Value;
                } else if (gridFieldIsGate(field)) {
                    cellValue = gateText(track, row);
                    active = row < track.gates.size() && track.gates[row].voiceCount > 0u;
                } else if (gridFieldIsSequenceAction(field)) {
                    cellValue = fxActionText(track, pairIndex, row);
                    active = !(cellValue == "---");
                } else {
                    cellValue = fxValueText(track, pairIndex, row);
                    active = row < track.fxPairs[pairIndex].values.size()
                        && track.fxPairs[pairIndex].values[row].state == FxValueCellState::Value;
                }
                drawCenteredText(cellValue, rInsetRect(fieldRect, 3.0, 1.0),
                    active && !unavailable ? activeColor : dim, field == 1u ? 9.2 : 10.0,
                    active ? FontWeight::Medium : FontWeight::Regular,
                    field == 1u ? Alignment::Right : Alignment::Center);
                if (head && !inSelection && !cursor) {
                    fillRect(makeRect(rMinX(fieldRect) + 1.0, y + 3.0, 2.0, kGridRowHeight - 6.0),
                        unavailable ? dim : literal(themeRGB(ThemeRole::GridPlaybackAccent)));
                }
            }
        }
        for (std::size_t field = 0u; field < fieldCount; ++field) {
            const auto* column = columns[field];
            if (column->length > rows)
                continue;
            const double lengthY
                = kGridHeaderHeight + static_cast<double>(column->length) * kGridRowHeight;
            const Rect fieldRect = gridFieldRect(
                x, lengthY - 1.0, fieldWidth, 2.0, model->sequenceColumnsExpanded, field);
            Color lengthColor = field == 0u
                ? note
                : gridFieldIsSequenceAction(field) ? literal(themeRGB(ThemeRole::Warning)) : value;
            fillRect(fieldRect, column->muted ? dim : lengthColor);
        }
    }
}
void GridPainter::paintEnvelope()
{

    fillRect(bounds_, trackerColor(0x1d1d1d));
    strokeRect(rInsetRect(bounds_, 0.5, 0.5), trackerColor(0x565656));
    fillRect(makeRect(0.0, 0.0, rWidth(bounds_), 24.0), trackerColor(0x131313));
    fillRect(makeRect(0.0, 0.0, rWidth(bounds_), 2.0), trackerColor(0xb8b8b8));
    auto* model = trackerState;
    const auto* pattern = playbackFollowPattern(model);
    if (!model || !pattern || pattern->tracks.empty())
        return;
    const auto lane = std::min(model->session.selectedTrack, pattern->tracks.size() - 1u);
    const auto& track = pattern->tracks[lane];
    const auto field = std::min<std::size_t>(model->session.selectedField, 6u);
    const bool gateField = gridFieldIsGate(field);
    const bool sequenceValue = gridFieldIsSequence(field) && !gridFieldIsSequenceAction(field);
    const auto pairIndex = sequenceValue ? gridSequencePair(field) : 0u;
    const auto rows = std::max<std::size_t>(16u,
        std::min<std::size_t>(256u,
            gateField ? track.gateColumn.length
                      : !sequenceValue ? track.velocityColumn.length
                                       : track.fxPairs[pairIndex].valueColumn.length));
    std::string envelopeName = gateField
        ? "GATE (0–4 ROWS)"
        : !sequenceValue ? "VOLUME"
                         : format("SEQUENCE %lu VALUE", static_cast<unsigned long>(pairIndex + 1u));
    drawText(format("%s ENVELOPE  /  T%lu  /  %s%s", envelopeName,
                 static_cast<unsigned long>(lane + 1u), std::string(playbackFollowPatternId(model)),
                 model->songPlaybackActive ? " PLAYING" : ""),
        makeRect(8.0, 6.0, rWidth(bounds_) - 16.0, 16.0), trackerColor(0xa8a8a8), 9.5);
    const double left = 30.0, right = 10.0, top = 34.0, bottom = 22.0;
    const double width = std::max<double>(1.0, rWidth(bounds_) - left - right);
    const double height = std::max<double>(1.0, rHeight(bounds_) - top - bottom);
    fillRect(makeRect(left, top, width, height), trackerColor(0x0c0c0c));
    strokeRect(makeRect(left, top, width, height), trackerColor(0x565656));
    drawText("1.0", makeRect(2.0, top - 5.0, 25.0, 12.0), trackerColor(0x737a80), 7.0,
        FontWeight::Regular, Alignment::Right);
    drawText("0", makeRect(2.0, top + height - 6.0, 25.0, 12.0), trackerColor(0x737a80), 7.0,
        FontWeight::Regular, Alignment::Right);
    std::vector<Point> curve;
    curve.reserve(rows);
    for (std::size_t row = 0u; row < rows; ++row) {
        const double x
            = left + (static_cast<double>(row) + 0.5) * width / static_cast<double>(rows);
        fillRect(makeRect(x, top, 0.5, height), trackerColor(0x292d30));
        const auto gate = row < track.gates.size() ? track.gates[row].gateVoice(0u) : GateVoice {};
        const float value = gateField
            ? (gate.mode == GateVoiceMode::Tie
                    ? 1.0f
                    : gate.mode == GateVoiceMode::Rows ? std::clamp(gate.rows / 4.0f, 0.0f, 1.0f)
                                                       : 0.175f)
            : !sequenceValue ? resolvedVelocity(track, row)
                             : resolvedFxValue(track, pairIndex, row);
        const double y = top + (1.0 - value) * height;
        curve.push_back(makePoint(x, y));
    }
    Color curveColor = trackerColor(0xb8b8b8, 0.8);
    strokeTrackerPolyline(curve, curveColor, 1.2);
    for (std::size_t row = 0u; row < rows; ++row) {
        const bool explicitValue = gateField
            ? row < track.gates.size() && track.gates[row].voiceCount > 0u
            : !sequenceValue ? row < track.velocities.size()
                    && track.velocities[row].state == ValueCellState::Value
                             : row < track.fxPairs[pairIndex].values.size()
                    && track.fxPairs[pairIndex].values[row].state == FxValueCellState::Value;
        if (!explicitValue)
            continue;
        const double x
            = left + (static_cast<double>(row) + 0.5) * width / static_cast<double>(rows);
        const auto gate = row < track.gates.size() ? track.gates[row].gateVoice(0u) : GateVoice {};
        const float value = gateField
            ? (gate.mode == GateVoiceMode::Tie
                    ? 1.0f
                    : gate.mode == GateVoiceMode::Rows ? std::clamp(gate.rows / 4.0f, 0.0f, 1.0f)
                                                       : 0.175f)
            : !sequenceValue ? resolvedVelocity(track, row)
                             : resolvedFxValue(track, pairIndex, row);
        const double y = top + (1.0 - value) * height;
        const bool notePresent = row < track.noteColumn.length && row < track.notes.size()
            && noteCellIsActivePulse(track.notes[row]);
        const bool selected = row == model->session.selectedRow;
        if (selected) {
            fillTrackerEllipse(
                makeRect(x - 4.5, y - 4.5, 9.0, 9.0), literal(themeRGB(ThemeRole::TextPrimary)));
        }
        const double pointSize = notePresent ? 7.0 : 4.0;
        const Rect point = makeRect(x - pointSize * 0.5, y - pointSize * 0.5, pointSize, pointSize);
        Color pointColor = notePresent ? trackerColor(0x14c7eb) : trackerColor(0x4c4c4c);
        fillTrackerEllipse(point, pointColor);
    }
    if (model->songPlaybackActive) {
        drawText("SONG PLAYBACK FOLLOW  /  READ ONLY",
            makeRect(left, rHeight(bounds_) - 17.0, width, 12.0), trackerColor(0x737a80), 7.0);
    }
}
void GridPainter::paintEnvelopePlayback()
{
    auto* model = trackerState;
    const auto* pattern = playbackFollowPattern(model);
    if (!model || !model->playing || !pattern || pattern->tracks.empty())
        return;
    const auto lane = std::min(model->session.selectedTrack, pattern->tracks.size() - 1u);
    const auto& track = pattern->tracks[lane];
    const auto field = std::min<std::size_t>(model->session.selectedField, 6u);
    const bool gateField = gridFieldIsGate(field);
    const bool sequenceValue = gridFieldIsSequence(field) && !gridFieldIsSequenceAction(field);
    const auto pairIndex = sequenceValue ? gridSequencePair(field) : 0u;
    const auto rows = std::max<std::size_t>(16u,
        std::min<std::size_t>(256u,
            gateField ? track.gateColumn.length
                      : !sequenceValue ? track.velocityColumn.length
                                       : track.fxPairs[pairIndex].valueColumn.length));
    const double left = 30.0, right = 10.0, top = 34.0, bottom = 22.0;
    const double width = std::max<double>(1.0, rWidth(bounds_) - left - right);
    const double height = std::max<double>(1.0, rHeight(bounds_) - top - bottom);
    const auto playhead = (gateField ? model->notePlayheads[lane]
                                     : !sequenceValue ? model->velocityPlayheads[lane]
                                                      : model->fxValuePlayheads[lane][pairIndex])
        % rows;
    const double x
        = left + (static_cast<double>(playhead) + 0.5) * width / static_cast<double>(rows);
    fillRect(makeRect(x - 1.0, top, 2.0, height), trackerColor(0xb8b8b8, 0.7));
}

}
