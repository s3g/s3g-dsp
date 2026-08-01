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
#include <vector>

namespace {

constexpr clap_id kCircuitParamId = 16u;
constexpr uint32_t kCircuitCount = 8u;

struct LegacyParamsV2 {
    float inputGainDb = 0.0f;
    float pressure = 0.28f;
    float shred = 0.18f;
    float feedback = 0.12f;
    float color = 0.55f;
    float react = 0.25f;
    float tune = 0.65f;
    float body = 0.65f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float skew = 0.0f;
    float center = 0.5f;
    float glideMs = 250.0f;
    float mix = 0.65f;
    float outputGainDb = -3.0f;
};

struct LegacyStateV2 {
    uint32_t version = 2u;
    LegacyParamsV2 params {};
};

static_assert(sizeof(LegacyStateV2) == 64u,
    "Unexpected Macro Shred v2 state layout.");

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
        static_cast<size_t>(requested), 11u);
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
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->offset, count);
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
            return &static_cast<const ParamEvent*>(list->ctx)->event.header;
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
        for (const auto& entry :
             std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double expected)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value)
        && std::abs(value - expected) <= 1.0e-6;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_macro_shred_clap_smoke "
            << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Macro Shred binary\n";
        return 1;
    }

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Macro Shred: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Macro Shred circuit smoke";
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
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    ok = ok && params && state;

    clap_param_info_t circuitInfo {};
    bool foundCircuit = false;
    if (ok) {
        for (uint32_t index = 0u; index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            if (params->get_info(plugin, index, &info)
                && info.id == kCircuitParamId) {
                circuitInfo = info;
                foundCircuit = true;
                break;
            }
        }
        ok = foundCircuit
            && (circuitInfo.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && circuitInfo.min_value == 0.0
            && circuitInfo.max_value
                == static_cast<double>(kCircuitCount - 1u);
    }

    constexpr std::array<const char*, kCircuitCount> kNames {
        "SHRED", "WOOL", "RAT", "ZONE A",
        "ZONE B", "FUZZ I", "FUZZ II", "DIODE"
    };
    if (ok) {
        for (uint32_t circuit = 0u; circuit < kCircuitCount; ++circuit) {
            char text[32] {};
            ok = params->value_to_text(plugin, kCircuitParamId,
                    static_cast<double>(circuit), text, sizeof(text))
                && std::strcmp(text, kNames[circuit]) == 0;
            if (!ok) break;
        }
    }

    if (ok) {
        ParamEvent selectDiode(kCircuitParamId, 7.0);
        params->flush(plugin, &selectDiode.input, nullptr);
        ok = getParam(plugin, params, kCircuitParamId, 7.0);
    }

    MemoryState current;
    clap_ostream_t output { &current, stateWrite };
    if (ok) {
        ok = state->save(plugin, &output)
            && current.bytes.size() == 68u;
    }
    if (ok) {
        uint32_t version = 0u;
        uint32_t circuit = 0u;
        std::memcpy(&version, current.bytes.data(), sizeof(version));
        std::memcpy(&circuit, current.bytes.data() + 64u,
            sizeof(circuit));
        ok = version == 3u && circuit == 7u;
    }

    LegacyStateV2 legacy;
    legacy.params.inputGainDb = 7.25f;
    MemoryState legacyInput;
    const auto* legacyBytes =
        reinterpret_cast<const uint8_t*>(&legacy);
    legacyInput.bytes.assign(
        legacyBytes, legacyBytes + sizeof(legacy));
    clap_istream_t input { &legacyInput, stateRead };
    if (ok) {
        ok = state->load(plugin, &input)
            && getParam(plugin, params, kCircuitParamId, 0.0)
            && getParam(plugin, params, 1u, 7.25);
    }

    MemoryState migrated;
    clap_ostream_t migratedOutput { &migrated, stateWrite };
    if (ok) {
        ok = state->save(plugin, &migratedOutput)
            && migrated.bytes.size() == 68u;
    }
    if (ok) {
        uint32_t version = 0u;
        uint32_t circuit = 1u;
        std::memcpy(&version, migrated.bytes.data(), sizeof(version));
        std::memcpy(&circuit, migrated.bytes.data() + 64u,
            sizeof(circuit));
        ok = version == 3u && circuit == 0u;
    }

    if (plugin) plugin->destroy(plugin);
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Macro Shred circuit/state smoke failed\n";
        return 1;
    }
    std::cout << "Macro Shred circuit/state smoke passed for "
              << argv[2] << "\n";
    return 0;
}
