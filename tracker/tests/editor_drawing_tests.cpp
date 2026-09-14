#include "s3g/tracker/editor_drawing.h"
#include "s3g/tracker/editor_state.h"

#include <iostream>
#include <cmath>

int main()
{
    using namespace s3g::tracker::editor;
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << message << '\n'; }
    };
    // This TU is ordinary C++: the shared editor contract must not acquire
    // Cocoa types just because its initial consumer remains the Mac editor.
    s3g::tracker::app::TrackerViewState state;
    s3g::tracker::app::WorkspaceCallbacks callbacks;
    callbacks.selectionChanged = [&] { state.session.selectedRow = 7u; };
    callbacks.selectionChanged();
    check(state.session.selectedRow == 7u, "portable editor callbacks changed");

    DisplayList list;
    const Color gray {186, 186, 186, 204};
    list.shape(Primitive::FillRect, {1.25, 2.5, 30.0, 40.0}, gray);
    list.shape(Primitive::StrokeRect, {0.5, 0.5, 99.0, 29.0}, gray, 1.2);
    list.polyline({{1.0, 2.0}, {3.0, 4.0}, {5.0, 1.0}}, gray, 1.2);
    list.shape(Primitive::FillEllipse, {10.0, 20.0, 7.0, 7.0}, gray);
    list.text("C♯4 → 127", {4.0, 8.0, 70.0, 16.0}, gray,
        "IBMPlexMono-Medium", 11.5, 21.25, Alignment::Center);
    const auto& commands = list.commands();
    check(commands.size() == 5u, "drawing order/count changed");
    check(commands[0].rect.x == 1.25 && commands[0].color.alpha == 204,
        "fractional coordinates or transparency lost");
    check(commands[1].lineWidth == 1.2 && commands[1].rect.width == 99.0,
        "stroke geometry changed");
    check(commands[2].points.size() == 3u && commands[2].points.back().y == 1.0,
        "envelope curve points lost");
    const auto& text = commands[4];
    check(text.text == "C♯4 → 127" && text.fontName == "IBMPlexMono-Medium"
            && text.fontSize == 11.5 && text.baseline == 21.25
            && text.alignment == Alignment::Center,
        "text/font/baseline/alignment contract changed");
    list.polyline({}, gray, 1.0);
    list.polyline({{0, 0}}, gray, 1.0);
    list.text("", {}, gray, "", 12, 0, Alignment::Left);
    check(commands.size() == 5u, "empty primitives should not be submitted");
    check(alignedTextX({2, 0, 12, 10}, 14.8, Alignment::Right) == 2.0
            && alignedTextX({2, 0, 12, 10}, 14.8, Alignment::Center) == 2.0,
        "over-wide gutter text should clip from its start");
    check(alignedTextX({2, 0, 20, 10}, 10, Alignment::Right) == 12.0
            && alignedTextX({2, 0, 20, 10}, 10, Alignment::Center) == 7.0,
        "fitting text alignment changed");
    for (double scale : {.65, 1., 1.5, 2.}) {
        Rect button {0, 7 * scale, 70 * scale, 15 * scale};
        double cap = 7.2900390625 * scale;
        double baseline = centeredCapsBaseline(button, cap);
        check(std::abs((baseline - cap - button.y)
            - (button.y + button.height - baseline)) < 1e-9,
            "button capitals must have equal top and bottom padding at every scale");
    }
    return failures ? 1 : 0;
}
