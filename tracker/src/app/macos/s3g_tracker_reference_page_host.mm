#include "s3g_tracker_reference_page_host.h"
#include "s3g_tracker_controls.h"
#include "s3g_tracker_vstgui_pilot.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/platform/platform_macos.h"
#import <CoreText/CoreText.h>
#include <cmath>

using namespace s3g::tracker::editor;
namespace foundation = s3g::portable_gui::foundation;

@implementation S3GTrackerReferencePageHost {
    VSTGUI::CFrame* _frame;
    ReferencePageView* _page;
    BOOL _runtime, _opened;
}
- (instancetype)initWithFrame:(NSRect)rect
                      console:(std::shared_ptr<ConsoleModel>)console
                     services:(ReferencePageServices)services
{
    self = [super initWithFrame:rect];
    if (!self)
        return nil;
    _runtime = foundation::acquireRuntime();
    if (!_runtime)
        return nil;
    services.tools.font = [](double size) {
        auto info = macTrackerSuiteFont(size);
        NSFont* font = [NSFont fontWithName:@(info.name.c_str()) size:size];
        if (font)
            info.lineHeight = [@"H" sizeWithAttributes:@{ NSFontAttributeName : font }].height;
        return info;
    };
    services.tools.color = macGridPaintServices().color;
    services.tools.fontFactory = macTrackerFontFactory();
    __weak S3GTrackerReferencePageHost* weakSelf = self;
    services.tools.requestNativeFocus = [weakSelf] {
        auto* host = weakSelf;
        if (!host || !host.window || host.hiddenOrHasHiddenAncestor || !host.subviews.count)
            return;
        auto* native = host.subviews.firstObject;
        [host.window makeFirstResponder:native];
        if (host.window.firstResponder == native && host->_frame)
            static_cast<VSTGUI::IPlatformFrameCallback*>(host->_frame)->platformOnActivate(true);
    };
    services.font = [](const ReferenceStyle& style) {
        NSFont* font = S3GTrackerFont(style.size,
            style.weight == FontWeight::Semibold
                ? NSFontWeightSemibold
                : style.weight == FontWeight::Medium ? NSFontWeightMedium : NSFontWeightRegular);
        NSLayoutManager* layout = [[NSLayoutManager alloc] init];
        return GridFont { font.fontName.UTF8String, font.pointSize,
            [layout defaultBaselineOffsetForFont:font], [layout defaultLineHeightForFont:font] };
    };
    services.fallbackFont = [](std::string_view text, const ReferenceStyle& style) {
        NSFont* font = S3GTrackerFont(style.size,
            style.weight == FontWeight::Semibold
                ? NSFontWeightSemibold
                : style.weight == FontWeight::Medium ? NSFontWeightMedium : NSFontWeightRegular);
        NSString* string = [[NSString alloc] initWithBytes:text.data()
                                                    length:text.size()
                                                  encoding:NSUTF8StringEncoding];
        CTFontRef fallback = CTFontCreateForString(
            (__bridge CTFontRef)font, (__bridge CFStringRef)string, CFRangeMake(0, string.length));
        NSFont* resolved = (__bridge NSFont*)fallback;
        NSLayoutManager* layout = [[NSLayoutManager alloc] init];
        GridFont result { resolved.fontName.UTF8String, resolved.pointSize,
            [layout defaultBaselineOffsetForFont:resolved],
            [layout defaultLineHeightForFont:resolved] };
        CFRelease(fallback);
        return result;
    };
    if (!services.closeHelp)
        services.closeHelp = [weakSelf] { [weakSelf.window performClose:nil]; };
    BOOL isConsole = bool(console);
    _frame = new VSTGUI::CFrame({ 0, 0, NSWidth(rect), NSHeight(rect) }, nullptr);
    _frame->enableTooltips(true);
    _page = new ReferencePageView(std::move(console), std::move(services));
    _frame->addView(_page);
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.accessibilityLabel
        = isConsole ? @"Tracker portable Console page" : @"Tracker portable Help page";
    [self resizeFrame];
    return self;
}
- (BOOL)isFlipped
{
    return YES;
}
- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if (macTrackerTextKeyEquivalent(self, _frame, event))
        return YES;
    return [super performKeyEquivalent:event];
}
- (BOOL)s3gTrackerHasFocusedTextInput
{
    return macTrackerHasFocusedTextInput(self, _frame);
}
- (ReferencePageView*)page
{
    return _page;
}
- (void)resizeFrame
{
    if (!_frame || NSWidth(self.bounds) <= 0 || NSHeight(self.bounds) <= 0)
        return;
    double width = NSWidth(self.bounds), height = NSHeight(self.bounds);
    double scale = std::min({ 1., width / 480., height / 360. });
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
- (void)refresh
{
    if (_page)
        _page->refresh();
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
