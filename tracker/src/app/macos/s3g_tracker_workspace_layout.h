#pragma once

#include <cstddef>

namespace s3g::tracker::app {

// These values are deliberately independent of AppKit and the tracker model so
// the responsive workspace contract can be regression-tested as ordinary C++.
// AppKit consumes the resulting values as points.
inline constexpr double kWorkspaceMinimumContentWidth = 760.0;
inline constexpr double kWorkspaceMinimumContentHeight = 560.0;
inline constexpr double kWorkspaceToolbarHeight = 92.0;
inline constexpr double kWorkspaceConsoleInputHeight = 44.0;
inline constexpr double kTrackerRowNumberWidth = 34.0;
inline constexpr double kTrackerLanePreferredWidth = 168.0;
inline constexpr double kTrackerLaneGutter = 7.0;

struct WorkspaceLayoutMetrics {
    double toolboxWidth = 252.0;
    double envelopeHeight = 140.0;
    double consoleOutputHeight = 196.0;
    double devicePanelHeight = 104.0;
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
    const double width = contentWidth > 0.0 ? contentWidth : 0.0;
    const double height = contentHeight > 0.0 ? contentHeight : 0.0;
    return {
        workspaceClamp(width * 0.22, 190.0, 252.0),
        workspaceClamp(height * 0.18, 92.0, 140.0),
        workspaceClamp(height * 0.24, 128.0, 196.0),
        workspaceClamp(height * 0.15, 84.0, 104.0),
    };
}

constexpr double trackerDocumentWidth(std::size_t trackCount,
    double viewportWidth) noexcept
{
    const double viewport = viewportWidth > 0.0 ? viewportWidth : 0.0;
    const double gutters = trackCount > 0u
        ? static_cast<double>(trackCount - 1u) * kTrackerLaneGutter : 0.0;
    const double content = kTrackerRowNumberWidth
        + static_cast<double>(trackCount) * kTrackerLanePreferredWidth
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
