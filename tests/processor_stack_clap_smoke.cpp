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
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr const char* kPluginId = "org.s3g.s3g-dsp.processor-stack";
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 256u;

enum ParamId : clap_id {
    kMode = 1u,
    kShape,
    kWire,
    kPick,
    kDamping,
    kGlide,
    kCrooked,
    kSpill,
    kCircuit,
    kBite,
    kPedalTone,
    kBias,
    kStack,
    kSag,
    kFocus,
    kCone,
    kCabinet,
    kMic,
    kFeedback,
    kProximity,
    kHarmonic,
    kTracking,
    kPolarity,
    kRoot,
    kChaos,
    kOutput,
    kMidiReceive,
    kArpPattern,
    kScale,
    kArpRate,
    kArpOctaves,
    kArpGate,
    kCustomLength,
    kCustomStep1,
    kCustomStep2,
    kCustomStep3,
    kCustomStep4,
    kCustomStep5,
    kCustomStep6,
    kCustomStep7,
    kCustomStep8,
    kPierce,
    kSelfListen,
};

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamSpec, 43u> kParamSpecs {{
    { kMode, "Mode", 0.0, 2.0, 0.0, true },
    { kShape, "Shape", 0.0, 1.0, 0.58, false },
    { kWire, "String", 0.0, 1.0, 0.56, false },
    { kPick, "Pick", 0.0, 1.0, 0.72, false },
    { kDamping, "Damp", 0.0, 1.0, 0.38, false },
    { kGlide, "Glide", 0.0, 2000.0, 34.0, false },
    { kCrooked, "Crooked", 0.0, 1.0, 0.36, false },
    { kSpill, "Spill", 0.0, 1.0, 0.32, false },
    { kCircuit, "Circuit", 0.0, 7.0, 2.0, true },
    { kBite, "Bite", 0.0, 1.0, 0.56, false },
    { kPedalTone, "Tone", 0.0, 1.0, 0.54, false },
    { kBias, "Bias", 0.0, 1.0, 0.52, false },
    { kStack, "Stack", 0.0, 1.0, 0.62, false },
    { kSag, "Sag", 0.0, 1.0, 0.46, false },
    { kFocus, "Focus", 0.0, 1.0, 0.55, false },
    { kCone, "Cone", 0.0, 1.0, 0.64, false },
    { kCabinet, "Cab", 0.0, 1.0, 0.52, false },
    { kMic, "Mic", 0.0, 1.0, 0.34, false },
    { kFeedback, "Feedback", 0.0, 1.0, 0.56, false },
    { kProximity, "Proximity", 0.0, 1.0, 0.58, false },
    { kHarmonic, "Harmonic", 0.0, 1.0, 0.42, false },
    { kTracking, "Track", 0.0, 1.0, 0.72, false },
    { kPolarity, "Polarity", 0.0, 1.0, 0.78, false },
    { kRoot, "Root", 0.0, 1.0, 0.28, false },
    { kChaos, "Chaos", 0.0, 1.0, 0.32, false },
    { kOutput, "Output", -36.0, 6.0, -12.0, false },
    { kMidiReceive, "MIDI Receive", 0.0, 16.0, 0.0, true },
    { kArpPattern, "Arp Pattern", 0.0, 6.0, 0.0, true },
    { kScale, "Scale Rule", 0.0, 4.0, 1.0, true },
    { kArpRate, "Arp Rate", 0.0, 5.0, 2.0, true },
    { kArpOctaves, "Arp Octaves", 1.0, 4.0, 2.0, true },
    { kArpGate, "Arp Gate", 0.05, 1.0, 0.62, false },
    { kCustomLength, "Pattern Length", 1.0, 8.0, 8.0, true },
    { kCustomStep1, "Pattern Step 1", -8.0, 15.0, 0.0, true },
    { kCustomStep2, "Pattern Step 2", -8.0, 15.0, 1.0, true },
    { kCustomStep3, "Pattern Step 3", -8.0, 15.0, 2.0, true },
    { kCustomStep4, "Pattern Step 4", -8.0, 15.0, 4.0, true },
    { kCustomStep5, "Pattern Step 5", -8.0, 15.0, 3.0, true },
    { kCustomStep6, "Pattern Step 6", -8.0, 15.0, 6.0, true },
    { kCustomStep7, "Pattern Step 7", -8.0, 15.0, 5.0, true },
    { kCustomStep8, "Pattern Step 8", -8.0, 15.0, 1.0, true },
    { kPierce, "Pierce", 0.0, 1.0, 0.68, false },
    { kSelfListen, "Self Listen", 0.0, 1.0, 0.72, false },
}};

struct HostContext {
    clap_host_t host {};
    clap_host_params_t params {};
    clap_host_tail_t tail {};
    uint32_t rescans = 0u;
    uint32_t flushRequests = 0u;
    uint32_t processRequests = 0u;
    uint32_t tailChanges = 0u;
};

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (id && std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &hostContext(host)->params;
    }
    if (id && std::strcmp(id, CLAP_EXT_TAIL) == 0) {
        return &hostContext(host)->tail;
    }
    return nullptr;
}

void hostRequestRestart(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t* host)
{
    ++hostContext(host)->processRequests;
}
void hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags)
{
    ++hostContext(host)->rescans;
}
void hostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostParamsRequestFlush(const clap_host_t* host)
{
    ++hostContext(host)->flushRequests;
}
void hostTailChanged(const clap_host_t* host)
{
    ++hostContext(host)->tailChanges;
}

struct EventList {
    std::array<clap_event_param_value_t, 32u> params {};
    std::array<clap_event_note_t, 16u> notes {};
    std::array<clap_event_note_expression_t, 8u> expressions {};
    std::array<clap_event_midi_t, 8u> midi {};
    std::array<const clap_event_header_t*, 64u> events {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t expressionCount = 0u;
    uint32_t midiCount = 0u;
    uint32_t eventCount = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const EventList*>(list->ctx)->eventCount;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return index < self->eventCount ? self->events[index] : nullptr;
        },
    };

    void clear()
    {
        paramCount = noteCount = expressionCount = midiCount = eventCount = 0u;
    }

    bool insert(const clap_event_header_t* event)
    {
        if (!event || eventCount >= events.size()) return false;
        uint32_t index = eventCount;
        while (index > 0u && events[index - 1u]->time > event->time) {
            events[index] = events[index - 1u];
            --index;
        }
        events[index] = event;
        ++eventCount;
        return true;
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
        event.note_id = event.port_index = event.channel = event.key = -1;
        event.value = value;
        (void)insert(&event.header);
    }

    void addNote(uint16_t type, int key, double velocity,
        uint32_t time = 0u, int channel = 0)
    {
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = key;
        event.port_index = 0;
        event.channel = static_cast<int16_t>(channel);
        event.key = static_cast<int16_t>(key);
        event.velocity = velocity;
        (void)insert(&event.header);
    }

    void addExpression(clap_note_expression id, double value,
        uint32_t time = 0u, int key = -1, int channel = 0)
    {
        auto& event = expressions[expressionCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.expression_id = id;
        event.note_id = -1;
        event.port_index = 0;
        event.channel = static_cast<int16_t>(channel);
        event.key = static_cast<int16_t>(key);
        event.value = value;
        (void)insert(&event.header);
    }

    void addMidi(uint8_t first, uint8_t second, uint8_t third,
        uint32_t time = 0u)
    {
        auto& event = midi[midiCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0] = first;
        event.data[1] = second;
        event.data[2] = third;
        (void)insert(&event.header);
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
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + requested);
    return static_cast<int64_t>(requested);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->bytes.size() - std::min(
        state->offset, state->bytes.size());
    const size_t count = std::min<size_t>(
        available, static_cast<size_t>(requested));
    std::memcpy(destination, state->bytes.data() + state->offset, count);
    state->offset += count;
    return static_cast<int64_t>(count);
}

struct AudioBlock {
    std::array<float, kFrames> left {};
    std::array<float, kFrames> right {};
    std::array<float*, 2u> channels {{ left.data(), right.data() }};
    clap_audio_buffer_t output {};
    clap_process_t process {};

    AudioBlock(EventList* events = nullptr)
    {
        output.data32 = channels.data();
        output.channel_count = 2u;
        process.steady_time = -1;
        process.frames_count = kFrames;
        process.in_events = events ? &events->input : nullptr;
        process.audio_outputs = &output;
        process.audio_outputs_count = 1u;
    }

    void clear()
    {
        left.fill(0.0f);
        right.fill(0.0f);
    }

    double energy() const
    {
        double result = 0.0;
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            result += static_cast<double>(left[frame]) * left[frame]
                + static_cast<double>(right[frame]) * right[frame];
        }
        return result;
    }

    bool finite() const
    {
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            if (!std::isfinite(left[frame]) || !std::isfinite(right[frame])
                || std::abs(left[frame]) > 1.0f
                || std::abs(right[frame]) > 1.0f) return false;
        }
        return true;
    }
};

bool verifyParams(const clap_plugin_t* plugin)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params || params->count(plugin) != kParamSpecs.size()) return false;
    for (uint32_t index = 0u; index < kParamSpecs.size(); ++index) {
        clap_param_info_t info {};
        const auto& expected = kParamSpecs[index];
        double value = 0.0;
        const bool valid = params->get_info(plugin, index, &info)
            && info.id == expected.id
            && std::strcmp(info.name, expected.name) == 0
            && info.min_value == expected.minimum
            && info.max_value == expected.maximum
            && info.default_value == expected.defaultValue
            && (((info.flags & CLAP_PARAM_IS_STEPPED) != 0u)
                == expected.stepped)
            && params->get_value(plugin, expected.id, &value)
            && std::abs(value - expected.defaultValue) < 1.0e-6;
        if (!valid) {
            std::cerr << "parameter " << index << " mismatch: id="
                      << info.id << "/" << expected.id << " name="
                      << info.name << "/" << expected.name << " range="
                      << info.min_value << ".." << info.max_value << "/"
                      << expected.minimum << ".." << expected.maximum
                      << " default=" << info.default_value << "/"
                      << expected.defaultValue << " value=" << value << "\n";
            return false;
        }
    }
    char text[64] {};
    double value = 0.0;
    return params->value_to_text(plugin, kMode, 2.0, text, sizeof(text))
        && std::strcmp(text, "LEAD") == 0
        && params->text_to_value(plugin, kCircuit, "FUZZ II", &value)
        && value == 6.0
        && params->text_to_value(plugin, kArpPattern, "SCRAMBLE", &value)
        && value == 5.0
        && params->text_to_value(plugin, kArpPattern, "CUSTOM", &value)
        && value == 6.0
        && params->text_to_value(plugin, kScale, "DIMINISHED", &value)
        && value == 3.0
        && params->text_to_value(plugin, kArpRate, "1/16T", &value)
        && value == 3.0
        && params->text_to_value(plugin, kPolarity, "-50%", &value)
        && std::abs(value - 0.25) < 1.0e-9;
}

bool verifyPorts(const clap_plugin_t* plugin)
{
    const auto* audio = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notes = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    if (!audio || !notes || audio->count(plugin, true) != 0u
        || audio->count(plugin, false) != 1u
        || notes->count(plugin, true) != 1u
        || notes->count(plugin, false) != 0u) return false;
    clap_audio_port_info_t audioInfo {};
    clap_note_port_info_t noteInfo {};
    return audio->get(plugin, 0u, false, &audioInfo)
        && audioInfo.channel_count == 2u
        && audioInfo.port_type
        && std::strcmp(audioInfo.port_type, CLAP_PORT_STEREO) == 0
        && notes->get(plugin, 0u, true, &noteInfo)
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u;
}

bool processChecks(const clap_plugin_t* plugin)
{
    AudioBlock idle;
    if (plugin->process(plugin, &idle.process) != CLAP_PROCESS_SLEEP
        || idle.energy() != 0.0) {
        std::cerr << "idle CLAP output did not sleep at exact silence\n";
        return false;
    }

    EventList events;
    events.addNote(CLAP_EVENT_NOTE_ON, 43, 0.92, 13u);
    events.addExpression(CLAP_NOTE_EXPRESSION_PRESSURE, 0.78, 96u, 43);
    events.addExpression(CLAP_NOTE_EXPRESSION_TUNING, 1.25, 132u, 43);
    AudioBlock noteBlock(&events);
    if (plugin->process(plugin, &noteBlock.process) == CLAP_PROCESS_ERROR
        || !noteBlock.finite() || noteBlock.energy() < 1.0e-8) {
        std::cerr << "CLAP note/expression render was not finite and audible\n";
        return false;
    }

    events.clear();
    events.addParam(kMode, 1.0, 0u);
    events.addParam(kFeedback, 0.84, 0u);
    events.addParam(kSpill, 0.72, 0u);
    events.addNote(CLAP_EVENT_NOTE_ON, 47, 0.78, 8u);
    events.addNote(CLAP_EVENT_NOTE_ON, 54, 0.72, 64u);
    events.addMidi(0xe0u, 0x00u, 0x60u, 112u);
    noteBlock.clear();
    if (plugin->process(plugin, &noteBlock.process) == CLAP_PROCESS_ERROR
        || !noteBlock.finite() || noteBlock.energy() < 1.0e-8) {
        std::cerr << "HAND mode and MIDI bend were not processed\n";
        return false;
    }

    events.clear();
    events.addNote(CLAP_EVENT_NOTE_OFF, 43, 0.0, 0u);
    events.addNote(CLAP_EVENT_NOTE_OFF, 47, 0.0, 16u);
    events.addNote(CLAP_EVENT_NOTE_OFF, 54, 0.0, 32u);
    noteBlock.clear();
    if (plugin->process(plugin, &noteBlock.process) == CLAP_PROCESS_ERROR
        || !noteBlock.finite()) return false;

    plugin->reset(plugin);
    events.clear();
    events.addParam(kMode, 2.0);
    events.addParam(kArpPattern, 6.0);
    events.addParam(kScale, 4.0);
    events.addParam(kArpRate, 5.0);
    events.addParam(kArpOctaves, 3.0);
    events.addParam(kArpGate, 0.34);
    events.addParam(kCustomLength, 4.0);
    events.addParam(kCustomStep1, 0.0);
    events.addParam(kCustomStep2, 4.0);
    events.addParam(kCustomStep3, -1.0);
    events.addParam(kCustomStep4, 6.0);
    events.addParam(kPierce, 1.0);
    events.addParam(kSelfListen, 1.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 40, 0.9, 0u);
    clap_event_transport_t transport {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO;
    transport.tempo = 180.0;
    noteBlock.process.transport = &transport;
    double arpEnergy = 0.0;
    for (uint32_t blockIndex = 0u; blockIndex < 24u; ++blockIndex) {
        noteBlock.clear();
        if (plugin->process(plugin, &noteBlock.process) == CLAP_PROCESS_ERROR
            || !noteBlock.finite()) {
            std::cerr << "tempo-synced scale arpeggiator was not finite\n";
            return false;
        }
        arpEnergy += noteBlock.energy();
        events.clear();
    }
    noteBlock.process.transport = nullptr;
    if (arpEnergy < 1.0e-8) {
        std::cerr << "tempo-synced scale arpeggiator was silent\n";
        return false;
    }

    // MIDI channel filtering is shared by CLAP-note and raw MIDI events.
    plugin->reset(plugin);
    events.clear();
    events.addParam(kMidiReceive, 2.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 50, 0.8, 0u, 0);
    noteBlock.clear();
    if (plugin->process(plugin, &noteBlock.process) != CLAP_PROCESS_SLEEP
        || noteBlock.energy() != 0.0) {
        std::cerr << "MIDI receive accepted the wrong channel\n";
        return false;
    }
    events.clear();
    events.addMidi(0x91u, 50u, 100u, 0u);
    noteBlock.clear();
    if (plugin->process(plugin, &noteBlock.process) == CLAP_PROCESS_ERROR
        || noteBlock.energy() < 1.0e-8 || !noteBlock.finite()) {
        std::cerr << "MIDI receive rejected the selected raw MIDI channel\n";
        return false;
    }
    return true;
}

bool stateAndTailChecks(const clap_plugin_t* plugin, HostContext& host)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    if (!params || !state || !tail) return false;

    EventList events;
    events.addParam(kFeedback, 0.91);
    events.addParam(kSpill, 0.77);
    events.addParam(kOutput, -17.0);
    events.addParam(kArpPattern, 6.0);
    events.addParam(kScale, 2.0);
    events.addParam(kArpRate, 3.0);
    events.addParam(kArpOctaves, 3.0);
    events.addParam(kArpGate, 0.41);
    events.addParam(kCustomLength, 4.0);
    events.addParam(kCustomStep1, 0.0);
    events.addParam(kCustomStep2, 5.0);
    events.addParam(kCustomStep3, -1.0);
    events.addParam(kCustomStep4, 7.0);
    events.addParam(kPierce, 0.93);
    events.addParam(kSelfListen, 0.88);
    AudioBlock block(&events);
    if (plugin->process(plugin, &block.process) == CLAP_PROCESS_ERROR
        || host.tailChanges == 0u
        || tail->get(plugin) < static_cast<uint32_t>(kSampleRate * 11.0)
        || tail->get(plugin) > static_cast<uint32_t>(kSampleRate * 14.0)) {
        std::cerr << "Processor Stack tail contract mismatch\n";
        return false;
    }

    MemoryState memory;
    clap_ostream_t output { &memory, stateWrite };
    if (!state->save(plugin, &output) || memory.bytes.size() != 360u) {
        std::cerr << "Processor Stack state size mismatch: "
                  << memory.bytes.size() << "\n";
        return false;
    }
    events.clear();
    events.addParam(kOutput, -3.0);
    block.clear();
    if (plugin->process(plugin, &block.process) == CLAP_PROCESS_ERROR) {
        return false;
    }
    double value = 0.0;
    if (!params->get_value(plugin, kOutput, &value) || value != -3.0) {
        return false;
    }
    memory.offset = 0u;
    clap_istream_t input { &memory, stateRead };
    if (!state->load(plugin, &input)
        || !params->get_value(plugin, kOutput, &value)
        || value != -17.0 || host.rescans == 0u) {
        std::cerr << "Processor Stack state roundtrip mismatch\n";
        return false;
    }
    if (!params->get_value(plugin, kArpPattern, &value) || value != 6.0
        || !params->get_value(plugin, kArpGate, &value)
        || std::abs(value - 0.41) > 1.0e-6
        || !params->get_value(plugin, kCustomLength, &value) || value != 4.0
        || !params->get_value(plugin, kCustomStep3, &value) || value != -1.0
        || !params->get_value(plugin, kPierce, &value)
        || std::abs(value - 0.93) > 1.0e-6
        || !params->get_value(plugin, kSelfListen, &value)
        || std::abs(value - 0.88) > 1.0e-6) {
        std::cerr << "Processor Stack arpeggiator state mismatch\n";
        return false;
    }

    struct LegacyHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t valueCount;
        uint32_t reserved;
    };
    LegacyHeader versionTwoHeader { 0x31545350u, 2u, 32u, 0u };
    std::array<double, 32u> versionTwoValues {};
    for (uint32_t index = 0u; index < versionTwoValues.size(); ++index) {
        versionTwoValues[index] = kParamSpecs[index].defaultValue;
    }
    versionTwoValues[static_cast<size_t>(kOutput - 1u)] = -18.0;
    MemoryState versionTwo;
    clap_ostream_t versionTwoOutput { &versionTwo, stateWrite };
    if (stateWrite(&versionTwoOutput, &versionTwoHeader,
            sizeof(versionTwoHeader)) < 0
        || stateWrite(&versionTwoOutput, versionTwoValues.data(),
            sizeof(versionTwoValues)) < 0) {
        return false;
    }
    versionTwo.offset = 0u;
    clap_istream_t versionTwoInput { &versionTwo, stateRead };
    if (!state->load(plugin, &versionTwoInput)
        || !params->get_value(plugin, kOutput, &value) || value != -18.0
        || !params->get_value(plugin, kCustomLength, &value) || value != 8.0
        || !params->get_value(plugin, kCustomStep4, &value) || value != 4.0
        || !params->get_value(plugin, kPierce, &value)
        || std::abs(value - 0.68) > 1.0e-6
        || !params->get_value(plugin, kSelfListen, &value)
        || std::abs(value - 0.72) > 1.0e-6) {
        std::cerr << "version 2 Processor Stack state did not migrate\n";
        return false;
    }

    LegacyHeader legacyHeader { 0x31545350u, 1u, 27u, 0u };
    std::array<double, 27u> legacyValues {};
    for (uint32_t index = 0u; index < legacyValues.size(); ++index) {
        legacyValues[index] = kParamSpecs[index].defaultValue;
    }
    legacyValues[static_cast<size_t>(kOutput - 1u)] = -19.0;
    MemoryState legacy;
    clap_ostream_t legacyOutput { &legacy, stateWrite };
    if (stateWrite(&legacyOutput, &legacyHeader, sizeof(legacyHeader)) < 0
        || stateWrite(&legacyOutput, legacyValues.data(),
            sizeof(legacyValues)) < 0) {
        return false;
    }
    legacy.offset = 0u;
    clap_istream_t legacyInput { &legacy, stateRead };
    if (!state->load(plugin, &legacyInput)
        || !params->get_value(plugin, kOutput, &value) || value != -19.0
        || !params->get_value(plugin, kArpPattern, &value) || value != 0.0
        || !params->get_value(plugin, kScale, &value) || value != 1.0
        || !params->get_value(plugin, kArpRate, &value) || value != 2.0
        || !params->get_value(plugin, kArpOctaves, &value) || value != 2.0
        || !params->get_value(plugin, kArpGate, &value)
        || std::abs(value - 0.62) > 1.0e-6
        || !params->get_value(plugin, kCustomLength, &value) || value != 8.0
        || !params->get_value(plugin, kCustomStep8, &value) || value != 1.0
        || !params->get_value(plugin, kPierce, &value)
        || std::abs(value - 0.68) > 1.0e-6
        || !params->get_value(plugin, kSelfListen, &value)
        || std::abs(value - 0.72) > 1.0e-6) {
        std::cerr << "version 1 Processor Stack state did not migrate\n";
        return false;
    }

    MemoryState truncated = memory;
    truncated.bytes.pop_back();
    truncated.offset = 0u;
    clap_istream_t truncatedInput { &truncated, stateRead };
    if (state->load(plugin, &truncatedInput)) {
        std::cerr << "truncated Processor Stack state was accepted\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: processor_stack_clap_smoke <plugin>\n";
        return 2;
    }
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        std::cerr << "dlopen failed: " << dlerror() << "\n";
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !clap_version_is_compatible(entry->clap_version)
        || !entry->init(argv[1])) {
        std::cerr << "invalid CLAP entry\n";
        dlclose(library);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) != 1u) {
        std::cerr << "invalid Processor Stack factory\n";
        entry->deinit();
        dlclose(library);
        return 1;
    }
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0u);
    if (!descriptor || std::strcmp(descriptor->id, kPluginId) != 0
        || std::strcmp(descriptor->name, "s3g Processor Stack") != 0) {
        std::cerr << "Processor Stack descriptor mismatch\n";
        entry->deinit();
        dlclose(library);
        return 1;
    }

    HostContext host;
    host.params.rescan = hostParamsRescan;
    host.params.clear = hostParamsClear;
    host.params.request_flush = hostParamsRequestFlush;
    host.tail.changed = hostTailChanged;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "Processor Stack Smoke Host";
    host.host.vendor = "s3g";
    host.host.version = "1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = hostRequestRestart;
    host.host.request_process = hostRequestProcess;
    host.host.request_callback = hostRequestCallback;

    const clap_plugin_t* plugin = factory->create_plugin(
        factory, &host.host, kPluginId);
    bool ok = plugin != nullptr;
    if (!ok) std::cerr << "Processor Stack instance creation failed\n";
    if (ok && !plugin->init(plugin)) {
        std::cerr << "Processor Stack init failed\n";
        ok = false;
    }
    if (ok && !verifyParams(plugin)) {
        std::cerr << "Processor Stack parameter contract failed\n";
        ok = false;
    }
    if (ok && !verifyPorts(plugin)) {
        std::cerr << "Processor Stack port contract failed\n";
        ok = false;
    }
    if (ok && !plugin->activate(plugin, kSampleRate, 1u, kFrames)) {
        std::cerr << "Processor Stack activation failed\n";
        ok = false;
    }
    if (ok && !plugin->start_processing(plugin)) {
        std::cerr << "Processor Stack start-processing failed\n";
        ok = false;
    }
    if (ok) ok = processChecks(plugin);
    if (ok) ok = stateAndTailChecks(plugin, host);
    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    entry->deinit();
    dlclose(library);
    if (!ok) return 1;
    std::cout << "Processor Stack CLAP smoke test passed\n";
    return 0;
}
