#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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

constexpr const char* kPluginId = "org.s3g.s3g-dsp.feedback-shift";
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 8u;
constexpr uint32_t kParamCount = 219u;

struct HostContext {
    clap_host_t host {};
    uint32_t processRequests = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t* host)
{
    ++hostContext(host)->processRequests;
}
void hostRequestCallback(const clap_host_t*) {}

struct EventList {
    std::array<clap_event_param_value_t, 8u> params {};
    std::array<clap_event_note_t, 4u> notes {};
    std::array<const clap_event_header_t*, 12u> events {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t eventCount = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const EventList*>(list->ctx)->eventCount;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* source = static_cast<const EventList*>(list->ctx);
            return index < source->eventCount ? source->events[index] : nullptr;
        },
    };

    void insert(const clap_event_header_t* event)
    {
        uint32_t index = eventCount;
        while (index > 0u && events[index - 1u]->time > event->time) {
            events[index] = events[index - 1u];
            --index;
        }
        events[index] = event;
        ++eventCount;
    }

    void addParam(clap_id id, double value, uint32_t time = 0u)
    {
        auto& event = params[paramCount++];
        event = {};
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
        insert(&event.header);
    }

    void addNote(int16_t channel, double velocity, uint32_t time = 0u)
    {
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = -1;
        event.port_index = 0;
        event.channel = channel;
        event.key = 60;
        event.velocity = velocity;
        insert(&event.header);
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t position = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 7u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const size_t available = state->position < state->bytes.size()
        ? state->bytes.size() - state->position : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 5u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->position, count);
        state->position += count;
    }
    return static_cast<int64_t>(count);
}

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

bool hasFeature(const clap_plugin_descriptor_t* descriptor,
    const char* expected)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* feature = descriptor->features; *feature; ++feature) {
        if (std::strcmp(*feature, expected) == 0) return true;
    }
    return false;
}

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

double getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value) ? value : -99999.0;
}

double processBlocks(const clap_plugin_t* plugin,
    EventList* firstEvents, uint32_t blockCount,
    std::array<double, kChannels>* channelEnergy = nullptr)
{
    std::array<std::array<float, kFrames>, kChannels> samples {};
    std::array<float*, kChannels> channels {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        channels[channel] = samples[channel].data();
    }
    clap_audio_buffer_t output {};
    output.data32 = channels.data();
    output.channel_count = kChannels;
    clap_process_t context {};
    context.frames_count = kFrames;
    context.audio_outputs = &output;
    context.audio_outputs_count = 1u;
    double energy = 0.0;
    for (uint32_t block = 0u; block < blockCount; ++block) {
        for (auto& channel : samples) channel.fill(0.0f);
        context.in_events = block == 0u && firstEvents
            ? &firstEvents->input : nullptr;
        if (plugin->process(plugin, &context) != CLAP_PROCESS_CONTINUE) {
            return -1.0;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float sample = samples[channel][frame];
                if (!std::isfinite(sample) || std::abs(sample) > 1.0f) {
                    return -1.0;
                }
                const double squared = static_cast<double>(sample) * sample;
                energy += squared;
                if (channelEnergy) (*channelEnergy)[channel] += squared;
            }
        }
    }
    return energy;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: feedback_shift_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    bool ok = true;
    const auto binary = resolveBinary(argv[1]);
    ok &= check(!binary.empty(), "could not resolve CLAP binary");
    void* handle = binary.empty()
        ? nullptr : dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    ok &= check(handle != nullptr, "could not load CLAP binary");
    if (!handle) return 1;
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry"));
    ok &= check(entry != nullptr && entry->init(nullptr),
        "CLAP entry initialization failed");
    if (!ok) {
        dlclose(handle);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    ok &= check(factory && factory->get_plugin_count(factory) == 1u,
        "plugin factory contract failed");
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok &= check(descriptor && std::strcmp(descriptor->id, kPluginId) == 0
        && std::strcmp(descriptor->name,
            "s3g Processor Feedback Shift") == 0,
        "plugin identity failed");
    ok &= check(hasFeature(descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SYNTHESIZER)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SURROUND),
        "instrument feature tags failed");

    HostContext host;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "s3g feedback shift smoke";
    host.host.vendor = "s3g";
    host.host.url = "https://github.com/s3g/s3g-dsp";
    host.host.version = "1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = hostRequestRestart;
    host.host.request_process = hostRequestProcess;
    host.host.request_callback = hostRequestCallback;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host.host, kPluginId) : nullptr;
    ok &= check(plugin && plugin->init(plugin), "plugin creation failed");
    if (!plugin) {
        entry->deinit();
        dlclose(handle);
        return 1;
    }

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    clap_audio_port_info_t outputInfo {};
    ok &= check(audioPorts && audioPorts->count(plugin, true) == 0u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && outputInfo.port_type
        && std::strcmp(outputInfo.port_type, CLAP_PORT_SURROUND) == 0,
        "zero-input eight-channel audio port contract failed");
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    clap_note_port_info_t noteInfo {};
    ok &= check(notePorts && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u,
        "MIDI excitation port contract failed");
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    ok &= check(params && params->count(plugin) == kParamCount,
        "parameter surface failed");
    if (params) {
        clap_param_info_t pulseSync {};
        clap_param_info_t nodeMode {};
        clap_param_info_t pedalAmount {};
        clap_param_info_t pedalExtra {};
        clap_param_info_t run {};
        clap_param_info_t outputMode {};
        clap_param_info_t outputRotation {};
        ok &= check(params->get_info(plugin, 4u, &pulseSync)
            && params->get_info(plugin, 8u, &run)
            && params->get_info(plugin, 9u, &outputMode)
            && params->get_info(plugin, 10u, &outputRotation)
            && params->get_info(plugin, 11u, &nodeMode)
            && params->get_info(plugin, 17u, &pedalAmount)
            && params->get_info(plugin, 21u, &pedalExtra)
            && (pulseSync.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && (run.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && (outputMode.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && outputMode.id == 10u && outputRotation.id == 11u,
            "stepped parameter flags failed");
        ok &= check((nodeMode.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && std::abs(getParam(plugin, params, 1002u) + 720.0) < 1.0e-6
            && pedalAmount.id == 1006u
            && pedalExtra.id == 1010u
            && std::abs(getParam(plugin, params, 1006u) - 0.52) < 1.0e-6
            && std::abs(getParam(plugin, params, 1010u) - 0.5) < 1.0e-6
            && std::abs(getParam(plugin, params, 10u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 11u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 2000u) - 0.86) < 1.0e-6,
            "node or patch-matrix defaults failed");
    }

    ok &= check(plugin->activate(plugin, 48000.0, 32u, kFrames)
        && plugin->start_processing(plugin), "activation failed");
    const double selfExcitedEnergy = processBlocks(plugin, nullptr, 32u);
    ok &= check(selfExcitedEnergy > 1.0e-9,
        "default patch did not self-excite");

    plugin->reset(plugin);
    EventList strike;
    strike.addParam(1u, 0.0); // continuous excitation
    strike.addParam(8u, 0.0); // output gain
    strike.addNote(3, 1.0, 64u);
    std::array<double, kChannels> strikeChannels {};
    const double strikeEnergy = processBlocks(plugin, &strike, 8u,
        &strikeChannels);
    ok &= check(strikeEnergy > 1.0e-5,
        "MIDI note did not excite the feedback instrument");
    ok &= check(strikeChannels[3u] > 1.0e-7,
        "MIDI channel 4 did not address feedback node 4");

    plugin->reset(plugin);
    EventList quadFold;
    quadFold.addParam(10u, 1.0); // quad ring projection
    quadFold.addParam(11u, 37.0); // channel-ring rotation
    quadFold.addNote(-1, 1.0);
    std::array<double, kChannels> quadChannels {};
    ok &= check(processBlocks(plugin, &quadFold, 8u, &quadChannels) > 1.0e-7
        && quadChannels[0u] + quadChannels[1u]
            + quadChannels[2u] + quadChannels[3u] > 1.0e-8
        && quadChannels[4u] + quadChannels[5u]
            + quadChannels[6u] + quadChannels[7u] < 1.0e-20,
        "quad ring fold did not use outputs 1-4 exclusively");

    plugin->reset(plugin);
    EventList stereoFold;
    stereoFold.addParam(10u, 2.0); // stereo ring projection
    stereoFold.addParam(11u, -51.0);
    stereoFold.addNote(-1, 1.0);
    std::array<double, kChannels> stereoChannels {};
    ok &= check(processBlocks(plugin, &stereoFold, 8u, &stereoChannels)
            > 1.0e-7
        && stereoChannels[0u] + stereoChannels[1u] > 1.0e-8
        && stereoChannels[2u] + stereoChannels[3u]
            + stereoChannels[4u] + stereoChannels[5u]
            + stereoChannels[6u] + stereoChannels[7u] < 1.0e-20,
        "stereo ring fold did not use outputs 1-2 exclusively");

    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    EventList outputChange;
    outputChange.addParam(8u, -3.0);
    (void)processBlocks(plugin, &outputChange, 1u);
    MemoryState memory;
    clap_ostream_t streamOut { &memory, stateWrite };
    ok &= check(state && state->save(plugin, &streamOut),
        "state save failed");
    EventList outputAway;
    outputAway.addParam(8u, -40.0);
    (void)processBlocks(plugin, &outputAway, 1u);
    memory.position = 0u;
    clap_istream_t streamIn { &memory, stateRead };
    ok &= check(state && state->load(plugin, &streamIn)
        && std::abs(getParam(plugin, params, 8u) + 3.0) < 1.0e-6,
        "state round trip failed");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);
    if (ok) std::cout << "feedback shift CLAP smoke tests passed\n";
    return ok ? 0 : 1;
}
