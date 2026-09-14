#include "s3g_cocoa_gui.h"
#include "s3g_tracker_controls.h"
#include "s3g_tracker_shell_host.h"
#include "vstgui/lib/events.h"
#import <CoreText/CoreText.h>
#include <iostream>
using namespace s3g::tracker::editor;
@interface S3GTrackerNativeShellReference : NSView
@end
@implementation S3GTrackerNativeShellReference
- (BOOL)isFlipped {
  return YES;
}
- (void)drawRect:(NSRect)r {
  [S3GTrackerThemeColor(S3GTrackerThemeRole::Canvas) setFill];
  NSRectFill(r);
}
@end
static NSBitmapImageRep *bitmap(NSView *view) {
  NSData *pdf = [view dataWithPDFInsideRect:NSMakeRect(0, 0, 1320, 40)];
  auto provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)pdf);
  auto document = CGPDFDocumentCreateWithProvider(provider);
  auto *image =
      [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                              pixelsWide:1320
                                              pixelsHigh:40
                                           bitsPerSample:8
                                         samplesPerPixel:4
                                                hasAlpha:YES
                                                isPlanar:NO
                                          colorSpaceName:NSDeviceRGBColorSpace
                                             bytesPerRow:0
                                            bitsPerPixel:0];
  auto context =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:image].CGContext;
  CGContextDrawPDFPage(context, CGPDFDocumentGetPage(document, 1));
  CGContextFlush(context);
  CGPDFDocumentRelease(document);
  CGDataProviderRelease(provider);
  // Force Quartz's deferred bitmap drawing to finish before inspecting bytes.
  return [NSBitmapImageRep
      imageRepWithData:[image representationUsingType:NSBitmapImageFileTypePNG
                                           properties:@{}]];
}
int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    for (NSString *face in @[ @"Regular", @"Medium", @"SemiBold" ]) {
      auto *path = [@(S3G_TRACKER_TEST_FONT_DIR)
          stringByAppendingPathComponent:
              [NSString stringWithFormat:@"IBMPlexMono-%@.ttf", face]];
      CTFontManagerRegisterFontsForURL(
          (__bridge CFURLRef)[NSURL fileURLWithPath:path],
          kCTFontManagerScopeProcess, nullptr);
    }
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *what) {
      ++checks;
      if (!ok) {
        ++failures;
        std::cerr << what << '\n';
      }
    };
    auto window = [](NSView *v) {
      auto *w =
          [[NSWindow alloc] initWithContentRect:NSMakeRect(20, 20, 1320, 860)
                                      styleMask:NSWindowStyleMaskTitled
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
      w.releasedWhenClosed = NO;
      w.contentView = v;
      [w makeKeyAndOrderFront:nil];
      return w;
    };
    ShellController model;
    model.select(ShellPage::Warps);
    model.setHostBpm(123.45);
    int selected = -1;
    bool doubleClick = false;
    int detaches = 0;
    ShellServices services;
    services.selectPage = [&](ShellPage p, bool twice) {
      selected = int(p);
      doubleClick = twice;
      model.select(p);
    };
    services.toggleDetach = [&](ShellPage p) {
      ++detaches;
      model.setDetached(p, !model.detached(p));
    };
    auto *host =
        [[S3GTrackerShellHost alloc] initWithModel:&model
                                          services:std::move(services)];
    auto *pw = window(host);
    auto *reference = [[S3GTrackerNativeShellReference alloc]
        initWithFrame:NSMakeRect(0, 0, 1320, 860)];
    const char *titles[] = {"TRACKER", "SONG",     "GEOMETRY", "BURSTS",
                            "PHRASES", "ASSEMBLE", "RESHAPE",  "WARPS",
                            "CONSOLE", "HELP"};
    const double widths[] = {70, 50, 72, 60, 64, 72, 66, 54, 60, 46};
    double tabX = 12;
    for (int i = 0; i < 10; ++i) {
      auto *b = [[S3GTrackerActionButton alloc]
          initWithFrame:NSMakeRect(tabX, 6, widths[i], 28)];
      b.title = @(titles[i]);
      b.tag = i == 7 ? 1 : 0;
      b.state = i == 7 ? NSControlStateValueOn : NSControlStateValueOff;
      [reference addSubview:b];
      tabX += widths[i] + 6;
    }
    auto *detach = [[S3GTrackerActionButton alloc]
        initWithFrame:NSMakeRect(1272, 6, 36, 28)];
    detach.s3gUsesSuiteStyle = YES;
    detach.title = @"↗";
    [reference addSubview:detach];
    auto *bpm = [NSTextField labelWithString:@"HOST BPM  123.45"];
    bpm.frame = NSMakeRect(1120, 10, 138, 20);
    bpm.font = s3g::clap_gui::uiFont(10);
    bpm.textColor = s3g::clap_gui::color(0x929292);
    bpm.alignment = NSTextAlignmentRight;
    bpm.lineBreakMode = NSLineBreakByClipping;
    [reference addSubview:bpm];
    auto *events = [NSTextField labelWithString:@(model.eventText().c_str())];
    events.frame = NSMakeRect(694, 10, 416, 20);
    events.font = s3g::clap_gui::uiFont(8.5);
    events.textColor = S3GTrackerThemeColor(S3GTrackerThemeRole::TextMuted);
    events.alignment = NSTextAlignmentRight;
    events.lineBreakMode = NSLineBreakByTruncatingHead;
    [reference addSubview:events];
    auto *nw = window(reference);
    [[NSRunLoop currentRunLoop]
        runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
    [reference display];
    [host display];
    for (NSView *v in host.subviews)
      [v displayIfNeeded];
    auto *a = bitmap(reference);
    auto *b = bitmap(host);
    // Check tab typography/colors independently from the suite-style detach
    // glyph (whose cap-centered alignment is an intentional shared rule).
    for (auto region :
         {NSMakeRect(0, 0, 690, 40), NSMakeRect(694, 0, 570, 40)}) {
      std::size_t mismatch = 0, total = 0, visible = 0;
      for (int y = 0; y < 40; ++y)
        for (int x = int(NSMinX(region)); x < int(NSMaxX(region)); ++x) {
          auto *p = [[a colorAtX:x y:y]
              colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
          auto *q = [[b colorAtX:x y:y]
              colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
          if (std::max({p.redComponent, p.greenComponent, p.blueComponent}) >
              .2)
            ++visible;
          if (std::max({std::abs(p.redComponent - q.redComponent),
                        std::abs(p.greenComponent - q.greenComponent),
                        std::abs(p.blueComponent - q.blueComponent)}) >
              24. / 255.)
            ++mismatch;
          ++total;
        }
      double ratio = double(mismatch) / total;
      std::cout << "shell raster mismatch " << ratio << '\n';
      check(visible > 20, "reference raster is not blank");
      check(ratio < .025, "native header raster parity");
    }
    if (const char *dir = std::getenv("S3G_TRACKER_SHELL_CAPTURE_DIR")) {
      NSString *folder = @(dir);
      [[NSFileManager defaultManager] createDirectoryAtPath:folder
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:nil];
      for (NSString *name in @[ @"shell-cocoa", @"shell-vstgui" ]) {
        auto *image = [name hasSuffix:@"cocoa"] ? a : b;
        [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
            writeToFile:[folder stringByAppendingPathComponent:
                                    [name stringByAppendingString:@".png"]]
             atomically:YES];
      }
    }
    auto *view = host.shellView;
    NSArray *accessible = host.accessibilityChildren;
    check(accessible.count == 13,
          "portable shell exposes tabs/detach/status accessibility");
    check([[accessible[1] accessibilityLabel] isEqualToString:@"SONG page"] &&
              [accessible[1] accessibilityPerformPress] && selected == 1,
          "accessible tab press uses portable action");
    check([accessible[7] accessibilityPerformPress] && selected == 7,
          "accessible tab return");
    auto click = [&](NSRect r, int count) {
      VSTGUI::MouseDownEvent d;
      d.mousePosition = {NSMidX(r), NSMidY(r)};
      d.buttonState = VSTGUI::MouseButton::Left;
      d.clickCount = count;
      view->onMouseDownEvent(d);
      VSTGUI::MouseUpEvent u;
      u.mousePosition = d.mousePosition;
      u.buttonState = VSTGUI::MouseButton::Left;
      view->onMouseUpEvent(u);
    };
    click([host shellControlRect:@"PHRASES page"], 1);
    check(selected == 4 && !doubleClick, "tab pointer action");
    click([host shellControlRect:@"WARPS page"], 2);
    check(selected == 7 && doubleClick, "double-click detach intent");
    [host refresh];
    [host display];
    for (NSView *v in host.subviews)
      [v displayIfNeeded];
    click([host shellControlRect:@"Detach selected tool page"], 1);
    check(detaches == 1 && model.detached(ShellPage::Warps),
          "detach pointer action");
    VSTGUI::MouseDownEvent d;
    d.mousePosition = {30, 20};
    d.buttonState = VSTGUI::MouseButton::Left;
    view->onMouseDownEvent(d);
    VSTGUI::MouseCancelEvent c;
    view->onMouseCancelEvent(c);
    VSTGUI::MouseUpEvent u;
    u.mousePosition = d.mousePosition;
    u.buttonState = VSTGUI::MouseButton::Left;
    view->onMouseUpEvent(u);
    check(selected == 7, "canceled tab press never switches page");
    [host suspend];
    [pw orderOut:nil];
    [nw orderOut:nil];
    // Host must release its CFrame before stack-owned model/callbacks expire.
    pw.contentView = [[NSView alloc] init];
    host = nil;
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
  }
}
