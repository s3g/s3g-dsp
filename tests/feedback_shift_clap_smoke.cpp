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
constexpr uint32_t kLegacyParamCount = 219u;
constexpr uint32_t kPreviousParamCount = 230u;
constexpr uint32_t kFeedbackSourceParamCount = 231u;
constexpr uint32_t kLaneSendParamCount = 239u;
constexpr uint32_t kPostGranulatorParamCount = 241u;
constexpr uint32_t kParamCount = 290u;

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
    std::array<clap_event_param_value_t, 20u> params {};
    std::array<clap_event_note_t, 4u> notes {};
    std::array<const clap_event_header_t*, 24u> events {};
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
            "s3g Processor Feedback Shift") == 0
        && std::strcmp(descriptor->version, "0.12.0") == 0,
        "plugin identity failed");
    ok &= check(hasFeature(descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SYNTHESIZER)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT)
        && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SURROUND),
        "instrument feature tags failed");

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
        clap_param_info_t pulseSync {};
        clap_param_info_t nodeMode {};
        clap_param_info_t pedalAmount {};
        clap_param_info_t pedalExtra {};
        clap_param_info_t run {};
        clap_param_info_t outputMode {};
        clap_param_info_t outputRotation {};
        clap_param_info_t auxPress {};
        clap_param_info_t auxReturn {};
        clap_param_info_t auxGrainMix {};
        clap_param_info_t auxSend1 {};
        clap_param_info_t auxSend8 {};
        clap_param_info_t grainCoherence {};
        clap_param_info_t grainLaneDrift {};
        clap_param_info_t exciterSource1 {};
        clap_param_info_t exciterGain8 {};
        clap_param_info_t motionSource1 {};
        clap_param_info_t motionTarget1 {};
        clap_param_info_t motionDepth8 {};
        clap_param_info_t motionSlew8 {};
        clap_param_info_t motionRate {};
        ok &= check(params->get_info(plugin, 4u, &pulseSync)
            && params->get_info(plugin, 8u, &run)
            && params->get_info(plugin, 9u, &outputMode)
            && params->get_info(plugin, 10u, &outputRotation)
            && params->get_info(plugin, 11u, &nodeMode)
            && params->get_info(plugin, 17u, &pedalAmount)
            && params->get_info(plugin, 21u, &pedalExtra)
            && params->get_info(plugin, 219u, &auxPress)
            && params->get_info(plugin, 229u, &auxReturn)
            && params->get_info(plugin, 230u, &auxGrainMix)
            && params->get_info(plugin, 231u, &auxSend1)
            && params->get_info(plugin, 238u, &auxSend8)
            && params->get_info(plugin, 239u, &grainCoherence)
            && params->get_info(plugin, 240u, &grainLaneDrift)
            && params->get_info(plugin, 241u, &exciterSource1)
            && params->get_info(plugin, 256u, &exciterGain8)
            && params->get_info(plugin, 257u, &motionSource1)
            && params->get_info(plugin, 258u, &motionTarget1)
            && params->get_info(plugin, 287u, &motionDepth8)
            && params->get_info(plugin, 288u, &motionSlew8)
            && params->get_info(plugin, 289u, &motionRate)
            && (pulseSync.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && (run.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && (outputMode.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && outputMode.id == 10u && outputRotation.id == 11u,
            "stepped parameter flags failed");
        ok &= check((nodeMode.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && std::abs(getParam(plugin, params, 1002u) + 720.0) < 1.0e-6
            && pedalAmount.id == 1006u
            && pedalExtra.id == 1010u
            && std::abs(getParam(plugin, params, 1006u) - 0.52) < 1.0e-6
            && std::abs(getParam(plugin, params, 1010u) - 0.5) < 1.0e-6
            && std::abs(getParam(plugin, params, 10u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 11u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 2000u) - 0.86) < 1.0e-6,
            "node or patch-matrix defaults failed");
        ok &= check(auxPress.id == 3000u && auxReturn.id == 3010u
            && auxGrainMix.id == 3011u
            && std::strcmp(auxPress.module, "Feedback AUX") == 0
            && std::strcmp(auxReturn.name, "Return") == 0
            && std::strcmp(auxGrainMix.name, "Grain Mix") == 0
            && std::strcmp(auxGrainMix.module, "Post Granulator") == 0
            && auxSend1.id == 3012u && auxSend8.id == 3019u
            && std::strcmp(auxSend1.module, "AUX Sends") == 0
            && std::strcmp(auxSend8.name, "Node 8 Send") == 0
            && grainCoherence.id == 3020u
            && grainLaneDrift.id == 3021u
            && std::strcmp(grainCoherence.module, "Post Granulator") == 0
            && std::strcmp(grainLaneDrift.name, "Grain Lane Drift") == 0
            && std::abs(getParam(plugin, params, 3000u) - 0.28) < 1.0e-6
            && std::abs(getParam(plugin, params, 3010u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 3011u) - 1.0) < 1.0e-6
            && std::abs(getParam(plugin, params, 3012u) - 1.0) < 1.0e-6
            && std::abs(getParam(plugin, params, 3019u) - 1.0) < 1.0e-6
            && std::abs(getParam(plugin, params, 3020u) - 1.0) < 1.0e-6
            && std::abs(getParam(plugin, params, 3021u) - 0.5) < 1.0e-6
            && exciterSource1.id == 4000u
            && exciterGain8.id == 4015u
            && motionSource1.id == 5000u
            && motionTarget1.id == 5001u
            && motionDepth8.id == 5030u
            && motionSlew8.id == 5031u
            && motionRate.id == 6000u
            && (exciterSource1.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && (motionSource1.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && (motionTarget1.flags & CLAP_PARAM_IS_STEPPED) != 0u
            && std::strcmp(exciterSource1.module, "Node 1 Exciter") == 0
            && std::strcmp(motionSource1.module, "Node 1 Motion") == 0
            && std::strcmp(motionRate.module, "Motion") == 0
            && std::abs(getParam(plugin, params, 4000u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 4015u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 5000u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 5001u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 5030u)) < 1.0e-6
            && std::abs(getParam(plugin, params, 5031u) - 0.45) < 1.0e-6
            && std::abs(getParam(plugin, params, 6000u) - 0.34) < 1.0e-6,
            "AUX parameter surface or transparent default failed");
        char frequencyText[32] {};
        char grainSizeText[32] {};
        ok &= check(params->value_to_text(plugin, 1002u, 0.125,
                frequencyText, sizeof(frequencyText))
            && std::strcmp(frequencyText, "+0.125 Hz") == 0
            && params->value_to_text(plugin, 3004u, 0.0,
                grainSizeText, sizeof(grainSizeText))
            && std::strcmp(grainSizeText, "2.00 ms") == 0,
            "sub-Hz or AUX grain readout lost precision");
    }

    ok &= check(plugin->activate(plugin, 48000.0, 32u, kFrames)
        && plugin->start_processing(plugin), "activation failed");
    const double selfExcitedEnergy = processBlocks(plugin, nullptr, 32u);
    ok &= check(selfExcitedEnergy > 1.0e-9,
        "default patch did not self-excite");

    EventList preciseCrosspoint;
    preciseCrosspoint.addParam(2001u, 0.137);
    (void)processBlocks(plugin, &preciseCrosspoint, 1u);
    ok &= check(std::abs(getParam(plugin, params, 2001u) - 0.137) < 1.0e-6,
        "continuous signed crosspoint gain lost precision");

    plugin->reset(plugin);
    EventList strike;
    strike.addParam(1u, 0.0); // continuous excitation
    strike.addParam(8u, 0.0); // output gain
    strike.addNote(3, 1.0, 64u);
    std::array<double, kChannels> strikeChannels {};
    const double strikeEnergy = processBlocks(plugin, &strike, 8u,
        &strikeChannels);
    ok &= check(strikeEnergy > 1.0e-5,
        "MIDI note did not excite the feedback instrument");
    ok &= check(strikeChannels[3u] > 1.0e-7,
        "MIDI channel 4 did not address feedback node 4");

    plugin->reset(plugin);
    EventList externalExciter;
    externalExciter.addParam(1u, 0.0); // suppress continuous internal drive
    for (uint32_t node = 0u; node < kChannels; ++node) {
        externalExciter.addParam(4000u + node * 2u, 6.0);
    }
    externalExciter.addParam(4004u, 4.0); // node 3 external source
    externalExciter.addParam(4005u, 0.0); // unity input gain
    std::array<double, kChannels> externalChannels {};
    const double externalEnergy = processBlocks(plugin, &externalExciter,
        16u, &externalChannels, 2);
    plugin->reset(plugin);
    EventList externalOff;
    externalOff.addParam(1u, 0.0);
    for (uint32_t node = 0u; node < kChannels; ++node) {
        externalOff.addParam(4000u + node * 2u, 6.0);
    }
    const double sourceOffEnergy = processBlocks(plugin, &externalOff,
        16u, nullptr, 2);
    const bool externalExcitationWorked = externalEnergy > 1.0e-5
            && externalChannels[2u] > 1.0e-7
            && externalEnergy > sourceOffEnergy * 20.0;
    if (!externalExcitationWorked) {
        std::cerr << "external excitation energy=" << externalEnergy
                  << ", node3=" << externalChannels[2u]
                  << ", off=" << sourceOffEnergy << '\n';
    }
    ok &= check(externalExcitationWorked,
        "eight-channel external input did not excite its selected node");
    EventList restoreSources;
    for (uint32_t node = 0u; node < kChannels; ++node) {
        restoreSources.addParam(4000u + node * 2u, 0.0);
    }
    (void)processBlocks(plugin, &restoreSources, 1u);

    plugin->reset(plugin);
    EventList quadFold;
    quadFold.addParam(10u, 1.0); // quad ring projection
    quadFold.addParam(11u, 37.0); // channel-ring rotation
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
    stereoFold.addParam(10u, 2.0); // stereo ring projection
    stereoFold.addParam(11u, -51.0);
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
    EventList outputChange;
    outputChange.addParam(8u, -3.0);
    outputChange.addParam(3004u, 0.41);
    outputChange.addParam(3010u, 0.73);
    outputChange.addParam(3011u, 0.29);
    outputChange.addParam(3012u, 0.33);
    outputChange.addParam(3019u, 0.77);
    outputChange.addParam(3020u, 0.37);
    outputChange.addParam(3021u, 0.82);
    outputChange.addParam(4000u, 4.0);
    outputChange.addParam(4015u, -6.0);
    outputChange.addParam(5000u, 2.0);
    outputChange.addParam(5001u, 2.0);
    outputChange.addParam(5002u, 0.45);
    outputChange.addParam(5003u, 0.71);
    outputChange.addParam(6000u, 0.66);
    (void)processBlocks(plugin, &outputChange, 1u);
    MemoryState memory;
    clap_ostream_t streamOut { &memory, stateWrite };
    ok &= check(state && state->save(plugin, &streamOut),
        "state save failed");
    EventList outputAway;
    outputAway.addParam(8u, -40.0);
    (void)processBlocks(plugin, &outputAway, 1u);
    memory.position = 0u;
    clap_istream_t streamIn { &memory, stateRead };
    ok &= check(state && state->load(plugin, &streamIn)
        && std::abs(getParam(plugin, params, 8u) + 3.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3004u) - 0.41) < 1.0e-6
        && std::abs(getParam(plugin, params, 3010u) - 0.73) < 1.0e-6
        && std::abs(getParam(plugin, params, 3011u) - 0.29) < 1.0e-6
        && std::abs(getParam(plugin, params, 3012u) - 0.33) < 1.0e-6
        && std::abs(getParam(plugin, params, 3019u) - 0.77) < 1.0e-6
        && std::abs(getParam(plugin, params, 3020u) - 0.37) < 1.0e-6
        && std::abs(getParam(plugin, params, 3021u) - 0.82) < 1.0e-6
        && std::abs(getParam(plugin, params, 4000u) - 4.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 4015u) + 6.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 5000u) - 2.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 5001u) - 2.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 5002u) - 0.45) < 1.0e-6
        && std::abs(getParam(plugin, params, 5003u) - 0.71) < 1.0e-6
        && std::abs(getParam(plugin, params, 6000u) - 0.66) < 1.0e-6,
        "state round trip failed");

    MemoryState postGranulator = memory;
    const uint32_t postGranulatorVersion = 9u;
    const uint32_t postGranulatorCount = kPostGranulatorParamCount;
    std::memcpy(postGranulator.bytes.data() + 4u, &postGranulatorVersion,
        sizeof(postGranulatorVersion));
    std::memcpy(postGranulator.bytes.data() + 8u, &postGranulatorCount,
        sizeof(postGranulatorCount));
    postGranulator.bytes.resize(
        16u + sizeof(double) * kPostGranulatorParamCount);
    EventList postGranulatorAway;
    postGranulatorAway.addParam(4000u, 6.0);
    postGranulatorAway.addParam(4015u, 9.0);
    postGranulatorAway.addParam(5000u, 4.0);
    postGranulatorAway.addParam(5001u, 4.0);
    postGranulatorAway.addParam(5002u, -0.9);
    postGranulatorAway.addParam(5003u, 0.05);
    postGranulatorAway.addParam(6000u, 0.95);
    (void)processBlocks(plugin, &postGranulatorAway, 1u);
    postGranulator.position = 0u;
    clap_istream_t postGranulatorStream { &postGranulator, stateRead };
    ok &= check(state && state->load(plugin, &postGranulatorStream)
        && std::abs(getParam(plugin, params, 3012u) - 0.33) < 1.0e-6
        && std::abs(getParam(plugin, params, 3019u) - 0.77) < 1.0e-6
        && std::abs(getParam(plugin, params, 4000u)) < 1.0e-6
        && std::abs(getParam(plugin, params, 4015u)) < 1.0e-6
        && std::abs(getParam(plugin, params, 5000u)) < 1.0e-6
        && std::abs(getParam(plugin, params, 5001u)) < 1.0e-6
        && std::abs(getParam(plugin, params, 5002u)) < 1.0e-6
        && std::abs(getParam(plugin, params, 5003u) - 0.45) < 1.0e-6
        && std::abs(getParam(plugin, params, 6000u) - 0.34) < 1.0e-6,
        "version-9 state did not add transparent exciter/motion defaults");

    MemoryState laneSend = memory;
    const uint32_t laneSendVersion = 8u;
    const uint32_t laneSendCount = kLaneSendParamCount;
    std::memcpy(laneSend.bytes.data() + 4u, &laneSendVersion,
        sizeof(laneSendVersion));
    std::memcpy(laneSend.bytes.data() + 8u, &laneSendCount,
        sizeof(laneSendCount));
    laneSend.bytes.resize(16u + sizeof(double) * kLaneSendParamCount);
    EventList laneSendAway;
    laneSendAway.addParam(3012u, 0.05);
    laneSendAway.addParam(3019u, 0.05);
    laneSendAway.addParam(3020u, 0.05);
    laneSendAway.addParam(3021u, 0.05);
    (void)processBlocks(plugin, &laneSendAway, 1u);
    laneSend.position = 0u;
    clap_istream_t laneSendStream { &laneSend, stateRead };
    ok &= check(state && state->load(plugin, &laneSendStream)
        && std::abs(getParam(plugin, params, 3012u) - 0.33) < 1.0e-6
        && std::abs(getParam(plugin, params, 3019u) - 0.77) < 1.0e-6
        && std::abs(getParam(plugin, params, 3020u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3021u) - 0.5) < 1.0e-6,
        "version-8 lane-send state did not add safe grain-lane defaults");

    MemoryState feedbackSource = memory;
    const uint32_t feedbackSourceVersion = 7u;
    const uint32_t feedbackSourceCount = kFeedbackSourceParamCount;
    std::memcpy(feedbackSource.bytes.data() + 4u, &feedbackSourceVersion,
        sizeof(feedbackSourceVersion));
    std::memcpy(feedbackSource.bytes.data() + 8u, &feedbackSourceCount,
        sizeof(feedbackSourceCount));
    feedbackSource.bytes.resize(
        16u + sizeof(double) * kFeedbackSourceParamCount);
    EventList feedbackSourceAway;
    feedbackSourceAway.addParam(3011u, 0.05);
    feedbackSourceAway.addParam(3012u, 0.05);
    feedbackSourceAway.addParam(3019u, 0.05);
    feedbackSourceAway.addParam(3020u, 0.05);
    feedbackSourceAway.addParam(3021u, 0.05);
    (void)processBlocks(plugin, &feedbackSourceAway, 1u);
    feedbackSource.position = 0u;
    clap_istream_t feedbackSourceStream { &feedbackSource, stateRead };
    ok &= check(state && state->load(plugin, &feedbackSourceStream)
        && std::abs(getParam(plugin, params, 3011u) - 0.29) < 1.0e-6
        && std::abs(getParam(plugin, params, 3012u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3019u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3020u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3021u) - 0.5) < 1.0e-6,
        "version-7 source-return state did not add unity lane sends");

    MemoryState granular = memory;
    const uint32_t granularVersion = 6u;
    const uint32_t previousCount = kPreviousParamCount;
    std::memcpy(granular.bytes.data() + 4u, &granularVersion,
        sizeof(granularVersion));
    std::memcpy(granular.bytes.data() + 8u, &previousCount,
        sizeof(previousCount));
    granular.bytes.resize(16u + sizeof(double) * kPreviousParamCount);
    EventList granularAway;
    granularAway.addParam(3004u, 0.95);
    granularAway.addParam(3010u, 0.95);
    granularAway.addParam(3011u, 0.05);
    granularAway.addParam(3012u, 0.05);
    granularAway.addParam(3019u, 0.05);
    granularAway.addParam(3020u, 0.05);
    granularAway.addParam(3021u, 0.05);
    (void)processBlocks(plugin, &granularAway, 1u);
    granular.position = 0u;
    clap_istream_t granularStream { &granular, stateRead };
    ok &= check(state && state->load(plugin, &granularStream)
        && std::abs(getParam(plugin, params, 3004u) - 0.41) < 1.0e-6
        && std::abs(getParam(plugin, params, 3010u) - 0.73) < 1.0e-6
        && std::abs(getParam(plugin, params, 3011u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3012u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3019u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3020u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3021u) - 0.5) < 1.0e-6,
        "version-6 granular state did not add the grain mix default");

    MemoryState holdCut = memory;
    const uint32_t holdCutVersion = 5u;
    std::memcpy(holdCut.bytes.data() + 4u, &holdCutVersion,
        sizeof(holdCutVersion));
    std::memcpy(holdCut.bytes.data() + 8u, &previousCount,
        sizeof(previousCount));
    holdCut.bytes.resize(16u + sizeof(double) * kPreviousParamCount);
    EventList holdCutAway;
    holdCutAway.addParam(3004u, 0.95);
    holdCutAway.addParam(3010u, 0.95);
    holdCutAway.addParam(3011u, 0.05);
    holdCutAway.addParam(3012u, 0.05);
    holdCutAway.addParam(3020u, 0.05);
    holdCutAway.addParam(3021u, 0.05);
    (void)processBlocks(plugin, &holdCutAway, 1u);
    holdCut.position = 0u;
    clap_istream_t holdCutStream { &holdCut, stateRead };
    ok &= check(state && state->load(plugin, &holdCutStream)
        && std::abs(getParam(plugin, params, 3004u) - 0.46) < 1.0e-6
        && std::abs(getParam(plugin, params, 3010u)) < 1.0e-6
        && std::abs(getParam(plugin, params, 3011u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3012u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3020u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3021u) - 0.5) < 1.0e-6,
        "version-5 hold/cut state did not migrate to safe grain defaults");

    MemoryState legacy = memory;
    const uint32_t legacyVersion = 4u;
    const uint32_t legacyCount = kLegacyParamCount;
    std::memcpy(legacy.bytes.data() + 4u, &legacyVersion,
        sizeof(legacyVersion));
    std::memcpy(legacy.bytes.data() + 8u, &legacyCount,
        sizeof(legacyCount));
    legacy.bytes.resize(16u + sizeof(double) * kLegacyParamCount);
    EventList auxAway;
    auxAway.addParam(3000u, 0.95);
    auxAway.addParam(3010u, 0.95);
    auxAway.addParam(3011u, 0.05);
    auxAway.addParam(3012u, 0.05);
    auxAway.addParam(3020u, 0.05);
    auxAway.addParam(3021u, 0.05);
    (void)processBlocks(plugin, &auxAway, 1u);
    legacy.position = 0u;
    clap_istream_t legacyStream { &legacy, stateRead };
    ok &= check(state && state->load(plugin, &legacyStream)
        && std::abs(getParam(plugin, params, 3000u) - 0.28) < 1.0e-6
        && std::abs(getParam(plugin, params, 3010u)) < 1.0e-6
        && std::abs(getParam(plugin, params, 3011u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3012u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3020u) - 1.0) < 1.0e-6
        && std::abs(getParam(plugin, params, 3021u) - 0.5) < 1.0e-6,
        "version-4 state did not migrate with a transparent AUX default");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);
    if (ok) std::cout << "feedback shift CLAP smoke tests passed\n";
    return ok ? 0 : 1;
}
