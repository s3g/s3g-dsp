#include "s3g_tracker_authoring_page_host.h"
#include "s3g_tracker_controls.h"
#include "s3g_tracker_reshape_window.h"
#include "vstgui/lib/events.h"
#import <CoreText/CoreText.h>
#include <iostream>
using namespace s3g::tracker;
using namespace s3g::tracker::editor;
@interface NSObject (AuthoringParity)
- (void)layoutPhraseInterface;
- (void)layoutInterface;
- (void)layoutReshapeInterface;
- (void)beginEditing;
- (void)commitEditor:(id)sender;
- (void)appendPressed:(id)sender;
- (void)duplicatePressed:(id)sender;
- (void)saveAsPhrasePressed:(id)sender;
- (void)savePressed:(id)sender;
- (void)previewPressed:(id)sender;
- (void)stopPhrasePreview;
- (void)stopPreview;
@end
static NSBitmapImageRep *bitmap(NSView *view) {
  NSData *pdf = [view dataWithPDFInsideRect:view.bounds];
  auto provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)pdf);
  auto document = CGPDFDocumentCreateWithProvider(provider);
  auto *image = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:NSInteger(NSWidth(view.bounds))
                    pixelsHigh:NSInteger(NSHeight(view.bounds))
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
  CGPDFDocumentRelease(document);
  CGDataProviderRelease(provider);
  return image;
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
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const std::string &s) {
      ++checks;
      if (!ok) {
        ++failures;
        std::cerr << s << '\n';
      }
    };
    auto window = [](NSView *v) {
      auto *w =
          [[NSWindow alloc] initWithContentRect:NSMakeRect(30, 30, 1320, 820)
                                      styleMask:NSWindowStyleMaskTitled |
                                                NSWindowStyleMaskResizable
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
      w.releasedWhenClosed = NO;
      w.contentView = v;
      v.hidden = NO;
      w.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
      [w makeKeyAndOrderFront:nil];
      return w;
    };
    auto draw = [](NSView *v) {
      [v layoutSubtreeIfNeeded];
      [v setNeedsDisplay:YES];
      [[NSRunLoop currentRunLoop]
          runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
      [v display];
      for (NSView *child in v.subviews)
        [child displayIfNeeded];
    };
    app::TrackerViewState base;
    base.session.pattern.tracks.resize(3);
    base.session.pattern.visibleRows = 32;
    for (std::size_t i = 0; i < 3; ++i) {
      auto &t = base.session.pattern.tracks[i];
      t.notes.assign(32, NoteCell::rest());
      t.noteColumn.length = 32;
      t.velocities.assign(32, ValueCell::defaultValue());
      for (std::size_t r = i; r < 32; r += 4) {
        t.notes[r] = NoteCell::withNote(uint8_t(48 + i * 5 + r % 12));
        t.velocities[r] = ValueCell::withValue(float(r + 40) / 127);
      }
    }
    base.phraseLibrary.phrases[0] = makeBlankPhrase(32);
    capturePhrase(base.session.pattern, 0, 0, 31,
                  base.phraseLibrary.phrases[0]);
    base.phraseLibrary.phrases[0].name = "Native reference";
    base.phraseLibrary.phrases[0].recommendedBpm = 123.5;
    base.assembly.blocks = {{kProjectAssetBankId, 0, 1},
                            {kProjectAssetBankId, 0, 2}};
    app::WorkspaceCallbacks nc, pc;
    std::vector<PitchPreviewEvent> nevents, pevents;
    int previews = 0, clears = 0, publications = 0;
    nc.previewPitchSequence = [&](const auto &e, uint8_t, double, uint32_t) {
      nevents = e;
    };
    pc.startAuthoringPreview = [&](const auto &e, uint8_t, double, uint32_t,
                                  uint32_t, bool) -> uint32_t {
      pevents = e;
      return 1;
    };
    pc.stopAuthoringPreview = [](uint32_t) {};
    pc.authoringPreviewPosition = [](uint32_t) -> int64_t { return 0; };
    pc.previewPattern = [&](const s3g::tracker::Pattern &) { ++previews; };
    pc.clearPatternPreview = [&] { ++clears; };
    pc.patternChanged = [&] { ++publications; };
    pc.createPatternVariant = [](const s3g::tracker::Pattern &) {};
    for (int k = 0; k < 3; ++k) {
      auto original = base, state = base;
      auto kind = AuthoringPage(k);
      id nativeController;
      NSView *native;
      if (k == 0) {
        nativeController = [[S3GTrackerPhraseView alloc] initWithState:&original
                                                             callbacks:&nc];
        native = [nativeController view];
      } else if (k == 1) {
        nativeController =
            [[S3GTrackerAssembleView alloc] initWithState:&original
                                                callbacks:&nc];
        native = [nativeController view];
      } else {
        nativeController =
            [[S3GTrackerReshapeWindowController alloc] initWithState:&original
                                                           callbacks:&nc];
        native = [[nativeController window] contentView];
      }
      auto *nw = window(native);
      auto *host = [[S3GTrackerAuthoringPageHost alloc] initWithState:&state
                                                            callbacks:&pc
                                                                 kind:kind];
      auto *pw = window(host);
      auto *p = host.page;
      [NSApp activateIgnoringOtherApps:YES];
      [native setFrameSize:NSMakeSize(1320, 820)];
      [host setFrameSize:NSMakeSize(1320, 820)];
      if (k == 0)
        [nativeController layoutPhraseInterface];
      else if (k == 1)
        [nativeController layoutInterface];
      else
        [nativeController layoutReshapeInterface];
      draw(native);
      draw(host);
      auto click = [&](VSTGUI::CRect r, bool twice = false) {
        check(!r.isEmpty(), "nonempty portable hit rectangle");
        VSTGUI::MouseDownEvent d;
        d.mousePosition = {r.getCenter().x, r.getCenter().y};
        d.buttonState = VSTGUI::MouseButton::Left;
        d.clickCount = twice ? 2 : 1;
        static_cast<VSTGUI::IPlatformFrameCallback *>(p->getFrame())
            ->platformOnEvent(d);
        VSTGUI::MouseUpEvent u;
        u.mousePosition = d.mousePosition;
        u.buttonState = VSTGUI::MouseButton::Left;
        static_cast<VSTGUI::IPlatformFrameCallback *>(p->getFrame())
            ->platformOnEvent(u);
        draw(host);
      };
      auto key = [&](VSTGUI::VirtualKey v, uint32_t ch = 0,
                     VSTGUI::Modifiers mods = {}) {
        VSTGUI::KeyboardEvent e;
        e.type = VSTGUI::EventType::KeyDown;
        e.virt = v;
        e.character = ch;
        e.modifiers = mods;
        static_cast<VSTGUI::IPlatformFrameCallback *>(p->getFrame())
            ->platformOnEvent(e);
        p->refreshPlaybackDisplay();
        draw(host);
      };
      auto compareRect = [&](NSString *key, const std::string &id) {
        NSView *control = [nativeController valueForKey:key];
        NSRect r = [control convertRect:control.bounds toView:native];
        auto q = p->controlBounds(id);
        bool ok = std::abs(r.origin.x - q.left) < .01 &&
                  std::abs(r.origin.y - q.top) < .01 &&
                  std::abs(r.size.width - q.getWidth()) < .01 &&
                  std::abs(r.size.height - q.getHeight()) < .01;
        if (!ok)
          std::cerr << "native " << NSStringFromRect(r).UTF8String
                    << " portable " << q.left << "," << q.top << ","
                    << q.getWidth() << "," << q.getHeight() << '\n';
        check(ok, "native control geometry " + id);
      };
      if (k == 0) {
        for (auto mapping : {std::pair{"bankPopup", "bank"},
                             {"libraryPopup", "phrase"},
                             {"nameField", "name"},
                             {"recommendedBpmField", "bpm"},
                             {"lengthPopup", "length"},
                             {"previewChannelPopup", "channel"},
                             {"modePopup", "mode"},
                             {"placeButton", "place"}})
          compareRect(@(mapping.first), mapping.second);
      } else if (k == 1) {
        for (auto mapping : {std::pair{"bankPopup", "bank"},
                             {"phrasePopup", "phrase"},
                             {"repeatPopup", "repeat"},
                             {"scopePopup", "scope"},
                             {"channelPopup", "channel"},
                             {"rowPopup", "row"},
                             {"fitPopup", "fit"},
                             {"placeButton", "place"}})
          compareRect(@(mapping.first), mapping.second);
      } else {
        for (auto mapping : {std::pair{"cyclePopup", "cycle"},
                             {"pocketField", "pocket"},
                             {"depthField", "depth"},
                             {"mutationAmountField", "amount"},
                             {"burstChanceField", "burst_chance"},
                             {"previewButton", "preview"},
                             {"createVariantButton", "variant"}})
          compareRect(@(mapping.first), mapping.second);
      }
      // Compare the actual independent Cocoa raster, not a restatement of its
      // intended colors. Exclude native scroller gutters and unused document
      // background; compare the authored grid/block/profile region itself.
      auto *nb = bitmap(native);
      auto *pb = bitmap(host);
      auto region = p->canvasBounds();
      region.left += k == 0 ? 52 : 64;
      region.right -= 20;
      region.top += 28;
      region.bottom =
          std::min(region.bottom - 22., k == 0 ? 757. : k == 1 ? 640. : 630.);
      std::size_t compared = 0, different = 0, chromaUnion = 0, chromaDiff = 0;
      for (int y = int(region.top); y < int(region.bottom); ++y)
        for (int x = int(region.left); x < int(region.right); ++x) {
          auto *a = nb.bitmapData + y * nb.bytesPerRow + x * 4;
          auto *b = pb.bitmapData + y * pb.bytesPerRow + x * 4;
          int diff =
              std::max({std::abs(int(a[0]) - b[0]), std::abs(int(a[1]) - b[1]),
                        std::abs(int(a[2]) - b[2])});
          ++compared;
          if (diff > 24)
            ++different;
          auto colored = [](const unsigned char *c) {
            return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}) >
                   22;
          };
          bool ca = colored(a), cb = colored(b);
          if (ca || cb)
            ++chromaUnion;
          if (ca != cb)
            ++chromaDiff;
        }
      double mismatch = double(different) / std::max<std::size_t>(1, compared);
      std::cout << "page " << k << " raster mismatch " << mismatch << " chroma "
                << chromaDiff << "/" << chromaUnion << '\n';
      check(mismatch < .035, "native authored-region raster parity");
      if (k == 2)
        check(chromaUnion > 100 && double(chromaDiff) / chromaUnion < .12,
              "native profile colored curve parity");
      if (const char *dir = std::getenv("S3G_TRACKER_AUTHORING_CAPTURE_DIR")) {
        NSString *folder = @(dir);
        [[NSFileManager defaultManager] createDirectoryAtPath:folder
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        for (NSView *v in @[ native, host ]) {
          auto *name = [NSString
              stringWithFormat:@"%@-%@",
                               k == 0 ? @"phrases"
                                      : k == 1 ? @"assemble" : @"reshape",
                               v == native ? @"cocoa" : @"vstgui"];
          [[v dataWithPDFInsideRect:v.bounds]
              writeToFile:[folder stringByAppendingPathComponent:
                                      [name stringByAppendingString:@".pdf"]]
               atomically:YES];
          [[bitmap(v) representationUsingType:NSBitmapImageFileTypePNG
                                   properties:@{}]
              writeToFile:[folder stringByAppendingPathComponent:
                                      [name stringByAppendingString:@".png"]]
               atomically:YES];
        }
      }
      if (k == 0) {
        id grid = [nativeController valueForKey:@"grid"];
        for (auto spec : {std::pair{0, "60+64+67"},
                          {1, "0.123456+0.8"},
                          {2, "MT"},
                          {3, "0.25"},
                          {4, "CC74"},
                          {5, "127"},
                          {6, "TIE+0.5"}}) {
          [grid setValue:@(spec.first) forKey:@"selectedField"];
          [grid setValue:@0 forKey:@"selectedRow"];
          [grid beginEditing];
          NSTextField *field = [grid valueForKey:@"editor"];
          field.stringValue = @(spec.second);
          [grid commitEditor:nil];
          p->phrases.field = std::size_t(spec.first);
          p->phrases.row = 0;
          check(p->phrases.edit(spec.second),
                "portable parser accepts native token");
          check(phraseGridCellText(original.phraseLibrary.phrases[0],
                                   spec.first, 0) ==
                    phraseGridCellText(state.phraseLibrary.phrases[0],
                                       spec.first, 0),
                "native parser cell equality");
        }
        draw(host);
        click(p->cellBounds(0, 1));
        key(VSTGUI::VirtualKey::None, '6');
        key(VSTGUI::VirtualKey::None, '1');
        key(VSTGUI::VirtualKey::Return);
        check(state.phraseLibrary.phrases[0].notes[1].note == 61 &&
                  !p->textActive(),
              "real GenericTextEdit commit");
        click(p->cellBounds(0, 1));
        key(VSTGUI::VirtualKey::None, '7');
        key(VSTGUI::VirtualKey::Escape);
        check(state.phraseLibrary.phrases[0].notes[1].note == 61,
              "Escape cancels entry");
        draw(host);
        click(p->controlBounds("name"));
        key(VSTGUI::VirtualKey::Space, ' ');
        key(VSTGUI::VirtualKey::Escape);
        check([host conformsToProtocol:@protocol(S3GTrackerTextInputOwner)],
              "REAPER text-owner protocol");
        auto savedName = state.phraseLibrary.phrases[0].name;
        auto savedBpm = state.phraseLibrary.phrases[0].recommendedBpm;
        click(p->controlBounds("name"));
        const std::string newName = "Phrase with spaces";
        for (auto c : newName)
          key(c == ' ' ? VSTGUI::VirtualKey::Space : VSTGUI::VirtualKey::None,
              c);
        key(VSTGUI::VirtualKey::Return);
        click(p->controlBounds("bpm"));
        for (auto c : std::string("145.5"))
          key(VSTGUI::VirtualKey::None, c);
        key(VSTGUI::VirtualKey::Return);
        check(state.phraseLibrary.phrases[0].name == savedName &&
                  state.phraseLibrary.phrases[0].recommendedBpm == savedBpm,
              "metadata remains a draft before SAVE");
        NSTextField *nativeName = [nativeController valueForKey:@"nameField"];
        NSTextField *nativeBpm =
            [nativeController valueForKey:@"recommendedBpmField"];
        nativeName.stringValue = @(newName.c_str());
        nativeBpm.stringValue = @"145.5";
        [nativeController savePressed:nil];
        click(p->controlBounds("SAVE"));
        check(state.phraseLibrary.phrases[0].name ==
                      original.phraseLibrary.phrases[0].name &&
                  state.phraseLibrary.phrases[0].recommendedBpm ==
                      original.phraseLibrary.phrases[0].recommendedBpm,
              "explicit SAVE matches native metadata commit");
        click(p->controlBounds("bpm"));
        key(VSTGUI::VirtualKey::None, '1');
        key(VSTGUI::VirtualKey::Return);
        click(p->controlBounds("SAVE"));
        check(state.phraseLibrary.phrases[0].recommendedBpm == 145.5 &&
                  p->phrases.status == "BPM MUST BE 20–400 OR BLANK",
              "invalid BPM cannot overwrite saved metadata");
        p->reloadModel();
        draw(host);
        click(p->cellBounds(0, 2));
        key(VSTGUI::VirtualKey::None, '6');
        state.phraseLibrary.phrases[0].notes[2] = NoteCell::withNote(90);
        p->reloadModel();
        check(!p->textActive() &&
                  state.phraseLibrary.phrases[0].notes[2].note == 90,
              "recall rejects stale inline edit");
        [nativeController previewPressed:nil];
        p->phrases.phrase() = original.phraseLibrary.phrases[0];
        p->startAudition();
        check(nevents.size() == pevents.size(), "native phrase audition count");
        for (std::size_t i = 0; i < std::min(nevents.size(), pevents.size());
             ++i)
          check(nevents[i].row == pevents[i].row &&
                    nevents[i].note == pevents[i].note &&
                    nevents[i].velocity == pevents[i].velocity &&
                    nevents[i].gatePercent == pevents[i].gatePercent &&
                    nevents[i].position == pevents[i].position,
                "native phrase preview exact events");
        [nativeController stopPhrasePreview];
        p->stopAudition();
        p->phrases.length(64);
        draw(host);
        VSTGUI::MouseWheelEvent wheel;
        wheel.mousePosition = p->canvasBounds().getCenter();
        wheel.deltaY = -10;
        wheel.deltaX = -3;
        p->onMouseWheelEvent(wheel);
        check(p->scrollOffset().y > 0, "phrase grid scroll");
        auto beforeScroll = p->scrollOffset().y;
        auto canvas = p->canvasBounds();
        click({canvas.right - 7, canvas.bottom - 12, canvas.right - 1,
               canvas.bottom - 6});
        check(p->scrollOffset().y > beforeScroll,
              "click/drag scrollbar reaches final rows");
        // Read audio-owned progress at 30/60/120 FPS. Neither elapsed wall time
        // nor passing repeat boundaries can publish more MIDI from the GUI.
        for (int fps : {30, 60, 120}) {
          auto clockState = base;
          app::WorkspaceCallbacks clockCallbacks;
          int emitted = 0, stopped = 0;
          int64_t audioRow = 0;
          clockState.phraseLoopPreview = true;
          clockCallbacks.startAuthoringPreview =
              [&](const auto &, uint8_t, double, uint32_t, uint32_t rows,
                  bool loop) -> uint32_t {
                check(loop && rows == 32, "full-length loop submitted once");
                ++emitted;
                return 42;
              };
          clockCallbacks.stopAuthoringPreview = [&](uint32_t t) {
            check(t == 42, "cancel owns the correct audio plan");
            ++stopped;
          };
          clockCallbacks.authoringPreviewPosition =
              [&](uint32_t) { return audioRow; };
          double time = 0;
          AuthoringServices clockServices;
          clockServices.monotonicTime = [&] { return time; };
          auto clockPage = VSTGUI::owned(new AuthoringPageView(
              clockState, clockCallbacks, AuthoringPage::Phrases,
              std::move(clockServices)));
          clockPage->startAudition();
          auto seconds = phraseAudition(clockState, clockPage->phrases.phrase())
                             .rowSeconds();
          for (int tick = 0; tick < fps; ++tick) {
            time = double(tick) / fps;
            clockPage->refreshPlaybackDisplay();
          }
          check(emitted == 1 && clockPage->auditionRow() == 0,
                "refresh FPS cannot advance audition or emit MIDI");
          time = seconds * 3.2;
          audioRow = 3;
          clockPage->auditionTick();
          check(clockPage->auditionRow() == 3 && emitted == 1,
                "audio audition cursor independent of FPS");
          time = seconds * 10.2;
          audioRow = 10;
          clockPage->auditionTick();
          check(clockPage->auditionRow() == 10 && emitted == 1,
                "stalled UI cursor catches up without replaying MIDI");
          time = seconds * 3203.2;
          audioRow = 3;
          clockPage->auditionTick();
          check(clockPage->auditionRow() == 3 && emitted == 1,
                "100 missed GUI loop boundaries never republish MIDI");
          clockPage->stopRefresh();
          check(clockPage->auditionRow() < 0 && stopped == 1,
                "hiding page cancels audio plan and display timer");
        }
      } else if (k == 1) {
        [nativeController appendPressed:nil];
        p->assemble.append();
        check(original.assembly.blocks.size() == state.assembly.blocks.size(),
              "native append");
        [nativeController duplicatePressed:nil];
        p->assemble.duplicate();
        check(original.assembly.blocks.size() == state.assembly.blocks.size(),
              "native duplicate");
        [nativeController previewPressed:nil];
        p->startAudition();
        check(nevents.size() == pevents.size(),
              "native assembly audition count");
        for (std::size_t i = 0; i < std::min(nevents.size(), pevents.size());
             ++i)
          check(nevents[i].row == pevents[i].row &&
                    nevents[i].note == pevents[i].note &&
                    nevents[i].velocity == pevents[i].velocity &&
                    nevents[i].position == pevents[i].position,
                "native assembly preview exact events");
        [nativeController stopPreview];
        p->stopAudition();
        draw(host);
        auto canvas = p->canvasBounds();
        VSTGUI::MouseDownEvent down;
        down.mousePosition = {canvas.left + 100, canvas.top + 40};
        down.buttonState = VSTGUI::MouseButton::Left;
        p->onMouseDownEvent(down);
        VSTGUI::MouseMoveEvent move;
        move.mousePosition = {canvas.left + 100, canvas.top + 400};
        move.buttonState = VSTGUI::MouseButton::Left;
        p->onMouseMoveEvent(move);
        const auto count = state.assembly.blocks.size();
        VSTGUI::MouseUpEvent up;
        up.mousePosition = move.mousePosition;
        up.buttonState = VSTGUI::MouseButton::Left;
        up.modifiers.add(VSTGUI::ModifierKey::Alt);
        p->onMouseUpEvent(up);
        check(state.assembly.blocks.size() == count + 1,
              "Option drag copies selected block");
        p->onMouseDownEvent(down);
        p->onMouseMoveEvent(move);
        state.assembly.blocks.clear();
        p->reloadModel();
        p->onMouseUpEvent(up);
        check(state.assembly.blocks.empty(), "recall cancels stale block drag");
      } else {
        NSTextField *text = [nativeController valueForKey:@"analysisLabel"];
        check([text.stringValue
                  isEqualToString:@(p->reshape.analysisText().c_str())],
              "native analysis text");
        NSTextField *result = [nativeController valueForKey:@"resultLabel"];
        check([result.stringValue
                  isEqualToString:@(p->reshape.resultText().c_str())],
              "native reshape result text");
        click(p->controlBounds("preview"));
        check(p->reshape.preview && previews > 0, "preview enabled");
        click(p->controlBounds("original"));
        check(!p->reshape.showingReshaped && clears > 0, "A/B original");
        auto slider = p->controlBounds("depth");
        VSTGUI::MouseDownEvent down;
        down.mousePosition = {slider.left + 40, slider.top + 12};
        down.buttonState = VSTGUI::MouseButton::Left;
        auto old = p->reshape.settings.timingDepth;
        p->onMouseDownEvent(down);
        check(p->reshape.settings.timingDepth != old, "smooth slider gesture");
        VSTGUI::MouseCancelEvent cancel;
        p->onMouseCancelEvent(cancel);
        check(p->reshape.settings.timingDepth == old,
              "cancel slider restores own settings");
      }
      // Real reparenting retains the same portable page and proportionally
      // fits the complete control area in small detached windows.
      NSView *placeholder =
          [[NSView alloc] initWithFrame:pw.contentView.bounds];
      pw.contentView = placeholder;
      auto *detached = window(host);
      [detached setContentSize:NSMakeSize(480, 360)];
      draw(host);
      check(host.page == p && p->getViewSize().getHeight() >= 780,
            "detached small-window authoring fit");
      host.hidden = YES;
      check(p->auditionRow() < 0 && !p->textActive() && !p->reshape.preview,
            "hide releases audition/text/preview");
      host.hidden = NO;
      pw.contentView = host;
      [pw setContentSize:NSMakeSize(1320, 820)];
      draw(host);
      check(host.page == p && p->drawCount() > 0, "reattachment keeps page");
      [host suspendEditing];
      [detached orderOut:nil];
      [pw orderOut:nil];
      [nw orderOut:nil];
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
  }
}
