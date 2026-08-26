#include "s3g_realtime.h"
#include "s3g_ring_output_mixdown.h"
#include "s3g_sample_asset.h"
#include "s3g_sample_rings.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_sample_storage.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

using s3g::sample::SampleRingsHeadFormation;
using s3g::sample::SampleRingsRelationship;
using s3g::sample::SampleRingsSlotSettings;
using s3g::sample::SampleRingsRingPath;
using s3g::sample::SampleAsset;
using s3g::sample::SampleRingsEngine;
using s3g::sample::SampleRingsSettings;
using s3g::sample_storage::ProjectCopyResult;
using s3g::sample_storage::ProjectFileRegistration;
using s3g::sample_storage::ProjectLocation;
using s3g::sample_storage::ReaperContext;
using s3g::sample_storage::StorageMode;

constexpr uint32_t kChannelCount = 8u;
constexpr uint32_t kSourceCount = 4u;
constexpr uint32_t kHeadCount = 8u;
constexpr uint32_t kStateMagic = 0x534c4d58u; // SLMX
constexpr uint32_t kStateVersion = 6u;
constexpr uint32_t kPreviousStateVersion = 5u;
constexpr std::size_t kCaptureSampleCapacity = 6u * 1024u * 1024u;
constexpr uint64_t kMaximumEmbeddedAudioBytes = 1024ull * 1024ull * 1024ull;

constexpr uint32_t kGuiW = 1356u;
constexpr uint32_t kGuiH = 820u;

constexpr clap_id kPlaybackRateParamId = 1u;
constexpr clap_id kRingPathParamId = 2u;
constexpr clap_id kRelationshipParamId = 3u;
constexpr clap_id kRelationshipAmountParamId = 4u;
constexpr clap_id kDriftParamId = 5u;
constexpr clap_id kCenterParamId = 6u;
constexpr clap_id kGlideParamId = 7u;
constexpr clap_id kRingBlendParamId = 8u;
constexpr clap_id kFormationParamId = 9u;
constexpr clap_id kLoopJoinParamId = 10u;
constexpr clap_id kSeamDuckParamId = 11u;
constexpr clap_id kHeadMaskParamId = 12u;
constexpr clap_id kOutputGainParamId = 13u;
constexpr clap_id kOutputFormatParamId = 14u;
constexpr clap_id kOutputRotationParamId = 15u;
constexpr clap_id kPlayingParamId = 16u;
constexpr clap_id kCaptureTargetParamId = 17u;
constexpr clap_id kCaptureGateParamId = 18u;
constexpr clap_id kCaptureModeParamId = 19u;
constexpr clap_id kInputWidthParamId = 20u;
constexpr clap_id kCaptureQuantizeParamId = 21u;
constexpr clap_id kMonitorParamId = 22u;
constexpr clap_id kSelectedSlotParamId = 23u;
constexpr clap_id kMidiModeParamId = 24u;
constexpr clap_id kMidiRootParamId = 25u;
constexpr clap_id kOverdubFeedbackParamId = 26u;
constexpr clap_id kSeedParamId = 27u;
constexpr clap_id kRingPositionParamId = 28u;
constexpr clap_id kRadialRatioParamId = 29u;
constexpr clap_id kPathDepthParamId = 30u;
constexpr clap_id kPathOffsetParamId = 31u;
constexpr clap_id kPathSpreadParamId = 32u;
constexpr clap_id kPathSlewParamId = 33u;
constexpr clap_id kRadialReverseParamId = 34u;
constexpr clap_id kAngularReverseParamId = 35u;

constexpr clap_id kSlotParamBase = 100u;
constexpr clap_id kSlotParamStride = 16u;
constexpr clap_id kManualRingParamBase = 200u;
constexpr clap_id kManualPhaseParamBase = 220u;
constexpr clap_id kManualRateParamBase = 240u;

enum class CaptureMode : uint32_t { Replace = 0u, Overdub, Punch };
enum class CaptureQuantize : uint32_t {
    Free = 0u, Beat, Bar, TwoBars, FourBars, EightBars,
};
enum class MonitorMode : uint32_t { Loop = 0u, Input, Both };
enum class TransportCommand : uint32_t { None = 0u, Relaunch, Stop };

enum SlotParamOffset : clap_id {
    kSlotStart = 0u,
    kSlotEnd = 1u,
    kSlotSpeed = 2u,
    kSlotStretch = 3u,
    kSlotPitch = 4u,
    kSlotNudge = 5u,
    kSlotGain = 6u,
    kSlotReverse = 7u,
};

constexpr clap_id slotParamId(uint32_t slot, clap_id offset)
{ return kSlotParamBase + slot * kSlotParamStride + offset; }

bool decodeSlotParam(clap_id id, uint32_t& slot, clap_id& offset)
{
    if (id < kSlotParamBase
        || id >= kSlotParamBase + kSourceCount * kSlotParamStride)
        return false;
    const clap_id relative = id - kSlotParamBase;
    slot = relative / kSlotParamStride;
    offset = relative % kSlotParamStride;
    return slot < kSourceCount && offset <= kSlotReverse;
}

struct SlotTargets {
    std::atomic<float> start { 0.0f };
    std::atomic<float> end { 1.0f };
    std::atomic<float> speed { 1.0f };
    std::atomic<float> stretch { 1.0f };
    std::atomic<float> pitch { 0.0f };
    std::atomic<float> nudge { 0.0f };
    std::atomic<float> gain { -6.0f };
    std::atomic<uint32_t> reverse { 0u };
};

struct ParameterTargets {
    std::atomic<float> playbackRate { 1.0f };
    std::atomic<uint32_t> ringPath {
        static_cast<uint32_t>(SampleRingsRingPath::Bounce) };
    std::atomic<uint32_t> relationship {
        static_cast<uint32_t>(SampleRingsRelationship::Canon) };
    std::atomic<float> relationshipAmount { 0.5f };
    std::atomic<float> drift { 0.0f };
    std::atomic<float> center { 0.5f };
    std::atomic<float> glideMilliseconds { 180.0f };
    std::atomic<float> ringBlend { 1.0f };
    std::atomic<uint32_t> formation {
        static_cast<uint32_t>(SampleRingsHeadFormation::Field8) };
    std::atomic<float> ringPosition { 0.5f };
    std::atomic<float> radialRatio { 1.0f };
    std::atomic<float> pathDepth { 1.0f };
    std::atomic<float> pathOffset { 0.0f };
    std::atomic<float> pathSpread { 0.0f };
    std::atomic<float> pathSlewMilliseconds { 80.0f };
    std::atomic<uint32_t> radialReverse { 0u };
    std::atomic<uint32_t> angularReverse { 0u };
    std::atomic<float> loopJoin { 0.04f };
    std::atomic<float> seamDuck { 0.0f };
    std::atomic<uint32_t> headMask { 0xffu };
    std::atomic<float> outputGain { -9.0f };
    std::atomic<uint32_t> outputFormat { 0u };
    std::atomic<float> outputRotation { 0.0f };
    std::atomic<uint32_t> playing { 1u };
    std::atomic<uint32_t> captureTarget { 0u };
    std::atomic<uint32_t> captureGate { 0u };
    std::atomic<uint32_t> captureMode { 0u };
    std::atomic<uint32_t> inputWidth { 0u };
    std::atomic<uint32_t> captureQuantize { 0u };
    std::atomic<uint32_t> monitor { 0u };
    std::atomic<uint32_t> selectedSlot { 0u };
    std::atomic<uint32_t> midiMode { 0u };
    std::atomic<float> midiRoot { 60.0f };
    std::atomic<float> overdubFeedback { 0.75f };
    std::atomic<uint32_t> seed { 4312u };
    std::array<SlotTargets, kSourceCount> slots {};
    std::array<std::atomic<float>, kHeadCount> manualRings {};
    std::array<std::atomic<float>, kHeadCount> manualPhases {};
    std::array<std::atomic<float>, kHeadCount> manualRates {};

    ParameterTargets()
    {
        for (uint32_t head = 0u; head < kHeadCount; ++head) {
            manualRings[head].store(static_cast<float>(head)
                / static_cast<float>(kHeadCount - 1u));
            manualPhases[head].store(static_cast<float>(head)
                / static_cast<float>(kHeadCount));
            manualRates[head].store(1.0f);
        }
    }
};

struct CaptureBuffer {
    std::vector<float> samples;
    uint32_t channels = 0u;
    uint32_t frames = 0u;
    uint32_t writeFrame = 0u;
    uint32_t baseFrames = 0u;
    CaptureMode mode = CaptureMode::Replace;
    std::atomic<bool> recording { false };
    bool pendingStart = false;
    bool pendingStop = false;
    std::atomic<bool> prepared { false };
    std::atomic<bool> ready { false };
    std::atomic<uint32_t> readyFrames { 0u };
    std::atomic<uint32_t> readyChannels { 0u };
};

#if defined(__APPLE__)
struct LoadRequest {
    uint64_t generation = 0u;
    uint8_t slot = 0u;
    std::string path;
    ProjectLocation projectLocation;
    std::string projectError;
    bool copyOnly = false;
};

struct LoadResult {
    uint64_t generation = 0u;
    uint8_t slot = 0u;
    std::string sourcePath;
    std::string decodedPath;
    std::shared_ptr<const SampleAsset> asset;
    std::string error;
    ProjectCopyResult projectCopy;
    uint64_t sourceFileBytes = 0u;
    bool copyOnly = false;
};
#endif

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    ParameterTargets targets {};
    SampleRingsEngine engine;
    s3g::RingOutputMixdown outputMixdown;
    std::vector<float> monitorScratch;
    std::array<std::shared_ptr<const SampleAsset>, kSourceCount> sources {};
    std::array<std::shared_ptr<const SampleAsset>, kSourceCount>
        audioSources {};
    std::array<std::shared_ptr<const SampleAsset>, kSourceCount>
        audioRetained {};
    std::vector<std::shared_ptr<const SampleAsset>> retiredSources;
    std::array<std::string, kSourceCount> sourcePaths {};
    std::array<std::string, kSourceCount> linkSourcePaths {};
    std::array<std::string, kSourceCount> projectRelativePaths {};
    std::array<std::string, kSourceCount> sourceNames {{
        "EMPTY", "EMPTY", "EMPTY", "EMPTY",
    }};
    std::array<std::string, kSourceCount> sourceStatuses {{
        "EMPTY SLOT", "EMPTY SLOT", "EMPTY SLOT", "EMPTY SLOT",
    }};
    std::array<uint64_t, kSourceCount> sourceFileBytes {};
    std::array<bool, kSourceCount> projectStoragePending {};
    std::array<bool, kSourceCount> projectCopyInFlight {};
    std::array<std::chrono::steady_clock::time_point, kSourceCount>
        nextProjectCopyProbe {};
    StorageMode storageMode = StorageMode::Project;
    ReaperContext reaperContext;
    std::array<ProjectFileRegistration, kSourceCount>
        projectFileRegistrations {};
    std::array<CaptureBuffer, kSourceCount> captures {};
    std::atomic<int32_t> activeCaptureSlot { -1 };
    std::atomic<uint32_t> captureFrames { 0u };
    std::atomic<uint32_t> captureChannels { 0u };
    std::array<std::atomic<float>, kChannelCount> inputPeaks {};
    std::array<std::atomic<float>, kHeadCount> headPhases {};
    std::array<std::atomic<float>, kHeadCount> headPhaseTargets {};
    std::array<std::atomic<float>, kHeadCount> headRates {};
    std::array<std::atomic<uint32_t>, kHeadCount> headRings {};
    std::array<std::atomic<uint32_t>, kHeadCount> headRingTargets {};
    std::array<std::atomic<uint32_t>, kHeadCount> headSlots {};
    std::array<std::atomic<uint32_t>, kHeadCount> headSlotTargets {};
    std::array<std::atomic<uint32_t>, kHeadCount> headChannels {};
    std::array<std::atomic<uint32_t>, kHeadCount> headChannelTargets {};
    std::array<std::atomic<float>, kHeadCount> headRingMixes {};
    std::array<std::atomic<float>, kHeadCount> headRadialPositions {};
    std::array<std::atomic<uint32_t>, kHeadCount> headFormationLeaders {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> heldNotes { 0u };
    std::atomic<float> midiRateScale { 1.0f };
    std::atomic<uint32_t> transportCommand {
        static_cast<uint32_t>(TransportCommand::None) };
    std::atomic<bool> transportStopped { false };
    std::atomic<bool> captureCallbackPending { false };
    std::atomic<bool> processing { false };
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
#if defined(__APPLE__)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoadRequest> loadRequests;
    std::deque<LoadResult> loadResults;
    std::thread loaderThread;
    std::array<uint64_t, kSourceCount> loadGenerations {};
    bool loaderStopping = false;
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{ return static_cast<Plugin*>(plugin->plugin_data); }

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output) noexcept;
void queueGuiParamGesture(Plugin& plugin, clap_id id, double value);
void queueGuiParamBegin(Plugin& plugin, clap_id id);
void queueGuiParamValue(Plugin& plugin, clap_id id, double value);
void queueGuiParamEnd(Plugin& plugin, clap_id id);
void requestTransportCommand(Plugin& plugin,
    TransportCommand command) noexcept;

const char* ringPathName(uint32_t value)
{
    switch (std::min(value, 7u)) {
    case 0u: return "FIXED RING";
    case 1u: return "OUTWARD";
    case 2u: return "INWARD";
    case 3u: return "BOUNCE";
    case 4u: return "SINE SWEEP";
    case 5u: return "STEPPED BANDS";
    case 6u: return "RANDOM WALK";
    default: return "MANUAL";
    }
}

const char* relationshipName(uint32_t value)
{
    switch (std::min(value, 7u)) {
    case 0u: return "UNISON";
    case 1u: return "CANON";
    case 2u: return "FAN";
    case 3u: return "RATIO";
    case 4u: return "SHEAR";
    case 5u: return "ORBIT";
    case 6u: return "DRIFT";
    default: return "MANUAL";
    }
}

const char* formationName(uint32_t value)
{
    switch (std::min(value, 3u)) {
    case 0u: return "FREE HEADS";
    case 1u: return "LINKED PAIRS";
    case 2u: return "LINKED QUADS";
    default: return "FIELD 8";
    }
}

const char* captureModeName(uint32_t value)
{
    switch (std::min(value, 2u)) {
    case 1u: return "OVERDUB";
    case 2u: return "PUNCH";
    default: return "REPLACE";
    }
}

const char* captureQuantizeName(uint32_t value)
{
    switch (std::min(value, 5u)) {
    case 1u: return "BEAT";
    case 2u: return "BAR";
    case 3u: return "2 BARS";
    case 4u: return "4 BARS";
    case 5u: return "8 BARS";
    default: return "FREE";
    }
}

const char* monitorName(uint32_t value)
{
    switch (std::min(value, 2u)) {
    case 1u: return "INPUT";
    case 2u: return "BOTH";
    default: return "LOOP";
    }
}

const char* midiModeName(uint32_t value)
{
    switch (std::min(value, 2u)) {
    case 1u: return "GATE";
    case 2u: return "TRIGGER";
    default: return "OFF";
    }
}

SampleRingsSettings snapshotSettings(const Plugin& plugin)
{
    SampleRingsSettings settings;
    settings.playbackRate = plugin.targets.playbackRate.load(std::memory_order_acquire)
        * plugin.midiRateScale.load(std::memory_order_acquire);
    settings.ringPath = static_cast<SampleRingsRingPath>(
        plugin.targets.ringPath.load(std::memory_order_acquire));
    settings.relationship = static_cast<SampleRingsRelationship>(
        plugin.targets.relationship.load(std::memory_order_acquire));
    settings.relationshipAmount = plugin.targets.relationshipAmount.load(
        std::memory_order_acquire);
    settings.driftAmount = plugin.targets.drift.load(
        std::memory_order_acquire);
    settings.relationshipCenter = plugin.targets.center.load(
        std::memory_order_acquire);
    settings.relationshipGlideMilliseconds
        = plugin.targets.glideMilliseconds.load(std::memory_order_acquire);
    settings.ringBlend = plugin.targets.ringBlend.load(
        std::memory_order_acquire);
    settings.formation = static_cast<SampleRingsHeadFormation>(
        plugin.targets.formation.load(std::memory_order_acquire));
    settings.ringPosition = plugin.targets.ringPosition.load(
        std::memory_order_acquire);
    settings.radialRatio = plugin.targets.radialRatio.load(
        std::memory_order_acquire);
    settings.pathDepth = plugin.targets.pathDepth.load(
        std::memory_order_acquire);
    settings.pathOffset = plugin.targets.pathOffset.load(
        std::memory_order_acquire);
    settings.pathSpread = plugin.targets.pathSpread.load(
        std::memory_order_acquire);
    settings.pathSlewMilliseconds = plugin.targets.pathSlewMilliseconds.load(
        std::memory_order_acquire);
    settings.reverseRadialPath = plugin.targets.radialReverse.load(
        std::memory_order_acquire) != 0u;
    settings.reverseAngularMotion = plugin.targets.angularReverse.load(
        std::memory_order_acquire) != 0u;
    settings.loopJoin = plugin.targets.loopJoin.load(
        std::memory_order_acquire);
    settings.seamDuck = plugin.targets.seamDuck.load(
        std::memory_order_acquire);
    settings.headMask = plugin.targets.headMask.load(
        std::memory_order_acquire);
    settings.outputGainDecibels = plugin.targets.outputGain.load(
        std::memory_order_acquire);
    settings.seed = plugin.targets.seed.load(std::memory_order_acquire);
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        const auto& source = plugin.targets.slots[slot];
        auto& target = settings.slots[slot];
        target.start = source.start.load(std::memory_order_acquire);
        target.end = source.end.load(std::memory_order_acquire);
        target.speed = source.speed.load(std::memory_order_acquire);
        target.stretch = source.stretch.load(std::memory_order_acquire);
        target.pitchSemitones = source.pitch.load(std::memory_order_acquire);
        target.nudge = source.nudge.load(std::memory_order_acquire);
        target.gainDecibels = source.gain.load(std::memory_order_acquire);
        target.reverse = source.reverse.load(std::memory_order_acquire) != 0u;
    }
    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        settings.manualRings[head] = plugin.targets.manualRings[head].load(
            std::memory_order_acquire);
        settings.manualPhases[head] = plugin.targets.manualPhases[head].load(
            std::memory_order_acquire);
        settings.manualRates[head] = plugin.targets.manualRates[head].load(
            std::memory_order_acquire);
    }
    return settings;
}

void markDirty(Plugin& plugin)
{
    if (plugin.hostState && plugin.hostState->mark_dirty)
        plugin.hostState->mark_dirty(plugin.host);
}

void setParam(Plugin& plugin, clap_id id, double value)
{
    uint32_t slot = 0u;
    clap_id offset = 0u;
    if (decodeSlotParam(id, slot, offset)) {
        auto& target = plugin.targets.slots[slot];
        switch (offset) {
        case kSlotStart:
            target.start.store(static_cast<float>(std::clamp(value, 0.0,
                static_cast<double>(target.end.load()) - 0.001)));
            break;
        case kSlotEnd:
            target.end.store(static_cast<float>(std::clamp(value,
                static_cast<double>(target.start.load()) + 0.001, 1.0)));
            break;
        case kSlotSpeed: target.speed.store(static_cast<float>(
            std::clamp(value, 0.125, 4.0))); break;
        case kSlotStretch: target.stretch.store(static_cast<float>(
            std::clamp(value, 0.25, 4.0))); break;
        case kSlotPitch: target.pitch.store(static_cast<float>(
            std::clamp(value, -48.0, 48.0))); break;
        case kSlotNudge: target.nudge.store(static_cast<float>(
            std::clamp(value, -0.5, 0.5))); break;
        case kSlotGain: target.gain.store(static_cast<float>(
            std::clamp(value, -60.0, 12.0))); break;
        case kSlotReverse: target.reverse.store(value >= 0.5 ? 1u : 0u);
            break;
        default: break;
        }
        return;
    }
    if (id >= kManualRingParamBase
        && id < kManualRingParamBase + kHeadCount) {
        plugin.targets.manualRings[id - kManualRingParamBase].store(
            static_cast<float>(std::clamp(value, 0.0, 1.0)));
        return;
    }
    if (id >= kManualPhaseParamBase
        && id < kManualPhaseParamBase + kHeadCount) {
        plugin.targets.manualPhases[id - kManualPhaseParamBase].store(
            static_cast<float>(std::clamp(value, 0.0, 1.0)));
        return;
    }
    if (id >= kManualRateParamBase
        && id < kManualRateParamBase + kHeadCount) {
        plugin.targets.manualRates[id - kManualRateParamBase].store(
            static_cast<float>(std::clamp(value, -4.0, 4.0)));
        return;
    }
    switch (id) {
    case kPlaybackRateParamId: plugin.targets.playbackRate.store(static_cast<float>(
        std::clamp(value, 0.125, 4.0))); break;
    case kRingPathParamId: plugin.targets.ringPath.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 7l))); break;
    case kRelationshipParamId: plugin.targets.relationship.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 7l))); break;
    case kRelationshipAmountParamId:
        plugin.targets.relationshipAmount.store(static_cast<float>(
            std::clamp(value, -1.0, 1.0))); break;
    case kDriftParamId: plugin.targets.drift.store(static_cast<float>(
        std::clamp(value, 0.0, 1.0))); break;
    case kCenterParamId: plugin.targets.center.store(static_cast<float>(
        std::clamp(value, 0.0, 1.0))); break;
    case kGlideParamId: plugin.targets.glideMilliseconds.store(
        static_cast<float>(std::clamp(value, 0.0, 2000.0))); break;
    case kRingBlendParamId: plugin.targets.ringBlend.store(
        static_cast<float>(std::clamp(value, 0.0, 1.0))); break;
    case kFormationParamId: plugin.targets.formation.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 3l))); break;
    case kLoopJoinParamId: plugin.targets.loopJoin.store(static_cast<float>(
        std::clamp(value, 0.0, 0.5))); break;
    case kSeamDuckParamId: plugin.targets.seamDuck.store(static_cast<float>(
        std::clamp(value, 0.0, 0.75))); break;
    case kHeadMaskParamId: plugin.targets.headMask.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 255l)));
        break;
    case kOutputGainParamId: plugin.targets.outputGain.store(
        static_cast<float>(std::clamp(value, -60.0, 12.0))); break;
    case kOutputFormatParamId: plugin.targets.outputFormat.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 2l))); break;
    case kOutputRotationParamId: plugin.targets.outputRotation.store(
        s3g::sanitizeRingOutputRotation(static_cast<float>(value))); break;
    case kPlayingParamId: {
        const bool playing = value >= 0.5;
        plugin.targets.playing.store(playing ? 1u : 0u);
        if (playing) plugin.transportStopped.store(false);
        break;
    }
    case kCaptureTargetParamId: plugin.targets.captureTarget.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 3l))); break;
    case kCaptureGateParamId: plugin.targets.captureGate.store(
        value >= 0.5 ? 1u : 0u); break;
    case kCaptureModeParamId: plugin.targets.captureMode.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 2l))); break;
    case kInputWidthParamId: plugin.targets.inputWidth.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 8l))); break;
    case kCaptureQuantizeParamId: plugin.targets.captureQuantize.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 5l))); break;
    case kMonitorParamId: plugin.targets.monitor.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 2l))); break;
    case kSelectedSlotParamId: plugin.targets.selectedSlot.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 3l))); break;
    case kMidiModeParamId: plugin.targets.midiMode.store(
        static_cast<uint32_t>(std::clamp(std::lround(value), 0l, 2l))); break;
    case kMidiRootParamId: plugin.targets.midiRoot.store(static_cast<float>(
        std::clamp(std::lround(value), 0l, 127l))); break;
    case kOverdubFeedbackParamId:
        plugin.targets.overdubFeedback.store(static_cast<float>(
            std::clamp(value, 0.0, 1.0))); break;
    case kSeedParamId: plugin.targets.seed.store(static_cast<uint32_t>(
        std::clamp(std::lround(value), 1l, 999999l))); break;
    case kRingPositionParamId: plugin.targets.ringPosition.store(
        static_cast<float>(std::clamp(value, 0.0, 1.0))); break;
    case kRadialRatioParamId: plugin.targets.radialRatio.store(
        static_cast<float>(std::clamp(value, 0.0, 4.0))); break;
    case kPathDepthParamId: plugin.targets.pathDepth.store(
        static_cast<float>(std::clamp(value, 0.0, 1.0))); break;
    case kPathOffsetParamId: plugin.targets.pathOffset.store(
        static_cast<float>(std::clamp(value, 0.0, 1.0))); break;
    case kPathSpreadParamId: plugin.targets.pathSpread.store(
        static_cast<float>(std::clamp(value, 0.0, 1.0))); break;
    case kPathSlewParamId: plugin.targets.pathSlewMilliseconds.store(
        static_cast<float>(std::clamp(value, 0.0, 2000.0))); break;
    case kRadialReverseParamId: plugin.targets.radialReverse.store(
        value >= 0.5 ? 1u : 0u); break;
    case kAngularReverseParamId: plugin.targets.angularReverse.store(
        value >= 0.5 ? 1u : 0u); break;
    default: break;
    }
}

double paramValue(const Plugin& plugin, clap_id id)
{
    uint32_t slot = 0u;
    clap_id offset = 0u;
    if (decodeSlotParam(id, slot, offset)) {
        const auto& target = plugin.targets.slots[slot];
        switch (offset) {
        case kSlotStart: return target.start.load();
        case kSlotEnd: return target.end.load();
        case kSlotSpeed: return target.speed.load();
        case kSlotStretch: return target.stretch.load();
        case kSlotPitch: return target.pitch.load();
        case kSlotNudge: return target.nudge.load();
        case kSlotGain: return target.gain.load();
        case kSlotReverse: return target.reverse.load();
        default: return 0.0;
        }
    }
    if (id >= kManualRingParamBase
        && id < kManualRingParamBase + kHeadCount)
        return plugin.targets.manualRings[id - kManualRingParamBase].load();
    if (id >= kManualPhaseParamBase
        && id < kManualPhaseParamBase + kHeadCount)
        return plugin.targets.manualPhases[id - kManualPhaseParamBase].load();
    if (id >= kManualRateParamBase
        && id < kManualRateParamBase + kHeadCount)
        return plugin.targets.manualRates[id - kManualRateParamBase].load();
    switch (id) {
    case kPlaybackRateParamId: return plugin.targets.playbackRate.load();
    case kRingPathParamId: return plugin.targets.ringPath.load();
    case kRelationshipParamId: return plugin.targets.relationship.load();
    case kRelationshipAmountParamId:
        return plugin.targets.relationshipAmount.load();
    case kDriftParamId: return plugin.targets.drift.load();
    case kCenterParamId: return plugin.targets.center.load();
    case kGlideParamId: return plugin.targets.glideMilliseconds.load();
    case kRingBlendParamId: return plugin.targets.ringBlend.load();
    case kFormationParamId: return plugin.targets.formation.load();
    case kLoopJoinParamId: return plugin.targets.loopJoin.load();
    case kSeamDuckParamId: return plugin.targets.seamDuck.load();
    case kHeadMaskParamId: return plugin.targets.headMask.load();
    case kOutputGainParamId: return plugin.targets.outputGain.load();
    case kOutputFormatParamId: return plugin.targets.outputFormat.load();
    case kOutputRotationParamId: return plugin.targets.outputRotation.load();
    case kPlayingParamId: return plugin.targets.playing.load();
    case kCaptureTargetParamId: return plugin.targets.captureTarget.load();
    case kCaptureGateParamId: return plugin.targets.captureGate.load();
    case kCaptureModeParamId: return plugin.targets.captureMode.load();
    case kInputWidthParamId: return plugin.targets.inputWidth.load();
    case kCaptureQuantizeParamId:
        return plugin.targets.captureQuantize.load();
    case kMonitorParamId: return plugin.targets.monitor.load();
    case kSelectedSlotParamId: return plugin.targets.selectedSlot.load();
    case kMidiModeParamId: return plugin.targets.midiMode.load();
    case kMidiRootParamId: return plugin.targets.midiRoot.load();
    case kOverdubFeedbackParamId:
        return plugin.targets.overdubFeedback.load();
    case kSeedParamId: return plugin.targets.seed.load();
    case kRingPositionParamId: return plugin.targets.ringPosition.load();
    case kRadialRatioParamId: return plugin.targets.radialRatio.load();
    case kPathDepthParamId: return plugin.targets.pathDepth.load();
    case kPathOffsetParamId: return plugin.targets.pathOffset.load();
    case kPathSpreadParamId: return plugin.targets.pathSpread.load();
    case kPathSlewParamId:
        return plugin.targets.pathSlewMilliseconds.load();
    case kRadialReverseParamId:
        return plugin.targets.radialReverse.load();
    case kAngularReverseParamId:
        return plugin.targets.angularReverse.load();
    default: return 0.0;
    }
}

uint64_t regularFileByteCount(const std::string& path) noexcept
{
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(path, error))
        return 0u;
    const auto bytes = std::filesystem::file_size(path, error);
    return error ? 0u : static_cast<uint64_t>(bytes);
}

std::string sourceDisplayName(const std::string& path)
{
    if (path.empty()) return "NO SOURCE";
    const std::string name = std::filesystem::path(path).filename().string();
    return name.empty() ? s3g::sample_storage::abbreviatedPath(path) : name;
}

void updateSourceStatus(Plugin& plugin, uint32_t slot)
{
    if (slot >= kSourceCount) return;
    if (!plugin.sources[slot]) {
        plugin.sourceStatuses[slot] = plugin.sourcePaths[slot].empty()
            ? "EMPTY SLOT" : "OFFLINE / "
                + sourceDisplayName(plugin.sourcePaths[slot]);
        return;
    }
    if (plugin.sourcePaths[slot].empty()) {
        plugin.sourceStatuses[slot] = "CAPTURE / EMBEDDED";
        return;
    }
    plugin.sourceStatuses[slot]
        = std::string(s3g::sample_storage::storageModeName(
            plugin.storageMode)) + " / " + plugin.sourceNames[slot];
}

void publishSource(Plugin& plugin, uint32_t slot,
    std::shared_ptr<const SampleAsset> asset, std::string path,
    std::string name, bool dirty = true)
{
    if (slot >= kSourceCount) return;
    if (plugin.sources[slot] && plugin.sources[slot] != asset)
        plugin.retiredSources.push_back(plugin.sources[slot]);
    plugin.sources[slot] = asset;
    std::atomic_store_explicit(&plugin.audioSources[slot], std::move(asset),
        std::memory_order_release);
    plugin.sourcePaths[slot] = std::move(path);
    plugin.sourceNames[slot] = std::move(name);
    updateSourceStatus(plugin, slot);
    if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
    if (dirty) markDirty(plugin);
}

void clearSource(Plugin& plugin, uint32_t slot)
{
    if (slot >= kSourceCount) return;
#if defined(__APPLE__)
    ++plugin.loadGenerations[slot];
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        plugin.loadRequests.erase(std::remove_if(
            plugin.loadRequests.begin(), plugin.loadRequests.end(),
            [slot](const LoadRequest& request) {
                return request.slot == slot;
            }), plugin.loadRequests.end());
    }
#endif
    plugin.projectFileRegistrations[slot].clear();
    std::shared_ptr<const SampleAsset> empty;
    if (plugin.sources[slot])
        plugin.retiredSources.push_back(plugin.sources[slot]);
    plugin.sources[slot].reset();
    std::atomic_store_explicit(&plugin.audioSources[slot], empty,
        std::memory_order_release);
    plugin.sourcePaths[slot].clear();
    plugin.linkSourcePaths[slot].clear();
    plugin.projectRelativePaths[slot].clear();
    plugin.sourceNames[slot] = "EMPTY";
    plugin.sourceFileBytes[slot] = 0u;
    plugin.projectStoragePending[slot] = false;
    plugin.projectCopyInFlight[slot] = false;
    updateSourceStatus(plugin, slot);
    markDirty(plugin);
}

#if defined(__APPLE__)
std::shared_ptr<const SampleAsset> readSampleFromPath(
    const std::string& path)
{
    if (path.empty()) return nullptr;
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSError* error = nil;
        AVAudioFile* file = [[AVAudioFile alloc]
            initForReading:[NSURL fileURLWithPath:nsPath] error:&error];
        if (!file) return nullptr;
        AVAudioFormat* format = [file processingFormat];
        const AVAudioFrameCount frames = static_cast<AVAudioFrameCount>(
            std::min<int64_t>([file length], 0x7fffffff));
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:format frameCapacity:frames];
        if (!buffer || ![file readIntoBuffer:buffer error:&error]
            || buffer.frameLength < 2u || !buffer.floatChannelData) {
            [buffer release];
            [file release];
            return nullptr;
        }
        auto asset = std::make_shared<SampleAsset>();
        asset->sampleRate = buffer.format.sampleRate;
        asset->channelCount = static_cast<uint8_t>(std::min<NSUInteger>(
            buffer.format.channelCount,
            s3g::sample::kMaximumAudioChannels));
        for (uint8_t channel = 0u; channel < asset->channelCount; ++channel) {
            const float* source = buffer.floatChannelData[channel];
            if (!source) continue;
            asset->channels[channel].assign(source,
                source + buffer.frameLength);
        }
        [buffer release];
        [file release];
        return asset->valid() ? asset : nullptr;
    }
}

bool loadSourceFromPath(Plugin& plugin, uint32_t slot,
    const std::string& path)
{
    auto asset = readSampleFromPath(path);
    if (!asset) return false;
    NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
    const char* last = [[nsPath lastPathComponent] UTF8String];
    plugin.projectFileRegistrations[slot].clear();
    plugin.linkSourcePaths[slot] = path;
    plugin.projectRelativePaths[slot].clear();
    plugin.sourceFileBytes[slot] = regularFileByteCount(path);
    plugin.projectStoragePending[slot]
        = plugin.storageMode == StorageMode::Project;
    plugin.projectCopyInFlight[slot] = false;
    publishSource(plugin, slot, asset, path, last ? last : "LOADED");
    if (plugin.storageMode == StorageMode::Project)
        plugin.sourceStatuses[slot] = "PROJECT PENDING / SAVE PROJECT";
    return true;
}

bool isSupportedAudioURL(NSURL* url)
{
    if (!url || !url.isFileURL) return false;
    NSString* extension = url.pathExtension.lowercaseString;
    return [@[@"wav", @"aif", @"aiff", @"caf", @"mp3", @"m4a"]
        containsObject:extension];
}

void loaderMain(Plugin* plugin)
{
    for (;;) {
        LoadRequest request;
        {
            std::unique_lock<std::mutex> lock(plugin->loaderMutex);
            plugin->loaderCondition.wait(lock, [plugin] {
                return plugin->loaderStopping
                    || !plugin->loadRequests.empty();
            });
            if (plugin->loaderStopping) return;
            request = std::move(plugin->loadRequests.front());
            plugin->loadRequests.pop_front();
        }
        LoadResult result;
        result.generation = request.generation;
        result.slot = request.slot;
        result.sourcePath = std::move(request.path);
        result.decodedPath = result.sourcePath;
        result.copyOnly = request.copyOnly;
        result.error = std::move(request.projectError);
        if (request.projectLocation.available()) {
            result.projectCopy = s3g::sample_storage::copyFileIntoProject(
                request.projectLocation, result.sourcePath);
            if (result.projectCopy.success) {
                result.decodedPath = result.projectCopy.absolutePath;
                result.sourceFileBytes = result.projectCopy.byteCount;
            } else if (result.error.empty()) {
                result.error = result.projectCopy.error;
            }
        }
        if (result.sourceFileBytes == 0u)
            result.sourceFileBytes = regularFileByteCount(result.sourcePath);
        if (!result.copyOnly) {
            try { result.asset = readSampleFromPath(result.decodedPath); }
            catch (...) {
                result.asset.reset();
                result.error = "SOURCE DECODE EXCEEDED AVAILABLE MEMORY";
            }
            if (!result.asset && result.error.empty())
                result.error = "SOURCE DECODE FAILED";
        }
        {
            std::lock_guard<std::mutex> lock(plugin->loaderMutex);
            plugin->loadResults.push_back(std::move(result));
        }
        if (plugin->host && plugin->host->request_callback)
            plugin->host->request_callback(plugin->host);
    }
}

bool startLoader(Plugin& plugin)
{
    try { plugin.loaderThread = std::thread(loaderMain, &plugin); }
    catch (...) { return false; }
    return true;
}

void stopLoader(Plugin& plugin)
{
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        plugin.loaderStopping = true;
    }
    plugin.loaderCondition.notify_all();
    if (plugin.loaderThread.joinable()) plugin.loaderThread.join();
}

void queueSourceLoad(Plugin& plugin, uint32_t slot, std::string path)
{
    if (slot >= kSourceCount || path.empty()) return;
    LoadRequest request;
    request.generation = ++plugin.loadGenerations[slot];
    request.slot = static_cast<uint8_t>(slot);
    request.path = path;
    plugin.linkSourcePaths[slot] = path;
    plugin.projectRelativePaths[slot].clear();
    plugin.projectStoragePending[slot]
        = plugin.storageMode == StorageMode::Project;
    plugin.projectCopyInFlight[slot] = true;
    if (plugin.storageMode == StorageMode::Project) {
        plugin.reaperContext = s3g::sample_storage::reaperContext(plugin.host);
        (void)s3g::sample_storage::queryProjectLocation(
            plugin.reaperContext, request.projectLocation,
            &request.projectError);
    }
    plugin.sourceStatuses[slot] = "DECODING / " + sourceDisplayName(path);
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        plugin.loadRequests.erase(std::remove_if(
            plugin.loadRequests.begin(), plugin.loadRequests.end(),
            [slot](const LoadRequest& pending) {
                return pending.slot == slot;
            }), plugin.loadRequests.end());
        plugin.loadRequests.push_back(std::move(request));
    }
    plugin.loaderCondition.notify_one();
}

void queueProjectCopy(Plugin& plugin, uint32_t slot)
{
    if (slot >= kSourceCount || !plugin.sources[slot]
        || plugin.sourcePaths[slot].empty()) return;
    std::string path = !plugin.linkSourcePaths[slot].empty()
        ? plugin.linkSourcePaths[slot] : plugin.sourcePaths[slot];
    plugin.projectStoragePending[slot] = true;
    const ReaperContext context
        = s3g::sample_storage::reaperContext(plugin.host);
    ProjectLocation location;
    std::string error;
    if (!s3g::sample_storage::queryProjectLocation(context, location,
            &error)) {
        plugin.sourceStatuses[slot] = "PROJECT PENDING / SAVE PROJECT";
        plugin.projectCopyInFlight[slot] = false;
        plugin.nextProjectCopyProbe[slot]
            = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        return;
    }
    std::string absolute = path;
    if (!std::filesystem::path(absolute).is_absolute()
        && !s3g::sample_storage::resolveProjectRelativePath(location, path,
            absolute, &error)) return;
    std::string relative;
    if (s3g::sample_storage::makeProjectRelativePath(location, absolute,
            relative, &error) && regularFileByteCount(absolute) != 0u) {
        plugin.sourcePaths[slot] = absolute;
        plugin.projectRelativePaths[slot] = relative;
        plugin.projectStoragePending[slot] = false;
        plugin.projectCopyInFlight[slot] = false;
        plugin.sourceNames[slot] = sourceDisplayName(absolute);
        updateSourceStatus(plugin, slot);
        (void)plugin.projectFileRegistrations[slot].reset(context, absolute,
            nullptr, nullptr, plugin.plugin.desc->name);
        markDirty(plugin);
        return;
    }
    LoadRequest request;
    request.generation = ++plugin.loadGenerations[slot];
    request.slot = static_cast<uint8_t>(slot);
    request.path = absolute;
    request.projectLocation = std::move(location);
    request.copyOnly = true;
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        plugin.loadRequests.push_back(std::move(request));
    }
    plugin.projectCopyInFlight[slot] = true;
    plugin.sourceStatuses[slot] = "COPYING TO PROJECT...";
    plugin.loaderCondition.notify_one();
}

void serviceLoads(Plugin& plugin)
{
    std::deque<LoadResult> results;
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        results.swap(plugin.loadResults);
    }
    for (auto& result : results) {
        const uint32_t slot = result.slot;
        if (slot >= kSourceCount
            || result.generation != plugin.loadGenerations[slot]) continue;
        plugin.projectCopyInFlight[slot] = false;
        if (result.copyOnly) {
            if (plugin.storageMode != StorageMode::Project) continue;
            if (!result.projectCopy.success) {
                plugin.sourceStatuses[slot] = result.error.empty()
                    ? "PROJECT COPY FAILED" : result.error;
                plugin.nextProjectCopyProbe[slot]
                    = std::chrono::steady_clock::now()
                        + std::chrono::seconds(1);
                continue;
            }
            plugin.sourcePaths[slot] = result.projectCopy.absolutePath;
            plugin.projectRelativePaths[slot]
                = result.projectCopy.relativePath;
            plugin.sourceFileBytes[slot] = result.projectCopy.byteCount;
            plugin.sourceNames[slot] = sourceDisplayName(
                result.projectCopy.absolutePath);
            plugin.projectStoragePending[slot] = false;
            updateSourceStatus(plugin, slot);
            const ReaperContext context
                = s3g::sample_storage::reaperContext(plugin.host);
            (void)plugin.projectFileRegistrations[slot].reset(context,
                result.projectCopy.absolutePath, nullptr, nullptr,
                plugin.plugin.desc->name);
            markDirty(plugin);
            continue;
        }
        if (!result.asset) {
            plugin.sourceStatuses[slot] = result.error.empty()
                ? "SOURCE DECODE FAILED" : result.error;
            continue;
        }
        std::string publishedPath = result.sourcePath;
        if (plugin.storageMode == StorageMode::Project
            && result.projectCopy.success) {
            publishedPath = result.projectCopy.absolutePath;
            plugin.projectRelativePaths[slot]
                = result.projectCopy.relativePath;
            plugin.projectStoragePending[slot] = false;
        } else if (plugin.storageMode == StorageMode::Project) {
            plugin.projectStoragePending[slot] = true;
            plugin.nextProjectCopyProbe[slot]
                = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        } else {
            plugin.projectFileRegistrations[slot].clear();
            plugin.projectRelativePaths[slot].clear();
            plugin.projectStoragePending[slot] = false;
        }
        plugin.sourceFileBytes[slot] = result.sourceFileBytes;
        publishSource(plugin, slot, result.asset, publishedPath,
            sourceDisplayName(publishedPath));
        if (plugin.storageMode == StorageMode::Project) {
            if (result.projectCopy.success) {
                const ReaperContext context
                    = s3g::sample_storage::reaperContext(plugin.host);
                (void)plugin.projectFileRegistrations[slot].reset(context,
                    result.projectCopy.absolutePath, nullptr, nullptr,
                    plugin.plugin.desc->name);
            } else {
                plugin.sourceStatuses[slot]
                    = "PROJECT PENDING / SAVE PROJECT";
            }
        }
    }

    const auto now = std::chrono::steady_clock::now();
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        if (plugin.storageMode == StorageMode::Project
            && plugin.projectStoragePending[slot]
            && !plugin.projectCopyInFlight[slot] && plugin.sources[slot]
            && !plugin.sourcePaths[slot].empty()
            && now >= plugin.nextProjectCopyProbe[slot])
            queueProjectCopy(plugin, slot);
    }
}

void setStorageMode(Plugin& plugin, StorageMode mode)
{
    mode = s3g::sample_storage::sanitizeStorageMode(
        static_cast<uint8_t>(mode));
    if (plugin.storageMode == mode) return;
    plugin.storageMode = mode;
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        if (mode == StorageMode::Project) {
            if (plugin.sources[slot] && !plugin.sourcePaths[slot].empty()
                && !plugin.projectCopyInFlight[slot])
                queueProjectCopy(plugin, slot);
            else {
                plugin.projectStoragePending[slot]
                    = plugin.sources[slot]
                    && !plugin.sourcePaths[slot].empty();
                updateSourceStatus(plugin, slot);
            }
            continue;
        }
        const std::string registeredPath
            = plugin.projectFileRegistrations[slot].registered()
            ? plugin.projectFileRegistrations[slot].absolutePath()
            : std::string();
        plugin.projectFileRegistrations[slot].clear();
        plugin.projectStoragePending[slot] = false;
        plugin.projectRelativePaths[slot].clear();
        if (!plugin.linkSourcePaths[slot].empty()) {
            plugin.sourcePaths[slot] = plugin.linkSourcePaths[slot];
            plugin.sourceNames[slot]
                = sourceDisplayName(plugin.sourcePaths[slot]);
        } else if (!registeredPath.empty()) {
            plugin.sourcePaths[slot] = registeredPath;
            plugin.sourceNames[slot] = sourceDisplayName(registeredPath);
        }
        updateSourceStatus(plugin, slot);
    }
    markDirty(plugin);
}
#endif

uint32_t detectedInputWidth(const Plugin& plugin)
{
    const uint32_t explicitWidth = plugin.targets.inputWidth.load(
        std::memory_order_acquire);
    if (explicitWidth > 0u) return std::min(explicitWidth, kChannelCount);
    uint32_t width = 0u;
    for (uint32_t channel = 0u; channel < kChannelCount; ++channel)
        if (plugin.inputPeaks[channel].load(std::memory_order_relaxed)
            > 1.0e-5f) width = channel + 1u;
    if (width == 0u) {
        const uint32_t target = plugin.targets.captureTarget.load();
        const auto asset = target < kSourceCount ? plugin.sources[target]
            : nullptr;
        width = asset ? std::min<uint32_t>(asset->channelCount,
            kChannelCount) : 2u;
    }
    return std::clamp(width, 1u, kChannelCount);
}

float assetSampleAt(const SampleAsset& asset, uint32_t channel,
    double position)
{
    if (asset.frameCount() == 0u) return 0.0f;
    channel %= asset.channelCount;
    position = std::clamp(position, 0.0,
        static_cast<double>(asset.frameCount() - 1u));
    const uint32_t first = static_cast<uint32_t>(std::floor(position));
    const uint32_t second = std::min(first + 1u, asset.frameCount() - 1u);
    const float mix = static_cast<float>(position - std::floor(position));
    return asset.channels[channel][first]
        + (asset.channels[channel][second]
            - asset.channels[channel][first]) * mix;
}

void prepareCaptureOnMainThread(Plugin& plugin)
{
    const uint32_t slot = std::min(plugin.targets.captureTarget.load(), 3u);
    auto& capture = plugin.captures[slot];
    if (capture.recording.load(std::memory_order_acquire)) return;
    const uint32_t channels = detectedInputWidth(plugin);
    capture.channels = channels;
    capture.frames = 0u;
    capture.writeFrame = 0u;
    capture.baseFrames = 0u;
    capture.mode = static_cast<CaptureMode>(std::min(
        plugin.targets.captureMode.load(), 2u));
    if (capture.samples.size() != kCaptureSampleCapacity)
        capture.samples.assign(kCaptureSampleCapacity, 0.0f);
    const auto source = plugin.sources[slot];
    if (source && capture.mode != CaptureMode::Replace) {
        const uint32_t capacityFrames = static_cast<uint32_t>(
            capture.samples.size() / channels);
        const double ratio = source->sampleRate / plugin.sampleRate;
        capture.baseFrames = std::min(capacityFrames,
            static_cast<uint32_t>(std::max(1.0,
                std::floor(source->frameCount() / ratio))));
        for (uint32_t frame = 0u; frame < capture.baseFrames; ++frame)
            for (uint32_t channel = 0u; channel < channels; ++channel)
                capture.samples[static_cast<std::size_t>(frame) * channels
                    + channel] = assetSampleAt(*source, channel,
                        static_cast<double>(frame) * ratio);
        capture.frames = capture.baseFrames;
    }
    capture.prepared.store(true, std::memory_order_release);
}

double captureGridBeats(const clap_event_transport_t* transport,
    CaptureQuantize quantize)
{
    if (quantize == CaptureQuantize::Free) return 0.0;
    if (quantize == CaptureQuantize::Beat) return 1.0;
    double bar = 4.0;
    if (transport
        && (transport->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0u
        && transport->tsig_denom != 0u)
        bar = static_cast<double>(transport->tsig_num) * 4.0
            / static_cast<double>(transport->tsig_denom);
    const uint32_t multiplier = quantize == CaptureQuantize::TwoBars ? 2u
        : quantize == CaptureQuantize::FourBars ? 4u
        : quantize == CaptureQuantize::EightBars ? 8u : 1u;
    return bar * multiplier;
}

bool captureBoundary(const clap_event_transport_t* transport,
    uint32_t frame, double sampleRate, CaptureQuantize quantize)
{
    const double grid = captureGridBeats(transport, quantize);
    if (!(grid > 0.0) || !transport
        || (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) == 0u
        || (transport->flags & CLAP_TRANSPORT_HAS_TEMPO) == 0u)
        return frame == 0u;
    const double beat = static_cast<double>(transport->song_pos_beats)
        / static_cast<double>(CLAP_BEATTIME_FACTOR)
        + static_cast<double>(frame) * transport->tempo
            / (60.0 * sampleRate);
    const double next = beat + transport->tempo / (60.0 * sampleRate);
    const double boundary = std::ceil((beat - 1.0e-9) / grid) * grid;
    return beat <= boundary + 1.0e-9 && next > boundary + 1.0e-9;
}

void beginCaptureAudio(Plugin& plugin, uint32_t slot)
{
    auto& capture = plugin.captures[slot];
    if (!capture.prepared.exchange(false, std::memory_order_acq_rel)) {
        capture.channels = detectedInputWidth(plugin);
        capture.frames = 0u;
        capture.writeFrame = 0u;
        capture.baseFrames = 0u;
        capture.mode = CaptureMode::Replace;
    }
    capture.recording.store(true, std::memory_order_release);
    capture.pendingStart = false;
    capture.pendingStop = false;
    capture.ready.store(false, std::memory_order_release);
    plugin.activeCaptureSlot.store(static_cast<int32_t>(slot),
        std::memory_order_release);
    plugin.captureChannels.store(capture.channels, std::memory_order_release);
}

void finishCaptureAudio(Plugin& plugin, uint32_t slot)
{
    auto& capture = plugin.captures[slot];
    capture.recording.store(false, std::memory_order_release);
    capture.pendingStop = false;
    capture.readyFrames.store(capture.frames, std::memory_order_relaxed);
    capture.readyChannels.store(capture.channels, std::memory_order_relaxed);
    capture.ready.store(capture.frames >= 2u, std::memory_order_release);
    plugin.activeCaptureSlot.store(-1, std::memory_order_release);
    plugin.captureFrames.store(capture.frames, std::memory_order_release);
    plugin.captureCallbackPending.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_callback)
        plugin.host->request_callback(plugin.host);
}

void processCapture(Plugin& plugin, const clap_process_t* process)
{
    const clap_audio_buffer_t* input = process->audio_inputs_count > 0u
        ? &process->audio_inputs[0u] : nullptr;
    const bool desired = plugin.targets.captureGate.load(
        std::memory_order_acquire) != 0u;
    const uint32_t target = std::min(plugin.targets.captureTarget.load(), 3u);
    const auto quantize = static_cast<CaptureQuantize>(std::min(
        plugin.targets.captureQuantize.load(), 5u));
    int32_t active = plugin.activeCaptureSlot.load(std::memory_order_acquire);
    if (active >= 0 && static_cast<uint32_t>(active) != target) {
        finishCaptureAudio(plugin, static_cast<uint32_t>(active));
        active = -1;
    }
    auto& capture = plugin.captures[target];
    if (desired && active < 0) capture.pendingStart = true;
    if (!desired && active == static_cast<int32_t>(target))
        capture.pendingStop = true;

    for (uint32_t frame = 0u; frame < process->frames_count; ++frame) {
        if (capture.pendingStart && captureBoundary(process->transport, frame,
                plugin.sampleRate, quantize)) {
            beginCaptureAudio(plugin, target);
            active = static_cast<int32_t>(target);
        }
        if (capture.pendingStop && captureBoundary(process->transport, frame,
                plugin.sampleRate, quantize)) {
            finishCaptureAudio(plugin, target);
            active = -1;
        }
        if (active != static_cast<int32_t>(target)
            || !capture.recording.load(std::memory_order_acquire))
            continue;
        const uint32_t channels = std::max(1u, capture.channels);
        const uint32_t capacityFrames = static_cast<uint32_t>(
            capture.samples.size() / channels);
        if (capture.mode == CaptureMode::Replace
            && capture.writeFrame >= capacityFrames) {
            finishCaptureAudio(plugin, target);
            plugin.targets.captureGate.store(0u, std::memory_order_release);
            active = -1;
            continue;
        }
        uint32_t destination = capture.writeFrame;
        if (capture.mode != CaptureMode::Replace && capture.baseFrames > 0u)
            destination %= capture.baseFrames;
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            float inputSample = 0.0f;
            if (input && input->data32 && channel < input->channel_count
                && input->data32[channel]) inputSample = input->data32[channel][frame];
            const std::size_t index = static_cast<std::size_t>(destination)
                * channels + channel;
            if (capture.mode == CaptureMode::Overdub
                && capture.baseFrames > 0u) {
                const float feedback = plugin.targets.overdubFeedback.load(
                    std::memory_order_relaxed);
                capture.samples[index] = std::clamp(
                    capture.samples[index] * feedback + inputSample,
                    -4.0f, 4.0f);
            } else capture.samples[index] = inputSample;
        }
        ++capture.writeFrame;
        capture.frames = capture.mode == CaptureMode::Replace
            ? std::max(capture.frames, capture.writeFrame)
            : std::max(capture.frames, capture.baseFrames);
        plugin.captureFrames.store(capture.frames, std::memory_order_relaxed);
    }
}

void finalizeCaptures(Plugin& plugin)
{
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        auto& capture = plugin.captures[slot];
        if (!capture.ready.exchange(false, std::memory_order_acq_rel))
            continue;
        const uint32_t frames = capture.readyFrames.load(
            std::memory_order_relaxed);
        const uint32_t channels = capture.readyChannels.load(
            std::memory_order_relaxed);
        if (frames < 2u || channels == 0u || channels > kChannelCount)
            continue;
        auto asset = std::make_shared<SampleAsset>();
        asset->sampleRate = plugin.sampleRate;
        asset->channelCount = static_cast<uint8_t>(channels);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            asset->channels[channel].resize(frames);
            for (uint32_t frame = 0u; frame < frames; ++frame)
                asset->channels[channel][frame] = capture.samples[
                    static_cast<std::size_t>(frame) * channels + channel];
        }
        if (asset->valid()) {
#if defined(__APPLE__)
            ++plugin.loadGenerations[slot];
            {
                std::lock_guard<std::mutex> lock(plugin.loaderMutex);
                plugin.loadRequests.erase(std::remove_if(
                    plugin.loadRequests.begin(), plugin.loadRequests.end(),
                    [slot](const LoadRequest& request) {
                        return request.slot == slot;
                    }), plugin.loadRequests.end());
            }
#endif
            plugin.projectFileRegistrations[slot].clear();
            plugin.linkSourcePaths[slot].clear();
            plugin.projectRelativePaths[slot].clear();
            plugin.sourceFileBytes[slot] = 0u;
            plugin.projectStoragePending[slot] = false;
            plugin.projectCopyInFlight[slot] = false;
            char name[64] {};
            std::snprintf(name, sizeof(name), "CAPTURE %c / %.2f S",
                static_cast<char>('A' + slot),
                static_cast<double>(frames) / plugin.sampleRate);
            publishSource(plugin, slot, asset, {}, name);
        }
    }
    plugin.captureCallbackPending.store(false, std::memory_order_release);
}

void readEvents(Plugin& plugin, const clap_input_events_t* input)
{
    if (!input) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* parameter = reinterpret_cast<
                const clap_event_param_value_t*>(event);
            setParam(plugin, parameter->param_id, parameter->value);
        } else if (event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF
            || event->type == CLAP_EVENT_NOTE_CHOKE) {
            const uint32_t mode = plugin.targets.midiMode.load();
            if (mode == 0u) continue;
            const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
            const bool on = event->type == CLAP_EVENT_NOTE_ON
                && note->velocity > 0.0;
            if (on) {
                const float semitones = static_cast<float>(note->key)
                    - plugin.targets.midiRoot.load();
                plugin.midiRateScale.store(std::pow(2.0f,
                    semitones / 12.0f));
                if (mode == 1u) plugin.heldNotes.fetch_add(1u);
                plugin.targets.playing.store(1u);
                plugin.transportStopped.store(false);
                if (mode == 2u)
                    plugin.engine.resync(snapshotSettings(plugin));
            } else if (mode == 1u) {
                uint32_t held = plugin.heldNotes.load();
                while (held > 0u && !plugin.heldNotes.compare_exchange_weak(
                    held, held - 1u)) {}
                if (plugin.heldNotes.load() == 0u) {
                    plugin.targets.playing.store(0u);
                    plugin.midiRateScale.store(1.0f);
                }
            }
        }
    }
}

bool init(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.reaperContext = s3g::sample_storage::reaperContext(
        instance.host);
    if (instance.host && instance.host->get_extension)
        instance.hostParams = static_cast<const clap_host_params_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_PARAMS));
#if defined(__APPLE__)
    if (!startLoader(instance)) return false;
#endif
    return true;
}

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance) return;
#if defined(__APPLE__)
    guiDestroy(plugin);
    stopLoader(*instance);
#endif
    for (auto& registration : instance->projectFileRegistrations)
        registration.clear();
    delete instance;
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maxFrames)
{
    auto& instance = *self(plugin);
    instance.sampleRate = sampleRate;
    instance.maxFrames = maxFrames;
    if (!instance.engine.prepare(sampleRate)) return false;
    try {
        for (auto& capture : instance.captures)
            if (capture.samples.size() != kCaptureSampleCapacity)
                capture.samples.assign(kCaptureSampleCapacity, 0.0f);
        instance.monitorScratch.assign(static_cast<std::size_t>(maxFrames)
            * kChannelCount, 0.0f);
    } catch (...) {
        instance.engine.unprepare();
        return false;
    }
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.engine.unprepare();
    instance.audioRetained = {};
    instance.retiredSources.clear();
    for (auto& capture : instance.captures) {
        capture.recording.store(false, std::memory_order_release);
        capture.pendingStart = false;
        capture.pendingStop = false;
        capture.prepared.store(false, std::memory_order_release);
        capture.ready.store(false, std::memory_order_release);
        capture.samples.clear();
        capture.samples.shrink_to_fit();
    }
    instance.activeCaptureSlot.store(-1, std::memory_order_release);
    instance.targets.captureGate.store(0u, std::memory_order_release);
    instance.monitorScratch.clear();
    instance.monitorScratch.shrink_to_fit();
}

bool startProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(true, std::memory_order_release);
    return true;
}
void stopProcessing(const clap_plugin_t* plugin)
{ self(plugin)->processing.store(false, std::memory_order_release); }
void reset(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.engine.reset();
    instance.transportCommand.store(
        static_cast<uint32_t>(TransportCommand::None));
    instance.transportStopped.store(false);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto& instance = *self(plugin);
    serviceGuiParamEvents(instance, process->out_events);
    readEvents(instance, process->in_events);
    const clap_audio_buffer_t* input = process->audio_inputs_count > 0u
        ? &process->audio_inputs[0u] : nullptr;
    for (uint32_t channel = 0u; channel < kChannelCount; ++channel) {
        float peak = 0.0f;
        if (input && input->data32 && channel < input->channel_count
            && input->data32[channel])
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                peak = std::max(peak, std::abs(input->data32[channel][frame]));
        instance.inputPeaks[channel].store(std::max(peak,
            instance.inputPeaks[channel].load(std::memory_order_relaxed)
                * 0.92f), std::memory_order_relaxed);
    }
    processCapture(instance, process);
    if (process->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    auto& output = process->audio_outputs[0u];
    if (!output.data32) {
        if (output.data64)
            for (uint32_t channel = 0u; channel < output.channel_count;
                 ++channel)
                if (output.data64[channel]) std::fill(output.data64[channel],
                    output.data64[channel] + process->frames_count, 0.0);
        return CLAP_PROCESS_CONTINUE;
    }
    std::array<float*, kChannelCount> pointers {};
    for (uint32_t channel = 0u; channel < kChannelCount; ++channel)
        pointers[channel] = channel < output.channel_count
            ? output.data32[channel] : nullptr;
    if (output.channel_count < kChannelCount
        || std::any_of(pointers.begin(), pointers.end(),
            [](float* pointer) { return pointer == nullptr; })) {
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel)
            if (output.data32[channel]) std::fill(output.data32[channel],
                output.data32[channel] + process->frames_count, 0.0f);
        return CLAP_PROCESS_CONTINUE;
    }
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        auto source = std::atomic_load_explicit(&instance.audioSources[slot],
            std::memory_order_acquire);
        if (source != instance.audioRetained[slot])
            instance.audioRetained[slot] = source;
        instance.engine.setPreparedAsset(slot, source.get());
    }
    const auto monitor = static_cast<MonitorMode>(std::min(
        instance.targets.monitor.load(), 2u));
    const bool copiedMonitorInput = monitor != MonitorMode::Loop && input
        && input->data32 && instance.monitorScratch.size()
            >= static_cast<std::size_t>(process->frames_count) * kChannelCount;
    if (copiedMonitorInput) {
        for (uint32_t channel = 0u; channel < kChannelCount; ++channel) {
            float* destination = instance.monitorScratch.data()
                + static_cast<std::size_t>(channel) * process->frames_count;
            if (channel < input->channel_count && input->data32[channel])
                std::copy_n(input->data32[channel], process->frames_count,
                    destination);
            else std::fill(destination, destination + process->frames_count,
                0.0f);
        }
    }
    const auto settings = snapshotSettings(instance);
    const auto transportCommand = static_cast<TransportCommand>(
        instance.transportCommand.exchange(
            static_cast<uint32_t>(TransportCommand::None),
            std::memory_order_acq_rel));
    if (transportCommand != TransportCommand::None) {
        if (transportCommand == TransportCommand::Stop) {
            instance.targets.playing.store(0u, std::memory_order_release);
            instance.transportStopped.store(true, std::memory_order_release);
        }
        instance.engine.resync(settings);
    }
    instance.engine.render(settings, pointers.data(), kChannelCount,
        process->frames_count, instance.targets.playing.load() != 0u);
    if (monitor != MonitorMode::Loop) {
        const float loopGain = monitor == MonitorMode::Both ? 0.70710678f
            : 0.0f;
        const float inputGain = monitor == MonitorMode::Both ? 0.70710678f
            : 1.0f;
        for (uint32_t channel = 0u; channel < kChannelCount; ++channel)
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame) {
                float source = 0.0f;
                if (copiedMonitorInput) source = instance.monitorScratch[
                    static_cast<std::size_t>(channel) * process->frames_count
                        + frame];
                else if (input && input->data32
                    && channel < input->channel_count
                    && input->data32[channel]) source = input->data32[channel][frame];
                pointers[channel][frame] = pointers[channel][frame] * loopGain
                    + source * inputGain;
            }
    }
    const auto outputFormat = s3g::sanitizeRingOutputFormat(
        instance.targets.outputFormat.load());
    instance.outputMixdown.configure(outputFormat,
        instance.targets.outputRotation.load());
    if (outputFormat != s3g::RingOutputFormat::Direct8) {
        std::array<float, kChannelCount> direct {};
        std::array<float, kChannelCount> rendered {};
        for (uint32_t frame = 0u; frame < process->frames_count; ++frame) {
            for (uint32_t channel = 0u; channel < kChannelCount; ++channel)
                direct[channel] = pointers[channel][frame];
            instance.outputMixdown.processFrame(direct.data(),
                rendered.data());
            for (uint32_t channel = 0u; channel < kChannelCount; ++channel)
                pointers[channel][frame] = rendered[channel];
        }
    }
    const auto& cursors = instance.engine.cursors();
    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        instance.headPhases[head].store(cursors[head].phaseA,
            std::memory_order_relaxed);
        instance.headPhaseTargets[head].store(cursors[head].phaseB,
            std::memory_order_relaxed);
        instance.headRates[head].store(cursors[head].rate,
            std::memory_order_relaxed);
        instance.headRings[head].store(cursors[head].ringA,
            std::memory_order_relaxed);
        instance.headRingTargets[head].store(cursors[head].ringB,
            std::memory_order_relaxed);
        instance.headSlots[head].store(cursors[head].sourceA,
            std::memory_order_relaxed);
        instance.headSlotTargets[head].store(cursors[head].sourceB,
            std::memory_order_relaxed);
        instance.headChannels[head].store(cursors[head].channelA,
            std::memory_order_relaxed);
        instance.headChannelTargets[head].store(cursors[head].channelB,
            std::memory_order_relaxed);
        instance.headRingMixes[head].store(cursors[head].sourceMix,
            std::memory_order_relaxed);
        instance.headRadialPositions[head].store(cursors[head].radialPosition,
            std::memory_order_relaxed);
        instance.headFormationLeaders[head].store(
            cursors[head].formationLeader,
            std::memory_order_relaxed);
    }
    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < kChannelCount; ++channel)
        for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
            peak = std::max(peak, std::abs(pointers[channel][frame]));
    instance.outputPeak.store(std::max(peak,
        instance.outputPeak.load(std::memory_order_relaxed) * 0.90f),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (instance.captureCallbackPending.load(std::memory_order_acquire))
        finalizeCaptures(instance);
#if defined(__APPLE__)
    serviceLoads(instance);
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name, isInput ? "8ch Capture / Through In"
        : "Sample Rings Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet,
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{ return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI Trigger In", sizeof(info->name));
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

std::vector<ParamDef> makeParamDefs()
{
    std::vector<ParamDef> result {
        { kPlaybackRateParamId, "Playback Rate", "Transport", 0.125, 4.0, 1.0, false },
        { kRingPathParamId, "Radial Path", "Ring Motion", 0.0, 7.0, 3.0, true },
        { kRelationshipParamId, "Head Relationship", "Heads", 0.0, 7.0, 1.0, true },
        { kRelationshipAmountParamId, "Relationship Amount", "Heads", -1.0, 1.0, 0.5, false },
        { kDriftParamId, "Head Drift", "Heads", 0.0, 1.0, 0.0, false },
        { kCenterParamId, "Head Pivot", "Heads", 0.0, 1.0, 0.5, false },
        { kGlideParamId, "Relationship Glide", "Heads", 0.0, 2000.0, 180.0, false },
        { kRingBlendParamId, "Ring Blend", "Ring Motion", 0.0, 1.0, 1.0, false },
        { kFormationParamId, "Head Formation", "Heads", 0.0, 3.0, 3.0, true },
        { kLoopJoinParamId, "Loop Join", "Loop", 0.0, 0.5, 0.04, false },
        { kSeamDuckParamId, "Seam Duck", "Loop", 0.0, 0.75, 0.0, false },
        { kHeadMaskParamId, "Head Mask", "Heads", 0.0, 255.0, 255.0, true },
        { kOutputGainParamId, "Output Gain", "Output", -60.0, 12.0, -9.0, false },
        { kOutputFormatParamId, "Output Format", "Output", 0.0, 2.0, 0.0, true },
        { kOutputRotationParamId, "Output Rotation", "Output", -180.0, 180.0, 0.0, false },
        { kPlayingParamId, "Play", "Transport", 0.0, 1.0, 1.0, true },
        { kCaptureTargetParamId, "Capture Target", "Capture", 0.0, 3.0, 0.0, true },
        { kCaptureGateParamId, "Capture", "Capture", 0.0, 1.0, 0.0, true },
        { kCaptureModeParamId, "Capture Mode", "Capture", 0.0, 2.0, 0.0, true },
        { kInputWidthParamId, "Input Width", "Capture", 0.0, 8.0, 0.0, true },
        { kCaptureQuantizeParamId, "Capture Quantize", "Capture", 0.0, 5.0, 0.0, true },
        { kMonitorParamId, "Monitor", "Capture", 0.0, 2.0, 0.0, true },
        { kSelectedSlotParamId, "Selected Slot", "Editor", 0.0, 3.0, 0.0, true },
        { kMidiModeParamId, "MIDI Mode", "MIDI", 0.0, 2.0, 0.0, true },
        { kMidiRootParamId, "MIDI Root", "MIDI", 0.0, 127.0, 60.0, true },
        { kOverdubFeedbackParamId, "Overdub Feedback", "Capture", 0.0, 1.0, 0.75, false },
        { kSeedParamId, "Motion Seed", "Ring Motion", 1.0, 999999.0, 4312.0, true },
        { kRingPositionParamId, "Ring Position", "Ring Motion", 0.0, 1.0, 0.5, false },
        { kRadialRatioParamId, "Radial Ratio", "Ring Motion", 0.0, 4.0, 1.0, false },
        { kPathDepthParamId, "Path Depth", "Ring Motion", 0.0, 1.0, 1.0, false },
        { kPathOffsetParamId, "Path Offset", "Ring Motion", 0.0, 1.0, 0.0, false },
        { kPathSpreadParamId, "Formation Spread", "Ring Motion", 0.0, 1.0, 0.0, false },
        { kPathSlewParamId, "Path Slew", "Ring Motion", 0.0, 2000.0, 80.0, false },
        { kRadialReverseParamId, "Reverse Radial Path", "Ring Motion", 0.0, 1.0, 0.0, true },
        { kAngularReverseParamId, "Reverse Angular Motion", "Heads", 0.0, 1.0, 0.0, true },
    };
    constexpr const char* slotNames[] {
        "Start", "End", "Speed", "Stretch", "Pitch", "Wrap Nudge",
        "Gain", "Reverse",
    };
    constexpr double minima[] { 0.0, 0.001, 0.125, 0.25, -48.0,
        -0.5, -60.0, 0.0 };
    constexpr double maxima[] { 0.999, 1.0, 4.0, 4.0, 48.0,
        0.5, 12.0, 1.0 };
    constexpr double defaults[] { 0.0, 1.0, 1.0, 1.0, 0.0,
        0.0, -6.0, 0.0 };
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        for (clap_id offset = 0u; offset <= kSlotReverse; ++offset) {
            std::string name = "Slot ";
            name += static_cast<char>('A' + slot);
            name += " ";
            name += slotNames[offset];
            char* stable = new char[name.size() + 1u];
            std::memcpy(stable, name.c_str(), name.size() + 1u);
            result.push_back({ slotParamId(slot, offset), stable, "Slots",
                minima[offset], maxima[offset], defaults[offset],
                offset == kSlotReverse });
        }
    }
    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        result.push_back({ kManualRingParamBase + head,
            "Manual Head Ring", "Manual Heads", 0.0, 1.0,
            static_cast<double>(head) / (kHeadCount - 1u), false });
        result.push_back({ kManualPhaseParamBase + head,
            "Manual Head Phase", "Manual Heads", 0.0, 1.0,
            static_cast<double>(head) / kHeadCount, false });
        result.push_back({ kManualRateParamBase + head,
            "Manual Head Rate", "Manual Heads", -4.0, 4.0, 1.0, false });
    }
    return result;
}

const std::vector<ParamDef>& paramDefs()
{
    static const std::vector<ParamDef> definitions = makeParamDefs();
    return definitions;
}

const ParamDef* paramDef(clap_id id)
{
    const auto& definitions = paramDefs();
    const auto found = std::find_if(definitions.begin(), definitions.end(),
        [id](const ParamDef& definition) { return definition.id == id; });
    return found == definitions.end() ? nullptr : &*found;
}

uint32_t paramsCount(const clap_plugin_t*)
{ return static_cast<uint32_t>(paramDefs().size()); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= paramDefs().size()) return false;
    const auto& definition = paramDefs()[index];
    info->id = definition.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (definition.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, definition.name, sizeof(info->name));
    std::strncpy(info->module, definition.module, sizeof(info->module));
    info->min_value = definition.minimum;
    info->max_value = definition.maximum;
    info->default_value = definition.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramDef(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kRingPathParamId)
        std::snprintf(display, size, "%s", ringPathName(
            static_cast<uint32_t>(std::lround(value))));
    else if (id == kRelationshipParamId)
        std::snprintf(display, size, "%s", relationshipName(
            static_cast<uint32_t>(std::lround(value))));
    else if (id == kFormationParamId)
        std::snprintf(display, size, "%s", formationName(
            static_cast<uint32_t>(std::lround(value))));
    else if (id == kCaptureModeParamId)
        std::snprintf(display, size, "%s", captureModeName(
            static_cast<uint32_t>(std::lround(value))));
    else if (id == kCaptureQuantizeParamId)
        std::snprintf(display, size, "%s", captureQuantizeName(
            static_cast<uint32_t>(std::lround(value))));
    else if (id == kMonitorParamId)
        std::snprintf(display, size, "%s", monitorName(
            static_cast<uint32_t>(std::lround(value))));
    else if (id == kMidiModeParamId)
        std::snprintf(display, size, "%s", midiModeName(
            static_cast<uint32_t>(std::lround(value))));
    else if (id == kOutputFormatParamId)
        std::snprintf(display, size, "%s", s3g::ringOutputFormatName(
            s3g::sanitizeRingOutputFormat(static_cast<uint32_t>(
                std::lround(value)))));
    else if (id == kInputWidthParamId)
        value < 0.5 ? std::snprintf(display, size, "AUTO")
            : std::snprintf(display, size, "%.0f", value);
    else if (id == kCaptureTargetParamId || id == kSelectedSlotParamId)
        std::snprintf(display, size, "%c", static_cast<char>('A'
            + std::clamp(std::lround(value), 0l, 3l)));
    else if (id == kOutputGainParamId || id == kOutputRotationParamId) {
        if (id == kOutputGainParamId)
            std::snprintf(display, size, "%+.1f dB", value);
        else std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kGlideParamId || id == kPathSlewParamId)
        std::snprintf(display, size, "%.0f ms", value);
    else if (id >= kManualPhaseParamBase
        && id < kManualPhaseParamBase + kHeadCount) {
        double degrees = value * 360.0;
        if (degrees > 180.0) degrees -= 360.0;
        std::snprintf(display, size, "%+.1f deg", degrees);
    } else if (id >= kManualRateParamBase
        && id < kManualRateParamBase + kHeadCount)
        std::snprintf(display, size, "%+.3fx", value);
    else if (id == kRadialRatioParamId || id == kPlaybackRateParamId)
        std::snprintf(display, size, "%.3fx", value);
    else if (id == kRelationshipAmountParamId)
        std::snprintf(display, size, "%+.3f", value);
    else if (id == kMidiRootParamId)
        std::snprintf(display, size, "%.0f", value);
    else if (id == kSeedParamId)
        std::snprintf(display, size, "%.0f", value);
    else if (id == kHeadMaskParamId)
        std::snprintf(display, size, "0x%02X",
            static_cast<uint32_t>(std::lround(value)));
    else if (id == kPlayingParamId || id == kCaptureGateParamId
        || id == kRadialReverseParamId || id == kAngularReverseParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else {
        uint32_t slot = 0u;
        clap_id offset = 0u;
        if (decodeSlotParam(id, slot, offset)
            && (offset == kSlotPitch || offset == kSlotGain))
            std::snprintf(display, size, "%+.1f%s", value,
                offset == kSlotGain ? " dB" : " st");
        else std::snprintf(display, size, "%.3f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !paramDef(id)) return false;

    const auto matchNames = [display, value](uint32_t count,
        const auto& nameForValue) {
        for (uint32_t index = 0u; index < count; ++index) {
            if (std::strcmp(display, nameForValue(index)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    };
    if (id == kRingPathParamId
        && matchNames(8u, [](uint32_t index) {
            return ringPathName(index);
        })) return true;
    if (id == kRelationshipParamId
        && matchNames(8u, [](uint32_t index) {
            return relationshipName(index);
        })) return true;
    if (id == kFormationParamId
        && matchNames(4u, [](uint32_t index) {
            return formationName(index);
        })) return true;
    if (id == kCaptureModeParamId
        && matchNames(3u, [](uint32_t index) {
            return captureModeName(index);
        })) return true;
    if (id == kCaptureQuantizeParamId
        && matchNames(6u, [](uint32_t index) {
            return captureQuantizeName(index);
        })) return true;
    if (id == kMonitorParamId
        && matchNames(3u, [](uint32_t index) {
            return monitorName(index);
        })) return true;
    if (id == kMidiModeParamId
        && matchNames(3u, [](uint32_t index) {
            return midiModeName(index);
        })) return true;
    if (id == kOutputFormatParamId
        && matchNames(s3g::kRingOutputFormatCount, [](uint32_t index) {
            return s3g::ringOutputFormatName(
                s3g::sanitizeRingOutputFormat(index));
        })) return true;

    const bool slotLetter = id == kCaptureTargetParamId
        || id == kSelectedSlotParamId;
    if (slotLetter && display[0] >= 'A' && display[0] <= 'D'
        && display[1] == '\0') {
        *value = static_cast<double>(display[0] - 'A');
        return true;
    }
    if (id == kInputWidthParamId && std::strcmp(display, "AUTO") == 0) {
        *value = 0.0;
        return true;
    }
    if ((id == kPlayingParamId || id == kCaptureGateParamId
            || id == kRadialReverseParamId || id == kAngularReverseParamId)
        && (std::strcmp(display, "ON") == 0
            || std::strcmp(display, "OFF") == 0)) {
        *value = std::strcmp(display, "ON") == 0 ? 1.0 : 0.0;
        return true;
    }
    if (id == kHeadMaskParamId) {
        char* end = nullptr;
        const auto parsed = std::strtoul(display, &end, 0);
        if (end != display) {
            *value = static_cast<double>(parsed);
            return true;
        }
        return false;
    }

    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || !std::isfinite(parsed)) return false;
    if (id >= kManualPhaseParamBase
        && id < kManualPhaseParamBase + kHeadCount) {
        double degrees = std::fmod(parsed, 360.0);
        if (degrees < 0.0) degrees += 360.0;
        *value = degrees / 360.0;
        return true;
    }
    *value = parsed;
    return true;
}

void requestGuiParamService(Plugin& plugin) noexcept
{
    if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
    if (plugin.hostParams && plugin.hostParams->request_flush)
        plugin.hostParams->request_flush(plugin.host);
}

void requestTransportCommand(Plugin& plugin,
    TransportCommand command) noexcept
{
    plugin.transportCommand.store(static_cast<uint32_t>(command),
        std::memory_order_release);
    if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
    if (pending.kind != s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_gesture_t event {};
        event.header.size = sizeof(event);
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = pending.kind
                == s3g::clap_gui::ParamEventKind::GestureBegin
            ? CLAP_EVENT_PARAM_GESTURE_BEGIN
            : CLAP_EVENT_PARAM_GESTURE_END;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        return output->try_push(output, &event.header);
    }
    clap_event_param_value_t event {};
    event.header.size = sizeof(event);
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = pending.value;
    return output->try_push(output, &event.header);
}

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output) noexcept
{
    if (plugin.guiParamConsumer.test_and_set(std::memory_order_acquire))
        return;
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            setParam(plugin, pending.paramId, pending.value);
        plugin.guiParamEvents.pop();
    }
    plugin.guiParamConsumer.clear(std::memory_order_release);
}

void queueGuiParamGesture(Plugin& plugin, clap_id id, double value)
{
    const auto* definition = paramDef(id);
    if (!definition) return;
    value = std::clamp(value, definition->minimum, definition->maximum);
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, value },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size()))
        return;
    setParam(plugin, id, value);
    requestGuiParamService(plugin);
}

void queueGuiParamBegin(Plugin& plugin, clap_id id)
{
    if (!paramDef(id)) return;
    if (plugin.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 }))
        requestGuiParamService(plugin);
}

void queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    const auto* definition = paramDef(id);
    if (!definition) return;
    value = std::clamp(value, definition->minimum, definition->maximum);
    if (!plugin.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) return;
    setParam(plugin, id, value);
    requestGuiParamService(plugin);
}

void queueGuiParamEnd(Plugin& plugin, clap_id id)
{
    if (!paramDef(id)) return;
    if (plugin.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 }))
        requestGuiParamService(plugin);
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto& instance = *self(plugin);
    readEvents(instance, input);
    serviceGuiParamEvents(instance, output);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    std::array<double, 35u> globals {};
    std::array<std::array<double, 8u>, kSourceCount> slots {};
    std::array<double, kHeadCount> manualRings {};
    std::array<double, kHeadCount> manualPhases {};
    std::array<double, kHeadCount> manualRates {};
    std::array<std::array<char, 1024u>, kSourceCount> paths {};
    std::array<uint8_t, kSourceCount> embedded {};
    std::array<uint8_t, kSourceCount> channels {};
    std::array<double, kSourceCount> sampleRates {};
    std::array<uint32_t, kSourceCount> frames {};
    uint8_t storageMode = static_cast<uint8_t>(StorageMode::Project);
    std::array<uint8_t, 7u> reserved {};
};

struct PreviousSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kPreviousStateVersion;
    std::array<double, 35u> globals {};
    std::array<std::array<double, 8u>, kSourceCount> slots {};
    std::array<double, kHeadCount> manualRings {};
    std::array<double, kHeadCount> manualPhases {};
    std::array<double, kHeadCount> manualRates {};
    std::array<std::array<char, 1024u>, kSourceCount> paths {};
    std::array<uint8_t, kSourceCount> embedded {};
    std::array<uint8_t, kSourceCount> channels {};
    std::array<double, kSourceCount> sampleRates {};
    std::array<uint32_t, kSourceCount> frames {};
};

struct StatePrefix {
    uint32_t magic = 0u;
    uint32_t version = 0u;
};

static_assert(offsetof(SavedState, storageMode) == sizeof(PreviousSavedState));

constexpr std::array<clap_id, 35u> kGlobalStateIds {{
    kPlaybackRateParamId, kRingPathParamId, kRelationshipParamId,
    kRelationshipAmountParamId, kDriftParamId, kCenterParamId, kGlideParamId,
    kRingBlendParamId, kFormationParamId, kLoopJoinParamId,
    kSeamDuckParamId, kHeadMaskParamId, kOutputGainParamId,
    kOutputFormatParamId, kOutputRotationParamId, kPlayingParamId,
    kCaptureTargetParamId, kCaptureGateParamId, kCaptureModeParamId,
    kInputWidthParamId, kCaptureQuantizeParamId, kMonitorParamId,
    kSelectedSlotParamId, kMidiModeParamId, kMidiRootParamId,
    kOverdubFeedbackParamId,
    kSeedParamId,
    kRingPositionParamId, kRadialRatioParamId, kPathDepthParamId,
    kPathOffsetParamId, kPathSpreadParamId, kPathSlewParamId,
    kRadialReverseParamId,
    kAngularReverseParamId,
}};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto& instance = *self(plugin);
    finalizeCaptures(instance);
    SavedState saved;
    saved.storageMode = static_cast<uint8_t>(instance.storageMode);
    uint64_t embeddedBytes = 0u;
    for (std::size_t index = 0u; index < kGlobalStateIds.size(); ++index)
        saved.globals[index] = paramValue(instance, kGlobalStateIds[index]);
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        for (clap_id offset = 0u; offset <= kSlotReverse; ++offset)
            saved.slots[slot][offset] = paramValue(instance,
                slotParamId(slot, offset));
        std::string locator = instance.sourcePaths[slot];
        if (instance.storageMode == StorageMode::Project && locator.empty()
            && !instance.projectRelativePaths[slot].empty())
            locator = instance.projectRelativePaths[slot];
        if (instance.storageMode == StorageMode::Project
            && instance.projectFileRegistrations[slot].registered())
            locator = instance.projectFileRegistrations[slot].absolutePath();
        if (instance.storageMode == StorageMode::Project
            && !locator.empty()) {
            const ReaperContext context
                = s3g::sample_storage::reaperContext(instance.host);
            std::string relative;
            std::string error;
            if (s3g::sample_storage::makeProjectRelativePath(context,
                    locator, relative, &error)) locator = relative;
        }
        std::snprintf(saved.paths[slot].data(), saved.paths[slot].size(),
            "%s", locator.c_str());
        const auto asset = instance.sources[slot];
        const uint64_t bytes = asset ? static_cast<uint64_t>(
            asset->frameCount()) * asset->channelCount * sizeof(float) : 0u;
        const bool mustEmbed = asset
            && (instance.storageMode == StorageMode::Embed
                || locator.empty());
        if (mustEmbed) {
            if (embeddedBytes > kMaximumEmbeddedAudioBytes - bytes)
                return false;
            embeddedBytes += bytes;
            saved.embedded[slot] = 1u;
        }
        if (saved.embedded[slot]) {
            saved.channels[slot] = asset->channelCount;
            saved.sampleRates[slot] = asset->sampleRate;
            saved.frames[slot] = asset->frameCount();
        }
    }
    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        saved.manualRings[head] = paramValue(instance,
            kManualRingParamBase + head);
        saved.manualPhases[head] = paramValue(instance,
            kManualPhaseParamBase + head);
        saved.manualRates[head] = paramValue(instance,
            kManualRateParamBase + head);
    }
    if (!s3g::clap_state::writeAll(stream, &saved, sizeof(saved)))
        return false;
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        if (!saved.embedded[slot]) continue;
        const auto asset = instance.sources[slot];
        if (!asset) return false;
        for (uint8_t channel = 0u; channel < asset->channelCount; ++channel)
            if (!s3g::clap_state::writeAll(stream,
                    asset->channels[channel].data(),
                    asset->channels[channel].size() * sizeof(float)))
                return false;
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    StatePrefix prefix;
    if (!s3g::clap_state::readAll(stream, &prefix, sizeof(prefix))
        || prefix.magic != kStateMagic
        || (prefix.version != kStateVersion
            && prefix.version != kPreviousStateVersion)) return false;
    SavedState saved;
    saved.magic = prefix.magic;
    saved.version = prefix.version;
    if (prefix.version == kStateVersion) {
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&saved) + sizeof(prefix),
                sizeof(saved) - sizeof(prefix))) return false;
    } else {
        PreviousSavedState previous;
        previous.magic = prefix.magic;
        previous.version = prefix.version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&previous) + sizeof(prefix),
                sizeof(previous) - sizeof(prefix))) return false;
        saved.globals = previous.globals;
        saved.slots = previous.slots;
        saved.manualRings = previous.manualRings;
        saved.manualPhases = previous.manualPhases;
        saved.manualRates = previous.manualRates;
        saved.paths = previous.paths;
        saved.embedded = previous.embedded;
        saved.channels = previous.channels;
        saved.sampleRates = previous.sampleRates;
        saved.frames = previous.frames;
        const bool linked = std::any_of(previous.paths.begin(),
            previous.paths.end(), [](const auto& path) {
                return path[0u] != '\0';
            });
        saved.storageMode = static_cast<uint8_t>(linked
            ? StorageMode::Link : StorageMode::Project);
    }
    auto& instance = *self(plugin);
#if defined(__APPLE__)
    for (auto& generation : instance.loadGenerations) ++generation;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
    }
#endif
    instance.storageMode = s3g::sample_storage::sanitizeStorageMode(
        saved.storageMode);
    for (std::size_t index = 0u; index < kGlobalStateIds.size(); ++index)
        setParam(instance, kGlobalStateIds[index], saved.globals[index]);
    setParam(instance, kCaptureGateParamId, 0.0);
    uint64_t embeddedBytes = 0u;
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        if (saved.embedded[slot] == 0u) continue;
        const uint64_t bytes = static_cast<uint64_t>(saved.channels[slot])
            * saved.frames[slot] * sizeof(float);
        if (saved.channels[slot] == 0u
            || saved.channels[slot] > s3g::sample::kMaximumAudioChannels
            || saved.frames[slot] < 2u || !(saved.sampleRates[slot] > 0.0)
            || embeddedBytes > kMaximumEmbeddedAudioBytes - bytes)
            return false;
        embeddedBytes += bytes;
    }
    for (uint32_t slot = 0u; slot < kSourceCount; ++slot) {
        instance.projectFileRegistrations[slot].clear();
        for (clap_id offset = 0u; offset <= kSlotReverse; ++offset)
            setParam(instance, slotParamId(slot, offset),
                saved.slots[slot][offset]);
        std::shared_ptr<const SampleAsset> asset;
        const std::string locator(saved.paths[slot].data(), strnlen(
            saved.paths[slot].data(), saved.paths[slot].size()));
        std::string path = locator;
        if (instance.storageMode == StorageMode::Project && !path.empty()
            && !std::filesystem::path(path).is_absolute()) {
            std::string resolved;
            std::string error;
            const ReaperContext context
                = s3g::sample_storage::reaperContext(instance.host);
            if (s3g::sample_storage::resolveProjectRelativePath(context,
                    path, resolved, &error)) path = std::move(resolved);
            else path.clear();
        }
#if defined(__APPLE__)
        if (!path.empty() && saved.embedded[slot] == 0u)
            asset = readSampleFromPath(path);
#endif
        if (saved.embedded[slot]) {
            if (saved.channels[slot] == 0u
                || saved.channels[slot] > s3g::sample::kMaximumAudioChannels
                || saved.frames[slot] < 2u) return false;
            auto decoded = std::make_shared<SampleAsset>();
            decoded->sampleRate = saved.sampleRates[slot];
            decoded->channelCount = saved.channels[slot];
            for (uint8_t channel = 0u; channel < decoded->channelCount;
                 ++channel) {
                decoded->channels[channel].resize(saved.frames[slot]);
                if (!s3g::clap_state::readAll(stream,
                        decoded->channels[channel].data(),
                        decoded->channels[channel].size() * sizeof(float)))
                    return false;
            }
            if (!decoded->valid()) return false;
            asset = decoded;
        }
        instance.linkSourcePaths[slot]
            = instance.storageMode == StorageMode::Project
            ? (std::filesystem::path(locator).is_absolute()
                    ? locator : std::string())
            : path;
        instance.projectRelativePaths[slot]
            = instance.storageMode == StorageMode::Project
                && !locator.empty()
                && !std::filesystem::path(locator).is_absolute()
            ? locator : std::string();
        instance.sourceFileBytes[slot] = regularFileByteCount(path);
        instance.projectStoragePending[slot] = false;
        instance.projectCopyInFlight[slot] = false;
        if (asset) {
            const std::string name = path.empty()
                ? "RESTORED CAPTURE" : sourceDisplayName(path);
            publishSource(instance, slot, asset, path, name, false);
        } else {
            publishSource(instance, slot, nullptr, path, "EMPTY", false);
            if (instance.storageMode == StorageMode::Project
                && !locator.empty())
                instance.sourceStatuses[slot] = "PROJECT SOURCE OFFLINE";
        }
        if (instance.storageMode == StorageMode::Project
            && asset && !path.empty()) {
            const ReaperContext context
                = s3g::sample_storage::reaperContext(instance.host);
            ProjectLocation location;
            std::string relative;
            std::string error;
            const bool inProject
                = s3g::sample_storage::queryProjectLocation(context,
                    location, &error)
                && s3g::sample_storage::makeProjectRelativePath(location,
                    path, relative, &error);
            if (inProject) {
                instance.projectRelativePaths[slot] = relative;
                (void)instance.projectFileRegistrations[slot].reset(context,
                    path, nullptr, nullptr, instance.plugin.desc->name);
                updateSourceStatus(instance, slot);
            } else {
                instance.projectStoragePending[slot] = true;
                instance.nextProjectCopyProbe[slot]
                    = std::chrono::steady_clock::now();
                instance.sourceStatuses[slot]
                    = "PROJECT COPY PENDING";
            }
        }
    }
    for (uint32_t head = 0u; head < kHeadCount; ++head) {
        setParam(instance, kManualRingParamBase + head,
            saved.manualRings[head]);
        setParam(instance, kManualPhaseParamBase + head,
            saved.manualPhases[head]);
        setParam(instance, kManualRateParamBase + head,
            saved.manualRates[head]);
    }
    instance.transportStopped.store(false, std::memory_order_release);
    instance.transportCommand.store(
        static_cast<uint32_t>(TransportCommand::None),
        std::memory_order_release);
    instance.engine.resync(snapshotSettings(instance));
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
#include "s3g_sample_rings_gui.inc"
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-rings-8",
    "s3g Sample Rings 8",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Four file-or-capture loop slots driving an eight-head temporal matrix.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->hostState = host ? static_cast<const clap_host_state_t*>(
        host->get_extension(host, CLAP_EXT_STATE)) : nullptr;
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{ return index == 0u ? &descriptor : nullptr; }
const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
};
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
