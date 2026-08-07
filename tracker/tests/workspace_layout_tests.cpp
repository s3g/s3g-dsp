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
    check(near(compact.toolboxWidth, 190.0),
        "compact workspace should narrow the right toolbox");
    check(near(compact.envelopeHeight, 100.8),
        "compact workspace should preserve a useful envelope editor");
    check(near(compact.consoleOutputHeight, 134.4),
        "compact workspace should shrink console output without hiding it");
    check(near(compact.devicePanelHeight, 84.0),
        "compact workspace should use the short device panel");

    const double compactTrackerHeight = kWorkspaceMinimumContentHeight
        - kWorkspaceToolbarHeight - kWorkspaceConsoleInputHeight
        - compact.envelopeHeight - compact.devicePanelHeight - 3.0;
    const double compactToolboxHeight = kWorkspaceMinimumContentHeight
        - kWorkspaceToolbarHeight - kWorkspaceConsoleInputHeight
        - compact.consoleOutputHeight - compact.devicePanelHeight - 3.0;
    check(compactTrackerHeight >= 230.0,
        "minimum window should leave several tracker rows visible");
    check(compactToolboxHeight >= 200.0,
        "minimum window should leave the instrument toolbox usable");

    const auto spacious = workspaceLayoutMetrics(1600.0, 1000.0);
    check(near(spacious.toolboxWidth, 252.0)
            && near(spacious.envelopeHeight, 140.0)
            && near(spacious.consoleOutputHeight, 196.0)
            && near(spacious.devicePanelHeight, 104.0),
        "large workspace should cap auxiliary panels at designed sizes");

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
