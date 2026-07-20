#include "s3g_ambi_terrain_navigator.h"
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
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiTerrainMaxChannels;
constexpr uint32_t kInputChannels = s3g::kAmbiTerrainMaxPoints;
constexpr uint32_t kStateVersion = 3;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kPointsParamId = 20;
constexpr clap_id kAzimuthParamId = 2;
constexpr clap_id kElevationParamId = 3;
constexpr clap_id kDistanceParamId = 4;
constexpr clap_id kRateParamId = 5;
constexpr clap_id kTraversalParamId = 6;
constexpr clap_id kTerrainDepthParamId = 7;
constexpr clap_id kLayerSpreadParamId = 8;
constexpr clap_id kInnerRadiusParamId = 9;
constexpr clap_id kOuterRadiusParamId = 10;
constexpr clap_id kAzimuthWarpParamId = 11;
constexpr clap_id kElevationWarpParamId = 12;
constexpr clap_id kDistanceWarpParamId = 13;
constexpr clap_id kFoldParamId = 14;
constexpr clap_id kSmoothingParamId = 15;
constexpr clap_id kInputParamId = 16;
constexpr clap_id kOutputParamId = 17;
constexpr clap_id kOrbitParamId = 18;
constexpr clap_id kPaletteParamId = 19;
constexpr clap_id kPlaybackParamId = 21;
constexpr clap_id kSyncParamId = 22;
constexpr clap_id kDivisionParamId = 23;
constexpr clap_id kPhaseParamId = 24;
constexpr clap_id kPhaseSpreadParamId = 25;
constexpr clap_id kEaseParamId = 26;
constexpr clap_id kDistanceScaleParamId = 27;
constexpr clap_id kDopplerParamId = 28;
constexpr clap_id kAirParamId = 29;
constexpr clap_id kSelectedSourceParamId = 30;
constexpr clap_id kRateSpreadParamId = 31;
constexpr clap_id kRateDeviationParamId = 32;

struct AmbiTerrainNavigatorParamsV1 {
    uint32_t order;
    uint32_t points;
    float azimuthDeg;
    float elevationDeg;
    float distance;
    float rateHz;
    float traversal;
    float terrainDepth;
    float layerSpread;
    float innerRadius;
    float outerRadius;
    float azimuthWarpDeg;
    float elevationWarpDeg;
    float distanceWarp;
    float fold;
    float smoothing;
    float inputGainDb;
    float outputGainDb;
    s3g::AmbiTerrainOrbit orbit;
    s3g::AmbiTerrainPalette palette;
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiTerrainNavigatorParams params {};
};

struct SavedStateV1 {
    uint32_t version = 1;
    AmbiTerrainNavigatorParamsV1 params {};
};

struct AmbiTerrainNavigatorParamsV2 {
    uint32_t order;
    uint32_t points;
    float azimuthDeg;
    float elevationDeg;
    float distance;
    float rateHz;
    float traversal;
    float terrainDepth;
    float layerSpread;
    float innerRadius;
    float outerRadius;
    float azimuthWarpDeg;
    float elevationWarpDeg;
    float distanceWarp;
    float fold;
    float smoothing;
    float inputGainDb;
    float outputGainDb;
    s3g::AmbiTerrainOrbit orbit;
    s3g::AmbiTerrainPalette palette;
    s3g::AmbiTerrainPlaybackMode playback;
    s3g::AmbiTerrainSyncMode syncMode;
    float syncDivisionBeats;
    float phase;
    float phaseSpread;
    float ease;
    float distanceScale;
    float doppler;
    float air;
    uint32_t selectedSource;
};

struct SavedStateV2 {
    uint32_t version = 2;
    AmbiTerrainNavigatorParamsV2 params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiTerrainNavigator encoder {};
    s3g::AmbiTerrainNavigatorParams params {};
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
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

const char* orbitName(uint32_t index)
{
    static constexpr const char* kNames[] { "DRIFT", "LISSAJOUS", "SPIRAL", "FOLD" };
    return kNames[std::min<uint32_t>(index, 3u)];
}

const char* paletteName(uint32_t index)
{
    static constexpr const char* kNames[] { "HARMONIC", "FBM", "CELL", "VOT", "RIDGES", "DUNES", "CRATERS", "TECTONIC" };
    return kNames[std::min<uint32_t>(index, 7u)];
}

const char* playbackName(uint32_t index)
{
    static constexpr const char* kNames[] { "OFF", "RUN", "SCRUB" };
    return kNames[std::min<uint32_t>(index, 2u)];
}

const char* syncName(uint32_t index)
{
    static constexpr const char* kNames[] { "FREE", "SYNC" };
    return kNames[std::min<uint32_t>(index, 1u)];
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: p.params.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiTerrainMaxOrder); break;
    case kPointsParamId: p.params.points = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiTerrainMaxPoints); break;
    case kAzimuthParamId: p.params.azimuthDeg = static_cast<float>(value); break;
    case kElevationParamId: p.params.elevationDeg = static_cast<float>(value); break;
    case kDistanceParamId: p.params.distance = static_cast<float>(value); break;
    case kRateParamId: p.params.rateHz = static_cast<float>(value); break;
    case kTraversalParamId: p.params.traversal = static_cast<float>(value); break;
    case kTerrainDepthParamId: p.params.terrainDepth = static_cast<float>(value); break;
    case kLayerSpreadParamId: p.params.layerSpread = static_cast<float>(value); break;
    case kInnerRadiusParamId: p.params.innerRadius = static_cast<float>(value); break;
    case kOuterRadiusParamId: p.params.outerRadius = static_cast<float>(value); break;
    case kAzimuthWarpParamId: p.params.azimuthWarpDeg = static_cast<float>(value); break;
    case kElevationWarpParamId: p.params.elevationWarpDeg = static_cast<float>(value); break;
    case kDistanceWarpParamId: p.params.distanceWarp = static_cast<float>(value); break;
    case kFoldParamId: p.params.fold = static_cast<float>(value); break;
    case kSmoothingParamId: p.params.smoothing = static_cast<float>(value); break;
    case kInputParamId: p.params.inputGainDb = static_cast<float>(value); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(value); break;
    case kOrbitParamId: p.params.orbit = static_cast<s3g::AmbiTerrainOrbit>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 3u)); break;
    case kPaletteParamId: p.params.palette = static_cast<s3g::AmbiTerrainPalette>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 7u)); break;
    case kPlaybackParamId: p.params.playback = static_cast<s3g::AmbiTerrainPlaybackMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kSyncParamId: p.params.syncMode = static_cast<s3g::AmbiTerrainSyncMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 1u)); break;
    case kDivisionParamId: p.params.syncDivisionBeats = static_cast<float>(value); break;
    case kPhaseParamId: p.params.phase = static_cast<float>(value); break;
    case kPhaseSpreadParamId: p.params.phaseSpread = static_cast<float>(value); break;
    case kEaseParamId: p.params.ease = static_cast<float>(value); break;
    case kDistanceScaleParamId: p.params.distanceScale = static_cast<float>(value); break;
    case kDopplerParamId: p.params.doppler = static_cast<float>(value); break;
    case kAirParamId: p.params.air = static_cast<float>(value); break;
    case kSelectedSourceParamId: p.params.selectedSource = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiTerrainMaxPoints) - 1u; break;
    case kRateSpreadParamId: p.params.rateSpread = static_cast<float>(value); break;
    case kRateDeviationParamId: p.params.rateDeviation = static_cast<float>(value); break;
    default: break;
    }
    p.encoder.setParams(p.params);
    p.params = p.encoder.params();
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

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->encoder.prepare(sampleRate);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->encoder.reset();
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

void updateTransportPhase(Plugin& p, const clap_event_transport_t* transport)
{
    if (p.params.syncMode != s3g::AmbiTerrainSyncMode::Sync) {
        p.encoder.useFreePhase();
        return;
    }
    if (transport && (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0) {
        const double beats = static_cast<double>(transport->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
        const double division = std::max(0.25, static_cast<double>(p.params.syncDivisionBeats));
        const double phase = std::fmod(beats / division, 1.0);
        p.encoder.setExternalPhase(static_cast<float>(phase < 0.0 ? phase + 1.0 : phase));
    } else {
        p.encoder.useFreePhase();
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc ? proc->in_events : nullptr);
    if (!proc) return CLAP_PROCESS_CONTINUE;
    updateTransportPhase(*p, proc->transport);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto* input = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = input && input->data32 ? std::min<uint32_t>(input->channel_count, kInputChannels) : 0u;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0, frames);
    if (!output.data32 || outChannels == 0u) return CLAP_PROCESS_CONTINUE;

    std::array<const float*, kInputChannels> inputPtrs {};
    std::array<float*, kOutputChannels> outputPtrs {};
    for (uint32_t ch = 0; ch < inChannels; ++ch) inputPtrs[ch] = input->data32[ch];
    for (uint32_t ch = 0; ch < outChannels; ++ch) outputPtrs[ch] = output.data32[ch];
    p->encoder.setParams(p->params);
    p->encoder.processBlock(inputPtrs.data(), outputPtrs.data(), inChannels, outChannels, frames);
    p->params = p->encoder.params();
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "64 Terrain In" : "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kPointsParamId, "Points", 1.0, 64.0, 16.0, true },
    { kAzimuthParamId, "Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Distance", 0.15, 3.0, 1.0, false },
    { kRateParamId, "Rate", 0.000001, 2.0, 0.035, false },
    { kTraversalParamId, "Traversal", 0.0, 1.0, 0.70, false },
    { kTerrainDepthParamId, "Terrain Depth", 0.0, 1.0, 0.55, false },
    { kLayerSpreadParamId, "Layer Spread", 0.0, 1.0, 0.62, false },
    { kInnerRadiusParamId, "Inner Radius", 0.05, 2.0, 0.42, false },
    { kOuterRadiusParamId, "Outer Radius", 0.10, 3.0, 1.35, false },
    { kAzimuthWarpParamId, "Azimuth Warp", 0.0, 180.0, 72.0, false },
    { kElevationWarpParamId, "Elevation Warp", 0.0, 90.0, 34.0, false },
    { kDistanceWarpParamId, "Distance Warp", 0.0, 1.0, 0.34, false },
    { kFoldParamId, "Fold", 0.0, 1.0, 0.20, false },
    { kSmoothingParamId, "Smoothing", 0.0, 0.995, 0.72, false },
    { kInputParamId, "Input", -60.0, 24.0, 0.0, false },
    { kOutputParamId, "Output", -60.0, 12.0, -9.0, false },
    { kOrbitParamId, "Orbit", 0.0, 3.0, 1.0, true },
    { kPaletteParamId, "Palette", 0.0, 7.0, 1.0, true },
    { kPlaybackParamId, "Playback", 0.0, 2.0, 1.0, true },
    { kSyncParamId, "Sync", 0.0, 1.0, 0.0, true },
    { kDivisionParamId, "Division", 0.25, 64.0, 4.0, false },
    { kPhaseParamId, "Phase", 0.0, 1.0, 0.0, false },
    { kPhaseSpreadParamId, "Phase Spread", 0.0, 1.0, 0.0, false },
    { kEaseParamId, "Ease", 0.0, 1.0, 0.0, false },
    { kDistanceScaleParamId, "Distance Scale", 0.05, 8.0, 1.0, false },
    { kDopplerParamId, "Doppler", 0.0, 1.0, 0.0, false },
    { kAirParamId, "Air", 0.0, 1.0, 0.0, false },
    { kSelectedSourceParamId, "Selected Source", 1.0, 64.0, 1.0, true },
    { kRateSpreadParamId, "Rate Spread", 0.0, 1.0, 0.35, false },
    { kRateDeviationParamId, "Rate Deviation", 0.0, 1.0, 0.18, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Terrain Navigator", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto p = self(plugin)->params;
    switch (id) {
    case kOrderParamId: *value = p.order; return true;
    case kPointsParamId: *value = p.points; return true;
    case kAzimuthParamId: *value = p.azimuthDeg; return true;
    case kElevationParamId: *value = p.elevationDeg; return true;
    case kDistanceParamId: *value = p.distance; return true;
    case kRateParamId: *value = p.rateHz; return true;
    case kTraversalParamId: *value = p.traversal; return true;
    case kTerrainDepthParamId: *value = p.terrainDepth; return true;
    case kLayerSpreadParamId: *value = p.layerSpread; return true;
    case kInnerRadiusParamId: *value = p.innerRadius; return true;
    case kOuterRadiusParamId: *value = p.outerRadius; return true;
    case kAzimuthWarpParamId: *value = p.azimuthWarpDeg; return true;
    case kElevationWarpParamId: *value = p.elevationWarpDeg; return true;
    case kDistanceWarpParamId: *value = p.distanceWarp; return true;
    case kFoldParamId: *value = p.fold; return true;
    case kSmoothingParamId: *value = p.smoothing; return true;
    case kInputParamId: *value = p.inputGainDb; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    case kOrbitParamId: *value = static_cast<uint32_t>(p.orbit); return true;
    case kPaletteParamId: *value = static_cast<uint32_t>(p.palette); return true;
    case kPlaybackParamId: *value = static_cast<uint32_t>(p.playback); return true;
    case kSyncParamId: *value = static_cast<uint32_t>(p.syncMode); return true;
    case kDivisionParamId: *value = p.syncDivisionBeats; return true;
    case kPhaseParamId: *value = p.phase; return true;
    case kPhaseSpreadParamId: *value = p.phaseSpread; return true;
    case kEaseParamId: *value = p.ease; return true;
    case kDistanceScaleParamId: *value = p.distanceScale; return true;
    case kDopplerParamId: *value = p.doppler; return true;
    case kAirParamId: *value = p.air; return true;
    case kSelectedSourceParamId: *value = p.selectedSource + 1u; return true;
    case kRateSpreadParamId: *value = p.rateSpread; return true;
    case kRateDeviationParamId: *value = p.rateDeviation; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kPointsParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kOrbitParamId) std::snprintf(display, size, "%s", orbitName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kPaletteParamId) std::snprintf(display, size, "%s", paletteName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kPlaybackParamId) std::snprintf(display, size, "%s", playbackName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kSyncParamId) std::snprintf(display, size, "%s", syncName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kAzimuthParamId || id == kElevationParamId || id == kAzimuthWarpParamId || id == kElevationWarpParamId) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kInputParamId || id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTraversalParamId || id == kTerrainDepthParamId || id == kLayerSpreadParamId || id == kDistanceWarpParamId || id == kFoldParamId || id == kSmoothingParamId || id == kPhaseParamId || id == kPhaseSpreadParamId || id == kEaseParamId || id == kDopplerParamId || id == kAirParamId || id == kRateSpreadParamId || id == kRateDeviationParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kRateParamId) {
        const double period = 1.0 / std::max(0.000001, value);
        if (period >= 3600.0) std::snprintf(display, size, "%.1f h/cycle", period / 3600.0);
        else if (period >= 60.0) std::snprintf(display, size, "%.1f min/cycle", period / 60.0);
        else if (value < 0.1) std::snprintf(display, size, "%.1f s/cycle", period);
        else std::snprintf(display, size, "%.3f Hz", value);
    }
    else if (id == kDivisionParamId) std::snprintf(display, size, "%.2g beats", value);
    else if (id == kDistanceScaleParamId) std::snprintf(display, size, "%.2fx", value);
    else if (id == kSelectedSourceParamId) std::snprintf(display, size, "%.0f", value);
    else std::snprintf(display, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    SavedState state { kStateVersion, p->params };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto* p = self(plugin);
    auto copyV1Params = [&](const auto& old) {
        p->params.order = old.order;
        p->params.points = old.points;
        p->params.azimuthDeg = old.azimuthDeg;
        p->params.elevationDeg = old.elevationDeg;
        p->params.distance = old.distance;
        p->params.rateHz = old.rateHz;
        p->params.traversal = old.traversal;
        p->params.terrainDepth = old.terrainDepth;
        p->params.layerSpread = old.layerSpread;
        p->params.innerRadius = old.innerRadius;
        p->params.outerRadius = old.outerRadius;
        p->params.azimuthWarpDeg = old.azimuthWarpDeg;
        p->params.elevationWarpDeg = old.elevationWarpDeg;
        p->params.distanceWarp = old.distanceWarp;
        p->params.fold = old.fold;
        p->params.smoothing = old.smoothing;
        p->params.inputGainDb = old.inputGainDb;
        p->params.outputGainDb = old.outputGainDb;
        p->params.orbit = old.orbit;
        p->params.palette = old.palette;
    };
    uint32_t version = 0;
    if (!readExact(stream, &version, sizeof(version))) return false;
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        p->params = state.params;
    } else if (version == 2u) {
        SavedStateV2 state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        const auto& old = state.params;
        copyV1Params(old);
        p->params.playback = old.playback;
        p->params.syncMode = old.syncMode;
        p->params.syncDivisionBeats = old.syncDivisionBeats;
        p->params.phase = old.phase;
        p->params.phaseSpread = old.phaseSpread;
        p->params.ease = old.ease;
        p->params.distanceScale = old.distanceScale;
        p->params.doppler = old.doppler;
        p->params.air = old.air;
        p->params.selectedSource = old.selectedSource;
    } else if (version == 1u) {
        SavedStateV1 state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        copyV1Params(state.params);
    } else {
        return false;
    }
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
NSColor* terrainSourceMarkerColor(uint32_t source, bool selected)
{
    static constexpr int kPalette[] {
        0x00e5ff, 0xfff000, 0xff4fd8, 0x00ff7a,
        0xff5a36, 0x7c6cff, 0x00b7ff, 0xc8ff00,
        0xff9f1c, 0x45ffdd, 0xff3f7f, 0xb7ff5a,
    };
    const int rgb = kPalette[source % (sizeof(kPalette) / sizeof(kPalette[0]))];
    return s3g::clap_gui::color(rgb, selected ? 1.0 : 0.92);
}

@interface S3GAmbiTerrainNavigatorView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    int _dragParam;
    int _viewMode;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    double _viewAzDeg;
    double _viewElDeg;
    double _viewZoom;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiTerrainNavigatorView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 792)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = 0;
        _viewMode = 0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
        _viewZoom = 1.0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (!_timer) _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)timerTick:(NSTimer*)timer { (void)timer; [self setNeedsDisplay:YES]; }

- (NSString*)valueText:(clap_id)param value:(double)value
{
    char text[64] {};
    paramsValueToText(&_plugin->plugin, param, value, text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

- (double)paramValue:(clap_id)param
{
    double value = 0.0;
    paramsGetValue(&_plugin->plugin, param, &value);
    return value;
}

- (const ParamDef*)paramDef:(clap_id)param
{
    for (const auto& def : kParams) if (def.id == param) return &def;
    return nullptr;
}

- (CGFloat)viewScaleForRect:(NSRect)rect
{
    return std::min(rect.size.width, rect.size.height) * 0.38 * std::clamp(_viewZoom, 0.55, 2.40);
}

- (NSPoint)projectWorldPoint:(s3g::Vec3)p rect:(NSRect)rect depth:(CGFloat*)depth
{
    const CGFloat cx = NSMidX(rect);
    const CGFloat cy = NSMidY(rect);
    const CGFloat scale = [self viewScaleForRect:rect];
    const float az = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float el = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 + se * p.z;
    const float z2 = -se * y1 + ce * p.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return NSMakePoint(cx + static_cast<CGFloat>(x1) * scale, cy - static_cast<CGFloat>(y2) * scale);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 90.0; _viewElDeg = 90.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
    [self setNeedsDisplay:YES];
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 732); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 638); }

- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 22.0;
    const CGFloat gap = 6.0;
    const CGFloat viewTotal = 3.0 * 38.0 + 2.0 * 6.0;
    const CGFloat zoomTotal = 2.0 * w + gap;
    const CGFloat viewStart = NSMaxX(rect) - 10.0 - viewTotal;
    const CGFloat start = viewStart - 12.0 - zoomTotal;
    return NSMakeRect(start + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 38.0;
    const CGFloat gap = 6.0;
    const CGFloat start = NSMaxX([self zoomButtonRect:1 inRect:rect]) + 12.0;
    return NSMakeRect(start + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

- (NSRect)playButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 48.0;
    const CGFloat gap = 6.0;
    const CGFloat start = rect.origin.x + 104.0;
    return NSMakeRect(start + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:rect], rect, labels[i], i == _viewMode, attrs, style);
    s3g::clap_gui::drawHeaderButton([self playButtonRect:0 inRect:rect], rect, @"EDIT", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self playButtonRect:1 inRect:rect], rect, @"PLAY", true, attrs, style);
}

- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"-", @"+" };
    for (int i = 0; i < 2; ++i) s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:rect], rect, labels[i], false, attrs, style);
}

- (s3g::Vec3)worldPoint:(const s3g::AmbiTerrainPoint&)point
{
    const s3g::Vec3 direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
}

- (void)drawTerrainShellInRect:(NSRect)rect
{
    struct Facet {
        std::array<NSPoint, 4> points {};
        CGFloat depth = 0.0;
        float terrain = 0.0f;
    };
    constexpr uint32_t kLongitudeBands = 28u;
    constexpr uint32_t kLatitudeBands = 14u;
    std::vector<Facet> facets;
    facets.reserve(kLongitudeBands * kLatitudeBands);
    for (uint32_t lat = 0; lat < kLatitudeBands; ++lat) {
        const float v0 = static_cast<float>(lat) / static_cast<float>(kLatitudeBands);
        const float v1 = static_cast<float>(lat + 1u) / static_cast<float>(kLatitudeBands);
        for (uint32_t lon = 0; lon < kLongitudeBands; ++lon) {
            const float u0 = static_cast<float>(lon) / static_cast<float>(kLongitudeBands);
            const float u1 = static_cast<float>(lon + 1u) / static_cast<float>(kLongitudeBands);
            const std::array<s3g::AmbiTerrainPoint, 4> shellPoints {
                _plugin->encoder.surfacePointForDisplay(u0, v0),
                _plugin->encoder.surfacePointForDisplay(u1, v0),
                _plugin->encoder.surfacePointForDisplay(u1, v1),
                _plugin->encoder.surfacePointForDisplay(u0, v1),
            };
            Facet facet {};
            for (uint32_t corner = 0; corner < 4u; ++corner) {
                CGFloat depth = 0.0;
                facet.points[corner] = [self projectWorldPoint:[self worldPoint:shellPoints[corner]] rect:rect depth:&depth];
                facet.depth += depth * 0.25;
                facet.terrain += shellPoints[corner].terrain * 0.25f;
            }
            facets.push_back(facet);
        }
    }
    std::sort(facets.begin(), facets.end(), [](const Facet& a, const Facet& b) { return a.depth > b.depth; });
    for (const auto& facet : facets) {
        NSBezierPath* face = [NSBezierPath bezierPath];
        [face moveToPoint:facet.points[0]];
        for (uint32_t corner = 1; corner < 4u; ++corner) [face lineToPoint:facet.points[corner]];
        [face closePath];
        const CGFloat light = std::clamp<CGFloat>(0.115 + static_cast<CGFloat>(facet.terrain) * 0.055 + facet.depth * 0.012, 0.055, 0.22);
        [[NSColor colorWithCalibratedWhite:light alpha:0.82] setFill];
        [face fill];
    }

    [[NSColor colorWithCalibratedWhite:0.48 alpha:0.24] setStroke];
    for (uint32_t lat = 1u; lat < kLatitudeBands; ++lat) {
        const float v = static_cast<float>(lat) / static_cast<float>(kLatitudeBands);
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line setLineWidth:0.45];
        for (uint32_t lon = 0; lon <= kLongitudeBands * 2u; ++lon) {
            const float u = static_cast<float>(lon) / static_cast<float>(kLongitudeBands * 2u);
            const NSPoint pt = [self projectWorldPoint:[self worldPoint:_plugin->encoder.surfacePointForDisplay(u, v)] rect:rect depth:nullptr];
            if (lon == 0u) [line moveToPoint:pt];
            else [line lineToPoint:pt];
        }
        [line stroke];
    }
    for (uint32_t lon = 0; lon < kLongitudeBands; lon += 2u) {
        const float u = static_cast<float>(lon) / static_cast<float>(kLongitudeBands);
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line setLineWidth:0.45];
        for (uint32_t lat = 0; lat <= kLatitudeBands * 2u; ++lat) {
            const float v = static_cast<float>(lat) / static_cast<float>(kLatitudeBands * 2u);
            const NSPoint pt = [self projectWorldPoint:[self worldPoint:_plugin->encoder.surfacePointForDisplay(u, v)] rect:rect depth:nullptr];
            if (lat == 0u) [line moveToPoint:pt];
            else [line lineToPoint:pt];
        }
        [line stroke];
    }
}

- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [s3g::clap_gui::color(0x111111) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(rect, 1, 1)] addClip];

    const auto params = _plugin->params;
    [self drawTerrainShellInRect:rect];
    const uint32_t visiblePaths = std::min<uint32_t>(params.points, 16u);
    for (uint32_t lane = 0; lane < visiblePaths; ++lane) {
        const uint32_t pi = params.selectedSource >= visiblePaths && lane == visiblePaths - 1u
            ? params.selectedSource
            : lane;
        const BOOL pathSelected = pi == params.selectedSource;
        NSBezierPath* bez = [NSBezierPath bezierPath];
        [bez setLineWidth:pathSelected ? 1.4 : 0.7];
        const uint32_t count = 128u;
        for (uint32_t i = 0; i < count; ++i) {
            const float phase = static_cast<float>(i) / static_cast<float>(count);
            const s3g::Vec3 v = [self worldPoint:_plugin->encoder.pathPointForDisplay(pi, phase)];
            NSPoint pt = [self projectWorldPoint:v rect:rect depth:nullptr];
            if (i == 0) [bez moveToPoint:pt];
            else [bez lineToPoint:pt];
        }
        const s3g::Vec3 first = [self worldPoint:_plugin->encoder.pathPointForDisplay(pi, 0.0f)];
        [bez lineToPoint:[self projectWorldPoint:first rect:rect depth:nullptr]];
        [[NSColor colorWithCalibratedWhite:(pathSelected ? 0.78 : 0.42) alpha:(pathSelected ? 0.90 : 0.45)] setStroke];
        [bez stroke];
    }

    const auto& points = _plugin->encoder.points();
    const uint32_t active = std::min<uint32_t>(params.points, s3g::kAmbiTerrainMaxPoints);
    for (uint32_t src = 0; src < active; ++src) {
        auto p = points[src];
        if (p.distance <= 0.0f) p = _plugin->encoder.pathPointForDisplay(src, params.phase);
        const s3g::Vec3 pos = [self worldPoint:p];
        NSPoint pt = [self projectWorldPoint:pos rect:rect depth:nullptr];
        const BOOL selected = src == params.selectedSource;
        const CGFloat r = selected ? 5.0 : 4.0;
        const NSRect outer = NSMakeRect(std::round(pt.x - r - 1.0), std::round(pt.y - r - 1.0), (r + 1.0) * 2.0, (r + 1.0) * 2.0);
        [[NSColor colorWithCalibratedWhite:0.02 alpha:0.95] setFill];
        NSRectFill(outer);
        [terrainSourceMarkerColor(src, selected) setFill];
        NSRectFill(NSMakeRect(std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
        [[NSColor colorWithCalibratedWhite:selected ? 0.96 : 0.12 alpha:selected ? 0.95 : 0.75] setStroke];
        NSFrameRect(NSMakeRect(std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
    }
    [NSGraphicsContext restoreGraphicsState];

    NSString* viewText = @"PLAY: CLICK/DRAG CAMERA";
    [viewText drawAtPoint:NSMakePoint(rect.origin.x + 10, NSMaxY(rect) - 22) withAttributes:attrs];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const ParamDef* def = [self paramDef:param];
    if (!def) return;
    const double value = [self paramValue:param];
    double norm = (value - def->min) / std::max(0.000001, def->max - def->min);
    if (param == kRateParamId || param == kDivisionParamId) {
        const double safeMin = std::max(0.000001, def->min);
        norm = std::log(std::max(safeMin, value) / safeMin) / std::log(def->max / safeMin);
    }
    s3g::clap_gui::drawSlider(name, [self valueText:param value:value], norm, y, attrs, s3g::clap_gui::softValueAttrs(), style, 646.0, 738.0, 826.0, 82.0);
}

- (void)drawMenu:(NSString*)name param:(clap_id)param y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, [self valueText:param value:[self paramValue:param]], y, attrs, s3g::clap_gui::softValueAttrs(), style, 646.0, 738.0, 126.0);
}

- (CGFloat)menuRowY:(int)menu
{
    switch (menu) {
    case 1: return 104.0;
    case 2: return 156.0;
    case 3: return 182.0;
    case 4: return 414.0;
    case 5: return 436.0;
    default: return 0.0;
    }
}

- (CGFloat)menuY { return [self menuRowY:_openMenu] + 17.0; }

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* orbitItems[] = { @"DRIFT", @"LISSAJOUS", @"SPIRAL", @"FOLD" };
    static NSString* paletteItems[] = { @"HARMONIC", @"FBM", @"CELL", @"VOT", @"RIDGES", @"DUNES", @"CRATERS", @"TECTONIC" };
    static NSString* playbackItems[] = { @"OFF", @"RUN", @"SCRUB" };
    static NSString* syncItems[] = { @"FREE", @"SYNC" };
    NSString* const* items = orderItems;
    int selected = 0;
    if (_openMenu == 1) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
        _menuItemCount = 7u;
    } else if (_openMenu == 2) {
        items = orbitItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.orbit));
        _menuItemCount = 4u;
    } else if (_openMenu == 3) {
        items = paletteItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.palette));
        _menuItemCount = 8u;
    } else if (_openMenu == 4) {
        items = playbackItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.playback));
        _menuItemCount = 3u;
    } else if (_openMenu == 5) {
        items = syncItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.syncMode));
        _menuItemCount = 2u;
    }
    const CGFloat itemH = 18.0;
    s3g::clap_gui::drawDropdownMenu(NSMakeRect(738.0, [self menuY], 126.0, itemH * _menuItemCount),
        itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [@"s3g AMBI TERRAIN NAVIGATOR 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    [s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)) drawAtPoint:NSMakePoint(735, 14) withAttributes:valueAttrs];
    [@"64 CH" drawAtPoint:NSMakePoint(832, 14) withAttributes:valueAttrs];
    const NSRect fieldPanel = [self fieldPanelRect];
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PATH FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, labelAttrs, style);
    [self drawViewButtonsInRect:fieldPanel attrs:valueAttrs style:style];
    [self drawZoomButtonsInRect:fieldPanel attrs:valueAttrs style:style];
    [self drawField:[self fieldRect] attrs:valueAttrs style:style];
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 330, style);
    s3g::clap_gui::drawPanelHeader(@"PATH", true, 630, 42, 250, 21, labelAttrs, style);
    s3g::clap_gui::drawPanelFrame(630, 384, 250, 320, style);
    s3g::clap_gui::drawPanelHeader(@"MOTION", true, 630, 384, 250, 21, labelAttrs, style);
    [self drawSlider:@"INPUTS" param:kPointsParamId y:78 attrs:labelAttrs style:style];
    [self drawMenu:@"ORDER" param:kOrderParamId y:104 attrs:labelAttrs style:style];
    [self drawSlider:@"SOURCE" param:kSelectedSourceParamId y:130 attrs:labelAttrs style:style];
    [self drawMenu:@"TRACE" param:kOrbitParamId y:156 attrs:labelAttrs style:style];
    [self drawMenu:@"SKIN" param:kPaletteParamId y:182 attrs:labelAttrs style:style];
    [self drawSlider:@"AZIM" param:kAzimuthParamId y:208 attrs:labelAttrs style:style];
    [self drawSlider:@"ELEV" param:kElevationParamId y:234 attrs:labelAttrs style:style];
    [self drawSlider:@"DEPTH" param:kTerrainDepthParamId y:260 attrs:labelAttrs style:style];
    [self drawSlider:@"TRAVERSE" param:kTraversalParamId y:286 attrs:labelAttrs style:style];
    [self drawSlider:@"FOLD" param:kFoldParamId y:312 attrs:labelAttrs style:style];
    [self drawSlider:@"OUTPUT" param:kOutputParamId y:354 attrs:labelAttrs style:style];

    [self drawMenu:@"PLAY" param:kPlaybackParamId y:414 attrs:labelAttrs style:style];
    [self drawMenu:@"SYNC" param:kSyncParamId y:436 attrs:labelAttrs style:style];
    [self drawSlider:@"DIV" param:kDivisionParamId y:458 attrs:labelAttrs style:style];
    [self drawSlider:@"RATE SP" param:kRateSpreadParamId y:480 attrs:labelAttrs style:style];
    [self drawSlider:@"RATE DEV" param:kRateDeviationParamId y:502 attrs:labelAttrs style:style];
    [self drawSlider:@"RATE" param:kRateParamId y:528 attrs:labelAttrs style:style];
    [self drawSlider:@"PHASE" param:kPhaseParamId y:550 attrs:labelAttrs style:style];
    [self drawSlider:@"PH SPREAD" param:kPhaseSpreadParamId y:572 attrs:labelAttrs style:style];
    [self drawSlider:@"SMOOTH" param:kSmoothingParamId y:594 attrs:labelAttrs style:style];
    [self drawSlider:@"EASE" param:kEaseParamId y:616 attrs:labelAttrs style:style];
    [self drawSlider:@"DIST" param:kDistanceScaleParamId y:638 attrs:labelAttrs style:style];
    [self drawSlider:@"DOPPLER" param:kDopplerParamId y:660 attrs:labelAttrs style:style];
    [self drawSlider:@"AIR" param:kAirParamId y:682 attrs:labelAttrs style:style];
    [self drawOpenMenu:labelAttrs style:style];
}

- (clap_id)paramAtPoint:(NSPoint)pt
{
    struct Row { clap_id id; CGFloat y; };
    static constexpr Row rows[] {
        { kPointsParamId, 78 }, { kSelectedSourceParamId, 130 },
        { kAzimuthParamId, 208 }, { kElevationParamId, 234 }, { kTerrainDepthParamId, 260 },
        { kTraversalParamId, 286 }, { kFoldParamId, 312 }, { kOutputParamId, 354 },
        { kDivisionParamId, 458 }, { kRateSpreadParamId, 480 }, { kRateDeviationParamId, 502 },
        { kRateParamId, 528 }, { kPhaseParamId, 550 },
        { kPhaseSpreadParamId, 572 }, { kSmoothingParamId, 594 }, { kEaseParamId, 616 },
        { kDistanceScaleParamId, 638 }, { kDopplerParamId, 660 }, { kAirParamId, 682 },
    };
    for (const auto& row : rows) if (NSPointInRect(pt, NSMakeRect(626, row.y - 8, 266, 24))) return row.id;
    return 0;
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)pt
{
    const ParamDef* def = [self paramDef:param];
    if (!def) return;
    double norm = std::clamp((static_cast<double>(pt.x) - 738.0) / 82.0, 0.0, 1.0);
    double value = def->min + norm * (def->max - def->min);
    if (param == kRateParamId || param == kDivisionParamId) value = def->min * std::pow(def->max / def->min, norm);
    if (def->stepped) value = std::round(value);
    applyParam(*_plugin, param, value);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738.0, [self menuY], 126.0, itemH * _menuItemCount), itemH, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 2) applyParam(*_plugin, kOrbitParamId, hit);
            else if (_openMenu == 3) applyParam(*_plugin, kPaletteParamId, hit);
            else if (_openMenu == 4) applyParam(*_plugin, kPlaybackParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kSyncParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    auto openMenu = [&](int menu, uint32_t count) {
        _openMenu = menu;
        _menuItemCount = count;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
    };
    if (NSPointInRect(pt, NSMakeRect(738, 103, 126, 17))) { openMenu(1, 7); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 155, 126, 17))) { openMenu(2, 4); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 181, 126, 17))) { openMenu(3, 8); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 413, 126, 17))) { openMenu(4, 3); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 435, 126, 17))) { openMenu(5, 2); return; }
    const NSRect fieldPanel = [self fieldPanelRect];
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(pt, [self viewButtonRect:i inRect:fieldPanel])) {
            [self setViewPreset:i];
            return;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (NSPointInRect(pt, [self zoomButtonRect:i inRect:fieldPanel])) {
            _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.86 : 1.16), 0.55, 2.40);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(pt, [self fieldRect])) {
        _dragView = YES;
        _lastDragPoint = pt;
        return;
    }
    _dragParam = [self paramAtPoint:pt];
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
}
- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.32;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.24, -88.0, 88.0);
        _viewMode = -1;
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragParam = 0; _dragView = NO; }

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat itemH = 18.0;
    const int hover = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738.0, [self menuY], 126.0, itemH * _menuItemCount), itemH, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiTerrainNavigatorView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p || !p->guiView) return;
    [static_cast<S3GAmbiTerrainNavigatorView*>(p->guiView) stopRefreshTimer];
    [static_cast<NSView*>(p->guiView) removeFromSuperview];
    [static_cast<NSView*>(p->guiView) release];
    p->guiView = nullptr;
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 900; *h = 792; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win)
{
    if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false;
    auto* p = self(plugin);
    if (!p->guiView) return false;
    NSView* parent = static_cast<NSView*>(win->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, 900, 792)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbiTerrainNavigatorView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<S3GAmbiTerrainNavigatorView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
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

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-terrain-navigator-64",
    "s3g Ambi Terrain Navigator 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.1-pre",
    "64-point ambisonic navigator whose AED source points traverse nested procedural wave terrains.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->encoder.prepare(p->sampleRate);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
