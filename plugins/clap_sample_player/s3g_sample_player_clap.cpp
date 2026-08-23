#include "s3g_sample_player.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_sample_storage.h"

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

using s3g::sample::EventKind;
using s3g::sample::FilterType;
using s3g::sample::PlayMode;
using s3g::sample::PitchMode;
using s3g::sample::PlayerSettings;
using s3g::sample::RenderEvent;
using s3g::sample::RetriggerMode;
using s3g::sample::SampleAsset;
using s3g::sample::SyncMode;
using s3g::sample::TriggerMode;
using s3g::sample::VoiceMode;
using s3g::sample_storage::ProjectCopyResult;
using s3g::sample_storage::ProjectFileRegistration;
using s3g::sample_storage::ProjectLocation;
using s3g::sample_storage::ReaperContext;
using s3g::sample_storage::StorageMode;

constexpr uint32_t kStateMagic = 0x50533353u; // "S3SP"
constexpr uint32_t kLegacyStateVersion = 1u;
constexpr uint32_t kProportionalStateVersion = 2u;
constexpr uint32_t kExpandedStateVersion = 3u;
constexpr uint32_t kPitchStateVersion = 4u;
constexpr uint32_t kMidiStateVersion = 5u;
constexpr uint32_t kStateVersion = 6u;
constexpr uint32_t kGuiWidth = 980u;
constexpr uint32_t kGuiHeight = 844u;
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr std::size_t kMaximumBlockEvents = 2048u;
constexpr uint64_t kMaximumEmbeddedAudioBytes = 1024ull * 1024ull * 1024ull;

constexpr clap_id kPlayModeParamId = 1u;
constexpr clap_id kStartParamId = 2u;
constexpr clap_id kLengthParamId = 3u;
constexpr clap_id kLoopStartParamId = 4u;
constexpr clap_id kLoopEndParamId = 5u;
constexpr clap_id kTuneParamId = 6u;
constexpr clap_id kFineTuneParamId = 7u;
constexpr clap_id kRootNoteParamId = 8u;
constexpr clap_id kAttackParamId = 9u;
constexpr clap_id kDecayParamId = 10u;
constexpr clap_id kSustainParamId = 11u;
constexpr clap_id kReleaseParamId = 12u;
constexpr clap_id kGainParamId = 13u;
constexpr clap_id kPanParamId = 14u;
constexpr clap_id kVelocityParamId = 15u;
constexpr clap_id kLoopCrossfadeParamId = 16u;
constexpr clap_id kFilterTypeParamId = 17u;
constexpr clap_id kFilterCutoffParamId = 18u;
constexpr clap_id kFilterResonanceParamId = 19u;
constexpr clap_id kFilterEnvelopeParamId = 20u;
constexpr clap_id kPitchModeParamId = 21u;
constexpr clap_id kSyncModeParamId = 22u;
constexpr clap_id kSourceTempoParamId = 23u;
constexpr clap_id kTriggerModeParamId = 24u;
constexpr clap_id kRetriggerModeParamId = 25u;
constexpr clap_id kVoiceModeParamId = 26u;
constexpr clap_id kGlideParamId = 27u;
constexpr clap_id kMidiReceiveParamId = 28u;
constexpr std::size_t kLegacyParamCount = 15u;
constexpr std::size_t kExpandedParamCount = 20u;
constexpr std::size_t kPitchParamCount = 21u;
constexpr std::size_t kParamCount = 28u;

constexpr clap_id kStereoOutputConfigId = 3002u;
constexpr clap_id kSixteenChannelOutputConfigId = 3016u;

struct OutputConfigDef {
    clap_id id;
    const char* name;
    uint32_t channelCount;
    const char* portType;
};

constexpr std::array<OutputConfigDef, 2u> kOutputConfigs {{
    { kStereoOutputConfigId, "Stereo", 2u, CLAP_PORT_STEREO },
    { kSixteenChannelOutputConfigId, "16 Channel", 16u, nullptr },
}};

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
    { kPlayModeParamId, "Play Mode", "Sample", 0.0, 5.0, 0.0, true },
    { kStartParamId, "Start", "Sample", 0.0, 1.0, 0.0, false },
    { kLengthParamId, "Length", "Sample", 0.0, 1.0, 1.0, false },
    { kLoopStartParamId, "Loop Start", "Loop", 0.0, 1.0, 0.0, false },
    { kLoopEndParamId, "Loop End", "Loop", 0.0, 1.0, 1.0, false },
    { kTuneParamId, "Tune", "Pitch", -60.0, 60.0, 0.0, false },
    { kFineTuneParamId, "Fine Tune", "Pitch", -100.0, 100.0, 0.0,
        false },
    { kRootNoteParamId, "Root Note", "Pitch", 0.0, 127.0, 60.0, true },
    { kAttackParamId, "Attack", "Amp Envelope", 0.0, 1.0, 0.001,
        false },
    { kDecayParamId, "Decay", "Amp Envelope", 0.0, 1.0, 0.0,
        false },
    { kSustainParamId, "Sustain", "Amp Envelope", 0.0, 1.0, 1.0,
        false },
    { kReleaseParamId, "Release", "Amp Envelope", 0.0, 1.0, 0.005,
        false },
    { kGainParamId, "Gain", "Amp", -60.0, 12.0, -6.0, false },
    { kPanParamId, "Pan", "Amp", -1.0, 1.0, 0.0, false },
    { kVelocityParamId, "Velocity", "MIDI", 0.0, 1.0, 1.0, false },
    { kLoopCrossfadeParamId, "Loop Crossfade", "Loop", 0.0, 0.5,
        0.02, false },
    { kFilterTypeParamId, "Filter Type", "Filter", 0.0, 4.0, 0.0,
        true },
    { kFilterCutoffParamId, "Filter Cutoff", "Filter", 20.0, 20000.0,
        20000.0, false },
    { kFilterResonanceParamId, "Filter Resonance", "Filter", 0.0, 1.0,
        0.0, false },
    { kFilterEnvelopeParamId, "Filter Envelope", "Filter", -1.0, 1.0,
        0.0, false },
    { kPitchModeParamId, "Pitch Mode", "Pitch", 0.0, 2.0, 0.0, true },
    { kSyncModeParamId, "Tempo Sync", "Playback", 0.0, 1.0, 0.0, true },
    { kSourceTempoParamId, "Sample BPM", "Playback", 20.0, 999.0,
        120.0, false },
    { kTriggerModeParamId, "Trigger", "Playback", 0.0, 3.0, 0.0, true },
    { kRetriggerModeParamId, "Retrigger", "Playback", 0.0, 2.0, 0.0,
        true },
    { kVoiceModeParamId, "Voice Mode", "Pitch", 0.0, 2.0, 0.0, true },
    { kGlideParamId, "Glide", "Pitch", 0.0, 2000.0, 0.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "MIDI", 0.0, 16.0, 0.0, true },
}};

struct LegacySavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kProportionalStateVersion;
    clap_id outputConfigId = kStereoOutputConfigId;
    uint32_t parameterCount = static_cast<uint32_t>(kLegacyParamCount);
    std::array<double, kLegacyParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    clap_id outputConfigId = kStereoOutputConfigId;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct ExpandedSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kExpandedStateVersion;
    clap_id outputConfigId = kStereoOutputConfigId;
    uint32_t parameterCount = static_cast<uint32_t>(kExpandedParamCount);
    std::array<double, kExpandedParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct PitchSavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kPitchStateVersion;
    clap_id outputConfigId = kStereoOutputConfigId;
    uint32_t parameterCount = static_cast<uint32_t>(kPitchParamCount);
    std::array<double, kPitchParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

#if defined(__APPLE__)
enum class LoadRequestKind : uint8_t {
    Decode = 0u,
    ProjectCopyOnly,
};

struct LoadRequest {
    uint64_t generation = 0u;
    LoadRequestKind kind = LoadRequestKind::Decode;
    std::string sourcePath;
    ProjectLocation projectLocation;
    std::string projectError;
};

struct LoadResult {
    uint64_t generation = 0u;
    LoadRequestKind kind = LoadRequestKind::Decode;
    std::string sourcePath;
    std::string decodedPath;
    ProjectCopyResult projectCopy;
    std::shared_ptr<const SampleAsset> asset;
    std::string error;
};
#endif

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    double sampleRate = 48000.0;
    uint32_t maximumFrames = 0u;
    uint32_t outputChannelCount = 2u;
    clap_id outputConfigId = kStereoOutputConfigId;
    s3g::sample::SamplePlayerEngine engine;
    const SampleAsset* audioAsset = nullptr;
    std::atomic<const SampleAsset*> publishedAsset { nullptr };
    std::shared_ptr<const SampleAsset> controlAsset;
    std::vector<std::shared_ptr<const SampleAsset>> retainedAssets;
    // samplePath is the persistent locator: absolute for LINK and relative
    // to REAPER's project media directory for a completed PROJECT copy.
    // resolvedSamplePath is the currently readable filesystem path.
    std::string samplePath;
    std::string resolvedSamplePath;
    std::string status { "DROP A SAMPLE OR PRESS LOAD" };
    std::array<std::atomic<double>, kParamCount> parameters {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::array<RenderEvent, kMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>, s3g::sample::kMaximumAudioChannels>
        scratchChannels {};
    std::array<std::atomic<float>, s3g::sample::kMaximumVoices>
        voiceCursorPositions {};
    std::array<std::atomic<uint8_t>, s3g::sample::kMaximumVoices>
        voiceCursorKeys {};
    std::atomic<uint32_t> voiceCursorCount { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> killRequested { false };
    StorageMode storageMode = StorageMode::Project;
    ReaperContext reaperContext {};
    ProjectFileRegistration projectFileRegistration;
    bool projectCopyPending = false;
    bool projectCopyInFlight = false;
    std::atomic<bool> projectRenamePending { false };
    std::chrono::steady_clock::time_point nextProjectCopyProbe {};
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

const OutputConfigDef* outputConfig(clap_id id) noexcept
{
    for (const auto& config : kOutputConfigs)
        if (config.id == id) return &config;
    return nullptr;
}

const ParamDef* paramDef(clap_id id) noexcept
{
    for (const auto& def : kParamDefs) if (def.id == id) return &def;
    return nullptr;
}

std::size_t paramIndex(clap_id id) noexcept
{
    return id >= kPlayModeParamId && id <= kMidiReceiveParamId
        ? static_cast<std::size_t>(id - kPlayModeParamId) : kParamCount;
}

double clampParam(const ParamDef& def, double value) noexcept
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

double paramValue(const Plugin& instance, clap_id id) noexcept
{
    const std::size_t index = paramIndex(id);
    return index < instance.parameters.size()
        ? instance.parameters[index].load(std::memory_order_acquire) : 0.0;
}

bool isEnvelopeProportionParam(clap_id id) noexcept
{
    return id == kAttackParamId || id == kDecayParamId
        || id == kReleaseParamId;
}

void setEnvelopeProportions(Plugin& instance, double attack, double decay,
    double release) noexcept
{
    std::array<double, 3u> values {{ attack, decay, release }};
    for (double& value : values)
        value = std::clamp(std::isfinite(value) ? value : 0.0, 0.0, 1.0);
    const double sum = values[0u] + values[1u] + values[2u];
    if (sum > 1.0) {
        for (double& value : values) value /= sum;
    }
    instance.parameters[paramIndex(kAttackParamId)].store(values[0u],
        std::memory_order_release);
    instance.parameters[paramIndex(kDecayParamId)].store(values[1u],
        std::memory_order_release);
    instance.parameters[paramIndex(kReleaseParamId)].store(values[2u],
        std::memory_order_release);
}

template <typename State>
void migrateLegacyEnvelope(State& saved) noexcept
{
    double durationMilliseconds = 10000.0;
    if (saved.frameCount != 0u && saved.sampleRate > 0.0
        && std::isfinite(saved.sampleRate)) {
        const double startValue = std::clamp(
            saved.parameters[paramIndex(kStartParamId)], 0.0, 1.0);
        const double lengthValue = std::clamp(
            saved.parameters[paramIndex(kLengthParamId)], 0.0, 1.0);
        const uint32_t start = std::min<uint32_t>(static_cast<uint32_t>(
            std::floor(startValue * saved.frameCount)),
            saved.frameCount - 1u);
        const uint32_t requested = std::max<uint32_t>(1u,
            static_cast<uint32_t>(std::floor(
                lengthValue * saved.frameCount)));
        const uint32_t windowFrames = std::min(requested,
            saved.frameCount - start);
        const double tune = saved.parameters[paramIndex(kTuneParamId)];
        const double fine = saved.parameters[paramIndex(kFineTuneParamId)];
        const double semitones = (std::isfinite(tune) ? tune : 0.0)
            + (std::isfinite(fine) ? fine : 0.0) * 0.01;
        const double ratio = std::pow(2.0, semitones / 12.0);
        const double calculated = static_cast<double>(windowFrames)
            / saved.sampleRate / ratio * 1000.0;
        if (calculated > 0.0 && std::isfinite(calculated))
            durationMilliseconds = calculated;
    }
    std::array<double, 3u> proportions {{
        saved.parameters[paramIndex(kAttackParamId)] / durationMilliseconds,
        saved.parameters[paramIndex(kDecayParamId)] / durationMilliseconds,
        saved.parameters[paramIndex(kReleaseParamId)] / durationMilliseconds,
    }};
    for (double& value : proportions)
        value = std::max(0.0, std::isfinite(value) ? value : 0.0);
    const double sum = proportions[0u] + proportions[1u]
        + proportions[2u];
    if (sum > 1.0) {
        for (double& value : proportions) value /= sum;
    }
    saved.parameters[paramIndex(kAttackParamId)] = proportions[0u];
    saved.parameters[paramIndex(kDecayParamId)] = proportions[1u];
    saved.parameters[paramIndex(kReleaseParamId)] = proportions[2u];
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
    const std::size_t index = paramIndex(id);
    if (!def || index >= instance.parameters.size()) return;
    value = clampParam(*def, value);
    if (isEnvelopeProportionParam(id)) {
        double available = 1.0;
        for (const clap_id sibling : {
                 kAttackParamId, kDecayParamId, kReleaseParamId }) {
            if (sibling != id) available -= paramValue(instance, sibling);
        }
        value = std::min(value, std::max(0.0, available));
    }
    instance.parameters[index].store(value,
        std::memory_order_release);
    if (dirty) markStateDirty(instance);
}

void initializeParams(Plugin& instance) noexcept
{
    for (const auto& def : kParamDefs) {
        if (isEnvelopeProportionParam(def.id)) continue;
        setParam(instance, def.id, def.defaultValue, false);
    }
    setEnvelopeProportions(instance,
        paramDef(kAttackParamId)->defaultValue,
        paramDef(kDecayParamId)->defaultValue,
        paramDef(kReleaseParamId)->defaultValue);
}

std::array<double, 4u> safeBoundaryValues(
    const SampleAsset& asset) noexcept
{
    const auto bounds = s3g::sample::defaultSafeSampleBounds(asset);
    const double frames = static_cast<double>(
        std::max<uint32_t>(1u, asset.frameCount()));
    const double start = static_cast<double>(bounds.startFrame) / frames;
    const double end = static_cast<double>(bounds.endFrame) / frames;
    return {{ start, std::max(0.0, end - start), start, end }};
}

void applySafeDefaultBounds(Plugin& instance, const SampleAsset& asset,
    bool notifyHost = true) noexcept
{
    const auto values = safeBoundaryValues(asset);
    setParam(instance, kStartParamId, values[0u], false);
    setParam(instance, kLengthParamId, values[1u], false);
    setParam(instance, kLoopStartParamId, values[2u], false);
    setParam(instance, kLoopEndParamId, values[3u], false);
    if (notifyHost && instance.host && instance.hostParams
        && instance.hostParams->rescan) {
        instance.hostParams->rescan(instance.host, CLAP_PARAM_RESCAN_VALUES);
    }
}

PlayerSettings settingsSnapshot(const Plugin& instance,
    double hostTempoBpm) noexcept
{
    PlayerSettings settings;
    settings.playMode = static_cast<PlayMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kPlayModeParamId))));
    settings.pitchMode = static_cast<PitchMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kPitchModeParamId))));
    settings.syncMode = static_cast<SyncMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kSyncModeParamId))));
    settings.triggerMode = static_cast<TriggerMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kTriggerModeParamId))));
    settings.retriggerMode = static_cast<RetriggerMode>(
        static_cast<uint8_t>(std::lround(
            paramValue(instance, kRetriggerModeParamId))));
    settings.voiceMode = static_cast<VoiceMode>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kVoiceModeParamId))));
    settings.sourceTempoBpm = paramValue(instance, kSourceTempoParamId);
    settings.hostTempoBpm = hostTempoBpm > 0.0
            && std::isfinite(hostTempoBpm)
        ? std::clamp(hostTempoBpm, 1.0, 999.0)
        : settings.sourceTempoBpm;
    settings.glideSeconds = paramValue(instance, kGlideParamId) * 0.001;
    settings.start = paramValue(instance, kStartParamId);
    settings.length = paramValue(instance, kLengthParamId);
    settings.loopStart = paramValue(instance, kLoopStartParamId);
    settings.loopEnd = paramValue(instance, kLoopEndParamId);
    settings.loopCrossfade = paramValue(instance,
        kLoopCrossfadeParamId);
    settings.tuneSemitones = static_cast<float>(
        paramValue(instance, kTuneParamId));
    settings.fineTuneCents = static_cast<float>(
        paramValue(instance, kFineTuneParamId));
    settings.rootNote = static_cast<uint8_t>(std::lround(
        paramValue(instance, kRootNoteParamId)));
    settings.attackProportion = static_cast<float>(
        paramValue(instance, kAttackParamId));
    settings.decayProportion = static_cast<float>(
        paramValue(instance, kDecayParamId));
    settings.sustain = static_cast<float>(
        paramValue(instance, kSustainParamId));
    settings.releaseProportion = static_cast<float>(
        paramValue(instance, kReleaseParamId));
    settings.gainDecibels = static_cast<float>(
        paramValue(instance, kGainParamId));
    settings.pan = instance.outputChannelCount == 2u
        ? static_cast<float>(paramValue(instance, kPanParamId)) : 0.0f;
    settings.velocitySensitivity = static_cast<float>(
        paramValue(instance, kVelocityParamId));
    settings.filterType = static_cast<FilterType>(static_cast<uint8_t>(
        std::lround(paramValue(instance, kFilterTypeParamId))));
    settings.filterCutoffHz = static_cast<float>(
        paramValue(instance, kFilterCutoffParamId));
    settings.filterResonance = static_cast<float>(
        paramValue(instance, kFilterResonanceParamId));
    settings.filterEnvelopeAmount = static_cast<float>(
        paramValue(instance, kFilterEnvelopeParamId));
    return settings;
}

void requestProcess(Plugin& instance)
{
    if (instance.host && instance.host->request_process)
        instance.host->request_process(instance.host);
}

StorageMode nextStorageMode(StorageMode mode) noexcept
{
    switch (mode) {
    case StorageMode::Project: return StorageMode::Link;
    case StorageMode::Link: return StorageMode::Embed;
    case StorageMode::Embed: return StorageMode::Project;
    }
    return StorageMode::Project;
}

void projectFileRenamed(void* owner, const std::string& absolutePath)
{
    auto* instance = static_cast<Plugin*>(owner);
    if (!instance || absolutePath.empty()) return;
    // file_in_project_ex2 may invoke this from inside REAPER's registration
    // machinery. The registration object has already retained the new path;
    // defer host queries, string publication, and dirty notification until
    // the regular main-thread service pass.
    instance->projectRenamePending.store(true, std::memory_order_release);
}

void serviceProjectFileRename(Plugin& instance)
{
    if (!instance.projectRenamePending.exchange(false,
            std::memory_order_acq_rel)) return;
    if (instance.storageMode != StorageMode::Project) return;
    const std::string absolutePath
        = instance.projectFileRegistration.absolutePath();
    if (absolutePath.empty()) return;
    instance.resolvedSamplePath = absolutePath;
    instance.reaperContext = s3g::sample_storage::reaperContext(
        instance.host);
    ProjectLocation location;
    std::string relative;
    std::string error;
    if (s3g::sample_storage::queryProjectLocation(instance.reaperContext,
            location, &error)
        && s3g::sample_storage::makeProjectRelativePath(location,
            absolutePath, relative, &error)) {
        instance.samplePath = std::move(relative);
        instance.projectCopyPending = false;
        instance.status = "PROJECT SAMPLE MOVED";
    } else {
        // Keep the renamed locator even if REAPER moved it outside the media
        // directory. PROJECT will recopy it on the next main-thread probe.
        instance.samplePath = absolutePath;
        instance.projectCopyPending = true;
        instance.status = "PROJECT SAMPLE MOVE PENDING";
    }
    markStateDirty(instance);
}

bool registerProjectSample(Plugin& instance, const std::string& absolutePath)
{
    if (absolutePath.empty()) return false;
    instance.reaperContext = s3g::sample_storage::reaperContext(
        instance.host);
    return instance.projectFileRegistration.reset(instance.reaperContext,
        absolutePath, &instance, projectFileRenamed,
        instance.plugin.desc ? instance.plugin.desc->name
                             : "s3g Sample Player");
}

bool publishAsset(Plugin& instance, std::shared_ptr<const SampleAsset> asset,
    std::string path, std::string resolvedPath, bool dirty = true)
{
    if (asset && (!asset->valid()
        || asset->channelCount > instance.outputChannelCount)) return false;
    instance.projectFileRegistration.clear();
    instance.projectRenamePending.store(false, std::memory_order_release);
    if (asset) instance.retainedAssets.push_back(asset);
    instance.controlAsset = std::move(asset);
    instance.samplePath = std::move(path);
    instance.resolvedSamplePath = std::move(resolvedPath);
    instance.publishedAsset.store(instance.controlAsset.get(),
        std::memory_order_release);
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

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
        if (channels < 1u || channels > s3g::sample::kMaximumAudioChannels
            || fileFrames < 1 || static_cast<uint64_t>(fileFrames)
                > std::numeric_limits<uint32_t>::max()) {
            error = "USE A 1-16 CHANNEL SAMPLE UNDER 2^32 FRAMES";
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
        for (AVAudioChannelCount channel = 0u; channel < channels; ++channel) {
            asset->channels[channel].assign(
                [buffer floatChannelData][channel],
                [buffer floatChannelData][channel] + decodedFrames);
        }
        if (!asset->valid()) {
            error = "DECODED SAMPLE IS INVALID";
            return false;
        }
        assetOut = std::move(asset);
        error.clear();
        return true;
    }
}

bool installDecodedSample(Plugin& instance, const std::string& locator,
    const std::string& resolvedPath, std::shared_ptr<const SampleAsset> asset)
{
    if (!asset || !asset->valid()) {
        instance.status = "INVALID DECODED SAMPLE";
        return false;
    }
    if (asset->channelCount > instance.outputChannelCount) {
        instance.status = instance.outputChannelCount == 2u
            ? "USE S3G SAMPLE PLAYER 16 FOR THIS FILE"
            : "SAMPLE CHANNEL COUNT IS NOT SUPPORTED";
        return false;
    }
    const uint8_t channels = asset->channelCount;
    applySafeDefaultBounds(instance, *asset, false);
    if (!publishAsset(instance, std::move(asset), locator, resolvedPath)) {
        instance.status = "COULD NOT PUBLISH SAMPLE";
        return false;
    }
    if (instance.host && instance.hostParams && instance.hostParams->rescan)
        instance.hostParams->rescan(instance.host, CLAP_PARAM_RESCAN_VALUES);
    instance.status = channels == 1u ? "MONO SAMPLE READY"
        : channels == 2u ? "STEREO SAMPLE READY"
        : "MULTICHANNEL SAMPLE READY";
    return true;
}

bool startSampleLoader(Plugin& instance)
{
    try {
        instance.loaderThread = std::thread([&instance] {
            for (;;) {
                LoadRequest request;
                {
                    std::unique_lock<std::mutex> lock(instance.loaderMutex);
                    instance.loaderCondition.wait(lock, [&instance] {
                        return instance.loaderStopping
                            || !instance.loadRequests.empty();
                    });
                    if (instance.loaderStopping) return;
                    request = std::move(instance.loadRequests.back());
                    instance.loadRequests.clear();
                }
                LoadResult result;
                result.generation = request.generation;
                result.kind = request.kind;
                result.sourcePath = std::move(request.sourcePath);
                result.decodedPath = result.sourcePath;
                result.error = std::move(request.projectError);
                try {
                    if (request.projectLocation.available()) {
                        result.projectCopy
                            = s3g::sample_storage::copyFileIntoProject(
                                request.projectLocation, result.sourcePath);
                        if (result.projectCopy.success) {
                            result.decodedPath
                                = result.projectCopy.absolutePath;
                            result.error.clear();
                        } else if (result.error.empty()) {
                            result.error = result.projectCopy.error;
                        }
                    }
                    if (result.kind == LoadRequestKind::Decode) {
                        std::string decodeError;
                        if (!decodeSampleFile(result.decodedPath,
                                result.asset, decodeError)) {
                            result.asset.reset();
                            result.error = std::move(decodeError);
                        }
                    }
                } catch (...) {
                    result.asset.reset();
                    result.error = result.kind
                            == LoadRequestKind::ProjectCopyOnly
                        ? "PROJECT COPY RAN OUT OF RESOURCES"
                        : "SAMPLE DECODE RAN OUT OF RESOURCES";
                }
                {
                    std::lock_guard<std::mutex> lock(instance.loaderMutex);
                    if (instance.loaderStopping) return;
                    instance.loadResults.push_back(std::move(result));
                }
                if (instance.host && instance.host->request_callback)
                    instance.host->request_callback(instance.host);
            }
        });
    } catch (...) {
        return false;
    }
    return true;
}

void stopSampleLoader(Plugin& instance)
{
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loaderStopping = true;
        instance.loadRequests.clear();
    }
    instance.loaderCondition.notify_all();
    if (instance.loaderThread.joinable()) instance.loaderThread.join();
    std::lock_guard<std::mutex> lock(instance.loaderMutex);
    instance.loadResults.clear();
}

void cancelSampleLoads(Plugin& instance)
{
    ++instance.loadGeneration;
    std::lock_guard<std::mutex> lock(instance.loaderMutex);
    instance.loadRequests.clear();
    instance.loadResults.clear();
    instance.projectCopyInFlight = false;
}

void queueSampleLoad(Plugin& instance, std::string path)
{
    if (path.empty()) return;
    LoadRequest request;
    request.generation = ++instance.loadGeneration;
    request.sourcePath = std::move(path);
    if (instance.storageMode == StorageMode::Project) {
        instance.reaperContext = s3g::sample_storage::reaperContext(
            instance.host);
        (void)s3g::sample_storage::queryProjectLocation(
            instance.reaperContext, request.projectLocation,
            &request.projectError);
        instance.projectCopyInFlight = request.projectLocation.available();
        instance.projectCopyPending = true;
    } else {
        instance.projectCopyInFlight = false;
        instance.projectCopyPending = false;
    }
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
        instance.loadRequests.push_back(std::move(request));
    }
    instance.status = "LOADING SAMPLE";
    instance.loaderCondition.notify_one();
}

bool queueProjectCopy(Plugin& instance)
{
    if (instance.storageMode != StorageMode::Project
        || instance.projectCopyInFlight || !instance.controlAsset
        || instance.resolvedSamplePath.empty()) return false;
    instance.reaperContext = s3g::sample_storage::reaperContext(
        instance.host);
    ProjectLocation location;
    std::string error;
    if (!s3g::sample_storage::queryProjectLocation(instance.reaperContext,
            location, &error)) {
        instance.projectCopyPending = true;
        instance.status = error;
        return false;
    }
    LoadRequest request;
    request.generation = ++instance.loadGeneration;
    request.kind = LoadRequestKind::ProjectCopyOnly;
    request.sourcePath = instance.resolvedSamplePath;
    request.projectLocation = std::move(location);
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
        instance.loadRequests.push_back(std::move(request));
    }
    instance.projectCopyPending = true;
    instance.projectCopyInFlight = true;
    instance.status = "COPYING SAMPLE INTO PROJECT";
    instance.loaderCondition.notify_one();
    return true;
}

void maybeQueuePendingProjectCopy(Plugin& instance)
{
    if (instance.storageMode != StorageMode::Project
        || !instance.projectCopyPending || instance.projectCopyInFlight
        || !instance.controlAsset || instance.resolvedSamplePath.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    if (now < instance.nextProjectCopyProbe) return;
    instance.nextProjectCopyProbe = now + std::chrono::seconds(1);
    (void)queueProjectCopy(instance);
}

void serviceSampleLoads(Plugin& instance)
{
    serviceProjectFileRename(instance);
    std::deque<LoadResult> completed;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        completed.swap(instance.loadResults);
    }
    for (auto& result : completed) {
        if (result.generation != instance.loadGeneration) continue;
        if (result.kind == LoadRequestKind::ProjectCopyOnly) {
            instance.projectCopyInFlight = false;
            if (instance.storageMode != StorageMode::Project) continue;
            if (!result.projectCopy.success) {
                instance.projectCopyPending = true;
                instance.status = result.error.empty()
                    ? "PROJECT SAMPLE COPY FAILED" : result.error;
                continue;
            }
            instance.samplePath = result.projectCopy.relativePath;
            instance.resolvedSamplePath = result.projectCopy.absolutePath;
            instance.projectCopyPending = false;
            const bool registered = registerProjectSample(instance,
                instance.resolvedSamplePath);
            instance.status = registered ? "PROJECT SAMPLE READY"
                : "PROJECT READY / REGISTRATION UNAVAILABLE";
            markStateDirty(instance);
            continue;
        }
        if (!result.asset) {
            instance.projectCopyInFlight = false;
            instance.status = result.error.empty()
                ? "SAMPLE DECODE FAILED" : result.error;
            continue;
        }
        const bool projectCopied = instance.storageMode
                == StorageMode::Project
            && result.projectCopy.success;
        const std::string locator = projectCopied
            ? result.projectCopy.relativePath : result.sourcePath;
        const std::string resolved = projectCopied
            ? result.projectCopy.absolutePath : result.sourcePath;
        if (!installDecodedSample(instance, locator, resolved,
                std::move(result.asset))) {
            instance.projectCopyInFlight = false;
            continue;
        }
        instance.projectCopyInFlight = false;
        if (instance.storageMode == StorageMode::Project) {
            instance.projectCopyPending = !projectCopied;
            if (projectCopied) {
                const bool registered = registerProjectSample(instance,
                    instance.resolvedSamplePath);
                instance.status = registered ? "PROJECT SAMPLE READY"
                    : "PROJECT READY / REGISTRATION UNAVAILABLE";
            } else {
                instance.status = result.error.empty()
                    ? "PROJECT PENDING / SAVE PROJECT"
                    : result.error;
            }
        } else {
            instance.projectCopyPending = false;
            instance.projectFileRegistration.clear();
        }
    }
    maybeQueuePendingProjectCopy(instance);
}

void setStorageMode(Plugin& instance, StorageMode mode)
{
    mode = s3g::sample_storage::sanitizeStorageMode(
        static_cast<uint8_t>(mode));
    if (mode == instance.storageMode) return;
    instance.storageMode = mode;
    if (mode == StorageMode::Project) {
        instance.projectFileRegistration.clear();
        instance.projectRenamePending.store(false, std::memory_order_release);
        instance.projectCopyPending = instance.controlAsset != nullptr;
        instance.projectCopyInFlight = false;
        instance.status = instance.controlAsset
            ? "PROJECT STORAGE PENDING" : "PROJECT / LOAD A SAMPLE";
        maybeQueuePendingProjectCopy(instance);
    } else {
        instance.projectCopyPending = false;
        instance.projectCopyInFlight = false;
        instance.projectFileRegistration.clear();
        instance.projectRenamePending.store(false, std::memory_order_release);
        if (!instance.resolvedSamplePath.empty())
            instance.samplePath = instance.resolvedSamplePath;
        instance.status = std::string("STORE ")
            + s3g::sample_storage::storageModeName(mode);
    }
    markStateDirty(instance);
}
#endif

void requestGuiParamService(Plugin& instance)
{
    if (instance.hostParams && instance.hostParams->request_flush)
        instance.hostParams->request_flush(instance.host);
    else requestProcess(instance);
}

void queueGuiParamValue(Plugin& instance, clap_id id, double value)
{
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) {
        markStateDirty(instance);
        requestGuiParamService(instance);
    }
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
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
    return output->try_push(output, &event.header);
}

void serviceGuiParamEvents(Plugin& instance,
    const clap_output_events_t* output) noexcept
{
    s3g::clap_gui::ParamEvent pending {};
    while (instance.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            setParam(instance, pending.paramId, pending.value);
        instance.guiParamEvents.pop();
    }
}

void appendNoteEvent(Plugin& instance, std::size_t& count, uint32_t frame,
    EventKind kind, uint64_t noteId, uint8_t key, float velocity,
    uint8_t midiChannel) noexcept
{
    if (count >= instance.blockEvents.size()) return;
    instance.blockEvents[count++] = {
        frame, kind, noteId, key, std::clamp(velocity, 0.0f, 1.0f),
        midiChannel,
    };
}

bool receivesNoteOn(const Plugin& instance, uint8_t midiChannel) noexcept
{
    const int receive = static_cast<int>(std::lround(
        paramValue(instance, kMidiReceiveParamId)));
    return receive == 0 || receive == static_cast<int>(midiChannel);
}

void readInputEvents(Plugin& instance, const clap_input_events_t* events,
    uint32_t frames, std::size_t& renderEventCount) noexcept
{
    if (!events || !events->size || !events->get) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (header->type == CLAP_EVENT_PARAM_VALUE
            && header->size >= sizeof(clap_event_param_value_t)) {
            const auto* event = reinterpret_cast<
                const clap_event_param_value_t*>(header);
            setParam(instance, event->param_id, event->value);
            continue;
        }
        const uint32_t frame = frames == 0u ? 0u
            : std::min(header->time, frames - 1u);
        if ((header->type == CLAP_EVENT_NOTE_ON
                || header->type == CLAP_EVENT_NOTE_OFF
                || header->type == CLAP_EVENT_NOTE_CHOKE)
            && header->size >= sizeof(clap_event_note_t)) {
            const auto* event = reinterpret_cast<const clap_event_note_t*>(
                header);
            if (event->port_index != 0 || event->key < 0 || event->key > 127)
                continue;
            const EventKind kind = header->type == CLAP_EVENT_NOTE_ON
                ? EventKind::NoteOn : header->type == CLAP_EVENT_NOTE_OFF
                    ? EventKind::NoteOff : EventKind::Choke;
            const uint8_t midiChannel = event->channel >= 0
                    && event->channel < 16
                ? static_cast<uint8_t>(event->channel + 1) : 0u;
            if (kind == EventKind::NoteOn
                && !receivesNoteOn(instance, midiChannel)) continue;
            appendNoteEvent(instance, renderEventCount, frame, kind,
                event->note_id >= 0
                    ? static_cast<uint64_t>(event->note_id) + 1u : 0u,
                static_cast<uint8_t>(event->key),
                kind == EventKind::NoteOn
                    ? static_cast<float>(event->velocity) : 0.0f,
                midiChannel);
            continue;
        }
        if (header->type == CLAP_EVENT_MIDI
            && header->size >= sizeof(clap_event_midi_t)) {
            const auto* event = reinterpret_cast<const clap_event_midi_t*>(
                header);
            if (event->port_index != 0u) continue;
            const uint8_t status = event->data[0u] & 0xf0u;
            const uint8_t channel = static_cast<uint8_t>(
                (event->data[0u] & 0x0fu) + 1u);
            const uint8_t key = event->data[1u] & 0x7fu;
            if (status == 0x90u && event->data[2u] != 0u) {
                if (!receivesNoteOn(instance, channel)) continue;
                appendNoteEvent(instance, renderEventCount, frame,
                    EventKind::NoteOn, 0u, key,
                    static_cast<float>(event->data[2u]) / 127.0f, channel);
            } else if (status == 0x80u
                || (status == 0x90u && event->data[2u] == 0u)) {
                appendNoteEvent(instance, renderEventCount, frame,
                    EventKind::NoteOff, 0u, key, 0.0f, channel);
            }
        }
    }
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
    if (!startSampleLoader(instance)) {
        instance.status = "COULD NOT START SAMPLE LOADER";
        return false;
    }
#endif
    return true;
}

#if defined(__APPLE__)
void destroyGui(Plugin& instance);
#endif

void pluginDestroy(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
#if defined(__APPLE__)
    destroyGui(instance);
#endif
    instance.projectFileRegistration.clear();
#if defined(__APPLE__)
    stopSampleLoader(instance);
#endif
    delete &instance;
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    if (!(sampleRate > 0.0) || maximumFrames == 0u
        || !instance.engine.prepare(sampleRate,
            instance.outputChannelCount)) return false;
    instance.sampleRate = sampleRate;
    instance.maximumFrames = maximumFrames;
    for (std::size_t channel = 0u;
         channel < instance.scratchChannels.size(); ++channel) {
        if (channel < instance.outputChannelCount)
            instance.scratchChannels[channel].assign(maximumFrames, 0.0f);
        else instance.scratchChannels[channel].clear();
    }
    instance.audioAsset = instance.publishedAsset.load(
        std::memory_order_acquire);
    instance.engine.setPreparedAsset(instance.audioAsset);
    instance.killRequested.store(false, std::memory_order_release);
    instance.active = true;
    return true;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.active = false;
    instance.engine.unprepare();
    instance.audioAsset = nullptr;
    instance.killRequested.store(false, std::memory_order_release);
    instance.voiceCursorCount.store(0u, std::memory_order_release);
    for (auto& channel : instance.scratchChannels) channel.clear();
    instance.retainedAssets.clear();
    if (instance.controlAsset)
        instance.retainedAssets.push_back(instance.controlAsset);
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    return self(plugin)->active;
}

void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.killRequested.store(false, std::memory_order_release);
    instance.engine.reset();
    instance.voiceCursorCount.store(0u, std::memory_order_release);
}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    if (!process) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    if (process->frames_count > instance.maximumFrames)
        return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(instance, process->out_events);
    const SampleAsset* nextAsset = instance.publishedAsset.load(
        std::memory_order_acquire);
    if (nextAsset != instance.audioAsset) {
        instance.audioAsset = nextAsset;
        instance.engine.setPreparedAsset(nextAsset);
    }
    std::size_t eventCount = 0u;
    readInputEvents(instance, process->in_events, process->frames_count,
        eventCount);
    if (instance.killRequested.exchange(false, std::memory_order_acq_rel)) {
        instance.engine.killAll();
        eventCount = 0u;
    }
    std::array<float*, s3g::sample::kMaximumAudioChannels> scratch {};
    for (uint32_t channel = 0u; channel < instance.outputChannelCount;
         ++channel)
        scratch[channel] = instance.scratchChannels[channel].data();
    double hostTempoBpm = 0.0;
    if (process->transport
        && (process->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && process->transport->tempo > 0.0
        && std::isfinite(process->transport->tempo))
        hostTempoBpm = process->transport->tempo;
    instance.engine.render(settingsSnapshot(instance, hostTempoBpm),
        instance.blockEvents.data(), eventCount, scratch.data(),
        instance.outputChannelCount, process->frames_count);
    const uint32_t cursorCount = std::min<uint32_t>(
        instance.engine.voiceCursorCount(),
        static_cast<uint32_t>(instance.voiceCursorPositions.size()));
    const auto& cursors = instance.engine.voiceCursors();
    for (uint32_t cursor = 0u; cursor < cursorCount; ++cursor) {
        instance.voiceCursorPositions[cursor].store(
            cursors[cursor].sourcePositionNormalized,
            std::memory_order_relaxed);
        instance.voiceCursorKeys[cursor].store(cursors[cursor].key,
            std::memory_order_relaxed);
    }
    instance.voiceCursorCount.store(cursorCount,
        std::memory_order_release);

    float peak = 0.0f;
    if (process->audio_outputs_count > 0u && process->audio_outputs) {
        auto& output = process->audio_outputs[0u];
        // This instrument always fills every published frame, and its output
        // may change as soon as a note or live control event arrives. Never
        // inherit a host-provided constant/silent hint from the buffer: hosts
        // are allowed to optimize channels marked in constant_mask.
        output.constant_mask = 0u;
        // REAPER may expose only the connected prefix of this fixed
        // 16-channel port (for example, 8/16 for an eight-channel source).
        // That is safe as long as every source lane is present. Keep the
        // complete 16-lane scratch render internally, then publish the host's
        // connected prefix. Never accept a buffer that would truncate the
        // loaded source field.
        const uint32_t requiredOutputChannels
            = instance.outputChannelCount == 16u && instance.audioAsset
            ? instance.audioAsset->channelCount
            : instance.outputChannelCount;
        if (output.channel_count < requiredOutputChannels)
            return CLAP_PROCESS_ERROR;
        for (uint32_t channel = 0u; channel < output.channel_count;
             ++channel) {
            const float* source = channel < instance.outputChannelCount
                ? instance.scratchChannels[channel].data() : nullptr;
            if (output.data32 && output.data32[channel]) {
                for (uint32_t frame = 0u; frame < process->frames_count;
                     ++frame) {
                    const float value = source ? source[frame] : 0.0f;
                    output.data32[channel][frame] = value;
                    peak = std::max(peak, std::abs(value));
                }
            } else if (output.data64 && output.data64[channel]) {
                for (uint32_t frame = 0u; frame < process->frames_count;
                     ++frame) {
                    const float value = source ? source[frame] : 0.0f;
                    output.data64[channel][frame] = value;
                    peak = std::max(peak, std::abs(value));
                }
            }
        }
    }
    const float previous = instance.outputPeak.load(std::memory_order_relaxed);
    instance.outputPeak.store(std::max(peak, previous * 0.90f),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    serviceSampleLoads(*self(plugin));
#else
    (void)plugin;
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool fillOutputPortInfo(const OutputConfigDef& config, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    *info = {};
    info->id = 20u;
    std::snprintf(info->name, sizeof(info->name), "%s Out", config.name);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = config.channelCount;
    info->port_type = config.portType;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    const auto* config = outputConfig(self(plugin)->outputConfigId);
    return config && fillOutputPortInfo(*config, index, isInput, info);
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
};

uint32_t audioPortsConfigCount(const clap_plugin_t*) { return 1u; }

bool audioPortsConfigGet(const clap_plugin_t* plugin, uint32_t index,
    clap_audio_ports_config_t* config)
{
    if (!config || index != 0u) return false;
    const auto* def = outputConfig(self(plugin)->outputConfigId);
    if (!def) return false;
    *config = {};
    config->id = def->id;
    std::snprintf(config->name, sizeof(config->name), "%s", def->name);
    config->output_port_count = 1u;
    config->has_main_output = true;
    config->main_output_channel_count = def->channelCount;
    config->main_output_port_type = def->portType;
    return true;
}

bool audioPortsConfigSelect(const clap_plugin_t* plugin, clap_id id)
{
    return self(plugin)->outputConfigId == id;
}

const clap_plugin_audio_ports_config_t audioPortsConfig {
    audioPortsConfigCount,
    audioPortsConfigGet,
    audioPortsConfigSelect,
};

clap_id audioPortsConfigCurrent(const clap_plugin_t* plugin)
{
    return self(plugin)->outputConfigId;
}

bool audioPortsConfigInfoGet(const clap_plugin_t* plugin, clap_id configId,
    uint32_t portIndex, bool isInput, clap_audio_port_info_t* info)
{
    const auto* config = outputConfig(self(plugin)->outputConfigId);
    return config && configId == config->id
        && fillOutputPortInfo(*config, portIndex, isInput, info);
}

const clap_plugin_audio_ports_config_info_t audioPortsConfigInfo {
    audioPortsConfigCurrent,
    audioPortsConfigInfoGet,
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1u : 0u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
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
    return self(plugin)->controlAsset ? 1u : 0u;
}

bool noteNameGet(const clap_plugin_t* plugin, uint32_t index,
    clap_note_name_t* noteName)
{
    if (!noteName || index != 0u || !self(plugin)->controlAsset) return false;
    *noteName = {};
    noteName->port = 0;
    noteName->channel = -1;
    noteName->key = static_cast<int16_t>(std::lround(
        paramValue(*self(plugin), kRootNoteParamId)));
    std::snprintf(noteName->name, sizeof(noteName->name), "%s",
        "SAMPLE ROOT");
    return true;
}

const clap_plugin_note_name_t noteNames {
    noteNameCount,
    noteNameGet,
};

bool paramIsExposed(const Plugin& instance, clap_id id) noexcept
{
    return instance.outputChannelCount == 2u || id != kPanParamId;
}

const ParamDef* exposedParamAt(const Plugin& instance,
    uint32_t index) noexcept
{
    uint32_t exposedIndex = 0u;
    for (const auto& def : kParamDefs) {
        if (!paramIsExposed(instance, def.id)) continue;
        if (exposedIndex++ == index) return &def;
    }
    return nullptr;
}

uint32_t paramsCount(const clap_plugin_t* plugin)
{
    return static_cast<uint32_t>(kParamDefs.size()
        - (self(plugin)->outputChannelCount == 16u ? 1u : 0u));
}

bool paramsGetInfo(const clap_plugin_t* plugin, uint32_t index,
    clap_param_info_t* info)
{
    const auto* def = exposedParamAt(*self(plugin), index);
    if (!info || !def) return false;
    *info = {};
    info->id = def->id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def->stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::snprintf(info->name, sizeof(info->name), "%s", def->name);
    std::snprintf(info->module, sizeof(info->module), "%s", def->module);
    info->min_value = def->minimum;
    info->max_value = def->maximum;
    info->default_value = def->defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramDef(id) || !paramIsExposed(*self(plugin), id))
        return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

const char* playModeName(int mode) noexcept
{
    constexpr std::array<const char*, 6u> names {{
        "Forward", "Forward Loop", "Reverse", "Reverse Loop",
        "Forward Ping-Pong", "Reverse Ping-Pong",
    }};
    return names[static_cast<std::size_t>(std::clamp(mode, 0, 5))];
}

const char* filterTypeName(int type) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Off", "Low Pass", "Band Pass", "High Pass", "Notch",
    }};
    return names[static_cast<std::size_t>(std::clamp(type, 0, 4))];
}

const char* pitchModeName(int mode) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Rate", "Stretch", "Rate Below / Stretch Above",
    }};
    return names[static_cast<std::size_t>(std::clamp(mode, 0, 2))];
}

const char* syncModeName(int mode) noexcept
{
    constexpr std::array<const char*, 2u> names {{ "Free", "Host" }};
    return names[static_cast<std::size_t>(std::clamp(mode, 0, 1))];
}

const char* triggerModeName(int mode) noexcept
{
    constexpr std::array<const char*, 4u> names {{
        "Auto", "Gate", "One Shot", "Toggle",
    }};
    return names[static_cast<std::size_t>(std::clamp(mode, 0, 3))];
}

const char* retriggerModeName(int mode) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Layer", "Restart", "Ignore",
    }};
    return names[static_cast<std::size_t>(std::clamp(mode, 0, 2))];
}

const char* voiceModeName(int mode) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Poly", "Mono", "Legato",
    }};
    return names[static_cast<std::size_t>(std::clamp(mode, 0, 2))];
}

const char* midiReceiveName(int channel, char* text,
    std::size_t size) noexcept
{
    if (channel <= 0) return "Omni";
    std::snprintf(text, size, "Channel %d", std::clamp(channel, 1, 16));
    return text;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)
        || !paramIsExposed(*self(plugin), id)) return false;
    if (id == kPlayModeParamId)
        std::snprintf(display, size, "%s", playModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kFilterTypeParamId)
        std::snprintf(display, size, "%s", filterTypeName(
            static_cast<int>(std::lround(value))));
    else if (id == kPitchModeParamId)
        std::snprintf(display, size, "%s", pitchModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kSyncModeParamId)
        std::snprintf(display, size, "%s", syncModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kTriggerModeParamId)
        std::snprintf(display, size, "%s", triggerModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kRetriggerModeParamId)
        std::snprintf(display, size, "%s", retriggerModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kVoiceModeParamId)
        std::snprintf(display, size, "%s", voiceModeName(
            static_cast<int>(std::lround(value))));
    else if (id == kStartParamId || id == kLengthParamId
        || id == kLoopStartParamId || id == kLoopEndParamId
        || id == kLoopCrossfadeParamId
        || id == kAttackParamId || id == kDecayParamId
        || id == kSustainParamId || id == kReleaseParamId
        || id == kVelocityParamId || id == kFilterResonanceParamId
        || id == kFilterEnvelopeParamId)
        std::snprintf(display, size, "%.1f %%", value * 100.0);
    else if (id == kFilterCutoffParamId) {
        if (value >= 1000.0)
            std::snprintf(display, size, "%.2f kHz", value * 0.001);
        else std::snprintf(display, size, "%.0f Hz", value);
    }
    else if (id == kGainParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTuneParamId)
        std::snprintf(display, size, "%+.2f st", value);
    else if (id == kFineTuneParamId)
        std::snprintf(display, size, "%+.1f ct", value);
    else if (id == kRootNoteParamId)
        std::snprintf(display, size, "%d", static_cast<int>(
            std::lround(value)));
    else if (id == kPanParamId)
        std::snprintf(display, size, "%+.2f", value);
    else if (id == kSourceTempoParamId)
        std::snprintf(display, size, "%.2f BPM", value);
    else if (id == kGlideParamId)
        std::snprintf(display, size, "%.1f ms", value);
    else if (id == kMidiReceiveParamId) {
        char text[32] {};
        std::snprintf(display, size, "%s", midiReceiveName(
            static_cast<int>(std::lround(value)), text, sizeof(text)));
    }
    else return false;
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !paramDef(id)
        || !paramIsExposed(*self(plugin), id)) return false;
    if (id == kPlayModeParamId) {
        for (int mode = 0; mode < 6; ++mode) {
            if (strcasecmp(display, playModeName(mode)) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
        return false;
    }
    if (id == kFilterTypeParamId) {
        for (int type = 0; type < 5; ++type) {
            if (strcasecmp(display, filterTypeName(type)) == 0) {
                *value = static_cast<double>(type);
                return true;
            }
        }
        return false;
    }
    if (id == kPitchModeParamId) {
        for (int mode = 0; mode < 3; ++mode) {
            if (strcasecmp(display, pitchModeName(mode)) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
        return false;
    }
    if (id == kSyncModeParamId) {
        for (int mode = 0; mode < 2; ++mode) {
            if (strcasecmp(display, syncModeName(mode)) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
        return false;
    }
    if (id == kTriggerModeParamId) {
        for (int mode = 0; mode < 4; ++mode) {
            if (strcasecmp(display, triggerModeName(mode)) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
        return false;
    }
    if (id == kRetriggerModeParamId) {
        for (int mode = 0; mode < 3; ++mode) {
            if (strcasecmp(display, retriggerModeName(mode)) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
        return false;
    }
    if (id == kVoiceModeParamId) {
        for (int mode = 0; mode < 3; ++mode) {
            if (strcasecmp(display, voiceModeName(mode)) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
        return false;
    }
    if (id == kMidiReceiveParamId) {
        if (strcasecmp(display, "Omni") == 0) {
            *value = 0.0;
            return true;
        }
        for (int channel = 1; channel <= 16; ++channel) {
            char text[32] {};
            if (strcasecmp(display, midiReceiveName(
                    channel, text, sizeof(text))) == 0) {
                *value = static_cast<double>(channel);
                return true;
            }
        }
    }
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;
    if ((id == kStartParamId || id == kLengthParamId
            || id == kLoopStartParamId || id == kLoopEndParamId
            || id == kLoopCrossfadeParamId
            || id == kAttackParamId || id == kDecayParamId
            || id == kSustainParamId || id == kReleaseParamId
            || id == kVelocityParamId || id == kFilterResonanceParamId
            || id == kFilterEnvelopeParamId)
        && std::strchr(display, '%')) parsed *= 0.01;
    if (id == kFilterCutoffParamId
        && (std::strchr(display, 'k') || std::strchr(display, 'K')))
        parsed *= 1000.0;
    *value = parsed;
    return true;
}

void readParameterEvents(Plugin& instance,
    const clap_input_events_t* events) noexcept
{
    if (!events || !events->size || !events->get) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        if (!paramIsExposed(instance, event->param_id)) continue;
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
    const auto& instance = *self(plugin);
    SavedState saved;
    std::memset(&saved, 0, sizeof(saved));
    saved.magic = kStateMagic;
    saved.version = kStateVersion;
    saved.outputConfigId = instance.outputConfigId;
    saved.parameterCount = static_cast<uint32_t>(kParamCount);
    for (const auto& def : kParamDefs)
        saved.parameters[paramIndex(def.id)] = paramValue(instance, def.id);
    std::snprintf(saved.path.data(), saved.path.size(), "%s",
        instance.samplePath.c_str());
    saved.reserved0 = static_cast<uint8_t>(instance.storageMode);
    if (instance.controlAsset) {
        saved.channelCount = instance.controlAsset->channelCount;
        saved.frameCount = instance.controlAsset->frameCount();
        saved.sampleRate = instance.controlAsset->sampleRate;
        const uint64_t bytes = static_cast<uint64_t>(saved.channelCount)
            * saved.frameCount * sizeof(float);
        // Requested mode and actual payload are intentionally independent.
        // A file-backed pending PROJECT keeps its original absolute locator
        // and remains a small state. Only a generated/pathless asset needs a
        // temporary embedded payload until it gains a file locator; an
        // over-cap explicit EMBED can still fall back to its retained path.
        const bool pathless = instance.samplePath.empty();
        const bool needsPayload = instance.storageMode == StorageMode::Embed
            || pathless;
        if (pathless && bytes > kMaximumEmbeddedAudioBytes) return false;
        saved.embedded = needsPayload && bytes <= kMaximumEmbeddedAudioBytes
            ? 1u : 0u;
    }
    if (!s3g::clap_state::writeAll(stream, &saved, sizeof(saved)))
        return false;
    if (saved.embedded != 0u && instance.controlAsset) {
        for (uint8_t channel = 0u;
             channel < instance.controlAsset->channelCount; ++channel) {
            const auto& samples = instance.controlAsset->channels[channel];
            if (!s3g::clap_state::writeAll(stream, samples.data(),
                    samples.size() * sizeof(float))) return false;
        }
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    struct StatePrefix {
        uint32_t magic = 0u;
        uint32_t version = 0u;
    } prefix;
    if (!s3g::clap_state::readAll(stream, &prefix, sizeof(prefix))
        || prefix.magic != kStateMagic) return false;
    SavedState saved;
    const bool explicitStorageMode = prefix.version == kStateVersion;
    if (prefix.version == kLegacyStateVersion
        || prefix.version == kProportionalStateVersion) {
        LegacySavedState legacy;
        legacy.magic = prefix.magic;
        legacy.version = prefix.version;
        auto* remainder = reinterpret_cast<uint8_t*>(&legacy)
            + sizeof(prefix);
        if (!s3g::clap_state::readAll(stream, remainder,
                sizeof(legacy) - sizeof(prefix))
            || legacy.parameterCount != kLegacyParamCount
            || legacy.outputConfigId != self(plugin)->outputConfigId)
            return false;
        if (legacy.version == kLegacyStateVersion)
            migrateLegacyEnvelope(legacy);
        saved.magic = legacy.magic;
        saved.version = kStateVersion;
        saved.outputConfigId = legacy.outputConfigId;
        saved.parameterCount = static_cast<uint32_t>(kParamCount);
        for (const auto& def : kParamDefs)
            saved.parameters[paramIndex(def.id)] = def.defaultValue;
        // Existing loops wrapped without overlap. Preserve their sound while
        // keeping crossfade enabled by default for newly initialized players.
        saved.parameters[paramIndex(kLoopCrossfadeParamId)] = 0.0;
        std::copy(legacy.parameters.begin(), legacy.parameters.end(),
            saved.parameters.begin());
        saved.path = legacy.path;
        saved.embedded = legacy.embedded;
        saved.channelCount = legacy.channelCount;
        saved.reserved0 = legacy.reserved0;
        saved.reserved1 = legacy.reserved1;
        saved.frameCount = legacy.frameCount;
        saved.sampleRate = legacy.sampleRate;
    } else if (prefix.version == kExpandedStateVersion) {
        ExpandedSavedState expanded;
        expanded.magic = prefix.magic;
        expanded.version = prefix.version;
        auto* remainder = reinterpret_cast<uint8_t*>(&expanded)
            + sizeof(prefix);
        if (!s3g::clap_state::readAll(stream, remainder,
                sizeof(expanded) - sizeof(prefix))
            || expanded.parameterCount != kExpandedParamCount
            || expanded.outputConfigId != self(plugin)->outputConfigId)
            return false;
        saved.magic = expanded.magic;
        saved.version = kStateVersion;
        saved.outputConfigId = expanded.outputConfigId;
        saved.parameterCount = static_cast<uint32_t>(kParamCount);
        for (const auto& def : kParamDefs)
            saved.parameters[paramIndex(def.id)] = def.defaultValue;
        std::copy(expanded.parameters.begin(), expanded.parameters.end(),
            saved.parameters.begin());
        saved.path = expanded.path;
        saved.embedded = expanded.embedded;
        saved.channelCount = expanded.channelCount;
        saved.reserved0 = expanded.reserved0;
        saved.reserved1 = expanded.reserved1;
        saved.frameCount = expanded.frameCount;
        saved.sampleRate = expanded.sampleRate;
    } else if (prefix.version == kPitchStateVersion) {
        PitchSavedState pitch;
        pitch.magic = prefix.magic;
        pitch.version = prefix.version;
        auto* remainder = reinterpret_cast<uint8_t*>(&pitch)
            + sizeof(prefix);
        if (!s3g::clap_state::readAll(stream, remainder,
                sizeof(pitch) - sizeof(prefix))
            || pitch.parameterCount != kPitchParamCount
            || pitch.outputConfigId != self(plugin)->outputConfigId)
            return false;
        saved.magic = pitch.magic;
        saved.version = kStateVersion;
        saved.outputConfigId = pitch.outputConfigId;
        saved.parameterCount = static_cast<uint32_t>(kParamCount);
        for (const auto& def : kParamDefs)
            saved.parameters[paramIndex(def.id)] = def.defaultValue;
        std::copy(pitch.parameters.begin(), pitch.parameters.end(),
            saved.parameters.begin());
        saved.path = pitch.path;
        saved.embedded = pitch.embedded;
        saved.channelCount = pitch.channelCount;
        saved.reserved0 = pitch.reserved0;
        saved.reserved1 = pitch.reserved1;
        saved.frameCount = pitch.frameCount;
        saved.sampleRate = pitch.sampleRate;
    } else if (prefix.version == kMidiStateVersion
        || prefix.version == kStateVersion) {
        saved.magic = prefix.magic;
        saved.version = prefix.version;
        auto* remainder = reinterpret_cast<uint8_t*>(&saved)
            + sizeof(prefix);
        if (!s3g::clap_state::readAll(stream, remainder,
                sizeof(saved) - sizeof(prefix))
            || saved.parameterCount != kParamCount
            || saved.outputConfigId != self(plugin)->outputConfigId)
            return false;
    } else {
        return false;
    }
    auto& instance = *self(plugin);
    std::shared_ptr<const SampleAsset> asset;
    const std::string path(saved.path.data(), strnlen(saved.path.data(),
        saved.path.size()));
    const StorageMode requestedStorageMode = explicitStorageMode
        ? s3g::sample_storage::sanitizeStorageMode(saved.reserved0,
            saved.embedded != 0u ? StorageMode::Embed
                : (!path.empty() ? StorageMode::Link
                                 : StorageMode::Project))
        : (saved.embedded != 0u ? StorageMode::Embed
            : (!path.empty() ? StorageMode::Link : StorageMode::Project));
    std::string resolvedPath;
    bool projectLocatorReady = false;
    if (!path.empty()) {
        if (requestedStorageMode == StorageMode::Project) {
            if (std::filesystem::path(path).is_absolute()) {
                resolvedPath = path;
            } else {
                instance.reaperContext
                    = s3g::sample_storage::reaperContext(instance.host);
                ProjectLocation location;
                std::string error;
                if (s3g::sample_storage::queryProjectLocation(
                        instance.reaperContext, location, &error)
                    && s3g::sample_storage::resolveProjectRelativePath(
                        location, path, resolvedPath, &error)) {
                    projectLocatorReady = true;
                }
            }
        } else {
            resolvedPath = path;
        }
    }
    if (saved.embedded != 0u) {
        if (saved.channelCount == 0u
            || saved.channelCount > instance.outputChannelCount
            || saved.frameCount == 0u || !(saved.sampleRate > 0.0)
            || !std::isfinite(saved.sampleRate)) return false;
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
        } catch (...) {
            return false;
        }
    } else if (!resolvedPath.empty()) {
#if defined(__APPLE__)
        std::string error;
        if (!decodeSampleFile(resolvedPath, asset, error)
            || !asset || asset->channelCount > instance.outputChannelCount)
            asset.reset();
#endif
    }
#if defined(__APPLE__)
    cancelSampleLoads(instance);
#endif
    instance.storageMode = requestedStorageMode;
    instance.projectCopyPending = requestedStorageMode
            == StorageMode::Project
        && !projectLocatorReady;
    if (!publishAsset(instance, std::move(asset), path, resolvedPath, false))
        return false;
    for (const auto& def : kParamDefs) {
        if (isEnvelopeProportionParam(def.id)) continue;
        setParam(instance, def.id, saved.parameters[paramIndex(def.id)],
            false);
    }
    setEnvelopeProportions(instance,
        saved.parameters[paramIndex(kAttackParamId)],
        saved.parameters[paramIndex(kDecayParamId)],
        saved.parameters[paramIndex(kReleaseParamId)]);
#if defined(__APPLE__)
    bool projectRegistered = false;
    if (requestedStorageMode == StorageMode::Project
        && projectLocatorReady && !instance.resolvedSamplePath.empty()) {
        projectRegistered = registerProjectSample(instance,
            instance.resolvedSamplePath);
    } else if (requestedStorageMode == StorageMode::Project
        && instance.controlAsset && !instance.resolvedSamplePath.empty()) {
        maybeQueuePendingProjectCopy(instance);
    }
#else
    constexpr bool projectRegistered = false;
#endif
    if (instance.controlAsset) {
        if (requestedStorageMode == StorageMode::Project) {
            if (projectLocatorReady) {
                instance.status = projectRegistered
                    ? "PROJECT SAMPLE RESTORED"
                    : "PROJECT RESTORED / REGISTRATION UNAVAILABLE";
            } else {
                instance.status = instance.resolvedSamplePath.empty()
                    ? "PROJECT / STATE SAMPLE RESTORED"
                    : "PROJECT SAMPLE COPY PENDING";
            }
        } else {
            instance.status = std::string(
                s3g::sample_storage::storageModeName(requestedStorageMode))
                + " SAMPLE RESTORED";
        }
    } else {
        instance.status = std::string(
            s3g::sample_storage::storageModeName(requestedStorageMode))
            + " RESTORED / SAMPLE OFFLINE";
    }
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad,
};

} // namespace

#if defined(__APPLE__)

namespace {

constexpr CGFloat kContentTop = static_cast<CGFloat>(
    s3g::gui_layout::kStandardMetrics.contentTop);
constexpr auto kSampleTitleBand = s3g::gui_layout::encoderTitleBand({
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight),
});
static_assert(s3g::gui_layout::processorTitleBandFits(kSampleTitleBand));

NSRect samplePanelRect()
{
    return NSMakeRect(18.0, kContentTop, 944.0, 282.0);
}

NSRect waveformRect()
{
    return NSMakeRect(30.0, 70.0, 920.0, 210.0);
}

NSRect playbackPanelRect()
{
    return NSMakeRect(18.0, 336.0, 456.0, 294.0);
}

NSRect filterPanelRect()
{
    return NSMakeRect(486.0, 476.0, 476.0, 154.0);
}

NSRect outputPanelRect()
{
    return NSMakeRect(486.0, 336.0, 476.0, 128.0);
}

NSRect killAllButtonRect()
{
    const NSRect panel = outputPanelRect();
    return NSMakeRect(NSMaxX(panel) - 86.0, panel.origin.y + 3.0,
        74.0, 15.0);
}

NSRect pitchOutputPanelRect()
{
    return NSMakeRect(18.0, 642.0, 456.0, 184.0);
}

NSRect envelopePanelRect()
{
    return NSMakeRect(486.0, 642.0, 476.0, 166.0);
}

NSRect sampleLoadButtonRect()
{
    return NSMakeRect(700.0, kContentTop + 3.0, 88.0, 15.0);
}

NSRect storageButtonRect()
{
    return NSMakeRect(796.0, kContentTop + 3.0, 154.0, 15.0);
}

NSRect playModeMenuRect()
{
    const NSRect panel = playbackPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        371.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect playModeDropdownRect()
{
    const NSRect menu = playModeMenuRect();
    return NSMakeRect(menu.origin.x, NSMaxY(menu) + 1.0,
        menu.size.width, 120.0);
}

NSRect triggerModeMenuRect()
{
    const NSRect panel = playbackPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        397.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect triggerModeDropdownRect()
{
    const NSRect menu = triggerModeMenuRect();
    return NSMakeRect(menu.origin.x, NSMaxY(menu) + 1.0,
        menu.size.width, 80.0);
}

NSRect retriggerModeMenuRect()
{
    const NSRect panel = playbackPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        423.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect retriggerModeDropdownRect()
{
    const NSRect menu = retriggerModeMenuRect();
    return NSMakeRect(menu.origin.x, NSMaxY(menu) + 1.0,
        menu.size.width, 60.0);
}

NSRect syncModeMenuRect()
{
    const NSRect panel = playbackPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        449.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect syncModeDropdownRect()
{
    const NSRect menu = syncModeMenuRect();
    return NSMakeRect(menu.origin.x, NSMaxY(menu) + 1.0,
        menu.size.width, 40.0);
}

NSRect filterTypeMenuRect()
{
    const NSRect panel = filterPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        511.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect midiReceiveMenuRect()
{
    const NSRect panel = outputPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        423.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect midiReceiveDropdownRect()
{
    const NSRect menu = midiReceiveMenuRect();
    return NSMakeRect(menu.origin.x, menu.origin.y - 341.0,
        menu.size.width, 340.0);
}

NSRect filterTypeDropdownRect()
{
    const NSRect menu = filterTypeMenuRect();
    return NSMakeRect(menu.origin.x, NSMaxY(menu) + 1.0,
        menu.size.width, 100.0);
}

NSRect pitchModeMenuRect()
{
    const NSRect panel = pitchOutputPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        676.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect voiceModeMenuRect()
{
    const NSRect panel = pitchOutputPanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        702.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect voiceModeDropdownRect()
{
    const NSRect menu = voiceModeMenuRect();
    return NSMakeRect(menu.origin.x, NSMaxY(menu) + 1.0,
        menu.size.width, 60.0);
}

NSRect pitchModeDropdownRect()
{
    const NSRect menu = pitchModeMenuRect();
    return NSMakeRect(menu.origin.x, NSMaxY(menu) + 1.0,
        menu.size.width, 60.0);
}

NSString* parameterText(const Plugin& instance, clap_id id, double value)
{
    char text[64] {};
    return paramsValueToText(&instance.plugin, id, value, text,
        static_cast<uint32_t>(sizeof(text)))
        ? [NSString stringWithUTF8String:text] : @"";
}

struct GuiSlider {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

const std::array<GuiSlider, 20u> kGuiSliders {{
    { kStartParamId, "START", 18.0, 456.0, 476.0 },
    { kLengthParamId, "LENGTH", 18.0, 456.0, 502.0 },
    { kLoopStartParamId, "LOOP START", 18.0, 456.0, 528.0 },
    { kLoopEndParamId, "LOOP END", 18.0, 456.0, 554.0 },
    { kLoopCrossfadeParamId, "LOOP XFADE", 18.0, 456.0, 580.0 },
    { kSourceTempoParamId, "SAMPLE BPM", 18.0, 456.0, 606.0 },
    { kGainParamId, "OUT", 486.0, 476.0, 372.0 },
    { kPanParamId, "PAN", 486.0, 476.0, 398.0 },
    { kVelocityParamId, "VELOCITY", 486.0, 476.0, 450.0 },
    { kFilterCutoffParamId, "CUTOFF", 486.0, 476.0, 538.0 },
    { kFilterResonanceParamId, "RESONANCE", 486.0, 476.0, 564.0 },
    { kFilterEnvelopeParamId, "ENV AMOUNT", 486.0, 476.0, 590.0 },
    { kGlideParamId, "GLIDE", 18.0, 456.0, 730.0 },
    { kTuneParamId, "TUNE", 18.0, 456.0, 756.0 },
    { kFineTuneParamId, "FINE", 18.0, 456.0, 782.0 },
    { kRootNoteParamId, "ROOT NOTE", 18.0, 456.0, 808.0 },
    { kAttackParamId, "ATTACK", 486.0, 476.0, 678.0 },
    { kDecayParamId, "DECAY", 486.0, 476.0, 704.0 },
    { kSustainParamId, "SUSTAIN", 486.0, 476.0, 730.0 },
    { kReleaseParamId, "RELEASE", 486.0, 476.0, 756.0 },
}};

bool guiSliderIsVisible(const Plugin& instance,
    const GuiSlider& slider) noexcept
{
    return paramIsExposed(instance, slider.id);
}

GuiSlider positionedGuiSlider(const Plugin& instance,
    const GuiSlider& slider) noexcept
{
    (void)instance;
    return slider;
}

NSRect sliderTrackRect(const GuiSlider& slider)
{
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(slider.panelX)),
        slider.y + 1.0, static_cast<CGFloat>(
            s3g::gui_layout::processorTrackWidth(slider.panelWidth)), 9.0);
}

NSRect sliderHitRect(const GuiSlider& slider)
{
    return NSMakeRect(slider.panelX + static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitInset),
        slider.y - 8.0, slider.panelWidth - 2.0 * static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitInset),
        static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitHeight));
}

double sliderNormalizedValue(clap_id id, double value) noexcept
{
    const auto* def = paramDef(id);
    if (!def || !(def->maximum > def->minimum)) return 0.0;
    if (id == kFilterCutoffParamId) {
        return std::clamp(std::log(value / def->minimum)
            / std::log(def->maximum / def->minimum), 0.0, 1.0);
    }
    return std::clamp((value - def->minimum)
        / (def->maximum - def->minimum), 0.0, 1.0);
}

double sliderValueFromNormalized(clap_id id, double normalized) noexcept
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    if (id == kFilterCutoffParamId) {
        return def->minimum * std::pow(
            def->maximum / def->minimum, normalized);
    }
    return def->minimum + normalized * (def->maximum - def->minimum);
}

const GuiSlider* sliderForId(clap_id id)
{
    for (const auto& slider : kGuiSliders)
        if (slider.id == id) return &slider;
    return nullptr;
}

const GuiSlider* sliderAtPoint(const Plugin& instance, NSPoint point)
{
    for (const auto& slider : kGuiSliders) {
        if (!guiSliderIsVisible(instance, slider)) continue;
        const GuiSlider positioned = positionedGuiSlider(instance, slider);
        if (NSPointInRect(point, sliderHitRect(positioned))) return &slider;
    }
    return nullptr;
}

bool isBoundaryParam(clap_id id) noexcept
{
    return id == kStartParamId || id == kLengthParamId
        || id == kLoopStartParamId || id == kLoopEndParamId;
}

double safeBoundaryDefault(const Plugin& instance, clap_id id)
{
    if (!instance.controlAsset) return paramDef(id)->defaultValue;
    const auto values = safeBoundaryValues(*instance.controlAsset);
    if (id == kStartParamId) return values[0u];
    if (id == kLengthParamId) return values[1u];
    if (id == kLoopStartParamId) return values[2u];
    if (id == kLoopEndParamId) return values[3u];
    return paramDef(id)->defaultValue;
}

NSString* const kPlayModeItems[] = {
    @"FORWARD", @"FORWARD LOOP", @"REVERSE", @"REVERSE LOOP",
    @"FORWARD PING-PONG", @"REVERSE PING-PONG",
};

NSString* const kFilterTypeItems[] = {
    @"OFF", @"LOW PASS", @"BAND PASS", @"HIGH PASS", @"NOTCH",
};

NSString* const kPitchModeItems[] = {
    @"RATE", @"STRETCH", @"RATE BELOW / STRETCH ABOVE",
};

NSString* const kSyncModeItems[] = { @"FREE", @"HOST" };

NSString* const kTriggerModeItems[] = {
    @"AUTO", @"GATE", @"ONE SHOT", @"TOGGLE",
};

NSString* const kRetriggerModeItems[] = {
    @"LAYER", @"RESTART", @"IGNORE",
};

NSString* const kVoiceModeItems[] = { @"POLY", @"MONO", @"LEGATO" };

NSString* const kMidiReceiveItems[] = {
    @"OMNI", @"CHANNEL 1", @"CHANNEL 2", @"CHANNEL 3", @"CHANNEL 4",
    @"CHANNEL 5", @"CHANNEL 6", @"CHANNEL 7", @"CHANNEL 8", @"CHANNEL 9",
    @"CHANNEL 10", @"CHANNEL 11", @"CHANNEL 12", @"CHANNEL 13",
    @"CHANNEL 14", @"CHANNEL 15", @"CHANNEL 16",
};

} // namespace

@interface S3GSamplePlayerView : NSView <NSDraggingDestination> {
    Plugin* _instance;
    NSTimer* _timer;
    NSTrackingArea* _trackingArea;
    clap_id _dragParam;
    clap_id _waveDragParam;
    BOOL _playModeMenuOpen;
    int _playModeMenuHover;
    BOOL _triggerModeMenuOpen;
    int _triggerModeMenuHover;
    BOOL _retriggerModeMenuOpen;
    int _retriggerModeMenuHover;
    BOOL _syncModeMenuOpen;
    int _syncModeMenuHover;
    BOOL _filterTypeMenuOpen;
    int _filterTypeMenuHover;
    BOOL _pitchModeMenuOpen;
    int _pitchModeMenuHover;
    BOOL _voiceModeMenuOpen;
    int _voiceModeMenuHover;
    BOOL _midiReceiveMenuOpen;
    int _midiReceiveMenuHover;
    double _waveZoom;
    double _waveViewStart;
    double _waveFixedStart;
    double _waveFixedEnd;
    double _waveFixedLoopStart;
    double _waveFixedLoopEnd;
    char _presetName[64];
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (BOOL)loadDocumentationSample;
- (BOOL)killAllPending;
- (void)closeMenus;
- (void)startTimer;
- (void)stopTimer;
@end

@implementation S3GSamplePlayerView

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _instance = instance;
    _trackingArea = nil;
    _dragParam = CLAP_INVALID_ID;
    _waveDragParam = CLAP_INVALID_ID;
    _playModeMenuOpen = NO;
    _playModeMenuHover = -1;
    _triggerModeMenuOpen = NO;
    _triggerModeMenuHover = -1;
    _retriggerModeMenuOpen = NO;
    _retriggerModeMenuHover = -1;
    _syncModeMenuOpen = NO;
    _syncModeMenuHover = -1;
    _filterTypeMenuOpen = NO;
    _filterTypeMenuHover = -1;
    _pitchModeMenuOpen = NO;
    _pitchModeMenuHover = -1;
    _voiceModeMenuOpen = NO;
    _voiceModeMenuHover = -1;
    _midiReceiveMenuOpen = NO;
    _midiReceiveMenuHover = -1;
    _waveZoom = 1.0;
    _waveViewStart = 0.0;
    _waveFixedStart = 0.0;
    _waveFixedEnd = 1.0;
    _waveFixedLoopStart = 0.0;
    _waveFixedLoopEnd = 1.0;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
    [self setWantsLayer:YES];
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)killAllPending
{
    return _instance->killRequested.load(std::memory_order_acquire);
}

- (void)closeMenus
{
    _playModeMenuOpen = NO;
    _playModeMenuHover = -1;
    _triggerModeMenuOpen = NO;
    _triggerModeMenuHover = -1;
    _retriggerModeMenuOpen = NO;
    _retriggerModeMenuHover = -1;
    _syncModeMenuOpen = NO;
    _syncModeMenuHover = -1;
    _filterTypeMenuOpen = NO;
    _filterTypeMenuHover = -1;
    _pitchModeMenuOpen = NO;
    _pitchModeMenuHover = -1;
    _voiceModeMenuOpen = NO;
    _voiceModeMenuHover = -1;
    _midiReceiveMenuOpen = NO;
    _midiReceiveMenuHover = -1;
}

- (void)updateTrackingAreas
{
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited
            | NSTrackingActiveInKeyWindow
        owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [super updateTrackingAreas];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)queueSlider:(const GuiSlider&)slider point:(NSPoint)point
{
    const auto* def = paramDef(slider.id);
    if (!def) return;
    const GuiSlider positioned = positionedGuiSlider(*_instance, slider);
    const NSRect track = sliderTrackRect(positioned);
    const double normalized = std::clamp(static_cast<double>(
        (point.x - track.origin.x) / track.size.width), 0.0, 1.0);
    queueGuiParamValue(*_instance, slider.id, clampParam(*def,
        sliderValueFromNormalized(slider.id, normalized)));
    [self setNeedsDisplay:YES];
}

- (double)waveVisibleSpan
{
    return 1.0 / std::clamp(_waveZoom, 1.0, 128.0);
}

- (double)waveNormalizedAtPoint:(NSPoint)point
{
    const NSRect wave = waveformRect();
    const double local = std::clamp(static_cast<double>(
        (point.x - wave.origin.x) / wave.size.width), 0.0, 1.0);
    return std::clamp(_waveViewStart + local * [self waveVisibleSpan],
        0.0, 1.0);
}

- (CGFloat)waveXForNormalized:(double)value
{
    const NSRect wave = waveformRect();
    const double span = [self waveVisibleSpan];
    return wave.origin.x + static_cast<CGFloat>(
        (value - _waveViewStart) / span) * wave.size.width;
}

- (clap_id)waveBoundaryAtPoint:(NSPoint)point
{
    const NSRect wave = waveformRect();
    if (!NSPointInRect(point, wave) || !_instance->controlAsset)
        return CLAP_INVALID_ID;
    const double start = paramValue(*_instance, kStartParamId);
    const double end = std::min(1.0, start
        + paramValue(*_instance, kLengthParamId));
    const double loopStart = std::clamp(
        paramValue(*_instance, kLoopStartParamId), start, end);
    const double loopEnd = std::clamp(
        paramValue(*_instance, kLoopEndParamId), loopStart, end);
    const bool upper = point.y < NSMidY(wave);
    const std::array<std::pair<clap_id, double>, 2u> candidates = upper
        ? std::array<std::pair<clap_id, double>, 2u> {{
            { kStartParamId, start }, { kLengthParamId, end },
        }}
        : std::array<std::pair<clap_id, double>, 2u> {{
            { kLoopStartParamId, loopStart },
            { kLoopEndParamId, loopEnd },
        }};
    clap_id nearest = CLAP_INVALID_ID;
    CGFloat distance = 11.0;
    for (const auto& candidate : candidates) {
        const CGFloat candidateX = [self waveXForNormalized:candidate.second];
        const CGFloat candidateDistance = std::abs(point.x - candidateX);
        if (candidateDistance < distance) {
            distance = candidateDistance;
            nearest = candidate.first;
        }
    }
    return nearest;
}

- (void)queueWaveBoundaryAtPoint:(NSPoint)point
{
    if (_waveDragParam == CLAP_INVALID_ID || !_instance->controlAsset)
        return;
    const double frameStep = 1.0 / static_cast<double>(
        std::max<uint32_t>(1u, _instance->controlAsset->frameCount()));
    const double value = [self waveNormalizedAtPoint:point];
    if (_waveDragParam == kStartParamId) {
        const double start = std::clamp(value, 0.0,
            std::max(0.0, _waveFixedEnd - frameStep));
        queueGuiParamValue(*_instance, kStartParamId, start);
        queueGuiParamValue(*_instance, kLengthParamId,
            _waveFixedEnd - start);
    } else if (_waveDragParam == kLengthParamId) {
        const double end = std::clamp(value,
            std::min(1.0, _waveFixedStart + frameStep), 1.0);
        queueGuiParamValue(*_instance, kLengthParamId,
            end - _waveFixedStart);
    } else if (_waveDragParam == kLoopStartParamId) {
        queueGuiParamValue(*_instance, kLoopStartParamId,
            std::clamp(value, _waveFixedStart,
                std::max(_waveFixedStart,
                    _waveFixedLoopEnd - frameStep)));
    } else if (_waveDragParam == kLoopEndParamId) {
        queueGuiParamValue(*_instance, kLoopEndParamId,
            std::clamp(value,
                std::min(_waveFixedEnd,
                    _waveFixedLoopStart + frameStep), _waveFixedEnd));
    }
    [self setNeedsDisplay:YES];
}

- (void)loadSample:(id)sender
{
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    if ([panel runModal] != NSModalResponseOK || ![panel URL]) return;
    const char* path = [[[panel URL] path] fileSystemRepresentation];
    if (path) queueSampleLoad(*_instance, path);
}

- (BOOL)loadDocumentationSample
{
    const char* path = std::getenv("S3G_GUI_DOCUMENTATION_SAMPLE_PATH");
    if (!path || !path[0]) return NO;
    std::shared_ptr<const SampleAsset> asset;
    std::string error;
    if (!decodeSampleFile(path, asset, error) || !asset
        || asset->channelCount > _instance->outputChannelCount
        || !installDecodedSample(*_instance, path, path,
            std::move(asset)))
        return NO;
    if (_instance->storageMode == StorageMode::Project)
        _instance->projectCopyPending = true;
    setParam(*_instance, kPlayModeParamId,
        static_cast<double>(PlayMode::ForwardLoop));
    setParam(*_instance, kPitchModeParamId,
        static_cast<double>(PitchMode::Stretch));
    setParam(*_instance, kLoopCrossfadeParamId, 0.08);
    setParam(*_instance, kFilterTypeParamId,
        static_cast<double>(FilterType::LowPass));
    setParam(*_instance, kFilterCutoffParamId, 3200.0);
    setParam(*_instance, kFilterResonanceParamId, 0.42);
    setParam(*_instance, kFilterEnvelopeParamId, 0.35);
    const auto defaults = safeBoundaryValues(*_instance->controlAsset);
    const double start = defaults[0u];
    const double end = std::min(1.0, start + defaults[1u]);
    setParam(*_instance, kLoopStartParamId,
        start + (end - start) * 0.24);
    setParam(*_instance, kLoopEndParamId,
        start + (end - start) * 0.76);
    constexpr std::array<float, 3u> documentationCursorPositions {{
        0.18f, 0.47f, 0.71f,
    }};
    constexpr std::array<uint8_t, 3u> documentationCursorKeys {{
        48u, 55u, 60u,
    }};
    for (std::size_t cursor = 0u;
         cursor < documentationCursorPositions.size(); ++cursor) {
        _instance->voiceCursorPositions[cursor].store(
            documentationCursorPositions[cursor],
            std::memory_order_relaxed);
        _instance->voiceCursorKeys[cursor].store(
            documentationCursorKeys[cursor], std::memory_order_relaxed);
    }
    _instance->voiceCursorCount.store(
        static_cast<uint32_t>(documentationCursorPositions.size()),
        std::memory_order_release);
    _waveZoom = 1.0;
    _waveViewStart = 0.0;
    _instance->status = "CROSSFADING LOOP READY";
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
    return YES;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    return [[sender draggingPasteboard] canReadObjectForClasses:
        @[ [NSURL class] ] options:@{
            NSPasteboardURLReadingFileURLsOnlyKey: @YES }]
        ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ] options:@{
            NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    if (urls.count == 0u) return NO;
    const char* path = [[urls[0u] path] fileSystemRepresentation];
    if (!path) return NO;
    queueSampleLoad(*_instance, path);
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];

    if (_playModeMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            playModeDropdownRect(), 20.0, 6u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kPlayModeParamId,
                static_cast<double>(selected));
            return;
        }
    }
    if (_triggerModeMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            triggerModeDropdownRect(), 20.0, 4u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kTriggerModeParamId,
                static_cast<double>(selected));
            return;
        }
    }
    if (_retriggerModeMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            retriggerModeDropdownRect(), 20.0, 3u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kRetriggerModeParamId,
                static_cast<double>(selected));
            return;
        }
    }
    if (_syncModeMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            syncModeDropdownRect(), 20.0, 2u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kSyncModeParamId,
                static_cast<double>(selected));
            return;
        }
    }
    if (_filterTypeMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            filterTypeDropdownRect(), 20.0, 5u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kFilterTypeParamId,
                static_cast<double>(selected));
            return;
        }
    }
    if (_pitchModeMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            pitchModeDropdownRect(), 20.0, 3u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kPitchModeParamId,
                static_cast<double>(selected));
            return;
        }
    }
    if (_voiceModeMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            voiceModeDropdownRect(), 20.0, 3u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kVoiceModeParamId,
                static_cast<double>(selected));
            return;
        }
    }
    if (_midiReceiveMenuOpen) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            midiReceiveDropdownRect(), 20.0, 17u);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            queueGuiParamValue(*_instance, kMidiReceiveParamId,
                static_cast<double>(selected));
            return;
        }
    }

    const bool initAction = NSPointInRect(point,
        s3g::clap_gui::cocoaRect(kSampleTitleBand.presetMenu));
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &_instance->plugin, @"s3g Sample Player", kSampleTitleBand,
            _presetName, sizeof(_presetName))) {
        if (initAction && _instance->controlAsset)
            applySafeDefaultBounds(*_instance, *_instance->controlAsset);
        if (initAction) {
            _waveZoom = 1.0;
            _waveViewStart = 0.0;
        }
        markStateDirty(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, killAllButtonRect())) {
        [self closeMenus];
        _instance->killRequested.store(true, std::memory_order_release);
        requestProcess(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, sampleLoadButtonRect())) {
        [self loadSample:nil];
        return;
    }
    if (NSPointInRect(point, storageButtonRect())) {
        setStorageMode(*_instance, nextStorageMode(_instance->storageMode));
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, playModeMenuRect())) {
        [self closeMenus];
        _playModeMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, triggerModeMenuRect())) {
        [self closeMenus];
        _triggerModeMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, retriggerModeMenuRect())) {
        [self closeMenus];
        _retriggerModeMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, syncModeMenuRect())) {
        [self closeMenus];
        _syncModeMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, filterTypeMenuRect())) {
        [self closeMenus];
        _filterTypeMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, pitchModeMenuRect())) {
        [self closeMenus];
        _pitchModeMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, voiceModeMenuRect())) {
        [self closeMenus];
        _voiceModeMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, midiReceiveMenuRect())) {
        [self closeMenus];
        _midiReceiveMenuOpen = YES;
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, waveformRect())
        && _instance->controlAsset) {
        _waveDragParam = [self waveBoundaryAtPoint:point];
        if (_waveDragParam != CLAP_INVALID_ID) {
            _waveFixedStart = paramValue(*_instance, kStartParamId);
            _waveFixedEnd = std::min(1.0, _waveFixedStart
                + paramValue(*_instance, kLengthParamId));
            _waveFixedLoopStart = std::clamp(paramValue(*_instance,
                kLoopStartParamId), _waveFixedStart, _waveFixedEnd);
            _waveFixedLoopEnd = std::clamp(paramValue(*_instance,
                kLoopEndParamId), _waveFixedLoopStart, _waveFixedEnd);
            [self queueWaveBoundaryAtPoint:point];
            return;
        }
        if ([event clickCount] >= 2) {
            _waveZoom = 1.0;
            _waveViewStart = 0.0;
            [self setNeedsDisplay:YES];
            return;
        }
    }

    const GuiSlider* slider = sliderAtPoint(*_instance, point);
    if (slider) {
        double resetValue = 0.0;
        const bool reset = isBoundaryParam(slider->id)
            && _instance->controlAsset
            ? [event clickCount] >= 2
            : s3g::clap_gui::sliderDoubleClickDefault(event,
                &_instance->plugin, slider->id, &resetValue);
        if (reset) {
            if (isBoundaryParam(slider->id) && _instance->controlAsset)
                resetValue = safeBoundaryDefault(*_instance, slider->id);
            queueGuiParamValue(*_instance, slider->id, resetValue);
        } else {
            _dragParam = slider->id;
            [self queueSlider:*slider point:point];
        }
        [self setNeedsDisplay:YES];
        return;
    }
    [super mouseDown:event];
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_waveDragParam != CLAP_INVALID_ID) {
        [self queueWaveBoundaryAtPoint:point];
        return;
    }
    const GuiSlider* slider = sliderForId(_dragParam);
    if (!slider) return;
    [self queueSlider:*slider point:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _waveDragParam = CLAP_INVALID_ID;
}

- (void)mouseMoved:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_playModeMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            playModeDropdownRect(), 20.0, 6u);
        if (hover != _playModeMenuHover) {
            _playModeMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    } else if (_triggerModeMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            triggerModeDropdownRect(), 20.0, 4u);
        if (hover != _triggerModeMenuHover) {
            _triggerModeMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    } else if (_retriggerModeMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            retriggerModeDropdownRect(), 20.0, 3u);
        if (hover != _retriggerModeMenuHover) {
            _retriggerModeMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    } else if (_syncModeMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            syncModeDropdownRect(), 20.0, 2u);
        if (hover != _syncModeMenuHover) {
            _syncModeMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    } else if (_filterTypeMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            filterTypeDropdownRect(), 20.0, 5u);
        if (hover != _filterTypeMenuHover) {
            _filterTypeMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    } else if (_pitchModeMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            pitchModeDropdownRect(), 20.0, 3u);
        if (hover != _pitchModeMenuHover) {
            _pitchModeMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    } else if (_voiceModeMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            voiceModeDropdownRect(), 20.0, 3u);
        if (hover != _voiceModeMenuHover) {
            _voiceModeMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    } else if (_midiReceiveMenuOpen) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            midiReceiveDropdownRect(), 20.0, 17u);
        if (hover != _midiReceiveMenuHover) {
            _midiReceiveMenuHover = hover;
            [self setNeedsDisplay:YES];
        }
    }
}

- (void)scrollWheel:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (!NSPointInRect(point, waveformRect()) || !_instance->controlAsset) {
        [super scrollWheel:event];
        return;
    }
    const double oldSpan = [self waveVisibleSpan];
    const bool pan = ([event modifierFlags] & NSEventModifierFlagShift) != 0
        || std::abs([event scrollingDeltaX])
            > std::abs([event scrollingDeltaY]);
    if (pan && _waveZoom > 1.0) {
        const double delta = std::abs([event scrollingDeltaX])
                > std::abs([event scrollingDeltaY])
            ? [event scrollingDeltaX] : [event scrollingDeltaY];
        _waveViewStart = std::clamp(_waveViewStart
            + delta * oldSpan * 0.0125, 0.0, 1.0 - oldSpan);
    } else {
        const NSRect wave = waveformRect();
        const double anchor = std::clamp(static_cast<double>(
            (point.x - wave.origin.x) / wave.size.width), 0.0, 1.0);
        const double sourceAnchor = _waveViewStart + anchor * oldSpan;
        _waveZoom = std::clamp(_waveZoom * std::exp(
            static_cast<double>([event scrollingDeltaY]) * 0.08),
            1.0, 128.0);
        const double newSpan = [self waveVisibleSpan];
        _waveViewStart = std::clamp(sourceAnchor - anchor * newSpan,
            0.0, 1.0 - newSpan);
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_playModeMenuHover >= 0 || _triggerModeMenuHover >= 0
        || _retriggerModeMenuHover >= 0 || _syncModeMenuHover >= 0
        || _filterTypeMenuHover >= 0 || _pitchModeMenuHover >= 0
        || _voiceModeMenuHover >= 0 || _midiReceiveMenuHover >= 0) {
        _playModeMenuHover = -1;
        _triggerModeMenuHover = -1;
        _retriggerModeMenuHover = -1;
        _syncModeMenuHover = -1;
        _filterTypeMenuHover = -1;
        _pitchModeMenuHover = -1;
        _voiceModeMenuHover = -1;
        _midiReceiveMenuHover = -1;
        [self setNeedsDisplay:YES];
    }
}

- (void)startTimer
{
    if (_timer) return;
    __weak S3GSamplePlayerView* weakSelf = self;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
        repeats:YES block:^(NSTimer*) {
            S3GSamplePlayerView* view = weakSelf;
            if (!view) return;
            serviceSampleLoads(*view->_instance);
            [view setNeedsDisplay:YES];
        }];
}

- (void)stopTimer
{
    [_timer invalidate];
    _timer = nil;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill([self bounds]);

    NSString* title = [NSString stringWithUTF8String:
        _instance->plugin.desc->name];
    s3g::clap_gui::drawProcessorTitleBand(title,
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(_instance->outputPeak.load(
            std::memory_order_relaxed)), kSampleTitleBand,
        titleAttrs, labelAttrs, valueAttrs, style);

    const NSRect samplePanel = samplePanelRect();
    s3g::clap_gui::drawPanelFrame(samplePanel.origin.x,
        samplePanel.origin.y, samplePanel.size.width,
        samplePanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SAMPLE", true,
        samplePanel.origin.x, samplePanel.origin.y, samplePanel.size.width,
        static_cast<CGFloat>(s3g::gui_layout::kStandardMetrics.headerHeight),
        labelAttrs, style);
    s3g::clap_gui::drawHeaderButton(sampleLoadButtonRect(), samplePanel,
        @"LOAD SAMPLE", false, labelAttrs, style);
    NSString* storageLabel = [NSString stringWithFormat:@"STORE %s",
        s3g::sample_storage::storageModeName(_instance->storageMode)];
    s3g::clap_gui::drawHeaderButton(storageButtonRect(), samplePanel,
        storageLabel, _instance->storageMode != StorageMode::Link,
        labelAttrs, style);
    NSString* status = [NSString stringWithUTF8String:
        _instance->status.c_str()];
    s3g::clap_gui::drawBoundedRightText(status,
        NSMakeRect(390.0, kContentTop + 4.0, 300.0, 15.0),
        valueAttrs);

    const NSRect wave = waveformRect();
    [style.strip setFill];
    NSRectFill(wave);
    [style.grid setStroke];
    NSFrameRect(wave);
    auto asset = _instance->controlAsset;
    if (asset && asset->valid()) {
        const std::size_t width = static_cast<std::size_t>(wave.size.width);
        const double visibleSpan = [self waveVisibleSpan];
        const double visibleEnd = _waveViewStart + visibleSpan;
        const CGFloat usableHeight = wave.size.height - 4.0;
        const CGFloat laneHeight = usableHeight
            / static_cast<CGFloat>(asset->channelCount);
        NSDictionary* channelAttrs = s3g::clap_gui::textAttrs(
            s3g::clap_gui::color(0x676767),
            asset->channelCount > 8u ? 7.0 : 8.0);
        for (uint32_t channel = 0u; channel < asset->channelCount;
             ++channel) {
            const auto& samples = asset->channels[channel];
            const CGFloat laneY = wave.origin.y + 2.0
                + laneHeight * static_cast<CGFloat>(channel);
            const CGFloat center = laneY + laneHeight * 0.5;
            NSBezierPath* trace = [NSBezierPath bezierPath];
            [trace setLineWidth:1.0];
            for (std::size_t pixel = 0u; pixel < width; ++pixel) {
                const double firstPosition = _waveViewStart
                    + static_cast<double>(pixel) / width * visibleSpan;
                const double lastPosition = _waveViewStart
                    + static_cast<double>(pixel + 1u) / width * visibleSpan;
                const std::size_t first = std::min(samples.size() - 1u,
                    static_cast<std::size_t>(std::floor(firstPosition
                        * samples.size())));
                const std::size_t last = std::max(first + 1u,
                    std::min(samples.size(), static_cast<std::size_t>(
                        std::ceil(lastPosition * samples.size()))));
                const std::size_t boundedLast = std::min(last,
                    samples.size());
                // Keep the editor refresh cost independent of sample length.
                // A long multichannel file can otherwise make the GUI thread
                // scan tens of millions of samples at 30 Hz and starve audio.
                const std::size_t stride = std::max<std::size_t>(1u,
                    (boundedLast - first) / 32u);
                float minimum = 1.0f;
                float maximum = -1.0f;
                for (std::size_t frame = first;
                     frame < boundedLast; frame += stride) {
                    minimum = std::min(minimum, samples[frame]);
                    maximum = std::max(maximum, samples[frame]);
                }
                if (boundedLast > first) {
                    const float finalSample = samples[boundedLast - 1u];
                    minimum = std::min(minimum, finalSample);
                    maximum = std::max(maximum, finalSample);
                }
                const CGFloat x = wave.origin.x
                    + static_cast<CGFloat>(pixel);
                [trace moveToPoint:NSMakePoint(x,
                    center - static_cast<CGFloat>(maximum)
                        * laneHeight * 0.44)];
                [trace lineToPoint:NSMakePoint(x,
                    center - static_cast<CGFloat>(minimum)
                        * laneHeight * 0.44)];
            }
            [s3g::clap_gui::color(channel % 2u == 0u
                ? 0x747d78 : 0x68706c) setStroke];
            [trace stroke];
            if (channel != 0u) {
                [s3g::clap_gui::color(0x333333) setFill];
                NSRectFill(NSMakeRect(wave.origin.x + 1.0, laneY,
                    wave.size.width - 2.0, 1.0));
            }
            NSString* channelLabel = [NSString stringWithFormat:@"%02u",
                static_cast<unsigned>(channel + 1u)];
            const NSSize labelSize = [channelLabel sizeWithAttributes:
                channelAttrs];
            [channelLabel drawAtPoint:NSMakePoint(wave.origin.x + 5.0,
                    laneY + std::max<CGFloat>(0.0,
                        (laneHeight - labelSize.height) * 0.5))
                withAttributes:channelAttrs];
        }

        const double start = paramValue(*_instance, kStartParamId);
        const double end = std::min(1.0, start
            + paramValue(*_instance, kLengthParamId));
        const double loopStart = std::clamp(
            paramValue(*_instance, kLoopStartParamId), start, end);
        const double loopEnd = std::clamp(
            paramValue(*_instance, kLoopEndParamId), loopStart, end);
        struct WaveMarker {
            double position;
            NSColor* color;
            NSString* label;
            bool upper;
        };
        const std::array<WaveMarker, 4u> markers {{
            { start, style.accent, @"S", true },
            { end, style.accent, @"E", true },
            { loopStart, style.dim, @"LS", false },
            { loopEnd, style.dim, @"LE", false },
        }};
        for (const auto& marker : markers) {
            if (marker.position < _waveViewStart
                || marker.position > visibleEnd) continue;
            [marker.color setStroke];
            NSBezierPath* line = [NSBezierPath bezierPath];
            const CGFloat x = [self waveXForNormalized:marker.position];
            [line moveToPoint:NSMakePoint(x, wave.origin.y)];
            [line lineToPoint:NSMakePoint(x, NSMaxY(wave))];
            [line setLineWidth:1.5];
            [line stroke];
            const CGFloat labelY = marker.upper
                ? wave.origin.y + 3.0 : NSMaxY(wave) - 15.0;
            [marker.label drawAtPoint:NSMakePoint(std::clamp(x + 3.0,
                    wave.origin.x + 3.0, NSMaxX(wave) - 20.0), labelY)
                withAttributes:labelAttrs];
        }
        const uint32_t cursorCount = std::min<uint32_t>(
            _instance->voiceCursorCount.load(std::memory_order_acquire),
            static_cast<uint32_t>(_instance->voiceCursorPositions.size()));
        for (uint32_t cursor = 0u; cursor < cursorCount; ++cursor) {
            const float position = _instance->voiceCursorPositions[cursor]
                .load(std::memory_order_relaxed);
            if (position < _waveViewStart || position > visibleEnd)
                continue;
            NSColor* cursorColor = cursor % 3u == 0u ? style.fill
                : (cursor % 3u == 1u ? style.accent : style.dim);
            [cursorColor setStroke];
            NSBezierPath* line = [NSBezierPath bezierPath];
            const CGFloat x = [self waveXForNormalized:position];
            [line moveToPoint:NSMakePoint(x, wave.origin.y)];
            [line lineToPoint:NSMakePoint(x, NSMaxY(wave))];
            [line setLineWidth:1.25];
            [line stroke];
            NSString* cursorLabel = [NSString stringWithFormat:@"N%u",
                static_cast<unsigned>(_instance->voiceCursorKeys[cursor]
                    .load(std::memory_order_relaxed))];
            const NSSize labelSize = [cursorLabel sizeWithAttributes:
                labelAttrs];
            CGFloat labelX = x + 3.0;
            if (labelX + labelSize.width + 6.0 > NSMaxX(wave))
                labelX = x - labelSize.width - 7.0;
            const CGFloat labelY = wave.origin.y + 18.0
                + static_cast<CGFloat>(cursor % 12u) * 15.0;
            [style.bg setFill];
            NSRectFill(NSMakeRect(labelX - 2.0, labelY - 1.0,
                labelSize.width + 5.0, labelSize.height + 2.0));
            [cursorLabel drawAtPoint:NSMakePoint(labelX, labelY)
                withAttributes:labelAttrs];
        }
        const uint64_t decodedBytes = static_cast<uint64_t>(
            asset->channelCount) * asset->frameCount() * sizeof(float);
        const bool safetyPayload = _instance->samplePath.empty();
        const bool embeddedPayload = _instance->storageMode
                == StorageMode::Embed
            && decodedBytes <= kMaximumEmbeddedAudioBytes;
        const std::string storageDetail = safetyPayload
            ? "PCM SAFETY "
                + s3g::sample_storage::formatByteCount(decodedBytes)
            : (embeddedPayload
                ? "PCM STATE "
                    + s3g::sample_storage::formatByteCount(decodedBytes)
                : s3g::sample_storage::abbreviatedPath(
                    _instance->samplePath, 42u));
        NSString* details = [NSString stringWithFormat:
            @"%u VOICES  /  %u CH  /  %u FRAMES  /  %.0f HZ  /  %@",
            static_cast<unsigned>(cursorCount),
            static_cast<unsigned>(asset->channelCount),
            static_cast<unsigned>(asset->frameCount()), asset->sampleRate,
            [NSString stringWithUTF8String:storageDetail.c_str()]];
        [details drawAtPoint:NSMakePoint(28.0, 286.0)
            withAttributes:valueAttrs];
        NSString* waveHelp = [NSString stringWithFormat:
            @"DRAG S / E / LS / LE   SCROLL ZOOM   SHIFT-SCROLL PAN   DOUBLE-CLICK FIT   ZOOM %.1fX",
            _waveZoom];
        [waveHelp drawAtPoint:NSMakePoint(28.0, 304.0)
            withAttributes:labelAttrs];
    } else {
        [@"DROP AUDIO HERE" drawAtPoint:NSMakePoint(420.0, 142.0)
            withAttributes:valueAttrs];
    }

    const std::array<std::pair<NSRect, NSString*>, 5u> panels {{
        { playbackPanelRect(), @"PLAYBACK" },
        { outputPanelRect(), @"OUTPUT / MIDI" },
        { filterPanelRect(), @"FILTER" },
        { pitchOutputPanelRect(), @"PITCH" },
        { envelopePanelRect(), @"AMP ENVELOPE" },
    }};
    for (const auto& panel : panels) {
        s3g::clap_gui::drawPanelFrame(panel.first.origin.x,
            panel.first.origin.y, panel.first.size.width,
            panel.first.size.height, style);
        s3g::clap_gui::drawPanelHeader(panel.second, true,
            panel.first.origin.x, panel.first.origin.y,
            panel.first.size.width, static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.headerHeight),
            labelAttrs, style);
    }
    s3g::clap_gui::drawHeaderButton(killAllButtonRect(), outputPanelRect(),
        @"KILL ALL", false, labelAttrs, style);

    s3g::clap_gui::drawProcessorMenu(@"PLAY MODE",
        [NSString stringWithUTF8String:playModeName(static_cast<int>(
            std::lround(paramValue(*_instance, kPlayModeParamId))))],
        372.0, playbackPanelRect().origin.x,
        playbackPanelRect().size.width, labelAttrs, valueAttrs, style);
    s3g::clap_gui::drawProcessorMenu(@"TRIGGER",
        [NSString stringWithUTF8String:triggerModeName(static_cast<int>(
            std::lround(paramValue(*_instance, kTriggerModeParamId))))],
        398.0, playbackPanelRect().origin.x,
        playbackPanelRect().size.width, labelAttrs, valueAttrs, style);
    s3g::clap_gui::drawProcessorMenu(@"RETRIGGER",
        [NSString stringWithUTF8String:retriggerModeName(static_cast<int>(
            std::lround(paramValue(*_instance, kRetriggerModeParamId))))],
        424.0, playbackPanelRect().origin.x,
        playbackPanelRect().size.width, labelAttrs, valueAttrs, style);
    s3g::clap_gui::drawProcessorMenu(@"TEMPO SYNC",
        [NSString stringWithUTF8String:syncModeName(static_cast<int>(
            std::lround(paramValue(*_instance, kSyncModeParamId))))],
        450.0, playbackPanelRect().origin.x,
        playbackPanelRect().size.width, labelAttrs, valueAttrs, style);
    s3g::clap_gui::drawProcessorMenu(@"FILTER TYPE",
        [NSString stringWithUTF8String:filterTypeName(static_cast<int>(
            std::lround(paramValue(*_instance, kFilterTypeParamId))))],
        512.0, filterPanelRect().origin.x,
        filterPanelRect().size.width, labelAttrs, valueAttrs, style);
    s3g::clap_gui::drawProcessorMenu(@"PITCH MODE",
        [NSString stringWithUTF8String:pitchModeName(static_cast<int>(
            std::lround(paramValue(*_instance, kPitchModeParamId))))],
        677.0, pitchOutputPanelRect().origin.x,
        pitchOutputPanelRect().size.width,
        labelAttrs, valueAttrs, style);
    s3g::clap_gui::drawProcessorMenu(@"VOICE MODE",
        [NSString stringWithUTF8String:voiceModeName(static_cast<int>(
            std::lround(paramValue(*_instance, kVoiceModeParamId))))],
        703.0, pitchOutputPanelRect().origin.x,
        pitchOutputPanelRect().size.width,
        labelAttrs, valueAttrs, style);
    char receiveText[32] {};
    s3g::clap_gui::drawProcessorMenu(@"RECEIVE",
        [NSString stringWithUTF8String:midiReceiveName(static_cast<int>(
            std::lround(paramValue(*_instance, kMidiReceiveParamId))),
            receiveText, sizeof(receiveText))],
        424.0, outputPanelRect().origin.x, outputPanelRect().size.width,
        labelAttrs, valueAttrs, style);

    for (const auto& slider : kGuiSliders) {
        if (!guiSliderIsVisible(*_instance, slider)) continue;
        const GuiSlider positioned = positionedGuiSlider(
            *_instance, slider);
        const double value = paramValue(*_instance, slider.id);
        const CGFloat normalized = static_cast<CGFloat>(
            sliderNormalizedValue(slider.id, value));
        s3g::clap_gui::drawProcessorSlider(
            [NSString stringWithUTF8String:slider.label],
            parameterText(*_instance, slider.id, value), normalized,
            positioned.y, positioned.panelX, positioned.panelWidth,
            labelAttrs, valueAttrs, style);
    }
    if (_instance->outputChannelCount == 16u) {
        [@"PAN DISABLED / SOURCE CHANNEL RELATIONSHIPS PRESERVED"
            drawAtPoint:NSMakePoint(536.0, 400.0)
            withAttributes:labelAttrs];
    }

    if (_playModeMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(playModeDropdownRect(), 20.0,
            kPlayModeItems, 6u, static_cast<int>(std::lround(
                paramValue(*_instance, kPlayModeParamId))),
            _playModeMenuHover,
            valueAttrs, style);
    } else if (_triggerModeMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(triggerModeDropdownRect(), 20.0,
            kTriggerModeItems, 4u, static_cast<int>(std::lround(
                paramValue(*_instance, kTriggerModeParamId))),
            _triggerModeMenuHover, valueAttrs, style);
    } else if (_retriggerModeMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(retriggerModeDropdownRect(), 20.0,
            kRetriggerModeItems, 3u, static_cast<int>(std::lround(
                paramValue(*_instance, kRetriggerModeParamId))),
            _retriggerModeMenuHover, valueAttrs, style);
    } else if (_syncModeMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(syncModeDropdownRect(), 20.0,
            kSyncModeItems, 2u, static_cast<int>(std::lround(
                paramValue(*_instance, kSyncModeParamId))),
            _syncModeMenuHover, valueAttrs, style);
    } else if (_filterTypeMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(filterTypeDropdownRect(), 20.0,
            kFilterTypeItems, 5u, static_cast<int>(std::lround(
                paramValue(*_instance, kFilterTypeParamId))),
            _filterTypeMenuHover, valueAttrs, style);
    } else if (_pitchModeMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(pitchModeDropdownRect(), 20.0,
            kPitchModeItems, 3u, static_cast<int>(std::lround(
                paramValue(*_instance, kPitchModeParamId))),
            _pitchModeMenuHover, valueAttrs, style);
    } else if (_voiceModeMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(voiceModeDropdownRect(), 20.0,
            kVoiceModeItems, 3u, static_cast<int>(std::lround(
                paramValue(*_instance, kVoiceModeParamId))),
            _voiceModeMenuHover, valueAttrs, style);
    } else if (_midiReceiveMenuOpen) {
        s3g::clap_gui::drawDropdownMenu(midiReceiveDropdownRect(), 20.0,
            kMidiReceiveItems, 17u, static_cast<int>(std::lround(
                paramValue(*_instance, kMidiReceiveParamId))),
            _midiReceiveMenuHover, valueAttrs, style);
    }
}

@end

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
    S3GSamplePlayerView* view = [[S3GSamplePlayerView alloc]
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
    [(__bridge S3GSamplePlayerView*)instance.guiView stopTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance.guiViewport,
        instance.guiView);
}

void guiDestroy(const clap_plugin_t* plugin)
{
    destroyGui(*self(plugin));
}

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

bool guiSetParent(const clap_plugin_t* plugin,
    const clap_window_t* window)
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
    [(__bridge S3GSamplePlayerView*)instance.guiView startTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return false;
    [(__bridge S3GSamplePlayerView*)instance.guiView stopTimer];
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

} // namespace

#endif

namespace {

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
    "org.s3g.s3g-dsp.sample-player",
    "s3g Sample Player 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.5.0",
    "Tempo-aware stereo sampler with configurable trigger, voice, pitch, loops, filter, and ADSR.",
    stereoFeatures,
};

const clap_plugin_descriptor_t multichannelDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-player-16",
    "s3g Sample Player 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.5.0",
    "Tempo-aware 16-output channel-preserving sampler with configurable voices and pitch.",
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
        outputChannels = 16u;
        configId = kSixteenChannelOutputConfigId;
    } else {
        return nullptr;
    }
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->outputChannelCount = outputChannels;
    instance->outputConfigId = configId;
    for (auto& position : instance->voiceCursorPositions)
        position.store(-1.0f, std::memory_order_relaxed);
    for (auto& key : instance->voiceCursorKeys)
        key.store(0u, std::memory_order_relaxed);
    instance->voiceCursorCount.store(0u, std::memory_order_relaxed);
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
