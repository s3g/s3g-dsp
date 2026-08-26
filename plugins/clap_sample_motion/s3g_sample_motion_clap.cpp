#include "s3g_sample_motion.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_sample_storage.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/audio-ports-config.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <strings.h>
#include <thread>
#include <vector>

namespace {

using s3g::sample::MotionArticulation;
using s3g::sample::MotionEventKind;
using s3g::sample::MotionMode;
using s3g::sample::MotionRateBasis;
using s3g::sample::MotorEnvelopeShape;
using s3g::sample::OutputAssignmentEvent;
using s3g::sample::SegmentModel;
using s3g::sample::SegmentOverlap;
using s3g::sample::SegmentTrigger;
using s3g::sample::MotionRenderEvent;
using s3g::sample::MotionSettings;
using s3g::sample::SampleAsset;
using s3g::sample::TriggerMode;
using s3g::sample::VoiceMode;
using s3g::sample_storage::ProjectCopyResult;
using s3g::sample_storage::ProjectFileRegistration;
using s3g::sample_storage::ProjectLocation;
using s3g::sample_storage::ReaperContext;
using s3g::sample_storage::StorageMode;
using s3g::routing::OutputTraversal;
using s3g::routing::OutputVoiceWidth;
using s3g::routing::StereoPairLayout;

constexpr uint32_t kStateMagic = 0x4d533353u; // "S3SM"
constexpr uint32_t kStateVersion = 4u;
constexpr uint32_t kProjectStorageStateVersion = 3u;
constexpr uint32_t kGuiWidth = 980u;
constexpr uint32_t kGuiHeight = 844u;
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr std::size_t kMaximumBlockEvents = 2048u;
constexpr uint64_t kMaximumEmbeddedAudioBytes =
    1024ull * 1024ull * 1024ull;
constexpr uint32_t kActionPreview = 1u << 0u;
constexpr uint32_t kActionStopAll = 1u << 1u;
constexpr clap_id kStereoOutputConfigId = 3602u;
constexpr clap_id kThirtyTwoChannelOutputConfigId = 3632u;

constexpr clap_id kOutParamId = 1u;
constexpr clap_id kMotionParamId = 2u;
constexpr clap_id kArticulationParamId = 3u;
constexpr clap_id kStartParamId = 4u;
constexpr clap_id kEndParamId = 5u;
constexpr clap_id kLocusParamId = 6u;
constexpr clap_id kFieldParamId = 7u;
constexpr clap_id kMotionRateParamId = 8u;
constexpr clap_id kTravelParamId = 9u;
constexpr clap_id kJitterParamId = 10u;
constexpr clap_id kInnerRateParamId = 11u;
constexpr clap_id kOuterRateParamId = 12u;
constexpr clap_id kPacketDutyParamId = 13u;
constexpr clap_id kSymmetryParamId = 14u;
constexpr clap_id kJoinParamId = 15u;
constexpr clap_id kVoiceModeParamId = 16u;
constexpr clap_id kTriggerParamId = 17u;
constexpr clap_id kRootParamId = 18u;
constexpr clap_id kTuneParamId = 19u;
constexpr clap_id kFineParamId = 20u;
constexpr clap_id kAttackParamId = 21u;
constexpr clap_id kReleaseParamId = 22u;
constexpr clap_id kVelocityParamId = 23u;
constexpr clap_id kSeedParamId = 24u;
constexpr clap_id kMidiParamId = 25u;
constexpr clap_id kRateBasisParamId = 26u;
constexpr clap_id kMotorEnvelopeParamId = 27u;
constexpr clap_id kOutputTraversalParamId = 28u;
constexpr clap_id kOutputVoiceWidthParamId = 29u;
constexpr clap_id kStereoPairLayoutParamId = 30u;
constexpr clap_id kActiveOutputCountParamId = 31u;
constexpr clap_id kSegmentModelParamId = 32u;
constexpr clap_id kSegmentTriggerParamId = 33u;
constexpr clap_id kEventRateParamId = 34u;
constexpr clap_id kEventRepeatsParamId = 35u;
constexpr clap_id kEventStepParamId = 36u;
constexpr clap_id kEventPitchParamId = 37u;
constexpr clap_id kEventLevelParamId = 38u;
constexpr clap_id kEventCurveParamId = 39u;
constexpr clap_id kEventOverlapParamId = 40u;
constexpr clap_id kOutputAssignmentEventParamId = 41u;
constexpr clap_id kAvoidAdjacentParamId = 42u;
constexpr std::size_t kLegacyParamCount = 25u;
constexpr std::size_t kOriginalStereoParamCount = 27u;
constexpr std::size_t kPreviousParamCount = 31u;
constexpr std::size_t kStereoParamCount = 36u;
constexpr std::size_t kParamCount = 42u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, kParamCount> kParamDefs {{
    { kOutParamId, "Out", "Output", -60.0, 12.0, -6.0, false },
    { kMotionParamId, "Motion", "Motion", 0.0, 7.0, 0.0, true },
    { kArticulationParamId, "Articulation", "Pulse", 0.0, 2.0, 0.0,
        true },
    { kStartParamId, "Start", "Sample", 0.0, 1.0, 0.0, false },
    { kEndParamId, "End", "Sample", 0.0, 1.0, 1.0, false },
    { kLocusParamId, "Locus", "Motion", 0.0, 1.0, 0.5, false },
    { kFieldParamId, "Field", "Motion", 0.00001, 1.0, 0.25, false },
    { kMotionRateParamId, "Motion Rate", "Motion", 0.01, 80.0, 1.0,
        false },
    { kTravelParamId, "Travel", "Motion", 0.0, 1.0, 0.35, false },
    { kJitterParamId, "Jitter", "Motion", 0.0, 1.0, 0.0, false },
    { kInnerRateParamId, "Inner Rate", "Motor", 0.25, 200.0, 12.0,
        false },
    { kOuterRateParamId, "Outer Rate", "Motor", 0.05, 20.0, 1.0,
        false },
    { kPacketDutyParamId, "Packet Duty", "Motor", 0.02, 1.0, 0.65,
        false },
    { kSymmetryParamId, "Symmetry", "Motor", 0.05, 0.95, 0.5,
        false },
    { kJoinParamId, "Join", "Motion", 0.0, 1.0, 1.0, false },
    { kVoiceModeParamId, "Voice Mode", "Voice", 0.0, 2.0, 0.0, true },
    { kTriggerParamId, "Trigger", "Voice", 0.0, 3.0, 1.0, true },
    { kRootParamId, "Root Note", "Pitch", 0.0, 127.0, 60.0, true },
    { kTuneParamId, "Tune", "Pitch", -60.0, 60.0, 0.0, false },
    { kFineParamId, "Fine Tune", "Pitch", -100.0, 100.0, 0.0, false },
    { kAttackParamId, "Attack", "Amp", 0.0, 2.0, 0.003, false },
    { kReleaseParamId, "Release", "Amp", 0.0, 2.0, 0.020, false },
    { kVelocityParamId, "Velocity", "MIDI", 0.0, 1.0, 1.0, false },
    { kSeedParamId, "Seed", "Motion", 1.0, 65535.0, 1.0, true },
    { kMidiParamId, "MIDI Receive", "MIDI", 0.0, 16.0, 0.0, true },
    { kRateBasisParamId, "Rate Basis", "Motion", 0.0, 1.0, 0.0, true },
    { kMotorEnvelopeParamId, "Motor Envelope", "Motor", 0.0, 3.0, 1.0,
        true },
    { kOutputTraversalParamId, "Output Order", "Output Routing",
        0.0, 4.0, 0.0, true },
    { kOutputVoiceWidthParamId, "Voice Output", "Output Routing",
        0.0, 1.0, 1.0, true },
    { kStereoPairLayoutParamId, "Stereo Pair Map", "Output Routing",
        0.0, 1.0, 0.0, true },
    { kActiveOutputCountParamId, "Output Count", "Output Routing",
        2.0, 32.0, 32.0, true },
    { kSegmentModelParamId, "Segment Model", "Segment Events",
        0.0, 6.0, 0.0, true },
    { kSegmentTriggerParamId, "Segment Trigger", "Segment Events",
        0.0, 2.0, 0.0, true },
    { kEventRateParamId, "Event Rate", "Segment Events",
        0.25, 80.0, 4.0, false },
    { kEventRepeatsParamId, "Event Repeats", "Segment Events",
        1.0, 16.0, 2.0, true },
    { kEventStepParamId, "Step", "Segment Events",
        0.0, 1.0, 0.05, false },
    { kEventPitchParamId, "Pitch Scatter", "Segment Events",
        0.0, 12.0, 0.0, false },
    { kEventLevelParamId, "Level Variation", "Segment Events",
        0.0, 1.0, 0.15, false },
    { kEventCurveParamId, "Interval Curve", "Segment Events",
        -1.0, 1.0, 0.0, false },
    { kEventOverlapParamId, "Event Overlap", "Segment Events",
        0.0, 1.0, 0.0, true },
    { kOutputAssignmentEventParamId, "Route On", "Output Routing",
        0.0, 2.0, 0.0, true },
    { kAvoidAdjacentParamId, "Avoid Adjacent", "Output Routing",
        0.0, 1.0, 0.0, true },
}};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    uint32_t reservedHeader = 0u;
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 0u;
    uint8_t storageMode = static_cast<uint8_t>(StorageMode::Project);
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct LegacySavedStateV1 {
    uint32_t magic = kStateMagic;
    uint32_t version = 1u;
    uint32_t parameterCount = static_cast<uint32_t>(kLegacyParamCount);
    uint32_t reservedHeader = 0u;
    std::array<double, kLegacyParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct PreviousSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kProjectStorageStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kPreviousParamCount);
    uint32_t reservedHeader = 0u;
    std::array<double, kPreviousParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 1u;
    uint8_t channelCount = 0u;
    uint8_t storageMode = static_cast<uint8_t>(StorageMode::Project);
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct StateHeader {
    uint32_t magic = 0u;
    uint32_t version = 0u;
    uint32_t parameterCount = 0u;
    uint32_t reservedHeader = 0u;
};

static_assert(sizeof(StateHeader) == 16u);

#if defined(__APPLE__)
struct LoadRequest {
    uint64_t generation = 0u;
    std::string path;
    ProjectLocation projectLocation;
    std::string projectError;
    bool copyOnly = false;
};

struct LoadResult {
    uint64_t generation = 0u;
    std::string sourcePath;
    std::string decodedPath;
    std::shared_ptr<const SampleAsset> asset;
    std::string error;
    ProjectCopyResult projectCopy;
    uint64_t sourceFileBytes = 0u;
    bool copyOnly = false;
    bool projectLocationAvailable = false;
};
#endif

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    double sampleRate = 48000.0;
    uint32_t outputChannelCount = 2u;
    clap_id outputConfigId = kStereoOutputConfigId;
    uint32_t maximumFrames = 0u;
    s3g::sample::SampleMotionEngine engine;
    const SampleAsset* audioAsset = nullptr;
    std::atomic<const SampleAsset*> publishedAsset { nullptr };
    std::shared_ptr<const SampleAsset> controlAsset;
    std::vector<std::shared_ptr<const SampleAsset>> retainedAssets;
    std::mutex statusMutex;
    std::string samplePath;
    std::string linkSourcePath;
    std::string projectRelativePath;
    std::string status { "DROP A SAMPLE OR PRESS LOAD" };
    uint64_t sourceFileBytes = 0u;
    bool projectStoragePending = false;
    bool projectContextPending = false;
    bool projectCopyInFlight = false;
    std::chrono::steady_clock::time_point nextProjectCopyProbe {};
    std::array<std::atomic<double>, kParamCount> parameters {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::array<MotionRenderEvent, kMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>, s3g::sample::kMaximumMotionOutputChannels>
        scratch {};
    std::array<std::atomic<float>, s3g::sample::kMaximumMotionVoices>
        cursorPositions {};
    std::array<std::atomic<float>, s3g::sample::kMaximumMotionVoices>
        cursorMotionPhases {};
    std::array<std::atomic<uint8_t>, s3g::sample::kMaximumMotionVoices>
        cursorKeys {};
    std::array<std::atomic<float>, s3g::sample::kMaximumMotionVoices>
        cursorInnerPhases {};
    std::array<std::atomic<float>, s3g::sample::kMaximumMotionVoices>
        cursorOuterPhases {};
    std::array<std::atomic<uint64_t>, s3g::sample::kMaximumMotionVoices>
        cursorIdentities {};
    std::array<std::atomic<bool>, s3g::sample::kMaximumMotionVoices>
        cursorDirections {};
    std::array<std::atomic<bool>, s3g::sample::kMaximumMotionVoices>
        cursorPacketActive {};
    std::array<std::atomic<uint8_t>, s3g::sample::kMaximumMotionVoices>
        cursorOutputFirst {};
    std::array<std::atomic<uint8_t>, s3g::sample::kMaximumMotionVoices>
        cursorOutputSecond {};
    std::array<std::atomic<uint8_t>, s3g::sample::kMaximumMotionVoices>
        cursorOutputWidths {};
    std::atomic<uint32_t> cursorCount { 0u };
    std::atomic<uint32_t> activeVoiceCount { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> requestedActions { 0u };
    std::atomic<uint32_t> actionFeedback { 0u };
    std::atomic<uint64_t> cursorRevision { 0u };
    std::atomic<bool> processing { false };
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
    StorageMode storageMode = StorageMode::Project;
    ReaperContext reaperContext;
    ProjectFileRegistration projectFileRegistration;
    bool active = false;
#if defined(__APPLE__)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoadRequest> loadRequests;
    std::deque<LoadResult> loadResults;
    std::thread loaderThread;
    uint64_t loadGeneration = 0u;
    bool loaderStopping = false;
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

const ParamDef* paramDef(clap_id id) noexcept
{
    for (const auto& def : kParamDefs) if (def.id == id) return &def;
    return nullptr;
}

std::size_t paramIndex(clap_id id) noexcept
{
    return id >= kOutParamId && id <= kAvoidAdjacentParamId
        ? static_cast<std::size_t>(id - kOutParamId) : kParamCount;
}

bool isMultichannel(const Plugin& instance) noexcept
{ return instance.outputChannelCount == 32u; }

std::size_t visibleParamCount(const Plugin& instance) noexcept
{ return isMultichannel(instance) ? kParamCount : kStereoParamCount; }

bool paramIsVisible(const Plugin& instance, clap_id id) noexcept
{
    if (!paramDef(id)) return false;
    return isMultichannel(instance)
        || id <= kMotorEnvelopeParamId
        || (id >= kSegmentModelParamId && id <= kEventOverlapParamId);
}

const ParamDef* visibleParamDef(const Plugin& instance,
    uint32_t index) noexcept
{
    if (index >= visibleParamCount(instance)) return nullptr;
    if (isMultichannel(instance) || index < kOriginalStereoParamCount)
        return &kParamDefs[index];
    return &kParamDefs[kPreviousParamCount
        + index - kOriginalStereoParamCount];
}

double paramValue(const Plugin& instance, clap_id id) noexcept
{
    const auto index = paramIndex(id);
    return index < kParamCount
        ? instance.parameters[index].load(std::memory_order_acquire) : 0.0;
}

double clampParam(const ParamDef& def, double value) noexcept
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

void markStateDirty(Plugin& instance)
{
    if (instance.host && instance.hostState
        && instance.hostState->mark_dirty)
        instance.hostState->mark_dirty(instance.host);
}

void setParam(Plugin& instance, clap_id id, double value,
    bool dirty = false) noexcept
{
    const auto* def = paramDef(id);
    const auto index = paramIndex(id);
    if (!def || index >= kParamCount) return;
    value = clampParam(*def, value);
    constexpr double epsilon = 1.0e-5;
    if (id == kStartParamId)
        value = std::min(value, paramValue(instance, kEndParamId) - epsilon);
    else if (id == kEndParamId)
        value = std::max(value, paramValue(instance, kStartParamId) + epsilon);
    const double previous = instance.parameters[index].exchange(value,
        std::memory_order_acq_rel);
    const bool cursorContract = id == kMotionParamId
        || id == kArticulationParamId || id == kStartParamId
        || id == kEndParamId || id == kLocusParamId || id == kFieldParamId
        || id == kMotionRateParamId || id == kInnerRateParamId
        || id == kOuterRateParamId || id == kPacketDutyParamId
        || id == kSymmetryParamId || id == kRootParamId
        || id == kTuneParamId || id == kFineParamId
        || id == kRateBasisParamId || id == kMotorEnvelopeParamId
        || (id >= kSegmentModelParamId && id <= kAvoidAdjacentParamId);
    if (cursorContract && previous != value)
        instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    if (dirty && previous != value) markStateDirty(instance);
}

void initializeParams(Plugin& instance) noexcept
{
    for (const auto& def : kParamDefs)
        instance.parameters[paramIndex(def.id)].store(def.defaultValue,
            std::memory_order_relaxed);
}

MotionSettings settingsSnapshot(const Plugin& instance) noexcept
{
    MotionSettings settings;
    settings.motion = static_cast<MotionMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kMotionParamId))));
    settings.articulation = static_cast<MotionArticulation>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kArticulationParamId))));
    settings.rateBasis = static_cast<MotionRateBasis>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kRateBasisParamId))));
    settings.motorEnvelope = static_cast<MotorEnvelopeShape>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kMotorEnvelopeParamId))));
    settings.segmentModel = static_cast<SegmentModel>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kSegmentModelParamId))));
    settings.segmentTrigger = static_cast<SegmentTrigger>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kSegmentTriggerParamId))));
    settings.segmentOverlap = static_cast<SegmentOverlap>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kEventOverlapParamId))));
    settings.voiceMode = static_cast<VoiceMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kVoiceModeParamId))));
    settings.triggerMode = static_cast<TriggerMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kTriggerParamId))));
    settings.start = paramValue(instance, kStartParamId);
    settings.end = paramValue(instance, kEndParamId);
    settings.locus = paramValue(instance, kLocusParamId);
    settings.field = paramValue(instance, kFieldParamId);
    settings.motionRate = static_cast<float>(
        paramValue(instance, kMotionRateParamId));
    settings.travel = static_cast<float>(
        paramValue(instance, kTravelParamId));
    settings.jitter = static_cast<float>(
        paramValue(instance, kJitterParamId));
    settings.innerRateHz = static_cast<float>(
        paramValue(instance, kInnerRateParamId));
    settings.outerRateHz = static_cast<float>(
        paramValue(instance, kOuterRateParamId));
    settings.packetDuty = static_cast<float>(
        paramValue(instance, kPacketDutyParamId));
    settings.symmetry = static_cast<float>(
        paramValue(instance, kSymmetryParamId));
    settings.joinAmount = static_cast<float>(
        paramValue(instance, kJoinParamId));
    settings.eventRateHz = static_cast<float>(
        paramValue(instance, kEventRateParamId));
    settings.eventRepeats = static_cast<uint8_t>(std::lround(
        paramValue(instance, kEventRepeatsParamId)));
    settings.eventStep = static_cast<float>(
        paramValue(instance, kEventStepParamId));
    settings.eventPitchScatterSemitones = static_cast<float>(
        paramValue(instance, kEventPitchParamId));
    settings.eventLevelVariation = static_cast<float>(
        paramValue(instance, kEventLevelParamId));
    settings.eventIntervalCurve = static_cast<float>(
        paramValue(instance, kEventCurveParamId));
    settings.rootNote = static_cast<uint8_t>(std::lround(
        paramValue(instance, kRootParamId)));
    settings.tuneSemitones = static_cast<float>(
        paramValue(instance, kTuneParamId));
    settings.fineTuneCents = static_cast<float>(
        paramValue(instance, kFineParamId));
    settings.attackSeconds = static_cast<float>(
        paramValue(instance, kAttackParamId));
    settings.releaseSeconds = static_cast<float>(
        paramValue(instance, kReleaseParamId));
    settings.velocitySensitivity = static_cast<float>(
        paramValue(instance, kVelocityParamId));
    settings.outputGainDecibels = static_cast<float>(
        paramValue(instance, kOutParamId));
    settings.seed = static_cast<uint32_t>(std::lround(
        paramValue(instance, kSeedParamId)));
    if (isMultichannel(instance)) {
        settings.outputRouting.traversal = static_cast<OutputTraversal>(
            static_cast<uint8_t>(std::lround(
                paramValue(instance, kOutputTraversalParamId))));
        settings.outputRouting.width = static_cast<OutputVoiceWidth>(
            static_cast<uint8_t>(std::lround(
                paramValue(instance, kOutputVoiceWidthParamId))));
        settings.outputRouting.pairLayout = static_cast<StereoPairLayout>(
            static_cast<uint8_t>(std::lround(
                paramValue(instance, kStereoPairLayoutParamId))));
        settings.outputRouting.avoidAdjacent = paramValue(instance,
            kAvoidAdjacentParamId) >= 0.5;
        settings.activeOutputChannelCount = static_cast<uint32_t>(std::lround(
            paramValue(instance, kActiveOutputCountParamId)));
        settings.outputAssignmentEvent = static_cast<OutputAssignmentEvent>(
            static_cast<uint8_t>(std::lround(paramValue(instance,
                kOutputAssignmentEventParamId))));
    } else {
        settings.outputRouting.traversal = OutputTraversal::Sequential;
        settings.outputRouting.width = OutputVoiceWidth::Stereo;
        settings.outputRouting.pairLayout = StereoPairLayout::Adjacent;
        settings.outputRouting.avoidAdjacent = false;
        settings.activeOutputChannelCount = 2u;
        settings.outputAssignmentEvent = OutputAssignmentEvent::Note;
    }
    return settings;
}

void requestProcess(Plugin& instance)
{
    if (instance.host && instance.host->request_process)
        instance.host->request_process(instance.host);
}

void requestGuiParamService(Plugin& instance)
{
    if (instance.processing.load(std::memory_order_acquire)) return;
    if (instance.hostParams && instance.hostParams->request_flush)
        instance.hostParams->request_flush(instance.host);
    else requestProcess(instance);
}

std::string sampleDisplayName(const std::string& path)
{
    if (path.empty()) return "EMBEDDED SAMPLE";
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1u);
}

uint64_t regularFileByteCount(const std::string& path) noexcept
{
    if (path.empty()) return 0u;
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    return error || bytes > std::numeric_limits<uint64_t>::max()
        ? 0u : static_cast<uint64_t>(bytes);
}

uint64_t decodedPcmByteCount(const SampleAsset* asset) noexcept
{
    if (!asset) return 0u;
    return static_cast<uint64_t>(asset->channelCount)
        * asset->frameCount() * sizeof(float);
}

std::string storageDetail(Plugin& instance,
    const std::shared_ptr<const SampleAsset>& asset)
{
    StorageMode mode = StorageMode::Project;
    std::string path;
    uint64_t fileBytes = 0u;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        mode = instance.storageMode;
        path = mode == StorageMode::Project
                && !instance.projectRelativePath.empty()
            ? instance.projectRelativePath : instance.samplePath;
        fileBytes = instance.sourceFileBytes;
    }
    if (mode == StorageMode::Embed) {
        const uint64_t bytes = decodedPcmByteCount(asset.get());
        return bytes != 0u ? "PCM STATE "
                + s3g::sample_storage::formatByteCount(bytes)
            : "PCM STATE OFFLINE";
    }
    if (path.empty() && asset) {
        return "PCM SAFETY " + s3g::sample_storage::formatByteCount(
            decodedPcmByteCount(asset.get()));
    }
    const std::string bytes = fileBytes != 0u
        ? s3g::sample_storage::formatByteCount(fileBytes) : "OFFLINE";
    if (path.empty()) return bytes;
    return bytes + " / " + s3g::sample_storage::abbreviatedPath(path, 34u);
}

void serviceProjectFileRegistration(Plugin& instance)
{
    if (!instance.projectFileRegistration.registered()) return;
    const std::string absolutePath
        = instance.projectFileRegistration.absolutePath();
    if (absolutePath.empty()) return;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (instance.storageMode != StorageMode::Project
            || instance.samplePath == absolutePath) return;
    }
    const ReaperContext context = s3g::sample_storage::reaperContext(
        instance.host);
    std::string relative;
    std::string ignoredError;
    (void)s3g::sample_storage::makeProjectRelativePath(context,
        absolutePath, relative, &ignoredError);
    const uint64_t bytes = regularFileByteCount(absolutePath);
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (instance.storageMode != StorageMode::Project) return;
        instance.samplePath = absolutePath;
        if (!relative.empty()) instance.projectRelativePath = relative;
        instance.sourceFileBytes = bytes;
        instance.projectStoragePending = false;
        instance.projectContextPending = false;
        instance.projectCopyInFlight = false;
        instance.status = "PROJECT / " + sampleDisplayName(absolutePath);
    }
    markStateDirty(instance);
}

std::shared_ptr<const SampleAsset> currentAsset(Plugin& instance)
{
    std::lock_guard<std::mutex> lock(instance.statusMutex);
    return instance.controlAsset;
}

bool publishAsset(Plugin& instance, std::shared_ptr<const SampleAsset> asset,
    std::string path, bool dirty = true)
{
    if (asset && (!asset->valid() || asset->channelCount > 2u)) return false;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (asset) instance.retainedAssets.push_back(asset);
        instance.controlAsset = std::move(asset);
        instance.samplePath = std::move(path);
        instance.status = instance.controlAsset
            ? ((instance.controlAsset->channelCount == 1u
                    ? "MONO / " : "STEREO / ")
                + sampleDisplayName(instance.samplePath))
            : (instance.samplePath.empty()
                ? "DROP A MONO OR STEREO SAMPLE"
                : "SAMPLE OFFLINE / " + sampleDisplayName(
                    instance.samplePath));
        instance.publishedAsset.store(instance.controlAsset.get(),
            std::memory_order_release);
    }
    instance.cursorCount.store(0u, std::memory_order_release);
    instance.activeVoiceCount.store(0u, std::memory_order_release);
    instance.outputPeak.store(0.0f, std::memory_order_relaxed);
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

void queueGuiParamGesture(Plugin& instance, clap_id id, double value);

#if defined(__APPLE__)
bool decodeSampleFile(const std::string& path,
    std::shared_ptr<const SampleAsset>& assetOut, std::string& error)
{
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSError* nsError = nil;
        AVAudioFile* file = nsPath ? [[AVAudioFile alloc]
            initForReading:[NSURL fileURLWithPath:nsPath] error:&nsError]
            : nil;
        if (!file) {
            error = "COULD NOT OPEN SAMPLE";
            return false;
        }
        AVAudioFormat* format = [file processingFormat];
        const AVAudioChannelCount channels = [format channelCount];
        const AVAudioFramePosition fileFrames = [file length];
        if (channels < 1u || channels > 2u || fileFrames < 1
            || static_cast<uint64_t>(fileFrames)
                > std::numeric_limits<uint32_t>::max()) {
            error = "USE A MONO OR STEREO SAMPLE UNDER 2^32 FRAMES";
            return false;
        }
        const auto frames = static_cast<AVAudioFrameCount>(fileFrames);
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:format frameCapacity:frames];
        if (!buffer || ![file readIntoBuffer:buffer error:&nsError]
            || [buffer frameLength] == 0u || ![buffer floatChannelData]) {
            error = "SAMPLE DECODE FAILED";
            return false;
        }
        auto asset = std::make_shared<SampleAsset>();
        asset->sampleRate = [format sampleRate];
        asset->channelCount = static_cast<uint8_t>(channels);
        const uint32_t decodedFrames = [buffer frameLength];
        for (AVAudioChannelCount channel = 0u; channel < channels; ++channel)
            asset->channels[channel].assign(
                [buffer floatChannelData][channel],
                [buffer floatChannelData][channel] + decodedFrames);
        if (!asset->valid()) {
            error = "DECODED SAMPLE IS INVALID";
            return false;
        }
        assetOut = std::move(asset);
        error.clear();
        return true;
    }
}

void loaderMain(Plugin* instance)
{
    for (;;) {
        LoadRequest request;
        {
            std::unique_lock<std::mutex> lock(instance->loaderMutex);
            instance->loaderCondition.wait(lock, [instance] {
                return instance->loaderStopping
                    || !instance->loadRequests.empty();
            });
            if (instance->loaderStopping) return;
            request = std::move(instance->loadRequests.back());
            instance->loadRequests.clear();
        }
        LoadResult result;
        result.generation = request.generation;
        result.sourcePath = std::move(request.path);
        result.decodedPath = result.sourcePath;
        result.copyOnly = request.copyOnly;
        result.projectLocationAvailable
            = request.projectLocation.available();
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
            std::string decodeError;
            try {
                if (!decodeSampleFile(result.decodedPath, result.asset,
                        decodeError)) result.asset.reset();
            } catch (...) {
                result.asset.reset();
                decodeError = "SAMPLE DECODE EXCEEDED AVAILABLE MEMORY";
            }
            if (!result.asset) result.error = std::move(decodeError);
        }
        {
            std::lock_guard<std::mutex> lock(instance->loaderMutex);
            instance->loadResults.push_back(std::move(result));
        }
        if (instance->host && instance->host->request_callback)
            instance->host->request_callback(instance->host);
    }
}

bool startLoader(Plugin& instance)
{
    try { instance.loaderThread = std::thread(loaderMain, &instance); }
    catch (...) { return false; }
    return true;
}

void stopLoader(Plugin& instance)
{
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loaderStopping = true;
    }
    instance.loaderCondition.notify_all();
    if (instance.loaderThread.joinable()) instance.loaderThread.join();
}

void cancelSampleLoads(Plugin& instance)
{
    ++instance.loadGeneration;
    std::lock_guard<std::mutex> lock(instance.loaderMutex);
    instance.loadRequests.clear();
    instance.loadResults.clear();
    {
        std::lock_guard<std::mutex> statusLock(instance.statusMutex);
        instance.projectCopyInFlight = false;
    }
}

void queueSampleLoad(Plugin& instance, std::string path)
{
    LoadRequest request;
    request.generation = ++instance.loadGeneration;
    request.path = std::move(path);
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (instance.storageMode == StorageMode::Project) {
            instance.reaperContext = s3g::sample_storage::reaperContext(
                instance.host);
            (void)s3g::sample_storage::queryProjectLocation(
                instance.reaperContext, request.projectLocation,
                &request.projectError);
            instance.projectStoragePending = true;
            instance.projectContextPending
                = !request.projectLocation.available();
            // Suppress a timer retry until this source-load result has been
            // serviced, even if the project is not saved yet.
            instance.projectCopyInFlight = true;
        } else {
            instance.projectStoragePending = false;
            instance.projectContextPending = false;
            instance.projectCopyInFlight = false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
        instance.loadRequests.push_back(std::move(request));
    }
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.status = "DECODING SAMPLE...";
    }
    instance.loaderCondition.notify_one();
}

void queueProjectCopy(Plugin& instance)
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        path = instance.samplePath;
        instance.storageMode = StorageMode::Project;
        instance.projectStoragePending = true;
    }
    if (path.empty()) {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.projectContextPending = false;
        instance.projectCopyInFlight = false;
        instance.status = "PROJECT PENDING / LOAD A SAMPLE";
        return;
    }

    const ReaperContext context = s3g::sample_storage::reaperContext(
        instance.host);
    ProjectLocation location;
    std::string error;
    if (!s3g::sample_storage::queryProjectLocation(context, location,
            &error)) {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.reaperContext = context;
        instance.projectContextPending = true;
        instance.projectCopyInFlight = false;
        instance.status = "PROJECT PENDING/SAVE PROJECT FIRST";
        return;
    }

    std::string absolutePath = path;
    const std::filesystem::path filesystemPath(path);
    if (!filesystemPath.is_absolute()) {
        if (!s3g::sample_storage::resolveProjectRelativePath(location, path,
                absolutePath, &error)) {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.status = "PROJECT PENDING / " + error;
            return;
        }
    }
    std::string relativePath;
    if (s3g::sample_storage::makeProjectRelativePath(location, absolutePath,
            relativePath, &error)
        && regularFileByteCount(absolutePath) != 0u) {
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.reaperContext = context;
            instance.samplePath = absolutePath;
            instance.projectRelativePath = relativePath;
            instance.sourceFileBytes = regularFileByteCount(absolutePath);
            instance.projectStoragePending = false;
            instance.projectContextPending = false;
            instance.projectCopyInFlight = false;
            instance.status = "PROJECT / " + sampleDisplayName(absolutePath);
        }
        if (!currentAsset(instance)) {
            queueSampleLoad(instance, absolutePath);
            return;
        }
        if (!instance.projectFileRegistration.reset(context, absolutePath,
                nullptr, nullptr, instance.plugin.desc->name)) {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.projectStoragePending = false;
            instance.projectContextPending = false;
            instance.status = "PROJECT READY/REGISTRATION UNAVAILABLE";
        }
        markStateDirty(instance);
        return;
    }

    LoadRequest request;
    request.generation = ++instance.loadGeneration;
    request.path = std::move(absolutePath);
    request.projectLocation = std::move(location);
    request.copyOnly = true;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
        instance.loadRequests.push_back(std::move(request));
    }
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.reaperContext = context;
        instance.projectContextPending = false;
        instance.projectCopyInFlight = true;
        instance.status = "COPYING SAMPLE TO PROJECT...";
    }
    instance.loaderCondition.notify_one();
}

void maybeRetryPendingProjectStorage(Plugin& instance)
{
    bool retry = false;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        const auto now = std::chrono::steady_clock::now();
        if (instance.storageMode == StorageMode::Project
            && instance.projectStoragePending
            && instance.projectContextPending
            && !instance.projectCopyInFlight && instance.controlAsset
            && !instance.samplePath.empty()
            && now >= instance.nextProjectCopyProbe) {
            instance.nextProjectCopyProbe = now + std::chrono::seconds(1);
            retry = true;
        }
    }
    if (retry) queueProjectCopy(instance);
}

void serviceLoads(Plugin& instance)
{
    serviceProjectFileRegistration(instance);
    std::deque<LoadResult> results;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        results.swap(instance.loadResults);
    }
    for (auto& result : results) {
        if (result.generation != instance.loadGeneration) continue;
        StorageMode mode = StorageMode::Project;
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            mode = instance.storageMode;
            if (mode == StorageMode::Project || result.copyOnly)
                instance.projectCopyInFlight = false;
        }
        if (result.copyOnly) {
            if (mode != StorageMode::Project) continue;
            if (!result.projectCopy.success) {
                std::lock_guard<std::mutex> lock(instance.statusMutex);
                instance.projectStoragePending = true;
                instance.projectContextPending
                    = !result.projectLocationAvailable;
                instance.status = !result.projectLocationAvailable
                    ? "PROJECT PENDING/SAVE PROJECT FIRST"
                    : (result.error.empty() ? std::string(
                        "PROJECT COPY FAILED") : result.error);
                continue;
            }
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            {
                std::lock_guard<std::mutex> lock(instance.statusMutex);
                instance.reaperContext = context;
                instance.samplePath = result.projectCopy.absolutePath;
                instance.projectRelativePath = result.projectCopy.relativePath;
                instance.sourceFileBytes = result.projectCopy.byteCount;
                instance.projectStoragePending = false;
                instance.projectContextPending = false;
                instance.status = "PROJECT / " + sampleDisplayName(
                    result.projectCopy.absolutePath);
            }
            if (!instance.projectFileRegistration.reset(context,
                    result.projectCopy.absolutePath, nullptr, nullptr,
                    instance.plugin.desc->name)) {
                std::lock_guard<std::mutex> lock(instance.statusMutex);
                instance.projectStoragePending = false;
                instance.projectContextPending = false;
                instance.status = "PROJECT READY/REGISTRATION UNAVAILABLE";
            }
            markStateDirty(instance);
            continue;
        }
        if (!result.asset) {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.status = result.error.empty()
                ? "SAMPLE DECODE FAILED" : result.error;
            continue;
        }
        const auto bounds = s3g::sample::defaultSafeSampleBounds(
            *result.asset);
        const double frames = std::max<double>(1.0,
            result.asset->frameCount());
        const double start = bounds.startFrame / frames;
        const double end = bounds.endFrame / frames;
        queueGuiParamGesture(instance, kStartParamId, start);
        queueGuiParamGesture(instance, kEndParamId, end);
        queueGuiParamGesture(instance, kLocusParamId, 0.5 * (start + end));
        const bool projectCopied = mode == StorageMode::Project
            && result.projectCopy.success;
        const std::string publishedPath = projectCopied
            ? result.projectCopy.absolutePath : result.sourcePath;
        publishAsset(instance, std::move(result.asset), publishedPath);
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.linkSourcePath = result.sourcePath;
            instance.sourceFileBytes = projectCopied
                ? result.projectCopy.byteCount : result.sourceFileBytes;
            instance.projectRelativePath = projectCopied
                ? result.projectCopy.relativePath : std::string {};
            instance.projectStoragePending = mode == StorageMode::Project
                && !projectCopied;
            instance.projectContextPending = instance.projectStoragePending
                && !result.projectLocationAvailable;
            if (instance.projectStoragePending) {
                instance.status = !result.projectLocationAvailable
                    ? "PROJECT PENDING/SAVE PROJECT FIRST"
                    : (result.error.empty() ? std::string(
                        "PROJECT COPY FAILED") : result.error);
            }
        }
        if (mode == StorageMode::Project && projectCopied) {
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            {
                std::lock_guard<std::mutex> lock(instance.statusMutex);
                instance.reaperContext = context;
            }
            if (!instance.projectFileRegistration.reset(context,
                    publishedPath, nullptr, nullptr,
                    instance.plugin.desc->name)) {
                std::lock_guard<std::mutex> lock(instance.statusMutex);
                instance.projectStoragePending = false;
                instance.projectContextPending = false;
                instance.status = "PROJECT READY/REGISTRATION UNAVAILABLE";
            }
        } else {
            instance.projectFileRegistration.clear();
            if (mode == StorageMode::Project
                && result.projectCopy.error.empty()
                && result.error.empty()) queueProjectCopy(instance);
        }
    }
    maybeRetryPendingProjectStorage(instance);
}

void setStorageMode(Plugin& instance, StorageMode mode)
{
    mode = s3g::sample_storage::sanitizeStorageMode(
        static_cast<uint8_t>(mode));
    std::string externalPath;
    if (mode != StorageMode::Project) {
        std::string projectRelativePath;
        {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            if (instance.storageMode == mode) return;
            externalPath = !instance.linkSourcePath.empty()
                ? instance.linkSourcePath : instance.samplePath;
            projectRelativePath = instance.projectRelativePath;
        }
        if (!externalPath.empty()
            && !std::filesystem::path(externalPath).is_absolute()) {
            const ReaperContext context
                = s3g::sample_storage::reaperContext(instance.host);
            ProjectLocation location;
            std::string error;
            std::string resolved;
            const std::string& locator = !projectRelativePath.empty()
                ? projectRelativePath : externalPath;
            if (s3g::sample_storage::queryProjectLocation(context, location,
                    &error)
                && s3g::sample_storage::resolveProjectRelativePath(location,
                    locator, resolved, &error)) externalPath = resolved;
        }
    }
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (instance.storageMode == mode) return;
        instance.storageMode = mode;
        instance.projectStoragePending = mode == StorageMode::Project;
        if (mode != StorageMode::Project) {
            instance.projectContextPending = false;
            instance.projectCopyInFlight = false;
            if (!externalPath.empty()) instance.samplePath = externalPath;
            instance.projectRelativePath.clear();
            instance.status = std::string(
                s3g::sample_storage::storageModeName(mode)) + " / "
                + sampleDisplayName(instance.samplePath);
        }
    }
    if (mode == StorageMode::Project) queueProjectCopy(instance);
    else instance.projectFileRegistration.clear();
    markStateDirty(instance);
}
#endif

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

void serviceGuiParamEvents(Plugin& instance,
    const clap_output_events_t* output) noexcept
{
    if (instance.guiParamConsumer.test_and_set(std::memory_order_acquire))
        return;
    s3g::clap_gui::ParamEvent pending {};
    while (instance.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            setParam(instance, pending.paramId, pending.value);
        instance.guiParamEvents.pop();
    }
    instance.guiParamConsumer.clear(std::memory_order_release);
}

void queueGuiParamGesture(Plugin& instance, clap_id id, double value)
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, clampParam(*def, value) },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!instance.guiParamEvents.pushBatch(events.data(), events.size()))
        return;
    setParam(instance, id, events[1u].value, true);
    requestGuiParamService(instance);
}

void queueGuiParamBegin(Plugin& instance, clap_id id)
{
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 }))
        requestGuiParamService(instance);
}

void queueGuiParamValue(Plugin& instance, clap_id id, double value)
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    value = clampParam(*def, value);
    if (!instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) return;
    setParam(instance, id, value, true);
    requestGuiParamService(instance);
}

void queueGuiParamEnd(Plugin& instance, clap_id id)
{
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 }))
        requestGuiParamService(instance);
}

bool midiChannelAccepted(const Plugin& instance, uint8_t channel) noexcept
{
    const int configured = static_cast<int>(std::lround(
        paramValue(instance, kMidiParamId)));
    return configured == 0 || configured == static_cast<int>(channel) + 1;
}

void applyMidiController(Plugin& instance, uint8_t controller,
    uint8_t value) noexcept
{
    const double normalized = static_cast<double>(value) / 127.0;
    switch (controller) {
    case 16u: setParam(instance, kStartParamId, normalized); break;
    case 17u: setParam(instance, kEndParamId, normalized); break;
    case 18u: setParam(instance, kLocusParamId, normalized); break;
    case 19u: setParam(instance, kFieldParamId, normalized); break;
    case 20u: setParam(instance, kMotionRateParamId,
        0.01 * std::pow(8000.0, normalized)); break;
    case 21u: setParam(instance, kTravelParamId, normalized); break;
    case 22u: setParam(instance, kInnerRateParamId,
        0.25 * std::pow(800.0, normalized)); break;
    case 23u: setParam(instance, kOuterRateParamId,
        0.05 * std::pow(400.0, normalized)); break;
    case 123u: break;
    default: break;
    }
}

void requestAction(Plugin& instance, uint32_t action) noexcept
{
    instance.requestedActions.fetch_or(action, std::memory_order_release);
    instance.actionFeedback.fetch_or(action, std::memory_order_release);
    requestProcess(instance);
}

std::size_t collectRenderEvents(Plugin& instance,
    const clap_input_events_t* input, uint32_t frameCount) noexcept
{
    std::size_t resultCount = 0u;
    auto append = [&](uint32_t frame, MotionEventKind kind, uint64_t noteId,
                      uint8_t key, float velocity, uint8_t channel) {
        if (resultCount >= instance.blockEvents.size()) return;
        instance.blockEvents[resultCount++] = {
            std::min(frame, frameCount), kind, noteId, key,
            velocity, channel,
        };
    };
    const uint32_t actions = instance.requestedActions.exchange(
        0u, std::memory_order_acq_rel);
    if ((actions & kActionStopAll) != 0u)
        append(0u, MotionEventKind::StopAll, 0u, 0u, 0.0f, 0u);
    if ((actions & kActionPreview) != 0u)
        append(0u, MotionEventKind::Preview, 0xffffffffffffffffull,
            static_cast<uint8_t>(std::lround(
                paramValue(instance, kRootParamId))), 1.0f, 0u);
    if (!input || !input->size || !input->get) return resultCount;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* header = input->get(input, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (header->type == CLAP_EVENT_PARAM_VALUE
            && header->size >= sizeof(clap_event_param_value_t)) {
            const auto* event = reinterpret_cast<
                const clap_event_param_value_t*>(header);
            setParam(instance, event->param_id, event->value);
            continue;
        }
        if ((header->type == CLAP_EVENT_NOTE_ON
                || header->type == CLAP_EVENT_NOTE_OFF
                || header->type == CLAP_EVENT_NOTE_CHOKE)
            && header->size >= sizeof(clap_event_note_t)) {
            const auto* event = reinterpret_cast<const clap_event_note_t*>(
                header);
            if (event->channel < 0 || event->channel > 15
                || event->key < 0 || event->key > 127
                || !midiChannelAccepted(instance,
                    static_cast<uint8_t>(event->channel))) continue;
            append(header->time,
                header->type == CLAP_EVENT_NOTE_ON
                    ? MotionEventKind::NoteOn : MotionEventKind::NoteOff,
                event->note_id >= 0
                    ? static_cast<uint64_t>(
                        static_cast<uint32_t>(event->note_id)) + 1u
                    : 0u,
                static_cast<uint8_t>(event->key),
                static_cast<float>(event->velocity),
                static_cast<uint8_t>(event->channel));
            continue;
        }
        if (header->type != CLAP_EVENT_MIDI
            || header->size < sizeof(clap_event_midi_t)) continue;
        const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
        const uint8_t status = event->data[0u] & 0xf0u;
        const uint8_t channel = event->data[0u] & 0x0fu;
        if (!midiChannelAccepted(instance, channel)) continue;
        const uint8_t key = event->data[1u] & 0x7fu;
        const uint8_t value = event->data[2u] & 0x7fu;
        if (status == 0x90u && value != 0u)
            append(header->time, MotionEventKind::NoteOn, 0u, key,
                static_cast<float>(value) / 127.0f, channel);
        else if (status == 0x80u || (status == 0x90u && value == 0u))
            append(header->time, MotionEventKind::NoteOff, 0u, key,
                0.0f, channel);
        else if (status == 0xb0u) {
            if (key == 123u) {
                append(header->time, MotionEventKind::StopAll, 0u, 0u,
                    0.0f, channel);
                instance.actionFeedback.fetch_or(kActionStopAll,
                    std::memory_order_release);
            } else applyMidiController(instance, key, value);
        }
    }
    return resultCount;
}

bool pluginInit(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    initializeParams(instance);
    instance.retainedAssets.reserve(32u);
    instance.reaperContext = s3g::sample_storage::reaperContext(
        instance.host);
    if (instance.host && instance.host->get_extension) {
        instance.hostParams = static_cast<const clap_host_params_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_PARAMS));
        instance.hostState = static_cast<const clap_host_state_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_STATE));
    }
#if defined(__APPLE__)
    if (!startLoader(instance)) return false;
#endif
    return true;
}

void destroyGui(Plugin& instance);

void pluginDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance) return;
    instance->projectFileRegistration.clear();
#if defined(__APPLE__)
    destroyGui(*instance);
    stopLoader(*instance);
#endif
    delete instance;
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    instance.sampleRate = sampleRate;
    instance.maximumFrames = maximumFrames;
    instance.active = instance.engine.prepare(sampleRate,
        instance.outputChannelCount);
    if (!instance.active) return false;
    for (uint32_t channel = 0u;
         channel < instance.scratch.size(); ++channel) {
        if (channel < instance.outputChannelCount)
            instance.scratch[channel].assign(maximumFrames, 0.0f);
        else instance.scratch[channel].clear();
    }
    instance.audioAsset = nullptr;
    return instance.active;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.active = false;
    instance.processing.store(false, std::memory_order_release);
    instance.engine.reset();
    instance.audioAsset = nullptr;
    for (auto& channel : instance.scratch) channel.clear();
    std::lock_guard<std::mutex> lock(instance.statusMutex);
    instance.retainedAssets.clear();
    if (instance.controlAsset)
        instance.retainedAssets.push_back(instance.controlAsset);
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(true, std::memory_order_release);
    return true;
}
void pluginStopProcessing(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.processing.store(false, std::memory_order_release);
    instance.cursorCount.store(0u, std::memory_order_release);
    instance.activeVoiceCount.store(0u, std::memory_order_release);
}

void pluginReset(const clap_plugin_t* plugin)
{
    self(plugin)->engine.reset();
}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto& instance = *self(plugin);
    if (!process || process->frames_count > instance.maximumFrames
        || process->audio_outputs_count < 1u || !process->audio_outputs
        || process->audio_outputs[0u].channel_count
            < instance.outputChannelCount)
        return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(instance, process->out_events);

    const auto* asset = instance.publishedAsset.load(std::memory_order_acquire);
    if (asset != instance.audioAsset) {
        instance.audioAsset = asset;
        instance.engine.setAsset(instance.audioAsset);
    }
    const std::size_t eventCount = collectRenderEvents(instance,
        process->in_events, process->frames_count);
    const MotionSettings settings = settingsSnapshot(instance);
    std::array<float*, s3g::sample::kMaximumMotionOutputChannels> scratch {};
    for (uint32_t channel = 0u; channel < instance.outputChannelCount;
         ++channel) scratch[channel] = instance.scratch[channel].data();
    instance.engine.render(settings, instance.blockEvents.data(), eventCount,
        scratch.data(), instance.outputChannelCount, process->frames_count);
    const auto& cursors = instance.engine.voiceCursors();
    const uint32_t cursorCount = instance.engine.voiceCursorCount();
    for (uint32_t index = 0u;
         index < s3g::sample::kMaximumMotionVoices; ++index) {
        instance.cursorPositions[index].store(index < cursorCount
                ? cursors[index].sourcePositionNormalized : -1.0f,
            std::memory_order_release);
        instance.cursorMotionPhases[index].store(index < cursorCount
                ? cursors[index].motionPhase : 0.0f,
            std::memory_order_release);
        instance.cursorKeys[index].store(index < cursorCount
                ? cursors[index].key : 0u, std::memory_order_release);
        instance.cursorInnerPhases[index].store(index < cursorCount
                ? cursors[index].innerPhase : 0.0f,
            std::memory_order_release);
        instance.cursorOuterPhases[index].store(index < cursorCount
                ? cursors[index].outerPhase : 0.0f,
            std::memory_order_release);
        instance.cursorIdentities[index].store(index < cursorCount
                ? cursors[index].identity : 0u,
            std::memory_order_release);
        instance.cursorDirections[index].store(index < cursorCount
                ? cursors[index].directionForward : true,
            std::memory_order_release);
        instance.cursorPacketActive[index].store(index < cursorCount
                ? cursors[index].motorPacketActive : false,
            std::memory_order_release);
        instance.cursorOutputFirst[index].store(index < cursorCount
                ? cursors[index].outputFirstChannel : 0u,
            std::memory_order_release);
        instance.cursorOutputSecond[index].store(index < cursorCount
                ? cursors[index].outputSecondChannel : 0u,
            std::memory_order_release);
        instance.cursorOutputWidths[index].store(index < cursorCount
                ? cursors[index].outputChannelCount : 0u,
            std::memory_order_release);
    }
    instance.cursorCount.store(cursorCount, std::memory_order_release);
    instance.activeVoiceCount.store(instance.engine.activeVoiceCount(),
        std::memory_order_release);
    instance.outputPeak.store(instance.engine.outputPeak(),
        std::memory_order_release);
    auto& output = process->audio_outputs[0u];
    output.constant_mask = 0u;
    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        const float* source = channel < instance.outputChannelCount
            ? instance.scratch[channel].data() : nullptr;
        if (output.data32 && output.data32[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data32[channel][frame] = source ? source[frame] : 0.0f;
        } else if (output.data64 && output.data64[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data64[channel][frame] = source ? source[frame] : 0.0;
        } else return CLAP_PROCESS_ERROR;
    }
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    serviceLoads(*self(plugin));
#else
    (void)plugin;
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool fillOutputPortInfo(uint32_t channelCount, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    *info = {};
    info->id = 20u;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = channelCount;
    info->port_type = channelCount == 2u ? CLAP_PORT_STEREO : nullptr;
    info->in_place_pair = CLAP_INVALID_ID;
    std::snprintf(info->name, sizeof(info->name), "%s",
        channelCount == 2u ? "Stereo Out" : "32 Channel Out");
    return true;
}

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    return plugin && fillOutputPortInfo(self(plugin)->outputChannelCount,
        index, isInput, info);
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
};

uint32_t audioPortsConfigCount(const clap_plugin_t*) { return 1u; }
bool audioPortsConfigGet(const clap_plugin_t* plugin, uint32_t index,
    clap_audio_ports_config_t* config)
{
    if (!plugin || !config || index != 0u) return false;
    const auto& instance = *self(plugin);
    *config = {};
    config->id = instance.outputConfigId;
    std::snprintf(config->name, sizeof(config->name), "%s",
        isMultichannel(instance) ? "32 Channel" : "Stereo");
    config->output_port_count = 1u;
    config->has_main_output = true;
    config->main_output_channel_count = instance.outputChannelCount;
    config->main_output_port_type = isMultichannel(instance)
        ? nullptr : CLAP_PORT_STEREO;
    return true;
}
bool audioPortsConfigSelect(const clap_plugin_t* plugin, clap_id id)
{ return plugin && id == self(plugin)->outputConfigId; }
const clap_plugin_audio_ports_config_t audioPortsConfig {
    audioPortsConfigCount, audioPortsConfigGet, audioPortsConfigSelect,
};
clap_id audioPortsConfigCurrent(const clap_plugin_t* plugin)
{ return plugin ? self(plugin)->outputConfigId : CLAP_INVALID_ID; }
bool audioPortsConfigInfoGet(const clap_plugin_t* plugin, clap_id configId,
    uint32_t portIndex, bool isInput, clap_audio_port_info_t* info)
{
    return plugin && configId == self(plugin)->outputConfigId
        && fillOutputPortInfo(self(plugin)->outputChannelCount, portIndex,
            isInput, info);
}
const clap_plugin_audio_ports_config_info_t audioPortsConfigInfo {
    audioPortsConfigCurrent, audioPortsConfigInfoGet,
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1u : 0u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || !isInput || index != 0u) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "MIDI In");
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount,
    notePortsGet,
};

uint32_t noteNameCount(const clap_plugin_t* plugin)
{
    return currentAsset(*self(plugin)) ? 1u : 0u;
}

bool noteNameGet(const clap_plugin_t* plugin, uint32_t index,
    clap_note_name_t* noteName)
{
    if (!noteName || index != 0u || !currentAsset(*self(plugin)))
        return false;
    *noteName = {};
    noteName->port = 0;
    noteName->channel = -1;
    noteName->key = static_cast<int16_t>(std::lround(
        paramValue(*self(plugin), kRootParamId)));
    std::snprintf(noteName->name, sizeof(noteName->name), "%s",
        "MOTION ROOT");
    return true;
}

const clap_plugin_note_name_t noteNames {
    noteNameCount,
    noteNameGet,
};

uint32_t paramsCount(const clap_plugin_t* plugin)
{
    return plugin ? static_cast<uint32_t>(visibleParamCount(*self(plugin)))
        : 0u;
}

bool paramsGetInfo(const clap_plugin_t* plugin, uint32_t index,
    clap_param_info_t* info)
{
    if (!plugin || !info || index >= visibleParamCount(*self(plugin)))
        return false;
    const auto* visible = visibleParamDef(*self(plugin), index);
    if (!visible) return false;
    const auto& def = *visible;
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!plugin || !value || !paramIsVisible(*self(plugin), id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

const char* motionName(int value) noexcept
{
    constexpr std::array<const char*, 8u> names {{
        "Hover", "Mirror", "Drunk", "Zigzag", "Forward", "Reverse",
        "Moving Loop", "Round Trip",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 7))];
}

const char* articulationName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Continuous", "Motor", "Packets",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* rateBasisName(int value) noexcept
{ return value <= 0 ? "Normal" : "Hertz"; }

const char* motorEnvelopeName(int value) noexcept
{
    constexpr std::array<const char*, 4u> names {{
        "Linear", "Rounded", "Exponential", "Plateau",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 3))];
}

const char* segmentModelName(int value) noexcept
{
    constexpr std::array<const char*, 7u> names {{
        "Off", "Freeze", "Iterate", "Pulser", "Doublets", "Bounce",
        "Routed Iterate",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 6))];
}

const char* segmentTriggerName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Clock", "Packet", "Turn",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* segmentOverlapName(int value) noexcept
{ return value <= 0 ? "Cut" : "Layer"; }

const char* outputAssignmentEventName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Note", "Turn", "Segment",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* offOnName(int value) noexcept
{ return value <= 0 ? "Off" : "On"; }

const char* outputTraversalName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Sequential", "Reverse Sequential", "Palindrome", "Random",
        "Random Cycle",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}

const char* outputVoiceWidthName(int value) noexcept
{ return value <= 0 ? "Mono" : "Stereo Pair"; }

const char* stereoPairLayoutName(int value) noexcept
{ return value <= 0 ? "Adjacent" : "Split Banks"; }

const char* voiceName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Poly", "Mono", "Legato",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* triggerName(int value) noexcept
{
    constexpr std::array<const char*, 4u> names {{
        "Auto", "Gate", "One Shot", "Toggle",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 3))];
}

const char* midiName(int value, char* buffer, std::size_t size) noexcept
{
    if (value <= 0) return "Omni";
    std::snprintf(buffer, size, "Channel %d", std::clamp(value, 1, 16));
    return buffer;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!plugin || !display || size == 0u
        || !paramIsVisible(*self(plugin), id)) return false;
    if (id == kMotionParamId)
        std::snprintf(display, size, "%s", motionName(
            static_cast<int>(std::lround(value))));
    else if (id == kArticulationParamId)
        std::snprintf(display, size, "%s", articulationName(
            static_cast<int>(std::lround(value))));
    else if (id == kRateBasisParamId)
        std::snprintf(display, size, "%s", rateBasisName(
            static_cast<int>(std::lround(value))));
    else if (id == kMotorEnvelopeParamId)
        std::snprintf(display, size, "%s", motorEnvelopeName(
            static_cast<int>(std::lround(value))));
    else if (id == kSegmentModelParamId)
        std::snprintf(display, size, "%s", segmentModelName(
            static_cast<int>(std::lround(value))));
    else if (id == kSegmentTriggerParamId)
        std::snprintf(display, size, "%s", segmentTriggerName(
            static_cast<int>(std::lround(value))));
    else if (id == kEventOverlapParamId)
        std::snprintf(display, size, "%s", segmentOverlapName(
            static_cast<int>(std::lround(value))));
    else if (id == kOutputAssignmentEventParamId)
        std::snprintf(display, size, "%s", outputAssignmentEventName(
            static_cast<int>(std::lround(value))));
    else if (id == kAvoidAdjacentParamId)
        std::snprintf(display, size, "%s", offOnName(
            static_cast<int>(std::lround(value))));
    else if (id == kOutputTraversalParamId)
        std::snprintf(display, size, "%s", outputTraversalName(
            static_cast<int>(std::lround(value))));
    else if (id == kOutputVoiceWidthParamId)
        std::snprintf(display, size, "%s", outputVoiceWidthName(
            static_cast<int>(std::lround(value))));
    else if (id == kStereoPairLayoutParamId)
        std::snprintf(display, size, "%s", stereoPairLayoutName(
            static_cast<int>(std::lround(value))));
    else if (id == kActiveOutputCountParamId)
        std::snprintf(display, size, "%d CH", static_cast<int>(
            std::lround(value)));
    else if (id == kVoiceModeParamId)
        std::snprintf(display, size, "%s", voiceName(
            static_cast<int>(std::lround(value))));
    else if (id == kTriggerParamId)
        std::snprintf(display, size, "%s", triggerName(
            static_cast<int>(std::lround(value))));
    else if (id == kMidiParamId) {
        char buffer[32] {};
        std::snprintf(display, size, "%s", midiName(
            static_cast<int>(std::lround(value)), buffer, sizeof(buffer)));
    } else if (id == kOutParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kMotionRateParamId)
        std::snprintf(display, size,
            paramValue(*self(plugin), kRateBasisParamId) < 0.5
                ? "%.2f x" : "%.2f Hz", value);
    else if (id == kInnerRateParamId || id == kOuterRateParamId
        || id == kEventRateParamId)
        std::snprintf(display, size, "%.2f Hz", value);
    else if (id == kEventRepeatsParamId)
        std::snprintf(display, size, "%d", static_cast<int>(
            std::lround(value)));
    else if (id == kEventPitchParamId)
        std::snprintf(display, size, "%.2f st", value);
    else if (id == kEventCurveParamId)
        std::snprintf(display, size, "%+.2f", value);
    else if (id == kTuneParamId)
        std::snprintf(display, size, "%+.2f st", value);
    else if (id == kFineParamId)
        std::snprintf(display, size, "%+.1f ct", value);
    else if (id == kAttackParamId || id == kReleaseParamId)
        std::snprintf(display, size, "%.1f ms", value * 1000.0);
    else if (id == kRootParamId || id == kSeedParamId)
        std::snprintf(display, size, "%d", static_cast<int>(
            std::lround(value)));
    else if (id == kStartParamId || id == kEndParamId
        || id == kLocusParamId || id == kFieldParamId
        || id == kTravelParamId || id == kJitterParamId
        || id == kPacketDutyParamId || id == kSymmetryParamId
        || id == kJoinParamId || id == kVelocityParamId
        || id == kEventStepParamId || id == kEventLevelParamId)
        std::snprintf(display, size, "%.1f %%", value * 100.0);
    else return false;
    return true;
}

template <typename Name>
bool parseNamedValue(const char* display, double* value, int count,
    Name name)
{
    for (int index = 0; index < count; ++index) {
        if (strcasecmp(display, name(index)) == 0) {
            *value = static_cast<double>(index);
            return true;
        }
    }
    return false;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id,
    const char* display, double* value)
{
    if (!plugin || !display || !value
        || !paramIsVisible(*self(plugin), id)) return false;
    if (id == kMotionParamId)
        return parseNamedValue(display, value, 8, motionName);
    if (id == kArticulationParamId)
        return parseNamedValue(display, value, 3, articulationName);
    if (id == kRateBasisParamId)
        return parseNamedValue(display, value, 2, rateBasisName);
    if (id == kMotorEnvelopeParamId)
        return parseNamedValue(display, value, 4, motorEnvelopeName);
    if (id == kSegmentModelParamId)
        return parseNamedValue(display, value, 7, segmentModelName);
    if (id == kSegmentTriggerParamId)
        return parseNamedValue(display, value, 3, segmentTriggerName);
    if (id == kEventOverlapParamId)
        return parseNamedValue(display, value, 2, segmentOverlapName);
    if (id == kOutputAssignmentEventParamId)
        return parseNamedValue(display, value, 3,
            outputAssignmentEventName);
    if (id == kAvoidAdjacentParamId)
        return parseNamedValue(display, value, 2, offOnName);
    if (id == kOutputTraversalParamId)
        return parseNamedValue(display, value, 5, outputTraversalName);
    if (id == kOutputVoiceWidthParamId)
        return parseNamedValue(display, value, 2, outputVoiceWidthName);
    if (id == kStereoPairLayoutParamId)
        return parseNamedValue(display, value, 2, stereoPairLayoutName);
    if (id == kVoiceModeParamId)
        return parseNamedValue(display, value, 3, voiceName);
    if (id == kTriggerParamId)
        return parseNamedValue(display, value, 4, triggerName);
    if (id == kMidiParamId) {
        if (strcasecmp(display, "Omni") == 0) {
            *value = 0.0;
            return true;
        }
        for (int channel = 1; channel <= 16; ++channel) {
            char buffer[32] {};
            if (strcasecmp(display, midiName(channel, buffer,
                    sizeof(buffer))) == 0) {
                *value = static_cast<double>(channel);
                return true;
            }
        }
    }
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;
    if ((id == kStartParamId || id == kEndParamId
            || id == kLocusParamId || id == kFieldParamId
            || id == kTravelParamId || id == kJitterParamId
            || id == kPacketDutyParamId || id == kSymmetryParamId
            || id == kJoinParamId || id == kVelocityParamId
            || id == kEventStepParamId || id == kEventLevelParamId)
        && std::strchr(display, '%')) parsed *= 0.01;
    if ((id == kAttackParamId || id == kReleaseParamId)
        && (std::strstr(display, "ms") || std::strstr(display, "MS")))
        parsed *= 0.001;
    *value = parsed;
    return true;
}

void readParameterEvents(Plugin& instance,
    const clap_input_events_t* events) noexcept
{
    if (!events || !events->size || !events->get) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        setParam(instance, event->param_id, event->value);
    }
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto& instance = *self(plugin);
    readParameterEvents(instance, input);
    serviceGuiParamEvents(instance, output);
}

const clap_plugin_params_t params {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto& instance = *self(plugin);
    std::shared_ptr<const SampleAsset> asset;
    std::string samplePath;
    std::string projectRelativePath;
    StorageMode storageMode = StorageMode::Project;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        asset = instance.controlAsset;
        samplePath = instance.samplePath;
        projectRelativePath = instance.projectRelativePath;
        storageMode = instance.storageMode;
    }
    if (storageMode == StorageMode::Project
        && instance.projectFileRegistration.registered()
        && !instance.projectFileRegistration.absolutePath().empty())
        samplePath = instance.projectFileRegistration.absolutePath();
    if (storageMode == StorageMode::Project && !samplePath.empty()) {
        const ReaperContext context = s3g::sample_storage::reaperContext(
            instance.host);
        std::string relative;
        std::string ignoredError;
        if (s3g::sample_storage::makeProjectRelativePath(context, samplePath,
                relative, &ignoredError)) projectRelativePath = relative;
        if (!projectRelativePath.empty()) samplePath = projectRelativePath;
    }
    SavedState saved;
    for (const auto& def : kParamDefs)
        saved.parameters[paramIndex(def.id)] = paramValue(instance, def.id);
    std::snprintf(saved.path.data(), saved.path.size(), "%s",
        samplePath.c_str());
    saved.storageMode = static_cast<uint8_t>(storageMode);
    if (asset) {
        saved.channelCount = asset->channelCount;
        saved.frameCount = asset->frameCount();
        saved.sampleRate = asset->sampleRate;
        const uint64_t bytes = static_cast<uint64_t>(saved.channelCount)
            * saved.frameCount * sizeof(float);
        const bool hasLocator = samplePath[0] != '\0';
        const bool mustEmbed = storageMode == StorageMode::Embed
            || !hasLocator;
        if (mustEmbed && bytes > kMaximumEmbeddedAudioBytes
            && !hasLocator) return false;
        saved.embedded = mustEmbed && bytes <= kMaximumEmbeddedAudioBytes
            ? 1u : 0u;
    } else saved.embedded = 0u;
    if (!s3g::clap_state::writeAll(stream, &saved, sizeof(saved)))
        return false;
    if (saved.embedded != 0u && asset) {
        for (uint8_t channel = 0u; channel < asset->channelCount; ++channel) {
            const auto& samples = asset->channels[channel];
            if (!s3g::clap_state::writeAll(stream, samples.data(),
                    samples.size() * sizeof(float))) return false;
        }
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto& instance = *self(plugin);
    StateHeader header;
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic) return false;
    SavedState saved;
    if (header.version == kStateVersion
        && header.parameterCount == kParamCount) {
        std::memcpy(&saved, &header, sizeof(header));
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&saved) + sizeof(header),
                sizeof(saved) - sizeof(header))) return false;
    } else if ((header.version == kProjectStorageStateVersion
            || header.version == 2u)
        && header.parameterCount == kPreviousParamCount) {
        PreviousSavedState previous;
        std::memcpy(&previous, &header, sizeof(header));
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&previous) + sizeof(header),
                sizeof(previous) - sizeof(header))) return false;
        for (const auto& def : kParamDefs)
            saved.parameters[paramIndex(def.id)] = def.defaultValue;
        std::copy(previous.parameters.begin(), previous.parameters.end(),
            saved.parameters.begin());
        saved.path = previous.path;
        saved.embedded = previous.embedded;
        saved.channelCount = previous.channelCount;
        saved.storageMode = header.version == 2u
            ? static_cast<uint8_t>(previous.embedded != 0u
                ? StorageMode::Embed : StorageMode::Link)
            : previous.storageMode;
        saved.frameCount = previous.frameCount;
        saved.sampleRate = previous.sampleRate;
    } else if (header.version == 1u
        && header.parameterCount == kLegacyParamCount) {
        LegacySavedStateV1 legacy;
        std::memcpy(&legacy, &header, sizeof(header));
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(header),
                sizeof(legacy) - sizeof(header))) return false;
        for (const auto& def : kParamDefs)
            saved.parameters[paramIndex(def.id)] = def.defaultValue;
        std::copy(legacy.parameters.begin(), legacy.parameters.end(),
            saved.parameters.begin());
        saved.parameters[paramIndex(kRateBasisParamId)] = 1.0;
        saved.parameters[paramIndex(kMotorEnvelopeParamId)] = 0.0;
        saved.path = legacy.path;
        saved.embedded = legacy.embedded;
        saved.storageMode = static_cast<uint8_t>(legacy.embedded != 0u
            ? StorageMode::Embed : StorageMode::Link);
        saved.channelCount = legacy.channelCount;
        saved.frameCount = legacy.frameCount;
        saved.sampleRate = legacy.sampleRate;
    } else return false;
    if (saved.channelCount > 2u || !std::isfinite(saved.sampleRate))
        return false;
    std::shared_ptr<const SampleAsset> asset;
    std::string loadError;
    const std::string path(saved.path.data(), strnlen(saved.path.data(),
        saved.path.size()));
    const StorageMode storageMode
        = s3g::sample_storage::sanitizeStorageMode(saved.storageMode,
            saved.embedded != 0u ? StorageMode::Embed : StorageMode::Link);
    std::string runtimePath = path;
    std::string projectRelativePath;
    ReaperContext reaperContext;
    ProjectLocation projectLocation;
    bool projectPending = storageMode == StorageMode::Project;
    bool projectContextPending = storageMode == StorageMode::Project;
    if (storageMode == StorageMode::Project && !path.empty()) {
        if (!std::filesystem::path(path).is_absolute())
            projectRelativePath = path;
        reaperContext = s3g::sample_storage::reaperContext(instance.host);
        std::string projectError;
        if (s3g::sample_storage::queryProjectLocation(reaperContext,
                projectLocation, &projectError)) {
            projectContextPending = false;
            const std::filesystem::path storedPath(path);
            if (storedPath.is_absolute()) {
                std::string relative;
                if (s3g::sample_storage::makeProjectRelativePath(
                        projectLocation, path, relative, &projectError)) {
                    projectRelativePath = std::move(relative);
                    projectPending = false;
                }
            } else if (s3g::sample_storage::resolveProjectRelativePath(
                    projectLocation, path, runtimePath, &projectError)) {
                projectRelativePath = path;
                projectPending = false;
            }
        }
    }
    if (saved.embedded != 0u) {
        if (saved.channelCount == 0u || saved.frameCount == 0u
            || !(saved.sampleRate > 0.0)) return false;
        const uint64_t bytes = static_cast<uint64_t>(saved.channelCount)
            * saved.frameCount * sizeof(float);
        if (bytes > kMaximumEmbeddedAudioBytes) return false;
        try {
            auto decoded = std::make_shared<SampleAsset>();
            decoded->channelCount = saved.channelCount;
            decoded->sampleRate = saved.sampleRate;
            for (uint8_t channel = 0u; channel < decoded->channelCount;
                 ++channel) {
                auto& samples = decoded->channels[channel];
                samples.resize(saved.frameCount);
                if (!s3g::clap_state::readAll(stream, samples.data(),
                        samples.size() * sizeof(float))) return false;
            }
            if (!decoded->valid()) return false;
            asset = std::move(decoded);
        } catch (...) { return false; }
    } else if (!runtimePath.empty()
        && (storageMode != StorageMode::Project
            || std::filesystem::path(runtimePath).is_absolute())) {
#if defined(__APPLE__)
        try {
            if (!decodeSampleFile(runtimePath, asset, loadError)) asset.reset();
        } catch (...) {
            asset.reset();
            loadError = "SAMPLE DECODE EXCEEDED AVAILABLE MEMORY";
        }
#endif
    }
#if defined(__APPLE__)
    cancelSampleLoads(instance);
#endif
    instance.projectFileRegistration.clear();
    for (const auto& def : kParamDefs)
        setParam(instance, def.id, saved.parameters[paramIndex(def.id)]);
    publishAsset(instance, std::move(asset), runtimePath, false);
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.storageMode = storageMode;
        instance.reaperContext = reaperContext;
        instance.linkSourcePath = runtimePath;
        instance.projectRelativePath = projectRelativePath;
        instance.sourceFileBytes = storageMode == StorageMode::Project
                && !std::filesystem::path(runtimePath).is_absolute()
            ? 0u : regularFileByteCount(runtimePath);
        if (storageMode == StorageMode::Project
            && instance.sourceFileBytes == 0u) projectPending = true;
        instance.projectStoragePending = projectPending;
        instance.projectContextPending = projectContextPending;
        instance.projectCopyInFlight = false;
        if (!loadError.empty()) instance.status = loadError;
        else if (projectPending)
            instance.status = projectContextPending
                ? "PROJECT PENDING/SAVE PROJECT FIRST"
                : "PROJECT SAMPLE OFFLINE / "
                    + sampleDisplayName(runtimePath);
        else instance.status = std::string(
            s3g::sample_storage::storageModeName(storageMode)) + " / "
            + sampleDisplayName(runtimePath);
    }
    if (storageMode == StorageMode::Project && !projectPending
        && !runtimePath.empty() && regularFileByteCount(runtimePath) != 0u) {
        if (!instance.projectFileRegistration.reset(reaperContext,
                runtimePath, nullptr, nullptr, instance.plugin.desc->name)) {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.projectStoragePending = false;
            instance.projectContextPending = false;
            instance.status = "PROJECT READY/REGISTRATION UNAVAILABLE";
        }
    }
#if defined(__APPLE__)
    // A PROJECT state saved before its source was collected may contain the
    // original absolute locator. Start that pending copy immediately when the
    // restored project is available; the editor service retains the retry for
    // an as-yet-unsaved project without repeatedly retrying hard copy errors.
    if (storageMode == StorageMode::Project && projectPending
        && !runtimePath.empty()) {
        if (projectContextPending) maybeRetryPendingProjectStorage(instance);
        else queueProjectCopy(instance);
    }
#endif
    if (instance.hostParams && instance.hostParams->rescan)
        instance.hostParams->rescan(instance.host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad,
};

#if defined(__APPLE__)

#if 0 // Superseded by the shared Sample-family editor below.
NSColor* color(CGFloat red, CGFloat green, CGFloat blue, CGFloat alpha = 1.0)
{
    return [NSColor colorWithSRGBRed:red green:green blue:blue alpha:alpha];
}

void fillRect(NSRect rect, NSColor* fill, CGFloat radius = 0.0)
{
    [fill setFill];
    if (radius > 0.0)
        [[NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius
            yRadius:radius] fill];
    else NSRectFill(rect);
}

void strokeRect(NSRect rect, NSColor* stroke, CGFloat radius = 0.0,
    CGFloat width = 1.0)
{
    [stroke setStroke];
    NSBezierPath* path = radius > 0.0
        ? [NSBezierPath bezierPathWithRoundedRect:rect xRadius:radius
            yRadius:radius]
        : [NSBezierPath bezierPathWithRect:rect];
    [path setLineWidth:width];
    [path stroke];
}

NSDictionary* textAttributes(CGFloat size, NSColor* foreground,
    bool bold = false)
{
    NSFont* font = bold
        ? [NSFont systemFontOfSize:size weight:NSFontWeightSemibold]
        : [NSFont monospacedSystemFontOfSize:size
            weight:NSFontWeightRegular];
    return @{
        NSFontAttributeName: font,
        NSForegroundColorAttributeName: foreground,
    };
}

constexpr std::array<clap_id, 21u> kGuiSliderIds {{
    kStartParamId, kEndParamId, kLocusParamId, kFieldParamId,
    kMotionRateParamId, kTravelParamId,
    kJitterParamId, kInnerRateParamId, kOuterRateParamId,
    kPacketDutyParamId, kSymmetryParamId, kJoinParamId,
    kRootParamId, kTuneParamId, kFineParamId, kAttackParamId,
    kReleaseParamId, kVelocityParamId, kSeedParamId, kMidiParamId,
    kOutParamId,
}};

NSRect guiSliderRect(std::size_t index)
{
    constexpr CGFloat left = 24.0;
    constexpr CGFloat top = 490.0;
    constexpr CGFloat gap = 8.0;
    constexpr CGFloat width = 148.6666667;
    constexpr CGFloat height = 74.0;
    const CGFloat column = static_cast<CGFloat>(index % 6u);
    const CGFloat row = static_cast<CGFloat>(index / 6u);
    return NSMakeRect(left + column * (width + gap), top + row * 84.0,
        width, height);
}

double paramNormalized(const ParamDef& def, double value)
{
    if (def.id == kMotionRateParamId || def.id == kInnerRateParamId
        || def.id == kOuterRateParamId) {
        return std::log(std::max(value, def.minimum) / def.minimum)
            / std::log(def.maximum / def.minimum);
    }
    return (value - def.minimum) / (def.maximum - def.minimum);
}

double paramFromNormalized(const ParamDef& def, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    double value = def.minimum + normalized * (def.maximum - def.minimum);
    if (def.id == kMotionRateParamId || def.id == kInnerRateParamId
        || def.id == kOuterRateParamId)
        value = def.minimum * std::pow(def.maximum / def.minimum, normalized);
    return def.stepped ? std::round(value) : value;
}

} // namespace

@interface S3GSampleMotionView : NSView {
    Plugin* _instance;
    NSTimer* _timer;
    clap_id _dragParam;
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (void)startTimer;
- (void)stopTimer;
@end

@implementation S3GSampleMotionView

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0,
        static_cast<CGFloat>(kGuiWidth), static_cast<CGFloat>(kGuiHeight))];
    if (self) {
        _instance = instance;
        _dragParam = CLAP_INVALID_ID;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)startTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
        target:self selector:@selector(timerFired:) userInfo:nil repeats:YES];
}

- (void)stopTimer
{
    [_timer invalidate];
    _timer = nil;
}

- (void)timerFired:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (void)drawButton:(NSRect)rect label:(NSString*)label active:(BOOL)active
{
    fillRect(rect, active ? color(0.93, 0.43, 0.13)
        : color(0.14, 0.15, 0.17), 5.0);
    strokeRect(rect, color(0.39, 0.41, 0.44), 5.0);
    NSDictionary* attrs = textAttributes(11.0,
        active ? color(0.05, 0.05, 0.06) : color(0.88, 0.89, 0.90), true);
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
        NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}

- (void)drawModeGroup:(NSRect)rect label:(NSString*)label count:(int)count
    selected:(int)selected names:(const char* (*)(int))names
{
    NSDictionary* labelAttrs = textAttributes(10.0, color(0.55, 0.58, 0.62),
        true);
    [label drawAtPoint:NSMakePoint(rect.origin.x, rect.origin.y - 17.0)
        withAttributes:labelAttrs];
    const CGFloat segmentWidth = rect.size.width / count;
    for (int index = 0; index < count; ++index) {
        const NSRect segment = NSMakeRect(rect.origin.x + segmentWidth * index,
            rect.origin.y, segmentWidth - 3.0, rect.size.height);
        [self drawButton:segment
            label:[NSString stringWithUTF8String:names(index)]
            active:index == selected];
    }
}

- (void)drawWaveform:(NSRect)rect
{
    fillRect(rect, color(0.055, 0.06, 0.07), 7.0);
    strokeRect(rect, color(0.28, 0.30, 0.33), 7.0);
    auto asset = currentAsset(*_instance);
    if (!asset) {
        NSString* message = @"LOAD A MONO OR STEREO SAMPLE";
        NSDictionary* attrs = textAttributes(13.0, color(0.44, 0.47, 0.51),
            true);
        const NSSize size = [message sizeWithAttributes:attrs];
        [message drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
            NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
        return;
    }
    const double start = paramValue(*_instance, kStartParamId);
    const double end = paramValue(*_instance, kEndParamId);
    const double locus = std::clamp(paramValue(*_instance, kLocusParamId),
        start, end);
    const double half = paramValue(*_instance, kFieldParamId) * 0.5;
    const double low = std::max(start, locus - half);
    const double high = std::min(end, locus + half);
    const auto xFor = [&](double normalized) {
        return rect.origin.x + static_cast<CGFloat>(normalized)
            * rect.size.width;
    };
    fillRect(NSMakeRect(xFor(low), rect.origin.y,
        std::max<CGFloat>(1.0, xFor(high) - xFor(low)), rect.size.height),
        color(0.93, 0.43, 0.13, 0.10));

    const auto& samples = asset->channels[0u];
    NSBezierPath* path = [NSBezierPath bezierPath];
    const CGFloat center = NSMidY(rect);
    const CGFloat amplitude = rect.size.height * 0.40;
    const int columns = static_cast<int>(std::floor(rect.size.width));
    for (int column = 0; column < columns; ++column) {
        const std::size_t first = static_cast<std::size_t>(
            static_cast<double>(column) / columns * samples.size());
        const std::size_t last = std::min(samples.size(),
            static_cast<std::size_t>(static_cast<double>(column + 1)
                / columns * samples.size()) + 1u);
        float minimum = 0.0f;
        float maximum = 0.0f;
        for (std::size_t frame = first; frame < last; ++frame) {
            minimum = std::min(minimum, samples[frame]);
            maximum = std::max(maximum, samples[frame]);
        }
        const CGFloat x = rect.origin.x + column;
        [path moveToPoint:NSMakePoint(x, center - maximum * amplitude)];
        [path lineToPoint:NSMakePoint(x, center - minimum * amplitude)];
    }
    [color(0.66, 0.69, 0.72) setStroke];
    [path setLineWidth:1.0];
    [path stroke];

    for (const auto marker : { start, end }) {
        NSBezierPath* markerPath = [NSBezierPath bezierPath];
        [markerPath moveToPoint:NSMakePoint(xFor(marker), rect.origin.y)];
        [markerPath lineToPoint:NSMakePoint(xFor(marker), NSMaxY(rect))];
        [color(0.86, 0.87, 0.89) setStroke];
        [markerPath setLineWidth:1.2];
        [markerPath stroke];
    }
    NSBezierPath* locusPath = [NSBezierPath bezierPath];
    [locusPath moveToPoint:NSMakePoint(xFor(locus), rect.origin.y)];
    [locusPath lineToPoint:NSMakePoint(xFor(locus), NSMaxY(rect))];
    [color(0.93, 0.43, 0.13) setStroke];
    [locusPath setLineWidth:2.0];
    [locusPath stroke];

    constexpr std::array<std::array<CGFloat, 3u>, 6u> cursorColors {{
        {{ 0.96, 0.45, 0.16 }}, {{ 0.26, 0.72, 0.94 }},
        {{ 0.47, 0.82, 0.45 }}, {{ 0.93, 0.73, 0.22 }},
        {{ 0.76, 0.49, 0.92 }}, {{ 0.95, 0.42, 0.59 }},
    }};
    const uint32_t cursorCount = std::min<uint32_t>(
        _instance->cursorCount.load(std::memory_order_acquire),
        static_cast<uint32_t>(s3g::sample::kMaximumMotionVoices));
    for (uint32_t index = 0u; index < cursorCount; ++index) {
        const float position = _instance->cursorPositions[index].load(
            std::memory_order_acquire);
        if (position < 0.0f) continue;
        const auto& rgb = cursorColors[index % cursorColors.size()];
        NSBezierPath* cursor = [NSBezierPath bezierPath];
        [cursor moveToPoint:NSMakePoint(xFor(position), rect.origin.y + 2.0)];
        [cursor lineToPoint:NSMakePoint(xFor(position), NSMaxY(rect) - 2.0)];
        [color(rgb[0u], rgb[1u], rgb[2u]) setStroke];
        [cursor setLineWidth:1.5];
        [cursor stroke];
        NSString* note = [NSString stringWithFormat:@"N%u",
            _instance->cursorKeys[index].load(std::memory_order_acquire)];
        [note drawAtPoint:NSMakePoint(xFor(position) + 3.0,
            rect.origin.y + 5.0 + (index % 4u) * 15.0)
            withAttributes:textAttributes(9.0,
                color(rgb[0u], rgb[1u], rgb[2u]), true)];
    }

    if (paramValue(*_instance, kArticulationParamId) > 0.5) {
        const NSRect raster = NSMakeRect(rect.origin.x + 10.0,
            NSMaxY(rect) - 28.0, rect.size.width - 20.0, 17.0);
        fillRect(raster, color(0.02, 0.025, 0.03, 0.85), 3.0);
        const float innerRate = static_cast<float>(paramValue(*_instance,
            kInnerRateParamId));
        const float outerRate = static_cast<float>(paramValue(*_instance,
            kOuterRateParamId));
        const float ratio = std::clamp(innerRate / std::max(0.05f, outerRate),
            1.0f, 64.0f);
        const int ticks = static_cast<int>(std::round(ratio));
        for (int tick = 0; tick < ticks; ++tick) {
            const CGFloat x = raster.origin.x + (tick + 0.5) / ticks
                * raster.size.width;
            fillRect(NSMakeRect(x, raster.origin.y + 4.0, 1.0, 9.0),
                color(0.93, 0.43, 0.13, 0.75));
        }
        if (cursorCount > 0u) {
            const float innerPhase = _instance->cursorInnerPhases[0u].load(
                std::memory_order_acquire);
            const float outerPhase = _instance->cursorOuterPhases[0u].load(
                std::memory_order_acquire);
            const bool packetActive = innerPhase < paramValue(*_instance,
                kPacketDutyParamId);
            const CGFloat x = raster.origin.x
                + std::clamp<CGFloat>(outerPhase, 0.0, 1.0)
                    * raster.size.width;
            fillRect(NSMakeRect(x - 1.0, raster.origin.y + 1.0,
                    2.0, raster.size.height - 2.0),
                packetActive ? color(0.98, 0.92, 0.82)
                    : color(0.48, 0.50, 0.54));
        }
    }
}

- (void)drawControl:(NSRect)rect param:(clap_id)param
{
    const auto* def = paramDef(param);
    if (!def) return;
    fillRect(rect, color(0.09, 0.10, 0.115), 5.0);
    strokeRect(rect, color(0.24, 0.26, 0.29), 5.0);
    NSString* label = [NSString stringWithUTF8String:def->name];
    [label drawAtPoint:NSMakePoint(rect.origin.x + 9.0, rect.origin.y + 8.0)
        withAttributes:textAttributes(9.0, color(0.55, 0.58, 0.62), true)];
    char valueText[64] {};
    paramsValueToText(&_instance->plugin, param,
        paramValue(*_instance, param), valueText, sizeof(valueText));
    NSString* value = [NSString stringWithUTF8String:valueText];
    [value drawAtPoint:NSMakePoint(rect.origin.x + 9.0,
        rect.origin.y + 27.0)
        withAttributes:textAttributes(11.0, color(0.90, 0.91, 0.92), true)];
    const NSRect rail = NSMakeRect(rect.origin.x + 9.0,
        NSMaxY(rect) - 13.0, rect.size.width - 18.0, 4.0);
    fillRect(rail, color(0.22, 0.24, 0.27), 2.0);
    const double normalized = std::clamp(paramNormalized(*def,
        paramValue(*_instance, param)), 0.0, 1.0);
    fillRect(NSMakeRect(rail.origin.x, rail.origin.y,
        rail.size.width * normalized, rail.size.height),
        color(0.93, 0.43, 0.13), 2.0);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    fillRect([self bounds], color(0.035, 0.039, 0.046));
    fillRect(NSMakeRect(0.0, 0.0, kGuiWidth, 58.0),
        color(0.065, 0.071, 0.082));
    [@"s3g Sample Motion 2" drawAtPoint:NSMakePoint(24.0, 17.0)
        withAttributes:textAttributes(19.0, color(0.95, 0.95, 0.96), true)];
    std::string status;
    {
        std::lock_guard<std::mutex> lock(_instance->statusMutex);
        status = _instance->status;
    }
    [[NSString stringWithUTF8String:status.c_str()]
        drawAtPoint:NSMakePoint(266.0, 21.0)
        withAttributes:textAttributes(9.0, color(0.53, 0.56, 0.60), true)];
    [self drawButton:NSMakeRect(620.0, 14.0, 86.0, 31.0)
        label:@"LOAD" active:NO];
    [self drawButton:NSMakeRect(714.0, 14.0, 94.0, 31.0)
        label:_instance->embedSampleInState ? @"EMBED ON" : @"PATH ONLY"
        active:_instance->embedSampleInState];
    [self drawButton:NSMakeRect(816.0, 14.0, 62.0, 31.0)
        label:@"PLAY" active:NO];
    [self drawButton:NSMakeRect(886.0, 14.0, 70.0, 31.0)
        label:@"STOP" active:NO];

    [self drawWaveform:NSMakeRect(24.0, 76.0, 932.0, 286.0)];
    [self drawModeGroup:NSMakeRect(24.0, 397.0, 290.0, 36.0)
        label:@"SOURCE MOTION" count:3
        selected:static_cast<int>(std::lround(paramValue(*_instance,
            kMotionParamId))) names:motionName];
    [self drawModeGroup:NSMakeRect(330.0, 397.0, 210.0, 36.0)
        label:@"ARTICULATION" count:2
        selected:static_cast<int>(std::lround(paramValue(*_instance,
            kArticulationParamId))) names:articulationName];
    [self drawModeGroup:NSMakeRect(556.0, 397.0, 180.0, 36.0)
        label:@"VOICE" count:3
        selected:static_cast<int>(std::lround(paramValue(*_instance,
            kVoiceModeParamId))) names:voiceName];
    [self drawModeGroup:NSMakeRect(752.0, 397.0, 204.0, 36.0)
        label:@"TRIGGER" count:4
        selected:static_cast<int>(std::lround(paramValue(*_instance,
            kTriggerParamId))) names:triggerGuiName];

    for (std::size_t index = 0u; index < kGuiSliderIds.size(); ++index)
        [self drawControl:guiSliderRect(index) param:kGuiSliderIds[index]];
    const float peak = std::clamp(_instance->outputPeak.load(
        std::memory_order_acquire), 0.0f, 1.0f);
    fillRect(NSMakeRect(24.0, 827.0, 932.0, 4.0), color(0.17, 0.18, 0.20),
        2.0);
    fillRect(NSMakeRect(24.0, 827.0, 932.0 * peak, 4.0),
        peak > 0.95f ? color(0.95, 0.20, 0.16) : color(0.93, 0.43, 0.13),
        2.0);
}

- (void)setParamFromPoint:(NSPoint)point param:(clap_id)param
    rect:(NSRect)rect
{
    const auto* def = paramDef(param);
    if (!def) return;
    const double normalized = std::clamp(
        static_cast<double>((point.x - rect.origin.x - 9.0)
            / std::max<CGFloat>(1.0, rect.size.width - 18.0)), 0.0, 1.0);
    queueGuiParamValue(*_instance, param,
        paramFromNormalized(*def, normalized));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (NSPointInRect(point, NSMakeRect(620.0, 14.0, 86.0, 31.0))) {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            const std::string path([[[panel URL] path] UTF8String]);
            std::shared_ptr<const SampleAsset> asset;
            std::string error;
            if (decodeSampleFile(path, asset, error) && asset) {
                const auto bounds = s3g::sample::defaultSafeSampleBounds(*asset);
                const double frames = std::max<double>(1.0,
                    asset->frameCount());
                const double start = bounds.startFrame / frames;
                const double end = bounds.endFrame / frames;
                setParam(*_instance, kStartParamId, start);
                setParam(*_instance, kEndParamId, end);
                setParam(*_instance, kLocusParamId, 0.5 * (start + end));
                publishAsset(*_instance, std::move(asset), path);
            } else {
                std::lock_guard<std::mutex> lock(_instance->statusMutex);
                _instance->status = error;
            }
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(714.0, 14.0, 94.0, 31.0))) {
        _instance->embedSampleInState = !_instance->embedSampleInState;
        markStateDirty(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(816.0, 14.0, 62.0, 31.0))) {
        _instance->previewRequested.store(true, std::memory_order_release);
        requestProcess(*_instance);
        return;
    }
    if (NSPointInRect(point, NSMakeRect(886.0, 14.0, 70.0, 31.0))) {
        _instance->killRequested.store(true, std::memory_order_release);
        requestProcess(*_instance);
        return;
    }

    struct Group { NSRect rect; clap_id id; int count; };
    const std::array<Group, 4u> groups {{
        { NSMakeRect(24.0, 397.0, 290.0, 36.0), kMotionParamId, 3 },
        { NSMakeRect(330.0, 397.0, 210.0, 36.0),
            kArticulationParamId, 2 },
        { NSMakeRect(556.0, 397.0, 180.0, 36.0), kVoiceModeParamId, 3 },
        { NSMakeRect(752.0, 397.0, 204.0, 36.0), kTriggerParamId, 4 },
    }};
    for (const auto& group : groups) {
        if (!NSPointInRect(point, group.rect)) continue;
        const int selected = std::clamp(static_cast<int>(
            (point.x - group.rect.origin.x) / group.rect.size.width
                * group.count), 0, group.count - 1);
        queueGuiParamValue(*_instance, group.id,
            static_cast<double>(selected));
        [self setNeedsDisplay:YES];
        return;
    }
    for (std::size_t index = 0u; index < kGuiSliderIds.size(); ++index) {
        const NSRect rect = guiSliderRect(index);
        if (!NSPointInRect(point, rect)) continue;
        _dragParam = kGuiSliderIds[index];
        if ([event clickCount] >= 2) {
            const auto* def = paramDef(_dragParam);
            if (def) queueGuiParamValue(*_instance, _dragParam,
                def->defaultValue);
        } else [self setParamFromPoint:point param:_dragParam rect:rect];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    for (std::size_t index = 0u; index < kGuiSliderIds.size(); ++index) {
        if (kGuiSliderIds[index] != _dragParam) continue;
        [self setParamFromPoint:point param:_dragParam
            rect:guiSliderRect(index)];
        return;
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
}

@end

#endif

#include "s3g_sample_motion_gui.inc"

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto& instance = *self(plugin);
    if (instance.guiView) return true;
    S3GSampleMotionView* view = [[S3GSampleMotionView alloc]
        initWithPlugin:&instance];
    if (!view) return false;
    instance.guiView = (__bridge_retained void*)view;
    if (!s3g::clap_gui::createResponsiveViewport(instance.guiViewport, view,
            kGuiWidth, kGuiHeight, 480u, 360u)) {
        void* owned = instance.guiView;
        instance.guiView = nullptr;
        (void)(__bridge_transfer NSView*)owned;
        return false;
    }
    return true;
}

void destroyGui(Plugin& instance)
{
    if (!instance.guiView) return;
    [(__bridge S3GSampleMotionView*)instance.guiView stopTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance.guiViewport,
        instance.guiView);
}

void guiDestroy(const clap_plugin_t* plugin) { destroyGui(*self(plugin)); }
bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        480u, 360u);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        480u, 360u);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto& instance = *self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(instance.guiViewport,
        (__bridge NSView*)window->cocoa, instance.host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance.guiViewport, false)) return false;
    [(__bridge S3GSampleMotionView*)instance.guiView startTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return false;
    [(__bridge S3GSampleMotionView*)instance.guiView stopTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance.guiViewport, true);
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
    guiHide,
};

#else

void destroyGui(Plugin&) {}

#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG) == 0)
        return &audioPortsConfig;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO) == 0
        || std::strcmp(id, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO_COMPAT) == 0)
        return &audioPortsConfigInfo;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0) return &noteNames;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
    return nullptr;
}

const char* const stereoFeatures[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const char* const multichannelFeatures[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t stereoDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-motion",
    "s3g Sample Motion 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.4.3",
    "Stereo sample-motion instrument with eight trajectories and realtime CDP-inspired segment-event models.",
    stereoFeatures,
};

const clap_plugin_descriptor_t multichannelDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-motion-32",
    "s3g Sample Motion 32",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.4.3",
    "Sample Motion instrument with note-, turn-, and segment-assigned 32-channel output routing.",
    multichannelFeatures,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId) return nullptr;
    const clap_plugin_descriptor_t* descriptor = nullptr;
    uint32_t outputChannels = 0u;
    clap_id configId = CLAP_INVALID_ID;
    if (std::strcmp(pluginId, stereoDescriptor.id) == 0) {
        descriptor = &stereoDescriptor;
        outputChannels = 2u;
        configId = kStereoOutputConfigId;
    } else if (std::strcmp(pluginId, multichannelDescriptor.id) == 0) {
        descriptor = &multichannelDescriptor;
        outputChannels = 32u;
        configId = kThirtyTwoChannelOutputConfigId;
    } else {
        return nullptr;
    }
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->outputChannelCount = outputChannels;
    instance->outputConfigId = configId;
    for (auto& position : instance->cursorPositions)
        position.store(-1.0f, std::memory_order_relaxed);
    instance->plugin.desc = descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = pluginInit;
    instance->plugin.destroy = pluginDestroy;
    instance->plugin.activate = pluginActivate;
    instance->plugin.deactivate = pluginDeactivate;
    instance->plugin.start_processing = pluginStartProcessing;
    instance->plugin.stop_processing = pluginStopProcessing;
    instance->plugin.reset = pluginReset;
    instance->plugin.process = pluginProcess;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = pluginOnMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 2u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    if (index == 0u) return &stereoDescriptor;
    if (index == 1u) return &multichannelDescriptor;
    return nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
