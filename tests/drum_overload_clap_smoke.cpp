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

constexpr clap_id kCircuitParamId = 1u;
constexpr clap_id kOverloadParamId = 3u;
constexpr clap_id kOutputParamId = 12u;
constexpr clap_id kBypassParamId = 13u;
constexpr uint32_t kParamCount = 13u;
constexpr uint32_t kFrames = 512u;

struct EventList {
    std::vector<clap_event_param_value_t> events;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<uint32_t>(
                static_cast<const EventList*>(list->ctx)->events.size());
        },
        [](const clap_input_events_t* list,
            uint32_t index) -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void add(clap_id id, double value, uint32_t time = 0u)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events.push_back(event);
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t position = 0u;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 9u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->position < state->bytes.size()
        ? state->bytes.size() - state->position : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 7u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->position, count);
        state->position += count;
    }
    return static_cast<int64_t>(count);
}

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

bool hasFeature(const clap_plugin_descriptor_t* descriptor,
    const char* expected)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* feature = descriptor->features;
         *feature; ++feature) {
        if (std::strcmp(*feature, expected) == 0) return true;
    }
    return false;
}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id,
    double expected, double tolerance = 1.0e-6)
{
    double actual = 0.0;
    return params->get_value(plugin, id, &actual)
        && std::abs(actual - expected) <= tolerance;
}

float drumSample(uint32_t frame, float scale)
{
    const float time = static_cast<float>(frame) / 48000.0f;
    const float envelope = std::exp(-time * 16.0f);
    const float body = std::sin(2.0f * 3.14159265358979323846f
        * (54.0f * time + 4.0f * (1.0f - std::exp(-time * 35.0f))));
    const float click = frame < 24u
        ? 0.6f * (1.0f - static_cast<float>(frame) / 24.0f)
        : 0.0f;
    return scale * (0.82f * envelope * body + click);
}

bool audioPortProbe(const clap_plugin_t* plugin)
{
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (!ports || ports->count(plugin, true) != 1u
        || ports->count(plugin, false) != 1u) {
        return false;
    }
    clap_audio_port_info_t input {};
    clap_audio_port_info_t output {};
    return ports->get(plugin, 0u, true, &input)
        && ports->get(plugin, 0u, false, &output)
        && input.channel_count == 2u && output.channel_count == 2u
        && input.port_type && output.port_type
        && std::strcmp(input.port_type, CLAP_PORT_STEREO) == 0
        && std::strcmp(output.port_type, CLAP_PORT_STEREO) == 0
        && input.in_place_pair == output.id
        && output.in_place_pair == input.id;
}

bool parameterProbe(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params)
{
    if (!params || params->count(plugin) != kParamCount) return false;
    clap_param_info_t circuit {};
    clap_param_info_t bypass {};
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)) return false;
        if (info.id == kCircuitParamId) circuit = info;
        if (info.id == kBypassParamId) bypass = info;
    }
    if ((circuit.flags & CLAP_PARAM_IS_STEPPED) == 0u
        || (bypass.flags & CLAP_PARAM_IS_STEPPED) == 0u
        || circuit.min_value != 0.0 || circuit.max_value != 7.0) {
        return false;
    }
    constexpr std::array<const char*, 8u> kCircuitNames {
        "CONSOLE", "VALVE", "CLIP", "RUPTURE",
        "TAPE", "TRANSFORMER", "DIODE", "SPEAKER"
    };
    for (uint32_t index = 0u; index < kCircuitNames.size(); ++index) {
        char text[32] {};
        if (!params->value_to_text(plugin, kCircuitParamId,
                static_cast<double>(index), text, sizeof(text))
            || std::strcmp(text, kCircuitNames[index]) != 0) {
            return false;
        }
    }
    return true;
}

bool processProbe(const clap_plugin_t* plugin)
{
    std::array<float, kFrames> inputLeft {};
    std::array<float, kFrames> inputRight {};
    std::array<float, kFrames> outputLeft {};
    std::array<float, kFrames> outputRight {};
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        inputLeft[frame] = drumSample(frame, 1.0f);
        inputRight[frame] = drumSample(frame, 0.82f);
    }
    std::array<float*, 2u> inputPointers {
        inputLeft.data(), inputRight.data()
    };
    std::array<float*, 2u> outputPointers {
        outputLeft.data(), outputRight.data()
    };
    clap_audio_buffer_t input {};
    input.data32 = inputPointers.data();
    input.channel_count = 2u;
    clap_audio_buffer_t output {};
    output.data32 = outputPointers.data();
    output.channel_count = 2u;
    clap_process_t context {};
    context.frames_count = kFrames;
    context.audio_inputs = &input;
    context.audio_outputs = &output;
    context.audio_inputs_count = 1u;
    context.audio_outputs_count = 1u;

    EventList bypass;
    bypass.add(kBypassParamId, 1.0);
    context.in_events = &bypass.input;
    if (plugin->process(plugin, &context) != CLAP_PROCESS_CONTINUE
        || inputLeft != outputLeft || inputRight != outputRight) {
        std::cerr << "CLAP bypass was not sample exact\n";
        return false;
    }

    EventList overload;
    overload.add(kBypassParamId, 0.0);
    overload.add(kCircuitParamId, 3.0);
    overload.add(kOverloadParamId, 1.0);
    context.in_events = &overload.input;
    outputLeft.fill(0.0f);
    outputRight.fill(0.0f);
    if (plugin->process(plugin, &context) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    double differenceEnergy = 0.0;
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        if (!std::isfinite(outputLeft[frame])
            || !std::isfinite(outputRight[frame])) {
            return false;
        }
        const double difference = outputLeft[frame] - inputLeft[frame];
        differenceEnergy += difference * difference;
    }
    if (!(std::sqrt(differenceEnergy / kFrames) > 0.03)) {
        std::cerr << "CLAP overload processing was not audible\n";
        return false;
    }

    std::array<double, kFrames> input64Left {};
    std::array<double, kFrames> input64Right {};
    std::array<double, kFrames> output64Left {};
    std::array<double, kFrames> output64Right {};
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        input64Left[frame] = inputLeft[frame];
        input64Right[frame] = inputRight[frame];
    }
    std::array<double*, 2u> input64Pointers {
        input64Left.data(), input64Right.data()
    };
    std::array<double*, 2u> output64Pointers {
        output64Left.data(), output64Right.data()
    };
    input.data32 = nullptr;
    input.data64 = input64Pointers.data();
    output.data32 = nullptr;
    output.data64 = output64Pointers.data();
    context.in_events = nullptr;
    plugin->reset(plugin);
    if (plugin->process(plugin, &context) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        if (!std::isfinite(output64Left[frame])
            || !std::isfinite(output64Right[frame])) {
            return false;
        }
    }
    return true;
}

bool stateProbe(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params,
    const clap_plugin_state_t* state)
{
    EventList chosen;
    chosen.add(kCircuitParamId, 2.0);
    chosen.add(kOutputParamId, -11.25);
    params->flush(plugin, &chosen.input, nullptr);

    MemoryState memory;
    clap_ostream_t output { &memory, stateWrite };
    if (!state->save(plugin, &output) || memory.bytes.empty()) return false;

    EventList changed;
    changed.add(kCircuitParamId, 0.0);
    changed.add(kOutputParamId, 4.0);
    params->flush(plugin, &changed.input, nullptr);
    if (!getParam(plugin, params, kOutputParamId, 4.0)) return false;

    clap_istream_t input { &memory, stateRead };
    return state->load(plugin, &input)
        && getParam(plugin, params, kCircuitParamId, 2.0)
        && getParam(plugin, params, kOutputParamId, -11.25);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_drum_overload_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Drum Overload binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Drum Overload: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Drum Overload smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 1u
        && descriptor && std::strcmp(descriptor->id, argv[2]) == 0
        && std::strcmp(descriptor->name, "s3g Effect Drum Overload") == 0
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_DISTORTION)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_STEREO);

    const clap_plugin_t* plugin = ok
        ? factory->create_plugin(factory, &host, argv[2]) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    ok = ok && params && state && audioPortProbe(plugin)
        && parameterProbe(plugin, params)
        && stateProbe(plugin, params, state)
        && plugin->activate(plugin, 48000.0, 1u, kFrames)
        && plugin->start_processing(plugin)
        && processProbe(plugin);

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry && entry->deinit) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Drum Overload CLAP smoke failed\n";
        return 1;
    }
    std::cout << "s3g Drum Overload CLAP smoke passed\n";
    return 0;
}
