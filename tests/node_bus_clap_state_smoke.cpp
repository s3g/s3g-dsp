#include <clap/clap.h>
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
#include <string>
#include <vector>

namespace {

constexpr uint32_t kChannels = 128u;
constexpr uint32_t kFrames = 16u;
constexpr uint32_t kNodeCount = 16u;
constexpr clap_id kOutputLayoutParam = 1u;
constexpr clap_id kOutputChannelsParam = 2u;
constexpr clap_id kCursorZParam = 8u;
constexpr clap_id kOutputGainParam = 13u;
constexpr clap_id kLockZParam = 14u;
constexpr clap_id kNodeParamBase = 1000u;
constexpr clap_id kNodeParamStride = 10u;
constexpr clap_id kNodeSourceLayoutField = 2u;
constexpr clap_id kNodeSourceChannelsField = 3u;
constexpr clap_id kNodeInputStartField = 4u;
constexpr clap_id kNodeZField = 7u;

clap_id nodeParam(uint32_t node, clap_id field)
{
    return kNodeParamBase + node * kNodeParamStride + field;
}

const void* hostGetExtension(const clap_host_t*, const char*)
{
    return nullptr;
}

void hostRequest(const clap_host_t*) {}

struct EventList {
    std::vector<clap_event_param_value_t> storage;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->storage.size()
                ? &self->storage[index].header : nullptr;
        },
    };

    void add(clap_id id, double value)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        storage.push_back(event);
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
        static_cast<size_t>(requested), 23u);
    if (count > 0u) {
        const auto* bytes = static_cast<const uint8_t*>(source);
        state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    }
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
        available, static_cast<size_t>(requested), 19u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
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
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

struct AudioBlock {
    std::array<std::array<float, kFrames>, kChannels> input {};
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, kChannels> inputPointers {};
    std::array<float*, kChannels> outputPointers {};
    clap_audio_buffer_t inputBuffer {};
    clap_audio_buffer_t outputBuffer {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            inputPointers[channel] = input[channel].data();
            outputPointers[channel] = output[channel].data();
        }
        inputBuffer.data32 = inputPointers.data();
        inputBuffer.channel_count = kChannels;
        outputBuffer.data32 = outputPointers.data();
        outputBuffer.channel_count = kChannels;
    }

    void clearOutput()
    {
        for (auto& channel : output) channel.fill(0.0f);
    }
};

bool runBlock(const clap_plugin_t* plugin, AudioBlock& audio,
              const clap_input_events_t* events)
{
    audio.clearOutput();
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &audio.inputBuffer;
    process.audio_outputs = &audio.outputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
}

bool expectValue(const clap_plugin_t* plugin,
                 const clap_plugin_params_t* params,
                 clap_id id, double expected, const char* label)
{
    double actual = 0.0;
    const bool ok = params->get_value(plugin, id, &actual)
        && std::fabs(actual - expected) <= 1.0e-6;
    if (!ok) {
        std::cerr << label << " mismatch for parameter " << id
                  << ": expected " << expected
                  << ", got " << actual << "\n";
    }
    return ok;
}

bool expectSanitizedSnapshot(const clap_plugin_t* plugin,
                             const clap_plugin_params_t* params,
                             const char* stage)
{
    bool ok = true;
    ok = expectValue(plugin, params, kOutputLayoutParam, 14.0, stage) && ok;
    ok = expectValue(plugin, params, kOutputChannelsParam, 24.0, stage) && ok;
    ok = expectValue(plugin, params,
        nodeParam(0u, kNodeSourceLayoutField), 1.0, stage) && ok;
    ok = expectValue(plugin, params,
        nodeParam(0u, kNodeSourceChannelsField), 4.0, stage) && ok;
    ok = expectValue(plugin, params,
        nodeParam(0u, kNodeInputStartField), 1.0, stage) && ok;
    ok = expectValue(plugin, params,
        nodeParam(1u, kNodeInputStartField), 5.0, stage) && ok;
    ok = expectValue(plugin, params,
        nodeParam(2u, kNodeInputStartField), 13.0, stage) && ok;
    ok = expectValue(plugin, params, kLockZParam, 0.0, stage) && ok;
    ok = expectValue(plugin, params, kCursorZParam, 0.0, stage) && ok;
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        ok = expectValue(plugin, params,
            nodeParam(node, kNodeZField), 0.0, stage) && ok;
    }
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_node_bus_clap_state_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Node Bus plugin binary\n";
        return 1;
    }

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Node Bus plugin: " << dlerror() << "\n";
        return 1;
    }

    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Node Bus state smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    bool initialized = false;
    bool activated = false;
    bool processing = false;
    if (plugin) initialized = plugin->init(plugin);
    ok = ok && initialized;

    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    ok = ok && params && params->get_value && state
        && state->save && state->load;

    if (ok) {
        activated = plugin->activate(
            plugin, 48000.0, kFrames, kFrames);
        ok = activated;
    }
    if (ok) {
        processing = plugin->start_processing(plugin);
        ok = processing;
    }

    AudioBlock audio;

    // A single process event must publish both the selected output layout and
    // the channel count derived by the DSP sanitizer.
    if (ok) {
        EventList events;
        events.add(kOutputLayoutParam, 14.0); // DBL24 -> 24 channels.
        ok = runBlock(plugin, audio, &events.input)
            && expectValue(plugin, params,
                kOutputLayoutParam, 14.0, "output layout publication")
            && expectValue(plugin, params,
                kOutputChannelsParam, 24.0, "derived output channels");
    }

    // Source layout likewise owns sourceChannels and shifts every later node's
    // packed bus start. Only the source-layout parameter is sent here.
    if (ok) {
        EventList events;
        events.add(nodeParam(0u, kNodeSourceLayoutField), 1.0); // QUAD.
        ok = runBlock(plugin, audio, &events.input)
            && expectValue(plugin, params,
                nodeParam(0u, kNodeSourceChannelsField), 4.0,
                "derived source channels")
            && expectValue(plugin, params,
                nodeParam(0u, kNodeInputStartField), 1.0,
                "node 1 bus start")
            && expectValue(plugin, params,
                nodeParam(1u, kNodeInputStartField), 5.0,
                "node 2 shifted bus start")
            && expectValue(plugin, params,
                nodeParam(2u, kNodeInputStartField), 13.0,
                "node 3 shifted bus start");
    }

    // Establish nonzero Z state while unlocked, then verify Lock Z clears the
    // complete snapshot and that unlocking cannot revive stale bank values.
    if (ok) {
        EventList events;
        events.add(kLockZParam, 0.0);
        events.add(kCursorZParam, 1.25);
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            events.add(nodeParam(node, kNodeZField),
                -1.5 + static_cast<double>(node) * 0.17);
        }
        ok = runBlock(plugin, audio, &events.input)
            && expectValue(plugin, params,
                kCursorZParam, 1.25, "unlocked cursor Z");
        for (uint32_t node = 0u; ok && node < kNodeCount; ++node) {
            ok = expectValue(plugin, params, nodeParam(node, kNodeZField),
                -1.5 + static_cast<double>(node) * 0.17,
                "unlocked node Z");
        }
    }
    if (ok) {
        EventList events;
        events.add(kLockZParam, 1.0);
        ok = runBlock(plugin, audio, &events.input)
            && expectValue(plugin, params,
                kCursorZParam, 0.0, "locked cursor Z");
        for (uint32_t node = 0u; ok && node < kNodeCount; ++node) {
            ok = expectValue(plugin, params, nodeParam(node, kNodeZField),
                0.0, "locked node Z");
        }
    }
    if (ok) {
        EventList events;
        events.add(kLockZParam, 0.0);
        ok = runBlock(plugin, audio, &events.input)
            && expectSanitizedSnapshot(plugin, params,
                "unlock must not resurrect Z");
    }

    MemoryState saved;
    clap_ostream_t outputStream { &saved, stateWrite };
    if (ok) {
        ok = state->save(plugin, &outputStream) && !saved.bytes.empty();
        if (!ok) std::cerr << "Could not save sanitized Node Bus state\n";
    }

    // Move every dependency away from the saved values before restoring it.
    if (ok) {
        EventList events;
        events.add(kOutputLayoutParam, 0.0);
        events.add(nodeParam(0u, kNodeSourceLayoutField), 12.0);
        events.add(kCursorZParam, -1.75);
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            events.add(nodeParam(node, kNodeZField), 0.75);
        }
        events.add(kOutputGainParam, -9.0);
        ok = runBlock(plugin, audio, &events.input);
    }

    if (ok) {
        saved.offset = 0u;
        clap_istream_t inputStream { &saved, stateRead };
        ok = state->load(plugin, &inputStream)
            && expectSanitizedSnapshot(plugin, params,
                "restored published state")
            && runBlock(plugin, audio, nullptr)
            && expectSanitizedSnapshot(plugin, params,
                "restored audio-visible state");
        if (!ok) std::cerr << "Sanitized Node Bus state round-trip failed\n";
    }

    if (processing) plugin->stop_processing(plugin);
    if (activated) plugin->deactivate(plugin);
    if (plugin) plugin->destroy(plugin);
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);

    if (!ok) return 1;
    std::cout << "Node Bus parameter/state synchronization smoke passed for "
              << argv[2] << "\n";
    return 0;
}
