#include "s3g_tracker_geometry_page_host.h"
#include "s3g_tracker_controls.h"
#include "s3g_tracker_workspace.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/platform/platform_macos.h"
#import <CoreText/CoreText.h>
@interface S3GTrackerWorkspaceController (GeometryHostCallbacks)
- (void)modulePatternChanged;
- (void)moduleSelectionChanged;
@end
using namespace s3g::tracker::editor;
namespace foundation = s3g::portable_gui::foundation;
@implementation S3GTrackerGeometryPageHost {
  VSTGUI::CFrame *_frame;
  GeometryPageView *_page;
  s3g::tracker::app::WorkspaceCallbacks _callbacks;
  BOOL _runtime, _opened;
}
- (instancetype)initWithState:(s3g::tracker::app::TrackerViewState *)state
                    callbacks:(s3g::tracker::app::WorkspaceCallbacks *)callbacks
                        owner:(S3GTrackerWorkspaceController *)owner
                       bursts:(BOOL)bursts {
  self = [super initWithFrame:NSMakeRect(0, 0, 1320, 820)];
  if (!self)
    return nil;
  self.owner = owner;
  _runtime = foundation::acquireRuntime();
  if (!_runtime)
    return nil;
  _callbacks = *callbacks;
  __weak S3GTrackerGeometryPageHost *weakSelf = self;
  if (owner) {
    _callbacks.patternChanged = [weakSelf] {
      [weakSelf.owner modulePatternChanged];
    };
    _callbacks.selectionChanged = [weakSelf] {
      [weakSelf.owner moduleSelectionChanged];
    };
  }
  GeometryServices services;
  services.paint = macGridPaintServices();
  services.suiteFont = [](double size) {
    auto info = macTrackerSuiteFont(size);
    NSFont *font = [NSFont fontWithName:@(info.name.c_str()) size:size];
    if (font)
      info.lineHeight =
          [@"H" sizeWithAttributes:@{NSFontAttributeName : font}].height;
    return info;
  };
  services.measure = [](std::string_view text, const GridFont &font) {
    NSString *string = [[NSString alloc] initWithBytes:text.data()
                                                length:text.size()
                                              encoding:NSUTF8StringEncoding];
    NSFont *face = [NSFont fontWithName:@(font.name.c_str()) size:font.size];
    if (!face)
      face = [NSFont monospacedSystemFontOfSize:font.size
                                         weight:NSFontWeightRegular];
    return static_cast<double>(
        [string sizeWithAttributes:@{NSFontAttributeName : face}].width);
  };
  services.capHeight = [](const GridFont &font) {
    NSFont *face = [NSFont fontWithName:@(font.name.c_str()) size:font.size];
    return face ? static_cast<double>(face.capHeight) : font.size * .72;
  };
  services.focus = [weakSelf] {
    auto *host = weakSelf;
    if (!host || !host.window || host.hiddenOrHasHiddenAncestor ||
        !host.subviews.count)
      return;
    NSView *native = host.subviews.firstObject;
    [host.window makeFirstResponder:native];
    if (host.window.firstResponder == native && host->_frame)
      static_cast<VSTGUI::IPlatformFrameCallback *>(host->_frame)
          ->platformOnActivate(true);
  };
  if (owner)
    services.revealTracker = [weakSelf] {
      [weakSelf.owner showTrackerPage:nil];
      [weakSelf.owner focusTracker];
    };
  services.error = [] { NSBeep(); };
  _frame = new VSTGUI::CFrame({0, 0, 1320, 820}, nullptr);
  _page = new GeometryPageView(*state, _callbacks, bursts, std::move(services),
                               macTrackerFontFactory());
  _frame->addView(_page);
  self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  self.accessibilityLabel = bursts ? @"Burst editor" : @"Rhythm geometry";
  self.accessibilityRole = NSAccessibilityGroupRole;
  return self;
}
- (BOOL)isFlipped {
  return YES;
}
- (NSString *)accessibilityValue {
  if (!_page)
    return @"";
  auto &editor = _page->editor();
  return @(editor.viewModePopup.titleOfSelectedItem().c_str());
}
- (GeometryPageView *)page {
  return _page;
}
- (NSView *)playbackOverlay {
  return self;
}
- (void)setNeedsDisplay:(BOOL)needed {
  [super setNeedsDisplay:needed];
  if (needed && _frame)
    _frame->invalid();
}
- (BOOL)s3gTrackerHasFocusedTextInput {
  return macTrackerHasFocusedTextInput(self, _frame);
}
- (BOOL)performKeyEquivalent:(NSEvent *)event {
  return macTrackerTextKeyEquivalent(self, _frame, event) ||
         [super performKeyEquivalent:event];
}
- (void)resizeFrame {
  if (!_frame || !_page || NSWidth(self.bounds) <= 0 ||
      NSHeight(self.bounds) <= 0)
    return;
  const double width = NSWidth(self.bounds), height = NSHeight(self.bounds);
  // Embedded magnification is owned by the CLAP scaled viewport. Detached
  // pages retain the Cocoa responsive layout, with a small-window fit only.
  // Below this authoring size the original Burst matrix/radial pair overlaps
  // and the placement rows clip. Fit the unchanged layout proportionally.
  const double scale = std::min({1., width / 960., height / 720.});
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
  if (self.hiddenOrHasHiddenAncestor || !self.window || !self.window.visible ||
      self.window.miniaturized)
    _page->stopRefresh();
  else
    _page->refreshPlaybackDisplay();
}
- (void)selectBurstSlot:(std::size_t)slot {
  _page->editor().selectBurstSlot(slot);
}
- (void)openPitchMapFirstRow:(std::size_t)first lastRow:(std::size_t)last {
  _page->editor().openPitchMapFirstRow(first, last);
}
- (void)applyPitchMapContour:(s3g::tracker::PitchContour)contour
                    firstRow:(std::size_t)first
                     lastRow:(std::size_t)last {
  _page->editor().applyPitchMapContour(contour, first, last);
}
- (NSString *)displayedPatternId {
  return @(_page->editor().displayedPatternId().c_str());
}
- (NSUInteger)displayedLaneCount {
  return _page->editor().displayedLaneCount();
}
- (NSUInteger)displayedMutedLaneCount {
  return _page->editor().displayedMutedLaneCount();
}
- (CGFloat)ringRadiusForLane:(std::size_t)lane {
  return _page->editor().ringRadiusForLane(lane);
}
- (NSArray<NSString *> *)geometryMenuItems {
  NSMutableArray<NSString *> *items = [NSMutableArray array];
  for (const auto &item :
       _page->editor().itemsForGeometryMenu(GeometryMenuView))
    [items addObject:@(item.c_str())];
  return items;
}
- (void)selectGeometryMode:(NSInteger)mode {
  auto &editor = _page->editor();
  editor.viewModePopup.indexOfSelectedItem = static_cast<int>(mode);
  editor.viewModeChanged(editor.viewModePopup);
}
- (NSRect)burstPreviewChannelMenuBoxRect {
  const auto r = _page->editor().burstPreviewChannelMenuBoxRect();
  return NSMakeRect(r.x, r.y, r.width, r.height);
}
- (NSRect)burstPreviewHeaderButtonRect {
  const auto r = _page->editor().burstPreviewHeaderButtonRect();
  return NSMakeRect(r.x, r.y, r.width, r.height);
}
- (BOOL)handleToolboxClickAtPoint:(NSPoint)p {
  return _page->editor().handleToolboxClickAtPoint({p.x, p.y});
}
- (void)applyGeometryMenuSelection:(NSInteger)index {
  _page->editor().applyGeometryMenuSelection(static_cast<int>(index));
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
  _page = nullptr;
  _frame = nullptr;
  if (_runtime)
    foundation::releaseRuntime();
}
@end
