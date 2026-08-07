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

constexpr const char* kPluginId = "org.s3g.s3g-dsp.drum-kick";
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 2u;

enum ParamId : clap_id {
    kTune = 1u,
    kNoteTracking,
    kPitchDrop,
    kSweepTime,
    kPitchSettle,
    kBody,
    kHarmonics,
    kDecay,
    kTail,
    kPunch,
    kClick,
    kClickTone,
    kClickDecay,
    kTexture,
    kTextureTone,
    kTextureDecay,
    kDrive,
    kBias,
    kCompression,
    kRateReduction,
    kBitDepth,
    kReconstruction,
    kCharacterTone,
    kStereoWidth,
    kVelocitySensitivity,
    kOutputGain,
    kTrigger,
    kMidiReceive,
};

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamSpec, 28u> kParamSpecs {{
    { kTune, "Tune", 20.0, 180.0, 48.0, false },
    { kNoteTracking, "Note Tracking", 0.0, 1.0, 1.0, false },
    { kPitchDrop, "Pitch Drop", -12.0, 60.0, 24.0, false },
    { kSweepTime, "Sweep Time", 1.0, 500.0, 45.0, false },
    { kPitchSettle, "Pitch Settle", 0.0, 1.0, 0.25, false },
    { kBody, "Body", 0.0, 1.0, 0.25, false },
    { kHarmonics, "Harmonics", 0.0, 1.0, 0.10, false },
    { kDecay, "Decay", 0.02, 8.0, 0.85, false },
    { kTail, "Tail", 0.0, 1.0, 0.22, false },
    { kPunch, "Punch", 0.0, 1.0, 0.72, false },
    { kClick, "Click", 0.0, 1.0, 0.16, false },
    { kClickTone, "Click Tone", 0.0, 1.0, 0.55, false },
    { kClickDecay, "Click Decay", 0.25, 40.0, 6.5, false },
    { kTexture, "Texture", 0.0, 1.0, 0.04, false },
    { kTextureTone, "Texture Tone", 0.0, 1.0, 0.45, false },
    { kTextureDecay, "Texture Decay", 0.01, 4.0, 0.12, false },
    { kDrive, "Drive", 0.0, 1.0, 0.0, false },
    { kBias, "Bias", -1.0, 1.0, 0.0, false },
    { kCompression, "Compression", 0.0, 1.0, 0.0, false },
    { kRateReduction, "Rate Reduction", 0.0, 1.0, 0.0, false },
    { kBitDepth, "Bit Depth Reduction", 0.0, 1.0, 0.0, false },
    { kReconstruction, "Reconstruction", 0.0, 1.0, 0.0, false },
    { kCharacterTone, "Character Tone", -1.0, 1.0, 0.0, false },
    { kStereoWidth, "Stereo Width", 0.0, 1.0, 0.0, false },
    { kVelocitySensitivity, "Velocity Sensitivity", 0.0, 1.0, 0.90, false },
    { kOutputGain, "Output Gain", -36.0, 12.0, -6.0, false },
    { kMidiReceive, "MIDI Receive", 0.0, 16.0, 0.0, true },
    { kTrigger, "Trigger", 0.0, 1.0, 0.0, true },
}};

struct HostContext {
    clap_host_t host {};
    clap_host_tail_t tail {};
    clap_host_params_t parameterHost {};
    uint32_t processRequests = 0u;
    uint32_t tailChanges = 0u;
    uint32_t invalidTailThreadCalls = 0u;
    uint32_t parameterRescans = 0u;
};

bool gInsideAudioProcess = false;

HostContext* hostContext(const clap_host_t* host)
{
    return static_cast<HostContext*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) {
        return &hostContext(host)->tail;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &hostContext(host)->parameterHost;
    }
    return nullptr;
}

void hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags flags)
{
    if ((flags & CLAP_PARAM_RESCAN_VALUES) != 0u) {
        ++hostContext(host)->parameterRescans;
    }
}

void hostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostParamsRequestFlush(const clap_host_t*) {}

void hostRequestRestart(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) {}

void hostRequestProcess(const clap_host_t* host)
{
    ++hostContext(host)->processRequests;
}

void hostTailChanged(const clap_host_t* host)
{
    auto* context = hostContext(host);
    ++context->tailChanges;
    if (!gInsideAudioProcess) ++context->invalidTailThreadCalls;
}

struct EventList {
    static constexpr uint32_t kParamCapacity = 40u;
    static constexpr uint32_t kNoteCapacity = 8u;
    static constexpr uint32_t kEventCapacity =
        kParamCapacity + kNoteCapacity;

    std::array<clap_event_param_value_t, kParamCapacity> params {};
    std::array<clap_event_note_t, kNoteCapacity> notes {};
    std::array<const clap_event_header_t*, kEventCapacity> events {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t eventCount = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self ? self->eventCount : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return self && index < self->eventCount
                ? self->events[index] : nullptr;
        },
    };

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
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        return insert(&event.header);
    }

    bool addNote(int16_t key, double velocity, uint32_t time = 0u,
        int16_t channel = 0)
    {
        if (noteCount >= notes.size()) return false;
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = key;
        event.port_index = 0;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        return insert(&event.header);
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
    if (requested == 0u) return 0;
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
    if (requested == 0u) return 0;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 5u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset,
            count);
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
        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error) && !error) {
                return iterator->path();
            }
            iterator.increment(error);
        }
    }
#endif
    return {};
}

clap_process_status runBlock(const clap_plugin_t* plugin,
    AudioBlock& audio, const clap_input_events_t* events = nullptr)
{
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = kFrames;
    process.audio_outputs = &audio.buffer;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    gInsideAudioProcess = true;
    const clap_process_status status = plugin->process(plugin, &process);
    gInsideAudioProcess = false;
    return status;
}

bool finiteBlock(const AudioBlock& audio)
{
    for (const auto& channel : audio.storage) {
        for (const float value : channel) {
            if (!std::isfinite(value)) return false;
        }
    }
    return true;
}

double blockEnergy(const AudioBlock& audio)
{
    double result = 0.0;
    for (const auto& channel : audio.storage) {
        for (const float value : channel) {
            result += static_cast<double>(value) * value;
        }
    }
    return result;
}

bool getParam(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, clap_id id, double expected,
    double tolerance = 1.0e-5)
{
    double value = 0.0;
    return params->get_value(plugin, id, &value)
        && std::isfinite(value)
        && std::fabs(value - expected) <= tolerance;
}

bool flushParams(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params,
    std::initializer_list<std::pair<clap_id, double>> values)
{
    EventList events;
    for (const auto& value : values) {
        if (!events.addParam(value.first, value.second)) return false;
    }
    params->flush(plugin, &events.input, nullptr);
    return true;
}

bool parameterContract(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params)
{
    if (!params || params->count(plugin) != kParamSpecs.size()) {
        return false;
    }
    for (uint32_t index = 0u; index < kParamSpecs.size(); ++index) {
        const auto& expected = kParamSpecs[index];
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)
            || info.id != expected.id
            || std::strcmp(info.name, expected.name) != 0
            || std::fabs(info.min_value - expected.minimum) > 1.0e-9
            || std::fabs(info.max_value - expected.maximum) > 1.0e-9
            || std::fabs(info.default_value - expected.defaultValue)
                > 1.0e-9
            || (info.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0u
            || ((info.flags & CLAP_PARAM_IS_STEPPED) != 0u)
                != expected.stepped
            || !getParam(plugin, params, expected.id,
                expected.defaultValue)) {
            std::cerr << "parameter contract failed at index " << index
                      << " (" << expected.name << ")\n";
            return false;
        }
    }

    clap_param_info_t extra {};
    double invalidValue = 0.0;
    double parsed = -1.0;
    char text[32] {};
    char triggerText[32] {};
    char percentText[32] {};
    char receiveText[32] {};
    return !params->get_info(plugin,
            static_cast<uint32_t>(kParamSpecs.size()), &extra)
        && !params->get_value(plugin, 9999u, &invalidValue)
        && params->value_to_text(plugin, kTune, 48.0,
            text, sizeof(text))
        && std::strstr(text, "Hz") != nullptr
        && params->text_to_value(plugin, kTune, text, &parsed)
        && std::fabs(parsed - 48.0) < 1.0e-9
        && params->value_to_text(plugin, kNoteTracking, 1.0,
            percentText, sizeof(percentText))
        && params->text_to_value(plugin, kNoteTracking,
            percentText, &parsed)
        && std::fabs(parsed - 1.0) < 1.0e-9
        && params->value_to_text(plugin, kMidiReceive, 0.0,
            receiveText, sizeof(receiveText))
        && std::strcmp(receiveText, "OMNI") == 0
        && params->value_to_text(plugin, kMidiReceive, 12.0,
            receiveText, sizeof(receiveText))
        && std::strcmp(receiveText, "CH 12") == 0
        && params->text_to_value(plugin, kMidiReceive,
            receiveText, &parsed)
        && parsed == 12.0
        && params->value_to_text(plugin, kTrigger, 1.0,
            triggerText, sizeof(triggerText))
        && std::strcmp(triggerText, "Hit") == 0
        && params->text_to_value(plugin, kTrigger, triggerText, &parsed)
        && parsed == 1.0
        && !params->text_to_value(plugin, kTune,
            "not a number", &parsed);
}

struct StrikeMetrics {
    double energy = 0.0;
    double frequency = 0.0;
    bool finite = true;
};

StrikeMetrics renderTonalStrike(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, AudioBlock& audio,
    int16_t key, double velocity)
{
    StrikeMetrics result;
    const bool configured = flushParams(plugin, params, {
        { kTune, 48.0 },
        { kNoteTracking, 1.0 },
        { kPitchDrop, 0.0 },
        { kSweepTime, 1.0 },
        { kPitchSettle, 1.0 },
        { kBody, 1.0 },
        { kHarmonics, 0.0 },
        { kDecay, 0.85 },
        { kTail, 0.0 },
        { kPunch, 0.0 },
        { kClick, 0.0 },
        { kTexture, 0.0 },
        { kDrive, 0.0 },
        { kBias, 0.0 },
        { kCompression, 0.0 },
        { kRateReduction, 0.0 },
        { kBitDepth, 0.0 },
        { kReconstruction, 0.0 },
        { kCharacterTone, 0.0 },
        { kStereoWidth, 0.0 },
        { kVelocitySensitivity, 1.0 },
        { kOutputGain, -6.0 },
    });
    if (!configured) {
        result.finite = false;
        return result;
    }
    plugin->reset(plugin);

    EventList strike;
    if (!strike.addNote(key, velocity)) {
        result.finite = false;
        return result;
    }

    constexpr uint32_t kBlockCount = 64u;
    constexpr uint32_t kAnalysisStart = 768u;
    constexpr uint32_t kAnalysisEnd = 14000u;
    std::vector<double> crossings;
    crossings.reserve(40u);
    float previous = 0.0f;
    for (uint32_t block = 0u; block < kBlockCount; ++block) {
        audio.clear();
        const auto status = runBlock(plugin, audio,
            block == 0u ? &strike.input : nullptr);
        result.finite = result.finite && status != CLAP_PROCESS_ERROR
            && finiteBlock(audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            const uint32_t absolute = block * kFrames + sample;
            const float current = 0.5f * (audio.storage[0u][sample]
                + audio.storage[1u][sample]);
            if (absolute < 4096u) {
                result.energy += static_cast<double>(current) * current;
            }
            if (absolute >= kAnalysisStart && absolute < kAnalysisEnd
                && previous <= 0.0f && current > 0.0f) {
                const double denominator =
                    static_cast<double>(current) - previous;
                const double fraction = denominator > 0.0
                    ? -static_cast<double>(previous) / denominator : 0.0;
                crossings.push_back(static_cast<double>(absolute) - 1.0
                    + fraction);
            }
            previous = current;
        }
    }

    std::vector<double> periods;
    periods.reserve(crossings.size());
    for (size_t index = 1u; index < crossings.size(); ++index) {
        const double period = crossings[index] - crossings[index - 1u];
        if (period >= kSampleRate / 300.0
            && period <= kSampleRate / 15.0) {
            periods.push_back(period);
        }
    }
    if (!periods.empty()) {
        const auto middle = periods.begin()
            + static_cast<std::ptrdiff_t>(periods.size() / 2u);
        std::nth_element(periods.begin(), middle, periods.end());
        result.frequency = kSampleRate / *middle;
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: s3g_drum_kick_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    if (std::strcmp(argv[2], kPluginId) != 0) {
        std::cerr << "unexpected drum kick plugin id: " << argv[2] << "\n";
        return 1;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve drum kick binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "could not load drum kick: " << dlerror() << "\n";
        return 1;
    }

    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !entry->init(binary.c_str())) {
        std::cerr << "invalid drum kick CLAP entry\n";
        dlclose(library);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const auto* descriptor = factory && factory->get_plugin_count(factory) == 1u
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    if (!descriptor || !descriptor->id || !descriptor->name
        || std::strcmp(descriptor->id, kPluginId) != 0
        || std::strstr(descriptor->name, "Drum Kick") == nullptr) {
        std::cerr << "unexpected drum kick factory or descriptor\n";
        entry->deinit();
        dlclose(library);
        return 1;
    }

    HostContext context;
    context.tail.changed = hostTailChanged;
    context.parameterHost.rescan = hostParamsRescan;
    context.parameterHost.clear = hostParamsClear;
    context.parameterHost.request_flush = hostParamsRequestFlush;
    context.host.clap_version = CLAP_VERSION_INIT;
    context.host.host_data = &context;
    context.host.name = "s3g drum kick smoke";
    context.host.vendor = "s3g";
    context.host.url = "https://github.com/s3g/s3g-dsp";
    context.host.version = "1";
    context.host.get_extension = hostGetExtension;
    context.host.request_restart = hostRequestRestart;
    context.host.request_process = hostRequestProcess;
    context.host.request_callback = hostRequestCallback;

    const clap_plugin_t* plugin = factory->create_plugin(
        factory, &context.host, kPluginId);
    if (!plugin || !plugin->init(plugin)) {
        std::cerr << "could not create drum kick plugin\n";
        if (plugin) plugin->destroy(plugin);
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

    clap_audio_port_info_t outputInfo {};
    clap_note_port_info_t noteInfo {};
    bool ok = audioPorts && notePorts && params && state && tail
        && audioPorts->count(plugin, true) == 0u
        && audioPorts->count(plugin, false) == 1u
        && audioPorts->get(plugin, 0u, false, &outputInfo)
        && outputInfo.channel_count == kChannels
        && outputInfo.port_type
        && std::strcmp(outputInfo.port_type, CLAP_PORT_STEREO) == 0
        && (outputInfo.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0u
        && notePorts->count(plugin, true) == 1u
        && notePorts->count(plugin, false) == 0u
        && notePorts->get(plugin, 0u, true, &noteInfo)
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
        && (noteInfo.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && parameterContract(plugin, params);
    if (!ok) {
        std::cerr << "drum kick CLAP port/parameter contract failed\n";
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }

    bool activated = plugin->activate(plugin, kSampleRate, 1u, kFrames);
    bool processing = activated && plugin->start_processing(plugin);
    if (!processing) {
        std::cerr << "could not activate drum kick plugin\n";
        if (activated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }

    AudioBlock audio;

    // An untriggered instrument must remain exactly silent.
    audio.clear();
    const auto silenceStatus = runBlock(plugin, audio);
    ok = silenceStatus != CLAP_PROCESS_ERROR && finiteBlock(audio)
        && blockEnergy(audio) == 0.0;
    if (!ok) std::cerr << "untriggered silence contract failed\n";

    // MIDI RECEIVE uses OMNI=0 and user-facing channels 1-16.
    const bool receiveSet = flushParams(
        plugin, params, { { kMidiReceive, 2.0 } });
    plugin->reset(plugin);
    EventList rejectedNote;
    const bool rejectedNoteAdded = rejectedNote.addNote(36, 1.0, 0u, 0);
    audio.clear();
    const auto rejectedNoteStatus = runBlock(
        plugin, audio, &rejectedNote.input);
    const bool rejectedNoteSilent = rejectedNoteStatus != CLAP_PROCESS_ERROR
        && finiteBlock(audio) && blockEnergy(audio) == 0.0;

    plugin->reset(plugin);
    EventList acceptedNote;
    const bool acceptedNoteAdded = acceptedNote.addNote(36, 1.0, 0u, 1);
    audio.clear();
    const auto acceptedNoteStatus = runBlock(
        plugin, audio, &acceptedNote.input);
    const bool acceptedNoteAudible = acceptedNoteStatus != CLAP_PROCESS_ERROR
        && finiteBlock(audio) && blockEnergy(audio) > 1.0e-8;
    const bool receiveRestored = flushParams(
        plugin, params, { { kMidiReceive, 0.0 } });
    const bool receiveRouting = receiveSet && rejectedNoteAdded
        && rejectedNoteSilent && acceptedNoteAdded && acceptedNoteAudible
        && receiveRestored;
    if (!receiveRouting) std::cerr << "MIDI receive routing failed\n";
    ok = ok && receiveRouting;

    // Note events must begin at their exact sample offset.
    plugin->reset(plugin);
    EventList delayedStrike;
    constexpr uint32_t kStrikeTime = 73u;
    ok = ok && delayedStrike.addNote(36, 1.0, kStrikeTime);
    audio.clear();
    const auto delayedStatus = runBlock(plugin, audio, &delayedStrike.input);
    double before = 0.0;
    double after = 0.0;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            (sample < kStrikeTime ? before : after)
                += std::fabs(audio.storage[channel][sample]);
        }
    }
    const bool sampleAccurate = delayedStatus != CLAP_PROCESS_ERROR
        && finiteBlock(audio) && before == 0.0 && after > 1.0e-6;
    if (!sampleAccurate) {
        std::cerr << "sample-accurate note start failed: "
                  << before << " / " << after << "\n";
    }
    ok = ok && sampleAccurate;

    // Width zero is an exact dual-mono contract, including attack/texture.
    ok = ok && flushParams(plugin, params, {
        { kBody, 0.8 }, { kClick, 0.8 }, { kTexture, 0.8 },
        { kStereoWidth, 0.0 }, { kDrive, 0.0 },
        { kRateReduction, 0.0 }, { kBitDepth, 0.0 },
        { kReconstruction, 0.0 }, { kCharacterTone, 0.0 },
    });
    plugin->reset(plugin);
    EventList monoStrike;
    ok = ok && monoStrike.addNote(36, 0.9);
    bool exactMono = true;
    double monoEnergy = 0.0;
    for (uint32_t block = 0u; block < 12u; ++block) {
        audio.clear();
        const auto status = runBlock(plugin, audio,
            block == 0u ? &monoStrike.input : nullptr);
        exactMono = exactMono && status != CLAP_PROCESS_ERROR
            && finiteBlock(audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            exactMono = exactMono
                && audio.storage[0u][sample] == audio.storage[1u][sample];
            monoEnergy += static_cast<double>(audio.storage[0u][sample])
                * audio.storage[0u][sample];
        }
    }
    exactMono = exactMono && monoEnergy > 1.0e-9;
    if (!exactMono) std::cerr << "Width=0 was not exact dual mono\n";
    ok = ok && exactMono;

    // Full width must create upper-layer side energy without destabilizing.
    ok = ok && flushParams(plugin, params, {
        { kClick, 1.0 }, { kClickTone, 0.8 },
        { kTexture, 1.0 }, { kTextureTone, 0.75 },
        { kStereoWidth, 1.0 },
    });
    plugin->reset(plugin);
    EventList wideStrike;
    ok = ok && wideStrike.addNote(36, 0.9);
    double wideEnergy = 0.0;
    double sideEnergy = 0.0;
    for (uint32_t block = 0u; block < 12u; ++block) {
        audio.clear();
        const auto status = runBlock(plugin, audio,
            block == 0u ? &wideStrike.input : nullptr);
        ok = ok && status != CLAP_PROCESS_ERROR && finiteBlock(audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            const double left = audio.storage[0u][sample];
            const double right = audio.storage[1u][sample];
            wideEnergy += left * left + right * right;
            const double side = left - right;
            sideEnergy += side * side;
        }
    }
    const bool stereoResponse = wideEnergy > 1.0e-9
        && sideEnergy > wideEnergy * 1.0e-8;
    if (!stereoResponse) {
        std::cerr << "Width=1 produced no side energy: "
                  << sideEnergy << " / " << wideEnergy << "\n";
    }
    ok = ok && stereoResponse;

    const auto lowNote = renderTonalStrike(plugin, params, audio, 36, 1.0);
    const auto highNote = renderTonalStrike(plugin, params, audio, 48, 1.0);
    const auto softNote = renderTonalStrike(plugin, params, audio, 36, 0.2);
    const double octaveRatio = lowNote.frequency > 0.0
        ? highNote.frequency / lowNote.frequency : 0.0;
    const bool midiResponse = lowNote.finite && highNote.finite
        && softNote.finite
        && lowNote.frequency >= 38.0 && lowNote.frequency <= 60.0
        && octaveRatio >= 1.85 && octaveRatio <= 2.15
        && lowNote.energy > softNote.energy * 4.0;
    if (!midiResponse) {
        std::cerr << "MIDI pitch/velocity response failed: "
                  << lowNote.frequency << " Hz / "
                  << highNote.frequency << " Hz; energy "
                  << softNote.energy << " / " << lowNote.energy << "\n";
    }
    ok = ok && midiResponse;

    // Current state saves include MIDI RECEIVE; version-1 snapshots migrate
    // with that newly appended parameter defaulting to OMNI.
    ok = ok && flushParams(plugin, params, {
        { kTune, 73.25 }, { kDecay, 1.25 }, { kBias, -0.41 },
        { kRateReduction, 0.35 }, { kStereoWidth, 0.73 },
        { kMidiReceive, 5.0 },
    });
    MemoryState memory;
    clap_ostream_t ostream { &memory, stateWrite };
    const bool saved = state->save(plugin, &ostream) && !memory.bytes.empty();
    ok = ok && saved && flushParams(plugin, params, {
        { kTune, 31.0 }, { kDecay, 0.1 }, { kBias, 0.2 },
        { kRateReduction, 0.0 }, { kStereoWidth, 0.0 },
        { kMidiReceive, 2.0 },
    });
    memory.offset = 0u;
    clap_istream_t istream { &memory, stateRead };
    const uint32_t tailChangesBeforeLoad = context.tailChanges;
    const uint32_t rescansBeforeLoad = context.parameterRescans;
    const bool restored = saved && state->load(plugin, &istream)
        && getParam(plugin, params, kTune, 73.25)
        && getParam(plugin, params, kDecay, 1.25)
        && getParam(plugin, params, kBias, -0.41)
        && getParam(plugin, params, kRateReduction, 0.35)
        && getParam(plugin, params, kStereoWidth, 0.73)
        && getParam(plugin, params, kMidiReceive, 5.0)
        && getParam(plugin, params, kTrigger, 0.0)
        && context.parameterRescans > rescansBeforeLoad;
    if (!restored) std::cerr << "current state round trip failed\n";
    const bool tailDeferred = context.tailChanges == tailChangesBeforeLoad;
    audio.clear();
    const auto stateTailStatus = runBlock(plugin, audio);
    const bool tailDeliveredOnAudioThread =
        stateTailStatus != CLAP_PROCESS_ERROR
        && context.tailChanges > tailChangesBeforeLoad
        && context.invalidTailThreadCalls == 0u;
    if (!tailDeferred || !tailDeliveredOnAudioThread) {
        std::cerr << "tail change was not deferred to the audio thread\n";
    }
    ok = ok && restored && tailDeferred && tailDeliveredOnAudioThread;

    MemoryState legacyState = memory;
    std::array<uint32_t, 4u> legacyHeader {};
    if (legacyState.bytes.size() >= sizeof(legacyHeader)) {
        std::memcpy(legacyHeader.data(), legacyState.bytes.data(),
            sizeof(legacyHeader));
        legacyHeader[1u] = 1u;
        legacyHeader[2u] = 26u;
        std::memcpy(legacyState.bytes.data(), legacyHeader.data(),
            sizeof(legacyHeader));
        legacyState.bytes.resize(
            sizeof(legacyHeader) + 26u * sizeof(double));
    }
    ok = ok && flushParams(plugin, params, { { kMidiReceive, 9.0 } });
    legacyState.offset = 0u;
    clap_istream_t legacyIstream { &legacyState, stateRead };
    const bool legacyLoaded = state->load(plugin, &legacyIstream)
        && getParam(plugin, params, kMidiReceive, 0.0);
    if (!legacyLoaded) {
        std::cerr << "legacy state did not default MIDI receive to OMNI\n";
    }
    ok = ok && legacyLoaded;

    // A non-audio-thread trigger requests processing, then fires in process().
    plugin->reset(plugin);
    const uint32_t requestsBeforeTrigger = context.processRequests;
    EventList triggerEvent;
    ok = ok && triggerEvent.addParam(kTrigger, 1.0);
    params->flush(plugin, &triggerEvent.input, nullptr);
    const bool processRequested = context.processRequests
        > requestsBeforeTrigger;
    audio.clear();
    const auto triggerStatus = runBlock(plugin, audio);
    const bool parameterTriggered = processRequested
        && triggerStatus != CLAP_PROCESS_ERROR && finiteBlock(audio)
        && blockEnergy(audio) > 1.0e-9;
    if (!parameterTriggered) {
        std::cerr << "Trigger did not request/process a strike\n";
    }
    ok = ok && parameterTriggered;

    // The independent texture envelope is part of the advertised lifetime.
    ok = ok && flushParams(plugin, params, {
        { kDecay, 0.02 }, { kTail, 0.0 }, { kClickDecay, 0.25 },
        { kTexture, 1.0 }, { kTextureDecay, 4.0 },
    });
    const uint32_t longTextureTail = tail->get(plugin);
    const bool textureTailCovered = longTextureTail
            >= static_cast<uint32_t>(kSampleRate * 6.9)
        && longTextureTail <= static_cast<uint32_t>(kSampleRate * 7.1);
    if (!textureTailCovered) {
        std::cerr << "texture lifetime was absent from CLAP tail: "
                  << longTextureTail << "\n";
    }
    ok = ok && textureTailCovered;

    // At minimum decay, the reported tail is finite and processing sleeps no
    // later than a small allowance beyond that advertised tail.
    EventList triggerRelease;
    ok = ok && triggerRelease.addParam(kTrigger, 0.0);
    params->flush(plugin, &triggerRelease.input, nullptr);
    ok = ok && flushParams(plugin, params, {
        { kPitchDrop, 0.0 }, { kBody, 1.0 }, { kHarmonics, 0.0 },
        { kDecay, 0.02 }, { kTail, 0.0 }, { kClick, 0.0 },
        { kClickDecay, 0.25 }, { kTexture, 0.0 },
        { kTextureDecay, 0.01 }, { kDrive, 0.0 },
        { kCompression, 0.0 }, { kRateReduction, 0.0 },
        { kBitDepth, 0.0 }, { kReconstruction, 0.0 },
        { kStereoWidth, 0.0 },
    });
    plugin->reset(plugin);
    const uint32_t tailSamples = tail->get(plugin);
    const bool finiteTail = tailSamples > 0u
        && tailSamples < static_cast<uint32_t>(
            std::numeric_limits<int32_t>::max())
        && tailSamples < static_cast<uint32_t>(kSampleRate * 4.0);
    EventList shortStrike;
    ok = ok && shortStrike.addNote(36, 1.0);
    const uint32_t tailBlocks = finiteTail
        ? (tailSamples + kFrames - 1u) / kFrames + 4u : 4u;
    bool slept = false;
    bool tailOutputFinite = true;
    double shortEnergy = 0.0;
    for (uint32_t block = 0u; block < tailBlocks; ++block) {
        audio.clear();
        const auto status = runBlock(plugin, audio,
            block == 0u ? &shortStrike.input : nullptr);
        tailOutputFinite = tailOutputFinite
            && status != CLAP_PROCESS_ERROR && finiteBlock(audio);
        shortEnergy += blockEnergy(audio);
        if (status == CLAP_PROCESS_SLEEP) {
            slept = true;
            break;
        }
    }
    const bool tailContract = finiteTail && tailOutputFinite
        && shortEnergy > 1.0e-9 && slept
        && context.invalidTailThreadCalls == 0u;
    if (!tailContract) {
        std::cerr << "finite output/tail contract failed: "
                  << tailSamples << " samples, slept=" << slept << "\n";
    }
    ok = ok && tailContract;

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);

    if (!ok) {
        std::cerr << "drum kick CLAP smoke failed\n";
        return 1;
    }
    std::cout << "drum kick CLAP smoke passed\n";
    return 0;
}
