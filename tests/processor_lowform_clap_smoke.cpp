#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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
#include <set>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPluginId = "org.s3g.s3g-dsp.processor-lowform";
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 2u;
constexpr uint32_t kParamCount = 120u;

struct Host {
    clap_host_t host {};
    clap_host_params_t params {};
    clap_host_tail_t tail {};
    uint32_t rescans = 0u;
    uint32_t infoRescans = 0u;
    uint32_t tailChanges = 0u;
    uint32_t processRequests = 0u;
};

Host* context(const clap_host_t* host)
{
    return static_cast<Host*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (id && std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &context(host)->params;
    if (id && std::strcmp(id, CLAP_EXT_TAIL) == 0)
        return &context(host)->tail;
    return nullptr;
}

void hostRescan(const clap_host_t* host, clap_param_rescan_flags flags)
{
    if ((flags & CLAP_PARAM_RESCAN_VALUES) != 0u)
        ++context(host)->rescans;
    if ((flags & (CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT)) != 0u)
        ++context(host)->infoRescans;
}

void hostTailChanged(const clap_host_t* host)
{
    ++context(host)->tailChanges;
}

void hostRequestProcess(const clap_host_t* host)
{
    ++context(host)->processRequests;
}

void noRestart(const clap_host_t*) {}
void noCallback(const clap_host_t*) {}
void noClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void noFlush(const clap_host_t*) {}

struct Events {
    std::array<clap_event_param_value_t, 64u> params {};
    std::array<clap_event_note_t, 16u> notes {};
    std::array<clap_event_note_expression_t, 8u> expressions {};
    std::array<clap_event_midi_t, 8u> midi {};
    std::array<const clap_event_header_t*, 96u> ordered {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t expressionCount = 0u;
    uint32_t midiCount = 0u;
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

    bool addNote(uint16_t type, int16_t key, double velocity,
        uint32_t time, int32_t noteId, int16_t channel = 0)
    {
        if (noteCount >= notes.size()) return false;
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        return insert(&event.header);
    }

    bool addExpression(clap_note_expression expression, int16_t key,
        int32_t noteId, double value, uint32_t time)
    {
        if (expressionCount >= expressions.size()) return false;
        auto& event = expressions[expressionCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.expression_id = expression;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.value = value;
        return insert(&event.header);
    }

    bool addMidi(uint8_t status, uint8_t dataOne, uint8_t dataTwo,
        uint32_t time)
    {
        if (midiCount >= midi.size()) return false;
        auto& event = midi[midiCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.port_index = 0;
        event.data[0] = status;
        event.data[1] = dataOne;
        event.data[2] = dataTwo;
        return insert(&event.header);
    }
};

struct Audio {
    std::array<std::array<float, kFrames>, kChannels> storage {};
    std::array<float*, kChannels> pointers {};
    clap_audio_buffer_t output {};

    Audio()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel)
            pointers[channel] = storage[channel].data();
        output.data32 = pointers.data();
        output.channel_count = kChannels;
    }

    void clear()
    {
        for (auto& lane : storage) lane.fill(0.0f);
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
    const size_t count = std::min<size_t>(requested, 7u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
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
    Audio& audio, const clap_input_events_t* events = nullptr,
    const clap_event_transport_t* transport = nullptr)
{
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = kFrames;
    process.audio_outputs = &audio.output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    process.transport = transport;
    return plugin->process(plugin, &process);
}

double energy(const Audio& audio)
{
    double total = 0.0;
    for (const auto& lane : audio.storage) {
        for (float sample : lane) {
            if (!std::isfinite(sample) || std::fabs(sample) > 2.001f)
                return -1.0;
            total += static_cast<double>(sample) * sample;
        }
    }
    return total;
}

bool flush(const clap_plugin_t* plugin, const clap_plugin_params_t* params,
    std::initializer_list<std::pair<clap_id, double>> values)
{
    Events events;
    for (const auto& value : values)
        if (!events.addParam(value.first, value.second)) return false;
    params->flush(plugin, &events.input, nullptr);
    return true;
}

double rawMidiCcOneSignature(const clap_plugin_t* plugin,
    uint8_t key, uint8_t noteChannel, int ccChannel, uint8_t ccValue)
{
    plugin->reset(plugin);
    Events onset;
    if (!onset.addMidi(static_cast<uint8_t>(0x90u | noteChannel),
            key, 104u, 0u)) return -1.0;
    if (ccChannel >= 0
        && !onset.addMidi(static_cast<uint8_t>(0xb0u | ccChannel),
            1u, ccValue, 1u)) return -1.0;
    Audio audio;
    double signature = 0.0;
    for (uint32_t block = 0u; block < 24u; ++block) {
        audio.clear();
        if (processBlock(plugin, audio,
                block == 0u ? &onset.input : nullptr) == CLAP_PROCESS_ERROR)
            return -1.0;
        const double blockEnergy = energy(audio);
        if (blockEnergy < 0.0) return -1.0;
        signature += blockEnergy * (1.0 + 0.013 * block);
    }
    return signature;
}

double arpArticulationSignature(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, double accent,
    double tie, double stepOctave, bool gapWindow)
{
    if (!flush(plugin, params, {
            { 2u, 1.0 }, { 3u, 0.0 }, { 6u, 0.72 }, { 7u, 0.0 },
            { 10u, 0.0 }, { 11u, 0.44 }, { 15u, 0.0 },
            { 19u, 2.0 }, { 20u, 720.0 }, { 23u, 0.82 },
            { 27u, 0.0005 }, { 29u, 1.0 }, { 30u, 0.005 },
            { 50u, 0.0 }, { 52u, 0.0 }, { 55u, 0.0 },
            { 56u, 0.0 }, { 57u, 0.0 }, { 58u, 0.0 }, { 60u, 0.0 },
            { 67u, 6.0 }, { 68u, 0.0 }, { 69u, 0.0 },
            { 70u, 5.0 }, { 71u, 1.0 }, { 72u, 0.15 },
            { 73u, 2.0 }, { 74u, 0.0 }, { 75u, 1.0 },
            { 97u, accent }, { 98u, 1.0 },
            { 105u, tie }, { 106u, 0.0 },
            { 113u, stepOctave }, { 114u, 0.0 } })) return -1.0;
    plugin->reset(plugin);
    Events onset;
    if (!onset.addMidi(0x92u, 42u, 108u, 0u)) return -1.0;
    Audio audio;
    double signature = 0.0;
    uint64_t sampleIndex = 0u;
    for (uint32_t block = 0u; block < 5u; ++block) {
        audio.clear();
        if (processBlock(plugin, audio,
                block == 0u ? &onset.input : nullptr) == CLAP_PROCESS_ERROR)
            return -1.0;
        if (!gapWindow || block >= 2u) {
            for (const auto& channel : audio.storage) {
                for (const float sample : channel) {
                    if (!std::isfinite(sample)) return -1.0;
                    signature += std::fabs(sample)
                        * (1.0 + 0.002 * static_cast<double>(
                            sampleIndex % 37u));
                    ++sampleIndex;
                }
            }
        }
    }
    return signature;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_processor_lowform_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve Processor Lowform binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "could not load Processor Lowform: " << dlerror() << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !entry->init(binary.c_str())) {
        std::cerr << "invalid Processor Lowform CLAP entry\n";
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) != 1u) return 1;
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0u);
    if (!descriptor || std::strcmp(descriptor->id, kPluginId) != 0
        || std::strcmp(descriptor->name, "s3g Processor Lowform 2") != 0)
        return 1;

    Host host;
    host.params.rescan = hostRescan;
    host.params.clear = noClear;
    host.params.request_flush = noFlush;
    host.tail.changed = hostTailChanged;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "s3g Processor Lowform smoke";
    host.host.vendor = "s3g";
    host.host.url = "https://github.com/s3g/s3g-dsp";
    host.host.version = "1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = noRestart;
    host.host.request_process = hostRequestProcess;
    host.host.request_callback = noCallback;

    const clap_plugin_t* plugin = factory->create_plugin(
        factory, &host.host, kPluginId);
    if (!plugin || !plugin->init(plugin)) return 1;
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    if (!params || !audioPorts || !notePorts || !state || !tail
        || params->count(plugin) != kParamCount
        || audioPorts->count(plugin, false) != 1u
        || audioPorts->count(plugin, true) != 0u
        || notePorts->count(plugin, true) != 1u) return 1;
    clap_note_port_info_t noteInfo {};
    if (!notePorts->get(plugin, 0u, true, &noteInfo)
        || (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_CLAP) == 0u
        || (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) == 0u
        || (noteInfo.supported_dialects
            & CLAP_NOTE_DIALECT_MIDI_MPE) == 0u) {
        std::cerr << "MPE note-port contract failed\n";
        return 1;
    }

    std::set<clap_id> ids;
    for (uint32_t i = 0u; i < kParamCount; ++i) {
        clap_param_info_t info {};
        char text[64] {};
        double roundTrip = 0.0;
        if (!params->get_info(plugin, i, &info)
            || !ids.insert(info.id).second
            || !params->value_to_text(plugin, info.id,
                info.default_value, text, sizeof(text))
            || !params->text_to_value(plugin, info.id, text, &roundTrip)) {
            std::cerr << "parameter contract failed at " << i << '\n';
            return 1;
        }
    }
    clap_param_info_t expressionInfo {};
    char expressionText[32] {};
    double expressionValue = -1.0;
    if (!params->get_info(plugin, 64u, &expressionInfo)
        || expressionInfo.id != 65u
        || std::strcmp(expressionInfo.name, "Expression Mode") != 0
        || std::strcmp(expressionInfo.module, "Routing") != 0
        || expressionInfo.default_value != 0.0
        || !params->value_to_text(plugin, 65u, 1.0,
            expressionText, sizeof(expressionText))
        || std::strcmp(expressionText, "QUNEXUS GEN 1") != 0
        || !params->text_to_value(plugin, 65u,
            expressionText, &expressionValue)
        || expressionValue != 1.0) {
        std::cerr << "QuNexus expression-mode contract failed\n";
        return 1;
    }
    clap_param_info_t performanceInfo {};
    char performanceText[32] {};
    double performanceValue = -99.0;
    if (!params->get_info(plugin, 65u, &performanceInfo)
        || performanceInfo.id != 66u
        || std::strcmp(performanceInfo.name, "Transpose") != 0
        || performanceInfo.min_value != -24.0
        || performanceInfo.max_value != 24.0
        || !params->value_to_text(plugin, 66u, -12.0,
            performanceText, sizeof(performanceText))
        || std::strcmp(performanceText, "-12 st") != 0
        || !params->value_to_text(plugin, 67u, 6.0,
            performanceText, sizeof(performanceText))
        || std::strcmp(performanceText, "CUSTOM") != 0
        || !params->text_to_value(plugin, 68u,
            "BLUES", &performanceValue)
        || performanceValue != 10.0
        || !params->text_to_value(plugin, 70u,
            "1/1", &performanceValue)
        || performanceValue != 8.0
        || !params->text_to_value(plugin, 74u,
            "REST", &performanceValue)
        || performanceValue != -9.0) {
        std::cerr << "transpose/arpeggiator parameter contract failed\n";
        return 1;
    }
    clap_param_info_t bodyEngineInfo {};
    char acidText[32] {};
    double acidValue = -1.0;
    if (!params->get_info(plugin, 9u, &bodyEngineInfo)
        || bodyEngineInfo.id != 10u
        || bodyEngineInfo.min_value != 0.0
        || bodyEngineInfo.max_value != 7.0
        || !params->value_to_text(plugin, 10u, 3.0,
            acidText, sizeof(acidText))
        || std::strcmp(acidText, "ACID") != 0
        || !params->text_to_value(plugin, 10u, acidText, &acidValue)
        || acidValue != 3.0
        || !params->value_to_text(plugin, 10u, 4.0,
            acidText, sizeof(acidText))
        || std::strcmp(acidText, "RAVE") != 0
        || !params->text_to_value(plugin, 10u, acidText, &acidValue)
        || acidValue != 4.0
        || !params->value_to_text(plugin, 10u, 5.0,
            acidText, sizeof(acidText))
        || std::strcmp(acidText, "PHASE") != 0
        || !params->value_to_text(plugin, 10u, 6.0,
            acidText, sizeof(acidText))
        || std::strcmp(acidText, "THROAT") != 0
        || !params->value_to_text(plugin, 10u, 7.0,
            acidText, sizeof(acidText))
        || std::strcmp(acidText, "SYNC") != 0
        || !params->text_to_value(plugin, 10u, acidText, &acidValue)
        || acidValue != 7.0) {
        std::cerr << "body-engine parameter contract failed\n";
        return 1;
    }

    clap_param_info_t exposedInfo {};
    const auto hasExposedInfo = [&](uint32_t index, clap_id id,
                                     const char* name, const char* module) {
        if (params->get_info(plugin, index, &exposedInfo)
            && exposedInfo.id == id
            && std::strcmp(exposedInfo.name, name) == 0
            && std::strcmp(exposedInfo.module, module) == 0) return true;
        std::cerr << "replacement direct-control mismatch at index " << index
                  << " (id=" << exposedInfo.id << ", name="
                  << exposedInfo.name << ", module=" << exposedInfo.module
                  << ")\n";
        return false;
    };
    if (!hasExposedInfo(45u, 46u, "Modal Drive", "Body/MODAL")
        || std::fabs(exposedInfo.default_value - 0.25) > 1.0e-6
        || !hasExposedInfo(46u, 47u, "Mod Wheel Amount", "Motion")
        || !hasExposedInfo(47u, 48u, "Bite", "Finish")
        || !hasExposedInfo(48u, 49u, "Width", "Texture")) {
        std::cerr << "replacement direct-control contract failed\n";
        return 1;
    }

    clap_param_info_t directInfo {};
    char directText[32] {};
    double directValue = -1.0;
    if (!params->get_info(plugin, 11u, &directInfo)
        || directInfo.id != 12u
        || std::strcmp(directInfo.name, "Shape") != 0
        || !params->get_info(plugin, 61u, &directInfo)
        || directInfo.id != 62u
        || std::strcmp(directInfo.name, "Pulse Width") != 0
        || !flush(plugin, params, { { 10u, 1.0 }, { 62u, 0.5 } })
        || !params->get_info(plugin, 61u, &directInfo)
        || std::strcmp(directInfo.name, "Voices") != 0
        || std::strcmp(directInfo.module, "Body/SWARM") != 0
        || !params->value_to_text(plugin, 62u, 0.5,
            directText, sizeof(directText))
        || std::strcmp(directText, "4 VOICES") != 0
        || !params->text_to_value(plugin, 62u, directText, &directValue)
        || std::fabs(directValue - 0.5) > 1.0e-6
        || !flush(plugin, params, { { 10u, 2.0 } })
        || !params->get_info(plugin, 12u, &directInfo)
        || std::strcmp(directInfo.name, "Position") != 0
        || !params->get_info(plugin, 61u, &directInfo)
        || std::strcmp(directInfo.name, "Decay") != 0
        || !params->get_info(plugin, 62u, &directInfo)
        || std::strcmp(directInfo.name, "Sustain") != 0
        || !flush(plugin, params, { { 10u, 3.0 }, { 64u, 0.5 } })
        || !params->get_info(plugin, 63u, &directInfo)
        || std::strcmp(directInfo.name, "Cutoff Offset") != 0
        || !params->value_to_text(plugin, 64u, 0.5,
            directText, sizeof(directText))
        || std::strcmp(directText, "+0.00 OCT") != 0
        || !flush(plugin, params, { { 10u, 4.0 } })
        || !params->get_info(plugin, 12u, &directInfo)
        || std::strcmp(directInfo.name, "Sub") != 0
        || !params->get_info(plugin, 61u, &directInfo)
        || std::strcmp(directInfo.name, "Smear") != 0
        || !params->get_info(plugin, 62u, &directInfo)
        || std::strcmp(directInfo.name, "Noise") != 0
        || !params->get_info(plugin, 63u, &directInfo)
        || std::strcmp(directInfo.name, "Drive") != 0
        || std::strcmp(directInfo.module, "Body/RAVE") != 0
        || !flush(plugin, params, { { 10u, 5.0 }, { 13u, 0.5 } })
        || !params->get_info(plugin, 11u, &directInfo)
        || std::strcmp(directInfo.name, "Carrier") != 0
        || !params->get_info(plugin, 12u, &directInfo)
        || std::strcmp(directInfo.name, "Ratio") != 0
        || !params->value_to_text(plugin, 13u, 0.5,
            directText, sizeof(directText))
        || std::strcmp(directText, "2.00x") != 0
        || !params->text_to_value(plugin, 13u,
            directText, &directValue)
        || std::fabs(directValue - 0.5) > 1.0e-6
        || !flush(plugin, params, { { 10u, 6.0 }, { 13u, 0.5 } })
        || !params->get_info(plugin, 11u, &directInfo)
        || std::strcmp(directInfo.name, "Vowel") != 0
        || !params->get_info(plugin, 12u, &directInfo)
        || std::strcmp(directInfo.name, "Shift") != 0
        || !params->value_to_text(plugin, 13u, 0.5,
            directText, sizeof(directText))
        || std::strcmp(directText, "+0.00 OCT") != 0
        || !flush(plugin, params, { { 10u, 7.0 }, { 64u, 0.5 } })
        || !params->get_info(plugin, 11u, &directInfo)
        || std::strcmp(directInfo.name, "Wave") != 0
        || !params->get_info(plugin, 12u, &directInfo)
        || std::strcmp(directInfo.name, "Ratio") != 0
        || !params->get_info(plugin, 61u, &directInfo)
        || std::strcmp(directInfo.name, "Sweep") != 0
        || !params->get_info(plugin, 62u, &directInfo)
        || std::strcmp(directInfo.name, "Decay") != 0
        || !params->get_info(plugin, 63u, &directInfo)
        || std::strcmp(directInfo.name, "Ring") != 0
        || std::strcmp(directInfo.module, "Body/SYNC") != 0) {
        std::cerr << "dynamic body-control parameter contract failed\n";
        return 1;
    }
    plugin->on_main_thread(plugin);
    if (host.infoRescans == 0u) {
        std::cerr << "dynamic body-control rescan was not requested\n";
        return 1;
    }

    clap_param_info_t targetInfo {};
    char targetText[64] {};
    double targetValue = -1.0;
    if (!flush(plugin, params, { { 10u, 4.0 } })
        || !params->get_info(plugin, 36u, &targetInfo)
        || targetInfo.id != 37u
        || targetInfo.min_value != 0.0
        || targetInfo.max_value != 28.0
        || !params->value_to_text(plugin, 37u, 3.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "BODY / WAVE") != 0
        || !params->value_to_text(plugin, 37u, 4.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "BODY / SUB") != 0
        || !params->value_to_text(plugin, 37u, 9.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "BODY + TEXTURE / WIDTH") != 0
        || !params->value_to_text(plugin, 37u, 15.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "BODY / WIDTH") != 0
        || !params->value_to_text(plugin, 37u, 17.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "TEXTURE / WIDTH") != 0
        || !params->text_to_value(plugin, 37u,
            "FILTER / DRIVE", &targetValue)
        || targetValue != 18.0
        || !params->value_to_text(plugin, 37u, 20.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "AMPLIFIER / TUBE") != 0
        || !params->value_to_text(plugin, 37u, 21.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "SHRED / AMOUNT") != 0
        || !params->text_to_value(plugin, 37u,
            "SHRED / MIX", &targetValue)
        || targetValue != 24.0
        || !params->value_to_text(plugin, 37u, 25.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "DYNAMICS / AMOUNT") != 0
        || !params->text_to_value(plugin, 37u,
            "DYNAMICS / TILT", &targetValue)
        || targetValue != 28.0
        || !flush(plugin, params, { { 10u, 2.0 } })
        || !params->value_to_text(plugin, 37u, 12.0,
            targetText, sizeof(targetText))
        || std::strcmp(targetText, "BODY / DECAY") != 0) {
        std::cerr << "expanded modulation-target contract failed\n";
        return 1;
    }

    clap_param_info_t newInfo {};
    char newText[64] {};
    double newValue = -99.0;
    if (!params->get_info(plugin, 81u, &newInfo)
        || newInfo.id != 82u
        || std::strcmp(newInfo.name, "Target B") != 0
        || std::strcmp(newInfo.module, "Mod 1") != 0
        || newInfo.max_value != 28.0
        || !params->get_info(plugin, 87u, &newInfo)
        || newInfo.id != 88u
        || std::strcmp(newInfo.module, "Expression/Velocity") != 0
        || newInfo.default_value != 1.0
        || !params->value_to_text(plugin, 94u, 12.0,
            newText, sizeof(newText))
        || std::strcmp(newText, "HOLD") != 0
        || !params->text_to_value(plugin, 94u, "HOLD", &newValue)
        || newValue != 12.0
        || !params->value_to_text(plugin, 97u, 0.72,
            newText, sizeof(newText))
        || std::strcmp(newText, "72%") != 0
        || !params->value_to_text(plugin, 105u, 1.0,
            newText, sizeof(newText))
        || std::strcmp(newText, "TIE") != 0
        || !params->text_to_value(plugin, 105u, "GATE", &newValue)
        || newValue != 0.0
        || !params->value_to_text(plugin, 113u, -2.0,
            newText, sizeof(newText))
        || std::strcmp(newText, "-2") != 0) {
        std::cerr << "expression/articulation parameter contract failed\n";
        return 1;
    }

    if (!plugin->activate(plugin, 48000.0, 16u, 1024u)
        || !plugin->start_processing(plugin)) return 1;
    Audio audio;
    Events onset;
    onset.addNote(CLAP_EVENT_NOTE_ON, 33, 0.9, 0u, 100);
    onset.addNote(CLAP_EVENT_NOTE_ON, 40, 0.75, 48u, 101);
    onset.addExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 33, 100,
        0.8, 96u);
    onset.addExpression(CLAP_NOTE_EXPRESSION_BRIGHTNESS, 40, 101,
        0.7, 128u);
    onset.addMidi(0xe0u, 0x7fu, 0x4fu, 160u);
    onset.addMidi(0xb0u, 1u, 100u, 192u);
    double firstEnergy = 0.0;
    for (uint32_t block = 0u; block < 30u; ++block) {
        audio.clear();
        const auto status = processBlock(plugin, audio,
            block == 0u ? &onset.input : nullptr);
        if (status == CLAP_PROCESS_ERROR || energy(audio) < 0.0) return 1;
        firstEnergy += energy(audio);
    }
    if (firstEnergy < 0.01) {
        std::cerr << "Processor Lowform note path was silent\n";
        return 1;
    }

    // Body engine, texture, filter routing, modulation, and finish controls.
    if (!flush(plugin, params, {
            { 10u, 3.0 }, { 15u, 4.0 }, { 16u, 0.42 },
            { 19u, 4.0 }, { 20u, 1600.0 }, { 24u, 0.0 },
            { 34u, 6.0 }, { 35u, 0.55 }, { 36u, 0.6 }, { 37u, 7.0 },
            { 50u, 0.5 }, { 51u, 2.0 }, { 52u, 0.5 },
            { 53u, 0.12 }, { 55u, 0.42 }, { 56u, 0.5 },
            { 57u, 0.24 }, { 60u, 0.5 } })) return 1;
    Events secondOnset;
    secondOnset.addNote(CLAP_EVENT_NOTE_ON, 45, 0.85, 0u, 102);
    double secondEnergy = 0.0;
    for (uint32_t block = 0u; block < 24u; ++block) {
        audio.clear();
        processBlock(plugin, audio,
            block == 0u ? &secondOnset.input : nullptr);
        const double e = energy(audio);
        if (e < 0.0) return 1;
        secondEnergy += e;
    }
    if (secondEnergy < 0.01 || std::fabs(secondEnergy - firstEnergy) < 1.0e-4)
        return 1;

    plugin->reset(plugin);
    Events mpeOnset;
    // Member channel 3: configure a 48-semitone bend range, establish
    // expression before note-on, then play beside an unbent member channel.
    mpeOnset.addMidi(0xb2u, 101u, 0u, 0u);
    mpeOnset.addMidi(0xb2u, 100u, 0u, 1u);
    mpeOnset.addMidi(0xb2u, 6u, 48u, 2u);
    mpeOnset.addMidi(0xb2u, 74u, 108u, 3u);
    mpeOnset.addMidi(0xd2u, 104u, 0u, 4u);
    mpeOnset.addMidi(0xe2u, 0u, 96u, 5u);
    mpeOnset.addMidi(0x92u, 48u, 112u, 6u);
    mpeOnset.addMidi(0x93u, 55u, 104u, 7u);
    double mpeEnergy = 0.0;
    for (uint32_t block = 0u; block < 18u; ++block) {
        audio.clear();
        const auto status = processBlock(plugin, audio,
            block == 0u ? &mpeOnset.input : nullptr);
        const double e = energy(audio);
        if (status == CLAP_PROCESS_ERROR || e < 0.0) return 1;
        mpeEnergy += e;
    }
    if (mpeEnergy < 0.01) {
        std::cerr << "raw MIDI MPE path was silent\n";
        return 1;
    }
    Events mpeRelease;
    mpeRelease.addMidi(0x82u, 48u, 0u, 0u);
    mpeRelease.addMidi(0x83u, 55u, 0u, 1u);
    audio.clear();
    if (processBlock(plugin, audio, &mpeRelease.input)
        == CLAP_PROCESS_ERROR || energy(audio) < 0.0) return 1;

    // First-generation QuNexus Preset C rotates notes and CC1 across MIDI
    // channels. Its compatibility mode must consume CC1 as pressure only on
    // the matching voice; Standard MPE must retain CC1 as the global wheel.
    if (!flush(plugin, params, {
            { 2u, 1.0 }, { 6u, 0.35 }, { 7u, 0.0 },
            { 10u, 0.0 }, { 11u, 0.82 }, { 15u, 0.0 }, { 19u, 0.0 },
            { 24u, 0.0 }, { 25u, 0.0 }, { 26u, 0.0 },
            { 36u, 0.0 }, { 40u, 0.0 }, { 44u, 0.0 },
            { 48u, 0.0 }, { 50u, 0.0 }, { 52u, 0.0 }, { 55u, 0.0 },
            { 56u, 0.0 }, { 57u, 0.0 }, { 58u, 0.0 }, { 60u, 0.0 },
            { 65u, 0.0 } })) return 1;
    const double ccOneBaseline = rawMidiCcOneSignature(
        plugin, 42u, 2u, -1, 0u);
    const double standardCcOne = rawMidiCcOneSignature(
        plugin, 42u, 2u, 2, 116u);
    if (!flush(plugin, params, { { 65u, 1.0 } })) return 1;
    const double wrongChannelCcOne = rawMidiCcOneSignature(
        plugin, 42u, 2u, 3, 116u);
    const double quNexusCcOne = rawMidiCcOneSignature(
        plugin, 42u, 2u, 2, 116u);
    if (ccOneBaseline <= 0.0 || standardCcOne <= 0.0
        || wrongChannelCcOne <= 0.0 || quNexusCcOne <= 0.0
        || std::fabs(standardCcOne - ccOneBaseline) > 1.0e-9
        || std::fabs(wrongChannelCcOne - ccOneBaseline) > 1.0e-9
        || std::fabs(quNexusCcOne - ccOneBaseline) < 1.0e-4) {
        std::cerr << "QuNexus Gen 1 channelized CC1 routing failed\n";
        return 1;
    }

    // Direct keyboard playing must retain ordinary note priority while the
    // arp is off. MONO is last-note priority with fallback to the previous
    // held key; POLY releases only the addressed voice.
    plugin->reset(plugin);
    if (!flush(plugin, params, {
            { 2u, 0.0 }, { 3u, 24.0 }, { 27u, 0.0005 },
            { 29u, 0.90 }, { 30u, 0.005 }, { 54u, 0.0 },
            { 56u, 0.0 }, { 67u, 0.0 } })) return 1;
    Events monoFirst;
    monoFirst.addNote(CLAP_EVENT_NOTE_ON, 36, 0.88, 0u, 701, 2);
    audio.clear();
    if (processBlock(plugin, audio, &monoFirst.input)
        == CLAP_PROCESS_ERROR) return 1;
    Events monoSecond;
    monoSecond.addNote(CLAP_EVENT_NOTE_ON, 43, 0.82, 0u, 702, 3);
    audio.clear();
    if (processBlock(plugin, audio, &monoSecond.input)
        == CLAP_PROCESS_ERROR) return 1;
    Events monoSecondOff;
    monoSecondOff.addNote(CLAP_EVENT_NOTE_OFF, 43, 0.0, 0u, 702, 3);
    double monoFallbackEnergy = 0.0;
    for (uint32_t block = 0u; block < 12u; ++block) {
        audio.clear();
        if (processBlock(plugin, audio,
                block == 0u ? &monoSecondOff.input : nullptr)
                == CLAP_PROCESS_ERROR) return 1;
        if (block >= 8u) monoFallbackEnergy += energy(audio);
    }
    if (monoFallbackEnergy < 1.0e-8) {
        std::cerr << "MONO last-note fallback was lost\n";
        return 1;
    }
    Events monoFirstOff;
    monoFirstOff.addNote(CLAP_EVENT_NOTE_OFF, 36, 0.0, 0u, 701, 2);
    audio.clear();
    if (processBlock(plugin, audio, &monoFirstOff.input)
        == CLAP_PROCESS_ERROR) return 1;

    plugin->reset(plugin);
    if (!flush(plugin, params, { { 2u, 1.0 }, { 67u, 0.0 } })) return 1;
    Events polyOn;
    // Deliberately reuse note_id 0: MIDI-to-CLAP adapters in the field may
    // provide a non-negative but non-unique ID. Key/channel must still keep
    // these as two independent POLY voices.
    polyOn.addNote(CLAP_EVENT_NOTE_ON, 36, 0.88, 0u, 0, 2);
    polyOn.addNote(CLAP_EVENT_NOTE_ON, 43, 0.82, 1u, 0, 3);
    audio.clear();
    if (processBlock(plugin, audio, &polyOn.input)
        == CLAP_PROCESS_ERROR) return 1;
    Events polyUpperOff;
    polyUpperOff.addNote(CLAP_EVENT_NOTE_OFF, 43, 0.0, 0u, 0, 3);
    double polyRemainingEnergy = 0.0;
    for (uint32_t block = 0u; block < 12u; ++block) {
        audio.clear();
        if (processBlock(plugin, audio,
                block == 0u ? &polyUpperOff.input : nullptr)
                == CLAP_PROCESS_ERROR) return 1;
        if (block >= 8u) polyRemainingEnergy += energy(audio);
    }
    if (polyRemainingEnergy < 1.0e-8) {
        std::cerr << "POLY release silenced another held key\n";
        return 1;
    }
    Events polyLowerOff;
    polyLowerOff.addNote(CLAP_EVENT_NOTE_OFF, 36, 0.0, 0u, 0, 2);
    audio.clear();
    if (processBlock(plugin, audio, &polyLowerOff.input)
        == CLAP_PROCESS_ERROR) return 1;

    if (!flush(plugin, params, { { 67u, 0.0 }, { 66u, 12.0 } })) return 1;
    const double transposedSignature = rawMidiCcOneSignature(
        plugin, 30u, 2u, -1, 0u);
    if (!flush(plugin, params, { { 66u, 0.0 } })) return 1;
    const double directPitchSignature = rawMidiCcOneSignature(
        plugin, 42u, 2u, -1, 0u);
    if (transposedSignature <= 0.0 || directPitchSignature <= 0.0
        || std::fabs(transposedSignature - directPitchSignature) > 1.0e-9) {
        std::cerr << "MIDI input transpose failed\n";
        return 1;
    }

    if (!flush(plugin, params, {
            { 67u, 1.0 }, { 68u, 0.0 }, { 69u, 1.0 },
            { 70u, 2.0 }, { 71u, 2.0 }, { 72u, 0.62 } })) return 1;
    plugin->reset(plugin);
    Events arpOnset;
    arpOnset.addMidi(0x92u, 42u, 108u, 0u);
    clap_event_transport_t transport {};
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
        | CLAP_TRANSPORT_IS_PLAYING;
    transport.tempo = 120.0;
    transport.song_pos_beats = static_cast<clap_beattime>(
        0.125 * static_cast<double>(CLAP_BEATTIME_FACTOR));
    audio.clear();
    if (processBlock(plugin, audio, &arpOnset.input, &transport)
            == CLAP_PROCESS_ERROR
        || energy(audio) != 0.0) {
        std::cerr << "host-synced arpeggiator attacked between grid lines\n";
        return 1;
    }
    transport.song_pos_beats = static_cast<clap_beattime>(
        0.25 * static_cast<double>(CLAP_BEATTIME_FACTOR));
    audio.clear();
    if (processBlock(plugin, audio, nullptr, &transport)
            == CLAP_PROCESS_ERROR
        || energy(audio) < 1.0e-8) {
        std::cerr << "host-synced arpeggiator missed the grid boundary\n";
        return 1;
    }

    // A selected direction only arms the arp: it must stay silent without a
    // held key, then stop scheduling as soon as the last key is released.
    // Exercise a mismatched host note-id on release as well, since several
    // MIDI-to-CLAP paths preserve key/channel but not the synthesized ID.
    plugin->reset(plugin);
    if (!flush(plugin, params, {
            { 30u, 0.005 }, { 54u, 0.0 }, { 56u, 0.0 },
            { 67u, 1.0 }, { 69u, 0.0 }, { 70u, 5.0 },
            { 72u, 0.50 } })) return 1;
    double directionOnlyEnergy = 0.0;
    for (uint32_t block = 0u; block < 12u; ++block) {
        audio.clear();
        if (processBlock(plugin, audio) == CLAP_PROCESS_ERROR) return 1;
        directionOnlyEnergy += energy(audio);
    }
    if (directionOnlyEnergy != 0.0) {
        std::cerr << "arpeggiator ran without a held MIDI note\n";
        return 1;
    }
    Events gatedArpOn;
    gatedArpOn.addNote(CLAP_EVENT_NOTE_ON, 42, 0.85, 0u, 501, 2);
    audio.clear();
    if (processBlock(plugin, audio, &gatedArpOn.input)
            == CLAP_PROCESS_ERROR
        || energy(audio) < 1.0e-8) {
        std::cerr << "key-gated arpeggiator did not start\n";
        return 1;
    }
    Events gatedArpOff;
    gatedArpOff.addNote(CLAP_EVENT_NOTE_OFF, 42, 0.0, 0u, 999, 2);
    double lateReleaseEnergy = 0.0;
    for (uint32_t block = 0u; block < 64u; ++block) {
        audio.clear();
        if (processBlock(plugin, audio,
                block == 0u ? &gatedArpOff.input : nullptr)
                == CLAP_PROCESS_ERROR) return 1;
        const double blockEnergy = energy(audio);
        if (blockEnergy < 0.0) return 1;
        if (block >= 56u) lateReleaseEnergy += blockEnergy;
    }
    if (lateReleaseEnergy > 1.0e-7) {
        std::cerr << "arpeggiator remained latched after final note-off\n";
        return 1;
    }

    const double softAccent = arpArticulationSignature(
        plugin, params, 0.15, 0.0, 0.0, false);
    const double hardAccent = arpArticulationSignature(
        plugin, params, 1.0, 0.0, 0.0, false);
    const double raisedOctave = arpArticulationSignature(
        plugin, params, 1.0, 0.0, 1.0, false);
    const double gatedGap = arpArticulationSignature(
        plugin, params, 1.0, 0.0, 0.0, true);
    const double tiedGap = arpArticulationSignature(
        plugin, params, 1.0, 1.0, 0.0, true);
    if (softAccent <= 0.0 || hardAccent <= 0.0 || raisedOctave <= 0.0
        || gatedGap < 0.0 || tiedGap <= 0.0
        || std::fabs(hardAccent - softAccent) < 1.0e-3
        || std::fabs(raisedOctave - hardAccent) < 1.0e-3
        || tiedGap <= gatedGap * 1.5) {
        std::cerr << "arp accent/tie/octave behavior failed (accent "
                  << softAccent << "/" << hardAccent << ", octave "
                  << raisedOctave << ", gap " << gatedGap << "/"
                  << tiedGap << ")\n";
        return 1;
    }

    plugin->reset(plugin);
    if (!flush(plugin, params, {
            { 65u, 1.0 }, { 66u, -12.0 }, { 67u, 6.0 },
            { 68u, 10.0 }, { 69u, 1.0 }, { 70u, 5.0 },
            { 71u, 3.0 }, { 72u, 0.41 }, { 73u, 4.0 },
            { 74u, -9.0 }, { 75u, -2.0 }, { 76u, 3.0 },
            { 82u, 25.0 }, { 83u, -0.42 },
            { 88u, 7.0 }, { 89u, 0.81 },
            { 94u, 0.33 }, { 95u, 0.24 }, { 96u, 0.71 },
            { 97u, 0.64 }, { 105u, 1.0 }, { 113u, -2.0 } })) return 1;

    MemoryState saved;
    clap_ostream_t stream { &saved, stateWrite };
    if (!state->save(plugin, &stream) || saved.bytes.empty()) return 1;
    uint32_t savedVersion = 0u;
    if (saved.bytes.size() < sizeof(uint32_t) * 2u) return 1;
    std::memcpy(&savedVersion, saved.bytes.data() + sizeof(uint32_t),
        sizeof(savedVersion));
    if (savedVersion != 9u) {
        std::cerr << "Lowform state schema version was not advanced\n";
        return 1;
    }
    double savedCutoff = 0.0;
    params->get_value(plugin, 20u, &savedCutoff);
    if (!flush(plugin, params, { { 20u, 240.0 } })) return 1;
    saved.offset = 0u;
    clap_istream_t input { &saved, stateRead };
    if (!state->load(plugin, &input) || host.rescans == 0u) return 1;
    double loadedCutoff = 0.0;
    double loadedExpressionMode = 0.0;
    double loadedTranspose = 0.0;
    double loadedArpPattern = 0.0;
    double loadedArpStep = 0.0;
    double loadedSecondaryTarget = 0.0;
    double loadedVelocityDepth = 0.0;
    double loadedBodyDecay = 0.0;
    double loadedAccent = 0.0;
    double loadedGateMode = 0.0;
    double loadedStepOctave = 0.0;
    if (!params->get_value(plugin, 20u, &loadedCutoff)
        || !params->get_value(plugin, 65u, &loadedExpressionMode)
        || !params->get_value(plugin, 66u, &loadedTranspose)
        || !params->get_value(plugin, 67u, &loadedArpPattern)
        || !params->get_value(plugin, 74u, &loadedArpStep)
        || !params->get_value(plugin, 82u, &loadedSecondaryTarget)
        || !params->get_value(plugin, 89u, &loadedVelocityDepth)
        || !params->get_value(plugin, 94u, &loadedBodyDecay)
        || !params->get_value(plugin, 97u, &loadedAccent)
        || !params->get_value(plugin, 105u, &loadedGateMode)
        || !params->get_value(plugin, 113u, &loadedStepOctave)
        || std::fabs(loadedCutoff - savedCutoff) > 1.0e-6
        || loadedExpressionMode != 1.0 || loadedTranspose != -12.0
        || loadedArpPattern != 6.0 || loadedArpStep != -9.0
        || loadedSecondaryTarget != 25.0
        || std::fabs(loadedVelocityDepth - 0.81) > 1.0e-6
        || std::fabs(loadedBodyDecay - 0.33) > 1.0e-6
        || std::fabs(loadedAccent - 0.64) > 1.0e-6
        || loadedGateMode != 1.0 || loadedStepOctave != -2.0) return 1;

    MemoryState raveState = saved;
    raveState.offset = 0u;
    const uint32_t raveVersion = 8u;
    std::memcpy(raveState.bytes.data() + sizeof(uint32_t),
        &raveVersion, sizeof(raveVersion));
    clap_istream_t raveInput { &raveState, stateRead };
    if (!state->load(plugin, &raveInput)) {
        std::cerr << "version 8 state compatibility failed\n";
        return 1;
    }

    MemoryState articulationState = saved;
    articulationState.offset = 0u;
    const uint32_t articulationVersion = 7u;
    std::memcpy(articulationState.bytes.data() + sizeof(uint32_t),
        &articulationVersion, sizeof(articulationVersion));
    clap_istream_t articulationInput { &articulationState, stateRead };
    if (!state->load(plugin, &articulationInput)) {
        std::cerr << "version 7 state compatibility failed\n";
        return 1;
    }

    constexpr size_t valueOffset = sizeof(uint32_t) * 4u;
    const auto setStateValue = [valueOffset](MemoryState& memory, uint32_t id,
                                              double value) {
        std::memcpy(memory.bytes.data() + valueOffset
                + (id - 1u) * sizeof(double),
            &value, sizeof(value));
    };

    const uint32_t assignableMotionCount = 81u;
    MemoryState assignableMotionState = saved;
    assignableMotionState.offset = 0u;
    const uint32_t assignableMotionVersion = 6u;
    std::memcpy(assignableMotionState.bytes.data() + sizeof(uint32_t),
        &assignableMotionVersion, sizeof(assignableMotionVersion));
    std::memcpy(assignableMotionState.bytes.data() + sizeof(uint32_t) * 2u,
        &assignableMotionCount, sizeof(assignableMotionCount));
    clap_istream_t assignableMotionInput { &assignableMotionState, stateRead };
    double migratedSecondaryTarget = -1.0;
    double migratedVelocityTarget = -1.0;
    double migratedBodyDecay = -1.0;
    double migratedAccent = -1.0;
    if (!state->load(plugin, &assignableMotionInput)
        || !params->get_value(plugin, 82u, &migratedSecondaryTarget)
        || !params->get_value(plugin, 88u, &migratedVelocityTarget)
        || !params->get_value(plugin, 94u, &migratedBodyDecay)
        || !params->get_value(plugin, 97u, &migratedAccent)
        || migratedSecondaryTarget != 0.0
        || migratedVelocityTarget != 1.0
        || migratedBodyDecay != 12.0 || migratedAccent != 1.0) {
        std::cerr << "version 6 routing/articulation migration failed\n";
        return 1;
    }

    const uint32_t expressionCount = 65u;
    MemoryState expressionState = saved;
    expressionState.offset = 0u;
    const uint32_t expressionVersion = 5u;
    std::memcpy(expressionState.bytes.data() + sizeof(uint32_t),
        &expressionVersion, sizeof(expressionVersion));
    std::memcpy(expressionState.bytes.data() + sizeof(uint32_t) * 2u,
        &expressionCount, sizeof(expressionCount));
    clap_istream_t expressionInput { &expressionState, stateRead };
    double migratedExpressionMode = -1.0;
    double migratedTranspose = -99.0;
    double migratedArpPattern = -99.0;
    if (!state->load(plugin, &expressionInput)
        || !params->get_value(plugin, 65u, &migratedExpressionMode)
        || !params->get_value(plugin, 66u, &migratedTranspose)
        || !params->get_value(plugin, 67u, &migratedArpPattern)
        || migratedExpressionMode != 1.0 || migratedTranspose != 0.0
        || migratedArpPattern != 0.0) {
        std::cerr << "version 5 transpose/arpeggiator migration failed\n";
        return 1;
    }

    const uint32_t previousCount = 64u;
    MemoryState directControlsState = saved;
    directControlsState.offset = 0u;
    const uint32_t directControlsVersion = 4u;
    std::memcpy(directControlsState.bytes.data() + sizeof(uint32_t),
        &directControlsVersion, sizeof(directControlsVersion));
    std::memcpy(directControlsState.bytes.data() + sizeof(uint32_t) * 2u,
        &previousCount, sizeof(previousCount));
    clap_istream_t directControlsInput { &directControlsState, stateRead };
    migratedExpressionMode = -1.0;
    if (!state->load(plugin, &directControlsInput)
        || !params->get_value(plugin, 65u, &migratedExpressionMode)
        || migratedExpressionMode != 0.0) {
        std::cerr << "version 4 expression-mode migration failed\n";
        return 1;
    }

    MemoryState macroState = saved;
    macroState.offset = 0u;
    const uint32_t macroVersion = 3u;
    std::memcpy(macroState.bytes.data() + sizeof(uint32_t),
        &macroVersion, sizeof(macroVersion));
    std::memcpy(macroState.bytes.data() + sizeof(uint32_t) * 2u,
        &previousCount, sizeof(previousCount));
    setStateValue(macroState, 6u, 0.80);  // Foundation Level
    setStateValue(macroState, 10u, 0.0);  // Pressure
    setStateValue(macroState, 11u, 0.70); // Body Level
    setStateValue(macroState, 14u, 0.20); // Body Width
    setStateValue(macroState, 22u, 0.10); // Filter Drive
    setStateValue(macroState, 36u, 0.40); // Mod 1 Depth
    setStateValue(macroState, 40u, -0.60); // Mod 2 Depth
    setStateValue(macroState, 44u, 0.80); // Mod 3 Depth
    setStateValue(macroState, 46u, 0.75); // Weight
    setStateValue(macroState, 47u, 0.75); // Motion
    setStateValue(macroState, 48u, 0.40); // Edge
    setStateValue(macroState, 49u, 0.60); // Space
    setStateValue(macroState, 52u, 0.20); // Shred
    setStateValue(macroState, 64u, 0.30); // Pressure Drive
    clap_istream_t macroInput { &macroState, stateRead };
    if (!state->load(plugin, &macroInput)) return 1;
    const std::array<std::pair<uint32_t, double>, 13u> migratedValues {{
        { 6u, 0.924 }, { 11u, 0.644 }, { 14u, 0.47 },
        { 22u, 0.30 }, { 36u, 0.55 }, { 40u, -0.825 },
        { 44u, 1.0 }, { 46u, 0.40 }, { 47u, 0.75 },
        { 48u, 0.048 }, { 49u, 0.47 }, { 52u, 0.296 },
        { 64u, 0.30 + 0.40 * (1.8 / 5.2) },
    }};
    for (const auto& expected : migratedValues) {
        double actual = 0.0;
        if (!params->get_value(plugin, expected.first, &actual)
            || std::fabs(actual - expected.second) > 1.0e-6) {
            std::cerr << "version 3 macro migration failed at parameter "
                      << expected.first << '\n';
            return 1;
        }
    }

    MemoryState previous = saved;
    previous.offset = 0u;
    const uint32_t previousVersion = 2u;
    const double modalEngine = 2.0;
    const double oldModes = 0.75;
    const double oldDecay = 0.60;
    const double oldStrike = 0.80;
    std::memcpy(previous.bytes.data() + sizeof(uint32_t),
        &previousVersion, sizeof(previousVersion));
    std::memcpy(previous.bytes.data() + sizeof(uint32_t) * 2u,
        &previousCount, sizeof(previousCount));
    std::memcpy(previous.bytes.data() + valueOffset
            + (10u - 1u) * sizeof(double),
        &modalEngine, sizeof(modalEngine));
    std::memcpy(previous.bytes.data() + valueOffset
            + (13u - 1u) * sizeof(double),
        &oldModes, sizeof(oldModes));
    std::memcpy(previous.bytes.data() + valueOffset
            + (62u - 1u) * sizeof(double),
        &oldDecay, sizeof(oldDecay));
    std::memcpy(previous.bytes.data() + valueOffset
            + (63u - 1u) * sizeof(double),
        &oldStrike, sizeof(oldStrike));
    clap_istream_t previousInput { &previous, stateRead };
    double migratedPosition = 0.0;
    double migratedDecay = 0.0;
    double migratedSustain = 0.0;
    const double expectedDecay = std::clamp(std::log(
        std::max(0.080, 0.055 * std::pow(90.0, oldDecay)) / 0.080)
            / std::log(43.75), 0.0, 1.0);
    if (!state->load(plugin, &previousInput)
        || !params->get_value(plugin, 13u, &migratedPosition)
        || !params->get_value(plugin, 62u, &migratedDecay)
        || !params->get_value(plugin, 63u, &migratedSustain)
        || std::fabs(migratedPosition - 0.62) > 1.0e-6
        || std::fabs(migratedDecay - expectedDecay) > 1.0e-6
        || std::fabs(migratedSustain - 0.34) > 1.0e-6) {
        std::cerr << "version 2 Modal state migration failed\n";
        return 1;
    }

    MemoryState legacy = saved;
    legacy.offset = 0u;
    const uint32_t legacyVersion = 1u;
    const uint32_t legacyCount = 61u;
    std::memcpy(legacy.bytes.data() + sizeof(uint32_t),
        &legacyVersion, sizeof(legacyVersion));
    std::memcpy(legacy.bytes.data() + sizeof(uint32_t) * 2u,
        &legacyCount, sizeof(legacyCount));
    setStateValue(legacy, 46u, 0.50); // Weight
    setStateValue(legacy, 47u, 0.50); // Motion
    setStateValue(legacy, 48u, 0.0);  // Edge
    setStateValue(legacy, 49u, 0.0);  // Space
    legacy.bytes.resize(sizeof(uint32_t) * 4u
        + static_cast<size_t>(legacyCount) * sizeof(double));
    if (!flush(plugin, params,
            { { 62u, 0.9 }, { 63u, 0.9 }, { 64u, 0.9 } })) return 1;
    clap_istream_t legacyInput { &legacy, stateRead };
    double controlC = 0.0;
    double controlD = 0.0;
    double controlE = 0.0;
    if (!state->load(plugin, &legacyInput)
        || !params->get_value(plugin, 62u, &controlC)
        || !params->get_value(plugin, 63u, &controlD)
        || !params->get_value(plugin, 64u, &controlE)
        || std::fabs(controlC - 0.40) > 1.0e-6
        || std::fabs(controlD - 0.24) > 1.0e-6
        || std::fabs(controlE - 0.50) > 1.0e-6) {
        std::cerr << "legacy Lowform state migration failed\n";
        return 1;
    }
    if (tail->get(plugin) == 0u) return 1;

    Events release;
    release.addNote(CLAP_EVENT_NOTE_OFF, -1, 0.0, 0u, -1);
    release.addMidi(0xb0u, 120u, 0u, 1u);
    audio.clear();
    processBlock(plugin, audio, &release.input);

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    std::cout << "Processor Lowform CLAP smoke passed\n";
    return 0;
}
