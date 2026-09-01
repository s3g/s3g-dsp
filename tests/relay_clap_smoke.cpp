#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <dlfcn.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct HostContext {
    clap_host_t host {};
    clap_host_params_t params {};
    uint32_t processRequests = 0u;
    uint32_t flushRequests = 0u;
};

void hostRequestRestart(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}

void hostRequestProcess(const clap_host_t* host)
{
    ++static_cast<HostContext*>(host->host_data)->processRequests;
}

void hostParamsRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}

void hostParamsRequestFlush(const clap_host_t* host)
{
    ++static_cast<HostContext*>(host->host_data)->flushRequests;
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (id && std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &static_cast<HostContext*>(host->host_data)->params;
    return nullptr;
}

struct InputEvents {
    clap_input_events_t interface {};
    std::array<clap_event_param_value_t, 32u> events {};
    uint32_t count = 0u;

    InputEvents()
    {
        interface.ctx = this;
        interface.size = size;
        interface.get = get;
    }

    void add(clap_id id, double value)
    {
        auto& event = events[count++];
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }

    static uint32_t size(const clap_input_events_t* list)
    {
        return static_cast<const InputEvents*>(list->ctx)->count;
    }

    static const clap_event_header_t* get(const clap_input_events_t* list,
        uint32_t index)
    {
        const auto* self = static_cast<const InputEvents*>(list->ctx);
        return index < self->count ? &self->events[index].header : nullptr;
    }
};

struct MidiInputEvents {
    clap_input_events_t interface {};
    std::array<clap_event_midi_t, 16u> events {};
    uint32_t count = 0u;

    MidiInputEvents()
    {
        interface.ctx = this;
        interface.size = size;
        interface.get = get;
    }

    void add(uint32_t frame, uint8_t status, uint8_t data1, uint8_t data2)
    {
        auto& event = events[count++];
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0] = status;
        event.data[1] = data1;
        event.data[2] = data2;
    }

    static uint32_t size(const clap_input_events_t* list)
    {
        return static_cast<const MidiInputEvents*>(list->ctx)->count;
    }

    static const clap_event_header_t* get(const clap_input_events_t* list,
        uint32_t index)
    {
        const auto* self = static_cast<const MidiInputEvents*>(list->ctx);
        return index < self->count ? &self->events[index].header : nullptr;
    }
};

struct OutputEvents {
    clap_output_events_t interface {};
    std::array<clap_event_midi_t, 512u> midi {};
    uint32_t midiCount = 0u;

    OutputEvents()
    {
        interface.ctx = this;
        interface.try_push = push;
    }

    static bool push(const clap_output_events_t* list,
        const clap_event_header_t* header)
    {
        auto* self = static_cast<OutputEvents*>(list->ctx);
        if (!header) return false;
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID
            && header->type == CLAP_EVENT_MIDI
            && header->size >= sizeof(clap_event_midi_t)) {
            if (self->midiCount >= self->midi.size()) return false;
            self->midi[self->midiCount++] = *reinterpret_cast<
                const clap_event_midi_t*>(header);
        }
        return true;
    }
};

struct StateBuffer {
    clap_ostream_t output {};
    clap_istream_t input {};
    std::vector<uint8_t> bytes;
    std::size_t cursor = 0u;

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
        if (!source || count > 1024u * 1024u) return -1;
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
            static_cast<std::size_t>(amount));
        self->cursor += static_cast<std::size_t>(amount);
        return static_cast<int64_t>(amount);
    }
};

bool expect(bool condition, const char* message)
{
    if (condition) return true;
    std::fprintf(stderr, "Relay CLAP: %s\n", message);
    return false;
}

std::string resolveBinary(const char* input)
{
    std::string path = input ? input : "";
#if defined(__APPLE__)
    if (path.size() >= 5u && path.substr(path.size() - 5u) == ".clap")
        path += "/Contents/MacOS/s3g_relay";
#endif
    return path;
}

bool containsMidi(const OutputEvents& output, uint8_t status,
    uint8_t data1)
{
    for (uint32_t index = 0u; index < output.midiCount; ++index) {
        const auto& event = output.midi[index];
        if (event.data[0] == status && event.data[1] == data1) return true;
    }
    return false;
}

#if defined(__APPLE__)
NSView* findViewRespondingTo(NSView* root, SEL selector)
{
    if ([root respondsToSelector:selector]) return root;
    for (NSView* child in root.subviews) {
        if (NSView* match = findViewRespondingTo(child, selector))
            return match;
    }
    return nil;
}

bool exerciseGui(const clap_plugin_t* plugin, const char* capturePath)
{
    const auto* gui = static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI));
    if (!expect(gui != nullptr, "missing Cocoa GUI extension")) return false;
    if (!expect(gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false),
            "Cocoa API is not supported")
        || !expect(gui->create(plugin, CLAP_WINDOW_API_COCOA, false),
            "GUI creation failed")) return false;

    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!expect(gui->get_size(plugin, &width, &height)
            && width >= 900u && height >= 640u,
            "GUI returned an invalid size")) {
        gui->destroy(plugin);
        return false;
    }
    NSView* parent = [[NSView alloc] initWithFrame:NSMakeRect(
        0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height))];
    clap_window_t window {};
    window.api = CLAP_WINDOW_API_COCOA;
    window.cocoa = parent;
    const bool attached = gui->set_parent(plugin, &window);
    const bool shown = attached && gui->show(plugin);
    [parent layoutSubtreeIfNeeded];
    const SEL presetSelector = NSSelectorFromString(@"applyFactoryPreset:");
    NSView* relayView = findViewRespondingTo(parent, presetSelector);
    bool presetApplied = false;
    bool inputChannelMenuOpened = false;
    if (relayView) {
        using ApplyPreset = BOOL (*)(id, SEL, NSInteger);
        auto applyPreset = reinterpret_cast<ApplyPreset>(
            [relayView methodForSelector:presetSelector]);
        presetApplied = applyPreset
            && applyPreset(relayView, presetSelector, 1);
        const SEL menuSelector = NSSelectorFromString(@"openParameterMenu:");
        if ([relayView respondsToSelector:menuSelector]) {
            auto openMenu = reinterpret_cast<ApplyPreset>(
                [relayView methodForSelector:menuSelector]);
            // Global parameter index 23 is Input Channel: Omni plus sixteen
            // channels. Rendering this menu regresses the former 16-slot
            // stack buffer overflow observed in REAPER.
            inputChannelMenuOpened = openMenu
                && openMenu(relayView, menuSelector, 23);
        }
    }
    NSBitmapImageRep* bitmap = [parent bitmapImageRepForCachingDisplayInRect:
        parent.bounds];
    if (bitmap) [parent cacheDisplayInRect:parent.bounds
        toBitmapImageRep:bitmap];
    bool rendered = bitmap && bitmap.pixelsWide > 0
        && bitmap.pixelsHigh > 0 && bitmap.TIFFRepresentation.length > 4096u;
    if (rendered && capturePath && capturePath[0] != '\0') {
        NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG
            properties:@{}];
        rendered = png && [png writeToFile:
            [NSString stringWithUTF8String:capturePath] atomically:YES];
    }
    if (shown) (void)gui->hide(plugin);
    gui->destroy(plugin);
    [parent release];
    return expect(attached && shown && rendered && presetApplied
            && inputChannelMenuOpened,
        "GUI did not render its preset or 17-item input-channel menu");
}
#endif

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
            "usage: %s <s3g_relay.clap> [capture.png]\n", argv[0]);
        return 2;
    }
    const std::string binary = resolveBinary(argv[1]);
    void* handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    const char* loadError = handle ? nullptr : dlerror();
    if (!expect(handle != nullptr, loadError ? loadError : "dlopen failed"))
        return 1;
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry"));
    if (!expect(entry != nullptr, "missing clap_entry")
        || !expect(entry->init(binary.c_str()), "entry init failed")) {
        dlclose(handle);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!expect(factory && factory->get_plugin_count(factory) == 1u,
            "invalid plugin factory")) {
        entry->deinit();
        dlclose(handle);
        return 1;
    }
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0u);
    if (!expect(descriptor
            && std::strcmp(descriptor->id,
                "org.s3g.s3g-dsp.relay") == 0,
            "unexpected descriptor")) {
        entry->deinit();
        dlclose(handle);
        return 1;
    }

    HostContext context;
    context.params.rescan = hostParamsRescan;
    context.params.clear = hostParamsClear;
    context.params.request_flush = hostParamsRequestFlush;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = "Relay Smoke Host";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequestRestart;
    context.host.request_process = hostRequestProcess;
    context.host.request_callback = hostRequestCallback;

    const clap_plugin_t* plugin = factory->create_plugin(factory,
        &context.host, descriptor->id);
    if (!expect(plugin && plugin->init(plugin), "plugin init failed")) {
        entry->deinit();
        dlclose(handle);
        return 1;
    }

    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    clap_note_port_info_t inputPort {};
    clap_note_port_info_t outputPort {};
    bool ok = expect(notePorts
            && notePorts->count(plugin, true) == 1u
            && notePorts->count(plugin, false) == 1u
            && notePorts->get(plugin, 0u, true, &inputPort)
            && notePorts->get(plugin, 0u, false, &outputPort)
            && (inputPort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
            && (outputPort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
            && std::strcmp(inputPort.name, "Ecological Injection") == 0,
        "MIDI input/output port contract failed");

    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    ok &= expect(params && params->count(plugin) == 214u,
        "expected 214 generic neural sequencer parameters");
    clap_param_info_t scaleInfo {};
    ok &= expect(params && params->get_info(plugin, 18u, &scaleInfo)
            && scaleInfo.id == 19u
            && std::strcmp(scaleInfo.name, "Musical Scale") == 0
            && scaleInfo.max_value == 100.0,
        "global musical-scale parameter is missing");
    char scaleText[64] {};
    double parsedScale = -1.0;
    ok &= expect(params
            && params->value_to_text(plugin, 19u, 100.0,
                scaleText, sizeof(scaleText))
            && std::strcmp(scaleText, "BLUES COMPOSITE") == 0
            && params->text_to_value(plugin, 19u, "PENTATONIC MINOR",
                &parsedScale)
            && parsedScale == 31.0,
        "canonical musical-scale text conversion failed");
    clap_param_info_t noteInfo {};
    ok &= expect(params && params->get_info(plugin, 32u, &noteInfo)
            && noteInfo.id == 102u
            && std::strcmp(noteInfo.name, "MIDI Note") == 0,
        "Relay 1 MIDI-note parameter is missing");
    clap_param_info_t pitchModeInfo {};
    ok &= expect(params && params->get_info(plugin, 41u, &pitchModeInfo)
            && pitchModeInfo.id == 111u
            && std::strcmp(pitchModeInfo.name, "Pitch Mode") == 0,
        "Relay 1 pitch-mode parameter is missing");
    clap_param_info_t articulationInfo {};
    ok &= expect(params && params->get_info(plugin, 42u, &articulationInfo)
            && articulationInfo.id == 112u
            && std::strcmp(articulationInfo.name, "Articulation") == 0
            && articulationInfo.max_value == 3.0,
        "Relay 1 articulation parameter is missing");
    char articulationText[32] {};
    double parsedArticulation = -1.0;
    ok &= expect(params
            && params->value_to_text(plugin, 112u, 3.0,
                articulationText, sizeof(articulationText))
            && std::strcmp(articulationText, "Stack") == 0
            && params->text_to_value(plugin, 112u, "Extend",
                &parsedArticulation)
            && parsedArticulation == 2.0,
        "articulation text conversion failed");
    clap_param_info_t latticeDepthInfo {};
    ok &= expect(params && params->get_info(plugin, 20u, &latticeDepthInfo)
            && latticeDepthInfo.id == 21u
            && std::strcmp(latticeDepthInfo.name, "Lattice Depth") == 0
            && latticeDepthInfo.max_value == 2.0,
        "lattice-depth parameter is missing");
    char latticeDepthText[32] {};
    double parsedLatticeDepth = -1.0;
    ok &= expect(params
            && params->value_to_text(plugin, 21u, 2.0,
                latticeDepthText, sizeof(latticeDepthText))
            && std::strcmp(latticeDepthText, "4 Planes") == 0
            && params->text_to_value(plugin, 21u, "2 Planes",
                &parsedLatticeDepth)
            && parsedLatticeDepth == 1.0,
        "lattice-depth text conversion failed");
    clap_param_info_t ccSourceInfo {};
    ok &= expect(params && params->get_info(plugin, 43u, &ccSourceInfo)
            && ccSourceInfo.id == 1000u
            && std::strcmp(ccSourceInfo.name, "CC A Source") == 0
            && ccSourceInfo.max_value == 11.0,
        "Relay 1 CC-source parameter is missing");
    char ccSourceText[32] {};
    double parsedCcSource = -1.0;
    ok &= expect(params
            && params->value_to_text(plugin, 1000u, 5.0,
                ccSourceText, sizeof(ccSourceText))
            && std::strcmp(ccSourceText, "Climate Energy") == 0
            && params->text_to_value(plugin, 1000u, "Form Phase",
                &parsedCcSource)
            && parsedCcSource == 10.0,
        "CC-source text conversion failed");
    char ccCurveText[32] {};
    double parsedCcCurve = -2.0;
    ok &= expect(params
            && params->value_to_text(plugin, 1003u, 0.5,
                ccCurveText, sizeof(ccCurveText))
            && std::strcmp(ccCurveText, "+50%") == 0
            && params->text_to_value(plugin, 1003u, "+50%",
                &parsedCcCurve)
            && std::abs(parsedCcCurve - 0.5) < 1.0e-12,
        "CC-curve percentage conversion failed");
    clap_param_info_t dealerLawInfo {};
    char dealerLawText[32] {};
    double parsedDealerLaw = -1.0;
    ok &= expect(params && params->get_info(plugin, 21u, &dealerLawInfo)
            && dealerLawInfo.id == 22u
            && std::strcmp(dealerLawInfo.name, "Dealer Law") == 0
            && params->value_to_text(plugin, 22u, 4.0,
                dealerLawText, sizeof(dealerLawText))
            && std::strcmp(dealerLawText, "Climate Contrast") == 0
            && params->text_to_value(plugin, 22u, "Avoid Recent",
                &parsedDealerLaw)
            && parsedDealerLaw == 3.0,
        "dealer-law parameter/text conversion failed");
    clap_param_info_t inputModeInfo {};
    char inputModeText[32] {};
    double parsedInputMode = -1.0;
    ok &= expect(params && params->get_info(plugin, 22u, &inputModeInfo)
            && inputModeInfo.id == 23u
            && std::strcmp(inputModeInfo.name, "Input Source") == 0
            && params->value_to_text(plugin, 23u, 3.0,
                inputModeText, sizeof(inputModeText))
            && std::strcmp(inputModeText, "Notes + CC") == 0
            && params->text_to_value(plugin, 23u, "Notes", &parsedInputMode)
            && parsedInputMode == 1.0,
        "ecological-input parameter/text conversion failed");

    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    ok &= expect(state != nullptr, "state extension is missing");
    ok &= expect(plugin->activate(plugin, 48000.0, 1u, 512u),
        "activation failed");
    ok &= expect(plugin->start_processing(plugin), "start failed");

    InputEvents input;
    input.add(2u, 1.0);   // activity
    input.add(4u, 0.0);   // fresh comparator data
    input.add(5u, 0.0);   // no stochastic bit flips
    input.add(9u, 6.0);   // 16 comparator ticks / beat
    input.add(10u, 2.0);  // hold an emitted note until transport stop
    input.add(23u, 3.0);  // notes + CC ecological injection
    input.add(29u, 1.0);  // full input depth
    input.add(101u, 4.0); // MIDI channel 4
    input.add(102u, 64.0);
    input.add(103u, 74.0);
    input.add(104u, 71.0);
    input.add(105u, 1.0); // selective threshold produces register edges
    input.add(106u, 1.0); // positive receptor bias
    input.add(116u, 0.0);
    input.add(132u, 0.0);
    input.add(148u, 0.0);
    input.add(164u, 0.0);
    input.add(180u, 0.0);
    input.add(196u, 0.0);
    input.add(212u, 0.0);
    OutputEvents output;
    clap_event_transport_t transport {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_IS_PLAYING
        | CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
        | CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
    transport.tempo = 120.0;
    transport.song_pos_beats = 0;
    transport.tsig_num = 4u;
    transport.tsig_denom = 4u;
    clap_process_t process {};
    process.steady_time = 0;
    process.frames_count = 512u;
    process.transport = &transport;
    process.in_events = &input.interface;
    process.out_events = &output.interface;
    bool noteEmitted = false;
    constexpr uint64_t kFramesPerBlock = 512u;
    for (uint64_t block = 0u; block < 180u && !noteEmitted; ++block) {
        transport.song_pos_beats = static_cast<clap_beattime>(std::llround(
            static_cast<double>(block * kFramesPerBlock) * 120.0
                / (60.0 * 48000.0)
                * static_cast<double>(CLAP_BEATTIME_FACTOR)));
        process.steady_time = static_cast<int64_t>(block * kFramesPerBlock);
        ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            "process failed");
        noteEmitted = containsMidi(output, 0x93u, 64u);
        input.count = 0u;
    }
    ok &= expect(noteEmitted, "assigned note/channel was not emitted");
    ok &= expect(containsMidi(output, 0xb3u, 74u)
            && containsMidi(output, 0xb3u, 71u),
        "assigned signed-state CC pair was not emitted");

    MidiInputEvents ecologicalInput;
    ecologicalInput.add(64u, 0x92u, 99u, 120u);
    ecologicalInput.add(192u, 0xb2u, 1u, 127u);
    ecologicalInput.add(320u, 0x82u, 99u, 0u);
    OutputEvents ecologicalOutput;
    transport.song_pos_beats += static_cast<clap_beattime>(std::llround(
        120.0 * 512.0 / (60.0 * 48000.0)
            * static_cast<double>(CLAP_BEATTIME_FACTOR)));
    process.steady_time += 512;
    process.in_events = &ecologicalInput.interface;
    process.out_events = &ecologicalOutput.interface;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE
            && !containsMidi(ecologicalOutput, 0x92u, 99u),
        "ecological MIDI input was rejected or passed through directly");

    StateBuffer saved;
    ok &= expect(state->save(plugin, &saved.output) && !saved.bytes.empty(),
        "state save failed");
    InputEvents mutate;
    mutate.add(102u, 65.0);
    OutputEvents flushOutput;
    params->flush(plugin, &mutate.interface, &flushOutput.interface);
    double changedNote = 0.0;
    ok &= expect(params->get_value(plugin, 102u, &changedNote)
            && changedNote == 65.0,
        "parameter mutation failed");
    ok &= expect(state->load(plugin, &saved.input), "state load failed");
    double restoredNote = 0.0;
    ok &= expect(params->get_value(plugin, 102u, &restoredNote)
            && restoredNote == 64.0,
        "state did not restore MIDI mapping");

    // Crystallize is a compound runtime state rather than a host parameter.
    // Verify that its thaw memory and held form instant survive a state round
    // trip alongside the parameter array.
    StateBuffer crystallized;
    crystallized.bytes = saved.bytes;
    constexpr std::size_t kRuntimeOffset = sizeof(uint32_t) * 2u
        + sizeof(double) * 214u;
    const double savedThawMemory = 0.37;
    const double savedFormBeat = 47.25;
    const uint32_t savedRuntimeFlags = 3u;
    std::memcpy(crystallized.bytes.data() + kRuntimeOffset,
        &savedThawMemory, sizeof(savedThawMemory));
    std::memcpy(crystallized.bytes.data() + kRuntimeOffset
            + sizeof(double),
        &savedFormBeat, sizeof(savedFormBeat));
    std::memcpy(crystallized.bytes.data() + kRuntimeOffset
            + sizeof(double) * 2u,
        &savedRuntimeFlags, sizeof(savedRuntimeFlags));
    ok &= expect(state->load(plugin, &crystallized.input),
        "crystallized Relay state did not load");
    StateBuffer crystallizedRoundTrip;
    ok &= expect(state->save(plugin, &crystallizedRoundTrip.output),
        "crystallized Relay state did not save");
    double roundTripThawMemory = 0.0;
    double roundTripFormBeat = 0.0;
    uint32_t roundTripRuntimeFlags = 0u;
    std::memcpy(&roundTripThawMemory,
        crystallizedRoundTrip.bytes.data() + kRuntimeOffset,
        sizeof(roundTripThawMemory));
    std::memcpy(&roundTripFormBeat,
        crystallizedRoundTrip.bytes.data() + kRuntimeOffset
            + sizeof(double), sizeof(roundTripFormBeat));
    std::memcpy(&roundTripRuntimeFlags,
        crystallizedRoundTrip.bytes.data() + kRuntimeOffset
            + sizeof(double) * 2u, sizeof(roundTripRuntimeFlags));
    ok &= expect(std::abs(roundTripThawMemory - savedThawMemory) < 1.0e-12
            && std::abs(roundTripFormBeat - savedFormBeat) < 1.0e-12
            && roundTripRuntimeFlags == savedRuntimeFlags,
        "crystallized Relay runtime state did not round trip");
    saved.cursor = 0u;
    ok &= expect(state->load(plugin, &saved.input),
        "ordinary Relay state did not restore after Crystallize test");

    OutputEvents stoppedOutput;
    InputEvents noInput;
    transport.flags &= ~CLAP_TRANSPORT_IS_PLAYING;
    transport.song_pos_beats += static_cast<clap_beattime>(std::llround(
        120.0 * 512.0 / (60.0 * 48000.0)
            * static_cast<double>(CLAP_BEATTIME_FACTOR)));
    process.in_events = &noInput.interface;
    process.out_events = &stoppedOutput.interface;
    ok &= expect(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
        "stopped process failed");
    ok &= expect(containsMidi(stoppedOutput, 0x83u, 64u),
        "transport stop did not release the active note");

    StateBuffer legacy;
    constexpr uint32_t kLegacyMagic = 0x52454c59u;
    constexpr uint32_t kLegacyVersion = 1u;
    const uint32_t legacyHeader[2] { kLegacyMagic, kLegacyVersion };
    std::array<double, 104u> legacyValues {};
    legacyValues[0u] = 1.0;
    legacyValues[15u] = 1977.0;
    legacyValues[16u] = 1.0;
    legacyValues[17u] = 4.0;
    legacyValues[18u] = 67.0;
    StateBuffer::write(&legacy.output, legacyHeader, sizeof(legacyHeader));
    StateBuffer::write(&legacy.output, legacyValues.data(),
        sizeof(legacyValues));
    ok &= expect(state->load(plugin, &legacy.input),
        "legacy Relay v1 state did not migrate");
    double migratedNote = 0.0;
    double migratedScaleRoot = -1.0;
    double migratedPitchMode = -1.0;
    double migratedArticulation = -1.0;
    ok &= expect(params->get_value(plugin, 102u, &migratedNote)
            && migratedNote == 67.0
            && params->get_value(plugin, 17u, &migratedScaleRoot)
            && migratedScaleRoot == 0.0
            && params->get_value(plugin, 111u, &migratedPitchMode)
            && migratedPitchMode == 0.0
            && params->get_value(plugin, 112u, &migratedArticulation)
            && migratedArticulation == 0.0,
        "legacy Relay v1 state did not preserve fixed-note behavior");

    StateBuffer version3;
    constexpr uint32_t kVersion3 = 3u;
    const uint32_t version3Header[2] { kLegacyMagic, kVersion3 };
    std::array<double, 116u> version3Values {};
    version3Values[18u] = 31.0;
    version3Values[31u] = 1.0; // Relay 1 Scale Logic.
    StateBuffer::write(&version3.output, version3Header,
        sizeof(version3Header));
    StateBuffer::write(&version3.output, version3Values.data(),
        sizeof(version3Values));
    ok &= expect(state->load(plugin, &version3.input),
        "Relay v3 state did not migrate");
    double version3PitchMode = -1.0;
    double version3Articulation = -1.0;
    ok &= expect(params->get_value(plugin, 111u, &version3PitchMode)
            && version3PitchMode == 1.0
            && params->get_value(plugin, 112u, &version3Articulation)
            && version3Articulation == 0.0,
        "Relay v3 state did not default articulation to Restart");

    StateBuffer version4;
    constexpr uint32_t kVersion4 = 4u;
    const uint32_t version4Header[2] { kLegacyMagic, kVersion4 };
    std::array<double, 124u> version4Values {};
    version4Values[18u] = 31.0;
    version4Values[32u] = 3.0; // Relay 1 Stack.
    StateBuffer::write(&version4.output, version4Header,
        sizeof(version4Header));
    StateBuffer::write(&version4.output, version4Values.data(),
        sizeof(version4Values));
    ok &= expect(state->load(plugin, &version4.input),
        "Relay v4 state did not migrate");
    double version4Articulation = -1.0;
    double version4LatticeDepth = -1.0;
    ok &= expect(params->get_value(plugin, 112u, &version4Articulation)
            && version4Articulation == 3.0
            && params->get_value(plugin, 21u, &version4LatticeDepth)
            && version4LatticeDepth == 2.0,
        "Relay v4 state did not retain articulation/default lattice depth");

    StateBuffer version6;
    constexpr uint32_t kVersion6 = 6u;
    const uint32_t version6Header[2] { kLegacyMagic, kVersion6 };
    std::array<double, 205u> version6Values {};
    version6Values[20u] = 2.0;
    version6Values[33u] = 3.0; // Relay 1 Stack.
    version6Values[34u] = 5.0; // Relay 1 Climate Energy CC source.
    const double version6ThawMemory = 0.41;
    const double version6FormBeat = 31.5;
    const uint32_t version6Flags[2] { 3u, 0u };
    StateBuffer::write(&version6.output, version6Header,
        sizeof(version6Header));
    StateBuffer::write(&version6.output, version6Values.data(),
        sizeof(version6Values));
    StateBuffer::write(&version6.output, &version6ThawMemory,
        sizeof(version6ThawMemory));
    StateBuffer::write(&version6.output, &version6FormBeat,
        sizeof(version6FormBeat));
    StateBuffer::write(&version6.output, version6Flags,
        sizeof(version6Flags));
    ok &= expect(state->load(plugin, &version6.input),
        "Relay v6 state did not migrate");
    double version6Articulation = -1.0;
    double version6CcSource = -1.0;
    double version6DealerLaw = -1.0;
    double version6InputMode = -1.0;
    ok &= expect(params->get_value(plugin, 112u, &version6Articulation)
            && version6Articulation == 3.0
            && params->get_value(plugin, 1000u, &version6CcSource)
            && version6CcSource == 5.0
            && params->get_value(plugin, 22u, &version6DealerLaw)
            && version6DealerLaw == 0.0
            && params->get_value(plugin, 23u, &version6InputMode)
            && version6InputMode == 0.0,
        "Relay v6 state did not retain behavior/default new controls");

    StateBuffer version5;
    constexpr uint32_t kVersion5 = 5u;
    const uint32_t version5Header[2] { kLegacyMagic, kVersion5 };
    std::array<double, 125u> version5Values {};
    version5Values[20u] = 1.0; // Two-plane lattice.
    version5Values[33u] = 2.0; // Relay 1 Extend.
    StateBuffer::write(&version5.output, version5Header,
        sizeof(version5Header));
    StateBuffer::write(&version5.output, version5Values.data(),
        sizeof(version5Values));
    ok &= expect(state->load(plugin, &version5.input),
        "Relay v5 state did not migrate");
    double version5Articulation = -1.0;
    double version5LatticeDepth = -1.0;
    double version5CcASource = -1.0;
    double version5CcBSource = -1.0;
    ok &= expect(params->get_value(plugin, 112u, &version5Articulation)
            && version5Articulation == 2.0
            && params->get_value(plugin, 21u, &version5LatticeDepth)
            && version5LatticeDepth == 1.0
            && params->get_value(plugin, 1000u, &version5CcASource)
            && version5CcASource == 0.0
            && params->get_value(plugin, 1005u, &version5CcBSource)
            && version5CcBSource == 4.0,
        "Relay v5 state did not retain behavior/default CC sources");

    StateBuffer version2;
    constexpr uint32_t kVersion2 = 2u;
    const uint32_t version2Header[2] { kLegacyMagic, kVersion2 };
    std::array<double, 116u> version2Values {};
    version2Values[18u] = 9.0; // Old OCTATONIC scale ID.
    StateBuffer::write(&version2.output, version2Header,
        sizeof(version2Header));
    StateBuffer::write(&version2.output, version2Values.data(),
        sizeof(version2Values));
    ok &= expect(state->load(plugin, &version2.input),
        "Relay v2 state did not migrate");
    double migratedScale = -1.0;
    ok &= expect(params->get_value(plugin, 19u, &migratedScale)
            && migratedScale == 45.0,
        "Relay v2 octatonic scale did not map to DIMINISHED HALF-WHOLE");
    saved.cursor = 0u;
    ok &= expect(state->load(plugin, &saved.input),
        "current Relay state did not restore after migration test");

#if defined(__APPLE__)
    [NSApplication sharedApplication];
    ok &= exerciseGui(plugin, argc >= 3 ? argv[2] : nullptr);
    InputEvents presetFlushInput;
    OutputEvents presetFlushOutput;
    params->flush(plugin, &presetFlushInput.interface,
        &presetFlushOutput.interface);
    double presetEnergy = -1.0;
    double presetDealerLaw = -1.0;
    double presetInputMode = -1.0;
    double presetNote = -1.0;
    ok &= expect(params->get_value(plugin, 2u, &presetEnergy)
            && std::abs(presetEnergy - 0.74) < 1.0e-9
            && params->get_value(plugin, 22u, &presetDealerLaw)
            && presetDealerLaw == 1.0
            && params->get_value(plugin, 23u, &presetInputMode)
            && presetInputMode == 0.0
            && params->get_value(plugin, 102u, &presetNote)
            && presetNote == 64.0,
        "factory preset transaction was incomplete after GUI flush");
#endif

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);
    if (!ok) return 1;
    std::puts("Relay CLAP smoke passed");
    return 0;
}
