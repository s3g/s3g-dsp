#include "s3g_tracker_grid_selection.h"
#include "s3g_tracker_grid_input.h"

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

} // namespace

int main()
{
    using namespace s3g::tracker::app;

    GridSelection selection;
    selection.active = true;
    selection.page = 1u;
    selection.anchorTrack = 3u;
    selection.anchorField = 1u;
    selection.anchorRow = 12u;
    selection.focusTrack = 1u;
    selection.focusField = 0u;
    selection.focusRow = 8u;
    const auto range = selection.range();
    check(range.page == 1u && range.firstTrack == 1u
            && range.lastTrack == 3u && range.firstField == 0u
            && range.lastField == 1u && range.firstRow == 8u
            && range.lastRow == 12u,
        "selection range should normalize a reverse drag");
    check(range.trackCount() == 3u && range.fieldCount() == 2u
            && range.rowCount() == 5u,
        "selection dimensions should be inclusive");
    check(range.contains(1u, 2u, 1u, 10u)
            && !range.contains(0u, 2u, 1u, 10u)
            && !range.contains(1u, 4u, 1u, 10u),
        "selection containment should include page and every dimension");
    check(!selection.isSingleCell(),
        "multi-dimensional drag must not report one cell");

    std::size_t track = 0u;
    std::size_t field = 0u;
    gridAddressForClipboardColumn(gridClipboardColumn(7u, 2u, 3u),
        3u, track, field);
    check(track == 7u && field == 2u,
        "clipboard columns should round trip track-major addresses");

    GridSelection single;
    single.anchorTrack = single.focusTrack = 2u;
    single.anchorField = single.focusField = 1u;
    single.anchorRow = single.focusRow = 4u;
    check(single.isSingleCell(), "equal endpoints should be one cell");

    std::size_t instrument = 99u;
    check(parseGridInstrumentIndex("00", 12u, instrument)
            && instrument == 0u
            && parseGridInstrumentIndex("11", 12u, instrument)
            && instrument == 11u,
        "INS parser should use buffered zero-based decimal indices");
    check(!parseGridInstrumentIndex("12", 12u, instrument)
            && !parseGridInstrumentIndex("-1", 12u, instrument)
            && !parseGridInstrumentIndex("", 12u, instrument),
        "INS parser should reject unavailable or non-decimal indices");

    float normalized = -1.0f;
    check(parseGridNormalizedValue("0.000", normalized)
            && normalized == 0.0f
            && parseGridNormalizedValue(".625", normalized)
            && normalized > 0.624f && normalized < 0.626f
            && parseGridNormalizedValue("1", normalized)
            && normalized == 1.0f,
        "VOL parser should accept normalized decimal endpoints and fractions");
    check(!parseGridNormalizedValue("127", normalized)
            && !parseGridNormalizedValue("1.001", normalized)
            && !parseGridNormalizedValue("0.5x", normalized)
            && !parseGridNormalizedValue(".", normalized),
        "VOL parser should reject MIDI integers and malformed values");
    check(parseGridMidiOrNormalizedValue("64", normalized)
            && std::abs(normalized - 64.0f / 127.0f) < 0.0001f
            && parseGridMidiOrNormalizedValue("1.0", normalized)
            && normalized == 1.0f
            && parseGridMidiOrNormalizedValue("0", normalized)
            && normalized == 0.0f
            && !parseGridMidiOrNormalizedValue("128", normalized),
        "CC value parser should accept 7-bit integers and explicit normalized decimals");

    check(std::abs(normalizedValueFromVerticalDrag(
                0.5f, 20.0, false, false) - 0.6f) < 0.0001f
            && normalizedValueFromVerticalDrag(
                0.05f, -100.0, false, false) == 0.0f
            && std::abs(normalizedValueFromVerticalDrag(
                0.5f, 20.0, true, false) - 0.51f) < 0.0001f
            && std::abs(normalizedValueFromVerticalDrag(
                0.5f, 20.0, false, true) - 0.9f) < 0.0001f,
        "numeric cell drag should increase upward, clamp, and honor fine/coarse modifiers");

    if (failures == 0) {
        std::cout << "grid selection tests passed\n";
        return 0;
    }
    return 1;
}
