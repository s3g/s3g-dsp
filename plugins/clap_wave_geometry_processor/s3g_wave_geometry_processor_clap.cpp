#include "s3g_lane_patch.h"
#include "s3g_realtime.h"
#include "s3g_wave_geometry_processor.h"
#include "s3g_topology_heatmap.h"

#include <clap/clap.h>
#include <clap/ext/latency.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
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
#include <limits>
#include <new>
#include <vector>

namespace {

#ifndef S3G_WAVE_GEOMETRY_PLUGIN_ID
#define S3G_WAVE_GEOMETRY_PLUGIN_ID "org.s3g.s3g-dsp.wave-geometry-processor"
#endif

#ifndef S3G_WAVE_GEOMETRY_PLUGIN_NAME
#define S3G_WAVE_GEOMETRY_PLUGIN_NAME "s3g Processor Wave Geometry 8ch"
#endif

constexpr uint32_t kChannelCount = s3g::kWaveGeometryChannels;
constexpr uint32_t kStateVersion = 4;
constexpr uint32_t kGuiWidth = static_cast<uint32_t>(
    s3g::gui_layout::kTopologyProcessorColumns.canvasWidth);
constexpr uint32_t kGuiHeight = 788;
constexpr double kPrimaryPanelX =
    s3g::gui_layout::kTopologyProcessorColumns.first.x;
constexpr double kSecondaryPanelX =
    s3g::gui_layout::kTopologyProcessorColumns.second.x;
constexpr double kPanelWidth =
    s3g::gui_layout::kTopologyProcessorColumns.first.width;
constexpr double kLegacyContentTop = 34.0;
constexpr double kContentTranslation =
    s3g::gui_layout::kStandardMetrics.contentTop - kLegacyContentTop;
constexpr double kContentCoordinateHeight =
    static_cast<double>(kGuiHeight) - kContentTranslation;
constexpr uint32_t kScopeFrames = 128;
constexpr double kMotionRateMinHz = 0.01;
constexpr double kMotionRateMaxHz = 1.0;
constexpr double kEngineRowPitch =
    s3g::gui_layout::kStandardMetrics.rowPitch;
constexpr double kEngineFirstRow = 36.0;
constexpr double kEnginePanelHeight =
    s3g::gui_layout::toolboxHeightForRows(13u);
constexpr double kTopologyPanelY = kLegacyContentTop;
constexpr double kTopologyPanelHeight =
    s3g::gui_layout::toolboxHeightForRows(16u);
constexpr double kMeshPanelY = kTopologyPanelY + kTopologyPanelHeight
    + s3g::gui_layout::kStandardMetrics.panelGap;
constexpr double kMeshPanelHeight =
    s3g::gui_layout::toolboxHeightForRows(4u);

constexpr clap_id kFoldParamId = 1;
constexpr clap_id kDriveParamId = 2;
constexpr clap_id kHoldParamId = 3;
constexpr clap_id kClipParamId = 4;
constexpr clap_id kRectifyParamId = 5;
constexpr clap_id kEdgeParamId = 6;
constexpr clap_id kZeroParamId = 7;
constexpr clap_id kPolarParamId = 8;
constexpr clap_id kTransParamId = 9;
constexpr clap_id kMixParamId = 10;
constexpr clap_id kGainParamId = 11;
constexpr clap_id kSafetyParamId = 12;
constexpr clap_id kBitsParamId = 13;
constexpr clap_id kStepParamId = 14;
constexpr clap_id kTapeParamId = 15;
constexpr clap_id kSpeedParamId = 16;
constexpr clap_id kMeshCouplingParamId = 17;
constexpr clap_id kMeshTensionParamId = 18;
constexpr clap_id kMeshDecayParamId = 19;
constexpr clap_id kMeshDampingParamId = 20;

constexpr clap_id kTopologyShapeParamId = 30;
constexpr clap_id kTopologyAmountParamId = 31;
constexpr clap_id kTopologySeedParamId = 32;
constexpr clap_id kTopologyPullParamId = 33;
constexpr clap_id kTopologyXParamId = 34;
constexpr clap_id kTopologyYParamId = 35;
constexpr clap_id kTopologyZParamId = 36;
constexpr clap_id kTopologyTwistParamId = 37;
constexpr clap_id kTopologyFlareParamId = 38;
constexpr clap_id kTopologyMotionParamId = 39;
constexpr clap_id kTopologyVariantParamId = 40;
constexpr clap_id kTopologyRateParamId = 41;
constexpr clap_id kTopologyDepthParamId = 42;
constexpr clap_id kTopologyNeighborsParamId = 43;
constexpr clap_id kTopologyRadiusParamId = 44;
constexpr clap_id kTopologyCentroidParamId = 45;
constexpr uint32_t kParameterBankSize = 46u;

constexpr clap_id kStoredParamIds[] {
    kFoldParamId, kDriveParamId, kHoldParamId, kClipParamId,
    kRectifyParamId, kEdgeParamId, kZeroParamId, kPolarParamId,
    kTransParamId, kMixParamId, kGainParamId, kSafetyParamId,
    kBitsParamId, kStepParamId, kTapeParamId, kSpeedParamId,
    kMeshCouplingParamId, kMeshTensionParamId, kMeshDecayParamId,
    kMeshDampingParamId, kTopologyShapeParamId, kTopologyAmountParamId,
    kTopologySeedParamId, kTopologyPullParamId, kTopologyXParamId,
    kTopologyYParamId, kTopologyZParamId, kTopologyTwistParamId,
    kTopologyFlareParamId, kTopologyMotionParamId,
    kTopologyVariantParamId, kTopologyRateParamId,
    kTopologyDepthParamId, kTopologyNeighborsParamId,
    kTopologyRadiusParamId, kTopologyCentroidParamId
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::WaveGeometrySettings settings {};
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
};

// Versions 1-3 wrote native settings structs directly. Freeze their exact
// layouts here so adding the mesh cannot change how many bytes an old preset
// consumes.
struct LegacyWaveGeometryParamsV2 {
    float fold;
    float drive;
    float hold;
    float clip;
    float rectify;
    float edge;
    float zero;
    float polar;
    float bits;
    float step;
    float trans;
    float mix;
    float gainDb;
    float safety;
};

struct LegacyWaveGeometryParamsV3 {
    float fold;
    float drive;
    float hold;
    float clip;
    float rectify;
    float edge;
    float zero;
    float polar;
    float bits;
    float step;
    float trans;
    float tape;
    float speed;
    float mix;
    float gainDb;
    float safety;
};

struct LegacyTopologyStateV3 {
    double amount;
    double jitter;
    double collapse;
    double dirX;
    double dirY;
    double dirZ;
    double twist;
    double flare;
    uint32_t shape;
    uint32_t motionMode;
    uint32_t motionVariant;
    double motionRateHz;
    double motionDepth;
    double motionPhase;
    uint32_t neighborCount;
    double neighborRadius;
    double centroidAmount;
};

struct LegacyWaveGeometrySettingsV2 {
    LegacyWaveGeometryParamsV2 base {};
    LegacyTopologyStateV3 topology {};
};

struct LegacyWaveGeometrySettingsV3 {
    LegacyWaveGeometryParamsV3 base {};
    LegacyTopologyStateV3 topology {};
};

struct SavedStateV2 {
    uint32_t version = 2;
    LegacyWaveGeometrySettingsV2 settings {};
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
};

struct SavedStateV3 {
    uint32_t version = 3;
    LegacyWaveGeometrySettingsV3 settings {};
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
};

struct SavedStateV1 {
    uint32_t version = 1;
    LegacyWaveGeometrySettingsV3 settings {};
};

static_assert(sizeof(LegacyWaveGeometryParamsV2) == 56u);
static_assert(sizeof(LegacyWaveGeometryParamsV3) == 64u);
static_assert(sizeof(LegacyTopologyStateV3) == 128u);
static_assert(sizeof(LegacyWaveGeometrySettingsV2) == 184u);
static_assert(sizeof(LegacyWaveGeometrySettingsV3) == 192u);
static_assert(offsetof(SavedStateV1, settings) == 8u);
static_assert(sizeof(SavedStateV1) == 200u);
static_assert(offsetof(SavedStateV2, settings) == 8u);
static_assert(offsetof(SavedStateV2, patchRows) == 192u);
static_assert(sizeof(SavedStateV2) == 704u);
static_assert(offsetof(SavedStateV3, settings) == 8u);
static_assert(offsetof(SavedStateV3, patchRows) == 200u);
static_assert(sizeof(SavedStateV3) == 712u);
static_assert(sizeof(s3g::WaveGeometryMeshParams) == 16u);
static_assert(offsetof(SavedState, settings) == 8u);
static_assert(offsetof(SavedState, patchRows) == 216u);
static_assert(sizeof(SavedState) == 728u);

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    // `settings` belongs to the main/GUI thread. Host automation and GUI
    // gestures publish scalar values through the atomic bank; only the audio
    // thread owns `audioSettings` and mutates the DSP core.
    s3g::WaveGeometrySettings settings {};
    s3g::WaveGeometrySettings audioSettings {};
    std::array<std::atomic<double>, kParameterBankSize> parameterValues {};
    std::atomic<uint64_t> parameterRevision { 1u };
    uint64_t audioParameterRevision = 0u;
    std::atomic<double> publishedMotionPhase { 0.0 };
    std::atomic<bool> motionPhaseRestorePending { false };
    std::atomic<uint32_t> publishedMeshTailFrames { 0u };
    s3g::WaveGeometryProcessor processor;
    // The LanePatch object is main-thread owned. Audio consumes immutable row
    // masks published atomically whenever the GUI or state loader changes it.
    s3g::LanePatch patch;
    std::array<std::atomic<uint64_t>, kChannelCount> patchRowsPublished {};
    std::vector<std::vector<float>> input32;
    std::vector<std::vector<float>> output32;
    std::vector<const float*> inputPtrs;
    std::vector<float*> outputPtrs;
    std::array<std::array<std::atomic<float>, kScopeFrames>, kChannelCount> scope {};
    std::array<std::array<std::atomic<float>, kChannelCount>, kChannelCount> meshEdgeEnergy {};
    std::array<std::array<std::atomic<float>, kChannelCount>, kChannelCount> meshEdgePhase {};
    std::atomic<uint32_t> scopeWrite { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> tailChangePending { false };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
    void* macRealtimeActivity = nullptr;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void markTailChanged(Plugin& p)
{
    p.tailChangePending.store(true, std::memory_order_release);
}

// clap.tail requires changed() to be delivered on the audio thread. GUI,
// state, and params.flush paths only coalesce a pending notification.
void deliverTailChangedOnAudioThread(Plugin& p)
{
    if (p.tailChangePending.exchange(false, std::memory_order_acq_rel)
        && p.host && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

bool paramAffectsTail(clap_id id)
{
    return id == kTapeParamId || id == kSpeedParamId
        || (id >= kMeshCouplingParamId && id <= kMeshDampingParamId)
        || (id >= kTopologyShapeParamId
            && id <= kTopologyCentroidParamId);
}

double clampMotionRate(double value)
{
    return std::clamp(value, kMotionRateMinHz, kMotionRateMaxHz);
}

s3g::TopologyState topologyStateForPlugin(const Plugin& p)
{
    return p.settings.topology;
}

double engineRowY(double panelY, uint32_t index)
{
    return panelY + kEngineFirstRow + static_cast<double>(index) * kEngineRowPitch;
}

bool assignSettingsParam(
    s3g::WaveGeometrySettings& settings,
    clap_id id,
    double value)
{
    auto& prm = settings.base;
    auto& mesh = settings.mesh;
    auto& t = settings.topology;
    switch (id) {
    case kFoldParamId: prm.fold = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDriveParamId: prm.drive = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kHoldParamId: prm.hold = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kClipParamId: prm.clip = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kRectifyParamId: prm.rectify = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kEdgeParamId: prm.edge = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kZeroParamId: prm.zero = static_cast<float>(std::clamp(value, 0.0, 0.78)); break;
    case kPolarParamId: prm.polar = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBitsParamId: prm.bits = static_cast<float>(std::clamp(value, 0.0, 0.92)); break;
    case kStepParamId: prm.step = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kTransParamId: prm.trans = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kTapeParamId: prm.tape = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSpeedParamId: prm.speed = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kMeshCouplingParamId: mesh.coupling = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kMeshTensionParamId: mesh.tension = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kMeshDecayParamId: mesh.decay = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kMeshDampingParamId: mesh.damping = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kMixParamId: prm.mix = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: prm.gainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    case kSafetyParamId: prm.safety = static_cast<float>(std::clamp(value, 0.12, 0.92)); break;
    case kTopologyShapeParamId: t.shape = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::lround(value)), 0, static_cast<int32_t>(s3g::kTopologyShapeCount - 1u))); break;
    case kTopologyAmountParamId: t.amount = std::clamp(value, 0.0, 1.0); break;
    case kTopologySeedParamId: t.jitter = std::clamp(value, 0.0, 1.0); break;
    case kTopologyPullParamId: t.collapse = std::clamp(value, 0.0, 1.0); break;
    case kTopologyXParamId: t.dirX = std::clamp(value, -1.0, 1.0); break;
    case kTopologyYParamId: t.dirY = std::clamp(value, -1.0, 1.0); break;
    case kTopologyZParamId: t.dirZ = std::clamp(value, -1.0, 1.0); break;
    case kTopologyTwistParamId: t.twist = std::clamp(value, -1.0, 1.0); break;
    case kTopologyFlareParamId: t.flare = std::clamp(value, -1.0, 1.0); break;
    case kTopologyMotionParamId: t.motionMode = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::lround(value)), 0, static_cast<int32_t>(s3g::kTopologyMotionModeCount - 1u))); break;
    case kTopologyVariantParamId: t.motionVariant = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::lround(value)), 0, static_cast<int32_t>(s3g::kTopologyVariantCount - 1u))); break;
    case kTopologyRateParamId: t.motionRateHz = clampMotionRate(value); break;
    case kTopologyDepthParamId: t.motionDepth = std::clamp(value, 0.0, 1.0); break;
    case kTopologyNeighborsParamId: t.neighborCount = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(std::lround(value)), 1, 3)); break;
    case kTopologyRadiusParamId: t.neighborRadius = std::clamp(value, 0.0, 1.0); break;
    case kTopologyCentroidParamId: t.centroidAmount = std::clamp(value, 0.0, 1.0); break;
    default: return false;
    }
    return true;
}

bool settingsParamValue(
    const s3g::WaveGeometrySettings& settings,
    clap_id id,
    double& value)
{
    const auto& prm = settings.base;
    const auto& mesh = settings.mesh;
    const auto& t = settings.topology;
    switch (id) {
    case kFoldParamId: value = prm.fold; return true;
    case kDriveParamId: value = prm.drive; return true;
    case kHoldParamId: value = prm.hold; return true;
    case kClipParamId: value = prm.clip; return true;
    case kRectifyParamId: value = prm.rectify; return true;
    case kEdgeParamId: value = prm.edge; return true;
    case kZeroParamId: value = prm.zero; return true;
    case kPolarParamId: value = prm.polar; return true;
    case kBitsParamId: value = prm.bits; return true;
    case kStepParamId: value = prm.step; return true;
    case kTransParamId: value = prm.trans; return true;
    case kTapeParamId: value = prm.tape; return true;
    case kSpeedParamId: value = prm.speed; return true;
    case kMeshCouplingParamId: value = mesh.coupling; return true;
    case kMeshTensionParamId: value = mesh.tension; return true;
    case kMeshDecayParamId: value = mesh.decay; return true;
    case kMeshDampingParamId: value = mesh.damping; return true;
    case kMixParamId: value = prm.mix; return true;
    case kGainParamId: value = prm.gainDb; return true;
    case kSafetyParamId: value = prm.safety; return true;
    case kTopologyShapeParamId: value = t.shape; return true;
    case kTopologyAmountParamId: value = t.amount; return true;
    case kTopologySeedParamId: value = t.jitter; return true;
    case kTopologyPullParamId: value = t.collapse; return true;
    case kTopologyXParamId: value = t.dirX; return true;
    case kTopologyYParamId: value = t.dirY; return true;
    case kTopologyZParamId: value = t.dirZ; return true;
    case kTopologyTwistParamId: value = t.twist; return true;
    case kTopologyFlareParamId: value = t.flare; return true;
    case kTopologyMotionParamId: value = t.motionMode; return true;
    case kTopologyVariantParamId: value = t.motionVariant; return true;
    case kTopologyRateParamId: value = t.motionRateHz; return true;
    case kTopologyDepthParamId: value = t.motionDepth; return true;
    case kTopologyNeighborsParamId: value = t.neighborCount; return true;
    case kTopologyRadiusParamId: value = t.neighborRadius; return true;
    case kTopologyCentroidParamId: value = t.centroidAmount; return true;
    default: return false;
    }
}

void storeSettingsInParameterBank(
    Plugin& p,
    const s3g::WaveGeometrySettings& settings)
{
    for (const clap_id id : kStoredParamIds) {
        double value = 0.0;
        if (settingsParamValue(settings, id, value)) {
            p.parameterValues[id].store(value, std::memory_order_relaxed);
        }
    }
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
}

void loadSettingsFromParameterBank(
    const Plugin& p,
    s3g::WaveGeometrySettings& settings)
{
    const double phase = settings.topology.motionPhase;
    for (const clap_id id : kStoredParamIds) {
        assignSettingsParam(
            settings,
            id,
            p.parameterValues[id].load(std::memory_order_relaxed));
    }
    settings.topology.motionPhase = phase;
}

void syncGuiSettings(Plugin& p)
{
    loadSettingsFromParameterBank(p, p.settings);
    p.settings.topology.motionPhase = p.publishedMotionPhase.load(
        std::memory_order_relaxed);
}

void applyLaneParams(
    Plugin& p,
    const s3g::WaveGeometrySettings& settings)
{
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p.processor.setLaneParams(
            ch,
            s3g::waveGeometryLaneParams(settings, ch, kChannelCount));
    }
}

void applyProcessorSettings(
    Plugin& p,
    const s3g::WaveGeometrySettings& settings)
{
    applyLaneParams(p, settings);
    p.processor.setMeshParams(settings.mesh);
    p.processor.setTopology(settings.topology);
}

void syncAudioSettings(Plugin& p, bool force = false)
{
    const uint64_t revision = p.parameterRevision.load(
        std::memory_order_acquire);
    if (!force && revision == p.audioParameterRevision) return;
    loadSettingsFromParameterBank(p, p.audioSettings);
    if (p.motionPhaseRestorePending.exchange(
            false, std::memory_order_acq_rel)) {
        p.audioSettings.topology.motionPhase =
            p.publishedMotionPhase.load(std::memory_order_relaxed);
    }
    p.audioParameterRevision = revision;
    applyProcessorSettings(p, p.audioSettings);
    p.publishedMeshTailFrames.store(
        p.processor.meshTailRemainingFrames(),
        std::memory_order_release);
}

bool topologyMotionActive(const Plugin& p)
{
    return s3g::topologyMotionActive(p.audioSettings.topology);
}

void advanceTopologyMotion(Plugin& p, uint32_t frames)
{
    if (!topologyMotionActive(p) || p.sampleRate <= 0.0 || frames == 0u) return;
    auto& t = p.audioSettings.topology;
    t.motionPhase += (static_cast<double>(frames) / p.sampleRate)
        * t.motionRateHz;
    t.motionPhase -= std::floor(t.motionPhase);
    p.publishedMotionPhase.store(t.motionPhase, std::memory_order_relaxed);
    applyLaneParams(p, p.audioSettings);
    p.processor.setTopology(t);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id >= kParameterBankSize) return;
    s3g::WaveGeometrySettings singleValue {};
    if (!assignSettingsParam(singleValue, id, value)) return;
    double sanitized = 0.0;
    if (!settingsParamValue(singleValue, id, sanitized)) return;
    p.parameterValues[id].store(sanitized, std::memory_order_relaxed);
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (paramAffectsTail(id)) markTailChanged(p);
}

void preparePatch(Plugin& p)
{
    p.patch.setWidth(kChannelCount);
    bool hasPatch = false;
    for (uint32_t row = 0; row < kChannelCount; ++row) {
        if (p.patch.rowMask(row) != 0) {
            hasPatch = true;
            break;
        }
    }
    if (!hasPatch) {
        p.patch.setIdentity(kChannelCount);
    }
    for (uint32_t row = 0; row < kChannelCount; ++row) {
        p.patchRowsPublished[row].store(
            p.patch.rowMask(row), std::memory_order_release);
    }
}

void togglePatchCellFromGui(Plugin& p, uint32_t input, uint32_t output)
{
    p.patch.setWidth(kChannelCount);
    p.patch.toggle(input, output);
    preparePatch(p);
}

uint64_t injectedPatchOutputMask(const Plugin& p)
{
    uint64_t mask = 0u;
    for (uint32_t input = 0; input < kChannelCount; ++input) {
        mask |= p.patch.rowMask(input);
    }
    return mask & ((uint64_t { 1 } << kChannelCount) - 1u);
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
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(p->macRealtimeActivity);
#endif
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->input32.assign(kChannelCount, std::vector<float>(p->maxFrames, 0.0f));
    p->output32.assign(kChannelCount, std::vector<float>(p->maxFrames, 0.0f));
    p->inputPtrs.assign(kChannelCount, nullptr);
    p->outputPtrs.assign(kChannelCount, nullptr);
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p->inputPtrs[ch] = p->input32[ch].data();
        p->outputPtrs[ch] = p->output32[ch].data();
    }
    preparePatch(*p);
    if (!p->processor.prepare(sampleRate, kChannelCount, 0u, 0u, p->maxFrames)) return false;
    syncAudioSettings(*p, true);
    p->publishedMeshTailFrames.store(
        p->processor.meshTailRemainingFrames(),
        std::memory_order_release);
    for (uint32_t source = 0; source < kChannelCount; ++source) {
        for (uint32_t destination = 0; destination < kChannelCount; ++destination) {
            p->meshEdgeEnergy[source][destination].store(0.0f, std::memory_order_relaxed);
            p->meshEdgePhase[source][destination].store(0.0f, std::memory_order_relaxed);
        }
    }
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#else
    (void)plugin;
#endif
}
bool startProcessing(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    syncAudioSettings(*p, true);
    deliverTailChangedOnAudioThread(*p);
    return true;
}
void stopProcessing(const clap_plugin_t* plugin)
{
    deliverTailChangedOnAudioThread(*self(plugin));
}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->processor.reset();
    p->publishedMeshTailFrames.store(0u, std::memory_order_release);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    for (uint32_t source = 0; source < kChannelCount; ++source) {
        for (uint32_t destination = 0; destination < kChannelCount; ++destination) {
            p->meshEdgeEnergy[source][destination].store(0.0f, std::memory_order_relaxed);
            p->meshEdgePhase[source][destination].store(0.0f, std::memory_order_relaxed);
        }
    }
    deliverTailChangedOnAudioThread(*p);
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
    syncAudioSettings(*p);
    deliverTailChangedOnAudioThread(*p);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = std::min(proc->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kChannelCount) return CLAP_PROCESS_CONTINUE;

    advanceTopologyMotion(*p, frames);

    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        std::fill(p->input32[lane].begin(), p->input32[lane].begin() + frames, 0.0f);
        const uint64_t laneBit = uint64_t { 1 } << lane;
        for (uint32_t inCh = 0; inCh < kChannelCount; ++inCh) {
            const uint64_t rowMask = p->patchRowsPublished[inCh].load(
                std::memory_order_acquire);
            if ((rowMask & laneBit) == 0) continue;
            for (uint32_t i = 0; i < frames; ++i) {
                if (inCh < input.channel_count && input.data32 && input.data32[inCh]) p->input32[lane][i] += input.data32[inCh][i];
                else if (inCh < input.channel_count && input.data64 && input.data64[inCh]) p->input32[lane][i] += static_cast<float>(input.data64[inCh][i]);
            }
        }
    }
    p->processor.process(p->inputPtrs.data(), kChannelCount, p->outputPtrs.data(), kChannelCount, frames);
    p->publishedMeshTailFrames.store(
        p->processor.meshTailRemainingFrames(),
        std::memory_order_release);
    for (uint32_t source = 0; source < kChannelCount; ++source) {
        for (uint32_t destination = 0; destination < kChannelCount; ++destination) {
            p->meshEdgeEnergy[source][destination].store(
                p->processor.edgeEnergy(source, destination),
                std::memory_order_relaxed);
            p->meshEdgePhase[source][destination].store(
                p->processor.edgePhase(source, destination),
                std::memory_order_relaxed);
        }
    }

    float blockPeak = 0.0f;
    const uint32_t scopeBase = p->scopeWrite.load(std::memory_order_relaxed);
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float v = p->output32[ch][i];
            p->scope[ch][(scopeBase + i) % kScopeFrames].store(v, std::memory_order_relaxed);
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = v;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(v);
            blockPeak = std::max(blockPeak, std::abs(v));
        }
    }
    for (uint32_t ch = kChannelCount; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
    }
    p->scopeWrite.store((scopeBase + frames) % kScopeFrames, std::memory_order_relaxed);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "8ch %s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kFoldParamId, "Fold", 0.0, 1.0, 0.22 },
    { kDriveParamId, "Drive", 0.0, 1.0, 0.18 },
    { kHoldParamId, "Hold", 0.0, 1.0, 0.0 },
    { kClipParamId, "Clip", 0.0, 1.0, 0.18 },
    { kRectifyParamId, "Rectify", 0.0, 1.0, 0.0 },
    { kEdgeParamId, "Edge", 0.0, 1.0, 0.0 },
    { kZeroParamId, "Zero Drop", 0.0, 0.78, 0.0 },
    { kPolarParamId, "Polarity", 0.0, 1.0, 0.0 },
    { kBitsParamId, "Bit Collapse", 0.0, 0.92, 0.0 },
    { kStepParamId, "Sample Step", 0.0, 1.0, 0.0 },
    { kTransParamId, "Transform", -1.0, 1.0, 0.0 },
    { kTapeParamId, "Dual Tape Heads", 0.0, 1.0, 0.0 },
    { kSpeedParamId, "Tape Head Speed", 0.0, 1.0, 0.25 },
    { kMeshCouplingParamId, "Mesh Coupling", 0.0, 1.0, 0.0 },
    { kMeshTensionParamId, "Mesh Tension", 0.0, 1.0, 0.62 },
    { kMeshDecayParamId, "Mesh Decay", 0.0, 1.0, 0.35 },
    { kMeshDampingParamId, "Mesh Damping", 0.0, 1.0, 0.45 },
    { kMixParamId, "Mix", 0.0, 1.0, 1.0 },
    { kGainParamId, "Output", -60.0, 12.0, -3.0 },
    { kSafetyParamId, "Safety", 0.12, 0.92, 0.82 },
    { kTopologyShapeParamId, "Topology Shape", 0.0, static_cast<double>(s3g::kTopologyShapeCount - 1u), 0.0 },
    { kTopologyAmountParamId, "Topology Amount", 0.0, 1.0, 0.35 },
    { kTopologySeedParamId, "Topology Seed", 0.0, 1.0, 0.08 },
    { kTopologyPullParamId, "Topology Pull", 0.0, 1.0, 0.0 },
    { kTopologyXParamId, "Topology X", -1.0, 1.0, 0.0 },
    { kTopologyYParamId, "Topology Y", -1.0, 1.0, 0.0 },
    { kTopologyZParamId, "Topology Z", -1.0, 1.0, 1.0 },
    { kTopologyTwistParamId, "Topology Twist", -1.0, 1.0, 0.08 },
    { kTopologyFlareParamId, "Topology Flare", -1.0, 1.0, 0.0 },
    { kTopologyMotionParamId, "Topology Motion", 0.0, static_cast<double>(s3g::kTopologyMotionModeCount - 1u), 0.0 },
    { kTopologyVariantParamId, "Topology Variant", 0.0, static_cast<double>(s3g::kTopologyVariantCount - 1u), 0.0 },
    { kTopologyRateParamId, "Topology Rate", kMotionRateMinHz, kMotionRateMaxHz, 0.08 },
    { kTopologyDepthParamId, "Topology Depth", 0.0, 1.0, 0.0 },
    { kTopologyNeighborsParamId, "Topology Neighbors", 1.0, 3.0, 2.0 },
    { kTopologyRadiusParamId, "Topology Radius", 0.0, 1.0, 0.65 },
    { kTopologyCentroidParamId, "Topology Centroid", 0.0, 1.0, 0.18 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    const char* module = def.id >= kMeshCouplingParamId
            && def.id <= kMeshDampingParamId
        ? "Wave Mesh"
        : (def.id < kTopologyShapeParamId ? "Wave Engine" : "Topology");
    std::strncpy(info->module, module, sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || id >= kParameterBankSize) return false;
    const auto& p = *self(plugin);
    s3g::WaveGeometrySettings probe {};
    double ignored = 0.0;
    if (!settingsParamValue(probe, id, ignored)) return false;
    *value = p.parameterValues[id].load(std::memory_order_relaxed);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTransParamId || id == kTopologyXParamId || id == kTopologyYParamId ||
             id == kTopologyZParamId || id == kTopologyTwistParamId || id == kTopologyFlareParamId) std::snprintf(display, size, "%+.2f", value);
    else if (id == kTapeParamId || id == kSpeedParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kTopologyShapeParamId) std::snprintf(display, size, "%s", s3g::topologyShapeName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyMotionParamId) std::snprintf(display, size, "%s", s3g::topologyMotionModeName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyVariantParamId) std::snprintf(display, size, "%s", s3g::topologyVariantName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyNeighborsParamId) std::snprintf(display, size, "%.0fNN", value);
    else if (id == kTopologyRateParamId) std::snprintf(display, size, "%.2f Hz", value);
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    if (std::strchr(display, '%')) *value *= 0.01;
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

template <typename State>
bool readStateRemainder(const clap_istream_t* stream, uint32_t version, State& state)
{
    state.version = version;
    auto* cursor = reinterpret_cast<uint8_t*>(&state) + sizeof(state.version);
    uint64_t remaining = sizeof(state) - sizeof(state.version);
    while (remaining > 0u) {
        const int64_t read = stream->read(stream, cursor, remaining);
        if (read <= 0 || static_cast<uint64_t>(read) > remaining) return false;
        cursor += read;
        remaining -= static_cast<uint64_t>(read);
    }
    return true;
}

bool writeStateBytes(const clap_ostream_t* stream, const void* data, uint64_t size)
{
    auto* cursor = static_cast<const uint8_t*>(data);
    while (size > 0u) {
        const int64_t written = stream->write(stream, cursor, size);
        if (written <= 0 || static_cast<uint64_t>(written) > size) return false;
        cursor += written;
        size -= static_cast<uint64_t>(written);
    }
    return true;
}

bool readStateBytes(const clap_istream_t* stream, void* data, uint64_t size)
{
    auto* cursor = static_cast<uint8_t*>(data);
    while (size > 0u) {
        const int64_t read = stream->read(stream, cursor, size);
        if (read <= 0 || static_cast<uint64_t>(read) > size) return false;
        cursor += read;
        size -= static_cast<uint64_t>(read);
    }
    return true;
}

s3g::TopologyState migrateLegacyTopology(const LegacyTopologyStateV3& legacy)
{
    s3g::TopologyState topology {};
    topology.amount = legacy.amount;
    topology.jitter = legacy.jitter;
    topology.collapse = legacy.collapse;
    topology.dirX = legacy.dirX;
    topology.dirY = legacy.dirY;
    topology.dirZ = legacy.dirZ;
    topology.twist = legacy.twist;
    topology.flare = legacy.flare;
    topology.shape = legacy.shape;
    topology.motionMode = legacy.motionMode;
    topology.motionVariant = legacy.motionVariant;
    topology.motionRateHz = legacy.motionRateHz;
    topology.motionDepth = legacy.motionDepth;
    topology.motionPhase = legacy.motionPhase;
    topology.neighborCount = legacy.neighborCount;
    topology.neighborRadius = legacy.neighborRadius;
    topology.centroidAmount = legacy.centroidAmount;
    return topology;
}

s3g::WaveGeometrySettings migrateLegacySettings(
    const LegacyWaveGeometrySettingsV3& legacy)
{
    s3g::WaveGeometrySettings settings {};
    auto& base = settings.base;
    base.fold = legacy.base.fold;
    base.drive = legacy.base.drive;
    base.hold = legacy.base.hold;
    base.clip = legacy.base.clip;
    base.rectify = legacy.base.rectify;
    base.edge = legacy.base.edge;
    base.zero = legacy.base.zero;
    base.polar = legacy.base.polar;
    base.bits = legacy.base.bits;
    base.step = legacy.base.step;
    base.trans = legacy.base.trans;
    base.tape = legacy.base.tape;
    base.speed = legacy.base.speed;
    base.mix = legacy.base.mix;
    base.gainDb = legacy.base.gainDb;
    base.safety = legacy.base.safety;
    settings.topology = migrateLegacyTopology(legacy.topology);
    settings.mesh.coupling = 0.0f;
    return settings;
}

s3g::WaveGeometrySettings migrateLegacySettings(
    const LegacyWaveGeometrySettingsV2& legacy)
{
    s3g::WaveGeometrySettings settings {};
    auto& base = settings.base;
    base.fold = legacy.base.fold;
    base.drive = legacy.base.drive;
    base.hold = legacy.base.hold;
    base.clip = legacy.base.clip;
    base.rectify = legacy.base.rectify;
    base.edge = legacy.base.edge;
    base.zero = legacy.base.zero;
    base.polar = legacy.base.polar;
    base.bits = legacy.base.bits;
    base.step = legacy.base.step;
    base.trans = legacy.base.trans;
    base.mix = legacy.base.mix;
    base.gainDb = legacy.base.gainDb;
    base.safety = legacy.base.safety;
    settings.topology = migrateLegacyTopology(legacy.topology);
    settings.mesh.coupling = 0.0f;
    return settings;
}

void sanitizeMeshSettings(s3g::WaveGeometrySettings& settings)
{
    settings.base = s3g::sanitizeWaveGeometryParams(settings.base);
    settings.mesh.coupling = std::clamp(settings.mesh.coupling, 0.0f, 1.0f);
    settings.mesh.tension = std::clamp(settings.mesh.tension, 0.0f, 1.0f);
    settings.mesh.decay = std::clamp(settings.mesh.decay, 0.0f, 1.0f);
    settings.mesh.damping = std::clamp(settings.mesh.damping, 0.0f, 1.0f);
    if (!std::isfinite(settings.topology.motionPhase)) {
        settings.topology.motionPhase = 0.0;
    } else {
        settings.topology.motionPhase -= std::floor(
            settings.topology.motionPhase);
    }
}

template <typename State>
void restorePatch(Plugin& p, const State& state)
{
    p.patch.setWidth(kChannelCount);
    for (uint32_t row = 0; row < kChannelCount; ++row) {
        p.patch.setRowMask(row, state.patchRows[row]);
    }
    preparePatch(p);
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {};
    auto* p = self(plugin);
    syncGuiSettings(*p);
    s.settings = p->settings;
    for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
        s.patchRows[row] = p->patch.rowMask(row);
    }
    return writeStateBytes(stream, &s, sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0;
    if (!readStateBytes(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    if (version == kStateVersion) {
        SavedState s {};
        if (!readStateRemainder(stream, version, s)) return false;
        p->settings = s.settings;
        restorePatch(*p, s);
    } else if (version == 3u) {
        SavedStateV3 s {};
        if (!readStateRemainder(stream, version, s)) return false;
        p->settings = migrateLegacySettings(s.settings);
        restorePatch(*p, s);
    } else if (version == 2u) {
        SavedStateV2 s {};
        if (!readStateRemainder(stream, version, s)) return false;
        p->settings = migrateLegacySettings(s.settings);
        restorePatch(*p, s);
    } else if (version == 1u) {
        SavedStateV1 s {};
        if (!readStateRemainder(stream, version, s)) return false;
        p->settings = migrateLegacySettings(s.settings);
        p->patch.setIdentity(kChannelCount);
        preparePatch(*p);
    } else {
        return false;
    }
    sanitizeMeshSettings(p->settings);
    p->publishedMotionPhase.store(
        p->settings.topology.motionPhase,
        std::memory_order_relaxed);
    p->motionPhaseRestorePending.store(true, std::memory_order_release);
    storeSettingsInParameterBank(*p, p->settings);
    markTailChanged(*p);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };
uint32_t latencyGet(const clap_plugin_t* plugin) { return self(plugin)->processor.latencyFrames(); }
const clap_plugin_latency_t latencyExt { latencyGet };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p) return 0u;
    // Topology can raise the effective per-lane tape amount above the base
    // TAPE control, and a recently lowered control can still leave smoothed
    // tape output. The allocated 1.5-second ring is the safe upper bound.
    const uint64_t tapeTail = static_cast<uint64_t>(
        std::ceil(std::max(1.0, p->sampleRate) * 1.5));
    uint64_t meshTail = p->publishedMeshTailFrames.load(
        std::memory_order_acquire);
    const double meshCoupling = p->parameterValues[kMeshCouplingParamId].load(
        std::memory_order_relaxed);
    const double meshDecay = p->parameterValues[kMeshDecayParamId].load(
        std::memory_order_relaxed);
    if (meshCoupling > 0.0001) {
        // tail.get() is valid before activate(), when the processor has not yet
        // built topology-dependent delay targets. Use the mesh's maximum route
        // as a conservative pre-activation estimate, then retain whichever
        // estimate is longer after activation.
        const double sampleRate = std::max(1.0, p->sampleRate);
        const double maximumRoute = std::ceil(sampleRate * 0.040);
        const double reflection = 0.96 * std::pow(
            std::clamp(meshDecay, 0.0, 1.0),
            1.35);
        const double traversals = reflection <= 0.0001
            ? 1.0
            : std::max(2.0, std::ceil(
                std::log(0.0001) / std::log(std::min(0.999, reflection))) + 1.0);
        const uint64_t conservative = static_cast<uint64_t>(
            std::ceil(maximumRoute * traversals));
        meshTail = std::max(meshTail, conservative);
    }
    constexpr uint64_t kClapTailInfinite =
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
    // The lane tape feeds the mesh rather than running beside it, so the
    // worst-case decay is serial. `publishedMeshTailFrames` also preserves
    // the draining network after the target COUP value reaches zero.
    return static_cast<uint32_t>(std::min(
        tapeTail + meshTail, kClapTailInfinite - 1u));
}
const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
@interface S3GWaveGeometryView : NSView {
    void* _plugin;
    int _dragParam;
    bool _dragTopologyView;
    NSPoint _lastDragPoint;
    double _viewYaw;
    double _viewPitch;
    int _cameraView;
    NSTimer* _timer;
    bool _showReadout;
    int _fieldPage;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItemCount;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawEngineRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawScope:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (NSRect)fieldPageButtonRect:(NSRect)rect index:(int)index;
- (void)setTopologyView:(uint32_t)view;
- (void)updateDrag:(NSPoint)point;
- (void)updateMenuHover:(NSPoint)point;
@end

@implementation S3GWaveGeometryView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = 0;
        _dragTopologyView = false;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewYaw = -0.52;
        _viewPitch = 0.34;
        _cameraView = 2;
        _timer = nil;
        _showReadout = false;
        _fieldPage = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSMakePoint(0, 0);
        _menuItemCount = 0;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) {
        [self removeTrackingArea:area];
    }
    [super updateTrackingAreas];
    NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0/24.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}
- (void)drawEngineRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y, kPrimaryPanelX, kPanelWidth,
        attrs, small, style);
}
- (NSRect)fieldPageButtonRect:(NSRect)rect index:(int)index
{
    return s3g::clap_gui::topologyProcessorFieldPageButtonRect(
        rect, static_cast<uint32_t>(index));
}
- (void)setTopologyView:(uint32_t)view
{
    _cameraView = static_cast<int>(std::min<uint32_t>(view, 2u));
    if (view == 0u) {
        _viewYaw = 0.0;
        _viewPitch = 0.95;
    } else if (view == 1u) {
        _viewYaw = -1.57079632679;
        _viewPitch = 0.0;
    } else {
        _viewYaw = -0.52;
        _viewPitch = 0.34;
    }
    [self setNeedsDisplay:YES];
}
- (NSPoint)projectPoint:(s3g::TopologyPoint)point inRect:(NSRect)rect
{
    const double cyaw = std::cos(_viewYaw);
    const double syaw = std::sin(_viewYaw);
    const double cp = std::cos(_viewPitch);
    const double sp = std::sin(_viewPitch);
    const double xr = point.x * cyaw - point.z * syaw;
    const double zr = point.x * syaw + point.z * cyaw;
    const double yr = point.y * cp - zr * sp;
    const double zz = point.y * sp + zr * cp;
    const double scale = 0.82 + zz * 0.08;
    return NSMakePoint(NSMidX(rect) + static_cast<CGFloat>(xr * rect.size.width * 0.25 * scale),
                       rect.origin.y + rect.size.height * 0.52 - static_cast<CGFloat>(yr * rect.size.height * 0.38 * scale));
}
- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.cellBg setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);
    [style.strip setFill]; NSRectFill(NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, 21.0));
    [style.accent setFill]; NSRectFill(NSMakeRect(rect.origin.x, rect.origin.y, rect.size.width, 2.0));
    [@"TOPOLOGY" drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + 5) withAttributes:attrs];
    NSString* pageLabels[2] = { @"TOPO", @"SCOPE" };
    for (int i = 0; i < 2; ++i) {
        NSRect button = [self fieldPageButtonRect:rect index:i];
        s3g::clap_gui::drawHeaderButton(button, rect, pageLabels[i], _fieldPage == i, small, style);
    }
    if (_fieldPage == 0) {
        s3g::clap_gui::drawTopologyProcessorCameraButtons(
            rect, _cameraView, small, style);
    }

    const NSRect fieldRect =
        s3g::clap_gui::topologyProcessorFieldContentRect(rect);
    if (_fieldPage == 1) {
        [self drawScope:fieldRect attrs:attrs small:small];
        return;
    }

    const auto state = topologyStateForPlugin(*p);
    const auto controls = s3g::topologyControlsFromState(state);
    // The patch matrix injects sources, but the mesh always retains all eight
    // nodes so an unpatched lane can receive a propagated wave.
    const uint32_t visualLanes = kChannelCount;
    const uint64_t injectedMask = injectedPatchOutputMask(*p);
    const NSRect topoRect = NSMakeRect(fieldRect.origin.x + 30.0, fieldRect.origin.y + 28.0, fieldRect.size.width - 60.0, 348.0);
    const NSRect heatRect = NSMakeRect(fieldRect.origin.x + 30.0, fieldRect.origin.y + 394.0, fieldRect.size.width - 60.0, 180.0);
    [style.strip setFill]; NSRectFill(fieldRect);
    [style.grid setStroke]; NSFrameRect(fieldRect);
    [s3g::clap_gui::color(0x101010, 1.0) setFill]; NSRectFill(topoRect);
    [style.grid setStroke]; NSFrameRect(topoRect);

    constexpr uint32_t cols = 54;
    constexpr uint32_t rows = 18;
    [s3g::clap_gui::color(0x090b0d, 1.0) setFill]; NSRectFill(heatRect);
    std::array<double, cols * rows> heat {};
    const double heatMax = s3g::fillTopologyHeatmap(state, visualLanes, cols, rows, heat.data());
    const CGFloat cellW = heatRect.size.width / static_cast<CGFloat>(cols);
    const CGFloat cellH = heatRect.size.height / static_cast<CGFloat>(rows);
    for (uint32_t y = 0; y < rows; ++y) {
        for (uint32_t x = 0; x < cols; ++x) {
            const size_t index = static_cast<size_t>(y) * cols + x;
            const double v = std::pow(std::clamp(heat[index] / heatMax, 0.0, 1.0), 0.72);
            [s3g::clap_gui::heatColor(v, 1.0) setFill];
            NSRectFill(NSMakeRect(heatRect.origin.x + static_cast<CGFloat>(x) * cellW,
                                  heatRect.origin.y + static_cast<CGFloat>(y) * cellH,
                                  cellW,
                                  cellH));
        }
    }

    std::array<float, kChannelCount> nodeEnergy {};
    if (p->settings.mesh.coupling > 0.0001f
        && state.centroidAmount > 0.0001) {
        const s3g::TopologyPoint centerPoint {};
        const NSPoint center = [self projectPoint:centerPoint inRect:topoRect];
        float centerEnergy = 0.0f;
        for (uint32_t lane = 0; lane < visualLanes; ++lane) {
            float routeEnergy = 0.0f;
            for (uint32_t other = 0; other < visualLanes; ++other) {
                if (other == lane) continue;
                routeEnergy = std::max({
                    routeEnergy,
                    p->meshEdgeEnergy[lane][other].load(std::memory_order_relaxed),
                    p->meshEdgeEnergy[other][lane].load(std::memory_order_relaxed)
                });
            }
            routeEnergy = std::clamp(routeEnergy, 0.0f, 1.0f);
            nodeEnergy[lane] = std::max(nodeEnergy[lane], routeEnergy);
            centerEnergy = std::max(centerEnergy, routeEnergy);
            const auto point = s3g::topologyPointForLane(
                lane, visualLanes, controls);
            const NSPoint node = [self projectPoint:point inRect:topoRect];
            const float level = std::sqrt(routeEnergy);
            [s3g::clap_gui::color(
                0x787878,
                0.10 + static_cast<float>(state.centroidAmount)
                    * (0.12 + level * 0.34)) setStroke];
            [NSBezierPath strokeLineFromPoint:center toPoint:node];
        }
        const CGFloat centerSize = 5.0
            + static_cast<CGFloat>(std::sqrt(centerEnergy)) * 8.0;
        [s3g::clap_gui::color(
            0xc0c0c0,
            0.28 + std::sqrt(centerEnergy) * 0.52) setFill];
        NSRectFill(NSMakeRect(
            center.x - centerSize * 0.5,
            center.y - centerSize * 0.5,
            centerSize,
            centerSize));
    }
    bool edgeDrawn[kChannelCount][kChannelCount] {};
    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const auto nn = s3g::nearestTopologyNeighbors(state, lane, visualLanes);
        for (uint32_t i = 0; i < std::min<uint32_t>(state.neighborCount, 3u); ++i) {
            if (nn[i] < 0 || static_cast<uint32_t>(nn[i]) >= visualLanes
                || static_cast<uint32_t>(nn[i]) == lane) continue;
            const uint32_t aIndex = std::min<uint32_t>(lane, static_cast<uint32_t>(nn[i]));
            const uint32_t bIndex = std::max<uint32_t>(lane, static_cast<uint32_t>(nn[i]));
            if (edgeDrawn[aIndex][bIndex]) continue;
            edgeDrawn[aIndex][bIndex] = true;

            const auto aPoint = s3g::topologyPointForLane(aIndex, visualLanes, controls);
            const auto bPoint = s3g::topologyPointForLane(bIndex, visualLanes, controls);
            const NSPoint a = [self projectPoint:aPoint inRect:topoRect];
            const NSPoint b = [self projectPoint:bPoint inRect:topoRect];
            const float energyAB = std::clamp(
                p->meshEdgeEnergy[aIndex][bIndex].load(std::memory_order_relaxed),
                0.0f, 1.0f);
            const float energyBA = std::clamp(
                p->meshEdgeEnergy[bIndex][aIndex].load(std::memory_order_relaxed),
                0.0f, 1.0f);
            const float edgeLevel = std::sqrt(std::max(energyAB, energyBA));
            nodeEnergy[aIndex] = std::max(nodeEnergy[aIndex], std::max(energyAB, energyBA));
            nodeEnergy[bIndex] = std::max(nodeEnergy[bIndex], std::max(energyAB, energyBA));

            if (p->settings.mesh.coupling <= 0.0001f) {
                [s3g::clap_gui::color(0xb8b8b8, 0.60) setStroke];
            } else {
                const int gray = 0x58 + static_cast<int>(edgeLevel * 0x60);
                const uint32_t rgb = static_cast<uint32_t>((gray << 16) | (gray << 8) | gray);
                [s3g::clap_gui::color(rgb, 0.42 + edgeLevel * 0.48) setStroke];
            }
            [NSBezierPath strokeLineFromPoint:a toPoint:b];

            auto drawTravelMarker = [&](uint32_t source, uint32_t destination,
                                        NSPoint from, NSPoint to, float energy) {
                if (p->settings.mesh.coupling <= 0.0001f || energy <= 0.0004f) return;
                const float phase = std::clamp(
                    p->meshEdgePhase[source][destination].load(std::memory_order_relaxed),
                    0.0f, 1.0f);
                const float level = std::sqrt(energy);
                const CGFloat markerSize = 2.0 + static_cast<CGFloat>(level) * 4.0;
                const NSPoint marker = NSMakePoint(
                    from.x + (to.x - from.x) * static_cast<CGFloat>(phase),
                    from.y + (to.y - from.y) * static_cast<CGFloat>(phase));
                [s3g::clap_gui::color(0xd0d0d0, 0.30 + level * 0.70) setFill];
                NSRectFill(NSMakeRect(
                    marker.x - markerSize * 0.5,
                    marker.y - markerSize * 0.5,
                    markerSize,
                    markerSize));
            };
            drawTravelMarker(aIndex, bIndex, a, b, energyAB);
            drawTravelMarker(bIndex, aIndex, b, a, energyBA);
        }
    }
    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const auto pt = s3g::topologyPointForLane(lane, visualLanes, controls);
        const NSPoint c = [self projectPoint:pt inRect:topoRect];
        const CGFloat size = 8.0 + static_cast<CGFloat>(std::clamp(pt.radius, 0.0, 1.5)) * 2.0;
        const float activity = std::sqrt(std::clamp(nodeEnergy[lane], 0.0f, 1.0f));
        if (p->settings.mesh.coupling > 0.0001f && activity > 0.015f) {
            const CGFloat haloSize = size + 5.0 + static_cast<CGFloat>(activity) * 15.0;
            [s3g::clap_gui::color(0xb8b8b8, 0.18 + activity * 0.52) setStroke];
            NSFrameRect(NSMakeRect(
                c.x - haloSize * 0.5, c.y - haloSize * 0.5,
                haloSize, haloSize));
        }
        const bool injected = (injectedMask & (uint64_t { 1 } << lane)) != 0u;
        if (injected) [style.text setFill];
        else [s3g::clap_gui::color(0x626262, 0.82) setFill];
        NSRectFill(NSMakeRect(c.x - size * 0.5, c.y - size * 0.5, size, size));
        [[NSString stringWithFormat:@"L%u", lane + 1u] drawAtPoint:NSMakePoint(c.x + 7.0, c.y - 6.0) withAttributes:small];
    }

    NSString* topologyName = p->settings.mesh.coupling > 0.0001f
        ? @"8PT WAVE MESH" : @"8PT NEIGHBOR MAP";
    [topologyName drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 10.0) withAttributes:small];
    [[NSString stringWithFormat:@"SHAPE = %@", [NSString stringWithUTF8String:s3g::topologyShapeName(state.shape)]]
        drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 25.0)
      withAttributes:small];
    NSString* xyzText = state.shape == 0u ? @"XYZ = FOLD STEP POL" : @"XYZ = FOLD ZERO BITS";
    [xyzText drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 40.0) withAttributes:small];

    NSRect readoutButton = NSMakeRect(fieldRect.origin.x + fieldRect.size.width - 42.0, fieldRect.origin.y + 54.0, 32.0, 15.0);
    [style.strip setFill];
    NSRectFill(readoutButton);
    [style.grid setStroke];
    NSFrameRect(readoutButton);
    if (_showReadout) {
        [@"X" drawAtPoint:NSMakePoint(readoutButton.origin.x + 12.0, readoutButton.origin.y + 1.0) withAttributes:small];
        [@"FLD STP BIT POL" drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 55.0) withAttributes:small];
        for (uint32_t lane = 0; lane < visualLanes; ++lane) {
            const auto laneParams = s3g::waveGeometryLaneParams(p->settings, lane, visualLanes);
            NSString* line = [NSString stringWithFormat:@"L%u %3.0f %.2f %.2f %.2f",
                                        lane + 1u,
                                        laneParams.fold,
                                        laneParams.step,
                                        laneParams.bits,
                                        laneParams.polar];
            [line drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0,
                                          fieldRect.origin.y + 70.0 + lane * 15.0)
                withAttributes:small];
        }
    } else {
        [@"LST" drawAtPoint:NSMakePoint(readoutButton.origin.x + 6.0, readoutButton.origin.y + 1.0) withAttributes:small];
    }

}
- (void)drawScope:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    (void)attrs;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    const NSRect scopeRect = rect;
    [style.strip setFill];
    NSRectFill(scopeRect);
    [style.grid setStroke];
    NSFrameRect(scopeRect);
    [@"POST PROCESSING OSCILLOSCOPE" drawAtPoint:NSMakePoint(scopeRect.origin.x + 10.0, scopeRect.origin.y + 8.0) withAttributes:small];

    const uint32_t lanes = kChannelCount;
    const auto channelGrid =
        s3g::clap_gui::topologyProcessorChannelGrid(scopeRect, lanes);
    const uint32_t write = p->scopeWrite.load(std::memory_order_relaxed);
    for (uint32_t lane = 0; lane < lanes; ++lane) {
        const NSRect laneRect =
            s3g::clap_gui::topologyProcessorChannelRect(
                channelGrid, lane);
        [s3g::clap_gui::color(0x101010, 1.0) setFill];
        NSRectFill(laneRect);
        [style.grid setStroke];
        NSFrameRect(laneRect);
        [s3g::clap_gui::color(0x2f2f2f, 0.8) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(laneRect.origin.x, NSMidY(laneRect))
                                  toPoint:NSMakePoint(NSMaxX(laneRect), NSMidY(laneRect))];

        float peak = 0.0001f;
        for (uint32_t i = 0; i < kScopeFrames; ++i) {
            peak = std::max(peak, std::fabs(p->scope[lane][i].load(std::memory_order_relaxed)));
        }
        const float scale = std::min(4.0f, 0.92f / peak);
        NSBezierPath* path = [NSBezierPath bezierPath];
        for (uint32_t i = 0; i < kScopeFrames; ++i) {
            const uint32_t index = (write + i) % kScopeFrames;
            const float sample = p->scope[lane][index].load(std::memory_order_relaxed);
            const CGFloat x = laneRect.origin.x + (static_cast<CGFloat>(i) / static_cast<CGFloat>(kScopeFrames - 1u)) * laneRect.size.width;
            const CGFloat y = NSMidY(laneRect) - static_cast<CGFloat>(std::clamp(sample * scale, -1.0f, 1.0f)) * laneRect.size.height * 0.42;
            if (i == 0u) [path moveToPoint:NSMakePoint(x, y)];
            else [path lineToPoint:NSMakePoint(x, y)];
        }
        [style.text setStroke];
        [path stroke];
        [[NSString stringWithFormat:@"L%u", lane + 1u] drawAtPoint:NSMakePoint(laneRect.origin.x + 5.0, laneRect.origin.y + 4.0) withAttributes:small];
    }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    syncGuiSettings(*p);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* lab = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSString* titleText = [NSString stringWithFormat:
        @"s3g PROCESSOR WAVE GEOMETRY %uCH", kChannelCount];
    s3g::clap_gui::drawProcessorTitleBand(
        titleText,
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight),
        titleAttrs, lab, small, style);

    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform* contentTransform = [NSAffineTransform transform];
    [contentTransform translateXBy:0.0 yBy:kContentTranslation];
    [contentTransform concat];

    const auto& fieldLayout =
        s3g::gui_layout::kTopologyProcessorColumns.field;
    [self drawField:NSMakeRect(
        fieldLayout.x,
        fieldLayout.y - kContentTranslation,
        fieldLayout.width,
        fieldLayout.height)
        attrs:lab small:small];

    const CGFloat panelX = kPrimaryPanelX;
    const CGFloat topologyX = kSecondaryPanelX;
    const CGFloat panelW = kPanelWidth;
    const CGFloat headerH = 21.0;
    const CGFloat gap =
        s3g::gui_layout::kStandardMetrics.panelGap;
    CGFloat panelY = 34.0;
    auto drawHeader = [&](NSString* title, CGFloat x, CGFloat y) {
        s3g::clap_gui::drawPanelHeader(
            title, true, x, y, panelW, headerH, lab, style);
    };

    const CGFloat outputH = 80.0;
    s3g::clap_gui::drawPanelFrame(panelX, panelY, panelW, outputH, style);
    s3g::clap_gui::drawPanelHeader(
        @"OUTPUT", true, panelX, panelY, panelW, headerH, lab, style);
    const auto& prm = p->settings.base;
    [self drawEngineRow:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB", prm.gainDb]
        norm:(prm.gainDb + 60.0f) / 72.0f y:engineRowY(panelY, 0) attrs:small small:small];
    [self drawEngineRow:@"MIX" value:[NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f]
        norm:prm.mix y:engineRowY(panelY, 1) attrs:small small:small];
    panelY += outputH + gap;

    const CGFloat engineH =
        static_cast<CGFloat>(kEnginePanelHeight);
    s3g::clap_gui::drawPanelFrame(panelX, panelY, panelW, engineH, style);
    drawHeader(@"WAVE ENGINE", panelX, panelY);
    [self drawEngineRow:@"FOLD" value:[NSString stringWithFormat:@"%.0f%%", prm.fold * 100.0f] norm:prm.fold y:engineRowY(panelY, 0) attrs:small small:small];
    [self drawEngineRow:@"DRIV" value:[NSString stringWithFormat:@"%.0f%%", prm.drive * 100.0f] norm:prm.drive y:engineRowY(panelY, 1) attrs:small small:small];
    [self drawEngineRow:@"HOLD" value:[NSString stringWithFormat:@"%.0f%%", prm.hold * 100.0f] norm:prm.hold y:engineRowY(panelY, 2) attrs:small small:small];
    [self drawEngineRow:@"CLIP" value:[NSString stringWithFormat:@"%.0f%%", prm.clip * 100.0f] norm:prm.clip y:engineRowY(panelY, 3) attrs:small small:small];
    [self drawEngineRow:@"RECT" value:[NSString stringWithFormat:@"%.0f%%", prm.rectify * 100.0f] norm:prm.rectify y:engineRowY(panelY, 4) attrs:small small:small];
    [self drawEngineRow:@"EDGE" value:[NSString stringWithFormat:@"%.0f%%", prm.edge * 100.0f] norm:prm.edge y:engineRowY(panelY, 5) attrs:small small:small];
    [self drawEngineRow:@"ZERO" value:[NSString stringWithFormat:@"%.0f%%", prm.zero * 100.0f] norm:prm.zero / 0.78f y:engineRowY(panelY, 6) attrs:small small:small];
    [self drawEngineRow:@"POL" value:[NSString stringWithFormat:@"%.0f%%", prm.polar * 100.0f] norm:prm.polar y:engineRowY(panelY, 7) attrs:small small:small];
    [self drawEngineRow:@"BITS" value:[NSString stringWithFormat:@"%.0f%%", prm.bits * 100.0f] norm:prm.bits / 0.92f y:engineRowY(panelY, 8) attrs:small small:small];
    [self drawEngineRow:@"STEP" value:[NSString stringWithFormat:@"%.0f%%", prm.step * 100.0f] norm:prm.step y:engineRowY(panelY, 9) attrs:small small:small];
    [self drawEngineRow:@"TRNS" value:[NSString stringWithFormat:@"%+.2f", prm.trans] norm:(prm.trans + 1.0f) * 0.5f y:engineRowY(panelY, 10) attrs:small small:small];
    [self drawEngineRow:@"TAPE" value:[NSString stringWithFormat:@"%.0f%%", prm.tape * 100.0f] norm:prm.tape y:engineRowY(panelY, 11) attrs:small small:small];
    [self drawEngineRow:@"SPED" value:[NSString stringWithFormat:@"%.0f%%", prm.speed * 100.0f] norm:prm.speed y:engineRowY(panelY, 12) attrs:small small:small];
    panelY += engineH + gap;

    const CGFloat topologyY = static_cast<CGFloat>(kTopologyPanelY);
    const CGFloat topologyH = static_cast<CGFloat>(kTopologyPanelHeight);
    s3g::clap_gui::drawPanelFrame(
        topologyX, topologyY, panelW, topologyH, style);
    drawHeader(@"TOPOLOGY", topologyX, topologyY);
    const auto& t = p->settings.topology;
    s3g::clap_gui::TopologyUiValues values;
    values.shape = s3g::topologyShapeName(t.shape);
    values.amount = t.amount;
    values.pull = t.collapse;
    values.x = t.dirX;
    values.y = t.dirY;
    values.z = t.dirZ;
    values.twist = t.twist;
    values.flare = t.flare;
    values.seed = t.jitter;
    values.motion = s3g::topologyMotionModeName(t.motionMode);
    values.variant = s3g::topologyVariantName(t.motionVariant);
    values.rateHz = t.motionRateHz;
    values.rateMinHz = kMotionRateMinHz;
    values.rateMaxHz = kMotionRateMaxHz;
    values.depth = t.motionDepth;
    values.neighbors = t.neighborCount;
    values.neighborSuffix = true;
    values.radius = t.neighborRadius;
    values.centroid = t.centroidAmount;
    s3g::clap_gui::drawTopologyRows(
        values, topologyY, small, small, style,
        s3g::gui_layout::kStandardMetrics.rowPitch,
        topologyX, panelW);

    const CGFloat meshY = static_cast<CGFloat>(kMeshPanelY);
    const CGFloat meshH = static_cast<CGFloat>(kMeshPanelHeight);
    s3g::clap_gui::drawPanelFrame(topologyX, meshY, panelW, meshH, style);
    drawHeader(@"WAVE MESH", topologyX, meshY);
    const auto& mesh = p->settings.mesh;
    auto drawMeshRow = [&](NSString* name, float value, uint32_t row) {
        s3g::clap_gui::drawProcessorSlider(
            name,
            [NSString stringWithFormat:@"%.0f%%", value * 100.0f],
            value,
            engineRowY(meshY, row),
            topologyX,
            panelW,
            small,
            small,
            style);
    };
    drawMeshRow(@"COUP", mesh.coupling, 0u);
    drawMeshRow(@"TENS", mesh.tension, 1u);
    drawMeshRow(@"DECY", mesh.decay, 2u);
    drawMeshRow(@"DAMP", mesh.damping, 3u);

    const bool compactMatrix = kChannelCount > 8;
    const CGFloat matrixH = compactMatrix ? 354.0 : 248.0;
    s3g::clap_gui::drawPanelFrame(panelX, panelY, panelW, matrixH, style);
    drawHeader(@"PATCH MATRIX", panelX, panelY);
    {
        NSFont* tinyFont = [NSFont fontWithName:@"Menlo" size:7.0] ?: [NSFont monospacedSystemFontOfSize:7.0 weight:NSFontWeightRegular];
        NSDictionary* tiny = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:tinyFont };
        NSDictionary* matrixAttrs = compactMatrix ? tiny : small;
        const CGFloat left = compactMatrix ? 686.0 : 718.0;
        const CGFloat top = panelY + (compactMatrix ? 34.0 : 42.0);
        const CGFloat cell = compactMatrix ? 12.0 : 24.0;
        const CGFloat gapCell = compactMatrix ? 2.0 : 4.0;
        const CGFloat activeInset = compactMatrix ? 2.0 : 5.0;
        const CGFloat rowLabelX = 654.0;
        for (uint32_t i = 0; i < kChannelCount; ++i) {
            NSString* outLabel = [NSString stringWithFormat:@"%u", i + 1];
            [outLabel drawAtPoint:NSMakePoint(left + i * cell + (compactMatrix ? 0.0 : 8.0), top - (compactMatrix ? 13.0 : 18.0)) withAttributes:matrixAttrs];
            NSString* inLabel = [NSString stringWithFormat:@"I%u", i + 1];
            [inLabel drawAtPoint:NSMakePoint(rowLabelX, top + i * cell + (compactMatrix ? 2.0 : 6.0)) withAttributes:matrixAttrs];
        }
        for (uint32_t in = 0; in < kChannelCount; ++in) {
            for (uint32_t out = 0; out < kChannelCount; ++out) {
                const bool connected = p->patch.connected(in, out);
                NSRect r = NSMakeRect(left + out * cell, top + in * cell, cell - gapCell, cell - gapCell);
                [style.strip setFill];
                NSRectFill(r);
                [style.grid setStroke];
                NSFrameRect(r);
                if (connected) {
                    [style.accent setFill];
                    NSRectFill(NSInsetRect(r, activeInset, activeInset));
                }
            }
        }
        [@"UNUSED: CLEAR" drawAtPoint:NSMakePoint(650.0, top + cell * kChannelCount + 18.0) withAttributes:small];
    }

    if (_openMenu > 0 && _menuItemCount > 0) {
        constexpr uint32_t kMaxMenuItems = 24;
        NSString* items[kMaxMenuItems] {};
        const uint32_t count = std::min<uint32_t>(_menuItemCount, kMaxMenuItems);
        int selected = -1;
        for (uint32_t i = 0; i < count; ++i) {
            if (_openMenu == 1) {
                items[i] = [NSString stringWithUTF8String:s3g::topologyShapeName(i)];
                if (i == t.shape) selected = static_cast<int>(i);
            } else if (_openMenu == 2) {
                items[i] = [NSString stringWithUTF8String:s3g::topologyMotionModeName(i)];
                if (i == t.motionMode) selected = static_cast<int>(i);
            } else if (_openMenu == 4) {
                items[i] = [NSString stringWithUTF8String:s3g::topologyVariantName(i)];
                if (i == t.motionVariant) selected = static_cast<int>(i);
            } else {
                items[i] = [NSString stringWithFormat:@"%uNN", i + 1u];
                if ((i + 1u) == t.neighborCount) selected = static_cast<int>(i);
            }
        }
        const CGFloat itemH = 18.0;
        NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 178.0, itemH * static_cast<CGFloat>(count));
        s3g::clap_gui::drawDropdownMenu(menuRect, itemH, items, count, selected, _hoverMenuItem, small, style);
    }
    [NSGraphicsContext restoreGraphicsState];
}
- (void)updateDrag:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const bool meshParam =
        _dragParam >= static_cast<int>(kMeshCouplingParamId)
        && _dragParam <= static_cast<int>(kMeshDampingParamId);
    const bool topologyParam =
        _dragParam >= static_cast<int>(kTopologyAmountParamId)
        && _dragParam <= static_cast<int>(kTopologyCentroidParamId);
    const double panelX =
        (meshParam || topologyParam) ? kSecondaryPanelX : kPrimaryPanelX;
    const double n = std::clamp(
        (point.x - s3g::gui_layout::processorControlX(panelX))
            / s3g::gui_layout::processorTrackWidth(kPanelWidth),
        0.0, 1.0);
    switch (_dragParam) {
    case kFoldParamId: applyParam(*p, kFoldParamId, n); break;
    case kDriveParamId: applyParam(*p, kDriveParamId, n); break;
    case kHoldParamId: applyParam(*p, kHoldParamId, n); break;
    case kClipParamId: applyParam(*p, kClipParamId, n); break;
    case kRectifyParamId: applyParam(*p, kRectifyParamId, n); break;
    case kEdgeParamId: applyParam(*p, kEdgeParamId, n); break;
    case kZeroParamId: applyParam(*p, kZeroParamId, n * 0.78); break;
    case kPolarParamId: applyParam(*p, kPolarParamId, n); break;
    case kBitsParamId: applyParam(*p, kBitsParamId, n * 0.92); break;
    case kStepParamId: applyParam(*p, kStepParamId, n); break;
    case kTransParamId: applyParam(*p, kTransParamId, -1.0 + n * 2.0); break;
    case kTapeParamId: applyParam(*p, kTapeParamId, n); break;
    case kSpeedParamId: applyParam(*p, kSpeedParamId, n); break;
    case kMeshCouplingParamId: applyParam(*p, kMeshCouplingParamId, n); break;
    case kMeshTensionParamId: applyParam(*p, kMeshTensionParamId, n); break;
    case kMeshDecayParamId: applyParam(*p, kMeshDecayParamId, n); break;
    case kMeshDampingParamId: applyParam(*p, kMeshDampingParamId, n); break;
    case kMixParamId: applyParam(*p, kMixParamId, n); break;
    case kGainParamId: applyParam(*p, kGainParamId, -60.0 + n * 72.0); break;
    case kTopologyAmountParamId: applyParam(*p, kTopologyAmountParamId, n); break;
    case kTopologyPullParamId: applyParam(*p, kTopologyPullParamId, n); break;
    case kTopologyXParamId: applyParam(*p, kTopologyXParamId, -1.0 + n * 2.0); break;
    case kTopologyYParamId: applyParam(*p, kTopologyYParamId, -1.0 + n * 2.0); break;
    case kTopologyZParamId: applyParam(*p, kTopologyZParamId, -1.0 + n * 2.0); break;
    case kTopologyTwistParamId: applyParam(*p, kTopologyTwistParamId, -1.0 + n * 2.0); break;
    case kTopologyFlareParamId: applyParam(*p, kTopologyFlareParamId, -1.0 + n * 2.0); break;
    case kTopologySeedParamId: applyParam(*p, kTopologySeedParamId, n); break;
    case kTopologyRateParamId: applyParam(*p, kTopologyRateParamId, kMotionRateMinHz + n * (kMotionRateMaxHz - kMotionRateMinHz)); break;
    case kTopologyDepthParamId: applyParam(*p, kTopologyDepthParamId, n); break;
    case kTopologyRadiusParamId: applyParam(*p, kTopologyRadiusParamId, n); break;
    case kTopologyCentroidParamId: applyParam(*p, kTopologyCentroidParamId, n); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    const CGFloat itemH = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 178.0, itemH * static_cast<CGFloat>(_menuItemCount));
    const int next = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Processor Wave Geometry", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kGainParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    pt.y -= kContentTranslation;

    const auto& fieldLayout =
        s3g::gui_layout::kTopologyProcessorColumns.field;
    const NSRect fieldPanel = NSMakeRect(
        fieldLayout.x,
        fieldLayout.y - kContentTranslation,
        fieldLayout.width,
        fieldLayout.height);
    if (NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 2; ++i) {
            NSRect button = [self fieldPageButtonRect:fieldPanel index:i];
            if (NSPointInRect(pt, button)) {
                _fieldPage = i;
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }

    for (uint32_t index = 0u; index < 3u; ++index) {
        if (_fieldPage == 0 && NSPointInRect(
                pt,
                s3g::clap_gui::topologyProcessorCameraButtonRect(
                    fieldPanel, index))) {
            [self setTopologyView:index];
            return;
        }
    }

    if (NSPointInRect(pt, NSMakeRect(580.0, 116.0, 32.0, 15.0))) {
        _showReadout = !_showReadout;
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect topologyView =
        s3g::clap_gui::topologyProcessorFieldContentRect(
            fieldPanel);
    if (_fieldPage == 0 && _openMenu == 0
        && NSPointInRect(pt, topologyView)) {
        _dragTopologyView = true;
        _lastDragPoint = pt;
        return;
    }

    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 178.0, itemH * static_cast<CGFloat>(_menuItemCount));
        if (NSPointInRect(pt, menuRect)) {
            const uint32_t index = std::min<uint32_t>(_menuItemCount - 1u, static_cast<uint32_t>((pt.y - _menuOrigin.y) / itemH));
            if (_openMenu == 1) {
                applyParam(*p, kTopologyShapeParamId, index);
            } else if (_openMenu == 2) {
                applyParam(*p, kTopologyMotionParamId, index);
            } else if (_openMenu == 4) {
                applyParam(*p, kTopologyVariantParamId, index);
            } else if (_openMenu == 3) {
                applyParam(*p, kTopologyNeighborsParamId, index + 1u);
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        [self setNeedsDisplay:YES];
        return;
    }

    const CGFloat panelX = kPrimaryPanelX;
    const CGFloat topologyX = kSecondaryPanelX;
    const CGFloat panelW = kPanelWidth;
    const CGFloat gap =
        s3g::gui_layout::kStandardMetrics.panelGap;
    auto menuOrigin = [&](CGFloat x, CGFloat preferredY, uint32_t itemCount) {
        const CGFloat itemH = 18.0;
        const CGFloat bottom = kContentCoordinateHeight - 10.0;
        return NSMakePoint(x, std::max<CGFloat>(28.0, std::min<CGFloat>(preferredY, bottom - itemH * static_cast<CGFloat>(itemCount))));
    };

    CGFloat panelY = 34.0;
    const CGFloat outputH = 80.0;
    const clap_id outputIds[] = { kGainParamId, kMixParamId };
    for (uint32_t i = 0; i < 2u; ++i) {
        const CGFloat rowY = static_cast<CGFloat>(engineRowY(panelY, i));
        if (!NSPointInRect(
                pt, NSMakeRect(panelX, rowY - 8.0, panelW, 24.0))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, outputIds[i], &defaultValue)) {
            applyParam(*p, outputIds[i], defaultValue);
            _dragParam = 0;
        } else {
            _dragParam = static_cast<int>(outputIds[i]);
            [self updateDrag:pt];
        }
        return;
    }
    panelY += outputH + gap;
    const CGFloat engineH =
        static_cast<CGFloat>(kEnginePanelHeight);
    const clap_id engineIds[] = {kFoldParamId,kDriveParamId,kHoldParamId,kClipParamId,kRectifyParamId,kEdgeParamId,kZeroParamId,kPolarParamId,kBitsParamId,kStepParamId,kTransParamId,kTapeParamId,kSpeedParamId};
    for (uint32_t i = 0; i < 13u; ++i) {
        const CGFloat rowY = static_cast<CGFloat>(engineRowY(panelY, i));
        if (NSPointInRect(
                pt, NSMakeRect(panelX, rowY - 8.0, panelW, 24.0))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, engineIds[i], &defaultValue)) {
                applyParam(*p, engineIds[i], defaultValue);
                _dragParam = 0;
            } else {
                _dragParam = static_cast<int>(engineIds[i]);
                [self updateDrag:pt];
            }
            return;
        }
    }
    panelY += engineH + gap;

    const CGFloat topologyY = static_cast<CGFloat>(kTopologyPanelY);
    {
        const auto row = s3g::clap_gui::hitTopologyRow(
            pt, topologyY, topologyX, panelW);
        if (row == s3g::clap_gui::TopologyRow::Shape) {
            _openMenu = 1;
            _hoverMenuItem = -1;
            _menuItemCount = s3g::kTopologyShapeCount;
            _menuOrigin = menuOrigin(
                s3g::gui_layout::processorControlX(topologyX),
                s3g::clap_gui::topologyRowY(topologyY, row) + 18.0,
                _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (row == s3g::clap_gui::TopologyRow::Motion) {
            _openMenu = 2;
            _hoverMenuItem = -1;
            _menuItemCount = s3g::kTopologyMotionModeCount;
            _menuOrigin = menuOrigin(
                s3g::gui_layout::processorControlX(topologyX),
                s3g::clap_gui::topologyRowY(topologyY, row) + 18.0,
                _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (row == s3g::clap_gui::TopologyRow::Variant) {
            _openMenu = 4;
            _hoverMenuItem = -1;
            _menuItemCount = s3g::kTopologyVariantCount;
            _menuOrigin = menuOrigin(
                s3g::gui_layout::processorControlX(topologyX),
                s3g::clap_gui::topologyRowY(topologyY, row) + 18.0,
                _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (row == s3g::clap_gui::TopologyRow::Neighbors) {
            _openMenu = 3;
            _hoverMenuItem = -1;
            _menuItemCount = 3;
            _menuOrigin = menuOrigin(
                s3g::gui_layout::processorControlX(topologyX),
                s3g::clap_gui::topologyRowY(topologyY, row) + 18.0,
                _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        switch (row) {
        case s3g::clap_gui::TopologyRow::Amount: _dragParam = kTopologyAmountParamId; break;
        case s3g::clap_gui::TopologyRow::Pull: _dragParam = kTopologyPullParamId; break;
        case s3g::clap_gui::TopologyRow::X: _dragParam = kTopologyXParamId; break;
        case s3g::clap_gui::TopologyRow::Y: _dragParam = kTopologyYParamId; break;
        case s3g::clap_gui::TopologyRow::Z: _dragParam = kTopologyZParamId; break;
        case s3g::clap_gui::TopologyRow::Twist: _dragParam = kTopologyTwistParamId; break;
        case s3g::clap_gui::TopologyRow::Flare: _dragParam = kTopologyFlareParamId; break;
        case s3g::clap_gui::TopologyRow::Seed: _dragParam = kTopologySeedParamId; break;
        case s3g::clap_gui::TopologyRow::Rate: _dragParam = kTopologyRateParamId; break;
        case s3g::clap_gui::TopologyRow::Depth: _dragParam = kTopologyDepthParamId; break;
        case s3g::clap_gui::TopologyRow::Radius: _dragParam = kTopologyRadiusParamId; break;
        case s3g::clap_gui::TopologyRow::Centroid: _dragParam = kTopologyCentroidParamId; break;
        default: _dragParam = 0; break;
        }
        if (_dragParam != 0) {
            const clap_id param = static_cast<clap_id>(_dragParam);
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, param, &defaultValue)) {
                applyParam(*p, param, defaultValue);
                _dragParam = 0;
            } else {
                [self updateDrag:pt];
            }
            return;
        }
    }

    const CGFloat meshY = static_cast<CGFloat>(kMeshPanelY);
    const clap_id meshIds[] = {
        kMeshCouplingParamId,
        kMeshTensionParamId,
        kMeshDecayParamId,
        kMeshDampingParamId,
    };
    for (uint32_t row = 0; row < 4u; ++row) {
        const CGFloat rowY = static_cast<CGFloat>(engineRowY(meshY, row));
        if (!NSPointInRect(
                pt,
                NSMakeRect(
                    topologyX,
                    rowY - s3g::gui_layout::kStandardMetrics.hitInset,
                    panelW,
                    s3g::gui_layout::kStandardMetrics.hitHeight))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, meshIds[row], &defaultValue)) {
            applyParam(*p, meshIds[row], defaultValue);
            _dragParam = 0;
        } else {
            _dragParam = static_cast<int>(meshIds[row]);
            [self updateDrag:pt];
        }
        return;
    }

    const bool compactMatrix = kChannelCount > 8;
    {
        const CGFloat left = compactMatrix ? 686.0 : 718.0;
        const CGFloat top = panelY + (compactMatrix ? 34.0 : 42.0);
        const CGFloat cell = compactMatrix ? 12.0 : 24.0;
        if (pt.x >= left && pt.y >= top
            && pt.x < left + cell * kChannelCount
            && pt.y < top + cell * kChannelCount) {
            const uint32_t out = static_cast<uint32_t>((pt.x - left) / cell);
            const uint32_t in = static_cast<uint32_t>((pt.y - top) / cell);
            togglePatchCellFromGui(*p, in, out);
            [self setNeedsDisplay:YES];
            return;
        }
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    pt.y -= kContentTranslation;
    [self updateMenuHover:pt];
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    pt.y -= kContentTranslation;
    [self updateMenuHover:pt];
    if (_dragTopologyView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewYaw += dx * 0.015;
        _viewPitch = std::clamp(_viewPitch + dy * 0.012, -0.75, 0.95);
        _cameraView = -1;
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam != 0) [self updateDrag:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragParam = 0; _dragTopologyView = false; }
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GWaveGeometryView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight, kGuiWidth, 360u)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3GWaveGeometryView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h, kGuiWidth, 360u); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h, kGuiWidth, 360u); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GWaveGeometryView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GWaveGeometryView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
} // namespace
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    S3G_WAVE_GEOMETRY_PLUGIN_ID,
    S3G_WAVE_GEOMETRY_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Topology-driven waveform geometry processor with nonlinear shaping, tape heads, and a sample-domain wave mesh.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostTail = host && host->get_extension
        ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL))
        : nullptr;
    p->settings.base.fold = 0.22f;
    p->settings.base.drive = 0.18f;
    p->settings.base.hold = 0.0f;
    p->settings.base.clip = 0.18f;
    p->settings.base.rectify = 0.0f;
    p->settings.base.edge = 0.0f;
    p->settings.base.zero = 0.0f;
    p->settings.base.polar = 0.0f;
    p->settings.base.bits = 0.0f;
    p->settings.base.step = 0.0f;
    p->settings.base.gainDb = -3.0f;
    p->settings.base.mix = 1.0f;
    p->settings.base.trans = 0.0f;
    p->settings.base.safety = 0.82f;
    p->settings.mesh.coupling = 0.0f;
    p->settings.mesh.tension = 0.62f;
    p->settings.mesh.decay = 0.35f;
    p->settings.mesh.damping = 0.45f;
    p->settings.topology.amount = 0.35;
    p->settings.topology.jitter = 0.08;
    p->settings.topology.collapse = 0.0;
    p->settings.topology.dirX = 0.0;
    p->settings.topology.dirY = 0.0;
    p->settings.topology.dirZ = 1.0;
    p->settings.topology.twist = 0.08;
    p->settings.topology.flare = 0.0;
    p->settings.topology.shape = 0;
    p->settings.topology.motionMode = 0;
    p->settings.topology.motionVariant = 0;
    p->settings.topology.motionRateHz = 0.08;
    p->settings.topology.motionDepth = 0.0;
    p->settings.topology.neighborCount = 2;
    p->settings.topology.neighborRadius = 0.65;
    p->settings.topology.centroidAmount = 0.18;
    p->audioSettings = p->settings;
    p->publishedMotionPhase.store(
        p->settings.topology.motionPhase,
        std::memory_order_relaxed);
    storeSettingsInParameterBank(*p, p->settings);
    p->patch.setWidth(kChannelCount);
    p->patch.setIdentity(kChannelCount);
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
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
