#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

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
    std::vector<clap_event_param_value_t> values;
    clap_input_events_t events {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->values.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->values.size()
                ? &self->values[index].header : nullptr;
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
        values.push_back(event);
    }
};

struct StateMemory {
    std::vector<uint8_t> bytes;
    std::size_t offset = 0u;
};

int64_t writeState(const clap_ostream_t* stream, const void* source,
                   uint64_t bytes)
{
    auto* memory = stream ? static_cast<StateMemory*>(stream->ctx) : nullptr;
    if (!memory || !source) return -1;
    const auto* first = static_cast<const uint8_t*>(source);
    memory->bytes.insert(memory->bytes.end(), first, first + bytes);
    return static_cast<int64_t>(bytes);
}

int64_t readState(const clap_istream_t* stream, void* destination,
                  uint64_t bytes)
{
    auto* memory = stream ? static_cast<StateMemory*>(stream->ctx) : nullptr;
    if (!memory || !destination
        || memory->offset + bytes > memory->bytes.size()) return -1;
    std::memcpy(destination, memory->bytes.data() + memory->offset,
        static_cast<std::size_t>(bytes));
    memory->offset += static_cast<std::size_t>(bytes);
    return static_cast<int64_t>(bytes);
}

bool processBlock(const clap_plugin_t* plugin, EventList* events,
                  uint32_t frames, std::vector<std::vector<float>>& output)
{
    std::vector<float> inputLeft(frames);
    std::vector<float> inputRight(frames);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        inputLeft[frame] = 0.22f * std::sin(
            static_cast<float>(frame) * 0.071f);
        inputRight[frame] = 0.18f * std::cos(
            static_cast<float>(frame) * 0.053f);
    }
    output.assign(16u, std::vector<float>(frames, 0.0f));
    float* inputPointers[] { inputLeft.data(), inputRight.data() };
    std::vector<float*> outputPointers(16u);
    for (uint32_t channel = 0u; channel < 16u; ++channel)
        outputPointers[channel] = output[channel].data();
    clap_audio_buffer_t input {};
    input.data32 = inputPointers;
    input.channel_count = 2u;
    clap_audio_buffer_t out {};
    out.data32 = outputPointers.data();
    out.channel_count = 16u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frames;
    process.audio_inputs = &input;
    process.audio_outputs = &out;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = events ? &events->events : nullptr;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool validatesFormat(const std::vector<std::vector<float>>& output,
                     uint32_t activeChannels)
{
    double activeEnergy = 0.0;
    for (uint32_t channel = 0u; channel < output.size(); ++channel) {
        for (float value : output[channel]) {
            if (!std::isfinite(value)) return false;
            if (channel < activeChannels) activeEnergy += value * value;
            else if (value != 0.0f) return false;
        }
    }
    return activeEnergy > 0.000001;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_delay_field_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Delay Field plug-in binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Delay Field plug-in: " << dlerror()
                  << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Delay Field smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(factory,
        &host, "org.s3g.s3g-dsp.delay-field") : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    clap_audio_port_info_t inputPort {};
    clap_audio_port_info_t outputPort {};
    ok = ok && ports && params && state
        && ports->count(plugin, true) == 1u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, true, &inputPort)
        && ports->get(plugin, 0u, false, &outputPort)
        && inputPort.channel_count == 2u
        && outputPort.channel_count == 16u
        && params->count(plugin) == 55u;

    bool foundModel = false;
    bool foundFormat = false;
    bool foundTraversal = false;
    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info);
        if (info.id == 1u) foundModel = std::strcmp(info.name, "Model") == 0;
        if (info.id == 2u) foundFormat = std::strcmp(info.name, "Format") == 0;
        if (info.id == 410u)
            foundTraversal = std::strcmp(info.name, "Traversal") == 0;
    }
    ok = ok && foundModel && foundFormat && foundTraversal
        && plugin->activate(plugin, 1000.0, 1u, 4096u)
        && plugin->start_processing(plugin);

    constexpr uint32_t activeByFormat[] { 16u, 8u, 4u, 2u };
    std::vector<std::vector<float>> output;
    for (uint32_t model = 0u; ok && model < 4u; ++model) {
        for (uint32_t format = 0u; ok && format < 4u; ++format) {
            EventList changes;
            changes.add(1u, model);
            changes.add(2u, format);
            changes.add(3u, 22.5);
            if (model == 3u) {
                changes.add(401u, 10.0);
                changes.add(402u, 0.0);
                changes.add(403u, 20.0);
                changes.add(407u, 0.2);
                changes.add(408u, 1.0);
                changes.add(410u, 4.0);
            }
            ok = processBlock(plugin, &changes, 4096u, output)
                && validatesFormat(output, activeByFormat[format]);
        }
    }

    StateMemory memory;
    clap_ostream_t outputState { &memory, writeState };
    ok = ok && state->save(plugin, &outputState) && !memory.bytes.empty();
    if (ok) {
        EventList changes;
        changes.add(1u, 0.0);
        changes.add(2u, 0.0);
        params->flush(plugin, &changes.events, nullptr);
        memory.offset = 0u;
        clap_istream_t inputState { &memory, readState };
        ok = state->load(plugin, &inputState)
            && memory.offset == memory.bytes.size();
        double model = -1.0;
        double format = -1.0;
        ok = ok && params->get_value(plugin, 1u, &model)
            && params->get_value(plugin, 2u, &format)
            && model == 3.0 && format == 3.0;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Delay Field CLAP smoke failed\n";
        return 1;
    }
    std::cout << "Delay Field CLAP smoke passed\n";
    return 0;
}
