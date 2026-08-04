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

constexpr clap_id kSitesParamId = 1u;
constexpr clap_id kOrderParamId = 3u;
constexpr clap_id kLayoutParamId = 4u;
constexpr clap_id kNetworkSpreadParamId = 11u;
constexpr clap_id kPropagationParamId = 12u;
constexpr clap_id kMacroEngineParamId = 17u;
constexpr clap_id kMacroMetricParamId = 18u;
constexpr clap_id kListenModeParamId = 27u;
constexpr clap_id kOutputParamId = 29u;
constexpr clap_id kSiteXParamId = 30u;
constexpr uint32_t kFrames = 256u;

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
        static_cast<size_t>(requested), 13u);
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
        static_cast<size_t>(requested), 9u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

struct ParamEvent {
    clap_event_param_value_t event {};
    clap_input_events_t input {
        this,
        [](const clap_input_events_t*) -> uint32_t { return 1u; },
        [](const clap_input_events_t* list,
            uint32_t index) -> const clap_event_header_t* {
            if (index != 0u) return nullptr;
            return &static_cast<const ParamEvent*>(
                list->ctx)->event.header;
        },
    };

    ParamEvent(clap_id id, double value)
    {
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }
};

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

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

bool setParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double value)
{
    ParamEvent event(id, value);
    params->flush(plugin, &event.input, nullptr);
    double actual = 0.0;
    return params->get_value(plugin, id, &actual)
        && std::fabs(actual - value) <= 1.0e-6;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_cartography_encoder_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Cartography binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Cartography: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Cartography smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok
        ? static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok
        ? static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok
        ? static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* ports = ok
        ? static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    ok = ok && params && state && ports && params->count(plugin) == 35u;

    if (ok) {
        clap_audio_port_info_t input {};
        clap_audio_port_info_t output {};
        ok = ports->count(plugin, true) == 1u
            && ports->count(plugin, false) == 1u
            && ports->get(plugin, 0u, true, &input)
            && ports->get(plugin, 0u, false, &output)
            && input.channel_count == 2u
            && output.channel_count == 64u
            && std::strcmp(input.port_type, CLAP_PORT_STEREO) == 0;
    }

    if (ok) {
        char text[32] {};
        double parsed = 0.0;
        ok = params->value_to_text(plugin, kLayoutParamId, 4.0,
                text, sizeof(text))
            && std::strcmp(text, "WATERFRONT") == 0
            && params->value_to_text(plugin, kMacroEngineParamId, 4.0,
                text, sizeof(text))
            && std::strcmp(text, "FRACTURE") == 0
            && params->value_to_text(plugin, kListenModeParamId, 3.0,
                text, sizeof(text))
            && std::strcmp(text, "BALANCE") == 0
            && params->text_to_value(plugin, kPropagationParamId,
                "35%", &parsed)
            && std::fabs(parsed - 0.35) < 0.000001;
    }

    if (ok) {
        ok = setParam(plugin, params, kSitesParamId, 4.0)
            && setParam(plugin, params, kOrderParamId, 3.0)
            && setParam(plugin, params, kLayoutParamId, 3.0)
            && setParam(plugin, params, kMacroMetricParamId, 2.0)
            && setParam(plugin, params, kSiteXParamId, 0.33)
            && setParam(plugin, params, kOutputParamId, -3.0);
    }

    MemoryState saved;
    clap_ostream_t outputState { &saved, stateWrite };
    if (ok) ok = state->save(plugin, &outputState) && saved.bytes.size() > 256u;
    if (ok) {
        ok = setParam(plugin, params, kLayoutParamId, 0.0)
            && setParam(plugin, params, kSiteXParamId, -0.75);
        saved.offset = 0u;
        clap_istream_t inputState { &saved, stateRead };
        ok = ok && state->load(plugin, &inputState);
        double layout = 0.0;
        double siteX = 0.0;
        ok = ok && params->get_value(plugin, kLayoutParamId, &layout)
            && params->get_value(plugin, kSiteXParamId, &siteX)
            && layout == 3.0 && std::fabs(siteX - 0.33) < 0.0001;
    }

    if (ok) {
        ok = setParam(plugin, params, kNetworkSpreadParamId, 0.0)
            && setParam(plugin, params, kPropagationParamId, 0.0)
            && setParam(plugin, params, kMacroEngineParamId, 0.0)
            && setParam(plugin, params, kListenModeParamId, 0.0)
            && setParam(plugin, params, kOutputParamId, 0.0)
            && plugin->activate(plugin, 48000.0, 32u, kFrames)
            && plugin->start_processing(plugin);
    }

    std::array<std::array<float, kFrames>, 2u> inputStorage {};
    inputStorage[0][0] = 1.0f;
    inputStorage[1][0] = 1.0f;
    std::array<float*, 2u> inputPointers {
        inputStorage[0].data(), inputStorage[1].data()
    };
    std::array<std::array<float, kFrames>, 64u> outputStorage {};
    std::array<float*, 64u> outputPointers {};
    for (uint32_t channel = 0u; channel < 64u; ++channel) {
        outputPointers[channel] = outputStorage[channel].data();
    }
    clap_audio_buffer_t inputBuffer {};
    inputBuffer.data32 = inputPointers.data();
    inputBuffer.channel_count = 2u;
    clap_audio_buffer_t outputBuffer {};
    outputBuffer.data32 = outputPointers.data();
    outputBuffer.channel_count = 64u;
    clap_process_t process {};
    process.frames_count = kFrames;
    process.audio_inputs = &inputBuffer;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &outputBuffer;
    process.audio_outputs_count = 1u;
    if (ok) {
        ok = plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
        double activeEnergy = 0.0;
        double inactiveEnergy = 0.0;
        for (uint32_t channel = 0u; channel < 64u; ++channel) {
            for (float sample : outputStorage[channel]) {
                if (!std::isfinite(sample)) ok = false;
                if (channel < 16u) activeEnergy += sample * sample;
                else inactiveEnergy += sample * sample;
            }
        }
        ok = ok && activeEnergy > 0.001 && inactiveEnergy == 0.0;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Cartography CLAP parameter/state/audio smoke failed\n";
        return 1;
    }
    std::cout << "Cartography CLAP parameter/state/audio smoke passed for "
              << argv[2] << "\n";
    return 0;
}
