#include "s3g_tracker_controls.h"
#include "s3g_tracker_geometry_page_host.h"
#include "s3g_tracker_workspace.h"
#include "vstgui/lib/events.h"
#import <CoreText/CoreText.h>
#include <iostream>
using namespace s3g::tracker;
using namespace s3g::tracker::editor;
@interface NSView (GeometryParity)
- (NSRect)canvasRect;
- (NSRect)canvasPlotRect;
- (NSRect)inspectorRect;
- (NSRect)laneCyclePanelRect;
- (NSRect)editPanelRect;
- (NSRect)viewPanelRect;
- (NSRect)bridgePanelRect;
- (NSRect)lengthSliderTrack;
- (NSRect)defaultNoteSliderTrack;
- (NSRect)rotateSliderTrack;
- (NSRect)densitySliderTrack;
- (NSRect)laneMenuBoxRect;
- (NSRect)directionMenuBoxRect;
- (NSRect)viewMenuBoxRect;
- (NSRect)morphTargetMenuBoxRect;
- (NSRect)burstSlotMenuBoxRect;
- (NSRect)burstBankMenuBoxRect;
- (NSRect)burstEventMenuBoxRect;
- (NSRect)burstNameBoxRect;
- (NSRect)pitchScopeMenuBoxRect;
- (NSRect)pitchRootMenuBoxRect;
- (NSRect)pitchScaleMenuBoxRect;
- (NSRect)pitchMinimumSliderTrack;
- (NSRect)pitchMaximumSliderTrack;
- (NSRect)pitchContourMenuBoxRect;
- (NSRect)pitchLeapMenuBoxRect;
- (NSRect)pitchVariationSliderTrack;
- (NSRect)pitchTransposeSliderTrack;
- (NSRect)pitchGraphRect;
- (NSRect)pitchIntervalGraphRect;
- (NSRect)burstMatrixRect;
- (NSRect)burstOverviewRect;
- (NSRect)burstBreakpointRect;
- (NSRect)burstRadialPlotRect;
- (NSRect)burstPreviewChannelMenuBoxRect;
- (NSRect)burstPreviewHeaderButtonRect;
- (NSRect)burstLoopHeaderButtonRect;
- (NSRect)burstRenameHeaderButtonRect;
- (NSRect)zoomOutRect;
- (NSRect)zoomResetRect;
- (NSRect)zoomInRect;
- (NSArray<NSString *> *)itemsForGeometryMenu:(NSInteger)menu;
- (void)openGeometryMenu:(NSInteger)menu;
- (void)applyGeometryMenuSelection:(NSInteger)index;
- (BOOL)handleToolboxClickAtPoint:(NSPoint)point;
- (void)openPitchMapFirstRow:(std::size_t)first lastRow:(std::size_t)last;
- (void)refreshPitchMapPreview;
- (void)refreshPlaybackDisplay;
- (void)selectBurstSlot:(std::size_t)slot;
- (void)stopBurstPreview;
- (CGFloat)ringRadiusForLane:(std::size_t)lane;
- (NSPoint)geometryCenter;
@end
int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    for (NSString *face in @[ @"Regular", @"Medium", @"SemiBold" ]) {
      NSString *path = [@(S3G_TRACKER_TEST_FONT_DIR)
          stringByAppendingPathComponent:
              [NSString stringWithFormat:@"IBMPlexMono-%@.ttf", face]];
      CTFontManagerRegisterFontsForURL(
          (__bridge CFURLRef)[NSURL fileURLWithPath:path],
          kCTFontManagerScopeProcess, nullptr);
    }
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const std::string &message) {
      ++checks;
      if (!ok) {
        ++failures;
        std::cerr << message << '\n';
      }
    };
    auto window = [](NSView *view) {
      auto *w =
          [[NSWindow alloc] initWithContentRect:NSMakeRect(20, 20, 1320, 820)
                                      styleMask:NSWindowStyleMaskTitled |
                                                NSWindowStyleMaskResizable
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
      w.releasedWhenClosed = NO;
      w.contentView = view;
      view.hidden = NO;
      w.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
      [w makeKeyAndOrderFront:nil];
      return w;
    };
    app::TrackerViewState original;
    original.session.pattern = original.patternBank.entries.front().pattern;
    original.session.pattern.tracks.resize(4);
    for (std::size_t lane = 0; lane < original.session.pattern.tracks.size();
         ++lane) {
      auto &track = original.session.pattern.tracks[lane];
      track.noteColumn.length = 16;
      track.notes.assign(16, NoteCell::rest());
      for (std::size_t row = lane % 3; row < 16; row += 3)
        track.notes[row] =
            NoteCell::withNote(static_cast<uint8_t>(48 + lane + row));
    }
    auto state = original;
    app::WorkspaceCallbacks callbacks;
    auto *cocoa =
        [[S3GTrackerWorkspaceController alloc] initWithState:&original
                                                   callbacks:&callbacks];
    auto *native = [cocoa geometryPageView];
    auto *nativeBurst = [cocoa burstPageView];
    auto *nativeWindow = window(native);
    auto *nativeBurstWindow = window(nativeBurst);
    auto *host = [[S3GTrackerGeometryPageHost alloc] initWithState:&state
                                                         callbacks:&callbacks
                                                             owner:nil
                                                            bursts:NO];
    auto *burst = [[S3GTrackerGeometryPageHost alloc] initWithState:&state
                                                          callbacks:&callbacks
                                                              owner:nil
                                                             bursts:YES];
    auto *portableWindow = window(host);
    auto *portableBurstWindow = window(burst);
    [NSApp activateIgnoringOtherApps:YES];
    auto pump = [&] {
      for (NSView *v in @[ native, nativeBurst, host, burst ]) {
        [v setFrameSize:NSMakeSize(1320, 820)];
        [v layoutSubtreeIfNeeded];
        [v setNeedsDisplay:YES];
      }
      [[NSRunLoop currentRunLoop]
          runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
      for (NSView *v in @[ native, nativeBurst, host, burst ]) {
        v.hidden = NO;
        [v displayIfNeeded];
      }
    };
    auto capture = [&](NSString *name, NSView *p, NSView *n) {
      if (const char *directory =
              std::getenv("S3G_TRACKER_GEOMETRY_CAPTURE_DIR")) {
        NSString *dir = @(directory);
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        for (NSView *v in @[ p, n ]) {
          NSString *stem =
              [name stringByAppendingString:v == p ? @"-vstgui" : @"-cocoa"];
          [[v dataWithPDFInsideRect:v.bounds]
              writeToFile:[dir stringByAppendingPathComponent:
                                   [stem stringByAppendingString:@".pdf"]]
               atomically:YES];
          // The native layer-backed view returns a blank cache bitmap;
          // render its independent vector capture for an honest reference.
          NSData *pdf = [v dataWithPDFInsideRect:v.bounds];
          auto provider =
              CGDataProviderCreateWithCFData((__bridge CFDataRef)pdf);
          auto document = CGPDFDocumentCreateWithProvider(provider);
          auto page = CGPDFDocumentGetPage(document, 1);
          auto *bitmap = [[NSBitmapImageRep alloc]
              initWithBitmapDataPlanes:nullptr
                            pixelsWide:1320
                            pixelsHigh:820
                         bitsPerSample:8
                       samplesPerPixel:4
                              hasAlpha:YES
                              isPlanar:NO
                        colorSpaceName:NSDeviceRGBColorSpace
                           bytesPerRow:0
                          bitsPerPixel:0];
          auto context =
              [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap]
                  .CGContext;
          CGContextDrawPDFPage(context, page);
          CGPDFDocumentRelease(document);
          CGDataProviderRelease(provider);
          [[bitmap representationUsingType:NSBitmapImageFileTypePNG
                                properties:@{}]
              writeToFile:[dir stringByAppendingPathComponent:
                                   [stem stringByAppendingString:@".png"]]
               atomically:YES];
        }
      }
    };
    auto compareLayout = [&](NSView *n, GeometryEditor &e) {
      auto rect = [&](NSRect a, s3g::tracker::editor::Rect b,
                      const char *name) {
        check(std::abs(a.origin.x - b.x) < .001 &&
                  std::abs(a.origin.y - b.y) < .001 &&
                  std::abs(a.size.width - b.width) < .001 &&
                  std::abs(a.size.height - b.height) < .001,
              std::string("layout ") + name);
      };
      rect([n canvasRect], e.canvasRect(), "canvasRect");
      rect([n canvasPlotRect], e.canvasPlotRect(), "canvasPlotRect");
      rect([n inspectorRect], e.inspectorRect(), "inspectorRect");
      rect([n laneCyclePanelRect], e.laneCyclePanelRect(),
           "laneCyclePanelRect");
      rect([n editPanelRect], e.editPanelRect(), "editPanelRect");
      rect([n viewPanelRect], e.viewPanelRect(), "viewPanelRect");
      rect([n bridgePanelRect], e.bridgePanelRect(), "bridgePanelRect");
      rect([n lengthSliderTrack], e.lengthSliderTrack(), "lengthSliderTrack");
      rect([n defaultNoteSliderTrack], e.defaultNoteSliderTrack(),
           "defaultNoteSliderTrack");
      rect([n rotateSliderTrack], e.rotateSliderTrack(), "rotateSliderTrack");
      rect([n densitySliderTrack], e.densitySliderTrack(),
           "densitySliderTrack");
      rect([n laneMenuBoxRect], e.laneMenuBoxRect(), "laneMenuBoxRect");
      rect([n directionMenuBoxRect], e.directionMenuBoxRect(),
           "directionMenuBoxRect");
      rect([n viewMenuBoxRect], e.viewMenuBoxRect(), "viewMenuBoxRect");
      rect([n morphTargetMenuBoxRect], e.morphTargetMenuBoxRect(),
           "morphTargetMenuBoxRect");
      rect([n burstSlotMenuBoxRect], e.burstSlotMenuBoxRect(),
           "burstSlotMenuBoxRect");
      rect([n burstBankMenuBoxRect], e.burstBankMenuBoxRect(),
           "burstBankMenuBoxRect");
      rect([n burstEventMenuBoxRect], e.burstEventMenuBoxRect(),
           "burstEventMenuBoxRect");
      rect([n burstNameBoxRect], e.burstNameBoxRect(), "burstNameBoxRect");
      rect([n pitchScopeMenuBoxRect], e.pitchScopeMenuBoxRect(),
           "pitchScopeMenuBoxRect");
      rect([n pitchRootMenuBoxRect], e.pitchRootMenuBoxRect(),
           "pitchRootMenuBoxRect");
      rect([n pitchScaleMenuBoxRect], e.pitchScaleMenuBoxRect(),
           "pitchScaleMenuBoxRect");
      rect([n pitchMinimumSliderTrack], e.pitchMinimumSliderTrack(),
           "pitchMinimumSliderTrack");
      rect([n pitchMaximumSliderTrack], e.pitchMaximumSliderTrack(),
           "pitchMaximumSliderTrack");
      rect([n pitchContourMenuBoxRect], e.pitchContourMenuBoxRect(),
           "pitchContourMenuBoxRect");
      rect([n pitchLeapMenuBoxRect], e.pitchLeapMenuBoxRect(),
           "pitchLeapMenuBoxRect");
      rect([n pitchVariationSliderTrack], e.pitchVariationSliderTrack(),
           "pitchVariationSliderTrack");
      rect([n pitchTransposeSliderTrack], e.pitchTransposeSliderTrack(),
           "pitchTransposeSliderTrack");
      rect([n pitchGraphRect], e.pitchGraphRect(), "pitchGraphRect");
      rect([n pitchIntervalGraphRect], e.pitchIntervalGraphRect(),
           "pitchIntervalGraphRect");
      rect([n burstMatrixRect], e.burstMatrixRect(), "burstMatrixRect");
      rect([n burstOverviewRect], e.burstOverviewRect(), "burstOverviewRect");
      rect([n burstBreakpointRect], e.burstBreakpointRect(),
           "burstBreakpointRect");
      rect([n burstRadialPlotRect], e.burstRadialPlotRect(),
           "burstRadialPlotRect");
      rect([n burstPreviewChannelMenuBoxRect],
           e.burstPreviewChannelMenuBoxRect(),
           "burstPreviewChannelMenuBoxRect");
      rect([n burstPreviewHeaderButtonRect], e.burstPreviewHeaderButtonRect(),
           "burstPreviewHeaderButtonRect");
      rect([n burstLoopHeaderButtonRect], e.burstLoopHeaderButtonRect(),
           "burstLoopHeaderButtonRect");
      rect([n burstRenameHeaderButtonRect], e.burstRenameHeaderButtonRect(),
           "burstRenameHeaderButtonRect");
      rect([n zoomOutRect], e.zoomOutRect(), "zoomOutRect");
      rect([n zoomResetRect], e.zoomResetRect(), "zoomResetRect");
      rect([n zoomInRect], e.zoomInRect(), "zoomInRect");
    };
    for (int mode : {0, 1, 2, 3, 4, 5, 7}) {
      [native openGeometryMenu:3];
      [native applyGeometryMenuSelection:mode == 7 ? 6 : mode];
      [host selectGeometryMode:mode];
      pump();
      compareLayout(native, host.page->editor());
      auto center = [native geometryCenter];
      auto pc = host.page->editor().geometryCenter();
      check(std::hypot(center.x - pc.x, center.y - pc.y) < .001, "ring center");
      for (std::size_t lane = 0; lane < state.session.pattern.tracks.size();
           ++lane)
        check(std::abs([native ringRadiusForLane:lane] -
                       host.page->editor().ringRadiusForLane(lane)) < .001,
              "ring radius");
      capture([NSString stringWithFormat:@"geometry-%d", mode], host, native);
    }
    for (int menu = 1; menu <= GeometryMenuBurstPreviewChannel; ++menu) {
      auto items = host.page->editor().itemsForGeometryMenu(
          static_cast<GeometryMenu>(menu));
      NSArray<NSString *> *ns = [native itemsForGeometryMenu:menu];
      bool same = items.size() == ns.count;
      for (std::size_t i = 0; same && i < items.size(); ++i)
        same = items[i] == ns[i].UTF8String;
      check(same, "menu " + std::to_string(menu));
    }
    [native openPitchMapFirstRow:0 lastRow:15];
    [host openPitchMapFirstRow:0 lastRow:15];
    pump();
    capture(@"pitch-map", host, native);
    [native openGeometryMenu:9];
    host.page->editor().openGeometryMenu(GeometryMenuPitchScale);
    pump();
    capture(@"scale-menu", host, native);
    host.page->editor().suspend();
    [native openGeometryMenu:9];
    auto &be = burst.page->editor();
    const auto button = be.burstActionRectForRow(5, 0, 3);
    [nativeBurst
        handleToolboxClickAtPoint:NSMakePoint(button.x + button.width / 2,
                                              button.y + button.height / 2)];
    be.handleToolboxClickAtPoint(
        {button.x + button.width / 2, button.y + button.height / 2});
    pump();
    compareLayout(nativeBurst, be);
    check(state.session.burstLibrary.bursts[0].eventCount ==
              original.session.burstLibrary.bursts[0].eventCount,
          "create burst");
    capture(@"bursts", burst, nativeBurst);
    for (const auto [row, index, count] : {std::tuple{3u, 1u, 2u},
                                           {6u, 1u, 3u},
                                           {7u, 0u, 3u},
                                           {6u, 2u, 3u},
                                           {7u, 1u, 3u},
                                           {7u, 2u, 3u},
                                           {3u, 0u, 2u}}) {
      auto r = be.burstActionRectForRow(row, index, count);
      [nativeBurst handleToolboxClickAtPoint:NSMakePoint(r.x + r.width / 2,
                                                         r.y + r.height / 2)];
      be.handleToolboxClickAtPoint({r.x + r.width / 2, r.y + r.height / 2});
      auto &a = original.session.burstLibrary.bursts[0];
      auto &b = state.session.burstLibrary.bursts[0];
      bool same = a.eventCount == b.eventCount;
      for (std::size_t i = 0; same && i < a.eventCount; ++i) {
        auto x = a.events[i], y = b.events[i];
        same = x.note == y.note && x.position == y.position &&
               x.velocity == y.velocity && x.gatePercent == y.gatePercent;
      }
      check(same, "Burst action " + std::to_string(row) + "/" +
                      std::to_string(index));
    }
    for(NSSize size: {NSMakeSize(480,360),NSMakeSize(920,660)}) {
      [burst setFrameSize:size];
      const auto canvas=be.canvasRect(), radial=be.burstOverviewRect();
      const auto placement=be.burstPlacementPanelLayout();
      check(radial.x+radial.width<=canvas.x+canvas.width &&
          placement.frame.height>=s3g::gui_layout::toolboxHeightForRows(4),
          "small detached Burst page fits overview and every placement row");
    }
    pump();
    // Real VSTGUI text input (including spaces) and keyboard menu navigation.
    auto &page = *burst.page;
    auto nameRect = be.burstNameBoxRect();
    VSTGUI::MouseDownEvent down;
    down.mousePosition = {nameRect.x + 8, nameRect.y + 2};
    down.buttonState = VSTGUI::MouseButton::Left;
    page.onMouseDownEvent(down);
    check([burst s3gTrackerHasFocusedTextInput],
          "Burst name focus classification");
    VSTGUI::KeyboardEvent key;
    key.type = VSTGUI::EventType::KeyDown;
    key.character = ' ';
    auto *platform =
        static_cast<VSTGUI::IPlatformFrameCallback *>(page.getFrame());
    platform->platformOnEvent(key);
    check(key.consumed, "Burst name consumes Space");
    key = {};
    key.type = VSTGUI::EventType::KeyDown;
    key.virt = VSTGUI::VirtualKey::Escape;
    platform->platformOnEvent(key);
    page.refreshPlaybackDisplay();
    be.openGeometryMenu(GeometryMenuBurstPreviewChannel);
    key = {};
    key.type = VSTGUI::EventType::KeyDown;
    key.virt = VSTGUI::VirtualKey::Down;
    page.onKeyboardEvent(key);
    key.virt = VSTGUI::VirtualKey::Return;
    key.consumed = false;
    page.onKeyboardEvent(key);
    check(state.burstPreviewMidiChannel == 2 &&
              be.openMenu() == GeometryMenuNone,
          "VSTGUI menu keyboard selection");
    check(host.page->drawCount() && page.drawCount(),
          "both VSTGUI pages render");
    [host suspendEditing];
    [burst suspendEditing];
    [nativeBurst stopBurstPreview];
    for (NSWindow *w in @[
           nativeWindow, nativeBurstWindow, portableWindow, portableBurstWindow
         ])
      [w close];
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
  }
}
