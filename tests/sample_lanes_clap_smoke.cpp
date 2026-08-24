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

constexpr uint32_t kStateMagic = 0x4c533353u;
constexpr uint32_t kStateVersion = 4u;
constexpr uint32_t kRoutingPreviousStateVersion = 3u;
constexpr uint32_t kPreviousStateVersion = 2u;
constexpr uint32_t kLegacyStateVersion = 1u;
constexpr std::size_t kLegacyParamCount = 27u;
constexpr std::size_t kPreviousParamCount = 39u;
constexpr std::size_t kParamCount = 46u;
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
    uint32_t pointCount = 4u;
    uint32_t reserved = 0u;
    std::array<PathPoint, 16u> points {{
        { 0.0f, 0.5f }, { 0.25f, 0.0f }, { 0.70f, 1.0f },
        { 1.0f, 0.5f },
    }};
};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    uint8_t storageMode = 2u;
    std::array<uint8_t, 3u> reserved {};
    std::array<double, kParamCount> parameters {{
        -6.0, 0.0, 1.0, 2.0, 0.0, 1.0, 0.03,
        0.0, 0.0, 0.0, 1.0, 0.0, 0.5, 0.0, 0.0, 0.005,
        1.0, 1.0, 60.0, 0.0, 0.0, 0.0, 0.02, 0.0, 1.0,
        4312.0, 0.0,
        1.0, 1.0, 0.0, 1.0, 1.0, 0.0,
        1.0, 1.0, 0.0, 1.0, 1.0, 0.0,
        0.0, 32.0, 0.0, 0.0, 1.0, 0.0, 0.0,
    }};
    std::array<LaneState, 4u> lanes {};
    ManualPathState manualPath;
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

MemoryInput embeddedFixtureVersion(uint32_t version,
    std::size_t parameterCount, bool includeManualPath)
{
    SavedState saved;
    saved.version = version;
    saved.parameterCount = static_cast<uint32_t>(parameterCount);
    MemoryInput input;
    const auto append = [&input](const void* data, std::size_t size) {
        const auto* first = static_cast<const uint8_t*>(data);
        input.bytes.insert(input.bytes.end(), first, first + size);
    };
    constexpr std::size_t headerBytes = 16u;
    append(&saved, headerBytes);
    append(saved.parameters.data(), parameterCount * sizeof(double));
    append(saved.lanes.data(), saved.lanes.size() * sizeof(LaneState));
    if (includeManualPath)
        append(&saved.manualPath, sizeof(saved.manualPath));
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t lane = 0u; lane < 4u; ++lane) {
        std::vector<float> left(kFrames);
        std::vector<float> right(kFrames);
        for (uint32_t frame = 0u; frame < kFrames; ++frame) {
            const double time = static_cast<double>(frame) / 48000.0;
            const double level = 0.2 + 0.15 * lane;
            left[frame] = static_cast<float>(level * std::sin(
                2.0 * pi * (110.0 + 37.0 * lane) * time));
            right[frame] = static_cast<float>(0.75 * level * std::sin(
                2.0 * pi * (151.0 + 41.0 * lane) * time + 0.2));
        }
        append(left.data(), left.size() * sizeof(float));
        append(right.data(), right.size() * sizeof(float));
    }
    return input;
}

MemoryInput embeddedFixture()
{ return embeddedFixtureVersion(kStateVersion, kParamCount, true); }

MemoryInput routingPreviousEmbeddedFixture()
{
    return embeddedFixtureVersion(kRoutingPreviousStateVersion,
        kPreviousParamCount, true);
}

MemoryInput previousEmbeddedFixture()
{
    return embeddedFixtureVersion(kPreviousStateVersion,
        kPreviousParamCount, false);
}

MemoryInput legacyEmbeddedFixture()
{
    return embeddedFixtureVersion(kLegacyStateVersion,
        kLegacyParamCount, false);
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

bool processBlock(const clap_plugin_t* plugin, const clap_input_events_t* events,
    std::vector<float>& left, std::vector<float>& right)
{
    left.assign(256u, 0.0f);
    right.assign(256u, 0.0f);
    float* channels[] { left.data(), right.data() };
    clap_audio_buffer_t output {};
    output.data32 = channels;
    output.channel_count = 2u;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = 256u;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool processWideBlock(const clap_plugin_t* plugin,
    const clap_input_events_t* events,
    std::array<std::vector<float>, 32u>& storage)
{
    std::array<float*, 32u> channels {};
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
        std::cerr << "usage: s3g_sample_lanes_clap_smoke <plugin>\n";
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
        if (!ok) std::cerr << "sample lanes CLAP smoke stage: "
            << stage << '\n';
    };
    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 2u;
    const auto* descriptor = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    const auto* multichannelDescriptor = factory
        ? factory->get_plugin_descriptor(factory, 1u) : nullptr;
    ok = ok && descriptor
        && std::strcmp(descriptor->id,
            "org.s3g.s3g-dsp.sample-lanes") == 0
        && std::strcmp(descriptor->name, "s3g Sample Lanes 2") == 0;
    ok = ok && multichannelDescriptor
        && std::strcmp(multichannelDescriptor->id,
            "org.s3g.s3g-dsp.sample-lanes-32") == 0
        && std::strcmp(multichannelDescriptor->name,
            "s3g Sample Lanes 32") == 0;
    checkpoint("descriptor");

    clap_host_t host {
        CLAP_VERSION_INIT, nullptr, "s3g smoke", "s3g", "", "1",
        hostGetExtension, hostRequest, hostRequest, hostRequest,
    };
    const clap_plugin_t* plugin = factory && descriptor
        ? factory->create_plugin(factory, &host, descriptor->id) : nullptr;
    ok = ok && plugin && plugin->init(plugin);
    const auto* params = plugin ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = plugin ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* ports = plugin ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* notes = plugin ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    clap_audio_port_info_t port {};
    ok = ok && params && state && ports && notes
        && params->count(plugin) == kParamCount
        && ports->get(plugin, 0u, false, &port)
        && port.channel_count == 2u
        && notes->count(plugin, true) == 1u;
    checkpoint("ports and parameters");
    const std::array<const char*, 8u> names {{
        "Out", "Transport", "Rate Basis", "Rate", "Start", "End",
        "Loop Crossfade", "Lane Path",
    }};
    for (uint32_t index = 0u; ok && index < names.size(); ++index) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, index, &info)
            && std::strcmp(info.name, names[index]) == 0;
    }
    checkpoint("core parameter names");
    const std::array<const char*, 3u> laneNames {{
        "Lane 1 Speed", "Lane 1 Stretch", "Lane 1 Nudge",
    }};
    for (uint32_t offset = 0u; ok && offset < laneNames.size(); ++offset) {
        clap_param_info_t info {};
        ok = params->get_info(plugin, 27u + offset, &info)
            && std::strcmp(info.name, laneNames[offset]) == 0;
    }
    checkpoint("lane parameter names");
    char text[64] {};
    double parsed = -1.0;
    ok = ok && params->value_to_text(plugin, 8u, 4.0, text, sizeof(text))
        && std::strcmp(text, "Steps Down") == 0
        && params->text_to_value(plugin, 10u, "Plateau", &parsed)
        && parsed == 3.0
        && params->value_to_text(plugin, 28u, 2.0, text, sizeof(text))
        && std::strcmp(text, "2.00 x") == 0
        && params->text_to_value(plugin, 30u, "-25%", &parsed)
        && std::abs(parsed + 0.25) < 1.0e-9;
    checkpoint("parameter text");
#if defined(__APPLE__)
    ok = ok && plugin->get_extension(plugin, CLAP_EXT_GUI) != nullptr;
#endif

    auto fixture = embeddedFixture();
    ok = ok && state->load(plugin, &fixture.stream);
    checkpoint("state load");
    if (ok) {
        for (clap_id id = 28u; id <= 39u; ++id) {
            double value = -99.0;
            if (!params->get_value(plugin, id, &value)) ok = false;
            if (!(value >= (id % 3u == 0u ? -0.5 : 0.25))) {
                std::cerr << "unexpected lane parameter " << id
                    << " = " << value << '\n';
                ok = false;
            }
        }
    }
    MemoryOutput saved;
    ok = ok && state->save(plugin, &saved.stream)
        && saved.bytes.size() == fixture.bytes.size();
    SavedState roundTrip;
    if (ok) {
        std::memcpy(&roundTrip, saved.bytes.data(), sizeof(roundTrip));
        ok = roundTrip.manualPath.pointCount == 4u
            && std::abs(roundTrip.manualPath.points[1u].phase - 0.25f)
                < 1.0e-6f
            && std::abs(roundTrip.manualPath.points[2u].lane - 1.0f)
                < 1.0e-6f;
    }
    checkpoint("state save");
    auto routingPrevious = routingPreviousEmbeddedFixture();
    ok = ok && state->load(plugin, &routingPrevious.stream);
    checkpoint("v3 state migration");
    auto previous = previousEmbeddedFixture();
    ok = ok && state->load(plugin, &previous.stream);
    checkpoint("v2 state migration");
    auto legacy = legacyEmbeddedFixture();
    ok = ok && state->load(plugin, &legacy.stream);
    for (clap_id id = 28u; ok && id <= 39u; ++id) {
        double value = -99.0;
        ok = params->get_value(plugin, id, &value)
            && std::abs(value - (id % 3u == 0u ? 0.0 : 1.0)) < 1.0e-9;
    }
    checkpoint("legacy state migration");
    ok = ok && plugin->activate(plugin, 48000.0, 32u, 256u)
        && plugin->start_processing(plugin);
    Events events;
    events.note(60);
    std::vector<float> left;
    std::vector<float> right;
    const bool rendered = ok
        && processBlock(plugin, &events.interface, left, right);
    const float leftPeak = peak(left);
    const float rightPeak = peak(right);
    ok = rendered && leftPeak > 1.0e-5f && rightPeak > 1.0e-5f;
    if (!ok) std::cerr << "rendered=" << rendered
        << " left=" << leftPeak << " right=" << rightPeak << '\n';
    checkpoint("audio render");
    Events jumps;
    jumps.parameter(8u, 6.0);
    jumps.parameter(9u, 1.0);
    jumps.parameter(11u, 8.0);
    jumps.parameter(28u, 1.25);
    jumps.parameter(29u, 2.0);
    jumps.parameter(30u, -0.25);
    ok = ok && processBlock(plugin, &jumps.interface, left, right)
        && peak(left) > 1.0e-5f;

    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    const clap_plugin_t* widePlugin = ok && multichannelDescriptor
        ? factory->create_plugin(factory, &host, multichannelDescriptor->id)
        : nullptr;
    ok = ok && widePlugin && widePlugin->init(widePlugin);
    const auto* widePorts = widePlugin ? static_cast<
        const clap_plugin_audio_ports_t*>(widePlugin->get_extension(
            widePlugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* wideState = widePlugin ? static_cast<
        const clap_plugin_state_t*>(widePlugin->get_extension(
            widePlugin, CLAP_EXT_STATE)) : nullptr;
    clap_audio_port_info_t widePort {};
    ok = ok && widePorts && wideState
        && widePorts->get(widePlugin, 0u, false, &widePort)
        && widePort.channel_count == 32u;
    auto wideFixture = embeddedFixture();
    ok = ok && wideState->load(widePlugin, &wideFixture.stream)
        && widePlugin->activate(widePlugin, 48000.0, 32u, 256u)
        && widePlugin->start_processing(widePlugin);
    Events wideEvents;
    wideEvents.parameter(17u, 0.0);
    wideEvents.parameter(40u, 1.0);
    wideEvents.parameter(41u, 16.0);
    for (int key = 60; key < 68; ++key) wideEvents.note(key);
    std::array<std::vector<float>, 32u> wideAudio;
    ok = ok && processWideBlock(widePlugin, &wideEvents.interface,
        wideAudio);
    for (std::size_t channel = 0u; ok && channel < 16u; ++channel)
        ok = peak(wideAudio[channel]) > 1.0e-6f;
    for (std::size_t channel = 16u; ok && channel < wideAudio.size();
         ++channel)
        ok = peak(wideAudio[channel]) == 0.0f;
    checkpoint("32-channel descriptor and render");
    if (widePlugin) {
        widePlugin->stop_processing(widePlugin);
        widePlugin->deactivate(widePlugin);
        widePlugin->destroy(widePlugin);
    }
    if (entry) entry->deinit();
    if (handle) dlclose(handle);
    if (!ok) {
        std::cerr << "sample lanes CLAP smoke failed\n";
        return 1;
    }
    std::cout << "sample lanes CLAP smoke passed\n";
    return 0;
}
