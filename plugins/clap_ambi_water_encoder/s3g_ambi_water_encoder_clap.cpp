#include "s3g_ambi_water_encoder.h"
#include "s3g_ambi_water_presets.h"
#include "s3g_parameter_surface.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_gui_param_queue.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_parameter_surface_cocoa.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiWaterMaxChannels;
constexpr uint32_t kStateVersion = 6;
constexpr uint32_t kCustomPresetMagic = 0x31544157u; // WAT1
constexpr uint32_t kCustomPresetVersion = 6;

constexpr clap_id kPresetParamId = 1;
constexpr clap_id kOrderParamId = 2;
constexpr clap_id kVoicesParamId = 3;
constexpr clap_id kWaterParamId = 4;
constexpr clap_id kFlowParamId = 5;
constexpr clap_id kScaleParamId = 6;
constexpr clap_id kTurbulenceParamId = 7;
constexpr clap_id kAerationParamId = 8;
constexpr clap_id kSpreadParamId = 9;
constexpr clap_id kDeviationParamId = 10;
constexpr clap_id kRegimeParamId = 11;
constexpr clap_id kEnvironmentParamId = 12;
constexpr clap_id kDropsParamId = 13;
constexpr clap_id kSplashParamId = 14;
constexpr clap_id kBubblesParamId = 15;
constexpr clap_id kDensityParamId = 16;
constexpr clap_id kEventSizeParamId = 17;
constexpr clap_id kEventDecayParamId = 18;
constexpr clap_id kDepthParamId = 19;
constexpr clap_id kBrightnessParamId = 20;
constexpr clap_id kResonanceParamId = 21;
constexpr clap_id kDampingParamId = 22;
constexpr clap_id kContactParamId = 23;
constexpr clap_id kMotionRateParamId = 24;
constexpr clap_id kCurrentParamId = 25;
constexpr clap_id kSlopeParamId = 26;
constexpr clap_id kEddyParamId = 27;
constexpr clap_id kConvergenceParamId = 28;
constexpr clap_id kWidthParamId = 29;
constexpr clap_id kAzimuthParamId = 30;
constexpr clap_id kElevationParamId = 31;
constexpr clap_id kDistanceParamId = 32;
constexpr clap_id kSpatialFollowParamId = 33;
constexpr clap_id kOutputParamId = 34;
constexpr clap_id kPlaceParamId = 35;
constexpr clap_id kSpaceParamId = 36;
constexpr clap_id kEnvironmentSizeParamId = 37;
constexpr clap_id kEnvironmentDecayParamId = 38;
constexpr clap_id kEnvironmentDampingParamId = 39;
constexpr clap_id kFieldListenModeParamId = 40;
constexpr clap_id kFieldListenAmountParamId = 41;
constexpr clap_id kFieldListenResponseParamId = 42;
constexpr clap_id kSurfaceXParamId = 43;
constexpr clap_id kSurfaceYParamId = 44;
constexpr clap_id kFoamParamId = 45;
constexpr clap_id kShoreParamId = 46;
constexpr uint32_t kParamCount = 46u;
constexpr clap_id kRandomizeActionId = CLAP_INVALID_ID - 1u;
constexpr clap_id kApplySurfaceActionId = CLAP_INVALID_ID - 2u;
constexpr clap_id kSetMetadataActionId = CLAP_INVALID_ID - 3u;
constexpr clap_id kReplaceCustomStateActionId = CLAP_INVALID_ID - 4u;
constexpr clap_id kReplaceSavedStateActionId = CLAP_INVALID_ID - 5u;

using WaterSurface = s3g::ParameterSurfaceState<s3g::AmbiWaterParams>;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiWaterParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    WaterSurface surface {};
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiWaterParams params {};
};

struct MetadataCommand {
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

struct ControlSnapshot {
    s3g::AmbiWaterParams params {};
    s3g::AmbiWaterParams effectiveParams {};
    WaterSurface surface {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiWaterEncoder engine {};
    s3g::AmbiWaterParams params {};
    s3g::AmbiWaterParams effectiveParams {};
    WaterSurface surface {};
    std::atomic<float> effectiveSurfaceX { 0.5f };
    std::atomic<float> effectiveSurfaceY { 0.5f };
    std::atomic<bool> active { false };
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    uint32_t randomSeed = 0x6d2b79f5u;
    std::atomic<float> outputPeak { 0.0f };
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    s3g::clap_gui::SpscEventQueue<WaterSurface, 64u>
        guiSurfaceCommands {};
    s3g::clap_gui::SpscEventQueue<SavedState, 8u>
        guiStateCommands {};
    s3g::clap_gui::SpscEventQueue<MetadataCommand, 16u>
        guiMetadataCommands {};
    std::atomic_flag controlSnapshotLock = ATOMIC_FLAG_INIT;
    ControlSnapshot publishedControl {};
    std::atomic<bool> publishedParamsDirty { true };
    std::atomic<bool> publishedSurfaceDirty { true };
    std::atomic<bool> paramsRescanRequested { false };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    uint32_t guiTelemetryCountdown = 0u;
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    std::array<std::atomic<float>, s3g::kAmbiWaterMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiWaterMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiWaterMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiWaterMaxVoices> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiWaterMaxVoices> guiEvent {};
    std::array<std::atomic<float>, s3g::kAmbiWaterMaxVoices> guiRenderGain {};
    std::atomic<uint32_t> guiVoiceCount { 1u };
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

bool saveCustomPresetFile(const char* path,
    const s3g::AmbiWaterParams& params, const char* name)
{
    if (!path || !*path) return false;
    CustomPresetFile file {};
    std::snprintf(file.name, sizeof(file.name), "%s", name && *name ? name : "Custom");
    file.params = params;
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
        && (file.version == 1u || file.version == 2u || file.version == 3u
            || file.version == 4u || file.version == 5u
            || file.version == kCustomPresetVersion)
        && std::fread(file.name, 1, sizeof(file.name), handle) == sizeof(file.name);
    if (ok) {
        const size_t paramsSize = file.version == 1u
            ? offsetof(s3g::AmbiWaterParams, place)
            : (file.version == 2u
                    ? offsetof(s3g::AmbiWaterParams, environmentSize)
                    : (file.version == 3u
                            ? offsetof(s3g::AmbiWaterParams, fieldListenMode)
                            : (file.version == 4u
                                    ? offsetof(s3g::AmbiWaterParams, fieldListenAmount)
                                    : (file.version == 5u
                                            ? offsetof(s3g::AmbiWaterParams, foam)
                                            : sizeof(file.params)))));
        ok = std::fread(&file.params, 1, paramsSize, handle) == paramsSize;
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

constexpr const char* kRegimeNames[] = {
    "CURRENT", "RAIN", "CASCADE", "SURGE", "VORTEX", "SLOSH", "DRIP", "BUBBLE PLUME",
    "BREAKER", "BACKWASH", "HAIL", "SLEET", "FREEZING RAIN"
};

constexpr const char* kEnvironmentNames[] = {
    "OPEN", "ROCK", "LEAVES", "MUD", "CONCRETE", "METAL", "GLASS", "PIPE", "CAVE", "ICE"
};

constexpr const char* kPlaceNames[] = {
    "OPEN", "SUBMERGED", "CAVE", "CISTERN", "CHANNEL", "PIPE"
};

constexpr const char* kFieldListenNames[] = {
    "OFF", "FOLLOW", "COUNTER", "BALANCE"
};

constexpr const char* kFieldListenResponseNames[] = {
    "LEGACY", "ACCRETE", "SETTLE", "IMPRINT"
};

void applyEffectiveParams(Plugin& plugin, bool publish = true);
void copyName(char* destination, size_t capacity, const char* source);
void publishControlSnapshot(Plugin& plugin);
void publishAllParams(Plugin& plugin);
void publishSurfaceSnapshot(Plugin& plugin);
void publishPresetMetadata(Plugin& plugin);
void requestGuiParamService(Plugin& plugin);
void requestParamsRescan(Plugin& plugin);

void randomizeSafe(Plugin& plugin)
{
    auto p = plugin.params;
    const uint32_t order = p.order;
    const float outputGainDb = p.outputGainDb;
    const auto fieldListenMode = p.fieldListenMode;
    const float fieldListenAmount = p.fieldListenAmount;
    const auto fieldListenResponse = p.fieldListenResponse;
    uint32_t seed = plugin.randomSeed ^ static_cast<uint32_t>(std::lround(plugin.outputPeak.load(std::memory_order_relaxed) * 1000000.0f));
    p.voices = 16u + randomChoice(seed, 29u);
    p.regime = randomChoice(seed, s3g::kAmbiWaterRegimeCount);
    p.environment = randomChoice(seed, s3g::kAmbiWaterEnvironmentCount);
    p.water = randomRange(seed, 0.18f, 0.72f);
    p.flow = randomRange(seed, 0.10f, 0.78f);
    p.scale = randomRange(seed, 0.16f, 0.82f);
    p.turbulence = randomRange(seed, 0.06f, 0.72f);
    p.aeration = randomRange(seed, 0.04f, 0.74f);
    p.spread = randomRange(seed, 0.24f, 0.90f);
    p.deviation = randomRange(seed, 0.04f, 0.22f);
    p.drops = randomRange(seed, 0.04f, 0.76f);
    p.splash = randomRange(seed, 0.04f, 0.72f);
    p.bubbles = randomRange(seed, 0.02f, 0.68f);
    p.density = randomRange(seed, 0.10f, 0.72f);
    p.eventSize = randomRange(seed, 0.14f, 0.78f);
    p.eventDecay = randomRange(seed, 0.18f, 0.88f);
    p.depth = randomRange(seed, 0.18f, 0.82f);
    p.brightness = randomRange(seed, 0.18f, 0.76f);
    p.resonance = randomRange(seed, 0.08f, 0.68f);
    p.damping = randomRange(seed, 0.24f, 0.84f);
    p.contact = randomRange(seed, 0.04f, 0.76f);
    p.motionRateHz = randomRange(seed, 0.018f, 0.48f);
    p.current = randomRange(seed, 0.18f, 0.86f);
    p.slope = randomRange(seed, -0.82f, 0.48f);
    p.eddy = randomRange(seed, 0.06f, 0.72f);
    p.convergence = p.regime == 4u ? randomRange(seed, 0.46f, 0.94f) : randomRange(seed, 0.02f, 0.48f);
    p.width = randomRange(seed, 0.28f, 0.94f);
    p.centerAzimuthDeg = randomRange(seed, -45.0f, 45.0f);
    p.centerElevationDeg = randomRange(seed, -20.0f, 24.0f);
    p.centerDistance = randomRange(seed, 0.82f, 1.38f);
    p.spatialFollow = randomRange(seed, 0.28f, 0.88f);
    p.place = randomChoice(seed, s3g::kAmbiWaterPlaceCount);
    p.space = randomRange(seed, 0.08f, p.place == 0u ? 0.28f : 0.58f);
    p.environmentSize = randomRange(seed, 0.30f, 0.74f);
    p.environmentDecay = randomRange(seed, 0.34f, 0.78f);
    p.environmentDamping = randomRange(seed, 0.26f, 0.76f);
    p.foam = randomRange(seed, 0.04f, 0.72f);
    p.shore = randomRange(seed, 0.02f, 0.70f);
    p.order = order;
    p.outputGainDb = outputGainDb;
    p.fieldListenMode = fieldListenMode;
    p.fieldListenAmount = fieldListenAmount;
    p.fieldListenResponse = fieldListenResponse;

    switch (p.regime) {
    case 0u: // Current
        p.water = std::max(p.water, 0.42f);
        p.flow = std::max(p.flow, 0.44f);
        p.drops *= 0.45f;
        p.splash *= 0.72f;
        break;
    case 1u: // Rain
        p.drops = std::max(p.drops, 0.58f);
        p.density = std::max(p.density, 0.48f);
        p.bubbles *= 0.34f;
        break;
    case 2u: // Cascade
        p.water = std::max(p.water, 0.54f);
        p.flow = std::max(p.flow, 0.58f);
        p.aeration = std::max(p.aeration, 0.52f);
        p.splash = std::max(p.splash, 0.48f);
        break;
    case 3u: // Surge
        p.water = std::max(p.water, 0.54f);
        p.splash = std::max(p.splash, 0.42f);
        p.drops *= 0.30f;
        p.motionRateHz = std::min(p.motionRateHz, 0.24f);
        break;
    case 4u: // Vortex
        p.convergence = randomRange(seed, 0.52f, 0.94f);
        p.bubbles = std::max(p.bubbles, 0.38f);
        p.current = std::max(p.current, 0.54f);
        break;
    case 5u: // Slosh
        p.water = std::max(p.water, 0.48f);
        p.flow = std::min(p.flow, 0.48f);
        p.splash = std::max(p.splash, 0.30f);
        p.motionRateHz = std::min(p.motionRateHz, 0.18f);
        break;
    case 6u: // Drip
        p.water = std::min(p.water, 0.38f);
        p.flow = std::min(p.flow, 0.24f);
        p.aeration = std::min(p.aeration, 0.30f);
        p.drops = std::max(p.drops, 0.58f);
        p.bubbles = std::min(p.bubbles, 0.24f);
        p.density = randomRange(seed, 0.07f, 0.40f);
        p.eventSize = std::max(p.eventSize, 0.38f);
        break;
    case 7u: // Bubble Plume
        p.water = std::min(p.water, 0.46f);
        p.flow = std::min(p.flow, 0.32f);
        p.drops = std::min(p.drops, 0.08f);
        p.splash = std::min(p.splash, 0.20f);
        p.bubbles = std::max(p.bubbles, 0.58f);
        p.density = std::max(p.density, 0.28f);
        p.slope = std::max(p.slope, 0.22f);
        break;
    case 8u: // Breaker
        p.water = std::max(p.water, 0.62f);
        p.splash = std::max(p.splash, 0.58f);
        p.aeration = std::max(p.aeration, 0.62f);
        p.foam = std::max(p.foam, 0.66f);
        p.shore = std::max(p.shore, 0.34f);
        break;
    case 9u: // Backwash
        p.current = std::max(p.current, 0.58f);
        p.slope = std::min(p.slope, -0.32f);
        p.shore = std::max(p.shore, 0.62f);
        p.foam = std::max(p.foam, 0.28f);
        break;
    case 10u: // Hail
        p.water = std::min(p.water, 0.28f);
        p.drops = std::max(p.drops, 0.72f);
        p.contact = std::max(p.contact, 0.78f);
        p.brightness = std::max(p.brightness, 0.70f);
        break;
    case 11u: // Sleet
        p.drops = std::max(p.drops, 0.62f);
        p.contact = std::max(p.contact, 0.54f);
        p.damping = std::max(p.damping, 0.58f);
        break;
    default: // Freezing Rain
        p.drops = std::max(p.drops, 0.70f);
        p.contact = std::max(p.contact, 0.66f);
        p.resonance = std::max(p.resonance, 0.52f);
        p.environment = 9u;
        break;
    }

    plugin.randomSeed = seed;
    plugin.params = p;
    plugin.presetIndex = 0u;
    copyName(plugin.customPresetName,
        sizeof(plugin.customPresetName), "Random");
    s3g::bypassParameterSurfaceForSceneChange(plugin.surface);
    applyEffectiveParams(plugin, false);
    plugin.engine.beginTransition();
    publishControlSnapshot(plugin);
}

bool assignParam(s3g::AmbiWaterParams& params, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: params.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kVoicesParamId: params.voices = static_cast<uint32_t>(std::lround(value)); return true;
    case kWaterParamId: params.water = static_cast<float>(value); return true;
    case kFlowParamId: params.flow = static_cast<float>(value); return true;
    case kScaleParamId: params.scale = static_cast<float>(value); return true;
    case kTurbulenceParamId: params.turbulence = static_cast<float>(value); return true;
    case kAerationParamId: params.aeration = static_cast<float>(value); return true;
    case kSpreadParamId: params.spread = static_cast<float>(value); return true;
    case kDeviationParamId: params.deviation = static_cast<float>(value); return true;
    case kRegimeParamId: params.regime = static_cast<uint32_t>(std::lround(value)); return true;
    case kEnvironmentParamId: params.environment = static_cast<uint32_t>(std::lround(value)); return true;
    case kDropsParamId: params.drops = static_cast<float>(value); return true;
    case kSplashParamId: params.splash = static_cast<float>(value); return true;
    case kBubblesParamId: params.bubbles = static_cast<float>(value); return true;
    case kDensityParamId: params.density = static_cast<float>(value); return true;
    case kEventSizeParamId: params.eventSize = static_cast<float>(value); return true;
    case kEventDecayParamId: params.eventDecay = static_cast<float>(value); return true;
    case kDepthParamId: params.depth = static_cast<float>(value); return true;
    case kBrightnessParamId: params.brightness = static_cast<float>(value); return true;
    case kResonanceParamId: params.resonance = static_cast<float>(value); return true;
    case kDampingParamId: params.damping = static_cast<float>(value); return true;
    case kContactParamId: params.contact = static_cast<float>(value); return true;
    case kMotionRateParamId: params.motionRateHz = static_cast<float>(value); return true;
    case kCurrentParamId: params.current = static_cast<float>(value); return true;
    case kSlopeParamId: params.slope = static_cast<float>(value); return true;
    case kEddyParamId: params.eddy = static_cast<float>(value); return true;
    case kConvergenceParamId: params.convergence = static_cast<float>(value); return true;
    case kWidthParamId: params.width = static_cast<float>(value); return true;
    case kAzimuthParamId: params.centerAzimuthDeg = static_cast<float>(value); return true;
    case kElevationParamId: params.centerElevationDeg = static_cast<float>(value); return true;
    case kDistanceParamId: params.centerDistance = static_cast<float>(value); return true;
    case kSpatialFollowParamId: params.spatialFollow = static_cast<float>(value); return true;
    case kOutputParamId: params.outputGainDb = static_cast<float>(value); return true;
    case kPlaceParamId: params.place = static_cast<uint32_t>(std::lround(value)); return true;
    case kSpaceParamId: params.space = static_cast<float>(value); return true;
    case kEnvironmentSizeParamId: params.environmentSize = static_cast<float>(value); return true;
    case kEnvironmentDecayParamId: params.environmentDecay = static_cast<float>(value); return true;
    case kEnvironmentDampingParamId: params.environmentDamping = static_cast<float>(value); return true;
    case kFieldListenModeParamId:
        params.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    case kFieldListenAmountParamId:
        params.fieldListenAmount = static_cast<float>(value);
        return true;
    case kFieldListenResponseParamId:
        params.fieldListenResponse =
            static_cast<s3g::AmbiFieldListenerResponse>(
                static_cast<uint32_t>(std::lround(value)));
        return true;
    case kSurfaceXParamId: params.surfaceX = static_cast<float>(value); return true;
    case kSurfaceYParamId: params.surfaceY = static_cast<float>(value); return true;
    case kFoamParamId: params.foam = static_cast<float>(value); return true;
    case kShoreParamId: params.shore = static_cast<float>(value); return true;
    default: return false;
    }
}

bool paramValueFromParams(const s3g::AmbiWaterParams& params,
    clap_id id, double& value)
{
    switch (id) {
    case kOrderParamId: value = params.order; break;
    case kVoicesParamId: value = params.voices; break;
    case kWaterParamId: value = params.water; break;
    case kFlowParamId: value = params.flow; break;
    case kScaleParamId: value = params.scale; break;
    case kTurbulenceParamId: value = params.turbulence; break;
    case kAerationParamId: value = params.aeration; break;
    case kSpreadParamId: value = params.spread; break;
    case kDeviationParamId: value = params.deviation; break;
    case kRegimeParamId: value = params.regime; break;
    case kEnvironmentParamId: value = params.environment; break;
    case kDropsParamId: value = params.drops; break;
    case kSplashParamId: value = params.splash; break;
    case kBubblesParamId: value = params.bubbles; break;
    case kDensityParamId: value = params.density; break;
    case kEventSizeParamId: value = params.eventSize; break;
    case kEventDecayParamId: value = params.eventDecay; break;
    case kDepthParamId: value = params.depth; break;
    case kBrightnessParamId: value = params.brightness; break;
    case kResonanceParamId: value = params.resonance; break;
    case kDampingParamId: value = params.damping; break;
    case kContactParamId: value = params.contact; break;
    case kMotionRateParamId: value = params.motionRateHz; break;
    case kCurrentParamId: value = params.current; break;
    case kSlopeParamId: value = params.slope; break;
    case kEddyParamId: value = params.eddy; break;
    case kConvergenceParamId: value = params.convergence; break;
    case kWidthParamId: value = params.width; break;
    case kAzimuthParamId: value = params.centerAzimuthDeg; break;
    case kElevationParamId: value = params.centerElevationDeg; break;
    case kDistanceParamId: value = params.centerDistance; break;
    case kSpatialFollowParamId: value = params.spatialFollow; break;
    case kOutputParamId: value = params.outputGainDb; break;
    case kPlaceParamId: value = params.place; break;
    case kSpaceParamId: value = params.space; break;
    case kEnvironmentSizeParamId: value = params.environmentSize; break;
    case kEnvironmentDecayParamId: value = params.environmentDecay; break;
    case kEnvironmentDampingParamId: value = params.environmentDamping; break;
    case kFieldListenModeParamId: value = static_cast<uint32_t>(params.fieldListenMode); break;
    case kFieldListenAmountParamId: value = params.fieldListenAmount; break;
    case kFieldListenResponseParamId: value = static_cast<uint32_t>(params.fieldListenResponse); break;
    case kSurfaceXParamId: value = params.surfaceX; break;
    case kSurfaceYParamId: value = params.surfaceY; break;
    case kFoamParamId: value = params.foam; break;
    case kShoreParamId: value = params.shore; break;
    default: return false;
    }
    return true;
}

void copyName(char* destination, size_t capacity, const char* source)
{
    if (!destination || capacity == 0u) return;
    const char* safe = source ? source : "";
    size_t index = 0u;
    for (; index + 1u < capacity && safe[index] != '\0'; ++index) {
        destination[index] = safe[index];
    }
    destination[index] = '\0';
}

void lockNonAudio(std::atomic_flag& lock)
{
    while (lock.test_and_set(std::memory_order_acquire)) {
        // Fixed-size snapshots are only waited on by non-audio threads.
    }
}

void publishControlSnapshot(Plugin& plugin)
{
    plugin.publishedParamsDirty.store(true, std::memory_order_release);
    plugin.publishedSurfaceDirty.store(true, std::memory_order_release);
    if (plugin.controlSnapshotLock.test_and_set(
            std::memory_order_acquire)) return;
    plugin.publishedControl.params = plugin.params;
    plugin.publishedControl.effectiveParams = plugin.effectiveParams;
    plugin.publishedControl.surface = plugin.surface;
    plugin.publishedControl.presetIndex = plugin.presetIndex;
    copyName(plugin.publishedControl.customPresetName,
        sizeof(plugin.publishedControl.customPresetName),
        plugin.customPresetName);
    plugin.publishedParamsDirty.store(false, std::memory_order_release);
    plugin.publishedSurfaceDirty.store(false, std::memory_order_release);
    plugin.controlSnapshotLock.clear(std::memory_order_release);
}

void publishParamSnapshot(Plugin& plugin)
{
    plugin.publishedParamsDirty.store(true, std::memory_order_release);
    if (plugin.controlSnapshotLock.test_and_set(
            std::memory_order_acquire)) return;
    plugin.publishedControl.params = plugin.params;
    plugin.publishedControl.effectiveParams = plugin.effectiveParams;
    plugin.publishedControl.presetIndex = plugin.presetIndex;
    copyName(plugin.publishedControl.customPresetName,
        sizeof(plugin.publishedControl.customPresetName),
        plugin.customPresetName);
    plugin.publishedParamsDirty.store(false, std::memory_order_release);
    plugin.controlSnapshotLock.clear(std::memory_order_release);
}

void retryControlSnapshotPublication(Plugin& plugin)
{
    if (plugin.publishedSurfaceDirty.load(std::memory_order_acquire)) {
        publishControlSnapshot(plugin);
    } else if (plugin.publishedParamsDirty.load(std::memory_order_acquire)) {
        publishParamSnapshot(plugin);
    }
}

ControlSnapshot controlSnapshot(Plugin& plugin)
{
    lockNonAudio(plugin.controlSnapshotLock);
    const auto result = plugin.publishedControl;
    plugin.controlSnapshotLock.clear(std::memory_order_release);
    return result;
}

void publishPresetMetadata(Plugin& plugin) { publishParamSnapshot(plugin); }
void publishAllParams(Plugin& plugin) { publishParamSnapshot(plugin); }
void publishEffectiveParams(Plugin& plugin) { publishParamSnapshot(plugin); }
void publishSurfaceSnapshot(Plugin& plugin) { publishControlSnapshot(plugin); }

double publishedParamValue(Plugin& plugin, clap_id id, bool effective)
{
    if (id < 1u || id > kParamCount) return 0.0;
    const auto snapshot = controlSnapshot(plugin);
    if (id == kPresetParamId) return snapshot.presetIndex;
    double value = 0.0;
    (void)paramValueFromParams(
        effective ? snapshot.effectiveParams : snapshot.params,
        id, value);
    return value;
}

s3g::AmbiWaterParams publishedParamsSnapshot(
    Plugin& plugin, bool effective = false)
{
    const auto snapshot = controlSnapshot(plugin);
    return effective ? snapshot.effectiveParams : snapshot.params;
}

WaterSurface publishedSurfaceSnapshot(Plugin& plugin)
{
    return controlSnapshot(plugin).surface;
}

bool queueGuiSurface(Plugin& plugin, const WaterSurface& surface)
{
    if (plugin.guiSurfaceCommands.available() == 0u
        || plugin.guiParamEvents.available() == 0u) return false;
    if (!plugin.guiSurfaceCommands.push(surface)) return false;
    const s3g::clap_gui::ParamEvent action {
        s3g::clap_gui::ParamEventKind::Value,
        kApplySurfaceActionId, 0.0
    };
    if (!plugin.guiParamEvents.push(action)) return false;
    requestGuiParamService(plugin);
    return true;
}

s3g::AmbiWaterParams waterSurfaceParams(
    const Plugin& plugin, float cursorX, float cursorY)
{
    const auto& base = plugin.params;
    if (!plugin.surface.enabled || plugin.surface.cellCount < 2u) return base;
    const auto weights = s3g::parameterSurfaceWeights(
        plugin.surface, cursorX, cursorY);
    if (weights.activeCount < 2u) return base;
    const auto& nearest = s3g::parameterSurfaceNearestParams(
        plugin.surface, weights, base);
    auto result = nearest;
#define S3G_WATER_SURFACE_BLEND(member) \
    result.member = s3g::parameterSurfaceBlend(plugin.surface, weights, \
        [](const s3g::AmbiWaterParams& p) { return p.member; }, base.member)
    S3G_WATER_SURFACE_BLEND(water);
    S3G_WATER_SURFACE_BLEND(flow);
    S3G_WATER_SURFACE_BLEND(scale);
    S3G_WATER_SURFACE_BLEND(turbulence);
    S3G_WATER_SURFACE_BLEND(aeration);
    S3G_WATER_SURFACE_BLEND(spread);
    S3G_WATER_SURFACE_BLEND(deviation);
    S3G_WATER_SURFACE_BLEND(drops);
    S3G_WATER_SURFACE_BLEND(splash);
    S3G_WATER_SURFACE_BLEND(bubbles);
    S3G_WATER_SURFACE_BLEND(density);
    S3G_WATER_SURFACE_BLEND(eventSize);
    S3G_WATER_SURFACE_BLEND(eventDecay);
    S3G_WATER_SURFACE_BLEND(depth);
    S3G_WATER_SURFACE_BLEND(brightness);
    S3G_WATER_SURFACE_BLEND(resonance);
    S3G_WATER_SURFACE_BLEND(damping);
    S3G_WATER_SURFACE_BLEND(contact);
    S3G_WATER_SURFACE_BLEND(motionRateHz);
    S3G_WATER_SURFACE_BLEND(current);
    S3G_WATER_SURFACE_BLEND(slope);
    S3G_WATER_SURFACE_BLEND(eddy);
    S3G_WATER_SURFACE_BLEND(convergence);
    S3G_WATER_SURFACE_BLEND(width);
    result.centerAzimuthDeg = s3g::parameterSurfaceBlendAngleDegrees(
        plugin.surface, weights,
        [](const s3g::AmbiWaterParams& p) { return p.centerAzimuthDeg; },
        base.centerAzimuthDeg);
    S3G_WATER_SURFACE_BLEND(centerElevationDeg);
    S3G_WATER_SURFACE_BLEND(centerDistance);
    S3G_WATER_SURFACE_BLEND(spatialFollow);
    S3G_WATER_SURFACE_BLEND(space);
    S3G_WATER_SURFACE_BLEND(environmentSize);
    S3G_WATER_SURFACE_BLEND(environmentDecay);
    S3G_WATER_SURFACE_BLEND(environmentDamping);
    S3G_WATER_SURFACE_BLEND(fieldListenAmount);
    S3G_WATER_SURFACE_BLEND(foam);
    S3G_WATER_SURFACE_BLEND(shore);
#undef S3G_WATER_SURFACE_BLEND
    result.order = base.order;
    result.outputGainDb = base.outputGainDb;
    result.surfaceX = base.surfaceX;
    result.surfaceY = base.surfaceY;
    return result;
}

s3g::AmbiWaterParams waterSurfaceParams(const Plugin& plugin)
{
    if (!plugin.active.load(std::memory_order_acquire)) {
        return waterSurfaceParams(
            plugin, plugin.params.surfaceX, plugin.params.surfaceY);
    }
    return waterSurfaceParams(plugin,
        plugin.effectiveSurfaceX.load(std::memory_order_relaxed),
        plugin.effectiveSurfaceY.load(std::memory_order_relaxed));
}

void snapSurfaceCursor(Plugin& plugin)
{
    plugin.effectiveSurfaceX.store(
        plugin.params.surfaceX, std::memory_order_relaxed);
    plugin.effectiveSurfaceY.store(
        plugin.params.surfaceY, std::memory_order_relaxed);
}

bool advanceSurfaceCursor(Plugin& plugin, float deltaSeconds)
{
    const bool gliding = plugin.surface.enabled
        && plugin.surface.cellCount >= 2u
        && plugin.surface.glideMs > 0.0f;
    const float glideMs = gliding ? plugin.surface.glideMs : 0.0f;
    const float currentX = plugin.effectiveSurfaceX.load(
        std::memory_order_relaxed);
    const float currentY = plugin.effectiveSurfaceY.load(
        std::memory_order_relaxed);
    const float nextX = s3g::parameterSurfaceGlideValue(
        currentX, plugin.params.surfaceX, glideMs, deltaSeconds);
    const float nextY = s3g::parameterSurfaceGlideValue(
        currentY, plugin.params.surfaceY, glideMs, deltaSeconds);
    plugin.effectiveSurfaceX.store(nextX,
        std::memory_order_relaxed);
    plugin.effectiveSurfaceY.store(nextY,
        std::memory_order_relaxed);
    return std::fabs(nextX - currentX) > 1.0e-7f
        || std::fabs(nextY - currentY) > 1.0e-7f;
}

void applyEffectiveParams(Plugin& plugin, bool publish)
{
    plugin.engine.setParameterSurfaceGlideMs(
        plugin.surface.enabled && plugin.surface.cellCount >= 2u
            ? plugin.surface.glideMs : 0.0f);
    const bool audioActive = plugin.active.load(
        std::memory_order_acquire);
    const float cursorX = audioActive
        ? plugin.effectiveSurfaceX.load(std::memory_order_relaxed)
        : plugin.params.surfaceX;
    const float cursorY = audioActive
        ? plugin.effectiveSurfaceY.load(std::memory_order_relaxed)
        : plugin.params.surfaceY;
    plugin.engine.setParams(waterSurfaceParams(
        plugin, cursorX, cursorY));
    if (plugin.surface.enabled && plugin.surface.cellCount >= 2u) {
        const auto weights = s3g::parameterSurfaceWeights(
            plugin.surface, cursorX, cursorY);
        plugin.engine.setParameterSurfaceVoiceMembership(
            s3g::parameterSurfaceVoiceMembership<
                s3g::kAmbiWaterMaxVoices>(
                plugin.surface, weights, plugin.params.voices));
    } else {
        plugin.engine.clearParameterSurfaceVoiceMembership();
    }
    plugin.effectiveParams = plugin.engine.params();
    if (publish) publishEffectiveParams(plugin);
}

void sanitizeWaterState(Plugin& plugin)
{
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
    s3g::sanitizeParameterSurface(plugin.surface);
    for (uint32_t index = 0u; index < plugin.surface.cellCount; ++index) {
        plugin.engine.setParams(plugin.surface.cells[index].params);
        plugin.surface.cells[index].params = plugin.engine.params();
    }
    applyEffectiveParams(plugin, false);
    publishControlSnapshot(plugin);
}

void requestSurfaceProcess(Plugin& plugin)
{
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kPresetParamId) {
        p.presetIndex = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, s3g::kAmbiWaterFactoryPresetCount - 1u);
        p.customPresetName[0] = '\0';
        const float outputGainDb = p.params.outputGainDb;
        p.params = s3g::ambiWaterFactoryPreset(p.presetIndex);
        p.params.outputGainDb = outputGainDb;
        applyEffectiveParams(p, false);
        p.engine.beginTransition();
        publishControlSnapshot(p);
        requestParamsRescan(p);
        return;
    }
    if (!assignParam(p.params, id, value)) return;
    applyEffectiveParams(p);
    publishAllParams(p);
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiParamEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!plugin.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 },
        { s3g::clap_gui::ParamEventKind::Value, id, value },
        { s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 },
    }};
    if (!plugin.guiParamEvents.pushBatch(
            events.data(), events.size())) return false;
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiMetadata(Plugin& plugin, uint32_t presetIndex,
    const char* customName)
{
    if (plugin.guiMetadataCommands.available() == 0u
        || plugin.guiParamEvents.available() == 0u) return false;
    MetadataCommand command {};
    command.presetIndex = presetIndex;
    copyName(command.customPresetName,
        sizeof(command.customPresetName), customName);
    if (!plugin.guiMetadataCommands.push(command)) return false;
    const s3g::clap_gui::ParamEvent action {
        s3g::clap_gui::ParamEventKind::Value,
        kSetMetadataActionId, 0.0
    };
    if (!plugin.guiParamEvents.push(action)) return false;
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiState(Plugin& plugin, const SavedState& state,
    clap_id actionId)
{
    if (plugin.guiStateCommands.available() == 0u
        || plugin.guiParamEvents.available() == 0u) return false;
    if (!plugin.guiStateCommands.push(state)) return false;
    const s3g::clap_gui::ParamEvent action {
        s3g::clap_gui::ParamEventKind::Value, actionId, 0.0
    };
    if (!plugin.guiParamEvents.push(action)) return false;
    requestGuiParamService(plugin);
    return true;
}

void requestParamsRescan(Plugin& plugin)
{
    plugin.paramsRescanRequested.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_callback) {
        plugin.host->request_callback(plugin.host);
    }
}

bool pushGuiParamEvent(const clap_output_events_t* out,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (pending.paramId > kParamCount) return true;
    if (!out || !out->try_push) return true;
    if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = pending.value;
        return out->try_push(out, &event.header);
    }
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = pending.kind
            == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return out->try_push(out, &event.header);
}

bool applyPendingMetadata(Plugin& plugin)
{
    MetadataCommand command {};
    if (!plugin.guiMetadataCommands.peek(command)) return false;
    plugin.presetIndex = std::min<uint32_t>(command.presetIndex,
        s3g::kAmbiWaterFactoryPresetCount - 1u);
    copyName(plugin.customPresetName,
        sizeof(plugin.customPresetName), command.customPresetName);
    publishParamSnapshot(plugin);
    plugin.guiMetadataCommands.pop();
    return true;
}

bool applyPendingSurface(Plugin& plugin)
{
    WaterSurface surface {};
    if (!plugin.guiSurfaceCommands.peek(surface)) return false;
    s3g::sanitizeParameterSurface(surface);
    plugin.surface = surface;
    applyEffectiveParams(plugin, false);
    publishControlSnapshot(plugin);
    plugin.guiSurfaceCommands.pop();
    return true;
}

bool applyPendingState(Plugin& plugin, bool notifyHost)
{
    SavedState state {};
    if (!plugin.guiStateCommands.peek(state)) return false;
    plugin.params = state.params;
    plugin.surface = state.surface;
    plugin.presetIndex = std::min<uint32_t>(state.presetIndex,
        s3g::kAmbiWaterFactoryPresetCount - 1u);
    copyName(plugin.customPresetName,
        sizeof(plugin.customPresetName), state.customPresetName);
    sanitizeWaterState(plugin);
    plugin.engine.beginTransition();
    plugin.guiStateCommands.pop();
    if (notifyHost) requestParamsRescan(plugin);
    return true;
}

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* out)
{
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(out, pending)) break;
        bool consumed = true;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            if (pending.paramId == kRandomizeActionId) {
                randomizeSafe(plugin);
                requestParamsRescan(plugin);
            } else if (pending.paramId == kApplySurfaceActionId) {
                consumed = applyPendingSurface(plugin);
            } else if (pending.paramId == kSetMetadataActionId) {
                consumed = applyPendingMetadata(plugin);
            } else if (pending.paramId == kReplaceCustomStateActionId) {
                consumed = applyPendingState(plugin, true);
            } else if (pending.paramId == kReplaceSavedStateActionId) {
                consumed = applyPendingState(plugin, true);
            } else {
                applyParam(plugin, pending.paramId, pending.value);
            }
        }
        if (!consumed) break;
        plugin.guiParamEvents.pop();
    }
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->host && p->host->get_extension) {
        p->hostParams = static_cast<const clap_host_params_t*>(
            p->host->get_extension(p->host, CLAP_EXT_PARAMS));
    }
    return true;
}
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
    p->active.store(false, std::memory_order_release);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    snapSurfaceCursor(*p);
    sanitizeWaterState(*p);
    p->active.store(true, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->active.store(false, std::memory_order_release);
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    snapSurfaceCursor(*p);
    applyEffectiveParams(*p);
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    bool paramsChanged = false;
    bool presetChanged = false;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            if (param->param_id == kPresetParamId) {
                p.presetIndex = std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(param->value)), 0u,
                    s3g::kAmbiWaterFactoryPresetCount - 1u);
                p.customPresetName[0] = '\0';
                const float outputGainDb = p.params.outputGainDb;
                p.params = s3g::ambiWaterFactoryPreset(p.presetIndex);
                p.params.outputGainDb = outputGainDb;
                paramsChanged = true;
                presetChanged = true;
            } else {
                paramsChanged |= assignParam(
                    p.params, param->param_id, param->value);
            }
        }
    }
    if (paramsChanged) {
        applyEffectiveParams(p, false);
        if (presetChanged) p.engine.beginTransition();
        if (presetChanged) publishControlSnapshot(p);
        else publishParamSnapshot(p);
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    serviceGuiParamEvents(*p, proc->out_events);
    retryControlSnapshotPublication(*p);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (!output.data32 || outChannels == 0u) return CLAP_PROCESS_CONTINUE;

    constexpr uint32_t kSurfaceControlFrames = 64u;
    uint32_t surfaceOffset = 0u;
    while (surfaceOffset < frames) {
        const float currentX = p->effectiveSurfaceX.load(
            std::memory_order_relaxed);
        const float currentY = p->effectiveSurfaceY.load(
            std::memory_order_relaxed);
        const bool cursorMoving = p->surface.enabled
            && p->surface.cellCount >= 2u
            && p->surface.glideMs > 0.0f
            && (std::fabs(currentX - p->params.surfaceX) > 1.0e-6f
                || std::fabs(currentY - p->params.surfaceY) > 1.0e-6f);
        const uint32_t spanFrames = cursorMoving
            ? std::min<uint32_t>(kSurfaceControlFrames,
                frames - surfaceOffset)
            : frames - surfaceOffset;
        if (advanceSurfaceCursor(*p,
                static_cast<float>(spanFrames)
                    / static_cast<float>(p->sampleRate))) {
            applyEffectiveParams(*p);
        }
        std::array<float*, kOutputChannels> spanOutputs {};
        for (uint32_t ch = 0u; ch < outChannels; ++ch) {
            spanOutputs[ch] = output.data32[ch]
                ? output.data32[ch] + surfaceOffset : nullptr;
        }
        p->engine.process(spanOutputs.data(), outChannels, spanFrames);
        surfaceOffset += spanFrames;
    }
    p->effectiveParams = p->engine.params();
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

#if defined(__APPLE__)
    const bool guiIsVisible = p->guiVisible.load(std::memory_order_acquire);
    if (guiIsVisible && p->guiTelemetryCountdown <= frames) {
        float peak = 0.0f;
        for (uint32_t ch = 0u; ch < outChannels; ++ch) {
            if (!output.data32[ch]) continue;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                peak = std::max(peak, std::fabs(output.data32[ch][frame]));
            }
        }
        p->outputPeak.store(std::max(
            p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak),
            std::memory_order_relaxed);
        const uint32_t voices = std::min<uint32_t>(
            p->engine.processingVoiceCount(), s3g::kAmbiWaterMaxVoices);
        p->guiVoiceCount.store(voices, std::memory_order_relaxed);
        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const auto point = p->engine.voicePoint(voice);
            p->guiAzimuth[voice].store(point.azimuthDeg, std::memory_order_relaxed);
            p->guiElevation[voice].store(point.elevationDeg, std::memory_order_relaxed);
            p->guiDistance[voice].store(point.distance, std::memory_order_relaxed);
            p->guiEnergy[voice].store(p->engine.voiceEnergy(voice), std::memory_order_relaxed);
            p->guiEvent[voice].store(p->engine.voiceEventLevel(voice), std::memory_order_relaxed);
            p->guiRenderGain[voice].store(
                p->engine.voiceRenderGain(voice),
                std::memory_order_relaxed);
        }
        p->guiTelemetryCountdown = std::max<uint32_t>(
            1u, static_cast<uint32_t>(p->sampleRate / 30.0));
    } else if (guiIsVisible) {
        p->guiTelemetryCountdown -= frames;
    } else {
        p->guiTelemetryCountdown = 0u;
    }
#endif
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->paramsRescanRequested.exchange(
            false, std::memory_order_acq_rel)) return;
    if (p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
}

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
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiWaterFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 28.0, true },
    { kWaterParamId, "Water", 0.0, 1.0, 0.58, false },
    { kFlowParamId, "Flow", 0.0, 1.0, 0.48, false },
    { kScaleParamId, "Scale", 0.0, 1.0, 0.46, false },
    { kTurbulenceParamId, "Turbulence", 0.0, 1.0, 0.38, false },
    { kAerationParamId, "Aeration", 0.0, 1.0, 0.30, false },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.58, false },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.14, false },
    { kRegimeParamId, "Water Regime", 0.0, static_cast<double>(s3g::kAmbiWaterRegimeCount - 1u), 0.0, true },
    { kEnvironmentParamId, "Environment", 0.0, static_cast<double>(s3g::kAmbiWaterEnvironmentCount - 1u), 0.0, true },
    { kDropsParamId, "Drops", 0.0, 1.0, 0.26, false },
    { kSplashParamId, "Splash", 0.0, 1.0, 0.34, false },
    { kBubblesParamId, "Bubbles", 0.0, 1.0, 0.18, false },
    { kDensityParamId, "Event Density", 0.0, 1.0, 0.38, false },
    { kEventSizeParamId, "Event Size", 0.0, 1.0, 0.42, false },
    { kEventDecayParamId, "Event Life", 0.0, 1.0, 0.38, false },
    { kDepthParamId, "Depth", 0.0, 1.0, 0.48, false },
    { kBrightnessParamId, "Brightness", 0.0, 1.0, 0.44, false },
    { kResonanceParamId, "Bubble Rise", 0.0, 1.0, 0.30, false },
    { kDampingParamId, "Damping", 0.0, 1.0, 0.52, false },
    { kContactParamId, "Impact Texture", 0.0, 1.0, 0.30, false },
    { kMotionRateParamId, "Parcel Rate", 0.002, 3.0, 0.12, false },
    { kCurrentParamId, "Current", 0.0, 1.0, 0.54, false },
    { kSlopeParamId, "Slope", -1.0, 1.0, 0.18, false },
    { kEddyParamId, "Eddy", 0.0, 1.0, 0.32, false },
    { kConvergenceParamId, "Convergence", 0.0, 1.0, 0.18, false },
    { kWidthParamId, "Width", 0.0, 1.0, 0.68, false },
    { kAzimuthParamId, "Direction", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Range", 0.15, 2.0, 1.0, false },
    { kSpatialFollowParamId, "Inertia", 0.0, 1.0, 0.72, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kPlaceParamId, "Place", 0.0, static_cast<double>(s3g::kAmbiWaterPlaceCount - 1u), 0.0, true },
    { kSpaceParamId, "Env Return", 0.0, 1.0, 0.18, false },
    { kEnvironmentSizeParamId, "Env Size", 0.0, 1.0, 0.5, false },
    { kEnvironmentDecayParamId, "Env Decay", 0.0, 1.0, 0.5, false },
    { kEnvironmentDampingParamId, "Env Damping", 0.0, 1.0, 0.5, false },
    { kFieldListenModeParamId, "Field Listen", 0.0, 3.0, 0.0, true },
    { kFieldListenAmountParamId, "Listen Amount", 0.0, 1.0, 1.0, false },
    { kFieldListenResponseParamId, "Listen Response", 0.0, 3.0, 0.0, true },
    { kSurfaceXParamId, "Surface X", 0.0, 1.0, 0.5, false },
    { kSurfaceYParamId, "Surface Y", 0.0, 1.0, 0.5, false },
    { kFoamParamId, "Foam", 0.0, 1.0, 0.0, false },
    { kShoreParamId, "Shore Contact", 0.0, 1.0, 0.0, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

const char* paramModule(clap_id id)
{
    switch (id) {
    case kPresetParamId: return "Global";
    case kSurfaceXParamId:
    case kSurfaceYParamId: return "Parameter Surface";
    case kFoamParamId:
    case kShoreParamId: return "Coastal Objects";
    case kOrderParamId:
    case kRegimeParamId:
    case kEnvironmentParamId:
    case kVoicesParamId:
    case kWaterParamId:
    case kFlowParamId:
    case kScaleParamId:
    case kTurbulenceParamId: return "Water Source";
    case kAerationParamId:
    case kDropsParamId:
    case kSplashParamId:
    case kBubblesParamId:
    case kDensityParamId:
    case kEventSizeParamId:
    case kEventDecayParamId: return "Events";
    case kDepthParamId:
    case kBrightnessParamId:
    case kResonanceParamId:
    case kDampingParamId:
    case kContactParamId:
    case kOutputParamId: return "Body and Events";
    case kMotionRateParamId:
    case kCurrentParamId:
    case kSlopeParamId:
    case kEddyParamId:
    case kConvergenceParamId:
    case kWidthParamId:
    case kSpreadParamId:
    case kDeviationParamId:
    case kSpatialFollowParamId: return "Parcel Motion";
    case kAzimuthParamId:
    case kElevationParamId:
    case kDistanceParamId: return "Field Origin";
    case kPlaceParamId:
    case kSpaceParamId:
    case kEnvironmentSizeParamId:
    case kEnvironmentDecayParamId:
    case kEnvironmentDampingParamId:
    case kFieldListenModeParamId:
    case kFieldListenAmountParamId:
    case kFieldListenResponseParamId: return "Environment Field";
    default: return "Ambi Water Encoder";
    }
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, paramModule(def.id), sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || id < 1u || id > kParamCount) return false;
    *value = publishedParamValue(*self(plugin), id, false);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", s3g::ambiWaterFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kRegimeParamId) {
        std::snprintf(display, size, "%s", kRegimeNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiWaterRegimeCount - 1u)]);
    } else if (id == kEnvironmentParamId) {
        std::snprintf(display, size, "%s", kEnvironmentNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiWaterEnvironmentCount - 1u)]);
    } else if (id == kPlaceParamId) {
        std::snprintf(display, size, "%s", kPlaceNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiWaterPlaceCount - 1u)]);
    } else if (id == kFieldListenModeParamId) {
        std::snprintf(display, size, "%s", kFieldListenNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kFieldListenResponseParamId) {
        std::snprintf(display, size, "%s", kFieldListenResponseNames[
            std::min<uint32_t>(
                static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kMotionRateParamId) {
        std::snprintf(display, size, "%.3f Hz", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kWaterParamId || id == kFlowParamId || id == kScaleParamId
        || id == kTurbulenceParamId || id == kAerationParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kDropsParamId || id == kSplashParamId || id == kBubblesParamId || id == kDensityParamId
        || id == kEventSizeParamId || id == kEventDecayParamId || id == kDepthParamId
        || id == kBrightnessParamId || id == kResonanceParamId || id == kDampingParamId || id == kContactParamId
        || id == kCurrentParamId || id == kSlopeParamId || id == kEddyParamId
        || id == kConvergenceParamId || id == kWidthParamId || id == kSpatialFollowParamId
        || id == kSpaceParamId || id == kEnvironmentSizeParamId
        || id == kEnvironmentDecayParamId || id == kEnvironmentDampingParamId
        || id == kFieldListenAmountParamId || id == kFoamParamId || id == kShoreParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;

    if (id == kPresetParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWaterFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiWaterFactoryPresetInfo(index).name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kRegimeParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWaterRegimeCount; ++index) {
            if (std::strcmp(display, kRegimeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kEnvironmentParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWaterEnvironmentCount; ++index) {
            if (std::strcmp(display, kEnvironmentNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kPlaceParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWaterPlaceCount; ++index) {
            if (std::strcmp(display, kPlaceNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kFieldListenModeParamId) {
        for (uint32_t index = 0u; index < std::size(kFieldListenNames); ++index) {
            if (std::strcmp(display, kFieldListenNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kFieldListenResponseParamId) {
        for (uint32_t index = 0u;
            index < std::size(kFieldListenResponseNames); ++index) {
            if (std::strcmp(display, kFieldListenResponseNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }

    *value = std::atof(display);
    if (id == kWaterParamId || id == kFlowParamId || id == kScaleParamId
        || id == kTurbulenceParamId || id == kAerationParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kDropsParamId || id == kSplashParamId || id == kBubblesParamId || id == kDensityParamId
        || id == kEventSizeParamId || id == kEventDecayParamId || id == kDepthParamId
        || id == kBrightnessParamId || id == kResonanceParamId || id == kDampingParamId || id == kContactParamId
        || id == kCurrentParamId || id == kSlopeParamId || id == kEddyParamId
        || id == kConvergenceParamId || id == kWidthParamId || id == kSpatialFollowParamId
        || id == kSpaceParamId || id == kEnvironmentSizeParamId
        || id == kEnvironmentDecayParamId || id == kEnvironmentDampingParamId
        || id == kFieldListenAmountParamId || id == kFoamParamId || id == kShoreParamId) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in,
    const clap_output_events_t* out)
{
    auto* p = self(plugin);
    serviceGuiParamEvents(*p, out);
    readParamEvents(*p, in);
    retryControlSnapshotPublication(*p);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    const auto snapshot = controlSnapshot(*p);
    SavedState state {};
    state.version = kStateVersion;
    state.params = snapshot.params;
    state.presetIndex = snapshot.presetIndex;
    copyName(state.customPresetName,
        sizeof(state.customPresetName), snapshot.customPresetName);
    state.surface = snapshot.surface;
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    SavedState loaded {};
    loaded.version = kStateVersion;
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(state.version), sizeof(state) - sizeof(state.version))) return false;
        loaded = state;
    } else if (version == 5u) {
        s3g::AmbiWaterParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiWaterParams, foam);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        loaded.params = params;
        loaded.presetIndex = presetIndex;
        copyName(loaded.customPresetName,
            sizeof(loaded.customPresetName), customPresetName);
    } else if (version == 4u) {
        s3g::AmbiWaterParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize =
            offsetof(s3g::AmbiWaterParams, fieldListenAmount);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        loaded.params = params;
        loaded.presetIndex = presetIndex;
        copyName(loaded.customPresetName,
            sizeof(loaded.customPresetName), customPresetName);
    } else if (version == 3u) {
        s3g::AmbiWaterParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiWaterParams, fieldListenMode);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        loaded.params = params;
        loaded.presetIndex = presetIndex;
        copyName(loaded.customPresetName,
            sizeof(loaded.customPresetName), customPresetName);
    } else if (version == 2u) {
        s3g::AmbiWaterParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiWaterParams, environmentSize);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        loaded.params = params;
        loaded.presetIndex = presetIndex;
        copyName(loaded.customPresetName,
            sizeof(loaded.customPresetName), customPresetName);
    } else if (version == 1u) {
        s3g::AmbiWaterParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiWaterParams, place);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        loaded.params = params;
        loaded.presetIndex = presetIndex;
        copyName(loaded.customPresetName,
            sizeof(loaded.customPresetName), customPresetName);
    } else {
        return false;
    }
    loaded.presetIndex = std::min<uint32_t>(loaded.presetIndex,
        s3g::kAmbiWaterFactoryPresetCount - 1u);
    s3g::sanitizeParameterSurface(loaded.surface);
    if (p->active.load(std::memory_order_acquire)) {
        if (!queueGuiState(*p, loaded,
                kReplaceSavedStateActionId)) return false;
    } else {
        p->params = loaded.params;
        p->presetIndex = loaded.presetIndex;
        copyName(p->customPresetName,
            sizeof(p->customPresetName), loaded.customPresetName);
        p->surface = loaded.surface;
        sanitizeWaterState(*p);
        p->engine.beginTransition();
    }
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
    { kOutputParamId, 630, 78, -60.0, 12.0, false },
    { kVoicesParamId, 630, 222, 1.0, 64.0, false },
    { kWaterParamId, 630, 248, 0.0, 1.0, false },
    { kFlowParamId, 630, 274, 0.0, 1.0, false },
    { kScaleParamId, 630, 300, 0.0, 1.0, false },
    { kTurbulenceParamId, 630, 326, 0.0, 1.0, false },
    { kAerationParamId, 630, 392, 0.0, 1.0, false },
    { kDropsParamId, 630, 418, 0.0, 1.0, false },
    { kSplashParamId, 630, 444, 0.0, 1.0, false },
    { kBubblesParamId, 630, 470, 0.0, 1.0, false },
    { kDensityParamId, 630, 496, 0.0, 1.0, false },
    { kEventSizeParamId, 630, 522, 0.0, 1.0, false },
    { kEventDecayParamId, 630, 548, 0.0, 1.0, false },
    { kDepthParamId, 630, 614, 0.0, 1.0, false },
    { kBrightnessParamId, 630, 640, 0.0, 1.0, false },
    { kResonanceParamId, 630, 666, 0.0, 1.0, false },
    { kDampingParamId, 630, 692, 0.0, 1.0, false },
    { kContactParamId, 630, 718, 0.0, 1.0, false },
    { kMotionRateParamId, 896, 78, 0.002, 3.0, true },
    { kCurrentParamId, 896, 104, 0.0, 1.0, false },
    { kSlopeParamId, 896, 130, -1.0, 1.0, false },
    { kEddyParamId, 896, 156, 0.0, 1.0, false },
    { kConvergenceParamId, 896, 182, 0.0, 1.0, false },
    { kWidthParamId, 896, 208, 0.0, 1.0, false },
    { kSpreadParamId, 896, 234, 0.0, 1.0, false },
    { kDeviationParamId, 896, 260, 0.0, 1.0, false },
    { kSpatialFollowParamId, 896, 286, 0.0, 1.0, false },
    { kAzimuthParamId, 896, 352, -180.0, 180.0, false },
    { kElevationParamId, 896, 378, -90.0, 90.0, false },
    { kDistanceParamId, 896, 404, 0.15, 2.0, false },
    { kSpaceParamId, 896, 496, 0.0, 1.0, false },
    { kEnvironmentSizeParamId, 896, 522, 0.0, 1.0, false },
    { kEnvironmentDecayParamId, 896, 548, 0.0, 1.0, false },
    { kEnvironmentDampingParamId, 896, 574, 0.0, 1.0, false },
    { kFieldListenAmountParamId, 896, 626, 0.0, 1.0, false },
    { kFoamParamId, 896, 692, 0.0, 1.0, false },
    { kShoreParamId, 896, 718, 0.0, 1.0, false },
};

const GuiSliderSpec* guiSliderSpec(clap_id id)
{
    for (const auto& spec : kGuiSliders) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

double sliderNorm(const GuiSliderSpec& spec, double value)
{
    if (spec.id == kAzimuthParamId) return
        s3g::aedAzimuthSliderNorm(static_cast<float>(value));
    if (spec.logarithmic) {
        if (spec.min <= 0.0) {
            if (value <= 0.0) return 0.0;
            constexpr double zeroZone = 0.02;
            const double minPositive = std::max(0.000001, spec.max * 0.001);
            const double logNorm = std::log(std::max(minPositive, value) / minPositive)
                / std::log(spec.max / minPositive);
            return std::clamp(zeroZone + (1.0 - zeroZone) * logNorm, zeroZone, 1.0);
        }
        const double minValue = std::max(0.000001, spec.min);
        return std::clamp(std::log(std::max(minValue, value) / minValue) / std::log(spec.max / minValue), 0.0, 1.0);
    }
    return std::clamp((value - spec.min) / std::max(0.000001, spec.max - spec.min), 0.0, 1.0);
}

double sliderValue(const GuiSliderSpec& spec, NSPoint point)
{
    const double norm = std::clamp((static_cast<double>(point.x) - (spec.panelX + 108.0)) / 82.0, 0.0, 1.0);
    if (spec.id == kAzimuthParamId) return
        s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm));
    if (spec.logarithmic) {
        if (spec.min <= 0.0) {
            constexpr double zeroZone = 0.02;
            if (norm <= zeroZone) return 0.0;
            const double minPositive = std::max(0.000001, spec.max * 0.001);
            const double logNorm = (norm - zeroZone) / (1.0 - zeroZone);
            return minPositive * std::pow(spec.max / minPositive, logNorm);
        }
        return spec.min * std::pow(spec.max / spec.min, norm);
    }
    return spec.min + norm * (spec.max - spec.min);
}

@interface S3GAmbiWaterEncoderView : NSView <NSWindowDelegate> {
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
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
    BOOL _surfaceEdit;
    int _selectedSurfaceCell;
    int _dragSurfaceCell;
    BOOL _dragSurfaceCursor;
    BOOL _surfacePopupChild;
    S3GAmbiWaterEncoderView* _surfacePopupOwner;
    NSPanel* _surfacePanel;
    S3GAmbiWaterEncoderView* _surfacePopupView;
    NSClipView* _surfacePopupClip;
    s3g::AmbiWaterParams _paramsSnapshot;
    s3g::AmbiWaterParams _displayParamsSnapshot;
    WaterSurface _surfaceSnapshot;
    uint32_t _presetIndexSnapshot;
    char _customPresetNameSnapshot[64];
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)refreshControlSnapshot;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)openSurfacePopup;
- (void)hideSurfacePopup;
- (void)destroySurfacePopup;
@end

@implementation S3GAmbiWaterEncoderView
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
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _openMenuRect = NSZeroRect;
        _surfaceEdit = NO;
        _selectedSurfaceCell = -1;
        _dragSurfaceCell = -1;
        _dragSurfaceCursor = NO;
        _surfacePopupChild = NO;
        _surfacePopupOwner = nil;
        _surfacePanel = nil;
        _surfacePopupView = nil;
        _surfacePopupClip = nil;
        const auto snapshot = controlSnapshot(*plugin);
        _paramsSnapshot = snapshot.params;
        _displayParamsSnapshot = snapshot.effectiveParams;
        _surfaceSnapshot = snapshot.surface;
        _presetIndexSnapshot = snapshot.presetIndex;
        copyName(_customPresetNameSnapshot,
            sizeof(_customPresetNameSnapshot),
            snapshot.customPresetName);
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    if (!_surfacePopupChild) [self destroySurfacePopup];
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

- (void)refreshControlSnapshot
{
    if (!_plugin) return;
    const auto snapshot = controlSnapshot(*_plugin);
    _paramsSnapshot = snapshot.params;
    _displayParamsSnapshot = snapshot.effectiveParams;
    _surfaceSnapshot = snapshot.surface;
    _presetIndexSnapshot = snapshot.presetIndex;
    copyName(_customPresetNameSnapshot,
        sizeof(_customPresetNameSnapshot),
        snapshot.customPresetName);
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 608); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 558); }
- (NSRect)presetMenuRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Preset); }
- (NSRect)loadPresetButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Load); }
- (NSRect)savePresetButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Save); }
- (NSRect)randomizeButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Random); }

- (NSRect)pageButtonRect:(int)index
{
    return s3g::clap_gui::environmentalFieldPageButtonRect(
        [self fieldPanelRect], static_cast<uint32_t>(index));
}

- (NSRect)surfacePlotRect
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 10.0, field.origin.y + 76.0,
        field.size.width - 20.0, field.size.height - 88.0);
}

- (NSRect)surfaceButtonRect:(int)index
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 10.0 + index * 52.0,
        field.origin.y + 10.0, 46.0, 16.0);
}

- (void)syncSurfaceEditMode:(BOOL)editing
{
    _surfaceEdit = editing;
    if (_surfacePopupChild && _surfacePopupOwner) {
        _surfacePopupOwner->_surfaceEdit = editing;
        [_surfacePopupOwner setNeedsDisplay:YES];
    } else if (_surfacePopupView) {
        _surfacePopupView->_surfaceEdit = editing;
        [_surfacePopupView setNeedsDisplay:YES];
    }
}

- (void)openSurfacePopup
{
    if (_surfacePopupChild) return;
    const NSRect source = [self fieldPanelRect];
    if (!_surfacePanel) {
        _surfacePanel = [[NSPanel alloc] initWithContentRect:
            NSMakeRect(0.0, 0.0, source.size.width, source.size.height)
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                | NSWindowStyleMaskUtilityWindow)
            backing:NSBackingStoreBuffered defer:NO];
        [_surfacePanel setTitle:@"s3g AMBI ENCODER WATER — SURF"];
        [_surfacePanel setReleasedWhenClosed:NO];
        [_surfacePanel setHidesOnDeactivate:YES];
        [_surfacePanel setDelegate:self];
        _surfacePopupView = [[S3GAmbiWaterEncoderView alloc]
            initWithPlugin:_plugin];
        _surfacePopupView->_surfacePopupChild = YES;
        _surfacePopupView->_surfacePopupOwner = self;
        _surfacePopupView->_fieldPage = 1;
        _surfacePopupView->_surfaceEdit = _surfaceEdit;
        _surfacePopupClip = [[NSClipView alloc] initWithFrame:
            NSMakeRect(0.0, 0.0, source.size.width, source.size.height)];
        [_surfacePopupClip setDrawsBackground:NO];
        [_surfacePopupClip setDocumentView:_surfacePopupView];
        [_surfacePanel setContentView:_surfacePopupClip];
        [_surfacePopupClip setBoundsOrigin:source.origin];
        [_surfacePopupView release];
        [_surfacePopupClip release];

        NSWindow* parent = [self window];
        const NSRect parentFrame = parent ? [parent frame]
            : [[NSScreen mainScreen] visibleFrame];
        const NSRect panelFrame = [_surfacePanel frame];
        CGFloat x = NSMaxX(parentFrame) + 8.0;
        CGFloat y = NSMaxY(parentFrame) - panelFrame.size.height;
        NSScreen* screen = parent ? [parent screen] : [NSScreen mainScreen];
        const NSRect visible = screen ? [screen visibleFrame] : parentFrame;
        if (x + panelFrame.size.width > NSMaxX(visible)) {
            x = NSMinX(parentFrame) - panelFrame.size.width - 8.0;
        }
        [_surfacePanel setFrameOrigin:NSMakePoint(
            std::max(NSMinX(visible), x),
            std::clamp(y, NSMinY(visible), std::max(NSMinY(visible),
                NSMaxY(visible) - panelFrame.size.height)))];
    }
    _surfacePopupView->_fieldPage = 1;
    [self syncSurfaceEditMode:_surfaceEdit];
    _fieldPage = 0;
    NSWindow* parent = [self window];
    NSWindow* previousParent = [_surfacePanel parentWindow];
    if (previousParent && previousParent != parent) {
        [previousParent removeChildWindow:_surfacePanel];
    }
    if (parent && [_surfacePanel parentWindow] != parent) {
        [parent addChildWindow:_surfacePanel ordered:NSWindowAbove];
    }
    [_surfacePopupView startRefreshTimer];
    [_surfacePanel makeKeyAndOrderFront:nil];
    [self setNeedsDisplay:YES];
}

- (void)hideSurfacePopup
{
    if (!_surfacePanel) return;
    [_surfacePopupView stopRefreshTimer];
    NSWindow* parent = [_surfacePanel parentWindow];
    if (parent) [parent removeChildWindow:_surfacePanel];
    [_surfacePanel orderOut:nil];
}

- (void)destroySurfacePopup
{
    if (!_surfacePanel) return;
    [_surfacePopupView stopRefreshTimer];
    [_surfacePanel setDelegate:nil];
    NSWindow* parent = [_surfacePanel parentWindow];
    if (parent) [parent removeChildWindow:_surfacePanel];
    [_surfacePanel orderOut:nil];
    [_surfacePanel release];
    _surfacePanel = nil;
    _surfacePopupView = nil;
    _surfacePopupClip = nil;
}

- (void)windowWillClose:(NSNotification*)notification
{
    if ([notification object] != _surfacePanel) return;
    [_surfacePopupView stopRefreshTimer];
    NSWindow* parent = [_surfacePanel parentWindow];
    if (parent) [parent removeChildWindow:_surfacePanel];
}

- (NSRect)surfaceCurveRect
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 10.0, field.origin.y + 38.0,
        104.0, 18.0);
}

- (NSRect)surfaceFocusRect:(int)index
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 170.0 + index * 24.0,
        field.origin.y + 38.0, 20.0, 18.0);
}

- (NSRect)surfaceGlideRect:(int)index
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 336.0 + index * 24.0,
        field.origin.y + 38.0, 20.0, 18.0);
}

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
    if (!_plugin || voice >= s3g::kAmbiWaterMaxVoices) return { 0.0f, 0.0f, 0.0f };
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
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Water Encoder"];
}

- (void)saveCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gwater" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3gwater", [self presetDisplayName]]];
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* name = [[[[panel URL] lastPathComponent] stringByDeletingPathExtension] copy];
    if (saveCustomPresetFile([[[panel URL] path] UTF8String],
            _paramsSnapshot, [name UTF8String])) {
        (void)queueGuiMetadata(*_plugin, _presetIndexSnapshot,
            [name UTF8String]);
        std::snprintf(_customPresetNameSnapshot,
            sizeof(_customPresetNameSnapshot), "%s", [name UTF8String]);
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
    [panel setAllowedFileTypes:@[ @"s3gwater" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return;
    file.params.outputGainDb = _paramsSnapshot.outputGainDb;
    SavedState state {};
    state.params = file.params;
    state.surface = _surfaceSnapshot;
    s3g::bypassParameterSurfaceForSceneChange(state.surface);
    state.presetIndex = 0u;
    copyName(state.customPresetName,
        sizeof(state.customPresetName),
        file.name[0] ? file.name : "Custom");
    if (!queueGuiState(*_plugin, state,
            kReplaceCustomStateActionId)) {
        NSBeep();
        return;
    }
    _surfaceSnapshot = state.surface;
    _paramsSnapshot = file.params;
    _presetIndexSnapshot = 0u;
    std::snprintf(_customPresetNameSnapshot,
        sizeof(_customPresetNameSnapshot), "%s",
        file.name[0] ? file.name : "Custom");
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

- (void)drawSurfacePage:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    const NSRect header = NSMakeRect(field.origin.x, field.origin.y,
        field.size.width, 28.0);
    static NSString* labels[] = { @"PLAY", @"ON", @"ADD", @"DEL", @"CAP", @"POP" };
    labels[0] = _surfaceEdit ? @"EDIT" : @"PLAY";
    labels[1] = _surfaceSnapshot.enabled ? @"ON" : @"OFF";
    for (int index = 0; index < 6; ++index) {
        const BOOL active = (index == 0 && _surfaceEdit)
            || (index == 1 && _surfaceSnapshot.enabled)
            || (index == 5 && (_surfacePopupChild || [_surfacePanel isVisible]));
        s3g::clap_gui::drawHeaderButton([self surfaceButtonRect:index],
            header, labels[index], active, attrs, style);
    }
    const NSRect curve = [self surfaceCurveRect];
    [style.strip setFill]; NSRectFill(curve);
    [style.grid setStroke]; NSFrameRect(curve);
    [[NSString stringWithFormat:@"CURVE  %s",
        s3g::parameterSurfaceCurveName(_surfaceSnapshot.curve)]
        drawAtPoint:NSMakePoint(curve.origin.x + 5.0, curve.origin.y + 2.0)
        withAttributes:valueAttrs];
    [@"FOCUS" drawAtPoint:NSMakePoint(field.origin.x + 122.0,
        field.origin.y + 40.0) withAttributes:attrs];
    s3g::clap_gui::drawHeaderButton([self surfaceFocusRect:0], header,
        @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self surfaceFocusRect:1], header,
        @"+", false, attrs, style);
    [[NSString stringWithFormat:@"%.2f", _surfaceSnapshot.focus]
        drawAtPoint:NSMakePoint(field.origin.x + 222.0, field.origin.y + 40.0)
        withAttributes:valueAttrs];
    [@"GLIDE" drawAtPoint:NSMakePoint(field.origin.x + 280.0,
        field.origin.y + 40.0) withAttributes:attrs];
    s3g::clap_gui::drawHeaderButton([self surfaceGlideRect:0], header,
        @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self surfaceGlideRect:1], header,
        @"+", false, attrs, style);
    NSString* glide = _surfaceSnapshot.glideMs < 0.5f ? @"OFF"
        : [NSString stringWithFormat:@"%.0f MS", _surfaceSnapshot.glideMs];
    [glide drawAtPoint:NSMakePoint(field.origin.x + 388.0,
        field.origin.y + 40.0) withAttributes:valueAttrs];
    const NSRect plot = [self surfacePlotRect];
    const bool audioActive = _plugin->active.load(
        std::memory_order_acquire);
    const float effectiveX = audioActive
        ? _plugin->effectiveSurfaceX.load(std::memory_order_relaxed)
        : _paramsSnapshot.surfaceX;
    const float effectiveY = audioActive
        ? _plugin->effectiveSurfaceY.load(std::memory_order_relaxed)
        : _paramsSnapshot.surfaceY;
    s3g::clap_gui::drawParameterSurfaceVoronoi(_surfaceSnapshot, plot,
        effectiveX, effectiveY,
        _selectedSurfaceCell, valueAttrs,
        _paramsSnapshot.surfaceX, _paramsSnapshot.surfaceY);
    [[NSString stringWithFormat:@"T %.3f %.3f   /   A %.3f %.3f   /   %u CELLS   /   %@",
        _paramsSnapshot.surfaceX, _paramsSnapshot.surfaceY,
        effectiveX, effectiveY,
        _surfaceSnapshot.cellCount,
        _surfaceSnapshot.cellCount < 2u ? @"ADD TWO CELLS TO ENABLE" :
            (_surfaceSnapshot.enabled ? @"INTERPOLATING" : @"BYPASSED")]
        drawAtPoint:NSMakePoint(plot.origin.x + 8.0, NSMaxY(plot) - 18.0)
        withAttributes:valueAttrs];
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"WATER PARCEL FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:0], header,
        @"FIELD", _fieldPage == 0, attrs, style);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:1], header,
        @"SURF", _fieldPage == 1, attrs, style);
    if (_fieldPage == 1) {
        [self drawSurfacePage:attrs valueAttrs:valueAttrs style:style];
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
        _surfaceEdit ? _paramsSnapshot.voices
            : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiWaterMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<NSPoint, s3g::kAmbiWaterMaxVoices> projected {};
    for (uint32_t voice = 0; voice < voices; ++voice) projected[voice] = [self projectVoice:voice depth:nullptr];
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const uint32_t next = (voice + 1u) % voices;
        const float edgeMembership = _surfaceEdit ? 1.0f
            : std::min(
                _plugin->guiRenderGain[voice].load(
                    std::memory_order_relaxed),
                _plugin->guiRenderGain[next].load(
                    std::memory_order_relaxed));
        [s3g::clap_gui::color(0x5c5c5c,
            0.18 * std::clamp(edgeMembership, 0.0f, 1.0f)) setStroke];
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
        const float event = std::clamp(_plugin->guiEvent[voice].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float membership = _surfaceEdit ? 1.0f
            : std::clamp(_plugin->guiRenderGain[voice].load(
                std::memory_order_relaxed), 0.0f, 1.0f);
        const float activity = std::clamp(std::sqrt(std::max(0.0f, energy)) * 18.0f, 0.0f, 1.0f);
        const CGFloat base = voices > 32u ? 7.0 : 9.0;
        const CGFloat size = ((selected ? base + 5.0 : base)
            + activity * 5.0f + event * 7.0f)
            * (0.45f + membership * 0.55f);
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        if (event > 0.04f || activity > 0.04f) {
            const CGFloat halo = size * (1.15 + event * 1.9 + activity * 0.6);
            NSRect haloRect = NSMakeRect(projected[voice].x - halo * 0.5, projected[voice].y - halo * 0.5, halo, halo);
            [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:(0.05 + event * 0.18 + activity * 0.08) * membership] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:haloRect] fill];
        }
        [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:(selected ? 0.98 : 0.22 + event * 0.54 + activity * 0.20) * membership] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe6e6e6 : 0x4f4f4f, (selected ? 1.0 : 0.22 + event * 0.46 + activity * 0.18) * membership) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        if (membership > 0.45f && (event > 0.28f || selected)) {
            [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5, NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
        }
    }
    [NSGraphicsContext restoreGraphicsState];

    const float az = _plugin->guiAzimuth[_selectedVoice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[_selectedVoice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[_selectedVoice].load(std::memory_order_relaxed);
    const float energy = _plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed);
    const float event = _plugin->guiEvent[_selectedVoice].load(std::memory_order_relaxed);
    NSString* readout = [NSString stringWithFormat:@"P%02u  AZ%+.1f  EL%+.1f  D%.2f  EVT%.2f  E%.3f", _selectedVoice + 1u, az, el, dist, event, energy];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
    [@"PARCEL VELOCITY + IMPACT ENERGY     DIRECT ACN/SN3D" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19) withAttributes:valueAttrs];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    char display[64] {};
    paramsValueToText(nullptr, param, value, display, sizeof(display));
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:display], sliderNorm(*spec, value), spec->y,
        attrs, valueAttrs, style, spec->panelX + 16, spec->panelX + 108, spec->panelX + 196, 82);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value panelX:(CGFloat)panelX y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (NSString*)presetDisplayName
{
    if (_customPresetNameSnapshot[0]) return [NSString stringWithFormat:@"CUSTOM: %s", _customPresetNameSnapshot];
    return [NSString stringWithUTF8String:s3g::ambiWaterFactoryPresetInfo(_presetIndexSnapshot).name];
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _surfaceEdit
        ? _paramsSnapshot : _displayParamsSnapshot;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 80, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 630, 42, 250, 21, attrs, style);
    [self drawSlider:@"OUT" param:kOutputParamId value:p.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] panelX:630 y:104 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 134, 250, 210, style);
    s3g::clap_gui::drawPanelHeader(@"WATER SOURCE", true, 630, 134, 250, 21, attrs, style);
    [self drawMenu:@"WATER REGIME" value:[NSString stringWithUTF8String:kRegimeNames[
        std::min<uint32_t>(p.regime, s3g::kAmbiWaterRegimeCount - 1u)]] panelX:630 y:170 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ENVIRONMENT" value:[NSString stringWithUTF8String:kEnvironmentNames[
        std::min<uint32_t>(p.environment, s3g::kAmbiWaterEnvironmentCount - 1u)]] panelX:630 y:196 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:p.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"WATER" param:kWaterParamId value:p.water attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FLOW" param:kFlowParamId value:p.flow attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SCALE" param:kScaleParamId value:p.scale attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TURBULENCE" param:kTurbulenceParamId value:p.turbulence attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 356, 250, 210, style);
    s3g::clap_gui::drawPanelHeader(@"EVENTS", true, 630, 356, 250, 21, attrs, style);
    [self drawSlider:@"AERATION" param:kAerationParamId value:p.aeration attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DROPS" param:kDropsParamId value:p.drops attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPLASH" param:kSplashParamId value:p.splash attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BUBBLES" param:kBubblesParamId value:p.bubbles attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"EVENT DENSITY" param:kDensityParamId value:p.density attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"EVENT SIZE" param:kEventSizeParamId value:p.eventSize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"EVENT LIFE" param:kEventDecayParamId value:p.eventDecay attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 578, 250, 158, style);
    s3g::clap_gui::drawPanelHeader(@"BODY AND EVENTS", true, 630, 578, 250, 21, attrs, style);
    [self drawSlider:@"DEPTH" param:kDepthParamId value:p.depth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BRIGHTNESS" param:kBrightnessParamId value:p.brightness attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BUBBLE RISE" param:kResonanceParamId value:p.resonance attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DAMPING" param:kDampingParamId value:p.damping attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"IMPACT TEXTURE" param:kContactParamId value:p.contact attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 262, style);
    s3g::clap_gui::drawPanelHeader(@"PARCEL MOTION", true, 896, 42, 246, 21, attrs, style);
    [self drawSlider:@"PARCEL RATE" param:kMotionRateParamId value:p.motionRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CURRENT" param:kCurrentParamId value:p.current attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SLOPE" param:kSlopeParamId value:p.slope attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"EDDY" param:kEddyParamId value:p.eddy attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CONVERGENCE" param:kConvergenceParamId value:p.convergence attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"WIDTH" param:kWidthParamId value:p.width attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPREAD" param:kSpreadParamId value:p.spread attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEVIATION" param:kDeviationParamId value:p.deviation attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"INERTIA" param:kSpatialFollowParamId value:p.spatialFollow attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 316, 246, 106, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD ORIGIN", true, 896, 316, 246, 21, attrs, style);
    [self drawSlider:@"DIRECTION" param:kAzimuthParamId value:p.centerAzimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEVATION" param:kElevationParamId value:p.centerElevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RANGE" param:kDistanceParamId value:p.centerDistance attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 434, 246, 302, style);
    s3g::clap_gui::drawPanelHeader(@"ENVIRONMENT FIELD", true, 896, 434, 246, 21, attrs, style);
    [self drawMenu:@"PLACE" value:[NSString stringWithUTF8String:kPlaceNames[
        std::min<uint32_t>(p.place, s3g::kAmbiWaterPlaceCount - 1u)]] panelX:896 y:470 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV RETURN" param:kSpaceParamId value:p.space attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV SIZE" param:kEnvironmentSizeParamId value:p.environmentSize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DECAY" param:kEnvironmentDecayParamId value:p.environmentDecay attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DAMPING" param:kEnvironmentDampingParamId value:p.environmentDamping attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"LISTEN" value:[NSString stringWithUTF8String:kFieldListenNames[
        std::min<uint32_t>(static_cast<uint32_t>(p.fieldListenMode), 3u)]]
        panelX:896 y:600 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"LISTEN AMOUNT" param:kFieldListenAmountParamId
        value:p.fieldListenAmount attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"RESPONSE" value:[NSString stringWithUTF8String:
        kFieldListenResponseNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.fieldListenResponse), 3u)]]
        panelX:896 y:652 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FOAM" param:kFoamParamId value:p.foam attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SHORE CONTACT" param:kShoreParamId value:p.shore attrs:attrs valueAttrs:valueAttrs style:style];
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return [self presetMenuRect];
    case 2: return NSMakeRect(738, 169, 124, 15);
    case 3: return NSMakeRect(738, 195, 124, 15);
    case 4: return NSMakeRect(1004, 469, 124, 15);
    case 5: return NSMakeRect(1004, 599, 124, 15);
    case 6: return NSMakeRect(1004, 651, 124, 15);
    case 7: return NSZeroRect;
    case 8: return NSZeroRect;
    case 9: return NSZeroRect;
    case 10: return NSMakeRect(738, 103, 124, 15);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return s3g::kAmbiWaterFactoryPresetCount;
    case 2: return s3g::kAmbiWaterRegimeCount;
    case 3: return s3g::kAmbiWaterEnvironmentCount;
    case 4: return s3g::kAmbiWaterPlaceCount;
    case 5: return 4u;
    case 6: return 4u;
    case 7: return 0u;
    case 8: return 0u;
    case 9: return 0u;
    case 10: return 7u;
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
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* regimeItems[] = { @"CURRENT", @"RAIN", @"CASCADE", @"SURGE",
        @"VORTEX", @"SLOSH", @"DRIP", @"BUBBLE PLUME", @"BREAKER",
        @"BACKWASH", @"HAIL", @"SLEET", @"FREEZING RAIN" };
    static NSString* environmentItems[] = { @"OPEN", @"ROCK", @"LEAVES", @"MUD", @"CONCRETE", @"METAL", @"GLASS", @"PIPE", @"CAVE", @"ICE" };
    static NSString* placeItems[] = { @"OPEN", @"SUBMERGED", @"CAVE", @"CISTERN", @"CHANNEL", @"PIPE" };
    static NSString* listenItems[] = { @"OFF", @"FOLLOW", @"COUNTER", @"BALANCE" };
    static NSString* responseItems[] = {
        @"LEGACY", @"ACCRETE", @"SETTLE", @"IMPRINT"
    };
    static NSString* presetItems[s3g::kAmbiWaterFactoryPresetCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t i = 0; i < s3g::kAmbiWaterFactoryPresetCount; ++i) presetItems[i] = [[NSString stringWithUTF8String:s3g::ambiWaterFactoryPresetInfo(i).name] retain];
    });
    NSString** items = presetItems;
    int selected = static_cast<int>(_presetIndexSnapshot);
    if (_openMenu == 2) {
        items = regimeItems;
        selected = static_cast<int>(_paramsSnapshot.regime);
    } else if (_openMenu == 3) {
        items = environmentItems;
        selected = static_cast<int>(_paramsSnapshot.environment);
    } else if (_openMenu == 4) {
        items = placeItems;
        selected = static_cast<int>(_paramsSnapshot.place);
    } else if (_openMenu == 5) {
        items = listenItems;
        selected = static_cast<int>(_paramsSnapshot.fieldListenMode);
    } else if (_openMenu == 6) {
        items = responseItems;
        selected = static_cast<int>(_paramsSnapshot.fieldListenResponse);
    } else if (_openMenu == 10) {
        items = orderItems;
        selected = static_cast<int>(_paramsSnapshot.order) - 1;
    }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    [self refreshControlSnapshot];
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    [@"s3g AMBI ENCODER WATER" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    s3g::clap_gui::drawEncoderPresetMenu(
        [self presetDisplayName],
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight),
        attrs, valueAttrs, style);
    s3g::clap_gui::drawHeaderActionButton([self loadPresetButtonRect], [self loadPresetButtonRect], @"LOAD", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self savePresetButtonRect], [self savePresetButtonRect], @"SAVE", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self randomizeButtonRect], [self randomizeButtonRect], @"RANDOM", attrs, style);
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)), kGuiWidth, 14, valueAttrs, 18);
    [self drawField:attrs valueAttrs:valueAttrs style:style];
    [self drawPanels:attrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (int)hitVoice:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    const uint32_t voices = std::clamp<uint32_t>(
        _surfaceEdit ? _paramsSnapshot.voices
            : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiWaterMaxVoices);
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

- (int)hitSurfaceCell:(NSPoint)point
{
    const NSRect plot = [self surfacePlotRect];
    int best = -1;
    CGFloat bestDistance = 15.0;
    for (uint32_t index = 0u; index < _surfaceSnapshot.cellCount; ++index) {
        const auto& cell = _surfaceSnapshot.cells[index];
        const NSPoint site = NSMakePoint(plot.origin.x + cell.x * plot.size.width,
            NSMaxY(plot) - cell.y * plot.size.height);
        const CGFloat distance = std::hypot(point.x - site.x, point.y - site.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(index);
        }
    }
    return best;
}

- (void)updateSurfacePosition:(NSPoint)point cursor:(BOOL)cursor
{
    const NSRect plot = [self surfacePlotRect];
    const float x = std::clamp(static_cast<float>(
        (point.x - plot.origin.x) / plot.size.width), 0.0f, 1.0f);
    const float y = std::clamp(static_cast<float>(
        (NSMaxY(plot) - point.y) / plot.size.height), 0.0f, 1.0f);
    if (cursor) {
        queueGuiParamValue(*_plugin, kSurfaceXParamId, x);
        queueGuiParamValue(*_plugin, kSurfaceYParamId, y);
        _paramsSnapshot.surfaceX = x;
        _paramsSnapshot.surfaceY = y;
    } else if (_dragSurfaceCell >= 0
        && static_cast<uint32_t>(_dragSurfaceCell) < _surfaceSnapshot.cellCount) {
        auto& cell = _surfaceSnapshot.cells[static_cast<uint32_t>(_dragSurfaceCell)];
        cell.x = x;
        cell.y = y;
        queueGuiSurface(*_plugin, _surfaceSnapshot);
    }
    requestSurfaceProcess(*_plugin);
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)point
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    queueGuiParamValue(*_plugin, param, sliderValue(*spec, point));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    [self refreshControlSnapshot];
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) queueGuiParamValue(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 2) queueGuiParamValue(*_plugin, kRegimeParamId, hit);
            else if (_openMenu == 3) queueGuiParamValue(*_plugin, kEnvironmentParamId, hit);
            else if (_openMenu == 4) queueGuiParamValue(*_plugin, kPlaceParamId, hit);
            else if (_openMenu == 5) queueGuiParamValue(*_plugin, kFieldListenModeParamId, hit);
            else if (_openMenu == 6) queueGuiParamValue(*_plugin, kFieldListenResponseParamId, hit);
            else if (_openMenu == 10) queueGuiParamValue(*_plugin, kOrderParamId, hit + 1);
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
        (void)queueGuiParamEvent(*_plugin,
            s3g::clap_gui::ParamEventKind::Value,
            kRandomizeActionId);
        requestSurfaceProcess(*_plugin);
        _selectedVoice = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(738, 103, 124, 15))) { [self openMenu:10]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 169, 124, 15))) { [self openMenu:2]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 195, 124, 15))) { [self openMenu:3]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 469, 124, 15))) { [self openMenu:4]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 599, 124, 15))) { [self openMenu:5]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 651, 124, 15))) { [self openMenu:6]; return; }
    const NSRect panel = [self fieldPanelRect];
    if (NSPointInRect(point, panel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, [self pageButtonRect:i])) {
                if (_surfacePopupChild) return;
                _fieldPage = i;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (_fieldPage == 1) {
            if (NSPointInRect(point, [self surfaceButtonRect:5])) {
                if (_surfacePopupChild) [_surfacePopupOwner hideSurfacePopup];
                else [self openSurfacePopup];
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, [self surfaceButtonRect:0])) {
                [self syncSurfaceEditMode:!_surfaceEdit];
            } else if (NSPointInRect(point, [self surfaceButtonRect:1])) {
                if (_surfaceSnapshot.cellCount < 2u) NSBeep();
                else {
                    _surfaceSnapshot.enabled = !_surfaceSnapshot.enabled;
                    queueGuiSurface(*_plugin, _surfaceSnapshot);
                    requestSurfaceProcess(*_plugin);
                }
            } else if (NSPointInRect(point, [self surfaceButtonRect:2])) {
                NSString* name = [self presetDisplayName];
                if (s3g::addParameterSurfaceCell(_surfaceSnapshot,
                        _paramsSnapshot, static_cast<int32_t>(_presetIndexSnapshot),
                        [name UTF8String])) {
                    _selectedSurfaceCell = static_cast<int>(
                        _surfaceSnapshot.cellCount) - 1;
                    queueGuiSurface(*_plugin, _surfaceSnapshot);
                } else NSBeep();
            } else if (NSPointInRect(point, [self surfaceButtonRect:3])) {
                if (_selectedSurfaceCell < 0
                    || !s3g::removeParameterSurfaceCell(_surfaceSnapshot,
                        static_cast<uint32_t>(_selectedSurfaceCell))) NSBeep();
                _selectedSurfaceCell = _surfaceSnapshot.cellCount == 0u ? -1
                    : std::min(_selectedSurfaceCell,
                        static_cast<int>(_surfaceSnapshot.cellCount) - 1);
                queueGuiSurface(*_plugin, _surfaceSnapshot);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceButtonRect:4])) {
                if (_selectedSurfaceCell < 0
                    || static_cast<uint32_t>(_selectedSurfaceCell)
                        >= _surfaceSnapshot.cellCount) NSBeep();
                else {
                    auto& cell = _surfaceSnapshot.cells[
                        static_cast<uint32_t>(_selectedSurfaceCell)];
                    cell.params = _paramsSnapshot;
                    cell.presetIndex = static_cast<int32_t>(_presetIndexSnapshot);
                    std::snprintf(cell.name, sizeof(cell.name), "%s",
                        [[self presetDisplayName] UTF8String]);
                    queueGuiSurface(*_plugin, _surfaceSnapshot);
                    requestSurfaceProcess(*_plugin);
                }
            } else if (NSPointInRect(point, [self surfaceCurveRect])) {
                const uint32_t next = (static_cast<uint32_t>(
                    _surfaceSnapshot.curve) + 1u)
                    % s3g::kParameterSurfaceCurveCount;
                _surfaceSnapshot.curve = static_cast<s3g::ParameterSurfaceCurve>(next);
                queueGuiSurface(*_plugin, _surfaceSnapshot);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceFocusRect:0])
                || NSPointInRect(point, [self surfaceFocusRect:1])) {
                const float scale = NSPointInRect(point,
                    [self surfaceFocusRect:0]) ? 0.8f : 1.25f;
                _surfaceSnapshot.focus = std::clamp(
                    _surfaceSnapshot.focus * scale, 0.25f, 8.0f);
                queueGuiSurface(*_plugin, _surfaceSnapshot);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceGlideRect:0])
                || NSPointInRect(point, [self surfaceGlideRect:1])) {
                const int direction = NSPointInRect(point,
                    [self surfaceGlideRect:0]) ? -1 : 1;
                _surfaceSnapshot.glideMs = s3g::parameterSurfaceSteppedGlide(
                    _surfaceSnapshot.glideMs, direction);
                queueGuiSurface(*_plugin, _surfaceSnapshot);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfacePlotRect])) {
                if (_surfaceEdit) {
                    const int cell = [self hitSurfaceCell:point];
                    if (cell >= 0) {
                        _selectedSurfaceCell = cell;
                        _dragSurfaceCell = cell;
                    }
                } else {
                    _dragSurfaceCursor = YES;
                    [self updateSurfacePosition:point cursor:YES];
                }
            }
            [self setNeedsDisplay:YES];
            return;
        }
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
    _dragParam = 0;
    for (const auto& spec : kGuiSliders) {
        if (NSPointInRect(point, NSMakeRect(spec.panelX + 8, spec.y - 8, 230, 24))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &_plugin->plugin, spec.id, &defaultValue)) {
                queueGuiParamValue(*_plugin, spec.id, defaultValue);
                _dragParam = 0;
                [self setNeedsDisplay:YES];
                return;
            }
            _dragParam = static_cast<int>(spec.id);
            [self setParam:spec.id fromPoint:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_fieldPage == 1 && (_dragSurfaceCursor || _dragSurfaceCell >= 0)) {
        [self updateSurfacePosition:point cursor:_dragSurfaceCursor];
        [self setNeedsDisplay:YES];
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
    _dragSurfaceCell = -1;
    _dragSurfaceCursor = NO;
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
    p->guiView = [[S3GAmbiWaterEncoderView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiWaterEncoderView*>(p->guiView) destroySurfacePopup];
    [static_cast<S3GAmbiWaterEncoderView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible.store(false, std::memory_order_release);
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
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible.store(true, std::memory_order_release); [static_cast<S3GAmbiWaterEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_release); [static_cast<S3GAmbiWaterEncoderView*>(p->guiView) hideSurfacePopup]; [static_cast<S3GAmbiWaterEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    "org.s3g.s3g-dsp.ambi-water-encoder-64",
    "s3g Ambi Encoder Water",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.2.0",
    "Procedural water environment whose 7OA ACN/SN3D field can steer its own current, eddies, and events.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiWaterFactoryPreset(0u);
    p->engine.prepare(p->sampleRate);
    sanitizeWaterState(*p);
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
