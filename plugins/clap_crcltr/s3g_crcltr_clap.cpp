#include "s3g_crcltr.h"

#include <clap/clap.h>
#include <clap/ext/note-name.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "../common/s3g_clap_gui_param_queue.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
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
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kChannelCount = 2u;
constexpr uint32_t kStateVersion = 3u;
constexpr uint32_t kGuiWidth = 760u;
constexpr uint32_t kGuiHeight = 660u;
constexpr uint32_t kLoopWaveformBins = 256u;
constexpr uint32_t kLoopImportQueueCapacity = 32u;
constexpr uint32_t kMinimumLoopImportFramesPerBlock = 8192u;
constexpr uint32_t kMaximumLoopImportFramesPerBlock = 65536u;

constexpr uint8_t kMidiCaptureHold = 36u;
constexpr uint8_t kMidiPlayPause = 37u;
constexpr uint8_t kMidiReverseA = 38u;
constexpr uint8_t kMidiReverseB = 39u;

constexpr clap_id kLoop1RateParamId = 1u;
constexpr clap_id kLoop2RateParamId = 2u;
constexpr clap_id kCrossfadeModeParamId = 3u;
constexpr clap_id kCrossfadeParamId = 4u;
constexpr clap_id kBlendParamId = 5u;
constexpr clap_id kInputGainParamId = 6u;
constexpr clap_id kOutputGainParamId = 7u;
constexpr clap_id kRecordParamId = 8u;
constexpr clap_id kRecordTargetParamId = 9u;
constexpr clap_id kMonitorModeParamId = 10u;
constexpr clap_id kPlaybackModelParamId = 11u;
constexpr clap_id kRecordModeParamId = 12u;
constexpr clap_id kOverdubFeedbackParamId = 13u;
constexpr clap_id kLoop1ReverseParamId = 14u;
constexpr clap_id kLoop2ReverseParamId = 15u;
constexpr clap_id kLoop1StartParamId = 16u;
constexpr clap_id kLoop1EndParamId = 17u;
constexpr clap_id kLoop2StartParamId = 18u;
constexpr clap_id kLoop2EndParamId = 19u;
constexpr clap_id kLoop1JoinParamId = 20u;
constexpr clap_id kLoop2JoinParamId = 21u;
constexpr clap_id kPlayingParamId = 22u;
constexpr clap_id kCrossfadeShapeParamId = 23u;
constexpr clap_id kClearLoopAParamId = 24u;
constexpr clap_id kClearLoopBParamId = 25u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
    bool automatable = true;
};

constexpr ParamDef kParamDefs[] {
    { kLoop1RateParamId, "Loop 1 Rate", "Loops", 0.25, 2.75, 1.0, false },
    { kLoop2RateParamId, "Loop 2 Rate", "Loops", 0.25, 2.75, 1.0, false },
    { kCrossfadeModeParamId, "Crossfade Motion", "Crossfade", 0.0, 8.0, 0.0, true },
    { kCrossfadeParamId, "Position / Rate", "Crossfade", 0.0, 1.0, 0.5, false },
    { kBlendParamId, "Blend", "Mix", 0.0, 1.0, 0.5, false },
    { kInputGainParamId, "Input Gain", "Mix", 0.0, 1.0, 1.0, false },
    { kOutputGainParamId, "Output Gain", "Mix", 0.0, 1.0, 1.0, false },
    { kRecordParamId, "Record", "Record", 0.0, 1.0, 0.0, true },
    { kRecordTargetParamId, "Record Target", "Record", 0.0, 2.0, 1.0, true },
    { kMonitorModeParamId, "Record Monitor", "Record", 0.0, 2.0, 1.0, true },
    { kPlaybackModelParamId, "Loop Model", "Loops", 0.0, 1.0, 1.0, true },
    { kRecordModeParamId, "Record Mode", "Record", 0.0, 2.0, 0.0, true },
    { kOverdubFeedbackParamId, "Overdub Feedback", "Record", 0.0, 1.0, 0.8, false },
    { kLoop1ReverseParamId, "Loop 1 Reverse", "Loop 1", 0.0, 1.0, 0.0, true },
    { kLoop2ReverseParamId, "Loop 2 Reverse", "Loop 2", 0.0, 1.0, 0.0, true },
    { kLoop1StartParamId, "Loop 1 Start", "Loop 1", 0.0, 0.999, 0.0, false },
    { kLoop1EndParamId, "Loop 1 End", "Loop 1", 0.001, 1.0, 1.0, false },
    { kLoop2StartParamId, "Loop 2 Start", "Loop 2", 0.0, 0.999, 0.0, false },
    { kLoop2EndParamId, "Loop 2 End", "Loop 2", 0.001, 1.0, 1.0, false },
    { kLoop1JoinParamId, "Loop 1 Join", "Loop 1", 0.0, 1.0, 0.0, true },
    { kLoop2JoinParamId, "Loop 2 Join", "Loop 2", 0.0, 1.0, 0.0, true },
    { kPlayingParamId, "Playing", "Transport", 0.0, 1.0, 1.0, true },
    { kCrossfadeShapeParamId, "Fade Shape", "Crossfade", 0.0, 8.0, 0.0, true },
    { kClearLoopAParamId, "Clear Loop A", "Loops", 0.0, 1.0, 0.0, true,
        false },
    { kClearLoopBParamId, "Clear Loop B", "Loops", 0.0, 1.0, 0.0, true,
        false },
};
constexpr uint32_t kParamCount = static_cast<uint32_t>(
    sizeof(kParamDefs) / sizeof(kParamDefs[0]));

struct LegacySavedState {
    uint32_t version = 1u;
    float loop1Rate = 1.0f;
    float loop2Rate = 1.0f;
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    uint32_t crossfadeMode = 0u;
    uint32_t recordTarget = 1u;
    uint32_t monitorMode = 1u;
};

struct SavedStateHeaderV2 {
    uint32_t version = 2u;
    uint32_t headerBytes = 0u;
    float loop1Rate = 1.0f;
    float loop2Rate = 1.0f;
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    float overdubFeedback = 0.8f;
    float loop1Start = 0.0f;
    float loop1End = 1.0f;
    float loop2Start = 0.0f;
    float loop2End = 1.0f;
    uint32_t crossfadeMode = 0u;
    uint32_t recordTarget = 1u;
    uint32_t monitorMode = 1u;
    uint32_t playbackModel = 1u;
    uint32_t recordMode = 0u;
    uint32_t loop1Reverse = 0u;
    uint32_t loop2Reverse = 0u;
    uint32_t loop1Join = 0u;
    uint32_t loop2Join = 0u;
    uint32_t playing = 1u;
    double audioSampleRate = 0.0;
    uint32_t loopFrames[2] { 0u, 0u };
};

struct SavedStateHeader {
    uint32_t version = kStateVersion;
    uint32_t headerBytes = 0u;
    float loop1Rate = 1.0f;
    float loop2Rate = 1.0f;
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    float overdubFeedback = 0.8f;
    float loop1Start = 0.0f;
    float loop1End = 1.0f;
    float loop2Start = 0.0f;
    float loop2End = 1.0f;
    uint32_t crossfadeMode = 0u;
    uint32_t crossfadeShape = 0u;
    uint32_t recordTarget = 1u;
    uint32_t monitorMode = 1u;
    uint32_t playbackModel = 1u;
    uint32_t recordMode = 0u;
    uint32_t loop1Reverse = 0u;
    uint32_t loop2Reverse = 0u;
    uint32_t loop1Join = 0u;
    uint32_t loop2Join = 0u;
    uint32_t playing = 1u;
    double audioSampleRate = 0.0;
    uint32_t loopFrames[2] { 0u, 0u };
};

enum class LoopSourceKind : uint8_t {
    Empty = 0u,
    Embedded = 1u,
    File = 2u,
    Loading = 3u,
};

struct ImportedLoopAudio {
    double sampleRate = 0.0;
    std::vector<float> left;
    std::vector<float> right;
    std::string name;
    bool truncated = false;

    bool valid() const noexcept
    {
        return std::isfinite(sampleRate) && sampleRate > 1.0
            && left.size() >= 2u && left.size() == right.size()
            && left.size() <= std::numeric_limits<uint32_t>::max();
    }
};

struct LoopImportCommand {
    uint32_t loop = 0u;
    uint64_t generation = 0u;
    std::shared_ptr<const ImportedLoopAudio> audio;
    uint32_t copiedFrames = 0u;
    bool begun = false;
    bool committed = false;
};

#if defined(__APPLE__)
struct LoopLoadRequest {
    uint32_t loop = 0u;
    uint64_t generation = 0u;
    double destinationSampleRate = 48000.0;
    uint32_t destinationCapacity = 0u;
    std::string path;
};

struct LoopLoadResult {
    uint32_t loop = 0u;
    uint64_t generation = 0u;
    std::shared_ptr<const ImportedLoopAudio> audio;
    std::string error;
};
#endif

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::CrcltrParams params {};
    s3g::Crcltr dsp;
    std::vector<float> inputLeft;
    std::vector<float> inputRight;
    std::vector<float> outputLeft;
    std::vector<float> outputRight;
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> currentCrossfade { 0.5f };
    std::atomic<float> crossfadeDirection { 0.0f };
    std::atomic<bool> processing { false };
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
    std::array<std::atomic<float>, 2u> loopPosition {};
    std::array<std::atomic<float>, 2u> activeLoopStart {};
    std::array<std::atomic<float>, 2u> activeLoopEnd {};
    std::array<std::atomic<bool>, 2u> loopWindowPending {};
    struct WaveformPublication {
        std::array<std::array<std::atomic<float>, kLoopWaveformBins>, 2u>
            minimum {};
        std::array<std::array<std::atomic<float>, kLoopWaveformBins>, 2u>
            maximum {};
        std::atomic<uint32_t> frames { 0u };
        std::atomic<uint32_t> bins { 0u };
        std::atomic<uint32_t> validBins { 0u };
    };
    struct WaveformBuilder {
        uint32_t sourceFrames = 0u;
        uint32_t targetBins = 0u;
        uint32_t nextFrame = 0u;
        uint32_t currentBin = 0u;
        std::array<float, 2u> minimum {};
        std::array<float, 2u> maximum {};
        bool binStarted = false;
        bool rebuilding = false;
    };
    std::array<WaveformPublication, 2u> loopWaveforms {};
    std::array<WaveformBuilder, 2u> waveformBuilders {};
    std::atomic<uint32_t> waveformRebuildMask { 3u };
    std::atomic<uint32_t> loopClearRequestMask { 0u };
    std::array<std::atomic<uint8_t>, 2u> loopSourceKinds {};
    std::array<std::atomic<uint64_t>, 2u> loopImportGenerations {};
    std::atomic<uint32_t> loopLoadPendingMask { 0u };
    std::atomic<uint32_t> loopLoadErrorMask { 0u };
    s3g::clap_gui::SpscEventQueue<LoopImportCommand*,
        kLoopImportQueueCapacity> loopImportQueue {};
    s3g::clap_gui::SpscEventQueue<LoopImportCommand*,
        kLoopImportQueueCapacity> retiredLoopImportQueue {};
    std::array<LoopImportCommand*, 2u> activeLoopImports {};
    std::array<std::shared_ptr<const ImportedLoopAudio>, 2u>
        controlLoadedAudio {};
    bool prepared = false;
    double pendingAudioSampleRate = 0.0;
    std::array<uint32_t, 2u> pendingLoopFrames {};
    std::array<std::vector<float>, 4u> pendingLoopAudio;
#if defined(__APPLE__)
    std::mutex loaderMutex;
    std::condition_variable loaderCondition;
    std::deque<LoopLoadRequest> loadRequests;
    std::deque<LoopLoadResult> loadResults;
    std::thread loaderThread;
    bool loaderStopping = false;
    std::array<std::string, 2u> loopLoadStatuses {{ "EMPTY", "EMPTY" }};
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

const ParamDef* findParam(clap_id id)
{
    for (const auto& def : kParamDefs)
        if (def.id == id) return &def;
    return nullptr;
}

uint32_t steppedValue(double value, uint32_t maximum)
{
    return std::min<uint32_t>(maximum,
        static_cast<uint32_t>(std::max(0.0, std::round(value))));
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    const bool recordWasHigh = plugin.params.record;
    switch (id) {
    case kLoop1RateParamId:
        plugin.params.loop1Rate = static_cast<float>(std::clamp(value, 0.25, 2.75));
        break;
    case kLoop2RateParamId:
        plugin.params.loop2Rate = static_cast<float>(std::clamp(value, 0.25, 2.75));
        break;
    case kCrossfadeModeParamId:
        plugin.params.crossfadeMode = static_cast<s3g::CrcltrCrossfadeMode>(
            steppedValue(value, 8u));
        break;
    case kCrossfadeShapeParamId:
        plugin.params.crossfadeShape = static_cast<s3g::CrcltrCrossfadeShape>(
            steppedValue(value, 8u));
        break;
    case kCrossfadeParamId:
        plugin.params.crossfade = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kBlendParamId:
        plugin.params.blend = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kInputGainParamId:
        plugin.params.inputGain = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kOutputGainParamId:
        plugin.params.outputGain = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kRecordParamId:
        plugin.params.record = value >= 0.5;
        if (plugin.params.record && !recordWasHigh) {
            const uint32_t target = static_cast<uint32_t>(
                plugin.params.recordTarget);
            for (uint32_t loop = 0u; loop < 2u; ++loop) {
                if (target != 1u && target != loop * 2u) continue;
                plugin.loopImportGenerations[loop].fetch_add(
                    1u, std::memory_order_acq_rel);
                plugin.loopSourceKinds[loop].store(
                    static_cast<uint8_t>(LoopSourceKind::Embedded),
                    std::memory_order_release);
                plugin.loopLoadPendingMask.fetch_and(~(1u << loop),
                    std::memory_order_acq_rel);
                plugin.loopLoadErrorMask.fetch_and(~(1u << loop),
                    std::memory_order_acq_rel);
            }
        }
        break;
    case kRecordTargetParamId:
        plugin.params.recordTarget = static_cast<s3g::CrcltrRecordTarget>(
            steppedValue(value, 2u));
        break;
    case kMonitorModeParamId:
        plugin.params.monitorMode = static_cast<s3g::CrcltrMonitorMode>(
            steppedValue(value, 2u));
        break;
    case kPlaybackModelParamId:
        plugin.params.playbackModel = static_cast<s3g::CrcltrPlaybackModel>(
            steppedValue(value, 1u));
        break;
    case kRecordModeParamId:
        plugin.params.recordMode = static_cast<s3g::CrcltrRecordMode>(
            steppedValue(value, 2u));
        break;
    case kOverdubFeedbackParamId:
        plugin.params.overdubFeedback = static_cast<float>(
            std::clamp(value, 0.0, 1.0));
        break;
    case kLoop1ReverseParamId:
        plugin.params.loop1Reverse = value >= 0.5;
        break;
    case kLoop2ReverseParamId:
        plugin.params.loop2Reverse = value >= 0.5;
        break;
    case kLoop1StartParamId:
        plugin.params.loop1Start = static_cast<float>(
            std::clamp(value, 0.0,
                static_cast<double>(plugin.params.loop1End - 0.001f)));
        break;
    case kLoop1EndParamId:
        plugin.params.loop1End = static_cast<float>(
            std::clamp(value,
                static_cast<double>(plugin.params.loop1Start + 0.001f), 1.0));
        break;
    case kLoop2StartParamId:
        plugin.params.loop2Start = static_cast<float>(
            std::clamp(value, 0.0,
                static_cast<double>(plugin.params.loop2End - 0.001f)));
        break;
    case kLoop2EndParamId:
        plugin.params.loop2End = static_cast<float>(
            std::clamp(value,
                static_cast<double>(plugin.params.loop2Start + 0.001f), 1.0));
        break;
    case kLoop1JoinParamId:
        plugin.params.loop1Join = static_cast<s3g::CrcltrLoopJoin>(
            steppedValue(value, 1u));
        break;
    case kLoop2JoinParamId:
        plugin.params.loop2Join = static_cast<s3g::CrcltrLoopJoin>(
            steppedValue(value, 1u));
        break;
    case kPlayingParamId:
        plugin.params.playing = value >= 0.5;
        break;
    case kClearLoopAParamId:
    case kClearLoopBParamId:
        if (value >= 0.5) {
            const uint32_t loop = id == kClearLoopAParamId ? 0u : 1u;
            plugin.loopImportGenerations[loop].fetch_add(
                1u, std::memory_order_acq_rel);
            plugin.loopSourceKinds[loop].store(
                static_cast<uint8_t>(LoopSourceKind::Empty),
                std::memory_order_release);
            plugin.loopLoadPendingMask.fetch_and(~(1u << loop),
                std::memory_order_acq_rel);
            plugin.loopLoadErrorMask.fetch_and(~(1u << loop),
                std::memory_order_acq_rel);
            plugin.loopClearRequestMask.fetch_or(
                1u << loop,
                std::memory_order_release);
        }
        return;
    default:
        return;
    }
    plugin.dsp.setParams(plugin.params);
    if (id == kRecordParamId && recordWasHigh && !plugin.params.record)
        plugin.waveformRebuildMask.fetch_or(3u, std::memory_order_release);
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.processing.load(std::memory_order_acquire)) return;
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiParamEvent(Plugin& plugin,
                        s3g::clap_gui::ParamEventKind kind,
                        clap_id id, double value = 0.0)
{
    if (!plugin.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(plugin);
    return true;
}

void queueGuiParamGestureBegin(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    const ParamDef* def = findParam(id);
    if (!def) return;
    value = std::clamp(value, def->minimum, def->maximum);
    if (!plugin.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) return;
    applyParam(plugin, id, value);
    requestGuiParamService(plugin);
}

void queueGuiParamGestureEnd(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
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

void serviceGuiParamEvents(Plugin& plugin,
                           const clap_output_events_t* output) noexcept
{
    if (plugin.guiParamConsumer.test_and_set(std::memory_order_acquire))
        return;
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            applyParam(plugin, pending.paramId, pending.value);
        plugin.guiParamEvents.pop();
    }
    plugin.guiParamConsumer.clear(std::memory_order_release);
}

void beginWaveformRebuild(Plugin& plugin, uint32_t loop)
{
    if (loop >= plugin.loopWaveforms.size()) return;
    auto& publication = plugin.loopWaveforms[loop];
    auto& builder = plugin.waveformBuilders[loop];
    builder = {};
    builder.sourceFrames = plugin.dsp.recordedFrames(loop);
    builder.targetBins = std::min<uint32_t>(kLoopWaveformBins,
        builder.sourceFrames);
    builder.rebuilding = builder.targetBins > 0u;
    publication.validBins.store(0u, std::memory_order_release);
    publication.frames.store(builder.sourceFrames, std::memory_order_relaxed);
    publication.bins.store(builder.targetBins, std::memory_order_release);
    for (uint32_t channel = 0u; channel < 2u; ++channel) {
        for (uint32_t bin = 0u; bin < kLoopWaveformBins; ++bin) {
            publication.minimum[channel][bin].store(0.0f,
                std::memory_order_relaxed);
            publication.maximum[channel][bin].store(0.0f,
                std::memory_order_relaxed);
        }
    }
}

void continueWaveformRebuild(Plugin& plugin, uint32_t loop,
                             uint32_t sampleBudget)
{
    if (loop >= plugin.loopWaveforms.size()) return;
    auto& publication = plugin.loopWaveforms[loop];
    auto& builder = plugin.waveformBuilders[loop];
    while (builder.rebuilding && sampleBudget > 0u
           && builder.currentBin < builder.targetBins) {
        const uint32_t binEnd = static_cast<uint32_t>(
            (static_cast<uint64_t>(builder.currentBin + 1u)
                * builder.sourceFrames) / builder.targetBins);
        while (builder.nextFrame < binEnd && sampleBudget > 0u) {
            const float left = plugin.dsp.loopSample(
                loop, 0u, builder.nextFrame);
            const float right = plugin.dsp.loopSample(
                loop, 1u, builder.nextFrame);
            if (!builder.binStarted) {
                builder.minimum = { left, right };
                builder.maximum = { left, right };
                builder.binStarted = true;
            } else {
                builder.minimum[0] = std::min(builder.minimum[0], left);
                builder.minimum[1] = std::min(builder.minimum[1], right);
                builder.maximum[0] = std::max(builder.maximum[0], left);
                builder.maximum[1] = std::max(builder.maximum[1], right);
            }
            ++builder.nextFrame;
            --sampleBudget;
        }
        if (builder.nextFrame < binEnd) break;
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            publication.minimum[channel][builder.currentBin].store(
                builder.minimum[channel], std::memory_order_relaxed);
            publication.maximum[channel][builder.currentBin].store(
                builder.maximum[channel], std::memory_order_relaxed);
        }
        ++builder.currentBin;
        builder.binStarted = false;
        publication.validBins.store(builder.currentBin,
            std::memory_order_release);
        if (builder.currentBin >= builder.targetBins)
            builder.rebuilding = false;
    }
}

void publishImportedWaveform(Plugin& plugin, uint32_t loop,
                             const ImportedLoopAudio& audio)
{
    if (loop >= plugin.loopWaveforms.size() || !audio.valid()) return;
    auto& publication = plugin.loopWaveforms[loop];
    const uint32_t frames = static_cast<uint32_t>(audio.left.size());
    const uint32_t bins = std::min<uint32_t>(kLoopWaveformBins, frames);
    publication.validBins.store(0u, std::memory_order_release);
    publication.frames.store(frames, std::memory_order_relaxed);
    publication.bins.store(bins, std::memory_order_release);
    for (uint32_t bin = 0u; bin < bins; ++bin) {
        const uint32_t begin = static_cast<uint32_t>(
            (static_cast<uint64_t>(bin) * frames) / bins);
        const uint32_t end = std::max<uint32_t>(begin + 1u,
            static_cast<uint32_t>((static_cast<uint64_t>(bin + 1u)
                * frames) / bins));
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            const auto& samples = channel == 0u ? audio.left : audio.right;
            float minimum = samples[begin];
            float maximum = samples[begin];
            for (uint32_t frame = begin + 1u; frame < end; ++frame) {
                minimum = std::min(minimum, samples[frame]);
                maximum = std::max(maximum, samples[frame]);
            }
            publication.minimum[channel][bin].store(minimum,
                std::memory_order_relaxed);
            publication.maximum[channel][bin].store(maximum,
                std::memory_order_relaxed);
        }
        publication.validBins.store(bin + 1u, std::memory_order_release);
    }
}

void serviceWaveformPublication(Plugin& plugin)
{
    const uint32_t rebuildMask = plugin.waveformRebuildMask.exchange(
        0u, std::memory_order_acq_rel);
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        if ((rebuildMask & (1u << loop)) != 0u)
            beginWaveformRebuild(plugin, loop);
        continueWaveformRebuild(plugin, loop, 16384u);
        plugin.loopPosition[loop].store(plugin.dsp.playbackPosition(loop),
            std::memory_order_relaxed);
        plugin.activeLoopStart[loop].store(
            plugin.dsp.activeLoopStart(loop), std::memory_order_relaxed);
        plugin.activeLoopEnd[loop].store(
            plugin.dsp.activeLoopEnd(loop), std::memory_order_relaxed);
        plugin.loopWindowPending[loop].store(
            plugin.dsp.loopWindowPending(loop), std::memory_order_relaxed);
    }
    plugin.currentCrossfade.store(plugin.dsp.currentCrossfade(),
        std::memory_order_relaxed);
    plugin.crossfadeDirection.store(plugin.dsp.crossfadeDirection(),
        std::memory_order_relaxed);
}

void serviceLoopClearRequests(Plugin& plugin)
{
    const uint32_t clearMask = plugin.loopClearRequestMask.exchange(
        0u, std::memory_order_acq_rel) & 3u;
    if (clearMask == 0u) return;
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        if ((clearMask & (1u << loop)) == 0u) continue;
        plugin.dsp.clearLoop(loop);
        plugin.loopPosition[loop].store(0.0f, std::memory_order_relaxed);
        if (!plugin.prepared) plugin.pendingLoopFrames[loop] = 0u;
    }
    if (!plugin.prepared && plugin.pendingLoopFrames[0] == 0u
        && plugin.pendingLoopFrames[1] == 0u)
        plugin.pendingAudioSampleRate = 0.0;
    plugin.waveformRebuildMask.fetch_or(clearMask,
        std::memory_order_release);
}

void markStateDirty(Plugin& plugin)
{
    if (plugin.hostState && plugin.hostState->mark_dirty)
        plugin.hostState->mark_dirty(plugin.host);
}

bool retireLoopImport(Plugin& plugin, LoopImportCommand* command) noexcept
{
    if (!command || !plugin.retiredLoopImportQueue.push(command))
        return false;
    if (plugin.host && plugin.host->request_callback)
        plugin.host->request_callback(plugin.host);
    return true;
}

void serviceLoopImports(Plugin& plugin) noexcept
{
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        auto*& active = plugin.activeLoopImports[loop];
        if (active && active->generation
                != plugin.loopImportGenerations[loop].load(
                    std::memory_order_acquire)) {
            if (retireLoopImport(plugin, active)) active = nullptr;
        }
    }

    LoopImportCommand* queued = nullptr;
    while (plugin.loopImportQueue.peek(queued)) {
        if (!queued || queued->loop >= 2u) {
            if (queued && !retireLoopImport(plugin, queued)) break;
            plugin.loopImportQueue.pop();
            continue;
        }
        const uint32_t loop = queued->loop;
        if (plugin.activeLoopImports[loop]) break;
        if (queued->generation != plugin.loopImportGenerations[loop].load(
                std::memory_order_acquire)) {
            if (!retireLoopImport(plugin, queued)) break;
            plugin.loopImportQueue.pop();
            continue;
        }
        plugin.activeLoopImports[loop] = queued;
        plugin.loopImportQueue.pop();
        queued->begun = plugin.dsp.beginLoopImport(loop);
        if (!queued->begun) {
            if (retireLoopImport(plugin, queued))
                plugin.activeLoopImports[loop] = nullptr;
            continue;
        }
        plugin.loopSourceKinds[loop].store(
            static_cast<uint8_t>(LoopSourceKind::Loading),
            std::memory_order_release);
    }

    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        auto*& active = plugin.activeLoopImports[loop];
        if (!active || !active->begun || !active->audio
            || !active->audio->valid()) continue;
        const uint32_t total = static_cast<uint32_t>(
            active->audio->left.size());
        const uint32_t remaining = total - active->copiedFrames;
        const uint32_t importBudget = std::clamp<uint32_t>(
            plugin.maxFrames > kMaximumLoopImportFramesPerBlock / 64u
                ? kMaximumLoopImportFramesPerBlock : plugin.maxFrames * 64u,
            kMinimumLoopImportFramesPerBlock,
            kMaximumLoopImportFramesPerBlock);
        const uint32_t chunk = std::min<uint32_t>(
            remaining, importBudget);
        if (chunk > 0u && !plugin.dsp.writeLoopImport(loop,
                active->copiedFrames,
                active->audio->left.data() + active->copiedFrames,
                active->audio->right.data() + active->copiedFrames,
                chunk)) {
            active->generation = 0u;
            continue;
        }
        active->copiedFrames += chunk;
        if (active->copiedFrames < total) continue;
        active->committed = plugin.dsp.finishLoopImport(loop, total);
        if (active->committed) {
            plugin.loopSourceKinds[loop].store(
                static_cast<uint8_t>(LoopSourceKind::File),
                std::memory_order_release);
            plugin.waveformRebuildMask.fetch_or(1u << loop,
                std::memory_order_release);
        }
        if (retireLoopImport(plugin, active)) active = nullptr;
    }
}

void serviceRetiredLoopImports(Plugin& plugin)
{
    LoopImportCommand* command = nullptr;
    while (plugin.retiredLoopImportQueue.peek(command)) {
#if defined(__APPLE__)
        if (command && command->loop < 2u
            && command->generation == plugin.loopImportGenerations[
                command->loop].load(std::memory_order_acquire)) {
            if (command->committed && command->audio) {
                plugin.loopLoadStatuses[command->loop]
                    = command->audio->name
                    + (command->audio->truncated ? " / 32S MAX" : " / READY");
                plugin.loopLoadPendingMask.fetch_and(
                    ~(1u << command->loop), std::memory_order_acq_rel);
                plugin.loopLoadErrorMask.fetch_and(
                    ~(1u << command->loop), std::memory_order_acq_rel);
            } else {
                plugin.loopLoadStatuses[command->loop] = "LOAD FAILED";
                plugin.loopLoadPendingMask.fetch_and(
                    ~(1u << command->loop), std::memory_order_acq_rel);
                plugin.loopLoadErrorMask.fetch_or(1u << command->loop,
                    std::memory_order_acq_rel);
            }
        }
#endif
        delete command;
        plugin.retiredLoopImportQueue.pop();
    }
}

void discardLoopImports(Plugin& plugin)
{
    LoopImportCommand* command = nullptr;
    while (plugin.loopImportQueue.peek(command)) {
        delete command;
        plugin.loopImportQueue.pop();
    }
    serviceRetiredLoopImports(plugin);
    for (auto*& active : plugin.activeLoopImports) {
        delete active;
        active = nullptr;
    }
}

#if defined(__APPLE__)
bool startLoopLoader(Plugin& plugin);
void stopLoopLoader(Plugin& plugin);
void serviceLoopLoadResults(Plugin& plugin);
#endif

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->host && p->host->get_extension) {
        p->hostParams = static_cast<const clap_host_params_t*>(
            p->host->get_extension(p->host, CLAP_EXT_PARAMS));
        p->hostState = static_cast<const clap_host_state_t*>(
            p->host->get_extension(p->host, CLAP_EXT_STATE));
    }
#if defined(__APPLE__)
    return startLoopLoader(*p);
#else
    return true;
#endif
}

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
    stopLoopLoader(*self(plugin));
#endif
    discardLoopImports(*self(plugin));
    delete self(plugin);
}

bool snapshotLoops(Plugin& plugin)
{
    if (!plugin.prepared) return true;
    try {
        plugin.pendingAudioSampleRate = plugin.dsp.sampleRate();
        for (uint32_t loop = 0u; loop < 2u; ++loop) {
            const auto sourceKind = static_cast<LoopSourceKind>(
                plugin.loopSourceKinds[loop].load(
                    std::memory_order_acquire));
            const auto& loadingAudio = plugin.controlLoadedAudio[loop];
            if (sourceKind == LoopSourceKind::Loading && loadingAudio
                && loadingAudio->valid()
                && std::abs(loadingAudio->sampleRate
                    - plugin.pendingAudioSampleRate) < 0.5) {
                const uint32_t frames = static_cast<uint32_t>(
                    loadingAudio->left.size());
                plugin.pendingLoopFrames[loop] = frames;
                plugin.pendingLoopAudio[loop * 2u] = loadingAudio->left;
                plugin.pendingLoopAudio[loop * 2u + 1u]
                    = loadingAudio->right;
                continue;
            }
            const uint32_t frames = plugin.dsp.recordedFrames(loop);
            plugin.pendingLoopFrames[loop] = frames;
            auto& left = plugin.pendingLoopAudio[loop * 2u];
            auto& right = plugin.pendingLoopAudio[loop * 2u + 1u];
            left.resize(frames);
            right.resize(frames);
            if (frames > 0u && !plugin.dsp.copyLoop(loop, left.data(),
                    right.data(), frames)) return false;
        }
        if (plugin.pendingLoopFrames[0] == 0u
            && plugin.pendingLoopFrames[1] == 0u)
            plugin.pendingAudioSampleRate = 0.0;
    } catch (...) {
        return false;
    }
    return true;
}

bool restorePendingLoops(Plugin& plugin)
{
    if (!plugin.prepared || plugin.pendingAudioSampleRate <= 1.0) return true;
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        const uint32_t frames = plugin.pendingLoopFrames[loop];
        if (frames == 0u) continue;
        const auto& left = plugin.pendingLoopAudio[loop * 2u];
        const auto& right = plugin.pendingLoopAudio[loop * 2u + 1u];
        if (left.size() != frames || right.size() != frames
            || !plugin.dsp.restoreLoop(loop, left.data(), right.data(),
                frames, plugin.pendingAudioSampleRate)) return false;
    }
    return true;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t,
              uint32_t maxFrames)
{
    auto* p = self(plugin);
    if (p->prepared && !snapshotLoops(*p)) return false;
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    try {
        p->inputLeft.resize(p->maxFrames);
        p->inputRight.resize(p->maxFrames);
        p->outputLeft.resize(p->maxFrames);
        p->outputRight.resize(p->maxFrames);
    } catch (...) {
        return false;
    }
    p->params.record = false;
    if (!p->dsp.prepare(sampleRate, p->maxFrames)) return false;
    p->prepared = true;
    p->dsp.setParams(p->params);
    if (!restorePendingLoops(*p)) return false;
    beginWaveformRebuild(*p, 0u);
    beginWaveformRebuild(*p, 1u);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(false, std::memory_order_release);
}

bool startProcessing(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->dsp.applyPendingLoopWindows();
    p->processing.store(true, std::memory_order_release);
    return true;
}

void stopProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing.store(false, std::memory_order_release);
}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->params.record = false;
    p->dsp.setParams(p->params);
    p->dsp.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, value->param_id, value->value);
    }
}

void applyMidiButton(Plugin& plugin, uint8_t key, bool noteOn)
{
    if (key == kMidiCaptureHold) {
        applyParam(plugin, kRecordParamId, noteOn ? 1.0 : 0.0);
        return;
    }
    if (!noteOn) return;
    if (key == kMidiPlayPause) {
        applyParam(plugin, kPlayingParamId,
            plugin.params.playing ? 0.0 : 1.0);
    } else if (key == kMidiReverseA) {
        applyParam(plugin, kLoop1ReverseParamId,
            plugin.params.loop1Reverse ? 0.0 : 1.0);
    } else if (key == kMidiReverseB) {
        applyParam(plugin, kLoop2ReverseParamId,
            plugin.params.loop2Reverse ? 0.0 : 1.0);
    }
}

void applyProcessEvent(Plugin& plugin, const clap_event_header_t* header)
{
    if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (header->type == CLAP_EVENT_PARAM_VALUE
        && header->size >= sizeof(clap_event_param_value_t)) {
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        applyParam(plugin, event->param_id, event->value);
        return;
    }
    if ((header->type == CLAP_EVENT_NOTE_ON
            || header->type == CLAP_EVENT_NOTE_OFF
            || header->type == CLAP_EVENT_NOTE_CHOKE)
        && header->size >= sizeof(clap_event_note_t)) {
        const auto* event = reinterpret_cast<const clap_event_note_t*>(header);
        if (event->port_index != 0 || event->key < 0 || event->key > 127)
            return;
        const bool noteOn = header->type == CLAP_EVENT_NOTE_ON
            && event->velocity > 0.0;
        applyMidiButton(plugin, static_cast<uint8_t>(event->key), noteOn);
        return;
    }
    if (header->type == CLAP_EVENT_MIDI
        && header->size >= sizeof(clap_event_midi_t)) {
        const auto* event = reinterpret_cast<const clap_event_midi_t*>(header);
        if (event->port_index != 0u) return;
        const uint8_t status = event->data[0u] & 0xf0u;
        const bool noteOn = status == 0x90u && event->data[2u] != 0u;
        const bool noteOff = status == 0x80u
            || (status == 0x90u && event->data[2u] == 0u);
        if (noteOn || noteOff)
            applyMidiButton(plugin, event->data[1u] & 0x7fu, noteOn);
    }
}

void renderRange(Plugin& plugin, uint32_t first, uint32_t frames)
{
    serviceLoopClearRequests(plugin);
    if (frames == 0u) return;
    plugin.dsp.setParams(plugin.params);
    plugin.dsp.process(plugin.inputLeft.data() + first,
        plugin.inputRight.data() + first, plugin.outputLeft.data() + first,
        plugin.outputRight.data() + first, frames);
}

clap_process_status process(const clap_plugin_t* plugin,
                            const clap_process_t* processInfo)
{
    auto* p = self(plugin);
    if (processInfo->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    serviceGuiParamEvents(*p, processInfo->out_events);
    serviceLoopClearRequests(*p);
    serviceLoopImports(*p);

    const auto* input = processInfo->audio_inputs_count > 0u
        ? &processInfo->audio_inputs[0] : nullptr;
    const auto& output = processInfo->audio_outputs[0];
    const uint32_t frames = std::min(processInfo->frames_count, p->maxFrames);
    if (output.channel_count < kChannelCount) return CLAP_PROCESS_CONTINUE;

    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (input && input->channel_count > 0u && input->data32
            && input->data32[0]) {
            p->inputLeft[frame] = input->data32[0][frame];
        } else if (input && input->channel_count > 0u && input->data64
                   && input->data64[0]) {
            p->inputLeft[frame] = static_cast<float>(input->data64[0][frame]);
        } else {
            p->inputLeft[frame] = 0.0f;
        }

        if (input && input->channel_count > 1u && input->data32
            && input->data32[1]) {
            p->inputRight[frame] = input->data32[1][frame];
        } else if (input && input->channel_count > 1u && input->data64
                   && input->data64[1]) {
            p->inputRight[frame] = static_cast<float>(input->data64[1][frame]);
        } else {
            p->inputRight[frame] = p->inputLeft[frame];
        }
    }

    uint32_t renderedFrames = 0u;
    const auto* events = processInfo->in_events;
    if (events && events->size && events->get) {
        const uint32_t eventCount = events->size(events);
        for (uint32_t index = 0u; index < eventCount; ++index) {
            const clap_event_header_t* header = events->get(events, index);
            if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;
            const uint32_t eventFrame = frames == 0u ? 0u
                : std::min<uint32_t>(frames - 1u,
                    std::max<uint32_t>(renderedFrames, header->time));
            renderRange(*p, renderedFrames, eventFrame - renderedFrames);
            renderedFrames = eventFrame;
            applyProcessEvent(*p, header);
        }
    }
    renderRange(*p, renderedFrames, frames - renderedFrames);
    serviceWaveformPublication(*p);

    float blockPeak = 0.0f;
    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = channel == 0u ? p->outputLeft[frame]
                : channel == 1u ? p->outputRight[frame] : 0.0f;
            if (output.data32 && output.data32[channel])
                output.data32[channel][frame] = value;
            if (output.data64 && output.data64[channel])
                output.data64[channel][frame] = static_cast<double>(value);
            blockPeak = std::max(blockPeak, std::abs(value));
        }
        for (uint32_t frame = frames; frame < processInfo->frames_count; ++frame) {
            if (output.data32 && output.data32[channel])
                output.data32[channel][frame] = 0.0f;
            if (output.data64 && output.data64[channel])
                output.data64[channel][frame] = 0.0;
        }
    }
    p->outputPeak.store(std::max(
        p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    serviceLoopLoadResults(*p);
#endif
    serviceRetiredLoopImports(*p);
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Stereo In" : "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
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
    std::snprintf(info->name, sizeof(info->name), "%s", "Loop Command MIDI In");
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

constexpr std::array<NoteNameDef, 4u> kNoteNames {{
    { kMidiCaptureHold, "HOLD CAPTURE" },
    { kMidiPlayPause, "PLAY / PAUSE" },
    { kMidiReverseA, "LOOP A REVERSE" },
    { kMidiReverseB, "LOOP B REVERSE" },
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

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = (def.automatable ? CLAP_PARAM_IS_AUTOMATABLE : 0u)
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !findParam(id)) return false;
    const auto& params = self(plugin)->params;
    switch (id) {
    case kLoop1RateParamId: *value = params.loop1Rate; return true;
    case kLoop2RateParamId: *value = params.loop2Rate; return true;
    case kCrossfadeModeParamId:
        *value = static_cast<uint32_t>(params.crossfadeMode); return true;
    case kCrossfadeShapeParamId:
        *value = static_cast<uint32_t>(params.crossfadeShape); return true;
    case kCrossfadeParamId: *value = params.crossfade; return true;
    case kBlendParamId: *value = params.blend; return true;
    case kInputGainParamId: *value = params.inputGain; return true;
    case kOutputGainParamId: *value = params.outputGain; return true;
    case kRecordParamId: *value = params.record ? 1.0 : 0.0; return true;
    case kRecordTargetParamId:
        *value = static_cast<uint32_t>(params.recordTarget); return true;
    case kMonitorModeParamId:
        *value = static_cast<uint32_t>(params.monitorMode); return true;
    case kPlaybackModelParamId:
        *value = static_cast<uint32_t>(params.playbackModel); return true;
    case kRecordModeParamId:
        *value = static_cast<uint32_t>(params.recordMode); return true;
    case kOverdubFeedbackParamId: *value = params.overdubFeedback; return true;
    case kLoop1ReverseParamId: *value = params.loop1Reverse ? 1.0 : 0.0; return true;
    case kLoop2ReverseParamId: *value = params.loop2Reverse ? 1.0 : 0.0; return true;
    case kLoop1StartParamId: *value = params.loop1Start; return true;
    case kLoop1EndParamId: *value = params.loop1End; return true;
    case kLoop2StartParamId: *value = params.loop2Start; return true;
    case kLoop2EndParamId: *value = params.loop2End; return true;
    case kLoop1JoinParamId:
        *value = static_cast<uint32_t>(params.loop1Join); return true;
    case kLoop2JoinParamId:
        *value = static_cast<uint32_t>(params.loop2Join); return true;
    case kPlayingParamId: *value = params.playing ? 1.0 : 0.0; return true;
    case kClearLoopAParamId:
    case kClearLoopBParamId: *value = 0.0; return true;
    default: return false;
    }
}

const char* crossfadeModeName(uint32_t value)
{
    constexpr const char* names[] {
        "Manual", "Sine LFO", "Trapezoid LFO", "Random Walk",
        "Triangle LFO", "Ramp A to B", "Ramp B to A", "Sample & Hold",
        "Square LFO"
    };
    return names[std::min<uint32_t>(value, 8u)];
}

const char* crossfadeShapeName(uint32_t value)
{
    constexpr const char* names[] {
        "Equal Power", "Linear", "Wide", "Tight", "Smooth",
        "Full Overlap", "Deep Dip", "Plateau", "Cut"
    };
    return names[std::min<uint32_t>(value, 8u)];
}

const char* targetName(uint32_t value)
{
    constexpr const char* names[] { "Loop 1", "Both", "Loop 2" };
    return names[std::min<uint32_t>(value, 2u)];
}

const char* monitorName(uint32_t value)
{
    constexpr const char* names[] { "Thru Loop 1", "Silent", "Thru Loop 2" };
    return names[std::min<uint32_t>(value, 2u)];
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value,
                       char* display, uint32_t size)
{
    if (!display || size == 0u || !findParam(id)) return false;
    switch (id) {
    case kLoop1RateParamId:
    case kLoop2RateParamId:
        std::snprintf(display, size, "%.2fx", value);
        break;
    case kCrossfadeModeParamId:
        std::snprintf(display, size, "%s", crossfadeModeName(steppedValue(value, 8u)));
        break;
    case kCrossfadeShapeParamId:
        std::snprintf(display, size, "%s",
            crossfadeShapeName(steppedValue(value, 8u)));
        break;
    case kCrossfadeParamId:
        if (self(plugin)->params.crossfadeMode == s3g::CrcltrCrossfadeMode::Manual)
            std::snprintf(display, size, "%.0f%%", value * 100.0);
        else
            std::snprintf(display, size, "%.2fx", 0.25 * std::pow(16.0, value));
        break;
    case kBlendParamId:
    case kInputGainParamId:
    case kOutputGainParamId:
    case kOverdubFeedbackParamId:
    case kLoop1StartParamId:
    case kLoop1EndParamId:
    case kLoop2StartParamId:
    case kLoop2EndParamId:
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        break;
    case kRecordParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "RECORD" : "Idle");
        break;
    case kRecordTargetParamId:
        std::snprintf(display, size, "%s", targetName(steppedValue(value, 2u)));
        break;
    case kMonitorModeParamId:
        std::snprintf(display, size, "%s", monitorName(steppedValue(value, 2u)));
        break;
    case kPlaybackModelParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "Dual" : "CRCLTR Classic");
        break;
    case kRecordModeParamId: {
        constexpr const char* names[] { "Replace", "Overdub", "Punch" };
        std::snprintf(display, size, "%s", names[steppedValue(value, 2u)]);
        break;
    }
    case kLoop1ReverseParamId:
    case kLoop2ReverseParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "Reverse" : "Forward");
        break;
    case kLoop1JoinParamId:
    case kLoop2JoinParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "Duck" : "Seam");
        break;
    case kPlayingParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "Play" : "Pause");
        break;
    case kClearLoopAParamId:
    case kClearLoopBParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "Clear" : "Ready");
        break;
    default:
        return false;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* display,
                       double* value)
{
    const auto* def = findParam(id);
    if (!def || !display || !value) return false;
    double parsed = std::atof(display);
    if (id == kCrossfadeModeParamId) {
        if (std::strcmp(display, "Manual") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Sine LFO") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Trapezoid LFO") == 0) parsed = 2.0;
        else if (std::strcmp(display, "Random Walk") == 0) parsed = 3.0;
        else if (std::strcmp(display, "Triangle LFO") == 0) parsed = 4.0;
        else if (std::strcmp(display, "Ramp A to B") == 0) parsed = 5.0;
        else if (std::strcmp(display, "Ramp B to A") == 0) parsed = 6.0;
        else if (std::strcmp(display, "Sample & Hold") == 0) parsed = 7.0;
        else if (std::strcmp(display, "Square LFO") == 0) parsed = 8.0;
    } else if (id == kCrossfadeShapeParamId) {
        if (std::strcmp(display, "Equal Power") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Linear") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Wide") == 0) parsed = 2.0;
        else if (std::strcmp(display, "Tight") == 0) parsed = 3.0;
        else if (std::strcmp(display, "Smooth") == 0) parsed = 4.0;
        else if (std::strcmp(display, "Full Overlap") == 0) parsed = 5.0;
        else if (std::strcmp(display, "Deep Dip") == 0) parsed = 6.0;
        else if (std::strcmp(display, "Plateau") == 0) parsed = 7.0;
        else if (std::strcmp(display, "Cut") == 0) parsed = 8.0;
    } else if (id == kRecordParamId) {
        parsed = std::strcmp(display, "RECORD") == 0 ? 1.0 : 0.0;
    } else if (id == kRecordTargetParamId) {
        if (std::strcmp(display, "Loop 1") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Both") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Loop 2") == 0) parsed = 2.0;
    } else if (id == kMonitorModeParamId) {
        if (std::strcmp(display, "Thru Loop 1") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Silent") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Thru Loop 2") == 0) parsed = 2.0;
    } else if (id == kPlaybackModelParamId) {
        parsed = std::strcmp(display, "Dual") == 0 ? 1.0 : 0.0;
    } else if (id == kRecordModeParamId) {
        if (std::strcmp(display, "Replace") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Overdub") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Punch") == 0) parsed = 2.0;
    } else if (id == kLoop1ReverseParamId
               || id == kLoop2ReverseParamId) {
        parsed = std::strcmp(display, "Reverse") == 0 ? 1.0 : 0.0;
    } else if (id == kLoop1JoinParamId || id == kLoop2JoinParamId) {
        parsed = std::strcmp(display, "Duck") == 0 ? 1.0 : 0.0;
    } else if (id == kPlayingParamId) {
        parsed = std::strcmp(display, "Play") == 0 ? 1.0 : 0.0;
    } else if (id == kClearLoopAParamId || id == kClearLoopBParamId) {
        parsed = std::strcmp(display, "Clear") == 0 ? 1.0 : 0.0;
    } else if (id == kCrossfadeParamId
               && self(plugin)->params.crossfadeMode
                    != s3g::CrcltrCrossfadeMode::Manual
               && std::strchr(display, 'x')) {
        parsed = std::log(std::max(0.25, parsed) / 0.25) / std::log(16.0);
    }
    if ((id == kCrossfadeParamId || id == kBlendParamId
         || id == kInputGainParamId || id == kOutputGainParamId
         || id == kOverdubFeedbackParamId || id == kLoop1StartParamId
         || id == kLoop1EndParamId || id == kLoop2StartParamId
         || id == kLoop2EndParamId)
        && std::strchr(display, '%')) parsed *= 0.01;
    *value = std::clamp(parsed, def->minimum, def->maximum);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* events,
                 const clap_output_events_t* output)
{
    auto* p = self(plugin);
    readParamEvents(*p, events);
    serviceGuiParamEvents(*p, output);
    if (!p->processing.load(std::memory_order_acquire)) {
        serviceLoopClearRequests(*p);
        serviceWaveformPublication(*p);
    }
}

const clap_plugin_params_t paramsExtension {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush,
};

SavedStateHeader savedStateFor(const s3g::CrcltrParams& params)
{
    SavedStateHeader state {};
    state.headerBytes = sizeof(SavedStateHeader);
    state.loop1Rate = params.loop1Rate;
    state.loop2Rate = params.loop2Rate;
    state.crossfade = params.crossfade;
    state.blend = params.blend;
    state.inputGain = params.inputGain;
    state.outputGain = params.outputGain;
    state.crossfadeMode = static_cast<uint32_t>(params.crossfadeMode);
    state.crossfadeShape = static_cast<uint32_t>(params.crossfadeShape);
    state.recordTarget = static_cast<uint32_t>(params.recordTarget);
    state.monitorMode = static_cast<uint32_t>(params.monitorMode);
    state.playbackModel = static_cast<uint32_t>(params.playbackModel);
    state.recordMode = static_cast<uint32_t>(params.recordMode);
    state.overdubFeedback = params.overdubFeedback;
    state.loop1Reverse = params.loop1Reverse ? 1u : 0u;
    state.loop2Reverse = params.loop2Reverse ? 1u : 0u;
    state.loop1Start = params.loop1Start;
    state.loop1End = params.loop1End;
    state.loop2Start = params.loop2Start;
    state.loop2End = params.loop2End;
    state.loop1Join = static_cast<uint32_t>(params.loop1Join);
    state.loop2Join = static_cast<uint32_t>(params.loop2Join);
    state.playing = params.playing ? 1u : 0u;
    return state;
}

bool writeAll(const clap_ostream_t* stream, const void* source, uint64_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t written = stream->write(stream, bytes + offset, size - offset);
        if (written <= 0) return false;
        offset += static_cast<uint64_t>(written);
    }
    return true;
}

bool readAll(const clap_istream_t* stream, void* destination, uint64_t size)
{
    auto* bytes = static_cast<uint8_t*>(destination);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t read = stream->read(stream, bytes + offset, size - offset);
        if (read <= 0) return false;
        offset += static_cast<uint64_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    if (!snapshotLoops(*p)) return false;
    SavedStateHeader state = savedStateFor(p->params);
    state.audioSampleRate = p->pendingAudioSampleRate;
    state.loopFrames[0] = p->pendingLoopFrames[0];
    state.loopFrames[1] = p->pendingLoopFrames[1];
    if (!writeAll(stream, &state, sizeof(state))) return false;
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        const uint64_t bytes = static_cast<uint64_t>(state.loopFrames[loop])
            * sizeof(float);
        if (bytes == 0u) continue;
        if (!writeAll(stream, p->pendingLoopAudio[loop * 2u].data(), bytes)
            || !writeAll(stream,
                p->pendingLoopAudio[loop * 2u + 1u].data(), bytes))
            return false;
    }
    return true;
}

SavedStateHeader upgradeStateV2(const SavedStateHeaderV2& old)
{
    SavedStateHeader state {};
    state.headerBytes = sizeof(SavedStateHeader);
    state.loop1Rate = old.loop1Rate;
    state.loop2Rate = old.loop2Rate;
    state.crossfade = old.crossfade;
    state.blend = old.blend;
    state.inputGain = old.inputGain;
    state.outputGain = old.outputGain;
    state.overdubFeedback = old.overdubFeedback;
    state.loop1Start = old.loop1Start;
    state.loop1End = old.loop1End;
    state.loop2Start = old.loop2Start;
    state.loop2End = old.loop2End;
    state.crossfadeMode = old.crossfadeMode;
    state.crossfadeShape = 0u;
    state.recordTarget = old.recordTarget;
    state.monitorMode = old.monitorMode;
    state.playbackModel = old.playbackModel;
    state.recordMode = old.recordMode;
    state.loop1Reverse = old.loop1Reverse;
    state.loop2Reverse = old.loop2Reverse;
    state.loop1Join = old.loop1Join;
    state.loop2Join = old.loop2Join;
    state.playing = old.playing;
    state.audioSampleRate = old.audioSampleRate;
    state.loopFrames[0] = old.loopFrames[0];
    state.loopFrames[1] = old.loopFrames[1];
    return state;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto* p = self(plugin);
    p->loopClearRequestMask.store(0u, std::memory_order_release);
    p->loopLoadPendingMask.store(0u, std::memory_order_release);
    p->loopLoadErrorMask.store(0u, std::memory_order_release);
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        p->loopImportGenerations[loop].fetch_add(
            1u, std::memory_order_acq_rel);
        p->loopSourceKinds[loop].store(
            static_cast<uint8_t>(LoopSourceKind::Empty),
            std::memory_order_release);
        p->controlLoadedAudio[loop].reset();
#if defined(__APPLE__)
        p->loopLoadStatuses[loop] = "EMPTY";
#endif
    }
    uint32_t version = 0u;
    if (!readAll(stream, &version, sizeof(version))) return false;
    if (version == 1u) {
        LegacySavedState legacy;
        legacy.version = version;
        if (!readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) return false;
        applyParam(*p, kLoop1RateParamId, legacy.loop1Rate);
        applyParam(*p, kLoop2RateParamId, legacy.loop2Rate);
        applyParam(*p, kCrossfadeModeParamId, legacy.crossfadeMode);
        applyParam(*p, kCrossfadeShapeParamId, 0.0);
        applyParam(*p, kCrossfadeParamId, legacy.crossfade);
        applyParam(*p, kBlendParamId, legacy.blend);
        applyParam(*p, kInputGainParamId, legacy.inputGain);
        applyParam(*p, kOutputGainParamId, legacy.outputGain);
        applyParam(*p, kRecordTargetParamId, legacy.recordTarget);
        applyParam(*p, kMonitorModeParamId, legacy.monitorMode);
        applyParam(*p, kPlaybackModelParamId, 0.0);
        applyParam(*p, kRecordModeParamId, 0.0);
        applyParam(*p, kOverdubFeedbackParamId, 0.8);
        applyParam(*p, kLoop1ReverseParamId, 0.0);
        applyParam(*p, kLoop2ReverseParamId, 0.0);
        applyParam(*p, kLoop1StartParamId, 0.0);
        applyParam(*p, kLoop1EndParamId, 1.0);
        applyParam(*p, kLoop2StartParamId, 0.0);
        applyParam(*p, kLoop2EndParamId, 1.0);
        applyParam(*p, kLoop1JoinParamId, 0.0);
        applyParam(*p, kLoop2JoinParamId, 1.0);
        applyParam(*p, kPlayingParamId, 1.0);
        applyParam(*p, kRecordParamId, 0.0);
        p->pendingAudioSampleRate = 0.0;
        p->pendingLoopFrames.fill(0u);
        for (auto& audio : p->pendingLoopAudio) audio.clear();
        if (p->prepared) p->dsp.clear();
        p->waveformRebuildMask.fetch_or(3u, std::memory_order_release);
        return true;
    }
    SavedStateHeader state {};
    if (version == 2u) {
        SavedStateHeaderV2 old;
        old.version = version;
        if (!readAll(stream,
                reinterpret_cast<uint8_t*>(&old) + sizeof(version),
                sizeof(old) - sizeof(version))
            || old.headerBytes != sizeof(SavedStateHeaderV2)) return false;
        state = upgradeStateV2(old);
    } else if (version == kStateVersion) {
        state.version = version;
        if (!readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))
            || state.headerBytes != sizeof(SavedStateHeader)) return false;
    } else {
        return false;
    }
    if (!std::isfinite(state.audioSampleRate)
        || state.audioSampleRate < 0.0) return false;
    constexpr uint32_t kMaximumPersistedFrames = 192000u * 32u;
    if (state.loopFrames[0] > kMaximumPersistedFrames
        || state.loopFrames[1] > kMaximumPersistedFrames) return false;

    applyParam(*p, kLoop1RateParamId, state.loop1Rate);
    applyParam(*p, kLoop2RateParamId, state.loop2Rate);
    applyParam(*p, kCrossfadeModeParamId, state.crossfadeMode);
    applyParam(*p, kCrossfadeShapeParamId, state.crossfadeShape);
    applyParam(*p, kCrossfadeParamId, state.crossfade);
    applyParam(*p, kBlendParamId, state.blend);
    applyParam(*p, kInputGainParamId, state.inputGain);
    applyParam(*p, kOutputGainParamId, state.outputGain);
    applyParam(*p, kRecordTargetParamId, state.recordTarget);
    applyParam(*p, kMonitorModeParamId, state.monitorMode);
    applyParam(*p, kPlaybackModelParamId, state.playbackModel);
    applyParam(*p, kRecordModeParamId, state.recordMode);
    applyParam(*p, kOverdubFeedbackParamId, state.overdubFeedback);
    applyParam(*p, kLoop1ReverseParamId, state.loop1Reverse);
    applyParam(*p, kLoop2ReverseParamId, state.loop2Reverse);
    applyParam(*p, kLoop1StartParamId, state.loop1Start);
    applyParam(*p, kLoop1EndParamId, state.loop1End);
    applyParam(*p, kLoop2StartParamId, state.loop2Start);
    applyParam(*p, kLoop2EndParamId, state.loop2End);
    applyParam(*p, kLoop1JoinParamId, state.loop1Join);
    applyParam(*p, kLoop2JoinParamId, state.loop2Join);
    applyParam(*p, kPlayingParamId, state.playing);
    applyParam(*p, kRecordParamId, 0.0);
    try {
        p->pendingAudioSampleRate = state.audioSampleRate;
        for (uint32_t loop = 0u; loop < 2u; ++loop) {
            const uint32_t frames = state.loopFrames[loop];
            p->pendingLoopFrames[loop] = frames;
            p->pendingLoopAudio[loop * 2u].resize(frames);
            p->pendingLoopAudio[loop * 2u + 1u].resize(frames);
            const uint64_t bytes = static_cast<uint64_t>(frames)
                * sizeof(float);
            if (bytes > 0u
                && (!readAll(stream, p->pendingLoopAudio[loop * 2u].data(),
                        bytes)
                    || !readAll(stream,
                        p->pendingLoopAudio[loop * 2u + 1u].data(), bytes)))
                return false;
            if (frames > 0u) {
                p->loopSourceKinds[loop].store(
                    static_cast<uint8_t>(LoopSourceKind::Embedded),
                    std::memory_order_release);
#if defined(__APPLE__)
                p->loopLoadStatuses[loop] = "PROJECT LOOP";
#endif
            }
        }
    } catch (...) {
        return false;
    }
    if (p->prepared) p->dsp.clear();
    if (!restorePendingLoops(*p)) return false;
    p->waveformRebuildMask.fetch_or(3u, std::memory_order_release);
    return true;
}

const clap_plugin_state_t stateExtension {
    stateSave,
    stateLoad,
};

#if defined(__APPLE__)

std::string loopSourceName(const std::string& path)
{
    const std::string name = std::filesystem::path(path).filename().string();
    return name.empty() ? "SAMPLE" : name;
}

bool decodeLoopSample(const LoopLoadRequest& request,
                      std::shared_ptr<const ImportedLoopAudio>& audioOut,
                      std::string& error)
{
    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:request.path.c_str()];
        NSError* nsError = nil;
        AVAudioFile* file = path ? [[AVAudioFile alloc]
            initForReading:[NSURL fileURLWithPath:path] error:&nsError] : nil;
        if (!file) {
            error = "COULD NOT OPEN SAMPLE";
            return false;
        }
        AVAudioFormat* format = [file processingFormat];
        const AVAudioChannelCount channels = [format channelCount];
        const AVAudioFramePosition fileFrameCount = [file length];
        const double sourceSampleRate = [format sampleRate];
        if (channels < 1u || channels > 2u || fileFrameCount < 1
            || !std::isfinite(sourceSampleRate) || sourceSampleRate <= 1.0
            || !std::isfinite(request.destinationSampleRate)
            || request.destinationSampleRate <= 1.0
            || request.destinationCapacity < 2u) {
            [file release];
            error = "USE A VALID MONO OR STEREO SAMPLE";
            return false;
        }
        const uint64_t maximumSourceFrames = std::max<uint64_t>(2u,
            static_cast<uint64_t>(std::ceil(sourceSampleRate
                * static_cast<double>(s3g::kCrcltrMaximumRecordSeconds)))
                + 1u);
        const uint64_t sourceFrames64 = std::min<uint64_t>(
            static_cast<uint64_t>(fileFrameCount), maximumSourceFrames);
        if (sourceFrames64 > std::numeric_limits<AVAudioFrameCount>::max()) {
            [file release];
            error = "SAMPLE IS TOO LONG";
            return false;
        }
        const AVAudioFrameCount sourceCapacity = static_cast<
            AVAudioFrameCount>(sourceFrames64);
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:format frameCapacity:sourceCapacity];
        if (!buffer || ![file readIntoBuffer:buffer error:&nsError]
            || [buffer frameLength] == 0u || ![buffer floatChannelData]) {
            [buffer release];
            [file release];
            error = "SAMPLE DECODE FAILED";
            return false;
        }
        const uint32_t decodedFrames = [buffer frameLength];
        const double scale = request.destinationSampleRate / sourceSampleRate;
        const uint64_t desiredFrames = std::max<uint64_t>(2u,
            static_cast<uint64_t>(std::llround(
                static_cast<double>(decodedFrames) * scale)));
        const uint32_t destinationFrames = static_cast<uint32_t>(
            std::min<uint64_t>(request.destinationCapacity, desiredFrames));
        try {
            auto audio = std::make_shared<ImportedLoopAudio>();
            audio->sampleRate = request.destinationSampleRate;
            audio->left.resize(destinationFrames);
            audio->right.resize(destinationFrames);
            audio->name = loopSourceName(request.path);
            audio->truncated = static_cast<uint64_t>(fileFrameCount)
                    > sourceFrames64
                || desiredFrames > request.destinationCapacity;
            const float* sourceLeft = [buffer floatChannelData][0u];
            const float* sourceRight = channels > 1u
                ? [buffer floatChannelData][1u] : sourceLeft;
            for (uint32_t frame = 0u; frame < destinationFrames; ++frame) {
                const double sourcePosition = std::min(
                    static_cast<double>(decodedFrames - 1u),
                    static_cast<double>(frame) / scale);
                const uint32_t first = static_cast<uint32_t>(sourcePosition);
                const uint32_t second = std::min<uint32_t>(
                    decodedFrames - 1u, first + 1u);
                const float amount = static_cast<float>(sourcePosition
                    - std::floor(sourcePosition));
                const float left = sourceLeft[first]
                    + (sourceLeft[second] - sourceLeft[first]) * amount;
                const float right = sourceRight[first]
                    + (sourceRight[second] - sourceRight[first]) * amount;
                audio->left[frame] = std::isfinite(left) ? left : 0.0f;
                audio->right[frame] = std::isfinite(right) ? right : 0.0f;
            }
            if (!audio->valid()) {
                [buffer release];
                [file release];
                error = "DECODED SAMPLE IS INVALID";
                return false;
            }
            audioOut = std::move(audio);
        } catch (...) {
            [buffer release];
            [file release];
            error = "SAMPLE DECODE RAN OUT OF MEMORY";
            return false;
        }
        [buffer release];
        [file release];
        error.clear();
        return true;
    }
}

bool startLoopLoader(Plugin& plugin)
{
    try {
        plugin.loaderThread = std::thread([&plugin] {
            for (;;) {
                LoopLoadRequest request;
                {
                    std::unique_lock<std::mutex> lock(plugin.loaderMutex);
                    plugin.loaderCondition.wait(lock, [&plugin] {
                        return plugin.loaderStopping
                            || !plugin.loadRequests.empty();
                    });
                    if (plugin.loaderStopping) return;
                    request = std::move(plugin.loadRequests.front());
                    plugin.loadRequests.pop_front();
                }
                LoopLoadResult result;
                result.loop = request.loop;
                result.generation = request.generation;
                try {
                    (void)decodeLoopSample(request, result.audio,
                        result.error);
                } catch (...) {
                    result.error = "SAMPLE DECODE FAILED";
                }
                {
                    std::lock_guard<std::mutex> lock(plugin.loaderMutex);
                    if (plugin.loaderStopping) return;
                    plugin.loadResults.push_back(std::move(result));
                }
                if (plugin.host && plugin.host->request_callback)
                    plugin.host->request_callback(plugin.host);
            }
        });
    } catch (...) {
        return false;
    }
    return true;
}

void stopLoopLoader(Plugin& plugin)
{
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        plugin.loaderStopping = true;
        plugin.loadRequests.clear();
    }
    plugin.loaderCondition.notify_all();
    if (plugin.loaderThread.joinable()) plugin.loaderThread.join();
    std::lock_guard<std::mutex> lock(plugin.loaderMutex);
    plugin.loadResults.clear();
}

bool installDecodedLoop(Plugin& plugin, uint32_t loop, uint64_t generation,
                        std::shared_ptr<const ImportedLoopAudio> audio)
{
    if (loop >= 2u || !audio || !audio->valid()
        || generation != plugin.loopImportGenerations[loop].load(
            std::memory_order_acquire)) return false;
    applyParam(plugin, kRecordParamId, 0.0);
    plugin.controlLoadedAudio[loop] = audio;
    if (!plugin.prepared) {
        try {
            plugin.pendingAudioSampleRate = audio->sampleRate;
            plugin.pendingLoopFrames[loop] = static_cast<uint32_t>(
                audio->left.size());
            plugin.pendingLoopAudio[loop * 2u] = audio->left;
            plugin.pendingLoopAudio[loop * 2u + 1u] = audio->right;
        } catch (...) {
            plugin.loopLoadStatuses[loop] = "LOAD RAN OUT OF MEMORY";
            return false;
        }
        plugin.loopSourceKinds[loop].store(
            static_cast<uint8_t>(LoopSourceKind::File),
            std::memory_order_release);
        publishImportedWaveform(plugin, loop, *audio);
        plugin.loopLoadStatuses[loop] = audio->name
            + (audio->truncated ? " / 32S MAX" : " / READY");
        plugin.loopLoadPendingMask.fetch_and(~(1u << loop),
            std::memory_order_acq_rel);
        plugin.loopLoadErrorMask.fetch_and(~(1u << loop),
            std::memory_order_acq_rel);
    } else if (!plugin.processing.load(std::memory_order_acquire)) {
        if (!plugin.dsp.restoreLoop(loop, audio->left.data(),
                audio->right.data(), static_cast<uint32_t>(audio->left.size()),
                audio->sampleRate)) {
            plugin.loopLoadStatuses[loop] = "LOAD INSTALL FAILED";
            return false;
        }
        plugin.loopSourceKinds[loop].store(
            static_cast<uint8_t>(LoopSourceKind::File),
            std::memory_order_release);
        beginWaveformRebuild(plugin, loop);
        while (plugin.waveformBuilders[loop].rebuilding)
            continueWaveformRebuild(plugin, loop, 65536u);
        plugin.loopLoadStatuses[loop] = audio->name
            + (audio->truncated ? " / 32S MAX" : " / READY");
        plugin.loopLoadPendingMask.fetch_and(~(1u << loop),
            std::memory_order_acq_rel);
        plugin.loopLoadErrorMask.fetch_and(~(1u << loop),
            std::memory_order_acq_rel);
    } else {
        if (std::abs(audio->sampleRate - plugin.dsp.sampleRate()) >= 0.5) {
            plugin.loopLoadStatuses[loop] = "RATE CHANGED / LOAD AGAIN";
            return false;
        }
        auto* command = new (std::nothrow) LoopImportCommand();
        if (!command) {
            plugin.loopLoadStatuses[loop] = "LOAD RAN OUT OF MEMORY";
            return false;
        }
        command->loop = loop;
        command->generation = generation;
        command->audio = std::move(audio);
        if (!plugin.loopImportQueue.push(command)) {
            delete command;
            plugin.loopLoadStatuses[loop] = "LOAD QUEUE FULL";
            return false;
        }
        plugin.loopSourceKinds[loop].store(
            static_cast<uint8_t>(LoopSourceKind::Loading),
            std::memory_order_release);
        plugin.loopLoadStatuses[loop] = "INSTALLING SAMPLE";
        if (plugin.host && plugin.host->request_process)
            plugin.host->request_process(plugin.host);
    }
    markStateDirty(plugin);
    return true;
}

void serviceLoopLoadResults(Plugin& plugin)
{
    serviceRetiredLoopImports(plugin);
    std::deque<LoopLoadResult> results;
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        results.swap(plugin.loadResults);
    }
    for (auto& result : results) {
        if (result.loop >= 2u || result.generation
                != plugin.loopImportGenerations[result.loop].load(
                    std::memory_order_acquire)) continue;
        if (!result.audio) {
            plugin.loopLoadStatuses[result.loop] = result.error.empty()
                ? "LOAD FAILED" : result.error;
            plugin.loopLoadPendingMask.fetch_and(~(1u << result.loop),
                std::memory_order_acq_rel);
            plugin.loopLoadErrorMask.fetch_or(1u << result.loop,
                std::memory_order_acq_rel);
            continue;
        }
        if (!installDecodedLoop(plugin, result.loop, result.generation,
                std::move(result.audio))) {
            plugin.loopLoadPendingMask.fetch_and(~(1u << result.loop),
                std::memory_order_acq_rel);
            plugin.loopLoadErrorMask.fetch_or(1u << result.loop,
                std::memory_order_acq_rel);
        }
    }
}

void queueLoopLoad(Plugin& plugin, uint32_t loop, std::string path)
{
    if (loop >= 2u || path.empty()) return;
    applyParam(plugin, kRecordParamId, 0.0);
    const uint64_t generation = plugin.loopImportGenerations[loop].fetch_add(
        1u, std::memory_order_acq_rel) + 1u;
    const double destinationSampleRate = plugin.prepared
        ? plugin.dsp.sampleRate()
        : plugin.pendingAudioSampleRate > 1.0
            ? plugin.pendingAudioSampleRate : plugin.sampleRate;
    LoopLoadRequest request;
    request.loop = loop;
    request.generation = generation;
    request.destinationSampleRate = destinationSampleRate;
    request.destinationCapacity = s3g::Crcltr::requiredLoopCapacity(
        destinationSampleRate);
    request.path = std::move(path);
    const std::string name = loopSourceName(request.path);
    {
        std::lock_guard<std::mutex> lock(plugin.loaderMutex);
        plugin.loadRequests.erase(std::remove_if(
            plugin.loadRequests.begin(), plugin.loadRequests.end(),
            [loop](const LoopLoadRequest& queued) {
                return queued.loop == loop;
            }), plugin.loadRequests.end());
        plugin.loadRequests.push_back(std::move(request));
    }
    plugin.loopLoadStatuses[loop] = "LOADING " + name;
    plugin.loopLoadPendingMask.fetch_or(1u << loop,
        std::memory_order_acq_rel);
    plugin.loopLoadErrorMask.fetch_and(~(1u << loop),
        std::memory_order_acq_rel);
    plugin.loaderCondition.notify_one();
}

#endif

} // namespace

#if defined(__APPLE__)

constexpr s3g::gui_layout::Canvas kCrcltrCanvas { 760.0, 660.0 };
constexpr s3g::gui_layout::Rect kCrcltrWaveformPanel {
    18.0, 42.0, 724.0, 142.0,
};
constexpr CGFloat kCrcltrWaveContentY = 64.0;
constexpr CGFloat kCrcltrWaveLaneHeight = 54.0;
constexpr CGFloat kCrcltrWaveX = 120.0;
constexpr CGFloat kCrcltrWaveWidth = 528.0;
constexpr s3g::gui_layout::Rect kCrcltrCrossfadeGraph {
    48.0, 70.0, 48.0, 94.0,
};

NSRect crcltrCrossfadeGraphRect()
{
    return NSMakeRect(kCrcltrCrossfadeGraph.x,
        kCrcltrCrossfadeGraph.y, kCrcltrCrossfadeGraph.width,
        kCrcltrCrossfadeGraph.height);
}

NSRect crcltrWaveRect(uint32_t loop)
{
    return NSMakeRect(kCrcltrWaveX,
        kCrcltrWaveContentY + kCrcltrWaveLaneHeight * loop + 2.0,
        kCrcltrWaveWidth, kCrcltrWaveLaneHeight - 6.0);
}

CGFloat crcltrWaveSourceScale(const Plugin& plugin, uint32_t loop)
{
    return loop == 0u && plugin.params.playbackModel
            == s3g::CrcltrPlaybackModel::Classic
        ? 0.5 : 1.0;
}

float crcltrLoopStart(const Plugin& plugin, uint32_t loop)
{
    return loop == 0u ? plugin.params.loop1Start : plugin.params.loop2Start;
}

float crcltrLoopEnd(const Plugin& plugin, uint32_t loop)
{
    return loop == 0u ? plugin.params.loop1End : plugin.params.loop2End;
}
constexpr s3g::gui_layout::Column kCrcltrFirstColumn {
    s3g::gui_layout::kCompactEffectFamilyLayout.firstColumn.x,
    s3g::gui_layout::kCompactEffectFamilyLayout.firstColumn.width,
    196.0,
};
constexpr s3g::gui_layout::Column kCrcltrSecondColumn {
    s3g::gui_layout::kCompactEffectFamilyLayout.secondColumn.x,
    s3g::gui_layout::kCompactEffectFamilyLayout.secondColumn.width,
    196.0,
};
constexpr auto kOutputPanel = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::CompactEffect,
    s3g::gui_layout::PanelRole::Output, kCrcltrFirstColumn,
    kCrcltrFirstColumn.top, 4u);
constexpr auto kCapturePanel = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::EventTiming, kOutputPanel, 6u);
constexpr auto kLoopsPanel = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::CompactEffect,
    s3g::gui_layout::PanelRole::Projection, kCrcltrSecondColumn,
    kCrcltrSecondColumn.top, 11u);
constexpr auto kCrossfadePanel =
    s3g::gui_layout::fittedStackPanel(
        s3g::gui_layout::PanelRole::Relationships, kLoopsPanel, 3u);
constexpr std::array kFirstColumnPanels { kOutputPanel, kCapturePanel };
constexpr std::array kSecondColumnPanels { kLoopsPanel, kCrossfadePanel };
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnPanels, kCrcltrCanvas));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnPanels, kCrcltrCanvas,
    false));

NSRect processorMenuRect(const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

NSRect crcltrClearButtonRect(uint32_t loop)
{
    constexpr CGFloat gap = 6.0;
    const CGFloat controlX = s3g::gui_layout::processorControlX(
        kCapturePanel.frame.x);
    const CGFloat totalWidth = s3g::gui_layout::processorMenuWidth(
        kCapturePanel.frame.width);
    const CGFloat width = (totalWidth - gap) * 0.5;
    return NSMakeRect(controlX + static_cast<CGFloat>(loop) * (width + gap),
        s3g::gui_layout::rowY(kCapturePanel, 5u) - 1.0, width, 15.0);
}

NSRect crcltrLoadButtonRect(uint32_t loop)
{
    constexpr CGFloat width = 62.0;
    constexpr CGFloat gap = 6.0;
    return NSMakeRect(602.0 + static_cast<CGFloat>(loop) * (width + gap),
        45.0, width, 15.0);
}

uint32_t crcltrMenuItemCount(int paramId)
{
    if (paramId == static_cast<int>(kCrossfadeModeParamId)
        || paramId == static_cast<int>(kCrossfadeShapeParamId)) return 9u;
    if (paramId == static_cast<int>(kPlaybackModelParamId)
        || paramId == static_cast<int>(kLoop1JoinParamId)
        || paramId == static_cast<int>(kLoop2JoinParamId)) return 2u;
    return 3u;
}

@interface S3GCrcltrView : NSView {
    void* _plugin;
    int _dragSlider;
    int _dragWaveMarker;
    bool _dragCrossfadeGraph;
    int _openMenu;
    int _hoverMenuItem;
    bool _recordHeld;
    NSPoint _menuOrigin;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)point;
- (void)updateWaveMarker:(NSPoint)point;
- (void)updateCrossfadeGraph:(NSPoint)point;
- (void)loadLoop:(uint32_t)loop;
- (BOOL)loadDocumentationSample;
- (void)drawLoopWaveforms:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values;
@end

@implementation S3GCrcltrView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _dragWaveMarker = -1;
        _dragCrossfadeGraph = false;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _recordHeld = false;
        _menuOrigin = NSZeroPoint;
        _timer = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    NSArray* existingAreas = [[self trackingAreas] copy];
    for (NSTrackingArea* area in existingAreas) {
        [self removeTrackingArea:area];
    }
    [existingAreas release];
    NSTrackingAreaOptions options = NSTrackingMouseMoved
        | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:area];
    [area release];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_dragWaveMarker > 0 && _plugin) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragWaveMarker));
        _dragWaveMarker = -1;
    }
    if (_dragCrossfadeGraph && _plugin) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            kCrossfadeParamId);
        _dragCrossfadeGraph = false;
    }
    if (_recordHeld && _plugin) {
        applyParam(*static_cast<Plugin*>(_plugin), kRecordParamId, 0.0);
        _recordHeld = false;
    }
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (_plugin) {
        auto& plugin = *static_cast<Plugin*>(_plugin);
        serviceLoopLoadResults(plugin);
    }
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawSlider:(NSString*)label
              param:(clap_id)paramId
                row:(uint32_t)row
              panel:(const s3g::gui_layout::Panel&)panel
              attrs:(NSDictionary*)attrs
             values:(NSDictionary*)values
              style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const ParamDef* def = findParam(paramId);
    if (!p || !def) return;
    double value = 0.0;
    paramsGetValue(&p->plugin, paramId, &value);
    const double span = std::max(0.000001, def->maximum - def->minimum);
    const CGFloat norm = static_cast<CGFloat>(
        (value - def->minimum) / span);
    char text[32] {};
    paramsValueToText(&p->plugin, paramId, value, text, sizeof(text));
    s3g::clap_gui::drawProcessorSlider(
        label, [NSString stringWithUTF8String:text], norm,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, values, style);
}

- (void)drawMenu:(NSString*)label
            value:(NSString*)value
              row:(uint32_t)row
            panel:(const s3g::gui_layout::Panel&)panel
            attrs:(NSDictionary*)attrs
           values:(NSDictionary*)values
            style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(label, value,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, values, style);
}

- (void)drawLoopWaveforms:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const float fade = p->params.crossfadeMode
            == s3g::CrcltrCrossfadeMode::Manual
        ? std::clamp(p->params.crossfade, 0.0f, 1.0f)
        : std::clamp(p->currentCrossfade.load(
            std::memory_order_relaxed), 0.0f, 1.0f);
    const auto mixGains = s3g::crcltrCrossfadeGains(
        p->params.crossfadeShape, fade);
    const float gains[2] { mixGains.a, mixGains.b };
    s3g::clap_gui::drawPanelFrame(kCrcltrWaveformPanel.x,
        kCrcltrWaveformPanel.y, kCrcltrWaveformPanel.width,
        kCrcltrWaveformPanel.height, style);
    s3g::clap_gui::drawPanelHeader(@"STEREO LOOP SOURCES", true,
        kCrcltrWaveformPanel.x, kCrcltrWaveformPanel.y,
        kCrcltrWaveformPanel.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);

    NSColor* loopTones[2] {
        s3g::clap_gui::color(0xd5d9d7),
        s3g::clap_gui::color(0xaeb4b1),
    };
    NSColor* crossfadeTones[3] {
        s3g::clap_gui::color(0x5f91a8),
        s3g::clap_gui::color(0x927fa5),
        s3g::clap_gui::color(0xb1845f),
    };
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        const CGFloat laneY = kCrcltrWaveContentY
            + kCrcltrWaveLaneHeight * loop;
        auto& publication = p->loopWaveforms[loop];
        const uint32_t bins = publication.bins.load(std::memory_order_acquire);
        const uint32_t validBins = std::min(bins,
            publication.validBins.load(std::memory_order_acquire));
        const uint32_t frameCount = publication.frames.load(
            std::memory_order_relaxed);

        const CGFloat sourceScale = crcltrWaveSourceScale(*p, loop);
        float start = crcltrLoopStart(*p, loop) * sourceScale;
        float end = crcltrLoopEnd(*p, loop) * sourceScale;
        start = std::clamp(start, 0.0f, 1.0f);
        end = std::clamp(end, start, 1.0f);
        const NSRect waveRect = crcltrWaveRect(loop);
        const CGFloat startX = kCrcltrWaveX + kCrcltrWaveWidth * start;
        const CGFloat endX = kCrcltrWaveX + kCrcltrWaveWidth * end;
        const bool windowPending = p->loopWindowPending[loop].load(
            std::memory_order_relaxed);

        [s3g::clap_gui::color(0x111111) setFill];
        NSRectFill(NSMakeRect(26.0, laneY, 708.0,
            kCrcltrWaveLaneHeight - 2.0));
        [s3g::clap_gui::color(0x090a0a) setFill];
        NSRectFill(waveRect);
        [s3g::clap_gui::color(0x343434, 0.85) setStroke];
        NSBezierPath* centerLines = [NSBezierPath bezierPath];
        [centerLines moveToPoint:NSMakePoint(kCrcltrWaveX, laneY + 14.0)];
        [centerLines lineToPoint:NSMakePoint(
            kCrcltrWaveX + kCrcltrWaveWidth,
            laneY + 14.0)];
        [centerLines moveToPoint:NSMakePoint(kCrcltrWaveX, laneY + 38.0)];
        [centerLines lineToPoint:NSMakePoint(
            kCrcltrWaveX + kCrcltrWaveWidth,
            laneY + 38.0)];
        [centerLines stroke];

        float peak = 0.0f;
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            for (uint32_t bin = 0u; bin < validBins; ++bin) {
                peak = std::max(peak, std::abs(
                    publication.minimum[channel][bin].load(
                        std::memory_order_relaxed)));
                peak = std::max(peak, std::abs(
                    publication.maximum[channel][bin].load(
                        std::memory_order_relaxed)));
            }
        }
        const float displayGain = 0.92f / std::max(0.02f, peak);
        for (uint32_t channel = 0u; channel < 2u; ++channel) {
            NSBezierPath* path = [NSBezierPath bezierPath];
            path.lineWidth = 1.0;
            const CGFloat center = laneY + (channel == 0u ? 14.0 : 38.0);
            for (uint32_t bin = 0u; bin < validBins; ++bin) {
                const float minimum = std::clamp(
                    publication.minimum[channel][bin].load(
                        std::memory_order_relaxed) * displayGain, -1.0f, 1.0f);
                const float maximum = std::clamp(
                    publication.maximum[channel][bin].load(
                        std::memory_order_relaxed) * displayGain, -1.0f, 1.0f);
                const CGFloat x = kCrcltrWaveX
                    + (static_cast<CGFloat>(bin) + 0.5)
                        / std::max<uint32_t>(1u, bins)
                        * kCrcltrWaveWidth;
                [path moveToPoint:NSMakePoint(x, center - maximum * 10.5)];
                [path lineToPoint:NSMakePoint(x, center - minimum * 10.5)];
            }
            [[loopTones[loop] colorWithAlphaComponent:0.58] setStroke];
            [path stroke];
        }

        if (windowPending) {
            const CGFloat activeStart = std::clamp<CGFloat>(
                p->activeLoopStart[loop].load(std::memory_order_relaxed)
                    * sourceScale,
                0.0, 1.0);
            const CGFloat activeEnd = std::clamp<CGFloat>(
                p->activeLoopEnd[loop].load(std::memory_order_relaxed)
                    * sourceScale,
                activeStart, 1.0);
            [s3g::clap_gui::color(0xb4b4b4, 0.72) setStroke];
            for (const CGFloat x : {
                    kCrcltrWaveX + kCrcltrWaveWidth * activeStart,
                    kCrcltrWaveX + kCrcltrWaveWidth * activeEnd }) {
                NSBezierPath* activeMarker = [NSBezierPath bezierPath];
                const CGFloat dash[] { 3.0, 3.0 };
                [activeMarker setLineDash:dash count:2 phase:0.0];
                [activeMarker moveToPoint:NSMakePoint(x, NSMinY(waveRect))];
                [activeMarker lineToPoint:NSMakePoint(x, NSMaxY(waveRect))];
                activeMarker.lineWidth = 1.0;
                [activeMarker stroke];
            }
        }

        [style.accent setStroke];
        for (const CGFloat x : { startX, endX }) {
            NSBezierPath* marker = [NSBezierPath bezierPath];
            [marker moveToPoint:NSMakePoint(x, NSMinY(waveRect))];
            [marker lineToPoint:NSMakePoint(x, NSMaxY(waveRect))];
            marker.lineWidth = 1.4;
            [marker stroke];
        }
        NSDictionary* markerText = s3g::clap_gui::textAttrs(
            style.text, 9.0);
        [@"S" drawAtPoint:NSMakePoint(startX + 4.0, NSMinY(waveRect) + 2.0)
            withAttributes:markerText];
        [@"E" drawAtPoint:NSMakePoint(endX - 12.0, NSMinY(waveRect) + 2.0)
            withAttributes:markerText];

        if (frameCount > 0u) {
            const CGFloat playheadX = kCrcltrWaveX
                + kCrcltrWaveWidth * std::clamp(
                p->loopPosition[loop].load(std::memory_order_relaxed),
                0.0f, 1.0f);
            [s3g::clap_gui::color(0xffffff,
                0.58 + 0.42 * gains[loop]) setStroke];
            NSBezierPath* playhead = [NSBezierPath bezierPath];
            [playhead moveToPoint:NSMakePoint(playheadX, laneY + 2.0)];
            [playhead lineToPoint:NSMakePoint(playheadX,
                laneY + kCrcltrWaveLaneHeight - 4.0)];
            playhead.lineWidth = 1.3;
            [playhead stroke];
        }

        [[NSString stringWithFormat:@"%c", loop == 0u ? 'A' : 'B']
            drawAtPoint:NSMakePoint(28.0, laneY + 9.0)
            withAttributes:s3g::clap_gui::textAttrs(
                crossfadeTones[loop == 0u ? 0u : 2u], 12.0)];
        [@"L" drawAtPoint:NSMakePoint(108.0, laneY + 7.0)
            withAttributes:values];
        [@"R" drawAtPoint:NSMakePoint(108.0, laneY + 31.0)
            withAttributes:values];
        const double seconds = p->sampleRate > 1.0
            ? static_cast<double>(frameCount) / p->sampleRate : 0.0;
        const bool recordTargeted = p->params.record
            && (p->params.recordTarget == s3g::CrcltrRecordTarget::Both
                || (loop == 0u && p->params.recordTarget
                    == s3g::CrcltrRecordTarget::Loop1)
                || (loop == 1u && p->params.recordTarget
                    == s3g::CrcltrRecordTarget::Loop2));
        const auto sourceKind = static_cast<LoopSourceKind>(
            p->loopSourceKinds[loop].load(std::memory_order_acquire));
        const bool loadPending = (p->loopLoadPendingMask.load(
            std::memory_order_acquire) & (1u << loop)) != 0u;
        const bool loadError = (p->loopLoadErrorMask.load(
            std::memory_order_acquire) & (1u << loop)) != 0u;
        NSString* status = recordTargeted ? @"RECORDING"
            : loadPending || sourceKind == LoopSourceKind::Loading
                ? @"LOADING"
            : loadError ? @"LOAD ERROR"
            : frameCount == 0u ? @"EMPTY"
            : windowPending ? @"NEXT WRAP"
            : validBins < bins ? @"ANALYZING"
            : sourceKind == LoopSourceKind::File
                ? [NSString stringWithFormat:@"FILE %.2fs", seconds]
                : [NSString stringWithFormat:@"%.0f%% %.2fs",
                    gains[loop] * 100.0f, seconds];
        [status drawAtPoint:NSMakePoint(660.0, laneY + 17.0)
            withAttributes:values];
    }

    const NSRect graph = crcltrCrossfadeGraphRect();
    [s3g::clap_gui::color(0x090a0a) setFill];
    NSRectFill(graph);
    constexpr uint32_t kGradientStops = 33u;
    constexpr CGFloat kAColor[3] { 0.3725, 0.5686, 0.6588 };
    constexpr CGFloat kOverlapColor[3] { 0.5725, 0.4980, 0.6471 };
    constexpr CGFloat kBColor[3] { 0.6941, 0.5176, 0.3725 };
    std::array<CGFloat, kGradientStops> gradientLocations {};
    NSMutableArray<NSColor*>* gradientColors =
        [NSMutableArray arrayWithCapacity:kGradientStops];
    for (uint32_t stop = 0u; stop < kGradientStops; ++stop) {
        const CGFloat unit = static_cast<CGFloat>(stop)
            / static_cast<CGFloat>(kGradientStops - 1u);
        const auto stopGains = s3g::crcltrCrossfadeGains(
            p->params.crossfadeShape, static_cast<float>(unit));
        const CGFloat gainSum = std::max(0.0001f,
            stopGains.a + stopGains.b);
        const CGFloat aIdentity = stopGains.a / gainSum;
        const CGFloat overlap = std::clamp(2.0 * std::min(
            stopGains.a, stopGains.b), 0.0, 1.0);
        const CGFloat energy = std::sqrt(
            stopGains.a * stopGains.a + stopGains.b * stopGains.b);
        const CGFloat energyScale = std::clamp(
            0.65 + 0.35 * energy, 0.82, 1.08);
        CGFloat color[3] {};
        for (uint32_t component = 0u; component < 3u; ++component) {
            const CGFloat identity = kAColor[component] * aIdentity
                + kBColor[component] * (1.0 - aIdentity);
            color[component] = std::clamp((identity * (1.0 - overlap)
                + kOverlapColor[component] * overlap) * energyScale,
                0.0, 1.0);
        }
        gradientLocations[stop] = unit;
        [gradientColors addObject:[NSColor colorWithCalibratedRed:color[0]
            green:color[1] blue:color[2] alpha:0.96]];
    }
    NSGradient* gainGradient = [[NSGradient alloc]
        initWithColors:gradientColors
        atLocations:gradientLocations.data()
        colorSpace:[NSColorSpace genericRGBColorSpace]];
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:graph] addClip];
    [gainGradient drawFromPoint:NSMakePoint(NSMidX(graph), NSMinY(graph))
        toPoint:NSMakePoint(NSMidX(graph), NSMaxY(graph)) options:0];
    [NSGraphicsContext restoreGraphicsState];
    [gainGradient release];
    [s3g::clap_gui::color(0x323434, 0.56) setStroke];
    for (uint32_t division = 1u; division < 4u; ++division) {
        const CGFloat y = NSMinY(graph) + NSHeight(graph)
            * static_cast<CGFloat>(division) / 4.0;
        NSBezierPath* guide = [NSBezierPath bezierPath];
        [guide moveToPoint:NSMakePoint(NSMinX(graph), y)];
        [guide lineToPoint:NSMakePoint(NSMaxX(graph), y)];
        [guide stroke];
    }
    [style.grid setStroke];
    NSFrameRect(graph);
    const CGFloat headUnit = static_cast<CGFloat>(fade);
    const CGFloat headY = NSMinY(graph) + headUnit * NSHeight(graph);
    [[style.accent colorWithAlphaComponent:0.78] setStroke];
    NSBezierPath* head = [NSBezierPath bezierPath];
    [head moveToPoint:NSMakePoint(NSMinX(graph), headY)];
    [head lineToPoint:NSMakePoint(NSMaxX(graph), headY)];
    head.lineWidth = 1.2;
    [head stroke];
    const NSPoint position = NSMakePoint(NSMidX(graph), headY);
    [s3g::clap_gui::color(0x050505, 0.95) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        position.x - 4.0, position.y - 4.0, 8.0, 8.0)] fill];
    [style.accent setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        position.x - 2.5, position.y - 2.5, 5.0, 5.0)] fill];
    const float direction = p->crossfadeDirection.load(
        std::memory_order_relaxed);
    if (std::abs(direction) > 0.5f) {
        const CGFloat arrowX = NSMaxX(graph) + 7.0;
        NSBezierPath* arrow = [NSBezierPath bezierPath];
        if (direction > 0.0f) {
            [arrow moveToPoint:NSMakePoint(arrowX - 3.5, headY - 3.0)];
            [arrow lineToPoint:NSMakePoint(arrowX + 3.5, headY - 3.0)];
            [arrow lineToPoint:NSMakePoint(arrowX, headY + 4.0)];
        } else {
            [arrow moveToPoint:NSMakePoint(arrowX - 3.5, headY + 3.0)];
            [arrow lineToPoint:NSMakePoint(arrowX + 3.5, headY + 3.0)];
            [arrow lineToPoint:NSMakePoint(arrowX, headY - 4.0)];
        }
        [arrow closePath];
        [[style.accent colorWithAlphaComponent:0.82] setFill];
        [arrow fill];
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    s3g::clap_gui::Style style;
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill([self bounds]);

    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        kCrcltrCanvas);
    s3g::clap_gui::drawCompactEffectTitleBand(
        @"s3g SAMPLE CIRCULATOR",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);

    [self drawLoopWaveforms:style labels:labels values:values];

    s3g::clap_gui::drawHeaderActionButton(crcltrLoadButtonRect(0u),
        crcltrLoadButtonRect(0u), @"LOAD A",
        s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x5f91a8), 10.0),
        style);
    s3g::clap_gui::drawHeaderActionButton(crcltrLoadButtonRect(1u),
        crcltrLoadButtonRect(1u), @"LOAD B",
        s3g::clap_gui::textAttrs(s3g::clap_gui::color(0xb1845f), 10.0),
        style);

    const auto drawPanel = [&](NSString* title,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel, style);
        s3g::clap_gui::drawPanelHeader(title, true, panel, labels, style);
    };
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"CAPTURE", kCapturePanel);
    drawPanel(@"LOOPS", kLoopsPanel);
    drawPanel(@"CROSSFADE", kCrossfadePanel);

    s3g::clap_gui::drawToggle(@"PLAY [C#2]", p->params.playing,
        s3g::gui_layout::rowY(kOutputPanel, 0u), labels, values, style,
        s3g::gui_layout::processorLabelX(kOutputPanel.frame.x),
        s3g::gui_layout::processorControlX(kOutputPanel.frame.x),
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width));
    [self drawSlider:@"IN" param:kInputGainParamId row:1u panel:kOutputPanel
        attrs:labels values:values style:style];
    [self drawSlider:@"BLEND" param:kBlendParamId row:2u panel:kOutputPanel
        attrs:labels values:values style:style];
    [self drawSlider:@"OUT" param:kOutputGainParamId row:3u panel:kOutputPanel
        attrs:labels values:values style:style];

    s3g::clap_gui::drawToggle(@"REC [C2 HOLD]", p->params.record,
        s3g::gui_layout::rowY(kCapturePanel, 0u), labels, values, style,
        s3g::gui_layout::processorLabelX(kCapturePanel.frame.x),
        s3g::gui_layout::processorControlX(kCapturePanel.frame.x),
        s3g::gui_layout::processorMenuWidth(kCapturePanel.frame.width));
    [self drawMenu:@"TARGET"
        value:[NSString stringWithUTF8String:targetName(
            static_cast<uint32_t>(p->params.recordTarget))]
        row:1u panel:kCapturePanel attrs:labels values:values style:style];
    constexpr const char* recordModeNames[] { "Replace", "Overdub", "Punch" };
    [self drawMenu:@"MODE"
        value:[NSString stringWithUTF8String:recordModeNames[
            static_cast<uint32_t>(p->params.recordMode)]]
        row:2u panel:kCapturePanel attrs:labels values:values style:style];
    [self drawMenu:@"MONITOR"
        value:[NSString stringWithUTF8String:monitorName(
            static_cast<uint32_t>(p->params.monitorMode))]
        row:3u panel:kCapturePanel attrs:labels values:values style:style];
    [self drawSlider:@"FEEDBACK" param:kOverdubFeedbackParamId row:4u
        panel:kCapturePanel attrs:labels values:values style:style];
    [@"ERASE" drawAtPoint:NSMakePoint(
        s3g::gui_layout::processorLabelX(kCapturePanel.frame.x),
        s3g::gui_layout::rowY(kCapturePanel, 5u) - 2.0)
        withAttributes:labels];
    s3g::clap_gui::drawHeaderActionButton(crcltrClearButtonRect(0u),
        crcltrClearButtonRect(0u), @"CLEAR A",
        s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x5f91a8), 11.0),
        style);
    s3g::clap_gui::drawHeaderActionButton(crcltrClearButtonRect(1u),
        crcltrClearButtonRect(1u), @"CLEAR B",
        s3g::clap_gui::textAttrs(s3g::clap_gui::color(0xb1845f), 11.0),
        style);

    [self drawMenu:@"MODEL"
        value:(p->params.playbackModel == s3g::CrcltrPlaybackModel::Dual
            ? @"DUAL SLOTS" : @"CRCLTR CLASSIC")
        row:0u panel:kLoopsPanel attrs:labels values:values style:style];
    [self drawSlider:@"A RATE" param:kLoop1RateParamId row:1u panel:kLoopsPanel
        attrs:labels values:values style:style];
    s3g::clap_gui::drawToggle(@"A REVERSE [D2]", p->params.loop1Reverse,
        s3g::gui_layout::rowY(kLoopsPanel, 2u), labels, values, style,
        s3g::gui_layout::processorLabelX(kLoopsPanel.frame.x),
        s3g::gui_layout::processorControlX(kLoopsPanel.frame.x),
        s3g::gui_layout::processorMenuWidth(kLoopsPanel.frame.width));
    [self drawSlider:@"A START" param:kLoop1StartParamId row:3u panel:kLoopsPanel
        attrs:labels values:values style:style];
    [self drawSlider:@"A END" param:kLoop1EndParamId row:4u panel:kLoopsPanel
        attrs:labels values:values style:style];
    [self drawMenu:@"A JOIN"
        value:(p->params.loop1Join == s3g::CrcltrLoopJoin::Seam
            ? @"SEAM" : @"DUCK")
        row:5u panel:kLoopsPanel attrs:labels values:values style:style];
    [self drawSlider:@"B RATE" param:kLoop2RateParamId row:6u panel:kLoopsPanel
        attrs:labels values:values style:style];
    s3g::clap_gui::drawToggle(@"B REVERSE [D#2]", p->params.loop2Reverse,
        s3g::gui_layout::rowY(kLoopsPanel, 7u), labels, values, style,
        s3g::gui_layout::processorLabelX(kLoopsPanel.frame.x),
        s3g::gui_layout::processorControlX(kLoopsPanel.frame.x),
        s3g::gui_layout::processorMenuWidth(kLoopsPanel.frame.width));
    [self drawSlider:@"B START" param:kLoop2StartParamId row:8u panel:kLoopsPanel
        attrs:labels values:values style:style];
    [self drawSlider:@"B END" param:kLoop2EndParamId row:9u panel:kLoopsPanel
        attrs:labels values:values style:style];
    [self drawMenu:@"B JOIN"
        value:(p->params.loop2Join == s3g::CrcltrLoopJoin::Seam
            ? @"SEAM" : @"DUCK")
        row:10u panel:kLoopsPanel attrs:labels values:values style:style];
    [self drawMenu:@"MOTION"
        value:[NSString stringWithUTF8String:crossfadeModeName(
            static_cast<uint32_t>(p->params.crossfadeMode))]
        row:0u panel:kCrossfadePanel attrs:labels values:values style:style];
    [self drawMenu:@"SHAPE"
        value:[NSString stringWithUTF8String:crossfadeShapeName(
            static_cast<uint32_t>(p->params.crossfadeShape))]
        row:1u panel:kCrossfadePanel attrs:labels values:values style:style];
    [self drawSlider:(p->params.crossfadeMode == s3g::CrcltrCrossfadeMode::Manual
            ? @"POSITION" : @"RATE")
        param:kCrossfadeParamId row:2u panel:kCrossfadePanel
        attrs:labels values:values style:style];

    [@"C2: hold to capture"
        drawAtPoint:NSMakePoint(kCapturePanel.frame.x + 16.0,
            kCapturePanel.frame.y + kCapturePanel.frame.height + 12.0)
        withAttributes:values];

    if (_openMenu > 0) {
        static NSString* xfadeItems[] = {
            @"MANUAL", @"SINE", @"TRAPEZOID", @"RANDOM WALK",
            @"TRIANGLE", @"RAMP A TO B", @"RAMP B TO A",
            @"SAMPLE & HOLD", @"SQUARE",
        };
        static NSString* shapeItems[] = {
            @"EQUAL POWER", @"LINEAR", @"WIDE", @"TIGHT",
            @"SMOOTH", @"FULL OVERLAP", @"DEEP DIP", @"PLATEAU", @"CUT",
        };
        static NSString* targetItems[] = { @"LOOP 1", @"BOTH", @"LOOP 2" };
        static NSString* monitorItems[] = {
            @"THRU LOOP 1", @"SILENT", @"THRU LOOP 2",
        };
        static NSString* recordItems[] = { @"REPLACE", @"OVERDUB", @"PUNCH" };
        static NSString* modelItems[] = { @"CRCLTR CLASSIC", @"DUAL SLOTS" };
        static NSString* joinItems[] = { @"SEAM", @"DUCK" };
        NSString** items = monitorItems;
        uint32_t itemCount = 3u;
        if (_openMenu == static_cast<int>(kCrossfadeModeParamId)) {
            items = xfadeItems;
            itemCount = 9u;
        } else if (_openMenu == static_cast<int>(kCrossfadeShapeParamId)) {
            items = shapeItems;
            itemCount = 9u;
        } else if (_openMenu == static_cast<int>(kRecordTargetParamId)) {
            items = targetItems;
        } else if (_openMenu == static_cast<int>(kRecordModeParamId)) {
            items = recordItems;
        } else if (_openMenu == static_cast<int>(kPlaybackModelParamId)) {
            items = modelItems;
            itemCount = 2u;
        } else if (_openMenu == static_cast<int>(kLoop1JoinParamId)
                   || _openMenu == static_cast<int>(kLoop2JoinParamId)) {
            items = joinItems;
            itemCount = 2u;
        }
        int selected = 0;
        if (_openMenu == static_cast<int>(kCrossfadeModeParamId)) {
            selected = static_cast<int>(p->params.crossfadeMode);
        } else if (_openMenu == static_cast<int>(kCrossfadeShapeParamId)) {
            selected = static_cast<int>(p->params.crossfadeShape);
        } else if (_openMenu == static_cast<int>(kRecordTargetParamId)) {
            selected = static_cast<int>(p->params.recordTarget);
        } else if (_openMenu == static_cast<int>(kRecordModeParamId)) {
            selected = static_cast<int>(p->params.recordMode);
        } else if (_openMenu == static_cast<int>(kPlaybackModelParamId)) {
            selected = static_cast<int>(p->params.playbackModel);
        } else if (_openMenu == static_cast<int>(kLoop1JoinParamId)) {
            selected = static_cast<int>(p->params.loop1Join);
        } else if (_openMenu == static_cast<int>(kLoop2JoinParamId)) {
            selected = static_cast<int>(p->params.loop2Join);
        } else {
            selected = static_cast<int>(p->params.monitorMode);
        }
        const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width),
            18.0 * itemCount);
        s3g::clap_gui::drawDropdownMenu(
            menu, 18.0, items, itemCount, selected, _hoverMenuItem, values, style);
    }
}

- (void)openMenuForParam:(clap_id)paramId
                    panel:(const s3g::gui_layout::Panel&)panel
                      row:(uint32_t)row
{
    const NSRect box = processorMenuRect(panel, row);
    _openMenu = static_cast<int>(paramId);
    _hoverMenuItem = -1;
    const CGFloat menuHeight = 18.0 * crcltrMenuItemCount(_openMenu);
    const CGFloat below = NSMaxY(box) + 3.0;
    const CGFloat above = NSMinY(box) - 3.0 - menuHeight;
    _menuOrigin = NSMakePoint(box.origin.x,
        below + menuHeight <= kGuiHeight - 4.0 ? below
            : std::max<CGFloat>(4.0, above));
    [self setNeedsDisplay:YES];
}

- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0) return;
    const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width),
        18.0 * crcltrMenuItemCount(_openMenu));
    const int hover = s3g::clap_gui::dropdownHitIndex(point, menu, 18.0,
        crcltrMenuItemCount(_openMenu));
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const ParamDef* def = p
        ? findParam(static_cast<clap_id>(_dragSlider)) : nullptr;
    if (!p || !def) return;
    const bool output = def->id == kInputGainParamId
        || def->id == kBlendParamId || def->id == kOutputGainParamId;
    const bool capture = def->id == kOverdubFeedbackParamId;
    const bool loop = def->id == kLoop1RateParamId
        || def->id == kLoop2RateParamId || def->id == kLoop1StartParamId
        || def->id == kLoop1EndParamId || def->id == kLoop2StartParamId
        || def->id == kLoop2EndParamId;
    const auto& panel = output ? kOutputPanel
        : capture ? kCapturePanel : loop ? kLoopsPanel : kCrossfadePanel;
    const double controlX =
        s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double norm = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    applyParam(*p, def->id,
        def->minimum + norm * (def->maximum - def->minimum));
    [self setNeedsDisplay:YES];
}

- (void)updateWaveMarker:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p || _dragWaveMarker <= 0) return;
    const clap_id marker = static_cast<clap_id>(_dragWaveMarker);
    const bool loop1 = marker == kLoop1StartParamId
        || marker == kLoop1EndParamId;
    const bool startMarker = marker == kLoop1StartParamId
        || marker == kLoop2StartParamId;
    const uint32_t loop = loop1 ? 0u : 1u;
    const double editableWidth = kCrcltrWaveWidth
        * crcltrWaveSourceScale(*p, loop);
    double value = std::clamp(
        (point.x - kCrcltrWaveX) / std::max(1.0, editableWidth),
        0.0, 1.0);
    constexpr double minimumSpan = 0.001;
    if (startMarker) {
        value = std::min(value,
            static_cast<double>(crcltrLoopEnd(*p, loop)) - minimumSpan);
    } else {
        value = std::max(value,
            static_cast<double>(crcltrLoopStart(*p, loop)) + minimumSpan);
    }
    queueGuiParamValue(*p, marker, value);
    [self setNeedsDisplay:YES];
}

- (void)updateCrossfadeGraph:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p || !_dragCrossfadeGraph
        || p->params.crossfadeMode != s3g::CrcltrCrossfadeMode::Manual)
        return;
    const NSRect graph = crcltrCrossfadeGraphRect();
    const double value = std::clamp(
        (point.y - NSMinY(graph)) / std::max<CGFloat>(1.0, NSHeight(graph)),
        0.0, 1.0);
    queueGuiParamValue(*p, kCrossfadeParamId, value);
    [self setNeedsDisplay:YES];
}

- (void)loadLoop:(uint32_t)loop
{
    if (!_plugin || loop >= 2u) return;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setTitle:loop == 0u ? @"Load Loop A" : @"Load Loop B"];
    if ([panel runModal] != NSModalResponseOK || ![panel URL]) return;
    const char* path = [[[panel URL] path] fileSystemRepresentation];
    if (!path) return;
    queueLoopLoad(*static_cast<Plugin*>(_plugin), loop, path);
    [self setNeedsDisplay:YES];
}

- (BOOL)loadDocumentationSample
{
    if (!_plugin) return NO;
    const char* path = std::getenv("S3G_GUI_DOCUMENTATION_SAMPLE_PATH");
    if (!path || !path[0]) return NO;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    const double destinationSampleRate = plugin.prepared
        ? plugin.dsp.sampleRate() : plugin.sampleRate;
    LoopLoadRequest request;
    request.destinationSampleRate = destinationSampleRate;
    request.destinationCapacity = s3g::Crcltr::requiredLoopCapacity(
        destinationSampleRate);
    request.path = path;
    std::shared_ptr<const ImportedLoopAudio> audio;
    std::string error;
    if (!decodeLoopSample(request, audio, error) || !audio) return NO;
    bool loaded = true;
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        const uint64_t generation = plugin.loopImportGenerations[loop]
            .fetch_add(1u, std::memory_order_acq_rel) + 1u;
        loaded = installDecodedLoop(plugin, loop, generation, audio) && loaded;
    }
    applyParam(plugin, kBlendParamId, 1.0);
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
    return loaded ? YES : NO;
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
    if (!_plugin) return NO;
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[ [NSURL class] ] options:@{
            NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
    if (urls.count == 0u) return NO;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    if (urls.count >= 2u) {
        for (uint32_t loop = 0u; loop < 2u; ++loop) {
            const char* path = [[urls[loop] path] fileSystemRepresentation];
            if (path) queueLoopLoad(plugin, loop, path);
        }
    } else {
        const NSPoint point = [self convertPoint:[sender draggingLocation]
            fromView:nil];
        const uint32_t loop = point.y >= kCrcltrWaveContentY
                + kCrcltrWaveLaneHeight
            ? 1u : 0u;
        const char* path = [[urls[0u] path] fileSystemRepresentation];
        if (!path) return NO;
        queueLoopLoad(plugin, loop, path);
    }
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;

    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        if (!NSPointInRect(point, crcltrLoadButtonRect(loop))) continue;
        [self loadLoop:loop];
        return;
    }

    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        kCrcltrCanvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            point, &p->plugin, @"Sample Circulator", titleBand,
            _titlePresetName, sizeof(_titlePresetName),
            kOutputGainParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }

    if (_openMenu > 0) {
        const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width),
            18.0 * crcltrMenuItemCount(_openMenu));
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, menu, 18.0, crcltrMenuItemCount(_openMenu));
        if (hit >= 0) {
            applyParam(*p, static_cast<clap_id>(_openMenu),
                static_cast<double>(hit));
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    if (p->params.crossfadeMode == s3g::CrcltrCrossfadeMode::Manual
        && NSPointInRect(point,
            NSInsetRect(crcltrCrossfadeGraphRect(), -5.0, -3.0))) {
        _dragCrossfadeGraph = true;
        queueGuiParamGestureBegin(*p, kCrossfadeParamId);
        [self updateCrossfadeGraph:point];
        return;
    }

    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        const NSRect wave = NSInsetRect(crcltrWaveRect(loop), -12.0, 0.0);
        if (!NSPointInRect(point, wave)) continue;
        const CGFloat scale = crcltrWaveSourceScale(*p, loop);
        const CGFloat startX = kCrcltrWaveX + kCrcltrWaveWidth * scale
            * crcltrLoopStart(*p, loop);
        const CGFloat endX = kCrcltrWaveX + kCrcltrWaveWidth * scale
            * crcltrLoopEnd(*p, loop);
        const CGFloat startDistance = std::abs(point.x - startX);
        const CGFloat endDistance = std::abs(point.x - endX);
        if (std::min(startDistance, endDistance) > 12.0) continue;
        const bool startMarker = startDistance <= endDistance;
        const clap_id marker = loop == 0u
            ? (startMarker ? kLoop1StartParamId : kLoop1EndParamId)
            : (startMarker ? kLoop2StartParamId : kLoop2EndParamId);
        _dragWaveMarker = static_cast<int>(marker);
        queueGuiParamGestureBegin(*p, marker);
        [self updateWaveMarker:point];
        return;
    }

    if (NSPointInRect(point, processorMenuRect(kCrossfadePanel, 0u))) {
        [self openMenuForParam:kCrossfadeModeParamId
            panel:kCrossfadePanel row:0u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kCrossfadePanel, 1u))) {
        [self openMenuForParam:kCrossfadeShapeParamId
            panel:kCrossfadePanel row:1u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kCapturePanel, 1u))) {
        [self openMenuForParam:kRecordTargetParamId
            panel:kCapturePanel row:1u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kCapturePanel, 2u))) {
        [self openMenuForParam:kRecordModeParamId
            panel:kCapturePanel row:2u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kCapturePanel, 3u))) {
        [self openMenuForParam:kMonitorModeParamId
            panel:kCapturePanel row:3u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kLoopsPanel, 0u))) {
        [self openMenuForParam:kPlaybackModelParamId panel:kLoopsPanel row:0u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kLoopsPanel, 5u))) {
        [self openMenuForParam:kLoop1JoinParamId panel:kLoopsPanel row:5u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kLoopsPanel, 10u))) {
        [self openMenuForParam:kLoop2JoinParamId panel:kLoopsPanel row:10u];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kOutputPanel, 0u)))) {
        applyParam(*p, kPlayingParamId, p->params.playing ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kCapturePanel, 0u)))) {
        applyParam(*p, kRecordParamId, 1.0);
        _recordHeld = true;
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t loop = 0u; loop < 2u; ++loop) {
        if (!NSPointInRect(point, NSInsetRect(
                crcltrClearButtonRect(loop), -2.0, -3.0))) continue;
        const clap_id clearParam = loop == 0u
            ? kClearLoopAParamId : kClearLoopBParamId;
        queueGuiParamGestureBegin(*p, clearParam);
        queueGuiParamValue(*p, clearParam, 1.0);
        queueGuiParamValue(*p, clearParam, 0.0);
        queueGuiParamGestureEnd(*p, clearParam);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kLoopsPanel, 2u)))) {
        applyParam(*p, kLoop1ReverseParamId,
            p->params.loop1Reverse ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kLoopsPanel, 7u)))) {
        applyParam(*p, kLoop2ReverseParamId,
            p->params.loop2Reverse ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }

    const struct {
        clap_id paramId;
        const s3g::gui_layout::Panel* panel;
        uint32_t row;
    } sliders[] {
        { kInputGainParamId, &kOutputPanel, 1u },
        { kBlendParamId, &kOutputPanel, 2u },
        { kOutputGainParamId, &kOutputPanel, 3u },
        { kOverdubFeedbackParamId, &kCapturePanel, 4u },
        { kLoop1RateParamId, &kLoopsPanel, 1u },
        { kLoop1StartParamId, &kLoopsPanel, 3u },
        { kLoop1EndParamId, &kLoopsPanel, 4u },
        { kLoop2RateParamId, &kLoopsPanel, 6u },
        { kLoop2StartParamId, &kLoopsPanel, 8u },
        { kLoop2EndParamId, &kLoopsPanel, 9u },
        { kCrossfadeParamId, &kCrossfadePanel, 2u },
    };
    for (const auto& slider : sliders) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(
                    *slider.panel, slider.row)))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, slider.paramId, &defaultValue)) {
            applyParam(*p, slider.paramId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(slider.paramId);
            [self updateSlider:point];
        }
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:
        [self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragWaveMarker > 0) {
        [self updateWaveMarker:point];
        return;
    }
    if (_dragCrossfadeGraph) {
        [self updateCrossfadeGraph:point];
        return;
    }
    [self updateMenuHover:point];
    if (_dragSlider > 0) [self updateSlider:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_recordHeld && _plugin) {
        applyParam(*static_cast<Plugin*>(_plugin), kRecordParamId, 0.0);
        _recordHeld = false;
        [self setNeedsDisplay:YES];
    }
    if (_dragWaveMarker > 0 && _plugin) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragWaveMarker));
        _dragWaveMarker = -1;
    }
    if (_dragCrossfadeGraph && _plugin) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            kCrossfadeParamId);
        _dragCrossfadeGraph = false;
    }
    _dragSlider = -1;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GCrcltrView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(
            p->guiViewport, static_cast<NSView*>(p->guiView),
            kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p || !p->guiView) return;
    p->guiVisible = false;
    [static_cast<S3GCrcltrView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
                   uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
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
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        p->guiViewport, static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GCrcltrView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GCrcltrView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t guiExtension {
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
namespace {
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0) return &noteNames;
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
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.crcltr",
    "s3g Sample Circulator 2",
    "s3g",
    "https://github.com/s3g/crcltr",
    "",
    "",
    "0.4.0",
    "File-or-capture stereo dual-loop sampler founded on the CRCLTR DSP core.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
                                  const clap_host_t* host,
                                  const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->params.playbackModel = s3g::CrcltrPlaybackModel::Dual;
    p->host = host;
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

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
