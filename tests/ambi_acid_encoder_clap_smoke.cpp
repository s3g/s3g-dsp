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

constexpr uint32_t kChannels = 16u;
constexpr uint32_t kFrames = 256u;
constexpr clap_id kOrderParamId = 1u;
constexpr clap_id kTempoParamId = 2u;
constexpr clap_id kRootParamId = 5u;
constexpr clap_id kListenModeParamId = 23u;
constexpr clap_id kOutputParamId = 26u;
constexpr clap_id kTransportSyncParamId = 27u;
constexpr clap_id kScaleParamId = 28u;
constexpr clap_id kSubOctaveParamId = 29u;
constexpr clap_id kSubLevelParamId = 30u;
constexpr clap_id kDriveCircuitParamId = 31u;
constexpr clap_id kDriveMixParamId = 32u;
constexpr clap_id kOutputModeParamId = 33u;
constexpr clap_id kStepOneNoteParamId = 100u;
constexpr clap_id kStepOneAccentParamId = 102u;
constexpr clap_id kStepTwoSlideParamId = 107u;
constexpr clap_id kStepOnePathXParamId = 200u;
constexpr clap_id kStepOnePathYParamId = 201u;
constexpr clap_id kStepOnePathHeightParamId = 202u;

struct HostContext {
    clap_host_t host {};
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}

struct EventList {
    std::vector<const clap_event_header_t*> events;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->events.size()
                ? self->events[index] : nullptr;
        },
    };
    std::array<clap_event_param_value_t, 16u> params {};
    uint32_t paramCount = 0u;
    std::array<clap_event_note_t, 8u> notes {};
    uint32_t noteCount = 0u;
    std::array<clap_event_midi_t, 8u> midi {};
    uint32_t midiCount = 0u;

    void add(clap_id id, double value, uint32_t time = 0u)
    {
        auto& event = params[paramCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events.push_back(&event.header);
    }

    void addNote(bool on, int16_t key, double velocity = 1.0,
        uint32_t time = 0u)
    {
        auto& event = notes[noteCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = -1;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        events.push_back(&event.header);
    }
};

struct AudioBlock {
    std::array<std::array<float, kFrames>, kChannels> storage {};
    std::array<float*, kChannels> pointers {};
    clap_audio_buffer_t buffer {};

    AudioBlock()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            pointers[channel] = storage[channel].data();
        }
        buffer.data32 = pointers.data();
        buffer.channel_count = kChannels;
    }

    void clear()
    {
        for (auto& channel : storage) channel.fill(0.0f);
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
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 5u });
    if (count > 0u) {
        std::memcpy(destination,
            state->bytes.data() + state->offset, count);
        state->offset += count;
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

clap_process_status runBlock(const clap_plugin_t* plugin,
    AudioBlock& audio, const clap_input_events_t* events = nullptr,
    const clap_event_transport_t* transport = nullptr)
{
    clap_process_t process {};
    process.frames_count = kFrames;
    process.transport = transport;
    process.audio_outputs = &audio.buffer;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process);
}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double expected)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value)
        && std::fabs(value - expected) < 1.0e-5;
}

bool stateRoundTrip(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params,
    const clap_plugin_state_t* state)
{
    EventList authored;
    authored.add(kTempoParamId, 174.0);
    authored.add(kRootParamId, 29.0);
    authored.add(kListenModeParamId, 3.0);
    authored.add(kOutputParamId, -4.0);
    authored.add(kTransportSyncParamId, 1.0);
    authored.add(kScaleParamId, 31.0);
    authored.add(kSubOctaveParamId, -2.0);
    authored.add(kSubLevelParamId, 0.72);
    authored.add(kDriveCircuitParamId, 8.0);
    authored.add(kDriveMixParamId, 0.64);
    authored.add(kOutputModeParamId, 1.0);
    authored.add(kStepOneNoteParamId, -12.0);
    authored.add(kStepOnePathXParamId, 0.25);
    authored.add(kStepOnePathYParamId, -0.75);
    authored.add(kStepOnePathHeightParamId, 0.5);
    params->flush(plugin, &authored.input, nullptr);

    MemoryState memory;
    clap_ostream_t output { &memory, stateWrite };
    if (!state->save(plugin, &output) || memory.bytes.empty()) return false;

    EventList mutation;
    mutation.add(kTempoParamId, 90.0);
    mutation.add(kRootParamId, 48.0);
    mutation.add(kListenModeParamId, 0.0);
    mutation.add(kOutputParamId, -30.0);
    mutation.add(kTransportSyncParamId, 0.0);
    mutation.add(kScaleParamId, 0.0);
    mutation.add(kSubOctaveParamId, 0.0);
    mutation.add(kSubLevelParamId, 0.0);
    mutation.add(kDriveCircuitParamId, 0.0);
    mutation.add(kDriveMixParamId, 0.0);
    mutation.add(kOutputModeParamId, 0.0);
    mutation.add(kStepOneNoteParamId, 7.0);
    mutation.add(kStepOnePathXParamId, -1.0);
    mutation.add(kStepOnePathYParamId, 1.0);
    mutation.add(kStepOnePathHeightParamId, -0.5);
    params->flush(plugin, &mutation.input, nullptr);
    if (!getParam(plugin, params, kTempoParamId, 90.0)
        || !getParam(plugin, params, kStepOneNoteParamId, 7.0)) {
        return false;
    }

    clap_istream_t input { &memory, stateRead };
    return state->load(plugin, &input)
        && getParam(plugin, params, kTempoParamId, 174.0)
        && getParam(plugin, params, kRootParamId, 29.0)
        && getParam(plugin, params, kListenModeParamId, 3.0)
        && getParam(plugin, params, kOutputParamId, -4.0)
        && getParam(plugin, params, kTransportSyncParamId, 1.0)
        && getParam(plugin, params, kScaleParamId, 31.0)
        && getParam(plugin, params, kSubOctaveParamId, -2.0)
        && getParam(plugin, params, kSubLevelParamId, 0.72)
        && getParam(plugin, params, kDriveCircuitParamId, 8.0)
        && getParam(plugin, params, kDriveMixParamId, 0.64)
        && getParam(plugin, params, kOutputModeParamId, 1.0)
        && getParam(plugin, params, kStepOneNoteParamId, -12.0)
        && getParam(plugin, params, kStepOnePathXParamId, 0.25)
        && getParam(plugin, params, kStepOnePathYParamId, -0.75)
        && getParam(plugin, params, kStepOnePathHeightParamId, 0.5);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_acid_encoder_clap_smoke "
                     "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve acid encoder binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "could not load acid encoder: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !entry->init(nullptr)) {
        std::cerr << "invalid acid encoder CLAP entry\n";
        dlclose(library);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    bool ok = factory && factory->get_plugin_count(factory) == 1u
        && descriptor && std::strcmp(descriptor->id, argv[2]) == 0
        && std::strstr(descriptor->name, "Acid") != nullptr;

    HostContext context;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = "s3g acid encoder smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequestRestart;
    context.host.request_process = hostRequestProcess;
    context.host.request_callback = hostRequestCallback;

    const clap_plugin_t* plugin = ok ? factory->create_plugin(
        factory, &context.host, argv[2]) : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        std::cerr << "could not create acid encoder\n";
        entry->deinit();
        dlclose(library);
        return 1;
    }
    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    clap_audio_port_info_t outputInfo {};
    clap_note_port_info_t noteInputInfo {};
    ok = ok && audioPorts && notePorts && params && state
        && audioPorts->count(plugin, true) == 0u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && std::strcmp(outputInfo.port_type, CLAP_PORT_AMBISONIC) == 0
        && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && notePorts->get(plugin, 0u, true, &noteInputInfo)
        && (noteInputInfo.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
        && (noteInputInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && params->count(plugin) == 145u
        && getParam(plugin, params, kOrderParamId, 3.0)
        && getParam(plugin, params, kTempoParamId, 126.0)
        && getParam(plugin, params, kRootParamId, 36.0)
        && getParam(plugin, params, kListenModeParamId, 0.0)
        && getParam(plugin, params, kOutputParamId, -10.0)
        && getParam(plugin, params, kTransportSyncParamId, 0.0)
        && getParam(plugin, params, kScaleParamId, 0.0)
        && getParam(plugin, params, kSubOctaveParamId, -1.0)
        && getParam(plugin, params, kSubLevelParamId, 0.0)
        && getParam(plugin, params, kDriveCircuitParamId, 0.0)
        && getParam(plugin, params, kDriveMixParamId, 0.0)
        && getParam(plugin, params, kOutputModeParamId, 0.0)
        && getParam(plugin, params, kStepOneAccentParamId, 1.0)
        && getParam(plugin, params, kStepTwoSlideParamId, 1.0)
        && getParam(plugin, params, kStepOnePathXParamId, 1.0)
        && getParam(plugin, params, kStepOnePathYParamId, 0.0)
        && getParam(plugin, params, kStepOnePathHeightParamId, 0.0);
    char rootText[32] {};
    char listenerText[32] {};
    char clockText[32] {};
    char scaleText[64] {};
    char circuitText[32] {};
    char outputModeText[32] {};
    double parsedScale = -1.0;
    ok = ok && params->value_to_text(plugin, kRootParamId,
            36.0, rootText, sizeof(rootText))
        && std::strcmp(rootText, "C2") == 0
        && params->value_to_text(plugin, kListenModeParamId,
            3.0, listenerText, sizeof(listenerText))
        && std::strcmp(listenerText, "Balance") == 0
        && params->value_to_text(plugin, kTransportSyncParamId,
            1.0, clockText, sizeof(clockText))
        && std::strcmp(clockText, "Host") == 0
        && params->value_to_text(plugin, kScaleParamId,
            31.0, scaleText, sizeof(scaleText))
        && std::strcmp(scaleText, "PENTATONIC MINOR") == 0
        && params->text_to_value(plugin, kScaleParamId,
            "PENTATONIC MINOR", &parsedScale)
        && std::fabs(parsedScale - 31.0) < 0.000001
        && params->value_to_text(plugin, kDriveCircuitParamId,
            8.0, circuitText, sizeof(circuitText))
        && std::strcmp(circuitText, "DIODE") == 0
        && params->value_to_text(plugin, kOutputModeParamId,
            1.0, outputModeText, sizeof(outputModeText))
        && std::strcmp(outputModeText, "DUAL MONO 1+2") == 0
        && stateRoundTrip(plugin, params, state);

    bool listenerParamsHidden = true;
    for (uint32_t index = 0u; index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)) {
            listenerParamsHidden = false;
            break;
        }
        if (info.id >= kListenModeParamId && info.id <= 25u) {
            listenerParamsHidden = listenerParamsHidden
                && (info.flags & CLAP_PARAM_IS_HIDDEN) != 0u;
        }
    }
    ok = ok && listenerParamsHidden;

    if (!ok || !plugin->activate(plugin, 48000.0, 1u, kFrames)
        || !plugin->start_processing(plugin)) {
        std::cerr << "acid encoder CLAP contract failed\n";
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }

    plugin->reset(plugin);
    AudioBlock audio;
    EventList hostClockMode;
    hostClockMode.add(kTransportSyncParamId, 1.0);
    hostClockMode.add(kOutputModeParamId, 0.0);
    hostClockMode.add(kDriveMixParamId, 0.0);
    params->flush(plugin, &hostClockMode.input, nullptr);
    clap_event_transport_t stoppedTransport {};
    stoppedTransport.header.size = sizeof(stoppedTransport);
    stoppedTransport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    stoppedTransport.header.type = CLAP_EVENT_TRANSPORT;
    stoppedTransport.flags = CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    stoppedTransport.tempo = 120.0;
    stoppedTransport.song_pos_beats = 0;
    audio.clear();
    runBlock(plugin, audio, nullptr, &stoppedTransport);
    double stoppedEnergy = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) stoppedEnergy += value * value;
    }
    plugin->reset(plugin);
    clap_event_transport_t playingTransport = stoppedTransport;
    playingTransport.flags |= CLAP_TRANSPORT_IS_PLAYING;
    audio.clear();
    runBlock(plugin, audio, nullptr, &playingTransport);
    double playingEnergy = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) playingEnergy += value * value;
    }
    plugin->reset(plugin);
    clap_event_transport_t restTransport = playingTransport;
    restTransport.song_pos_beats = static_cast<clap_beattime>(
        std::llround(1.75 * static_cast<double>(CLAP_BEATTIME_FACTOR)));
    audio.clear();
    runBlock(plugin, audio, nullptr, &restTransport);
    double restEnergy = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) restEnergy += value * value;
    }
    ok = ok && stoppedEnergy < 1.0e-12
        && playingEnergy > 1.0e-8 && restEnergy < 1.0e-12;
    EventList internalClockMode;
    internalClockMode.add(kTransportSyncParamId, 0.0);
    params->flush(plugin, &internalClockMode.input, nullptr);

    EventList outputLevel;
    outputLevel.add(kOutputParamId, -6.0);
    params->flush(plugin, &outputLevel.input, nullptr);
    plugin->reset(plugin);
    audio.clear();
    runBlock(plugin, audio);
    double highOutputEnergy = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) highOutputEnergy += value * value;
    }
    EventList quietOutputLevel;
    quietOutputLevel.add(kOutputParamId, -30.0);
    params->flush(plugin, &quietOutputLevel.input, nullptr);
    plugin->reset(plugin);
    audio.clear();
    runBlock(plugin, audio);
    double lowOutputEnergy = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) lowOutputEnergy += value * value;
    }
    ok = ok && highOutputEnergy > 1.0e-8
        && lowOutputEnergy > 0.0
        && highOutputEnergy > lowOutputEnergy * 40.0;
    EventList restoreOutputLevel;
    restoreOutputLevel.add(kOutputParamId, -10.0);
    params->flush(plugin, &restoreOutputLevel.input, nullptr);

    plugin->reset(plugin);
    audio.clear();
    runBlock(plugin, audio);
    AudioBlock midiAudio;
    plugin->reset(plugin);
    EventList midiTranspose;
    midiTranspose.addNote(true, 48, 0.8, 0u);
    midiTranspose.addNote(true, 60, 0.9, 64u);
    midiTranspose.addNote(false, 60, 0.0, 128u);
    midiTranspose.addNote(false, 48, 0.0, 192u);
    midiAudio.clear();
    runBlock(plugin, midiAudio, &midiTranspose.input);
    double midiDifference = 0.0;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            midiDifference += std::fabs(
                midiAudio.storage[channel][sample]
                - audio.storage[channel][sample]);
        }
    }
    ok = ok && midiDifference > 0.001;

    plugin->reset(plugin);
    EventList dualMonoMode;
    dualMonoMode.add(kOutputModeParamId, 1.0);
    audio.clear();
    runBlock(plugin, audio, &dualMonoMode.input);
    double dualMonoEnergy = 0.0;
    bool dualMonoClean = true;
    for (uint32_t sample = 0u; sample < kFrames; ++sample) {
        dualMonoClean = dualMonoClean
            && std::fabs(audio.storage[0u][sample]
                - audio.storage[1u][sample]) < 1.0e-7f;
        dualMonoEnergy += static_cast<double>(audio.storage[0u][sample])
            * audio.storage[0u][sample];
        for (uint32_t channel = 2u; channel < kChannels; ++channel) {
            dualMonoClean = dualMonoClean
                && audio.storage[channel][sample] == 0.0f;
        }
    }
    ok = ok && dualMonoClean && dualMonoEnergy > 1.0e-8;
    EventList restoreHoaMode;
    restoreHoaMode.add(kOutputModeParamId, 0.0);
    params->flush(plugin, &restoreHoaMode.input, nullptr);

    plugin->reset(plugin);
    EventList orderChange;
    orderChange.add(kOrderParamId, 3.0, 0u);
    orderChange.add(kOrderParamId, 1.0, 128u);
    audio.clear();
    runBlock(plugin, audio, &orderChange.input);
    double highOrderBefore = 0.0;
    double highOrderAfter = 0.0;
    for (uint32_t channel = 4u; channel < kChannels; ++channel) {
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            const float value = audio.storage[channel][sample];
            if (!std::isfinite(value)) ok = false;
            (sample < 128u ? highOrderBefore : highOrderAfter)
                += std::fabs(value);
        }
    }
    ok = ok && highOrderBefore > 0.001 && highOrderAfter == 0.0;

    plugin->reset(plugin);
    EventList fullField;
    fullField.add(kOrderParamId, 3.0);
    fullField.add(kListenModeParamId, 3.0);
    double totalEnergy = 0.0;
    double directionalEnergy = 0.0;
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 48u; ++block) {
        audio.clear();
        runBlock(plugin, audio, block == 0u ? &fullField.input : nullptr);
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (const float value : audio.storage[channel]) {
                if (!std::isfinite(value)) ok = false;
                const double energy = static_cast<double>(value) * value;
                totalEnergy += energy;
                if (channel > 0u) directionalEnergy += energy;
                peak = std::max(peak, std::fabs(value));
            }
        }
    }
    ok = ok && totalEnergy > 0.01
        && directionalEnergy > totalEnergy * 0.05
        && peak > 0.005f && peak <= 0.961f;

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "acid encoder audio/state behavior failed: high-order "
                  << highOrderBefore << "/" << highOrderAfter
                  << " transport=" << stoppedEnergy << "/"
                  << playingEnergy << "/" << restEnergy
                  << " midi-delta=" << midiDifference
                  << " output-level=" << highOutputEnergy << "/"
                  << lowOutputEnergy
                  << " dual-mono=" << dualMonoEnergy
                  << " energy=" << totalEnergy
                  << " directional=" << directionalEnergy
                  << " peak=" << peak << "\n";
        return 1;
    }
    std::cout << "ambi acid encoder CLAP smoke passed\n";
    return 0;
}
