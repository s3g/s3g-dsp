#include "s3g_ambi_wind_encoder.h"
#include "s3g_ambi_wind_presets.h"
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
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiWindMaxChannels;
constexpr uint32_t kStateVersion = 15;
constexpr uint32_t kCustomPresetMagic = 0x31445741u; // AWD1
constexpr uint32_t kCustomPresetVersion = 12;

constexpr clap_id kPresetParamId = 1;
constexpr clap_id kOrderParamId = 2;
constexpr clap_id kVoicesParamId = 3;
constexpr clap_id kRateAParamId = 4;
constexpr clap_id kRateBParamId = 5;
constexpr clap_id kFmAtoBParamId = 6;
constexpr clap_id kFmBtoAParamId = 7;
constexpr clap_id kFlutterParamId = 8;
constexpr clap_id kMaterialParamId = 9;
constexpr clap_id kSpreadParamId = 10;
constexpr clap_id kDeviationParamId = 11;
constexpr clap_id kGustShapeParamId = 12;
constexpr clap_id kRateModeAParamId = 13;
constexpr clap_id kThresholdParamId = 14;
constexpr clap_id kColorParamId = 15;
constexpr clap_id kFilterParamId = 16;
constexpr clap_id kResonanceParamId = 17;
constexpr clap_id kFilterRunParamId = 18;
constexpr clap_id kFilterSweepParamId = 19;
constexpr clap_id kSaturationParamId = 20;
constexpr clap_id kMotionRateParamId = 23;
constexpr clap_id kMotionFlowParamId = 24;
constexpr clap_id kMotionShearParamId = 25;
constexpr clap_id kMotionCurlParamId = 26;
constexpr clap_id kMotionUpdraftParamId = 27;
constexpr clap_id kAzimuthParamId = 28;
constexpr clap_id kElevationParamId = 29;
constexpr clap_id kDistanceParamId = 30;
constexpr clap_id kSpatialFollowParamId = 31;
constexpr clap_id kOutputParamId = 32;
constexpr clap_id kFieldParamId = 33;
constexpr clap_id kPwmAParamId = 37;
constexpr clap_id kPwmBParamId = 38;
constexpr clap_id kRateModeBParamId = 43;
constexpr clap_id kGustEdgeParamId = 46;
constexpr clap_id kPlaceParamId = 47;
constexpr clap_id kSpaceParamId = 48;
constexpr clap_id kEnvironmentSizeParamId = 49;
constexpr clap_id kEnvironmentDecayParamId = 50;
constexpr clap_id kEnvironmentDampingParamId = 51;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiWindParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiWindParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiWindEncoder engine {};
    s3g::AmbiWindParams params {};
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
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiGust {};
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
        && (file.version == 10u || file.version == 11u || file.version == kCustomPresetVersion)
        && std::fread(file.name, 1, sizeof(file.name), handle) == sizeof(file.name);
    if (ok) {
        const size_t paramsSize = file.version == 10u
            ? offsetof(s3g::AmbiWindParams, place)
            : (file.version == 11u ? offsetof(s3g::AmbiWindParams, environmentSize) : sizeof(file.params));
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

constexpr const char* kMaterialNames[] = {
    "OPEN", "LEAF", "HOLLOW", "WIRE", "METAL", "CHIMES", "BLOCKS", "HARP", "REEDS", "FABRIC"
};

constexpr const char* kPlaceNames[] = {
    "OPEN", "CANOPY", "PORCH", "ROOM", "HANGAR", "CANYON", "TUNNEL"
};

void randomizeSafe(Plugin& plugin)
{
    auto p = plugin.params;
    uint32_t seed = plugin.randomSeed ^ static_cast<uint32_t>(std::lround(plugin.outputPeak.load(std::memory_order_relaxed) * 1000000.0f));
    p.order = 3u;
    p.voices = 10u + randomChoice(seed, 23u);
    p.wind = randomRange(seed, 0.08f, 0.46f);
    p.gustRate = randomRange(seed, 0.025f, 0.52f);
    if (randomUnit(seed) < 0.32f) {
        p.wind *= 0.38f;
        p.gustRate *= 0.44f;
    }
    p.gustDepth = randomRange(seed, 0.04f, 0.48f);
    p.turbulence = randomRange(seed, 0.02f, 0.42f);
    p.flutter = randomRange(seed, 0.03f, 0.58f);
    p.material = randomRange(seed, 0.03f, 0.56f);
    p.air = randomRange(seed, 0.18f, 0.54f);
    p.hiss = randomRange(seed, 0.14f, 0.50f);
    p.sweep = randomRange(seed, 0.24f, 0.76f);
    p.body = randomRange(seed, 0.24f, 0.76f);
    p.spread = randomRange(seed, 0.06f, 0.42f);
    p.deviation = randomRange(seed, 0.025f, 0.22f);
    p.gustShape = randomChoice(seed, 6u);
    p.vectorRateHz = randomRange(seed, 0.004f, 0.080f);
    p.materialMode = randomChoice(seed, 10u);
    const bool resonantObject = p.materialMode == 5u || p.materialMode == 6u || p.materialMode == 7u;
    if (resonantObject) {
        p.material = randomRange(seed, 0.18f, 0.66f);
    }
    p.gustEdge = randomUnit(seed) < 0.72f ? 0u : (randomUnit(seed) < 0.62f ? 1u : 2u);
    p.center = randomRange(seed, 0.20f, 0.82f);
    p.sweep = randomRange(seed, 0.16f, 0.78f);
    p.q = resonantObject ? randomRange(seed, 0.22f, 0.62f) : randomRange(seed, 0.14f, 0.54f);
    p.shrill = randomRange(seed, 0.06f, 0.42f);
    p.body = randomRange(seed, 0.12f, 0.56f);
    p.breath = randomRange(seed, 0.08f, 0.54f);
    p.grit = randomRange(seed, 0.02f, 0.26f);
    p.field = randomRange(seed, 0.18f, 0.86f);
    p.motionRateHz = randomRange(seed, 0.008f, 0.220f);
    p.motionFlow = randomRange(seed, 0.12f, 0.72f);
    p.motionShear = randomRange(seed, 0.10f, 0.72f);
    p.motionCurl = randomUnit(seed) < 0.18f ? randomRange(seed, 0.78f, 1.0f) : randomRange(seed, 0.18f, 0.74f);
    p.motionUpdraft = randomUnit(seed) < 0.18f ? randomRange(seed, 0.55f, 1.0f) : randomRange(seed, 0.02f, 0.42f);
    p.centerAzimuthDeg = randomRange(seed, -35.0f, 35.0f);
    p.centerElevationDeg = randomRange(seed, -18.0f, 24.0f);
    p.centerDistance = randomRange(seed, 0.88f, 1.30f);
    p.spatialFollow = randomRange(seed, 0.12f, 0.70f);
    p.place = randomChoice(seed, s3g::kAmbiWindPlaceCount);
    p.space = randomRange(seed, 0.06f, p.place == 0u ? 0.24f : 0.50f);
    p.environmentSize = randomRange(seed, 0.30f, 0.76f);
    p.environmentDecay = randomRange(seed, 0.32f, 0.76f);
    p.environmentDamping = randomRange(seed, 0.24f, 0.74f);
    p.outputGainDb = resonantObject ? -8.0f : -6.0f;

    plugin.randomSeed = seed;
    plugin.params = p;
    plugin.presetIndex = 0u;
    std::snprintf(plugin.customPresetName, sizeof(plugin.customPresetName), "Random");
    plugin.engine.setParams(plugin.params);
    plugin.engine.beginTransition();
    plugin.params = plugin.engine.params();
}

bool assignParam(s3g::AmbiWindParams& params, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: params.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kVoicesParamId: params.voices = static_cast<uint32_t>(std::lround(value)); return true;
    case kRateAParamId: params.wind = static_cast<float>(value); return true;
    case kRateBParamId: params.gustRate = static_cast<float>(value); return true;
    case kFmAtoBParamId: params.gustDepth = static_cast<float>(value); return true;
    case kFmBtoAParamId: params.turbulence = static_cast<float>(value); return true;
    case kFlutterParamId: params.flutter = static_cast<float>(value); return true;
    case kMaterialParamId: params.material = static_cast<float>(value); return true;
    case kPwmAParamId: params.air = static_cast<float>(value); return true;
    case kPwmBParamId: params.hiss = static_cast<float>(value); return true;
    case kSpreadParamId: params.spread = static_cast<float>(value); return true;
    case kDeviationParamId: params.deviation = static_cast<float>(value); return true;
    case kGustShapeParamId: params.gustShape = static_cast<uint32_t>(std::lround(value)); return true;
    case kRateModeAParamId: params.vectorRateHz = static_cast<float>(value); return true;
    case kRateModeBParamId: params.materialMode = static_cast<uint32_t>(std::lround(value)); return true;
    case kGustEdgeParamId: params.gustEdge = static_cast<uint32_t>(std::lround(value)); return true;
    case kThresholdParamId: params.center = static_cast<float>(value); return true;
    case kColorParamId: params.sweep = static_cast<float>(value); return true;
    case kFilterParamId: params.q = static_cast<float>(value); return true;
    case kResonanceParamId: params.shrill = static_cast<float>(value); return true;
    case kFilterRunParamId: params.body = static_cast<float>(value); return true;
    case kFilterSweepParamId: params.breath = static_cast<float>(value); return true;
    case kSaturationParamId: params.grit = static_cast<float>(value); return true;
    case kFieldParamId: params.field = static_cast<float>(value); return true;
    case kMotionRateParamId: params.motionRateHz = static_cast<float>(value); return true;
    case kMotionFlowParamId: params.motionFlow = static_cast<float>(value); return true;
    case kMotionShearParamId: params.motionShear = static_cast<float>(value); return true;
    case kMotionCurlParamId: params.motionCurl = static_cast<float>(value); return true;
    case kMotionUpdraftParamId: params.motionUpdraft = static_cast<float>(value); return true;
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
    default: return false;
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kPresetParamId) {
        p.presetIndex = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, s3g::kAmbiWindFactoryPresetCount - 1u);
        p.customPresetName[0] = '\0';
        p.params = s3g::ambiWindFactoryPreset(p.presetIndex);
        p.engine.setParams(p.params);
        p.engine.beginTransition();
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
    const uint32_t voices = std::min<uint32_t>(p->params.voices, s3g::kAmbiWindMaxVoices);
    for (uint32_t voice = 0u; voice < voices; ++voice) {
        const auto point = p->engine.voicePoint(voice);
        p->guiAzimuth[voice].store(point.azimuthDeg, std::memory_order_relaxed);
        p->guiElevation[voice].store(point.elevationDeg, std::memory_order_relaxed);
        p->guiDistance[voice].store(point.distance, std::memory_order_relaxed);
        p->guiEnergy[voice].store(p->engine.voiceEnergy(voice), std::memory_order_relaxed);
        p->guiGust[voice].store(p->engine.voiceGustLevel(voice), std::memory_order_relaxed);
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
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiWindFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 16.0, true },
    { kRateAParamId, "Wind", 0.0, 1.0, 0.55, false },
    { kRateBParamId, "Gust Rate", 0.0, 1.0, 0.20, false },
    { kFmAtoBParamId, "Gust Depth", 0.0, 1.0, 0.48, false },
    { kFmBtoAParamId, "Turbulence", 0.0, 1.0, 0.36, false },
    { kFlutterParamId, "Flutter", 0.0, 1.0, 0.28, false },
    { kMaterialParamId, "Material", 0.0, 1.0, 0.34, false },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.26, false },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.12, false },
    { kGustShapeParamId, "Gust Shape", 0.0, 5.0, 2.0, true },
    { kRateModeAParamId, "Vector LFO", 0.0, 0.5, 0.024, false },
    { kRateModeBParamId, "Material Type", 0.0, 9.0, 0.0, true },
    { kGustEdgeParamId, "Gust Edge", 0.0, 2.0, 0.0, true },
    { kThresholdParamId, "Center", 0.0, 1.0, 0.38, false },
    { kColorParamId, "Sweep", 0.0, 1.0, 0.48, false },
    { kFilterParamId, "Q", 0.0, 1.0, 0.42, false },
    { kResonanceParamId, "Shrill", 0.0, 1.0, 0.24, false },
    { kFilterRunParamId, "Body", 0.0, 1.0, 0.52, false },
    { kFilterSweepParamId, "Breath", 0.0, 1.0, 0.36, false },
    { kSaturationParamId, "Grit", 0.0, 1.0, 0.18, false },
    { kMotionRateParamId, "Field Rate", 0.001, 2.0, 0.024, false },
    { kMotionFlowParamId, "Flow Push", 0.0, 1.0, 0.24, false },
    { kMotionShearParamId, "Shear", 0.0, 1.0, 0.22, false },
    { kMotionCurlParamId, "Curl", 0.0, 1.0, 0.32, false },
    { kMotionUpdraftParamId, "Updraft", 0.0, 1.0, 0.05, false },
    { kAzimuthParamId, "Direction", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Range", 0.15, 2.0, 1.0, false },
    { kSpatialFollowParamId, "Inertia", 0.0, 1.0, 0.90, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kFieldParamId, "Depth Push", 0.0, 1.0, 0.30, false },
    { kPwmAParamId, "Air", 0.0, 1.0, 0.42, false },
    { kPwmBParamId, "Hiss", 0.0, 1.0, 0.34, false },
    { kPlaceParamId, "Place", 0.0, static_cast<double>(s3g::kAmbiWindPlaceCount - 1u), 0.0, true },
    { kSpaceParamId, "Env Return", 0.0, 1.0, 0.14, false },
    { kEnvironmentSizeParamId, "Env Size", 0.0, 1.0, 0.5, false },
    { kEnvironmentDecayParamId, "Env Decay", 0.0, 1.0, 0.5, false },
    { kEnvironmentDampingParamId, "Env Damping", 0.0, 1.0, 0.5, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

const char* paramModule(clap_id id)
{
    switch (id) {
    case kPresetParamId: return "Global";
    case kOrderParamId:
    case kRateModeAParamId:
    case kRateModeBParamId:
    case kVoicesParamId:
    case kRateAParamId:
    case kRateBParamId:
    case kSpreadParamId:
    case kDeviationParamId: return "Source and Gust";
    case kGustEdgeParamId:
    case kFmAtoBParamId:
    case kFmBtoAParamId:
    case kFlutterParamId:
    case kMaterialParamId:
    case kGustShapeParamId:
    case kThresholdParamId: return "Gust and Material";
    case kColorParamId:
    case kFilterParamId:
    case kResonanceParamId:
    case kFilterRunParamId:
    case kFilterSweepParamId:
    case kSaturationParamId: return "Filter and Tone";
    case kPwmAParamId:
    case kPwmBParamId:
    case kOutputParamId: return "Air and Output";
    case kMotionRateParamId:
    case kMotionFlowParamId:
    case kMotionShearParamId:
    case kMotionCurlParamId:
    case kMotionUpdraftParamId:
    case kFieldParamId:
    case kSpatialFollowParamId: return "Microweather Motion";
    case kAzimuthParamId:
    case kElevationParamId:
    case kDistanceParamId: return "Macro Wind Vector";
    case kPlaceParamId:
    case kSpaceParamId:
    case kEnvironmentSizeParamId:
    case kEnvironmentDecayParamId:
    case kEnvironmentDampingParamId: return "Environment Field";
    default: return "Ambi Wind Encoder";
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
    case kRateAParamId: *value = params.wind; return true;
    case kRateBParamId: *value = params.gustRate; return true;
    case kFmAtoBParamId: *value = params.gustDepth; return true;
    case kFmBtoAParamId: *value = params.turbulence; return true;
    case kFlutterParamId: *value = params.flutter; return true;
    case kMaterialParamId: *value = params.material; return true;
    case kPwmAParamId: *value = params.air; return true;
    case kPwmBParamId: *value = params.hiss; return true;
    case kSpreadParamId: *value = params.spread; return true;
    case kDeviationParamId: *value = params.deviation; return true;
    case kGustShapeParamId: *value = params.gustShape; return true;
    case kRateModeAParamId: *value = params.vectorRateHz; return true;
    case kRateModeBParamId: *value = params.materialMode; return true;
    case kGustEdgeParamId: *value = params.gustEdge; return true;
    case kThresholdParamId: *value = params.center; return true;
    case kColorParamId: *value = params.sweep; return true;
    case kFilterParamId: *value = params.q; return true;
    case kResonanceParamId: *value = params.shrill; return true;
    case kFilterRunParamId: *value = params.body; return true;
    case kFilterSweepParamId: *value = params.breath; return true;
    case kSaturationParamId: *value = params.grit; return true;
    case kMotionRateParamId: *value = params.motionRateHz; return true;
    case kMotionFlowParamId: *value = params.motionFlow; return true;
    case kMotionShearParamId: *value = params.motionShear; return true;
    case kMotionCurlParamId: *value = params.motionCurl; return true;
    case kMotionUpdraftParamId: *value = params.motionUpdraft; return true;
    case kAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kElevationParamId: *value = params.centerElevationDeg; return true;
    case kDistanceParamId: *value = params.centerDistance; return true;
    case kSpatialFollowParamId: *value = params.spatialFollow; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kFieldParamId: *value = params.field; return true;
    case kPlaceParamId: *value = params.place; return true;
    case kSpaceParamId: *value = params.space; return true;
    case kEnvironmentSizeParamId: *value = params.environmentSize; return true;
    case kEnvironmentDecayParamId: *value = params.environmentDecay; return true;
    case kEnvironmentDampingParamId: *value = params.environmentDamping; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", s3g::ambiWindFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kRateModeAParamId) {
        std::snprintf(display, size, "%.3f Hz", value);
    } else if (id == kRateModeBParamId) {
        std::snprintf(display, size, "%s", kMaterialNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 9u)]);
    } else if (id == kPlaceParamId) {
        std::snprintf(display, size, "%s", kPlaceNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiWindPlaceCount - 1u)]);
    } else if (id == kGustShapeParamId) {
        static constexpr const char* kGustShapeNames[] = { "SINE", "SWELL", "SURGE", "TRI", "BLAST", "GATE" };
        std::snprintf(display, size, "%s", kGustShapeNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 5u)]);
    } else if (id == kGustEdgeParamId) {
        static constexpr const char* kEdgeNames[] = { "SOFT", "BEND", "HARD" };
        std::snprintf(display, size, "%s", kEdgeNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 2u)]);
    } else if (id == kMotionRateParamId) {
        std::snprintf(display, size, "%.3f Hz", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kRateAParamId || id == kRateBParamId) {
        std::snprintf(display, size, value < 0.01 ? "%.3f%%" : "%.2f%%", value * 100.0);
    } else if (id == kFmAtoBParamId || id == kFmBtoAParamId
        || id == kFlutterParamId || id == kMaterialParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kPwmAParamId || id == kPwmBParamId
        || id == kThresholdParamId || id == kColorParamId || id == kFilterParamId || id == kResonanceParamId
        || id == kFilterRunParamId || id == kFilterSweepParamId || id == kSaturationParamId
        || id == kFieldParamId
        || id == kMotionFlowParamId || id == kMotionShearParamId || id == kMotionUpdraftParamId
        || id == kSpatialFollowParamId || id == kSpaceParamId
        || id == kEnvironmentSizeParamId || id == kEnvironmentDecayParamId
        || id == kEnvironmentDampingParamId) {
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
        for (uint32_t index = 0u; index < s3g::kAmbiWindFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiWindFactoryPresetInfo(index).name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kGustEdgeParamId) {
        static constexpr const char* edgeNames[] = { "SOFT", "BEND", "HARD" };
        for (uint32_t index = 0u; index < std::size(edgeNames); ++index) {
            if (std::strcmp(display, edgeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kGustShapeParamId) {
        static constexpr const char* shapeNames[] = { "SINE", "SWELL", "SURGE", "TRI", "BLAST", "GATE" };
        for (uint32_t index = 0u; index < std::size(shapeNames); ++index) {
            if (std::strcmp(display, shapeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kRateModeBParamId) {
        for (uint32_t index = 0u; index < 10u; ++index) {
            if (std::strcmp(display, kMaterialNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kPlaceParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWindPlaceCount; ++index) {
            if (std::strcmp(display, kPlaceNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }

    *value = std::atof(display);
    if (id == kRateAParamId || id == kRateBParamId || id == kFmAtoBParamId || id == kFmBtoAParamId
        || id == kFlutterParamId || id == kMaterialParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kPwmAParamId || id == kPwmBParamId
        || id == kThresholdParamId || id == kColorParamId || id == kFilterParamId || id == kResonanceParamId
        || id == kFilterRunParamId || id == kFilterSweepParamId || id == kSaturationParamId
        || id == kFieldParamId
        || id == kMotionFlowParamId || id == kMotionShearParamId || id == kMotionUpdraftParamId
        || id == kSpatialFollowParamId || id == kSpaceParamId
        || id == kEnvironmentSizeParamId || id == kEnvironmentDecayParamId
        || id == kEnvironmentDampingParamId) {
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
        p->presetIndex = std::min<uint32_t>(state.presetIndex, s3g::kAmbiWindFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", state.customPresetName);
    } else if (version == 14u) {
        s3g::AmbiWindParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiWindParams, environmentSize);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        p->params = params;
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiWindFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", customPresetName);
    } else if (version == 13u) {
        s3g::AmbiWindParams params {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        constexpr size_t legacyParamsSize = offsetof(s3g::AmbiWindParams, place);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName, sizeof(customPresetName))) return false;
        p->params = params;
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiWindFactoryPresetCount - 1u);
        std::snprintf(p->customPresetName, sizeof(p->customPresetName), "%s", customPresetName);
    } else {
        return false;
    }
    p->engine.setParams(p->params);
    p->engine.beginTransition();
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
    { kVoicesParamId, 630, 156, 1.0, 64.0, false },
    { kRateAParamId, 630, 182, 0.0, 1.0, false },
    { kRateBParamId, 630, 208, 0.0, 1.0, false },
    { kSpreadParamId, 630, 234, 0.0, 1.0, false },
    { kDeviationParamId, 630, 260, 0.0, 1.0, false },
    { kRateModeAParamId, 630, 104, 0.0, 0.5, true },
    { kFmAtoBParamId, 630, 334, 0.0, 1.0, false },
    { kFmBtoAParamId, 630, 360, 0.0, 1.0, false },
    { kFlutterParamId, 630, 386, 0.0, 1.0, false },
    { kMaterialParamId, 630, 412, 0.0, 1.0, false },
    { kGustShapeParamId, 630, 438, 0.0, 5.0, false },
    { kThresholdParamId, 630, 464, 0.0, 1.0, false },
    { kColorParamId, 630, 538, 0.0, 1.0, false },
    { kFilterParamId, 630, 564, 0.0, 1.0, false },
    { kResonanceParamId, 630, 590, 0.0, 1.0, false },
    { kFilterRunParamId, 630, 616, 0.0, 1.0, false },
    { kFilterSweepParamId, 630, 642, 0.0, 1.0, false },
    { kSaturationParamId, 630, 668, 0.0, 1.0, false },
    { kPwmAParamId, 630, 748, 0.0, 1.0, false },
    { kPwmBParamId, 630, 774, 0.0, 1.0, false },
    { kOutputParamId, 630, 800, -60.0, 12.0, false },
    { kMotionRateParamId, 896, 104, 0.001, 2.0, true },
    { kMotionFlowParamId, 896, 130, 0.0, 1.0, false },
    { kMotionShearParamId, 896, 156, 0.0, 1.0, false },
    { kMotionCurlParamId, 896, 182, 0.0, 1.0, false },
    { kMotionUpdraftParamId, 896, 208, 0.0, 1.0, false },
    { kFieldParamId, 896, 234, 0.0, 1.0, false },
    { kSpatialFollowParamId, 896, 260, 0.0, 1.0, false },
    { kAzimuthParamId, 896, 334, -180.0, 180.0, false },
    { kElevationParamId, 896, 360, -90.0, 90.0, false },
    { kDistanceParamId, 896, 386, 0.15, 2.0, false },
    { kSpaceParamId, 896, 536, 0.0, 1.0, false },
    { kEnvironmentSizeParamId, 896, 562, 0.0, 1.0, false },
    { kEnvironmentDecayParamId, 896, 588, 0.0, 1.0, false },
    { kEnvironmentDampingParamId, 896, 614, 0.0, 1.0, false },
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

double rateNormToHzForDisplay(double value)
{
    return 0.012 * std::pow(1.25 / 0.012, std::clamp(value, 0.0, 1.0));
}

@interface S3GAmbiWindEncoderView : NSView {
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

@implementation S3GAmbiWindEncoderView
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
    return NSMakeRect(panel.origin.x + 104.0 + index * 48.0, panel.origin.y + 4.0, 43.0, 13.0);
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
    if (!_plugin || voice >= s3g::kAmbiWindMaxVoices) return { 0.0f, 0.0f, 0.0f };
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
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Wind Encoder"];
}

- (void)saveCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gwind" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3gwind", [self presetDisplayName]]];
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
    [panel setAllowedFileTypes:@[ @"s3gwind" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return;
    _plugin->params = file.params;
    _plugin->engine.setParams(_plugin->params);
    _plugin->engine.beginTransition();
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

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    _fieldPage = 0;
    s3g::clap_gui::drawPanelHeader(@"WIND FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:0], header, @"FIELD", _fieldPage == 0, attrs, style);
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

    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiWindMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<NSPoint, s3g::kAmbiWindMaxVoices> projected {};
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
        const float gust = std::clamp(_plugin->guiGust[voice].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float activity = std::clamp(std::sqrt(std::max(0.0f, energy)) * 18.0f, 0.0f, 1.0f);
        const CGFloat base = voices > 32u ? 7.0 : 9.0;
        const CGFloat size = (selected ? base + 5.0 : base) + activity * 5.0f + gust * 7.0f;
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        if (gust > 0.04f || activity > 0.04f) {
            const CGFloat halo = size * (1.15 + gust * 1.9 + activity * 0.6);
            NSRect haloRect = NSMakeRect(projected[voice].x - halo * 0.5, projected[voice].y - halo * 0.5, halo, halo);
            [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:0.05 + gust * 0.18 + activity * 0.08] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:haloRect] fill];
        }
        [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:(selected ? 0.98 : 0.22 + gust * 0.54 + activity * 0.20)] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe6e6e6 : 0x4f4f4f, selected ? 1.0 : 0.22 + gust * 0.46 + activity * 0.18) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        if (gust > 0.28f || selected) {
            [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5, NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
        }
    }
    [NSGraphicsContext restoreGraphicsState];

    const float az = _plugin->guiAzimuth[_selectedVoice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[_selectedVoice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[_selectedVoice].load(std::memory_order_relaxed);
    const float energy = _plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed);
    const float gust = _plugin->guiGust[_selectedVoice].load(std::memory_order_relaxed);
    NSString* readout = [NSString stringWithFormat:@"V%02u  AZ%+.1f  EL%+.1f  D%.2f  G%.2f  E%.3f", _selectedVoice + 1u, az, el, dist, gust, energy];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
    [@"MICROWEATHER FIELD     GUST SIZE + XYZ FLOW MOTION     ACN/SN3D" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19) withAttributes:valueAttrs];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    char display[64] {};
    if (param == kRateBParamId) {
        const double hz = rateNormToHzForDisplay(value);
        if (hz < 1.0) std::snprintf(display, sizeof(display), "%.4f Hz", hz);
        else std::snprintf(display, sizeof(display), "%.2f Hz", hz);
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
    return [NSString stringWithUTF8String:s3g::ambiWindFactoryPresetInfo(_plugin->presetIndex).name];
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->params;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 228, style);
    s3g::clap_gui::drawPanelHeader(@"SOURCE AND GUST", true, 630, 42, 250, 21, attrs, style);
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] panelX:630 y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VECTOR LFO" param:kRateModeAParamId value:p.vectorRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"MATERIAL TYPE" value:[NSString stringWithUTF8String:kMaterialNames[std::min<uint32_t>(p.materialMode, 9u)]] panelX:630 y:130 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:p.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"WIND" param:kRateAParamId value:p.wind attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GUST RATE" param:kRateBParamId value:p.gustRate attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPREAD" param:kSpreadParamId value:p.spread attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEVIATION" param:kDeviationParamId value:p.deviation attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 282, 250, 218, style);
    s3g::clap_gui::drawPanelHeader(@"GUST AND MATERIAL", true, 630, 282, 250, 21, attrs, style);
    static constexpr const char* kEdgeNames[] = { "SOFT", "BEND", "HARD" };
    [self drawMenu:@"GUST EDGE" value:[NSString stringWithUTF8String:kEdgeNames[std::min<uint32_t>(p.gustEdge, 2u)]] panelX:630 y:308 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GUST DEPTH" param:kFmAtoBParamId value:p.gustDepth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TURBULENCE" param:kFmBtoAParamId value:p.turbulence attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FLUTTER" param:kFlutterParamId value:p.flutter attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"MATERIAL" param:kMaterialParamId value:p.material attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GUST SHAPE" param:kGustShapeParamId value:p.gustShape attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CENTER" param:kThresholdParamId value:p.center attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 512, 250, 186, style);
    s3g::clap_gui::drawPanelHeader(@"FILTER AND TONE", true, 630, 512, 250, 21, attrs, style);
    [self drawSlider:@"SWEEP" param:kColorParamId value:p.sweep attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"Q" param:kFilterParamId value:p.q attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SHRILL" param:kResonanceParamId value:p.shrill attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BODY" param:kFilterRunParamId value:p.body attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BREATH" param:kFilterSweepParamId value:p.breath attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GRIT" param:kSaturationParamId value:p.grit attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 722, 250, 124, style);
    s3g::clap_gui::drawPanelHeader(@"AIR AND OUTPUT", true, 630, 722, 250, 21, attrs, style);
    [self drawSlider:@"AIR" param:kPwmAParamId value:p.air attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"HISS" param:kPwmBParamId value:p.hiss attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"OUTPUT" param:kOutputParamId value:p.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 254, style);
    s3g::clap_gui::drawPanelHeader(@"MICROWEATHER MOTION", true, 896, 42, 246, 21, attrs, style);
    [self drawSlider:@"FIELD RATE" param:kMotionRateParamId value:p.motionRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FLOW PUSH" param:kMotionFlowParamId value:p.motionFlow attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SHEAR" param:kMotionShearParamId value:p.motionShear attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CURL" param:kMotionCurlParamId value:p.motionCurl attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"UPDRAFT" param:kMotionUpdraftParamId value:p.motionUpdraft attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEPTH PUSH" param:kFieldParamId value:p.field attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"INERTIA" param:kSpatialFollowParamId value:p.spatialFollow attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 308, 246, 150, style);
    s3g::clap_gui::drawPanelHeader(@"MACRO WIND VECTOR", true, 896, 308, 246, 21, attrs, style);
    [self drawSlider:@"DIRECTION" param:kAzimuthParamId value:p.centerAzimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEVATION" param:kElevationParamId value:p.centerElevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RANGE" param:kDistanceParamId value:p.centerDistance attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 474, 246, 166, style);
    s3g::clap_gui::drawPanelHeader(@"ENVIRONMENT FIELD", true, 896, 474, 246, 21, attrs, style);
    [self drawMenu:@"PLACE" value:[NSString stringWithUTF8String:kPlaceNames[
        std::min<uint32_t>(p.place, s3g::kAmbiWindPlaceCount - 1u)]] panelX:896 y:510 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV RETURN" param:kSpaceParamId value:p.space attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV SIZE" param:kEnvironmentSizeParamId value:p.environmentSize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DECAY" param:kEnvironmentDecayParamId value:p.environmentDecay attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DAMPING" param:kEnvironmentDampingParamId value:p.environmentDamping attrs:attrs valueAttrs:valueAttrs style:style];
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return [self presetMenuRect];
    case 2: return NSZeroRect;
    case 3: return NSMakeRect(738, 129, 124, 15);
    case 4: return NSMakeRect(1004, 509, 124, 15);
    case 5: return NSZeroRect;
    case 6: return NSZeroRect;
    case 7: return NSZeroRect;
    case 8: return NSZeroRect;
    case 9: return NSMakeRect(738, 307, 124, 15);
    case 10: return NSMakeRect(738, 77, 124, 15);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return s3g::kAmbiWindFactoryPresetCount;
    case 2: return 0u;
    case 3: return 10u;
    case 4: return s3g::kAmbiWindPlaceCount;
    case 5: return 0u;
    case 6: return 0u;
    case 7: return 0u;
    case 8: return 0u;
    case 9: return 3u;
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
    static NSString* materialItems[] = { @"OPEN", @"LEAF", @"HOLLOW", @"WIRE", @"METAL", @"CHIMES", @"BLOCKS", @"HARP", @"REEDS", @"FABRIC" };
    static NSString* placeItems[] = { @"OPEN", @"CANOPY", @"PORCH", @"ROOM", @"HANGAR", @"CANYON", @"TUNNEL" };
    static NSString* edgeItems[] = { @"SOFT", @"BEND", @"HARD" };
    static NSString* presetItems[s3g::kAmbiWindFactoryPresetCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t i = 0; i < s3g::kAmbiWindFactoryPresetCount; ++i) presetItems[i] = [[NSString stringWithUTF8String:s3g::ambiWindFactoryPresetInfo(i).name] retain];
    });
    NSString** items = presetItems;
    int selected = static_cast<int>(_plugin->presetIndex);
    if (_openMenu == 3) {
        items = materialItems;
        selected = static_cast<int>(_plugin->params.materialMode);
    } else if (_openMenu == 4) {
        items = placeItems;
        selected = static_cast<int>(_plugin->params.place);
    } else if (_openMenu == 9) {
        items = edgeItems;
        selected = static_cast<int>(_plugin->params.gustEdge);
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
    [@"s3g AMBI WIND ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
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
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiWindMaxVoices);
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

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 3) applyParam(*_plugin, kRateModeBParamId, hit);
            else if (_openMenu == 4) applyParam(*_plugin, kPlaceParamId, hit);
            else if (_openMenu == 9) applyParam(*_plugin, kGustEdgeParamId, hit);
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
    if (NSPointInRect(point, NSMakeRect(738, 77, 124, 15))) { [self openMenu:10]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 129, 124, 15))) { [self openMenu:3]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 509, 124, 15))) { [self openMenu:4]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 307, 124, 15))) { [self openMenu:9]; return; }
    const NSRect panel = [self fieldPanelRect];
    if (NSPointInRect(point, panel)) {
        for (int i = 0; i < 1; ++i) {
            if (NSPointInRect(point, [self pageButtonRect:i])) {
                _fieldPage = i;
                [self setNeedsDisplay:YES];
                return;
            }
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
            _dragParam = static_cast<int>(spec.id);
            [self setParam:spec.id fromPoint:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _fieldPage = 0;
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
    p->guiView = [[S3GAmbiWindEncoderView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiWindEncoderView*>(p->guiView) stopRefreshTimer];
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
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GAmbiWindEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiWindEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    "org.s3g.s3g-dsp.ambi-wind-encoder-64",
    "s3g Ambi Wind Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "PAIA-inspired random-voltage filtered-noise wind field with direct 7OA ACN/SN3D output.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiWindFactoryPreset(0u);
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
