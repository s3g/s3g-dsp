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
#include <vector>

namespace {

struct HostContext {
    clap_host_t host {};
    uint32_t processRequests = 0u;
    uint32_t dirtyMarks = 0u;
    clap_host_state_t state {};
};

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    auto* context = static_cast<HostContext*>(host->host_data);
    return id && std::strcmp(id, CLAP_EXT_STATE) == 0
        ? &context->state : nullptr;
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

@implementation S3GTrackerCaptureHostView

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [[NSColor colorWithCalibratedWhite:0.024 alpha:1.0] setFill];
    NSRectFill(self.bounds);
}

@end


namespace {

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
    SEL selector = NSSelectorFromString(
        @"prepareDocumentationPlaybackSnapshot");
    if (!geometry || ![geometry respondsToSelector:selector]) return false;
    using PrepareFunction = NSInteger (*)(id, SEL);
    auto prepare = reinterpret_cast<PrepareFunction>(
        [geometry methodForSelector:selector]);
    return prepare(geometry, selector) >= 4;
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
    clap_note_port_info_t port {};
    ok &= expect(notePorts && notePorts->count(plugin, true) == 0u
        && notePorts->count(plugin, false) == 8u
        && notePorts->get(plugin, 0u, false, &port)
        && port.preferred_dialect == CLAP_NOTE_DIALECT_MIDI,
        "plugin should expose eight instrument-owned MIDI output buses");
    ok &= expect(plugin && !plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS),
        "tracker should not expose audio ports");
    ok &= expect(state != nullptr, "project state extension is missing");
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
                    && width >= 760u && height >= 560u,
                "full tracker workspace could not be constructed");
            NSView* parent = [[S3GTrackerCaptureHostView alloc]
                initWithFrame:NSMakeRect(
                0.0, 0.0, width, height)];
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
                const char* captureDirectory = std::getenv(
                    "S3G_GUI_SMOKE_PDF_DIR");
                NSString* directory = captureDirectory && captureDirectory[0]
                    ? [NSString stringWithUTF8String:captureDirectory] : nil;
                bool documentationProcessing = false;
                if (directory) {
                    ok &= expect(clickButton(parent, @"DUP", nil, nil)
                            && clickButton(parent, @"DUP", nil, nil),
                        "tracker documentation patterns could not be prepared");
                    for (int row = 0; row < 4; ++row) {
                        ok &= expect(clickButton(
                                parent, @"＋ ADD ROW", nil, nil),
                            "tracker documentation Song rows could not be added");
                    }
                    ok &= expect(clickButton(
                            parent, nil, @"SONG page", nil),
                        "Song documentation page could not be prepared");
                    [parent layoutSubtreeIfNeeded];
                    [parent displayIfNeeded];
                    const std::array<NSString*, 14u> songLaneMutes {{
                        @"Song row 1 lane 5 mute",
                        @"Song row 1 lane 6 mute",
                        @"Song row 2 lane 3 mute",
                        @"Song row 2 lane 7 mute",
                        @"Song row 3 lane 2 mute",
                        @"Song row 3 lane 4 mute",
                        @"Song row 3 lane 6 mute",
                        @"Song row 4 lane 1 mute",
                        @"Song row 4 lane 5 mute",
                        @"Song row 5 lane 3 mute",
                        @"Song row 5 lane 4 mute",
                        @"Song row 5 lane 7 mute",
                        @"Song row 6 lane 2 mute",
                        @"Song row 6 lane 6 mute",
                    }};
                    for (NSString* label : songLaneMutes) {
                        ok &= expect(clickButton(parent, nil, label, nil),
                            "tracker documentation Song lane mute could not be set");
                    }
                    ok &= expect(clickButton(
                            parent, @"LOOP SONG: OFF", nil, nil)
                            && clickButton(parent,
                                @"SONG TRANSPORT: OFF", nil, nil),
                        "tracker documentation Song mode could not be enabled");
                    ok &= expect(clickButton(
                            parent, nil, nil, @"warp-add-0")
                            && clickButton(parent,
                                nil, nil, @"warp-add-1")
                            && clickButton(parent,
                                nil, nil, @"warp-add-2"),
                        "tracker documentation warp stack could not be prepared");
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
                }
                ok &= expect(gui->hide(plugin),
                    "full tracker workspace hide failed");
            }
            gui->destroy(plugin);
        }
    }
#endif
    StateBuffer stateBuffer;
    ok &= expect(state && state->save(plugin, &stateBuffer.output)
        && !stateBuffer.bytes.empty()
        && stateBuffer.bytes.front() == static_cast<uint8_t>('{')
        && state->load(plugin, &stateBuffer.input),
        "native tracker project JSON did not round-trip");
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
    process.frames_count = 12000u;
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
    ok &= expect(output.count > 0u
        && output.events[0].header.time == 0u
        && (output.events[0].data[0] & 0xf0u) == 0x90u
        && (output.events[0].data[0] & 0x0fu) == 9u,
        "first event should be a channel-10 note-on at sample zero");

    output.count = 0u;
    transport.flags &= ~CLAP_TRANSPORT_IS_PLAYING;
    process.steady_time += process.frames_count;
    process.frames_count = 128u;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "stop process failed");

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (!ok) return 1;
    std::puts("s3g tracker CLAP smoke: ok");
    return 0;
}
