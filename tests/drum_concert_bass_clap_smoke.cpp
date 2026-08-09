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
#include <iostream>
#include <vector>

namespace {

constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.drum-concert-bass";
constexpr uint32_t kFrames = 256u;

enum ParamId : clap_id {
    kTune = 1u, kNoteTracking, kSize, kHeadTension, kStrikePosition,
    kBeaterHardness, kImpact, kBody, kBodyDecay, kDamping, kBloom, kAir,
    kShell, kShellTone, kMutedDecay, kRimLevel, kRimTone, kRimDecay,
    kDrive, kBias, kCompression, kRateReduction, kBitDepth,
    kReconstruction, kCharacterTone, kStereoWidth, kVelocity,
    kOutput, kTrigger, kMidiReceive,
};

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum, maximum, defaultValue;
};

constexpr std::array<ParamSpec, 30u> kSpecs {{
    { kTune, "Tune", 24.0, 96.0, 44.0 },
    { kNoteTracking, "Note Tracking", 0.0, 1.0, 0.50 },
    { kSize, "Form", 0.0, 1.0, 0.56 },
    { kHeadTension, "Head Tension", 0.0, 1.0, 0.45 },
    { kStrikePosition, "Strike Position", 0.0, 1.0, 0.20 },
    { kBeaterHardness, "Beater Hardness", 0.0, 1.0, 0.34 },
    { kImpact, "Impact", 0.0, 1.0, 0.58 },
    { kBody, "Body", 0.0, 1.0, 0.88 },
    { kBodyDecay, "Body Decay", 0.15, 8.0, 3.20 },
    { kDamping, "Damping", 0.0, 1.0, 0.22 },
    { kBloom, "Bloom", 0.0, 1.0, 0.58 },
    { kAir, "Air", 0.0, 1.0, 0.62 },
    { kShell, "Boundary", 0.0, 1.0, 0.28 },
    { kShellTone, "Boundary Tone", 0.0, 1.0, 0.38 },
    { kMutedDecay, "Muted Decay", 0.04, 1.5, 0.32 },
    { kRimLevel, "Rim Level", 0.0, 1.0, 0.55 },
    { kRimTone, "Rim Tone", 0.0, 1.0, 0.35 },
    { kRimDecay, "Rim Decay", 0.015, 1.0, 0.14 },
    { kDrive, "Drive", 0.0, 1.0, 0.0 },
    { kBias, "Bias", -1.0, 1.0, 0.0 },
    { kCompression, "Compression", 0.0, 1.0, 0.0 },
    { kRateReduction, "Rate Reduction", 0.0, 1.0, 0.0 },
    { kBitDepth, "Bit Depth Reduction", 0.0, 1.0, 0.0 },
    { kReconstruction, "Reconstruction", 0.0, 1.0, 0.0 },
    { kCharacterTone, "Character Tone", -1.0, 1.0, 0.0 },
    { kStereoWidth, "Stereo Width", 0.0, 1.0, 0.16 },
    { kVelocity, "Velocity Sensitivity", 0.0, 1.0, 0.90 },
    { kOutput, "Output Gain", -36.0, 12.0, -6.0 },
    { kMidiReceive, "MIDI Receive", 0.0, 16.0, 0.0 },
    { kTrigger, "Trigger", 0.0, 3.0, 0.0 },
}};

struct Host {
    clap_host_t host {};
    clap_host_params_t params {};
    clap_host_tail_t tail {};
    std::atomic<uint32_t> processRequests { 0u };
    std::atomic<uint32_t> tailChanges { 0u };
};

Host* hostData(const clap_host_t* host)
{
    return static_cast<Host*>(host->host_data);
}

const void* hostExtension(const clap_host_t* host, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &hostData(host)->params;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &hostData(host)->tail;
    return nullptr;
}

void requestRestart(const clap_host_t*) {}
void requestProcess(const clap_host_t* host) { ++hostData(host)->processRequests; }
void requestCallback(const clap_host_t*) {}
void rescan(const clap_host_t*, clap_param_rescan_flags) {}
void clear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void requestFlush(const clap_host_t*) {}
void tailChanged(const clap_host_t* host) { ++hostData(host)->tailChanges; }

struct Events {
    std::array<clap_event_param_value_t, 8u> params {};
    std::array<clap_event_note_t, 4u> notes {};
    std::array<clap_event_midi_t, 4u> midi {};
    std::array<const clap_event_header_t*, 16u> entries {};
    uint32_t paramCount = 0u, noteCount = 0u, midiCount = 0u, count = 0u;
    clap_input_events_t list {
        this,
        [](const clap_input_events_t* list) {
            return static_cast<const Events*>(list->ctx)->count;
        },
        [](const clap_input_events_t* list, uint32_t index) {
            const auto* self = static_cast<const Events*>(list->ctx);
            return index < self->count ? self->entries[index] : nullptr;
        },
    };

    bool insert(const clap_event_header_t* event)
    {
        if (!event || count >= entries.size()) return false;
        uint32_t index = count;
        while (index > 0u && entries[index - 1u]->time > event->time) {
            entries[index] = entries[index - 1u];
            --index;
        }
        entries[index] = event;
        ++count;
        return true;
    }

    bool param(clap_id id, double value, uint32_t time = 0u)
    {
        if (paramCount >= params.size()) return false;
        auto& event = params[paramCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1; event.port_index = -1;
        event.channel = -1; event.key = -1; event.value = value;
        return insert(&event.header);
    }

    bool note(int16_t key, double velocity, uint32_t time = 0u,
        int16_t channel = 0)
    {
        if (noteCount >= notes.size()) return false;
        auto& event = notes[noteCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_NOTE_ON;
        event.note_id = key; event.port_index = 0;
        event.channel = channel; event.key = key; event.velocity = velocity;
        return insert(&event.header);
    }

    bool midiNote(uint8_t key, uint8_t velocity, uint8_t channel = 0u)
    {
        if (midiCount >= midi.size()) return false;
        auto& event = midi[midiCount++];
        event = {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_MIDI;
        event.port_index = 0u;
        event.data[0] = static_cast<uint8_t>(0x90u | (channel & 0x0fu));
        event.data[1] = key; event.data[2] = velocity;
        return insert(&event.header);
    }
};

struct Audio {
    std::array<std::array<float, kFrames>, 2u> samples {};
    std::array<float*, 2u> pointers {{ samples[0].data(), samples[1].data() }};
    clap_audio_buffer_t buffer {};
    Audio() { buffer.data32 = pointers.data(); buffer.channel_count = 2u; }
    void clear() { samples[0].fill(0.0f); samples[1].fill(0.0f); }
    double energy(uint32_t begin = 0u, uint32_t end = kFrames) const
    {
        double sum = 0.0;
        for (const auto& channel : samples) for (uint32_t i = begin; i < end; ++i)
            sum += static_cast<double>(channel[i]) * channel[i];
        return sum;
    }
    bool finite() const
    {
        for (const auto& channel : samples) for (float x : channel)
            if (!std::isfinite(x)) return false;
        return true;
    }
    double peak() const
    {
        double maximum = 0.0;
        for (const auto& channel : samples) for (float sample : channel) {
            maximum = std::max(maximum,
                std::abs(static_cast<double>(sample)));
        }
        return maximum;
    }
};

clap_process_status run(const clap_plugin_t* plugin, Audio& audio,
    const Events* events = nullptr)
{
    clap_process_t process {};
    process.steady_time = -1;
    process.frames_count = kFrames;
    process.audio_outputs = &audio.buffer;
    process.audio_outputs_count = 1u;
    process.in_events = events ? &events->list : nullptr;
    return plugin->process(plugin, &process);
}

struct MemoryState { std::vector<uint8_t> bytes; size_t offset = 0u; };
int64_t writeState(const clap_ostream_t* stream, const void* data,
    uint64_t size)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const auto* bytes = static_cast<const uint8_t*>(data);
    const size_t count = std::min<size_t>(size, 7u);
    state->bytes.insert(state->bytes.end(), bytes, bytes + count);
    return static_cast<int64_t>(count);
}
int64_t readState(const clap_istream_t* stream, void* data, uint64_t size)
{
    auto* state = static_cast<MemoryState*>(stream->ctx);
    const size_t count = std::min<size_t>({ static_cast<size_t>(size), 5u,
        state->bytes.size() - std::min(state->offset, state->bytes.size()) });
    std::memcpy(data, state->bytes.data() + state->offset, count);
    state->offset += count;
    return static_cast<int64_t>(count);
}

std::filesystem::path resolve(const std::filesystem::path& supplied)
{
    std::error_code error;
    if (std::filesystem::is_regular_file(supplied, error)) return supplied;
#if defined(__APPLE__)
    if (std::filesystem::is_directory(supplied, error)) {
        const auto directory = supplied / "Contents" / "MacOS";
        for (const auto& entry : std::filesystem::directory_iterator(directory,
                 error)) if (entry.is_regular_file(error)) return entry.path();
    }
#endif
    return {};
}

bool parameterContract(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params)
{
    if (!params || params->count(plugin) != kSpecs.size()) return false;
    for (uint32_t i = 0u; i < kSpecs.size(); ++i) {
        clap_param_info_t info {};
        const auto& spec = kSpecs[i];
        double value = 0.0;
        if (!params->get_info(plugin, i, &info) || info.id != spec.id
            || std::strcmp(info.name, spec.name) != 0
            || info.min_value != spec.minimum || info.max_value != spec.maximum
            || info.default_value != spec.defaultValue
            || !params->get_value(plugin, spec.id, &value)
            || std::fabs(value - spec.defaultValue) > 1.0e-5) {
            std::cerr << "concert bass parameter contract failed at " << i
                      << ": id " << info.id << " / " << spec.id
                      << ", name " << info.name << " / " << spec.name
                      << ", range " << info.min_value << ".."
                      << info.max_value << ", default " << info.default_value
                      << ", value " << value << "\n";
            return false;
        }
    }
    char text[64] {};
    double value = 0.0;
    return params->value_to_text(plugin, kTrigger, 2.0, text, sizeof(text))
        && std::strcmp(text, "Muted") == 0
        && params->text_to_value(plugin, kTrigger, "Rim", &value)
        && value == 3.0;
}

bool portsAndNotes(const clap_plugin_t* plugin)
{
    const auto* audio = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    const auto* notes = static_cast<const clap_plugin_note_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    const auto* names = static_cast<const clap_plugin_note_name_t*>(
        plugin->get_extension(plugin, CLAP_EXT_NOTE_NAME));
    clap_audio_port_info_t audioInfo {};
    clap_note_port_info_t noteInfo {};
    if (!audio || !notes || !names || audio->count(plugin, false) != 1u
        || audio->count(plugin, true) != 0u
        || !audio->get(plugin, 0u, false, &audioInfo)
        || audioInfo.channel_count != 2u || notes->count(plugin, true) != 1u
        || !notes->get(plugin, 0u, true, &noteInfo)
        || names->count(plugin) != 3u) return false;
    constexpr std::array<int16_t, 3u> expected {{ 35, 36, 37 }};
    for (uint32_t i = 0u; i < expected.size(); ++i) {
        clap_note_name_t name {};
        if (!names->get(plugin, i, &name) || name.key != expected[i]) return false;
    }
    return true;
}

bool rendering(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params)
{
    Audio audio;
    plugin->reset(plugin);
    Events timed;
    timed.note(36, 1.0, 97u);
    audio.clear();
    if (run(plugin, audio, &timed) == CLAP_PROCESS_ERROR || !audio.finite()
        || audio.energy(0u, 97u) != 0.0 || !(audio.energy(97u) > 1.0e-9)) {
        std::cerr << "concert bass sample-accurate note failed\n";
        return false;
    }

    for (int16_t note : { 35, 36, 37 }) {
        plugin->reset(plugin);
        Events strike; strike.note(note, 0.9);
        double total = 0.0, maximum = 0.0;
        for (uint32_t block = 0u; block < 12u; ++block) {
            audio.clear();
            if (run(plugin, audio, block == 0u ? &strike : nullptr)
                    == CLAP_PROCESS_ERROR || !audio.finite()) return false;
            total += audio.energy();
            maximum = std::max(maximum, audio.peak());
        }
        if (!(total > 1.0e-8)) {
            std::cerr << "concert bass articulation was silent " << note << "\n";
            return false;
        }
        if (note == 36) {
            const double rms = std::sqrt(total
                / static_cast<double>(12u * kFrames * 2u));
            if (!(maximum > 0.50 && maximum < 0.80) || !(rms > 0.20)) {
                std::cerr << "concert bass CLAP pressure level failed: peak "
                          << maximum << ", rms " << rms << "\n";
                return false;
            }
        }
    }

    Events mono; mono.param(kStereoWidth, 0.0); mono.note(37, 1.0);
    plugin->reset(plugin); audio.clear(); run(plugin, audio, &mono);
    if (audio.samples[0] != audio.samples[1]) return false;

    // The three transient Trigger values must each produce audio.
    for (uint32_t code = 1u; code <= 3u; ++code) {
        plugin->reset(plugin);
        Events strike; strike.param(kTrigger, static_cast<double>(code));
        audio.clear(); run(plugin, audio, &strike);
        if (!(audio.energy() > 1.0e-9)) return false;
    }

    // MIDI Receive is 1-based in the UI: value 2 accepts only channel index 1.
    Events route; route.param(kMidiReceive, 2.0);
    params->flush(plugin, &route.list, nullptr);
    plugin->reset(plugin);
    Events rejected; rejected.midiNote(36, 110u, 0u);
    audio.clear(); run(plugin, audio, &rejected);
    if (audio.energy() != 0.0) return false;
    plugin->reset(plugin);
    Events accepted; accepted.midiNote(36, 110u, 1u);
    audio.clear(); run(plugin, audio, &accepted);
    return audio.energy() > 1.0e-9;
}

bool stateRoundTrip(const clap_plugin_t* plugin,
    const clap_plugin_params_t* params, const clap_plugin_state_t* state)
{
    Events edit; edit.param(kTune, 52.0); edit.param(kBodyDecay, 4.2);
    edit.param(kMidiReceive, 7.0); edit.param(kOutput, -12.0);
    params->flush(plugin, &edit.list, nullptr);
    MemoryState memory;
    clap_ostream_t output { &memory, writeState };
    if (!state->save(plugin, &output) || memory.bytes.empty()) return false;
    Events change; change.param(kTune, 31.0); change.param(kBodyDecay, 0.3);
    params->flush(plugin, &change.list, nullptr);
    clap_istream_t input { &memory, readState };
    if (!state->load(plugin, &input)) return false;
    double tune = 0.0, decay = 0.0, midi = 0.0, outputGain = 0.0;
    return params->get_value(plugin, kTune, &tune)
        && std::fabs(tune - 52.0) < 1.0e-5
        && params->get_value(plugin, kBodyDecay, &decay)
        && std::fabs(decay - 4.2) < 1.0e-5
        && params->get_value(plugin, kMidiReceive, &midi)
        && std::fabs(midi - 7.0) < 1.0e-5
        && params->get_value(plugin, kOutput, &outputGain)
        && std::fabs(outputGain + 12.0) < 1.0e-5;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: drum_concert_bass_clap_smoke <bundle> <id>\n";
        return 2;
    }
    const auto binary = resolve(argv[1]);
    if (binary.empty()) return 1;
    void* handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) { std::cerr << dlerror() << "\n"; return 1; }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(
        dlsym(handle, "clap_entry"));
    if (!entry || !entry->init(binary.c_str())) return 1;
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory || factory->get_plugin_count(factory) != 1u) return 1;
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0u);
    if (!descriptor || std::strcmp(descriptor->id, kPluginId) != 0
        || std::strcmp(argv[2], kPluginId) != 0) return 1;

    Host host;
    host.host = { CLAP_VERSION_INIT, &host, "s3g smoke", "s3g", "", "0",
        hostExtension, requestRestart, requestProcess, requestCallback };
    host.params = { rescan, clear, requestFlush };
    host.tail = { tailChanged };
    const auto* plugin = factory->create_plugin(factory, &host.host, kPluginId);
    if (!plugin || !plugin->init(plugin)) return 1;
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const auto* tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    bool ok = parameterContract(plugin, params);
    if (ok && !portsAndNotes(plugin)) {
        std::cerr << "concert bass port/note contract failed\n"; ok = false;
    }
    if (ok && (!state || !tail || tail->get(plugin) == 0u)) {
        std::cerr << "concert bass state/tail extension failed\n"; ok = false;
    }
    if (ok && !plugin->activate(plugin, 48000.0, 1u, 1024u)) {
        std::cerr << "concert bass activation failed\n"; ok = false;
    }
    if (ok && !plugin->start_processing(plugin)) {
        std::cerr << "concert bass start processing failed\n"; ok = false;
    }
    if (ok && !rendering(plugin, params)) {
        std::cerr << "concert bass rendering contract failed\n"; ok = false;
    }
    if (ok && !stateRoundTrip(plugin, params, state)) {
        std::cerr << "concert bass state round trip failed\n"; ok = false;
    }
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(handle);
    if (!ok) return 1;
    std::cout << "drum concert bass CLAP smoke passed\n";
    return 0;
}
