#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <vector>

namespace {

constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-neural-ecology-64";
constexpr std::size_t kWorkerStackBytes = 256u * 1024u;
constexpr uint32_t kChannels = 64u;
constexpr uint32_t kFrames = 64u;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}
struct InputEventList {
    const clap_event_header_t* event = nullptr;
};
uint32_t inputEventCount(const clap_input_events_t* events)
{
    const auto* list = static_cast<const InputEventList*>(events->ctx);
    return list && list->event ? 1u : 0u;
}
const clap_event_header_t* inputEventGet(const clap_input_events_t* events, uint32_t index)
{
    const auto* list = static_cast<const InputEventList*>(events->ctx);
    return list && index == 0u ? list->event : nullptr;
}
bool outputEventPush(const clap_output_events_t*, const clap_event_header_t*)
{
    return true;
}

struct StateBytes {
    std::vector<uint8_t> bytes;
    std::size_t offset = 0u;
};

int64_t writeState(const clap_ostream_t* stream, const void* data, uint64_t count)
{
    auto* state = static_cast<StateBytes*>(stream->ctx);
    const auto* first = static_cast<const uint8_t*>(data);
    state->bytes.insert(state->bytes.end(), first, first + count);
    return static_cast<int64_t>(count);
}

int64_t readState(const clap_istream_t* stream, void* data, uint64_t count)
{
    auto* state = static_cast<StateBytes*>(stream->ctx);
    const auto available = state->bytes.size() - state->offset;
    const auto size = std::min<std::size_t>(static_cast<std::size_t>(count), available);
    std::memcpy(data, state->bytes.data() + state->offset, size);
    state->offset += size;
    return static_cast<int64_t>(size);
}

struct WorkerInput {
    const clap_plugin_factory_t* factory = nullptr;
    bool passed = false;
};

void* runOnSmallStack(void* raw)
{
    auto& input = *static_cast<WorkerInput*>(raw);
    const clap_host_t host {
        CLAP_VERSION_INIT, nullptr, "s3g stack smoke", "s3g", "", "1",
        hostGetExtension, hostRequest, hostRequest, hostRequest
    };
    for (int iteration = 0; iteration < 2; ++iteration) {
        const auto* plugin = input.factory->create_plugin(input.factory, &host, kPluginId);
        if (!plugin) return nullptr;
        const bool initialized = plugin->init(plugin);
        const auto* state = static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE));
        StateBytes bytes;
        const clap_ostream_t output { &bytes, writeState };
        const bool saved = initialized && state && state->save(plugin, &output);
        const clap_istream_t savedInput { &bytes, readState };
        const bool loaded = saved && state->load(plugin, &savedInput);
        const bool activated = loaded && plugin->activate(plugin, 48000.0, 1u, 256u);
        bool processed = false;
        if (activated && plugin->start_processing(plugin)) {
            std::vector<float> samples(kChannels * kFrames);
            std::array<float*, kChannels> channels {};
            for (uint32_t channel = 0u; channel < kChannels; ++channel) {
                channels[channel] = samples.data() + channel * kFrames;
            }
            clap_audio_buffer_t outputBuffer {};
            outputBuffer.data32 = channels.data();
            outputBuffer.channel_count = kChannels;
            clap_event_param_value_t planesEvent {};
            planesEvent.header.size = sizeof(planesEvent);
            planesEvent.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            planesEvent.header.type = CLAP_EVENT_PARAM_VALUE;
            planesEvent.param_id = 49u; // Field Lattice Planes: 8 planes
            planesEvent.note_id = -1;
            planesEvent.port_index = -1;
            planesEvent.channel = -1;
            planesEvent.key = -1;
            planesEvent.value = 3.0;
            InputEventList eventList { &planesEvent.header };
            clap_input_events_t inputEvents { &eventList, inputEventCount, inputEventGet };
            clap_output_events_t outputEvents { nullptr, outputEventPush };
            clap_process_t process {};
            process.frames_count = kFrames;
            process.audio_outputs = &outputBuffer;
            process.audio_outputs_count = 1u;
            process.in_events = &inputEvents;
            process.out_events = &outputEvents;
            processed = plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
            const auto* params = static_cast<const clap_plugin_params_t*>(
                plugin->get_extension(plugin, CLAP_EXT_PARAMS));
            double planeSetting = 0.0;
            const bool eightPlanesSelected = params
                && params->get_value(plugin, 49u, &planeSetting)
                && planeSetting == 3.0;
            eventList.event = nullptr;
            process.steady_time += kFrames;
            processed = processed && eightPlanesSelected
                && plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
            plugin->stop_processing(plugin);
        }
        if (activated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        if (!processed || bytes.bytes.empty()) return nullptr;
    }
    input.passed = true;
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(module, "clap_entry"));
    if (!entry || !entry->init(argv[1])) {
        dlclose(module);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    WorkerInput input { factory, false };
    pthread_attr_t attributes;
    if (!factory || pthread_attr_init(&attributes) != 0) {
        entry->deinit();
        dlclose(module);
        return 1;
    }
    const bool stackConfigured =
        pthread_attr_setstacksize(&attributes, kWorkerStackBytes) == 0;
    pthread_t thread {};
    const bool started = stackConfigured
        && pthread_create(&thread, &attributes, runOnSmallStack, &input) == 0;
    pthread_attr_destroy(&attributes);
    if (started) pthread_join(thread, nullptr);
    entry->deinit();
    dlclose(module);
    if (!started || !input.passed) {
        std::fprintf(stderr, "Neural Ecology failed on a 256 KiB host thread stack\n");
        return 1;
    }
    std::puts("Neural Ecology create/state/process passed on a 256 KiB stack");
    return 0;
}
