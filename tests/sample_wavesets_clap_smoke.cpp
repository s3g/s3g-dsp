#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
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

constexpr uint32_t kStateMagic = 0x57533353u;
constexpr uint32_t kStateVersion = 2u;
constexpr std::size_t kParamCount = 20u;
constexpr std::size_t kMaximumPathBytes = 1024u;

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 2u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 48000u;
    double sampleRate = 48000.0;
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto macOS = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(macOS))
            if (entry.is_regular_file()) return entry.path();
    }
#endif
    return {};
}

struct MemoryInput {
    std::vector<uint8_t> bytes;
    std::size_t offset = 0u;
    clap_istream_t stream {
        this,
        [](const clap_istream_t* stream, void* buffer,
            uint64_t size) -> int64_t {
            auto* self = static_cast<MemoryInput*>(stream->ctx);
            if (!self || !buffer) return -1;
            const std::size_t available = self->bytes.size() - self->offset;
            const std::size_t count = std::min<std::size_t>(available,
                static_cast<std::size_t>(size));
            if (count == 0u) return 0;
            std::memcpy(buffer, self->bytes.data() + self->offset, count);
            self->offset += count;
            return static_cast<int64_t>(count);
        },
    };
};

struct MemoryOutput {
    std::vector<uint8_t> bytes;
    clap_ostream_t stream {
        this,
        [](const clap_ostream_t* stream, const void* buffer,
            uint64_t size) -> int64_t {
            auto* self = static_cast<MemoryOutput*>(stream->ctx);
            if (!self || (!buffer && size > 0u)) return -1;
            const auto* first = static_cast<const uint8_t*>(buffer);
            self->bytes.insert(self->bytes.end(), first,
                first + static_cast<std::size_t>(size));
            return static_cast<int64_t>(size);
        },
    };
};

struct NoteEvents {
    std::vector<clap_event_note_t> events;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const NoteEvents*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const NoteEvents*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void add(uint32_t frame, uint16_t type, int16_t channel,
        int16_t key, double velocity)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.port_index = 0;
        event.channel = channel;
        event.key = key;
        event.note_id = channel * 128 + key;
        event.velocity = velocity;
        events.push_back(event);
    }
};

struct MidiEvents {
    std::vector<clap_event_midi_t> events;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            const auto* self = static_cast<const MidiEvents*>(list->ctx);
            return self ? static_cast<uint32_t>(self->events.size()) : 0u;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const MidiEvents*>(list->ctx);
            return self && index < self->events.size()
                ? &self->events[index].header : nullptr;
        },
    };

    void addCc(uint32_t frame, uint8_t channel, uint8_t controller,
        uint8_t value)
    {
        clap_event_midi_t event {};
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0u] = static_cast<uint8_t>(0xb0u | (channel & 0x0fu));
        event.data[1u] = controller;
        event.data[2u] = value;
        events.push_back(event);
    }
};

bool processBlock(const clap_plugin_t* plugin, uint32_t frames,
    const clap_input_events_t* events, std::vector<float>& left,
    std::vector<float>& right)
{
    left.assign(frames, 0.0f);
    right.assign(frames, 0.0f);
    float* channels[] { left.data(), right.data() };
    clap_audio_buffer_t output {};
    output.data32 = channels;
    output.channel_count = 2u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = frames;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

float maximumMagnitude(const std::vector<float>& samples)
{
    float maximum = 0.0f;
    for (const float sample : samples)
        maximum = std::max(maximum, std::abs(sample));
    return maximum;
}

double sinusoidMagnitude(const std::vector<float>& samples,
    double sampleRate, double frequency)
{
    if (samples.empty()) return 0.0;
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t index = 0u; index < samples.size(); ++index) {
        const double phase = 2.0 * 3.14159265358979323846 * frequency
            * static_cast<double>(index) / sampleRate;
        real += static_cast<double>(samples[index]) * std::cos(phase);
        imaginary -= static_cast<double>(samples[index]) * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary)
        / static_cast<double>(samples.size());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_wavesets_clap_smoke "
            "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Sample Wavesets binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Sample Wavesets: " << dlerror() << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Sample Wavesets smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_descriptor_t* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.sample-wavesets") : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 1u
        && descriptor && std::strcmp(descriptor->name,
            "s3g Sample Wavesets") == 0
        && plugin && plugin->init(plugin);

    const auto* ports = ok ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* notePorts = ok ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* noteNames = ok ? static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME)) : nullptr;
    const auto* params = ok ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = ok ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* gui = ok ? static_cast<const clap_plugin_gui_t*>(
        plugin->get_extension(plugin, CLAP_EXT_GUI)) : nullptr;
    clap_audio_port_info_t outputPort {};
    clap_note_port_info_t notePort {};
    clap_note_name_t restartName {};
    ok = ok && ports && notePorts && noteNames && params && state
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputPort)
        && outputPort.channel_count == 2u
        && notePorts->count(plugin, true) == 1u
        && notePorts->get(plugin, 0u, true, &notePort)
        && (notePort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u
        && noteNames->count(plugin) == 13u
        && noteNames->get(plugin, 0u, &restartName)
        && restartName.key == 36
        && std::strcmp(restartName.name, "RESTART BOTH") == 0
        && params->count(plugin) == kParamCount;
#if defined(__APPLE__)
    ok = ok && gui;
#else
    (void)gui;
#endif

    std::array<bool, kParamCount + 1u> found {};
    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info)
            && info.id >= 1u && info.id <= kParamCount;
        if (ok && info.id == 13u)
            ok = std::strcmp(info.name, "MIDI Channel") == 0
                && info.min_value == 1.0 && info.max_value == 16.0;
        if (ok && info.id == 2u)
            ok = std::strcmp(info.name, "Crossfader") == 0
                && info.min_value == -1.0 && info.max_value == 1.0;
        if (ok && info.id == 4u)
            ok = std::strcmp(info.name, "Time") == 0
                && info.min_value == 0.0 && info.max_value == 2.0;
        if (ok) found[info.id] = true;
    }
    ok = ok && std::all_of(found.begin() + 1, found.end(),
        [](bool present) { return present; });

    SavedState saved;
    saved.parameters = {{
        -6.0, 0.0, 2.0, 0.0, 3.0, 2.0, 1.0, 0.0, 0.0,
        0.0, 1.0, 3.0, 1.0, 1.0,
        0.0, 1.0, 0.0, 0.25, 1.0, 0.0,
    }};
    std::array<std::vector<float>, 2u> samples;
    for (auto& channel : samples) channel.resize(saved.frameCount);
    constexpr double pi = 3.14159265358979323846;
    for (uint32_t frame = 0u; frame < saved.frameCount; ++frame) {
        const double phase = 2.0 * pi * 220.0
            * static_cast<double>(frame) / saved.sampleRate;
        samples[0u][frame] = static_cast<float>(0.7 * std::sin(phase));
        samples[1u][frame] = static_cast<float>(0.55 * std::sin(phase + 0.2));
    }
    MemoryInput input;
    input.bytes.resize(sizeof(saved)
        + samples[0u].size() * sizeof(float) * 2u);
    std::memcpy(input.bytes.data(), &saved, sizeof(saved));
    std::memcpy(input.bytes.data() + sizeof(saved), samples[0u].data(),
        samples[0u].size() * sizeof(float));
    std::memcpy(input.bytes.data() + sizeof(saved)
            + samples[0u].size() * sizeof(float),
        samples[1u].data(), samples[1u].size() * sizeof(float));
    ok = ok && state->load(plugin, &input.stream)
        && plugin->activate(plugin, 48000.0, 1u, 256u)
        && plugin->start_processing(plugin);

    std::vector<float> left;
    std::vector<float> right;
    NoteEvents notes;
    notes.add(0u, CLAP_EVENT_NOTE_ON, 0, 36, 1.0);
    ok = ok && processBlock(plugin, 256u, &notes.interface, left, right)
        && maximumMagnitude(left) > 0.02f
        && maximumMagnitude(right) > 0.02f;
    float sustainedPeak = 0.0f;
    for (uint32_t block = 0u; ok && block < 16u; ++block) {
        ok = processBlock(plugin, 256u, nullptr, left, right);
        sustainedPeak = std::max(sustainedPeak, maximumMagnitude(left));
    }
    ok = ok && sustainedPeak > 0.02f;

    // Adjacent MIDI channels no longer address individual heads.
    MidiEvents ignoredCc;
    ignoredCc.addCc(0u, 1u, 18u, 127u);
    ok = ok && processBlock(plugin, 64u, &ignoredCc.interface, left, right);
    double ignoredPositionB = 0.0;
    ok = ok && params->get_value(plugin, 18u, &ignoredPositionB)
        && std::abs(ignoredPositionB - 0.25) < 1.0e-9;

    MidiEvents cc;
    cc.addCc(0u, 0u, 18u, 127u);
    cc.addCc(0u, 0u, 20u, 95u);
    cc.addCc(0u, 0u, 1u, 127u);
    ok = ok && processBlock(plugin, 64u, &cc.interface, left, right);
    double positionB = 0.0;
    double scanB = 0.0;
    double crossfader = 0.0;
    ok = ok && params->get_value(plugin, 18u, &positionB)
        && params->get_value(plugin, 19u, &scanB)
        && params->get_value(plugin, 2u, &crossfader)
        && positionB > 0.999 && scanB > 3.04 && scanB < 3.07
        && crossfader > 0.999;

    MemoryOutput output;
    ok = ok && state->save(plugin, &output.stream)
        && output.bytes.size() == sizeof(SavedState)
            + samples[0u].size() * sizeof(float) * 2u;
    if (ok) {
        SavedState roundTrip;
        std::memcpy(&roundTrip, output.bytes.data(), sizeof(roundTrip));
        ok = roundTrip.magic == kStateMagic
            && roundTrip.version == kStateVersion
            && roundTrip.parameterCount == kParamCount
            && roundTrip.embedded == 1u
            && roundTrip.channelCount == 2u
            && roundTrip.frameCount == saved.frameCount
            && std::abs(roundTrip.parameters[18u] - scanB) < 1.0e-9;
    }

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Sample Wavesets CLAP smoke failed\n";
        return 1;
    }
    std::cout << "s3g Sample Wavesets CLAP smoke: ok\n";
    return 0;
}
