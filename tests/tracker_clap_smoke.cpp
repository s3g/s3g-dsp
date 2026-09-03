#include <clap/clap.h>

#include <dlfcn.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
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
};

HostContext* activeReaperHost = nullptr;

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
- (NSRect)burstPreviewHeaderButtonRect;
- (BOOL)handleToolboxClickAtPoint:(NSPoint)point;
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
    if (!button) return false;
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

bool writeDocumentationPage(NSView* root, NSString* directory,
    NSString* variant)
{
    [root setNeedsDisplay:YES];
    [root layoutSubtreeIfNeeded];
    [root displayIfNeeded];
    NSData* rendered = [root dataWithPDFInsideRect:root.bounds];
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
    return [rendered writeToFile:
        [directory stringByAppendingPathComponent:fileName]
        atomically:YES];
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
            || parsedHeight < 560u || parsedHeight > UINT32_MAX) {
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
            && factoryJson.find("\"schemaVersion\": 10")
                != std::string::npos
            && factoryJson.find("\"showMidiNoteValues\": true")
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
                    && width == 1320u && height == 860u,
                "full tracker workspace could not be constructed");
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
            if (shown) {
                [parent setFrameSize:NSMakeSize(resizedWidth, resizedHeight)];
                [parent layoutSubtreeIfNeeded];
                NSView* routeStatusView = findAccessibleView(parent,
                    @"Tracker route status");
                NSTextField* routeStatus =
                    [routeStatusView isKindOfClass:NSTextField.class]
                        ? static_cast<NSTextField*>(routeStatusView) : nil;
                NSView* hostBpmView = findAccessibleView(parent,
                    @"Host tempo in beats per minute");
                NSTextField* hostBpm =
                    [hostBpmView isKindOfClass:NSTextField.class]
                        ? static_cast<NSTextField*>(hostBpmView) : nil;
                const NSRect hostBpmFrame = hostBpm
                    ? [hostBpm convertRect:hostBpm.bounds toView:parent]
                    : NSZeroRect;
                ok &= expect([routeStatus.stringValue
                            containsString:@"1 OUT • 1 REC IN • CH 1–16"]
                        && ![routeStatus.stringValue
                            containsString:@"8 REAPER MIDI BUSES"],
                    "routing status did not reflect the single CLAP MIDI port");
                ok &= expect([hostBpm.stringValue hasPrefix:@"HOST BPM"]
                        && NSMaxX(hostBpmFrame)
                            >= NSWidth(parent.bounds) - 20.0,
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
                const bool geometryModesAvailable = geometryMode.numberOfItems
                        == 7u
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
                        isEqualToString:@"BURST EDITOR"];
                if (geometryModesAvailable) {
                    for (NSInteger mode = 1; mode < 7; ++mode) {
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
                    && geometryModesAvailable;
                NSView* geometryView = findAccessibleView(parent,
                    @"Rhythm geometry");
                if (burstPrepared) {
                    [geometryMode selectItemAtIndex:6u];
                    [geometryMode sendAction:geometryMode.action
                        to:geometryMode.target];
                }
                bool previewAudioOk = false;
                if (burstPrepared && geometryView
                    && plugin->activate(plugin, 48000.0, 64u, 8192u)
                    && plugin->start_processing(plugin)) {
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
                    std::size_t onsetCount = 0u;
                    for (uint32_t index = 0u;
                         index < previewOutput.count; ++index) {
                        const auto& event = previewOutput.events[index];
                        if ((event.data[0] & 0xf0u) != 0x90u
                            || event.data[2] == 0u
                            || onsetCount >= onsetTimes.size()) continue;
                        onsetTimes[onsetCount++] = event.header.time;
                    }
                    previewAudioOk = clicked && processed
                        && onsetCount == 4u
                        && onsetTimes == std::array<uint32_t, 4u> {{
                            0u, 1500u, 3000u, 4500u,
                        }};
                    plugin->stop_processing(plugin);
                    plugin->deactivate(plugin);
                }
                ok &= expect(previewAudioOk,
                    "stopped Burst Preview did not emit substeps at project-BPM row positions");
                ok &= expect(geometryModesAvailable
                        && clickButton(parent, nil, @"TRACKER page", nil),
                    "Geometry Ring Field and diagnostic view selector is incomplete");
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
                ok &= expect(gui->hide(plugin),
                    "full tracker workspace hide failed");
            }
            gui->destroy(plugin);
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
    std::string legacyJson(stateBuffer.bytes.begin(),
        stateBuffer.bytes.end());
    ok &= expect(legacyJson.find("\"state\": \"midi-control-change\"")
                != std::string::npos
            && legacyJson.find("\"controller\": 74")
                != std::string::npos,
        "Tracker MIDI CC edit did not persist in project state");
    const std::string initialKey = "\"initialInstrumentNode\": ";
    const auto initialKeyAt = legacyJson.find(initialKey);
    const auto initialValueAt = initialKeyAt == std::string::npos
        ? std::string::npos : initialKeyAt + initialKey.size();
    const auto initialValueEnd = initialValueAt == std::string::npos
        ? std::string::npos
        : legacyJson.find_first_not_of("0123456789", initialValueAt);
    const auto activeKeyAt = legacyJson.find("\"activeNodes\": [");
    const auto activeValueAt = activeKeyAt == std::string::npos
        ? std::string::npos
        : legacyJson.find_first_of("0123456789", activeKeyAt);
    const auto activeValueEnd = activeValueAt == std::string::npos
        ? std::string::npos
        : legacyJson.find_first_not_of("0123456789", activeValueAt);
    const bool legacyStateReady = initialValueAt != std::string::npos
        && initialValueEnd != std::string::npos
        && activeValueAt != std::string::npos
        && activeValueEnd != std::string::npos;
    unsigned long legacyNode = 0u;
    if (legacyStateReady) {
        const unsigned long firstMidiNode = std::strtoul(
            legacyJson.c_str() + initialValueAt, nullptr, 10);
        legacyNode = firstMidiNode + 5u;
        legacyJson.replace(initialValueAt,
            initialValueEnd - initialValueAt, std::to_string(legacyNode));
        const auto shiftedActiveValueEnd = legacyJson.find_first_not_of(
            "0123456789", legacyJson.find_first_of(
                "0123456789", legacyJson.find("\"activeNodes\": [")));
        legacyJson.insert(shiftedActiveValueEnd,
            ",\n      " + std::to_string(legacyNode));
    }
    StateBuffer legacyState;
    legacyState.bytes.assign(legacyJson.begin(), legacyJson.end());
    ok &= expect(legacyStateReady
            && state->load(plugin, &legacyState.input),
        "legacy multi-port tracker project did not load");
    StateBuffer normalizedState;
    const bool normalizedSaved = state->save(plugin, &normalizedState.output);
    const std::string normalizedJson(normalizedState.bytes.begin(),
        normalizedState.bytes.end());
    const auto normalizedActiveAt = normalizedJson.find("\"activeNodes\": [");
    const auto normalizedActiveEnd = normalizedActiveAt == std::string::npos
        ? std::string::npos : normalizedJson.find(']', normalizedActiveAt);
    const std::string normalizedActiveNodes = normalizedActiveAt
            != std::string::npos && normalizedActiveEnd != std::string::npos
        ? normalizedJson.substr(normalizedActiveAt,
            normalizedActiveEnd - normalizedActiveAt)
        : std::string {};
    ok &= expect(normalizedSaved
            && normalizedJson.find(initialKey + std::to_string(legacyNode))
                == std::string::npos
            && normalizedActiveNodes.find(std::to_string(legacyNode))
                == std::string::npos,
        "obsolete MIDI bus assignments did not collapse onto the single port");
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
