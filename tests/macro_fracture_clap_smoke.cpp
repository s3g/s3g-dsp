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

constexpr clap_id kProcessorParamId = 2u;
constexpr clap_id kAmountParamId = 3u;
constexpr uint32_t kProcessorCount = 10u;
constexpr uint32_t kStateMagic = 0x3146524du;

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

std::filesystem::path resolveBinary(
    const std::filesystem::path& supplied)
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

const void* hostGetExtension(
    const clap_host_t*, const char*) { return nullptr; }
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
    if (argc != 4) {
        std::cerr << "usage: s3g_macro_fracture_clap_smoke "
            << "<bundle-or-binary> <plugin-id> <channels>\n";
        return 2;
    }
    const uint32_t expectedChannels =
        static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Macro Fracture binary\n";
        return 1;
    }

    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Macro Fracture: "
                  << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Macro Fracture smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok
        ? static_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID))
        : nullptr;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, argv[2])
        : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok
        ? static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS))
        : nullptr;
    const auto* state = ok
        ? static_cast<const clap_plugin_state_t*>(
            plugin->get_extension(plugin, CLAP_EXT_STATE))
        : nullptr;
    const auto* ports = ok
        ? static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS))
        : nullptr;
    ok = ok && params && state && ports;

    clap_param_info_t processorInfo {};
    bool foundProcessor = false;
    if (ok) {
        for (uint32_t index = 0u;
             index < params->count(plugin); ++index) {
            clap_param_info_t info {};
            if (params->get_info(plugin, index, &info)
                && info.id == kProcessorParamId) {
                processorInfo = info;
                foundProcessor = true;
                break;
            }
        }
        ok = foundProcessor
            && (processorInfo.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && processorInfo.min_value == 0.0
            && processorInfo.max_value
                == static_cast<double>(kProcessorCount - 1u);
    }

    constexpr std::array<const char*, kProcessorCount> names {
        "RELAY", "CRUSH", "SPLICE", "LOGIC", "VOID",
        "THROAT", "ROBOT", "OCT DOWN", "OCT UP", "OCT STACK"
    };
    if (ok) {
        for (uint32_t index = 0u;
             index < kProcessorCount; ++index) {
            char text[32] {};
            ok = params->value_to_text(plugin, kProcessorParamId,
                    static_cast<double>(index), text, sizeof(text))
                && std::strcmp(text, names[index]) == 0;
            if (!ok) break;
        }
    }

    if (ok) {
        clap_audio_port_info_t input {};
        clap_audio_port_info_t output {};
        ok = ports->count(plugin, true) == 1u
            && ports->count(plugin, false) == 1u
            && ports->get(plugin, 0u, true, &input)
            && ports->get(plugin, 0u, false, &output)
            && input.channel_count == expectedChannels
            && output.channel_count == expectedChannels
            && (expectedChannels != 1u
                || (std::strcmp(input.port_type, CLAP_PORT_MONO) == 0
                    && std::strcmp(
                        output.port_type, CLAP_PORT_MONO) == 0));
    }

    if (ok) {
        ParamEvent selectOctStack(kProcessorParamId, 9.0);
        params->flush(plugin, &selectOctStack.input, nullptr);
        ParamEvent amount(kAmountParamId, 0.83);
        params->flush(plugin, &amount.input, nullptr);
        ok = getParam(plugin, params, kProcessorParamId, 9.0)
            && getParam(plugin, params, kAmountParamId, 0.83);
    }

    MemoryState saved;
    clap_ostream_t output { &saved, stateWrite };
    if (ok) {
        ok = state->save(plugin, &output)
            && saved.bytes.size() == 64u;
    }
    if (ok) {
        uint32_t magic = 0u;
        uint32_t version = 0u;
        uint32_t processor = 0u;
        std::memcpy(&magic, saved.bytes.data(), sizeof(magic));
        std::memcpy(&version, saved.bytes.data() + 4u,
            sizeof(version));
        std::memcpy(&processor, saved.bytes.data() + 12u,
            sizeof(processor));
        ok = magic == kStateMagic
            && version == 1u && processor == 9u;
    }

    if (ok) {
        ParamEvent selectRelay(kProcessorParamId, 0.0);
        params->flush(plugin, &selectRelay.input, nullptr);
        saved.offset = 0u;
        clap_istream_t input { &saved, stateRead };
        ok = state->load(plugin, &input)
            && getParam(plugin, params, kProcessorParamId, 9.0)
            && getParam(plugin, params, kAmountParamId, 0.83);
    }

    if (plugin) plugin->destroy(plugin);
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Macro Fracture parameter/state smoke failed\n";
        return 1;
    }
    std::cout << "Macro Fracture parameter/state smoke passed for "
              << argv[2] << "\n";
    return 0;
}
