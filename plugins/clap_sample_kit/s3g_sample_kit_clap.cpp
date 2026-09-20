#include "s3g_sample_kit.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_sample_file_decode.h"
#include "../common/s3g_sample_storage.h"

#if defined(S3G_ENABLE_VSTGUI_SAMPLE_KIT_GUI)
#include "../common/s3g_clap_vstgui.h"
#include "../common/s3g_gui_layout.h"
#include "../common/s3g_vstgui_canvas.h"
#endif

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
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
#if defined(_WIN32) && defined(_MSC_VER)
#include "../common/s3g_windows_string_compat.h"
#else
#include <strings.h>
#endif
#include <thread>
#include <vector>

namespace {

using s3g::sample::EventKind;
using s3g::sample::FilterType;
using s3g::sample::PlayMode;
using s3g::sample::RenderEvent;
using s3g::sample::SampleAsset;
using s3g::sample::SampleKitEngine;
using s3g::sample::SampleKitPadSettings;
using s3g::sample::SampleKitSettings;
using s3g::sample::SampleKitVariationMode;
using s3g::sample::TriggerMode;
using s3g::sample_storage::ProjectCopyResult;
using s3g::sample_storage::ProjectFileRegistration;
using s3g::sample_storage::ProjectLocation;
using s3g::sample_storage::ReaperContext;
using s3g::sample_storage::StorageMode;

constexpr uint32_t kStateMagic = 0x4b533353u; // "S3SK"
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kGuiWidth = 1120u;
constexpr uint32_t kGuiHeight = 760u;
constexpr uint64_t kMaximumEmbeddedAudioBytes =
    1024ull * 1024ull * 1024ull;
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr std::size_t kMaximumBlockEvents =
    s3g::sample::kSampleKitMaximumBlockEvents;

constexpr clap_id kActivePairsParamId = 1u;
constexpr clap_id kMasterGainParamId = 2u;
constexpr clap_id kBaseNoteParamId = 3u;
constexpr clap_id kMidiReceiveParamId = 4u;
constexpr clap_id kGlobalTuneParamId = 5u;
constexpr clap_id kDriveParamId = 6u;
constexpr clap_id kBitDepthParamId = 7u;
constexpr clap_id kRateReductionParamId = 8u;
constexpr clap_id kNaturalGlobalParamId = 12u;
constexpr clap_id kRandomSeedParamId = 13u;
constexpr std::size_t kLegacyGlobalExposedParamCount = 8u;
constexpr std::size_t kAddedGlobalParamCount = 2u;
constexpr std::size_t kGlobalExposedParamCount
    = kLegacyGlobalExposedParamCount + kAddedGlobalParamCount;
// Retain three retired delay slots in the serialized layout so v1 kits and
// host states remain readable after removing the built-in send/return path.
constexpr std::size_t kGlobalStoredParamCount = 11u;

constexpr clap_id kPadParamBase = 1000u;
constexpr clap_id kPadParamStride = 32u;
enum PadParamOffset : clap_id {
    kPadGain = 0u,
    kPadPan,
    kPadTune,
    kPadStart,
    kPadEnd,
    kPadAttack,
    kPadDecay,
    kPadSustain,
    kPadRelease,
    kPadFilterType,
    kPadFilterCutoff,
    kPadFilterResonance,
    kPadVelocity,
    kPadChokeGroup,
    kPadOutputPair,
    kPadRetiredEffectSend,
    kPadMute,
    kPadSolo,
    kPadPlayMode,
    kPadTriggerMode,
    kPadLoopCrossfade,
    kPadNaturalEnabled,
    kPadVariationMode,
    kPadNaturalGain,
    kPadNaturalPitch,
    kPadNaturalStart,
    kPadNaturalTiming,
    kPadParameterCount,
};
constexpr std::size_t kLegacyPadStoredParamCount = kPadNaturalEnabled;
constexpr std::size_t kAddedPadParamCount = 6u;
constexpr std::array<PadParamOffset, 26u> kExposedPadOffsets {{
    kPadGain, kPadPan, kPadTune, kPadStart, kPadEnd,
    kPadAttack, kPadDecay, kPadSustain, kPadRelease,
    kPadFilterType, kPadFilterCutoff, kPadFilterResonance,
    kPadVelocity, kPadChokeGroup, kPadOutputPair,
    kPadMute, kPadSolo, kPadPlayMode, kPadTriggerMode,
    kPadLoopCrossfade,
    kPadNaturalEnabled, kPadVariationMode, kPadNaturalGain,
    kPadNaturalPitch, kPadNaturalStart, kPadNaturalTiming,
}};
constexpr std::size_t kLegacyStoredParamCount = kGlobalStoredParamCount
    + s3g::sample::kSampleKitPadCount * kLegacyPadStoredParamCount;
constexpr std::size_t kStoredParamCount = kLegacyStoredParamCount
    + kAddedGlobalParamCount
    + s3g::sample::kSampleKitPadCount * kAddedPadParamCount;
constexpr std::size_t kExposedParamCount = kGlobalExposedParamCount
    + s3g::sample::kSampleKitPadCount * kExposedPadOffsets.size();

constexpr clap_id padParamId(std::size_t pad, PadParamOffset offset) noexcept
{
    return kPadParamBase + static_cast<clap_id>(pad) * kPadParamStride
        + static_cast<clap_id>(offset);
}

struct ParamDef {
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, kGlobalStoredParamCount> kGlobalParamDefs {{
    { "Active Output Pairs", "Output", 1.0, 16.0, 1.0, true },
    { "Master Gain", "Output", -60.0, 12.0, -6.0, false },
    { "Base Note", "MIDI", 0.0, 112.0, 36.0, true },
    { "MIDI Receive", "MIDI", 0.0, 16.0, 0.0, true },
    { "Global Tune", "Pitch", -24.0, 24.0, 0.0, false },
    { "Drive", "Effects / Character", 0.0, 1.0, 0.0, false },
    { "Bit Depth", "Effects / Character", 4.0, 24.0, 24.0, true },
    { "Rate Reduction", "Effects / Character", 1.0, 32.0, 1.0, true },
    { "Delay Time", "Effects / Delay", 1.0, 2000.0, 240.0, false },
    { "Delay Feedback", "Effects / Delay", 0.0, 0.95, 0.30, false },
    { "Delay Return", "Effects / Delay", 0.0, 1.0, 0.0, false },
}};

constexpr std::array<ParamDef, kAddedGlobalParamCount> kAddedGlobalParamDefs {{
    { "Natural", "Natural", 0.0, 1.0, 1.0, true },
    { "Random Seed", "Natural", 1.0, 2147483647.0, 1.0, true },
}};

constexpr std::array<ParamDef, kPadParameterCount> kPadParamDefs {{
    { "Gain", "Mixer", -60.0, 12.0, -6.0, false },
    { "Pan", "Mixer", -1.0, 1.0, 0.0, false },
    { "Tune", "Pitch", -24.0, 24.0, 0.0, false },
    { "Start", "Sample", 0.0, 1.0, 0.0, false },
    { "End", "Sample", 0.0, 1.0, 1.0, false },
    { "Attack", "Amp Envelope", 0.0, 1.0, 0.0, false },
    { "Decay", "Amp Envelope", 0.0, 1.0, 0.0, false },
    { "Sustain", "Amp Envelope", 0.0, 1.0, 1.0, false },
    { "Release", "Amp Envelope", 0.0, 1.0, 0.005, false },
    { "Filter Type", "Filter", 0.0, 4.0, 0.0, true },
    { "Filter Cutoff", "Filter", 20.0, 20000.0, 20000.0, false },
    { "Filter Resonance", "Filter", 0.0, 1.0, 0.0, false },
    { "Velocity", "Amp", 0.0, 1.0, 1.0, false },
    { "Choke Group", "Voice", 0.0, 4.0, 0.0, true },
    { "Output Pair", "Mixer", 1.0, 16.0, 1.0, true },
    { "Effect Send", "Mixer", 0.0, 1.0, 0.0, false },
    { "Mute", "Mixer", 0.0, 1.0, 0.0, true },
    { "Solo", "Mixer", 0.0, 1.0, 0.0, true },
    { "Play Mode", "Sample", 0.0, 4.0, 0.0, true },
    { "Trigger Mode", "Voice", 0.0, 2.0, 0.0, true },
    { "Loop Crossfade", "Sample", 0.0, 0.5, 0.02, false },
    { "Natural", "Natural", 0.0, 1.0, 0.0, true },
    { "Variation Mode", "Natural", 0.0, 4.0, 0.0, true },
    { "Gain Variation", "Natural", 0.0, 3.0, 0.6, false },
    { "Pitch Variation", "Natural", 0.0, 25.0, 5.0, false },
    { "Start Variation", "Natural", 0.0, 5.0, 0.5, false },
    { "Timing Variation", "Natural", 0.0, 12.0, 2.0, false },
}};

struct StateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kStoredParamCount);
    uint8_t storageMode = static_cast<uint8_t>(StorageMode::Project);
    std::array<uint8_t, 3u> reserved {};
};

struct SlotState {
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint16_t reserved = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct SavedStateV1Body {
    std::array<double, kLegacyStoredParamCount> parameters {};
    std::array<SlotState, s3g::sample::kSampleKitPadCount> slots {};
};

struct SavedStateV2Body {
    std::array<double, kStoredParamCount> parameters {};
    std::array<std::array<SlotState,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> slots {};
};

#if defined(S3G_SAMPLE_FILE_WORKER)
struct LoadRequest {
    uint64_t generation = 0u;
    uint8_t pad = 0u;
    uint8_t variation = 0u;
    std::string path;
    ProjectLocation projectLocation;
    std::string projectError;
};

struct LoadResult {
    uint64_t generation = 0u;
    uint8_t pad = 0u;
    uint8_t variation = 0u;
    std::string sourcePath;
    std::string publishedPath;
    std::shared_ptr<const SampleAsset> asset;
    ProjectCopyResult projectCopy;
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
    SampleKitEngine engine;
    std::array<std::atomic<double>, kStoredParamCount> parameters {};
    s3g::clap_gui::ParamEventQueue<2048u> guiParamEvents {};
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
    std::array<RenderEvent, kMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>, s3g::sample::kSampleKitOutputChannels>
        scratch {};
    std::array<std::array<const SampleAsset*,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> audioAssets {};
    std::array<std::array<std::atomic<const SampleAsset*>,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> publishedAssets {};
    std::array<std::array<std::shared_ptr<const SampleAsset>,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> controlAssets {};
    std::vector<std::shared_ptr<const SampleAsset>> retainedAssets;
    std::array<std::array<std::string,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> samplePaths {};
    std::array<std::array<std::string,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> statuses {};
    std::array<std::array<ProjectFileRegistration,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> projectRegistrations {};
    StorageMode storageMode = StorageMode::Project;
    mutable std::mutex statusMutex;
    std::atomic<uint32_t> pendingAuditions { 0u };
    std::atomic<bool> killRequested { false };
    std::array<std::atomic<float>, s3g::sample::kSampleKitPadCount>
        padPeaks {};
    std::array<std::atomic<uint8_t>, s3g::sample::kSampleKitPadCount>
        lastVariations {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> activeVoices { 0u };
    bool active = false;
#if defined(S3G_SAMPLE_FILE_WORKER)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoadRequest> loadRequests;
    std::deque<LoadResult> loadResults;
    std::array<std::array<uint64_t,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> loadGenerations {};
    std::thread loaderThread;
    bool loaderStopping = false;
#endif
#if defined(S3G_ENABLE_VSTGUI_SAMPLE_KIT_GUI)
    s3g::portable_gui::foundation::EditorHost* portableGuiEditor = nullptr;
    uint32_t portableGuiWidth = kGuiWidth;
    uint32_t portableGuiHeight = kGuiHeight;
    bool portableGuiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return plugin ? static_cast<Plugin*>(plugin->plugin_data) : nullptr;
}

bool parameterLocation(clap_id id, std::size_t& index,
    const ParamDef*& definition, std::size_t* padOut = nullptr,
    PadParamOffset* offsetOut = nullptr) noexcept
{
    if (id >= kActivePairsParamId && id <= kRateReductionParamId) {
        index = static_cast<std::size_t>(id - kActivePairsParamId);
        definition = &kGlobalParamDefs[index];
        return true;
    }
    if (id >= kNaturalGlobalParamId && id <= kRandomSeedParamId) {
        const std::size_t added = static_cast<std::size_t>(
            id - kNaturalGlobalParamId);
        index = kLegacyStoredParamCount + added;
        definition = &kAddedGlobalParamDefs[added];
        return true;
    }
    if (id < kPadParamBase) return false;
    const clap_id relative = id - kPadParamBase;
    const std::size_t pad = relative / kPadParamStride;
    const clap_id offset = relative % kPadParamStride;
    if (pad >= s3g::sample::kSampleKitPadCount
        || offset >= kPadParameterCount
        || offset == kPadRetiredEffectSend) return false;
    if (offset < kLegacyPadStoredParamCount)
        index = kGlobalStoredParamCount
            + pad * kLegacyPadStoredParamCount + offset;
    else index = kLegacyStoredParamCount + kAddedGlobalParamCount
        + pad * kAddedPadParamCount
        + (offset - kLegacyPadStoredParamCount);
    definition = &kPadParamDefs[offset];
    if (padOut) *padOut = pad;
    if (offsetOut) *offsetOut = static_cast<PadParamOffset>(offset);
    return true;
}

clap_id parameterIdAt(std::size_t index) noexcept
{
    if (index < kGlobalExposedParamCount)
        return index < kLegacyGlobalExposedParamCount
            ? kActivePairsParamId + static_cast<clap_id>(index)
            : kNaturalGlobalParamId + static_cast<clap_id>(
                index - kLegacyGlobalExposedParamCount);
    index -= kGlobalExposedParamCount;
    const std::size_t pad = index / kExposedPadOffsets.size();
    if (pad >= s3g::sample::kSampleKitPadCount) return CLAP_INVALID_ID;
    return padParamId(pad,
        kExposedPadOffsets[index % kExposedPadOffsets.size()]);
}

const ParamDef& storedParamDefinitionAt(std::size_t index) noexcept
{
    if (index < kLegacyStoredParamCount) {
        if (index < kGlobalStoredParamCount) return kGlobalParamDefs[index];
        index -= kGlobalStoredParamCount;
        return kPadParamDefs[index % kLegacyPadStoredParamCount];
    }
    index -= kLegacyStoredParamCount;
    if (index < kAddedGlobalParamCount)
        return kAddedGlobalParamDefs[index];
    index -= kAddedGlobalParamCount;
    return kPadParamDefs[kLegacyPadStoredParamCount
        + index % kAddedPadParamCount];
}

double clampParam(const ParamDef& definition, double value) noexcept
{
    value = std::isfinite(value) ? value : definition.defaultValue;
    value = std::clamp(value, definition.minimum, definition.maximum);
    return definition.stepped ? std::round(value) : value;
}

double paramValue(const Plugin& instance, clap_id id) noexcept
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return 0.0;
    return instance.parameters[index].load(std::memory_order_acquire);
}

void markStateDirty(Plugin& instance) noexcept
{
    if (instance.hostState && instance.hostState->mark_dirty)
        instance.hostState->mark_dirty(instance.host);
}

void setParam(Plugin& instance, clap_id id, double value,
    bool dirty = false) noexcept
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return;
    instance.parameters[index].store(clampParam(*definition, value),
        std::memory_order_release);
    if (dirty) markStateDirty(instance);
}

void requestProcess(Plugin& instance) noexcept
{
    if (instance.host && instance.host->request_process)
        instance.host->request_process(instance.host);
}

void requestGuiParamService(Plugin& instance) noexcept
{
    requestProcess(instance);
    if (instance.hostParams && instance.hostParams->request_flush)
        instance.hostParams->request_flush(instance.host);
}

void queueGuiParamBegin(Plugin& instance, clap_id id)
{
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 }))
        requestGuiParamService(instance);
}

void queueGuiParamValue(Plugin& instance, clap_id id, double value)
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return;
    value = clampParam(*definition, value);
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

void queueGuiParamGesture(Plugin& instance, clap_id id, double value)
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition)) return;
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, clampParam(*definition, value) },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!instance.guiParamEvents.pushBatch(events.data(), events.size()))
        return;
    setParam(instance, id, events[1u].value, true);
    requestGuiParamService(instance);
}

void serviceGuiParamEvents(Plugin& instance,
    const clap_output_events_t* output) noexcept
{
    if (instance.guiParamConsumer.test_and_set(std::memory_order_acquire))
        return;
    s3g::clap_gui::ParamEvent pending {};
    while (instance.guiParamEvents.peek(pending)) {
        if (!s3g::clap_gui::pushParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            setParam(instance, pending.paramId, pending.value);
        instance.guiParamEvents.pop();
    }
    instance.guiParamConsumer.clear(std::memory_order_release);
}

SampleKitSettings settingsSnapshot(const Plugin& instance) noexcept
{
    SampleKitSettings settings;
    settings.activeOutputPairs = static_cast<uint8_t>(std::lround(
        paramValue(instance, kActivePairsParamId)));
    settings.masterGainDecibels = static_cast<float>(
        paramValue(instance, kMasterGainParamId));
    settings.baseNote = static_cast<uint8_t>(std::lround(
        paramValue(instance, kBaseNoteParamId)));
    settings.drive = static_cast<float>(paramValue(instance, kDriveParamId));
    settings.bitDepth = static_cast<uint8_t>(std::lround(
        paramValue(instance, kBitDepthParamId)));
    settings.rateReduction = static_cast<uint8_t>(std::lround(
        paramValue(instance, kRateReductionParamId)));
    settings.naturalEnabled = paramValue(
        instance, kNaturalGlobalParamId) >= 0.5;
    settings.randomSeed = static_cast<uint32_t>(std::llround(
        paramValue(instance, kRandomSeedParamId)));
    const float globalTune = static_cast<float>(
        paramValue(instance, kGlobalTuneParamId));
    for (std::size_t pad = 0u; pad < settings.pads.size(); ++pad) {
        auto& value = settings.pads[pad];
        value.gainDecibels = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadGain)));
        value.pan = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadPan)));
        value.tuneSemitones = globalTune + static_cast<float>(paramValue(
            instance, padParamId(pad, kPadTune)));
        value.start = paramValue(instance, padParamId(pad, kPadStart));
        value.end = paramValue(instance, padParamId(pad, kPadEnd));
        value.attackProportion = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadAttack)));
        value.decayProportion = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadDecay)));
        value.sustain = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadSustain)));
        value.releaseProportion = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadRelease)));
        value.filterType = static_cast<FilterType>(static_cast<uint8_t>(
            std::lround(paramValue(instance,
                padParamId(pad, kPadFilterType)))));
        value.filterCutoffHz = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadFilterCutoff)));
        value.filterResonance = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadFilterResonance)));
        value.velocitySensitivity = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadVelocity)));
        value.chokeGroup = static_cast<uint8_t>(std::lround(paramValue(
            instance, padParamId(pad, kPadChokeGroup))));
        value.outputPair = static_cast<uint8_t>(std::lround(paramValue(
            instance, padParamId(pad, kPadOutputPair))) - 1.0);
        value.muted = paramValue(instance,
            padParamId(pad, kPadMute)) >= 0.5;
        value.soloed = paramValue(instance,
            padParamId(pad, kPadSolo)) >= 0.5;
        const int play = static_cast<int>(std::lround(paramValue(instance,
            padParamId(pad, kPadPlayMode))));
        constexpr std::array<PlayMode, 5u> playModes {{
            PlayMode::Forward, PlayMode::Reverse,
            PlayMode::ForwardLoop, PlayMode::ReverseLoop,
            PlayMode::ForwardPingPong,
        }};
        value.playMode = playModes[static_cast<std::size_t>(
            std::clamp(play, 0, 4))];
        const int trigger = static_cast<int>(std::lround(paramValue(instance,
            padParamId(pad, kPadTriggerMode))));
        constexpr std::array<TriggerMode, 3u> triggerModes {{
            TriggerMode::OneShot, TriggerMode::Gate, TriggerMode::Toggle,
        }};
        value.triggerMode = triggerModes[static_cast<std::size_t>(
            std::clamp(trigger, 0, 2))];
        value.loopCrossfade = paramValue(instance,
            padParamId(pad, kPadLoopCrossfade));
        value.naturalEnabled = paramValue(instance,
            padParamId(pad, kPadNaturalEnabled)) >= 0.5;
        value.variationMode = static_cast<SampleKitVariationMode>(
            static_cast<uint8_t>(std::clamp<int>(static_cast<int>(
                std::lround(paramValue(instance,
                    padParamId(pad, kPadVariationMode)))), 0, 4)));
        value.naturalGainDecibels = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadNaturalGain)));
        value.naturalPitchCents = static_cast<float>(paramValue(instance,
            padParamId(pad, kPadNaturalPitch)));
        value.naturalStartMilliseconds = static_cast<float>(paramValue(
            instance, padParamId(pad, kPadNaturalStart)));
        value.naturalTimingMilliseconds = static_cast<float>(paramValue(
            instance, padParamId(pad, kPadNaturalTiming)));
    }
    return settings;
}

bool midiChannelAccepted(const Plugin& instance, uint8_t channel) noexcept
{
    const int configured = static_cast<int>(std::lround(
        paramValue(instance, kMidiReceiveParamId)));
    return configured == 0 || configured == static_cast<int>(channel) + 1;
}

std::size_t collectEvents(Plugin& instance,
    const clap_input_events_t* input, uint32_t frameCount) noexcept
{
    std::size_t count = 0u;
    const auto append = [&](uint32_t frame, EventKind kind, int64_t noteId,
                            uint8_t key, float velocity, uint8_t channel) {
        if (count >= instance.blockEvents.size()) return;
        instance.blockEvents[count++] = {
            std::min(frame, frameCount), kind,
            static_cast<uint64_t>(noteId < 0 ? 0 : noteId), key,
            std::clamp(velocity, 0.0f, 1.0f), channel,
        };
    };
    const uint32_t baseNote = static_cast<uint32_t>(std::lround(
        paramValue(instance, kBaseNoteParamId)));
    uint32_t audition = instance.pendingAuditions.exchange(
        0u, std::memory_order_acq_rel);
    for (uint32_t pad = 0u; pad < s3g::sample::kSampleKitPadCount; ++pad) {
        if ((audition & (1u << pad)) != 0u)
            append(0u, EventKind::NoteOn,
                static_cast<int64_t>(0x70000000u + pad),
                static_cast<uint8_t>(baseNote + pad), 1.0f, 0u);
    }
    if (!input || !input->size || !input->get) return count;
    const uint32_t eventCount = input->size(input);
    for (uint32_t index = 0u; index < eventCount; ++index) {
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
            if (event->key < 0 || event->key > 127 || event->channel < 0
                || event->channel > 15
                || !midiChannelAccepted(instance,
                    static_cast<uint8_t>(event->channel))) continue;
            const EventKind kind = header->type == CLAP_EVENT_NOTE_ON
                ? EventKind::NoteOn : header->type == CLAP_EVENT_NOTE_OFF
                    ? EventKind::NoteOff : EventKind::Choke;
            append(header->time, kind, event->note_id,
                static_cast<uint8_t>(event->key),
                static_cast<float>(event->velocity),
                static_cast<uint8_t>(event->channel));
            continue;
        }
        if (header->type == CLAP_EVENT_MIDI
            && header->size >= sizeof(clap_event_midi_t)) {
            const auto* event = reinterpret_cast<const clap_event_midi_t*>(
                header);
            const uint8_t status = event->data[0u];
            const uint8_t channel = status & 0x0fu;
            if (!midiChannelAccepted(instance, channel)) continue;
            const uint8_t command = status & 0xf0u;
            if (command == 0x90u && event->data[2u] != 0u)
                append(header->time, EventKind::NoteOn, 0,
                    event->data[1u], event->data[2u] / 127.0f, channel);
            else if (command == 0x80u || command == 0x90u)
                append(header->time, EventKind::NoteOff, 0,
                    event->data[1u], 0.0f, channel);
        }
    }
    return count;
}

std::string sampleDisplayName(const std::string& path)
{
    if (path.empty()) return "NO SAMPLE";
    const auto name = std::filesystem::u8path(path).filename().u8string();
    return name.empty() ? s3g::sample_storage::abbreviatedPath(path) : name;
}

bool publishAsset(Plugin& instance, std::size_t pad, std::size_t variation,
    std::shared_ptr<const SampleAsset> asset, std::string path,
    bool dirty)
{
    if (pad >= instance.controlAssets.size()
        || variation >= s3g::sample::kSampleKitVariationCount
        || (asset && (!asset->valid() || asset->channelCount > 2u)))
        return false;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (asset) instance.retainedAssets.push_back(asset);
        instance.controlAssets[pad][variation] = std::move(asset);
        instance.samplePaths[pad][variation] = std::move(path);
        instance.statuses[pad][variation]
            = instance.controlAssets[pad][variation]
            ? sampleDisplayName(instance.samplePaths[pad][variation]) + " / "
                + std::to_string(
                    instance.controlAssets[pad][variation]->frameCount())
                + " FRAMES"
            : "DROP OR LOAD A MONO / STEREO SAMPLE";
        instance.publishedAssets[pad][variation].store(
            instance.controlAssets[pad][variation].get(),
            std::memory_order_release);
    }
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

#if defined(S3G_SAMPLE_FILE_WORKER)
bool decodeSampleFile(const std::string& path,
    std::shared_ptr<const SampleAsset>& assetOut, std::string& error)
{
#if defined(__APPLE__)
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
        const AVAudioFramePosition sourceFrames = [file length];
        if (channels < 1u || channels > 2u || sourceFrames < 1
            || static_cast<uint64_t>(sourceFrames)
                > std::numeric_limits<uint32_t>::max()) {
            error = "USE A MONO OR STEREO SAMPLE UNDER 2^32 FRAMES";
            return false;
        }
        const auto frames = static_cast<AVAudioFrameCount>(sourceFrames);
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
#else
    if (!s3g::sample_file::decodeWaveFile(path, assetOut, error))
        return false;
    if (assetOut && assetOut->channelCount > 2u) {
        assetOut.reset();
        error = "USE A MONO OR STEREO SAMPLE";
        return false;
    }
    return true;
#endif
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
            request = std::move(instance->loadRequests.front());
            instance->loadRequests.pop_front();
        }
        LoadResult result;
        result.generation = request.generation;
        result.pad = request.pad;
        result.variation = request.variation;
        result.sourcePath = std::move(request.path);
        result.publishedPath = result.sourcePath;
        result.error = std::move(request.projectError);
        if (request.projectLocation.available()) {
            result.projectCopy = s3g::sample_storage::copyFileIntoProject(
                request.projectLocation, result.sourcePath);
            if (result.projectCopy.success)
                result.publishedPath = result.projectCopy.absolutePath;
            else if (result.error.empty())
                result.error = result.projectCopy.error;
        }
        std::string decodeError;
        try {
            if (!decodeSampleFile(result.publishedPath, result.asset,
                    decodeError)) result.asset.reset();
        } catch (...) {
            result.asset.reset();
            decodeError = "SAMPLE DECODE EXCEEDED AVAILABLE MEMORY";
        }
        if (!result.asset) result.error = std::move(decodeError);
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
        instance.loadRequests.clear();
    }
    instance.loaderCondition.notify_all();
    if (instance.loaderThread.joinable()) instance.loaderThread.join();
}

void queueSampleLoad(Plugin& instance, std::size_t pad,
    std::size_t variation, std::string path)
{
    if (pad >= s3g::sample::kSampleKitPadCount
        || variation >= s3g::sample::kSampleKitVariationCount
        || path.empty()) return;
    LoadRequest request;
    request.generation = ++instance.loadGenerations[pad][variation];
    request.pad = static_cast<uint8_t>(pad);
    request.variation = static_cast<uint8_t>(variation);
    request.path = std::move(path);
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        if (instance.storageMode == StorageMode::Project) {
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            (void)s3g::sample_storage::queryProjectLocation(context,
                request.projectLocation, &request.projectError);
        }
        instance.statuses[pad][variation] = "DECODING...";
    }
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.erase(std::remove_if(
            instance.loadRequests.begin(), instance.loadRequests.end(),
            [pad, variation](const LoadRequest& pending) {
                return pending.pad == pad
                    && pending.variation == variation;
            }), instance.loadRequests.end());
        instance.loadRequests.push_back(std::move(request));
    }
    instance.loaderCondition.notify_one();
}

void serviceLoads(Plugin& instance)
{
    std::deque<LoadResult> results;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        results.swap(instance.loadResults);
    }
    for (auto& result : results) {
        const std::size_t pad = result.pad;
        const std::size_t variation = result.variation;
        if (pad >= s3g::sample::kSampleKitPadCount
            || variation >= s3g::sample::kSampleKitVariationCount
            || result.generation
                != instance.loadGenerations[pad][variation])
            continue;
        if (!result.asset) {
            std::lock_guard<std::mutex> lock(instance.statusMutex);
            instance.statuses[pad][variation] = result.error.empty()
                ? "SAMPLE DECODE FAILED" : result.error;
            continue;
        }
        if (result.projectCopy.success) {
            const ReaperContext context = s3g::sample_storage::reaperContext(
                instance.host);
            (void)instance.projectRegistrations[pad][variation].reset(context,
                result.projectCopy.absolutePath, nullptr, nullptr,
                instance.plugin.desc->name);
        }
        (void)publishAsset(instance, pad, variation,
            std::move(result.asset), std::move(result.publishedPath), true);
    }
}
#endif

void clearSample(Plugin& instance, std::size_t pad,
    std::size_t variation)
{
    if (pad >= s3g::sample::kSampleKitPadCount
        || variation >= s3g::sample::kSampleKitVariationCount) return;
#if defined(S3G_SAMPLE_FILE_WORKER)
    ++instance.loadGenerations[pad][variation];
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.erase(std::remove_if(
            instance.loadRequests.begin(), instance.loadRequests.end(),
            [pad, variation](const LoadRequest& pending) {
                return pending.pad == pad
                    && pending.variation == variation;
            }), instance.loadRequests.end());
    }
#endif
    instance.projectRegistrations[pad][variation].clear();
    (void)publishAsset(instance, pad, variation, nullptr, "", true);
}

void cycleStorageMode(Plugin& instance)
{
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        const uint8_t next = (static_cast<uint8_t>(instance.storageMode)
            + 1u) % 3u;
        instance.storageMode = static_cast<StorageMode>(next);
    }
    markStateDirty(instance);
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

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kExposedParamCount);
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kExposedParamCount) return false;
    const clap_id id = parameterIdAt(index);
    std::size_t storageIndex = 0u;
    std::size_t pad = 0u;
    PadParamOffset offset = kPadGain;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, storageIndex, definition, &pad, &offset))
        return false;
    *info = {};
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (definition->stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    info->min_value = definition->minimum;
    info->max_value = definition->maximum;
    info->default_value = id >= kPadParamBase
            && offset == kPadOutputPair
        ? static_cast<double>(pad + 1u) : definition->defaultValue;
    std::snprintf(info->name, sizeof(info->name), "%s", definition->name);
    if (id < kPadParamBase)
        std::snprintf(info->module, sizeof(info->module), "%s",
            definition->module);
    else std::snprintf(info->module, sizeof(info->module),
        "Pad %02zu / %s", pad + 1u, definition->module);
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    std::size_t index = 0u;
    const ParamDef* definition = nullptr;
    if (!plugin || !value || !parameterLocation(id, index, definition))
        return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

const char* filterName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Off", "Low Pass", "Band Pass", "High Pass", "Notch",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}

const char* playModeName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Forward", "Reverse", "Forward Loop", "Reverse Loop", "Ping Pong",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}

const char* triggerModeName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "One Shot", "Gate", "Toggle",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}

const char* variationModeName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Cycle", "Shuffle", "Random", "No Repeat", "Velocity",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    std::size_t index = 0u;
    std::size_t pad = 0u;
    PadParamOffset offset = kPadGain;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition, &pad, &offset))
        return false;
    if (id == kMasterGainParamId || id == kGlobalTuneParamId
        || (id >= kPadParamBase
            && (offset == kPadGain || offset == kPadTune)))
        std::snprintf(display, size, "%+.2f%s", value,
            (id == kMasterGainParamId
                || (id >= kPadParamBase && offset == kPadGain))
                ? " dB" : " st");
    else if (id == kMidiReceiveParamId)
        std::snprintf(display, size, value < 0.5 ? "Omni" : "Channel %d",
            static_cast<int>(std::lround(value)));
    else if (id == kBaseNoteParamId)
        std::snprintf(display, size, "MIDI %d",
            static_cast<int>(std::lround(value)));
    else if (id == kActivePairsParamId)
        std::snprintf(display, size, "%d pairs / %d ch",
            static_cast<int>(std::lround(value)),
            static_cast<int>(std::lround(value)) * 2);
    else if (id == kBitDepthParamId)
        std::snprintf(display, size, "%d bit",
            static_cast<int>(std::lround(value)));
    else if (id == kRateReductionParamId)
        std::snprintf(display, size, "%dx hold",
            static_cast<int>(std::lround(value)));
    else if (id == kNaturalGlobalParamId
        || (id >= kPadParamBase && offset == kPadNaturalEnabled))
        std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Bypass");
    else if (id == kRandomSeedParamId)
        std::snprintf(display, size, "%u",
            static_cast<unsigned>(std::llround(value)));
    else if (id >= kPadParamBase && offset == kPadFilterType)
        std::snprintf(display, size, "%s", filterName(
            static_cast<int>(std::lround(value))));
    else if (id >= kPadParamBase && offset == kPadPlayMode)
        std::snprintf(display, size, "%s", playModeName(
            static_cast<int>(std::lround(value))));
    else if (id >= kPadParamBase && offset == kPadTriggerMode)
        std::snprintf(display, size, "%s", triggerModeName(
            static_cast<int>(std::lround(value))));
    else if (id >= kPadParamBase && offset == kPadVariationMode)
        std::snprintf(display, size, "%s", variationModeName(
            static_cast<int>(std::lround(value))));
    else if (id >= kPadParamBase && offset == kPadOutputPair)
        std::snprintf(display, size, "Pair %02d / %02d-%02d",
            static_cast<int>(std::lround(value)),
            static_cast<int>(std::lround(value)) * 2 - 1,
            static_cast<int>(std::lround(value)) * 2);
    else if (id >= kPadParamBase && offset == kPadChokeGroup)
        std::snprintf(display, size, value < 0.5 ? "Off" : "Group %d",
            static_cast<int>(std::lround(value)));
    else if (id >= kPadParamBase
        && (offset == kPadMute || offset == kPadSolo))
        std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off");
    else if (id >= kPadParamBase && offset == kPadFilterCutoff)
        std::snprintf(display, size, value >= 1000.0
            ? "%.2f kHz" : "%.0f Hz",
            value >= 1000.0 ? value / 1000.0 : value);
    else if (id >= kPadParamBase && offset == kPadPan)
        std::snprintf(display, size, "%+.2f", value);
    else if (id >= kPadParamBase && offset == kPadNaturalGain)
        std::snprintf(display, size, "%.2f dB", value);
    else if (id >= kPadParamBase && offset == kPadNaturalPitch)
        std::snprintf(display, size, "%.1f ct", value);
    else if (id >= kPadParamBase
        && (offset == kPadNaturalStart || offset == kPadNaturalTiming))
        std::snprintf(display, size, "%.2f ms", value);
    else if ((id >= kPadParamBase
            && (offset == kPadStart || offset == kPadEnd
                || offset == kPadAttack || offset == kPadDecay
                || offset == kPadSustain || offset == kPadRelease
                || offset == kPadFilterResonance
                || offset == kPadVelocity
                || offset == kPadLoopCrossfade))
        || id == kDriveParamId)
        std::snprintf(display, size, "%.1f %%", value * 100.0);
    else std::snprintf(display, size, "%.4g", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    std::size_t index = 0u;
    std::size_t pad = 0u;
    PadParamOffset offset = kPadGain;
    const ParamDef* definition = nullptr;
    if (!parameterLocation(id, index, definition, &pad, &offset))
        return false;
    if (id == kMidiReceiveParamId && strcasecmp(display, "Omni") == 0) {
        *value = 0.0;
        return true;
    }
    if (id == kNaturalGlobalParamId
        || (id >= kPadParamBase
            && (offset == kPadMute || offset == kPadSolo
                || offset == kPadNaturalEnabled))) {
        if (strcasecmp(display, "On") == 0) {
            *value = 1.0;
            return true;
        }
        if (strcasecmp(display, "Off") == 0
            || strcasecmp(display, "Bypass") == 0) {
            *value = 0.0;
            return true;
        }
    }
    if (id >= kPadParamBase && offset == kPadChokeGroup
        && strcasecmp(display, "Off") == 0) {
        *value = 0.0;
        return true;
    }
    if (id >= kPadParamBase && offset == kPadFilterType) {
        for (int choice = 0; choice < 5; ++choice)
            if (strcasecmp(display, filterName(choice)) == 0) {
                *value = choice;
                return true;
            }
    }
    if (id >= kPadParamBase && offset == kPadPlayMode) {
        for (int choice = 0; choice < 5; ++choice)
            if (strcasecmp(display, playModeName(choice)) == 0) {
                *value = choice;
                return true;
            }
    }
    if (id >= kPadParamBase && offset == kPadTriggerMode) {
        for (int choice = 0; choice < 3; ++choice)
            if (strcasecmp(display, triggerModeName(choice)) == 0) {
                *value = choice;
                return true;
            }
    }
    if (id >= kPadParamBase && offset == kPadVariationMode) {
        for (int choice = 0; choice < 5; ++choice)
            if (strcasecmp(display, variationModeName(choice)) == 0) {
                *value = choice;
                return true;
            }
    }
    const char* numeric = display;
    if (id == kMidiReceiveParamId
        && strncasecmp(numeric, "Channel", 7u) == 0)
        numeric += 7u;
    else if (id == kBaseNoteParamId
        && strncasecmp(numeric, "MIDI", 4u) == 0)
        numeric += 4u;
    else if (id >= kPadParamBase && offset == kPadOutputPair
        && strncasecmp(numeric, "Pair", 4u) == 0)
        numeric += 4u;
    else if (id >= kPadParamBase && offset == kPadChokeGroup
        && strncasecmp(numeric, "Group", 5u) == 0)
        numeric += 5u;
    char* end = nullptr;
    double parsed = std::strtod(numeric, &end);
    if (end == numeric) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (id >= kPadParamBase && offset == kPadFilterCutoff
        && strncasecmp(end, "kHz", 3u) == 0)
        parsed *= 1000.0;
    if (std::strchr(display, '%')) parsed *= 0.01;
    *value = parsed;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto& instance = *self(plugin);
    readParameterEvents(instance, input);
    serviceGuiParamEvents(instance, output);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!plugin || !stream || !stream->write) return false;
    auto& instance = *self(plugin);
    StateHeader header;
    SavedStateV2Body saved;
    for (std::size_t index = 0u; index < kStoredParamCount; ++index)
        saved.parameters[index] = instance.parameters[index].load(
            std::memory_order_acquire);
    std::array<std::array<std::shared_ptr<const SampleAsset>,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> assets;
    std::array<std::array<std::string,
        s3g::sample::kSampleKitVariationCount>,
        s3g::sample::kSampleKitPadCount> paths;
    StorageMode mode;
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        assets = instance.controlAssets;
        paths = instance.samplePaths;
        mode = instance.storageMode;
    }
    header.storageMode = static_cast<uint8_t>(mode);
    uint64_t embeddedBytes = 0u;
    for (std::size_t pad = 0u; pad < assets.size(); ++pad) {
        for (std::size_t variation = 0u;
             variation < assets[pad].size(); ++variation) {
            auto& path = paths[pad][variation];
            if (mode == StorageMode::Project
                && instance.projectRegistrations[pad][variation].registered())
                path = instance.projectRegistrations[pad][variation]
                    .absolutePath();
            if (mode == StorageMode::Project && !path.empty()) {
                const ReaperContext context
                    = s3g::sample_storage::reaperContext(instance.host);
                std::string relative;
                if (s3g::sample_storage::makeProjectRelativePath(context,
                        path, relative, nullptr)) path = relative;
            }
            auto& state = saved.slots[pad][variation];
            std::snprintf(state.path.data(), state.path.size(), "%s",
                path.c_str());
            const auto& asset = assets[pad][variation];
            if (!asset) continue;
            state.channelCount = asset->channelCount;
            state.frameCount = asset->frameCount();
            state.sampleRate = asset->sampleRate;
            const uint64_t bytes = static_cast<uint64_t>(state.channelCount)
                * state.frameCount * sizeof(float);
            if (mode == StorageMode::Embed || path.empty()) {
                if (bytes > kMaximumEmbeddedAudioBytes - embeddedBytes)
                    return false;
                embeddedBytes += bytes;
                state.embedded = 1u;
            }
        }
    }
    if (!s3g::clap_state::writeAll(stream, &header, sizeof(header))
        || !s3g::clap_state::writeAll(stream, &saved, sizeof(saved)))
        return false;
    for (std::size_t pad = 0u; pad < assets.size(); ++pad) {
        for (std::size_t variation = 0u;
             variation < assets[pad].size(); ++variation) {
            const auto& asset = assets[pad][variation];
            if (!asset || saved.slots[pad][variation].embedded == 0u)
                continue;
            for (uint8_t channel = 0u;
                 channel < asset->channelCount; ++channel) {
                const auto& samples = asset->channels[channel];
                if (!s3g::clap_state::writeAll(stream, samples.data(),
                        samples.size() * sizeof(float))) return false;
            }
        }
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!plugin || !stream || !stream->read) return false;
    auto& instance = *self(plugin);
    StateHeader header;
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic) return false;
    SavedStateV2Body saved;
    for (std::size_t index = 0u; index < kStoredParamCount; ++index)
        saved.parameters[index] = storedParamDefinitionAt(index).defaultValue;
    if (header.version == 1u
        && header.parameterCount == kLegacyStoredParamCount) {
        SavedStateV1Body legacy;
        if (!s3g::clap_state::readAll(stream, &legacy, sizeof(legacy)))
            return false;
        std::copy(legacy.parameters.begin(), legacy.parameters.end(),
            saved.parameters.begin());
        for (std::size_t pad = 0u; pad < legacy.slots.size(); ++pad)
            saved.slots[pad][0u] = legacy.slots[pad];
    } else if (header.version == kStateVersion
        && header.parameterCount == kStoredParamCount) {
        if (!s3g::clap_state::readAll(stream, &saved, sizeof(saved)))
            return false;
    } else return false;
    for (std::size_t index = 0u; index < kStoredParamCount; ++index)
        instance.parameters[index].store(clampParam(
            storedParamDefinitionAt(index), saved.parameters[index]),
            std::memory_order_release);
    const StorageMode mode = s3g::sample_storage::sanitizeStorageMode(
        header.storageMode);
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.storageMode = mode;
    }
#if defined(S3G_SAMPLE_FILE_WORKER)
    for (auto& pad : instance.loadGenerations)
        for (auto& generation : pad) ++generation;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
    }
#endif
    uint64_t embeddedBytes = 0u;
    for (const auto& pad : saved.slots) {
        for (const auto& slot : pad) {
            if (slot.embedded == 0u) continue;
            const uint64_t bytes = static_cast<uint64_t>(slot.channelCount)
                * slot.frameCount * sizeof(float);
            if (slot.channelCount == 0u || slot.channelCount > 2u
                || slot.frameCount == 0u || !(slot.sampleRate > 0.0)
                || bytes > kMaximumEmbeddedAudioBytes - embeddedBytes)
                return false;
            embeddedBytes += bytes;
        }
    }
    for (std::size_t pad = 0u; pad < saved.slots.size(); ++pad) {
        for (std::size_t variation = 0u;
             variation < saved.slots[pad].size(); ++variation) {
            instance.projectRegistrations[pad][variation].clear();
            const auto& state = saved.slots[pad][variation];
            const std::string locator(state.path.data(), strnlen(
                state.path.data(), state.path.size()));
            std::string runtimePath = locator;
            std::shared_ptr<const SampleAsset> asset;
            if (state.embedded != 0u) {
                auto decoded = std::make_shared<SampleAsset>();
                decoded->sampleRate = state.sampleRate;
                decoded->channelCount = state.channelCount;
                for (uint8_t channel = 0u; channel < state.channelCount;
                     ++channel) {
                    decoded->channels[channel].resize(state.frameCount);
                    if (!s3g::clap_state::readAll(stream,
                            decoded->channels[channel].data(),
                            decoded->channels[channel].size()
                                * sizeof(float)))
                        return false;
                }
                if (!decoded->valid()) return false;
                asset = std::move(decoded);
            } else if (!locator.empty()) {
                if (mode == StorageMode::Project
                    && !std::filesystem::u8path(locator).is_absolute()) {
                    const ReaperContext context
                        = s3g::sample_storage::reaperContext(instance.host);
                    (void)s3g::sample_storage::resolveProjectRelativePath(
                        context, locator, runtimePath, nullptr);
                }
#if defined(S3G_SAMPLE_FILE_WORKER)
                std::string error;
                if (!runtimePath.empty())
                    (void)decodeSampleFile(runtimePath, asset, error);
#endif
            }
            (void)publishAsset(instance, pad, variation, std::move(asset),
                runtimePath, false);
            if (mode == StorageMode::Project && !runtimePath.empty()) {
                const ReaperContext context
                    = s3g::sample_storage::reaperContext(instance.host);
                (void)instance.projectRegistrations[pad][variation].reset(
                    context, runtimePath, nullptr, nullptr,
                    instance.plugin.desc->name);
            }
        }
    }
    instance.killRequested.store(true, std::memory_order_release);
    if (instance.hostParams && instance.hostParams->rescan)
        instance.hostParams->rescan(instance.host, CLAP_PARAM_RESCAN_VALUES);
    requestProcess(instance);
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    *info = {};
    info->id = 20u;
    std::snprintf(info->name, sizeof(info->name), "%s", "Kit 32 Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = s3g::sample::kSampleKitOutputChannels;
    info->port_type = nullptr;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPortsExtension {
    audioPortsCount, audioPortsGet,
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
    std::snprintf(info->name, sizeof(info->name), "%s", "Kit MIDI In");
    return true;
}

const clap_plugin_note_ports_t notePortsExtension {
    notePortsCount, notePortsGet,
};

uint32_t noteNameCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(s3g::sample::kSampleKitPadCount);
}

bool noteNameGet(const clap_plugin_t* plugin, uint32_t index,
    clap_note_name_t* info)
{
    if (!info || index >= s3g::sample::kSampleKitPadCount) return false;
    *info = {};
    info->port = 0;
    info->channel = -1;
    info->key = static_cast<int16_t>(std::lround(paramValue(*self(plugin),
        kBaseNoteParamId)) + index);
    std::snprintf(info->name, sizeof(info->name), "PAD %02u", index + 1u);
    return true;
}

const clap_plugin_note_name_t noteNameExtension {
    noteNameCount, noteNameGet,
};

bool pluginInit(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (instance.host && instance.host->get_extension) {
        instance.hostParams = static_cast<const clap_host_params_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_PARAMS));
        instance.hostState = static_cast<const clap_host_state_t*>(
            instance.host->get_extension(instance.host, CLAP_EXT_STATE));
    }
#if defined(S3G_SAMPLE_FILE_WORKER)
    return startLoader(instance);
#else
    return true;
#endif
}

#if defined(S3G_ENABLE_VSTGUI_SAMPLE_KIT_GUI)
void destroyPortableGui(Plugin& instance);
#endif

void pluginDestroy(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
#if defined(S3G_ENABLE_VSTGUI_SAMPLE_KIT_GUI)
    destroyPortableGui(instance);
#endif
#if defined(S3G_SAMPLE_FILE_WORKER)
    stopLoader(instance);
#endif
    delete &instance;
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    if (!instance.engine.prepare(sampleRate, maximumFrames)) return false;
    instance.sampleRate = sampleRate;
    instance.maximumFrames = maximumFrames;
    try {
        for (auto& channel : instance.scratch)
            channel.assign(maximumFrames, 0.0f);
    } catch (...) {
        instance.engine.unprepare();
        return false;
    }
    for (std::size_t pad = 0u; pad < instance.audioAssets.size(); ++pad) {
        for (std::size_t variation = 0u;
             variation < instance.audioAssets[pad].size(); ++variation) {
            instance.audioAssets[pad][variation]
                = instance.publishedAssets[pad][variation].load(
                    std::memory_order_acquire);
            instance.engine.setPreparedAsset(pad, variation,
                instance.audioAssets[pad][variation]);
        }
    }
    instance.active = true;
    return true;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.active = false;
    instance.engine.unprepare();
    for (auto& pad : instance.audioAssets) pad.fill(nullptr);
    for (auto& channel : instance.scratch) channel.clear();
    {
        std::lock_guard<std::mutex> lock(instance.statusMutex);
        instance.retainedAssets.clear();
        for (const auto& pad : instance.controlAssets)
            for (const auto& asset : pad)
                if (asset) instance.retainedAssets.push_back(asset);
    }
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    return self(plugin)->active;
}

void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* plugin)
{
    self(plugin)->engine.reset();
}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    if (!process) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    if (process->frames_count > instance.maximumFrames
        || process->audio_outputs_count < 1u || !process->audio_outputs)
        return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(instance, process->out_events);
    for (std::size_t pad = 0u; pad < instance.audioAssets.size(); ++pad) {
        for (std::size_t variation = 0u;
             variation < instance.audioAssets[pad].size(); ++variation) {
            const auto* asset = instance.publishedAssets[pad][variation].load(
                std::memory_order_acquire);
            if (asset != instance.audioAssets[pad][variation]) {
                instance.audioAssets[pad][variation] = asset;
                instance.engine.setPreparedAsset(pad, variation, asset);
            }
        }
    }
    std::size_t eventCount = collectEvents(instance, process->in_events,
        process->frames_count);
    if (instance.killRequested.exchange(false, std::memory_order_acq_rel)) {
        instance.engine.killAll();
        eventCount = 0u;
    }
    std::array<float*, s3g::sample::kSampleKitOutputChannels> pointers {};
    for (std::size_t channel = 0u; channel < pointers.size(); ++channel)
        pointers[channel] = instance.scratch[channel].data();
    const SampleKitSettings settings = settingsSnapshot(instance);
    instance.engine.render(settings, instance.blockEvents.data(), eventCount,
        pointers.data(), s3g::sample::kSampleKitOutputChannels,
        process->frames_count);
    for (std::size_t pad = 0u; pad < instance.padPeaks.size(); ++pad)
        instance.padPeaks[pad].store(instance.engine.padPeak(pad),
            std::memory_order_relaxed);
    for (std::size_t pad = 0u; pad < instance.lastVariations.size(); ++pad)
        instance.lastVariations[pad].store(
            instance.engine.lastSelectedVariation(pad),
            std::memory_order_relaxed);
    instance.outputPeak.store(instance.engine.outputPeak(),
        std::memory_order_relaxed);
    instance.activeVoices.store(static_cast<uint32_t>(
        instance.engine.activeVoiceCount()), std::memory_order_relaxed);

    auto& output = process->audio_outputs[0u];
    const uint32_t requiredChannels = settings.activeOutputPairs * 2u;
    if (output.channel_count < requiredChannels) return CLAP_PROCESS_ERROR;
    output.constant_mask = 0u;
    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        const float* source = channel < instance.scratch.size()
            ? instance.scratch[channel].data() : nullptr;
        if (output.data32 && output.data32[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data32[channel][frame] = source ? source[frame] : 0.0f;
        } else if (output.data64 && output.data64[channel]) {
            for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                output.data64[channel][frame] = source ? source[frame] : 0.0;
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
#if defined(S3G_SAMPLE_FILE_WORKER)
    serviceLoads(*self(plugin));
#else
    (void)plugin;
#endif
}

#if defined(S3G_ENABLE_VSTGUI_SAMPLE_KIT_GUI)
#include "s3g_sample_kit_vstgui.inc"
#include "../common/s3g_clap_canvas_gui.inc"
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &audioPortsExtension;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &notePortsExtension;
    if (std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0)
        return &noteNameExtension;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0)
        return &stateExtension;
#if defined(S3G_ENABLE_VSTGUI_SAMPLE_KIT_GUI)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &portableGui;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_DRUM,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-kit",
    "s3g Sample Kit 32",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.2.2",
    "Sixteen-pad stereo sample instrument with eight variations per pad, deterministic Natural humanization, mixer, choke groups, and one to sixteen routed output pairs.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId || std::strcmp(pluginId, descriptor.id) != 0)
        return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    for (std::size_t index = 0u; index < kStoredParamCount; ++index)
        instance->parameters[index].store(
            storedParamDefinitionAt(index).defaultValue,
            std::memory_order_relaxed);
    for (std::size_t pad = 0u; pad < s3g::sample::kSampleKitPadCount; ++pad) {
        setParam(*instance, padParamId(pad, kPadOutputPair),
            static_cast<double>(pad + 1u));
        for (std::size_t variation = 0u;
             variation < s3g::sample::kSampleKitVariationCount;
             ++variation) {
            instance->statuses[pad][variation]
                = "DROP OR LOAD A MONO / STEREO SAMPLE";
            instance->publishedAssets[pad][variation].store(nullptr,
                std::memory_order_relaxed);
        }
        instance->padPeaks[pad].store(0.0f, std::memory_order_relaxed);
        instance->lastVariations[pad].store(0xffu,
            std::memory_order_relaxed);
    }
    instance->host = host;
    instance->plugin.desc = &descriptor;
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
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
