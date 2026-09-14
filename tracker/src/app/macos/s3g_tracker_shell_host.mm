#include "s3g_tracker_shell_host.h"
#include "s3g_cocoa_gui.h"
#include "s3g_tracker_controls.h"
#include "s3g_tracker_vstgui_pilot.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platform_macos.h"
using namespace s3g::tracker::editor;
namespace f = s3g::portable_gui::foundation;
@interface S3GTrackerShellHost (Accessibility)
- (BOOL)pressShellControl:(NSString *)identifier;
@end
@interface S3GTrackerShellAccessibility : NSAccessibilityElement
@property(nonatomic, weak) S3GTrackerShellHost *owner;
@property(nonatomic, copy) NSString *controlId;
@end
@implementation S3GTrackerShellAccessibility
- (id)accessibilityParent {
  return self.owner;
}
- (NSString *)accessibilityLabel {
  return self.controlId;
}
- (NSString *)accessibilityRole {
  return [self.controlId hasSuffix:@" page"] ? NSAccessibilityButtonRole
                                             : NSAccessibilityStaticTextRole;
}
- (id)accessibilityValue {
  return [self.owner shellStatusText:self.controlId];
}
- (BOOL)isAccessibilityEnabled {
  return self.owner.window != nil;
}
- (NSRect)accessibilityFrame {
  auto *owner = self.owner;
  return [owner.window
      convertRectToScreen:[owner
                              convertRect:[owner
                                              shellControlRect:self.controlId]
                                   toView:nil]];
}
- (BOOL)accessibilityPerformPress {
  return [self.owner pressShellControl:self.controlId];
}
@end
@implementation S3GTrackerShellHost {
  ShellController *_model;
  ShellView *_view;
  VSTGUI::CFrame *_frame;
  BOOL _runtime, _opened;
  NSMutableDictionary<NSString *, S3GTrackerShellAccessibility *>
      *_accessibleControls;
}
- (instancetype)initWithModel:(ShellController *)model
                     services:(ShellServices)services {
  self = [super initWithFrame:NSMakeRect(0, 0, 1320, 860)];
  if (!self)
    return nil;
  _runtime = f::acquireRuntime();
  if (!_runtime)
    return nil;
  _model = model;
  _accessibleControls = [[NSMutableDictionary alloc] init];
  auto grid = macGridPaintServices();
  services.tools.font = macTrackerSuiteFont;
  services.tools.color = grid.color;
  services.tools.fontFactory = macTrackerFontFactory();
  auto font = [](NSFont *native) {
    NSLayoutManager *layout = [[NSLayoutManager alloc] init];
    return GridFont {
      native.fontName.UTF8String, native.pointSize,
          [layout defaultBaselineOffsetForFont:native],
          [@"H" sizeWithAttributes:@{NSFontAttributeName : native}].height
    };
  };
  services.tabFont = font(S3GTrackerFont(9.5, NSFontWeightMedium));
  services.placeholderFont = font(S3GTrackerFont(11, NSFontWeightMedium));
  services.statusFont = font(s3g::clap_gui::uiFont(8.5));
  services.bpmFont = font(s3g::clap_gui::uiFont(10));
  _frame = new VSTGUI::CFrame({0, 0, 1320, 860}, nullptr);
  _view = new ShellView(*model, std::move(services));
  _view->resize(1320, 860);
  _frame->addView(_view);
  self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  self.accessibilityLabel = @"Tracker portable workspace shell";
  self.accessibilityRole = NSAccessibilityGroupRole;
  return self;
}
- (BOOL)isFlipped {
  return YES;
}
- (ShellView *)shellView {
  return _view;
}
- (void)setFrameSize:(NSSize)size {
  [super setFrameSize:size];
  if (_frame && size.width > 0 && size.height > 0) {
    _frame->setSize(size.width, size.height);
    _view->resize(size.width, size.height);
  }
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
  _frame->setVisible(self.window && !self.hidden);
  if (!self.window)
    [self suspend];
}
- (void)refresh {
  if (_view)
    _view->invalid();
}
- (void)suspend {
  if (_view)
    _view->stopRefresh();
}
- (NSArray *)accessibilityChildren {
  NSMutableArray *children = [[NSMutableArray alloc] init];
  NSMutableArray<NSString *> *names = [[NSMutableArray alloc] init];
  for (std::size_t i = 0; i < kShellPageCount; ++i)
    [names addObject:[@(ShellController::title(ShellPage(i)))
                         stringByAppendingString:@" page"]];
  [names addObjectsFromArray:@[
    @"Detach selected tool page", @"MIDI event statistics",
    @"Host tempo in beats per minute"
  ]];
  for (NSString *name in names) {
    if (NSIsEmptyRect([self shellControlRect:name]))
      continue;
    auto *item = _accessibleControls[name];
    if (!item) {
      item = [[S3GTrackerShellAccessibility alloc] init];
      item.owner = self;
      item.controlId = name;
      _accessibleControls[name] = item;
    }
    [children addObject:item];
  }
  return children;
}
- (BOOL)pressShellControl:(NSString *)identifier {
  if (!_view || !self.window || self.hiddenOrHasHiddenAncestor ||
      ![identifier hasSuffix:@" page"])
    return NO;
  NSRect r = [self shellControlRect:identifier];
  if (NSIsEmptyRect(r))
    return NO;
  // Use the same portable hit/action path as pointer input, not hidden
  // native NSButtons or a second Cocoa implementation of navigation.
  VSTGUI::MouseDownEvent down;
  down.mousePosition = {NSMidX(r), NSMidY(r)};
  down.buttonState = VSTGUI::MouseButton::Left;
  down.clickCount = 1;
  _view->onMouseDownEvent(down);
  VSTGUI::MouseUpEvent up;
  up.mousePosition = down.mousePosition;
  up.buttonState = VSTGUI::MouseButton::Left;
  _view->onMouseUpEvent(up);
  return true;
}
- (NSRect)shellControlRect:(NSString *)identifier {
  if (!_view || !_model)
    return NSZeroRect;
  auto l = _model->layout(NSWidth(self.bounds), NSHeight(self.bounds));
  auto rect = [](s3g::tracker::editor::Rect r) {
    return NSMakeRect(r.x, r.y, r.width, r.height);
  };
  if ([identifier isEqualToString:@"Detach selected tool page"])
    return ShellController::canDetach(_model->selected()) ? rect(l.detach)
                                                          : NSZeroRect;
  if ([identifier isEqualToString:@"Host tempo in beats per minute"])
    return rect(l.bpm);
  if ([identifier isEqualToString:@"MIDI event statistics"])
    return l.eventsVisible ? rect(l.events) : NSZeroRect;
  for (std::size_t i = 0; i < kShellPageCount; ++i) {
    NSString *label = [@(ShellController::title(ShellPage(i)))
        stringByAppendingString:@" page"];
    if ([identifier isEqualToString:label])
      return rect(l.tabs[i]);
  }
  return NSZeroRect;
}
- (NSString *)shellStatusText:(NSString *)identifier {
  if (!_model)
    return @"";
  if ([identifier isEqualToString:@"Host tempo in beats per minute"])
    return @(_model->bpmText().c_str());
  if ([identifier isEqualToString:@"MIDI event statistics"])
    return @(_model->eventText().c_str());
  if ([identifier isEqualToString:@"Detach selected tool page"])
    return _model->detached(_model->selected()) ? @"Return to plugin window"
                                                : @"Open detached window";
  NSString *selected = [@(ShellController::title(_model->selected()))
      stringByAppendingString:@" page"];
  if ([identifier hasSuffix:@" page"])
    return [identifier isEqualToString:selected] ? @"Selected" : @"";
  return @"";
}
- (void)dealloc {
  [self suspend];
  if (_frame) {
    if (_opened)
      _frame->close();
    else
      _frame->forget();
  }
  _frame = nullptr;
  _view = nullptr;
  if (_runtime)
    f::releaseRuntime();
}
@end
