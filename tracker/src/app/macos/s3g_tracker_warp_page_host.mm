#include "s3g_tracker_warp_page_host.h"
#include "s3g_tracker_vstgui_pilot.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/platform/platform_macos.h"
#include <cmath>

using namespace s3g::tracker::editor;
namespace foundation = s3g::portable_gui::foundation;

@implementation S3GTrackerWarpPageHost {
    VSTGUI::CFrame* _frame;
    WarpPageView* _page;
    BOOL _runtime, _opened;
}
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState*)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks
{
    self = [super initWithFrame:NSMakeRect(0, 0, 820, 580)];
    if (!self)
        return nil;
    _runtime = foundation::acquireRuntime();
    if (!_runtime)
        return nil;
    WarpPageServices services;
    services.font = [](double size) {
        auto info = macTrackerSuiteFont(size);
        // Warps' suite labels/buttons center NSString's measured line box,
        // which is not the rounded ascender+descender box used by grid cells.
        NSFont* font = [NSFont fontWithName:@(info.name.c_str()) size:size];
        if (font)
            info.lineHeight = [@"H" sizeWithAttributes:@{ NSFontAttributeName : font }].height;
        return info;
    };
    services.color = macGridPaintServices().color;
    services.fontFactory = macTrackerFontFactory();
    services.error = [] { NSBeep(); };
    __weak S3GTrackerWarpPageHost* weakSelf = self;
    services.requestNativeFocus = [weakSelf] {
        auto* host = weakSelf;
        if (!host || !host.window || host.hiddenOrHasHiddenAncestor || !host.subviews.count)
            return;
        auto* native = host.subviews.firstObject;
        [host.window makeFirstResponder:native];
        if (host.window.firstResponder == native && host->_frame)
            static_cast<VSTGUI::IPlatformFrameCallback*>(host->_frame)->platformOnActivate(true);
    };
    _frame = new VSTGUI::CFrame({ 0, 0, 820, 580 }, nullptr);
    _frame->enableTooltips(true);
    _page = new WarpPageView(*state, *callbacks, std::move(services));
    _frame->addView(_page);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.accessibilityLabel = @"Tracker portable Warps page";
    [self resizeFrame];
    return self;
}
- (BOOL)isFlipped
{
    return YES;
}
- (WarpPageView*)page
{
    return _page;
}
- (BOOL)s3gTrackerHasFocusedTextInput
{
    return macTrackerHasFocusedTextInput(self, _frame);
}
- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    return macTrackerTextKeyEquivalent(self, _frame, event)
        || [super performKeyEquivalent:event];
}
- (NSRect)warpControlRect:(NSString*)identifier
{
    if (!_page) return NSZeroRect;
    const auto r = _page->controlBounds(identifier.UTF8String);
    const double scale = NSWidth(self.bounds) / _page->getViewSize().getWidth();
    return NSMakeRect(r.left * scale, r.top * scale,
        r.getWidth() * scale, r.getHeight() * scale);
}
- (void)resizeFrame
{
    if (!_frame || NSWidth(self.bounds) <= 0 || NSHeight(self.bounds) <= 0)
        return;
    double width = NSWidth(self.bounds), height = NSHeight(self.bounds);
    // The original tool reflows independently when detached. Keep that behavior
    // at ordinary sizes. Below its usable authoring canvas, scale proportionally
    // so the shared 480x360 pop-out minimum cannot clip the bottom controls.
    double scale = std::min({ 1., width / 720., height / 580. });
    _frame->setSize(width, height);
    _frame->setTransform(VSTGUI::CGraphicsTransform().scale(scale, scale));
    if (std::abs(_page->getViewSize().getWidth() - width / scale) > .001
        || std::abs(_page->getViewSize().getHeight() - height / scale) > .001)
        _page->resize(width / scale, height / scale);
    _frame->invalid();
}
- (void)setFrameSize:(NSSize)size
{
    [super setFrameSize:size];
    [self resizeFrame];
}
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if (!_frame)
        return;
    if (self.window && !_opened) {
        VSTGUI::CocoaFrameConfig config;
        config.flags = VSTGUI::CocoaFrameConfig::kNoCALayer;
        _opened = _frame->open((__bridge void*)self, VSTGUI::PlatformType::kNSView, &config);
    }
    [self resizeFrame];
    _frame->setVisible(self.window && !self.hidden);
}
- (void)setHidden:(BOOL)hidden
{
    [super setHidden:hidden];
    if (_frame)
        _frame->setVisible(!hidden && self.window);
    if (hidden && _page)
        _page->stopRefresh();
}
- (void)refreshPlaybackDisplay
{
    if (!_page)
        return;
    self.accessibilityValue = @(_page->editor().playbackDescription().c_str());
    // Use the reparented view's actual visibility, not the original controller's
    // window. Song/main-page selection must not freeze a detached Warps curve.
    _page->refreshPlaybackDisplay(self.window.visible && !self.hiddenOrHasHiddenAncestor);
}
- (void)dealloc
{
    if (_page)
        _page->stopRefresh();
    if (_frame) {
        if (_opened)
            _frame->close();
        else
            _frame->forget();
    }
    _frame = nullptr;
    _page = nullptr;
    if (_runtime)
        foundation::releaseRuntime();
}
@end
