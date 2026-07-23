#include "s3g_ambi_wrangler_encoder.h"
#include "s3g_ambi_wrangler_presets.h"
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
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiWranglerMaxChannels;
constexpr uint32_t kStateVersion = 11;
constexpr uint32_t kCustomPresetMagic = 0x33505741u; // AWP3
constexpr uint32_t kCustomPresetVersion = 8;

constexpr clap_id kPresetParamId = 1;
constexpr clap_id kOrderParamId = 2;
constexpr clap_id kVoicesParamId = 3;
constexpr clap_id kRateAParamId = 4;
constexpr clap_id kRateBParamId = 5;
constexpr clap_id kFmAtoBParamId = 6;
constexpr clap_id kFmBtoAParamId = 7;
constexpr clap_id kRunglerAParamId = 8;
constexpr clap_id kRunglerBParamId = 9;
constexpr clap_id kSpreadParamId = 10;
constexpr clap_id kDeviationParamId = 11;
constexpr clap_id kRungSizeParamId = 12;
constexpr clap_id kRateModeAParamId = 13;
constexpr clap_id kThresholdParamId = 14;
constexpr clap_id kColorParamId = 15;
constexpr clap_id kFilterParamId = 16;
constexpr clap_id kResonanceParamId = 17;
constexpr clap_id kFilterRunParamId = 18;
constexpr clap_id kFilterSweepParamId = 19;
constexpr clap_id kSaturationParamId = 20;
constexpr clap_id kTopologyShapeParamId = 21;
constexpr clap_id kTopologyMotionParamId = 22;
constexpr clap_id kTopologyRateParamId = 23;
constexpr clap_id kTopologyAmountParamId = 24;
constexpr clap_id kTopologyDepthParamId = 25;
constexpr clap_id kTopologyScaleParamId = 26;
constexpr clap_id kTopologyCollapseParamId = 27;
constexpr clap_id kAzimuthParamId = 28;
constexpr clap_id kElevationParamId = 29;
constexpr clap_id kDistanceParamId = 30;
constexpr clap_id kSpatialFollowParamId = 31;
constexpr clap_id kOutputParamId = 32;
constexpr clap_id kFieldParamId = 33;
constexpr clap_id kMaskModeParamId = 34;
constexpr clap_id kMaskDepthParamId = 35;
constexpr clap_id kMaskRateParamId = 36;
constexpr clap_id kPwmAParamId = 37;
constexpr clap_id kPwmBParamId = 38;
constexpr clap_id kRampAParamId = 39;
constexpr clap_id kRampBParamId = 40;
constexpr clap_id kInputAParamId = 41;
constexpr clap_id kInputBParamId = 42;
constexpr clap_id kRateModeBParamId = 43;
constexpr clap_id kSnapParamId = 44;
constexpr clap_id kSnapDecayParamId = 45;
constexpr clap_id kRungLoopParamId = 46;
constexpr clap_id kListeningEnabledParamId = 47;
constexpr clap_id kPickupSetParamId = 48;
constexpr clap_id kListenModeParamId = 49;
constexpr clap_id kFieldWriteParamId = 50;
constexpr clap_id kRegisterMotionParamId = 51;
constexpr clap_id kFieldReturnParamId = 52;
constexpr clap_id kPropagationParamId = 53;
constexpr clap_id kReturnBypassParamId = 54;
constexpr clap_id kCircuitLawParamId = 55;
constexpr clap_id kEnginesParamId = 56;
constexpr clap_id kChangeParamId = 57;
constexpr clap_id kListenerResponseParamId = 58;
constexpr clap_id kSettleAmountParamId = 59;
constexpr clap_id kSettleTargetParamId = 60;
constexpr clap_id kSettleRecoveryParamId = 61;

constexpr size_t kWranglerV9ParamsSize =
    offsetof(s3g::AmbiWranglerParams, listeningEnabled);
constexpr size_t kWranglerV10ParamsSize =
    offsetof(s3g::AmbiWranglerParams, circuitLaw);

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiWranglerParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

struct SavedStateV3 {
    uint32_t version = 3u;
    s3g::AmbiWranglerParams params {};
    uint32_t presetIndex = 0u;
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiWranglerParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiWranglerEncoder engine {};
    s3g::AmbiWranglerParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    uint32_t randomSeed = 0x6d2b79f5u;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiMask {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWranglerMaxVoices> guiRegister {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWranglerMaxVoices> guiReadEar {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWranglerMaxVoices> guiComparatorBit {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWranglerMaxVoices> guiAuditoryBit {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWranglerMaxVoices> guiWrittenBit {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiFieldWritePulse {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiFieldReturn {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiCapture {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiEvolutionRate {};
    std::array<std::atomic<float>, s3g::kAmbiWranglerMaxVoices> guiCouplingRate {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWranglerMaxVoices> guiRegisterHeld {};
    std::array<std::atomic<uint64_t>, s3g::kAmbiWranglerMaxVoices> guiHeldClockCount {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWranglerMaxVoices> guiNodeEngine {};
    std::array<std::atomic<float>, s3g::kAmbiFieldListenerMaxLobes> guiListenerEnvelope {};
    std::array<std::atomic<float>, s3g::kAmbiFieldListenerMaxLobes> guiListenerSignal {};
    std::atomic<float> guiListenerActivity { 0.0f };
    std::atomic<float> guiListenerTension { 0.0f };
    std::atomic<float> guiTopologyRate { 1.0f };
    std::atomic<float> guiListenerEnableGain { 0.0f };
    std::atomic<float> guiReturnEnableGain { 0.0f };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->write(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->read(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

std::array<float, s3g::kAmbiWranglerMaxVoices>& breakpointArray(s3g::AmbiWranglerParams& params, uint32_t row)
{
    switch (row) {
    case 0: return params.bpRateA;
    case 1: return params.bpRateB;
    case 2: return params.bpFmAtoB;
    case 3: return params.bpFmBtoA;
    case 4: return params.bpRunglerA;
    case 5: return params.bpRunglerB;
    case 6: return params.bpFilter;
    case 7: return params.bpThreshold;
    case 8: return params.bpPwmA;
    case 9: return params.bpPwmB;
    case 10: return params.bpRampA;
    case 11: return params.bpRampB;
    default: return params.bpAmp;
    }
}

const std::array<float, s3g::kAmbiWranglerMaxVoices>& breakpointArray(const s3g::AmbiWranglerParams& params, uint32_t row)
{
    switch (row) {
    case 0: return params.bpRateA;
    case 1: return params.bpRateB;
    case 2: return params.bpFmAtoB;
    case 3: return params.bpFmBtoA;
    case 4: return params.bpRunglerA;
    case 5: return params.bpRunglerB;
    case 6: return params.bpFilter;
    case 7: return params.bpThreshold;
    case 8: return params.bpPwmA;
    case 9: return params.bpPwmB;
    case 10: return params.bpRampA;
    case 11: return params.bpRampB;
    default: return params.bpAmp;
    }
}

void canonicalizeParamsPadding(s3g::AmbiWranglerParams& params)
{
    constexpr size_t start =
        offsetof(s3g::AmbiWranglerParams, voiceBreakpointsEnabled) + sizeof(bool);
    constexpr size_t end = offsetof(s3g::AmbiWranglerParams, bpRateA);
    static_assert(end >= start);
    std::memset(reinterpret_cast<uint8_t*>(&params) + start, 0, end - start);
}

void migratePreV11Params(s3g::AmbiWranglerParams& params)
{
    params.circuitLaw = s3g::AmbiWranglerCircuitLaw::Legacy;
    params.engines = std::clamp<uint32_t>(
        params.voices, 1u, s3g::kAmbiWranglerMaxVoices);
    params.change = 1.0f;
    params.listenerResponse = s3g::AmbiWranglerListenerResponse::Write;
    params.settleAmount = 0.65f;
    params.settleTarget = 0.30f;
    params.settleRecoverySeconds = 3.0f;
}

double breakpointFallback(const s3g::AmbiWranglerParams& params, uint32_t row)
{
    switch (row) {
    case 0: return params.rateA;
    case 1: return params.rateB;
    case 2: return params.fmAtoB;
    case 3: return params.fmBtoA;
    case 4: return params.runglerA;
    case 5: return params.runglerB;
    case 6: return params.filter;
    case 7: return params.threshold;
    case 8: return params.pwmA;
    case 9: return params.pwmB;
    case 10: return params.rampA;
    case 11: return params.rampB;
    default: return 1.0;
    }
}

double breakpointRange(uint32_t row)
{
    switch (row) {
    case 0:
    case 1:
        return 0.85;
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
        return 0.70;
    case 12:
        return 1.0;
    default:
        return 0.95;
    }
}

double breakpointFinalValue(const s3g::AmbiWranglerParams& params, uint32_t row, uint32_t voice)
{
    const auto& values = breakpointArray(params, row);
    const double curve = values[std::min<uint32_t>(voice, s3g::kAmbiWranglerMaxVoices - 1u)];
    if (row == 12u) return std::clamp(curve, 0.0, 1.0);
    return std::clamp(breakpointFallback(params, row) + (curve - 0.5) * breakpointRange(row), 0.0, 1.0);
}

void seedBreakpointsFromMacros(s3g::AmbiWranglerParams& params)
{
    const uint32_t engines = std::min<uint32_t>(
        std::clamp<uint32_t>(params.engines, 1u, s3g::kAmbiWranglerMaxVoices),
        std::clamp<uint32_t>(params.voices, 1u, s3g::kAmbiWranglerMaxVoices));
    for (uint32_t row = 0u; row < 13u; ++row) {
        auto& values = breakpointArray(params, row);
        for (uint32_t voice = 0u; voice < s3g::kAmbiWranglerMaxVoices; ++voice) {
            const float lane = static_cast<float>(voice % engines)
                / static_cast<float>(std::max<uint32_t>(1u, engines - 1u));
            const float tilt = row < 2u ? (lane - 0.5f) * params.spread * 0.18f : 0.0f;
            values[voice] = row == 12u ? 1.0f : std::clamp(0.5f + tilt, 0.0f, 1.0f);
        }
    }
}

bool saveCustomPresetFile(const char* path, const Plugin& plugin, const char* name)
{
    if (!path || !*path) return false;
    CustomPresetFile file {};
    std::snprintf(file.name, sizeof(file.name), "%s", name && *name ? name : "Custom");
    file.params = plugin.params;
    canonicalizeParamsPadding(file.params);
    FILE* handle = std::fopen(path, "wb");
    if (!handle) return false;
    const bool ok = std::fwrite(&file, 1, sizeof(file), handle) == sizeof(file);
    std::fclose(handle);
    return ok;
}

bool loadCustomPresetFile(const char* path, CustomPresetFile& file)
{
    if (!path || !*path) return false;
    FILE* handle = std::fopen(path, "rb");
    if (!handle) return false;
    file = {};
    bool ok = std::fread(&file.magic, 1, sizeof(file.magic), handle) == sizeof(file.magic)
        && std::fread(&file.version, 1, sizeof(file.version), handle) == sizeof(file.version)
        && file.magic == kCustomPresetMagic
        && (file.version == kCustomPresetVersion || file.version == 7u
            || file.version == 6u || file.version == 5u
            || file.version == 4u || file.version == 3u)
        && std::fread(file.name, 1, sizeof(file.name), handle) == sizeof(file.name);
    if (ok) {
        const uint32_t loadedVersion = file.version;
        if (file.version == kCustomPresetVersion) {
            ok = std::fread(&file.params, 1, sizeof(file.params), handle) == sizeof(file.params);
        } else if (file.version == 7u) {
            ok = std::fread(&file.params, 1, kWranglerV10ParamsSize, handle)
                == kWranglerV10ParamsSize;
        } else if (file.version == 6u) {
            ok = std::fread(&file.params, 1, kWranglerV9ParamsSize, handle)
                == kWranglerV9ParamsSize;
        } else if (file.version == 5u || file.version == 4u) {
            constexpr size_t kPresetV5ParamsSize = kWranglerV9ParamsSize - sizeof(uint32_t);
            ok = std::fread(&file.params, 1, kPresetV5ParamsSize, handle) == kPresetV5ParamsSize;
            file.params.rungLoop = 0u;
            if (ok && loadedVersion == 4u) {
                file.params.rateModeA = std::min<uint32_t>(2u, file.params.rateModeA + 1u);
                file.params.rateModeB = std::min<uint32_t>(2u, file.params.rateModeB + 1u);
            }
        } else {
            constexpr size_t kLegacyParamsV3Size = kWranglerV9ParamsSize - sizeof(float) * 2u - sizeof(uint32_t);
            ok = std::fread(&file.params, 1, kLegacyParamsV3Size, handle) == kLegacyParamsV3Size;
            file.params.snap = 0.0f;
            file.params.snapDecay = 0.34f;
            file.params.rungLoop = 0u;
            file.params.rateModeA = std::min<uint32_t>(2u, file.params.rateModeA + 1u);
            file.params.rateModeB = std::min<uint32_t>(2u, file.params.rateModeB + 1u);
            file.version = kCustomPresetVersion;
        }
        if (ok && loadedVersion < kCustomPresetVersion) {
            migratePreV11Params(file.params);
            file.version = kCustomPresetVersion;
        }
    }
    std::fclose(handle);
    return ok;
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
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
}

float randomRange(uint32_t& seed, float minValue, float maxValue)
{
    return minValue + (maxValue - minValue) * randomUnit(seed);
}

uint32_t randomChoice(uint32_t& seed, uint32_t count)
{
    return std::min<uint32_t>(count - 1u, static_cast<uint32_t>(randomUnit(seed) * static_cast<float>(count)));
}

void randomizeSafe(Plugin& plugin)
{
    auto p = plugin.params;
    uint32_t seed = plugin.randomSeed ^ static_cast<uint32_t>(std::lround(plugin.outputPeak.load(std::memory_order_relaxed) * 1000000.0f));
    const bool calm = randomUnit(seed) < 0.35f;
    if (calm) {
        p.order = 3u;
        p.voices = 12u + randomChoice(seed, 29u);
        p.engines = 2u + randomChoice(seed, 5u);
        p.circuitLaw = s3g::AmbiWranglerCircuitLaw::Bounded;
        p.change = randomRange(seed, 0.08f, 0.34f);
        p.rateA = randomRange(seed, 0.35f, 0.47f);
        p.rateB = randomRange(seed, 0.30f, 0.62f);
        p.rateModeA = 1u;
        p.rateModeB = 0u;
        p.fmAtoB = randomRange(seed, 0.0f, 0.16f);
        p.fmBtoA = randomRange(seed, 0.0f, 0.14f);
        p.runglerA = randomRange(seed, 0.02f, 0.18f);
        p.runglerB = randomRange(seed, 0.02f, 0.16f);
        p.pwmA = randomRange(seed, 0.42f, 0.58f);
        p.pwmB = randomRange(seed, 0.42f, 0.58f);
        p.rampA = randomRange(seed, 0.42f, 0.58f);
        p.rampB = randomRange(seed, 0.42f, 0.58f);
        p.inputA = 1u;
        p.inputB = 1u;
        p.spread = randomRange(seed, 0.01f, 0.12f);
        p.deviation = randomRange(seed, 0.005f, 0.055f);
        p.rungSize = 6u + randomChoice(seed, 3u);
        p.rungLoop = randomUnit(seed) < 0.68f ? 1u : 0u;
        p.threshold = randomRange(seed, 0.42f, 0.58f);
        p.color = randomRange(seed, 0.78f, 1.0f);
        p.filter = randomRange(seed, 0.22f, 0.48f);
        p.resonance = randomRange(seed, 0.24f, 0.62f);
        p.filterRun = randomRange(seed, 0.02f, 0.16f);
        p.filterSweep = randomRange(seed, 0.02f, 0.18f);
        p.saturation = randomRange(seed, 0.02f, 0.16f);
        p.snap = randomRange(seed, 0.0f, 0.07f);
        p.snapDecay = randomRange(seed, 0.34f, 0.72f);
        p.field = randomRange(seed, 0.42f, 0.76f);
        p.maskMode = randomUnit(seed) < 0.58f ? 0u : 1u;
        p.maskDepth = randomRange(seed, 0.10f, 0.38f);
        p.maskRateHz = randomRange(seed, 0.006f, 0.045f);
        p.topologyShape = randomChoice(seed, s3g::kTopologyShapeCount);
        p.topologyMotion = randomUnit(seed) < 0.72f ? 1u : randomChoice(seed, s3g::kTopologyMotionModeCount);
        p.topologyRateHz = randomRange(seed, 0.004f, 0.035f);
        p.topologyAmount = randomRange(seed, 0.28f, 0.62f);
        p.topologyDepth = randomRange(seed, 0.18f, 0.52f);
        p.topologyScale = randomRange(seed, 0.82f, 1.18f);
        p.topologyCollapse = randomRange(seed, 0.0f, 0.08f);
        p.centerAzimuthDeg = randomRange(seed, -24.0f, 24.0f);
        p.centerElevationDeg = randomRange(seed, -12.0f, 18.0f);
        p.centerDistance = randomRange(seed, 0.94f, 1.18f);
        p.spatialFollow = randomRange(seed, 0.88f, 0.98f);
        p.outputGainDb = -6.0f;
        p.listeningEnabled = randomUnit(seed) < 0.84f ? 1u : 0u;
        p.pickupSet = randomUnit(seed) < 0.78f
            ? s3g::AmbiWranglerPickupSet::Cube8
            : s3g::AmbiWranglerPickupSet::Tetra4;
        p.listenMode = randomUnit(seed) < 0.58f
            ? s3g::AmbiWranglerListenMode::Trace
            : s3g::AmbiWranglerListenMode::Balance;
        p.listenerResponse = randomUnit(seed) < 0.84f
            ? s3g::AmbiWranglerListenerResponse::Settle
            : s3g::AmbiWranglerListenerResponse::Write;
        p.fieldWrite = p.listenerResponse == s3g::AmbiWranglerListenerResponse::Write
            ? randomRange(seed, 0.04f, 0.18f) : 0.0f;
        p.registerMotion = randomRange(seed, 0.03f, 0.16f);
        p.fieldReturn = 0.0f;
        p.propagation = randomRange(seed, 0.08f, 0.34f);
        p.returnBypass = 1u;
        p.settleAmount = randomRange(seed, 0.38f, 0.76f);
        p.settleTarget = randomRange(seed, 0.18f, 0.36f);
        p.settleRecoverySeconds = randomRange(seed, 2.5f, 6.5f);

        seedBreakpointsFromMacros(p);
        p.voiceBreakpointsEnabled = randomUnit(seed) < 0.42f;
        if (p.voiceBreakpointsEnabled) {
            const uint32_t engines = std::clamp<uint32_t>(
                p.engines, 1u, s3g::kAmbiWranglerMaxVoices);
            for (uint32_t row = 0u; row < 13u; ++row) {
                auto& values = breakpointArray(p, row);
                const float depth = row == 12u
                    ? randomRange(seed, 0.015f, 0.055f)
                    : randomRange(seed, 0.006f, 0.028f);
                const float phase = randomUnit(seed);
                for (uint32_t voice = 0u;
                    voice < s3g::kAmbiWranglerMaxVoices; ++voice) {
                    const float lane = static_cast<float>(voice % engines)
                        / static_cast<float>(std::max<uint32_t>(1u, engines - 1u));
                    const float wave = std::sin(
                        (lane + phase) * s3g::kPi * 2.0f);
                    values[voice] = row == 12u
                        ? std::clamp(0.88f + wave * depth, 0.72f, 1.0f)
                        : std::clamp(0.5f + wave * depth, 0.0f, 1.0f);
                }
            }
        }
    } else {
    p.order = 3u;
    p.voices = 12u + randomChoice(seed, 29u);
    p.rateA = randomRange(seed, 0.035f, 0.58f);
    p.rateB = randomRange(seed, 0.035f, 0.64f);
    if (randomUnit(seed) < 0.32f) {
        p.rateA *= 0.38f;
        p.rateB *= 0.44f;
    }
    p.fmAtoB = randomRange(seed, 0.02f, 0.58f);
    p.fmBtoA = randomRange(seed, 0.02f, 0.58f);
    p.runglerA = randomRange(seed, 0.03f, 0.78f);
    p.runglerB = randomRange(seed, 0.03f, 0.82f);
    p.pwmA = randomRange(seed, 0.30f, 0.70f);
    p.pwmB = randomRange(seed, 0.30f, 0.70f);
    p.rampA = randomRange(seed, 0.24f, 0.76f);
    p.rampB = randomRange(seed, 0.24f, 0.76f);
    p.inputA = randomUnit(seed) < 0.22f ? 1u : 0u;
    p.inputB = randomUnit(seed) < 0.18f ? 1u : 0u;
    p.spread = randomRange(seed, 0.06f, 0.42f);
    p.deviation = randomRange(seed, 0.025f, 0.22f);
    p.rungSize = 2u + randomChoice(seed, 7u);
    p.rateModeA = randomUnit(seed) < 0.18f ? 0u : (randomUnit(seed) < 0.26f ? 2u : 1u);
    p.rateModeB = randomUnit(seed) < 0.22f ? 0u : (randomUnit(seed) < 0.32f ? 2u : 1u);
    p.rungLoop = randomUnit(seed) < 0.72f ? 0u : (randomUnit(seed) < 0.62f ? 1u : 2u);
    p.threshold = randomRange(seed, 0.20f, 0.82f);
    p.color = randomRange(seed, 0.16f, 0.78f);
    p.filter = randomRange(seed, 0.22f, 0.72f);
    p.resonance = randomRange(seed, 0.12f, 0.58f);
    p.filterRun = randomRange(seed, 0.06f, 0.62f);
    p.filterSweep = randomRange(seed, 0.04f, 0.62f);
    p.saturation = randomRange(seed, 0.16f, 0.58f);
    p.snap = randomRange(seed, 0.04f, 0.46f);
    p.snapDecay = randomRange(seed, 0.08f, 0.54f);
    p.field = randomRange(seed, 0.36f, 0.92f);
    p.maskMode = randomChoice(seed, 6u);
    if (p.maskMode == 0u && randomUnit(seed) < 0.70f) p.maskMode = 2u + randomChoice(seed, 4u);
    p.maskDepth = randomRange(seed, 0.34f, 0.84f);
    p.maskRateHz = randomRange(seed, 0.012f, 0.18f);
    p.topologyShape = randomChoice(seed, s3g::kTopologyShapeCount);
    p.topologyMotion = randomChoice(seed, s3g::kTopologyMotionModeCount);
    p.topologyRateHz = randomRange(seed, 0.010f, 0.12f);
    p.topologyAmount = randomRange(seed, 0.32f, 0.86f);
    p.topologyDepth = randomRange(seed, 0.28f, 0.82f);
    p.topologyScale = randomRange(seed, 0.74f, 1.42f);
    p.topologyCollapse = randomRange(seed, 0.0f, 0.18f);
    p.centerAzimuthDeg = randomRange(seed, -35.0f, 35.0f);
    p.centerElevationDeg = randomRange(seed, -18.0f, 24.0f);
    p.centerDistance = randomRange(seed, 0.88f, 1.30f);
    p.spatialFollow = randomRange(seed, 0.82f, 0.98f);
    p.outputGainDb = -6.0f;
    p.listeningEnabled = randomUnit(seed) < 0.82f ? 1u : 0u;
    p.pickupSet = randomUnit(seed) < 0.72f
        ? s3g::AmbiWranglerPickupSet::Cube8
        : s3g::AmbiWranglerPickupSet::Tetra4;
    p.listenMode = static_cast<s3g::AmbiWranglerListenMode>(randomChoice(seed, 4u));
    p.fieldWrite = randomRange(seed, 0.12f, 0.62f);
    p.registerMotion = randomRange(seed, 0.08f, 0.50f);
    p.fieldReturn = randomRange(seed, 0.04f, 0.32f);
    p.propagation = randomRange(seed, 0.04f, 0.72f);
    p.returnBypass = randomUnit(seed) < 0.20f ? 1u : 0u;
    p.circuitLaw = s3g::AmbiWranglerCircuitLaw::Legacy;
    p.engines = p.voices;
    p.change = 1.0f;
    p.listenerResponse = s3g::AmbiWranglerListenerResponse::Write;
    p.settleAmount = 0.65f;
    p.settleTarget = 0.28f;
    p.settleRecoverySeconds = 3.0f;

    seedBreakpointsFromMacros(p);
    p.voiceBreakpointsEnabled = true;
    const uint32_t voices = std::clamp<uint32_t>(p.voices, 1u, s3g::kAmbiWranglerMaxVoices);
    for (uint32_t row = 0u; row < 13u; ++row) {
        auto& values = breakpointArray(p, row);
        const float depth = row == 12u ? randomRange(seed, 0.12f, 0.34f) : randomRange(seed, 0.04f, 0.24f);
        const float phase = randomUnit(seed);
        const float cycles = 1.0f + static_cast<float>(randomChoice(seed, 5u));
        for (uint32_t voice = 0u; voice < s3g::kAmbiWranglerMaxVoices; ++voice) {
            const float lane = static_cast<float>(voice % voices) / static_cast<float>(std::max<uint32_t>(1u, voices - 1u));
            const float wave = std::sin((lane * cycles + phase) * s3g::kPi * 2.0f);
            const float speck = randomRange(seed, -0.035f, 0.035f);
            if (row == 12u) values[voice] = std::clamp(0.70f + wave * depth + speck, 0.12f, 1.0f);
            else values[voice] = std::clamp(0.5f + wave * depth + speck, 0.0f, 1.0f);
        }
    }
    }

    plugin.randomSeed = seed;
    plugin.params = p;
    plugin.presetIndex = 0u;
    std::snprintf(plugin.customPresetName, sizeof(plugin.customPresetName), "Random");
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
}

bool assignParam(s3g::AmbiWranglerParams& params, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: params.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kVoicesParamId: params.voices = static_cast<uint32_t>(std::lround(value)); return true;
    case kRateAParamId: params.rateA = static_cast<float>(value); return true;
    case kRateBParamId: params.rateB = static_cast<float>(value); return true;
    case kFmAtoBParamId: params.fmAtoB = static_cast<float>(value); return true;
    case kFmBtoAParamId: params.fmBtoA = static_cast<float>(value); return true;
    case kRunglerAParamId: params.runglerA = static_cast<float>(value); return true;
    case kRunglerBParamId: params.runglerB = static_cast<float>(value); return true;
    case kPwmAParamId: params.pwmA = static_cast<float>(value); return true;
    case kPwmBParamId: params.pwmB = static_cast<float>(value); return true;
    case kRampAParamId: params.rampA = static_cast<float>(value); return true;
    case kRampBParamId: params.rampB = static_cast<float>(value); return true;
    case kInputAParamId: params.inputA = static_cast<uint32_t>(std::lround(value)); return true;
    case kInputBParamId: params.inputB = static_cast<uint32_t>(std::lround(value)); return true;
    case kSpreadParamId: params.spread = static_cast<float>(value); return true;
    case kDeviationParamId: params.deviation = static_cast<float>(value); return true;
    case kRungSizeParamId: params.rungSize = static_cast<uint32_t>(std::lround(value)); return true;
    case kRateModeAParamId: params.rateModeA = static_cast<uint32_t>(std::lround(value)); return true;
    case kRateModeBParamId: params.rateModeB = static_cast<uint32_t>(std::lround(value)); return true;
    case kRungLoopParamId: params.rungLoop = static_cast<uint32_t>(std::lround(value)); return true;
    case kThresholdParamId: params.threshold = static_cast<float>(value); return true;
    case kColorParamId: params.color = static_cast<float>(value); return true;
    case kFilterParamId: params.filter = static_cast<float>(value); return true;
    case kResonanceParamId: params.resonance = static_cast<float>(value); return true;
    case kFilterRunParamId: params.filterRun = static_cast<float>(value); return true;
    case kFilterSweepParamId: params.filterSweep = static_cast<float>(value); return true;
    case kSaturationParamId: params.saturation = static_cast<float>(value); return true;
    case kSnapParamId: params.snap = static_cast<float>(value); return true;
    case kSnapDecayParamId: params.snapDecay = static_cast<float>(value); return true;
    case kFieldParamId: params.field = static_cast<float>(value); return true;
    case kMaskModeParamId: params.maskMode = static_cast<uint32_t>(std::lround(value)); return true;
    case kMaskDepthParamId: params.maskDepth = static_cast<float>(value); return true;
    case kMaskRateParamId: params.maskRateHz = static_cast<float>(value); return true;
    case kTopologyShapeParamId: params.topologyShape = static_cast<uint32_t>(std::lround(value)); return true;
    case kTopologyMotionParamId: params.topologyMotion = static_cast<uint32_t>(std::lround(value)); return true;
    case kTopologyRateParamId: params.topologyRateHz = static_cast<float>(value); return true;
    case kTopologyAmountParamId: params.topologyAmount = static_cast<float>(value); return true;
    case kTopologyDepthParamId: params.topologyDepth = static_cast<float>(value); return true;
    case kTopologyScaleParamId: params.topologyScale = static_cast<float>(value); return true;
    case kTopologyCollapseParamId: params.topologyCollapse = static_cast<float>(value); return true;
    case kAzimuthParamId: params.centerAzimuthDeg = static_cast<float>(value); return true;
    case kElevationParamId: params.centerElevationDeg = static_cast<float>(value); return true;
    case kDistanceParamId: params.centerDistance = static_cast<float>(value); return true;
    case kSpatialFollowParamId: params.spatialFollow = static_cast<float>(value); return true;
    case kOutputParamId: params.outputGainDb = static_cast<float>(value); return true;
    case kListeningEnabledParamId: params.listeningEnabled = static_cast<uint32_t>(std::lround(value)); return true;
    case kPickupSetParamId: params.pickupSet = static_cast<s3g::AmbiWranglerPickupSet>(static_cast<uint32_t>(std::lround(value))); return true;
    case kListenModeParamId: params.listenMode = static_cast<s3g::AmbiWranglerListenMode>(static_cast<uint32_t>(std::lround(value))); return true;
    case kFieldWriteParamId: params.fieldWrite = static_cast<float>(value); return true;
    case kRegisterMotionParamId: params.registerMotion = static_cast<float>(value); return true;
    case kFieldReturnParamId: params.fieldReturn = static_cast<float>(value); return true;
    case kPropagationParamId: params.propagation = static_cast<float>(value); return true;
    case kReturnBypassParamId: params.returnBypass = static_cast<uint32_t>(std::lround(value)); return true;
    case kCircuitLawParamId:
        params.circuitLaw = static_cast<s3g::AmbiWranglerCircuitLaw>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    case kEnginesParamId: params.engines = static_cast<uint32_t>(std::lround(value)); return true;
    case kChangeParamId: params.change = static_cast<float>(value); return true;
    case kListenerResponseParamId:
        params.listenerResponse = static_cast<s3g::AmbiWranglerListenerResponse>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    case kSettleAmountParamId: params.settleAmount = static_cast<float>(value); return true;
    case kSettleTargetParamId: params.settleTarget = static_cast<float>(value); return true;
    case kSettleRecoveryParamId: params.settleRecoverySeconds = static_cast<float>(value); return true;
    default: return false;
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kPresetParamId) {
        p.presetIndex = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, s3g::kAmbiWranglerFactoryPresetCount - 1u);
        p.customPresetName[0] = '\0';
        p.params = s3g::ambiWranglerFactoryPreset(p.presetIndex);
        p.engine.setParams(p.params);
        p.params = p.engine.params();
        return;
    }
    if (!assignParam(p.params, id, value)) return;
    p.engine.setParams(p.params);
    p.params = p.engine.params();
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
    p->engine.setParams(p->params);
    p->params = p->engine.params();
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

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            applyParam(p, param->param_id, param->value);
        }
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0, frames);
    if (!output.data32 || outChannels == 0u) return CLAP_PROCESS_CONTINUE;

    std::array<float*, kOutputChannels> outputs {};
    for (uint32_t ch = 0u; ch < outChannels; ++ch) outputs[ch] = output.data32[ch];
    p->engine.setParams(p->params);
    p->engine.process(outputs.data(), outChannels, frames);
    p->params = p->engine.params();
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0u; ch < outChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
#if defined(__APPLE__)
    const uint32_t voices = std::min<uint32_t>(p->params.voices, s3g::kAmbiWranglerMaxVoices);
    for (uint32_t voice = 0u; voice < voices; ++voice) {
        const auto point = p->engine.voicePoint(voice);
        p->guiAzimuth[voice].store(point.azimuthDeg, std::memory_order_relaxed);
        p->guiElevation[voice].store(point.elevationDeg, std::memory_order_relaxed);
        p->guiDistance[voice].store(point.distance, std::memory_order_relaxed);
        p->guiEnergy[voice].store(p->engine.voiceEnergy(voice), std::memory_order_relaxed);
        p->guiMask[voice].store(p->engine.voiceMaskLevel(voice), std::memory_order_relaxed);
        p->guiRegister[voice].store(p->engine.voiceRegister(voice), std::memory_order_relaxed);
        p->guiReadEar[voice].store(p->engine.voiceReadEar(voice), std::memory_order_relaxed);
        p->guiComparatorBit[voice].store(p->engine.voiceComparatorBit(voice), std::memory_order_relaxed);
        p->guiAuditoryBit[voice].store(p->engine.voiceAuditoryBit(voice), std::memory_order_relaxed);
        p->guiWrittenBit[voice].store(p->engine.voiceWrittenBit(voice), std::memory_order_relaxed);
        p->guiFieldWritePulse[voice].store(p->engine.voiceFieldWritePulse(voice), std::memory_order_relaxed);
        p->guiFieldReturn[voice].store(p->engine.voiceFieldReturn(voice), std::memory_order_relaxed);
        p->guiCapture[voice].store(
            p->engine.voiceListenerCapture(voice),
            std::memory_order_relaxed);
        p->guiEvolutionRate[voice].store(
            p->engine.voiceListenerEvolutionRate(voice),
            std::memory_order_relaxed);
        p->guiCouplingRate[voice].store(
            p->engine.voiceListenerCouplingRate(voice),
            std::memory_order_relaxed);
        p->guiRegisterHeld[voice].store(
            p->engine.voiceRegisterHeld(voice),
            std::memory_order_relaxed);
        p->guiHeldClockCount[voice].store(
            p->engine.voiceHeldClockCount(voice),
            std::memory_order_relaxed);
        p->guiNodeEngine[voice].store(p->engine.nodeEngine(voice), std::memory_order_relaxed);
    }
    for (uint32_t ear = 0u; ear < s3g::kAmbiFieldListenerMaxLobes; ++ear) {
        p->guiListenerEnvelope[ear].store(p->engine.listenerEnvelope(ear), std::memory_order_relaxed);
        p->guiListenerSignal[ear].store(p->engine.listenerSignal(ear), std::memory_order_relaxed);
    }
    p->guiListenerActivity.store(p->engine.listenerActivity(), std::memory_order_relaxed);
    p->guiListenerTension.store(p->engine.listenerTension(), std::memory_order_relaxed);
    p->guiTopologyRate.store(
        p->engine.listenerTopologyRate(), std::memory_order_relaxed);
    p->guiListenerEnableGain.store(p->engine.listenerEnableGain(), std::memory_order_relaxed);
    p->guiReturnEnableGain.store(p->engine.returnEnableGain(), std::memory_order_relaxed);
#endif
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    info->id = 20;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiWranglerFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Nodes", 1.0, 64.0, 16.0, true },
    { kCircuitLawParamId, "Circuit Law", 0.0, 1.0, 0.0, true },
    { kEnginesParamId, "Engines", 1.0, 64.0, 4.0, true },
    { kRateAParamId, "Rate A", 0.0, 1.0, 0.28, false },
    { kRateBParamId, "Rate B", 0.0, 1.0, 0.34, false },
    { kFmAtoBParamId, "FM A to B", 0.0, 1.0, 0.18, false },
    { kFmBtoAParamId, "FM B to A", 0.0, 1.0, 0.14, false },
    { kRunglerAParamId, "Rungler A", 0.0, 1.0, 0.25, false },
    { kRunglerBParamId, "Rungler B", 0.0, 1.0, 0.32, false },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.26, false },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.12, false },
    { kRungSizeParamId, "Rung Size", 2.0, 8.0, 4.0, true },
    { kRateModeAParamId, "Rate A Range", 0.0, 2.0, 1.0, true },
    { kRateModeBParamId, "Rate B Range", 0.0, 2.0, 1.0, true },
    { kRungLoopParamId, "Rung Loop", 0.0, 2.0, 0.0, true },
    { kChangeParamId, "Change", 0.0, 1.0, 1.0, false },
    { kThresholdParamId, "Threshold", 0.0, 1.0, 0.50, false },
    { kColorParamId, "Color", 0.0, 1.0, 0.42, false },
    { kFilterParamId, "Filter", 0.0, 1.0, 0.36, false },
    { kResonanceParamId, "Resonance", 0.0, 1.0, 0.20, false },
    { kFilterRunParamId, "Filter Run", 0.0, 1.0, 0.28, false },
    { kFilterSweepParamId, "Filter Sweep", 0.0, 1.0, 0.20, false },
    { kSaturationParamId, "Saturation", 0.0, 1.0, 0.36, false },
    { kTopologyShapeParamId, "Topology", 0.0, static_cast<double>(s3g::kTopologyShapeCount - 1u), 11.0, true },
    { kTopologyMotionParamId, "Motion", 0.0, static_cast<double>(s3g::kTopologyMotionModeCount - 1u), 1.0, true },
    { kTopologyRateParamId, "Motion Rate", 0.001, 2.0, 0.032, false },
    { kTopologyAmountParamId, "Topology Amount", 0.0, 1.0, 0.80, false },
    { kTopologyDepthParamId, "Topology Depth", 0.0, 1.0, 0.72, false },
    { kTopologyScaleParamId, "Topology Scale", 0.20, 2.50, 1.14, false },
    { kTopologyCollapseParamId, "Topology Collapse", 0.0, 1.0, 0.0, false },
    { kAzimuthParamId, "Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Distance", 0.15, 2.0, 1.0, false },
    { kSpatialFollowParamId, "Spatial Follow", 0.0, 1.0, 0.90, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kFieldParamId, "Voice Field", 0.0, 1.0, 0.62, false },
    { kMaskModeParamId, "Mask Mode", 0.0, 5.0, 2.0, true },
    { kMaskDepthParamId, "Mask Depth", 0.0, 1.0, 0.55, false },
    { kMaskRateParamId, "Mask Rate", 0.0, 4.0, 0.070, false },
    { kPwmAParamId, "PWM A", 0.0, 1.0, 0.50, false },
    { kPwmBParamId, "PWM B", 0.0, 1.0, 0.50, false },
    { kRampAParamId, "Ramp A", 0.0, 1.0, 0.50, false },
    { kRampBParamId, "Ramp B", 0.0, 1.0, 0.50, false },
    { kInputAParamId, "Input A", 0.0, 1.0, 0.0, true },
    { kInputBParamId, "Input B", 0.0, 1.0, 0.0, true },
    { kSnapParamId, "Snap", 0.0, 1.0, 0.0, false },
    { kSnapDecayParamId, "Snap Decay", 0.0, 1.0, 0.34, false },
    { kListeningEnabledParamId, "Field Listener", 0.0, 1.0, 0.0, true },
    { kPickupSetParamId, "Pickup Set", 0.0, 1.0, 1.0, true },
    { kListenModeParamId, "Read Mode", 0.0, 3.0, 0.0, true },
    { kListenerResponseParamId, "Listener Response", 0.0, 1.0, 0.0, true },
    { kFieldWriteParamId, "Field Write", 0.0, 1.0, 0.0, false },
    { kRegisterMotionParamId, "Register Motion", 0.0, 1.0, 0.0, false },
    { kFieldReturnParamId, "Field Return", 0.0, 1.0, 0.0, false },
    { kPropagationParamId, "Propagation", 0.0, 1.0, 0.18, false },
    { kReturnBypassParamId, "Return Bypass", 0.0, 1.0, 0.0, true },
    { kSettleAmountParamId, "Listener Viscosity", 0.0, 1.0, 0.65, false },
    { kSettleTargetParamId, "Calm Target", 0.0, 0.95, 0.30, false },
    { kSettleRecoveryParamId, "Capture Recovery", 0.25, 12.0, 3.0, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool isListenerParamId(clap_id id)
{
    return (id >= kListeningEnabledParamId && id <= kReturnBypassParamId)
        || id == kListenerResponseParamId
        || id == kSettleAmountParamId
        || id == kSettleTargetParamId
        || id == kSettleRecoveryParamId;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module,
        isListenerParamId(def.id)
            ? "Field Listener" : "Ambi Wrangler Encoder",
        sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* p = self(plugin);
    const auto params = p->params;
    switch (id) {
    case kPresetParamId: *value = p->presetIndex; return true;
    case kOrderParamId: *value = params.order; return true;
    case kVoicesParamId: *value = params.voices; return true;
    case kRateAParamId: *value = params.rateA; return true;
    case kRateBParamId: *value = params.rateB; return true;
    case kFmAtoBParamId: *value = params.fmAtoB; return true;
    case kFmBtoAParamId: *value = params.fmBtoA; return true;
    case kRunglerAParamId: *value = params.runglerA; return true;
    case kRunglerBParamId: *value = params.runglerB; return true;
    case kPwmAParamId: *value = params.pwmA; return true;
    case kPwmBParamId: *value = params.pwmB; return true;
    case kRampAParamId: *value = params.rampA; return true;
    case kRampBParamId: *value = params.rampB; return true;
    case kInputAParamId: *value = params.inputA; return true;
    case kInputBParamId: *value = params.inputB; return true;
    case kSpreadParamId: *value = params.spread; return true;
    case kDeviationParamId: *value = params.deviation; return true;
    case kRungSizeParamId: *value = params.rungSize; return true;
    case kRateModeAParamId: *value = params.rateModeA; return true;
    case kRateModeBParamId: *value = params.rateModeB; return true;
    case kRungLoopParamId: *value = params.rungLoop; return true;
    case kThresholdParamId: *value = params.threshold; return true;
    case kColorParamId: *value = params.color; return true;
    case kFilterParamId: *value = params.filter; return true;
    case kResonanceParamId: *value = params.resonance; return true;
    case kFilterRunParamId: *value = params.filterRun; return true;
    case kFilterSweepParamId: *value = params.filterSweep; return true;
    case kSaturationParamId: *value = params.saturation; return true;
    case kSnapParamId: *value = params.snap; return true;
    case kSnapDecayParamId: *value = params.snapDecay; return true;
    case kTopologyShapeParamId: *value = params.topologyShape; return true;
    case kTopologyMotionParamId: *value = params.topologyMotion; return true;
    case kTopologyRateParamId: *value = params.topologyRateHz; return true;
    case kTopologyAmountParamId: *value = params.topologyAmount; return true;
    case kTopologyDepthParamId: *value = params.topologyDepth; return true;
    case kTopologyScaleParamId: *value = params.topologyScale; return true;
    case kTopologyCollapseParamId: *value = params.topologyCollapse; return true;
    case kAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kElevationParamId: *value = params.centerElevationDeg; return true;
    case kDistanceParamId: *value = params.centerDistance; return true;
    case kSpatialFollowParamId: *value = params.spatialFollow; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kFieldParamId: *value = params.field; return true;
    case kMaskModeParamId: *value = params.maskMode; return true;
    case kMaskDepthParamId: *value = params.maskDepth; return true;
    case kMaskRateParamId: *value = params.maskRateHz; return true;
    case kListeningEnabledParamId: *value = params.listeningEnabled; return true;
    case kPickupSetParamId: *value = static_cast<uint32_t>(params.pickupSet); return true;
    case kListenModeParamId: *value = static_cast<uint32_t>(params.listenMode); return true;
    case kFieldWriteParamId: *value = params.fieldWrite; return true;
    case kRegisterMotionParamId: *value = params.registerMotion; return true;
    case kFieldReturnParamId: *value = params.fieldReturn; return true;
    case kPropagationParamId: *value = params.propagation; return true;
    case kReturnBypassParamId: *value = params.returnBypass; return true;
    case kCircuitLawParamId: *value = static_cast<uint32_t>(params.circuitLaw); return true;
    case kEnginesParamId: *value = params.engines; return true;
    case kChangeParamId: *value = params.change; return true;
    case kListenerResponseParamId: *value = static_cast<uint32_t>(params.listenerResponse); return true;
    case kSettleAmountParamId: *value = params.settleAmount; return true;
    case kSettleTargetParamId: *value = params.settleTarget; return true;
    case kSettleRecoveryParamId: *value = params.settleRecoverySeconds; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", s3g::ambiWranglerFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kVoicesParamId || id == kEnginesParamId) {
        std::snprintf(display, size, "%.0f", value);
    } else if (id == kCircuitLawParamId) {
        std::snprintf(display, size, "%s", value < 0.5 ? "LEGACY" : "BOUNDED");
    } else if (id == kRateModeAParamId || id == kRateModeBParamId) {
        static constexpr const char* kRateRangeNames[] = { "LOW", "SINGLE", "DOUBLE" };
        std::snprintf(display, size, "%s", kRateRangeNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 2u)]);
    } else if (id == kRungLoopParamId) {
        static constexpr const char* kLoopNames[] = { "OFF", "LOOP", "XOR" };
        std::snprintf(display, size, "%s", kLoopNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 2u)]);
    } else if (id == kListeningEnabledParamId) {
        std::snprintf(display, size, "%s", value < 0.5 ? "OFF" : "ON");
    } else if (id == kReturnBypassParamId) {
        std::snprintf(display, size, "%s", value < 0.5 ? "ACTIVE" : "BYPASS");
    } else if (id == kPickupSetParamId) {
        std::snprintf(display, size, "%s", value < 0.5 ? "TETRA 4" : "CUBE 8");
    } else if (id == kListenModeParamId) {
        static constexpr const char* kListenNames[] = { "TRACE", "RING", "CROSS", "BALANCE" };
        std::snprintf(display, size, "%s", kListenNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kListenerResponseParamId) {
        std::snprintf(display, size, "%s", value < 0.5 ? "WRITE" : "SETTLE");
    } else if (id == kInputAParamId || id == kInputBParamId) {
        std::snprintf(display, size, "%s", value < 0.5 ? "SQUARE" : "TRI");
    } else if (id == kMaskModeParamId) {
        static constexpr const char* kMaskNames[] = { "ALL", "BREATH", "CHOIR", "CELLS", "SPARK", "WEAVE" };
        std::snprintf(display, size, "%s", kMaskNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 5u)]);
    } else if (id == kTopologyShapeParamId) {
        std::snprintf(display, size, "%s", s3g::topologyShapeName(static_cast<uint32_t>(std::lround(value))));
    } else if (id == kTopologyMotionParamId) {
        std::snprintf(display, size, "%s", s3g::topologyMotionModeName(static_cast<uint32_t>(std::lround(value))));
    } else if (id == kTopologyRateParamId || id == kMaskRateParamId) {
        std::snprintf(display, size, "%.3f Hz", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kRateAParamId || id == kRateBParamId) {
        std::snprintf(display, size, value < 0.01 ? "%.3f%%" : "%.2f%%", value * 100.0);
    } else if (id == kFmAtoBParamId || id == kFmBtoAParamId
        || id == kRunglerAParamId || id == kRunglerBParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kPwmAParamId || id == kPwmBParamId || id == kRampAParamId || id == kRampBParamId
        || id == kThresholdParamId || id == kColorParamId || id == kFilterParamId || id == kResonanceParamId
        || id == kFilterRunParamId || id == kFilterSweepParamId || id == kSaturationParamId
        || id == kSnapParamId || id == kSnapDecayParamId
        || id == kFieldParamId || id == kMaskDepthParamId
        || id == kTopologyAmountParamId || id == kTopologyDepthParamId || id == kTopologyCollapseParamId
        || id == kSpatialFollowParamId || id == kFieldWriteParamId
        || id == kRegisterMotionParamId || id == kFieldReturnParamId
        || id == kChangeParamId || id == kSettleAmountParamId
        || id == kSettleTargetParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (id == kSettleRecoveryParamId) {
        std::snprintf(display, size, "%.1f s", value);
    } else if (id == kPropagationParamId) {
        const double milliseconds = (1.0 / 48000.0 + value * value * 0.180) * 1000.0;
        std::snprintf(display, size, "%.1f ms", milliseconds);
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;

    if (id == kPresetParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWranglerFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiWranglerFactoryPresetInfo(index).name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kTopologyShapeParamId) {
        for (uint32_t index = 0u; index < s3g::kTopologyShapeCount; ++index) {
            if (std::strcmp(display, s3g::topologyShapeName(index)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kTopologyMotionParamId) {
        for (uint32_t index = 0u; index < s3g::kTopologyMotionModeCount; ++index) {
            if (std::strcmp(display, s3g::topologyMotionModeName(index)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kInputAParamId || id == kInputBParamId) {
        if (std::strcmp(display, "SQUARE") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "TRI") == 0) {
            *value = 1.0;
            return true;
        }
        return false;
    }
    if (id == kCircuitLawParamId) {
        if (std::strcmp(display, "LEGACY") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "BOUNDED") == 0) {
            *value = 1.0;
            return true;
        }
        return false;
    }
    if (id == kRateModeAParamId || id == kRateModeBParamId) {
        if (std::strcmp(display, "LOW") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "SINGLE") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "DOUBLE") == 0) {
            *value = 2.0;
            return true;
        }
    }
    if (id == kRungLoopParamId) {
        if (std::strcmp(display, "OFF") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "LOOP") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "XOR") == 0) {
            *value = 2.0;
            return true;
        }
    }
    if (id == kMaskModeParamId) {
        static constexpr const char* kMaskNames[] = {
            "ALL", "BREATH", "CHOIR", "CELLS", "SPARK", "WEAVE"
        };
        for (uint32_t mode = 0u; mode < 6u; ++mode) {
            if (std::strcmp(display, kMaskNames[mode]) == 0) {
                *value = mode;
                return true;
            }
        }
    }
    if (id == kListeningEnabledParamId || id == kReturnBypassParamId) {
        if (std::strcmp(display, "OFF") == 0 || std::strcmp(display, "ACTIVE") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "ON") == 0 || std::strcmp(display, "BYPASS") == 0) {
            *value = 1.0;
            return true;
        }
    }
    if (id == kPickupSetParamId) {
        if (std::strcmp(display, "TETRA 4") == 0) { *value = 0.0; return true; }
        if (std::strcmp(display, "CUBE 8") == 0) { *value = 1.0; return true; }
    }
    if (id == kListenModeParamId) {
        static constexpr const char* kListenNames[] = { "TRACE", "RING", "CROSS", "BALANCE" };
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            if (std::strcmp(display, kListenNames[mode]) == 0) {
                *value = mode;
                return true;
            }
        }
    }
    if (id == kListenerResponseParamId) {
        if (std::strcmp(display, "WRITE") == 0) { *value = 0.0; return true; }
        if (std::strcmp(display, "SETTLE") == 0) { *value = 1.0; return true; }
        return false;
    }
    if (id == kPropagationParamId) {
        const double milliseconds = std::max(0.0, std::atof(display));
        const double seconds = milliseconds * 0.001;
        *value = std::sqrt(std::clamp(
            (seconds - 1.0 / 48000.0) / 0.180, 0.0, 1.0));
        return true;
    }

    *value = std::atof(display);
    if (id == kRateAParamId || id == kRateBParamId || id == kFmAtoBParamId || id == kFmBtoAParamId
        || id == kRunglerAParamId || id == kRunglerBParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kPwmAParamId || id == kPwmBParamId || id == kRampAParamId || id == kRampBParamId
        || id == kThresholdParamId || id == kColorParamId || id == kFilterParamId || id == kResonanceParamId
        || id == kFilterRunParamId || id == kFilterSweepParamId || id == kSaturationParamId
        || id == kSnapParamId || id == kSnapDecayParamId
        || id == kFieldParamId || id == kMaskDepthParamId
        || id == kTopologyAmountParamId || id == kTopologyDepthParamId || id == kTopologyCollapseParamId
        || id == kSpatialFollowParamId || id == kFieldWriteParamId
        || id == kRegisterMotionParamId || id == kFieldReturnParamId
        || id == kChangeParamId || id == kSettleAmountParamId
        || id == kSettleTargetParamId) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    SavedState state {};
    state.version = kStateVersion;
    state.params = p->params;
    canonicalizeParamsPadding(state.params);
    state.presetIndex = p->presetIndex;
    std::snprintf(state.customPresetName, sizeof(state.customPresetName), "%s", p->customPresetName);
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    SavedState state {};
    state.version = version;
    if (version == 3u) {
        constexpr size_t kLegacyParamsV3Size =
            kWranglerV9ParamsSize - sizeof(float) * 2u - sizeof(uint32_t);
        if (!readExact(stream, &state.params, kLegacyParamsV3Size)) return false;
        if (!readExact(stream, &state.presetIndex, sizeof(state.presetIndex))) return false;
        state.params.snap = 0.0f;
        state.params.snapDecay = 0.34f;
        state.params.rungLoop = 0u;
        state.customPresetName[0] = '\0';
    } else if (version == 6u) {
        constexpr size_t kLegacyParamsV6Size =
            kWranglerV9ParamsSize - sizeof(float) * 2u - sizeof(uint32_t);
        if (!readExact(stream, &state.params, kLegacyParamsV6Size)) return false;
        state.params.snap = 0.0f;
        state.params.snapDecay = 0.34f;
        state.params.rungLoop = 0u;
        state.params.rateModeA = std::min<uint32_t>(2u, state.params.rateModeA + 1u);
        state.params.rateModeB = std::min<uint32_t>(2u, state.params.rateModeB + 1u);
        if (!readExact(stream, &state.presetIndex, sizeof(state.presetIndex))) return false;
        if (!readExact(stream, state.customPresetName, sizeof(state.customPresetName))) return false;
    } else if (version == 7u) {
        constexpr size_t kStateV7ParamsSize = kWranglerV9ParamsSize - sizeof(uint32_t);
        if (!readExact(stream, &state.params, kStateV7ParamsSize)) return false;
        state.params.rungLoop = 0u;
        if (!readExact(stream, &state.presetIndex, sizeof(state.presetIndex))) return false;
        if (!readExact(stream, state.customPresetName, sizeof(state.customPresetName))) return false;
        state.params.rateModeA = std::min<uint32_t>(2u, state.params.rateModeA + 1u);
        state.params.rateModeB = std::min<uint32_t>(2u, state.params.rateModeB + 1u);
    } else if (version == 8u) {
        constexpr size_t kStateV8ParamsSize = kWranglerV9ParamsSize - sizeof(uint32_t);
        if (!readExact(stream, &state.params, kStateV8ParamsSize)) return false;
        state.params.rungLoop = 0u;
        if (!readExact(stream, &state.presetIndex, sizeof(state.presetIndex))) return false;
        if (!readExact(stream, state.customPresetName, sizeof(state.customPresetName))) return false;
    } else if (version == 9u) {
        if (!readExact(stream, &state.params, kWranglerV9ParamsSize)) return false;
        if (!readExact(stream, &state.presetIndex, sizeof(state.presetIndex))) return false;
        if (!readExact(stream, state.customPresetName, sizeof(state.customPresetName))) return false;
    } else if (version == 10u) {
        if (!readExact(stream, &state.params, kWranglerV10ParamsSize)) return false;
        if (!readExact(stream, &state.presetIndex, sizeof(state.presetIndex))) return false;
        if (!readExact(stream, state.customPresetName, sizeof(state.customPresetName))) return false;
    } else if (version == kStateVersion) {
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(state.version), sizeof(state) - sizeof(state.version))) return false;
    } else {
        return false;
    }
    if (version < kStateVersion) migratePreV11Params(state.params);
    auto* p = self(plugin);
    p->params = state.params;
    p->presetIndex = std::min<uint32_t>(state.presetIndex, s3g::kAmbiWranglerFactoryPresetCount - 1u);
    std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", state.customPresetName);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

constexpr uint32_t kGuiWidth = 1160;
constexpr uint32_t kGuiHeight = 858;

struct GuiSliderSpec {
    clap_id id;
    CGFloat panelX;
    CGFloat y;
    double min;
    double max;
    bool logarithmic;
};

constexpr GuiSliderSpec kGuiSliders[] {
    { kVoicesParamId, 630, 144, 1.0, 64.0, false },
    { kEnginesParamId, 630, 168, 1.0, 64.0, false },
    { kRateAParamId, 630, 192, 0.0, 1.0, false },
    { kRateBParamId, 630, 216, 0.0, 1.0, false },
    { kSpreadParamId, 630, 240, 0.0, 1.0, false },
    { kDeviationParamId, 630, 264, 0.0, 1.0, false },
    { kFmAtoBParamId, 630, 334, 0.0, 1.0, false },
    { kFmBtoAParamId, 630, 358, 0.0, 1.0, false },
    { kRunglerAParamId, 630, 382, 0.0, 1.0, false },
    { kRunglerBParamId, 630, 406, 0.0, 1.0, false },
    { kRungSizeParamId, 630, 430, 2.0, 8.0, false },
    { kChangeParamId, 630, 454, 0.0, 1.0, false },
    { kThresholdParamId, 630, 478, 0.0, 1.0, false },
    { kColorParamId, 630, 538, 0.0, 1.0, false },
    { kFilterParamId, 630, 564, 0.0, 1.0, false },
    { kResonanceParamId, 630, 590, 0.0, 1.0, false },
    { kFilterRunParamId, 630, 616, 0.0, 1.0, false },
    { kFilterSweepParamId, 630, 642, 0.0, 1.0, false },
    { kSaturationParamId, 630, 668, 0.0, 1.0, false },
    { kPwmAParamId, 630, 748, 0.0, 1.0, false },
    { kPwmBParamId, 630, 774, 0.0, 1.0, false },
    { kTopologyRateParamId, 896, 156, 0.001, 2.0, true },
    { kTopologyAmountParamId, 896, 182, 0.0, 1.0, false },
    { kTopologyDepthParamId, 896, 208, 0.0, 1.0, false },
    { kTopologyScaleParamId, 896, 234, 0.20, 2.50, false },
    { kTopologyCollapseParamId, 896, 260, 0.0, 1.0, false },
    { kAzimuthParamId, 896, 334, -180.0, 180.0, false },
    { kElevationParamId, 896, 360, -90.0, 90.0, false },
    { kDistanceParamId, 896, 386, 0.15, 2.0, false },
    { kSpatialFollowParamId, 896, 412, 0.0, 1.0, false },
    { kFieldParamId, 896, 534, 0.0, 1.0, false },
    { kMaskDepthParamId, 896, 560, 0.0, 1.0, false },
    { kMaskRateParamId, 896, 586, 0.001, 4.0, true },
    { kRampAParamId, 896, 720, 0.0, 1.0, false },
    { kRampBParamId, 896, 746, 0.0, 1.0, false },
    { kSnapParamId, 896, 772, 0.0, 1.0, false },
    { kSnapDecayParamId, 896, 798, 0.0, 1.0, false },
    { kOutputParamId, 896, 824, -60.0, 12.0, false },
    { kFieldReturnParamId, 38, 540, 0.0, 1.0, false },
    { kPropagationParamId, 38, 574, 0.0, 1.0, false },
    { kFieldWriteParamId, 314, 540, 0.0, 1.0, false },
    { kRegisterMotionParamId, 314, 574, 0.0, 1.0, false },
    { kSettleAmountParamId, 38, 540, 0.0, 1.0, false },
    { kSettleTargetParamId, 38, 574, 0.0, 0.95, false },
    { kSettleRecoveryParamId, 314, 540, 0.25, 12.0, true },
};

const GuiSliderSpec* guiSliderSpec(clap_id id)
{
    for (const auto& spec : kGuiSliders) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

bool isListenerGuiParam(clap_id id)
{
    return id == kFieldWriteParamId || id == kRegisterMotionParamId
        || id == kFieldReturnParamId || id == kPropagationParamId
        || id == kSettleAmountParamId || id == kSettleTargetParamId
        || id == kSettleRecoveryParamId;
}

double sliderNorm(const GuiSliderSpec& spec, double value)
{
    if (spec.logarithmic) {
        const double minValue = std::max(0.000001, spec.min);
        return std::clamp(std::log(std::max(minValue, value) / minValue) / std::log(spec.max / minValue), 0.0, 1.0);
    }
    return std::clamp((value - spec.min) / std::max(0.000001, spec.max - spec.min), 0.0, 1.0);
}

double sliderValue(const GuiSliderSpec& spec, NSPoint point)
{
    const double norm = std::clamp((static_cast<double>(point.x) - (spec.panelX + 108.0)) / 82.0, 0.0, 1.0);
    if (spec.logarithmic) return spec.min * std::pow(spec.max / spec.min, norm);
    return spec.min + norm * (spec.max - spec.min);
}

double rateModeScaleForDisplay(uint32_t mode)
{
    switch (mode) {
    case 0u: return 0.01;
    case 2u: return 2.0;
    default: return 1.0;
    }
}

double rateNormToHzForDisplay(double value, uint32_t mode)
{
    return 440.0 * std::pow(2.0, (std::clamp(value, 0.0, 1.0) * 172.0 - 109.0) / 12.0)
        * rateModeScaleForDisplay(mode);
}

@interface S3GAmbiWranglerEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    uint32_t _selectedVoice;
    int _dragParam;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    int _fieldPage;
    int _dragBreakpointRow;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiWranglerEncoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _selectedVoice = 0;
        _dragParam = 0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _fieldPage = 0;
        _dragBreakpointRow = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _openMenuRect = NSZeroRect;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)timerTick:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 608); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 558); }
- (NSRect)presetMenuRect { return NSMakeRect(382, 13, 190, 15); }
- (NSRect)savePresetButtonRect { return NSMakeRect(580, 13, 46, 15); }
- (NSRect)loadPresetButtonRect { return NSMakeRect(632, 13, 46, 15); }
- (NSRect)randomizeButtonRect { return NSMakeRect(684, 13, 66, 15); }

- (NSRect)pageButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(panel.origin.x + 98.0 + index * 74.0,
        panel.origin.y + 4.0, 70.0, 13.0);
}

- (NSRect)listenVoiceRect { return NSMakeRect(112.0, 484.0, 104.0, 15.0); }
- (NSRect)pickupSetRect { return NSMakeRect(306.0, 484.0, 104.0, 15.0); }
- (NSRect)listenModeRect { return NSMakeRect(488.0, 484.0, 104.0, 15.0); }
- (NSRect)listenerResponseRect { return NSMakeRect(306.0, 508.0, 104.0, 15.0); }
- (NSRect)listenEnableRect { return NSMakeRect(46.0, 610.0, 118.0, 15.0); }
- (NSRect)returnBypassRect { return NSMakeRect(172.0, 610.0, 118.0, 15.0); }
- (NSRect)circuitLawButtonRect { return NSMakeRect(776.0, 46.0, 92.0, 13.0); }

- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    const CGFloat w = 38.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(NSMaxX(panel) - 10.0 - (3.0 - index) * w - (2.0 - index) * gap, panel.origin.y + 4.0, w, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    const CGFloat w = 18.0;
    const CGFloat gap = 4.0;
    const CGFloat x = [self viewButtonRect:0].origin.x - 12.0 - (2.0 - index) * w - (1.0 - index) * gap;
    return NSMakeRect(x, [self fieldPanelRect].origin.y + 4.0, w, 13.0);
}

- (s3g::Vec3)voiceWorld:(uint32_t)voice
{
    if (!_plugin || voice >= s3g::kAmbiWranglerMaxVoices) return { 0.0f, 0.0f, 0.0f };
    const float az = _plugin->guiAzimuth[voice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[voice].load(std::memory_order_relaxed);
    const s3g::Vec3 dir = s3g::directionFromAed(az, el);
    return { dir.x * dist, dir.y * dist, dir.z * dist };
}

- (NSPoint)projectWorld:(s3g::Vec3)point depth:(CGFloat*)depth
{
    const NSRect field = [self fieldRect];
    const CGFloat scale = std::min(field.size.width, field.size.height) * 0.36 * std::clamp(_viewZoom, 0.55, 2.20);
    const CGFloat centerX = NSMidX(field);
    const CGFloat centerY = NSMidY(field);
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
    return NSMakePoint(centerX + x1 * scale, centerY - y2 * scale);
}

- (NSPoint)projectVoice:(uint32_t)voice depth:(CGFloat*)depth
{
    return [self projectWorld:[self voiceWorld:voice] depth:depth];
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 0.0;
        _viewElDeg = -90.0;
    } else {
        _viewAzDeg = 38.0;
        _viewElDeg = 32.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (NSString*)customPresetDirectory
{
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Wrangler Encoder"];
}

- (void)saveCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gawp" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3gawp", [self presetDisplayName]]];
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* name = [[[[panel URL] lastPathComponent] stringByDeletingPathExtension] copy];
    if (saveCustomPresetFile([[[panel URL] path] UTF8String], *_plugin, [name UTF8String])) {
        std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s", [name UTF8String]);
    }
    [name release];
    [self setNeedsDisplay:YES];
}

- (void)loadCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gawp" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return;
    _plugin->params = file.params;
    _plugin->engine.setParams(_plugin->params);
    _plugin->params = _plugin->engine.params();
    std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s", file.name[0] ? file.name : "Custom");
    [self setNeedsDisplay:YES];
}

- (NSColor*)voiceColor:(uint32_t)voice selected:(BOOL)selected
{
    const float az = _plugin->guiAzimuth[voice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const float hue = std::fmod((az + 180.0f) / 360.0f + 0.08f, 1.0f);
    const float sat = selected ? 0.72f : 0.52f;
    const float bri = selected ? 0.96f : 0.72f + std::max(0.0f, el) / 90.0f * 0.18f;
    return [NSColor colorWithCalibratedHue:hue saturation:sat brightness:bri alpha:selected ? 1.0 : 0.84];
}

- (void)drawBreakpointEditor:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    (void)style;
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);

    static NSString* rowLabels[] = { @"RATE A", @"RATE B", @"FM A>B", @"FM B>A", @"RUNG A", @"RUNG B", @"FILTER", @"THRESH", @"PWM A", @"PWM B", @"RAMP A", @"RAMP B", @"AMP" };
    const uint32_t engines = std::min<uint32_t>(
        std::clamp<uint32_t>(_plugin->params.engines, 1u, s3g::kAmbiWranglerMaxVoices),
        std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiWranglerMaxVoices));
    _selectedVoice = std::min<uint32_t>(_selectedVoice, engines - 1u);
    const CGFloat labelW = 54.0;
    const CGFloat left = field.origin.x + labelW + 10.0;
    const CGFloat right = NSMaxX(field) - 12.0;
    const CGFloat top = field.origin.y + 28.0;
    const CGFloat bottom = NSMaxY(field) - 30.0;
    const CGFloat rowH = (bottom - top) / 13.0;

    for (uint32_t row = 0; row < 13u; ++row) {
        const CGFloat y = top + rowH * row;
        [[s3g::clap_gui::color(row % 2u ? 0x111111 : 0x151515) colorWithAlphaComponent:0.92] setFill];
        NSRectFill(NSMakeRect(field.origin.x + 1.0, y, field.size.width - 2.0, rowH));
        [rowLabels[row] drawAtPoint:NSMakePoint(field.origin.x + 10.0, y + rowH * 0.5 - 5.0) withAttributes:attrs];
        [s3g::clap_gui::color(0x2b2b2b) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(left, y + rowH) toPoint:NSMakePoint(right, y + rowH)];

        NSBezierPath* curve = [NSBezierPath bezierPath];
        for (uint32_t voice = 0u; voice < engines; ++voice) {
            const double fallback = breakpointFallback(_plugin->params, row);
            const double value = _plugin->params.voiceBreakpointsEnabled ? breakpointFinalValue(_plugin->params, row, voice) : fallback;
            const CGFloat x = left + (right - left) * (engines <= 1u ? 0.0 : static_cast<CGFloat>(voice) / static_cast<CGFloat>(engines - 1u));
            const CGFloat yy = y + 5.0 + (1.0 - std::clamp(value, 0.0, 1.0)) * (rowH - 10.0);
            if (voice == 0u) [curve moveToPoint:NSMakePoint(x, yy)];
            else [curve lineToPoint:NSMakePoint(x, yy)];
        }
        [[s3g::clap_gui::color(row == static_cast<uint32_t>(_dragBreakpointRow) ? 0xf0d86a : 0xa7c7ba) colorWithAlphaComponent:_plugin->params.voiceBreakpointsEnabled ? 0.90 : 0.42] setStroke];
        [curve setLineWidth:1.35];
        [curve stroke];

        for (uint32_t voice = 0u; voice < engines; ++voice) {
            const double fallback = breakpointFallback(_plugin->params, row);
            const double value = _plugin->params.voiceBreakpointsEnabled ? breakpointFinalValue(_plugin->params, row, voice) : fallback;
            const CGFloat x = left + (right - left) * (engines <= 1u ? 0.0 : static_cast<CGFloat>(voice) / static_cast<CGFloat>(engines - 1u));
            const CGFloat yy = y + 5.0 + (1.0 - std::clamp(value, 0.0, 1.0)) * (rowH - 10.0);
            const CGFloat size = voice == _selectedVoice ? 4.8 : (engines > 36u ? 2.4 : 3.2);
            [[self voiceColor:voice selected:voice == _selectedVoice] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - size * 0.5, yy - size * 0.5, size, size)] fill];
        }
    }

    const auto& ampValues = _plugin->params.bpAmp;
    const double amp = _plugin->params.voiceBreakpointsEnabled ? ampValues[_selectedVoice] : 1.0;
    NSString* readout = [NSString stringWithFormat:@"E%02u  %@  AMP %.2f", _selectedVoice + 1u, _plugin->params.voiceBreakpointsEnabled ? @"BREAKPOINTS ON" : @"MACRO PREVIEW", amp];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
    [@"GLOBAL SLIDERS TRIM CURVES     CLICK/DRAG TO SET PER-ENGINE VALUES" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19) withAttributes:valueAttrs];
}

- (void)drawListenerMenuLabel:(NSString*)label value:(NSString*)value
    rect:(NSRect)rect style:(const s3g::clap_gui::Style&)style
{
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [label drawAtPoint:NSMakePoint(rect.origin.x - 72.0, rect.origin.y + 1.0)
        withAttributes:labels];
    [style.strip setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    [style.fill setFill];
    NSRectFill(NSMakeRect(rect.origin.x + 1.0, rect.origin.y + 1.0,
        2.0, rect.size.height - 2.0));
    [value drawAtPoint:NSMakePoint(rect.origin.x + 8.0, rect.origin.y + 2.0)
        withAttributes:values];
    [@"v" drawAtPoint:NSMakePoint(NSMaxX(rect) - 13.0, rect.origin.y + 1.0)
        withAttributes:values];
}

- (void)drawListenerVoiceLabel:(const s3g::clap_gui::Style&)style
{
    const NSRect rect = [self listenVoiceRect];
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [@"NODE" drawAtPoint:NSMakePoint(rect.origin.x - 72.0, rect.origin.y + 1.0)
        withAttributes:labels];
    [style.strip setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    [style.fill setFill];
    NSRectFill(NSMakeRect(rect.origin.x + 1.0, rect.origin.y + 1.0,
        2.0, rect.size.height - 2.0));
    [@"<" drawAtPoint:NSMakePoint(rect.origin.x + 8.0, rect.origin.y + 1.0)
        withAttributes:values];
    const uint32_t engine = _plugin->guiNodeEngine[_selectedVoice].load(
        std::memory_order_relaxed);
    [[NSString stringWithFormat:@"N%02u E%02u", _selectedVoice + 1u, engine + 1u]
        drawAtPoint:NSMakePoint(rect.origin.x + 25.0, rect.origin.y + 2.0)
        withAttributes:values];
    [@">" drawAtPoint:NSMakePoint(NSMaxX(rect) - 15.0, rect.origin.y + 1.0)
        withAttributes:values];
}

- (void)drawListenerPage:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [@"AMBISONIC AUDITORY BODY / NEGATIVE-FEEDBACK REGISTER" drawAtPoint:NSMakePoint(
        field.origin.x + 10.0, field.origin.y + 10.0) withAttributes:valueAttrs];

    const uint32_t voices = std::clamp<uint32_t>(
        _plugin->params.voices, 1u, s3g::kAmbiWranglerMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    const uint32_t pickupCount =
        _plugin->params.pickupSet == s3g::AmbiWranglerPickupSet::Tetra4 ? 4u : 8u;
    const uint32_t readEar = _plugin->guiReadEar[_selectedVoice].load(
        std::memory_order_relaxed) % pickupCount;
    const float activity = _plugin->guiListenerActivity.load(std::memory_order_relaxed);
    const float tension = std::clamp(
        _plugin->guiListenerTension.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float capture = std::clamp(
        _plugin->guiCapture[_selectedVoice].load(std::memory_order_relaxed),
        0.0f, 1.0f);
    const float evolutionRate = capture > 0.000001f
        ? std::clamp(
            _plugin->guiEvolutionRate[_selectedVoice].load(
                std::memory_order_relaxed), 0.03f, 1.0f)
        : 1.0f;
    const float couplingRate = capture > 0.000001f
        ? std::clamp(
            _plugin->guiCouplingRate[_selectedVoice].load(
                std::memory_order_relaxed), 0.22f, 1.0f)
        : 1.0f;
    const float topologyRate = std::clamp(
        _plugin->guiTopologyRate.load(std::memory_order_relaxed),
        0.12f, 1.0f);
    const bool registerHeld =
        _plugin->guiRegisterHeld[_selectedVoice].load(
            std::memory_order_relaxed) != 0u;
    const uint64_t heldClockCount =
        _plugin->guiHeldClockCount[_selectedVoice].load(
            std::memory_order_relaxed);
    const uint32_t engineIndex = _plugin->guiNodeEngine[_selectedVoice].load(
        std::memory_order_relaxed);
    const bool settleResponse =
        _plugin->params.listenerResponse
            == s3g::AmbiWranglerListenerResponse::Settle;
    const float returned = _plugin->guiFieldReturn[_selectedVoice].load(
        std::memory_order_relaxed);
    const uint32_t comparatorBit = _plugin->guiComparatorBit[_selectedVoice].load(
        std::memory_order_relaxed);
    const uint32_t auditoryBit = _plugin->guiAuditoryBit[_selectedVoice].load(
        std::memory_order_relaxed);
    const uint32_t writtenBit = _plugin->guiWrittenBit[_selectedVoice].load(
        std::memory_order_relaxed);
    const float writePulse = _plugin->guiFieldWritePulse[_selectedVoice].load(
        std::memory_order_relaxed);
    const uint32_t word = _plugin->guiRegister[_selectedVoice].load(
        std::memory_order_relaxed);
    const uint32_t rungSize = std::clamp<uint32_t>(_plugin->params.rungSize, 2u, 8u);

    constexpr float k = 0.57735026919f;
    static constexpr std::array<s3g::Vec3, 4> tetra {{
        { k, k, k }, { -k, -k, k }, { -k, k, -k }, { k, -k, -k },
    }};
    const auto& cube = s3g::ambiFieldListenerCubeDirections();
    const NSPoint center = NSMakePoint(NSMidX(field), field.origin.y + 215.0);
    const CGFloat radius = 148.0;
    [s3g::clap_gui::color(0x303030) setStroke];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        center.x - radius, center.y - radius, radius * 2.0, radius * 2.0)] stroke];

    std::array<NSPoint, s3g::kAmbiFieldListenerMaxLobes> ears {};
    const float cameraAz = static_cast<float>(38.0 * s3g::kPi / 180.0);
    const float cameraEl = static_cast<float>(32.0 * s3g::kPi / 180.0);
    for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
        const s3g::Vec3 direction = pickupCount == 4u ? tetra[ear] : cube[ear];
        const float x1 = std::cos(cameraAz) * direction.x
            - std::sin(cameraAz) * direction.y;
        const float y1 = std::sin(cameraAz) * direction.x
            + std::cos(cameraAz) * direction.y;
        const float y2 = std::cos(cameraEl) * y1
            - std::sin(cameraEl) * direction.z;
        ears[ear] = NSMakePoint(center.x + x1 * radius, center.y - y2 * radius);
    }

    const CGFloat cellStep = 29.0;
    const CGFloat registerWidth = cellStep * static_cast<CGFloat>(rungSize);
    const CGFloat registerX = center.x - registerWidth * 0.5;
    const CGFloat registerY = center.y - 11.0;
    const NSRect registerBody = NSMakeRect(
        registerX - 8.0, registerY - 8.0, registerWidth + 10.0, 38.0);
    [s3g::clap_gui::color(0x101010) setFill];
    NSRectFill(registerBody);
    [s3g::clap_gui::color(0x505050) setStroke];
    NSFrameRect(registerBody);

    const NSPoint inputPoint = NSMakePoint(
        registerX + (rungSize - 1u) * cellStep + 10.0, registerY + 10.0);
    [s3g::clap_gui::color(0x707070, 0.42) setStroke];
    NSBezierPath* returnPath = [NSBezierPath bezierPath];
    [returnPath moveToPoint:ears[readEar]];
    [returnPath lineToPoint:inputPoint];
    [returnPath setLineWidth:0.8 + std::fabs(returned) * 2.4];
    [returnPath stroke];

    for (uint32_t bit = 0u; bit < rungSize; ++bit) {
        const uint32_t shift = rungSize - 1u - bit;
        const bool set = ((word >> shift) & 1u) != 0u;
        const bool inputCell = bit == rungSize - 1u;
        const bool fieldWrite = inputCell && writePulse > 0.08f;
        const int shade = fieldWrite ? 0xe8
            : (set ? 0xb0 : 0x24);
        [s3g::clap_gui::color((shade << 16) | (shade << 8) | shade, 0.96) setFill];
        const NSRect cell = NSMakeRect(
            registerX + bit * cellStep, registerY, 20.0, 20.0);
        NSRectFill(cell);
        [s3g::clap_gui::color(inputCell ? 0xb8b8b8 : 0x555555,
            fieldWrite ? 1.0 : 0.72) setStroke];
        NSFrameRect(cell);
        if (bit + 1u < rungSize) {
            [s3g::clap_gui::color(0x686868, 0.62) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMaxX(cell), NSMidY(cell))
                toPoint:NSMakePoint(NSMaxX(cell) + cellStep - 20.0, NSMidY(cell))];
        }
        NSString* value = set ? @"1" : @"0";
        [value drawAtPoint:NSMakePoint(NSMidX(cell) - 3.0, NSMidY(cell) - 7.0)
            withAttributes:s3g::clap_gui::textAttrs(
                s3g::clap_gui::color(set || fieldWrite ? 0x101010 : 0x8c8c8c), 8.0)];
    }

    for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
        const float energy = std::clamp(
            _plugin->guiListenerEnvelope[ear].load(std::memory_order_relaxed)
                / 0.08f, 0.0f, 1.0f);
        const CGFloat halo = 8.0 + std::sqrt(energy) * 18.0;
        [[s3g::clap_gui::color(0xc8c8c8, 0.04 + energy * 0.20)
            colorWithAlphaComponent:0.04 + energy * 0.20] setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            ears[ear].x - halo, ears[ear].y - halo, halo * 2.0, halo * 2.0)] fill];
        const CGFloat diamondRadius = ear == readEar ? 7.5 : 6.0;
        NSBezierPath* diamond = [NSBezierPath bezierPath];
        [diamond moveToPoint:NSMakePoint(ears[ear].x, ears[ear].y - diamondRadius)];
        [diamond lineToPoint:NSMakePoint(ears[ear].x + diamondRadius, ears[ear].y)];
        [diamond lineToPoint:NSMakePoint(ears[ear].x, ears[ear].y + diamondRadius)];
        [diamond lineToPoint:NSMakePoint(ears[ear].x - diamondRadius, ears[ear].y)];
        [diamond closePath];
        [s3g::clap_gui::color(ear == readEar ? 0xd6d6d6 : 0x8c8c8c,
            0.34 + energy * 0.66) setFill];
        [diamond fill];
        if (ear == readEar) {
            [s3g::clap_gui::color(0xf0f0f0, 0.92) setStroke];
            [diamond stroke];
        }
        [[NSString stringWithFormat:@"E%u", ear + 1u] drawAtPoint:NSMakePoint(
            ears[ear].x + 8.0, ears[ear].y - 6.0) withAttributes:valueAttrs];
    }

    NSString* registerStatus = settleResponse
        ? [NSString stringWithFormat:
            @"N%02u / ENGINE %02u  REGISTER 0x%02X  /  %@  %llu CLOCKS",
            _selectedVoice + 1u, engineIndex + 1u, word & 0xffu,
            registerHeld ? @"HELD" : @"OPEN",
            static_cast<unsigned long long>(heldClockCount)]
        : [NSString stringWithFormat:
            @"N%02u / ENGINE %02u  REGISTER 0x%02X  /  READ E%u",
            _selectedVoice + 1u, engineIndex + 1u, word & 0xffu,
            readEar + 1u];
    [registerStatus drawAtPoint:NSMakePoint(
        center.x - 96.0, field.origin.y + 374.0) withAttributes:valueAttrs];
    NSString* bitStatus = settleResponse
        ? [NSString stringWithFormat:
            @"TENSION %3.0f%%    CAPTURE %3.0f%%    EVOLVE x%.2f    COUPLE x%.2f",
            tension * 100.0f, capture * 100.0f,
            evolutionRate, couplingRate]
        : [NSString stringWithFormat:
            @"COMPARATOR %u    EAR %u    WRITE %u    RETURN %+.3f",
            comparatorBit, auditoryBit, writtenBit, returned];
    [bitStatus drawAtPoint:NSMakePoint(
        center.x - 126.0, field.origin.y + 392.0) withAttributes:valueAttrs];

    static constexpr const char* modeNames[] = {
        "TRACE", "RING", "CROSS", "BALANCE"
    };
    [self drawListenerVoiceLabel:style];
    [self drawListenerMenuLabel:@"EARS"
        value:pickupCount == 4u ? @"TETRA 4" : @"CUBE 8"
        rect:[self pickupSetRect] style:style];
    [self drawListenerMenuLabel:@"READ"
        value:[NSString stringWithUTF8String:modeNames[
            std::min<uint32_t>(static_cast<uint32_t>(_plugin->params.listenMode), 3u)]]
        rect:[self listenModeRect] style:style];
    [self drawListenerMenuLabel:@"RESPONSE"
        value:settleResponse ? @"SETTLE" : @"WRITE"
        rect:[self listenerResponseRect] style:style];

    if (settleResponse) {
        [self drawSlider:@"VISCOSITY" param:kSettleAmountParamId
            value:_plugin->params.settleAmount attrs:attrs
            valueAttrs:valueAttrs style:style];
        [self drawSlider:@"CALM TARGET" param:kSettleTargetParamId
            value:_plugin->params.settleTarget attrs:attrs
            valueAttrs:valueAttrs style:style];
        [self drawSlider:@"RECOVERY" param:kSettleRecoveryParamId
            value:_plugin->params.settleRecoverySeconds attrs:attrs
            valueAttrs:valueAttrs style:style];
        [@"CAPTURE" drawAtPoint:NSMakePoint(330.0, 575.0)
            withAttributes:attrs];
        const NSRect tensionTrack = NSMakeRect(422.0, 579.0, 82.0, 5.0);
        [style.strip setFill];
        NSRectFill(tensionTrack);
        [style.grid setStroke];
        NSFrameRect(tensionTrack);
        [style.fill setFill];
        NSRectFill(NSMakeRect(
            tensionTrack.origin.x + 1.0, tensionTrack.origin.y + 1.0,
            (tensionTrack.size.width - 2.0) * capture,
            tensionTrack.size.height - 2.0));
        [[NSString stringWithFormat:@"%3.0f%% / E%.2f / T%.2f",
            capture * 100.0f, evolutionRate, topologyRate]
            drawAtPoint:NSMakePoint(510.0, 574.0) withAttributes:valueAttrs];
    } else {
        [self drawSlider:@"FIELD RETURN" param:kFieldReturnParamId
            value:_plugin->params.fieldReturn attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"PROPAGATION" param:kPropagationParamId
            value:_plugin->params.propagation attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"FIELD WRITE" param:kFieldWriteParamId
            value:_plugin->params.fieldWrite attrs:attrs valueAttrs:valueAttrs style:style];
        [self drawSlider:@"REG MOTION" param:kRegisterMotionParamId
            value:_plugin->params.registerMotion attrs:attrs valueAttrs:valueAttrs style:style];
    }

    const NSRect controlHeader = NSMakeRect(field.origin.x, 604.0, field.size.width, 27.0);
    s3g::clap_gui::drawHeaderButton([self listenEnableRect], controlHeader,
        _plugin->params.listeningEnabled ? @"LISTEN ON" : @"LISTEN OFF",
        _plugin->params.listeningEnabled != 0u, attrs, style);
    s3g::clap_gui::drawHeaderButton([self returnBypassRect], controlHeader,
        settleResponse
            ? @"RETURN INERT"
            : (_plugin->params.returnBypass ? @"BYPASSED" : @"RETURN ACTIVE"),
        !settleResponse && _plugin->params.returnBypass != 0u, attrs, style);
    NSString* status = settleResponse
        ? [NSString stringWithFormat:
            @"EARS %3.0f%%  CAPTURE %3.0f%%  EVOLVE x%.2f  TOPO x%.2f",
            activity * 100.0f, capture * 100.0f,
            evolutionRate, topologyRate]
        : [NSString stringWithFormat:@"EARS %3.0f%%  WRITE %3.0f%%  RETURN %3.0f%%",
            activity * 100.0f,
            _plugin->guiListenerEnableGain.load(std::memory_order_relaxed) * 100.0f,
            _plugin->guiReturnEnableGain.load(std::memory_order_relaxed) * 100.0f];
    s3g::clap_gui::drawRightStatus(
        status, NSMaxX(field) - 4.0, 610.0, valueAttrs, 0.0);
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:0], header, @"FIELD", _fieldPage == 0, attrs, style);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:1], header, @"CURVE", _fieldPage == 1, attrs, style);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:2], header, @"LISTEN", _fieldPage == 2, attrs, style);
    if (_fieldPage == 1) {
        [self drawBreakpointEditor:attrs valueAttrs:valueAttrs style:style];
        return;
    }
    if (_fieldPage == 2) {
        [self drawListenerPage:attrs valueAttrs:valueAttrs style:style];
        return;
    }
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) s3g::clap_gui::drawHeaderButton([self viewButtonRect:i], header, labels[i], i == _viewMode, attrs, style);

    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1, 1)] addClip];
    const CGFloat radius = std::min(field.size.width, field.size.height) * 0.36 * _viewZoom;
    [s3g::clap_gui::color(0x303030) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMidX(field) - radius, NSMidY(field) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];
    [s3g::clap_gui::color(0x242424) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMinX(field) + 18, NSMidY(field)) toPoint:NSMakePoint(NSMaxX(field) - 18, NSMidY(field))];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(field), NSMinY(field) + 18) toPoint:NSMakePoint(NSMidX(field), NSMaxY(field) - 18)];

    const uint32_t voices = std::clamp<uint32_t>(
        _plugin->params.voices, 1u, s3g::kAmbiWranglerMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<NSPoint, s3g::kAmbiWranglerMaxVoices> projected {};
    for (uint32_t voice = 0; voice < voices; ++voice) projected[voice] = [self projectVoice:voice depth:nullptr];
    [s3g::clap_gui::color(0x5c5c5c, 0.18) setStroke];
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const uint32_t next = (voice + 1u) % voices;
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:projected[voice]];
        [path lineToPoint:projected[next]];
        [path setLineWidth:0.55];
        [path stroke];
    }
    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x080808), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const BOOL selected = voice == _selectedVoice;
        const float energy = _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
        const float mask = std::clamp(_plugin->guiMask[voice].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const CGFloat base = voices > 32u ? 7.0 : 9.0;
        const CGFloat size = ((selected ? base + 5.0 : base) + std::clamp(std::sqrt(std::max(0.0f, energy)) * 34.0f, 0.0f, 8.0f)) * (0.55 + mask * 0.45);
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        if (mask > 0.12f) {
            const CGFloat halo = size * (1.2 + mask * 1.4);
            NSRect haloRect = NSMakeRect(projected[voice].x - halo * 0.5, projected[voice].y - halo * 0.5, halo, halo);
            [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:0.08 + mask * 0.16] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:haloRect] fill];
        }
        [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:(selected ? 0.98 : 0.22 + mask * 0.70)] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe6e6e6 : 0x4f4f4f, selected ? 1.0 : 0.25 + mask * 0.55) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        if (mask > 0.25f || selected) {
            [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5, NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
        }
    }
    [NSGraphicsContext restoreGraphicsState];

    const float az = _plugin->guiAzimuth[_selectedVoice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[_selectedVoice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[_selectedVoice].load(std::memory_order_relaxed);
    const float energy = _plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed);
    const float mask = _plugin->guiMask[_selectedVoice].load(std::memory_order_relaxed);
    const uint32_t engine = _plugin->guiNodeEngine[_selectedVoice].load(
        std::memory_order_relaxed);
    NSString* readout = [NSString stringWithFormat:
        @"N%02u/E%02u  AZ%+.1f  EL%+.1f  D%.2f  M%.2f  A%.3f",
        _selectedVoice + 1u, engine + 1u, az, el, dist, mask, energy];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
    [@"RUNG REGISTER FIELD     SHARED ENGINES ACROSS SPATIAL NODES     ACN/SN3D" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19) withAttributes:valueAttrs];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    char display[64] {};
    if (param == kRateAParamId || param == kRateBParamId) {
        const uint32_t mode = param == kRateAParamId ? _plugin->params.rateModeA : _plugin->params.rateModeB;
        const double hz = rateNormToHzForDisplay(value, mode);
        if (hz < 1.0) std::snprintf(display, sizeof(display), "%.4f Hz", hz);
        else if (hz < 100.0) std::snprintf(display, sizeof(display), "%.2f Hz", hz);
        else std::snprintf(display, sizeof(display), "%.0f Hz", hz);
    } else {
        paramsValueToText(nullptr, param, value, display, sizeof(display));
    }
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:display], sliderNorm(*spec, value), spec->y,
        attrs, valueAttrs, style, spec->panelX + 16, spec->panelX + 108, spec->panelX + 196, 82);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value panelX:(CGFloat)panelX y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (NSString*)presetDisplayName
{
    if (_plugin->customPresetName[0]) return [NSString stringWithFormat:@"CUSTOM: %s", _plugin->customPresetName];
    return [NSString stringWithUTF8String:s3g::ambiWranglerFactoryPresetInfo(_plugin->presetIndex).name];
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->params;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 228, style);
    s3g::clap_gui::drawPanelHeader(@"OSCILLATORS", true, 630, 42, 250, 21, attrs, style);
    s3g::clap_gui::drawHeaderButton([self circuitLawButtonRect],
        NSMakeRect(630, 42, 250, 21),
        p.circuitLaw == s3g::AmbiWranglerCircuitLaw::Bounded
            ? @"CIR BOUNDED" : @"CIR LEGACY",
        p.circuitLaw == s3g::AmbiWranglerCircuitLaw::Bounded,
        attrs, style);
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] panelX:630 y:72 attrs:attrs valueAttrs:valueAttrs style:style];
    static constexpr const char* kRateRangeNames[] = { "LOW", "SINGLE", "DOUBLE" };
    [self drawMenu:@"RNG A" value:[NSString stringWithUTF8String:kRateRangeNames[std::min<uint32_t>(p.rateModeA, 2u)]] panelX:630 y:96 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"RNG B" value:[NSString stringWithUTF8String:kRateRangeNames[std::min<uint32_t>(p.rateModeB, 2u)]] panelX:630 y:120 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"NODES" param:kVoicesParamId value:p.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENGINES" param:kEnginesParamId value:p.engines attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RATE A" param:kRateAParamId value:p.rateA attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RATE B" param:kRateBParamId value:p.rateB attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPREAD" param:kSpreadParamId value:p.spread attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEV" param:kDeviationParamId value:p.deviation attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 282, 250, 218, style);
    s3g::clap_gui::drawPanelHeader(@"CROSS / RUNGLER", true, 630, 282, 250, 21, attrs, style);
    static constexpr const char* kLoopNames[] = { "OFF", "LOOP", "XOR" };
    [self drawMenu:@"LOOP" value:[NSString stringWithUTF8String:kLoopNames[std::min<uint32_t>(p.rungLoop, 2u)]] panelX:630 y:308 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FM A>B" param:kFmAtoBParamId value:p.fmAtoB attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FM B>A" param:kFmBtoAParamId value:p.fmBtoA attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RUNG A" param:kRunglerAParamId value:p.runglerA attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RUNG B" param:kRunglerBParamId value:p.runglerB attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SIZE" param:kRungSizeParamId value:p.rungSize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CHANGE" param:kChangeParamId value:p.change attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"THRESH" param:kThresholdParamId value:p.threshold attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 512, 250, 186, style);
    s3g::clap_gui::drawPanelHeader(@"FILTER / OUTPUT", true, 630, 512, 250, 21, attrs, style);
    [self drawSlider:@"COLOR" param:kColorParamId value:p.color attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FIL FRQ" param:kFilterParamId value:p.filter attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FIL RES" param:kResonanceParamId value:p.resonance attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FIL RUN" param:kFilterRunParamId value:p.filterRun attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FIL SWP" param:kFilterSweepParamId value:p.filterSweep attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SAT" param:kSaturationParamId value:p.saturation attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 722, 250, 98, style);
    s3g::clap_gui::drawPanelHeader(@"PULSE WIDTH", true, 630, 722, 250, 21, attrs, style);
    [self drawSlider:@"PWM A" param:kPwmAParamId value:p.pwmA attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"PWM B" param:kPwmBParamId value:p.pwmB attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 254, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY", true, 896, 42, 246, 21, attrs, style);
    [self drawMenu:@"SHAPE" value:[NSString stringWithUTF8String:s3g::topologyShapeName(p.topologyShape)] panelX:896 y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"MOTION" value:[NSString stringWithUTF8String:s3g::topologyMotionModeName(p.topologyMotion)] panelX:896 y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RATE" param:kTopologyRateParamId value:p.topologyRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AMT" param:kTopologyAmountParamId value:p.topologyAmount attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEPTH" param:kTopologyDepthParamId value:p.topologyDepth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SCALE" param:kTopologyScaleParamId value:p.topologyScale attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"COLL" param:kTopologyCollapseParamId value:p.topologyCollapse attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 308, 246, 150, style);
    s3g::clap_gui::drawPanelHeader(@"PROJECTION", true, 896, 308, 246, 21, attrs, style);
    [self drawSlider:@"AZIM" param:kAzimuthParamId value:p.centerAzimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEV" param:kElevationParamId value:p.centerElevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DIST" param:kDistanceParamId value:p.centerDistance attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FOLLOW" param:kSpatialFollowParamId value:p.spatialFollow attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 472, 246, 146, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE MASK", true, 896, 472, 246, 21, attrs, style);
    static constexpr const char* kMaskNames[] = { "ALL", "BREATH", "CHOIR", "CELLS", "SPARK", "WEAVE" };
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:kMaskNames[std::min<uint32_t>(p.maskMode, 5u)]] panelX:896 y:508 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FIELD" param:kFieldParamId value:p.field attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEPTH" param:kMaskDepthParamId value:p.maskDepth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RATE" param:kMaskRateParamId value:p.maskRateHz attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 632, 246, 214, style);
    s3g::clap_gui::drawPanelHeader(@"OSC SHAPE", true, 896, 632, 246, 21, attrs, style);
    [self drawMenu:@"IN A" value:(p.inputA == 0u ? @"SQUARE" : @"TRI") panelX:896 y:668 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"IN B" value:(p.inputB == 0u ? @"SQUARE" : @"TRI") panelX:896 y:694 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RAMP A" param:kRampAParamId value:p.rampA attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RAMP B" param:kRampBParamId value:p.rampB attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SNAP" param:kSnapParamId value:p.snap attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DECAY" param:kSnapDecayParamId value:p.snapDecay attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"OUT" param:kOutputParamId value:p.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return [self presetMenuRect];
    case 2: return NSMakeRect(738, 95, 124, 15);
    case 3: return NSMakeRect(738, 119, 124, 15);
    case 4: return NSMakeRect(1004, 77, 124, 15);
    case 5: return NSMakeRect(1004, 103, 124, 15);
    case 6: return NSMakeRect(1004, 507, 124, 15);
    case 7: return NSMakeRect(1004, 667, 124, 15);
    case 8: return NSMakeRect(1004, 693, 124, 15);
    case 9: return NSMakeRect(738, 307, 124, 15);
    case 10: return NSMakeRect(738, 71, 124, 15);
    case 12: return [self pickupSetRect];
    case 13: return [self listenModeRect];
    case 14: return [self listenerResponseRect];
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return s3g::kAmbiWranglerFactoryPresetCount;
    case 2: return 3u;
    case 3: return 3u;
    case 4: return s3g::kTopologyShapeCount;
    case 5: return s3g::kTopologyMotionModeCount;
    case 6: return 6u;
    case 7: return 2u;
    case 8: return 2u;
    case 9: return 3u;
    case 10: return 7u;
    case 12: return 2u;
    case 13: return 4u;
    case 14: return 2u;
    default: return 0u;
    }
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _menuItemCount = [self menuCount:menu];
    _hoverMenuItem = -1;
    const NSRect box = [self menuBoxRect:menu];
    const CGFloat itemH = 21.0;
    CGFloat y = NSMaxY(box) + 2.0;
    const CGFloat height = itemH * _menuItemCount;
    if (y + height > kGuiHeight - 8.0) y = box.origin.y - height - 2.0;
    _openMenuRect = NSMakeRect(box.origin.x, y, box.size.width, height);
    [self setNeedsDisplay:YES];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* rateItems[] = { @"LOW", @"SINGLE", @"DOUBLE" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* loopItems[] = { @"OFF", @"LOOP", @"XOR" };
    static NSString* maskItems[] = { @"ALL", @"BREATH", @"CHOIR", @"CELLS", @"SPARK", @"WEAVE" };
    static NSString* inputItems[] = { @"SQUARE", @"TRI" };
    static NSString* pickupItems[] = { @"TETRA 4", @"CUBE 8" };
    static NSString* listenItems[] = { @"TRACE", @"RING", @"CROSS", @"BALANCE" };
    static NSString* responseItems[] = { @"WRITE", @"SETTLE" };
    static NSString* presetItems[s3g::kAmbiWranglerFactoryPresetCount];
    static NSString* shapeItems[s3g::kTopologyShapeCount];
    static NSString* motionItems[s3g::kTopologyMotionModeCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t i = 0; i < s3g::kAmbiWranglerFactoryPresetCount; ++i) presetItems[i] = [[NSString stringWithUTF8String:s3g::ambiWranglerFactoryPresetInfo(i).name] retain];
        for (uint32_t i = 0; i < s3g::kTopologyShapeCount; ++i) shapeItems[i] = [[NSString stringWithUTF8String:s3g::topologyShapeName(i)] retain];
        for (uint32_t i = 0; i < s3g::kTopologyMotionModeCount; ++i) motionItems[i] = [[NSString stringWithUTF8String:s3g::topologyMotionModeName(i)] retain];
    });
    NSString** items = presetItems;
    int selected = static_cast<int>(_plugin->presetIndex);
    if (_openMenu == 2) {
        items = rateItems;
        selected = static_cast<int>(_plugin->params.rateModeA);
    } else if (_openMenu == 3) {
        items = rateItems;
        selected = static_cast<int>(_plugin->params.rateModeB);
    } else if (_openMenu == 4) {
        items = shapeItems;
        selected = static_cast<int>(_plugin->params.topologyShape);
    } else if (_openMenu == 5) {
        items = motionItems;
        selected = static_cast<int>(_plugin->params.topologyMotion);
    } else if (_openMenu == 6) {
        items = maskItems;
        selected = static_cast<int>(_plugin->params.maskMode);
    } else if (_openMenu == 7) {
        items = inputItems;
        selected = static_cast<int>(_plugin->params.inputA);
    } else if (_openMenu == 8) {
        items = inputItems;
        selected = static_cast<int>(_plugin->params.inputB);
    } else if (_openMenu == 9) {
        items = loopItems;
        selected = static_cast<int>(_plugin->params.rungLoop);
    } else if (_openMenu == 10) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
    } else if (_openMenu == 12) {
        items = pickupItems;
        selected = static_cast<int>(_plugin->params.pickupSet);
    } else if (_openMenu == 13) {
        items = listenItems;
        selected = static_cast<int>(_plugin->params.listenMode);
    } else if (_openMenu == 14) {
        items = responseItems;
        selected = static_cast<int>(_plugin->params.listenerResponse);
    }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    [@"s3g AMBI WRANGLER ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    s3g::clap_gui::drawMenu(@"PRESET", [self presetDisplayName], 14, attrs, valueAttrs, style, 320, 382, 190);
    s3g::clap_gui::drawHeaderActionButton([self savePresetButtonRect], [self savePresetButtonRect], @"SAVE", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self loadPresetButtonRect], [self loadPresetButtonRect], @"LOAD", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self randomizeButtonRect], [self randomizeButtonRect], @"RANDOM", attrs, style);
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)), kGuiWidth, 14, valueAttrs, 18);
    [self drawField:attrs valueAttrs:valueAttrs style:style];
    [self drawPanels:attrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (int)hitVoice:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiWranglerMaxVoices);
    int best = -1;
    CGFloat bestDistance = 15.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const NSPoint projected = [self projectVoice:voice depth:nullptr];
        const CGFloat distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(voice);
        }
    }
    return best;
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)point
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    applyParam(*_plugin, param, sliderValue(*spec, point));
    [self setNeedsDisplay:YES];
}

- (BOOL)setBreakpointAtPoint:(NSPoint)point
{
    const NSRect field = [self fieldRect];
    if (!NSPointInRect(point, field)) return NO;
    const uint32_t voices = std::min<uint32_t>(
        std::clamp<uint32_t>(_plugin->params.engines, 1u, s3g::kAmbiWranglerMaxVoices),
        std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiWranglerMaxVoices));
    const CGFloat labelW = 54.0;
    const CGFloat left = field.origin.x + labelW + 10.0;
    const CGFloat right = NSMaxX(field) - 12.0;
    const CGFloat top = field.origin.y + 28.0;
    const CGFloat bottom = NSMaxY(field) - 30.0;
    if (point.y < top || point.y > bottom) return NO;
    const CGFloat rowH = (bottom - top) / 13.0;
    const uint32_t row = std::min<uint32_t>(12u, static_cast<uint32_t>((point.y - top) / rowH));
    const double voicePos = std::clamp((static_cast<double>(point.x) - left) / std::max(1.0, static_cast<double>(right - left)), 0.0, 1.0);
    const uint32_t voice = std::min<uint32_t>(voices - 1u, static_cast<uint32_t>(std::lround(voicePos * static_cast<double>(voices - 1u))));
    const CGFloat rowTop = top + rowH * static_cast<CGFloat>(row);
    const double value = std::clamp(1.0 - (static_cast<double>(point.y - rowTop) - 5.0) / std::max(1.0, static_cast<double>(rowH - 10.0)), 0.0, 1.0);
    if (!_plugin->params.voiceBreakpointsEnabled) {
        seedBreakpointsFromMacros(_plugin->params);
        _plugin->params.voiceBreakpointsEnabled = true;
    }
    const double curve = row == 12u ? value : 0.5 + (value - breakpointFallback(_plugin->params, row)) / breakpointRange(row);
    breakpointArray(_plugin->params, row)[voice] = static_cast<float>(std::clamp(curve, 0.0, 1.0));
    _selectedVoice = voice;
    _dragBreakpointRow = static_cast<int>(row);
    _plugin->engine.setParams(_plugin->params);
    _plugin->params = _plugin->engine.params();
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 2) applyParam(*_plugin, kRateModeAParamId, hit);
            else if (_openMenu == 3) applyParam(*_plugin, kRateModeBParamId, hit);
            else if (_openMenu == 4) applyParam(*_plugin, kTopologyShapeParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kTopologyMotionParamId, hit);
            else if (_openMenu == 6) applyParam(*_plugin, kMaskModeParamId, hit);
            else if (_openMenu == 7) applyParam(*_plugin, kInputAParamId, hit);
            else if (_openMenu == 8) applyParam(*_plugin, kInputBParamId, hit);
            else if (_openMenu == 9) applyParam(*_plugin, kRungLoopParamId, hit);
            else if (_openMenu == 10) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 12) applyParam(*_plugin, kPickupSetParamId, hit);
            else if (_openMenu == 13) applyParam(*_plugin, kListenModeParamId, hit);
            else if (_openMenu == 14) applyParam(*_plugin, kListenerResponseParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self presetMenuRect])) { [self openMenu:1]; return; }
    if (NSPointInRect(point, [self savePresetButtonRect])) { [self saveCustomPreset]; return; }
    if (NSPointInRect(point, [self loadPresetButtonRect])) { [self loadCustomPreset]; return; }
    if (NSPointInRect(point, [self randomizeButtonRect])) {
        randomizeSafe(*_plugin);
        _selectedVoice = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self circuitLawButtonRect])) {
        applyParam(*_plugin, kCircuitLawParamId,
            _plugin->params.circuitLaw == s3g::AmbiWranglerCircuitLaw::Legacy
                ? 1.0 : 0.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(738, 71, 124, 15))) { [self openMenu:10]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 95, 124, 15))) { [self openMenu:2]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 119, 124, 15))) { [self openMenu:3]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 77, 124, 15))) { [self openMenu:4]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 103, 124, 15))) { [self openMenu:5]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 507, 124, 15))) { [self openMenu:6]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 667, 124, 15))) { [self openMenu:7]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 693, 124, 15))) { [self openMenu:8]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 307, 124, 15))) { [self openMenu:9]; return; }
    const NSRect panel = [self fieldPanelRect];
    if (NSPointInRect(point, panel)) {
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(point, [self pageButtonRect:i])) {
                _fieldPage = i;
                if (_fieldPage == 1 && !_plugin->params.voiceBreakpointsEnabled) seedBreakpointsFromMacros(_plugin->params);
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (_fieldPage == 1) {
            [self setBreakpointAtPoint:point];
            return;
        }
        if (_fieldPage == 2) {
            if (NSPointInRect(point, [self listenVoiceRect])) {
                const uint32_t voiceCount = std::clamp<uint32_t>(
                    _plugin->params.voices, 1u, s3g::kAmbiWranglerMaxVoices);
                _selectedVoice = point.x < NSMidX([self listenVoiceRect])
                    ? (_selectedVoice + voiceCount - 1u) % voiceCount
                    : (_selectedVoice + 1u) % voiceCount;
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, [self pickupSetRect])) {
                [self openMenu:12];
                return;
            }
            if (NSPointInRect(point, [self listenModeRect])) {
                [self openMenu:13];
                return;
            }
            if (NSPointInRect(point, [self listenerResponseRect])) {
                [self openMenu:14];
                return;
            }
            if (NSPointInRect(point, [self listenEnableRect])) {
                applyParam(*_plugin, kListeningEnabledParamId,
                    _plugin->params.listeningEnabled ? 0.0 : 1.0);
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, [self returnBypassRect])) {
                if (_plugin->params.listenerResponse
                    == s3g::AmbiWranglerListenerResponse::Write) {
                    applyParam(*_plugin, kReturnBypassParamId,
                        _plugin->params.returnBypass ? 0.0 : 1.0);
                }
                [self setNeedsDisplay:YES];
                return;
            }
        } else {
            for (int i = 0; i < 2; ++i) {
                if (NSPointInRect(point, [self zoomButtonRect:i])) {
                    _viewZoom = std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
                    [self storeViewState];
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
            for (int i = 0; i < 3; ++i) {
                if (NSPointInRect(point, [self viewButtonRect:i])) {
                    [self setViewPreset:i];
                    return;
                }
            }
            const int hit = [self hitVoice:point];
            if (hit >= 0) {
                _selectedVoice = static_cast<uint32_t>(hit);
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, [self fieldRect])) {
                _dragView = YES;
                _lastDragPoint = point;
                _viewMode = -1;
                [self storeViewState];
                return;
            }
        }
    }
    _dragParam = 0;
    for (const auto& spec : kGuiSliders) {
        if (isListenerGuiParam(spec.id) && _fieldPage != 2) continue;
        const bool settleResponse = _plugin->params.listenerResponse
            == s3g::AmbiWranglerListenerResponse::Settle;
        const bool settleControl = spec.id == kSettleAmountParamId
            || spec.id == kSettleTargetParamId
            || spec.id == kSettleRecoveryParamId;
        const bool writeControl = spec.id == kFieldWriteParamId
            || spec.id == kRegisterMotionParamId
            || spec.id == kFieldReturnParamId
            || spec.id == kPropagationParamId;
        if ((settleControl && !settleResponse)
            || (writeControl && settleResponse)) continue;
        if (NSPointInRect(point, NSMakeRect(spec.panelX + 8, spec.y - 8, 230, 24))) {
            _dragParam = static_cast<int>(spec.id);
            [self setParam:spec.id fromPoint:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_fieldPage == 1 && _dragBreakpointRow >= 0) {
        [self setBreakpointAtPoint:point];
        return;
    }
    if (_dragView) {
        const CGFloat dx = point.x - _lastDragPoint.x;
        const CGFloat dy = point.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = 0;
    _dragView = NO;
    _dragBreakpointRow = -1;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const int next = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
@end

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
    p->guiView = [[S3GAmbiWranglerEncoderView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiWranglerEncoderView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win)
{
    if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(win->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GAmbiWranglerEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiWranglerEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };

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

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-wrangler-encoder-64",
    "s3g Ambi Wrangler Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Legacy and bounded rungler engines with homeostatic field listening and direct 7OA ACN/SN3D output.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiWranglerFactoryPreset(0u);
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0u ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
