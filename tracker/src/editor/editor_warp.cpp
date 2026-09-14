#include "s3g/tracker/editor_warp.h"
#include <array>
#include <cmath>
#include <cstdio>

namespace s3g::tracker::editor {
namespace {
    template <class... A> std::string fmt(const char* pattern, A... args)
    {
        int n = std::snprintf(nullptr, 0, pattern, args...);
        std::vector<char> buffer(static_cast<std::size_t>(std::max(0, n)) + 1);
        std::snprintf(buffer.data(), buffer.size(), pattern, args...);
        return buffer.data();
    }
    TimingWarpTransform initial(TimingWarpKind kind, TimingWarpOptions options = {})
    {
        switch (kind) {
        case TimingWarpKind::Exponential:
            return TimingWarpTransform::exponential(2, options);
        case TimingWarpKind::StepQuantize:
            return TimingWarpTransform::stepQuantize(8, options);
        case TimingWarpKind::EuclideanQuantize:
            return TimingWarpTransform::euclideanQuantize(3, 8, options);
        }
        return {};
    }
    // Unicode White_Space, matching the native name field's trimming (including
    // pasted NBSP and ideographic spaces), without depending on a process locale.
    std::string trimName(std::string text)
    {
        const std::vector<std::string> spaces = { " ", "\t", "\n", "\r", "\v", "\f", "\xc2\x85",
            "\xc2\xa0", "\xe1\x9a\x80", "\xe2\x80\x80", "\xe2\x80\x81", "\xe2\x80\x82",
            "\xe2\x80\x83", "\xe2\x80\x84", "\xe2\x80\x85", "\xe2\x80\x86", "\xe2\x80\x87",
            "\xe2\x80\x88", "\xe2\x80\x89", "\xe2\x80\x8a", "\xe2\x80\xa8", "\xe2\x80\xa9",
            "\xe2\x80\xaf", "\xe2\x81\x9f", "\xe3\x80\x80" };
        bool removed;
        do {
            removed = false;
            for (const auto& space : spaces) {
                if (text.compare(0, space.size(), space) == 0) {
                    text.erase(0, space.size());
                    removed = true;
                }
                if (text.size() >= space.size()
                    && text.compare(text.size() - space.size(), space.size(), space) == 0) {
                    text.erase(text.size() - space.size());
                    removed = true;
                }
            }
        } while (removed);
        return text;
    }
}
void WarpEditor::reconcile()
{
    slot = std::min(slot, kMaximumTimingWarpLibraryEntries - 1);
    selected = std::min(
        selected, std::max(std::size_t(1), state.session.transport.timingWarp.size()) - 1);
}
void WarpEditor::publish()
{
    reconcile();
    if (changed)
        changed();
}
bool WarpEditor::selectSlot(std::size_t index)
{
    if (index >= kMaximumTimingWarpLibraryEntries)
        return false;
    slot = index;
    if (const auto* entry = state.session.warpLibrary.entry(slot)) {
        state.session.transport.timingWarp = entry->stack;
        state.session.transport.warpCycleTicks = entry->cycleTicks;
        selected = 0;
        publish();
    }
    return true;
}
std::string WarpEditor::name() const
{
    auto* entry = state.session.warpLibrary.entry(slot);
    return entry ? entry->name : "";
}
bool WarpEditor::save(std::string name)
{
    name = trimName(std::move(name));
    if (name.empty())
        name = fmt("WARP %02zu", slot + 1);
    const auto& transport = state.session.transport;
    if (!state.session.warpLibrary.store(
            slot, name, transport.warpCycleTicks, transport.timingWarp))
        return false;
    publish();
    return true;
}
bool WarpEditor::erase()
{
    if (!state.session.warpLibrary.erase(slot))
        return false;
    publish();
    return true;
}
void WarpEditor::toggle()
{
    auto& enabled = state.session.transport.timingWarpEnabled;
    enabled = !enabled;
    publish();
}
bool WarpEditor::add(TimingWarpKind kind)
{
    auto stack = state.session.transport.timingWarp;
    if (!stack.append(initial(kind)).added())
        return false;
    state.session.transport.timingWarp = stack;
    selected = stack.size() - 1;
    publish();
    return true;
}
bool WarpEditor::remove()
{
    const auto& source = state.session.transport.timingWarp;
    if (selected >= source.size())
        return false;
    std::array<TimingWarpTransform, TimingWarpStack::kMaximumTransforms> values {};
    std::size_t count = 0;
    for (std::size_t i = 0; i < source.size(); ++i)
        if (i != selected)
            values[count++] = *source.transform(i);
    TimingWarpStack stack;
    (void)stack.compile(values.data(), count);
    state.session.transport.timingWarp = stack;
    if (selected)
        --selected;
    publish();
    return true;
}
void WarpEditor::clear()
{
    state.session.transport.timingWarp.clear();
    selected = 0;
    publish();
}
bool WarpEditor::replace(TimingWarpTransform transform)
{
    const auto& source = state.session.transport.timingWarp;
    if (selected >= source.size())
        return false;
    std::array<TimingWarpTransform, TimingWarpStack::kMaximumTransforms> values {};
    for (std::size_t i = 0; i < source.size(); ++i)
        values[i] = i == selected ? transform : *source.transform(i);
    TimingWarpStack stack;
    if (stack.compile(values.data(), source.size()).rejected)
        return false;
    state.session.transport.timingWarp = stack;
    publish();
    return true;
}
bool WarpEditor::setKind(TimingWarpKind kind)
{
    const auto* t = state.session.transport.timingWarp.transform(selected);
    return t && replace(t->kind == kind ? *t : initial(kind, t->options));
}
WarpFieldValue WarpEditor::field(WarpField field) const
{
    const auto& transport = state.session.transport;
    if (field == WarpField::Cycle)
        return { double(transport.warpCycleTicks), 1, 16, 0, true, true };
    const auto* t = transport.timingWarp.transform(selected);
    if (!t)
        return { 0, 0, 1, 2, false, field != WarpField::Pulses };
    switch (field) {
    case WarpField::Primary:
        return t->kind == TimingWarpKind::Exponential
            ? WarpFieldValue { t->exponent, .1, 16, 3, true, true }
            : WarpFieldValue { double(t->steps), 1, 64, 0, true, true };
    case WarpField::Pulses:
        return { double(t->pulses), 1, double(t->steps), 0, true,
            t->kind == TimingWarpKind::EuclideanQuantize };
    case WarpField::Mix:
        return { t->options.alpha, 0, 1, 2, true, true };
    case WarpField::Begin:
        return { t->options.phaseBegin, 0, 1, 2, true, true };
    case WarpField::End:
        return { t->options.phaseEnd, 0, 1, 2, true, true };
    case WarpField::Repeats:
        return { double(t->options.repetitions), 1, 16, 0, true, true };
    case WarpField::Cycle:
        break;
    }
    return {};
}
bool WarpEditor::set(WarpField field, double value)
{
    if (!std::isfinite(value))
        return false;
    if (field == WarpField::Cycle) {
        if (value < 1 || value > 16 || value != std::floor(value))
            return false;
        state.session.transport.warpCycleTicks = static_cast<uint32_t>(value);
        publish();
        return true;
    }
    const auto* original = state.session.transport.timingWarp.transform(selected);
    if (!original)
        return false;
    auto t = *original;
    switch (field) {
    case WarpField::Primary:
        if (t.kind == TimingWarpKind::Exponential) {
            if (value <= 0)
                return false;
            t.exponent = value;
        } else {
            if (value < 1 || value > 64)
                return false;
            t.steps = static_cast<uint32_t>(std::llround(value));
        }
        break;
    case WarpField::Pulses:
        if (value < 1 || value > t.steps || value != std::floor(value))
            return false;
        t.pulses = static_cast<uint32_t>(value);
        break;
    case WarpField::Mix:
        t.options.alpha = value;
        break;
    case WarpField::Begin:
        t.options.phaseBegin = value;
        break;
    case WarpField::End:
        t.options.phaseEnd = value;
        break;
    case WarpField::Repeats:
        if (value < 1 || value > 16 || value != std::floor(value))
            return false;
        t.options.repetitions = static_cast<uint32_t>(value);
        break;
    case WarpField::Cycle:
        break;
    }
    if (t.options.alpha < 0 || t.options.alpha > 1 || t.options.phaseBegin < 0
        || t.options.phaseEnd > 1 || t.options.phaseBegin >= t.options.phaseEnd
        || (t.kind == TimingWarpKind::EuclideanQuantize && t.pulses > t.steps))
        return false;
    return replace(t);
}
std::vector<std::string> WarpEditor::slots() const
{
    std::vector<std::string> values;
    for (std::size_t i = 0; i < kMaximumTimingWarpLibraryEntries; ++i) {
        const auto* e = state.session.warpLibrary.entry(i);
        values.push_back(e ? fmt("%02zu  ·  %s  ·  %uT / %zuX", i + 1, e->name.c_str(),
                             e->cycleTicks, e->stack.size())
                           : fmt("%02zu  ·  EMPTY", i + 1));
    }
    return values;
}
std::vector<std::string> WarpEditor::transforms() const
{
    std::vector<std::string> values;
    const auto& stack = state.session.transport.timingWarp;
    for (std::size_t i = 0; i < stack.size(); ++i) {
        const auto& t = *stack.transform(i);
        values.push_back(t.kind == TimingWarpKind::Exponential
                ? fmt("%02zu  EXP  %.3g", i + 1, t.exponent)
                : t.kind == TimingWarpKind::StepQuantize
                    ? fmt("%02zu  STEP  %u", i + 1, t.steps)
                    : fmt("%02zu  EUCLID  %u/%u", i + 1, t.pulses, t.steps));
    }
    if (values.empty())
        values.push_back("NO TRANSFORMS");
    return values;
}
std::string WarpEditor::status() const
{
    const auto& transport = state.session.transport;
    auto count = transport.timingWarp.size(), saved = state.session.warpLibrary.size();
    const char* mode = transport.timingWarpEnabled ? "PLAYBACK ON" : "BYPASSED";
    return count
        ? fmt("%s • %zu TRANSFORM%s • SERIAL LEFT → RIGHT\n%zu SAVED • SONG RECALLS SAVED SLOTS",
            mode, count, count == 1 ? "" : "S", saved)
        : fmt("%s • IDENTITY TIMING • ADD EXP, STEP, OR EUCLID\n%zu SAVED WARP%s", mode, saved,
            saved == 1 ? "" : "S");
}
std::string WarpEditor::playbackDescription() const
{
    if (!state.playing || !state.timingWarpPlaybackActive)
        return "Warp playback inactive";
    auto cycle = std::max(1u, state.timingWarpPlaybackCycleTicks);
    return fmt("%s warp playback, step %llu of %u",
        state.timingWarpPlaybackFromSong ? "Song" : "Pattern",
        static_cast<unsigned long long>(state.timingWarpPlaybackTick % cycle + 1), cycle);
}

DisplayList paintWarpCurve(const app::TrackerViewState& state, Rect bounds, GridFont font,
    std::function<Color(uint32_t, double)> convert)
{
    auto color = [&](uint32_t rgb, double alpha = 1.) {
        return convert ? convert(rgb, alpha)
                       : Color { uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb),
                             uint8_t(std::lround(alpha * 255)) };
    };
    DisplayList list;
    list.shape(Primitive::FillRect, bounds, color(0x0a0a0a));
    Rect g { bounds.x + 30, bounds.y + 22, bounds.width - 60, bounds.height - 44 };
    if (g.width <= 1 || g.height <= 1)
        return list;
    const auto& t = state.session.transport;
    bool active = state.playing && state.timingWarpPlaybackActive;
    const auto& stack = active ? state.timingWarpPlaybackStack : t.timingWarp;
    bool enabled = active || t.timingWarpEnabled;
    uint32_t cycle = std::max(1u, active ? state.timingWarpPlaybackCycleTicks : t.warpCycleTicks);
    for (uint32_t tick = 0; tick <= cycle; ++tick)
        list.shape(Primitive::FillRect, { g.x + g.width * tick / cycle, g.y, 1, g.height },
            color(0x303030, tick == 0 || tick == cycle ? .9 : .55));
    for (unsigned i = 1; i < 4; ++i)
        list.shape(
            Primitive::FillRect, { g.x, g.y + g.height * i / 4, g.width, 1 }, color(0x303030, .42));
    std::vector<Point> identity { { g.x, g.y + g.height }, { g.x + g.width, g.y } };
    list.polyline(identity, color(0x656565, .72), 1, { 4, 4 });
    auto point = [&](double input) {
        return Point { g.x + g.width * input, g.y + g.height * (1 - stack.map(input)) };
    };
    auto curve = [&](double end, unsigned samples, Color c, double width) {
        std::vector<Point> points;
        for (unsigned i = 0; i <= samples; ++i)
            points.push_back(point(end * i / samples));
        list.polyline(std::move(points), c, width);
    };
    curve(1, 256, color(enabled ? 0x7fd7e8 : 0x656565, enabled ? 1 : .55), 2);
    double input = double(state.timingWarpPlaybackTick % cycle) / cycle;
    auto p = point(input);
    if (active) {
        curve(input, 192, color(0x7fd7e8), 3);
        list.shape(Primitive::FillRect, { p.x, g.y, 1, g.height }, color(0x7fd7e8, .22));
    }
    if (!enabled)
        list.polyline(identity, color(0x7fd7e8), 2);
    for (uint32_t tick = 0; tick <= cycle; ++tick) {
        double phase = double(tick) / cycle;
        auto dot = enabled ? point(phase)
                           : Point { g.x + g.width * phase, g.y + g.height * (1 - phase) };
        list.shape(Primitive::FillEllipse, { dot.x - 2.5, dot.y - 2.5, 5, 5 }, color(0xe8d47d));
    }
    if (active) {
        list.shape(Primitive::FillEllipse, { p.x - 7, p.y - 7, 14, 14 }, color(0x7fd7e8, .2));
        list.shape(Primitive::FillEllipse, { p.x - 4, p.y - 4, 8, 8 }, color(0xe8d47d));
        list.shape(Primitive::StrokeEllipse, { p.x - 4, p.y - 4, 8, 8 }, color(0x7fd7e8), 1.5);
    }
    auto text = [&](std::string value, Rect r, Alignment align = Alignment::Left) {
        list.text(value, r, color(0x878787), font.name, font.size, r.y + font.baseline, align);
    };
    text("INPUT PHASE", { g.x + g.width - 70, g.y + g.height + 5, 100, 16 });
    text(active ? (state.timingWarpPlaybackFromSong ? "SONG WARP · PLAYING" : "WARPED · PLAYING")
                : enabled ? "WARPED" : "OUTPUT · BYPASSED",
        { g.x, bounds.y + 4, g.width, 16 });
    if (active)
        text(fmt("STEP %02llu / %02u",
                 static_cast<unsigned long long>(state.timingWarpPlaybackTick % cycle + 1), cycle),
            { g.x, bounds.y + 4, g.width, 16 }, Alignment::Right);
    return list;
}
}
