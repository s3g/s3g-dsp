#include "s3g_tracker_main_page_host.h"
#include "s3g_tracker_vstgui_pilot.h"

using namespace s3g::tracker::editor;
using s3g::portable_gui::foundation::EditorHost;

@implementation S3GTrackerMainPageHost {
    std::unique_ptr<EditorHost> _host;
    MainPageView* _page;
    BOOL _attached;
}
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState*)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks*)callbacks
                     services:(MainPageServices)services
{
    self = [super initWithFrame:NSMakeRect(0, 0, 1320, 820)];
    if (!self)
        return nil;
    services.paint = macGridPaintServices();
    services.fontFactory = macTrackerFontFactory();
    services.suiteFont = macTrackerSuiteFont;
    services.grid.clipboardRevision
        = [] { return static_cast<uint64_t>(NSPasteboard.generalPasteboard.changeCount); };
    __weak S3GTrackerMainPageHost* weakSelf = self;
    services.requestNativeFocus = [weakSelf] {
        S3GTrackerMainPageHost* host = weakSelf;
        if (!host || !host.window || host.hiddenOrHasHiddenAncestor || host.subviews.count == 0)
            return;
        NSView* frameView = host.subviews.firstObject;
        [host.window makeFirstResponder:frameView];
        // The mixed Cocoa/VSTGUI shell can already have this NSView as its
        // first responder while CFrame has no active focus (e.g. reparenting).
        // Reconcile the native focus explicitly; never activate another window.
        if (host.window.firstResponder == frameView && host.page->getFrame())
            static_cast<VSTGUI::IPlatformFrameCallback*>(host.page->getFrame())
                ->platformOnActivate(true);
    };
    _host = std::make_unique<EditorHost>(1320, 820, 1320, 820);
    if (!_host->ready())
        return nil;
    _page = new MainPageView(*state, *callbacks, std::move(services));
    if (!_host->attach(_page)) {
        _page = nullptr;
        return nil;
    }
    self.accessibilityLabel = @"Tracker portable main page";
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    return self;
}
- (BOOL)isFlipped
{
    return YES;
}
- (BOOL)performKeyEquivalent:(NSEvent*)event
{
    if (macTrackerTextKeyEquivalent(self, _page ? _page->getFrame() : nullptr, event))
        return YES;
    return [super performKeyEquivalent:event];
}
- (BOOL)s3gTrackerHasFocusedTextInput
{
    return macTrackerHasFocusedTextInput(self, _page ? _page->getFrame() : nullptr);
}
- (MainPageView*)page
{
    return _page;
}
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    if (!_host)
        return;
    if (self.window && !_attached)
        _attached = _host->setParent((__bridge void*)self);
    if (_attached)
        _host->setVisible(self.window && !self.hidden);
}
- (void)setHidden:(BOOL)hidden
{
    [super setHidden:hidden];
    if (_host && _attached)
        _host->setVisible(!hidden && self.window);
}
- (void)dealloc
{
    // Close CFrame while its native parent and model still exist.
    _host.reset();
    _page = nullptr;
}
@end
