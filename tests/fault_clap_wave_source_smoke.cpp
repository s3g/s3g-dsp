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
constexpr clap_id kRandomizePatchParamId = 51u;
constexpr clap_id kMutateParamId = 53u;
constexpr clap_id kModSourceParamId = 62u;
constexpr clap_id kModTargetParamId = 63u;
constexpr clap_id kModRateParamId = 64u;
constexpr clap_id kModRatioParamId = 65u;
constexpr clap_id kModIndexParamId = 66u;
constexpr clap_id kModFeedbackParamId = 67u;
constexpr clap_id kModClockLockParamId = 68u;
constexpr clap_id kModAlgorithmParamId = 69u;
constexpr clap_id kModSource2ParamId = 70u;
constexpr clap_id kModRate2ParamId = 71u;
constexpr clap_id kModRatio2ParamId = 72u;
constexpr clap_id kModIndex2ParamId = 73u;
constexpr clap_id kModFeedback2ParamId = 74u;
constexpr clap_id kModClockLock2ParamId = 75u;
constexpr clap_id kModTarget2ParamId = 76u;
constexpr clap_id kModSource3ParamId = 77u;
constexpr clap_id kModTarget3ParamId = 78u;
constexpr clap_id kModRate3ParamId = 79u;
constexpr clap_id kModRatio3ParamId = 80u;
constexpr clap_id kModIndex3ParamId = 81u;
constexpr clap_id kModFeedback3ParamId = 82u;
constexpr clap_id kModClockLock3ParamId = 83u;
constexpr clap_id kModEnvelope1ParamId = 84u;
constexpr clap_id kModEnvelope2ParamId = 85u;
constexpr clap_id kModEnvelope3ParamId = 86u;
constexpr clap_id kModulationEnabledParamId = 87u;
constexpr clap_id kBassReceiverParamId = 88u;
constexpr clap_id kBassBodyParamId = 89u;
constexpr clap_id kBassPunchParamId = 90u;
constexpr clap_id kBassTraceParamId = 91u;
constexpr clap_id kBassPitchTrackingParamId = 92u;
constexpr clap_id kBassGlideParamId = 93u;
constexpr clap_id kBassOctaveParamId = 94u;
constexpr clap_id kBassLowWidthParamId = 95u;
constexpr clap_id kBassFuzzParamId = 96u;
constexpr clap_id kBassMetalParamId = 97u;
constexpr clap_id kBassFeedbackParamId = 98u;
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

struct LegacyParamsV17 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
    uint32_t modSource;
    uint32_t modTarget;
    float modRate, modRatio, modIndex, modFeedback;
    uint32_t modClockLock;
};
static_assert(sizeof(LegacyParamsV17) == 104u, "Unexpected version-17 parameter layout");

LegacyParamsV17 legacyParamsV17(const s3g::PsdRawFieldParams& params)
{
    return {
        params.scanRate, params.texture, params.geometry, params.chaos, params.fold, params.evolve,
        static_cast<uint32_t>(params.channelScheme), params.channelSpread,
        static_cast<uint32_t>(params.codecMode), params.codecRate, params.bitDepth,
        params.codecDamage, params.carrierTune, params.drive, params.shred, params.resonance,
        params.gainDb, params.seed, static_cast<uint32_t>(params.fieldCodecMode),
        static_cast<uint32_t>(params.modSource), static_cast<uint32_t>(params.modTarget),
        params.modRate, params.modRatio, params.modIndex, params.modFeedback, params.modClockLock,
    };
}

struct LegacyParamsV18 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
    uint32_t modSource;
    uint32_t modTarget;
    float modRate, modRatio, modIndex, modFeedback;
    uint32_t modClockLock;
    uint32_t modAlgorithm;
    uint32_t modSource2;
    float modRate2, modRatio2, modIndex2, modFeedback2;
    uint32_t modClockLock2;
};
static_assert(sizeof(LegacyParamsV18) == 132u, "Unexpected version-18 parameter layout");

LegacyParamsV18 legacyParamsV18(const s3g::PsdRawFieldParams& params)
{
    LegacyParamsV18 result {};
    const LegacyParamsV17 first = legacyParamsV17(params);
    std::memcpy(&result, &first, sizeof(first));
    result.modAlgorithm = static_cast<uint32_t>(params.modAlgorithm);
    result.modSource2 = static_cast<uint32_t>(params.modSource2);
    result.modRate2 = params.modRate2;
    result.modRatio2 = params.modRatio2;
    result.modIndex2 = params.modIndex2;
    result.modFeedback2 = params.modFeedback2;
    result.modClockLock2 = params.modClockLock2;
    return result;
}

struct LegacyParamsV19 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, carrierTune, drive, shred, resonance, gainDb;
    uint32_t seed;
    uint32_t fieldCodecMode;
    uint32_t modSource;
    uint32_t modTarget;
    float modRate, modRatio, modIndex, modFeedback;
    uint32_t modClockLock;
    uint32_t modAlgorithm;
    uint32_t modSource2;
    float modRate2, modRatio2, modIndex2, modFeedback2;
    uint32_t modClockLock2;
    uint32_t modTarget2, modSource3, modTarget3;
    float modRate3, modRatio3, modIndex3, modFeedback3;
    uint32_t modClockLock3;
};
static_assert(sizeof(LegacyParamsV19) == 164u, "Unexpected version-19 parameter layout");

LegacyParamsV19 legacyParamsV19(const s3g::PsdRawFieldParams& params)
{
    LegacyParamsV19 result {};
    std::memcpy(&result, &params, sizeof(result));
    return result;
}

struct LegacyParamsV20 {
    LegacyParamsV19 previous;
    uint32_t modEnvelope1, modEnvelope2, modEnvelope3;
};
static_assert(sizeof(LegacyParamsV20) == 176u, "Unexpected version-20 parameter layout");

LegacyParamsV20 legacyParamsV20(const s3g::PsdRawFieldParams& params)
{
    LegacyParamsV20 result {};
    std::memcpy(&result, &params, sizeof(result));
    return result;
}

struct LegacyParamsV21 {
    LegacyParamsV20 previous;
    uint32_t modulationEnabled;
};
static_assert(sizeof(LegacyParamsV21) == 180u, "Unexpected version-21 parameter layout");

LegacyParamsV21 legacyParamsV21(const s3g::PsdRawFieldParams& params)
{
    LegacyParamsV21 result {};
    std::memcpy(&result, &params, sizeof(result));
    return result;
}

struct LegacyParamsV22 {
    LegacyParamsV21 previous;
    uint32_t bassReceiver;
    float bassBody;
    float bassPunch;
    float bassTrace;
    uint32_t bassPitchTracking;
    float bassGlide;
    uint32_t bassOctave;
    float bassLowWidth;
};
static_assert(sizeof(LegacyParamsV22) == 212u,
    "Unexpected version-22 parameter layout");

LegacyParamsV22 legacyParamsV22(const s3g::PsdRawFieldParams& params)
{
    LegacyParamsV22 result {};
    std::memcpy(&result, &params, sizeof(result));
    return result;
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

struct LegacySavedStateV17 {
    uint32_t version = 17u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV17 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV17) == 4236u, "Unexpected version-17 Fault state layout");

struct LegacySavedStateV18 {
    uint32_t version = 18u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV18 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV18) == 4264u, "Unexpected version-18 Fault state layout");

struct LegacySavedStateV19 {
    uint32_t version = 19u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV19 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV19) == 4296u, "Unexpected version-19 Fault state layout");

struct LegacySavedStateV20 {
    uint32_t version = 20u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV20 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV20) == 4308u, "Unexpected version-20 Fault state layout");

struct LegacySavedStateV21 {
    uint32_t version = 21u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV21 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV21) == 4312u,
    "Unexpected version-21 Fault state layout");

struct LegacySavedStateV22 {
    uint32_t version = 22u;
    uint32_t selectedPreset = 12u;
    LegacyParamsV22 params {};
    uint32_t sourceMode = 2u;
    uint32_t runState = 1u;
    uint32_t performanceMode = 0u;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV22) == 4344u,
    "Unexpected version-22 Fault state layout");

struct SavedStateV23 {
    uint32_t version = 23u;
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
static_assert(sizeof(SavedStateV23) == 4356u,
    "Unexpected version-23 Fault state layout");

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
    if (!ok) {
        std::cerr << "Fault startup failed: state=" << (stateExtension != nullptr)
                  << " loaded=" << loaded
                  << " activated=" << activated
                  << " started=" << started << "\n";
    }
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
    if (!ok) std::cerr << "Fault initial process returned CLAP_PROCESS_ERROR\n";

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
        if (ok) {
            flushParam(kBassReceiverParamId,
                static_cast<double>(s3g::PsdRawFieldBassReceiver::Direct));
            flushParam(kBassBodyParamId, 1.0);
            flushParam(kBassPunchParamId, 0.0);
            flushParam(kBassTraceParamId, 0.0);
            flushParam(kBassPitchTrackingParamId,
                static_cast<double>(s3g::PsdRawFieldPitchTracking::Body));
            flushParam(kBassGlideParamId, 0.0);
            flushParam(kBassOctaveParamId,
                static_cast<double>(s3g::PsdRawFieldBassOctave::MinusTwo));
            flushParam(kBassLowWidthParamId, 0.0);

            constexpr uint32_t noteBlocks = 32u;
            auto renderBassNote = [&](int16_t key) {
                std::vector<float> rendered(noteBlocks * kTransportFrames);
                clap_event_note_t event {};
                event.header.size = sizeof(event);
                event.header.time = 0u;
                event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                event.header.type = CLAP_EVENT_NOTE_ON;
                event.note_id = -1;
                event.port_index = 30;
                event.channel = 0;
                event.key = key;
                event.velocity = 0.90;
                for (uint32_t block = 0u; block < noteBlocks; ++block) {
                    if (!renderMidiBlock(block == 0u ? &event.header : nullptr)) {
                        ok = false;
                        break;
                    }
                    for (uint32_t i = 0u; i < kTransportFrames; ++i) {
                        double mean = 0.0;
                        for (uint32_t ch = 0u; ch < kChannels; ++ch) mean += midiAudio[ch][i];
                        rendered[block * kTransportFrames + i]
                            = static_cast<float>(mean / static_cast<double>(kChannels));
                    }
                }
                event.header.type = CLAP_EVENT_NOTE_OFF;
                event.velocity = 0.0;
                if (ok) ok = renderMidiBlock(&event.header) && renderMidiBlock(nullptr);
                return rendered;
            };
            auto dominantFrequency = [](const std::vector<float>& signal) {
                double bestEnergy = -1.0;
                double bestFrequency = 45.0;
                for (double frequency = 45.0; frequency <= 155.0; frequency += 0.5) {
                    const double coefficient = 2.0 * std::cos(
                        2.0 * static_cast<double>(s3g::kPi) * frequency / 48000.0);
                    double q1 = 0.0;
                    double q2 = 0.0;
                    for (uint32_t i = 4096u; i < signal.size(); ++i) {
                        const double q0 = signal[i] + coefficient * q1 - q2;
                        q2 = q1;
                        q1 = q0;
                    }
                    const double energy = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
                    if (energy > bestEnergy) {
                        bestEnergy = energy;
                        bestFrequency = frequency;
                    }
                }
                return std::pair<double, double> { bestFrequency, bestEnergy };
            };
            const auto lowNote = dominantFrequency(renderBassNote(60));
            const auto highNote = dominantFrequency(renderBassNote(72));
            const double octaveRatio = highNote.first / lowNote.first;
            // The note selects the divider/filter target, but the audible
            // period is still resolved from source and codec crossings. A
            // loaded waveform therefore needs to move clearly upward without
            // behaving like a pitch-perfect autonomous oscillator.
            ok = ok && lowNote.second > 1.0e-6 && highNote.second > 1.0e-6
                && octaveRatio >= 1.08 && octaveRatio <= 2.60;
            if (!ok) {
                std::cerr << "Fault MIDI receiver-derived bass did not follow its pitch target: frequency="
                          << lowNote.first << "/" << highNote.first
                          << " ratio=" << octaveRatio << " energy="
                          << lowNote.second << "/" << highNote.second << "\n";
            }

            flushParam(kBassBodyParamId, 0.0);
            flushParam(kBassTraceParamId, 1.0);
            flushParam(kBassPitchTrackingParamId,
                static_cast<double>(s3g::PsdRawFieldPitchTracking::Scan));
            flushParam(kBassOctaveParamId,
                static_cast<double>(s3g::PsdRawFieldBassOctave::MinusOne));
            flushParam(kBassLowWidthParamId, 1.0);
        }
    }

    if (started) plugin->stop_processing(plugin);
    if (activated) plugin->deactivate(plugin);

    if (ok) {
        MemoryOutput currentOutput;
        clap_ostream_t currentStream { &currentOutput, streamWrite };
        ok = stateExtension->save(plugin, &currentStream)
            && currentOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 saved {};
            std::memcpy(&saved, currentOutput.bytes.data(), sizeof(saved));
            ok = saved.version == 23u && saved.selectedPreset == 13u
                && saved.sourceMode == 2u && saved.runState == 1u
                && saved.params.fieldCodecMode == s3g::PsdRawFieldCodecMode::RawPcm
                && saved.params.carrierTune == 0.0f
                && saved.params.modSource == s3g::PsdRawFieldModSource::Off
                && saved.params.modIndex == 0.0f
                && saved.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Broadcast
                && saved.params.modSource2 == s3g::PsdRawFieldModSource::Off
                && saved.params.modIndex2 == 0.0f
                && saved.params.modTarget2 == s3g::PsdRawFieldModTarget::Off
                && saved.params.modSource3 == s3g::PsdRawFieldModSource::Off
                && saved.params.modTarget3 == s3g::PsdRawFieldModTarget::Off
                && saved.params.modIndex3 == 0.0f
                && saved.params.modEnvelope1 == 0u
                && saved.params.modEnvelope2 == 0u
                && saved.params.modEnvelope3 == 0u
                && saved.params.modulationEnabled == 1u
                && saved.params.bassReceiver == s3g::PsdRawFieldBassReceiver::Direct
                && saved.params.bassBody == 0.0f
                && saved.params.bassPunch == 0.0f
                && saved.params.bassTrace == 1.0f
                && saved.params.bassPitchTracking == s3g::PsdRawFieldPitchTracking::Scan
                && saved.params.bassGlide == 0.0f
                && saved.params.bassOctave == s3g::PsdRawFieldBassOctave::MinusOne
                && saved.params.bassLowWidth == 1.0f
                && saved.params.bassFuzz == 0.0f
                && saved.params.bassMetal == 0.55f
                && saved.params.bassFeedback == 0.0f
                && saved.performanceMode == 1u && saved.attackMs == 1.0f
                && saved.decayMs == 5.0f && saved.sustain == 1.0f && saved.releaseMs == 5.0f;
        }
        if (!ok) std::cerr << "Fault did not preserve the current version-23 state contract\n";
    }

    if (ok) {
        s3g::PsdRawFieldParams oldParams {};
        oldParams.codecMode = s3g::PsdRawFieldCodecMode::Apt;
        oldParams.bassReceiver = s3g::PsdRawFieldBassReceiver::Divide;
        oldParams.bassBody = 0.73f;
        oldParams.bassPunch = 0.46f;
        oldParams.bassTrace = 0.29f;
        oldParams.bassPitchTracking = s3g::PsdRawFieldPitchTracking::BodyAndScan;
        oldParams.bassGlide = 0.17f;
        oldParams.bassOctave = s3g::PsdRawFieldBassOctave::MinusTwo;
        oldParams.bassLowWidth = 0.21f;
        LegacySavedStateV22 legacy {};
        legacy.selectedPreset = 7u;
        legacy.params = legacyParamsV22(oldParams);
        legacy.sourceMode = 1u;
        legacy.runState = 0u;
        legacy.performanceMode = 1u;
        legacy.attackMs = 7.0f;
        legacy.decayMs = 83.0f;
        legacy.sustain = 0.64f;
        legacy.releaseMs = 490.0f;
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u
                && migrated.selectedPreset == 7u
                && migrated.params.codecMode == s3g::PsdRawFieldCodecMode::Apt
                && migrated.params.bassReceiver == s3g::PsdRawFieldBassReceiver::Divide
                && std::abs(migrated.params.bassBody - 0.73f) < 1.0e-6f
                && std::abs(migrated.params.bassPunch - 0.46f) < 1.0e-6f
                && std::abs(migrated.params.bassTrace - 0.29f) < 1.0e-6f
                && migrated.params.bassPitchTracking
                    == s3g::PsdRawFieldPitchTracking::BodyAndScan
                && std::abs(migrated.params.bassGlide - 0.17f) < 1.0e-6f
                && migrated.params.bassOctave == s3g::PsdRawFieldBassOctave::MinusTwo
                && std::abs(migrated.params.bassLowWidth - 0.21f) < 1.0e-6f
                && migrated.params.bassFuzz == 0.0f
                && migrated.params.bassMetal == 0.55f
                && migrated.params.bassFeedback == 0.0f
                && migrated.sourceMode == 1u && migrated.runState == 0u
                && migrated.performanceMode == 1u
                && migrated.attackMs == 7.0f && migrated.decayMs == 83.0f
                && migrated.sustain == 0.64f && migrated.releaseMs == 490.0f
                && std::strcmp(migrated.sourcePath, wavePath.c_str()) == 0;
        }
        if (!ok) std::cerr << "Fault did not migrate version-22 bass high-gain defaults\n";
    }

    if (ok) {
        s3g::PsdRawFieldParams oldParams {};
        oldParams.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
        oldParams.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Multiplex;
        oldParams.modSource = s3g::PsdRawFieldModSource::Morse;
        oldParams.modTarget = s3g::PsdRawFieldModTarget::Body;
        oldParams.modIndex = 0.57f;
        oldParams.modulationEnabled = 0u;
        LegacySavedStateV21 legacy {};
        legacy.params = legacyParamsV21(oldParams);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u
                && migrated.params.codecMode == s3g::PsdRawFieldCodecMode::HfFax
                && migrated.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Multiplex
                && migrated.params.modSource == s3g::PsdRawFieldModSource::Morse
                && migrated.params.modTarget == s3g::PsdRawFieldModTarget::Body
                && std::abs(migrated.params.modIndex - 0.57f) < 1.0e-6f
                && migrated.params.modulationEnabled == 0u
                && migrated.params.bassReceiver == s3g::PsdRawFieldBassReceiver::Direct
                && migrated.params.bassBody == 0.0f
                && migrated.params.bassPunch == 0.0f
                && migrated.params.bassTrace == 1.0f
                && migrated.params.bassPitchTracking == s3g::PsdRawFieldPitchTracking::Scan
                && migrated.params.bassGlide == 0.0f
                && migrated.params.bassOctave == s3g::PsdRawFieldBassOctave::MinusOne
                && migrated.params.bassLowWidth == 1.0f
                && migrated.params.bassFuzz == 0.0f
                && migrated.params.bassMetal == 0.55f
                && migrated.params.bassFeedback == 0.0f;
        }
        if (!ok) std::cerr << "Fault did not migrate version-21 bass compatibility defaults\n";
    }

    if (ok) {
        s3g::PsdRawFieldParams oldParams {};
        oldParams.modAlgorithm = s3g::PsdRawFieldModAlgorithm::CrossedMachines;
        oldParams.modSource = s3g::PsdRawFieldModSource::Sine;
        oldParams.modIndex = 0.62f;
        oldParams.modEnvelope1 = 1u;
        oldParams.modEnvelope2 = 0u;
        oldParams.modEnvelope3 = 1u;
        LegacySavedStateV20 legacy {};
        legacy.params = legacyParamsV20(oldParams);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u
                && migrated.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::CrossedMachines
                && migrated.params.modEnvelope1 == 1u
                && migrated.params.modEnvelope2 == 0u
                && migrated.params.modEnvelope3 == 1u
                && migrated.params.modulationEnabled == 1u;
        }
        if (!ok) std::cerr << "Fault did not migrate version-20 modulation bypass state\n";
    }

    if (ok) {
        s3g::PsdRawFieldParams oldParams {};
        oldParams.codecMode = s3g::PsdRawFieldCodecMode::Sstv;
        oldParams.modAlgorithm = s3g::PsdRawFieldModAlgorithm::Relay;
        oldParams.modSource = s3g::PsdRawFieldModSource::Sine;
        oldParams.modTarget = s3g::PsdRawFieldModTarget::Carrier;
        oldParams.modIndex = 0.52f;
        oldParams.modSource2 = s3g::PsdRawFieldModSource::Morse;
        oldParams.modTarget2 = s3g::PsdRawFieldModTarget::Clock;
        oldParams.modIndex2 = 0.43f;
        oldParams.modSource3 = s3g::PsdRawFieldModSource::Hellschreiber;
        oldParams.modTarget3 = s3g::PsdRawFieldModTarget::Damage;
        oldParams.modIndex3 = 0.34f;
        LegacySavedStateV19 legacy {};
        legacy.params = legacyParamsV19(oldParams);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u
                && migrated.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Relay
                && migrated.params.modSource3 == s3g::PsdRawFieldModSource::Hellschreiber
                && migrated.params.modTarget3 == s3g::PsdRawFieldModTarget::Damage
                && std::abs(migrated.params.modIndex3 - 0.34f) < 1.0e-6f
                && migrated.params.modEnvelope1 == 0u
                && migrated.params.modEnvelope2 == 0u
                && migrated.params.modEnvelope3 == 0u;
        }
        if (!ok) std::cerr << "Fault did not migrate version-19 three-operator state\n";
    }

    if (ok) {
        s3g::PsdRawFieldParams oldParams {};
        oldParams.codecMode = s3g::PsdRawFieldCodecMode::HfFax;
        oldParams.modAlgorithm = s3g::PsdRawFieldModAlgorithm::CrossedMachines;
        oldParams.modSource = s3g::PsdRawFieldModSource::Field;
        oldParams.modTarget = s3g::PsdRawFieldModTarget::Data;
        oldParams.modRate = 0.41f;
        oldParams.modIndex = 0.63f;
        oldParams.modSource2 = s3g::PsdRawFieldModSource::Apt;
        oldParams.modRate2 = 0.22f;
        oldParams.modRatio2 = 0.5f;
        oldParams.modIndex2 = 0.48f;
        oldParams.modClockLock2 = 1u;
        LegacySavedStateV18 legacy {};
        legacy.params = legacyParamsV18(oldParams);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u
                && migrated.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::CrossedMachines
                && migrated.params.modSource == s3g::PsdRawFieldModSource::Field
                && migrated.params.modTarget == s3g::PsdRawFieldModTarget::Carrier
                && std::abs(migrated.params.modRate - 0.41f) < 1.0e-6f
                && std::abs(migrated.params.modIndex - 0.63f) < 1.0e-6f
                && migrated.params.modSource2 == s3g::PsdRawFieldModSource::Apt
                && migrated.params.modTarget2 == s3g::PsdRawFieldModTarget::Clock
                && std::abs(migrated.params.modRate2 - 0.22f) < 1.0e-6f
                && std::abs(migrated.params.modRatio2 - 0.5f) < 1.0e-6f
                && std::abs(migrated.params.modIndex2 - 0.48f) < 1.0e-6f
                && migrated.params.modClockLock2 == 1u
                && migrated.params.modSource3 == s3g::PsdRawFieldModSource::Off
                && migrated.params.modTarget3 == s3g::PsdRawFieldModTarget::Off;
        }
        if (!ok) std::cerr << "Fault did not migrate version-18 two-operator state\n";
    }

    if (ok) {
        s3g::PsdRawFieldParams oldParams {};
        oldParams.codecMode = s3g::PsdRawFieldCodecMode::Hellschreiber;
        oldParams.modSource = s3g::PsdRawFieldModSource::Morse;
        oldParams.modTarget = s3g::PsdRawFieldModTarget::Data;
        oldParams.modRate = 0.42f;
        oldParams.modRatio = 3.0f;
        oldParams.modIndex = 0.67f;
        oldParams.modFeedback = 0.31f;
        oldParams.modClockLock = 1u;
        LegacySavedStateV17 legacy {};
        legacy.params = legacyParamsV17(oldParams);
        std::snprintf(legacy.sourcePath, sizeof(legacy.sourcePath), "%s", wavePath.c_str());
        MemoryInput legacyInput { reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy), 0u };
        clap_istream_t legacyStream { &legacyInput, streamRead };
        ok = stateExtension->load(plugin, &legacyStream);
        MemoryOutput migratedOutput;
        clap_ostream_t migratedStream { &migratedOutput, streamWrite };
        ok = ok && stateExtension->save(plugin, &migratedStream)
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u
                && migrated.params.codecMode == s3g::PsdRawFieldCodecMode::Hellschreiber
                && migrated.params.modSource == s3g::PsdRawFieldModSource::Morse
                && migrated.params.modTarget == s3g::PsdRawFieldModTarget::Data
                && std::abs(migrated.params.modRate - 0.42f) < 1.0e-6f
                && std::abs(migrated.params.modRatio - 3.0f) < 1.0e-6f
                && std::abs(migrated.params.modIndex - 0.67f) < 1.0e-6f
                && std::abs(migrated.params.modFeedback - 0.31f) < 1.0e-6f
                && migrated.params.modClockLock == 1u
                && migrated.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Broadcast
                && migrated.params.modSource2 == s3g::PsdRawFieldModSource::Off
                && migrated.params.modIndex2 == 0.0f
                && migrated.params.modTarget2 == s3g::PsdRawFieldModTarget::Off
                && migrated.params.modSource3 == s3g::PsdRawFieldModSource::Off
                && migrated.params.modTarget3 == s3g::PsdRawFieldModTarget::Off;
        }
        if (!ok) std::cerr << "Fault did not migrate version-17 modulation state\n";
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
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u
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
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u && migrated.selectedPreset == 12u
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
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u && migrated.selectedPreset == 12u
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
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u && migrated.selectedPreset == 12u
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
            && migratedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 migrated {};
            std::memcpy(&migrated, migratedOutput.bytes.data(), sizeof(migrated));
            ok = migrated.version == 23u && migrated.selectedPreset == 13u
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
            && stoppedOutput.bytes.size() == sizeof(SavedStateV23);
        if (ok) {
            SavedStateV23 savedStopped {};
            std::memcpy(&savedStopped, stoppedOutput.bytes.data(), sizeof(savedStopped));
            ok = savedStopped.version == 23u && savedStopped.runState == 0u;
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
            char algorithmText[24] {};
            char source2Text[24] {};
            char source3Text[24] {};
            char target3Text[24] {};
            char envelopeText[16] {};
            char modulationEnabledText[16] {};
            double sourceValue = -1.0;
            double targetValue = -1.0;
            double clockValue = -1.0;
            double rateValue = -1.0;
            double algorithmValue = -1.0;
            double source2Value = -1.0;
            double source3Value = -1.0;
            double target3Value = -1.0;
            double envelopeValue = -1.0;
            double modulationEnabledValue = -1.0;
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
                && std::abs(rateValue - displayedRate) < 0.002
                && paramsExtension->value_to_text(plugin, kModAlgorithmParamId,
                    static_cast<double>(s3g::PsdRawFieldModAlgorithm::Transcode),
                    algorithmText, sizeof(algorithmText))
                && std::strcmp(algorithmText, "TRANSCODE") == 0
                && paramsExtension->text_to_value(plugin, kModAlgorithmParamId,
                    "TRANSCODE", &algorithmValue)
                && algorithmValue == static_cast<double>(s3g::PsdRawFieldModAlgorithm::Transcode)
                && paramsExtension->value_to_text(plugin, kModSource2ParamId,
                    static_cast<double>(s3g::PsdRawFieldModSource::BaudotRtty),
                    source2Text, sizeof(source2Text))
                && std::strcmp(source2Text, "BAUDOT RTTY") == 0
                && paramsExtension->text_to_value(plugin, kModSource2ParamId,
                    "BAUDOT RTTY", &source2Value)
                && source2Value == static_cast<double>(s3g::PsdRawFieldModSource::BaudotRtty)
                && paramsExtension->value_to_text(plugin, kModSource3ParamId,
                    static_cast<double>(s3g::PsdRawFieldModSource::Sstv),
                    source3Text, sizeof(source3Text))
                && std::strcmp(source3Text, "SSTV") == 0
                && paramsExtension->text_to_value(plugin, kModSource3ParamId,
                    "SSTV", &source3Value)
                && source3Value == static_cast<double>(s3g::PsdRawFieldModSource::Sstv)
                && paramsExtension->value_to_text(plugin, kModTarget3ParamId,
                    static_cast<double>(s3g::PsdRawFieldModTarget::Off),
                    target3Text, sizeof(target3Text))
                && std::strcmp(target3Text, "OFF") == 0
                && paramsExtension->text_to_value(plugin, kModTarget3ParamId,
                    "OFF", &target3Value)
                && target3Value == static_cast<double>(s3g::PsdRawFieldModTarget::Off)
                && paramsExtension->value_to_text(plugin, kModEnvelope3ParamId,
                    1.0, envelopeText, sizeof(envelopeText))
                && std::strcmp(envelopeText, "ADSR") == 0
                && paramsExtension->text_to_value(plugin, kModEnvelope3ParamId,
                    "FIXED", &envelopeValue)
                && envelopeValue == 0.0
                && paramsExtension->value_to_text(plugin, kModulationEnabledParamId,
                    0.0, modulationEnabledText, sizeof(modulationEnabledText))
                && std::strcmp(modulationEnabledText, "MOD OFF") == 0
                && paramsExtension->text_to_value(plugin, kModulationEnabledParamId,
                    "MOD ON", &modulationEnabledValue)
                && modulationEnabledValue == 1.0;
            if (!ok) std::cerr << "Fault did not expose protocol modulation parameter text\n";
        }
        if (ok) {
            struct BassParamExpectation {
                clap_id id;
                const char* name;
                double maximum;
                double defaultValue;
                bool stepped;
            };
            constexpr BassParamExpectation bassParams[] {
                { kBassReceiverParamId, "Receiver", 3.0, 0.0, true },
                { kBassBodyParamId, "Body", 1.0, 0.0, false },
                { kBassPunchParamId, "Excite", 1.0, 0.0, false },
                { kBassTraceParamId, "Trace", 1.0, static_cast<double>(0.62f), false },
                { kBassPitchTrackingParamId, "Pitch Tracking", 2.0, 0.0, true },
                { kBassGlideParamId, "Glide", 1.0, 0.0, false },
                { kBassOctaveParamId, "Octave", 2.0, 1.0, true },
                { kBassLowWidthParamId, "Low Width", 1.0, 1.0, false },
                { kBassFuzzParamId, "Fuzz", 1.0, 0.0, false },
                { kBassMetalParamId, "Metal", 1.0, 0.55, false },
                { kBassFeedbackParamId, "Feedback", 1.0, 0.0, false },
            };
            for (const auto& expectation : bassParams) {
                clap_param_info_t found {};
                uint32_t matches = 0u;
                for (uint32_t index = 0u; index < paramsExtension->count(plugin); ++index) {
                    clap_param_info_t candidate {};
                    if (paramsExtension->get_info(plugin, index, &candidate)
                        && candidate.id == expectation.id) {
                        found = candidate;
                        ++matches;
                    }
                }
                const bool stepped = (found.flags & CLAP_PARAM_IS_STEPPED) != 0u;
                ok = ok && matches == 1u
                    && std::strcmp(found.name, expectation.name) == 0
                    && std::strcmp(found.module, "Bass Core") == 0
                    && found.min_value == 0.0
                    && found.max_value == expectation.maximum
                    && found.default_value == expectation.defaultValue
                    && (found.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0u
                    && stepped == expectation.stepped;
            }

            constexpr const char* receiverNames[] { "DIRECT", "DEMOD", "DIVIDE", "ERROR" };
            constexpr const char* trackingNames[] { "SCAN", "BODY", "BODY + SCAN" };
            constexpr const char* octaveNames[] { "-2 OCT", "-1 OCT", "UNISON" };
            auto checkEnumText = [&](clap_id id, const char* const* names, uint32_t count) {
                for (uint32_t value = 0u; value < count; ++value) {
                    char text[24] {};
                    double parsed = -1.0;
                    if (!paramsExtension->value_to_text(plugin, id,
                            static_cast<double>(value), text, sizeof(text))
                        || std::strcmp(text, names[value]) != 0
                        || !paramsExtension->text_to_value(plugin, id, names[value], &parsed)
                        || parsed != static_cast<double>(value)) {
                        return false;
                    }
                }
                return true;
            };
            char bodyText[16] {};
            double bodyValue = -1.0;
            auto checkPercentText = [&](clap_id id, double value, const char* expected) {
                char display[16] {};
                double parsed = -1.0;
                return paramsExtension->value_to_text(plugin, id,
                           value, display, sizeof(display))
                    && std::strcmp(display, expected) == 0
                    && paramsExtension->text_to_value(plugin, id, display, &parsed)
                    && std::abs(parsed - value) < 1.0e-9;
            };
            ok = ok
                && checkEnumText(kBassReceiverParamId, receiverNames,
                    static_cast<uint32_t>(std::size(receiverNames)))
                && checkEnumText(kBassPitchTrackingParamId, trackingNames,
                    static_cast<uint32_t>(std::size(trackingNames)))
                && checkEnumText(kBassOctaveParamId, octaveNames,
                    static_cast<uint32_t>(std::size(octaveNames)))
                && paramsExtension->value_to_text(plugin, kBassBodyParamId,
                    0.375, bodyText, sizeof(bodyText))
                && std::strcmp(bodyText, "37.5%") == 0
                && paramsExtension->text_to_value(plugin, kBassBodyParamId,
                    bodyText, &bodyValue)
                && std::abs(bodyValue - 0.375) < 1.0e-9
                && checkPercentText(kBassFuzzParamId, 0.625, "62.5%")
                && checkPercentText(kBassMetalParamId, 0.55, "55.0%")
                && checkPercentText(kBassFeedbackParamId, 0.42, "42.0%");
            if (!ok) std::cerr << "Fault did not expose the bass-core parameter contract\n";
        }
        if (ok) {
            constexpr const char* targetNames[] {
                "CARRIER", "DEVIATION", "CLOCK", "DATA", "DAMAGE", "OFF",
                "BODY", "RING", "STRIKE", "FOLD", "SCAN",
            };
            clap_param_info_t targetInfo {};
            bool foundTarget = false;
            const uint32_t paramCount = paramsExtension->count(plugin);
            for (uint32_t index = 0u; index < paramCount; ++index) {
                clap_param_info_t candidate {};
                if (paramsExtension->get_info(plugin, index, &candidate)
                    && candidate.id == kModTargetParamId) {
                    targetInfo = candidate;
                    foundTarget = true;
                    break;
                }
            }
            ok = foundTarget
                && targetInfo.max_value == static_cast<double>(s3g::kPsdRawFieldModTargetCount - 1u);
            for (uint32_t target = 0u; ok && target < s3g::kPsdRawFieldModTargetCount; ++target) {
                char display[24] {};
                double parsed = -1.0;
                ok = paramsExtension->value_to_text(plugin, kModTargetParamId,
                        static_cast<double>(target), display, sizeof(display))
                    && std::strcmp(display, targetNames[target]) == 0
                    && paramsExtension->text_to_value(plugin, kModTargetParamId,
                        targetNames[target], &parsed)
                    && parsed == static_cast<double>(target);
            }
            if (!ok) std::cerr << "Fault did not expose all modulation destinations\n";
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
            flushParam(kModAlgorithmParamId,
                static_cast<double>(s3g::PsdRawFieldModAlgorithm::Transcode));
            flushParam(kModSource2ParamId,
                static_cast<double>(s3g::PsdRawFieldModSource::BaudotRtty));
            flushParam(kModRate2ParamId, 0.27);
            flushParam(kModRatio2ParamId, 0.75);
            flushParam(kModIndex2ParamId, 0.82);
            flushParam(kModFeedback2ParamId, 0.29);
            flushParam(kModClockLock2ParamId, 1.0);
            flushParam(kModTarget2ParamId,
                static_cast<double>(s3g::PsdRawFieldModTarget::Clock));
            flushParam(kModSource3ParamId,
                static_cast<double>(s3g::PsdRawFieldModSource::Sstv));
            flushParam(kModTarget3ParamId,
                static_cast<double>(s3g::PsdRawFieldModTarget::Data));
            flushParam(kModRate3ParamId, 0.31);
            flushParam(kModRatio3ParamId, 2.0);
            flushParam(kModIndex3ParamId, 0.69);
            flushParam(kModFeedback3ParamId, 0.37);
            flushParam(kModClockLock3ParamId, 1.0);
            flushParam(kModEnvelope1ParamId, 1.0);
            flushParam(kModEnvelope2ParamId, 0.0);
            flushParam(kModEnvelope3ParamId, 1.0);
            flushParam(kModulationEnabledParamId, 0.0);
            flushParam(kBassReceiverParamId,
                static_cast<double>(s3g::PsdRawFieldBassReceiver::Error));
            flushParam(kBassBodyParamId, 0.76);
            flushParam(kBassPunchParamId, 0.42);
            flushParam(kBassTraceParamId, 0.31);
            flushParam(kBassPitchTrackingParamId,
                static_cast<double>(s3g::PsdRawFieldPitchTracking::BodyAndScan));
            flushParam(kBassGlideParamId, 0.27);
            flushParam(kBassOctaveParamId,
                static_cast<double>(s3g::PsdRawFieldBassOctave::MinusTwo));
            flushParam(kBassLowWidthParamId, 0.18);
            flushParam(kBassFuzzParamId, 0.68);
            flushParam(kBassMetalParamId, 0.77);
            flushParam(kBassFeedbackParamId, 0.49);
        }
        if (ok) {
            flushParam(kRandomizeFieldParamId, 0.613);
            flushParam(kCodecModeParamId, static_cast<double>(s3g::PsdRawFieldCodecMode::ModemFsk));
            MemoryOutput generatedOutput;
            clap_ostream_t generatedStream { &generatedOutput, streamWrite };
            ok = stateExtension->save(plugin, &generatedStream)
                && generatedOutput.bytes.size() == sizeof(SavedStateV23);
            if (ok) {
                SavedStateV23 generated {};
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
                    && generated.params.modClockLock == 1u
                    && generated.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Transcode
                    && generated.params.modSource2 == s3g::PsdRawFieldModSource::BaudotRtty
                    && std::abs(generated.params.modRate2 - 0.27f) < 1.0e-6f
                    && std::abs(generated.params.modRatio2 - 0.75f) < 1.0e-6f
                    && std::abs(generated.params.modIndex2 - 0.82f) < 1.0e-6f
                    && std::abs(generated.params.modFeedback2 - 0.29f) < 1.0e-6f
                    && generated.params.modClockLock2 == 1u
                    && generated.params.modTarget2 == s3g::PsdRawFieldModTarget::Clock
                    && generated.params.modSource3 == s3g::PsdRawFieldModSource::Sstv
                    && generated.params.modTarget3 == s3g::PsdRawFieldModTarget::Data
                    && std::abs(generated.params.modRate3 - 0.31f) < 1.0e-6f
                    && std::abs(generated.params.modRatio3 - 2.0f) < 1.0e-6f
                    && std::abs(generated.params.modIndex3 - 0.69f) < 1.0e-6f
                    && std::abs(generated.params.modFeedback3 - 0.37f) < 1.0e-6f
                    && generated.params.modClockLock3 == 1u
                    && generated.params.modEnvelope1 == 1u
                    && generated.params.modEnvelope2 == 0u
                    && generated.params.modEnvelope3 == 1u
                    && generated.params.modulationEnabled == 0u
                    && generated.params.bassReceiver == s3g::PsdRawFieldBassReceiver::Error
                    && std::abs(generated.params.bassBody - 0.76f) < 1.0e-6f
                    && std::abs(generated.params.bassPunch - 0.42f) < 1.0e-6f
                    && std::abs(generated.params.bassTrace - 0.31f) < 1.0e-6f
                    && generated.params.bassPitchTracking
                        == s3g::PsdRawFieldPitchTracking::BodyAndScan
                    && std::abs(generated.params.bassGlide - 0.27f) < 1.0e-6f
                    && generated.params.bassOctave == s3g::PsdRawFieldBassOctave::MinusTwo
                    && std::abs(generated.params.bassLowWidth - 0.18f) < 1.0e-6f
                    && std::abs(generated.params.bassFuzz - 0.68f) < 1.0e-6f
                    && std::abs(generated.params.bassMetal - 0.77f) < 1.0e-6f
                    && std::abs(generated.params.bassFeedback - 0.49f) < 1.0e-6f;
            }
            if (ok) {
                MemoryInput roundTripInput {
                    generatedOutput.bytes.data(), generatedOutput.bytes.size(), 0u
                };
                clap_istream_t roundTripStream { &roundTripInput, streamRead };
                ok = stateExtension->load(plugin, &roundTripStream);
                MemoryOutput roundTripOutput;
                clap_ostream_t roundTripSave { &roundTripOutput, streamWrite };
                ok = ok && stateExtension->save(plugin, &roundTripSave)
                    && roundTripOutput.bytes.size() == sizeof(SavedStateV23);
                if (ok) {
                    SavedStateV23 roundTripped {};
                    std::memcpy(&roundTripped, roundTripOutput.bytes.data(), sizeof(roundTripped));
                    ok = roundTripped.version == 23u
                        && roundTripped.params.bassReceiver == s3g::PsdRawFieldBassReceiver::Error
                        && std::abs(roundTripped.params.bassBody - 0.76f) < 1.0e-6f
                        && std::abs(roundTripped.params.bassPunch - 0.42f) < 1.0e-6f
                        && std::abs(roundTripped.params.bassTrace - 0.31f) < 1.0e-6f
                        && roundTripped.params.bassPitchTracking
                            == s3g::PsdRawFieldPitchTracking::BodyAndScan
                        && std::abs(roundTripped.params.bassGlide - 0.27f) < 1.0e-6f
                        && roundTripped.params.bassOctave
                            == s3g::PsdRawFieldBassOctave::MinusTwo
                        && std::abs(roundTripped.params.bassLowWidth - 0.18f) < 1.0e-6f
                        && std::abs(roundTripped.params.bassFuzz - 0.68f) < 1.0e-6f
                        && std::abs(roundTripped.params.bassMetal - 0.77f) < 1.0e-6f
                        && std::abs(roundTripped.params.bassFeedback - 0.49f) < 1.0e-6f;
                }
            }
        }
        if (!ok) std::cerr << "Fault GEN FIELD did not latch the selected codec profile\n";
        if (ok) {
            constexpr const char* expectedPresetNames[] {
                "INIT", "SUB CLOCK", "SCAR DRUM", "FAX BODY", "GATED BREAKS",
                "SYNC METAL", "BAUDOT DRUM", "DELTA KNOCK", "ADPCM SUB",
                "MORSE BODY", "SPARK IMPACT", "WIDE FAX BASS", "WAVE TRACE",
            };
            constexpr s3g::PsdRawFieldModAlgorithm expectedAlgorithms[] {
                s3g::PsdRawFieldModAlgorithm::Broadcast,
                s3g::PsdRawFieldModAlgorithm::Broadcast,
                s3g::PsdRawFieldModAlgorithm::Regenerator,
                s3g::PsdRawFieldModAlgorithm::CrossedMachines,
                s3g::PsdRawFieldModAlgorithm::Relay,
                s3g::PsdRawFieldModAlgorithm::Multiplex,
                s3g::PsdRawFieldModAlgorithm::Transcode,
                s3g::PsdRawFieldModAlgorithm::Broadcast,
                s3g::PsdRawFieldModAlgorithm::Regenerator,
                s3g::PsdRawFieldModAlgorithm::Transcode,
                s3g::PsdRawFieldModAlgorithm::Relay,
                s3g::PsdRawFieldModAlgorithm::Multiplex,
                s3g::PsdRawFieldModAlgorithm::CrossedMachines,
            };
            std::array<uint32_t, s3g::kPsdRawFieldModAlgorithmCount> algorithmCounts {};
            for (uint32_t preset = 0u; ok && preset <= 12u; ++preset) {
                char presetText[32] {};
                flushParam(kPresetParamId, preset);
                MemoryOutput presetOutput;
                clap_ostream_t presetStream { &presetOutput, streamWrite };
                ok = paramsExtension->value_to_text(plugin, kPresetParamId,
                        preset, presetText, sizeof(presetText))
                    && std::strcmp(presetText, expectedPresetNames[preset]) == 0
                    && stateExtension->save(plugin, &presetStream)
                    && presetOutput.bytes.size() == sizeof(SavedStateV23);
                if (!ok) break;
                SavedStateV23 savedPreset {};
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
                        && savedPreset.params.modIndex == 0.0f
                        && savedPreset.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Broadcast
                        && savedPreset.params.modSource2 == s3g::PsdRawFieldModSource::Off
                        && savedPreset.params.modTarget2 == s3g::PsdRawFieldModTarget::Off
                        && savedPreset.params.modIndex2 == 0.0f
                        && savedPreset.params.modSource3 == s3g::PsdRawFieldModSource::Off
                        && savedPreset.params.modTarget3 == s3g::PsdRawFieldModTarget::Off
                        && savedPreset.params.modIndex3 == 0.0f
                        && savedPreset.params.modEnvelope1 == 0u
                        && savedPreset.params.modEnvelope2 == 0u
                        && savedPreset.params.modEnvelope3 == 0u
                        && savedPreset.params.modulationEnabled == 1u
                        && savedPreset.params.bassReceiver == s3g::PsdRawFieldBassReceiver::Direct
                        && savedPreset.params.bassBody == 0.0f
                        && savedPreset.params.bassPunch == 0.0f
                        && savedPreset.params.bassTrace == 0.62f
                        && savedPreset.params.bassPitchTracking == s3g::PsdRawFieldPitchTracking::Scan
                        && savedPreset.params.bassGlide == 0.0f
                        && savedPreset.params.bassOctave == s3g::PsdRawFieldBassOctave::MinusOne
                        && savedPreset.params.bassLowWidth == 1.0f
                        && savedPreset.params.bassFuzz == 0.0f
                        && savedPreset.params.bassMetal == 0.55f
                        && savedPreset.params.bassFeedback == 0.0f;
                    continue;
                }
                const uint32_t source = static_cast<uint32_t>(savedPreset.params.modSource);
                const uint32_t source2 = static_cast<uint32_t>(savedPreset.params.modSource2);
                const uint32_t source3 = static_cast<uint32_t>(savedPreset.params.modSource3);
                const uint32_t target = static_cast<uint32_t>(savedPreset.params.modTarget);
                const uint32_t target2 = static_cast<uint32_t>(savedPreset.params.modTarget2);
                const uint32_t target3 = static_cast<uint32_t>(savedPreset.params.modTarget3);
                const uint32_t algorithm = static_cast<uint32_t>(savedPreset.params.modAlgorithm);
                const auto curatedDestination = [](s3g::PsdRawFieldModTarget destination) {
                    return destination == s3g::PsdRawFieldModTarget::Body
                        || destination == s3g::PsdRawFieldModTarget::Ring
                        || destination == s3g::PsdRawFieldModTarget::Strike
                        || destination == s3g::PsdRawFieldModTarget::Fold
                        || destination == s3g::PsdRawFieldModTarget::Scan;
                };
                const auto destructiveDestination = [](s3g::PsdRawFieldModTarget destination) {
                    return destination == s3g::PsdRawFieldModTarget::Deviation
                        || destination == s3g::PsdRawFieldModTarget::Data
                        || destination == s3g::PsdRawFieldModTarget::Damage;
                };
                ok = savedPreset.selectedPreset == preset
                    && source > 0u && source < s3g::kPsdRawFieldModSourceCount
                    && source2 > 0u && source2 < s3g::kPsdRawFieldModSourceCount
                    && source3 > 0u && source3 < s3g::kPsdRawFieldModSourceCount
                    && target < s3g::kPsdRawFieldModTargetCount
                    && target2 < s3g::kPsdRawFieldModTargetCount
                    && target3 < s3g::kPsdRawFieldModTargetCount
                    && algorithm < s3g::kPsdRawFieldModAlgorithmCount
                    && savedPreset.params.modAlgorithm == expectedAlgorithms[preset]
                    && savedPreset.params.modIndex >= 0.10f
                    && savedPreset.params.modRatio >= 0.125f
                    && savedPreset.params.modRatio <= 16.0f
                    && savedPreset.params.modIndex2 >= 0.10f
                    && savedPreset.params.modRatio2 >= 0.125f
                    && savedPreset.params.modRatio2 <= 16.0f
                    && savedPreset.params.modIndex3 >= 0.10f
                    && savedPreset.params.modRatio3 >= 0.125f
                    && savedPreset.params.modRatio3 <= 16.0f
                    && savedPreset.params.modEnvelope1
                        == (savedPreset.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::CrossedMachines
                            || savedPreset.params.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Transcode ? 1u : 0u)
                    && savedPreset.params.modEnvelope2 == 1u
                    && savedPreset.params.modEnvelope3 == 1u
                    && savedPreset.params.modulationEnabled == 1u
                    && savedPreset.params.modSource != s3g::PsdRawFieldModSource::Noise
                    && savedPreset.params.modSource2 != s3g::PsdRawFieldModSource::Noise
                    && savedPreset.params.modSource3 != s3g::PsdRawFieldModSource::Noise
                    && (curatedDestination(savedPreset.params.modTarget)
                        || curatedDestination(savedPreset.params.modTarget2)
                        || curatedDestination(savedPreset.params.modTarget3))
                    && !destructiveDestination(savedPreset.params.modTarget)
                    && !destructiveDestination(savedPreset.params.modTarget2)
                    && !destructiveDestination(savedPreset.params.modTarget3)
                    && savedPreset.params.texture <= 0.46f
                    && savedPreset.params.chaos <= 0.40f
                    && savedPreset.params.fold <= 0.34f
                    && savedPreset.params.codecDamage <= 0.24f
                    && savedPreset.params.shred <= 0.20f
                    && static_cast<uint32_t>(savedPreset.params.bassReceiver)
                        < s3g::kPsdRawFieldBassReceiverCount
                    && savedPreset.params.bassBody >= 0.42f
                    && savedPreset.params.bassBody <= 0.88f
                    && savedPreset.params.bassPunch >= 0.15f
                    && savedPreset.params.bassPunch <= 0.92f
                    && savedPreset.params.bassTrace >= 0.22f
                    && savedPreset.params.bassTrace <= 0.72f
                    && savedPreset.params.bassPitchTracking
                        != s3g::PsdRawFieldPitchTracking::Scan
                    && savedPreset.params.bassGlide <= 0.28f
                    && savedPreset.params.bassOctave
                        != s3g::PsdRawFieldBassOctave::Unison
                    && savedPreset.params.bassLowWidth >= 0.03f
                    && savedPreset.params.bassLowWidth <= 0.45f
                    && savedPreset.params.bassFuzz >= 0.28f
                    && savedPreset.params.bassFuzz <= 0.80f
                    && savedPreset.params.bassMetal >= 0.24f
                    && savedPreset.params.bassMetal <= 0.86f
                    && savedPreset.params.bassFeedback >= 0.18f
                    && savedPreset.params.bassFeedback <= 0.58f;
                if (ok) ++algorithmCounts[algorithm];
            }
            for (uint32_t count : algorithmCounts) ok = ok && count == 2u;
            if (!ok) std::cerr << "Fault factory presets did not cover all six algorithms twice\n";
        }
        if (ok) {
            auto targetIndexLimit = [](s3g::PsdRawFieldModTarget target) {
                switch (target) {
                case s3g::PsdRawFieldModTarget::Body: return 0.70f;
                case s3g::PsdRawFieldModTarget::Ring: return 0.62f;
                case s3g::PsdRawFieldModTarget::Strike: return 0.70f;
                case s3g::PsdRawFieldModTarget::Fold: return 0.48f;
                case s3g::PsdRawFieldModTarget::Scan: return 0.28f;
                case s3g::PsdRawFieldModTarget::Clock:
                case s3g::PsdRawFieldModTarget::Carrier: return 0.30f;
                case s3g::PsdRawFieldModTarget::Deviation: return 0.24f;
                case s3g::PsdRawFieldModTarget::Data:
                case s3g::PsdRawFieldModTarget::Damage: return 0.22f;
                case s3g::PsdRawFieldModTarget::Off:
                default: return 0.68f;
                }
            };
            auto guarded = [&](const s3g::PsdRawFieldParams& rp) {
                const bool transcode = rp.modAlgorithm == s3g::PsdRawFieldModAlgorithm::Transcode;
                const auto safeTarget = [](s3g::PsdRawFieldModTarget target) {
                    return target != s3g::PsdRawFieldModTarget::Data
                        && target != s3g::PsdRawFieldModTarget::Damage
                        && target != s3g::PsdRawFieldModTarget::Deviation;
                };
                return rp.modSource != s3g::PsdRawFieldModSource::Off
                    && rp.modSource2 != s3g::PsdRawFieldModSource::Off
                    && rp.modSource3 != s3g::PsdRawFieldModSource::Off
                    && rp.modIndex >= 0.06f && rp.modIndex <= targetIndexLimit(rp.modTarget)
                    && rp.modIndex2 >= 0.06f && rp.modIndex2 <= targetIndexLimit(rp.modTarget2)
                    && rp.modIndex3 >= 0.06f && rp.modIndex3 <= targetIndexLimit(rp.modTarget3)
                    && rp.modFeedback <= (rp.modSource == s3g::PsdRawFieldModSource::Feedback ? 0.55f : 0.48f)
                    && rp.modFeedback2 <= (rp.modSource2 == s3g::PsdRawFieldModSource::Feedback ? 0.55f : 0.48f)
                    && rp.modFeedback3 <= (rp.modSource3 == s3g::PsdRawFieldModSource::Feedback ? 0.55f : 0.48f)
                    && rp.texture >= 0.16f && rp.texture <= 0.68f
                    && rp.chaos >= 0.10f && rp.chaos <= 0.62f
                    && rp.fold >= 0.04f && rp.fold <= 0.52f
                    && rp.codecDamage <= 0.36f && rp.drive <= 0.78f
                    && rp.shred >= 0.04f && rp.shred <= 0.42f
                    && rp.resonance >= 0.16f && rp.resonance <= 0.88f
                    && static_cast<uint32_t>(rp.bassReceiver)
                        < s3g::kPsdRawFieldBassReceiverCount
                    && rp.bassBody >= 0.42f && rp.bassBody <= 0.92f
                    && rp.bassPunch >= 0.08f && rp.bassPunch <= 0.92f
                    && rp.bassTrace >= 0.18f && rp.bassTrace <= 0.78f
                    && rp.bassPitchTracking != s3g::PsdRawFieldPitchTracking::Scan
                    && rp.bassGlide >= 0.0f && rp.bassGlide <= 0.55f
                    && rp.bassOctave != s3g::PsdRawFieldBassOctave::Unison
                    && rp.bassLowWidth >= 0.02f && rp.bassLowWidth <= 0.65f
                    && rp.bassFuzz >= 0.14f && rp.bassFuzz <= 0.86f
                    && rp.bassMetal >= 0.12f && rp.bassMetal <= 0.94f
                    && rp.bassFeedback >= 0.08f && rp.bassFeedback <= 0.72f
                    && rp.modulationEnabled == 1u
                    && rp.modEnvelope2 == 1u && rp.modEnvelope3 == 1u
                    && safeTarget(rp.modTarget) && safeTarget(rp.modTarget2)
                    && safeTarget(rp.modTarget3)
                    && (transcode
                        ? rp.modTarget == s3g::PsdRawFieldModTarget::Off
                            && rp.modTarget2 == s3g::PsdRawFieldModTarget::Off
                            && (rp.modTarget3 == s3g::PsdRawFieldModTarget::Body
                                || rp.modTarget3 == s3g::PsdRawFieldModTarget::Strike)
                        : rp.modTarget != s3g::PsdRawFieldModTarget::Off
                            && rp.modTarget2 != s3g::PsdRawFieldModTarget::Off
                            && rp.modTarget3 != s3g::PsdRawFieldModTarget::Off);
            };
            for (uint32_t iteration = 0u; ok && iteration < 64u; ++iteration) {
                flushParam(kRandomizePatchParamId,
                    std::fmod(0.071 + static_cast<double>(iteration) * 0.053, 1.0));
                MemoryOutput randomOutput;
                clap_ostream_t randomStream { &randomOutput, streamWrite };
                ok = stateExtension->save(plugin, &randomStream)
                    && randomOutput.bytes.size() == sizeof(SavedStateV23);
                if (!ok) break;
                SavedStateV23 randomized {};
                std::memcpy(&randomized, randomOutput.bytes.data(), sizeof(randomized));
                const auto& rp = randomized.params;
                ok = guarded(rp);
            }
            if (!ok) std::cerr << "Fault curated random escaped its modulation guardrails\n";
            for (uint32_t iteration = 0u; ok && iteration < 64u; ++iteration) {
                flushParam(kMutateParamId,
                    std::fmod(0.193 + static_cast<double>(iteration) * 0.071, 1.0));
                MemoryOutput mutateOutput;
                clap_ostream_t mutateStream { &mutateOutput, streamWrite };
                ok = stateExtension->save(plugin, &mutateStream)
                    && mutateOutput.bytes.size() == sizeof(SavedStateV23);
                if (!ok) break;
                SavedStateV23 mutated {};
                std::memcpy(&mutated, mutateOutput.bytes.data(), sizeof(mutated));
                ok = guarded(mutated.params);
            }
            if (!ok) std::cerr << "Fault MUTATE escaped its modulation guardrails\n";
        }
    }
    plugin->destroy(plugin);
    entry->deinit();
    dlclose(library);
    std::remove(wavePath.c_str());
    return ok ? 0 : 1;
}
