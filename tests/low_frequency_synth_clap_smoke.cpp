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
#include <limits>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.low-frequency-synth";
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 256u;

enum ParamId : clap_id {
    kTranspose = 1u,
    kFine,
    kFundamental,
    kBody,
    kLoading,
    kCoupling,
    kTension,
    kExcitation,
    kDamping,
    kNonlinearity,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kGlide,
    kPitchTransient,
    kPitchTransientTime,
    kStereoWidth,
    kVelocity,
    kPressure,
    kOutput,
    kMidiReceive,
    kHarmonics,
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvelope,
    kFilterDecay,
    kDrive,
    kWavefold,
    kDriveFeedback,
    kProcessedMix,
    kValvePreamp,
    kPowerStage,
    kSupplySag,
    kAmpBass,
    kAmpMid,
    kAmpMidFrequency,
    kAmpTreble,
    kCabinet,
    kPedalCircuit,
    kPedalDrive,
    kPedalTone,
    kPedalCharacter,
    kPedalCrossover,
    kPedalBlend,
    kFilterMotionRate,
    kFilterMotionDepth,
    kFilterMotionShape,
    kVoiceSpread,
    kFilterMotionClock,
    kFilterMotionDivision,
    kShred,
    kShredFeedback,
    kShredMix,
    kShredCircuit,
    kShredColor,
    kDriveDensityMacro,
    kAmplitudeMotionPosition,
    kShredFeedbackToneLevel,
};

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamSpec, 32u> kParamSpecs {{
    { kOutput, "Output", -36.0, 6.0, -8.0, false },
    { kFundamental, "Sub", 0.0, 1.0, 0.96, false },
    { kBody, "Modal Body", 0.0, 1.0, 0.70, false },
    { kLoading, "Mass Load", 0.0, 1.0, 0.82, false },
    { kCoupling, "Coupling", 0.0, 1.0, 0.50, false },
    { kDamping, "Damping", 0.0, 1.0, 0.28, false },
    { kExcitation, "Excitation", 0.0, 1.0, 0.25, false },
    { kHarmonics, "Upper Modes", 0.0, 1.0, 0.18, false },
    { kFilterCutoff, "Cutoff", 30.0, 12000.0, 1200.0, false },
    { kFilterResonance, "Resonance", 0.0, 1.0, 0.18, false },
    { kDrive, "Membrane Drive", 0.0, 1.0, 0.28, false },
    { kProcessedMix, "Filter Mix", 0.0, 1.0, 0.36, false },
    { kValvePreamp, "Tube", 0.0, 1.0, 0.36, false },
    { kAttack, "Attack", 0.0005, 2.0, 0.008, false },
    { kDecay, "Decay", 0.005, 5.0, 0.22, false },
    { kSustain, "Sustain", 0.0, 1.0, 0.86, false },
    { kRelease, "Release", 0.005, 8.0, 0.35, false },
    { kTranspose, "Transpose", -36.0, 24.0, 0.0, false },
    { kGlide, "Glide", 0.0, 2000.0, 35.0, false },
    { kPitchTransient, "Pitch Transient", -12.0, 36.0, 0.0, false },
    { kFilterMotionClock, "Clock", 0.0, 1.0, 1.0, true },
    { kFilterMotionDivision, "Division", 0.0, 15.0, 7.0, true },
    { kFilterMotionRate, "Free Rate", 0.05, 20.0, 2.0, false },
    { kFilterMotionDepth, "Amount", 0.0, 1.0, 0.0, false },
    { kMidiReceive, "MIDI Receive", 0.0, 16.0, 0.0, true },
    { kShred, "Shred", 0.0, 1.0, 0.0, false },
    { kShredFeedback, "Feedback", 0.0, 1.0, 0.0, false },
    { kShredMix, "Shred Mix", 0.0, 1.0, 0.0, false },
    { kShredCircuit, "Circuit", 0.0, 7.0, 0.0, true },
    { kShredColor, "Color", 0.0, 1.0, 0.55, false },
    { kAmplitudeMotionPosition, "Position", 0.0, 1.0, 1.0, true },
    { kShredFeedbackToneLevel, "Feedback Tone Level", 0.0, 1.0, 1.0, false },
}};

struct HostContext {
    clap_host_t host {};
    clap_host_params_t params {};
    clap_host_tail_t tail {};
    uint32_t rescans = 0u;
    uint32_t flushRequests = 0u;
    uint32_t processRequests = 0u;
    uint32_t tailChanges = 0u;
    uint32_t invalidTailCalls = 0u;
};

bool gInsideProcess = false;

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
    auto* context = hostContext(host);
    ++context->tailChanges;
    if (!gInsideProcess) ++context->invalidTailCalls;
}

struct EventList {
    std::array<clap_event_param_value_t, 32u> params {};
    std::array<clap_event_note_t, 16u> notes {};
    std::array<clap_event_note_expression_t, 4u> expressions {};
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

    bool addParam(clap_id id, double value, uint32_t time = 0u)
    {
        if (paramCount >= params.size()) return false;
        auto& event = params[paramCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = event.channel = event.key = -1;
        event.value = value;
        return insert(&event.header);
    }

    bool addNote(uint16_t type, int16_t key, double velocity,
        uint32_t time = 0u, int16_t channel = 0)
    {
        if (noteCount >= notes.size()) return false;
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.note_id = key;
        event.port_index = 0;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        return insert(&event.header);
    }

    bool addPressure(double value, uint32_t time = 0u, int16_t channel = 0)
    {
        if (expressionCount >= expressions.size()) return false;
        auto& event = expressions[expressionCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_EXPRESSION;
        event.expression_id = CLAP_NOTE_EXPRESSION_PRESSURE;
        event.note_id = event.port_index = event.key = -1;
        event.channel = channel;
        event.value = value;
        return insert(&event.header);
    }

    bool addMidi(uint8_t status, uint8_t first, uint8_t second,
        uint32_t time = 0u)
    {
        if (midiCount >= midi.size()) return false;
        auto& event = midi[midiCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.data[0] = status;
        event.data[1] = first;
        event.data[2] = second;
        return insert(&event.header);
    }
};

struct AudioBlock {
    std::array<std::array<float, kFrames>, 2u> storage {};
    std::array<float*, 2u> pointers {};
    clap_audio_buffer_t buffer {};

    AudioBlock()
    {
        pointers[0] = storage[0].data();
        pointers[1] = storage[1].data();
        buffer.data32 = pointers.data();
        buffer.channel_count = 2u;
    }

    void clear()
    {
        storage[0].fill(0.0f);
        storage[1].fill(0.0f);
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
    if (count == 0u) return 0;
    std::memcpy(destination, state->bytes.data() + state->offset, count);
    state->offset += count;
    return static_cast<int64_t>(count);
}

std::filesystem::path modulePath(std::filesystem::path path)
{
#if defined(__APPLE__)
    if (std::filesystem::is_directory(path)) {
        const std::string stem = path.stem().string();
        return path / "Contents" / "MacOS" / stem;
    }
#endif
    return path;
}

struct LoadedPlugin {
    void* handle = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_t* plugin = nullptr;
};

bool loadPlugin(const char* argument, HostContext& host, LoadedPlugin& loaded)
{
    const auto path = modulePath(argument);
    loaded.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!loaded.handle) {
        std::cerr << "dlopen failed: " << dlerror() << "\n";
        return false;
    }
    loaded.entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(loaded.handle, "clap_entry"));
    if (!loaded.entry || !loaded.entry->init(path.c_str())) return false;
    loaded.factory = static_cast<const clap_plugin_factory_t*>(
        loaded.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!loaded.factory || loaded.factory->get_plugin_count(loaded.factory) != 1u) {
        return false;
    }
    const auto* descriptor = loaded.factory->get_plugin_descriptor(
        loaded.factory, 0u);
    if (!descriptor || std::strcmp(descriptor->id, kPluginId) != 0
        || std::strcmp(descriptor->name, "s3g Processor LF Synth") != 0
        || std::strcmp(descriptor->version, "1.3.6") != 0) {
        std::cerr << "descriptor mismatch\n";
        return false;
    }
    loaded.plugin = loaded.factory->create_plugin(
        loaded.factory, &host.host, kPluginId);
    return loaded.plugin && loaded.plugin->init(loaded.plugin);
}

void unloadPlugin(LoadedPlugin& loaded)
{
    if (loaded.plugin) loaded.plugin->destroy(loaded.plugin);
    if (loaded.entry) loaded.entry->deinit();
    if (loaded.handle) dlclose(loaded.handle);
    loaded = {};
}

clap_process_status runBlock(const clap_plugin_t* plugin,
    EventList& events, AudioBlock& audio,
    const clap_event_transport_t* transport = nullptr)
{
    audio.clear();
    clap_process_t process {};
    process.frames_count = kFrames;
    process.in_events = &events.input;
    process.transport = transport;
    process.audio_outputs = &audio.buffer;
    process.audio_outputs_count = 1u;
    gInsideProcess = true;
    const auto status = plugin->process(plugin, &process);
    gInsideProcess = false;
    return status;
}

bool parameterContractProbe(const clap_plugin_t* plugin)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params || params->count(plugin) != kParamSpecs.size()) return false;
    for (uint32_t index = 0u; index < kParamSpecs.size(); ++index) {
        clap_param_info_t info {};
        const auto& expected = kParamSpecs[index];
        if (!params->get_info(plugin, index, &info)
            || info.id != expected.id
            || std::strcmp(info.name, expected.name) != 0
            || info.min_value != expected.minimum
            || info.max_value != expected.maximum
            || info.default_value != expected.defaultValue
            || ((info.flags & CLAP_PARAM_IS_STEPPED) != 0u)
                != expected.stepped
            || (info.flags & CLAP_PARAM_IS_HIDDEN) != 0u) {
            std::cerr << "parameter contract mismatch at " << index << "\n";
            return false;
        }
        double value = 0.0;
        char text[64] {};
        double parsed = 0.0;
        if (!params->get_value(plugin, expected.id, &value)
            || std::fabs(value - expected.defaultValue) > 1.0e-6
            || !params->value_to_text(
                plugin, expected.id, value, text, sizeof(text))
            || !params->text_to_value(plugin, expected.id, text, &parsed)
            || std::fabs(parsed - value) > 0.011) {
            std::cerr << "parameter text/default mismatch: "
                      << expected.name << " / " << text << "\n";
            return false;
        }
    }
    char preText[64] {};
    char postText[64] {};
    if (!params->value_to_text(plugin, kAmplitudeMotionPosition,
            0.0, preText, sizeof(preText))
        || !params->value_to_text(plugin, kAmplitudeMotionPosition,
            1.0, postText, sizeof(postText))
        || std::strcmp(preText, "PRE SHRED") != 0
        || std::strcmp(postText, "POST SHRED") != 0) {
        std::cerr << "Amplitude LFO Position menu labels mismatch: "
                  << preText << " / " << postText << "\n";
        return false;
    }
    constexpr std::array<clap_id, 8u> removed {{
        kFine, kTension, kWavefold, kDriveFeedback, kPedalCircuit,
        kVoiceSpread, kFilterMotionShape, kDriveDensityMacro,
    }};
    for (const clap_id id : removed) {
        double value = 0.0;
        if (params->get_value(plugin, id, &value)) {
            std::cerr << "removed parameter remained addressable: "
                      << id << "\n";
            return false;
        }
    }
    return true;
}

bool portContractProbe(const clap_plugin_t* plugin)
{
    const auto* audio = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notes = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    if (!audio || !notes || audio->count(plugin, false) != 1u
        || audio->count(plugin, true) != 0u
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

bool sustainedMidiProbe(const clap_plugin_t* plugin)
{
    EventList events;
    AudioBlock audio;
    events.addParam(kBody, 0.0);
    events.addParam(kFundamental, 1.0);
    events.addParam(kAttack, 0.001);
    events.addParam(kDecay, 0.005);
    events.addParam(kSustain, 1.0);
    events.addParam(kGlide, 0.0);
    events.addParam(kOutput, -12.0);
    events.addParam(kHarmonics, 0.0);
    events.addParam(kProcessedMix, 0.0);
    events.addParam(kValvePreamp, 0.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 33, 1.0);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) return false;

    uint32_t crossings = 0u;
    float previous = audio.storage[0].back();
    float peak = 0.0f;
    uint32_t measuredSamples = 0u;
    events.clear();
    for (uint32_t block = 0u; block < 420u; ++block) {
        const auto status = runBlock(plugin, events, audio);
        if (status != CLAP_PROCESS_CONTINUE) return false;
        for (float sample : audio.storage[0]) {
            peak = std::max(peak, std::fabs(sample));
            if (block >= 20u) {
                if (previous <= 0.0f && sample > 0.0f) ++crossings;
                ++measuredSamples;
            }
            previous = sample;
        }
    }
    const double measured = static_cast<double>(crossings) * kSampleRate
        / static_cast<double>(measuredSamples);
    if (measured < 54.5 || measured > 55.5 || peak < 0.08f
        || peak > 0.40f) {
        std::cerr << "CLAP sustained pitch/level mismatch: "
                  << measured << " Hz / " << peak << "\n";
        return false;
    }

    events.addNote(CLAP_EVENT_NOTE_OFF, 33, 0.0);
    runBlock(plugin, events, audio);
    events.clear();
    clap_process_status status = CLAP_PROCESS_CONTINUE;
    for (uint32_t block = 0u; block < 100u; ++block) {
        status = runBlock(plugin, events, audio);
        if (status == CLAP_PROCESS_SLEEP) break;
    }
    if (status != CLAP_PROCESS_SLEEP) {
        std::cerr << "CLAP release did not reach sleep\n";
        return false;
    }

    // Raw MIDI is accepted on the same input port.
    events.addMidi(0x90u, 33u, 100u);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) return false;
    events.clear();
    events.addMidi(0x80u, 33u, 0u);
    runBlock(plugin, events, audio);
    return true;
}

bool receiveAndPressureProbe(const clap_plugin_t* plugin)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    EventList events;
    AudioBlock audio;
    events.addParam(kMidiReceive, 2.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 36, 1.0, 0u, 0);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_SLEEP) {
        std::cerr << "MIDI receive accepted wrong CLAP channel\n";
        return false;
    }
    events.clear();
    events.addNote(CLAP_EVENT_NOTE_ON, 36, 0.8, 0u, 1);
    events.addPressure(1.0, 32u, 1);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) return false;
    double value = 0.0;
    return params->get_value(plugin, kMidiReceive, &value) && value == 2.0;
}

bool filterDriveProbe(const clap_plugin_t* plugin)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params) return false;
    EventList events;
    AudioBlock audio;
    events.addParam(kMidiReceive, 0.0);
    events.addParam(kFundamental, 0.72);
    events.addParam(kBody, 0.82);
    events.addParam(kHarmonics, 1.0);
    events.addParam(kFilterCutoff, 1800.0);
    events.addParam(kFilterResonance, 0.82);
    events.addParam(kDrive, 0.82);
    events.addParam(kProcessedMix, 1.0);
    events.addParam(kFilterMotionRate, 5.8);
    events.addParam(kFilterMotionDepth, 0.0);
    events.addParam(kOutput, -16.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 33, 1.0);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) return false;

    float previous = audio.storage[0].back();
    float peak = 0.0f;
    double energy = 0.0;
    double variation = 0.0;
    events.clear();
    for (uint32_t block = 0u; block < 96u; ++block) {
        if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
        for (float sample : audio.storage[0]) {
            if (!std::isfinite(sample)) return false;
            peak = std::max(peak, std::fabs(sample));
            energy += static_cast<double>(sample) * sample;
            variation += std::fabs(static_cast<double>(sample) - previous);
            previous = sample;
        }
    }
    double value = 0.0;
    if (peak < 0.01f || peak > 1.0f || energy < 0.01
        || variation < 1.0
        || !params->get_value(plugin, kDrive, &value)
        || std::fabs(value - 0.82) > 1.0e-6) {
        std::cerr << "CLAP filter/drive path lacked bounded activity\n";
        return false;
    }
    events.addNote(CLAP_EVENT_NOTE_OFF, 33, 0.0);
    runBlock(plugin, events, audio);
    return true;
}

bool directSurfaceProbe(const clap_plugin_t* plugin)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (!params) return false;
    EventList events;
    AudioBlock audio;
    events.addParam(kFundamental, 1.0);
    events.addParam(kBody, 1.0);
    events.addParam(kLoading, 1.0);
    events.addParam(kCoupling, 0.78);
    events.addParam(kDamping, 0.14);
    events.addParam(kExcitation, 0.34);
    events.addParam(kHarmonics, 0.36);
    events.addParam(kFilterResonance, 0.24);
    events.addParam(kDrive, 0.65);
    events.addParam(kProcessedMix, 0.58);
    events.addParam(kValvePreamp, 0.68);
    events.addParam(kAttack, 0.001);
    events.addParam(kSustain, 1.0);
    events.addParam(kOutput, -17.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 33, 1.0);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    double fundamental = 0.0;
    double body = 0.0;
    double loading = 0.0;
    double drive = 0.0;
    if (!params->get_value(plugin, kFundamental, &fundamental)
        || !params->get_value(plugin, kBody, &body)
        || !params->get_value(plugin, kLoading, &loading)
        || !params->get_value(plugin, kDrive, &drive)
        || fundamental < 0.99 || body < 0.99 || loading < 0.99
        || drive < 0.64 || drive > 0.66) {
        std::cerr << "CLAP direct surface did not retain thick settings\n";
        return false;
    }
    events.clear();
    float peak = 0.0f;
    double energy = 0.0;
    for (uint32_t block = 0u; block < 96u; ++block) {
        if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
        for (float sample : audio.storage[0u]) {
            if (!std::isfinite(sample)) return false;
            peak = std::max(peak, std::fabs(sample));
            energy += static_cast<double>(sample) * sample;
        }
    }
    if (peak < 0.01f || peak > 0.941f || energy < 0.01) {
        std::cerr << "CLAP direct surface lost bounded activity\n";
        return false;
    }
    events.addNote(CLAP_EVENT_NOTE_OFF, 33, 0.0);
    runBlock(plugin, events, audio);
    return true;
}

bool transportMotionProbe(const clap_plugin_t* plugin)
{
    EventList events;
    AudioBlock audio;
    events.addParam(kFundamental, 1.0);
    events.addParam(kBody, 0.0);
    events.addParam(kHarmonics, 0.0);
    events.addParam(kProcessedMix, 0.0);
    events.addParam(kDrive, 0.0);
    events.addParam(kValvePreamp, 0.0);
    events.addParam(kAttack, 0.001);
    events.addParam(kSustain, 1.0);
    events.addParam(kOutput, -12.0);
    events.addParam(kFilterMotionDepth, 1.0);
    events.addParam(kFilterMotionClock, 1.0);
    events.addParam(kFilterMotionDivision, 7.0); // Quarter note / one beat.
    events.addNote(CLAP_EVENT_NOTE_ON, 33, 1.0);

    clap_event_transport_t transport {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO
        | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    transport.tempo = 174.0;
    transport.song_pos_beats = 0;
    if (runBlock(plugin, events, audio, &transport)
            != CLAP_PROCESS_CONTINUE) return false;
    events.clear();
    for (uint32_t block = 0u; block < 64u; ++block) {
        runBlock(plugin, events, audio, &transport);
    }
    double closedEnergy = 0.0;
    for (uint32_t block = 0u; block < 32u; ++block) {
        runBlock(plugin, events, audio, &transport);
        for (float sample : audio.storage[0u]) {
            closedEnergy += static_cast<double>(sample) * sample;
        }
    }

    transport.song_pos_beats = static_cast<clap_beattime>(
        0.5 * static_cast<double>(CLAP_BEATTIME_FACTOR));
    float previous = audio.storage[0u].back();
    float maximumStep = 0.0f;
    for (uint32_t block = 0u; block < 64u; ++block) {
        runBlock(plugin, events, audio, &transport);
        for (float sample : audio.storage[0u]) {
            maximumStep = std::max(maximumStep, std::fabs(sample - previous));
            previous = sample;
        }
    }
    double openEnergy = 0.0;
    for (uint32_t block = 0u; block < 32u; ++block) {
        runBlock(plugin, events, audio, &transport);
        for (float sample : audio.storage[0u]) {
            if (!std::isfinite(sample)) return false;
            openEnergy += static_cast<double>(sample) * sample;
        }
    }
    if (openEnergy < 0.001 || closedEnergy > openEnergy * 0.005
        || maximumStep > 0.30f) {
        std::cerr << "transport amplitude LFO phase/seek mismatch: "
                  << closedEnergy << " / " << openEnergy << " / "
                  << maximumStep << "\n";
        return false;
    }

    events.addParam(kFilterMotionDepth, 0.0);
    events.addNote(CLAP_EVENT_NOTE_OFF, 33, 0.0);
    runBlock(plugin, events, audio, &transport);
    return true;
}

bool monophonicPriorityProbe(const clap_plugin_t* plugin)
{
    EventList events;
    AudioBlock audio;
    events.addParam(kMidiReceive, 0.0);
    events.addParam(kFundamental, 1.0);
    events.addParam(kBody, 0.0);
    events.addParam(kHarmonics, 0.0);
    events.addParam(kProcessedMix, 0.0);
    events.addParam(kValvePreamp, 0.0);
    events.addParam(kAttack, 0.001);
    events.addParam(kDecay, 0.005);
    events.addParam(kSustain, 1.0);
    events.addParam(kGlide, 0.0);
    events.addParam(kOutput, -10.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 33, 1.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 40, 1.0);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
        return false;
    }

    events.clear();
    for (uint32_t block = 0u; block < 24u; ++block) {
        if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
    }
    constexpr double frequencies[3u] {
        55.0, 82.4068892282, 110.0
    };
    double sine[2u][2u] {};
    double cosine[2u][2u] {};
    uint64_t sampleIndex = 0u;
    constexpr uint32_t measureBlocks = 96u;
    for (uint32_t block = 0u; block < measureBlocks; ++block) {
        if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
        for (uint32_t sample = 0u; sample < kFrames; ++sample, ++sampleIndex) {
            for (uint32_t tone = 0u; tone < 2u; ++tone) {
                const double phase = 6.28318530717958647692
                    * frequencies[tone] * static_cast<double>(sampleIndex)
                    / kSampleRate;
                const double s = std::sin(phase);
                const double c = std::cos(phase);
                for (uint32_t channel = 0u; channel < 2u; ++channel) {
                    const double value = audio.storage[channel][sample];
                    sine[channel][tone] += value * s;
                    cosine[channel][tone] += value * c;
                }
            }
        }
    }
    const double count = static_cast<double>(measureBlocks * kFrames);
    const auto amplitude = [&](uint32_t channel, uint32_t tone) {
        return 2.0 * std::hypot(sine[channel][tone], cosine[channel][tone])
            / count;
    };
    const double lowerLeft = amplitude(0u, 0u);
    const double lowerRight = amplitude(1u, 0u);
    const double upperLeft = amplitude(0u, 1u);
    const double upperRight = amplitude(1u, 1u);
    if (upperLeft < 0.05 || upperRight < 0.05
        || lowerLeft > upperLeft * 0.03
        || lowerRight > upperRight * 0.03
        || std::fabs(upperLeft - upperRight)
            > 0.01 * std::max(upperLeft, upperRight)) {
        std::cerr << "monophonic last-note priority mismatch: "
                  << lowerLeft << " / " << lowerRight << " / "
                  << upperLeft << " / " << upperRight << "\n";
        return false;
    }

    events.addNote(CLAP_EVENT_NOTE_ON, 45, 1.0);
    runBlock(plugin, events, audio);
    events.clear();
    events.addNote(CLAP_EVENT_NOTE_OFF, 45, 0.0);
    runBlock(plugin, events, audio);
    events.clear();
    for (uint32_t block = 0u; block < 8u; ++block) {
        if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
            std::cerr << "held voice was not restored after voice steal\n";
            return false;
        }
    }
    double restoredSine[2u] {};
    double restoredCosine[2u] {};
    sampleIndex = 0u;
    constexpr uint32_t restoredBlocks = 64u;
    for (uint32_t block = 0u; block < restoredBlocks; ++block) {
        runBlock(plugin, events, audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample, ++sampleIndex) {
            for (uint32_t tone = 0u; tone < 2u; ++tone) {
                const double phase = 6.28318530717958647692
                    * frequencies[tone + 1u]
                    * static_cast<double>(sampleIndex)
                    / kSampleRate;
                restoredSine[tone] += audio.storage[0u][sample]
                    * std::sin(phase);
                restoredCosine[tone] += audio.storage[0u][sample]
                    * std::cos(phase);
            }
        }
    }
    const double restoredCount = static_cast<double>(restoredBlocks * kFrames);
    const double restoredUpper = 2.0 * std::hypot(
        restoredSine[0u], restoredCosine[0u]) / restoredCount;
    const double releasedTop = 2.0 * std::hypot(
        restoredSine[1u], restoredCosine[1u]) / restoredCount;
    if (restoredUpper < 0.05 || releasedTop > restoredUpper * 0.05) {
        std::cerr << "monophonic held-note restoration mismatch: "
                  << restoredUpper << " / " << releasedTop << "\n";
        return false;
    }
    events.addNote(CLAP_EVENT_NOTE_OFF, 40, 0.0);
    runBlock(plugin, events, audio);
    events.clear();
    for (uint32_t block = 0u; block < 8u; ++block) {
        runBlock(plugin, events, audio);
    }
    double finalSine = 0.0;
    double finalCosine = 0.0;
    sampleIndex = 0u;
    for (uint32_t block = 0u; block < 48u; ++block) {
        runBlock(plugin, events, audio);
        for (uint32_t sample = 0u; sample < kFrames;
             ++sample, ++sampleIndex) {
            const double phase = 6.28318530717958647692
                * frequencies[0u] * static_cast<double>(sampleIndex)
                / kSampleRate;
            finalSine += audio.storage[0u][sample] * std::sin(phase);
            finalCosine += audio.storage[0u][sample] * std::cos(phase);
        }
    }
    const double finalCount = static_cast<double>(48u * kFrames);
    if (2.0 * std::hypot(finalSine, finalCosine) / finalCount < 0.05) {
        std::cerr << "monophonic base note was not restored\n";
        return false;
    }
    events.addNote(CLAP_EVENT_NOTE_OFF, 33, 0.0);
    runBlock(plugin, events, audio);
    return true;
}

bool releaseRetriggerContinuityProbe(const clap_plugin_t* plugin)
{
    EventList events;
    AudioBlock audio;
    events.addParam(kMidiReceive, 0.0);
    events.addParam(kFundamental, 1.0);
    events.addParam(kBody, 0.86);
    events.addParam(kHarmonics, 0.72);
    events.addParam(kAttack, 0.0005);
    events.addParam(kDecay, 0.01);
    events.addParam(kSustain, 1.0);
    events.addParam(kRelease, 0.65);
    events.addParam(kGlide, 0.0);
    events.addParam(kFilterCutoff, 5200.0);
    events.addParam(kFilterResonance, 0.72);
    events.addParam(kDrive, 0.88);
    events.addParam(kProcessedMix, 0.82);
    events.addParam(kValvePreamp, 0.86);
    events.addParam(kShred, 0.82);
    events.addParam(kShredFeedback, 0.68);
    events.addParam(kShredMix, 0.76);
    events.addParam(kShredCircuit, 1.0);
    events.addParam(kOutput, -6.0);
    events.addNote(CLAP_EVENT_NOTE_ON, 29, 1.0);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    events.clear();
    for (uint32_t block = 0u; block < 40u; ++block) {
        if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
    }

    // Release at one arbitrary phase and interrupt it later in the same host
    // block, verifying sample-offset event handling as well as the DSP lane.
    events.addNote(CLAP_EVENT_NOTE_OFF, 29, 0.0, 37u);
    events.addNote(CLAP_EVENT_NOTE_ON, 46, 0.23, 171u);
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    const float boundaryStep = std::fabs(audio.storage[0u][171u]
        - audio.storage[0u][170u]);
    if (boundaryStep > 1.0e-5f) {
        std::cerr << "CLAP interrupted-release boundary clicked: "
                  << boundaryStep << "\n";
        return false;
    }
    for (uint32_t sample = 0u; sample < kFrames; ++sample) {
        if (!std::isfinite(audio.storage[0u][sample])
            || !std::isfinite(audio.storage[1u][sample])) return false;
    }

    events.clear();
    int currentKey = 46;
    for (uint32_t event = 0u; event < 8u; ++event) {
        const uint32_t time = 8u + event * 30u;
        const int nextKey = 25 + static_cast<int>((event * 11u) % 31u);
        events.addNote(CLAP_EVENT_NOTE_OFF, currentKey, 0.0, time);
        events.addNote(CLAP_EVENT_NOTE_ON, nextKey,
            (event & 1u) != 0u ? 0.18 : 1.0, time);
        currentKey = nextKey;
    }
    if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
        return false;
    }
    float maximumStep = 0.0f;
    float previous = audio.storage[0u][0u];
    for (uint32_t sample = 1u; sample < kFrames; ++sample) {
        const float value = audio.storage[0u][sample];
        if (!std::isfinite(value)) return false;
        maximumStep = std::max(maximumStep, std::fabs(value - previous));
        previous = value;
    }
    if (maximumStep > 0.0401f) {
        std::cerr << "CLAP rapid retrigger handoff clicked: "
                  << maximumStep << "\n";
        return false;
    }
    events.clear();
    events.addNote(CLAP_EVENT_NOTE_OFF, currentKey, 0.0);
    runBlock(plugin, events, audio);
    return true;
}

bool tubeAudibilityProbe(const clap_plugin_t* plugin)
{
    std::array<double, 2u> energy {};
    std::array<double, 2u> variation {};
    for (uint32_t setting = 0u; setting < 2u; ++setting) {
        plugin->reset(plugin);
        EventList events;
        AudioBlock audio;
        events.addParam(kMidiReceive, 0.0);
        events.addParam(kFundamental, 0.96);
        events.addParam(kBody, 0.78);
        events.addParam(kLoading, 0.86);
        events.addParam(kHarmonics, 0.42);
        events.addParam(kDrive, 0.28);
        events.addParam(kProcessedMix, 0.52);
        events.addParam(kValvePreamp, static_cast<double>(setting));
        events.addParam(kAttack, 0.001);
        events.addParam(kSustain, 1.0);
        events.addParam(kOutput, -14.0);
        events.addNote(CLAP_EVENT_NOTE_ON, 33, 1.0);
        if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
            return false;
        }
        events.clear();
        float previous = audio.storage[0u].back();
        for (uint32_t block = 0u; block < 96u; ++block) {
            if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
                return false;
            }
            for (float sample : audio.storage[0u]) {
                if (!std::isfinite(sample)) return false;
                energy[setting] += static_cast<double>(sample) * sample;
                variation[setting] += std::fabs(
                    static_cast<double>(sample) - previous);
                previous = sample;
            }
        }
        events.addNote(CLAP_EVENT_NOTE_OFF, 33, 0.0);
        runBlock(plugin, events, audio);
    }
    const double cleanSignature = variation[0u]
        / std::sqrt(std::max(energy[0u], 1.0e-12));
    const double tubeSignature = variation[1u]
        / std::sqrt(std::max(energy[1u], 1.0e-12));
    if (energy[0u] < 0.01 || energy[1u] < 0.01
        || std::fabs(std::log(energy[1u] / energy[0u])) < 0.08
        || std::fabs(tubeSignature - cleanSignature) < 0.015) {
        std::cerr << "CLAP Tube control was not audibly distinct: "
                  << energy[0u] << " / " << energy[1u] << " / "
                  << cleanSignature << " / " << tubeSignature << "\n";
        return false;
    }
    return true;
}

bool stereoShredProbe(const clap_plugin_t* plugin)
{
    using StereoFrame = std::array<float, 2u>;
    const auto render = [plugin](double shred, double feedback, double mix,
                            double circuit = 0.0, double color = 0.55,
                            double feedbackToneLevel = 1.0) {
        plugin->reset(plugin);
        EventList events;
        AudioBlock audio;
        events.addParam(kMidiReceive, 0.0);
        events.addParam(kFundamental, 1.0);
        events.addParam(kBody, 0.0);
        events.addParam(kHarmonics, 0.0);
        events.addParam(kProcessedMix, 0.0);
        events.addParam(kValvePreamp, 0.0);
        events.addParam(kAttack, 0.001);
        events.addParam(kDecay, 0.005);
        events.addParam(kSustain, 1.0);
        events.addParam(kOutput, -18.0);
        events.addParam(kShred, shred);
        events.addParam(kShredFeedback, feedback);
        events.addParam(kShredFeedbackToneLevel, feedbackToneLevel);
        events.addParam(kShredMix, mix);
        events.addParam(kShredCircuit, circuit);
        events.addParam(kShredColor, color);
        runBlock(plugin, events, audio);
        plugin->reset(plugin);
        events.clear();
        events.addNote(CLAP_EVENT_NOTE_ON, 33, 1.0);
        std::vector<StereoFrame> result;
        result.reserve(80u * kFrames);
        for (uint32_t block = 0u; block < 96u; ++block) {
            if (runBlock(plugin, events, audio) != CLAP_PROCESS_CONTINUE) {
                result.clear();
                return result;
            }
            events.clear();
            if (block < 16u) continue;
            for (uint32_t sample = 0u; sample < kFrames; ++sample) {
                result.push_back({ audio.storage[0u][sample],
                    audio.storage[1u][sample] });
            }
        }
        events.addNote(CLAP_EVENT_NOTE_OFF, 33, 0.0);
        runBlock(plugin, events, audio);
        return result;
    };

    const auto clean = render(0.0, 0.0, 0.0);
    const auto zeroMix = render(1.0, 1.0, 0.0);
    const auto wet = render(0.74, 0.72, 1.0);
    const auto rat = render(0.68, 0.36, 0.72, 2.0, 0.38);
    const auto wool = render(0.68, 0.36, 0.72, 1.0, 0.68);
    const auto mutedReturn = render(0.74, 0.72, 1.0, 0.0, 0.55, 0.0);
    const auto sourceOnly = render(0.74, 0.0, 1.0, 0.0, 0.55, 0.0);
    if (clean.empty() || clean.size() != zeroMix.size()
        || clean.size() != wet.size() || clean.size() != rat.size()
        || clean.size() != wool.size() || clean.size() != mutedReturn.size()
        || clean.size() != sourceOnly.size()) return false;

    double difference = 0.0;
    double sideEnergy = 0.0;
    double circuitDifference = 0.0;
    double feedbackToneDifference = 0.0;
    float maximumZeroMixError = 0.0f;
    float feedbackIsolationError = 0.0f;
    for (size_t sample = 0u; sample < clean.size(); ++sample) {
        maximumZeroMixError = std::max(maximumZeroMixError,
            std::max(std::fabs(clean[sample][0u] - zeroMix[sample][0u]),
                std::fabs(clean[sample][1u] - zeroMix[sample][1u])));
        if (!std::isfinite(wet[sample][0u])
            || !std::isfinite(wet[sample][1u])) return false;
        difference += std::fabs(wet[sample][0u] - clean[sample][0u])
            + std::fabs(wet[sample][1u] - clean[sample][1u]);
        const double side = 0.5
            * static_cast<double>(wet[sample][0u] - wet[sample][1u]);
        sideEnergy += side * side;
        circuitDifference += std::fabs(rat[sample][0u] - wool[sample][0u])
            + std::fabs(rat[sample][1u] - wool[sample][1u]);
        feedbackToneDifference += std::fabs(wet[sample][0u]
                - mutedReturn[sample][0u])
            + std::fabs(wet[sample][1u] - mutedReturn[sample][1u]);
        feedbackIsolationError = std::max(feedbackIsolationError,
            std::max(std::fabs(mutedReturn[sample][0u]
                    - sourceOnly[sample][0u]),
                std::fabs(mutedReturn[sample][1u]
                    - sourceOnly[sample][1u])));
    }
    if (maximumZeroMixError > 1.0e-6f
        || difference < 5.0 || sideEnergy < 1.0e-7
        || circuitDifference < 5.0 || feedbackToneDifference < 1.0
        || feedbackIsolationError > 1.0e-4f) {
        std::cerr << "CLAP stereo Shred lacked audible independent lanes: "
                  << maximumZeroMixError << " / " << difference
                  << " / " << sideEnergy << " / "
                  << circuitDifference << " / " << feedbackToneDifference
                  << " / " << feedbackIsolationError << "\n";
        return false;
    }
    return true;
}

bool simplifiedStateAndTailProbe(const clap_plugin_t* plugin,
    HostContext& host)
{
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    if (!params || !state || !tail) return false;

    EventList events;
    AudioBlock audio;
    events.addParam(kLoading, 0.93);
    events.addParam(kCoupling, 0.84);
    events.addParam(kDrive, 0.68);
    events.addParam(kValvePreamp, 0.76);
    events.addParam(kFilterMotionRate, 4.7);
    events.addParam(kFilterMotionDepth, 0.72);
    events.addParam(kFilterMotionClock, 1.0);
    events.addParam(kFilterMotionDivision, 10.0);
    events.addParam(kShred, 0.64);
    events.addParam(kShredFeedback, 0.48);
    events.addParam(kShredFeedbackToneLevel, 0.67);
    events.addParam(kShredMix, 0.52);
    events.addParam(kShredCircuit, 2.0);
    events.addParam(kShredColor, 0.38);
    events.addParam(kAmplitudeMotionPosition, 0.0);
    events.addParam(kRelease, 0.20);
    runBlock(plugin, events, audio);
    events.clear();
    runBlock(plugin, events, audio);
    if (host.tailChanges == 0u || host.invalidTailCalls != 0u
        || tail->get(plugin) < 27090u || tail->get(plugin) > 27100u) {
        std::cerr << "tail contract mismatch\n";
        return false;
    }

    MemoryState memory;
    clap_ostream_t output { &memory, stateWrite };
    if (!state->save(plugin, &output)
        || memory.bytes.size() != 16u + 32u * sizeof(double)) {
        std::cerr << "simplified state size mismatch\n";
        return false;
    }
    events.addParam(kLoading, 0.12);
    events.addParam(kValvePreamp, 0.05);
    runBlock(plugin, events, audio);
    clap_istream_t input { &memory, stateRead };
    if (!state->load(plugin, &input)) return false;

    const auto valueIs = [&](clap_id id, double expected) {
        double value = 0.0;
        return params->get_value(plugin, id, &value)
            && std::fabs(value - expected) <= 1.0e-6;
    };
    if (!valueIs(kLoading, 0.93) || !valueIs(kCoupling, 0.84)
        || !valueIs(kDrive, 0.68) || !valueIs(kValvePreamp, 0.76)
        || !valueIs(kFilterMotionRate, 4.7)
        || !valueIs(kFilterMotionDepth, 0.72)
        || !valueIs(kFilterMotionClock, 1.0)
        || !valueIs(kFilterMotionDivision, 10.0)
        || !valueIs(kShred, 0.64)
        || !valueIs(kShredFeedback, 0.48)
        || !valueIs(kShredFeedbackToneLevel, 0.67)
        || !valueIs(kShredMix, 0.52)
        || !valueIs(kShredCircuit, 2.0)
        || !valueIs(kShredColor, 0.38)
        || !valueIs(kAmplitudeMotionPosition, 0.0)
        || host.rescans == 0u) {
        std::cerr << "simplified state roundtrip mismatch\n";
        return false;
    }

    MemoryState versionSix = memory;
    versionSix.bytes.resize(16u + 31u * sizeof(double));
    versionSix.offset = 0u;
    const uint32_t versionSixNumber = 6u;
    const uint32_t versionSixValueCount = 31u;
    std::memcpy(versionSix.bytes.data() + sizeof(uint32_t),
        &versionSixNumber, sizeof(versionSixNumber));
    std::memcpy(versionSix.bytes.data() + 2u * sizeof(uint32_t),
        &versionSixValueCount, sizeof(versionSixValueCount));
    clap_istream_t versionSixInput { &versionSix, stateRead };
    if (!state->load(plugin, &versionSixInput)
        || !valueIs(kShredFeedback, 0.48)
        || !valueIs(kAmplitudeMotionPosition, 0.0)
        || !valueIs(kShredFeedbackToneLevel, 1.0)) {
        std::cerr << "v1.3 state did not acquire unity feedback tone level\n";
        return false;
    }

    MemoryState versionFive = memory;
    versionFive.bytes.resize(16u + 30u * sizeof(double));
    versionFive.offset = 0u;
    const uint32_t versionFiveNumber = 5u;
    const uint32_t versionFiveValueCount = 30u;
    std::memcpy(versionFive.bytes.data() + sizeof(uint32_t),
        &versionFiveNumber, sizeof(versionFiveNumber));
    std::memcpy(versionFive.bytes.data() + 2u * sizeof(uint32_t),
        &versionFiveValueCount, sizeof(versionFiveValueCount));
    clap_istream_t versionFiveInput { &versionFive, stateRead };
    if (!state->load(plugin, &versionFiveInput)
        || !valueIs(kShredCircuit, 2.0)
        || !valueIs(kShredColor, 0.38)
        || !valueIs(kAmplitudeMotionPosition, 1.0)
        || !valueIs(kShredFeedbackToneLevel, 1.0)) {
        std::cerr << "v1.2 state did not acquire POST SHRED default\n";
        return false;
    }

    MemoryState versionFour = memory;
    versionFour.bytes.resize(16u + 28u * sizeof(double));
    versionFour.offset = 0u;
    const uint32_t versionFourNumber = 4u;
    const uint32_t versionFourValueCount = 28u;
    std::memcpy(versionFour.bytes.data() + sizeof(uint32_t),
        &versionFourNumber, sizeof(versionFourNumber));
    std::memcpy(versionFour.bytes.data() + 2u * sizeof(uint32_t),
        &versionFourValueCount, sizeof(versionFourValueCount));
    clap_istream_t versionFourInput { &versionFour, stateRead };
    if (!state->load(plugin, &versionFourInput)
        || !valueIs(kShred, 0.64)
        || !valueIs(kShredFeedback, 0.48)
        || !valueIs(kShredMix, 0.52)
        || !valueIs(kShredCircuit, 0.0)
        || !valueIs(kShredColor, 0.55)
        || !valueIs(kAmplitudeMotionPosition, 1.0)
        || !valueIs(kShredFeedbackToneLevel, 1.0)) {
        std::cerr << "v1.1 state did not acquire safe circuit defaults\n";
        return false;
    }

    MemoryState versionTwo = memory;
    versionTwo.bytes.resize(16u + 25u * sizeof(double));
    versionTwo.offset = 0u;
    const uint32_t previousVersion = 2u;
    const uint32_t previousValueCount = 25u;
    std::memcpy(versionTwo.bytes.data() + sizeof(uint32_t),
        &previousVersion, sizeof(previousVersion));
    std::memcpy(versionTwo.bytes.data() + 2u * sizeof(uint32_t),
        &previousValueCount, sizeof(previousValueCount));
    clap_istream_t versionTwoInput { &versionTwo, stateRead };
    if (!state->load(plugin, &versionTwoInput)
        || !valueIs(kFilterMotionDepth, 0.72)
        || !valueIs(kShred, 0.0)
        || !valueIs(kShredFeedback, 0.0)
        || !valueIs(kShredMix, 0.0)
        || !valueIs(kShredFeedbackToneLevel, 1.0)) {
        std::cerr << "v0.9 state was not accepted by the membrane engine\n";
        return false;
    }

    MemoryState versionOne = versionTwo;
    versionOne.offset = 0u;
    const uint32_t oldVersion = 1u;
    const double oldFilterMotionOctaves = 3.6;
    std::memcpy(versionOne.bytes.data() + sizeof(uint32_t),
        &oldVersion, sizeof(oldVersion));
    std::memcpy(versionOne.bytes.data() + 16u + 23u * sizeof(double),
        &oldFilterMotionOctaves, sizeof(oldFilterMotionOctaves));
    clap_istream_t versionOneInput { &versionOne, stateRead };
    double migratedAmount = -1.0;
    if (!state->load(plugin, &versionOneInput)
        || !params->get_value(plugin, kFilterMotionDepth, &migratedAmount)
        || std::fabs(migratedAmount - 0.60) > 1.0e-6) {
        std::cerr << "v0.8 filter-motion depth did not migrate to AM amount: "
                  << migratedAmount << "\n";
        return false;
    }

    MemoryState truncated = memory;
    truncated.offset = 0u;
    truncated.bytes.pop_back();
    clap_istream_t truncatedInput { &truncated, stateRead };
    if (state->load(plugin, &truncatedInput)) {
        std::cerr << "truncated state was accepted\n";
        return false;
    }
    MemoryState corrupt = memory;
    corrupt.offset = 0u;
    corrupt.bytes[0u] ^= 0xffu;
    clap_istream_t corruptInput { &corrupt, stateRead };
    if (state->load(plugin, &corruptInput)) {
        std::cerr << "corrupt state was accepted\n";
        return false;
    }
    struct LegacyHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t valueCount;
        uint32_t reserved;
    };
    MemoryState legacy;
    const LegacyHeader header { 0x464c3353u, 6u, 55u, 0u };
    legacy.bytes.resize(sizeof(header));
    std::memcpy(legacy.bytes.data(), &header, sizeof(header));
    clap_istream_t legacyInput { &legacy, stateRead };
    if (state->load(plugin, &legacyInput)) {
        std::cerr << "breaking legacy state schema was unexpectedly accepted\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3 || std::strcmp(argv[2], kPluginId) != 0) {
        std::cerr << "usage: low_frequency_synth_clap_smoke <plugin> "
                     "<plugin-id>\n";
        return 2;
    }

    HostContext host;
    host.params.rescan = hostParamsRescan;
    host.params.clear = hostParamsClear;
    host.params.request_flush = hostParamsRequestFlush;
    host.tail.changed = hostTailChanged;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "s3g LF Synth smoke host";
    host.host.vendor = "s3g";
    host.host.url = "https://github.com/s3g/s3g-dsp";
    host.host.version = "0.1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = hostRequestRestart;
    host.host.request_process = hostRequestProcess;
    host.host.request_callback = hostRequestCallback;

    LoadedPlugin loaded;
    if (!loadPlugin(argv[1], host, loaded)) {
        unloadPlugin(loaded);
        return 1;
    }
    bool ok = parameterContractProbe(loaded.plugin)
        && portContractProbe(loaded.plugin)
        && loaded.plugin->activate(
            loaded.plugin, kSampleRate, kFrames, kFrames)
        && loaded.plugin->start_processing(loaded.plugin)
        && sustainedMidiProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && filterDriveProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && directSurfaceProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && transportMotionProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && monophonicPriorityProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && releaseRetriggerContinuityProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && tubeAudibilityProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && stereoShredProbe(loaded.plugin);
    loaded.plugin->reset(loaded.plugin);
    ok = ok && receiveAndPressureProbe(loaded.plugin)
        && simplifiedStateAndTailProbe(loaded.plugin, host);
    loaded.plugin->stop_processing(loaded.plugin);
    loaded.plugin->deactivate(loaded.plugin);
    unloadPlugin(loaded);
    if (!ok) return 1;
    std::cout << "Processor LF Synth CLAP smoke passed\n";
    return 0;
}
