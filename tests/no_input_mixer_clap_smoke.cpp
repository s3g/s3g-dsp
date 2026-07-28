#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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

constexpr uint32_t kChannels = 8u;
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kParamCount = 320u;
constexpr clap_id kFirstMatrixParam = 100u;
constexpr clap_id kMotionShapeParam = 20u;
constexpr clap_id kAuxATypeParam = 23u;
constexpr clap_id kLaneOneAuxAParam = 1008u;

struct LegacyInsert {
    uint32_t type = 0u;
    float gain = 0.35f;
    float tone = 0.50f;
    float bias = 0.0f;
    float levelDb = 0.0f;
    uint32_t bypass = 0u;
};

struct LegacyLane {
    float body = 0.50f;
    float loss = 0.38f;
    float levelDb = -3.0f;
    uint32_t mute = 0u;
    float lowDb = 0.0f;
    float midFrequencyHz = 850.0f;
    float midGainDb = 0.0f;
    float highDb = 0.0f;
    std::array<LegacyInsert, 3u> inserts {};
};

struct LegacyParams {
    float outputGainDb = -18.0f;
    float ceilingDb = -1.0f;
    uint32_t limiterEnabled = 1u;
    uint32_t dcBlockEnabled = 1u;
    float feedback = 0.82f;
    float coupling = 0.42f;
    float phase = 0.34f;
    float drift = 0.18f;
    float formant = 0.30f;
    uint32_t quality = 1u;
    uint32_t seed = 0x5455444fu;
    std::array<float, 64u> matrix {};
    std::array<LegacyLane, 8u> lanes {};
};

struct LegacyState {
    uint32_t version = 1u;
    LegacyParams params {};
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
};

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
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
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
    const size_t count = std::min<size_t>(requested, 17u);
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
        static_cast<size_t>(requested), 13u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

struct AudioBlock {
    std::array<std::array<float, kFrames>, kChannels> output {};
    std::array<float*, kChannels> pointers {};
    clap_audio_buffer_t buffer {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            pointers[channel] = output[channel].data();
        }
        buffer.data32 = pointers.data();
        buffer.channel_count = kChannels;
    }

    void clear()
    {
        for (auto& channel : output) channel.fill(0.0f);
    }
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_no_input_mixer_clap_smoke "
                     "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve No Input Mixer binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load No Input Mixer: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "No Input Mixer smoke";
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
        factory, &host, "org.s3g.s3g-dsp.no-input-mixer-8ch") : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    clap_audio_port_info_t outputPort {};
    ok = ok && ports && params && state
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputPort)
        && outputPort.channel_count == kChannels
        && params->count(plugin) == kParamCount
        && plugin->activate(plugin, 48000.0, kFrames, kFrames)
        && plugin->start_processing(plugin);

    MemoryState saved;
    clap_ostream_t outputStream { &saved, stateWrite };
    ok = ok && state->save(plugin, &outputStream) && !saved.bytes.empty();

    EventList matrixChange;
    matrixChange.add(kFirstMatrixParam, 0.0);
    if (ok) params->flush(plugin, &matrixChange.input, nullptr);
    double matrixValue = -1.0;
    ok = ok && params->get_value(plugin, kFirstMatrixParam, &matrixValue)
        && matrixValue == 0.0;
    clap_istream_t inputStream { &saved, stateRead };
    ok = ok && state->load(plugin, &inputStream)
        && params->get_value(plugin, kFirstMatrixParam, &matrixValue)
        && std::abs(matrixValue - 0.94) < 1.0e-6;

    LegacyState legacy;
    legacy.params.feedback = 0.73f;
    legacy.params.matrix[0] = -0.44f;
    legacy.params.lanes[0].body = 0.61f;
    MemoryState legacyMemory;
    const auto* legacyBytes = reinterpret_cast<const uint8_t*>(&legacy);
    legacyMemory.bytes.assign(legacyBytes, legacyBytes + sizeof(legacy));
    clap_istream_t legacyStream { &legacyMemory, stateRead };
    double migrated = 0.0;
    ok = ok && state->load(plugin, &legacyStream)
        && params->get_value(plugin, 5u, &migrated)
        && std::abs(migrated - 0.73) < 1.0e-6
        && params->get_value(plugin, 100u, &migrated)
        && std::abs(migrated + 0.44) < 1.0e-6
        && params->get_value(plugin, 1000u, &migrated)
        && std::abs(migrated - 0.61) < 1.0e-6
        && params->get_value(plugin, 11u, &migrated)
        && std::abs(migrated - 0.28) < 1.0e-6
        && params->get_value(plugin, kLaneOneAuxAParam, &migrated)
        && std::abs(migrated - 0.08) < 1.0e-6;
    saved.offset = 0u;
    clap_istream_t restoredStream { &saved, stateRead };
    ok = ok && state->load(plugin, &restoredStream);

    EventList hybridChange;
    hybridChange.add(kMotionShapeParam, 3.0);
    hybridChange.add(kAuxATypeParam, 1.0);
    hybridChange.add(kLaneOneAuxAParam, 0.72);
    if (ok) params->flush(plugin, &hybridChange.input, nullptr);
    double hybridValue = 0.0;
    ok = ok && params->get_value(plugin, kMotionShapeParam, &hybridValue)
        && hybridValue == 3.0
        && params->get_value(plugin, kAuxATypeParam, &hybridValue)
        && hybridValue == 1.0
        && params->get_value(plugin, kLaneOneAuxAParam, &hybridValue)
        && std::abs(hybridValue - 0.72) < 1.0e-6;
    char processorName[32] {};
    ok = ok && params->value_to_text(plugin, kAuxATypeParam, 1.0,
            processorName, sizeof(processorName))
        && std::strcmp(processorName, "WOOL") == 0;

    AudioBlock audio;
    std::array<double, kChannels> energy {};
    std::array<double, kChannels> difference {};
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_outputs = &audio.buffer;
    process.audio_outputs_count = 1u;
    for (uint32_t block = 0u; ok && block < 220u; ++block) {
        audio.clear();
        ok = plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
        if (block < 24u) continue;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float value = audio.output[channel][frame];
                ok = ok && std::isfinite(value) && std::abs(value) <= 1.01f;
                energy[channel] += static_cast<double>(value) * value;
                if (channel > 0u) {
                    difference[channel] += std::abs(static_cast<double>(
                        value - audio.output[0u][frame]));
                }
            }
        }
    }
    for (uint32_t channel = 0u; ok && channel < kChannels; ++channel) {
        ok = energy[channel] > 1.0e-8;
        if (channel > 0u) ok = ok && difference[channel] > 1.0e-4;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);

    if (!ok) {
        std::cerr << "No Input Mixer CLAP smoke failed\n";
        return 1;
    }
    std::cout << "No Input Mixer CLAP smoke passed\n";
    return 0;
}
