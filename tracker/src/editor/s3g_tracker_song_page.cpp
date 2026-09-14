#include "s3g_tracker_song_page.h"
#include "s3g/tracker/editor_palette.h"
#include "s3g_gui_layout.h"
#include "vstgui/lib/events.h"
#include <cmath>
#include <cstdio>

namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace {
    constexpr double tableHeaderHeight = 28;
    CRect rect(s3g::gui_layout::Rect r) { return toolRect(r.x, r.y, r.width, r.height); }
    uint32_t legacy(uint32_t rgb)
    {
        auto r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
        return std::max({ r, g, b }) - std::min({ r, g, b }) <= 8
            ? nightNeutral(uint8_t((r + g + b) / 3))
            : rgb;
    }
    std::string decimal(double value)
    {
        char text[40];
        std::snprintf(text, sizeof(text), "%.1f", value);
        return text;
    }
    std::string rowId(std::size_t row, const std::string& kind)
    {
        return "row" + std::to_string(row) + ":" + kind;
    }
}
SongPageView::SongPageView(ToolPageServices services)
    : ToolPageView(std::move(services))
{
}
SongPageView::~SongPageView() { stopRefresh(); }
CRect SongPageView::tableViewport() const
{
    auto f = s3g::gui_layout::trackerSongFamilyLayout(
        { getViewSize().getWidth(), getViewSize().getHeight() });
    return toolRect(f.arrangement.frame.x + 1, f.arrangement.frame.y + 21,
        f.arrangement.frame.width - 2, f.arrangement.frame.height - 22);
}
std::array<double, 11> SongPageView::columnWidths() const
{
    std::array<double, 11> widths { 40, 110, 92, 140, 48, 68, 66, 78, 58, 344, 40 };
    double fixed = 11;
    for (std::size_t i = 0; i < widths.size(); ++i)
        if (i != 9)
            fixed += widths[i];
    widths[9] = std::max(344., tableViewport().getWidth() - fixed);
    return widths;
}
double SongPageView::tableWidth() const
{
    double width = 11;
    for (auto c : columnWidths())
        width += c;
    return width;
}
CRect SongPageView::cellBounds(std::size_t row, int column) const
{
    auto widths = columnWidths();
    auto vp = tableViewport();
    double x = vp.left - scrollX_;
    for (int i = 0; i < column; ++i)
        x += widths[std::size_t(i)] + 1;
    return toolRect(x, vp.top + tableHeaderHeight + double(row) * 49 - scrollY_,
        widths[std::size_t(column)], 48);
}
CRect SongPageView::swingBounds(std::size_t row) const
{
    auto c = cellBounds(row, 7);
    return toolRect(c.left + 4, c.top + 12.5, c.getWidth() - 8, 23);
}
void SongPageView::scrollTo(double x, double y)
{
    auto vp = tableViewport();
    scrollX_ = std::clamp(x, 0., std::max(0., tableWidth() - vp.getWidth()));
    scrollY_ = std::clamp(y, 0.,
        std::max(0.,
            double(editor.arrangement.rows.size()) * 49 - (vp.getHeight() - tableHeaderHeight)));
    invalid();
}
void SongPageView::revealSelection()
{
    if (editor.selected < 0)
        return;
    auto vp = tableViewport();
    auto c = cellBounds(std::size_t(editor.selected), 0);
    double y = scrollY_;
    if (c.top < vp.top + tableHeaderHeight)
        y -= vp.top + tableHeaderHeight - c.top;
    else if (c.bottom > vp.bottom - 10)
        y += c.bottom - vp.bottom + 10;
    scrollTo(scrollX_, y);
}
void SongPageView::modelChanged()
{
    stopRefresh();
    editor.selected = editor.arrangement.rows.empty()
        ? -1
        : std::clamp(editor.selected, -1, int(editor.arrangement.rows.size()) - 1);
    scrollTo(scrollX_, scrollY_);
    invalid();
}
void SongPageView::resize(double w, double h)
{
    ToolPageView::resize(w, h);
    scrollTo(scrollX_, scrollY_);
}
void SongPageView::stopRefresh()
{
    autoscroll_ = nullptr;
    scrollSwingRow_.reset();
    scrollRemainder_ = 0;
    finishSwing(true);
    dragRow_.reset();
    scrollbarDrag_ = 0;
    ToolPageView::stopRefresh();
}
void SongPageView::onMouseCancelEvent(MouseCancelEvent& e)
{
    stopRefresh();
    invalid();
    e.consumed = true;
}
void SongPageView::rowMenu(std::size_t row, SongField field, CRect bounds, bool enabled)
{
    std::vector<ToolMenuItem> items;
    for (auto c : editor.choices(row, field))
        items.push_back({ c.title,
            [this, row, field, c] {
                editor.choose(row, field, c);
                invalid();
            },
            c.selected, true });
    menu(bounds, rowId(row, std::to_string(int(field))), std::move(items), enabled);
}
void SongPageView::drawSwing(std::size_t row, CRect r)
{
    double value = swingRow_ && *swingRow_ == row ? swingPreview_ : editor.swingPercent(row);
    bool has = editor.arrangement.rows[row].swing.has_value()
        || (swingRow_ && *swingRow_ == row && swingMoved_);
    // Convert the native Swing NSControl's bottom-left track coordinates.
    auto t = toolRect(r.left + 2, r.top + 5, std::max(18., r.getWidth() - 42), 9);
    double n = (value - 50) / 25;
    fill(t, 0x131313);
    fill(toolRect(t.left + 1, t.top + 1, std::max(1., (t.getWidth() - 2) * n), 7),
        has ? 0x7f7f7f : 0x656565);
    fill(toolRect(std::clamp(t.left + t.getWidth() * n - 1.5, t.left + 1, t.right - 4), t.top - 2,
             3, 13),
        0xc9c9c9);
    label(has ? decimal(value) : "—", toolRect(r.right - 34, r.top + 2, 32, 15), 0x929292,
        Alignment::Right);
}
void SongPageView::drawPage()
{
    fill(getViewSize(), 0x0a0a0a);
    auto family = s3g::gui_layout::trackerSongFamilyLayout(
        { getViewSize().getWidth(), getViewSize().getHeight() });
    auto project = rect(family.project.frame), transport = rect(family.transport.frame),
         tools = rect(family.rowTools.frame), arrangement = rect(family.arrangement.frame);
    panel(project, "SONG / FILE");
    panel(transport, "TRANSPORT / QUEUE");
    panel(tools, "ROW EDIT");
    panel(arrangement, "ARRANGEMENT / ROWS");
    auto control = [](CRect p, double x, double width) {
        return toolRect(p.left + x, p.top + 27, std::max(20., width), 22);
    };
    menu(control(project, 8, project.getWidth() - 16), "file",
        { { "SAVE SONG + PATTERNS…",
              [this] {
                  if (editor.callbacks.saveProject)
                      editor.callbacks.saveProject();
              } },
            { "LOAD SONG + PATTERNS…",
                [this] {
                    if (editor.callbacks.loadProject)
                        editor.callbacks.loadProject();
                } } },
        true, "SONG FILE");
    double width = transport.getWidth() - 16, x = 8;
    double mode = std::clamp(width * .18, 82., 108.), loop = std::clamp(width * .16, 72., 96.),
           quant = std::clamp(width * .20, 96., 120.), queue = std::clamp(width * .20, 92., 120.);
    button(
        control(transport, x, mode), "mode", editor.playbackEnabled ? "SONG: ON" : "SONG: OFF",
        [this] {
            editor.toggleMode();
            invalid();
        },
        true, editor.playbackEnabled ? 2 : -1);
    x += mode + 4;
    std::string loopTitle = editor.loadedLoopTitle ? "LOOP SONG: " : "LOOP: ";
    loopTitle += editor.arrangement.loop ? "ON" : "OFF";
    button(
        control(transport, x, loop), "loop", loopTitle,
        [this] {
            editor.toggleLoop();
            invalid();
        },
        true, editor.arrangement.loop ? 1 : 0);
    x += loop + 4;
    std::vector<ToolMenuItem> quantItems;
    const char* names[] = { "NEXT TICK", "NEXT BEAT", "END OF PASS", "END OF ROW" };
    for (int i = 0; i < 4; ++i)
        quantItems.push_back({ names[i],
            [this, i] {
                editor.quantization = SongLaunchQuantization(i);
                invalid();
            },
            int(editor.quantization) == i });
    menu(control(transport, x, quant), "quantization", std::move(quantItems),
        editor.playbackEnabled && editor.playing);
    x += quant + 4;
    button(
        control(transport, x, queue), "queue", "SELECT QUEUE",
        [this] {
            editor.queue();
            invalid();
        },
        editor.queueAvailable());
    x += queue + 4;
    auto status = control(transport, x, transport.getWidth() - 8 - x);
    label(fit(editor.queueStatus(), status.getWidth(), 8.5), status,
        editor.pendingRow ? 0xf0ad6d : legacy(0x737879), Alignment::Left, 8.5);
    double tw = tools.getWidth() - 16, add = tw * .16, dup = tw * .15, del = tw * .16,
           mv = std::clamp(tw * .09, 28., 38.);
    x = 8;
    bool selected
        = editor.selected >= 0 && std::size_t(editor.selected) < editor.arrangement.rows.size();
    button(
        control(tools, x, add), "add", "＋ ADD",
        [this] {
            editor.add();
            revealSelection();
        },
        editor.arrangement.rows.size() < kMaximumSongRows);
    x += add + 4;
    button(
        control(tools, x, dup), "duplicate", "DUP",
        [this] {
            editor.duplicate();
            revealSelection();
        },
        selected && editor.arrangement.rows.size() < kMaximumSongRows);
    x += dup + 4;
    button(
        control(tools, x, del), "delete", "− DEL",
        [this] {
            editor.erase(std::size_t(editor.selected));
            scrollTo(scrollX_, scrollY_);
        },
        selected);
    x += del + 4;
    button(
        control(tools, x, mv), "up", "↑",
        [this] {
            editor.move(-1);
            revealSelection();
        },
        selected && editor.selected > 0);
    x += mv + 4;
    button(
        control(tools, x, mv), "down", "↓",
        [this] {
            editor.move(1);
            revealSelection();
        },
        selected && std::size_t(editor.selected + 1) < editor.arrangement.rows.size());
    auto vp = tableViewport();
    fill(vp, legacy(0x0e0e0e));
    CRect previousClip;
    context_->getClipRect(previousClip);
    auto clip = vp;
    clip.bound(previousClip);
    context_->setClipRect(clip);
    auto widths = columnWidths();
    double cx = vp.left - scrollX_;
    const char* headers[] = { "ROW", "PATTERN", "WARP", "LOOP IN–OUT", "REP", "TICKS / SPAN",
        "BPM ×", "SWING %", "EN %", "LANE MUTES  1–16 TOP · 17–32 BOTTOM", "DEL" };
    for (std::size_t i = 0; i < widths.size(); ++i) {
        fill(toolRect(cx, vp.top, widths[i] + 1, tableHeaderHeight), legacy(0x181818));
        fill(toolRect(cx + widths[i] - .5, vp.top, 1, tableHeaderHeight), legacy(0x3a3a3a));
        label(headers[i], toolRect(cx + 4, vp.top + 6, widths[i] - 8, 16), legacy(0x8e9697),
            Alignment::Center);
        cx += widths[i] + 1;
    }
    auto body = vp;
    body.top += tableHeaderHeight;
    clip = body;
    clip.bound(previousClip);
    context_->setClipRect(clip);
    hitClip_ = body;
    std::size_t first = std::size_t(std::max(0., std::floor(scrollY_ / 49)));
    std::size_t last = std::min(
        editor.arrangement.rows.size(), first + std::size_t(std::ceil(body.getHeight() / 49)) + 1);
    for (std::size_t row = first; row < last; ++row) {
        const auto& data = editor.arrangement.rows[row];
        auto c = cellBounds(row, 0);
        auto rr = toolRect(vp.left - scrollX_, c.top, tableWidth(), 48);
        fill(rr, legacy(row % 2 ? 0x151515 : 0x111111));
        bool active = editor.playbackRow && *editor.playbackRow == row,
             pending = editor.pendingRow && *editor.pendingRow == row;
        if (active)
            fill(rr, 0x72d68c, .10);
        if (int(row) == editor.selected) {
            fill(rr, 0x424242);
            stroke(CRect(rr).inset(.5, .5), 0xc0c0bc, .82);
        }
        if (active)
            fill(toolRect(rr.left, rr.top, 3, 48), 0x72d68c);
        if (pending) {
            fill(rr, 0xf0ad6d, .12);
            fill(toolRect(rr.left, rr.top, rr.getWidth(), 2), 0xf0ad6d);
            fill(toolRect(rr.left, rr.bottom - 2, rr.getWidth(), 2), 0xf0ad6d);
            fill(toolRect(rr.right - 3, rr.top, 3, 48), 0xf0ad6d);
        }
        char rowText[40];
        std::snprintf(rowText, sizeof(rowText), "⠿ %02zu", row + 1);
        label(rowText, toolRect(c.left + 4, c.top + 12.5, c.getWidth() - 8, 23), legacy(0xa0a7a8),
            Alignment::Center, 11);
        auto popup = [&](int col) {
            auto b = cellBounds(row, col);
            return toolRect(b.left + 4, b.top + 16.5, b.getWidth() - 8, 15);
        };
        rowMenu(row, SongField::Pattern, popup(1));
        rowMenu(row, SongField::Warp, popup(2));
        c = cellBounds(row, 3);
        double w = (c.getWidth() - 12) * .5;
        rowMenu(row, SongField::LoopIn, toolRect(c.left + 4, c.top + 16.5, w, 15));
        rowMenu(row, SongField::LoopOut, toolRect(c.left + 8 + w, c.top + 16.5, w, 15),
            data.patternLoop.has_value());
        rowMenu(row, SongField::Repeats, popup(4));
        c = cellBounds(row, 5);
        rowMenu(row, SongField::Ticks, toolRect(c.left + 4, c.top + 5, c.getWidth() - 8, 15));
        label(editor.spanSummary(row), toolRect(c.left + 4, c.top + 25, c.getWidth() - 8, 14),
            0x878787, Alignment::Center, 7);
        rowMenu(row, SongField::Tempo, popup(6));
        drawSwing(row, swingBounds(row));
        rowMenu(row, SongField::Energy, popup(8));
        c = cellBounds(row, 9);
        double slotW = std::max(22., (std::max(340., c.getWidth() - 4) - 8) / 16), slotH = 23;
        for (uint32_t lane = 0; lane < 32; ++lane) {
            auto slot = toolRect(c.left + 6 + (lane % 16) * slotW, c.top + 1 + (lane / 16) * slotH,
                slotW - 2, slotH);
            auto box = CRect(slot).inset(2, 3);
            bool available = lane < editor.laneCount(data.patternId),
                 muted = available && (data.mutedTracks & (uint32_t(1) << lane));
            fill(box, legacy(!available ? 0x151515 : muted ? 0x303030 : 0x242424));
            if (muted)
                fill(toolRect(box.left + 1, box.bottom - 3, box.getWidth() - 2, 2), 0xf06a72);
            // NSButton paints five points below the slot top, despite its
            // containing mute matrix having bottom-left coordinates.
            label(muted ? "×" : std::to_string(lane + 1),
                toolRect(slot.left, slot.top + 5, slot.getWidth(), 18),
                !available ? 0x656565 : muted ? 0xf06a72 : 0xbababa, Alignment::Center, 10,
                available ? 1 : .55);
            if (available)
                hit(slot, rowId(row, "mute" + std::to_string(lane)), [this, row, lane] {
                    editor.toggleMute(row, lane);
                    invalid();
                });
        }
        c = cellBounds(row, 10);
        auto saved = hitClip_;
        auto deleteClip = clip;
        deleteClip.bound(c);
        context_->setClipRect(deleteClip);
        hitClip_ = deleteClip;
        button(toolRect(c.left + 4, c.top + 9, 40, 30), rowId(row, "delete"), "×", [this, row] {
            editor.erase(row);
            scrollTo(scrollX_, scrollY_);
        });
        context_->setClipRect(clip);
        hitClip_ = saved;
    }
    // Grid continues through empty table space, as in NSTableView.
    cx = vp.left - scrollX_;
    for (double w : widths) {
        cx += w + 1;
        fill(toolRect(cx - 1, body.top, 1, body.getHeight()), legacy(0x303030));
    }
    for (double y = body.top + std::floor(scrollY_ / 49) * 49 - scrollY_ + 48; y < body.bottom;
         y += 49) {
        fill(toolRect(vp.left - scrollX_, y, tableWidth(), 1), legacy(0x303030));
    }
    if (dragRow_ && dragMoved_)
        fill(toolRect(vp.left, body.top + double(dropRow_) * 49 - scrollY_, vp.getWidth(), 2),
            0xc0c0bc);
    context_->setClipRect(previousClip);
    hitClip_ = getViewSize();
    double fullHeight = double(editor.arrangement.rows.size()) * 49;
    if (fullHeight > body.getHeight()) {
        fill(toolRect(vp.right - 9, body.top, 9, body.getHeight()), 0x131313, .8);
        double h = std::max(24., body.getHeight() * body.getHeight() / fullHeight),
               y = body.top + scrollY_ / (fullHeight - body.getHeight()) * (body.getHeight() - h);
        fill(toolRect(vp.right - 7, y, 5, h), 0x656565);
    }
    if (tableWidth() > vp.getWidth()) {
        fill(toolRect(vp.left, vp.bottom - 9, vp.getWidth(), 9), 0x131313, .8);
        double w = std::max(24., vp.getWidth() * vp.getWidth() / tableWidth()),
               left = vp.left + scrollX_ / (tableWidth() - vp.getWidth()) * (vp.getWidth() - w);
        fill(toolRect(left, vp.bottom - 7, w, 5), 0x656565);
    }
}
void SongPageView::stageSwing(double x)
{
    if (!swingRow_)
        return;
    auto r = swingBounds(*swingRow_);
    auto track = toolRect(r.left + 2, r.top + 9, std::max(18., r.getWidth() - 42), 9);
    swingPreview_
        = std::round((50 + std::clamp((x - track.left) / track.getWidth(), 0., 1.) * 25) * 10) / 10;
    swingMoved_ = true;
    invalid();
}
void SongPageView::finishSwing(bool cancel)
{
    auto row = swingRow_;
    swingRow_.reset();
    bool changed = swingMoved_;
    swingMoved_ = false;
    if (!cancel && row && changed && *row < editor.arrangement.rows.size()
        && editor.arrangement.rows[*row].id == gestureIdentity_)
        editor.swing(*row, swingPreview_);
}
void SongPageView::onMouseDownEvent(MouseDownEvent& e)
{
    auto vp = tableViewport();
    bool hadMenu = menuOpen();
    if (!hadMenu && (e.buttonState.isLeft() || e.buttonState.isRight())) {
        auto body = vp;
        body.top += tableHeaderHeight;
        if (toolContains(body, e.mousePosition)) {
            if (e.mousePosition.x >= vp.right - 9
                && double(editor.arrangement.rows.size()) * 49 > body.getHeight()) {
                scrollbarDrag_ = 2;
                scrollbarOrigin_ = e.mousePosition.y;
                scrollStart_ = scrollY_;
                e.consumed = true;
                return;
            }
            if (e.mousePosition.y >= vp.bottom - 9 && tableWidth() > vp.getWidth()) {
                scrollbarDrag_ = 1;
                scrollbarOrigin_ = e.mousePosition.x;
                scrollStart_ = scrollX_;
                e.consumed = true;
                return;
            }
            auto row = std::size_t(
                std::max(0., std::floor((e.mousePosition.y - body.top + scrollY_) / 49)));
            if (row < editor.arrangement.rows.size()
                && toolContains(swingBounds(row), e.mousePosition)) {
                focus();
                if (e.buttonState.isRight() || e.modifiers.has(ModifierKey::Alt)) {
                    editor.swing(row, {});
                    invalid();
                } else if (e.mousePosition.x
                    < swingBounds(row).left + std::max(18., swingBounds(row).getWidth() - 42) + 4) {
                    swingRow_ = row;
                    gestureIdentity_ = editor.arrangement.rows[row].id;
                    swingPreview_ = editor.swingPercent(row);
                    stageSwing(e.mousePosition.x);
                }
                e.consumed = true;
                return;
            }
        }
    }
    ToolPageView::onMouseDownEvent(e);
    if (e.consumed || !e.buttonState.isLeft())
        return;
    if (e.mousePosition.y < vp.top + tableHeaderHeight || !toolContains(vp, e.mousePosition))
        return;
    int row = int(std::floor((e.mousePosition.y - vp.top - tableHeaderHeight + scrollY_) / 49));
    if (row < 0 || std::size_t(row) >= editor.arrangement.rows.size()) {
        editor.selected = -1;
        invalid();
        return;
    }
    editor.selected = row;
    if (toolContains(cellBounds(std::size_t(row), 0), e.mousePosition)) {
        dragRow_ = std::size_t(row);
        gestureIdentity_ = editor.arrangement.rows[std::size_t(row)].id;
        dragOrigin_ = e.mousePosition;
        dragMoved_ = false;
        copyDrag_ = e.modifiers.has(ModifierKey::Alt);
        dropRow_ = std::size_t(row);
    }
    e.consumed = true;
    invalid();
}
void SongPageView::trackScrollbar(CPoint p)
{
    auto vp = tableViewport();
    if (scrollbarDrag_ == 1) {
        double thumb = std::max(24., vp.getWidth() * vp.getWidth() / tableWidth());
        scrollTo(scrollStart_
                + (p.x - scrollbarOrigin_) * (tableWidth() - vp.getWidth())
                    / std::max(1., vp.getWidth() - thumb),
            scrollY_);
    } else if (scrollbarDrag_ == 2) {
        double h = vp.getHeight() - tableHeaderHeight,
               total = double(editor.arrangement.rows.size()) * 49,
               thumb = std::max(24., h * h / total);
        scrollTo(scrollX_,
            scrollStart_ + (p.y - scrollbarOrigin_) * (total - h) / std::max(1., h - thumb));
    }
}
void SongPageView::onMouseMoveEvent(MouseMoveEvent& e)
{
    if (scrollbarDrag_) {
        trackScrollbar(e.mousePosition);
        e.consumed = true;
        return;
    }
    if (swingRow_) {
        stageSwing(e.mousePosition.x);
        e.consumed = true;
        return;
    }
    if (dragRow_) {
        dragMoved_
            |= std::hypot(e.mousePosition.x - dragOrigin_.x, e.mousePosition.y - dragOrigin_.y) > 4;
        copyDrag_ = e.modifiers.has(ModifierKey::Alt);
        dragPoint_ = e.mousePosition;
        updateDrop(false);
        // Interaction-only autoscroll: a stationary pointer at the table edge
        // must keep revealing drop targets. This timer never advances playback.
        if (dragMoved_ && !autoscroll_)
            autoscroll_ = owned(new CVSTGUITimer([this](CVSTGUITimer*) { updateDrop(true); }, 50));
        e.consumed = true;
        invalid();
        return;
    }
    ToolPageView::onMouseMoveEvent(e);
    if (!menuOpen()) {
        bool swing = false;
        auto vp = tableViewport();
        if (toolContains(vp, e.mousePosition) && e.mousePosition.y >= vp.top + tableHeaderHeight) {
            auto row = std::size_t(
                std::floor((e.mousePosition.y - vp.top - tableHeaderHeight + scrollY_) / 49));
            swing = row < editor.arrangement.rows.size()
                && toolContains(swingBounds(row), e.mousePosition);
        }
        if (getFrame())
            getFrame()->setCursor(swing ? kCursorHSize : kCursorDefault);
        std::string tip;
        if (swing)
            tip = "Swing override: drag or scroll; Option-click/right-click returns to base";
        else if (toolContains(controlBounds("file"), e.mousePosition))
            tip = "Save or load the Song arrangement and its complete pattern bank";
        else if (toolContains(vp, e.mousePosition)
            && e.mousePosition.y >= vp.top + tableHeaderHeight) {
            auto row = std::size_t(
                std::floor((e.mousePosition.y - vp.top - tableHeaderHeight + scrollY_) / 49));
            if (row < editor.arrangement.rows.size()) {
                const auto& data = editor.arrangement.rows[row];
                for (int col = 0; col < 11; ++col)
                    if (toolContains(cellBounds(row, col), e.mousePosition)) {
                        switch (col) {
                        case 0:
                            tip = "Drag to move this Song row; Option-drag duplicates it";
                            break;
                        case 1:
                            tip = std::any_of(editor.patterns.begin(), editor.patterns.end(),
                                      [&](const auto& p) { return p.id == data.patternId; })
                                ? data.patternId
                                : "This Song row references a pattern that is not in the bank";
                            break;
                        case 2:
                            if (data.timingWarpLibraryIndex
                                && !editor.warps.entry(*data.timingWarpLibraryIndex))
                                tip = "This Song row references an empty saved warp slot; playback "
                                      "uses OFF";
                            break;
                        case 3:
                            tip = "Loop In: OFF or the inclusive first pattern row. Loop Out: "
                                  "inclusive last row, not before Loop In";
                            break;
                        case 4:
                            tip = "Number of pattern-cycle repetitions, 1–64";
                            break;
                        case 5:
                            tip = editor.spanSummary(row)
                                + " · Ticks per pass, independent of the pattern/loop span";
                            break;
                        case 6:
                            tip = "Multiply the observed REAPER BPM for this Song row; also "
                                  "composes with the Tracker transport BPM multiplier";
                            break;
                        case 8:
                            tip = "Song intensity available to EN threshold cells; 100% reveals "
                                  "every EN-gated event";
                            break;
                        case 9:
                            tip = "Toggle lane mutes for this Song row; lanes 1–16 on top, 17–32 "
                                  "below";
                            break;
                        case 10:
                            tip = "Delete this song row";
                            break;
                        default:
                            break;
                        }
                        break;
                    }
            }
        }
        setTooltipText(tip.empty() ? nullptr : tip.c_str());
    }
}
void SongPageView::onMouseUpEvent(MouseUpEvent& e)
{
    if (scrollbarDrag_) {
        trackScrollbar(e.mousePosition);
        scrollbarDrag_ = 0;
        e.consumed = true;
        return;
    }
    if (swingRow_) {
        stageSwing(e.mousePosition.x);
        finishSwing();
        e.consumed = true;
        invalid();
        return;
    }
    if (dragRow_) {
        auto source = *dragRow_;
        dragRow_.reset();
        autoscroll_ = nullptr;
        if (dragMoved_ && source < editor.arrangement.rows.size()
            && editor.arrangement.rows[source].id == gestureIdentity_)
            editor.drop(source, dropRow_, copyDrag_);
        revealSelection();
        e.consumed = true;
        return;
    }
    ToolPageView::onMouseUpEvent(e);
}
void SongPageView::updateDrop(bool scroll)
{
    if (!dragRow_ || !dragMoved_)
        return;
    auto vp = tableViewport();
    if (scroll) {
        if (dragPoint_.y < vp.top + tableHeaderHeight + 12)
            scrollTo(scrollX_, scrollY_ - 12);
        else if (dragPoint_.y > vp.bottom - 12)
            scrollTo(scrollX_, scrollY_ + 12);
    }
    dropRow_ = std::size_t(
        std::clamp(std::floor((dragPoint_.y - vp.top - tableHeaderHeight + scrollY_ + 24.5) / 49),
            0., double(editor.arrangement.rows.size())));
    invalid();
}
void SongPageView::onMouseWheelEvent(MouseWheelEvent& e)
{
    if (menuOpen()) {
        e.consumed = true;
        return;
    }
    auto vp = tableViewport();
    if (!toolContains(vp, e.mousePosition))
        return;
    if (e.mousePosition.y >= vp.top + tableHeaderHeight) {
        auto row = std::size_t(
            std::floor((e.mousePosition.y - vp.top - tableHeaderHeight + scrollY_) / 49));
        if (row < editor.arrangement.rows.size()
            && toolContains(swingBounds(row), e.mousePosition)) {
            if (scrollSwingRow_ != row) {
                scrollSwingRow_ = row;
                scrollRemainder_ = 0;
            }
            int steps = 0;
            if (e.flags & MouseWheelEvent::PreciseDeltas) {
                scrollRemainder_ += e.deltaY * 10;
                steps = int(std::trunc(scrollRemainder_));
                scrollRemainder_ -= steps;
            } else {
                scrollRemainder_ = 0;
                steps = e.deltaY > 0 ? 1 : e.deltaY < 0 ? -1 : 0;
            }
            // Native precise scrolling publishes one increment per whole pixel.
            for (int i = 0; i < std::abs(steps); ++i)
                if (!editor.swing(row,
                        editor.swingPercent(row)
                            + (steps > 0 ? 1 : -1) * (e.modifiers.has(ModifierKey::Alt) ? .1 : .5)))
                    break;
            invalid();
            e.consumed = true;
            return;
        }
    }
    scrollSwingRow_.reset();
    scrollRemainder_ = 0;
    double unit = (e.flags & MouseWheelEvent::PreciseDeltas) ? 10 : 40;
    double dx = e.deltaX, dy = e.deltaY;
    if (e.modifiers.has(ModifierKey::Shift) && dx == 0) {
        dx = dy;
        dy = 0;
    }
    closeMenu();
    scrollTo(scrollX_ - dx * unit, scrollY_ - dy * unit);
    e.consumed = true;
}
void SongPageView::onKeyboardEvent(KeyboardEvent& e)
{
    if (e.type == EventType::KeyDown && e.virt == VirtualKey::Escape
        && (swingRow_ || dragRow_ || scrollbarDrag_)) {
        stopRefresh();
        invalid();
        e.consumed = true;
        return;
    }
    ToolPageView::onKeyboardEvent(e);
    if (e.consumed || e.type != EventType::KeyDown)
        return;
    if (e.virt == VirtualKey::Escape) {
        stopRefresh();
        invalid();
        e.consumed = true;
        return;
    }
    int next = editor.selected;
    if (e.virt == VirtualKey::Down)
        ++next;
    else if (e.virt == VirtualKey::Up)
        --next;
    else if (e.virt == VirtualKey::Home)
        next = 0;
    else if (e.virt == VirtualKey::End)
        next = int(editor.arrangement.rows.size()) - 1;
    else if (e.virt == VirtualKey::PageDown)
        next += std::max(1, int((tableViewport().getHeight() - tableHeaderHeight) / 49));
    else if (e.virt == VirtualKey::PageUp)
        next -= std::max(1, int((tableViewport().getHeight() - tableHeaderHeight) / 49));
    else
        return;
    editor.selected = editor.arrangement.rows.empty()
        ? -1
        : std::clamp(next, 0, int(editor.arrangement.rows.size()) - 1);
    revealSelection();
    e.consumed = true;
}
}
