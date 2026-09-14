#include "s3g_tracker_tool_page.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include <cmath>

namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace f = portable_gui::foundation;
ToolPageView::ToolPageView(ToolPageServices services)
    : ContentView(toolRect(0, 0, 1320, 820))
    , services_(std::move(services))
{
    if (!services_.font)
        services_.font = [](double size) {
            auto font = f::makeUiFont(size);
            auto platform = font->getPlatformFont();
            double ascent = platform ? platform->getAscent() : size;
            return GridFont { font->getName().getString(), size, ascent,
                platform ? std::ceil(ascent + platform->getDescent() + platform->getLeading())
                         : size * 1.3 };
        };
    setWantsFocus(true);
}
Color ToolPageView::color(uint32_t rgb, double a) const
{
    return services_.color ? services_.color(rgb, a)
                           : Color { uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb),
                                 uint8_t(std::lround(a * 255)) };
}
void ToolPageView::fill(CRect r, uint32_t rgb, double a)
{
    DisplayList list;
    list.shape(Primitive::FillRect, toolLogical(r), color(rgb, a));
    drawDisplayList(*context_, list, services_.fontFactory);
}
void ToolPageView::stroke(CRect r, uint32_t rgb, double a)
{
    DisplayList list;
    list.shape(Primitive::StrokeRect, toolLogical(r), color(rgb, a));
    drawDisplayList(*context_, list, services_.fontFactory);
}
void ToolPageView::label(
    std::string text, CRect r, uint32_t rgb, Alignment align, double size, double a)
{
    auto font = services_.font(size);
    DisplayList list;
    list.text(std::move(text), toolLogical(r), color(rgb, a), font.name, font.size,
        r.top + font.baseline, align);
    drawDisplayList(*context_, list, services_.fontFactory);
}
void ToolPageView::panel(CRect r, std::string title)
{
    fill(r, 0x1d1d1d);
    fill(toolRect(r.left, r.top, r.getWidth(), 21), 0x131313);
    fill(toolRect(r.left, r.top, r.getWidth(), 2), 0xb8b8b8);
    label(title, toolRect(r.left + 8, r.top + 5, r.getWidth() - 16, 16));
}
void ToolPageView::hit(CRect r, std::string id, std::function<void()> action, bool onDown)
{
    r.bound(hitClip_);
    if (!r.isEmpty())
        hits_.push_back({ r, std::move(id), std::move(action), onDown });
}
void ToolPageView::button(CRect r, std::string id, std::string title, std::function<void()> action,
    bool enabled, int state)
{
    bool pressed = pressed_ && pressed_->id == id && toolContains(r, hover_);
    uint32_t active = state == 2 ? 0x72d68c : state == -1 ? 0xf06a72 : 0x7fd7e8;
    auto box = CRect(r).inset(.5, .5);
    double alpha = state == 2 ? .2 : .16;
    fill(box,
        pressed ? 0x414141
                : state ? active : enabled && toolContains(r, hover_) ? 0x343434 : 0x292929,
        state && !pressed ? alpha : 1);
    if (state && !pressed) {
        fill(toolRect(box.left, box.top, box.getWidth(), 1), active, alpha);
        fill(toolRect(box.left, box.bottom - 1, box.getWidth(), 1), active, alpha);
        fill(toolRect(box.left, box.top + 1, 1, box.getHeight() - 2), active, alpha);
        fill(toolRect(box.right - 1, box.top + 1, 1, box.getHeight() - 2), active, alpha);
    }
    auto info = services_.font(10);
    auto font
        = services_.fontFactory ? services_.fontFactory(info.name, info.size) : f::makeUiFont(10);
    auto platform = font->getPlatformFont();
    double cap = platform && platform->getCapHeight() > 0 ? platform->getCapHeight() : 7.2;
    DisplayList list;
    list.text(title, toolLogical(r), color(!enabled ? 0x656565 : state ? active : 0x929292),
        info.name, info.size, centeredCapsBaseline(toolLogical(r), cap), Alignment::Center);
    drawDisplayList(*context_, list, services_.fontFactory);
    if (enabled)
        hit(r, std::move(id), std::move(action));
}
std::string ToolPageView::fit(std::string text, double width, double size)
{
    for (char& c : text)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    auto info = services_.font(size);
    auto font
        = services_.fontFactory ? services_.fontFactory(info.name, size) : f::makeUiFont(size);
    context_->setFont(font);
    if (context_->getStringWidth(text.c_str()) <= width)
        return text;
    while (!text.empty() && context_->getStringWidth((text + "…").c_str()) > width) {
        auto at = text.size() - 1;
        while (at && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
            --at;
        text.erase(at);
    }
    return text + "…";
}
void ToolPageView::menu(CRect r, std::string id, std::vector<ToolMenuItem> items, bool enabled,
    const std::string& display)
{
    if (items.empty())
        return;
    std::size_t selected = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].checked)
            selected = i;
    fill(toolRect(r.left, r.top, r.getWidth(), 15), 0x131313);
    fill(toolRect(r.left + 1, r.top + 1, 2, 13), enabled ? 0x7f7f7f : 0x303030);
    label(fit(display.empty() ? items[selected].title : display, r.getWidth() - 28),
        toolRect(r.left + 8, r.top + 2, r.getWidth() - 28, 20), enabled ? 0x929292 : 0x303030);
    label("v", toolRect(r.right - 12, r.top + 1, 10, 20), enabled ? 0x929292 : 0x303030);
    if (enabled)
        hit(
            r, std::move(id),
            [this, r, items = std::move(items), selected] { openToolMenu(r, items, selected); },
            true);
}
void ToolPageView::openToolMenu(CRect r, std::vector<ToolMenuItem> items, std::size_t selected)
{
    if (items.empty())
        return;
    double vw = getViewSize().getWidth(), vh = getViewSize().getHeight();
    int maxRows = std::max(1, int((vh - 16) / 21));
    int columns = (int(items.size()) + maxRows - 1) / maxRows,
        rows = (int(items.size()) + columns - 1) / columns;
    double width = std::min(vw - 16, std::max(132., r.getWidth()) * columns), height = 21. * rows;
    double y = r.bottom + 2;
    if (y + height > vh - 8)
        y = r.top - height - 2;
    popup_ = Popup { toolRect(std::clamp(r.left, 8., vw - 8 - width),
                         std::clamp(y, 8., vh - 8 - height), width, height),
        items, rows, columns, -1, int(selected) };
    invalid();
}
void ToolPageView::draw(CDrawContext* context)
{
    context_ = context;
    ++draws_;
    hits_.clear();
    hitClip_ = getViewSize();
    context->saveGlobalState();
    drawPage();
    context->restoreGlobalState();
    hitClip_ = getViewSize();
    drawPopup();
    context_ = nullptr;
    setDirty(false);
}
CRect ToolPageView::controlBounds(const std::string& id) const
{
    for (const auto& h : hits_)
        if (h.id == id)
            return h.bounds;
    return {};
}
CRect ToolPageView::popupItemBounds(std::size_t i) const
{
    if (!popup_ || i >= popup_->items.size())
        return {};
    auto& p = *popup_;
    double w = p.bounds.getWidth() / p.columns;
    return toolRect(
        p.bounds.left + double(i / p.rows) * w, p.bounds.top + double(i % p.rows) * 21, w, 21);
}
void ToolPageView::drawPopup()
{
    if (!popup_)
        return;
    const auto& p = *popup_;
    fill(CRect(p.bounds).inset(-2, -2), 0x080808);
    fill(p.bounds, 0x151515);
    stroke(p.bounds, 0x6c6c6c);
    for (std::size_t i = 0; i < p.items.size(); ++i) {
        auto b = popupItemBounds(i);
        int row = int(i) % p.rows, column = int(i) / p.rows;
        if (int(i) == p.hover || p.items[i].checked)
            fill(CRect(b).inset(1, 1), int(i) == p.hover ? 0x343434 : 0x292929);
        else if (row % 2)
            fill(CRect(b).inset(1, 1), 0x131313);
        if (row)
            fill(toolRect(b.left, b.top, b.getWidth(), 1), 0x3a3a3a);
        if (column)
            fill(toolRect(b.left, b.top, 1, b.getHeight()), 0x6c6c6c);
        if (int(i) == p.hover || p.items[i].checked)
            fill(toolRect(b.left + 2, b.top + 2, 3, 17), 0x7f7f7f);
        label(fit(p.items[i].title, b.getWidth() - 18),
            toolRect(b.left + 9, b.top + 4, b.getWidth() - 18, 16),
            p.items[i].enabled ? 0x929292 : 0x656565);
    }
}
bool ToolPageView::popupPointer(CPoint point, bool click)
{
    if (!popup_)
        return false;
    for (std::size_t i = 0; i < popup_->items.size(); ++i)
        if (toolContains(popupItemBounds(i), point)) {
            popup_->hover = int(i);
            if (click && popup_->items[i].enabled) {
                auto action = popup_->items[i].action;
                popup_.reset();
                if (action)
                    action();
            }
            invalid();
            return true;
        }
    if (click)
        closeMenu();
    return true;
}
void ToolPageView::focus()
{
    if (services_.requestNativeFocus)
        services_.requestNativeFocus();
    if (getFrame())
        getFrame()->setFocusView(this);
}
void ToolPageView::onMouseDownEvent(MouseDownEvent& e)
{
    if (!e.buttonState.isLeft())
        return;
    hover_ = e.mousePosition;
    if (popupPointer(hover_, true)) {
        e.consumed = true;
        return;
    }
    focus();
    for (const auto& h : hits_)
        if (toolContains(h.bounds, hover_)) {
            auto copy = h;
            if (copy.onDown)
                copy.action();
            else
                pressed_ = copy;
            e.consumed = true;
            invalid();
            return;
        }
}
void ToolPageView::onMouseMoveEvent(MouseMoveEvent& e)
{
    hover_ = e.mousePosition;
    if (popupPointer(hover_, false))
        e.consumed = true;
    invalid();
}
void ToolPageView::onMouseExitEvent(MouseExitEvent&)
{
    hover_ = { -1, -1 };
    setTooltipText(nullptr);
    if (getFrame())
        getFrame()->setCursor(kCursorDefault);
    invalid();
}
void ToolPageView::onMouseUpEvent(MouseUpEvent& e)
{
    if (!pressed_)
        return;
    auto h = *pressed_;
    pressed_.reset();
    if (toolContains(h.bounds, e.mousePosition) && h.action)
        h.action();
    e.consumed = true;
    invalid();
}
void ToolPageView::onKeyboardEvent(KeyboardEvent& e)
{
    if (e.type != EventType::KeyDown || !popup_)
        return;
    int i = popup_->hover >= 0 ? popup_->hover : popup_->selected;
    if (e.virt == VirtualKey::Escape)
        closeMenu();
    else if (e.virt == VirtualKey::Return) {
        auto item = popup_->items[std::size_t(i)];
        if (item.enabled) {
            closeMenu();
            if (item.action)
                item.action();
        }
    } else {
        int d = e.virt == VirtualKey::Down ? 1
                                           : e.virt == VirtualKey::Up
                ? -1
                : e.virt == VirtualKey::Right ? popup_->rows
                                              : e.virt == VirtualKey::Left ? -popup_->rows : 0;
        if (!d)
            return;
        popup_->hover = std::clamp(i + d, 0, int(popup_->items.size()) - 1);
    }
    e.consumed = true;
    invalid();
}
void ToolPageView::stopRefresh()
{
    pressed_.reset();
    popup_.reset();
}
void ToolPageView::resize(double w, double h)
{
    stopRefresh();
    auto r = toolRect(0, 0, w, h);
    setViewSize(r);
    setMouseableArea(r);
    invalid();
}
}
