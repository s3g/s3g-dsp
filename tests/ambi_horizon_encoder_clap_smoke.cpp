#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

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
constexpr clap_id kOutputGainParamId = 24u;
constexpr clap_id kAirNoiseParamId = 26u;
constexpr clap_id kMachinesParamId = 27u;
constexpr clap_id kBellsParamId = 28u;
constexpr clap_id kTrafficParamId = 29u;
constexpr clap_id kAircraftParamId = 30u;
constexpr clap_id kFoghornsParamId = 31u;
constexpr clap_id kSurfParamId = 32u;
constexpr clap_id kAircraftFlightParamId = 35u;
constexpr clap_id kFoghornPitchParamId = 39u;
constexpr clap_id kFoghornPressureParamId = 40u;
constexpr clap_id kFoghornLengthParamId = 41u;
constexpr clap_id kFieldListenModeParamId = 47u;
constexpr clap_id kFieldListenAmountParamId = 48u;
constexpr clap_id kFieldListenResponseParamId = 49u;
constexpr uint32_t kPresetCount = 16u;

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

struct LegacyParams {
    uint32_t order = 3u;
    uint32_t entities = 24u;
    uint32_t ecology = 0u;
    float activity = 0.48f;
    float occupancy = 0.36f;
    float pace = 0.42f;
    float memory = 0.68f;
    float cascade = 0.48f;
    float signals = 0.48f;
    float horizonBed = 0.62f;
    float localFloor = 0.22f;
    float rangeKm = 2.8f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float arcDeg = 240.0f;
    float detail = 0.54f;
    float air = 0.58f;
    uint32_t ground = 2u;
    float terrain = 0.42f;
    float carry = 0.0f;
    float turbulence = 0.24f;
    float edgeDb = 0.0f;
    float outputGainDb = -6.0f;
    uint32_t seed = 1979u;
};

struct LegacyState {
    uint32_t version = 1u;
    LegacyParams params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct LegacyParamsV2 {
    LegacyParams prefix {};
    float airNoise = 0.35f;
};

struct LegacyStateV2 {
    uint32_t version = 2u;
    LegacyParamsV2 params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct LegacyParamsV3 {
    LegacyParamsV2 prefix {};
    float machines = 0.45f;
    float bells = 0.25f;
    float traffic = 0.45f;
    float aircraft = 0.0f;
    float foghorns = 0.0f;
    float surf = 0.15f;
};

struct LegacyStateV3 {
    uint32_t version = 3u;
    LegacyParamsV3 params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct LegacyParamsV4 {
    LegacyParamsV3 prefix {};
    float trafficSpeed = 0.50f;
    float engineLoad = 0.55f;
    float aircraftFlight = 0.80f;
    float aircraftSpeed = 0.52f;
    float aircraftPower = 0.62f;
    float aircraftTone = 0.35f;
    float foghornPitch = 0.42f;
    float foghornPressure = 0.75f;
    float foghornLength = 0.55f;
    float waveRate = 0.45f;
    float waveBreak = 0.58f;
    float machineTone = 0.50f;
    float bellPitch = 0.52f;
    float bellDecay = 0.68f;
};

struct LegacyStateV4 {
    uint32_t version = 4u;
    LegacyParamsV4 params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct MemoryInput {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
    clap_istream_t interface {
        this,
        [](const clap_istream_t* stream, void* buffer, uint64_t size)
            -> int64_t {
            auto* self = static_cast<MemoryInput*>(stream->ctx);
            const size_t available = self->bytes.size() - self->offset;
            const size_t count = std::min<size_t>(
                available, static_cast<size_t>(size));
            if (count == 0u) return 0;
            std::memcpy(buffer, self->bytes.data() + self->offset, count);
            self->offset += count;
            return static_cast<int64_t>(count);
        },
    };

    template <typename State>
    explicit MemoryInput(const State& state)
        : bytes(sizeof(state))
    {
        std::memcpy(bytes.data(), &state, sizeof(state));
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
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    clap_audio_port_info_t outputInfo {};
    ok = ok && ports && params && state
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && params->count(plugin) == 49u;
    if (ok) {
        char display[64] {};
        double parsed = 0.0;
        ok = params->value_to_text(plugin, kAircraftFlightParamId,
                0.70, display, sizeof(display))
            && std::strcmp(display, "APPRCH") == 0
            && params->text_to_value(plugin, kAircraftFlightParamId,
                "OVERHEAD", &parsed)
            && std::abs(parsed - 1.0) < 1.0e-9
            && params->value_to_text(plugin, kFoghornPitchParamId,
                0.42, display, sizeof(display))
            && params->text_to_value(plugin, kFoghornPitchParamId,
                display, &parsed)
            && std::abs(parsed - 0.42) < 0.01
            && params->value_to_text(plugin, kFoghornLengthParamId,
                0.55, display, sizeof(display))
            && params->text_to_value(plugin, kFoghornLengthParamId,
                display, &parsed)
            && std::abs(parsed - 0.55) < 0.01
            && params->value_to_text(plugin, kFieldListenModeParamId,
                2.0, display, sizeof(display))
            && std::strcmp(display, "COUNTER") == 0
            && params->text_to_value(plugin, kFieldListenResponseParamId,
                "DISTANCE", &parsed)
            && std::abs(parsed - 3.0) < 1.0e-9;
    }
    if (ok) {
        LegacyState legacy {};
        MemoryInput input(legacy);
        double migratedAirNoise = 0.0;
        double migratedMachines = 0.0;
        double migratedBells = 0.0;
        double migratedAircraft = 1.0;
        ok = state->load(plugin, &input.interface)
            && params->get_value(
                plugin, kAirNoiseParamId, &migratedAirNoise)
            && params->get_value(plugin, kMachinesParamId, &migratedMachines)
            && params->get_value(plugin, kBellsParamId, &migratedBells)
            && params->get_value(plugin, kAircraftParamId, &migratedAircraft)
            && std::abs(migratedAirNoise - 1.0) < 1.0e-9
            && std::abs(migratedMachines - 1.0) < 1.0e-9
            && std::abs(migratedBells - 1.0) < 1.0e-9
            && std::abs(migratedAircraft) < 1.0e-9;
    }
    if (ok) {
        LegacyStateV2 legacy {};
        legacy.params.prefix.ecology = 2u;
        legacy.params.airNoise = 0.17f;
        MemoryInput input(legacy);
        double migratedAirNoise = 0.0;
        double migratedTraffic = 0.0;
        double migratedFoghorns = 1.0;
        double migratedSurf = 1.0;
        ok = state->load(plugin, &input.interface)
            && params->get_value(plugin, kAirNoiseParamId, &migratedAirNoise)
            && params->get_value(plugin, kTrafficParamId, &migratedTraffic)
            && params->get_value(plugin, kFoghornsParamId, &migratedFoghorns)
            && params->get_value(plugin, kSurfParamId, &migratedSurf)
            && std::abs(migratedAirNoise - 0.17) < 1.0e-6
            && std::abs(migratedTraffic - 1.0) < 1.0e-9
            && std::abs(migratedFoghorns) < 1.0e-9
            && std::abs(migratedSurf) < 1.0e-9;
    }
    if (ok) {
        LegacyStateV3 legacy {};
        legacy.params.prefix.prefix.ecology = 7u;
        legacy.params.aircraft = 0.73f;
        legacy.params.traffic = 0.19f;
        MemoryInput input(legacy);
        double migratedAircraft = 0.0;
        double migratedTraffic = 0.0;
        double migratedFlight = 0.0;
        double migratedHornPressure = 0.0;
        ok = state->load(plugin, &input.interface)
            && params->get_value(plugin, kAircraftParamId, &migratedAircraft)
            && params->get_value(plugin, kTrafficParamId, &migratedTraffic)
            && params->get_value(plugin, kAircraftFlightParamId, &migratedFlight)
            && params->get_value(plugin, kFoghornPressureParamId,
                &migratedHornPressure)
            && std::abs(migratedAircraft - 0.73) < 1.0e-6
            && std::abs(migratedTraffic - 0.19) < 1.0e-6
            && std::abs(migratedFlight - 0.80) < 1.0e-6
            && std::abs(migratedHornPressure - 0.75) < 1.0e-6;
    }
    if (ok) {
        LegacyStateV4 legacy {};
        legacy.params.prefix.prefix.prefix.ecology = 4u;
        legacy.params.machineTone = 0.23f;
        legacy.params.bellDecay = 0.81f;
        MemoryInput input(legacy);
        double migratedMachineTone = 0.0;
        double migratedBellDecay = 0.0;
        double migratedListenMode = 1.0;
        double migratedListenAmount = 0.0;
        double migratedListenResponse = 1.0;
        ok = state->load(plugin, &input.interface)
            && params->get_value(plugin, 44u, &migratedMachineTone)
            && params->get_value(plugin, 46u, &migratedBellDecay)
            && params->get_value(plugin, kFieldListenModeParamId,
                &migratedListenMode)
            && params->get_value(plugin, kFieldListenAmountParamId,
                &migratedListenAmount)
            && params->get_value(plugin, kFieldListenResponseParamId,
                &migratedListenResponse)
            && std::abs(migratedMachineTone - 0.23) < 1.0e-6
            && std::abs(migratedBellDecay - 0.81) < 1.0e-6
            && std::abs(migratedListenMode) < 1.0e-9
            && std::abs(migratedListenAmount - 0.65) < 1.0e-6
            && std::abs(migratedListenResponse) < 1.0e-9;
    }
    ok = ok && plugin->activate(plugin, 48000.0, 32u, kFrames)
        && plugin->start_processing(plugin);

    Audio audio;
    double minimumRms = 1.0;
    float maximumPeak = 0.0f;
    for (uint32_t preset = 0u; ok && preset < kPresetCount; ++preset) {
        EventList events;
        events.add(kPresetParamId, static_cast<double>(preset));
        events.add(kOutputGainParamId, 0.0);
        const Measurement measurement = render(plugin, audio, 96u, &events);
        const double rms = std::sqrt(measurement.wEnergy
            / static_cast<double>(96u * kFrames));
        minimumRms = std::min(minimumRms, rms);
        maximumPeak = std::max(maximumPeak, measurement.peak);
        std::cout << "CLAP preset " << preset << " 0 dB W RMS="
                  << rms << " peak=" << measurement.peak << '\n';
        // Preset selection must establish audible W-channel energy within the
        // first half-second at unity output, not only after a long render or
        // after applying positive gain in the host.
        ok = measurement.finite && measurement.inactiveChannelsClear
            && rms >= 0.0015 && measurement.peak >= 0.004f
            && measurement.peak < 0.98f;
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
