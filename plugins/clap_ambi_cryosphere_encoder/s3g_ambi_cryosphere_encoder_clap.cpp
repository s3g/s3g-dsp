#include "s3g_ambi_cryosphere_encoder.h"
#include "s3g_ambi_cryosphere_presets.h"
#include "s3g_parameter_surface.h"
#include "s3g_realtime.h"

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

constexpr uint32_t kOutputChannels = s3g::kAmbiCryosphereMaxChannels;
constexpr uint32_t kStateVersion = 9;
constexpr uint32_t kCustomPresetMagic = 0x31454349u; // ICE1
constexpr uint32_t kCustomPresetVersion = 9;

constexpr clap_id kPresetParamId = 1;
constexpr clap_id kOrderParamId = 2;
constexpr clap_id kVoicesParamId = 3;
constexpr clap_id kCryosphereParamId = 4;
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
constexpr clap_id kSurfaceLoadParamId = 47;
constexpr clap_id kSnapParamId = 48;
constexpr clap_id kPlateFailureParamId = 49;
constexpr clap_id kScorePaceParamId = 50;
constexpr clap_id kScoreOccupancyParamId = 51;
constexpr clap_id kScoreCascadeParamId = 52;
constexpr clap_id kScoreMemoryParamId = 53;
constexpr clap_id kScoreRestParamId = 54;

using CryosphereSurface = s3g::ParameterSurfaceState<s3g::AmbiCryosphereParams>;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiCryosphereParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    CryosphereSurface surface {};
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiCryosphereParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiCryosphereEncoder engine {};
    s3g::AmbiCryosphereParams params {};
    s3g::AmbiCryosphereParams effectiveParams {};
    CryosphereSurface surface {};
    std::atomic<float> effectiveSurfaceX { 0.5f };
    std::atomic<float> effectiveSurfaceY { 0.5f };
    std::atomic<bool> active { false };
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    uint32_t randomSeed = 0x6d2b79f5u;
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> guiScoreActivity { 0.0f };
    std::atomic<uint32_t> guiScoreEntities { 0u };
    std::atomic<uint64_t> guiScoreArcs { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    std::array<std::atomic<float>, s3g::kAmbiCryosphereMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiCryosphereMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiCryosphereMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiCryosphereMaxVoices> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiCryosphereMaxVoices> guiEvent {};
    std::array<std::atomic<float>, s3g::kAmbiCryosphereMaxVoices> guiRenderGain {};
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

bool saveCustomPresetFile(const char* path, const Plugin& plugin, const char* name)
{
    if (!path || !*path) return false;
    CustomPresetFile file {};
    std::snprintf(file.name, sizeof(file.name), "%s", name && *name ? name : "Custom");
    file.params = plugin.params;
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
            || file.version == 8u
            || file.version == kCustomPresetVersion)
        && std::fread(file.name, 1, sizeof(file.name), handle) == sizeof(file.name);
    if (ok) {
        const size_t paramsSize = file.version == 1u
            ? offsetof(s3g::AmbiCryosphereParams, place)
            : (file.version == 2u
                    ? offsetof(s3g::AmbiCryosphereParams, environmentSize)
                    : (file.version == 3u
                            ? offsetof(s3g::AmbiCryosphereParams, fieldListenMode)
                            : (file.version == 4u
                                    ? offsetof(s3g::AmbiCryosphereParams, fieldListenAmount)
                                    : (file.version == 5u
                                            ? offsetof(s3g::AmbiCryosphereParams, foam)
                                            : (file.version == 8u
                                                    ? offsetof(s3g::AmbiCryosphereParams, scorePace)
                                                    : sizeof(file.params))))));
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

float randomLogRange(uint32_t& seed, float minValue, float maxValue)
{
    return minValue * std::pow(maxValue / minValue, randomUnit(seed));
}

uint32_t randomChoice(uint32_t& seed, uint32_t count)
{
    return std::min<uint32_t>(count - 1u, static_cast<uint32_t>(randomUnit(seed) * static_cast<float>(count)));
}

constexpr const char* kRegimeNames[] = {
    "FROST CRACK", "ICE SEGREGATION", "PERMAFROST HEAVE", "BASAL STICK-SLIP",
    "PRESSURE RIDGE", "CALVING", "ICEBERG IMPACT", "AVALANCHE",
    "SNOWPACK CREEP", "HAIL", "SLEET", "FREEZING RAIN",
    "MELTWATER UNDER ICE", "SINGING LAKE"
};

constexpr const char* kEnvironmentNames[] = {
    "OPEN ICE", "ROCK", "SNOWPACK", "MORAINE", "CONCRETE", "METAL",
    "GLASS", "ICE TUNNEL", "ICE CAVE", "GLACIER"
};

constexpr const char* kPlaceNames[] = {
    "OPEN", "SUBMERGED", "CAVE", "CISTERN", "CHANNEL", "PIPE"
};

constexpr const char* kFieldListenNames[] = {
    "OFF", "FOLLOW", "COUNTER", "BALANCE"
};

constexpr const char* kFieldListenResponseNames[] = {
    "DIRECT", "ACCRETE", "SETTLE", "IMPRINT"
};

void applyEffectiveParams(Plugin& plugin);

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
    p.regime = randomChoice(seed, s3g::kAmbiCryosphereRegimeCount);
    p.environment = randomChoice(seed, s3g::kAmbiCryosphereEnvironmentCount);
    p.water = randomRange(seed, 0.18f, 0.72f);
    p.flow = randomLogRange(seed, 0.004f, 1.0f);
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
    p.place = randomChoice(seed, s3g::kAmbiCryospherePlaceCount);
    p.space = randomRange(seed, 0.08f, p.place == 0u ? 0.28f : 0.58f);
    p.environmentSize = randomRange(seed, 0.30f, 0.74f);
    p.environmentDecay = randomRange(seed, 0.34f, 0.78f);
    p.environmentDamping = randomRange(seed, 0.26f, 0.76f);
    p.foam = randomRange(seed, 0.04f, 0.72f);
    p.shore = randomRange(seed, 0.02f, 0.70f);
    p.surfaceLoad = randomRange(seed, 0.18f, 0.86f);
    p.snap = randomRange(seed, 0.42f, 0.96f);
    p.plateFailure = p.regime <= 4u
        ? randomRange(seed, 0.18f, 0.72f) : randomRange(seed, 0.04f, 0.42f);
    p.scorePace = randomRange(seed, 0.14f, 0.72f);
    p.scoreOccupancy = randomRange(seed, 0.08f, 0.46f);
    p.scoreCascade = randomRange(seed, 0.24f, 0.92f);
    p.scoreMemory = randomRange(seed, 0.46f, 0.94f);
    p.scoreRest = randomRange(seed, 0.52f, 0.96f);
    p.order = order;
    p.outputGainDb = outputGainDb;
    p.fieldListenMode = fieldListenMode;
    p.fieldListenAmount = fieldListenAmount;
    p.fieldListenResponse = fieldListenResponse;

    switch (p.regime) {
    case 0u: // Frost crack
        p.water = std::max(p.water, 0.58f);
        p.contact = std::max(p.contact, 0.72f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.66f);
        p.snap = std::max(p.snap, 0.76f);
        p.plateFailure = std::max(p.plateFailure, 0.30f);
        p.resonance = std::min(p.resonance, 0.34f);
        break;
    case 1u: // Ice segregation
        p.water = std::max(p.water, 0.76f);
        p.density = std::max(p.density, 0.50f);
        p.contact = std::max(p.contact, 0.72f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.58f);
        p.snap = std::max(p.snap, 0.68f);
        break;
    case 2u: // Permafrost heave
        p.scale = std::max(p.scale, 0.72f);
        p.convergence = std::max(p.convergence, 0.58f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.70f);
        p.snap = std::max(p.snap, 0.62f);
        p.plateFailure = std::max(p.plateFailure, 0.32f);
        p.motionRateHz = std::min(p.motionRateHz, 0.16f);
        break;
    case 3u: // Basal stick-slip
        p.scale = std::max(p.scale, 0.70f);
        p.current = std::max(p.current, 0.68f);
        p.convergence = std::max(p.convergence, 0.58f);
        p.shore = std::max(p.shore, 0.68f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.62f);
        p.plateFailure = std::max(p.plateFailure, 0.24f);
        break;
    case 4u: // Pressure ridge
        p.convergence = randomRange(seed, 0.66f, 0.98f);
        p.current = std::max(p.current, 0.58f);
        p.contact = std::max(p.contact, 0.72f);
        p.shore = std::max(p.shore, 0.68f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.78f);
        p.snap = std::max(p.snap, 0.78f);
        p.plateFailure = std::max(p.plateFailure, 0.58f);
        break;
    case 5u: // Calving
        p.water = std::max(p.water, 0.58f);
        p.scale = std::max(p.scale, 0.78f);
        p.splash = std::max(p.splash, 0.72f);
        p.eventSize = std::max(p.eventSize, 0.74f);
        p.depth = std::max(p.depth, 0.66f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.66f);
        p.snap = std::max(p.snap, 0.72f);
        p.plateFailure = std::max(p.plateFailure, 0.62f);
        break;
    case 6u: // Iceberg impact
        p.water = std::max(p.water, 0.58f);
        p.scale = std::max(p.scale, 0.82f);
        p.splash = std::max(p.splash, 0.76f);
        p.bubbles = std::max(p.bubbles, 0.58f);
        p.eventSize = std::max(p.eventSize, 0.80f);
        p.depth = std::max(p.depth, 0.76f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.48f);
        p.plateFailure = std::max(p.plateFailure, 0.54f);
        break;
    case 7u: // Avalanche
        p.aeration = std::max(p.aeration, 0.62f);
        p.density = std::max(p.density, 0.66f);
        p.foam = std::max(p.foam, 0.66f);
        p.current = std::max(p.current, 0.72f);
        p.slope = std::min(p.slope, -0.54f);
        p.plateFailure = std::max(p.plateFailure, 0.34f);
        break;
    case 8u: // Snowpack creep
        p.current = std::max(p.current, 0.46f);
        p.aeration = std::max(p.aeration, 0.42f);
        p.foam = std::max(p.foam, 0.64f);
        p.damping = std::max(p.damping, 0.72f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.42f);
        p.snap = std::min(p.snap, 0.58f);
        break;
    case 9u: // Hail
        p.water = std::min(p.water, 0.28f);
        p.drops = std::max(p.drops, 0.72f);
        p.contact = std::max(p.contact, 0.78f);
        p.brightness = std::max(p.brightness, 0.70f);
        p.surfaceLoad = std::min(p.surfaceLoad, 0.26f);
        p.plateFailure = std::min(p.plateFailure, 0.20f);
        break;
    case 10u: // Sleet
        p.drops = std::max(p.drops, 0.62f);
        p.contact = std::max(p.contact, 0.54f);
        p.damping = std::max(p.damping, 0.58f);
        p.surfaceLoad = std::min(p.surfaceLoad, 0.32f);
        break;
    case 11u: // Freezing rain
        p.drops = std::max(p.drops, 0.70f);
        p.contact = std::max(p.contact, 0.66f);
        p.resonance = std::min(p.resonance, 0.46f);
        p.surfaceLoad = std::max(p.surfaceLoad, 0.38f);
        p.snap = std::max(p.snap, 0.58f);
        break;
    case 12u: // Meltwater under ice
        p.water = std::max(p.water, 0.54f);
        p.bubbles = std::max(p.bubbles, 0.72f);
        p.current = std::max(p.current, 0.72f);
        p.shore = std::max(p.shore, 0.68f);
        p.environment = 9u;
        p.surfaceLoad = std::max(p.surfaceLoad, 0.40f);
        break;
    default: // Singing lake
        p.environment = 0u;
        p.voices = 6u + randomChoice(seed, 9u);
        p.flow = randomLogRange(seed, 0.012f, 0.12f);
        p.scale = randomRange(seed, 0.60f, 0.90f);
        p.turbulence = randomRange(seed, 0.08f, 0.32f);
        p.aeration = randomRange(seed, 0.0f, 0.08f);
        p.drops = randomRange(seed, 0.0f, 0.08f);
        p.splash = 0.0f;
        p.density = randomRange(seed, 0.08f, 0.24f);
        p.eventSize = randomRange(seed, 0.62f, 0.92f);
        p.eventDecay = randomRange(seed, 0.22f, 0.55f);
        p.depth = randomRange(seed, 0.68f, 0.92f);
        p.brightness = randomRange(seed, 0.32f, 0.62f);
        p.resonance = randomRange(seed, 0.45f, 0.78f);
        p.damping = randomRange(seed, 0.22f, 0.50f);
        p.current = randomRange(seed, 0.0f, 0.12f);
        p.convergence = randomRange(seed, 0.04f, 0.20f);
        p.surfaceLoad = randomRange(seed, 0.12f, 0.30f);
        p.plateFailure = randomRange(seed, 0.0f, 0.06f);
        break;
    }

    plugin.randomSeed = seed;
    plugin.params = p;
    plugin.presetIndex = 0u;
    std::snprintf(plugin.customPresetName, sizeof(plugin.customPresetName), "Random");
    applyEffectiveParams(plugin);
    plugin.engine.beginTransition();
}

bool assignParam(s3g::AmbiCryosphereParams& params, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: params.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kVoicesParamId: params.voices = static_cast<uint32_t>(std::lround(value)); return true;
    case kCryosphereParamId: params.water = static_cast<float>(value); return true;
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
    case kSurfaceLoadParamId: params.surfaceLoad = static_cast<float>(value); return true;
    case kSnapParamId: params.snap = static_cast<float>(value); return true;
    case kPlateFailureParamId: params.plateFailure = static_cast<float>(value); return true;
    case kScorePaceParamId: params.scorePace = static_cast<float>(value); return true;
    case kScoreOccupancyParamId: params.scoreOccupancy = static_cast<float>(value); return true;
    case kScoreCascadeParamId: params.scoreCascade = static_cast<float>(value); return true;
    case kScoreMemoryParamId: params.scoreMemory = static_cast<float>(value); return true;
    case kScoreRestParamId: params.scoreRest = static_cast<float>(value); return true;
    default: return false;
    }
}

s3g::AmbiCryosphereParams waterSurfaceParams(
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
#define S3G_CRYOSPHERE_SURFACE_BLEND(member) \
    result.member = s3g::parameterSurfaceBlend(plugin.surface, weights, \
        [](const s3g::AmbiCryosphereParams& p) { return p.member; }, base.member)
    S3G_CRYOSPHERE_SURFACE_BLEND(water);
    result.flow = s3g::parameterSurfaceBlendLog(
        plugin.surface, weights,
        [](const s3g::AmbiCryosphereParams& p) { return p.flow; },
        base.flow, 0.001f);
    S3G_CRYOSPHERE_SURFACE_BLEND(scale);
    S3G_CRYOSPHERE_SURFACE_BLEND(turbulence);
    S3G_CRYOSPHERE_SURFACE_BLEND(aeration);
    S3G_CRYOSPHERE_SURFACE_BLEND(spread);
    S3G_CRYOSPHERE_SURFACE_BLEND(deviation);
    S3G_CRYOSPHERE_SURFACE_BLEND(drops);
    S3G_CRYOSPHERE_SURFACE_BLEND(splash);
    S3G_CRYOSPHERE_SURFACE_BLEND(bubbles);
    S3G_CRYOSPHERE_SURFACE_BLEND(density);
    S3G_CRYOSPHERE_SURFACE_BLEND(eventSize);
    S3G_CRYOSPHERE_SURFACE_BLEND(eventDecay);
    S3G_CRYOSPHERE_SURFACE_BLEND(depth);
    S3G_CRYOSPHERE_SURFACE_BLEND(brightness);
    S3G_CRYOSPHERE_SURFACE_BLEND(resonance);
    S3G_CRYOSPHERE_SURFACE_BLEND(damping);
    S3G_CRYOSPHERE_SURFACE_BLEND(contact);
    S3G_CRYOSPHERE_SURFACE_BLEND(motionRateHz);
    S3G_CRYOSPHERE_SURFACE_BLEND(current);
    S3G_CRYOSPHERE_SURFACE_BLEND(slope);
    S3G_CRYOSPHERE_SURFACE_BLEND(eddy);
    S3G_CRYOSPHERE_SURFACE_BLEND(convergence);
    S3G_CRYOSPHERE_SURFACE_BLEND(width);
    result.centerAzimuthDeg = s3g::parameterSurfaceBlendAngleDegrees(
        plugin.surface, weights,
        [](const s3g::AmbiCryosphereParams& p) { return p.centerAzimuthDeg; },
        base.centerAzimuthDeg);
    S3G_CRYOSPHERE_SURFACE_BLEND(centerElevationDeg);
    S3G_CRYOSPHERE_SURFACE_BLEND(centerDistance);
    S3G_CRYOSPHERE_SURFACE_BLEND(spatialFollow);
    S3G_CRYOSPHERE_SURFACE_BLEND(space);
    S3G_CRYOSPHERE_SURFACE_BLEND(environmentSize);
    S3G_CRYOSPHERE_SURFACE_BLEND(environmentDecay);
    S3G_CRYOSPHERE_SURFACE_BLEND(environmentDamping);
    S3G_CRYOSPHERE_SURFACE_BLEND(fieldListenAmount);
    S3G_CRYOSPHERE_SURFACE_BLEND(foam);
    S3G_CRYOSPHERE_SURFACE_BLEND(shore);
    S3G_CRYOSPHERE_SURFACE_BLEND(surfaceLoad);
    S3G_CRYOSPHERE_SURFACE_BLEND(snap);
    S3G_CRYOSPHERE_SURFACE_BLEND(plateFailure);
    S3G_CRYOSPHERE_SURFACE_BLEND(scorePace);
    S3G_CRYOSPHERE_SURFACE_BLEND(scoreOccupancy);
    S3G_CRYOSPHERE_SURFACE_BLEND(scoreCascade);
    S3G_CRYOSPHERE_SURFACE_BLEND(scoreMemory);
    S3G_CRYOSPHERE_SURFACE_BLEND(scoreRest);
#undef S3G_CRYOSPHERE_SURFACE_BLEND
    result.order = base.order;
    result.outputGainDb = base.outputGainDb;
    result.surfaceX = base.surfaceX;
    result.surfaceY = base.surfaceY;
    return result;
}

s3g::AmbiCryosphereParams waterSurfaceParams(const Plugin& plugin)
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

void applyEffectiveParams(Plugin& plugin)
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
    plugin.engine.setParams(waterSurfaceParams(plugin, cursorX, cursorY));
    if (plugin.surface.enabled && plugin.surface.cellCount >= 2u) {
        const auto weights = s3g::parameterSurfaceWeights(
            plugin.surface, cursorX, cursorY);
        plugin.engine.setParameterSurfaceVoiceMembership(
            s3g::parameterSurfaceVoiceMembership<
                s3g::kAmbiCryosphereMaxVoices>(
                plugin.surface, weights, plugin.params.voices));
    } else {
        plugin.engine.clearParameterSurfaceVoiceMembership();
    }
    plugin.effectiveParams = plugin.engine.params();
}

void sanitizeCryosphereState(Plugin& plugin)
{
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
    s3g::sanitizeParameterSurface(plugin.surface);
    for (uint32_t index = 0u; index < plugin.surface.cellCount; ++index) {
        plugin.engine.setParams(plugin.surface.cells[index].params);
        plugin.surface.cells[index].params = plugin.engine.params();
    }
    applyEffectiveParams(plugin);
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
        p.presetIndex = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, s3g::kAmbiCryosphereFactoryPresetCount - 1u);
        p.customPresetName[0] = '\0';
        p.params = s3g::ambiCryosphereFactoryPreset(p.presetIndex);
        applyEffectiveParams(p);
        p.engine.beginTransition();
        return;
    }
    if (!assignParam(p.params, id, value)) return;
    applyEffectiveParams(p);
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
    p->active.store(false, std::memory_order_release);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    snapSurfaceCursor(*p);
    sanitizeCryosphereState(*p);
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
    p->guiScoreActivity.store(
        p->engine.scoreActivity(), std::memory_order_relaxed);
    p->guiScoreEntities.store(
        p->engine.scoredEntityCount(), std::memory_order_relaxed);
    p->guiScoreArcs.store(
        p->engine.scoreArcCount(), std::memory_order_relaxed);
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0u; ch < outChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
#if defined(__APPLE__)
    const uint32_t voices = std::min<uint32_t>(
        p->engine.processingVoiceCount(), s3g::kAmbiCryosphereMaxVoices);
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
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiCryosphereFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 28.0, true },
    { kCryosphereParamId, "Ice Growth", 0.0, 1.0, 0.58, false },
    { kFlowParamId, "Event Rate", 0.0, 1.0, 0.48, false },
    { kScaleParamId, "Slab Scale", 0.0, 1.0, 0.46, false },
    { kTurbulenceParamId, "Fracture Branching", 0.0, 1.0, 0.38, false },
    { kAerationParamId, "Granules", 0.0, 1.0, 0.30, false },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.58, false },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.14, false },
    { kRegimeParamId, "Geological Process", 0.0, static_cast<double>(s3g::kAmbiCryosphereRegimeCount - 1u), 0.0, true },
    { kEnvironmentParamId, "Environment", 0.0, static_cast<double>(s3g::kAmbiCryosphereEnvironmentCount - 1u), 0.0, true },
    { kDropsParamId, "Frozen Impacts", 0.0, 1.0, 0.26, false },
    { kSplashParamId, "Calving", 0.0, 1.0, 0.34, false },
    { kBubblesParamId, "Brine Pockets", 0.0, 1.0, 0.18, false },
    { kDensityParamId, "Fracture Density", 0.0, 1.0, 0.38, false },
    { kEventSizeParamId, "Fracture Mass", 0.0, 1.0, 0.42, false },
    { kEventDecayParamId, "Fracture Tail", 0.0, 1.0, 0.38, false },
    { kDepthParamId, "Depth", 0.0, 1.0, 0.48, false },
    { kBrightnessParamId, "Brightness", 0.0, 1.0, 0.44, false },
    { kResonanceParamId, "Body Scatter", 0.0, 1.0, 0.30, false },
    { kDampingParamId, "Damping", 0.0, 1.0, 0.52, false },
    { kContactParamId, "Brittleness", 0.0, 1.0, 0.30, false },
    { kMotionRateParamId, "Drift Rate", 0.002, 3.0, 0.12, false },
    { kCurrentParamId, "Drift", 0.0, 1.0, 0.54, false },
    { kSlopeParamId, "Slope", -1.0, 1.0, 0.18, false },
    { kEddyParamId, "Torque", 0.0, 1.0, 0.32, false },
    { kConvergenceParamId, "Compression", 0.0, 1.0, 0.18, false },
    { kWidthParamId, "Width", 0.0, 1.0, 0.68, false },
    { kAzimuthParamId, "Direction", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Range", 0.15, 2.0, 1.0, false },
    { kSpatialFollowParamId, "Inertia", 0.0, 1.0, 0.72, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kPlaceParamId, "Place", 0.0, static_cast<double>(s3g::kAmbiCryospherePlaceCount - 1u), 0.0, true },
    { kSpaceParamId, "Env Return", 0.0, 1.0, 0.18, false },
    { kEnvironmentSizeParamId, "Env Size", 0.0, 1.0, 0.5, false },
    { kEnvironmentDecayParamId, "Env Decay", 0.0, 1.0, 0.5, false },
    { kEnvironmentDampingParamId, "Env Damping", 0.0, 1.0, 0.5, false },
    { kFieldListenModeParamId, "Field Listen", 0.0, 3.0, 0.0, true },
    { kFieldListenAmountParamId, "Listen Amount", 0.0, 1.0, 1.0, false },
    { kFieldListenResponseParamId, "Listen Response", 0.0, 3.0, 0.0, true },
    { kSurfaceXParamId, "Surface X", 0.0, 1.0, 0.5, false },
    { kSurfaceYParamId, "Surface Y", 0.0, 1.0, 0.5, false },
    { kFoamParamId, "Snow", 0.0, 1.0, 0.0, false },
    { kShoreParamId, "Grinding", 0.0, 1.0, 0.0, false },
    { kSurfaceLoadParamId, "Surface Load", 0.0, 1.0, 0.20, false },
    { kSnapParamId, "Ice Snap", 0.0, 1.0, 0.48, false },
    { kPlateFailureParamId, "Plate Failure", 0.0, 1.0, 0.14, false },
    { kScorePaceParamId, "Score Pace", 0.0, 1.0, 0.46, false },
    { kScoreOccupancyParamId, "Entity Occupancy", 0.0, 1.0, 0.28, false },
    { kScoreCascadeParamId, "Causal Cascade", 0.0, 1.0, 0.58, false },
    { kScoreMemoryParamId, "Score Memory", 0.0, 1.0, 0.72, false },
    { kScoreRestParamId, "Scored Rest", 0.0, 1.0, 0.64, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

const char* paramModule(clap_id id)
{
    switch (id) {
    case kPresetParamId: return "Global";
    case kSurfaceXParamId:
    case kSurfaceYParamId: return "Parameter Surface";
    case kFoamParamId:
    case kShoreParamId: return "Snow and Grinding";
    case kSurfaceLoadParamId:
    case kSnapParamId:
    case kPlateFailureParamId: return "Structural Consequences";
    case kScorePaceParamId:
    case kScoreOccupancyParamId:
    case kScoreCascadeParamId:
    case kScoreMemoryParamId:
    case kScoreRestParamId: return "Aleatoric Entity Score";
    case kOrderParamId:
    case kRegimeParamId:
    case kEnvironmentParamId:
    case kVoicesParamId:
    case kCryosphereParamId:
    case kFlowParamId:
    case kScaleParamId:
    case kTurbulenceParamId: return "Cryosphere Source";
    case kAerationParamId:
    case kDropsParamId:
    case kSplashParamId:
    case kBubblesParamId:
    case kDensityParamId:
    case kEventSizeParamId:
    case kEventDecayParamId: return "Fracture Events";
    case kDepthParamId:
    case kBrightnessParamId:
    case kResonanceParamId:
    case kDampingParamId:
    case kContactParamId:
    case kOutputParamId: return "Ice Body and Modes";
    case kMotionRateParamId:
    case kCurrentParamId:
    case kSlopeParamId:
    case kEddyParamId:
    case kConvergenceParamId:
    case kWidthParamId:
    case kSpreadParamId:
    case kDeviationParamId:
    case kSpatialFollowParamId: return "Ice Field Motion";
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
    default: return "Ambi Cryosphere Encoder";
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
    if (!value) return false;
    auto* p = self(plugin);
    const auto params = p->params;
    switch (id) {
    case kPresetParamId: *value = p->presetIndex; return true;
    case kOrderParamId: *value = params.order; return true;
    case kVoicesParamId: *value = params.voices; return true;
    case kCryosphereParamId: *value = params.water; return true;
    case kFlowParamId: *value = params.flow; return true;
    case kScaleParamId: *value = params.scale; return true;
    case kTurbulenceParamId: *value = params.turbulence; return true;
    case kAerationParamId: *value = params.aeration; return true;
    case kSpreadParamId: *value = params.spread; return true;
    case kDeviationParamId: *value = params.deviation; return true;
    case kRegimeParamId: *value = params.regime; return true;
    case kEnvironmentParamId: *value = params.environment; return true;
    case kDropsParamId: *value = params.drops; return true;
    case kSplashParamId: *value = params.splash; return true;
    case kBubblesParamId: *value = params.bubbles; return true;
    case kDensityParamId: *value = params.density; return true;
    case kEventSizeParamId: *value = params.eventSize; return true;
    case kEventDecayParamId: *value = params.eventDecay; return true;
    case kDepthParamId: *value = params.depth; return true;
    case kBrightnessParamId: *value = params.brightness; return true;
    case kResonanceParamId: *value = params.resonance; return true;
    case kDampingParamId: *value = params.damping; return true;
    case kContactParamId: *value = params.contact; return true;
    case kMotionRateParamId: *value = params.motionRateHz; return true;
    case kCurrentParamId: *value = params.current; return true;
    case kSlopeParamId: *value = params.slope; return true;
    case kEddyParamId: *value = params.eddy; return true;
    case kConvergenceParamId: *value = params.convergence; return true;
    case kWidthParamId: *value = params.width; return true;
    case kAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kElevationParamId: *value = params.centerElevationDeg; return true;
    case kDistanceParamId: *value = params.centerDistance; return true;
    case kSpatialFollowParamId: *value = params.spatialFollow; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kPlaceParamId: *value = params.place; return true;
    case kSpaceParamId: *value = params.space; return true;
    case kEnvironmentSizeParamId: *value = params.environmentSize; return true;
    case kEnvironmentDecayParamId: *value = params.environmentDecay; return true;
    case kEnvironmentDampingParamId: *value = params.environmentDamping; return true;
    case kFieldListenModeParamId: *value = static_cast<uint32_t>(params.fieldListenMode); return true;
    case kFieldListenAmountParamId: *value = params.fieldListenAmount; return true;
    case kFieldListenResponseParamId:
        *value = static_cast<uint32_t>(params.fieldListenResponse);
        return true;
    case kSurfaceXParamId: *value = params.surfaceX; return true;
    case kSurfaceYParamId: *value = params.surfaceY; return true;
    case kFoamParamId: *value = params.foam; return true;
    case kShoreParamId: *value = params.shore; return true;
    case kSurfaceLoadParamId: *value = params.surfaceLoad; return true;
    case kSnapParamId: *value = params.snap; return true;
    case kPlateFailureParamId: *value = params.plateFailure; return true;
    case kScorePaceParamId: *value = params.scorePace; return true;
    case kScoreOccupancyParamId: *value = params.scoreOccupancy; return true;
    case kScoreCascadeParamId: *value = params.scoreCascade; return true;
    case kScoreMemoryParamId: *value = params.scoreMemory; return true;
    case kScoreRestParamId: *value = params.scoreRest; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", s3g::ambiCryosphereFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kRegimeParamId) {
        std::snprintf(display, size, "%s", kRegimeNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiCryosphereRegimeCount - 1u)]);
    } else if (id == kEnvironmentParamId) {
        std::snprintf(display, size, "%s", kEnvironmentNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiCryosphereEnvironmentCount - 1u)]);
    } else if (id == kPlaceParamId) {
        std::snprintf(display, size, "%s", kPlaceNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiCryospherePlaceCount - 1u)]);
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
    } else if (id == kFlowParamId) {
        if (value <= 0.0) {
            std::snprintf(display, size, "HOLD");
        } else if (value < 0.1) {
            std::snprintf(display, size, "%.3fx", value);
        } else {
            std::snprintf(display, size, "%.2fx", value);
        }
    } else if (id == kCryosphereParamId || id == kScaleParamId
        || id == kTurbulenceParamId || id == kAerationParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kDropsParamId || id == kSplashParamId || id == kBubblesParamId || id == kDensityParamId
        || id == kEventSizeParamId || id == kEventDecayParamId || id == kDepthParamId
        || id == kBrightnessParamId || id == kResonanceParamId || id == kDampingParamId || id == kContactParamId
        || id == kCurrentParamId || id == kSlopeParamId || id == kEddyParamId
        || id == kConvergenceParamId || id == kWidthParamId || id == kSpatialFollowParamId
        || id == kSpaceParamId || id == kEnvironmentSizeParamId
        || id == kEnvironmentDecayParamId || id == kEnvironmentDampingParamId
        || id == kFieldListenAmountParamId || id == kFoamParamId || id == kShoreParamId
        || id == kSurfaceLoadParamId || id == kSnapParamId
        || id == kPlateFailureParamId || id == kScorePaceParamId
        || id == kScoreOccupancyParamId || id == kScoreCascadeParamId
        || id == kScoreMemoryParamId || id == kScoreRestParamId) {
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
        for (uint32_t index = 0u; index < s3g::kAmbiCryosphereFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiCryosphereFactoryPresetInfo(index).name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kRegimeParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiCryosphereRegimeCount; ++index) {
            if (std::strcmp(display, kRegimeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kEnvironmentParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiCryosphereEnvironmentCount; ++index) {
            if (std::strcmp(display, kEnvironmentNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kPlaceParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiCryospherePlaceCount; ++index) {
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

    if (id == kFlowParamId
        && (std::strcmp(display, "HOLD") == 0
            || std::strcmp(display, "hold") == 0)) {
        *value = 0.0;
        return true;
    }
    *value = std::atof(display);
    if (id == kFlowParamId) {
        *value = std::clamp(*value, 0.0, 1.0);
        return true;
    }
    if (id == kCryosphereParamId || id == kScaleParamId
        || id == kTurbulenceParamId || id == kAerationParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kDropsParamId || id == kSplashParamId || id == kBubblesParamId || id == kDensityParamId
        || id == kEventSizeParamId || id == kEventDecayParamId || id == kDepthParamId
        || id == kBrightnessParamId || id == kResonanceParamId || id == kDampingParamId || id == kContactParamId
        || id == kCurrentParamId || id == kSlopeParamId || id == kEddyParamId
        || id == kConvergenceParamId || id == kWidthParamId || id == kSpatialFollowParamId
        || id == kSpaceParamId || id == kEnvironmentSizeParamId
        || id == kEnvironmentDecayParamId || id == kEnvironmentDampingParamId
        || id == kFieldListenAmountParamId || id == kFoamParamId || id == kShoreParamId
        || id == kSurfaceLoadParamId || id == kSnapParamId
        || id == kPlateFailureParamId || id == kScorePaceParamId
        || id == kScoreOccupancyParamId || id == kScoreCascadeParamId
        || id == kScoreMemoryParamId || id == kScoreRestParamId) {
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
    state.presetIndex = p->presetIndex;
    std::snprintf(state.customPresetName, sizeof(state.customPresetName), "%s", p->customPresetName);
    state.surface = p->surface;
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(state.version), sizeof(state) - sizeof(state.version))) return false;
        p->params = state.params;
        p->presetIndex = std::min<uint32_t>(state.presetIndex, s3g::kAmbiCryosphereFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", state.customPresetName);
        p->surface = state.surface;
    } else if (version == 5u) {
        s3g::AmbiCryosphereParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiCryosphereParams, foam);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        p->params = params;
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiCryosphereFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", customPresetName);
    } else if (version == 4u) {
        s3g::AmbiCryosphereParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize =
            offsetof(s3g::AmbiCryosphereParams, fieldListenAmount);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        p->params = params;
        p->presetIndex = std::min<uint32_t>(
            presetIndex, s3g::kAmbiCryosphereFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName,
            sizeof(p->customPresetName), "%s", customPresetName);
    } else if (version == 3u) {
        s3g::AmbiCryosphereParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiCryosphereParams, fieldListenMode);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        p->params = params;
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiCryosphereFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", customPresetName);
    } else if (version == 2u) {
        s3g::AmbiCryosphereParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiCryosphereParams, environmentSize);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        p->params = params;
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiCryosphereFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", customPresetName);
    } else if (version == 1u) {
        s3g::AmbiCryosphereParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiCryosphereParams, place);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        p->params = params;
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiCryosphereFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", customPresetName);
    } else {
        return false;
    }
    sanitizeCryosphereState(*p);
    p->engine.beginTransition();
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
    { kCryosphereParamId, 630, 248, 0.0, 1.0, false },
    { kFlowParamId, 630, 274, 0.0, 1.0, true },
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
    { kSurfaceLoadParamId, 630, 744, 0.0, 1.0, false },
    { kSnapParamId, 630, 770, 0.0, 1.0, false },
    { kPlateFailureParamId, 630, 796, 0.0, 1.0, false },
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
    { kScorePaceParamId, 18, 698, 0.0, 1.0, false },
    { kScoreOccupancyParamId, 18, 724, 0.0, 1.0, false },
    { kScoreCascadeParamId, 18, 750, 0.0, 1.0, false },
    { kScoreMemoryParamId, 310, 698, 0.0, 1.0, false },
    { kScoreRestParamId, 310, 724, 0.0, 1.0, false },
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

@interface S3GAmbiCryosphereEncoderView : NSView <NSWindowDelegate> {
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
    S3GAmbiCryosphereEncoderView* _surfacePopupOwner;
    NSPanel* _surfacePanel;
    S3GAmbiCryosphereEncoderView* _surfacePopupView;
    NSClipView* _surfacePopupClip;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)openSurfacePopup;
- (void)hideSurfacePopup;
- (void)destroySurfacePopup;
@end

@implementation S3GAmbiCryosphereEncoderView
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
        [_surfacePanel setTitle:@"s3g AMBI ENCODER CRYOSPHERE — SURF"];
        [_surfacePanel setReleasedWhenClosed:NO];
        [_surfacePanel setHidesOnDeactivate:YES];
        [_surfacePanel setDelegate:self];
        _surfacePopupView = [[S3GAmbiCryosphereEncoderView alloc]
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
    if (!_plugin || voice >= s3g::kAmbiCryosphereMaxVoices) return { 0.0f, 0.0f, 0.0f };
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
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Cryosphere Encoder"];
}

- (void)saveCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gcryo" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3gcryo", [self presetDisplayName]]];
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
    [panel setAllowedFileTypes:@[ @"s3gcryo" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return;
    _plugin->params = file.params;
    applyEffectiveParams(*_plugin);
    _plugin->engine.beginTransition();
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
    labels[1] = _plugin->surface.enabled ? @"ON" : @"OFF";
    for (int index = 0; index < 6; ++index) {
        const BOOL active = (index == 0 && _surfaceEdit)
            || (index == 1 && _plugin->surface.enabled)
            || (index == 5 && (_surfacePopupChild || [_surfacePanel isVisible]));
        s3g::clap_gui::drawHeaderButton([self surfaceButtonRect:index],
            header, labels[index], active, attrs, style);
    }
    const NSRect curve = [self surfaceCurveRect];
    [style.strip setFill]; NSRectFill(curve);
    [style.grid setStroke]; NSFrameRect(curve);
    [[NSString stringWithFormat:@"CURVE  %s",
        s3g::parameterSurfaceCurveName(_plugin->surface.curve)]
        drawAtPoint:NSMakePoint(curve.origin.x + 5.0, curve.origin.y + 2.0)
        withAttributes:valueAttrs];
    [@"FOCUS" drawAtPoint:NSMakePoint(field.origin.x + 122.0,
        field.origin.y + 40.0) withAttributes:attrs];
    s3g::clap_gui::drawHeaderButton([self surfaceFocusRect:0], header,
        @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self surfaceFocusRect:1], header,
        @"+", false, attrs, style);
    [[NSString stringWithFormat:@"%.2f", _plugin->surface.focus]
        drawAtPoint:NSMakePoint(field.origin.x + 222.0, field.origin.y + 40.0)
        withAttributes:valueAttrs];
    [@"GLIDE" drawAtPoint:NSMakePoint(field.origin.x + 280.0,
        field.origin.y + 40.0) withAttributes:attrs];
    s3g::clap_gui::drawHeaderButton([self surfaceGlideRect:0], header,
        @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self surfaceGlideRect:1], header,
        @"+", false, attrs, style);
    NSString* glide = _plugin->surface.glideMs < 0.5f ? @"OFF"
        : [NSString stringWithFormat:@"%.0f MS", _plugin->surface.glideMs];
    [glide drawAtPoint:NSMakePoint(field.origin.x + 388.0,
        field.origin.y + 40.0) withAttributes:valueAttrs];
    const NSRect plot = [self surfacePlotRect];
    const bool audioActive = _plugin->active.load(
        std::memory_order_acquire);
    const float effectiveX = audioActive
        ? _plugin->effectiveSurfaceX.load(std::memory_order_relaxed)
        : _plugin->params.surfaceX;
    const float effectiveY = audioActive
        ? _plugin->effectiveSurfaceY.load(std::memory_order_relaxed)
        : _plugin->params.surfaceY;
    s3g::clap_gui::drawParameterSurfaceVoronoi(_plugin->surface, plot,
        effectiveX, effectiveY,
        _selectedSurfaceCell, valueAttrs,
        _plugin->params.surfaceX, _plugin->params.surfaceY);
    [[NSString stringWithFormat:@"T %.3f %.3f   /   A %.3f %.3f   /   %u CELLS   /   %@",
        _plugin->params.surfaceX, _plugin->params.surfaceY,
        effectiveX, effectiveY,
        _plugin->surface.cellCount,
        _plugin->surface.cellCount < 2u ? @"ADD TWO CELLS TO ENABLE" :
            (_plugin->surface.enabled ? @"INTERPOLATING" : @"BYPASSED")]
        drawAtPoint:NSMakePoint(plot.origin.x + 8.0, NSMaxY(plot) - 18.0)
        withAttributes:valueAttrs];
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"CRYOSPHERE GEO FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
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
        _surfaceEdit ? _plugin->params.voices
            : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiCryosphereMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<NSPoint, s3g::kAmbiCryosphereMaxVoices> projected {};
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
    [@"FROST / FRACTURE FIELD     STRESS + SLIP + IMPACT     ACN/SN3D" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19) withAttributes:valueAttrs];
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
    if (_plugin->customPresetName[0]) return [NSString stringWithFormat:@"CUSTOM: %s", _plugin->customPresetName];
    return [NSString stringWithUTF8String:s3g::ambiCryosphereFactoryPresetInfo(_plugin->presetIndex).name];
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->effectiveParams;
    s3g::clap_gui::drawPanelFrame(18, 662, 596, 152, style);
    s3g::clap_gui::drawPanelHeader(@"ALEATORIC ENTITY SCORE", true,
        18, 662, 596, 21, attrs, style);
    [self drawSlider:@"PACE" param:kScorePaceParamId value:p.scorePace
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"OCCUPANCY" param:kScoreOccupancyParamId
        value:p.scoreOccupancy attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CASCADE" param:kScoreCascadeParamId
        value:p.scoreCascade attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"MEMORY" param:kScoreMemoryParamId value:p.scoreMemory
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"REST" param:kScoreRestParamId value:p.scoreRest
        attrs:attrs valueAttrs:valueAttrs style:style];
    NSString* scoreStatus = [NSString stringWithFormat:@"%u ENT / %llu ARCS / %.2f ACT",
        _plugin->guiScoreEntities.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(_plugin->guiScoreArcs.load(
            std::memory_order_relaxed)),
        _plugin->guiScoreActivity.load(std::memory_order_relaxed)];
    s3g::clap_gui::drawRightStatus(scoreStatus, 614, 669,
        valueAttrs, 8.0);

    s3g::clap_gui::drawPanelFrame(630, 42, 250, 80, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 630, 42, 250, 21, attrs, style);
    [self drawSlider:@"OUT" param:kOutputParamId value:p.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] panelX:630 y:104 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 134, 250, 210, style);
    s3g::clap_gui::drawPanelHeader(@"FROST / ICE GEOLOGY", true, 630, 134, 250, 21, attrs, style);
    [self drawMenu:@"PROCESS" value:[NSString stringWithUTF8String:kRegimeNames[
        std::min<uint32_t>(p.regime, s3g::kAmbiCryosphereRegimeCount - 1u)]] panelX:630 y:170 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ENVIRONMENT" value:[NSString stringWithUTF8String:kEnvironmentNames[
        std::min<uint32_t>(p.environment, s3g::kAmbiCryosphereEnvironmentCount - 1u)]] panelX:630 y:196 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:p.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ICE GROWTH" param:kCryosphereParamId value:p.water attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"EVENT RATE" param:kFlowParamId value:p.flow attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SLAB SCALE" param:kScaleParamId value:p.scale attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BRANCHING" param:kTurbulenceParamId value:p.turbulence attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 356, 250, 210, style);
    s3g::clap_gui::drawPanelHeader(@"EVENTS", true, 630, 356, 250, 21, attrs, style);
    [self drawSlider:@"GRANULES" param:kAerationParamId value:p.aeration attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FROZEN IMPACTS" param:kDropsParamId value:p.drops attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CALVING" param:kSplashParamId value:p.splash attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BRINE POCKETS" param:kBubblesParamId value:p.bubbles attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FRACTURE DENS" param:kDensityParamId value:p.density attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FRACTURE MASS" param:kEventSizeParamId value:p.eventSize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FRACTURE TAIL" param:kEventDecayParamId value:p.eventDecay attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 578, 250, 236, style);
    s3g::clap_gui::drawPanelHeader(@"MATERIAL + CONSEQUENCE", true, 630, 578, 250, 21, attrs, style);
    [self drawSlider:@"DEPTH" param:kDepthParamId value:p.depth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BRIGHTNESS" param:kBrightnessParamId value:p.brightness attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BODY SCATTER" param:kResonanceParamId value:p.resonance attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DAMPING" param:kDampingParamId value:p.damping attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BRITTLENESS" param:kContactParamId value:p.contact attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SURFACE LOAD" param:kSurfaceLoadParamId
        value:p.surfaceLoad attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ICE SNAP" param:kSnapParamId value:p.snap
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"PLATE FAILURE" param:kPlateFailureParamId
        value:p.plateFailure attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 262, style);
    s3g::clap_gui::drawPanelHeader(@"MASS TRANSPORT", true, 896, 42, 246, 21, attrs, style);
    [self drawSlider:@"DRIFT RATE" param:kMotionRateParamId value:p.motionRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DRIFT" param:kCurrentParamId value:p.current attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SLOPE" param:kSlopeParamId value:p.slope attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TORQUE" param:kEddyParamId value:p.eddy attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"COMPRESSION" param:kConvergenceParamId value:p.convergence attrs:attrs valueAttrs:valueAttrs style:style];
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
        std::min<uint32_t>(p.place, s3g::kAmbiCryospherePlaceCount - 1u)]] panelX:896 y:470 attrs:attrs valueAttrs:valueAttrs style:style];
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
    [self drawSlider:@"SNOW" param:kFoamParamId value:p.foam attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GRINDING" param:kShoreParamId value:p.shore attrs:attrs valueAttrs:valueAttrs style:style];
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
    case 1: return s3g::kAmbiCryosphereFactoryPresetCount;
    case 2: return s3g::kAmbiCryosphereRegimeCount;
    case 3: return s3g::kAmbiCryosphereEnvironmentCount;
    case 4: return s3g::kAmbiCryospherePlaceCount;
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
    static NSString* regimeItems[] = { @"FROST CRACK", @"ICE SEGREGATION",
        @"PERMAFROST HEAVE", @"BASAL STICK-SLIP", @"PRESSURE RIDGE",
        @"CALVING", @"ICEBERG IMPACT", @"AVALANCHE", @"SNOWPACK CREEP",
        @"HAIL", @"SLEET", @"FREEZING RAIN", @"MELTWATER UNDER ICE",
        @"SINGING LAKE" };
    static_assert(std::size(regimeItems) == s3g::kAmbiCryosphereRegimeCount);
    static NSString* environmentItems[] = { @"OPEN ICE", @"ROCK", @"SNOWPACK",
        @"MORAINE", @"CONCRETE", @"METAL", @"GLASS", @"ICE TUNNEL",
        @"ICE CAVE", @"GLACIER" };
    static NSString* placeItems[] = { @"OPEN", @"SUBMERGED", @"CAVE", @"CISTERN", @"CHANNEL", @"PIPE" };
    static NSString* listenItems[] = { @"OFF", @"FOLLOW", @"COUNTER", @"BALANCE" };
    static NSString* responseItems[] = {
        @"DIRECT", @"ACCRETE", @"SETTLE", @"IMPRINT"
    };
    static NSString* presetItems[s3g::kAmbiCryosphereFactoryPresetCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t i = 0; i < s3g::kAmbiCryosphereFactoryPresetCount; ++i) presetItems[i] = [[NSString stringWithUTF8String:s3g::ambiCryosphereFactoryPresetInfo(i).name] retain];
    });
    NSString** items = presetItems;
    int selected = static_cast<int>(_plugin->presetIndex);
    if (_openMenu == 2) {
        items = regimeItems;
        selected = static_cast<int>(_plugin->params.regime);
    } else if (_openMenu == 3) {
        items = environmentItems;
        selected = static_cast<int>(_plugin->params.environment);
    } else if (_openMenu == 4) {
        items = placeItems;
        selected = static_cast<int>(_plugin->params.place);
    } else if (_openMenu == 5) {
        items = listenItems;
        selected = static_cast<int>(_plugin->params.fieldListenMode);
    } else if (_openMenu == 6) {
        items = responseItems;
        selected = static_cast<int>(_plugin->params.fieldListenResponse);
    } else if (_openMenu == 10) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
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
    [@"s3g AMBI ENCODER CRYOSPHERE" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
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
        _surfaceEdit ? _plugin->params.voices
            : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiCryosphereMaxVoices);
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
    for (uint32_t index = 0u; index < _plugin->surface.cellCount; ++index) {
        const auto& cell = _plugin->surface.cells[index];
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
        applyParam(*_plugin, kSurfaceXParamId, x);
        applyParam(*_plugin, kSurfaceYParamId, y);
    } else if (_dragSurfaceCell >= 0
        && static_cast<uint32_t>(_dragSurfaceCell) < _plugin->surface.cellCount) {
        auto& cell = _plugin->surface.cells[static_cast<uint32_t>(_dragSurfaceCell)];
        cell.x = x;
        cell.y = y;
        applyEffectiveParams(*_plugin);
    }
    requestSurfaceProcess(*_plugin);
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)point
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    applyParam(*_plugin, param, sliderValue(*spec, point));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 2) applyParam(*_plugin, kRegimeParamId, hit);
            else if (_openMenu == 3) applyParam(*_plugin, kEnvironmentParamId, hit);
            else if (_openMenu == 4) applyParam(*_plugin, kPlaceParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kFieldListenModeParamId, hit);
            else if (_openMenu == 6) applyParam(*_plugin, kFieldListenResponseParamId, hit);
            else if (_openMenu == 10) applyParam(*_plugin, kOrderParamId, hit + 1);
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
                if (_plugin->surface.cellCount < 2u) NSBeep();
                else {
                    _plugin->surface.enabled = !_plugin->surface.enabled;
                    applyEffectiveParams(*_plugin);
                    requestSurfaceProcess(*_plugin);
                }
            } else if (NSPointInRect(point, [self surfaceButtonRect:2])) {
                NSString* name = [self presetDisplayName];
                if (s3g::addParameterSurfaceCell(_plugin->surface,
                        _plugin->params, static_cast<int32_t>(_plugin->presetIndex),
                        [name UTF8String])) {
                    _selectedSurfaceCell = static_cast<int>(
                        _plugin->surface.cellCount) - 1;
                } else NSBeep();
            } else if (NSPointInRect(point, [self surfaceButtonRect:3])) {
                if (_selectedSurfaceCell < 0
                    || !s3g::removeParameterSurfaceCell(_plugin->surface,
                        static_cast<uint32_t>(_selectedSurfaceCell))) NSBeep();
                _selectedSurfaceCell = _plugin->surface.cellCount == 0u ? -1
                    : std::min(_selectedSurfaceCell,
                        static_cast<int>(_plugin->surface.cellCount) - 1);
                applyEffectiveParams(*_plugin);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceButtonRect:4])) {
                if (_selectedSurfaceCell < 0
                    || static_cast<uint32_t>(_selectedSurfaceCell)
                        >= _plugin->surface.cellCount) NSBeep();
                else {
                    auto& cell = _plugin->surface.cells[
                        static_cast<uint32_t>(_selectedSurfaceCell)];
                    cell.params = _plugin->params;
                    cell.presetIndex = static_cast<int32_t>(_plugin->presetIndex);
                    std::snprintf(cell.name, sizeof(cell.name), "%s",
                        [[self presetDisplayName] UTF8String]);
                    applyEffectiveParams(*_plugin);
                    requestSurfaceProcess(*_plugin);
                }
            } else if (NSPointInRect(point, [self surfaceCurveRect])) {
                const uint32_t next = (static_cast<uint32_t>(
                    _plugin->surface.curve) + 1u)
                    % s3g::kParameterSurfaceCurveCount;
                _plugin->surface.curve = static_cast<s3g::ParameterSurfaceCurve>(next);
                applyEffectiveParams(*_plugin);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceFocusRect:0])
                || NSPointInRect(point, [self surfaceFocusRect:1])) {
                const float scale = NSPointInRect(point,
                    [self surfaceFocusRect:0]) ? 0.8f : 1.25f;
                _plugin->surface.focus = std::clamp(
                    _plugin->surface.focus * scale, 0.25f, 8.0f);
                applyEffectiveParams(*_plugin);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceGlideRect:0])
                || NSPointInRect(point, [self surfaceGlideRect:1])) {
                const int direction = NSPointInRect(point,
                    [self surfaceGlideRect:0]) ? -1 : 1;
                _plugin->surface.glideMs = s3g::parameterSurfaceSteppedGlide(
                    _plugin->surface.glideMs, direction);
                applyEffectiveParams(*_plugin);
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
                applyParam(*_plugin, spec.id, defaultValue);
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
    p->guiView = [[S3GAmbiCryosphereEncoderView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiCryosphereEncoderView*>(p->guiView) destroySurfacePopup];
    [static_cast<S3GAmbiCryosphereEncoderView*>(p->guiView) stopRefreshTimer];
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
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GAmbiCryosphereEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiCryosphereEncoderView*>(p->guiView) hideSurfacePopup]; [static_cast<S3GAmbiCryosphereEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    "org.s3g.s3g-dsp.ambi-cryosphere-encoder-64",
    "s3g Ambi Encoder Cryosphere",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.3.0",
    "Procedural 7OA ice instrument modeling paced strain, structural plates, branching fractures, grinding, snow, hail, sleet, and calving.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiCryosphereFactoryPreset(0u);
    p->engine.prepare(p->sampleRate);
    sanitizeCryosphereState(*p);
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
