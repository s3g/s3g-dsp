#include "s3g_sample_doubles.h"
#include "s3g_sample_doubles_presets.h"
#include "s3g_sample_tempo_estimator.h"
#include "../common/s3g_clap_gui_param_queue.h"
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
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <strings.h>
#include <thread>
#include <vector>

namespace {

using s3g::sample::DoublesCrossfadeCurve;
using s3g::sample::DoublesEventKind;
using s3g::sample::DoublesRenderEvent;
using s3g::sample::DoublesSettings;
using s3g::sample::SampleAsset;
using s3g::sample::SampleDoublesEngine;

constexpr uint32_t kStateMagic = 0x44443353u; // "S3DD"
constexpr uint32_t kStateVersion = 4u;
constexpr uint32_t kGuiWidth = 1040u;
constexpr uint32_t kGuiHeight = 838u;
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr std::size_t kMaximumBlockEvents = 2048u;
constexpr uint64_t kMaximumEmbeddedAudioBytes
    = 1024ull * 1024ull * 1024ull;
constexpr clap_id kStereoOutputConfigId = 3202u;

constexpr clap_id kSpeedParamId = 1u;
constexpr clap_id kPhaseCentsParamId = 2u;
constexpr clap_id kSourceTempoParamId = 3u;
constexpr clap_id kOffsetParamId = 4u;
constexpr clap_id kPhaseStepParamId = 5u;
constexpr clap_id kStartParamId = 6u;
constexpr clap_id kEndParamId = 7u;
constexpr clap_id kLoopParamId = 8u;
constexpr clap_id kCrossfaderParamId = 9u;
constexpr clap_id kCurveParamId = 10u;
constexpr clap_id kGainParamId = 11u;
constexpr clap_id kMidiReceiveParamId = 12u;
constexpr clap_id kDeckALevelParamId = 13u;
constexpr clap_id kDeckBLevelParamId = 14u;
constexpr clap_id kLinkDecksParamId = 15u;
constexpr clap_id kLivePhaseParamId = 16u;
constexpr clap_id kCuePrerollParamId = 17u;
constexpr std::size_t kParamCount = 17u;
constexpr std::size_t kVersionTwoParamCount = 16u;
constexpr std::size_t kLegacyParamCount = 12u;

constexpr uint32_t kActionRestart = 1u << 0u;
constexpr uint32_t kActionStop = 1u << 1u;
constexpr uint32_t kActionPlay = 1u << 2u;
constexpr uint32_t kActionSync = 1u << 3u;
constexpr uint32_t kActionStepBackward = 1u << 4u;
constexpr uint32_t kActionStepForward = 1u << 5u;
constexpr uint32_t kActionPunchAOn = 1u << 6u;
constexpr uint32_t kActionPunchAOff = 1u << 7u;
constexpr uint32_t kActionPunchBOn = 1u << 8u;
constexpr uint32_t kActionPunchBOff = 1u << 9u;
constexpr uint32_t kActionToggleDeckA = 1u << 10u;
constexpr uint32_t kActionToggleDeckB = 1u << 11u;
constexpr uint32_t kActionDragAOn = 1u << 12u;
constexpr uint32_t kActionDragAOff = 1u << 13u;
constexpr uint32_t kActionDragBOn = 1u << 14u;
constexpr uint32_t kActionDragBOff = 1u << 15u;
constexpr uint32_t kActionSetCueA = 1u << 16u;
constexpr uint32_t kActionTriggerCueA = 1u << 17u;
constexpr uint32_t kActionSetCueB = 1u << 18u;
constexpr uint32_t kActionTriggerCueB = 1u << 19u;
constexpr uint32_t kActionPlaceCueA = 1u << 20u;
constexpr uint32_t kActionPlaceCueB = 1u << 21u;

constexpr uint32_t kFeedbackRestart = 1u << 0u;
constexpr uint32_t kFeedbackStop = 1u << 1u;
constexpr uint32_t kFeedbackPlay = 1u << 2u;
constexpr uint32_t kFeedbackSync = 1u << 3u;
constexpr uint32_t kFeedbackStepBackward = 1u << 4u;
constexpr uint32_t kFeedbackStepForward = 1u << 5u;
constexpr uint32_t kFeedbackPunchA = 1u << 6u;
constexpr uint32_t kFeedbackPunchB = 1u << 7u;
constexpr uint32_t kFeedbackToggleDeckA = 1u << 8u;
constexpr uint32_t kFeedbackToggleDeckB = 1u << 9u;
constexpr uint32_t kFeedbackDragA = 1u << 10u;
constexpr uint32_t kFeedbackDragB = 1u << 11u;
constexpr uint32_t kFeedbackSetCueA = 1u << 12u;
constexpr uint32_t kFeedbackTriggerCueA = 1u << 13u;
constexpr uint32_t kFeedbackSetCueB = 1u << 14u;
constexpr uint32_t kFeedbackTriggerCueB = 1u << 15u;

enum class TempoOrigin : uint8_t {
    Fallback = 0u,
    Estimated,
    Suggested,
    Manual,
    Restored,
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

constexpr std::array<ParamDef, kParamCount> kParamDefs {{
    { kSpeedParamId, "Speed", "Decks", -24.0, 12.0, -7.0, false },
    { kPhaseCentsParamId, "Phase Drift", "Deck B", -100.0, 100.0,
        0.0, false },
    { kSourceTempoParamId, "Sample BPM", "Timing", 20.0, 999.0,
        120.0, false },
    { kOffsetParamId, "Deck B Offset", "Deck B", -8.0, 8.0,
        1.0, false },
    { kPhaseStepParamId, "Phase Step", "Deck B", 0.0, 6.0,
        2.0, true },
    { kStartParamId, "Start", "Sample", 0.0, 1.0, 0.0, false },
    { kEndParamId, "End", "Sample", 0.0, 1.0, 1.0, false },
    { kLoopParamId, "Loop", "Sample", 0.0, 1.0, 0.0, true },
    { kCrossfaderParamId, "Crossfader", "Mixer", -1.0, 1.0,
        -1.0, false },
    { kCurveParamId, "Crossfader Curve", "Mixer", 0.0, 2.0,
        0.0, true },
    { kGainParamId, "Out", "Output", -60.0, 12.0, -6.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "MIDI", 0.0, 16.0,
        0.0, true },
    { kDeckALevelParamId, "Deck A Level", "Mixer", -60.0, 12.0,
        0.0, false },
    { kDeckBLevelParamId, "Deck B Level", "Mixer", -60.0, 12.0,
        0.0, false },
    { kLinkDecksParamId, "Link Decks", "Decks", 0.0, 1.0,
        1.0, true },
    { kLivePhaseParamId, "Deck B Live Phase", "Deck B", -1.0, 1.0,
        0.0, false },
    { kCuePrerollParamId, "Cue Preroll", "Sample", 0.0, 1000.0,
        150.0, false },
}};

constexpr std::array<double, 7u> kPhaseStepBeats {{
    0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0,
}};

struct SavedStateV1 {
    uint32_t magic = kStateMagic;
    uint32_t version = 1u;
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

struct SavedStateV2 {
    uint32_t magic = kStateMagic;
    uint32_t version = 2u;
    uint32_t parameterCount = static_cast<uint32_t>(kVersionTwoParamCount);
    std::array<double, kVersionTwoParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
};

struct SavedStateV3 {
    uint32_t magic = kStateMagic;
    uint32_t version = 3u;
    uint32_t parameterCount = static_cast<uint32_t>(kVersionTwoParamCount);
    std::array<double, kVersionTwoParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
    double cueA = -1.0;
    double cueB = -1.0;
    uint8_t cueValidMask = 0u;
    std::array<uint8_t, 7u> reservedCue {};
};

struct SavedStateV4 {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t parameterCount = static_cast<uint32_t>(kParamCount);
    std::array<double, kParamCount> parameters {};
    std::array<char, kMaximumPathBytes> path {};
    uint8_t embedded = 0u;
    uint8_t channelCount = 0u;
    uint8_t reserved0 = 0u;
    uint8_t reserved1 = 0u;
    uint32_t frameCount = 0u;
    double sampleRate = 0.0;
    double cueA = -1.0;
    double cueB = -1.0;
    uint8_t cueValidMask = 0u;
    std::array<uint8_t, 7u> reservedCue {};
};

struct StatePrefix {
    uint32_t magic = 0u;
    uint32_t version = 0u;
    uint32_t parameterCount = 0u;
};

static_assert(sizeof(StatePrefix) == 12u);

#if defined(__APPLE__)
struct LoadRequest {
    uint64_t generation = 0u;
    uint64_t tempoRevision = 0u;
    bool tempoOnly = false;
    std::string path;
    std::shared_ptr<const SampleAsset> asset;
};

struct LoadResult {
    uint64_t generation = 0u;
    uint64_t tempoRevision = 0u;
    bool tempoOnly = false;
    std::string path;
    std::shared_ptr<const SampleAsset> asset;
    std::shared_ptr<const SampleAsset> analyzedAsset;
    s3g::sample::TempoEstimate tempo;
    std::string error;
};
#endif

struct CursorSnapshot {
    const SampleAsset* asset = nullptr;
    double deckA = -1.0;
    double deckB = -1.0;
    double start = 0.0;
    double end = 1.0;
    double rateA = 0.0;
    double rateB = 0.0;
    uint64_t discontinuity = 0u;
    uint8_t activeMask = 0u;
    bool playing = false;
    bool loop = false;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    double sampleRate = 48000.0;
    uint32_t maximumFrames = 0u;
    SampleDoublesEngine engine;
    const SampleAsset* audioAsset = nullptr;
    std::atomic<const SampleAsset*> publishedAsset { nullptr };
    std::shared_ptr<const SampleAsset> controlAsset;
    std::vector<std::shared_ptr<const SampleAsset>> retainedAssets;
    std::string samplePath;
    std::string status { "DROP A SAMPLE OR PRESS LOAD" };
    std::array<std::atomic<double>, kParamCount> parameters {};
    std::atomic<uint64_t> parameterRevision { 0u };
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
    std::array<DoublesRenderEvent, kMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>, 2u> scratchChannels {};
    std::atomic<float> deckAPosition { -1.0f };
    std::atomic<float> deckBPosition { -1.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> playing { false };
    std::atomic<bool> processing { false };
    std::atomic<uint64_t> processBlockCount { 0u };
    // These atomics form one sequence-guarded audio-to-GUI cursor snapshot.
    // Hosts may render ahead in bursts, so the GUI uses the published rates
    // and bounds as a wall-clock motion contract instead of redrawing the
    // latest (possibly unchanged) process-block position.
    std::atomic<uint64_t> cursorPublicationSequence { 0u };
    std::atomic<const SampleAsset*> cursorAsset { nullptr };
    std::atomic<double> cursorStart { 0.0 };
    std::atomic<double> cursorEnd { 1.0 };
    std::atomic<double> cursorRateA { 0.0 };
    std::atomic<double> cursorRateB { 0.0 };
    std::atomic<uint8_t> cursorActiveMask { 0u };
    std::atomic<bool> cursorLoop { false };
    std::atomic<uint64_t> cursorDiscontinuitySerial { 0u };
    std::atomic<float> cueA { -1.0f };
    std::atomic<float> cueB { -1.0f };
    std::atomic<uint8_t> cueValidMask { 0u };
    std::atomic<uint64_t> cueRestoreRevision { 0u };
    std::atomic<bool> cueStateDirtyPending { false };
    uint64_t audioCueRestoreRevision =
        std::numeric_limits<uint64_t>::max();
    // Audio-thread-only geometry history. A bounds or loop-mode change can
    // constrain a head even without an explicit transport event, and must
    // therefore reseed the visual clock from the coherent snapshot.
    bool cursorGeometryInitialized = false;
    double cursorGeometryStart = 0.0;
    double cursorGeometryEnd = 1.0;
    bool cursorGeometryLoop = false;
    bool cursorLivePhaseInitialized = false;
    double cursorLivePhaseBeats = 0.0;
    std::atomic<uint32_t> actionRequests { 0u };
    std::atomic<float> requestedCueA { 0.0f };
    std::atomic<float> requestedCueB { 0.0f };
    std::atomic<uint32_t> actionFeedbackPulses { 0u };
    std::atomic<uint32_t> gestureHeldMask { 0u };
    std::atomic<uint64_t> sourceTempoRevision { 0u };
    std::atomic<double> estimatedTempoBpm { 0.0 };
    std::atomic<float> tempoConfidence { 0.0f };
    std::atomic<bool> tempoEstimateValid { false };
    std::atomic<bool> tempoOctaveAmbiguous { false };
    std::atomic<uint8_t> tempoOrigin {
        static_cast<uint8_t>(TempoOrigin::Fallback)
    };
    bool embedSampleInState = true;
    bool active = false;
#if defined(__APPLE__)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoadRequest> loadRequests;
    std::deque<LoadResult> loadResults;
    std::thread loaderThread;
    uint64_t loadGeneration = 0u;
    uint64_t tempoAnalysisGeneration = 0u;
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
    for (const auto& def : kParamDefs)
        if (def.id == id) return &def;
    return nullptr;
}

std::size_t paramIndex(clap_id id) noexcept
{
    return id >= kSpeedParamId && id <= kCuePrerollParamId
        ? static_cast<std::size_t>(id - kSpeedParamId) : kParamCount;
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

void markStateDirty(Plugin& instance)
{
    if (instance.host && instance.hostState
        && instance.hostState->mark_dirty)
        instance.hostState->mark_dirty(instance.host);
}

void requestCueStateDirtyOnMainThread(Plugin& instance) noexcept
{
    if (instance.cueStateDirtyPending.exchange(true,
            std::memory_order_acq_rel)) return;
    if (instance.host && instance.host->request_callback)
        instance.host->request_callback(instance.host);
}

void setParam(Plugin& instance, clap_id id, double value,
    bool dirty = false, bool estimatedTempo = false) noexcept
{
    const auto* def = paramDef(id);
    const std::size_t index = paramIndex(id);
    if (!def || index >= instance.parameters.size()) return;
    value = clampParam(*def, value);
    constexpr double kMinimumWindow = 1.0e-6;
    if (id == kStartParamId)
        value = std::min(value,
            std::max(0.0, paramValue(instance, kEndParamId)
                - kMinimumWindow));
    else if (id == kEndParamId)
        value = std::max(value,
            std::min(1.0, paramValue(instance, kStartParamId)
                + kMinimumWindow));
    const double previous = instance.parameters[index].exchange(value,
        std::memory_order_acq_rel);
    if (previous != value) {
        instance.parameterRevision.fetch_add(1u,
            std::memory_order_acq_rel);
    }
    if (id == kSourceTempoParamId && previous != value) {
        instance.sourceTempoRevision.fetch_add(1u,
            std::memory_order_acq_rel);
        instance.tempoOrigin.store(static_cast<uint8_t>(estimatedTempo
                ? TempoOrigin::Estimated : TempoOrigin::Manual),
            std::memory_order_release);
    }
    if (dirty) markStateDirty(instance);
}

void initializeParams(Plugin& instance) noexcept
{
    for (const auto& def : kParamDefs)
        setParam(instance, def.id, def.defaultValue, false);
    instance.parameterRevision.store(0u, std::memory_order_release);
    instance.sourceTempoRevision.store(0u, std::memory_order_release);
    instance.tempoOrigin.store(static_cast<uint8_t>(TempoOrigin::Fallback),
        std::memory_order_release);
}

double phaseStepBeats(const Plugin& instance) noexcept
{
    const int index = std::clamp(static_cast<int>(std::lround(
        paramValue(instance, kPhaseStepParamId))), 0, 6);
    return kPhaseStepBeats[static_cast<std::size_t>(index)];
}

DoublesSettings settingsSnapshot(const Plugin& instance) noexcept
{
    DoublesSettings settings;
    settings.sourceTempoBpm = paramValue(instance, kSourceTempoParamId);
    settings.speedSemitones = paramValue(instance, kSpeedParamId);
    settings.phaseCents = paramValue(instance, kPhaseCentsParamId);
    settings.offsetBeats = paramValue(instance, kOffsetParamId);
    settings.phaseStepBeats = phaseStepBeats(instance);
    settings.livePhaseBeats = paramValue(instance, kLivePhaseParamId);
    settings.cuePrerollMilliseconds = paramValue(
        instance, kCuePrerollParamId);
    settings.start = paramValue(instance, kStartParamId);
    settings.end = paramValue(instance, kEndParamId);
    settings.loop = paramValue(instance, kLoopParamId) >= 0.5;
    settings.crossfader = paramValue(instance, kCrossfaderParamId);
    settings.crossfadeCurve = static_cast<DoublesCrossfadeCurve>(
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(
            paramValue(instance, kCurveParamId))), 0, 2)));
    settings.deckALevelDecibels = static_cast<float>(
        paramValue(instance, kDeckALevelParamId));
    settings.deckBLevelDecibels = static_cast<float>(
        paramValue(instance, kDeckBLevelParamId));
    settings.gainDecibels = static_cast<float>(
        paramValue(instance, kGainParamId));
    settings.linkDecks = paramValue(instance, kLinkDecksParamId) >= 0.5;
    return settings;
}

void beginCursorPublication(Plugin& instance) noexcept
{
    instance.cursorPublicationSequence.fetch_add(
        1u, std::memory_order_acq_rel);
}

void endCursorPublication(Plugin& instance) noexcept
{
    instance.cursorPublicationSequence.fetch_add(
        1u, std::memory_order_release);
}

void publishCursorReset(Plugin& instance,
    const SampleAsset* asset) noexcept
{
    beginCursorPublication(instance);
    instance.cursorAsset.store(asset, std::memory_order_relaxed);
    instance.deckAPosition.store(-1.0f, std::memory_order_relaxed);
    instance.deckBPosition.store(-1.0f, std::memory_order_relaxed);
    instance.cursorStart.store(0.0, std::memory_order_relaxed);
    instance.cursorEnd.store(1.0, std::memory_order_relaxed);
    instance.cursorRateA.store(0.0, std::memory_order_relaxed);
    instance.cursorRateB.store(0.0, std::memory_order_relaxed);
    instance.cursorActiveMask.store(0u, std::memory_order_relaxed);
    instance.cursorLoop.store(false, std::memory_order_relaxed);
    instance.playing.store(false, std::memory_order_relaxed);
    instance.gestureHeldMask.store(0u, std::memory_order_relaxed);
    instance.cursorDiscontinuitySerial.fetch_add(
        1u, std::memory_order_relaxed);
    endCursorPublication(instance);
    instance.cursorGeometryInitialized = false;
    instance.cursorLivePhaseInitialized = false;
}

void publishCursorState(Plugin& instance, const SampleAsset* asset,
    const DoublesSettings& settings, bool discontinuity) noexcept
{
    double start = 0.0;
    double end = 1.0;
    double rateA = 0.0;
    double rateB = 0.0;
    if (asset && asset->valid() && asset->frameCount() > 0u) {
        const uint32_t frames = asset->frameCount();
        const uint32_t startFrame = std::min(static_cast<uint32_t>(
            std::llround(settings.start * static_cast<double>(frames))),
            frames - 1u);
        const uint32_t endFrame = std::clamp(static_cast<uint32_t>(
            std::llround(settings.end * static_cast<double>(frames))),
            startFrame + 1u, frames);
        const double frameCount = static_cast<double>(frames);
        start = static_cast<double>(startFrame) / frameCount;
        end = static_cast<double>(endFrame) / frameCount;
        const double baseRate = asset->sampleRate * std::pow(2.0,
            settings.speedSemitones / 12.0) / frameCount;
        rateA = baseRate * instance.engine.deckARateScale();
        rateB = baseRate * std::pow(2.0,
            settings.phaseCents / 1200.0)
            * instance.engine.deckBRateScale();
    }

    const bool geometryChanged = !instance.cursorGeometryInitialized
        || start != instance.cursorGeometryStart
        || end != instance.cursorGeometryEnd
        || settings.loop != instance.cursorGeometryLoop;
    discontinuity = discontinuity || geometryChanged;
    instance.cursorGeometryInitialized = true;
    instance.cursorGeometryStart = start;
    instance.cursorGeometryEnd = end;
    instance.cursorGeometryLoop = settings.loop;

    uint8_t activeMask = 0u;
    if (instance.engine.deckAActive()) activeMask |= 1u;
    if (instance.engine.deckBActive()) activeMask |= 2u;
    uint8_t cueMask = 0u;
    if (instance.engine.deckACueValid()) cueMask |= 1u;
    if (instance.engine.deckBCueValid()) cueMask |= 2u;

    beginCursorPublication(instance);
    instance.cursorAsset.store(asset, std::memory_order_relaxed);
    instance.deckAPosition.store(
        instance.engine.deckAPositionNormalized(),
        std::memory_order_relaxed);
    instance.deckBPosition.store(
        instance.engine.deckBPositionNormalized(),
        std::memory_order_relaxed);
    instance.cursorStart.store(start, std::memory_order_relaxed);
    instance.cursorEnd.store(end, std::memory_order_relaxed);
    instance.cursorRateA.store(rateA, std::memory_order_relaxed);
    instance.cursorRateB.store(rateB, std::memory_order_relaxed);
    instance.cursorActiveMask.store(activeMask, std::memory_order_relaxed);
    instance.cueA.store(instance.engine.deckACueNormalized(),
        std::memory_order_relaxed);
    instance.cueB.store(instance.engine.deckBCueNormalized(),
        std::memory_order_relaxed);
    instance.cueValidMask.store(cueMask, std::memory_order_release);
    instance.cursorLoop.store(settings.loop, std::memory_order_relaxed);
    instance.playing.store(instance.engine.playing(),
        std::memory_order_relaxed);
    uint32_t gestureMask = 0u;
    if (instance.engine.punchAHeld()) gestureMask |= kFeedbackPunchA;
    if (instance.engine.punchBHeld()) gestureMask |= kFeedbackPunchB;
    if (instance.engine.dragAHeld()) gestureMask |= kFeedbackDragA;
    if (instance.engine.dragBHeld()) gestureMask |= kFeedbackDragB;
    instance.gestureHeldMask.store(gestureMask, std::memory_order_relaxed);
    if (discontinuity) {
        instance.cursorDiscontinuitySerial.fetch_add(
            1u, std::memory_order_relaxed);
    }
    endCursorPublication(instance);
}

bool readCursorSnapshot(const Plugin& instance,
    CursorSnapshot& snapshot) noexcept
{
    // Never make the main thread wait on the audio thread. If a publication
    // happens to overlap this display tick, retain the previous visual state
    // and retry naturally on the next 30 Hz tick.
    for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        const uint64_t before = instance.cursorPublicationSequence.load(
            std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        CursorSnapshot candidate;
        candidate.asset = instance.cursorAsset.load(
            std::memory_order_relaxed);
        candidate.deckA = instance.deckAPosition.load(
            std::memory_order_relaxed);
        candidate.deckB = instance.deckBPosition.load(
            std::memory_order_relaxed);
        candidate.start = instance.cursorStart.load(
            std::memory_order_relaxed);
        candidate.end = instance.cursorEnd.load(
            std::memory_order_relaxed);
        candidate.rateA = instance.cursorRateA.load(
            std::memory_order_relaxed);
        candidate.rateB = instance.cursorRateB.load(
            std::memory_order_relaxed);
        candidate.activeMask = instance.cursorActiveMask.load(
            std::memory_order_relaxed);
        candidate.loop = instance.cursorLoop.load(
            std::memory_order_relaxed);
        candidate.playing = instance.playing.load(
            std::memory_order_relaxed);
        candidate.discontinuity =
            instance.cursorDiscontinuitySerial.load(
                std::memory_order_relaxed);
        const uint64_t after = instance.cursorPublicationSequence.load(
            std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            snapshot = candidate;
            return true;
        }
    }
    return false;
}

void requestProcess(Plugin& instance)
{
    if (instance.host && instance.host->request_process)
        instance.host->request_process(instance.host);
}

uint32_t feedbackForAction(uint32_t action) noexcept
{
    switch (action) {
    case kActionRestart: return kFeedbackRestart;
    case kActionStop: return kFeedbackStop;
    case kActionPlay: return kFeedbackPlay;
    case kActionSync: return kFeedbackSync;
    case kActionStepBackward: return kFeedbackStepBackward;
    case kActionStepForward: return kFeedbackStepForward;
    case kActionToggleDeckA: return kFeedbackToggleDeckA;
    case kActionToggleDeckB: return kFeedbackToggleDeckB;
    case kActionSetCueA: return kFeedbackSetCueA;
    case kActionTriggerCueA: return kFeedbackTriggerCueA;
    case kActionSetCueB: return kFeedbackSetCueB;
    case kActionTriggerCueB: return kFeedbackTriggerCueB;
    case kActionPlaceCueA: return kFeedbackSetCueA;
    case kActionPlaceCueB: return kFeedbackSetCueB;
    default: return 0u;
    }
}

void pulseFeedback(Plugin& instance, uint32_t feedback) noexcept
{
    if (feedback != 0u)
        instance.actionFeedbackPulses.fetch_or(feedback,
            std::memory_order_release);
}

void requestAction(Plugin& instance, uint32_t action) noexcept
{
    pulseFeedback(instance, feedbackForAction(action));
    instance.actionRequests.fetch_or(action, std::memory_order_release);
    requestProcess(instance);
}

void queueCueRestore(Plugin& instance, float cueA, float cueB,
    uint8_t validMask) noexcept
{
    instance.cueA.store(cueA, std::memory_order_relaxed);
    instance.cueB.store(cueB, std::memory_order_relaxed);
    instance.cueValidMask.store(validMask & 3u,
        std::memory_order_release);
    instance.cueRestoreRevision.fetch_add(1u,
        std::memory_order_acq_rel);
}

bool publishAsset(Plugin& instance, std::shared_ptr<const SampleAsset> asset,
    std::string path, bool dirty = true)
{
    if (asset && (!asset->valid() || asset->channelCount > 2u)) return false;
    if (asset) instance.retainedAssets.push_back(asset);
    instance.controlAsset = std::move(asset);
    instance.samplePath = std::move(path);
    queueCueRestore(instance, -1.0f, -1.0f, 0u);
    instance.publishedAsset.store(instance.controlAsset.get(),
        std::memory_order_release);
    requestProcess(instance);
    if (dirty) markStateDirty(instance);
    return true;
}

void applySafeDefaultBounds(Plugin& instance,
    const SampleAsset& asset) noexcept
{
    const auto bounds = s3g::sample::defaultSafeSampleBounds(asset);
    const double frames = static_cast<double>(
        std::max<uint32_t>(1u, asset.frameCount()));
    setParam(instance, kStartParamId,
        static_cast<double>(bounds.startFrame) / frames, false);
    setParam(instance, kEndParamId,
        static_cast<double>(bounds.endFrame) / frames, false);
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
        for (AVAudioChannelCount channel = 0u; channel < channels;
             ++channel) {
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

void retainTempoEstimate(Plugin& instance,
    const s3g::sample::TempoEstimate& tempo) noexcept
{
    instance.estimatedTempoBpm.store(tempo.valid ? tempo.bpm : 0.0,
        std::memory_order_release);
    instance.tempoConfidence.store(tempo.confidence,
        std::memory_order_release);
    instance.tempoEstimateValid.store(tempo.valid,
        std::memory_order_release);
    instance.tempoOctaveAmbiguous.store(tempo.octaveAmbiguous,
        std::memory_order_release);
}

bool installDecodedSample(Plugin& instance, const std::string& path,
    std::shared_ptr<const SampleAsset> asset,
    const s3g::sample::TempoEstimate* tempo = nullptr,
    uint64_t requestedTempoRevision = 0u)
{
    if (!asset || !asset->valid() || asset->channelCount > 2u) {
        instance.status = "INVALID MONO/STEREO SAMPLE";
        return false;
    }
    const uint8_t channels = asset->channelCount;
    applySafeDefaultBounds(instance, *asset);
    bool tempoApplied = false;
    if (tempo) {
        retainTempoEstimate(instance, *tempo);
        const bool revisionMatches = instance.sourceTempoRevision.load(
            std::memory_order_acquire) == requestedTempoRevision;
        tempoApplied = tempo->valid && tempo->confidence >= 0.62f
            && !tempo->octaveAmbiguous && revisionMatches;
        if (tempoApplied) {
            setParam(instance, kSourceTempoParamId, tempo->bpm,
                false, true);
            instance.tempoOrigin.store(static_cast<uint8_t>(
                TempoOrigin::Estimated), std::memory_order_release);
        } else if (tempo->valid && revisionMatches) {
            instance.tempoOrigin.store(static_cast<uint8_t>(
                TempoOrigin::Suggested), std::memory_order_release);
        }
    } else {
        instance.estimatedTempoBpm.store(0.0, std::memory_order_release);
        instance.tempoConfidence.store(0.0f, std::memory_order_release);
        instance.tempoEstimateValid.store(false, std::memory_order_release);
        instance.tempoOctaveAmbiguous.store(false,
            std::memory_order_release);
    }
    if (!publishAsset(instance, std::move(asset), path)) {
        instance.status = "COULD NOT PUBLISH SAMPLE";
        return false;
    }
    if (instance.host && instance.hostParams && instance.hostParams->rescan)
        instance.hostParams->rescan(instance.host, CLAP_PARAM_RESCAN_VALUES);
    const char* channelText = channels == 1u ? "MONO" : "STEREO";
    char status[128] {};
    if (tempoApplied) {
        std::snprintf(status, sizeof(status), "%s READY / BPM %.2f AUTO",
            channelText, tempo->bpm);
    } else if (tempo && tempo->valid) {
        std::snprintf(status, sizeof(status),
            "%s READY / BPM %.2f SUGGESTED%s", channelText, tempo->bpm,
            tempo->octaveAmbiguous ? " / HALF-DOUBLE?" : "");
    } else {
        std::snprintf(status, sizeof(status), "%s DOUBLES READY / BPM MANUAL",
            channelText);
    }
    instance.status = status;
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
                    request = std::move(instance.loadRequests.front());
                    instance.loadRequests.pop_front();
                }
                LoadResult result;
                result.generation = request.generation;
                result.tempoRevision = request.tempoRevision;
                result.tempoOnly = request.tempoOnly;
                result.path = std::move(request.path);
                try {
                    if (request.tempoOnly) {
                        result.analyzedAsset = request.asset;
                        if (result.analyzedAsset)
                            result.tempo = s3g::sample::estimateSampleTempo(
                                *result.analyzedAsset);
                        else result.error = "NO SAMPLE TO ANALYZE";
                    } else {
                        decodeSampleFile(result.path, result.asset,
                            result.error);
                    }
                    if (!request.tempoOnly && result.asset)
                        result.tempo = s3g::sample::estimateSampleTempo(
                            *result.asset);
                } catch (...) {
                    result.error = "SAMPLE DECODE RAN OUT OF RESOURCES";
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

void queueSampleLoad(Plugin& instance, std::string path)
{
    if (path.empty()) return;
    const uint64_t generation = ++instance.loadGeneration;
    // A tempo-only request owns a retained pointer to the asset that was
    // current when AUTO was pressed. Invalidate even an in-flight request so
    // its result can never overwrite the estimate for a subsequently loaded
    // file.
    ++instance.tempoAnalysisGeneration;
    retainTempoEstimate(instance, {});
    const uint64_t tempoRevision = instance.sourceTempoRevision.load(
        std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        // Only the newest file request can become the control asset. Pending
        // tempo-only work for the previous asset is stale too, so do not let
        // it delay the replacement decode.
        instance.loadRequests.clear();
        LoadRequest request;
        request.generation = generation;
        request.tempoRevision = tempoRevision;
        request.path = std::move(path);
        instance.loadRequests.push_back(std::move(request));
    }
    instance.status = "LOADING SAMPLE";
    instance.loaderCondition.notify_one();
}

void queueTempoAnalysis(Plugin& instance)
{
    if (!instance.controlAsset) {
        instance.status = "LOAD A SAMPLE BEFORE BPM AUTO";
        return;
    }
    LoadRequest request;
    request.generation = ++instance.tempoAnalysisGeneration;
    request.tempoRevision = instance.sourceTempoRevision.load(
        std::memory_order_acquire);
    request.tempoOnly = true;
    request.asset = instance.controlAsset;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.erase(std::remove_if(
            instance.loadRequests.begin(), instance.loadRequests.end(),
            [](const LoadRequest& pending) { return pending.tempoOnly; }),
            instance.loadRequests.end());
        instance.loadRequests.push_back(std::move(request));
    }
    instance.status = "ANALYZING SAMPLE BPM";
    instance.loaderCondition.notify_one();
}

void serviceSampleLoads(Plugin& instance)
{
    std::deque<LoadResult> completed;
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        completed.swap(instance.loadResults);
    }
    for (auto& result : completed) {
        if (result.tempoOnly) {
            if (result.generation != instance.tempoAnalysisGeneration)
                continue;
            if (!result.analyzedAsset
                || result.analyzedAsset.get()
                    != instance.controlAsset.get()) continue;
            retainTempoEstimate(instance, result.tempo);
            const bool revisionMatches =
                instance.sourceTempoRevision.load(
                    std::memory_order_acquire) == result.tempoRevision;
            if (result.tempo.valid && revisionMatches) {
                setParam(instance, kSourceTempoParamId,
                    result.tempo.bpm, true, true);
                instance.tempoOrigin.store(static_cast<uint8_t>(
                    TempoOrigin::Estimated), std::memory_order_release);
                char status[96] {};
                std::snprintf(status, sizeof(status),
                    "BPM %.2f RE-ANALYZED / %d%%%s", result.tempo.bpm,
                    static_cast<int>(std::lround(
                        result.tempo.confidence * 100.0f)),
                    result.tempo.octaveAmbiguous ? " / HALF-DOUBLE?" : "");
                instance.status = status;
                if (instance.host && instance.hostParams
                    && instance.hostParams->rescan) {
                    instance.hostParams->rescan(instance.host,
                        CLAP_PARAM_RESCAN_VALUES);
                }
            } else if (!revisionMatches) {
                instance.status = "BPM RESCAN FINISHED / MANUAL EDIT KEPT";
            } else {
                instance.status = "BPM COULD NOT BE DETECTED";
            }
            continue;
        }
        if (result.generation != instance.loadGeneration) continue;
        if (!result.asset) {
            instance.status = result.error.empty()
                ? "SAMPLE DECODE FAILED" : result.error;
            continue;
        }
        installDecodedSample(instance, result.path, std::move(result.asset),
            &result.tempo, result.tempoRevision);
    }
}
#endif

void requestGuiParamService(Plugin& instance)
{
    // CLAP defines request_flush() and request_process() as alternatives for
    // publishing a plug-in GUI edit. While process() is already running it
    // consumes this queue on the next audio block, so an additional host wake
    // request only floods the host's main/audio coordination path during a
    // continuous drag. If processing is stopped, request one appropriate
    // service path so the host still receives the edit.
    if (instance.processing.load(std::memory_order_acquire)) return;
    if (instance.hostParams && instance.hostParams->request_flush)
        instance.hostParams->request_flush(instance.host);
    else requestProcess(instance);
}

bool queueGuiParamEvent(Plugin& instance,
    s3g::clap_gui::ParamEventKind kind, clap_id id,
    double value = 0.0)
{
    if (!instance.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(instance);
    return true;
}

void queueGuiParamGestureBegin(Plugin& instance, clap_id id)
{
    (void)queueGuiParamEvent(instance,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& instance, clap_id id, double value)
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    value = clampParam(*def, value);
    if (!instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) return;
    // Publish only after the host event owns a durable queue slot. This keeps
    // the DSP and GUI responsive even if a host defers params.flush(). A
    // parameter event already makes CLAP state dirty implicitly; calling
    // host_state.mark_dirty() for every drag sample can provoke repeated
    // snapshots of the embedded audio payload.
    setParam(instance, id, value);
    requestGuiParamService(instance);
}

void queueGuiParamGestureEnd(Plugin& instance, clap_id id)
{
    (void)queueGuiParamEvent(instance,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queueGuiParamGesture(Plugin& instance, clap_id id, double value)
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    value = clampParam(*def, value);
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, value },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!instance.guiParamEvents.pushBatch(events.data(), events.size()))
        return;
    setParam(instance, id, value);
    requestGuiParamService(instance);
}

bool queueGuiFactoryPreset(Plugin& instance, uint32_t index)
{
    if (index >= s3g::sample::kDoublesFactoryPresetCount) return false;
    const DoublesSettings preset = s3g::sample::doublesFactoryPreset(
        index, settingsSnapshot(instance));
    uint32_t phaseStepIndex = 0u;
    double closestStep = std::numeric_limits<double>::max();
    for (uint32_t candidate = 0u;
         candidate < kPhaseStepBeats.size(); ++candidate) {
        const double distance = std::abs(
            kPhaseStepBeats[candidate] - preset.phaseStepBeats);
        if (distance < closestStep) {
            closestStep = distance;
            phaseStepIndex = candidate;
        }
    }
    const std::array<std::pair<clap_id, double>, 8u> values {{
        { kSpeedParamId, preset.speedSemitones },
        { kPhaseCentsParamId, preset.phaseCents },
        { kOffsetParamId, preset.offsetBeats },
        { kPhaseStepParamId, static_cast<double>(phaseStepIndex) },
        { kLoopParamId, preset.loop ? 1.0 : 0.0 },
        { kCrossfaderParamId, preset.crossfader },
        { kCurveParamId, static_cast<double>(
            static_cast<uint8_t>(preset.crossfadeCurve)) },
        { kLivePhaseParamId, preset.livePhaseBeats },
    }};
    using Kind = s3g::clap_gui::ParamEventKind;
    std::array<s3g::clap_gui::ParamEvent, values.size() * 3u> events {};
    for (uint32_t valueIndex = 0u;
         valueIndex < values.size(); ++valueIndex) {
        const uint32_t eventIndex = valueIndex * 3u;
        events[eventIndex] = {
            Kind::GestureBegin, values[valueIndex].first, 0.0 };
        events[eventIndex + 1u] = {
            Kind::Value, values[valueIndex].first,
            values[valueIndex].second };
        events[eventIndex + 2u] = {
            Kind::GestureEnd, values[valueIndex].first, 0.0 };
    }
    if (!instance.guiParamEvents.pushBatch(events.data(), events.size()))
        return false;
    for (const auto& value : values)
        setParam(instance, value.first, value.second);
    requestGuiParamService(instance);
    return true;
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
    if (pending.kind != s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_gesture_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
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
    // Hosts may call params.flush() concurrently with process(). The queue is
    // SPSC, so let either callback service it without ever allowing both to
    // become consumers at once. A missed audio-thread attempt is harmless:
    // the next flush or process block will retry without blocking realtime.
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

bool appendEvent(Plugin& instance, std::size_t& count, uint32_t frame,
    DoublesEventKind kind, uint64_t noteId = 0u,
    float value = 1.0f) noexcept
{
    if (count >= instance.blockEvents.size()) return false;
    instance.blockEvents[count++] = {
        frame, kind, noteId, value,
    };
    return true;
}

bool receivesMidi(const Plugin& instance, uint8_t channel) noexcept
{
    const int receive = static_cast<int>(std::lround(
        paramValue(instance, kMidiReceiveParamId)));
    return receive == 0 || receive == static_cast<int>(channel);
}

void appendMidiCommand(Plugin& instance, std::size_t& count,
    uint32_t frame, uint8_t key, bool noteOn, uint64_t noteId,
    float velocity) noexcept
{
    if (!noteOn) {
        if (key == s3g::sample::kDoublesMidiPunchA)
            appendEvent(instance, count, frame,
                DoublesEventKind::PunchAOff, noteId, 0.0f);
        else if (key == s3g::sample::kDoublesMidiPunchB)
            appendEvent(instance, count, frame,
                DoublesEventKind::PunchBOff, noteId, 0.0f);
        else if (key == s3g::sample::kDoublesMidiDragA)
            appendEvent(instance, count, frame,
                DoublesEventKind::DragAOff, noteId, 0.0f);
        else if (key == s3g::sample::kDoublesMidiDragB)
            appendEvent(instance, count, frame,
                DoublesEventKind::DragBOff, noteId, 0.0f);
        return;
    }
    switch (key) {
    case s3g::sample::kDoublesMidiRestart:
        pulseFeedback(instance, kFeedbackRestart);
        appendEvent(instance, count, frame, DoublesEventKind::Restart,
            noteId);
        return;
    case s3g::sample::kDoublesMidiStop:
        pulseFeedback(instance, kFeedbackStop);
        appendEvent(instance, count, frame, DoublesEventKind::Stop,
            noteId);
        return;
    case s3g::sample::kDoublesMidiSyncDeckB:
        pulseFeedback(instance, kFeedbackSync);
        appendEvent(instance, count, frame, DoublesEventKind::SyncDeckB,
            noteId);
        return;
    case s3g::sample::kDoublesMidiPhaseStepBackward:
        pulseFeedback(instance, kFeedbackStepBackward);
        appendEvent(instance, count, frame,
            DoublesEventKind::PhaseStepBackward, noteId);
        return;
    case s3g::sample::kDoublesMidiPunchA:
        pulseFeedback(instance, kFeedbackPunchA);
        appendEvent(instance, count, frame, DoublesEventKind::PunchAOn,
            noteId, velocity);
        return;
    case s3g::sample::kDoublesMidiPunchB:
        pulseFeedback(instance, kFeedbackPunchB);
        appendEvent(instance, count, frame, DoublesEventKind::PunchBOn,
            noteId, velocity);
        return;
    case s3g::sample::kDoublesMidiPhaseStepForward:
        pulseFeedback(instance, kFeedbackStepForward);
        appendEvent(instance, count, frame,
            DoublesEventKind::PhaseStepForward, noteId);
        return;
    case s3g::sample::kDoublesMidiPlay:
        pulseFeedback(instance, kFeedbackPlay);
        appendEvent(instance, count, frame, DoublesEventKind::Play,
            noteId);
        return;
    case s3g::sample::kDoublesMidiToggleDeckA:
        pulseFeedback(instance, kFeedbackToggleDeckA);
        appendEvent(instance, count, frame, DoublesEventKind::ToggleDeckA,
            noteId);
        return;
    case s3g::sample::kDoublesMidiToggleDeckB:
        pulseFeedback(instance, kFeedbackToggleDeckB);
        appendEvent(instance, count, frame, DoublesEventKind::ToggleDeckB,
            noteId);
        return;
    case s3g::sample::kDoublesMidiDragA:
        pulseFeedback(instance, kFeedbackDragA);
        appendEvent(instance, count, frame, DoublesEventKind::DragAOn,
            noteId, velocity);
        return;
    case s3g::sample::kDoublesMidiDragB:
        pulseFeedback(instance, kFeedbackDragB);
        appendEvent(instance, count, frame, DoublesEventKind::DragBOn,
            noteId, velocity);
        return;
    case s3g::sample::kDoublesMidiSetCueA:
        pulseFeedback(instance, kFeedbackSetCueA);
        appendEvent(instance, count, frame, DoublesEventKind::SetCueA,
            noteId);
        return;
    case s3g::sample::kDoublesMidiTriggerCueA:
        pulseFeedback(instance, kFeedbackTriggerCueA);
        appendEvent(instance, count, frame, DoublesEventKind::TriggerCueA,
            noteId);
        return;
    case s3g::sample::kDoublesMidiSetCueB:
        pulseFeedback(instance, kFeedbackSetCueB);
        appendEvent(instance, count, frame, DoublesEventKind::SetCueB,
            noteId);
        return;
    case s3g::sample::kDoublesMidiTriggerCueB:
        pulseFeedback(instance, kFeedbackTriggerCueB);
        appendEvent(instance, count, frame, DoublesEventKind::TriggerCueB,
            noteId);
        return;
    default:
        break;
    }
    double offset = 0.0;
    if (s3g::sample::doublesOffsetForMidiNote(key, offset)) {
        pulseFeedback(instance, kFeedbackSync);
        setParam(instance, kOffsetParamId, offset, false);
        appendEvent(instance, count, frame, DoublesEventKind::SelectOffset,
            noteId, static_cast<float>(offset));
    }
}

void applyMidiCc(Plugin& instance, uint8_t controller,
    uint8_t rawValue) noexcept
{
    const double normalized = static_cast<double>(rawValue) / 127.0;
    clap_id id = CLAP_INVALID_ID;
    switch (controller) {
    case s3g::sample::kDoublesMidiCcCrossfader:
        id = kCrossfaderParamId;
        break;
    case s3g::sample::kDoublesMidiCcDeckALevel:
        id = kDeckALevelParamId;
        break;
    case s3g::sample::kDoublesMidiCcDeckBLevel:
        id = kDeckBLevelParamId;
        break;
    case s3g::sample::kDoublesMidiCcLivePhase:
        id = kLivePhaseParamId;
        break;
    default:
        return;
    }
    const ParamDef* def = paramDef(id);
    if (!def) return;
    setParam(instance, id, def->minimum
        + normalized * (def->maximum - def->minimum), false);
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
            if (event->port_index != 0 || event->key < 0
                || event->key > 127) continue;
            const uint8_t channel = event->channel >= 0
                    && event->channel < 16
                ? static_cast<uint8_t>(event->channel + 1) : 0u;
            if (!receivesMidi(instance, channel)) continue;
            const bool noteOn = header->type == CLAP_EVENT_NOTE_ON
                && event->velocity > 0.0;
            const uint64_t noteId = event->note_id >= 0
                ? static_cast<uint64_t>(event->note_id) + 1u : 0u;
            appendMidiCommand(instance, renderEventCount, frame,
                static_cast<uint8_t>(event->key), noteOn, noteId,
                noteOn ? static_cast<float>(event->velocity) : 0.0f);
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
            if (!receivesMidi(instance, channel)) continue;
            const uint8_t key = event->data[1u] & 0x7fu;
            if (status == 0xb0u) {
                applyMidiCc(instance, key, event->data[2u] & 0x7fu);
                continue;
            }
            const bool noteOn = status == 0x90u && event->data[2u] != 0u;
            const bool noteOff = status == 0x80u
                || (status == 0x90u && event->data[2u] == 0u);
            if (!noteOn && !noteOff) continue;
            appendMidiCommand(instance, renderEventCount, frame, key,
                noteOn, 0u, noteOn
                    ? static_cast<float>(event->data[2u]) / 127.0f
                    : 0.0f);
        }
    }
}

void appendRequestedActions(Plugin& instance,
    std::size_t& eventCount) noexcept
{
    const uint32_t actions = instance.actionRequests.exchange(
        0u, std::memory_order_acq_rel);
    const auto appendIf = [&](uint32_t flag, DoublesEventKind kind,
                              float value = 1.0f) {
        if ((actions & flag) != 0u)
            appendEvent(instance, eventCount, 0u, kind, 0u, value);
    };
    appendIf(kActionRestart, DoublesEventKind::Restart);
    appendIf(kActionStop, DoublesEventKind::Stop);
    appendIf(kActionPlay, DoublesEventKind::Play);
    appendIf(kActionSync, DoublesEventKind::SyncDeckB);
    appendIf(kActionStepBackward, DoublesEventKind::PhaseStepBackward);
    appendIf(kActionStepForward, DoublesEventKind::PhaseStepForward);
    appendIf(kActionPunchAOn, DoublesEventKind::PunchAOn);
    appendIf(kActionPunchAOff, DoublesEventKind::PunchAOff);
    appendIf(kActionPunchBOn, DoublesEventKind::PunchBOn);
    appendIf(kActionPunchBOff, DoublesEventKind::PunchBOff);
    appendIf(kActionToggleDeckA, DoublesEventKind::ToggleDeckA);
    appendIf(kActionToggleDeckB, DoublesEventKind::ToggleDeckB);
    appendIf(kActionDragAOn, DoublesEventKind::DragAOn);
    appendIf(kActionDragAOff, DoublesEventKind::DragAOff);
    appendIf(kActionDragBOn, DoublesEventKind::DragBOn);
    appendIf(kActionDragBOff, DoublesEventKind::DragBOff);
    appendIf(kActionSetCueA, DoublesEventKind::SetCueA);
    appendIf(kActionTriggerCueA, DoublesEventKind::TriggerCueA);
    appendIf(kActionSetCueB, DoublesEventKind::SetCueB);
    appendIf(kActionTriggerCueB, DoublesEventKind::TriggerCueB);
    appendIf(kActionPlaceCueA, DoublesEventKind::PlaceCueA,
        instance.requestedCueA.load(std::memory_order_acquire));
    appendIf(kActionPlaceCueB, DoublesEventKind::PlaceCueB,
        instance.requestedCueB.load(std::memory_order_acquire));
}

bool pluginInit(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    initializeParams(instance);
    instance.retainedAssets.reserve(32u);
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
#if defined(__APPLE__)
    auto& instance = *self(plugin);
    destroyGui(instance);
    stopSampleLoader(instance);
#endif
    delete self(plugin);
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    if (!(sampleRate > 0.0) || maximumFrames == 0u
        || !instance.engine.prepare(sampleRate)) return false;
    instance.sampleRate = sampleRate;
    instance.maximumFrames = maximumFrames;
    for (auto& channel : instance.scratchChannels)
        channel.assign(maximumFrames, 0.0f);
    instance.audioAsset = instance.publishedAsset.load(
        std::memory_order_acquire);
    instance.engine.setPreparedAsset(instance.audioAsset);
    instance.audioCueRestoreRevision =
        std::numeric_limits<uint64_t>::max();
    publishCursorReset(instance, instance.audioAsset);
    instance.actionRequests.store(0u, std::memory_order_release);
    instance.actionFeedbackPulses.store(0u, std::memory_order_release);
    instance.gestureHeldMask.store(0u, std::memory_order_release);
    instance.active = true;
    return true;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.processing.store(false, std::memory_order_release);
    instance.active = false;
    instance.engine.unprepare();
    instance.audioCueRestoreRevision =
        std::numeric_limits<uint64_t>::max();
    instance.audioAsset = nullptr;
    publishCursorReset(instance, nullptr);
    instance.actionRequests.store(0u, std::memory_order_release);
    instance.actionFeedbackPulses.store(0u, std::memory_order_release);
    instance.gestureHeldMask.store(0u, std::memory_order_release);
    for (auto& channel : instance.scratchChannels) channel.clear();
    instance.retainedAssets.clear();
    if (instance.controlAsset)
        instance.retainedAssets.push_back(instance.controlAsset);
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.active) return false;
    instance.processing.store(true, std::memory_order_release);
    return true;
}

void pluginStopProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(false, std::memory_order_release);
}

void pluginReset(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.engine.reset();
    instance.audioCueRestoreRevision =
        std::numeric_limits<uint64_t>::max();
    instance.actionRequests.store(0u, std::memory_order_release);
    instance.actionFeedbackPulses.store(0u, std::memory_order_release);
    instance.gestureHeldMask.store(0u, std::memory_order_release);
    publishCursorReset(instance, instance.audioAsset);
}

bool eventDiscontinuesCursor(DoublesEventKind kind) noexcept
{
    switch (kind) {
    case DoublesEventKind::Restart:
    case DoublesEventKind::Stop:
    case DoublesEventKind::Play:
    case DoublesEventKind::SyncDeckB:
    case DoublesEventKind::PhaseStepBackward:
    case DoublesEventKind::PhaseStepForward:
    case DoublesEventKind::SelectOffset:
    case DoublesEventKind::ToggleDeckA:
    case DoublesEventKind::ToggleDeckB:
    case DoublesEventKind::TriggerCueA:
    case DoublesEventKind::TriggerCueB:
        return true;
    case DoublesEventKind::PunchAOn:
    case DoublesEventKind::PunchAOff:
    case DoublesEventKind::PunchBOn:
    case DoublesEventKind::PunchBOff:
    case DoublesEventKind::DragAOn:
    case DoublesEventKind::DragAOff:
    case DoublesEventKind::DragBOn:
    case DoublesEventKind::DragBOff:
    case DoublesEventKind::SetCueA:
    case DoublesEventKind::SetCueB:
    case DoublesEventKind::PlaceCueA:
    case DoublesEventKind::PlaceCueB:
        return false;
    }
    return false;
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
    bool cursorDiscontinuity = false;
    if (nextAsset != instance.audioAsset) {
        instance.audioAsset = nextAsset;
        instance.engine.setPreparedAsset(nextAsset);
        cursorDiscontinuity = true;
    }
    const uint64_t cueRestoreRevision = instance.cueRestoreRevision.load(
        std::memory_order_acquire);
    if (cueRestoreRevision != instance.audioCueRestoreRevision) {
        instance.engine.restoreCuePoints(
            instance.cueA.load(std::memory_order_relaxed),
            instance.cueB.load(std::memory_order_relaxed),
            instance.cueValidMask.load(std::memory_order_acquire));
        instance.audioCueRestoreRevision = cueRestoreRevision;
    }
    std::size_t eventCount = 0u;
    // GUI actions all occur at frame zero. Append them before the host's
    // time-ordered input list so the engine always receives sorted events.
    appendRequestedActions(instance, eventCount);
    readInputEvents(instance, process->in_events, process->frames_count,
        eventCount);
    bool cueChanged = false;
    for (std::size_t index = 0u; index < eventCount; ++index) {
        cursorDiscontinuity = cursorDiscontinuity
            || eventDiscontinuesCursor(instance.blockEvents[index].kind);
        cueChanged = cueChanged
            || instance.blockEvents[index].kind == DoublesEventKind::SetCueA
            || instance.blockEvents[index].kind == DoublesEventKind::SetCueB
            || instance.blockEvents[index].kind == DoublesEventKind::PlaceCueA
            || instance.blockEvents[index].kind == DoublesEventKind::PlaceCueB;
    }
    std::array<float*, 2u> scratch {{
        instance.scratchChannels[0u].data(),
        instance.scratchChannels[1u].data(),
    }};
    const DoublesSettings settings = settingsSnapshot(instance);
    if (!instance.cursorLivePhaseInitialized
        || settings.livePhaseBeats != instance.cursorLivePhaseBeats) {
        cursorDiscontinuity = cursorDiscontinuity
            || instance.cursorLivePhaseInitialized;
        instance.cursorLivePhaseInitialized = true;
        instance.cursorLivePhaseBeats = settings.livePhaseBeats;
    }
    instance.engine.render(settings,
        instance.blockEvents.data(), eventCount, scratch.data(), 2u,
        process->frames_count);
    publishCursorState(instance, instance.audioAsset, settings,
        cursorDiscontinuity);
    // Cue positions are part of project state. Publish the new marker first,
    // then ask the host to mark state dirty from its main-thread callback.
    if (cueChanged) requestCueStateDirtyOnMainThread(instance);
    instance.processBlockCount.fetch_add(1u, std::memory_order_release);

    float peak = 0.0f;
    if (process->audio_outputs_count > 0u && process->audio_outputs) {
        auto& output = process->audio_outputs[0u];
        if (output.channel_count < 2u) return CLAP_PROCESS_ERROR;
        output.constant_mask = 0u;
        for (uint32_t channel = 0u; channel < output.channel_count;
             ++channel) {
            const float* source = channel < 2u
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
    const float previous = instance.outputPeak.load(
        std::memory_order_relaxed);
    instance.outputPeak.store(std::max(peak, previous * 0.90f),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (instance.cueStateDirtyPending.exchange(false,
            std::memory_order_acq_rel))
        markStateDirty(instance);
#if defined(__APPLE__)
    serviceSampleLoads(instance);
#else
    (void)instance;
#endif
}

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
    std::snprintf(info->name, sizeof(info->name), "%s", "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2u;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
};

uint32_t audioPortsConfigCount(const clap_plugin_t*) { return 1u; }

bool audioPortsConfigGet(const clap_plugin_t*, uint32_t index,
    clap_audio_ports_config_t* config)
{
    if (!config || index != 0u) return false;
    *config = {};
    config->id = kStereoOutputConfigId;
    std::snprintf(config->name, sizeof(config->name), "%s", "Stereo");
    config->output_port_count = 1u;
    config->has_main_output = true;
    config->main_output_channel_count = 2u;
    config->main_output_port_type = CLAP_PORT_STEREO;
    return true;
}

bool audioPortsConfigSelect(const clap_plugin_t*, clap_id id)
{
    return id == kStereoOutputConfigId;
}

const clap_plugin_audio_ports_config_t audioPortsConfig {
    audioPortsConfigCount,
    audioPortsConfigGet,
    audioPortsConfigSelect,
};

clap_id audioPortsConfigCurrent(const clap_plugin_t*)
{
    return kStereoOutputConfigId;
}

bool audioPortsConfigInfoGet(const clap_plugin_t*, clap_id configId,
    uint32_t portIndex, bool isInput, clap_audio_port_info_t* info)
{
    return configId == kStereoOutputConfigId
        && audioPortsGet(nullptr, portIndex, isInput, info);
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
    if (!info || !isInput || index != 0u) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "%s", "Command MIDI In");
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount,
    notePortsGet,
};

struct NoteNameDef {
    uint8_t key;
    const char* name;
};

constexpr std::array<NoteNameDef, 29u> kNoteNames {{
    { 36u, "RESTART" }, { 37u, "STOP" }, { 38u, "SYNC B" },
    { 39u, "PHASE STEP -" }, { 40u, "PUNCH A" },
    { 41u, "PUNCH B" }, { 42u, "PHASE STEP +" },
    { 43u, "PLAY" },
    { 44u, "DECK A PLAY/PAUSE" },
    { 45u, "DECK B PLAY/PAUSE" },
    { 46u, "DRAG A" }, { 47u, "DRAG B" },
    { 48u, "OFFSET -4" }, { 49u, "OFFSET -2" },
    { 50u, "OFFSET -1" }, { 51u, "OFFSET -1/2" },
    { 52u, "OFFSET -1/4" }, { 53u, "OFFSET -1/8" },
    { 54u, "OFFSET 0" }, { 55u, "OFFSET +1/8" },
    { 56u, "OFFSET +1/4" }, { 57u, "OFFSET +1/2" },
    { 58u, "OFFSET +1" }, { 59u, "OFFSET +2" },
    { 60u, "OFFSET +4" },
    { 61u, "SET CUE A" }, { 62u, "TRIGGER CUE A" },
    { 63u, "SET CUE B" }, { 64u, "TRIGGER CUE B" },
}};

uint32_t noteNameCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kNoteNames.size());
}

bool noteNameGet(const clap_plugin_t*, uint32_t index,
    clap_note_name_t* noteName)
{
    if (!noteName || index >= kNoteNames.size()) return false;
    *noteName = {};
    noteName->port = 0;
    noteName->channel = -1;
    noteName->key = static_cast<int16_t>(kNoteNames[index].key);
    std::snprintf(noteName->name, sizeof(noteName->name), "%s",
        kNoteNames[index].name);
    return true;
}

const clap_plugin_note_name_t noteNames {
    noteNameCount,
    noteNameGet,
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kParamDefs.size());
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamDefs.size()) return false;
    const auto& def = kParamDefs[index];
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
    if (!value || !paramDef(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

const char* curveName(int curve) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Cut", "Sharp", "Blend",
    }};
    return names[static_cast<std::size_t>(std::clamp(curve, 0, 2))];
}

const char* phaseStepName(int step) noexcept
{
    constexpr std::array<const char*, 7u> names {{
        "1/16 beat", "1/8 beat", "1/4 beat", "1/2 beat",
        "1 beat", "2 beats", "4 beats",
    }};
    return names[static_cast<std::size_t>(std::clamp(step, 0, 6))];
}

const char* midiReceiveName(int channel, char* text,
    std::size_t size) noexcept
{
    if (channel <= 0) return "Omni";
    std::snprintf(text, size, "Channel %d", std::clamp(channel, 1, 16));
    return text;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kSpeedParamId) {
        const double shown = std::round(value * 10000.0) / 10000.0;
        const double percent = std::pow(2.0, shown / 12.0) * 100.0;
        std::snprintf(display, size, "%+.4f st / %.2f %%", shown,
            percent);
    } else if (id == kPhaseCentsParamId)
        std::snprintf(display, size, "%+.3f ct", value);
    else if (id == kSourceTempoParamId)
        std::snprintf(display, size, "%.2f BPM", value);
    else if (id == kOffsetParamId)
        std::snprintf(display, size, "%+.3f beats", value);
    else if (id == kPhaseStepParamId)
        std::snprintf(display, size, "%s", phaseStepName(
            static_cast<int>(std::lround(value))));
    else if (id == kStartParamId || id == kEndParamId)
        std::snprintf(display, size, "%.2f %%", value * 100.0);
    else if (id == kLoopParamId || id == kLinkDecksParamId)
        std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off");
    else if (id == kCrossfaderParamId) {
        if (std::abs(value) < 0.005)
            std::snprintf(display, size, "%s", "Center");
        else std::snprintf(display, size, "%c %.1f %%",
            value < 0.0 ? 'A' : 'B', std::abs(value) * 100.0);
    } else if (id == kCurveParamId)
        std::snprintf(display, size, "%s", curveName(
            static_cast<int>(std::lround(value))));
    else if (id == kGainParamId || id == kDeckALevelParamId
        || id == kDeckBLevelParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kLivePhaseParamId)
        std::snprintf(display, size, "%+.4f beats", value);
    else if (id == kCuePrerollParamId)
        std::snprintf(display, size, "%.0f ms", value);
    else if (id == kMidiReceiveParamId) {
        char text[32] {};
        std::snprintf(display, size, "%s", midiReceiveName(
            static_cast<int>(std::lround(value)), text, sizeof(text)));
    } else return false;
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !paramDef(id)) return false;
    if (id == kCurveParamId) {
        for (int curve = 0; curve < 3; ++curve) {
            if (strcasecmp(display, curveName(curve)) == 0) {
                *value = static_cast<double>(curve);
                return true;
            }
        }
        return false;
    }
    if (id == kLoopParamId || id == kLinkDecksParamId) {
        if (strcasecmp(display, "On") == 0) { *value = 1.0; return true; }
        if (strcasecmp(display, "Off") == 0) { *value = 0.0; return true; }
    }
    if (id == kPhaseStepParamId) {
        for (int step = 0; step < 7; ++step) {
            if (strcasecmp(display, phaseStepName(step)) == 0) {
                *value = static_cast<double>(step);
                return true;
            }
        }
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
    if (id == kCrossfaderParamId) {
        if (strcasecmp(display, "Center") == 0) {
            *value = 0.0;
            return true;
        }
        const char side = static_cast<char>(std::toupper(
            static_cast<unsigned char>(display[0])));
        if (side == 'A' || side == 'B') {
            char* end = nullptr;
            double parsed = std::strtod(display + 1, &end);
            if (end == display + 1) return false;
            if (std::strchr(display, '%')) parsed *= 0.01;
            *value = side == 'A' ? -std::abs(parsed) : std::abs(parsed);
            return true;
        }
    }
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;
    if ((id == kStartParamId || id == kEndParamId)
        && std::strchr(display, '%')) parsed *= 0.01;
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
    SavedStateV4 saved;
    // The fixed-format state includes naturally aligned doubles. Clear the
    // complete record so its padding bytes remain reproducible as well.
    std::memset(&saved, 0, sizeof(saved));
    saved.magic = kStateMagic;
    saved.version = kStateVersion;
    saved.parameterCount = static_cast<uint32_t>(kParamCount);
    for (const auto& def : kParamDefs)
        saved.parameters[paramIndex(def.id)] = paramValue(instance, def.id);
    saved.cueA = instance.cueA.load(std::memory_order_relaxed);
    saved.cueB = instance.cueB.load(std::memory_order_relaxed);
    saved.cueValidMask = instance.cueValidMask.load(
        std::memory_order_acquire);
    std::snprintf(saved.path.data(), saved.path.size(), "%s",
        instance.samplePath.c_str());
    if (instance.controlAsset) {
        saved.channelCount = instance.controlAsset->channelCount;
        saved.frameCount = instance.controlAsset->frameCount();
        saved.sampleRate = instance.controlAsset->sampleRate;
        const uint64_t bytes = static_cast<uint64_t>(saved.channelCount)
            * saved.frameCount * sizeof(float);
        saved.embedded = instance.embedSampleInState
                && bytes <= kMaximumEmbeddedAudioBytes ? 1u : 0u;
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

template <typename Saved>
bool restoreSavedState(Plugin& instance, const Saved& saved,
    const clap_istream_t* stream, float cueA = -1.0f,
    float cueB = -1.0f, uint8_t cueValidMask = 0u)
{
    std::shared_ptr<const SampleAsset> asset;
    const std::string path(saved.path.data(), strnlen(saved.path.data(),
        saved.path.size()));
    if (saved.embedded != 0u) {
        if (saved.channelCount == 0u || saved.channelCount > 2u
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
    } else if (!path.empty()) {
#if defined(__APPLE__)
        std::string error;
        if (!decodeSampleFile(path, asset, error) || !asset
            || asset->channelCount > 2u) asset.reset();
#endif
    }
    if (!publishAsset(instance, std::move(asset), path, false)) return false;
    // Loading a legacy record must reset newly appended parameters to their
    // modern defaults rather than inheriting values from the current instance.
    for (const auto& def : kParamDefs) setParam(instance, def.id,
        def.defaultValue, false);
    const std::size_t savedParameterCount = std::min<std::size_t>(
        saved.parameters.size(), saved.parameterCount);
    for (std::size_t index = 0u; index < savedParameterCount; ++index)
        setParam(instance, static_cast<clap_id>(index + kSpeedParamId),
            saved.parameters[index], false);
    instance.embedSampleInState = saved.embedded != 0u;
    queueCueRestore(instance, cueA, cueB, cueValidMask);
    instance.estimatedTempoBpm.store(0.0, std::memory_order_release);
    instance.tempoConfidence.store(0.0f, std::memory_order_release);
    instance.tempoEstimateValid.store(false, std::memory_order_release);
    instance.tempoOctaveAmbiguous.store(false,
        std::memory_order_release);
    instance.tempoOrigin.store(static_cast<uint8_t>(TempoOrigin::Restored),
        std::memory_order_release);
    instance.status = instance.controlAsset
        ? "PROJECT DOUBLES RESTORED"
        : "PROJECT RESTORED - SAMPLE OFFLINE";
    return true;
}

template <typename Saved>
bool readSavedStateRecord(const StatePrefix& prefix,
    const clap_istream_t* stream, Saved& saved)
{
    std::memset(&saved, 0, sizeof(saved));
    saved.magic = prefix.magic;
    saved.version = prefix.version;
    saved.parameterCount = prefix.parameterCount;
    auto* bytes = reinterpret_cast<uint8_t*>(&saved);
    return s3g::clap_state::readAll(stream,
        bytes + sizeof(StatePrefix), sizeof(saved) - sizeof(StatePrefix));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    StatePrefix prefix;
    if (!s3g::clap_state::readAll(stream, &prefix, sizeof(prefix))
        || prefix.magic != kStateMagic) return false;
    auto& instance = *self(plugin);
    if (prefix.version == 1u
        && prefix.parameterCount == kLegacyParamCount) {
        SavedStateV1 saved;
        return readSavedStateRecord(prefix, stream, saved)
            && restoreSavedState(instance, saved, stream);
    }
    if (prefix.version == 2u
        && prefix.parameterCount == kVersionTwoParamCount) {
        SavedStateV2 saved;
        return readSavedStateRecord(prefix, stream, saved)
            && restoreSavedState(instance, saved, stream);
    }
    if (prefix.version == 3u
        && prefix.parameterCount == kVersionTwoParamCount) {
        SavedStateV3 saved;
        return readSavedStateRecord(prefix, stream, saved)
            && restoreSavedState(instance, saved, stream,
                static_cast<float>(saved.cueA),
                static_cast<float>(saved.cueB), saved.cueValidMask);
    }
    if (prefix.version == kStateVersion
        && prefix.parameterCount == kParamCount) {
        SavedStateV4 saved;
        return readSavedStateRecord(prefix, stream, saved)
            && restoreSavedState(instance, saved, stream,
                static_cast<float>(saved.cueA),
                static_cast<float>(saved.cueB), saved.cueValidMask);
    }
    return false;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad,
};

} // namespace

// The native editor is defined below the platform-neutral CLAP/DSP core.

#if defined(__APPLE__)

namespace {

constexpr uint32_t kDeckACyan = 0x69d2dc;
constexpr uint32_t kDeckBOrange = 0xff7047;
constexpr CGFloat kMenuItemHeight = 20.0;
constexpr clap_id kFactoryPresetMenuId = CLAP_INVALID_ID - 1u;
constexpr CGFloat kContentTop = static_cast<CGFloat>(
    s3g::gui_layout::kStandardMetrics.contentTop);
constexpr auto kDoublesTitleBand = s3g::gui_layout::encoderTitleBand({
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight),
});
static_assert(s3g::gui_layout::processorTitleBandFits(kDoublesTitleBand));

void drawText(NSString* text, NSRect rect, CGFloat size,
    NSColor* color, NSFontWeight weight = NSFontWeightRegular,
    NSTextAlignment alignment = NSTextAlignmentLeft)
{
    if (!text) return;
    NSMutableParagraphStyle* paragraph = [[NSMutableParagraphStyle alloc]
        init];
    if (!paragraph) return;
    paragraph.alignment = alignment;
    paragraph.lineBreakMode = NSLineBreakByClipping;
    NSFont* font = [NSFont monospacedSystemFontOfSize:size weight:weight];
    if (!font) font = [NSFont systemFontOfSize:size weight:weight];
    NSColor* resolvedColor = color
        ? color : s3g::clap_gui::color(0xa8a8a8);
    NSMutableDictionary* attributes = [NSMutableDictionary dictionary];
    if (font) [attributes setObject:font forKey:NSFontAttributeName];
    if (resolvedColor) {
        [attributes setObject:resolvedColor
            forKey:NSForegroundColorAttributeName];
    }
    [attributes setObject:paragraph forKey:NSParagraphStyleAttributeName];
    [text drawInRect:rect withAttributes:attributes];
}

void fillRect(NSRect rect, NSColor* color)
{
    [color setFill];
    NSRectFill(rect);
}

void strokeRect(NSRect rect, NSColor* color, CGFloat width = 1.0)
{
    [color setStroke];
    NSBezierPath* path = [NSBezierPath bezierPathWithRect:rect];
    path.lineWidth = width;
    [path stroke];
}

NSRect sourcePanelRect() { return NSMakeRect(18.0, kContentTop, 1004.0, 252.0); }
NSRect waveformRect() { return NSMakeRect(30.0, 70.0, 980.0, 184.0); }
NSRect loadButtonRect() { return NSMakeRect(818.0, kContentTop + 3.0, 86.0, 15.0); }
NSRect embedButtonRect() { return NSMakeRect(912.0, kContentTop + 3.0, 98.0, 15.0); }
NSRect decksPanelRect() { return NSMakeRect(18.0, 306.0, 494.0, 210.0); }
NSRect phasePanelRect() { return NSMakeRect(528.0, 306.0, 494.0, 210.0); }
NSRect transportPanelRect() { return NSMakeRect(18.0, 528.0, 1004.0, 230.0); }
NSRect restartButtonRect() { return NSMakeRect(30.0, 558.0, 104.0, 28.0); }
NSRect playButtonRect() { return NSMakeRect(142.0, 558.0, 86.0, 28.0); }
NSRect stopButtonRect() { return NSMakeRect(236.0, 558.0, 86.0, 28.0); }
NSRect deckAPlayButtonRect() { return NSMakeRect(338.0, 558.0, 100.0, 28.0); }
NSRect deckBPlayButtonRect() { return NSMakeRect(446.0, 558.0, 100.0, 28.0); }
NSRect linkButtonRect() { return NSMakeRect(554.0, 558.0, 80.0, 28.0); }
NSRect stepBackButtonRect() { return NSMakeRect(718.0, 558.0, 86.0, 28.0); }
NSRect syncButtonRect() { return NSMakeRect(812.0, 558.0, 86.0, 28.0); }
NSRect stepForwardButtonRect() { return NSMakeRect(906.0, 558.0, 104.0, 28.0); }
NSRect punchAButtonRect() { return NSMakeRect(30.0, 602.0, 110.0, 42.0); }
NSRect dragAButtonRect() { return NSMakeRect(148.0, 602.0, 96.0, 42.0); }
NSRect cueAButtonRect() { return NSMakeRect(252.0, 602.0, 96.0, 42.0); }
NSRect triggerAButtonRect() { return NSMakeRect(356.0, 602.0, 108.0, 42.0); }
NSRect triggerBButtonRect() { return NSMakeRect(576.0, 602.0, 108.0, 42.0); }
NSRect cueBButtonRect() { return NSMakeRect(692.0, 602.0, 96.0, 42.0); }
NSRect dragBButtonRect() { return NSMakeRect(796.0, 602.0, 90.0, 42.0); }
NSRect punchBButtonRect() { return NSMakeRect(894.0, 602.0, 116.0, 42.0); }
NSRect crossfaderTrackRect() { return NSMakeRect(172.0, 716.0, 696.0, 14.0); }
NSRect crossfaderDynamicRect() { return NSMakeRect(143.0, 706.0, 754.0, 38.0); }
NSRect bpmHalfButtonRect() { return NSMakeRect(568.0, 263.0, 44.0, 17.0); }
NSRect bpmAutoButtonRect() { return NSMakeRect(616.0, 263.0, 52.0, 17.0); }
NSRect bpmDoubleButtonRect() { return NSMakeRect(672.0, 263.0, 44.0, 17.0); }
NSRect factoryPresetMenuRect()
{
    const NSRect anchor = s3g::clap_gui::cocoaRect(
        kDoublesTitleBand.presetMenu);
    return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width, kMenuItemHeight * static_cast<CGFloat>(
            s3g::sample::kDoublesFactoryPresetCount));
}

struct UiSlider {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

const std::array<UiSlider, 11u> kUiSliders {{
    { kGainParamId, "OUT", 18.0, 494.0, 342.0 },
    { kSpeedParamId, "SPEED", 18.0, 494.0, 368.0 },
    { kSourceTempoParamId, "SAMPLE BPM", 18.0, 494.0, 394.0 },
    { kStartParamId, "START", 18.0, 494.0, 420.0 },
    { kEndParamId, "END", 18.0, 494.0, 446.0 },
    { kCuePrerollParamId, "CUE PREROLL", 18.0, 494.0, 472.0 },
    { kOffsetParamId, "B OFFSET", 528.0, 494.0, 342.0 },
    { kPhaseCentsParamId, "B DRIFT", 528.0, 494.0, 368.0 },
    { kLivePhaseParamId, "B LIVE PHASE", 528.0, 494.0, 394.0 },
    { kDeckALevelParamId, "A LEVEL", 18.0, 494.0, 664.0 },
    { kDeckBLevelParamId, "B LEVEL", 528.0, 494.0, 664.0 },
}};

NSString* const kPhaseStepItems[] = {
    @"1/16 BEAT", @"1/8 BEAT", @"1/4 BEAT", @"1/2 BEAT",
    @"1 BEAT", @"2 BEATS", @"4 BEATS",
};
NSString* const kLoopItems[] = { @"OFF", @"ON" };
NSString* const kCurveItems[] = { @"CUT", @"SHARP", @"BLEND" };
NSString* const kMidiReceiveItems[] = {
    @"OMNI", @"CHANNEL 1", @"CHANNEL 2", @"CHANNEL 3", @"CHANNEL 4",
    @"CHANNEL 5", @"CHANNEL 6", @"CHANNEL 7", @"CHANNEL 8",
    @"CHANNEL 9", @"CHANNEL 10", @"CHANNEL 11", @"CHANNEL 12",
    @"CHANNEL 13", @"CHANNEL 14", @"CHANNEL 15", @"CHANNEL 16",
};

struct UiMenu {
    clap_id id;
    const char* label;
    CGFloat y;
    NSString* const* items;
    uint32_t itemCount;
    bool opensAbove;
};

const std::array<UiMenu, 4u> kUiMenus {{
    { kPhaseStepParamId, "PHASE STEP", 420.0,
        kPhaseStepItems, 7u, false },
    { kLoopParamId, "LOOP", 446.0, kLoopItems, 2u, false },
    { kCurveParamId, "XFADE CURVE", 472.0,
        kCurveItems, 3u, false },
    { kMidiReceiveParamId, "MIDI RECEIVE", 498.0,
        kMidiReceiveItems, 17u, true },
}};

NSRect sliderTrackRect(const UiSlider& slider)
{
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(slider.panelX)),
        slider.y + 1.0, static_cast<CGFloat>(
            s3g::gui_layout::processorTrackWidth(slider.panelWidth)), 9.0);
}

NSRect sliderHitRect(const UiSlider& slider)
{
    return NSMakeRect(slider.panelX + static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitInset),
        slider.y - 8.0, slider.panelWidth - 2.0 * static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.hitInset),
        static_cast<CGFloat>(s3g::gui_layout::kStandardMetrics.hitHeight));
}

NSRect sliderDynamicRect(const UiSlider& slider)
{
    return NSMakeRect(slider.panelX + 10.0, slider.y - 5.0,
        slider.panelWidth - 20.0, 20.0);
}

NSRect menuRect(const UiMenu& menu)
{
    const NSRect panel = phasePanelRect();
    return NSMakeRect(static_cast<CGFloat>(
            s3g::gui_layout::processorControlX(panel.origin.x)),
        menu.y - 1.0, static_cast<CGFloat>(
            s3g::gui_layout::processorMenuWidth(panel.size.width)), 15.0);
}

NSRect dropdownRect(const UiMenu& menu)
{
    const NSRect anchor = menuRect(menu);
    const CGFloat height = kMenuItemHeight
        * static_cast<CGFloat>(menu.itemCount);
    return NSMakeRect(anchor.origin.x,
        menu.opensAbove ? anchor.origin.y - height - 1.0
            : NSMaxY(anchor) + 1.0,
        anchor.size.width, height);
}

const UiMenu* menuForId(clap_id id)
{
    for (const auto& menu : kUiMenus)
        if (menu.id == id) return &menu;
    return nullptr;
}

const UiMenu* menuAtPoint(NSPoint point)
{
    for (const auto& menu : kUiMenus)
        if (NSPointInRect(point, menuRect(menu))) return &menu;
    return nullptr;
}

double sliderNormalized(clap_id id, double value) noexcept
{
    const auto* def = paramDef(id);
    if (!def || !(def->maximum > def->minimum)) return 0.0;
    return std::clamp((value - def->minimum)
        / (def->maximum - def->minimum), 0.0, 1.0);
}

double sliderValue(clap_id id, double normalized) noexcept
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    return clampParam(*def, def->minimum
        + normalized * (def->maximum - def->minimum));
}

const UiSlider* sliderAtPoint(NSPoint point)
{
    for (const auto& slider : kUiSliders) {
        if (NSPointInRect(point, sliderHitRect(slider))) return &slider;
    }
    return nullptr;
}

void drawButton(NSRect rect, NSString* label,
    NSDictionary* labelAttrs, const s3g::clap_gui::Style& style,
    bool active = false, NSColor* activeColor = nil)
{
    fillRect(rect, active ? s3g::clap_gui::color(0x303030)
        : style.strip);
    strokeRect(rect, active && activeColor ? activeColor : style.grid);
    const NSSize size = [label sizeWithAttributes:labelAttrs];
    [label drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:labelAttrs];
}

void drawSlider(Plugin& instance, const UiSlider& slider,
    NSDictionary* labelAttrs, NSDictionary* valueAttrs,
    const s3g::clap_gui::Style& style)
{
    fillRect(sliderDynamicRect(slider), style.cellBg);
    const double value = paramValue(instance, slider.id);
    char valueText[96] {};
    paramsValueToText(&instance.plugin, slider.id, value,
        valueText, sizeof(valueText));
    s3g::clap_gui::drawProcessorSlider(
        [NSString stringWithUTF8String:slider.label],
        [NSString stringWithUTF8String:valueText],
        static_cast<CGFloat>(sliderNormalized(slider.id, value)), slider.y,
        slider.panelX, slider.panelWidth,
        labelAttrs, valueAttrs, style);
}

void drawCrossfader(Plugin& instance,
    const s3g::clap_gui::Style& style)
{
    const NSRect dynamic = crossfaderDynamicRect();
    fillRect(dynamic, style.cellBg);
    const NSRect crossfader = crossfaderTrackRect();
    const double value = paramValue(instance, kCrossfaderParamId);
    const double normalized = sliderNormalized(kCrossfaderParamId, value);
    drawText(@"A", NSMakeRect(151.0, 711.0, 18.0, 22.0),
        12.0, s3g::clap_gui::color(kDeckACyan), NSFontWeightSemibold,
        NSTextAlignmentCenter);
    drawText(@"B", NSMakeRect(871.0, 711.0, 18.0, 22.0),
        12.0, s3g::clap_gui::color(kDeckBOrange), NSFontWeightSemibold,
        NSTextAlignmentCenter);
    fillRect(crossfader, style.strip);
    fillRect(NSMakeRect(crossfader.origin.x, crossfader.origin.y,
        crossfader.size.width * 0.5, crossfader.size.height),
        s3g::clap_gui::color(kDeckACyan, 0.16));
    fillRect(NSMakeRect(NSMidX(crossfader), crossfader.origin.y,
        crossfader.size.width * 0.5, crossfader.size.height),
        s3g::clap_gui::color(kDeckBOrange, 0.16));
    strokeRect(crossfader, style.grid);
    const CGFloat faderX = crossfader.origin.x
        + crossfader.size.width * static_cast<CGFloat>(normalized);
    fillRect(NSMakeRect(faderX - 7.0, crossfader.origin.y - 8.0,
        14.0, crossfader.size.height + 16.0), style.text);
}

double cursorClockSeconds() noexcept
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

NSString* tempoReadout(Plugin& instance)
{
    const TempoOrigin origin = static_cast<TempoOrigin>(
        instance.tempoOrigin.load(std::memory_order_acquire));
    const bool valid = instance.tempoEstimateValid.load(
        std::memory_order_acquire);
    const double estimate = instance.estimatedTempoBpm.load(
        std::memory_order_acquire);
    const int confidence = static_cast<int>(std::lround(
        instance.tempoConfidence.load(std::memory_order_acquire) * 100.0f));
    char text[96] {};
    if (origin == TempoOrigin::Estimated && valid) {
        std::snprintf(text, sizeof(text), "BPM %.2f AUTO / %d%%",
            estimate, confidence);
    } else if (origin == TempoOrigin::Suggested && valid) {
        std::snprintf(text, sizeof(text), "BPM %.2f SUGGEST / %d%%%s",
            estimate, confidence,
            instance.tempoOctaveAmbiguous.load(std::memory_order_acquire)
                ? " ?2X" : "");
    } else if (origin == TempoOrigin::Restored) {
        std::snprintf(text, sizeof(text), "BPM %.2f RESTORED",
            paramValue(instance, kSourceTempoParamId));
    } else {
        std::snprintf(text, sizeof(text), "BPM %.2f MANUAL",
            paramValue(instance, kSourceTempoParamId));
    }
    return [NSString stringWithUTF8String:text];
}

} // namespace

// The playheads use an explicit Core Animation motion contract rather than
// frame-by-frame AppKit drawing. REAPER can briefly stop committing AppKit
// frames when a parameter gesture ends even though audio and the render server
// continue. Once these animations are committed, the WindowServer advances
// them independently through that gap.
@interface S3GSampleDoublesCursorView : NSView {
    CAShapeLayer* _deckALayer;
    CAShapeLayer* _deckBLayer;
    CAShapeLayer* _cursorMaskLayer;
    double _contractStart[2];
    double _contractEnd[2];
    double _contractRate[2];
    double _contractAnchorMediaTime[2];
    double _contractAnchorPosition[2];
    BOOL _contractLoop[2];
    BOOL _contractRunning[2];
    BOOL _contractVisible[2];
    BOOL _contractInitialized[2];
    NSUInteger _animationInstallCount;
}
- (void)synchronizeDeckA:(double)deckA
                   deckB:(double)deckB
                   start:(double)start
                     end:(double)end
                   rateA:(double)rateA
                   rateB:(double)rateB
              activeMask:(uint8_t)activeMask
                 playing:(BOOL)playing
              processing:(BOOL)processing
                    loop:(BOOL)loop
                   force:(BOOL)force;
- (NSUInteger)animationInstallCount;
- (BOOL)deckAAnimationActive;
- (BOOL)deckBAnimationActive;
- (double)deckAPresentationX;
- (double)deckBPresentationX;
- (void)setOcclusionRect:(NSRect)rect;
@end

@implementation S3GSampleDoublesCursorView

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return nil;

    CALayer* root = [CALayer layer];
    root.backgroundColor = [[NSColor clearColor] CGColor];
    root.masksToBounds = YES;
    root.geometryFlipped = YES;
    [self setLayer:root];
    [self setWantsLayer:YES];

    const CGFloat half = frame.size.height * 0.5;
    const auto makeCursor = ^CAShapeLayer*(NSColor* color, CGFloat centerY) {
        CAShapeLayer* cursor = [CAShapeLayer layer];
        cursor.bounds = CGRectMake(0.0, 0.0, 2.0, half);
        cursor.position = CGPointMake(0.0, centerY);
        cursor.anchorPoint = CGPointMake(0.5, 0.5);
        cursor.fillColor = nil;
        cursor.strokeColor = [color CGColor];
        cursor.lineWidth = 2.0;
        cursor.lineCap = kCALineCapButt;
        cursor.hidden = YES;
        cursor.actions = @{
            @"position": [NSNull null],
            @"hidden": [NSNull null],
            @"bounds": [NSNull null],
            @"path": [NSNull null],
        };
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathMoveToPoint(path, nullptr, 1.0, 0.0);
        CGPathAddLineToPoint(path, nullptr, 1.0, half);
        cursor.path = path;
        CGPathRelease(path);
        [root addSublayer:cursor];
        return cursor;
    };
    _deckALayer = makeCursor(
        s3g::clap_gui::color(kDeckACyan), half * 0.5);
    _deckBLayer = makeCursor(
        s3g::clap_gui::color(kDeckBOrange), half * 1.5);
    _cursorMaskLayer = nil;
    for (std::size_t index = 0u; index < 2u; ++index) {
        _contractStart[index] = 0.0;
        _contractEnd[index] = 1.0;
        _contractRate[index] = 0.0;
        _contractAnchorMediaTime[index] = 0.0;
        _contractAnchorPosition[index] = 0.0;
        _contractLoop[index] = NO;
        _contractRunning[index] = NO;
        _contractVisible[index] = NO;
        _contractInitialized[index] = NO;
    }
    _animationInstallCount = 0u;
    [self updateLayerContentsScale];
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (NSView*)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}

- (void)updateLayerContentsScale
{
    CGFloat scale = [[self window] backingScaleFactor];
    if (!(scale > 0.0)) scale = [[NSScreen mainScreen] backingScaleFactor];
    if (!(scale > 0.0)) scale = 1.0;
    self.layer.contentsScale = scale;
    _deckALayer.contentsScale = scale;
    _deckBLayer.contentsScale = scale;
    _cursorMaskLayer.contentsScale = scale;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self updateLayerContentsScale];
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    [self updateLayerContentsScale];
}

- (void)setOcclusionRect:(NSRect)rect
{
    rect = NSIntersectionRect(self.bounds, rect);
    if (NSIsEmptyRect(rect)) {
        self.layer.mask = nil;
        _cursorMaskLayer = nil;
        return;
    }
    if (!_cursorMaskLayer) {
        _cursorMaskLayer = [CAShapeLayer layer];
        _cursorMaskLayer.fillColor = [[NSColor blackColor] CGColor];
        _cursorMaskLayer.fillRule = kCAFillRuleEvenOdd;
        _cursorMaskLayer.actions = @{
            @"path": [NSNull null],
            @"frame": [NSNull null],
        };
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _cursorMaskLayer.frame = self.bounds;
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathAddRect(path, nullptr, self.bounds);
    const CGRect layerRect = CGRectMake(rect.origin.x,
        self.bounds.size.height - NSMaxY(rect),
        rect.size.width, rect.size.height);
    CGPathAddRect(path, nullptr, layerRect);
    _cursorMaskLayer.path = path;
    CGPathRelease(path);
    self.layer.mask = _cursorMaskLayer;
    [CATransaction commit];
    [self updateLayerContentsScale];
}

- (void)synchronizeLayer:(CAShapeLayer*)layer
                   index:(std::size_t)index
                position:(double)position
                   start:(double)start
                     end:(double)end
                    rate:(double)rate
                    loop:(BOOL)loop
                 running:(BOOL)running
                   force:(BOOL)force
               mediaTime:(CFTimeInterval)mediaTime
{
    const BOOL visible = std::isfinite(position) && position >= 0.0;
    running = running && visible && std::isfinite(rate) && rate > 0.0
        && std::isfinite(start) && std::isfinite(end) && end > start;
    const BOOL animationMissing = running
        && [layer animationForKey:@"s3g.cursor.motion"] == nil;
    const BOOL changed = force || !_contractInitialized[index]
        || _contractStart[index] != start
        || _contractEnd[index] != end
        || _contractRate[index] != rate
        || _contractLoop[index] != loop
        || _contractRunning[index] != running
        || _contractVisible[index] != visible
        || animationMissing;
    if (!changed) return;

    // Preserve the presentation phase analytically for a soft contract
    // change. presentationLayer is client-side state and can itself be stale
    // across the exact AppKit gap this view is designed to survive.
    if (!force && visible && _contractInitialized[index]
        && _contractRunning[index]) {
        const double elapsed = std::max(0.0,
            static_cast<double>(mediaTime)
                - _contractAnchorMediaTime[index]);
        position = _contractAnchorPosition[index]
            + elapsed * _contractRate[index];
        if (_contractLoop[index]
            && _contractEnd[index] > _contractStart[index]) {
            const double span = _contractEnd[index]
                - _contractStart[index];
            double wrapped = std::fmod(
                position - _contractStart[index], span);
            if (wrapped < 0.0) wrapped += span;
            position = _contractStart[index] + wrapped;
        } else {
            position = std::min(position, _contractEnd[index]);
        }
    }

    _contractInitialized[index] = YES;
    _contractStart[index] = start;
    _contractEnd[index] = end;
    _contractRate[index] = rate;
    _contractLoop[index] = loop;
    _contractRunning[index] = running;
    _contractVisible[index] = visible;

    const CGFloat width = self.bounds.size.width;
    double normalized = visible ? position : 0.0;
    if (end > start) {
        if (loop) {
            const double span = end - start;
            double wrapped = std::fmod(normalized - start, span);
            if (wrapped < 0.0) wrapped += span;
            normalized = start + wrapped;
        } else {
            normalized = std::clamp(normalized, start, end);
        }
    }
    normalized = std::clamp(normalized, 0.0, 1.0);
    _contractAnchorMediaTime[index] = static_cast<double>(mediaTime);
    _contractAnchorPosition[index] = normalized;
    const CGFloat currentX = width * static_cast<CGFloat>(normalized);

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    [layer removeAnimationForKey:@"s3g.cursor.motion"];
    layer.hidden = !visible;
    CGPoint modelPosition = layer.position;
    modelPosition.x = currentX;
    layer.position = modelPosition;

    if (running) {
        CABasicAnimation* animation = [CABasicAnimation
            animationWithKeyPath:@"position.x"];
        animation.timingFunction = [CAMediaTimingFunction
            functionWithName:kCAMediaTimingFunctionLinear];
        animation.autoreverses = NO;
        const CFTimeInterval localNow = [layer convertTime:mediaTime
                                                 fromLayer:nil];
        if (loop) {
            const double span = end - start;
            const double phaseSeconds = (normalized - start) / rate;
            animation.fromValue = @(width * static_cast<CGFloat>(start));
            animation.toValue = @(width * static_cast<CGFloat>(end));
            animation.duration = std::max(1.0e-6, span / rate);
            animation.repeatCount = std::numeric_limits<float>::max();
            animation.beginTime = localNow - phaseSeconds;
            animation.fillMode = kCAFillModeBoth;
            animation.removedOnCompletion = NO;
        } else {
            const double remaining = std::max(0.0, end - normalized);
            modelPosition.x = width * static_cast<CGFloat>(end);
            layer.position = modelPosition;
            if (remaining > 1.0e-12) {
                animation.fromValue = @(currentX);
                animation.toValue = @(modelPosition.x);
                animation.duration = std::max(1.0e-6, remaining / rate);
                animation.beginTime = localNow;
                animation.fillMode = kCAFillModeBackwards;
            }
        }
        if (loop || animation.duration > 0.0) {
            [layer addAnimation:animation forKey:@"s3g.cursor.motion"];
            ++_animationInstallCount;
        }
    }
    [CATransaction commit];
}

- (void)synchronizeDeckA:(double)deckA
                   deckB:(double)deckB
                   start:(double)start
                     end:(double)end
                   rateA:(double)rateA
                   rateB:(double)rateB
              activeMask:(uint8_t)activeMask
                 playing:(BOOL)playing
              processing:(BOOL)processing
                    loop:(BOOL)loop
                   force:(BOOL)force
{
    const CFTimeInterval mediaTime = CACurrentMediaTime();
    [self synchronizeLayer:_deckALayer index:0u position:deckA
        start:start end:end rate:rateA loop:loop
        running:playing && processing && (activeMask & 1u) != 0u
        force:force mediaTime:mediaTime];
    [self synchronizeLayer:_deckBLayer index:1u position:deckB
        start:start end:end rate:rateB loop:loop
        running:playing && processing && (activeMask & 2u) != 0u
        force:force mediaTime:mediaTime];
}

- (NSUInteger)animationInstallCount { return _animationInstallCount; }
- (BOOL)deckAAnimationActive
{
    return [_deckALayer animationForKey:@"s3g.cursor.motion"] != nil;
}
- (BOOL)deckBAnimationActive
{
    return [_deckBLayer animationForKey:@"s3g.cursor.motion"] != nil;
}
- (double)presentationXForLayer:(CAShapeLayer*)layer
{
    CALayer* presentation = [layer presentationLayer];
    return presentation ? presentation.position.x : layer.position.x;
}
- (double)deckAPresentationX
{
    return [self presentationXForLayer:_deckALayer];
}
- (double)deckBPresentationX
{
    return [self presentationXForLayer:_deckBLayer];
}

@end

@interface S3GSampleDoublesView : NSView <NSDraggingDestination> {
    Plugin* _instance;
    NSTimer* _timer;
    NSTrackingArea* _trackingArea;
    S3GSampleDoublesCursorView* _cursorView;
    const SampleAsset* _waveformAsset;
    NSBezierPath* _waveformPathA;
    NSBezierPath* _waveformPathB;
    clap_id _dragParam;
    clap_id _waveDragParam;
    int _cueDragDeck;
    clap_id _openMenu;
    int _menuHover;
    BOOL _punchA;
    BOOL _punchB;
    BOOL _dragA;
    BOOL _dragB;
    uint32_t _feedbackPulseMask;
    double _feedbackPulseUntil;
    NSUInteger _refreshTickCount;
    NSUInteger _fullFrameRequestCount;
    NSUInteger _drawPassCount;
    NSUInteger _fullDrawPassCount;
    NSUInteger _cursorDrawPassCount;
    const SampleAsset* _visualCursorAsset;
    double _visualDeckAPosition;
    double _visualDeckBPosition;
    double _visualCursorLastTick;
    uint64_t _visualDiscontinuitySerial;
    uint8_t _visualActiveMask;
    BOOL _visualCursorInitialized;
    BOOL _visualWasPlaying;
    int _factoryPresetIndex;
    uint64_t _observedParamRevision;
    char _presetName[64];
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (BOOL)loadDocumentationSample;
- (void)closeMenu;
- (void)rebuildWaveformCacheIfNeeded;
- (void)startTimer;
- (void)stopTimer;
- (NSUInteger)refreshTickCount;
- (NSUInteger)fullFrameRequestCount;
- (NSUInteger)drawPassCount;
- (NSUInteger)fullDrawPassCount;
- (NSUInteger)cursorDrawPassCount;
- (NSUInteger)cursorAnimationInstallCount;
- (BOOL)deckACursorAnimationActive;
- (BOOL)deckBCursorAnimationActive;
- (double)deckACursorPresentationX;
- (double)deckBCursorPresentationX;
- (uint64_t)processBlockCount;
- (double)deckAPositionValue;
- (double)deckBPositionValue;
- (double)visualDeckAPositionValue;
- (double)visualDeckBPositionValue;
- (void)updateVisualCursors;
- (void)updateActionFeedback;
- (void)applyTempoMultiplier:(double)multiplier autoEstimate:(BOOL)automatic;
- (void)applyFactoryPreset:(int)index;
- (void)updateFactoryPresetIdentity;
- (void)queueCueAtPoint:(NSPoint)point;
@end

@implementation S3GSampleDoublesView

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0,
        kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _instance = instance;
    _trackingArea = nil;
    _waveformAsset = nullptr;
    _waveformPathA = nil;
    _waveformPathB = nil;
    _dragParam = CLAP_INVALID_ID;
    _waveDragParam = CLAP_INVALID_ID;
    _cueDragDeck = -1;
    _openMenu = CLAP_INVALID_ID;
    _menuHover = -1;
    _punchA = NO;
    _punchB = NO;
    _dragA = NO;
    _dragB = NO;
    _feedbackPulseMask = 0u;
    _feedbackPulseUntil = 0.0;
    _refreshTickCount = 0u;
    _fullFrameRequestCount = 0u;
    _drawPassCount = 0u;
    _fullDrawPassCount = 0u;
    _cursorDrawPassCount = 0u;
    _visualCursorAsset = nullptr;
    _visualDeckAPosition = -1.0;
    _visualDeckBPosition = -1.0;
    _visualCursorLastTick = 0.0;
    _visualDiscontinuitySerial = 0u;
    _visualActiveMask = 0u;
    _visualCursorInitialized = NO;
    _visualWasPlaying = NO;
    _factoryPresetIndex = s3g::sample::doublesFactoryPresetIndex(
        settingsSnapshot(*_instance));
    _observedParamRevision = _instance->parameterRevision.load(
        std::memory_order_acquire);
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::sample::doublesFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
    [self setWantsLayer:YES];
    _cursorView = [[S3GSampleDoublesCursorView alloc]
        initWithFrame:waveformRect()];
    [self addSubview:_cursorView positioned:NSWindowAbove relativeTo:nil];
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)closeMenu
{
    _openMenu = CLAP_INVALID_ID;
    _menuHover = -1;
    [_cursorView setOcclusionRect:NSZeroRect];
}

- (NSUInteger)refreshTickCount { return _refreshTickCount; }
- (NSUInteger)fullFrameRequestCount { return _fullFrameRequestCount; }
- (NSUInteger)drawPassCount { return _drawPassCount; }
- (NSUInteger)fullDrawPassCount { return _fullDrawPassCount; }
- (NSUInteger)cursorDrawPassCount { return _cursorDrawPassCount; }
- (NSUInteger)cursorAnimationInstallCount
{
    return [_cursorView animationInstallCount];
}
- (BOOL)deckACursorAnimationActive
{
    return [_cursorView deckAAnimationActive];
}
- (BOOL)deckBCursorAnimationActive
{
    return [_cursorView deckBAnimationActive];
}
- (double)deckACursorPresentationX
{
    return [_cursorView deckAPresentationX];
}
- (double)deckBCursorPresentationX
{
    return [_cursorView deckBPresentationX];
}
- (uint64_t)processBlockCount
{
    return _instance->processBlockCount.load(std::memory_order_acquire);
}
- (double)deckAPositionValue
{
    return _instance->deckAPosition.load(std::memory_order_relaxed);
}
- (double)deckBPositionValue
{
    return _instance->deckBPosition.load(std::memory_order_relaxed);
}
- (double)deckACueValue
{
    return _instance->cueA.load(std::memory_order_relaxed);
}
- (double)deckBCueValue
{
    return _instance->cueB.load(std::memory_order_relaxed);
}
- (NSUInteger)cueValidMaskValue
{
    return _instance->cueValidMask.load(std::memory_order_acquire);
}
- (BOOL)tempoEstimateValidValue
{
    return _instance->tempoEstimateValid.load(
        std::memory_order_acquire) ? YES : NO;
}
- (double)estimatedTempoBpmValue
{
    return _instance->estimatedTempoBpm.load(std::memory_order_acquire);
}
- (double)visualDeckAPositionValue { return _visualDeckAPosition; }
- (double)visualDeckBPositionValue { return _visualDeckBPosition; }

- (void)updateActionFeedback
{
    const double now = cursorClockSeconds();
    const uint32_t incoming = _instance->actionFeedbackPulses.exchange(
        0u, std::memory_order_acq_rel);
    if (incoming != 0u) {
        _feedbackPulseMask |= incoming;
        _feedbackPulseUntil = now + 0.14;
    } else if (_feedbackPulseMask != 0u && now >= _feedbackPulseUntil) {
        _feedbackPulseMask = 0u;
    }
}

- (void)applyFactoryPreset:(int)index
{
    if (index < 0
        || index >= static_cast<int>(
            s3g::sample::kDoublesFactoryPresetCount)
        || !queueGuiFactoryPreset(
            *_instance, static_cast<uint32_t>(index))) {
        NSBeep();
        return;
    }
    _factoryPresetIndex = index;
    _observedParamRevision = _instance->parameterRevision.load(
        std::memory_order_acquire);
    const char* name = s3g::sample::doublesFactoryPresetInfo(
        static_cast<uint32_t>(index)).name;
    std::snprintf(_presetName, sizeof(_presetName), "%s", name);
    _instance->status = std::string(name) + " PRESET";
    [self setNeedsDisplay:YES];
}

- (void)updateFactoryPresetIdentity
{
    const uint64_t revision = _instance->parameterRevision.load(
        std::memory_order_acquire);
    if (revision == _observedParamRevision) return;
    _observedParamRevision = revision;
    _factoryPresetIndex = s3g::sample::doublesFactoryPresetIndex(
        settingsSnapshot(*_instance));
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::sample::doublesFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
}

- (void)applyTempoMultiplier:(double)multiplier autoEstimate:(BOOL)automatic
{
    const bool hasEstimate = _instance->tempoEstimateValid.load(
        std::memory_order_acquire);
    if (automatic && !hasEstimate) {
        queueTempoAnalysis(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }
    const double base = hasEstimate
        ? _instance->estimatedTempoBpm.load(std::memory_order_acquire)
        : paramValue(*_instance, kSourceTempoParamId);
    const ParamDef* def = paramDef(kSourceTempoParamId);
    if (!def || !std::isfinite(base)) return;
    const double value = clampParam(*def, base * multiplier);
    queueGuiParamGesture(*_instance, kSourceTempoParamId, value);
    _instance->tempoOrigin.store(static_cast<uint8_t>(automatic
            ? TempoOrigin::Estimated : TempoOrigin::Manual),
        std::memory_order_release);
    _instance->status = automatic
        ? "DETECTED BPM APPLIED" : "BPM OCTAVE ADJUSTED";
    [self setNeedsDisplay:YES];
}

- (void)updateVisualCursors
{
    const double now = cursorClockSeconds();
    CursorSnapshot snapshot;
    if (!readCursorSnapshot(*_instance, snapshot)) return;
    const BOOL processing = _instance->processing.load(
        std::memory_order_acquire) ? YES : NO;
    const bool mustSnap = !_visualCursorInitialized
        || snapshot.asset != _visualCursorAsset
        || snapshot.discontinuity != _visualDiscontinuitySerial;

    if (mustSnap) {
        _visualDeckAPosition = snapshot.deckA;
        _visualDeckBPosition = snapshot.deckB;
        _visualCursorInitialized = YES;
        _visualCursorAsset = snapshot.asset;
        _visualDiscontinuitySerial = snapshot.discontinuity;
        _visualActiveMask = snapshot.activeMask;
        _visualWasPlaying = snapshot.playing ? YES : NO;
        _visualCursorLastTick = now;
        [_cursorView synchronizeDeckA:_visualDeckAPosition
            deckB:_visualDeckBPosition start:snapshot.start end:snapshot.end
            rateA:snapshot.rateA rateB:snapshot.rateB
            activeMask:_visualActiveMask playing:_visualWasPlaying
            processing:processing loop:snapshot.loop force:YES];
        return;
    }

    // Routine process publications are deliberately not cursor anchors.
    // REAPER can generate them in anticipative bursts, leaving the latest raw
    // position unchanged while that buffered audio is heard. Advance from the
    // coherent sample-domain rate for the full wall-clock interval instead;
    // crossfader, gain, menu, and repaint traffic cannot stop this clock.
    const double elapsed = std::max(0.0, now - _visualCursorLastTick);
    if (elapsed > 0.0 && _visualWasPlaying && processing) {
        const auto advance = [&](double position, double rate,
                                 uint8_t activeBit) {
            if ((_visualActiveMask & activeBit) == 0u
                || position < 0.0 || !(snapshot.end > snapshot.start))
                return position;
            position += elapsed * rate;
            if (!snapshot.loop) {
                if (position >= snapshot.end) {
                    position = snapshot.end;
                    _visualActiveMask &= static_cast<uint8_t>(~activeBit);
                }
                return position;
            }
            const double length = snapshot.end - snapshot.start;
            double wrapped = std::fmod(position - snapshot.start, length);
            if (wrapped < 0.0) wrapped += length;
            return snapshot.start + wrapped;
        };
        _visualDeckAPosition = advance(_visualDeckAPosition,
            snapshot.rateA, 1u);
        _visualDeckBPosition = advance(_visualDeckBPosition,
            snapshot.rateB, 2u);
        if (_visualActiveMask == 0u) _visualWasPlaying = NO;
    }
    _visualCursorLastTick = now;
    [_cursorView synchronizeDeckA:_visualDeckAPosition
        deckB:_visualDeckBPosition start:snapshot.start end:snapshot.end
        rateA:snapshot.rateA rateB:snapshot.rateB
        activeMask:_visualActiveMask playing:_visualWasPlaying
        processing:processing loop:snapshot.loop force:NO];
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

- (void)startTimer
{
    if (_timer) return;
    // Commit the current trajectories before the user can begin a gesture.
    // Subsequent timer ticks only replace them when the motion contract
    // changes.
    [self updateVisualCursors];
    __weak S3GSampleDoublesView* weakSelf = self;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0
        repeats:YES block:^(NSTimer*) {
            S3GSampleDoublesView* view = weakSelf;
            if (!view) return;
            serviceSampleLoads(*view->_instance);
            [view updateFactoryPresetIdentity];
            ++view->_refreshTickCount;
            [view updateVisualCursors];
            [view updateActionFeedback];
            // Non-playhead feedback follows Sample Player's 30 Hz full-view
            // refresh path. The playheads themselves are long-lived Core
            // Animation trajectories presented at the display cadence and do
            // not depend on this invalidation.
            ++view->_fullFrameRequestCount;
            [view setNeedsDisplay:YES];
        }];
    _timer.tolerance = 1.0 / 120.0;
    // REAPER runs AppKit in NSEventTrackingRunLoopMode while a slider is held
    // and while a menu is tracking. Common modes keep state sampling and meter
    // refresh alive in both ordinary and tracking loops. Cursor motion itself
    // remains compositor-driven between these samples.
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopTimer
{
    [_timer invalidate];
    _timer = nil;
    if (_punchA) requestAction(*_instance, kActionPunchAOff);
    if (_punchB) requestAction(*_instance, kActionPunchBOff);
    if (_dragA) requestAction(*_instance, kActionDragAOff);
    if (_dragB) requestAction(*_instance, kActionDragBOff);
    _punchA = NO;
    _punchB = NO;
    _dragA = NO;
    _dragB = NO;
    _cueDragDeck = -1;
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
        || !installDecodedSample(*_instance, path, std::move(asset)))
        return NO;
    setParam(*_instance, kSpeedParamId, -7.0);
    setParam(*_instance, kOffsetParamId, 1.0);
    setParam(*_instance, kPhaseCentsParamId, 0.35);
    setParam(*_instance, kLoopParamId, 1.0);
    setParam(*_instance, kCrossfaderParamId, -1.0);
    _instance->deckAPosition.store(0.31f, std::memory_order_relaxed);
    _instance->deckBPosition.store(0.39f, std::memory_order_relaxed);
    queueCueRestore(*_instance, 0.18f, 0.62f, 3u);
    _instance->status = "TRACKER CHOP / PHASE READY";
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

- (void)queueSlider:(const UiSlider&)slider point:(NSPoint)point
{
    const NSRect track = sliderTrackRect(slider);
    const double normalized = std::clamp(static_cast<double>(
        (point.x - track.origin.x) / track.size.width),
        0.0, 1.0);
    queueGuiParamValue(*_instance, slider.id,
        sliderValue(slider.id, normalized));
    [self setNeedsDisplayInRect:sliderDynamicRect(slider)];
}

- (void)queueCrossfaderAtPoint:(NSPoint)point
{
    const NSRect track = crossfaderTrackRect();
    const double normalized = std::clamp(static_cast<double>(
        (point.x - track.origin.x) / track.size.width), 0.0, 1.0);
    queueGuiParamValue(*_instance, kCrossfaderParamId,
        sliderValue(kCrossfaderParamId, normalized));
    [self setNeedsDisplayInRect:crossfaderDynamicRect()];
}

- (void)queueWaveAtPoint:(NSPoint)point
{
    if (_waveDragParam == CLAP_INVALID_ID || !_instance->controlAsset)
        return;
    const NSRect wave = waveformRect();
    double value = std::clamp(static_cast<double>(
        (point.x - wave.origin.x) / wave.size.width), 0.0, 1.0);
    const double frame = 1.0 / static_cast<double>(
        std::max<uint32_t>(1u, _instance->controlAsset->frameCount()));
    if (_waveDragParam == kStartParamId)
        value = std::min(value,
            std::max(0.0, paramValue(*_instance, kEndParamId) - frame));
    else value = std::max(value,
        std::min(1.0, paramValue(*_instance, kStartParamId) + frame));
    queueGuiParamValue(*_instance, _waveDragParam, value);
    [self setNeedsDisplayInRect:waveformRect()];
    if (const UiSlider* slider = [&]() -> const UiSlider* {
            for (const auto& candidate : kUiSliders)
                if (candidate.id == _waveDragParam) return &candidate;
            return nullptr;
        }()) {
        [self setNeedsDisplayInRect:sliderDynamicRect(*slider)];
    }
}

- (void)queueCueAtPoint:(NSPoint)point
{
    if (_cueDragDeck < 0 || _cueDragDeck > 1) return;
    const NSRect wave = waveformRect();
    const float normalized = static_cast<float>(std::clamp(
        static_cast<double>((point.x - wave.origin.x) / wave.size.width),
        0.0, 1.0));
    if (_cueDragDeck == 0) {
        _instance->requestedCueA.store(normalized,
            std::memory_order_release);
        requestAction(*_instance, kActionPlaceCueA);
    } else {
        _instance->requestedCueB.store(normalized,
            std::memory_order_release);
        requestAction(*_instance, kActionPlaceCueB);
    }
    [self setNeedsDisplayInRect:wave];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];

    if (_openMenu == kFactoryPresetMenuId) {
        const int selected = s3g::clap_gui::dropdownHitIndex(point,
            factoryPresetMenuRect(), kMenuItemHeight,
            s3g::sample::kDoublesFactoryPresetCount);
        [self closeMenu];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            [self applyFactoryPreset:selected];
            return;
        }
    } else if (_openMenu != CLAP_INVALID_ID) {
        if (const UiMenu* open = menuForId(_openMenu)) {
            const int selected = s3g::clap_gui::dropdownHitIndex(point,
                dropdownRect(*open), kMenuItemHeight, open->itemCount);
            [self closeMenu];
            [self setNeedsDisplay:YES];
            if (selected >= 0) {
                queueGuiParamGesture(*_instance, open->id,
                    static_cast<double>(selected));
                return;
            }
        } else {
            [self closeMenu];
        }
    }

    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(kDoublesTitleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _menuHover = -1;
        const NSRect wave = waveformRect();
        const NSRect covered = NSIntersectionRect(
            factoryPresetMenuRect(), wave);
        [_cursorView setOcclusionRect:NSOffsetRect(covered,
            -wave.origin.x, -wave.origin.y)];
        [self setNeedsDisplay:YES];
        return;
    }

    const bool stateFileAction = NSPointInRect(point,
            s3g::clap_gui::cocoaRect(kDoublesTitleBand.loadButton))
        || NSPointInRect(point,
            s3g::clap_gui::cocoaRect(kDoublesTitleBand.saveButton));
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &_instance->plugin, @"s3g Sample Doubles 2",
            kDoublesTitleBand, _presetName, sizeof(_presetName))) {
        if (stateFileAction) {
            _factoryPresetIndex = -1;
            _observedParamRevision = _instance->parameterRevision.load(
                std::memory_order_acquire);
        }
        markStateDirty(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }

    if (const UiMenu* menu = menuAtPoint(point)) {
        _openMenu = menu->id;
        _menuHover = -1;
        // The layer-hosted cursors composite above drawRect:. Mask only the
        // portion covered by a dropdown, preserving visible motion elsewhere
        // in the waveform while preventing a line from punching through it.
        const NSRect wave = waveformRect();
        const NSRect covered = NSIntersectionRect(
            dropdownRect(*menu), wave);
        [_cursorView setOcclusionRect:NSOffsetRect(covered,
            -wave.origin.x, -wave.origin.y)];
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, loadButtonRect())) {
        [self loadSample:nil];
        return;
    }
    if (NSPointInRect(point, embedButtonRect())) {
        _instance->embedSampleInState = !_instance->embedSampleInState;
        markStateDirty(*_instance);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, bpmHalfButtonRect())) {
        [self applyTempoMultiplier:0.5 autoEstimate:NO];
        return;
    }
    if (NSPointInRect(point, bpmAutoButtonRect())) {
        [self applyTempoMultiplier:1.0 autoEstimate:YES];
        return;
    }
    if (NSPointInRect(point, bpmDoubleButtonRect())) {
        [self applyTempoMultiplier:2.0 autoEstimate:NO];
        return;
    }
    if (NSPointInRect(point, restartButtonRect())) {
        requestAction(*_instance, kActionRestart);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, playButtonRect())) {
        requestAction(*_instance, kActionPlay);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, stopButtonRect())) {
        requestAction(*_instance, kActionStop);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, deckAPlayButtonRect())) {
        requestAction(*_instance, kActionToggleDeckA);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, deckBPlayButtonRect())) {
        requestAction(*_instance, kActionToggleDeckB);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, linkButtonRect())) {
        const double value = paramValue(*_instance, kLinkDecksParamId)
                >= 0.5 ? 0.0 : 1.0;
        queueGuiParamGesture(*_instance, kLinkDecksParamId, value);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, syncButtonRect())) {
        requestAction(*_instance, kActionSync);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, stepBackButtonRect())) {
        requestAction(*_instance, kActionStepBackward);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, stepForwardButtonRect())) {
        requestAction(*_instance, kActionStepForward);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, punchAButtonRect())) {
        _punchA = YES;
        requestAction(*_instance, kActionPunchAOn);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, punchBButtonRect())) {
        _punchB = YES;
        requestAction(*_instance, kActionPunchBOn);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, dragAButtonRect())) {
        _dragA = YES;
        requestAction(*_instance, kActionDragAOn);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, dragBButtonRect())) {
        _dragB = YES;
        requestAction(*_instance, kActionDragBOn);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, cueAButtonRect())) {
        requestAction(*_instance, kActionSetCueA);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, triggerAButtonRect())) {
        requestAction(*_instance, kActionTriggerCueA);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, cueBButtonRect())) {
        requestAction(*_instance, kActionSetCueB);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, triggerBButtonRect())) {
        requestAction(*_instance, kActionTriggerCueB);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, waveformRect())
        && _instance->controlAsset) {
        const NSRect wave = waveformRect();
        const CGFloat half = wave.size.height * 0.5;
        const int lane = point.y < wave.origin.y + half ? 0 : 1;
        const uint8_t cueMask = _instance->cueValidMask.load(
            std::memory_order_acquire);
        const float cue = lane == 0
            ? _instance->cueA.load(std::memory_order_relaxed)
            : _instance->cueB.load(std::memory_order_relaxed);
        if ((cueMask & (1u << lane)) != 0u && cue >= 0.0f) {
            const CGFloat markerX = wave.origin.x + wave.size.width
                * static_cast<CGFloat>(cue);
            if (std::abs(point.x - markerX) <= 10.0) {
                _cueDragDeck = lane;
                [self queueCueAtPoint:point];
                return;
            }
        }
    }
    if (NSPointInRect(point, waveformRect())
        && _instance->controlAsset) {
        const NSRect wave = waveformRect();
        const CGFloat startX = wave.origin.x + wave.size.width
            * static_cast<CGFloat>(paramValue(*_instance, kStartParamId));
        const CGFloat endX = wave.origin.x + wave.size.width
            * static_cast<CGFloat>(paramValue(*_instance, kEndParamId));
        const CGFloat startDistance = std::abs(point.x - startX);
        const CGFloat endDistance = std::abs(point.x - endX);
        if (std::min(startDistance, endDistance) <= 12.0) {
            _waveDragParam = startDistance <= endDistance
                ? kStartParamId : kEndParamId;
            queueGuiParamGestureBegin(*_instance, _waveDragParam);
            [self queueWaveAtPoint:point];
            return;
        }
    }
    if (const UiSlider* slider = sliderAtPoint(point)) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(event,
                &_instance->plugin, slider->id, &defaultValue)) {
            queueGuiParamGesture(*_instance, slider->id, defaultValue);
            return;
        }
        _dragParam = slider->id;
        queueGuiParamGestureBegin(*_instance, _dragParam);
        [self queueSlider:*slider point:point];
        return;
    }
    if (NSPointInRect(point, NSInsetRect(
            crossfaderTrackRect(), -8.0, -10.0))) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(event,
                &_instance->plugin, kCrossfaderParamId, &defaultValue)) {
            queueGuiParamGesture(*_instance, kCrossfaderParamId,
                defaultValue);
            return;
        }
        _dragParam = kCrossfaderParamId;
        queueGuiParamGestureBegin(*_instance, _dragParam);
        [self queueCrossfaderAtPoint:point];
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_cueDragDeck >= 0) {
        [self queueCueAtPoint:point];
        return;
    }
    if (_waveDragParam != CLAP_INVALID_ID) {
        [self queueWaveAtPoint:point];
        return;
    }
    if (_dragParam == CLAP_INVALID_ID) return;
    if (_dragParam == kCrossfaderParamId) {
        [self queueCrossfaderAtPoint:point];
        return;
    }
    for (const auto& slider : kUiSliders) {
        if (slider.id == _dragParam) {
            [self queueSlider:slider point:point];
            return;
        }
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam != CLAP_INVALID_ID)
        queueGuiParamGestureEnd(*_instance, _dragParam);
    if (_waveDragParam != CLAP_INVALID_ID)
        queueGuiParamGestureEnd(*_instance, _waveDragParam);
    _dragParam = CLAP_INVALID_ID;
    _waveDragParam = CLAP_INVALID_ID;
    _cueDragDeck = -1;
    if (_punchA) {
        _punchA = NO;
        requestAction(*_instance, kActionPunchAOff);
    }
    if (_punchB) {
        _punchB = NO;
        requestAction(*_instance, kActionPunchBOff);
    }
    if (_dragA) {
        _dragA = NO;
        requestAction(*_instance, kActionDragAOff);
    }
    if (_dragB) {
        _dragB = NO;
        requestAction(*_instance, kActionDragBOff);
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_openMenu == kFactoryPresetMenuId) {
        const int hover = s3g::clap_gui::dropdownHitIndex(point,
            factoryPresetMenuRect(), kMenuItemHeight,
            s3g::sample::kDoublesFactoryPresetCount);
        if (hover == _menuHover) return;
        _menuHover = hover;
        [self setNeedsDisplayInRect:factoryPresetMenuRect()];
        return;
    }
    const UiMenu* menu = menuForId(_openMenu);
    if (!menu) return;
    const int hover = s3g::clap_gui::dropdownHitIndex(point,
        dropdownRect(*menu), kMenuItemHeight, menu->itemCount);
    if (hover == _menuHover) return;
    _menuHover = hover;
    [self setNeedsDisplayInRect:dropdownRect(*menu)];
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_menuHover < 0) return;
    _menuHover = -1;
    if (_openMenu == kFactoryPresetMenuId)
        [self setNeedsDisplayInRect:factoryPresetMenuRect()];
    else if (const UiMenu* menu = menuForId(_openMenu))
        [self setNeedsDisplayInRect:dropdownRect(*menu)];
}

- (void)rebuildWaveformCacheIfNeeded
{
    const SampleAsset* asset = _instance->controlAsset.get();
    if (asset == _waveformAsset) return;
    _waveformAsset = asset;
    _waveformPathA = nil;
    _waveformPathB = nil;
    if (!asset || !asset->valid()) return;

    const NSRect wave = waveformRect();
    const CGFloat half = wave.size.height * 0.5;
    const uint32_t frames = asset->frameCount();
    const int columns = std::max(1, static_cast<int>(wave.size.width));
    _waveformPathA = [NSBezierPath bezierPath];
    _waveformPathB = [NSBezierPath bezierPath];
    _waveformPathA.lineWidth = 1.0;
    _waveformPathB.lineWidth = 1.0;
    for (int column = 0; column < columns; ++column) {
        const uint32_t first = static_cast<uint32_t>(
            static_cast<uint64_t>(column) * frames
            / static_cast<uint32_t>(columns));
        const uint32_t last = std::min<uint32_t>(frames,
            static_cast<uint32_t>(
                static_cast<uint64_t>(column + 1) * frames
                / static_cast<uint32_t>(columns)));
        float peak = 0.0f;
        const uint32_t span = std::max<uint32_t>(1u, last - first);
        const uint32_t step = std::max<uint32_t>(1u, span / 24u);
        for (uint32_t frame = first; frame < last; frame += step) {
            for (uint8_t channel = 0u; channel < asset->channelCount;
                 ++channel) {
                peak = std::max(peak,
                    std::abs(asset->channels[channel][frame]));
            }
        }
        peak = std::min(peak, 1.0f);
        const CGFloat x = wave.origin.x + static_cast<CGFloat>(column);
        const CGFloat magnitude = static_cast<CGFloat>(peak)
            * (half * 0.42);
        const CGFloat centerA = wave.origin.y + half * 0.5;
        const CGFloat centerB = wave.origin.y + half * 1.5;
        [_waveformPathA moveToPoint:NSMakePoint(
            x, centerA - magnitude)];
        [_waveformPathA lineToPoint:NSMakePoint(
            x, centerA + magnitude)];
        [_waveformPathB moveToPoint:NSMakePoint(
            x, centerB - magnitude)];
        [_waveformPathB lineToPoint:NSMakePoint(
            x, centerB + magnitude)];
    }
}

- (void)drawWaveform:(NSRect)wave
{
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    fillRect(wave, style.strip);
    const CGFloat half = wave.size.height * 0.5;
    [s3g::clap_gui::color(0x343434, 0.8) setStroke];
    NSBezierPath* divide = [NSBezierPath bezierPath];
    [divide moveToPoint:NSMakePoint(wave.origin.x, wave.origin.y + half)];
    [divide lineToPoint:NSMakePoint(NSMaxX(wave), wave.origin.y + half)];
    [divide stroke];

    const double start = paramValue(*_instance, kStartParamId);
    const double end = paramValue(*_instance, kEndParamId);
    fillRect(NSMakeRect(wave.origin.x, wave.origin.y,
        wave.size.width * static_cast<CGFloat>(start), wave.size.height),
        s3g::clap_gui::color(0x080808, 0.72));
    fillRect(NSMakeRect(wave.origin.x
            + wave.size.width * static_cast<CGFloat>(end), wave.origin.y,
        wave.size.width * static_cast<CGFloat>(1.0 - end),
        wave.size.height), s3g::clap_gui::color(0x080808, 0.72));

    [self rebuildWaveformCacheIfNeeded];
    if (_waveformPathA && _waveformPathB) {
        [s3g::clap_gui::color(0x7f8783) setStroke];
        [_waveformPathA stroke];
        [_waveformPathB stroke];
    }

    const CGFloat startX = wave.origin.x + wave.size.width
        * static_cast<CGFloat>(start);
    const CGFloat endX = wave.origin.x + wave.size.width
        * static_cast<CGFloat>(end);
    [style.accent setStroke];
    for (const CGFloat x : { startX, endX }) {
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(x, wave.origin.y)];
        [line lineToPoint:NSMakePoint(x, NSMaxY(wave))];
        line.lineWidth = 1.4;
        [line stroke];
    }
    drawText(@"S", NSMakeRect(startX + 4.0, wave.origin.y + 2.0,
        20.0, 16.0), 10.0, style.text);
    drawText(@"E", NSMakeRect(endX - 18.0, wave.origin.y + 2.0,
        14.0, 16.0), 10.0, style.text);

    const uint8_t cueMask = _instance->cueValidMask.load(
        std::memory_order_acquire);
    const std::array<std::pair<float, NSColor*>, 2u> cues {{
        { _instance->cueA.load(std::memory_order_relaxed),
            s3g::clap_gui::color(kDeckACyan) },
        { _instance->cueB.load(std::memory_order_relaxed),
            s3g::clap_gui::color(kDeckBOrange) },
    }};
    for (std::size_t index = 0u; index < cues.size(); ++index) {
        if ((cueMask & (1u << index)) == 0u || cues[index].first < 0.0f)
            continue;
        const CGFloat x = wave.origin.x + wave.size.width
            * static_cast<CGFloat>(cues[index].first);
        const CGFloat top = wave.origin.y
            + static_cast<CGFloat>(index) * half;
        [cues[index].second setStroke];
        NSBezierPath* marker = [NSBezierPath bezierPath];
        [marker moveToPoint:NSMakePoint(x, top)];
        [marker lineToPoint:NSMakePoint(x, top + half)];
        marker.lineWidth = 1.2;
        CGFloat dash[] { 3.0, 3.0 };
        [marker setLineDash:dash count:2 phase:0.0];
        [marker stroke];
        [cues[index].second setFill];
        NSBezierPath* flag = [NSBezierPath bezierPath];
        [flag moveToPoint:NSMakePoint(x - 5.0, top + 1.0)];
        [flag lineToPoint:NSMakePoint(x + 5.0, top + 1.0)];
        [flag lineToPoint:NSMakePoint(x, top + 9.0)];
        [flag closePath];
        [flag fill];
    }

    drawText(@"DECK A", NSMakeRect(wave.origin.x + 8.0,
        wave.origin.y + 5.0, 80.0, 16.0), 10.0,
        s3g::clap_gui::color(kDeckACyan),
        NSFontWeightSemibold);
    drawText(@"DECK B", NSMakeRect(wave.origin.x + 8.0,
        wave.origin.y + half + 5.0, 80.0, 16.0), 10.0,
        s3g::clap_gui::color(kDeckBOrange), NSFontWeightSemibold);

    // Screen playheads live in the compositor-owned cursor view. Retain this
    // ordinary draw fallback only while processing is inactive so bitmap/PDF
    // documentation captures still include held heads without duplicating a
    // moving cursor underneath the animation.
    ++_cursorDrawPassCount;
    if (![[NSGraphicsContext currentContext] isDrawingToScreen]) {
        const std::array<std::pair<double, NSColor*>, 2u> cursors {{
            { _visualCursorInitialized ? _visualDeckAPosition
                  : _instance->deckAPosition.load(std::memory_order_relaxed),
                s3g::clap_gui::color(kDeckACyan) },
            { _visualCursorInitialized ? _visualDeckBPosition
                  : _instance->deckBPosition.load(std::memory_order_relaxed),
                s3g::clap_gui::color(kDeckBOrange) },
        }};
        for (std::size_t index = 0u; index < cursors.size(); ++index) {
            if (cursors[index].first < 0.0f) continue;
            const CGFloat x = wave.origin.x + wave.size.width
                * static_cast<CGFloat>(cursors[index].first);
            [cursors[index].second setStroke];
            NSBezierPath* line = [NSBezierPath bezierPath];
            [line moveToPoint:NSMakePoint(x,
                wave.origin.y + static_cast<CGFloat>(index) * half)];
            [line lineToPoint:NSMakePoint(x,
                wave.origin.y + static_cast<CGFloat>(index + 1u) * half)];
            line.lineWidth = 2.0;
            [line stroke];
        }
    }
    strokeRect(wave, style.grid);
}

- (void)drawRect:(NSRect)dirtyRect
{
    ++_drawPassCount;
    [self updateActionFeedback];
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();

    // AppKit preserves separate dirty regions even though dirtyRect is their
    // bounding box. Recognize the small regions used by localized live-control
    // invalidations so a gesture event does not rebuild every panel and
    // measure every string in the editor. The 30 Hz timer requests complete
    // frames; any other unrecognized invalidation falls through to the same
    // complete draw below.
    const NSRect* drawingRects = nullptr;
    NSInteger drawingRectCount = 0;
    [self getRectsBeingDrawn:&drawingRects count:&drawingRectCount];
    const UiMenu* openMenu = menuForId(_openMenu);
    const auto isDynamicRegion = [&](NSRect rect) {
        constexpr CGFloat margin = 2.0;
        if (NSContainsRect(NSInsetRect(waveformRect(), -margin, -margin),
                rect)
            || NSContainsRect(NSInsetRect(crossfaderDynamicRect(),
                    -margin, -margin), rect))
            return true;
        for (const auto& slider : kUiSliders) {
            if (NSContainsRect(NSInsetRect(sliderDynamicRect(slider),
                    -margin, -margin), rect))
                return true;
        }
        return openMenu && NSContainsRect(NSInsetRect(
            dropdownRect(*openMenu), -margin, -margin), rect);
    };
    bool dynamicOnly = drawingRectCount > 0;
    for (NSInteger index = 0; dynamicOnly && index < drawingRectCount;
         ++index) {
        dynamicOnly = isDynamicRegion(drawingRects[index]);
    }
    if (dynamicOnly) {
        const auto intersectsDirtyRegion = [&](NSRect rect) {
            for (NSInteger index = 0; index < drawingRectCount; ++index) {
                if (NSIntersectsRect(drawingRects[index], rect)) return true;
            }
            return false;
        };
        if (intersectsDirtyRegion(waveformRect()))
            [self drawWaveform:waveformRect()];

        NSDictionary* labelAttrs = nil;
        NSDictionary* valueAttrs = nil;
        for (const auto& slider : kUiSliders) {
            if (!intersectsDirtyRegion(sliderDynamicRect(slider))) continue;
            if (!labelAttrs) labelAttrs = s3g::clap_gui::softLabelAttrs();
            if (!valueAttrs) valueAttrs = s3g::clap_gui::softValueAttrs();
            drawSlider(*_instance, slider, labelAttrs, valueAttrs, style);
        }
        if (intersectsDirtyRegion(crossfaderDynamicRect()))
            drawCrossfader(*_instance, style);

        // The MIDI dropdown can overlap the waveform. Repaint its intersecting
        // portion last so the cursor refresh never punches through the menu.
        if (openMenu
            && intersectsDirtyRegion(dropdownRect(*openMenu))) {
            if (!valueAttrs) valueAttrs = s3g::clap_gui::softValueAttrs();
            const int selected = std::clamp(static_cast<int>(std::lround(
                paramValue(*_instance, openMenu->id))), 0,
                static_cast<int>(openMenu->itemCount) - 1);
            s3g::clap_gui::drawDropdownMenu(dropdownRect(*openMenu),
                kMenuItemHeight, openMenu->items, openMenu->itemCount,
                selected, _menuHover, valueAttrs, style);
        }
        return;
    }

    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    ++_fullDrawPassCount;
    fillRect([self bounds], style.bg);

    NSString* title = [NSString stringWithUTF8String:
        _instance->plugin.desc->name];
    s3g::clap_gui::drawProcessorTitleBand(title,
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(
            _instance->outputPeak.load(std::memory_order_relaxed)),
        kDoublesTitleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const NSRect sourcePanel = sourcePanelRect();
    s3g::clap_gui::drawPanelFrame(sourcePanel.origin.x,
        sourcePanel.origin.y, sourcePanel.size.width,
        sourcePanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SHARED SAMPLE / TWO READ HEADS", true,
        sourcePanel.origin.x, sourcePanel.origin.y, sourcePanel.size.width,
        static_cast<CGFloat>(s3g::gui_layout::kStandardMetrics.headerHeight),
        labelAttrs, style);
    s3g::clap_gui::drawHeaderButton(loadButtonRect(), sourcePanel,
        @"LOAD SAMPLE", false, labelAttrs, style);
    s3g::clap_gui::drawHeaderButton(embedButtonRect(), sourcePanel,
        _instance->embedSampleInState ? @"EMBED ON" : @"EMBED OFF",
        _instance->embedSampleInState, labelAttrs, style);
    drawText(_instance->playing.load(std::memory_order_relaxed)
            ? @"PLAYING" : @"STOPPED",
        NSMakeRect(694.0, kContentTop + 4.0, 108.0, 15.0), 10.0,
        _instance->playing.load(std::memory_order_relaxed)
            ? s3g::clap_gui::color(0xc8c8c8) : style.dim,
        NSFontWeightRegular, NSTextAlignmentRight);
    [self drawWaveform:waveformRect()];
    NSString* status = [NSString stringWithUTF8String:
        _instance->status.c_str()];
    drawText(status ? status : @"", NSMakeRect(30.0, 264.0,
        320.0, 18.0), 10.0, style.dim);
    drawText(tempoReadout(*_instance), NSMakeRect(354.0, 264.0,
        206.0, 18.0), 9.5, style.text,
        NSFontWeightRegular, NSTextAlignmentRight);
    const TempoOrigin tempoOrigin = static_cast<TempoOrigin>(
        _instance->tempoOrigin.load(std::memory_order_acquire));
    drawButton(bpmHalfButtonRect(), @"1/2", labelAttrs, style);
    drawButton(bpmAutoButtonRect(), @"AUTO", labelAttrs, style,
        tempoOrigin == TempoOrigin::Estimated, style.accent);
    drawButton(bpmDoubleButtonRect(), @"x2", labelAttrs, style);
    if (_instance->controlAsset) {
        NSString* file = [NSString stringWithUTF8String:
            _instance->samplePath.c_str()];
        drawText([file lastPathComponent], NSMakeRect(728.0, 264.0,
            282.0, 18.0), 10.0, style.dim,
            NSFontWeightRegular, NSTextAlignmentRight);
    }

    const std::array<std::pair<NSRect, NSString*>, 3u> panels {{
        { decksPanelRect(), @"OUTPUT / DECKS / SOURCE" },
        { phasePanelRect(), @"DECK B PHASE / CUT / MIDI" },
        { transportPanelRect(), @"TRANSPORT / CROSSFADER" },
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

    for (const auto& slider : kUiSliders) {
        drawSlider(*_instance, slider, labelAttrs, valueAttrs, style);
    }

    for (const auto& menu : kUiMenus) {
        const int selected = std::clamp(static_cast<int>(std::lround(
            paramValue(*_instance, menu.id))), 0,
            static_cast<int>(menu.itemCount) - 1);
        s3g::clap_gui::drawProcessorMenu(
            [NSString stringWithUTF8String:menu.label],
            menu.items[static_cast<uint32_t>(selected)], menu.y,
            phasePanelRect().origin.x, phasePanelRect().size.width,
            labelAttrs, valueAttrs, style);
    }

    const uint32_t pulse = _feedbackPulseMask;
    const uint32_t held = _instance->gestureHeldMask.load(
        std::memory_order_acquire);
    const uint8_t cueMask = _instance->cueValidMask.load(
        std::memory_order_acquire);
    const bool playing = _instance->playing.load(std::memory_order_relaxed);
    drawButton(restartButtonRect(), @"RESTART / 36",
        labelAttrs, style, (pulse & kFeedbackRestart) != 0u, style.accent);
    drawButton(playButtonRect(), @"PLAY / 43", labelAttrs, style,
        playing || (pulse & kFeedbackPlay) != 0u, style.accent);
    drawButton(stopButtonRect(), @"STOP / 37", labelAttrs, style,
        !playing || (pulse & kFeedbackStop) != 0u, style.accent);
    drawButton(deckAPlayButtonRect(), @"A P/P / 44", labelAttrs, style,
        (_visualActiveMask & 1u) != 0u
            || (pulse & kFeedbackToggleDeckA) != 0u,
        s3g::clap_gui::color(kDeckACyan));
    drawButton(deckBPlayButtonRect(), @"B P/P / 45", labelAttrs, style,
        (_visualActiveMask & 2u) != 0u
            || (pulse & kFeedbackToggleDeckB) != 0u,
        s3g::clap_gui::color(kDeckBOrange));
    drawButton(linkButtonRect(), @"LINK", labelAttrs, style,
        paramValue(*_instance, kLinkDecksParamId) >= 0.5, style.accent);
    drawButton(stepBackButtonRect(), @"STEP − / 39", labelAttrs, style,
        (pulse & kFeedbackStepBackward) != 0u, style.accent);
    drawButton(syncButtonRect(), @"SYNC / 38", labelAttrs, style,
        (pulse & kFeedbackSync) != 0u, style.accent);
    drawButton(stepForwardButtonRect(), @"STEP + / 42", labelAttrs, style,
        (pulse & kFeedbackStepForward) != 0u, style.accent);
    drawButton(punchAButtonRect(), @"PUNCH A / 40", labelAttrs, style,
        _punchA || (held & kFeedbackPunchA) != 0u
            || (pulse & kFeedbackPunchA) != 0u,
        s3g::clap_gui::color(kDeckACyan));
    drawButton(dragAButtonRect(), @"DRAG A / 46", labelAttrs, style,
        _dragA || (held & kFeedbackDragA) != 0u
            || (pulse & kFeedbackDragA) != 0u,
        s3g::clap_gui::color(kDeckACyan));
    drawButton(cueAButtonRect(), @"CUE A / 61", labelAttrs, style,
        (cueMask & 1u) != 0u || (pulse & kFeedbackSetCueA) != 0u,
        s3g::clap_gui::color(kDeckACyan));
    drawButton(triggerAButtonRect(), @"TRIG A / 62", labelAttrs, style,
        (pulse & kFeedbackTriggerCueA) != 0u,
        s3g::clap_gui::color(kDeckACyan));
    drawButton(triggerBButtonRect(), @"TRIG B / 64", labelAttrs, style,
        (pulse & kFeedbackTriggerCueB) != 0u,
        s3g::clap_gui::color(kDeckBOrange));
    drawButton(cueBButtonRect(), @"CUE B / 63", labelAttrs, style,
        (cueMask & 2u) != 0u || (pulse & kFeedbackSetCueB) != 0u,
        s3g::clap_gui::color(kDeckBOrange));
    drawButton(dragBButtonRect(), @"DRAG B / 47", labelAttrs, style,
        _dragB || (held & kFeedbackDragB) != 0u
            || (pulse & kFeedbackDragB) != 0u,
        s3g::clap_gui::color(kDeckBOrange));
    drawButton(punchBButtonRect(), @"PUNCH B / 41", labelAttrs, style,
        _punchB || (held & kFeedbackPunchB) != 0u
            || (pulse & kFeedbackPunchB) != 0u,
        s3g::clap_gui::color(kDeckBOrange));

    drawCrossfader(*_instance, style);

    const NSRect trackerHelp = NSMakeRect(18.0, 770.0, 1004.0, 50.0);
    fillRect(trackerHelp, style.cellBg);
    strokeRect(trackerHelp, style.grid);
    drawText(@"NOTES  36 RST  37 STOP  38 SYNC  39/42 STEP  40/41 PUNCH  44/45 P/P  46/47 DRAG  61/63 CUE  62/64 TRIG",
        NSMakeRect(30.0, 780.0, 980.0, 18.0), 9.5,
        style.text, NSFontWeightRegular);
    drawText(@"48–60 OFFSET+SYNC  •  CC16 XFADE  CC17 A LVL  CC18 B LVL  CC19 LIVE PHASE  •  VOL=PUNCH/DRAG DEPTH",
        NSMakeRect(30.0, 799.0, 980.0, 17.0), 9.5,
        style.dim);

    if (_openMenu == kFactoryPresetMenuId) {
        NSString* items[s3g::sample::kDoublesFactoryPresetCount] {};
        for (uint32_t index = 0u;
             index < s3g::sample::kDoublesFactoryPresetCount; ++index) {
            items[index] = [NSString stringWithUTF8String:
                s3g::sample::doublesFactoryPresetInfo(index).name];
        }
        s3g::clap_gui::drawDropdownMenu(factoryPresetMenuRect(),
            kMenuItemHeight, items,
            s3g::sample::kDoublesFactoryPresetCount,
            _factoryPresetIndex, _menuHover, valueAttrs, style);
    } else if (_openMenu != CLAP_INVALID_ID) {
        if (const UiMenu* open = menuForId(_openMenu)) {
            const int selected = std::clamp(static_cast<int>(std::lround(
                paramValue(*_instance, open->id))), 0,
                static_cast<int>(open->itemCount) - 1);
            s3g::clap_gui::drawDropdownMenu(dropdownRect(*open),
                kMenuItemHeight, open->items, open->itemCount,
                selected, _menuHover, valueAttrs, style);
        }
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
    S3GSampleDoublesView* view = [[S3GSampleDoublesView alloc]
        initWithPlugin:&instance];
    if (!view) return false;
    instance.guiView = (__bridge_retained void*)view;
    if (!s3g::clap_gui::createResponsiveViewport(instance.guiViewport,
            view, kGuiWidth, kGuiHeight, 480u, 360u)) {
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
    [(__bridge S3GSampleDoublesView*)instance.guiView stopTimer];
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
    [(__bridge S3GSampleDoublesView*)instance.guiView startTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return false;
    [(__bridge S3GSampleDoublesView*)instance.guiView stopTimer];
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
    extern const clap_plugin_gui_t gui;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-doubles",
    "s3g Sample Doubles 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Two-copy stereo varispeed instrument for MIDI-sequenced chopping and phase relationships.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId
        || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
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
