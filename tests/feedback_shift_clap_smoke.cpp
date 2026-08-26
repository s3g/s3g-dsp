#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
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

constexpr const char* kPluginId = "org.s3g.s3g-dsp.feedback-shift";
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 8u;
constexpr uint32_t kParamCount = 397u;
constexpr double kExternalExciterSource = 5.0;
constexpr double kOffExciterSource = 7.0;

struct HostContext {
    clap_host_t host {};
    uint32_t processRequests = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t* host)
{
    ++hostContext(host)->processRequests;
}
void hostRequestCallback(const clap_host_t*) {}

struct EventList {
    std::array<clap_event_param_value_t, 32u> params {};
    std::array<clap_event_note_t, 4u> notes {};
    std::array<const clap_event_header_t*, 36u> events {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t eventCount = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const EventList*>(list->ctx)->eventCount;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* source = static_cast<const EventList*>(list->ctx);
            return index < source->eventCount ? source->events[index] : nullptr;
        },
    };

    void insert(const clap_event_header_t* event)
    {
        uint32_t index = eventCount;
        while (index > 0u && events[index - 1u]->time > event->time) {
            events[index] = events[index - 1u];
            --index;
        }
        events[index] = event;
        ++eventCount;
    }

    void addParam(clap_id id, double value, uint32_t time = 0u)
    {
        auto& event = params[paramCount++];
        event = {};
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
        insert(&event.header);
    }

    void addNote(int16_t channel, double velocity, uint32_t time = 0u)
    {
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = -1;
        event.port_index = 0;
        event.channel = channel;
        event.key = 60;
        event.velocity = velocity;
        insert(&event.header);
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
    const size_t count = std::min<size_t>(
        static_cast<size_t>(requested), 7u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const size_t available = state->position < state->bytes.size()
        ? state->bytes.size() - state->position : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 5u });
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
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

bool hasFeature(const clap_plugin_descriptor_t* descriptor,
    const char* expected)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* feature = descriptor->features; *feature; ++feature) {
        if (std::strcmp(*feature, expected) == 0) return true;
    }
    return false;
}

bool check(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

double getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value) ? value : -99999.0;
}

double processBlocks(const clap_plugin_t* plugin,
    EventList* firstEvents, uint32_t blockCount,
    std::array<double, kChannels>* channelEnergy = nullptr,
    int32_t excitationChannel = -1)
{
    std::array<std::array<float, kFrames>, kChannels> samples {};
    std::array<std::array<float, kFrames>, kChannels> inputSamples {};
    std::array<float*, kChannels> channels {};
    std::array<float*, kChannels> inputChannels {};
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        channels[channel] = samples[channel].data();
        inputChannels[channel] = inputSamples[channel].data();
    }
    clap_audio_buffer_t input {};
    input.data32 = inputChannels.data();
    input.channel_count = kChannels;
    clap_audio_buffer_t output {};
    output.data32 = channels.data();
    output.channel_count = kChannels;
    clap_process_t context {};
    context.frames_count = kFrames;
    context.audio_inputs = &input;
    context.audio_inputs_count = 1u;
    context.audio_outputs = &output;
    context.audio_outputs_count = 1u;
    double energy = 0.0;
    for (uint32_t block = 0u; block < blockCount; ++block) {
        for (auto& channel : samples) channel.fill(0.0f);
        for (auto& channel : inputSamples) channel.fill(0.0f);
        if (excitationChannel >= 0
            && excitationChannel < static_cast<int32_t>(kChannels)) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const uint32_t absoluteFrame = block * kFrames + frame;
                inputSamples[static_cast<uint32_t>(excitationChannel)][frame]
                    = std::sin(static_cast<double>(absoluteFrame)
                        * 2.0 * 3.14159265358979323846 * 173.0 / 48000.0)
                        * 0.25f;
            }
        }
        context.in_events = block == 0u && firstEvents
            ? &firstEvents->input : nullptr;
        if (plugin->process(plugin, &context) != CLAP_PROCESS_CONTINUE) {
            return -1.0;
        }
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t frame = 0u; frame < kFrames; ++frame) {
                const float sample = samples[channel][frame];
                if (!std::isfinite(sample) || std::abs(sample) > 1.0f) {
                    return -1.0;
                }
                const double squared = static_cast<double>(sample) * sample;
                energy += squared;
                if (channelEnergy) (*channelEnergy)[channel] += squared;
            }
        }
    }
    return energy;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: feedback_shift_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    bool ok = true;
    const auto binary = resolveBinary(argv[1]);
    ok &= check(!binary.empty(), "could not resolve CLAP binary");
    void* handle = binary.empty()
        ? nullptr : dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* error = dlerror();
        if (error) std::cerr << "dlopen: " << error << '\n';
    }
    ok &= check(handle != nullptr, "could not load CLAP binary");
    if (!handle) return 1;
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry"));
    ok &= check(entry != nullptr && entry->init(nullptr),
        "CLAP entry initialization failed");
    if (!ok) {
        dlclose(handle);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    ok &= check(factory && factory->get_plugin_count(factory) == 1u,
        "plugin factory contract failed");
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok &= check(descriptor && std::strcmp(descriptor->id, kPluginId) == 0
        && std::strcmp(descriptor->name,
            "s3g Processor Feedback Shift 8") == 0
        && std::strcmp(descriptor->version, "0.18.1") == 0,
        "plugin identity failed");
    ok &= check(hasFeature(descriptor, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_FREQUENCY_SHIFTER)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_MULTI_EFFECTS)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SURROUND)
        && !hasFeature(descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT)
        && !hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SYNTHESIZER),
        "audio processor feature tags failed");

    HostContext host;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "s3g feedback shift smoke";
    host.host.vendor = "s3g";
    host.host.url = "https://github.com/s3g/s3g-dsp";
    host.host.version = "1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = hostRequestRestart;
    host.host.request_process = hostRequestProcess;
    host.host.request_callback = hostRequestCallback;
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host.host, kPluginId) : nullptr;
    ok &= check(plugin && plugin->init(plugin), "plugin creation failed");
    if (!plugin) {
        entry->deinit();
        dlclose(handle);
        return 1;
    }

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    clap_audio_port_info_t inputInfo {};
    clap_audio_port_info_t outputInfo {};
    ok &= check(audioPorts && audioPorts->count(plugin, true) == 1u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, true, &inputInfo)
        && inputInfo.channel_count == kChannels
        && inputInfo.port_type
        && std::strcmp(inputInfo.port_type, CLAP_PORT_SURROUND) == 0
        && audioPorts->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && outputInfo.port_type
        && std::strcmp(outputInfo.port_type, CLAP_PORT_SURROUND) == 0,
        "eight-channel excitation/output port contract failed");
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    clap_note_port_info_t noteInfo {};
    ok &= check(notePorts && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u,
        "MIDI excitation port contract failed");

    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    ok &= check(params && params->count(plugin) == kParamCount,
        "parameter surface failed");
    if (params) {
        clap_param_info_t morphSource {};
        clap_param_info_t morphHold {};
        clap_param_info_t governorReflex {};
        clap_param_info_t governorSensitivity {};
        clap_param_info_t governorRecovery {};
        clap_param_info_t governorRest {};
        clap_param_info_t spliceAmount {};
        clap_param_info_t spliceRate {};
        clap_param_info_t spliceFine {};
        clap_param_info_t spliceContrast {};
        clap_param_info_t spliceSpace {};
        clap_param_info_t subBassTune {};
        clap_param_info_t subBassSustain {};
        clap_param_info_t run {};
        clap_param_info_t nodeMode {};
        clap_param_info_t exciterSource {};
        clap_param_info_t sceneAShift {};
        clap_param_info_t sceneABody {};
        clap_param_info_t sceneAAuxSend {};
        clap_param_info_t sceneBShift {};
        clap_param_info_t matrixA {};
        clap_param_info_t matrixB {};
        clap_param_info_t auxPress {};
        clap_param_info_t auxReturn {};
        clap_param_info_t grainCoherence {};
        clap_param_info_t grainSpacing {};
        clap_param_info_t grainShape {};
        ok &= check(params->get_info(plugin, 3u, &morphSource)
            && params->get_info(plugin, 7u, &morphHold)
            && params->get_info(plugin, 12u, &run)
            && params->get_info(plugin, 15u, &governorReflex)
            && params->get_info(plugin, 16u, &governorSensitivity)
            && params->get_info(plugin, 17u, &governorRecovery)
            && params->get_info(plugin, 18u, &governorRest)
            && params->get_info(plugin, 19u, &spliceAmount)
            && params->get_info(plugin, 20u, &spliceRate)
            && params->get_info(plugin, 21u, &spliceFine)
            && params->get_info(plugin, 22u, &spliceContrast)
            && params->get_info(plugin, 23u, &spliceSpace)
            && params->get_info(plugin, 24u, &subBassTune)
            && params->get_info(plugin, 28u, &subBassSustain)
            && params->get_info(plugin, 29u, &nodeMode)
            && params->get_info(plugin, 31u, &exciterSource)
            && params->get_info(plugin, 157u, &sceneAShift)
            && params->get_info(plugin, 160u, &sceneABody)
            && params->get_info(plugin, 162u, &sceneAAuxSend)
            && params->get_info(plugin, 205u, &sceneBShift)
            && params->get_info(plugin, 253u, &matrixA)
            && params->get_info(plugin, 317u, &matrixB)
            && params->get_info(plugin, 381u, &auxPress)
            && params->get_info(plugin, 391u, &auxReturn)
            && params->get_info(plugin, 393u, &grainCoherence)
            && params->get_info(plugin, 395u, &grainSpacing)
            && params->get_info(plugin, 396u, &grainShape),
            "ecology parameter indices were incomplete");
        ok &= check((morphSource.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && (morphHold.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && (run.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && (nodeMode.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && (exciterSource.flags & CLAP_PARAM_IS_STEPPED) != 0u
                && std::strcmp(morphSource.name, "Morph Driver") == 0
                && std::strcmp(morphSource.module, "Morph") == 0
                && std::strcmp(governorReflex.name, "Governor Reflex") == 0
                && std::strcmp(governorReflex.module, "Governor") == 0
                && std::strcmp(governorSensitivity.name,
                    "Governor Sensitivity") == 0
                && std::strcmp(governorRecovery.name,
                    "Governor Recovery") == 0
                && std::strcmp(governorRest.name, "Governor Rest") == 0
                && std::strcmp(spliceAmount.name, "Splice Amount") == 0
                && std::strcmp(spliceAmount.module, "Splice") == 0
                && std::strcmp(spliceRate.name, "Splice Range") == 0
                && std::strcmp(spliceFine.name, "Splice Fine") == 0
                && std::strcmp(spliceFine.module, "Splice") == 0
                && std::strcmp(spliceContrast.name, "Splice Contrast") == 0
                && std::strcmp(spliceSpace.name, "Splice Space") == 0
                && std::strcmp(subBassTune.name, "Sub Bass Tune") == 0
                && std::strcmp(subBassTune.module, "Sub Bass") == 0
                && std::strcmp(subBassSustain.name,
                    "Sub Bass Sustain") == 0
                && std::strcmp(sceneAShift.module, "Scene A / Node 1") == 0
                && std::strcmp(sceneBShift.module, "Scene B / Node 1") == 0
                && std::strcmp(sceneABody.name, "Body") == 0
                && std::strcmp(sceneAAuxSend.name, "AUX Send") == 0
                && std::strcmp(matrixA.module, "Scene A Matrix") == 0
                && std::strcmp(matrixB.module, "Scene B Matrix") == 0
                && std::strcmp(auxPress.module, "Feedback AUX") == 0
                && std::strcmp(auxReturn.name, "Return") == 0
                && std::strcmp(grainCoherence.module,
                    "Post Granulator") == 0
                && grainSpacing.id == 6014u
                && std::strcmp(grainSpacing.name, "Grain Spacing") == 0
                && grainShape.id == 6015u
                && std::strcmp(grainShape.name, "Grain Shape") == 0
                && (grainShape.flags & CLAP_PARAM_IS_STEPPED) != 0u,
            "scene, morph, or secondary processor metadata failed");
        ok &= check(std::abs(getParam(plugin, params, 3u)) < 1.0e-6
                && std::abs(getParam(plugin, params, 5u) - 1.0) < 1.0e-6
                && std::abs(getParam(plugin, params, 6u) - 0.34) < 1.0e-6
                && std::abs(getParam(plugin, params, 7u) - 0.32) < 1.0e-6
                && std::abs(getParam(plugin, params, 12u) + 18.0) < 1.0e-6
                && std::abs(getParam(plugin, params, 16u) - 0.32) < 1.0e-6
                && std::abs(getParam(plugin, params, 17u) - 0.28) < 1.0e-6
                && std::abs(getParam(plugin, params, 18u) - 0.48) < 1.0e-6
                && std::abs(getParam(plugin, params, 19u)) < 1.0e-6
                && std::abs(getParam(plugin, params, 20u) - 0.64) < 1.0e-6
                && std::abs(getParam(plugin, params, 21u) - 0.66) < 1.0e-6
                && std::abs(getParam(plugin, params, 22u)) < 1.0e-6
                && std::abs(getParam(plugin, params, 23u) - 0.82) < 1.0e-6
                && std::abs(getParam(plugin, params, 24u) - 0.30) < 1.0e-6
                && std::abs(getParam(plugin, params, 25u) - 0.34) < 1.0e-6
                && std::abs(getParam(plugin, params, 26u) - 0.18) < 1.0e-6
                && std::abs(getParam(plugin, params, 27u) - 0.28) < 1.0e-6
                && std::abs(getParam(plugin, params, 28u) - 0.56) < 1.0e-6
                && std::abs(getParam(plugin, params, 29u)) < 1.0e-6
                && std::abs(getParam(plugin, params, 1004u) - 0.52) < 1.0e-6
                && std::abs(getParam(plugin, params, 1008u) - 0.5) < 1.0e-6
                && std::abs(getParam(plugin, params, 2000u) + 720.0) < 1.0e-6
                && std::abs(getParam(plugin, params, 2001u) - 0.56) < 1.0e-6
                && std::abs(getParam(plugin, params, 2002u) + 0.24) < 1.0e-6
                && std::abs(getParam(plugin, params, 2003u) - 0.18) < 1.0e-6
                && std::abs(getParam(plugin, params, 2005u) - 1.0) < 1.0e-6
                && std::abs(getParam(plugin, params, 3000u) + 1.68) < 1.0e-5
                && std::abs(getParam(plugin, params, 3003u) - 0.54) < 1.0e-6
                && std::abs(getParam(plugin, params, 3005u) - 0.24) < 1.0e-6
                && std::abs(getParam(plugin, params, 4000u) - 0.72) < 1.0e-6
                && std::abs(getParam(plugin, params, 5000u) - 0.90) < 1.0e-6
                && std::abs(getParam(plugin, params, 6000u) - 0.28) < 1.0e-6
                && std::abs(getParam(plugin, params, 6010u)) < 1.0e-6
                && std::abs(getParam(plugin, params, 6011u)) < 1.0e-6
                && std::abs(getParam(plugin, params, 6012u) - 1.0) < 1.0e-6
                && std::abs(getParam(plugin, params, 6014u)) < 1.0e-6
                && std::abs(getParam(plugin, params, 6015u)) < 1.0e-6,
            "paired-scene defaults failed");
        char frequencyText[32] {};
        char colorText[32] {};
        char edgeDriverText[32] {};
        char spliceFineText[32] {};
        char grainSizeText[32] {};
        char grainSpacingText[32] {};
        char grainShapeText[32] {};
        ok &= check(params->value_to_text(plugin, 2000u, 0.125,
                frequencyText, sizeof(frequencyText))
            && std::strcmp(frequencyText, "+0.125 Hz") == 0
            && params->value_to_text(plugin, 2002u, -0.5,
                colorText, sizeof(colorText))
            && std::strcmp(colorText, "-0.50") == 0
            && params->value_to_text(plugin, 4u, 2.0,
                edgeDriverText, sizeof(edgeDriverText))
            && std::strcmp(edgeDriverText, "ECOLOGY EDGE") == 0
            && params->value_to_text(plugin, 22u, 1.0,
                spliceFineText, sizeof(spliceFineText))
            && std::strcmp(spliceFineText, "x2.000") == 0
            && params->value_to_text(plugin, 6004u, 0.0,
                grainSizeText, sizeof(grainSizeText))
            && std::strcmp(grainSizeText, "2.00 ms") == 0
            && params->value_to_text(plugin, 6014u, 0.0,
                grainSpacingText, sizeof(grainSpacingText))
            && std::strcmp(grainSpacingText, "0.00 ms") == 0
            && params->value_to_text(plugin, 6015u, 3.0,
                grainShapeText, sizeof(grainShapeText))
            && std::strcmp(grainShapeText, "DECAY") == 0,
            "sub-Hz, bipolar color, or AUX readout lost precision");
    }

    ok &= check(plugin->activate(plugin, 48000.0, 32u, kFrames)
        && plugin->start_processing(plugin), "activation failed");
    ok &= check(processBlocks(plugin, nullptr, 32u) > 1.0e-9,
        "default patch did not self-excite");

    EventList preciseCrosspoint;
    preciseCrosspoint.addParam(4001u, 0.137);
    (void)processBlocks(plugin, &preciseCrosspoint, 1u);
    ok &= check(std::abs(getParam(plugin, params, 4001u) - 0.137) < 1.0e-6,
        "continuous signed crosspoint gain lost precision");

    plugin->reset(plugin);
    EventList strike;
    strike.addParam(1u, 0.0);
    strike.addParam(12u, 0.0);
    strike.addNote(3, 1.0, 64u);
    std::array<double, kChannels> strikeChannels {};
    const double strikeEnergy = processBlocks(plugin, &strike, 8u,
        &strikeChannels);
    ok &= check(strikeEnergy > 1.0e-5 && strikeChannels[3u] > 1.0e-7,
        "MIDI channel 4 did not excite feedback node 4");

    plugin->reset(plugin);
    EventList externalExciter;
    externalExciter.addParam(1u, 0.0);
    for (uint32_t node = 0u; node < kChannels; ++node) {
        externalExciter.addParam(1002u + node * 16u,
            kExternalExciterSource);
    }
    externalExciter.addParam(1034u, kExternalExciterSource);
    externalExciter.addParam(1035u, 0.0);
    std::array<double, kChannels> externalChannels {};
    const double externalEnergy = processBlocks(plugin, &externalExciter,
        16u, &externalChannels, 2);
    plugin->reset(plugin);
    EventList externalOff;
    externalOff.addParam(1u, 0.0);
    for (uint32_t node = 0u; node < kChannels; ++node) {
        externalOff.addParam(1002u + node * 16u,
            kOffExciterSource);
    }
    const double sourceOffEnergy = processBlocks(plugin, &externalOff,
        16u, nullptr, 2);
    if (!(externalEnergy > 1.0e-5
            && externalChannels[2u] > 1.0e-7
            && externalEnergy > sourceOffEnergy * 20.0)) {
        std::cerr << "external energy=" << externalEnergy
            << " channel3=" << externalChannels[2u]
            << " silent-input energy=" << sourceOffEnergy << '\n';
    }
    ok &= check(externalEnergy > 1.0e-5
            && externalChannels[2u] > 1.0e-7
            && externalEnergy > sourceOffEnergy * 20.0,
        "eight-channel external input did not excite its selected node");
    EventList restoreSources;
    for (uint32_t node = 0u; node < kChannels; ++node) {
        restoreSources.addParam(1002u + node * 16u, 0.0);
    }
    (void)processBlocks(plugin, &restoreSources, 1u);

    plugin->reset(plugin);
    EventList quadFold;
    quadFold.addParam(14u, 1.0);
    quadFold.addParam(15u, 37.0);
    quadFold.addNote(-1, 1.0);
    std::array<double, kChannels> quadChannels {};
    ok &= check(processBlocks(plugin, &quadFold, 8u, &quadChannels) > 1.0e-7
        && quadChannels[0u] + quadChannels[1u]
            + quadChannels[2u] + quadChannels[3u] > 1.0e-8
        && quadChannels[4u] + quadChannels[5u]
            + quadChannels[6u] + quadChannels[7u] < 1.0e-20,
        "quad ring fold did not use outputs 1-4 exclusively");

    plugin->reset(plugin);
    EventList stereoFold;
    stereoFold.addParam(14u, 2.0);
    stereoFold.addParam(15u, -51.0);
    stereoFold.addNote(-1, 1.0);
    std::array<double, kChannels> stereoChannels {};
    ok &= check(processBlocks(plugin, &stereoFold, 8u, &stereoChannels)
            > 1.0e-7
        && stereoChannels[0u] + stereoChannels[1u] > 1.0e-8
        && stereoChannels[2u] + stereoChannels[3u]
            + stereoChannels[4u] + stereoChannels[5u]
            + stereoChannels[6u] + stereoChannels[7u] < 1.0e-20,
        "stereo ring fold did not use outputs 1-2 exclusively");

    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    EventList ecologyChange;
    ecologyChange.addParam(3u, 0.63);
    ecologyChange.addParam(12u, -3.0);
    ecologyChange.addParam(16u, 0.81);
    ecologyChange.addParam(17u, 0.66);
    ecologyChange.addParam(18u, 0.73);
    ecologyChange.addParam(19u, 0.58);
    ecologyChange.addParam(2003u, 0.70);
    ecologyChange.addParam(3000u, 123.4);
    ecologyChange.addParam(5001u, -0.41);
    ecologyChange.addParam(6012u, 0.37);
    ecologyChange.addParam(6014u, 0.42);
    ecologyChange.addParam(6015u, 3.0);
    (void)processBlocks(plugin, &ecologyChange, 1u);
    MemoryState memory;
    clap_ostream_t streamOut { &memory, stateWrite };
    ok &= check(state && state->save(plugin, &streamOut),
        "paired-ecology state save failed");
    EventList ecologyAway;
    ecologyAway.addParam(3u, 0.0);
    ecologyAway.addParam(12u, -40.0);
    ecologyAway.addParam(16u, 0.0);
    ecologyAway.addParam(17u, 0.0);
    ecologyAway.addParam(18u, 0.0);
    ecologyAway.addParam(19u, 0.0);
    ecologyAway.addParam(2003u, 0.05);
    ecologyAway.addParam(3000u, -5000.0);
    ecologyAway.addParam(5001u, 0.0);
    ecologyAway.addParam(6012u, 1.0);
    ecologyAway.addParam(6014u, 0.0);
    ecologyAway.addParam(6015u, 0.0);
    (void)processBlocks(plugin, &ecologyAway, 1u);
    memory.position = 0u;
    clap_istream_t streamIn { &memory, stateRead };
    ok &= check(state && state->load(plugin, &streamIn)
        && std::abs(getParam(plugin, params, 3u) - 0.63) < 1.0e-6
        && std::abs(getParam(plugin, params, 12u) + 3.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 16u) - 0.81) < 1.0e-6
        && std::abs(getParam(plugin, params, 17u) - 0.66) < 1.0e-6
        && std::abs(getParam(plugin, params, 18u) - 0.73) < 1.0e-6
        && std::abs(getParam(plugin, params, 19u) - 0.58) < 1.0e-6
        && std::abs(getParam(plugin, params, 2003u) - 0.70) < 1.0e-6
        && std::abs(getParam(plugin, params, 3000u) - 123.4) < 1.0e-6
        && std::abs(getParam(plugin, params, 5001u) + 0.41) < 1.0e-6
        && std::abs(getParam(plugin, params, 6012u) - 0.37) < 1.0e-6
        && std::abs(getParam(plugin, params, 6014u) - 0.42) < 1.0e-6
        && std::abs(getParam(plugin, params, 6015u) - 3.0) < 1.0e-6,
        "paired-ecology state round trip failed");

    MemoryState obsolete = memory;
    const uint32_t oldMagic = 0x46533353u;
    std::memcpy(obsolete.bytes.data(), &oldMagic, sizeof(oldMagic));
    obsolete.position = 0u;
    clap_istream_t obsoleteStream { &obsolete, stateRead };
    ok &= check(state && !state->load(plugin, &obsoleteStream),
        "obsolete Feedback Shift state was not rejected");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);
    if (ok) std::cout << "feedback shift CLAP smoke tests passed\n";
    return ok ? 0 : 1;
}
