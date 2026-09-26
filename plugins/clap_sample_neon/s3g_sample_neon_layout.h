#pragma once

namespace s3g::sample_neon_gui {

// Logical pixels at 100%; all right-hand pages share the same row centers.
// Keeping centers independent of control height prevents mixed menus/sliders
// from introducing half-row gaps. The miniature NEON is laid out separately.
struct InspectorLayout {
    static constexpr double left = 870.0, width = 278.0;
    static constexpr double rowPitch = 28.0, firstCenter = 90.0;
    static constexpr double labelWidth = 78.0;
    static constexpr double controlLeft = left + 82.0;
    static constexpr double controlWidth = width - 82.0;
    static constexpr double trackWidth = 125.0, valueGap = 8.0, valueWidth = 63.0;
    static constexpr double buttonHeight = 20.0, buttonGap = 8.0;
    static constexpr double menuHeight = 15.0, popupRowHeight = 18.0;
    static constexpr double row(unsigned index) { return firstCenter + rowPitch * index; }
    static constexpr double slider(unsigned index) { return row(index) - 7.0; }
    static constexpr double menu(unsigned index) { return row(index) - 12.5; }
    static constexpr double button(unsigned index) { return row(index) - buttonHeight * 0.5; }
    static constexpr double label(unsigned index) { return row(index) - 9.0; }
    static constexpr double columnWidth(unsigned count) { return (width - buttonGap * (count - 1u)) / count; }
    static constexpr double columnLeft(unsigned column, unsigned count) { return left + column * (columnWidth(count) + buttonGap); }
    static constexpr double statusTop = firstCenter + rowPitch * 21u - 9.0;
};

static_assert(InspectorLayout::trackWidth + InspectorLayout::valueGap + InspectorLayout::valueWidth == InspectorLayout::controlWidth);
static_assert(InspectorLayout::button(20u) + InspectorLayout::buttonHeight < InspectorLayout::statusTop);

} // namespace s3g::sample_neon_gui
