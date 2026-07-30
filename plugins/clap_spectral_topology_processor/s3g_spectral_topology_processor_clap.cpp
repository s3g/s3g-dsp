#include "s3g_lane_patch.h"
#include "s3g_realtime.h"
#include "s3g_spectral_topology_processor.h"
#include "s3g_topology_heatmap.h"
#include "../common/s3g_objc_class_name.h"

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
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

#ifndef S3G_SPECTRAL_TOPOLOGY_CHANNEL_COUNT
#define S3G_SPECTRAL_TOPOLOGY_CHANNEL_COUNT 8
#endif

#define S3G_SPECTRAL_TOPOLOGY_VIEW_CLASS \
    S3G_OBJC_CLASS_JOIN(S3GSpectralTopologyView, \
        S3G_SPECTRAL_TOPOLOGY_CHANNEL_COUNT)

#ifndef S3G_SPECTRAL_TOPOLOGY_PLUGIN_ID
#define S3G_SPECTRAL_TOPOLOGY_PLUGIN_ID "org.s3g.s3g-dsp.spectral-topology-processor"
#endif

#ifndef S3G_SPECTRAL_TOPOLOGY_PLUGIN_NAME
#define S3G_SPECTRAL_TOPOLOGY_PLUGIN_NAME "s3g Processor Spectral 8ch"
#endif

constexpr uint32_t kChannelCount = s3g::kSpectralTopologyChannels;
constexpr uint32_t kStateVersion = 4;
constexpr uint32_t kGuiWidth = static_cast<uint32_t>(
    s3g::gui_layout::kTopologyProcessorColumns.canvasWidth);
constexpr uint32_t kGuiHeight = 820u;
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
constexpr uint32_t kScopeFrames = 131072;
constexpr uint32_t kFftSize = 2048u;
constexpr uint32_t kFftOverlap = 4u;
constexpr uint32_t kFftHopFrames = kFftSize / kFftOverlap;
// clap/ext/tail.h reserves every value >= INT32_MAX for an infinite tail.
// The vendored CLAP revision documents that sentinel without naming it.
constexpr uint32_t kClapTailInfinite =
    static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
constexpr double kMotionRateMinHz = 0.01;
constexpr double kMotionRateMaxHz = 1.0;
constexpr double kEngineRowPitch =
    s3g::gui_layout::kStandardMetrics.rowPitch;
constexpr double kEngineFirstRow = 36.0;
constexpr double kEnginePanelHeight =
    s3g::gui_layout::toolboxHeightForRows(14u);
constexpr double kPropagationPanelHeight =
    s3g::gui_layout::toolboxHeightForRows(3u);
constexpr double kTopologyPanelY = 34.0;
constexpr double kTopologyPanelHeight =
    s3g::gui_layout::toolboxHeightForRows(16u);
constexpr double kPropagationPanelY = kTopologyPanelY
    + kTopologyPanelHeight
    + s3g::gui_layout::kStandardMetrics.panelGap;

constexpr clap_id kSprayBinsParamId = 1;
constexpr clap_id kDriftParamId = 2;
constexpr clap_id kHoldParamId = 3;
constexpr clap_id kFreezeParamId = 4;
constexpr clap_id kFeedbackParamId = 5;
constexpr clap_id kSmearParamId = 6;
constexpr clap_id kHolesParamId = 7;
constexpr clap_id kPhaseBlurParamId = 8;
constexpr clap_id kTiltParamId = 9;
constexpr clap_id kMixParamId = 10;
constexpr clap_id kGainParamId = 11;
constexpr clap_id kSafetyParamId = 12;
constexpr clap_id kDamageParamId = 13;
constexpr clap_id kRepeatParamId = 14;
constexpr clap_id kLoFreqParamId = 15;
constexpr clap_id kHiFreqParamId = 16;
constexpr clap_id kTransientProtectParamId = 17;
constexpr clap_id kPropagationVelocityParamId = 18;
constexpr clap_id kPropagationDispersionParamId = 19;
constexpr clap_id kPropagationDampingParamId = 20;

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

struct SavedState {
    uint32_t version = kStateVersion;
    float transientProtect = 0.35f;
    float propagationVelocity = 1.0f;
    float propagationDispersion = 0.0f;
    float propagationDamping = 0.0f;
    uint32_t reserved = 0u;
    s3g::SpectralTopologySettings settings {};
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
};

// Versions 1 and 2 wrote native structs directly. Keep their exact field
// layout frozen here so future settings additions cannot silently change the
// number of bytes consumed while migrating an old preset.
struct LegacySpectralSprayParamsV2 {
    float sprayBins;
    float drift;
    float hold;
    float freeze;
    float feedback;
    float smear;
    float holes;
    float phaseBlur;
    float damage;
    float repeat;
    float loFreq;
    float hiFreq;
    float gainDb;
    float mix;
    float tilt;
    float safety;
};

struct LegacyTopologyStateV2 {
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

struct LegacySpectralTopologySettingsV2 {
    LegacySpectralSprayParamsV2 base;
    LegacyTopologyStateV2 topology;
};

struct SavedStateV2 {
    uint32_t version = 2;
    LegacySpectralTopologySettingsV2 settings {};
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
};

struct SavedStateV3 {
    uint32_t version = 3;
    float transientProtect = 0.35f;
    LegacySpectralTopologySettingsV2 settings {};
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
};

struct SavedStateV1 {
    uint32_t version = 1;
    LegacySpectralTopologySettingsV2 settings {};
};

static_assert(sizeof(LegacySpectralSprayParamsV2) == 64u);
static_assert(sizeof(LegacyTopologyStateV2) == 128u);
static_assert(sizeof(LegacySpectralTopologySettingsV2) == 192u);
static_assert(offsetof(SavedStateV1, settings) == 8u);
static_assert(sizeof(SavedStateV1) == 200u);
static_assert(offsetof(SavedStateV2, settings) == 8u);
static_assert(offsetof(SavedStateV2, patchRows) == 200u);
static_assert(sizeof(SavedStateV2) == 712u);
static_assert(offsetof(SavedStateV3, transientProtect) == 4u);
static_assert(offsetof(SavedStateV3, settings) == 8u);
static_assert(offsetof(SavedStateV3, patchRows) == 200u);
static_assert(sizeof(SavedStateV3) == 712u);
static_assert(offsetof(SavedState, transientProtect) == 4u);
static_assert(offsetof(SavedState, propagationVelocity) == 8u);
static_assert(offsetof(SavedState, propagationDispersion) == 12u);
static_assert(offsetof(SavedState, propagationDamping) == 16u);
static_assert(offsetof(SavedState, reserved) == 20u);
static_assert(offsetof(SavedState, settings) == 24u);
static_assert(offsetof(SavedState, patchRows) == 216u);
static_assert(sizeof(SavedState) == 728u);

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::SpectralTopologySettings settings {};
    float transientProtect = 0.35f;
    float propagationVelocity = 1.0f;
    float propagationDispersion = 0.0f;
    float propagationDamping = 0.0f;
    s3g::SpectralTopologyProcessor processor;
    s3g::LanePatch patch;
    std::vector<std::vector<float>> input32;
    std::vector<std::vector<float>> output32;
    std::vector<const float*> inputPtrs;
    std::vector<float*> outputPtrs;
    std::array<std::array<std::atomic<float>, kScopeFrames>, kChannelCount> scope {};
    std::atomic<uint32_t> scopeWrite { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> tailChangePending { false };
    bool tailCaptureState = false;
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

// clap.tail requires hostTail->changed() on the audio thread. Main-thread
// state and GUI paths only mark the change; processing entry points coalesce
// and deliver it safely.
void deliverTailChangedOnAudioThread(Plugin& p)
{
    if (p.tailChangePending.exchange(false, std::memory_order_acq_rel)
        && p.host && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

bool paramAffectsTail(clap_id id)
{
    return id == kHoldParamId || id == kFreezeParamId
        || id == kFeedbackParamId || id == kRepeatParamId
        || id == kPropagationVelocityParamId
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

void applyLaneParams(Plugin& p)
{
    auto& base = p.settings.base;
    base.loFreq = std::clamp(base.loFreq, 0.0f, 23980.0f);
    base.hiFreq = std::clamp(base.hiFreq, 20.0f, 24000.0f);
    if (base.hiFreq < base.loFreq + 20.0f) {
        base.hiFreq = base.loFreq + 20.0f;
    }
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p.processor.setLaneParams(ch, s3g::spectralTopologyLaneParams(p.settings, ch, kChannelCount));
    }
    p.processor.setTopologyState(p.settings.topology);
    p.processor.setTransientProtect(p.transientProtect);
    p.processor.setPropagation(
        p.propagationVelocity,
        p.propagationDispersion,
        p.propagationDamping);
}

bool topologyMotionActive(const Plugin& p)
{
    return s3g::topologyMotionActive(p.settings.topology);
}

void advanceTopologyMotion(Plugin& p, uint32_t frames)
{
    if (!topologyMotionActive(p) || p.sampleRate <= 0.0 || frames == 0u) return;
    auto& t = p.settings.topology;
    t.motionPhase += (static_cast<double>(frames) / p.sampleRate) * t.motionRateHz;
    t.motionPhase -= std::floor(t.motionPhase);
    applyLaneParams(p);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    const bool tailWasAffected = paramAffectsTail(id);
    auto& prm = p.settings.base;
    auto& t = p.settings.topology;
    switch (id) {
    case kSprayBinsParamId: prm.sprayBins = static_cast<float>(std::clamp(value, 0.0, 192.0)); break;
    case kDriftParamId: prm.drift = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kHoldParamId: prm.hold = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFreezeParamId: prm.freeze = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFeedbackParamId: prm.feedback = static_cast<float>(std::clamp(value, 0.0, 0.72)); break;
    case kSmearParamId: prm.smear = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kHolesParamId: prm.holes = static_cast<float>(std::clamp(value, 0.0, 0.60)); break;
    case kPhaseBlurParamId: prm.phaseBlur = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDamageParamId: prm.damage = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kRepeatParamId: prm.repeat = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kLoFreqParamId: prm.loFreq = static_cast<float>(std::clamp(value, 0.0, std::max(0.0, static_cast<double>(prm.hiFreq) - 20.0))); break;
    case kHiFreqParamId: prm.hiFreq = static_cast<float>(std::clamp(value, std::min(24000.0, static_cast<double>(prm.loFreq) + 20.0), 24000.0)); break;
    case kTransientProtectParamId: p.transientProtect = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kPropagationVelocityParamId: p.propagationVelocity = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kPropagationDispersionParamId: p.propagationDispersion = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kPropagationDampingParamId: p.propagationDamping = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kTiltParamId: prm.tilt = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kMixParamId: prm.mix = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: prm.gainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    case kSafetyParamId: prm.safety = static_cast<float>(std::clamp(value, 0.12, 0.92)); break;
    case kTopologyShapeParamId: t.shape = std::min<uint32_t>(s3g::kTopologyShapeCount - 1u, static_cast<uint32_t>(std::floor(value + 0.5))); break;
    case kTopologyAmountParamId: t.amount = std::clamp(value, 0.0, 1.0); break;
    case kTopologySeedParamId: t.jitter = std::clamp(value, 0.0, 1.0); break;
    case kTopologyPullParamId: t.collapse = std::clamp(value, 0.0, 1.0); break;
    case kTopologyXParamId: t.dirX = std::clamp(value, -1.0, 1.0); break;
    case kTopologyYParamId: t.dirY = std::clamp(value, -1.0, 1.0); break;
    case kTopologyZParamId: t.dirZ = std::clamp(value, -1.0, 1.0); break;
    case kTopologyTwistParamId: t.twist = std::clamp(value, -1.0, 1.0); break;
    case kTopologyFlareParamId: t.flare = std::clamp(value, -1.0, 1.0); break;
    case kTopologyMotionParamId: t.motionMode = std::min<uint32_t>(s3g::kTopologyMotionModeCount - 1u, static_cast<uint32_t>(std::floor(value + 0.5))); break;
    case kTopologyVariantParamId: t.motionVariant = std::min<uint32_t>(s3g::kTopologyVariantCount - 1u, static_cast<uint32_t>(std::floor(value + 0.5))); break;
    case kTopologyRateParamId: t.motionRateHz = clampMotionRate(value); break;
    case kTopologyDepthParamId: t.motionDepth = std::clamp(value, 0.0, 1.0); break;
    case kTopologyNeighborsParamId: t.neighborCount = std::clamp<uint32_t>(static_cast<uint32_t>(std::floor(value + 0.5)), 1u, 3u); break;
    case kTopologyRadiusParamId: t.neighborRadius = std::clamp(value, 0.0, 1.0); break;
    case kTopologyCentroidParamId: t.centroidAmount = std::clamp(value, 0.0, 1.0); break;
    default: break;
    }
    applyLaneParams(p);
    if (tailWasAffected) markTailChanged(p);
}

void preparePatch(Plugin& p)
{
    p.patch.setWidth(kChannelCount);
}

void togglePatchCellFromGui(Plugin& p, uint32_t input, uint32_t output)
{
    p.patch.setWidth(kChannelCount);
    p.patch.toggle(input, output);
}

bool patchOutputInjected(const Plugin& p, uint32_t output)
{
    if (output >= kChannelCount) return false;
    const uint64_t bit = uint64_t { 1 } << output;
    for (uint32_t input = 0; input < kChannelCount; ++input) {
        if ((p.patch.rowMask(input) & bit) != 0) return true;
    }
    return false;
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
    if (!p->processor.prepare(sampleRate, kChannelCount, kFftSize, kFftOverlap, p->maxFrames)) return false;
    applyLaneParams(*p);
    p->tailCaptureState = p->processor.hasCapture();
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
    deliverTailChangedOnAudioThread(*self(plugin));
    return true;
}
void stopProcessing(const clap_plugin_t* plugin)
{
    deliverTailChangedOnAudioThread(*self(plugin));
}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    const bool hadCapture = p->tailCaptureState || p->processor.hasCapture();
    p->processor.reset();
    p->tailCaptureState = false;
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    if (hadCapture) markTailChanged(*p);
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
    deliverTailChangedOnAudioThread(*p);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = std::min(proc->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kChannelCount) return CLAP_PROCESS_CONTINUE;

    advanceTopologyMotion(*p, frames);

    preparePatch(*p);
    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        std::fill(p->input32[lane].begin(), p->input32[lane].begin() + frames, 0.0f);
        const uint64_t laneBit = uint64_t { 1 } << lane;
        for (uint32_t inCh = 0; inCh < kChannelCount; ++inCh) {
            if ((p->patch.rowMask(inCh) & laneBit) == 0) continue;
            for (uint32_t i = 0; i < frames; ++i) {
                if (inCh < input.channel_count && input.data32 && input.data32[inCh]) p->input32[lane][i] += input.data32[inCh][i];
                else if (inCh < input.channel_count && input.data64 && input.data64[inCh]) p->input32[lane][i] += static_cast<float>(input.data64[inCh][i]);
            }
        }
    }
    p->processor.process(p->inputPtrs.data(), kChannelCount, p->outputPtrs.data(), kChannelCount, frames);
    const bool hasCapture = p->processor.hasCapture();
    if (hasCapture != p->tailCaptureState) {
        p->tailCaptureState = hasCapture;
        markTailChanged(*p);
    }
    deliverTailChangedOnAudioThread(*p);

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
    std::snprintf(info->name, sizeof(info->name), "%uch %s", kChannelCount, isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kSprayBinsParamId, "Spray Bins", 0.0, 192.0, 18.0 },
    { kDriftParamId, "Drift", 0.0, 1.0, 0.18 },
    { kHoldParamId, "Hold", 0.0, 1.0, 0.72 },
    { kFreezeParamId, "Freeze", 0.0, 1.0, 0.0 },
    { kFeedbackParamId, "Feedback", 0.0, 0.72, 0.18 },
    { kSmearParamId, "Smear", 0.0, 1.0, 0.25 },
    { kHolesParamId, "Holes", 0.0, 0.60, 0.05 },
    { kPhaseBlurParamId, "Phase Blur", 0.0, 1.0, 0.28 },
    { kDamageParamId, "Spectral Damage", 0.0, 1.0, 0.0 },
    { kRepeatParamId, "Spectral Repeat", 0.0, 1.0, 0.0 },
    { kTiltParamId, "Tilt", -1.0, 1.0, 0.0 },
    { kMixParamId, "Mix", 0.0, 1.0, 1.0 },
    { kGainParamId, "Output", -60.0, 12.0, -2.5 },
    { kSafetyParamId, "Safety", 0.12, 0.92, 0.82 },
    { kLoFreqParamId, "Lo Freq", 0.0, 24000.0, 0.0 },
    { kHiFreqParamId, "Hi Freq", 20.0, 24000.0, 20000.0 },
    { kTransientProtectParamId, "Transient Protect", 0.0, 1.0, 0.35 },
    { kPropagationVelocityParamId, "Propagation Velocity", 0.0, 1.0, 1.0 },
    { kPropagationDispersionParamId, "Propagation Dispersion", -1.0, 1.0, 0.0 },
    { kPropagationDampingParamId, "Propagation Damping", 0.0, 1.0, 0.0 },
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
    const char* module = def.id >= kPropagationVelocityParamId
            && def.id <= kPropagationDampingParamId
        ? "Propagation"
        : def.id < kTopologyShapeParamId ? "Spectral Engine" : "Topology";
    std::strncpy(info->module, module, sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& p = *self(plugin);
    const auto& prm = p.settings.base;
    const auto& t = p.settings.topology;
    switch (id) {
    case kSprayBinsParamId: *value = prm.sprayBins; return true;
    case kDriftParamId: *value = prm.drift; return true;
    case kHoldParamId: *value = prm.hold; return true;
    case kFreezeParamId: *value = prm.freeze; return true;
    case kFeedbackParamId: *value = prm.feedback; return true;
    case kSmearParamId: *value = prm.smear; return true;
    case kHolesParamId: *value = prm.holes; return true;
    case kPhaseBlurParamId: *value = prm.phaseBlur; return true;
    case kDamageParamId: *value = prm.damage; return true;
    case kRepeatParamId: *value = prm.repeat; return true;
    case kLoFreqParamId: *value = prm.loFreq; return true;
    case kHiFreqParamId: *value = prm.hiFreq; return true;
    case kTransientProtectParamId: *value = p.transientProtect; return true;
    case kPropagationVelocityParamId: *value = p.propagationVelocity; return true;
    case kPropagationDispersionParamId: *value = p.propagationDispersion; return true;
    case kPropagationDampingParamId: *value = p.propagationDamping; return true;
    case kTiltParamId: *value = prm.tilt; return true;
    case kMixParamId: *value = prm.mix; return true;
    case kGainParamId: *value = prm.gainDb; return true;
    case kSafetyParamId: *value = prm.safety; return true;
    case kTopologyShapeParamId: *value = t.shape; return true;
    case kTopologyAmountParamId: *value = t.amount; return true;
    case kTopologySeedParamId: *value = t.jitter; return true;
    case kTopologyPullParamId: *value = t.collapse; return true;
    case kTopologyXParamId: *value = t.dirX; return true;
    case kTopologyYParamId: *value = t.dirY; return true;
    case kTopologyZParamId: *value = t.dirZ; return true;
    case kTopologyTwistParamId: *value = t.twist; return true;
    case kTopologyFlareParamId: *value = t.flare; return true;
    case kTopologyMotionParamId: *value = t.motionMode; return true;
    case kTopologyVariantParamId: *value = t.motionVariant; return true;
    case kTopologyRateParamId: *value = t.motionRateHz; return true;
    case kTopologyDepthParamId: *value = t.motionDepth; return true;
    case kTopologyNeighborsParamId: *value = t.neighborCount; return true;
    case kTopologyRadiusParamId: *value = t.neighborRadius; return true;
    case kTopologyCentroidParamId: *value = t.centroidAmount; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kSprayBinsParamId) std::snprintf(display, size, "%.0f bins", value);
    else if (id == kLoFreqParamId || id == kHiFreqParamId) std::snprintf(display, size, "%.0f Hz", value);
    else if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTiltParamId || id == kPropagationDispersionParamId
             || id == kTopologyXParamId || id == kTopologyYParamId ||
             id == kTopologyZParamId || id == kTopologyTwistParamId || id == kTopologyFlareParamId) std::snprintf(display, size, "%+.2f", value);
    else if (id == kTopologyShapeParamId) std::snprintf(display, size, "%s", s3g::topologyShapeName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyMotionParamId) std::snprintf(display, size, "%s", s3g::topologyMotionModeName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyVariantParamId) std::snprintf(display, size, "%s", s3g::topologyVariantName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyNeighborsParamId) std::snprintf(display, size, "%.0fNN", value);
    else if (id == kTopologyRateParamId) std::snprintf(display, size, "%.2f Hz", value);
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;

    if (id == kTopologyShapeParamId) {
        for (uint32_t i = 0; i < s3g::kTopologyShapeCount; ++i) {
            if (std::strcmp(display, s3g::topologyShapeName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
    } else if (id == kTopologyMotionParamId) {
        for (uint32_t i = 0; i < s3g::kTopologyMotionModeCount; ++i) {
            if (std::strcmp(display, s3g::topologyMotionModeName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
    } else if (id == kTopologyVariantParamId) {
        for (uint32_t i = 0; i < s3g::kTopologyVariantCount; ++i) {
            if (std::strcmp(display, s3g::topologyVariantName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
    }

    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || !std::isfinite(parsed)) return false;
    *value = std::strchr(display, '%') ? parsed * 0.01 : parsed;
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

s3g::SpectralTopologySettings migrateLegacySettings(
    const LegacySpectralTopologySettingsV2& legacy)
{
    s3g::SpectralTopologySettings settings {};
    auto& base = settings.base;
    base.sprayBins = legacy.base.sprayBins;
    base.drift = legacy.base.drift;
    base.hold = legacy.base.hold;
    base.freeze = legacy.base.freeze;
    base.feedback = legacy.base.feedback;
    base.smear = legacy.base.smear;
    base.holes = legacy.base.holes;
    base.phaseBlur = legacy.base.phaseBlur;
    base.damage = legacy.base.damage;
    base.repeat = legacy.base.repeat;
    base.loFreq = legacy.base.loFreq;
    base.hiFreq = legacy.base.hiFreq;
    base.gainDb = legacy.base.gainDb;
    base.mix = legacy.base.mix;
    base.tilt = legacy.base.tilt;
    base.safety = legacy.base.safety;

    auto& topology = settings.topology;
    topology.amount = legacy.topology.amount;
    topology.jitter = legacy.topology.jitter;
    topology.collapse = legacy.topology.collapse;
    topology.dirX = legacy.topology.dirX;
    topology.dirY = legacy.topology.dirY;
    topology.dirZ = legacy.topology.dirZ;
    topology.twist = legacy.topology.twist;
    topology.flare = legacy.topology.flare;
    topology.shape = legacy.topology.shape;
    topology.motionMode = legacy.topology.motionMode;
    topology.motionVariant = legacy.topology.motionVariant;
    topology.motionRateHz = legacy.topology.motionRateHz;
    topology.motionDepth = legacy.topology.motionDepth;
    topology.motionPhase = legacy.topology.motionPhase;
    topology.neighborCount = legacy.topology.neighborCount;
    topology.neighborRadius = legacy.topology.neighborRadius;
    topology.centroidAmount = legacy.topology.centroidAmount;
    return settings;
}

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

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {};
    auto* p = self(plugin);
    s.transientProtect = p->transientProtect;
    s.propagationVelocity = p->propagationVelocity;
    s.propagationDispersion = p->propagationDispersion;
    s.propagationDamping = p->propagationDamping;
    s.settings = p->settings;
    // Motion phase is runtime transport, not a user parameter. Excluding it
    // keeps state deterministic whether a host reaches the preset through
    // process() or params.flush().
    s.settings.topology.motionPhase = 0.0;
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
        p->transientProtect = std::clamp(s.transientProtect, 0.0f, 1.0f);
        p->propagationVelocity = std::clamp(s.propagationVelocity, 0.0f, 1.0f);
        p->propagationDispersion = std::clamp(s.propagationDispersion, -1.0f, 1.0f);
        p->propagationDamping = std::clamp(s.propagationDamping, 0.0f, 1.0f);
        p->patch.setWidth(kChannelCount);
        for (uint32_t row = 0; row < kChannelCount; ++row) {
            p->patch.setRowMask(row, s.patchRows[row]);
        }
        preparePatch(*p);
    } else if (version == 3u) {
        SavedStateV3 s {};
        if (!readStateRemainder(stream, version, s)) return false;
        p->settings = migrateLegacySettings(s.settings);
        p->transientProtect = std::clamp(s.transientProtect, 0.0f, 1.0f);
        p->propagationVelocity = 1.0f;
        p->propagationDispersion = 0.0f;
        p->propagationDamping = 0.0f;
        p->patch.setWidth(kChannelCount);
        for (uint32_t row = 0; row < kChannelCount; ++row) {
            p->patch.setRowMask(row, s.patchRows[row]);
        }
        preparePatch(*p);
    } else if (version == 2u) {
        SavedStateV2 s {};
        if (!readStateRemainder(stream, version, s)) return false;
        p->settings = migrateLegacySettings(s.settings);
        p->transientProtect = 0.35f;
        p->propagationVelocity = 1.0f;
        p->propagationDispersion = 0.0f;
        p->propagationDamping = 0.0f;
        p->patch.setWidth(kChannelCount);
        for (uint32_t row = 0; row < kChannelCount; ++row) {
            p->patch.setRowMask(row, s.patchRows[row]);
        }
        preparePatch(*p);
    } else if (version == 1u) {
        SavedStateV1 s {};
        if (!readStateRemainder(stream, version, s)) return false;
        p->settings = migrateLegacySettings(s.settings);
        p->transientProtect = 0.35f;
        p->propagationVelocity = 1.0f;
        p->propagationDispersion = 0.0f;
        p->propagationDamping = 0.0f;
        p->patch.setIdentity(kChannelCount);
    } else {
        return false;
    }
    p->settings.topology.motionPhase = 0.0;
    applyLaneParams(*p);
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

    double hold = 0.0;
    double freeze = 0.0;
    double feedback = 0.0;
    double repeat = 0.0;
    if (topologyMotionActive(*p)) {
        // Motion continuously changes the derived lane controls. Advertise a
        // stable worst case instead of changing the host tail every block as
        // motionPhase advances.
        hold = 1.0;
        freeze = std::clamp(static_cast<double>(p->settings.base.freeze), 0.0, 1.0);
        feedback = 0.72;
        repeat = 0.82;
    } else {
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            const auto params = s3g::spectralTopologyLaneParams(
                p->settings, ch, kChannelCount);
            hold = std::max(hold, static_cast<double>(params.hold));
            freeze = std::max(freeze, static_cast<double>(params.freeze));
            feedback = std::max(feedback, static_cast<double>(params.feedback));
            repeat = std::max(repeat, static_cast<double>(params.repeat));
        }
    }
    if (freeze > 0.0001 && p->processor.hasCapture()) {
        return kClapTailInfinite;
    }

    const double sampleRate = std::max(1.0, p->sampleRate);
    const double hopSeconds = static_cast<double>(kFftHopFrames) / sampleRate;
    const double minus60Db = std::log(1000.0);
    const double holdTau = hold > 0.0001
        ? 0.025 * std::pow(160.0, std::clamp(hold, 0.0, 1.0))
        : 0.0;
    const double holdHops = holdTau > 0.0
        ? std::ceil(minus60Db * holdTau / hopSeconds)
        : 0.0;

    const double feedbackGain = std::clamp(feedback * 0.92, 0.0, 0.95);
    const double feedbackHops = feedbackGain > 0.0001
        ? std::ceil(std::log(0.001) / std::log(feedbackGain))
        : 0.0;

    const double repeatGain = std::clamp(repeat * 0.82, 0.0, 0.999);
    const double repeatCycles = repeatGain > 0.0001
        ? std::ceil(std::log(0.001) / std::log(repeatGain))
        : 0.0;
    const double repeatDelay = repeatGain > 0.0001
        ? 2.0 + std::floor(repeat * repeat
            * static_cast<double>(s3g::kSpectralMeshHistoryFrames - 3u))
        : 0.0;

    // Propagation is transparent at VEL=1 with zero dispersion. Otherwise an
    // active mesh can emit material from the oldest propagation frame, even
    // after the direct and locally processed paths have become silent. Each
    // feedback or repeat recurrence can launch another trip through that
    // history, so a single added span would under-report slow recirculation.
    const bool delayedPropagation = p->settings.topology.amount > 0.0001
        && p->propagationVelocity < 0.999999f;
    const double propagationTraversals = delayedPropagation
        ? 1.0 + feedbackHops + repeatCycles
        : 0.0;
    const double propagationHops = delayedPropagation
        ? static_cast<double>(s3g::kSpectralMeshPropagationFrames - 1u)
            * propagationTraversals
        : 0.0;

    const double tailFrames = static_cast<double>(p->processor.latencyFrames())
        + (holdHops + feedbackHops + repeatCycles * repeatDelay
            + propagationHops)
            * static_cast<double>(kFftHopFrames);
    return static_cast<uint32_t>(std::clamp(
        tailFrames, 0.0,
        static_cast<double>(kClapTailInfinite - 1u)));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
namespace {
NSRect spectralEngineCaptureButtonRect(CGFloat panelY)
{
    return NSMakeRect(kPrimaryPanelX + kPanelWidth - 86.0, panelY + 2.0, 38.0, 17.0);
}

NSRect spectralEngineClearButtonRect(CGFloat panelY)
{
    return NSMakeRect(kPrimaryPanelX + kPanelWidth - 44.0, panelY + 2.0, 38.0, 17.0);
}

NSString* spectralFrequencyText(float frequencyHz)
{
    return frequencyHz >= 1000.0f
        ? [NSString stringWithFormat:@"%.1fk", frequencyHz * 0.001f]
        : [NSString stringWithFormat:@"%.0f Hz", frequencyHz];
}
} // namespace

@interface S3G_SPECTRAL_TOPOLOGY_VIEW_CLASS : NSView {
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

@implementation S3G_SPECTRAL_TOPOLOGY_VIEW_CLASS
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
    NSString* pageLabels[2] = { @"TOPO", @"SONO" };
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
    // Every physical output is a mesh node. The patch matrix only determines
    // which nodes receive direct input; unpatched nodes can still receive
    // transported spectral material.
    const uint32_t visualLanes = kChannelCount;
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

    std::array<bool, kChannelCount * kChannelCount> drawnEdges {};
    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const auto pt = s3g::topologyPointForLane(lane, visualLanes, controls);
        const NSPoint a = [self projectPoint:pt inRect:topoRect];
        const auto nn = s3g::nearestTopologyNeighbors(state, lane, visualLanes);
        for (uint32_t i = 0; i < std::min<uint32_t>(state.neighborCount, 3u); ++i) {
            if (nn[i] < 0 || static_cast<uint32_t>(nn[i]) >= visualLanes) continue;
            const uint32_t destination = static_cast<uint32_t>(nn[i]);
            if (destination == lane) continue;
            const uint32_t low = std::min(lane, destination);
            const uint32_t high = std::max(lane, destination);
            const size_t edgeKey = static_cast<size_t>(low) * kChannelCount + high;
            if (drawnEdges[edgeKey]) continue;
            drawnEdges[edgeKey] = true;
            const float activity = std::clamp(std::max(
                p->processor.edgeActivity(lane, destination),
                p->processor.edgeActivity(destination, lane)), 0.0f, 1.0f);
            const CGFloat energy = static_cast<CGFloat>(std::sqrt(activity));
            [s3g::clap_gui::color(0xb8b8b8, 0.20 + energy * 0.78) setStroke];
            NSBezierPath* edge = [NSBezierPath bezierPath];
            [edge setLineWidth:0.65 + energy * 2.35];
            const auto other = s3g::topologyPointForLane(destination, visualLanes, controls);
            const NSPoint b = [self projectPoint:other inRect:topoRect];
            [edge moveToPoint:a];
            [edge lineToPoint:b];
            [edge stroke];

            auto drawPulse = [&](uint32_t source, uint32_t dest,
                                 NSPoint from, NSPoint to) {
                const float directedActivity = std::clamp(
                    p->processor.edgeActivity(source, dest), 0.0f, 1.0f);
                if (directedActivity <= 0.0001f) return;
                const float pulse = p->processor.edgePulsePosition(source, dest);
                if (!std::isfinite(pulse)) return;
                const CGFloat position = static_cast<CGFloat>(
                    std::clamp(pulse, 0.0f, 1.0f));
                const NSPoint marker = NSMakePoint(
                    from.x + (to.x - from.x) * position,
                    from.y + (to.y - from.y) * position);
                const CGFloat markerEnergy = static_cast<CGFloat>(
                    std::sqrt(directedActivity));
                const CGFloat markerSize = 3.0 + markerEnergy * 4.5;
                [s3g::clap_gui::color(
                    0xf2f2f2, 0.32 + markerEnergy * 0.68) setFill];
                NSRectFill(NSMakeRect(
                    marker.x - markerSize * 0.5,
                    marker.y - markerSize * 0.5,
                    markerSize, markerSize));
            };
            drawPulse(lane, destination, a, b);
            drawPulse(destination, lane, b, a);
        }
    }
    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const auto pt = s3g::topologyPointForLane(lane, visualLanes, controls);
        const NSPoint c = [self projectPoint:pt inRect:topoRect];
        const CGFloat size = 8.0 + static_cast<CGFloat>(std::clamp(pt.radius, 0.0, 1.5)) * 2.0;
        NSColor* nodeColor = patchOutputInjected(*p, lane) ? style.accent : style.text;
        [nodeColor setFill];
        NSRectFill(NSMakeRect(c.x - size * 0.5, c.y - size * 0.5, size, size));
        [[NSString stringWithFormat:@"L%u", lane + 1u] drawAtPoint:NSMakePoint(c.x + 7.0, c.y - 6.0) withAttributes:small];
    }

    NSString* topologyName = visualLanes == 8 ? @"8PT NEIGHBOR MAP"
        : visualLanes == 6 ? @"6PT NEIGHBOR MAP"
        : visualLanes == 4 ? @"4PT NEIGHBOR MAP"
        : [NSString stringWithFormat:@"%uPT NEIGHBOR MAP", visualLanes];
    [topologyName drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 10.0) withAttributes:small];
    [[NSString stringWithFormat:@"SHAPE = %@", [NSString stringWithUTF8String:s3g::topologyShapeName(state.shape)]]
        drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 25.0)
      withAttributes:small];
    NSString* xyzText = state.shape == 0u ? @"XYZ = TILT HOLD PHAS" : @"XYZ = BIN DROP RPT";
    [xyzText drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 40.0) withAttributes:small];
    [@"BRIGHT NODE = DIRECT IN" drawAtPoint:NSMakePoint(fieldRect.origin.x + 42.0, fieldRect.origin.y + 10.0) withAttributes:small];

    NSRect readoutButton = NSMakeRect(fieldRect.origin.x + fieldRect.size.width - 42.0, fieldRect.origin.y + 54.0, 32.0, 15.0);
    [style.strip setFill];
    NSRectFill(readoutButton);
    [style.grid setStroke];
    NSFrameRect(readoutButton);
    if (_showReadout) {
        [@"X" drawAtPoint:NSMakePoint(readoutButton.origin.x + 12.0, readoutButton.origin.y + 1.0) withAttributes:small];
        [@"BIN FRZ DMG RPT" drawAtPoint:NSMakePoint(fieldRect.origin.x + fieldRect.size.width - 188.0, fieldRect.origin.y + 55.0) withAttributes:small];
        for (uint32_t lane = 0; lane < visualLanes; ++lane) {
            const auto laneParams = s3g::spectralTopologyLaneParams(p->settings, lane, visualLanes);
            NSString* line = [NSString stringWithFormat:@"L%u %3.0f %.2f %.2f %.2f",
                                        lane + 1u,
                                        laneParams.sprayBins,
                                        laneParams.freeze,
                                        laneParams.damage,
                                        laneParams.repeat];
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
    const NSRect sonoRect = rect;
    [style.strip setFill];
    NSRectFill(sonoRect);
    [style.grid setStroke];
    NSFrameRect(sonoRect);
    [@"POST PROCESSING SONOGRAM" drawAtPoint:NSMakePoint(sonoRect.origin.x + 10.0, sonoRect.origin.y + 8.0) withAttributes:small];

    const uint32_t lanes = kChannelCount;
    const auto channelGrid =
        s3g::clap_gui::topologyProcessorChannelGrid(sonoRect, lanes);
    const uint32_t write = p->scopeWrite.load(std::memory_order_relaxed);
    constexpr uint32_t kTimeBins = 96u;
    constexpr uint32_t kFreqBins = 24u;
    constexpr uint32_t kWindow = 48u;
    const uint32_t oneSecondFrames = static_cast<uint32_t>(std::clamp(p->sampleRate, 8000.0, static_cast<double>(kScopeFrames - kWindow)));
    const uint32_t historyFrames = std::min<uint32_t>(kScopeFrames - kWindow, std::max<uint32_t>(kWindow, oneSecondFrames));
    constexpr double kTau = 6.28318530717958647692;

    for (uint32_t lane = 0; lane < lanes; ++lane) {
        const NSRect laneRect =
            s3g::clap_gui::topologyProcessorChannelRect(
                channelGrid, lane);
        [s3g::clap_gui::color(0x090b0d, 1.0) setFill];
        NSRectFill(laneRect);
        [style.grid setStroke];
        NSFrameRect(laneRect);

        std::array<float, kTimeBins * kFreqBins> mags {};
        float peak = 0.000001f;
        for (uint32_t t = 0; t < kTimeBins; ++t) {
            const uint32_t age = static_cast<uint32_t>((static_cast<double>(kTimeBins - 1u - t) / static_cast<double>(kTimeBins - 1u))
                                                       * static_cast<double>(historyFrames));
            const uint32_t center = (write + kScopeFrames - 1u - age) % kScopeFrames;
            for (uint32_t bin = 0; bin < kFreqBins; ++bin) {
                const uint32_t k = bin + 1u;
                double re = 0.0;
                double im = 0.0;
                for (uint32_t n = 0; n < kWindow; ++n) {
                    const uint32_t offset = (kWindow - 1u) - n;
                    const uint32_t index = (center + kScopeFrames - offset) % kScopeFrames;
                    const double window = 0.5 - 0.5 * std::cos(kTau * static_cast<double>(n) / static_cast<double>(kWindow - 1u));
                    const double sample = static_cast<double>(p->scope[lane][index].load(std::memory_order_relaxed)) * window;
                    const double phase = kTau * static_cast<double>(k) * static_cast<double>(n) / static_cast<double>(kWindow);
                    re += sample * std::cos(phase);
                    im -= sample * std::sin(phase);
                }
                const float mag = static_cast<float>(std::sqrt(re * re + im * im) / static_cast<double>(kWindow));
                mags[t * kFreqBins + bin] = mag;
                peak = std::max(peak, mag);
            }
        }

        const CGFloat pixelW = laneRect.size.width / static_cast<CGFloat>(kTimeBins);
        const CGFloat pixelH = laneRect.size.height / static_cast<CGFloat>(kFreqBins);
        for (uint32_t t = 0; t < kTimeBins; ++t) {
            for (uint32_t bin = 0; bin < kFreqBins; ++bin) {
                const float raw = mags[t * kFreqBins + bin] / peak;
                const double norm = std::pow(std::clamp(static_cast<double>(raw), 0.0, 1.0), 0.36);
                const uint8_t gray = static_cast<uint8_t>(std::clamp(10.0 + norm * 220.0, 10.0, 230.0));
                const uint32_t rgb = (static_cast<uint32_t>(gray) << 16u) | (static_cast<uint32_t>(gray) << 8u) | static_cast<uint32_t>(gray);
                [s3g::clap_gui::color(rgb, 0.98) setFill];
                NSRectFill(NSMakeRect(laneRect.origin.x + static_cast<CGFloat>(t) * pixelW,
                                      laneRect.origin.y + laneRect.size.height - static_cast<CGFloat>(bin + 1u) * pixelH,
                                      pixelW + 0.20,
                                      pixelH + 0.20));
            }
        }

        [[NSString stringWithFormat:@"L%u", lane + 1u] drawAtPoint:NSMakePoint(laneRect.origin.x + 5.0, laneRect.origin.y + 4.0) withAttributes:small];
    }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* lab = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSString* titleText = [NSString stringWithFormat:
        @"s3g PROCESSOR SPECTRAL %uCH", kChannelCount];
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
    drawHeader(@"SPECTRAL ENGINE", panelX, panelY);
    const NSRect engineHeader = NSMakeRect(panelX, panelY, panelW, headerH);
    const NSRect captureButton = spectralEngineCaptureButtonRect(panelY);
    const NSRect clearButton = spectralEngineClearButtonRect(panelY);
    s3g::clap_gui::drawHeaderActionButton(captureButton, engineHeader, @"CAP", small, style);
    s3g::clap_gui::drawHeaderActionButton(clearButton, engineHeader, @"CLR", small, style);
    if (p->processor.hasCapture()) {
        [style.accent setFill];
        NSRectFill(NSMakeRect(captureButton.origin.x + 3.0, captureButton.origin.y + 3.0, 3.0, 3.0));
    }
    [self drawEngineRow:@"BINS" value:[NSString stringWithFormat:@"%.0f", prm.sprayBins] norm:prm.sprayBins / 192.0f y:engineRowY(panelY, 0) attrs:small small:small];
    [self drawEngineRow:@"DRFT" value:[NSString stringWithFormat:@"%.0f%%", prm.drift * 100.0f] norm:prm.drift y:engineRowY(panelY, 1) attrs:small small:small];
    [self drawEngineRow:@"HOLD" value:[NSString stringWithFormat:@"%.0f%%", prm.hold * 100.0f] norm:prm.hold y:engineRowY(panelY, 2) attrs:small small:small];
    [self drawEngineRow:@"FRZ" value:[NSString stringWithFormat:@"%.0f%%", prm.freeze * 100.0f] norm:prm.freeze y:engineRowY(panelY, 3) attrs:small small:small];
    [self drawEngineRow:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", prm.feedback * 100.0f] norm:prm.feedback / 0.72f y:engineRowY(panelY, 4) attrs:small small:small];
    [self drawEngineRow:@"SMR" value:[NSString stringWithFormat:@"%.0f%%", prm.smear * 100.0f] norm:prm.smear y:engineRowY(panelY, 5) attrs:small small:small];
    [self drawEngineRow:@"HOLE" value:[NSString stringWithFormat:@"%.0f%%", prm.holes * 100.0f] norm:prm.holes / 0.60f y:engineRowY(panelY, 6) attrs:small small:small];
    [self drawEngineRow:@"PHAS" value:[NSString stringWithFormat:@"%.0f%%", prm.phaseBlur * 100.0f] norm:prm.phaseBlur y:engineRowY(panelY, 7) attrs:small small:small];
    [self drawEngineRow:@"DMG" value:[NSString stringWithFormat:@"%.0f%%", prm.damage * 100.0f] norm:prm.damage y:engineRowY(panelY, 8) attrs:small small:small];
    [self drawEngineRow:@"RPT" value:[NSString stringWithFormat:@"%.0f%%", prm.repeat * 100.0f] norm:prm.repeat y:engineRowY(panelY, 9) attrs:small small:small];
    [self drawEngineRow:@"TILT" value:[NSString stringWithFormat:@"%+.2f", prm.tilt] norm:(prm.tilt + 1.0f) * 0.5f y:engineRowY(panelY, 10) attrs:small small:small];
    [self drawEngineRow:@"LO" value:spectralFrequencyText(prm.loFreq) norm:prm.loFreq / 24000.0f y:engineRowY(panelY, 11) attrs:small small:small];
    [self drawEngineRow:@"HI" value:spectralFrequencyText(prm.hiFreq) norm:(prm.hiFreq - 20.0f) / 23980.0f y:engineRowY(panelY, 12) attrs:small small:small];
    [self drawEngineRow:@"TRANS" value:[NSString stringWithFormat:@"%.0f%%", p->transientProtect * 100.0f] norm:p->transientProtect y:engineRowY(panelY, 13) attrs:small small:small];
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

    const CGFloat propagationY = static_cast<CGFloat>(kPropagationPanelY);
    const CGFloat propagationH = static_cast<CGFloat>(kPropagationPanelHeight);
    s3g::clap_gui::drawPanelFrame(
        topologyX, propagationY, panelW, propagationH, style);
    drawHeader(@"PROPAGATION", topologyX, propagationY);
    s3g::clap_gui::drawProcessorSlider(
        @"VEL",
        [NSString stringWithFormat:@"%.0f%%", p->propagationVelocity * 100.0f],
        p->propagationVelocity,
        engineRowY(propagationY, 0u), topologyX, panelW,
        small, small, style);
    s3g::clap_gui::drawProcessorSlider(
        @"DISP",
        [NSString stringWithFormat:@"%+.2f", p->propagationDispersion],
        (p->propagationDispersion + 1.0f) * 0.5f,
        engineRowY(propagationY, 1u), topologyX, panelW,
        small, small, style);
    s3g::clap_gui::drawProcessorSlider(
        @"DAMP",
        [NSString stringWithFormat:@"%.0f%%", p->propagationDamping * 100.0f],
        p->propagationDamping,
        engineRowY(propagationY, 2u), topologyX, panelW,
        small, small, style);

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
        [@"PATCH: DIRECT IN / EMPTY: MESH RECEIVE" drawAtPoint:NSMakePoint(650.0, top + cell * kChannelCount + 18.0) withAttributes:small];
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
    const bool propagationParam =
        _dragParam >= static_cast<int>(kPropagationVelocityParamId)
        && _dragParam <= static_cast<int>(kPropagationDampingParamId);
    const bool topologyParam =
        _dragParam >= static_cast<int>(kTopologyAmountParamId)
        && _dragParam <= static_cast<int>(kTopologyCentroidParamId);
    const double panelX =
        topologyParam || propagationParam
        ? kSecondaryPanelX : kPrimaryPanelX;
    const double n = std::clamp(
        (point.x - s3g::gui_layout::processorControlX(panelX))
            / s3g::gui_layout::processorTrackWidth(kPanelWidth),
        0.0, 1.0);
    switch (_dragParam) {
    case kSprayBinsParamId: applyParam(*p, kSprayBinsParamId, n * 192.0); break;
    case kDriftParamId: applyParam(*p, kDriftParamId, n); break;
    case kHoldParamId: applyParam(*p, kHoldParamId, n); break;
    case kFreezeParamId: applyParam(*p, kFreezeParamId, n); break;
    case kFeedbackParamId: applyParam(*p, kFeedbackParamId, n * 0.72); break;
    case kSmearParamId: applyParam(*p, kSmearParamId, n); break;
    case kHolesParamId: applyParam(*p, kHolesParamId, n * 0.60); break;
    case kPhaseBlurParamId: applyParam(*p, kPhaseBlurParamId, n); break;
    case kDamageParamId: applyParam(*p, kDamageParamId, n); break;
    case kRepeatParamId: applyParam(*p, kRepeatParamId, n); break;
    case kTiltParamId: applyParam(*p, kTiltParamId, -1.0 + n * 2.0); break;
    case kLoFreqParamId: applyParam(*p, kLoFreqParamId, n * 24000.0); break;
    case kHiFreqParamId: applyParam(*p, kHiFreqParamId, 20.0 + n * 23980.0); break;
    case kTransientProtectParamId: applyParam(*p, kTransientProtectParamId, n); break;
    case kPropagationVelocityParamId: applyParam(*p, kPropagationVelocityParamId, n); break;
    case kPropagationDispersionParamId: applyParam(*p, kPropagationDispersionParamId, -1.0 + n * 2.0); break;
    case kPropagationDampingParamId: applyParam(*p, kPropagationDampingParamId, n); break;
    case kGainParamId: applyParam(*p, kGainParamId, -60.0 + n * 72.0); break;
    case kMixParamId: applyParam(*p, kMixParamId, n); break;
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
            pt, &p->plugin, @"Processor Spectral", titleBand,
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
    const NSRect captureButton = spectralEngineCaptureButtonRect(panelY);
    const NSRect clearButton = spectralEngineClearButtonRect(panelY);
    if (NSPointInRect(pt, captureButton)) {
        p->processor.requestCapture();
        markTailChanged(*p);
        if (p->host && p->host->request_process) p->host->request_process(p->host);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, clearButton)) {
        p->processor.requestClearCapture();
        markTailChanged(*p);
        if (p->host && p->host->request_process) p->host->request_process(p->host);
        [self setNeedsDisplay:YES];
        return;
    }
    const clap_id engineIds[] = {
        kSprayBinsParamId, kDriftParamId, kHoldParamId, kFreezeParamId,
        kFeedbackParamId, kSmearParamId, kHolesParamId, kPhaseBlurParamId,
        kDamageParamId, kRepeatParamId, kTiltParamId, kLoFreqParamId,
        kHiFreqParamId, kTransientProtectParamId
    };
    for (uint32_t i = 0; i < sizeof(engineIds) / sizeof(engineIds[0]); ++i) {
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

    const clap_id propagationIds[] = {
        kPropagationVelocityParamId,
        kPropagationDispersionParamId,
        kPropagationDampingParamId
    };
    for (uint32_t i = 0u; i < 3u; ++i) {
        const CGFloat rowY = static_cast<CGFloat>(
            engineRowY(kPropagationPanelY, i));
        if (!NSPointInRect(
                pt, NSMakeRect(topologyX, rowY - 8.0, panelW, 24.0))) {
            continue;
        }
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, propagationIds[i], &defaultValue)) {
            applyParam(*p, propagationIds[i], defaultValue);
            _dragParam = 0;
        } else {
            _dragParam = static_cast<int>(propagationIds[i]);
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
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G_SPECTRAL_TOPOLOGY_VIEW_CLASS alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight, kGuiWidth, 360u)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3G_SPECTRAL_TOPOLOGY_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h, kGuiWidth, 360u); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h, kGuiWidth, 360u); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3G_SPECTRAL_TOPOLOGY_VIEW_CLASS*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3G_SPECTRAL_TOPOLOGY_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    S3G_SPECTRAL_TOPOLOGY_PLUGIN_ID,
    S3G_SPECTRAL_TOPOLOGY_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Synchronized multichannel spectral mesh with delayed topology propagation, capture, and field memory.",
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
    p->settings.base.sprayBins = 18.0f;
    p->settings.base.drift = 0.18f;
    p->settings.base.hold = 0.72f;
    p->settings.base.freeze = 0.0f;
    p->settings.base.feedback = 0.18f;
    p->settings.base.smear = 0.25f;
    p->settings.base.holes = 0.05f;
    p->settings.base.phaseBlur = 0.28f;
    p->settings.base.damage = 0.0f;
    p->settings.base.repeat = 0.0f;
    p->settings.base.loFreq = 0.0f;
    p->settings.base.hiFreq = 20000.0f;
    p->settings.base.gainDb = -2.5f;
    p->settings.base.mix = 1.0f;
    p->settings.base.tilt = 0.0f;
    p->settings.base.safety = 0.82f;
    p->transientProtect = 0.35f;
    p->propagationVelocity = 1.0f;
    p->propagationDispersion = 0.0f;
    p->propagationDamping = 0.0f;
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
