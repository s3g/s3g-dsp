#include "s3g_tracker_controls.h"
#include "s3g_tracker_help_window.h"
#include "s3g_tracker_reference_page_host.h"
#include "s3g_tracker_reaper_keyboard.h"
#include "s3g_tracker_vstgui_pilot.h"
#include "s3g_tracker_workspace.h"
#include "vstgui/lib/cclipboard.h"
#include "vstgui/lib/events.h"
#import <CoreText/CoreText.h>
#include <cmath>
#include <iostream>

using namespace s3g::tracker;
using namespace s3g::tracker::editor;
using namespace VSTGUI;
namespace {
std::vector<ReaperKeyboardAccelerator*> registrations;
int (*textInfo)(void*, intptr_t) = nullptr;
int infoAdds = 0, infoRemoves = 0, priorityAdds = 0;
int registerKeyboard(const char* key, void* value)
{
    std::string name(key);
    if (name == "<accelerator") {
        registrations.push_back(static_cast<ReaperKeyboardAccelerator*>(value));
        ++priorityAdds;
    }
    else if (name == "-accelerator")
        registrations.erase(std::remove(registrations.begin(), registrations.end(), value), registrations.end());
    else if (name == "hwnd_info") { textInfo = reinterpret_cast<decltype(textInfo)>(value); ++infoAdds; }
    else if (name == "-hwnd_info") { textInfo = nullptr; ++infoRemoves; }
    else return 0;
    return 1;
}
}
int main()
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        for (NSString* face in @[ @"Regular", @"Medium", @"SemiBold" ]) {
            NSString* path = [@(S3G_TRACKER_TEST_FONT_DIR)
                stringByAppendingPathComponent:[NSString
                                                   stringWithFormat:@"IBMPlexMono-%@.ttf", face]];
            CTFontManagerRegisterFontsForURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],
                kCTFontManagerScopeProcess, nullptr);
        }
        int failures = 0, returns = 0, closes = 0;
        auto check = [&](bool ok, const std::string& message) {
            if (!ok) {
                ++failures;
                std::cerr << message << '\n';
            }
        };
        auto window = [](NSView* view) {
            auto* w = [[NSWindow alloc]
                initWithContentRect:view.bounds
                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                            backing:NSBackingStoreBuffered
                              defer:NO];
            w.releasedWhenClosed = NO;
            w.contentView = view;
            [w makeKeyAndOrderFront:nil];
            return w;
        };
        auto model = std::make_shared<ConsoleModel>();
        std::vector<std::string> executed;
        model->execute = [&](const std::string& s) { executed.push_back(s); };
        ReferencePageServices services;
        services.returnToTracker = [&] { ++returns; };
        services.closeHelp = [&] { ++closes; };
        auto* consoleHost =
            [[S3GTrackerReferencePageHost alloc] initWithFrame:NSMakeRect(0, 0, 1320, 820)
                                                       console:model
                                                      services:services];
        auto* helpHost =
            [[S3GTrackerReferencePageHost alloc] initWithFrame:NSMakeRect(0, 0, 1320, 820)
                                                       console:nullptr
                                                      services:services];
        auto* consoleWindow = window(consoleHost);
        auto* helpWindow = window(helpHost);
        auto consoleKeys = std::make_unique<MacReaperTextInput>(registerKeyboard, @[ consoleHost ]);
        auto helpKeys = std::make_unique<MacReaperTextInput>(registerKeyboard, @[ helpHost ]);
        check(registrations.size() == 2 && infoAdds == 1 && priorityAdds == 2,
            "multiple instances must share one text classification hook");
        consoleWindow.appearance = helpWindow.appearance =
            [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        auto* cocoaHelp = [[S3GTrackerConsoleHelpWindowController alloc] init];
        [cocoaHelp.window setContentSize:NSMakeSize(1320, 820)];
        app::TrackerViewState state;
        state.session.pattern = state.patternBank.entries.front().pattern;
        app::WorkspaceCallbacks callbacks;
        auto* cocoa = [[S3GTrackerWorkspaceController alloc] initWithState:&state
                                                                 callbacks:&callbacks];
        auto* nativeConsole = [cocoa consolePageView];
        [nativeConsole setFrameSize:NSMakeSize(1320, 820)];
        auto* nativeWindow = window(nativeConsole);
        nativeWindow.appearance = cocoaHelp.window.appearance =
            [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        NSTextView* log = [cocoa valueForKey:@"consoleOutput"];
        log.string = @"";
        NSTextView* help = [cocoaHelp valueForKey:@"helpTextView"];
        auto pump = [&] {
            consoleHost.page->refresh();
            helpHost.page->refresh();
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
            [nativeConsole layoutSubtreeIfNeeded];
            [cocoaHelp.window.contentView layoutSubtreeIfNeeded];
            [consoleHost displayIfNeeded];
            [helpHost displayIfNeeded];
        };
        [NSApp activateIgnoringOtherApps:YES];
        auto append = [&](const std::string& s, bool error = false) {
            model->append(s, error);
            [cocoa appendConsoleMessage:s error:error];
            pump();
        };
        append("Native console ready. Try kit superior compact, @k x---x---, or help.");
        append(": bpm 120");
        append("Tempo set to 120 BPM");
        append("Invalid command: not-a-command", true);
        append("UTF-8: café — 日本語 / 🥁\nSecond line");
        pump();
        check(consoleHost.page->drawCount() && helpHost.page->drawCount(),
            "both reference CFrames draw");
        check(model->output.plainText() == log.string.UTF8String, "native Console text equality");
        check(trackerHelpDocument().plainText() == help.string.UTF8String,
            "entire original Help document must match byte-for-byte, including workflow guides");
        auto capture = [&](NSString* name, NSView* portable, NSView* native) {
            if (const char* dir = std::getenv("S3G_TRACKER_REFERENCE_CAPTURE_DIR")) {
                NSString* path = @(dir);
                [[NSFileManager defaultManager] createDirectoryAtPath:path
                                          withIntermediateDirectories:YES
                                                           attributes:nil
                                                                error:nil];
                [[portable dataWithPDFInsideRect:portable.bounds]
                    writeToFile:[path stringByAppendingPathComponent:
                                          [name stringByAppendingString:@"-vstgui.pdf"]]
                     atomically:YES];
                [[native dataWithPDFInsideRect:native.bounds]
                    writeToFile:[path stringByAppendingPathComponent:
                                          [name stringByAppendingString:@"-cocoa.pdf"]]
                     atomically:YES];
                for (NSView* v in @[ portable, native ]) {
                    auto* bitmap = [v bitmapImageRepForCachingDisplayInRect:v.bounds];
                    [v cacheDisplayInRect:v.bounds toBitmapImageRep:bitmap];
                    [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@ {}]
                        writeToFile:[path
                                        stringByAppendingPathComponent:[name
                                                                           stringByAppendingString:v
                                                                                   == portable
                                                                               ? @"-vstgui.png"
                                                                               : @"-cocoa.png"]]
                         atomically:YES];
                }
            }
        };
        capture(@"console", consoleHost, nativeConsole);
        capture(@"help", helpHost, cocoaHelp.window.contentView);
        auto geometry = [&](ReferencePageView* page, NSTextView* text, NSView* root,
                            const char* name) {
            [text.layoutManager ensureLayoutForTextContainer:text.textContainer];
            NSRect viewport = [text.enclosingScrollView convertRect:text.enclosingScrollView.bounds
                                                             toView:root];
            if (!root.isFlipped)
                viewport.origin.y = NSHeight(root.bounds) - NSMaxY(viewport);
            auto vp = page->textViewport();
            std::cout << name << " viewport " << NSStringFromRect(viewport).UTF8String
                      << " portable " << vp.left << ',' << vp.top << ',' << vp.getWidth() << ','
                      << vp.getHeight() << " background "
                      << text.backgroundColor.description.UTF8String << '\n';
            check(std::abs(viewport.origin.x - vp.left) < .01
                    && std::abs(viewport.origin.y - vp.top) < .01
                    && std::abs(viewport.size.width - vp.getWidth()) < .01
                    && std::abs(viewport.size.height - vp.getHeight()) < .01,
                std::string(name) + " viewport parity");
            std::size_t scalar = 0;
            int mismatches = 0;
            int styleMismatches = 0;
            double largest = 0;
            for (NSUInteger pos = 0;
                 pos < text.string.length && scalar < page->textLayout().glyphs.size();) {
                NSUInteger glyph = [text.layoutManager glyphIndexForCharacterAtIndex:pos];
                NSRect line = [text.layoutManager lineFragmentRectForGlyphAtIndex:glyph
                                                                   effectiveRange:nullptr];
                NSPoint location = [text.layoutManager locationForGlyphAtIndex:glyph];
                const auto& g = page->textLayout().glyphs[scalar];
                auto attributes = [text.textStorage attributesAtIndex:pos effectiveRange:nullptr];
                NSFont* nativeFont = attributes[NSFontAttributeName];
                NSColor* nativeColor = attributes[NSForegroundColorAttributeName];
                auto pc = macGridPaintServices().color(page->textLayout().styles[g.style].rgb, 1);
                auto nc = resolvedColor(nativeColor);
                const auto& pf = page->textLayout().fonts[g.style];
                // TextKit materializes fallback faces into attributed runs;
                // VSTGUI/CoreText resolves the same face when painting the run.
                std::string paintedFace = pf.name;
                if (g.character > 127) {
                    NSFont* base = [NSFont fontWithName:@(pf.name.c_str()) size:pf.size];
                    auto scalarText = referenceUtf8(std::u32string_view(&g.character, 1));
                    NSString* string = @(scalarText.c_str());
                    CTFontRef fallback = CTFontCreateForString((__bridge CTFontRef)base,
                        (__bridge CFStringRef)string, CFRangeMake(0, string.length));
                    paintedFace = ((__bridge NSFont*)fallback).fontName.UTF8String;
                    CFRelease(fallback);
                }
                if (paintedFace != nativeFont.fontName.UTF8String
                    || std::abs(pf.size - nativeFont.pointSize) > .01 || pc.red != nc.red
                    || pc.green != nc.green || pc.blue != nc.blue) {
                    if (styleMismatches < 5)
                        std::cerr << "style " << scalar << " portable " << pf.name << ',' << pf.size
                                  << " native " << nativeFont.fontName.UTF8String << ','
                                  << nativeFont.pointSize << " colors " << int(pc.red) << ','
                                  << int(pc.green) << ',' << int(pc.blue) << " / " << int(nc.red)
                                  << ',' << int(nc.green) << ',' << int(nc.blue) << '\n';
                    ++styleMismatches;
                }
                double error = std::max(std::abs(line.origin.x + location.x - 5 - g.x),
                    std::abs(line.origin.y + location.y - g.baseline));
                if (error > .01) {
                    if (!mismatches)
                        std::cout << "wrap context ["
                                  << referenceUtf8(std::u32string_view(page->textLayout().text)
                                                       .substr(scalar > 30 ? scalar - 30 : 0, 100))
                                  << "]\n";
                    if (mismatches < 6)
                        std::cout << name << " offset " << scalar << " native "
                                  << line.origin.x + location.x - 5 << ","
                                  << line.origin.y + location.y << " portable " << g.x << ","
                                  << g.baseline << '\n';
                    ++mismatches;
                    largest = std::max(largest, error);
                }
                pos += page->textLayout().text[scalar] > 0xffff ? 2 : 1;
                ++scalar;
            }
            std::cout << name << " glyphs " << scalar << " mismatches " << mismatches << " max "
                      << largest << '\n';
            check(mismatches == 0, std::string(name) + " glyph positions differ from Cocoa");
            check(styleMismatches == 0,
                std::string(name) + " font/color attributes differ from Cocoa");
        };
        geometry(consoleHost.page, log, nativeConsole, "Console");
        geometry(helpHost.page, help, cocoaHelp.window.contentView, "Help");
        auto* active = consoleHost.page;
        auto key = [&](VirtualKey virt, char32_t character = 0, Modifiers mods = {}) {
            KeyboardEvent e(EventType::KeyDown);
            e.virt = virt;
            e.character = character;
            e.modifiers = mods;
            static_cast<IPlatformFrameCallback*>(active->getFrame())->platformOnEvent(e);
            pump();
        };
        auto chars = [&](const std::string& value) {
            for (char32_t c : referenceUnicode(value))
                key(VirtualKey::None, c);
        };
        auto click = [&](CPoint p, int count = 1, MouseButton button = MouseButton::Left) {
            MouseDownEvent d;
            d.mousePosition = p;
            d.buttonState = button;
            d.clickCount = count;
            auto* platform = static_cast<IPlatformFrameCallback*>(active->getFrame());
            platform->platformOnEvent(d);
            MouseUpEvent u;
            u.mousePosition = p;
            u.buttonState = button;
            platform->platformOnEvent(u);
            pump();
        };
        auto command = Modifiers { ModifierKey::Control };
        [consoleWindow makeKeyAndOrderFront:nil];
        consoleHost.page->focusInput();
        pump();
        auto* space = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
            modifierFlags:0 timestamp:0 windowNumber:consoleWindow.windowNumber context:nil
            characters:@" " charactersIgnoringModifiers:@" " isARepeat:NO keyCode:49];
        check([consoleHost performKeyEquivalent:space], "Console Space leaked to host shortcuts");
        pump();
        check(model->draft == " ", "Console key-equivalent Space must insert exactly once");
        check(!macTrackerTextKeyEquivalent(helpHost, helpHost.page->getFrame(), space),
            "inactive Help window must not claim Console keystrokes");
        ReaperKeyboardMessage keyMessage { (__bridge void*)consoleWindow.firstResponder };
        check(registrations[0]->translate(&keyMessage, registrations[0]) == -10
                && registrations[1]->translate(&keyMessage, registrations[1]) == 0,
            "only the focused instance may request raw REAPER input");
        check(textInfo((__bridge void*)consoleWindow.firstResponder, 0) == 1
                && textInfo((__bridge void*)consoleWindow.firstResponder, 1) == 1
                && textInfo((__bridge void*)helpWindow, 0) == 0
                && textInfo(reinterpret_cast<void*>(1), 0) == 0
                && textInfo((__bridge void*)consoleWindow.firstResponder, 99) == 0,
            "classification must identify focused text, not foreign windows or unknown queries");
        keyMessage.window = (__bridge void*)helpWindow;
        check(registrations[0]->translate(&keyMessage, registrations[0]) == 0
                && registrations[0]->translate(nullptr, registrations[0]) == 0,
            "an inactive retained text field must not claim a foreign or missing message target");
        key(VirtualKey::Back);
        chars("pan");
        key(VirtualKey::Tab);
        check(model->draft == "panic ", "unique Tab completion");
        key(VirtualKey::Return);
        check(executed == std::vector<std::string> { "panic" } && model->draft.empty(),
            "Return executes exactly once");
        chars("bpm 103");
        key(VirtualKey::Return);
        chars("unfinished");
        key(VirtualKey::Up);
        check(model->draft == "bpm 103", "Up history latest");
        key(VirtualKey::Up);
        check(model->draft == "panic", "Up history previous");
        key(VirtualKey::Down);
        key(VirtualKey::Down);
        check(model->draft == "unfinished", "Down restores draft");
        key(VirtualKey::None, 'a', command);
        chars("fx");
        key(VirtualKey::Tab);
        check(model->draft == "fx"
                && model->output.plainText().find("matches: fx, fxvalue") != std::string::npos,
            "ambiguous completion prints choices without replacing input");
        key(VirtualKey::Escape);
        check(
            returns == 1 && model->draft == "fx", "Escape preserves draft and returns to Tracker");
        consoleHost.page->focusInput();
        chars("value");
        check(model->draft == "fxvalue", "refocus places caret at end of shared draft");
        consoleHost.page->stopRefresh();
        model->draft = "shared main-page draft";
        consoleHost.page->refresh();
        consoleHost.page->focusInput();
        key(VirtualKey::Up);
        check(model->draft == "bpm 103", "shared model history");
        key(VirtualKey::Down);
        check(model->draft == "shared main-page draft", "shared draft restored");
        click({ 45, 70 });
        key(VirtualKey::None, 'a', command);
        key(VirtualKey::None, 'c', command);
        check(CClipboard::getString()
                && CClipboard::getString()->getString() == model->output.plainText(),
            "Console select-all/copy exact UTF-8");
        auto output = model->output.plainText();
        chars("must not edit output");
        check(output == model->output.plainText(), "Console output is read-only");
        active = helpHost.page;
        [helpWindow makeKeyAndOrderFront:nil];
        active->focusInput();
        pump();
        key(VirtualKey::None, 'a', command);
        key(VirtualKey::None, 'c', command);
        check(CClipboard::getString()
                && CClipboard::getString()->getString() == trackerHelpDocument().plainText(),
            "Help complete select-all/copy");
        click({ 55, 50 }, 2);
        check(active->selectedText() == "Lane", "Help double-click word selection");
        click({ 76, 50 }, 2);
        check(active->selectedText() == " ", "Help double-click whitespace selection");
        click({ 55, 50 }, 3);
        check(active->selectedText().find("session unchanged.\n") != std::string::npos,
            "Help triple-click paragraph selection");
        MouseDownEvent down;
        down.mousePosition = { 49, 50 };
        down.buttonState = MouseButton::Left;
        auto* platform = static_cast<IPlatformFrameCallback*>(active->getFrame());
        platform->platformOnEvent(down);
        MouseMoveEvent move;
        move.mousePosition = { 74, 50 };
        move.buttonState = MouseButton::Left;
        platform->platformOnEvent(move);
        MouseUpEvent up;
        up.mousePosition = move.mousePosition;
        up.buttonState = MouseButton::Left;
        platform->platformOnEvent(up);
        pump();
        check(active->selectedText() == "Lane",
            "Help mouse drag selection uses rendered glyph positions");
        click({ 70, 50 }, 1, MouseButton::Right);
        check(!active->popupItemBounds(0).isEmpty(),
            "right-click opens separated canvas menu immediately");
        key(VirtualKey::Escape);
        key(VirtualKey::None, 'f', command);
        chars("TRANSPORT + SONG");
        key(VirtualKey::Return);
        check(active->selectedText() == "TRANSPORT + SONG" && active->scrollY() > 0,
            "Help find selects and scrolls");
        key(VirtualKey::Escape);
        check(active->inputBounds().top == 27 && closes == 0, "find Escape does not close Help");
        key(VirtualKey::Home);
        check(active->scrollY() == 0, "Home scrolls top");
        key(VirtualKey::PageDown);
        check(active->scrollY() > 0, "PageDown scrolls");
        key(VirtualKey::End);
        auto end = active->scrollY();
        MouseWheelEvent wheel;
        wheel.mousePosition = { 200, 200 };
        wheel.deltaY = 3;
        platform->platformOnEvent(wheel);
        pump();
        check(active->scrollY() < end, "mouse wheel scrolls");
        key(VirtualKey::None, 'g', command);
        check(active->selectedText() == "TRANSPORT + SONG", "find-again wraps");
        key(VirtualKey::Escape);
        check(closes == 1, "Help Escape uses host close callback");
        for (NSValue* size in @[
                 [NSValue valueWithSize:NSMakeSize(760, 720)],
                 [NSValue valueWithSize:NSMakeSize(570, 430)]
             ]) {
            [helpWindow setContentSize:size.sizeValue];
            [cocoaHelp.window setContentSize:size.sizeValue];
            active->scrollTo(0);
            pump();
            capture([NSString stringWithFormat:@"help-%.0f", size.sizeValue.width], helpHost,
                cocoaHelp.window.contentView);
            geometry(active, help, cocoaHelp.window.contentView, "Help resized");
            check(active->getViewSize().getWidth() == size.sizeValue.width,
                "detached Help reflows to its window");
        }
        // Prefix trimming is compared against the actual native NSMutableAttributedString.
        for (int i = 0; i < 200; ++i) {
            std::string message = std::to_string(i) + std::string(170, 'x');
            model->append(message, i % 2);
            [cocoa appendConsoleMessage:message error:i % 2];
        }
        // Prior command-test entries were intentionally only in the portable model.
        ConsoleModel bounds;
        log.string = @"";
        for (int i = 0; i < 200; ++i) {
            std::string message = std::to_string(i) + std::string(170, 'x');
            bounds.append(message, i % 2);
            [cocoa appendConsoleMessage:message error:i % 2];
            check(bounds.output.plainText() == log.string.UTF8String,
                "native bounded Console log parity");
        }
        consoleHost.page->refresh();
        pump();
        check(consoleHost.page->scrollY() > 0, "Console follows newly appended output");
        auto* detached = window([[NSView alloc] initWithFrame:consoleHost.frame]);
        [consoleHost removeFromSuperview];
        detached.contentView = consoleHost;
        pump();
        active = consoleHost.page;
        [detached makeKeyAndOrderFront:nil];
        active->focusInput();
        key(VirtualKey::Return);
        check(executed.back() == "shared main-page draft",
            "reparented Console retains draft and command callback");
        active->stopRefresh();
        [consoleHost removeFromSuperview];
        consoleWindow.contentView = consoleHost;
        pump();
        check(consoleHost.page->getFrame() && consoleHost.page->drawCount(),
            "reattached Console keeps its CFrame");
        consoleKeys.reset();
        check(registrations.size() == 1 && textInfo && infoRemoves == 0,
            "closing one instance must preserve the other's host hook");
        helpKeys.reset();
        check(registrations.empty() && !textInfo && infoRemoves == 1,
            "last close must unregister every host callback");
        [consoleWindow close];
        [helpWindow close];
        [nativeWindow close];
        [detached close];
        [cocoaHelp.window close];
        if (!failures)
            std::cout << "Cocoa / portable Console + Help parity and interaction: ok\n";
        return failures ? 1 : 0;
    }
}
