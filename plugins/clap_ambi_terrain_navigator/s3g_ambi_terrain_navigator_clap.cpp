#include "s3g_ambi_terrain_navigator.h"
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
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiTerrainMaxChannels;
constexpr uint32_t kInputChannels = s3g::kAmbiTerrainMaxPoints;
constexpr uint32_t kStateVersion = 5;
constexpr clap_id kReplaceStateActionId = CLAP_INVALID_ID - 1u;

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
constexpr clap_id kTerrainFormParamId = 33;
constexpr clap_id kTerrainFacetParamId = 34;
constexpr clap_id kTerrainBevelParamId = 35;
constexpr clap_id kTerrainOrientationParamId = 36;
constexpr clap_id kTerrainTerraceParamId = 37;
constexpr clap_id kTerrainTerraceStepsParamId = 38;
constexpr clap_id kTerrainRidgeParamId = 39;
constexpr clap_id kTerrainErosionParamId = 40;
constexpr clap_id kTerrainDomainWarpParamId = 41;
constexpr clap_id kTerrainTwistParamId = 42;
constexpr clap_id kTerrainRoughnessParamId = 43;
constexpr clap_id kTerrainReliefParamId = 44;
constexpr clap_id kTerrainReadParamId = 45;
constexpr clap_id kTerrainReadMixParamId = 46;
constexpr uint32_t kParamCount = 46u;

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

struct ControlSnapshot {
    s3g::AmbiTerrainNavigatorParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiTerrainNavigator encoder {};
    s3g::AmbiTerrainNavigatorParams params {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<clap_id> guiDragValueOutstanding { CLAP_INVALID_ID };
    s3g::clap_gui::SpscEventQueue<
        s3g::AmbiTerrainNavigatorParams, 8u> guiStateCommands {};
    std::atomic_flag controlSnapshotLock = ATOMIC_FLAG_INIT;
    ControlSnapshot publishedControl {};
    std::atomic<bool> publishedControlDirty { true };
    std::atomic<bool> pendingParamValuesRescan { false };
    std::atomic<bool> active { false };
    std::atomic<bool> processing { false };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kAmbiTerrainMaxPoints> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiTerrainMaxPoints> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiTerrainMaxPoints> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiTerrainMaxPoints> guiTerrain {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
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

const char* formName(uint32_t index)
{
    static constexpr const char* kNames[] { "SPHERE", "TETRA", "CUBE", "OCTA", "DODECA", "ICOSA" };
    return kNames[std::min<uint32_t>(index, 5u)];
}

const char* readName(uint32_t index)
{
    static constexpr const char* kNames[] {
        "HEIGHT", "EDGE", "CURVE", "BLEND", "GRADIENT",
        "RIDGE", "VALLEY", "NORMAL", "CROSS", "VECTOR",
    };
    return kNames[std::min<uint32_t>(index, 9u)];
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

bool assignParam(s3g::AmbiTerrainNavigatorParams& params,
    clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: params.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiTerrainMaxOrder); break;
    case kPointsParamId: params.points = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiTerrainMaxPoints); break;
    case kAzimuthParamId: params.azimuthDeg = static_cast<float>(value); break;
    case kElevationParamId: params.elevationDeg = static_cast<float>(value); break;
    case kDistanceParamId: params.distance = static_cast<float>(value); break;
    case kRateParamId: params.rateHz = static_cast<float>(value); break;
    case kTraversalParamId: params.traversal = static_cast<float>(value); break;
    case kTerrainDepthParamId: params.terrainDepth = static_cast<float>(value); break;
    case kLayerSpreadParamId: params.layerSpread = static_cast<float>(value); break;
    case kInnerRadiusParamId: params.innerRadius = static_cast<float>(value); break;
    case kOuterRadiusParamId: params.outerRadius = static_cast<float>(value); break;
    case kAzimuthWarpParamId: params.azimuthWarpDeg = static_cast<float>(value); break;
    case kElevationWarpParamId: params.elevationWarpDeg = static_cast<float>(value); break;
    case kDistanceWarpParamId: params.distanceWarp = static_cast<float>(value); break;
    case kFoldParamId: params.fold = static_cast<float>(value); break;
    case kSmoothingParamId: params.smoothing = static_cast<float>(value); break;
    case kInputParamId: params.inputGainDb = static_cast<float>(value); break;
    case kOutputParamId: params.outputGainDb = static_cast<float>(value); break;
    case kOrbitParamId: params.orbit = static_cast<s3g::AmbiTerrainOrbit>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 3u)); break;
    case kPaletteParamId: params.palette = static_cast<s3g::AmbiTerrainPalette>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 7u)); break;
    case kPlaybackParamId: params.playback = static_cast<s3g::AmbiTerrainPlaybackMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kSyncParamId: params.syncMode = static_cast<s3g::AmbiTerrainSyncMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 1u)); break;
    case kDivisionParamId: params.syncDivisionBeats = static_cast<float>(value); break;
    case kPhaseParamId: params.phase = static_cast<float>(value); break;
    case kPhaseSpreadParamId: params.phaseSpread = static_cast<float>(value); break;
    case kEaseParamId: params.ease = static_cast<float>(value); break;
    case kDistanceScaleParamId: params.distanceScale = static_cast<float>(value); break;
    case kDopplerParamId: params.doppler = static_cast<float>(value); break;
    case kAirParamId: params.air = static_cast<float>(value); break;
    case kSelectedSourceParamId: params.selectedSource = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiTerrainMaxPoints) - 1u; break;
    case kRateSpreadParamId: params.rateSpread = static_cast<float>(value); break;
    case kRateDeviationParamId: params.rateDeviation = static_cast<float>(value); break;
    case kTerrainFormParamId: params.terrainForm = static_cast<s3g::AmbiTerrainForm>(
        std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 5u)); break;
    case kTerrainFacetParamId: params.terrainFacet = static_cast<float>(value); break;
    case kTerrainBevelParamId: params.terrainBevel = static_cast<float>(value); break;
    case kTerrainOrientationParamId: params.terrainOrientation = static_cast<float>(value); break;
    case kTerrainTerraceParamId: params.terrainTerrace = static_cast<float>(value); break;
    case kTerrainTerraceStepsParamId: params.terrainTerraceSteps = static_cast<uint32_t>(std::lround(value)); break;
    case kTerrainRidgeParamId: params.terrainRidge = static_cast<float>(value); break;
    case kTerrainErosionParamId: params.terrainErosion = static_cast<float>(value); break;
    case kTerrainDomainWarpParamId: params.terrainDomainWarp = static_cast<float>(value); break;
    case kTerrainTwistParamId: params.terrainTwist = static_cast<float>(value); break;
    case kTerrainRoughnessParamId: params.terrainRoughness = static_cast<float>(value); break;
    case kTerrainReliefParamId: params.terrainRelief = static_cast<float>(value); break;
    case kTerrainReadParamId: params.terrainRead = static_cast<s3g::AmbiTerrainRead>(
        std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 9u)); break;
    case kTerrainReadMixParamId: params.terrainReadMix = static_cast<float>(value); break;
    default: return false;
    }
    return true;
}

void publishControlSnapshot(Plugin& p);

void applyParam(Plugin& p, clap_id id, double value)
{
    if (!assignParam(p.params, id, value)) return;
    p.encoder.setParams(p.params);
    p.params = p.encoder.params();
    publishControlSnapshot(p);
}

bool paramValueFromParams(const s3g::AmbiTerrainNavigatorParams& p,
    clap_id id, double& value)
{
    switch (id) {
    case kOrderParamId: value = p.order; break;
    case kPointsParamId: value = p.points; break;
    case kAzimuthParamId: value = p.azimuthDeg; break;
    case kElevationParamId: value = p.elevationDeg; break;
    case kDistanceParamId: value = p.distance; break;
    case kRateParamId: value = p.rateHz; break;
    case kTraversalParamId: value = p.traversal; break;
    case kTerrainDepthParamId: value = p.terrainDepth; break;
    case kLayerSpreadParamId: value = p.layerSpread; break;
    case kInnerRadiusParamId: value = p.innerRadius; break;
    case kOuterRadiusParamId: value = p.outerRadius; break;
    case kAzimuthWarpParamId: value = p.azimuthWarpDeg; break;
    case kElevationWarpParamId: value = p.elevationWarpDeg; break;
    case kDistanceWarpParamId: value = p.distanceWarp; break;
    case kFoldParamId: value = p.fold; break;
    case kSmoothingParamId: value = p.smoothing; break;
    case kInputParamId: value = p.inputGainDb; break;
    case kOutputParamId: value = p.outputGainDb; break;
    case kOrbitParamId: value = static_cast<uint32_t>(p.orbit); break;
    case kPaletteParamId: value = static_cast<uint32_t>(p.palette); break;
    case kPlaybackParamId: value = static_cast<uint32_t>(p.playback); break;
    case kSyncParamId: value = static_cast<uint32_t>(p.syncMode); break;
    case kDivisionParamId: value = p.syncDivisionBeats; break;
    case kPhaseParamId: value = p.phase; break;
    case kPhaseSpreadParamId: value = p.phaseSpread; break;
    case kEaseParamId: value = p.ease; break;
    case kDistanceScaleParamId: value = p.distanceScale; break;
    case kDopplerParamId: value = p.doppler; break;
    case kAirParamId: value = p.air; break;
    case kSelectedSourceParamId: value = p.selectedSource + 1u; break;
    case kRateSpreadParamId: value = p.rateSpread; break;
    case kRateDeviationParamId: value = p.rateDeviation; break;
    case kTerrainFormParamId: value = static_cast<uint32_t>(p.terrainForm); break;
    case kTerrainFacetParamId: value = p.terrainFacet; break;
    case kTerrainBevelParamId: value = p.terrainBevel; break;
    case kTerrainOrientationParamId: value = p.terrainOrientation; break;
    case kTerrainTerraceParamId: value = p.terrainTerrace; break;
    case kTerrainTerraceStepsParamId: value = p.terrainTerraceSteps; break;
    case kTerrainRidgeParamId: value = p.terrainRidge; break;
    case kTerrainErosionParamId: value = p.terrainErosion; break;
    case kTerrainDomainWarpParamId: value = p.terrainDomainWarp; break;
    case kTerrainTwistParamId: value = p.terrainTwist; break;
    case kTerrainRoughnessParamId: value = p.terrainRoughness; break;
    case kTerrainReliefParamId: value = p.terrainRelief; break;
    case kTerrainReadParamId: value = static_cast<uint32_t>(p.terrainRead); break;
    case kTerrainReadMixParamId: value = p.terrainReadMix; break;
    default: return false;
    }
    return true;
}

double canonicalParamValue(
    const s3g::AmbiTerrainNavigatorParams& base, clap_id id, double value)
{
    auto staged = base;
    if (!assignParam(staged, id, value)) return value;
    s3g::AmbiTerrainNavigator validator;
    validator.setParams(staged);
    (void)paramValueFromParams(validator.params(), id, value);
    return value;
}

void lockNonAudio(std::atomic_flag& lock)
{
    while (lock.test_and_set(std::memory_order_acquire)) {
        // Fixed-size snapshots are only waited on by non-audio threads.
    }
}

void publishControlSnapshot(Plugin& p)
{
    p.publishedControlDirty.store(true, std::memory_order_release);
    if (p.controlSnapshotLock.test_and_set(
            std::memory_order_acquire)) return;
    p.publishedControl.params = p.params;
    p.publishedControlDirty.store(false, std::memory_order_release);
    p.controlSnapshotLock.clear(std::memory_order_release);
}

void retryControlSnapshotPublication(Plugin& p)
{
    if (p.publishedControlDirty.load(std::memory_order_acquire)) {
        publishControlSnapshot(p);
    }
}

ControlSnapshot controlSnapshot(Plugin& p)
{
    lockNonAudio(p.controlSnapshotLock);
    const auto result = p.publishedControl;
    p.controlSnapshotLock.clear(std::memory_order_release);
    return result;
}

double publishedParamValue(Plugin& p, clap_id id)
{
    double value = 0.0;
    (void)paramValueFromParams(controlSnapshot(p).params, id, value);
    return value;
}

s3g::AmbiTerrainNavigatorParams publishedParamsSnapshot(Plugin& p)
{
    return controlSnapshot(p).params;
}

void publishPoints(Plugin& p)
{
    const auto& points = p.encoder.points();
    for (uint32_t index = 0u; index < s3g::kAmbiTerrainMaxPoints; ++index) {
        p.guiAzimuth[index].store(points[index].azimuthDeg,
            std::memory_order_relaxed);
        p.guiElevation[index].store(points[index].elevationDeg,
            std::memory_order_relaxed);
        p.guiDistance[index].store(points[index].distance,
            std::memory_order_relaxed);
        p.guiTerrain[index].store(points[index].terrain,
            std::memory_order_relaxed);
    }
}

void requestGuiParamService(Plugin& p)
{
    // process() consumes the GUI queue on the next audio block. Waking the
    // host's flush path as well is redundant while audio is running and makes
    // gesture-end cross the host's main/audio coordination boundary exactly
    // when the mouse is released.
    if (p.processing.load(std::memory_order_acquire)) return;
    if (p.hostParams && p.hostParams->request_flush) {
        p.hostParams->request_flush(p.host);
    } else if (p.host && p.host->request_process) {
        p.host->request_process(p.host);
    }
}

void requestParamValuesRescan(Plugin& p)
{
    p.pendingParamValuesRescan.store(true, std::memory_order_release);
    if (p.host && p.host->request_callback) {
        p.host->request_callback(p.host);
    }
}

bool queueGuiParamEvent(Plugin& p, s3g::clap_gui::ParamEventKind kind,
    clap_id id, double value = 0.0)
{
    if (!p.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(p);
    return true;
}

bool queueGuiCoalescedDragValue(Plugin& p, clap_id id, double value)
{
    clap_id expected = CLAP_INVALID_ID;
    if (!p.guiDragValueOutstanding.compare_exchange_strong(expected, id,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        return false;
    }
    if (!p.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) {
        p.guiDragValueOutstanding.store(
            CLAP_INVALID_ID, std::memory_order_release);
        return false;
    }
    requestGuiParamService(p);
    return true;
}

bool queueGuiParamValue(Plugin& p, clap_id id, double value)
{
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 },
        { s3g::clap_gui::ParamEventKind::Value, id, value },
        { s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 },
    }};
    if (!p.guiParamEvents.pushBatch(events.data(), events.size())) return false;
    requestGuiParamService(p);
    return true;
}

bool queueGuiParamValueAndGestureEnd(Plugin& p, clap_id id, double value)
{
    const std::array<s3g::clap_gui::ParamEvent, 2u> events {{
        { s3g::clap_gui::ParamEventKind::Value, id, value },
        { s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 },
    }};
    if (!p.guiParamEvents.pushBatch(events.data(), events.size())) return false;
    requestGuiParamService(p);
    return true;
}

bool queueGuiState(Plugin& p,
    const s3g::AmbiTerrainNavigatorParams& params)
{
    if (p.guiStateCommands.available() == 0u
        || p.guiParamEvents.available() == 0u) return false;
    if (!p.guiStateCommands.push(params)) return false;
    const s3g::clap_gui::ParamEvent action {
        s3g::clap_gui::ParamEventKind::Value,
        kReplaceStateActionId, 0.0
    };
    if (!p.guiParamEvents.push(action)) return false;
    requestGuiParamService(p);
    return true;
}

bool pushGuiParamEvent(const clap_output_events_t* out,
    const s3g::clap_gui::ParamEvent& pending)
{
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

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* out)
{
    s3g::clap_gui::ParamEvent pending {};
    while (p.guiParamEvents.peek(pending)) {
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value
            && pending.paramId == kReplaceStateActionId) {
            s3g::AmbiTerrainNavigatorParams params {};
            if (!p.guiStateCommands.peek(params)) break;
            p.params = params;
            p.encoder.setParams(p.params);
            p.params = p.encoder.params();
            publishControlSnapshot(p);
            requestParamValuesRescan(p);
            p.guiStateCommands.pop();
            p.guiParamEvents.pop();
            continue;
        }
        if (!pushGuiParamEvent(out, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(p, pending.paramId, pending.value);
        }
        p.guiParamEvents.pop();
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            clap_id expected = pending.paramId;
            (void)p.guiDragValueOutstanding.compare_exchange_strong(
                expected, CLAP_INVALID_ID,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }
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
    publishControlSnapshot(*p);
    publishPoints(*p);
    p->active.store(true, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->processing.store(false, std::memory_order_release);
    p->active.store(false, std::memory_order_release);
}
bool startProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(true, std::memory_order_release);
    return true;
}
void stopProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(false, std::memory_order_release);
}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->encoder.reset();
    publishPoints(*p);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    bool changed = false;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            changed |= assignParam(p.params, param->param_id, param->value);
        }
    }
    if (changed) {
        p.encoder.setParams(p.params);
        p.params = p.encoder.params();
        publishControlSnapshot(p);
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
    serviceGuiParamEvents(*p, proc->out_events);
    retryControlSnapshotPublication(*p);
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
    p->encoder.processBlock(inputPtrs.data(), outputPtrs.data(), inChannels, outChannels, frames);
    publishPoints(*p);
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->pendingParamValuesRescan.exchange(false,
            std::memory_order_acq_rel)
        && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
}

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
    { kPointsParamId, "Input Count", 1.0, 64.0, 16.0, true },
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
    { kTerrainFormParamId, "Terrain Form", 0.0, 5.0, 0.0, true },
    { kTerrainFacetParamId, "Terrain Facet", 0.0, 1.0, 0.0, false },
    { kTerrainBevelParamId, "Terrain Bevel", 0.0, 1.0, 0.18, false },
    { kTerrainOrientationParamId, "Terrain Orientation", -1.0, 1.0, 0.0, false },
    { kTerrainTerraceParamId, "Terrain Terrace", 0.0, 1.0, 0.0, false },
    { kTerrainTerraceStepsParamId, "Terrain Terrace Steps", 2.0, 24.0, 8.0, true },
    { kTerrainRidgeParamId, "Terrain Ridge", 0.0, 1.0, 0.0, false },
    { kTerrainErosionParamId, "Terrain Erosion", 0.0, 1.0, 0.0, false },
    { kTerrainDomainWarpParamId, "Terrain Domain Warp", 0.0, 1.0, 0.0, false },
    { kTerrainTwistParamId, "Terrain Twist", -1.0, 1.0, 0.0, false },
    { kTerrainRoughnessParamId, "Terrain Roughness", 0.0, 1.0, 0.50, false },
    { kTerrainReliefParamId, "Terrain Relief", 0.0, 1.0, 1.0, false },
    { kTerrainReadParamId, "Terrain Read", 0.0, 9.0, 0.0, true },
    { kTerrainReadMixParamId, "Terrain Read Mix", 0.0, 1.0, 0.50, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

constexpr bool paramIsAutomatable(clap_id id)
{
    switch (id) {
    case kOrderParamId:
    case kPointsParamId:
    case kOrbitParamId:
    case kPaletteParamId:
    case kSelectedSourceParamId:
    case kTerrainFormParamId:
    case kTerrainTerraceStepsParamId:
    case kTerrainReadParamId:
        return false;
    default:
        return true;
    }
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = (paramIsAutomatable(def.id) ? CLAP_PARAM_IS_AUTOMATABLE : 0)
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Encoder Surface Terrain", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || id < 1u || id > kParamCount) return false;
    *value = publishedParamValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kPointsParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kOrbitParamId) std::snprintf(display, size, "%s", orbitName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kPaletteParamId) std::snprintf(display, size, "%s", paletteName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kTerrainFormParamId) std::snprintf(display, size, "%s", formName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kTerrainReadParamId) std::snprintf(display, size, "%s", readName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kPlaybackParamId) std::snprintf(display, size, "%s", playbackName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kSyncParamId) std::snprintf(display, size, "%s", syncName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kAzimuthParamId || id == kElevationParamId || id == kAzimuthWarpParamId || id == kElevationWarpParamId) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kInputParamId || id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTerrainOrientationParamId || id == kTerrainTwistParamId) std::snprintf(display, size, "%+.0f deg", value * 180.0);
    else if (id == kTerrainTerraceStepsParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kTraversalParamId || id == kTerrainDepthParamId || id == kLayerSpreadParamId || id == kDistanceWarpParamId || id == kFoldParamId || id == kSmoothingParamId || id == kPhaseParamId || id == kPhaseSpreadParamId || id == kEaseParamId || id == kDopplerParamId || id == kAirParamId || id == kRateSpreadParamId || id == kRateDeviationParamId || id == kTerrainFacetParamId || id == kTerrainBevelParamId || id == kTerrainTerraceParamId || id == kTerrainRidgeParamId || id == kTerrainErosionParamId || id == kTerrainDomainWarpParamId || id == kTerrainRoughnessParamId || id == kTerrainReliefParamId || id == kTerrainReadMixParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
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

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    static constexpr const char* orbitNames[] { "DRIFT", "LISSAJOUS", "SPIRAL", "FOLD" };
    static constexpr const char* paletteNames[] { "HARMONIC", "FBM", "CELL", "VOT", "RIDGES", "DUNES", "CRATERS", "TECTONIC" };
    static constexpr const char* playbackNames[] { "OFF", "RUN", "SCRUB" };
    static constexpr const char* syncNames[] { "FREE", "SYNC" };
    static constexpr const char* formNames[] { "SPHERE", "TETRA", "CUBE", "OCTA", "DODECA", "ICOSA" };
    static constexpr const char* readNames[] {
        "HEIGHT", "EDGE", "CURVE", "BLEND", "GRADIENT",
        "RIDGE", "VALLEY", "NORMAL", "CROSS", "VECTOR",
    };
    const char* const* names = nullptr;
    uint32_t nameCount = 0u;
    if (id == kOrbitParamId) { names = orbitNames; nameCount = static_cast<uint32_t>(std::size(orbitNames)); }
    else if (id == kPaletteParamId) { names = paletteNames; nameCount = static_cast<uint32_t>(std::size(paletteNames)); }
    else if (id == kPlaybackParamId) { names = playbackNames; nameCount = static_cast<uint32_t>(std::size(playbackNames)); }
    else if (id == kSyncParamId) { names = syncNames; nameCount = static_cast<uint32_t>(std::size(syncNames)); }
    else if (id == kTerrainFormParamId) { names = formNames; nameCount = static_cast<uint32_t>(std::size(formNames)); }
    else if (id == kTerrainReadParamId) { names = readNames; nameCount = static_cast<uint32_t>(std::size(readNames)); }
    if (names) {
        for (uint32_t index = 0u; index < nameCount; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    *value = std::atof(display);
    if (id == kRateParamId) {
        if (std::strstr(display, "h/cycle")) *value = 1.0 / std::max(0.000001, *value * 3600.0);
        else if (std::strstr(display, "min/cycle")) *value = 1.0 / std::max(0.000001, *value * 60.0);
        else if (std::strstr(display, "s/cycle")) *value = 1.0 / std::max(0.000001, *value);
        return true;
    }
    const bool percent = id == kTraversalParamId || id == kTerrainDepthParamId || id == kLayerSpreadParamId
        || id == kDistanceWarpParamId || id == kFoldParamId || id == kSmoothingParamId
        || id == kPhaseParamId || id == kPhaseSpreadParamId || id == kEaseParamId
        || id == kDopplerParamId || id == kAirParamId || id == kRateSpreadParamId
        || id == kRateDeviationParamId || id == kTerrainFacetParamId || id == kTerrainBevelParamId
        || id == kTerrainTerraceParamId || id == kTerrainRidgeParamId || id == kTerrainErosionParamId
        || id == kTerrainDomainWarpParamId || id == kTerrainRoughnessParamId || id == kTerrainReliefParamId
        || id == kTerrainReadMixParamId;
    if (percent) *value *= 0.01;
    if (id == kTerrainOrientationParamId || id == kTerrainTwistParamId) *value /= 180.0;
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
    SavedState state { kStateVersion, publishedParamsSnapshot(*p) };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto* p = self(plugin);
    s3g::AmbiTerrainNavigatorParams loaded {};
    auto copyV1Params = [&](const auto& old) {
        loaded.order = old.order;
        loaded.points = old.points;
        loaded.azimuthDeg = old.azimuthDeg;
        loaded.elevationDeg = old.elevationDeg;
        loaded.distance = old.distance;
        loaded.rateHz = old.rateHz;
        loaded.traversal = old.traversal;
        loaded.terrainDepth = old.terrainDepth;
        loaded.layerSpread = old.layerSpread;
        loaded.innerRadius = old.innerRadius;
        loaded.outerRadius = old.outerRadius;
        loaded.azimuthWarpDeg = old.azimuthWarpDeg;
        loaded.elevationWarpDeg = old.elevationWarpDeg;
        loaded.distanceWarp = old.distanceWarp;
        loaded.fold = old.fold;
        loaded.smoothing = old.smoothing;
        loaded.inputGainDb = old.inputGainDb;
        loaded.outputGainDb = old.outputGainDb;
        loaded.orbit = old.orbit;
        loaded.palette = old.palette;
    };
    uint32_t version = 0;
    if (!readExact(stream, &version, sizeof(version))) return false;
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        loaded = state.params;
    } else if (version == 4u) {
        constexpr size_t kV4ParamsSize = offsetof(s3g::AmbiTerrainNavigatorParams, terrainRoughness);
        if (!readExact(stream, &loaded, kV4ParamsSize)) return false;
    } else if (version == 3u) {
        constexpr size_t kV3ParamsSize = offsetof(s3g::AmbiTerrainNavigatorParams, terrainForm);
        if (!readExact(stream, &loaded, kV3ParamsSize)) return false;
    } else if (version == 2u) {
        SavedStateV2 state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        const auto& old = state.params;
        copyV1Params(old);
        loaded.playback = old.playback;
        loaded.syncMode = old.syncMode;
        loaded.syncDivisionBeats = old.syncDivisionBeats;
        loaded.phase = old.phase;
        loaded.phaseSpread = old.phaseSpread;
        loaded.ease = old.ease;
        loaded.distanceScale = old.distanceScale;
        loaded.doppler = old.doppler;
        loaded.air = old.air;
        loaded.selectedSource = old.selectedSource;
    } else if (version == 1u) {
        SavedStateV1 state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        copyV1Params(state.params);
    } else {
        return false;
    }
    if (p->active.load(std::memory_order_acquire)) {
        if (!queueGuiState(*p, loaded)) return false;
    } else {
        p->params = loaded;
        p->encoder.setParams(p->params);
        p->params = p->encoder.params();
        publishControlSnapshot(*p);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
constexpr auto kOutputSourcePanel = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Output,
    { 630.0, 250.0, 42.0 }, 42.0, 3u);
static_assert(s3g::gui_layout::combinedOutputSourceCardinalityControlMatches(
    kOutputSourcePanel,
    s3g::gui_layout::SharedControlRole::SourceCardinality));
constexpr CGFloat kInputCountRowY = static_cast<CGFloat>(
    s3g::gui_layout::rowY(kOutputSourcePanel,
        s3g::gui_layout::combinedOutputSourceCardinalityRow(
            s3g::gui_layout::SharedControlRole::SourceCardinality)));

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

float terrainDisplayLerp(float current, float target, float follow)
{
    const float next = current + (target - current) * follow;
    return std::fabs(next - target) < 0.0001f ? target : next;
}

float terrainDisplayLerpAngle(float current, float target, float follow)
{
    float delta = target - current;
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;
    float next = current + delta * follow;
    while (next > 180.0f) next -= 360.0f;
    while (next <= -180.0f) next += 360.0f;
    return std::fabs(delta * (1.0f - follow)) < 0.0001f ? target : next;
}

float terrainDisplayLerpPhase(float current, float target, float follow)
{
    float delta = target - current;
    while (delta > 0.5f) delta -= 1.0f;
    while (delta < -0.5f) delta += 1.0f;
    float next = current + delta * follow;
    next -= std::floor(next);
    return std::fabs(delta * (1.0f - follow)) < 0.0001f ? target : next;
}

void smoothTerrainDisplayParams(s3g::AmbiTerrainNavigatorParams& current,
    const s3g::AmbiTerrainNavigatorParams& target, float follow)
{
    // Menus and cardinality controls change immediately. Continuously valued
    // geometry is eased independently for the display so path and terrain
    // edits do not teleport while the DSP remains authoritative.
    auto next = target;
    next.rateHz = terrainDisplayLerp(
        current.rateHz, target.rateHz, follow);
    next.azimuthDeg = terrainDisplayLerpAngle(
        current.azimuthDeg, target.azimuthDeg, follow);
    next.elevationDeg = terrainDisplayLerp(
        current.elevationDeg, target.elevationDeg, follow);
    next.distance = terrainDisplayLerp(
        current.distance, target.distance, follow);
    next.traversal = terrainDisplayLerp(
        current.traversal, target.traversal, follow);
    next.terrainDepth = terrainDisplayLerp(
        current.terrainDepth, target.terrainDepth, follow);
    next.layerSpread = terrainDisplayLerp(
        current.layerSpread, target.layerSpread, follow);
    next.innerRadius = terrainDisplayLerp(
        current.innerRadius, target.innerRadius, follow);
    next.outerRadius = terrainDisplayLerp(
        current.outerRadius, target.outerRadius, follow);
    next.azimuthWarpDeg = terrainDisplayLerp(
        current.azimuthWarpDeg, target.azimuthWarpDeg, follow);
    next.elevationWarpDeg = terrainDisplayLerp(
        current.elevationWarpDeg, target.elevationWarpDeg, follow);
    next.distanceWarp = terrainDisplayLerp(
        current.distanceWarp, target.distanceWarp, follow);
    next.fold = terrainDisplayLerp(current.fold, target.fold, follow);
    next.smoothing = terrainDisplayLerp(
        current.smoothing, target.smoothing, follow);
    next.inputGainDb = terrainDisplayLerp(
        current.inputGainDb, target.inputGainDb, follow);
    next.outputGainDb = terrainDisplayLerp(
        current.outputGainDb, target.outputGainDb, follow);
    next.syncDivisionBeats = terrainDisplayLerp(
        current.syncDivisionBeats, target.syncDivisionBeats, follow);
    next.phase = terrainDisplayLerpPhase(current.phase, target.phase, follow);
    next.phaseSpread = terrainDisplayLerp(
        current.phaseSpread, target.phaseSpread, follow);
    next.ease = terrainDisplayLerp(current.ease, target.ease, follow);
    next.distanceScale = terrainDisplayLerp(
        current.distanceScale, target.distanceScale, follow);
    next.doppler = terrainDisplayLerp(
        current.doppler, target.doppler, follow);
    next.air = terrainDisplayLerp(current.air, target.air, follow);
    next.rateSpread = terrainDisplayLerp(
        current.rateSpread, target.rateSpread, follow);
    next.rateDeviation = terrainDisplayLerp(
        current.rateDeviation, target.rateDeviation, follow);
    next.terrainFacet = terrainDisplayLerp(
        current.terrainFacet, target.terrainFacet, follow);
    next.terrainBevel = terrainDisplayLerp(
        current.terrainBevel, target.terrainBevel, follow);
    next.terrainOrientation = terrainDisplayLerp(
        current.terrainOrientation, target.terrainOrientation, follow);
    next.terrainTerrace = terrainDisplayLerp(
        current.terrainTerrace, target.terrainTerrace, follow);
    next.terrainRidge = terrainDisplayLerp(
        current.terrainRidge, target.terrainRidge, follow);
    next.terrainErosion = terrainDisplayLerp(
        current.terrainErosion, target.terrainErosion, follow);
    next.terrainDomainWarp = terrainDisplayLerp(
        current.terrainDomainWarp, target.terrainDomainWarp, follow);
    next.terrainTwist = terrainDisplayLerp(
        current.terrainTwist, target.terrainTwist, follow);
    next.terrainRoughness = terrainDisplayLerp(
        current.terrainRoughness, target.terrainRoughness, follow);
    next.terrainRelief = terrainDisplayLerp(
        current.terrainRelief, target.terrainRelief, follow);
    next.terrainReadMix = terrainDisplayLerp(
        current.terrainReadMix, target.terrainReadMix, follow);
    current = next;
}

bool terrainDisplaySurfaceMatches(
    const s3g::AmbiTerrainNavigatorParams& a,
    const s3g::AmbiTerrainNavigatorParams& b)
{
    return a.distance == b.distance
        && a.terrainDepth == b.terrainDepth
        && a.layerSpread == b.layerSpread
        && a.innerRadius == b.innerRadius
        && a.outerRadius == b.outerRadius
        && a.azimuthWarpDeg == b.azimuthWarpDeg
        && a.elevationWarpDeg == b.elevationWarpDeg
        && a.distanceWarp == b.distanceWarp
        && a.fold == b.fold
        && a.palette == b.palette
        && a.distanceScale == b.distanceScale
        && a.terrainForm == b.terrainForm
        && a.terrainFacet == b.terrainFacet
        && a.terrainBevel == b.terrainBevel
        && a.terrainOrientation == b.terrainOrientation
        && a.terrainTerrace == b.terrainTerrace
        && a.terrainTerraceSteps == b.terrainTerraceSteps
        && a.terrainRidge == b.terrainRidge
        && a.terrainErosion == b.terrainErosion
        && a.terrainDomainWarp == b.terrainDomainWarp
        && a.terrainTwist == b.terrainTwist
        && a.terrainRoughness == b.terrainRoughness
        && a.terrainRelief == b.terrainRelief
        && a.terrainRead == b.terrainRead
        && a.terrainReadMix == b.terrainReadMix;
}

bool terrainDisplayPathMatches(
    const s3g::AmbiTerrainNavigatorParams& a,
    const s3g::AmbiTerrainNavigatorParams& b)
{
    return terrainDisplaySurfaceMatches(a, b)
        && a.points == b.points
        && a.selectedSource == b.selectedSource
        && a.azimuthDeg == b.azimuthDeg
        && a.elevationDeg == b.elevationDeg
        && a.traversal == b.traversal
        && a.orbit == b.orbit
        && a.phaseSpread == b.phaseSpread
        && a.ease == b.ease;
}

constexpr uint32_t kTerrainDisplayLongitudeBands = 28u;
constexpr uint32_t kTerrainDisplayLatitudeBands = 14u;
constexpr uint32_t kTerrainDisplayFineLongitudes =
    kTerrainDisplayLongitudeBands * 2u + 1u;
constexpr uint32_t kTerrainDisplayFineLatitudes =
    kTerrainDisplayLatitudeBands * 2u + 1u;
constexpr uint32_t kTerrainDisplaySurfaceSamples =
    kTerrainDisplayFineLongitudes * kTerrainDisplayFineLatitudes;
constexpr uint32_t kTerrainDisplayMaxPaths = 16u;
constexpr uint32_t kTerrainDisplayPathSegments = 128u;

struct TerrainDisplaySample {
    s3g::Vec3 world {};
    float terrain = 0.0f;
};

constexpr uint32_t terrainDisplaySurfaceIndex(
    uint32_t longitude, uint32_t latitude)
{
    return latitude * kTerrainDisplayFineLongitudes + longitude;
}

struct TerrainSurfaceRenderCoordinator {
    std::atomic<uint64_t> generation { 0u };
    std::atomic<void*> owner { nullptr };
};

struct TerrainSurfaceRenderRequest {
    s3g::AmbiTerrainNavigatorParams params {};
    CGSize size {};
    double viewAzimuthDeg = 90.0;
    double viewElevationDeg = 0.0;
    double viewZoom = 1.0;
    uint64_t generation = 0u;
};

dispatch_queue_t terrainSurfaceRenderQueue()
{
    static dispatch_queue_t queue = nullptr;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        queue = dispatch_queue_create(
            "org.s3g.surface-terrain-render", DISPATCH_QUEUE_SERIAL);
    });
    return queue;
}

s3g::Vec3 terrainDisplayWorldPoint(const s3g::AmbiTerrainPoint& point)
{
    const s3g::Vec3 direction = s3g::directionFromAed(
        point.azimuthDeg, point.elevationDeg);
    return { direction.x * point.distance,
        direction.y * point.distance,
        direction.z * point.distance };
}

CGPoint terrainDisplayProjectPoint(const s3g::Vec3& point,
    const TerrainSurfaceRenderRequest& request, CGFloat* depth)
{
    const CGFloat scale = std::min(
        request.size.width, request.size.height) * 0.38
        * std::clamp(request.viewZoom, 0.55, 2.40);
    const float az = static_cast<float>(
        request.viewAzimuthDeg * s3g::kPi / 180.0);
    const float el = static_cast<float>(
        request.viewElevationDeg * s3g::kPi / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return CGPointMake(request.size.width * 0.5
            + static_cast<CGFloat>(x1) * scale,
        request.size.height * 0.5
            - static_cast<CGFloat>(y2) * scale);
}

CGImageRef createTerrainSurfaceImage(
    const TerrainSurfaceRenderRequest& request,
    const std::shared_ptr<TerrainSurfaceRenderCoordinator>& coordinator)
{
    if (request.size.width <= 0.0 || request.size.height <= 0.0)
        return nullptr;
    const auto cancelled = [&] {
        return !coordinator
            || coordinator->generation.load(std::memory_order_acquire)
                != request.generation;
    };
    if (cancelled()) return nullptr;
    s3g::AmbiTerrainNavigator encoder;
    // Display queries are stateless and do not use the depth processor. Do
    // not call prepare(): it allocates and clears 64 audio delay lines.
    encoder.setParams(request.params);
    std::array<TerrainDisplaySample,
        kTerrainDisplaySurfaceSamples> samples {};
    for (uint32_t latitude = 0u;
         latitude < kTerrainDisplayFineLatitudes; ++latitude) {
        if (cancelled()) return nullptr;
        const float v = static_cast<float>(latitude)
            / static_cast<float>(kTerrainDisplayFineLatitudes - 1u);
        for (uint32_t longitude = 0u;
             longitude < kTerrainDisplayFineLongitudes; ++longitude) {
            const float u = static_cast<float>(longitude)
                / static_cast<float>(kTerrainDisplayFineLongitudes - 1u);
            const auto point = encoder.surfacePointForDisplay(u, v);
            auto& sample = samples[terrainDisplaySurfaceIndex(
                longitude, latitude)];
            sample.world = terrainDisplayWorldPoint(point);
            sample.terrain = point.terrain;
        }
    }

    constexpr CGFloat bitmapScale = 2.0;
    const size_t pixelWidth = static_cast<size_t>(
        std::ceil(request.size.width * bitmapScale));
    const size_t pixelHeight = static_cast<size_t>(
        std::ceil(request.size.height * bitmapScale));
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(nullptr,
        pixelWidth, pixelHeight, 8u, pixelWidth * 4u, colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);
    if (!context) return nullptr;
    CGContextSetShouldAntialias(context, true);
    CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(pixelHeight));
    CGContextScaleCTM(context, bitmapScale, -bitmapScale);

    struct Facet {
        std::array<CGPoint, 4u> points {};
        CGFloat depth = 0.0;
        float terrain = 0.0f;
    };
    std::vector<Facet> facets;
    facets.reserve(kTerrainDisplayLongitudeBands
        * kTerrainDisplayLatitudeBands);
    for (uint32_t latitude = 0u;
         latitude < kTerrainDisplayLatitudeBands; ++latitude) {
        if (cancelled()) {
            CGContextRelease(context);
            return nullptr;
        }
        for (uint32_t longitude = 0u;
             longitude < kTerrainDisplayLongitudeBands; ++longitude) {
            const uint32_t x0 = longitude * 2u;
            const uint32_t x1 = (longitude + 1u) * 2u;
            const uint32_t y0 = latitude * 2u;
            const uint32_t y1 = (latitude + 1u) * 2u;
            const std::array<const TerrainDisplaySample*, 4u> corners {{
                &samples[terrainDisplaySurfaceIndex(x0, y0)],
                &samples[terrainDisplaySurfaceIndex(x1, y0)],
                &samples[terrainDisplaySurfaceIndex(x1, y1)],
                &samples[terrainDisplaySurfaceIndex(x0, y1)],
            }};
            Facet facet {};
            for (uint32_t corner = 0u; corner < 4u; ++corner) {
                CGFloat depth = 0.0;
                facet.points[corner] = terrainDisplayProjectPoint(
                    corners[corner]->world, request, &depth);
                facet.depth += depth * 0.25;
                facet.terrain += corners[corner]->terrain * 0.25f;
            }
            facets.push_back(facet);
        }
    }
    std::sort(facets.begin(), facets.end(),
        [](const Facet& a, const Facet& b) {
            return a.depth > b.depth;
        });
    for (const auto& facet : facets) {
        const CGFloat light = std::clamp<CGFloat>(
            0.115 + static_cast<CGFloat>(facet.terrain) * 0.055
                + facet.depth * 0.012,
            0.055, 0.22);
        CGContextSetRGBFillColor(
            context, light, light, light, 0.82);
        CGContextBeginPath(context);
        CGContextMoveToPoint(context,
            facet.points[0].x, facet.points[0].y);
        for (uint32_t corner = 1u; corner < 4u; ++corner) {
            CGContextAddLineToPoint(context,
                facet.points[corner].x, facet.points[corner].y);
        }
        CGContextClosePath(context);
        CGContextFillPath(context);
    }

    CGContextSetRGBStrokeColor(context, 0.48, 0.48, 0.48, 0.24);
    CGContextSetLineWidth(context, 0.45);
    for (uint32_t latitude = 1u;
         latitude < kTerrainDisplayLatitudeBands; ++latitude) {
        CGContextBeginPath(context);
        for (uint32_t longitude = 0u;
             longitude < kTerrainDisplayFineLongitudes; ++longitude) {
            const auto& sample = samples[terrainDisplaySurfaceIndex(
                longitude, latitude * 2u)];
            const CGPoint point = terrainDisplayProjectPoint(
                sample.world, request, nullptr);
            if (longitude == 0u)
                CGContextMoveToPoint(context, point.x, point.y);
            else
                CGContextAddLineToPoint(context, point.x, point.y);
        }
        CGContextStrokePath(context);
    }
    for (uint32_t longitude = 0u;
         longitude < kTerrainDisplayLongitudeBands; longitude += 2u) {
        CGContextBeginPath(context);
        for (uint32_t latitude = 0u;
             latitude < kTerrainDisplayFineLatitudes; ++latitude) {
            const auto& sample = samples[terrainDisplaySurfaceIndex(
                longitude * 2u, latitude)];
            const CGPoint point = terrainDisplayProjectPoint(
                sample.world, request, nullptr);
            if (latitude == 0u)
                CGContextMoveToPoint(context, point.x, point.y);
            else
                CGContextAddLineToPoint(context, point.x, point.y);
        }
        CGContextStrokePath(context);
    }

    // The static path mesh belongs to the same target geometry as the shell.
    // Rasterize both on the worker so drawRect never evaluates terrain DSP.
    const uint32_t pathCount = std::min<uint32_t>(
        request.params.points, kTerrainDisplayMaxPaths);
    for (uint32_t lane = 0u; lane < pathCount; ++lane) {
        if (cancelled()) {
            CGContextRelease(context);
            return nullptr;
        }
        const uint32_t source = request.params.selectedSource >= pathCount
                && lane == pathCount - 1u
            ? request.params.selectedSource : lane;
        const bool selected = source == request.params.selectedSource;
        const CGFloat light = selected ? 0.78 : 0.42;
        CGContextSetRGBStrokeColor(context, light, light, light,
            selected ? 0.90 : 0.45);
        CGContextSetLineWidth(context, selected ? 1.4 : 0.7);
        CGContextBeginPath(context);
        for (uint32_t segment = 0u;
             segment <= kTerrainDisplayPathSegments; ++segment) {
            const float phase = segment == kTerrainDisplayPathSegments
                ? 0.0f
                : static_cast<float>(segment)
                    / static_cast<float>(kTerrainDisplayPathSegments);
            const auto point = encoder.pathPointForDisplay(source, phase);
            const CGPoint projected = terrainDisplayProjectPoint(
                terrainDisplayWorldPoint(point), request, nullptr);
            if (segment == 0u)
                CGContextMoveToPoint(context, projected.x, projected.y);
            else
                CGContextAddLineToPoint(context, projected.x, projected.y);
        }
        CGContextStrokePath(context);
    }
    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    return image;
}

@interface S3GAmbiTerrainNavigatorView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    int _dragParam;
    BOOL _pendingDragValueDirty;
    double _pendingDragValue;
    BOOL _pendingGestureEnd;
    clap_id _pendingGestureParam;
    clap_id _displayOverrideParam;
    double _displayOverrideValue;
    double _displayOverrideReleaseTime;
    int _viewMode;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    double _viewAzDeg;
    double _viewElDeg;
    double _viewZoom;
    int _surfacePage;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    char _titlePresetName[64];
    s3g::AmbiTerrainNavigatorParams _paramsSnapshot;
    s3g::AmbiTerrainNavigatorParams _displayParams;
    s3g::AmbiTerrainNavigator _displayEncoder;
    std::array<s3g::AmbiTerrainPoint, s3g::kAmbiTerrainMaxPoints> _displayPoints;
    std::array<bool, s3g::kAmbiTerrainMaxPoints> _displayPointsPrimed;
    NSImage* _surfaceImageCache;
    BOOL _surfaceImageCacheValid;
    NSImage* _previousSurfaceImage;
    double _surfaceCrossfadeStartTime;
    s3g::AmbiTerrainNavigatorParams _requestedSurfaceParams;
    std::shared_ptr<TerrainSurfaceRenderCoordinator> _surfaceRenderCoordinator;
    double _lastDisplayUpdateTime;
    float _displayFollow;
    NSUInteger _drawPassCount;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)refreshControlSnapshot;
- (void)flushPendingDragValue;
- (void)requestSurfaceImageForParams:(const s3g::AmbiTerrainNavigatorParams&)params;
- (void)acceptSurfaceImage:(CGImageRef)image generation:(uint64_t)generation size:(NSSize)size;
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
        _pendingDragValueDirty = NO;
        _pendingDragValue = 0.0;
        _pendingGestureEnd = NO;
        _pendingGestureParam = CLAP_INVALID_ID;
        _displayOverrideParam = CLAP_INVALID_ID;
        _displayOverrideValue = 0.0;
        _displayOverrideReleaseTime = 0.0;
        _viewMode = 0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
        _viewZoom = 1.0;
        _surfacePage = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _paramsSnapshot = publishedParamsSnapshot(*plugin);
        _displayParams = _paramsSnapshot;
        _displayEncoder.setParams(_displayParams);
        _displayPoints = {};
        _displayPointsPrimed = {};
        _surfaceImageCache = nil;
        _surfaceImageCacheValid = NO;
        _previousSurfaceImage = nil;
        _surfaceCrossfadeStartTime = 0.0;
        _requestedSurfaceParams = _displayParams;
        _surfaceRenderCoordinator
            = std::make_shared<TerrainSurfaceRenderCoordinator>();
        _surfaceRenderCoordinator->owner.store(
            self, std::memory_order_release);
        _lastDisplayUpdateTime = [[NSProcessInfo processInfo] systemUptime];
        _displayFollow = 1.0f;
        _drawPassCount = 0u;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
        [self setWantsLayer:YES];
        [self requestSurfaceImageForParams:_requestedSurfaceParams];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (NSUInteger)drawPassCount { return _drawPassCount; }
- (void)dealloc
{
    [self stopRefreshTimer];
    if (_surfaceRenderCoordinator) {
        _surfaceRenderCoordinator->owner.store(
            nullptr, std::memory_order_release);
        _surfaceRenderCoordinator->generation.fetch_add(
            1u, std::memory_order_acq_rel);
    }
    [_previousSurfaceImage release];
    _previousSurfaceImage = nil;
    [_surfaceImageCache release];
    _surfaceImageCache = nil;
    [super dealloc];
}
- (void)startRefreshTimer
{
    if (_timer) return;
    _lastDisplayUpdateTime = [[NSProcessInfo processInfo] systemUptime];
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0
        target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
    _timer.tolerance = 1.0 / 120.0;
    // Hosts enter event-tracking mode while a slider is held. Common modes
    // keep terrain motion and visual interpolation alive during the gesture.
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)timerTick:(NSTimer*)timer
{
    (void)timer;
    @autoreleasepool {
        [self flushPendingDragValue];
        [self setNeedsDisplay:YES];
        // This is an animated editor. Keep committing the throttled frame
        // after mouse-up as well as during tracking; otherwise the scheduling
        // policy changes at release and some hosts defer the easing/crossfade
        // tail until their parameter-commit work has completed.
        [self displayIfNeeded];
    }
}
- (void)refreshControlSnapshot
{
    if (!_plugin) return;
    auto target = publishedParamsSnapshot(*_plugin);
    const double now = [[NSProcessInfo processInfo] systemUptime];
    if (_displayOverrideParam != CLAP_INVALID_ID) {
        double published = 0.0;
        const bool hasPublished = paramValueFromParams(
            target, _displayOverrideParam, published);
        if (!_dragParam && hasPublished
            && (std::fabs(published - _displayOverrideValue) < 0.000001
                || (_displayOverrideReleaseTime > 0.0
                    && now - _displayOverrideReleaseTime > 0.25))) {
            _displayOverrideParam = CLAP_INVALID_ID;
            _displayOverrideReleaseTime = 0.0;
        } else {
            (void)assignParam(target, _displayOverrideParam,
                _displayOverrideValue);
        }
    }
    if (!terrainDisplayPathMatches(
            _requestedSurfaceParams, target)) {
        _requestedSurfaceParams = target;
        [self requestSurfaceImageForParams:target];
    }
    const double elapsed = std::clamp(
        now - _lastDisplayUpdateTime, 0.0, 0.25);
    _lastDisplayUpdateTime = now;
    constexpr double kDisplayTransitionSeconds = 0.10;
    _displayFollow = static_cast<float>(
        1.0 - std::exp(-elapsed / kDisplayTransitionSeconds));
    _paramsSnapshot = target;
    smoothTerrainDisplayParams(_displayParams, target, _displayFollow);
    _displayEncoder.setParams(_displayParams);
}

- (NSString*)valueText:(clap_id)param value:(double)value
{
    char text[64] {};
    paramsValueToText(&_plugin->plugin, param, value, text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

- (double)paramValue:(clap_id)param
{
    if (_displayOverrideParam == param) return _displayOverrideValue;
    double value = 0.0;
    (void)paramValueFromParams(_paramsSnapshot, param, value);
    return value;
}

- (void)flushPendingDragValue
{
    if (!_plugin) return;
    if (_pendingGestureEnd) {
        const bool queued = _pendingDragValueDirty
            ? queueGuiParamValueAndGestureEnd(*_plugin,
                _pendingGestureParam, _pendingDragValue)
            : queueGuiParamEvent(*_plugin,
                s3g::clap_gui::ParamEventKind::GestureEnd,
                _pendingGestureParam);
        if (queued) {
            _pendingDragValueDirty = NO;
            _pendingGestureEnd = NO;
            _pendingGestureParam = CLAP_INVALID_ID;
        }
        return;
    }
    if (!_dragParam || !_pendingDragValueDirty) return;
    if (queueGuiCoalescedDragValue(*_plugin,
            static_cast<clap_id>(_dragParam), _pendingDragValue)) {
        _pendingDragValueDirty = NO;
    }
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
    [self requestSurfaceImageForParams:_requestedSurfaceParams];
    [self setNeedsDisplay:YES];
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 732); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 682); }

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

- (NSRect)pathTabRect:(int)index
{
    static constexpr CGFloat starts[] { 638.0, 682.0, 730.0, 774.0, 822.0 };
    static constexpr CGFloat widths[] { 40.0, 44.0, 40.0, 44.0, 42.0 };
    const int safe = std::clamp(index, 0, 4);
    return NSMakeRect(starts[safe], 163.0, widths[safe], 16.0);
}

- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:rect], rect, labels[i], i == _viewMode, attrs, style);
}

- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"-", @"+" };
    for (int i = 0; i < 2; ++i) s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:rect], rect, labels[i], false, attrs, style);
}

- (void)drawPathTabsWithAttrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"PATH", @"FORM", @"SKIN", @"WARP", @"READ" };
    const NSRect tabHeader = NSMakeRect(630, 160, 250, 21);
    for (int index = 0; index < 5; ++index)
        s3g::clap_gui::drawHeaderButton([self pathTabRect:index], tabHeader, labels[index], index == _surfacePage, attrs, style);
}

- (s3g::Vec3)worldPoint:(const s3g::AmbiTerrainPoint&)point
{
    const s3g::Vec3 direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
}

- (void)requestSurfaceImageForParams:(const s3g::AmbiTerrainNavigatorParams&)params
{
    if (!_surfaceRenderCoordinator) return;
    TerrainSurfaceRenderRequest request {};
    request.params = params;
    request.size = [self fieldRect].size;
    request.viewAzimuthDeg = _viewAzDeg;
    request.viewElevationDeg = _viewElDeg;
    request.viewZoom = _viewZoom;
    request.generation = _surfaceRenderCoordinator->generation.fetch_add(
        1u, std::memory_order_acq_rel) + 1u;
    const auto coordinator = _surfaceRenderCoordinator;
    dispatch_async(terrainSurfaceRenderQueue(), ^{
        @autoreleasepool {
            if (coordinator->generation.load(std::memory_order_acquire)
                != request.generation) return;
            CGImageRef image = createTerrainSurfaceImage(
                request, coordinator);
            if (!image) return;
            if (coordinator->generation.load(std::memory_order_acquire)
                != request.generation) {
                CGImageRelease(image);
                return;
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                auto* owner = static_cast<S3GAmbiTerrainNavigatorView*>(
                    coordinator->owner.load(std::memory_order_acquire));
                if (owner
                    && coordinator->generation.load(
                           std::memory_order_acquire)
                        == request.generation) {
                    [owner acceptSurfaceImage:image
                        generation:request.generation size:request.size];
                }
                CGImageRelease(image);
            });
        }
    });
}

- (void)acceptSurfaceImage:(CGImageRef)image
    generation:(uint64_t)generation size:(NSSize)size
{
    if (!image || !_surfaceRenderCoordinator
        || _surfaceRenderCoordinator->generation.load(
               std::memory_order_acquire)
            != generation) return;
    NSImage* next = [[NSImage alloc] initWithCGImage:image size:size];
    if (!next) return;
    [_previousSurfaceImage release];
    _previousSurfaceImage = _surfaceImageCache;
    _surfaceImageCache = next;
    _surfaceImageCacheValid = YES;
    _surfaceCrossfadeStartTime
        = [[NSProcessInfo processInfo] systemUptime];
    [self setNeedsDisplay:YES];
}

- (void)drawTerrainShellInRect:(NSRect)rect
{
    if (!_surfaceImageCache || !_surfaceImageCacheValid
        || !NSEqualSizes([_surfaceImageCache size], rect.size)) {
        return;
    }
    const NSRect sourceRect = NSMakeRect(
        0.0, 0.0, rect.size.width, rect.size.height);
    CGFloat transition = 1.0;
    if (_previousSurfaceImage) {
        constexpr double kSurfaceCrossfadeSeconds = 0.12;
        transition = static_cast<CGFloat>(std::clamp(
            ([[NSProcessInfo processInfo] systemUptime]
                - _surfaceCrossfadeStartTime)
                / kSurfaceCrossfadeSeconds,
            0.0, 1.0));
        [_previousSurfaceImage drawInRect:rect fromRect:sourceRect
            operation:NSCompositingOperationSourceOver
            fraction:1.0 - transition respectFlipped:YES hints:nil];
    }
    [_surfaceImageCache drawInRect:rect fromRect:sourceRect
        operation:NSCompositingOperationSourceOver fraction:transition
        respectFlipped:YES hints:nil];
    if (_previousSurfaceImage && transition >= 1.0) {
        [_previousSurfaceImage release];
        _previousSurfaceImage = nil;
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

    const auto params = _paramsSnapshot;
    [self drawTerrainShellInRect:rect];

    const uint32_t active = std::min<uint32_t>(params.points, s3g::kAmbiTerrainMaxPoints);
    for (uint32_t src = 0; src < active; ++src) {
        s3g::AmbiTerrainPoint target {};
        target.azimuthDeg = _plugin->guiAzimuth[src].load(std::memory_order_relaxed);
        target.elevationDeg = _plugin->guiElevation[src].load(std::memory_order_relaxed);
        target.distance = _plugin->guiDistance[src].load(std::memory_order_relaxed);
        target.terrain = _plugin->guiTerrain[src].load(std::memory_order_relaxed);
        if (target.distance <= 0.0f) {
            target = _displayEncoder.pathPointForDisplay(
                src, _displayParams.phase);
        }
        auto& p = _displayPoints[src];
        if (!_displayPointsPrimed[src]) {
            p = target;
            _displayPointsPrimed[src] = true;
        } else {
            p.azimuthDeg = terrainDisplayLerpAngle(
                p.azimuthDeg, target.azimuthDeg, _displayFollow);
            p.elevationDeg = terrainDisplayLerp(
                p.elevationDeg, target.elevationDeg, _displayFollow);
            p.distance = terrainDisplayLerp(
                p.distance, target.distance, _displayFollow);
            p.terrain = terrainDisplayLerp(
                p.terrain, target.terrain, _displayFollow);
            p.shell = target.shell;
        }
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

}

- (void)drawSlider:(NSString*)name param:(clap_id)param y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const ParamDef* def = [self paramDef:param];
    if (!def) return;
    const double value = [self paramValue:param];
    double norm = (value - def->min) / std::max(0.000001, def->max - def->min);
    if (param == kAzimuthParamId) {
        norm = s3g::aedAzimuthSliderNorm(static_cast<float>(value));
    } else if (param == kRateParamId || param == kDivisionParamId) {
        const double safeMin = std::max(0.000001, def->min);
        norm = std::log(std::max(safeMin, value) / safeMin) / std::log(def->max / safeMin);
    }
    s3g::clap_gui::drawSlider(name, [self valueText:param value:value], norm, y, attrs, s3g::clap_gui::softValueAttrs(), style, 646.0, 738.0, 826.0, 82.0);
}

- (void)drawMenu:(NSString*)name param:(clap_id)param y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, [self valueText:param value:[self paramValue:param]], y, attrs, s3g::clap_gui::softValueAttrs(), style, 646.0, 738.0, 126.0);
}

- (BOOL)point:(NSPoint)point isInControlRowAtY:(CGFloat)y
{
    return NSPointInRect(point, NSMakeRect(626.0, y - 8.0, 266.0, 24.0));
}

- (CGFloat)menuRowY:(int)menu
{
    switch (menu) {
    case 1: return 104.0;
    case 2: return 222.0;
    case 3: return 196.0;
    case 4: return 392.0;
    case 5: return 418.0;
    case 6: return 196.0;
    case 7: return 196.0;
    default: return 0.0;
    }
}

- (CGFloat)menuY { return [self menuRowY:_openMenu] + 17.0; }

- (NSRect)menuControlRect:(int)menu
{
    return NSMakeRect(738.0, [self menuRowY:menu] - 1.0, 126.0, 17.0);
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* orbitItems[] = { @"DRIFT", @"LISSAJOUS", @"SPIRAL", @"FOLD" };
    static NSString* paletteItems[] = { @"HARMONIC", @"FBM", @"CELL", @"VOT", @"RIDGES", @"DUNES", @"CRATERS", @"TECTONIC" };
    static NSString* playbackItems[] = { @"OFF", @"RUN", @"SCRUB" };
    static NSString* syncItems[] = { @"FREE", @"SYNC" };
    static NSString* formItems[] = { @"SPHERE", @"TETRA", @"CUBE", @"OCTA", @"DODECA", @"ICOSA" };
    static NSString* readItems[] = {
        @"HEIGHT", @"EDGE", @"CURVE", @"BLEND", @"GRADIENT",
        @"RIDGE", @"VALLEY", @"NORMAL", @"CROSS", @"VECTOR",
    };
    NSString* const* items = orderItems;
    int selected = 0;
    if (_openMenu == 1) {
        items = orderItems;
        selected = static_cast<int>(_paramsSnapshot.order) - 1;
        _menuItemCount = 7u;
    } else if (_openMenu == 2) {
        items = orbitItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.orbit));
        _menuItemCount = 4u;
    } else if (_openMenu == 3) {
        items = paletteItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.palette));
        _menuItemCount = 8u;
    } else if (_openMenu == 4) {
        items = playbackItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.playback));
        _menuItemCount = 3u;
    } else if (_openMenu == 5) {
        items = syncItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.syncMode));
        _menuItemCount = 2u;
    } else if (_openMenu == 6) {
        items = formItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.terrainForm));
        _menuItemCount = 6u;
    } else if (_openMenu == 7) {
        items = readItems;
        selected = static_cast<int>(static_cast<uint32_t>(_paramsSnapshot.terrainRead));
        _menuItemCount = 10u;
    }
    const CGFloat itemH = 18.0;
    s3g::clap_gui::drawDropdownMenu(NSMakeRect(738.0, [self menuY], 126.0, itemH * _menuItemCount),
        itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    ++_drawPassCount;
    [self refreshControlSnapshot];
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g AMBI ENCODER SURFACE TERRAIN",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        s3g::clap_gui::encoderTitleBand(900.0, 792.0),
        titleAttrs, labelAttrs, valueAttrs, style);
    const NSRect fieldPanel = [self fieldPanelRect];
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SURFACE TERRAIN FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, labelAttrs, style);
    [self drawViewButtonsInRect:fieldPanel attrs:valueAttrs style:style];
    [self drawZoomButtonsInRect:fieldPanel attrs:valueAttrs style:style];
    [self drawField:[self fieldRect] attrs:valueAttrs style:style];
    constexpr CGFloat outputPanelHeight = static_cast<CGFloat>(
        s3g::gui_layout::toolboxHeightForRows(3u));
    constexpr CGFloat surfacePanelHeight = static_cast<CGFloat>(
        s3g::gui_layout::toolboxHeightForRows(6u));
    constexpr CGFloat motionPanelHeight = static_cast<CGFloat>(
        s3g::gui_layout::toolboxHeightForRows(13u));
    s3g::clap_gui::drawPanelFrame(630, 42, 250, outputPanelHeight, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT / INPUT", true, 630, 42, 250, 21, labelAttrs, style);
    [self drawSlider:@"OUT" param:kOutputParamId y:78 attrs:labelAttrs style:style];
    [self drawMenu:@"ORDER" param:kOrderParamId y:104 attrs:labelAttrs style:style];
    [self drawSlider:@"INPUTS" param:kPointsParamId y:kInputCountRowY attrs:labelAttrs style:style];
    s3g::clap_gui::drawPanelFrame(630, 160, 250, surfacePanelHeight, style);
    s3g::clap_gui::drawPanelHeader(@"", true, 630, 160, 250, 21, labelAttrs, style);
    [self drawPathTabsWithAttrs:valueAttrs style:style];
    s3g::clap_gui::drawPanelFrame(630, 356, 250, motionPanelHeight, style);
    s3g::clap_gui::drawPanelHeader(@"PLAYBACK / MOTION", true, 630, 356, 250, 21, labelAttrs, style);
    if (_surfacePage == 0) {
        [self drawSlider:@"SOURCE" param:kSelectedSourceParamId y:196 attrs:labelAttrs style:style];
        [self drawMenu:@"TRACE" param:kOrbitParamId y:222 attrs:labelAttrs style:style];
        [self drawSlider:@"AZIM" param:kAzimuthParamId y:248 attrs:labelAttrs style:style];
        [self drawSlider:@"ELEV" param:kElevationParamId y:274 attrs:labelAttrs style:style];
        [self drawSlider:@"TRAVERSE" param:kTraversalParamId y:300 attrs:labelAttrs style:style];
    } else if (_surfacePage == 1) {
        [self drawMenu:@"FORM" param:kTerrainFormParamId y:196 attrs:labelAttrs style:style];
        [self drawSlider:@"FACET" param:kTerrainFacetParamId y:222 attrs:labelAttrs style:style];
        [self drawSlider:@"BEVEL" param:kTerrainBevelParamId y:248 attrs:labelAttrs style:style];
        [self drawSlider:@"ORIENT" param:kTerrainOrientationParamId y:274 attrs:labelAttrs style:style];
    } else if (_surfacePage == 2) {
        [self drawMenu:@"SKIN" param:kPaletteParamId y:196 attrs:labelAttrs style:style];
        [self drawSlider:@"DEPTH" param:kTerrainDepthParamId y:222 attrs:labelAttrs style:style];
        [self drawSlider:@"ROUGH" param:kTerrainRoughnessParamId y:248 attrs:labelAttrs style:style];
        [self drawSlider:@"FOLD" param:kFoldParamId y:274 attrs:labelAttrs style:style];
        [self drawSlider:@"RELIEF" param:kTerrainReliefParamId y:300 attrs:labelAttrs style:style];
    } else if (_surfacePage == 3) {
        [self drawSlider:@"TERRACE" param:kTerrainTerraceParamId y:196 attrs:labelAttrs style:style];
        [self drawSlider:@"STEPS" param:kTerrainTerraceStepsParamId y:222 attrs:labelAttrs style:style];
        [self drawSlider:@"RIDGE" param:kTerrainRidgeParamId y:248 attrs:labelAttrs style:style];
        [self drawSlider:@"ERODE" param:kTerrainErosionParamId y:274 attrs:labelAttrs style:style];
        [self drawSlider:@"DOMAIN" param:kTerrainDomainWarpParamId y:300 attrs:labelAttrs style:style];
        [self drawSlider:@"TWIST" param:kTerrainTwistParamId y:326 attrs:labelAttrs style:style];
    } else {
        [self drawMenu:@"READ" param:kTerrainReadParamId y:196 attrs:labelAttrs style:style];
        [self drawSlider:@"MIX" param:kTerrainReadMixParamId y:222 attrs:labelAttrs style:style];
        [self drawSlider:@"AZ WARP" param:kAzimuthWarpParamId y:248 attrs:labelAttrs style:style];
        [self drawSlider:@"EL WARP" param:kElevationWarpParamId y:274 attrs:labelAttrs style:style];
        [self drawSlider:@"DIST WARP" param:kDistanceWarpParamId y:300 attrs:labelAttrs style:style];
    }

    [self drawMenu:@"PLAY" param:kPlaybackParamId y:392 attrs:labelAttrs style:style];
    [self drawMenu:@"SYNC" param:kSyncParamId y:418 attrs:labelAttrs style:style];
    [self drawSlider:@"DIV" param:kDivisionParamId y:444 attrs:labelAttrs style:style];
    [self drawSlider:@"RATE SP" param:kRateSpreadParamId y:470 attrs:labelAttrs style:style];
    [self drawSlider:@"RATE DEV" param:kRateDeviationParamId y:496 attrs:labelAttrs style:style];
    [self drawSlider:@"RATE" param:kRateParamId y:522 attrs:labelAttrs style:style];
    [self drawSlider:@"PHASE" param:kPhaseParamId y:548 attrs:labelAttrs style:style];
    [self drawSlider:@"PH SPREAD" param:kPhaseSpreadParamId y:574 attrs:labelAttrs style:style];
    [self drawSlider:@"SMOOTH" param:kSmoothingParamId y:600 attrs:labelAttrs style:style];
    [self drawSlider:@"EASE" param:kEaseParamId y:626 attrs:labelAttrs style:style];
    [self drawSlider:@"DIST" param:kDistanceScaleParamId y:652 attrs:labelAttrs style:style];
    [self drawSlider:@"DOPPLER" param:kDopplerParamId y:678 attrs:labelAttrs style:style];
    [self drawSlider:@"AIR" param:kAirParamId y:704 attrs:labelAttrs style:style];
    [self drawOpenMenu:labelAttrs style:style];
}

- (clap_id)paramAtPoint:(NSPoint)pt
{
    struct Row { clap_id id; CGFloat y; };
    static constexpr Row pathRows[] {
        { kSelectedSourceParamId, 196 },
        { kAzimuthParamId, 248 }, { kElevationParamId, 274 }, { kTraversalParamId, 300 },
    };
    static constexpr Row formRows[] {
        { kTerrainFacetParamId, 222 }, { kTerrainBevelParamId, 248 },
        { kTerrainOrientationParamId, 274 },
    };
    static constexpr Row skinRows[] {
        { kTerrainDepthParamId, 222 }, { kTerrainRoughnessParamId, 248 },
        { kFoldParamId, 274 }, { kTerrainReliefParamId, 300 },
    };
    static constexpr Row warpRows[] {
        { kTerrainTerraceParamId, 196 }, { kTerrainTerraceStepsParamId, 222 },
        { kTerrainRidgeParamId, 248 }, { kTerrainErosionParamId, 274 },
        { kTerrainDomainWarpParamId, 300 }, { kTerrainTwistParamId, 326 },
    };
    static constexpr Row readRows[] {
        { kTerrainReadMixParamId, 222 }, { kAzimuthWarpParamId, 248 },
        { kElevationWarpParamId, 274 }, { kDistanceWarpParamId, 300 },
    };
    static constexpr Row motionRows[] {
        { kDivisionParamId, 444 }, { kRateSpreadParamId, 470 }, { kRateDeviationParamId, 496 },
        { kRateParamId, 522 }, { kPhaseParamId, 548 },
        { kPhaseSpreadParamId, 574 }, { kSmoothingParamId, 600 }, { kEaseParamId, 626 },
        { kDistanceScaleParamId, 652 }, { kDopplerParamId, 678 }, { kAirParamId, 704 },
    };
    if ([self point:pt isInControlRowAtY:78.0]) return kOutputParamId;
    if ([self point:pt isInControlRowAtY:kInputCountRowY]) return kPointsParamId;
    if (_surfacePage == 0) {
        for (const auto& row : pathRows) if ([self point:pt isInControlRowAtY:row.y]) return row.id;
    } else if (_surfacePage == 1) {
        for (const auto& row : formRows) if ([self point:pt isInControlRowAtY:row.y]) return row.id;
    } else if (_surfacePage == 2) {
        for (const auto& row : skinRows) if ([self point:pt isInControlRowAtY:row.y]) return row.id;
    } else if (_surfacePage == 3) {
        for (const auto& row : warpRows) if ([self point:pt isInControlRowAtY:row.y]) return row.id;
    } else {
        for (const auto& row : readRows) if ([self point:pt isInControlRowAtY:row.y]) return row.id;
    }
    for (const auto& row : motionRows) if ([self point:pt isInControlRowAtY:row.y]) return row.id;
    return 0;
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)pt
{
    const ParamDef* def = [self paramDef:param];
    if (!def) return;
    double norm = std::clamp((static_cast<double>(pt.x) - 738.0) / 82.0, 0.0, 1.0);
    double value = def->min + norm * (def->max - def->min);
    if (param == kAzimuthParamId) value = s3g::aedAzimuthFromSliderNorm(
        static_cast<float>(norm));
    else if (param == kRateParamId || param == kDivisionParamId) value = def->min * std::pow(def->max / def->min, norm);
    if (def->stepped) value = std::round(value);
    value = canonicalParamValue(_paramsSnapshot, param, value);
    _pendingDragValue = value;
    _pendingDragValueDirty = YES;
    _displayOverrideParam = param;
    _displayOverrideValue = value;
    _displayOverrideReleaseTime = 0.0;
    // The common-mode refresh timer redraws at a stable 30 Hz. Avoid asking
    // AppKit for an expensive full terrain render on every mouse event.
    if (!_timer) [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    [self refreshControlSnapshot];
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const auto titleBand = s3g::clap_gui::encoderTitleBand(900.0, 792.0);
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        const uint32_t order = _paramsSnapshot.order;
        const float output = _paramsSnapshot.outputGainDb;
        auto initial = s3g::AmbiTerrainNavigatorParams {};
        initial.order = order;
        initial.outputGainDb = output;
        if (!queueGuiState(*_plugin, initial)) NSBeep();
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &_plugin->plugin, @"Ambi Encoder Surface Terrain", kOutputParamId, &name)) {
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
            [self setNeedsDisplay:YES];
        } else {
            NSBeep();
        }
        return;
    }
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(titleBand.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &_plugin->plugin, @"Ambi Encoder Surface Terrain", &name)) {
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
            [self setNeedsDisplay:YES];
        } else {
            NSBeep();
        }
        return;
    }
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        const auto randomUnit = [] {
            return static_cast<double>(arc4random()) / 4294967295.0;
        };
        auto randomized = _paramsSnapshot;
        (void)assignParam(randomized, kOrbitParamId, arc4random_uniform(4u));
        (void)assignParam(randomized, kPaletteParamId, arc4random_uniform(8u));
        (void)assignParam(randomized, kTerrainFormParamId, arc4random_uniform(6u));
        (void)assignParam(randomized, kTerrainReadParamId, arc4random_uniform(10u));
        (void)assignParam(randomized, kTraversalParamId, randomUnit());
        (void)assignParam(randomized, kTerrainDepthParamId, randomUnit());
        (void)assignParam(randomized, kTerrainRoughnessParamId, randomUnit());
        (void)assignParam(randomized, kFoldParamId, randomUnit());
        (void)assignParam(randomized, kTerrainReliefParamId, 0.35 + randomUnit() * 0.65);
        (void)assignParam(randomized, kRateParamId, 0.002 * std::pow(250.0, randomUnit()));
        (void)assignParam(randomized, kPhaseSpreadParamId, randomUnit());
        (void)assignParam(randomized, kEaseParamId, randomUnit());
        (void)assignParam(randomized, kDopplerParamId, randomUnit());
        (void)assignParam(randomized, kAirParamId, randomUnit());
        if (!queueGuiState(*_plugin, randomized)) NSBeep();
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
        [self setNeedsDisplay:YES];
        return;
    }
    for (int index = 0; index < 5; ++index) {
        if (NSPointInRect(pt, [self pathTabRect:index])) {
            _surfacePage = index;
            _openMenu = 0;
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738.0, [self menuY], 126.0, itemH * _menuItemCount), itemH, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) queueGuiParamValue(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 2) queueGuiParamValue(*_plugin, kOrbitParamId, hit);
            else if (_openMenu == 3) queueGuiParamValue(*_plugin, kPaletteParamId, hit);
            else if (_openMenu == 4) queueGuiParamValue(*_plugin, kPlaybackParamId, hit);
            else if (_openMenu == 5) queueGuiParamValue(*_plugin, kSyncParamId, hit);
            else if (_openMenu == 6) queueGuiParamValue(*_plugin, kTerrainFormParamId, hit);
            else if (_openMenu == 7) queueGuiParamValue(*_plugin, kTerrainReadParamId, hit);
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
    if (NSPointInRect(pt, [self menuControlRect:1])) { openMenu(1, 7); return; }
    if (_surfacePage == 0) {
        if (NSPointInRect(pt, [self menuControlRect:2])) { openMenu(2, 4); return; }
    } else if (_surfacePage == 1) {
        if (NSPointInRect(pt, [self menuControlRect:6])) { openMenu(6, 6); return; }
    } else if (_surfacePage == 2) {
        if (NSPointInRect(pt, [self menuControlRect:3])) { openMenu(3, 8); return; }
    } else if (_surfacePage == 4) {
        if (NSPointInRect(pt, [self menuControlRect:7])) { openMenu(7, 10); return; }
    }
    if (NSPointInRect(pt, [self menuControlRect:4])) { openMenu(4, 3); return; }
    if (NSPointInRect(pt, [self menuControlRect:5])) { openMenu(5, 2); return; }
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
            [self requestSurfaceImageForParams:_requestedSurfaceParams];
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
    if (_dragParam) {
        _pendingDragValueDirty = NO;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &_plugin->plugin,
                static_cast<clap_id>(_dragParam), &defaultValue)) {
            queueGuiParamValue(*_plugin, static_cast<clap_id>(_dragParam), defaultValue);
            _dragParam = 0;
            _displayOverrideParam = CLAP_INVALID_ID;
            _displayOverrideReleaseTime = 0.0;
            [self setNeedsDisplay:YES];
            return;
        }
        (void)queueGuiParamEvent(*_plugin,
            s3g::clap_gui::ParamEventKind::GestureBegin,
            static_cast<clap_id>(_dragParam));
        [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
    }
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
        [self requestSurfaceImageForParams:_requestedSurfaceParams];
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam) {
        const clap_id param = static_cast<clap_id>(_dragParam);
        bool queued = false;
        if (_pendingDragValueDirty) {
            queued = queueGuiParamValueAndGestureEnd(
                *_plugin, param, _pendingDragValue);
            if (queued) _pendingDragValueDirty = NO;
        } else {
            queued = queueGuiParamEvent(*_plugin,
                s3g::clap_gui::ParamEventKind::GestureEnd, param);
        }
        if (!queued) {
            _pendingGestureEnd = YES;
            _pendingGestureParam = param;
        }
        _displayOverrideReleaseTime
            = [[NSProcessInfo processInfo] systemUptime];
    }
    _dragParam = 0;
    _dragView = NO;
}

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
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), 900u, 792u)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false;
    }
    return true;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p || !p->guiView) return;
    [static_cast<S3GAmbiTerrainNavigatorView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, 900u, 792u, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, 900u, 792u, w, h); }
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
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; [static_cast<S3GAmbiTerrainNavigatorView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<S3GAmbiTerrainNavigatorView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    "s3g Ambi Encoder Surface Terrain",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.1-pre",
    "64-point ambisonic surface-terrain encoder whose AED sources traverse a deformable procedural shell.",
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
    publishControlSnapshot(*p);
    publishPoints(*p);
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
