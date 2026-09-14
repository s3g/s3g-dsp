#include "s3g_tracker_authoring_page_host.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platform_macos.h"
using namespace s3g::tracker::editor;
namespace foundation = s3g::portable_gui::foundation;
@implementation S3GTrackerAuthoringPageHost {
  VSTGUI::CFrame *_frame;
  AuthoringPageView *_page;
  BOOL _runtime, _opened;
}
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks *)callbacks
                         kind:(AuthoringPage)kind {
  self = [super initWithFrame:NSMakeRect(0, 0, 1320, 820)];
  if (!self)
    return nil;
  _runtime = foundation::acquireRuntime();
  if (!_runtime)
    return nil;
  AuthoringServices services;
  services.grid = macGridPaintServices();
  services.tools.color = services.grid.color;
  services.tools.fontFactory = macTrackerFontFactory();
  services.tools.font = [](double size) {
    auto f = macTrackerSuiteFont(size);
    NSFont *font = [NSFont fontWithName:@(f.name.c_str()) size:size];
    if (font)
      f.lineHeight =
          [@"H" sizeWithAttributes:@{NSFontAttributeName : font}].height;
    return f;
  };
  services.error = [] { NSBeep(); };
  services.clipboardRevision = [] {
    return uint64_t(NSPasteboard.generalPasteboard.changeCount);
  };
  __weak S3GTrackerAuthoringPageHost *weakSelf = self;
  services.tools.requestNativeFocus = [weakSelf] {
    auto *host = weakSelf;
    if (!host || !host.window || host.hiddenOrHasHiddenAncestor ||
        !host.subviews.count)
      return;
    auto *native = host.subviews.firstObject;
    [host.window makeFirstResponder:native];
    if (host.window.firstResponder == native && host->_frame)
      static_cast<VSTGUI::IPlatformFrameCallback *>(host->_frame)
          ->platformOnActivate(true);
  };
  _frame = new VSTGUI::CFrame({0, 0, 1320, 820}, nullptr);
  _frame->enableTooltips(true);
  _page = new AuthoringPageView(*state, *callbacks, kind, std::move(services));
  _frame->addView(_page);
  self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  self.accessibilityRole = NSAccessibilityGroupRole;
  self.accessibilityLabel = kind == AuthoringPage::Phrases
                                ? @"Tracker portable Phrases page"
                                : kind == AuthoringPage::Assemble
                                      ? @"Tracker portable Assemble page"
                                      : @"Tracker portable Reshape page";
  return self;
}
- (BOOL)isFlipped {
  return YES;
}
- (AuthoringPageView *)page {
  return _page;
}
- (NSRect)authoringControlRect:(NSString *)identifier {
  if (!_page)
    return NSZeroRect;
  auto r = _page->controlBounds(identifier.UTF8String);
  double scale = NSWidth(self.bounds) / _page->getViewSize().getWidth();
  return NSMakeRect(r.left * scale, r.top * scale, r.getWidth() * scale,
                    r.getHeight() * scale);
}
- (NSRect)authoringPopupItemRect:(NSUInteger)index {
  if (!_page)
    return NSZeroRect;
  auto r = _page->popupItemBounds(index);
  double scale = NSWidth(self.bounds) / _page->getViewSize().getWidth();
  return NSMakeRect(r.left * scale, r.top * scale, r.getWidth() * scale,
                    r.getHeight() * scale);
}
- (void)resizeFrame {
  if (!_frame || !_page || NSWidth(self.bounds) <= 0 ||
      NSHeight(self.bounds) <= 0)
    return;
  double width = NSWidth(self.bounds), height = NSHeight(self.bounds);
  // Maintain the original responsive layout at normal sizes. Below a usable
  // authoring canvas, fit proportionally instead of clipping bottom rows.
  double minimumWidth = _page->kind() == AuthoringPage::Reshape ? 1080 : 960;
  double minimumHeight = _page->kind() == AuthoringPage::Assemble ? 820 : 780;
  double scale = std::min({1., width / minimumWidth, height / minimumHeight});
  _frame->setSize(width, height);
  _frame->setTransform(VSTGUI::CGraphicsTransform().scale(scale, scale));
  if (std::abs(_page->getViewSize().getWidth() - width / scale) > .001 ||
      std::abs(_page->getViewSize().getHeight() - height / scale) > .001)
    _page->resize(width / scale, height / scale);
  _frame->invalid();
}
- (void)setFrameSize:(NSSize)size {
  [super setFrameSize:size];
  [self resizeFrame];
}
- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  if (!_frame)
    return;
  if (self.window && !_opened) {
    VSTGUI::CocoaFrameConfig config;
    config.flags = VSTGUI::CocoaFrameConfig::kNoCALayer;
    _opened = _frame->open((__bridge void *)self, VSTGUI::PlatformType::kNSView,
                           &config);
  }
  [self resizeFrame];
  _frame->setVisible(self.window && !self.hidden);
  if (!self.window)
    [self suspendEditing];
}
- (void)setHidden:(BOOL)hidden {
  [super setHidden:hidden];
  if (_frame)
    _frame->setVisible(!hidden && self.window);
  if (hidden)
    [self suspendEditing];
}
- (void)suspendEditing {
  if (_page)
    _page->stopRefresh();
}
- (void)reloadModel {
  if (_page)
    _page->reloadModel();
}
- (void)refreshPlaybackDisplay {
  if (!_page)
    return;
  if (self.hiddenOrHasHiddenAncestor || !self.window.visible ||
      self.window.miniaturized)
    [self suspendEditing];
  else
    _page->refreshPlaybackDisplay();
}
- (BOOL)s3gTrackerHasFocusedTextInput {
  return macTrackerHasFocusedTextInput(self, _frame);
}
- (BOOL)authoringKey:(NSEvent *)event {
  if (!_page || self.hiddenOrHasHiddenAncestor ||
      event.type != NSEventTypeKeyDown)
    return NO;
  if (_page->textActive())
    return macTrackerTextKeyEquivalent(self, _frame, event);
  VSTGUI::KeyboardEvent e;
  e.type = VSTGUI::EventType::KeyDown;
  if (event.modifierFlags & NSEventModifierFlagCommand)
    e.modifiers.add(VSTGUI::ModifierKey::Super);
  if (event.modifierFlags & NSEventModifierFlagControl)
    e.modifiers.add(VSTGUI::ModifierKey::Control);
  if (event.modifierFlags & NSEventModifierFlagOption)
    e.modifiers.add(VSTGUI::ModifierKey::Alt);
  if (event.modifierFlags & NSEventModifierFlagShift)
    e.modifiers.add(VSTGUI::ModifierKey::Shift);
  NSString *text = event.charactersIgnoringModifiers;
  if (text.length == 1)
    e.character = [text characterAtIndex:0];
  switch (event.keyCode) {
  case 36:
    e.virt = VSTGUI::VirtualKey::Return;
    break;
  case 48:
    e.virt = VSTGUI::VirtualKey::Tab;
    break;
  case 49:
    e.virt = VSTGUI::VirtualKey::Space;
    break;
  case 51:
    e.virt = VSTGUI::VirtualKey::Back;
    break;
  case 53:
    e.virt = VSTGUI::VirtualKey::Escape;
    break;
  case 117:
    e.virt = VSTGUI::VirtualKey::Delete;
    break;
  case 123:
    e.virt = VSTGUI::VirtualKey::Left;
    break;
  case 124:
    e.virt = VSTGUI::VirtualKey::Right;
    break;
  case 125:
    e.virt = VSTGUI::VirtualKey::Down;
    break;
  case 126:
    e.virt = VSTGUI::VirtualKey::Up;
    break;
  default:
    break;
  }
  _page->onKeyboardEvent(e);
  return bool(e.consumed);
}
- (BOOL)s3gHandlePhraseKeyEquivalent:(NSEvent *)event {
  return _page && _page->kind() == AuthoringPage::Phrases
             ? [self authoringKey:event]
             : NO;
}
- (BOOL)s3gHandleAssembleKeyEquivalent:(NSEvent *)event {
  return _page && _page->kind() == AuthoringPage::Assemble
             ? [self authoringKey:event]
             : NO;
}
- (BOOL)performKeyEquivalent:(NSEvent *)event {
  return [self authoringKey:event] || [super performKeyEquivalent:event];
}
- (void)dealloc {
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
@implementation S3GTrackerPortablePhraseController
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:
                        (s3g::tracker::app::WorkspaceCallbacks *)callbacks {
  self = [super initWithNibName:nil bundle:nil];
  if (self)
    self.view = [[S3GTrackerAuthoringPageHost alloc]
        initWithState:state
            callbacks:callbacks
                 kind:AuthoringPage::Phrases];
  return self;
}
- (void)reloadModel {
  [(S3GTrackerAuthoringPageHost *)self.view reloadModel];
}
- (void)refreshPlaybackDisplay {
  [(S3GTrackerAuthoringPageHost *)self.view refreshPlaybackDisplay];
}
- (BOOL)captureTrack:(std::size_t)t
            firstRow:(std::size_t)first
             lastRow:(std::size_t)last {
  auto *p = [(S3GTrackerAuthoringPageHost *)self.view page];
  p->stopRefresh();
  bool ok = p->phrases.capture(t, first, last);
  p->reloadModel();
  return ok;
}
- (BOOL)placeAtTrack:(std::size_t)t row:(std::size_t)r merge:(BOOL)merge {
  auto *p = [(S3GTrackerAuthoringPageHost *)self.view page];
  p->stopRefresh();
  bool ok = p->phrases.place(t, r, merge);
  p->invalid();
  return ok;
}
@end
@implementation S3GTrackerPortableAssembleController
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:
                        (s3g::tracker::app::WorkspaceCallbacks *)callbacks {
  self = [super initWithNibName:nil bundle:nil];
  if (self)
    self.view = [[S3GTrackerAuthoringPageHost alloc]
        initWithState:state
            callbacks:callbacks
                 kind:AuthoringPage::Assemble];
  return self;
}
- (void)reloadModel {
  [(S3GTrackerAuthoringPageHost *)self.view reloadModel];
}
- (void)refreshPlaybackDisplay {
  [(S3GTrackerAuthoringPageHost *)self.view refreshPlaybackDisplay];
}
@end
@implementation S3GTrackerPortableReshapeController {
  S3GTrackerAuthoringPageHost *_pageHost;
}
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:
                        (s3g::tracker::app::WorkspaceCallbacks *)callbacks {
  NSWindow *window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 1080, 780)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  self = [super initWithWindow:window];
  if (self) {
    window.title = @"s3g Tracker — Pattern Reshape";
    window.releasedWhenClosed = NO;
    window.minSize = NSMakeSize(480, 360);
    _pageHost = [[S3GTrackerAuthoringPageHost alloc]
        initWithState:state
            callbacks:callbacks
                 kind:AuthoringPage::Reshape];
    window.contentView = _pageHost;
  }
  return self;
}
- (NSView *)pageView {
  return _pageHost;
}
- (void)reloadModel {
  [_pageHost reloadModel];
}
- (void)refreshPlaybackDisplay {
  [_pageHost refreshPlaybackDisplay];
}
- (void)clearPreview {
  if (_pageHost.page)
    _pageHost.page->reshape.clearPreview();
}
@end
