#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

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
constexpr clap_id kShapeParamId = 2u;
constexpr clap_id kTuneParamId = 3u;
constexpr clap_id kPitchSweepParamId = 4u;
constexpr clap_id kDecayParamId = 6u;
constexpr clap_id kClickParamId = 9u;
constexpr clap_id kStrikeXParamId = 11u;
constexpr clap_id kStrikeYParamId = 12u;
constexpr clap_id kVelocityParamId = 17u;
constexpr clap_id kNoteTrackingParamId = 18u;
constexpr clap_id kTriggerParamId = 20u;
constexpr clap_id kStrikeModeParamId = 21u;

struct HostContext {
    clap_host_t host {};
    clap_host_tail_t tail {};
    uint32_t processRequests = 0u;
    uint32_t tailChanges = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    return id && std::strcmp(id, CLAP_EXT_TAIL) == 0
        ? &hostContext(host)->tail : nullptr;
}

void hostRequestRestart(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}

void hostRequestProcess(const clap_host_t* host)
{
    ++hostContext(host)->processRequests;
}

void hostTailChanged(const clap_host_t* host)
{
    ++hostContext(host)->tailChanges;
}

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
    std::array<clap_event_param_value_t, 8u> params {};
    std::array<clap_event_note_t, 4u> notes {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;

    void addParam(clap_id id, double value, uint32_t time = 0u)
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

    void addNote(int16_t key, double velocity, uint32_t time)
    {
        auto& event = notes[noteCount++];
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = key;
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
        std::memcpy(destination, state->bytes.data() + state->offset, count);
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
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) return entry.path();
        }
    }
#endif
    return {};
}

clap_process_status runBlock(const clap_plugin_t* plugin,
    AudioBlock& audio, const clap_input_events_t* events)
{
    clap_process_t process {};
    process.frames_count = kFrames;
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

bool hasAutomatableParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, bool stepped)
{
    for (uint32_t index = 0u; index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info) || info.id != id) {
            continue;
        }
        return (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
            && ((info.flags & CLAP_PARAM_IS_STEPPED) != 0u) == stepped;
    }
    return false;
}

struct StrikeMetrics {
    double attackEnergy = 0.0;
    double settledFrequency = 0.0;
};

StrikeMetrics renderMidiStrike(const clap_plugin_t* plugin,
    AudioBlock& audio, int16_t key, double velocity)
{
    plugin->reset(plugin);
    EventList strike;
    strike.addParam(kPitchSweepParamId, 0.0);
    strike.addParam(kClickParamId, 0.0);
    strike.addNote(key, velocity, 0u);

    constexpr uint32_t blockCount = 96u;
    constexpr uint32_t analysisStart = 1000u;
    StrikeMetrics metrics;
    double low = 0.0;
    double previousLow = 0.0;
    uint32_t crossings = 0u;
    for (uint32_t block = 0u; block < blockCount; ++block) {
        audio.clear();
        runBlock(plugin, audio, block == 0u ? &strike.input : nullptr);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            const uint32_t absoluteSample = block * kFrames + sample;
            if (absoluteSample < 4096u) {
                for (uint32_t channel = 0u; channel < kChannels; ++channel) {
                    const double value = audio.storage[channel][sample];
                    metrics.attackEnergy += value * value;
                }
            }
            const double sampleValue = audio.storage[0u][sample];
            low += (sampleValue - low) * 0.0105;
            if (absoluteSample >= analysisStart
                && previousLow <= 0.0 && low > 0.0) {
                ++crossings;
            }
            previousLow = low;
        }
    }

    metrics.settledFrequency = static_cast<double>(crossings)
        / (static_cast<double>(blockCount * kFrames - analysisStart)
            / 48000.0);
    return metrics;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_ambi_membrane_kick_clap_smoke "
            << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve membrane kick binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "could not load membrane kick: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !entry->init(nullptr)) {
        std::cerr << "invalid CLAP entry\n";
        dlclose(library);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) != 1u) {
        std::cerr << "invalid membrane kick factory\n";
        entry->deinit();
        dlclose(library);
        return 1;
    }
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0u);
    if (!descriptor || std::strcmp(descriptor->id, argv[2]) != 0
        || std::strstr(descriptor->name, "Membrane Kick") == nullptr) {
        std::cerr << "unexpected membrane kick descriptor\n";
        entry->deinit();
        dlclose(library);
        return 1;
    }

    HostContext context;
    context.tail.changed = hostTailChanged;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = "s3g membrane kick smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequestRestart;
    context.host.request_process = hostRequestProcess;
    context.host.request_callback = hostRequestCallback;

    const clap_plugin_t* plugin = factory->create_plugin(
        factory, &context.host, argv[2]);
    if (!plugin || !plugin->init(plugin)) {
        std::cerr << "could not create membrane kick\n";
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
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    bool ok = audioPorts && notePorts && params && state && tail;
    clap_audio_port_info_t audioInfo {};
    clap_note_port_info_t noteInfo {};
    ok = ok && audioPorts->count(plugin, true) == 0u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, false, &audioInfo)
        && audioInfo.channel_count == kChannels
        && std::strcmp(audioInfo.port_type, CLAP_PORT_SURROUND) == 0
        && notePorts->count(plugin, true) == 1u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && params->count(plugin) == 21u
        && getParam(plugin, params, kTuneParamId, 43.0)
        && getParam(plugin, params, kShapeParamId, 0.0)
        && getParam(plugin, params, kVelocityParamId, 1.0)
        && getParam(plugin, params, kNoteTrackingParamId, 1.0)
        && getParam(plugin, params, kStrikeModeParamId, 0.0)
        && hasAutomatableParam(plugin, params, kStrikeXParamId, false)
        && hasAutomatableParam(plugin, params, kStrikeYParamId, false)
        && hasAutomatableParam(plugin, params, kStrikeModeParamId, true);
    char shapeText[32] {};
    ok = ok && params->value_to_text(plugin, kShapeParamId, 3.0,
        shapeText, sizeof(shapeText))
        && std::strcmp(shapeText, "Triangle") == 0;
    char strikeModeText[32] {};
    ok = ok && params->value_to_text(plugin, kStrikeModeParamId, 2.0,
        strikeModeText, sizeof(strikeModeText))
        && std::strcmp(strikeModeText, "Random Rim") == 0;
    char directText[32] {};
    ok = ok && params->value_to_text(plugin, kOrderParamId, 4.0,
        directText, sizeof(directText))
        && std::strcmp(directText, "16 Pickups") == 0;
    if (!ok || !plugin->activate(plugin, 48000.0, 1u, kFrames)
        || !plugin->start_processing(plugin)) {
        std::cerr << "membrane kick CLAP contract failed\n";
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }

    AudioBlock audio;
    runBlock(plugin, audio, nullptr);
    double silence = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) silence += std::fabs(value);
    }
    EventList strike;
    strike.addNote(36, 1.0, 37u);
    audio.clear();
    runBlock(plugin, audio, &strike.input);
    double before = 0.0;
    double after = 0.0;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            const float value = audio.storage[channel][sample];
            if (!std::isfinite(value)) ok = false;
            (sample < 37u ? before : after) += std::fabs(value);
        }
    }
    ok = ok && silence == 0.0 && before == 0.0 && after > 0.01;

    plugin->reset(plugin);
    EventList directSelect;
    directSelect.addParam(kOrderParamId, 4.0);
    params->flush(plugin, &directSelect.input, nullptr);
    EventList directStrike;
    directStrike.addNote(36, 0.9, 0u);
    audio.clear();
    runBlock(plugin, audio, &directStrike.input);
    std::array<double, kChannels> directEnergy {};
    double directVariation = 0.0;
    for (uint32_t sample = 0u; sample < kFrames; ++sample) {
        directVariation += std::fabs(audio.storage[0u][sample]
            - audio.storage[15u][sample]);
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            directEnergy[channel] += static_cast<double>(
                audio.storage[channel][sample]) * audio.storage[channel][sample];
        }
    }
    bool directContract = getParam(plugin, params, kOrderParamId, 4.0)
        && directVariation > 0.0001;
    for (const double energy : directEnergy) {
        directContract = directContract && energy > 1.0e-10;
    }
    if (!directContract) {
        std::cerr << "sixteen-pickup direct output contract failed\n";
    }
    ok = ok && directContract;
    plugin->reset(plugin);
    EventList restoreHoa;
    restoreHoa.addParam(kOrderParamId, 3.0);
    params->flush(plugin, &restoreHoa.input, nullptr);

    const auto lowNote = renderMidiStrike(plugin, audio, 36, 1.0);
    const auto highNote = renderMidiStrike(plugin, audio, 48, 1.0);
    const auto softStrike = renderMidiStrike(plugin, audio, 36, 0.20);
    const bool midiResponse = lowNote.settledFrequency >= 38.0
        && lowNote.settledFrequency <= 49.0
        && highNote.settledFrequency >= 78.0
        && highNote.settledFrequency <= 95.0
        && lowNote.attackEnergy > softStrike.attackEnergy * 8.0;
    if (!midiResponse) {
        std::cerr << "MIDI pitch/velocity response was out of range: "
                  << lowNote.settledFrequency << " Hz / "
                  << highNote.settledFrequency << " Hz; energy "
                  << softStrike.attackEnergy << " / "
                  << lowNote.attackEnergy << "\n";
    }
    ok = ok && midiResponse;

    EventList firstOrderStrike;
    firstOrderStrike.addParam(kOrderParamId, 1.0);
    firstOrderStrike.addNote(36, 0.8, 0u);
    audio.clear();
    runBlock(plugin, audio, &firstOrderStrike.input);
    double highOrder = 0.0;
    for (uint32_t channel = 4u; channel < kChannels; ++channel) {
        for (const float value : audio.storage[channel]) {
            highOrder += std::fabs(value);
        }
    }
    ok = ok && highOrder == 0.0;

    MemoryState memory;
    clap_ostream_t ostream { &memory, stateWrite };
    ok = ok && state->save(plugin, &ostream);
    EventList change;
    change.addParam(kTuneParamId, 70.0);
    change.addParam(kDecayParamId, 2.5);
    change.addParam(kStrikeModeParamId, 2.0);
    params->flush(plugin, &change.input, nullptr);
    ok = ok && getParam(plugin, params, kTuneParamId, 70.0)
        && getParam(plugin, params, kStrikeModeParamId, 2.0)
        && context.tailChanges > 0u;
    memory.offset = 0u;
    clap_istream_t istream { &memory, stateRead };
    ok = ok && state->load(plugin, &istream)
        && getParam(plugin, params, kTuneParamId, 43.0)
        && getParam(plugin, params, kStrikeModeParamId, 0.0)
        && tail->get(plugin) > 48000u;

    struct LegacyState {
        uint32_t version = 1u;
        uint32_t reserved = 0u;
        std::array<double, 19u> values {};
    } legacyState;
    for (clap_id id = 1u; id <= 19u; ++id) {
        params->get_value(plugin, id, &legacyState.values[id - 1u]);
    }
    legacyState.values[kTuneParamId - 1u] = 55.0;
    MemoryState legacyMemory;
    legacyMemory.bytes.resize(sizeof(legacyState));
    std::memcpy(legacyMemory.bytes.data(), &legacyState,
        sizeof(legacyState));
    clap_istream_t legacyStream { &legacyMemory, stateRead };
    ok = ok && state->load(plugin, &legacyStream)
        && getParam(plugin, params, kTuneParamId, 55.0)
        && getParam(plugin, params, kStrikeModeParamId, 0.0);

    EventList triggerOn;
    triggerOn.addParam(kTriggerParamId, 1.0);
    params->flush(plugin, &triggerOn.input, nullptr);
    ok = ok && context.processRequests > 0u;
    audio.clear();
    runBlock(plugin, audio, nullptr);
    double triggered = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) triggered += std::fabs(value);
    }
    ok = ok && triggered > 0.01;

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "membrane kick behavior/state probe failed\n";
        return 1;
    }
    std::cout << "ambi membrane kick CLAP smoke passed\n";
    return 0;
}
