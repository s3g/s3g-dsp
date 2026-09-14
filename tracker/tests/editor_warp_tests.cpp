#include "s3g/tracker/editor_warp.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace s3g::tracker;
using namespace s3g::tracker::editor;
int main()
{
    int failures = 0, publications = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    app::TrackerViewState state;
    WarpEditor editor(state, [&] { ++publications; });
    check(editor.slots().size() == 64 && editor.transforms().front() == "NO TRANSFORMS",
        "empty library/stack");
    check(!editor.field(WarpField::Primary).enabled && !editor.field(WarpField::Pulses).visible,
        "empty transform controls");
    editor.add(TimingWarpKind::Exponential);
    editor.set(WarpField::Primary, 1.75);
    editor.set(WarpField::Cycle, 7);
    editor.set(WarpField::Mix, .37);
    editor.set(WarpField::Begin, .2);
    editor.set(WarpField::End, .8);
    editor.set(WarpField::Repeats, 3);
    editor.selectSlot(63);
    check(editor.save("\xc2\xa0 éCHO \xe3\x80\x80"), "UTF-8 save");
    check(editor.name() == "éCHO", "Unicode name trimming");
    const auto saved = *state.session.warpLibrary.entry(63);
    editor.clear();
    editor.set(WarpField::Cycle, 2);
    bool mode = state.session.transport.timingWarpEnabled;
    auto before = publications;
    editor.selectSlot(62);
    check(publications == before && state.session.transport.timingWarp.empty(),
        "empty slot must not replace stack/publish");
    editor.selectSlot(63);
    check(state.session.transport.warpCycleTicks == 7
            && editor.field(WarpField::Primary).value == 1.75
            && mode == state.session.transport.timingWarpEnabled,
        "recall stack/cycle without changing bypass");
    editor.erase();
    check(!state.session.warpLibrary.entry(63) && state.session.transport.timingWarp.size() == 1,
        "delete must preserve editing stack");
    editor.setKind(TimingWarpKind::EuclideanQuantize);
    check(editor.field(WarpField::Primary).value == 8 && editor.field(WarpField::Pulses).value == 3
            && editor.field(WarpField::Mix).value == .37
            && editor.field(WarpField::Begin).value == .2
            && editor.field(WarpField::End).value == .8
            && editor.field(WarpField::Repeats).value == 3,
        "type change defaults and option preservation");
    before = publications;
    check(!editor.set(WarpField::Primary, 2) && !editor.set(WarpField::Pulses, 9)
            && !editor.set(WarpField::Mix, 1.1) && !editor.set(WarpField::Begin, .8)
            && !editor.set(WarpField::End, .2) && !editor.set(WarpField::Repeats, 17)
            && !editor.set(WarpField::Cycle, 0) && !editor.set(WarpField::Cycle, 4.5)
            && !editor.set(WarpField::Primary, std::numeric_limits<double>::infinity())
            && !editor.save(std::string(65, 'X')) && before == publications,
        "invalid edits must be atomic and not publish");
    editor.clear();
    for (std::size_t i = 0; i < 32; ++i)
        check(editor.add(static_cast<TimingWarpKind>(i % 3)), "stack capacity add");
    check(!editor.add(TimingWarpKind::Exponential) && editor.selected == 31, "stack overflow");
    check(
        editor.remove() && editor.selected == 30 && state.session.transport.timingWarp.size() == 31,
        "remove selection");
    editor.clear();
    editor.selected = 99;
    editor.slot = 99;
    editor.reconcile();
    check(editor.selected == 0 && editor.slot == 63, "external model reconciliation");
    check(editor.save("") && editor.name() == "WARP 64", "default name");
    state.session.transport.timingWarp = saved.stack;
    state.session.transport.warpCycleTicks = 7;
    state.session.transport.timingWarpEnabled = false;
    state.playing = state.timingWarpPlaybackActive = state.timingWarpPlaybackFromSong = true;
    state.timingWarpPlaybackStack.clear();
    state.timingWarpPlaybackStack.append(TimingWarpTransform::exponential(2));
    state.timingWarpPlaybackCycleTicks = 8;
    state.timingWarpPlaybackTick = 3;
    GridFont font { "Menlo", 8.5, 8, 11 };
    auto list = paintWarpCurve(state, { 0, 0, 500, 400 }, font);
    bool dash = false, marker = false, song = false, curve = false, progress = false;
    for (const auto& cmd : list.commands()) {
        dash |= cmd.dashes == std::vector<double>({ 4, 4 });
        marker |= cmd.primitive == Primitive::StrokeEllipse && cmd.lineWidth == 1.5;
        song |= cmd.text == "SONG WARP · PLAYING";
        progress |= cmd.text == "STEP 04 / 08";
        if (cmd.primitive == Primitive::Polyline && cmd.points.size() == 257) {
            auto p = cmd.points[128];
            curve = std::abs(p.x - 250) < 1e-9
                && std::abs(p.y - (378 - 356 * state.timingWarpPlaybackStack.map(.5))) < 1e-9;
        }
    }
    check(dash && marker && song && curve && progress,
        "Song graph must use sounding stack, cycle, dash, marker and progress");
    check(editor.playbackDescription() == "Song warp playback, step 4 of 8",
        "playback accessibility");
    state.playing = false;
    list = paintWarpCurve(state, { 0, 0, 500, 400 }, font);
    marker = false;
    bool bypass = false;
    for (const auto& cmd : list.commands()) {
        marker |= cmd.primitive == Primitive::StrokeEllipse;
        bypass |= cmd.text == "OUTPUT · BYPASSED";
    }
    check(!marker && bypass && state.timingWarpPlaybackTick == 3,
        "drawing must never advance playback");
    return failures ? 1 : 0;
}
