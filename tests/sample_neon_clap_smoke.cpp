#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

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
    std::array<clap_event_midi_t, 2u> events {};
    clap_input_events_t list {};

    MidiInput()
    {
        events[0u].header.size = sizeof(clap_event_midi_t);
        events[0u].header.time = 0u;
        events[0u].header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        events[0u].header.type = CLAP_EVENT_MIDI;
        events[0u].port_index = 0u;
        events[0u].data[0u] = 0x93u;
        events[0u].data[1u] = 0x00u;
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

struct HostContext {
    clap_host_t host {};

    HostContext()
    {
        host.clap_version = CLAP_VERSION_INIT;
        host.name = "Sample Neon smoke";
        host.vendor = "s3g";
        host.url = "https://github.com/s3g/s3g-dsp";
        host.version = "1";
        host.get_extension = [](const clap_host_t*, const char*)
            -> const void* { return nullptr; };
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
    OutputEvents& output, std::array<std::vector<float>, 32u>& channels)
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
    return plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
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
            && ports->count(instrument, true) == 0u
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
    const std::size_t expectedStateBytes = sizeof(StateHeader)
        + kParameterCount * sizeof(double) + 32u * kPathBytes
        + kSliceModeBytes + kSliceCountBytes + kSliceOptionBytes
        + kPerformanceStateBytes + kSlicerDomainStateBytes
        + kTextureStateBytes + kTransientStateBytes + kPlaybackStateBytes + kModernStateBytes + kCaptureStateBytes;
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
        setStateParameter(saved, 18u, 5.0); // slot A1 Gate character
        const std::size_t pathOffset = sizeof(StateHeader)
            + kParameterCount * sizeof(double);
        std::snprintf(reinterpret_cast<char*>(saved.bytes.data() + pathOffset),
            kPathBytes, "%s", wavePath.c_str());
        const std::size_t sliceModeOffset = pathOffset + 32u * kPathBytes;
        saved.bytes[sliceModeOffset] = 2u;
        const std::size_t sliceCountOffset = sliceModeOffset
            + kSliceModeBytes;
        saved.bytes[sliceCountOffset] = 17u;
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
        ok = expect(std::equal(saved.bytes.begin() + modernOffset, saved.bytes.begin() + modernOffset + kModernStateBytes,
            modeRoundTrip.bytes.begin() + modernOffset), "Per-effect and per-technique settings did not round-trip") && ok;

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
    cursorInput.events[0u].data[1u] = 0x07u; // deck A Hot Cue
    cursorInput.events[0u].data[2u] = 0x7fu;
    cursorInput.events[1u].data[0u] = 0xb6u;
    cursorInput.events[1u].data[1u] = 0x04u; // deck A LOOP
    cursorInput.events[1u].data[2u] = 0x0au; // +10 relative steps
    OutputEvents cursorOutput;
    std::vector<float> cursorLeft;
    std::vector<float> cursorRight;
    ok = expect(processStereo(instrument, cursorInput, cursorOutput,
            cursorLeft, cursorRight),
        "Hot Cue cursor encoder process failed") && ok;
    StateBuffer cursorState;
    ok = expect(state->save(instrument, &cursorState.output)
            && std::abs(stateDouble(cursorState, kEditPositionOffset) - 0.10)
                < 1.0e-9,
        "Hot Cue LOOP encoder did not move the edit cursor") && ok;

    MidiInput midi;
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

    const auto send = [&](uint8_t status, uint8_t note, uint8_t velocity = 127u) {
        MidiInput input;
        input.list.size = [](const clap_input_events_t*) { return 1u; };
        input.events[0u].data[0u] = status;
        input.events[0u].data[1u] = note;
        input.events[0u].data[2u] = velocity;
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
    // Compare the actual instrument output for ordinary velocity with the
    // hardware's separate CC + fixed-127 note sequence across host blocks.
    for (uint8_t key : {0u, 0x10u}) { // PLAY and EDIT audition.
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

    // Toggle remains a performance gesture, not a forced edit retrigger.
    send(0x93u, 0x00u); // Bank A
    change(1015u, 3.0);
    instrument->reset(instrument);
    ok = expect(send(0x97u, 0x78u) && maximumMagnitude(left) > 0.001f,
        "RESAMPLE Toggle did not start") && ok;
    send(0x87u, 0x78u, 0u);
    ok = expect(maximumMagnitude(left) > 0.001f, "RESAMPLE Toggle stopped on release") && ok;
    send(0x97u, 0x78u);
    for (unsigned block = 0u; block < 8u; ++block) send(0x90u, 127u, 0u);
    ok = expect(maximumMagnitude(left) == 0.0f, "RESAMPLE Toggle retriggered instead of stopping") && ok;
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
    send(0x97u, 0x11u); // EDIT selects A2 (the captured cell).
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
        readyForReplace = send(0x97u, 0x10u) && maximumMagnitude(left) > 0.001f;
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
        send(0x96u, 0x07u); send(0xb6u, 0x07u, 10u); // EDIT/Source LOOP moves selected cell cursor.
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
        send(0x96u, 0x07u); send(0xb6u, 0x07u, 10u);
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
            const auto mcSend = [&](uint8_t note) {
                MidiInput input;
                input.list.size = [](const clap_input_events_t*) { return 1u; };
                input.events[0u].data[0u] = 0x97u;
                input.events[0u].data[1u] = note;
                return processChannels(multichannel, input, output, renderedChannels);
            };
            multichannel->reset(multichannel);
            mcSend(0x1du); // Capture bus 2 = channels 17-32 for 3OA.
            processChannels(multichannel, recordAndPlay, output, renderedChannels);
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
            assignAll.list.size = [](const clap_input_events_t*) { return 1u; };
            assignAll.events[0u].data[0u] = 0x97u;
            assignAll.events[0u].data[1u] = 0x6fu; // Keep source; A2-A4 become standalone samples.
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
            assignAll.events[0u].data[1u] = 0x01u; // A2 ordinary PLAY.
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
                assignAll.events[0u].data[1u] = 0x01u; // Select A2 in PLAY after restore.
                processChannels(multichannel, assignAll, output, renderedChannels);
                assignAll.events[0u].data[1u] = 0x09u; // CHOP slice 2.
                processChannels(multichannel, assignAll, output, renderedChannels);
                assignAll.events[0u].data[1u] = 0x6eu; // Assign One to now-empty A1.
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

    std::error_code removeError;
    std::filesystem::remove(wavePath, removeError);
    if (entry) entry->deinit();
    if (library) dlclose(library);
    if (!ok) return 1;
    std::cout << "Sample Neon CLAP checks passed\n";
    return 0;
}
