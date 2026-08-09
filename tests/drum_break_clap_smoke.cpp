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

constexpr const char* kPluginId = "org.s3g.s3g-dsp.drum-break";
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 2u;

enum ParamId : clap_id {
    kLowTune = 1u,
    kNoteTracking,
    kLowDrop,
    kLowDecay,
    kLowWeight,
    kMidTune,
    kMidBody,
    kMidCrack,
    kMidDecay,
    kHighTone,
    kHighTexture,
    kHighDecay,
    kTransient,
    kBleed,
    kRoom,
    kAge,
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
    kTomTune,
    kTomDecay,
    kKickLevel,
    kKickBand,
    kSnareLevel,
    kSnareBand,
    kTomLevel,
    kTomBand,
    kHiHatLevel,
    kHiHatBand,
};

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamSpec, 38u> kParamSpecs {{
    { kLowTune, "Kick Tune", 28.0, 96.0, 52.0, false },
    { kNoteTracking, "Note Tracking", 0.0, 1.0, 0.20, false },
    { kLowDrop, "Kick Drop", 0.0, 42.0, 14.0, false },
    { kLowDecay, "Kick Decay", 0.05, 2.0, 0.42, false },
    { kLowWeight, "Kick Weight", 0.0, 1.0, 0.78, false },
    { kMidTune, "Snare Tune", 90.0, 380.0, 175.0, false },
    { kMidBody, "Snare Body", 0.0, 1.0, 0.56, false },
    { kMidCrack, "Snare Wire", 0.0, 1.0, 0.72, false },
    { kMidDecay, "Snare Decay", 0.04, 1.8, 0.32, false },
    { kHighTone, "Hat Tone", 0.0, 1.0, 0.56, false },
    { kHighTexture, "Hat Texture", 0.0, 1.0, 0.66, false },
    { kHighDecay, "Hat Decay", 0.018, 1.2, 0.12, false },
    { kTransient, "Transient", 0.0, 1.0, 0.62, false },
    { kBleed, "Bleed", 0.0, 1.0, 0.20, false },
    { kRoom, "Room", 0.0, 1.0, 0.28, false },
    { kAge, "Age", 0.0, 1.0, 0.25, false },
    { kDrive, "Drive", 0.0, 1.0, 0.0, false },
    { kBias, "Bias", -1.0, 1.0, 0.0, false },
    { kCompression, "Compression", 0.0, 1.0, 0.0, false },
    { kRateReduction, "Rate Reduction", 0.0, 1.0, 0.0, false },
    { kBitDepth, "Bit Depth Reduction", 0.0, 1.0, 0.0, false },
    { kReconstruction, "Reconstruction", 0.0, 1.0, 0.0, false },
    { kCharacterTone, "Character Tone", -1.0, 1.0, 0.0, false },
    { kStereoWidth, "Stereo Width", 0.0, 1.0, 0.38, false },
    { kVelocitySensitivity, "Velocity Sensitivity", 0.0, 1.0, 0.90, false },
    { kOutputGain, "Output Gain", -36.0, 12.0, -8.0, false },
    { kMidiReceive, "MIDI Receive", 0.0, 16.0, 0.0, true },
    { kTomTune, "Tom Tune", 58.0, 260.0, 118.0, false },
    { kTomDecay, "Tom Decay", 0.08, 2.4, 0.52, false },
    { kKickLevel, "Kick Level", -24.0, 12.0, 0.0, false },
    { kKickBand, "Kick Band", 55.0, 900.0, 140.0, false },
    { kSnareLevel, "Snare Level", -24.0, 12.0, 0.0, false },
    { kSnareBand, "Snare Band", 300.0, 6000.0, 1800.0, false },
    { kTomLevel, "Tom Level", -24.0, 12.0, 0.0, false },
    { kTomBand, "Tom Band", 80.0, 1800.0, 320.0, false },
    { kHiHatLevel, "Hi-Hat Level", -24.0, 12.0, 0.0, false },
    { kHiHatBand, "Hi-Hat Band", 2200.0, 14000.0, 8200.0, false },
    { kTrigger, "Trigger", 0.0, 4.0, 0.0, true },
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
        && params->value_to_text(plugin, kLowTune, 48.0,
            text, sizeof(text))
        && std::strstr(text, "Hz") != nullptr
        && params->text_to_value(plugin, kLowTune, text, &parsed)
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
        && std::strcmp(triggerText, "Kick") == 0
        && params->text_to_value(plugin, kTrigger, triggerText, &parsed)
        && parsed == 1.0
        && !params->text_to_value(plugin, kLowTune,
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
        { kLowTune, 52.0 },
        { kNoteTracking, 1.0 },
        { kLowDrop, 0.0 },
        { kLowDecay, 0.85 },
        { kLowWeight, 1.0 },
        { kMidTune, 175.0 },
        { kMidBody, 0.0 },
        { kMidCrack, 0.0 },
        { kMidDecay, 0.04 },
        { kHighTone, 0.0 },
        { kHighTexture, 0.0 },
        { kHighDecay, 0.018 },
        { kTomTune, 118.0 },
        { kTomDecay, 0.08 },
        { kKickLevel, 0.0 }, { kKickBand, 140.0 },
        { kSnareLevel, 0.0 }, { kSnareBand, 1800.0 },
        { kTomLevel, 0.0 }, { kTomBand, 320.0 },
        { kHiHatLevel, 0.0 }, { kHiHatBand, 8200.0 },
        { kTransient, 0.0 },
        { kBleed, 0.0 },
        { kRoom, 0.0 },
        { kAge, 0.0 },
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
        std::cerr << "usage: s3g_drum_break_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    if (std::strcmp(argv[2], kPluginId) != 0) {
        std::cerr << "unexpected drum break plugin id: " << argv[2] << "\n";
        return 1;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve drum break binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "could not load drum break: " << dlerror() << "\n";
        return 1;
    }

    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !entry->init(binary.c_str())) {
        std::cerr << "invalid drum break CLAP entry\n";
        dlclose(library);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const auto* descriptor = factory && factory->get_plugin_count(factory) == 1u
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    if (!descriptor || !descriptor->id || !descriptor->name
        || std::strcmp(descriptor->id, kPluginId) != 0
        || std::strstr(descriptor->name, "Drum Break") == nullptr) {
        std::cerr << "unexpected drum break factory or descriptor\n";
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
    context.host.name = "s3g drum break smoke";
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
        std::cerr << "could not create drum break plugin\n";
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
        std::cerr << "drum break CLAP port/parameter contract failed\n";
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }

    bool activated = plugin->activate(plugin, kSampleRate, 1u, kFrames);
    bool processing = activated && plugin->start_processing(plugin);
    if (!processing) {
        std::cerr << "could not activate drum break plugin\n";
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
        { kMidTune, 180.0 }, { kHighTexture, 0.8 }, { kBleed, 0.8 },
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
        { kHighTexture, 1.0 }, { kHighDecay, 0.8 },
        { kBleed, 1.0 }, { kRoom, 0.75 },
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

    // The four canonical notes must reach distinct synthesis families.
    const auto renderCanonical = [&](int16_t note, AudioBlock& result) {
        plugin->reset(plugin);
        EventList strike;
        if (!strike.addNote(note, 1.0)) return false;
        result.clear();
        const auto status = runBlock(plugin, result, &strike.input);
        return status != CLAP_PROCESS_ERROR && finiteBlock(result)
            && blockEnergy(result) > 1.0e-9;
    };
    std::array<AudioBlock, 4u> familyBlocks {};
    constexpr std::array<int16_t, 4u> familyNotes {{36, 38, 45, 42}};
    bool canonicalRendered = true;
    for (uint32_t family = 0u; family < familyBlocks.size(); ++family) {
        canonicalRendered = canonicalRendered
            && renderCanonical(familyNotes[family], familyBlocks[family]);
    }
    bool distinctFamilies = true;
    for (uint32_t family = 1u; family < familyBlocks.size(); ++family) {
        double difference = 0.0;
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            for (uint32_t sample = 0u; sample < kFrames; ++sample) {
                const double delta = familyBlocks[family - 1u]
                        .storage[channel][sample]
                    - familyBlocks[family].storage[channel][sample];
                difference += delta * delta;
            }
        }
        distinctFamilies = distinctFamilies && difference > 1.0e-9;
    }
    const bool articulationRouting = canonicalRendered && distinctFamilies;
    if (!articulationRouting) {
        std::cerr << "kick/snare/tom/hi-hat routing failed\n";
    }
    ok = ok && articulationRouting;

    // Notes 24 and 36 both select kick and span one tracked octave.
    const auto lowNote = renderTonalStrike(plugin, params, audio, 24, 1.0);
    const auto highNote = renderTonalStrike(plugin, params, audio, 36, 1.0);
    const auto softNote = renderTonalStrike(plugin, params, audio, 24, 0.2);
    const double octaveRatio = lowNote.frequency > 0.0
        ? highNote.frequency / lowNote.frequency : 0.0;
    const bool midiResponse = lowNote.finite && highNote.finite
        && softNote.finite
        && lowNote.frequency >= 20.0 && lowNote.frequency <= 34.0
        && octaveRatio >= 1.85 && octaveRatio <= 2.15
        && lowNote.energy > softNote.energy * 4.0;
    if (!midiResponse) {
        std::cerr << "MIDI pitch/velocity response failed: "
                  << lowNote.frequency << " Hz / "
                  << highNote.frequency << " Hz; energy "
                  << softNote.energy << " / " << lowNote.energy << "\n";
    }
    ok = ok && midiResponse;

    // State includes every persistent control and excludes Trigger.
    ok = ok && flushParams(plugin, params, {
        { kLowTune, 73.25 }, { kLowDecay, 1.25 }, { kBias, -0.41 },
        { kRateReduction, 0.35 }, { kStereoWidth, 0.73 },
        { kTomTune, 146.5 }, { kTomDecay, 1.17 },
        { kKickLevel, -3.5 }, { kKickBand, 245.0 },
        { kSnareLevel, 1.25 }, { kSnareBand, 3100.0 },
        { kTomLevel, -2.0 }, { kTomBand, 470.0 },
        { kHiHatLevel, -1.5 }, { kHiHatBand, 9600.0 },
        { kMidiReceive, 5.0 },
    });
    MemoryState memory;
    clap_ostream_t ostream { &memory, stateWrite };
    const bool saved = state->save(plugin, &ostream) && !memory.bytes.empty();
    ok = ok && saved && flushParams(plugin, params, {
        { kLowTune, 31.0 }, { kLowDecay, 0.1 }, { kBias, 0.2 },
        { kRateReduction, 0.0 }, { kStereoWidth, 0.0 },
        { kTomTune, 80.0 }, { kTomDecay, 0.2 },
        { kKickLevel, 0.0 }, { kKickBand, 60.0 },
        { kSnareLevel, 0.0 }, { kSnareBand, 400.0 },
        { kTomLevel, 0.0 }, { kTomBand, 100.0 },
        { kHiHatLevel, 0.0 }, { kHiHatBand, 2500.0 },
        { kMidiReceive, 2.0 },
    });
    memory.offset = 0u;
    clap_istream_t istream { &memory, stateRead };
    const uint32_t tailChangesBeforeLoad = context.tailChanges;
    const uint32_t rescansBeforeLoad = context.parameterRescans;
    const bool restored = saved && state->load(plugin, &istream)
        && getParam(plugin, params, kLowTune, 73.25)
        && getParam(plugin, params, kLowDecay, 1.25)
        && getParam(plugin, params, kBias, -0.41)
        && getParam(plugin, params, kRateReduction, 0.35)
        && getParam(plugin, params, kStereoWidth, 0.73)
        && getParam(plugin, params, kTomTune, 146.5)
        && getParam(plugin, params, kTomDecay, 1.17)
        && getParam(plugin, params, kKickLevel, -3.5)
        && getParam(plugin, params, kKickBand, 245.0)
        && getParam(plugin, params, kSnareLevel, 1.25)
        && getParam(plugin, params, kSnareBand, 3100.0)
        && getParam(plugin, params, kTomLevel, -2.0)
        && getParam(plugin, params, kTomBand, 470.0)
        && getParam(plugin, params, kHiHatLevel, -1.5)
        && getParam(plugin, params, kHiHatBand, 9600.0)
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

    // Version-1 three-band states retain all original controls and receive
    // safe defaults for every later voice-isolation parameter.
    struct LegacyHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t valueCount;
        uint32_t reserved;
    };
    constexpr LegacyHeader legacyHeader {
        0x42473353u, 1u, 27u, 0u
    };
    std::array<double, 27u> legacyValues {};
    for (uint32_t index = 0u; index < legacyValues.size(); ++index) {
        legacyValues[index] = kParamSpecs[index].defaultValue;
    }
    legacyValues[0u] = 68.5;
    legacyValues[25u] = -11.0;
    legacyValues[26u] = 7.0;
    MemoryState legacyMemory;
    const auto appendLegacyBytes = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        legacyMemory.bytes.insert(legacyMemory.bytes.end(), bytes,
            bytes + size);
    };
    appendLegacyBytes(&legacyHeader, sizeof(legacyHeader));
    appendLegacyBytes(legacyValues.data(),
        legacyValues.size() * sizeof(double));
    clap_istream_t legacyStream { &legacyMemory, stateRead };
    const bool legacyRestored = state->load(plugin, &legacyStream)
        && getParam(plugin, params, kLowTune, 68.5)
        && getParam(plugin, params, kOutputGain, -11.0)
        && getParam(plugin, params, kMidiReceive, 7.0)
        && getParam(plugin, params, kTomTune, 118.0)
        && getParam(plugin, params, kTomDecay, 0.52)
        && getParam(plugin, params, kKickLevel, 0.0)
        && getParam(plugin, params, kKickBand, 140.0)
        && getParam(plugin, params, kSnareLevel, 0.0)
        && getParam(plugin, params, kSnareBand, 1800.0)
        && getParam(plugin, params, kTomLevel, 0.0)
        && getParam(plugin, params, kTomBand, 320.0)
        && getParam(plugin, params, kHiHatLevel, 0.0)
        && getParam(plugin, params, kHiHatBand, 8200.0);
    if (!legacyRestored) {
        std::cerr << "version-1 break state compatibility failed\n";
    }
    ok = ok && legacyRestored;
    audio.clear();
    const auto legacyConsumeStatus = runBlock(plugin, audio);
    ok = ok && legacyConsumeStatus != CLAP_PROCESS_ERROR
        && finiteBlock(audio);

    // Version-2 cymbal slots are intentionally discarded instead of being
    // reinterpreted as the new kick level and band controls.
    constexpr LegacyHeader versionTwoHeader {
        0x42473353u, 2u, 31u, 0u
    };
    std::array<double, 31u> versionTwoValues {};
    for (uint32_t index = 0u; index < 27u; ++index) {
        versionTwoValues[index] = kParamSpecs[index].defaultValue;
    }
    versionTwoValues[0u] = 66.0;
    versionTwoValues[27u] = 155.0;
    versionTwoValues[28u] = 0.90;
    versionTwoValues[29u] = 0.91; // retired cymbal tone
    versionTwoValues[30u] = 6.50; // retired cymbal decay
    MemoryState versionTwoMemory;
    const auto appendVersionTwoBytes = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        versionTwoMemory.bytes.insert(versionTwoMemory.bytes.end(), bytes,
            bytes + size);
    };
    appendVersionTwoBytes(&versionTwoHeader, sizeof(versionTwoHeader));
    appendVersionTwoBytes(versionTwoValues.data(),
        versionTwoValues.size() * sizeof(double));
    clap_istream_t versionTwoStream { &versionTwoMemory, stateRead };
    const bool versionTwoRestored = state->load(plugin, &versionTwoStream)
        && getParam(plugin, params, kLowTune, 66.0)
        && getParam(plugin, params, kTomTune, 155.0)
        && getParam(plugin, params, kTomDecay, 0.90)
        && getParam(plugin, params, kKickLevel, 0.0)
        && getParam(plugin, params, kKickBand, 140.0)
        && getParam(plugin, params, kSnareLevel, 0.0)
        && getParam(plugin, params, kTomLevel, 0.0)
        && getParam(plugin, params, kHiHatLevel, 0.0);
    if (!versionTwoRestored) {
        std::cerr << "version-2 break state migration failed\n";
    }
    ok = ok && versionTwoRestored;
    audio.clear();
    const auto versionTwoConsumeStatus = runBlock(plugin, audio);
    ok = ok && versionTwoConsumeStatus != CLAP_PROCESS_ERROR
        && finiteBlock(audio);

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

    // The maximum kick decay is part of the advertised lifetime.
    ok = ok && flushParams(plugin, params, {
        { kLowDecay, 2.0 }, { kMidDecay, 0.04 },
        { kHighDecay, 0.018 }, { kTomDecay, 0.08 },
        { kRoom, 0.0 }, { kDrive, 0.0 },
    });
    plugin->reset(plugin);
    const uint32_t longLowTail = tail->get(plugin);
    const bool lowTailCovered = longLowTail
            >= static_cast<uint32_t>(kSampleRate * 4.35)
        && longLowTail <= static_cast<uint32_t>(kSampleRate * 4.45);
    if (!lowTailCovered) {
        std::cerr << "low lifetime was absent from CLAP tail: "
                  << longLowTail << "\n";
    }
    ok = ok && lowTailCovered;

    // At minimum decay, the reported tail is finite and processing sleeps no
    // later than a small allowance beyond that advertised tail.
    EventList triggerRelease;
    ok = ok && triggerRelease.addParam(kTrigger, 0.0);
    params->flush(plugin, &triggerRelease.input, nullptr);
    ok = ok && flushParams(plugin, params, {
        { kLowDrop, 0.0 }, { kLowDecay, 0.05 },
        { kMidTune, 175.0 }, { kMidBody, 0.0 }, { kMidCrack, 0.0 },
        { kMidDecay, 0.04 }, { kHighTexture, 0.0 },
        { kHighDecay, 0.018 }, { kTransient, 0.0 }, { kBleed, 0.0 },
        { kTomDecay, 0.08 },
        { kRoom, 0.0 }, { kAge, 0.0 }, { kDrive, 0.0 },
        { kCompression, 0.0 }, { kRateReduction, 0.0 },
        { kBitDepth, 0.0 }, { kReconstruction, 0.0 },
        { kStereoWidth, 0.0 }, { kMidiReceive, 0.0 },
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
        std::cerr << "drum break CLAP smoke failed\n";
        return 1;
    }
    std::cout << "drum break CLAP smoke passed\n";
    return 0;
}
