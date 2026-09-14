#include "s3g_tracker_warp_page.h"
#include "s3g_gui_layout.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/common/generictextedit.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace f = portable_gui::foundation;
namespace {
    CRect rect(double x, double y, double w, double h) { return { x, y, x + w, y + h }; }
    CRect rect(s3g::gui_layout::Rect r) { return rect(r.x, r.y, r.width, r.height); }
    Rect logical(CRect r) { return { r.left, r.top, r.getWidth(), r.getHeight() }; }
    CColor nativeColor(Color c) { return { c.red, c.green, c.blue, c.alpha }; }
    bool contains(CRect r, CPoint p)
    {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }
    std::string number(double value, unsigned digits)
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::fixed << std::setprecision(digits) << value;
        auto text = stream.str();
        if (digits) {
            while (!text.empty() && text.back() == '0')
                text.pop_back();
            if (text.back() == '.')
                text.pop_back();
        }
        return text;
    }
    const char* fieldName(WarpField f)
    {
        constexpr const char* names[]
            = { "cycle", "primary", "pulses", "mix", "begin", "end", "repeats" };
        return names[static_cast<int>(f)];
    }
    class WarpTextEdit final : public CTextEdit {
    public:
        using CTextEdit::CTextEdit;
        std::function<bool(KeyboardEvent&)> handle;
        void takeFocus() override
        {
            if (!getFrame() || platformControl)
                return;
            platformControl = makeOwned<GenericTextEdit>(this);
            CTextLabel::takeFocus();
            invalid();
        }
        void platformOnKeyboardEvent(KeyboardEvent& event) override
        {
            if (handle && handle(event)) {
                event.consumed = true;
                return;
            }
            CTextEdit::platformOnKeyboardEvent(event);
        }
    };
}
WarpPageView::WarpPageView(
    app::TrackerViewState& state, app::WorkspaceCallbacks& callbacks, WarpPageServices services)
    : ContentView(rect(0, 0, 1320, 820))
    , services_(std::move(services))
    , editor_(state, [&callbacks] {
        if (callbacks.transportChanged)
            callbacks.transportChanged();
    })
{
    if (!services_.font)
        services_.font = [](double size) {
            auto font = f::makeUiFont(size);
            auto platform = font->getPlatformFont();
            auto ascent = platform ? platform->getAscent() : size;
            return GridFont { font->getName().getString(), size, ascent,
                platform ? std::ceil(ascent + platform->getDescent() + platform->getLeading())
                         : size * 1.3 };
        };
    setWantsFocus(true);
    reloadModel();
}
WarpPageView::~WarpPageView() { stopRefresh(); }
void WarpPageView::stopRefresh()
{
    finishText(true);
    popup_.reset();
    pressed_.reset();
    dragging_.reset();
}
void WarpPageView::reloadModel()
{
    // Project recall/undo must not commit a stale edit into the new stack. Local
    // text commits remove their editor before publishing transportChanged.
    finishText(true);
    editor_.reconcile();
    name_ = editor_.name();
    for (int i = 0; i < 7; ++i) {
        auto field = editor_.field(static_cast<WarpField>(i));
        if (field.enabled)
            fields_[i] = field;
        else {
            fields_[i].enabled = false;
            fields_[i].visible = field.visible;
        }
    }
    if (const auto* t = editor_.state.session.transport.timingWarp.transform(editor_.selected))
        kind_ = t->kind;
    layoutAndDraw(nullptr);
    invalid();
}
void WarpPageView::refreshPlaybackDisplay(bool visible)
{
    if (closeText_)
        finishText(cancelText_, saveText_);
    // No private FPS clock, scheduler, or extrapolation. This is the snapshot
    // already held back by Tracker's shared presentation coordinator.
    if (visible)
        invalid();
}
void WarpPageView::resize(double width, double height)
{
    finishText();
    popup_.reset();
    pressed_.reset();
    dragging_.reset();
    auto r = rect(0, 0, width, height);
    setViewSize(r);
    setMouseableArea(r);
    // Hit testing must use the new layout immediately, even if a detached
    // window receives input before Quartz delivers its first paint.
    layoutAndDraw(nullptr);
    invalid();
}
Color WarpPageView::color(uint32_t rgb, double alpha) const
{
    return services_.color ? services_.color(rgb, alpha)
                           : Color { uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb),
                                 uint8_t(std::lround(alpha * 255)) };
}
void WarpPageView::fill(CRect r, uint32_t rgb, double alpha)
{
    if (!context_) return;
    DisplayList list;
    list.shape(Primitive::FillRect, logical(r), color(rgb, alpha));
    drawDisplayList(*context_, list, services_.fontFactory);
}
void WarpPageView::stroke(CRect r, uint32_t rgb)
{
    if (!context_) return;
    DisplayList list;
    list.shape(Primitive::StrokeRect, logical(r), color(rgb));
    drawDisplayList(*context_, list, services_.fontFactory);
}
void WarpPageView::label(std::string text, CRect r, uint32_t rgb, Alignment alignment, double size)
{
    if (!context_) return;
    const auto font = services_.font(size);
    DisplayList list;
    list.text(std::move(text), logical(r), color(rgb), font.name, font.size, r.top + font.baseline,
        alignment);
    drawDisplayList(*context_, list, services_.fontFactory);
}
void WarpPageView::panel(CRect r, std::string title)
{
    // NSFrameRect in the original renderer uses the fill color, so these
    // toolboxes deliberately do not acquire a new contrasting outline.
    fill(r, 0x1d1d1d);
    fill(rect(r.left, r.top, r.getWidth(), 21), 0x131313);
    fill(rect(r.left, r.top, r.getWidth(), 2), 0xb8b8b8);
    label(title, rect(r.left + 8, r.top + 5, r.getWidth() - 16, 16));
}
void WarpPageView::button(CRect r, std::string id, std::string title, std::function<void()> action,
    bool enabled, bool live)
{
    bool pressed = pressed_ && pressed_->id == id && contains(r, hover_);
    fill(CRect(r).inset(.5, .5),
        pressed ? 0x414141 : live ? 0x7fd7e8 : enabled && contains(r, hover_) ? 0x343434 : 0x292929,
        live && !pressed ? .16 : 1);
    if (live && !pressed) {
        // Native NSFrameRect paints the translucent fill a second time at the
        // inside edge. Preserve that subtle live-state edge, not a bright stroke.
        auto b = CRect(r).inset(.5, .5);
        fill(rect(b.left, b.top, b.getWidth(), 1), 0x7fd7e8, .16);
        fill(rect(b.left, b.bottom - 1, b.getWidth(), 1), 0x7fd7e8, .16);
        fill(rect(b.left, b.top + 1, 1, b.getHeight() - 2), 0x7fd7e8, .16);
        fill(rect(b.right - 1, b.top + 1, 1, b.getHeight() - 2), 0x7fd7e8, .16);
    }
    auto font = services_.font(10);
    auto face = services_.fontFactory ? services_.fontFactory(font.name, font.size)
                                     : f::makeUiFont(font.size);
    auto platform = face->getPlatformFont();
    const double capHeight = platform && platform->getCapHeight() > 0
        ? platform->getCapHeight() : font.size * .72;
    DisplayList titleList;
    titleList.text(title, logical(r), color(!enabled ? 0x656565 : live ? 0x7fd7e8 : 0x929292),
        font.name, font.size, centeredCapsBaseline(logical(r), capHeight), Alignment::Center);
    if (context_) drawDisplayList(*context_, titleList, services_.fontFactory);
    if (enabled)
        hits_.push_back({ r, std::move(id), HitKind::Button, std::move(action) });
}
std::string WarpPageView::fit(std::string text, double width)
{
    if (!context_) return text;
    for (char& c : text)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    auto info = services_.font(10);
    auto font = services_.fontFactory ? services_.fontFactory(info.name, info.size)
                                      : f::makeUiFont(info.size);
    context_->setFont(font);
    if (context_->getStringWidth(text.c_str()) <= width)
        return text;
    while (!text.empty() && context_->getStringWidth((text + "…").c_str()) > width) {
        auto index = text.size() - 1;
        while (index && (static_cast<unsigned char>(text[index]) & 0xc0) == 0x80)
            --index;
        text.erase(index);
    }
    return text + "…";
}
void WarpPageView::menu(CRect r, std::string id, std::vector<std::string> items,
    std::size_t selected, std::function<void(std::size_t)> select, bool enabled)
{
    fill(r, 0x131313);
    fill(rect(r.left + 1, r.top + 1, 2, 13), enabled ? 0x7f7f7f : 0x303030);
    label(fit(items.at(std::min(selected, items.size() - 1)), r.getWidth() - 28),
        rect(r.left + 8, r.top + 2, r.getWidth() - 28, 20), enabled ? 0x929292 : 0x303030);
    label("v", rect(r.right - 12, r.top + 1, 10, 20), enabled ? 0x929292 : 0x303030);
    if (enabled)
        hits_.push_back({ r, std::move(id), HitKind::Menu,
            [this, r, items = std::move(items), selected, select = std::move(select)] {
                int maxRows = std::max(1, std::min(38, int((getHeight() - 16) / 21)));
                int columns = (int(items.size()) + maxRows - 1) / maxRows;
                int rows = (int(items.size()) + columns - 1) / columns;
                double width = std::min(getWidth() - 16, std::max(132., r.getWidth()) * columns),
                       height = 21. * rows;
                double y = r.bottom + 2;
                if (y + height > getHeight() - 8)
                    y = r.top - height - 2;
                popup_ = Popup { rect(std::clamp(r.left, 8., getWidth() - 8 - width),
                                     std::clamp(y, 8., getHeight() - 8 - height), width, height),
                    items, select, -1, int(selected), rows, columns };
                invalid();
            } });
}
void WarpPageView::slider(CRect r, WarpField field)
{
    auto spec = fields_[static_cast<int>(field)];
    if (!spec.visible)
        return;
    auto trackWidth = std::min(150., r.getWidth() - 50);
    auto track = rect(r.left, r.top + 9, trackWidth, 9);
    double normalized = std::clamp(
        (spec.value - spec.minimum) / std::max(.00001, spec.maximum - spec.minimum), 0., 1.);
    fill(track, 0x131313);
    fill(rect(track.left + 1, track.top + 1, std::max(1., (trackWidth - 2) * normalized), 7),
        spec.enabled ? 0x7f7f7f : 0x333333);
    fill(rect(track.left + std::clamp(trackWidth * normalized - 1.5, 1., trackWidth - 4),
             track.top - 2, 3, 13),
        spec.enabled ? 0xc9c9c9 : 0x656565);
    label(number(spec.value, spec.digits), rect(r.right - 42, r.top + 6, 42, 15),
        spec.enabled ? 0x929292 : 0x656565, Alignment::Right);
    if (spec.enabled)
        hits_.push_back({ r, fieldName(field), HitKind::Slider, {}, field });
}
void WarpPageView::draw(CDrawContext* context)
{
    layoutAndDraw(context);
}
void WarpPageView::layoutAndDraw(CDrawContext* context)
{
    context_ = context;
    if (context) ++draws_;
    hits_.clear();
    fill(getViewSize(), 0x0a0a0a);
    const auto family = s3g::gui_layout::trackerWarpFamilyLayout({ getWidth(), getHeight() });
    auto field = rect(family.fieldPanel), library = rect(family.library.frame),
         stack = rect(family.stack.frame), transform = rect(family.transform.frame);
    panel(field, "WARP FUNCTION  /  INPUT → WARPED PHASE");
    panel(library, "WARP LIBRARY");
    panel(stack, "SERIAL STACK");
    panel(transform, "SELECTED TRANSFORM");
    if (context) drawDisplayList(*context,
        paintWarpCurve(editor_.state,
            { field.left + 1, field.top + 21, field.getWidth() - 2, field.getHeight() - 22 },
            services_.font(8.5), services_.color),
        services_.fontFactory);
    auto control = [](CRect p, int row, bool slider = false) {
        return rect(p.left + 108, p.top + 36 + row * 26 - (slider ? 8 : 1),
            std::max(20., p.getWidth() - 124), slider ? 24 : 15);
    };
    auto rows = [&](CRect panel, std::vector<std::string> labels) {
        auto font = services_.font(10);
        for (std::size_t i = 0; i < labels.size(); ++i)
            label(labels[i],
                rect(panel.left + 16,
                    panel.top + 35 + double(i) * 26 + std::floor((15 - font.lineHeight) * .5), 86,
                    font.lineHeight));
    };
    rows(library, { "SLOT", "NAME", "SLOT EDIT" });
    menu(control(library, 0), "slot", editor_.slots(), editor_.slot,
        [this](std::size_t i) { result(editor_.selectSlot(i)); });
    auto name = control(library, 1, true);
    fill(name, 0x262626);
    stroke(CRect(name).inset(.5, .5), 0x4c4c4c);
    label(name_, rect(name.left + 3, name.top + 5, name.getWidth() - 6, 18), 0xdededa);
    hits_.push_back({ name, "name", HitKind::Name, {} });
    button(rect(library.right - 82, library.top + 3, 70, 15), "save", "SAVE",
        [this] { result(editor_.save(name_)); });
    button(
        control(library, 2), "delete", "DELETE SLOT", [this] { result(editor_.erase()); },
        editor_.state.session.warpLibrary.entry(editor_.slot));
    rows(stack, { "MODE", "CYCLE", "TRANSFORM", "ADD", "EDIT" });
    bool enabled = editor_.state.session.transport.timingWarpEnabled;
    button(
        control(stack, 0), "mode", enabled ? "WARP PLAYBACK: ON" : "WARP PLAYBACK: OFF",
        [this] {
            editor_.toggle();
            reloadModel();
        },
        true, enabled);
    slider(control(stack, 1, true), WarpField::Cycle);
    menu(control(stack, 2), "transform", editor_.transforms(), editor_.selected,
        [this](std::size_t i) {
            editor_.selected = i;
            reloadModel();
        });
    auto add = control(stack, 3);
    double w = (add.getWidth() - 8) / 3;
    for (int i = 0; i < 3; ++i)
        button(rect(add.left + i * (w + 4), add.top, w, 15), "add" + std::to_string(i),
            i == 0 ? "+ EXP" : i == 1 ? "+ STEP" : "+ EUCLID",
            [this, i] { result(editor_.add(static_cast<TimingWarpKind>(i))); });
    auto edit = control(stack, 4);
    w = (edit.getWidth() - 4) / 2;
    button(
        rect(edit.left, edit.top, w, 15), "remove", "REMOVE", [this] { result(editor_.remove()); });
    button(rect(edit.left + w + 4, edit.top, w, 15), "clear", "CLEAR", [this] {
        editor_.clear();
        reloadModel();
    });
    bool has = !editor_.state.session.transport.timingWarp.empty();
    rows(transform,
        { "TYPE", !has || kind_ == TimingWarpKind::Exponential ? "POWER" : "STEPS",
            fields_[2].visible ? "PULSES" : "", "MIX", "SEGMENT START", "SEGMENT END", "REPEAT" });
    menu(
        control(transform, 0), "type", { "EXPONENTIAL", "STEP QUANTIZE", "EUCLIDEAN QUANTIZE" },
        static_cast<std::size_t>(kind_),
        [this](std::size_t i) { result(editor_.setKind(static_cast<TimingWarpKind>(i))); }, has);
    for (int i = 1; i < 7; ++i)
        slider(control(transform, i, true), static_cast<WarpField>(i));
    if (!context) return;
    // Native status is a two-line, word-wrapped NSTextField; wrap at actual font
    // advances and clip after two lines rather than inventing smaller type.
    auto status = editor_.status();
    auto statusRect = rect(transform.left + 16,
        transform.top + std::max(220., transform.getHeight() - 43), transform.getWidth() - 32, 35);
    auto statusFont = services_.font(8.5);
    auto font = services_.fontFactory ? services_.fontFactory(statusFont.name, statusFont.size)
                                      : f::makeUiFont(8.5);
    context_->setFont(font);
    for (int line = 0; line < 2 && !status.empty(); ++line) {
        auto end = status.find('\n');
        if (end == std::string::npos)
            end = status.size();
        while (end
            && context_->getStringWidth(status.substr(0, end).c_str()) > statusRect.getWidth()) {
            auto space = status.rfind(' ', end - 1);
            if (space == std::string::npos)
                break;
            end = space;
        }
        label(status.substr(0, end),
            rect(statusRect.left, statusRect.top + line * statusFont.lineHeight,
                statusRect.getWidth(), statusFont.lineHeight),
            0x878787, Alignment::Left, 8.5);
        status.erase(0, std::min(status.size(), end + 1));
        context_->setFont(font);
    }
    drawPopup();
    context_ = nullptr;
    setDirty(false);
}
CRect WarpPageView::controlBounds(const std::string& id) const
{
    for (const auto& hit : hits_)
        if (hit.id == id)
            return hit.bounds;
    return {};
}
CRect WarpPageView::popupItemBounds(std::size_t index) const
{
    if (!popup_ || index >= popup_->items.size())
        return {};
    const auto& p = *popup_;
    auto width = p.bounds.getWidth() / p.columns;
    return rect(p.bounds.left + double(index / p.rows) * width,
        p.bounds.top + double(index % p.rows) * 21, width, 21);
}
void WarpPageView::drawPopup()
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
        if (int(i) == p.hover || int(i) == p.selected)
            fill(CRect(b).inset(1, 1), int(i) == p.hover ? 0x343434 : 0x292929);
        else if (row % 2)
            fill(CRect(b).inset(1, 1), 0x131313);
        if (row)
            fill(rect(b.left, b.top, b.getWidth(), 1), 0x3a3a3a);
        if (column)
            fill(rect(b.left, b.top, 1, b.getHeight()), 0x6c6c6c);
        if (int(i) == p.hover || int(i) == p.selected)
            fill(rect(b.left + 2, b.top + 2, 3, 17), 0x7f7f7f);
        label(fit(p.items[i], b.getWidth() - 18),
            rect(b.left + 9, b.top + 4, b.getWidth() - 18, 16), 0x929292);
    }
}
bool WarpPageView::popupPointer(CPoint point, bool click)
{
    if (!popup_)
        return false;
    for (std::size_t i = 0; i < popup_->items.size(); ++i)
        if (contains(popupItemBounds(i), point)) {
            popup_->hover = int(i);
            if (click) {
                auto select = popup_->select;
                popup_.reset();
                select(i);
            }
            invalid();
            return true;
        }
    if (click) {
        popup_.reset();
        invalid();
    }
    return true;
}
void WarpPageView::focus()
{
    if (services_.requestNativeFocus)
        services_.requestNativeFocus();
    if (getFrame())
        getFrame()->setFocusView(this);
}
void WarpPageView::result(bool ok)
{
    if (!ok && services_.error)
        services_.error();
    reloadModel();
}
void WarpPageView::onMouseDownEvent(MouseDownEvent& event)
{
    if (!event.buttonState.isLeft())
        return;
    hover_ = event.mousePosition;
    if (popupPointer(hover_, true)) {
        event.consumed = true;
        return;
    }
    finishText();
    focus();
    for (const auto& source : hits_)
        if (contains(source.bounds, hover_)) {
            auto hit = source; // model publications can synchronously redraw
            event.consumed = true;
            switch (hit.kind) {
            case HitKind::Name:
                startText(hit.bounds, {});
                break;
            case HitKind::Slider:
                if (event.clickCount >= 2)
                    startText(hit.bounds, hit.field);
                else if (hover_.x <= hit.bounds.left + std::min(150., hit.bounds.getWidth() - 50)) {
                    dragging_ = hit;
                    drag(hover_);
                }
                break;
            case HitKind::Menu:
                hit.action();
                break;
            case HitKind::Button:
                pressed_ = hit;
                invalid();
                break;
            }
            return;
        }
}
void WarpPageView::drag(CPoint point)
{
    if (!dragging_)
        return;
    auto spec = editor_.field(dragging_->field);
    double width = std::min(150., dragging_->bounds.getWidth() - 50);
    double n = std::clamp((point.x - dragging_->bounds.left) / width, 0., 1.);
    double factor = std::pow(10., spec.digits);
    double value = std::round((spec.minimum + n * (spec.maximum - spec.minimum)) * factor) / factor;
    if (value != spec.value)
        result(editor_.set(dragging_->field, value));
}
void WarpPageView::onMouseMoveEvent(MouseMoveEvent& event)
{
    hover_ = event.mousePosition;
    CCursorType cursor = kCursorDefault;
    std::string tip;
    if (!popup_)
        for (const auto& hit : hits_)
            if (contains(hit.bounds, hover_)) {
                if (hit.kind == HitKind::Slider) {
                    cursor
                        = hover_.x <= hit.bounds.left + std::min(150., hit.bounds.getWidth() - 50)
                        ? kCursorHSize
                        : kCursorDefault;
                    tip = "Drag the track, or double-click to enter an exact value";
                } else if (hit.kind == HitKind::Name) {
                    cursor = kCursorIBeam;
                    tip = "Enter a name, then press Return or SAVE";
                } else if (hit.id == "save")
                    tip = "Save the current warp stack and NAME to this slot";
                else if (hit.id == "delete")
                    tip = "Delete this saved Warp without changing the current stack";
                break;
            }
    if (getFrame())
        getFrame()->setCursor(cursor);
    setTooltipText(tip.empty() ? nullptr : tip.c_str());
    if (dragging_)
        drag(hover_);
    else
        popupPointer(hover_, false);
    invalid();
    event.consumed = true;
}
void WarpPageView::onMouseExitEvent(MouseExitEvent&)
{
    hover_ = { -1, -1 };
    if (getFrame())
        getFrame()->setCursor(kCursorDefault);
    setTooltipText(nullptr);
    invalid();
}
void WarpPageView::onMouseUpEvent(MouseUpEvent& event)
{
    if (dragging_) {
        drag(event.mousePosition);
        dragging_.reset();
        event.consumed = true;
    }
    if (pressed_) {
        auto hit = *pressed_;
        pressed_.reset();
        if (contains(hit.bounds, event.mousePosition) && hit.action)
            hit.action();
        event.consumed = true;
        invalid();
    }
}
void WarpPageView::onKeyboardEvent(KeyboardEvent& event)
{
    if (event.type != EventType::KeyDown || !popup_)
        return;
    int i = popup_->hover >= 0 ? popup_->hover : popup_->selected;
    if (event.virt == VirtualKey::Escape)
        popup_.reset();
    else if (event.virt == VirtualKey::Return) {
        auto select = popup_->select;
        popup_.reset();
        select(std::size_t(i));
    } else {
        int delta = event.virt == VirtualKey::Down
            ? 1
            : event.virt == VirtualKey::Up ? -1
                                           : event.virt == VirtualKey::Right
                    ? popup_->rows
                    : event.virt == VirtualKey::Left ? -popup_->rows : 0;
        if (!delta)
            return;
        popup_->hover = std::clamp(i + delta, 0, int(popup_->items.size()) - 1);
    }
    event.consumed = true;
    invalid();
}
void WarpPageView::startText(CRect bounds, std::optional<WarpField> field)
{
    if (!getFrame())
        return;
    textField_ = field;
    if (field)
        bounds = rect(bounds.right - 58, bounds.top + 3, 58, 20);
    std::ostringstream exact;
    exact.imbue(std::locale::classic());
    if (field)
        exact << std::setprecision(std::numeric_limits<double>::max_digits10)
              << editor_.field(*field).value;
    auto* text = new WarpTextEdit(bounds, this, 0, field ? exact.str().c_str() : name_.c_str());
    text->handle = [this](KeyboardEvent& event) {
        if (event.type != EventType::KeyDown)
            return false;
        if (event.virt != VirtualKey::Return && event.virt != VirtualKey::Escape
            && event.virt != VirtualKey::Tab)
            return false;
        closeText_ = true;
        cancelText_ = event.virt == VirtualKey::Escape;
        saveText_ = event.virt == VirtualKey::Return && !textField_;
        return true;
    };
    auto info = services_.font(10);
    text->setFont(
        services_.fontFactory ? services_.fontFactory(info.name, info.size) : f::makeUiFont(10));
    text->setFontColor(nativeColor(color(0xdededa)));
    text->setBackColor(nativeColor(color(0x262626)));
    text->setFrameColor(nativeColor(color(0x4c4c4c)));
    text->setHoriAlign(field ? kRightText : kLeftText);
    text->setTextInset({ 3, 0 });
    text->registerTextEditListener(this);
    text_ = text;
    getFrame()->addView(text);
    getFrame()->setFocusView(text);
    invalid();
}
void WarpPageView::onTextEditPlatformControlLostFocus(CTextEdit*) { closeText_ = true; }
void WarpPageView::finishText(bool cancel, bool save)
{
    if (!text_)
        return;
    auto* text = text_;
    auto field = textField_;
    text_ = nullptr;
    text->unregisterTextEditListener(this);
    // GenericTextEdit commits its buffer on focus loss. Keep the control alive
    // until that callback has completed, and never delete it inside a key event.
    if (getFrame() && getFrame()->getFocusView() == text)
        getFrame()->setFocusView(this);
    std::string value = text->getText().getString();
    if (getFrame())
        getFrame()->removeView(text, true);
    closeText_ = cancelText_ = saveText_ = false;
    textField_.reset();
    if (!cancel) {
        if (field) {
            std::istringstream stream(value);
            stream.imbue(std::locale::classic());
            double numeric = 0;
            stream >> numeric;
            bool valid = !stream.fail();
            stream >> std::ws;
            result(valid && stream.eof() && editor_.set(*field, numeric));
        } else {
            name_ = value;
            if (save)
                result(editor_.save(name_));
        }
    }
    invalid();
}
}
