#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include "../plugins/common/s3g_sample_storage.h"
#include "realtime_alloc_probe_api.h"
#include "../dsp/s3g_sample_neon_family.h"
#include "../dsp/s3g_sample_neon_character.h"
#include "../dsp/s3g_neon_midi_bridge.h"
#include "../dsp/s3g_neon_note_map.h"
#include "../dsp/s3g_reloop_neon.h"
#include <clap/ext/note-name.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
uint64_t allocationProbeBlocks = 0u;

constexpr uint32_t kStateMagic = 0x4e533353u;
constexpr uint32_t kStateVersion = 14u;
#if defined(__APPLE__)
constexpr bool kHostLedOutput = false;
#else
constexpr bool kHostLedOutput = true;
#endif
constexpr std::size_t kModernStateBytes = (32u * 36u + 64u) * sizeof(float);
constexpr std::size_t kParameterCount = 7u + 32u * 19u;
constexpr std::size_t kPathBytes = 2048u;
constexpr std::size_t kSliceModeBytes = 32u * 32u;
constexpr std::size_t kSliceCountBytes = 32u;
constexpr std::size_t kSliceOptionBytes = 32u * 32u;
constexpr std::size_t kPerformanceStateBytes = 4u * 32u
    + 32u * sizeof(double)
    + 32u * 33u * sizeof(double)
    + 32u * 8u * sizeof(double) + 32u * 8u
    + 2u * 32u * 8u * sizeof(double) + 32u * 8u
    + 32u * sizeof(double)
    + 8u * 16u + 8u * 16u + 8u + 4u;
constexpr std::size_t kSlicerDomainStateBytes = 2u * 32u;
constexpr std::size_t kTextureStateBytes = 2u * 32u
    + 11u * 32u * sizeof(float);
constexpr std::size_t kCaptureStateBytes = 5u * sizeof(double) + 33u * 16u;
constexpr std::size_t kTransientStateBytes = 32u * sizeof(float) + 32u;
constexpr std::size_t kPlaybackStateBytes = 32u + 3u * 32u * sizeof(float);
constexpr uint8_t kMotionOption = 1u << 0u;
constexpr uint8_t kLanesOption = 1u << 1u;
constexpr uint8_t kGrainsOption = 1u << 2u;

struct StateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParameterCount);
    uint32_t pathBytes = static_cast<uint32_t>(kPathBytes);
};

constexpr std::size_t kPerformanceStateOffset = sizeof(StateHeader)
    + kParameterCount * sizeof(double) + 32u * kPathBytes
    + kSliceModeBytes + kSliceCountBytes + kSliceOptionBytes;
constexpr std::size_t kEditPositionOffset = kPerformanceStateOffset
    + 4u * 32u + 32u * sizeof(double)
    + 32u * 33u * sizeof(double)
    + 32u * 8u * sizeof(double) + 32u * 8u
    + 2u * 32u * 8u * sizeof(double) + 32u * 8u;

struct StateBuffer {
    clap_ostream_t output {};
    clap_istream_t input {};
    std::vector<uint8_t> bytes;
    std::size_t cursor = 0u;

    StateBuffer()
    {
        output.ctx = this;
        output.write = [](const clap_ostream_t* stream, const void* source,
                           uint64_t count) -> int64_t {
            auto* self = static_cast<StateBuffer*>(stream->ctx);
            if (!self || !source) return -1;
            const auto* first = static_cast<const uint8_t*>(source);
            self->bytes.insert(self->bytes.end(), first, first + count);
            return static_cast<int64_t>(count);
        };
        input.ctx = this;
        input.read = [](const clap_istream_t* stream, void* destination,
                        uint64_t count) -> int64_t {
            auto* self = static_cast<StateBuffer*>(stream->ctx);
            if (!self || !destination || self->cursor > self->bytes.size())
                return -1;
            const std::size_t available = self->bytes.size() - self->cursor;
            const std::size_t amount = std::min<std::size_t>(
                static_cast<std::size_t>(count), available);
            if (amount == 0u) return 0;
            std::memcpy(destination, self->bytes.data() + self->cursor,
                amount);
            self->cursor += amount;
            return static_cast<int64_t>(amount);
        };
    }
};

struct MidiInput {
    std::array<clap_event_midi_t, 4u> events {};
    clap_input_events_t list {};

    MidiInput()
    {
        events[0u].header.size = sizeof(clap_event_midi_t);
        events[0u].header.time = 0u;
        events[0u].header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        events[0u].header.type = CLAP_EVENT_MIDI;
        events[0u].port_index = 0u;
        events[0u].data[0u] = 0x93u;
        events[0u].data[1u] = 0x05u; // Explicit PLAY before the performance pad.
        events[0u].data[2u] = 0x7fu;
        events[1u].header.size = sizeof(clap_event_midi_t);
        events[1u].header.time = 1u;
        events[1u].header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        events[1u].header.type = CLAP_EVENT_MIDI;
        events[1u].port_index = 0u;
        events[1u].data[0u] = 0x97u;
        events[1u].data[1u] = 0x00u;
        events[1u].data[2u] = 0x7fu;
        list.ctx = this;
        list.size = [](const clap_input_events_t*) { return 2u; };
        list.get = [](const clap_input_events_t* events, uint32_t index)
            -> const clap_event_header_t* {
            const auto* self = static_cast<const MidiInput*>(events->ctx);
            return index < self->events.size()
                ? &self->events[index].header : nullptr;
        };
    }

    // These DSP fixtures perform deliberately chosen page-specific actions.
    // Model an explicit mode-button press when entering that page; pad notes
    // alone no longer select it. Raw stale-address regressions bypass this
    // helper (history smoke / Utility chain). No extra audio block is added.
    void performanceGesture(uint8_t status, uint8_t note, uint8_t velocity, int& page) {
        namespace neon=s3g::controller::reloop_neon;
        const auto a=neon::decode({status,note,velocity});
        const int next=static_cast<int>(a.mode)+4*static_cast<int>(a.layer);
        bool select=((a.type==neon::ActionType::Pad && a.pressed)
            || a.type==neon::ActionType::PadVelocity) && next!=page;
        if(select || (a.type==neon::ActionType::SelectMode && a.pressed)) page=next;
        events[0].data[0]=status;events[0].data[1]=note;events[0].data[2]=velocity;
        if(select) {
            events[1]=events[0];
            const auto mode=neon::modeLedMessage(0,a.mode,a.layer);
            events[0].data[0]=mode.status;events[0].data[1]=mode.data1;events[0].data[2]=mode.data2;
            list.size=[](const clap_input_events_t*)->uint32_t{return 2;};
        } else list.size=[](const clap_input_events_t*)->uint32_t{return 1;};
    }
};

struct OutputEvents {
    clap_output_events_t list {};
    uint32_t midiCount = 0u;
    uint32_t largePadCount = 0u;
    uint32_t sysexCount = 0u;
    uint32_t paramCount = 0u;
    std::array<uint8_t, 8u> samplerPads {};
    bool captureMidi = false;
    std::vector<clap_event_midi_t> messages;

    OutputEvents()
    {
        list.ctx = this;
        list.try_push = [](const clap_output_events_t* events,
                            const clap_event_header_t* header) {
            auto* self = static_cast<OutputEvents*>(events->ctx);
            if (!self || !header) return false;
            if (header->type == CLAP_EVENT_MIDI) {
                ++self->midiCount;
                const auto* midi = reinterpret_cast<
                    const clap_event_midi_t*>(header);
                if (self->captureMidi) self->messages.push_back(*midi);
                if (midi->data[0u] == 0x97u && midi->data[1u] < 8u)
                    self->samplerPads[midi->data[1u]] = midi->data[2u];
                if ((midi->data[0u] & 0xf0u) == 0x90u
                    && (midi->data[0u] & 0x0fu) >= 7u
                    && (midi->data[0u] & 0x0fu) <= 10u)
                    ++self->largePadCount;
            }
            else if (header->type == CLAP_EVENT_MIDI_SYSEX)
                ++self->sysexCount;
            else if (header->type == CLAP_EVENT_PARAM_VALUE)
                ++self->paramCount;
            return true;
        };
    }
};

struct ProjectSimulation {
    bool enabled = false;
    std::string directory;
    unsigned additions = 0u, removals = 0u;
} projectSimulation;
void* simulatedContext(const clap_host_t*, int selector) {
    return selector == 3 ? reinterpret_cast<void*>(uintptr_t{1}) : selector == 4 ? reinterpret_cast<void*>(uintptr_t{2}) : nullptr;
}
void* simulatedProjects(int index, char* path, int size) {
    if (index != 0) return nullptr;
    std::snprintf(path, static_cast<size_t>(size), "%s/session.rpp", projectSimulation.directory.c_str());
    return reinterpret_cast<void*>(uintptr_t{1});
}
void simulatedMedia(void*, char* path, int size) {
    std::snprintf(path, static_cast<size_t>(size), "%s", projectSimulation.directory.c_str());
}
int simulatedRegistration(const char* name, void*) {
    if (std::strcmp(name, "file_in_project_ex2") == 0) ++projectSimulation.additions;
    if (std::strcmp(name, "-file_in_project_ex2") == 0) ++projectSimulation.removals;
    return 1;
}
void* simulatedFunction(const char* name) {
    if (std::strcmp(name, "clap_get_reaper_context") == 0) return reinterpret_cast<void*>(&simulatedContext);
    if (std::strcmp(name, "EnumProjects") == 0) return reinterpret_cast<void*>(&simulatedProjects);
    if (std::strcmp(name, "GetProjectPathEx") == 0) return reinterpret_cast<void*>(&simulatedMedia);
    return nullptr;
}
s3g::sample_storage::ReaperHostBridge simulatedBridge {0, nullptr, simulatedRegistration, simulatedFunction};

struct HostContext {
    clap_host_t host {};

    HostContext()
    {
        host.clap_version = CLAP_VERSION_INIT;
        host.name = "Sample Neon smoke";
        host.vendor = "s3g";
        host.url = "https://github.com/s3g/s3g-dsp";
        host.version = "1";
        host.get_extension = [](const clap_host_t*, const char* name)
            -> const void* { return projectSimulation.enabled && std::strcmp(name, "cockos.reaper_extension") == 0 ? &simulatedBridge : nullptr; };
        host.request_restart = [](const clap_host_t*) {};
        host.request_process = [](const clap_host_t*) {};
        host.request_callback = [](const clap_host_t*) {};
    }
};

std::filesystem::path resolveBinary(const std::filesystem::path& supplied)
{
    if (std::filesystem::is_regular_file(supplied)) return supplied;
    const auto macOS = supplied / "Contents" / "MacOS";
    if (std::filesystem::is_directory(macOS)) {
        for (const auto& entry : std::filesystem::directory_iterator(macOS))
            if (entry.is_regular_file()) return entry.path();
    }
    return {};
}

bool expect(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "Sample Neon CLAP: " << message << '\n';
    return false;
}

void writeU16(std::ofstream& output, uint16_t value)
{
    const std::array<char, 2u> bytes {{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
    }};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ofstream& output, uint32_t value)
{
    const std::array<char, 4u> bytes {{
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu),
    }};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool writeFixtureWave(const std::filesystem::path& path, uint32_t channels = 2u, bool ramp = false)
{
    constexpr uint32_t frames = 4096u;
    constexpr uint32_t bytesPerSample = 2u;
    const uint32_t dataBytes = frames * channels * bytesPerSample;
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output.write("RIFF", 4);
    writeU32(output, 36u + dataBytes);
    output.write("WAVEfmt ", 8);
    writeU32(output, 16u);
    writeU16(output, 1u);
    writeU16(output, static_cast<uint16_t>(channels));
    writeU32(output, 48000u);
    writeU32(output, 48000u * channels * bytesPerSample);
    writeU16(output, static_cast<uint16_t>(channels * bytesPerSample));
    writeU16(output, 16u);
    output.write("data", 4);
    writeU32(output, dataBytes);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        for (uint32_t channel = 0u; channel < channels; ++channel)
            writeU16(output, static_cast<uint16_t>((ramp ? 256u + frame % 1024u : 1024u) * (channel + 1u)));
    }
    return static_cast<bool>(output);
}

void setStateParameter(StateBuffer& state, std::size_t index, double value)
{
    const std::size_t offset = sizeof(StateHeader) + index * sizeof(double);
    if (offset + sizeof(double) <= state.bytes.size())
        std::memcpy(state.bytes.data() + offset, &value, sizeof(value));
}

double stateDouble(const StateBuffer& state, std::size_t offset)
{
    double value = 0.0;
    if (offset + sizeof(value) <= state.bytes.size())
        std::memcpy(&value, state.bytes.data() + offset, sizeof(value));
    return value;
}

struct EmbeddedAudioView {
    uint32_t channels = 0u, frames = 0u;
    double rate = 0.0;
    std::size_t pcm = 0u;
};

EmbeddedAudioView embeddedAudio(const StateBuffer& state, std::size_t emptyStateSize, std::size_t target)
{
    std::array<EmbeddedAudioView, 33u> assets {};
    std::size_t offset = emptyStateSize - assets.size() * 16u;
    for (std::size_t slot = 0u; slot <= target && slot < assets.size(); ++slot) {
        if (offset + 16u > state.bytes.size()) return {};
        auto& asset = assets[slot];
        std::memcpy(&asset.channels, state.bytes.data() + offset, 4u);
        std::memcpy(&asset.frames, state.bytes.data() + offset + 4u, 4u);
        std::memcpy(&asset.rate, state.bytes.data() + offset + 8u, 8u);
        offset += 16u;
        if (asset.channels == UINT32_MAX) {
            if (asset.frames >= slot) return {};
            asset = assets[asset.frames];
        } else {
            asset.pcm = offset;
            const uint64_t bytes = static_cast<uint64_t>(asset.channels) * asset.frames * sizeof(float);
            if (bytes > state.bytes.size() - offset) return {};
            offset += static_cast<std::size_t>(bytes);
        }
        if (slot == target) return asset;
    }
    return {};
}

float maximumMagnitude(const std::vector<float>& values)
{
    float maximum = 0.0f;
    for (float value : values)
        maximum = std::max(maximum, std::abs(value));
    return maximum;
}

template<class Input>
bool processChannels(const clap_plugin_t* plugin, Input& input,
    OutputEvents& output, std::array<std::vector<float>, 32u>& channels,
    clap_audio_buffer_t* trackInput = nullptr)
{
    constexpr uint32_t frames = 64u;
    std::array<float*, 32u> pointers {};
    for (std::size_t ch = 0u; ch < channels.size(); ++ch) {
        channels[ch].assign(frames, 0.0f);
        pointers[ch] = channels[ch].data();
    }
    clap_audio_buffer_t audio {};
    audio.data32 = pointers.data();
    audio.channel_count = 32u;
    clap_process_t process {};
    process.frames_count = frames;
    process.in_events = &input.list;
    process.out_events = &output.list;
    process.audio_outputs = &audio;
    process.audio_outputs_count = 1u;
    process.audio_inputs = trackInput;
    process.audio_inputs_count = trackInput ? 1u : 0u;
    if (!std::getenv("S3G_NEON_ALLOCATION_PROBE"))
        return plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
    static const auto beginProbe = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, "s3g_rt_alloc_probe_begin"));
    static const auto endProbe = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, "s3g_rt_alloc_probe_end"));
    static const auto readProbe = reinterpret_cast<int (*)(s3g_rt_alloc_probe_counts*, size_t)>(dlsym(RTLD_DEFAULT, "s3g_rt_alloc_probe_read"));
    if (!expect(beginProbe && endProbe && readProbe, "requested allocation probe is unavailable")) return false;
    // The mock host's optional MIDI log must not allocate inside try_push().
    if (output.captureMidi) output.messages.reserve(output.messages.size() + 1024u);
    beginProbe();
    const auto status = plugin->process(plugin, &process);
    endProbe();
    s3g_rt_alloc_probe_counts counts {};
    const bool read = readProbe(&counts, sizeof(counts)) == 1
        && counts.abi_version == S3G_RT_ALLOC_PROBE_ABI_VERSION && counts.struct_size == sizeof(counts);
    const uint64_t operations = counts.malloc_calls + counts.calloc_calls + counts.realloc_calls
        + counts.free_calls + counts.posix_memalign_calls + counts.aligned_alloc_calls
        + counts.allocation_failures + counts.invalid_alignment_calls;
    ++allocationProbeBlocks;
    return expect(read && operations == 0u, "Sample Neon allocated/freed in a measured process callback")
        && status != CLAP_PROCESS_ERROR;
}

template<class Input>
bool processStereo(const clap_plugin_t* plugin, Input& input,
    OutputEvents& output, std::vector<float>& left, std::vector<float>& right)
{
    std::array<std::vector<float>, 32u> channels;
    const bool ok = processChannels(plugin, input, output, channels);
    left = channels[0u];
    right = channels[1u];
    return ok && std::all_of(channels.begin() + 2, channels.end(),
        [](const auto& ch) { return maximumMagnitude(ch) == 0.0f; });
}

} // namespace

int main(int argc, char** argv)
{
#if defined(__APPLE__)
    (void)setenv("S3G_SAMPLE_NEON_DISABLE_DIRECT_MIDI", "1", 1);
#endif
    if (argc != 2) {
        std::cerr << "usage: s3g_sample_neon_clap_smoke "
            "<bundle-or-binary>\n";
        return 2;
    }
    const auto binary = resolveBinary(argv[1]);
    bool ok = expect(!binary.empty(), "could not resolve plug-in binary");
    void* library = ok ? dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW)
                       : nullptr;
    if (!library) {
        const char* error = dlerror();
        if (error) std::cerr << "Sample Neon CLAP loader: " << error << '\n';
    }
    ok = expect(library != nullptr, "could not load plug-in") && ok;
    const auto* entry = library ? static_cast<const clap_plugin_entry_t*>(
        dlsym(library, "clap_entry")) : nullptr;
    ok = expect(entry && entry->init(binary.c_str()),
        "entry initialization failed") && ok;
    const auto* factory = entry ? static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    ok = expect(factory && factory->get_plugin_count(factory) == 1u,
        "factory must expose exactly one instrument")
        && ok;
    if (factory) {
        const auto* first = factory->get_plugin_descriptor(factory, 0u);
        const auto* second = factory->get_plugin_descriptor(factory, 1u);
        ok = expect(first && !second
                && std::strcmp(first->id,
                    "org.s3g.s3g-dsp.sample-neon") == 0
                && std::strcmp(first->name, "s3g Sample Neon 32") == 0,
            "descriptor IDs or ordering changed") && ok;
    }

    HostContext host;
    ok = expect(!factory->create_plugin(factory, &host.host,
        "org.s3g.s3g-dsp.sample-neon-stereo"),
        "removed stereo-only descriptor still exists") && ok;
    const clap_plugin_t* instrument = factory ? factory->create_plugin(
        factory, &host.host, "org.s3g.s3g-dsp.sample-neon")
        : nullptr;
    ok = expect(instrument && instrument->init(instrument),
        "instrument instance did not initialize") && ok;
    const auto* ports = instrument ? static_cast<const clap_plugin_audio_ports_t*>(
        instrument->get_extension(instrument, CLAP_EXT_AUDIO_PORTS)) : nullptr;
    const auto* notes = instrument ? static_cast<const clap_plugin_note_ports_t*>(
        instrument->get_extension(instrument, CLAP_EXT_NOTE_PORTS)) : nullptr;
    const auto* names = instrument ? static_cast<const clap_plugin_note_name_t*>(
        instrument->get_extension(instrument, CLAP_EXT_NOTE_NAME)) : nullptr;
    const auto* params = instrument ? static_cast<const clap_plugin_params_t*>(
        instrument->get_extension(instrument, CLAP_EXT_PARAMS)) : nullptr;
    const auto* state = instrument ? static_cast<const clap_plugin_state_t*>(
        instrument->get_extension(instrument, CLAP_EXT_STATE)) : nullptr;
    const auto* gui = instrument ? static_cast<const clap_plugin_gui_t*>(
        instrument->get_extension(instrument, CLAP_EXT_GUI)) : nullptr;
    clap_audio_port_info_t port {};
    ok = expect(ports && notes && names && params && state && gui
            && ports->count(instrument, true) == 1u
            && ports->count(instrument, false) == 1u
            && ports->get(instrument, 0u, false, &port)
            && port.channel_count == 32u
            && notes->count(instrument, true) == 1u
            && notes->count(instrument, false) == (kHostLedOutput ? 1u : 0u)
            && names->count(instrument) == 32u
            && params->count(instrument) == kParameterCount,
        "instrument extensions do not match the public contract") && ok;
    clap_note_port_info_t midiPort {};
    ok = expect(notes->get(instrument, 0u, false, &midiPort) == kHostLedOutput,
        "Note output get/count disagree about direct-only Mac LED routing") && ok;
    for (uint32_t index = 0u; params && index < params->count(instrument); ++index) {
        clap_param_info_t info {};
        if (!params->get_info(instrument, index, &info)) { ok = false; break; }
        for (double input : {info.min_value, info.default_value, info.max_value}) {
            char text[128] {}, repeated[128] {};
            double parsed = 0.0;
            ok = expect(params->value_to_text(instrument, info.id, input, text, sizeof(text))
                && params->text_to_value(instrument, info.id, text, &parsed)
                && params->value_to_text(instrument, info.id, parsed, repeated, sizeof(repeated))
                && std::strcmp(text, repeated) == 0, "Parameter label/unit did not round-trip") && ok;
        }
    }
    double parsed = 0.0;
    ok = expect(params->text_to_value(instrument, 1u, "3OA ACN/SN3D", &parsed) && parsed == 6.0
        && params->text_to_value(instrument, 3u, "50.0%", &parsed) && parsed == .5
        && params->text_to_value(instrument, 1007u, "2.5 kHz", &parsed) && parsed == 2500.0
        && !params->text_to_value(instrument, 1u, "3garbage", &parsed),
        "Named layout, percent or kHz parsing regressed") && ok;

    const auto serial = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto wavePath = std::filesystem::temp_directory_path()
        / ("s3g-sample-neon-smoke-" + std::to_string(serial) + ".wav");
    ok = expect(writeFixtureWave(wavePath),
        "could not write the sample fixture") && ok;

    StateBuffer saved;
    ok = expect(state && state->save(instrument, &saved.output),
        "initial state save failed") && ok;
    const auto emptyProjectState = saved.bytes;
    // Even a pad without audio retains automated trim values in v15.
    StateBuffer emptyEdited; emptyEdited.bytes = emptyProjectState;
    setStateParameter(emptyEdited, 7u + 3u, 0.25);
    setStateParameter(emptyEdited, 7u + 4u, 0.75);
    ok = expect(state->load(instrument, &emptyEdited.input), "empty pad edit recall") && ok;
    double emptyStart = 0.0, emptyEnd = 0.0;
    ok = expect(params->get_value(instrument, 1003u, &emptyStart)
        && params->get_value(instrument, 1004u, &emptyEnd) && emptyStart == 0.25 && emptyEnd == 0.75,
        "v15 empty pad trim was replaced by an absent layer's defaults") && ok;
    const std::size_t expectedStateBytes = sizeof(StateHeader)
        + kParameterCount * sizeof(double) + 32u * kPathBytes
        + kSliceModeBytes + kSliceCountBytes + kSliceOptionBytes
        + kPerformanceStateBytes + kSlicerDomainStateBytes
        + kTextureStateBytes + kTransientStateBytes + kPlaybackStateBytes + kModernStateBytes + kCaptureStateBytes;
    // The new-instance default is PROJECT/version 15. The established tests
    // below deliberately use a version-14 LINK fixture to guard migration.
    ok = expect(saved.bytes.size() > expectedStateBytes && saved.bytes[4] == 15u,
        "new instances must use the extended PROJECT state") && ok;
    saved.bytes.resize(expectedStateBytes);
    saved.bytes[4] = 14u;
    ok = expect(state->load(instrument, &saved.input), "legacy LINK fixture did not migrate") && ok;
    saved.bytes.clear(); saved.cursor = 0u;
    ok = expect(state->save(instrument, &saved.output), "legacy LINK fixture did not save") && ok;
    ok = expect(saved.bytes.size() == expectedStateBytes,
        "saved state size is not stable") && ok;
    if (saved.bytes.size() == expectedStateBytes) {
        const std::size_t preRollOffset = expectedStateBytes - kCaptureStateBytes - kPlaybackStateBytes - kTransientStateBytes - kModernStateBytes;
        const float preRoll = 12.5f;
        std::memcpy(saved.bytes.data() + preRollOffset, &preRoll, sizeof(preRoll));
        const std::size_t playbackOffset = preRollOffset + kTransientStateBytes;
        const std::size_t modernOffset = expectedStateBytes - kCaptureStateBytes - kModernStateBytes;
        const std::size_t envelopeOffset = modernOffset + 32u * 36u * sizeof(float);
        for (unsigned n = 0u; n < 64u; ++n) {
            const float value = 0.001f + static_cast<float>(n) * 0.1f;
            std::memcpy(saved.bytes.data() + envelopeOffset + n * sizeof(float), &value, sizeof(value));
        }
        for (unsigned n = 0u; n < 32u * 36u; ++n) {
            const float value = static_cast<float>(n % 31u) / 30.0f;
            std::memcpy(saved.bytes.data() + modernOffset + n * sizeof(float), &value, sizeof(float));
        }
        // Non-default clock/rate/duration settings survive recall independently per cell.
        saved.bytes[playbackOffset + 31u] = 1u;
        const float cycleSeconds = 1.25f, shotSeconds = 0.125f, intervalBeats = 0.5f;
        std::memcpy(saved.bytes.data() + playbackOffset + 32u, &cycleSeconds, sizeof(float));
        std::memcpy(saved.bytes.data() + playbackOffset + 32u + 32u * sizeof(float), &shotSeconds, sizeof(float));
        std::memcpy(saved.bytes.data() + playbackOffset + 32u + 64u * sizeof(float), &intervalBeats, sizeof(float));
        setStateParameter(saved, 1u, 0.0);  // master gain
        setStateParameter(saved, 4u, 1.0);  // Neon owner
        setStateParameter(saved, 7u, 0.0);  // slot A1 gain
        setStateParameter(saved, 18u, 5.0); // retired slot A1 Character selection
        const std::size_t pathOffset = sizeof(StateHeader)
            + kParameterCount * sizeof(double);
        std::snprintf(reinterpret_cast<char*>(saved.bytes.data() + pathOffset),
            kPathBytes, "%s", wavePath.c_str());
        const std::size_t sliceModeOffset = pathOffset + 32u * kPathBytes;
        saved.bytes[sliceModeOffset] = 2u;
        const std::size_t sliceCountOffset = sliceModeOffset
            + kSliceModeBytes;
        saved.bytes[sliceCountOffset] = 17u;
        // The extended stack state validates slice geometry, so changing its
        // count must also construct a matching, ordered boundary table.
        const std::size_t boundaryOffset = kPerformanceStateOffset
            + 4u * 32u + 32u * sizeof(double);
        for (unsigned n = 0; n <= 32; ++n) {
            const double boundary = std::min(1.0, static_cast<double>(n) / 17.0);
            std::memcpy(saved.bytes.data() + boundaryOffset + n * sizeof(double),
                &boundary, sizeof(boundary));
        }
        const std::size_t sliceOptionOffset = sliceCountOffset
            + kSliceCountBytes;
        saved.bytes[sliceOptionOffset] = 3u;
        saved.cursor = 0u;
        ok = expect(state->load(instrument, &saved.input),
            "state containing a sample path did not load") && ok;

        StateBuffer modeRoundTrip;
        ok = expect(state->save(instrument, &modeRoundTrip.output)
                && modeRoundTrip.bytes.size() == expectedStateBytes
                && modeRoundTrip.bytes[sliceModeOffset] == 2u
                && modeRoundTrip.bytes[sliceCountOffset] == 17u
                && modeRoundTrip.bytes[sliceOptionOffset] == 3u,
            "Slicer trigger/repeat/sync/count did not survive state round-trip")
            && ok;
        ok = expect(std::equal(saved.bytes.begin() + preRollOffset,
                saved.bytes.begin() + preRollOffset + kTransientStateBytes,
                modeRoundTrip.bytes.begin() + preRollOffset),
            "Transient pre-roll and slice limit did not survive state round-trip") && ok;
        ok = expect(std::equal(saved.bytes.begin() + playbackOffset,
            saved.bytes.begin() + playbackOffset + kPlaybackStateBytes,
            modeRoundTrip.bytes.begin() + playbackOffset), "Playback clock/cycle/shot/interval did not round-trip") && ok;
        for (unsigned pad=0;pad<32;++pad) {
            for (unsigned effect=0;effect<8;++effect) for (unsigned n=0;n<3;++n) {
                float value; std::memcpy(&value,modeRoundTrip.bytes.data()+modernOffset+(pad*36+effect*3+n)*sizeof(float),sizeof(float));
                ok = expect(value==s3g::sample::neonCharacterDefaults(effect)[n],"Retired Character settings did not reset to the new defaults") && ok;
            }
            const auto first=modernOffset+(pad*36+24)*sizeof(float);
            ok = expect(std::equal(saved.bytes.begin()+first,saved.bytes.begin()+first+12*sizeof(float),modeRoundTrip.bytes.begin()+first),
                "Playback technique settings changed during Character migration") && ok;
        }
        ok = expect(std::equal(saved.bytes.begin()+envelopeOffset,saved.bytes.begin()+envelopeOffset+64*sizeof(float),modeRoundTrip.bytes.begin()+envelopeOffset),
            "Gesture envelope settings changed during Character migration") && ok;

        // Version 21 retains every effect bank (including hidden settings and
        // trim), not only the selected effect's old three-value prefix.
        clap_event_param_value_t mixEvent {};
        mixEvent.header={sizeof(mixEvent),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
        mixEvent.param_id=1009; mixEvent.value=.25;
        clap_input_events_t mixInput {};
        mixInput.ctx=&mixEvent;
        mixInput.size=[](const clap_input_events_t*)->uint32_t{return 1;};
        mixInput.get=[](const clap_input_events_t* e,uint32_t i)->const clap_event_header_t*{return i?nullptr:&static_cast<const clap_event_param_value_t*>(e->ctx)->header;};
        params->flush(instrument,&mixInput,nullptr);
        StateBuffer expanded;
        ok=expect(state->save(instrument,&expanded.output) && expanded.bytes[4]==21,"Expanded Character bank did not use version 21")&&ok;
        const bool plainFxLoad=state->load(instrument,&expanded.input);
        if(!plainFxLoad)std::cerr<<"FX state load offset="<<expanded.cursor<<" bytes="<<expanded.bytes.size()<<'\n';
        ok=expect(plainFxLoad,"Unmodified expanded Character state failed to load")&&ok;
        expanded.cursor=0;
        constexpr size_t extraFxBytes=32*8*(s3g::sample::kNeonCharacterControls-3)*sizeof(float);
        if(expanded.bytes.size()>extraFxBytes) {
            const size_t extraOffset=expanded.bytes.size()-extraFxBytes;
            for(unsigned pad=0;pad<32;++pad) for(unsigned effect=0;effect<8;++effect)
                for(unsigned n=0;n<s3g::sample::kNeonCharacterControls;++n) {
                    const float value=float((pad+effect+n)%17)/16;
                    const size_t at=n<3?modernOffset+(pad*36+effect*3+n)*sizeof(float)
                        :extraOffset+((pad*8+effect)*(s3g::sample::kNeonCharacterControls-3)+n-3)*sizeof(float);
                    std::memcpy(expanded.bytes.data()+at,&value,sizeof(value));
                }
            const bool loadedFx=state->load(instrument,&expanded.input);
            if(!loadedFx)std::cerr<<"Mutated FX load offset="<<expanded.cursor<<" bytes="<<expanded.bytes.size()<<'\n';
            ok=expect(loadedFx,"Expanded Character banks failed to load")&&ok;
            StateBuffer recalledFx;
            ok=expect(state->save(instrument,&recalledFx.output) && recalledFx.bytes==expanded.bytes,"Expanded Character banks failed exact recall")&&ok;
            StateBuffer truncatedFx;truncatedFx.bytes=expanded.bytes;truncatedFx.bytes.pop_back();
            ok=expect(!state->load(instrument,&truncatedFx.input),"Truncated extended Character bank accepted")&&ok;
            StateBuffer badFx;badFx.bytes=expanded.bytes;
            const float invalid=std::numeric_limits<float>::quiet_NaN();
            std::memcpy(badFx.bytes.data()+extraOffset,&invalid,sizeof(invalid));
            ok=expect(!state->load(instrument,&badFx.input),"Nonfinite extended Character bank accepted")&&ok;
            StateBuffer stableFx;
            ok=expect(state->save(instrument,&stableFx.output)&&stableFx.bytes==expanded.bytes,"Rejected Character state mutated active sound")&&ok;
        }
        modeRoundTrip.cursor=0;
        ok=expect(state->load(instrument,&modeRoundTrip.input),"Could not restore source/playback fixture after Character bank test")&&ok;

        StateBuffer combinedProcess;
        combinedProcess.bytes = saved.bytes;
        const std::size_t textureOffset = expectedStateBytes
            - kTextureStateBytes - kTransientStateBytes - kPlaybackStateBytes - kCaptureStateBytes - kModernStateBytes;
        combinedProcess.bytes[textureOffset] = static_cast<uint8_t>(
            kMotionOption | kLanesOption | kGrainsOption);
        ok = expect(state->load(instrument, &combinedProcess.input),
            "combined legacy process state did not load") && ok;
        StateBuffer combinedRoundTrip;
        ok = expect(state->save(instrument, &combinedRoundTrip.output)
                && combinedRoundTrip.bytes[textureOffset]
                    == kGrainsOption,
            "Legacy combined stages did not commit to a single playback technique")
            && ok;

        for (uint8_t option : {8u, 16u, 32u}) {
            StateBuffer technique; technique.bytes = saved.bytes;
            technique.bytes[textureOffset] = option;
            ok = expect(state->load(instrument, &technique.input), "New playback state did not load") && ok;
            StateBuffer recalled;
            ok = expect(state->save(instrument, &recalled.output) && recalled.bytes[textureOffset] == option,
                "New playback did not survive recall") && ok;
        }
        StateBuffer unsafe; unsafe.bytes = saved.bytes;
        unsafe.bytes[textureOffset] = 32u;
        setStateParameter(unsafe, 23u, 1.0); // Ambisonic cell.
        setStateParameter(unsafe, 18u, 7.0); // Crush, illegal for Ambisonics.
        ok = expect(state->load(instrument, &unsafe.input), "Ambisonic protection fixture did not load") && ok;
        StateBuffer safe;
        double character = -1.0;
        ok = expect(state->save(instrument, &safe.output) && safe.bytes[textureOffset] == 0u
            && params->get_value(instrument, 1011u, &character) && character == 0.0,
            "Recall accepted Ambisonic Wavesets or Crush") && ok;
        for (double value : {6.0, 7.0}) {
            clap_event_param_value_t event {};
            event.header = { sizeof(event), 0u, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0u };
            event.param_id = 1011u; event.value = value;
            clap_input_events_t input {};
            input.ctx = &event;
            input.size = [](const clap_input_events_t*) -> uint32_t { return 1u; };
            input.get = [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
                return index ? nullptr : &static_cast<const clap_event_param_value_t*>(list->ctx)->header;
            };
            params->flush(instrument, &input, nullptr);
            ok = expect(params->get_value(instrument, 1011u, &character) && character == 0.0,
                "Automation accepted unsafe Ambisonic FX") && ok;
        }
        combinedRoundTrip.cursor = 0u;
        ok = expect(state->load(instrument, &combinedRoundTrip.input), "Could not restore combined fixture") && ok;

        for (uint32_t version = 1u; version < 10u; ++version) {
            StateBuffer legacy;
            legacy.bytes = saved.bytes;
            std::memcpy(legacy.bytes.data() + sizeof(uint32_t), &version, sizeof(version));
            ok = expect(!state->load(instrument, &legacy.input),
                "legacy state was unexpectedly accepted") && ok;
        }
        StateBuffer truncated;
        truncated.bytes = saved.bytes;
        truncated.bytes.pop_back();
        ok = expect(!state->load(instrument, &truncated.input),
            "truncated state was accepted") && ok;
        StateBuffer invalid;
        invalid.bytes = saved.bytes;
        setStateParameter(invalid, 0u, std::numeric_limits<double>::quiet_NaN());
        ok = expect(!state->load(instrument, &invalid.input),
            "non-finite state was accepted") && ok;
        StateBuffer invalidPreRoll;
        invalidPreRoll.bytes = saved.bytes;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::memcpy(invalidPreRoll.bytes.data() + preRollOffset, &nan, sizeof(nan));
        ok = expect(!state->load(instrument, &invalidPreRoll.input),
            "non-finite transient pre-roll was accepted") && ok;
        StateBuffer invalidShot;
        invalidShot.bytes = saved.bytes;
        std::memcpy(invalidShot.bytes.data() + playbackOffset + 32u + 32u * sizeof(float), &nan, sizeof(float));
        ok = expect(!state->load(instrument, &invalidShot.input), "Non-finite Shot Length was accepted") && ok;
        StateBuffer invalidFx; invalidFx.bytes = saved.bytes;
        std::memcpy(invalidFx.bytes.data() + modernOffset, &nan, sizeof(float));
        ok = expect(!state->load(instrument, &invalidFx.input), "Non-finite FX setting was accepted") && ok;
        StateBuffer invalidEnvelope; invalidEnvelope.bytes = saved.bytes;
        std::memcpy(invalidEnvelope.bytes.data() + envelopeOffset, &nan, sizeof(float));
        ok = expect(!state->load(instrument, &invalidEnvelope.input), "Non-finite gesture envelope was accepted") && ok;
        StateBuffer afterInvalid;
        ok = expect(state->save(instrument, &afterInvalid.output)
                && afterInvalid.bytes == combinedRoundTrip.bytes,
            "rejected state mutated the instrument") && ok;
        StateBuffer v019; v019.bytes = saved.bytes;
        v019.bytes.erase(v019.bytes.begin() + envelopeOffset, v019.bytes.begin() + envelopeOffset + 64u * sizeof(float));
        const uint32_t version019 = 13u;
        std::memcpy(v019.bytes.data() + sizeof(uint32_t), &version019, sizeof(version019));
        ok = expect(state->load(instrument, &v019.input), "v0.19 state did not migrate") && ok;
        StateBuffer migrated019;
        ok = expect(state->save(instrument, &migrated019.output), "Migrated v0.19 failed to save") && ok;
        for (unsigned n = 0u; n < 64u; ++n) {
            float value = 0.0f;
            std::memcpy(&value, migrated019.bytes.data() + envelopeOffset + n * sizeof(float), sizeof(value));
            ok = expect(value == 0.005f, "v0.19 migration did not preserve the original 5 ms fade") && ok;
        }
        StateBuffer v018; v018.bytes = saved.bytes;
        v018.bytes.erase(v018.bytes.end() - kCaptureStateBytes - kModernStateBytes, v018.bytes.end() - kCaptureStateBytes);
        const uint32_t version018 = 12u;
        std::memcpy(v018.bytes.data() + sizeof(uint32_t), &version018, sizeof(version018));
        ok = expect(state->load(instrument, &v018.input), "v0.18 state did not migrate") && ok;
        StateBuffer v015;
        v015.bytes = saved.bytes;
        v015.bytes.erase(v015.bytes.end() - kCaptureStateBytes - kModernStateBytes, v015.bytes.end() - kCaptureStateBytes);
        const uint32_t version015 = 11u;
        std::memcpy(v015.bytes.data() + sizeof(uint32_t), &version015, sizeof(version015));
        v015.bytes.erase(v015.bytes.begin() + preRollOffset + kTransientStateBytes,
            v015.bytes.begin() + preRollOffset + kTransientStateBytes + kPlaybackStateBytes);
        ok = expect(state->load(instrument, &v015.input), "v0.15 state did not migrate") && ok;
        StateBuffer v014;
        v014.bytes = saved.bytes;
        v014.bytes.erase(v014.bytes.end() - kCaptureStateBytes - kModernStateBytes, v014.bytes.end() - kCaptureStateBytes);
        const uint32_t priorVersion = 10u;
        std::memcpy(v014.bytes.data() + sizeof(uint32_t), &priorVersion, sizeof(priorVersion));
        v014.bytes.erase(v014.bytes.begin() + preRollOffset,
            v014.bytes.begin() + preRollOffset + kTransientStateBytes + kPlaybackStateBytes);
        ok = expect(state->load(instrument, &v014.input), "v0.13/v0.14 state did not migrate") && ok;
        StateBuffer migrated;
        ok = expect(state->save(instrument, &migrated.output)
            && migrated.bytes.size() == expectedStateBytes
            && std::all_of(migrated.bytes.begin() + preRollOffset,
                migrated.bytes.begin() + preRollOffset + 32u * sizeof(float), [](uint8_t v) { return v == 0u; })
            && migrated.bytes[preRollOffset + 32u * sizeof(float)] == 17u,
            "Older state did not default to zero pre-roll and its saved slice count") && ok;
        saved.cursor = 0u;
        ok = expect(state->load(instrument, &saved.input),
            "could not restore base fixture") && ok;
    }
    ok = expect(instrument->activate(instrument, 48000.0, 1u, 64u)
            && instrument->start_processing(instrument),
        "instrument processor did not activate") && ok;

    MidiInput cursorInput;
    cursorInput.events[0u].data[0u] = 0x93u;
    cursorInput.events[0u].data[1u] = 0x06u; // deck A CHOP cursor editor
    cursorInput.events[0u].data[2u] = 0x7fu;
    cursorInput.events[1u].data[0u] = 0xb6u;
    cursorInput.events[1u].data[1u] = 0x04u; // deck A LOOP
    cursorInput.events[1u].data[2u] = 0x0au; // +10 relative steps
    OutputEvents cursorOutput;
    std::vector<float> cursorLeft;
    std::vector<float> cursorRight;
    ok = expect(processStereo(instrument, cursorInput, cursorOutput,
            cursorLeft, cursorRight),
        "CHOP cursor encoder process failed") && ok;
    StateBuffer cursorState;
    ok = expect(state->save(instrument, &cursorState.output)
            && std::abs(stateDouble(cursorState, kEditPositionOffset) - 0.10)
                < 1.0e-9,
        "CHOP LOOP encoder did not move the edit cursor") && ok;

    MidiInput midi;
    midi.events[0].data[1]=0x05; // Explicit PLAY after testing the CHOP cursor.
    OutputEvents output;
    std::vector<float> left;
    std::vector<float> right;
    bool rendered = false;
    for (int attempt = 0; ok && attempt < 200 && !rendered; ++attempt) {
        instrument->on_main_thread(instrument);
        rendered = processStereo(instrument, midi, output, left, right)
            && maximumMagnitude(left) > 0.01f
            && maximumMagnitude(right) > 0.01f;
        if (!rendered) std::this_thread::sleep_for(
            std::chrono::milliseconds(5));
    }
    ok = expect(rendered,
        "Reloop Neon pad MIDI did not trigger the asynchronously loaded slot")
        && ok;
    ok = expect(kHostLedOutput ? (cursorOutput.sysexCount + output.sysexCount >= 1u
            && cursorOutput.midiCount + output.midiCount >= 1u
            && cursorOutput.largePadCount + output.largePadCount >= 8u)
            : (cursorOutput.sysexCount + output.sysexCount + cursorOutput.midiCount + output.midiCount == 0u),
        "Neon LED output did not respect its platform-specific routing")
        && ok;

    int fixturePage=0;
    const auto send = [&](uint8_t status, uint8_t note, uint8_t velocity = 127u) {
        MidiInput input;
        input.performanceGesture(status,note,velocity,fixturePage);
        return processStereo(instrument, input, output, left, right);
    };
    const auto change = [&](clap_id id, double value) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id; event.value = value;
        event.note_id = -1; event.port_index = -1; event.channel = -1; event.key = -1;
        clap_input_events_t input {};
        input.ctx = &event;
        input.size = [](const clap_input_events_t*) { return 1u; };
        input.get = [](const clap_input_events_t* input, uint32_t index) -> const clap_event_header_t* {
            return index == 0u ? &static_cast<const clap_event_param_value_t*>(input->ctx)->header : nullptr;
        };
        params->flush(instrument, &input, &output.list);
    };
    // Exercise every new processor in the exact CLAP binary, including effect
    // switches on an active voice. processChannels measures allocations when
    // the optional realtime probe is enabled.
    double previousCharacter=0, previousMix=0;
    params->get_value(instrument,1011u,&previousCharacter);
    params->get_value(instrument,1009u,&previousMix);
    change(1009u,.8);
    for(unsigned effect=0;effect<8;++effect) {
        change(1011u,effect);
        ok=expect(send(0x97u,0u),"Character FX trigger failed")&&ok;
        for(unsigned n=0;n<180;++n) {
            ok=expect(send(0x90u,127u,0u),"Character FX processing failed")&&ok;
            ok=expect(std::all_of(left.begin(),left.end(),[](float value){return std::isfinite(value);})
                && std::all_of(right.begin(),right.end(),[](float value){return std::isfinite(value);}),
                "Character FX produced nonfinite host output")&&ok;
        }
    }
    change(1011u,previousCharacter);change(1009u,previousMix);
    instrument->reset(instrument);
    // Explicit musical channels must work even where the controller's raw
    // protocol uses the same note addresses. Private bridge controls remain
    // independent; Omni retains the existing hardware priority.
    struct HeaderInput {
        std::vector<const clap_event_header_t*> headers;
        clap_input_events_t list {};
        HeaderInput() {
            list.ctx=this;
            list.size=[](const clap_input_events_t* e) {return static_cast<uint32_t>(static_cast<const HeaderInput*>(e->ctx)->headers.size());};
            list.get=[](const clap_input_events_t* e,uint32_t n) {return static_cast<const HeaderInput*>(e->ctx)->headers[n];};
        }
    };
    double oldReceive=0,oldTrigger=0,oldRelease=0;
    params->get_value(instrument,7u,&oldReceive);
    params->get_value(instrument,1015u,&oldTrigger);
    params->get_value(instrument,1006u,&oldRelease);
    change(1015u,1);change(1006u,0);
    clap_event_note_t standard {};
    standard.header={sizeof(standard),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_NOTE_ON,0};
    standard.port_index=0;standard.key=36;standard.note_id=222;standard.velocity=1;
    HeaderInput channelNotes;channelNotes.headers={&standard.header};
    for (unsigned channel=0;channel<16;++channel) {
        change(7u,channel+1);instrument->reset(instrument);
        ok=expect(send(static_cast<uint8_t>(0x90u|channel),36) && maximumMagnitude(left)>.001f,
            "Selected raw MIDI channel failed to trigger A1 with NEON ownership enabled")&&ok;
        send(static_cast<uint8_t>(0x80u|channel),36,0);
        for(unsigned n=0;n<8;++n) send(0x90u,127,0);
        ok=expect(maximumMagnitude(left)==0,"Selected raw MIDI note-off did not release the pad")&&ok;
        instrument->reset(instrument);standard.channel=static_cast<int16_t>((channel+1)%16);
        standard.header.type=CLAP_EVENT_NOTE_ON;
        ok=expect(processStereo(instrument,channelNotes,output,left,right) && maximumMagnitude(left)==0,
            "CLAP note from an unselected channel was accepted")&&ok;
        standard.channel=static_cast<int16_t>(channel);
        ok=expect(processStereo(instrument,channelNotes,output,left,right) && maximumMagnitude(left)>.001f,
            "Selected CLAP channel failed to trigger A1")&&ok;
        standard.header.type=CLAP_EVENT_NOTE_OFF;
        standard.channel=static_cast<int16_t>((channel+1)%16);
        ok=expect(processStereo(instrument,channelNotes,output,left,right) && maximumMagnitude(left)>.001f,
            "Wrong-channel CLAP note-off released the selected channel")&&ok;
        standard.channel=static_cast<int16_t>(channel);
        processStereo(instrument,channelNotes,output,left,right);
        for(unsigned n=0;n<8;++n) send(0x90u,127,0);
        ok=expect(maximumMagnitude(left)==0,"Selected CLAP note-off did not release the pad")&&ok;
    }
    change(7u,1);instrument->reset(instrument);
    ok=expect(send(0x91u,36) && maximumMagnitude(left)==0,"Wrong-channel raw MIDI was accepted")&&ok;
    clap_event_param_value_t channelChange {};
    channelChange.header={sizeof(channelChange),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
    channelChange.param_id=7;channelChange.value=2;
    standard.channel=1;standard.header.type=CLAP_EVENT_NOTE_ON;
    channelNotes.headers={&channelChange.header,&standard.header};
    ok=expect(processStereo(instrument,channelNotes,output,left,right) && maximumMagnitude(left)>.001f,
        "MIDI channel automation did not apply before a following note in the same block")&&ok;
    double oldPressure=0;
    params->get_value(instrument,1010u,&oldPressure);
    change(1010u,1);change(1011u,0);
    clap_event_note_expression_t pressure {};
    pressure.header={sizeof(pressure),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_NOTE_EXPRESSION,0};
    pressure.expression_id=CLAP_NOTE_EXPRESSION_PRESSURE;
    pressure.port_index=0;pressure.key=36;pressure.note_id=222;
    const auto pressureRender=[&](int16_t channel,double value,bool raw) {
        instrument->reset(instrument);
        standard.header.type=CLAP_EVENT_NOTE_ON;standard.channel=1;
        channelNotes.headers={&standard.header};
        ok=processStereo(instrument,channelNotes,output,left,right)&&ok;
        pressure.channel=channel;pressure.value=value;
        channelNotes.headers={&pressure.header};
        if(raw) ok=send(static_cast<uint8_t>(0xa0u|channel),36,static_cast<uint8_t>(value*127))&&ok;
        else ok=processStereo(instrument,channelNotes,output,left,right)&&ok;
        return left;
    };
    const auto noPressure=pressureRender(1,0,false);
    const auto wrongPressure=pressureRender(0,1,false);
    const auto rightPressure=pressureRender(1,1,false);
    ok=expect(noPressure==wrongPressure && rightPressure!=noPressure,
        "CLAP pressure did not respect the selected musical channel")&&ok;
    ok=expect(pressureRender(0,1,true)==noPressure && pressureRender(1,1,true)==rightPressure,
        "Raw poly pressure did not respect the selected musical channel")&&ok;
    change(1010u,oldPressure);change(1011u,previousCharacter);
    // Save/load the existing parameter, without introducing a new state schema.
    StateBuffer channelState;
    ok=expect(state->save(instrument,&channelState.output),"MIDI receive state save failed")&&ok;
    const auto* recalledChannel=factory->create_plugin(factory,&host.host,"org.s3g.s3g-dsp.sample-neon");
    const bool channelInit=recalledChannel && recalledChannel->init(recalledChannel);
    const auto* channelStateApi=channelInit?static_cast<const clap_plugin_state_t*>(recalledChannel->get_extension(recalledChannel,CLAP_EXT_STATE)):nullptr;
    const auto* channelParams=channelInit?static_cast<const clap_plugin_params_t*>(recalledChannel->get_extension(recalledChannel,CLAP_EXT_PARAMS)):nullptr;
    double recalledReceive=-1;
    ok=expect(channelStateApi && channelStateApi->load(recalledChannel,&channelState.input)
        && channelParams && channelParams->get_value(recalledChannel,7,&recalledReceive) && recalledReceive==2,
        "MIDI receive channel failed project recall")&&ok;
    if(recalledChannel)recalledChannel->destroy(recalledChannel);
    change(7u,16);instrument->reset(instrument);
    using namespace s3g::controller::neon_midi;
    BridgeMessage control;control.kind=BridgeKind::Control;control.midi={0x97,0,127};
    const auto packet=encodeBridge(control);
    clap_event_midi_sysex_t bridge {};
    bridge.header={sizeof(bridge),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
    bridge.buffer=packet.data();bridge.size=static_cast<uint32_t>(packet.size());
    channelNotes.headers={&bridge.header};
    ok=expect(processStereo(instrument,channelNotes,output,left,right) && maximumMagnitude(left)>.001f,
        "Musical channel filter disabled private NEON control input")&&ok;
    // Omni accepts ordinary musical channels that do not overlap raw controls.
    change(7u,0);
    for(uint8_t channel:{0u,1u,12u,15u}) {
        instrument->reset(instrument);
        ok=expect(send(static_cast<uint8_t>(0x90u|channel),36) && maximumMagnitude(left)>.001f,
            "Omni stopped accepting ordinary MIDI notes")&&ok;
    }
    change(7u,oldReceive);change(1015u,oldTrigger);change(1006u,oldRelease);
    instrument->reset(instrument);
    // Custom maps replace subtraction-based routing for both CLAP and MIDI,
    // including keys 0/127 and live map changes before a note in the same block.
    {
        auto mapped = sequentialNotes(); mapped[0]=0; mapped[1]=127;
        auto mapPacket=encodeNoteMap(mapped);
        clap_event_midi_sysex_t mapEvent {};
        mapEvent.header={sizeof(mapEvent),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
        mapEvent.buffer=mapPacket.data();mapEvent.size=static_cast<uint32_t>(mapPacket.size());
        change(7,1);change(1015,1);change(1006,0);
        standard.header.type=CLAP_EVENT_NOTE_ON;standard.key=0;standard.channel=0;
        channelNotes.headers={&mapEvent.header,&standard.header};
        ok=expect(processStereo(instrument,channelNotes,output,left,right) && maximumMagnitude(left)>.001f,
            "Custom map must apply before a CLAP note in the same block")&&ok;
        const auto* names=static_cast<const clap_plugin_note_name_t*>(instrument->get_extension(instrument,CLAP_EXT_NOTE_NAME));
        clap_note_name_t name {};
        ok=expect(names && names->get(instrument,0,&name) && name.key==0
            && names->get(instrument,1,&name) && name.key==127,"Custom pad names must expose actual keys")&&ok;
        instrument->reset(instrument);
        ok=expect(send(0x90,0,64) && maximumMagnitude(left)>.001f,"Custom raw MIDI key 0 failed")&&ok;
        send(0x80,0,0);for(unsigned n=0;n<8;++n) send(0x80,126,0);
        ok=expect(maximumMagnitude(left)==0,"Custom raw MIDI release failed")&&ok;
        instrument->reset(instrument);
        ok=expect(send(0x90,36,100) && maximumMagnitude(left)==0,"Old default address still triggers custom pad")&&ok;
        StateBuffer mappedState;
        ok=expect(state->save(instrument,&mappedState.output) && mappedState.bytes[4]==24,
            "Custom map did not extend saved state to v24")&&ok;
        for(unsigned n=0;n<34;++n) {
            StateBuffer truncated;truncated.bytes=mappedState.bytes;truncated.bytes.resize(truncated.bytes.size()-1-n);
            ok=expect(!state->load(instrument,&truncated.input),"Truncated note-map tail accepted")&&ok;
        }
        StateBuffer badMap;badMap.bytes=mappedState.bytes;badMap.bytes.back()=0;
        ok=expect(!state->load(instrument,&badMap.input),"Duplicate pad map accepted from state")&&ok;
        StateBuffer unchangedMap;
        ok=expect(state->save(instrument,&unchangedMap.output) && unchangedMap.bytes==mappedState.bytes,
            "Invalid map restore changed the current set")&&ok;
        // Malformed wire data must not replace the working map.
        mapPacket[9]=0;channelNotes.headers={&mapEvent.header};
        processStereo(instrument,channelNotes,output,left,right);
        ok=expect(names->get(instrument,1,&name) && name.key==127,"Malformed MIDI map changed note names")&&ok;
        // Restore defaults under a held finger, then re-use the pad's new key.
        send(0x90,0,100);
        mapPacket=encodeNoteMap(sequentialNotes());
        processStereo(instrument,channelNotes,output,left,right);
        send(0x90,36,100);send(0x80,0,0);
        ok=expect(maximumMagnitude(left)>.001f,"Stale release after map change killed the new address")&&ok;
        send(0x80,36,0);for(unsigned n=0;n<8;++n) send(0x80,126,0);
        ok=expect(maximumMagnitude(left)==0,"New address stuck after a map change")&&ok;
        change(7u,oldReceive);change(1015u,oldTrigger);change(1006u,oldRelease);
        instrument->reset(instrument);
    }
    // Compare the actual instrument output for ordinary velocity with the
    // hardware's separate CC + fixed-127 note sequence across host blocks.
    for (uint8_t key : {0u, 0x10u}) { // Both physical shortcuts into PLAY.
        float previousPeak = 0.0f;
        for (uint8_t velocity : {3u, 32u, 64u, 127u}) {
            instrument->reset(instrument);
            ok = expect(send(0x97u, key, velocity), "Reference velocity render failed") && ok;
            const auto expectedLeft = left, expectedRight = right;
            instrument->reset(instrument);
            ok = expect(send(0xb7u, key, velocity) && maximumMagnitude(left) == 0.0f,
                "Velocity CC alone triggered Sample Neon") && ok;
            ok = expect(send(0x97u, key, 127u), "Paired velocity render failed") && ok;
            bool matched = left.size() == expectedLeft.size() && right.size() == expectedRight.size();
            for (std::size_t i = 0u; matched && i < left.size(); ++i)
                matched = std::abs(left[i] - expectedLeft[i]) < 1.0e-6f
                    && std::abs(right[i] - expectedRight[i]) < 1.0e-6f;
            ok = expect(matched && maximumMagnitude(left) > 0.00001f,
                "Direct NEON hardware velocity differs from ordinary MIDI velocity") && ok;
            const float peak = maximumMagnitude(left);
            ok = expect(peak > previousPeak, "Direct NEON soft/hard strikes all rendered at constant level") && ok;
            previousPeak = peak;
        }
    }
    instrument->reset(instrument);
    ok = expect(send(0x9bu, 0x24u) && maximumMagnitude(left) == 0.0f,
        "Returned status-lamp MIDI triggered an ordinary sample note") && ok;
    ok = expect(send(0x97u, 0x10u) && maximumMagnitude(left) > 0.001f,
        "EDIT cell pad did not audition its current sound") && ok;
    ok = expect(send(0x97u, 0x71u) && maximumMagnitude(left) > 0.001f,
        "EDIT secondary property pad did not preview the selected cell") && ok;
    const auto methodOffset = expectedStateBytes - kCaptureStateBytes - kPlaybackStateBytes - kTransientStateBytes - kTextureStateBytes - kModernStateBytes;
    for (const auto selection : { std::pair<uint8_t, uint8_t> { 0x72u, kMotionOption },
            std::pair<uint8_t, uint8_t> { 0x73u, kGrainsOption }, std::pair<uint8_t, uint8_t> { 0x71u, 0u } }) {
        send(0x97u, selection.first);
        StateBuffer technique;
        ok = expect(state->save(instrument, &technique.output) && technique.bytes[methodOffset] == selection.second,
            "Hardware technique shortcut did not commit exclusively") && ok;
    }
    ok = expect(send(0x97u, 0x74u) && maximumMagnitude(left) > 0.001f,
        "EDIT One Shot shortcut did not preview the selected technique") && ok;
    double triggerMode = -1.0;
    ok = expect(params->get_value(instrument, 1015u, &triggerMode) && triggerMode == 2.0,
        "EDIT One Shot shortcut did not commit the trigger mode") && ok;
    send(0x97u, 0x77u); // Momentary cell stop, not a persistent mute.
    for (unsigned i = 0u; i < 5u; ++i) send(0x90u, 127u, 0u);
    ok = expect(maximumMagnitude(left) == 0.0f && send(0x97u, 0x10u)
        && maximumMagnitude(left) > 0.001f, "STOP latched a hidden mute or failed to stop") && ok;
    change(1000u, -12.0);
    send(0x97u, 0x10u);
    const float quietPreview = maximumMagnitude(left);
    change(1000u, 0.0);
    send(0x97u, 0x10u);
    ok = expect(maximumMagnitude(left) > quietPreview * 3.9f,
        "EDIT preview bypassed the updated cell gain") && ok;
    instrument->reset(instrument);
    ok = expect(send(0x97u, 0x68u) && maximumMagnitude(left) == 0.0f,
        "CHOP secondary ADD tool unexpectedly played audio") && ok;
    ok = expect(send(0x97u, 0x08u) && maximumMagnitude(left) > 0.001f,
        "CHOP primary slice did not preview audio") && ok;

    change(1017u, 0.0); // Zero crossing override is per cell.
    change(1003u, 0.123456);
    double trim = 0.0;
    ok = expect(params->get_value(instrument, 1003u, &trim) && trim == 0.123456,
        "ZERO CROSS OFF did not preserve the requested trim") && ok;
    change(1017u, 1.0);
    ok = expect(params->get_value(instrument, 1003u, &trim)
        && std::abs(trim * 4096.0 - std::round(trim * 4096.0)) < 1.0e-9,
        "ZERO CROSS ON did not resolve to a sample boundary") && ok;
    change(1017u, 0.0);
    change(1004u, 0.05);
    double end = 0.0;
    ok = expect(params->get_value(instrument, 1004u, &end) && end > trim,
        "unsnapped trim allowed an empty or inverted cell") && ok;
    change(1004u, 1.0); change(1003u, 0.0); change(1017u, 1.0);

    // A release remains attached to its original cell after page/bank changes.
    change(1015u, 1.0); // Gate
    instrument->reset(instrument);
    ok = expect(send(0x97u, 0x78u) && maximumMagnitude(left) > 0.001f,
        "RESAMPLE secondary did not perform the gated cell") && ok;
    send(0x93u, 0x07u); // EDIT mode
    send(0x94u, 0x00u); // Bank B
    send(0x87u, 0x78u, 0u); // Release the original RESAMPLE Bank A pad.
    for (unsigned block = 0u; block < 8u; ++block) send(0x90u, 127u, 0u);
    ok = expect(maximumMagnitude(left) == 0.0f,
        "changing page/bank stranded the original held note") && ok;
    change(1015u, 2.0); // One-shot

    // All cell-pad shortcuts share performance behavior, including Hot Cue:
    // selecting EDIT encoders must not turn Toggle into a forced retrigger.
    send(0x93u, 0x00u); // Bank A
    change(1015u, 3.0);
    for (uint8_t key : {0u, 0x10u, 0x78u}) {
        instrument->reset(instrument);
        ok = expect(send(0x97u, key) && maximumMagnitude(left) > 0.001f,
            "Cell pad Toggle did not start") && ok;
        send(0x87u, key, 0u);
        ok = expect(maximumMagnitude(left) > 0.001f, "Cell pad Toggle stopped on release") && ok;
        send(0x97u, key);
        for (unsigned block = 0u; block < 8u; ++block) send(0x90u, 127u, 0u);
        ok = expect(maximumMagnitude(left) == 0.0f, "Cell pad Toggle retriggered instead of stopping") && ok;
    }
    change(1015u, 2.0);

    // Record and perform entirely within RESAMPLE, then choose an empty target.
    instrument->reset(instrument);
    send(0x93u, 0x00u); // Bank A (primary pad notes intentionally retain the bank).
    send(0x97u, 0x58u); // SHIFT + secondary pad 1 starts REC, not loaded A1.
    ok = expect(maximumMagnitude(left) == 0.0f, "SHIFT REC accidentally triggered loaded A1") && ok;
    send(0x87u, 0x58u, 0u); // REC release must not stop capture.
    send(0x97u, 0x38u); // SHIFT + primary pad 1 stops it on the other layer.
    instrument->on_main_thread(instrument);
    StateBuffer shortcutTake;
    ok = expect(state->save(instrument, &shortcutTake.output)
        && shortcutTake.bytes.size() == expectedStateBytes + 128u * 2u * sizeof(float),
        "SHIFT REC did not toggle recording or released too early") && ok;
    send(0x97u, 0x1bu); // Discard silent test take, then record actual sound.
    instrument->on_main_thread(instrument);
    MidiInput recordAndPlay;
    recordAndPlay.events[0u].data[0u] = 0x97u;
    recordAndPlay.events[0u].data[1u] = 0x38u; // SHIFT + primary pad 1 starts REC.
    recordAndPlay.events[1u].data[1u] = 0x78u; // RESAMPLE secondary plays loaded A1.
    // Primary Record is already selected; switch explicitly before performing.
    recordAndPlay.events[2]=recordAndPlay.events[1];
    recordAndPlay.events[1].data[0]=0x93;recordAndPlay.events[1].data[1]=0x0c;
    recordAndPlay.events[1].header.time=0;
    recordAndPlay.list.size=[](const clap_input_events_t*)->uint32_t{return 3;};
    ok = expect(processStereo(instrument, recordAndPlay, output, left, right),
        "record-and-perform block failed") && ok;
    for (unsigned block = 0u; block < 3u; ++block) send(0x90u, 127u, 0u);
    send(0x97u, 0x58u); // SHIFT + secondary pad 1 stops REC on the other layer.
    instrument->on_main_thread(instrument);
    StateBuffer captured;
    ok = expect(state->save(instrument, &captured.output)
        && captured.bytes.size() == expectedStateBytes + 256u * 2u * sizeof(float),
        "completed stereo capture was not embedded in state") && ok;
    uint32_t captureWidth = 0u, captureLength = 0u;
    if (captured.bytes.size() >= expectedStateBytes) {
        std::memcpy(&captureWidth, captured.bytes.data() + expectedStateBytes - 16u, 4u);
        std::memcpy(&captureLength, captured.bytes.data() + expectedStateBytes - 12u, 4u);
    }
    ok = expect(captureWidth == 2u && captureLength == 256u,
        "capture selected the wrong bus width or block range") && ok;
    ok = expect(stateDouble(captured, expectedStateBytes - kCaptureStateBytes + 4u * sizeof(double)) == 255.0,
        "Playing a loaded RESAMPLE pad changed the capture destination") && ok;
    send(0x97u, 0x58u); // A completed take is never overwritten by the shortcut.
    send(0x87u, 0x58u, 0u);
    instrument->on_main_thread(instrument);
    StateBuffer protectedTake;
    ok = expect(state->save(instrument, &protectedTake.output) && protectedTake.bytes == captured.bytes,
        "SHIFT REC overwrote a completed review take") && ok;
    // An unassigned review has no pad owner. After a host reactivation, crop
    // it while an audition voice is still reading the previous immutable take.
    // ASan must see that old source retained until processing is quiescent.
    instrument->stop_processing(instrument);
    instrument->deactivate(instrument);
    ok = expect(instrument->activate(instrument, 48000.0, 1u, 64u)
        && instrument->start_processing(instrument), "capture lifetime reactivation failed") && ok;
    send(0x97u, 0x1au); // Audition the 256-frame review (one 64-frame block).
    send(0x97u, 0x3eu); // Crop the full range; old audition is still active.
    instrument->on_main_thread(instrument);
    for (unsigned n = 0u; n < 4u; ++n)
        ok = expect(send(0x90u, 127u, 0u) && std::all_of(left.begin(), left.end(),
            [](float sample) { return std::isfinite(sample); }), "capture crop after reactivation was unsafe") && ok;
    StateBuffer retainedTake;
    ok = expect(state->save(instrument, &retainedTake.output) && retainedTake.bytes == captured.bytes,
        "full-range crop after reactivation altered the take") && ok;
    send(0x97u, 0x79u); // RESAMPLE secondary selects empty A2.
    send(0x97u, 0x1fu); // Assign.
    instrument->on_main_thread(instrument);
    instrument->reset(instrument);
    ok = expect(send(0x97u, 0x01u) && maximumMagnitude(left) > 0.001f
        && std::abs(maximumMagnitude(right) - 2.0f * maximumMagnitude(left)) < 1.0e-5f,
        "assigned capture did not play as an ordinary channel-linked cell") && ok;
    if (maximumMagnitude(left) <= 0.001f) {
        double captureStart = -1.0, captureEnd = -1.0;
        params->get_value(instrument, 1035u, &captureStart);
        params->get_value(instrument, 1036u, &captureEnd);
        std::cerr << "Capture A2 trim=" << captureStart << ',' << captureEnd
            << " peak=" << maximumMagnitude(left) << '\n';
    }
    StateBuffer assigned;
    ok = expect(state->save(instrument, &assigned.output), "capture assignment state save failed") && ok;
    send(0x97u, 0x1fu); // Assigning to occupied A2 must not overwrite.
    instrument->on_main_thread(instrument);
    StateBuffer protectedCell;
    ok = expect(state->save(instrument, &protectedCell.output) && assigned.bytes == protectedCell.bytes,
        "capture assignment overwrote an occupied cell") && ok;
    // Remove the external source link. Captured cell and review must recall alone.
    std::fill_n(assigned.bytes.data() + sizeof(StateHeader) + kParameterCount * sizeof(double), kPathBytes, 0u);
    ok = expect(state->load(instrument, &assigned.input), "embedded capture state did not reload") && ok;
    instrument->reset(instrument);
    ok = expect(send(0x97u, 0x01u) && maximumMagnitude(left) > 0.001f,
        "capture recall incorrectly depended on the original source file") && ok;

    // Crop the review, not a previously assigned cell. Change its saved trim
    // exactly, then use the physical SHIFT+RESAMPLE pad 7 command.
    StateBuffer trimmed;
    ok = expect(state->save(instrument, &trimmed.output), "capture trim fixture save failed") && ok;
    const std::size_t captureSettingsOffset = expectedStateBytes - kCaptureStateBytes;
    const double cropStart = 0.25, cropEnd = 0.75, noSnap = 0.0;
    std::memcpy(trimmed.bytes.data() + captureSettingsOffset, &cropStart, sizeof(double));
    std::memcpy(trimmed.bytes.data() + captureSettingsOffset + sizeof(double), &cropEnd, sizeof(double));
    std::memcpy(trimmed.bytes.data() + captureSettingsOffset + 3u * sizeof(double), &noSnap, sizeof(double));
    ok = expect(state->load(instrument, &trimmed.input), "trimmed capture state did not load") && ok;
    send(0x97u, 0x3eu); // Shift + primary RESAMPLE pad 7: crop.
    instrument->on_main_thread(instrument);
    StateBuffer cropped;
    ok = expect(state->save(instrument, &cropped.output)
        && cropped.bytes.size() == expectedStateBytes + (256u + 128u) * 2u * sizeof(float)
        && stateDouble(cropped, captureSettingsOffset) == 0.0
        && stateDouble(cropped, captureSettingsOffset + sizeof(double)) == 1.0,
        "capture crop did not remove outside frames/reset the review range") && ok;
    if (cropped.bytes.size() == expectedStateBytes + (256u + 128u) * 2u * sizeof(float)) {
        const auto reviewAudio = expectedStateBytes + 256u * 2u * sizeof(float);
        for (unsigned channel = 0u; channel < 2u; ++channel)
            ok = expect(std::memcmp(cropped.bytes.data() + reviewAudio + channel * 128u * sizeof(float),
                captured.bytes.data() + expectedStateBytes + (channel * 256u + 64u) * sizeof(float),
                128u * sizeof(float)) == 0, "crop changed samples or channel alignment") && ok;
        // A2's original embedded PCM is retained independently of the review.
        const auto a2Audio = expectedStateBytes - 33u * 16u + 2u * 16u;
        ok = expect(std::memcmp(cropped.bytes.data() + a2Audio,
            captured.bytes.data() + expectedStateBytes, 256u * 2u * sizeof(float)) == 0,
            "cropping the review modified an already assigned cell") && ok;
    }
    send(0x97u, 0x01u); // PLAY selects A2 (the captured cell).
    send(0x97u, 0x6eu); // CHOP secondary Assign One -> first empty cell A1.
    instrument->on_main_thread(instrument);
    instrument->reset(instrument);
    ok = expect(send(0x97u, 0x00u) && maximumMagnitude(left) > 0.001f,
        "CHOP assignment did not create a playable ordinary cell") && ok;

    // A complete 32-slice map fits when the original cell becomes slice 1.
    StateBuffer replaceSource;
    replaceSource.bytes = saved.bytes;
    setStateParameter(replaceSource, 9u, 7.0); // Keep the source's audible tuning.
    const auto sliceCountsOffset = sizeof(StateHeader) + kParameterCount * sizeof(double)
        + 32u * kPathBytes + kSliceModeBytes;
    replaceSource.bytes[sliceCountsOffset] = 32u;
    ok = expect(state->load(instrument, &replaceSource.input), "replace-source fixture failed to load") && ok;
    instrument->reset(instrument);
    bool readyForReplace = false;
    for (unsigned attempt = 0u; attempt < 100u && !readyForReplace; ++attempt) {
        instrument->on_main_thread(instrument);
        readyForReplace = send(0x97u, 0x00u) && maximumMagnitude(left) > 0.001f;
        if (!readyForReplace) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    send(0x97u, 0x4fu); // Shift + CHOP secondary Assign All -> replace source.
    instrument->on_main_thread(instrument);
    StateBuffer mapped;
    ok = expect(readyForReplace && state->save(instrument, &mapped.output),
        "32-slice replacement failed") && ok;
    for (unsigned slot = 0u; slot < 32u; ++slot) {
        double startValue = -1.0, endValue = -1.0, tuneValue = 0.0;
        params->get_value(instrument, 1000u + slot * 32u + 3u, &startValue);
        params->get_value(instrument, 1000u + slot * 32u + 4u, &endValue);
        params->get_value(instrument, 1000u + slot * 32u + 2u, &tuneValue);
        ok = expect(startValue == 0.0 && endValue == 1.0,
            "committed slices retained source-relative trim instead of full new samples") && ok;
        ok = expect(tuneValue == 7.0, "slice assignment lost the source's tuning") && ok;
        const auto pathOffset = sizeof(StateHeader) + kParameterCount * sizeof(double) + slot * kPathBytes;
        ok = expect(mapped.bytes[pathOffset] == 0u && mapped.bytes[sliceCountsOffset + slot] == 1u,
            "committed slice still links to the original file or old slice map") && ok;
        const auto sample = embeddedAudio(mapped, expectedStateBytes, slot);
        ok = expect(sample.channels == 2u && sample.frames == 128u && sample.rate == 48000.0,
            "source replacement failed to create 32 independently cropped samples") && ok;
    }
    ok = expect(mapped.bytes.size() == expectedStateBytes + 4096u * 2u * sizeof(float),
        "32 committed slices contain duplicated full-source audio") && ok;
    ok = expect(state->load(instrument, &mapped.input), "committed slice set failed to reload") && ok;
    StateBuffer recalledSlices;
    ok = expect(state->save(instrument, &recalledSlices.output) && recalledSlices.bytes == mapped.bytes,
        "committed slice audio or trim changed on recall") && ok;
    instrument->reset(instrument);
    ok = expect(send(0x9au, 0x07u) && maximumMagnitude(left) > 0.001f,
        "last committed slice did not play after recall") && ok;
    StateBuffer fullBefore;
    state->save(instrument, &fullBefore.output);
    send(0x97u, 0x6eu); // No empty cells: must not change another occupied cell.
    instrument->on_main_thread(instrument);
    StateBuffer fullAfter;
    ok = expect(state->save(instrument, &fullAfter.output) && fullAfter.bytes == fullBefore.bytes,
        "full-bank assignment overwrote an unrelated occupied cell") && ok;

    if (instrument) {
        // Inventory color survives bank changes and periodic recovery, without
        // turning mere selection into "last played" or silence into LED-off.
        StateBuffer ledFixture; ledFixture.bytes = saved.bytes;
        const size_t ledPaths = sizeof(StateHeader) + kParameterCount * sizeof(double);
        std::snprintf(reinterpret_cast<char*>(ledFixture.bytes.data() + ledPaths + 8u * kPathBytes),
            kPathBytes, "%s", wavePath.c_str());
        ok = expect(state->load(instrument, &ledFixture.input), "LED bank fixture failed to load") && ok;
        if (kHostLedOutput) {
        output.samplerPads.fill(0xffu);
        send(0x93u, 0x05u); // PLAY primary; bank buttons preserve current mode.
        send(0x94u, 0x00u); // Never-played B1.
        bool lit = false;
        for (unsigned n = 0u; n < 100u && !lit; ++n) {
            instrument->on_main_thread(instrument);
            send(0x90u, 127u, 0u);
            lit = output.samplerPads[0] == 48u;
            if (!lit) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ok = expect(lit && output.samplerPads[1] == 0u, "Loaded bank cell was not red / empty cell was not dark") && ok;
        send(0x93u, 0x00u); send(0x97u, 0x00u); // Play A1.
        ok = expect(output.samplerPads[0] == 127u, "Actively playing cell was not white") && ok;
        for (unsigned n = 0u; n < 120u; ++n) send(0x90u, 127u, 0u);
        ok = expect(output.samplerPads[0] == 104u, "Completed cell did not retain yellow last-played state") && ok;
        send(0x94u, 0x00u);
        ok = expect(output.samplerPads[0] == 48u, "B inherited A's last-played highlight") && ok;
        send(0x93u, 0x00u);
        ok = expect(output.samplerPads[0] == 104u, "A lost last-played history on returning from B") && ok;
        const auto beforeRefresh = output.largePadCount;
        for (unsigned n = 0u; n < 190u; ++n) send(0x90u, 127u, 0u);
        ok = expect(output.largePadCount >= beforeRefresh + 8u && output.samplerPads[0] == 104u,
            "Unchanged LED inventory was not periodically reasserted") && ok;
        }
        // Hold Grains on D1 while a hypothetical host route returns the
        // plug-in's own MIDI output. This reproduced the D1 -> D2 selection
        // bug in 0.20 when D2 was loaded and its red lamp became a pad press.
        StateBuffer heldFixture; heldFixture.bytes = saved.bytes;
        std::fill_n(heldFixture.bytes.data() + ledPaths, 32u * kPathBytes, 0u);
        for (unsigned slot : {24u, 25u}) {
            std::snprintf(reinterpret_cast<char*>(heldFixture.bytes.data() + ledPaths + slot * kPathBytes),
                kPathBytes, "%s", wavePath.c_str());
            setStateParameter(heldFixture, 7u + slot * 19u + 15u, 1.0); // Hold.
            heldFixture.bytes[methodOffset + slot] = kGrainsOption;
            const float fade = 0.02f;
            const size_t envelope = expectedStateBytes - kCaptureStateBytes - 64u * sizeof(float);
            std::memcpy(heldFixture.bytes.data() + envelope + 2u * slot * sizeof(float), &fade, sizeof(float));
            std::memcpy(heldFixture.bytes.data() + envelope + (2u * slot + 1u) * sizeof(float), &fade, sizeof(float));
        }
        ok = expect(state->load(instrument, &heldFixture.input), "D1 Hold fixture failed to load") && ok;
        send(0x93u, 0x05u); send(0x96u, 0x00u); // PLAY, bank D.
        bool grainsLoaded = false;
        for (unsigned n = 0u; n < 100u && !grainsLoaded; ++n) {
            instrument->on_main_thread(instrument);
            send(0x97u, 0x00u, 105u);
            for (unsigned block = 0u; block < 20u; ++block) send(0x90u, 127u, 0u);
            grainsLoaded = maximumMagnitude(left) > 0.00001f;
            if (!grainsLoaded) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ok = expect(grainsLoaded, "D1 Grains did not sound") && ok;
        struct ReturnedMidi {
            std::vector<clap_event_midi_t> events;
            clap_input_events_t list {};
            ReturnedMidi() {
                list.ctx = this;
                list.size = [](const clap_input_events_t* in) { return static_cast<uint32_t>(static_cast<const ReturnedMidi*>(in->ctx)->events.size()); };
                list.get = [](const clap_input_events_t* in, uint32_t n) -> const clap_event_header_t* {
                    const auto& events = static_cast<const ReturnedMidi*>(in->ctx)->events;
                    return n < events.size() ? &events[n].header : nullptr;
                };
            }
        } returned;
        output.captureMidi = true; output.messages.clear();
        const auto beforeHeldMidi = output.midiCount;
        for (unsigned n = 0u; n < 2300u; ++n) { // Just over three seconds.
            // A Mac instance must produce no loopable hardware commands. On
            // other platforms a dedicated one-way LED route is intentional.
            if (!kHostLedOutput) returned.events.swap(output.messages);
            output.messages.clear();
            if (n % 40u == 0u) {
                // Keep servicing the file worker like a real host. D1 being
                // audible does not imply D2's asynchronous load is published.
                instrument->on_main_thread(instrument);
                clap_event_midi_t pressure {};
                pressure.header = { sizeof(pressure), 0u, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0u };
                pressure.data[0] = n % 80u ? 0xa7u : 0xb7u; // Both NEON pressure dialects.
                pressure.data[1] = 0u; pressure.data[2] = static_cast<uint8_t>(n % 128u);
                returned.events.push_back(pressure);
            }
            ok = processStereo(instrument, returned, output, left, right) && ok;
            if (kHostLedOutput) returned.events.clear();
        }
        output.captureMidi = false; output.messages.clear();
        send(0x96u, 0x06u); send(0xb6u, 0x07u, 10u); // CHOP LOOP moves selected cell cursor.
        StateBuffer afterHold;
        ok = expect(state->save(instrument, &afterHold.output)
            && stateDouble(afterHold, kEditPositionOffset + 24u * sizeof(double)) > 0.09
            && stateDouble(afterHold, kEditPositionOffset + 25u * sizeof(double)) == 0.0,
            "A long D1 Grains hold changed selection to D2 when MIDI output returned") && ok;
#if defined(__APPLE__)
        ok = expect(output.midiCount == beforeHeldMidi, "Mac LED feedback leaked into the host MIDI graph") && ok;
#else
        (void)beforeHeldMidi;
#endif
        send(0x87u, 0x00u, 0u);
        for (unsigned n = 0u; n < 80u; ++n) send(0x90u, 127u, 0u);
        ok = expect(maximumMagnitude(left) == 0.0f, "D1 Grains was stranded after releasing its physical pad") && ok;
        // Do not "fix" echoes by rejecting color-valued velocities or by
        // locking selection while another pad is held: both are valid playing.
        send(0x97u, 0x00u, 127u); send(0x97u, 0x01u, 48u);
        send(0x87u, 0x00u, 0u);
        bool secondHeld = false;
        for (unsigned n = 0u; n < 150u; ++n) {
            send(0x90u, 127u, 0u);
            if (n > 40u) secondHeld = secondHeld || maximumMagnitude(left) > 0.00001f;
        }
        ok = expect(secondHeld, "Releasing D1 stopped a genuinely held D2") && ok;
        send(0x96u, 0x06u); send(0xb6u, 0x07u, 10u);
        StateBuffer realSecond;
        ok = expect(state->save(instrument, &realSecond.output)
            && stateDouble(realSecond, kEditPositionOffset + 25u * sizeof(double)) > 0.09,
            "Real D2 press at velocity 48 did not select its waveform") && ok;
        send(0x87u, 0x01u, 0u);
        for (unsigned n = 0u; n < 80u; ++n) send(0x90u, 127u, 0u);
        ok = expect(maximumMagnitude(left) == 0.0f, "D2 Grains did not release") && ok;
        instrument->stop_processing(instrument);
        instrument->deactivate(instrument);
        instrument->destroy(instrument);
    }

    const clap_plugin_t* multichannel = factory ? factory->create_plugin(
        factory, &host.host, "org.s3g.s3g-dsp.sample-neon") : nullptr;
    ok = expect(multichannel && multichannel->init(multichannel),
        "32-channel instance did not initialize") && ok;
    const auto* multiPorts = multichannel
        ? static_cast<const clap_plugin_audio_ports_t*>(
            multichannel->get_extension(multichannel, CLAP_EXT_AUDIO_PORTS))
        : nullptr;
    clap_audio_port_info_t multiPort {};
    ok = expect(multiPorts
            && multiPorts->get(multichannel, 0u, false, &multiPort)
            && multiPort.channel_count == 32u,
        "multichannel descriptor did not expose 32 channels") && ok;
    const auto* multiState = multichannel
        ? static_cast<const clap_plugin_state_t*>(
            multichannel->get_extension(multichannel, CLAP_EXT_STATE)) : nullptr;
    constexpr std::array<uint32_t, 5u> widths {{ 4u, 8u, 4u, 9u, 16u }};
    for (std::size_t test = 0u; multiState && test < widths.size(); ++test) {
        const auto channels = widths[test];
        const auto fixturePath = std::filesystem::temp_directory_path()
            / ("s3g-neon-mc-" + std::to_string(serial) + "-" + std::to_string(test) + ".wav");
        ok = expect(writeFixtureWave(fixturePath, channels, true), "could not write MC WAV") && ok;
        StateBuffer spatial;
        spatial.bytes = saved.bytes;
        const unsigned bus = 32u / channels; // Last complete bus, never wrap.
        setStateParameter(spatial, 0u, static_cast<double>(test + 2u));
        setStateParameter(spatial, 19u, bus);
        setStateParameter(spatial, 23u, test >= 2u ? 1.0 : 0.0);
        setStateParameter(spatial, 24u, 0.0); // Exact, unsnapped multichannel crop fixture.
        setStateParameter(spatial, 10u, 0.125);
        setStateParameter(spatial, 11u, 0.875);
        spatial.bytes[sliceCountsOffset] = 3u;
        const auto boundaryOffset = kPerformanceStateOffset + 4u * 32u + 32u * sizeof(double);
        const std::array<double, 4u> boundaries {{ 0.0, 0.25, 0.75, 1.0 }};
        std::memcpy(spatial.bytes.data() + boundaryOffset, boundaries.data(), sizeof(boundaries));
        const std::size_t pathOffset = sizeof(StateHeader) + kParameterCount * sizeof(double);
        std::snprintf(reinterpret_cast<char*>(spatial.bytes.data() + pathOffset),
            kPathBytes, "%s", fixturePath.c_str());
        ok = expect(multiState->load(multichannel, &spatial.input), "MC state load failed") && ok;
        StateBuffer roundTrip;
        ok = expect(multiState->save(multichannel, &roundTrip.output)
                && stateDouble(roundTrip, sizeof(StateHeader)) == static_cast<double>(test + 2u)
                && stateDouble(roundTrip, sizeof(StateHeader) + 19u * sizeof(double)) == bus
                && stateDouble(roundTrip, sizeof(StateHeader) + 23u * sizeof(double)) == (test >= 2u ? 1.0 : 0.0),
            "layout, bus or source format did not round-trip") && ok;
        const bool activated = multichannel->activate(multichannel, 48000.0, 1u, 64u);
        const bool started = activated && multichannel->start_processing(multichannel);
        ok = expect(started, "MC processor did not activate") && ok;
        std::array<std::vector<float>, 32u> renderedChannels;
        bool audible = false;
        const auto first = (bus - 1u) * channels;
        for (unsigned attempt = 0u; started && attempt < 200u && !audible; ++attempt) {
            multichannel->on_main_thread(multichannel);
            audible = processChannels(multichannel, midi, output, renderedChannels)
                && maximumMagnitude(renderedChannels[first]) > 0.001f;
            if (!audible) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ok = expect(audible, "asynchronous MC decoder/route produced no audio") && ok;
        if (audible) {
            for (uint32_t channel = 0u; channel < 32u; ++channel)
                for (std::size_t frame = 0u; frame < 64u; ++frame) {
                    const float expected = channel >= first && channel < first + channels
                        ? renderedChannels[first][frame] * static_cast<float>(channel - first + 1u) : 0.0f;
                    ok = expect(std::abs(renderedChannels[channel][frame] - expected) < 1.0e-5f,
                        "MC decode reordered, downmixed or lost a channel") && ok;
                }
        }
        if (audible && test == 4u) {
            int mcPage=-1;
            const auto mcSend = [&](uint8_t note) {
                MidiInput input;
                input.performanceGesture(0x97,note,127,mcPage);
                return processChannels(multichannel, input, output, renderedChannels);
            };
            multichannel->reset(multichannel);
            mcSend(0x1du); // Capture bus 2 = channels 17-32 for 3OA.
            processChannels(multichannel, recordAndPlay, output, renderedChannels);
            mcPage=7; // recordAndPlay explicitly ends on secondary RESAMPLE.
            mcSend(0x19u);
            multichannel->on_main_thread(multichannel);
            StateBuffer recorded;
            const bool savedCapture = multiState->save(multichannel, &recorded.output);
            const auto expectedBytes = expectedStateBytes + 16u * 64u * sizeof(float);
            ok = expect(savedCapture && recorded.bytes.size() == expectedBytes,
                "3OA bus capture did not preserve all 16 channels") && ok;
            if (recorded.bytes.size() == expectedBytes) {
                for (unsigned channel = 0u; channel < 16u; ++channel) {
                    float firstSample = 0.0f, sample = 0.0f;
                    std::memcpy(&firstSample, recorded.bytes.data() + expectedStateBytes + 32u * sizeof(float), sizeof(float));
                    std::memcpy(&sample, recorded.bytes.data() + expectedStateBytes
                        + (channel * 64u + 32u) * sizeof(float), sizeof(float));
                    ok = expect(firstSample > 0.001f && std::abs(sample - firstSample * (channel + 1u)) < 1.0e-5f,
                        "3OA capture downmixed, reordered or decorrelated source channels") && ok;
                }
            }
        }
        if (audible) {
            MidiInput assignAll;
            int assignPage=-1;
            assignAll.performanceGesture(0x97,0x6f,127,assignPage); // Keep source; A2-A4 become standalone samples.
            processChannels(multichannel, assignAll, output, renderedChannels);
            multichannel->on_main_thread(multichannel);
            StateBuffer chops;
            ok = expect(multiState->save(multichannel, &chops.output), "MC slice assignment could not save") && ok;
            const std::array<uint32_t, 4u> edges {{ 512u, 1280u, 2816u, 3584u }};
            for (unsigned slice = 0u; slice < 3u; ++slice) {
                const unsigned cell = slice + 1u;
                const auto sample = embeddedAudio(chops, expectedStateBytes, cell);
                const auto paramOffset = sizeof(StateHeader) + (7u + cell * 19u) * sizeof(double);
                const bool shape = sample.channels == channels && sample.frames == edges[slice + 1u] - edges[slice];
                ok = expect(shape && stateDouble(chops, paramOffset + 3u * sizeof(double)) == 0.0
                    && stateDouble(chops, paramOffset + 4u * sizeof(double)) == 1.0
                    && stateDouble(chops, paramOffset + 12u * sizeof(double)) == bus
                    && stateDouble(chops, paramOffset + 16u * sizeof(double)) == (test >= 2u ? 1.0 : 0.0),
                    "MC crop lost its extent, channel count, output route or ACN/SN3D format") && ok;
                bool exact = shape;
                for (unsigned channel = 0u; shape && channel < channels; ++channel)
                    for (unsigned frame = 0u; frame < sample.frames; ++frame) {
                        float value = 0.0f;
                        std::memcpy(&value, chops.bytes.data() + sample.pcm
                            + (channel * sample.frames + frame) * sizeof(float), sizeof(float));
                        const float expected = static_cast<float>((256u + (edges[slice] + frame) % 1024u) * (channel + 1u)) / 32768.0f;
                        exact = exact && value == expected;
                    }
                ok = expect(exact, "MC slice copy changed samples, boundaries or channel alignment") && ok;
            }
            ok = expect(stateDouble(chops, sizeof(StateHeader) + 10u * sizeof(double)) == 0.125
                && stateDouble(chops, sizeof(StateHeader) + 11u * sizeof(double)) == 0.875
                && std::strcmp(reinterpret_cast<const char*>(chops.bytes.data() + pathOffset), fixturePath.c_str()) == 0,
                "Keep Source altered the source cell") && ok;
            // Drop the source locator before recall: slices must need only their embedded PCM.
            std::fill_n(chops.bytes.data() + pathOffset, kPathBytes, 0u);
            ok = expect(multiState->load(multichannel, &chops.input), "MC slices failed to reload without the original source") && ok;
            multichannel->reset(multichannel);
            assignPage=-1;
            assignAll.performanceGesture(0x97,0x01,127,assignPage); // A2 ordinary PLAY.
            ok = expect(processChannels(multichannel, assignAll, output, renderedChannels)
                && maximumMagnitude(renderedChannels[first + channels - 1u]) > 0.001f,
                "MC committed slice failed to play after source-free recall") && ok;
            if (test == 4u) {
                // CHOP A2 again: Assign One must copy the selected second half
                // of this new sample, not coordinates from the original source.
                StateBuffer rechop;
                multiState->save(multichannel, &rechop.output);
                rechop.bytes[sliceCountsOffset + 1u] = 2u;
                const std::array<double, 3u> halves {{ 0.0, 0.5, 1.0 }};
                std::memcpy(rechop.bytes.data() + boundaryOffset + 33u * sizeof(double), halves.data(), sizeof(halves));
                ok = expect(multiState->load(multichannel, &rechop.input), "re-chop fixture failed to load") && ok;
                assignPage=-1;
                assignAll.performanceGesture(0x97,0x01,127,assignPage); // Select A2 in PLAY after restore.
                processChannels(multichannel, assignAll, output, renderedChannels);
                assignAll.performanceGesture(0x97,0x09,127,assignPage); // CHOP slice 2.
                processChannels(multichannel, assignAll, output, renderedChannels);
                assignAll.performanceGesture(0x97,0x6e,127,assignPage); // Assign One to now-empty A1.
                processChannels(multichannel, assignAll, output, renderedChannels);
                multichannel->on_main_thread(multichannel);
                StateBuffer resliced;
                ok = expect(multiState->save(multichannel, &resliced.output), "re-chopped sample did not save") && ok;
                const auto result = embeddedAudio(resliced, expectedStateBytes, 0u);
                const auto original = embeddedAudio(resliced, expectedStateBytes, 1u);
                bool exact = result.channels == 16u && result.frames == 384u && original.frames == 768u;
                for (unsigned channel = 0u; exact && channel < 16u; ++channel)
                    exact = std::memcmp(resliced.bytes.data() + result.pcm + channel * 384u * sizeof(float),
                        resliced.bytes.data() + original.pcm + (channel * 768u + 384u) * sizeof(float),
                        384u * sizeof(float)) == 0;
                ok = expect(exact, "Assign One did not commit the selected slice of a previously committed sample") && ok;
            }
        }
        if (started) multichannel->stop_processing(multichannel);
        if (activated) multichannel->deactivate(multichannel);
        std::error_code ignored;
        std::filesystem::remove(fixturePath, ignored);
    }
    if (multichannel) multichannel->destroy(multichannel);

    // Version 15: 32 independent layers in one 3OA cell. Construct the public
    // serialized format, then test the actual binary's recall and MIDI render.
    const auto* stacked = factory->create_plugin(factory, &host.host, "org.s3g.s3g-dsp.sample-neon");
    ok = expect(stacked && stacked->init(stacked), "stack instance init") && ok;
    if (stacked) {
        const auto* stackState = static_cast<const clap_plugin_state_t*>(stacked->get_extension(stacked, CLAP_EXT_STATE));
        StateBuffer fixture; fixture.bytes.assign(emptyProjectState.begin(), emptyProjectState.begin() + expectedStateBytes);
        const auto append = [&](const auto& value) {
            const auto* first = reinterpret_cast<const uint8_t*>(&value);
            fixture.bytes.insert(fixture.bytes.end(), first, first + sizeof(value));
        };
        setStateParameter(fixture, 0u, 6.0); // 3OA ACN/SN3D.
        setStateParameter(fixture, 1u, 0.0); setStateParameter(fixture, 7u, 0.0);
        setStateParameter(fixture, 4u, 1.0); // Decode native NEON performance notes.
        setStateParameter(fixture, 23u, 1.0);
        const uint8_t embedMode = 2u; append(embedMode);
        for (unsigned pad = 0u; pad < 32u; ++pad) {
            const std::array<uint8_t, 4u> choices {{static_cast<uint8_t>(pad ? 0u : 32u), 0u, static_cast<uint8_t>(pad ? 0u : 2u), 2u}};
            append(choices); const float seconds = 4.0f, beats = 8.0f; append(seconds); append(beats);
            if (pad) continue;
            for (unsigned layer = 0u; layer < 32u; ++layer) {
                const std::array<double, 4u> edit {{0.0, 1.0, 0.0, 120.0}};
                const std::array<uint8_t, 4u> chop {{1u, 2u, 2u, 32u}};
                const float lead = 0.0f; std::array<double, 33u> edges {}; edges[1] = 1.0;
                const uint8_t relative = 0u; const uint32_t length = 0u;
                append(edit); append(chop); append(lead); append(edges); append(relative); append(length);
            }
        }
        for (unsigned asset = 0u; asset < 1025u; ++asset) {
            const uint32_t channels = asset < 32u ? 16u : 0u, frames = asset < 32u ? 512u : 0u;
            const double rate = channels ? 48000.0 : 0.0;
            append(channels); append(frames); append(rate);
            for (unsigned ch = 0u; ch < channels; ++ch) for (unsigned frame = 0u; frame < frames; ++frame) {
                const float value = static_cast<float>((asset + 1u) * (ch + 1u)) * 0.001f; append(value);
            }
        }
        ok = expect(stackState->load(stacked, &fixture.input), "32-layer ACN stack state load") && ok;
        StateBuffer savedStack;
        ok = expect(stackState->save(stacked, &savedStack.output) && savedStack.bytes[4] == 15u,
            "stack state must save extended format") && ok;
        ok = expect(stackState->load(stacked, &savedStack.input), "extended stack reload") && ok;
        StateBuffer roundTrip;
        ok = expect(stackState->save(stacked, &roundTrip.output) && roundTrip.bytes == savedStack.bytes,
            "all layer/source/scan settings must round-trip exactly") && ok;
        StateBuffer corrupt; corrupt.bytes = savedStack.bytes;
        corrupt.bytes[expectedStateBytes + 1u] = 33u; // Excess count must reject atomically.
        ok = expect(!stackState->load(stacked, &corrupt.input), "oversized stack accepted") && ok;
        StateBuffer protectedState;
        ok = expect(stackState->save(stacked, &protectedState.output) && protectedState.bytes == savedStack.bytes,
            "invalid stack state modified the current sound") && ok;
        const bool active = stacked->activate(stacked, 48000.0, 1u, 256u);
        {
            using namespace s3g::sample;
            StateBuffer extended; extended.bytes = savedStack.bytes; extended.bytes[4] = 17u;
            std::array<NeonFamilySettings, 32u> family;
            for (unsigned pad = 0; pad < 32; ++pad) {
                family[pad][NeonFamily::MotionSound] = float(pad % 3u);
                family[pad][NeonFamily::GrainProcess] = float(pad % 5u);
                family[pad][NeonFamily::StackShape] = 0;
                family[pad].values[neonFamilyIndex(NeonFamily::PathValue) + 8] = .8f;
                const auto* bytes = reinterpret_cast<const uint8_t*>(family[pad].values.data());
                extended.bytes.insert(extended.bytes.end(), bytes, bytes + kNeonFamilyV17Count * sizeof(float));
            }
            ok = expect(stackState->load(stacked, &extended.input), "family v17 state load") && ok;
            StateBuffer again;
            ok = expect(stackState->save(stacked, &again.output) && again.bytes == extended.bytes, "all pads family/path state roundtrip") && ok;
            {
                StateBuffer old; old.bytes = savedStack.bytes; old.bytes[4] = 16u;
                std::array<NeonFamilySettings,32> expected;
                for (unsigned pad = 0; pad < 32; ++pad) {
                    const size_t entry = expectedStateBytes + 1u + pad * 12u + (pad ? 32u * 309u : 0u);
                    old.bytes[entry + 3] = static_cast<uint8_t>(pad % 4);
                    auto f = family[pad]; f[NeonFamily::StackShape] = float(pad % 2); // old PRESET/BREAKPOINTS flag
                    f[NeonFamily::StackCurve] = .35f;
                    for (unsigned n = 0; n < 32; ++n) {
                        f.values[neonFamilyIndex(NeonFamily::PathTime)+n] = float(n)/31;
                        f.values[neonFamilyIndex(NeonFamily::PathValue)+n] = .1f + float(n)/40;
                    }
                    const auto* bytes = reinterpret_cast<const uint8_t*>(f.values.data());
                    old.bytes.insert(old.bytes.end(), bytes, bytes + kNeonFamilyV17Count * sizeof(float));
                    expected[pad] = f;
                    if (pad % 2) expected[pad][NeonFamily::StackShape] = 0;
                    else neonSetStackShape(expected[pad], neonLegacyStackShape(pad % 4), 32, pad);
                }
                ok = expect(stackState->load(stacked,&old.input), "v16 preset/manual migration") && ok;
                StateBuffer migrated;
                ok = expect(stackState->save(stacked,&migrated.output) && migrated.bytes[4] == 17u, "migrated path shape metadata saved") && ok;
                const auto base = migrated.bytes.size() - 32u * kNeonFamilyV17Count * sizeof(float);
                for (unsigned pad = 0; pad < 32; ++pad)
                    ok = expect(std::memcmp(migrated.bytes.data()+base+pad*kNeonFamilyV17Count*sizeof(float),expected[pad].values.data(),kNeonFamilyV17Count*sizeof(float)) == 0,
                        "manual points must survive; legacy paths must become shape points") && ok;
                StateBuffer malformed; malformed.bytes = old.bytes;
                const float unsupported = 2;
                std::memcpy(malformed.bytes.data()+savedStack.bytes.size(), &unsupported, sizeof(unsupported));
                ok = expect(!stackState->load(stacked,&malformed.input), "v16 unknown path flag rejected") && ok;
                extended.cursor = 0;
                ok = expect(stackState->load(stacked,&extended.input), "restore new family shape state") && ok;
            }
            StateBuffer invalid; invalid.bytes = extended.bytes;
            const float nan = std::numeric_limits<float>::quiet_NaN();
            std::memcpy(invalid.bytes.data() + savedStack.bytes.size(), &nan, sizeof(nan));
            ok = expect(!stackState->load(stacked, &invalid.input), "nonfinite family state accepted") && ok;
            invalid.bytes = extended.bytes; invalid.cursor = 0;
            const float duplicate = 0;
            std::memcpy(invalid.bytes.data() + savedStack.bytes.size() + (neonFamilyIndex(NeonFamily::PathTime) + 1) * sizeof(float), &duplicate, sizeof(duplicate));
            ok = expect(!stackState->load(stacked, &invalid.input), "duplicate path time accepted") && ok;
            StateBuffer protectedFamily;
            ok = expect(stackState->save(stacked, &protectedFamily.output) && protectedFamily.bytes == extended.bytes, "invalid family state changed sound") && ok;
            const size_t textureOffset = expectedStateBytes
                - kTextureStateBytes - kTransientStateBytes - kPlaybackStateBytes - kCaptureStateBytes - kModernStateBytes;
            for (unsigned method = 0; method < 2; ++method) for (unsigned process = 0; process < 5; ++process) {
                StateBuffer sound; sound.bytes = extended.bytes;
                sound.bytes[textureOffset] = method ? kGrainsOption : kMotionOption;
                sound.bytes[textureOffset + kTextureStateBytes + kTransientStateBytes] = 0; // FREE
                sound.bytes[expectedStateBytes + 3u] = 4; // STACK SCAN
                setStateParameter(sound, 7u + 15u, 1); // HOLD
                auto controls = family[0];
                controls[NeonFamily::MotionSound] = 2; controls[NeonFamily::MotionModel] = float(process);
                controls[NeonFamily::GrainProcess] = float(process); controls[NeonFamily::GrainWindow] = 5;
                controls[NeonFamily::GrainPitch] = 12; controls[NeonFamily::GrainSizeScale] = 8;
                controls[NeonFamily::GrainDensityScale] = 2; controls[NeonFamily::GrainScatter] = .8f;
                controls[NeonFamily::GrainSizeVariation] = .8f;
                std::memcpy(sound.bytes.data() + savedStack.bytes.size(), controls.values.data(), kNeonFamilyV17Count * sizeof(float));
                ok = expect(stackState->load(stacked, &sound.input), "family process fixture load") && ok;
                const bool running = active && stacked->start_processing(stacked);
                MidiInput gesture; OutputEvents feedback; std::array<std::vector<float>,32> output;
                bool audible = false, linked = true;
                for (unsigned block = 0; running && block < 400; ++block) {
                    ok = processChannels(stacked,gesture,feedback,output) && ok;
                    gesture.list.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
                    for (unsigned i = 0; i < output[0].size(); ++i) {
                        audible |= std::abs(output[0][i]) > 1e-6;
                        for (unsigned ch = 1; ch < 16; ++ch)
                            linked &= std::isfinite(output[ch][i]) && std::abs(output[ch][i] - output[0][i] * (ch + 1)) < 1e-4;
                    }
                }
                if (!running || !audible || !linked) std::cerr << "family scenario method=" << method << " process=" << process
                    << " running=" << running << " audible=" << audible << " linked=" << linked << '\n';
                ok = expect(running && audible && linked, "family plugin processing must remain audible and ACN-linked") && ok;
                gesture.list.size = [](const clap_input_events_t*) -> uint32_t { return 2; };
                gesture.events[1].data[0] = 0x87u; gesture.events[1].data[2] = 0;
                if (running) ok = processChannels(stacked,gesture,feedback,output) && ok;
                if (running) stacked->stop_processing(stacked);
            }
            // New LANES append-only controls use v18; old family states above
            // stay byte-identical v17 rather than silently shifting pad tails.
            StateBuffer lanes; lanes.bytes = savedStack.bytes; lanes.bytes[4] = 18;
            lanes.bytes[textureOffset] = 1u << 6u;
            lanes.bytes[textureOffset + kTextureStateBytes + kTransientStateBytes] = 0;
            setStateParameter(lanes, 7u + 15u, 1); // HOLD
            for (unsigned pad = 0; pad < 32; ++pad) {
                NeonFamilySettings f;
                f[NeonFamily::LanePosition] = float(pad) / 31;
                f[NeonFamily::LaneRate] = 1.25f;
                f[NeonFamily::LaneAuto] = 1;
                const auto* bytes = reinterpret_cast<const uint8_t*>(f.values.data());
                lanes.bytes.insert(lanes.bytes.end(), bytes, bytes + kNeonFamilyV19Count*sizeof(float));
            }
            ok = expect(stackState->load(stacked, &lanes.input), "Lanes v18 state load") && ok;
            StateBuffer savedLanes;
            ok = expect(stackState->save(stacked, &savedLanes.output) && savedLanes.bytes == lanes.bytes,
                "Lanes state roundtrip preserves all 32 pads") && ok;
            StateBuffer routing; routing.bytes=savedStack.bytes; routing.bytes[4]=20;
            for(unsigned pad=0;pad<32;++pad) {
                NeonFamilySettings f;
                f[NeonFamily::SliceAttack]=.12f; f[NeonFamily::SliceDecay]=.2f;
                f[NeonFamily::SliceSustain]=.4f; f[NeonFamily::SliceRelease]=.3f;
                f[NeonFamily::RoutingMode]=float(pad%2); f[NeonFamily::RoutingWidth]=float((pad+1)%2);
                f[NeonFamily::RoutingTraversal]=float(pad%5); f[NeonFamily::GrainStereoLink]=1;
                const auto* bytes=reinterpret_cast<const uint8_t*>(f.values.data());
                routing.bytes.insert(routing.bytes.end(),bytes,bytes+kNeonFamilyV21Count*sizeof(float));
            }
            const std::array<float,3> defaultFill {{2,2,.5f}};
            const auto* defaultFillBytes=reinterpret_cast<const uint8_t*>(defaultFill.data());
            routing.bytes.insert(routing.bytes.end(),defaultFillBytes,defaultFillBytes+sizeof(defaultFill));
            ok=expect(stackState->load(stacked,&routing.input),"v20 slice ADSR and object routing state loads")&&ok;
            StateBuffer savedRouting;
            ok=expect(stackState->save(stacked,&savedRouting.output)&&savedRouting.bytes==routing.bytes,
                "v20 retains all 32 pads, routing choices and slice envelopes byte-identically")&&ok;
            StateBuffer badRouting; badRouting.bytes=routing.bytes;
            const float badTraversal=5;
            std::memcpy(badRouting.bytes.data()+savedStack.bytes.size()+neonFamilyIndex(NeonFamily::RoutingTraversal)*sizeof(float),&badTraversal,sizeof(float));
            ok=expect(!stackState->load(stacked,&badRouting.input),"invalid routing traversal rejected atomically")&&ok;
            StateBuffer stillRouting;
            ok=expect(stackState->save(stacked,&stillRouting.output)&&stillRouting.bytes==routing.bytes,
                "invalid routing restore preserves current audio state")&&ok;
            StateBuffer distributed; distributed.bytes=routing.bytes;
            setStateParameter(distributed,0,2); // Quad output, first bus.
            setStateParameter(distributed,7u+12u,1); setStateParameter(distributed,7u+16u,0); // Discrete 16-channel source.
            distributed.bytes[textureOffset]=kGrainsOption;
            distributed.bytes[textureOffset+kTextureStateBytes+kTransientStateBytes]=0; // FREE.
            const float enabledRoute=1;
            std::memcpy(distributed.bytes.data()+savedStack.bytes.size()+neonFamilyIndex(NeonFamily::RoutingMode)*sizeof(float),&enabledRoute,sizeof(float));
            ok=expect(stackState->load(stacked,&distributed.input),"distributed wide-source state load")&&ok;
            const bool distributedRunning=active&&stacked->start_processing(stacked);
            MidiInput routeGesture; OutputEvents routeFeedback; std::array<std::vector<float>,32> routedOutput;
            bool distributedAudible=false, busSafe=true;
            for(unsigned block=0;distributedRunning&&block<400;++block) {
                ok=processChannels(stacked,routeGesture,routeFeedback,routedOutput)&&ok;
                routeGesture.list.size=[](const clap_input_events_t*)->uint32_t{return 0;};
                for(unsigned ch=0;ch<32;++ch)for(float value:routedOutput[ch]) {
                    if(ch<4)distributedAudible|=std::abs(value)>1.e-6f;
                    else busSafe&=value==0;
                }
            }
            ok=expect(distributedRunning&&distributedAudible&&busSafe,"distributed grains fold into selected bus without channel leaks")&&ok;
            if(distributedRunning)stacked->stop_processing(stacked);
            lanes.cursor=0;
            ok=expect(stackState->load(stacked,&lanes.input),"old v18 reload resets new controls to Preserve Field/default slice envelope")&&ok;
            StateBuffer badLane; badLane.bytes = lanes.bytes;
            const float invalidRate = 0;
            std::memcpy(badLane.bytes.data() + savedStack.bytes.size() + neonFamilyIndex(NeonFamily::LaneRate) * sizeof(float), &invalidRate, sizeof(float));
            ok = expect(!stackState->load(stacked, &badLane.input), "invalid Lanes rate accepted") && ok;
            StateBuffer safeLanes;
            ok = expect(stackState->save(stacked, &safeLanes.output) && safeLanes.bytes == lanes.bytes, "invalid Lanes restore changed state") && ok;
            const bool lanesRunning = active && stacked->start_processing(stacked);
            MidiInput gesture; OutputEvents laneFeedback; std::array<std::vector<float>,32> laneOutput;
            bool lanesAudible = false, lanesLinked = true;
            for (unsigned block = 0; lanesRunning && block < 400; ++block) {
                ok = processChannels(stacked, gesture, laneFeedback, laneOutput) && ok;
                gesture.list.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
                for (unsigned frame = 0; frame < laneOutput[0].size(); ++frame) {
                    lanesAudible |= std::abs(laneOutput[0][frame]) > 1e-6;
                    for (unsigned ch = 1; ch < 16; ++ch) lanesLinked &= std::isfinite(laneOutput[ch][frame])
                        && std::abs(laneOutput[ch][frame] - laneOutput[0][frame] * (ch + 1)) < 1e-4;
                }
            }
            if (lanesRunning) stacked->stop_processing(stacked);
            ok = expect(lanesRunning && lanesAudible && lanesLinked, "Lanes processing remains audible and ACN-linked") && ok;
            StateBuffer fillState; fillState.bytes = savedLanes.bytes; fillState.bytes[4] = 19;
            const std::array<float,3> fillControls {{2,2,0}};
            const auto* fillBytes = reinterpret_cast<const uint8_t*>(fillControls.data());
            fillState.bytes.insert(fillState.bytes.end(),fillBytes,fillBytes+sizeof(fillControls));
            ok = expect(stackState->load(stacked,&fillState.input),"Fill v19 loads") && ok;
            StateBuffer fillRoundtrip;
            ok = expect(stackState->save(stacked,&fillRoundtrip.output) && fillRoundtrip.bytes == fillState.bytes,"Fill v19 roundtrip") && ok;
            StateBuffer badFill; badFill.bytes = fillState.bytes; const float invalidFill = 6;
            std::memcpy(badFill.bytes.data()+badFill.bytes.size()-8,&invalidFill,4);
            ok = expect(!stackState->load(stacked,&badFill.input),"Fill invalid repeat rejected") && ok;
            StateBuffer safeFill;
            ok = expect(stackState->save(stacked,&safeFill.output) && safeFill.bytes == fillState.bytes,"Fill invalid state rejection atomic") && ok;
            const bool filling = stacked->start_processing(stacked);
            MidiInput fillMidi; MidiInput noMidi; noMidi.list.size = [](const clap_input_events_t*) { return 0u; };
            const auto fillBlock = [&](auto& input) {
                ok = processChannels(stacked,input,laneFeedback,laneOutput) && ok;
                for (unsigned n=0;n<64;++n) for (unsigned ch=1;ch<16;++ch)
                    ok = expect(std::abs(laneOutput[ch][n]-laneOutput[0][n]*(ch+1)) < 1e-4,"Fill ACN linkage") && ok;
            };
            int fillPage=-1;
            const auto control = [&](uint8_t status,uint8_t key,uint8_t value) {
                fillMidi.performanceGesture(status,key,value,fillPage);
                fillBlock(fillMidi);
            };
            if (filling) {
                fillBlock(fillMidi);
                for (unsigned n=0;n<200;++n) fillBlock(noMidi);
                control(0x93,0x55,127); // SHIFT+CENSOR.
                control(0x87,0,0); // Stop the held pad; frozen output must keep sounding.
                for (unsigned n=0;n<20;++n) fillBlock(noMidi);
                ok = expect(maximumMagnitude(laneOutput[0]) > 1e-5,"Shift Censor replaces now-silent live output") && ok;
                control(0x97,0x38,127); control(0x87,0x38,0); // SHIFT+REC while fill overrides silent live pads.
                for(unsigned n=0;n<8;++n)fillBlock(noMidi);
                control(0x97,0x38,127); control(0x87,0x38,0); stacked->on_main_thread(stacked);
                StateBuffer printedFill;
                ok = stackState->save(stacked,&printedFill.output) && ok;
                constexpr unsigned printedFrames = 10u * 64u;
                const size_t printedBytes = printedFrames * 16u * sizeof(float);
                bool printedAudible=false,printedLinked=true;
                if(printedFill.bytes.size() == fillRoundtrip.bytes.size()+printedBytes) {
                    const size_t pcm = printedFill.bytes.size()-32u*kNeonFamilyV19Count*sizeof(float)-12u-printedBytes;
                    for(unsigned frame=0;frame<printedFrames;++frame) {
                        float first=0;std::memcpy(&first,printedFill.bytes.data()+pcm+frame*4u,4);
                        printedAudible |= std::abs(first)>1e-5;
                        for(unsigned ch=1;ch<16;++ch) {
                            float v=0;std::memcpy(&v,printedFill.bytes.data()+pcm+(ch*printedFrames+frame)*4u,4);
                            printedLinked &= std::abs(v-first*(ch+1))<1e-4;
                        }
                    }
                }
                ok = expect(printedAudible && printedLinked,"resample prints frozen fill, not silent live mix, with all ACN channels") && ok;
                control(0xb6,0x49,1); control(0xb6,0x45,1); // Shift encoders edit global fill, not selected pad.
                StateBuffer editedFill; stackState->save(stacked,&editedFill.output);
                float repeat=0,breakup=0;
                std::memcpy(&repeat,editedFill.bytes.data()+editedFill.bytes.size()-8,4);
                std::memcpy(&breakup,editedFill.bytes.data()+editedFill.bytes.size()-4,4);
                ok = expect(repeat == 3 && std::abs(breakup-.01f)<1e-6,"held fill consumes LOOP/TRAX") && ok;
                control(0x84,0x10,0); // SHIFT released first, release addressed to another bank.
                for (unsigned n=0;n<6;++n) fillBlock(noMidi);
                ok = expect(maximumMagnitude(laneOutput[0]) < 1e-8,"unshifted cross-bank release cannot strand fill") && ok;
                // The same physical top-right button is MODE on primary
                // SAMPLE (96,52 shifted), and CENSOR on every deck page.
                // Include both release encodings and a page change while held.
                for (unsigned release = 0; release < 3; ++release) {
                    control(0x97,0,127);
                    for (unsigned n=0;n<100;++n) fillBlock(noMidi);
                    control(0x96,0x52,127);
                    control(0x87,0,0);
                    for (unsigned n=0;n<20;++n) fillBlock(noMidi);
                    ok = expect(maximumMagnitude(laneOutput[0])>1e-5,
                        "primary SAMPLE Shift MODE must freeze recent output") && ok;
                    // SLIP release must not end a hold on MODE/CENSOR.
                    control(0x83,0x11,0);
                    for (unsigned n=0;n<6;++n) fillBlock(noMidi);
                    ok = expect(maximumMagnitude(laneOutput[0])>1e-5,
                        "unrelated utility release must not stop Sample fill") && ok;
                    if (release == 2) control(0x94,0x06,127);
                    control(release == 2 ? 0x84 : 0x86,
                        release == 0 ? 0x52 : release == 1 ? 0x0d : 0x10,0);
                    for (unsigned n=0;n<6;++n) fillBlock(noMidi);
                    ok = expect(maximumMagnitude(laneOutput[0])<1e-8,
                        "Sample fill releases with Shift held, released first, or page changed") && ok;
                }
                using namespace s3g::controller::neon_midi;
                struct BridgeInput {
                    clap_input_events_t list {}; clap_event_midi_sysex_t event {}; BridgePacket packet {};
                } bridge;
                bridge.list.ctx=&bridge; bridge.list.size=[](const clap_input_events_t*){return 1u;};
                bridge.list.get=[](const clap_input_events_t* list,uint32_t)->const clap_event_header_t*{return &static_cast<BridgeInput*>(list->ctx)->event.header;};
                bridge.event.header={sizeof(bridge.event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
                bridge.event.buffer=bridge.packet.data(); bridge.event.size=15;
                control(0x97,0,127); for(unsigned n=0;n<100;++n) fillBlock(noMidi);
                bridge.packet=encodeBridge({BridgeKind::Control,0,36,0,{0x93,0x55,127},0}); fillBlock(bridge);
                control(0x87,0,0); for(unsigned n=0;n<20;++n) fillBlock(noMidi);
                bool bridgeAudible=maximumMagnitude(laneOutput[0])>1e-5;
                bridge.packet=encodeBridge({BridgeKind::Control,0,36,0,{0x83,0x55,0},0}); fillBlock(bridge);
                for(unsigned n=0;n<6;++n) fillBlock(noMidi);
                ok = expect(bridgeAudible && maximumMagnitude(laneOutput[0])<1e-8,"Utility/Tracker private bridge carries fill press and release") && ok;
                struct DualBridgeInput {
                    clap_input_events_t list {}; clap_event_midi_sysex_t event {}; AddressedBridgePacket packet {};
                } dual;
                dual.list.ctx=&dual; dual.list.size=[](const clap_input_events_t*){return 1u;};
                dual.list.get=[](const clap_input_events_t* list,uint32_t)->const clap_event_header_t*{return &static_cast<DualBridgeInput*>(list->ctx)->event.header;};
                dual.event.header={sizeof(dual.event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0};
                dual.event.buffer=dual.packet.data(); dual.event.size=21;
                const auto dualControl = [&](uint8_t unit, BridgeKind kind, uint8_t status, uint8_t key, uint8_t value) {
                    dual.packet=encodeAddressedBridge({kind,0,36,0,{status,key,value},0,true,unit,unit?341027543:2116083961});
                    fillBlock(dual);
                };
                control(0x97,0,127); for(unsigned n=0;n<100;++n) fillBlock(noMidi);
                dualControl(0,BridgeKind::Control,0x93,0x55,127);
                dualControl(1,BridgeKind::Control,0x96,0x52,127);
                dualControl(0,BridgeKind::Control,0x87,0,0);
                for(unsigned n=0;n<20;++n) fillBlock(noMidi);
                dualControl(1,BridgeKind::Disconnect,0,0,0);
                for(unsigned n=0;n<6;++n) fillBlock(noMidi);
                ok = expect(maximumMagnitude(laneOutput[0])>1e-5,"U2 disconnect cannot release U1's held fill") && ok;
                dualControl(0,BridgeKind::Control,0x86,0x0d,0);
                for(unsigned n=0;n<6;++n) fillBlock(noMidi);
                ok = expect(maximumMagnitude(laneOutput[0])<1e-8,"last USB-unit fill release restores silent live output") && ok;
                bridge.packet=encodeBridge({BridgeKind::Sync,0,36,0,{},0}); fillBlock(bridge);
                stacked->stop_processing(stacked);
            }
            for (unsigned variant = 0; variant < 5; ++variant) {
                StateBuffer modernPlayback; modernPlayback.bytes = savedStack.bytes; modernPlayback.bytes[4] = variant == 4 ? 25 : variant == 3 ? 23 : 22;
                modernPlayback.bytes[textureOffset] = variant == 4 ? 2 : variant == 1 ? 32 : variant == 2 ? 8 : 128;
                modernPlayback.bytes[textureOffset + kTextureStateBytes + kTransientStateBytes] = 0;
                modernPlayback.bytes[expectedStateBytes + 2u] = 0;
                modernPlayback.bytes[expectedStateBytes + 3u] = variant == 2 ? 0 : 4;
                setStateParameter(modernPlayback, 7u + 15u, 1); // HOLD
                if (variant == 1) setStateParameter(modernPlayback, 7u + 16u, 0); // discrete source
                for (unsigned pad = 0; pad < 32; ++pad) {
                    NeonFamilySettings f;
                    f[NeonFamily::SpectralBlur] = .7f; f[NeonFamily::SpectralAdvance] = .25f;
                    f[NeonFamily::MosaicMode] = 2; f[NeonFamily::MosaicScope] = 1;
                    f[NeonFamily::WavesetEngine] = 1; f[NeonFamily::OscFrequency] = 220;
                    if (variant == 3) {
                        f[NeonFamily::SpectralSmear] = 1.25f; f[NeonFamily::SpectralFocus] = -.4f;
                        f[NeonFamily::SpectralTilt] = 3; f[NeonFamily::SpectralThin] = .7f;
                    }
                    if (variant == 4) {
                        f[NeonFamily::CutRegions]=32; f[NeonFamily::CutRate]=80;
                        f[NeonFamily::CutRepeat]=2; f[NeonFamily::CutFileOrder]=0;
                        f.values[neonFamilyIndex(NeonFamily::CutPatternLane)+63]=31;
                        f.values[neonFamilyIndex(NeonFamily::CutPatternSource)+63]=.75f;
                    }
                    const auto* bytes = reinterpret_cast<const uint8_t*>(f.values.data());
                    modernPlayback.bytes.insert(modernPlayback.bytes.end(), bytes,
                        bytes + (variant == 4 ? kNeonFamilyCount : variant == 3 ? kNeonFamilyV23Count : kNeonFamilyV22Count) * sizeof(float));
                }
                modernPlayback.bytes.insert(modernPlayback.bytes.end(), defaultFillBytes, defaultFillBytes + sizeof(defaultFill));
                for (unsigned pad = 0; pad < 32; ++pad) for (unsigned effect = 0; effect < 8; ++effect) {
                    const auto values = neonCharacterDefaults(effect);
                    const auto* bytes = reinterpret_cast<const uint8_t*>(values.data() + 3);
                    modernPlayback.bytes.insert(modernPlayback.bytes.end(), bytes, bytes + (values.size() - 3) * sizeof(float));
                }
                if (variant == 4) {
                    modernPlayback.bytes.push_back(0); modernPlayback.bytes.push_back(1);
                    for (unsigned n=0;n<32;++n) modernPlayback.bytes.push_back(static_cast<uint8_t>(36+n));
                }
                ok = expect(stackState->load(stacked, &modernPlayback.input), "v22/v23/v25 playback loads") && ok;
                StateBuffer recalled;
                ok = expect(stackState->save(stacked, &recalled.output) && recalled.bytes == modernPlayback.bytes,
                    "v22/v23/v25 preserves controls, sources and Character banks byte-identically") && ok;
                StateBuffer bad; bad.bytes = modernPlayback.bytes;
                const float invalid = 2001;
                std::memcpy(bad.bytes.data() + savedStack.bytes.size() + neonFamilyIndex(NeonFamily::OscFrequency) * sizeof(float), &invalid, sizeof(float));
                ok = expect(!stackState->load(stacked, &bad.input), "v22 invalid engine values reject atomically") && ok;
                if (variant == 3) for (auto key : {NeonFamily::SpectralSmear, NeonFamily::SpectralFocus,
                        NeonFamily::SpectralTilt, NeonFamily::SpectralThin}) {
                    StateBuffer invalidColour; invalidColour.bytes = modernPlayback.bytes;
                    const float value = neonFamilyDef(neonFamilyIndex(key)).maximum + 1;
                    std::memcpy(invalidColour.bytes.data() + savedStack.bytes.size() + neonFamilyIndex(key) * sizeof(float), &value, sizeof(float));
                    ok = expect(!stackState->load(stacked, &invalidColour.input), "v23 rejects out-of-range spectral colour") && ok;
                    StateBuffer unchanged;
                    ok = expect(stackState->save(stacked, &unchanged.output) && unchanged.bytes == modernPlayback.bytes,
                        "invalid v23 load leaves all pad controls intact") && ok;
                }
                MidiInput gesture; OutputEvents feedback; std::array<std::vector<float>, 32> output;
                const bool running = active && stacked->start_processing(stacked);
                ok = expect(running, "v22 engine starts") && ok;
                float mosaicPeak = 0; bool mosaicLinked = true;
                for (unsigned block = 0; running && block < 400; ++block) {
                    ok = processChannels(stacked, gesture, feedback, output) && ok;
                    gesture.list.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
                    for (const auto& channel : output) for (float value : channel) ok = std::isfinite(value) && ok;
                    if (variant == 2 || variant == 4) {
                        mosaicPeak = std::max(mosaicPeak, maximumMagnitude(output[0]));
                        for (unsigned ch = 1; ch < 16; ++ch) for (unsigned i = 0; i < output[0].size(); ++i)
                            mosaicLinked &= std::abs(output[ch][i] - output[0][i] * (ch + 1)) < 1.e-5f;
                    }
                }
                if (variant == 2 || variant == 4) ok = expect(mosaicPeak > .002f && mosaicLinked,
                    "recalled Mosaic/Cutups plays stack audio and preserves ACN channels") && ok;
                if (running) stacked->stop_processing(stacked);
#include "sample_neon_capture_clap_checks.inc"
#include "sample_neon_poly_clap_checks.inc"
            }
            savedStack.cursor = 0;
            ok = expect(stackState->load(stacked, &savedStack.input), "legacy family defaults restore") && ok;
        }
        const bool processing = active && stacked->start_processing(stacked);
        std::array<std::vector<float>, 32u> rendered;
        MidiInput hit; OutputEvents feedback;
        ok = expect(processing && processChannels(stacked, hit, feedback, rendered)
            && maximumMagnitude(rendered[0]) > 0.02f
            && std::abs(maximumMagnitude(rendered[15]) - 16.0f * maximumMagnitude(rendered[0])) < 1.0e-5f,
            "velocity layer 32 must play with all 16 ACN channels linked") && ok;
        if (processing) stacked->stop_processing(stacked);
        if (active) stacked->deactivate(stacked);
        // A discrete stack's cycle maps are rebuilt asynchronously from saved
        // PCM. No GUI needs to be opened to analyze or play Wavesets.
        StateBuffer waveStack;
        const size_t metadataEnd = expectedStateBytes + 1u + 32u * 12u + 32u * 309u;
        waveStack.bytes.assign(savedStack.bytes.begin(), savedStack.bytes.begin() + metadataEnd);
        setStateParameter(waveStack, 0u, 0.0); setStateParameter(waveStack, 23u, 0.0);
        setStateParameter(waveStack, 7u + 15u, 1.0);
        waveStack.bytes[methodOffset] = 1u << 5u;
        waveStack.bytes[expectedStateBytes + 3u] = 4u;
        const float fastScan = 0.1f;
        std::memcpy(waveStack.bytes.data() + expectedStateBytes + 5u, &fastScan, sizeof(fastScan));
        const auto waveAppend = [&](const auto& value) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            waveStack.bytes.insert(waveStack.bytes.end(), bytes, bytes + sizeof(value));
        };
        for (unsigned layer = 0u; layer < 1025u; ++layer) {
            const uint32_t channels = layer < 32u ? 2u : 0u, frames = channels ? 512u : 0u;
            const double rate = channels ? 48000.0 : 0.0;
            waveAppend(channels); waveAppend(frames); waveAppend(rate);
            for (unsigned ch = 0u; ch < channels; ++ch) for (unsigned f = 0u; f < frames; ++f) {
                const float x = static_cast<float>(ch + 1u) * 0.15f * std::sin(static_cast<float>(f) * (0.07f + layer * 0.002f));
                waveAppend(x);
            }
        }
        ok = expect(stackState->load(stacked, &waveStack.input), "Wavesets scan state load") && ok;
        const bool waveActive = stacked->activate(stacked, 48000.0, 1u, 256u);
        const bool waveRunning = waveActive && stacked->start_processing(stacked);
        MidiInput quiet;
        for (auto& event : quiet.events) { event.data[0] = 0x90u; event.data[1] = 127u; event.data[2] = 0u; }
        bool waveHeard = false;
        for (unsigned n = 0u; waveRunning && n < 200u; ++n) {
            stacked->on_main_thread(stacked);
            ok = processChannels(stacked, n ? quiet : hit, feedback, rendered) && ok;
            waveHeard |= maximumMagnitude(rendered[0]) > 0.01f;
            if (!waveHeard) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ok = expect(waveHeard, "worker-analyzed Wavesets stack must sound without a GUI") && ok;
        MidiInput waveOff; waveOff.events[1].data[0] = 0x87u; waveOff.events[1].data[2] = 0u;
        for (unsigned n = 0u; waveRunning && n < 40u; ++n)
            ok = processChannels(stacked, n ? quiet : waveOff, feedback, rendered) && ok;
        ok = expect(maximumMagnitude(rendered[0]) == 0.0f, "Wavesets scan release left stuck audio") && ok;
        if (waveRunning) stacked->stop_processing(stacked);
        if (waveActive) stacked->deactivate(stacked);
        const auto projectRoot = std::filesystem::temp_directory_path() / ("s3g-neon-project-" + std::to_string(serial));
        std::filesystem::create_directories(projectRoot / "媒体");
        projectSimulation.directory = (projectRoot / "媒体").u8string(); projectSimulation.enabled = true;
        StateBuffer projectFixture; projectFixture.bytes = savedStack.bytes;
        projectFixture.bytes[expectedStateBytes] = 0u; // PROJECT, generated PCM must be collected safely.
        ok = expect(stackState->load(stacked, &projectFixture.input), "PROJECT stack fixture load") && ok;
        for (unsigned attempt = 0u; attempt < 200u && projectSimulation.additions < 32u; ++attempt) {
            stacked->on_main_thread(stacked);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        StateBuffer collected;
        ok = expect(projectSimulation.additions == 32u && stackState->save(stacked, &collected.output)
            && collected.bytes.size() < savedStack.bytes.size() / 2u,
            "PROJECT must collect every generated layer, register media and remove PCM from state") && ok;
        stacked->destroy(stacked);
        ok = expect(projectSimulation.removals == projectSimulation.additions, "project file registrations must balance") && ok;
        std::filesystem::rename(projectRoot / "媒体", projectRoot / "relocated");
        projectSimulation.directory = (projectRoot / "relocated").u8string();
        const auto* relocated = factory->create_plugin(factory, &host.host, "org.s3g.s3g-dsp.sample-neon");
        ok = expect(relocated && relocated->init(relocated), "relocated stack init") && ok;
        if (relocated) {
            const auto* stateAgain = static_cast<const clap_plugin_state_t*>(relocated->get_extension(relocated, CLAP_EXT_STATE));
            ok = expect(stateAgain->load(relocated, &collected.input), "relative PROJECT stack recall") && ok;
            const bool ready = relocated->activate(relocated, 48000.0, 1u, 64u);
            const bool running = ready && relocated->start_processing(relocated);
            bool heard = false;
            for (unsigned attempt = 0u; running && !heard && attempt < 200u; ++attempt) {
                relocated->on_main_thread(relocated);
                heard = processChannels(relocated, hit, feedback, rendered) && maximumMagnitude(rendered[0]) > 0.02f;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            ok = expect(heard, "relocated PROJECT layer 32 must play without its original directory") && ok;
            if (running) relocated->stop_processing(relocated);
            if (ready) relocated->deactivate(relocated);
            relocated->destroy(relocated);
        }
        projectSimulation.enabled = false;
        std::error_code cleanupError; std::filesystem::remove_all(projectRoot, cleanupError);
    }

    std::error_code removeError;
    std::filesystem::remove(wavePath, removeError);
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (std::getenv("S3G_NEON_ALLOCATION_PROBE"))
        std::cout << "Allocation-probed process callbacks: " << allocationProbeBlocks << '\n';
    if (!ok) return 1;
    std::cout << "Sample Neon CLAP checks passed\n";
    return 0;
}
