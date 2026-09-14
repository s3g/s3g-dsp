#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace s3g::tracker {

enum class TrackerFollowMode : uint8_t { Static, Center, Page };

// Presentation only. A lane is an ordinal within the displayed pattern;
// reordering lanes remaps the pinned ordinal with the musical lane.
struct TrackerFollowSettings {
    TrackerFollowMode mode = TrackerFollowMode::Static;
    bool selectedLane = false;
    uint32_t lane = 0u;
    bool operator==(const TrackerFollowSettings& other) const noexcept
    {
        return mode == other.mode && selectedLane == other.selectedLane && lane == other.lane;
    }
    bool operator!=(const TrackerFollowSettings& other) const noexcept { return !(*this == other); }
};

// Pure viewport geometry: no scheduler, wall clock, interpolation or MIDI.
// Non-static modes pin the existing 86-unit header and permit blank padding
// at pattern boundaries. PAGE uses complete power-of-two groups up to 16.
struct TrackerFollowLayout {
    TrackerFollowMode mode;
    std::size_t rows, pageRows = 16;
    double bodyHeight, minimum = 0, maximum = 0, centerOffset = 0;

    TrackerFollowLayout(TrackerFollowMode value, std::size_t rowCount, double viewportHeight,
        double scrollbarHeight = 10)
        : mode(value)
        , rows(std::max<std::size_t>(1, rowCount))
        , bodyHeight(std::max(25., viewportHeight - 86. - scrollbarHeight))
    {
        const auto visible = std::max(1., std::floor(bodyHeight / 25.));
        while (double(pageRows) > visible)
            pageRows /= 2;
        if (mode == TrackerFollowMode::Center) {
            centerOffset = (bodyHeight - 25.) / 2.;
            minimum = -centerOffset;
            maximum = double(rows - 1) * 25. - centerOffset;
        } else if (mode == TrackerFollowMode::Page)
            maximum = double((rows - 1) / pageRows * pageRows) * 25.;
        else
            maximum = std::max(0., 86. + double(rows) * 25. - viewportHeight);
    }
    double scrollForRow(std::size_t row) const noexcept
    {
        row = std::min(row, rows - 1);
        if (mode == TrackerFollowMode::Center)
            return double(row) * 25. - centerOffset;
        if (mode == TrackerFollowMode::Page)
            return double(row / pageRows * pageRows) * 25.;
        return 0.;
    }
};

} // namespace s3g::tracker
