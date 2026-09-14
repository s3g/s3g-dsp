#include "s3g/tracker/editor_song.h"
#include <cmath>
#include <iostream>
#include <set>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;
int main()
{
    int failures = 0, changes = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << message << '\n';
        }
    };
    SongEditor e;
    e.callbacks.changed = [&] { ++changes; };
    SongArrangement source;
    SongRow row;
    row.id = 91;
    row.patternId = "A01";
    row.durationTicks = 19;
    row.repeats = 70;
    row.energy = .37f;
    row.tempoMultiplier = 1.3;
    row.swing = .61;
    row.mutedTracks = 0x80010001;
    row.patternLoop = SongPatternLoop { 3, 20 };
    row.timingWarpLibraryIndex = 63;
    source.rows.push_back(row);
    e.setPatterns({ { "A01", "FIRST", 32, 32 }, { "A02", "SHORT", 8, 3 } }, "A02");
    e.setArrangement(source);
    check(changes == 0 && e.snapshot().rows[0].id == 91,
        "restore must not publish or change stable ID");
    for (auto field : { SongField::Warp, SongField::Repeats, SongField::Ticks, SongField::Tempo,
             SongField::Energy }) {
        auto choices = e.choices(0, field);
        check(choices.back().selected, "saved custom value missing from menu");
    }
    check(e.span(0) == 17 && e.spanSummary(0) == "19/17 LOOP",
        "loop span is independent of row ticks");
    e.playing = true;
    e.playbackEnabled = true;
    e.selected = 0;
    check(e.add() && e.arrangement.rows.back().patternId == "A02",
        "active pattern add while playing");
    auto added = e.arrangement.rows.back();
    check(added.durationTicks == 19 && added.repeats == 1 && !added.patternLoop
            && added.mutedTracks == 0 && added.swing == row.swing
            && added.timingWarpLibraryIndex == row.timingWarpLibraryIndex,
        "add inheritance");
    e.selected = 0;
    check(e.duplicate(), "duplicate");
    check(e.arrangement.rows[1].id != 91 && e.arrangement.rows[1].mutedTracks == row.mutedTracks,
        "duplicate identity/content");
    auto copied = e.arrangement.rows[1].id;
    e.move(1);
    check(e.arrangement.rows[2].id == copied, "move preserves identity");
    e.drop(2, 0, false);
    check(e.arrangement.rows[0].id == copied, "gutter move insertion semantics");
    e.drop(0, 3, true);
    check(e.arrangement.rows.back().id != copied, "Option-drag copy identity");
    check(e.toggleMute(0, 31), "lane 32 mute must be available");
    check(!e.toggleMute(2, 3), "unavailable lanes cannot be muted");
    int before = changes;
    e.remapMute(0, 31, "A01");
    check(changes == before, "lane remap is coordinator synchronization, not another publication");
    auto pattern = e.choices(0, SongField::Pattern)[1];
    e.choose(0, SongField::Pattern, pattern);
    auto changed = e.arrangement.rows[0];
    check(changed.patternLoop->endRow == 8 && changed.durationTicks == 19
            && (changed.mutedTracks & ~7u) == 0,
        "pattern change clamps loop/mutes only");
    e.swing(0, 62.3);
    e.swing(0, {});
    check(!e.arrangement.rows[0].swing && std::abs(e.swingPercent(0) - 62.3) < 1e-8,
        "base reset retains last numeric swing");
    e.swing(0, 100);
    check(e.arrangement.rows[0].swing == .75, "swing range clamp");
    e.selected = 1;
    int launches = 0;
    e.callbacks.launch = [&](std::size_t r, SongLaunchQuantization q) {
        ++launches;
        check(r == 1 && q == SongLaunchQuantization::NextSongRow, "queued boundary/selection");
    };
    e.queue();
    check(launches == 1 && !e.pendingRow,
        "queue delegates scheduling and pending marker to coordinator");
    e.playing = false;
    e.queue();
    check(launches == 1, "stopped transport cannot queue");
    int modes = 0, loops = 0;
    before = changes;
    e.callbacks.modeChanged = [&](bool enabled) {
        ++modes;
        check(enabled == e.playbackEnabled, "mode callback value");
    };
    e.callbacks.loopChanged = [&](bool enabled) {
        ++loops;
        check(enabled == e.arrangement.loop, "loop callback value");
    };
    e.toggleMode();
    e.toggleLoop();
    check(modes == 1 && loops == 1 && changes == before,
        "mode/loop use their coordinator callbacks, not duplicate edit publication");
    e.callbacks.loopChanged = {};
    e.toggleLoop();
    check(changes == before + 1, "loop edit fallback");
    e.pendingRow = 2;
    e.pendingQuantization = SongLaunchQuantization::NextBeat;
    check(e.queueStatus() == "QUEUED ROW 03 · NEXT BEAT", "pending marker comes from snapshot");
    source.rows[0].patternId = "MISSING";
    e.setArrangement(source);
    check(e.choices(0, SongField::Pattern).back().selected && e.snapshot().rows[0].mutedTracks == 0,
        "missing pattern reference/mutes");
    source.rows[0].id = UINT32_MAX;
    e.setArrangement(source);
    e.add();
    e.duplicate();
    std::set<uint32_t> identities;
    for (auto& r : e.arrangement.rows)
        check(r.id && identities.insert(r.id).second, "ID wraparound collision");
    e.setArrangement({});
    check(e.selected == -1 && e.snapshot().rows.empty(), "empty arrangement remains empty");
    e.add();
    check(e.arrangement.rows.size() == 1 && e.selected == 0, "add to empty arrangement");
    e.erase(0);
    check(e.selected == -1 && e.summary() == "0 ROWS · EMPTY ARRANGEMENT", "delete last row");
    source.rows.assign(kMaximumSongRows, row);
    e.setArrangement(source);
    check(!e.add() && !e.duplicate(), "maximum row count");
    return failures ? 1 : 0;
}
