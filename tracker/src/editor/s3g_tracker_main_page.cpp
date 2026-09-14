#include "s3g_tracker_main_page.h"
#include "s3g/tracker/editor_palette.h"
#include "s3g/tracker/editor_reference.h"
#include "s3g/tracker/fx_catalog.h"
#include "vstgui/lib/cclipboard.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/common/generictextedit.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace s3g::tracker::editor {
using namespace VSTGUI;
namespace f = s3g::portable_gui::foundation;
namespace {
    CRect rect(double x, double y, double w, double h) { return { x, y, x + w, y + h }; }
    Rect logical(CRect r) { return { r.left, r.top, r.getWidth(), r.getHeight() }; }
    CColor color(Color c) { return { c.red, c.green, c.blue, c.alpha }; }
    bool contains(CRect r, CPoint p)
    {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }
    template <class... A> std::string fmt(const char* pattern, A... args)
    {
        auto size = std::snprintf(nullptr, 0, pattern, args...);
        if (size <= 0)
            return {};
        std::vector<char> buffer(static_cast<std::size_t>(size) + 1);
        std::snprintf(buffer.data(), buffer.size(), pattern, args...);
        return { buffer.data(), static_cast<std::size_t>(size) };
    }
    uint32_t modifiers(const Modifiers& m)
    {
        uint32_t result
            = (m.has(ModifierKey::Shift) ? Shift : 0u) | (m.has(ModifierKey::Alt) ? Alt : 0u);
#if defined(__APPLE__)
        if (m.has(ModifierKey::Super))
            result |= Control;
        if (m.has(ModifierKey::Control))
            result |= Command;
#else
        if (m.has(ModifierKey::Control))
            result |= Control;
        if (m.has(ModifierKey::Super))
            result |= Command;
#endif
        return result;
    }
    MainPageServices withDefaults(MainPageServices services)
    {
        if (!services.paint.font)
            services.paint.font = [](double size, FontWeight weight, bool) {
                const double readable = std::max(8., std::round(size * 1.16 * 2) / 2);
                auto font = f::makeUiFont(readable);
                if (weight == FontWeight::Semibold)
                    font->setStyle(kBoldFace);
                auto platform = font->getPlatformFont();
                const double ascent = platform ? platform->getAscent() : readable;
                const double height = platform
                    ? std::ceil(ascent + platform->getDescent() + platform->getLeading())
                    : readable * 1.3;
                return GridFont { font->getName().getString(), readable, ascent, height };
            };
        if (!services.suiteFont)
            services.suiteFont = [](double size) {
                auto font = f::makeUiFont(size);
                auto platform = font->getPlatformFont();
                double ascent = platform ? platform->getAscent() : size;
                return GridFont { font->getName().getString(), size, ascent,
                    platform ? std::ceil(ascent + platform->getDescent() + platform->getLeading())
                             : size * 1.3 };
            };
        return services;
    }
    class PageTextEdit final : public CTextEdit {
    public:
        using CTextEdit::CTextEdit;
        std::function<bool(KeyboardEvent&)> handle;
        bool caretAtEnd = true;
        void moveCaretToEnd()
        {
            if (!getFrame())
                return;
            // Run outside GenericTextEdit's recursive-key guard. Holding the edit
            // alive does not keep a closed frame alive; recheck focus before use.
            auto move = [self = SharedPointer<PageTextEdit>(this)] {
                auto* frame = self->getFrame();
                if (!frame || frame->getFocusView() != self.get())
                    return;
                KeyboardEvent end(EventType::KeyDown);
#if defined(__APPLE__)
                end.virt = VirtualKey::Right;
                end.modifiers
                    = Modifiers { ModifierKey::Control }; // Command-End-of-line in GenericTextEdit
#else
                end.virt = VirtualKey::End;
#endif
                static_cast<IPlatformFrameCallback*>(frame)->platformOnEvent(end);
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
            // The source editor places the caret after the initial cell token.
            if (caretAtEnd)
                moveCaretToEnd();
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
} // namespace

MainPageView::MainPageView(
    app::TrackerViewState& state, app::WorkspaceCallbacks& callbacks, MainPageServices services)
    : ContentView(rect(0, 0, 1320, 820))
    , state_(state)
    , callbacks_(callbacks)
    , services_(withDefaults(std::move(services)))
    , grid_(state, callbacks, gridServices())
{
    setWantsFocus(true);
    displayedPattern_ = playbackFollowPatternId(&state_);
    observedFollow_ = state_.trackerFollow;
    observedFollowRevision_ = state_.trackerFollowRevision;
}
MainPageView::~MainPageView()
{
    finishText(true);
    stopRefresh();
}
GridServices MainPageView::gridServices()
{
    auto result = services_.grid;
    result.invalidate = [this] { invalid(); };
    result.reveal = [this](Rect r) { reveal(r); };
    result.beginText = [this](const GridTextEdit& edit) { startText(edit); };
    result.readClipboard = [] {
        auto s = CClipboard::getString();
        return s ? std::string(s->getString()) : std::string();
    };
    result.writeClipboard = [](const std::string& text) { CClipboard::setString(text.c_str()); };
    result.focusConsole = [this] { focusConsole(); };
    result.zoomIn = [this] { setGridZoom(zoom_ * 1.16); };
    result.zoomOut = [this] { setGridZoom(zoom_ / 1.16); };
    result.zoomReset = [this] { setGridZoom(1); };
    return result;
}
CRect MainPageView::gridViewport() const
{
    return rect(0, 115, 1320, 478);
}
CRect MainPageView::envelopeRect() const
{
    return rect(0, 594, 1320, 140);
}
Point MainPageView::gridPoint(CPoint p) const
{
    const auto y = (p.y - gridViewport().top) / zoom_;
    return { scrollX_ + p.x / zoom_, y + (pinnedHeader() && y < 86 ? 0 : scrollY_) };
}
CRect MainPageView::pageRect(Rect r) const
{
    return rect((r.x - scrollX_) * zoom_,
        gridViewport().top + (r.y - (pinnedHeader() && r.y < 86 ? 0 : scrollY_)) * zoom_,
        r.width * zoom_, r.height * zoom_);
}
void MainPageView::scrollTo(double x, double y)
{
    pauseFollowing();
    setViewport(x, y);
}
TrackerFollowLayout MainPageView::followLayout() const
{
    return { state_.trackerFollow.mode, playbackFollowVisibleRows(&state_),
        gridViewport().getHeight() / zoom_, 10. / zoom_ };
}
std::optional<std::size_t> MainPageView::followLane() const
{
    const auto* pattern = playbackFollowPattern(&state_);
    const auto lane = state_.trackerFollow.selectedLane ? state_.session.selectedTrack
                                                        : std::size_t(state_.trackerFollow.lane);
    if (!pattern || lane >= pattern->tracks.size() || lane >= state_.notePlayheads.size())
        return {};
    return lane;
}
CRect MainPageView::playbackGuideRect() const
{
    const auto lane = followLane();
    if (!pinnedHeader() || !state_.playing || !lane)
        return {};
    const auto row = gridPlaybackRow(&state_, *lane, 0);
    if (row >= playbackFollowVisibleRows(&state_))
        return {};
    return rect(0, gridViewport().top + (86. + 25. * double(row) - scrollY_) * zoom_,
        gridViewport().getWidth() - 10., 25. * zoom_);
}
void MainPageView::pauseFollowing()
{
    if (pinnedHeader() && !followPaused_) {
        followPaused_ = true;
        invalidRect(rect(900, 9, 402, 51));
    }
}
void MainPageView::resumeFollowing()
{
    finishText();
    if (text_)
        return;
    followPaused_ = false;
    updateFollowing();
    invalid();
}
void MainPageView::followPreferencesChanged()
{
    observedFollow_ = state_.trackerFollow;
    followPaused_ = false;
    if (callbacks_.viewPreferencesChanged)
        callbacks_.viewPreferencesChanged();
    setViewport(scrollX_, scrollY_);
    updateFollowing();
    invalid();
}
void MainPageView::setFollowMode(TrackerFollowMode mode)
{
    if (mode != TrackerFollowMode::Static && mode != TrackerFollowMode::Center
        && mode != TrackerFollowMode::Page)
        return;
    finishText();
    if (text_)
        return;
    state_.trackerFollow.mode = mode;
    followPreferencesChanged();
}
void MainPageView::setFollowSource(bool selectedLane, std::size_t lane)
{
    if (lane >= kMaximumTrackCount)
        return;
    state_.trackerFollow.selectedLane = selectedLane;
    state_.trackerFollow.lane = static_cast<uint32_t>(lane);
    followPreferencesChanged();
}
void MainPageView::updateFollowing()
{
    if (observedFollow_ != state_.trackerFollow
        || observedFollowRevision_ != state_.trackerFollowRevision) {
        observedFollow_ = state_.trackerFollow;
        observedFollowRevision_ = state_.trackerFollowRevision;
        followPaused_ = false;
        setViewport(scrollX_, scrollY_);
        invalid();
    }
    const auto lane = followLane();
    const std::optional<std::size_t> row = lane && state_.playing
        ? std::optional<std::size_t>(gridPlaybackRow(&state_, *lane, 0))
        : std::nullopt;
    if (pinnedHeader()) {
        if (!followPaused_ && row && *row < playbackFollowVisibleRows(&state_) && !text_
            && popups_.empty() && !dragSelection_ && !dragEnvelope_ && !scrollDrag_ && !loopAnchor_
            && !dragValue_) {
            const auto y = followLayout().scrollForRow(*row);
            if (y != scrollY_)
                setViewport(scrollX_, y);
        }
        if (lane != presentedFollowLane_ || row != presentedFollowRow_) {
            invalidRect(gridViewport());
            invalidRect(rect(900, 9, 402, 51));
        }
    }
    presentedFollowLane_ = lane;
    presentedFollowRow_ = row;
}
void MainPageView::setViewport(double x, double y)
{
    auto* pattern = playbackFollowPattern(&state_);
    const auto viewport = gridViewport();
    double width = app::trackerDocumentWidth(pattern ? pattern->tracks.size() : 0,
        viewport.getWidth() / zoom_, state_.sequenceColumnsExpanded);
    const auto layout = followLayout();
    scrollX_ = std::clamp(x, 0., std::max(0., width - viewport.getWidth() / zoom_));
    scrollY_ = std::clamp(y, layout.minimum, layout.maximum);
    invalidRect(viewport);
}
void MainPageView::setGridZoom(double zoom)
{
    finishText();
    const auto viewport = gridViewport();
    const auto centerX = scrollX_ + viewport.getWidth() / (2 * zoom_);
    const auto centerY = scrollY_ + viewport.getHeight() / (2 * zoom_);
    zoom_ = std::clamp(std::isfinite(zoom) ? zoom : 1., .55, 1.8);
    setViewport(
        centerX - viewport.getWidth() / (2 * zoom_), centerY - viewport.getHeight() / (2 * zoom_));
    if (!pinnedHeader() || followPaused_)
        reveal(grid_.cellRect({ state_.session.selectedTrack, state_.session.selectedField,
            state_.session.selectedRow }));
    else
        updateFollowing();
    invalid();
}
void MainPageView::reveal(Rect r)
{
    if (selectingHeader_)
        return;
    pauseFollowing();
    auto vp = gridViewport();
    double x = scrollX_, y = scrollY_;
    if (r.x < x)
        x = r.x;
    else if (r.x + r.width > x + vp.getWidth() / zoom_)
        x = r.x + r.width - vp.getWidth() / zoom_;
    const auto header = pinnedHeader() ? 86. : 0.;
    if (r.y < y + header)
        y = r.y - header;
    else if (r.y + r.height > y + vp.getHeight() / zoom_)
        y = r.y + r.height - vp.getHeight() / zoom_;
    if (x != scrollX_ || y != scrollY_)
        scrollTo(x, y);
}
void MainPageView::focusTracker()
{
    if (services_.requestNativeFocus)
        services_.requestNativeFocus();
    if (getFrame())
        getFrame()->setFocusView(this);
}
void MainPageView::focusConsole()
{
    startConsole();
}
void MainPageView::stopRefresh()
{
    grid_.finishValueDrag();
    pendingButton_ = {};
    returnTextFocus_ = false;
    dragSelection_ = dragValueCandidate_ = dragValue_ = dragEnvelope_ = dragSwing_ = false;
    scrollDrag_ = 0;
    loopAnchor_.reset();
    dragAddress_.reset();
    finishText();
    if (text_)
        finishText(true);
    popups_.clear();
    if (momentaryFill_) {
        state_.fillActive = oldFill_;
        momentaryFill_ = false;
        if (callbacks_.fillChanged)
            callbacks_.fillChanged(state_.fillActive);
    }
}
void MainPageView::reloadModel()
{
    auto id = playbackFollowPatternId(&state_);
    if (id != displayedPattern_ || displayedSongFollow_ != state_.songPlaybackActive) {
        popups_.clear();
        // A different pattern invalidates a cell edit, not Live Code's draft
        // or command-entry focus (commands can themselves switch patterns).
        if (edit_)
            finishText(true);
        grid_.clearGridSelection();
        displayedPattern_ = id;
        displayedSongFollow_ = state_.songPlaybackActive;
    }
    grid_.effectiveGridSelection();
    setViewport(scrollX_, scrollY_);
    updateFollowing();
    invalid();
}
void MainPageView::refreshPlaybackDisplay()
{
    // The coordinator has already applied Tracker's presentation holdback.
    // Consume that snapshot; never create another timer or advance a playhead.
    if (closeText_) {
        const bool returnFocus = returnTextFocus_;
        returnTextFocus_ = false;
        finishText(cancelText_);
        if (returnFocus && !text_)
            focusTracker();
    }
    if (displayedPattern_ != playbackFollowPatternId(&state_)
        || displayedSongFollow_ != state_.songPlaybackActive)
        reloadModel();
    updateFollowing();
    auto fields = gridFieldCount(state_.sequenceColumnsExpanded);
    auto* pattern = playbackFollowPattern(&state_);
    if (!pattern)
        return;
    bool changed = !primed_ || presentedPlaying_ != state_.playing;
    if (!primed_ || presentedMutedTracks_ != state_.songPlaybackMutedTracks) {
        invalid();
        changed = true;
    }
    for (std::size_t lane = 0; lane < std::min(pattern->tracks.size(), presented_.size()); ++lane)
        for (std::size_t field = 0; field < fields; ++field) {
            auto row = gridPlaybackRow(&state_, lane, field);
            if (!primed_ || presentedPlaying_ != state_.playing || row != presented_[lane][field]) {
                auto old = pageRect(grid_.cellRect({ lane, field, presented_[lane][field] }));
                old.bound(gridViewport());
                auto next = pageRect(grid_.cellRect({ lane, field, row }));
                next.bound(gridViewport());
                if (!old.isEmpty())
                    invalidRect(old);
                if (!next.isEmpty())
                    invalidRect(next);
                changed = true;
            }
            presented_[lane][field] = row;
        }
    if (changed) {
        invalidRect(envelopeRect());
        invalidRect(rect(18, 740, 1284, 68));
    }
    primed_ = true;
    presentedPlaying_ = state_.playing;
    presentedMutedTracks_ = state_.songPlaybackMutedTracks;
}
void MainPageView::fill(CRect r, uint32_t rgb, double alpha)
{
    context_->setFillColor(
        services_.paint.color ? color(services_.paint.color(rgb, alpha)) : f::color(rgb));
    context_->drawRect(r, kDrawFilled);
}
void MainPageView::stroke(CRect r, uint32_t rgb, double width)
{
    context_->setFrameColor(
        services_.paint.color ? color(services_.paint.color(rgb, 1)) : f::color(rgb));
    context_->setLineWidth(width);
    auto path = owned(context_->createGraphicsPath());
    if (path) {
        path->addRect(r);
        context_->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
}
void MainPageView::label(
    const std::string& text, CRect r, uint32_t rgb, CHoriTxtAlign align, double size)
{
    auto font = services_.suiteFont ? services_.suiteFont(size)
                                    : GridFont { "Fira Code", size, size, size * 1.3 };
    DisplayList list;
    auto c = services_.paint.color
        ? services_.paint.color(rgb, 1)
        : Color { uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), 255 };
    list.text(text, logical(r), c, font.name, font.size, r.top + font.baseline,
        align == kCenterText ? Alignment::Center
                             : align == kRightText ? Alignment::Right : Alignment::Left);
    drawDisplayList(*context_, list, services_.fontFactory);
}
void MainPageView::panel(CRect r, const std::string& title)
{
    // Preserve the rendered Cocoa reference: NSFrameRect uses the current
    // fill, not the separately configured stroke. These controls therefore
    // have no contrasting outline (unlike the grid's explicit line paths).
    fill(r, 0x1d1d1d);
    fill(rect(r.left, r.top, r.getWidth(), 21), 0x131313);
    fill(rect(r.left, r.top, r.getWidth(), 2), 0xb8b8b8);
    label(title, rect(r.left + 8, r.top + 5, r.getWidth() - 16, 16), 0xa8a8a8);
}
void MainPageView::button(CRect r, const std::string& title, std::function<void()> action,
    bool enabled, int state, bool danger)
{
    auto b = CRect(r).inset(.5, .5);
    const bool pressed = pendingButton_ && r == pendingButtonBounds_ && contains(r, hover_);
    fill(b,
        pressed ? 0x414141
                : state == 2
                ? 0x72d68c
                : state == 1 ? 0x7fd7e8 : enabled && contains(r, hover_) ? 0x343434 : 0x292929,
        pressed ? 1. : state == 2 ? .2 : state == 1 ? .16 : 1.);
    (void)danger; // The native suite renderer's NSFrameRect is fill-colored.
    auto font
        = services_.suiteFont ? services_.suiteFont(10) : GridFont { "Fira Code", 10, 10, 13 };
    label(title,
        rect(r.left, r.top + (r.getHeight() - font.lineHeight) * .5, r.getWidth(), font.lineHeight),
        enabled ? 0x929292 : 0x656565, kCenterText);
    if (enabled)
        hits_.push_back({ r, std::move(action), title, false });
}
void MainPageView::menu(
    CRect r, const std::string& title, std::vector<MainMenuItem> items, bool enabled)
{
    auto box = rect(r.left, r.top, r.getWidth(), 15);
    fill(box, 0x131313);
    fill(rect(r.left + 1, r.top + 1, 2, 13), enabled ? 0x7f7f7f : themeRGB(ThemeRole::Grid));
    label(fitMenuText(title, r.getWidth() - 28), rect(r.left + 8, r.top + 2, r.getWidth() - 28, 20),
        enabled ? 0x929292 : themeRGB(ThemeRole::Grid));
    label(
        "v", rect(r.right - 12, r.top + 1, 10, 20), enabled ? 0x929292 : themeRGB(ThemeRole::Grid));
    if (enabled)
        hits_.push_back(
            { r, [this, r, items = std::move(items)]() mutable { openMenu(r, std::move(items)); },
                title });
}
std::string MainPageView::fitMenuText(std::string text, double width)
{
    for (auto& c : text)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    if (width <= 0)
        return text;
    auto info = services_.suiteFont(10);
    auto font = services_.fontFactory ? services_.fontFactory(info.name, info.size)
                                      : f::makeUiFont(info.size);
    auto* painter = font->getFontPainter();
    auto fits = [&](const std::string& value) {
        return !painter
            || painter->getStringWidth(nullptr, UTF8String(value.c_str()).getPlatformString(), true)
            <= width;
    };
    if (fits(text))
        return text;
    constexpr std::pair<const char*, const char*> substitutions[]
        = { { "ENERGY-NORMALIZED", "ENERGY NORM" }, { "ENERGY NORMALIZED", "ENERGY NORM" },
              { "HYPERCARDIOID", "HYPER" }, { "SUPERCARDIOID", "SUPER" }, { "CARDIOID", "CARD" },
              { "VIRTUAL", "VIRT" }, { "FEEDFORWARD", "FEED FWD" }, { "PROJECTION", "PROJ" },
              { "ELEVATION", "ELEV" }, { "DIRECTIONAL", "DIR" }, { "INTERPOLATION", "INTERP" },
              { "ALTERNATING", "ALT" } };
    for (const auto& substitution : substitutions) {
        std::size_t at = 0;
        std::string from = substitution.first, to = substitution.second;
        while ((at = text.find(from, at)) != std::string::npos) {
            text.replace(at, from.size(), to);
            at += to.size();
        }
        if (fits(text))
            return text;
    }
    while (text.size() > 1 && !fits(text + "…")) {
        auto at = text.size() - 1;
        while (at && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
            --at;
        text.resize(at);
    }
    return text + "…";
}
void MainPageView::draw(CDrawContext* context)
{
    context_ = context;
    ++draws_;
    hits_.clear();
    if (draws_ == 1 && getFrame())
        getFrame()->enableTooltips(true, 650);
    context->saveGlobalState();
    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    fill(getViewSize(), 0x060606);
    paintControls();
    GridPainter painter(state_, grid_.selection, services_.paint);
    auto vp = gridViewport();
    CRect originalClip;
    context->getClipRect(originalClip);
    auto clip = vp;
    clip.bound(originalClip);
    if (pinnedHeader()) {
        context->setClipRect(clip);
        fill(vp, themeRGB(ThemeRole::Panel));
        context->setClipRect(originalClip);
    }
    // STATIC retains the original scrollable-header rendering. Follow modes
    // pin headers, including during a manual hold, and leave genuine blank
    // padding at the first/last row instead of drawing duplicated rows.
    auto paintGrid = [&](CRect region, double scroll) {
        region.bound(originalClip);
        if (region.isEmpty())
            return;
        context->setClipRect(region);
        {
            CDrawContext::Transform transform(*context,
                CGraphicsTransform(zoom_, 0, 0, zoom_, -scrollX_ * zoom_, vp.top - scroll * zoom_));
            CRect localClip;
            context->getClipRect(localClip);
            auto* pattern = playbackFollowPattern(&state_);
            Rect document { 0, 0,
                app::trackerDocumentWidth(pattern ? pattern->tracks.size() : 0,
                    vp.getWidth() / zoom_, state_.sequenceColumnsExpanded),
                app::kTrackerGridHeaderHeight
                    + double(playbackFollowVisibleRows(&state_)) * app::kTrackerGridRowHeight };
            drawDisplayList(
                *context, painter.grid(document, logical(localClip)), services_.fontFactory);
        }
        context->setClipRect(region);
        {
            CDrawContext::Transform transform(*context, CGraphicsTransform(1, 0, 0, 1, 0, vp.top));
            drawDisplayList(*context,
                painter.gutter({ 0, 0, app::kTrackerRowNumberWidth * zoom_, vp.getHeight() },
                    scroll, zoom_, grid_.selectingWholeRows),
                services_.fontFactory);
        }
    };
    if (pinnedHeader()) {
        auto body = vp;
        body.top += std::max(86., 86. - scrollY_) * zoom_;
        paintGrid(body, scrollY_);
        auto header = vp;
        header.bottom = header.top + 86. * zoom_;
        paintGrid(header, 0.);
        body = vp;
        body.top = header.bottom;
        body.bottom -= 10.;
        body.bound(originalClip);
        if (!body.isEmpty()) {
            context->setClipRect(body);
            const auto guide = playbackGuideRect();
            if (!guide.isEmpty()) {
                fill(guide, 0x7fd7e8, .075);
                fill(rect(0, guide.top, app::kTrackerRowNumberWidth * zoom_, 2.), 0x7fd7e8, .8);
                const auto lane = followLane();
                if (lane) {
                    auto cell = pageRect(
                        grid_.cellRect({ *lane, 0, gridPlaybackRow(&state_, *lane, 0) }));
                    cell.left = std::max(cell.left, app::kTrackerRowNumberWidth * zoom_);
                    if (!cell.isEmpty())
                        stroke(cell, 0x7fd7e8, 1.5);
                }
            }
        }
    } else
        paintGrid(clip, scrollY_);
    context->setClipRect(originalClip);
    {
        auto e = envelopeRect();
        CDrawContext::Transform transform(*context, CGraphicsTransform(1, 0, 0, 1, e.left, e.top));
        drawDisplayList(*context, painter.envelope({ 0, 0, e.getWidth(), e.getHeight() }),
            services_.fontFactory);
        drawDisplayList(*context, painter.envelopePlayback({ 0, 0, e.getWidth(), e.getHeight() }),
            services_.fontFactory);
    }
    paintScrollbars();
    drawMenus();
    context->restoreGlobalState();
    context_ = nullptr;
    setDirty(false);
}
void MainPageView::command(const std::string& text)
{
    if (callbacks_.executeCommand)
        callbacks_.executeCommand(text);
    reloadModel();
}
void MainPageView::changedTransport()
{
    if (callbacks_.transportChanged)
        callbacks_.transportChanged();
    invalid();
}

void MainPageView::paintControls()
{
    const bool editable = !state_.songPlaybackActive;
    const auto invoke = [this](const std::function<void()>& fn) {
        if (fn)
            fn();
        reloadModel();
    };
    fill(rect(0, 0, 1320, 69), 0x101010);
    panel(rect(18, 9, 874, 51), "PATTERN");
    panel(rect(900, 9, 402, 51), "VIEW");
    std::vector<MainMenuItem> followModes;
    constexpr const char* followNames[] { "STATIC", "CENTER", "PAGE" };
    for (int i = 0; i < 3; ++i)
        followModes.push_back({ std::string("FOLLOW: ") + followNames[i],
            [this, i] { setFollowMode(static_cast<TrackerFollowMode>(i)); }, {}, true,
            int(state_.trackerFollow.mode) == i });
    const auto followTitle = state_.trackerFollow.mode == TrackerFollowMode::Page
        ? fmt("PAGE %02zu", followPageRows())
        : std::string(followNames[std::clamp(int(state_.trackerFollow.mode), 0, 2)]);
    menu(rect(950, 12, 126, 17), followTitle, std::move(followModes));
    std::vector<MainMenuItem> followSources;
    followSources.push_back(
        { "SELECTED LANE (NOTE)", [this] { setFollowSource(true, state_.trackerFollow.lane); }, {},
            true, state_.trackerFollow.selectedLane });
    followSources.push_back({ "", {}, {}, false });
    if (const auto* pattern = playbackFollowPattern(&state_))
        for (std::size_t lane = 0; lane < pattern->tracks.size(); ++lane)
            followSources.push_back({ fmt("NOTE L%02zu · ", lane + 1) + pattern->tracks[lane].name,
                [this, lane] { setFollowSource(false, lane); }, {}, true,
                !state_.trackerFollow.selectedLane && state_.trackerFollow.lane == lane });
    menu(rect(1082, 12, 140, 17),
        state_.trackerFollow.selectedLane ? "NOTE: SELECTED"
                                          : fmt("NOTE: L%02u", state_.trackerFollow.lane + 1),
        std::move(followSources));
    button(
        rect(1228, 12, 66, 17),
        followPaused_ ? "RESUME" : !followLane() ? "NO LANE" : pinnedHeader() ? "FOLLOW" : "OFF",
        [this] { resumeFollowing(); }, pinnedHeader() && followPaused_,
        pinnedHeader() && !followPaused_ ? 1 : 0);
    std::vector<MainMenuItem> patterns;
    std::string selected = playbackFollowPatternId(&state_);
    for (const auto& entry : state_.patternBank.entries) {
        auto title = entry.id + (entry.pattern.name.empty() ? "" : " · " + entry.pattern.name);
        patterns.push_back({ title,
            [this, id = entry.id] {
                if (callbacks_.selectPattern)
                    callbacks_.selectPattern(id);
                reloadModel();
            },
            {}, true, entry.id == selected });
        if (entry.id == selected)
            selected = title;
    }
    menu(rect(26, 34, 383, 22), selected, std::move(patterns), editable);
    button(
        rect(417, 34, 50, 22), "NAME", [=] { invoke(callbacks_.renamePattern); }, editable);
    auto add = [this](bool duplicate) {
        if (callbacks_.addPattern)
            callbacks_.addPattern(duplicate);
        reloadModel();
    };
    bool canGrow = state_.patternBank.entries.size() < kMaximumPatternBankEntries;
    button(
        rect(475, 34, 45, 22), "DUP", [=] { add(true); }, editable && canGrow);
    button(
        rect(528, 34, 30, 22), "＋", [=] { add(false); }, editable && canGrow);
    button(
        rect(566, 34, 30, 22), "−", [=] { invoke(callbacks_.deletePattern); },
        editable && state_.patternBank.entries.size() > 1, 0, true);
    button(
        rect(604, 34, 70, 22), "+ TRACK", [this] { command("track add"); }, editable);
    button(
        rect(682, 34, 76, 22), "− TRACK",
        [this] { command("track remove " + std::to_string(state_.session.selectedTrack + 1)); },
        editable && !state_.session.pattern.tracks.empty());
    button(
        rect(766, 34, 55, 22), "UNDO", [this] { command("undo"); }, editable && state_.canUndo);
    button(
        rect(829, 34, 55, 22), "REDO", [this] { command("redo"); }, editable && state_.canRedo);
    button(rect(908, 34, 94, 22),
        state_.sequenceColumnsExpanded ? "COLLAPSE DETAIL" : "EXPAND DETAIL", [this] {
            state_.sequenceColumnsExpanded = !state_.sequenceColumnsExpanded;
            state_.session.selectedField = std::min(
                state_.session.selectedField, gridFieldCount(state_.sequenceColumnsExpanded) - 1);
            grid_.clearGridSelection();
            if (callbacks_.viewPreferencesChanged)
                callbacks_.viewPreferencesChanged();
            reloadModel();
        });
    button(rect(1010, 34, 88, 22), state_.showMidiNoteValues ? "NOTE: MIDI" : "NOTE: NAME", [this] {
        state_.showMidiNoteValues = !state_.showMidiNoteValues;
        if (callbacks_.viewPreferencesChanged)
            callbacks_.viewPreferencesChanged();
        invalid();
    });
    std::vector<MainMenuItem> jumps;
    for (uint32_t n = 1; n <= 16; ++n)
        jumps.push_back({ "JUMP " + std::to_string(n),
            [this, n] {
                state_.trackerRowJump = n;
                if (callbacks_.viewPreferencesChanged)
                    callbacks_.viewPreferencesChanged();
                invalid();
            },
            {}, true, n == state_.trackerRowJump });
    menu(rect(1106, 34, 58, 22), "JUMP " + std::to_string(state_.trackerRowJump), std::move(jumps));
    button(rect(1172, 34, 26, 22), "−", [this] { setGridZoom(zoom_ / 1.16); });
    button(rect(1206, 34, 44, 22), fmt("%.0f%%", zoom_ * 100), [this] { setGridZoom(1); });
    button(rect(1258, 34, 26, 22), "+", [this] { setGridZoom(zoom_ * 1.16); });
    fill(rect(0, 70, 1320, 44), 0x181818);
    fill(rect(0, 70, 1320, 21), 0x101010);
    fill(rect(0, 70, 1320, 2), themeRGB(ThemeRole::TextMuted));
    const auto consoleLabel = [this](const char* text, Rect r, double size, uint32_t rgb) {
        auto font = services_.paint.font(size, FontWeight::Regular, false);
        DisplayList list;
        list.text(text, r,
            services_.paint.color
                ? services_.paint.color(rgb, 1)
                : Color { uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), 255 },
            font.name, font.size, r.y + font.baseline, Alignment::Left);
        drawDisplayList(*context_, list, services_.fontFactory);
    };
    consoleLabel("LIVE CODE", { 12, 85.5, 56, 20 }, 8.5, 0xa5a5a5);
    consoleLabel(":", { 71, 81, 10, 22 }, 12, 0xbcbcbc);
    if (!text_ || edit_) {
        if (services_.consoleDraft)
            consoleText_ = services_.consoleDraft();
        fill(rect(84, 77, 1224, 29), 0x262626);
        stroke(CRect(rect(84, 77, 1224, 29)).inset(.5, .5), 0x4c4c4c);
        auto info = services_.paint.font(10, FontWeight::Regular, false);
        const auto text = consoleText_.empty() ? "kit superior compact | @k x---x--- | @h eu 7 16"
                                               : consoleText_;
        const auto textColor = consoleText_.empty() ? 0x161616u : 0xdededau;
        DisplayList list;
        list.text(text, logical(rect(87, 80, 1218, 25)),
            services_.paint.color ? services_.paint.color(textColor, 1)
                                  : Color { uint8_t(textColor >> 16), uint8_t(textColor >> 8),
                                      uint8_t(textColor), 255 },
            info.name, info.size + 2, 80 + info.baseline + 2, Alignment::Left);
        drawDisplayList(*context_, list, services_.fontFactory);
        hits_.push_back({ rect(84, 77, 1224, 29), [this] { startConsole(); }, "Live Code" });
    }
    panel(rect(18, 740, 1284, 68), "TRANSPORT");
    constexpr double y = 779;
    button(
        rect(26, y, 32, 22), "▶", [=] { invoke(callbacks_.togglePlayback); }, true,
        state_.playing ? 2 : 0);
    button(
        rect(66, y, 32, 22), "↻",
        [this] {
            state_.session.transport.loopEnabled = !state_.session.transport.loopEnabled;
            changedTransport();
        },
        true, state_.session.transport.loopEnabled ? 1 : 0);
    button(
        rect(106, y, 48, 22), "FILL",
        [this] {
            state_.fillActive = !state_.fillActive;
            if (callbacks_.fillChanged)
                callbacks_.fillChanged(state_.fillActive);
            invalid();
        },
        true, state_.fillActive ? 2 : 0);
    button(rect(162, y, 60, 22), "SYNC ALL", [=] { invoke(callbacks_.restartPlayback); });
    button(
        rect(230, y, 55, 22), "! PANIC", [=] { invoke(callbacks_.panic); }, true, 0, true);
    fill(rect(293, 767, 1, 32), 0x4c4c4c);
    const auto caption = [this](const char* text, double x, double width) {
        label(text, rect(x, 765, width, 14), themeRGB(ThemeRole::TextMuted), kCenterText);
    };
    static constexpr std::array<double, 7> rates { .25, .5, 2. / 3., 1, 1.5, 2, 4 };
    constexpr const char* rateNames[] { "1/4×", "1/2×", "2/3×", "1×", "3/2×", "2×", "4×" };
    std::vector<MainMenuItem> tempo;
    std::size_t selectedRate = 3;
    for (std::size_t i = 0; i < rates.size(); ++i) {
        if (std::abs(state_.tempoScale - rates[i]) < 1e-6)
            selectedRate = i;
        tempo.push_back({ rateNames[i],
            [this, i] {
                state_.tempoScale = rates[i];
                changedTransport();
            },
            {}, true, std::abs(state_.tempoScale - rates[i]) < 1e-6 });
    }
    caption("BPM", 302, 60);
    menu(rect(302, y, 60, 22), rateNames[selectedRate], std::move(tempo));
    caption("SWING", 370, 90);
    auto swing = std::clamp((state_.session.transport.swing - .5) / .25, 0., 1.);
    fill(rect(372, y + 9, 48, 9), 0x131313);
    fill(rect(373, y + 10, std::max(1., 46 * swing), 7), 0x7f7f7f);
    fill(rect(std::clamp(372 + 48 * swing - 1.5, 373., 416.), y + 7, 3, 13), 0xc9c9c9);
    label(fmt("%.1f", state_.session.transport.swing * 100), rect(426, y + 6, 32, 15), 0x929292,
        kRightText);
    hits_.push_back({ rect(370, y + 2, 52, 22),
        [this] {
            dragSwing_ = true;
            swingStart_ = state_.session.transport.swing;
            state_.session.transport.swing
                = std::round(std::clamp(.5 + (dragOrigin_.x - 372) / 48 * .25, .5, .75) * 1000)
                / 1000;
        },
        "Swing" });
    constexpr std::array<double, 20> gates { 1, 5, 10, 15, 20, 25, 30, 40, 50, 60, 75, 90, 100, 125,
        150, 200, 250, 500, 1000, 5000 };
    std::vector<MainMenuItem> gateItems;
    auto gateLabel = [](double g) { return g >= 1000 ? fmt("%.1fs", g / 1000) : fmt("%g MS", g); };
    bool foundGate = false;
    for (auto g : gates) {
        bool checked = std::abs(g - state_.session.gateMilliseconds) < 1e-4;
        foundGate |= checked;
        gateItems.push_back({ gateLabel(g),
            [this, g] {
                state_.session.gateMilliseconds = g;
                if (callbacks_.outputChanged)
                    callbacks_.outputChanged();
                invalid();
            },
            {}, true, checked });
    }
    if (!foundGate)
        gateItems.push_back({ fmt("%g MS", state_.session.gateMilliseconds), {}, {}, true, true });
    caption("GATE", 468, 62);
    menu(rect(468, y, 62, 22), gateLabel(state_.session.gateMilliseconds), std::move(gateItems));
    fill(rect(538, 767, 1, 32), 0x4c4c4c);
    auto maximum = std::max<std::size_t>(1, state_.session.pattern.visibleRows);
    for (const auto& entry : state_.patternBank.entries)
        maximum = std::max(maximum, entry.pattern.visibleRows);
    maximum = std::min<std::size_t>(maximum, 256);
    auto loopIn = std::min<std::size_t>(maximum, state_.session.transport.loopStartRow + 1);
    auto loopOut = std::clamp<std::size_t>(state_.session.transport.loopEndRow, loopIn, maximum);
    std::vector<MainMenuItem> starts, ends;
    for (std::size_t row = 1; row <= loopOut; ++row)
        starts.push_back({ fmt("%02zu", row),
            [this, row] {
                state_.session.transport.loopStartRow = static_cast<uint32_t>(row - 1);
                changedTransport();
            },
            {}, true, row == loopIn });
    for (auto row = loopIn; row <= maximum; ++row)
        ends.push_back({ fmt("%02zu", row),
            [this, row] {
                state_.session.transport.loopEndRow = static_cast<uint32_t>(row);
                changedTransport();
            },
            {}, true, row == loopOut });
    caption("LOOP IN", 547, 55);
    menu(rect(547, y, 55, 22), fmt("%02zu", loopIn), std::move(starts));
    caption("LOOP OUT", 610, 55);
    menu(rect(610, y, 55, 22), fmt("%02zu", loopOut), std::move(ends));
    fill(rect(678, 767, 1, 32), 0x4c4c4c);
    constexpr const char* recordNames[] { "REC OFF", "REC STEP", "REC Q", "REC MT" };
    std::vector<MainMenuItem> recordModes, recordLanes;
    for (int i = 0; i < 4; ++i)
        recordModes.push_back({ recordNames[i],
            [this, i] {
                state_.midiStepRecordMode = static_cast<MidiStepRecordMode>(i);
                if (callbacks_.midiStepRecordModeChanged)
                    callbacks_.midiStepRecordModeChanged(state_.midiStepRecordMode);
                reloadModel();
            },
            {}, true, i == int(state_.midiStepRecordMode) });
    for (std::size_t i = 0; i < state_.session.pattern.tracks.size(); ++i)
        recordLanes.push_back({ fmt("L%02zu · ", i + 1) + state_.session.pattern.tracks[i].name,
            [this, i] {
                state_.midiRecordTrack = i;
                if (callbacks_.midiRecordTrackChanged)
                    callbacks_.midiRecordTrackChanged(i);
                reloadModel();
            },
            {}, true, i == state_.midiRecordTrack });
    caption("RECORD", 682, 75);
    menu(rect(682, y, 75, 22), recordNames[std::clamp(int(state_.midiStepRecordMode), 0, 3)],
        std::move(recordModes), state_.midiStepInputAvailable);
    caption("LANE", 765, 50);
    menu(rect(765, y, 50, 22), fmt("L%02zu", state_.midiRecordTrack + 1), std::move(recordLanes),
        editable && state_.midiStepInputAvailable && !state_.session.pattern.tracks.empty());
}

void MainPageView::paintScrollbars()
{
    auto vp = gridViewport();
    auto* pattern = playbackFollowPattern(&state_);
    double width = app::trackerDocumentWidth(pattern ? pattern->tracks.size() : 0,
        vp.getWidth() / zoom_, state_.sequenceColumnsExpanded);
    double height = 86 + 25 * double(playbackFollowVisibleRows(&state_));
    const auto layout = followLayout();
    if (pinnedHeader() && layout.maximum > layout.minimum) {
        const double available = vp.getHeight() - 86. * zoom_ - 10.;
        const double range = layout.maximum - layout.minimum;
        const double length
            = std::max(24., available * layout.bodyHeight / (range + layout.bodyHeight));
        const double top
            = vp.top + 86. * zoom_ + (scrollY_ - layout.minimum) / range * (available - length);
        fill(rect(vp.right - 8, top, 6, length), 0x777777, .8);
    } else if (!pinnedHeader() && height > vp.getHeight() / zoom_) {
        double length = std::max(24., vp.getHeight() * vp.getHeight() / zoom_ / height);
        double top
            = vp.top + scrollY_ / (height - vp.getHeight() / zoom_) * (vp.getHeight() - length);
        fill(rect(vp.right - 8, top, 6, length), 0x777777, .8);
    }
    if (width > vp.getWidth() / zoom_) {
        double length = std::max(24., vp.getWidth() * vp.getWidth() / zoom_ / width);
        double left = scrollX_ / (width - vp.getWidth() / zoom_) * (vp.getWidth() - length);
        fill(rect(left, vp.bottom - 8, length, 6), 0x777777, .8);
    }
}

void MainPageView::startText(GridTextEdit edit)
{
    finishText();
    if (text_ || !getFrame() || !grid_.editable())
        return;
    pauseFollowing();
    edit_ = std::move(edit);
    cancelText_ = closeText_ = textCommitted_ = textError_ = returnTextFocus_ = false;
    auto b = pageRect(edit_->bounds);
    b.inset(zoom_, zoom_);
    auto* field = new PageTextEdit(b, this, 1, edit_->text.c_str());
    text_ = field;
    field->caretAtEnd = edit_->kind == GridEditKind::Cell;
    field->handle = [this](KeyboardEvent& e) { return handleTextKey(e); };
    auto info = services_.paint.font(11, FontWeight::Semibold, false);
    auto font = services_.fontFactory ? services_.fontFactory(info.name, info.size * zoom_)
                                      : makeOwned<CFontDesc>(info.name.c_str(), info.size * zoom_);
    field->setFont(font);
    field->setFontColor(f::color(0xdededa));
    field->setBackColor(f::color(0x262626));
    field->setFrameColor(f::color(0x4c4c4c));
    field->setHoriAlign(edit_->kind == GridEditKind::TrackName ? kLeftText : kCenterText);
    field->setImmediateTextChange(true);
    field->registerTextEditListener(this);
    getFrame()->addView(field);
    resizeText();
    getFrame()->setFocusView(field);
}
void MainPageView::startConsole()
{
    finishText();
    if (text_ || !getFrame())
        return;
    pauseFollowing();
    edit_.reset();
    cancelText_ = closeText_ = textCommitted_ = textError_ = returnTextFocus_ = false;
    if (services_.consoleDraft)
        consoleText_ = services_.consoleDraft();
    if (services_.consoleHistory)
        history_ = services_.consoleHistory();
    historyIndex_ = -1;
    if (services_.requestNativeFocus)
        services_.requestNativeFocus();
    auto* field = new PageTextEdit(rect(84, 77, 1224, 29), this, 2, consoleText_.c_str());
    text_ = field;
    field->handle = [this](KeyboardEvent& e) { return handleTextKey(e); };
    auto info = services_.paint.font(10, FontWeight::Regular, false);
    info.size += 2;
    field->setFont(services_.fontFactory ? services_.fontFactory(info.name, info.size)
                                         : makeOwned<CFontDesc>(info.name.c_str(), info.size));
    field->setFontColor(f::color(0xdededa));
    field->setBackColor(f::color(0x262626));
    field->setFrameColor(f::color(0x4c4c4c));
    field->setHoriAlign(kLeftText);
    field->setImmediateTextChange(true);
    field->registerTextEditListener(this);
    getFrame()->addView(field);
    getFrame()->setFocusView(field);
    invalid();
}
void MainPageView::setConsoleText(const std::string& text)
{
    consoleText_ = text;
    if (text_ && !edit_) {
        text_->setText(text.c_str());
        static_cast<PageTextEdit*>(text_)->moveCaretToEnd();
    }
    if (services_.consoleDraftChanged)
        services_.consoleDraftChanged(text);
}
void MainPageView::resizeText()
{
    if (!text_ || !edit_)
        return;
    auto b = pageRect(edit_->bounds);
    b.inset(zoom_, zoom_);
    auto* font = text_->getFont();
    auto* painter = font ? font->getFontPainter() : nullptr;
    const double measured = painter
        ? painter->getStringWidth(nullptr, text_->getText().getPlatformString(), true)
        : 0;
    double width = std::min(
        gridViewport().getWidth(), std::max(b.getWidth(), std::ceil(measured) + 22 * zoom_));
    double left
        = std::clamp(b.left + b.getWidth() * .5 - width * .5, 0., gridViewport().right - width);
    b.left = left;
    b.right = left + width;
    text_->setViewSize(b);
    text_->setMouseableArea(b);
}
void MainPageView::valueChanged(CControl* control)
{
    if (control != text_)
        return;
    textCommitted_ = false;
    textError_ = false;
    if (edit_)
        resizeText();
    else {
        consoleText_ = text_->getText().getString();
        if (services_.consoleDraftChanged)
            services_.consoleDraftChanged(consoleText_);
    }
}
bool MainPageView::commitText()
{
    if (!text_ || textCommitted_)
        return true;
    auto value = text_->getText().getString();
    if (edit_ && !grid_.commitText(*edit_, value)) {
        textError_ = true;
        text_->setBackColor(f::color(0x3a2020));
        text_->invalid();
        return false;
    }
    if (!edit_) {
        consoleText_ = value;
        if (services_.consoleDraftChanged)
            services_.consoleDraftChanged(value);
    }
    textCommitted_ = true;
    return true;
}
void MainPageView::finishText(bool cancel)
{
    if (inTextCallback_) {
        closeText_ = true;
        cancelText_ = cancel;
        return;
    }
    if (!text_) {
        closeText_ = cancelText_ = false;
        return;
    }
    if (!cancel && !commitText()) {
        closeText_ = false;
        if (getFrame())
            getFrame()->setFocusView(text_);
        return;
    }
    auto* field = text_;
    text_ = nullptr;
    field->unregisterTextEditListener(this);
    static_cast<PageTextEdit*>(field)->handle = {};
    field->looseFocus();
    if (field->getFrame())
        field->getFrame()->removeView(field);
    edit_.reset();
    closeText_ = cancelText_ = textError_ = textCommitted_ = false;
    invalid();
}
void MainPageView::onTextEditPlatformControlLostFocus(CTextEdit* field)
{
    if (field == text_) {
        closeText_ = true;
        cancelText_ = false;
    }
}
bool MainPageView::handleTextKey(KeyboardEvent& event)
{
    if (event.type != EventType::KeyDown || !text_)
        return false;
    if (event.virt == VirtualKey::Escape) {
        returnTextFocus_ = true;
        closeText_ = cancelText_ = true;
        return true;
    }
    if (!edit_ && (event.virt == VirtualKey::Up || event.virt == VirtualKey::Down)) {
        if (history_.empty())
            return true;
        if (historyIndex_ < 0) {
            consoleDraft_ = text_->getText().getString();
            historyIndex_ = int(history_.size());
        }
        historyIndex_ = std::clamp(
            historyIndex_ + (event.virt == VirtualKey::Up ? -1 : 1), 0, int(history_.size()));
        setConsoleText(historyIndex_ == int(history_.size())
                ? consoleDraft_
                : history_[std::size_t(historyIndex_)]);
        return true;
    }
    if (!edit_ && event.virt == VirtualKey::Tab) {
        auto matches = consoleCompletions(text_->getText().getString());
        if (matches.size() == 1)
            setConsoleText(matches.front() + " ");
        else if (matches.size() > 1 && services_.consoleMessage) {
            std::string message = "matches: ";
            for (const auto& match : matches) {
                if (message.size() > 9)
                    message += ", ";
                message += match;
            }
            services_.consoleMessage(message);
        }
        return true;
    }
    if (event.virt == VirtualKey::Return) {
        returnTextFocus_ = true;
        inTextCallback_ = true;
        if (!edit_) {
            auto text = trimCellText(text_->getText().getString());
            if (!text.empty()) {
                history_.push_back(text);
                if (history_.size() > 100)
                    history_.erase(history_.begin());
                historyIndex_ = -1;
                consoleDraft_.clear();
                if (services_.submitConsole)
                    services_.submitConsole(text);
                else
                    command(text);
                setConsoleText("");
            }
            textCommitted_ = true;
            returnTextFocus_ = false;
            inTextCallback_ = false;
            invalid();
            return true;
        } else
            commitText();
        inTextCallback_ = false;
        if (!textError_) {
            closeText_ = true;
            cancelText_ = false;
        }
        return true;
    }
    return false;
}

void MainPageView::onMouseDownEvent(MouseDownEvent& e)
{
    if (menuPointer(e.mousePosition, true)) {
        e.consumed = true;
        return;
    }
    finishText();
    if (text_) {
        e.consumed = true;
        return;
    }
    focusTracker();
    auto p = e.mousePosition;
    dragOrigin_ = { p.x, p.y };
    auto mods = modifiers(e.modifiers);
    hover_ = p;
    auto vp = gridViewport();
    if (contains(rect(370, 779, 90, 22), p) && (e.buttonState.isRight() || (mods & Alt))) {
        state_.session.transport.swing = .5;
        changedTransport();
        e.consumed = true;
        return;
    }
    if (e.buttonState.isLeft()) {
        for (auto i = hits_.rbegin(); i != hits_.rend(); ++i)
            if (contains(i->bounds, p)) {
                auto action = i->action;
                auto name = i->name;
                if (name == "FILL" && (mods & Shift)) {
                    momentaryFill_ = true;
                    oldFill_ = state_.fillActive;
                    state_.fillActive = true;
                    if (callbacks_.fillChanged)
                        callbacks_.fillChanged(true);
                } else if (action) {
                    if (i->activateOnDown)
                        action();
                    else {
                        pendingButton_ = std::move(action);
                        pendingButtonBounds_ = i->bounds;
                    }
                }
                e.consumed = true;
                invalid();
                return;
            }
        if (contains(envelopeRect(), p)) {
            pauseFollowing();
            dragEnvelope_ = grid_.editable();
            grid_.paintEnvelope({ p.x, p.y }, logical(envelopeRect()), mods & Alt);
            e.consumed = true;
            return;
        }
    }
    if (!contains(vp, p))
        return;
    if (e.buttonState.isLeft() && (p.x >= vp.right - 10 || p.y >= vp.bottom - 10)) {
        pauseFollowing();
        scrollDrag_ = p.x >= vp.right - 10 ? 1 : 2;
        e.consumed = true;
        return;
    }
    Point gp = gridPoint(p);
    // CENTER's leading blank padding is not a second interactive header.
    if (pinnedHeader() && p.y >= vp.top + 86. * zoom_ && gp.y < 86.)
        return;
    if (p.x < app::kTrackerRowNumberWidth * zoom_) {
        if (gp.y < 86)
            return;
        pauseFollowing();
        auto row = std::min<std::size_t>(
            static_cast<std::size_t>((gp.y - 86) / 25), playbackFollowVisibleRows(&state_) - 1);
        if (e.buttonState.isRight()) {
            if (!grid_.selection.active || row < grid_.selection.range().firstRow
                || row > grid_.selection.range().lastRow)
                grid_.select({ state_.session.selectedTrack, state_.session.selectedField, row });
            openMenu(rect(p.x, p.y, 0, 0),
                contextMenu(
                    { state_.session.selectedTrack, state_.session.selectedField, row }, true));
        } else if (grid_.editable()) {
            wholeRowDrag_ = (mods & Shift) != 0;
            loopAnchor_ = wholeRowDrag_ ? (grid_.selectingWholeRows ? grid_.selection.anchorRow
                                                                    : state_.session.selectedRow)
                                        : row;
            if (wholeRowDrag_)
                grid_.selectWholeRows(*loopAnchor_, row);
            else
                grid_.selectLoop(*loopAnchor_, row);
        }
        e.consumed = true;
        return;
    }
    auto address = grid_.addressAt(gp, true);
    if (!address)
        return;
    auto a = *address;
    if (gp.y < 86) {
        selectingHeader_ = pinnedHeader();
        grid_.select({ a.track, a.field, state_.session.selectedRow });
        selectingHeader_ = false;
        if (e.buttonState.isRight() && gp.y < 21) {
            std::vector<MainMenuItem> entries;
            entries.push_back({ "FOLLOW THIS LANE",
                [this, a] {
                    setFollowSource(false, a.track);
                    if (!pinnedHeader())
                        setFollowMode(TrackerFollowMode::Center);
                    resumeFollowing();
                },
                {}, true,
                pinnedHeader() && !state_.trackerFollow.selectedLane
                    && state_.trackerFollow.lane == a.track });
            if (grid_.editable()) {
                entries.push_back({ "", {}, {}, false });
                entries.push_back({ "MOVE LANE LEFT",
                    [this, a] {
                        grid_.moveCompleteLane(int(a.track) - 1);
                        reloadModel();
                    },
                    {}, a.track > 0 });
                entries.push_back({ "MOVE LANE RIGHT",
                    [this, a] {
                        grid_.moveCompleteLane(int(a.track) + 1);
                        reloadModel();
                    },
                    {}, a.track + 1 < state_.session.pattern.tracks.size() });
                entries.push_back({ "", {}, {}, false });
                std::vector<MainMenuItem> destinations;
                for (std::size_t lane = 0; lane < state_.session.pattern.tracks.size(); ++lane)
                    destinations.push_back({ fmt("LANE %02zu", lane + 1),
                        [this, lane] {
                            grid_.moveCompleteLane(int(lane));
                            reloadModel();
                        },
                        {}, lane != a.track });
                entries.push_back({ "MOVE LANE TO", {}, std::move(destinations) });
            }
            openMenu(rect(p.x, p.y, 0, 0), std::move(entries));
        } else if (e.buttonState.isLeft() && grid_.editable()) {
            auto width = gridLaneWidth(state_.sequenceColumnsExpanded),
                 x = gridLaneFieldX(a.track, width), fw = gridLaneFieldWidth(width);
            auto hit = [&](Rect r) {
                return gp.x >= r.x && gp.x < r.x + r.width && gp.y >= r.y && gp.y < r.y + r.height;
            };
            if (hit(gridLaneChannelRect(x, fw))) {
                std::vector<MainMenuItem> channels;
                for (uint8_t n = 1; n <= 16; ++n)
                    channels.push_back({ fmt("CHANNEL %02u", unsigned(n)),
                        [this, a, n] {
                            state_.session.pattern.tracks[a.track].midiChannel = n;
                            grid_.patternChanged();
                        },
                        {}, true, n == state_.session.pattern.tracks[a.track].midiChannel });
                openMenu(pageRect(gridLaneChannelRect(x, fw)), std::move(channels));
            } else if (hit(gridLaneResyncRect(x, fw))) {
                if (callbacks_.resyncTrack)
                    callbacks_.resyncTrack(a.track);
            } else if (e.clickCount >= 2 && gp.y < 22)
                startText(grid_.textEdit(GridEditKind::TrackName));
            else if (gp.y >= 73) {
                auto* c = columnForField(state_.session.pattern.tracks[a.track], 0, a.field);
                c->muted = !c->muted;
                grid_.patternChanged();
            } else if (gp.y >= 60) {
                auto* c = columnForField(state_.session.pattern.tracks[a.track], 0, a.field);
                c->direction = nextDirection(c->direction);
                grid_.patternChanged();
            } else if (e.clickCount >= 2 && gp.y >= 47)
                startText(grid_.textEdit(GridEditKind::ReadStart));
            else if (e.clickCount >= 2 && gp.y >= 34)
                startText(grid_.textEdit(GridEditKind::Length));
            else if (gp.y >= 21 && gp.y < 34 && gridFieldIsSequence(a.field)
                && !gridFieldIsSequenceAction(a.field)) {
                auto& mode = state_.session.pattern.tracks[a.track]
                                 .fxPairs[gridSequencePair(a.field)]
                                 .valueInterpolation;
                mode = mode == ValueInterpolation::Step ? ValueInterpolation::Linear
                                                        : ValueInterpolation::Step;
                grid_.patternChanged();
            }
        }
        e.consumed = true;
        return;
    }
    if (gp.y >= 86 + 25 * double(playbackFollowVisibleRows(&state_)))
        return;
    pauseFollowing();
    if (e.buttonState.isRight()) {
        auto fields = gridFieldCount(state_.sequenceColumnsExpanded);
        bool preserve = grid_.selection.active
            && grid_.selection.containsLinear(0, a.track, a.field, a.row, fields);
        if (!preserve)
            grid_.select(a);
        else {
            state_.session.selectedTrack = a.track;
            state_.session.selectedField = a.field;
            state_.session.selectedRow = a.row;
        }
        if (grid_.editable())
            openMenu(rect(p.x, p.y, 0, 0), contextMenu(a));
        e.consumed = true;
        return;
    }
    if (!e.buttonState.isLeft())
        return;
    bool sameColumn
        = a.track == state_.session.selectedTrack && a.field == state_.session.selectedField;
    grid_.select(a, (mods & Shift) && sameColumn);
    if (!grid_.editable()) {
        e.consumed = true;
        return;
    }
    dragAddress_ = a;
    dragOrigin_ = gp;
    dragSelection_ = true;
    bool condition = false;
    if (gridFieldIsSequence(a.field) && !gridFieldIsSequenceAction(a.field)) {
        auto& pair = state_.session.pattern.tracks[a.track].fxPairs[gridSequencePair(a.field)];
        condition = a.row < pair.actions.size()
            && pair.actions[a.row].state == FxActionCellState::Sequencer
            && pair.actions[a.row].sequencerAction == SequencerAction::Condition;
    }
    if (e.clickCount >= 2) {
        dragSelection_ = false;
        if (condition)
            openMenu(rect(p.x, p.y, 0, 0), contextMenu(a));
        else
            grid_.beginCellEditing();
    } else if (!(mods & Control) && !condition
        && (a.field == 1 || (gridFieldIsSequence(a.field) && !gridFieldIsSequenceAction(a.field))))
        dragValueCandidate_ = true;
    e.consumed = true;
}
void MainPageView::onMouseMoveEvent(MouseMoveEvent& e)
{
    for (const auto& hit : hits_)
        if (contains(hit.bounds, hover_) != contains(hit.bounds, e.mousePosition))
            invalidRect(hit.bounds);
    hover_ = e.mousePosition;
    std::string tip;
    for (const auto& hit : hits_)
        if (contains(hit.bounds, hover_)) {
            tip = hit.name;
            if (tip == "FILL")
                tip = "Click to latch FILL; Shift-hold for momentary FILL";
            else if (tip == "SYNC ALL")
                tip = "Force every lane and column to row 1, ignoring phase, without "
                      "stopping "
                      "REAPER";
            else if (tip == "Swing")
                tip = "Drag horizontally or scroll: 50–75%. Option-scroll for fine "
                      "steps; "
                      "Option-click to reset";
            else if (tip == "Live Code")
                tip = "Type a Tracker command; Return executes; Up/Down recall command "
                      "history; "
                      "Escape returns to the grid";
            else if (tip == "NAME")
                tip = "Rename the active pattern without changing its stable ID";
            else if (tip == "DUP")
                tip = "Duplicate the active pattern";
            else if (tip == "! PANIC")
                tip = "Send tracked Note Offs and CC 123 All Notes Off on MIDI "
                      "channels 1–16";
            break;
        }
    if (contains(envelopeRect(), hover_))
        tip = "Drag to paint the selected value column; Option-click writes "
              "Previous (Default for "
              "GATE). Song playback is read-only";
    if (contains(rect(950, 12, 126, 17), hover_))
        tip = "Playback follow: STATIC keeps the view still; CENTER holds the NOTE cursor at the "
              "center; "
              "PAGE advances complete groups (16, or fewer when zoomed in)";
    else if (contains(rect(1082, 12, 140, 17), hover_))
        tip = "Follow one NOTE lane, including rests. Pin a lane number or follow the selected "
              "lane; "
              "other polymetric cursors remain independent";
    else if (contains(rect(1228, 12, 66, 17), hover_))
        tip = !followLane() ? "Source lane is absent from this pattern; choose another NOTE lane"
                            : followPaused_
                ? "Following is held for manual navigation/editing. Click RESUME to follow again"
                : "Manual scrolling or cell editing holds following until RESUME";
    setTooltipText(tip.empty() ? nullptr : tip.c_str());
    if (menuPointer(e.mousePosition, false)) {
        e.consumed = true;
        return;
    }
    auto p = e.mousePosition;
    auto mods = modifiers(e.modifiers);
    auto vp = gridViewport();
    if (dragSwing_) {
        state_.session.transport.swing
            = std::round(std::clamp(.5 + (p.x - 372) / 48 * .25, .5, .75) * 1000) / 1000;
        invalidRect(rect(370, 779, 90, 22));
        e.consumed = true;
        return;
    }
    if (scrollDrag_) {
        auto* pattern = playbackFollowPattern(&state_);
        auto width = app::trackerDocumentWidth(pattern ? pattern->tracks.size() : 0,
            vp.getWidth() / zoom_, state_.sequenceColumnsExpanded);
        auto height = 86 + 25 * double(playbackFollowVisibleRows(&state_));
        if (scrollDrag_ == 1) {
            if (pinnedHeader()) {
                const auto layout = followLayout();
                const auto available = vp.getHeight() - 86. * zoom_ - 10.;
                const auto range = layout.maximum - layout.minimum;
                const auto length
                    = std::max(24., available * layout.bodyHeight / (range + layout.bodyHeight));
                const auto fraction
                    = (p.y - vp.top - 86. * zoom_ - length * .5) / std::max(1., available - length);
                scrollTo(scrollX_, layout.minimum + fraction * range);
            } else
                scrollTo(scrollX_,
                    (p.y - vp.top) / vp.getHeight() * height - vp.getHeight() / zoom_ * .5);
        } else
            scrollTo(p.x / vp.getWidth() * width - vp.getWidth() / zoom_ * .5, scrollY_);
        e.consumed = true;
        return;
    }
    if (dragEnvelope_) {
        grid_.paintEnvelope({ p.x, p.y }, logical(envelopeRect()), false);
        e.consumed = true;
        return;
    }
    if (!grid_.editable())
        return;
    if (loopAnchor_) {
        auto gp = gridPoint(p);
        auto row = static_cast<std::size_t>(
            std::clamp(std::floor((gp.y - 86) / 25), 0., double(visibleRows(&state_) - 1)));
        if (wholeRowDrag_)
            grid_.selectWholeRows(*loopAnchor_, row);
        else
            grid_.selectLoop(*loopAnchor_, row);
        e.consumed = true;
        return;
    }
    auto gp = gridPoint(p);
    if (dragValueCandidate_ && !dragValue_ && std::abs(gp.y - dragOrigin_.y) >= 2
        && std::abs(gp.y - dragOrigin_.y) >= std::abs(gp.x - dragOrigin_.x)) {
        dragValue_ = true;
        dragSelection_ = false;
        grid_.clearGridSelection();
        grid_.beginValueDrag(*dragAddress_);
    }
    if (dragValue_) {
        grid_.updateValueDrag(dragOrigin_.y - gp.y, mods & Alt, mods & Shift);
        e.consumed = true;
        return;
    }
    if (dragSelection_) {
        if (auto a = grid_.addressAt(gp, true))
            grid_.select(*a, true);
        e.consumed = true;
    }
}
void MainPageView::onMouseUpEvent(MouseUpEvent& e)
{
    auto buttonAction = std::move(pendingButton_);
    pendingButton_ = {};
    if (buttonAction && contains(pendingButtonBounds_, e.mousePosition))
        buttonAction();
    if (dragValue_)
        grid_.finishValueDrag();
    if (dragSwing_ && state_.session.transport.swing != swingStart_)
        changedTransport();
    if (momentaryFill_) {
        state_.fillActive = oldFill_;
        if (callbacks_.fillChanged)
            callbacks_.fillChanged(oldFill_);
    }
    dragSelection_ = dragValueCandidate_ = dragValue_ = dragEnvelope_ = dragSwing_ = momentaryFill_
        = false;
    scrollDrag_ = 0;
    loopAnchor_.reset();
    dragAddress_.reset();
    invalid();
    e.consumed = true;
}
void MainPageView::onMouseWheelEvent(MouseWheelEvent& e)
{
    if (!popups_.empty()) {
        for (auto i = popups_.rbegin(); i != popups_.rend(); ++i)
            if (contains(i->bounds, e.mousePosition)) {
                if (i->columns == 1)
                    i->scroll = std::clamp(i->scroll + (e.deltaY < 0 ? 1 : -1), 0,
                        std::max(0, int(i->items.size()) - i->rows));
                invalid();
                break;
            }
        e.consumed = true;
        return;
    }
    if (contains(rect(370, 779, 90, 22), e.mousePosition)) {
        auto delta = e.deltaY != 0 ? e.deltaY : e.deltaX;
        if (delta != 0) {
            double step = e.modifiers.has(ModifierKey::Alt) ? .001 : .005;
            state_.session.transport.swing
                = std::round(std::clamp(state_.session.transport.swing + (delta > 0 ? step : -step),
                                 .5, .75)
                      * 1000)
                / 1000;
            changedTransport();
        }
        e.consumed = true;
        return;
    }
    if (contains(gridViewport(), e.mousePosition)) {
        finishText();
        auto mods = modifiers(e.modifiers);
        if (mods & Control)
            setGridZoom(zoom_ + e.deltaY * .05);
        else
            scrollTo(scrollX_ - (mods & Shift ? e.deltaY : e.deltaX) * 40 / zoom_,
                scrollY_ - (mods & Shift ? 0 : e.deltaY) * 40 / zoom_);
        e.consumed = true;
    }
}
void MainPageView::onZoomGestureEvent(ZoomGestureEvent& e)
{
    if (contains(gridViewport(), e.mousePosition)) {
        setGridZoom(zoom_ * (1. + e.zoom));
        e.consumed = true;
    }
}
void MainPageView::onKeyboardEvent(KeyboardEvent& e)
{
    if (e.type != EventType::KeyDown)
        return;
    if (text_)
        return;
    if (!popups_.empty()) {
        auto level = popups_.size() - 1;
        auto& p = popups_.back();
        if (e.virt == VirtualKey::Escape || e.virt == VirtualKey::Left) {
            popups_.pop_back();
            invalid();
            e.consumed = true;
            return;
        }
        if (e.virt == VirtualKey::Up || e.virt == VirtualKey::Down) {
            int direction = e.virt == VirtualKey::Up ? -1 : 1;
            int row = p.hover;
            for (std::size_t i = 0; i < p.items.size(); ++i) {
                row = (row + direction + int(p.items.size())) % int(p.items.size());
                if (p.items[std::size_t(row)].enabled)
                    break;
            }
            p.hover = row;
            int visible = p.rows * p.columns;
            if (row < p.scroll)
                p.scroll = row;
            if (row >= p.scroll + visible)
                p.scroll = row - visible + 1;
            invalid();
            e.consumed = true;
            return;
        }
        if (e.virt == VirtualKey::Return || e.virt == VirtualKey::Right) {
            activateMenu(level, std::max(0, p.hover));
            e.consumed = true;
            return;
        }
        e.consumed = true;
        return;
    }
    GridKeyInput key;
    key.modifiers = modifiers(e.modifiers);
    key.visibleHeight = gridViewport().getHeight() / zoom_;
    if (e.character > 0 && e.character < 128)
        key.text = std::string(1, static_cast<char>(e.character));
    switch (e.virt) {
    case VirtualKey::Left:
        key.key = GridKey::Left;
        break;
    case VirtualKey::Right:
        key.key = GridKey::Right;
        break;
    case VirtualKey::Up:
        key.key = GridKey::Up;
        break;
    case VirtualKey::Down:
        key.key = GridKey::Down;
        break;
    case VirtualKey::Home:
        key.key = GridKey::Home;
        break;
    case VirtualKey::End:
        key.key = GridKey::End;
        break;
    case VirtualKey::PageUp:
        key.key = GridKey::PageUp;
        break;
    case VirtualKey::PageDown:
        key.key = GridKey::PageDown;
        break;
    case VirtualKey::F9:
        key.key = GridKey::F9;
        break;
    case VirtualKey::F10:
        key.key = GridKey::F10;
        break;
    case VirtualKey::F11:
        key.key = GridKey::F11;
        break;
    case VirtualKey::F12:
        key.key = GridKey::F12;
        break;
    case VirtualKey::Back:
        key.key = GridKey::Backspace;
        break;
    case VirtualKey::Delete:
        key.key = GridKey::Delete;
        break;
    case VirtualKey::Tab:
        key.key = GridKey::Tab;
        break;
    case VirtualKey::Return:
        key.text = "\r";
        break;
    case VirtualKey::Space:
        key.text = " ";
        break;
    default:
        break;
    }
    if (key.text == "=" || key.text == "+")
        key.key = GridKey::Plus;
    if (key.text == "-")
        key.key = GridKey::Minus;
    if (key.text == "0")
        key.key = GridKey::Zero;
    if (grid_.keyDown(key)) {
        if (key.text != " " && key.key != GridKey::Plus && key.key != GridKey::Minus
            && key.key != GridKey::Zero)
            pauseFollowing();
        invalid();
        e.consumed = true;
    }
}
} // namespace s3g::tracker::editor
