#pragma once

namespace s3g::sample_neon_gui {

struct CanvasLayout {
    static constexpr unsigned width = 1504, height = 846; // Exact 16:9.
    static constexpr double panelTop = 42, statusTop = 816;
    static constexpr double workspaceBottom = 768; // Grains' final row + standard clearance.
    static constexpr double statusLeft = 20, statusWidth = width - 40;
    static constexpr double titleTop = 13, titleHeight = 15, statusRightInset = 18;
};

// Logical pixels at 100%; all right-hand pages share the same row centers.
// Keeping centers independent of control height prevents mixed menus/sliders
// from introducing half-row gaps. The miniature NEON is laid out separately.
template<int Left> struct ToolboxLayout {
    static constexpr double left = Left, width = 270.0;
    // Dense, two-column sample editor: explicit compact 24 px family pitch.
    static constexpr double rowPitch = 24.0, firstCenter = 78.0;
    static constexpr double labelWidth = 78.0;
    static constexpr double controlLeft = left + 82.0;
    static constexpr double controlWidth = width - 82.0;
    static constexpr double trackWidth = 117.0, valueGap = 8.0, valueWidth = 63.0;
    static constexpr double buttonHeight = 20.0, buttonGap = 8.0;
    static constexpr double menuHeight = 15.0, popupRowHeight = 18.0;
    static constexpr double row(unsigned index) { return firstCenter + rowPitch * index; }
    static constexpr double slider(unsigned index) { return row(index) - 7.0; }
    static constexpr double menu(unsigned index) { return row(index) - 12.5; }
    static constexpr double button(unsigned index) { return row(index) - buttonHeight * 0.5; }
    static constexpr double label(unsigned index) { return row(index) - 9.0; }
    static constexpr double columnWidth(unsigned count) { return (width - buttonGap * (count - 1u)) / count; }
    static constexpr double columnLeft(unsigned column, unsigned count) { return left + column * (columnWidth(count) + buttonGap); }
    static constexpr double statusTop = CanvasLayout::statusTop;
};
using InspectorLayout = ToolboxLayout<880>;
using PadLayout = ToolboxLayout<1198>;

// Existing engine-row addresses start at five. Compact that body into the
// three rows formerly occupied by duplicate Character FX controls, keeping
// the shared 24 px pitch and the same label/track/value widths.
struct PlaybackLayout : InspectorLayout {
    static constexpr unsigned index(unsigned row) { return row >= 5 ? row - 3 : row; }
    static constexpr double row(unsigned index) { return InspectorLayout::row(PlaybackLayout::index(index)); }
    static constexpr double slider(unsigned index) { return row(index) - 7; }
    static constexpr double menu(unsigned index) { return row(index) - 12.5; }
    static constexpr double button(unsigned index) { return row(index) - buttonHeight * .5; }
    static constexpr double label(unsigned index) { return row(index) - 9; }
};

struct MiniLayout : PadLayout {
    static constexpr double top = 276, bottom = 694;
    static constexpr double pageLeft = left, pageTop = 304.5;
    static constexpr double pageWidth = 61.5, pageGap = 8.0, pageHeight = 15.0;
    static constexpr double pageCenter(unsigned page) { return pageLeft + page * (pageWidth + pageGap) + pageWidth * .5; }
    static constexpr double pageCenterY = pageTop + pageHeight * .5;
    static constexpr double encoderLeft = controlLeft, encoderTop = 330.5, encoderWidth = controlWidth, encoderHeight = 15;
    static constexpr double encoderCenter = encoderLeft + encoderWidth * .5;
    static constexpr double encoderCenterY = encoderTop + encoderHeight * .5;
    static constexpr double trax = 366, loop = 392, banks = 418, actions = 598, fill = 624, master = 650, capture = 676;
    static constexpr double padWidth = pageWidth, padHeight = 62, padTop = 442, padGapY = 10;
    static constexpr double padX(unsigned pad) { return pageLeft + (pad % 4) * (pageWidth + pageGap); }
    static constexpr double padY(unsigned pad) { return padTop + (pad / 4) * (padHeight + padGapY); }
};

// The waveform, overview/path and route status never move when a page, engine
// or toolbox changes. All engine controls live in EDIT / PLAYBACK; the fixed
// workspace never lends them height. Draw, drag, wheel and tests share bounds.
struct WaveLayout {
    static constexpr double panelLeft = 20, panelWidth = 823;
    static constexpr double left = 36, top = 126, width = 791;
    static constexpr double actionCenter = 78, viewCenter = 102, controlHeight = 15;
    static constexpr double actionTop = actionCenter - controlHeight * .5;
    static constexpr double viewTop = viewCenter - controlHeight * .5;
    static constexpr double overviewLeft = left, overviewWidth = width, overviewHeight = 72;
    static constexpr double bottom() { return CanvasLayout::workspaceBottom; }
    static constexpr double overviewTop() { return bottom() - 118; }
    static constexpr double height() { return overviewTop() - 14 - top; }
    static constexpr double routeTop() { return bottom() - 32; }
    // Stack path and overview share the main waveform's horizontal bounds.
    static constexpr double pathLeft = left, pathWidth = width, pathHeight = overviewHeight;
    static constexpr double pathTop = CanvasLayout::workspaceBottom - 118;
};

// Extra-control addresses retained by the editor helpers: 10..15, 17..19.
// A single contiguous inspector column, packed for the selected Motion sound.
// Cutoff follows the final detail row. No below-waveform toolbox remains.
struct FamilyGrid {
    static constexpr unsigned articulationCount(unsigned sound) { return sound == 2 ? 5 : sound == 1 ? 4 : 2; }
    static constexpr unsigned index(unsigned address, bool motion = false, unsigned sound = 0) {
        return (motion ? 18u : 19u) + (address < 16 ? address - 10
            : motion ? articulationCount(sound) + address - 17 : address - 11);
    }
    static constexpr unsigned cutoff(bool motion, unsigned sound = 0) {
        return motion ? 18u + articulationCount(sound) + 3u : 28u;
    }
    static constexpr double row(unsigned address, bool motion = false, unsigned sound = 0) {
        return InspectorLayout::row(index(address, motion, sound));
    }
    static constexpr double controlLeft(unsigned) { return InspectorLayout::controlLeft; }
};

static_assert(InspectorLayout::trackWidth + InspectorLayout::valueGap + InspectorLayout::valueWidth == InspectorLayout::controlWidth);
static_assert(InspectorLayout::button(20u) + InspectorLayout::buttonHeight < InspectorLayout::statusTop);
static_assert(CanvasLayout::width * 9u == CanvasLayout::height * 16u);
static_assert(WaveLayout::height() == 510); // Fixed on every engine and page.
static_assert(WaveLayout::bottom() == InspectorLayout::row(FamilyGrid::cutoff(false)) + 18);
static_assert(WaveLayout::pathTop == WaveLayout::overviewTop());
static_assert(WaveLayout::pathLeft == WaveLayout::left && WaveLayout::pathWidth == WaveLayout::width);
static_assert(WaveLayout::overviewLeft == WaveLayout::left && WaveLayout::overviewWidth == WaveLayout::width);
static_assert(PlaybackLayout::row(5) == InspectorLayout::row(2));
static_assert(InspectorLayout::row(FamilyGrid::cutoff(false)) + 18 + 8 <= CanvasLayout::statusTop);
static_assert(InspectorLayout::row(FamilyGrid::cutoff(true, 2)) + 18 + 8 <= CanvasLayout::statusTop);
static_assert(MiniLayout::capture + 18 == MiniLayout::bottom);
static_assert(MiniLayout::padY(7) + MiniLayout::padHeight + 22 == MiniLayout::actions);
static_assert(CanvasLayout::height == CanvasLayout::statusTop + 18 + 12);

} // namespace s3g::sample_neon_gui
