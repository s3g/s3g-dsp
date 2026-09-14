#include <clap/clap.h>

#include <dlfcn.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct ReaperHostBridge {
    int callerVersion = 0;
    void* mainWindow = nullptr;
    int (*registerObject)(const char*, void*) = nullptr;
    void* (*getFunction)(const char*) = nullptr;
};

struct ReaperKeyboardAccelerator {
    int (*translate)(void*, ReaperKeyboardAccelerator*);
    bool isLocal;
    void* user;
};

struct HostContext {
    clap_host_t host {};
    uint32_t processRequests = 0u;
    uint32_t dirtyMarks = 0u;
    uint32_t playRequests = 0u;
    uint32_t pauseRequests = 0u;
    uint32_t stopRequests = 0u;
    int playState = 0;
    double masterTempo = 120.0;
    clap_host_state_t state {};
    ReaperHostBridge reaper {};
    std::vector<ReaperKeyboardAccelerator*> keyboardAccelerators;
    int (*hwndInfo)(void*, intptr_t) = nullptr;
};

HostContext* activeReaperHost = nullptr;

int reaperRegisterObject(const char* name, void* object)
{
    if (!activeReaperHost) return 0;
    auto& c = *activeReaperHost;
    if (std::strcmp(name, "<accelerator") == 0) {
        c.keyboardAccelerators.push_back(static_cast<ReaperKeyboardAccelerator*>(object));
        return 1;
    }
    if (std::strcmp(name, "-accelerator") == 0) {
        auto& list = c.keyboardAccelerators;
        list.erase(std::remove(list.begin(), list.end(), object), list.end());
        return 1;
    }
    if (std::strcmp(name, "hwnd_info") == 0) {
        c.hwndInfo = reinterpret_cast<decltype(c.hwndInfo)>(object); return 1;
    }
    if (std::strcmp(name, "-hwnd_info") == 0) {
        c.hwndInfo = nullptr; return 1;
    }
    return 0;
}

void reaperPlay()
{
    if (activeReaperHost) {
        ++activeReaperHost->playRequests;
        activeReaperHost->playState = 1;
    }
}

void reaperPause()
{
    if (activeReaperHost) {
        ++activeReaperHost->pauseRequests;
        activeReaperHost->playState = 2;
    }
}

void reaperStop()
{
    if (activeReaperHost) {
        ++activeReaperHost->stopRequests;
        activeReaperHost->playState = 0;
    }
}

int reaperGetPlayState()
{
    return activeReaperHost ? activeReaperHost->playState : 0;
}

double reaperMasterTempo()
{
    return activeReaperHost ? activeReaperHost->masterTempo : 120.0;
}

void* reaperGetFunction(const char* name)
{
    if (!name) return nullptr;
    if (std::strcmp(name, "OnPlayButton") == 0)
        return reinterpret_cast<void*>(&reaperPlay);
    if (std::strcmp(name, "OnPauseButton") == 0)
        return reinterpret_cast<void*>(&reaperPause);
    if (std::strcmp(name, "OnStopButton") == 0)
        return reinterpret_cast<void*>(&reaperStop);
    if (std::strcmp(name, "GetPlayState") == 0)
        return reinterpret_cast<void*>(&reaperGetPlayState);
    if (std::strcmp(name, "Master_GetTempo") == 0)
        return reinterpret_cast<void*>(&reaperMasterTempo);
    return nullptr;
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    auto* context = static_cast<HostContext*>(host->host_data);
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &context->state;
    if (std::strcmp(id, "cockos.reaper_extension") == 0)
        return &context->reaper;
    return nullptr;
}

void hostRequest(const clap_host_t* host)
{
    ++static_cast<HostContext*>(host->host_data)->processRequests;
}

void hostMarkDirty(const clap_host_t* host)
{
    ++static_cast<HostContext*>(host->host_data)->dirtyMarks;
}

struct OutputEvents {
    clap_output_events_t interface {};
    std::array<clap_event_midi_t, 128u> events {};
    uint32_t count = 0u;

    OutputEvents()
    {
        interface.ctx = this;
        interface.try_push = push;
    }

    static bool push(const clap_output_events_t* list,
        const clap_event_header_t* header)
    {
        auto* self = static_cast<OutputEvents*>(list->ctx);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_MIDI
            || header->size < sizeof(clap_event_midi_t)
            || self->count >= self->events.size()) return false;
        self->events[self->count++] = *reinterpret_cast<
            const clap_event_midi_t*>(header);
        return true;
    }
};

struct InputEvents {
    clap_input_events_t interface {};
    std::array<clap_event_midi_t, 8u> events {};
    uint32_t count = 0u;

    InputEvents()
    {
        interface.ctx = this;
        interface.size = size;
        interface.get = get;
    }

    void addMidi(uint32_t time, uint8_t status, uint8_t note,
        uint8_t velocity)
    {
        auto& event = events[count++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0] = status;
        event.data[1] = note;
        event.data[2] = velocity;
    }

    static uint32_t size(const clap_input_events_t* list)
    {
        return static_cast<InputEvents*>(list->ctx)->count;
    }

    static const clap_event_header_t* get(const clap_input_events_t* list,
        uint32_t index)
    {
        auto* self = static_cast<InputEvents*>(list->ctx);
        return index < self->count ? &self->events[index].header : nullptr;
    }
};

struct StateBuffer {
    clap_ostream_t output {};
    clap_istream_t input {};
    std::vector<uint8_t> bytes;
    size_t cursor = 0u;

    StateBuffer()
    {
        output.ctx = this;
        output.write = write;
        input.ctx = this;
        input.read = read;
    }

    static int64_t write(const clap_ostream_t* stream, const void* source,
        uint64_t count)
    {
        auto* self = static_cast<StateBuffer*>(stream->ctx);
        if (!source || count > 64u * 1024u * 1024u
            || self->bytes.size() > 64u * 1024u * 1024u - count)
            return -1;
        const auto* data = static_cast<const uint8_t*>(source);
        self->bytes.insert(self->bytes.end(), data, data + count);
        return static_cast<int64_t>(count);
    }

    static int64_t read(const clap_istream_t* stream, void* destination,
        uint64_t count)
    {
        auto* self = static_cast<StateBuffer*>(stream->ctx);
        const uint64_t available = self->bytes.size() - self->cursor;
        const uint64_t amount = std::min(count, available);
        if (amount == 0u) return 0;
        std::memcpy(destination, self->bytes.data() + self->cursor,
            static_cast<size_t>(amount));
        self->cursor += static_cast<size_t>(amount);
        return static_cast<int64_t>(amount);
    }
};

std::string resolveBinary(const char* input)
{
    std::string path = input ? input : "";
#if defined(__APPLE__)
    if (path.size() >= 5u && path.substr(path.size() - 5u) == ".clap") {
        path += "/Contents/MacOS/s3g_tracker";
    }
#endif
    return path;
}

bool expect(bool condition, const char* message)
{
    if (condition) return true;
    std::fprintf(stderr, "tracker CLAP: %s\n", message);
    return false;
}

} // namespace

#if defined(__APPLE__)

@interface S3GTrackerCaptureHostView : NSView
@end

@interface NSView (S3GTrackerBurstPreviewSmokeAccess)
- (NSArray<NSString*>*)geometryMenuItems;
- (void)selectGeometryMode:(NSInteger)mode;
- (NSRect)burstPreviewHeaderButtonRect;
- (NSRect)burstPreviewChannelMenuBoxRect;
- (void)applyGeometryMenuSelection:(NSInteger)index;
- (BOOL)handleToolboxClickAtPoint:(NSPoint)point;
- (NSRect)authoringControlRect:(NSString*)identifier;
- (NSRect)authoringPopupItemRect:(NSUInteger)index;
- (BOOL)s3gTrackerHasFocusedTextInput;
- (NSRect)shellControlRect:(NSString*)identifier;
- (NSString*)shellStatusText:(NSString*)identifier;
- (NSRect)warpControlRect:(NSString*)identifier;
@end

@implementation S3GTrackerCaptureHostView

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [[NSColor colorWithCalibratedWhite:0.024 alpha:1.0] setFill];
    NSRectFill(self.bounds);
}

@end

namespace {

NSView* findAccessibleView(NSView* root, NSString* accessibilityLabel);

NSButton* findButton(NSView* root, NSString* title,
    NSString* accessibilityLabel, NSString* identifier)
{
    if ([root isKindOfClass:NSButton.class]) {
        NSButton* button = static_cast<NSButton*>(root);
        if ((!title || [button.title isEqualToString:title])
            && (!accessibilityLabel
                || [button.accessibilityLabel
                    isEqualToString:accessibilityLabel])
            && (!identifier
                || [button.identifier isEqualToString:identifier]))
            return button;
    }
    for (NSView* child in root.subviews) {
        if (NSButton* result = findButton(
                child, title, accessibilityLabel, identifier))
            return result;
    }
    return nil;
}

bool clickButton(NSView* root, NSString* title,
    NSString* accessibilityLabel, NSString* identifier)
{
    NSButton* button = findButton(
        root, title, accessibilityLabel, identifier);
    if (!button) {
        NSView* shell = findAccessibleView(root, @"Tracker portable workspace shell");
        if (!shell || !accessibilityLabel
            || ![shell respondsToSelector:@selector(shellControlRect:)]) return false;
        [shell display];
        NSRect r = [shell shellControlRect:accessibilityLabel];
        if (NSIsEmptyRect(r)) return false;
        NSPoint point = NSMakePoint(NSMidX(r), NSMidY(r));
        NSWindow* window = shell.window;
        NSPoint location = [shell convertPoint:point toView:nil];
        NSView* target = [window.contentView hitTest:
            [shell convertPoint:point toView:window.contentView]];
        if (!target || ![target isDescendantOf:shell]) return false;
        for (auto type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
            auto* event = [NSEvent mouseEventWithType:type location:location
                modifierFlags:0 timestamp:0 windowNumber:window.windowNumber
                context:nil eventNumber:0 clickCount:1 pressure:type == NSEventTypeLeftMouseDown ? 1 : 0];
            if (type == NSEventTypeLeftMouseDown) [target mouseDown:event];
            else [target mouseUp:event];
        }
        return true;
    }
    [button performClick:nil];
    return true;
}

bool submitCommand(NSView* root, NSString* command)
{
    NSView* view = findAccessibleView(root, @"Live command input");
    if (![view isKindOfClass:NSTextField.class]) return false;
    NSTextField* field = static_cast<NSTextField*>(view);
    field.stringValue = command;
    return [field sendAction:field.action to:field.target];
}

NSWindow* visibleWindow(NSString* title)
{
    for (NSWindow* window in NSApp.windows) {
        if (window.visible && [window.title isEqualToString:title])
            return window;
    }
    return nil;
}

NSWindow* waitForOrderedDetachedWindow(NSString* title, NSWindow* parent)
{
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:0.5];
    do {
        NSWindow* window = visibleWindow(title);
        if (window && window.parentWindow == nil
            && window.level > parent.level && !window.hidesOnDeactivate)
            return window;
        [[NSRunLoop currentRunLoop] runUntilDate:
            [NSDate dateWithTimeIntervalSinceNow:0.01]];
    } while ([deadline timeIntervalSinceNow] > 0.0);
    return visibleWindow(title);
}

NSView* findAccessibleView(NSView* root, NSString* accessibilityLabel)
{
    if ([root.accessibilityLabel isEqualToString:accessibilityLabel])
        return root;
    for (NSView* child in root.subviews) {
        if (NSView* result = findAccessibleView(child, accessibilityLabel))
            return result;
    }
    return nil;
}

bool prepareGeometryPlaybackSnapshot(NSView* root)
{
    [root layoutSubtreeIfNeeded];
    NSView* geometry = findAccessibleView(root, @"Rhythm geometry");
    NSView* modeView = findAccessibleView(root, @"Geometry view mode");
    if (![modeView isKindOfClass:NSPopUpButton.class]) return false;
    NSPopUpButton* mode = static_cast<NSPopUpButton*>(modeView);
    [mode selectItemAtIndex:0];
    if (![mode sendAction:mode.action to:mode.target]) return false;
    SEL selector = NSSelectorFromString(
        @"prepareDocumentationPlaybackSnapshot");
    if (!geometry || ![geometry respondsToSelector:selector]) return false;
    using PrepareFunction = NSInteger (*)(id, SEL);
    auto prepare = reinterpret_cast<PrepareFunction>(
        [geometry methodForSelector:selector]);
    return prepare(geometry, selector) >= 4;
}

NSTableView* findTableView(NSView* root)
{
    if ([root isKindOfClass:NSTableView.class])
        return static_cast<NSTableView*>(root);
    for (NSView* child in root.subviews) {
        if (NSTableView* table = findTableView(child)) return table;
    }
    return nil;
}

bool prepareDocumentationSongMuteControls(NSView* root)
{
    NSTableView* table = findTableView(root);
    if (!table) return false;
    NSInteger muteColumn = -1;
    for (NSUInteger column = 0u;
         column < table.tableColumns.count; ++column) {
        if ([table.tableColumns[column].identifier isEqualToString:@"mutes"]) {
            muteColumn = static_cast<NSInteger>(column);
            break;
        }
    }
    if (muteColumn < 0 || table.numberOfRows < 1) return false;
    for (NSInteger row = 0; row < table.numberOfRows; ++row) {
        (void)[table viewAtColumn:muteColumn row:row makeIfNecessary:YES];
    }
    [table layoutSubtreeIfNeeded];
    return true;
}

bool setDocumentationSongPatternLoop(NSView* root, NSUInteger row,
    NSInteger loopStart, NSInteger loopEnd)
{
    NSTableView* table = findTableView(root);
    if (!table || row >= static_cast<NSUInteger>(table.numberOfRows))
        return false;
    NSInteger loopColumn = -1;
    for (NSUInteger column = 0u;
         column < table.tableColumns.count; ++column) {
        if ([table.tableColumns[column].identifier
                isEqualToString:@"patternLoop"]) {
            loopColumn = static_cast<NSInteger>(column);
            break;
        }
    }
    if (loopColumn < 0) return false;
    NSView* cell = [table viewAtColumn:loopColumn
        row:static_cast<NSInteger>(row) makeIfNecessary:YES];
    NSString* inLabel = [NSString stringWithFormat:
        @"Song row %lu pattern loop in",
        static_cast<unsigned long>(row + 1u)];
    NSView* inView = findAccessibleView(cell, inLabel);
    if (![inView isKindOfClass:NSPopUpButton.class]) return false;
    NSPopUpButton* inPopup = static_cast<NSPopUpButton*>(inView);
    const NSInteger inIndex = [inPopup
        indexOfItemWithRepresentedObject:@(loopStart)];
    if (inIndex < 0) return false;
    [inPopup selectItemAtIndex:inIndex];
    if (![inPopup sendAction:inPopup.action to:inPopup.target]) return false;

    cell = [table viewAtColumn:loopColumn
        row:static_cast<NSInteger>(row) makeIfNecessary:YES];
    NSString* outLabel = [NSString stringWithFormat:
        @"Song row %lu pattern loop out",
        static_cast<unsigned long>(row + 1u)];
    NSView* outView = findAccessibleView(cell, outLabel);
    if (![outView isKindOfClass:NSPopUpButton.class]) return false;
    NSPopUpButton* outPopup = static_cast<NSPopUpButton*>(outView);
    const NSInteger outIndex = [outPopup
        indexOfItemWithRepresentedObject:@(loopEnd)];
    if (outIndex < 0) return false;
    [outPopup selectItemAtIndex:outIndex];
    return [outPopup sendAction:outPopup.action to:outPopup.target];
}

bool selectTrackerGridVolumeField(NSView* root)
{
    NSView* grid = findAccessibleView(root, @"Editable tracker lanes");
    if (!grid) return false;
    [grid.window makeFirstResponder:grid];
    for (NSUInteger attempt = 0u; attempt < 6u; ++attempt) {
        NSString* value = [grid.accessibilityValue isKindOfClass:NSString.class]
            ? static_cast<NSString*>(grid.accessibilityValue) : @"";
        if ([value containsString:@", Volume,"]) return true;
        NSEvent* rightArrow = [NSEvent
            keyEventWithType:NSEventTypeKeyDown
            location:NSZeroPoint
            modifierFlags:0u
            timestamp:NSProcessInfo.processInfo.systemUptime
            windowNumber:grid.window.windowNumber
            context:nil
            characters:@"\uf703"
            charactersIgnoringModifiers:@"\uf703"
            isARepeat:NO
            keyCode:124u];
        [grid keyDown:rightArrow];
    }
    return false;
}

using NativeDrawRect = void (*)(id, SEL, NSRect);
NativeDrawRect referenceGridDraw = nullptr;
NativeDrawRect referenceGutterDraw = nullptr;

void printClippedGrid(id object, SEL selector, NSRect dirty)
{
    NSView* view = static_cast<NSView*>(object);
    NSClipView* clip = view.enclosingScrollView.contentView;
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(clip ? [view convertRect:clip.bounds fromView:clip] : view.bounds);
    referenceGridDraw(object, selector, dirty);
    [NSGraphicsContext restoreGraphicsState];
}

void printClippedGutter(id object, SEL selector, NSRect dirty)
{
    NSView* view = static_cast<NSView*>(object);
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(view.bounds);
    referenceGutterDraw(object, selector, dirty);
    [NSGraphicsContext restoreGraphicsState];
}

bool writeDocumentationPage(NSView* root, NSString* directory,
    NSString* variant)
{
    [root setNeedsDisplay:YES];
    [root layoutSubtreeIfNeeded];
    [root displayIfNeeded];
    // AppKit's PDF path omits the layer-backed viewport clip at >100% zoom;
    // clipsToBounds alone does not fix that print path. Apply the actual
    // viewport clip around the unchanged native drawing method for this test
    // capture only, then immediately restore the methods. Both bundles get
    // the same treatment. No plugin code or normal on-screen drawing changes.
    NSView* grid = findAccessibleView(root, @"Editable tracker lanes");
    NSView* gutter = findAccessibleView(root, @"Frozen tracker row numbers");
    Method gridDraw = grid ? class_getInstanceMethod(grid.class, @selector(drawRect:)) : nullptr;
    Method gutterDraw = gutter ? class_getInstanceMethod(gutter.class, @selector(drawRect:)) : nullptr;
    if (gridDraw) referenceGridDraw = reinterpret_cast<NativeDrawRect>(
        method_setImplementation(gridDraw, reinterpret_cast<IMP>(printClippedGrid)));
    if (gutterDraw) referenceGutterDraw = reinterpret_cast<NativeDrawRect>(
        method_setImplementation(gutterDraw, reinterpret_cast<IMP>(printClippedGutter)));
    NSData* rendered = [root dataWithPDFInsideRect:root.bounds];
    if (gridDraw) method_setImplementation(gridDraw, reinterpret_cast<IMP>(referenceGridDraw));
    if (gutterDraw) method_setImplementation(gutterDraw, reinterpret_cast<IMP>(referenceGutterDraw));
    if (!rendered || rendered.length == 0u) return false;
    if (!directory) return true;
    [[NSFileManager defaultManager]
        createDirectoryAtPath:directory
        withIntermediateDirectories:YES
        attributes:nil
        error:nil];
    NSString* fileName = variant.length == 0u
        ? @"org.s3g.s3g-dsp.tracker.pdf"
        : [NSString stringWithFormat:@"org.s3g.s3g-dsp.tracker.%@.pdf",
            variant];
    NSString* path = [directory stringByAppendingPathComponent:fileName];
    bool written = [rendered writeToFile:path atomically:YES];
    NSClipView* viewport = grid.enclosingScrollView.contentView;
    if (viewport) {
        const NSRect rect = [viewport convertRect:viewport.bounds toView:root];
        const double top = root.isFlipped ? NSMinY(rect) - NSMinY(root.bounds)
            : NSMaxY(root.bounds) - NSMaxY(rect);
        // PDF text extraction includes glyphs hidden by clipping paths. Record
        // the actual viewport so the checker compares visible grid text while
        // still comparing the complete page raster (all controls/panels).
        NSDictionary* metadata = @{@"trackerViewport": @[
            @(rect.origin.x - NSMinX(root.bounds)), @(top),
            @(rect.size.width), @(rect.size.height)]};
        NSData* json = [NSJSONSerialization dataWithJSONObject:metadata options:0 error:nil];
        written &= [json writeToFile:[path stringByAppendingString:@".json"] atomically:YES];
    }
    return written;
}

// Check geometry and native event coordinates, not just a stored zoom value.
bool checkWholeInterfaceScaling(const clap_plugin_gui_t* gui,
    const clap_plugin_t* plugin, NSView* root, NSWindow* hostWindow)
{
    clap_gui_resize_hints_t hints {};
    if (!gui->get_resize_hints(plugin, &hints)) return false;
    if (!hints.preserve_aspect_ratio)
        return expect(!std::getenv("S3G_TRACKER_EXPECT_VSTGUI"),
            "VSTGUI Tracker must advertise proportional resizing");
    bool ok = expect(hints.can_resize_horizontally && hints.can_resize_vertically
        && hints.aspect_ratio_width == 1320u && hints.aspect_ratio_height == 860u,
        "Tracker aspect-ratio hints changed");
    uint32_t savedWidth = 0u, savedHeight = 0u;
    gui->get_size(plugin, &savedWidth, &savedHeight);
    ok &= expect(!gui->set_scale(plugin, 2.0),
        "Cocoa must not apply Retina DPI as a second user zoom");
    NSView* workspace = findAccessibleView(root, @"s3g Tracker REAPER page workspace");
    NSView* grid = findAccessibleView(root, @"Editable tracker lanes");
    NSScrollView* scroll = grid.enclosingScrollView;
    const CGFloat savedGridZoom = scroll.magnification;
    const char* capturePath = std::getenv("S3G_TRACKER_SCALE_CAPTURE_DIR");
    NSString* captureDir = capturePath && capturePath[0]
        ? [NSString stringWithUTF8String:capturePath] : nil;
    const auto resize = [&](uint32_t w, uint32_t h, bool hostFirst) {
        if (hostFirst) [hostWindow setContentSize:NSMakeSize(w, h)];
        bool result = gui->set_size(plugin, w, h);
        if (!hostFirst) [hostWindow setContentSize:NSMakeSize(w, h)];
        [root layoutSubtreeIfNeeded];
        [root displayIfNeeded];
        uint32_t actualW = 0u, actualH = 0u;
        return result && gui->get_size(plugin, &actualW, &actualH)
            && actualW == w && actualH == h;
    };
    const auto eventAt = [&](NSView* view, NSPoint point, NSInteger clicks) {
        return [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
            location:[view convertPoint:point toView:nil] modifierFlags:0
            timestamp:0 windowNumber:hostWindow.windowNumber context:nil
            eventNumber:0 clickCount:clicks pressure:1.0];
    };
    const auto hits = [&](NSView* view, NSPoint point) {
        NSPoint inParent = [view convertPoint:point toView:root.superview];
        NSView* hit = [root hitTest:inParent];
        if (hit != view && ![hit isDescendantOf:view]) std::fprintf(stderr,
            "hit-test %s at %.1f,%.1f -> %s (root %s)\n",
            view.accessibilityLabel.UTF8String, inParent.x, inParent.y,
            NSStringFromClass(hit.class).UTF8String,
            NSStringFromRect(root.frame).UTF8String);
        return hit == view || [hit isDescendantOf:view];
    };
    const std::array<std::array<uint32_t, 2>, 7> sizes {{
        {{858, 559}}, {{1320, 860}}, {{1980, 1290}}, {{2640, 1720}},
        {{900, 620}}, {{1, 1}}, {{5000, 5000}}
    }};
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        uint32_t w = sizes[index][0], h = sizes[index][1];
        ok &= expect(gui->adjust_size(plugin, &w, &h), "Tracker size negotiation failed");
        if (index == 0 || index == 5)
            ok &= expect(w == 858u && h == 559u, "65% lower resize limit changed");
        if (index == 3 || index == 6)
            ok &= expect(w == 2640u && h == 1720u, "200% upper resize limit changed");
        const double retainedZoom = index % 2 == 0 ? 0.8 : 1.4;
        [scroll setMagnification:retainedZoom centeredAtPoint:NSZeroPoint];
        const double scale = std::min(w / 1320.0, h / 860.0);
        uint32_t repeatedW = w, repeatedH = h;
        for (int repeat = 0; repeat < 8; ++repeat)
            gui->adjust_size(plugin, &repeatedW, &repeatedH);
        ok &= expect(scale >= 0.65 && scale <= 2.0
                && repeatedW == w && repeatedH == h
                && resize(w, h, index % 2 == 0),
            "Tracker resize limits, rounding or host resize ordering failed");
        const NSRect physical = [workspace convertRect:workspace.bounds toView:root];
        NSButton* page = findButton(root, nil, @"TRACKER page", nil);
        NSTextField* bpm = static_cast<NSTextField*>(findAccessibleView(root,
            @"Host tempo in beats per minute"));
        ok &= expect(NSEqualSizes(workspace.bounds.size, NSMakeSize(1320, 860))
                && std::abs(NSWidth(physical) - 1320 * scale) < 0.01
                && std::abs(NSHeight(physical) - 860 * scale) < 0.01
                && std::abs([page convertSize:page.bounds.size toView:root].width
                    - page.bounds.size.width * scale) < 0.01
                && std::abs([bpm convertSize:NSMakeSize(0, bpm.font.pointSize)
                    toView:root].height - bpm.font.pointSize * scale) < 0.01
                && hits(page, NSMakePoint(NSMidX(page.bounds), NSMidY(page.bounds)))
                && std::abs(scroll.magnification - retainedZoom) < 0.0001,
            "whole-interface geometry, typography, button hit area or independent grid zoom failed");
        for (double gridZoom : {0.55, 1.0, 1.8}) {
            [scroll setMagnification:gridZoom centeredAtPoint:NSZeroPoint];
            [grid scrollPoint:NSZeroPoint];
            [root layoutSubtreeIfNeeded];
            // Original geometry: NOTE lane 1, row 2 (86 header + 25 per row).
            const NSPoint cell = NSMakePoint(60, 123.5);
            ok &= expect(hits(grid, cell), "scaled grid cell hit test failed");
            [grid mouseDown:eventAt(grid, cell, 2)];
            NSTextField* editor = [grid valueForKey:@"cellEditor"];
            const NSSize physicalEditor = [editor convertSize:editor.bounds.size toView:root];
            ok &= expect(editor && [[grid valueForKey:@"editingTrack"] unsignedIntegerValue] == 0
                    && [[grid valueForKey:@"editingRow"] unsignedIntegerValue] == 1
                    && [[grid valueForKey:@"editingField"] unsignedIntegerValue] == 0
                    && std::abs(physicalEditor.width
                        - editor.bounds.size.width * scale * gridZoom) < 0.01
                    && [hostWindow.firstResponder isKindOfClass:NSTextView.class],
                "scaled double-click or inline cell editor failed");
            if (editor) [(id<NSTextFieldDelegate>)grid control:editor
                textView:static_cast<NSTextView*>(hostWindow.firstResponder)
                doCommandBySelector:@selector(cancelOperation:)];
        }
        [scroll setMagnification:savedGridZoom centeredAtPoint:NSZeroPoint];
        [grid scrollPoint:NSZeroPoint];
        if (captureDir && index < 4)
            ok &= expect(writeDocumentationPage(root, captureDir,
                [NSString stringWithFormat:@"interface-%03d", static_cast<int>(std::lround(scale * 100))]),
                "whole-interface capture failed");

        // Canvas dropdowns must stay inside the scaled hierarchy as well.
        [root layoutSubtreeIfNeeded];
        NSPopUpButton* menu = static_cast<NSPopUpButton*>(
            findAccessibleView(root, @"Active pattern"));
        const NSPoint center = NSMakePoint(NSMidX(menu.bounds), NSMidY(menu.bounds));
        ok &= expect(hits(menu, center), "scaled dropdown hit test failed");
        [menu mouseDown:eventAt(menu, center, 1)];
        NSView* overlay = [menu valueForKey:@"s3gMenuOverlay"];
        ok &= expect(overlay && [overlay isDescendantOf:workspace]
                && std::abs([overlay convertSize:NSMakeSize(21, 21) toView:root].height
                    - 21 * scale) < 0.01,
            "canvas dropdown did not inherit whole-interface zoom");
        if (overlay) {
            // Select the current item through window coordinates, without
            // changing the pattern used by the MIDI fixture after this test.
            NSRect menuRect = [[overlay valueForKey:@"menuRect"] rectValue];
            const NSInteger columns = [[overlay valueForKey:@"columns"] integerValue];
            const NSInteger rows = (menu.numberOfItems + columns - 1) / columns;
            const NSInteger item = menu.indexOfSelectedItem;
            [overlay mouseDown:eventAt(overlay,
                NSMakePoint(NSMinX(menuRect) + (item / rows) * NSWidth(menuRect) / columns + 10,
                    NSMinY(menuRect) + (item % rows) * 21 + 10), 1)];
            ok &= expect([menu valueForKey:@"s3gMenuOverlay"] == nil,
                "scaled dropdown selection did not dismiss the menu");
        }
    }
    ok &= expect(resize(savedWidth, savedHeight, true), "Tracker size restore failed");
    if (ok) std::puts("Tracker whole-interface 65–200% scaling / nested zoom / input: ok");
    return ok;
}

} // namespace

#endif

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 5) {
        std::fprintf(stderr,
            "usage: tracker_clap_smoke <bundle-or-binary> "
            "[plugin-id width height]\n");
        return 2;
    }
    uint32_t requestedWidth = 900u;
    uint32_t requestedHeight = 620u;
    if (argc == 5) {
        if (std::strcmp(argv[2], "org.s3g.s3g-dsp.tracker") != 0) {
            std::fprintf(stderr, "tracker CLAP: unexpected plugin ID %s\n", argv[2]);
            return 2;
        }
        char* widthEnd = nullptr;
        char* heightEnd = nullptr;
        const unsigned long parsedWidth = std::strtoul(argv[3], &widthEnd, 10);
        const unsigned long parsedHeight = std::strtoul(argv[4], &heightEnd, 10);
        if (!widthEnd || *widthEnd || !heightEnd || *heightEnd
            || parsedWidth < 760u || parsedWidth > UINT32_MAX
            || parsedHeight < 559u || parsedHeight > UINT32_MAX) {
            std::fprintf(stderr, "tracker CLAP: invalid capture size %s x %s\n",
                argv[3], argv[4]);
            return 2;
        }
        requestedWidth = static_cast<uint32_t>(parsedWidth);
        requestedHeight = static_cast<uint32_t>(parsedHeight);
    }
    bool ok = true;
    const std::string binary = resolveBinary(argv[1]);
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    const char* loadError = library ? nullptr : dlerror();
    ok &= expect(library != nullptr, loadError ? loadError : "dlopen failed");
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry")) : nullptr;
    ok &= expect(entry && entry->init(binary.c_str()), "entry initialization failed");

    HostContext context;
    context.state.mark_dirty = hostMarkDirty;
    context.reaper.getFunction = reaperGetFunction;
    context.reaper.registerObject = reaperRegisterObject;
    activeReaperHost = &context;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = "s3g tracker smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequest;
    context.host.request_process = hostRequest;
    context.host.request_callback = hostRequest;

    const auto* factory = entry ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(factory,
        &context.host, "org.s3g.s3g-dsp.tracker") : nullptr;
    ok &= expect(plugin && plugin->init(plugin), "plugin creation failed");

    const auto* notePorts = plugin ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* state = plugin ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
#if defined(__APPLE__)
    const auto* gui = plugin ? static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI)) : nullptr;
#endif
    clap_note_port_info_t inputPort {};
    clap_note_port_info_t outputPort {};
    ok &= expect(notePorts && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 1u
        && notePorts->get(plugin, 0u, true, &inputPort)
        && notePorts->get(plugin, 0u, false, &outputPort)
        && !notePorts->get(plugin, 1u, true, &inputPort)
        && !notePorts->get(plugin, 1u, false, &outputPort)
        && inputPort.preferred_dialect == CLAP_NOTE_DIALECT_MIDI
        && outputPort.preferred_dialect == CLAP_NOTE_DIALECT_MIDI
        && std::strcmp(inputPort.name, "MIDI Record Input") == 0
        && std::strcmp(outputPort.name, "Tracker MIDI Output") == 0,
        "plugin should expose one record input and one channel-addressed output");
    ok &= expect(plugin && !plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS),
        "tracker should not expose audio ports");
    ok &= expect(state != nullptr, "project state extension is missing");
    StateBuffer factoryState;
    const bool factoryStateSaved = state
        && state->save(plugin, &factoryState.output);
    const std::string factoryJson(factoryState.bytes.begin(),
        factoryState.bytes.end());
    const auto countText = [&factoryJson](std::string_view needle) {
        std::size_t count = 0u;
        for (std::size_t at = factoryJson.find(needle);
             at != std::string::npos;
             at = factoryJson.find(needle, at + needle.size())) ++count;
        return count;
    };
    const auto defaultsKey = factoryJson.find("\"laneDefaultNotes\"");
    const auto defaultsBegin = defaultsKey == std::string::npos
        ? std::string::npos : factoryJson.find('[', defaultsKey);
    const auto defaultsEnd = defaultsBegin == std::string::npos
        ? std::string::npos : factoryJson.find(']', defaultsBegin);
    std::string compactDefaults;
    if (defaultsEnd != std::string::npos) {
        for (std::size_t at = defaultsBegin + 1u; at < defaultsEnd; ++at) {
            const char value = factoryJson[at];
            if ((value >= '0' && value <= '9') || value == ',')
                compactDefaults += value;
        }
    }
    ok &= expect(factoryStateSaved
            && factoryJson.find(
                "\"format\": \"s3g-tracker-midi-composition\"")
                != std::string::npos
            && factoryJson.find("\"version\": 3") != std::string::npos
            && factoryJson.find("\"burstBanks\"") != std::string::npos
            && factoryJson.find("\"phraseBanks\"") != std::string::npos
            && factoryJson.find("instrumentRack") == std::string::npos
            && factoryJson.find("sampleRate") == std::string::npos
            && factoryJson.find("\"showMidiNoteValues\": true")
                != std::string::npos
            && factoryJson.find("\"trackerRowJump\": 1")
                != std::string::npos
            && countText("\"midiChannel\": 1") == 4u
            && compactDefaults == "36,38,41,61"
            && countText("\"note\": 36") == 4u
            && countText("\"note\": 38") == 2u
            && countText("\"note\": 41") == 2u
            && countText("\"note\": 61") == 8u
            && factoryJson.find("\"name\": \"FOUR ON THE FLOOR\"")
                != std::string::npos
            && factoryJson.find("\"name\": \"Kick\"")
                != std::string::npos
            && factoryJson.find("\"name\": \"Snare\"")
                != std::string::npos
            && factoryJson.find("\"name\": \"Tom\"")
                != std::string::npos
            && factoryJson.find("\"name\": \"Hat\"")
                != std::string::npos,
        "fresh Tracker state is not the four-lane Superior Drummer groove");
#if defined(__APPLE__)
    ok &= expect(gui
        && gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false)
        && gui->can_resize(plugin), "resizable Cocoa editor is missing");
    if (gui) {
        @autoreleasepool {
            (void)[NSApplication sharedApplication];
            uint32_t width = 0u;
            uint32_t height = 0u;
            ok &= expect(gui->create(plugin, CLAP_WINDOW_API_COCOA, false)
                    && gui->get_size(plugin, &width, &height)
                    && width >= 858u && width <= 1320u
                    && height >= 559u && height <= 860u,
                "full tracker workspace could not be constructed");
            // Initial size fits the current NSScreen. Test geometry must not
            // depend on which display was active when the suite started.
            ok &= expect(gui->set_size(plugin, 1320u, 860u)
                    && gui->get_size(plugin, &width, &height)
                    && width == 1320u && height == 860u,
                "tracker workspace could not adopt the reference size");
            NSView* parent = [[S3GTrackerCaptureHostView alloc]
                initWithFrame:NSMakeRect(
                0.0, 0.0, width, height)];
            NSWindow* hostWindow = nil;
            clap_window_t window {};
            window.api = CLAP_WINDOW_API_COCOA;
            window.cocoa = (__bridge clap_nsview)parent;
            uint32_t resizedWidth = requestedWidth;
            uint32_t resizedHeight = requestedHeight;
            const bool shown = gui->set_parent(plugin, &window)
                    && gui->adjust_size(plugin, &resizedWidth, &resizedHeight)
                    && gui->set_size(plugin, resizedWidth, resizedHeight)
                    && gui->show(plugin);
            ok &= expect(shown,
                "full tracker workspace lifecycle failed");
            NSView* portableMain = findAccessibleView(parent, @"Tracker portable main page");
            if (shown && portableMain) {
                hostWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,resizedWidth,resizedHeight)
                    styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
                hostWindow.releasedWhenClosed = NO;
                hostWindow.contentView = parent;
                [NSApp activateIgnoringOtherApps:YES];
                [hostWindow makeKeyAndOrderFront:nil];
                const auto pump = [&] {
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
                    [parent layoutSubtreeIfNeeded]; [parent displayIfNeeded];
                };
                const auto click = [&](NSPoint point, NSInteger clicks) {
                    const NSPoint location = [portableMain convertPoint:point toView:nil];
                    NSView* target = [parent hitTest:[parent convertPoint:point fromView:portableMain]];
                    [hostWindow makeFirstResponder:parent];
                    [hostWindow makeFirstResponder:target];
                    NSEvent* down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:location
                        modifierFlags:0 timestamp:0 windowNumber:hostWindow.windowNumber context:nil
                        eventNumber:0 clickCount:clicks pressure:1];
                    [target mouseDown:down];
                    NSEvent* up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:location
                        modifierFlags:0 timestamp:0 windowNumber:hostWindow.windowNumber context:nil
                        eventNumber:0 clickCount:clicks pressure:0];
                    [target mouseUp:up]; pump();
                };
                const auto key = [&](NSString* characters, unsigned short code, NSEventModifierFlags flags) {
                    NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                        modifierFlags:flags timestamp:0 windowNumber:hostWindow.windowNumber context:nil
                        characters:characters charactersIgnoringModifiers:characters isARepeat:NO keyCode:code];
                    [hostWindow.firstResponder keyDown:event]; pump();
                };
                pump();
                ok &= expect(findAccessibleView(parent,@"Editable tracker lanes") == nil
                    && portableMain.subviews.count > 0,
                    "portable Tracker unexpectedly retained the native grid or failed to attach CFrame");
                if (std::getenv("S3G_TRACKER_EXPECT_PORTABLE_SHELL")) {
                    NSView* shell = findAccessibleView(parent, @"Tracker portable workspace shell");
                    ok &= expect(shell && shell.subviews.count > 0
                        && !findButton(parent,nil,@"TRACKER page",nil),
                        "portable shell retained Cocoa tab buttons or failed to attach");
                    if (shell) {
                        NSRect bpm = [shell shellControlRect:@"Host tempo in beats per minute"];
                        NSRect events = [shell shellControlRect:@"MIDI event statistics"];
                        ok &= expect(NSMaxX(events)<NSMinX(bpm)
                            && [[shell shellStatusText:@"MIDI event statistics"] containsString:@"SEND"],
                            "portable MIDI status/BPM layout or contents changed");
                        context.masterTempo=97.5; pump(); pump();
                        ok &= expect([[shell shellStatusText:@"Host tempo in beats per minute"] isEqualToString:@"HOST BPM  97.50"],
                            "portable shell stopped host tempo refresh failed");
                        context.masterTempo=120; pump();
                    }
                }
                clap_gui_resize_hints_t hints {};
                ok &= expect(gui->get_resize_hints(plugin,&hints) && hints.preserve_aspect_ratio
                    && hints.aspect_ratio_width==1320 && hints.aspect_ratio_height==860,
                    "portable Tracker lost proportional outer resizing");
                for (double scale : {0.65,1.0,1.5,2.0}) {
                    uint32_t w=static_cast<uint32_t>(1320*scale),h=static_cast<uint32_t>(860*scale);
                    ok &= expect(gui->adjust_size(plugin,&w,&h) && gui->set_size(plugin,w,h),
                        "portable Tracker rejected supported outer size");
                    [parent setFrameSize:NSMakeSize(w,h)]; [hostWindow setContentSize:NSMakeSize(w,h)]; pump();
                    NSRect physical=[portableMain convertRect:portableMain.bounds toView:parent];
                    const bool logicalSize = std::abs(NSWidth(portableMain.bounds)-1320)<1e-6
                        && std::abs(NSHeight(portableMain.bounds)-820)<1e-6;
                    const bool physicalSize = std::abs(NSWidth(physical)-1320*scale)<1.1;
                    if(!logicalSize||!physicalSize)std::fprintf(stderr,
                        "Tracker scale %.2f logical %.9f x %.9f physical %.9f x %.9f\n",scale,
                        NSWidth(portableMain.bounds),NSHeight(portableMain.bounds),NSWidth(physical),NSHeight(physical));
                    ok &= expect(logicalSize && physicalSize,
                        "portable main page changed logical size instead of scaling");
                    click(NSMakePoint(60,115+86+12),2);
                    key(@"a",0,NSEventModifierFlagCommand);key(@"8",28,0);key(@"8",28,0);key(@"\r",36,0);
                    StateBuffer edited;
                    ok &= expect(state->save(plugin,&edited.output),"portable edited state could not be saved");
                    const std::string json(edited.bytes.begin(),edited.bytes.end());
                    if(json.find("\"note\": 88")==std::string::npos)
                        std::fprintf(stderr,"Tracker failed scale %.2f, responder %s\n",scale,NSStringFromClass([hostWindow.firstResponder class]).UTF8String);
                    ok &= expect(json.find("\"note\": 88")!=std::string::npos,
                        "scaled native-to-VSTGUI pointer/keyboard edit did not reach CLAP state");
                    factoryState.cursor=0;
                    ok &= expect(state->load(plugin,&factoryState.input),"portable editor fixture restore failed");pump();
                }
                gui->set_size(plugin,1320,860);[parent setFrameSize:NSMakeSize(1320,860)];
                [hostWindow setContentSize:NSMakeSize(1320,860)];pump();
                click(NSMakePoint(950,44),1);
                StateBuffer expanded;state->save(plugin,&expanded.output);
                const std::string expandedJson(expanded.bytes.begin(),expanded.bytes.end());
                ok &= expect(!expandedJson.empty(),"portable view toggle interrupted CLAP state save");
                const char* captures=std::getenv("S3G_TRACKER_MAIN_CLAP_CAPTURE_DIR");
                if(captures)ok &= expect(writeDocumentationPage(parent,[NSString stringWithUTF8String:captures],@"portable-main"),
                    "portable CLAP page capture failed");
                for(NSString* page in @[@"SONG page",@"GEOMETRY page",@"BURSTS page",@"PHRASES page",@"ASSEMBLE page",@"RESHAPE page",@"WARPS page",@"CONSOLE page",@"HELP page",@"TRACKER page"]){
                    ok &= expect(clickButton(parent,nil,page,nil),"portable/native page switch failed");pump();
                }
                NSView* portableWarps = findAccessibleView(parent, @"Tracker portable Warps page");
                if(std::getenv("S3G_TRACKER_EXPECT_PORTABLE_GEOMETRY")) {
                    for(NSString* label in @[@"Rhythm geometry",@"Burst editor"]) {
                        const bool bursts=[label isEqualToString:@"Burst editor"];
                        NSString* tab=bursts?@"BURSTS page":@"GEOMETRY page";
                        clickButton(parent,nil,tab,nil); pump();
                        NSView* page=findAccessibleView(parent,label);
                        ok &= expect(page && [page respondsToSelector:@selector(geometryMenuItems)],"portable Geometry/Bursts host missing");
                        if(!page) continue;
                        auto nativeClick=[&](NSPoint point) {
                            NSWindow* window=page.window;
                            NSPoint location=[page convertPoint:point toView:nil];
                            NSView* target=[window.contentView hitTest:[page convertPoint:point toView:window.contentView]];
                            [window makeFirstResponder:target];
                            for(NSEventType type:{NSEventTypeLeftMouseDown,NSEventTypeLeftMouseUp}) {
                                NSEvent* event=[NSEvent mouseEventWithType:type location:location modifierFlags:0 timestamp:0
                                    windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:type==NSEventTypeLeftMouseDown?1:0];
                                if(type==NSEventTypeLeftMouseDown) [target mouseDown:event]; else [target mouseUp:event];
                            }
                            pump();
                        };
                        for(double scale:{.65,1.,1.5,2.,1.}) {
                            uint32_t w=uint32_t(1320*scale),h=uint32_t(860*scale);
                            ok &= expect(gui->adjust_size(plugin,&w,&h)&&gui->set_size(plugin,w,h),"Geometry/Bursts resize failed");
                            [parent setFrameSize:NSMakeSize(w,h)]; [hostWindow setContentSize:NSMakeSize(w,h)]; pump();
                            NSRect physical=[page convertRect:page.bounds toView:parent];
                            ok &= expect(std::abs(NSWidth(physical)-NSWidth(page.bounds)*scale)<1.1,"Geometry/Bursts proportional page scale mismatch");
                            if(!bursts) {
                                [page selectGeometryMode:0];
                                // MODE menu at the shared family row; choose Active Pulses.
                                nativeClick(NSMakePoint(1180,44));
                                nativeClick(NSMakePoint(1180,91));
                                ok &= expect([[page accessibilityValue] isEqualToString:@"ACTIVE PULSES"],"scaled Geometry native menu click missed");
                            } else {
                                auto box=[page burstPreviewChannelMenuBoxRect];
                                nativeClick(NSMakePoint(NSMidX(box),NSMidY(box)));
                                NSEvent* escape=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
                                    windowNumber:page.window.windowNumber context:nil characters:@"\033" charactersIgnoringModifiers:@"\033" isARepeat:NO keyCode:53];
                                [page.window.firstResponder keyDown:escape]; pump();
                            }
                        }
                        ok &= expect(clickButton(parent,nil,@"Detach selected tool page",nil),"Geometry/Bursts detach failed");
                        NSWindow* detached=waitForOrderedDetachedWindow(bursts?@"s3g Tracker — Bursts":@"s3g Tracker — Rhythm Geometry",hostWindow);
                        ok &= expect(detached&&page.window==detached&&detached.level>hostWindow.level,"Geometry/Bursts detached ordering");
                        if(detached) {
                            clickButton(parent,nil,@"TRACKER page",nil); pump();
                            ok &= expect(page.window==detached&&detached.visible,"detached Geometry/Bursts hidden by page switch");
                            [detached performClose:nil]; pump();
                            ok &= expect(page.window==hostWindow,"Geometry/Bursts CFrame did not reattach");
                        }
                    }
                    clickButton(parent,nil,@"TRACKER page",nil); pump();
                    [hostWindow makeKeyAndOrderFront:nil]; pump();
                }
                if (std::getenv("S3G_TRACKER_EXPECT_PORTABLE_WARPS"))
                    ok &= expect(portableWarps != nil, "expected portable Warps page is missing");
                if (portableWarps) {
                    ok &= expect(clickButton(parent, nil, @"WARPS page", nil), "portable Warps page switch failed");
                    pump();
                    const auto warpClick = [&](NSPoint point, NSInteger count) {
                        NSWindow* window = portableWarps.window;
                        // Detach reparents/resizes immediately, but the first
                        // paint (which rebuilds Warps' hit map) is deferred.
                        // Test the displayed controls, not the previous width.
                        [portableWarps layoutSubtreeIfNeeded];
                        [portableWarps display];
                        NSPoint location = [portableWarps convertPoint:point toView:nil];
                        NSView* target = [window.contentView hitTest:[portableWarps convertPoint:point toView:window.contentView]];
                        [window makeFirstResponder:target];
                        NSEvent* down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:location
                            modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil
                            eventNumber:0 clickCount:count pressure:1];
                        [target mouseDown:down];
                        NSEvent* up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:location
                            modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil
                            eventNumber:0 clickCount:count pressure:0];
                        [target mouseUp:up]; pump();
                    };
                    const auto warpCycle = [&](NSString* value) {
                        // Use the right-side numeric value, avoiding the drag track.
                        warpClick(NSMakePoint(NSWidth(portableWarps.bounds) - 40, 186), 2);
                        if (![portableWarps s3gTrackerHasFocusedTextInput]) {
                            std::fprintf(stderr,"Warps text unfocused; host %s; cycle %s; native %s; hidden %d\n",
                                NSStringFromRect(portableWarps.bounds).UTF8String,
                                NSStringFromRect([portableWarps warpControlRect:@"cycle"]).UTF8String,
                                NSStringFromRect(portableWarps.subviews.firstObject.frame).UTF8String,
                                portableWarps.hiddenOrHasHiddenAncestor);
                        }
                        for (NSUInteger i = 0; i <= value.length; ++i) {
                            NSString* chars = i == value.length ? @"\r" : [value substringWithRange:NSMakeRange(i, 1)];
                            NSWindow* window = portableWarps.window;
                            NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                                modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil
                                characters:chars charactersIgnoringModifiers:chars isARepeat:NO keyCode:i == value.length ? 36 : 0];
                            [window.firstResponder keyDown:event]; pump();
                        }
                        StateBuffer saved;
                        bool savedOK = state->save(plugin, &saved.output);
                        std::string json(saved.bytes.begin(), saved.bytes.end());
                        const std::string token = std::string("\"warpCycleTicks\": ") + value.UTF8String;
                        // Return queues a commit for the workspace refresh.
                        // Observe that asynchronous commit without repeating
                        // input or assuming the timer ran within one 50 ms pump.
                        NSDate* deadline=[NSDate dateWithTimeIntervalSinceNow:.5];
                        while(savedOK && json.find(token)==std::string::npos && [deadline timeIntervalSinceNow]>0) {
                            pump(); saved.bytes.clear();
                            savedOK=state->save(plugin,&saved.output);
                            json.assign(saved.bytes.begin(),saved.bytes.end());
                        }
                        if(!savedOK || json.find(token)==std::string::npos) {
                            const auto at=json.find("\"warpCycleTicks\"");
                            std::fprintf(stderr,"Warps expected %s; state %s; window %s; responder %s\n",value.UTF8String,
                                at==std::string::npos?"missing":json.substr(at,45).c_str(),portableWarps.window.title.UTF8String,
                                NSStringFromClass([portableWarps.window.firstResponder class]).UTF8String);
                        }
                        ok &= expect(savedOK && json.find(token) != std::string::npos,
                            "native Warps text edit did not reach serialized CLAP state");
                    };
                    warpClick(NSMakePoint(1096, 241), 1); // + EXP
                    for (double scale : {0.65, 1.0, 1.5, 2.0, 1.0}) {
                        uint32_t w = static_cast<uint32_t>(1320 * scale);
                        uint32_t h = static_cast<uint32_t>(860 * scale);
                        ok &= expect(gui->adjust_size(plugin, &w, &h) && gui->set_size(plugin, w, h),
                            "portable Warps rejected proportional outer resizing");
                        [parent setFrameSize:NSMakeSize(w, h)];
                        [hostWindow setContentSize:NSMakeSize(w, h)]; pump();
                        warpCycle(@"13");
                    }
                    ok &= expect(clickButton(parent, nil, @"Detach selected tool page", nil), "Warps detach failed");
                    NSWindow* detached = waitForOrderedDetachedWindow(@"s3g Tracker — Timing Warps", hostWindow);
                    ok &= expect(detached && portableWarps.window == detached && detached.parentWindow == nil
                        && detached.level > hostWindow.level && !detached.hidesOnDeactivate,
                        "portable Warps detached window ordering/ownership failed");
                    if (detached) {
                        warpCycle(@"9");
                        ok &= expect(clickButton(parent, nil, @"TRACKER page", nil), "main page switch while Warps detached failed");
                        pump();
                        ok &= expect(portableWarps.window == detached && detached.visible,
                            "switching main page hid detached Warps");
                        // A title-bar close uses performClose/shouldClose;
                        // NSWindow::close deliberately bypasses that delegate.
                        [detached performClose:nil]; pump();
                        ok &= expect(portableWarps.window == hostWindow, "Warps close did not reattach its CFrame");
                        clickButton(parent, nil, @"WARPS page", nil); pump();
                        warpCycle(@"11");
                    }
                    if (captures) ok &= expect(writeDocumentationPage(parent, [NSString stringWithUTF8String:captures], @"portable-warps"),
                        "portable Warps CLAP capture failed");
                    clickButton(parent, nil, @"TRACKER page", nil); pump();
                }
                NSView* portableSong = findAccessibleView(parent, @"Tracker portable Song page");
                if (std::getenv("S3G_TRACKER_EXPECT_PORTABLE_SONG"))
                    ok &= expect(portableSong != nil, "expected portable Song page is missing");
                if (portableSong) {
                    factoryState.cursor = 0; state->load(plugin, &factoryState.input); pump();
                    ok &= expect(clickButton(parent, nil, @"SONG page", nil), "portable Song page switch failed");
                    pump();
                    const auto songClick = [&](NSPoint point) {
                        NSPoint location = [portableSong convertPoint:point toView:nil];
                        NSView* target = [hostWindow.contentView hitTest:[portableSong convertPoint:point toView:hostWindow.contentView]];
                        [hostWindow makeFirstResponder:target];
                        NSEvent* down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:location
                            modifierFlags:0 timestamp:0 windowNumber:hostWindow.windowNumber context:nil
                            eventNumber:0 clickCount:1 pressure:1];
                        [target mouseDown:down];
                        NSEvent* up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:location
                            modifierFlags:0 timestamp:0 windowNumber:hostWindow.windowNumber context:nil
                            eventNumber:0 clickCount:1 pressure:0];
                        [target mouseUp:up]; pump();
                    };
                    const auto songDocument = [&]() -> NSDictionary* {
                        StateBuffer saved;
                        if (!state->save(plugin, &saved.output)) return nil;
                        NSData* bytes = [NSData dataWithBytes:saved.bytes.data() length:saved.bytes.size()];
                        return [NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil];
                    };
                    for (double scale : {0.65, 1.0, 1.5, 2.0}) {
                        uint32_t w=static_cast<uint32_t>(1320*scale), h=static_cast<uint32_t>(860*scale);
                        ok &= expect(gui->adjust_size(plugin,&w,&h) && gui->set_size(plugin,w,h), "Song resize negotiation");
                        [parent setFrameSize:NSMakeSize(w,h)]; [hostWindow setContentSize:NSMakeSize(w,h)]; pump();
                        NSUInteger before = [songDocument()[@"arrangement"][@"rows"] count];
                        uint32_t dirtyBefore = context.dirtyMarks;
                        songClick(NSMakePoint(980,40)); // + ADD in ROW EDIT
                        NSDictionary* saved = songDocument();
                        ok &= expect([saved[@"arrangement"][@"rows"] count] == before+1 && context.dirtyMarks > dirtyBefore,
                            "scaled native Song ADD did not publish to serialized CLAP state");
                    }
                    gui->set_size(plugin,1320,860); [parent setFrameSize:NSMakeSize(1320,860)];
                    [hostWindow setContentSize:NSMakeSize(1320,860)]; pump();
                    // Native click -> canvas repeats menu -> coordinator -> project codec.
                    songClick(NSMakePoint(430,141));
                    key(@"\uf701",125,0); key(@"\uf701",125,0); key(@"\r",36,0);
                    NSDictionary* edited = songDocument();
                    ok &= expect([edited[@"arrangement"][@"rows"][0][@"repeats"] unsignedIntValue] == 3,
                        "portable Song menu did not reach the coordinator");
                    songClick(NSMakePoint(280,40));
                    ok &= expect([songDocument()[@"workspace"][@"songPlaybackEnabled"] boolValue],
                        "portable Song mode callback did not reach the project");
                    StateBuffer songRecall; state->save(plugin,&songRecall.output);
                    songClick(NSMakePoint(980,40));
                    songRecall.cursor=0;
                    ok &= expect(state->load(plugin,&songRecall.input),"portable Song project recall failed");pump();
                    ok &= expect([songDocument()[@"arrangement"][@"rows"] count] == [edited[@"arrangement"][@"rows"] count],
                        "Song restore did not discard later arrangement edit");
                    // Live edit publication and pending launch still use the existing
                    // audio-thread mailboxes; the CFrame never clocks Song itself.
                    bool processing=plugin->activate(plugin,48000,1,32768) && plugin->start_processing(plugin);
                    clap_event_transport_t transport{};transport.header.size=sizeof(transport);
                    transport.header.space_id=CLAP_CORE_EVENT_SPACE_ID;transport.header.type=CLAP_EVENT_TRANSPORT;
                    transport.flags=CLAP_TRANSPORT_HAS_TEMPO|CLAP_TRANSPORT_IS_PLAYING;transport.tempo=120;
                    OutputEvents output;clap_process_t process{};process.transport=&transport;
                    process.frames_count=128;process.out_events=&output.interface;
                    context.playState=1;
                    if(processing)processing=plugin->process(plugin,&process)==CLAP_PROCESS_CONTINUE;
                    pump();
                    NSUInteger before = [songDocument()[@"arrangement"][@"rows"] count];
                    songClick(NSMakePoint(980,40));
                    ok &= expect(processing&&[songDocument()[@"arrangement"][@"rows"] count]==before+1,
                        "Song editing should remain enabled during host playback");
                    songClick(NSMakePoint(610,40)); // SELECT QUEUE
                    for(int block=0;processing&&block<8;++block){output.count=0;process.steady_time+=process.frames_count;
                        processing=plugin->process(plugin,&process)==CLAP_PROCESS_CONTINUE;pump();}
                    ok &= expect(processing,"Song queue/live edit interrupted CLAP processing");
                    plugin->stop_processing(plugin);plugin->deactivate(plugin);context.playState=0;pump();
                    if(captures)ok &= expect(writeDocumentationPage(parent,@(captures),@"portable-song"),"Song CLAP capture failed");
                    clickButton(parent,nil,@"TRACKER page",nil);pump();
                }
                if (std::getenv("S3G_TRACKER_EXPECT_PORTABLE_REFERENCE")) {
                    NSView* console = findAccessibleView(parent, @"Tracker portable Console page");
                    NSView* help = findAccessibleView(parent, @"Tracker portable Help page");
                    ok &= expect(console && help, "expected portable Console/Help pages missing");
                    const auto referenceClick = [&](NSView* view, NSPoint point) {
                        NSWindow* window = view.window;
                        NSPoint location = [view convertPoint:point toView:nil];
                        NSView* target = [window.contentView hitTest:[view convertPoint:point toView:window.contentView]];
                        [window makeFirstResponder:target];
                        for (NSEventType type : {NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp}) {
                            NSEvent* event = [NSEvent mouseEventWithType:type location:location modifierFlags:0
                                timestamp:0 windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:1];
                            if (type == NSEventTypeLeftMouseDown) [target mouseDown:event]; else [target mouseUp:event];
                        }
                        pump();
                    };
                    const auto referenceKey = [&](NSView* view, NSString* chars, unsigned short code, NSEventModifierFlags flags) {
                        NSWindow* window = view.window;
                        NSEvent* event = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:flags
                            timestamp:0 windowNumber:window.windowNumber context:nil characters:chars
                            charactersIgnoringModifiers:chars isARepeat:NO keyCode:code];
                        if (code == 49 && flags == 0) {
                            // REAPER pre-translates before NSView key equivalents.
                            // -10 requests original macOS text-input processing.
                            auto* accel = context.keyboardAccelerators.empty() ? nullptr
                                : context.keyboardAccelerators.front();
                            struct { void* window; } message { (__bridge void*)window.firstResponder };
                            ok &= expect(accel && accel->isLocal
                                    && accel->translate(&message, accel) == -10,
                                "focused text did not request raw REAPER keyboard routing");
                            void* responder = (__bridge void*)window.firstResponder;
                            ok &= expect(context.hwndInfo && context.hwndInfo(responder, 0) == 1
                                    && context.hwndInfo(responder, 1) == 1
                                    && context.hwndInfo(reinterpret_cast<void*>(1), 0) == 0,
                                "REAPER text-field/global-shortcut classification failed");
                            [window.firstResponder keyDown:event];
                        } else [window.firstResponder keyDown:event];
                        pump();
                    };
                    const auto referenceType = [&](NSView* view, NSString* text) {
                        for (NSUInteger i=0;i<text.length;++i) referenceKey(view,[text substringWithRange:NSMakeRange(i,1)],
                            [text characterAtIndex:i] == ' ' ? 49 : 0,0);
                    };
                    const auto copySelection = [&](NSView* view) -> NSString* {
                        referenceKey(view,@"a",0,NSEventModifierFlagCommand);
                        referenceKey(view,@"c",8,NSEventModifierFlagCommand);
                        return [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] ?: @"";
                    };
                    if (console && help) {
                        clickButton(parent,nil,@"CONSOLE page",nil);pump();
                        int note=70;
                        for(double scale : {0.65,1.0,1.5,2.0}) {
                            uint32_t w=uint32_t(1320*scale),h=uint32_t(860*scale);
                            ok &= expect(gui->adjust_size(plugin,&w,&h)&&gui->set_size(plugin,w,h),"Console outer resizing");
                            [parent setFrameSize:NSMakeSize(w,h)];[hostWindow setContentSize:NSMakeSize(w,h)];pump();
                            referenceClick(console,NSMakePoint(200,49));
                            referenceKey(console,@"a",0,NSEventModifierFlagCommand);
                            referenceType(console,[NSString stringWithFormat:@"note 2 1 %d",note]);
                            referenceKey(console,@"\r",36,0);
                            StateBuffer saved; state->save(plugin,&saved.output);
                            const std::string json(saved.bytes.begin(),saved.bytes.end());
                            ok &= expect(json.find("\"note\": "+std::to_string(note++))!=std::string::npos,
                                "scaled Console command did not reach serialized CLAP state");
                        }
                        gui->set_size(plugin,1320,860);[parent setFrameSize:NSMakeSize(1320,860)];
                        [hostWindow setContentSize:NSMakeSize(1320,860)];pump();
                        referenceType(console,@"shared draft");
                        clickButton(parent,nil,@"TRACKER page",nil);pump();
                        click(NSMakePoint(250,91),1);
                        ok &= expect([copySelection(portableMain) isEqualToString:@"shared draft"],"Console draft did not reach main Live Code");
                        referenceType(portableMain,@"note 2 1 64");
                        clickButton(parent,nil,@"CONSOLE page",nil);pump();
                        referenceClick(console,NSMakePoint(200,49));
                        ok &= expect([copySelection(console) isEqualToString:@"note 2 1 64"],"main Live Code draft did not reach Console");
                        referenceKey(console,@"\r",36,0);
                        referenceKey(console,@"\uf700",126,0);
                        ok &= expect([copySelection(console) isEqualToString:@"note 2 1 64"],"Console history lost submitted command");
                        referenceKey(console,@"\uf701",125,0);
                        clickButton(parent,nil,@"Detach selected tool page",nil);pump();
                        NSWindow* detached = waitForOrderedDetachedWindow(@"s3g Tracker — Console",hostWindow);
                        ok &= expect(detached && console.window==detached && detached.level>hostWindow.level,
                            "Console detach/order failed");
                        if(detached) {
                            referenceClick(console,NSMakePoint(200,49));
                            referenceType(console,@"note 2 1 65");referenceKey(console,@"\r",36,0);
                            clickButton(parent,nil,@"TRACKER page",nil);pump();
                            ok &= expect(console.window==detached && detached.visible,"detached Console hidden by page switching");
                            [detached performClose:nil];pump();
                            ok &= expect(console.window==hostWindow,"Console close did not reattach frame");
                        }
                        clickButton(parent,nil,@"CONSOLE page",nil);pump();
                        referenceClick(console,NSMakePoint(100,90));
                        NSString* log=copySelection(console);
                        ok &= expect([log containsString:@"note 2 1 65"] && [log containsString:@"note 2 1 70"],
                            "Console copy/log history lost commands across detach");
                        if(captures)writeDocumentationPage(parent,@(captures),@"portable-console");
                        clickButton(parent,nil,@"HELP page",nil);pump();
                        referenceClick(help,NSMakePoint(100,52));
                        NSString* document=copySelection(help);
                        ok &= expect([document containsString:@"TRACKER GRID WORKFLOW"]
                            && [document containsString:@"GEOMETRY + TOOL WINDOWS"] && document.length>20000,
                            "portable Help copy missing command reference/workflow notes");
                        referenceKey(help,@"f",3,NSEventModifierFlagCommand);
                        referenceType(help,@"TRANSPORT + SONG");referenceKey(help,@"\r",36,0);
                        referenceKey(help,@"\033",53,0);referenceKey(help,@"c",8,NSEventModifierFlagCommand);
                        ok &= expect([[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString]
                            isEqualToString:@"TRANSPORT + SONG"],"Help Find/selection failed inside CLAP");
                        clickButton(parent,nil,@"Detach selected tool page",nil);pump();
                        NSWindow* helpDetached=waitForOrderedDetachedWindow(@"s3g Tracker — Help",hostWindow);
                        ok &= expect(helpDetached && help.window==helpDetached,"Help detach failed");
                        if(helpDetached) {
                            [helpDetached setContentSize:NSMakeSize(760,720)];pump();
                            referenceClick(help,NSMakePoint(100,52));
                            ok &= expect([copySelection(help) isEqualToString:document],"detached Help lost text");
                            referenceKey(help,@"\033",53,0);pump();
                            ok &= expect(help.window==hostWindow,"Help Escape did not reattach safely");
                        }
                        clickButton(parent,nil,@"HELP page",nil);pump();
                        if(captures)writeDocumentationPage(parent,@(captures),@"portable-help");
                        clickButton(parent,nil,@"TRACKER page",nil);pump();
                    }
                }
                factoryState.cursor=0;state->load(plugin,&factoryState.input);pump();
                const auto command=[&](NSString* text){
                    click(NSMakePoint(250,91),1);key(@"a",0,NSEventModifierFlagCommand);
                    for(NSUInteger i=0;i<text.length;++i)key([text substringWithRange:NSMakeRange(i,1)],0,0);
                    key(@"\r",36,0);
                };
                command(@"note 2 1 36");
                command(@"fx 1 1 2 CC74 64");
                command(@"interp 1 v1 step");
                ok &= expect(gui->hide(plugin),"portable Tracker hide failed");
                requestedWidth=1320;requestedHeight=860;
                if(ok)std::puts("Tracker portable main CLAP embedding / 65–200% input / page switching: ok");
            }
            if (shown && !portableMain) {
                // Use the accepted dimensions throughout, including later
                // mock-window creation and capture. The pilot now negotiates
                // a proportional size rather than a responsive canvas.
                requestedWidth = resizedWidth;
                requestedHeight = resizedHeight;
                [parent setFrameSize:NSMakeSize(resizedWidth, resizedHeight)];
                [parent layoutSubtreeIfNeeded];
                NSView* midiEventView = findAccessibleView(parent,
                    @"MIDI event statistics");
                NSTextField* midiEventStatus =
                    [midiEventView isKindOfClass:NSTextField.class]
                        ? static_cast<NSTextField*>(midiEventView) : nil;
                NSView* hostBpmView = findAccessibleView(parent,
                    @"Host tempo in beats per minute");
                NSTextField* hostBpm =
                    [hostBpmView isKindOfClass:NSTextField.class]
                        ? static_cast<NSTextField*>(hostBpmView) : nil;
                const NSRect hostBpmFrame = hostBpm
                    ? [hostBpm convertRect:hostBpm.bounds toView:parent]
                    : NSZeroRect;
                const NSRect midiEventFrame = midiEventStatus
                    ? [midiEventStatus convertRect:midiEventStatus.bounds
                        toView:parent] : NSZeroRect;
                ok &= expect(midiEventStatus
                        && [midiEventStatus.stringValue
                            containsString:@"SEND 0  DROP 0  LATE 0  CLK 0"]
                        && NSMaxX(midiEventFrame) < NSMinX(hostBpmFrame)
                        && findAccessibleView(parent,
                            @"Tracker route status") == nil,
                    "the obsolete Tracker route line remained or MIDI event statistics did not move beside HOST BPM");
                ok &= expect([hostBpm.stringValue hasPrefix:@"HOST BPM"]
                        && NSMaxX(hostBpmFrame)
                            >= NSWidth(parent.bounds) - [hostBpm
                                convertSize:NSMakeSize(20.0, 0.0) toView:parent].width,
                    "host BPM should use the shared passive top-right status position");
                context.masterTempo = 97.5;
                NSDate* tempoDeadline = [NSDate
                    dateWithTimeIntervalSinceNow:0.3];
                while (![hostBpm.stringValue
                            isEqualToString:@"HOST BPM  97.50"]
                    && [tempoDeadline timeIntervalSinceNow] > 0.0) {
                    [[NSRunLoop currentRunLoop] runUntilDate:
                        [NSDate dateWithTimeIntervalSinceNow:0.01]];
                }
                ok &= expect([hostBpm.stringValue
                            isEqualToString:@"HOST BPM  97.50"],
                    "stopped Tracker GUI did not follow a REAPER master-tempo change");
                context.masterTempo = 120.0;
                ok &= expect(clickButton(parent, nil,
                            @"Expand tracker sequencing columns", nil)
                        && clickButton(parent, nil,
                            @"Collapse tracker sequencing columns", nil),
                    "tracker lanes did not toggle between compact and expanded columns");
                StateBuffer nameViewState;
                const bool selectedNameView = clickButton(parent, nil,
                    @"Show notes as pitch names", nil);
                const bool savedNameView = selectedNameView && state
                    && state->save(plugin, &nameViewState.output);
                const std::string nameViewJson(nameViewState.bytes.begin(),
                    nameViewState.bytes.end());
                const bool returnedToMidi = clickButton(parent, nil,
                    @"Show notes as MIDI values", nil);
                const bool restoredNameView = savedNameView
                    && returnedToMidi
                    && state->load(plugin, &nameViewState.input)
                    && findButton(parent, nil,
                        @"Show notes as MIDI values", nil) != nil;
                const bool restoredMidiDefault = restoredNameView
                    && clickButton(parent, nil,
                        @"Show notes as MIDI values", nil);
                ok &= expect(savedNameView
                        && nameViewJson.find(
                            "\"showMidiNoteValues\": false")
                            != std::string::npos
                        && restoredNameView && restoredMidiDefault,
                    "project state did not restore the saved Tracker NOTE view preference");
                ok &= expect(clickButton(parent, nil,
                            @"Sync all tracker lanes and columns to row 1", nil),
                    "tracker did not expose global row-one synchronization");
                ok &= expect(clickButton(parent, nil, @"GEOMETRY page", nil),
                    "Geometry page could not be selected for view-mode audit");
                [parent layoutSubtreeIfNeeded];
                NSView* geometryModeView = findAccessibleView(parent,
                    @"Geometry view mode");
                NSPopUpButton* geometryMode =
                    [geometryModeView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(geometryModeView) : nil;
                bool geometryModesAvailable = geometryMode.numberOfItems
                        == 8u
                    && geometryMode.indexOfSelectedItem == 0
                    && [[geometryMode itemAtIndex:0].title
                        isEqualToString:@"RING FIELD"]
                    && [[geometryMode itemAtIndex:1].title
                        isEqualToString:@"ACTIVE PULSES"]
                    && [[geometryMode itemAtIndex:2].title
                        isEqualToString:@"ALL STEPS UNDERLAY"]
                    && [[geometryMode itemAtIndex:3].title
                        isEqualToString:@"PHASE SPOKES"]
                    && [[geometryMode itemAtIndex:4].title
                        isEqualToString:@"LANE FOCUS"]
                    && [[geometryMode itemAtIndex:5].title
                        isEqualToString:@"COMPOSITE RING"]
                    && [[geometryMode itemAtIndex:6].title
                        isEqualToString:@"BURST EDITOR"]
                    && [geometryMode itemAtIndex:6].hidden
                    && [[geometryMode itemAtIndex:7].title
                        isEqualToString:@"PITCH MAP"];
                if (std::getenv("S3G_TRACKER_EXPECT_PORTABLE_GEOMETRY")) {
                    NSView* host = findAccessibleView(parent, @"Rhythm geometry");
                    geometryModesAvailable = [host respondsToSelector:@selector(geometryMenuItems)]
                        && [[host geometryMenuItems] isEqualToArray:@[ @"RING FIELD", @"ACTIVE PULSES",
                            @"ALL STEPS UNDERLAY", @"PHASE SPOKES", @"LANE FOCUS", @"COMPOSITE RING", @"PITCH MAP" ]];
                    if(geometryModesAvailable) {
                        for(NSInteger mode=0;mode<8;++mode) if(mode!=6) [host selectGeometryMode:mode];
                        [host selectGeometryMode:3];
                    }
                }
                else if (geometryModesAvailable) {
                    for (NSInteger mode = 1; mode < 8; ++mode) {
                        if (mode == 6) continue;
                        [geometryMode selectItemAtIndex:mode];
                        [geometryMode sendAction:geometryMode.action
                            to:geometryMode.target];
                    }
                    // Leave the animated phase view active for optional
                    // documentation capture later in this smoke run.
                    [geometryMode selectItemAtIndex:3u];
                    [geometryMode sendAction:geometryMode.action
                        to:geometryMode.target];
                }
                const bool burstPrepared = submitCommand(parent,
                        @"burst new B01 STOPPED PREVIEW")
                    && submitCommand(parent,
                        @"burst B01 notes 36 38 41 46")
                    && geometryModesAvailable
                    && clickButton(parent, nil, @"BURSTS page", nil);
                [parent layoutSubtreeIfNeeded];
                NSView* geometryView = findAccessibleView(parent,
                    @"Burst editor");
                bool previewAudioOk = false;
                if (burstPrepared && geometryView
                    && plugin->activate(plugin, 48000.0, 64u, 8192u)
                    && plugin->start_processing(plugin)) {
                    const NSRect channelMenu = [geometryView
                        burstPreviewChannelMenuBoxRect];
                    const bool channelMenuOpened = [geometryView
                        handleToolboxClickAtPoint:NSMakePoint(
                            NSMidX(channelMenu), NSMidY(channelMenu))];
                    // The menu is user-facing MIDI 01..16, so index 9 selects
                    // MIDI channel 10 and should emit zero-based status 9.
                    [geometryView applyGeometryMenuSelection:9u];
                    const NSRect previewButton = [geometryView
                        burstPreviewHeaderButtonRect];
                    const bool clicked = [geometryView
                        handleToolboxClickAtPoint:NSMakePoint(
                            NSMidX(previewButton), NSMidY(previewButton))];
                    clap_event_transport_t stopped {};
                    stopped.header.size = sizeof(stopped);
                    stopped.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    stopped.header.type = CLAP_EVENT_TRANSPORT;
                    stopped.flags = CLAP_TRANSPORT_HAS_TEMPO;
                    stopped.tempo = 120.0;
                    OutputEvents previewOutput;
                    clap_process_t previewProcess {};
                    previewProcess.frames_count = 6000u;
                    previewProcess.transport = &stopped;
                    previewProcess.out_events = &previewOutput.interface;
                    const bool processed = plugin->process(plugin,
                        &previewProcess) == CLAP_PROCESS_CONTINUE;
                    std::array<uint32_t, 4u> onsetTimes {};
                    std::array<uint8_t, 4u> channels {};
                    std::size_t onsetCount = 0u;
                    for (uint32_t index = 0u;
                         index < previewOutput.count; ++index) {
                        const auto& event = previewOutput.events[index];
                        if ((event.data[0] & 0xf0u) != 0x90u
                            || event.data[2] == 0u
                            || onsetCount >= onsetTimes.size()) continue;
                        onsetTimes[onsetCount] = event.header.time;
                        channels[onsetCount++] = static_cast<uint8_t>(
                            event.data[0] & 0x0fu);
                    }
                    previewAudioOk = channelMenuOpened && clicked && processed
                        && onsetCount == 4u
                        && onsetTimes == std::array<uint32_t, 4u> {{
                            0u, 1500u, 3000u, 4500u,
                        }}
                        && channels == std::array<uint8_t, 4u> {{
                            9u, 9u, 9u, 9u,
                        }};
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);
                }
                ok &= expect(previewAudioOk,
                    "stopped Burst Preview did not emit substeps at project-BPM "
                    "row positions on the selected MIDI channel");
                const bool reshapeSelected = clickButton(
                    parent, nil, @"RESHAPE page", nil);
                [parent layoutSubtreeIfNeeded];
                NSView* reshapeProfile = findAccessibleView(parent,
                    @"Pattern reshape profile");
                NSView* reshapeCycle = findAccessibleView(parent,
                    @"Reshape analysis cycle");
                NSView* reshapeDepth = findAccessibleView(parent,
                    @"Reshape timing depth");
                NSView* portableReshape = findAccessibleView(parent, @"Tracker portable Reshape page");
                const bool reshapeControls = std::getenv("S3G_TRACKER_EXPECT_PORTABLE_AUTHORING")
                    ? portableReshape && [portableReshape respondsToSelector:@selector(authoringControlRect:)]
                    : reshapeProfile && reshapeCycle && reshapeDepth;
                ok &= expect(geometryModesAvailable && reshapeSelected
                        && reshapeControls
                        && clickButton(parent, nil, @"TRACKER page", nil),
                    "Geometry or Reshape workspace controls are incomplete");
                if(std::getenv("S3G_TRACKER_EXPECT_PORTABLE_AUTHORING")) {
                    auto pump = [&] { [parent layoutSubtreeIfNeeded]; [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]]; [parent displayIfNeeded]; };
                    for(NSString* tab in @[@"PHRASES",@"ASSEMBLE",@"RESHAPE"]) {
                        NSString* title=[tab isEqualToString:@"PHRASES"]?@"Phrases":[tab isEqualToString:@"ASSEMBLE"]?@"Assemble":@"Reshape";
                        NSString* tabLabel=[tab stringByAppendingString:@" page"];
                        ok &= expect(clickButton(parent,nil,tabLabel,nil),"authoring page switch");pump();
                        NSView* page=findAccessibleView(parent,[NSString stringWithFormat:@"Tracker portable %@ page",title]);
                        ok &= expect(page&&[page respondsToSelector:@selector(authoringControlRect:)],"portable authoring host missing");
                        if(!page) continue;
                        auto pointer=[&](NSRect r){
                            [page layoutSubtreeIfNeeded];[page display];NSWindow* window=page.window;
                            NSPoint point=NSMakePoint(NSMidX(r),NSMidY(r));NSPoint location=[page convertPoint:point toView:nil];
                            NSView* target=[window.contentView hitTest:[page convertPoint:point toView:window.contentView]];
                            [window makeFirstResponder:target];
                            for(NSEventType type:{NSEventTypeLeftMouseDown,NSEventTypeLeftMouseUp}) {
                                NSEvent* event=[NSEvent mouseEventWithType:type location:location modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:1 pressure:type==NSEventTypeLeftMouseDown?1:0];
                                if(type==NSEventTypeLeftMouseDown) [target mouseDown:event];else [target mouseUp:event];
                            }pump();
                        };
                        auto control=[&](NSString* name){[page display];NSRect r=[page authoringControlRect:name];ok &= expect(!NSIsEmptyRect(r),"portable authoring control missing");if(!NSIsEmptyRect(r)) pointer(r);};
                        auto editPhraseName=[&](NSString* value){
                            control(@"name");
                            ok &= expect([page s3gTrackerHasFocusedTextInput],"Phrase name lacks scoped REAPER text ownership");
                            for(NSUInteger i=0;i<=value.length;++i){NSString* chars=i==value.length?@"\r":[value substringWithRange:NSMakeRange(i,1)];NSWindow* window=page.window;
                                NSEvent* event=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil characters:chars charactersIgnoringModifiers:chars isARepeat:NO keyCode:i==value.length?36:[chars isEqualToString:@" "]?49:0];
                                if(![page performKeyEquivalent:event]) [window.firstResponder keyDown:event];pump();}
                            control(@"SAVE");
                            StateBuffer saved;std::string json;NSDate* deadline=[NSDate dateWithTimeIntervalSinceNow:.5];
                            do {saved.bytes.clear();state->save(plugin,&saved.output);json.assign(saved.bytes.begin(),saved.bytes.end());if(json.find(value.UTF8String)!=std::string::npos) break;pump();} while([deadline timeIntervalSinceNow]>0);
                            ok &= expect(json.find(value.UTF8String)!=std::string::npos,"portable Phrase edit/Space did not reach CLAP state");
                        };
                        if (![tab isEqualToString:@"RESHAPE"]) {
                            // Exercise the actual LISTEN callback and CLAP MIDI
                            // output without pumping the GUI during rendering.
                            StateBuffer beforePreview;
                            state->save(plugin, &beforePreview.output);
                            NSData* bytes = [NSData dataWithBytes:beforePreview.bytes.data()
                                length:beforePreview.bytes.size()];
                            NSMutableDictionary* fixture = [NSJSONSerialization
                                JSONObjectWithData:bytes options:NSJSONReadingMutableContainers error:nil];
                            NSMutableDictionary* bank = fixture[@"phraseBanks"][0];
                            NSMutableDictionary* phrase = bank[@"slots"][0];
                            phrase[@"length"] = @16;
                            phrase[@"previewMidiChannel"] = @4;
                            phrase[@"notes"] = @[@{@"state":@"rest"}, @{@"state":@"rest"},
                                @{@"state":@"note", @"note":@60}];
                            phrase[@"velocities"] = @[];
                            phrase[@"gates"] = @[];
                            fixture[@"playback"][@"ticksPerBeat"] = @4;
                            fixture[@"workspace"][@"activePhraseBank"] = bank[@"id"];
                            NSMutableDictionary* assembly = fixture[@"workspace"][@"assembly"];
                            assembly[@"loopPreview"] = @YES;
                            assembly[@"previewMidiChannel"] = @4;
                            assembly[@"blocks"] = @[@{@"bank":bank[@"id"], @"phrase":@0, @"repeats":@2}];
                            NSData* encoded = [NSJSONSerialization dataWithJSONObject:fixture options:0 error:nil];
                            StateBuffer auditionState;
                            const auto* raw = static_cast<const uint8_t*>(encoded.bytes);
                            auditionState.bytes.assign(raw, raw + encoded.length);
                            ok &= expect(state->load(plugin, &auditionState.input), "audition timing fixture load");
                            pump();
                            control(@"phrase");
                            pointer([page authoringPopupItemRect:0]);
                            const bool phrases = [tab isEqualToString:@"PHRASES"];
                            if (phrases) control(@"loop");
                            for (double bpm : {120., 123., 130.}) {
                                context.masterTempo = bpm;
                                ok &= expect(plugin->activate(plugin, 48000, 64, 8192)
                                    && plugin->start_processing(plugin), "audition audio activation");
                                control(@"listen");
                                clap_event_transport_t stopped {};
                                stopped.flags = CLAP_TRANSPORT_HAS_TEMPO;
                                stopped.tempo = bpm;
                                const double rowSamples = 48000. * 60. / (bpm * 4.);
                                std::vector<uint64_t> hits;
                                uint64_t frame = 0;
                                bool ordered = true, routed = true;
                                int held = 0;
                                const uint64_t total = uint64_t(std::ceil(rowSamples * 66.1));
                                unsigned blockIndex = 0;
                                while (frame < total) {
                                    const uint32_t sizes[] = {64, 127, 512, 8192};
                                    OutputEvents output;
                                    clap_process_t process {};
                                    process.frames_count = uint32_t(std::min<uint64_t>(
                                        sizes[blockIndex++ % 4], total-frame));
                                    process.transport = &stopped;
                                    process.out_events = &output.interface;
                                    plugin->process(plugin, &process);
                                    for (uint32_t i = 0; i < output.count; ++i) {
                                        const auto& e = output.events[i];
                                        ordered &= e.header.time < process.frames_count &&
                                            (!i || e.header.time >= output.events[i-1].header.time);
                                        const auto kind = e.data[0] & 0xf0;
                                        if (kind == 0x90 && e.data[2]) {
                                            hits.push_back(frame + e.header.time);
                                            routed &= e.data[0] == 0x93 && e.data[1] == 60;
                                            ++held;
                                        } else if (kind == 0x80 || (kind == 0x90 && !e.data[2])) --held;
                                        ordered &= held >= 0 && held <= 1;
                                    }
                                    frame += process.frames_count;
                                }
                                bool accurate = hits.size() == 5;
                                for (std::size_t i = 0; i < hits.size(); ++i) {
                                    const double error = double(hits[i]) - (2 + 16*i)*rowSamples;
                                    accurate &= error > -1e-5 && error < 1.00001;
                                }
                                ok &= expect(accurate && ordered && routed,
                                    "LISTEN loop drift, rest loss, routing or note-off ordering regression");
                                // Stop before the next pass. No further notes
                                // may be emitted even without another UI tick.
                                control(@"listen");
                                OutputEvents stoppedOutput;
                                clap_process_t stopProcess {};
                                stopProcess.frames_count = 8192;
                                stopProcess.transport = &stopped;
                                stopProcess.out_events = &stoppedOutput.interface;
                                bool silent = true;
                                for (int i = 0; i < 30; ++i) {
                                    stoppedOutput.count = 0;
                                    plugin->process(plugin, &stopProcess);
                                    for (uint32_t j = 0; j < stoppedOutput.count; ++j) {
                                        const auto& e = stoppedOutput.events[j];
                                        silent &= (e.data[0] & 0xf0) != 0x90 || !e.data[2];
                                        if ((e.data[0] & 0xf0) == 0x80) --held;
                                    }
                                }
                                ok &= expect(silent && held == 0, "LISTEN stop left repeated or stuck notes");
                                plugin->stop_processing(plugin);
                                plugin->deactivate(plugin);
                            }
                            if (phrases) control(@"loop");
                            context.masterTempo = 120;
                            beforePreview.cursor = 0;
                            ok &= expect(state->load(plugin, &beforePreview.input), "restore after audition timing test");
                            pump();
                        }
                        for(double scale:{.65,1.,1.5,2.,1.}) {
                            uint32_t w=uint32_t(1320*scale),h=uint32_t(860*scale);ok &= expect(gui->adjust_size(plugin,&w,&h)&&gui->set_size(plugin,w,h),"authoring resize rejected");[parent setFrameSize:NSMakeSize(w,h)];[hostWindow setContentSize:NSMakeSize(w,h)];pump();
                            NSRect physical=[page convertRect:page.bounds toView:parent];ok &= expect(std::abs(NSWidth(physical)-NSWidth(page.bounds)*scale)<1.1,"authoring proportional scale mismatch");
                            if([tab isEqualToString:@"PHRASES"]) editPhraseName([NSString stringWithFormat:@"PORTABLE PHRASE %d",int(scale*100)]);
                            else {control([tab isEqualToString:@"ASSEMBLE"]?@"repeat":@"cycle");NSRect item=[page authoringPopupItemRect:2];ok &= expect(!NSIsEmptyRect(item),"authoring canvas menu did not open");if(!NSIsEmptyRect(item)) pointer(item);}
                        }
                        ok &= expect(clickButton(parent,nil,@"Detach selected tool page",nil),"authoring detach");
                        NSString* windowTitle=[tab isEqualToString:@"PHRASES"]?@"s3g Tracker — MIDI Phrases":[tab isEqualToString:@"ASSEMBLE"]?@"s3g Tracker — Phrase Assembly":@"s3g Tracker — Pattern Reshape";
                        NSWindow* detached=waitForOrderedDetachedWindow(windowTitle,hostWindow);
                        ok &= expect(detached&&page.window==detached&&detached.parentWindow==nil&&detached.level>hostWindow.level,"authoring detached ordering/ownership");
                        if(detached){[detached setContentSize:NSMakeSize(480,360)];pump();if([tab isEqualToString:@"PHRASES"]) editPhraseName(@"DETACHED PHRASE OK");
                            clickButton(parent,nil,@"TRACKER page",nil);pump();ok &= expect(page.window==detached&&detached.visible,"detached authoring page hidden on main navigation");[detached performClose:nil];pump();ok &= expect(page.window==hostWindow,"authoring reattachment");}
                    }
                    clickButton(parent,nil,@"TRACKER page",nil);[hostWindow makeKeyAndOrderFront:nil];pump();
                }
                NSView* pageWorkspace = findAccessibleView(parent,
                    @"s3g Tracker REAPER page workspace");
                [hostWindow makeFirstResponder:nil];
                NSEvent* nextPanel = [NSEvent keyEventWithType:NSEventTypeKeyDown
                    location:NSZeroPoint modifierFlags:NSEventModifierFlagShift
                    timestamp:NSProcessInfo.processInfo.systemUptime
                    windowNumber:hostWindow.windowNumber context:nil
                    characters:@">" charactersIgnoringModifiers:@"."
                    isARepeat:NO keyCode:47u];
                NSEvent* previousPanel = [NSEvent keyEventWithType:NSEventTypeKeyDown
                    location:NSZeroPoint modifierFlags:NSEventModifierFlagShift
                    timestamp:NSProcessInfo.processInfo.systemUptime
                    windowNumber:hostWindow.windowNumber context:nil
                    characters:@"<" charactersIgnoringModifiers:@","
                    isARepeat:NO keyCode:43u];
                NSButton* songPageButton = findButton(
                    parent, nil, @"SONG page", nil);
                NSButton* trackerPageKeyButton = findButton(
                    parent, nil, @"TRACKER page", nil);
                const bool nextPanelHandled = [pageWorkspace
                    performKeyEquivalent:nextPanel];
                const bool songSelectedByKey = songPageButton.state
                    == NSControlStateValueOn;
                const bool previousPanelHandled = [pageWorkspace
                    performKeyEquivalent:previousPanel];
                ok &= expect(nextPanelHandled && songSelectedByKey
                        && previousPanelHandled
                        && trackerPageKeyButton.state
                            == NSControlStateValueOn,
                    "less-than and greater-than did not navigate adjacent Tracker panels");
                ok &= expect(submitCommand(parent, @"play")
                        && submitCommand(parent, @"stop")
                        && context.playRequests == 1u
                        && context.stopRequests == 1u,
                    "REAPER bridge did not receive console transport requests");
                const uint32_t dirtyBeforeHistory = context.dirtyMarks;
                NSView* undoView = findAccessibleView(parent,
                    @"Undo last Tracker edit");
                NSView* redoView = findAccessibleView(parent,
                    @"Redo last Tracker edit");
                NSButton* undoButton = [undoView isKindOfClass:NSButton.class]
                    ? static_cast<NSButton*>(undoView) : nil;
                NSButton* redoButton = [redoView isKindOfClass:NSButton.class]
                    ? static_cast<NSButton*>(redoView) : nil;
                const bool editedForOverlap = submitCommand(parent,
                    @"note 2 1 36");
                const bool undoAvailable = undoButton.enabled;
                if (undoAvailable) [undoButton performClick:nil];
                const bool redoAvailable = redoButton.enabled;
                if (redoAvailable) [redoButton performClick:nil];
                ok &= expect(editedForOverlap && undoAvailable
                        && redoAvailable && undoButton.enabled
                        && context.dirtyMarks >= dirtyBeforeHistory + 3u,
                    "Tracker project undo/redo did not restore a persistent edit");
                NSView* stepModeView = findAccessibleView(parent,
                    @"MIDI recording mode");
                NSPopUpButton* stepMode =
                    [stepModeView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(stepModeView) : nil;
                const bool stepModeAvailable = stepMode.numberOfItems == 4u
                    && [stepMode.itemArray[0u].title isEqualToString:@"REC OFF"]
                    && [stepMode.itemArray[1u].title isEqualToString:@"REC STEP"]
                    && [stepMode.itemArray[2u].title isEqualToString:@"REC Q"]
                    && [stepMode.itemArray[3u].title isEqualToString:@"REC MT"];
                if (stepModeAvailable) {
                    [stepMode selectItemAtIndex:1u];
                    [stepMode sendAction:stepMode.action to:stepMode.target];
                }
                const bool stepCursorSelected = submitCommand(parent,
                    @"select 1 5");
                bool stepMonitorPassed = false;
                context.playState = 1;
                bool stepRecordProcessing = plugin->activate(
                        plugin, 48000.0, 16u, 32768u)
                    && plugin->start_processing(plugin);
                if (stepRecordProcessing) {
                    clap_event_transport_t stepTransport {};
                    stepTransport.header.size = sizeof(stepTransport);
                    stepTransport.header.space_id =
                        CLAP_CORE_EVENT_SPACE_ID;
                    stepTransport.header.type = CLAP_EVENT_TRANSPORT;
                    stepTransport.flags = CLAP_TRANSPORT_HAS_TEMPO
                        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
                        | CLAP_TRANSPORT_IS_PLAYING;
                    stepTransport.tempo = 120.0;
                    InputEvents stepInput;
                    stepInput.addMidi(600u, 0x96u, 67u, 101u);
                    stepInput.addMidi(700u, 0x86u, 67u, 0u);
                    OutputEvents stepOutput;
                    clap_process_t stepProcess {};
                    stepProcess.frames_count = 2048u;
                    stepProcess.transport = &stepTransport;
                    stepProcess.in_events = &stepInput.interface;
                    stepProcess.out_events = &stepOutput.interface;
                    stepRecordProcessing = plugin->process(plugin,
                        &stepProcess) == CLAP_PROCESS_CONTINUE;
                    bool monitoredOn = false;
                    bool monitoredOff = false;
                    for (uint32_t index = 0u;
                         index < stepOutput.count; ++index) {
                        const auto& event = stepOutput.events[index];
                        monitoredOn |= event.header.time == 600u
                            && event.data[0] == 0x90u
                            && event.data[1] == 67u
                            && event.data[2] == 101u;
                        monitoredOff |= event.header.time == 700u
                            && event.data[0] == 0x80u
                            && event.data[1] == 67u;
                    }
                    stepMonitorPassed = monitoredOn && monitoredOff;
                    [[NSRunLoop currentRunLoop] runUntilDate:
                        [NSDate dateWithTimeIntervalSinceNow:0.08]];
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);
                    context.playState = 0;
                }
                if (stepModeAvailable) {
                    [stepMode selectItemAtIndex:3u];
                    [stepMode sendAction:stepMode.action to:stepMode.target];
                }
                bool liveMonitorPassed = false;
                context.playState = 1;
                bool liveRecordProcessing = plugin->activate(
                        plugin, 48000.0, 16u, 32768u)
                    && plugin->start_processing(plugin);
                if (liveRecordProcessing) {
                    clap_event_transport_t liveTransport {};
                    liveTransport.header.size = sizeof(liveTransport);
                    liveTransport.header.space_id =
                        CLAP_CORE_EVENT_SPACE_ID;
                    liveTransport.header.type = CLAP_EVENT_TRANSPORT;
                    liveTransport.flags = CLAP_TRANSPORT_HAS_TEMPO
                        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
                        | CLAP_TRANSPORT_IS_PLAYING;
                    liveTransport.tempo = 120.0;
                    InputEvents liveInput;
                    liveInput.addMidi(6600u, 0x96u, 69u, 111u);
                    OutputEvents liveOutput;
                    clap_process_t liveProcess {};
                    liveProcess.frames_count = 8000u;
                    liveProcess.transport = &liveTransport;
                    liveProcess.in_events = &liveInput.interface;
                    liveProcess.out_events = &liveOutput.interface;
                    liveRecordProcessing = plugin->process(plugin,
                        &liveProcess) == CLAP_PROCESS_CONTINUE;
                    bool monitoredOn = false;
                    for (uint32_t index = 0u;
                         index < liveOutput.count; ++index) {
                        const auto& event = liveOutput.events[index];
                        monitoredOn |= event.header.time == 6600u
                            && event.data[0] == 0x90u
                            && event.data[1] == 69u
                            && event.data[2] == 111u;
                    }
                    if (stepModeAvailable) {
                        [stepMode selectItemAtIndex:0u];
                        [stepMode sendAction:stepMode.action
                            to:stepMode.target];
                    }
                    OutputEvents disarmOutput;
                    InputEvents disarmedInput;
                    disarmedInput.addMidi(64u, 0x96u, 71u, 99u);
                    liveProcess.frames_count = 128u;
                    liveProcess.in_events = &disarmedInput.interface;
                    liveProcess.out_events = &disarmOutput.interface;
                    liveRecordProcessing &= plugin->process(plugin,
                        &liveProcess) == CLAP_PROCESS_CONTINUE;
                    bool releasedOnDisarm = false;
                    bool disarmedNoteLeaked = false;
                    for (uint32_t index = 0u;
                         index < disarmOutput.count; ++index) {
                        const auto& event = disarmOutput.events[index];
                        releasedOnDisarm |= event.header.time == 0u
                            && event.data[0] == 0x80u
                            && event.data[1] == 69u;
                        disarmedNoteLeaked |= event.data[1] == 71u
                            && (event.data[0] & 0xf0u) == 0x90u;
                    }
                    liveMonitorPassed = monitoredOn && releasedOnDisarm
                        && !disarmedNoteLeaked;
                    [[NSRunLoop currentRunLoop] runUntilDate:
                        [NSDate dateWithTimeIntervalSinceNow:0.08]];
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);
                    context.playState = 0;
                }
                NSView* consoleMessages = findAccessibleView(parent,
                    @"Console printed messages");
                const bool midiRecorded =
                    [consoleMessages isKindOfClass:NSTextView.class]
                    && [static_cast<NSTextView*>(consoleMessages).string
                        containsString:@"STEP REC CH7 note 67 → lane 1, row 5"]
                    && [static_cast<NSTextView*>(consoleMessages).string
                        containsString:@"LIVE MT REC CH7 note 69 → lane 1, row 2, MT 75%"];
                ok &= expect(stepModeAvailable && stepCursorSelected
                        && stepRecordProcessing && liveRecordProcessing
                        && stepMonitorPassed && liveMonitorPassed
                        && midiRecorded,
                    "armed STEP/LIVE recording did not monitor and record MIDI notes");

                ok &= expect(clickButton(parent, nil, @"SONG page", nil),
                    "Song page could not be selected for file-menu audit");
                [parent layoutSubtreeIfNeeded];
                [parent displayIfNeeded];
                NSView* projectFileView = findAccessibleView(parent,
                    @"Song and pattern project file");
                NSView* songPatternView = findAccessibleView(parent,
                    @"Song row 1 pattern");
                NSPopUpButton* projectFileMenu =
                    [projectFileView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(projectFileView) : nil;
                NSPopUpButton* songPatternMenu =
                    [songPatternView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(songPatternView) : nil;
                ok &= expect(projectFileMenu.numberOfItems == 3u
                        && [[projectFileMenu itemAtIndex:1].title
                            isEqualToString:@"SAVE SONG + PATTERNS…"]
                        && [[projectFileMenu itemAtIndex:2].title
                            isEqualToString:@"LOAD SONG + PATTERNS…"]
                        && songPatternMenu.numberOfItems > 0u
                        && [songPatternMenu.itemArray.firstObject.title
                            containsString:@"A01"]
                        && [[[songPatternMenu itemAtIndex:0] representedObject]
                            isEqualToString:@"A01"]
                        && clickButton(parent, nil, @"TRACKER page", nil),
                    "Song file menu or named stable-ID pattern selector is incomplete");
                hostWindow = [[NSWindow alloc] initWithContentRect:
                    NSMakeRect(0.0, 0.0, requestedWidth, requestedHeight)
                    styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered defer:NO];
                hostWindow.title = @"s3g Tracker smoke host";
                hostWindow.releasedWhenClosed = NO;
                hostWindow.contentView = parent;
                [hostWindow setContentSize:NSMakeSize(
                    requestedWidth, requestedHeight)];
                [parent setFrame:NSMakeRect(
                    0.0, 0.0, requestedWidth, requestedHeight)];
                (void)gui->set_size(
                    plugin, requestedWidth, requestedHeight);
                [parent layoutSubtreeIfNeeded];
                [hostWindow orderFront:nil];
                [hostWindow setContentSize:NSMakeSize(
                    requestedWidth, requestedHeight)];
                [parent setFrame:NSMakeRect(
                    0.0, 0.0, requestedWidth, requestedHeight)];
                (void)gui->set_size(
                    plugin, requestedWidth, requestedHeight);
                [parent layoutSubtreeIfNeeded];
                [parent displayIfNeeded];
                NSView* grid = findAccessibleView(parent, @"Editable tracker lanes");
                // Earlier MIDI tests leave a pending GUI transport update.
                // Freeze the mock host before paired captures so a timer tick
                // cannot add/remove the playback row halfway through a matrix.
                const int playStateBeforeZoom = context.playState;
                context.playState = 0;
                [[NSRunLoop currentRunLoop] runUntilDate:
                    [NSDate dateWithTimeIntervalSinceNow:0.10]];
                NSScrollView* gridScroll = grid.enclosingScrollView;
                NSView* gutter = findAccessibleView(parent, @"Frozen tracker row numbers");
                bool zoomControls = grid && gridScroll && gutter
                    && clickButton(parent, nil, @"100 percent Tracker zoom", nil);
                const char* pilotCapturePath = std::getenv("S3G_TRACKER_PILOT_CAPTURE_DIR");
                NSString* pilotCaptureDir = pilotCapturePath && pilotCapturePath[0]
                    ? [NSString stringWithUTF8String:pilotCapturePath] : nil;
                if (pilotCaptureDir) ok &= expect(writeDocumentationPage(parent,
                    pilotCaptureDir, @"compact-100"), "compact grid capture failed");
                zoomControls &= clickButton(parent, nil,
                    @"Expand tracker sequencing columns", nil);
                if (pilotCaptureDir) ok &= expect(writeDocumentationPage(parent,
                    pilotCaptureDir, @"expanded-100"), "expanded grid capture failed");
                for (int step = 0; step < 30 && gridScroll.magnification > 0.5501; ++step)
                    zoomControls &= clickButton(parent, nil, @"Zoom Tracker out", nil);
                zoomControls &= std::abs(gridScroll.magnification - 0.55) < 0.0001;
                if (pilotCaptureDir) ok &= expect(writeDocumentationPage(parent,
                    pilotCaptureDir, @"expanded-55"), "minimum zoom capture failed");
                for (int step = 0; step < 30 && gridScroll.magnification < 1.7999; ++step)
                    zoomControls &= clickButton(parent, nil, @"Zoom Tracker in", nil);
                zoomControls &= std::abs(gridScroll.magnification - 1.80) < 0.0001;
                if (pilotCaptureDir) ok &= expect(writeDocumentationPage(parent,
                    pilotCaptureDir, @"expanded-180"), "maximum zoom capture failed");
                const double gutterX = [gutter convertRect:gutter.bounds toView:parent].origin.x;
                [grid scrollPoint:NSMakePoint(160.0, 75.0)];
                [gridScroll reflectScrolledClipView:gridScroll.contentView];
                zoomControls &= std::abs([gutter convertRect:gutter.bounds
                    toView:parent].origin.x - gutterX) < 0.0001;
                if (pilotCaptureDir) ok &= expect(writeDocumentationPage(parent,
                    pilotCaptureDir, @"scrolled-180"), "scrolled grid capture failed");
                zoomControls &= clickButton(parent, nil, @"100 percent Tracker zoom", nil)
                    && clickButton(parent, nil, @"Collapse tracker sequencing columns", nil);
                [grid scrollPoint:NSZeroPoint];
                [gridScroll reflectScrolledClipView:gridScroll.contentView];
                ok &= expect(zoomControls && std::abs(gridScroll.magnification - 1.0) < 0.0001,
                    "Tracker grid zoom limits/reset or frozen row gutter changed");
                ok &= checkWholeInterfaceScaling(gui, plugin, parent, hostWindow);
                context.playState = playStateBeforeZoom;
                [[NSRunLoop currentRunLoop] runUntilDate:
                    [NSDate dateWithTimeIntervalSinceNow:0.10]];
                NSButton* trackerPageButton = findButton(
                    parent, nil, @"TRACKER page", nil);
                ok &= expect(trackerPageButton
                        && ![[trackerPageButton valueForKey:
                            @"s3gUsesSuiteStyle"] boolValue]
                        && trackerPageButton.state
                            == NSControlStateValueOn,
                    "active page navigation should retain its cyan Tracker highlight");
                NSView* activePatternView = findAccessibleView(parent,
                    @"Active pattern");
                NSView* midiRecordView = findAccessibleView(parent,
                    @"MIDI recording mode");
                NSPopUpButton* activePatternMenu =
                    [activePatternView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(activePatternView) : nil;
                NSPopUpButton* midiRecordMenu =
                    [midiRecordView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(midiRecordView) : nil;
                [activePatternMenu selectItemAtIndex:0];
                const bool patternDispatched = [activePatternMenu sendAction:
                    activePatternMenu.action to:activePatternMenu.target];
                [midiRecordMenu selectItemAtIndex:1];
                const bool midiDispatched = [midiRecordMenu sendAction:
                    midiRecordMenu.action to:midiRecordMenu.target];
                [midiRecordMenu selectItemAtIndex:0];
                [midiRecordMenu sendAction:midiRecordMenu.action
                    to:midiRecordMenu.target];
                ok &= expect(activePatternMenu.enabled
                        && midiRecordMenu.enabled && patternDispatched
                        && midiDispatched,
                    "embedded Pattern and MIDI REC menus did not dispatch through the plug-in coordinator");
                const bool songMenuPage = clickButton(
                    parent, nil, @"SONG page", nil);
                [parent layoutSubtreeIfNeeded];
                NSView* songPatternClickView = findAccessibleView(parent,
                    @"Song row 1 pattern");
                NSPopUpButton* songPatternClickMenu =
                    [songPatternClickView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(songPatternClickView)
                        : nil;
                ok &= expect(songMenuPage
                        && [songPatternClickMenu sendAction:
                            songPatternClickMenu.action
                            to:songPatternClickMenu.target]
                        && clickButton(parent, nil, @"TRACKER page", nil),
                    "embedded Song row menus did not dispatch through the plug-in coordinator");

                // A Song edit made while REAPER is running must be handed to
                // the scheduler at the next Song-row boundary. Editing row 2
                // from one to three repetitions makes that behavior directly
                // observable: after its first pass row 2 must still be active.
                const bool songLiveEditPage = clickButton(
                    parent, nil, @"SONG page", nil);
                const bool songLiveMode = songLiveEditPage
                    && clickButton(parent, @"SONG: OFF", nil, nil);
                context.playState = 1;
                bool songLiveProcessing = songLiveMode
                    && plugin->activate(plugin, 48000.0, 16u, 32768u)
                    && plugin->start_processing(plugin);
                clap_event_transport_t songLiveTransport {};
                songLiveTransport.header.size = sizeof(songLiveTransport);
                songLiveTransport.header.space_id =
                    CLAP_CORE_EVENT_SPACE_ID;
                songLiveTransport.header.type = CLAP_EVENT_TRANSPORT;
                songLiveTransport.flags = CLAP_TRANSPORT_HAS_TEMPO
                    | CLAP_TRANSPORT_IS_PLAYING;
                songLiveTransport.tempo = 120.0;
                OutputEvents songLiveOutput;
                clap_process_t songLiveProcess {};
                songLiveProcess.transport = &songLiveTransport;
                songLiveProcess.out_events = &songLiveOutput.interface;
                const auto processSongBlock = [&](uint32_t frames) {
                    songLiveOutput.count = 0u;
                    songLiveProcess.frames_count = frames;
                    const bool processed = plugin->process(plugin,
                        &songLiveProcess) == CLAP_PROCESS_CONTINUE;
                    songLiveProcess.steady_time += frames;
                    return processed;
                };
                if (songLiveProcessing)
                    songLiveProcessing &= processSongBlock(128u);
                [[NSRunLoop currentRunLoop] runUntilDate:
                    [NSDate dateWithTimeIntervalSinceNow:0.06]];
                NSView* liveRepeatsView = findAccessibleView(parent,
                    @"Song row 2 repetitions");
                NSPopUpButton* liveRepeats =
                    [liveRepeatsView isKindOfClass:NSPopUpButton.class]
                        ? static_cast<NSPopUpButton*>(liveRepeatsView) : nil;
                const NSInteger threeRepeats = [liveRepeats
                    indexOfItemWithRepresentedObject:@3];
                const uint32_t dirtyBeforeLiveSongEdit = context.dirtyMarks;
                bool repetitionsDispatched = threeRepeats >= 0;
                if (repetitionsDispatched) {
                    [liveRepeats selectItemAtIndex:threeRepeats];
                    repetitionsDispatched = [liveRepeats sendAction:
                        liveRepeats.action to:liveRepeats.target];
                }
                if (songLiveProcessing)
                    songLiveProcessing &= processSongBlock(128u);
                [[NSRunLoop currentRunLoop] runUntilDate:
                    [NSDate dateWithTimeIntervalSinceNow:0.06]];
                NSView* queueStatusView = findAccessibleView(parent,
                    @"Song queue status");
                NSTextField* queueStatus =
                    [queueStatusView isKindOfClass:NSTextField.class]
                        ? static_cast<NSTextField*>(queueStatusView) : nil;
                const bool editDidNotClaimPerformanceQueue =
                    ![queueStatus.stringValue containsString:@"QUEUED"];
                if (songLiveProcessing) {
                    for (unsigned pass = 0u; pass < 4u; ++pass)
                        songLiveProcessing &= processSongBlock(24000u);
                    songLiveProcessing &= processSongBlock(128u);
                    for (unsigned pass = 0u; pass < 4u; ++pass)
                        songLiveProcessing &= processSongBlock(24000u);
                    songLiveProcessing &= processSongBlock(128u);
                }
                [[NSRunLoop currentRunLoop] runUntilDate:
                    [NSDate dateWithTimeIntervalSinceNow:0.08]];
                liveRepeatsView = findAccessibleView(parent,
                    @"Song row 2 repetitions");
                liveRepeats = [liveRepeatsView
                        isKindOfClass:NSPopUpButton.class]
                    ? static_cast<NSPopUpButton*>(liveRepeatsView) : nil;
                NSView* liveRowView = liveRepeats;
                while (liveRowView
                    && ![liveRowView isKindOfClass:NSTableRowView.class]) {
                    liveRowView = liveRowView.superview;
                }
                ok &= expect(songLiveProcessing && repetitionsDispatched
                        && context.dirtyMarks > dirtyBeforeLiveSongEdit
                        && editDidNotClaimPerformanceQueue
                        && [liveRepeats.selectedItem.representedObject
                            integerValue] == 3
                        && [liveRowView.accessibilityValue
                            isEqualToString:@"Playing"],
                    "an edit to a future Song row did not enter playback at the next row boundary");
                if (songLiveProcessing) {
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);
                }
                context.playState = 0;
                [[NSRunLoop currentRunLoop] runUntilDate:
                    [NSDate dateWithTimeIntervalSinceNow:0.06]];
                ok &= expect(clickButton(parent, @"SONG: ON", nil, nil)
                        && clickButton(parent, nil, @"TRACKER page", nil),
                    "Song live-edit test could not restore pattern playback mode");
                const bool consoleSelected = clickButton(parent, nil,
                    @"CONSOLE page", nil);
                [parent layoutSubtreeIfNeeded];
                NSView* consoleLiveCode = findAccessibleView(parent,
                    @"Console live command input");
                NSView* consolePanel = findAccessibleView(parent,
                    @"Console output page");
                const bool consoleHeadingMatches = [consolePanel
                        isKindOfClass:NSClassFromString(
                            @"S3GTrackerToolboxView")]
                    && [[consolePanel valueForKey:@"toolboxTitle"]
                        isEqualToString:@"CONSOLE / LIVE CODE"]
                    && [[consolePanel valueForKey:@"toolboxIndex"]
                        integerValue] == 0;
                bool consoleCommandWorked = false;
                if ([consoleLiveCode isKindOfClass:NSTextField.class]) {
                    NSTextField* field = static_cast<NSTextField*>(
                        consoleLiveCode);
                    field.stringValue = @"autoalias";
                    const bool automatic = [field sendAction:field.action
                        to:field.target];
                    field.stringValue = @"aliases";
                    const bool listed = [field sendAction:field.action
                        to:field.target];
                    NSView* messagesView = findAccessibleView(parent,
                        @"Console printed messages");
                    NSString* messages =
                        [messagesView isKindOfClass:NSTextView.class]
                            ? static_cast<NSTextView*>(messagesView).string
                            : @"";
                    const NSRange kickAliases = [messages rangeOfString:
                        @"Lane 1 (Kick): @k"];
                    const NSRange snareAliases = [messages rangeOfString:
                        @"Lane 2 (Snare): @s"];
                    const NSRange tomAliases = [messages rangeOfString:
                        @"Lane 3 (Tom): @t"];
                    const NSRange hatAliases = [messages rangeOfString:
                        @"Lane 4 (Hat): @h"];
                    consoleCommandWorked = automatic && listed
                        && kickAliases.location != NSNotFound
                        && snareAliases.location > kickAliases.location
                        && tomAliases.location > snareAliases.location
                        && hatAliases.location > tomAliases.location;
                }
                const bool consoleDetached = clickButton(parent, nil,
                    @"Detach selected tool page", nil);
                NSWindow* detachedConsole = waitForOrderedDetachedWindow(
                    @"s3g Tracker — Console", hostWindow);
                ok &= expect(consoleSelected && consoleHeadingMatches
                        && consoleCommandWorked
                        && consoleDetached && detachedConsole != nil
                        && detachedConsole.parentWindow == nil
                        && detachedConsole.level > hostWindow.level
                        && !detachedConsole.hidesOnDeactivate
                        && findAccessibleView(detachedConsole.contentView,
                            @"Console live command input") != nil,
                    "Console autoalias/listing or independent detached-window ordering failed");
                [detachedConsole close];
                ok &= expect(visibleWindow(@"s3g Tracker — Console") == nil
                        && clickButton(parent, nil, @"TRACKER page", nil),
                    "detached Console could not return to the plug-in page");
                const bool helpSelected = clickButton(
                    parent, nil, @"HELP page", nil);
                [parent layoutSubtreeIfNeeded];
                NSView* helpTextView = findAccessibleView(parent,
                    @"All console commands, grouped by function");
                if (std::getenv("S3G_TRACKER_EXPECT_VSTGUI"))
                    ok &= expect([helpTextView isKindOfClass:NSTextView.class]
                            && static_cast<NSTextView*>(helpTextView).textLayoutManager == nil,
                        "magnified Help must use stable TextKit 1 layout");
                if (const char* helpCapture = std::getenv("S3G_TRACKER_HELP_CAPTURE_DIR"))
                    ok &= expect(writeDocumentationPage(parent,
                        [NSString stringWithUTF8String:helpCapture], @"help"),
                        "Help layout capture failed");
                NSView* helpPanel = findAccessibleView(parent,
                    @"Help command reference panel");
                const bool helpHeadingMatches = [helpPanel
                        isKindOfClass:NSClassFromString(
                            @"S3GTrackerToolboxView")]
                    && [[helpPanel valueForKey:@"toolboxTitle"]
                        isEqualToString:@"HELP / COMMAND REFERENCE"]
                    && [[helpPanel valueForKey:@"toolboxIndex"]
                        integerValue] == 0;
                BOOL differentiatedExampleStyling = NO;
                BOOL helpDocumentsCompactSymbols = NO;
                BOOL helpUsesCompactSectionRules = NO;
                BOOL helpOrganizesWorkflowReference = NO;
                if ([helpTextView isKindOfClass:NSTextView.class]) {
                    NSTextStorage* storage =
                        static_cast<NSTextView*>(helpTextView).textStorage;
                    helpDocumentsCompactSymbols =
                        [storage.string containsString:
                            @"COMPACT SYMBOL REFERENCE"]
                        && [storage.string containsString:
                            @"! = 1.00, + = 0.85"]
                        && [storage.string containsString:
                            @"A standalone - always means no authored event"]
                        && [storage.string containsString:
                            @"? is reserved for Help"]
                        && [storage.string containsString:
                            @"PITCH, RHYTHM & NOTE CELLS"]
                        && [storage.string containsString:
                            @"pitch|defaultnote <target> <MIDI|note name>"]
                        && [storage.string containsString:
                            @"pitch @kick C-2"]
                        && [storage.string containsString:
                            @"scale fit <target>"]
                        && [storage.string containsString:
                            @"scale generate <target>"]
                        && [storage.string containsString:@"QUICK ENTRY"]
                        && [storage.string containsString:
                            @"X toggles an anchored hit"];
                    helpOrganizesWorkflowReference =
                        [storage.string containsString:
                            @"TRACKER GRID WORKFLOW"]
                        && [storage.string containsString:
                            @"MIDI + LANE ROUTING"]
                        && [storage.string containsString:
                            @"TRANSPORT + SONG"]
                        && [storage.string containsString:
                            @"SONG ROW LENGTH"]
                        && [storage.string containsString:
                            @"TICKS is the number of tracker-row advances"]
                        && [storage.string containsString:
                            @"GEOMETRY + TOOL WINDOWS"];
                    const NSRange pitchHeading = [storage.string
                        rangeOfString:@"PITCH, RHYTHM & NOTE CELLS"];
                    NSFont* pitchHeadingFont = pitchHeading.location
                            != NSNotFound
                        ? [storage attribute:NSFontAttributeName
                            atIndex:pitchHeading.location
                            effectiveRange:nullptr] : nil;
                    helpUsesCompactSectionRules =
                        [storage.string containsString:
                            @"────────────────────────────────"]
                        && pitchHeadingFont
                        && pitchHeadingFont.pointSize <= 11.0;
                    const NSRange exampleRange = [storage.string
                        rangeOfString:@"EXAMPLE  "];
                    if (exampleRange.location != NSNotFound
                        && NSMaxRange(exampleRange) < storage.length) {
                        NSColor* exampleLabelColor = [storage attribute:
                            NSForegroundColorAttributeName
                            atIndex:exampleRange.location
                            effectiveRange:nullptr];
                        NSColor* exampleCommandColor = [storage attribute:
                            NSForegroundColorAttributeName
                            atIndex:NSMaxRange(exampleRange)
                            effectiveRange:nullptr];
                        const NSRange syntaxRange = [storage.string
                            rangeOfString:
                                @"pitch|defaultnote <target> <MIDI|note name>"];
                        NSColor* syntaxColor = syntaxRange.location
                                != NSNotFound
                            ? [storage attribute:
                                NSForegroundColorAttributeName
                                atIndex:syntaxRange.location
                                effectiveRange:nullptr] : nil;
                        differentiatedExampleStyling = exampleLabelColor
                            && exampleCommandColor && syntaxColor
                            && ![exampleLabelColor
                                isEqual:exampleCommandColor]
                            && ![syntaxColor isEqual:exampleCommandColor];
                    }
                }
                ok &= expect(helpSelected && helpHeadingMatches
                        && differentiatedExampleStyling
                        && helpDocumentsCompactSymbols
                        && helpUsesCompactSectionRules
                        && helpOrganizesWorkflowReference
                        && clickButton(parent, nil,
                            @"Detach selected tool page", nil),
                    "Help symbol reference, example styling, or detach action is incomplete");
                NSWindow* detachedHelp = waitForOrderedDetachedWindow(
                    @"s3g Tracker — Help", hostWindow);
                ok &= expect(detachedHelp != nil
                        && detachedHelp.parentWindow == nil
                        && detachedHelp.level > hostWindow.level
                        && !detachedHelp.hidesOnDeactivate,
                    "detached Help did not remain an independent visible window");
                [detachedHelp close];
                ok &= expect(visibleWindow(@"s3g Tracker — Help") == nil
                        && clickButton(parent, nil, @"TRACKER page", nil),
                    "detached Help window could not be reattached");
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                NSString* directory = captureDirectory && captureDirectory[0]
                    ? [NSString stringWithUTF8String:captureDirectory] : nil;
                bool documentationProcessing = false;
                if (directory) {
                    [hostWindow setContentSize:NSMakeSize(
                        requestedWidth, requestedHeight)];
                    [parent setFrame:NSMakeRect(
                        0.0, 0.0, requestedWidth, requestedHeight)];
                    [parent layoutSubtreeIfNeeded];
                    ok &= expect(NSWidth(parent.bounds) == requestedWidth
                            && NSHeight(parent.bounds) == requestedHeight,
                        "tracker documentation host size could not be restored");
                    ok &= expect(clickButton(parent, @"DUP", nil, nil)
                            && clickButton(parent, @"DUP", nil, nil),
                        "tracker documentation patterns could not be prepared");
                    for (int row = 0; row < 4; ++row) {
                        ok &= expect(clickButton(
                            parent, @"＋ ADD", nil, nil),
                            "tracker documentation Song rows could not be added");
                    }
                    ok &= expect(clickButton(
                            parent, nil, @"SONG page", nil),
                        "Song documentation page could not be prepared");
                    [parent layoutSubtreeIfNeeded];
                    [parent displayIfNeeded];
                    ok &= expect(prepareDocumentationSongMuteControls(parent),
                        "tracker documentation Song mute controls could not be prepared");
                    NSButton* fourthLane = findButton(parent, nil,
                        @"Song row 1 lane 4 mute", nil);
                    NSButton* fifthLane = findButton(parent, nil,
                        @"Song row 1 lane 5 mute", nil);
                    ok &= expect(fourthLane && fourthLane.enabled
                            && fifthLane && !fifthLane.enabled,
                        "Song mute availability did not follow the selected pattern's actual lane count");
                    const std::array<std::pair<NSUInteger, NSUInteger>, 9u>
                        songLaneMutes {{
                        { 0u, 0u }, { 0u, 1u },
                        { 1u, 2u },
                        { 2u, 1u }, { 2u, 3u },
                        { 3u, 0u },
                        { 4u, 2u }, { 4u, 3u },
                        { 5u, 1u },
                    }};
                    for (const auto& mute : songLaneMutes) {
                        ok &= expect(prepareDocumentationSongMuteControls(parent),
                            "tracker documentation Song mute controls could not be prepared");
                        NSString* label = [NSString stringWithFormat:
                            @"Song row %lu lane %lu mute",
                            static_cast<unsigned long>(mute.first + 1u),
                            static_cast<unsigned long>(mute.second + 1u)];
                        ok &= expect(clickButton(parent, nil, label, nil),
                            "tracker documentation Song lane mute could not be set");
                    }
                    ok &= expect(setDocumentationSongPatternLoop(
                                parent, 1u, 1, 4)
                            && setDocumentationSongPatternLoop(
                                parent, 3u, 5, 8)
                            && setDocumentationSongPatternLoop(
                                parent, 5u, 9, 12),
                        "tracker documentation Song pattern loops could not be set");
                    ok &= expect(clickButton(
                            parent, @"LOOP: OFF", nil, nil)
                            && clickButton(parent,
                                @"SONG: OFF", nil, nil),
                        "tracker documentation Song mode could not be enabled");
                    ok &= expect(clickButton(
                            parent, nil, nil, @"warp-add-0")
                            && clickButton(parent,
                                nil, nil, @"warp-add-1")
                            && clickButton(parent,
                                nil, nil, @"warp-add-2"),
                        "tracker documentation warp stack could not be prepared");
                    context.playState = 1;
                    documentationProcessing = plugin->activate(
                            plugin, 48000.0, 16u, 32768u)
                        && plugin->start_processing(plugin);
                    ok &= expect(documentationProcessing,
                        "tracker documentation playback could not start");
                    if (documentationProcessing) {
                        clap_event_transport_t captureTransport {};
                        captureTransport.header.size = sizeof(captureTransport);
                        captureTransport.header.space_id =
                            CLAP_CORE_EVENT_SPACE_ID;
                        captureTransport.header.type = CLAP_EVENT_TRANSPORT;
                        captureTransport.flags = CLAP_TRANSPORT_HAS_TEMPO
                            | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
                            | CLAP_TRANSPORT_IS_PLAYING;
                        captureTransport.tempo = 120.0;
                        captureTransport.song_pos_beats = 0;
                        OutputEvents captureOutput;
                        clap_process_t captureProcess {};
                        captureProcess.steady_time = 0;
                        captureProcess.frames_count = 128u;
                        captureProcess.transport = &captureTransport;
                        captureProcess.out_events = &captureOutput.interface;
                        ok &= expect(plugin->process(plugin, &captureProcess)
                                == CLAP_PROCESS_CONTINUE,
                            "tracker documentation playback failed");
                        [[NSRunLoop currentRunLoop] runUntilDate:
                            [NSDate dateWithTimeIntervalSinceNow:0.05]];
                    }
                }

                ok &= expect(clickButton(
                        parent, nil, @"TRACKER page", nil),
                    "Tracker documentation page could not be selected");
                ok &= expect(submitCommand(parent, @"fx 1 1 2 CC74 64")
                        && submitCommand(parent, @"interp 1 v1 step"),
                    "Tracker MIDI CC fixture could not be prepared");
                if (directory) {
                    ok &= expect(submitCommand(parent,
                                @"fx 1 1 1 RR 0.62")
                            && submitCommand(parent,
                                @"fx 1 1 5 PR 0.84")
                            && submitCommand(parent,
                                @"fx 1 1 9 AC 0.72")
                            && submitCommand(parent,
                                @"fx 1 1 13 FL 0.46")
                            && submitCommand(parent,
                                @"fx 1 2 1 MT 0.58")
                            && submitCommand(parent,
                                @"fx 1 2 5 ST 0.36")
                            && submitCommand(parent,
                                @"fx 1 2 9 GL 0.64")
                            && submitCommand(parent,
                                @"fx 1 2 13 SK 0.48")
                            && submitCommand(parent,
                                @"vol 1 1.0 0.68 0.82 0.54 0.94 0.62 0.78 0.48 0.88 0.58 0.74 0.44 0.84 0.52 0.70 0.40"),
                        "Tracker documentation sequencing fixture could not be prepared");
                    ok &= expect(clickButton(parent, nil,
                                @"Expand tracker sequencing columns", nil)
                            && selectTrackerGridVolumeField(parent),
                        "Tracker documentation sequencing columns or Volume field could not be selected");
                }
                ok &= expect(writeDocumentationPage(parent, directory, @""),
                    "full tracker workspace did not render");
                if (directory) {
                    ok &= expect(clickButton(
                            parent, nil, @"GEOMETRY page", nil)
                            && prepareGeometryPlaybackSnapshot(parent)
                            && writeDocumentationPage(
                                parent, directory, @"geometry"),
                        "active Tracker Geometry page did not render");
                    ok &= expect(clickButton(parent, nil, @"SONG page", nil)
                            && prepareDocumentationSongMuteControls(parent)
                            && writeDocumentationPage(
                                parent, directory, @"song"),
                        "active Tracker Song page did not render");
                    ok &= expect(clickButton(parent, nil, @"WARPS page", nil)
                            && writeDocumentationPage(
                                parent, directory, @"warps"),
                        "active Tracker Warps page did not render");
                }
                if (documentationProcessing) {
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);
                    context.playState = 0;
                    [[NSRunLoop currentRunLoop] runUntilDate:
                        [NSDate dateWithTimeIntervalSinceNow:0.05]];
                }
                if (directory) {
                    // The capture deliberately shows Song transport enabled,
                    // but the DSP assertions below exercise the active
                    // pattern directly. Stop first so Song controls unlock,
                    // then restore pattern transport and identity timing.
                    ok &= expect(clickButton(parent, nil, @"SONG page", nil)
                            && clickButton(parent,
                                @"SONG: ON", nil, nil)
                            && clickButton(parent, nil, @"TRACKER page", nil)
                            && submitCommand(parent, @"warp clear"),
                        "tracker documentation transport state could not be restored");
                    [[NSRunLoop currentRunLoop] runUntilDate:
                        [NSDate dateWithTimeIntervalSinceNow:0.05]];
                }
                if (std::getenv("S3G_TRACKER_EXPECT_VSTGUI")) {
                    using DrawCount = uint64_t (*)(unsigned);
                    auto drawCount = reinterpret_cast<DrawCount>(dlsym(library,
                        "s3g_tracker_vstgui_pilot_draw_count"));
                    ok &= expect(drawCount != nullptr,
                        "expected Tracker VSTGUI pilot, but loaded a Cocoa-only build");
                    for (unsigned surface = 0u; drawCount && surface < 4u; ++surface) {
                        ok &= expect(drawCount(surface) > 0u,
                            "a Tracker pilot surface never drew through VSTGUI");
                    }
                }
                ok &= expect(gui->hide(plugin),
                    "full tracker workspace hide failed");
            }
            gui->destroy(plugin);
            ok &= expect(context.keyboardAccelerators.empty() && !context.hwndInfo,
                "Tracker keyboard registrations survived GUI destruction");
            if (shown && std::getenv("S3G_TRACKER_EXPECT_VSTGUI")) {
                ok &= expect(parent.subviews.count == 0u
                        && gui->create(plugin, CLAP_WINDOW_API_COCOA, false)
                        && gui->set_parent(plugin, &window)
                        && gui->set_size(plugin, requestedWidth, requestedHeight)
                        && gui->show(plugin) && gui->hide(plugin),
                    "scaled Tracker editor did not cleanly close/reopen");
                gui->destroy(plugin);
                ok &= expect(parent.subviews.count == 0u,
                    "scaled Tracker viewport remained attached after destroy");
            }
            [hostWindow close];
        }
    }
#endif
#if defined(__APPLE__)
    // Documentation mode has already exercised the complete native GUI and
    // written every requested PDF above. Its intentionally embellished Song,
    // Warp, and pattern state is not the deterministic DSP fixture used by
    // the ordinary no-environment smoke run below.
    const char* documentationDirectory = std::getenv(
        "S3G_GUI_SMOKE_PDF_DIR");
    if (documentationDirectory && documentationDirectory[0]) {
        if (plugin) plugin->destroy(plugin);
        activeReaperHost = nullptr;
        if (entry) entry->deinit();
        if (library) dlclose(library);
        if (!ok) return 1;
        std::puts("s3g tracker CLAP documentation smoke: ok");
        return 0;
    }
#endif
    StateBuffer stateBuffer;
    ok &= expect(state && state->save(plugin, &stateBuffer.output)
        && !stateBuffer.bytes.empty()
        && stateBuffer.bytes.front() == static_cast<uint8_t>('{'),
        "native tracker project JSON did not save");
    const std::string savedJson(stateBuffer.bytes.begin(),
        stateBuffer.bytes.end());
    ok &= expect(savedJson.find("\"state\": \"midi-control-change\"")
                != std::string::npos
            && savedJson.find("\"controller\": 74")
                != std::string::npos,
        "Tracker MIDI CC edit did not persist in project state");

    std::string retiredJson = savedJson;
    const std::string currentFormat = "s3g-tracker-midi-composition";
    const auto formatAt = retiredJson.find(currentFormat);
    if (formatAt != std::string::npos)
        retiredJson.replace(formatAt, currentFormat.size(),
            "s3g-tracker-project");
    StateBuffer retiredState;
    retiredState.bytes.assign(retiredJson.begin(), retiredJson.end());
    ok &= expect(formatAt != std::string::npos
            && !state->load(plugin, &retiredState.input),
        "retired hybrid tracker state should be rejected");

    StateBuffer afterRejectedState;
    ok &= expect(state->save(plugin, &afterRejectedState.output)
            && afterRejectedState.bytes == stateBuffer.bytes,
        "rejecting retired state should not mutate the current MIDI composition");
    ok &= expect(plugin && plugin->activate(plugin, 48000.0, 16u, 32768u)
        && plugin->start_processing(plugin), "activation failed");

    clap_event_transport_t transport {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
        | CLAP_TRANSPORT_IS_PLAYING;
    transport.tempo = 120.0;
    transport.song_pos_beats = 0;

    OutputEvents output;
    clap_process_t process {};
    process.steady_time = 0;
    // Keep the first block shorter than the default 90 ms gate so the
    // following block can prove that only the replacement note owns it.
    process.frames_count = 2048u;
    process.transport = &transport;
    process.out_events = &output.interface;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "process failed");
    ok &= expect(output.count >= 4u, "default pattern did not emit drum MIDI");
    for (uint32_t index = 1u; index < output.count; ++index) {
        ok &= expect(output.events[index - 1u].header.time
            <= output.events[index].header.time,
            "output MIDI is not sample sorted");
    }
    for (uint32_t index = 0u; index < output.count; ++index) {
        ok &= expect(output.events[index].port_index == 0u,
            "output MIDI escaped the single declared note port");
    }
    ok &= expect(output.count > 0u
        && output.events[0].header.time < process.frames_count
        && (output.events[0].data[0] & 0xf0u) == 0x90u
        && (output.events[0].data[0] & 0x0fu) == 0u,
        "first event should be a sample-aligned channel-1 note-on");
    const uint32_t replacementOnset = output.count > 0u
        ? output.events[0].header.time : 0u;
    const bool latestNoteWins = output.count >= 3u
        && output.events[0].header.time == replacementOnset
        && output.events[1].header.time == replacementOnset
        && output.events[2].header.time == replacementOnset
        && (output.events[0].data[0] & 0xf0u) == 0x90u
        && (output.events[1].data[0] & 0xf0u) == 0x80u
        && (output.events[2].data[0] & 0xf0u) == 0x90u
        && output.events[0].data[1] == 36u
        && output.events[1].data[1] == 36u
        && output.events[2].data[1] == 36u;
    if (!latestNoteWins) {
        std::fprintf(stderr, "tracker CLAP overlap events:");
        for (uint32_t index = 0u; index < output.count; ++index) {
            std::fprintf(stderr, " [%u:%02x:%u]",
                output.events[index].header.time,
                output.events[index].data[0],
                output.events[index].data[1]);
        }
        std::fputc('\n', stderr);
    }
    ok &= expect(latestNoteWins,
        "same-channel same-pitch overlap did not follow latest-note-wins retrigger policy");

    output.count = 0u;
    process.steady_time += process.frames_count;
    transport.song_pos_beats = static_cast<int64_t>(CLAP_BEATTIME_FACTOR)
        * 2048 / 24000;
    process.frames_count = 4096u;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "overlap gate process failed");
    uint32_t replacementGateOffs = 0u;
    bool emittedMidiCc = false;
    for (uint32_t index = 0u; index < output.count; ++index) {
        if ((output.events[index].data[0] & 0xf0u) == 0x80u
            && output.events[index].data[1] == 36u)
            ++replacementGateOffs;
        emittedMidiCc |= output.events[index].header.time == 3952u
            && output.events[index].data[0] == 0xb0u
            && output.events[index].data[1] == 74u
            && output.events[index].data[2] == 64u;
    }
    ok &= expect(replacementGateOffs == 1u,
        "a superseded same-pitch gate cut or duplicated the replacement note-off");
    if (!emittedMidiCc) {
        std::fprintf(stderr, "tracker CLAP second-block events:");
        for (uint32_t index = 0u; index < output.count; ++index) {
            std::fprintf(stderr, " [%u:%02x:%u:%u]",
                output.events[index].header.time,
                output.events[index].data[0],
                output.events[index].data[1],
                output.events[index].data[2]);
        }
        std::fputc('\n', stderr);
    }
    ok &= expect(emittedMidiCc,
        "rest-row SEQ MIDI CC did not reach the CLAP MIDI output");

    output.count = 0u;
    transport.flags &= ~CLAP_TRANSPORT_IS_PLAYING;
    process.steady_time += process.frames_count;
    process.frames_count = 128u;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "stop process failed");

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        const bool largeBlockActive = plugin->activate(
                plugin, 48000.0, 16u, 32768u)
            && plugin->start_processing(plugin);
        uint32_t sameBlockPitch36Offs = 0u;
        if (largeBlockActive) {
            output.count = 0u;
            transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
            transport.song_pos_beats = 0;
            process.steady_time = 0;
            process.frames_count = 12000u;
            process.in_events = nullptr;
            ok &= expect(plugin->process(plugin, &process)
                    == CLAP_PROCESS_CONTINUE,
                "large-block gate process failed");
            const uint32_t largeBlockOnset = output.count > 0u
                ? output.events[0u].header.time : 0u;
            for (uint32_t index = 0u; index < output.count; ++index) {
                if (output.events[index].header.time > largeBlockOnset
                    && (output.events[index].data[0] & 0xf0u) == 0x80u
                    && output.events[index].data[1] == 36u)
                    ++sameBlockPitch36Offs;
            }
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
        }
        if (!largeBlockActive || sameBlockPitch36Offs != 1u) {
            std::fprintf(stderr, "tracker CLAP large-block events:");
            for (uint32_t index = 0u; index < output.count; ++index) {
                std::fprintf(stderr, " [%u:%02x:%u]",
                    output.events[index].header.time,
                    output.events[index].data[0],
                    output.events[index].data[1]);
            }
            std::fputc('\n', stderr);
        }
        ok &= expect(largeBlockActive && sameBlockPitch36Offs == 1u,
            "a gate beginning and ending inside one host block was not released exactly once");
        plugin->destroy(plugin);
    }
    activeReaperHost = nullptr;
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (!ok) return 1;
    std::puts("s3g tracker CLAP smoke: ok");
    return 0;
}
