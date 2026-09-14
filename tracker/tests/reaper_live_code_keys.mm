// Manual integration test extension: build into an ISOLATED REAPER resource
// directory. Never load this in the user's regular REAPER instance.
// It posts CGEvents to its own process so REAPER's accelerator pre-translation
// runs before Cocoa/VSTGUI. Direct keyDown/performKeyEquivalent and NSApplication
// postEvent tests alone do not reproduce the real host's shortcut routing.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import <ApplicationServices/ApplicationServices.h>
#include <unistd.h>
#include "s3g/tracker/editor_reference.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>
#ifndef S3G_KEY_TEST_RESOURCE
#error Define S3G_KEY_TEST_RESOURCE to an isolated /private/tmp resource directory
#endif
@interface NSView (TrackerTestPages)
- (void)showPage:(NSInteger)index;
- (void)toggleDetachPage:(NSInteger)index;
@end
namespace {
struct Host {
    int version;
    void* window;
    int (*reg)(const char*, void*);
    void* (*get)(const char*);
};
Host api;
template <typename T> T fn(const char* name) { return reinterpret_cast<T>(api.get(name)); }
FILE* logFile;
NSView* root;
id workspace;
void* track;
int fx = -1, failures = 0;
std::size_t step = 0;
double next = 0;
NSTimer* testTimer;
std::vector<std::function<void()>> steps;
void log(const char* message) { std::fprintf(logFile, "%s\n", message); std::fflush(logFile); }
void check(bool ok, const char* message)
{
    std::fprintf(logFile, "%s %s\n", ok ? "PASS" : "FAIL", message);
    std::fflush(logFile);
    if (!ok) ++failures;
}
NSView* find(NSView* view, NSString* label)
{
    if ([view.accessibilityLabel isEqualToString:label]) return view;
    for (NSView* child in view.subviews)
        if (auto* found = find(child, label)) return found;
    return nil;
}
std::string draft()
{
    Ivar ivar = class_getInstanceVariable([workspace class], "_portableConsoleModel");
    if (!ivar) return "<missing portable model>";
    auto* model = reinterpret_cast<std::shared_ptr<s3g::tracker::editor::ConsoleModel>*>(
        static_cast<char*>((__bridge void*)workspace) + ivar_getOffset(ivar));
    return *model ? (*model)->draft : "<null model>";
}
void key(unsigned short code, NSEventModifierFlags flags = 0)
{
    for (bool down : { true, false }) {
        auto event = CGEventCreateKeyboardEvent(nullptr, code, down);
        CGEventSetFlags(event, static_cast<CGEventFlags>(flags));
        CGEventPostToPid(getpid(), event);
        CFRelease(event);
    }
}
void click(NSView* view, NSPoint point)
{
    [view.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    auto location = [view convertPoint:point toView:nil];
    auto* target = [view.window.contentView hitTest:
        [view convertPoint:point toView:view.window.contentView]];
    [view.window makeFirstResponder:target];
    for (auto type : { NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp }) {
        auto* e = [NSEvent mouseEventWithType:type location:location modifierFlags:0
            timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:view.window.windowNumber
            context:nil eventNumber:0 clickCount:1 pressure:1];
        if (type == NSEventTypeLeftMouseDown) [target mouseDown:e];
        else [target mouseUp:e];
    }
}
int playState() { return fn<int (*)()>("GetPlayState")(); }
void stop() { fn<void (*)()>("OnStopButton")(); }
void testField(NSString* label, NSPoint point, const char* name)
{
    steps.push_back([=] { log(name); click(find(root, label), point); });
    steps.push_back([] { key(0, NSEventModifierFlagCommand); });
    steps.push_back([] { key(11); });
    steps.push_back([] { key(49); });
    steps.push_back([] {
        check(draft() == "b ", "Space inserted once in Live Code");
        check(playState() == 0, "Space did not start transport");
        std::fprintf(logFile, "draft=[%s] play=%d responder=%s\n", draft().c_str(),
            playState(), NSStringFromClass(NSApp.keyWindow.firstResponder.class).UTF8String);
        stop();
    });
    steps.push_back([] { key(49, NSEventModifierFlagShift); });
    steps.push_back([] {
        check(draft() == "b  ", "Shift Space inserted once");
        check(playState() == 0, "Shift Space did not start transport");
        stop();
    });
    steps.push_back([=] { click(find(root, label), NSMakePoint(700, 25)); });
}
void timer()
{
    double now = NSProcessInfo.processInfo.systemUptime;
    if (now < next) return;
    next = now + .25;
    if (step < steps.size()) {
        std::fprintf(logFile, "step %zu\n", step); std::fflush(logFile);
        @try {
            steps[step++]();
            log("step returned");
        } @catch (NSException* exception) {
            check(false, exception.description.UTF8String);
            step = steps.size();
        }
        return;
    }
    [testTimer invalidate]; testTimer = nil;
    std::fprintf(logFile, "DONE failures=%d\n", failures);
    std::fflush(logFile);
}
}
extern "C" __attribute__((visibility("default"))) int ReaperPluginEntry(void*, Host* host)
{
    if (!host) {
        [testTimer invalidate]; testTimer = nil;
        if (logFile) std::fclose(logFile); logFile = nullptr; return 0;
    }
    api = *host;
    auto resource = fn<const char* (*)()>("GetResourcePath")();
    if (std::strcmp(resource, S3G_KEY_TEST_RESOURCE) != 0) return 0;
    logFile = std::fopen(S3G_KEY_TEST_RESOURCE "/keys.log", "w");
    if (!logFile) return 0;
    log("Isolated REAPER process-targeted CGEvent integration test");
    if (std::getenv("S3G_KEY_TEST_GLOBAL")) {
        struct Binding { unsigned char flags; unsigned short key, command; const char* name; };
        static Binding binding { 1, 32, 40044, "Transport: Play/stop" };
        check(api.reg("gaccel_global", &binding) != 0, "register global Space fixture");
    }
    steps.push_back([] {
        fn<void (*)(int, bool)>("InsertTrackAtIndex")(0, true);
        track = fn<void* (*)(void*, int)>("GetTrack")(nullptr, 0);
        fn<bool (*)(void*, const char*, double)>("SetMediaTrackInfo_Value")(track, "B_MUTE", 1);
        fx = fn<int (*)(void*, const char*, bool, int)>("TrackFX_AddByName")(
            track, "CLAP: s3g Tracker (s3g)", false, -1);
        check(fx >= 0, "instantiate Tracker");
        if (fx < 0) { step = steps.size(); return; }
        fn<void (*)(void*, int, int)>("TrackFX_Show")(track, fx,
            std::getenv("S3G_KEY_TEST_FLOATING") ? 3 : 1);
    });
    steps.push_back([] {
        for (NSWindow* w in NSApp.windows)
            if ((root = find(w.contentView, @"s3g Tracker REAPER page workspace"))) break;
        check(root != nil, "find Tracker workspace");
        if (!root) { step = steps.size(); return; }
        NSArray* pages = [root valueForKey:@"pageViews"];
        workspace = [pages[2] valueForKey:@"owner"];
        log([NSString stringWithFormat:@"Loaded %@", [NSBundle bundleForClass:root.class].bundlePath].UTF8String);
        stop();
    });
    testField(@"Tracker portable main page", NSMakePoint(250, 91), "Main Live Code");
    steps.push_back([] { [root showPage:8]; });
    testField(@"Tracker portable Console page", NSMakePoint(200, 49), "Console Live Code");
    steps.push_back([] { [root showPage:8]; [root toggleDetachPage:8]; });
    // Detached page no longer belongs to root, so find it in the page array.
    steps.push_back([] {
        NSView* console = [root valueForKey:@"pageViews"][8];
        click(console, NSMakePoint(200, 49));
    });
    steps.push_back([] { key(0, NSEventModifierFlagCommand); });
    steps.push_back([] { key(2); });
    steps.push_back([] { key(49); });
    steps.push_back([] {
        check(draft() == "d ", "detached Console Space inserted once");
        check(playState() == 0, "detached Console did not start transport");
        std::fprintf(logFile, "detached draft=[%s] key=%s responder=%s\n", draft().c_str(),
            NSApp.keyWindow.description.UTF8String,
            NSStringFromClass(NSApp.keyWindow.firstResponder.class).UTF8String);
        stop();
    });
    steps.push_back([] {
        NSView* main = (__bridge NSView*)api.window;
        [main.window makeKeyAndOrderFront:nil];
        [main.window makeFirstResponder:main];
        [NSApp activateIgnoringOtherApps:YES];
    });
    steps.push_back([] {
        std::fprintf(logFile, "outside key=%s responder=%s\n", NSApp.keyWindow.description.UTF8String,
            NSStringFromClass(NSApp.keyWindow.firstResponder.class).UTF8String);
        key(49);
    });
    steps.push_back([] {
        check(playState() != 0, "control: Space reaches REAPER transport outside Tracker text");
        stop();
    });
    next = NSProcessInfo.processInfo.systemUptime + 4;
    testTimer = [NSTimer timerWithTimeInterval:.05 repeats:YES block:^(NSTimer*) { timer(); }];
    [[NSRunLoop mainRunLoop] addTimer:testTimer forMode:NSRunLoopCommonModes];
    return 1;
}
