#include "s3g_tracker_reference_page.h"
#include "s3g/tracker/editor_palette.h"
#include "s3g_gui_layout.h"
#include "vstgui/lib/cclipboard.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/common/generictextedit.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include <cmath>
#include <map>

namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace f = portable_gui::foundation;
namespace {
    class ReferenceInput final : public CTextEdit {
    public:
        using CTextEdit::CTextEdit;
        std::function<bool(KeyboardEvent&)> handle;
        double baselineOffset = 0;
        CRect platformGetSize() const override
        {
            auto r = CTextEdit::platformGetSize();
            // GenericTextEdit centers on capitals. The native Console field
            // uses a top-inset baseline; retain it while entering/editing text.
            r.offset(0, baselineOffset * getGlobalTransform().m22);
            return r;
        }
        void endCaret()
        {
            if (!getFrame())
                return;
            auto move = [self = SharedPointer<ReferenceInput>(this)] {
                auto* frame = self->getFrame();
                if (!frame || frame->getFocusView() != self.get())
                    return;
                KeyboardEvent e(EventType::KeyDown);
#if defined(__APPLE__)
                e.virt = VirtualKey::Right;
                e.modifiers = Modifiers { ModifierKey::Control };
#else
                e.virt = VirtualKey::End;
#endif
                static_cast<IPlatformFrameCallback*>(frame)->platformOnEvent(e);
            };
            if (!getFrame()->doAfterEventProcessing(move))
                move();
        }
        void takeFocus() override
        {
            if (!getFrame() || platformControl)
                return;
            platformControl = makeOwned<GenericTextEdit>(this);
            CTextLabel::takeFocus();
            endCaret();
            invalid();
        }
        void platformOnKeyboardEvent(KeyboardEvent& e) override
        {
            if (handle && handle(e)) {
                e.consumed = true;
                return;
            }
            CTextEdit::platformOnKeyboardEvent(e);
        }
    };
}
ReferencePageView::ReferencePageView(
    std::shared_ptr<ConsoleModel> model, ReferencePageServices services)
    : ToolPageView(services.tools)
    , console_(std::move(model))
    , referenceServices_(std::move(services))
{
    if (!console_)
        help_ = trackerHelpDocument();
    if (!referenceServices_.font)
        referenceServices_.font = [](const ReferenceStyle& s) {
            double size = std::max(8., std::round(s.size * 1.16 * 2) / 2);
            auto font = f::makeUiFont(size);
            auto platform = font->getPlatformFont();
            double ascent = platform ? platform->getAscent() : size;
            return GridFont { font->getName().getString(), size, ascent,
                platform ? std::ceil(ascent + platform->getDescent() + platform->getLeading())
                         : size * 1.3 };
        };
}
ReferencePageView::~ReferencePageView() { stopRefresh(); }
CRect ReferencePageView::inputBounds() const
{
    return console_ && !findOpen_ ? toolRect(44, 37, getViewSize().getWidth() - 72, 25)
                                  : toolRect(78, 27, getViewSize().getWidth() - 274, 25);
}
CRect ReferencePageView::textViewport() const
{
    double top = findOpen_ ? 60 : console_ ? 69 : 31;
    return toolRect(26, top, getViewSize().getWidth() - 52, getViewSize().getHeight() - 26 - top);
}
void ReferencePageView::resize(double width, double height)
{
    ToolPageView::resize(width, height);
    layoutWidth_ = -1;
}
void ReferencePageView::scrollTo(double y)
{
    auto vp = textViewport();
    double inset = console_ ? 3 : 14;
    scrollY_ = std::clamp(y, 0., std::max(0., layout_.height + inset * 2 - vp.getHeight()));
    invalid();
}
void ReferencePageView::layoutText()
{
    auto vp = textViewport();
    double inset = console_ ? 10 : 23, width = std::max(1., vp.getWidth() - inset * 2);
    auto revision = console_ ? console_->revision : 0;
    if (layoutWidth_ == width && revision_ == revision)
        return;
    bool changed = revision_ != revision;
    revision_ = revision;
    layoutWidth_ = width;
    std::map<std::pair<std::string, double>, SharedPointer<CFontDesc>> fonts;
    std::map<std::tuple<std::string, double, std::string>, double> widths;
    std::map<std::pair<double, FontWeight>, GridFont> metricCache;
    auto metrics = [&](const ReferenceStyle& style) {
        auto key = std::make_pair(style.size, style.weight);
        auto found = metricCache.find(key);
        if (found != metricCache.end())
            return found->second;
        return metricCache.emplace(key, referenceServices_.font(style)).first->second;
    };
    layout_.build(
        console_ ? console_->output : help_, width, metrics,
        [&](std::string_view text, const ReferenceStyle& style) {
            auto info = metrics(style);
            auto key = std::make_tuple(info.name, info.size, std::string(text));
            if (auto found = widths.find(key); found != widths.end())
                return found->second;
            auto& face = fonts[{ info.name, info.size }];
            if (!face)
                face = services_.fontFactory ? services_.fontFactory(info.name, info.size)
                                             : f::makeUiFont(info.size);
            context_->setFont(face);
            double width = context_->getStringWidth(std::string(text).c_str());
            return widths.emplace(key, width).first->second;
        },
        referenceServices_.fallbackFont);
    if (changed && console_) {
        anchor_ = caret_ = std::min(caret_, layout_.text.size());
        followEnd_ = true;
    }
    if (followEnd_) {
        scrollTo(layout_.height);
        followEnd_ = false;
    } else
        scrollTo(scrollY_);
}
void ReferencePageView::paintText()
{
    auto vp = textViewport();
    fill(vp, console_ ? 0x060606 : nightNeutral(0x12));
    CRect previous;
    context_->getClipRect(previous);
    auto clip = vp;
    clip.bound(previous);
    context_->setClipRect(clip);
    double left = vp.left + (console_ ? 10 : 23), top = vp.top + (console_ ? 3 : 14) - scrollY_;
    auto a = std::min(anchor_, caret_), b = std::max(anchor_, caret_);
    DisplayList list;
    for (const auto& line : layout_.lines) {
        if (top + line.y + line.height < vp.top)
            continue;
        if (top + line.y > vp.bottom)
            break;
        for (std::size_t i = line.first; i < line.last; ++i) {
            const auto& g = layout_.glyphs[i];
            bool selected = g.offset >= a && g.offset < b;
            if (selected)
                fill(toolRect(left + g.x, top + g.y,
                         std::max(g.width, g.character == U'\n' ? 3. : 0.), g.height),
                    nightNeutral(0x4a));
        }
        for (std::size_t i = line.first; i < line.last;) {
            const auto& g = layout_.glyphs[i];
            auto& style = layout_.styles[g.style];
            auto& font = layout_.fonts[g.style];
            bool selected = g.offset >= a && g.offset < b;
            std::u32string run;
            std::size_t end = i;
            do {
                const auto& next = layout_.glyphs[end];
                if (next.character != U'\n')
                    run += next.character;
                ++end;
            } while (style.tracking == 0 && end < line.last && layout_.glyphs[end].style == g.style
                && (layout_.glyphs[end].offset >= a && layout_.glyphs[end].offset < b) == selected);
            if (!run.empty())
                list.text(referenceUtf8(run),
                    { left + g.x, clip.top, std::max(1., clip.right - left - g.x),
                        clip.getHeight() },
                    color(selected ? nightNeutral(0xf2) : style.rgb), font.name, font.size,
                    top + g.baseline, Alignment::Left);
            i = end;
        }
    }
    drawDisplayList(*context_, list, services_.fontFactory);
    context_->setClipRect(previous);
    double total = layout_.height + (console_ ? 6 : 28);
    if (total > vp.getHeight()) {
        fill(toolRect(vp.right - 9, vp.top, 9, vp.getHeight()), 0x131313, .8);
        double h = std::max(24., vp.getHeight() * vp.getHeight() / total),
               y = vp.top + scrollY_ / (total - vp.getHeight()) * (vp.getHeight() - h);
        fill(toolRect(vp.right - 7, y, 5, h), 0x656565);
    }
}
void ReferencePageView::drawPage()
{
    fill(getViewSize(), 0x060606);
    panel(toolRect(18, 2, getViewSize().getWidth() - 36, getViewSize().getHeight() - 20),
        console_ ? "CONSOLE / LIVE CODE" : "HELP / COMMAND REFERENCE");
    if (console_ || findOpen_) {
        auto r = inputBounds();
        auto font = referenceServices_.font({ 10 });
        fill(r, 0x262626);
        stroke(r, 0x4c4c4c);
        std::string text = findOpen_ ? findQuery_ : console_->draft;
        if (!input_) {
            bool placeholder = text.empty();
            if (placeholder)
                text = findOpen_ ? "Find in reference"
                                 : "Live Code remains available when this Console is detached";
            DisplayList list;
            list.text(text, { r.left + 2, r.top, r.getWidth() - 4, r.getHeight() },
                color(placeholder ? 0x878787 : 0xdededa), font.name, font.size,
                r.top + font.baseline, Alignment::Left);
            drawDisplayList(*context_, list, services_.fontFactory);
        }
        if (findOpen_) {
            label("FIND", toolRect(30, 33, 44, 18));
            button(toolRect(getViewSize().getWidth() - 190, 28, 45, 24), "previous", "↑",
                [this] { find(findQuery_, true); });
            button(toolRect(getViewSize().getWidth() - 141, 28, 45, 24), "next", "↓",
                [this] { find(findQuery_); });
            button(
                toolRect(getViewSize().getWidth() - 92, 28, 64, 24), "close-find", "CLOSE", [this] {
                    finishInput();
                    findOpen_ = false;
                    invalid();
                });
        } else {
            auto prompt = referenceServices_.font({ 12 });
            DisplayList list;
            list.text(":", { 30, r.top, 12, 25 }, color(nightNeutral(0xb8)), prompt.name,
                prompt.size, r.top + (25 - prompt.lineHeight) * .5 + prompt.baseline,
                Alignment::Left);
            drawDisplayList(*context_, list, services_.fontFactory);
        }
    }
    if (console_)
        displayedDraft_ = console_->draft;
    layoutText();
    paintText();
}
void ReferencePageView::openInput(bool findMode)
{
    finishInput();
    findOpen_ = findMode;
    if (!findMode && !console_)
        return;
    if (!getFrame())
        return;
    focus();
    historyIndex_ = console_ ? console_->history.size() : 0;
    auto* field = new ReferenceInput(
        inputBounds(), this, 1, (findMode ? findQuery_ : console_->draft).c_str());
    input_ = field;
    auto font = referenceServices_.font({ 10 });
    field->setFont(services_.fontFactory ? services_.fontFactory(font.name, font.size)
                                         : f::makeUiFont(font.size));
    auto converted = [this](uint32_t rgb) {
        auto c = color(rgb);
        return CColor { c.red, c.green, c.blue, c.alpha };
    };
    field->setFontColor(converted(0xdededa));
    field->setBackColor(converted(0x262626));
    field->setFrameColor(converted(0x4c4c4c));
    auto platformFont = field->getFont()->getPlatformFont();
    double cap = platformFont && platformFont->getCapHeight() > 0 ? platformFont->getCapHeight()
                                                                  : font.size;
    field->baselineOffset = font.baseline - (inputBounds().getHeight() + cap) * .5;
    field->setTextInset({ 2, 0 });
    field->setHoriAlign(kLeftText);
    field->setImmediateTextChange(true);
    field->registerTextEditListener(this);
    field->handle = [this](KeyboardEvent& e) { return handleInputKey(e); };
    getFrame()->addView(field);
    getFrame()->setFocusView(field);
    invalid();
}
void ReferencePageView::focusInput()
{
    if (console_)
        openInput(false);
    else
        focus();
}
void ReferencePageView::finishInput()
{
    closeInput_ = false;
    if (!input_)
        return;
    auto* field = input_;
    input_ = nullptr;
    if (findOpen_)
        findQuery_ = field->getText().getString();
    else if (console_)
        console_->draft = field->getText().getString();
    field->unregisterTextEditListener(this);
    static_cast<ReferenceInput*>(field)->handle = {};
    if (auto* frame = field->getFrame()) {
        if (frame->getFocusView() == field)
            frame->setFocusView(this);
        frame->removeView(field);
    }
}
void ReferencePageView::stopRefresh()
{
    finishInput();
    closeFind_ = returnFocus_ = false;
    autoscroll_ = nullptr;
    selecting_ = scrollbar_ = false;
    ToolPageView::stopRefresh();
}
void ReferencePageView::refresh()
{
    if (closeInput_) {
        finishInput();
        if (closeFind_) {
            closeFind_ = false;
            findOpen_ = false;
            invalid();
        }
        if (returnFocus_) {
            returnFocus_ = false;
            if (referenceServices_.returnToTracker)
                referenceServices_.returnToTracker();
        }
    }
    if (console_) {
        if (revision_ != console_->revision || displayedDraft_ != console_->draft)
            invalid();
        if (input_ && !findOpen_ && std::string(input_->getText().getString()) != console_->draft)
            updateInput(console_->draft);
    }
}
void ReferencePageView::updateInput(const std::string& text)
{
    if (findOpen_)
        findQuery_ = text;
    else if (console_)
        console_->draft = text;
    if (input_) {
        input_->setText(text.c_str());
        static_cast<ReferenceInput*>(input_)->endCaret();
    }
    invalid();
}
void ReferencePageView::valueChanged(CControl*)
{
    if (input_) {
        if (findOpen_)
            findQuery_ = input_->getText().getString();
        else if (console_)
            console_->draft = input_->getText().getString();
    }
}
void ReferencePageView::onTextEditPlatformControlLostFocus(CTextEdit*)
{
    closeInput_ = true;
    // Help has no playback refresh timer. Defer removal until the text editor
    // has unwound its focus callback, including when hosted in its own window.
    if (getFrame())
        getFrame()->doAfterEventProcessing(
            [self = SharedPointer<ReferencePageView>(this)] { self->refresh(); });
}
bool ReferencePageView::handleInputKey(KeyboardEvent& e)
{
    if (e.type != EventType::KeyDown || !input_)
        return false;
    if (e.virt == VirtualKey::Escape) {
        closeInput_ = true;
        if (findOpen_)
            closeFind_ = true;
        else
            returnFocus_ = true;
        getFrame()->doAfterEventProcessing(
            [self = SharedPointer<ReferencePageView>(this)] { self->refresh(); });
        invalid();
        return true;
    }
    if (e.virt == VirtualKey::Return) {
        if (findOpen_)
            find(input_->getText().getString(), e.modifiers.has(ModifierKey::Shift));
        else {
            auto text = std::string(input_->getText().getString());
            console_->submit(text);
            updateInput(console_->draft);
            historyIndex_ = console_->history.size();
            historyDraft_.clear();
        }
        return true;
    }
    if (findOpen_ && e.modifiers.has(ModifierKey::Control)
        && (e.character == 'g' || e.character == 'G')) {
        find(input_->getText().getString(), e.modifiers.has(ModifierKey::Shift));
        return true;
    }
    if (findOpen_)
        return false;
    if (e.virt == VirtualKey::Up || e.virt == VirtualKey::Down) {
        if (historyIndex_ == console_->history.size())
            historyDraft_ = input_->getText().getString();
        if (e.virt == VirtualKey::Up && historyIndex_)
            --historyIndex_;
        else if (e.virt == VirtualKey::Down)
            historyIndex_ = std::min(historyIndex_ + 1, console_->history.size());
        updateInput(historyIndex_ == console_->history.size() ? historyDraft_
                                                              : console_->history[historyIndex_]);
        return true;
    }
    if (e.virt == VirtualKey::Tab) {
        auto matches = consoleCompletions(input_->getText().getString());
        if (matches.size() == 1)
            updateInput(matches.front() + " ");
        else if (matches.size() > 1) {
            std::string text = "matches: ";
            for (auto& m : matches) {
                if (text.size() > 9)
                    text += ", ";
                text += m;
            }
            console_->append(text);
        }
        return true;
    }
    return false;
}
std::size_t ReferencePageView::hitText(CPoint p) const
{
    auto vp = textViewport();
    return layout_.hit(
        p.x - vp.left - (console_ ? 10 : 23), p.y - vp.top - (console_ ? 3 : 14) + scrollY_);
}
void ReferencePageView::select(std::size_t a, std::size_t b)
{
    anchor_ = std::min(a, layout_.text.size());
    caret_ = std::min(b, layout_.text.size());
    invalid();
}
std::string ReferencePageView::selectedText() const
{
    return referenceUtf8(std::u32string_view(layout_.text)
                             .substr(std::min(anchor_, caret_),
                                 std::max(anchor_, caret_) - std::min(anchor_, caret_)));
}
void ReferencePageView::onMouseDownEvent(MouseDownEvent& e)
{
    bool wasMenu = menuOpen();
    ToolPageView::onMouseDownEvent(e);
    if (wasMenu || e.consumed)
        return;
    if (e.buttonState.isLeft() && (console_ || findOpen_)
        && toolContains(inputBounds(), e.mousePosition)) {
        openInput(findOpen_);
        e.consumed = true;
        return;
    }
    auto vp = textViewport();
    if (!toolContains(vp, e.mousePosition))
        return;
    finishInput();
    focus();
    if (e.buttonState.isRight()) {
        // First click opens the same item-separated canvas menu as other pages.
        openToolMenu(toolRect(e.mousePosition.x, e.mousePosition.y, 132, 0),
            { { "COPY", [this] { CClipboard::setString(selectedText().c_str()); }, false,
                  anchor_ != caret_ },
                { "SELECT ALL", [this] { select(0, layout_.text.size()); } },
                { "FIND…", [this] { openInput(true); } } });
        invalid();
        e.consumed = true;
        return;
    }
    if (!e.buttonState.isLeft())
        return;
    if (e.mousePosition.x >= vp.right - 9 && layout_.height > vp.getHeight()) {
        scrollbar_ = true;
        scrollOrigin_ = e.mousePosition.y;
        scrollStart_ = scrollY_;
        e.consumed = true;
        return;
    }
    auto offset = hitText(e.mousePosition);
    if (e.clickCount >= 3) {
        auto r = layout_.paragraph(offset);
        select(r.first, r.second);
    } else if (e.clickCount == 2) {
        auto r = layout_.word(offset);
        select(r.first, r.second);
    } else {
        if (!e.modifiers.has(ModifierKey::Shift))
            anchor_ = offset;
        caret_ = offset;
    }
    selecting_ = true;
    selectionPoint_ = e.mousePosition;
    autoscroll_ = owned(new CVSTGUITimer([this](CVSTGUITimer*) { dragSelection(true); }, 50));
    invalid();
    e.consumed = true;
}
void ReferencePageView::dragSelection(bool scroll)
{
    if (!selecting_)
        return;
    auto vp = textViewport();
    if (scroll) {
        if (selectionPoint_.y < vp.top)
            scrollTo(scrollY_ - 12);
        else if (selectionPoint_.y > vp.bottom)
            scrollTo(scrollY_ + 12);
        else
            return;
    }
    caret_ = hitText(selectionPoint_);
    invalid();
}
void ReferencePageView::onMouseMoveEvent(MouseMoveEvent& e)
{
    if (scrollbar_) {
        auto vp = textViewport();
        double total = layout_.height + (console_ ? 6 : 28),
               h = std::max(24., vp.getHeight() * vp.getHeight() / total);
        scrollTo(scrollStart_
            + (e.mousePosition.y - scrollOrigin_) * (total - vp.getHeight())
                / std::max(1., vp.getHeight() - h));
        e.consumed = true;
        return;
    }
    if (selecting_) {
        selectionPoint_ = e.mousePosition;
        dragSelection(false);
        e.consumed = true;
        return;
    }
    ToolPageView::onMouseMoveEvent(e);
    if (getFrame())
        getFrame()->setCursor(
            toolContains(textViewport(), e.mousePosition) ? kCursorIBeam : kCursorDefault);
}
void ReferencePageView::onMouseUpEvent(MouseUpEvent& e)
{
    if (selecting_ || scrollbar_) {
        selecting_ = scrollbar_ = false;
        autoscroll_ = nullptr;
        e.consumed = true;
        return;
    }
    ToolPageView::onMouseUpEvent(e);
}
void ReferencePageView::onMouseCancelEvent(MouseCancelEvent& e)
{
    stopRefresh();
    e.consumed = true;
    invalid();
}
void ReferencePageView::onMouseWheelEvent(MouseWheelEvent& e)
{
    if (!toolContains(textViewport(), e.mousePosition))
        return;
    scrollTo(scrollY_ - e.deltaY * ((e.flags & MouseWheelEvent::PreciseDeltas) ? 10 : 40));
    e.consumed = true;
}
void ReferencePageView::revealCaret()
{
    if (layout_.glyphs.empty())
        return;
    auto i = std::min(caret_, layout_.glyphs.size() - 1);
    auto& g = layout_.glyphs[i];
    auto vp = textViewport();
    double inset = console_ ? 3 : 14;
    if (g.y + inset < scrollY_)
        scrollTo(g.y + inset);
    else if (g.y + g.height + inset > scrollY_ + vp.getHeight())
        scrollTo(g.y + g.height + inset - vp.getHeight());
}
void ReferencePageView::find(std::string query, bool backwards)
{
    findQuery_ = std::move(query);
    auto needle = referenceUnicode(findQuery_), haystack = layout_.text;
    for (auto& c : needle)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    for (auto& c : haystack)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    if (needle.empty())
        return;
    std::size_t at;
    if (backwards) {
        auto start = std::min(anchor_, caret_);
        at = haystack.rfind(needle, start ? start - 1 : std::u32string::npos);
        if (at == std::u32string::npos)
            at = haystack.rfind(needle);
    } else {
        at = haystack.find(needle, std::max(anchor_, caret_));
        if (at == std::u32string::npos)
            at = haystack.find(needle);
    }
    if (at != std::u32string::npos) {
        select(at, at + needle.size());
        revealCaret();
    }
}
void ReferencePageView::onKeyboardEvent(KeyboardEvent& e)
{
    ToolPageView::onKeyboardEvent(e);
    if (e.consumed || e.type != EventType::KeyDown)
        return;
    bool command = e.modifiers.has(ModifierKey::Control);
    if (command && (e.character == 'c' || e.character == 'C')) {
        CClipboard::setString(selectedText().c_str());
    } else if (command && (e.character == 'a' || e.character == 'A'))
        select(0, layout_.text.size());
    else if (command && (e.character == 'f' || e.character == 'F'))
        openInput(true);
    else if (command && (e.character == 'g' || e.character == 'G'))
        find(findQuery_, e.modifiers.has(ModifierKey::Shift));
    else if (e.virt == VirtualKey::Escape) {
        stopRefresh();
        if (console_) {
            if (referenceServices_.returnToTracker)
                referenceServices_.returnToTracker();
        } else if (referenceServices_.closeHelp)
            referenceServices_.closeHelp();
    } else if (console_ && (e.character == ':' || e.character == '`'))
        focusInput();
    else if (e.virt == VirtualKey::PageDown)
        scrollTo(scrollY_ + textViewport().getHeight() - 20);
    else if (e.virt == VirtualKey::PageUp)
        scrollTo(scrollY_ - textViewport().getHeight() + 20);
    else if (e.virt == VirtualKey::Home && !e.modifiers.has(ModifierKey::Shift))
        scrollTo(0);
    else if (e.virt == VirtualKey::End && !e.modifiers.has(ModifierKey::Shift))
        scrollTo(layout_.height);
    else if (e.virt == VirtualKey::Left || e.virt == VirtualKey::Right || e.virt == VirtualKey::Up
        || e.virt == VirtualKey::Down || e.virt == VirtualKey::Home || e.virt == VirtualKey::End) {
        auto next = caret_;
        if (e.virt == VirtualKey::Left)
            next = next ? next - 1 : 0;
        else if (e.virt == VirtualKey::Right)
            next = std::min(next + 1, layout_.text.size());
        else if (e.virt == VirtualKey::Home)
            next = 0;
        else if (e.virt == VirtualKey::End)
            next = layout_.text.size();
        else if (!layout_.glyphs.empty()) {
            auto& g = layout_.glyphs[std::min(next, layout_.glyphs.size() - 1)];
            next = layout_.hit(g.x, g.y + (e.virt == VirtualKey::Up ? -g.height : 1.1 * g.height));
        }
        if (!e.modifiers.has(ModifierKey::Shift))
            anchor_ = next;
        caret_ = next;
        revealCaret();
    } else
        return;
    e.consumed = true;
    invalid();
}
}
