#include "s3g/tracker/editor_song.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>

namespace s3g::tracker::editor {
namespace {
    template <class... A> std::string fmt(const char* pattern, A... args)
    {
        int count = std::snprintf(nullptr, 0, pattern, args...);
        std::vector<char> buffer(static_cast<std::size_t>(std::max(0, count)) + 1);
        std::snprintf(buffer.data(), buffer.size(), pattern, args...);
        return buffer.data();
    }
    constexpr std::array<uint32_t, 16> ticks { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128,
        192, 256 };
    constexpr std::array<double, 7> tempos { .25, .5, 2. / 3, 1, 1.5, 2, 4 };
    constexpr const char* tempoNames[] = { "1/4×", "1/2×", "2/3×", "1×", "3/2×", "2×", "4×" };
    constexpr const char* quantNames[] = { "NEXT TICK", "NEXT BEAT", "END OF PASS", "END OF ROW" };
    uint32_t mask(uint32_t lanes)
    {
        return lanes >= 32 ? ~uint32_t(0) : (uint32_t(1) << lanes) - 1;
    }
}
SongEditor::SongEditor()
{
    arrangement.name = "SONG";
    SongRow row;
    row.patternId = "A01";
    row.durationTicks = 4;
    row.swing = .56;
    arrangement.rows.push_back(row);
    baseSwing_.push_back(56);
}
void SongEditor::publish()
{
    if (callbacks.changed)
        callbacks.changed();
}
uint32_t SongEditor::identity()
{
    // Preserve stable identities on reorder; newly inserted/copied rows must
    // not collide with a loaded identity, including after uint32 wraparound.
    for (;;) {
        uint32_t value = nextIdentity_++;
        if (!nextIdentity_)
            nextIdentity_ = 1;
        if (value
            && std::none_of(arrangement.rows.begin(), arrangement.rows.end(),
                [value](const auto& r) { return r.id == value; }))
            return value;
    }
}
void SongEditor::setArrangement(const SongArrangement& source)
{
    arrangement = source;
    arrangement.ticksPerBeat = source.ticksPerBeat ? source.ticksPerBeat : 4;
    nextIdentity_ = 1;
    baseSwing_.clear();
    for (auto& r : arrangement.rows) {
        if (!r.id)
            r.id = identity();
        if (r.id != UINT32_MAX)
            nextIdentity_ = std::max(nextIdentity_, r.id + 1);
        // Match the native authoring model's percent precision and legacy BPM
        // boundary. Loading is synchronization, never a user publication.
        r.energy = static_cast<float>(std::lround(std::clamp(r.energy, 0.f, 1.f) * 100.f)) * .01f;
        r.tempoMultiplier = std::clamp(r.tempoMultiplier, .25, 4.);
        r.bpm.reset();
        baseSwing_.push_back(r.swing.value_or(.56) * 100);
        removeUnavailableMutes(r);
    }
    selected = arrangement.rows.empty() ? -1 : 0;
    loadedLoopTitle = true;
}
SongArrangement SongEditor::snapshot() const
{
    auto result = arrangement;
    result.ticksPerBeat = std::clamp(result.ticksPerBeat, 1u, 96u);
    for (auto& r : result.rows) {
        r.durationTicks = std::clamp(r.durationTicks, 1u, kMaximumSongDurationTicks);
        r.repeats = std::clamp(r.repeats, 1u, kMaximumSongRepeats);
        r.energy = static_cast<float>(std::lround(std::clamp(r.energy, 0.f, 1.f) * 100.f)) * .01f;
        r.tempoMultiplier = std::clamp(r.tempoMultiplier, .25, 4.);
        if (r.swing)
            r.swing = std::clamp(*r.swing, .5, .75);
        r.mutedTracks &= mask(laneCount(r.patternId));
        r.bpm.reset();
    }
    return result;
}
void SongEditor::setPatterns(std::vector<SongPatternInfo> source, std::string active)
{
    patterns.clear();
    for (auto p : source)
        if (!p.id.empty() && std::none_of(patterns.begin(), patterns.end(), [&](const auto& a) {
                return a.id == p.id;
            })) {
            p.length = std::clamp(p.length, 1u, kMaximumSongPatternRows);
            p.lanes = std::min(p.lanes, 32u);
            patterns.push_back(std::move(p));
        }
    if (patterns.empty())
        patterns.push_back({ "A01", "", 16, 32 });
    activePattern = std::any_of(patterns.begin(), patterns.end(),
                        [&](const auto& p) { return p.id == active; })
        ? active
        : patterns.front().id;
    for (auto& row : arrangement.rows)
        removeUnavailableMutes(row);
}
uint32_t SongEditor::patternLength(const std::string& id) const
{
    for (const auto& p : patterns)
        if (p.id == id)
            return p.length;
    return 16;
}
uint32_t SongEditor::laneCount(const std::string& id) const
{
    for (const auto& p : patterns)
        if (p.id == id)
            return p.lanes;
    return 0;
}
void SongEditor::removeUnavailableMutes(SongRow& row)
{
    for (const auto& p : patterns)
        if (p.id == row.patternId) {
            row.mutedTracks &= mask(p.lanes);
            break;
        }
}
uint32_t SongEditor::span(std::size_t i) const
{
    const auto& r = arrangement.rows.at(i);
    return r.patternLoop ? (r.patternLoop->endRow > r.patternLoop->startRow
                   ? r.patternLoop->endRow - r.patternLoop->startRow
                   : 1u)
                         : patternLength(r.patternId);
}
std::string SongEditor::spanSummary(std::size_t i) const
{
    return fmt("%u/%u %s", arrangement.rows.at(i).durationTicks, span(i),
        arrangement.rows.at(i).patternLoop ? "LOOP" : "PAT");
}
std::string SongEditor::summary() const
{
    if (arrangement.rows.empty())
        return "0 ROWS · EMPTY ARRANGEMENT";
    std::set<std::string> ids;
    uint64_t passes = 0;
    for (const auto& r : arrangement.rows) {
        ids.insert(r.patternId);
        passes += r.repeats;
    }
    return fmt("%zu ROW%s · %zu PATTERN%s · %llu PASS%s · HOST TEMPO", arrangement.rows.size(),
        arrangement.rows.size() == 1 ? "" : "S", ids.size(), ids.size() == 1 ? "" : "S",
        static_cast<unsigned long long>(passes), passes == 1 ? "" : "ES");
}
std::string SongEditor::queueStatus() const
{
    return pendingRow ? fmt("QUEUED ROW %02zu · %s", *pendingRow + 1,
               quantNames[std::min(3, int(pendingQuantization))])
                      : "QUEUE —";
}
std::vector<SongChoice> SongEditor::choices(std::size_t row, SongField field) const
{
    if (row >= arrangement.rows.size())
        return {};
    const auto& r = arrangement.rows[row];
    std::vector<SongChoice> result;
    double current = 0;
    auto add = [&](std::string title, double value) {
        result.push_back({ std::move(title), "", value, false });
    };
    switch (field) {
    case SongField::Pattern: {
        for (const auto& p : patterns)
            result.push_back(
                { p.name.empty() ? p.id : p.id + " · " + p.name, p.id, 0, p.id == r.patternId });
        if (std::none_of(result.begin(), result.end(), [](const auto& c) { return c.selected; }))
            result.push_back(
                { "MISSING · " + (r.patternId.empty() ? "—" : r.patternId), r.patternId, 0, true });
        return result;
    }
    case SongField::Warp:
        current = r.timingWarpLibraryIndex ? double(*r.timingWarpLibraryIndex + 1) : 0;
        add("OFF", 0);
        for (std::size_t i = 0; i < kMaximumTimingWarpLibraryEntries; ++i)
            if (const auto* e = warps.entry(i))
                add(fmt("%02zu · %s", i + 1, e->name.empty() ? "UNTITLED" : e->name.c_str()),
                    double(i + 1));
        break;
    case SongField::LoopIn:
        current = r.patternLoop ? r.patternLoop->startRow + 1 : 0;
        add("OFF", 0);
        for (uint32_t i = 1; i <= patternLength(r.patternId); ++i)
            add(fmt("IN %03u", i), i);
        break;
    case SongField::LoopOut:
        if (!r.patternLoop)
            return { { "—", "", 0, true } };
        current = r.patternLoop->endRow;
        for (uint32_t i = std::clamp(r.patternLoop->startRow + 1, 1u, patternLength(r.patternId));
             i <= patternLength(r.patternId); ++i)
            add(fmt("OUT %03u", i), i);
        break;
    case SongField::Repeats:
        current = r.repeats;
        for (int i = 1; i <= 64; ++i)
            add(std::to_string(i), i);
        break;
    case SongField::Ticks: {
        current = r.durationTicks;
        auto full = span(row);
        add(fmt("FULL · 1× · %u", full), full);
        for (auto t : ticks)
            if (t != full)
                add(t < full ? fmt("%u · %ld%%", t, std::lround(double(t) * 100 / full))
                             : fmt("%u · %.2g×", t, double(t) / full),
                    t);
        break;
    }
    case SongField::Tempo:
        current = r.tempoMultiplier;
        for (std::size_t i = 0; i < tempos.size(); ++i)
            add(tempoNames[i], tempos[i]);
        break;
    case SongField::Energy:
        current = std::lround(r.energy * 100.f);
        for (int i = 0; i <= 100; i += 5)
            add(fmt("%d%%", i), i);
        break;
    }
    bool found = false;
    for (auto& c : result) {
        c.selected = std::abs(c.value - current) < 1e-9;
        found |= c.selected;
    }
    if (!found) {
        std::string title = field == SongField::Warp ? fmt("%02u · MISSING", unsigned(current))
                                                     : field == SongField::LoopIn
                ? fmt("IN %03u · SAVED", unsigned(current))
                : field == SongField::LoopOut ? fmt("OUT %03u · SAVED", unsigned(current))
                                              : field == SongField::Tempo
                        ? fmt("%g× · SAVED", current)
                        : field == SongField::Energy ? fmt("%u%% · SAVED", unsigned(current))
                                                     : fmt("%u · SAVED", unsigned(current));
        result.push_back({ title, "", current, true });
    }
    return result;
}
bool SongEditor::choose(std::size_t i, SongField field, const SongChoice& choice)
{
    if (i >= arrangement.rows.size() || !std::isfinite(choice.value))
        return false;
    auto& r = arrangement.rows[i];
    auto value = choice.value;
    if (field == SongField::Pattern) {
        if (choice.pattern.empty() || choice.pattern == r.patternId)
            return false;
        r.patternId = choice.pattern;
        removeUnavailableMutes(r);
        if (r.patternLoop) {
            r.patternLoop->startRow
                = std::min(r.patternLoop->startRow, patternLength(r.patternId) - 1);
            r.patternLoop->endRow = std::clamp(
                r.patternLoop->endRow, r.patternLoop->startRow + 1, patternLength(r.patternId));
        }
    } else if (field == SongField::Tempo) {
        if (value < .25 || value > 4 || std::abs(value - r.tempoMultiplier) < 1e-9)
            return false;
        r.tempoMultiplier = value;
    } else {
        if (value < 0 || value > kMaximumSongDurationTicks || value != std::floor(value))
            return false;
        auto v = static_cast<uint32_t>(value);
        switch (field) {
        case SongField::Warp:
            if (v > 64 || (r.timingWarpLibraryIndex ? *r.timingWarpLibraryIndex + 1 : 0) == v)
                return false;
            r.timingWarpLibraryIndex = v ? std::optional<std::size_t>(v - 1) : std::nullopt;
            break;
        case SongField::LoopIn:
            if (v > patternLength(r.patternId))
                return false;
            if (!v) {
                if (!r.patternLoop)
                    return false;
                r.patternLoop.reset();
            } else {
                auto end = r.patternLoop
                    ? std::clamp(r.patternLoop->endRow, v, patternLength(r.patternId))
                    : patternLength(r.patternId);
                if (r.patternLoop && r.patternLoop->startRow == v - 1
                    && r.patternLoop->endRow == end)
                    return false;
                r.patternLoop = SongPatternLoop { v - 1, end };
            }
            break;
        case SongField::LoopOut:
            if (!r.patternLoop || v <= r.patternLoop->startRow || v > patternLength(r.patternId)
                || v == r.patternLoop->endRow)
                return false;
            r.patternLoop->endRow = v;
            break;
        case SongField::Repeats:
            if (v < 1 || v > 64 || v == r.repeats)
                return false;
            r.repeats = v;
            break;
        case SongField::Ticks:
            if (v == r.durationTicks
                || (v != span(i) && std::find(ticks.begin(), ticks.end(), v) == ticks.end()))
                return false;
            r.durationTicks = v;
            break;
        case SongField::Energy:
            if (v > 100 || std::lround(r.energy * 100.f) == v)
                return false;
            r.energy = float(v) * .01f;
            break;
        default:
            return false;
        }
    }
    publish();
    return true;
}
bool SongEditor::add()
{
    if (arrangement.rows.size() >= kMaximumSongRows)
        return false;
    auto insertion = selected >= 0 ? std::min(std::size_t(selected + 1), arrangement.rows.size())
                                   : arrangement.rows.size();
    SongRow r;
    r.id = identity();
    r.patternId = activePattern.empty() ? "A01" : activePattern;
    r.durationTicks = 4;
    r.swing = .56;
    double base = 56;
    if (insertion) {
        const auto& prior = arrangement.rows[insertion - 1];
        r.durationTicks = prior.durationTicks;
        r.tempoMultiplier = prior.tempoMultiplier;
        r.energy = prior.energy;
        r.swing = prior.swing;
        r.timingWarpLibraryIndex = prior.timingWarpLibraryIndex;
        base = baseSwing_[insertion - 1];
    }
    arrangement.rows.insert(arrangement.rows.begin() + static_cast<std::ptrdiff_t>(insertion), r);
    baseSwing_.insert(baseSwing_.begin() + static_cast<std::ptrdiff_t>(insertion), base);
    selected = int(insertion);
    publish();
    return true;
}
bool SongEditor::duplicate()
{
    return selected >= 0 && drop(std::size_t(selected), std::size_t(selected + 1), true);
}
bool SongEditor::erase(std::size_t row)
{
    if (row >= arrangement.rows.size())
        return false;
    arrangement.rows.erase(arrangement.rows.begin() + static_cast<std::ptrdiff_t>(row));
    baseSwing_.erase(baseSwing_.begin() + static_cast<std::ptrdiff_t>(row));
    selected = arrangement.rows.empty() ? -1 : int(std::min(row, arrangement.rows.size() - 1));
    publish();
    return true;
}
bool SongEditor::move(int offset)
{
    if ((offset != -1 && offset != 1) || selected < 0 || selected + offset < 0
        || std::size_t(selected + offset) >= arrangement.rows.size())
        return false;
    std::swap(
        arrangement.rows[std::size_t(selected)], arrangement.rows[std::size_t(selected + offset)]);
    std::swap(baseSwing_[std::size_t(selected)], baseSwing_[std::size_t(selected + offset)]);
    selected += offset;
    publish();
    return true;
}
bool SongEditor::drop(std::size_t source, std::size_t destination, bool copy)
{
    if (source >= arrangement.rows.size() || destination > arrangement.rows.size()
        || (copy && arrangement.rows.size() >= kMaximumSongRows))
        return false;
    auto r = arrangement.rows[source];
    auto base = baseSwing_[source];
    if (copy)
        r.id = identity();
    else {
        arrangement.rows.erase(arrangement.rows.begin() + static_cast<std::ptrdiff_t>(source));
        baseSwing_.erase(baseSwing_.begin() + static_cast<std::ptrdiff_t>(source));
        if (source < destination)
            --destination;
    }
    arrangement.rows.insert(arrangement.rows.begin() + static_cast<std::ptrdiff_t>(destination), r);
    baseSwing_.insert(baseSwing_.begin() + static_cast<std::ptrdiff_t>(destination), base);
    selected = int(destination);
    publish();
    return true;
}
bool SongEditor::toggleMute(std::size_t row, uint32_t lane)
{
    if (row >= arrangement.rows.size() || lane >= laneCount(arrangement.rows[row].patternId))
        return false;
    arrangement.rows[row].mutedTracks ^= uint32_t(1) << lane;
    publish();
    return true;
}
void SongEditor::remapMute(uint32_t source, uint32_t destination, const std::string& pattern)
{
    if (source == destination || source >= 32 || destination >= 32 || pattern.empty())
        return;
    for (auto& r : arrangement.rows)
        if (r.patternId == pattern) {
            uint32_t result = 0;
            for (uint32_t i = 0; i < 32; ++i)
                if (r.mutedTracks & (uint32_t(1) << i)) {
                    uint32_t j = i == source
                        ? destination
                        : source < destination && i > source && i <= destination
                            ? i - 1
                            : source > destination && i >= destination && i < source ? i + 1 : i;
                    result |= uint32_t(1) << j;
                }
            r.mutedTracks = result;
        }
}
double SongEditor::swingPercent(std::size_t row) const
{
    return std::clamp(
        arrangement.rows.at(row).swing ? *arrangement.rows[row].swing * 100 : baseSwing_.at(row),
        50., 75.);
}
bool SongEditor::swing(std::size_t row, std::optional<double> value)
{
    if (row >= arrangement.rows.size() || (value && !std::isfinite(*value)))
        return false;
    auto& r = arrangement.rows[row];
    if (!value) {
        if (!r.swing)
            return false;
        r.swing.reset();
    } else {
        double percent = std::round(std::clamp(*value, 50., 75.) * 10) / 10;
        if (r.swing && std::abs(*r.swing * 100 - percent) < 1e-9)
            return false;
        baseSwing_[row] = percent;
        r.swing = percent * .01;
    }
    publish();
    return true;
}
bool SongEditor::queueAvailable() const
{
    return playbackEnabled && playing && selected >= 0
        && std::size_t(selected) < arrangement.rows.size();
}
void SongEditor::queue()
{
    if (queueAvailable() && callbacks.launch)
        callbacks.launch(std::size_t(selected), quantization);
}
void SongEditor::toggleMode()
{
    playbackEnabled = !playbackEnabled;
    if (callbacks.modeChanged)
        callbacks.modeChanged(playbackEnabled);
}
void SongEditor::toggleLoop()
{
    arrangement.loop = !arrangement.loop;
    loadedLoopTitle = false;
    if (callbacks.loopChanged)
        callbacks.loopChanged(arrangement.loop);
    else
        publish();
}
}
