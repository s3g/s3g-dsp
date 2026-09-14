#include "s3g_tracker_song_page_host.h"
#include "s3g_tracker_vstgui_pilot.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/platform/platform_macos.h"
#include <cmath>

using namespace s3g::tracker::editor;
namespace foundation = s3g::portable_gui::foundation;

@implementation S3GTrackerSongPageHost {
    VSTGUI::CFrame* _frame;
    SongPageView* _page;
    BOOL _runtime, _opened;
}
- (instancetype)initWithFrame:(NSRect)rect
{
    self = [super initWithFrame:rect];
    if (!self)
        return nil;
    _runtime = foundation::acquireRuntime();
    if (!_runtime)
        return nil;
    ToolPageServices services;
    services.font = [](double size) {
        auto info = macTrackerSuiteFont(size);
        NSFont* font = [NSFont fontWithName:@(info.name.c_str()) size:size];
        if (font)
            info.lineHeight = [@"H" sizeWithAttributes:@{ NSFontAttributeName : font }].height;
        return info;
    };
    services.color = macGridPaintServices().color;
    services.fontFactory = macTrackerFontFactory();
    __weak S3GTrackerSongPageHost* weakSelf = self;
    services.requestNativeFocus = [weakSelf] {
        auto* host = weakSelf;
        if (!host || !host.window || host.hiddenOrHasHiddenAncestor || !host.subviews.count)
            return;
        auto* native = host.subviews.firstObject;
        [host.window makeFirstResponder:native];
        if (host.window.firstResponder == native && host->_frame)
            static_cast<VSTGUI::IPlatformFrameCallback*>(host->_frame)->platformOnActivate(true);
    };
    _frame = new VSTGUI::CFrame({ 0, 0, NSWidth(rect), NSHeight(rect) }, nullptr);
    _frame->enableTooltips(true);
    _page = new SongPageView(std::move(services));
    _frame->addView(_page);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.accessibilityLabel = @"Tracker portable Song page";
    [self resizeFrame];
    return self;
}
- (BOOL)isFlipped
{
    return YES;
}
- (SongPageView*)page
{
    return _page;
}
- (void)resizeFrame
{
    if (!_frame || NSWidth(self.bounds) <= 0 || NSHeight(self.bounds) <= 0)
        return;
    double width = NSWidth(self.bounds), height = NSHeight(self.bounds);
    double scale = std::min({ 1., width / 980., height / 408. });
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
