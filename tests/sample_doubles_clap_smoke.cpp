#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

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

constexpr uint32_t kStateMagic = 0x44443353u;
constexpr uint32_t kStateVersion = 1u;
constexpr std::size_t kParamCount = 12u;
constexpr std::size_t kMaximumPathBytes = 1024u;

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 1u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 4096u;
    double sampleRate = 1000.0;
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

    void add(uint32_t frame, uint16_t type, int32_t noteId, int16_t key,
        double velocity)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.time = frame;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.note_id = noteId;
        event.velocity = velocity;
        events.push_back(event);
    }
};

bool processBlock(const clap_plugin_t* plugin, uint32_t frames,
    NoteEvents* events, std::vector<float>& left,
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
    process.in_events = events ? &events->interface : nullptr;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool near(float actual, float expected, float tolerance = 0.002f)
{
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_doubles_clap_smoke "
            "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "Could not resolve Sample Doubles binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Sample Doubles: " << dlerror() << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    bool ok = entry && entry->init(binary.c_str());

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Sample Doubles smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;

    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    const clap_plugin_t* plugin = factory ? factory->create_plugin(
        factory, &host, "org.s3g.s3g-dsp.sample-doubles") : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 1u
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
    ok = ok && ports && notePorts && noteNames && params && state
        && ports->count(plugin, true) == 0u
        && ports->count(plugin, false) == 1u
        && ports->get(plugin, 0u, false, &outputPort)
        && outputPort.channel_count == 2u
        && notePorts->count(plugin, true) == 1u
        && noteNames->count(plugin) == 21u
        && params->count(plugin) == kParamCount;
#if defined(__APPLE__)
    ok = ok && gui;
#endif

    bool foundCrossfader = false;
    bool foundPhase = false;
    for (uint32_t index = 0u; ok && index < params->count(plugin); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info);
        if (info.id == 9u)
            foundCrossfader = std::strcmp(info.name, "Crossfader") == 0;
        if (info.id == 2u)
            foundPhase = std::strcmp(info.name, "Phase Drift") == 0;
    }
    ok = ok && foundCrossfader && foundPhase;

    SavedState saved;
    saved.parameters = {{
        0.0, 0.0, 60.0, 1.0, 2.0, 0.0, 1.0, 0.0,
        -1.0, 0.0, 0.0, 0.0,
    }};
    std::vector<float> samples(saved.frameCount);
    for (uint32_t frame = 0u; frame < saved.frameCount; ++frame)
        samples[frame] = static_cast<float>(frame) * 0.0001f;
    MemoryInput input;
    input.bytes.resize(sizeof(saved) + samples.size() * sizeof(float));
    std::memcpy(input.bytes.data(), &saved, sizeof(saved));
    std::memcpy(input.bytes.data() + sizeof(saved), samples.data(),
        samples.size() * sizeof(float));
    ok = ok && state->load(plugin, &input.stream);
    ok = ok && plugin->activate(plugin, 1000.0, 1u, 128u)
        && plugin->start_processing(plugin);

    NoteEvents commands;
    commands.add(0u, CLAP_EVENT_NOTE_ON, 1, 36, 1.0);
    commands.add(10u, CLAP_EVENT_NOTE_ON, 2, 41, 1.0);
    commands.add(30u, CLAP_EVENT_NOTE_OFF, 2, 41, 0.0);
    std::vector<float> left;
    std::vector<float> right;
    ok = ok && processBlock(plugin, 64u, &commands, left, right)
        && near(left[4u], 0.0004f)
        && left[18u] > 0.10f
        && left[42u] < 0.01f
        && near(left[42u], right[42u]);

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    if (entry) entry->deinit();
    dlclose(library);
    if (!ok) {
        std::cerr << "Sample Doubles CLAP smoke failed\n";
        return 1;
    }
    std::cout << "s3g Sample Doubles CLAP smoke: ok\n";
    return 0;
}
