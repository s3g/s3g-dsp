#include "s3g_psd_raw_field.h"

#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kChannels = s3g::kPsdRawFieldChannels;
constexpr uint32_t kFrames = 8192u;
constexpr uint32_t kTransportFrames = 512u;
constexpr std::size_t kSourcePathCapacity = 4096u;
constexpr clap_id kRunParamId = 55u;
constexpr clap_id kPerformanceModeParamId = 56u;
constexpr clap_id kAttackParamId = 57u;
constexpr clap_id kDecayParamId = 58u;
constexpr clap_id kSustainParamId = 59u;
constexpr clap_id kReleaseParamId = 60u;
constexpr clap_id kCarrierTuneParamId = 61u;
constexpr clap_id kPresetParamId = 50u;
constexpr clap_id kModSourceParamId = 62u;
constexpr clap_id kModTargetParamId = 63u;
constexpr clap_id kModRateParamId = 64u;
constexpr clap_id kModRatioParamId = 65u;
constexpr clap_id kModIndexParamId = 66u;
constexpr clap_id kModFeedbackParamId = 67u;
constexpr clap_id kModClockLockParamId = 68u;
constexpr clap_id kCodecModeParamId = 15u;
constexpr clap_id kRandomizeFieldParamId = 23u;

struct LegacyParamsV13 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, drive, shred, resonance, gainDb;
    uint32_t seed;
};
static_assert(sizeof(LegacyParamsV13) == 68u, "Unexpected version-13 parameter layout");

struct LegacyParamsV15 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    s3g::PsdRawFieldChannelScheme channelScheme;
    float channelSpread;
    s3g::PsdRawFieldCodecMode codecMode;
    float codecRate, bitDepth, codecDamage, drive, shred, resonance, gainDb;
    uint32_t seed;
    s3g::PsdRawFieldCodecMode fieldCodecMode;
};
static_assert(sizeof(LegacyParamsV15) == 72u, "Unexpected version-15 parameter layout");

LegacyParamsV15 legacyParamsV15(const s3g::PsdRawFieldParams& params)
{
    return {
        params.scanRate, params.texture, params.geometry, params.chaos, params.fold, params.evolve,
        params.channelScheme, params.channelSpread, params.codecMode, params.codecRate, params.bitDepth,
        params.codecDamage, params.drive, params.shred, params.resonance, params.gainDb, params.seed,
        params.fieldCodecMode,
    };
}

struct LegacyParamsV16 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
};
static_assert(sizeof(LegacyParamsV16) == 76u, "Unexpected version-16 parameter layout");

LegacyParamsV16 legacyParamsV16(const s3g::PsdRawFieldParams& params)
{
    return {
        params.scanRate, params.texture, params.geometry, params.chaos, params.fold, params.evolve,
        static_cast<uint32_t>(params.channelScheme), params.channelSpread,
        static_cast<uint32_t>(params.codecMode), params.codecRate, params.bitDepth,
        params.codecDamage, params.carrierTune, params.drive, params.shred, params.resonance,
        params.gainDb, params.seed, static_cast<uint32_t>(params.fieldCodecMode),
    };
}

LegacyParamsV13 legacyParams(const s3g::PsdRawFieldParams& params)
{
    return {
        params.scanRate,
        params.texture,
        params.geometry,
        params.chaos,
        params.fold,
        params.evolve,
        static_cast<uint32_t>(params.channelScheme),
        params.channelSpread,
        static_cast<uint32_t>(params.codecMode),
        params.codecRate,
        params.bitDepth,
        params.codecDamage,
        params.drive,
        params.shred,
        params.resonance,
        params.gainDb,
        params.seed,
    };
}

LegacyParamsV13 legacyParams(const LegacyParamsV15& params)
{
    return {
        params.scanRate, params.texture, params.geometry, params.chaos, params.fold, params.evolve,
        static_cast<uint32_t>(params.channelScheme), params.channelSpread,
        static_cast<uint32_t>(params.codecMode), params.codecRate, params.bitDepth, params.codecDamage,
        params.drive, params.shred, params.resonance, params.gainDb, params.seed,
    };
}

struct SavedStateV14 {
    uint32_t version = 14u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV15 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedStateV14) == 4184u, "Unexpected version-14 Fault state layout");

struct SavedStateV15 {
    uint32_t version = 15u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV15 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedStateV15) == 4204u, "Unexpected version-15 Fault state layout");

struct SavedStateV16 {
    uint32_t version = 16u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV16 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedStateV16) == 4208u, "Unexpected version-16 Fault state layout");

struct SavedStateV17 {
    uint32_t version = 17u;
    uint32_t selectedPreset = 12u;
    s3g::PsdRawFieldParams params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedStateV17) == 4236u, "Unexpected current Fault state layout");

struct SavedStateV13 {
    uint32_t version = 13u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedStateV13) == 4180u, "Unexpected version-13 Fault state layout");

struct SavedStateV12 {
    uint32_t version = 12u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 2u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedStateV12) == 4176u, "Unexpected version-12 Fault state layout");

struct SavedStateV11 {
    uint32_t version = 11u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 1u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedStateV11) == 4176u, "Unexpected legacy Fault state layout");

void append16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8u));
}

void append32(std::vector<uint8_t>& bytes, uint32_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8u));
    bytes.push_back(static_cast<uint8_t>(value >> 16u));
    bytes.push_back(static_cast<uint8_t>(value >> 24u));
}

void appendTag(std::vector<uint8_t>& bytes, const char* tag)
{
    bytes.insert(bytes.end(), tag, tag + 4u);
}

bool writeTestWave(const std::filesystem::path& path)
{
    std::vector<uint8_t> payload;
    appendTag(payload, "WAVE");
    appendTag(payload, "JUNK");
    append32(payload, 10u);
    for (uint8_t i = 0u; i < 10u; ++i) payload.push_back(static_cast<uint8_t>(0xa0u + i));
    appendTag(payload, "fmt ");
    append32(payload, 16u);
    append16(payload, 1u);
    append16(payload, 2u);
    append32(payload, 48000u);
    append32(payload, 48000u * 4u);
    append16(payload, 4u);
    append16(payload, 16u);
    appendTag(payload, "LIST");
    append32(payload, 6u);
    payload.insert(payload.end(), { 'I', 'N', 'F', 'O', 0u, 0u });
    appendTag(payload, "data");
    append32(payload, kFrames * 4u);
    for (uint32_t frame = 0u; frame < kFrames; ++frame) {
        const float left = std::sin(2.0f * s3g::kPi * 220.0f * static_cast<float>(frame) / 48000.0f) * 0.64f;
        const float right = std::sin(2.0f * s3g::kPi * 330.0f * static_cast<float>(frame) / 48000.0f) * 0.52f;
        append16(payload, static_cast<uint16_t>(static_cast<int16_t>(std::round(left * 32767.0f))));
        append16(payload, static_cast<uint16_t>(static_cast<int16_t>(std::round(right * 32767.0f))));
    }

    std::vector<uint8_t> file;
    appendTag(file, "RIFF");
    append32(file, static_cast<uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return output.good();
}

const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequest(const clap_host_t*) {}

struct InputEventList {
    std::vector<const clap_event_header_t*> events;

    void set(const clap_event_header_t* event)
    {
        events.clear();
        if (event) events.push_back(event);
    }
};

uint32_t inputEventCount(const clap_input_events_t* events)
{
    const auto* input = static_cast<const InputEventList*>(events->ctx);
    return input ? static_cast<uint32_t>(input->events.size()) : 0u;
}

const clap_event_header_t* inputEventGet(const clap_input_events_t* events, uint32_t index)
{
    const auto* input = static_cast<const InputEventList*>(events->ctx);
    return input && index < input->events.size() ? input->events[index] : nullptr;
}
bool outputEventPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

struct MemoryInput {
    const uint8_t* bytes = nullptr;
    std::size_t size = 0u;
    std::size_t offset = 0u;
};

struct MemoryOutput {
    std::vector<uint8_t> bytes;
};

int64_t streamRead(const clap_istream_t* stream, void* destination, uint64_t requested)
{
    auto* input = static_cast<MemoryInput*>(stream->ctx);
    const std::size_t count = std::min<std::size_t>(
        static_cast<std::size_t>(requested), input->size - input->offset);
    if (count == 0u) return 0;
    std::memcpy(destination, input->bytes + input->offset, count);
    input->offset += count;
    return static_cast<int64_t>(count);
}

int64_t streamWrite(const clap_ostream_t* stream, const void* source, uint64_t requested)
{
    auto* output = static_cast<MemoryOutput*>(stream->ctx);
    const auto* bytes = static_cast<const uint8_t*>(source);
    output->bytes.insert(output->bytes.end(), bytes, bytes + requested);
    return static_cast<int64_t>(requested);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: s3g_fault_clap_wave_source_smoke <plugin binary>\n";
        return 2;
    }
    const std::filesystem::path wavePath = std::filesystem::temp_directory_path()
        / "s3g_fault_wave_source_smoke.wav";
    if (!writeTestWave(wavePath)) {
        std::cerr << "Could not write the Fault WAVE fixture\n";
        return 1;
    }

    void* library = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        std::cerr << "Could not load Fault: " << dlerror() << "\n";
        std::remove(wavePath.c_str());
        return 1;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
    if (!entry || !entry->init(argv[1])) {
        std::cerr << "Could not initialize Fault's CLAP entry\n";
        dlclose(library);
        std::remove(wavePath.c_str());
        return 1;
    }

    clap_host_t host {};
    host.clap_version = CLAP_VERSION_INIT;
    host.name = "Fault wave source smoke";
    host.vendor = "s3g";
    host.url = "https://github.com/s3g/s3g-dsp";
    host.version = "1";
    host.get_extension = hostGetExtension;
    host.request_restart = hostRequest;
    host.request_process = hostRequest;
    host.request_callback = hostRequest;
    const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* plugin = factory
        ? factory->create_plugin(factory, &host, "org.s3g.s3g-dsp.fault")
        : nullptr;
    if (!plugin || !plugin->init(plugin)) {
        std::cerr << "Could not create Fault\n";
        entry->deinit();
        dlclose(library);
        std::remove(wavePath.c_str());
        return 1;
    }

    SavedStateV15 state {};
    state.params.scanRate = 0.50f;
    state.params.texture = 0.22f;
    state.params.geometry = 0.20f;
    state.params.chaos = 0.18f;
    state.params.fold = 0.14f;
    state.params.evolve = 0.0f;
    state.params.channelScheme = s3g::PsdRawFieldChannelScheme::Deinterleave;
    state.params.channelSpread = 0.92f;
    state.params.codecMode = s3g::PsdRawFieldCodecMode::RawPcm;
    state.params.codecRate = 0.0f;
    state.params.bitDepth = 12.0f;
    state.params.codecDamage = 0.0f;
    state.params.drive = 0.20f;
    state.params.shred = 0.14f;
    state.params.resonance = 0.04f;
    state.params.gainDb = -8.0f;
    state.params.seed = 0x514f97bdu;
    state.params.fieldCodecMode = s3g::PsdRawFieldCodecMode::RawPcm;
    std::snprintf(state.sourcePath, sizeof(state.sourcePath), "%s", wavePath.c_str());
    MemoryInput memory { reinterpret_cast<const uint8_t*>(&state), sizeof(state), 0u };
    clap_istream_t stream { &memory, streamRead };
    const auto* stateExtension = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    const bool loaded = stateExtension && stateExtension->load(plugin, &stream);
    const bool activated = loaded && plugin->activate(plugin, 48000.0, 64u, 512u);
    const bool started = activated && plugin->start_processing(plugin);
    bool ok = started;
    if (ok) {
        const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        clap_note_port_info_t notePort {};
        ok = notePorts && notePorts->count(plugin, true) == 1u
            && notePorts->count(plugin, false) == 0u
            && notePorts->get(plugin, 0u, true, &notePort)
            && notePort.id == 30u
            && (notePort.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0u
            && (notePort.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0u;
        if (!ok) std::cerr << "Fault did not expose its MIDI note input port\n";
    }

    std::array<std::array<float, kFrames>, kChannels> audio {};
    InputEventList inputEventList {};
    clap_input_events_t inputEvents { &inputEventList, inputEventCount, inputEventGet };
    clap_output_events_t outputEvents { nullptr, outputEventPush };
    for (uint32_t offset = 0u; ok && offset < kFrames; offset += 512u) {
        std::array<float*, kChannels> pointers {};
        for (uint32_t ch = 0u; ch < kChannels; ++ch) pointers[ch] = audio[ch].data() + offset;
        clap_audio_buffer_t output {};
        output.data32 = pointers.data();
        output.channel_count = kChannels;
        clap_process_t process {};
        process.steady_time = offset;
        process.frames_count = std::min<uint32_t>(512u, kFrames - offset);
        process.audio_outputs = &output;
        process.audio_outputs_count = 1u;
        process.in_events = &inputEvents;
        process.out_events = &outputEvents;
        ok = plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
    }

    if (ok) {
        constexpr uint32_t lag = 218u;
        double numerator = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        double channelDelta = 0.0;
        for (uint32_t i = 2048u + lag; i < kFrames; ++i) {
            const double a = audio[0][i];
            const double b = audio[0][i - lag];
            numerator += a * b;
            energyA += a * a;
            energyB += b * b;
            channelDelta += std::abs(static_cast<double>(audio[0][i] - audio[7][i]));
        }
        const double correlation = numerator / std::sqrt(energyA * energyB + 1.0e-20);
        ok = correlation > 0.18 && energyA > 0.0001 && channelDelta > 0.01;
        if (!ok) {
            std::cerr << "Fault did not retain an eight-channel waveform trace: correlation="
                      << correlation << " energy=" << energyA
                      << " channelDelta=" << channelDelta << "\n";
        }
    }

    if (ok) {
        std::array<std::array<float, kTransportFrames>, kChannels> transportAudio {};
        uint64_t transportTime = kFrames;
        auto renderTransportBlock = [&](const clap_event_header_t* event) {
            std::array<float*, kChannels> pointers {};
            for (uint32_t ch = 0u; ch < kChannels; ++ch) {
                transportAudio[ch].fill(0.0f);
                pointers[ch] = transportAudio[ch].data();
            }
            clap_audio_buffer_t output {};
            output.data32 = pointers.data();
            output.channel_count = kChannels;
            clap_process_t process {};
            process.steady_time = transportTime;
            process.frames_count = kTransportFrames;
            process.audio_outputs = &output;
            process.audio_outputs_count = 1u;
            inputEventList.set(event);
            process.in_events = &inputEvents;
            process.out_events = &outputEvents;
            const bool processed = plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
            inputEventList.set(nullptr);
            transportTime += kTransportFrames;
            return processed;
        };
        auto blockEnergy = [&]() {
            double energy = 0.0;
            for (const auto& channel : transportAudio) {
                for (float sample : channel) energy += static_cast<double>(sample) * sample;
            }
            return energy;
        };
        auto makeRunEvent = [](double value) {
            clap_event_param_value_t event {};
            event.header.size = sizeof(event);
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.param_id = kRunParamId;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = value;
            return event;
        };

        const clap_event_param_value_t stopEvent = makeRunEvent(0.0);
        ok = renderTransportBlock(&stopEvent.header);
        const double fadeEnergy = blockEnergy();
        ok = ok && renderTransportBlock(nullptr) && renderTransportBlock(nullptr);
        const double stoppedEnergy = blockEnergy();
        const auto* paramsExtension = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        double runValue = -1.0;
        ok = ok && fadeEnergy > 1.0e-10 && stoppedEnergy <= 1.0e-20
            && paramsExtension && paramsExtension->get_value(plugin, kRunParamId, &runValue)
            && runValue == 0.0;
        if (!ok) {
            std::cerr << "Fault STOP did not fade and freeze: fadeEnergy=" << fadeEnergy
                      << " stoppedEnergy=" << stoppedEnergy << " run=" << runValue << "\n";
        }

        const clap_event_param_value_t playEvent = makeRunEvent(1.0);
        if (ok) ok = renderTransportBlock(&playEvent.header) && renderTransportBlock(nullptr);
        const double resumedEnergy = blockEnergy();
        runValue = -1.0;
        ok = ok && resumedEnergy > 1.0e-8
            && paramsExtension->get_value(plugin, kRunParamId, &runValue) && runValue == 1.0;
        if (!ok) {
            std::cerr << "Fault PLAY did not resume: energy=" << resumedEnergy
                      << " run=" << runValue << "\n";
        }
    }

    if (ok) {
        const auto* paramsExtension = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        auto flushParam = [&](clap_id id, double value) {
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
            inputEventList.set(&event.header);
            paramsExtension->flush(plugin, &inputEvents, &outputEvents);
            inputEventList.set(nullptr);
        };
        ok = paramsExtension && paramsExtension->flush;
        if (ok) {
            flushParam(kPerformanceModeParamId, 1.0);
            flushParam(kAttackParamId, 1.0);
            flushParam(kDecayParamId, 5.0);
            flushParam(kSustainParamId, 1.0);
            flushParam(kReleaseParamId, 5.0);
        }

        std::array<std::array<float, kTransportFrames>, kChannels> midiAudio {};
        uint64_t midiTime = kFrames + kTransportFrames * 5u;
        auto renderMidiBlock = [&](const clap_event_header_t* event) {
            std::array<float*, kChannels> pointers {};
            for (uint32_t ch = 0u; ch < kChannels; ++ch) {
                midiAudio[ch].fill(0.0f);
                pointers[ch] = midiAudio[ch].data();
            }
            clap_audio_buffer_t output {};
            output.data32 = pointers.data();
            output.channel_count = kChannels;
            clap_process_t process {};
            process.steady_time = midiTime;
            process.frames_count = kTransportFrames;
            process.audio_outputs = &output;
            process.audio_outputs_count = 1u;
            inputEventList.set(event);
            process.in_events = &inputEvents;
            process.out_events = &outputEvents;
            const bool processed = plugin->process(plugin, &process) != CLAP_PROCESS_ERROR;
            inputEventList.set(nullptr);
            midiTime += kTransportFrames;
            return processed;
        };
        auto rangeEnergy = [&](uint32_t begin, uint32_t end) {
            double energy = 0.0;
            for (const auto& channel : midiAudio) {
                for (uint32_t i = begin; i < end; ++i) {
                    energy += static_cast<double>(channel[i]) * channel[i];
                }
            }
            return energy;
        };

        clap_event_note_t noteOn {};
        noteOn.header.size = sizeof(noteOn);
        noteOn.header.time = 128u;
        noteOn.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        noteOn.header.type = CLAP_EVENT_NOTE_ON;
        noteOn.note_id = -1;
        noteOn.port_index = 30;
        noteOn.channel = 0;
        noteOn.key = 60;
        noteOn.velocity = 0.82;
        if (ok) ok = renderMidiBlock(&noteOn.header);
        const double preNoteEnergy = rangeEnergy(0u, 128u);
        const double attackEnergy = rangeEnergy(128u, kTransportFrames);

        clap_event_note_t noteOff = noteOn;
        noteOff.header.time = 128u;
        noteOff.header.type = CLAP_EVENT_NOTE_OFF;
        noteOff.velocity = 0.0;
        if (ok) ok = renderMidiBlock(&noteOff.header);
        const double heldEnergy = rangeEnergy(0u, 128u);
        const double releaseEnergy = rangeEnergy(128u, kTransportFrames);
        if (ok) ok = renderMidiBlock(nullptr);
        const double idleEnergy = rangeEnergy(0u, kTransportFrames);
        ok = ok && preNoteEnergy <= 1.0e-20 && attackEnergy > 1.0e-8
            && heldEnergy > 1.0e-8 && releaseEnergy > 1.0e-10 && idleEnergy <= 1.0e-20;
        if (!ok) {
            std::cerr << "Fault MIDI ADSR/timing failed: pre=" << preNoteEnergy
                      << " attack=" << attackEnergy << " held=" << heldEnergy
                      << " release=" << releaseEnergy << " idle=" << idleEnergy << "\n";
        }
    }

    if (started) plugin->stop_processing(plugin);
    if (activated) plugin->deactivate(plugin);

    if (ok) {
        MemoryOutput currentOutput;
        clap_ostream_t currentStream { &currentOutput, streamWrite };
        ok = stateExtension->save(plugin, &currentStream)
            && currentOutput.bytes.size() == sizeof(SavedStateV17);
        if (ok) {
            SavedStateV17 saved {};
            std::memcpy(&saved, currentOutput.bytes.data(), sizeof(saved));
            ok = saved.version == 17u && saved.selectedPreset == 12u
                && saved.sourceMode == 2u && saved.runState == 1u
                && saved.params.fieldCodecMode == s3g::PsdRawFieldCodecMode::RawPcm
                && saved.params.carrierTune == 0.0f
                && saved.params.modSource == s3g::PsdRawFieldModSource::Off
                && saved.params.modIndex == 0.0f
                && saved.performanceMode == 1u && saved.attackMs == 1.0f
                && saved.decayMs == 5.0f && saved.sustain == 1.0f && saved.releaseMs == 5.0f;
        }
    }

    if (ok) {
        s3g::PsdRawFieldParams oldParams {};
        oldParams.codecMode = s3g::PsdRawFieldCodecMode::Sstv;
        oldParams.fieldCodecMode = s3g::PsdRawFieldCodecMode::HfFax;
        oldParams.carrierTune = -7.5f;
        SavedStateV16 legacy {};
        legacy.params = legacyParamsV16(oldParams);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV17);
        if (ok) {
            SavedStateV17 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 17u
                && migrated.params.codecMode == s3g::PsdRawFieldCodecMode::Sstv
                && migrated.params.fieldCodecMode == s3g::PsdRawFieldCodecMode::HfFax
                && migrated.params.carrierTune == -7.5f
                && migrated.params.modSource == s3g::PsdRawFieldModSource::Off
                && migrated.params.modTarget == s3g::PsdRawFieldModTarget::Carrier
                && migrated.params.modIndex == 0.0f
                && migrated.params.modFeedback == 0.0f
                && migrated.params.modClockLock == 0u;
        }
        if (!ok) std::cerr << "Fault did not migrate version-16 protocol modulation defaults\n";
    }

    if (ok) {
        SavedStateV14 legacy {};
        legacy.params = state.params;
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV17);
        if (ok) {
            SavedStateV17 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 17u && migrated.selectedPreset == 12u
                && migrated.sourceMode == 2u && migrated.runState == 1u
                && migrated.performanceMode == 0u && migrated.attackMs == 12.0f
                && migrated.decayMs == 280.0f && migrated.sustain == 0.72f
                && migrated.releaseMs == 850.0f;
        }
        if (!ok) std::cerr << "Fault did not migrate version-14 performance state\n";
    }

    if (ok) {
        SavedStateV13 legacy {};
        legacy.params = legacyParams(state.params);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV17);
        if (ok) {
            SavedStateV17 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 17u && migrated.selectedPreset == 12u
                && migrated.sourceMode == 2u && migrated.runState == 1u
                && migrated.params.fieldCodecMode == migrated.params.codecMode
                && migrated.performanceMode == 0u;
        }
        if (!ok) std::cerr << "Fault did not migrate version-13 codec field state\n";
    }

    if (ok) {
        SavedStateV12 legacy {};
        legacy.params = legacyParams(state.params);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV17);
        if (ok) {
            SavedStateV17 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 17u && migrated.selectedPreset == 12u
                && migrated.sourceMode == 2u && migrated.runState == 1u
                && migrated.params.fieldCodecMode == migrated.params.codecMode
                && migrated.performanceMode == 0u;
        }
        if (!ok) std::cerr << "Fault did not migrate version-12 waveform source state\n";
    }

    if (ok) {
        SavedStateV11 legacy {};
        legacy.params = legacyParams(state.params);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV17);
        if (ok) {
            SavedStateV17 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 17u && migrated.selectedPreset == 13u
                && migrated.sourceMode == 1u && migrated.runState == 1u
                && migrated.params.fieldCodecMode == migrated.params.codecMode
                && migrated.performanceMode == 0u;
        }
        if (!ok) std::cerr << "Fault did not preserve version-11 literal-byte source state\n";
    }
    if (ok) {
        SavedStateV15 stoppedState = state;
        stoppedState.runState = 0u;
        MemoryInput stoppedInput {
            reinterpret_cast<const uint8_t*>(&stoppedState), sizeof(stoppedState), 0u
        };
        clap_istream_t stoppedStream { &stoppedInput, streamRead };
        ok = stateExtension->load(plugin, &stoppedStream);
        MemoryOutput stoppedOutput;
        clap_ostream_t stoppedOutputStream { &stoppedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &stoppedOutputStream)
            && stoppedOutput.bytes.size() == sizeof(SavedStateV17);
        if (ok) {
            SavedStateV17 savedStopped {};
            std::memcpy(&savedStopped, stoppedOutput.bytes.data(), sizeof(savedStopped));
            ok = savedStopped.version == 17u && savedStopped.runState == 0u;
        }
        if (!ok) std::cerr << "Fault did not preserve its stopped transport state\n";
    }
    if (ok) {
        const auto* paramsExtension = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        auto flushParam = [&](clap_id id, double value) {
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
            inputEventList.set(&event.header);
            paramsExtension->flush(plugin, &inputEvents, &outputEvents);
            inputEventList.set(nullptr);
        };
        ok = paramsExtension && paramsExtension->flush;
        if (ok) {
            clap_param_info_t codecInfo {};
            bool foundCodec = false;
            for (uint32_t index = 0u; index < paramsExtension->count(plugin); ++index) {
                clap_param_info_t candidate {};
                if (paramsExtension->get_info(plugin, index, &candidate)
                    && candidate.id == kCodecModeParamId) {
                    codecInfo = candidate;
                    foundCodec = true;
                    break;
                }
            }
            char aptText[16] {};
            char sstvText[16] {};
            double aptValue = -1.0;
            double sstvValue = -1.0;
            ok = foundCodec
                && codecInfo.max_value == static_cast<double>(s3g::PsdRawFieldCodecMode::Sstv)
                && paramsExtension->value_to_text(plugin, kCodecModeParamId,
                    static_cast<double>(s3g::PsdRawFieldCodecMode::Apt), aptText, sizeof(aptText))
                && std::strcmp(aptText, "APT") == 0
                && paramsExtension->text_to_value(plugin, kCodecModeParamId, "APT", &aptValue)
                && aptValue == static_cast<double>(s3g::PsdRawFieldCodecMode::Apt)
                && paramsExtension->value_to_text(plugin, kCodecModeParamId,
                    static_cast<double>(s3g::PsdRawFieldCodecMode::Sstv), sstvText, sizeof(sstvText))
                && std::strcmp(sstvText, "SSTV") == 0
                && paramsExtension->text_to_value(plugin, kCodecModeParamId, "SSTV", &sstvValue)
                && sstvValue == static_cast<double>(s3g::PsdRawFieldCodecMode::Sstv);
            if (!ok) std::cerr << "Fault did not expose the full transmission codec range\n";
        }
        if (ok) {
            flushParam(kCodecModeParamId, static_cast<double>(s3g::PsdRawFieldCodecMode::Apt));
            flushParam(kCarrierTuneParamId, 12.0);
            char carrierText[16] {};
            double carrierValue = 0.0;
            ok = paramsExtension->value_to_text(plugin, kCarrierTuneParamId,
                    12.0, carrierText, sizeof(carrierText))
                && std::strcmp(carrierText, "4800 Hz") == 0
                && paramsExtension->text_to_value(plugin, kCarrierTuneParamId,
                    "2400 Hz", &carrierValue)
                && std::abs(carrierValue) < 1.0e-9;
            if (!ok) std::cerr << "Fault did not expose protocol-relative carrier tuning\n";
        }
        if (ok) {
            char sourceText[24] {};
            char targetText[24] {};
            char clockText[16] {};
            char rateText[24] {};
            double sourceValue = -1.0;
            double targetValue = -1.0;
            double clockValue = -1.0;
            double rateValue = -1.0;
            constexpr double displayedRate = 0.61;
            ok = paramsExtension->value_to_text(plugin, kModSourceParamId,
                    static_cast<double>(s3g::PsdRawFieldModSource::Hellschreiber),
                    sourceText, sizeof(sourceText))
                && std::strcmp(sourceText, "HELL") == 0
                && paramsExtension->text_to_value(plugin, kModSourceParamId,
                    "HELL", &sourceValue)
                && sourceValue == static_cast<double>(s3g::PsdRawFieldModSource::Hellschreiber)
                && paramsExtension->value_to_text(plugin, kModTargetParamId,
                    static_cast<double>(s3g::PsdRawFieldModTarget::Clock),
                    targetText, sizeof(targetText))
                && std::strcmp(targetText, "CLOCK") == 0
                && paramsExtension->text_to_value(plugin, kModTargetParamId,
                    "CLOCK", &targetValue)
                && targetValue == static_cast<double>(s3g::PsdRawFieldModTarget::Clock)
                && paramsExtension->value_to_text(plugin, kModClockLockParamId,
                    1.0, clockText, sizeof(clockText))
                && std::strcmp(clockText, "LOCK") == 0
                && paramsExtension->text_to_value(plugin, kModClockLockParamId,
                    "LOCK", &clockValue)
                && clockValue == 1.0
                && paramsExtension->value_to_text(plugin, kModRateParamId,
                    displayedRate, rateText, sizeof(rateText))
                && paramsExtension->text_to_value(plugin, kModRateParamId,
                    rateText, &rateValue)
                && std::abs(rateValue - displayedRate) < 0.002;
            if (!ok) std::cerr << "Fault did not expose protocol modulation parameter text\n";
        }
        if (ok) {
            flushParam(kModSourceParamId,
                static_cast<double>(s3g::PsdRawFieldModSource::Sstv));
            flushParam(kModTargetParamId,
                static_cast<double>(s3g::PsdRawFieldModTarget::Damage));
            flushParam(kModRateParamId, 0.63);
            flushParam(kModRatioParamId, 3.0);
            flushParam(kModIndexParamId, 0.74);
            flushParam(kModFeedbackParamId, 0.61);
            flushParam(kModClockLockParamId, 1.0);
        }
        if (ok) {
            flushParam(kRandomizeFieldParamId, 0.613);
            flushParam(kCodecModeParamId, static_cast<double>(s3g::PsdRawFieldCodecMode::ModemFsk));
            MemoryOutput generatedOutput;
            clap_ostream_t generatedStream { &generatedOutput, streamWrite };
            ok = stateExtension->save(plugin, &generatedStream)
                && generatedOutput.bytes.size() == sizeof(SavedStateV17);
            if (ok) {
                SavedStateV17 generated {};
                std::memcpy(&generated, generatedOutput.bytes.data(), sizeof(generated));
                ok = generated.sourceMode == 0u
                    && generated.params.codecMode == s3g::PsdRawFieldCodecMode::ModemFsk
                    && generated.params.fieldCodecMode == s3g::PsdRawFieldCodecMode::Apt
                    && generated.params.carrierTune == 12.0f
                    && generated.params.modSource == s3g::PsdRawFieldModSource::Sstv
                    && generated.params.modTarget == s3g::PsdRawFieldModTarget::Damage
                    && std::abs(generated.params.modRate - 0.63f) < 1.0e-6f
                    && std::abs(generated.params.modRatio - 3.0f) < 1.0e-6f
                    && std::abs(generated.params.modIndex - 0.74f) < 1.0e-6f
                    && std::abs(generated.params.modFeedback - 0.61f) < 1.0e-6f
                    && generated.params.modClockLock == 1u;
            }
        }
        if (!ok) std::cerr << "Fault GEN FIELD did not latch the selected codec profile\n";
        if (ok) {
            constexpr const char* expectedPresetNames[] {
                "INIT", "SLOW CLOCK", "CODEC SCAR", "FIELD DIVERGE", "GATED BREAKS",
                "SYNC GLASS", "MU DUST", "DELTA STAIRS", "ADPCM FEEDBACK",
                "MORSE CHOIR", "SPARK EMBERS", "WIDE FAX", "WAVE TRACE",
            };
            std::array<std::array<bool, s3g::kPsdRawFieldModTargetCount>,
                s3g::kPsdRawFieldModSourceCount> combinations {};
            uint32_t uniqueCombinations = 0u;
            for (uint32_t preset = 0u; ok && preset <= 12u; ++preset) {
                char presetText[32] {};
                flushParam(kPresetParamId, preset);
                MemoryOutput presetOutput;
                clap_ostream_t presetStream { &presetOutput, streamWrite };
                ok = paramsExtension->value_to_text(plugin, kPresetParamId,
                        preset, presetText, sizeof(presetText))
                    && std::strcmp(presetText, expectedPresetNames[preset]) == 0
                    && stateExtension->save(plugin, &presetStream)
                    && presetOutput.bytes.size() == sizeof(SavedStateV17);
                if (!ok) break;
                SavedStateV17 savedPreset {};
                std::memcpy(&savedPreset, presetOutput.bytes.data(), sizeof(savedPreset));
                constexpr uint32_t presetFrames = 1024u;
                std::array<std::array<float, presetFrames>, kChannels> presetAudio {};
                std::array<float*, kChannels> presetPointers {};
                for (uint32_t ch = 0u; ch < kChannels; ++ch) {
                    presetPointers[ch] = presetAudio[ch].data();
                }
                s3g::PsdRawField presetRenderer;
                presetRenderer.prepare(48000.0);
                presetRenderer.setParams(savedPreset.params);
                presetRenderer.reset();
                presetRenderer.process(presetPointers.data(), kChannels, presetFrames);
                double presetEnergy = 0.0;
                for (const auto& channel : presetAudio) {
                    for (float sample : channel) {
                        ok = ok && std::isfinite(sample) && std::abs(sample) <= 1.0f;
                        presetEnergy += static_cast<double>(sample) * sample;
                    }
                }
                ok = ok && presetEnergy > 1.0e-8;
                if (preset == 0u) {
                    ok = savedPreset.params.modSource == s3g::PsdRawFieldModSource::Off
                        && savedPreset.params.modIndex == 0.0f;
                    continue;
                }
                const uint32_t source = static_cast<uint32_t>(savedPreset.params.modSource);
                const uint32_t target = static_cast<uint32_t>(savedPreset.params.modTarget);
                ok = savedPreset.selectedPreset == preset
                    && source > 0u && source < s3g::kPsdRawFieldModSourceCount
                    && target < s3g::kPsdRawFieldModTargetCount
                    && savedPreset.params.modIndex >= 0.10f
                    && savedPreset.params.modRatio >= 0.125f
                    && savedPreset.params.modRatio <= 16.0f;
                if (ok && !combinations[source][target]) {
                    combinations[source][target] = true;
                    ++uniqueCombinations;
                }
            }
            ok = ok && uniqueCombinations == 12u;
            if (!ok) std::cerr << "Fault factory presets were not modulation-aware and distinct\n";
        }
    }
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    std::remove(wavePath.c_str());
    return ok ? 0 : 1;
}
