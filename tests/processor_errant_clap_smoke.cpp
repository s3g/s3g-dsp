#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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

constexpr const char* kPluginId = "org.s3g.s3g-dsp.processor-errant";
constexpr uint32_t kFrames = 256u;
constexpr uint32_t kChannels = 2u;

struct Host {
    clap_host_t host {};
    clap_host_params_t params {};
    uint32_t rescans = 0u;
};

Host* context(const clap_host_t* host)
{
    return static_cast<Host*>(host->host_data);
}

const void* hostGetExtension(const clap_host_t* host, const char* id)
{
    return id && std::strcmp(id, CLAP_EXT_PARAMS) == 0
        ? &context(host)->params : nullptr;
}

void hostRescan(const clap_host_t* host, clap_param_rescan_flags flags)
{
    if ((flags & CLAP_PARAM_RESCAN_VALUES) != 0u) ++context(host)->rescans;
}

void noRestart(const clap_host_t*) {}
void noProcess(const clap_host_t*) {}
void noCallback(const clap_host_t*) {}
void noClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void noFlush(const clap_host_t*) {}

struct Events {
    std::array<clap_event_param_value_t, 24u> params {};
    std::array<clap_event_note_t, 8u> notes {};
    std::array<const clap_event_header_t*, 32u> ordered {};
    uint32_t paramCount = 0u;
    uint32_t noteCount = 0u;
    uint32_t count = 0u;
    clap_input_events_t input {
        this,
        [](const clap_input_events_t* list) -> uint32_t {
            return static_cast<const Events*>(list->ctx)->count;
        },
        [](const clap_input_events_t* list, uint32_t index)
            -> const clap_event_header_t* {
            const auto* events = static_cast<const Events*>(list->ctx);
            return index < events->count ? events->ordered[index] : nullptr;
        },
    };

    bool insert(const clap_event_header_t* event)
    {
        if (!event || count >= ordered.size()) return false;
        uint32_t index = count;
        while (index > 0u && ordered[index - 1u]->time > event->time) {
            ordered[index] = ordered[index - 1u];
            --index;
        }
        ordered[index] = event;
        ++count;
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
        event.note_id = event.port_index = event.channel = event.key = -1;
        event.value = value;
        return insert(&event.header);
    }

    bool addNote(uint16_t type, int16_t key, double velocity,
        uint32_t time = 0u, int16_t channel = 0, int32_t noteId = 42)
    {
        if (noteCount >= notes.size()) return false;
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = type;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.note_id = noteId;
        event.port_index = 0;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        return insert(&event.header);
    }
};

struct Audio {
    std::array<std::array<float, kFrames>, kChannels> storage {};
    std::array<float*, kChannels> pointers {};
    clap_audio_buffer_t output {};

    Audio()
    {
        for (uint32_t channel = 0u; channel < kChannels; ++channel) {
            pointers[channel] = storage[channel].data();
        }
        output.data32 = pointers.data();
        output.channel_count = kChannels;
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
    const size_t count = std::min<size_t>(requested, 7u);
    const auto* bytes = static_cast<const uint8_t*>(source);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}

int64_t stateRead(const clap_istream_t* stream,
    void* destination, uint64_t requested)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    if (!state || (!destination && requested > 0u)) return -1;
    const size_t available = state->offset < state->bytes.size()
        ? state->bytes.size() - state->offset : 0u;
    const size_t count = std::min<size_t>({
        available, static_cast<size_t>(requested), 5u });
    if (count > 0u) {
        std::memcpy(destination, state->bytes.data() + state->offset, count);
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
        for (std::filesystem::directory_iterator it(directory, error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error) && !error) return it->path();
        }
    }
#endif
    return {};
}

clap_process_status processBlock(const clap_plugin_t* plugin,
    Audio& audio, const clap_input_events_t* events = nullptr)
{
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = kFrames;
    process.audio_outputs = &audio.output;
    process.audio_outputs_count = 1u;
    process.in_events = events;
    return plugin->process(plugin, &process);
}

double energy(const Audio& audio)
{
    double result = 0.0;
    for (const auto& channel : audio.storage) {
        for (float value : channel) {
            if (!std::isfinite(value) || std::fabs(value) > 1.001f) {
                return -1.0;
            }
            result += static_cast<double>(value) * value;
        }
    }
    return result;
}

bool flush(const clap_plugin_t* plugin, const clap_plugin_params_t* params,
    std::initializer_list<std::pair<clap_id, double>> values)
{
    Events events;
    for (const auto& value : values) {
        if (!events.addParam(value.first, value.second)) return false;
    }
    params->flush(plugin, &events.input, nullptr);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_processor_errant_clap_smoke <bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    if (binary.empty()) {
        std::cerr << "could not resolve Processor Errant binary\n";
        return 1;
    }
    void* library = dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "could not load Processor Errant: " << dlerror() << '\n';
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry"));
    if (!entry || !entry->init(binary.c_str())) {
        std::cerr << "invalid Processor Errant CLAP entry\n";
        dlclose(library);
        return 1;
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) != 1u) {
        std::cerr << "invalid Processor Errant factory\n";
        return 1;
    }

    Host host;
    host.params.rescan = hostRescan;
    host.params.clear = noClear;
    host.params.request_flush = noFlush;
    host.host.clap_version = CLAP_VERSION_INIT;
    host.host.host_data = &host;
    host.host.name = "Processor Errant smoke";
    host.host.vendor = "s3g";
    host.host.url = "https://github.com/s3g/s3g-dsp";
    host.host.version = "1";
    host.host.get_extension = hostGetExtension;
    host.host.request_restart = noRestart;
    host.host.request_process = noProcess;
    host.host.request_callback = noCallback;

    const clap_plugin_t* plugin = factory->create_plugin(
        factory, &host.host, kPluginId);
    if (!plugin || !plugin->init(plugin)) {
        std::cerr << "could not create Processor Errant\n";
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
    clap_audio_port_info_t audioInfo {};
    clap_note_port_info_t noteInfo {};
    if (!audioPorts || audioPorts->count(plugin, true) != 0u
        || audioPorts->count(plugin, false) != 1u
        || !audioPorts->get(plugin, 0u, false, &audioInfo)
        || audioInfo.channel_count != 2u
        || !notePorts || notePorts->count(plugin, true) != 1u
        || !notePorts->get(plugin, 0u, true, &noteInfo)
        || !params || params->count(plugin) != 24u || !state) {
        std::cerr << "Processor Errant extension contract failed\n";
        return 1;
    }
    const std::array<std::pair<clap_id, const char*>, 24u> parameters {{
        { 1u, "Mode" }, { 2u, "Growl" }, { 3u, "Span" },
        { 4u, "Density" }, { 5u, "Ancestry" }, { 6u, "Mutation" },
        { 7u, "Repeat" }, { 8u, "Coherence" }, { 9u, "Register" },
        { 10u, "Pitch Gravity" }, { 11u, "Cutoff" }, { 12u, "Drive" },
        { 13u, "Stereo Topology" }, { 14u, "Width" }, { 15u, "Seed" },
        { 16u, "Velocity Sensitivity" }, { 17u, "Output Gain" },
        { 18u, "MIDI Receive" }, { 20u, "Key Role" }, { 21u, "Sub" },
        { 22u, "Resonance" }, { 23u, "Filter Contour" },
        { 24u, "Crosswire" }, { 19u, "Trigger" }
    }};
    for (uint32_t index = 0u; index < parameters.size(); ++index) {
        clap_param_info_t info {};
        if (!params->get_info(plugin, index, &info)
            || info.id != parameters[index].first
            || std::strcmp(info.name, parameters[index].second) != 0
            || (info.flags & CLAP_PARAM_IS_AUTOMATABLE) == 0u) {
            std::cerr << "Processor Errant parameter contract failed at "
                      << index << '\n';
            return 1;
        }
    }

    if (!plugin->activate(plugin, 48000.0, kFrames, kFrames)
        || !plugin->start_processing(plugin)) {
        std::cerr << "Processor Errant activation failed\n";
        return 1;
    }
    Audio audio;
    Events noteOn;
    noteOn.addNote(CLAP_EVENT_NOTE_ON, 60, 0.9);
    double totalEnergy = 0.0;
    for (uint32_t block = 0u; block < 80u; ++block) {
        audio.clear();
        const auto status = processBlock(plugin, audio,
            block == 0u ? &noteOn.input : nullptr);
        const double blockEnergy = energy(audio);
        if (status == CLAP_PROCESS_ERROR || blockEnergy < 0.0) {
            std::cerr << "Processor Errant produced unsafe audio\n";
            return 1;
        }
        totalEnergy += blockEnergy;
    }
    if (totalEnergy < 1.0) {
        std::cerr << "Processor Errant note input was silent\n";
        return 1;
    }

    plugin->reset(plugin);
    flush(plugin, params, { { 1u, 2.0 }, { 18u, 2.0 } });
    Events rejected;
    rejected.addNote(CLAP_EVENT_NOTE_ON, 64, 1.0, 0u, 0);
    audio.clear();
    processBlock(plugin, audio, &rejected.input);
    if (energy(audio) != 0.0) {
        std::cerr << "Processor Errant MIDI receive filter accepted wrong channel\n";
        return 1;
    }
    Events accepted;
    accepted.addNote(CLAP_EVENT_NOTE_ON, 64, 1.0, 0u, 1, 77);
    audio.clear();
    processBlock(plugin, audio, &accepted.input);
    if (energy(audio) <= 0.0) {
        std::cerr << "Processor Errant MIDI receive filter rejected selected channel\n";
        return 1;
    }
    Events noteOff;
    noteOff.addNote(CLAP_EVENT_NOTE_OFF, 64, 0.0, 0u, 1, 77);
    processBlock(plugin, audio, &noteOff.input);
    clap_process_status finalStatus = CLAP_PROCESS_CONTINUE;
    for (uint32_t block = 0u; block < 500u; ++block) {
        audio.clear();
        finalStatus = processBlock(plugin, audio);
        if (energy(audio) < 0.0) return 1;
    }
    if (finalStatus != CLAP_PROCESS_SLEEP) {
        std::cerr << "Processor Errant Field note-off did not reach sleep\n";
        return 1;
    }

    flush(plugin, params, {
        { 2u, 0.91 }, { 5u, 0.93 }, { 6u, 0.88 },
        { 13u, 3.0 }, { 15u, 54321.0 }, { 17u, -11.0 },
        { 20u, 1.0 }, { 21u, 0.84 }, { 22u, 0.71 },
        { 23u, -0.38 }, { 24u, 0.77 }
    });
    MemoryState memory;
    clap_ostream_t outputState { &memory, stateWrite };
    if (!state->save(plugin, &outputState) || memory.bytes.empty()) {
        std::cerr << "Processor Errant state save failed\n";
        return 1;
    }
    flush(plugin, params, {
        { 2u, 0.1 }, { 5u, 0.1 }, { 6u, 0.1 },
        { 13u, 0.0 }, { 15u, 2.0 }, { 17u, -30.0 },
        { 20u, 2.0 }, { 21u, 0.1 }, { 22u, 0.1 },
        { 23u, 0.1 }, { 24u, 0.1 }
    });
    clap_istream_t inputState { &memory, stateRead };
    if (!state->load(plugin, &inputState)) {
        std::cerr << "Processor Errant state load failed\n";
        return 1;
    }
    const std::array<std::pair<clap_id, double>, 11u> restored {{
        { 2u, 0.91 }, { 5u, 0.93 }, { 6u, 0.88 },
        { 13u, 3.0 }, { 15u, 54321.0 }, { 17u, -11.0 },
        { 20u, 1.0 }, { 21u, 0.84 }, { 22u, 0.71 },
        { 23u, -0.38 }, { 24u, 0.77 }
    }};
    for (const auto& expected : restored) {
        double value = 0.0;
        if (!params->get_value(plugin, expected.first, &value)
            || std::fabs(value - expected.second) > 1.0e-5) {
            std::cerr << "Processor Errant state did not restore parameter "
                      << expected.first << '\n';
            return 1;
        }
    }

    struct LegacyHeader {
        uint32_t magic;
        uint32_t version;
        uint32_t valueCount;
        uint32_t reserved;
    };
    const LegacyHeader legacyHeader { 0x45524753u, 1u, 18u, 0u };
    std::array<double, 18u> legacyValues {{
        1.0, 0.73, 0.46, 0.58, 0.68, 0.42, 0.38, 0.62,
        0.0, 1.0, 0.18, 0.20, 1.0, 0.56, 1979.0, 0.78, -8.0, 0.0
    }};
    MemoryState legacy;
    const auto* headerBytes = reinterpret_cast<const uint8_t*>(&legacyHeader);
    legacy.bytes.insert(legacy.bytes.end(), headerBytes,
        headerBytes + sizeof(legacyHeader));
    const auto* valueBytes = reinterpret_cast<const uint8_t*>(
        legacyValues.data());
    legacy.bytes.insert(legacy.bytes.end(), valueBytes,
        valueBytes + sizeof(legacyValues));
    clap_istream_t legacyStream { &legacy, stateRead };
    double legacyMaterial = 0.0;
    double legacyKeyRole = -1.0;
    double legacySub = 0.0;
    double legacyCrosswire = 0.0;
    if (!state->load(plugin, &legacyStream)
        || !params->get_value(plugin, 2u, &legacyMaterial)
        || !params->get_value(plugin, 20u, &legacyKeyRole)
        || !params->get_value(plugin, 21u, &legacySub)
        || !params->get_value(plugin, 24u, &legacyCrosswire)
        || std::fabs(legacyMaterial - 0.73) > 1.0e-5
        || legacyKeyRole != 0.0 || std::fabs(legacySub - 0.62) > 1.0e-5
        || std::fabs(legacyCrosswire - 0.34) > 1.0e-5) {
        std::cerr << "Processor Errant v0.1 state compatibility failed\n";
        return 1;
    }

    const LegacyHeader versionTwoHeader { 0x45524753u, 2u, 19u, 0u };
    std::array<double, 19u> versionTwoValues {{
        1.0, 0.81, 0.46, 0.58, 0.68, 0.42, 0.38, 0.62,
        0.0, 1.0, 0.18, 0.20, 1.0, 0.56, 1979.0, 0.78, -8.0, 0.0,
        1.0
    }};
    MemoryState versionTwo;
    const auto* versionTwoHeaderBytes = reinterpret_cast<const uint8_t*>(
        &versionTwoHeader);
    versionTwo.bytes.insert(versionTwo.bytes.end(), versionTwoHeaderBytes,
        versionTwoHeaderBytes + sizeof(versionTwoHeader));
    const auto* versionTwoValueBytes = reinterpret_cast<const uint8_t*>(
        versionTwoValues.data());
    versionTwo.bytes.insert(versionTwo.bytes.end(), versionTwoValueBytes,
        versionTwoValueBytes + sizeof(versionTwoValues));
    clap_istream_t versionTwoStream { &versionTwo, stateRead };
    double versionTwoMaterial = 0.0;
    double versionTwoKeyRole = -1.0;
    double versionTwoResonance = 0.0;
    if (!state->load(plugin, &versionTwoStream)
        || !params->get_value(plugin, 2u, &versionTwoMaterial)
        || !params->get_value(plugin, 20u, &versionTwoKeyRole)
        || !params->get_value(plugin, 22u, &versionTwoResonance)
        || std::fabs(versionTwoMaterial - 0.81) > 1.0e-5
        || versionTwoKeyRole != 1.0
        || std::fabs(versionTwoResonance - 0.38) > 1.0e-5) {
        std::cerr << "Processor Errant v0.2 state compatibility failed\n";
        return 1;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    std::cout << "Processor Errant CLAP smoke passed\n";
    return 0;
}
