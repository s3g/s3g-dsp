#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr const char* kPluginId = "org.s3g.s3g-dsp.drum-toms";
constexpr const char* kPluginName = "s3g Drum Toms";
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 2u;
constexpr int16_t kBaseMidiNote = 45;

enum ParamId : clap_id {
    kLowTune = 1u,
    kNoteTracking,
    kMidTune,
    kHighTune,
    kPitchDrop,
    kSweepTime,
    kShellSpread,
    kBody,
    kRing,
    kBodyDecay,
    kDecaySpread,
    kPunch,
    kRimLevel,
    kRimCharacter,
    kRimDecay,
    kStickTone,
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
};

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    double textValue;
    bool stepped;
};

constexpr std::array<ParamSpec, 27u> kParamSpecs {{
    { kLowTune, "Low Tune", 40.0, 180.0, 82.0, 96.5, false },
    { kNoteTracking, "Note Tracking", 0.0, 1.0, 0.75, 0.37, false },
    { kMidTune, "Mid Tune", 55.0, 280.0, 127.0, 134.5, false },
    { kHighTune, "High Tune", 70.0, 420.0, 182.0, 205.5, false },
    { kPitchDrop, "Pitch Drop", -6.0, 30.0, 7.0, 12.5, false },
    { kSweepTime, "Sweep Time", 1.0, 250.0, 32.0, 42.5, false },
    { kShellSpread, "Shell Spread", 0.0, 1.0, 0.40, 0.41, false },
    { kBody, "Body", 0.0, 1.0, 0.62, 0.63, false },
    { kRing, "Ring", 0.0, 1.0, 0.38, 0.29, false },
    { kBodyDecay, "Body Decay", 0.03, 3.0, 0.65, 0.55, false },
    { kDecaySpread, "Decay Spread", -1.0, 1.0, 0.25, -0.41, false },
    { kPunch, "Punch", 0.0, 1.0, 0.74, 0.71, false },
    { kRimLevel, "Rim Level", 0.0, 1.0, 0.52, 0.68, false },
    { kRimCharacter, "Rim Character", 0.0, 1.0, 0.52, 0.46, false },
    { kRimDecay, "Rim Decay", 0.02, 0.30, 0.065, 0.065, false },
    { kStickTone, "Stick Tone", 0.0, 1.0, 0.62, 0.62, false },
    { kDrive, "Drive", 0.0, 1.0, 0.0, 0.23, false },
    { kBias, "Bias", -1.0, 1.0, 0.0, -0.41, false },
    { kCompression, "Compression", 0.0, 1.0, 0.0, 0.27, false },
    { kRateReduction, "Rate Reduction", 0.0, 1.0, 0.0, 0.34, false },
    { kBitDepth, "Bit Depth Reduction", 0.0, 1.0, 0.0, 0.43, false },
    { kReconstruction, "Reconstruction", 0.0, 1.0, 0.0, 0.52, false },
    { kCharacterTone, "Character Tone", -1.0, 1.0, 0.0, 0.31, false },
    { kStereoWidth, "Stereo Width", 0.0, 1.0, 0.0, 0.67, false },
    { kVelocitySensitivity, "Velocity Sensitivity", 0.0, 1.0, 0.90, 0.83, false },
    { kOutputGain, "Output Gain", -36.0, 12.0, -6.0, -4.5, false },
    { kTrigger, "Trigger", 0.0, 6.0, 0.0, 6.0, true },
}};

struct HostContext {
    clap_host_t host {};
    clap_host_tail_t tail {};
    clap_host_params_t parameterHost {};
    std::atomic<uint32_t> processRequests { 0u };
    std::atomic<uint32_t> tailChanges { 0u };
    std::atomic<uint32_t> nonAudioTailCalls { 0u };
    std::atomic<uint32_t> parameterRescans { 0u };
    std::atomic<uint32_t> parameterFlushRequests { 0u };
    std::atomic<uint32_t> callbackRequests { 0u };
};

thread_local bool gInsideAudioThread = false;

struct AudioThreadMarker {
    bool previous = false;

    AudioThreadMarker()
        : previous(gInsideAudioThread)
    {
        gInsideAudioThread = true;
    }

    ~AudioThreadMarker()
    {
        gInsideAudioThread = previous;
    }
};

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
void hostParamsRequestFlush(const clap_host_t* host)
{
    ++hostContext(host)->parameterFlushRequests;
}
void hostRequestRestart(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t* host)
{
    ++hostContext(host)->callbackRequests;
}

void hostRequestProcess(const clap_host_t* host)
{
    ++hostContext(host)->processRequests;
}

void hostTailChanged(const clap_host_t* host)
{
    auto* context = hostContext(host);
    ++context->tailChanges;
    if (!gInsideAudioThread) ++context->nonAudioTailCalls;
}

struct EventList {
    static constexpr uint32_t kParamCapacity = 40u;
    static constexpr uint32_t kNoteCapacity = 8u;
    static constexpr uint32_t kMidiCapacity = 8u;
    static constexpr uint32_t kEventCapacity =
        kParamCapacity + kNoteCapacity + kMidiCapacity;

    std::array<clap_event_param_value_t, kParamCapacity> params {};
    std::array<clap_event_note_t, kNoteCapacity> notes {};
    std::array<clap_event_midi_t, kMidiCapacity> midi {};
    std::array<const clap_event_header_t*, kEventCapacity> events {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t midiCount = 0u;
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
        event.note_id = key;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.velocity = velocity;
        return insert(&event.header);
    }

    bool addMidi(uint8_t key, uint8_t velocity, uint32_t time = 0u)
    {
        if (midiCount >= midi.size()) return false;
        auto& event = midi[midiCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.port_index = 0u;
        event.data[0] = 0x90u;
        event.data[1] = key;
        event.data[2] = velocity;
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
    AudioThreadMarker audioThread;
    return plugin->process(plugin, &process);
}

void resetOnAudioThread(const clap_plugin_t* plugin)
{
    AudioThreadMarker audioThread;
    plugin->reset(plugin);
}

bool startProcessingOnAudioThread(const clap_plugin_t* plugin)
{
    AudioThreadMarker audioThread;
    return plugin->start_processing(plugin);
}

void stopProcessingOnAudioThread(const clap_plugin_t* plugin)
{
    AudioThreadMarker audioThread;
    plugin->stop_processing(plugin);
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

bool blocksMatchExactly(const AudioBlock& first, const AudioBlock& second)
{
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        if (std::memcmp(first.storage[channel].data(),
                second.storage[channel].data(),
                kFrames * sizeof(float)) != 0) {
            return false;
        }
    }
    return true;
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

bool flushParamsOnAudioThread(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params,
    std::initializer_list<std::pair<clap_id, double>> values)
{
    EventList events;
    for (const auto& value : values) {
        if (!events.addParam(value.first, value.second)) return false;
    }
    AudioThreadMarker audioThread;
    params->flush(plugin, &events.input, nullptr);
    return true;
}

bool flushParamsWhileInactive(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params,
    std::initializer_list<std::pair<clap_id, double>> values)
{
    if (gInsideAudioThread) return false;
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

        char text[64] {};
        double parsed = std::numeric_limits<double>::quiet_NaN();
        if (!params->value_to_text(plugin, expected.id,
                expected.textValue, text, sizeof(text))
            || !params->text_to_value(plugin, expected.id, text, &parsed)
            || std::fabs(parsed - expected.textValue) > 1.0e-12) {
            std::cerr << "strict text round trip failed at index " << index
                      << " (" << expected.name << "): " << text
                      << " -> " << parsed << "\n";
            return false;
        }
    }

    clap_param_info_t extra {};
    double invalidValue = 0.0;
    double parsed = 0.0;
    char invalidText[8] {};
    return !params->get_info(plugin,
            static_cast<uint32_t>(kParamSpecs.size()), &extra)
        && !params->get_value(plugin, 9999u, &invalidValue)
        && !params->value_to_text(plugin, 9999u, 0.0,
            invalidText, sizeof(invalidText))
        && !params->text_to_value(plugin, kLowTune,
            "not a number", &parsed)
        && !params->text_to_value(plugin, kLowTune,
            "180.0 bananas", &parsed)
        && !params->text_to_value(plugin, kTrigger,
            "Fire", &parsed);
}

bool exactTimedStart(const clap_plugin_t* plugin, AudioBlock& audio,
    const clap_input_events_t* events, uint32_t strikeTime,
    double& before, double& after)
{
    audio.clear();
    const auto status = runBlock(plugin, audio, events);
    before = 0.0;
    after = 0.0;
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            (sample < strikeTime ? before : after)
                += std::fabs(audio.storage[channel][sample]);
        }
    }
    return status != CLAP_PROCESS_ERROR && finiteBlock(audio)
        && before == 0.0 && after > 1.0e-6;
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
    const bool configured = flushParamsOnAudioThread(plugin, params, {
        { kLowTune, 82.0 },
        { kNoteTracking, 0.0 },
        { kMidTune, 127.0 },
        { kHighTune, 182.0 },
        { kPitchDrop, 0.0 },
        { kSweepTime, 1.0 },
        { kShellSpread, 0.0 },
        { kBody, 1.0 },
        { kRing, 0.2 },
        { kBodyDecay, 1.0 },
        { kDecaySpread, 0.0 },
        { kPunch, 0.0 },
        { kRimLevel, 0.7 },
        { kRimCharacter, 0.5 },
        { kRimDecay, 0.065 },
        { kStickTone, 0.5 },
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
    resetOnAudioThread(plugin);

    EventList strike;
    if (!strike.addNote(key, velocity)) {
        result.finite = false;
        return result;
    }

    constexpr uint32_t kBlockCount = 40u;
    constexpr uint32_t kAnalysisStart = 512u;
    constexpr uint32_t kAnalysisEnd = 9000u;
    std::vector<double> crossings;
    crossings.reserve(100u);
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
        if (period >= kSampleRate / 600.0
            && period <= kSampleRate / 35.0) {
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
        std::cerr << "usage: s3g_drum_toms_clap_smoke "
                  << "<bundle-or-binary> <plugin-id>\n";
        return 2;
    }
    if (std::strcmp(argv[2], kPluginId) != 0) {
        std::cerr << "unexpected drum toms plugin id: " << argv[2]
                  << "\n";
        return 1;
    }

    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve drum toms binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "could not load drum toms: " << dlerror() << "\n";
        return 1;
    }

    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !entry->init(binary.c_str())) {
        std::cerr << "invalid drum toms CLAP entry\n";
        dlclose(library);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const auto* descriptor = factory && factory->get_plugin_count(factory) == 1u
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    if (!descriptor || !descriptor->id || !descriptor->name
        || std::strcmp(descriptor->id, kPluginId) != 0
        || std::strcmp(descriptor->name, kPluginName) != 0) {
        std::cerr << "unexpected drum toms factory or descriptor\n";
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
    context.host.name = "s3g drum toms smoke";
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
        std::cerr << "could not create drum toms plugin\n";
        if (plugin) plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* noteNames = static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME));
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));

    clap_audio_port_info_t outputInfo {};
    clap_note_port_info_t noteInfo {};
    bool ok = audioPorts && notePorts && noteNames && params && state && tail
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
        && (noteInfo.supported_dialects & noteInfo.preferred_dialect) != 0u
        && parameterContract(plugin, params);
    constexpr std::array<int16_t, 9u> expectedNamedKeys {{
        37, 45, 47, 48, 50, 57, 59, 60, 62,
    }};
    bool noteNameContract = noteNames
        && noteNames->count(plugin) == expectedNamedKeys.size();
    for (uint32_t index = 0u;
         noteNameContract && index < expectedNamedKeys.size(); ++index) {
        clap_note_name_t name {};
        noteNameContract = noteNames->get(plugin, index, &name)
            && name.name[0] != '\0' && name.port == 0
            && name.channel == -1 && name.key == expectedNamedKeys[index];
    }
    ok = ok && noteNameContract;
    if (!ok) {
        std::cerr << "drum toms CLAP port/parameter contract failed\n";
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }

    const bool activated = plugin->activate(
        plugin, kSampleRate, 1u, kFrames);
    const bool processing = activated
        && startProcessingOnAudioThread(plugin);
    if (!processing) {
        std::cerr << "could not activate drum toms plugin\n";
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
    const bool silent = silenceStatus != CLAP_PROCESS_ERROR
        && finiteBlock(audio) && blockEnergy(audio) == 0.0;
    if (!silent) std::cerr << "untriggered silence contract failed\n";
    ok = ok && silent;

    // Native CLAP notes must start at their exact sample offsets.
    resetOnAudioThread(plugin);
    EventList delayedNote;
    constexpr uint32_t kNoteTime = 73u;
    const bool addedNote = delayedNote.addNote(
        kBaseMidiNote, 1.0, kNoteTime);
    double noteBefore = 0.0;
    double noteAfter = 0.0;
    const bool clapNoteTiming = addedNote
        && exactTimedStart(plugin, audio, &delayedNote.input, kNoteTime,
            noteBefore, noteAfter);
    if (!clapNoteTiming) {
        std::cerr << "sample-accurate CLAP note start failed: "
                  << noteBefore << " / " << noteAfter << "\n";
    }
    ok = ok && clapNoteTiming;

    // Raw MIDI note-on uses the same sample-accurate trigger path.
    resetOnAudioThread(plugin);
    EventList delayedMidi;
    constexpr uint32_t kMidiTime = 119u;
    const bool addedMidi = delayedMidi.addMidi(
        static_cast<uint8_t>(kBaseMidiNote), 127u, kMidiTime);
    double midiBefore = 0.0;
    double midiAfter = 0.0;
    const bool midiTiming = addedMidi
        && exactTimedStart(plugin, audio, &delayedMidi.input, kMidiTime,
            midiBefore, midiAfter);
    if (!midiTiming) {
        std::cerr << "sample-accurate raw MIDI start failed: "
                  << midiBefore << " / " << midiAfter << "\n";
    }
    ok = ok && midiTiming;

    // Every documented head/rim MIDI route must retain sample-accurate onset.
    constexpr std::array<int16_t, 9u> kMappedNotes {{
        37, 45, 47, 48, 50, 57, 59, 60, 62,
    }};
    bool mappedNotes = true;
    for (const int16_t key : kMappedNotes) {
        resetOnAudioThread(plugin);
        EventList routed;
        constexpr uint32_t kRouteTime = 31u;
        double before = 0.0;
        double after = 0.0;
        mappedNotes = mappedNotes && routed.addNote(key, 0.9, kRouteTime)
            && exactTimedStart(plugin, audio, &routed.input, kRouteTime,
                before, after);
    }
    if (!mappedNotes) std::cerr << "documented tom MIDI routing failed\n";
    ok = ok && mappedNotes;

    // The stepped Trigger parameter exposes all three heads and all three
    // rim-stick articulations. Code then Ready is one transient gesture.
    std::array<AudioBlock, 6u> triggerCaptures;
    bool allTriggerCodes = true;
    for (uint32_t code = 1u; code <= triggerCaptures.size(); ++code) {
        resetOnAudioThread(plugin);
        EventList triggerEvents;
        allTriggerCodes = allTriggerCodes
            && triggerEvents.addParam(kTrigger, code)
            && triggerEvents.addParam(kTrigger, 0.0);
        triggerCaptures[code - 1u].clear();
        const auto status = runBlock(plugin, triggerCaptures[code - 1u],
            &triggerEvents.input);
        allTriggerCodes = allTriggerCodes
            && status != CLAP_PROCESS_ERROR
            && finiteBlock(triggerCaptures[code - 1u])
            && blockEnergy(triggerCaptures[code - 1u]) > 1.0e-9;
    }
    const bool headRimDistinct = allTriggerCodes
        && !blocksMatchExactly(triggerCaptures[1u], triggerCaptures[4u]);
    if (!allTriggerCodes || !headRimDistinct) {
        std::cerr << "six-code Trigger/head-rim contract failed\n";
    }
    ok = ok && allTriggerCodes && headRimDistinct;

    resetOnAudioThread(plugin);
    EventList simultaneous;
    const bool addedSimultaneous = simultaneous.addNote(45, 0.9)
        && simultaneous.addNote(48, 0.9)
        && simultaneous.addNote(50, 0.9);
    audio.clear();
    const auto simultaneousStatus = runBlock(plugin, audio,
        addedSimultaneous ? &simultaneous.input : nullptr);
    const bool simultaneousHits = addedSimultaneous
        && simultaneousStatus != CLAP_PROCESS_ERROR && finiteBlock(audio)
        && blockEnergy(audio) > 1.0e-9
        && !blocksMatchExactly(audio, triggerCaptures[0u])
        && !blocksMatchExactly(audio, triggerCaptures[1u])
        && !blocksMatchExactly(audio, triggerCaptures[2u]);
    if (!simultaneousHits) std::cerr << "simultaneous tom voices failed\n";
    ok = ok && simultaneousHits;

    // Width zero is exact dual mono, including head and rim-stick layers.
    const bool monoConfigured = flushParamsOnAudioThread(plugin, params, {
        { kBody, 0.8 }, { kRing, 0.7 }, { kRimLevel, 0.9 },
        { kRimCharacter, 0.8 }, { kPunch, 0.8 }, { kStereoWidth, 0.0 },
        { kDrive, 0.0 }, { kRateReduction, 0.0 },
        { kBitDepth, 0.0 }, { kReconstruction, 0.0 },
        { kCharacterTone, 0.0 },
    });
    resetOnAudioThread(plugin);
    EventList monoStrike;
    const bool addedMono = monoStrike.addNote(kBaseMidiNote, 0.9);
    bool exactMono = monoConfigured && addedMono;
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

    // Full width must create side energy across positioned tom voices.
    const bool wideConfigured = flushParamsOnAudioThread(plugin, params, {
        { kRimLevel, 1.0 }, { kRimCharacter, 0.8 },
        { kStickTone, 0.6 }, { kRimCharacter, 1.0 },
        { kPunch, 0.7 }, { kStickTone, 0.8 },
        { kStereoWidth, 1.0 },
    });
    resetOnAudioThread(plugin);
    EventList wideStrike;
    const bool addedWide = wideStrike.addNote(kBaseMidiNote, 0.9);
    double wideEnergy = 0.0;
    double sideEnergy = 0.0;
    bool wideFinite = wideConfigured && addedWide;
    for (uint32_t block = 0u; block < 12u; ++block) {
        audio.clear();
        const auto status = runBlock(plugin, audio,
            block == 0u ? &wideStrike.input : nullptr);
        wideFinite = wideFinite && status != CLAP_PROCESS_ERROR
            && finiteBlock(audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            const double left = audio.storage[0u][sample];
            const double right = audio.storage[1u][sample];
            wideEnergy += left * left + right * right;
            const double side = left - right;
            sideEnergy += side * side;
        }
    }
    const bool stereoResponse = wideFinite && wideEnergy > 1.0e-9
        && sideEnergy > wideEnergy * 1.0e-8;
    if (!stereoResponse) {
        std::cerr << "Width=1 produced no side energy: "
                  << sideEnergy << " / " << wideEnergy << "\n";
    }
    ok = ok && stereoResponse;

    const auto lowNote = renderTonalStrike(
        plugin, params, audio, kBaseMidiNote, 1.0);
    const auto midNote = renderTonalStrike(
        plugin, params, audio, 48, 1.0);
    const auto highNote = renderTonalStrike(
        plugin, params, audio, 50, 1.0);
    const auto softNote = renderTonalStrike(
        plugin, params, audio, kBaseMidiNote, 0.2);
    const bool noteResponse = lowNote.finite && midNote.finite
        && highNote.finite
        && softNote.finite
        && lowNote.frequency >= 50.0
        && lowNote.frequency < midNote.frequency
        && midNote.frequency < highNote.frequency
        && lowNote.energy > softNote.energy * 4.0;
    if (!noteResponse) {
        std::cerr << "low/mid/high ordering or velocity response failed: "
                  << lowNote.frequency << " Hz / "
                  << midNote.frequency << " Hz / "
                  << highNote.frequency << " Hz; energy "
                  << softNote.energy << " / " << lowNote.energy << "\n";
    }
    ok = ok && noteResponse;

    // State save/load is a main-thread operation. Published parameter values
    // and the rescan are immediate, while active realtime DSP consumes loaded
    // values on its next process boundary. Trigger remains transient and must
    // never be replayed by load().
    resetOnAudioThread(plugin);
    const bool stateConfigured = flushParamsOnAudioThread(plugin, params, {
        { kLowTune, 123.5 }, { kNoteTracking, 0.23 },
        { kMidTune, 175.5 }, { kHighTune, 260.5 },
        { kPitchDrop, 12.0 }, { kSweepTime, 83.0 },
        { kShellSpread, 0.31 }, { kBody, 0.44 }, { kRing, 0.57 },
        { kBodyDecay, 1.25 }, { kDecaySpread, -0.33 },
        { kPunch, 0.66 }, { kRimLevel, 0.72 },
        { kRimCharacter, 0.56 }, { kRimDecay, 0.24 },
        { kStickTone, 0.48 }, { kDrive, 0.17 },
        { kBias, -0.41 }, { kCompression, 0.26 },
        { kRateReduction, 0.35 }, { kBitDepth, 0.28 },
        { kReconstruction, 0.39 }, { kCharacterTone, 0.22 },
        { kStereoWidth, 0.73 }, { kVelocitySensitivity, 0.81 },
        { kOutputGain, -11.5 },
    });
    stopProcessingOnAudioThread(plugin);
    plugin->deactivate(plugin);

    MemoryState memory;
    clap_ostream_t ostream { &memory, stateWrite };
    constexpr size_t kExpectedStateBytes = 4u * sizeof(uint32_t)
        + (kParamSpecs.size() - 1u) * sizeof(double);
    bool saved = stateConfigured && state->save(plugin, &ostream)
        && memory.bytes.size() == kExpectedStateBytes;
    std::array<uint32_t, 4u> stateHeader {};
    if (saved) {
        std::memcpy(stateHeader.data(), memory.bytes.data(),
            sizeof(stateHeader));
        saved = stateHeader[0u] == 0x54473353u
            && stateHeader[1u] == 1u
            && stateHeader[2u] == kParamSpecs.size() - 1u
            && stateHeader[3u] == 0u;
    }
    const bool stateMutated = flushParamsWhileInactive(plugin, params, {
        { kLowTune, 91.0 }, { kBodyDecay, 0.1 }, { kBias, 0.2 },
        { kRimLevel, 0.0 }, { kRimDecay, 0.02 },
        { kRateReduction, 0.0 }, { kStereoWidth, 0.0 },
    });

    // Consume the mutation before loading so the post-load stereo response
    // cannot accidentally come from the DSP state that existed at save time.
    const bool mutationActivated = plugin->activate(
        plugin, kSampleRate, 1u, kFrames);
    const bool mutationProcessing = mutationActivated
        && startProcessingOnAudioThread(plugin);
    if (!mutationProcessing) {
        std::cerr << "could not activate staged state mutation\n";
        if (mutationActivated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }
    EventList mutatedStateStrike;
    const bool addedMutatedStateStrike = mutatedStateStrike.addNote(
        kBaseMidiNote, 1.0);
    bool mutationFinite = stateMutated && addedMutatedStateStrike;
    bool mutationExactMono = true;
    for (uint32_t block = 0u; block < 4u; ++block) {
        audio.clear();
        const auto status = runBlock(plugin, audio,
            block == 0u ? &mutatedStateStrike.input : nullptr);
        mutationFinite = mutationFinite
            && status != CLAP_PROCESS_ERROR && finiteBlock(audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            mutationExactMono = mutationExactMono
                && audio.storage[0u][sample] == audio.storage[1u][sample];
        }
    }
    resetOnAudioThread(plugin);
    stopProcessingOnAudioThread(plugin);
    plugin->deactivate(plugin);
    const bool mutationConsumed = mutationFinite && mutationExactMono;
    if (!mutationConsumed) {
        std::cerr << "inactive mutation was not consumed on activation\n";
    }

    // An inactive flush is the legal main-thread path for a Trigger. Loading
    // state must clear this queued transient before audio resumes.
    const uint32_t requestsBeforeStagedTrigger = context.processRequests;
    const bool stagedTrigger = flushParamsWhileInactive(plugin, params, {
        { kTrigger, 1.0 },
    });
    const bool stagedTriggerRequested = stagedTrigger
        && context.processRequests > requestsBeforeStagedTrigger;

    // Resume with the inactive Trigger still queued, then load on the main
    // thread before process(). A correct active load stages persistent DSP
    // values and clears the pending transient atomically at that boundary.
    const bool reactivated = plugin->activate(
        plugin, kSampleRate, 1u, kFrames);
    const bool reprocessing = reactivated
        && startProcessingOnAudioThread(plugin);
    if (!reprocessing) {
        std::cerr << "could not reactivate before active state load\n";
        if (reactivated) plugin->deactivate(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        dlclose(library);
        return 1;
    }
    memory.offset = 0u;
    clap_istream_t istream { &memory, stateRead };
    const uint32_t tailChangesBeforeLoad = context.tailChanges;
    const uint32_t rescansBeforeLoad = context.parameterRescans;
    const bool restored = saved && mutationConsumed && stagedTriggerRequested
        && state->load(plugin, &istream)
        && getParam(plugin, params, kLowTune, 123.5)
        && getParam(plugin, params, kMidTune, 175.5)
        && getParam(plugin, params, kHighTune, 260.5)
        && getParam(plugin, params, kBodyDecay, 1.25)
        && getParam(plugin, params, kRimLevel, 0.72)
        && getParam(plugin, params, kRimDecay, 0.24)
        && getParam(plugin, params, kBias, -0.41)
        && getParam(plugin, params, kRateReduction, 0.35)
        && getParam(plugin, params, kStereoWidth, 0.73)
        && getParam(plugin, params, kVelocitySensitivity, 0.81)
        && getParam(plugin, params, kOutputGain, -11.5)
        && getParam(plugin, params, kTrigger, 0.0)
        && context.parameterRescans > rescansBeforeLoad;
    if (!restored) std::cerr << "current state round trip failed\n";

    const bool tailDeferred = context.tailChanges == tailChangesBeforeLoad;
    audio.clear();
    const auto stateProcessStatus = runBlock(plugin, audio);
    const bool stateDidNotTrigger = stateProcessStatus != CLAP_PROCESS_ERROR
        && finiteBlock(audio) && blockEnergy(audio) == 0.0;
    const bool tailDeliveredOnAudioThread =
        context.tailChanges > tailChangesBeforeLoad
        && context.nonAudioTailCalls == 0u;
    if (!tailDeferred || !tailDeliveredOnAudioThread) {
        std::cerr << "tail change was not deferred to the audio thread\n";
    }
    if (!stateDidNotTrigger) {
        std::cerr << "state load replayed transient Trigger\n";
    }
    EventList loadedStateStrike;
    const bool addedLoadedStateStrike = loadedStateStrike.addNote(
        kBaseMidiNote, 1.0);
    double loadedStateEnergy = 0.0;
    double loadedStateSideEnergy = 0.0;
    bool loadedStateFinite = addedLoadedStateStrike;
    for (uint32_t block = 0u; block < 12u; ++block) {
        audio.clear();
        const auto status = runBlock(plugin, audio,
            block == 0u ? &loadedStateStrike.input : nullptr);
        loadedStateFinite = loadedStateFinite
            && status != CLAP_PROCESS_ERROR && finiteBlock(audio);
        loadedStateEnergy += blockEnergy(audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            const double side = static_cast<double>(
                audio.storage[0u][sample]) - audio.storage[1u][sample];
            loadedStateSideEnergy += side * side;
        }
    }
    const bool stateConsumedByAudio = loadedStateFinite
        && loadedStateEnergy > 1.0e-9
        && loadedStateSideEnergy > loadedStateEnergy * 1.0e-8;
    if (!stateConsumedByAudio) {
        std::cerr << "staged state was not consumed by realtime DSP: "
                  << loadedStateSideEnergy << " / "
                  << loadedStateEnergy << " side/total energy\n";
    }
    ok = ok && restored && tailDeferred && stateDidNotTrigger
        && tailDeliveredOnAudioThread && stateConsumedByAudio;

    // An inactive load has no realtime consumer and must therefore be applied
    // immediately. A later inactive flush is later in callback order and must
    // not be overwritten by an old pending snapshot during activate(). Width
    // zero makes the resulting DSP ordering observable as exact dual mono.
    resetOnAudioThread(plugin);
    stopProcessingOnAudioThread(plugin);
    plugin->deactivate(plugin);
    memory.offset = 0u;
    clap_istream_t inactiveIstream { &memory, stateRead };
    const bool inactiveStateLoaded = state->load(plugin, &inactiveIstream);
    const bool laterInactiveEdit = flushParamsWhileInactive(plugin, params, {
        { kStereoWidth, 0.0 },
    });
    const bool orderingActivated = plugin->activate(
        plugin, kSampleRate, 1u, kFrames);
    const bool orderingProcessing = orderingActivated
        && startProcessingOnAudioThread(plugin);
    EventList orderingStrike;
    const bool addedOrderingStrike = orderingStrike.addNote(
        kBaseMidiNote, 1.0);
    bool inactiveCallbackOrder = inactiveStateLoaded && laterInactiveEdit
        && orderingProcessing && addedOrderingStrike;
    double orderingEnergy = 0.0;
    for (uint32_t block = 0u; block < 4u; ++block) {
        audio.clear();
        const auto status = orderingProcessing
            ? runBlock(plugin, audio,
                block == 0u ? &orderingStrike.input : nullptr)
            : CLAP_PROCESS_ERROR;
        inactiveCallbackOrder = inactiveCallbackOrder
            && status != CLAP_PROCESS_ERROR && finiteBlock(audio);
        for (uint32_t sample = 0u; sample < kFrames; ++sample) {
            inactiveCallbackOrder = inactiveCallbackOrder
                && audio.storage[0u][sample] == audio.storage[1u][sample];
        }
        orderingEnergy += blockEnergy(audio);
    }
    inactiveCallbackOrder = inactiveCallbackOrder
        && orderingEnergy > 1.0e-9;
    if (!inactiveCallbackOrder) {
        std::cerr << "inactive load/flush callback order was reversed\n";
    }
    ok = ok && inactiveCallbackOrder;

    // A main-thread Trigger flush is legal only while inactive. It must ask
    // the host for processing and fire once audio processing resumes.
    if (orderingProcessing) resetOnAudioThread(plugin);
    if (orderingProcessing) stopProcessingOnAudioThread(plugin);
    if (orderingActivated) plugin->deactivate(plugin);
    const uint32_t requestsBeforeTrigger = context.processRequests;
    const bool addedTrigger = flushParamsWhileInactive(plugin, params, {
        { kTrigger, 1.0 },
    });
    const bool processRequested = addedTrigger
        && context.processRequests > requestsBeforeTrigger;
    const bool triggerReactivated = plugin->activate(
        plugin, kSampleRate, 1u, kFrames);
    const bool triggerProcessing = triggerReactivated
        && startProcessingOnAudioThread(plugin);
    audio.clear();
    const auto triggerStatus = triggerProcessing
        ? runBlock(plugin, audio) : CLAP_PROCESS_ERROR;
    const bool parameterTriggered = processRequested && triggerProcessing
        && triggerStatus != CLAP_PROCESS_ERROR && finiteBlock(audio)
        && blockEnergy(audio) > 1.0e-9;
    if (!parameterTriggered) {
        std::cerr << "Trigger did not request/process a toms strike\n";
    }
    ok = ok && parameterTriggered;

    // Once a long voice has started, later edits must not make the advertised
    // tail shorter than the bound under which that active voice was created.
    // The voice itself snapshots its synthesis parameters, so the CLAP tail
    // must likewise retain a host-safe active bound until reset/completion.
    const bool longVoiceConfigured = flushParamsOnAudioThread(
        plugin, params, {
            { kSweepTime, 250.0 }, { kBody, 1.0 }, { kRing, 1.0 },
            { kBodyDecay, 3.0 }, { kPunch, 1.0 },
            { kDecaySpread, 1.0 },
            { kRimLevel, 1.0 }, { kRimDecay, 0.30 },
            { kRimCharacter, 1.0 }, { kPunch, 1.0 }, { kDrive, 1.0 },
        });
    resetOnAudioThread(plugin);
    const uint32_t configuredLongVoiceTail = tail->get(plugin);
    EventList longVoiceStrike;
    const bool addedLongVoice = longVoiceStrike.addNote(
        kBaseMidiNote, 1.0);
    audio.clear();
    const auto longVoiceStatus = runBlock(plugin, audio,
        addedLongVoice ? &longVoiceStrike.input : nullptr);
    const uint32_t latchedLongVoiceTail = tail->get(plugin);
    const bool shortenedActiveVoice = flushParamsOnAudioThread(
        plugin, params, {
            { kSweepTime, 1.0 }, { kBody, 0.0 }, { kRing, 0.0 },
            { kBodyDecay, 0.03 }, { kDecaySpread, 0.0 },
            { kPunch, 0.0 },
            { kRimLevel, 0.0 }, { kRimDecay, 0.02 },
            { kRimCharacter, 0.0 }, { kPunch, 0.0 }, { kDrive, 0.0 },
        });
    const uint32_t shortenedActiveTail = tail->get(plugin);
    const bool activeTailLatched = longVoiceConfigured && addedLongVoice
        && longVoiceStatus != CLAP_PROCESS_ERROR && finiteBlock(audio)
        && blockEnergy(audio) > 1.0e-9 && shortenedActiveVoice
        && configuredLongVoiceTail
            >= static_cast<uint32_t>(kSampleRate * 10.0)
        && latchedLongVoiceTail >= configuredLongVoiceTail
        && shortenedActiveTail >= latchedLongVoiceTail;
    if (!activeTailLatched) {
        std::cerr << "active long-voice tail shrank after parameter edits: "
                  << configuredLongVoiceTail << " configured, "
                  << latchedLongVoiceTail << " latched, "
                  << shortenedActiveTail << " after shortening\n";
    }
    ok = ok && activeTailLatched;
    resetOnAudioThread(plugin);

    // The longer resonant rim body must be included in the advertised tail.
    const bool longRimConfigured = flushParamsOnAudioThread(plugin, params, {
        { kSweepTime, 1.0 }, { kBodyDecay, 0.03 },
        { kDecaySpread, 0.0 }, { kRimDecay, 0.30 },
        { kRimLevel, 1.0 }, { kRimCharacter, 0.0 }, { kPunch, 0.0 },
    });
    const uint32_t longRimTail = tail->get(plugin);
    const bool rimTailCovered = longRimConfigured
        && longRimTail >= static_cast<uint32_t>(kSampleRate * 2.03)
        && longRimTail <= static_cast<uint32_t>(kSampleRate * 2.10);
    if (!rimTailCovered) {
        std::cerr << "rim lifetime was absent from CLAP tail: "
                  << longRimTail << "\n";
    }
    ok = ok && rimTailCovered;

    // DrumCharacter's drive/DC-blocker can outlive otherwise tight envelopes.
    // An active flush runs on the audio thread, so tail.changed() may be sent
    // immediately there or coalesced for the following process() call.
    const bool tightDriveBase = flushParamsOnAudioThread(plugin, params, {
        { kSweepTime, 1.0 }, { kBody, 0.0 }, { kRing, 0.0 },
        { kBodyDecay, 0.03 }, { kPunch, 1.0 },
        { kRimLevel, 0.0 }, { kRimDecay, 0.02 },
        { kRimCharacter, 0.0 },
        { kPunch, 0.0 }, { kStickTone, 1.0 }, { kDrive, 0.0 },
    });
    resetOnAudioThread(plugin);
    audio.clear();
    const auto driveBaseStatus = runBlock(plugin, audio);
    const uint32_t tailChangesBeforeDrive = context.tailChanges;
    const bool driveConfigured = flushParamsOnAudioThread(plugin, params, {
        { kDrive, 1.0 },
    });
    const uint32_t driveTail = tail->get(plugin);
    const bool driveTailConfigured = tightDriveBase
        && driveBaseStatus != CLAP_PROCESS_ERROR && driveConfigured
        && driveTail >= static_cast<uint32_t>(kSampleRate * 0.50)
        && context.nonAudioTailCalls == 0u;
    audio.clear();
    const auto driveTailStatus = runBlock(plugin, audio);
    const bool driveTailDelivered = driveTailStatus != CLAP_PROCESS_ERROR
        && context.tailChanges > tailChangesBeforeDrive
        && context.nonAudioTailCalls == 0u;
    if (!driveTailConfigured || !driveTailDelivered) {
        std::cerr << "Drive tail/callback contract failed: "
                  << driveTail << " samples\n";
    }
    ok = ok && driveTailConfigured && driveTailDelivered;

    // With minimum envelopes the finite tail must eventually let the host
    // sleep, with a small block allowance beyond the advertised lifetime.
    const bool addedRelease = flushParamsOnAudioThread(plugin, params, {
        { kTrigger, 0.0 },
    });
    const bool shortConfigured = flushParamsOnAudioThread(plugin, params, {
        { kPitchDrop, 0.0 }, { kShellSpread, 0.0 },
        { kBody, 1.0 }, { kRing, 0.0 }, { kBodyDecay, 0.03 },
        { kPunch, 0.0 }, { kRimLevel, 0.0 }, { kRimDecay, 0.02 },
        { kRimCharacter, 0.0 }, { kPunch, 0.0 },
        { kDrive, 0.0 }, { kCompression, 0.0 },
        { kRateReduction, 0.0 }, { kBitDepth, 0.0 },
        { kReconstruction, 0.0 }, { kStereoWidth, 0.0 },
    });
    resetOnAudioThread(plugin);
    const uint32_t tailSamples = tail->get(plugin);
    const bool finiteTail = addedRelease && shortConfigured
        && tailSamples > 0u
        && tailSamples < static_cast<uint32_t>(
            std::numeric_limits<int32_t>::max())
        && tailSamples < static_cast<uint32_t>(kSampleRate * 6.0);
    EventList shortStrike;
    const bool addedShort = shortStrike.addNote(kBaseMidiNote, 1.0);
    const uint32_t tailBlocks = finiteTail
        ? (tailSamples + kFrames - 1u) / kFrames + 4u : 4u;
    bool slept = false;
    bool tailOutputFinite = addedShort;
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
        && context.nonAudioTailCalls == 0u;
    if (!tailContract) {
        std::cerr << "finite output/tail contract failed: "
                  << tailSamples << " samples, slept=" << slept << "\n";
    }
    ok = ok && tailContract;

    // Exercise the active state handoff with genuine main/audio concurrency.
    // Resetting before every note makes each complete state deterministic.
    // The global smoothed character/output controls are identical in A and B,
    // while all voice-defining controls differ. Every rendered block must then
    // bit-match the complete-A or complete-B reference; a torn hybrid snapshot
    // cannot be silently accepted as a third DSP configuration.
    stopProcessingOnAudioThread(plugin);
    plugin->deactivate(plugin);
    MemoryState coherenceStateA;
    MemoryState coherenceStateB;
    const bool coherenceConfiguredA = flushParamsWhileInactive(
        plugin, params, {
            { kLowTune, 60.0 }, { kNoteTracking, 0.0 },
            { kMidTune, 90.0 }, { kHighTune, 130.0 },
            { kPitchDrop, -6.0 }, { kSweepTime, 1.0 },
            { kShellSpread, 0.0 }, { kBody, 1.0 }, { kRing, 0.0 },
            { kBodyDecay, 0.08 }, { kDecaySpread, -1.0 },
            { kPunch, 0.10 }, { kRimLevel, 0.05 },
            { kRimCharacter, 0.0 }, { kRimDecay, 0.05 },
            { kStickTone, 0.0 },
            { kDrive, 0.0 }, { kBias, 0.0 }, { kCompression, 0.0 },
            { kRateReduction, 0.0 }, { kBitDepth, 0.0 },
            { kReconstruction, 0.0 }, { kCharacterTone, 0.0 },
            { kStereoWidth, 0.65 }, { kVelocitySensitivity, 0.0 },
            { kOutputGain, -6.0 },
        });
    clap_ostream_t coherenceOutputA { &coherenceStateA, stateWrite };
    const bool coherenceSavedA = coherenceConfiguredA
        && state->save(plugin, &coherenceOutputA);
    const bool coherenceConfiguredB = flushParamsWhileInactive(
        plugin, params, {
            { kLowTune, 170.0 }, { kNoteTracking, 1.0 },
            { kMidTune, 260.0 }, { kHighTune, 400.0 },
            { kPitchDrop, 30.0 }, { kSweepTime, 250.0 },
            { kShellSpread, 1.0 }, { kBody, 0.15 }, { kRing, 1.0 },
            { kBodyDecay, 2.5 }, { kDecaySpread, 1.0 },
            { kPunch, 1.0 }, { kRimLevel, 1.0 },
            { kRimCharacter, 1.0 }, { kRimDecay, 0.30 },
            { kStickTone, 1.0 },
            { kDrive, 0.0 }, { kBias, 0.0 }, { kCompression, 0.0 },
            { kRateReduction, 0.0 }, { kBitDepth, 0.0 },
            { kReconstruction, 0.0 }, { kCharacterTone, 0.0 },
            { kStereoWidth, 0.65 }, { kVelocitySensitivity, 1.0 },
            { kOutputGain, -6.0 },
        });
    clap_ostream_t coherenceOutputB { &coherenceStateB, stateWrite };
    const bool coherenceSavedB = coherenceConfiguredB
        && state->save(plugin, &coherenceOutputB);
    const bool coherenceActivated = plugin->activate(
        plugin, kSampleRate, 1u, kFrames);
    const bool coherenceProcessing = coherenceActivated
        && startProcessingOnAudioThread(plugin);

    const auto loadCoherenceState = [&](MemoryState& snapshot) {
        snapshot.offset = 0u;
        clap_istream_t stream { &snapshot, stateRead };
        return state->load(plugin, &stream);
    };
    EventList coherenceStrike;
    const bool coherenceStrikeAdded = coherenceStrike.addNote(
        50, 0.37);
    AudioBlock coherenceReferenceA;
    AudioBlock coherenceReferenceB;
    const auto renderCoherenceReference = [&](MemoryState& snapshot,
                                               AudioBlock& reference) {
        if (!coherenceProcessing || !loadCoherenceState(snapshot)) {
            return false;
        }
        resetOnAudioThread(plugin);
        reference.clear();
        const auto status = runBlock(
            plugin, reference, &coherenceStrike.input);
        return status != CLAP_PROCESS_ERROR && finiteBlock(reference)
            && blockEnergy(reference) > 1.0e-9;
    };
    const bool coherenceReferences = coherenceSavedA && coherenceSavedB
        && coherenceStrikeAdded
        && renderCoherenceReference(coherenceStateA, coherenceReferenceA)
        && renderCoherenceReference(coherenceStateB, coherenceReferenceB)
        && !blocksMatchExactly(coherenceReferenceA, coherenceReferenceB);

    constexpr uint32_t kCoherenceLoads = 30000u;
    constexpr uint32_t kMinimumCoherenceBlocks = 1024u;
    std::atomic<bool> coherenceStart { false };
    std::atomic<bool> coherenceWriterDone { false };
    std::atomic<uint32_t> coherenceBlocks { 0u };
    std::atomic<uint32_t> coherenceOverlapBlocks { 0u };
    std::atomic<uint32_t> coherenceHybridBlocks { 0u };
    std::atomic<uint32_t> coherenceProcessErrors { 0u };
    bool coherenceLoadsOk = coherenceReferences;
    if (coherenceReferences) {
        std::thread audioWorker([&] {
            AudioBlock candidate;
            while (!coherenceStart.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            do {
                const bool writerActive = !coherenceWriterDone.load(
                    std::memory_order_acquire);
                resetOnAudioThread(plugin);
                candidate.clear();
                const auto status = runBlock(
                    plugin, candidate, &coherenceStrike.input);
                if (status == CLAP_PROCESS_ERROR || !finiteBlock(candidate)) {
                    coherenceProcessErrors.fetch_add(
                        1u, std::memory_order_relaxed);
                } else if (!blocksMatchExactly(candidate, coherenceReferenceA)
                    && !blocksMatchExactly(candidate, coherenceReferenceB)) {
                    coherenceHybridBlocks.fetch_add(
                        1u, std::memory_order_relaxed);
                }
                coherenceBlocks.fetch_add(1u, std::memory_order_relaxed);
                if (writerActive) {
                    coherenceOverlapBlocks.fetch_add(
                        1u, std::memory_order_relaxed);
                }
            } while (!coherenceWriterDone.load(std::memory_order_acquire)
                || coherenceBlocks.load(std::memory_order_relaxed)
                    < kMinimumCoherenceBlocks);
        });

        coherenceStart.store(true, std::memory_order_release);
        for (uint32_t iteration = 0u; iteration < kCoherenceLoads;
             ++iteration) {
            coherenceLoadsOk = loadCoherenceState(
                (iteration & 1u) == 0u
                    ? coherenceStateA : coherenceStateB)
                && coherenceLoadsOk;
            if ((iteration & 31u) == 0u) std::this_thread::yield();
        }
        coherenceWriterDone.store(true, std::memory_order_release);
        audioWorker.join();
    }
    const bool coherentConcurrentLoads = coherenceLoadsOk
        && coherenceBlocks.load(std::memory_order_relaxed)
            >= kMinimumCoherenceBlocks
        && coherenceOverlapBlocks.load(std::memory_order_relaxed) > 0u
        && coherenceHybridBlocks.load(std::memory_order_relaxed) == 0u
        && coherenceProcessErrors.load(std::memory_order_relaxed) == 0u;
    if (!coherentConcurrentLoads) {
        std::cerr << "concurrent state handoff consumed a hybrid snapshot: "
                  << coherenceBlocks.load(std::memory_order_relaxed)
                  << " blocks, "
                  << coherenceOverlapBlocks.load(std::memory_order_relaxed)
                  << " overlapping, "
                  << coherenceHybridBlocks.load(std::memory_order_relaxed)
                  << " hybrid, "
                  << coherenceProcessErrors.load(std::memory_order_relaxed)
                  << " errors\n";
    }
    ok = ok && coherentConcurrentLoads;

    if (coherenceProcessing) stopProcessingOnAudioThread(plugin);
    if (coherenceActivated) plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);

    if (!ok) {
        std::cerr << "drum toms CLAP smoke failed\n";
        return 1;
    }
    std::cout << "drum toms CLAP smoke passed\n";
    return 0;
}
