#include "s3g_lane_patch.h"
#include "s3g_math.h"
#include "s3g_delay_processor.h"
#include "s3g_topology.h"
#include "s3g_topology_heatmap.h"

#include <clap/clap.h>
#include "s3g_realtime.h"
#include <clap/ext/latency.h>
#include <clap/ext/tail.h>
#if defined(__APPLE__)
#include <clap/ext/gui.h>
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
#include <cstdint>
#include <limits>
#include <new>

namespace {

#ifndef S3G_DELAY_PROCESSOR_CHANNEL_COUNT
#define S3G_DELAY_PROCESSOR_CHANNEL_COUNT 8
#endif

constexpr uint32_t kChannelCount = S3G_DELAY_PROCESSOR_CHANNEL_COUNT;
static_assert(kChannelCount > 0 && kChannelCount <= s3g::kLanePatchMaxChannels,
              "S3G_DELAY_PROCESSOR_CHANNEL_COUNT must fit the lane patch matrix");

#if S3G_DELAY_PROCESSOR_CHANNEL_COUNT == 24
constexpr const char* kPluginId = "org.s3g.s3g-dsp.delay-processor-24ch";
constexpr const char* kPluginName = "s3g Processor Delay 24ch";
constexpr const char* kPluginDescription =
    "24-channel topological delay with per-lane shaping, diffusion, and directed Echo Routes that walk repeats across the channel graph.";
#else
constexpr const char* kPluginId = "org.s3g.s3g-dsp.delay-processor-8ch";
constexpr const char* kPluginName = "s3g Processor Delay 8ch";
constexpr const char* kPluginDescription =
    "8-channel topological delay with per-lane shaping, diffusion, and directed Echo Routes that walk repeats across the channel graph.";
#endif

constexpr bool kLockUnusedChannelsToPassThrough = kChannelCount >= 24;
constexpr uint32_t kVisiblePatchChannels = kChannelCount < 24 ? kChannelCount : 24;
constexpr uint32_t kScopeFrames = 131072;
constexpr uint32_t kStateVersion = 11;
constexpr uint32_t kV10StateVersion = 10;
constexpr uint32_t kV9StateVersion = 9;
constexpr uint32_t kV8StateVersion = 8;
constexpr uint32_t kV7StateVersion = 7;
constexpr uint32_t kV6StateVersion = 6;
constexpr uint32_t kV5StateVersion = 5;
constexpr uint32_t kV4StateVersion = 4;
constexpr uint32_t kPreviousStateVersion = 3;
constexpr uint32_t kV2StateVersion = 2;
constexpr uint32_t kLegacyStateVersion = 1;
constexpr clap_id kDelayMsParamId = 1;
constexpr clap_id kFeedbackParamId = 2;
constexpr clap_id kMixParamId = 3;
constexpr clap_id kToneParamId = 4;
constexpr clap_id kTopologySpreadParamId = 5;
constexpr clap_id kTopologySkewParamId = 6;
constexpr clap_id kTopologyJitterParamId = 7;
constexpr clap_id kDisplaceCollapseParamId = 8;
constexpr clap_id kDisplaceDirXParamId = 9;
constexpr clap_id kDisplaceDirYParamId = 10;
constexpr clap_id kDisplaceDirZParamId = 11;
constexpr clap_id kDisplaceTwistParamId = 12;
constexpr clap_id kDisplaceFlareParamId = 13;
constexpr clap_id kPitchParamId = 14;
constexpr clap_id kTopologyShapeParamId = 15;
constexpr clap_id kCharacterParamId = 16;
constexpr clap_id kOutputTrimParamId = 17;
constexpr clap_id kTapParamId = 18;
constexpr clap_id kTopologyMotionModeParamId = 19;
constexpr clap_id kTopologyMotionRateParamId = 20;
constexpr clap_id kTopologyMotionDepthParamId = 21;
constexpr clap_id kTopologyNeighborCountParamId = 22;
constexpr clap_id kTopologyRadiusParamId = 23;
constexpr clap_id kTopologyCentroidParamId = 24;
constexpr clap_id kTopologyMotionVariantParamId = 25;
constexpr clap_id kRouteAmountParamId = 26;
constexpr clap_id kRouteTurnParamId = 27;
constexpr clap_id kRouteBranchParamId = 28;
constexpr clap_id kRouteLossParamId = 29;
constexpr uint32_t kParameterBankSize = 30;
constexpr uint32_t kTopologyShapeCount = s3g::kTopologyShapeCount;
constexpr uint32_t kTopologyMotionModeCount = s3g::kTopologyMotionModeCount;
constexpr uint32_t kTopologyVariantCount = s3g::kTopologyVariantCount;
constexpr double kDelayMinMs = 20.0;
constexpr double kDelayMaxMs = 1995.0;
constexpr double kPitchMinSemitones = -24.0;
constexpr double kPitchMaxSemitones = 24.0;
constexpr double kOutputTrimMinDb = -24.0;
constexpr double kOutputTrimMaxDb = 6.0;
constexpr double kTopologyDelaySpreadMs = 1550.0;
constexpr double kTopologyDelayJitterMs = 520.0;
constexpr double kTopologyFeedbackSpread = 0.56;
constexpr double kTopologyFeedbackJitter = 0.24;
constexpr double kTopologyToneSpread = 0.42;
constexpr double kTopologyNetworkSpread = 0.68;
constexpr double kTopologyPitchSpreadSemitones = 3.0;
constexpr double kTopologyMotionMinHz = 0.01;
constexpr double kTopologyMotionMaxHz = 4.0;

const char* topologyShapeName(uint32_t shape)
{
    return s3g::topologyShapeName(shape);
}

const char* topologyMotionModeName(uint32_t mode)
{
    return s3g::topologyMotionModeName(mode);
}

const char* topologyVariantName(uint32_t variant)
{
    return s3g::topologyVariantName(variant);
}

struct __attribute__((packed)) SavedStateV10 {
    uint32_t version = kV10StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double tapAmount = 0.0;
    double outputTrimDb = -6.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
    uint32_t topologyMotionMode = 0;
    uint32_t topologyMotionVariant = 0;
    double topologyMotionRateHz = 0.10;
    double topologyMotionDepth = 0.0;
    uint32_t topologyNeighborCount = 2;
    double topologyRadius = 0.65;
    double topologyCentroid = 0.22;
};

struct __attribute__((packed)) SavedState {
    uint32_t version = kStateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double tapAmount = 0.0;
    double outputTrimDb = -6.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
    uint32_t topologyMotionMode = 0;
    uint32_t topologyMotionVariant = 0;
    double topologyMotionRateHz = 0.10;
    double topologyMotionDepth = 0.0;
    uint32_t topologyNeighborCount = 2;
    double topologyRadius = 0.65;
    double topologyCentroid = 0.22;
    double routeAmount = 0.0;
    double routeTurn = 0.0;
    double routeBranch = 0.35;
    double routeLoss = 0.25;
    double topologyMotionPhase = 0.0;
};

struct __attribute__((packed)) SavedStateV9 {
    uint32_t version = kV9StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double tapAmount = 0.0;
    double outputTrimDb = -6.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
    uint32_t topologyMotionMode = 0;
    double topologyMotionRateHz = 0.10;
    double topologyMotionDepth = 0.0;
    uint32_t topologyNeighborCount = 2;
    double topologyRadius = 0.65;
    double topologyCentroid = 0.22;
};

struct __attribute__((packed)) SavedStateV8 {
    uint32_t version = kV8StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double tapAmount = 0.0;
    double outputTrimDb = -6.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
    uint32_t topologyMotionMode = 0;
    double topologyMotionRateHz = 0.10;
    double topologyMotionDepth = 0.0;
};

struct __attribute__((packed)) SavedStateV7 {
    uint32_t version = kV7StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double tapAmount = 0.0;
    double outputTrimDb = -6.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
};

struct __attribute__((packed)) SavedStateV6 {
    uint32_t version = kV6StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double outputTrimDb = -6.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
};

struct __attribute__((packed)) SavedStateV5 {
    uint32_t version = kV5StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
};

struct __attribute__((packed)) SavedStateV4 {
    uint32_t version = kV4StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
    uint32_t topologyShape = 0;
};

struct __attribute__((packed)) SavedStateV3 {
    uint32_t version = kPreviousStateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    double pitchSemitones = 0.0;
};

struct __attribute__((packed)) SavedStateV2 {
    uint32_t version = kV2StateVersion;
    uint64_t patchRows[s3g::kLanePatchMaxChannels] {};
    uint32_t clearUnused = 0;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
};

struct SavedStateV1 {
    uint32_t version = kLegacyStateVersion;
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
};

static_assert(sizeof(SavedStateV1) == 64u);
static_assert(offsetof(SavedStateV10, patchRows) == 4u);
static_assert(offsetof(SavedStateV10, delayMs) == 520u);
static_assert(offsetof(SavedStateV10, topologyCentroid) == 696u);
static_assert(sizeof(SavedStateV10) == 704u);
static_assert(offsetof(SavedState, routeAmount) == 704u);
static_assert(offsetof(SavedState, topologyMotionPhase) == 736u);
static_assert(sizeof(SavedState) == 744u);

struct DelaySettings {
    double delayMs = 280.0;
    double feedback = 0.35;
    double mix = 0.45;
    double tone = 0.60;
    double character = 0.0;
    double tapAmount = 0.0;
    double outputTrimDb = -6.0;
    double pitchSemitones = 0.0;
    double topologySpread = 0.0;
    double topologySkew = 0.0;
    double topologyJitter = 0.0;
    double displaceCollapse = 0.0;
    double displaceDirX = 0.0;
    double displaceDirY = 0.0;
    double displaceDirZ = 1.0;
    double displaceTwist = 0.0;
    double displaceFlare = 0.0;
    uint32_t topologyMotionMode = 0;
    uint32_t topologyMotionVariant = 0;
    double topologyMotionRateHz = 0.10;
    double topologyMotionDepth = 0.0;
    double topologyMotionPhase = 0.0;
    uint32_t topologyNeighborCount = 2;
    double topologyRadius = 0.65;
    double topologyCentroid = 0.22;
    uint32_t topologyShape = 0;
    double routeAmount = 0.0;
    double routeTurn = 0.0;
    double routeBranch = 0.35;
    double routeLoss = 0.25;
};

struct Plugin : DelaySettings {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    bool clearUnused = false;
    const clap_host_tail_t* hostTail = nullptr;
    DelaySettings audioSettings {};
    std::array<std::atomic<double>, kParameterBankSize> parameterValues {};
    std::atomic<uint64_t> parameterRevision { 1u };
    uint64_t audioParameterRevision = 0u;
    std::atomic<double> publishedMotionPhase { 0.0 };
    std::atomic<bool> motionPhaseRestorePending { false };
    std::atomic<bool> clearUnusedPublished { false };
    bool audioClearUnused = false;
    std::array<std::atomic<uint64_t>, kChannelCount> patchRowsPublished {};
    std::atomic<uint64_t> patchRevision { 1u };
    uint64_t audioPatchRevision = 0u;
    std::array<uint64_t, kChannelCount> audioPatchRows {};
    std::array<uint32_t, kChannelCount> audioActiveLanes {};
    uint32_t audioActiveLaneCount = kChannelCount;
    std::atomic<bool> tailChangePending { false };
    uint32_t audioLegacyTailRemainingFrames = 0u;
    std::atomic<uint32_t> publishedLegacyTailFrames { 0u };
    std::atomic<uint32_t> publishedRouteTailFrames { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> outputClip { false };
    std::array<std::array<std::atomic<float>, kScopeFrames>, kChannelCount> scope {};
    std::array<std::array<std::atomic<float>, kChannelCount>, kChannelCount> routeEdgeEnergy {};
    std::array<std::array<std::atomic<float>, kChannelCount>, kChannelCount> routeEdgePhase {};
    std::array<std::atomic<float>, kChannelCount> routeNodeEnergy {};
    std::atomic<float> routeCentroidEnergy { 0.0f };
    std::atomic<float> routeCentroidPhase { 0.0f };
    std::atomic<uint32_t> scopeWrite { 0u };
    uint32_t meterRedrawCountdown = 0;
    s3g::LanePatch patch;
    s3g::DelayProcessor delay;
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
    std::atomic<bool> guiDirty { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}


void requestGuiRedraw(Plugin& p);
void publishRouteTelemetry(Plugin& p);
void publishLegacyTail(
    Plugin& p, uint32_t elapsedFrames, bool clearResidual);

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double clampFeedback(double value)
{
    return std::clamp(value, 0.0, 0.82);
}

double clampDelayMs(double value)
{
    return std::clamp(value, kDelayMinMs, kDelayMaxMs);
}

double clampBipolar(double value)
{
    return std::clamp(value, -1.0, 1.0);
}

double clampOutputTrimDb(double value)
{
    return std::clamp(value, kOutputTrimMinDb, kOutputTrimMaxDb);
}

double clampMotionRateHz(double value)
{
    return std::clamp(value, kTopologyMotionMinHz, kTopologyMotionMaxHz);
}

float dbToGain(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

double laneNoise(uint32_t channel)
{
    return s3g::laneNoise(channel);
}

using TopologyPoint = s3g::TopologyPoint;

template <typename Settings>
bool topologyMotionActive(const Settings& p)
{
    s3g::TopologyState state {};
    state.motionMode = p.topologyMotionMode;
    state.motionDepth = p.topologyMotionDepth;
    state.motionRateHz = p.topologyMotionRateHz;
    return s3g::topologyMotionActive(state);
}

template <typename Settings>
s3g::TopologyState topologyStateForPlugin(const Settings& p)
{
    s3g::TopologyState state {};
    state.amount = p.topologySpread;
    state.jitter = p.topologyJitter;
    state.collapse = p.displaceCollapse;
    state.dirX = p.displaceDirX;
    state.dirY = p.displaceDirY;
    state.dirZ = p.displaceDirZ;
    state.twist = p.displaceTwist;
    state.flare = p.displaceFlare;
    state.shape = p.topologyShape;
    state.motionMode = p.topologyMotionMode;
    state.motionVariant = p.topologyMotionVariant;
    state.motionRateHz = p.topologyMotionRateHz;
    state.motionDepth = p.topologyMotionDepth;
    state.motionPhase = p.topologyMotionPhase;
    state.neighborCount = p.topologyNeighborCount;
    state.neighborRadius = p.topologyRadius;
    state.centroidAmount = p.topologyCentroid;
    return state;
}

template <typename Settings>
s3g::TopologyControls topologyControlsForPlugin(const Settings& p)
{
    return s3g::topologyControlsFromState(topologyStateForPlugin(p));
}

template <typename Settings>
TopologyPoint topologyPointForLane(const Settings& p, uint32_t channel, uint32_t count)
{
    return s3g::topologyPointForLane(channel, count, topologyControlsForPlugin(p));
}

template <typename Settings>
double topologyLaneValue(const Settings& p, uint32_t channel, uint32_t count)
{
    return topologyPointForLane(p, channel, count).lane;
}

template <typename Settings>
double topologyAmount(const Settings& p)
{
    const double motionAmount = std::max({
        p.topologySpread,
        p.displaceCollapse,
        std::fabs(p.displaceTwist) * 0.65,
        std::fabs(p.displaceFlare) * 0.55,
        topologyMotionActive(p) ? p.topologyMotionDepth * 0.85 : 0.0
    });
    return s3g::topologyAmount(motionAmount);
}

uint32_t legacyTailEstimateFrames(
    const DelaySettings& settings, double sampleRate)
{
    const double amount = topologyAmount(settings);
    const double feedbackEstimate = std::clamp(
        settings.feedback + amount * kTopologyFeedbackSpread
            + settings.topologyJitter * kTopologyFeedbackJitter,
        0.0,
        0.82);
    const double repeatsToMinus60 = feedbackEstimate > 0.001
        ? std::ceil(std::log(0.001) / std::log(feedbackEstimate))
        : 1.0;
    const double tailSeconds = std::clamp(
        2.25 * repeatsToMinus60 + 0.5, 0.5, 120.0);
    const uint64_t frames = static_cast<uint64_t>(std::ceil(
        tailSeconds * std::max(1.0, sampleRate)));
    return static_cast<uint32_t>(std::min<uint64_t>(
        frames,
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max() - 1)));
}

template <typename Settings>
std::array<int, 3> nearestTopologyNeighbors(const Settings& p, uint32_t channel, uint32_t count)
{
    return s3g::nearestTopologyNeighbors(topologyStateForPlugin(p), channel, count);
}

template <typename Settings>
double resolvedChannelDelayMs(const Settings& p, uint32_t channel, uint32_t count)
{
    const auto topo = topologyPointForLane(p, channel, count);
    const double amount = topologyAmount(p);
    const double delayField = std::clamp(
        topo.x * 0.86 +
            topo.z * 0.70 +
            topo.y * 0.34 +
            (topo.radius - 1.0) * 0.86 +
            laneNoise(channel) * p.topologyJitter * 0.55,
        -1.0,
        1.0);
    return std::clamp(
        p.delayMs +
            delayField * amount * kTopologyDelaySpreadMs +
            laneNoise(channel + 11u) * p.topologyJitter * kTopologyDelayJitterMs,
        kDelayMinMs,
        kDelayMaxMs);
}

template <typename Settings>
double resolvedChannelFeedback(const Settings& p, uint32_t channel, uint32_t count)
{
    const auto topo = topologyPointForLane(p, channel, count);
    const double amount = topologyAmount(p);
    const double feedbackField = std::clamp(
        topo.y * 0.78 -
            topo.z * 0.42 +
            topo.x * 0.22 +
            (topo.radius - 1.0) * 0.45 +
            laneNoise(channel + 23u) * p.topologyJitter * 0.55,
        -1.0,
        1.0);
    return std::clamp(
        p.feedback +
            feedbackField * amount * kTopologyFeedbackSpread +
            laneNoise(channel + 29u) * p.topologyJitter * kTopologyFeedbackJitter,
        0.0,
        0.82);
}

template <typename Settings>
double resolvedChannelTone(const Settings& p, uint32_t channel, uint32_t count)
{
    const auto topo = topologyPointForLane(p, channel, count);
    const double amount = topologyAmount(p);
    const double toneField = std::clamp(
        -topo.y * 0.70 +
            topo.x * 0.36 +
            (topo.radius - 1.0) * 0.52 +
            laneNoise(channel + 47u) * p.topologyJitter * 0.45,
        -1.0,
        1.0);
    return std::clamp(p.tone + toneField * amount * kTopologyToneSpread, 0.0, 1.0);
}

template <typename Settings>
double resolvedChannelNetwork(const Settings& p, uint32_t channel, uint32_t count)
{
    const auto topo = topologyPointForLane(p, channel, count);
    const double amount = topologyAmount(p);
    const double shapeBias = p.topologyShape == 3 ? 0.28
        : p.topologyShape == 5 ? 0.24
        : p.topologyShape == 7 ? 0.18
        : p.topologyShape == 6 ? 0.26
        : 0.10;
    const double field = std::clamp(
        std::fabs(topo.x - topo.z) * 0.40 +
            std::fabs(topo.y) * 0.22 +
            std::max(0.0, topo.radius - 1.0) * 0.46 +
            p.topologyJitter * 0.35 +
            shapeBias,
        0.0,
        1.0);
    return std::clamp(field * amount * kTopologyNetworkSpread, 0.0, 0.68);
}

template <typename Settings>
double resolvedChannelCharacter(const Settings& p, uint32_t channel, uint32_t count)
{
    const auto topo = topologyPointForLane(p, channel, count);
    const double amount = topologyAmount(p);
    const double field = std::clamp(
        topo.radius * 0.44 +
            std::fabs(topo.x - topo.z) * 0.28 +
            std::max(0.0, -topo.y) * 0.22 +
            p.topologyJitter * 0.30,
        0.0,
        1.0);
    return std::clamp(p.character + field * amount * 0.62, 0.0, 1.0);
}

template <typename Settings>
double resolvedChannelSmearAmount(const Settings& p, uint32_t channel, uint32_t count)
{
    const auto topo = topologyPointForLane(p, channel, count);
    const double amount = topologyAmount(p);
    const double field = std::clamp(
        std::fabs(topo.lane) * 0.34 +
            std::max(0.0, topo.radius - 0.82) * 0.42 +
            std::fabs(topo.x + topo.z) * 0.20 +
            p.topologyJitter * 0.24,
        0.0,
        1.0);
    return std::clamp(p.tapAmount + field * amount * 0.54, 0.0, 1.0);
}

template <typename Settings>
double resolvedChannelPitchSemitones(const Settings& p, uint32_t channel, uint32_t count)
{
    const auto topo = topologyPointForLane(p, channel, count);
    const double amount = topologyAmount(p);
    const double shapeBias = p.topologyShape == 6 ? laneNoise(channel + 89u) * 0.55
        : p.topologyShape == 5 ? (static_cast<double>(static_cast<int>(channel % 3u)) - 1.0) * 0.42
        : p.topologyShape == 8 ? std::sin(std::atan2(topo.x, topo.z) * 2.0) * 0.45
        : 0.0;
    const double pitchField = std::clamp(
        topo.y * 0.68 +
            (topo.radius - 1.0) * 0.58 +
            topo.z * 0.26 +
            laneNoise(channel + 61u) * p.topologyJitter * 0.42 +
            shapeBias,
        -1.0,
        1.0);
    return std::clamp(
        p.pitchSemitones + pitchField * amount * kTopologyPitchSpreadSemitones,
        kPitchMinSemitones,
        kPitchMaxSemitones);
}

constexpr std::array<clap_id, 29> kStoredParamIds {
    kDelayMsParamId,
    kFeedbackParamId,
    kMixParamId,
    kToneParamId,
    kTopologySpreadParamId,
    kTopologySkewParamId,
    kTopologyJitterParamId,
    kDisplaceCollapseParamId,
    kDisplaceDirXParamId,
    kDisplaceDirYParamId,
    kDisplaceDirZParamId,
    kDisplaceTwistParamId,
    kDisplaceFlareParamId,
    kPitchParamId,
    kTopologyShapeParamId,
    kCharacterParamId,
    kOutputTrimParamId,
    kTapParamId,
    kTopologyMotionModeParamId,
    kTopologyMotionRateParamId,
    kTopologyMotionDepthParamId,
    kTopologyNeighborCountParamId,
    kTopologyRadiusParamId,
    kTopologyCentroidParamId,
    kTopologyMotionVariantParamId,
    kRouteAmountParamId,
    kRouteTurnParamId,
    kRouteBranchParamId,
    kRouteLossParamId,
};

template <typename Settings>
bool assignSettingsParam(Settings& p, clap_id paramId, double value)
{
    switch (paramId) {
    case kDelayMsParamId: p.delayMs = clampDelayMs(value); break;
    case kFeedbackParamId: p.feedback = clampFeedback(value); break;
    case kMixParamId: p.mix = clamp01(value); break;
    case kToneParamId: p.tone = clamp01(value); break;
    case kCharacterParamId: p.character = clamp01(value); break;
    case kTapParamId: p.tapAmount = clamp01(value); break;
    case kOutputTrimParamId: p.outputTrimDb = clampOutputTrimDb(value); break;
    case kPitchParamId: p.pitchSemitones = std::clamp(value, kPitchMinSemitones, kPitchMaxSemitones); break;
    case kTopologyShapeParamId: p.topologyShape = std::min<uint32_t>(kTopologyShapeCount - 1u, roundedUint(value)); break;
    case kTopologySpreadParamId: p.topologySpread = clamp01(value); break;
    case kTopologySkewParamId: p.topologySkew = clampBipolar(value); break;
    case kTopologyJitterParamId: p.topologyJitter = clamp01(value); break;
    case kDisplaceCollapseParamId: p.displaceCollapse = clamp01(value); break;
    case kDisplaceDirXParamId: p.displaceDirX = clampBipolar(value); break;
    case kDisplaceDirYParamId: p.displaceDirY = clampBipolar(value); break;
    case kDisplaceDirZParamId: p.displaceDirZ = clampBipolar(value); break;
    case kDisplaceTwistParamId: p.displaceTwist = clampBipolar(value); break;
    case kDisplaceFlareParamId: p.displaceFlare = clampBipolar(value); break;
    case kTopologyMotionModeParamId:
        p.topologyMotionMode = std::min<uint32_t>(kTopologyMotionModeCount - 1u, roundedUint(value));
        break;
    case kTopologyMotionVariantParamId: p.topologyMotionVariant = std::min<uint32_t>(kTopologyVariantCount - 1u, roundedUint(value)); break;
    case kTopologyMotionRateParamId: p.topologyMotionRateHz = clampMotionRateHz(value); break;
    case kTopologyMotionDepthParamId: p.topologyMotionDepth = clamp01(value); break;
    case kTopologyNeighborCountParamId: p.topologyNeighborCount = std::clamp<uint32_t>(roundedUint(value), 1u, 3u); break;
    case kTopologyRadiusParamId: p.topologyRadius = clamp01(value); break;
    case kTopologyCentroidParamId: p.topologyCentroid = clamp01(value); break;
    case kRouteAmountParamId: p.routeAmount = clamp01(value); break;
    case kRouteTurnParamId: p.routeTurn = clampBipolar(value); break;
    case kRouteBranchParamId: p.routeBranch = clamp01(value); break;
    case kRouteLossParamId: p.routeLoss = clamp01(value); break;
    default: return false;
    }
    return true;
}

template <typename Settings>
bool settingsParamValue(const Settings& p, clap_id paramId, double& value)
{
    switch (paramId) {
    case kDelayMsParamId: value = p.delayMs; break;
    case kFeedbackParamId: value = p.feedback; break;
    case kMixParamId: value = p.mix; break;
    case kToneParamId: value = p.tone; break;
    case kCharacterParamId: value = p.character; break;
    case kTapParamId: value = p.tapAmount; break;
    case kOutputTrimParamId: value = p.outputTrimDb; break;
    case kPitchParamId: value = p.pitchSemitones; break;
    case kTopologyShapeParamId: value = p.topologyShape; break;
    case kTopologySpreadParamId: value = p.topologySpread; break;
    case kTopologySkewParamId: value = p.topologySkew; break;
    case kTopologyJitterParamId: value = p.topologyJitter; break;
    case kDisplaceCollapseParamId: value = p.displaceCollapse; break;
    case kDisplaceDirXParamId: value = p.displaceDirX; break;
    case kDisplaceDirYParamId: value = p.displaceDirY; break;
    case kDisplaceDirZParamId: value = p.displaceDirZ; break;
    case kDisplaceTwistParamId: value = p.displaceTwist; break;
    case kDisplaceFlareParamId: value = p.displaceFlare; break;
    case kTopologyMotionModeParamId: value = p.topologyMotionMode; break;
    case kTopologyMotionVariantParamId: value = p.topologyMotionVariant; break;
    case kTopologyMotionRateParamId: value = p.topologyMotionRateHz; break;
    case kTopologyMotionDepthParamId: value = p.topologyMotionDepth; break;
    case kTopologyNeighborCountParamId: value = p.topologyNeighborCount; break;
    case kTopologyRadiusParamId: value = p.topologyRadius; break;
    case kTopologyCentroidParamId: value = p.topologyCentroid; break;
    case kRouteAmountParamId: value = p.routeAmount; break;
    case kRouteTurnParamId: value = p.routeTurn; break;
    case kRouteBranchParamId: value = p.routeBranch; break;
    case kRouteLossParamId: value = p.routeLoss; break;
    default: return false;
    }
    return true;
}

void storeSettingsInParameterBank(Plugin& p, const DelaySettings& settings)
{
    for (const clap_id id : kStoredParamIds) {
        double value = 0.0;
        if (settingsParamValue(settings, id, value)) {
            p.parameterValues[id].store(value, std::memory_order_relaxed);
        }
    }
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
}

void loadSettingsFromParameterBank(const Plugin& p, DelaySettings& settings)
{
    const double phase = settings.topologyMotionPhase;
    for (const clap_id id : kStoredParamIds) {
        assignSettingsParam(
            settings, id,
            p.parameterValues[id].load(std::memory_order_relaxed));
    }
    settings.topologyMotionPhase = phase;
}

void syncGuiSettings(Plugin& p)
{
    loadSettingsFromParameterBank(p, static_cast<DelaySettings&>(p));
    p.topologyMotionPhase = p.publishedMotionPhase.load(
        std::memory_order_relaxed);
}

void markTailChanged(Plugin& p)
{
    p.tailChangePending.store(true, std::memory_order_release);
}

void deliverTailChangedOnAudioThread(Plugin& p)
{
    if (p.tailChangePending.exchange(false, std::memory_order_acq_rel)
        && p.host && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

void syncAudioPatch(Plugin& p, bool force = false)
{
    const uint64_t revision = p.patchRevision.load(std::memory_order_acquire);
    if (!force && revision == p.audioPatchRevision) return;
    p.audioActiveLaneCount = 0u;
    for (uint32_t row = 0; row < kChannelCount; ++row) {
        const uint64_t mask = p.patchRowsPublished[row].load(
            std::memory_order_acquire);
        p.audioPatchRows[row] = mask;
        if (mask != 0u) {
            p.audioActiveLanes[p.audioActiveLaneCount++] = row;
        }
    }
    if (p.audioActiveLaneCount == 0u) {
        for (uint32_t row = 0; row < kChannelCount; ++row) {
            p.audioActiveLanes[row] = row;
        }
        p.audioActiveLaneCount = kChannelCount;
    }
    p.audioPatchRevision = revision;
}

void applyParamsToDsp(Plugin& p, const DelaySettings& settings)
{
    const uint32_t topologyCount = std::max<uint32_t>(
        1u, std::min<uint32_t>(kChannelCount, p.audioActiveLaneCount));
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        uint32_t logicalLane = 0u;
        bool laneIsActive = false;
        for (uint32_t ordinal = 0; ordinal < topologyCount; ++ordinal) {
            if (p.audioActiveLanes[ordinal] == ch) {
                logicalLane = ordinal;
                laneIsActive = true;
                break;
            }
        }
        p.delay.setChannelDelayMs(static_cast<int>(ch), static_cast<float>(resolvedChannelDelayMs(settings, logicalLane, topologyCount)));
        p.delay.setChannelFeedback(static_cast<int>(ch), static_cast<float>(resolvedChannelFeedback(settings, logicalLane, topologyCount)));
        p.delay.setChannelTone(static_cast<int>(ch), static_cast<float>(resolvedChannelTone(settings, logicalLane, topologyCount)));
        p.delay.setChannelNetwork(static_cast<int>(ch), static_cast<float>(resolvedChannelNetwork(settings, logicalLane, topologyCount)));
        const auto logicalNeighbors = nearestTopologyNeighbors(settings, logicalLane, topologyCount);
        std::array<int, 3> physicalNeighbors {
            static_cast<int>(ch), static_cast<int>(ch), static_cast<int>(ch)
        };
        if (laneIsActive) {
            for (uint32_t i = 0; i < physicalNeighbors.size(); ++i) {
                const int logical = logicalNeighbors[i];
                if (logical >= 0 && static_cast<uint32_t>(logical) < topologyCount) {
                    physicalNeighbors[i] = static_cast<int>(
                        p.audioActiveLanes[static_cast<uint32_t>(logical)]);
                }
            }
        }
        p.delay.setChannelNetworkTopology(static_cast<int>(ch),
            physicalNeighbors[0],
            physicalNeighbors[1],
            physicalNeighbors[2],
            laneIsActive
                ? static_cast<int>(std::clamp<uint32_t>(settings.topologyNeighborCount, 1u, 3u))
                : 0,
            static_cast<float>(settings.topologyCentroid));
        p.delay.setChannelCharacter(static_cast<int>(ch), static_cast<float>(resolvedChannelCharacter(settings, logicalLane, topologyCount)));
        p.delay.setChannelSmearAmount(static_cast<int>(ch), static_cast<float>(resolvedChannelSmearAmount(settings, logicalLane, topologyCount)));
        p.delay.setChannelPitchSemitones(static_cast<int>(ch), static_cast<float>(resolvedChannelPitchSemitones(settings, logicalLane, topologyCount)));
    }

    s3g::DelayRouteParams route {};
    route.route = static_cast<float>(settings.routeAmount);
    route.turn = static_cast<float>(settings.routeTurn);
    route.branch = static_cast<float>(settings.routeBranch);
    route.loss = static_cast<float>(settings.routeLoss);
    p.delay.setRouteParams(route);
    p.delay.setTopology(
        topologyStateForPlugin(settings),
        p.audioActiveLanes.data(), p.audioActiveLaneCount);
}

void syncAudioSettings(Plugin& p, bool force = false)
{
    const uint64_t parameterRevision = p.parameterRevision.load(
        std::memory_order_acquire);
    const uint64_t patchRevision = p.patchRevision.load(
        std::memory_order_acquire);
    const bool parametersChanged = force
        || parameterRevision != p.audioParameterRevision;
    const bool patchChanged = force || patchRevision != p.audioPatchRevision;
    if (!parametersChanged && !patchChanged) return;

    if (patchChanged) syncAudioPatch(p, true);
    if (parametersChanged) {
        loadSettingsFromParameterBank(p, p.audioSettings);
        if (p.motionPhaseRestorePending.exchange(
                false, std::memory_order_acq_rel)) {
            p.audioSettings.topologyMotionPhase =
                p.publishedMotionPhase.load(std::memory_order_relaxed);
        }
        if (p.audioSettings.topologyMotionMode == 0u) {
            p.audioSettings.topologyMotionPhase = 0.0;
            p.publishedMotionPhase.store(0.0, std::memory_order_relaxed);
        }
        p.audioParameterRevision = parameterRevision;
    }
    p.audioClearUnused = p.clearUnusedPublished.load(std::memory_order_acquire);
    applyParamsToDsp(p, p.audioSettings);
}

void advanceTopologyMotion(Plugin& p, uint32_t frames)
{
    auto& settings = p.audioSettings;
    if (!topologyMotionActive(settings) || p.sampleRate <= 0.0 || frames == 0) {
        return;
    }
    settings.topologyMotionPhase +=
        (static_cast<double>(frames) / p.sampleRate)
        * settings.topologyMotionRateHz;
    settings.topologyMotionPhase -= std::floor(settings.topologyMotionPhase);
    p.publishedMotionPhase.store(
        settings.topologyMotionPhase, std::memory_order_relaxed);
    applyParamsToDsp(p, settings);
    requestGuiRedraw(p);
}

void requestGuiRedraw(Plugin& p)
{
#if defined(__APPLE__)
    if (!p.guiView || !p.guiVisible.load(std::memory_order_relaxed)) {
        return;
    }
    const bool wasDirty = p.guiDirty.exchange(true, std::memory_order_release);
    if (!wasDirty && p.host && p.host->request_callback) {
        p.host->request_callback(p.host);
    }
#else
    (void)p;
#endif
}

void preparePatch(Plugin& p)
{
    p.patch.setWidth(kChannelCount);
    if constexpr (kLockUnusedChannelsToPassThrough) {
        p.clearUnused = false;
    }
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
    p.clearUnusedPublished.store(p.clearUnused, std::memory_order_release);
    for (uint32_t row = 0; row < kChannelCount; ++row) {
        p.patchRowsPublished[row].store(
            p.patch.rowMask(row), std::memory_order_relaxed);
    }
    p.patchRevision.fetch_add(1u, std::memory_order_release);
}

void togglePatchCellFromGui(Plugin& p, uint32_t input, uint32_t output)
{
    p.clearUnused = !kLockUnusedChannelsToPassThrough;
    p.patch.setWidth(kChannelCount);
    p.patch.toggle(input, output);
    preparePatch(p);
    markTailChanged(p);
    requestGuiRedraw(p);
}

bool init(const clap_plugin_t*) { return true; }

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
    p->maxFrames = maxFrames;
    p->meterRedrawCountdown = static_cast<uint32_t>(std::max(1.0, sampleRate / 24.0));
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->outputClip.store(false, std::memory_order_relaxed);
    preparePatch(*p);
    p->delay.prepare(sampleRate, static_cast<int>(kChannelCount), 2.25);
    syncAudioSettings(*p, true);
    publishRouteTelemetry(*p);
    publishLegacyTail(*p, 0u, true);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#endif
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->delay.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->outputClip.store(false, std::memory_order_relaxed);
    syncAudioSettings(*p, true);
    publishRouteTelemetry(*p);
    publishLegacyTail(*p, 0u, true);
}

bool paramAffectsTail(clap_id paramId)
{
    switch (paramId) {
    case kDelayMsParamId:
    case kFeedbackParamId:
    case kTopologySpreadParamId:
    case kTopologySkewParamId:
    case kTopologyJitterParamId:
    case kDisplaceCollapseParamId:
    case kDisplaceDirXParamId:
    case kDisplaceDirYParamId:
    case kDisplaceDirZParamId:
    case kDisplaceTwistParamId:
    case kDisplaceFlareParamId:
    case kTopologyShapeParamId:
    case kTopologyMotionModeParamId:
    case kTopologyMotionVariantParamId:
    case kTopologyMotionRateParamId:
    case kTopologyMotionDepthParamId:
    case kTopologyNeighborCountParamId:
    case kTopologyRadiusParamId:
    case kTopologyCentroidParamId:
    case kRouteAmountParamId:
    case kRouteTurnParamId:
    case kRouteBranchParamId:
    case kRouteLossParamId:
        return true;
    default:
        return false;
    }
}

void setParam(Plugin& p, clap_id paramId, double value)
{
    DelaySettings sanitized {};
    if (!assignSettingsParam(sanitized, paramId, value)) return;
    double storedValue = 0.0;
    settingsParamValue(sanitized, paramId, storedValue);
    p.parameterValues[paramId].store(storedValue, std::memory_order_relaxed);
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (paramId == kTopologyMotionModeParamId && roundedUint(storedValue) == 0u) {
        p.publishedMotionPhase.store(0.0, std::memory_order_relaxed);
        p.motionPhaseRestorePending.store(true, std::memory_order_release);
    }
    if (paramAffectsTail(paramId)) markTailChanged(p);
    requestGuiRedraw(p);
}

bool applyParamEvent(Plugin& p, const clap_event_header_t* ev)
{
    if (!ev || ev->space_id != CLAP_CORE_EVENT_SPACE_ID || ev->type != CLAP_EVENT_PARAM_VALUE) {
        return false;
    }
    const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
    setParam(p, param->param_id, param->value);
    return true;
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) {
        return;
    }

    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        applyParamEvent(p, in->get(in, i));
    }
}

void finishExtraOutputs(const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames, bool passThrough)
{
    for (uint32_t ch = channels; ch < output.channel_count; ++ch) {
        if (passThrough && ch < input.channel_count) {
            if (output.data32 && output.data32[ch]) {
                if (input.data32 && input.data32[ch]) {
                    std::memcpy(output.data32[ch], input.data32[ch], sizeof(float) * frames);
                    continue;
                }
                if (input.data64 && input.data64[ch]) {
                    for (uint32_t i = 0; i < frames; ++i) output.data32[ch][i] = static_cast<float>(input.data64[ch][i]);
                    continue;
                }
            }
            if (output.data64 && output.data64[ch]) {
                if (input.data64 && input.data64[ch]) {
                    std::memcpy(output.data64[ch], input.data64[ch], sizeof(double) * frames);
                    continue;
                }
                if (input.data32 && input.data32[ch]) {
                    for (uint32_t i = 0; i < frames; ++i) output.data64[ch][i] = static_cast<double>(input.data32[ch][i]);
                    continue;
                }
            }
        }
        if (output.data32 && output.data32[ch]) {
            std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        }
        if (output.data64 && output.data64[ch]) {
            std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
        }
    }
}

void copyAvailableChannels(const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (input.data32 && output.data32 && input.data32[ch] && output.data32[ch]) {
            std::memcpy(output.data32[ch], input.data32[ch], sizeof(float) * frames);
        } else if (input.data64 && output.data64 && input.data64[ch] && output.data64[ch]) {
            std::memcpy(output.data64[ch], input.data64[ch], sizeof(double) * frames);
        }
    }
}

void clearOutputs(const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (output.data32 && output.data32[ch]) {
            std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        }
        if (output.data64 && output.data64[ch]) {
            std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
        }
    }
}

float applyOutputStage(Plugin& p, float value, float& blockPeak, bool& blockClip)
{
    const float trimmed = value * dbToGain(p.audioSettings.outputTrimDb);
    const float peak = std::fabs(trimmed);
    blockPeak = std::max(blockPeak, peak);
    blockClip = blockClip || peak >= 0.98f;
    return trimmed;
}

void publishOutputMeter(Plugin& p, float blockPeak, bool blockClip, uint32_t frames)
{
    const float previous = p.outputPeak.load(std::memory_order_relaxed);
    p.outputPeak.store(std::max(previous * 0.94f, blockPeak), std::memory_order_relaxed);
    if (blockClip) {
        p.outputClip.store(true, std::memory_order_relaxed);
    }
#if defined(__APPLE__)
    if (p.guiView) {
        if (p.meterRedrawCountdown <= frames) {
            p.meterRedrawCountdown = static_cast<uint32_t>(std::max(1.0, p.sampleRate / 24.0));
            requestGuiRedraw(p);
        } else {
            p.meterRedrawCountdown -= frames;
        }
    }
#else
    (void)frames;
#endif
}

void publishRouteTelemetry(Plugin& p)
{
    for (uint32_t source = 0; source < kChannelCount; ++source) {
        p.routeNodeEnergy[source].store(
            p.delay.nodeEnergy(source), std::memory_order_relaxed);
        for (uint32_t destination = 0; destination < kChannelCount; ++destination) {
            p.routeEdgeEnergy[source][destination].store(
                p.delay.edgeEnergy(source, destination),
                std::memory_order_relaxed);
            p.routeEdgePhase[source][destination].store(
                p.delay.edgePhase(source, destination),
                std::memory_order_relaxed);
        }
    }
    p.routeCentroidEnergy.store(
        p.delay.centroidEnergy(), std::memory_order_relaxed);
    p.routeCentroidPhase.store(
        p.delay.centroidPhase(), std::memory_order_relaxed);
    p.publishedRouteTailFrames.store(
        std::max(p.delay.routeTailFrames(),
            p.delay.routeTailRemainingFrames()),
        std::memory_order_relaxed);
}

void noteLegacyTailTarget(Plugin& p)
{
    p.audioLegacyTailRemainingFrames = std::max(
        p.audioLegacyTailRemainingFrames,
        legacyTailEstimateFrames(p.audioSettings, p.sampleRate));
}

void publishLegacyTail(Plugin& p, uint32_t elapsedFrames, bool clearResidual)
{
    if (clearResidual) {
        p.audioLegacyTailRemainingFrames = 0u;
    } else if (p.audioLegacyTailRemainingFrames > elapsedFrames) {
        p.audioLegacyTailRemainingFrames -= elapsedFrames;
    } else {
        p.audioLegacyTailRemainingFrames = 0u;
    }
    noteLegacyTailTarget(p);
    p.publishedLegacyTailFrames.store(
        p.audioLegacyTailRemainingFrames, std::memory_order_release);
}

void processFloatSegment(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t startFrame, uint32_t frames)
{
    syncAudioSettings(p);
    noteLegacyTailTarget(p);
    advanceTopologyMotion(p, frames);
    std::array<float, kChannelCount> inFrame {};
    std::array<float, kChannelCount> wetFrame {};
    float blockPeak = 0.0f;
    const uint32_t scopeBase = p.scopeWrite.load(std::memory_order_relaxed);
    bool blockClip = false;

    const uint32_t endFrame = startFrame + frames;
    for (uint32_t i = startFrame; i < endFrame; ++i) {
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            inFrame[ch] = input.data32[ch][i];
        }

        p.delay.processFrame(inFrame.data(), wetFrame.data());

        for (uint32_t outCh = 0; outCh < kChannelCount; ++outCh) {
            float sum = 0.0f;
            bool hasConnection = false;
            const uint64_t outBit = uint64_t { 1 } << outCh;
            for (uint32_t inCh = 0; inCh < kChannelCount; ++inCh) {
                if ((p.audioPatchRows[inCh] & outBit) == 0) {
                    continue;
                }
                hasConnection = true;
                sum += s3g::lerp(inFrame[inCh], wetFrame[inCh], static_cast<float>(p.audioSettings.mix));
            }
            if (hasConnection) {
                const float out = applyOutputStage(p, sum, blockPeak, blockClip);
                output.data32[outCh][i] = out;
                p.scope[outCh][(scopeBase + (i - startFrame)) % kScopeFrames].store(out, std::memory_order_relaxed);
            }
        }
    }
    p.scopeWrite.store((scopeBase + frames) % kScopeFrames, std::memory_order_relaxed);
    publishOutputMeter(p, blockPeak, blockClip, frames);
}

void processDoubleSegment(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t startFrame, uint32_t frames)
{
    syncAudioSettings(p);
    noteLegacyTailTarget(p);
    advanceTopologyMotion(p, frames);
    std::array<float, kChannelCount> inFrame {};
    std::array<float, kChannelCount> wetFrame {};
    float blockPeak = 0.0f;
    const uint32_t scopeBase = p.scopeWrite.load(std::memory_order_relaxed);
    bool blockClip = false;

    const uint32_t endFrame = startFrame + frames;
    for (uint32_t i = startFrame; i < endFrame; ++i) {
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            inFrame[ch] = static_cast<float>(input.data64[ch][i]);
        }

        p.delay.processFrame(inFrame.data(), wetFrame.data());

        for (uint32_t outCh = 0; outCh < kChannelCount; ++outCh) {
            float sum = 0.0f;
            bool hasConnection = false;
            const uint64_t outBit = uint64_t { 1 } << outCh;
            for (uint32_t inCh = 0; inCh < kChannelCount; ++inCh) {
                if ((p.audioPatchRows[inCh] & outBit) == 0) {
                    continue;
                }
                hasConnection = true;
                sum += s3g::lerp(inFrame[inCh], wetFrame[inCh], static_cast<float>(p.audioSettings.mix));
            }
            if (hasConnection) {
                const float out = applyOutputStage(p, sum, blockPeak, blockClip);
                output.data64[outCh][i] = static_cast<double>(out);
                p.scope[outCh][(scopeBase + (i - startFrame)) % kScopeFrames].store(out, std::memory_order_relaxed);
            }
        }
    }
    p.scopeWrite.store((scopeBase + frames) % kScopeFrames, std::memory_order_relaxed);
    publishOutputMeter(p, blockPeak, blockClip, frames);
}

template <typename ProcessSegmentFn>
void processWithSampleAccurateEvents(Plugin& p,
    const clap_audio_buffer_t& input,
    const clap_audio_buffer_t& output,
    uint32_t frames,
    const clap_input_events_t* inEvents,
    ProcessSegmentFn processSegment)
{
    uint32_t frameCursor = 0;
    const uint32_t eventCount = inEvents ? inEvents->size(inEvents) : 0;
    uint32_t eventIndex = 0;

    while (eventIndex < eventCount) {
        const clap_event_header_t* ev = inEvents->get(inEvents, eventIndex);
        if (!ev) {
            ++eventIndex;
            continue;
        }

        const uint32_t eventTime = std::min<uint32_t>(ev->time, frames);
        if (eventTime > frameCursor) {
            processSegment(frameCursor, eventTime - frameCursor);
            frameCursor = eventTime;
        }

        applyParamEvent(p, ev);
        ++eventIndex;
    }

    if (frameCursor < frames) {
        processSegment(frameCursor, frames - frameCursor);
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    const uint32_t frames = process->frames_count;
    syncAudioSettings(*p);
    deliverTailChangedOnAudioThread(*p);

    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
        readParamEvents(*p, process->in_events);
        syncAudioSettings(*p);
        publishRouteTelemetry(*p);
        publishLegacyTail(*p, frames, false);
        deliverTailChangedOnAudioThread(*p);
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, kChannelCount });

    if (channels == kChannelCount && input.data32 && output.data32) {
        if (p->audioClearUnused && !kLockUnusedChannelsToPassThrough) {
            clearOutputs(output, kChannelCount, frames);
        } else {
            copyAvailableChannels(input, output, kChannelCount, frames);
        }
        processWithSampleAccurateEvents(*p, input, output, frames, process->in_events,
            [&](uint32_t start, uint32_t count) {
                processFloatSegment(*p, input, output, start, count);
            });
    } else if (channels == kChannelCount && input.data64 && output.data64) {
        if (p->audioClearUnused && !kLockUnusedChannelsToPassThrough) {
            clearOutputs(output, kChannelCount, frames);
        } else {
            copyAvailableChannels(input, output, kChannelCount, frames);
        }
        processWithSampleAccurateEvents(*p, input, output, frames, process->in_events,
            [&](uint32_t start, uint32_t count) {
                processDoubleSegment(*p, input, output, start, count);
            });
    } else {
        readParamEvents(*p, process->in_events);
        copyAvailableChannels(input, output, channels, frames);
    }

    finishExtraOutputs(input, output, channels, frames, !p->audioClearUnused || kLockUnusedChannelsToPassThrough);
    syncAudioSettings(*p);
    publishRouteTelemetry(*p);
    publishLegacyTail(*p, frames, false);
    deliverTailChangedOnAudioThread(*p);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (!p || !p->guiView) {
        return;
    }
    if (p->guiDirty.exchange(false, std::memory_order_acquire)) {
        NSView* view = static_cast<NSView*>(p->guiView);
        [view setNeedsDisplay:YES];
    }
#else
    (void)plugin;
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) {
        return false;
    }
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "%uch %s", kChannelCount, isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = nullptr;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return 28; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) {
        return false;
    }

    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Processor Delay", sizeof(info->module));

    switch (index) {
    case 0:
        info->id = kDelayMsParamId;
        std::strncpy(info->name, "Delay Time", sizeof(info->name));
        info->min_value = kDelayMinMs;
        info->max_value = kDelayMaxMs;
        info->default_value = 280.0;
        return true;
    case 1:
        info->id = kFeedbackParamId;
        std::strncpy(info->name, "Feedback", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 0.82;
        info->default_value = 0.35;
        return true;
    case 2:
        info->id = kMixParamId;
        std::strncpy(info->name, "Mix", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.45;
        return true;
    case 3:
        info->id = kToneParamId;
        std::strncpy(info->name, "Tone", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.60;
        return true;
    case 4:
        info->id = kPitchParamId;
        std::strncpy(info->name, "Pitch", sizeof(info->name));
        info->min_value = kPitchMinSemitones;
        info->max_value = kPitchMaxSemitones;
        info->default_value = 0.0;
        return true;
    case 5:
        info->id = kCharacterParamId;
        std::strncpy(info->name, "Character", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 6:
        info->id = kTapParamId;
        std::strncpy(info->name, "Smear", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 7:
        info->id = kOutputTrimParamId;
        std::strncpy(info->name, "Output Trim", sizeof(info->name));
        info->min_value = kOutputTrimMinDb;
        info->max_value = kOutputTrimMaxDb;
        info->default_value = -6.0;
        return true;
    case 8:
        info->id = kTopologyShapeParamId;
        std::strncpy(info->name, "Topology Shape", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        info->min_value = 0.0;
        info->max_value = static_cast<double>(kTopologyShapeCount - 1u);
        info->default_value = 0.0;
        return true;
    case 9:
        info->id = kTopologySpreadParamId;
        std::strncpy(info->name, "Topology Amount", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 10:
        info->id = kDisplaceCollapseParamId;
        std::strncpy(info->name, "Topology Pull", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 11:
        info->id = kDisplaceDirXParamId;
        std::strncpy(info->name, "Topology X", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 12:
        info->id = kDisplaceDirYParamId;
        std::strncpy(info->name, "Topology Y", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 13:
        info->id = kDisplaceDirZParamId;
        std::strncpy(info->name, "Topology Z", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 1.0;
        return true;
    case 14:
        info->id = kDisplaceTwistParamId;
        std::strncpy(info->name, "Topology Twist", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 15:
        info->id = kDisplaceFlareParamId;
        std::strncpy(info->name, "Topology Flare", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 16:
        info->id = kTopologyJitterParamId;
        std::strncpy(info->name, "Topology Seed", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 17:
        info->id = kTopologyMotionModeParamId;
        std::strncpy(info->name, "Topology Animation", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        info->min_value = 0.0;
        info->max_value = static_cast<double>(kTopologyMotionModeCount - 1u);
        info->default_value = 0.0;
        return true;
    case 18:
        info->id = kTopologyMotionVariantParamId;
        std::strncpy(info->name, "Topology Variant", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        info->min_value = 0.0;
        info->max_value = static_cast<double>(kTopologyVariantCount - 1u);
        info->default_value = 0.0;
        return true;
    case 19:
        info->id = kTopologyMotionRateParamId;
        std::strncpy(info->name, "Topology Rate", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = kTopologyMotionMinHz;
        info->max_value = kTopologyMotionMaxHz;
        info->default_value = 0.10;
        return true;
    case 20:
        info->id = kTopologyMotionDepthParamId;
        std::strncpy(info->name, "Topology Depth", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 21:
        info->id = kTopologyNeighborCountParamId;
        std::strncpy(info->name, "Topology Neighbors", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        info->min_value = 1.0;
        info->max_value = 3.0;
        info->default_value = 2.0;
        return true;
    case 22:
        info->id = kTopologyRadiusParamId;
        std::strncpy(info->name, "Topology Radius", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.65;
        return true;
    case 23:
        info->id = kTopologyCentroidParamId;
        std::strncpy(info->name, "Topology Centroid", sizeof(info->name));
        std::strncpy(info->module, "Topology", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.22;
        return true;
    case 24:
        info->id = kRouteAmountParamId;
        std::strncpy(info->name, "Route", sizeof(info->name));
        std::strncpy(info->module, "Echo Routes", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 25:
        info->id = kRouteTurnParamId;
        std::strncpy(info->name, "Turn", sizeof(info->name));
        std::strncpy(info->module, "Echo Routes", sizeof(info->module));
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    case 26:
        info->id = kRouteBranchParamId;
        std::strncpy(info->name, "Branch", sizeof(info->name));
        std::strncpy(info->module, "Echo Routes", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.35;
        return true;
    case 27:
        info->id = kRouteLossParamId;
        std::strncpy(info->name, "Loss", sizeof(info->name));
        std::strncpy(info->module, "Echo Routes", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.25;
        return true;
    default:
        return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) {
        return false;
    }

    const auto* p = self(plugin);
    if (paramId >= kParameterBankSize
        || std::find(kStoredParamIds.begin(), kStoredParamIds.end(), paramId)
            == kStoredParamIds.end()) {
        return false;
    }
    *value = p->parameterValues[paramId].load(std::memory_order_relaxed);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) {
        return false;
    }

    switch (paramId) {
    case kDelayMsParamId:
        std::snprintf(display, size, "%.1f ms", value);
        return true;
    case kPitchParamId:
        std::snprintf(display, size, "%+.2f st", value);
        return true;
    case kOutputTrimParamId:
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    case kTopologyShapeParamId:
        std::snprintf(display, size, "%s", topologyShapeName(roundedUint(value)));
        return true;
    case kTopologyMotionModeParamId:
        std::snprintf(display, size, "%s", topologyMotionModeName(roundedUint(value)));
        return true;
    case kTopologyMotionVariantParamId:
        std::snprintf(display, size, "%s", topologyVariantName(roundedUint(value)));
        return true;
    case kTopologyMotionRateParamId:
        std::snprintf(display, size, "%.3f Hz", value);
        return true;
    case kTopologyNeighborCountParamId:
        std::snprintf(display, size, "%u", std::clamp<uint32_t>(roundedUint(value), 1u, 3u));
        return true;
    case kFeedbackParamId:
    case kMixParamId:
    case kToneParamId:
    case kCharacterParamId:
    case kTapParamId:
    case kTopologySpreadParamId:
    case kTopologyJitterParamId:
    case kDisplaceCollapseParamId:
    case kTopologyMotionDepthParamId:
    case kTopologyRadiusParamId:
    case kTopologyCentroidParamId:
    case kRouteAmountParamId:
    case kRouteBranchParamId:
    case kRouteLossParamId:
        std::snprintf(display, size, "%.1f %%", value * 100.0);
        return true;
    case kTopologySkewParamId:
    case kDisplaceDirXParamId:
    case kDisplaceDirYParamId:
    case kDisplaceDirZParamId:
    case kDisplaceTwistParamId:
    case kDisplaceFlareParamId:
    case kRouteTurnParamId:
        std::snprintf(display, size, "%+.1f %%", value * 100.0);
        return true;
    default:
        return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) {
        return false;
    }

    if (paramId == kTopologyShapeParamId) {
        for (uint32_t i = 0; i < kTopologyShapeCount; ++i) {
            if (std::strcmp(display, topologyShapeName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
        *value = static_cast<double>(std::min<uint32_t>(kTopologyShapeCount - 1u, roundedUint(std::atof(display))));
        return true;
    }

    if (paramId == kTopologyMotionModeParamId) {
        for (uint32_t i = 0; i < kTopologyMotionModeCount; ++i) {
            if (std::strcmp(display, topologyMotionModeName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
        *value = static_cast<double>(std::min<uint32_t>(kTopologyMotionModeCount - 1u, roundedUint(std::atof(display))));
        return true;
    }

    if (paramId == kTopologyMotionVariantParamId) {
        for (uint32_t i = 0; i < kTopologyVariantCount; ++i) {
            if (std::strcmp(display, topologyVariantName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
        *value = static_cast<double>(std::min<uint32_t>(kTopologyVariantCount - 1u, roundedUint(std::atof(display))));
        return true;
    }

    *value = std::atof(display);
    switch (paramId) {
    case kDelayMsParamId:
        return true;
    case kPitchParamId:
    case kOutputTrimParamId:
    case kTopologyMotionRateParamId:
        return true;
    case kTopologyNeighborCountParamId:
        *value = static_cast<double>(std::clamp<uint32_t>(roundedUint(*value), 1u, 3u));
        return true;
    case kFeedbackParamId:
    case kMixParamId:
    case kToneParamId:
    case kCharacterParamId:
    case kTapParamId:
    case kTopologySpreadParamId:
    case kTopologyJitterParamId:
    case kDisplaceCollapseParamId:
    case kTopologyMotionDepthParamId:
    case kTopologyRadiusParamId:
    case kTopologyCentroidParamId:
    case kRouteAmountParamId:
    case kRouteBranchParamId:
    case kRouteLossParamId:
        if (std::strchr(display, '%') || *value > 1.0) {
            *value *= 0.01;
        }
        return true;
    case kTopologySkewParamId:
    case kDisplaceDirXParamId:
    case kDisplaceDirYParamId:
    case kDisplaceDirZParamId:
    case kDisplaceTwistParamId:
    case kDisplaceFlareParamId:
    case kRouteTurnParamId:
        if (std::strchr(display, '%') || *value < -1.0 || *value > 1.0) {
            *value *= 0.01;
        }
        return true;
    default:
        return false;
    }
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t params {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

uint32_t latencyGet(const clap_plugin_t*)
{
    return 0;
}

const clap_plugin_latency_t latency {
    latencyGet
};

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p) {
        return 0;
    }

    DelaySettings settings {};
    loadSettingsFromParameterBank(*p, settings);
    const uint64_t legacyFrames = std::max<uint32_t>(
        legacyTailEstimateFrames(settings, p->sampleRate),
        p->publishedLegacyTailFrames.load(std::memory_order_acquire));

    uint64_t routeFrames = p->publishedRouteTailFrames.load(
        std::memory_order_relaxed);
    if (settings.routeAmount > 0.000001 && routeFrames == 0u) {
        // The audio thread publishes the core's exact estimate. Until it has
        // observed a newly automated route value, advertise a finite ceiling.
        routeFrames = static_cast<uint64_t>(std::ceil(
            120.0 * std::max(1.0, p->sampleRate)));
    }
    return static_cast<uint32_t>(std::min<uint64_t>(
        std::max(legacyFrames, routeFrames),
        static_cast<uint64_t>(std::numeric_limits<int32_t>::max())));
}

const clap_plugin_tail_t tail {
    tailGet
};

bool writeAll(const clap_ostream_t* stream, const void* data, uint64_t size)
{
    auto* cursor = static_cast<const uint8_t*>(data);
    uint64_t remaining = size;

    while (remaining > 0) {
        const int64_t written = stream->write(stream, cursor, remaining);
        if (written <= 0) {
            return false;
        }
        cursor += written;
        remaining -= static_cast<uint64_t>(written);
    }
    return true;
}

bool readAll(const clap_istream_t* stream, void* data, uint64_t size)
{
    auto* cursor = static_cast<uint8_t*>(data);
    uint64_t remaining = size;

    while (remaining > 0) {
        const int64_t count = stream->read(stream, cursor, remaining);
        if (count <= 0) {
            return false;
        }
        cursor += count;
        remaining -= static_cast<uint64_t>(count);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) {
        return false;
    }

    auto* p = self(plugin);
    syncGuiSettings(*p);
    SavedState state {};
    state.version = kStateVersion;
    for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
        state.patchRows[row] = p->patch.rowMask(row);
    }
    state.clearUnused = (p->clearUnused && !kLockUnusedChannelsToPassThrough) ? 1u : 0u;
    state.delayMs = p->delayMs;
    state.feedback = p->feedback;
    state.mix = p->mix;
    state.tone = p->tone;
    state.character = p->character;
    state.tapAmount = p->tapAmount;
    state.outputTrimDb = p->outputTrimDb;
    state.pitchSemitones = p->pitchSemitones;
    state.topologySpread = p->topologySpread;
    state.topologySkew = p->topologySkew;
    state.topologyJitter = p->topologyJitter;
    state.displaceCollapse = p->displaceCollapse;
    state.displaceDirX = p->displaceDirX;
    state.displaceDirY = p->displaceDirY;
    state.displaceDirZ = p->displaceDirZ;
    state.displaceTwist = p->displaceTwist;
    state.displaceFlare = p->displaceFlare;
    state.topologyShape = p->topologyShape;
    state.topologyMotionMode = p->topologyMotionMode;
    state.topologyMotionVariant = p->topologyMotionVariant;
    state.topologyMotionRateHz = p->topologyMotionRateHz;
    state.topologyMotionDepth = p->topologyMotionDepth;
    state.topologyNeighborCount = p->topologyNeighborCount;
    state.topologyRadius = p->topologyRadius;
    state.topologyCentroid = p->topologyCentroid;
    state.routeAmount = p->routeAmount;
    state.routeTurn = p->routeTurn;
    state.routeBranch = p->routeBranch;
    state.routeLoss = p->routeLoss;
    state.topologyMotionPhase = p->publishedMotionPhase.load(
        std::memory_order_relaxed);
    return writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) {
        return false;
    }

    uint32_t version = 0;
    if (!readAll(stream, &version, sizeof(version))) {
        return false;
    }

    SavedState state {};
    state.version = version;
    if (version == kStateVersion) {
        auto* cursor = reinterpret_cast<uint8_t*>(&state) + sizeof(state.version);
        if (!readAll(stream, cursor, sizeof(state) - sizeof(state.version))) {
            return false;
        }
    } else if (version == kV10StateVersion) {
        SavedStateV10 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        static_assert(sizeof(oldState) <= offsetof(SavedState, routeAmount));
        std::memcpy(&state, &oldState, sizeof(oldState));
        state.version = kStateVersion;
    } else if (version == kV9StateVersion) {
        SavedStateV9 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.character = oldState.character;
        state.tapAmount = oldState.tapAmount;
        state.outputTrimDb = oldState.outputTrimDb;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
        state.pitchSemitones = oldState.pitchSemitones;
        state.topologyShape = oldState.topologyShape;
        state.topologyMotionMode = oldState.topologyMotionMode;
        state.topologyMotionRateHz = oldState.topologyMotionRateHz;
        state.topologyMotionDepth = oldState.topologyMotionDepth;
        state.topologyNeighborCount = oldState.topologyNeighborCount;
        state.topologyRadius = oldState.topologyRadius;
        state.topologyCentroid = oldState.topologyCentroid;
    } else if (version == kV8StateVersion) {
        SavedStateV8 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.character = oldState.character;
        state.tapAmount = oldState.tapAmount;
        state.outputTrimDb = oldState.outputTrimDb;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
        state.pitchSemitones = oldState.pitchSemitones;
        state.topologyShape = oldState.topologyShape;
        state.topologyMotionMode = oldState.topologyMotionMode;
        state.topologyMotionRateHz = oldState.topologyMotionRateHz;
        state.topologyMotionDepth = oldState.topologyMotionDepth;
    } else if (version == kV7StateVersion) {
        SavedStateV7 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.character = oldState.character;
        state.tapAmount = oldState.tapAmount;
        state.outputTrimDb = oldState.outputTrimDb;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
        state.pitchSemitones = oldState.pitchSemitones;
        state.topologyShape = oldState.topologyShape;
    } else if (version == kV6StateVersion) {
        SavedStateV6 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.character = oldState.character;
        state.outputTrimDb = oldState.outputTrimDb;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
        state.pitchSemitones = oldState.pitchSemitones;
        state.topologyShape = oldState.topologyShape;
    } else if (version == kV5StateVersion) {
        SavedStateV5 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.character = oldState.character;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
        state.pitchSemitones = oldState.pitchSemitones;
        state.topologyShape = oldState.topologyShape;
    } else if (version == kV4StateVersion) {
        SavedStateV4 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
        state.pitchSemitones = oldState.pitchSemitones;
        state.topologyShape = oldState.topologyShape;
    } else if (version == kPreviousStateVersion) {
        SavedStateV3 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
        state.pitchSemitones = oldState.pitchSemitones;
    } else if (version == kV2StateVersion) {
        SavedStateV2 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
            state.patchRows[row] = oldState.patchRows[row];
        }
        state.clearUnused = oldState.clearUnused;
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
        state.displaceCollapse = oldState.displaceCollapse;
        state.displaceDirX = oldState.displaceDirX;
        state.displaceDirY = oldState.displaceDirY;
        state.displaceDirZ = oldState.displaceDirZ;
        state.displaceTwist = oldState.displaceTwist;
        state.displaceFlare = oldState.displaceFlare;
    } else if (version == kLegacyStateVersion) {
        SavedStateV1 oldState {};
        oldState.version = version;
        auto* cursor = reinterpret_cast<uint8_t*>(&oldState) + sizeof(oldState.version);
        if (!readAll(stream, cursor, sizeof(oldState) - sizeof(oldState.version))) {
            return false;
        }
        state.delayMs = oldState.delayMs;
        state.feedback = oldState.feedback;
        state.mix = oldState.mix;
        state.tone = oldState.tone;
        state.topologySpread = oldState.topologySpread;
        state.topologySkew = oldState.topologySkew;
        state.topologyJitter = oldState.topologyJitter;
    } else {
        return false;
    }

    auto* p = self(plugin);
    p->patch.setWidth(kChannelCount);
    for (uint32_t row = 0; row < s3g::kLanePatchMaxChannels; ++row) {
        p->patch.setRowMask(row, state.patchRows[row]);
    }
    p->clearUnused = (state.clearUnused != 0) && !kLockUnusedChannelsToPassThrough;
    p->delayMs = clampDelayMs(state.delayMs);
    p->feedback = clampFeedback(state.feedback);
    p->mix = clamp01(state.mix);
    p->tone = clamp01(state.tone);
    p->character = clamp01(state.character);
    p->tapAmount = clamp01(state.tapAmount);
    p->outputTrimDb = clampOutputTrimDb(state.outputTrimDb);
    p->pitchSemitones = std::clamp(state.pitchSemitones, kPitchMinSemitones, kPitchMaxSemitones);
    p->topologySpread = clamp01(state.topologySpread);
    p->topologySkew = clampBipolar(state.topologySkew);
    p->topologyJitter = clamp01(state.topologyJitter);
    p->displaceCollapse = clamp01(state.displaceCollapse);
    p->displaceDirX = clampBipolar(state.displaceDirX);
    p->displaceDirY = clampBipolar(state.displaceDirY);
    p->displaceDirZ = clampBipolar(state.displaceDirZ);
    p->displaceTwist = clampBipolar(state.displaceTwist);
    p->displaceFlare = clampBipolar(state.displaceFlare);
    p->topologyShape = std::min<uint32_t>(kTopologyShapeCount - 1u, state.topologyShape);
    p->topologyMotionMode = std::min<uint32_t>(kTopologyMotionModeCount - 1u, state.topologyMotionMode);
    p->topologyMotionVariant = std::min<uint32_t>(kTopologyVariantCount - 1u, state.topologyMotionVariant);
    p->topologyMotionRateHz = clampMotionRateHz(state.topologyMotionRateHz);
    p->topologyMotionDepth = clamp01(state.topologyMotionDepth);
    p->topologyNeighborCount = std::clamp<uint32_t>(state.topologyNeighborCount, 1u, 3u);
    p->topologyRadius = clamp01(state.topologyRadius);
    p->topologyCentroid = clamp01(state.topologyCentroid);
    p->routeAmount = clamp01(state.routeAmount);
    p->routeTurn = clampBipolar(state.routeTurn);
    p->routeBranch = clamp01(state.routeBranch);
    p->routeLoss = clamp01(state.routeLoss);
    p->topologyMotionPhase = std::isfinite(state.topologyMotionPhase)
        ? state.topologyMotionPhase - std::floor(state.topologyMotionPhase)
        : 0.0;
    if (p->topologyMotionMode == 0u) {
        p->topologyMotionPhase = 0.0;
    }
    preparePatch(*p);
    p->publishedMotionPhase.store(
        p->topologyMotionPhase, std::memory_order_relaxed);
    p->motionPhaseRestorePending.store(true, std::memory_order_release);
    storeSettingsInParameterBank(*p, static_cast<const DelaySettings&>(*p));
    markTailChanged(*p);
    requestGuiRedraw(*p);
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad
};

#if defined(__APPLE__)

} // namespace

@interface S3GDelayProcessorView : NSView {
    void* _plugin;
    int _dragSlider;
    bool _dragTopologyView;
    NSPoint _lastDragPoint;
    double _viewYaw;
    double _viewPitch;
    int _cameraView;
    bool _showReadout;
    int _fieldPage;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItemCount;
    NSTimer* _refreshTimer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)updateSliderAtPoint:(NSPoint)pt;
- (NSRect)fieldPageButtonRect:(NSRect)rect index:(int)index;
- (void)drawScope:(NSRect)rect small:(NSDictionary*)small;
- (void)resetTopology;
- (void)setTopologyView:(uint32_t)view;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateMenuHover:(NSPoint)point;
@end

static NSColor* s3gTapeColor(int rgb) { return s3g::clap_gui::color(rgb); }
static NSColor* s3gHeatColor(double value, double alpha) { return s3g::clap_gui::heatColor(value, alpha); }
static constexpr CGFloat kDelayGuiWidth =
    s3g::gui_layout::kTopologyProcessorColumns.canvasWidth;
static constexpr CGFloat kDelayGuiHeight = 696.0;
static constexpr CGFloat kDelayGuiPanelX =
    s3g::gui_layout::kTopologyProcessorColumns.first.x;
static constexpr CGFloat kDelayGuiTopologyPanelX =
    s3g::gui_layout::kTopologyProcessorColumns.second.x;
static constexpr CGFloat kDelayGuiPanelWidth =
    s3g::gui_layout::kTopologyProcessorColumns.first.width;
static constexpr CGFloat kDelayLegacyContentTop = 34.0;
static constexpr CGFloat kDelayContentTranslation =
    s3g::gui_layout::kStandardMetrics.contentTop
        - kDelayLegacyContentTop;
static constexpr CGFloat kDelayContentCoordinateHeight =
    kDelayGuiHeight - kDelayContentTranslation;
static constexpr CGFloat kDelayGuiRowPitch =
    s3g::gui_layout::kStandardMetrics.rowPitch;
static constexpr CGFloat kDelayGuiTopologyRowPitch =
    s3g::gui_layout::kStandardMetrics.rowPitch;

static CGFloat delayEngineRowY(CGFloat panelY, uint32_t index)
{
    return panelY + 36.0 + static_cast<CGFloat>(index) * kDelayGuiRowPitch;
}

static CGFloat delayOutputRowY(CGFloat panelY, uint32_t index)
{
    return panelY + 36.0 + static_cast<CGFloat>(index) * 26.0;
}

@implementation S3GDelayProcessorView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(
        0, 0, kDelayGuiWidth, kDelayGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _dragTopologyView = false;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewYaw = -0.52;
        _viewPitch = 0.34;
        _cameraView = 2;
        _showReadout = false;
        _fieldPage = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSMakePoint(0, 0);
        _menuItemCount = 0;
        _refreshTimer = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "CURRENT");
    }
    return self;
}

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)stopRefreshTimer
{
    if (_refreshTimer) {
        [_refreshTimer invalidate];
        _refreshTimer = nil;
    }
}

- (void)startRefreshTimer
{
    if (_refreshTimer) {
        return;
    }
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 30.0)
                                            target:self
                                          selector:@selector(refreshTimerFired:)
                                          userInfo:nil
                                           repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}

- (void)refreshTimerFired:(NSTimer*)timer
{
    (void)timer;
    if ([self isHidden] || !_plugin || !s3g::clap_support::hostAppIsActive()) {
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    DelaySettings settings {};
    loadSettingsFromParameterBank(*p, settings);
    bool routeVisualLive =
        p->routeCentroidEnergy.load(std::memory_order_relaxed) > 0.0001f;
    for (uint32_t source = 0u; !routeVisualLive && source < kChannelCount;
         ++source) {
        routeVisualLive =
            p->routeNodeEnergy[source].load(std::memory_order_relaxed)
                > 0.0001f;
        for (uint32_t destination = 0u;
             !routeVisualLive && destination < kChannelCount; ++destination) {
            routeVisualLive =
                p->routeEdgeEnergy[source][destination].load(
                    std::memory_order_relaxed) > 0.0001f;
        }
    }
    if (topologyMotionActive(settings) || routeVisualLive) {
        [self setNeedsDisplay:YES];
    }
}

- (BOOL)isFlipped
{
    return YES;
}

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

- (NSRect)fieldPageButtonRect:(NSRect)rect index:(int)index
{
    return s3g::clap_gui::topologyProcessorFieldPageButtonRect(
        rect, static_cast<uint32_t>(index));
}

- (void)drawScope:(NSRect)rect small:(NSDictionary*)small
{
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
    constexpr uint32_t kDrawFrames = 512u;
    const uint32_t oneSecondFrames = static_cast<uint32_t>(std::clamp(p->sampleRate, 8000.0, static_cast<double>(kScopeFrames - 1u)));
    const uint32_t historyFrames = std::min<uint32_t>(kScopeFrames - 1u, std::max<uint32_t>(kDrawFrames, oneSecondFrames));

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
        for (uint32_t i = 0; i < kDrawFrames; ++i) {
            const uint32_t age = static_cast<uint32_t>((static_cast<double>(kDrawFrames - 1u - i) / static_cast<double>(kDrawFrames - 1u))
                                                       * static_cast<double>(historyFrames));
            const uint32_t index = (write + kScopeFrames - 1u - age) % kScopeFrames;
            peak = std::max(peak, std::fabs(p->scope[lane][index].load(std::memory_order_relaxed)));
        }
        const float scale = std::min(4.0f, 0.92f / peak);
        NSBezierPath* path = [NSBezierPath bezierPath];
        for (uint32_t i = 0; i < kDrawFrames; ++i) {
            const uint32_t age = static_cast<uint32_t>((static_cast<double>(kDrawFrames - 1u - i) / static_cast<double>(kDrawFrames - 1u))
                                                       * static_cast<double>(historyFrames));
            const uint32_t index = (write + kScopeFrames - 1u - age) % kScopeFrames;
            const float sample = p->scope[lane][index].load(std::memory_order_relaxed);
            const CGFloat x = laneRect.origin.x + (static_cast<CGFloat>(i) / static_cast<CGFloat>(kDrawFrames - 1u)) * laneRect.size.width;
            const CGFloat y = NSMidY(laneRect) - static_cast<CGFloat>(std::clamp(sample * scale, -1.0f, 1.0f)) * laneRect.size.height * 0.42;
            if (i == 0u) [path moveToPoint:NSMakePoint(x, y)];
            else [path lineToPoint:NSMakePoint(x, y)];
        }
        [style.text setStroke];
        [path stroke];
        [[NSString stringWithFormat:@"L%u", lane + 1u] drawAtPoint:NSMakePoint(laneRect.origin.x + 5.0, laneRect.origin.y + 3.0) withAttributes:small];
    }
}

- (void)drawSlider:(NSString*)name
             value:(NSString*)value
              norm:(CGFloat)norm
                 y:(CGFloat)y
        labelAttrs:(NSDictionary*)labelAttrs
        valueAttrs:(NSDictionary*)valueAttrs
             strip:(NSColor*)strip
              grid:(NSColor*)grid
              fill:(NSColor*)fill
              text:(NSColor*)text
{
    (void)strip;
    (void)grid;
    (void)fill;
    (void)text;
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y, kDelayGuiPanelX, kDelayGuiPanelWidth,
        labelAttrs, valueAttrs, style);
}

- (void)drawMenuControl:(NSString*)name
                  value:(NSString*)value
                      y:(CGFloat)y
             labelAttrs:(NSDictionary*)labelAttrs
             valueAttrs:(NSDictionary*)valueAttrs
                  strip:(NSColor*)strip
                   grid:(NSColor*)grid
                   fill:(NSColor*)fill
                   text:(NSColor*)text
{
    (void)strip;
    (void)grid;
    (void)fill;
    (void)text;
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorMenu(
        name, value, y, kDelayGuiPanelX, kDelayGuiPanelWidth,
        labelAttrs, valueAttrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    syncGuiSettings(*p);
    s3g::clap_gui::Style style;
    NSColor* bg = style.bg;
    NSColor* strip = style.strip;
    NSColor* cellBg = style.cellBg;
    NSColor* grid = style.grid;
    NSColor* text = style.text;
    NSColor* accent = style.accent;
    NSColor* fillColor = style.fill;

    [bg setFill];
    NSRectFill([self bounds]);

    NSDictionary* smallAttrs = s3g::clap_gui::textAttrs(
        style.dim, 10.0);
    NSDictionary* tinyAttrs = s3g::clap_gui::textAttrs(
        style.dim, 7.0);
    NSDictionary* sectionAttrs =
        s3g::clap_gui::softLabelAttrs();

    const float peak = p->outputPeak.load(std::memory_order_relaxed);
    const bool clipped = p->outputClip.exchange(false, std::memory_order_relaxed);
    NSString* meterText = clipped ? [NSString stringWithFormat:@"%@ CLIP", s3g::clap_gui::peakDbText(peak)]
                                  : s3g::clap_gui::peakDbText(peak);
    NSString* titleText = [NSString stringWithFormat:@"s3g PROCESSOR DELAY %uCH", kChannelCount];
    s3g::clap_gui::drawProcessorTitleBand(
        titleText,
        [NSString stringWithUTF8String:_titlePresetName],
        meterText,
        s3g::clap_gui::encoderTitleBand(
            kDelayGuiWidth, kDelayGuiHeight),
        s3g::clap_gui::softTitleAttrs(),
        s3g::clap_gui::softLabelAttrs(),
        s3g::clap_gui::softValueAttrs(),
        style);

    [NSGraphicsContext saveGraphicsState];
    NSAffineTransform* contentTransform = [NSAffineTransform transform];
    [contentTransform translateXBy:0.0 yBy:kDelayContentTranslation];
    [contentTransform concat];

    const auto& fieldLayout =
        s3g::gui_layout::kTopologyProcessorColumns.field;
    NSRect topologyPanel = NSMakeRect(
        fieldLayout.x,
        fieldLayout.y - kDelayContentTranslation,
        fieldLayout.width,
        fieldLayout.height);
    [cellBg setFill];
    NSRectFill(topologyPanel);
    [grid setStroke];
    NSFrameRect(topologyPanel);
    [strip setFill];
    NSRectFill(NSMakeRect(12, 34, 620, 21));
    [accent setFill];
    NSRectFill(NSMakeRect(12, 34, 620, 2));
    [@"TOPOLOGY" drawAtPoint:NSMakePoint(
        topologyPanel.origin.x + 12.0,
        topologyPanel.origin.y + 5.0)
        withAttributes:sectionAttrs];
    s3g::clap_gui::Style pageStyle;
    NSString* pageLabels[2] = { @"TOPO", @"SCOPE" };
    for (int i = 0; i < 2; ++i) {
        NSRect button = [self fieldPageButtonRect:topologyPanel index:i];
        s3g::clap_gui::drawHeaderButton(button, topologyPanel, pageLabels[i], _fieldPage == i, smallAttrs, pageStyle);
    }
    if (_fieldPage == 0) {
        s3g::clap_gui::drawTopologyProcessorCameraButtons(
            topologyPanel, _cameraView, smallAttrs, pageStyle);
    }

    const NSRect fieldRect =
        s3g::clap_gui::topologyProcessorFieldContentRect(
            topologyPanel);
    const CGFloat fieldX = fieldRect.origin.x;
    const CGFloat fieldY = fieldRect.origin.y;
    const CGFloat fieldW = fieldRect.size.width;
    const CGFloat fieldH = fieldRect.size.height;
    if (_fieldPage == 1) {
        [self drawScope:fieldRect small:smallAttrs];
    } else {
    [strip setFill];
    NSRectFill(NSMakeRect(fieldX, fieldY, fieldW, fieldH));
    [grid setStroke];
    NSFrameRect(NSMakeRect(fieldX, fieldY, fieldW, fieldH));
    NSRect topoRect = NSMakeRect(fieldX + 30.0, fieldY + 44.0, fieldW - 60.0, 330.0);
    NSRect heatRect = NSMakeRect(fieldX + 30.0, fieldY + 392.0, fieldW - 60.0, 180.0);
    [s3gTapeColor(0x101010) setFill];
    NSRectFill(topoRect);
    [grid setStroke];
    NSFrameRect(topoRect);

    auto projectTopology = [&](double x, double y, double z) -> NSPoint {
        const double cy = std::cos(_viewYaw);
        const double sy = std::sin(_viewYaw);
        const double cp = std::cos(_viewPitch);
        const double sp = std::sin(_viewPitch);
        const double xr = x * cy - z * sy;
        const double zr = x * sy + z * cy;
        const double yr = y * cp - zr * sp;
        const double zz = y * sp + zr * cp;
        const double scale = 0.78 + zz * 0.08;
        return NSMakePoint(topoRect.origin.x + topoRect.size.width * 0.5 + static_cast<CGFloat>(xr * topoRect.size.width * 0.25 * scale),
                           topoRect.origin.y + topoRect.size.height * 0.52 - static_cast<CGFloat>(yr * topoRect.size.height * 0.38 * scale));
    };

    std::array<uint32_t, kChannelCount> activePins {};
    uint32_t activePinCount = 0u;
    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        if (p->patch.rowMask(lane) != 0u) {
            activePins[activePinCount++] = lane;
        }
    }
    if (activePinCount == 0u) {
        for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
            activePins[activePinCount++] = lane;
        }
    }
    const uint32_t visualLanes = activePinCount;
    constexpr uint32_t kHeatCols = 54;
    constexpr uint32_t kHeatRows = 18;
    [s3gTapeColor(0x090b0d) setFill];
    NSRectFill(heatRect);
    std::array<double, kHeatCols * kHeatRows> heat {};
    const double heatMax = s3g::fillTopologyHeatmap(topologyStateForPlugin(*p), visualLanes, kHeatCols, kHeatRows, heat.data());
    const CGFloat cellW = heatRect.size.width / static_cast<CGFloat>(kHeatCols);
    const CGFloat cellH = heatRect.size.height / static_cast<CGFloat>(kHeatRows);
    for (uint32_t row = 0; row < kHeatRows; ++row) {
        for (uint32_t col = 0; col < kHeatCols; ++col) {
            const size_t index = static_cast<size_t>(row) * kHeatCols + col;
            const double norm = std::pow(std::clamp(heat[index] / heatMax, 0.0, 1.0), 0.72);
            [s3gHeatColor(norm, 1.0) setFill];
            NSRectFill(NSMakeRect(heatRect.origin.x + static_cast<CGFloat>(col) * cellW,
                                  heatRect.origin.y + static_cast<CGFloat>(row) * cellH,
                                  cellW,
                                  cellH));
        }
    }

    std::array<NSPoint, kChannelCount> nodePoints {};
    std::array<double, kChannelCount> nodeDiffusion {};
    double centroidX = 0.0;
    double centroidY = 0.0;
    double centroidZ = 0.0;
    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const auto topo = topologyPointForLane(*p, lane, visualLanes);
        const double delayNorm = std::clamp(resolvedChannelDelayMs(*p, lane, visualLanes) / kDelayMaxMs, 0.0, 1.0);
        const double feedbackNorm = std::clamp(resolvedChannelFeedback(*p, lane, visualLanes) / 0.82, 0.0, 1.0);
        nodeDiffusion[lane] = resolvedChannelNetwork(*p, lane, visualLanes) / 0.68;
        const double radius = 0.56 + delayNorm * 0.68 + p->topologySpread * 0.22;
        const double x = topo.x * radius;
        const double y = topo.y * radius + (feedbackNorm - 0.5) * 0.58;
        const double z = topo.z * radius + topo.lane * (0.10 + p->topologyJitter * 0.44);
        centroidX += x;
        centroidY += y;
        centroidZ += z;
        nodePoints[lane] = projectTopology(x, y, z);
    }
    if (visualLanes > 0) {
        centroidX /= static_cast<double>(visualLanes);
        centroidY /= static_cast<double>(visualLanes);
        centroidZ /= static_cast<double>(visualLanes);
    }

    auto strokeEdge = [&](uint32_t a, uint32_t b) {
        if (a < visualLanes && b < visualLanes) {
            const double diffusion = std::clamp((nodeDiffusion[a] + nodeDiffusion[b]) * 0.5, 0.0, 1.0);
            const int gray = static_cast<int>(0x6f + diffusion * 0x70);
            [s3gTapeColor((gray << 16) | (gray << 8) | gray) setStroke];
            [NSBezierPath strokeLineFromPoint:nodePoints[a] toPoint:nodePoints[b]];
        }
    };
    bool edgeDrawn[kChannelCount][kChannelCount] {};
    const uint32_t drawNeighborCount = std::clamp<uint32_t>(p->topologyNeighborCount, 1u, 3u);
    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const auto neighbors = nearestTopologyNeighbors(*p, lane, visualLanes);
        for (uint32_t slot = 0; slot < drawNeighborCount; ++slot) {
            const int neighbor = neighbors[slot];
            if (neighbor < 0 || static_cast<uint32_t>(neighbor) >= visualLanes || neighbor == static_cast<int>(lane)) {
                continue;
            }
            const uint32_t a = std::min<uint32_t>(lane, static_cast<uint32_t>(neighbor));
            const uint32_t b = std::max<uint32_t>(lane, static_cast<uint32_t>(neighbor));
            if (!edgeDrawn[a][b]) {
                edgeDrawn[a][b] = true;
                strokeEdge(a, b);
            }
        }
    }

    // Directional telemetry overlays the static topology. Energy controls
    // line weight/brightness; phase advances the marker source-to-destination.
    for (uint32_t source = 0; source < visualLanes; ++source) {
        const uint32_t physicalSource = activePins[source];
        for (uint32_t destination = 0; destination < visualLanes; ++destination) {
            if (source == destination) continue;
            const uint32_t physicalDestination = activePins[destination];
            const double energy = std::clamp<double>(
                p->routeEdgeEnergy[physicalSource][physicalDestination].load(
                    std::memory_order_relaxed),
                0.0, 1.0);
            if (energy <= 0.0001) continue;

            NSBezierPath* liveEdge = [NSBezierPath bezierPath];
            [liveEdge moveToPoint:nodePoints[source]];
            [liveEdge lineToPoint:nodePoints[destination]];
            [liveEdge setLineWidth:0.75 + static_cast<CGFloat>(energy) * 2.5];
            [s3g::clap_gui::color(0xf2f2f2,
                0.12 + energy * 0.72) setStroke];
            [liveEdge stroke];

            double phase = p->routeEdgePhase[physicalSource][physicalDestination]
                .load(std::memory_order_relaxed);
            phase -= std::floor(phase);
            const NSPoint marker = NSMakePoint(
                nodePoints[source].x
                    + (nodePoints[destination].x - nodePoints[source].x) * phase,
                nodePoints[source].y
                    + (nodePoints[destination].y - nodePoints[source].y) * phase);
            const CGFloat markerRadius = 1.5 + static_cast<CGFloat>(energy) * 2.0;
            [s3g::clap_gui::color(0xffffff,
                0.35 + energy * 0.65) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                marker.x - markerRadius, marker.y - markerRadius,
                markerRadius * 2.0, markerRadius * 2.0)] fill];
        }
    }

    const NSPoint centroidPoint = projectTopology(centroidX, centroidY, centroidZ);
    const double centroidEnergy = std::clamp<double>(
        p->routeCentroidEnergy.load(std::memory_order_relaxed), 0.0, 1.0);
    if (centroidEnergy > 0.0001) {
        [s3g::clap_gui::color(0xe8e8e8,
            0.05 + centroidEnergy * 0.22) setStroke];
        for (uint32_t lane = 0; lane < visualLanes; ++lane) {
            NSBezierPath* spoke = [NSBezierPath bezierPath];
            [spoke moveToPoint:centroidPoint];
            [spoke lineToPoint:nodePoints[lane]];
            [spoke setLineWidth:0.5 + centroidEnergy];
            [spoke stroke];
        }
        const CGFloat centroidHalo = 9.0
            + static_cast<CGFloat>(centroidEnergy) * 17.0;
        [s3g::clap_gui::color(0xffffff,
            0.05 + centroidEnergy * 0.20) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            centroidPoint.x - centroidHalo,
            centroidPoint.y - centroidHalo,
            centroidHalo * 2.0, centroidHalo * 2.0)] fill];

        double centroidPhase = p->routeCentroidPhase.load(
            std::memory_order_relaxed);
        centroidPhase -= std::floor(centroidPhase);
        const double angle = centroidPhase * 6.283185307179586;
        const NSPoint centroidMarker = NSMakePoint(
            centroidPoint.x + std::cos(angle) * centroidHalo,
            centroidPoint.y + std::sin(angle) * centroidHalo);
        [accent setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            centroidMarker.x - 2.0, centroidMarker.y - 2.0, 4.0, 4.0)] fill];
    }
    [s3gTapeColor(0xd8d8d8) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(centroidPoint.x - 6, centroidPoint.y)
                              toPoint:NSMakePoint(centroidPoint.x + 6, centroidPoint.y)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(centroidPoint.x, centroidPoint.y - 6)
                              toPoint:NSMakePoint(centroidPoint.x, centroidPoint.y + 6)];

    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const uint32_t physicalLane = activePins[lane];
        const double nodeEnergy = std::clamp<double>(
            p->routeNodeEnergy[physicalLane].load(std::memory_order_relaxed),
            0.0, 1.0);
        if (nodeEnergy > 0.0001) {
            const CGFloat haloRadius = 8.0 + static_cast<CGFloat>(nodeEnergy) * 13.0;
            [s3g::clap_gui::color(0xf0f0f0,
                0.06 + nodeEnergy * 0.24) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                nodePoints[lane].x - haloRadius,
                nodePoints[lane].y - haloRadius,
                haloRadius * 2.0, haloRadius * 2.0)] fill];
        }
        const CGFloat r = 5.0;
        [accent setFill];
        NSRectFill(NSMakeRect(nodePoints[lane].x - r, nodePoints[lane].y - r, r * 2.0, r * 2.0));
        NSString* label = [NSString stringWithFormat:@"%u", physicalLane + 1u];
        [label drawAtPoint:NSMakePoint(nodePoints[lane].x + 7, nodePoints[lane].y - 8) withAttributes:smallAttrs];
    }

    const bool routesVisualActive = p->routeAmount > 0.000001
        || p->publishedRouteTailFrames.load(std::memory_order_relaxed) > 0u;
    NSString* topologyName = routesVisualActive
        ? [NSString stringWithFormat:@"ECHO ROUTES / %u NODES", visualLanes]
        : visualLanes == 8 ? @"8PT NEIGHBOR MAP"
        : visualLanes == 6 ? @"6PT NEIGHBOR MAP"
        : visualLanes == 4 ? @"4PT NEIGHBOR MAP"
        : @"SPHERE NEIGHBOR MAP";
    [topologyName drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 10) withAttributes:smallAttrs];
    [[NSString stringWithFormat:@"SHAPE %@", [NSString stringWithUTF8String:topologyShapeName(p->topologyShape)]]
        drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 25)
      withAttributes:smallAttrs];
    NSString* shapeHint = p->topologyShape == 0
        ? @"X=AZ Y=EL Z=DIST"
        : [NSString stringWithFormat:@"CENTROID + %uNN", drawNeighborCount];
    [shapeHint drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 40) withAttributes:smallAttrs];
    NSRect readoutButton = NSMakeRect(fieldX + fieldW - 42, fieldY + 54, 32, 15);
    [strip setFill];
    NSRectFill(readoutButton);
    [grid setStroke];
    NSFrameRect(readoutButton);
    if (_showReadout) {
        [@"X" drawAtPoint:NSMakePoint(readoutButton.origin.x + 12, readoutButton.origin.y + 1) withAttributes:smallAttrs];
        [@"DLY FDB CHR DIF SMR" drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 55) withAttributes:smallAttrs];
        for (uint32_t row = 0; row < activePinCount; ++row) {
            const uint32_t lane = activePins[row];
            NSString* line = [NSString stringWithFormat:@"L%u %4.0f %.2f %.2f %.2f %.2f",
                                        lane + 1u,
                                        resolvedChannelDelayMs(*p, row, visualLanes),
                                        resolvedChannelFeedback(*p, row, visualLanes),
                                        resolvedChannelCharacter(*p, row, visualLanes),
                                        resolvedChannelNetwork(*p, row, visualLanes),
                                        resolvedChannelSmearAmount(*p, row, visualLanes)];
            [line drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 70 + row * 15.0) withAttributes:smallAttrs];
        }
    } else {
        [@"LST" drawAtPoint:NSMakePoint(readoutButton.origin.x + 6, readoutButton.origin.y + 1) withAttributes:smallAttrs];
    }
    }

    const CGFloat panelX = kDelayGuiPanelX;
    const CGFloat topologyX = kDelayGuiTopologyPanelX;
    const CGFloat panelW = kDelayGuiPanelWidth;
    const CGFloat headerH = 21.0;
    const CGFloat gap =
        s3g::gui_layout::kStandardMetrics.panelGap;
    CGFloat panelY = 34.0;
    auto drawHeader = [&](NSString* title, CGFloat x, CGFloat y) {
        s3g::clap_gui::drawPanelHeader(
            title, true, x, y, panelW, headerH, sectionAttrs, style);
    };
    auto drawPanelFrame = [&](CGFloat x, CGFloat y, CGFloat h) {
        s3g::clap_gui::drawPanelFrame(
            x, y, panelW, h, style);
    };

    const CGFloat outputH = 80.0;
    drawPanelFrame(panelX, panelY, outputH);
    s3g::clap_gui::drawPanelHeader(
        @"OUTPUT", true, panelX, panelY, panelW, headerH, sectionAttrs, style);
    [self drawSlider:@"OUT"
               value:[NSString stringWithFormat:@"%+4.1f dB", p->outputTrimDb]
                norm:static_cast<CGFloat>((p->outputTrimDb - kOutputTrimMinDb) / (kOutputTrimMaxDb - kOutputTrimMinDb))
                   y:delayOutputRowY(panelY, 0)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    [self drawSlider:@"MIX"
               value:[NSString stringWithFormat:@"%3.0f%%", p->mix * 100.0]
                norm:static_cast<CGFloat>(p->mix)
                   y:delayOutputRowY(panelY, 1)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    panelY += outputH + gap;

    const CGFloat engineH = static_cast<CGFloat>(
        s3g::gui_layout::toolboxHeightForRows(6u));
    drawPanelFrame(panelX, panelY, engineH);
    drawHeader(@"ENGINE", panelX, panelY);
    [self drawSlider:@"TIME"
                   value:[NSString stringWithFormat:@"%4.0f", p->delayMs]
                norm:static_cast<CGFloat>((p->delayMs - kDelayMinMs) / (kDelayMaxMs - kDelayMinMs))
                   y:delayEngineRowY(panelY, 0)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    [self drawSlider:@"FDBK"
               value:[NSString stringWithFormat:@"%3.0f%%", p->feedback * 100.0]
                norm:static_cast<CGFloat>(p->feedback / 0.82)
                   y:delayEngineRowY(panelY, 1)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    [self drawSlider:@"TONE"
               value:[NSString stringWithFormat:@"%3.0f%%", p->tone * 100.0]
                norm:static_cast<CGFloat>(p->tone)
                   y:delayEngineRowY(panelY, 2)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    [self drawSlider:@"PITCH"
               value:[NSString stringWithFormat:@"%+4.1f", p->pitchSemitones]
                norm:static_cast<CGFloat>((p->pitchSemitones - kPitchMinSemitones) / (kPitchMaxSemitones - kPitchMinSemitones))
                   y:delayEngineRowY(panelY, 3)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    [self drawSlider:@"CHAR"
               value:[NSString stringWithFormat:@"%3.0f%%", p->character * 100.0]
                norm:static_cast<CGFloat>(p->character)
                   y:delayEngineRowY(panelY, 4)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    [self drawSlider:@"SMR"
               value:[NSString stringWithFormat:@"%3.0f%%", p->tapAmount * 100.0]
                norm:static_cast<CGFloat>(p->tapAmount)
                   y:delayEngineRowY(panelY, 5)
          labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    panelY += engineH + gap;

    const CGFloat topologyY = 34.0;
    const CGFloat metaH = static_cast<CGFloat>(
        s3g::gui_layout::toolboxHeightForRows(16u));
    drawPanelFrame(topologyX, topologyY, metaH);
    drawHeader(@"TOPOLOGY", topologyX, topologyY);
    NSRect resetRect = NSMakeRect(
        topologyX + panelW - 64.0, topologyY + 4, 54, 15);
    [strip setFill];
    NSRectFill(resetRect);
    [grid setStroke];
    NSFrameRect(resetRect);
    [@"RESET" drawAtPoint:NSMakePoint(
        resetRect.origin.x + 9.0, topologyY + 6)
        withAttributes:smallAttrs];
    s3g::clap_gui::TopologyUiValues topoValues;
    topoValues.shape = topologyShapeName(p->topologyShape);
    topoValues.amount = p->topologySpread;
    topoValues.pull = p->displaceCollapse;
    topoValues.x = p->displaceDirX;
    topoValues.y = p->displaceDirY;
    topoValues.z = p->displaceDirZ;
    topoValues.twist = p->displaceTwist;
    topoValues.flare = p->displaceFlare;
    topoValues.seed = p->topologyJitter;
    topoValues.motion = topologyMotionModeName(p->topologyMotionMode);
    topoValues.variant = topologyVariantName(p->topologyMotionVariant);
    topoValues.rateHz = p->topologyMotionRateHz;
    topoValues.rateMinHz = kTopologyMotionMinHz;
    topoValues.rateMaxHz = kTopologyMotionMaxHz;
    topoValues.depth = p->topologyMotionDepth;
    topoValues.neighbors = p->topologyNeighborCount;
    topoValues.neighborSuffix = true;
    topoValues.radius = p->topologyRadius;
    topoValues.centroid = p->topologyCentroid;
    s3g::clap_gui::Style topologyStyle;
    s3g::clap_gui::drawTopologyRows(
        topoValues, topologyY, smallAttrs, smallAttrs, topologyStyle,
        kDelayGuiTopologyRowPitch, topologyX, panelW);

    const CGFloat echoRoutesY = 490.0;
    const CGFloat echoRoutesH = 132.0;
    drawPanelFrame(topologyX, echoRoutesY, echoRoutesH);
    drawHeader(@"ECHO ROUTES", topologyX, echoRoutesY);
    auto drawEchoRouteSlider = [&](NSString* name, NSString* value,
                                   CGFloat norm, uint32_t row) {
        s3g::clap_gui::drawProcessorSlider(
            name, value, norm,
            echoRoutesY + 36.0 + static_cast<CGFloat>(row) * 26.0,
            topologyX, panelW, smallAttrs, smallAttrs, style);
    };
    drawEchoRouteSlider(@"ROUTE",
        [NSString stringWithFormat:@"%3.0f%%", p->routeAmount * 100.0],
        static_cast<CGFloat>(p->routeAmount), 0u);
    drawEchoRouteSlider(@"TURN",
        [NSString stringWithFormat:@"%+3.0f%%", p->routeTurn * 100.0],
        static_cast<CGFloat>((p->routeTurn + 1.0) * 0.5), 1u);
    drawEchoRouteSlider(@"BRCH",
        [NSString stringWithFormat:@"%3.0f%%", p->routeBranch * 100.0],
        static_cast<CGFloat>(p->routeBranch), 2u);
    drawEchoRouteSlider(@"LOSS",
        [NSString stringWithFormat:@"%3.0f%%", p->routeLoss * 100.0],
        static_cast<CGFloat>(p->routeLoss), 3u);

    const bool compactMatrix = kVisiblePatchChannels > 8;
    const CGFloat left = compactMatrix ? 686.0 : 718.0;
    const CGFloat cell = compactMatrix ? 12.0 : 24.0;
    const CGFloat cellGap = compactMatrix ? 2.0 : 4.0;
    const CGFloat activeInset = compactMatrix ? 2.0 : 5.0;
    const CGFloat rowLabelX = 654.0;
    const CGFloat matrixTopPad = compactMatrix ? 34.0 : 42.0;
    const CGFloat matrixH = compactMatrix ? 354.0 : 248.0;
    drawPanelFrame(panelX, panelY, matrixH);
    NSString* matrixTitle = kChannelCount > kVisiblePatchChannels
        ? [NSString stringWithFormat:@"PATCH MATRIX 1-%u", kVisiblePatchChannels]
        : @"PATCH MATRIX";
    drawHeader(matrixTitle, panelX, panelY);
    {
        const CGFloat top = panelY + matrixTopPad;
        NSDictionary* matrixAttrs = compactMatrix ? tinyAttrs : smallAttrs;
        for (uint32_t i = 0; i < kVisiblePatchChannels; ++i) {
            NSString* outLabel = [NSString stringWithFormat:@"%u", i + 1];
            [outLabel drawAtPoint:NSMakePoint(left + i * cell + (compactMatrix ? 0.0 : 8.0), top - (compactMatrix ? 13.0 : 18.0)) withAttributes:matrixAttrs];
            NSString* inLabel = [NSString stringWithFormat:@"I%u", i + 1];
            [inLabel drawAtPoint:NSMakePoint(rowLabelX, top + i * cell + (compactMatrix ? 2.0 : 6.0)) withAttributes:matrixAttrs];
        }

        for (uint32_t in = 0; in < kVisiblePatchChannels; ++in) {
            for (uint32_t out = 0; out < kVisiblePatchChannels; ++out) {
                const bool connected = p->patch.connected(in, out);
                NSRect r = NSMakeRect(left + out * cell, top + in * cell, cell - cellGap, cell - cellGap);
                [strip setFill];
                NSRectFill(r);
                [grid setStroke];
                NSFrameRect(r);
                if (connected) {
                    [accent setFill];
                    NSRectFill(NSInsetRect(r, activeInset, activeInset));
                }
            }
        }

        NSString* clearText = kLockUnusedChannelsToPassThrough
            ? @"UNUSED: PASS LOCK"
            : (p->clearUnused ? @"UNUSED: CLEAR" : @"UNUSED: PASS");
        [clearText drawAtPoint:NSMakePoint(650, top + cell * kVisiblePatchChannels + 18) withAttributes:smallAttrs];
    }

    if (_openMenu > 0 && _menuItemCount > 0) {
        const CGFloat menuW = 178.0;
        const CGFloat itemH = 18.0;
        const CGFloat menuH = itemH * static_cast<CGFloat>(_menuItemCount);
        NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, menuW, menuH);
        [s3gTapeColor(0x080808) setFill];
        NSRectFill(NSInsetRect(menuRect, -2, -2));
        [cellBg setFill];
        NSRectFill(menuRect);
        [grid setStroke];
        NSFrameRect(menuRect);

        auto menuTitle = [&](uint32_t index) -> NSString* {
            if (_openMenu == 1) {
                return [NSString stringWithUTF8String:topologyShapeName(index)];
            }
            if (_openMenu == 2) {
                return [NSString stringWithUTF8String:topologyMotionModeName(index)];
            }
            if (_openMenu == 4) {
                return [NSString stringWithUTF8String:topologyVariantName(index)];
            }
            return [NSString stringWithFormat:@"%uNN", index + 1u];
        };
        auto menuSelected = [&](uint32_t index) -> bool {
            if (_openMenu == 1) {
                return index == p->topologyShape;
            }
            if (_openMenu == 2) {
                return index == p->topologyMotionMode;
            }
            if (_openMenu == 4) {
                return index == p->topologyMotionVariant;
            }
            return (index + 1u) == p->topologyNeighborCount;
        };

        for (uint32_t i = 0; i < _menuItemCount; ++i) {
            NSRect row = NSMakeRect(_menuOrigin.x, _menuOrigin.y + static_cast<CGFloat>(i) * itemH, menuW, itemH);
            if (static_cast<int>(i) == _hoverMenuItem) {
                [s3gTapeColor(0x343434) setFill];
                NSRectFill(NSInsetRect(row, 1, 1));
                [fillColor setFill];
                NSRectFill(NSMakeRect(row.origin.x + 2, row.origin.y + 2, 3, row.size.height - 4));
            } else if (menuSelected(i)) {
                [s3gTapeColor(0x2c2c2c) setFill];
                NSRectFill(NSInsetRect(row, 1, 1));
                [fillColor setFill];
                NSRectFill(NSMakeRect(row.origin.x + 2, row.origin.y + 2, 3, row.size.height - 4));
            } else if ((i % 2u) == 1u) {
                [strip setFill];
                NSRectFill(NSInsetRect(row, 1, 1));
            }
            [menuTitle(i) drawAtPoint:NSMakePoint(row.origin.x + 9, row.origin.y + 3) withAttributes:smallAttrs];
        }
    }
    [NSGraphicsContext restoreGraphicsState];
}

- (void)resetTopology
{
    auto* p = static_cast<Plugin*>(_plugin);
    constexpr clap_id ids[] {
        kTopologyShapeParamId, kTopologySpreadParamId,
        kTopologySkewParamId, kTopologyJitterParamId,
        kDisplaceCollapseParamId, kDisplaceDirXParamId,
        kDisplaceDirYParamId, kDisplaceDirZParamId,
        kDisplaceTwistParamId, kDisplaceFlareParamId,
        kTopologyMotionModeParamId, kTopologyMotionVariantParamId,
        kTopologyMotionRateParamId, kTopologyMotionDepthParamId,
        kTopologyNeighborCountParamId, kTopologyRadiusParamId,
        kTopologyCentroidParamId,
    };
    constexpr double defaults[] {
        0.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 0.10, 0.0, 2.0, 0.65, 0.22,
    };
    static_assert(std::size(ids) == std::size(defaults));
    for (size_t i = 0; i < std::size(ids); ++i) {
        setParam(*p, ids[i], defaults[i]);
    }
    _viewYaw = -0.52;
    _viewPitch = 0.34;
    _cameraView = 2;
    [self setNeedsDisplay:YES];
}

- (void)setTopologyView:(uint32_t)view
{
    _cameraView = static_cast<int>(std::min<uint32_t>(view, 2u));
    if (view == 0) {
        _viewYaw = 0.0;
        _viewPitch = 0.95;
    } else if (view == 1) {
        _viewYaw = -1.57079632679;
        _viewPitch = 0.0;
    } else {
        _viewYaw = -0.52;
        _viewPitch = 0.34;
    }
    [self setNeedsDisplay:YES];
}

- (void)updateSliderAtPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider < 0 || _dragSlider > 26) {
        return;
    }
    const CGFloat panelX =
        _dragSlider >= 9 ? kDelayGuiTopologyPanelX : kDelayGuiPanelX;
    const double norm = std::clamp(
        (pt.x - s3g::gui_layout::processorControlX(panelX))
            / s3g::gui_layout::processorTrackWidth(kDelayGuiPanelWidth),
        0.0, 1.0);
    clap_id paramId = CLAP_INVALID_ID;
    double value = norm;
    switch (_dragSlider) {
    case 0: paramId = kDelayMsParamId; value = kDelayMinMs + norm * (kDelayMaxMs - kDelayMinMs); break;
    case 1: paramId = kFeedbackParamId; value = norm * 0.82; break;
    case 2: paramId = kMixParamId; break;
    case 3: paramId = kToneParamId; break;
    case 4: paramId = kPitchParamId; value = kPitchMinSemitones + norm * (kPitchMaxSemitones - kPitchMinSemitones); break;
    case 5: paramId = kCharacterParamId; break;
    case 6: paramId = kTapParamId; break;
    case 7: paramId = kOutputTrimParamId; value = kOutputTrimMinDb + norm * (kOutputTrimMaxDb - kOutputTrimMinDb); break;
    case 8: paramId = kTopologyShapeParamId; value = norm * static_cast<double>(kTopologyShapeCount - 1u); break;
    case 9: paramId = kTopologySpreadParamId; break;
    case 10: paramId = kDisplaceCollapseParamId; break;
    case 11: paramId = kDisplaceDirXParamId; value = norm * 2.0 - 1.0; break;
    case 12: paramId = kDisplaceDirYParamId; value = norm * 2.0 - 1.0; break;
    case 13: paramId = kDisplaceDirZParamId; value = norm * 2.0 - 1.0; break;
    case 14: paramId = kDisplaceTwistParamId; value = norm * 2.0 - 1.0; break;
    case 15: paramId = kDisplaceFlareParamId; value = norm * 2.0 - 1.0; break;
    case 16: paramId = kTopologyJitterParamId; break;
    case 17: paramId = kTopologyMotionModeParamId; value = norm * static_cast<double>(kTopologyMotionModeCount - 1u); break;
    case 18: paramId = kTopologyMotionRateParamId; value = kTopologyMotionMinHz + norm * (kTopologyMotionMaxHz - kTopologyMotionMinHz); break;
    case 19: paramId = kTopologyMotionDepthParamId; break;
    case 20: paramId = kTopologyNeighborCountParamId; value = 1.0 + norm * 2.0; break;
    case 21: paramId = kTopologyRadiusParamId; break;
    case 22: paramId = kTopologyCentroidParamId; break;
    case 23: paramId = kRouteAmountParamId; break;
    case 24: paramId = kRouteTurnParamId; value = norm * 2.0 - 1.0; break;
    case 25: paramId = kRouteBranchParamId; break;
    case 26: paramId = kRouteLossParamId; break;
    default: break;
    }
    if (paramId != CLAP_INVALID_ID) setParam(*p, paramId, value);
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
    syncGuiSettings(*p);
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kDelayGuiWidth, kDelayGuiHeight);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Processor Delay", titleBand,
            _titlePresetName, sizeof(_titlePresetName))) {
        [self setNeedsDisplay:YES];
        return;
    }
    pt.y -= kDelayContentTranslation;

    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 178.0, itemH * static_cast<CGFloat>(_menuItemCount));
        if (NSPointInRect(pt, menuRect)) {
            const uint32_t index = std::min<uint32_t>(_menuItemCount - 1u, static_cast<uint32_t>((pt.y - _menuOrigin.y) / itemH));
            if (_openMenu == 1) {
                setParam(*p, kTopologyShapeParamId, index);
            } else if (_openMenu == 2) {
                setParam(*p, kTopologyMotionModeParamId, index);
            } else if (_openMenu == 4) {
                setParam(*p, kTopologyMotionVariantParamId, index);
            } else if (_openMenu == 3) {
                setParam(*p, kTopologyNeighborCountParamId, index + 1u);
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        [self setNeedsDisplay:YES];
        return;
    }

    const auto& fieldLayout =
        s3g::gui_layout::kTopologyProcessorColumns.field;
    const NSRect topologyPanel = NSMakeRect(
        fieldLayout.x,
        fieldLayout.y - kDelayContentTranslation,
        fieldLayout.width,
        fieldLayout.height);
    if (NSPointInRect(pt, topologyPanel)) {
        for (int i = 0; i < 2; ++i) {
            NSRect button = [self fieldPageButtonRect:topologyPanel index:i];
            if (NSPointInRect(pt, button)) {
                _fieldPage = i;
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }

    for (uint32_t i = 0; i < 3; ++i) {
        if (_fieldPage == 0 && NSPointInRect(
                pt,
                s3g::clap_gui::topologyProcessorCameraButtonRect(
                    topologyPanel, i))) {
            [self setTopologyView:i];
            return;
        }
    }

    const CGFloat panelX = kDelayGuiPanelX;
    const CGFloat topologyX = kDelayGuiTopologyPanelX;
    const CGFloat panelW = kDelayGuiPanelWidth;
    const CGFloat gap =
        s3g::gui_layout::kStandardMetrics.panelGap;
    CGFloat panelY = 34.0;
    auto menuOrigin = [&](CGFloat x, CGFloat preferredY, uint32_t itemCount) {
        const CGFloat itemH = 18.0;
        const CGFloat bottom = kDelayContentCoordinateHeight - 10.0;
        return NSMakePoint(x, std::max<CGFloat>(28.0, std::min<CGFloat>(preferredY, bottom - itemH * static_cast<CGFloat>(itemCount))));
    };

    const CGFloat outputH = 80.0;
    const int outputDragIds[] = { 7, 2 };
    const clap_id outputParamIds[] = { kOutputTrimParamId, kMixParamId };
    for (uint32_t i = 0; i < 2u; ++i) {
        NSRect r = NSMakeRect(
            panelX, delayOutputRowY(panelY, i) - 8.0,
            panelW, 24.0);
        if (!NSPointInRect(pt, r)) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, outputParamIds[i], &defaultValue)) {
            setParam(*p, outputParamIds[i], defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = outputDragIds[i];
            [self updateSliderAtPoint:pt];
        }
        [self setNeedsDisplay:YES];
        return;
    }
    panelY += outputH + gap;

    const CGFloat engineH = static_cast<CGFloat>(
        s3g::gui_layout::toolboxHeightForRows(6u));
    const int engineDragIds[] = { 0, 1, 3, 4, 5, 6 };
    const clap_id engineParamIds[] = {
        kDelayMsParamId, kFeedbackParamId, kToneParamId,
        kPitchParamId, kCharacterParamId, kTapParamId
    };
    for (uint32_t i = 0; i < 6u; ++i) {
        NSRect r = NSMakeRect(
            panelX, delayEngineRowY(panelY, i) - 8.0,
            panelW, 24.0);
        if (NSPointInRect(pt, r)) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, engineParamIds[i], &defaultValue)) {
                setParam(*p, engineParamIds[i], defaultValue);
                _dragSlider = -1;
            } else {
                _dragSlider = engineDragIds[i];
                [self updateSliderAtPoint:pt];
            }
            return;
        }
    }
    panelY += engineH + gap;

    const CGFloat topologyY = 34.0;
    if (NSPointInRect(pt, NSMakeRect(
            topologyX + panelW - 64.0, topologyY + 4, 54, 15))) {
        [self resetTopology];
        return;
    }
    {
        const auto row = s3g::clap_gui::hitTopologyRow(
            pt, topologyY, topologyX, panelW,
            kDelayGuiTopologyRowPitch);
        if (row == s3g::clap_gui::TopologyRow::Shape) {
            _openMenu = 1;
            _hoverMenuItem = -1;
            _menuItemCount = kTopologyShapeCount;
            _menuOrigin = menuOrigin(
                s3g::gui_layout::processorControlX(topologyX),
                s3g::clap_gui::topologyRowY(
                    topologyY, row, kDelayGuiTopologyRowPitch) + 18.0,
                _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (row == s3g::clap_gui::TopologyRow::Motion) {
            _openMenu = 2;
            _hoverMenuItem = -1;
            _menuItemCount = kTopologyMotionModeCount;
            _menuOrigin = menuOrigin(
                s3g::gui_layout::processorControlX(topologyX),
                s3g::clap_gui::topologyRowY(
                    topologyY, row, kDelayGuiTopologyRowPitch) + 18.0,
                _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (row == s3g::clap_gui::TopologyRow::Variant) {
            _openMenu = 4;
            _hoverMenuItem = -1;
            _menuItemCount = kTopologyVariantCount;
            _menuOrigin = menuOrigin(
                s3g::gui_layout::processorControlX(topologyX),
                s3g::clap_gui::topologyRowY(
                    topologyY, row, kDelayGuiTopologyRowPitch) + 18.0,
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
                s3g::clap_gui::topologyRowY(
                    topologyY, row, kDelayGuiTopologyRowPitch) + 18.0,
                _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        switch (row) {
        case s3g::clap_gui::TopologyRow::Amount: _dragSlider = 9; break;
        case s3g::clap_gui::TopologyRow::Pull: _dragSlider = 10; break;
        case s3g::clap_gui::TopologyRow::X: _dragSlider = 11; break;
        case s3g::clap_gui::TopologyRow::Y: _dragSlider = 12; break;
        case s3g::clap_gui::TopologyRow::Z: _dragSlider = 13; break;
        case s3g::clap_gui::TopologyRow::Twist: _dragSlider = 14; break;
        case s3g::clap_gui::TopologyRow::Flare: _dragSlider = 15; break;
        case s3g::clap_gui::TopologyRow::Seed: _dragSlider = 16; break;
        case s3g::clap_gui::TopologyRow::Rate: _dragSlider = 18; break;
        case s3g::clap_gui::TopologyRow::Depth: _dragSlider = 19; break;
        case s3g::clap_gui::TopologyRow::Radius: _dragSlider = 21; break;
        case s3g::clap_gui::TopologyRow::Centroid: _dragSlider = 22; break;
        default: _dragSlider = -1; break;
        }
        if (_dragSlider >= 0) {
            auto paramForDrag = [](int drag) -> clap_id {
                switch (drag) {
                case 9: return kTopologySpreadParamId;
                case 10: return kDisplaceCollapseParamId;
                case 11: return kDisplaceDirXParamId;
                case 12: return kDisplaceDirYParamId;
                case 13: return kDisplaceDirZParamId;
                case 14: return kDisplaceTwistParamId;
                case 15: return kDisplaceFlareParamId;
                case 16: return kTopologyJitterParamId;
                case 18: return kTopologyMotionRateParamId;
                case 19: return kTopologyMotionDepthParamId;
                case 21: return kTopologyRadiusParamId;
                case 22: return kTopologyCentroidParamId;
                default: return CLAP_INVALID_ID;
                }
            };
            const clap_id param = paramForDrag(_dragSlider);
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, param, &defaultValue)) {
                setParam(*p, param, defaultValue);
                _dragSlider = -1;
            } else {
                [self updateSliderAtPoint:pt];
            }
            return;
        }
    }

    const CGFloat echoRoutesY = 490.0;
    const int echoRouteDragIds[] = { 23, 24, 25, 26 };
    const clap_id echoRouteParamIds[] = {
        kRouteAmountParamId, kRouteTurnParamId,
        kRouteBranchParamId, kRouteLossParamId
    };
    for (uint32_t i = 0; i < 4u; ++i) {
        const CGFloat rowY = echoRoutesY + 36.0
            + static_cast<CGFloat>(i) * 26.0;
        const NSRect rowRect = NSMakeRect(
            topologyX, rowY - 8.0, panelW, 24.0);
        if (!NSPointInRect(pt, rowRect)) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, echoRouteParamIds[i], &defaultValue)) {
            setParam(*p, echoRouteParamIds[i], defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = echoRouteDragIds[i];
            [self updateSliderAtPoint:pt];
        }
        [self setNeedsDisplay:YES];
        return;
    }

    {
        const bool compactMatrix = kVisiblePatchChannels > 8;
        const CGFloat left = compactMatrix ? 686.0 : 718.0;
        const CGFloat top = panelY + (compactMatrix ? 34.0 : 42.0);
        const CGFloat cell = compactMatrix ? 12.0 : 24.0;
        if (pt.x >= left && pt.y >= top
            && pt.x < left + cell * kVisiblePatchChannels
            && pt.y < top + cell * kVisiblePatchChannels) {
            auto* p = static_cast<Plugin*>(_plugin);
            const uint32_t out = static_cast<uint32_t>((pt.x - left) / cell);
            const uint32_t in = static_cast<uint32_t>((pt.y - top) / cell);
            togglePatchCellFromGui(*p, in, out);
            [self setNeedsDisplay:YES];
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
            topologyPanel);
    if (_fieldPage == 0 && NSPointInRect(pt, topologyView)) {
        _dragTopologyView = true;
        _lastDragPoint = pt;
        return;
    }
}

- (void)mouseMoved:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    pt.y -= kDelayContentTranslation;
    [self updateMenuHover:pt];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    pt.y -= kDelayContentTranslation;
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
    [self updateSliderAtPoint:pt];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
    _dragTopologyView = false;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) {
        return false;
    }
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) {
        return false;
    }
    auto* p = self(plugin);
    if (p->guiView) {
        return true;
    }
    p->guiView = [[S3GDelayProcessorView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView),
            static_cast<uint32_t>(kDelayGuiWidth),
            static_cast<uint32_t>(kDelayGuiHeight),
            static_cast<uint32_t>(kDelayGuiWidth), 360u)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible.store(false, std::memory_order_relaxed);
        p->guiDirty.store(false, std::memory_order_relaxed);
        NSView* view = static_cast<NSView*>(p->guiView);
        if ([view respondsToSelector:@selector(stopRefreshTimer)]) {
            [static_cast<S3GDelayProcessorView*>(view) stopRefreshTimer];
        }
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport,
        static_cast<uint32_t>(kDelayGuiWidth),
        static_cast<uint32_t>(kDelayGuiHeight), width, height,
        static_cast<uint32_t>(kDelayGuiWidth), 360u);
}

bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, static_cast<uint32_t>(kDelayGuiWidth), static_cast<uint32_t>(kDelayGuiHeight), width, height, static_cast<uint32_t>(kDelayGuiWidth), 360u); }

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) {
        return false;
    }
    p->guiVisible.store(true, std::memory_order_relaxed);
    if ([static_cast<NSView*>(p->guiView) respondsToSelector:@selector(startRefreshTimer)]) {
        [static_cast<S3GDelayProcessorView*>(p->guiView) startRefreshTimer];
    }
    requestGuiRedraw(*p);
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    p->guiVisible.store(false, std::memory_order_relaxed);
    p->guiDirty.store(false, std::memory_order_relaxed);
    if ([static_cast<NSView*>(p->guiView) respondsToSelector:@selector(stopRefreshTimer)]) {
        [static_cast<S3GDelayProcessorView*>(p->guiView) stopRefreshTimer];
    }
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t gui {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide
};

#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &audioPorts;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &params;
    }
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) {
        return &latency;
    }
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) {
        return &tail;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &state;
    }
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
        return &gui;
    }
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DELAY,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kPluginName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    kPluginDescription,
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) {
        return nullptr;
    }

    auto* p = new (std::nothrow) Plugin();
    if (!p) {
        return nullptr;
    }

    p->host = host;
    p->hostTail = host && host->get_extension ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL)) : nullptr;
    storeSettingsInParameterBank(*p, static_cast<const DelaySettings&>(*p));
    preparePatch(*p);
    p->delay.prepare(48000.0, static_cast<int>(kChannelCount), 2.25);
    syncAudioSettings(*p, true);
    publishRouteTelemetry(*p);
    publishLegacyTail(*p, 0u, true);
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

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &factory;
    }
    return nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
