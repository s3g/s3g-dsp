#pragma once

#include <cstddef>

namespace s3g::tracker::app {

// These values are deliberately independent of AppKit and the tracker model so
// the responsive workspace contract can be regression-tested as ordinary C++.
// AppKit consumes the resulting values as points.
inline constexpr double kWorkspaceMinimumContentWidth = 760.0;
inline constexpr double kWorkspaceMinimumContentHeight = 620.0;
// One standard toolbox header, one compact control row, and one status line.
// The former two-row header left dead vertical space above the tracker grid.
inline constexpr double kWorkspaceToolbarHeight = 69.0;
inline constexpr double kWorkspaceConsoleInputHeight = 44.0;
inline constexpr double kWorkspaceTransportFooterHeight = 57.0;
inline constexpr double kTrackerGridHeaderHeight = 86.0;
inline constexpr double kTrackerGridRowHeight = 25.0;
// The native CLAP page loses 40 points to its page selector. A slightly
// denser logical 100% keeps the first 16 tracker rows fully visible without
// increasing the host window or changing the authored row geometry.
inline constexpr double kTrackerDefaultMagnification = 0.95;
inline constexpr double kTrackerRowNumberWidth = 34.0;
inline constexpr double kTrackerLaneInnerPadding = 3.0;
inline constexpr double kTrackerLaneExpandedWidth = 360.0;
inline constexpr double kTrackerExpandedNoteFraction = 0.19;
inline constexpr double kTrackerExpandedVolumeFraction = 0.15;
// Collapsing removes the SEQ fields without stretching NOTE or VOL. The
// compact field area is exactly their combined width in an expanded lane.
inline constexpr double kTrackerLaneCompactWidth =
    2.0 * kTrackerLaneInnerPadding
    + (kTrackerLaneExpandedWidth - 2.0 * kTrackerLaneInnerPadding)
        * (kTrackerExpandedNoteFraction + kTrackerExpandedVolumeFraction);
inline constexpr double kTrackerLaneGutter = 7.0;

struct WorkspaceLayoutMetrics {
    double envelopeHeight = 140.0;
};

constexpr double workspaceClamp(double value, double minimum,
    double maximum) noexcept
{
    return value < minimum ? minimum
        : value > maximum ? maximum : value;
}

constexpr WorkspaceLayoutMetrics workspaceLayoutMetrics(
    double contentWidth, double contentHeight) noexcept
{
    (void)contentWidth;
    const double height = contentHeight > 0.0 ? contentHeight : 0.0;
    return {
        workspaceClamp(height * 0.18, 92.0, 140.0),
    };
}

constexpr double trackerDocumentWidth(std::size_t trackCount,
    double viewportWidth, bool sequenceColumnsExpanded) noexcept
{
    const double viewport = viewportWidth > 0.0 ? viewportWidth : 0.0;
    const double gutters = trackCount > 0u
        ? static_cast<double>(trackCount - 1u) * kTrackerLaneGutter : 0.0;
    const double laneWidth = sequenceColumnsExpanded
        ? kTrackerLaneExpandedWidth : kTrackerLaneCompactWidth;
    const double content = kTrackerRowNumberWidth
        + static_cast<double>(trackCount) * laneWidth
        + gutters;
    return content > viewport ? content : viewport;
}

constexpr double scrollingStripDocumentWidth(double fittingWidth,
    double viewportWidth) noexcept
{
    const double fitting = fittingWidth > 0.0 ? fittingWidth : 0.0;
    const double viewport = viewportWidth > 0.0 ? viewportWidth : 0.0;
    return fitting > viewport ? fitting : viewport;
}

} // namespace s3g::tracker::app
