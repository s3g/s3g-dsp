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
#include <string>
#include <vector>

namespace {

constexpr uint32_t kFrames = 256u;
constexpr uint32_t kMaxChannels = 8u;

struct Host {
    clap_host_t host {};
    clap_host_params_t params {};
    uint32_t rescans = 0u;
    uint32_t callbacks = 0u;
};

Host* context(const clap_host_t* host)
{
    return static_cast<Host*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    return id && std::strcmp(id, CLAP_EXT_PARAMS) == 0
        ? &context(host)->params : nullptr;
}

void hostRescan(const clap_host_t* host, clap_param_rescan_flags flags)
{
    if ((flags & CLAP_PARAM_RESCAN_VALUES) != 0u) {
        ++context(host)->rescans;
    }
}

void noRestart(const clap_host_t*) {}
void noProcess(const clap_host_t*) {}
void requestCallback(const clap_host_t* host)
{
    ++context(host)->callbacks;
}
void noClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void noFlush(const clap_host_t*) {}

struct Events {
    std::array<clap_event_param_value_t, 16u> params {};
    std::array<clap_event_note_t, 4u> notes {};
    std::array<const clap_event_header_t*, 20u> ordered {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t count = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const Events*>(list->ctx)->count;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* events = static_cast<const Events*>(list->ctx);
            return index < events->count ? events->ordered[index] : nullptr;
        },
    };

    bool insert(const clap_event_header_t* event)
    {
        if (!event || count >= ordered.size()) return false;
        uint32_t index = count;
        while (index > 0u && ordered[index - 1u]->time > event->time) {
            ordered[index] = ordered[index - 1u];
            --index;
        }
        ordered[index] = event;
        ++count;
        return true;
    }

    bool addParam(clap_id id, double value, uint32_t time = 0u)
    {
        if (paramCount >= params.size()) return false;
        auto& event = params[paramCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = event.port_index = event.channel = event.key = -1;
        event.value = value;
        return insert(&event.header);
    }

    bool addNote(int16_t key, double velocity, uint32_t time = 0u)
    {
        if (noteCount >= notes.size()) return false;
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = 42;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        return insert(&event.header);
    }
};

struct Audio {
    std::array<std::array<float, kFrames>, 2u> inputStorage {};
    std::array<std::array<float, kFrames>, kMaxChannels> outputStorage {};
    std::array<float*, 2u> inputPointers {};
    std::array<float*, kMaxChannels> outputPointers {};
    clap_audio_buffer_t input {};
    clap_audio_buffer_t output {};
    uint32_t channels = 2u;
    uint64_t frameOffset = 0u;

    explicit Audio(uint32_t outputChannels) : channels(outputChannels)
    {
        for (uint32_t channel = 0u; channel < inputPointers.size(); ++channel) {
            inputPointers[channel] = inputStorage[channel].data();
        }
        for (uint32_t channel = 0u; channel < outputPointers.size(); ++channel) {
            outputPointers[channel] = outputStorage[channel].data();
        }
        input.data32 = inputPointers.data();
        input.channel_count = 2u;
        output.data32 = outputPointers.data();
        output.channel_count = channels;
    }

    void prepareInput(bool enabled)
    {
        constexpr double twoPi = 6.28318530717958647692;
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            const double time = static_cast<double>(frameOffset + frame)
                / 48000.0;
            inputStorage[0u][frame] = enabled
                ? 0.2f * static_cast<float>(std::sin(twoPi * 173.0 * time))
                : 0.0f;
            inputStorage[1u][frame] = enabled
                ? 0.17f * static_cast<float>(std::sin(twoPi * 281.0 * time))
                : 0.0f;
        }
        frameOffset += kFrames;
        for (auto& channel : outputStorage) channel.fill(0.0f);
    }
};

struct MemoryState {
    std::vector<uint8_t> bytes;
    size_t offset = 0u;
};

struct FissureStateHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t liveValueCount;
    uint32_t sceneValueCount;
};

int64_t stateWrite(const clap_ostream_t* stream,
    const void* source, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!source && requested > 0u)) return -1;
    const size_t count = std::min<size_t>(requested, 7u);
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
        available, static_cast<size_t>(requested), 5u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
        state->offset += count;
    }
    return static_cast<int64_t>(count);
}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    std::error_code error;
    if (std::filesystem::is_regular_file(supplied, error)) return supplied;
#if defined(__APPLE__)
    error.clear();
    if (std::filesystem::is_directory(supplied, error)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (std::filesystem::directory_iterator it(directory, error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error) && !error) return it->path();
        }
    }
#endif
    return {};
}

clap_process_status processBlock(const clap_plugin_t* plugin,
    Audio& audio, bool useInput, const clap_input_events_t* events = nullptr)
{
    audio.prepareInput(useInput);
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = kFrames;
    process.audio_inputs = &audio.input;
    process.audio_inputs_count = 1u;
    process.audio_outputs = &audio.output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process);
}

double energy(const Audio& audio)
{
    double result = 0.0;
    for (uint32_t channel = 0u; channel < audio.channels; ++channel) {
        for (float value : audio.outputStorage[channel]) {
            if (!std::isfinite(value) || std::abs(value) > 1.001f) {
                return -1.0;
            }
            result += static_cast<double>(value) * value;
        }
    }
    return result;
}

double channelEnergy(const Audio& audio, uint32_t channel)
{
    if (channel >= audio.channels) return -1.0;
    double result = 0.0;
    for (float value : audio.outputStorage[channel]) {
        if (!std::isfinite(value) || std::abs(value) > 1.001f) return -1.0;
        result += static_cast<double>(value) * value;
    }
    return result;
}

bool flush(const clap_plugin_t* plugin, const clap_plugin_params_t* params,
    std::initializer_list<std::pair<clap_id, double>> values)
{
    Events events;
    for (const auto& item : values) {
        if (!events.addParam(item.first, item.second)) return false;
    }
    params->flush(plugin, &events.input, nullptr);
    return true;
}

bool hasFeature(const clap_plugin_descriptor_t* descriptor,
    const char* expected)
{
    if (!descriptor || !descriptor->features) return false;
    for (const char* const* feature = descriptor->features; *feature;
         ++feature) {
        if (std::strcmp(*feature, expected) == 0) return true;
    }
    return false;
}

bool check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: processor_fissure_clap_smoke "
            << "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    constexpr const char* pluginId =
        "org.s3g.s3g-dsp.processor-fissure";
    if (binary.empty()) {
        std::cerr << "could not resolve CLAP binary\n";
        return 2;
    }
    void* library = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        std::cerr << "dlopen failed: " << dlerror() << '\n';
        return 2;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = check(entry != nullptr, "missing clap_entry");
    if (!entry) {
        dlclose(library);
        return 1;
    }
    ok &= check(entry->init(binary.c_str()), "entry init failed");
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    ok &= check(factory && factory->get_plugin_count(factory) == 1u,
        "factory did not expose one plug-in");
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    ok &= check(descriptor
            && std::strcmp(descriptor->id, pluginId) == 0
            && std::strcmp(descriptor->name, "s3g Processor Fissure 8") == 0
            && std::strcmp(descriptor->version, "0.10.5") == 0,
        "descriptor identity, host name, or version is wrong");
    ok &= check(hasFeature(descriptor, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT)
            && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_MULTI_EFFECTS)
            && hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SURROUND)
            && !hasFeature(descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT)
            && !hasFeature(descriptor, CLAP_PLUGIN_FEATURE_SYNTHESIZER),
        "Fissure must be classified as a CLAP effect, not CLAPi");

    Host host;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "Processor Fissure smoke";
    host.host.vendor = "s3g tests";
    host.host.url = "";
    host.host.version = "1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = noRestart;
    host.host.request_process = noProcess;
    host.host.request_callback = requestCallback;
    host.params.rescan = hostRescan;
    host.params.clear = noClear;
    host.params.request_flush = noFlush;

    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host.host, pluginId)
        : nullptr;
    ok &= check(plugin != nullptr, "factory rejected the expected plug-in ID");
    if (!plugin) {
        entry->deinit();
        dlclose(library);
        return 1;
    }
    ok &= check(plugin->init(plugin), "plug-in init failed");

    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    clap_audio_port_info_t portInfo {};
    ok &= check(ports && ports->count(plugin, true) == 1u
            && ports->get(plugin, 0u, true, &portInfo)
            && portInfo.channel_count == 2u,
        "optional stereo input port contract is wrong");
    portInfo = {};
    ok &= check(ports && ports->count(plugin, false) == 1u
            && ports->get(plugin, 0u, false, &portInfo)
            && portInfo.channel_count == 8u,
        "the plug-in did not expose one fixed eight-channel output bus");

    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    clap_note_port_info_t noteInfo {};
    ok &= check(notePorts && notePorts->count(plugin, true) == 1u
            && notePorts->get(plugin, 0u, true, &noteInfo),
        "cell-strike note input is missing");

    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    ok &= check(params && params->count(plugin) == 169u,
        "169-parameter performance/matrix/object surface changed");
    clap_param_info_t info {};
    ok &= check(params && params->get_info(plugin, 12u, &info)
            && info.id == 13u && info.min_value == 1.0
            && info.max_value == 4.0,
        "four-scene selector contract is wrong");
    info = {};
    ok &= check(params && params->get_info(plugin, 26u, &info)
            && info.id == 27u && std::strcmp(info.name, "Spring") == 0,
        "physical exciter parameters are incomplete");
    info = {};
    ok &= check(params && params->get_info(plugin, 32u, &info)
            && info.id == 33u && info.min_value == 0.0
            && info.max_value == 12.0
            && std::strcmp(info.name, "Instrument Preset") == 0,
        "the twelve-instrument preset selector is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 33u, &info)
            && info.id == 34u && info.min_value == 0.0
            && info.max_value == 1.0
            && std::strcmp(info.name, "Cell 1 Cut Enabled") == 0,
        "the eight-cell cut mask is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 41u, &info)
            && info.id == 42u
            && std::strcmp(info.name, "Fracture Edge") == 0,
        "the return-or-latch fracture pad parameters are missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 43u, &info)
            && info.id == 44u
            && std::strcmp(info.name, "Grab Performance") == 0
            && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u,
        "the latching performance Grab control is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 45u, &info)
            && info.id == 46u && std::strcmp(info.name, "Rate") == 0,
        "the independent event Rate control is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 46u, &info)
            && info.id == 47u && std::strcmp(info.name, "Space") == 0,
        "the independent gap Space control is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 47u, &info)
            && info.id == 48u
            && std::strcmp(info.name, "Fracture Void") == 0,
        "the second fracture puck parameters are missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 49u, &info)
            && info.id == 50u
            && std::strcmp(info.name, "Cut Density Puck Latch") == 0
            && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u,
        "the independent cut-density-puck latch is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 50u, &info)
            && info.id == 51u
            && std::strcmp(info.name, "Rupture Shape Puck Latch") == 0
            && (info.flags & CLAP_PARAM_IS_STEPPED) != 0u,
        "the independent rupture-shape-puck latch is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 51u, &info)
            && info.id == 52u
            && std::strcmp(info.name, "Cut Density Gesture X") == 0
            && (info.flags & CLAP_PARAM_IS_HIDDEN) != 0u,
        "the velocity-sensitive fracture gesture transport is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 57u, &info)
            && info.id == 100u && info.min_value == -1.0
            && info.max_value == 1.0,
        "the first signed matrix route is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 121u, &info)
            && info.id == 200u && info.min_value == 0.0
            && info.max_value == 1.0,
        "the first cell-level parameter is missing");
    info = {};
    ok &= check(params && params->get_info(plugin, 129u, &info)
            && info.id == 300u
            && std::strcmp(info.name, "Cell 1 Size") == 0,
        "the per-object character parameters are missing");

    ok &= check(plugin->activate(plugin, 48000.0, kFrames, kFrames),
        "activation failed");
    ok &= check(plugin->start_processing(plugin),
        "start_processing failed");
    Audio audio(8u);
    double internalEnergy = 0.0;
    for (uint32_t block = 0u; block < 24u; ++block) {
        ok &= check(processBlock(plugin, audio, false)
                == CLAP_PROCESS_CONTINUE,
            "internal generator did not continue");
        internalEnergy += energy(audio);
    }
    ok &= check(internalEnergy > 0.0001,
        "internal eight-cell ecology produced silence");

    double value = 0.0;
    ok &= check(flush(plugin, params, { { 21u, 0.0 } }),
        "could not establish Stereo projection before preset auditioning");
    std::array<double, 12u> presetSignatures {};
    for (uint32_t preset = 0u; preset < presetSignatures.size(); ++preset) {
        Events presetPanic;
        presetPanic.addParam(19u, 1.0);
        processBlock(plugin, audio, false, &presetPanic.input);
        double pressureValue = 0.0;
        double contactValue = 0.0;
        double routeValue = 0.0;
        double objectValue = 0.0;
        double presetValue = -1.0;
        ok &= check(flush(plugin, params, { { 33u, preset } })
                && params->get_value(plugin, 33u, &presetValue)
                && params->get_value(plugin, 1u, &pressureValue)
                && params->get_value(plugin, 24u, &contactValue)
                && params->get_value(plugin, 100u, &routeValue)
                && params->get_value(plugin, 300u, &objectValue)
                && static_cast<uint32_t>(presetValue) == preset,
            "a complete instrument preset could not be recalled");
        presetSignatures[preset] = pressureValue * 1.0
            + contactValue * 3.0 + routeValue * 7.0
            + objectValue * 11.0;
        if (preset > 0u) {
            ok &= check(std::abs(presetSignatures[preset]
                        - presetSignatures[preset - 1u]) > 0.001,
                "adjacent instrument presets are not distinct");
        }
        double presetAuditionEnergy = 0.0;
        for (uint32_t block = 0u; block < 12u; ++block) {
            ok &= check(processBlock(plugin, audio, false)
                    == CLAP_PROCESS_CONTINUE,
                "an instrument preset stopped processing");
            presetAuditionEnergy += energy(audio);
        }
        double preservedMode = -1.0;
        ok &= check(presetAuditionEnergy > 1.0
                && params->get_value(plugin, 21u, &preservedMode)
                && preservedMode == 0.0,
            "a factory preset was inaudible or changed the user's projection");
    }
    ok &= check(flush(plugin, params, { { 33u, 0.0 } }),
        "could not return to INIT / PLATE RING");
    double sceneA = 0.0;
    double sceneB = 0.0;
    params->get_value(plugin, 1u, &sceneA);
    ok &= check(flush(plugin, params, { { 13u, 2.0 } })
            && params->get_value(plugin, 1u, &sceneB)
            && std::abs(sceneB - sceneA) > 0.001,
        "factory Scene B is not a distinct preset variation");
    ok &= check(flush(plugin, params, { { 28u, 0.5 } })
            && params->get_value(plugin, 1u, &value)
            && std::abs(value - (sceneA + sceneB) * 0.5) < 0.0001,
        "continuous scene morph did not interpolate A and B");
    ok &= check(flush(plugin, params, { { 13u, 1.0 } }),
        "could not return to factory Scene A");

    ok &= check(flush(plugin, params, { { 21u, 0.0 } }),
        "could not select Stereo output mode");
    processBlock(plugin, audio, false);
    ok &= check(channelEnergy(audio, 0u) > 0.0
            && channelEnergy(audio, 1u) > 0.0,
        "Stereo mode did not render its active pair");
    for (uint32_t channel = 2u; channel < 8u; ++channel) {
        ok &= check(channelEnergy(audio, channel) == 0.0,
            "Stereo mode leaked into an inactive bus channel");
    }
    ok &= check(flush(plugin, params, { { 21u, 1.0 } }),
        "could not select Quad output mode");
    processBlock(plugin, audio, false);
    for (uint32_t channel = 0u; channel < 4u; ++channel) {
        ok &= check(channelEnergy(audio, channel) > 0.0,
            "Quad mode left an active channel silent");
    }
    for (uint32_t channel = 4u; channel < 8u; ++channel) {
        ok &= check(channelEnergy(audio, channel) == 0.0,
            "Quad mode leaked into an inactive bus channel");
    }
    ok &= check(flush(plugin, params, { { 21u, 2.0 } }),
        "could not select 8 Direct output mode");
    processBlock(plugin, audio, false);
    for (uint32_t channel = 0u; channel < 8u; ++channel) {
        ok &= check(channelEnergy(audio, channel) > 0.0,
            "8 Direct mode left a bus channel silent");
    }

    ok &= check(flush(plugin, params, { { 7u, 1.0 }, { 9u, 6.0 } }),
        "could not set Input Coupling/input gain");
    double externalEnergy = 0.0;
    for (uint32_t block = 0u; block < 8u; ++block) {
        processBlock(plugin, audio, true);
        externalEnergy += energy(audio);
    }
    ok &= check(externalEnergy > 0.0001,
        "optional stereo excitation path produced silence");
    ok &= check(flush(plugin, params, { { 10u, -60.0 } }),
        "could not park OUT at its mute end-stop");
    for (uint32_t block = 0u; block < 3u; ++block) {
        processBlock(plugin, audio, true);
        ok &= check(energy(audio) == 0.0,
            "OUT minimum leaked live input or internally generated audio");
    }
    ok &= check(flush(plugin, params, { { 10u, -12.0 } }),
        "could not restore OUT after the mute probe");

    ok &= check(flush(plugin, params, {
            { 100u, -0.66 }, { 200u, 0.31 },
            { 22u, 5.0 }, { 24u, 0.81 }, { 304u, 0.19 } }),
        "matrix/cell/contact edits could not be flushed");
    ok &= check(params->get_value(plugin, 100u, &value)
            && std::abs(value + 0.66) < 0.0001,
        "signed matrix route edit was not published");
    ok &= check(params->get_value(plugin, 200u, &value)
            && std::abs(value - 0.31) < 0.0001,
        "cell level edit was not published");
    ok &= check(params->get_value(plugin, 304u, &value)
            && std::abs(value - 0.19) < 0.0001,
        "selected physical-object edit was not published");

    ok &= check(flush(plugin, params, {
            { 29u, 0.0 }, { 30u, 1.0 }, { 31u, 1.0 } }),
        "could not request the Islands topology");
    processBlock(plugin, audio, false);
    ok &= check(params->get_value(plugin, 100u, &value)
            && std::abs(value - 0.52) < 0.0001,
        "topology gesture did not author the matrix");

    Events noteEvent;
    noteEvent.addNote(43, 1.0, 32u);
    ok &= check(processBlock(plugin, audio, false, &noteEvent.input)
            == CLAP_PROCESS_CONTINUE && energy(audio) >= 0.0,
        "MIDI cell strike produced invalid audio");

    double routeBeforeCut = 0.0;
    params->get_value(plugin, 100u, &routeBeforeCut);
    Events cutEvent;
    cutEvent.addParam(15u, 1.0);
    ok &= check(processBlock(plugin, audio, false, &cutEvent.input)
                == CLAP_PROCESS_CONTINUE && energy(audio) >= 0.0
            && params->get_value(plugin, 100u, &value)
            && std::abs(value - routeBeforeCut) > 0.0001,
        "Cut + Links did not splice audio and mutate authored routing");

    ok &= check(flush(plugin, params, {
            { 34u, 0.0 }, { 35u, 1.0 }, { 36u, 0.0 }, { 37u, 0.0 },
            { 38u, 0.0 }, { 39u, 0.0 }, { 40u, 0.0 }, { 41u, 0.0 } }),
        "the eight-cell cut mask could not be edited");
    double protectedRoute = 0.0;
    double eligibleRoute = 0.0;
    params->get_value(plugin, 100u, &protectedRoute);
    params->get_value(plugin, 108u, &eligibleRoute);
    Events maskedCutEvent;
    maskedCutEvent.addParam(15u, 1.0);
    processBlock(plugin, audio, false, &maskedCutEvent.input);
    double protectedRouteAfter = 0.0;
    double eligibleRouteAfter = 0.0;
    ok &= check(params->get_value(plugin, 100u, &protectedRouteAfter)
            && params->get_value(plugin, 108u, &eligibleRouteAfter)
            && std::abs(protectedRouteAfter - protectedRoute) < 0.000001
            && std::abs(eligibleRouteAfter - eligibleRoute) > 0.0001,
        "Cut + Links did not restrict authored mutation to masked cells");

    ok &= check(flush(plugin, params, {
            { 42u, 0.12 }, { 43u, 0.18 },
            { 48u, 0.14 }, { 49u, 0.20 }, { 44u, 1.0 } }),
        "the fracture pad and latching Grab control could not be flushed");
    double fractureDistance = 0.0;
    double fractureForce = 0.0;
    double fractureVoid = 0.0;
    double fractureSpace = 0.0;
    double grab = 0.0;
    double repeat = 0.0;
    for (uint32_t block = 0u; block < 12u; ++block) {
        processBlock(plugin, audio, false);
    }
    ok &= check(params->get_value(plugin, 44u, &grab) && grab == 1.0,
        "Grab did not remain latched after its initiating flush");
    ok &= check(flush(plugin, params, {
            { 1u, 0.91 }, { 3u, 0.86 },
            { 46u, 0.94 }, { 47u, 0.76 },
            { 42u, 0.92 }, { 43u, 0.88 },
            { 48u, 0.82 }, { 49u, 0.78 }, { 15u, 1.0 } }),
        "the captured parameter and Cut gesture could not be changed");
    for (uint32_t block = 0u; block < 12u; ++block) {
        processBlock(plugin, audio, false);
    }
    ok &= check(flush(plugin, params, { { 44u, 0.0 } })
            && params->get_value(plugin, 44u, &grab) && grab == 0.0,
        "the second Grab toggle did not close the captured duration");
    ok &= check(flush(plugin, params, { { 45u, 1.0 } }),
        "the momentary Repeat control could not start playback");
    ok &= check(processBlock(plugin, audio, false) == CLAP_PROCESS_CONTINUE
            && energy(audio) >= 0.0
            && params->get_value(plugin, 42u, &fractureDistance)
            && params->get_value(plugin, 43u, &fractureForce)
            && params->get_value(plugin, 48u, &fractureVoid)
            && params->get_value(plugin, 49u, &fractureSpace)
            && params->get_value(plugin, 45u, &repeat)
            && std::abs(fractureDistance - 0.92) < 0.0001
            && std::abs(fractureForce - 0.88) < 0.0001
            && std::abs(fractureVoid - 0.82) < 0.0001
            && std::abs(fractureSpace - 0.78) < 0.0001
            && repeat == 1.0,
        "the captured performance did not reach the Repeat engine");
    ok &= check(flush(plugin, params, {
            { 42u, 0.0 }, { 43u, 0.0 },
            { 48u, 0.0 }, { 49u, 0.0 }, { 45u, 0.0 } }),
        "the momentary Repeat and fracture performance did not return to rest");

    double densityLatch = 0.0;
    double shapeLatch = 0.0;
    ok &= check(flush(plugin, params, {
            { 50u, 1.0 }, { 51u, 1.0 },
            { 42u, 0.61 }, { 48u, 0.72 },
            { 43u, 0.53 }, { 49u, 0.84 } })
            && params->get_value(plugin, 50u, &densityLatch)
            && params->get_value(plugin, 51u, &shapeLatch)
            && densityLatch == 1.0 && shapeLatch == 1.0,
        "the two fracture pucks could not enter independent latch mode");
    ok &= check(flush(plugin, params, { { 50u, 0.0 } })
            && params->get_value(plugin, 42u, &fractureDistance)
            && params->get_value(plugin, 48u, &fractureVoid)
            && params->get_value(plugin, 43u, &fractureForce)
            && params->get_value(plugin, 49u, &fractureSpace)
            && fractureForce == 0.0 && fractureVoid == 0.0
            && std::abs(fractureDistance - 0.61) < 0.0001
            && std::abs(fractureSpace - 0.84) < 0.0001,
        "releasing Cut Latch did not zero only Rate and Void");
    ok &= check(flush(plugin, params, { { 51u, 0.0 } })
            && params->get_value(plugin, 42u, &fractureDistance)
            && params->get_value(plugin, 49u, &fractureSpace)
            && fractureDistance == 0.0 && fractureSpace == 0.0,
        "releasing Shape Latch did not zero Edge and Space");
    double gestureEnergy = 1.0;
    ok &= check(flush(plugin, params, {
            { 52u, 1.0 }, { 53u, 0.0 }, { 54u, 0.92 },
            { 55u, 0.0 }, { 56u, 1.0 }, { 57u, 0.86 } })
            && processBlock(plugin, audio, false) == CLAP_PROCESS_CONTINUE
            && params->get_value(plugin, 57u, &gestureEnergy)
            && gestureEnergy == 0.0,
        "velocity-sensitive puck gestures did not reach the audio engine");

    Events panicEvent;
    panicEvent.addParam(19u, 1.0);
    processBlock(plugin, audio, true, &panicEvent.input);
    ok &= check(energy(audio) == 0.0,
        "Panic did not clear both internal and external paths");
    ok &= check(flush(plugin, params, { { 33u, 8.0 } }),
        "preset recall after Panic could not be requested");
    double presetRecoveryEnergy = 0.0;
    for (uint32_t block = 0u; block < 16u; ++block) {
        processBlock(plugin, audio, false);
        presetRecoveryEnergy += energy(audio);
    }
    ok &= check(presetRecoveryEnergy > 0.0001,
        "preset recall did not rearm a panicked or depleted ecology");
    Events secondPanicEvent;
    secondPanicEvent.addParam(19u, 1.0);
    processBlock(plugin, audio, false, &secondPanicEvent.input);
    ok &= check(flush(plugin, params, { { 22u, 5.0 } }),
        "could not restore the selected object after preset recovery");
    double objectSizeBeforeMutation = 0.0;
    params->get_value(plugin, 304u, &objectSizeBeforeMutation);
    Events newEvent;
    newEvent.addParam(18u, 1.0);
    processBlock(plugin, audio, false, &newEvent.input);
    ok &= check(energy(audio) > 0.0,
        "Mutate Object did not strike and restart after Panic");
    ok &= check(params->get_value(plugin, 304u, &value)
            && std::abs(value - objectSizeBeforeMutation) > 0.001,
        "Mutate Object did not change the selected object's character");

    ok &= check(flush(plugin, params, {
            { 13u, 1.0 }, { 1u, 0.23 }, { 24u, 0.81 },
            { 100u, -0.66 }, { 200u, 0.31 },
            { 300u, 0.17 }, { 14u, 1.0 } }),
        "Scene A store request failed");
    processBlock(plugin, audio, false);
    ok &= check(flush(plugin, params, {
            { 1u, 0.91 }, { 24u, 0.09 },
            { 100u, 0.22 }, { 200u, 0.93 },
            { 300u, 0.88 }, { 13u, 1.0 } }),
        "Scene A recall request failed");
    double pressure = 0.0;
    ok &= check(params->get_value(plugin, 1u, &pressure)
            && std::abs(pressure - 0.23) < 0.0001,
        "scene recall did not restore its macro value");
    ok &= check(params->get_value(plugin, 24u, &value)
            && std::abs(value - 0.81) < 0.0001,
        "scene recall did not restore the physical exciter");
    ok &= check(params->get_value(plugin, 100u, &value)
            && std::abs(value + 0.66) < 0.0001,
        "scene recall did not restore the signed matrix");
    ok &= check(params->get_value(plugin, 200u, &value)
            && std::abs(value - 0.31) < 0.0001,
        "scene recall did not restore the cell level");
    ok &= check(params->get_value(plugin, 300u, &value)
            && std::abs(value - 0.17) < 0.0001,
        "scene recall did not restore the physical object");

    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    ok &= check(flush(plugin, params, {
            { 34u, 0.0 }, { 35u, 1.0 }, { 46u, 0.67 }, { 47u, 0.78 },
            { 50u, 1.0 }, { 51u, 1.0 },
            { 42u, 0.62 }, { 43u, 0.71 },
            { 48u, 0.54 }, { 49u, 0.83 },
            { 52u, 0.25 }, { 53u, -0.49 },
            { 55u, -0.86 }, { 56u, -0.88 } }),
        "cut mask state could not be prepared for persistence");
    MemoryState memory;
    clap_ostream_t outputStream { &memory, stateWrite };
    ok &= check(state && state->save(plugin, &outputStream)
            && !memory.bytes.empty(),
        "state save failed under partial stream writes");
    flush(plugin, params, {
        { 1u, 0.11 }, { 34u, 1.0 }, { 46u, 0.02 }, { 47u, 0.03 },
        { 50u, 0.0 }, { 51u, 0.0 },
        { 52u, 0.0 }, { 53u, 0.0 }, { 55u, 0.0 }, { 56u, 0.0 } });
    clap_istream_t inputStream { &memory, stateRead };
    ok &= check(state && state->load(plugin, &inputStream),
        "state load failed under partial stream reads");
    pressure = 0.0;
    ok &= check(params->get_value(plugin, 1u, &pressure)
            && std::abs(pressure - 0.23) < 0.0001,
        "state round trip did not restore the macro surface");
    ok &= check(params->get_value(plugin, 34u, &value) && value == 0.0,
        "state round trip did not restore the eight-cell cut mask");
    ok &= check(params->get_value(plugin, 46u, &value)
            && std::abs(value - 0.67) < 0.0001
            && params->get_value(plugin, 47u, &value)
            && std::abs(value - 0.78) < 0.0001,
        "state round trip did not restore independent Rate and Space");
    ok &= check(params->get_value(plugin, 42u, &fractureDistance)
            && std::abs(fractureDistance - 0.62) < 0.0001
            && params->get_value(plugin, 43u, &fractureForce)
            && std::abs(fractureForce - 0.71) < 0.0001
            && params->get_value(plugin, 48u, &fractureVoid)
            && std::abs(fractureVoid - 0.54) < 0.0001
            && params->get_value(plugin, 49u, &fractureSpace)
            && std::abs(fractureSpace - 0.83) < 0.0001
            && params->get_value(plugin, 50u, &densityLatch)
            && densityLatch == 1.0
            && params->get_value(plugin, 51u, &shapeLatch)
            && shapeLatch == 1.0,
        "state round trip did not restore the fracture puck surface");
    double motionValue = 0.0;
    ok &= check(params->get_value(plugin, 52u, &motionValue)
            && std::abs(motionValue - 0.25) < 0.0001
            && params->get_value(plugin, 53u, &motionValue)
            && std::abs(motionValue + 0.49) < 0.0001
            && params->get_value(plugin, 55u, &motionValue)
            && std::abs(motionValue + 0.86) < 0.0001
            && params->get_value(plugin, 56u, &motionValue)
            && std::abs(motionValue + 0.88) < 0.0001,
        "state round trip did not restore fracture gesture coordinates");

    constexpr uint32_t currentLiveCount = 160u;
    constexpr uint32_t previousLiveCount = 146u;
    constexpr uint32_t legacyLiveCount = 144u;
    constexpr uint32_t olderLiveCount = 136u;
    constexpr uint32_t currentSceneStride = 126u;
    constexpr uint32_t previousSceneStride = 124u;
    constexpr uint32_t sceneCount = currentSceneStride * 4u;
    constexpr uint32_t previousSceneCount = previousSceneStride * 4u;
    const size_t expectedStateBytes = sizeof(FissureStateHeader)
        + sizeof(double) * (currentLiveCount + sceneCount);
    ok &= check(memory.bytes.size() == expectedStateBytes,
        "the Processor Fissure state payload layout changed unexpectedly");
    if (memory.bytes.size() == expectedStateBytes) {
        std::array<double, currentLiveCount> currentLive {};
        std::array<double, previousLiveCount> previousLive {};
        std::array<double, legacyLiveCount> legacyLive {};
        std::array<double, olderLiveCount> olderLive {};
        std::array<double, sceneCount> currentSceneValues {};
        std::array<double, previousSceneCount> previousSceneValues {};
        std::memcpy(currentLive.data(),
            memory.bytes.data() + sizeof(FissureStateHeader),
            sizeof(currentLive));
        std::copy_n(currentLive.begin(), previousLiveCount,
            previousLive.begin());
        std::memcpy(currentSceneValues.data(), memory.bytes.data()
                + sizeof(FissureStateHeader) + sizeof(currentLive),
            sizeof(currentSceneValues));
        const FissureStateHeader previousHeader {
            0x53494653u, 6u, previousLiveCount, sceneCount,
        };
        MemoryState previousState;
        const auto append = [](MemoryState& destination, const auto& item) {
            const auto* first = reinterpret_cast<const uint8_t*>(&item);
            destination.bytes.insert(destination.bytes.end(), first,
                first + sizeof(item));
        };
        append(previousState, previousHeader);
        append(previousState, previousLive);
        append(previousState, currentSceneValues);
        clap_istream_t previousInputStream { &previousState, stateRead };
        ok &= check(state->load(plugin, &previousInputStream),
            "version 0.10 Processor Fissure state was not accepted");

        std::copy_n(previousLive.begin(), 32u, legacyLive.begin());
        std::copy(previousLive.begin() + 34u, previousLive.end(),
            legacyLive.begin() + 32u);
        for (uint32_t scene = 0u; scene < 4u; ++scene) {
            const auto current = currentSceneValues.begin()
                + scene * currentSceneStride;
            auto legacy = previousSceneValues.begin()
                + scene * previousSceneStride;
            std::copy_n(current, 12u, legacy);
            std::copy_n(current + 14u, 112u, legacy + 12u);
        }
        const FissureStateHeader legacyHeader {
            0x53494653u, 5u, legacyLiveCount, previousSceneCount,
        };
        MemoryState legacyState;
        append(legacyState, legacyHeader);
        append(legacyState, legacyLive);
        append(legacyState, previousSceneValues);
        clap_istream_t legacyInputStream { &legacyState, stateRead };
        ok &= check(state->load(plugin, &legacyInputStream),
            "version 0.9 Processor Fissure state was not accepted");
        double oldEdge = 0.0;
        double oldVoid = 0.0;
        double migratedRate = 0.0;
        double migratedSpace = 0.0;
        ok &= check(params->get_value(plugin, 3u, &oldEdge)
                && params->get_value(plugin, 4u, &oldVoid)
                && params->get_value(plugin, 46u, &migratedRate)
                && params->get_value(plugin, 47u, &migratedSpace)
                && std::abs(migratedRate - oldEdge) < 0.0001
                && std::abs(migratedSpace - oldVoid) < 0.0001,
            "version 0.9 state did not migrate coupled Edge/Void timing");

        std::copy_n(legacyLive.begin(), 24u, olderLive.begin());
        std::copy(legacyLive.begin() + 32u, legacyLive.end(),
            olderLive.begin() + 24u);
        const FissureStateHeader olderHeader {
            0x53494653u, 4u, olderLiveCount, previousSceneCount,
        };
        MemoryState olderState;
        append(olderState, olderHeader);
        append(olderState, olderLive);
        append(olderState, previousSceneValues);
        flush(plugin, params, {
            { 34u, 0.0 }, { 35u, 0.0 }, { 36u, 0.0 }, { 37u, 0.0 },
            { 38u, 0.0 }, { 39u, 0.0 }, { 40u, 0.0 }, { 41u, 0.0 } });
        clap_istream_t olderInputStream { &olderState, stateRead };
        ok &= check(state->load(plugin, &olderInputStream),
            "version 0.6 Processor Fissure state was not accepted");
        for (clap_id id = 34u; id <= 41u; ++id) {
            ok &= check(params->get_value(plugin, id, &value)
                    && value == 1.0,
                "version 0.6 state did not default the Cut Mask to enabled");
        }
    }
    plugin->on_main_thread(plugin);
    ok &= check(host.callbacks > 0u && host.rescans > 0u,
        "scene/state changes did not request a host value rescan");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    if (!ok) return 1;
    std::cout << "Processor Fissure 8-bus CLAP smoke passed\n";
    return 0;
}
