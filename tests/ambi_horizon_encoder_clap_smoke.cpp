#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kChannels = 64u;
constexpr uint32_t kFrames = 256u;
constexpr clap_id kPresetParamId = 1u;
constexpr clap_id kOrderParamId = 2u;
constexpr clap_id kEntitiesParamId = 3u;
constexpr uint32_t kPresetCount = 12u;

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
    std::vector<clap_event_param_value_t> values;
    clap_input_events_t interface {
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

struct Audio {
    std::array<std::array<float, kFrames>, kChannels> samples {};
    std::array<float*, kChannels> pointers {};
    clap_audio_buffer_t buffer {};

    Audio()
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

bool processBlock(const clap_plugin_t* plugin, Audio& audio, EventList* events,
                  uint32_t frames = kFrames)
{
    audio.clear();
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frames;
    process.audio_outputs = &audio.buffer;
    process.audio_outputs_count = 1u;
    process.in_events = events ? &events->interface : nullptr;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

struct RealtimeStress {
    double p95Load = 0.0;
    double maximumLoad = 0.0;
    uint32_t deadlineMisses = 0u;
    bool finite = true;
    bool highOrderChannelsActive = false;
};

RealtimeStress stressMaximumOrder(const clap_plugin_t* plugin, Audio& audio)
{
    constexpr uint32_t frames = 16u;
    constexpr uint32_t warmupBlocks = 256u;
    constexpr uint32_t measuredBlocks = 2000u;
    constexpr double sampleRate = 96000.0;
    const double deadlineSeconds = static_cast<double>(frames) / sampleRate;

    EventList maximumConfiguration;
    maximumConfiguration.add(kOrderParamId, 7.0);
    maximumConfiguration.add(kEntitiesParamId, 32.0);
    RealtimeStress result {};
    for (uint32_t block = 0u; block < warmupBlocks; ++block) {
        if (!processBlock(plugin, audio,
                block == 0u ? &maximumConfiguration : nullptr, frames)) {
            result.finite = false;
            return result;
        }
    }

    std::vector<double> loads;
    loads.reserve(measuredBlocks);
    for (uint32_t block = 0u; block < measuredBlocks; ++block) {
        const auto start = std::chrono::steady_clock::now();
        const bool processed = processBlock(plugin, audio, nullptr, frames);
        const auto finish = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(finish - start).count();
        const double load = seconds / deadlineSeconds;
        loads.push_back(load);
        result.maximumLoad = std::max(result.maximumLoad, load);
        result.deadlineMisses += load > 1.0 ? 1u : 0u;
        result.finite &= processed;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                const float sample = audio.samples[channel][frame];
                result.finite &= std::isfinite(sample);
                if (channel >= 16u && sample != 0.0f) {
                    result.highOrderChannelsActive = true;
                }
            }
        }
    }
    std::sort(loads.begin(), loads.end());
    result.p95Load = loads[static_cast<size_t>(
        std::floor(0.95 * static_cast<double>(loads.size() - 1u)))];
    return result;
}

struct Measurement {
    double wEnergy = 0.0;
    float peak = 0.0f;
    bool finite = true;
    bool inactiveChannelsClear = true;
};

Measurement render(const clap_plugin_t* plugin, Audio& audio,
                   uint32_t blocks, EventList* firstEvents)
{
    Measurement result {};
    for (uint32_t block = 0u; block < blocks; ++block) {
        if (!processBlock(plugin, audio, block == 0u ? firstEvents : nullptr)) {
            result.finite = false;
            return result;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (const float sample : audio.samples[channel]) {
                result.finite &= std::isfinite(sample);
                result.peak = std::max(result.peak, std::abs(sample));
                if (channel == 0u) {
                    result.wEnergy += static_cast<double>(sample) * sample;
                }
                if (channel >= 16u && sample != 0.0f) {
                    result.inactiveChannelsClear = false;
                }
            }
        }
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_horizon_encoder_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Horizon plugin binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Horizon plugin: " << dlerror() << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Horizon smoke";
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
        factory, &host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);

    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    clap_audio_port_info_t outputInfo {};
    ok = ok && ports && params
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && params->count(plugin) == 25u;
    ok = ok && plugin->activate(plugin, 48000.0, 32u, kFrames)
        && plugin->start_processing(plugin);

    Audio audio;
    double minimumRms = 1.0;
    float maximumPeak = 0.0f;
    for (uint32_t preset = 0u; ok && preset < kPresetCount; ++preset) {
        EventList events;
        events.add(kPresetParamId, static_cast<double>(preset));
        const Measurement measurement = render(plugin, audio, 96u, &events);
        const double rms = std::sqrt(measurement.wEnergy
            / static_cast<double>(96u * kFrames));
        minimumRms = std::min(minimumRms, rms);
        maximumPeak = std::max(maximumPeak, measurement.peak);
        ok = measurement.finite && measurement.inactiveChannelsClear
            && rms > 1.0e-5 && measurement.peak < 0.98f;
        if (!ok) {
            std::cerr << "Preset " << preset << " failed: rms=" << rms
                      << " peak=" << measurement.peak
                      << " finite=" << measurement.finite
                      << " inactive-clear=" << measurement.inactiveChannelsClear
                      << '\n';
        }
    }

    RealtimeStress maximumOrderStress {};
    if (ok) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        ok = plugin->activate(plugin, 96000.0, 16u, kFrames)
            && plugin->start_processing(plugin);
    }
    if (ok) {
        maximumOrderStress = stressMaximumOrder(plugin, audio);
        ok = maximumOrderStress.finite
            && maximumOrderStress.highOrderChannelsActive
            && maximumOrderStress.p95Load < 0.90;
        if (!ok) {
            std::cerr << "7OA/32-entity 96 kHz/16-frame stress failed: p95="
                      << maximumOrderStress.p95Load * 100.0 << "% max="
                      << maximumOrderStress.maximumLoad * 100.0
                      << "% misses=" << maximumOrderStress.deadlineMisses
                      << " finite=" << maximumOrderStress.finite
                      << " high-order-active="
                      << maximumOrderStress.highOrderChannelsActive << '\n';
        }
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);

    if (!ok) return 1;
    std::cout << "Horizon CLAP startup/preset smoke passed: minimum W RMS="
              << minimumRms << " maximum peak=" << maximumPeak
              << "; 7OA/32-entity 96 kHz/16-frame p95="
              << maximumOrderStress.p95Load * 100.0 << "% ("
              << maximumOrderStress.deadlineMisses << " raw misses)\n";
    return 0;
}
