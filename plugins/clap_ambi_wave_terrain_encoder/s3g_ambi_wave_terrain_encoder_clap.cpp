#include "s3g_ambi_wave_terrain_encoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
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
#include <iterator>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiWaveTerrainMaxChannels;
constexpr uint32_t kStateVersion = 5u;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kVoicesParamId = 2;
constexpr clap_id kModeParamId = 3;
constexpr clap_id kBaseNoteParamId = 4;
constexpr clap_id kPitchSpreadParamId = 5;
constexpr clap_id kTuneParamId = 6;
constexpr clap_id kDetuneParamId = 7;
constexpr clap_id kSkinParamId = 8;
constexpr clap_id kTerrainDepthParamId = 9;
constexpr clap_id kTerrainRoughnessParamId = 10;
constexpr clap_id kTerrainFoldParamId = 11;
constexpr clap_id kTerrainReliefParamId = 12;
constexpr clap_id kTraceParamId = 13;
constexpr clap_id kInterpretationParamId = 14;
constexpr clap_id kInterpretationMixParamId = 15;
constexpr clap_id kScanRadiusParamId = 16;
constexpr clap_id kScanAspectParamId = 17;
constexpr clap_id kScanRotationParamId = 18;
constexpr clap_id kScanWarpParamId = 19;
constexpr clap_id kSelectionParamId = 20;
constexpr clap_id kTransitionParamId = 21;
constexpr clap_id kFieldDensityParamId = 22;
constexpr clap_id kFieldDurationParamId = 23;
constexpr clap_id kFieldRestParamId = 24;
constexpr clap_id kFieldContrastParamId = 25;
constexpr clap_id kSelectionMemoryParamId = 26;
constexpr clap_id kRegionDeviationParamId = 27;
constexpr clap_id kNeighborTransferParamId = 28;
constexpr clap_id kMacroDurationParamId = 29;
constexpr clap_id kTableXfadeParamId = 30;
constexpr clap_id kAttackParamId = 31;
constexpr clap_id kDecayParamId = 32;
constexpr clap_id kSustainParamId = 33;
constexpr clap_id kReleaseParamId = 34;
constexpr clap_id kAzimuthParamId = 35;
constexpr clap_id kElevationParamId = 36;
constexpr clap_id kDistanceParamId = 37;
constexpr clap_id kSpatialSpreadParamId = 38;
constexpr clap_id kSpatialFollowParamId = 39;
constexpr clap_id kOutputParamId = 40;
constexpr clap_id kPitchModeParamId = 41;
constexpr clap_id kMotionModeParamId = 42;
constexpr clap_id kAzimuthRateParamId = 43;
constexpr clap_id kElevationRateParamId = 44;
constexpr clap_id kRotationDeviationParamId = 45;
constexpr clap_id kPitchScaleParamId = 46;
constexpr clap_id kTerrainFormParamId = 47;
constexpr clap_id kTerrainFacetParamId = 48;
constexpr clap_id kTerrainBevelParamId = 49;
constexpr clap_id kTerrainOrientationParamId = 50;
constexpr clap_id kTerrainTerraceParamId = 51;
constexpr clap_id kTerrainTerraceStepsParamId = 52;
constexpr clap_id kTerrainRidgeParamId = 53;
constexpr clap_id kTerrainErosionParamId = 54;
constexpr clap_id kTerrainDomainWarpParamId = 55;
constexpr clap_id kTerrainTwistParamId = 56;
constexpr clap_id kPolygonSidesParamId = 57;
constexpr clap_id kPolygonRoundParamId = 58;
constexpr clap_id kPolygonStarParamId = 59;
constexpr clap_id kPolygonSkewParamId = 60;
constexpr clap_id kFieldListenModeParamId = 61;

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 12.0, true },
    { kModeParamId, "Mode", 0.0, 2.0, 0.0, true },
    { kBaseNoteParamId, "Base Note", 12.0, 96.0, 40.0, false },
    { kPitchSpreadParamId, "Pitch Spread", 0.0, 48.0, 19.0, false },
    { kTuneParamId, "Tune", -1200.0, 1200.0, 0.0, false },
    { kDetuneParamId, "Detune", 0.0, 100.0, 9.0, false },
    { kSkinParamId, "Terrain Skin", 0.0, 7.0, 1.0, true },
    { kTerrainDepthParamId, "Terrain Depth", 0.0, 1.0, 0.82, false },
    { kTerrainRoughnessParamId, "Terrain Roughness", 0.0, 1.0, 0.58, false },
    { kTerrainFoldParamId, "Terrain Fold", 0.0, 1.0, 0.24, false },
    { kTerrainReliefParamId, "Terrain Relief", 0.0, 1.0, 0.62, false },
    { kTraceParamId, "Scan Trace", 0.0, 4.0, 1.0, true },
    { kInterpretationParamId, "Interpretation", 0.0, 9.0, 0.0, true },
    { kInterpretationMixParamId, "Interpretation Mix", 0.0, 1.0, 0.32, false },
    { kScanRadiusParamId, "Scan Radius", 0.005, 0.48, 0.16, false },
    { kScanAspectParamId, "Scan Aspect", 0.05, 1.0, 0.68, false },
    { kScanRotationParamId, "Scan Rotation", -1.0, 1.0, 0.0, false },
    { kScanWarpParamId, "Scan Warp", 0.0, 1.0, 0.18, false },
    { kSelectionParamId, "Selection Law", 0.0, 5.0, 5.0, true },
    { kTransitionParamId, "Transition", 0.0, 2.0, 0.0, true },
    { kFieldDensityParamId, "Field Density", 0.0, 1.0, 0.82, false },
    { kFieldDurationParamId, "Field Duration", 0.05, 30.0, 1.8, false },
    { kFieldRestParamId, "Field Rest", 0.02, 8.0, 0.24, false },
    { kFieldContrastParamId, "Field Contrast", 0.0, 1.0, 0.72, false },
    { kSelectionMemoryParamId, "Selection Memory", 0.0, 1.0, 0.74, false },
    { kRegionDeviationParamId, "Region Deviation", 0.0, 1.0, 0.36, false },
    { kNeighborTransferParamId, "Neighbor Transfer", 0.0, 1.0, 0.28, false },
    { kMacroDurationParamId, "Macro Duration", 2.0, 300.0, 24.0, false },
    { kTableXfadeParamId, "Table Crossfade", 5.0, 5000.0, 180.0, false },
    { kAttackParamId, "Attack", 1.0, 4000.0, 12.0, false },
    { kDecayParamId, "Decay", 5.0, 8000.0, 180.0, false },
    { kSustainParamId, "Sustain", 0.0, 1.0, 0.76, false },
    { kReleaseParamId, "Release", 5.0, 12000.0, 420.0, false },
    { kAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Center Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Center Distance", 0.15, 3.0, 1.0, false },
    { kSpatialSpreadParamId, "Spatial Spread", 0.0, 1.0, 0.82, false },
    { kSpatialFollowParamId, "Spatial Follow", 0.0, 0.995, 0.90, false },
    { kOutputParamId, "Output", -60.0, 12.0, -22.0, false },
    { kPitchModeParamId, "Pitch Mode", 0.0, 1.0, 0.0, true },
    { kMotionModeParamId, "Motion Mode", 0.0, 1.0, 1.0, true },
    { kAzimuthRateParamId, "Azimuth Rotation Rate", -12.0, 12.0, 0.70, false },
    { kElevationRateParamId, "Elevation Rotation Rate", -12.0, 12.0, 0.43, false },
    { kRotationDeviationParamId, "Rotation Rate Deviation", 0.0, 1.0, 0.28, false },
    { kPitchScaleParamId, "Pitch Scale", 0.0, 6.0, 0.0, true },
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
    { kPolygonSidesParamId, "Polygon Sides", 3.0, 12.0, 6.0, true },
    { kPolygonRoundParamId, "Polygon Round", 0.0, 1.0, 0.18, false },
    { kPolygonStarParamId, "Polygon Star", 0.0, 1.0, 0.0, false },
    { kPolygonSkewParamId, "Polygon Skew", -1.0, 1.0, 0.0, false },
    { kFieldListenModeParamId, "Field Listener", 0.0, 3.0, 0.0, true },
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiWaveTerrainEncoder engine {};
    s3g::AmbiWaveTerrainParams params {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> lastMidiNote { 0u };
    std::atomic<uint32_t> guiSelectedVoice { 0u };
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiFrequency {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiTransition {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiPointAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiPointElevation {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiPointDistance {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiCurrentRegionU {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiCurrentRegionV {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiCurrentRadius {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiCurrentAspect {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiCurrentRotation {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWaveTerrainMaxVoices> guiCurrentTrace {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiNextRegionU {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiNextRegionV {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiNextRadius {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiNextAspect {};
    std::array<std::atomic<float>, s3g::kAmbiWaveTerrainMaxVoices> guiNextRotation {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWaveTerrainMaxVoices> guiNextTrace {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiWaveTerrainMaxVoices> guiFieldActive {};
    std::array<std::atomic<float>, 256> guiTerrainA {};
    std::array<std::atomic<float>, 256> guiTerrainB {};
    std::array<std::atomic<float>, 256> guiTableA {};
    std::array<std::atomic<float>, 256> guiTableB {};
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
        const int64_t count = stream->write(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t count = stream->read(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

const ParamDef* paramDef(clap_id id)
{
    for (const auto& def : kParams) if (def.id == id) return &def;
    return nullptr;
}

void applyParam(Plugin& p, clap_id id, double value)
{
    auto& v = p.params;
    switch (id) {
    case kOrderParamId: v.order = static_cast<uint32_t>(std::lround(value)); break;
    case kVoicesParamId: v.voices = static_cast<uint32_t>(std::lround(value)); break;
    case kModeParamId: v.mode = static_cast<s3g::AmbiWaveTerrainMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kBaseNoteParamId: v.baseNote = static_cast<float>(value); break;
    case kPitchSpreadParamId: v.pitchSpreadSemitones = static_cast<float>(value); break;
    case kTuneParamId: v.tuneCents = static_cast<float>(value); break;
    case kDetuneParamId: v.detuneCents = static_cast<float>(value); break;
    case kSkinParamId: v.skin = static_cast<s3g::AmbiWaveTerrainSkin>(static_cast<uint32_t>(std::lround(value))); break;
    case kTerrainDepthParamId: v.terrainDepth = static_cast<float>(value); break;
    case kTerrainRoughnessParamId: v.terrainRoughness = static_cast<float>(value); break;
    case kTerrainFoldParamId: v.terrainFold = static_cast<float>(value); break;
    case kTerrainReliefParamId: v.terrainRelief = static_cast<float>(value); break;
    case kTraceParamId: v.trace = static_cast<s3g::AmbiWaveTerrainTrace>(static_cast<uint32_t>(std::lround(value))); break;
    case kInterpretationParamId: v.interpretation = static_cast<s3g::AmbiWaveTerrainInterpretation>(static_cast<uint32_t>(std::lround(value))); break;
    case kInterpretationMixParamId: v.interpretationMix = static_cast<float>(value); break;
    case kScanRadiusParamId: v.scanRadius = static_cast<float>(value); break;
    case kScanAspectParamId: v.scanAspect = static_cast<float>(value); break;
    case kScanRotationParamId: v.scanRotation = static_cast<float>(value); break;
    case kScanWarpParamId: v.scanWarp = static_cast<float>(value); break;
    case kSelectionParamId: v.selection = static_cast<s3g::AmbiWaveTerrainSelection>(static_cast<uint32_t>(std::lround(value))); break;
    case kTransitionParamId: v.transition = static_cast<s3g::AmbiWaveTerrainTransition>(static_cast<uint32_t>(std::lround(value))); break;
    case kFieldDensityParamId: v.fieldDensity = static_cast<float>(value); break;
    case kFieldDurationParamId: v.fieldDurationSeconds = static_cast<float>(value); break;
    case kFieldRestParamId: v.fieldRestSeconds = static_cast<float>(value); break;
    case kFieldContrastParamId: v.fieldContrast = static_cast<float>(value); break;
    case kSelectionMemoryParamId: v.selectionMemory = static_cast<float>(value); break;
    case kRegionDeviationParamId: v.regionDeviation = static_cast<float>(value); break;
    case kNeighborTransferParamId: v.neighborTransfer = static_cast<float>(value); break;
    case kMacroDurationParamId: v.macroDurationSeconds = static_cast<float>(value); break;
    case kTableXfadeParamId: v.tableXfadeMs = static_cast<float>(value); break;
    case kAttackParamId: v.attackMs = static_cast<float>(value); break;
    case kDecayParamId: v.decayMs = static_cast<float>(value); break;
    case kSustainParamId: v.sustain = static_cast<float>(value); break;
    case kReleaseParamId: v.releaseMs = static_cast<float>(value); break;
    case kAzimuthParamId: v.centerAzimuthDeg = static_cast<float>(value); break;
    case kElevationParamId: v.centerElevationDeg = static_cast<float>(value); break;
    case kDistanceParamId: v.centerDistance = static_cast<float>(value); break;
    case kSpatialSpreadParamId: v.spatialSpread = static_cast<float>(value); break;
    case kSpatialFollowParamId: v.spatialFollow = static_cast<float>(value); break;
    case kOutputParamId: v.outputGainDb = static_cast<float>(value); break;
    case kPitchModeParamId: v.pitchMode = static_cast<s3g::AmbiWaveTerrainPitchMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kMotionModeParamId: v.motionMode = static_cast<s3g::AmbiWaveTerrainMotionMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kAzimuthRateParamId: v.azimuthRateRpm = static_cast<float>(value); break;
    case kElevationRateParamId: v.elevationRateRpm = static_cast<float>(value); break;
    case kRotationDeviationParamId: v.rotationRateDeviation = static_cast<float>(value); break;
    case kPitchScaleParamId: v.pitchScale = static_cast<s3g::AmbiWaveTerrainPitchScale>(static_cast<uint32_t>(std::lround(value))); break;
    case kTerrainFormParamId: v.terrainForm = static_cast<s3g::AmbiWaveTerrainForm>(static_cast<uint32_t>(std::lround(value))); break;
    case kTerrainFacetParamId: v.terrainFacet = static_cast<float>(value); break;
    case kTerrainBevelParamId: v.terrainBevel = static_cast<float>(value); break;
    case kTerrainOrientationParamId: v.terrainOrientation = static_cast<float>(value); break;
    case kTerrainTerraceParamId: v.terrainTerrace = static_cast<float>(value); break;
    case kTerrainTerraceStepsParamId: v.terrainTerraceSteps = static_cast<uint32_t>(std::lround(value)); break;
    case kTerrainRidgeParamId: v.terrainRidge = static_cast<float>(value); break;
    case kTerrainErosionParamId: v.terrainErosion = static_cast<float>(value); break;
    case kTerrainDomainWarpParamId: v.terrainDomainWarp = static_cast<float>(value); break;
    case kTerrainTwistParamId: v.terrainTwist = static_cast<float>(value); break;
    case kPolygonSidesParamId: v.polygonSides = static_cast<uint32_t>(std::lround(value)); break;
    case kPolygonRoundParamId: v.polygonRound = static_cast<float>(value); break;
    case kPolygonStarParamId: v.polygonStar = static_cast<float>(value); break;
    case kPolygonSkewParamId: v.polygonSkew = static_cast<float>(value); break;
    case kFieldListenModeParamId: v.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
        static_cast<uint32_t>(std::lround(value))); break;
    default: break;
    }
    p.engine.setParams(v);
    p.params = p.engine.params();
}

void snapshotGui(Plugin& p)
{
    const auto& active = p.engine.fieldActive();
    const auto& points = p.engine.points();
    for (uint32_t voice = 0; voice < s3g::kAmbiWaveTerrainMaxVoices; ++voice) {
        p.guiEnergy[voice].store(p.engine.voiceEnergy(voice), std::memory_order_relaxed);
        p.guiFrequency[voice].store(p.engine.voiceFrequencyHz(voice), std::memory_order_relaxed);
        p.guiTransition[voice].store(p.engine.voiceTransition(voice), std::memory_order_relaxed);
        p.guiPointAzimuth[voice].store(points[voice].azimuthDeg, std::memory_order_relaxed);
        p.guiPointElevation[voice].store(points[voice].elevationDeg, std::memory_order_relaxed);
        p.guiPointDistance[voice].store(points[voice].distance, std::memory_order_relaxed);
        const auto current = p.engine.voiceRegion(voice, false);
        p.guiCurrentRegionU[voice].store(current.u, std::memory_order_relaxed);
        p.guiCurrentRegionV[voice].store(current.v, std::memory_order_relaxed);
        p.guiCurrentRadius[voice].store(current.radius, std::memory_order_relaxed);
        p.guiCurrentAspect[voice].store(current.aspect, std::memory_order_relaxed);
        p.guiCurrentRotation[voice].store(current.rotation, std::memory_order_relaxed);
        p.guiCurrentTrace[voice].store(static_cast<uint32_t>(current.trace), std::memory_order_relaxed);
        const auto next = p.engine.voiceRegion(voice, true);
        p.guiNextRegionU[voice].store(next.u, std::memory_order_relaxed);
        p.guiNextRegionV[voice].store(next.v, std::memory_order_relaxed);
        p.guiNextRadius[voice].store(next.radius, std::memory_order_relaxed);
        p.guiNextAspect[voice].store(next.aspect, std::memory_order_relaxed);
        p.guiNextRotation[voice].store(next.rotation, std::memory_order_relaxed);
        p.guiNextTrace[voice].store(static_cast<uint32_t>(next.trace), std::memory_order_relaxed);
        p.guiFieldActive[voice].store(active[voice], std::memory_order_relaxed);
    }
    const uint32_t selected = std::min<uint32_t>(p.guiSelectedVoice.load(std::memory_order_relaxed), s3g::kAmbiWaveTerrainMaxVoices - 1u);
    for (uint32_t index = 0; index < 256u; ++index) {
        p.guiTerrainA[index].store(p.engine.terrainProfileValue(selected, false, index * 2u), std::memory_order_relaxed);
        p.guiTerrainB[index].store(p.engine.terrainProfileValue(selected, true, index * 2u), std::memory_order_relaxed);
        p.guiTableA[index].store(p.engine.tableValue(selected, false, index * 2u), std::memory_order_relaxed);
        p.guiTableB[index].store(p.engine.tableValue(selected, true, index * 2u), std::memory_order_relaxed);
    }
}

void readEvents(Plugin& p, const clap_input_events_t* events)
{
    if (!events) return;
    for (uint32_t index = 0; index < events->size(events); ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(p, param->param_id, param->value);
        } else if (event->type == CLAP_EVENT_NOTE_ON || event->type == CLAP_EVENT_NOTE_OFF || event->type == CLAP_EVENT_NOTE_CHOKE) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
            if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
                p.engine.noteOn(note->key, static_cast<float>(note->velocity));
                p.lastMidiNote.store(static_cast<uint32_t>(note->key), std::memory_order_relaxed);
            } else p.engine.noteOff(note->key);
        }
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
bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
    snapshotGui(*p);
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { auto* p = self(plugin); p->engine.reset(); p->outputPeak.store(0.0f); snapshotGui(*p); }

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* data)
{
    auto* p = self(plugin);
    if (!data) return CLAP_PROCESS_CONTINUE;
    readEvents(*p, data->in_events);
    if (!data->audio_outputs || data->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    auto& output = data->audio_outputs[0];
    if (!output.data32) return CLAP_PROCESS_CONTINUE;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    std::array<float*, kOutputChannels> outputs {};
    for (uint32_t channel = 0; channel < channels; ++channel) outputs[channel] = output.data32[channel];
    p->engine.process(outputs.data(), channels, data->frames_count);
    s3g::clearAudioBufferFromChannel(output, channels, data->frames_count);
    float peak = 0.0f;
    for (uint32_t channel = 0; channel < channels; ++channel) {
        if (!output.data32[channel]) continue;
        for (uint32_t frame = 0; frame < data->frames_count; ++frame) peak = std::max(peak, std::fabs(output.data32[channel][frame]));
    }
    p->outputPeak.store(std::max(peak, p->outputPeak.load(std::memory_order_relaxed) * 0.92f), std::memory_order_relaxed);
    snapshotGui(*p);
    return CLAP_PROCESS_CONTINUE;
}
void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }
bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    info->id = 30;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI In", sizeof(info->name));
    return true;
}
const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(kParams)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    info->min_value = def.min; info->max_value = def.max; info->default_value = def.def;
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "Ambi Wave Terrain Encoder");
    return true;
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto p = self(plugin)->params;
    switch (id) {
    case kOrderParamId: *value = p.order; break;
    case kVoicesParamId: *value = p.voices; break;
    case kModeParamId: *value = static_cast<uint32_t>(p.mode); break;
    case kBaseNoteParamId: *value = p.baseNote; break;
    case kPitchSpreadParamId: *value = p.pitchSpreadSemitones; break;
    case kTuneParamId: *value = p.tuneCents; break;
    case kDetuneParamId: *value = p.detuneCents; break;
    case kSkinParamId: *value = static_cast<uint32_t>(p.skin); break;
    case kTerrainDepthParamId: *value = p.terrainDepth; break;
    case kTerrainRoughnessParamId: *value = p.terrainRoughness; break;
    case kTerrainFoldParamId: *value = p.terrainFold; break;
    case kTerrainReliefParamId: *value = p.terrainRelief; break;
    case kTraceParamId: *value = static_cast<uint32_t>(p.trace); break;
    case kInterpretationParamId: *value = static_cast<uint32_t>(p.interpretation); break;
    case kInterpretationMixParamId: *value = p.interpretationMix; break;
    case kScanRadiusParamId: *value = p.scanRadius; break;
    case kScanAspectParamId: *value = p.scanAspect; break;
    case kScanRotationParamId: *value = p.scanRotation; break;
    case kScanWarpParamId: *value = p.scanWarp; break;
    case kSelectionParamId: *value = static_cast<uint32_t>(p.selection); break;
    case kTransitionParamId: *value = static_cast<uint32_t>(p.transition); break;
    case kFieldDensityParamId: *value = p.fieldDensity; break;
    case kFieldDurationParamId: *value = p.fieldDurationSeconds; break;
    case kFieldRestParamId: *value = p.fieldRestSeconds; break;
    case kFieldContrastParamId: *value = p.fieldContrast; break;
    case kSelectionMemoryParamId: *value = p.selectionMemory; break;
    case kRegionDeviationParamId: *value = p.regionDeviation; break;
    case kNeighborTransferParamId: *value = p.neighborTransfer; break;
    case kMacroDurationParamId: *value = p.macroDurationSeconds; break;
    case kTableXfadeParamId: *value = p.tableXfadeMs; break;
    case kAttackParamId: *value = p.attackMs; break;
    case kDecayParamId: *value = p.decayMs; break;
    case kSustainParamId: *value = p.sustain; break;
    case kReleaseParamId: *value = p.releaseMs; break;
    case kAzimuthParamId: *value = p.centerAzimuthDeg; break;
    case kElevationParamId: *value = p.centerElevationDeg; break;
    case kDistanceParamId: *value = p.centerDistance; break;
    case kSpatialSpreadParamId: *value = p.spatialSpread; break;
    case kSpatialFollowParamId: *value = p.spatialFollow; break;
    case kOutputParamId: *value = p.outputGainDb; break;
    case kPitchModeParamId: *value = static_cast<uint32_t>(p.pitchMode); break;
    case kMotionModeParamId: *value = static_cast<uint32_t>(p.motionMode); break;
    case kAzimuthRateParamId: *value = p.azimuthRateRpm; break;
    case kElevationRateParamId: *value = p.elevationRateRpm; break;
    case kRotationDeviationParamId: *value = p.rotationRateDeviation; break;
    case kPitchScaleParamId: *value = static_cast<uint32_t>(p.pitchScale); break;
    case kTerrainFormParamId: *value = static_cast<uint32_t>(p.terrainForm); break;
    case kTerrainFacetParamId: *value = p.terrainFacet; break;
    case kTerrainBevelParamId: *value = p.terrainBevel; break;
    case kTerrainOrientationParamId: *value = p.terrainOrientation; break;
    case kTerrainTerraceParamId: *value = p.terrainTerrace; break;
    case kTerrainTerraceStepsParamId: *value = p.terrainTerraceSteps; break;
    case kTerrainRidgeParamId: *value = p.terrainRidge; break;
    case kTerrainErosionParamId: *value = p.terrainErosion; break;
    case kTerrainDomainWarpParamId: *value = p.terrainDomainWarp; break;
    case kTerrainTwistParamId: *value = p.terrainTwist; break;
    case kPolygonSidesParamId: *value = p.polygonSides; break;
    case kPolygonRoundParamId: *value = p.polygonRound; break;
    case kPolygonStarParamId: *value = p.polygonStar; break;
    case kPolygonSkewParamId: *value = p.polygonSkew; break;
    case kFieldListenModeParamId: *value = static_cast<uint32_t>(p.fieldListenMode); break;
    default: return false;
    }
    return true;
}
bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kModeParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainModeName(static_cast<s3g::AmbiWaveTerrainMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kTerrainFormParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainFormName(static_cast<s3g::AmbiWaveTerrainForm>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kSkinParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainSkinName(static_cast<s3g::AmbiWaveTerrainSkin>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kTraceParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainTraceName(static_cast<s3g::AmbiWaveTerrainTrace>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kInterpretationParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainInterpretationName(static_cast<s3g::AmbiWaveTerrainInterpretation>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kSelectionParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainSelectionName(static_cast<s3g::AmbiWaveTerrainSelection>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kTransitionParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainTransitionName(static_cast<s3g::AmbiWaveTerrainTransition>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kPitchModeParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainPitchModeName(static_cast<s3g::AmbiWaveTerrainPitchMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kPitchScaleParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainPitchScaleName(static_cast<s3g::AmbiWaveTerrainPitchScale>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kMotionModeParamId) std::snprintf(display, size, "%s", s3g::ambiWaveTerrainMotionModeName(static_cast<s3g::AmbiWaveTerrainMotionMode>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kFieldListenModeParamId) {
        static constexpr const char* names[] { "OFF", "FOLLOW", "COUNTER", "BALANCE" };
        std::snprintf(display, size, "%s", names[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 3u)]);
    }
    else if (id == kAzimuthRateParamId || id == kElevationRateParamId) std::snprintf(display, size, "%+.2f rpm", value);
    else if (id == kTerrainOrientationParamId || id == kTerrainTwistParamId) std::snprintf(display, size, "%+.0f deg", value * 180.0);
    else if (id == kPolygonSkewParamId) std::snprintf(display, size, "%+.0f%%", value * 100.0);
    else if (id == kTerrainTerraceStepsParamId || id == kPolygonSidesParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kTuneParamId || id == kDetuneParamId) std::snprintf(display, size, "%+.0f ct", value);
    else if (id == kAzimuthParamId || id == kElevationParamId) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId || id == kTableXfadeParamId) std::snprintf(display, size, "%.0f ms", value);
    else if (id == kFieldDurationParamId || id == kFieldRestParamId || id == kMacroDurationParamId) std::snprintf(display, size, "%.2f s", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTerrainDepthParamId || id == kTerrainRoughnessParamId || id == kTerrainFoldParamId || id == kTerrainReliefParamId
        || id == kInterpretationMixParamId || id == kScanAspectParamId || id == kScanWarpParamId || id == kFieldDensityParamId
        || id == kFieldContrastParamId || id == kSelectionMemoryParamId || id == kRegionDeviationParamId || id == kNeighborTransferParamId
        || id == kSustainParamId || id == kSpatialSpreadParamId || id == kSpatialFollowParamId
        || id == kRotationDeviationParamId || id == kTerrainFacetParamId || id == kTerrainBevelParamId
        || id == kTerrainTerraceParamId || id == kTerrainRidgeParamId || id == kTerrainErosionParamId
        || id == kTerrainDomainWarpParamId || id == kPolygonRoundParamId || id == kPolygonStarParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    else std::snprintf(display, size, "%.2f", value);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text, double* value)
{
    if (!text || !value) return false;
    static constexpr const char* mode[] { "FREE", "MIDI", "BOTH" };
    static constexpr const char* form[] { "SPHERE", "TETRA", "CUBE", "OCTA", "DODECA", "ICOSA" };
    static constexpr const char* skin[] { "HARMONIC", "FBM", "CELL", "VOT", "RIDGES", "DUNES", "CRATERS", "TECTONIC" };
    static constexpr const char* trace[] { "ORBIT", "LISSAJOUS", "ROSETTE", "FOLD", "POLYGON" };
    static constexpr const char* interpretation[] { "HEIGHT", "EDGE", "CURVE", "BLEND", "GRADIENT", "RIDGE", "VALLEY", "NORMAL", "CROSS", "VECTOR" };
    static constexpr const char* selection[] { "RANDOM", "SERIES", "WEIGHT", "TENDENCY", "MARKOV", "WALK" };
    static constexpr const char* transition[] { "LINK", "MERGE", "VARY" };
    static constexpr const char* pitch[] { "NOTE", "TRAVEL" };
    static constexpr const char* scale[] { "FREE", "CHROM", "MAJOR", "MINOR", "PENTA", "WHOLE", "HARM MIN" };
    static constexpr const char* motion[] { "FIELD", "ROTATE" };
    static constexpr const char* listener[] { "OFF", "FOLLOW", "COUNTER", "BALANCE" };

    const char* const* names = nullptr;
    uint32_t count = 0u;
    if (id == kModeParamId) { names = mode; count = static_cast<uint32_t>(std::size(mode)); }
    else if (id == kTerrainFormParamId) { names = form; count = static_cast<uint32_t>(std::size(form)); }
    else if (id == kSkinParamId) { names = skin; count = static_cast<uint32_t>(std::size(skin)); }
    else if (id == kTraceParamId) { names = trace; count = static_cast<uint32_t>(std::size(trace)); }
    else if (id == kInterpretationParamId) { names = interpretation; count = static_cast<uint32_t>(std::size(interpretation)); }
    else if (id == kSelectionParamId) { names = selection; count = static_cast<uint32_t>(std::size(selection)); }
    else if (id == kTransitionParamId) { names = transition; count = static_cast<uint32_t>(std::size(transition)); }
    else if (id == kPitchModeParamId) { names = pitch; count = static_cast<uint32_t>(std::size(pitch)); }
    else if (id == kPitchScaleParamId) { names = scale; count = static_cast<uint32_t>(std::size(scale)); }
    else if (id == kMotionModeParamId) { names = motion; count = static_cast<uint32_t>(std::size(motion)); }
    else if (id == kFieldListenModeParamId) { names = listener; count = static_cast<uint32_t>(std::size(listener)); }
    if (names) {
        for (uint32_t index = 0u; index < count; ++index) {
            if (std::strcmp(text, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }

    *value = std::atof(text);
    const bool percent = id == kTerrainDepthParamId || id == kTerrainRoughnessParamId
        || id == kTerrainFoldParamId || id == kTerrainReliefParamId || id == kInterpretationMixParamId
        || id == kScanAspectParamId || id == kScanWarpParamId || id == kFieldDensityParamId
        || id == kFieldContrastParamId || id == kSelectionMemoryParamId || id == kRegionDeviationParamId
        || id == kNeighborTransferParamId || id == kSustainParamId || id == kSpatialSpreadParamId
        || id == kSpatialFollowParamId || id == kRotationDeviationParamId || id == kTerrainFacetParamId
        || id == kTerrainBevelParamId || id == kTerrainTerraceParamId || id == kTerrainRidgeParamId
        || id == kTerrainErosionParamId || id == kTerrainDomainWarpParamId || id == kPolygonRoundParamId
        || id == kPolygonStarParamId || id == kPolygonSkewParamId;
    if (percent) *value *= 0.01;
    if (id == kTerrainOrientationParamId || id == kTerrainTwistParamId) *value /= 180.0;
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const uint32_t version = kStateVersion;
    const auto params = self(plugin)->params;
    return writeExact(stream, &version, sizeof(version)) && writeExact(stream, &params, sizeof(params));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    s3g::AmbiWaveTerrainParams loaded {};
    if (version == 1u) {
        loaded.pitchMode = s3g::AmbiWaveTerrainPitchMode::Note;
        loaded.motionMode = s3g::AmbiWaveTerrainMotionMode::Field;
        constexpr size_t legacyBytes = offsetof(s3g::AmbiWaveTerrainParams, pitchMode);
        if (!readExact(stream, &loaded, legacyBytes)) return false;
    } else if (version == 2u) {
        constexpr size_t legacyBytes = offsetof(s3g::AmbiWaveTerrainParams, pitchScale);
        if (!readExact(stream, &loaded, legacyBytes)) return false;
    } else if (version == 3u) {
        constexpr size_t legacyBytes = offsetof(s3g::AmbiWaveTerrainParams, terrainForm);
        if (!readExact(stream, &loaded, legacyBytes)) return false;
    } else if (version == 4u) {
        constexpr size_t legacyBytes = offsetof(s3g::AmbiWaveTerrainParams, fieldListenMode);
        if (!readExact(stream, &loaded, legacyBytes)) return false;
    } else if (version == kStateVersion) {
        if (!readExact(stream, &loaded, sizeof(loaded))) return false;
    } else return false;
    auto* p = self(plugin);
    p->params = loaded;
    p->engine.setParams(p->params);
    p->params = p->engine.params();
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {

struct GuiRow { const char* label; clap_id param; CGFloat panelX; CGFloat y; bool menu; };
constexpr GuiRow kGuiRows[] {
    { "MODE", kModeParamId, 630, 78, true }, { "ORDER", kOrderParamId, 630, 104, true },
    { "VOICES", kVoicesParamId, 630, 130, false }, { "BASE", kBaseNoteParamId, 630, 156, false },
    { "SPREAD", kPitchSpreadParamId, 630, 182, false }, { "TUNE", kTuneParamId, 630, 208, false },
    { "DETUNE", kDetuneParamId, 630, 234, false },
    { "FORM", kTerrainFormParamId, 630, 318, true }, { "FACET", kTerrainFacetParamId, 630, 344, false },
    { "BEVEL", kTerrainBevelParamId, 630, 370, false }, { "ORIENT", kTerrainOrientationParamId, 630, 396, false },
    { "SKIN", kSkinParamId, 630, 318, true }, { "DEPTH", kTerrainDepthParamId, 630, 344, false },
    { "ROUGH", kTerrainRoughnessParamId, 630, 370, false }, { "FOLD", kTerrainFoldParamId, 630, 396, false },
    { "RELIEF", kTerrainReliefParamId, 630, 422, false },
    { "TERRACE", kTerrainTerraceParamId, 630, 318, false }, { "STEPS", kTerrainTerraceStepsParamId, 630, 344, false },
    { "RIDGE", kTerrainRidgeParamId, 630, 370, false }, { "ERODE", kTerrainErosionParamId, 630, 396, false },
    { "DOMAIN", kTerrainDomainWarpParamId, 630, 422, false }, { "TWIST", kTerrainTwistParamId, 630, 448, false },
    { "READ", kInterpretationParamId, 630, 318, true }, { "MIX", kInterpretationMixParamId, 630, 344, false },
    { "ATTACK", kAttackParamId, 630, 544, false }, { "DECAY", kDecayParamId, 630, 570, false },
    { "SUSTAIN", kSustainParamId, 630, 596, false }, { "RELEASE", kReleaseParamId, 630, 622, false },
    { "PITCH", kPitchModeParamId, 630, 706, true }, { "SCALE", kPitchScaleParamId, 630, 732, true },
    { "OUT", kOutputParamId, 630, 758, false }, { "LISTEN", kFieldListenModeParamId, 630, 784, true },
    { "TRACE", kTraceParamId, 896, 78, true }, { "RADIUS", kScanRadiusParamId, 896, 104, false },
    { "ASPECT", kScanAspectParamId, 896, 130, false }, { "ROTATE", kScanRotationParamId, 896, 156, false },
    { "WARP", kScanWarpParamId, 896, 182, false }, { "XFADE", kTableXfadeParamId, 896, 234, false },
    { "SIDES", kPolygonSidesParamId, 896, 130, false }, { "ROUND", kPolygonRoundParamId, 896, 156, false },
    { "STAR", kPolygonStarParamId, 896, 182, false }, { "SKEW", kPolygonSkewParamId, 896, 208, false },
    { "LAW", kSelectionParamId, 896, 318, true }, { "JOIN", kTransitionParamId, 896, 344, true },
    { "XFER", kNeighborTransferParamId, 896, 370, false }, { "MEM", kSelectionMemoryParamId, 896, 396, false },
    { "MOTION", kMotionModeParamId, 896, 454, true },
    { "DENS", kFieldDensityParamId, 896, 480, false }, { "DUR", kFieldDurationParamId, 896, 506, false },
    { "REST", kFieldRestParamId, 896, 532, false }, { "CONT", kFieldContrastParamId, 896, 558, false },
    { "DEV", kRegionDeviationParamId, 896, 584, false }, { "MACRO", kMacroDurationParamId, 896, 610, false },
    { "AZ RATE", kAzimuthRateParamId, 896, 480, false }, { "EL RATE", kElevationRateParamId, 896, 506, false },
    { "RATE DEV", kRotationDeviationParamId, 896, 532, false },
    { "AZIM", kAzimuthParamId, 896, 694, false }, { "ELEV", kElevationParamId, 896, 720, false },
    { "DIST", kDistanceParamId, 896, 746, false }, { "SPACE", kSpatialSpreadParamId, 896, 772, false },
    { "FOLLOW", kSpatialFollowParamId, 896, 798, false },
};

const GuiRow* guiRow(clap_id param)
{
    for (const auto& row : kGuiRows) if (row.param == param) return &row;
    return nullptr;
}

int terrainPageForParam(clap_id param)
{
    if (param == kTerrainFormParamId || param == kTerrainFacetParamId
        || param == kTerrainBevelParamId || param == kTerrainOrientationParamId) return 0;
    if (param == kSkinParamId || param == kTerrainDepthParamId || param == kTerrainRoughnessParamId
        || param == kTerrainFoldParamId || param == kTerrainReliefParamId) return 1;
    if (param == kTerrainTerraceParamId || param == kTerrainTerraceStepsParamId || param == kTerrainRidgeParamId
        || param == kTerrainErosionParamId || param == kTerrainDomainWarpParamId || param == kTerrainTwistParamId) return 2;
    if (param == kInterpretationParamId || param == kInterpretationMixParamId) return 3;
    return -1;
}

bool guiRowVisible(const GuiRow& row, s3g::AmbiWaveTerrainMotionMode motion, int terrainPage,
                   s3g::AmbiWaveTerrainTrace trace)
{
    const bool fieldOnly = row.param == kFieldDensityParamId || row.param == kFieldDurationParamId
        || row.param == kFieldRestParamId || row.param == kFieldContrastParamId
        || row.param == kRegionDeviationParamId || row.param == kMacroDurationParamId
        || row.param == kSelectionParamId || row.param == kTransitionParamId
        || row.param == kNeighborTransferParamId || row.param == kSelectionMemoryParamId;
    const bool rotateOnly = row.param == kAzimuthRateParamId || row.param == kElevationRateParamId
        || row.param == kRotationDeviationParamId;
    const int page = terrainPageForParam(row.param);
    const bool polygonOnly = row.param == kPolygonSidesParamId || row.param == kPolygonRoundParamId
        || row.param == kPolygonStarParamId || row.param == kPolygonSkewParamId;
    const bool curvedScanOnly = row.param == kScanAspectParamId || row.param == kScanRotationParamId
        || row.param == kScanWarpParamId;
    if (page >= 0 && page != terrainPage) return false;
    if (polygonOnly) return trace == s3g::AmbiWaveTerrainTrace::Polygon;
    if (curvedScanOnly && trace == s3g::AmbiWaveTerrainTrace::Polygon) return false;
    if (fieldOnly) return motion == s3g::AmbiWaveTerrainMotionMode::Field;
    if (rotateOnly) return motion == s3g::AmbiWaveTerrainMotionMode::Rotate;
    return true;
}

} // namespace

@interface S3GAmbiWaveTerrainEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
    clap_id _openMenuParam;
    uint32_t _menuItemCount;
    int _hoverMenuItem;
    uint32_t _selectedVoice;
    int _viewMode;
    int _terrainPage;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    double _viewAzDeg;
    double _viewElDeg;
    double _viewZoom;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiWaveTerrainEncoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 1158, 828)];
    if (self) {
        _plugin = plugin; _timer = nil; _dragParam = 0; _openMenuParam = 0; _menuItemCount = 0u;
        _hoverMenuItem = -1; _selectedVoice = 0u; _viewMode = 2; _terrainPage = 0; _dragView = NO;
        _viewAzDeg = 38.0; _viewElDeg = 32.0; _viewZoom = 1.0; [self setWantsLayer:YES];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (!_timer) _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)timerTick:(NSTimer*)timer { (void)timer; [self setNeedsDisplay:YES]; }

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 500); }
- (NSRect)fieldRect { return NSMakeRect(34, 78, 564, 442); }
- (NSRect)wavePanelRect { return NSMakeRect(18, 554, 596, 254); }
- (NSRect)terrainProfileRect { return NSMakeRect(34, 588, 564, 88); }
- (NSRect)waveRect { return NSMakeRect(34, 692, 564, 94); }
- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(NSMaxX(panel) - 10.0 - (3 - index) * 44.0, panel.origin.y + 3.0, 38.0, 16.0);
}
- (NSRect)zoomButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(NSMaxX(panel) - 200.0 + index * 28.0, panel.origin.y + 3.0, 22.0, 16.0);
}
- (NSRect)terrainTabRect:(int)index
{
    return NSMakeRect(704.0 + index * 42.0, 285.0, 38.0, 16.0);
}
- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 90.0; _viewElDeg = 90.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
    [self setNeedsDisplay:YES];
}
- (NSPoint)project:(s3g::Vec3)point rect:(NSRect)rect depth:(CGFloat*)depth
{
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.36 * std::clamp(_viewZoom, 0.55, 2.4);
    const float az = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float el = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(az), sa = std::sin(az), ce = std::cos(el), se = std::sin(el);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 + se * point.z;
    const float z2 = -se * y1 + ce * point.z;
    if (depth) *depth = z2;
    return NSMakePoint(NSMidX(rect) + x1 * scale, NSMidY(rect) - y2 * scale);
}
- (s3g::Vec3)worldPoint:(s3g::AmbiWaveTerrainPoint)point
{
    const auto direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return { direction.x * point.distance, direction.y * point.distance, direction.z * point.distance };
}
- (s3g::AmbiWaveTerrainRegion)snapshotRegion:(uint32_t)voice next:(BOOL)next
{
    s3g::AmbiWaveTerrainRegion region {};
    if (next) {
        region.u = _plugin->guiNextRegionU[voice].load(std::memory_order_relaxed);
        region.v = _plugin->guiNextRegionV[voice].load(std::memory_order_relaxed);
        region.radius = _plugin->guiNextRadius[voice].load(std::memory_order_relaxed);
        region.aspect = _plugin->guiNextAspect[voice].load(std::memory_order_relaxed);
        region.rotation = _plugin->guiNextRotation[voice].load(std::memory_order_relaxed);
        region.trace = static_cast<s3g::AmbiWaveTerrainTrace>(_plugin->guiNextTrace[voice].load(std::memory_order_relaxed));
    } else {
        region.u = _plugin->guiCurrentRegionU[voice].load(std::memory_order_relaxed);
        region.v = _plugin->guiCurrentRegionV[voice].load(std::memory_order_relaxed);
        region.radius = _plugin->guiCurrentRadius[voice].load(std::memory_order_relaxed);
        region.aspect = _plugin->guiCurrentAspect[voice].load(std::memory_order_relaxed);
        region.rotation = _plugin->guiCurrentRotation[voice].load(std::memory_order_relaxed);
        region.trace = static_cast<s3g::AmbiWaveTerrainTrace>(_plugin->guiCurrentTrace[voice].load(std::memory_order_relaxed));
    }
    return region;
}

- (s3g::AmbiWaveTerrainRegion)activeRegion:(uint32_t)voice
{
    const auto current = [self snapshotRegion:voice next:NO];
    const auto next = [self snapshotRegion:voice next:YES];
    const float transition = _plugin->guiTransition[voice].load(std::memory_order_relaxed);
    float deltaU = next.u - current.u;
    if (deltaU > 0.5f) deltaU -= 1.0f;
    else if (deltaU < -0.5f) deltaU += 1.0f;
    s3g::AmbiWaveTerrainRegion region {};
    region.u = current.u + deltaU * transition;
    region.u -= std::floor(region.u);
    region.v = s3g::lerp(current.v, next.v, transition);
    region.radius = s3g::lerp(current.radius, next.radius, transition);
    region.aspect = s3g::lerp(current.aspect, next.aspect, transition);
    region.rotation = s3g::lerp(current.rotation, next.rotation, transition);
    region.trace = transition < 0.5f ? current.trace : next.trace;
    return region;
}

- (std::array<float, 2>)contourUv:(s3g::AmbiWaveTerrainRegion)region phase:(float)phase
{
    return _plugin->engine.contourPoint(region, phase);
}

- (void)drawVoiceField:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect], field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"TERRAIN VOICE FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int index = 0; index < 3; ++index) s3g::clap_gui::drawHeaderButton([self viewButtonRect:index], panel, labels[index], index == _viewMode, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], panel, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], panel, @"+", false, attrs, style);
    [s3g::clap_gui::color(0x090909) setFill]; NSRectFill(field); [style.grid setStroke]; NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState]; [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1, 1)] addClip];
    struct Facet { std::array<NSPoint, 4> p {}; CGFloat depth = 0.0; NSColor* color = nil; };
    std::vector<Facet> facets; facets.reserve(32u * 16u);
    for (uint32_t lat = 0; lat < 16u; ++lat) {
        const float v0 = static_cast<float>(lat) / 16.0f, v1 = static_cast<float>(lat + 1u) / 16.0f;
        for (uint32_t lon = 0; lon < 32u; ++lon) {
            const float u0 = static_cast<float>(lon) / 32.0f, u1 = static_cast<float>(lon + 1u) / 32.0f;
            const float uc = (u0 + u1) * 0.5f, vc = (v0 + v1) * 0.5f;
            const float h = _plugin->engine.terrainHeight(uc, vc);
            const float visibleHeight = h * _plugin->params.terrainDepth * _plugin->params.terrainRelief;
            const std::array<s3g::AmbiWaveTerrainPoint, 4> shell { _plugin->engine.surfacePoint(u0, v0), _plugin->engine.surfacePoint(u1, v0), _plugin->engine.surfacePoint(u1, v1), _plugin->engine.surfacePoint(u0, v1) };
            Facet facet {};
            for (uint32_t corner = 0; corner < 4u; ++corner) { CGFloat d = 0; facet.p[corner] = [self project:[self worldPoint:shell[corner]] rect:field depth:&d]; facet.depth += d * 0.25; }
            const CGFloat light = std::clamp<CGFloat>(0.155 + visibleHeight * 0.065 + (1.0 - vc) * 0.025, 0.08, 0.26);
            facet.color = [NSColor colorWithCalibratedWhite:light alpha:0.96];
            facets.push_back(facet);
        }
    }
    std::sort(facets.begin(), facets.end(), [](const Facet& a, const Facet& b) { return a.depth > b.depth; });
    for (const auto& facet : facets) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:facet.p[0]];
        for (int corner = 1; corner < 4; ++corner) [path lineToPoint:facet.p[corner]];
        [path closePath];
        [facet.color setFill];
        [path fill];
        [[NSColor colorWithCalibratedWhite:0.62 alpha:0.075] setStroke];
        [path setLineWidth:0.28];
        [path stroke];
    }
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiWaveTerrainMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u); _plugin->guiSelectedVoice.store(_selectedVoice, std::memory_order_relaxed);
    for (int pass = 0; pass < 2; ++pass) {
        for (uint32_t voice = 0; voice < voices; ++voice) {
            const bool selected = voice == _selectedVoice;
            if (selected != (pass == 1)) continue;
            const bool active = _plugin->guiFieldActive[voice].load(std::memory_order_relaxed) != 0u;
            const CGFloat activityAlpha = active ? 1.0 : 0.24;
            const uint32_t segments = selected ? 112u : 30u;
            const auto current = [self snapshotRegion:voice next:NO];
            const auto next = [self snapshotRegion:voice next:YES];
            const auto region = [self activeRegion:voice];
            const auto drawPath = [&](const s3g::AmbiWaveTerrainRegion& pathRegion, NSColor* color, CGFloat alpha, CGFloat width) {
                NSBezierPath* scanPath = [NSBezierPath bezierPath];
                for (uint32_t index = 0; index <= segments; ++index) {
                    const auto uv = [self contourUv:pathRegion phase:static_cast<float>(index) / static_cast<float>(segments)];
                    const NSPoint point = [self project:[self worldPoint:_plugin->engine.surfacePoint(uv[0], uv[1])] rect:field depth:nullptr];
                    if (index == 0u) [scanPath moveToPoint:point]; else [scanPath lineToPoint:point];
                }
                [[color colorWithAlphaComponent:alpha * activityAlpha] setStroke];
                [scanPath setLineWidth:width];
                [scanPath stroke];
            };
            if (selected) {
                drawPath(current, [NSColor colorWithCalibratedRed:0.25 green:0.72 blue:0.92 alpha:1.0], 0.28, 0.72);
                drawPath(next, [NSColor colorWithCalibratedRed:0.94 green:0.40 blue:0.68 alpha:1.0], 0.28, 0.72);
            }
            drawPath(region, [NSColor colorWithCalibratedWhite:0.94 alpha:1.0], selected ? 0.94 : 0.30, selected ? 1.65 : 0.62);

            s3g::AmbiWaveTerrainPoint spatialPoint {};
            spatialPoint.azimuthDeg = _plugin->guiPointAzimuth[voice].load(std::memory_order_relaxed);
            spatialPoint.elevationDeg = _plugin->guiPointElevation[voice].load(std::memory_order_relaxed);
            spatialPoint.distance = _plugin->guiPointDistance[voice].load(std::memory_order_relaxed);
            const NSPoint center = [self project:[self worldPoint:spatialPoint] rect:field depth:nullptr];
            const float energy = _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
            const CGFloat size = (selected ? 9.0 : 5.0) + std::clamp<CGFloat>(energy * 20.0f, 0.0, 4.0);
            const NSRect marker = NSMakeRect(center.x - size * 0.5, center.y - size * 0.5, size, size);
            [[NSColor colorWithCalibratedWhite:0.98 alpha:activityAlpha] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:marker] fill];
            [[NSColor colorWithCalibratedWhite:selected ? 0.06 : 0.34 alpha:activityAlpha] setStroke];
            [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(marker, -1.0, -1.0)] stroke];
        }
    }
    [NSGraphicsContext restoreGraphicsState];
    NSString* voiceStatus = nil;
    if (_plugin->params.motionMode == s3g::AmbiWaveTerrainMotionMode::Rotate) {
        voiceStatus = [NSString stringWithFormat:@"VOICE %u  AZ %+.2f / EL %+.2f RPM", _selectedVoice + 1u,
            static_cast<double>(_plugin->params.azimuthRateRpm), static_cast<double>(_plugin->params.elevationRateRpm)];
    } else {
        voiceStatus = [NSString stringWithFormat:@"VOICE %u  %@ / %@", _selectedVoice + 1u,
            [NSString stringWithUTF8String:s3g::ambiWaveTerrainSelectionName(_plugin->params.selection)],
            [NSString stringWithUTF8String:s3g::ambiWaveTerrainTransitionName(_plugin->params.transition)]];
    }
    [voiceStatus drawAtPoint:NSMakePoint(field.origin.x + 10, NSMaxY(field) - 20) withAttributes:attrs];
}

- (void)drawWaveform:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self wavePanelRect], terrainRect = [self terrainProfileRect], waveRect = [self waveRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"TERRAIN TO WAVEFORM", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const float transition = _plugin->guiTransition[_selectedVoice].load(std::memory_order_relaxed);
    const auto drawProfile = [&](NSRect rect, bool topography) {
        [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(rect); [style.grid setStroke]; NSFrameRect(rect);
        [[NSColor colorWithCalibratedWhite:0.24 alpha:1.0] setStroke]; NSBezierPath* mid = [NSBezierPath bezierPath];
        [mid moveToPoint:NSMakePoint(rect.origin.x + 10, NSMidY(rect))]; [mid lineToPoint:NSMakePoint(NSMaxX(rect) - 10, NSMidY(rect))]; [mid stroke];
        NSBezierPath* a = [NSBezierPath bezierPath], *b = [NSBezierPath bezierPath], *mix = [NSBezierPath bezierPath];
        for (uint32_t index = 0; index < 256u; ++index) {
            const float va = topography ? _plugin->guiTerrainA[index].load(std::memory_order_relaxed) : _plugin->guiTableA[index].load(std::memory_order_relaxed);
            const float vb = topography ? _plugin->guiTerrainB[index].load(std::memory_order_relaxed) : _plugin->guiTableB[index].load(std::memory_order_relaxed);
            const CGFloat x = rect.origin.x + 10.0 + static_cast<CGFloat>(index) / 255.0 * (rect.size.width - 20.0);
            const CGFloat ya = NSMidY(rect) - va * rect.size.height * 0.31, yb = NSMidY(rect) - vb * rect.size.height * 0.31;
            const CGFloat ym = NSMidY(rect) - s3g::lerp(va, vb, transition) * rect.size.height * 0.31;
            if (index == 0u) { [a moveToPoint:NSMakePoint(x, ya)]; [b moveToPoint:NSMakePoint(x, yb)]; [mix moveToPoint:NSMakePoint(x, ym)]; }
            else { [a lineToPoint:NSMakePoint(x, ya)]; [b lineToPoint:NSMakePoint(x, yb)]; [mix lineToPoint:NSMakePoint(x, ym)]; }
        }
        [[NSColor colorWithCalibratedRed:0.25 green:0.72 blue:0.92 alpha:0.30] setStroke]; [a setLineWidth:0.70]; [a stroke];
        [[NSColor colorWithCalibratedRed:0.94 green:0.40 blue:0.68 alpha:0.30] setStroke]; [b setLineWidth:0.70]; [b stroke];
        [[NSColor colorWithCalibratedWhite:0.92 alpha:0.94] setStroke]; [mix setLineWidth:1.30]; [mix stroke];
    };
    drawProfile(terrainRect, true);
    drawProfile(waveRect, false);
    [@"TOPOGRAPHY" drawAtPoint:NSMakePoint(terrainRect.origin.x + 8, terrainRect.origin.y + 5) withAttributes:attrs];
    [[NSString stringWithFormat:@"OSCILLATOR / %@   %.1f HZ   A -> B %.0f%%",
        [NSString stringWithUTF8String:s3g::ambiWaveTerrainInterpretationName(_plugin->params.interpretation)],
        static_cast<double>(_plugin->guiFrequency[_selectedVoice].load(std::memory_order_relaxed)),
        static_cast<double>(transition * 100.0f)] drawAtPoint:NSMakePoint(waveRect.origin.x + 8, waveRect.origin.y + 5) withAttributes:attrs];
}

- (NSString*)valueText:(clap_id)param
{
    double value = 0.0; paramsGetValue(&_plugin->plugin, param, &value); char text[64] {};
    paramsValueToText(&_plugin->plugin, param, value, text, sizeof(text)); return [NSString stringWithUTF8String:text];
}
- (void)drawControls:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 228, style); s3g::clap_gui::drawPanelHeader(@"ENGINE", true, 630, 42, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(630, 282, 250, 202, style); s3g::clap_gui::drawPanelHeader(@"TERRAIN", true, 630, 282, 250, 21, attrs, style);
    static NSString* terrainTabs[] = { @"FORM", @"SKIN", @"WARP", @"READ" };
    for (int index = 0; index < 4; ++index) {
        s3g::clap_gui::drawHeaderButton([self terrainTabRect:index], NSMakeRect(630, 282, 250, 202),
            terrainTabs[index], index == _terrainPage, attrs, style);
    }
    s3g::clap_gui::drawPanelFrame(630, 508, 250, 150, style); s3g::clap_gui::drawPanelHeader(@"ENVELOPE", true, 630, 508, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(630, 670, 250, 138, style); s3g::clap_gui::drawPanelHeader(@"PITCH / OUTPUT", true, 630, 670, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(896, 42, 246, 228, style); s3g::clap_gui::drawPanelHeader(@"SCAN", true, 896, 42, 246, 21, attrs, style);
    if (_plugin->params.motionMode == s3g::AmbiWaveTerrainMotionMode::Field) {
        s3g::clap_gui::drawPanelFrame(896, 282, 246, 124, style); s3g::clap_gui::drawPanelHeader(@"SELECTION", true, 896, 282, 246, 21, attrs, style);
    }
    s3g::clap_gui::drawPanelFrame(896, 418, 246, 228, style);
    s3g::clap_gui::drawPanelHeader(_plugin->params.motionMode == s3g::AmbiWaveTerrainMotionMode::Rotate ? @"ROTATION" : @"TIME FIELDS", true, 896, 418, 246, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(896, 658, 246, 150, style); s3g::clap_gui::drawPanelHeader(@"PROJECTION", true, 896, 658, 246, 21, attrs, style);
    for (const auto& row : kGuiRows) {
        if (!guiRowVisible(row, _plugin->params.motionMode, _terrainPage, _plugin->params.trace)) continue;
        NSString* label = [NSString stringWithUTF8String:row.label];
        if (row.menu) s3g::clap_gui::drawMenu(label, [self valueText:row.param], row.y, attrs, valueAttrs, style, row.panelX + 16, row.panelX + 108, 124);
        else {
            double value = 0.0; paramsGetValue(&_plugin->plugin, row.param, &value); const ParamDef* def = paramDef(row.param);
            const CGFloat norm = def ? std::clamp<CGFloat>((value - def->min) / std::max(0.000001, def->max - def->min), 0.0, 1.0) : 0.0;
            s3g::clap_gui::drawSlider(label, [self valueText:row.param], norm, row.y, attrs, valueAttrs, style, row.panelX + 16, row.panelX + 108, row.panelX + 200, 82);
        }
    }
}

- (NSString* const*)menuItems:(clap_id)param count:(uint32_t*)count selected:(int*)selected
{
    static NSString* mode[] = { @"FREE", @"MIDI", @"BOTH" };
    static NSString* order[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* form[] = { @"SPHERE", @"TETRA", @"CUBE", @"OCTA", @"DODECA", @"ICOSA" };
    static NSString* skin[] = { @"HARMONIC", @"FBM", @"CELL", @"VOT", @"RIDGES", @"DUNES", @"CRATERS", @"TECTONIC" };
    static NSString* read[] = { @"HEIGHT", @"EDGE", @"CURVE", @"BLEND", @"GRADIENT", @"RIDGE", @"VALLEY", @"NORMAL", @"CROSS", @"VECTOR" };
    static NSString* trace[] = { @"ORBIT", @"LISSAJOUS", @"ROSETTE", @"FOLD", @"POLYGON" };
    static NSString* law[] = { @"RANDOM", @"SERIES", @"WEIGHT", @"TENDENCY", @"MARKOV", @"WALK" };
    static NSString* join[] = { @"LINK", @"MERGE", @"VARY" };
    static NSString* pitch[] = { @"NOTE", @"TRAVEL" };
    static NSString* scale[] = { @"FREE", @"CHROM", @"MAJOR", @"MINOR", @"PENTA", @"WHOLE", @"HARM MIN" };
    static NSString* motion[] = { @"FIELD", @"ROTATE" };
    static NSString* listener[] = { @"OFF", @"FOLLOW", @"COUNTER", @"BALANCE" };
    double value = 0.0; paramsGetValue(&_plugin->plugin, param, &value); *selected = static_cast<int>(std::lround(value));
    if (param == kModeParamId) { *count = 3; return mode; }
    if (param == kOrderParamId) { *count = 7; *selected -= 1; return order; }
    if (param == kTerrainFormParamId) { *count = 6; return form; }
    if (param == kSkinParamId) { *count = 8; return skin; }
    if (param == kInterpretationParamId) { *count = 10; return read; }
    if (param == kTraceParamId) { *count = 5; return trace; }
    if (param == kSelectionParamId) { *count = 6; return law; }
    if (param == kPitchModeParamId) { *count = 2; return pitch; }
    if (param == kPitchScaleParamId) { *count = 7; return scale; }
    if (param == kMotionModeParamId) { *count = 2; return motion; }
    if (param == kFieldListenModeParamId) { *count = 4; return listener; }
    *count = 3; return join;
}
- (NSRect)openMenuRect
{
    const GuiRow* row = guiRow(_openMenuParam); if (!row) return NSZeroRect;
    const CGFloat height = 18.0 * _menuItemCount;
    CGFloat y = row->y + 17;
    if (y + height > [self bounds].size.height - 4.0) y = row->y - height;
    return NSMakeRect(row->panelX + 108, y, 124, height);
}
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (!_openMenuParam) return; int selected = 0; uint32_t count = 0; NSString* const* items = [self menuItems:_openMenuParam count:&count selected:&selected];
    _menuItemCount = count; s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0, items, count, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty; const auto style = s3g::clap_gui::softTextStyle(); [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs(), *values = s3g::clap_gui::softValueAttrs(), *title = s3g::clap_gui::softTitleAttrs();
    [@"s3g AMBI WAVE TERRAIN ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:title];
    [s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)) drawAtPoint:NSMakePoint(1010, 14) withAttributes:values];
    [@"64 CH" drawAtPoint:NSMakePoint(1092, 14) withAttributes:values];
    [self drawVoiceField:attrs style:style]; [self drawWaveform:attrs style:style]; [self drawControls:attrs valueAttrs:values style:style]; [self drawOpenMenu:attrs style:style];
}

- (int)hitVoice:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    int best = -1;
    CGFloat distance = 12.0;
    const NSRect field = [self fieldRect];
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiWaveTerrainMaxVoices);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const auto region = [self activeRegion:voice];
        auto uv = [self contourUv:region phase:0.0f];
        NSPoint previous = [self project:[self worldPoint:_plugin->engine.surfacePoint(uv[0], uv[1])] rect:field depth:nullptr];
        for (uint32_t index = 1; index <= 36u; ++index) {
            uv = [self contourUv:region phase:static_cast<float>(index) / 36.0f];
            const NSPoint current = [self project:[self worldPoint:_plugin->engine.surfacePoint(uv[0], uv[1])] rect:field depth:nullptr];
            const CGFloat vx = current.x - previous.x, vy = current.y - previous.y;
            const CGFloat lengthSquared = vx * vx + vy * vy;
            const CGFloat amount = lengthSquared > 0.000001
                ? std::clamp(((point.x - previous.x) * vx + (point.y - previous.y) * vy) / lengthSquared, 0.0, 1.0) : 0.0;
            const CGFloat nearestX = previous.x + vx * amount, nearestY = previous.y + vy * amount;
            const CGFloat candidate = std::hypot(point.x - nearestX, point.y - nearestY);
            if (candidate < distance) { distance = candidate; best = static_cast<int>(voice); }
            previous = current;
        }
    }
    return best;
}
- (void)setParam:(clap_id)param point:(NSPoint)point
{
    const GuiRow* row = guiRow(param); const ParamDef* def = paramDef(param); if (!row || !def) return;
    const double norm = std::clamp((static_cast<double>(point.x) - (row->panelX + 108.0)) / 82.0, 0.0, 1.0);
    double value = def->min + norm * (def->max - def->min); if (def->stepped) value = std::round(value);
    applyParam(*_plugin, param, value); [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenuParam) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, [self openMenuRect], 18.0, _menuItemCount);
        if (hit >= 0) applyParam(*_plugin, _openMenuParam, _openMenuParam == kOrderParamId ? hit + 1 : hit);
        _openMenuParam = 0; _hoverMenuItem = -1; [self setNeedsDisplay:YES]; return;
    }
    for (int index = 0; index < 4; ++index) if (NSPointInRect(point, [self terrainTabRect:index])) { _terrainPage = index; [self setNeedsDisplay:YES]; return; }
    for (int index = 0; index < 3; ++index) if (NSPointInRect(point, [self viewButtonRect:index])) { [self setViewPreset:index]; return; }
    for (int index = 0; index < 2; ++index) if (NSPointInRect(point, [self zoomButtonRect:index])) { _viewZoom = std::clamp(_viewZoom + (index ? 0.15 : -0.15), 0.55, 2.4); [self setNeedsDisplay:YES]; return; }
    const int voice = [self hitVoice:point]; if (voice >= 0) { _selectedVoice = static_cast<uint32_t>(voice); _plugin->guiSelectedVoice.store(_selectedVoice); [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, [self fieldRect])) { _dragView = YES; _lastDragPoint = point; return; }
    for (const auto& row : kGuiRows) {
        if (!guiRowVisible(row, _plugin->params.motionMode, _terrainPage, _plugin->params.trace)) continue;
        if (!NSPointInRect(point, NSMakeRect(row.panelX + 8, row.y - 8, 232, 24))) continue;
        if (row.menu) { _openMenuParam = row.param; int selected = 0; [self menuItems:row.param count:&_menuItemCount selected:&selected]; _hoverMenuItem = -1; [self setNeedsDisplay:YES]; }
        else { _dragParam = row.param; [self setParam:row.param point:point]; }
        return;
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) { _viewAzDeg += (point.x - _lastDragPoint.x) * 0.42; _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.34, -85.0, 85.0); _viewMode = -1; _lastDragPoint = point; [self setNeedsDisplay:YES]; return; }
    if (_dragParam) [self setParam:_dragParam point:point];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragParam = 0; _dragView = NO; }
- (void)viewDidMoveToWindow { [super viewDidMoveToWindow]; [[self window] setAcceptsMouseMovedEvents:YES]; }
- (void)mouseMoved:(NSEvent*)event
{
    if (!_openMenuParam) return; const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(point, [self openMenuRect], 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) { _hoverMenuItem = hover; [self setNeedsDisplay:YES]; }
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating) { return !floating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating) { if (!api || !floating) return false; *api = CLAP_WINDOW_API_COCOA; *floating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false; auto* p = self(plugin); if (p->guiView) return true;
    p->guiView = [[S3GAmbiWaveTerrainEncoderView alloc] initWithPlugin:p]; if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), 1158u, 828u)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false;
    }
    return true;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin); if (!p || !p->guiView) return; [static_cast<S3GAmbiWaveTerrainEncoderView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, 1158u, 828u, width, height); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, 1158u, 828u, width, height); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, width, height); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false;
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; [static_cast<S3GAmbiWaveTerrainEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<S3GAmbiWaveTerrainEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };

} // namespace
#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
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

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-wave-terrain-encoder-64",
    "s3g Ambi Wave Terrain Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.5.0-pre",
    "64-voice stochastic wave-terrain instrument with an optional ambisonic field listener.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin(); if (!p) return nullptr; p->host = host;
    p->engine.prepare(p->sampleRate); p->engine.setParams(p->params); p->params = p->engine.params(); snapshotGui(*p);
    p->plugin.desc = &descriptor; p->plugin.plugin_data = p; p->plugin.init = init; p->plugin.destroy = destroy;
    p->plugin.activate = activate; p->plugin.deactivate = deactivate; p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing; p->plugin.reset = reset; p->plugin.process = process;
    p->plugin.get_extension = getExtension; p->plugin.on_main_thread = onMainThread; return &p->plugin;
}
uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0u ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) { return id && std::strcmp(id, descriptor.id) == 0 ? create(host) : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id) { return id && std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
