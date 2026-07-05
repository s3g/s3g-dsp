#include "s3g_lane_patch.h"
#include "s3g_math.h"
#include "s3g_delay_processor.h"
#include "s3g_topology.h"
#include "s3g_topology_heatmap.h"

#include <clap/clap.h>
#include <clap/ext/latency.h>
#include <clap/ext/tail.h>
#if defined(__APPLE__)
#include <clap/ext/gui.h>
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
constexpr const char* kPluginName = "s3g Delay Processor 24ch";
constexpr const char* kPluginDescription =
    "24-channel topological delay processor with per-lane delay, feedback, tone, pitch, and cross-lane diffusion.";
#else
constexpr const char* kPluginId = "org.s3g.s3g-dsp.delay-processor-8ch";
constexpr const char* kPluginName = "s3g Delay Processor 8ch";
constexpr const char* kPluginDescription =
    "8-channel topological delay processor with per-lane delay, feedback, tone, pitch, and cross-lane diffusion.";
#endif

constexpr bool kLockUnusedChannelsToPassThrough = kChannelCount >= 24;
constexpr uint32_t kVisiblePatchChannels = kChannelCount < 8 ? kChannelCount : 8;
constexpr uint32_t kStateVersion = 10;
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

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
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
    bool clearUnused = false;
    const clap_host_tail_t* hostTail = nullptr;
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> outputClip { false };
    uint32_t meterRedrawCountdown = 0;
    s3g::LanePatch patch;
    s3g::DelayProcessor delay;
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiDirty { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

uint32_t activePatchRows(const Plugin& p);
void requestGuiRedraw(Plugin& p);

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double clampFeedback(double value)
{
    return std::clamp(value, 0.0, 0.95);
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

bool topologyMotionActive(const Plugin& p)
{
    s3g::TopologyState state {};
    state.motionMode = p.topologyMotionMode;
    state.motionDepth = p.topologyMotionDepth;
    state.motionRateHz = p.topologyMotionRateHz;
    return s3g::topologyMotionActive(state);
}

s3g::TopologyState topologyStateForPlugin(const Plugin& p)
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

s3g::TopologyControls topologyControlsForPlugin(const Plugin& p)
{
    return s3g::topologyControlsFromState(topologyStateForPlugin(p));
}

TopologyPoint topologyPointForLane(const Plugin& p, uint32_t channel, uint32_t count)
{
    return s3g::topologyPointForLane(channel, count, topologyControlsForPlugin(p));
}

double topologyLaneValue(const Plugin& p, uint32_t channel, uint32_t count)
{
    return topologyPointForLane(p, channel, count).lane;
}

double topologyAmount(const Plugin& p)
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

std::array<int, 3> nearestTopologyNeighbors(const Plugin& p, uint32_t channel, uint32_t count)
{
    return s3g::nearestTopologyNeighbors(topologyStateForPlugin(p), channel, count);
}

double resolvedChannelDelayMs(const Plugin& p, uint32_t channel, uint32_t count)
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

double resolvedChannelFeedback(const Plugin& p, uint32_t channel, uint32_t count)
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

double resolvedChannelTone(const Plugin& p, uint32_t channel, uint32_t count)
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

double resolvedChannelNetwork(const Plugin& p, uint32_t channel, uint32_t count)
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

double resolvedChannelCharacter(const Plugin& p, uint32_t channel, uint32_t count)
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

double resolvedChannelSmearAmount(const Plugin& p, uint32_t channel, uint32_t count)
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

double resolvedChannelPitchSemitones(const Plugin& p, uint32_t channel, uint32_t count)
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

void applyParamsToDsp(Plugin& p)
{
    const uint32_t topologyCount = std::max<uint32_t>(1, std::min<uint32_t>(kChannelCount, activePatchRows(p)));
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p.delay.setChannelDelayMs(static_cast<int>(ch), static_cast<float>(resolvedChannelDelayMs(p, ch, topologyCount)));
        p.delay.setChannelFeedback(static_cast<int>(ch), static_cast<float>(resolvedChannelFeedback(p, ch, topologyCount)));
        p.delay.setChannelTone(static_cast<int>(ch), static_cast<float>(resolvedChannelTone(p, ch, topologyCount)));
        p.delay.setChannelNetwork(static_cast<int>(ch), static_cast<float>(resolvedChannelNetwork(p, ch, topologyCount)));
        const auto neighbors = nearestTopologyNeighbors(p, ch, topologyCount);
        p.delay.setChannelNetworkTopology(static_cast<int>(ch),
            neighbors[0],
            neighbors[1],
            neighbors[2],
            static_cast<int>(std::clamp<uint32_t>(p.topologyNeighborCount, 1u, 3u)),
            static_cast<float>(p.topologyCentroid));
        p.delay.setChannelCharacter(static_cast<int>(ch), static_cast<float>(resolvedChannelCharacter(p, ch, topologyCount)));
        p.delay.setChannelSmearAmount(static_cast<int>(ch), static_cast<float>(resolvedChannelSmearAmount(p, ch, topologyCount)));
        p.delay.setChannelPitchSemitones(static_cast<int>(ch), static_cast<float>(resolvedChannelPitchSemitones(p, ch, topologyCount)));
    }
}

void applyTopologyMotionSceneDefaults(Plugin& p, uint32_t mode)
{
    p.topologyMotionMode = std::min<uint32_t>(kTopologyMotionModeCount - 1u, mode);
    p.topologyMotionVariant = 0;
    p.topologyMotionPhase = 0.0;
    switch (p.topologyMotionMode) {
    case 1: // FREE
        p.topologyShape = 11;
        p.topologySpread = 0.45;
        p.displaceCollapse = 0.28;
        p.displaceDirX = 0.35;
        p.displaceDirY = -0.20;
        p.displaceDirZ = 0.85;
        p.displaceTwist = 0.22;
        p.displaceFlare = 0.12;
        p.topologyJitter = 0.18;
        p.topologyMotionRateHz = 0.18;
        p.topologyMotionDepth = 0.72;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.65;
        p.topologyCentroid = 0.22;
        break;
    case 2: // DRIFT
        p.topologyShape = 0;
        p.topologySpread = 0.34;
        p.displaceCollapse = 0.08;
        p.displaceDirX = 0.25;
        p.displaceDirY = -0.18;
        p.displaceDirZ = 0.90;
        p.displaceTwist = 0.08;
        p.displaceFlare = 0.10;
        p.topologyJitter = 0.10;
        p.topologyMotionRateHz = 0.04;
        p.topologyMotionDepth = 0.72;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.75;
        p.topologyCentroid = 0.16;
        break;
    case 3: // PULSE
        p.topologyShape = 4;
        p.topologySpread = 0.45;
        p.displaceCollapse = 0.32;
        p.displaceDirX = 0.0;
        p.displaceDirY = 0.0;
        p.displaceDirZ = 1.0;
        p.displaceTwist = 0.0;
        p.displaceFlare = 0.28;
        p.topologyJitter = 0.04;
        p.topologyMotionRateHz = 0.16;
        p.topologyMotionDepth = 0.78;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.62;
        p.topologyCentroid = 0.32;
        break;
    case 4: // ORBIT
        p.topologyShape = 3;
        p.topologySpread = 0.52;
        p.displaceCollapse = 0.0;
        p.displaceDirX = 0.0;
        p.displaceDirY = 1.0;
        p.displaceDirZ = 0.10;
        p.displaceTwist = 0.08;
        p.displaceFlare = 0.0;
        p.topologyJitter = 0.08;
        p.topologyMotionRateHz = 0.10;
        p.topologyMotionDepth = 0.78;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.80;
        p.topologyCentroid = 0.18;
        break;
    case 5: // FOLD
        p.topologyShape = 2;
        p.topologySpread = 0.58;
        p.displaceCollapse = 0.38;
        p.displaceDirX = 0.0;
        p.displaceDirY = 0.20;
        p.displaceDirZ = 1.0;
        p.displaceTwist = 0.40;
        p.displaceFlare = -0.24;
        p.topologyJitter = 0.06;
        p.topologyMotionRateHz = 0.12;
        p.topologyMotionDepth = 0.72;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.55;
        p.topologyCentroid = 0.34;
        break;
    case 6: // WEAVE
        p.topologyShape = 8;
        p.topologySpread = 0.48;
        p.displaceCollapse = 0.10;
        p.displaceDirX = 0.36;
        p.displaceDirY = -0.20;
        p.displaceDirZ = 0.86;
        p.displaceTwist = 0.18;
        p.displaceFlare = -0.14;
        p.topologyJitter = 0.08;
        p.topologyMotionRateHz = 0.14;
        p.topologyMotionDepth = 0.74;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.70;
        p.topologyCentroid = 0.20;
        break;
    case 7: // GRID
        p.topologyShape = 10;
        p.topologySpread = 0.62;
        p.displaceCollapse = 0.02;
        p.displaceDirX = 0.0;
        p.displaceDirY = 0.0;
        p.displaceDirZ = 1.0;
        p.displaceTwist = 0.12;
        p.displaceFlare = 0.10;
        p.topologyJitter = 0.02;
        p.topologyMotionRateHz = 0.11;
        p.topologyMotionDepth = 0.68;
        p.topologyNeighborCount = 3;
        p.topologyRadius = 0.58;
        p.topologyCentroid = 0.20;
        break;
    case 8: // TRACE
        p.topologyShape = 11;
        p.topologySpread = 0.50;
        p.displaceCollapse = 0.08;
        p.displaceDirX = 0.20;
        p.displaceDirY = 0.16;
        p.displaceDirZ = 0.96;
        p.displaceTwist = 0.16;
        p.displaceFlare = 0.08;
        p.topologyJitter = 0.12;
        p.topologyMotionRateHz = 0.09;
        p.topologyMotionDepth = 0.76;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.72;
        p.topologyCentroid = 0.18;
        break;
    case 9: // HOVER
        p.topologyShape = 4;
        p.topologySpread = 0.30;
        p.displaceCollapse = 0.22;
        p.displaceDirX = 0.0;
        p.displaceDirY = 0.55;
        p.displaceDirZ = 0.88;
        p.displaceTwist = 0.02;
        p.displaceFlare = 0.06;
        p.topologyJitter = 0.04;
        p.topologyMotionRateHz = 0.05;
        p.topologyMotionDepth = 0.64;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.62;
        p.topologyCentroid = 0.30;
        break;
    case 10: // LEAP
        p.topologyShape = 6;
        p.topologySpread = 0.42;
        p.displaceCollapse = 0.12;
        p.displaceDirX = 0.20;
        p.displaceDirY = -0.10;
        p.displaceDirZ = 0.90;
        p.displaceTwist = 0.14;
        p.displaceFlare = 0.28;
        p.topologyJitter = 0.22;
        p.topologyMotionRateHz = 0.22;
        p.topologyMotionDepth = 0.76;
        p.topologyNeighborCount = 1;
        p.topologyRadius = 0.52;
        p.topologyCentroid = 0.12;
        break;
    case 11: // FIELD
        p.topologyShape = 4;
        p.topologySpread = 0.56;
        p.displaceCollapse = 0.06;
        p.displaceDirX = 0.0;
        p.displaceDirY = 0.0;
        p.displaceDirZ = 1.0;
        p.displaceTwist = 0.28;
        p.displaceFlare = 0.18;
        p.topologyJitter = 0.10;
        p.topologyMotionRateHz = 0.12;
        p.topologyMotionDepth = 0.70;
        p.topologyNeighborCount = 3;
        p.topologyRadius = 0.68;
        p.topologyCentroid = 0.26;
        break;
    case 12: // PAIR
        p.topologyShape = 7;
        p.topologySpread = 0.38;
        p.displaceCollapse = 0.12;
        p.displaceDirX = 0.0;
        p.displaceDirY = 0.0;
        p.displaceDirZ = 1.0;
        p.displaceTwist = 0.10;
        p.displaceFlare = 0.20;
        p.topologyJitter = 0.02;
        p.topologyMotionRateHz = 0.10;
        p.topologyMotionDepth = 0.68;
        p.topologyNeighborCount = 1;
        p.topologyRadius = 0.45;
        p.topologyCentroid = 0.18;
        break;
    case 13: // FLOW
        p.topologyShape = 3;
        p.topologySpread = 0.50;
        p.displaceCollapse = 0.04;
        p.displaceDirX = 0.12;
        p.displaceDirY = 0.20;
        p.displaceDirZ = 0.94;
        p.displaceTwist = 0.22;
        p.displaceFlare = 0.06;
        p.topologyJitter = 0.08;
        p.topologyMotionRateHz = 0.13;
        p.topologyMotionDepth = 0.72;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.78;
        p.topologyCentroid = 0.16;
        break;
    case 14: // GROUP
        p.topologyShape = 6;
        p.topologySpread = 0.44;
        p.displaceCollapse = 0.24;
        p.displaceDirX = 0.0;
        p.displaceDirY = 0.0;
        p.displaceDirZ = 1.0;
        p.displaceTwist = 0.08;
        p.displaceFlare = 0.10;
        p.topologyJitter = 0.18;
        p.topologyMotionRateHz = 0.08;
        p.topologyMotionDepth = 0.68;
        p.topologyNeighborCount = 3;
        p.topologyRadius = 0.60;
        p.topologyCentroid = 0.38;
        break;
    case 15: // MARCH
        p.topologyShape = 9;
        p.topologySpread = 0.62;
        p.displaceCollapse = 0.02;
        p.displaceDirX = 1.0;
        p.displaceDirY = 0.0;
        p.displaceDirZ = 0.0;
        p.displaceTwist = 0.04;
        p.displaceFlare = 0.0;
        p.topologyJitter = 0.00;
        p.topologyMotionRateHz = 0.16;
        p.topologyMotionDepth = 0.66;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.54;
        p.topologyCentroid = 0.12;
        break;
    case 16: // PATH
        p.topologyShape = 9;
        p.topologySpread = 0.54;
        p.displaceCollapse = 0.14;
        p.displaceDirX = 0.35;
        p.displaceDirY = 0.10;
        p.displaceDirZ = 0.90;
        p.displaceTwist = 0.18;
        p.displaceFlare = 0.06;
        p.topologyJitter = 0.08;
        p.topologyMotionRateHz = 0.12;
        p.topologyMotionDepth = 0.72;
        p.topologyNeighborCount = 2;
        p.topologyRadius = 0.64;
        p.topologyCentroid = 0.20;
        break;
    case 17: // SCAT
        p.topologyShape = 6;
        p.topologySpread = 0.58;
        p.displaceCollapse = 0.04;
        p.displaceDirX = 0.10;
        p.displaceDirY = -0.10;
        p.displaceDirZ = 0.95;
        p.displaceTwist = 0.10;
        p.displaceFlare = 0.22;
        p.topologyJitter = 0.38;
        p.topologyMotionRateHz = 0.18;
        p.topologyMotionDepth = 0.78;
        p.topologyNeighborCount = 1;
        p.topologyRadius = 0.50;
        p.topologyCentroid = 0.10;
        break;
    case 0:
    default:
        p.topologyMotionMode = 0;
        p.topologyMotionDepth = 0.0;
        break;
    }
}

void advanceTopologyMotion(Plugin& p, uint32_t frames)
{
    if (!topologyMotionActive(p) || p.sampleRate <= 0.0 || frames == 0) {
        return;
    }
    p.topologyMotionPhase += (static_cast<double>(frames) / p.sampleRate) * p.topologyMotionRateHz;
    p.topologyMotionPhase -= std::floor(p.topologyMotionPhase);
    applyParamsToDsp(p);
    requestGuiRedraw(p);
}

void requestGuiRedraw(Plugin& p)
{
#if defined(__APPLE__)
    if (!p.guiView) {
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
}

void togglePatchCellFromGui(Plugin& p, uint32_t input, uint32_t output)
{
    p.clearUnused = !kLockUnusedChannelsToPassThrough;
    p.patch.setWidth(kChannelCount);
    p.patch.toggle(input, output);
    applyParamsToDsp(p);
}

uint32_t activePatchRows(const Plugin& p)
{
    uint32_t count = 0;
    for (uint32_t row = 0; row < kChannelCount; ++row) {
        if (p.patch.rowMask(row) != 0) {
            ++count;
        }
    }
    return count > 0 ? count : kChannelCount;
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
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->meterRedrawCountdown = static_cast<uint32_t>(std::max(1.0, sampleRate / 24.0));
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->outputClip.store(false, std::memory_order_relaxed);
    preparePatch(*p);
    p->delay.prepare(sampleRate, static_cast<int>(kChannelCount), 2.25);
    applyParamsToDsp(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->delay.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->outputClip.store(false, std::memory_order_relaxed);
    applyParamsToDsp(*p);
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
        return true;
    default:
        return false;
    }
}

void setParam(Plugin& p, clap_id paramId, double value)
{
    const bool tailWasAffected = paramAffectsTail(paramId);
    switch (paramId) {
    case kDelayMsParamId:
        p.delayMs = clampDelayMs(value);
        break;
    case kFeedbackParamId:
        p.feedback = clampFeedback(value);
        break;
    case kMixParamId:
        p.mix = clamp01(value);
        break;
    case kToneParamId:
        p.tone = clamp01(value);
        break;
    case kCharacterParamId:
        p.character = clamp01(value);
        break;
    case kTapParamId:
        p.tapAmount = clamp01(value);
        break;
    case kOutputTrimParamId:
        p.outputTrimDb = clampOutputTrimDb(value);
        break;
    case kPitchParamId:
        p.pitchSemitones = std::clamp(value, kPitchMinSemitones, kPitchMaxSemitones);
        break;
    case kTopologyShapeParamId:
        p.topologyShape = std::min<uint32_t>(kTopologyShapeCount - 1u, roundedUint(value));
        break;
    case kTopologySpreadParamId:
        p.topologySpread = clamp01(value);
        break;
    case kTopologySkewParamId:
        p.topologySkew = std::clamp(value, -1.0, 1.0);
        break;
    case kTopologyJitterParamId:
        p.topologyJitter = clamp01(value);
        break;
    case kDisplaceCollapseParamId:
        p.displaceCollapse = clamp01(value);
        break;
    case kDisplaceDirXParamId:
        p.displaceDirX = clampBipolar(value);
        break;
    case kDisplaceDirYParamId:
        p.displaceDirY = clampBipolar(value);
        break;
    case kDisplaceDirZParamId:
        p.displaceDirZ = clampBipolar(value);
        break;
    case kDisplaceTwistParamId:
        p.displaceTwist = clampBipolar(value);
        break;
    case kDisplaceFlareParamId:
        p.displaceFlare = clampBipolar(value);
        break;
    case kTopologyMotionModeParamId:
        p.topologyMotionMode = std::min<uint32_t>(kTopologyMotionModeCount - 1u, roundedUint(value));
        if (p.topologyMotionMode == 0u) {
            p.topologyMotionPhase = 0.0;
        }
        break;
    case kTopologyMotionVariantParamId:
        p.topologyMotionVariant = std::min<uint32_t>(kTopologyVariantCount - 1u, roundedUint(value));
        break;
    case kTopologyMotionRateParamId:
        p.topologyMotionRateHz = clampMotionRateHz(value);
        break;
    case kTopologyMotionDepthParamId:
        p.topologyMotionDepth = clamp01(value);
        break;
    case kTopologyNeighborCountParamId:
        p.topologyNeighborCount = std::clamp<uint32_t>(roundedUint(value), 1u, 3u);
        break;
    case kTopologyRadiusParamId:
        p.topologyRadius = clamp01(value);
        break;
    case kTopologyCentroidParamId:
        p.topologyCentroid = clamp01(value);
        break;
    default:
        return;
    }
    applyParamsToDsp(p);
    if (tailWasAffected && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
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

void clearExtraOutputs(const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = channels; ch < output.channel_count; ++ch) {
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
    const float trimmed = value * dbToGain(p.outputTrimDb);
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

void processFloatSegment(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t startFrame, uint32_t frames)
{
    advanceTopologyMotion(p, frames);
    std::array<float, kChannelCount> inFrame {};
    std::array<float, kChannelCount> wetFrame {};
    float blockPeak = 0.0f;
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
                if ((p.patch.rowMask(inCh) & outBit) == 0) {
                    continue;
                }
                hasConnection = true;
                sum += s3g::lerp(inFrame[inCh], wetFrame[inCh], static_cast<float>(p.mix));
            }
            if (hasConnection) {
                output.data32[outCh][i] = applyOutputStage(p, sum, blockPeak, blockClip);
            }
        }
    }
    publishOutputMeter(p, blockPeak, blockClip, frames);
}

void processDoubleSegment(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t startFrame, uint32_t frames)
{
    advanceTopologyMotion(p, frames);
    std::array<float, kChannelCount> inFrame {};
    std::array<float, kChannelCount> wetFrame {};
    float blockPeak = 0.0f;
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
                if ((p.patch.rowMask(inCh) & outBit) == 0) {
                    continue;
                }
                hasConnection = true;
                sum += s3g::lerp(inFrame[inCh], wetFrame[inCh], static_cast<float>(p.mix));
            }
            if (hasConnection) {
                output.data64[outCh][i] = static_cast<double>(applyOutputStage(p, sum, blockPeak, blockClip));
            }
        }
    }
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

    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
        readParamEvents(*p, process->in_events);
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, kChannelCount });

    if (channels == kChannelCount && input.data32 && output.data32) {
        if (p->clearUnused && !kLockUnusedChannelsToPassThrough) {
            clearOutputs(output, kChannelCount, frames);
        } else {
            copyAvailableChannels(input, output, kChannelCount, frames);
        }
        processWithSampleAccurateEvents(*p, input, output, frames, process->in_events,
            [&](uint32_t start, uint32_t count) {
                processFloatSegment(*p, input, output, start, count);
            });
    } else if (channels == kChannelCount && input.data64 && output.data64) {
        if (p->clearUnused && !kLockUnusedChannelsToPassThrough) {
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

    clearExtraOutputs(output, channels, frames);
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

uint32_t paramsCount(const clap_plugin_t*) { return 24; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) {
        return false;
    }

    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Delay Processor", sizeof(info->module));

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
        info->max_value = 0.95;
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
    switch (paramId) {
    case kDelayMsParamId:
        *value = p->delayMs;
        return true;
    case kFeedbackParamId:
        *value = p->feedback;
        return true;
    case kMixParamId:
        *value = p->mix;
        return true;
    case kToneParamId:
        *value = p->tone;
        return true;
    case kCharacterParamId:
        *value = p->character;
        return true;
    case kTapParamId:
        *value = p->tapAmount;
        return true;
    case kOutputTrimParamId:
        *value = p->outputTrimDb;
        return true;
    case kPitchParamId:
        *value = p->pitchSemitones;
        return true;
    case kTopologyShapeParamId:
        *value = static_cast<double>(p->topologyShape);
        return true;
    case kTopologySpreadParamId:
        *value = p->topologySpread;
        return true;
    case kTopologySkewParamId:
        *value = p->topologySkew;
        return true;
    case kTopologyJitterParamId:
        *value = p->topologyJitter;
        return true;
    case kDisplaceCollapseParamId:
        *value = p->displaceCollapse;
        return true;
    case kDisplaceDirXParamId:
        *value = p->displaceDirX;
        return true;
    case kDisplaceDirYParamId:
        *value = p->displaceDirY;
        return true;
    case kDisplaceDirZParamId:
        *value = p->displaceDirZ;
        return true;
    case kDisplaceTwistParamId:
        *value = p->displaceTwist;
        return true;
    case kDisplaceFlareParamId:
        *value = p->displaceFlare;
        return true;
    case kTopologyMotionModeParamId:
        *value = static_cast<double>(p->topologyMotionMode);
        return true;
    case kTopologyMotionVariantParamId:
        *value = static_cast<double>(p->topologyMotionVariant);
        return true;
    case kTopologyMotionRateParamId:
        *value = p->topologyMotionRateHz;
        return true;
    case kTopologyMotionDepthParamId:
        *value = p->topologyMotionDepth;
        return true;
    case kTopologyNeighborCountParamId:
        *value = static_cast<double>(p->topologyNeighborCount);
        return true;
    case kTopologyRadiusParamId:
        *value = p->topologyRadius;
        return true;
    case kTopologyCentroidParamId:
        *value = p->topologyCentroid;
        return true;
    default:
        return false;
    }
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
        std::snprintf(display, size, "%.1f %%", value * 100.0);
        return true;
    case kTopologySkewParamId:
    case kDisplaceDirXParamId:
    case kDisplaceDirYParamId:
    case kDisplaceDirZParamId:
    case kDisplaceTwistParamId:
    case kDisplaceFlareParamId:
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

    const double amount = topologyAmount(*p);
    const double feedbackEstimate = std::clamp(
        p->feedback + amount * kTopologyFeedbackSpread + p->topologyJitter * kTopologyFeedbackJitter,
        0.0,
        0.95);
    if (feedbackEstimate >= 0.80 || resolvedChannelNetwork(*p, 0, kChannelCount) > 0.45) {
        return static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    }

    const double delaySeconds = kDelayMaxMs * 0.001;
    const double repeatsToMinus60 = feedbackEstimate > 0.001 ? std::ceil(std::log(0.001) / std::log(feedbackEstimate)) : 1.0;
    const double tailSeconds = std::clamp(delaySeconds * repeatsToMinus60 + 0.5, 0.5, 30.0);
    return static_cast<uint32_t>(std::ceil(tailSeconds * p->sampleRate));
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

    const auto* p = self(plugin);
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
    preparePatch(*p);
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
    if (p->topologyMotionMode == 0u) {
        p->topologyMotionPhase = 0.0;
    }
    applyParamsToDsp(*p);
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
    bool _showEngine;
    bool _showMeta;
    bool _showDisplace;
    bool _showMatrix;
    bool _showReadout;
    int _openMenu;
    NSPoint _menuOrigin;
    uint32_t _menuItemCount;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)updateSliderAtPoint:(NSPoint)pt;
- (void)resetTopology;
- (void)setTopologyView:(uint32_t)view;
- (void)stopRefreshTimer;
- (void)selectShapeMenuItem:(NSMenuItem*)item;
- (void)selectMotionMenuItem:(NSMenuItem*)item;
- (void)selectNeighborMenuItem:(NSMenuItem*)item;
@end

static NSColor* s3gTapeColor(int rgb)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:1.0];
}

static NSColor* s3gHeatColor(double value, double alpha)
{
    struct Stop {
        double t;
        int r;
        int g;
        int b;
    };
    static constexpr Stop kStops[] = {
        { 0.00, 10, 24, 94 },
        { 0.22, 0, 146, 232 },
        { 0.48, 255, 232, 42 },
        { 0.72, 255, 84, 12 },
        { 1.00, 238, 0, 0 },
    };
    value = std::clamp(value, 0.0, 1.0);
    const Stop* a = &kStops[0];
    const Stop* b = &kStops[sizeof(kStops) / sizeof(kStops[0]) - 1];
    for (size_t i = 1; i < sizeof(kStops) / sizeof(kStops[0]); ++i) {
        if (value <= kStops[i].t) {
            a = &kStops[i - 1];
            b = &kStops[i];
            break;
        }
    }
    const double span = std::max(0.0001, b->t - a->t);
    const double mix = (value - a->t) / span;
    const double r = s3g::lerp(static_cast<double>(a->r), static_cast<double>(b->r), mix) / 255.0;
    const double g = s3g::lerp(static_cast<double>(a->g), static_cast<double>(b->g), mix) / 255.0;
    const double bl = s3g::lerp(static_cast<double>(a->b), static_cast<double>(b->b), mix) / 255.0;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:alpha];
}

@implementation S3GDelayProcessorView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 1000, 700)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _dragTopologyView = false;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewYaw = -0.52;
        _viewPitch = 0.34;
        _showEngine = true;
        _showMeta = true;
        _showDisplace = true;
        _showMatrix = false;
        _showReadout = false;
        _openMenu = 0;
        _menuOrigin = NSMakePoint(0, 0);
        _menuItemCount = 0;
        _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 30.0)
                                                target:self
                                              selector:@selector(refreshTimerFired:)
                                              userInfo:nil
                                               repeats:YES];
        [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
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

- (void)refreshTimerFired:(NSTimer*)timer
{
    (void)timer;
    if ([self isHidden] || !_plugin) {
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    if (topologyMotionActive(*p)) {
        [self setNeedsDisplay:YES];
    }
}

- (BOOL)isFlipped
{
    return YES;
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
    [name drawAtPoint:NSMakePoint(654, y - 2) withAttributes:labelAttrs];
    NSRect track = NSMakeRect(750, y + 1, 150, 9);
    [strip setFill];
    NSRectFill(track);
    [grid setStroke];
    NSFrameRect(track);

    norm = std::clamp(norm, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
    NSRect filled = NSInsetRect(track, 1.0, 1.0);
    filled.size.width = std::max<CGFloat>(1.0, filled.size.width * norm);
    [fill setFill];
    NSRectFill(filled);

    const CGFloat handleX = std::clamp(track.origin.x + track.size.width * norm - 1.5,
                                       track.origin.x + 1.0,
                                       track.origin.x + track.size.width - 4.0);
    [text setFill];
    NSRectFill(NSMakeRect(handleX, track.origin.y - 2.0, 3.0, track.size.height + 4.0));
    [value drawAtPoint:NSMakePoint(920, y - 2) withAttributes:valueAttrs];
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
    [name drawAtPoint:NSMakePoint(654, y - 2) withAttributes:labelAttrs];
    NSRect box = NSMakeRect(750, y - 1, 178, 15);
    [strip setFill];
    NSRectFill(box);
    [grid setStroke];
    NSFrameRect(box);
    [fill setFill];
    NSRectFill(NSMakeRect(box.origin.x + 1, box.origin.y + 1, 2, box.size.height - 2));
    [value drawAtPoint:NSMakePoint(box.origin.x + 8, y + 1) withAttributes:valueAttrs];
    [@"v" drawAtPoint:NSMakePoint(box.origin.x + box.size.width - 12, y) withAttributes:valueAttrs];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    NSColor* bg = s3gTapeColor(0x0c0c0c);
    NSColor* strip = s3gTapeColor(0x131313);
    NSColor* cellBg = s3gTapeColor(0x1d1d1d);
    NSColor* grid = s3gTapeColor(0x636363);
    NSColor* dim = s3gTapeColor(0x9e9e9e);
    NSColor* text = s3gTapeColor(0xf0f0f0);
    NSColor* accent = s3gTapeColor(0xd1d1d1);
    NSColor* fillColor = s3gTapeColor(0x8f8f8f);

    [bg setFill];
    NSRectFill([self bounds]);

    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSFont* monoBold = [NSFont fontWithName:@"Menlo-Bold" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightBold];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10.5] ?: [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    NSDictionary* labelAttrs = @{ NSForegroundColorAttributeName: text, NSFontAttributeName: monoBold };
    NSDictionary* smallAttrs = @{ NSForegroundColorAttributeName: dim, NSFontAttributeName: mono };
    NSDictionary* sectionAttrs = @{ NSForegroundColorAttributeName: accent, NSFontAttributeName: monoBold };
    NSDictionary* titleAttrs = @{ NSForegroundColorAttributeName: text, NSFontAttributeName: titleFont };

    [@"s3g DELAY PROCESSOR" drawAtPoint:NSMakePoint(18, 13) withAttributes:titleAttrs];
    NSString* channelText = [NSString stringWithFormat:@"%uCH", kChannelCount];
    [channelText drawAtPoint:NSMakePoint(946, 14) withAttributes:smallAttrs];
    const float peak = p->outputPeak.load(std::memory_order_relaxed);
    const bool clipped = p->outputClip.exchange(false, std::memory_order_relaxed);
    const double peakDb = 20.0 * std::log10(std::max(0.000001f, peak));
    NSString* meterText = clipped ? [NSString stringWithFormat:@"PK %+4.1f CLIP", peakDb]
                                  : [NSString stringWithFormat:@"PK %+4.1f", peakDb];
    [meterText drawAtPoint:NSMakePoint(808, 14) withAttributes:clipped ? labelAttrs : smallAttrs];

    NSRect topologyPanel = NSMakeRect(12, 34, 620, 654);
    [cellBg setFill];
    NSRectFill(topologyPanel);
    [grid setStroke];
    NSFrameRect(topologyPanel);
    [strip setFill];
    NSRectFill(NSMakeRect(12, 34, 620, 21));
    [accent setFill];
    NSRectFill(NSMakeRect(12, 34, 620, 2));
    [@"TOPOLOGY" drawAtPoint:NSMakePoint(18, 39) withAttributes:sectionAttrs];
    const char* viewNames[] = { "TOP", "SIDE", "3/4" };
    for (uint32_t i = 0; i < 3; ++i) {
        NSRect viewRect = NSMakeRect(478 + i * 48, 38, 42, 14);
        [strip setFill];
        NSRectFill(viewRect);
        [grid setStroke];
        NSFrameRect(viewRect);
        [[NSString stringWithUTF8String:viewNames[i]] drawAtPoint:NSMakePoint(viewRect.origin.x + 8, viewRect.origin.y + 1) withAttributes:smallAttrs];
    }

    const CGFloat fieldX = 22.0;
    const CGFloat fieldY = 62.0;
    const CGFloat fieldW = 600.0;
    const CGFloat fieldH = 600.0;
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

    const uint32_t visualLanes = std::min<uint32_t>(kChannelCount, activePatchRows(*p));
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

    const NSPoint centroidPoint = projectTopology(centroidX, centroidY, centroidZ);
    [s3gTapeColor(0xd8d8d8) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(centroidPoint.x - 6, centroidPoint.y)
                              toPoint:NSMakePoint(centroidPoint.x + 6, centroidPoint.y)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(centroidPoint.x, centroidPoint.y - 6)
                              toPoint:NSMakePoint(centroidPoint.x, centroidPoint.y + 6)];

    for (uint32_t lane = 0; lane < visualLanes; ++lane) {
        const CGFloat r = 5.0;
        [accent setFill];
        NSRectFill(NSMakeRect(nodePoints[lane].x - r, nodePoints[lane].y - r, r * 2.0, r * 2.0));
        NSString* label = [NSString stringWithFormat:@"%u", lane + 1];
        [label drawAtPoint:NSMakePoint(nodePoints[lane].x + 7, nodePoints[lane].y - 8) withAttributes:smallAttrs];
    }

    NSString* topologyName = visualLanes == 8 ? @"8PT NEIGHBOR MAP"
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
    std::array<uint32_t, kChannelCount> activePins {};
    uint32_t activePinCount = 0;
    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        if (p->patch.rowMask(lane) != 0) {
            activePins[activePinCount++] = lane;
        }
    }
    if (activePinCount == 0) {
        for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
            activePins[activePinCount++] = lane;
        }
    }
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
                                        resolvedChannelDelayMs(*p, lane, visualLanes),
                                        resolvedChannelFeedback(*p, lane, visualLanes),
                                        resolvedChannelCharacter(*p, lane, visualLanes),
                                        resolvedChannelNetwork(*p, lane, visualLanes),
                                        resolvedChannelSmearAmount(*p, lane, visualLanes)];
            [line drawAtPoint:NSMakePoint(fieldX + fieldW - 188, fieldY + 70 + row * 15.0) withAttributes:smallAttrs];
        }
    } else {
        [@"LST" drawAtPoint:NSMakePoint(readoutButton.origin.x + 6, readoutButton.origin.y + 1) withAttributes:smallAttrs];
    }

    const CGFloat panelX = 644.0;
    const CGFloat panelW = 344.0;
    const CGFloat headerH = 21.0;
    const CGFloat gap = 8.0;
    CGFloat panelY = 34.0;
    auto drawHeader = [&](NSString* title, bool open, CGFloat y) {
        [strip setFill];
        NSRectFill(NSMakeRect(panelX, y, panelW, headerH));
        [accent setFill];
        NSRectFill(NSMakeRect(panelX, y, panelW, 2));
        NSString* marker = open ? @"-" : @"+";
        [marker drawAtPoint:NSMakePoint(panelX + 8, y + 5) withAttributes:sectionAttrs];
        [title drawAtPoint:NSMakePoint(panelX + 24, y + 5) withAttributes:sectionAttrs];
    };
    auto drawPanelFrame = [&](CGFloat y, CGFloat h) {
        [cellBg setFill];
        NSRectFill(NSMakeRect(panelX, y, panelW, h));
        [grid setStroke];
        NSFrameRect(NSMakeRect(panelX, y, panelW, h));
    };

    const CGFloat engineH = _showEngine ? 182.0 : headerH;
    drawPanelFrame(panelY, engineH);
    drawHeader(@"ENGINE", _showEngine, panelY);
    if (_showEngine) {
        [self drawSlider:@"TIME"
                   value:[NSString stringWithFormat:@"%4.0f", p->delayMs]
                    norm:static_cast<CGFloat>((p->delayMs - kDelayMinMs) / (kDelayMaxMs - kDelayMinMs))
                       y:panelY + 30.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"FDBK"
                   value:[NSString stringWithFormat:@"%3.0f%%", (p->feedback / 0.95) * 100.0]
                    norm:static_cast<CGFloat>(p->feedback / 0.95)
                       y:panelY + 48.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"MIX"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->mix * 100.0]
                    norm:static_cast<CGFloat>(p->mix)
                       y:panelY + 66.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"TONE"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->tone * 100.0]
                    norm:static_cast<CGFloat>(p->tone)
                       y:panelY + 84.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"PITCH"
                   value:[NSString stringWithFormat:@"%+4.1f", p->pitchSemitones]
                    norm:static_cast<CGFloat>((p->pitchSemitones - kPitchMinSemitones) / (kPitchMaxSemitones - kPitchMinSemitones))
                       y:panelY + 102.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"CHAR"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->character * 100.0]
                    norm:static_cast<CGFloat>(p->character)
                       y:panelY + 120.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"SMR"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->tapAmount * 100.0]
                    norm:static_cast<CGFloat>(p->tapAmount)
                       y:panelY + 138.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"OUT"
                   value:[NSString stringWithFormat:@"%+4.1f", p->outputTrimDb]
                    norm:static_cast<CGFloat>((p->outputTrimDb - kOutputTrimMinDb) / (kOutputTrimMaxDb - kOutputTrimMinDb))
                       y:panelY + 156.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    }
    panelY += engineH + gap;

    const CGFloat metaH = _showMeta ? 328.0 : headerH;
    drawPanelFrame(panelY, metaH);
    drawHeader(@"TOPOLOGY", _showMeta, panelY);
    NSRect resetRect = NSMakeRect(924, panelY + 4, 54, 15);
    [strip setFill];
    NSRectFill(resetRect);
    [grid setStroke];
    NSFrameRect(resetRect);
    [@"RESET" drawAtPoint:NSMakePoint(933, panelY + 6) withAttributes:smallAttrs];
    if (_showMeta) {
        [self drawMenuControl:@"SHAP"
                         value:[NSString stringWithUTF8String:topologyShapeName(p->topologyShape)]
                             y:panelY + 30.0
                    labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"AMT"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->topologySpread * 100.0]
                    norm:static_cast<CGFloat>(p->topologySpread)
                       y:panelY + 48.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"PULL"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->displaceCollapse * 100.0]
                    norm:static_cast<CGFloat>(p->displaceCollapse)
                       y:panelY + 66.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"X"
                   value:[NSString stringWithFormat:@"%+3.0f%%", p->displaceDirX * 100.0]
                    norm:static_cast<CGFloat>((p->displaceDirX + 1.0) * 0.5)
                       y:panelY + 84.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"Y"
                   value:[NSString stringWithFormat:@"%+3.0f%%", p->displaceDirY * 100.0]
                    norm:static_cast<CGFloat>((p->displaceDirY + 1.0) * 0.5)
                       y:panelY + 102.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"Z"
                   value:[NSString stringWithFormat:@"%+3.0f%%", p->displaceDirZ * 100.0]
                    norm:static_cast<CGFloat>((p->displaceDirZ + 1.0) * 0.5)
                       y:panelY + 120.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"TWST"
                   value:[NSString stringWithFormat:@"%+3.0f%%", p->displaceTwist * 100.0]
                    norm:static_cast<CGFloat>((p->displaceTwist + 1.0) * 0.5)
                       y:panelY + 138.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"FLAR"
                   value:[NSString stringWithFormat:@"%+3.0f%%", p->displaceFlare * 100.0]
                    norm:static_cast<CGFloat>((p->displaceFlare + 1.0) * 0.5)
                       y:panelY + 156.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"SEED"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->topologyJitter * 100.0]
                    norm:static_cast<CGFloat>(p->topologyJitter)
                       y:panelY + 174.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawMenuControl:@"ANIM"
                         value:[NSString stringWithUTF8String:topologyMotionModeName(p->topologyMotionMode)]
                             y:panelY + 192.0
                    labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawMenuControl:@"VAR"
                         value:[NSString stringWithUTF8String:topologyVariantName(p->topologyMotionVariant)]
                             y:panelY + 210.0
                    labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"RATE"
                   value:[NSString stringWithFormat:@"%4.2f", p->topologyMotionRateHz]
                    norm:static_cast<CGFloat>((p->topologyMotionRateHz - kTopologyMotionMinHz) / (kTopologyMotionMaxHz - kTopologyMotionMinHz))
                       y:panelY + 228.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"DPTH"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->topologyMotionDepth * 100.0]
                    norm:static_cast<CGFloat>(p->topologyMotionDepth)
                       y:panelY + 246.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawMenuControl:@"NBR"
                         value:[NSString stringWithFormat:@"%uNN", p->topologyNeighborCount]
                             y:panelY + 264.0
                    labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"RAD"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->topologyRadius * 100.0]
                    norm:static_cast<CGFloat>(p->topologyRadius)
                       y:panelY + 282.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
        [self drawSlider:@"CENT"
                   value:[NSString stringWithFormat:@"%3.0f%%", p->topologyCentroid * 100.0]
                    norm:static_cast<CGFloat>(p->topologyCentroid)
                       y:panelY + 300.0
              labelAttrs:smallAttrs valueAttrs:smallAttrs strip:strip grid:grid fill:fillColor text:text];
    }
    panelY += metaH + gap;

    const CGFloat left = 718.0;
    const CGFloat cell = 24.0;
    const CGFloat rowLabelX = 654.0;
    const CGFloat matrixTopPad = 42.0;
    const CGFloat matrixH = _showMatrix ? 248.0 : headerH;
    drawPanelFrame(panelY, matrixH);
    NSString* matrixTitle = kChannelCount > kVisiblePatchChannels
        ? [NSString stringWithFormat:@"PATCH MATRIX 1-%u", kVisiblePatchChannels]
        : @"PATCH MATRIX";
    drawHeader(matrixTitle, _showMatrix, panelY);
    if (_showMatrix) {
        const CGFloat top = panelY + matrixTopPad;
        for (uint32_t i = 0; i < kVisiblePatchChannels; ++i) {
            NSString* outLabel = [NSString stringWithFormat:@"%u", i + 1];
            [outLabel drawAtPoint:NSMakePoint(left + i * cell + 8, top - 18) withAttributes:smallAttrs];
            NSString* inLabel = [NSString stringWithFormat:@"I%u", i + 1];
            [inLabel drawAtPoint:NSMakePoint(rowLabelX, top + i * cell + 6) withAttributes:smallAttrs];
        }

        for (uint32_t in = 0; in < kVisiblePatchChannels; ++in) {
            for (uint32_t out = 0; out < kVisiblePatchChannels; ++out) {
                const bool connected = p->patch.connected(in, out);
                NSRect r = NSMakeRect(left + out * cell, top + in * cell, cell - 4, cell - 4);
                [strip setFill];
                NSRectFill(r);
                [grid setStroke];
                NSFrameRect(r);
                if (connected) {
                    [accent setFill];
                    NSRectFill(NSInsetRect(r, 5, 5));
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
            if (menuSelected(i)) {
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
}

- (void)resetTopology
{
    auto* p = static_cast<Plugin*>(_plugin);
    p->topologyShape = 0;
    p->topologySpread = 0.0;
    p->topologySkew = 0.0;
    p->topologyJitter = 0.0;
    p->displaceCollapse = 0.0;
    p->displaceDirX = 0.0;
    p->displaceDirY = 0.0;
    p->displaceDirZ = 1.0;
    p->displaceTwist = 0.0;
    p->displaceFlare = 0.0;
    p->topologyMotionMode = 0;
    p->topologyMotionVariant = 0;
    p->topologyMotionRateHz = 0.10;
    p->topologyMotionDepth = 0.0;
    p->topologyMotionPhase = 0.0;
    p->topologyNeighborCount = 2;
    p->topologyRadius = 0.65;
    p->topologyCentroid = 0.22;
    _viewYaw = -0.52;
    _viewPitch = 0.34;
    applyParamsToDsp(*p);
    [self setNeedsDisplay:YES];
}

- (void)setTopologyView:(uint32_t)view
{
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

- (void)selectShapeMenuItem:(NSMenuItem*)item
{
    auto* p = static_cast<Plugin*>(_plugin);
    p->topologyShape = std::min<uint32_t>(kTopologyShapeCount - 1u, static_cast<uint32_t>([item tag]));
    applyParamsToDsp(*p);
    [self setNeedsDisplay:YES];
}

- (void)selectMotionMenuItem:(NSMenuItem*)item
{
    auto* p = static_cast<Plugin*>(_plugin);
    applyTopologyMotionSceneDefaults(*p, static_cast<uint32_t>([item tag]));
    applyParamsToDsp(*p);
    [self setNeedsDisplay:YES];
}

- (void)selectNeighborMenuItem:(NSMenuItem*)item
{
    auto* p = static_cast<Plugin*>(_plugin);
    p->topologyNeighborCount = std::clamp<uint32_t>(static_cast<uint32_t>([item tag]), 1u, 3u);
    applyParamsToDsp(*p);
    [self setNeedsDisplay:YES];
}

- (void)updateSliderAtPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider < 0 || _dragSlider > 22) {
        return;
    }
    const double norm = std::clamp((pt.x - 750.0) / 150.0, 0.0, 1.0);
    if (_dragSlider == 0) {
        p->delayMs = kDelayMinMs + norm * (kDelayMaxMs - kDelayMinMs);
    } else if (_dragSlider == 1) {
        p->feedback = norm * 0.95;
    } else if (_dragSlider == 2) {
        p->mix = norm;
    } else if (_dragSlider == 3) {
        p->tone = norm;
    } else if (_dragSlider == 4) {
        p->pitchSemitones = kPitchMinSemitones + norm * (kPitchMaxSemitones - kPitchMinSemitones);
    } else if (_dragSlider == 5) {
        p->character = norm;
    } else if (_dragSlider == 6) {
        p->tapAmount = norm;
    } else if (_dragSlider == 7) {
        p->outputTrimDb = kOutputTrimMinDb + norm * (kOutputTrimMaxDb - kOutputTrimMinDb);
    } else if (_dragSlider == 8) {
        p->topologyShape = std::min<uint32_t>(kTopologyShapeCount - 1u, roundedUint(norm * static_cast<double>(kTopologyShapeCount - 1u)));
    } else if (_dragSlider == 9) {
        p->topologySpread = norm;
    } else if (_dragSlider == 10) {
        p->displaceCollapse = norm;
    } else if (_dragSlider == 11) {
        p->displaceDirX = norm * 2.0 - 1.0;
    } else if (_dragSlider == 12) {
        p->displaceDirY = norm * 2.0 - 1.0;
    } else if (_dragSlider == 13) {
        p->displaceDirZ = norm * 2.0 - 1.0;
    } else if (_dragSlider == 14) {
        p->displaceTwist = norm * 2.0 - 1.0;
    } else if (_dragSlider == 15) {
        p->displaceFlare = norm * 2.0 - 1.0;
    } else if (_dragSlider == 16) {
        p->topologyJitter = norm;
    } else if (_dragSlider == 17) {
        p->topologyMotionMode = std::min<uint32_t>(kTopologyMotionModeCount - 1u, roundedUint(norm * static_cast<double>(kTopologyMotionModeCount - 1u)));
        if (p->topologyMotionMode == 0u) {
            p->topologyMotionPhase = 0.0;
        }
    } else if (_dragSlider == 18) {
        p->topologyMotionRateHz = kTopologyMotionMinHz + norm * (kTopologyMotionMaxHz - kTopologyMotionMinHz);
    } else if (_dragSlider == 19) {
        p->topologyMotionDepth = norm;
    } else if (_dragSlider == 20) {
        p->topologyNeighborCount = std::clamp<uint32_t>(1u + roundedUint(norm * 2.0), 1u, 3u);
    } else if (_dragSlider == 21) {
        p->topologyRadius = norm;
    } else if (_dragSlider == 22) {
        p->topologyCentroid = norm;
    }
    applyParamsToDsp(*p);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);

    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 178.0, itemH * static_cast<CGFloat>(_menuItemCount));
        if (NSPointInRect(pt, menuRect)) {
            const uint32_t index = std::min<uint32_t>(_menuItemCount - 1u, static_cast<uint32_t>((pt.y - _menuOrigin.y) / itemH));
            if (_openMenu == 1) {
                p->topologyShape = std::min<uint32_t>(kTopologyShapeCount - 1u, index);
            } else if (_openMenu == 2) {
                applyTopologyMotionSceneDefaults(*p, index);
            } else if (_openMenu == 4) {
                p->topologyMotionVariant = std::min<uint32_t>(kTopologyVariantCount - 1u, index);
            } else if (_openMenu == 3) {
                p->topologyNeighborCount = std::clamp<uint32_t>(index + 1u, 1u, 3u);
            }
            applyParamsToDsp(*p);
        }
        _openMenu = 0;
        _menuItemCount = 0;
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t i = 0; i < 3; ++i) {
        if (NSPointInRect(pt, NSMakeRect(478 + i * 48, 38, 42, 14))) {
            [self setTopologyView:i];
            return;
        }
    }

    const CGFloat panelX = 644.0;
    const CGFloat panelW = 344.0;
    const CGFloat headerH = 21.0;
    const CGFloat gap = 8.0;
    CGFloat panelY = 34.0;
    auto headerRect = [&](CGFloat y) {
        return NSMakeRect(panelX, y, panelW, headerH);
    };
    auto menuOrigin = [&](CGFloat x, CGFloat preferredY, uint32_t itemCount) {
        const CGFloat itemH = 18.0;
        const CGFloat bottom = 690.0;
        return NSMakePoint(x, std::max<CGFloat>(28.0, std::min<CGFloat>(preferredY, bottom - itemH * static_cast<CGFloat>(itemCount))));
    };

    const CGFloat engineH = _showEngine ? 182.0 : headerH;
    if (NSPointInRect(pt, headerRect(panelY))) {
        _showEngine = !_showEngine;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_showEngine) {
        for (uint32_t i = 0; i < 8; ++i) {
            NSRect r = NSMakeRect(650, panelY + 24.0 + i * 18.0, 330, 20);
            if (NSPointInRect(pt, r)) {
                _dragSlider = static_cast<int>(i);
                [self updateSliderAtPoint:pt];
                return;
            }
        }
    }
    panelY += engineH + gap;

    const CGFloat metaH = _showMeta ? 328.0 : headerH;
    if (NSPointInRect(pt, NSMakeRect(924, panelY + 4, 54, 15))) {
        [self resetTopology];
        return;
    }
    if (NSPointInRect(pt, headerRect(panelY))) {
        _showMeta = !_showMeta;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_showMeta) {
        NSRect shapeRow = NSMakeRect(650, panelY + 24.0, 330, 20);
        NSRect motionRow = NSMakeRect(650, panelY + 24.0 + 9.0 * 18.0, 330, 20);
        NSRect variantRow = NSMakeRect(650, panelY + 24.0 + 10.0 * 18.0, 330, 20);
        NSRect neighborRow = NSMakeRect(650, panelY + 24.0 + 13.0 * 18.0, 330, 20);
        if (NSPointInRect(pt, shapeRow)) {
            _openMenu = 1;
            _menuItemCount = kTopologyShapeCount;
            _menuOrigin = menuOrigin(750, panelY + 48.0, _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(pt, motionRow)) {
            _openMenu = 2;
            _menuItemCount = kTopologyMotionModeCount;
            _menuOrigin = menuOrigin(750, panelY + 210.0, _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(pt, variantRow)) {
            _openMenu = 4;
            _menuItemCount = kTopologyVariantCount;
            _menuOrigin = menuOrigin(750, panelY + 228.0, _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(pt, neighborRow)) {
            _openMenu = 3;
            _menuItemCount = 3;
            _menuOrigin = menuOrigin(750, panelY + 282.0, _menuItemCount);
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t i = 0; i < 16; ++i) {
            if (i == 0 || i == 9 || i == 10 || i == 13) {
                continue;
            }
            NSRect r = NSMakeRect(650, panelY + 24.0 + i * 18.0, 330, 20);
            if (NSPointInRect(pt, r)) {
                if (i < 9) {
                    _dragSlider = static_cast<int>(i + 8);
                } else if (i == 11) {
                    _dragSlider = 18;
                } else if (i == 12) {
                    _dragSlider = 19;
                } else if (i == 14) {
                    _dragSlider = 21;
                } else if (i == 15) {
                    _dragSlider = 22;
                } else {
                    continue;
                }
                [self updateSliderAtPoint:pt];
                return;
            }
        }
    }
    panelY += metaH + gap;

    const CGFloat matrixH = _showMatrix ? 248.0 : headerH;
    if (NSPointInRect(pt, headerRect(panelY))) {
        _showMatrix = !_showMatrix;
        if (_showMatrix) {
            _showEngine = false;
            _showMeta = false;
        } else {
            _showEngine = true;
            _showMeta = true;
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (_showMatrix) {
        const CGFloat left = 718.0;
        const CGFloat top = panelY + 42.0;
        const CGFloat cell = 24.0;
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

    NSRect topologyView = NSMakeRect(22.0, 62.0, 600.0, 600.0);
    if (NSPointInRect(pt, topologyView)) {
        _dragTopologyView = true;
        _lastDragPoint = pt;
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragTopologyView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewYaw += dx * 0.015;
        _viewPitch = std::clamp(_viewPitch + dy * 0.012, -0.75, 0.95);
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
    return p->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
        if ([view respondsToSelector:@selector(stopRefreshTimer)]) {
            [static_cast<S3GDelayProcessorView*>(view) stopRefreshTimer];
        }
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) {
        return false;
    }
    *width = 1000;
    *height = 700;
    return true;
}

bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    NSView* view = static_cast<NSView*>(p->guiView);
    [view setFrameSize:NSMakeSize(width, height)];
    return true;
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, 1000, 700)];
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    [static_cast<NSView*>(p->guiView) setHidden:NO];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    [static_cast<NSView*>(p->guiView) setHidden:YES];
    return true;
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

    p->delay.prepare(48000.0, static_cast<int>(kChannelCount), 2.25);
    preparePatch(*p);
    applyParamsToDsp(*p);
    p->host = host;
    p->hostTail = host && host->get_extension ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL)) : nullptr;
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
