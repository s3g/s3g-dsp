#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

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

constexpr uint32_t kChannels = 16u;
constexpr uint32_t kFrames = 256u;
constexpr double kSampleRate = 48000.0;
constexpr clap_id kOrderParamId = 1u;
constexpr clap_id kSpeedParamId = 2u;
constexpr clap_id kActuatorNodeParamId = 8u;
constexpr clap_id kOutputGainParamId = 11u;

struct HostContext {
    clap_host_t host {};
    clap_host_tail_t tail {};
    uint32_t tailChanges = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (id && std::strcmp(id, CLAP_EXT_TAIL) == 0) {
        return &hostContext(host)->tail;
    }
    return nullptr;
}

void hostRequest(const clap_host_t*) {}

void hostTailChanged(const clap_host_t* host)
{
    ++hostContext(host)->tailChanges;
}

struct EventList {
    std::vector<const clap_event_header_t*> events;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self =
                static_cast<const EventList*>(list->ctx);
            return self
                ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self =
                static_cast<const EventList*>(list->ctx);
            return self && index < self->events.size()
                ? self->events[index] : nullptr;
        },
    };
    std::array<clap_event_param_value_t, 8u> params {};
    std::array<clap_event_note_t, 8u> notes {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;

    void addParam(clap_id id, double value, uint32_t time = 0u)
    {
        auto& event = params[paramCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events.push_back(&event.header);
    }

    void addNote(int16_t key, double velocity, uint32_t time)
    {
        auto& event = notes[noteCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = key;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        events.push_back(&event.header);
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
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 9u);
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
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry :
             std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

struct AudioBlock {
    std::array<float, kFrames> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, 1u> inputPointers {};
    std::array<float*, kChannels> outputPointers {};
    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};

    AudioBlock()
    {
        inputPointers[0] = input.data();
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            outputPointers[channel] = output[channel].data();
        }
        inputBuffer.data32 = inputPointers.data();
        inputBuffer.channel_count = 1u;
        outputBuffer.data32 = outputPointers.data();
        outputBuffer.channel_count = kChannels;
    }

    void clear()
    {
        input.fill(0.0f);
        for (auto& channel : output) channel.fill(0.0f);
    }
};

clap_process_status runBlock(const clap_plugin_t* plugin,
    AudioBlock& audio, const clap_input_events_t* events)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &audio.inputBuffer;
    process.audio_outputs = &audio.outputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process);
}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double expected)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value)
        && std::abs(value - expected) <= 1.0e-6;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_fractional_waveguide_network_clap_smoke "
            << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve waveguide encoder binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load waveguide encoder: "
            << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    HostContext hostContext {};
    hostContext.tail.changed = hostTailChanged;
    hostContext.host.clap_version = CLAP_VERSION_INIT;
    hostContext.host.host_data = &hostContext;
    hostContext.host.name = "Waveguide encoder smoke";
    hostContext.host.vendor = "s3g";
    hostContext.host.url = "https://github.com/s3g/s3g-dsp";
    hostContext.host.version = "1";
    hostContext.host.get_extension = hostGetExtension;
    hostContext.host.request_restart = hostRequest;
    hostContext.host.request_process = hostRequest;
    hostContext.host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(
            factory, &hostContext.host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* audioPorts = ok
        ? static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS))
        : nullptr;
    const auto* notePorts = ok
        ? static_cast<const clap_plugin_note_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS))
        : nullptr;
    const auto* params = ok
        ? static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS))
        : nullptr;
    const auto* state = ok
        ? static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE))
        : nullptr;
    const auto* tail = ok
        ? static_cast<const clap_plugin_tail_t*>(
            plugin->get_extension(plugin, CLAP_EXT_TAIL))
        : nullptr;
    clap_audio_port_info_t inputInfo {};
    clap_audio_port_info_t outputInfo {};
    clap_note_port_info_t noteInfo {};
    ok = ok && audioPorts && notePorts && params && state && tail
        && audioPorts->count(plugin, true) == 1u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, true, &inputInfo)
        && inputInfo.channel_count == 1u
        && inputInfo.port_type
            && std::strcmp(inputInfo.port_type, CLAP_PORT_MONO) == 0
        && audioPorts->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && outputInfo.port_type
            && std::strcmp(outputInfo.port_type, CLAP_PORT_AMBISONIC) == 0
        && notePorts->count(plugin, true) == 1u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && params->count(plugin) == 11u;

    if (ok) {
        clap_param_info_t order {};
        ok = params->get_info(plugin, 0u, &order)
            && order.id == kOrderParamId
            && (order.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && order.min_value == 1.0 && order.max_value == 3.0;
    }
    ok = ok
        && plugin->activate(plugin, kSampleRate, kFrames, kFrames)
        && plugin->start_processing(plugin)
        && tail->get(plugin) > kFrames;

    AudioBlock audio;
    if (ok) {
        audio.clear();
        EventList events;
        events.addNote(62, 0.8, 17u);
        ok = runBlock(plugin, audio, &events.input)
            == CLAP_PROCESS_CONTINUE;
        double before = 0.0;
        double after = 0.0;
        double higherOrder = 0.0;
        float peak = 0.0f;
        bool finite = true;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float sample = audio.output[channel][frame];
                finite = finite && std::isfinite(sample);
                peak = std::max(peak, std::abs(sample));
                if (frame < 17u) before += std::abs(sample);
                else after += std::abs(sample);
                if (channel >= 4u) higherOrder += std::abs(sample);
            }
        }
        ok = finite && before < 1.0e-8
            && after > 1.0e-4 && higherOrder > 1.0e-5
            && peak <= 0.892f;
    }

    if (ok) {
        plugin->reset(plugin);
        audio.clear();
        EventList events;
        events.addParam(kOrderParamId, 1.0);
        events.addNote(60, 0.8, 0u);
        ok = runBlock(plugin, audio, &events.input)
            == CLAP_PROCESS_CONTINUE;
        double firstOrder = 0.0;
        double inactive = 0.0;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (float sample : audio.output[channel]) {
                if (channel < 4u) firstOrder += std::abs(sample);
                else inactive += std::abs(sample);
            }
        }
        ok = firstOrder > 1.0e-5 && inactive < 1.0e-8;
    }

    MemoryState saved;
    if (ok) {
        EventList changes;
        changes.addParam(kSpeedParamId, 271.5);
        changes.addParam(kActuatorNodeParamId, 6.0);
        changes.addParam(kOutputGainParamId, -18.0);
        params->flush(plugin, &changes.input, nullptr);
        clap_ostream_t stream { &saved, stateWrite };
        ok = state->save(plugin, &stream)
            && saved.bytes.size() > 80u
            && getParam(plugin, params, kSpeedParamId, 271.5)
            && getParam(plugin, params, kActuatorNodeParamId, 6.0)
            && getParam(plugin, params, kOutputGainParamId, -18.0)
            && hostContext.tailChanges > 0u;
    }
    if (ok) {
        EventList changes;
        changes.addParam(kSpeedParamId, 800.0);
        changes.addParam(kActuatorNodeParamId, 2.0);
        params->flush(plugin, &changes.input, nullptr);
        MemoryState input = saved;
        clap_istream_t stream { &input, stateRead };
        ok = state->load(plugin, &stream)
            && getParam(plugin, params, kSpeedParamId, 271.5)
            && getParam(plugin, params, kActuatorNodeParamId, 6.0)
            && getParam(plugin, params, kOutputGainParamId, -18.0);
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Fractional waveguide encoder CLAP smoke failed\n";
        return 1;
    }
    std::cout << "Fractional waveguide encoder CLAP smoke passed\n";
    return 0;
}
