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
#include <utility>
#include <vector>

namespace {

using s3g::sample::LaneBlend;
using s3g::sample::LaneEventKind;
using s3g::sample::LanePath;
using s3g::sample::LanePathPoint;
using s3g::sample::LanePathShape;
using s3g::sample::LaneRateBasis;
using s3g::sample::LaneTransport;
using s3g::sample::LaneOutputMode;
using s3g::sample::LaneAllocationCadence;
using s3g::sample::LanesRenderEvent;
using s3g::sample::SampleAsset;
using s3g::sample::SampleLanesSettings;
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
using InstrumentEngine = s3g::sample::SampleGrainsEngine;
using InstrumentSettings = s3g::sample::SampleGrainsSettings;
using s3g::sample::GrainChannelMode;
using s3g::sample::GrainEnvelope;
using s3g::sample::GrainMutate;
using s3g::sample::GrainSourceMode;
using s3g::sample::GrainStereoLink;
using s3g::sample::GrainTiming;
#else
using InstrumentEngine = s3g::sample::SampleLanesEngine;
using InstrumentSettings = s3g::sample::SampleLanesSettings;
#endif
using s3g::sample::TriggerMode;
using s3g::sample::VoiceMode;
using s3g::sample_storage::ProjectCopyResult;
using s3g::sample_storage::ProjectFileRegistration;
using s3g::sample_storage::ProjectLocation;
using s3g::sample_storage::ReaperContext;
using s3g::sample_storage::StorageMode;

#if defined(S3G_SAMPLE_GRAINS_VARIANT)
constexpr uint32_t kStateMagic = 0x47533353u; // "S3SG"
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kRoutingPreviousStateVersion = 0xfffffffdu;
constexpr uint32_t kPreviousStateVersion = 1u;
constexpr uint32_t kLegacyStateVersion = 0xffffffffu;
#else
constexpr uint32_t kStateMagic = 0x4c533353u; // "S3SL"
constexpr uint32_t kStateVersion = 4u;
constexpr uint32_t kRoutingPreviousStateVersion = 3u;
constexpr uint32_t kPreviousStateVersion = 2u;
constexpr uint32_t kLegacyStateVersion = 1u;
#endif
constexpr std::size_t kLegacyParamCount = 27u;
constexpr std::size_t kPreviousParamCount = 39u;
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
constexpr std::size_t kGrainsPreviousParamCount = 59u;
#endif
constexpr uint32_t kGuiWidth = 1280u;
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
constexpr uint32_t kGuiHeight = 956u;
#else
constexpr uint32_t kGuiHeight = 724u;
#endif
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr std::size_t kMaximumBlockEvents = 2048u;
constexpr uint64_t kMaximumEmbeddedAudioBytes =
    1024ull * 1024ull * 1024ull;
constexpr uint32_t kActionPreview = 1u << 0u;
constexpr uint32_t kActionStopAll = 1u << 1u;
constexpr clap_id kStereoOutputConfigId = 2002u;
constexpr clap_id kThirtyTwoChannelOutputConfigId = 2032u;

constexpr clap_id kOutParamId = 1u;
constexpr clap_id kTransportParamId = 2u;
constexpr clap_id kRateBasisParamId = 3u;
constexpr clap_id kRateParamId = 4u;
constexpr clap_id kStartParamId = 5u;
constexpr clap_id kEndParamId = 6u;
constexpr clap_id kLoopCrossfadeParamId = 7u;
constexpr clap_id kPathParamId = 8u;
constexpr clap_id kBlendParamId = 9u;
constexpr clap_id kShapeParamId = 10u;
constexpr clap_id kCyclesParamId = 11u;
constexpr clap_id kOffsetParamId = 12u;
constexpr clap_id kSkewParamId = 13u;
constexpr clap_id kCurveParamId = 14u;
constexpr clap_id kManualLaneParamId = 15u;
constexpr clap_id kLaneSlewParamId = 16u;
constexpr clap_id kVoiceModeParamId = 17u;
constexpr clap_id kTriggerParamId = 18u;
constexpr clap_id kRootParamId = 19u;
constexpr clap_id kTuneParamId = 20u;
constexpr clap_id kFineParamId = 21u;
constexpr clap_id kAttackParamId = 22u;
constexpr clap_id kReleaseParamId = 23u;
constexpr clap_id kPanParamId = 24u;
constexpr clap_id kVelocityParamId = 25u;
constexpr clap_id kSeedParamId = 26u;
constexpr clap_id kMidiParamId = 27u;
constexpr clap_id kLane1SpeedParamId = 28u;
constexpr clap_id kLane1StretchParamId = 29u;
constexpr clap_id kLane1NudgeParamId = 30u;
constexpr clap_id kLane2SpeedParamId = 31u;
constexpr clap_id kLane2StretchParamId = 32u;
constexpr clap_id kLane2NudgeParamId = 33u;
constexpr clap_id kLane3SpeedParamId = 34u;
constexpr clap_id kLane3StretchParamId = 35u;
constexpr clap_id kLane3NudgeParamId = 36u;
constexpr clap_id kLane4SpeedParamId = 37u;
constexpr clap_id kLane4StretchParamId = 38u;
constexpr clap_id kLane4NudgeParamId = 39u;
constexpr clap_id kOutputModeParamId = 40u;
constexpr clap_id kActiveOutputsParamId = 41u;
constexpr clap_id kAllocationCadenceParamId = 42u;
constexpr clap_id kTraversalParamId = 43u;
constexpr clap_id kOutputWidthParamId = 44u;
constexpr clap_id kPairLayoutParamId = 45u;
constexpr clap_id kAvoidAdjacentParamId = 46u;
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
constexpr clap_id kGrainSourceModeParamId = 47u;
constexpr clap_id kGrainDensityParamId = 48u;
constexpr clap_id kGrainSizeParamId = 49u;
constexpr clap_id kSourcePositionParamId = 50u;
constexpr clap_id kPositionSprayParamId = 51u;
constexpr clap_id kPitchSprayParamId = 52u;
constexpr clap_id kReverseChanceParamId = 53u;
constexpr clap_id kGrainEnvelopeParamId = 54u;
constexpr clap_id kGrainTimingParamId = 55u;
constexpr clap_id kGrainMutateParamId = 56u;
constexpr clap_id kMutateAmountParamId = 57u;
constexpr clap_id kRegionCountParamId = 58u;
constexpr clap_id kSourceTimeSyncParamId = 59u;
constexpr clap_id kChannelModeParamId = 60u;
constexpr clap_id kStereoLinkParamId = 61u;
constexpr clap_id kMonoSpreadParamId = 62u;
constexpr std::size_t kParamCount = 62u;
#else
constexpr std::size_t kParamCount = 46u;
#endif

constexpr clap_id laneSpeedParamId(std::size_t lane) noexcept
{ return kLane1SpeedParamId + static_cast<clap_id>(lane * 3u); }
constexpr clap_id laneStretchParamId(std::size_t lane) noexcept
{ return kLane1StretchParamId + static_cast<clap_id>(lane * 3u); }
constexpr clap_id laneNudgeParamId(std::size_t lane) noexcept
{ return kLane1NudgeParamId + static_cast<clap_id>(lane * 3u); }

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
    { kTransportParamId, "Transport", "Loop", 0.0, 2.0, 0.0, true },
    { kRateBasisParamId, "Rate Basis", "Loop", 0.0, 1.0, 0.0, true },
    { kRateParamId, "Rate", "Loop", 0.01, 80.0, 1.0, false },
    { kStartParamId, "Start", "Loop", 0.0, 1.0, 0.0, false },
    { kEndParamId, "End", "Loop", 0.0, 1.0, 1.0, false },
    { kLoopCrossfadeParamId, "Loop Crossfade", "Loop", 0.0, 0.5,
        0.02, false },
    { kPathParamId, "Lane Path", "Path", 0.0, 7.0, 0.0, true },
    { kBlendParamId, "Lane Change", "Path", 0.0, 1.0, 0.0, true },
    { kShapeParamId, "Path Shape", "Path", 0.0, 3.0, 0.0, true },
    { kCyclesParamId, "Path Cycles", "Path", 0.125, 16.0, 1.0, false },
    { kOffsetParamId, "Path Offset", "Path", 0.0, 1.0, 0.0, false },
    { kSkewParamId, "Path Skew", "Path", 0.05, 0.95, 0.5, false },
    { kCurveParamId, "Path Curve", "Path", -1.0, 1.0, 0.0, false },
    { kManualLaneParamId, "Manual Lane", "Path", 0.0, 1.0, 0.0, false },
    { kLaneSlewParamId, "Lane Slew", "Path", 0.0, 0.1, 0.005, false },
    { kVoiceModeParamId, "Voice Mode", "Voice", 0.0, 2.0, 0.0, true },
    { kTriggerParamId, "Trigger", "Voice", 0.0, 3.0, 1.0, true },
    { kRootParamId, "Root Note", "Pitch", 0.0, 127.0, 60.0, true },
    { kTuneParamId, "Tune", "Pitch", -60.0, 60.0, 0.0, false },
    { kFineParamId, "Fine Tune", "Pitch", -100.0, 100.0, 0.0, false },
    { kAttackParamId, "Attack", "Amp", 0.0, 2.0, 0.003, false },
    { kReleaseParamId, "Release", "Amp", 0.0, 2.0, 0.020, false },
    { kPanParamId, "Pan", "Output", -1.0, 1.0, 0.0, false },
    { kVelocityParamId, "Velocity", "MIDI", 0.0, 1.0, 1.0, false },
    { kSeedParamId, "Seed", "Path", 1.0, 65535.0, 1.0, true },
    { kMidiParamId, "MIDI Receive", "MIDI", 0.0, 16.0, 0.0, true },
    { kLane1SpeedParamId, "Lane 1 Speed", "Lane 1", 0.25, 4.0, 1.0,
        false },
    { kLane1StretchParamId, "Lane 1 Stretch", "Lane 1", 0.25, 4.0, 1.0,
        false },
    { kLane1NudgeParamId, "Lane 1 Nudge", "Lane 1", -0.5, 0.5, 0.0,
        false },
    { kLane2SpeedParamId, "Lane 2 Speed", "Lane 2", 0.25, 4.0, 1.0,
        false },
    { kLane2StretchParamId, "Lane 2 Stretch", "Lane 2", 0.25, 4.0, 1.0,
        false },
    { kLane2NudgeParamId, "Lane 2 Nudge", "Lane 2", -0.5, 0.5, 0.0,
        false },
    { kLane3SpeedParamId, "Lane 3 Speed", "Lane 3", 0.25, 4.0, 1.0,
        false },
    { kLane3StretchParamId, "Lane 3 Stretch", "Lane 3", 0.25, 4.0, 1.0,
        false },
    { kLane3NudgeParamId, "Lane 3 Nudge", "Lane 3", -0.5, 0.5, 0.0,
        false },
    { kLane4SpeedParamId, "Lane 4 Speed", "Lane 4", 0.25, 4.0, 1.0,
        false },
    { kLane4StretchParamId, "Lane 4 Stretch", "Lane 4", 0.25, 4.0, 1.0,
        false },
    { kLane4NudgeParamId, "Lane 4 Nudge", "Lane 4", -0.5, 0.5, 0.0,
        false },
    { kOutputModeParamId, "Output Mode", "Output", 0.0, 1.0, 0.0, true },
    { kActiveOutputsParamId, "Active Outputs", "Output", 2.0, 32.0,
        32.0, true },
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    { kAllocationCadenceParamId, "Allocation Unit", "Output", 0.0, 0.0,
        0.0, true },
#else
    { kAllocationCadenceParamId, "Allocation Clock", "Output", 0.0, 2.0,
        0.0, true },
#endif
    { kTraversalParamId, "Output Traversal", "Output", 0.0, 4.0, 0.0,
        true },
    { kOutputWidthParamId, "Output Voice Width", "Output", 0.0, 1.0, 1.0,
        true },
    { kPairLayoutParamId, "Stereo Pair Layout", "Output", 0.0, 1.0, 0.0,
        true },
    { kAvoidAdjacentParamId, "Avoid Adjacent", "Output", 0.0, 1.0, 0.0,
        true },
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    { kGrainSourceModeParamId, "Grain Source", "Grain Source", 0.0, 3.0,
        0.0, true },
    { kGrainDensityParamId, "Density", "Grain Events", 0.1, 160.0,
        24.0, false },
    { kGrainSizeParamId, "Grain Size", "Grain Events", 8.0, 4000.0,
        90.0, false },
    { kSourcePositionParamId, "Source Position", "Grain Source", 0.0, 1.0,
        0.0, false },
    { kPositionSprayParamId, "Position Spray", "Grain Source", 0.0, 1.0,
        0.08, false },
    { kPitchSprayParamId, "Pitch Spray", "Grain Events", 0.0, 24.0,
        0.0, false },
    { kReverseChanceParamId, "Reverse Chance", "Grain Events", 0.0, 1.0,
        0.0, false },
    { kGrainEnvelopeParamId, "Grain Envelope", "Grain Events", 0.0, 4.0,
        0.0, true },
    { kGrainTimingParamId, "Grain Timing", "Grain Events", 0.0, 1.0,
        0.0, true },
    { kGrainMutateParamId, "Mutate Process", "Grain Events", 0.0, 4.0,
        0.0, true },
    { kMutateAmountParamId, "Mutate Amount", "Grain Events", 0.0, 1.0,
        0.5, false },
    { kRegionCountParamId, "Regions", "Grain Source", 2.0, 32.0,
        16.0, true },
    { kSourceTimeSyncParamId, "Source-time Sync", "Grain Events", 0.0, 1.0,
        1.0, true },
    { kChannelModeParamId, "Channel Mode", "Stereo Output", 0.0, 5.0,
        0.0, true },
    { kStereoLinkParamId, "Stereo Link", "Stereo Output", 0.0, 1.0,
        0.0, true },
    { kMonoSpreadParamId, "Mono Spread", "Stereo Output", 0.0, 1.0,
        0.0, false },
#endif
}};

struct StateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    uint8_t storageMode = static_cast<uint8_t>(StorageMode::Project);
    std::array<uint8_t, 3u> reserved {};
};

struct LaneState {
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint16_t reserved = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct ManualPathState {
    uint32_t pointCount = 0u;
    uint32_t reserved = 0u;
    std::array<LanePathPoint,
        s3g::sample::kMaximumLanePathPoints> points {};
};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    uint8_t storageMode = static_cast<uint8_t>(StorageMode::Project);
    std::array<uint8_t, 3u> reserved {};
    std::array<double, kParamCount> parameters {};
    std::array<LaneState, s3g::sample::kSampleLaneCount> lanes {};
    ManualPathState manualPath;
};

static_assert(sizeof(StateHeader) == 16u);
static_assert(offsetof(SavedState, parameters) == sizeof(StateHeader));
static_assert(sizeof(SavedState) == sizeof(StateHeader)
    + kParamCount * sizeof(double)
    + s3g::sample::kSampleLaneCount * sizeof(LaneState)
    + sizeof(ManualPathState));

#if defined(__APPLE__)
struct LoadRequest {
    uint64_t generation = 0u;
    uint8_t lane = 0u;
    std::string path;
    ProjectLocation projectLocation;
    std::string projectError;
    bool copyOnly = false;
};

struct LoadResult {
    uint64_t generation = 0u;
    uint8_t lane = 0u;
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
    uint32_t maximumFrames = 0u;
    InstrumentEngine engine;
    uint32_t outputChannelCount = 2u;
    clap_id outputConfigId = kStereoOutputConfigId;
    std::array<const SampleAsset*, s3g::sample::kSampleLaneCount>
        audioAssets {};
    std::array<std::atomic<const SampleAsset*>,
        s3g::sample::kSampleLaneCount> publishedAssets {};
    std::array<std::shared_ptr<const SampleAsset>,
        s3g::sample::kSampleLaneCount> controlAssets {};
    std::vector<std::shared_ptr<const SampleAsset>> retainedAssets;
    std::mutex statusMutex;
    std::array<std::string, s3g::sample::kSampleLaneCount> samplePaths {};
    std::array<std::string, s3g::sample::kSampleLaneCount> linkSourcePaths {};
    std::array<std::string, s3g::sample::kSampleLaneCount>
        projectRelativePaths {};
    std::array<std::string, s3g::sample::kSampleLaneCount> statuses {{
        "LANE 1 / DROP OR LOAD", "LANE 2 / DROP OR LOAD",
        "LANE 3 / DROP OR LOAD", "LANE 4 / DROP OR LOAD",
    }};
    std::array<uint64_t, s3g::sample::kSampleLaneCount> sourceFileBytes {};
    std::array<bool, s3g::sample::kSampleLaneCount> projectStoragePending {};
    std::array<bool, s3g::sample::kSampleLaneCount> projectCopyInFlight {};
    std::array<std::chrono::steady_clock::time_point,
        s3g::sample::kSampleLaneCount> nextProjectCopyProbe {};
    std::array<std::atomic<double>, kParamCount> parameters {};
    std::array<std::atomic<float>, s3g::sample::kMaximumLanePathPoints>
        manualPathPhases {};
    std::array<std::atomic<float>, s3g::sample::kMaximumLanePathPoints>
        manualPathLanes {};
    std::atomic<uint32_t> manualPathPointCount { 0u };
    std::atomic<uint32_t> manualPathRevision { 0u };
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::array<LanesRenderEvent, kMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>, 32u> scratch {};
    std::array<std::atomic<float>, s3g::sample::kMaximumLaneVoices>
        cursorSourcePositions {};
    std::array<std::atomic<float>, s3g::sample::kMaximumLaneVoices>
        cursorLanePositions {};
    std::array<std::atomic<float>, s3g::sample::kMaximumLaneVoices>
        cursorPathPhases {};
    std::array<std::array<std::atomic<float>,
        s3g::sample::kSampleLaneCount>, s3g::sample::kMaximumLaneVoices>
        cursorLaneSourcePositions {};
    std::array<std::array<std::atomic<float>,
        s3g::sample::kSampleLaneCount>, s3g::sample::kMaximumLaneVoices>
        cursorLaneWeights {};
    std::array<std::atomic<uint8_t>, s3g::sample::kMaximumLaneVoices>
        cursorKeys {};
    std::atomic<uint32_t> cursorCount { 0u };
#if defined(S3G_SAMPLE_GRAINS_VARIANT)
    std::array<std::atomic<float>,
        s3g::sample::kMaximumPublishedGrainCursors> grainCursorPhases {};
    std::array<std::atomic<float>,
        s3g::sample::kMaximumPublishedGrainCursors> grainCursorGains {};
    std::array<std::atomic<float>,
        s3g::sample::kMaximumPublishedGrainCursors>
        grainCursorSourcePositions {};
    std::array<std::atomic<float>,
        s3g::sample::kMaximumPublishedGrainCursors>
        grainCursorLanePositions {};
    std::array<std::array<std::atomic<float>,
        s3g::sample::kSampleLaneCount>,
        s3g::sample::kMaximumPublishedGrainCursors>
        grainCursorLaneSourcePositions {};
    std::array<std::array<std::atomic<float>,
        s3g::sample::kSampleLaneCount>,
        s3g::sample::kMaximumPublishedGrainCursors>
        grainCursorLaneSourceSpans {};
    std::array<std::array<std::atomic<float>,
        s3g::sample::kSampleLaneCount>,
        s3g::sample::kMaximumPublishedGrainCursors>
        grainCursorLaneWeights {};
    std::atomic<uint32_t> grainCursorCount { 0u };
#endif
    std::atomic<uint32_t> activeVoiceCount { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> requestedActions { 0u };
    std::atomic<uint32_t> actionFeedback { 0u };
    std::atomic<uint64_t> cursorRevision { 0u };
    std::atomic<uint8_t> selectedLane { 0u };
    std::atomic<bool> processing { false };
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
    StorageMode storageMode = StorageMode::Project;
    ReaperContext reaperContext;
    std::array<ProjectFileRegistration, s3g::sample::kSampleLaneCount>
        projectFileRegistrations {};
    bool active = false;
#if defined(__APPLE__)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoadRequest> loadRequests;
    std::deque<LoadResult> loadResults;
    std::thread loaderThread;
    std::array<uint64_t, s3g::sample::kSampleLaneCount> loadGenerations {};
    bool loaderStopping = false;
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin);
const ParamDef* paramDef(clap_id id) noexcept;
std::size_t paramIndex(clap_id id) noexcept;
double paramValue(const Plugin& instance, clap_id id) noexcept;
double clampParam(const ParamDef& def, double value) noexcept;
void markStateDirty(Plugin& instance);
void setParam(Plugin& instance, clap_id id, double value,
    bool dirty = false) noexcept;
void initializeParams(Plugin& instance) noexcept;
uint32_t manualPathSnapshot(const Plugin& instance,
    std::array<LanePathPoint, s3g::sample::kMaximumLanePathPoints>& points)
    noexcept;
void publishManualPath(Plugin& instance,
    const std::array<LanePathPoint,
        s3g::sample::kMaximumLanePathPoints>& points,
    uint32_t pointCount, bool dirty = true) noexcept;
InstrumentSettings settingsSnapshot(const Plugin& instance) noexcept;
void requestProcess(Plugin& instance) noexcept;
void requestGuiParamService(Plugin& instance) noexcept;
uint64_t regularFileByteCount(const std::string& path) noexcept;
std::string sampleDisplayName(const std::string& path);
std::shared_ptr<const SampleAsset> currentAsset(Plugin& instance,
    std::size_t lane);
bool publishAsset(Plugin& instance, std::size_t lane,
    std::shared_ptr<const SampleAsset> asset, std::string path,
    bool dirty = true);
void clearLane(Plugin& instance, std::size_t lane);

#if defined(__APPLE__)
bool decodeSampleFile(const std::string& path,
    std::shared_ptr<const SampleAsset>& assetOut, std::string& error,
    uint32_t maximumChannels);
bool startLoader(Plugin& instance);
void stopLoader(Plugin& instance);
void queueSampleLoad(Plugin& instance, std::size_t lane, std::string path);
void queueProjectCopy(Plugin& instance, std::size_t lane);
void serviceLoads(Plugin& instance);
void setStorageMode(Plugin& instance, StorageMode mode);
#endif
