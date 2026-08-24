#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/audio-ports-config.h>
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

constexpr uint32_t kStateMagic = 0x47533353u;
constexpr uint32_t kStateVersion = 1u;
constexpr std::size_t kParamCount = 59u;
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

struct PathPoint { float phase = 0.0f; float lane = 0.0f; };
struct ManualPathState {
    uint32_t pointCount = 0u;
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
        -6.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.02,
        0.0, 0.0, 0.0, 1.0, 0.0, 0.5, 0.0, 0.0, 0.005,
        0.0, 1.0, 60.0, 0.0, 0.0, 0.0, 0.02, 0.0, 1.0,
        1.0, 0.0,
        1.0, 1.0, 0.0, 1.0, 1.0, 0.0,
        1.0, 1.0, 0.0, 1.0, 1.0, 0.0,
        0.0, 32.0, 0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 24.0, 90.0, 0.0, 0.08, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.5, 16.0, 1.0,
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

MemoryInput embeddedFixture(uint8_t sourceChannels)
{
    SavedState saved;
    for (auto& lane : saved.lanes) lane.channelCount = sourceChannels;
    MemoryInput input;
    const auto append = [&input](const void* data, std::size_t size) {
        const auto* first = static_cast<const uint8_t*>(data);
        input.bytes.insert(input.bytes.end(), first, first + size);
    };
    append(&saved, sizeof(saved));
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t lane = 0u; lane < 4u; ++lane) {
        for (uint8_t channel = 0u; channel < sourceChannels; ++channel) {
            std::vector<float> audio(kFrames);
            for (uint32_t frame = 0u; frame < kFrames; ++frame)
                audio[frame] = static_cast<float>((0.15 + 0.05 * lane
                    + 0.025 * channel) * std::sin(2.0 * pi
                    * (110.0 + 29.0 * channel + 37.0 * lane)
                    * frame / 48000.0));
            append(audio.data(), audio.size() * sizeof(float));
        }
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

    void note(int16_t key, int32_t noteId)
    {
        clap_event_note_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.port_index = 0;
        event.channel = 0;
        event.key = key;
        event.note_id = noteId;
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

float peak(const std::vector<float>& samples)
{
    float result = 0.0f;
    for (float sample : samples) result = std::max(result, std::abs(sample));
    return result;
}

bool processBlock(const clap_plugin_t* plugin, const clap_input_events_t* events,
    uint32_t channelCount, std::vector<std::vector<float>>& storage)
{
    storage.assign(channelCount, std::vector<float>(512u));
    std::vector<float*> channels(channelCount);
    for (uint32_t channel = 0u; channel < channelCount; ++channel)
        channels[channel] = storage[channel].data();
    clap_audio_buffer_t output {};
    output.data32 = channels.data();
    output.channel_count = channelCount;
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = 512u;
    process.audio_outputs = &output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE;
}

bool exercise(const clap_plugin_factory_t* factory,
    const clap_plugin_descriptor_t* descriptor, const clap_host_t& host,
    uint32_t outputChannels, uint8_t sourceChannels)
{
    const clap_plugin_t* plugin = factory->create_plugin(factory, &host,
        descriptor->id);
    bool ok = plugin && plugin->init(plugin);
    const auto* params = plugin ? static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = plugin ? static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE)) : nullptr;
    const auto* ports = plugin ? static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* configs = plugin ? static_cast<
        const clap_plugin_audio_ports_config_t*>(plugin->get_extension(plugin,
            CLAP_EXT_AUDIO_PORTS_CONFIG)) : nullptr;
    const auto* notes = plugin ? static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS)) : nullptr;
    clap_audio_port_info_t port {};
    clap_audio_ports_config_t config {};
    ok = ok && params && state && ports && configs && notes
        && params->count(plugin) == kParamCount
        && ports->get(plugin, 0u, false, &port)
        && port.channel_count == outputChannels
        && configs->count(plugin) == 1u
        && configs->get(plugin, 0u, &config)
        && config.output_port_count == 1u
        && notes->count(plugin, true) == 1u;
    clap_param_info_t info {};
    ok = ok && params->get_info(plugin, 46u, &info)
        && std::strcmp(info.name, "Grain Source") == 0
        && params->get_info(plugin, 55u, &info)
        && std::strcmp(info.name, "Mutate Process") == 0;
    char text[64] {};
    double parsed = -1.0;
    ok = ok && params->value_to_text(plugin, 56u, 4.0, text, sizeof(text))
        && std::strcmp(text, "Doublets") == 0
        && params->text_to_value(plugin, 55u, "Scatter", &parsed)
        && parsed == 1.0;
#if defined(__APPLE__)
    ok = ok && plugin->get_extension(plugin, CLAP_EXT_GUI) != nullptr;
#endif
    auto fixture = embeddedFixture(sourceChannels);
    ok = ok && state->load(plugin, &fixture.stream);
    MemoryOutput saved;
    ok = ok && state->save(plugin, &saved.stream)
        && saved.bytes.size() == fixture.bytes.size();
    ok = ok && plugin->activate(plugin, 48000.0, 32u, 512u)
        && plugin->start_processing(plugin);
    Events events;
    if (outputChannels > 2u) {
        events.parameter(40u, 1.0);
        events.parameter(41u, 16.0);
        events.parameter(44u, 1.0);
        for (int32_t note = 0; note < 8; ++note)
            events.note(static_cast<int16_t>(60 + note), note + 1);
    } else events.note(60, 1);
    std::vector<std::vector<float>> audio;
    ok = ok && processBlock(plugin, &events.interface, outputChannels, audio);
    if (outputChannels == 2u)
        ok = ok && peak(audio[0u]) > 1.0e-6f
            && peak(audio[1u]) > 1.0e-6f;
    else {
        for (uint32_t channel = 0u; ok && channel < 16u; ++channel)
            ok = peak(audio[channel]) > 1.0e-6f;
        for (uint32_t channel = 16u; ok && channel < outputChannels; ++channel)
            ok = peak(audio[channel]) == 0.0f;
    }
    if (plugin) {
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_grains_clap_smoke <plugin>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) return 1;
    void* handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    const auto* entry = handle ? static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry")) : nullptr;
    bool ok = entry && clap_version_is_compatible(entry->clap_version)
        && entry->init(binary.c_str());
    const auto* factory = ok ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok = ok && factory && factory->get_plugin_count(factory) == 2u;
    const auto* stereo = factory
        ? factory->get_plugin_descriptor(factory, 0u) : nullptr;
    const auto* multichannel = factory
        ? factory->get_plugin_descriptor(factory, 1u) : nullptr;
    ok = ok && stereo && multichannel
        && std::strcmp(stereo->id, "org.s3g.s3g-dsp.sample-grains") == 0
        && std::strcmp(stereo->name, "s3g Sample Grains 2") == 0
        && std::strcmp(multichannel->id,
            "org.s3g.s3g-dsp.sample-grains-32") == 0
        && std::strcmp(multichannel->name, "s3g Sample Grains 32") == 0;
    clap_host_t host {
        CLAP_VERSION_INIT, nullptr, "s3g smoke", "s3g", "", "1",
        hostGetExtension, hostRequest, hostRequest, hostRequest,
    };
    if (ok) ok = exercise(factory, stereo, host, 2u, 2u);
    if (ok) ok = exercise(factory, multichannel, host, 32u, 4u);
    if (entry) entry->deinit();
    if (handle) dlclose(handle);
    if (!ok) {
        std::cerr << "sample grains CLAP smoke failed\n";
        return 1;
    }
    std::cout << "sample grains CLAP smoke passed\n";
    return 0;
}
