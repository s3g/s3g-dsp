#include "s3g_ambi_pulsar_encoder.h"
#include "s3g_ambi_pulsar_presets.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiPulsarMaxChannels;
constexpr uint32_t kStateVersion = 8u;
constexpr uint32_t kCustomPresetMagic = 0x50473353u; // "S3GP" on little-endian hosts
constexpr uint32_t kCustomPresetVersion = 2u;
constexpr uint32_t kParamBankSize = 128u;
constexpr size_t kLegacyParamsSize = offsetof(s3g::AmbiPulsarParams, listening);
static_assert(kLegacyParamsSize + sizeof(s3g::AmbiPulsarListeningParams)
        == sizeof(s3g::AmbiPulsarParams),
    "listening parameters must remain an appended state suffix");

constexpr clap_id kPresetParamId = 1u;
constexpr clap_id kOrderParamId = 2u;
constexpr clap_id kEmissionParamId = 3u;
constexpr clap_id kEmissionModRateParamId = 4u;
constexpr clap_id kEmissionModDepthParamId = 5u;
constexpr clap_id kFormantModRateParamId = 6u;
constexpr clap_id kFormantModDepthParamId = 7u;
constexpr clap_id kFormantScatterParamId = 8u;
constexpr clap_id kPhaseScatterParamId = 9u;
constexpr clap_id kProbabilityParamId = 10u;
constexpr clap_id kBurstOnParamId = 11u;
constexpr clap_id kBurstOffParamId = 12u;
constexpr clap_id kSieveModuloParamId = 13u;
constexpr clap_id kSieveResidueParamId = 14u;
constexpr clap_id kDistributionParamId = 15u;

constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneFormantIds {{ 20u, 25u, 30u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneOverlapIds {{ 21u, 26u, 31u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneLevelIds {{ 22u, 27u, 32u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneOffsetIds {{ 23u, 28u, 33u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneWaveformIds {{ 24u, 29u, 34u }};

constexpr clap_id kEnvelopeParamId = 35u;
constexpr clap_id kEnvelopeEdgeParamId = 36u;
constexpr clap_id kQualityParamId = 37u;
constexpr clap_id kAzimuthParamId = 38u;
constexpr clap_id kElevationParamId = 39u;
constexpr clap_id kDistanceParamId = 40u;
constexpr clap_id kWidthParamId = 41u;
constexpr clap_id kSpatialScatterParamId = 42u;
constexpr clap_id kOrbitRateParamId = 43u;
constexpr clap_id kOrbitDepthParamId = 44u;
constexpr clap_id kSpatialFollowParamId = 45u;
constexpr clap_id kAirParamId = 46u;
constexpr clap_id kDopplerParamId = 47u;
constexpr clap_id kOutputParamId = 48u;
constexpr clap_id kPointsParamId = 49u;
constexpr clap_id kMotionModeParamId = 50u;

constexpr clap_id kNeuralLevelParamId = 60u;
constexpr clap_id kNeuralDriveParamId = 61u;
constexpr clap_id kNeuralFeedbackParamId = 62u;
constexpr clap_id kNeuralCouplingParamId = 63u;
constexpr clap_id kNeuralHierarchyParamId = 64u;
constexpr clap_id kNeuralPhaseParamId = 65u;
constexpr clap_id kNeuralBrownianParamId = 66u;
constexpr clap_id kNeuralDriftParamId = 67u;
constexpr clap_id kNeuralSelfModParamId = 68u;
constexpr clap_id kNeuralAudioFeedbackParamId = 69u;
constexpr clap_id kNeuralPulsaretParamId = 70u;
constexpr clap_id kNeuralEnvelopeParamId = 71u;
constexpr clap_id kNeuralFmParamId = 72u;
constexpr clap_id kNeuralCaptureParamId = 73u;

constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneTuneModeIds {{ 80u, 86u, 92u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneCarrierRatioIds {{ 81u, 87u, 93u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneRetriggerModeIds {{ 82u, 88u, 94u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneFmRatioIds {{ 83u, 89u, 95u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneFmIndexIds {{ 84u, 90u, 96u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneWindowSkewIds {{ 85u, 91u, 97u }};

constexpr clap_id kNeuralSetParamId = 100u;
constexpr clap_id kListeningEnableParamId = 101u;
constexpr clap_id kListeningBypassParamId = 102u;
constexpr clap_id kPickupSetParamId = 103u;
constexpr clap_id kListeningModeParamId = 104u;
constexpr clap_id kFieldReturnParamId = 105u;
constexpr clap_id kPropagationParamId = 106u;
constexpr clap_id kPickupFocusParamId = 107u;
constexpr clap_id kLaneListeningParamId = 108u;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiPulsarParams params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewZoom = 1.0f;
    char customPresetName[64] {};
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiPulsarParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiPulsarEncoder engine {};
    // params is a main-thread GUI snapshot; audioParams is audio-thread owned.
    s3g::AmbiPulsarParams params {};
    s3g::AmbiPulsarParams audioParams {};
    std::atomic<uint32_t> presetIndex { 0u };
    char customPresetName[64] {};
    std::atomic<bool> customPresetActive { false };
    std::array<std::atomic<double>, kParamBankSize> parameterValues {};
    std::atomic<uint64_t> guiParamRevision { 0u };
    uint64_t audioParamRevision = 0u;
    std::atomic<uint32_t> audioResetRequest { 0u };
    uint32_t audioResetSerial = 0u;
    uint32_t randomSeed = 0x7158492du;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kAmbiPulsarMaxPoints> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarMaxPoints> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarMaxPoints> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarMaxPoints> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarMaxPoints> guiEvent {};
    std::atomic<uint32_t> guiSelectedPoint { 0u };
    int32_t guiViewMode = 2;
    float guiViewZoom = 1.0f;
    std::array<std::atomic<float>, s3g::kAmbiPulsarNeuralMaxNodes> guiNeuralNode {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarNeuralMaxClusters> guiNeuralCluster {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarListeningPickups> guiListeningPickup {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarListeningPickups> guiListeningEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarNeuralLobes> guiListeningReturn {};
    std::atomic<float> guiListeningAnalysis { 0.0f };
    std::atomic<float> guiListeningInfluence { 0.0f };
    std::atomic<float> guiCaptureProgress { 0.0f };
    std::atomic<uint32_t> guiCaptureGeneration { 0u };
    std::atomic<uint32_t> guiCaptureRequestSerial { 0u };
    std::atomic<uint32_t> guiCaptureCompletedSerial { 0u };
    uint32_t audioCaptureRequestSerial = 0u;
    uint32_t audioPendingCaptureSerial = 0u;
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void publishParams(Plugin& plugin, s3g::AmbiPulsarParams params, uint32_t presetIndex, bool requestProcess);
void syncGuiParams(Plugin& plugin);
void syncAudioParams(Plugin& plugin);
void applyAudioParam(Plugin& plugin, clap_id id, double value);

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0u;
    while (done < size) {
        const int64_t count = stream->write(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0u;
    while (done < size) {
        const int64_t count = stream->read(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

bool saveCustomPresetFile(const char* path, const Plugin& plugin, const char* name)
{
    if (!path || !*path) return false;
    CustomPresetFile file {};
    std::snprintf(file.name, sizeof(file.name), "%s", name && *name ? name : "Custom");
    file.params = s3g::sanitizeAmbiPulsarParams(plugin.params);
    FILE* handle = std::fopen(path, "wb");
    if (!handle) return false;
    const bool ok = std::fwrite(&file, 1u, sizeof(file), handle) == sizeof(file);
    std::fclose(handle);
    return ok;
}

bool loadCustomPresetFile(const char* path, CustomPresetFile& file)
{
    if (!path || !*path) return false;
    FILE* handle = std::fopen(path, "rb");
    if (!handle) return false;
    file = {};
    uint32_t magic = 0u;
    uint32_t version = 0u;
    bool ok = std::fread(&magic, 1u, sizeof(magic), handle) == sizeof(magic)
        && std::fread(&version, 1u, sizeof(version), handle) == sizeof(version)
        && magic == kCustomPresetMagic && (version == 1u || version == kCustomPresetVersion);
    file.magic = magic;
    file.version = kCustomPresetVersion;
    if (ok) ok = std::fread(file.name, 1u, sizeof(file.name), handle) == sizeof(file.name);
    if (ok) {
        const size_t paramsSize = version == 1u ? kLegacyParamsSize : sizeof(file.params);
        ok = std::fread(&file.params, 1u, paramsSize, handle) == paramsSize;
    }
    std::fclose(handle);
    if (!ok) return false;
    file.name[sizeof(file.name) - 1u] = '\0';
    file.params = s3g::sanitizeAmbiPulsarParams(file.params);
    return true;
}

float randomUnit(uint32_t& seed)
{
    seed += 0x9e3779b9u;
    uint32_t value = seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float randomRange(uint32_t& seed, float low, float high)
{
    return low + (high - low) * randomUnit(seed);
}

void randomizeSafe(Plugin& plugin)
{
    syncGuiParams(plugin);
    auto p = plugin.params;
    uint32_t seed = plugin.randomSeed;
    p.order = 3u;
    p.emissionHz = std::pow(2.0f, randomRange(seed, std::log2(1.2f), std::log2(120.0f)));
    p.emissionModRateHz = std::pow(2.0f, randomRange(seed, std::log2(0.008f), std::log2(4.0f)));
    p.emissionModDepth = randomRange(seed, 0.02f, 0.68f);
    p.formantModRateHz = std::pow(2.0f, randomRange(seed, std::log2(0.05f), std::log2(18.0f)));
    p.formantModDepthSemitones = randomRange(seed, 0.0f, 5.0f);
    p.formantScatterSemitones = randomRange(seed, 0.0f, 4.5f);
    p.phaseScatter = randomRange(seed, 0.0f, 0.65f);
    p.probability = randomRange(seed, 0.45f, 1.0f);
    p.burstOn = 3u + static_cast<uint32_t>(randomUnit(seed) * 14.0f);
    p.burstOff = randomUnit(seed) < 0.55f ? 0u : 1u + static_cast<uint32_t>(randomUnit(seed) * 8.0f);
    p.sieveModulo = randomUnit(seed) < 0.68f ? 1u : 2u + static_cast<uint32_t>(randomUnit(seed) * 6.0f);
    p.sieveResidue = static_cast<uint32_t>(randomUnit(seed) * static_cast<float>(p.sieveModulo));
    p.pointRandomness = randomRange(seed, 0.0f, 0.88f);
    const float root = std::pow(2.0f, randomRange(seed, std::log2(45.0f), std::log2(620.0f)));
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        p.lanes[lane].formantHz = root * std::pow(randomRange(seed, 1.35f, 2.75f), static_cast<float>(lane));
        p.lanes[lane].overlap = std::pow(2.0f, randomRange(seed, std::log2(0.12f), std::log2(4.5f)));
        p.lanes[lane].level = randomRange(seed, 0.38f, 0.98f);
        p.lanes[lane].triggerOffset = lane == 0u ? 0.0f : randomRange(seed, 0.04f, 0.72f);
        p.lanes[lane].waveform = static_cast<s3g::AmbiPulsarWaveform>(static_cast<uint32_t>(randomUnit(seed) * 8.0f) % 8u);
        const float tuneChoice = randomUnit(seed);
        p.advancedLanes[lane].tuneMode = tuneChoice < 0.55f ? s3g::AmbiPulsarTuneMode::Hertz
            : tuneChoice < 0.86f ? s3g::AmbiPulsarTuneMode::Ratio : s3g::AmbiPulsarTuneMode::Subharmonic;
        p.advancedLanes[lane].carrierRatio = std::pow(2.0f,
            randomRange(seed, std::log2(0.5f), std::log2(24.0f)));
        const float retriggerChoice = randomUnit(seed);
        p.advancedLanes[lane].retriggerMode = retriggerChoice < 0.68f
            ? s3g::AmbiPulsarRetriggerMode::Retrigger : retriggerChoice < 0.89f
            ? s3g::AmbiPulsarRetriggerMode::Free : s3g::AmbiPulsarRetriggerMode::IdleOnly;
        p.advancedLanes[lane].fmRatio = std::pow(2.0f,
            randomRange(seed, std::log2(0.5f), std::log2(12.0f)));
        p.advancedLanes[lane].fmIndex = randomUnit(seed) < 0.56f
            ? 0.0f : randomRange(seed, 0.15f, 6.0f);
        p.advancedLanes[lane].windowSkew = randomRange(seed, 0.14f, 0.86f);
    }
    p.envelope = static_cast<s3g::AmbiPulsarEnvelope>(static_cast<uint32_t>(randomUnit(seed) * 5.0f) % 5u);
    p.envelopeEdge = randomRange(seed, 0.12f, 0.88f);
    p.quality = s3g::AmbiPulsarQuality::High;
    p.centerAzimuthDeg = randomRange(seed, -35.0f, 35.0f);
    p.centerElevationDeg = randomRange(seed, -18.0f, 22.0f);
    p.centerDistance = randomRange(seed, 0.72f, 1.48f);
    p.spatialWidth = randomRange(seed, 0.24f, 0.96f);
    p.spatialScatter = randomRange(seed, 0.0f, 0.42f);
    p.orbitRateHz = randomRange(seed, -0.15f, 0.15f);
    p.orbitDepth = randomRange(seed, 0.12f, 0.88f);
    p.spatialFollow = randomRange(seed, 0.35f, 0.92f);
    p.air = randomRange(seed, 0.0f, 0.42f);
    p.doppler = randomUnit(seed) < 0.75f ? 0.0f : randomRange(seed, 0.05f, 0.35f);
    p.outputGainDb = -12.0f;
    p.seed = seed;
    p.points = 4u + static_cast<uint32_t>(randomUnit(seed) * 29.0f) % 29u;
    p.motionMode = static_cast<s3g::AmbiPulsarMotionMode>(
        static_cast<uint32_t>(randomUnit(seed) * 5.0f) % 5u);
    p.neuralLevel = randomUnit(seed) < 0.58f ? 0.0f : randomRange(seed, 0.08f, 0.52f);
    p.neural.drive = randomRange(seed, 1.15f, 3.35f);
    p.neural.feedback = randomRange(seed, 0.48f, 1.04f);
    p.neural.coupling = randomRange(seed, 0.12f, 0.82f);
    p.neural.hierarchy = randomRange(seed, 0.12f, 0.92f);
    p.neural.phaseShift = randomRange(seed, 0.0f, 0.88f);
    p.neural.brownian = randomRange(seed, 0.0f, 0.68f);
    p.neural.drift = randomRange(seed, 0.0f, 0.72f);
    p.neural.selfModulation = randomRange(seed, 0.0f, 0.92f);
    p.neural.audioFeedback = randomUnit(seed) < 0.64f ? 0.0f : randomRange(seed, 0.04f, 0.52f);
    p.neural.seed = seed ^ 0x4e455552u;
    p.neuralPulsaretMix = randomUnit(seed) < 0.60f ? 0.0f : randomRange(seed, 0.08f, 0.72f);
    p.neuralEnvelopeMix = randomUnit(seed) < 0.68f ? 0.0f : randomRange(seed, 0.08f, 0.62f);
    p.neuralFmDepthSemitones = randomUnit(seed) < 0.66f ? 0.0f : randomRange(seed, 0.25f, 7.0f);
    p.listening.neuralSet = static_cast<s3g::AmbiPulsarNeuralSet>(
        static_cast<uint32_t>(randomUnit(seed) * 3.0f) % 3u);
    p.listening.enabled = randomUnit(seed) < 0.58f ? 1u : 0u;
    p.listening.bypass = 0u;
    p.listening.pickupSet = randomUnit(seed) < 0.52f
        ? s3g::AmbiPulsarPickupSet::Tetra4 : s3g::AmbiPulsarPickupSet::Cube8;
    p.listening.mode = static_cast<s3g::AmbiPulsarListeningMode>(
        static_cast<uint32_t>(randomUnit(seed) * 4.0f) % 4u);
    p.listening.fieldReturn = randomRange(seed, 0.12f, 0.58f);
    p.listening.propagationMs = randomRange(seed, 2.0f, 96.0f);
    p.listening.focus = randomRange(seed, 0.28f, 0.96f);
    p.listening.laneInfluence = randomRange(seed, 0.0f, 0.72f);
    plugin.randomSeed = seed;
    plugin.customPresetActive.store(false, std::memory_order_relaxed);
    publishParams(plugin, s3g::sanitizeAmbiPulsarParams(p), 0u, true);
}

template <typename F>
bool laneForParam(clap_id id, const std::array<clap_id, s3g::kAmbiPulsarLanes>& ids, F&& fn)
{
    for (uint32_t lane = 0u; lane < ids.size(); ++lane) {
        if (id == ids[lane]) {
            fn(lane);
            return true;
        }
    }
    return false;
}

bool assignParam(s3g::AmbiPulsarParams& p, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: p.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kEmissionParamId: p.emissionHz = static_cast<float>(value); return true;
    case kEmissionModRateParamId: p.emissionModRateHz = static_cast<float>(value); return true;
    case kEmissionModDepthParamId: p.emissionModDepth = static_cast<float>(value); return true;
    case kFormantModRateParamId: p.formantModRateHz = static_cast<float>(value); return true;
    case kFormantModDepthParamId: p.formantModDepthSemitones = static_cast<float>(value); return true;
    case kFormantScatterParamId: p.formantScatterSemitones = static_cast<float>(value); return true;
    case kPhaseScatterParamId: p.phaseScatter = static_cast<float>(value); return true;
    case kProbabilityParamId: p.probability = static_cast<float>(value); return true;
    case kBurstOnParamId: p.burstOn = static_cast<uint32_t>(std::lround(value)); return true;
    case kBurstOffParamId: p.burstOff = static_cast<uint32_t>(std::lround(value)); return true;
    case kSieveModuloParamId: p.sieveModulo = static_cast<uint32_t>(std::lround(value)); return true;
    case kSieveResidueParamId: p.sieveResidue = static_cast<uint32_t>(std::lround(value)); return true;
    case kDistributionParamId: p.pointRandomness = static_cast<float>(value); return true;
    case kEnvelopeParamId: p.envelope = static_cast<s3g::AmbiPulsarEnvelope>(static_cast<uint32_t>(std::lround(value))); return true;
    case kEnvelopeEdgeParamId: p.envelopeEdge = static_cast<float>(value); return true;
    case kQualityParamId: p.quality = static_cast<s3g::AmbiPulsarQuality>(static_cast<uint32_t>(std::lround(value))); return true;
    case kAzimuthParamId: p.centerAzimuthDeg = static_cast<float>(value); return true;
    case kElevationParamId: p.centerElevationDeg = static_cast<float>(value); return true;
    case kDistanceParamId: p.centerDistance = static_cast<float>(value); return true;
    case kWidthParamId: p.spatialWidth = static_cast<float>(value); return true;
    case kSpatialScatterParamId: p.spatialScatter = static_cast<float>(value); return true;
    case kOrbitRateParamId: p.orbitRateHz = static_cast<float>(value); return true;
    case kOrbitDepthParamId: p.orbitDepth = static_cast<float>(value); return true;
    case kSpatialFollowParamId: p.spatialFollow = static_cast<float>(value); return true;
    case kAirParamId: p.air = static_cast<float>(value); return true;
    case kDopplerParamId: p.doppler = static_cast<float>(value); return true;
    case kOutputParamId: p.outputGainDb = static_cast<float>(value); return true;
    case kPointsParamId: p.points = static_cast<uint32_t>(std::lround(value)); return true;
    case kMotionModeParamId: p.motionMode = static_cast<s3g::AmbiPulsarMotionMode>(
        static_cast<uint32_t>(std::lround(value))); return true;
    case kNeuralLevelParamId: p.neuralLevel = static_cast<float>(value); return true;
    case kNeuralDriveParamId: p.neural.drive = static_cast<float>(value); return true;
    case kNeuralFeedbackParamId: p.neural.feedback = static_cast<float>(value); return true;
    case kNeuralCouplingParamId: p.neural.coupling = static_cast<float>(value); return true;
    case kNeuralHierarchyParamId: p.neural.hierarchy = static_cast<float>(value); return true;
    case kNeuralPhaseParamId: p.neural.phaseShift = static_cast<float>(value); return true;
    case kNeuralBrownianParamId: p.neural.brownian = static_cast<float>(value); return true;
    case kNeuralDriftParamId: p.neural.drift = static_cast<float>(value); return true;
    case kNeuralSelfModParamId: p.neural.selfModulation = static_cast<float>(value); return true;
    case kNeuralAudioFeedbackParamId: p.neural.audioFeedback = static_cast<float>(value); return true;
    case kNeuralPulsaretParamId: p.neuralPulsaretMix = static_cast<float>(value); return true;
    case kNeuralEnvelopeParamId: p.neuralEnvelopeMix = static_cast<float>(value); return true;
    case kNeuralFmParamId: p.neuralFmDepthSemitones = static_cast<float>(value); return true;
    case kNeuralCaptureParamId: p.neuralCapture = static_cast<uint32_t>(std::lround(value)); return true;
    case kNeuralSetParamId: p.listening.neuralSet = static_cast<s3g::AmbiPulsarNeuralSet>(
        static_cast<uint32_t>(std::lround(value))); return true;
    case kListeningEnableParamId: p.listening.enabled = static_cast<uint32_t>(std::lround(value)); return true;
    case kListeningBypassParamId: p.listening.bypass = static_cast<uint32_t>(std::lround(value)); return true;
    case kPickupSetParamId: p.listening.pickupSet = static_cast<s3g::AmbiPulsarPickupSet>(
        static_cast<uint32_t>(std::lround(value))); return true;
    case kListeningModeParamId: p.listening.mode = static_cast<s3g::AmbiPulsarListeningMode>(
        static_cast<uint32_t>(std::lround(value))); return true;
    case kFieldReturnParamId: p.listening.fieldReturn = static_cast<float>(value); return true;
    case kPropagationParamId: p.listening.propagationMs = static_cast<float>(value); return true;
    case kPickupFocusParamId: p.listening.focus = static_cast<float>(value); return true;
    case kLaneListeningParamId: p.listening.laneInfluence = static_cast<float>(value); return true;
    default: break;
    }
    if (laneForParam(id, kLaneFormantIds, [&](uint32_t lane) { p.lanes[lane].formantHz = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneOverlapIds, [&](uint32_t lane) { p.lanes[lane].overlap = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneLevelIds, [&](uint32_t lane) { p.lanes[lane].level = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneOffsetIds, [&](uint32_t lane) { p.lanes[lane].triggerOffset = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneWaveformIds, [&](uint32_t lane) {
        p.lanes[lane].waveform = static_cast<s3g::AmbiPulsarWaveform>(static_cast<uint32_t>(std::lround(value)));
    })) return true;
    if (laneForParam(id, kLaneTuneModeIds, [&](uint32_t lane) {
        p.advancedLanes[lane].tuneMode = static_cast<s3g::AmbiPulsarTuneMode>(static_cast<uint32_t>(std::lround(value)));
    })) return true;
    if (laneForParam(id, kLaneCarrierRatioIds, [&](uint32_t lane) {
        p.advancedLanes[lane].carrierRatio = static_cast<float>(value);
    })) return true;
    if (laneForParam(id, kLaneRetriggerModeIds, [&](uint32_t lane) {
        p.advancedLanes[lane].retriggerMode = static_cast<s3g::AmbiPulsarRetriggerMode>(static_cast<uint32_t>(std::lround(value)));
    })) return true;
    if (laneForParam(id, kLaneFmRatioIds, [&](uint32_t lane) {
        p.advancedLanes[lane].fmRatio = static_cast<float>(value);
    })) return true;
    if (laneForParam(id, kLaneFmIndexIds, [&](uint32_t lane) {
        p.advancedLanes[lane].fmIndex = static_cast<float>(value);
    })) return true;
    if (laneForParam(id, kLaneWindowSkewIds, [&](uint32_t lane) {
        p.advancedLanes[lane].windowSkew = static_cast<float>(value);
    })) return true;
    return false;
}

double paramValue(const s3g::AmbiPulsarParams& p, clap_id id)
{
    switch (id) {
    case kOrderParamId: return p.order;
    case kEmissionParamId: return p.emissionHz;
    case kEmissionModRateParamId: return p.emissionModRateHz;
    case kEmissionModDepthParamId: return p.emissionModDepth;
    case kFormantModRateParamId: return p.formantModRateHz;
    case kFormantModDepthParamId: return p.formantModDepthSemitones;
    case kFormantScatterParamId: return p.formantScatterSemitones;
    case kPhaseScatterParamId: return p.phaseScatter;
    case kProbabilityParamId: return p.probability;
    case kBurstOnParamId: return p.burstOn;
    case kBurstOffParamId: return p.burstOff;
    case kSieveModuloParamId: return p.sieveModulo;
    case kSieveResidueParamId: return p.sieveResidue;
    case kDistributionParamId: return p.pointRandomness;
    case kEnvelopeParamId: return static_cast<uint32_t>(p.envelope);
    case kEnvelopeEdgeParamId: return p.envelopeEdge;
    case kQualityParamId: return static_cast<uint32_t>(p.quality);
    case kAzimuthParamId: return p.centerAzimuthDeg;
    case kElevationParamId: return p.centerElevationDeg;
    case kDistanceParamId: return p.centerDistance;
    case kWidthParamId: return p.spatialWidth;
    case kSpatialScatterParamId: return p.spatialScatter;
    case kOrbitRateParamId: return p.orbitRateHz;
    case kOrbitDepthParamId: return p.orbitDepth;
    case kSpatialFollowParamId: return p.spatialFollow;
    case kAirParamId: return p.air;
    case kDopplerParamId: return p.doppler;
    case kOutputParamId: return p.outputGainDb;
    case kPointsParamId: return p.points;
    case kMotionModeParamId: return static_cast<uint32_t>(p.motionMode);
    case kNeuralLevelParamId: return p.neuralLevel;
    case kNeuralDriveParamId: return p.neural.drive;
    case kNeuralFeedbackParamId: return p.neural.feedback;
    case kNeuralCouplingParamId: return p.neural.coupling;
    case kNeuralHierarchyParamId: return p.neural.hierarchy;
    case kNeuralPhaseParamId: return p.neural.phaseShift;
    case kNeuralBrownianParamId: return p.neural.brownian;
    case kNeuralDriftParamId: return p.neural.drift;
    case kNeuralSelfModParamId: return p.neural.selfModulation;
    case kNeuralAudioFeedbackParamId: return p.neural.audioFeedback;
    case kNeuralPulsaretParamId: return p.neuralPulsaretMix;
    case kNeuralEnvelopeParamId: return p.neuralEnvelopeMix;
    case kNeuralFmParamId: return p.neuralFmDepthSemitones;
    case kNeuralCaptureParamId: return p.neuralCapture;
    case kNeuralSetParamId: return static_cast<uint32_t>(p.listening.neuralSet);
    case kListeningEnableParamId: return p.listening.enabled;
    case kListeningBypassParamId: return p.listening.bypass;
    case kPickupSetParamId: return static_cast<uint32_t>(p.listening.pickupSet);
    case kListeningModeParamId: return static_cast<uint32_t>(p.listening.mode);
    case kFieldReturnParamId: return p.listening.fieldReturn;
    case kPropagationParamId: return p.listening.propagationMs;
    case kPickupFocusParamId: return p.listening.focus;
    case kLaneListeningParamId: return p.listening.laneInfluence;
    default: break;
    }
    double value = 0.0;
    if (laneForParam(id, kLaneFormantIds, [&](uint32_t lane) { value = p.lanes[lane].formantHz; })) return value;
    if (laneForParam(id, kLaneOverlapIds, [&](uint32_t lane) { value = p.lanes[lane].overlap; })) return value;
    if (laneForParam(id, kLaneLevelIds, [&](uint32_t lane) { value = p.lanes[lane].level; })) return value;
    if (laneForParam(id, kLaneOffsetIds, [&](uint32_t lane) { value = p.lanes[lane].triggerOffset; })) return value;
    if (laneForParam(id, kLaneWaveformIds, [&](uint32_t lane) { value = static_cast<uint32_t>(p.lanes[lane].waveform); })) return value;
    if (laneForParam(id, kLaneTuneModeIds, [&](uint32_t lane) { value = static_cast<uint32_t>(p.advancedLanes[lane].tuneMode); })) return value;
    if (laneForParam(id, kLaneCarrierRatioIds, [&](uint32_t lane) { value = p.advancedLanes[lane].carrierRatio; })) return value;
    if (laneForParam(id, kLaneRetriggerModeIds, [&](uint32_t lane) { value = static_cast<uint32_t>(p.advancedLanes[lane].retriggerMode); })) return value;
    if (laneForParam(id, kLaneFmRatioIds, [&](uint32_t lane) { value = p.advancedLanes[lane].fmRatio; })) return value;
    if (laneForParam(id, kLaneFmIndexIds, [&](uint32_t lane) { value = p.advancedLanes[lane].fmIndex; })) return value;
    if (laneForParam(id, kLaneWindowSkewIds, [&](uint32_t lane) { value = p.advancedLanes[lane].windowSkew; })) return value;
    return value;
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    syncGuiParams(plugin);
    if (id == kPresetParamId) {
        const uint32_t preset = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u,
            s3g::kAmbiPulsarFactoryPresetCount - 1u);
        plugin.customPresetActive.store(false, std::memory_order_relaxed);
        publishParams(plugin, s3g::ambiPulsarFactoryPreset(preset), preset, true);
        return;
    }
    if (!assignParam(plugin.params, id, value)) return;
    plugin.params = s3g::sanitizeAmbiPulsarParams(plugin.params);
    publishParams(plugin, plugin.params, plugin.presetIndex.load(std::memory_order_relaxed), true);
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    syncAudioParams(*p);
    p->engine.setParams(p->audioParams);
    p->engine.reset();
    p->audioResetSerial = p->audioResetRequest.load(std::memory_order_acquire);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* input)
{
    if (!input) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyAudioParam(plugin, value->param_id, value->value);
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    syncAudioParams(*p);
    const uint32_t resetRequest = p->audioResetRequest.load(std::memory_order_acquire);
    if (resetRequest != p->audioResetSerial) {
        p->audioResetSerial = resetRequest;
        p->engine.setParams(p->audioParams);
        p->engine.reset();
    }
    if (process->audio_outputs_count == 0u) {
        readParamEvents(*p, process->in_events);
        return CLAP_PROCESS_CONTINUE;
    }
    auto& output = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0u, frames);
    if (!output.data32 || channels == 0u) {
        readParamEvents(*p, process->in_events);
        return CLAP_PROCESS_CONTINUE;
    }

    const uint32_t captureRequest = p->guiCaptureRequestSerial.load(std::memory_order_acquire);
    if (captureRequest != p->audioCaptureRequestSerial) {
        p->audioCaptureRequestSerial = captureRequest;
        p->audioPendingCaptureSerial = captureRequest;
        p->audioParams.neuralCapture = (p->audioParams.neuralCapture + 1u) & 0xffffu;
        p->parameterValues[kNeuralCaptureParamId].store(p->audioParams.neuralCapture, std::memory_order_relaxed);
        p->engine.setParams(p->audioParams);
    }
    const uint32_t captureGenerationBefore = p->engine.neuralCaptureGeneration();

    auto renderRange = [&](uint32_t firstFrame, uint32_t frameCount) {
        if (frameCount == 0u) return;
        std::array<float*, kOutputChannels> outputs {};
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            outputs[channel] = output.data32[channel] ? output.data32[channel] + firstFrame : nullptr;
        }
        p->engine.process(outputs.data(), channels, frameCount);
    };

    uint32_t renderedFrames = 0u;
    if (process->in_events) {
        const uint32_t eventCount = process->in_events->size(process->in_events);
        for (uint32_t index = 0u; index < eventCount; ++index) {
            const clap_event_header_t* event = process->in_events->get(process->in_events, index);
            if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
                || event->type != CLAP_EVENT_PARAM_VALUE) continue;
            const uint32_t eventFrame = std::clamp<uint32_t>(event->time, renderedFrames, frames);
            renderRange(renderedFrames, eventFrame - renderedFrames);
            renderedFrames = eventFrame;
            const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyAudioParam(*p, value->param_id, value->value);
        }
    }
    renderRange(renderedFrames, frames - renderedFrames);
    s3g::clearAudioBufferFromChannel(output, channels, frames);

    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (!output.data32[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[channel][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.91f, peak), std::memory_order_relaxed);
    for (uint32_t index = 0u; index < s3g::kAmbiPulsarMaxPoints; ++index) {
        const auto point = p->engine.point(index);
        p->guiAzimuth[index].store(point.azimuthDeg, std::memory_order_relaxed);
        p->guiElevation[index].store(point.elevationDeg, std::memory_order_relaxed);
        p->guiDistance[index].store(point.distance, std::memory_order_relaxed);
        p->guiEnergy[index].store(p->engine.pointEnergy(index), std::memory_order_relaxed);
        p->guiEvent[index].store(p->engine.pointPulse(index), std::memory_order_relaxed);
    }
    for (uint32_t node = 0u; node < s3g::kAmbiPulsarNeuralMaxNodes; ++node) {
        p->guiNeuralNode[node].store(p->engine.neuralNode(node), std::memory_order_relaxed);
    }
    for (uint32_t cluster = 0u; cluster < s3g::kAmbiPulsarNeuralMaxClusters; ++cluster) {
        p->guiNeuralCluster[cluster].store(p->engine.neuralCluster(cluster), std::memory_order_relaxed);
    }
    for (uint32_t pickup = 0u; pickup < s3g::kAmbiPulsarListeningPickups; ++pickup) {
        p->guiListeningPickup[pickup].store(
            p->engine.listeningPickupValue(pickup), std::memory_order_relaxed);
        p->guiListeningEnergy[pickup].store(
            p->engine.listeningPickupEnergy(pickup), std::memory_order_relaxed);
    }
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiPulsarNeuralLobes; ++lobe) {
        p->guiListeningReturn[lobe].store(
            p->engine.listeningLobeReturn(lobe), std::memory_order_relaxed);
    }
    p->guiListeningAnalysis.store(
        p->engine.listeningAnalysisInfluence(), std::memory_order_relaxed);
    p->guiListeningInfluence.store(p->engine.listeningInfluence(), std::memory_order_relaxed);
    const uint32_t captureGeneration = p->engine.neuralCaptureGeneration();
    p->guiCaptureProgress.store(p->engine.neuralCaptureProgress(), std::memory_order_relaxed);
    p->guiCaptureGeneration.store(captureGeneration, std::memory_order_relaxed);
    if (p->audioPendingCaptureSerial != 0u && captureGeneration != captureGenerationBefore) {
        p->guiCaptureCompletedSerial.store(p->audioPendingCaptureSerial, std::memory_order_release);
        p->audioPendingCaptureSerial = 0u;
    }
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    info->id = 20u;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr ParamDef kParams[] {
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiPulsarFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Ambisonic Order", 1.0, 7.0, 3.0, true },
    { kEmissionParamId, "Emission Rate", 0.05, 2000.0, 18.0, false },
    { kEmissionModRateParamId, "Emission Mod Rate", 0.001, 80.0, 0.13, false },
    { kEmissionModDepthParamId, "Emission Mod Depth", 0.0, 0.95, 0.12, false },
    { kFormantModRateParamId, "Formant Mod Rate", 0.001, 200.0, 1.7, false },
    { kFormantModDepthParamId, "Formant Mod Depth", 0.0, 24.0, 0.45, false },
    { kFormantScatterParamId, "Formant Scatter", 0.0, 24.0, 0.22, false },
    { kPhaseScatterParamId, "Phase Scatter", 0.0, 1.0, 0.04, false },
    { kProbabilityParamId, "Pulse Probability", 0.0, 1.0, 0.92, false },
    { kBurstOnParamId, "Burst On", 1.0, 64.0, 8.0, true },
    { kBurstOffParamId, "Burst Rest", 0.0, 64.0, 0.0, true },
    { kSieveModuloParamId, "Sieve Modulo", 1.0, 32.0, 1.0, true },
    { kSieveResidueParamId, "Sieve Residue", 0.0, 31.0, 0.0, true },
    { kDistributionParamId, "Point Randomness", 0.0, 1.0, 0.08, false },
    { kLaneFormantIds[0], "Lane A Formant", 12.0, 22000.0, 240.0, false },
    { kLaneOverlapIds[0], "Lane A Overlap", 0.025, 8.0, 0.72, false },
    { kLaneLevelIds[0], "Lane A Level", 0.0, 1.5, 0.82, false },
    { kLaneOffsetIds[0], "Lane A Offset", 0.0, 0.95, 0.0, false },
    { kLaneWaveformIds[0], "Lane A Pulsaret", 0.0, 7.0, 0.0, true },
    { kLaneFormantIds[1], "Lane B Formant", 12.0, 22000.0, 720.0, false },
    { kLaneOverlapIds[1], "Lane B Overlap", 0.025, 8.0, 0.48, false },
    { kLaneLevelIds[1], "Lane B Level", 0.0, 1.5, 0.58, false },
    { kLaneOffsetIds[1], "Lane B Offset", 0.0, 0.95, 0.19, false },
    { kLaneWaveformIds[1], "Lane B Pulsaret", 0.0, 7.0, 4.0, true },
    { kLaneFormantIds[2], "Lane C Formant", 12.0, 22000.0, 1680.0, false },
    { kLaneOverlapIds[2], "Lane C Overlap", 0.025, 8.0, 0.32, false },
    { kLaneLevelIds[2], "Lane C Level", 0.0, 1.5, 0.42, false },
    { kLaneOffsetIds[2], "Lane C Offset", 0.0, 0.95, 0.41, false },
    { kLaneWaveformIds[2], "Lane C Pulsaret", 0.0, 7.0, 1.0, true },
    { kEnvelopeParamId, "Pulsaret Envelope", 0.0, 4.0, 0.0, true },
    { kEnvelopeEdgeParamId, "Envelope Edge", 0.01, 1.0, 0.38, false },
    { kQualityParamId, "Render Quality", 0.0, 2.0, 1.0, true },
    { kAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Center Elevation", -89.0, 89.0, 0.0, false },
    { kDistanceParamId, "Center Distance", 0.1, 8.0, 1.0, false },
    { kWidthParamId, "Spatial Width", 0.0, 1.0, 0.54, false },
    { kSpatialScatterParamId, "Spatial Scatter", 0.0, 1.0, 0.08, false },
    { kOrbitRateParamId, "Orbit Rate", -4.0, 4.0, 0.025, false },
    { kOrbitDepthParamId, "Orbit Depth", 0.0, 1.0, 0.36, false },
    { kSpatialFollowParamId, "Spatial Inertia", 0.0, 1.0, 0.72, false },
    { kAirParamId, "Air Absorption", 0.0, 1.0, 0.10, false },
    { kDopplerParamId, "Doppler", 0.0, 1.0, 0.0, false },
    { kOutputParamId, "Output Gain", -60.0, 6.0, -12.0, false },
    { kPointsParamId, "Spatial Points", 4.0, 32.0, 6.0, true },
    { kMotionModeParamId, "Field Motion", 0.0, 4.0, 0.0, true },
    { kNeuralLevelParamId, "Neural Direct Level", 0.0, 1.5, 0.0, false },
    { kNeuralDriveParamId, "Neural Sigmoid Drive", 0.25, 5.0, 1.85, false },
    { kNeuralFeedbackParamId, "Neural Ring Feedback", 0.0, 1.25, 0.76, false },
    { kNeuralCouplingParamId, "Neural Matrix Coupling", 0.0, 1.25, 0.38, false },
    { kNeuralHierarchyParamId, "Neural Hierarchy Gate", 0.0, 1.0, 0.52, false },
    { kNeuralPhaseParamId, "Neural Phase Paths", 0.0, 1.0, 0.28, false },
    { kNeuralBrownianParamId, "Neural Brownian Weights", 0.0, 1.0, 0.08, false },
    { kNeuralDriftParamId, "Neural Autonomous Drift", 0.0, 1.0, 0.10, false },
    { kNeuralSelfModParamId, "Neural Slow Fast Mod", 0.0, 1.0, 0.32, false },
    { kNeuralAudioFeedbackParamId, "Neural Audio Feedback", 0.0, 1.0, 0.0, false },
    { kNeuralPulsaretParamId, "Captured Pulsaret Mix", 0.0, 1.0, 0.0, false },
    { kNeuralEnvelopeParamId, "Captured Envelope Mix", 0.0, 1.0, 0.0, false },
    { kNeuralFmParamId, "Captured FM Depth", 0.0, 24.0, 0.0, false },
    { kNeuralCaptureParamId, "Capture Neural Tables", 0.0, 65535.0, 0.0, true },
    { kNeuralSetParamId, "Neural Population", 0.0, 2.0, 0.0, true },
    { kListeningEnableParamId, "Listening Enable", 0.0, 1.0, 0.0, true },
    { kListeningBypassParamId, "Listening Bypass", 0.0, 1.0, 0.0, true },
    { kPickupSetParamId, "Listening Ears", 0.0, 1.0, 0.0, true },
    { kListeningModeParamId, "Listening Topology", 0.0, 3.0, 0.0, true },
    { kFieldReturnParamId, "Listening Field Return", 0.0, 1.0, 0.34, false },
    { kPropagationParamId, "Listening Propagation", 0.0, 180.0, 24.0, false },
    { kPickupFocusParamId, "Listening Focus", 0.0, 1.0, 0.72, false },
    { kLaneListeningParamId, "Listening Field Response", 0.0, 1.0, 0.0, false },
    { kLaneTuneModeIds[0], "Lane A Tuning Mode", 0.0, 2.0, 0.0, true },
    { kLaneCarrierRatioIds[0], "Lane A Clock Ratio", 0.125, 128.0, 4.0, false },
    { kLaneRetriggerModeIds[0], "Lane A Retrigger Mode", 0.0, 2.0, 0.0, true },
    { kLaneFmRatioIds[0], "Lane A FM Ratio", 0.125, 64.0, 2.0, false },
    { kLaneFmIndexIds[0], "Lane A FM Index", 0.0, 20.0, 0.0, false },
    { kLaneWindowSkewIds[0], "Lane A Window Skew", 0.02, 0.98, 0.5, false },
    { kLaneTuneModeIds[1], "Lane B Tuning Mode", 0.0, 2.0, 0.0, true },
    { kLaneCarrierRatioIds[1], "Lane B Clock Ratio", 0.125, 128.0, 7.0, false },
    { kLaneRetriggerModeIds[1], "Lane B Retrigger Mode", 0.0, 2.0, 0.0, true },
    { kLaneFmRatioIds[1], "Lane B FM Ratio", 0.125, 64.0, 3.0, false },
    { kLaneFmIndexIds[1], "Lane B FM Index", 0.0, 20.0, 0.0, false },
    { kLaneWindowSkewIds[1], "Lane B Window Skew", 0.02, 0.98, 0.5, false },
    { kLaneTuneModeIds[2], "Lane C Tuning Mode", 0.0, 2.0, 0.0, true },
    { kLaneCarrierRatioIds[2], "Lane C Clock Ratio", 0.125, 128.0, 11.0, false },
    { kLaneRetriggerModeIds[2], "Lane C Retrigger Mode", 0.0, 2.0, 0.0, true },
    { kLaneFmRatioIds[2], "Lane C FM Ratio", 0.125, 64.0, 5.0, false },
    { kLaneFmIndexIds[2], "Lane C FM Index", 0.0, 20.0, 0.0, false },
    { kLaneWindowSkewIds[2], "Lane C Window Skew", 0.02, 0.98, 0.5, false },
};

const ParamDef* findParam(clap_id id)
{
    for (const auto& param : kParams) if (param.id == id) return &param;
    return nullptr;
}

s3g::AmbiPulsarParams paramsFromBank(const Plugin& plugin, s3g::AmbiPulsarParams base)
{
    for (const auto& param : kParams) {
        if (param.id == kPresetParamId || param.id >= kParamBankSize) continue;
        assignParam(base, param.id, plugin.parameterValues[param.id].load(std::memory_order_relaxed));
    }
    return s3g::sanitizeAmbiPulsarParams(base);
}

void storeParamBank(Plugin& plugin, const s3g::AmbiPulsarParams& params, uint32_t presetIndex)
{
    plugin.presetIndex.store(presetIndex, std::memory_order_relaxed);
    for (const auto& param : kParams) {
        if (param.id >= kParamBankSize) continue;
        const double value = param.id == kPresetParamId ? presetIndex : paramValue(params, param.id);
        plugin.parameterValues[param.id].store(value, std::memory_order_relaxed);
    }
}

void publishParams(Plugin& plugin, s3g::AmbiPulsarParams params, uint32_t presetIndex, bool requestProcess)
{
    params = s3g::sanitizeAmbiPulsarParams(params);
    presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
    plugin.params = params;
    storeParamBank(plugin, params, presetIndex);
    plugin.guiParamRevision.fetch_add(1u, std::memory_order_release);
    if (requestProcess && plugin.host && plugin.host->request_process) plugin.host->request_process(plugin.host);
}

void syncGuiParams(Plugin& plugin)
{
    plugin.params = paramsFromBank(plugin, plugin.params);
}

void syncAudioParams(Plugin& plugin)
{
    const uint64_t revision = plugin.guiParamRevision.load(std::memory_order_acquire);
    if (revision == plugin.audioParamRevision) return;
    plugin.audioParams = paramsFromBank(plugin, plugin.audioParams);
    plugin.audioParamRevision = revision;
    plugin.engine.setParams(plugin.audioParams);
}

void applyAudioParam(Plugin& plugin, clap_id id, double value)
{
    if (!findParam(id)) return;
    if (id == kPresetParamId) {
        const uint32_t preset = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u,
            s3g::kAmbiPulsarFactoryPresetCount - 1u);
        plugin.audioParams = s3g::ambiPulsarFactoryPreset(preset);
        storeParamBank(plugin, plugin.audioParams, preset);
        plugin.customPresetActive.store(false, std::memory_order_relaxed);
    } else {
        if (!assignParam(plugin.audioParams, id, value)) return;
        plugin.audioParams = s3g::sanitizeAmbiPulsarParams(plugin.audioParams);
        plugin.parameterValues[id].store(paramValue(plugin.audioParams, id), std::memory_order_relaxed);
    }
    plugin.engine.setParams(plugin.audioParams);
}

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(kParams)) return false;
    const auto& param = kParams[index];
    *info = {};
    info->id = param.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (param.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, param.name, sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    const char* module = param.id >= kNeuralSetParamId && param.id <= kLaneListeningParamId
        ? "Field Listening"
        : param.id >= kNeuralLevelParamId && param.id <= kNeuralCaptureParamId
        ? "Neural Synthesis" : param.id >= kLaneTuneModeIds[0] && param.id <= kLaneWindowSkewIds[2]
        ? "Pulsaret Lanes" : "Pulsar Encoder";
    std::strncpy(info->module, module, sizeof(info->module));
    info->module[sizeof(info->module) - 1u] = '\0';
    info->min_value = param.minimum;
    info->max_value = param.maximum;
    info->default_value = param.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !findParam(id)) return false;
    const auto* p = self(plugin);
    *value = p->parameterValues[id].load(std::memory_order_relaxed);
    return true;
}

constexpr const char* kWaveformNames[] { "SINE", "TRIANGLE", "SAW", "SQUARE", "OVERTONE", "FOLD", "IMPULSE", "NOISE" };
constexpr const char* kEnvelopeNames[] { "HANN", "TUKEY", "WELCH", "PERCUSSIVE", "REVERSE" };
constexpr const char* kQualityNames[] { "ECO", "HIGH", "ULTRA" };
constexpr const char* kMotionModeNames[] { "FREE", "ORBIT", "SWAY", "FIGURE 8", "FORSY" };
constexpr const char* kTuneModeNames[] { "HZ", "RATIO", "SUB" };
constexpr const char* kRetriggerModeNames[] { "RETRIGGER", "FREE", "IDLE ONLY" };
constexpr const char* kNeuralSetNames[] { "16 NODES", "32 NODES", "64 NODES" };
constexpr const char* kPickupSetNames[] { "TETRA 4", "CUBE 8" };
constexpr const char* kListeningModeNames[] { "LOCAL", "CROSS", "DIFFUSE", "ROAMING" };

template <size_t N>
bool isLaneParam(clap_id id, const std::array<clap_id, N>& ids)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t capacity)
{
    if (!display || capacity == 0u || !findParam(id)) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, capacity, "%s", s3g::ambiPulsarFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, capacity, "%uOA", static_cast<uint32_t>(std::lround(value)));
    } else if (id == kPointsParamId) {
        std::snprintf(display, capacity, "%u points", static_cast<uint32_t>(std::lround(value)));
    } else if (id == kEnvelopeParamId) {
        std::snprintf(display, capacity, "%s", kEnvelopeNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 4)]);
    } else if (id == kQualityParamId) {
        std::snprintf(display, capacity, "%s", kQualityNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 2)]);
    } else if (id == kMotionModeParamId) {
        std::snprintf(display, capacity, "%s", kMotionModeNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 4)]);
    } else if (id == kNeuralSetParamId) {
        std::snprintf(display, capacity, "%s", kNeuralSetNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 2)]);
    } else if (id == kPickupSetParamId) {
        std::snprintf(display, capacity, "%s", kPickupSetNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 1)]);
    } else if (id == kListeningModeParamId) {
        std::snprintf(display, capacity, "%s", kListeningModeNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 3)]);
    } else if (id == kListeningEnableParamId) {
        std::snprintf(display, capacity, "%s", value >= 0.5 ? "ON" : "OFF");
    } else if (id == kListeningBypassParamId) {
        std::snprintf(display, capacity, "%s", value >= 0.5 ? "BYPASSED" : "ACTIVE");
    } else if (id == kLaneWaveformIds[0] || id == kLaneWaveformIds[1] || id == kLaneWaveformIds[2]) {
        std::snprintf(display, capacity, "%s", kWaveformNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 7)]);
    } else if (isLaneParam(id, kLaneTuneModeIds)) {
        std::snprintf(display, capacity, "%s", kTuneModeNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 2)]);
    } else if (isLaneParam(id, kLaneRetriggerModeIds)) {
        std::snprintf(display, capacity, "%s", kRetriggerModeNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 2)]);
    } else if (isLaneParam(id, kLaneCarrierRatioIds) || isLaneParam(id, kLaneFmRatioIds)) {
        std::snprintf(display, capacity, "%.3g x", value);
    } else if (id == kEmissionParamId || id == kEmissionModRateParamId || id == kFormantModRateParamId
        || id == kLaneFormantIds[0] || id == kLaneFormantIds[1] || id == kLaneFormantIds[2]) {
        std::snprintf(display, capacity, "%.7g Hz", value);
    } else if (id == kFormantModDepthParamId || id == kFormantScatterParamId || id == kNeuralFmParamId) {
        std::snprintf(display, capacity, "%.2f st", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, capacity, "%+.1f deg", value);
    } else if (id == kDistanceParamId) {
        std::snprintf(display, capacity, "%.2f", value);
    } else if (id == kPropagationParamId) {
        std::snprintf(display, capacity, "%.1f ms", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, capacity, "%+.1f dB", value);
    } else if (id == kNeuralCaptureParamId) {
        std::snprintf(display, capacity, "capture %u", static_cast<uint32_t>(std::lround(value)));
    } else if (findParam(id)->stepped) {
        std::snprintf(display, capacity, "%d", static_cast<int>(std::lround(value)));
    } else {
        std::snprintf(display, capacity, "%.3f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value || !findParam(id)) return false;
    if (id == kPresetParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiPulsarFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiPulsarFactoryPresetInfo(index).name) == 0) {
                *value = index;
                return true;
            }
        }
    }
    if (id == kEnvelopeParamId) {
        for (uint32_t index = 0u; index < std::size(kEnvelopeNames); ++index) {
            if (std::strcmp(display, kEnvelopeNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kQualityParamId) {
        for (uint32_t index = 0u; index < std::size(kQualityNames); ++index) {
            if (std::strcmp(display, kQualityNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kMotionModeParamId) {
        for (uint32_t index = 0u; index < std::size(kMotionModeNames); ++index) {
            if (std::strcmp(display, kMotionModeNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kNeuralSetParamId) {
        for (uint32_t index = 0u; index < std::size(kNeuralSetNames); ++index) {
            if (std::strcmp(display, kNeuralSetNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kPickupSetParamId) {
        for (uint32_t index = 0u; index < std::size(kPickupSetNames); ++index) {
            if (std::strcmp(display, kPickupSetNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kListeningModeParamId) {
        for (uint32_t index = 0u; index < std::size(kListeningModeNames); ++index) {
            if (std::strcmp(display, kListeningModeNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kListeningEnableParamId && (std::strcmp(display, "ON") == 0 || std::strcmp(display, "OFF") == 0)) {
        *value = std::strcmp(display, "ON") == 0 ? 1.0 : 0.0;
        return true;
    }
    if (id == kListeningBypassParamId
        && (std::strcmp(display, "BYPASSED") == 0 || std::strcmp(display, "ACTIVE") == 0)) {
        *value = std::strcmp(display, "BYPASSED") == 0 ? 1.0 : 0.0;
        return true;
    }
    if (id == kLaneWaveformIds[0] || id == kLaneWaveformIds[1] || id == kLaneWaveformIds[2]) {
        for (uint32_t index = 0u; index < std::size(kWaveformNames); ++index) {
            if (std::strcmp(display, kWaveformNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (isLaneParam(id, kLaneTuneModeIds)) {
        for (uint32_t index = 0u; index < std::size(kTuneModeNames); ++index) {
            if (std::strcmp(display, kTuneModeNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (isLaneParam(id, kLaneRetriggerModeIds)) {
        for (uint32_t index = 0u; index < std::size(kRetriggerModeNames); ++index) {
            if (std::strcmp(display, kRetriggerModeNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kNeuralCaptureParamId && std::strncmp(display, "capture ", 8u) == 0) display += 8u;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display) return false;
    const auto* param = findParam(id);
    *value = std::clamp(parsed, param->minimum, param->maximum);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    auto* p = self(plugin);
    syncGuiParams(*p);
    SavedState state;
    state.params = p->params;
    state.presetIndex = p->presetIndex.load(std::memory_order_relaxed);
    state.guiViewMode = p->guiViewMode;
    state.guiViewZoom = p->guiViewZoom;
    if (p->customPresetActive.load(std::memory_order_relaxed)) {
        std::strncpy(state.customPresetName, p->customPresetName, sizeof(state.customPresetName) - 1u);
    }
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    p->customPresetName[0] = '\0';
    p->customPresetActive.store(false, std::memory_order_relaxed);
    if (version == 1u) {
        constexpr size_t kV1ParamsSize = offsetof(s3g::AmbiPulsarParams, neuralLevel);
        std::array<uint8_t, kV1ParamsSize> bytes {};
        uint32_t presetIndex = 0u;
        if (!readExact(stream, bytes.data(), bytes.size()) || !readExact(stream, &presetIndex, sizeof(presetIndex))) return false;
        s3g::AmbiPulsarParams upgraded {};
        std::memcpy(&upgraded, bytes.data(), bytes.size());
        p->params = s3g::sanitizeAmbiPulsarParams(upgraded);
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
    } else if (version == 2u) {
        constexpr size_t kV2ParamsSize = offsetof(s3g::AmbiPulsarParams, points);
        std::array<uint8_t, kV2ParamsSize> bytes {};
        uint32_t presetIndex = 0u;
        if (!readExact(stream, bytes.data(), bytes.size()) || !readExact(stream, &presetIndex, sizeof(presetIndex))) return false;
        s3g::AmbiPulsarParams upgraded {};
        std::memcpy(&upgraded, bytes.data(), bytes.size());
        p->params = s3g::sanitizeAmbiPulsarParams(upgraded);
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
    } else if (version == 3u) {
        constexpr size_t kV3ParamsSize = offsetof(s3g::AmbiPulsarParams, advancedLanes);
        std::array<uint8_t, kV3ParamsSize> bytes {};
        uint32_t presetIndex = 0u;
        int32_t guiViewMode = 2;
        float guiViewZoom = 1.0f;
        if (!readExact(stream, bytes.data(), bytes.size())
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, &guiViewMode, sizeof(guiViewMode))
            || !readExact(stream, &guiViewZoom, sizeof(guiViewZoom))) return false;
        s3g::AmbiPulsarParams upgraded {};
        std::memcpy(&upgraded, bytes.data(), bytes.size());
        p->params = s3g::sanitizeAmbiPulsarParams(upgraded);
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
        p->guiViewMode = std::clamp<int32_t>(guiViewMode, 0, 2);
        p->guiViewZoom = std::clamp(guiViewZoom, 0.55f, 2.20f);
    } else if (version == 4u) {
        constexpr size_t kV4ParamsSize = offsetof(s3g::AmbiPulsarParams, motionMode);
        std::array<uint8_t, kV4ParamsSize> bytes {};
        uint32_t presetIndex = 0u;
        int32_t guiViewMode = 2;
        float guiViewZoom = 1.0f;
        if (!readExact(stream, bytes.data(), bytes.size())
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, &guiViewMode, sizeof(guiViewMode))
            || !readExact(stream, &guiViewZoom, sizeof(guiViewZoom))) return false;
        s3g::AmbiPulsarParams upgraded {};
        std::memcpy(&upgraded, bytes.data(), bytes.size());
        p->params = s3g::sanitizeAmbiPulsarParams(upgraded);
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
        p->guiViewMode = std::clamp<int32_t>(guiViewMode, 0, 2);
        p->guiViewZoom = std::clamp(guiViewZoom, 0.55f, 2.20f);
    } else if (version == 5u || version == 6u) {
        s3g::AmbiPulsarParams upgraded {};
        uint32_t presetIndex = 0u;
        int32_t guiViewMode = 2;
        float guiViewZoom = 1.0f;
        if (!readExact(stream, &upgraded, kLegacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, &guiViewMode, sizeof(guiViewMode))
            || !readExact(stream, &guiViewZoom, sizeof(guiViewZoom))) return false;
        // Version 5 stored ROTATE/SWAY/FIGURE 8 as 0/1/2. FREE is inserted at
        // zero in version 6, so retain the exact legacy motion selection.
        if (version == 5u) {
            const uint32_t legacyMotion = std::min<uint32_t>(
                static_cast<uint32_t>(upgraded.motionMode), 2u);
            upgraded.motionMode = static_cast<s3g::AmbiPulsarMotionMode>(legacyMotion + 1u);
        }
        p->params = s3g::sanitizeAmbiPulsarParams(upgraded);
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
        p->guiViewMode = std::clamp<int32_t>(guiViewMode, 0, 2);
        p->guiViewZoom = std::clamp(guiViewZoom, 0.55f, 2.20f);
    } else if (version == 7u) {
        s3g::AmbiPulsarParams upgraded {};
        uint32_t presetIndex = 0u;
        int32_t guiViewMode = 2;
        float guiViewZoom = 1.0f;
        std::array<char, 64u> customPresetName {};
        if (!readExact(stream, &upgraded, kLegacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, &guiViewMode, sizeof(guiViewMode))
            || !readExact(stream, &guiViewZoom, sizeof(guiViewZoom))
            || !readExact(stream, customPresetName.data(), customPresetName.size())) return false;
        p->params = s3g::sanitizeAmbiPulsarParams(upgraded);
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
        p->guiViewMode = std::clamp<int32_t>(guiViewMode, 0, 2);
        p->guiViewZoom = std::clamp(guiViewZoom, 0.55f, 2.20f);
        std::strncpy(p->customPresetName, customPresetName.data(), sizeof(p->customPresetName) - 1u);
        p->customPresetName[sizeof(p->customPresetName) - 1u] = '\0';
        p->customPresetActive.store(p->customPresetName[0] != '\0', std::memory_order_relaxed);
    } else if (version == kStateVersion) {
        SavedState state;
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        p->params = s3g::sanitizeAmbiPulsarParams(state.params);
        p->presetIndex = std::min<uint32_t>(state.presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
        p->guiViewMode = std::clamp<int32_t>(state.guiViewMode, 0, 2);
        p->guiViewZoom = std::clamp(state.guiViewZoom, 0.55f, 2.20f);
        std::strncpy(p->customPresetName, state.customPresetName, sizeof(p->customPresetName) - 1u);
        p->customPresetName[sizeof(p->customPresetName) - 1u] = '\0';
        p->customPresetActive.store(p->customPresetName[0] != '\0', std::memory_order_relaxed);
    } else {
        return false;
    }
    publishParams(*p, p->params, p->presetIndex.load(std::memory_order_relaxed), false);
    p->audioResetRequest.fetch_add(1u, std::memory_order_release);
    if (p->host && p->host->request_process) p->host->request_process(p->host);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {

constexpr uint32_t kGuiWidth = 1160u;
constexpr uint32_t kGuiHeight = 858u;

struct GuiSliderSpec {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat y;
    CGFloat trackWidth;
};

constexpr GuiSliderSpec kGuiSliders[] {
    { kEmissionParamId, "EMISSION", 630.0, 104.0, 82.0 },
    { kEmissionModRateParamId, "EMIT RATE", 630.0, 130.0, 82.0 },
    { kEmissionModDepthParamId, "EMIT DEPTH", 630.0, 156.0, 82.0 },
    { kFormantModRateParamId, "FORM RATE", 630.0, 182.0, 82.0 },
    { kFormantModDepthParamId, "FORM DEPTH", 630.0, 208.0, 82.0 },
    { kFormantScatterParamId, "FORM SCAT", 630.0, 234.0, 82.0 },
    { kPhaseScatterParamId, "PHASE SCAT", 630.0, 260.0, 82.0 },
    { kOutputParamId, "OUTPUT", 630.0, 286.0, 82.0 },

    { kProbabilityParamId, "PROB", 630.0, 370.0, 82.0 },
    { kBurstOnParamId, "BURST ON", 630.0, 396.0, 82.0 },
    { kBurstOffParamId, "REST", 630.0, 422.0, 82.0 },
    { kSieveModuloParamId, "SIEVE MOD", 630.0, 448.0, 82.0 },
    { kSieveResidueParamId, "RESIDUE", 630.0, 474.0, 82.0 },
    { kDistributionParamId, "POINT RAND", 630.0, 500.0, 82.0 },
    { kEnvelopeEdgeParamId, "ENV EDGE", 630.0, 552.0, 82.0 },

    { kAzimuthParamId, "AZIMUTH", 630.0, 652.0, 82.0 },
    { kElevationParamId, "ELEVATION", 630.0, 678.0, 82.0 },
    { kDistanceParamId, "DISTANCE", 630.0, 704.0, 82.0 },
    { kWidthParamId, "WIDTH", 630.0, 730.0, 82.0 },
    { kSpatialScatterParamId, "SPAT SCAT", 630.0, 756.0, 82.0 },

    { kOrbitRateParamId, "ORBIT RATE", 896.0, 78.0, 82.0 },
    { kOrbitDepthParamId, "ORBIT DEPTH", 896.0, 104.0, 82.0 },
    { kSpatialFollowParamId, "INERTIA", 896.0, 130.0, 82.0 },
    { kAirParamId, "AIR", 896.0, 156.0, 82.0 },
    { kDopplerParamId, "DOPPLER", 896.0, 182.0, 82.0 },

    { kNeuralLevelParamId, "DIRECT", 896.0, 292.0, 82.0 },
    { kNeuralDriveParamId, "SIGMOID", 896.0, 318.0, 82.0 },
    { kNeuralFeedbackParamId, "RING FB", 896.0, 344.0, 82.0 },
    { kNeuralCouplingParamId, "MATRIX", 896.0, 370.0, 82.0 },
    { kNeuralHierarchyParamId, "HIERARCHY", 896.0, 396.0, 82.0 },
    { kNeuralPhaseParamId, "PHASE", 896.0, 422.0, 82.0 },
    { kNeuralBrownianParamId, "BROWNIAN", 896.0, 448.0, 82.0 },
    { kNeuralDriftParamId, "DRIFT", 896.0, 474.0, 82.0 },
    { kNeuralSelfModParamId, "SLOW > FAST", 896.0, 500.0, 82.0 },
    { kNeuralAudioFeedbackParamId, "AUDIO FB", 896.0, 526.0, 82.0 },

    { kNeuralPulsaretParamId, "PULSARET", 896.0, 610.0, 82.0 },
    { kNeuralEnvelopeParamId, "ENVELOPE", 896.0, 636.0, 82.0 },
    { kNeuralFmParamId, "FM DEPTH", 896.0, 662.0, 82.0 },

    { kFieldReturnParamId, "FIELD RETURN", 38.0, 540.0, 82.0 },
    { kPropagationParamId, "PROPAGATION", 38.0, 574.0, 82.0 },
    { kPickupFocusParamId, "FOCUS", 314.0, 540.0, 82.0 },
    { kLaneListeningParamId, "FIELD RESPONSE", 314.0, 574.0, 82.0 },

    { kLaneFormantIds[0], "CARRIER HZ", 18.0, 702.0, 82.0 },
    { kLaneCarrierRatioIds[0], "CLOCK RATIO", 18.0, 726.0, 82.0 },
    { kLaneOverlapIds[0], "OVERLAP", 18.0, 750.0, 82.0 },
    { kLaneLevelIds[0], "LEVEL", 18.0, 774.0, 82.0 },
    { kLaneOffsetIds[0], "OFFSET", 18.0, 798.0, 82.0 },
    { kLaneFmRatioIds[0], "FM RATIO", 314.0, 702.0, 82.0 },
    { kLaneFmIndexIds[0], "FM INDEX", 314.0, 726.0, 82.0 },
    { kLaneWindowSkewIds[0], "WIN SKEW", 314.0, 750.0, 82.0 },
    { kLaneFormantIds[1], "CARRIER HZ", 18.0, 702.0, 82.0 },
    { kLaneCarrierRatioIds[1], "CLOCK RATIO", 18.0, 726.0, 82.0 },
    { kLaneOverlapIds[1], "OVERLAP", 18.0, 750.0, 82.0 },
    { kLaneLevelIds[1], "LEVEL", 18.0, 774.0, 82.0 },
    { kLaneOffsetIds[1], "OFFSET", 18.0, 798.0, 82.0 },
    { kLaneFmRatioIds[1], "FM RATIO", 314.0, 702.0, 82.0 },
    { kLaneFmIndexIds[1], "FM INDEX", 314.0, 726.0, 82.0 },
    { kLaneWindowSkewIds[1], "WIN SKEW", 314.0, 750.0, 82.0 },
    { kLaneFormantIds[2], "CARRIER HZ", 18.0, 702.0, 82.0 },
    { kLaneCarrierRatioIds[2], "CLOCK RATIO", 18.0, 726.0, 82.0 },
    { kLaneOverlapIds[2], "OVERLAP", 18.0, 750.0, 82.0 },
    { kLaneLevelIds[2], "LEVEL", 18.0, 774.0, 82.0 },
    { kLaneOffsetIds[2], "OFFSET", 18.0, 798.0, 82.0 },
    { kLaneFmRatioIds[2], "FM RATIO", 314.0, 702.0, 82.0 },
    { kLaneFmIndexIds[2], "FM INDEX", 314.0, 726.0, 82.0 },
    { kLaneWindowSkewIds[2], "WIN SKEW", 314.0, 750.0, 82.0 },
};

bool listeningGuiParam(clap_id id)
{
    return id >= kNeuralSetParamId && id <= kLaneListeningParamId;
}

int guiLaneForParam(clap_id id)
{
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        if (id == kLaneFormantIds[lane] || id == kLaneCarrierRatioIds[lane]
            || id == kLaneOverlapIds[lane] || id == kLaneLevelIds[lane]
            || id == kLaneOffsetIds[lane] || id == kLaneFmRatioIds[lane]
            || id == kLaneFmIndexIds[lane] || id == kLaneWindowSkewIds[lane]
            || id == kLaneTuneModeIds[lane] || id == kLaneRetriggerModeIds[lane]
            || id == kLaneWaveformIds[lane]) return static_cast<int>(lane);
    }
    return -1;
}

const GuiSliderSpec* guiSlider(clap_id id)
{
    for (const auto& slider : kGuiSliders) if (slider.id == id) return &slider;
    return nullptr;
}

bool logarithmicParam(clap_id id)
{
    return id == kEmissionParamId || id == kEmissionModRateParamId || id == kFormantModRateParamId
        || id == kDistanceParamId || id == kLaneFormantIds[0] || id == kLaneFormantIds[1]
        || id == kLaneFormantIds[2] || id == kLaneOverlapIds[0] || id == kLaneOverlapIds[1]
        || id == kLaneOverlapIds[2] || isLaneParam(id, kLaneCarrierRatioIds)
        || isLaneParam(id, kLaneFmRatioIds);
}

CGFloat guiNormalize(clap_id id, double value)
{
    const auto* param = findParam(id);
    if (!param) return 0.0;
    if (logarithmicParam(id)) {
        const double low = std::log(std::max(1.0e-9, param->minimum));
        const double high = std::log(std::max(1.0e-9, param->maximum));
        return static_cast<CGFloat>((std::log(std::max(1.0e-9, value)) - low) / std::max(1.0e-9, high - low));
    }
    return static_cast<CGFloat>((value - param->minimum) / std::max(1.0e-9, param->maximum - param->minimum));
}

double guiValue(clap_id id, CGFloat normalized)
{
    const auto* param = findParam(id);
    if (!param) return 0.0;
    normalized = std::clamp(normalized, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
    double value = 0.0;
    if (logarithmicParam(id)) {
        value = std::exp(std::log(std::max(1.0e-9, param->minimum))
            + static_cast<double>(normalized) * (std::log(std::max(1.0e-9, param->maximum))
                - std::log(std::max(1.0e-9, param->minimum))));
    } else {
        value = param->minimum + static_cast<double>(normalized) * (param->maximum - param->minimum);
    }
    return param->stepped ? std::round(value) : value;
}

NSRect guiSliderHitRect(const GuiSliderSpec& slider)
{
    const CGFloat hitWidth = slider.panelX < 300.0 ? 280.0
        : slider.panelX < 620.0 ? 292.0 : slider.panelX < 890.0 ? 234.0 : 230.0;
    return NSMakeRect(slider.panelX + 8.0, slider.y - 8.0, hitWidth, 24.0);
}

float displayWave(s3g::AmbiPulsarWaveform waveform, float phase)
{
    phase -= std::floor(phase);
    switch (waveform) {
    case s3g::AmbiPulsarWaveform::Triangle: return 1.0f - 4.0f * std::fabs(phase - 0.5f);
    case s3g::AmbiPulsarWaveform::Saw: return phase * 2.0f - 1.0f;
    case s3g::AmbiPulsarWaveform::Square: return phase < 0.5f ? 1.0f : -1.0f;
    case s3g::AmbiPulsarWaveform::Overtone:
        return (std::sin(s3g::kAmbiPulsarTwoPi * phase)
            + 0.42f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 2.0f)
            + 0.22f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 3.0f)) / 1.64f;
    case s3g::AmbiPulsarWaveform::Fold:
        return std::tanh((std::sin(s3g::kAmbiPulsarTwoPi * phase)
            + 0.34f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 2.0f)) * 2.25f);
    case s3g::AmbiPulsarWaveform::Impulse: return std::exp(-phase * phase * 1200.0f);
    case s3g::AmbiPulsarWaveform::Noise:
        return std::sin(phase * 1927.0f) * std::sin(phase * 733.0f);
    case s3g::AmbiPulsarWaveform::Sine:
    default: return std::sin(s3g::kAmbiPulsarTwoPi * phase);
    }
}

} // namespace

@interface S3GAmbiPulsarEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
    uint32_t _selectedPoint;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    int _visualPage;
    uint32_t _selectedLane;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiPulsarEncoderView

- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
        _selectedPoint = 0u;
        _selectedLane = 0u;
        _visualPage = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _openMenuRect = NSZeroRect;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        if (_viewMode == 0) { _viewAzDeg = 0.0; _viewElDeg = 0.0; }
        else if (_viewMode == 1) { _viewAzDeg = 0.0; _viewElDeg = -90.0; }
        else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
                                               target:self
                                             selector:@selector(refresh:)
                                             userInfo:nil
                                              repeats:YES] retain];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    [_timer release];
    _timer = nil;
}

- (NSRect)presetRect { return NSMakeRect(382, 13, 190, 15); }
- (NSRect)savePresetRect { return NSMakeRect(580, 13, 50, 15); }
- (NSRect)loadPresetRect { return NSMakeRect(636, 13, 50, 15); }
- (NSRect)randomRect { return NSMakeRect(692, 13, 66, 15); }
- (NSRect)orderRect { return NSMakeRect(738, 77, 124, 15); }
- (NSRect)envelopeRect { return NSMakeRect(738, 525, 124, 15); }
- (NSRect)qualityRect { return NSMakeRect(738, 577, 124, 15); }
- (NSRect)pointsRect { return NSMakeRect(738, 781, 124, 15); }
- (NSRect)motionRect { return NSMakeRect(1004, 207, 124, 15); }
- (NSRect)captureHeaderRect { return NSMakeRect(896, 574, 246, 21); }
- (NSRect)captureRect { return NSMakeRect(1022, 578, 110, 13); }
- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 608); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 558); }
- (NSRect)lanePanelRect { return NSMakeRect(18, 666, 596, 180); }
- (NSRect)pageButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(panel.origin.x + 98.0 + static_cast<CGFloat>(index) * 74.0,
        panel.origin.y + 4.0, 70.0, 13.0);
}
- (NSRect)neuralSetRect { return NSMakeRect(112.0, 484.0, 104.0, 15.0); }
- (NSRect)pickupSetRect { return NSMakeRect(306.0, 484.0, 104.0, 15.0); }
- (NSRect)listeningModeRect { return NSMakeRect(488.0, 484.0, 104.0, 15.0); }
- (NSRect)listeningEnableRect { return NSMakeRect(46.0, 610.0, 118.0, 15.0); }
- (NSRect)listeningBypassRect { return NSMakeRect(172.0, 610.0, 118.0, 15.0); }
- (NSRect)laneButtonRect:(uint32_t)lane
{
    const NSRect panel = [self lanePanelRect];
    return NSMakeRect(panel.origin.x + 126.0 + static_cast<CGFloat>(lane) * 44.0,
        panel.origin.y + 4.0, 39.0, 13.0);
}
- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    const CGFloat width = 38.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(NSMaxX(panel) - 10.0 - (3.0 - index) * width - (2.0 - index) * gap,
        panel.origin.y + 4.0, width, 13.0);
}
- (NSRect)zoomButtonRect:(int)index
{
    const CGFloat width = 18.0;
    const CGFloat gap = 4.0;
    const CGFloat x = [self viewButtonRect:0].origin.x - 12.0
        - (2.0 - index) * width - (1.0 - index) * gap;
    return NSMakeRect(x, [self fieldPanelRect].origin.y + 4.0, width, 13.0);
}
- (NSRect)waveformRect:(uint32_t)lane
{
    (void)lane;
    return NSMakeRect(422.0, 822.0, 176.0, 15.0);
}
- (NSRect)tuneModeRect:(uint32_t)lane
{
    (void)lane;
    return NSMakeRect(422.0, 774.0, 176.0, 15.0);
}
- (NSRect)retriggerModeRect:(uint32_t)lane
{
    (void)lane;
    return NSMakeRect(422.0, 798.0, 176.0, 15.0);
}
- (NSRect)pulsaretLaneRect:(uint32_t)lane
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 14.0, field.origin.y + 38.0
        + static_cast<CGFloat>(lane) * 168.0, field.size.width - 28.0, 142.0);
}

- (NSString*)presetDisplayName
{
    if (_plugin->customPresetActive.load(std::memory_order_relaxed) && _plugin->customPresetName[0]) {
        return [NSString stringWithUTF8String:_plugin->customPresetName];
    }
    const uint32_t preset = std::min<uint32_t>(
        _plugin->presetIndex.load(std::memory_order_relaxed), s3g::kAmbiPulsarFactoryPresetCount - 1u);
    return [NSString stringWithUTF8String:s3g::ambiPulsarFactoryPresetInfo(preset).name];
}

- (NSString*)customPresetDirectory
{
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Pulsar Encoder"];
}

- (void)saveCustomPreset
{
    if (!_plugin) return;
    syncGuiParams(*_plugin);
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gpulsar" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3gpulsar", [self presetDisplayName]]];
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* name = [[[[panel URL] lastPathComponent] stringByDeletingPathExtension] copy];
    if (saveCustomPresetFile([[[panel URL] path] UTF8String], *_plugin, [name UTF8String])) {
        std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s", [name UTF8String]);
        _plugin->customPresetActive.store(true, std::memory_order_relaxed);
    }
    [name release];
    [self setNeedsDisplay:YES];
}

- (void)loadCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES attributes:nil error:nil];
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gpulsar" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return;
    syncGuiParams(*_plugin);
    // Capture tables are runtime material, not portable preset data. Preserve
    // the current action serial so loading a preset does not request an
    // unrelated recapture; the captured mix and neural controls are restored.
    file.params.neuralCapture = _plugin->params.neuralCapture;
    std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s",
        file.name[0] ? file.name : "Custom");
    _plugin->customPresetActive.store(true, std::memory_order_relaxed);
    publishParams(*_plugin, file.params, 0u, true);
    [self setNeedsDisplay:YES];
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return [self presetRect];
    case 2: return [self orderRect];
    case 3: return [self envelopeRect];
    case 4: return [self qualityRect];
    case 5: return [self pointsRect];
    case 6: return [self tuneModeRect:_selectedLane];
    case 7: return [self retriggerModeRect:_selectedLane];
    case 8: return [self waveformRect:_selectedLane];
    case 9: return [self motionRect];
    case 10: return [self neuralSetRect];
    case 11: return [self pickupSetRect];
    case 12: return [self listeningModeRect];
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return s3g::kAmbiPulsarFactoryPresetCount;
    case 2: return 7u;
    case 3: return 5u;
    case 4: return 3u;
    case 5: return 29u;
    case 6: return 3u;
    case 7: return 3u;
    case 8: return 8u;
    case 9: return 5u;
    case 10: return 3u;
    case 11: return 2u;
    case 12: return 4u;
    default: return 0u;
    }
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _menuItemCount = [self menuCount:menu];
    _hoverMenuItem = -1;
    const NSRect box = [self menuBoxRect:menu];
    const CGFloat itemHeight = 21.0;
    const CGFloat height = itemHeight * _menuItemCount;
    CGFloat y = NSMaxY(box) + 2.0;
    if (y + height > kGuiHeight - 8.0) y = box.origin.y - height - 2.0;
    _openMenuRect = NSMakeRect(box.origin.x, y, box.size.width, height);
    [self setNeedsDisplay:YES];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* envelopeItems[] = { @"HANN", @"TUKEY", @"WELCH", @"PERCUSSIVE", @"REVERSE" };
    static NSString* qualityItems[] = { @"ECO", @"HIGH", @"ULTRA" };
    static NSString* pointItems[] = { @"4", @"5", @"6", @"7", @"8", @"9", @"10",
        @"11", @"12", @"13", @"14", @"15", @"16", @"17", @"18", @"19", @"20",
        @"21", @"22", @"23", @"24", @"25", @"26", @"27", @"28", @"29", @"30",
        @"31", @"32" };
    static NSString* tuneItems[] = { @"HZ", @"RATIO", @"SUB" };
    static NSString* triggerItems[] = { @"RETRIGGER", @"FREE", @"IDLE ONLY" };
    static NSString* waveformItems[] = { @"SINE", @"TRIANGLE", @"SAW", @"SQUARE",
        @"OVERTONE", @"FOLD", @"IMPULSE", @"NOISE" };
    static NSString* motionItems[] = { @"FREE", @"ORBIT", @"SWAY", @"FIGURE 8", @"FORSY" };
    static NSString* neuralSetItems[] = { @"16 NODES", @"32 NODES", @"64 NODES" };
    static NSString* pickupSetItems[] = { @"TETRA 4", @"CUBE 8" };
    static NSString* listeningModeItems[] = { @"LOCAL", @"CROSS", @"DIFFUSE", @"ROAMING" };
    static NSString* presetItems[s3g::kAmbiPulsarFactoryPresetCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t index = 0u; index < s3g::kAmbiPulsarFactoryPresetCount; ++index) {
            presetItems[index] = [[NSString stringWithUTF8String:
                s3g::ambiPulsarFactoryPresetInfo(index).name] retain];
        }
    });

    NSString** items = presetItems;
    int selected = static_cast<int>(_plugin->presetIndex.load(std::memory_order_relaxed));
    if (_openMenu == 2) { items = orderItems; selected = static_cast<int>(_plugin->params.order) - 1; }
    else if (_openMenu == 3) { items = envelopeItems; selected = static_cast<int>(_plugin->params.envelope); }
    else if (_openMenu == 4) { items = qualityItems; selected = static_cast<int>(_plugin->params.quality); }
    else if (_openMenu == 5) { items = pointItems; selected = static_cast<int>(_plugin->params.points) - 4; }
    else if (_openMenu == 6) {
        items = tuneItems;
        selected = static_cast<int>(_plugin->params.advancedLanes[_selectedLane].tuneMode);
    } else if (_openMenu == 7) {
        items = triggerItems;
        selected = static_cast<int>(_plugin->params.advancedLanes[_selectedLane].retriggerMode);
    } else if (_openMenu == 8) {
        items = waveformItems;
        selected = static_cast<int>(_plugin->params.lanes[_selectedLane].waveform);
    } else if (_openMenu == 9) {
        items = motionItems;
        selected = static_cast<int>(_plugin->params.motionMode);
    } else if (_openMenu == 10) {
        items = neuralSetItems;
        selected = static_cast<int>(_plugin->params.listening.neuralSet);
    } else if (_openMenu == 11) {
        items = pickupSetItems;
        selected = static_cast<int>(_plugin->params.listening.pickupSet);
    } else if (_openMenu == 12) {
        items = listeningModeItems;
        selected = static_cast<int>(_plugin->params.listening.mode);
    }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount,
        selected, _hoverMenuItem, attrs, style);
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
    _plugin->guiSelectedPoint.store(_selectedPoint, std::memory_order_relaxed);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = std::clamp(mode, 0, 2);
    if (_viewMode == 0) { _viewAzDeg = 0.0; _viewElDeg = 0.0; }
    else if (_viewMode == 1) { _viewAzDeg = 0.0; _viewElDeg = -90.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (s3g::Vec3)pointWorld:(uint32_t)index
{
    const float azimuth = _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    const float elevation = _plugin->guiElevation[index].load(std::memory_order_relaxed);
    const float distance = _plugin->guiDistance[index].load(std::memory_order_relaxed);
    const float referenceDistance = std::max(0.10f, _plugin->params.centerDistance);
    const float displayDistance = std::clamp(distance / referenceDistance, 0.42f, 1.48f);
    const s3g::Vec3 direction = s3g::directionFromAed(azimuth, elevation);
    return { direction.x * displayDistance, direction.y * displayDistance, direction.z * displayDistance };
}

- (NSPoint)projectPoint:(uint32_t)index depth:(CGFloat*)depth
{
    const NSRect field = [self fieldRect];
    const s3g::Vec3 point = [self pointWorld:index];
    const CGFloat scale = std::min(field.size.width, field.size.height) * 0.36
        * std::clamp(_viewZoom, static_cast<CGFloat>(0.55), static_cast<CGFloat>(2.20));
    const float azimuth = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 - se * point.z;
    const float z2 = se * y1 + ce * point.z;
    if (depth) *depth = z2;
    return NSMakePoint(NSMidX(field) + x1 * scale, NSMidY(field) - y2 * scale);
}

- (NSColor*)pointColor:(uint32_t)index selected:(BOOL)selected
{
    const float azimuth = _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    const float elevation = _plugin->guiElevation[index].load(std::memory_order_relaxed);
    const float hue = std::fmod((azimuth + 180.0f) / 360.0f + 0.08f, 1.0f);
    const float saturation = selected ? 0.72f : 0.52f;
    const float brightness = selected ? 0.96f : 0.72f + std::max(0.0f, elevation) / 90.0f * 0.18f;
    return [NSColor colorWithCalibratedHue:hue saturation:saturation brightness:brightness
        alpha:selected ? 1.0 : 0.84];
}

- (void)drawMenuLabel:(NSString*)label value:(NSString*)value rect:(NSRect)rect style:(const s3g::clap_gui::Style&)style
{
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [label drawAtPoint:NSMakePoint(rect.origin.x - 92.0, rect.origin.y + 1.0) withAttributes:attrs];
    [style.strip setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    [style.fill setFill];
    NSRectFill(NSMakeRect(rect.origin.x + 1.0, rect.origin.y + 1.0, 2.0, rect.size.height - 2.0));
    [value drawAtPoint:NSMakePoint(rect.origin.x + 8.0, rect.origin.y + 2.0) withAttributes:values];
    [@"v" drawAtPoint:NSMakePoint(NSMaxX(rect) - 13.0, rect.origin.y + 1.0) withAttributes:values];
}

- (void)drawSlider:(const GuiSliderSpec&)slider style:(const s3g::clap_gui::Style&)style
{
    char text[64] {};
    const double value = paramValue(_plugin->params, slider.id);
    paramsValueToText(nullptr, slider.id, value, text, sizeof(text));
    NSString* label = [NSString stringWithUTF8String:slider.label];
    NSString* valueText = [NSString stringWithUTF8String:text];
    s3g::clap_gui::drawSlider(label, valueText, guiNormalize(slider.id, value), slider.y,
        s3g::clap_gui::softLabelAttrs(), s3g::clap_gui::softValueAttrs(), style,
        slider.panelX + 16.0, slider.panelX + 108.0, slider.panelX + 196.0, slider.trackWidth);
}

- (void)drawPulsaret:(uint32_t)lane rect:(NSRect)rect style:(const s3g::clap_gui::Style&)style
{
    [style.strip setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    [s3g::clap_gui::color(0x303030) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMinX(rect) + 1.0, NSMidY(rect))
        toPoint:NSMakePoint(NSMaxX(rect) - 1.0, NSMidY(rect))];
    NSBezierPath* path = [NSBezierPath bezierPath];
    NSBezierPath* windowPath = [NSBezierPath bezierPath];
    const auto waveform = _plugin->params.lanes[lane].waveform;
    const auto& advanced = _plugin->params.advancedLanes[lane];
    const float skew = advanced.windowSkew;
    constexpr uint32_t points = 192u;
    for (uint32_t index = 0u; index < points; ++index) {
        const float phase = static_cast<float>(index) / static_cast<float>(points - 1u);
        const float warped = phase < skew ? 0.5f * phase / skew
            : 0.5f + 0.5f * (phase - skew) / (1.0f - skew);
        const float window = 0.5f - 0.5f * std::cos(s3g::kAmbiPulsarTwoPi * warped);
        const float fmOffset = advanced.fmIndex
            * std::sin(s3g::kAmbiPulsarTwoPi * phase * advanced.fmRatio)
            / s3g::kAmbiPulsarTwoPi;
        const float value = displayWave(waveform, phase + fmOffset) * window;
        const NSPoint point = NSMakePoint(rect.origin.x + phase * rect.size.width,
            NSMidY(rect) - value * rect.size.height * 0.40);
        const NSPoint windowPoint = NSMakePoint(rect.origin.x + phase * rect.size.width,
            NSMaxY(rect) - 4.0 - window * (rect.size.height - 8.0));
        index == 0u ? [path moveToPoint:point] : [path lineToPoint:point];
        index == 0u ? [windowPath moveToPoint:windowPoint] : [windowPath lineToPoint:windowPoint];
    }
    [s3g::clap_gui::color(0x666666, 0.52) setStroke];
    [windowPath setLineWidth:0.75];
    [windowPath stroke];
    [style.accent setStroke];
    [path setLineWidth:1.15];
    [path stroke];
}

- (void)drawPointField:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1.0, 1.0)] addClip];
    const CGFloat sphereRadius = std::min(field.size.width, field.size.height) * 0.36 * _viewZoom;
    [s3g::clap_gui::color(0x303030) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        NSMidX(field) - sphereRadius, NSMidY(field) - sphereRadius,
        sphereRadius * 2.0, sphereRadius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];
    [s3g::clap_gui::color(0x242424) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMinX(field) + 18.0, NSMidY(field))
        toPoint:NSMakePoint(NSMaxX(field) - 18.0, NSMidY(field))];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(field), NSMinY(field) + 18.0)
        toPoint:NSMakePoint(NSMidX(field), NSMaxY(field) - 18.0)];

    const uint32_t points = std::clamp<uint32_t>(_plugin->params.points,
        s3g::kAmbiPulsarMinPoints, s3g::kAmbiPulsarMaxPoints);
    _selectedPoint = std::min<uint32_t>(_selectedPoint, points - 1u);
    _plugin->guiSelectedPoint.store(_selectedPoint, std::memory_order_relaxed);
    std::array<NSPoint, s3g::kAmbiPulsarMaxPoints> projected {};
    for (uint32_t index = 0u; index < points; ++index) {
        projected[index] = [self projectPoint:index depth:nullptr];
    }
    [s3g::clap_gui::color(0x5c5c5c, 0.22) setStroke];
    for (uint32_t index = 0u; index < points; ++index) {
        NSBezierPath* link = [NSBezierPath bezierPath];
        [link moveToPoint:projected[index]];
        [link lineToPoint:projected[(index + 1u) % points]];
        [link setLineWidth:0.55];
        [link stroke];
    }

    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x080808), 6.5);
    for (uint32_t index = 0u; index < points; ++index) {
        const BOOL selected = index == _selectedPoint;
        const float energy = _plugin->guiEnergy[index].load(std::memory_order_relaxed);
        const float event = std::clamp(_plugin->guiEvent[index].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float activity = std::clamp(std::sqrt(std::max(0.0f, energy)) * 18.0f, 0.0f, 1.0f);
        const CGFloat size = (selected ? 14.0 : 10.0) + activity * 5.0 + event * 7.0;
        const NSRect marker = NSMakeRect(projected[index].x - size * 0.5,
            projected[index].y - size * 0.5, size, size);
        NSColor* pointColor = [self pointColor:index selected:selected];
        if (event > 0.04f || activity > 0.04f) {
            const CGFloat halo = size * (1.15 + event * 1.9 + activity * 0.6);
            const NSRect haloRect = NSMakeRect(projected[index].x - halo * 0.5,
                projected[index].y - halo * 0.5, halo, halo);
            [[pointColor colorWithAlphaComponent:0.05 + event * 0.18 + activity * 0.08] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:haloRect] fill];
        }
        [[pointColor colorWithAlphaComponent:selected ? 0.98 : 0.22 + event * 0.54 + activity * 0.20] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe6e6e6 : 0x4f4f4f,
            selected ? 1.0 : 0.22 + event * 0.46 + activity * 0.18) setStroke];
        NSFrameRect(marker);
        NSString* pointLabel = [NSString stringWithFormat:@"%u", index + 1u];
        const NSSize labelSize = [pointLabel sizeWithAttributes:idAttrs];
        [pointLabel drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5,
            NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
    }
    [NSGraphicsContext restoreGraphicsState];

    const float azimuth = _plugin->guiAzimuth[_selectedPoint].load(std::memory_order_relaxed);
    const float elevation = _plugin->guiElevation[_selectedPoint].load(std::memory_order_relaxed);
    const float distance = _plugin->guiDistance[_selectedPoint].load(std::memory_order_relaxed);
    const float energy = _plugin->guiEnergy[_selectedPoint].load(std::memory_order_relaxed);
    NSString* readout = [NSString stringWithFormat:@"P%02u  AZ%+.1f  EL%+.1f  D%.2f  E%.3f",
        _selectedPoint + 1u, azimuth, elevation, distance, energy];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7.0, valueAttrs, 8.0);
    [@"A+B+C PULSARET OBJECTS     DIRECT ACN/SN3D" drawAtPoint:NSMakePoint(field.origin.x + 9.0,
        NSMaxY(field) - 19.0) withAttributes:valueAttrs];
}

- (void)drawNeuralNetwork:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [@"FOUR LOBES / SIXTEEN SIGNED R-C RINGS" drawAtPoint:NSMakePoint(field.origin.x + 12.0,
        field.origin.y + 12.0) withAttributes:s3g::clap_gui::softValueAttrs()];

    const float progress = _plugin->guiCaptureProgress.load(std::memory_order_relaxed);
    const uint32_t generation = _plugin->guiCaptureGeneration.load(std::memory_order_relaxed);
    const uint32_t activeLobes = _plugin->params.listening.neuralSet == s3g::AmbiPulsarNeuralSet::Nodes64
        ? 4u : _plugin->params.listening.neuralSet == s3g::AmbiPulsarNeuralSet::Nodes32 ? 2u : 1u;
    NSString* state = [NSString stringWithFormat:@"%u NEURONS / %@ %02u",
        activeLobes * s3g::kNeuralSynthesisNodes,
        generation > 0u ? @"TABLE" : @"BUFFER",
        generation > 0u ? generation : static_cast<uint32_t>(progress * 100.0f)];
    s3g::clap_gui::drawRightStatus(state, NSMaxX(field), field.origin.y + 12.0,
        s3g::clap_gui::softValueAttrs(), 12.0);

    static NSString* timeLabels[] = { @"80", @"32", @"9.5", @"2" };
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiPulsarNeuralLobes; ++lobe) {
        const uint32_t column = lobe % 2u;
        const uint32_t row = lobe / 2u;
        const NSRect lobeBox = NSMakeRect(field.origin.x + 10.0 + column * 272.0,
            field.origin.y + 45.0 + row * 224.0, 264.0, 210.0);
        const BOOL active = lobe < activeLobes;
        [s3g::clap_gui::color(active ? 0x121212 : 0x0d0d0d) setFill];
        NSRectFill(lobeBox);
        [s3g::clap_gui::color(active ? 0x5b5b5b : 0x292929) setStroke];
        NSFrameRect(lobeBox);
        [[NSString stringWithFormat:@"LOBE %c / %@", static_cast<char>('A' + lobe),
            active ? @"ACTIVE" : @"STANDBY"]
            drawAtPoint:NSMakePoint(lobeBox.origin.x + 8.0, lobeBox.origin.y + 7.0)
            withAttributes:s3g::clap_gui::softLabelAttrs()];

        for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
            const uint32_t clusterColumn = cluster % 2u;
            const uint32_t clusterRow = cluster / 2u;
            const NSPoint center = NSMakePoint(lobeBox.origin.x + 67.0 + clusterColumn * 130.0,
                lobeBox.origin.y + 70.0 + clusterRow * 91.0);
            std::array<NSPoint, s3g::kNeuralNodesPerCluster> points {{
                NSMakePoint(center.x - 20.0, center.y - 20.0),
                NSMakePoint(center.x + 20.0, center.y - 20.0),
                NSMakePoint(center.x + 20.0, center.y + 20.0),
                NSMakePoint(center.x - 20.0, center.y + 20.0),
            }};
            [[NSString stringWithFormat:@"C%u / %@ MS", cluster + 1u, timeLabels[cluster]]
                drawAtPoint:NSMakePoint(center.x - 42.0, center.y - 39.0)
                withAttributes:s3g::clap_gui::softValueAttrs()];
        NSBezierPath* ring = [NSBezierPath bezierPath];
        [ring moveToPoint:points[0]];
        for (uint32_t local = 1u; local < points.size(); ++local) [ring lineToPoint:points[local]];
        [ring closePath];
            [s3g::clap_gui::color(active ? 0x686868 : 0x303030, 0.72) setStroke];
            [ring setLineWidth:0.8];
        [ring stroke];

        for (uint32_t local = 0u; local < s3g::kNeuralNodesPerCluster; ++local) {
                const uint32_t node = lobe * s3g::kNeuralSynthesisNodes
                    + cluster * s3g::kNeuralNodesPerCluster + local;
            const float value = _plugin->guiNeuralNode[node].load(std::memory_order_relaxed);
                const CGFloat radius = active ? 3.5 + std::fabs(value) * 5.5 : 3.0;
                const int shade = active
                    ? static_cast<int>(std::clamp(108.0f + value * 116.0f, 20.0f, 238.0f)) : 38;
                [s3g::clap_gui::color((shade << 16) | (shade << 8) | shade, active ? 0.94 : 0.5) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(points[local].x - radius,
                points[local].y - radius, radius * 2.0, radius * 2.0)] fill];
                [s3g::clap_gui::color(value >= 0.0f ? 0xd8d8d8 : 0x777777, active ? 0.74 : 0.24) setStroke];
                [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(points[local].x - radius,
                    points[local].y - radius, radius * 2.0, radius * 2.0)] stroke];
            }
        }
    }

    [@"BROWNIAN + DRIFT + LOBE MATRIX + DIRECTIONAL FIELD RETURN" drawAtPoint:NSMakePoint(
        field.origin.x + 12.0, NSMaxY(field) - 27.0) withAttributes:s3g::clap_gui::softValueAttrs()];
    (void)style;
}

- (void)drawPulsaretPage:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    static constexpr const char* laneNames[] { "LANE A", "LANE B", "LANE C" };
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        const NSRect row = [self pulsaretLaneRect:lane];
        [s3g::clap_gui::color(lane == _selectedLane ? 0x181818 : 0x101010) setFill];
        NSRectFill(row);
        [s3g::clap_gui::color(lane == _selectedLane ? 0xb0b0b0 : 0x3b3b3b) setStroke];
        NSFrameRect(row);
        const auto& laneParams = _plugin->params.lanes[lane];
        const auto& advanced = _plugin->params.advancedLanes[lane];
        NSString* title = [NSString stringWithFormat:@"%s  %s / %s / %s", laneNames[lane],
            kWaveformNames[static_cast<uint32_t>(laneParams.waveform)],
            kTuneModeNames[static_cast<uint32_t>(advanced.tuneMode)],
            kRetriggerModeNames[static_cast<uint32_t>(advanced.retriggerMode)]];
        [title drawAtPoint:NSMakePoint(row.origin.x + 10.0, row.origin.y + 8.0)
            withAttributes:s3g::clap_gui::softLabelAttrs()];
        NSString* detail = [NSString stringWithFormat:@"%.1f Hz   CLK %.3gx   FM %.3gx / %.2f   WIN %.2f   N%02.0f",
            laneParams.formantHz, advanced.carrierRatio, advanced.fmRatio, advanced.fmIndex,
            advanced.windowSkew, _plugin->params.neuralPulsaretMix * 100.0f];
        s3g::clap_gui::drawRightStatus(detail, NSMaxX(row), row.origin.y + 8.0,
            s3g::clap_gui::softValueAttrs(), 10.0);
        [self drawPulsaret:lane rect:NSMakeRect(row.origin.x + 10.0, row.origin.y + 31.0,
            row.size.width - 20.0, row.size.height - 42.0) style:style];
    }
}

- (void)drawListeningPage:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [@"AMBISONIC AUDITORY BODY / PREVIOUS-SAMPLE RETURN" drawAtPoint:NSMakePoint(
        field.origin.x + 10.0, field.origin.y + 10.0) withAttributes:values];

    const uint32_t pickupCount = _plugin->params.listening.pickupSet
        == s3g::AmbiPulsarPickupSet::Cube8 ? 8u : 4u;
    constexpr float inverseRootThree = 0.5773502691896258f;
    static constexpr std::array<s3g::Vec3, s3g::kAmbiPulsarListeningPickups> directions {{
        {  inverseRootThree,  inverseRootThree,  inverseRootThree },
        { -inverseRootThree, -inverseRootThree,  inverseRootThree },
        { -inverseRootThree,  inverseRootThree, -inverseRootThree },
        {  inverseRootThree, -inverseRootThree, -inverseRootThree },
        { -inverseRootThree, -inverseRootThree, -inverseRootThree },
        {  inverseRootThree,  inverseRootThree, -inverseRootThree },
        {  inverseRootThree, -inverseRootThree,  inverseRootThree },
        { -inverseRootThree,  inverseRootThree,  inverseRootThree },
    }};
    const NSPoint center = NSMakePoint(NSMidX(field), field.origin.y + 215.0);
    const CGFloat radius = 148.0;
    [s3g::clap_gui::color(0x303030) setStroke];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(center.x - radius, center.y - radius,
        radius * 2.0, radius * 2.0)] stroke];

    std::array<NSPoint, s3g::kAmbiPulsarListeningPickups> ears {};
    const float cameraAz = static_cast<float>(38.0 * s3g::kPi / 180.0);
    const float cameraEl = static_cast<float>(32.0 * s3g::kPi / 180.0);
    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
        const auto direction = directions[pickup];
        const float x1 = std::cos(cameraAz) * direction.x - std::sin(cameraAz) * direction.y;
        const float y1 = std::sin(cameraAz) * direction.x + std::cos(cameraAz) * direction.y;
        const float y2 = std::cos(cameraEl) * y1 - std::sin(cameraEl) * direction.z;
        ears[pickup] = NSMakePoint(center.x + x1 * radius, center.y - y2 * radius);
    }
    std::array<NSPoint, s3g::kAmbiPulsarNeuralLobes> lobes {{
        NSMakePoint(center.x - 40.0, center.y - 26.0),
        NSMakePoint(center.x + 40.0, center.y - 26.0),
        NSMakePoint(center.x + 40.0, center.y + 26.0),
        NSMakePoint(center.x - 40.0, center.y + 26.0),
    }};

    [s3g::clap_gui::color(0x707070, 0.38) setStroke];
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiPulsarNeuralLobes; ++lobe) {
        uint32_t pickup = lobe % pickupCount;
        if (_plugin->params.listening.mode == s3g::AmbiPulsarListeningMode::Cross) {
            pickup = (lobe + 1u) % pickupCount;
        } else if (_plugin->params.listening.mode == s3g::AmbiPulsarListeningMode::Roaming) {
            pickup = (lobe + static_cast<uint32_t>(
                [NSDate timeIntervalSinceReferenceDate] * 0.037 * pickupCount)) % pickupCount;
        }
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:ears[pickup]];
        [path lineToPoint:lobes[lobe]];
        [path setLineWidth:0.7 + std::fabs(
            _plugin->guiListeningReturn[lobe].load(std::memory_order_relaxed)) * 2.2];
        [path stroke];
    }

    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
        const float energy = std::clamp(
            _plugin->guiListeningEnergy[pickup].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const CGFloat size = 8.0 + std::sqrt(energy) * 18.0;
        [[s3g::clap_gui::color(0xc8c8c8, 0.04 + energy * 0.20)
            colorWithAlphaComponent:0.04 + energy * 0.20] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(ears[pickup].x - size,
            ears[pickup].y - size, size * 2.0, size * 2.0)] fill];
        NSBezierPath* diamond = [NSBezierPath bezierPath];
        [diamond moveToPoint:NSMakePoint(ears[pickup].x, ears[pickup].y - 6.0)];
        [diamond lineToPoint:NSMakePoint(ears[pickup].x + 6.0, ears[pickup].y)];
        [diamond lineToPoint:NSMakePoint(ears[pickup].x, ears[pickup].y + 6.0)];
        [diamond lineToPoint:NSMakePoint(ears[pickup].x - 6.0, ears[pickup].y)];
        [diamond closePath];
        [s3g::clap_gui::color(0x9a9a9a, 0.35 + energy * 0.65) setFill];
        [diamond fill];
        [[NSString stringWithFormat:@"E%u", pickup + 1u] drawAtPoint:NSMakePoint(
            ears[pickup].x + 8.0, ears[pickup].y - 6.0) withAttributes:values];
    }
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiPulsarNeuralLobes; ++lobe) {
        const float value = _plugin->guiListeningReturn[lobe].load(std::memory_order_relaxed);
        const int shade = static_cast<int>(std::clamp(118.0f + value * 116.0f, 22.0f, 234.0f));
        [s3g::clap_gui::color((shade << 16) | (shade << 8) | shade, 0.94) setFill];
        NSRectFill(NSMakeRect(lobes[lobe].x - 10.0, lobes[lobe].y - 10.0, 20.0, 20.0));
        [[NSString stringWithFormat:@"%c", static_cast<char>('A' + lobe)] drawAtPoint:NSMakePoint(
            lobes[lobe].x - 3.0, lobes[lobe].y - 7.0)
            withAttributes:s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x080808), 8.0)];
    }

    [self drawMenuLabel:@"NODES" value:[NSString stringWithUTF8String:
        kNeuralSetNames[static_cast<uint32_t>(_plugin->params.listening.neuralSet)]]
        rect:[self neuralSetRect] style:style];
    [self drawMenuLabel:@"EARS" value:[NSString stringWithUTF8String:
        kPickupSetNames[static_cast<uint32_t>(_plugin->params.listening.pickupSet)]]
        rect:[self pickupSetRect] style:style];
    [self drawMenuLabel:@"LISTENING" value:[NSString stringWithUTF8String:
        kListeningModeNames[static_cast<uint32_t>(_plugin->params.listening.mode)]]
        rect:[self listeningModeRect] style:style];
    for (const auto& slider : kGuiSliders) {
        if (listeningGuiParam(slider.id)) [self drawSlider:slider style:style];
    }
    const NSRect controlHeader = NSMakeRect(field.origin.x, 604.0, field.size.width, 27.0);
    s3g::clap_gui::drawHeaderButton([self listeningEnableRect], controlHeader,
        _plugin->params.listening.enabled ? @"LISTEN ON" : @"LISTEN OFF",
        _plugin->params.listening.enabled != 0u, labels, style);
    s3g::clap_gui::drawHeaderButton([self listeningBypassRect], controlHeader,
        _plugin->params.listening.bypass ? @"BYPASSED" : @"RETURN ACTIVE",
        _plugin->params.listening.bypass != 0u, labels, style);
    NSString* status = [NSString stringWithFormat:@"EARS %3.0f%%  RETURN %3.0f%%",
        _plugin->guiListeningAnalysis.load(std::memory_order_relaxed) * 100.0f,
        _plugin->guiListeningInfluence.load(std::memory_order_relaxed) * 100.0f];
    s3g::clap_gui::drawRightStatus(status, NSMaxX(field) - 4.0, 610.0, values, 0.0);
}

- (void)drawVisualPanel:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21.0);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PULSAR VISUAL", true, panel.origin.x, panel.origin.y,
        panel.size.width, 21.0, attrs, style);
    static NSString* pageNames[] = { @"FIELD", @"PULSARETS", @"NEURAL", @"LISTEN" };
    for (int index = 0; index < 4; ++index) {
        s3g::clap_gui::drawHeaderButton([self pageButtonRect:index], header, pageNames[index],
            index == _visualPage, attrs, style);
    }
    if (_visualPage == 0) {
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
        static NSString* viewNames[] = { @"TOP", @"SIDE", @"3/4" };
        for (int index = 0; index < 3; ++index) {
            s3g::clap_gui::drawHeaderButton([self viewButtonRect:index], header, viewNames[index],
                index == _viewMode, attrs, style);
        }
        [self drawPointField:style];
    } else if (_visualPage == 1) {
        [self drawPulsaretPage:style];
    } else if (_visualPage == 2) {
        [self drawNeuralNetwork:style];
    } else {
        [self drawListeningPage:style];
    }
}

- (void)drawCaptureState:(const s3g::clap_gui::Style&)style
{
    const float captureProgress = _plugin->guiCaptureProgress.load(std::memory_order_relaxed);
    const uint32_t captureGeneration = _plugin->guiCaptureGeneration.load(std::memory_order_relaxed);
    const BOOL capturePending = _plugin->guiCaptureRequestSerial.load(std::memory_order_acquire)
        != _plugin->guiCaptureCompletedSerial.load(std::memory_order_acquire);
    const BOOL capturedPulsaret = _plugin->params.neuralPulsaretMix > 0.001f;
    const BOOL capturedEnvelope = _plugin->params.neuralEnvelopeMix > 0.001f;
    const BOOL capturedFm = _plugin->params.neuralFmDepthSemitones > 0.01f;
    const BOOL captureInUse = captureGeneration > 0u && (capturedPulsaret || capturedEnvelope || capturedFm);
    const NSRect captureStateRect = NSMakeRect(908.0, 696.0, 222.0, 38.0);
    [s3g::clap_gui::color(captureInUse ? 0xb8b8b8 : 0x151515) setFill];
    NSRectFill(captureStateRect);
    [s3g::clap_gui::color(captureInUse ? 0xe0e0e0 : 0x454545) setStroke];
    NSFrameRect(captureStateRect);
    if (captureGeneration == 0u) {
        [s3g::clap_gui::color(0x343434) setFill];
        NSRect progress = NSInsetRect(captureStateRect, 1.0, 1.0);
        progress.size.width *= std::clamp(captureProgress, 0.0f, 1.0f);
        NSRectFill(progress);
    }
    NSString* tableStatus = capturePending
        ? @"CAPTURE REQUESTED / WAITING FOR AUDIO"
        : captureGeneration > 0u
        ? [NSString stringWithFormat:@"TABLE %02u / %@", captureGeneration,
            captureInUse ? @"ACTIVE" : @"STORED / MIX OFF"]
        : [NSString stringWithFormat:@"BUFFER %3.0f%% / NO TABLE", captureProgress * 100.0f];
    NSString* pathStatus = [NSString stringWithFormat:@"PUL %@    ENV %@    FM %@",
        capturedPulsaret ? (captureGeneration > 0u ? @"ON" : @"ARM") : @"--",
        capturedEnvelope ? (captureGeneration > 0u ? @"ON" : @"ARM") : @"--",
        capturedFm ? (captureGeneration > 0u ? @"ON" : @"ARM") : @"--"];
    NSDictionary* captureAttrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(captureInUse ? 0x101010 : 0xa8a8a8), 9.0);
    [tableStatus drawAtPoint:NSMakePoint(captureStateRect.origin.x + 7.0, captureStateRect.origin.y + 5.0)
        withAttributes:captureAttrs];
    [pathStatus drawAtPoint:NSMakePoint(captureStateRect.origin.x + 7.0, captureStateRect.origin.y + 20.0)
        withAttributes:captureAttrs];
    [@"CAPTURE BUILDS NEW 2048-SAMPLE TABLES." drawAtPoint:NSMakePoint(908.0, 748.0)
        withAttributes:s3g::clap_gui::softLabelAttrs()];
    [@"MIX CHANGES APPLY ON THE NEXT EVENT." drawAtPoint:NSMakePoint(908.0, 766.0)
        withAttributes:s3g::clap_gui::softLabelAttrs()];
    (void)style;
}

- (void)drawControlPanels:(const s3g::clap_gui::Style&)style
{
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 280, style);
    s3g::clap_gui::drawPanelHeader(@"CLOCK / GLOBAL", true, 630, 42, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(630, 334, 250, 270, style);
    s3g::clap_gui::drawPanelHeader(@"EVENT MASK / WINDOW", true, 630, 334, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(630, 616, 250, 230, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD ORIGIN", true, 630, 616, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(896, 42, 246, 202, style);
    s3g::clap_gui::drawPanelHeader(@"DEPTH / MOTION", true, 896, 42, 246, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(896, 256, 246, 306, style);
    s3g::clap_gui::drawPanelHeader(@"NEURAL CIRCUIT", true, 896, 256, 246, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(896, 574, 246, 272, style);
    s3g::clap_gui::drawPanelHeader(@"CAPTURE UTILITY", true, 896, 574, 246, 21, attrs, style);

    for (const auto& slider : kGuiSliders) {
        if (guiLaneForParam(slider.id) < 0 && !listeningGuiParam(slider.id)) {
            [self drawSlider:slider style:style];
        }
    }
    [self drawMenuLabel:@"ORDER" value:[NSString stringWithFormat:@"%uOA", _plugin->params.order]
                   rect:[self orderRect] style:style];
    [self drawMenuLabel:@"POINTS" value:[NSString stringWithFormat:@"%u", _plugin->params.points]
                   rect:[self pointsRect] style:style];
    [self drawMenuLabel:@"ENVELOPE" value:[NSString stringWithUTF8String:kEnvelopeNames[static_cast<uint32_t>(_plugin->params.envelope)]]
                   rect:[self envelopeRect] style:style];
    [self drawMenuLabel:@"QUALITY" value:[NSString stringWithUTF8String:kQualityNames[static_cast<uint32_t>(_plugin->params.quality)]]
                   rect:[self qualityRect] style:style];
    [self drawMenuLabel:@"MOTION" value:[NSString stringWithUTF8String:
        kMotionModeNames[static_cast<uint32_t>(_plugin->params.motionMode)]]
                   rect:[self motionRect] style:style];
    const uint32_t captureGeneration = _plugin->guiCaptureGeneration.load(std::memory_order_relaxed);
    const BOOL capturePending = _plugin->guiCaptureRequestSerial.load(std::memory_order_acquire)
        != _plugin->guiCaptureCompletedSerial.load(std::memory_order_acquire);
    s3g::clap_gui::drawHeaderActionButton([self captureRect], [self captureHeaderRect],
        capturePending ? @"CAPTURING" : captureGeneration > 0u ? @"RECAPTURE" : @"CAPTURE", attrs, style);
    [self drawCaptureState:style];
}

- (void)drawLanePanel:(const s3g::clap_gui::Style&)style
{
    _selectedLane = std::min<uint32_t>(_selectedLane, s3g::kAmbiPulsarLanes - 1u);
    const NSRect panel = [self lanePanelRect];
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21.0);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PULSARET LANE", true, panel.origin.x, panel.origin.y,
        panel.size.width, 21.0, attrs, style);
    static NSString* laneNames[] = { @"A", @"B", @"C" };
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        s3g::clap_gui::drawHeaderButton([self laneButtonRect:lane], header, laneNames[lane],
            lane == _selectedLane, attrs, style);
    }
    for (const auto& slider : kGuiSliders) {
        if (guiLaneForParam(slider.id) == static_cast<int>(_selectedLane)) [self drawSlider:slider style:style];
    }
    const auto& lane = _plugin->params.lanes[_selectedLane];
    const auto& advanced = _plugin->params.advancedLanes[_selectedLane];
    [self drawMenuLabel:@"TUNING" value:[NSString stringWithUTF8String:
        kTuneModeNames[static_cast<uint32_t>(advanced.tuneMode)]]
                   rect:[self tuneModeRect:_selectedLane] style:style];
    [self drawMenuLabel:@"TRIGGER" value:[NSString stringWithUTF8String:
        kRetriggerModeNames[static_cast<uint32_t>(advanced.retriggerMode)]]
                   rect:[self retriggerModeRect:_selectedLane] style:style];
    [self drawMenuLabel:@"PULSARET" value:[NSString stringWithUTF8String:
        kWaveformNames[static_cast<uint32_t>(lane.waveform)]]
                   rect:[self waveformRect:_selectedLane] style:style];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    syncGuiParams(*_plugin);
    const auto style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    [@"s3g AMBI PULSAR ENCODER 64" drawAtPoint:NSMakePoint(18, 14)
        withAttributes:s3g::clap_gui::softTitleAttrs()];
    [self drawMenuLabel:@"PRESET" value:[self presetDisplayName]
                   rect:[self presetRect] style:style];
    s3g::clap_gui::drawHeaderActionButton([self savePresetRect], [self savePresetRect], @"SAVE",
        s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawHeaderActionButton([self loadPresetRect], [self loadPresetRect], @"LOAD",
        s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawHeaderActionButton([self randomRect], [self randomRect], @"RANDOM",
        s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(
        _plugin->outputPeak.load(std::memory_order_relaxed)), kGuiWidth, 14,
        s3g::clap_gui::softValueAttrs(), 18);
    [self drawVisualPanel:style];
    [self drawControlPanels:style];
    [self drawLanePanel:style];
    [self drawOpenMenu:s3g::clap_gui::softValueAttrs() style:style];
}

- (void)setSlider:(const GuiSliderSpec&)slider point:(NSPoint)point
{
    const CGFloat normalized = (point.x - (slider.panelX + 108.0)) / slider.trackWidth;
    applyParam(*_plugin, slider.id, guiValue(slider.id, normalized));
    [self setNeedsDisplay:YES];
}

- (int)hitPoint:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    const uint32_t points = std::clamp<uint32_t>(_plugin->params.points,
        s3g::kAmbiPulsarMinPoints, s3g::kAmbiPulsarMaxPoints);
    int best = -1;
    CGFloat bestDistance = 18.0;
    for (uint32_t index = 0u; index < points; ++index) {
        const NSPoint projected = [self projectPoint:index depth:nullptr];
        const CGFloat distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(index);
        }
    }
    return best;
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 2) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 3) applyParam(*_plugin, kEnvelopeParamId, hit);
            else if (_openMenu == 4) applyParam(*_plugin, kQualityParamId, hit);
            else if (_openMenu == 5) {
                applyParam(*_plugin, kPointsParamId, hit + 4);
                _selectedPoint = std::min<uint32_t>(_selectedPoint, _plugin->params.points - 1u);
                [self storeViewState];
            } else if (_openMenu == 6) applyParam(*_plugin, kLaneTuneModeIds[_selectedLane], hit);
            else if (_openMenu == 7) applyParam(*_plugin, kLaneRetriggerModeIds[_selectedLane], hit);
            else if (_openMenu == 8) applyParam(*_plugin, kLaneWaveformIds[_selectedLane], hit);
            else if (_openMenu == 9) applyParam(*_plugin, kMotionModeParamId, hit);
            else if (_openMenu == 10) applyParam(*_plugin, kNeuralSetParamId, hit);
            else if (_openMenu == 11) applyParam(*_plugin, kPickupSetParamId, hit);
            else if (_openMenu == 12) applyParam(*_plugin, kListeningModeParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self presetRect])) { [self openMenu:1]; return; }
    if (NSPointInRect(point, [self savePresetRect])) { [self saveCustomPreset]; return; }
    if (NSPointInRect(point, [self loadPresetRect])) { [self loadCustomPreset]; return; }
    if (NSPointInRect(point, [self randomRect])) { randomizeSafe(*_plugin); [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, [self orderRect])) { [self openMenu:2]; return; }
    if (NSPointInRect(point, [self envelopeRect])) { [self openMenu:3]; return; }
    if (NSPointInRect(point, [self qualityRect])) { [self openMenu:4]; return; }
    if (NSPointInRect(point, [self pointsRect])) { [self openMenu:5]; return; }
    if (NSPointInRect(point, [self motionRect])) { [self openMenu:9]; return; }
    if (NSPointInRect(point, [self captureRect])) {
        _plugin->guiCaptureRequestSerial.fetch_add(1u, std::memory_order_release);
        if (_plugin->host && _plugin->host->request_process) _plugin->host->request_process(_plugin->host);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self fieldPanelRect])) {
        for (int index = 0; index < 4; ++index) {
            if (NSPointInRect(point, [self pageButtonRect:index])) {
                _visualPage = index;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (_visualPage == 0) {
            for (int index = 0; index < 2; ++index) {
                if (NSPointInRect(point, [self zoomButtonRect:index])) {
                    _viewZoom = std::clamp(_viewZoom + (index == 0 ? -0.15 : 0.15),
                        static_cast<CGFloat>(0.55), static_cast<CGFloat>(2.20));
                    [self storeViewState];
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
            for (int index = 0; index < 3; ++index) {
                if (NSPointInRect(point, [self viewButtonRect:index])) {
                    [self setViewPreset:index];
                    return;
                }
            }
            const int hit = [self hitPoint:point];
            if (hit >= 0) {
                _selectedPoint = static_cast<uint32_t>(hit);
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        } else if (_visualPage == 1) {
            for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
                if (NSPointInRect(point, [self pulsaretLaneRect:lane])) {
                    _selectedLane = lane;
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
        } else if (_visualPage == 3) {
            if (NSPointInRect(point, [self neuralSetRect])) { [self openMenu:10]; return; }
            if (NSPointInRect(point, [self pickupSetRect])) { [self openMenu:11]; return; }
            if (NSPointInRect(point, [self listeningModeRect])) { [self openMenu:12]; return; }
            if (NSPointInRect(point, [self listeningEnableRect])) {
                applyParam(*_plugin, kListeningEnableParamId,
                    _plugin->params.listening.enabled ? 0.0 : 1.0);
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, [self listeningBypassRect])) {
                applyParam(*_plugin, kListeningBypassParamId,
                    _plugin->params.listening.bypass ? 0.0 : 1.0);
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    if (NSPointInRect(point, [self lanePanelRect])) {
        for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
            if (NSPointInRect(point, [self laneButtonRect:lane])) {
                _selectedLane = lane;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (NSPointInRect(point, [self tuneModeRect:_selectedLane])) {
            [self openMenu:6];
            return;
        }
        if (NSPointInRect(point, [self retriggerModeRect:_selectedLane])) {
            [self openMenu:7];
            return;
        }
        if (NSPointInRect(point, [self waveformRect:_selectedLane])) {
            [self openMenu:8];
            return;
        }
    }
    _dragParam = CLAP_INVALID_ID;
    for (const auto& slider : kGuiSliders) {
        if (listeningGuiParam(slider.id) && _visualPage != 3) continue;
        const int sliderLane = guiLaneForParam(slider.id);
        if (sliderLane >= 0 && sliderLane != static_cast<int>(_selectedLane)) continue;
        if (NSPointInRect(point, guiSliderHitRect(slider))) {
            _dragParam = slider.id;
            [self setSlider:slider point:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam == CLAP_INVALID_ID) return;
    const auto* slider = guiSlider(_dragParam);
    if (!slider) return;
    [self setSlider:*slider point:[self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const int next = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
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
    p->guiView = [[S3GAmbiPulsarEncoderView alloc] initWithPlugin:p];
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
    if (!p->guiView) return;
    [static_cast<S3GAmbiPulsarEncoderView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
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
    [static_cast<S3GAmbiPulsarEncoderView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GAmbiPulsarEncoderView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

} // namespace
#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

constexpr const char* features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-pulsar-encoder-64",
    "s3g Ambi Pulsar Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.11.1",
    "Four-to-thirty-two-point, three-lane pulsar and 16-to-64-node recurrent instrument with direct 1-7OA ACN/SN3D output.",
    features,
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiPulsarFactoryPreset(0u);
    publishParams(*p, p->params, 0u, false);
    p->audioParams = p->params;
    p->audioParamRevision = p->guiParamRevision.load(std::memory_order_acquire);
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->audioParams);
    p->engine.reset();
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
    p->plugin.get_extension = getExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
