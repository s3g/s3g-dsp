#include <clap/clap.h>

#include <dlfcn.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
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

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: tracker_clap_smoke <bundle-or-binary>\n");
        return 2;
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
            NSView* parent = [[NSView alloc] initWithFrame:NSMakeRect(
                0.0, 0.0, width, height)];
            clap_window_t window {};
            window.api = CLAP_WINDOW_API_COCOA;
            window.cocoa = (__bridge clap_nsview)parent;
            uint32_t resizedWidth = 900u;
            uint32_t resizedHeight = 620u;
            ok &= expect(gui->set_parent(plugin, &window)
                    && gui->adjust_size(plugin, &resizedWidth, &resizedHeight)
                    && gui->set_size(plugin, resizedWidth, resizedHeight)
                    && gui->show(plugin) && gui->hide(plugin),
                "full tracker workspace lifecycle failed");
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
