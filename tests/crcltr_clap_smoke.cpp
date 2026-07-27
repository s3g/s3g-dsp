#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto macOS = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(macOS))
            if (entry.is_regular_file()) return entry.path();
    }
#endif
    return {};
}

struct EventList {
    std::vector<clap_event_param_value_t> events;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void add(clap_id id, double value)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events.push_back(event);
    }
};

float energy(const std::vector<float>& values)
{
    float sum = 0.0f;
    for (float value : values) {
        if (!std::isfinite(value)) return -1.0f;
        sum += value * value;
    }
    return sum;
}

bool processBlock(const clap_plugin_t* plugin,
                  std::vector<float>& inputLeft,
                  std::vector<float>& inputRight,
                  std::vector<float>& outputLeft,
                  std::vector<float>& outputRight,
                  EventList* eventList)
{
    float* inputChannels[] { inputLeft.data(), inputRight.data() };
    float* outputChannels[] { outputLeft.data(), outputRight.data() };
    clap_audio_buffer_t input {};
    input.data32 = inputChannels;
    input.channel_count = 2u;
    clap_audio_buffer_t output {};
    output.data32 = outputChannels;
    output.channel_count = 2u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = static_cast<uint32_t>(inputLeft.size());
    process.audio_inputs = &input;
    process.audio_outputs = &output;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = eventList ? &eventList->interface : nullptr;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_crcltr_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve CRCLTR plugin binary\n";
        return 1;
    }

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load CRCLTR plugin: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "CRCLTR smoke";
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
        factory, &host, "org.s3g.s3g-dsp.crcltr") : nullptr;
    ok = ok && plugin && plugin->init(plugin);

    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    clap_audio_port_info_t inputPort {};
    clap_audio_port_info_t outputPort {};
    ok = ok && ports && params
        && ports->count(plugin, true) == 1u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, true, &inputPort)
        && ports->get(plugin, 0u, false, &outputPort)
        && inputPort.channel_count == 2u
        && outputPort.channel_count == 2u
        && params->count(plugin) == 10u;

    bool foundRecord = false;
    bool foundTarget = false;
    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info);
        if (info.id == 8u) foundRecord = std::strcmp(info.name, "Record") == 0;
        if (info.id == 9u) foundTarget = std::strcmp(info.name, "Record Target") == 0;
    }
    ok = ok && foundRecord && foundTarget;
    ok = ok && plugin->activate(plugin, 1000.0, 1u, 512u);
    ok = ok && plugin->start_processing(plugin);

    std::vector<float> inputLeft(400u);
    std::vector<float> inputRight(400u);
    std::vector<float> outputLeft(400u);
    std::vector<float> outputRight(400u);
    for (uint32_t frame = 0u; frame < inputLeft.size(); ++frame) {
        inputLeft[frame] = 0.22f * std::sin(0.031f * static_cast<float>(frame));
        inputRight[frame] = 0.18f * std::cos(0.027f * static_cast<float>(frame));
    }
    EventList recordEvents;
    recordEvents.add(4u, 0.0); // Loop 1 side of the loop crossfade.
    recordEvents.add(5u, 1.0); // Fully wet.
    recordEvents.add(9u, 0.0); // Record Loop 1.
    recordEvents.add(8u, 1.0); // Hold record.
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &recordEvents);

    inputLeft.assign(512u, 0.0f);
    inputRight.assign(512u, 0.0f);
    outputLeft.assign(512u, 0.0f);
    outputRight.assign(512u, 0.0f);
    EventList releaseEvents;
    releaseEvents.add(8u, 0.0);
    ok = ok && processBlock(plugin, inputLeft, inputRight,
        outputLeft, outputRight, &releaseEvents);
    ok = ok && energy(outputLeft) > 0.001f && energy(outputRight) > 0.001f;

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);

    if (!ok) {
        std::cerr << "CRCLTR CLAP smoke test failed\n";
        return 1;
    }
    std::cout << "CRCLTR CLAP smoke test passed\n";
    return 0;
}
