#include <clap/clap.h>
#include <clap/ext/params.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kChannels = 24u;
constexpr uint32_t kFrames = 64u;

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto macOS = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(macOS)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

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

    void add(clap_id id, double value, uint32_t time)
    {
        clap_event_param_value_t event {};
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
        storage.push_back(event);
    }
};

struct AudioBlock {
    std::array<std::array<float, kFrames>, kChannels> samples {};
    std::array<float*, kChannels> pointers {};
    clap_audio_buffer_t buffer {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            pointers[channel] = samples[channel].data();
        }
        buffer.data32 = pointers.data();
        buffer.channel_count = kChannels;
    }

    void clear()
    {
        for (auto& channel : samples) channel.fill(0.0f);
    }
};

struct Instance {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;

    bool initialize(const clap_plugin_factory_t* factory,
        const clap_host_t* host, const char* pluginId)
    {
        plugin = factory->create_plugin(factory, host, pluginId);
        if (!plugin || !plugin->init(plugin)) return false;
        params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        if (!params || !plugin->activate(plugin, 48000.0, 1u, kFrames)) {
            return false;
        }
        plugin->reset(plugin);
        return plugin->start_processing(plugin);
    }

    void shutdown()
    {
        if (!plugin) return;
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        plugin = nullptr;
    }
};

bool processBlock(Instance& instance, AudioBlock& input, AudioBlock& output,
    const clap_input_events_t* events)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &input.buffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &output.buffer;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return instance.plugin->process(instance.plugin, &process)
        != CLAP_PROCESS_ERROR;
}

double maxDifference(const AudioBlock& left, const AudioBlock& right)
{
    double difference = 0.0;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            difference = std::max(difference, std::fabs(
                static_cast<double>(left.samples[channel][frame])
                - static_cast<double>(right.samples[channel][frame])));
        }
    }
    return difference;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_delay_processor_block_coalescing_clap_smoke "
            << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve plug-in binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load plug-in: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Delay block coalescing smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    Instance same;
    Instance distributed;
    ok = ok && factory
        && same.initialize(factory, &host, argv[2])
        && distributed.initialize(factory, &host, argv[2]);

    // The repeated delay-time entry verifies that list ordering is preserved:
    // the last value must win regardless of its timestamp.
    const std::vector<std::pair<clap_id, double>> values {
        { 1u, 900.0 }, { 2u, 0.61 }, { 3u, 0.78 }, { 4u, 0.32 },
        { 5u, 0.72 }, { 8u, 0.21 }, { 9u, 0.44 }, { 10u, -0.31 },
        { 11u, 0.83 }, { 12u, 0.46 }, { 13u, -0.24 }, { 14u, 2.5 },
        { 16u, 0.58 }, { 17u, -3.0 }, { 18u, 0.41 }, { 20u, 0.7 },
        { 21u, 0.52 }, { 23u, 0.43 }, { 24u, 0.31 }, { 26u, 0.48 },
        { 27u, 0.22 }, { 28u, 0.63 }, { 29u, 0.34 }, { 1u, 20.0 },
    };
    EventList sameEvents;
    EventList distributedEvents;
    for (size_t index = 0u; index < values.size(); ++index) {
        sameEvents.add(values[index].first, values[index].second, 0u);
        distributedEvents.add(values[index].first, values[index].second,
            static_cast<uint32_t>((index * (kFrames - 1u))
                / (values.size() - 1u)));
    }

    AudioBlock inputSame;
    AudioBlock inputDistributed;
    AudioBlock firstSame;
    AudioBlock firstDistributed;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            const float signal = 0.08f * std::sin(
                0.13f * static_cast<float>(frame)
                + 0.07f * static_cast<float>(channel));
            inputSame.samples[channel][frame] = signal;
            inputDistributed.samples[channel][frame] = signal;
        }
        inputSame.samples[channel][0] += 0.25f;
        inputDistributed.samples[channel][0] += 0.25f;
    }

    if (ok) {
        ok = processBlock(same, inputSame, firstSame, &sameEvents.input)
            && processBlock(distributed, inputDistributed, firstDistributed,
                &distributedEvents.input);
    }

    // Compare every published final value, including the final duplicate.
    std::array<double, 30u> expected {};
    std::array<bool, 30u> hasExpected {};
    for (const auto& value : values) {
        expected[value.first] = value.second;
        hasExpected[value.first] = true;
    }
    if (ok) {
        for (clap_id id = 0u; id < expected.size(); ++id) {
            if (!hasExpected[id]) continue;
            double sameValue = 0.0;
            double distributedValue = 0.0;
            ok = same.params->get_value(same.plugin, id, &sameValue)
                && distributed.params->get_value(
                    distributed.plugin, id, &distributedValue)
                && std::fabs(sameValue - expected[id]) <= 1.0e-9
                && std::fabs(distributedValue - expected[id]) <= 1.0e-9;
            if (!ok) break;
        }
    }

    AudioBlock nextInputSame;
    AudioBlock nextInputDistributed;
    AudioBlock nextSame;
    AudioBlock nextDistributed;
    if (ok) {
        ok = processBlock(same, nextInputSame, nextSame, nullptr)
            && processBlock(distributed, nextInputDistributed,
                nextDistributed, nullptr);
    }
    const double firstDifference = maxDifference(firstSame, firstDistributed);
    const double nextDifference = maxDifference(nextSame, nextDistributed);
    ok = ok && firstDifference <= 1.0e-7 && nextDifference <= 1.0e-7;

    std::cout << "delay block-coalesced same/distributed max difference: "
        << firstDifference << " / next block " << nextDifference << "\n";

    same.shutdown();
    distributed.shutdown();
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Delay block-coalescing regression failed\n";
        return 1;
    }
    std::cout << "Delay block-coalescing regression passed\n";
    return 0;
}
