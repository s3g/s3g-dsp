#include "s3g_psd_raw_field.h"

#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kPsdRawFieldChannels;
constexpr uint32_t kCodecModeCount = s3g::kPsdRawFieldCodecModeCount;
constexpr uint32_t kCodecModeMax = kCodecModeCount - 1u;
constexpr uint32_t kWaveHistory = 512;
constexpr uint32_t kStateVersion = 15;
constexpr uint32_t kWaveTracePreset = 12;
constexpr uint32_t kCustomPreset = 13;
constexpr uint32_t kGuiWidth = 880;
constexpr uint32_t kGuiHeight = 846;
constexpr std::size_t kSourcePathCapacity = 4096u;

// IDs retain their nearest earlier meaning so existing automation has the best possible migration path.
constexpr clap_id kScanRateParamId = 1;
constexpr clap_id kTextureParamId = 2;
constexpr clap_id kFoldParamId = 8;
constexpr clap_id kCodecRateParamId = 10;
constexpr clap_id kBitDepthParamId = 11;
constexpr clap_id kCodecDamageParamId = 14;
constexpr clap_id kCodecModeParamId = 15;
constexpr clap_id kGainParamId = 17;
constexpr clap_id kSeedParamId = 22;
constexpr clap_id kRandomizeFieldParamId = 23;
constexpr clap_id kGeometryParamId = 24;
constexpr clap_id kChaosParamId = 26;
constexpr clap_id kChannelSchemeParamId = 29;
constexpr clap_id kChannelSpreadParamId = 30;
constexpr clap_id kShredParamId = 42;
constexpr clap_id kResonanceParamId = 43;
constexpr clap_id kDriveParamId = 48;
constexpr clap_id kPresetParamId = 50;
constexpr clap_id kRandomizePatchParamId = 51;
constexpr clap_id kEvolveParamId = 52;
constexpr clap_id kMutateParamId = 53;
constexpr clap_id kUndoParamId = 54;
constexpr clap_id kRunParamId = 55;
constexpr clap_id kPerformanceModeParamId = 56;
constexpr clap_id kAttackParamId = 57;
constexpr clap_id kDecayParamId = 58;
constexpr clap_id kSustainParamId = 59;
constexpr clap_id kReleaseParamId = 60;

enum class SourceInterpretation : uint32_t {
    Generated = 0u,
    RawBytes = 1u,
    Waveform = 2u,
};

enum class PerformanceMode : uint32_t {
    Free = 0u,
    Midi = 1u,
};

enum class EnvelopeStage : uint32_t {
    Idle = 0u,
    Attack,
    Decay,
    Sustain,
    Release,
};

struct LegacyParamsV13 {
    float scanRate, texture, geometry, chaos, fold, evolve;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, drive, shred, resonance, gainDb;
    uint32_t seed;
};
static_assert(sizeof(LegacyParamsV13) == 68u, "Unexpected version-13 parameter layout");

struct LegacySavedStateV10 {
    uint32_t version = 10u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
};
static_assert(sizeof(LegacySavedStateV10) == 76u, "Unexpected version-10 state layout");

struct LegacySavedStateV11 {
    uint32_t version = 11u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 0u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV11) == 4176u, "Unexpected version-11 state layout");

struct LegacySavedStateV12 {
    uint32_t version = 12u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 0u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV12) == 4176u, "Unexpected version-12 state layout");

struct LegacySavedStateV13 {
    uint32_t version = 13u;
    uint32_t selectedPreset = 0u;
    LegacyParamsV13 params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV13) == 4180u, "Unexpected version-13 state layout");

struct LegacySavedStateV14 {
    uint32_t version = 14u;
    uint32_t selectedPreset = 0u;
    s3g::PsdRawFieldParams params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(LegacySavedStateV14) == 4184u, "Unexpected version-14 state layout");

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t selectedPreset = 0u;
    s3g::PsdRawFieldParams params {};
    uint32_t sourceMode = 0u;
    uint32_t runState = 1u;
    uint32_t performanceMode = static_cast<uint32_t>(PerformanceMode::Free);
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    char sourcePath[kSourcePathCapacity] {};
};
static_assert(sizeof(SavedState) == 4204u, "Unexpected version-15 state layout");

struct LegacyParamsV8 {
    float rawRate, strata, compression, masks, metadata, colorBleed, byteSkew, channelSpread, fold;
    float connections, connectionRate, periodChaos, pitchJumps, interpDisaster;
    float dcBlock, downsample, bitDepth, predictor, packetLoss, codebook;
    float feedback, feedbackTime, feedbackTone, feedbackCross;
    float stochastic, ampStep, durStep, stochasticMemory, tendency, rest;
    float shaper, shaperInput, shaperPressure, shaperShred, shaperFeedback;
    float shaperColor, shaperReact, shaperTune, shaperBody, shaperSpread;
    uint32_t codecMode, compandMode, channelScheme, sourceMode, ampDistribution, durDistribution;
    float gainDb;
    uint32_t seed;
};
static_assert(sizeof(LegacyParamsV8) == 192u, "Unexpected version-8 state layout");

struct LegacyParamsV9 {
    float scanRate, texture, geometry, chaos, fold;
    uint32_t channelScheme;
    float channelSpread;
    uint32_t codecMode;
    float codecRate, bitDepth, codecDamage, drive, shred, resonance, gainDb;
    uint32_t seed;
};
static_assert(sizeof(LegacyParamsV9) == 64u, "Unexpected version-9 state layout");

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::PsdRawFieldParams params {};
    s3g::PsdRawFieldMorph field;
    std::shared_ptr<const s3g::PsdRawFieldSource> rawSource;
    std::string sourcePath;
    std::string sourceName;
    std::string sourceError;
    SourceInterpretation sourceInterpretation = SourceInterpretation::Generated;
    std::atomic<bool> playing { true };
    float runGain = 1.0f;
    PerformanceMode performanceMode = PerformanceMode::Free;
    float attackMs = 12.0f;
    float decayMs = 280.0f;
    float sustain = 0.72f;
    float releaseMs = 850.0f;
    EnvelopeStage envelopeStage = EnvelopeStage::Idle;
    float envelopeValue = 0.0f;
    bool envelopeGate = false;
    float activeVelocity = 1.0f;
    int32_t activeNote = -1;
    uint64_t noteOrderCounter = 0u;
    std::array<uint16_t, 128> heldNoteCount {};
    std::array<float, 128> heldVelocity {};
    std::array<uint64_t, 128> heldNoteOrder {};
    std::atomic<int32_t> displayNote { -1 };
    std::atomic<float> displayEnvelope { 0.0f };
    std::atomic<uint32_t> displayEnvelopeStage { static_cast<uint32_t>(EnvelopeStage::Idle) };
    uint32_t selectedPreset = 0u;
    s3g::PsdRawFieldParams undoParams {};
    std::shared_ptr<const s3g::PsdRawFieldSource> undoSource;
    std::string undoSourcePath;
    std::string undoSourceName;
    std::string undoSourceError;
    SourceInterpretation undoSourceInterpretation = SourceInterpretation::Generated;
    uint32_t undoPreset = 0u;
    bool hasUndo = false;
    std::vector<std::vector<float>> output32;
    std::vector<float*> outputPtrs;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::array<float, kWaveHistory>, kOutputChannels> waveHistory {};
    std::atomic<uint32_t> waveWrite { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

uint32_t hash32(uint32_t x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

void resetMidiPerformance(Plugin& p)
{
    p.envelopeStage = EnvelopeStage::Idle;
    p.envelopeValue = 0.0f;
    p.envelopeGate = false;
    p.activeVelocity = 1.0f;
    p.activeNote = -1;
    p.noteOrderCounter = 0u;
    p.heldNoteCount.fill(0u);
    p.heldVelocity.fill(0.0f);
    p.heldNoteOrder.fill(0u);
    p.displayNote.store(-1, std::memory_order_relaxed);
    p.displayEnvelope.store(0.0f, std::memory_order_relaxed);
    p.displayEnvelopeStage.store(static_cast<uint32_t>(EnvelopeStage::Idle), std::memory_order_relaxed);
    p.field.setPitchRatio(1.0f);
}

void setActiveNote(Plugin& p, uint32_t key, float velocity)
{
    key = std::min<uint32_t>(key, 127u);
    p.activeNote = static_cast<int32_t>(key);
    p.activeVelocity = std::clamp(velocity, 0.0f, 1.0f);
    p.displayNote.store(static_cast<int32_t>(key), std::memory_order_relaxed);
    p.field.setPitchRatio(std::pow(2.0f, (static_cast<float>(key) - 60.0f) / 12.0f));
}

int32_t latestHeldNote(const Plugin& p)
{
    int32_t selected = -1;
    uint64_t newest = 0u;
    for (uint32_t key = 0u; key < p.heldNoteCount.size(); ++key) {
        if (p.heldNoteCount[key] > 0u && (selected < 0 || p.heldNoteOrder[key] > newest)) {
            selected = static_cast<int32_t>(key);
            newest = p.heldNoteOrder[key];
        }
    }
    return selected;
}

void midiNoteOn(Plugin& p, int32_t key, float velocity)
{
    if (p.performanceMode != PerformanceMode::Midi || key < 0 || key > 127 || velocity <= 0.0f) return;
    const uint32_t index = static_cast<uint32_t>(key);
    if (p.heldNoteCount[index] < std::numeric_limits<uint16_t>::max()) ++p.heldNoteCount[index];
    p.heldVelocity[index] = std::clamp(velocity, 0.0f, 1.0f);
    p.heldNoteOrder[index] = ++p.noteOrderCounter;
    setActiveNote(p, index, p.heldVelocity[index]);
    p.envelopeGate = true;
    p.envelopeStage = EnvelopeStage::Attack;
}

void releaseMidiEnvelope(Plugin& p, bool immediate)
{
    p.envelopeGate = false;
    p.activeNote = -1;
    p.displayNote.store(-1, std::memory_order_relaxed);
    if (immediate) {
        p.envelopeStage = EnvelopeStage::Idle;
        p.envelopeValue = 0.0f;
        p.displayEnvelope.store(0.0f, std::memory_order_relaxed);
    } else if (p.envelopeStage != EnvelopeStage::Idle) {
        p.envelopeStage = EnvelopeStage::Release;
    }
}

void midiAllNotesOff(Plugin& p, bool immediate)
{
    p.heldNoteCount.fill(0u);
    p.heldNoteOrder.fill(0u);
    releaseMidiEnvelope(p, immediate);
}

void midiNoteOff(Plugin& p, int32_t key, bool immediate)
{
    if (p.performanceMode != PerformanceMode::Midi) return;
    if (key < 0 || key > 127) {
        midiAllNotesOff(p, immediate);
        return;
    }
    const uint32_t index = static_cast<uint32_t>(key);
    if (p.heldNoteCount[index] > 0u) --p.heldNoteCount[index];
    if (p.heldNoteCount[index] == 0u) p.heldNoteOrder[index] = 0u;
    if (p.activeNote != key || p.heldNoteCount[index] > 0u) return;

    const int32_t fallback = latestHeldNote(p);
    if (fallback >= 0) {
        const uint32_t fallbackIndex = static_cast<uint32_t>(fallback);
        setActiveNote(p, fallbackIndex, p.heldVelocity[fallbackIndex]);
    } else {
        releaseMidiEnvelope(p, immediate);
    }
}

float envelopeCoefficient(float milliseconds, double sampleRate)
{
    const float samples = std::max(1.0f,
        milliseconds * 0.001f * static_cast<float>(std::max(1.0, sampleRate)));
    return 1.0f - std::exp(-6.90775527898f / samples);
}

float processMidiEnvelope(Plugin& p, float attack, float decay, float release)
{
    switch (p.envelopeStage) {
    case EnvelopeStage::Attack:
        p.envelopeValue += (1.0f - p.envelopeValue) * attack;
        if (p.envelopeValue >= 0.999f) {
            p.envelopeValue = 1.0f;
            p.envelopeStage = EnvelopeStage::Decay;
        }
        break;
    case EnvelopeStage::Decay:
        p.envelopeValue += (p.sustain - p.envelopeValue) * decay;
        if (std::abs(p.envelopeValue - p.sustain) <= 0.001f) {
            p.envelopeValue = p.sustain;
            p.envelopeStage = EnvelopeStage::Sustain;
        }
        break;
    case EnvelopeStage::Sustain:
        p.envelopeValue = p.sustain;
        if (!p.envelopeGate) p.envelopeStage = EnvelopeStage::Release;
        break;
    case EnvelopeStage::Release:
        p.envelopeValue += (0.0f - p.envelopeValue) * release;
        if (p.envelopeValue <= 0.00005f) {
            p.envelopeValue = 0.0f;
            p.envelopeStage = EnvelopeStage::Idle;
        }
        break;
    case EnvelopeStage::Idle:
    default:
        p.envelopeValue = 0.0f;
        break;
    }
    return p.envelopeValue;
}

std::string sourceNameFromPath(const std::string& path)
{
    const std::size_t separator = path.find_last_of("/\\");
    if (separator == std::string::npos || separator + 1u >= path.size()) return path;
    return path.substr(separator + 1u);
}

// WAV structure is used only to derive an eight-lane byte field; playback remains deliberately lossy.
enum class WaveEncoding : uint32_t {
    Unsigned8,
    Signed16,
    Signed24,
    Signed32,
    Float32,
    Float64,
};

struct WaveFileInfo {
    uint64_t fileByteCount = 0u;
    uint64_t dataOffset = 0u;
    uint64_t dataByteCount = 0u;
    uint32_t sampleRate = 0u;
    uint32_t channelCount = 0u;
    uint32_t bitsPerSample = 0u;
    uint32_t blockAlign = 0u;
    uint32_t bytesPerSample = 0u;
    WaveEncoding encoding = WaveEncoding::Unsigned8;
};

uint16_t little16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0])
        | static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8u);
}

uint32_t little32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])
        | (static_cast<uint32_t>(bytes[1]) << 8u)
        | (static_cast<uint32_t>(bytes[2]) << 16u)
        | (static_cast<uint32_t>(bytes[3]) << 24u);
}

uint64_t little64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(little32(bytes))
        | (static_cast<uint64_t>(little32(bytes + 4u)) << 32u);
}

bool inspectWaveFile(const std::string& path, WaveFileInfo& info, std::string& error)
{
    info = {};
    error.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "FILE NOT FOUND";
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end < 12) {
        error = "NOT A PCM WAVE FILE";
        return false;
    }
    info.fileByteCount = static_cast<uint64_t>(end);
    input.seekg(0, std::ios::beg);
    std::array<uint8_t, 12> riff {};
    input.read(reinterpret_cast<char*>(riff.data()), static_cast<std::streamsize>(riff.size()));
    if (input.gcount() != static_cast<std::streamsize>(riff.size())
        || std::memcmp(riff.data(), "RIFF", 4u) != 0
        || std::memcmp(riff.data() + 8u, "WAVE", 4u) != 0) {
        error = "NOT A PCM WAVE FILE";
        return false;
    }

    bool foundFormat = false;
    bool foundData = false;
    uint64_t offset = 12u;
    while (offset + 8u <= info.fileByteCount) {
        input.clear();
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        std::array<uint8_t, 8> chunk {};
        input.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        if (input.gcount() != static_cast<std::streamsize>(chunk.size())) break;
        const uint64_t chunkSize = little32(chunk.data() + 4u);
        const uint64_t chunkData = offset + 8u;
        const uint64_t available = chunkData < info.fileByteCount ? info.fileByteCount - chunkData : 0u;

        if (std::memcmp(chunk.data(), "fmt ", 4u) == 0 && chunkSize >= 16u && available >= 16u) {
            std::array<uint8_t, 64> format {};
            const std::size_t readSize = static_cast<std::size_t>(std::min<uint64_t>(format.size(), std::min(chunkSize, available)));
            input.read(reinterpret_cast<char*>(format.data()), static_cast<std::streamsize>(readSize));
            if (input.gcount() == static_cast<std::streamsize>(readSize)) {
                uint16_t formatTag = little16(format.data());
                info.channelCount = little16(format.data() + 2u);
                info.sampleRate = little32(format.data() + 4u);
                info.blockAlign = little16(format.data() + 12u);
                info.bitsPerSample = little16(format.data() + 14u);
                if (formatTag == 0xfffeu && readSize >= 40u) formatTag = little16(format.data() + 24u);
                info.bytesPerSample = (info.bitsPerSample + 7u) / 8u;
                bool supported = true;
                if (formatTag == 1u) {
                    if (info.bitsPerSample == 8u) info.encoding = WaveEncoding::Unsigned8;
                    else if (info.bitsPerSample == 16u) info.encoding = WaveEncoding::Signed16;
                    else if (info.bitsPerSample == 24u) info.encoding = WaveEncoding::Signed24;
                    else if (info.bitsPerSample == 32u) info.encoding = WaveEncoding::Signed32;
                    else supported = false;
                } else if (formatTag == 3u) {
                    if (info.bitsPerSample == 32u) info.encoding = WaveEncoding::Float32;
                    else if (info.bitsPerSample == 64u) info.encoding = WaveEncoding::Float64;
                    else supported = false;
                } else {
                    supported = false;
                }
                foundFormat = supported && info.channelCount > 0u && info.channelCount <= 64u
                    && info.sampleRate > 0u && info.sampleRate <= 768000u
                    && info.blockAlign >= info.channelCount * info.bytesPerSample;
            }
        } else if (std::memcmp(chunk.data(), "data", 4u) == 0 && !foundData) {
            info.dataOffset = chunkData;
            info.dataByteCount = std::min(chunkSize, available);
            foundData = info.dataByteCount > 0u;
        }

        if (foundFormat && foundData) break;
        if (chunkSize > info.fileByteCount || chunkData + chunkSize < chunkData) break;
        const uint64_t next = chunkData + chunkSize + (chunkSize & 1u);
        if (next <= offset || next > info.fileByteCount) break;
        offset = next;
    }

    if (!foundFormat || !foundData || info.dataByteCount < info.blockAlign) {
        error = "UNSUPPORTED OR EMPTY WAVE DATA";
        return false;
    }
    return true;
}

float decodeWaveSample(const std::vector<uint8_t>& bytes, const WaveFileInfo& info, uint64_t frame, uint32_t channel)
{
    const uint64_t byteOffset = frame * info.blockAlign + static_cast<uint64_t>(channel) * info.bytesPerSample;
    if (byteOffset + info.bytesPerSample > bytes.size()) return 0.0f;
    const uint8_t* sample = bytes.data() + byteOffset;
    switch (info.encoding) {
    case WaveEncoding::Unsigned8:
        return (static_cast<float>(sample[0]) - 127.5f) * (1.0f / 127.5f);
    case WaveEncoding::Signed16: {
        const uint32_t value = little16(sample);
        const int32_t signedValue = value >= 0x8000u ? static_cast<int32_t>(value) - 0x10000 : static_cast<int32_t>(value);
        return static_cast<float>(signedValue) * (1.0f / 32768.0f);
    }
    case WaveEncoding::Signed24: {
        const uint32_t value = static_cast<uint32_t>(sample[0])
            | (static_cast<uint32_t>(sample[1]) << 8u)
            | (static_cast<uint32_t>(sample[2]) << 16u);
        const int32_t signedValue = (value & 0x800000u) != 0u
            ? static_cast<int32_t>(value | 0xff000000u)
            : static_cast<int32_t>(value);
        return static_cast<float>(signedValue) * (1.0f / 8388608.0f);
    }
    case WaveEncoding::Signed32: {
        const uint32_t value = little32(sample);
        const int64_t signedValue = value >= 0x80000000u
            ? static_cast<int64_t>(value) - 0x100000000ll
            : static_cast<int64_t>(value);
        return static_cast<float>(static_cast<double>(signedValue) * (1.0 / 2147483648.0));
    }
    case WaveEncoding::Float32: {
        const uint32_t bits = little32(sample);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
    }
    case WaveEncoding::Float64: {
        const uint64_t bits = little64(sample);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value) ? static_cast<float>(std::clamp(value, -1.0, 1.0)) : 0.0f;
    }
    }
    return 0.0f;
}

bool readRawByteSource(
    const std::string& path,
    std::shared_ptr<const s3g::PsdRawFieldSource>& source,
    std::string& error)
{
    source.reset();
    error.clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "FILE NOT FOUND";
        return false;
    }
    const std::streamoff end = input.tellg();
    if (end <= 0) {
        error = end == 0 ? "EMPTY FILE" : "UNREADABLE FILE";
        return false;
    }
    const uint64_t originalByteCount = static_cast<uint64_t>(end);
    const std::size_t loadedByteCount = static_cast<std::size_t>(
        std::min<uint64_t>(originalByteCount, s3g::kPsdRawFieldMaxSourceBytes));
    std::vector<uint8_t> bytes;
    try {
        bytes.resize(loadedByteCount);
    } catch (...) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
        error = "READ ERROR";
        return false;
    }
    source = s3g::makePsdRawFieldSource(std::move(bytes), originalByteCount);
    if (!source) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    return true;
}

bool readWaveformSource(
    const std::string& path,
    std::shared_ptr<const s3g::PsdRawFieldSource>& source,
    std::string& error)
{
    source.reset();
    WaveFileInfo info {};
    if (!inspectWaveFile(path, info, error)) return false;
    const uint64_t totalFrames = info.dataByteCount / info.blockAlign;
    const uint64_t maxFieldFrames = s3g::kPsdRawFieldMaxSourceBytes / kOutputChannels;
    const uint64_t maxInputFrames = s3g::kPsdRawFieldMaxSourceBytes / info.blockAlign;
    const uint64_t loadedFrames = std::min({ totalFrames, maxFieldFrames, maxInputFrames });
    if (loadedFrames == 0u) {
        error = "EMPTY WAVE DATA";
        return false;
    }

    std::vector<uint8_t> inputBytes;
    std::vector<uint8_t> fieldBytes;
    try {
        inputBytes.resize(static_cast<std::size_t>(loadedFrames * info.blockAlign));
        fieldBytes.resize(static_cast<std::size_t>(loadedFrames * kOutputChannels));
    } catch (...) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    input.seekg(static_cast<std::streamoff>(info.dataOffset), std::ios::beg);
    input.read(reinterpret_cast<char*>(inputBytes.data()), static_cast<std::streamsize>(inputBytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(inputBytes.size())) {
        error = "WAVE DATA READ ERROR";
        return false;
    }

    const uint64_t peakStride = std::max<uint64_t>(1u, loadedFrames / 250000u);
    float peak = 0.0f;
    for (uint64_t frame = 0u; frame < loadedFrames; frame += peakStride) {
        for (uint32_t channel = 0u; channel < info.channelCount; ++channel) {
            peak = std::max(peak, std::abs(decodeWaveSample(inputBytes, info, frame, channel)));
        }
    }
    const float preparationGain = peak > 0.0001f
        ? std::clamp(0.82f / peak, 1.0f, 8.0f)
        : 1.0f;
    // Repeated source channels receive short prime-spaced offsets so every output lane has motion.
    constexpr std::array<uint32_t, kOutputChannels> laneOffsets { 0u, 13u, 29u, 61u, 127u, 251u, 509u, 1021u };
    for (uint64_t frame = 0u; frame < loadedFrames; ++frame) {
        for (uint32_t lane = 0u; lane < kOutputChannels; ++lane) {
            const uint32_t sourceChannel = lane % info.channelCount;
            const uint32_t repetition = std::min<uint32_t>(lane / info.channelCount, kOutputChannels - 1u);
            const uint64_t sourceFrame = (frame + laneOffsets[repetition]) % loadedFrames;
            const float sample = std::clamp(
                decodeWaveSample(inputBytes, info, sourceFrame, sourceChannel) * preparationGain,
                -1.0f,
                1.0f);
            fieldBytes[static_cast<std::size_t>(frame * kOutputChannels + lane)] = static_cast<uint8_t>(
                std::round((sample * 0.5f + 0.5f) * 255.0f));
        }
    }

    s3g::PsdRawFieldWaveformInfo waveform {};
    waveform.sampleRate = info.sampleRate;
    waveform.channelCount = info.channelCount;
    waveform.bitsPerSample = info.bitsPerSample;
    waveform.sourceFrameCount = totalFrames;
    waveform.loadedFrameCount = loadedFrames;
    waveform.sourceDataByteCount = info.dataByteCount;
    waveform.truncated = loadedFrames < totalFrames;
    source = s3g::makePsdRawFieldSource(std::move(fieldBytes), info.fileByteCount, waveform);
    if (!source) {
        error = "NOT ENOUGH MEMORY";
        return false;
    }
    error.clear();
    return true;
}

bool readSource(
    const std::string& path,
    SourceInterpretation interpretation,
    std::shared_ptr<const s3g::PsdRawFieldSource>& source,
    std::string& error)
{
    if (interpretation == SourceInterpretation::Waveform) {
        return readWaveformSource(path, source, error);
    }
    return readRawByteSource(path, source, error);
}

void markHostStateDirty(Plugin& p)
{
    if (!p.host || !p.host->get_extension) return;
    const auto* state = static_cast<const clap_host_state_t*>(p.host->get_extension(p.host, CLAP_EXT_STATE));
    if (state && state->mark_dirty) state->mark_dirty(p.host);
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV13& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = old.scanRate;
    result.texture = old.texture;
    result.geometry = old.geometry;
    result.chaos = old.chaos;
    result.fold = old.fold;
    result.evolve = old.evolve;
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = old.channelSpread;
    result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, kCodecModeMax));
    result.codecRate = old.codecRate;
    result.bitDepth = old.bitDepth;
    result.codecDamage = old.codecDamage;
    result.drive = old.drive;
    result.shred = old.shred;
    result.resonance = old.resonance;
    result.gainDb = old.gainDb;
    result.seed = old.seed;
    result.fieldCodecMode = result.codecMode;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV8& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = std::clamp(old.rawRate, 0.0f, 1.0f);
    result.texture = std::clamp(old.strata * 0.24f + old.compression * 0.24f + old.masks * 0.20f
        + old.colorBleed * 0.17f + old.metadata * 0.15f, 0.0f, 1.0f);
    const float sourceAmount = old.sourceMode == 0u ? 0.0f : (old.sourceMode == 1u ? 1.0f : old.stochastic);
    result.geometry = std::clamp(std::max(old.connections, sourceAmount), 0.0f, 1.0f);
    result.chaos = std::clamp(old.periodChaos * 0.24f + old.pitchJumps * 0.22f
        + old.interpDisaster * 0.24f + old.ampStep * 0.15f + old.durStep * 0.15f, 0.0f, 1.0f);
    result.fold = std::clamp(old.fold, 0.0f, 1.0f);
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = std::clamp(old.channelSpread, 0.0f, 1.0f);

    if (old.codecMode == 4u) result.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble;
    else if (old.codecMode == 3u && old.compandMode == 2u) result.codecMode = s3g::PsdRawFieldCodecMode::ALaw;
    else if (old.codecMode == 3u && old.compandMode == 1u) result.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
    else result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, 2u));
    result.codecRate = std::clamp(old.downsample, 0.0f, 1.0f);
    result.bitDepth = std::clamp(old.bitDepth, 2.0f, 16.0f);
    result.codecDamage = std::clamp(std::max({ old.packetLoss * 2.5f, old.codebook * 0.75f, old.predictor * 0.45f }), 0.0f, 1.0f);

    result.drive = std::clamp(old.shaperInput * (0.25f + old.shaper * 0.75f), 0.0f, 1.0f);
    result.shred = std::clamp(old.shaper * 0.35f + old.shaperShred * 0.45f + old.shaperPressure * 0.20f, 0.0f, 1.0f);
    result.resonance = std::clamp(old.shaperFeedback * old.shaper, 0.0f, 1.0f);
    result.gainDb = std::clamp(old.gainDb, -60.0f, 6.0f);
    result.seed = old.seed ? old.seed : 0x50434431u;
    result.fieldCodecMode = result.codecMode;
    return result;
}

s3g::PsdRawFieldParams migrateLegacyParams(const LegacyParamsV9& old)
{
    s3g::PsdRawFieldParams result {};
    result.scanRate = old.scanRate;
    result.texture = old.texture;
    result.geometry = old.geometry;
    result.chaos = old.chaos;
    result.fold = old.fold;
    result.evolve = 0.0f;
    result.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(std::min(old.channelScheme, 4u));
    result.channelSpread = old.channelSpread;
    result.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(old.codecMode, 5u));
    result.codecRate = old.codecRate;
    result.bitDepth = old.bitDepth;
    result.codecDamage = old.codecDamage;
    result.drive = old.drive;
    result.shred = old.shred;
    result.resonance = old.resonance;
    result.gainDb = old.gainDb;
    result.seed = old.seed;
    result.fieldCodecMode = result.codecMode;
    return result;
}

s3g::PsdRawFieldParams presetParams(uint32_t preset)
{
    s3g::PsdRawFieldParams result {};
    switch (preset) {
    case 1u: // Slow Fold
        result.scanRate = 0.16f; result.texture = 0.58f; result.geometry = 0.78f; result.chaos = 0.64f;
        result.fold = 0.90f; result.evolve = 0.08f; result.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
        result.codecRate = 0.70f; result.bitDepth = 7.0f; result.codecDamage = 0.22f;
        result.drive = 0.76f; result.shred = 0.74f; result.resonance = 0.18f; result.gainDb = -12.5f;
        break;
    case 2u: // Codec Scar
        result.scanRate = 0.48f; result.texture = 0.82f; result.geometry = 0.42f; result.chaos = 0.52f;
        result.fold = 0.54f; result.evolve = 0.16f; result.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble;
        result.codecRate = 0.80f; result.bitDepth = 4.0f; result.codecDamage = 0.84f;
        result.drive = 0.82f; result.shred = 0.58f; result.resonance = 0.30f; result.gainDb = -15.0f;
        break;
    case 3u: // Diverge
        result.scanRate = 0.34f; result.texture = 0.72f; result.geometry = 0.88f; result.chaos = 0.92f;
        result.fold = 0.74f; result.evolve = 0.12f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Divergent;
        result.channelSpread = 0.96f; result.codecMode = s3g::PsdRawFieldCodecMode::Adpcm;
        result.codecRate = 0.54f; result.bitDepth = 5.0f; result.codecDamage = 0.52f;
        result.drive = 0.88f; result.shred = 0.78f; result.resonance = 0.42f; result.gainDb = -14.0f;
        break;
    case 4u: // Breaks
        result.scanRate = 0.26f; result.texture = 0.52f; result.geometry = 1.0f; result.chaos = 0.80f;
        result.fold = 0.66f; result.evolve = 0.10f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Shuffled;
        result.channelSpread = 0.86f; result.codecMode = s3g::PsdRawFieldCodecMode::DeltaPcm;
        result.codecRate = 0.42f; result.bitDepth = 6.0f; result.codecDamage = 0.36f;
        result.drive = 0.72f; result.shred = 0.70f; result.resonance = 0.26f; result.gainDb = -12.5f;
        break;
    case 5u: // Glass Grid
        result.scanRate = 0.62f; result.texture = 0.34f; result.geometry = 0.72f; result.chaos = 0.30f;
        result.fold = 0.96f; result.evolve = 0.04f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Planes;
        result.channelSpread = 0.78f; result.codecMode = s3g::PsdRawFieldCodecMode::RawPcm;
        result.codecRate = 0.18f; result.bitDepth = 10.0f; result.codecDamage = 0.10f;
        result.drive = 0.74f; result.shred = 0.92f; result.resonance = 0.30f; result.gainDb = -13.5f;
        break;
    case 6u: // Mu Dust
        result.scanRate = 0.40f; result.texture = 0.92f; result.geometry = 0.34f; result.chaos = 0.66f;
        result.fold = 0.50f; result.evolve = 0.22f; result.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
        result.codecRate = 0.78f; result.bitDepth = 5.0f; result.codecDamage = 0.44f;
        result.drive = 0.66f; result.shred = 0.48f; result.resonance = 0.12f; result.gainDb = -12.0f;
        break;
    case 7u: // Delta Stairs
        result.scanRate = 0.28f; result.texture = 0.60f; result.geometry = 0.90f; result.chaos = 0.46f;
        result.fold = 0.70f; result.evolve = 0.08f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Planes;
        result.channelSpread = 0.82f; result.codecMode = s3g::PsdRawFieldCodecMode::DeltaPcm;
        result.codecRate = 0.70f; result.bitDepth = 4.0f; result.codecDamage = 0.66f;
        result.drive = 0.82f; result.shred = 0.72f; result.resonance = 0.22f; result.gainDb = -13.0f;
        break;
    case 8u: // ADPCM Tear
        result.scanRate = 0.55f; result.texture = 0.86f; result.geometry = 0.82f; result.chaos = 0.95f;
        result.fold = 0.82f; result.evolve = 0.18f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Divergent;
        result.channelSpread = 0.94f; result.codecMode = s3g::PsdRawFieldCodecMode::Adpcm;
        result.codecRate = 0.48f; result.bitDepth = 4.0f; result.codecDamage = 0.80f;
        result.drive = 0.90f; result.shred = 0.86f; result.resonance = 0.36f; result.gainDb = -15.0f;
        break;
    case 9u: // CELP Choir
        result.scanRate = 0.38f; result.texture = 0.55f; result.geometry = 0.62f; result.chaos = 0.48f;
        result.fold = 0.46f; result.evolve = 0.14f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Planes;
        result.channelSpread = 0.95f; result.codecMode = s3g::PsdRawFieldCodecMode::CelpScramble;
        result.codecRate = 0.32f; result.bitDepth = 7.0f; result.codecDamage = 0.42f;
        result.drive = 0.76f; result.shred = 0.50f; result.resonance = 0.55f; result.gainDb = -15.0f;
        break;
    case 10u: // Low Embers
        result.scanRate = 0.08f; result.texture = 0.40f; result.geometry = 0.70f; result.chaos = 0.22f;
        result.fold = 0.92f; result.evolve = 0.05f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Parallel;
        result.channelSpread = 0.46f; result.codecMode = s3g::PsdRawFieldCodecMode::MuLaw;
        result.codecRate = 0.86f; result.bitDepth = 6.0f; result.codecDamage = 0.20f;
        result.drive = 0.78f; result.shred = 0.80f; result.resonance = 0.18f; result.gainDb = -12.0f;
        break;
    case 11u: // Wide Static
        result.scanRate = 0.72f; result.texture = 1.0f; result.geometry = 0.48f; result.chaos = 0.88f;
        result.fold = 0.58f; result.evolve = 0.28f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Shuffled;
        result.channelSpread = 1.0f; result.codecMode = s3g::PsdRawFieldCodecMode::ALaw;
        result.codecRate = 0.56f; result.bitDepth = 5.0f; result.codecDamage = 0.62f;
        result.drive = 0.84f; result.shred = 0.68f; result.resonance = 0.30f; result.gainDb = -14.0f;
        break;
    case kWaveTracePreset: // Wave Trace
        result.scanRate = 0.50f; result.texture = 0.22f; result.geometry = 0.20f; result.chaos = 0.18f;
        result.fold = 0.14f; result.evolve = 0.0f; result.channelScheme = s3g::PsdRawFieldChannelScheme::Deinterleave;
        result.channelSpread = 0.92f; result.codecMode = s3g::PsdRawFieldCodecMode::RawPcm;
        result.codecRate = 0.0f; result.bitDepth = 12.0f; result.codecDamage = 0.0f;
        result.drive = 0.20f; result.shred = 0.14f; result.resonance = 0.04f; result.gainDb = -8.0f;
        break;
    case 0u:
    default:
        break;
    }
    result.fieldCodecMode = result.codecMode;
    if (preset != 0u) result.seed = hash32(0x50434431u ^ (preset * 0x9e3779b9u));
    return result;
}

bool paramsDiffer(const s3g::PsdRawFieldParams& a, const s3g::PsdRawFieldParams& b)
{
    return std::memcmp(&a, &b, sizeof(a)) != 0;
}

void saveUndo(Plugin& p)
{
    p.undoParams = p.params;
    p.undoSource = p.rawSource;
    p.undoSourcePath = p.sourcePath;
    p.undoSourceName = p.sourceName;
    p.undoSourceError = p.sourceError;
    p.undoSourceInterpretation = p.sourceInterpretation;
    p.undoPreset = p.selectedPreset;
    p.hasUndo = true;
}

void installRawSource(
    Plugin& p,
    std::shared_ptr<const s3g::PsdRawFieldSource> source,
    const std::string& path,
    SourceInterpretation interpretation,
    bool remember)
{
    if (!source) return;
    if (remember) saveUndo(p);
    p.rawSource = std::move(source);
    p.sourcePath = path;
    p.sourceName = sourceNameFromPath(path);
    p.sourceError.clear();
    p.sourceInterpretation = interpretation;
    if (p.rawSource->waveform && p.selectedPreset == 0u) {
        p.params = presetParams(kWaveTracePreset);
        p.selectedPreset = kWaveTracePreset;
    } else {
        p.selectedPreset = kCustomPreset;
    }
    p.field.transitionToSource(p.rawSource, p.params, 1.15f);
}

void useGeneratedField(Plugin& p, uint32_t salt, bool remember)
{
    if (remember) saveUndo(p);
    p.rawSource.reset();
    p.sourcePath.clear();
    p.sourceName.clear();
    p.sourceError.clear();
    p.sourceInterpretation = SourceInterpretation::Generated;
    p.params.fieldCodecMode = p.params.codecMode;
    p.params.seed = hash32(p.params.seed ^ salt ^ 0x6d2b79f5u);
    p.selectedPreset = kCustomPreset;
    p.field.transitionToSource({}, p.params, 1.15f);
}

void transitionPatch(Plugin& p, const s3g::PsdRawFieldParams& next, uint32_t preset, float seconds, bool remember)
{
    if (!paramsDiffer(p.params, next)) {
        p.selectedPreset = preset;
        return;
    }
    if (remember) saveUndo(p);
    p.params = next;
    p.selectedPreset = preset;
    p.field.transitionTo(p.params, seconds);
}

void applyPreset(Plugin& p, uint32_t preset)
{
    preset = std::min(preset, kCustomPreset);
    if (preset == kCustomPreset) { p.selectedPreset = kCustomPreset; return; }
    transitionPatch(p, presetParams(preset), preset, 0.90f, true);
}

void randomizePatch(Plugin& p, uint32_t salt)
{
    auto random01 = [&salt]() {
        salt = hash32(salt + 0x9e3779b9u);
        return static_cast<float>(salt & 0xffffu) / 65535.0f;
    };
    s3g::PsdRawFieldParams next = p.params;
    next.seed = hash32(next.seed ^ salt);
    next.scanRate = 0.10f + random01() * 0.62f;
    next.texture = 0.22f + random01() * 0.78f;
    next.geometry = 0.28f + random01() * 0.72f;
    next.chaos = 0.18f + random01() * 0.82f;
    next.fold = 0.34f + random01() * 0.66f;
    next.evolve = random01() * 0.34f;
    next.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(1u + static_cast<uint32_t>(random01() * 3.999f));
    next.channelSpread = 0.52f + random01() * 0.48f;
    next.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(std::min(
        static_cast<uint32_t>(random01() * static_cast<float>(kCodecModeCount)), kCodecModeMax));
    if (!p.rawSource) next.fieldCodecMode = next.codecMode;
    next.codecRate = 0.12f + random01() * 0.76f;
    next.bitDepth = 3.0f + std::floor(random01() * 8.0f);
    next.codecDamage = random01() * 0.82f;
    next.drive = 0.48f + random01() * 0.52f;
    next.shred = 0.32f + random01() * 0.68f;
    next.resonance = 0.04f + random01() * 0.44f;
    transitionPatch(p, next, kCustomPreset, 0.90f, true);
}

void mutatePatch(Plugin& p, uint32_t salt)
{
    auto random01 = [&salt]() {
        salt = hash32(salt + 0x85ebca6bu);
        return static_cast<float>(salt & 0xffffu) / 65535.0f;
    };
    auto signedRandom = [&random01]() { return random01() * 2.0f - 1.0f; };
    s3g::PsdRawFieldParams next = p.params;
    next.scanRate = std::clamp(next.scanRate + signedRandom() * 0.07f, 0.0f, 1.0f);
    next.texture = std::clamp(next.texture + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.geometry = std::clamp(next.geometry + signedRandom() * 0.09f, 0.0f, 1.0f);
    next.chaos = std::clamp(next.chaos + signedRandom() * 0.11f, 0.0f, 1.0f);
    next.fold = std::clamp(next.fold + signedRandom() * 0.09f, 0.0f, 1.0f);
    next.evolve = std::clamp(next.evolve + signedRandom() * 0.06f, 0.0f, 1.0f);
    next.channelSpread = std::clamp(next.channelSpread + signedRandom() * 0.07f, 0.0f, 1.0f);
    next.codecRate = std::clamp(next.codecRate + signedRandom() * 0.09f, 0.0f, 1.0f);
    next.codecDamage = std::clamp(next.codecDamage + signedRandom() * 0.10f, 0.0f, 1.0f);
    next.drive = std::clamp(next.drive + signedRandom() * 0.07f, 0.0f, 1.0f);
    next.shred = std::clamp(next.shred + signedRandom() * 0.08f, 0.0f, 1.0f);
    next.resonance = std::clamp(next.resonance + signedRandom() * 0.06f, 0.0f, 1.0f);
    if (random01() < 0.28f) next.bitDepth = std::clamp(next.bitDepth + (random01() < 0.5f ? -1.0f : 1.0f), 2.0f, 16.0f);
    if (random01() < 0.12f) {
        const int mode = std::clamp(
            static_cast<int>(next.codecMode) + (random01() < 0.5f ? -1 : 1),
            0,
            static_cast<int>(kCodecModeMax));
        next.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(mode);
    }
    if (random01() < 0.10f) {
        next.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(static_cast<uint32_t>(random01() * 4.999f));
    }
    transitionPatch(p, next, kCustomPreset, 0.70f, true);
}

void undoPatch(Plugin& p)
{
    if (!p.hasUndo) return;
    const auto restore = p.undoParams;
    const auto restoreSource = p.undoSource;
    const std::string restoreSourcePath = p.undoSourcePath;
    const std::string restoreSourceName = p.undoSourceName;
    const std::string restoreSourceError = p.undoSourceError;
    const SourceInterpretation restoreSourceInterpretation = p.undoSourceInterpretation;
    const uint32_t restorePreset = p.undoPreset;
    p.hasUndo = false;
    p.params = restore;
    p.rawSource = restoreSource;
    p.sourcePath = restoreSourcePath;
    p.sourceName = restoreSourceName;
    p.sourceError = restoreSourceError;
    p.sourceInterpretation = restoreSourceInterpretation;
    p.selectedPreset = restorePreset;
    p.field.transitionToSource(p.rawSource, p.params, 0.70f);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kRunParamId) {
        p.playing.store(value >= 0.5, std::memory_order_relaxed);
        return;
    }
    if (id == kPerformanceModeParamId) {
        const auto next = static_cast<PerformanceMode>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, 1.0)));
        if (next != p.performanceMode) {
            p.performanceMode = next;
            resetMidiPerformance(p);
        }
        return;
    }
    if (id == kAttackParamId) {
        p.attackMs = static_cast<float>(std::clamp(value, 1.0, 5000.0));
        return;
    }
    if (id == kDecayParamId) {
        p.decayMs = static_cast<float>(std::clamp(value, 5.0, 8000.0));
        return;
    }
    if (id == kSustainParamId) {
        p.sustain = static_cast<float>(std::clamp(value, 0.0, 1.0));
        return;
    }
    if (id == kReleaseParamId) {
        p.releaseMs = static_cast<float>(std::clamp(value, 5.0, 12000.0));
        return;
    }
    const s3g::PsdRawFieldParams before = p.params;
    bool changed = true;
    bool structural = false;
    switch (id) {
    case kScanRateParamId: p.params.scanRate = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kTextureParamId: p.params.texture = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGeometryParamId: p.params.geometry = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kChaosParamId: p.params.chaos = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFoldParamId: p.params.fold = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kEvolveParamId: p.params.evolve = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kChannelSchemeParamId:
        p.params.channelScheme = static_cast<s3g::PsdRawFieldChannelScheme>(static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 4.0)));
        structural = true;
        break;
    case kChannelSpreadParamId: p.params.channelSpread = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kCodecModeParamId:
        p.params.codecMode = static_cast<s3g::PsdRawFieldCodecMode>(static_cast<uint32_t>(
            std::clamp(std::round(value), 0.0, static_cast<double>(kCodecModeMax))));
        structural = true;
        break;
    case kCodecRateParamId: p.params.codecRate = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBitDepthParamId:
        p.params.bitDepth = static_cast<float>(std::clamp(std::round(value), 2.0, 16.0));
        structural = true;
        break;
    case kCodecDamageParamId: p.params.codecDamage = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDriveParamId: p.params.drive = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kShredParamId: p.params.shred = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kResonanceParamId: p.params.resonance = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: p.params.gainDb = static_cast<float>(std::clamp(value, -60.0, 6.0)); break;
    case kPresetParamId:
        applyPreset(p, static_cast<uint32_t>(std::clamp(std::round(value), 0.0, static_cast<double>(kCustomPreset))));
        return;
    case kRandomizePatchParamId:
        if (value > 0.0) randomizePatch(p, hash32(p.params.seed ^ static_cast<uint32_t>(std::round(value * 4294967295.0))));
        return;
    case kMutateParamId:
        if (value > 0.0) mutatePatch(p, hash32(p.params.seed ^ static_cast<uint32_t>(std::round(value * 4294967295.0)) ^ 0x27d4eb2du));
        return;
    case kUndoParamId:
        if (value > 0.0) undoPatch(p);
        return;
    case kSeedParamId: {
        const uint32_t nextSeed = static_cast<uint32_t>(std::clamp(value, 1.0, 4294967295.0));
        if (nextSeed == p.params.seed) return;
        saveUndo(p);
        p.params.seed = nextSeed;
        p.selectedPreset = kCustomPreset;
        p.field.transitionTo(p.params, 1.15f);
        return;
    }
    case kRandomizeFieldParamId:
        if (value <= 0.0) return;
        useGeneratedField(p, static_cast<uint32_t>(std::round(value * 4294967295.0)), true);
        return;
    default:
        changed = false;
        break;
    }
    if (changed && paramsDiffer(before, p.params)) {
        p.selectedPreset = kCustomPreset;
        if (structural) p.field.transitionTo(p.params, 0.45f);
        else p.field.setParams(p.params);
    }
}

bool init(const clap_plugin_t*) { return true; }
#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif
void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->output32.assign(kOutputChannels, std::vector<float>(p->maxFrames, 0.0f));
    p->outputPtrs.assign(kOutputChannels, nullptr);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) p->outputPtrs[ch] = p->output32[ch].data();
    p->field.setParams(p->params);
    p->field.setSource(p->rawSource);
    p->field.prepare(sampleRate);
    resetMidiPerformance(*p);
    p->runGain = p->playing.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    resetMidiPerformance(*p);
    p->field.reset();
    p->runGain = p->playing.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* event = in->get(in, i);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(p, param->param_id, param->value);
        }
    }
}

void handleMidiMessage(Plugin& p, const clap_event_midi_t& midi)
{
    const uint8_t status = midi.data[0] & 0xf0u;
    const int32_t key = static_cast<int32_t>(midi.data[1] & 0x7fu);
    const uint8_t value = midi.data[2] & 0x7fu;
    if (status == 0x90u && value > 0u) {
        midiNoteOn(p, key, static_cast<float>(value) * (1.0f / 127.0f));
    } else if (status == 0x80u || (status == 0x90u && value == 0u)) {
        midiNoteOff(p, key, false);
    } else if (status == 0xb0u && (midi.data[1] == 120u || midi.data[1] == 123u)) {
        midiAllNotesOff(p, midi.data[1] == 120u);
    }
}

void handleCoreEvent(Plugin& p, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    switch (event->type) {
    case CLAP_EVENT_PARAM_VALUE: {
        const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(p, param->param_id, param->value);
        break;
    }
    case CLAP_EVENT_NOTE_ON: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (note->velocity > 0.0) midiNoteOn(p, note->key, static_cast<float>(note->velocity));
        else midiNoteOff(p, note->key, false);
        break;
    }
    case CLAP_EVENT_NOTE_OFF: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        midiNoteOff(p, note->key, false);
        break;
    }
    case CLAP_EVENT_NOTE_CHOKE:
    case CLAP_EVENT_NOTE_END: {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        midiNoteOff(p, note->key, true);
        break;
    }
    case CLAP_EVENT_MIDI:
        handleMidiMessage(p, *reinterpret_cast<const clap_event_midi_t*>(event));
        break;
    default:
        break;
    }
}

void renderSegment(Plugin& p, uint32_t offset, uint32_t frames)
{
    if (frames == 0u) return;
    for (uint32_t ch = 0u; ch < kOutputChannels; ++ch) {
        p.outputPtrs[ch] = p.output32[ch].data() + offset;
    }

    const bool playing = p.playing.load(std::memory_order_relaxed);
    const bool midiMode = p.performanceMode == PerformanceMode::Midi;
    const bool voiceActive = !midiMode || p.envelopeGate || p.envelopeStage != EnvelopeStage::Idle;
    const bool transportActive = playing || p.runGain > 0.0f;
    if (voiceActive && transportActive) {
        p.field.process(p.outputPtrs.data(), kOutputChannels, frames);
    } else {
        for (uint32_t ch = 0u; ch < kOutputChannels; ++ch) {
            std::fill(p.output32[ch].begin() + offset, p.output32[ch].begin() + offset + frames, 0.0f);
        }
    }

    const float runStep = 1.0f / static_cast<float>(std::max(1.0, p.sampleRate * 0.020));
    const float attack = envelopeCoefficient(p.attackMs, p.sampleRate);
    const float decay = envelopeCoefficient(p.decayMs, p.sampleRate);
    const float release = envelopeCoefficient(p.releaseMs, p.sampleRate);
    const float velocityGain = 0.15f + 0.85f * std::sqrt(std::clamp(p.activeVelocity, 0.0f, 1.0f));
    for (uint32_t i = 0u; i < frames; ++i) {
        float runGate = 0.0f;
        if (playing) {
            p.runGain = std::min(1.0f, p.runGain + runStep);
            runGate = p.runGain;
        } else if (p.runGain > 0.0f) {
            runGate = p.runGain;
            p.runGain = std::max(0.0f, p.runGain - runStep);
        }
        const float envelope = midiMode ? processMidiEnvelope(p, attack, decay, release) : 1.0f;
        const float gain = runGate * (midiMode ? envelope * velocityGain : 1.0f);
        for (uint32_t ch = 0u; ch < kOutputChannels; ++ch) p.output32[ch][offset + i] *= gain;
    }
    p.displayEnvelope.store(midiMode ? p.envelopeValue : 1.0f, std::memory_order_relaxed);
    p.displayEnvelopeStage.store(static_cast<uint32_t>(
        midiMode ? p.envelopeStage : EnvelopeStage::Idle), std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    if (!proc) return CLAP_PROCESS_ERROR;
    if (proc->audio_outputs_count == 0u) {
        if (proc->in_events) {
            const uint32_t count = proc->in_events->size(proc->in_events);
            for (uint32_t i = 0u; i < count; ++i) handleCoreEvent(*p, proc->in_events->get(proc->in_events, i));
        }
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = std::min(proc->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kOutputChannels) return CLAP_PROCESS_CONTINUE;

    uint32_t rendered = 0u;
    if (proc->in_events) {
        const uint32_t count = proc->in_events->size(proc->in_events);
        for (uint32_t i = 0u; i < count; ++i) {
            const clap_event_header_t* event = proc->in_events->get(proc->in_events, i);
            if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            const uint32_t eventFrame = std::max(rendered, std::min(event->time, frames));
            renderSegment(*p, rendered, eventFrame - rendered);
            handleCoreEvent(*p, event);
            rendered = eventFrame;
        }
    }
    renderSegment(*p, rendered, frames - rendered);

    float blockPeak = 0.0f;
    uint32_t waveWrite = p->waveWrite.load(std::memory_order_relaxed);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float value = p->output32[ch][i];
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = value;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(value);
            blockPeak = std::max(blockPeak, std::abs(value));
        }
    }
    const uint32_t historyStep = std::max<uint32_t>(1u, frames / 64u);
    for (uint32_t i = 0; i < frames; i += historyStep) {
        const uint32_t index = waveWrite++ & (kWaveHistory - 1u);
        for (uint32_t ch = 0; ch < kOutputChannels; ++ch) p->waveHistory[ch][index] = p->output32[ch][i];
    }
    p->waveWrite.store(waveWrite, std::memory_order_relaxed);
    for (uint32_t ch = kOutputChannels; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.9f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20;
    std::snprintf(info->name, sizeof(info->name), "8ch Fault");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }
bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI In", sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    return true;
}
const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kScanRateParamId, "Scan Rate", 0.0, 1.0, 0.44 },
    { kTextureParamId, "Data Texture", 0.0, 1.0, 0.62 },
    { kGeometryParamId, "Geometry", 0.0, 1.0, 0.64 },
    { kChaosParamId, "Chaos", 0.0, 1.0, 0.58 },
    { kFoldParamId, "Fold", 0.0, 1.0, 0.68 },
    { kEvolveParamId, "Field Evolve", 0.0, 1.0, 0.0 },
    { kChannelSchemeParamId, "Channel Routing", 0.0, 4.0, 1.0 },
    { kChannelSpreadParamId, "Channel Spread", 0.0, 1.0, 0.72 },
    { kCodecModeParamId, "Codec", 0.0, static_cast<double>(kCodecModeMax), 3.0 },
    { kCodecRateParamId, "Codec Rate", 0.0, 1.0, 0.35 },
    { kBitDepthParamId, "Bit Depth", 2.0, 16.0, 8.0 },
    { kCodecDamageParamId, "Codec Damage", 0.0, 1.0, 0.28 },
    { kDriveParamId, "Drive", 0.0, 1.0, 0.68 },
    { kShredParamId, "Shred", 0.0, 1.0, 0.58 },
    { kResonanceParamId, "Resonance", 0.0, 1.0, 0.18 },
    { kGainParamId, "Output Gain", -60.0, 6.0, -12.0 },
    { kRunParamId, "Run", 0.0, 1.0, 1.0 },
    { kPresetParamId, "Preset", 0.0, 13.0, 0.0 },
    { kRandomizePatchParamId, "Randomize Patch", 0.0, 1.0, 0.0 },
    { kMutateParamId, "Mutate Patch", 0.0, 1.0, 0.0 },
    { kUndoParamId, "Undo Patch", 0.0, 1.0, 0.0 },
    { kSeedParamId, "Field Seed", 1.0, 4294967295.0, 1346589745.0 },
    { kRandomizeFieldParamId, "Randomize Field", 0.0, 1.0, 0.0 },
    { kPerformanceModeParamId, "Performance Mode", 0.0, 1.0, 0.0 },
    { kAttackParamId, "Attack", 1.0, 5000.0, 12.0 },
    { kDecayParamId, "Decay", 5.0, 8000.0, 280.0 },
    { kSustainParamId, "Sustain", 0.0, 1.0, 0.72 },
    { kReleaseParamId, "Release", 5.0, 12000.0, 850.0 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParamDefs)); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.id == kCodecModeParamId || def.id == kChannelSchemeParamId || def.id == kBitDepthParamId
        || def.id == kPresetParamId || def.id == kRandomizeFieldParamId || def.id == kRandomizePatchParamId
        || def.id == kMutateParamId || def.id == kUndoParamId || def.id == kSeedParamId
        || def.id == kRunParamId || def.id == kPerformanceModeParamId) {
        info->flags |= CLAP_PARAM_IS_STEPPED;
    }
    std::strncpy(info->name, def.name, sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    const bool performance = def.id >= kPerformanceModeParamId && def.id <= kReleaseParamId;
    std::strncpy(info->module, performance ? "Performance" : "Fault", sizeof(info->module));
    info->module[sizeof(info->module) - 1u] = '\0';
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* instance = self(plugin);
    const auto& p = instance->params;
    switch (id) {
    case kScanRateParamId: *value = p.scanRate; return true;
    case kTextureParamId: *value = p.texture; return true;
    case kGeometryParamId: *value = p.geometry; return true;
    case kChaosParamId: *value = p.chaos; return true;
    case kFoldParamId: *value = p.fold; return true;
    case kEvolveParamId: *value = p.evolve; return true;
    case kChannelSchemeParamId: *value = static_cast<uint32_t>(p.channelScheme); return true;
    case kChannelSpreadParamId: *value = p.channelSpread; return true;
    case kCodecModeParamId: *value = static_cast<uint32_t>(p.codecMode); return true;
    case kCodecRateParamId: *value = p.codecRate; return true;
    case kBitDepthParamId: *value = p.bitDepth; return true;
    case kCodecDamageParamId: *value = p.codecDamage; return true;
    case kDriveParamId: *value = p.drive; return true;
    case kShredParamId: *value = p.shred; return true;
    case kResonanceParamId: *value = p.resonance; return true;
    case kGainParamId: *value = p.gainDb; return true;
    case kRunParamId: *value = instance->playing.load(std::memory_order_relaxed) ? 1.0 : 0.0; return true;
    case kPerformanceModeParamId: *value = static_cast<uint32_t>(instance->performanceMode); return true;
    case kAttackParamId: *value = instance->attackMs; return true;
    case kDecayParamId: *value = instance->decayMs; return true;
    case kSustainParamId: *value = instance->sustain; return true;
    case kReleaseParamId: *value = instance->releaseMs; return true;
    case kPresetParamId: *value = instance->selectedPreset; return true;
    case kRandomizePatchParamId:
    case kMutateParamId:
    case kUndoParamId:
    case kRandomizeFieldParamId: *value = 0.0; return true;
    case kSeedParamId: *value = p.seed; return true;
    default: return false;
    }
}

const char* codecModeName(uint32_t mode)
{
    switch (mode) {
    case 1u: return "DELTA";
    case 2u: return "ADPCM";
    case 3u: return "MU-LAW";
    case 4u: return "A-LAW";
    case 5u: return "CELP";
    case 6u: return "DISC";
    case 7u: return "CVSD";
    case 8u: return "SUBBAND";
    case 9u: return "LPC";
    case 10u: return "TRANSFORM";
    case 11u: return "PREDICT";
    case 12u: return "MODEM";
    case 13u: return "FAX";
    case 14u: return "SIGMA 1-BIT";
    case 15u: return "HYBRID";
    case 0u:
    default: return "PCM";
    }
}

const char* performanceModeName(uint32_t mode) { return mode == 1u ? "MIDI" : "FREE"; }

const char* channelSchemeName(uint32_t mode)
{
    switch (mode) {
    case 1u: return "DEINTERLEAVE";
    case 2u: return "PLANES";
    case 3u: return "SHUFFLED";
    case 4u: return "DIVERGENT";
    case 0u:
    default: return "PARALLEL";
    }
}

const char* presetName(uint32_t preset)
{
    switch (preset) {
    case 1u: return "SLOW FOLD";
    case 2u: return "CODEC SCAR";
    case 3u: return "DIVERGE";
    case 4u: return "BREAKS";
    case 5u: return "GLASS GRID";
    case 6u: return "MU DUST";
    case 7u: return "DELTA STAIRS";
    case 8u: return "ADPCM TEAR";
    case 9u: return "CELP CHOIR";
    case 10u: return "LOW EMBERS";
    case 11u: return "WIDE STATIC";
    case kWaveTracePreset: return "WAVE TRACE";
    case kCustomPreset: return "CUSTOM";
    case 0u:
    default: return "INIT";
    }
}

double scanBytesPerSecond(double normalized, double sampleRate)
{
    return 0.00002 * std::pow(375000.0, std::clamp(normalized, 0.0, 1.0)) * sampleRate;
}

double codecUpdatesPerSecond(double normalized, double sampleRate)
{
    return sampleRate / std::pow(2.0, std::clamp(normalized, 0.0, 1.0) * 14.0);
}

double evolveEventsPerSecond(double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    return normalized <= 0.0001 ? 0.0 : 1.0 / (12.0 + (0.35 - 12.0) * normalized * normalized);
}

std::string rateText(double value, const char* unit)
{
    char text[32] {};
    if (value >= 1000.0) std::snprintf(text, sizeof(text), "%.1f k%s", value / 1000.0, unit);
    else if (value >= 100.0) std::snprintf(text, sizeof(text), "%.0f %s", value, unit);
    else if (value >= 10.0) std::snprintf(text, sizeof(text), "%.1f %s", value, unit);
    else std::snprintf(text, sizeof(text), "%.2f %s", value, unit);
    return text;
}

std::string waveformSpeedText(double normalized)
{
    const double speed = std::pow(2.0, (std::clamp(normalized, 0.0, 1.0) - 0.5) * 16.0);
    char text[32] {};
    if (speed < 0.1) std::snprintf(text, sizeof(text), "%.3fx", speed);
    else if (speed < 10.0) std::snprintf(text, sizeof(text), "%.2fx", speed);
    else std::snprintf(text, sizeof(text), "%.1fx", speed);
    return text;
}

std::string evolveText(double normalized)
{
    const double rate = evolveEventsPerSecond(normalized);
    if (rate <= 0.0) return "OFF";
    char text[32] {};
    if (rate < 1.0) std::snprintf(text, sizeof(text), "%.4f Hz", rate);
    else std::snprintf(text, sizeof(text), "%.2f Hz", rate);
    return text;
}

std::string envelopeTimeText(double milliseconds)
{
    char text[32] {};
    if (milliseconds >= 1000.0) std::snprintf(text, sizeof(text), "%.4f s", milliseconds * 0.001);
    else std::snprintf(text, sizeof(text), "%.3f ms", milliseconds);
    return text;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    const double sampleRate = plugin ? self(plugin)->sampleRate : 48000.0;
    if (id == kScanRateParamId) {
        const auto* instance = plugin ? self(plugin) : nullptr;
        const bool waveform = instance && instance->rawSource && instance->rawSource->waveform;
        std::snprintf(display, size, "%s", waveform
            ? waveformSpeedText(value).c_str()
            : rateText(scanBytesPerSecond(value, sampleRate), "B/s").c_str());
    }
    else if (id == kCodecRateParamId) std::snprintf(display, size, "%s", rateText(codecUpdatesPerSecond(value, sampleRate), "Hz").c_str());
    else if (id == kEvolveParamId) std::snprintf(display, size, "%s", evolveText(value).c_str());
    else if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kRunParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "PLAY" : "STOP");
    else if (id == kPerformanceModeParamId) std::snprintf(display, size, "%s", performanceModeName(
        static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 1.0))));
    else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) {
        std::snprintf(display, size, "%s", envelopeTimeText(value).c_str());
    }
    else if (id == kSustainParamId) std::snprintf(display, size, "%.1f%%", value * 100.0);
    else if (id == kSeedParamId) std::snprintf(display, size, "%08X", static_cast<uint32_t>(value));
    else if (id == kRandomizeFieldParamId || id == kRandomizePatchParamId || id == kMutateParamId || id == kUndoParamId) std::snprintf(display, size, "TRIG");
    else if (id == kBitDepthParamId) std::snprintf(display, size, "%.0f bit", value);
    else if (id == kCodecModeParamId) std::snprintf(display, size, "%s", codecModeName(static_cast<uint32_t>(
        std::clamp(std::round(value), 0.0, static_cast<double>(kCodecModeMax)))));
    else if (id == kChannelSchemeParamId) std::snprintf(display, size, "%s", channelSchemeName(static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 4.0))));
    else if (id == kPresetParamId) std::snprintf(display, size, "%s", presetName(static_cast<uint32_t>(std::clamp(std::round(value), 0.0, static_cast<double>(kCustomPreset)))));
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    const double sampleRate = plugin ? self(plugin)->sampleRate : 48000.0;
    const double numeric = std::strtod(display, nullptr);
    if (id == kRunParamId) {
        if (std::strcmp(display, "PLAY") == 0 || std::strcmp(display, "RUN") == 0
            || std::strcmp(display, "ON") == 0) *value = 1.0;
        else if (std::strcmp(display, "STOP") == 0 || std::strcmp(display, "OFF") == 0) *value = 0.0;
        else *value = numeric >= 0.5 ? 1.0 : 0.0;
    } else if (id == kPerformanceModeParamId) {
        if (std::strcmp(display, "MIDI") == 0) *value = 1.0;
        else if (std::strcmp(display, "FREE") == 0) *value = 0.0;
        else *value = numeric >= 0.5 ? 1.0 : 0.0;
    } else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) {
        *value = numeric;
        if (!std::strstr(display, "ms") && std::strchr(display, 's')) *value *= 1000.0;
    } else if (id == kSustainParamId) {
        *value = std::strchr(display, '%') ? numeric / 100.0 : numeric;
    } else if (id == kScanRateParamId) {
        double rate = numeric;
        if (std::strchr(display, 'k')) rate *= 1000.0;
        *value = std::clamp(std::log(std::max(rate, 1.0e-12) / (0.00002 * sampleRate)) / std::log(375000.0), 0.0, 1.0);
    } else if (id == kCodecRateParamId) {
        double rate = numeric;
        if (std::strchr(display, 'k')) rate *= 1000.0;
        *value = std::clamp(std::log2(sampleRate / std::max(rate, 1.0e-12)) / 14.0, 0.0, 1.0);
    } else if (id == kEvolveParamId) {
        if (std::strcmp(display, "OFF") == 0) *value = 0.0;
        else {
            double rate = numeric;
            if (std::strchr(display, 'k')) rate *= 1000.0;
            const double interval = 1.0 / std::max(rate, 1.0e-12);
            const double squared = (12.0 - interval) / 11.65;
            *value = squared <= 0.0 ? 0.0002 : std::sqrt(std::clamp(squared, 0.0, 1.0));
        }
    } else if (id == kCodecModeParamId) {
        for (uint32_t mode = 0u; mode < kCodecModeCount; ++mode) {
            if (std::strcmp(display, codecModeName(mode)) == 0) { *value = mode; return true; }
        }
        return false;
    } else if (id == kChannelSchemeParamId) {
        for (uint32_t mode = 0u; mode <= 4u; ++mode) {
            if (std::strcmp(display, channelSchemeName(mode)) == 0) { *value = mode; return true; }
        }
        return false;
    } else if (id == kPresetParamId) {
        for (uint32_t preset = 0u; preset <= kCustomPreset; ++preset) {
            if (std::strcmp(display, presetName(preset)) == 0) { *value = preset; return true; }
        }
        return false;
    } else if (id == kSeedParamId) {
        *value = static_cast<double>(std::strtoull(display, nullptr, 16));
    } else if (id == kGainParamId || id == kBitDepthParamId) {
        *value = numeric;
    } else if (id == kRandomizeFieldParamId || id == kRandomizePatchParamId || id == kMutateParamId || id == kUndoParamId) {
        *value = 0.0;
    } else {
        *value = numeric / 100.0;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool writeFully(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t written = stream->write(stream, bytes + offset, size - offset);
        if (written <= 0) return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool readFully(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t read = stream->read(stream, bytes + offset, size - offset);
        if (read <= 0) return false;
        offset += static_cast<size_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state {};
    const auto* p = self(plugin);
    state.params = p->params;
    state.selectedPreset = p->selectedPreset;
    state.runState = p->playing.load(std::memory_order_relaxed) ? 1u : 0u;
    state.performanceMode = static_cast<uint32_t>(p->performanceMode);
    state.attackMs = p->attackMs;
    state.decayMs = p->decayMs;
    state.sustain = p->sustain;
    state.releaseMs = p->releaseMs;
    if (!p->sourcePath.empty()) {
        state.sourceMode = static_cast<uint32_t>(p->sourceInterpretation);
        std::snprintf(state.sourcePath, sizeof(state.sourcePath), "%s", p->sourcePath.c_str());
    }
    return writeFully(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto* p = self(plugin);
    uint32_t version = 0u;
    if (!readFully(stream, &version, sizeof(version))) return false;
    bool restoredPlaying = true;
    p->performanceMode = PerformanceMode::Free;
    p->attackMs = 12.0f;
    p->decayMs = 280.0f;
    p->sustain = 0.72f;
    p->releaseMs = 850.0f;
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = state.params;
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->performanceMode = static_cast<PerformanceMode>(std::min(state.performanceMode, 1u));
        p->attackMs = std::clamp(state.attackMs, 1.0f, 5000.0f);
        p->decayMs = std::clamp(state.decayMs, 5.0f, 8000.0f);
        p->sustain = std::clamp(state.sustain, 0.0f, 1.0f);
        p->releaseMs = std::clamp(state.releaseMs, 5.0f, 12000.0f);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 14u) {
        LegacySavedStateV14 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = state.params;
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 13u) {
        LegacySavedStateV13 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        restoredPlaying = state.runState != 0u;
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 12u) {
        LegacySavedStateV12 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = std::min(state.selectedPreset, kCustomPreset);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if ((state.sourceMode == static_cast<uint32_t>(SourceInterpretation::RawBytes)
                || state.sourceMode == static_cast<uint32_t>(SourceInterpretation::Waveform))
            && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = static_cast<SourceInterpretation>(state.sourceMode);
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 11u) {
        LegacySavedStateV11 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = state.selectedPreset == 12u
            ? kCustomPreset
            : std::min(state.selectedPreset, 11u);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
        if (state.sourceMode == 1u && state.sourcePath[0] != '\0') {
            std::size_t pathLength = 0u;
            while (pathLength < sizeof(state.sourcePath) && state.sourcePath[pathLength] != '\0') ++pathLength;
            p->sourcePath.assign(state.sourcePath, pathLength);
            p->sourceName = sourceNameFromPath(p->sourcePath);
            p->sourceInterpretation = SourceInterpretation::RawBytes;
            readSource(p->sourcePath, p->sourceInterpretation, p->rawSource, p->sourceError);
        }
    } else if (version == 10u) {
        LegacySavedStateV10 state {};
        state.version = version;
        constexpr size_t offset = sizeof(state.version);
        if (!readFully(stream, reinterpret_cast<uint8_t*>(&state) + offset, sizeof(state) - offset)) return false;
        p->params = migrateLegacyParams(state.params);
        p->selectedPreset = state.selectedPreset == 12u
            ? kCustomPreset
            : std::min(state.selectedPreset, 11u);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
    } else if (version == 9u) {
        uint32_t selectedPreset = 0u;
        LegacyParamsV9 legacy {};
        if (!readFully(stream, &selectedPreset, sizeof(selectedPreset))) return false;
        if (!readFully(stream, &legacy, sizeof(legacy))) return false;
        p->params = migrateLegacyParams(legacy);
        p->selectedPreset = selectedPreset == 5u ? kCustomPreset : std::min(selectedPreset, 4u);
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
    } else if (version == 8u) {
        LegacyParamsV8 legacy {};
        if (!readFully(stream, &legacy, sizeof(legacy))) return false;
        p->params = migrateLegacyParams(legacy);
        p->selectedPreset = kCustomPreset;
        p->rawSource.reset();
        p->sourcePath.clear();
        p->sourceName.clear();
        p->sourceError.clear();
        p->sourceInterpretation = SourceInterpretation::Generated;
    } else {
        return false;
    }
    p->hasUndo = false;
    p->undoSource.reset();
    p->undoSourceInterpretation = SourceInterpretation::Generated;
    p->playing.store(restoredPlaying, std::memory_order_relaxed);
    p->runGain = restoredPlaying ? 1.0f : 0.0f;
    resetMidiPerformance(*p);
    p->field.setSource(p->rawSource);
    p->field.setParams(p->params);
    p->field.reset();
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

double normalizedParam(const s3g::PsdRawFieldParams& p, clap_id id)
{
    switch (id) {
    case kScanRateParamId: return p.scanRate;
    case kTextureParamId: return p.texture;
    case kGeometryParamId: return p.geometry;
    case kChaosParamId: return p.chaos;
    case kFoldParamId: return p.fold;
    case kEvolveParamId: return p.evolve;
    case kChannelSchemeParamId: return static_cast<double>(static_cast<uint32_t>(p.channelScheme)) / 4.0;
    case kChannelSpreadParamId: return p.channelSpread;
    case kCodecModeParamId:
        return static_cast<double>(static_cast<uint32_t>(p.codecMode)) / static_cast<double>(kCodecModeMax);
    case kCodecRateParamId: return p.codecRate;
    case kBitDepthParamId: return (p.bitDepth - 2.0f) / 14.0f;
    case kCodecDamageParamId: return p.codecDamage;
    case kDriveParamId: return p.drive;
    case kShredParamId: return p.shred;
    case kResonanceParamId: return p.resonance;
    case kGainParamId: return (p.gainDb + 60.0f) / 66.0f;
    case kSeedParamId: return static_cast<double>((p.seed >> 8u) & 0xffffu) / 65535.0;
    default: return 0.5;
    }
}

double normalizedEnvelopeTime(double value, double minimum, double maximum)
{
    value = std::clamp(value, minimum, maximum);
    return std::log(value / minimum) / std::log(maximum / minimum);
}

double normalizedPerformanceParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kAttackParamId: return normalizedEnvelopeTime(p.attackMs, 1.0, 5000.0);
    case kDecayParamId: return normalizedEnvelopeTime(p.decayMs, 5.0, 8000.0);
    case kSustainParamId: return p.sustain;
    case kReleaseParamId: return normalizedEnvelopeTime(p.releaseMs, 5.0, 12000.0);
    default: return 0.0;
    }
}

double denormalizedEnvelopeTime(double normalized, double minimum, double maximum)
{
    return minimum * std::pow(maximum / minimum, std::clamp(normalized, 0.0, 1.0));
}

void applyNormalizedParam(Plugin& p, clap_id id, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    switch (id) {
    case kBitDepthParamId: applyParam(p, id, std::round(2.0 + normalized * 14.0)); break;
    case kCodecModeParamId: applyParam(p, id, std::round(normalized * static_cast<double>(kCodecModeMax))); break;
    case kChannelSchemeParamId: applyParam(p, id, std::round(normalized * 4.0)); break;
    case kGainParamId: applyParam(p, id, -60.0 + normalized * 66.0); break;
    case kSeedParamId: applyParam(p, id, 1.0 + normalized * 4294967294.0); break;
    case kAttackParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 1.0, 5000.0)); break;
    case kDecayParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 5.0, 8000.0)); break;
    case kSustainParamId: applyParam(p, id, normalized); break;
    case kReleaseParamId: applyParam(p, id, denormalizedEnvelopeTime(normalized, 5.0, 12000.0)); break;
    default: applyParam(p, id, normalized); break;
    }
}

std::string byteCountText(uint64_t bytes)
{
    char text[32] {};
    if (bytes >= 1024u * 1024u) {
        std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024u) {
        std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    }
    return text;
}

std::string sourceStatusText(const Plugin& p)
{
    if (!p.rawSource) {
        if (p.sourcePath.empty()) {
            return std::string("SOURCE GENERATED [")
                + codecModeName(static_cast<uint32_t>(p.params.fieldCodecMode)) + "] | 64.0 KB";
        }
        return (p.sourceError.empty() ? std::string("SOURCE MISSING") : p.sourceError)
            + " | " + p.sourceName;
    }
    if (p.rawSource->waveform) {
        char format[80] {};
        std::snprintf(format, sizeof(format), "WAVE %u>8 | %.1fK %u-BIT | ",
            p.rawSource->sourceChannelCount,
            static_cast<double>(p.rawSource->sourceSampleRate) / 1000.0,
            p.rawSource->sourceBitsPerSample);
        std::string status = format + p.sourceName;
        if (p.rawSource->truncated) status += " | PARTIAL";
        return status;
    }
    std::string status = "RAW | " + p.sourceName + " | " + byteCountText(p.rawSource->originalByteCount);
    if (p.rawSource->truncated) status += " | FIRST 64 MB";
    return status;
}

std::string compactEnvelopeTimeText(float milliseconds)
{
    char text[24] {};
    if (milliseconds >= 1000.0f) std::snprintf(text, sizeof(text), "%.2fs", milliseconds * 0.001f);
    else if (milliseconds >= 100.0f) std::snprintf(text, sizeof(text), "%.0fms", milliseconds);
    else std::snprintf(text, sizeof(text), "%.1fms", milliseconds);
    return text;
}

std::string midiNoteName(int32_t key)
{
    if (key < 0 || key > 127) return "WAITING";
    constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    char text[16] {};
    std::snprintf(text, sizeof(text), "%s%d", names[key % 12], key / 12 - 1);
    return text;
}

} // namespace

#if defined(__APPLE__)
namespace {

struct EnvelopeGraphGeometry {
    NSRect frame;
    CGFloat top;
    CGFloat bottom;
    NSPoint start;
    NSPoint attack;
    NSPoint decay;
    NSPoint sustain;
    NSPoint release;
};

EnvelopeGraphGeometry envelopeGraphGeometry(const Plugin& plugin)
{
    EnvelopeGraphGeometry graph {};
    graph.frame = NSMakeRect(36.0, 674.0, 808.0, 116.0);
    graph.top = graph.frame.origin.y + 14.0;
    graph.bottom = NSMaxY(graph.frame) - 14.0;
    const CGFloat sustainY = graph.bottom
        - static_cast<CGFloat>(std::clamp(plugin.sustain, 0.0f, 1.0f)) * (graph.bottom - graph.top);
    graph.start = NSMakePoint(50.0, graph.bottom);
    graph.attack = NSMakePoint(
        100.0 + static_cast<CGFloat>(normalizedPerformanceParam(plugin, kAttackParamId)) * 150.0,
        graph.top);
    graph.decay = NSMakePoint(
        300.0 + static_cast<CGFloat>(normalizedPerformanceParam(plugin, kDecayParamId)) * 165.0,
        sustainY);
    graph.sustain = NSMakePoint(620.0, sustainY);
    graph.release = NSMakePoint(
        670.0 + static_cast<CGFloat>(normalizedPerformanceParam(plugin, kReleaseParamId)) * 160.0,
        graph.bottom);
    return graph;
}

CGFloat squaredDistance(NSPoint a, NSPoint b)
{
    const CGFloat x = a.x - b.x;
    const CGFloat y = a.y - b.y;
    return x * x + y * y;
}

} // namespace

@interface S3GPsdRawFieldView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSTimer* _timer;
    NSTrackingArea* _trackingArea;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawMenuControl:(NSString*)name value:(NSString*)value x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (void)drawPerformanceMode:(Plugin*)plugin attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (void)drawEnvelopeEditor:(Plugin*)plugin attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawTransportButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs active:(BOOL)active;
- (void)drawWaveforms:(Plugin*)plugin style:(s3g::clap_gui::Style&)style;
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)chooseRawFile;
- (void)updateMenuHover:(NSPoint)point;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GPsdRawFieldView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _timer = nil;
        _trackingArea = nil;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc
{
    [self stopRefreshTimer];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
    }
    [super dealloc];
}
- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        [_trackingArea release];
    }
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
        options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
        owner:self
        userInfo:nil];
    [self addTrackingArea:_trackingArea];
}
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, small, style, x, x + 104.0, x + 286.0, 148.0);
}
- (void)drawMenuControl:(NSString*)name value:(NSString*)value x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, small, style, x, x + 104.0, 148.0);
}
- (void)drawPerformanceMode:(Plugin*)plugin attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    [@"MODE" drawAtPoint:NSMakePoint(36.0, 647.0) withAttributes:attrs];
    const NSRect freeRect = NSMakeRect(140.0, 642.0, 74.0, 22.0);
    const NSRect midiRect = NSMakeRect(214.0, 642.0, 74.0, 22.0);
    const bool midi = plugin->performanceMode == PerformanceMode::Midi;
    for (uint32_t item = 0u; item < 2u; ++item) {
        const NSRect rect = item == 0u ? freeRect : midiRect;
        const bool active = item == (midi ? 1u : 0u);
        NSColor* fill = active
            ? [NSColor colorWithCalibratedRed:0.13 green:0.31 blue:0.24 alpha:1.0]
            : [NSColor colorWithCalibratedWhite:0.12 alpha:1.0];
        [fill setFill];
        NSRectFill(rect);
        [(active ? style.text : style.grid) setStroke];
        NSFrameRect(rect);
        NSString* label = item == 0u ? @"FREE" : @"MIDI";
        const NSSize size = [label sizeWithAttributes:small];
        [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5, NSMidY(rect) - size.height * 0.5)
            withAttributes:small];
    }
    const std::string note = midi ? midiNoteName(plugin->displayNote.load(std::memory_order_relaxed)) : "CONTINUOUS";
    [[NSString stringWithFormat:@"NOTE %s", note.c_str()] drawAtPoint:NSMakePoint(318.0, 647.0) withAttributes:small];
    [[NSString stringWithFormat:@"ENV %.0f%%", plugin->displayEnvelope.load(std::memory_order_relaxed) * 100.0f]
        drawAtPoint:NSMakePoint(454.0, 647.0) withAttributes:small];
    [[NSString stringWithFormat:@"FIELD %08X", plugin->params.seed]
        drawAtPoint:NSMakePoint(706.0, 647.0) withAttributes:small];
}
- (void)drawEnvelopeEditor:(Plugin*)plugin attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*plugin);
    [style.strip setFill];
    NSRectFill(graph.frame);
    [style.grid setStroke];
    NSFrameRect(graph.frame);

    NSBezierPath* grid = [NSBezierPath bezierPath];
    for (uint32_t division = 1u; division < 4u; ++division) {
        const CGFloat y = graph.top
            + (graph.bottom - graph.top) * static_cast<CGFloat>(division) * 0.25;
        [grid moveToPoint:NSMakePoint(NSMinX(graph.frame) + 1.0, y)];
        [grid lineToPoint:NSMakePoint(NSMaxX(graph.frame) - 1.0, y)];
    }
    [grid setLineWidth:0.5];
    [grid stroke];

    NSBezierPath* area = [NSBezierPath bezierPath];
    [area moveToPoint:graph.start];
    [area lineToPoint:graph.attack];
    [area lineToPoint:graph.decay];
    [area lineToPoint:graph.sustain];
    [area lineToPoint:graph.release];
    [area closePath];
    [[style.fill colorWithAlphaComponent:0.18] setFill];
    [area fill];

    NSBezierPath* envelope = [NSBezierPath bezierPath];
    [envelope moveToPoint:graph.start];
    [envelope lineToPoint:graph.attack];
    [envelope lineToPoint:graph.decay];
    [envelope lineToPoint:graph.sustain];
    [envelope lineToPoint:graph.release];
    [style.text setStroke];
    [envelope setLineWidth:1.5];
    [envelope stroke];

    const bool midi = plugin->performanceMode == PerformanceMode::Midi;
    const float level = std::clamp(plugin->displayEnvelope.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (midi && level > 0.0001f) {
        const CGFloat y = graph.bottom - static_cast<CGFloat>(level) * (graph.bottom - graph.top);
        CGFloat dash[] = { 3.0, 4.0 };
        NSBezierPath* levelLine = [NSBezierPath bezierPath];
        [levelLine moveToPoint:NSMakePoint(NSMinX(graph.frame) + 1.0, y)];
        [levelLine lineToPoint:NSMakePoint(NSMaxX(graph.frame) - 1.0, y)];
        [levelLine setLineDash:dash count:2 phase:0.0];
        [[NSColor colorWithCalibratedRed:0.34 green:0.78 blue:0.55 alpha:0.72] setStroke];
        [levelLine setLineWidth:1.0];
        [levelLine stroke];
    }

    const auto displayedStage = static_cast<EnvelopeStage>(
        plugin->displayEnvelopeStage.load(std::memory_order_relaxed));
    auto drawHandle = [&](NSPoint point, int dragId, EnvelopeStage stage) {
        const bool active = _dragSlider == dragId || (midi && displayedStage == stage);
        NSBezierPath* handle = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(point.x - 5.0, point.y - 5.0, 10.0, 10.0)];
        NSColor* fill = active
            ? [NSColor colorWithCalibratedRed:0.19 green:0.55 blue:0.36 alpha:1.0]
            : style.cellBg;
        [fill setFill];
        [handle fill];
        [(active ? style.text : style.dim) setStroke];
        [handle setLineWidth:1.0];
        [handle stroke];
    };
    drawHandle(graph.attack, 201, EnvelopeStage::Attack);
    drawHandle(graph.decay, 202, EnvelopeStage::Decay);
    drawHandle(graph.sustain, 203, EnvelopeStage::Sustain);
    drawHandle(graph.release, 204, EnvelopeStage::Release);

    const std::string attackText = compactEnvelopeTimeText(plugin->attackMs);
    const std::string decayText = compactEnvelopeTimeText(plugin->decayMs);
    const std::string releaseText = compactEnvelopeTimeText(plugin->releaseMs);
    [[NSString stringWithFormat:@"A  %s", attackText.c_str()]
        drawAtPoint:NSMakePoint(50.0, 800.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"D  %s", decayText.c_str()]
        drawAtPoint:NSMakePoint(250.0, 800.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"S  %.0f%%", plugin->sustain * 100.0f]
        drawAtPoint:NSMakePoint(450.0, 800.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"R  %s", releaseText.c_str()]
        drawAtPoint:NSMakePoint(650.0, 800.0) withAttributes:attrs];
}
- (void)drawButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [[NSColor colorWithCalibratedWhite:0.18 alpha:1.0] setFill];
    NSRectFill(rect);
    [[NSColor colorWithCalibratedWhite:0.72 alpha:1.0] setStroke];
    NSFrameRect(rect);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5, NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}
- (void)drawTransportButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs active:(BOOL)active
{
    NSColor* fill = [NSColor colorWithCalibratedWhite:0.18 alpha:1.0];
    if (active) {
        fill = [label isEqualToString:@"STOP"]
            ? [NSColor colorWithCalibratedRed:0.42 green:0.17 blue:0.15 alpha:1.0]
            : [NSColor colorWithCalibratedRed:0.12 green:0.34 blue:0.25 alpha:1.0];
    }
    [fill setFill];
    NSRectFill(rect);
    [[NSColor colorWithCalibratedWhite:(active ? 0.90 : 0.58) alpha:1.0] setStroke];
    NSFrameRect(rect);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5, NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}
- (void)drawWaveforms:(Plugin*)plugin style:(s3g::clap_gui::Style&)style
{
    const CGFloat x = 18.0;
    const CGFloat y = 42.0;
    const CGFloat w = 844.0;
    const CGFloat h = 178.0;
    s3g::clap_gui::drawPanelFrame(x, y, w, h, style);
    NSDictionary* labelAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    [@"8 CHANNEL OUTPUT" drawAtPoint:NSMakePoint(x + 12.0, y + 7.0) withAttributes:labelAttrs];
    NSMutableParagraphStyle* paragraph = [[[NSMutableParagraphStyle alloc] init] autorelease];
    [paragraph setLineBreakMode:NSLineBreakByTruncatingMiddle];
    NSMutableDictionary* sourceAttrs = [NSMutableDictionary dictionaryWithDictionary:small];
    [sourceAttrs setObject:paragraph forKey:NSParagraphStyleAttributeName];
    const std::string sourceStatus = sourceStatusText(*plugin);
    [[NSString stringWithUTF8String:sourceStatus.c_str()] drawInRect:NSMakeRect(x + 148.0, y + 7.0, 330.0, 18.0)
        withAttributes:sourceAttrs];
    const bool playing = plugin->playing.load(std::memory_order_relaxed);
    [self drawTransportButton:@"PLAY" rect:NSMakeRect(506.0, y + 7.0, 46.0, 18.0) attrs:small active:playing];
    [self drawTransportButton:@"STOP" rect:NSMakeRect(560.0, y + 7.0, 48.0, 18.0) attrs:small active:!playing];
    [self drawButton:@"OPEN ANY" rect:NSMakeRect(616.0, y + 7.0, 104.0, 18.0) attrs:small];
    [self drawButton:@"GEN FIELD" rect:NSMakeRect(730.0, y + 7.0, 108.0, 18.0) attrs:small];

    const CGFloat originX = x + 12.0;
    const CGFloat originY = y + 30.0;
    const CGFloat traceW = w - 24.0;
    const CGFloat traceH = (h - 44.0) / static_cast<CGFloat>(kOutputChannels);
    const uint32_t write = plugin->waveWrite.load(std::memory_order_relaxed);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        const CGFloat top = originY + static_cast<CGFloat>(ch) * traceH;
        const CGFloat mid = top + traceH * 0.5;
        [[NSColor colorWithCalibratedWhite:0.13 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(originX, top + 1.0, traceW, traceH - 2.0));
        [[NSColor colorWithCalibratedWhite:0.25 alpha:1.0] setStroke];
        NSBezierPath* center = [NSBezierPath bezierPath];
        [center moveToPoint:NSMakePoint(originX, mid)];
        [center lineToPoint:NSMakePoint(originX + traceW, mid)];
        [center setLineWidth:0.5];
        [center stroke];

        const CGFloat hue = 0.53 + static_cast<CGFloat>(ch) * 0.045;
        [[NSColor colorWithCalibratedHue:hue saturation:0.55 brightness:0.92 alpha:0.96] setStroke];
        NSBezierPath* path = [NSBezierPath bezierPath];
        float peak = 0.0001f;
        for (uint32_t i = 0; i < kWaveHistory; ++i) {
            peak = std::max(peak, std::abs(plugin->waveHistory[ch][(write + i) & (kWaveHistory - 1u)]));
        }
        const float displayGain = std::min(24.0f, 0.92f / peak);
        for (uint32_t i = 0; i < kWaveHistory; ++i) {
            const uint32_t index = (write + i) & (kWaveHistory - 1u);
            const float sample = std::clamp(plugin->waveHistory[ch][index] * displayGain, -1.0f, 1.0f);
            const CGFloat px = originX + static_cast<CGFloat>(i) * traceW / static_cast<CGFloat>(kWaveHistory - 1u);
            const CGFloat py = mid - static_cast<CGFloat>(sample) * traceH * 0.43;
            if (i == 0u) [path moveToPoint:NSMakePoint(px, py)];
            else [path lineToPoint:NSMakePoint(px, py)];
        }
        [path setLineWidth:1.0];
        [path stroke];
        [[NSString stringWithFormat:@"%u", ch + 1u] drawAtPoint:NSMakePoint(originX + 4.0, top + 2.0) withAttributes:small];
    }
}
- (void)chooseRawFile
{
    auto* p = static_cast<Plugin*>(_plugin);
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setResolvesAliases:YES];
    [panel setTitle:@"Open Any Source"];
    [panel setPrompt:@"Open Any"];
    NSView* accessory = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 310, 28)] autorelease];
    NSButton* waveformButton = [[[NSButton alloc] initWithFrame:NSMakeRect(0, 2, 310, 22)] autorelease];
    [waveformButton setButtonType:NSButtonTypeSwitch];
    [waveformButton setTitle:@"Decode WAVE data into eight lanes"];
    [waveformButton setState:NSControlStateValueOn];
    [accessory addSubview:waveformButton];
    [panel setAccessoryView:accessory];
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* pathString = [[[panel URLs] firstObject] path];
    if (!pathString) return;
    const char* filePath = [pathString fileSystemRepresentation];
    if (!filePath) return;

    std::shared_ptr<const s3g::PsdRawFieldSource> source;
    std::string error;
    SourceInterpretation interpretation = [waveformButton state] == NSControlStateValueOn
        ? SourceInterpretation::Waveform
        : SourceInterpretation::RawBytes;
    bool loaded = readSource(filePath, interpretation, source, error);
    if (!loaded && interpretation == SourceInterpretation::Waveform) {
        interpretation = SourceInterpretation::RawBytes;
        loaded = readSource(filePath, interpretation, source, error);
    }
    if (!loaded) {
        NSAlert* alert = [[[NSAlert alloc] init] autorelease];
        [alert setMessageText:@"Could not open raw source"];
        [alert setInformativeText:[NSString stringWithUTF8String:error.c_str()]];
        [alert runModal];
        return;
    }
    installRawSource(*p, std::move(source), filePath, interpretation, true);
    markHostStateDirty(*p);
    [self setNeedsDisplay:YES];
}
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu == 1) {
        NSString* const items[] = {
            @"INIT", @"SLOW FOLD", @"CODEC SCAR", @"DIVERGE", @"BREAKS", @"GLASS GRID",
            @"MU DUST", @"DELTA STAIRS", @"ADPCM TEAR", @"CELP CHOIR", @"LOW EMBERS",
            @"WIDE STATIC", @"WAVE TRACE", @"CUSTOM"
        };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(140, 375, 148, 18.0 * 14.0), 18.0, items, 14,
            static_cast<int>(std::min(p->selectedPreset, kCustomPreset)), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 2) {
        NSString* const items[] = { @"PARALLEL", @"DEINTERLEAVE", @"PLANES", @"SHUFFLED", @"DIVERGENT" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(140, 469, 148, 18.0 * 5.0), 18.0, items, 5,
            static_cast<int>(p->params.channelScheme), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 3) {
        NSString* const items[] = {
            @"PCM", @"DELTA", @"ADPCM", @"MU-LAW", @"A-LAW", @"CELP", @"DISC", @"CVSD",
            @"SUBBAND", @"LPC", @"TRANSFORM", @"PREDICT", @"MODEM", @"FAX", @"SIGMA 1-BIT", @"HYBRID"
        };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(570, 289, 148, 18.0 * kCodecModeCount), 18.0,
            items, kCodecModeCount,
            static_cast<int>(p->params.codecMode), _hoverMenuItem, attrs, style);
    }
}
- (void)updateMenuHover:(NSPoint)point
{
    NSRect rect = NSZeroRect;
    uint32_t count = 0u;
    if (_openMenu == 1) { rect = NSMakeRect(140, 375, 148, 18.0 * 14.0); count = 14u; }
    else if (_openMenu == 2) { rect = NSMakeRect(140, 469, 148, 18.0 * 5.0); count = 5u; }
    else if (_openMenu == 3) {
        rect = NSMakeRect(570, 289, 148, 18.0 * kCodecModeCount);
        count = kCodecModeCount;
    }
    if (count == 0u) return;
    const int next = s3g::clap_gui::dropdownHitIndex(point, rect, 18.0, count);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softTitleAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [@"FAULT" drawAtPoint:NSMakePoint(18, 14) withAttributes:labels];
    [s3g::clap_gui::peakDbText(p->outputPeak.load(std::memory_order_relaxed)) drawAtPoint:NSMakePoint(708, 14) withAttributes:values];
    [@"0>8" drawAtPoint:NSMakePoint(826, 14) withAttributes:values];
    [self drawWaveforms:p style:style];

    s3g::clap_gui::drawPanelFrame(18, 238, 414, 322, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD / SPACE", true, 18, 238, 414, 21, labels, style);
    s3g::clap_gui::drawPanelFrame(448, 238, 414, 322, style);
    s3g::clap_gui::drawPanelHeader(@"CODEC / SHAPE", true, 448, 238, 414, 21, labels, style);
    s3g::clap_gui::drawPanelFrame(18, 578, 844, 244, style);
    s3g::clap_gui::drawPanelHeader(@"PATCH / PERFORMANCE", true, 18, 578, 844, 21, labels, style);

    const auto& params = p->params;
    const std::string scanText = p->rawSource && p->rawSource->waveform
        ? waveformSpeedText(params.scanRate)
        : rateText(scanBytesPerSecond(params.scanRate, p->sampleRate), "B/s");
    const std::string codecRateText = rateText(codecUpdatesPerSecond(params.codecRate, p->sampleRate), "Hz");
    const std::string fieldEvolveText = evolveText(params.evolve);
    [self drawRow:@"SCAN" value:[NSString stringWithUTF8String:scanText.c_str()] norm:params.scanRate x:36 y:274 attrs:labels small:values];
    [self drawRow:@"TEXTURE" value:[NSString stringWithFormat:@"%.0f%%", params.texture * 100.0f] norm:params.texture x:36 y:304 attrs:labels small:values];
    [self drawRow:@"GEOMETRY" value:[NSString stringWithFormat:@"%.0f%%", params.geometry * 100.0f] norm:params.geometry x:36 y:334 attrs:labels small:values];
    [self drawRow:@"CHAOS" value:[NSString stringWithFormat:@"%.0f%%", params.chaos * 100.0f] norm:params.chaos x:36 y:364 attrs:labels small:values];
    [self drawRow:@"FOLD" value:[NSString stringWithFormat:@"%.0f%%", params.fold * 100.0f] norm:params.fold x:36 y:394 attrs:labels small:values];
    [self drawRow:@"EVOLVE" value:[NSString stringWithUTF8String:fieldEvolveText.c_str()] norm:params.evolve x:36 y:424 attrs:labels small:values];
    [self drawMenuControl:@"ROUTE" value:[NSString stringWithUTF8String:channelSchemeName(static_cast<uint32_t>(params.channelScheme))] x:36 y:454 attrs:labels small:values style:style];
    [self drawRow:@"SPREAD" value:[NSString stringWithFormat:@"%.0f%%", params.channelSpread * 100.0f] norm:params.channelSpread x:36 y:484 attrs:labels small:values];
    [self drawRow:@"OUTPUT" value:[NSString stringWithFormat:@"%+.1f dB", params.gainDb] norm:normalizedParam(params, kGainParamId) x:36 y:514 attrs:labels small:values];

    [self drawMenuControl:@"CODEC" value:[NSString stringWithUTF8String:codecModeName(static_cast<uint32_t>(params.codecMode))] x:466 y:274 attrs:labels small:values style:style];
    [self drawRow:@"RATE" value:[NSString stringWithUTF8String:codecRateText.c_str()] norm:params.codecRate x:466 y:304 attrs:labels small:values];
    [self drawRow:@"BITS" value:[NSString stringWithFormat:@"%.0f", params.bitDepth] norm:normalizedParam(params, kBitDepthParamId) x:466 y:334 attrs:labels small:values];
    [self drawRow:@"DAMAGE" value:[NSString stringWithFormat:@"%.0f%%", params.codecDamage * 100.0f] norm:params.codecDamage x:466 y:364 attrs:labels small:values];
    [self drawRow:@"DRIVE" value:[NSString stringWithFormat:@"%.0f%%", params.drive * 100.0f] norm:params.drive x:466 y:408 attrs:labels small:values];
    [self drawRow:@"SHRED" value:[NSString stringWithFormat:@"%.0f%%", params.shred * 100.0f] norm:params.shred x:466 y:438 attrs:labels small:values];
    [self drawRow:@"RESONANCE" value:[NSString stringWithFormat:@"%.0f%%", params.resonance * 100.0f] norm:params.resonance x:466 y:468 attrs:labels small:values];

    [self drawMenuControl:@"PRESET" value:[NSString stringWithUTF8String:presetName(p->selectedPreset)] x:36 y:610 attrs:labels small:values style:style];
    [self drawButton:@"RANDOM PATCH" rect:NSMakeRect(466, 603, 126, 24) attrs:values];
    [self drawButton:@"MUTATE" rect:NSMakeRect(604, 603, 90, 24) attrs:values];
    [self drawButton:@"UNDO" rect:NSMakeRect(706, 603, 90, 24) attrs:values];
    [self drawPerformanceMode:p attrs:labels small:values style:style];
    [self drawEnvelopeEditor:p attrs:values style:style];
    [self drawOpenMenu:values style:style];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider >= 1 && _dragSlider <= 8) {
        static const clap_id ids[] = {
            kScanRateParamId, kTextureParamId, kGeometryParamId, kChaosParamId,
            kFoldParamId, kEvolveParamId, kChannelSpreadParamId, kGainParamId
        };
        const double normalized = std::clamp((point.x - 140.0) / 148.0, 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 1], normalized);
    } else if (_dragSlider >= 101 && _dragSlider <= 106) {
        static const clap_id ids[] = {
            kCodecRateParamId, kBitDepthParamId, kCodecDamageParamId,
            kDriveParamId, kShredParamId, kResonanceParamId
        };
        const double normalized = std::clamp((point.x - 570.0) / 148.0, 0.0, 1.0);
        applyNormalizedParam(*p, ids[_dragSlider - 101], normalized);
    } else if (_dragSlider >= 201 && _dragSlider <= 204) {
        const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*p);
        const double sustain = std::clamp(
            static_cast<double>((graph.bottom - point.y) / (graph.bottom - graph.top)), 0.0, 1.0);
        if (_dragSlider == 201) {
            applyNormalizedParam(*p, kAttackParamId,
                std::clamp(static_cast<double>((point.x - 100.0) / 150.0), 0.0, 1.0));
        } else if (_dragSlider == 202) {
            applyNormalizedParam(*p, kDecayParamId,
                std::clamp(static_cast<double>((point.x - 300.0) / 165.0), 0.0, 1.0));
            applyNormalizedParam(*p, kSustainParamId, sustain);
        } else if (_dragSlider == 203) {
            applyNormalizedParam(*p, kSustainParamId, sustain);
        } else {
            applyNormalizedParam(*p, kReleaseParamId,
                std::clamp(static_cast<double>((point.x - 670.0) / 160.0), 0.0, 1.0));
        }
        markHostStateDirty(*p);
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu != 0) {
        NSRect rect = NSZeroRect;
        uint32_t count = 0u;
        clap_id id = CLAP_INVALID_ID;
        if (_openMenu == 1) { rect = NSMakeRect(140, 375, 148, 18.0 * 14.0); count = 14u; id = kPresetParamId; }
        else if (_openMenu == 2) { rect = NSMakeRect(140, 469, 148, 18.0 * 5.0); count = 5u; id = kChannelSchemeParamId; }
        else if (_openMenu == 3) {
            rect = NSMakeRect(570, 289, 148, 18.0 * kCodecModeCount);
            count = kCodecModeCount;
            id = kCodecModeParamId;
        }
        const int hit = s3g::clap_gui::dropdownHitIndex(point, rect, 18.0, count);
        if (hit >= 0 && id != CLAP_INVALID_ID) applyParam(*p, id, hit);
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(140, 609, 148, 16))) { _openMenu = 1; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, NSMakeRect(140, 453, 148, 16))) { _openMenu = 2; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, NSMakeRect(570, 273, 148, 16))) { _openMenu = 3; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, NSMakeRect(140, 642, 148, 22))) {
        applyParam(*p, kPerformanceModeParamId,
            point.x < 214.0 ? static_cast<double>(PerformanceMode::Free) : static_cast<double>(PerformanceMode::Midi));
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(506, 49, 46, 18))) {
        applyParam(*p, kRunParamId, 1.0);
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(560, 49, 48, 18))) {
        applyParam(*p, kRunParamId, 0.0);
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(616, 49, 104, 18))) {
        [self chooseRawFile];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(730, 49, 108, 18))) {
        applyParam(*p, kRandomizeFieldParamId, std::fmod(static_cast<double>(p->params.seed) * 0.61803398875 + 0.37, 1.0));
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(466, 603, 126, 24))) {
        applyParam(*p, kRandomizePatchParamId, std::fmod(static_cast<double>(p->params.seed) * 0.754877666 + 0.21, 1.0));
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(604, 603, 90, 24))) {
        applyParam(*p, kMutateParamId, std::fmod(static_cast<double>(p->params.seed) * 0.569840291 + 0.43, 1.0));
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(706, 603, 90, 24))) {
        applyParam(*p, kUndoParamId, 1.0);
        markHostStateDirty(*p);
        [self setNeedsDisplay:YES];
        return;
    }

    const EnvelopeGraphGeometry graph = envelopeGraphGeometry(*p);
    if (NSPointInRect(point, graph.frame)) {
        const std::array<NSPoint, 4> handles {
            graph.attack, graph.decay, graph.sustain, graph.release
        };
        int selected = -1;
        CGFloat bestDistance = 14.0 * 14.0;
        for (int index = 0; index < static_cast<int>(handles.size()); ++index) {
            const CGFloat distance = squaredDistance(point, handles[index]);
            if (distance <= bestDistance) {
                selected = index;
                bestDistance = distance;
            }
        }
        if (selected < 0) {
            if (point.x < 275.0) selected = 0;
            else if (point.x < 545.0) selected = 1;
            else if (point.x < 645.0) selected = 2;
            else selected = 3;
        }
        _dragSlider = selected + 201;
        [self updateSlider:point];
        return;
    }

    static const CGFloat leftRows[] = { 274, 304, 334, 364, 394, 424, 484, 514 };
    for (int i = 0; i < 8; ++i) {
        if (NSPointInRect(point, NSMakeRect(30, leftRows[i] - 9.0, 390, 24))) {
            _dragSlider = i + 1;
            [self updateSlider:point];
            return;
        }
    }
    static const CGFloat rightRows[] = { 304, 334, 364, 408, 438, 468 };
    for (int i = 0; i < 6; ++i) {
        if (NSPointInRect(point, NSMakeRect(460, rightRows[i] - 9.0, 390, 24))) {
            _dragSlider = i + 101;
            [self updateSlider:point];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)resetCursorRects
{
    [super resetCursorRects];
    [self addCursorRect:NSMakeRect(36.0, 674.0, 808.0, 116.0) cursor:[NSCursor crosshairCursor]];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GPsdRawFieldView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false;
    }
    return true;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible = false;
        auto* view = static_cast<S3GPsdRawFieldView*>(p->guiView);
        [view stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GPsdRawFieldView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GPsdRawFieldView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}
const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide
};
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.fault",
    "Fault",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.9.1",
    "Eight-channel free-running or MIDI-playable byte geometry, codec damage, and resonant nonlinear synthesis.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->plugin.desc = &descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = init;
    p->plugin.destroy = destroy;
    p->plugin.activate = activate;
    p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing;
    p->plugin.reset = reset;
    p->plugin.process = process;
    p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
