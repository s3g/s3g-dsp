#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kFrames = 10u;
constexpr clap_id kFeedbackParameter = 5u;
constexpr uint16_t kMatrixRampParameter = 59u;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

struct InputEvents {
    std::vector<clap_event_midi_t> midi;
    std::vector<clap_event_param_value_t> params;
    std::vector<const clap_event_header_t*> order;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const InputEvents*>(list->ctx);
            return self ? static_cast<uint32_t>(self->order.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const InputEvents*>(list->ctx);
            return self && index < self->order.size()
                ? self->order[index] : nullptr;
        },
    };

    InputEvents()
    {
        midi.reserve(128u);
        params.reserve(16u);
        order.reserve(144u);
    }

    void addMidi(uint8_t status, uint8_t dataOne, uint8_t dataTwo,
        uint32_t time = 0u)
    {
        clap_event_midi_t event {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0] = status;
        event.data[1] = dataOne;
        event.data[2] = dataTwo;
        midi.push_back(event);
        order.push_back(&midi.back().header);
    }

    void addNrpn(uint16_t id, uint16_t value, uint32_t time,
        uint8_t channel = 15u)
    {
        const uint8_t status = static_cast<uint8_t>(0xb0u | channel);
        addMidi(status, 99u, static_cast<uint8_t>((id >> 7u) & 0x7fu), time);
        addMidi(status, 98u, static_cast<uint8_t>(id & 0x7fu), time);
        addMidi(status, 6u, static_cast<uint8_t>((value >> 7u) & 0x7fu), time);
        addMidi(status, 38u, static_cast<uint8_t>(value & 0x7fu), time);
    }

    void addParam(clap_id id, double value, uint32_t time = 0u)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        params.push_back(event);
        order.push_back(&params.back().header);
    }
};

struct OutputEvents {
    std::vector<clap_event_midi_t> midi;
    std::vector<clap_event_param_value_t> params;
    clap_output_events_t output {
        this,
        [](const clap_output_events_t* list,
            const clap_event_header_t* event) -> bool {
            auto* self = static_cast<OutputEvents*>(list->ctx);
            if (!self || !event
                || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
            if (event->type == CLAP_EVENT_MIDI
                && event->size >= sizeof(clap_event_midi_t)) {
                self->midi.push_back(
                    *reinterpret_cast<const clap_event_midi_t*>(event));
            } else if (event->type == CLAP_EVENT_PARAM_VALUE
                && event->size >= sizeof(clap_event_param_value_t)) {
                self->params.push_back(
                    *reinterpret_cast<const clap_event_param_value_t*>(event));
            }
            return true;
        },
    };

    void clear()
    {
        midi.clear();
        params.clear();
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(requested, 11u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({ available,
        static_cast<size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

std::vector<std::pair<uint32_t, uint16_t>> decodeNrpn(
    const std::vector<clap_event_midi_t>& midi)
{
    std::vector<std::pair<uint32_t, uint16_t>> values;
    uint16_t parameter = 0u;
    uint16_t value = 0u;
    bool hasMsb = false;
    for (const auto& event : midi) {
        if ((event.data[0] & 0xf0u) != 0xb0u) continue;
        switch (event.data[1] & 0x7fu) {
        case 99u:
            parameter = static_cast<uint16_t>((event.data[2] & 0x7fu) << 7u);
            break;
        case 98u:
            parameter = static_cast<uint16_t>(parameter | (event.data[2] & 0x7fu));
            break;
        case 6u:
            value = static_cast<uint16_t>((event.data[2] & 0x7fu) << 7u);
            hasMsb = true;
            break;
        case 38u:
            if (hasMsb) {
                value = static_cast<uint16_t>(value | (event.data[2] & 0x7fu));
                values.emplace_back(event.header.time, value);
                hasMsb = false;
            }
            break;
        default:
            break;
        }
    }
    return values;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_nim_gesture_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve NIM Gesture binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load NIM Gesture: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "NIM Gesture smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.nim-gesture") : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* ports = ok ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    clap_note_port_info_t inputPort {};
    clap_note_port_info_t outputPort {};
    ok = ok && ports && params && state
        && ports->count(plugin, true) == 1u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, true, &inputPort)
        && ports->get(plugin, 0u, false, &outputPort)
        && (inputPort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && (outputPort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && params->count(plugin) == 7u
        && plugin->activate(plugin, 1000.0, kFrames, kFrames)
        && plugin->start_processing(plugin);
    if (!ok) std::cerr << "failed: setup and ports\n";

    clap_process_t process {};
    process.frames_count = kFrames;
    OutputEvents output;
    process.out_events = &output.output;

    InputEvents live;
    live.addNrpn(kFeedbackParameter, 8193u, 3u);
    process.in_events = &live.input;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    auto decoded = decodeNrpn(output.midi);
    ok = ok && output.midi.size() == 4u && decoded.size() == 1u
        && decoded[0].first == 3u && decoded[0].second == 8193u
        && (output.midi[0].header.flags & CLAP_EVENT_IS_LIVE) != 0u;
    if (!ok) std::cerr << "failed: canonical live NRPN\n";

    output.clear();
    InputEvents wrongChannel;
    wrongChannel.addNrpn(kFeedbackParameter, 1234u, 1u, 14u);
    process.in_events = &wrongChannel.input;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE
        && output.midi.size() == 4u;
    if (!ok) std::cerr << "failed: non-control-channel passthrough\n";

    output.clear();
    InputEvents record;
    record.addMidi(0x9fu, 112u, 127u, 0u);
    record.addNrpn(kMatrixRampParameter, 100u, 2u);
    record.addNrpn(kMatrixRampParameter, 200u, 8u);
    process.in_events = &record.input;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    double recordingValue = 0.0;
    ok = ok && params->get_value(plugin, 1u, &recordingValue)
        && recordingValue == 1.0;
    if (!ok) std::cerr << "failed: record start and capture\n";

    output.clear();
    InputEvents stop;
    stop.addMidi(0x9fu, 112u, 127u, 0u);
    process.in_events = &stop.input;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    decoded = decodeNrpn(output.midi);
    double loopCount = 0.0;
    double lastLength = 0.0;
    ok = ok && decoded.size() == 2u
        && decoded[0] == std::make_pair(2u, static_cast<uint16_t>(100u))
        && decoded[1] == std::make_pair(8u, static_cast<uint16_t>(200u))
        && params->get_value(plugin, 1u, &recordingValue)
        && recordingValue == 0.0
        && params->get_value(plugin, 6u, &loopCount) && loopCount == 1.0
        && params->get_value(plugin, 7u, &lastLength)
        && std::abs(lastLength - 0.01) < 1.0e-9;
    if (!ok) std::cerr << "failed: free-loop commit and first cycle\n";

    output.clear();
    process.in_events = nullptr;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    decoded = decodeNrpn(output.midi);
    ok = ok && decoded.size() == 2u
        && decoded[0] == std::make_pair(2u, static_cast<uint16_t>(100u))
        && decoded[1] == std::make_pair(8u, static_cast<uint16_t>(200u));
    if (!ok) std::cerr << "failed: repeating free loop\n";

    MemoryState saved;
    clap_ostream_t ostream { &saved, stateWrite };
    ok = ok && state->save(plugin, &ostream) && !saved.bytes.empty();

    output.clear();
    InputEvents clearLast;
    clearLast.addMidi(0x9fu, 114u, 127u, 0u);
    process.in_events = &clearLast.input;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE
        && output.midi.empty()
        && params->get_value(plugin, 6u, &loopCount) && loopCount == 0.0;
    if (!ok) std::cerr << "failed: clear-last command\n";

    saved.offset = 0u;
    clap_istream_t istream { &saved, stateRead };
    ok = ok && state->load(plugin, &istream);
    output.clear();
    process.in_events = nullptr;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    decoded = decodeNrpn(output.midi);
    ok = ok && decoded.size() == 2u
        && decoded[0] == std::make_pair(2u, static_cast<uint16_t>(100u))
        && decoded[1] == std::make_pair(8u, static_cast<uint16_t>(200u));
    if (!ok) std::cerr << "failed: state restore\n";

    output.clear();
    InputEvents pause;
    pause.addMidi(0x9fu, 113u, 127u, 0u);
    process.in_events = &pause.input;
    ok = ok && plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
    double playbackValue = 1.0;
    ok = ok && params->get_value(plugin, 2u, &playbackValue)
        && playbackValue == 0.0 && output.midi.empty();
    if (!ok) std::cerr << "failed: playback command\n";

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) return 1;
    std::cout << "NIM Gesture CLAP smoke passed\n";
    return 0;
}
