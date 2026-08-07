#include "s3g_tracker_workspace_layout.h"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool near(double actual, double expected) noexcept
{
    return std::abs(actual - expected) < 0.001;
}

} // namespace

int main()
{
    using namespace s3g::tracker::app;

    const auto compact = workspaceLayoutMetrics(
        kWorkspaceMinimumContentWidth, kWorkspaceMinimumContentHeight);
    check(near(compact.envelopeHeight, 100.8),
        "compact workspace should preserve a useful envelope editor");

    const double compactTrackerHeight = kWorkspaceMinimumContentHeight
        - kWorkspaceToolbarHeight - kWorkspaceConsoleInputHeight
        - compact.envelopeHeight - 2.0;
    check(compactTrackerHeight >= 320.0,
        "minimum window should use removed device space for tracker rows");

    const auto spacious = workspaceLayoutMetrics(1600.0, 1000.0);
    check(near(spacious.envelopeHeight, 140.0),
        "large workspace should cap the envelope at its designed size");

    check(near(trackerDocumentWidth(1u, 500.0), 500.0),
        "one track should expand to fill a wide viewport");
    check(near(trackerDocumentWidth(4u, 500.0), 727.0),
        "track lanes should retain readable intrinsic widths");
    check(near(trackerDocumentWidth(12u, 500.0), 2127.0),
        "track count should grow the scroll document, not the window");
    check(near(trackerDocumentWidth(0u, 300.0), 300.0),
        "empty tracker should still fill its viewport");

    check(near(scrollingStripDocumentWidth(1080.0, 600.0), 1080.0),
        "transport controls should become horizontally scrollable");
    check(near(scrollingStripDocumentWidth(540.0, 900.0), 900.0),
        "transport strip should fill spare toolbar width");

    if (failures == 0) {
        std::cout << "workspace layout tests passed\n";
        return 0;
    }
    std::cerr << failures << " workspace layout assertion(s) failed\n";
    return 1;
}
