#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
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

constexpr uint32_t kStateMagic = 0x43553353u;
constexpr uint32_t kStateVersion = 1u;
constexpr std::size_t kParamCount = 42u;
constexpr std::size_t kStereoParamCount = 35u;
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr uint32_t kFrames = 4096u;

struct LaneState {
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 2u;
    uint16_t reserved = 0u;
    uint32_t frameCount = kFrames;
    double sampleRate = 48000.0;
};

struct PathPoint {
    float phase = 0.0f;
    float lane = 0.0f;
};

struct ManualPathState {
    uint32_t pointCount = 16u;
    uint32_t reserved = 0u;
    std::array<PathPoint, 16u> points {};
};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    uint8_t storageMode = 2u;
    std::array<uint8_t, 3u> reserved {};
    std::array<double, kParamCount> parameters {{
        -6.0, 1.0, 4.0, 8.0, 0.0, 1.0, 5.0,
        5.0, 6.0, 1.0, 16.0, 16.0, 1.0, 0.0, 1.0, 0.0,
        0.0, 1.0, 60.0, 0.0, 0.0, 0.0, 0.02, 0.0, 1.0,
        4312.0, 0.0,
        120.0, 100.0, 140.0, 90.0, 1.0, 0.15, 2.0, 0.1,
        0.0, 32.0, 0.0, 0.0, 1.0, 0.0, 0.0,
    }};
    std::array<LaneState, 4u> lanes {};
    ManualPathState manualPath;

    SavedState()
    {
        for (uint32_t step = 0u; step < manualPath.points.size(); ++step) {
            manualPath.points[step].phase = static_cast<float>((step * 5u)
                % manualPath.points.size())
                / static_cast<float>(manualPath.points.size());
            manualPath.points[step].lane = static_cast<float>(step % 4u)
                / 3.0f;
        }
    }
};

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory))
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
            if (!self || !buffer || self->offset > self->bytes.size())
                return -1;
            const std::size_t count = std::min<std::size_t>(
                self->bytes.size() - self->offset,
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
            if (!self || (!buffer && size != 0u)) return -1;
            const auto* first = static_cast<const uint8_t*>(buffer);
            self->bytes.insert(self->bytes.end(), first,
                first + static_cast<std::size_t>(size));
            return static_cast<int64_t>(size);
        },
    };
};

MemoryInput embeddedFixture()
{
    SavedState saved;
    MemoryInput input;
    const auto append = [&input](const void* data, std::size_t size) {
        const auto* first = static_cast<const uint8_t*>(data);
        input.bytes.insert(input.bytes.end(), first, first + size);
    };
    constexpr std::size_t headerBytes = 16u;
    append(&saved, headerBytes);
    append(saved.parameters.data(), saved.parameters.size() * sizeof(double));
    append(saved.lanes.data(), saved.lanes.size() * sizeof(LaneState));
    append(&saved.manualPath, sizeof(saved.manualPath));
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t lane = 0u; lane < 4u; ++lane) {
        std::vector<float> left(kFrames);
        std::vector<float> right(kFrames);
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            const double time = static_cast<double>(frame) / 48000.0;
            const double level = 0.2 + 0.12 * lane;
            left[frame] = static_cast<float>(level * std::sin(
                2.0 * pi * (110.0 + 37.0 * lane) * time));
            right[frame] = static_cast<float>(0.8 * level * std::sin(
                2.0 * pi * (151.0 + 41.0 * lane) * time + 0.2));
        }
        append(left.data(), left.size() * sizeof(float));
        append(right.data(), right.size() * sizeof(float));
    }
    return input;
}

struct Events {
    std::vector<std::vector<uint8_t>> storage;
    clap_input_events_t interface {
        this,
        [](const clap_input_events_t* events) -> uint32_t {
            const auto* self = static_cast<const Events*>(events->ctx);
            return self ? static_cast<uint32_t>(self->storage.size()) : 0u;
        },
        [](const clap_input_events_t* events, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const Events*>(events->ctx);
            return self && index < self->storage.size()
                ? reinterpret_cast<const clap_event_header_t*>(
                    self->storage[index].data()) : nullptr;
        },
    };

    template <typename Event>
    void add(const Event& event)
    {
        storage.emplace_back(sizeof(Event));
        std::memcpy(storage.back().data(), &event, sizeof(Event));
    }

    void note(int16_t key)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.note_id = key;
        event.velocity = 1.0;
        add(event);
    }

    void parameter(clap_id id, double value)
    {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        add(event);
    }
};

template <std::size_t Channels>
bool processBlock(const clap_plugin_t* plugin,
    const clap_input_events_t* events,
    std::array<std::vector<float>, Channels>& storage)
{
    std::array<float*, Channels> channels {};
    for (std::size_t channel = 0u; channel < storage.size(); ++channel) {
        storage[channel].assign(256u, 0.0f);
        channels[channel] = storage[channel].data();
    }
    clap_audio_buffer_t output {};
    output.data32 = channels.data();
    output.channel_count = static_cast<uint32_t>(channels.size());
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = 256u;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    clap_event_transport_t transport {};
    transport.header.size = sizeof(transport);
    transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    transport.header.type = CLAP_EVENT_TRANSPORT;
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO;
    transport.tempo = 132.0;
    process.transport = &transport;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

float peak(const std::vector<float>& samples)
{
    float result = 0.0f;
    for (float sample : samples) result = std::max(result, std::abs(sample));
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_cutups_clap_smoke <plugin>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) return 1;
    void* handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    const auto* entry = handle ? static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry")) : nullptr;
    bool ok = entry && clap_version_is_compatible(entry->clap_version)
        && entry->init(binary.c_str());
    const auto checkpoint = [&ok](const char* stage) {
        if (!ok) std::cerr << "sample cutups CLAP smoke stage: "
            << stage << '\n';
    };
    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 2u;
    const auto* stereo = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    const auto* wide = factory
        ? factory->get_plugin_descriptor(factory, 1u) : nullptr;
    ok = ok && stereo && wide
        && std::strcmp(stereo->id, "org.s3g.s3g-dsp.sample-cutups") == 0
        && std::strcmp(stereo->name, "s3g Sample Cutups 2") == 0
        && std::strcmp(wide->id, "org.s3g.s3g-dsp.sample-cutups-32") == 0
        && std::strcmp(wide->name, "s3g Sample Cutups 32") == 0;
    checkpoint("descriptors");

    clap_host_t host {
        CLAP_VERSION_INIT, nullptr, "s3g smoke", "s3g", "", "1",
        hostGetExtension, hostRequest, hostRequest, hostRequest,
    };
    const clap_plugin_t* plugin = stereo
        ? factory->create_plugin(factory, &host, stereo->id) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = plugin ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = plugin ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* ports = plugin ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    clap_audio_port_info_t port {};
    ok = ok && params && state && ports
        && params->count(plugin) == kStereoParamCount
        && ports->get(plugin, 0u, false, &port) && port.channel_count == 2u;
    const std::array<const char*, 12u> names {{
        "Out", "Cut Clock", "Division", "Free Rate", "Start", "End",
        "Join", "File Order", "Source Order", "Regions", "Region Count",
        "Pattern Length",
    }};
    for (uint32_t index = 0u; ok && index < names.size(); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info)
            && std::strcmp(info.name, names[index]) == 0;
    }
    char text[64] {};
    double parsed = -1.0;
    ok = ok && params->value_to_text(plugin, 3u, 4.0, text, sizeof(text))
        && std::strcmp(text, "1/16") == 0
        && params->value_to_text(plugin, 10u, 1.0, text, sizeof(text))
        && std::strcmp(text, "Transient") == 0
        && params->text_to_value(plugin, 8u, "Random Cycle", &parsed)
        && parsed == 4.0;
#if defined(__APPLE__)
    ok = ok && plugin->get_extension(plugin, CLAP_EXT_GUI) != nullptr;
#endif
    checkpoint("ports, parameters, text and GUI");

    auto fixture = embeddedFixture();
    ok = ok && state->load(plugin, &fixture.stream);
    double bpm = 0.0;
    ok = ok && params->get_value(plugin, 30u, &bpm)
        && std::abs(bpm - 140.0) < 1.0e-9;
    MemoryOutput saved;
    ok = ok && state->save(plugin, &saved.stream)
        && saved.bytes.size() == fixture.bytes.size();
    if (ok) {
        SavedState roundTrip;
        std::memcpy(&roundTrip, saved.bytes.data(), sizeof(roundTrip));
        ok = roundTrip.manualPath.pointCount == 16u
            && std::abs(roundTrip.manualPath.points[1u].phase - 5.0f / 16.0f)
                < 1.0e-6f
            && std::abs(roundTrip.manualPath.points[3u].lane - 1.0f)
                < 1.0e-6f;
    }
    checkpoint("state and manual pattern round trip");

    ok = ok && plugin->activate(plugin, 48000.0, 32u, 256u)
        && plugin->start_processing(plugin);
    Events events;
    events.note(60);
    std::array<std::vector<float>, 2u> stereoAudio;
    ok = ok && processBlock(plugin, &events.interface, stereoAudio)
        && peak(stereoAudio[0u]) > 1.0e-5f
        && peak(stereoAudio[1u]) > 1.0e-5f;
    checkpoint("stereo cut render");
    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    const clap_plugin_t* widePlugin = ok && wide
        ? factory->create_plugin(factory, &host, wide->id) : nullptr;
    ok = ok && widePlugin && widePlugin->init(widePlugin);
    const auto* wideState = widePlugin ? static_cast<const clap_plugin_state_t*>(
        widePlugin->get_extension(widePlugin, CLAP_EXT_STATE)) : nullptr;
    auto wideFixture = embeddedFixture();
    ok = ok && wideState && wideState->load(widePlugin, &wideFixture.stream)
        && widePlugin->activate(widePlugin, 48000.0, 32u, 256u)
        && widePlugin->start_processing(widePlugin);
    Events wideEvents;
    wideEvents.parameter(40u, 1.0);
    wideEvents.parameter(41u, 16.0);
    wideEvents.parameter(42u, 0.0);
    for (int key = 60; key < 68; ++key) wideEvents.note(key);
    std::array<std::vector<float>, 32u> wideAudio;
    ok = ok && processBlock(widePlugin, &wideEvents.interface, wideAudio);
    for (std::size_t channel = 0u; ok && channel < 16u; ++channel)
        ok = peak(wideAudio[channel]) > 1.0e-7f;
    for (std::size_t channel = 16u; ok && channel < wideAudio.size(); ++channel)
        ok = peak(wideAudio[channel]) == 0.0f;
    checkpoint("32-channel allocation");
    if (widePlugin) {
        widePlugin->stop_processing(widePlugin);
        widePlugin->deactivate(widePlugin);
        widePlugin->destroy(widePlugin);
    }
    if (entry) entry->deinit();
    if (handle) dlclose(handle);
    if (!ok) {
        std::cerr << "sample cutups CLAP smoke failed\n";
        return 1;
    }
    std::cout << "sample cutups CLAP smoke passed\n";
    return 0;
}
