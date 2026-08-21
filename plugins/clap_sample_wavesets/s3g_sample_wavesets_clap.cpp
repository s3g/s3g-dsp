#include "s3g_sample_wavesets.h"
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

using s3g::sample::SampleAsset;
using s3g::sample::SampleWavesetsEngine;
using s3g::sample::WavesetAdvanceMode;
using s3g::sample::WavesetCrossfadeCurve;
using s3g::sample::WavesetCrossingDetail;
using s3g::sample::WavesetDirection;
using s3g::sample::WavesetEventKind;
using s3g::sample::WavesetMap;
using s3g::sample::WavesetRenderEvent;
using s3g::sample::WavesetSettings;
using s3g::sample::WavesetShape;
using s3g::sample::WavesetUnit;

constexpr uint32_t kStateMagic = 0x57533353u; // "S3SW"
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kGuiWidth = 1040u;
constexpr uint32_t kGuiHeight = 820u;
constexpr std::size_t kMaximumPathBytes = 1024u;
constexpr std::size_t kMaximumBlockEvents = 2048u;
constexpr uint64_t kMaximumEmbeddedAudioBytes
    = 1024ull * 1024ull * 1024ull;
constexpr clap_id kStereoOutputConfigId = 3402u;

constexpr clap_id kOutParamId = 1u;
constexpr clap_id kCrossfaderParamId = 2u;
constexpr clap_id kCurveParamId = 3u;
constexpr clap_id kAdvanceParamId = 4u;
constexpr clap_id kGroupParamId = 5u;
constexpr clap_id kRepeatParamId = 6u;
constexpr clap_id kStrideParamId = 7u;
constexpr clap_id kDirectionParamId = 8u;
constexpr clap_id kShapeParamId = 9u;
constexpr clap_id kProcessParamId = 10u;
constexpr clap_id kJoinParamId = 11u;
constexpr clap_id kDetailParamId = 12u;
constexpr clap_id kMidiChannelParamId = 13u;
constexpr clap_id kLinkParamId = 14u;
constexpr clap_id kPositionAParamId = 15u;
constexpr clap_id kRateAParamId = 16u;
constexpr clap_id kLevelAParamId = 17u;
constexpr clap_id kPositionBParamId = 18u;
constexpr clap_id kRateBParamId = 19u;
constexpr clap_id kLevelBParamId = 20u;
constexpr std::size_t kParamCount = 20u;

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
    { kCrossfaderParamId, "Crossfader", "Mixer", -1.0, 1.0, -1.0,
        false },
    { kCurveParamId, "Crossfader Curve", "Mixer", 0.0, 2.0, 2.0, true },
    { kAdvanceParamId, "Time", "Wavesets", 0.0, 2.0, 0.0, true },
    { kGroupParamId, "Group", "Wavesets", 0.0, 5.0, 3.0, true },
    { kRepeatParamId, "Repeat", "Wavesets", 1.0, 16.0, 2.0, true },
    { kStrideParamId, "Stride", "Wavesets", -16.0, 16.0, 1.0, true },
    { kDirectionParamId, "Order", "Wavesets", 0.0, 3.0, 0.0, true },
    { kShapeParamId, "Process", "Process", 0.0, 4.0, 0.0, true },
    { kProcessParamId, "Depth", "Process", 0.0, 1.0, 0.0, false },
    { kJoinParamId, "Join", "Process", 0.0, 1.0, 1.0, false },
    { kDetailParamId, "Crossing Detail", "Analysis", 0.0, 4.0, 3.0, true },
    { kMidiChannelParamId, "MIDI Channel", "MIDI", 1.0, 16.0, 1.0, true },
    { kLinkParamId, "Link Decks", "Transport", 0.0, 1.0, 1.0, true },
    { kPositionAParamId, "Deck A Position", "Deck A", 0.0, 1.0, 0.0, false },
    { kRateAParamId, "Deck A Speed", "Deck A", 0.25, 4.0, 1.0, false },
    { kLevelAParamId, "Deck A Level", "Deck A", -60.0, 12.0, 0.0, false },
    { kPositionBParamId, "Deck B Position", "Deck B", 0.0, 1.0, 0.5, false },
    { kRateBParamId, "Deck B Speed", "Deck B", 0.25, 4.0, 1.0, false },
    { kLevelBParamId, "Deck B Level", "Deck B", -60.0, 12.0, 0.0, false },
}};

constexpr std::array<uint32_t, 6u> kGroupSizes {{ 1u, 2u, 4u, 8u, 16u, 32u }};
constexpr std::array<clap_id, 2u> kPositionParamIds {{
    kPositionAParamId, kPositionBParamId,
}};
constexpr std::array<clap_id, 2u> kRateParamIds {{
    kRateAParamId, kRateBParamId,
}};
constexpr std::array<clap_id, 2u> kLevelParamIds {{
    kLevelAParamId, kLevelBParamId,
}};

constexpr uint32_t kActionRestartBoth = 1u << 0u;
constexpr uint32_t kActionStopBoth = 1u << 1u;
constexpr uint32_t kActionPlayBoth = 1u << 2u;
constexpr uint32_t kActionPauseBoth = 1u << 3u;
constexpr uint32_t kActionToggleA = 1u << 4u;
constexpr uint32_t kActionToggleB = 1u << 5u;
constexpr uint32_t kActionRestartA = 1u << 6u;
constexpr uint32_t kActionRestartB = 1u << 7u;
constexpr uint32_t kActionStopA = 1u << 8u;
constexpr uint32_t kActionStopB = 1u << 9u;

struct SavedState {
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
};

#if defined(__APPLE__)
struct LoadRequest {
    uint64_t generation = 0u;
    std::string path;
    std::shared_ptr<const SampleAsset> asset;
    WavesetCrossingDetail detail = WavesetCrossingDetail::Raw;
};

struct LoadResult {
    uint64_t generation = 0u;
    std::string path;
    std::shared_ptr<const SampleAsset> asset;
    std::shared_ptr<const WavesetMap> map;
    std::string error;
};
#endif

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_state_t* hostState = nullptr;
    SampleWavesetsEngine engine;
    double sampleRate = 48000.0;
    uint32_t maximumFrames = 0u;
    std::array<std::atomic<double>, kParamCount> parameters {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic_flag guiParamConsumer = ATOMIC_FLAG_INIT;
    std::array<WavesetRenderEvent, kMaximumBlockEvents> blockEvents {};
    std::array<std::vector<float>, 2u> scratch {};
    std::shared_ptr<const WavesetMap> controlMap;
    std::vector<std::shared_ptr<const WavesetMap>> retainedMaps;
    std::atomic<const WavesetMap*> publishedMap { nullptr };
    const WavesetMap* audioMap = nullptr;
    std::string samplePath;
    std::string status { "DROP A MONO OR STEREO SAMPLE" };
    std::array<std::atomic<float>, 2u> headPositions {{}};
    std::array<std::atomic<float>, 2u> outputPhases {{}};
    std::atomic<uint8_t> headActiveMask { 0u };
    std::atomic<uint8_t> deckPlayingMask { 0u };
    std::atomic<uint32_t> requestedActions { 0u };
    std::atomic<uint32_t> actionFeedback { 0u };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> processing { false };
    std::atomic<uint64_t> cursorRevision { 0u };
    std::atomic<bool> analysisDirty { false };
    bool embedSampleInState = true;
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
    for (const auto& def : kParamDefs)
        if (def.id == id) return &def;
    return nullptr;
}

std::size_t paramIndex(clap_id id) noexcept
{
    return id >= kOutParamId && id <= kLevelBParamId
        ? static_cast<std::size_t>(id - kOutParamId) : kParamCount;
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
    if (instance.host && instance.hostState && instance.hostState->mark_dirty)
        instance.hostState->mark_dirty(instance.host);
}

std::string sampleDisplayName(const std::string& path)
{
    if (path.empty()) return "EMBEDDED SAMPLE";
    const std::size_t separator = path.find_last_of("/\\");
    return separator == std::string::npos ? path : path.substr(separator + 1u);
}

void setParam(Plugin& instance, clap_id id, double value,
    bool dirty = false) noexcept
{
    const ParamDef* def = paramDef(id);
    const std::size_t index = paramIndex(id);
    if (!def || index >= instance.parameters.size()) return;
    value = clampParam(*def, value);
    const double previous = instance.parameters[index].exchange(value,
        std::memory_order_acq_rel);
    const bool cursorContract = id == kAdvanceParamId || id == kGroupParamId
        || id == kRepeatParamId || id == kStrideParamId
        || id == kPositionAParamId || id == kPositionBParamId
        || id == kRateAParamId || id == kRateBParamId;
    if (cursorContract && previous != value)
        instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    if (id == kDetailParamId && previous != value) {
        instance.analysisDirty.store(true, std::memory_order_release);
        if (instance.host && instance.host->request_callback)
            instance.host->request_callback(instance.host);
    }
    if (dirty && previous != value) markStateDirty(instance);
}

void initializeParams(Plugin& instance) noexcept
{
    for (const auto& def : kParamDefs)
        setParam(instance, def.id, def.defaultValue, false);
    instance.analysisDirty.store(false, std::memory_order_release);
}

WavesetCrossingDetail crossingDetail(const Plugin& instance) noexcept
{
    return static_cast<WavesetCrossingDetail>(static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::lround(
            paramValue(instance, kDetailParamId))), 0, 4)));
}

WavesetSettings settingsSnapshot(const Plugin& instance) noexcept
{
    WavesetSettings settings;
    settings.outputGainDecibels = static_cast<float>(paramValue(instance,
        kOutParamId));
    settings.crossfader = paramValue(instance, kCrossfaderParamId);
    settings.crossfadeCurve = static_cast<WavesetCrossfadeCurve>(
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(
            paramValue(instance, kCurveParamId))), 0, 2)));
    settings.advance = static_cast<WavesetAdvanceMode>(
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(
            paramValue(instance, kAdvanceParamId))), 0, 2)));
    settings.groupSize = kGroupSizes[static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::lround(paramValue(instance, kGroupParamId))),
        0, 5))];
    settings.repeats = static_cast<uint32_t>(std::clamp(
        static_cast<int>(std::lround(paramValue(instance, kRepeatParamId))),
        1, 16));
    settings.stride = static_cast<int32_t>(std::clamp(
        static_cast<int>(std::lround(paramValue(instance, kStrideParamId))),
        -16, 16));
    settings.direction = static_cast<WavesetDirection>(
        static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(
            paramValue(instance, kDirectionParamId))), 0, 3)));
    settings.shape = static_cast<WavesetShape>(static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::lround(
            paramValue(instance, kShapeParamId))), 0, 4)));
    settings.processAmount = static_cast<float>(paramValue(instance,
        kProcessParamId));
    settings.joinAmount = static_cast<float>(paramValue(instance,
        kJoinParamId));
    for (std::size_t index = 0u; index < 2u; ++index) {
        settings.positions[index] = paramValue(instance,
            kPositionParamIds[index]);
        settings.scanRates[index] = paramValue(instance,
            kRateParamIds[index]);
        settings.levelsDecibels[index] = static_cast<float>(paramValue(
            instance, kLevelParamIds[index]));
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
    setParam(instance, id, events[1u].value);
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
    setParam(instance, id, value);
    requestGuiParamService(instance);
}

void queueGuiParamEnd(Plugin& instance, clap_id id)
{
    if (instance.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 }))
        requestGuiParamService(instance);
}

bool publishMap(Plugin& instance, std::shared_ptr<const WavesetMap> map,
    std::string path, bool dirty = true)
{
    if (map && !map->valid()) return false;
    if (map) instance.retainedMaps.push_back(map);
    instance.controlMap = std::move(map);
    instance.samplePath = std::move(path);
    instance.publishedMap.store(instance.controlMap.get(),
        std::memory_order_release);
    for (auto& position : instance.headPositions)
        position.store(-1.0f, std::memory_order_relaxed);
    for (auto& phase : instance.outputPhases)
        phase.store(0.0f, std::memory_order_relaxed);
    instance.headActiveMask.store(0u, std::memory_order_release);
    instance.deckPlayingMask.store(0u, std::memory_order_release);
    instance.outputPeak.store(0.0f, std::memory_order_relaxed);
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
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
        if (!file) { error = "COULD NOT OPEN SAMPLE"; return false; }
        AVAudioFormat* format = [file processingFormat];
        const AVAudioChannelCount channels = [format channelCount];
        const AVAudioFramePosition fileFrames = [file length];
        if (channels < 1u || channels > 2u || fileFrames < 8
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
        if (!asset->valid()) { error = "INVALID DECODED SAMPLE"; return false; }
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
        result.path = request.path;
        result.asset = std::move(request.asset);
        if (!result.asset)
            decodeSampleFile(result.path, result.asset, result.error);
        if (result.asset) {
            result.map = s3g::sample::analyzeWavesets(result.asset,
                request.detail);
            if (!result.map) result.error = "NO USABLE WAVESETS FOUND";
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

void queueSampleLoad(Plugin& instance, std::string path)
{
    LoadRequest request;
    request.generation = ++instance.loadGeneration;
    request.path = std::move(path);
    request.detail = crossingDetail(instance);
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
        instance.loadRequests.push_back(std::move(request));
    }
    instance.status = "DECODING AND ANALYZING WAVESETS...";
    instance.loaderCondition.notify_one();
}

void queueReanalysis(Plugin& instance)
{
    if (!instance.controlMap || !instance.controlMap->asset) return;
    LoadRequest request;
    request.generation = ++instance.loadGeneration;
    request.path = instance.samplePath;
    request.asset = instance.controlMap->asset;
    request.detail = crossingDetail(instance);
    {
        std::lock_guard<std::mutex> lock(instance.loaderMutex);
        instance.loadRequests.clear();
        instance.loadRequests.push_back(std::move(request));
    }
    instance.status = "REANALYZING CROSSINGS...";
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
        if (result.generation != instance.loadGeneration) continue;
        if (!result.map) {
            instance.status = result.error.empty()
                ? "WAVESET ANALYSIS FAILED" : result.error;
            continue;
        }
        const std::size_t count = result.map->units.size();
        publishMap(instance, std::move(result.map),
            std::move(result.path));
        instance.status = std::to_string(count) + " WAVESETS / "
            + sampleDisplayName(instance.samplePath);
    }
}
#endif

void appendEvent(Plugin& instance, std::size_t& count,
    uint32_t frame, WavesetEventKind kind, uint8_t deck = 0u,
    float value = 1.0f) noexcept
{
    if (count >= instance.blockEvents.size()) return;
    instance.blockEvents[count++] = { frame, kind, deck, value };
}

void requestAction(Plugin& instance, uint32_t action) noexcept
{
    instance.requestedActions.fetch_or(action, std::memory_order_release);
    instance.actionFeedback.fetch_or(action, std::memory_order_release);
    requestProcess(instance);
}

void appendRequestedActions(Plugin& instance, std::size_t& count) noexcept
{
    const uint32_t actions = instance.requestedActions.exchange(
        0u, std::memory_order_acq_rel);
    if ((actions & kActionRestartBoth) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::RestartBoth);
    if ((actions & kActionStopBoth) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::StopBoth);
    if ((actions & kActionPlayBoth) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::PlayBoth);
    if ((actions & kActionPauseBoth) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::PauseBoth);
    if ((actions & kActionRestartA) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::RestartDeck, 0u);
    if ((actions & kActionRestartB) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::RestartDeck, 1u);
    if ((actions & kActionStopA) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::StopDeck, 0u);
    if ((actions & kActionStopB) != 0u)
        appendEvent(instance, count, 0u, WavesetEventKind::StopDeck, 1u);
    const uint8_t playing = instance.deckPlayingMask.load(
        std::memory_order_acquire);
    const bool linked = paramValue(instance, kLinkParamId) >= 0.5;
    if ((actions & (kActionToggleA | kActionToggleB)) != 0u && linked) {
        appendEvent(instance, count, 0u,
            playing == 3u ? WavesetEventKind::PauseBoth
                          : WavesetEventKind::PlayBoth);
    } else {
        if ((actions & kActionToggleA) != 0u)
            appendEvent(instance, count, 0u,
                (playing & 1u) != 0u ? WavesetEventKind::PauseDeck
                                     : WavesetEventKind::PlayDeck, 0u);
        if ((actions & kActionToggleB) != 0u)
            appendEvent(instance, count, 0u,
                (playing & 2u) != 0u ? WavesetEventKind::PauseDeck
                                     : WavesetEventKind::PlayDeck, 1u);
    }
}

void handleMidi(Plugin& instance, std::size_t& count, uint32_t frame,
    uint8_t channel, uint8_t status, uint8_t data1, uint8_t data2) noexcept
{
    const int receiveChannel = static_cast<int>(std::lround(paramValue(
        instance, kMidiChannelParamId))) - 1;
    if (static_cast<int>(channel) != receiveChannel) return;
    if (status == 0x90u && data2 != 0u) {
        uint32_t feedback = 0u;
        switch (data1) {
        case 36u: feedback = kActionRestartBoth; appendEvent(instance, count, frame,
            WavesetEventKind::RestartBoth); break;
        case 37u: feedback = kActionStopBoth; appendEvent(instance, count, frame,
            WavesetEventKind::StopBoth); break;
        case 38u: feedback = kActionPlayBoth; appendEvent(instance, count, frame,
            WavesetEventKind::PlayBoth); break;
        case 39u: feedback = kActionPauseBoth; appendEvent(instance, count, frame,
            WavesetEventKind::PauseBoth); break;
        case 40u: {
            feedback = kActionToggleA;
            const uint8_t playing = instance.deckPlayingMask.load(
                std::memory_order_acquire);
            const bool linked = paramValue(instance, kLinkParamId) >= 0.5;
            appendEvent(instance, count, frame,
                linked
                    ? (playing == 3u ? WavesetEventKind::PauseBoth
                                     : WavesetEventKind::PlayBoth)
                    : ((playing & 1u) != 0u
                        ? WavesetEventKind::PauseDeck
                        : WavesetEventKind::PlayDeck),
                0u);
            break;
        }
        case 41u: {
            feedback = kActionToggleB;
            const uint8_t playing = instance.deckPlayingMask.load(
                std::memory_order_acquire);
            const bool linked = paramValue(instance, kLinkParamId) >= 0.5;
            appendEvent(instance, count, frame,
                linked
                    ? (playing == 3u ? WavesetEventKind::PauseBoth
                                     : WavesetEventKind::PlayBoth)
                    : ((playing & 2u) != 0u
                        ? WavesetEventKind::PauseDeck
                        : WavesetEventKind::PlayDeck),
                1u);
            break;
        }
        case 42u: feedback = kActionRestartA; appendEvent(instance, count, frame,
            WavesetEventKind::RestartDeck, 0u); break;
        case 43u: feedback = kActionRestartB; appendEvent(instance, count, frame,
            WavesetEventKind::RestartDeck, 1u); break;
        case 44u: feedback = kActionStopA; appendEvent(instance, count, frame,
            WavesetEventKind::StopDeck, 0u); break;
        case 45u: feedback = kActionStopB; appendEvent(instance, count, frame,
            WavesetEventKind::StopDeck, 1u); break;
        case 46u: setParam(instance, kCrossfaderParamId, -1.0); break;
        case 47u: setParam(instance, kCrossfaderParamId, 0.0); break;
        case 48u: setParam(instance, kCrossfaderParamId, 1.0); break;
        default: break;
        }
        if (feedback != 0u)
            instance.actionFeedback.fetch_or(feedback,
                std::memory_order_release);
    } else if (status == 0xb0u) {
        const double normalized = static_cast<double>(data2) / 127.0;
        if (data1 == 1u || data1 == 16u)
            setParam(instance, kCrossfaderParamId,
                normalized * 2.0 - 1.0);
        else if (data1 >= 17u && data1 <= 18u)
            setParam(instance, kPositionParamIds[data1 - 17u], normalized);
        else if (data1 >= 19u && data1 <= 20u)
            setParam(instance, kRateParamIds[data1 - 19u],
                0.25 + normalized * 3.75);
        else if (data1 == 21u)
            setParam(instance, kGroupParamId, normalized * 5.0);
        else if (data1 == 22u)
            setParam(instance, kProcessParamId, normalized);
        else if (data1 == 123u) {
            appendEvent(instance, count, frame, WavesetEventKind::StopBoth);
        }
    }
}

void readInputEvents(Plugin& instance, const clap_input_events_t* events,
    uint32_t frameCount, std::size_t& eventCount) noexcept
{
    if (!events || !events->size || !events->get) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        const uint32_t frame = std::min(header->time, frameCount);
        if (header->type == CLAP_EVENT_PARAM_VALUE
            && header->size >= sizeof(clap_event_param_value_t)) {
            const auto* event = reinterpret_cast<
                const clap_event_param_value_t*>(header);
            setParam(instance, event->param_id, event->value);
            continue;
        }
        if ((header->type == CLAP_EVENT_NOTE_ON
                || header->type == CLAP_EVENT_NOTE_OFF)
            && header->size >= sizeof(clap_event_note_t)) {
            const auto* event = reinterpret_cast<const clap_event_note_t*>(
                header);
            const uint8_t status = header->type == CLAP_EVENT_NOTE_ON
                && event->velocity > 0.0 ? 0x90u : 0x80u;
            handleMidi(instance, eventCount, frame,
                static_cast<uint8_t>(std::clamp<int>(event->channel, 0, 15)),
                status, static_cast<uint8_t>(std::clamp<int>(event->key, 0, 127)),
                static_cast<uint8_t>(std::clamp(event->velocity, 0.0, 1.0)
                    * 127.0));
            continue;
        }
        if (header->type == CLAP_EVENT_MIDI
            && header->size >= sizeof(clap_event_midi_t)) {
            const auto* event = reinterpret_cast<const clap_event_midi_t*>(
                header);
            handleMidi(instance, eventCount, frame,
                event->data[0u] & 0x0fu, event->data[0u] & 0xf0u,
                event->data[1u], event->data[2u]);
        }
    }
}

bool pluginInit(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    initializeParams(instance);
    instance.retainedMaps.reserve(32u);
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

#if defined(__APPLE__)
void destroyGui(Plugin& instance);
#endif

void pluginDestroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto& instance = *self(plugin);
    destroyGui(instance);
    stopLoader(instance);
#endif
    delete self(plugin);
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maximumFrames)
{
    auto& instance = *self(plugin);
    if (!(sampleRate > 0.0) || maximumFrames == 0u) return false;
    instance.sampleRate = sampleRate;
    instance.maximumFrames = maximumFrames;
    instance.engine.prepare(sampleRate, maximumFrames);
    for (auto& channel : instance.scratch)
        channel.assign(maximumFrames, 0.0f);
    instance.audioMap = instance.publishedMap.load(std::memory_order_acquire);
    instance.engine.setMap(instance.audioMap);
    for (auto& position : instance.headPositions)
        position.store(-1.0f, std::memory_order_relaxed);
    for (auto& phase : instance.outputPhases)
        phase.store(0.0f, std::memory_order_relaxed);
    instance.requestedActions.store(0u, std::memory_order_relaxed);
    instance.active = true;
    return true;
}

void pluginDeactivate(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.processing.store(false, std::memory_order_release);
    instance.active = false;
    instance.engine.setMap(nullptr);
    instance.audioMap = nullptr;
    for (auto& channel : instance.scratch) channel.clear();
    instance.headActiveMask.store(0u, std::memory_order_release);
    instance.deckPlayingMask.store(0u, std::memory_order_release);
    for (auto& position : instance.headPositions)
        position.store(-1.0f, std::memory_order_relaxed);
    for (auto& phase : instance.outputPhases)
        phase.store(0.0f, std::memory_order_relaxed);
    instance.outputPeak.store(0.0f, std::memory_order_relaxed);
    instance.retainedMaps.clear();
    if (instance.controlMap) instance.retainedMaps.push_back(instance.controlMap);
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
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
    auto& instance = *self(plugin);
    instance.processing.store(false, std::memory_order_release);
    instance.headActiveMask.store(0u, std::memory_order_release);
    instance.deckPlayingMask.store(0u, std::memory_order_release);
    instance.outputPeak.store(0.0f, std::memory_order_relaxed);
}

void pluginReset(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    instance.engine.reset();
    for (auto& position : instance.headPositions)
        position.store(-1.0f, std::memory_order_relaxed);
    for (auto& phase : instance.outputPhases)
        phase.store(0.0f, std::memory_order_relaxed);
    instance.headActiveMask.store(0u, std::memory_order_release);
    instance.deckPlayingMask.store(0u, std::memory_order_release);
    instance.outputPeak.store(0.0f, std::memory_order_relaxed);
    instance.cursorRevision.fetch_add(1u, std::memory_order_release);
}

clap_process_status pluginProcess(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    if (!process) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    if (process->frames_count > instance.maximumFrames)
        return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(instance, process->out_events);
    const WavesetMap* next = instance.publishedMap.load(
        std::memory_order_acquire);
    if (next != instance.audioMap) {
        instance.audioMap = next;
        instance.engine.setMap(next);
        instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    }
    std::size_t eventCount = 0u;
    appendRequestedActions(instance, eventCount);
    readInputEvents(instance, process->in_events, process->frames_count,
        eventCount);
    const WavesetSettings settings = settingsSnapshot(instance);
    std::array<float*, 2u> scratch {{
        instance.scratch[0u].data(), instance.scratch[1u].data(),
    }};
    instance.engine.render(settings, instance.blockEvents.data(), eventCount,
        scratch.data(), 2u, process->frames_count);
    const uint8_t previousMask = instance.headActiveMask.load(
        std::memory_order_relaxed);
    const uint8_t previousPlaying = instance.deckPlayingMask.load(
        std::memory_order_relaxed);
    const uint8_t activeMask = instance.engine.activeMask();
    uint8_t playingMask = 0u;
    for (std::size_t index = 0u; index < 2u; ++index) {
        instance.headPositions[index].store(
            instance.engine.deckPositionNormalized(index),
            std::memory_order_relaxed);
        instance.outputPhases[index].store(
            instance.engine.deckOutputPhaseNormalized(index),
            std::memory_order_relaxed);
        if (instance.engine.deckPlaying(index))
            playingMask |= static_cast<uint8_t>(1u << index);
    }
    instance.headActiveMask.store(activeMask, std::memory_order_release);
    instance.deckPlayingMask.store(playingMask, std::memory_order_release);
    if (activeMask != previousMask || playingMask != previousPlaying)
        instance.cursorRevision.fetch_add(1u, std::memory_order_release);
    instance.outputPeak.store(instance.engine.outputPeak(),
        std::memory_order_relaxed);

    if (process->audio_outputs_count > 0u && process->audio_outputs) {
        auto& output = process->audio_outputs[0u];
        if (output.channel_count < 2u) return CLAP_PROCESS_ERROR;
        output.constant_mask = 0u;
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            const float* source = channel < 2u
                ? instance.scratch[channel].data() : nullptr;
            if (output.data32 && output.data32[channel]) {
                for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                    output.data32[channel][frame] = source ? source[frame] : 0.0f;
            } else if (output.data64 && output.data64[channel]) {
                for (uint32_t frame = 0u; frame < process->frames_count; ++frame)
                    output.data64[channel][frame] = source ? source[frame] : 0.0;
            }
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
#if defined(__APPLE__)
    serviceLoads(instance);
    if (instance.analysisDirty.exchange(false, std::memory_order_acq_rel))
        queueReanalysis(instance);
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

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

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
{ return id == kStereoOutputConfigId; }
const clap_plugin_audio_ports_config_t audioPortsConfig {
    audioPortsConfigCount, audioPortsConfigGet, audioPortsConfigSelect,
};
clap_id audioPortsConfigCurrent(const clap_plugin_t*)
{ return kStereoOutputConfigId; }
bool audioPortsConfigInfoGet(const clap_plugin_t*, clap_id configId,
    uint32_t portIndex, bool isInput, clap_audio_port_info_t* info)
{
    return configId == kStereoOutputConfigId
        && audioPortsGet(nullptr, portIndex, isInput, info);
}
const clap_plugin_audio_ports_config_info_t audioPortsConfigInfo {
    audioPortsConfigCurrent, audioPortsConfigInfoGet,
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{ return isInput ? 1u : 0u; }
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
const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

struct NoteNameDef {
    uint8_t key;
    const char* name;
};

constexpr std::array<NoteNameDef, 13u> kNoteNames {{
    { 36u, "RESTART BOTH" }, { 37u, "STOP BOTH" },
    { 38u, "PLAY BOTH" }, { 39u, "PAUSE BOTH" },
    { 40u, "DECK A PLAY/PAUSE" }, { 41u, "DECK B PLAY/PAUSE" },
    { 42u, "RESTART DECK A" }, { 43u, "RESTART DECK B" },
    { 44u, "STOP DECK A" }, { 45u, "STOP DECK B" },
    { 46u, "XFADE A" }, { 47u, "XFADE CENTER" },
    { 48u, "XFADE B" },
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
{ return static_cast<uint32_t>(kParamDefs.size()); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamDefs.size()) return false;
    const auto& def = kParamDefs[index];
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
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
    if (!value || !paramDef(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

const char* curveName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Cut", "Sharp", "Blend",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}
const char* advanceName(int value) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Stretch", "Preserve", "Hold",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 2))];
}
const char* directionName(int value) noexcept
{
    constexpr std::array<const char*, 4u> names {{ "Forward", "Reverse", "Pendulum", "Shuffle" }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 3))];
}
const char* shapeName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{
        "Repeat", "Omit", "Replace", "Envelope", "Harmonic",
    }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}
const char* detailName(int value) noexcept
{
    constexpr std::array<const char*, 5u> names {{ "Raw", "8 kHz", "4 kHz", "1 kHz", "250 Hz" }};
    return names[static_cast<std::size_t>(std::clamp(value, 0, 4))];
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    const int rounded = static_cast<int>(std::lround(value));
    if (id == kCurveParamId)
        std::snprintf(display, size, "%s", curveName(rounded));
    else if (id == kAdvanceParamId)
        std::snprintf(display, size, "%s", advanceName(rounded));
    else if (id == kDirectionParamId)
        std::snprintf(display, size, "%s", directionName(rounded));
    else if (id == kShapeParamId)
        std::snprintf(display, size, "%s", shapeName(rounded));
    else if (id == kDetailParamId)
        std::snprintf(display, size, "%s", detailName(rounded));
    else if (id == kGroupParamId)
        std::snprintf(display, size, "%u wavesets", kGroupSizes[
            static_cast<std::size_t>(std::clamp(rounded, 0, 5))]);
    else if (id == kRepeatParamId)
        std::snprintf(display, size, "%d x", rounded);
    else if (id == kStrideParamId)
        std::snprintf(display, size, "%+d", rounded);
    else if (id == kOutParamId || id == kLevelAParamId
        || id == kLevelBParamId)
        std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kPositionAParamId || id == kPositionBParamId)
        std::snprintf(display, size, "%.2f %%", value * 100.0);
    else if (id == kRateAParamId || id == kRateBParamId)
        std::snprintf(display, size, "%.2f x", value);
    else if (id == kProcessParamId || id == kJoinParamId)
        std::snprintf(display, size, "%.1f %%", value * 100.0);
    else if (id == kCrossfaderParamId)
        std::snprintf(display, size, "%+.3f", value);
    else if (id == kLinkParamId)
        std::snprintf(display, size, "%s", rounded != 0 ? "On" : "Off");
    else if (id == kMidiChannelParamId)
        std::snprintf(display, size, "CH %02d", rounded);
    else return false;
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !paramDef(id)) return false;
    const auto matchNames = [&](const auto& names) {
        for (std::size_t index = 0u; index < names.size(); ++index) {
            if (strcasecmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    };
    if (id == kCurveParamId) {
        constexpr std::array<const char*, 3u> names {{
            "Cut", "Sharp", "Blend",
        }};
        return matchNames(names);
    }
    if (id == kAdvanceParamId) {
        constexpr std::array<const char*, 3u> names {{
            "Stretch", "Preserve", "Hold",
        }};
        return matchNames(names);
    }
    if (id == kDirectionParamId) {
        constexpr std::array<const char*, 4u> names {{ "Forward", "Reverse", "Pendulum", "Shuffle" }};
        return matchNames(names);
    }
    if (id == kShapeParamId) {
        constexpr std::array<const char*, 5u> names {{
            "Repeat", "Omit", "Replace", "Envelope", "Harmonic",
        }};
        return matchNames(names);
    }
    if (id == kDetailParamId) {
        constexpr std::array<const char*, 5u> names {{ "Raw", "8 kHz", "4 kHz", "1 kHz", "250 Hz" }};
        return matchNames(names);
    }
    if (id == kMidiChannelParamId
        && (strncasecmp(display, "CH", 2u) == 0)) display += 2u;
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;
    if (((id == kPositionAParamId || id == kPositionBParamId)
            || id == kProcessParamId || id == kJoinParamId)
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
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(
            header);
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
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto& instance = *self(plugin);
    SavedState saved;
    std::memset(&saved, 0, sizeof(saved));
    saved.magic = kStateMagic;
    saved.version = kStateVersion;
    saved.parameterCount = static_cast<uint32_t>(kParamCount);
    for (const auto& def : kParamDefs)
        saved.parameters[paramIndex(def.id)] = paramValue(instance, def.id);
    std::snprintf(saved.path.data(), saved.path.size(), "%s",
        instance.samplePath.c_str());
    if (instance.controlMap && instance.controlMap->asset) {
        const auto& asset = *instance.controlMap->asset;
        saved.channelCount = asset.channelCount;
        saved.frameCount = asset.frameCount();
        saved.sampleRate = asset.sampleRate;
        const uint64_t bytes = static_cast<uint64_t>(saved.channelCount)
            * saved.frameCount * sizeof(float);
        saved.embedded = instance.embedSampleInState
                && bytes <= kMaximumEmbeddedAudioBytes ? 1u : 0u;
    }
    if (!s3g::clap_state::writeAll(stream, &saved, sizeof(saved))) return false;
    if (saved.embedded != 0u && instance.controlMap) {
        for (uint8_t channel = 0u; channel < saved.channelCount; ++channel) {
            const auto& samples = instance.controlMap->asset->channels[channel];
            if (!s3g::clap_state::writeAll(stream, samples.data(),
                    samples.size() * sizeof(float))) return false;
        }
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState saved;
    if (!s3g::clap_state::readAll(stream, &saved, sizeof(saved))
        || saved.magic != kStateMagic || saved.version != kStateVersion
        || saved.parameterCount != kParamCount) return false;
    auto& instance = *self(plugin);
    for (const auto& def : kParamDefs)
        setParam(instance, def.id,
            saved.parameters[paramIndex(def.id)], false);
    std::shared_ptr<const SampleAsset> asset;
    const std::string path(saved.path.data(), strnlen(saved.path.data(),
        saved.path.size()));
    if (saved.embedded != 0u) {
        if (saved.channelCount == 0u || saved.channelCount > 2u
            || saved.frameCount < 8u || !(saved.sampleRate > 0.0)) return false;
        const uint64_t bytes = static_cast<uint64_t>(saved.channelCount)
            * saved.frameCount * sizeof(float);
        if (bytes > kMaximumEmbeddedAudioBytes) return false;
        try {
            auto decoded = std::make_shared<SampleAsset>();
            decoded->channelCount = saved.channelCount;
            decoded->sampleRate = saved.sampleRate;
            for (uint8_t channel = 0u; channel < saved.channelCount; ++channel) {
                decoded->channels[channel].resize(saved.frameCount);
                if (!s3g::clap_state::readAll(stream,
                        decoded->channels[channel].data(),
                        decoded->channels[channel].size() * sizeof(float)))
                    return false;
            }
            if (!decoded->valid()) return false;
            asset = std::move(decoded);
        } catch (...) { return false; }
    } else if (!path.empty()) {
#if defined(__APPLE__)
        std::string error;
        (void)decodeSampleFile(path, asset, error);
#endif
    }
    std::shared_ptr<const WavesetMap> map;
    if (asset) map = s3g::sample::analyzeWavesets(asset,
        crossingDetail(instance));
    if (asset && !map) return false;
    publishMap(instance, std::move(map), path, false);
    instance.embedSampleInState = saved.embedded != 0u;
    instance.analysisDirty.store(false, std::memory_order_release);
    instance.status = instance.controlMap
        ? std::to_string(instance.controlMap->units.size()) + " WAVESETS / "
            + sampleDisplayName(instance.samplePath)
        : "PROJECT RESTORED / SAMPLE OFFLINE";
    return true;
}
const clap_plugin_state_t state { stateSave, stateLoad };

} // namespace

// The native editor follows below.

#if defined(__APPLE__)

namespace {

constexpr uint32_t kHeadColors[2u] { 0x69d2dc, 0xff7047 };
constexpr NSRect kOverviewRect = {{ 18.0, 72.0 }, { 1004.0, 174.0 }};
constexpr NSRect kDeckWaveRects[2u] {
    {{ 18.0, 72.0 }, { 1004.0, 87.0 }},
    {{ 18.0, 159.0 }, { 1004.0, 87.0 }},
};
constexpr NSRect kScopeRect = {{ 18.0, 258.0 }, { 1004.0, 94.0 }};
constexpr NSRect kScopeGraphRect = {{ 139.0, 267.0 }, { 870.0, 76.0 }};
constexpr NSRect kGlobalPanel = {{ 18.0, 372.0 }, { 322.0, 424.0 }};
constexpr NSRect kHeadPanels[2u] {
    {{ 352.0, 372.0 }, { 325.0, 238.0 }},
    {{ 689.0, 372.0 }, { 333.0, 238.0 }},
};
constexpr NSRect kTransportPanel = {{ 352.0, 622.0 }, { 670.0, 174.0 }};
constexpr NSRect kDeckPlayButtons[2u] {
    {{ 364.0, 410.0 }, { 96.0, 28.0 }},
    {{ 701.0, 410.0 }, { 96.0, 28.0 }},
};
constexpr NSRect kDeckStopButtons[2u] {
    {{ 468.0, 410.0 }, { 78.0, 28.0 }},
    {{ 805.0, 410.0 }, { 78.0, 28.0 }},
};
constexpr NSRect kDeckRestartButtons[2u] {
    {{ 554.0, 410.0 }, { 110.0, 28.0 }},
    {{ 891.0, 410.0 }, { 118.0, 28.0 }},
};
constexpr NSRect kRestartBothButton = {{ 366.0, 656.0 }, { 112.0, 28.0 }};
constexpr NSRect kPlayBothButton = {{ 486.0, 656.0 }, { 92.0, 28.0 }};
constexpr NSRect kStopBothButton = {{ 586.0, 656.0 }, { 92.0, 28.0 }};
constexpr NSRect kLinkButton = {{ 686.0, 656.0 }, { 82.0, 28.0 }};
constexpr NSRect kCrossfaderTrack = {{ 408.0, 738.0 }, { 558.0, 14.0 }};
constexpr NSRect kPresetButton = {{ 620.0, 18.0 }, { 142.0, 24.0 }};
constexpr NSRect kLoadButton = {{ 774.0, 18.0 }, { 70.0, 24.0 }};
constexpr NSRect kClearButton = {{ 854.0, 18.0 }, { 70.0, 24.0 }};
constexpr NSRect kEmbedButton = {{ 934.0, 18.0 }, { 88.0, 24.0 }};

constexpr std::array<const char*, 5u> kPresetNames {{
    "Single Deck", "Dual Flow", "Preserved Phase", "Reverse Blocks",
    "Omit Drift",
}};
NSString* const kPresetMenuItems[] = {
    @"Single Deck", @"Dual Flow", @"Preserved Phase", @"Reverse Blocks",
    @"Omit Drift",
};
NSString* const kCurveMenuItems[] = { @"Cut", @"Sharp", @"Blend" };
NSString* const kAdvanceMenuItems[] = { @"Stretch", @"Preserve", @"Hold" };
NSString* const kGroupMenuItems[] = { @"1", @"2", @"4", @"8", @"16", @"32" };
NSString* const kRepeatMenuItems[] = {
    @"1 x", @"2 x", @"3 x", @"4 x", @"5 x", @"6 x", @"7 x", @"8 x",
    @"9 x", @"10 x", @"11 x", @"12 x", @"13 x", @"14 x", @"15 x", @"16 x",
};
NSString* const kStrideMenuItems[] = {
    @"-16", @"-15", @"-14", @"-13", @"-12", @"-11", @"-10", @"-9",
    @"-8", @"-7", @"-6", @"-5", @"-4", @"-3", @"-2", @"-1",
    @"0", @"+1", @"+2", @"+3", @"+4", @"+5", @"+6", @"+7",
    @"+8", @"+9", @"+10", @"+11", @"+12", @"+13", @"+14", @"+15", @"+16",
};
NSString* const kDirectionMenuItems[] = {
    @"Forward", @"Reverse", @"Pendulum", @"Shuffle",
};
NSString* const kShapeMenuItems[] = {
    @"Repeat", @"Omit", @"Replace", @"Envelope", @"Harmonic",
};
NSString* const kDetailMenuItems[] = {
    @"Raw", @"8 kHz", @"4 kHz", @"1 kHz", @"250 Hz",
};
NSString* const kMidiChannelMenuItems[] = {
    @"CH 01", @"CH 02", @"CH 03", @"CH 04", @"CH 05", @"CH 06",
    @"CH 07", @"CH 08", @"CH 09", @"CH 10", @"CH 11", @"CH 12",
    @"CH 13", @"CH 14", @"CH 15", @"CH 16",
};

struct CanvasMenuSpec {
    NSString* const* items = nullptr;
    uint32_t count = 0u;
    uint32_t columns = 1u;
};

CanvasMenuSpec canvasMenuSpec(clap_id id) noexcept
{
    switch (id) {
    case kCurveParamId: return { kCurveMenuItems, 3u, 1u };
    case kAdvanceParamId: return { kAdvanceMenuItems, 3u, 1u };
    case kGroupParamId: return { kGroupMenuItems, 6u, 1u };
    case kRepeatParamId: return { kRepeatMenuItems, 16u, 2u };
    case kStrideParamId: return { kStrideMenuItems, 33u, 3u };
    case kDirectionParamId: return { kDirectionMenuItems, 4u, 1u };
    case kShapeParamId: return { kShapeMenuItems, 5u, 1u };
    case kDetailParamId: return { kDetailMenuItems, 5u, 1u };
    case kMidiChannelParamId: return { kMidiChannelMenuItems, 16u, 2u };
    default: return {};
    }
}

int canvasMenuIndex(clap_id id, double value) noexcept
{
    const int rounded = static_cast<int>(std::lround(value));
    if (id == kRepeatParamId || id == kMidiChannelParamId)
        return rounded - 1;
    if (id == kStrideParamId) return rounded + 16;
    return rounded;
}

double canvasMenuValue(clap_id id, int index) noexcept
{
    if (id == kRepeatParamId || id == kMidiChannelParamId)
        return static_cast<double>(index + 1);
    if (id == kStrideParamId) return static_cast<double>(index - 16);
    return static_cast<double>(index);
}

struct CursorSignature {
    const WavesetMap* map = nullptr;
    uint64_t revision = 0u;
    bool processing = false;
    uint8_t activeMask = 0u;
    uint8_t playingMask = 0u;
    int timeMode = -1;
    int repeats = 1;
    int stride = 0;
    std::array<double, 2u> rates {{ 0.0, 0.0 }};
    std::array<double, 2u> positions {{ -1.0, -1.0 }};

    bool equals(const CursorSignature& other) const noexcept
    {
        return map == other.map && revision == other.revision
            && processing == other.processing
            && activeMask == other.activeMask
            && playingMask == other.playingMask
            && timeMode == other.timeMode
            && repeats == other.repeats && stride == other.stride
            && std::abs(rates[0u] - other.rates[0u]) < 1.0e-9
            && std::abs(rates[1u] - other.rates[1u]) < 1.0e-9
            && std::abs(positions[0u] - other.positions[0u]) < 1.0e-5
            && std::abs(positions[1u] - other.positions[1u]) < 1.0e-5;
    }
};

} // namespace

@interface S3GSampleWavesetsCursorView : NSView {
@private
    Plugin* _instance;
    CAShapeLayer* _deckLines[2u];
    CursorSignature _signature;
    NSUInteger _animationInstallCount;
}
- (instancetype)initWithPlugin:(Plugin*)instance frame:(NSRect)frame;
- (void)updateTrajectories;
- (void)invalidateTrajectories;
- (NSUInteger)motionAnimationCount;
- (NSUInteger)animationInstallCount;
@end

@implementation S3GSampleWavesetsCursorView

- (BOOL)isFlipped { return YES; }
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }
- (BOOL)isOpaque { return NO; }

- (instancetype)initWithPlugin:(Plugin*)instance frame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _instance = instance;
    _animationInstallCount = 0u;
    [self setWantsLayer:YES];
    [self.layer setBackgroundColor:[[NSColor clearColor] CGColor]];
    [self.layer setMasksToBounds:YES];
    for (std::size_t index = 0u; index < 2u; ++index) {
        CAShapeLayer* line = [CAShapeLayer layer];
        line.bounds = CGRectMake(0.0, 0.0, 2.0,
            kDeckWaveRects[index].size.height);
        line.anchorPoint = CGPointMake(0.5, 0.5);
        CGMutablePathRef path = CGPathCreateMutable();
        CGPathMoveToPoint(path, nullptr, 1.0, 0.0);
        CGPathAddLineToPoint(path, nullptr, 1.0,
            kDeckWaveRects[index].size.height);
        line.path = path;
        CGPathRelease(path);
        line.strokeColor = [s3g::clap_gui::color(
            static_cast<int>(kHeadColors[index])) CGColor];
        line.lineWidth = 2.0;
        line.shadowColor = line.strokeColor;
        line.shadowOpacity = 0.35f;
        line.shadowRadius = 2.0;
        line.position = CGPointMake(NSMinX(kDeckWaveRects[index]),
            NSMidY(kDeckWaveRects[index]));
        line.hidden = YES;
        [self.layer addSublayer:line];
        _deckLines[index] = line;
    }
    return self;
}

- (void)invalidateTrajectories
{
    _signature = {};
    for (CAShapeLayer* line : _deckLines) [line removeAllAnimations];
}

- (void)updateTrajectories
{
    if (!_instance) return;
    const WavesetMap* map = _instance->publishedMap.load(
        std::memory_order_acquire);
    const uint64_t revision = _instance->cursorRevision.load(
        std::memory_order_acquire);
    const int timeMode = static_cast<int>(std::lround(paramValue(*_instance,
        kAdvanceParamId)));
    const int stride = static_cast<int>(std::lround(paramValue(*_instance,
        kStrideParamId)));
    const int repeats = std::clamp(static_cast<int>(std::lround(paramValue(
        *_instance, kRepeatParamId))), 1, 16);
    const bool processing = _instance->processing.load(
        std::memory_order_acquire);
    const uint8_t activeMask = _instance->headActiveMask.load(
        std::memory_order_acquire);
    const uint8_t playingMask = _instance->deckPlayingMask.load(
        std::memory_order_acquire);
    CursorSignature signature;
    signature.map = map;
    signature.revision = revision;
    signature.processing = processing;
    signature.activeMask = activeMask;
    signature.playingMask = playingMask;
    signature.timeMode = timeMode;
    signature.repeats = repeats;
    signature.stride = stride;
    for (std::size_t deck = 0u; deck < 2u; ++deck) {
        signature.rates[deck] = paramValue(*_instance, kRateParamIds[deck]);
        signature.positions[deck] = paramValue(
            *_instance, kPositionParamIds[deck]);
    }
    if (_signature.equals(signature)) {
        bool ready = true;
        for (std::size_t deck = 0u; deck < 2u; ++deck) {
            const bool moving = processing
                && (playingMask & (1u << deck)) != 0u
                && timeMode != static_cast<int>(WavesetAdvanceMode::Hold)
                && stride != 0;
            ready = ready && (!moving || [_deckLines[deck]
                animationForKey:@"s3g.cursor.motion"] != nil);
        }
        if (ready) return;
    }
    _signature = signature;
    for (std::size_t deck = 0u; deck < 2u; ++deck) {
        CAShapeLayer* line = _deckLines[deck];
        [line removeAnimationForKey:@"s3g.cursor.motion"];
        float observed = _instance->headPositions[deck].load(
            std::memory_order_relaxed);
        if (observed < 0.0f) observed = static_cast<float>(
            signature.positions[deck]);
        observed = std::clamp(observed, 0.0f, 1.0f);
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        line.hidden = map == nullptr;
        line.position = CGPointMake(NSMinX(kDeckWaveRects[deck])
                + NSWidth(kDeckWaveRects[deck]) * observed,
            NSMidY(kDeckWaveRects[deck]));
        [CATransaction commit];
        const bool moving = map && processing
            && (playingMask & (1u << deck)) != 0u
            && timeMode != static_cast<int>(WavesetAdvanceMode::Hold)
            && stride != 0;
        if (!moving) continue;
        const double speed = std::clamp(signature.rates[deck], 0.25, 4.0);
        const double sourceStep = static_cast<double>(std::abs(stride))
            * (timeMode == static_cast<int>(WavesetAdvanceMode::Preserve)
                ? static_cast<double>(repeats) : 1.0);
        const double duration = std::max(0.05,
            static_cast<double>(map->asset->frameCount())
                / map->asset->sampleRate * static_cast<double>(repeats)
                / (sourceStep * speed));
        CAKeyframeAnimation* animation = [CAKeyframeAnimation
            animationWithKeyPath:@"position.x"];
        animation.values = stride > 0
            ? @[ @(NSMinX(kDeckWaveRects[deck])),
                 @(NSMaxX(kDeckWaveRects[deck])) ]
            : @[ @(NSMaxX(kDeckWaveRects[deck])),
                 @(NSMinX(kDeckWaveRects[deck])) ];
        animation.keyTimes = @[ @0.0, @1.0 ];
        animation.calculationMode = kCAAnimationLinear;
        animation.duration = duration;
        animation.repeatCount = HUGE_VALF;
        animation.beginTime = [line convertTime:CACurrentMediaTime()
            fromLayer:nil] - (stride > 0 ? observed : 1.0 - observed)
                * duration;
        animation.removedOnCompletion = NO;
        [line addAnimation:animation forKey:@"s3g.cursor.motion"];
        ++_animationInstallCount;
    }
}

- (NSUInteger)motionAnimationCount
{
    NSUInteger count = 0u;
    for (CAShapeLayer* line : _deckLines)
        if ([line animationForKey:@"s3g.cursor.motion"]) ++count;
    return count;
}

- (NSUInteger)animationInstallCount { return _animationInstallCount; }

@end

@interface S3GSampleWavesetsView : NSView <NSDraggingDestination> {
@private
    Plugin* _instance;
    NSTimer* _timer;
    S3GSampleWavesetsCursorView* _cursorView;
    clap_id _dragParam;
    NSInteger _selectedHead;
    NSInteger _presetIndex;
    clap_id _openMenuParam;
    BOOL _presetMenuOpen;
    int _menuHover;
    NSTrackingArea* _trackingArea;
    uint32_t _feedbackMask;
    double _feedbackUntil;
}
- (instancetype)initWithPlugin:(Plugin*)instance;
- (void)startTimer;
- (void)stopTimer;
- (NSUInteger)cursorMotionAnimationCount;
- (NSUInteger)cursorAnimationInstallCount;
- (BOOL)canvasMenuOpen;
- (int)canvasMenuHover;
@end

@implementation S3GSampleWavesetsView

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (instancetype)initWithPlugin:(Plugin*)instance
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _instance = instance;
    _dragParam = CLAP_INVALID_ID;
    _selectedHead = 0;
    _presetIndex = 0;
    _openMenuParam = CLAP_INVALID_ID;
    _presetMenuOpen = NO;
    _menuHover = -1;
    _feedbackMask = 0u;
    _feedbackUntil = 0.0;
    [self setWantsLayer:YES];
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    _cursorView = [[S3GSampleWavesetsCursorView alloc]
        initWithPlugin:instance frame:self.bounds];
    [self addSubview:_cursorView];
    [self startTimer];
    return self;
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
        options:(NSTrackingMouseMoved | NSTrackingActiveInActiveApp
            | NSTrackingInVisibleRect)
        owner:self userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)dealloc
{
    [self stopTimer];
    [self unregisterDraggedTypes];
}

- (void)startTimer
{
    if (_timer) return;
    __weak S3GSampleWavesetsView* weakSelf = self;
    _timer = [NSTimer timerWithTimeInterval:(1.0 / 30.0) repeats:YES
        block:^(NSTimer*) {
            S3GSampleWavesetsView* view = weakSelf;
            if (!view) return;
            const double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const uint32_t feedback = view->_instance->actionFeedback.exchange(
                0u, std::memory_order_acq_rel);
            if (feedback != 0u) {
                view->_feedbackMask |= feedback;
                view->_feedbackUntil = now + 0.14;
            } else if (view->_feedbackMask != 0u
                && now >= view->_feedbackUntil) {
                view->_feedbackMask = 0u;
            }
            [view->_cursorView updateTrajectories];
            [view setNeedsDisplay:YES];
        }];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
    [_cursorView updateTrajectories];
}

- (void)stopTimer
{
    [_timer invalidate];
    _timer = nil;
    [_cursorView invalidateTrajectories];
}

- (NSUInteger)cursorMotionAnimationCount
{
    [_cursorView updateTrajectories];
    return [_cursorView motionAnimationCount];
}

- (NSUInteger)cursorAnimationInstallCount
{
    return [_cursorView animationInstallCount];
}

- (BOOL)canvasMenuOpen
{
    return _presetMenuOpen || _openMenuParam != CLAP_INVALID_ID;
}

- (int)canvasMenuHover { return _menuHover; }

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    return [[[sender draggingPasteboard] readObjectsForClasses:@[
        [NSURL class]] options:@{ NSPasteboardURLReadingFileURLsOnlyKey:@YES }]
        count] > 0u ? NSDragOperationCopy : NSDragOperationNone;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    NSArray<NSURL*>* urls = [[sender draggingPasteboard]
        readObjectsForClasses:@[[NSURL class]]
        options:@{ NSPasteboardURLReadingFileURLsOnlyKey:@YES }];
    NSURL* url = [urls firstObject];
    if (!url || ![url isFileURL]) return NO;
    queueSampleLoad(*_instance, std::string([[url path] UTF8String]));
    return YES;
}

- (void)openSample
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setAllowedFileTypes:@[ @"wav", @"aif", @"aiff", @"caf", @"m4a", @"mp3", @"flac" ]];
    if ([panel runModal] != NSModalResponseOK) return;
    NSURL* url = [[panel URLs] firstObject];
    if (url) queueSampleLoad(*_instance,
        std::string([[url path] UTF8String]));
}

- (void)applyPreset:(NSInteger)index
{
    index = std::clamp<NSInteger>(index, 0, 4);
    _presetIndex = index;
    struct Setting { clap_id id; double value; };
    std::vector<Setting> settings;
    const auto add = [&settings](clap_id id, double value) {
        settings.push_back({ id, value });
    };
    if (index == 0) {
        add(kCrossfaderParamId, -1.0); add(kCurveParamId, 2);
        add(kAdvanceParamId, 0);
        add(kGroupParamId, 3); add(kRepeatParamId, 2);
        add(kStrideParamId, 1); add(kDirectionParamId, 0);
        add(kShapeParamId, 0); add(kProcessParamId, 0);
        add(kPositionAParamId, 0); add(kPositionBParamId, .5);
        for (clap_id id : kRateParamIds) add(id, 1.0);
    } else if (index == 1) {
        add(kCrossfaderParamId, 0.0); add(kCurveParamId, 2);
        add(kAdvanceParamId, 0);
        add(kGroupParamId, 3); add(kRepeatParamId, 2);
        add(kStrideParamId, 1); add(kDirectionParamId, 0);
        add(kShapeParamId, 0); add(kProcessParamId, 0);
        add(kPositionAParamId, 0); add(kPositionBParamId, .25);
        add(kRateAParamId, 1.0); add(kRateBParamId, 1.005);
    } else if (index == 2) {
        add(kCrossfaderParamId, 0.0); add(kCurveParamId, 2);
        add(kAdvanceParamId, 1);
        add(kGroupParamId, 2); add(kRepeatParamId, 4);
        add(kStrideParamId, 1); add(kDirectionParamId, 0);
        add(kShapeParamId, 3); add(kProcessParamId, .55);
        add(kPositionAParamId, 0); add(kPositionBParamId, .125);
        for (clap_id id : kRateParamIds) add(id, 1.0);
    } else if (index == 3) {
        add(kCrossfaderParamId, 0.0); add(kCurveParamId, 1);
        add(kAdvanceParamId, 0);
        add(kGroupParamId, 4); add(kRepeatParamId, 2);
        add(kStrideParamId, 1); add(kDirectionParamId, 1);
        add(kShapeParamId, 0); add(kProcessParamId, 0);
        add(kPositionAParamId, 0); add(kPositionBParamId, .5);
        for (clap_id id : kRateParamIds) add(id, 1.0);
    } else {
        add(kCrossfaderParamId, 0.0); add(kCurveParamId, 1);
        add(kAdvanceParamId, 1);
        add(kGroupParamId, 3); add(kRepeatParamId, 3);
        add(kStrideParamId, 1); add(kDirectionParamId, 3);
        add(kShapeParamId, 1); add(kProcessParamId, .6);
        add(kPositionAParamId, 0); add(kPositionBParamId, .33);
        add(kRateAParamId, .75); add(kRateBParamId, 1.0);
    }
    for (const auto& setting : settings)
        queueGuiParamGesture(*_instance, setting.id, setting.value);
    _instance->cursorRevision.fetch_add(1u, std::memory_order_release);
    [_cursorView invalidateTrajectories];
    [self setNeedsDisplay:YES];
}

- (NSRect)menuRectAtY:(CGFloat)y
{
    return NSMakeRect(kGlobalPanel.origin.x + 112.0, y - 2.0,
        kGlobalPanel.size.width - 130.0, 18.0);
}

- (NSRect)anchorRectForMenuParam:(clap_id)id
{
    switch (id) {
    case kAdvanceParamId: return [self menuRectAtY:435.0];
    case kGroupParamId: return [self menuRectAtY:460.0];
    case kRepeatParamId: return [self menuRectAtY:485.0];
    case kStrideParamId: return [self menuRectAtY:510.0];
    case kDirectionParamId: return [self menuRectAtY:535.0];
    case kShapeParamId: return [self menuRectAtY:560.0];
    case kDetailParamId: return [self menuRectAtY:635.0];
    case kCurveParamId: return [self menuRectAtY:660.0];
    case kMidiChannelParamId: return [self menuRectAtY:685.0];
    default: return NSZeroRect;
    }
}

- (NSRect)openMenuRect
{
    constexpr CGFloat rowHeight = 20.0;
    if (_presetMenuOpen) return NSMakeRect(NSMinX(kPresetButton),
        NSMaxY(kPresetButton), NSWidth(kPresetButton),
        rowHeight * static_cast<CGFloat>(std::size(kPresetMenuItems)));
    const CanvasMenuSpec spec = canvasMenuSpec(_openMenuParam);
    const NSRect anchor = [self anchorRectForMenuParam:_openMenuParam];
    if (spec.count == 0u) return NSZeroRect;
    const uint32_t rows = s3g::clap_gui::multiColumnMenuRows(
        spec.count, spec.columns);
    const CGFloat height = rowHeight * static_cast<CGFloat>(rows);
    const CGFloat below = NSMaxY(anchor);
    const CGFloat y = below + height <= kGuiHeight - 8.0
        ? below : NSMinY(anchor) - height;
    return NSMakeRect(NSMinX(anchor), y, NSWidth(anchor), height);
}

- (void)closeMenus
{
    _openMenuParam = CLAP_INVALID_ID;
    _presetMenuOpen = NO;
    _menuHover = -1;
    [_cursorView setHidden:NO];
}

- (NSRect)sliderHitRectForPanel:(NSRect)panel y:(CGFloat)y
{
    return NSMakeRect(panel.origin.x + 10.0, y - 8.0,
        panel.size.width - 20.0, 28.0);
}

- (clap_id)sliderAtPoint:(NSPoint)point panel:(NSRect*)panelOut y:(CGFloat*)yOut
{
    struct Row { clap_id id; NSRect panel; CGFloat y; };
    const std::array<Row, 9u> rows {{
        { kOutParamId, kGlobalPanel, 414.0 },
        { kProcessParamId, kGlobalPanel, 585.0 },
        { kJoinParamId, kGlobalPanel, 610.0 },
        { kPositionAParamId, kHeadPanels[0u], 460.0 },
        { kRateAParamId, kHeadPanels[0u], 510.0 },
        { kLevelAParamId, kHeadPanels[0u], 560.0 },
        { kPositionBParamId, kHeadPanels[1u], 460.0 },
        { kRateBParamId, kHeadPanels[1u], 510.0 },
        { kLevelBParamId, kHeadPanels[1u], 560.0 },
    }};
    for (const auto& row : rows) {
        if (NSPointInRect(point, [self sliderHitRectForPanel:row.panel y:row.y])) {
            if (panelOut) *panelOut = row.panel;
            if (yOut) *yOut = row.y;
            return row.id;
        }
    }
    return CLAP_INVALID_ID;
}

- (void)setDraggedParam:(clap_id)id fromPoint:(NSPoint)point panel:(NSRect)panel
{
    const ParamDef* def = paramDef(id);
    if (!def) return;
    const CGFloat controlX = panel.origin.x + 108.0;
    const CGFloat controlW = panel.size.width - 176.0;
    const double normalized = std::clamp(
        static_cast<double>((point.x - controlX) / controlW), 0.0, 1.0);
    queueGuiParamValue(*_instance, id,
        def->minimum + normalized * (def->maximum - def->minimum));
    if (id == kPositionAParamId || id == kPositionBParamId
        || id == kRateAParamId || id == kRateBParamId)
        _instance->cursorRevision.fetch_add(1u, std::memory_order_release);
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_presetMenuOpen || _openMenuParam != CLAP_INVALID_ID) {
        const BOOL presetWasOpen = _presetMenuOpen;
        const clap_id openParam = _openMenuParam;
        const CanvasMenuSpec spec = presetWasOpen
            ? CanvasMenuSpec { kPresetMenuItems,
                static_cast<uint32_t>(std::size(kPresetMenuItems)), 1u }
            : canvasMenuSpec(openParam);
        const int selected = spec.columns > 1u
            ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
                [self openMenuRect], 20.0, spec.count, spec.columns)
            : s3g::clap_gui::dropdownHitIndex(point,
                [self openMenuRect], 20.0, spec.count);
        [self closeMenus];
        [self setNeedsDisplay:YES];
        if (selected >= 0) {
            if (presetWasOpen) [self applyPreset:selected];
            else queueGuiParamGesture(*_instance, openParam,
                canvasMenuValue(openParam, selected));
            return;
        }
    }
    if (NSPointInRect(point, kLoadButton)) {
        [self closeMenus]; [self openSample]; return;
    }
    if (NSPointInRect(point, kClearButton)) {
        [self closeMenus];
        publishMap(*_instance, {}, "");
        _instance->status = "DROP A MONO OR STEREO SAMPLE";
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, kEmbedButton)) {
        [self closeMenus];
        _instance->embedSampleInState = !_instance->embedSampleInState;
        markStateDirty(*_instance); [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, kPresetButton)) {
        [self closeMenus];
        _presetMenuOpen = YES;
        // The preset dropdown crosses the overview. Keep the compositor
        // overlay beneath the menu instead of allowing cursors to punch
        // through its rows.
        [_cursorView setHidden:YES];
        [self setNeedsDisplay:YES];
        return;
    }
    for (std::size_t deck = 0u; deck < 2u; ++deck) {
        const bool linked = paramValue(*_instance, kLinkParamId) >= 0.5;
        if (NSPointInRect(point, kDeckPlayButtons[deck])) {
            requestAction(*_instance,
                deck == 0u ? kActionToggleA : kActionToggleB);
            [self setNeedsDisplay:YES]; return;
        }
        if (NSPointInRect(point, kDeckStopButtons[deck])) {
            requestAction(*_instance, linked ? kActionStopBoth
                : (deck == 0u ? kActionStopA : kActionStopB));
            [self setNeedsDisplay:YES]; return;
        }
        if (NSPointInRect(point, kDeckRestartButtons[deck])) {
            requestAction(*_instance, linked ? kActionRestartBoth
                : (deck == 0u ? kActionRestartA : kActionRestartB));
            [self setNeedsDisplay:YES]; return;
        }
    }
    if (NSPointInRect(point, kRestartBothButton)) {
        requestAction(*_instance, kActionRestartBoth);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, kPlayBothButton)) {
        requestAction(*_instance, kActionPlayBoth);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, kStopBothButton)) {
        requestAction(*_instance, kActionStopBoth);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, kLinkButton)) {
        queueGuiParamGesture(*_instance, kLinkParamId,
            paramValue(*_instance, kLinkParamId) >= 0.5 ? 0.0 : 1.0);
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, NSInsetRect(kCrossfaderTrack, -18.0, -14.0))) {
        _dragParam = kCrossfaderParamId;
        queueGuiParamBegin(*_instance, _dragParam);
        const double normalized = std::clamp(static_cast<double>(
            (point.x - NSMinX(kCrossfaderTrack))
                / NSWidth(kCrossfaderTrack)), 0.0, 1.0);
        queueGuiParamValue(*_instance, _dragParam,
            normalized * 2.0 - 1.0);
        return;
    }
    const struct MenuRow { clap_id id; CGFloat y; } menuRows[] = {
        { kAdvanceParamId, 435.0 },
        { kGroupParamId, 460.0 },
        { kRepeatParamId, 485.0 },
        { kStrideParamId, 510.0 },
        { kDirectionParamId, 535.0 },
        { kShapeParamId, 560.0 },
        { kDetailParamId, 635.0 },
        { kCurveParamId, 660.0 },
        { kMidiChannelParamId, 685.0 },
    };
    for (const auto& row : menuRows) {
        NSRect rect = [self menuRectAtY:row.y];
        if (NSPointInRect(point, rect)) {
            [self closeMenus];
            _openMenuParam = row.id;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, kOverviewRect) && _instance->controlMap) {
        const double normalized = std::clamp(
            static_cast<double>((point.x - NSMinX(kOverviewRect))
                / NSWidth(kOverviewRect)), 0.0, 1.0);
        const std::size_t chosen = point.y < NSMidY(kOverviewRect) ? 0u : 1u;
        _selectedHead = static_cast<NSInteger>(chosen);
        _dragParam = kPositionParamIds[chosen];
        queueGuiParamBegin(*_instance, _dragParam);
        queueGuiParamValue(*_instance, _dragParam, normalized);
        _instance->cursorRevision.fetch_add(1u, std::memory_order_release);
        return;
    }
    NSRect panel {};
    CGFloat rowY = 0.0;
    const clap_id slider = [self sliderAtPoint:point panel:&panel y:&rowY];
    (void)rowY;
    if (slider != CLAP_INVALID_ID) {
        _dragParam = slider;
        if (slider == kPositionAParamId) _selectedHead = 0;
        else if (slider == kPositionBParamId) _selectedHead = 1;
        queueGuiParamBegin(*_instance, slider);
        [self setDraggedParam:slider fromPoint:point panel:panel];
        return;
    }
    for (std::size_t head = 0u; head < 2u; ++head) {
        if (NSPointInRect(point, kHeadPanels[head])) {
            _selectedHead = static_cast<NSInteger>(head);
            [self setNeedsDisplay:YES]; return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_dragParam == kCrossfaderParamId) {
        const double normalized = std::clamp(static_cast<double>(
            (point.x - NSMinX(kCrossfaderTrack))
                / NSWidth(kCrossfaderTrack)), 0.0, 1.0);
        queueGuiParamValue(*_instance, _dragParam,
            normalized * 2.0 - 1.0);
        return;
    }
    if ((_dragParam == kPositionAParamId
            || _dragParam == kPositionBParamId)
        && NSPointInRect(point, NSInsetRect(kOverviewRect, -12.0, -20.0))) {
        const double normalized = std::clamp(
            static_cast<double>((point.x - NSMinX(kOverviewRect))
                / NSWidth(kOverviewRect)), 0.0, 1.0);
        queueGuiParamValue(*_instance, _dragParam, normalized);
        _instance->cursorRevision.fetch_add(1u, std::memory_order_release);
        return;
    }
    NSRect panel = kGlobalPanel;
    if (_dragParam == kPositionAParamId || _dragParam == kRateAParamId
        || _dragParam == kLevelAParamId) panel = kHeadPanels[0u];
    else if (_dragParam == kPositionBParamId || _dragParam == kRateBParamId
        || _dragParam == kLevelBParamId) panel = kHeadPanels[1u];
    [self setDraggedParam:_dragParam fromPoint:point panel:panel];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam != CLAP_INVALID_ID)
        queueGuiParamEnd(*_instance, _dragParam);
    _dragParam = CLAP_INVALID_ID;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_presetMenuOpen && _openMenuParam == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const uint32_t count = _presetMenuOpen
        ? static_cast<uint32_t>(std::size(kPresetMenuItems))
        : canvasMenuSpec(_openMenuParam).count;
    const uint32_t columns = _presetMenuOpen
        ? 1u : canvasMenuSpec(_openMenuParam).columns;
    const int hover = columns > 1u
        ? s3g::clap_gui::multiColumnDropdownHitIndex(point,
            [self openMenuRect], 20.0, count, columns)
        : s3g::clap_gui::dropdownHitIndex(point,
            [self openMenuRect], 20.0, count);
    if (hover != _menuHover) {
        _menuHover = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseExited:(NSEvent*)event
{
    (void)event;
    if (_menuHover != -1) {
        _menuHover = -1;
        [self setNeedsDisplay:YES];
    }
}

- (void)drawButton:(NSRect)rect label:(NSString*)label active:(BOOL)active
    style:(const s3g::clap_gui::Style&)style attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(active ? 0x303030 : 0x181818) setFill];
    NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);
    if (active) {
        [style.accent setStroke]; NSFrameRect(NSInsetRect(rect, 1.0, 1.0));
    }
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(NSMidX(rect) - size.width * 0.5,
        NSMidY(rect) - size.height * 0.5) withAttributes:attrs];
}

- (void)drawWaveform:(NSRect)rect map:(const WavesetMap*)map
    deck:(std::size_t)deck
    style:(const s3g::clap_gui::Style&)style
{
    [style.cellBg setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);
    if (!map || !map->valid()) return;
    const auto& samples = map->asset->channels[0u];
    NSBezierPath* path = [NSBezierPath bezierPath];
    const CGFloat center = NSMidY(rect);
    const CGFloat scale = rect.size.height * 0.43;
    const uint32_t frames = map->asset->frameCount();
    for (NSInteger pixel = 0; pixel < static_cast<NSInteger>(rect.size.width);
         ++pixel) {
        const uint32_t begin = static_cast<uint32_t>(
            static_cast<double>(pixel) / rect.size.width * frames);
        const uint32_t end = std::max(begin + 1u,
            static_cast<uint32_t>(static_cast<double>(pixel + 1)
                / rect.size.width * frames));
        float low = 1.0f, high = -1.0f;
        for (uint32_t frame = begin; frame < std::min(end, frames); ++frame) {
            low = std::min(low, samples[frame]);
            high = std::max(high, samples[frame]);
        }
        const CGFloat x = rect.origin.x + static_cast<CGFloat>(pixel);
        [path moveToPoint:NSMakePoint(x, center - high * scale)];
        [path lineToPoint:NSMakePoint(x, center - low * scale)];
    }
    [s3g::clap_gui::color(0x777777) setStroke];
    [path setLineWidth:1.0]; [path stroke];
    const std::size_t increment = std::max<std::size_t>(1u,
        map->units.size() / 400u);
    [s3g::clap_gui::color(0x505050, 0.55) setStroke];
    for (std::size_t index = 0u; index < map->units.size();
         index += increment) {
        const CGFloat x = rect.origin.x + rect.size.width
            * static_cast<CGFloat>(map->units[index].startPosition)
            / static_cast<CGFloat>(frames);
        NSBezierPath* tick = [NSBezierPath bezierPath];
        [tick moveToPoint:NSMakePoint(x, NSMaxY(rect) - 8.0)];
        [tick lineToPoint:NSMakePoint(x, NSMaxY(rect))]; [tick stroke];
    }
    NSString* deckLabel = [NSString stringWithFormat:@"DECK %c",
        static_cast<char>('A' + deck)];
    [deckLabel drawAtPoint:NSMakePoint(rect.origin.x + 8.0,
        rect.origin.y + 6.0) withAttributes:s3g::clap_gui::softValueAttrs()];
}

- (void)drawScope:(NSRect)rect map:(const WavesetMap*)map
    style:(const s3g::clap_gui::Style&)style attrs:(NSDictionary*)attrs
{
    [style.cellBg setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);
    [@"WAVESET SCOPE" drawAtPoint:NSMakePoint(rect.origin.x + 10.0,
        rect.origin.y + 7.0) withAttributes:attrs];
    if (!map || !map->valid()) return;
    const std::size_t head = static_cast<std::size_t>(
        std::clamp<NSInteger>(_selectedHead, 0, 1));
    float normalized = _instance->headPositions[head].load(
        std::memory_order_relaxed);
    if (normalized < 0.0f) normalized = static_cast<float>(paramValue(
        *_instance, kPositionParamIds[head]));
    const double frame = static_cast<double>(std::clamp(normalized,
        0.0f, 1.0f)) * static_cast<double>(map->asset->frameCount());
    auto found = std::lower_bound(map->units.begin(), map->units.end(), frame,
        [](const WavesetUnit& unit, double target) {
            return unit.startPosition < target;
        });
    std::size_t start = found == map->units.end() ? map->units.size() - 1u
        : static_cast<std::size_t>(found - map->units.begin());
    const uint32_t group = kGroupSizes[static_cast<std::size_t>(std::clamp(
        static_cast<int>(std::lround(paramValue(*_instance, kGroupParamId))),
        0, 5))];
    const NSRect graph = kScopeGraphRect;
    const CGFloat unitWidth = graph.size.width / static_cast<CGFloat>(group);
    const auto& samples = map->asset->channels[0u];
    NSBezierPath* path = [NSBezierPath bezierPath];
    for (uint32_t offset = 0u; offset < group; ++offset) {
        const WavesetUnit& unit = map->units[(start + offset)
            % map->units.size()];
        for (uint32_t sample = 0u; sample <= 64u; ++sample) {
            const double phase = static_cast<double>(sample) / 64.0;
            const double sourcePosition = std::clamp(unit.startPosition
                + phase * unit.sampleLength(), 0.0,
                static_cast<double>(samples.size() - 1u));
            const uint32_t sourceFrame = static_cast<uint32_t>(sourcePosition);
            const uint32_t nextFrame = std::min<uint32_t>(sourceFrame + 1u,
                static_cast<uint32_t>(samples.size() - 1u));
            const float fraction = static_cast<float>(
                sourcePosition - sourceFrame);
            const float value = samples[sourceFrame]
                + (samples[nextFrame] - samples[sourceFrame]) * fraction;
            const CGFloat x = graph.origin.x + unitWidth * offset
                + unitWidth * static_cast<CGFloat>(phase);
            const CGFloat y = NSMidY(graph) - value
                * graph.size.height * 0.42;
            if (sample == 0u) [path moveToPoint:NSMakePoint(x, y)];
            else [path lineToPoint:NSMakePoint(x, y)];
        }
        [s3g::clap_gui::color(
            static_cast<int>(kHeadColors[head]), 0.3) setStroke];
        NSBezierPath* boundary = [NSBezierPath bezierPath];
        const CGFloat x = graph.origin.x + unitWidth * offset;
        [boundary moveToPoint:NSMakePoint(x, graph.origin.y)];
        [boundary lineToPoint:NSMakePoint(x, NSMaxY(graph))];
        [boundary stroke];
    }
    [s3g::clap_gui::color(
        static_cast<int>(kHeadColors[head])) setStroke];
    [path setLineWidth:1.25]; [path stroke];
    NSString* detail = [NSString stringWithFormat:@"DECK %c / %u WS",
        static_cast<char>('A' + head), group];
    [detail drawAtPoint:NSMakePoint(rect.origin.x + 10.0,
        rect.origin.y + 30.0) withAttributes:attrs];
}

- (void)drawSliderRow:(clap_id)id name:(NSString*)name y:(CGFloat)y
    panel:(NSRect)panel style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    const ParamDef* def = paramDef(id);
    char text[64] {};
    paramsValueToText(nullptr, id, paramValue(*_instance, id), text,
        sizeof(text));
    const CGFloat norm = static_cast<CGFloat>((paramValue(*_instance, id)
        - def->minimum) / (def->maximum - def->minimum));
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:text],
        norm, y, labels, values, style, panel.origin.x + 12.0,
        panel.origin.x + 108.0, panel.origin.x + panel.size.width - 60.0,
        panel.size.width - 176.0, 50.0);
}

- (void)drawMenuRow:(clap_id)id name:(NSString*)name y:(CGFloat)y
    style:(const s3g::clap_gui::Style&)style labels:(NSDictionary*)labels
    values:(NSDictionary*)values
{
    char text[64] {};
    paramsValueToText(nullptr, id, paramValue(*_instance, id), text,
        sizeof(text));
    s3g::clap_gui::drawMenu(name, [NSString stringWithUTF8String:text], y,
        labels, values, style, kGlobalPanel.origin.x + 12.0,
        kGlobalPanel.origin.x + 112.0, kGlobalPanel.size.width - 130.0);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    [style.bg setFill]; NSRectFill(self.bounds);
    [@"s3g SAMPLE WAVESETS" drawAtPoint:NSMakePoint(18.0, 20.0)
        withAttributes:title];
    NSString* status = [NSString stringWithUTF8String:_instance->status.c_str()];
    [status drawAtPoint:NSMakePoint(18.0, 45.0) withAttributes:values];
    NSString* preset = [NSString stringWithFormat:@"PRESET: %s",
        kPresetNames[static_cast<std::size_t>(_presetIndex)]];
    [self drawButton:kPresetButton label:preset active:NO style:style attrs:values];
    [self drawButton:kLoadButton label:@"LOAD" active:NO style:style attrs:values];
    [self drawButton:kClearButton label:@"CLEAR" active:NO style:style attrs:values];
    [self drawButton:kEmbedButton
        label:_instance->embedSampleInState ? @"EMBED" : @"PATHS"
        active:_instance->embedSampleInState style:style attrs:values];

    const WavesetMap* map = _instance->controlMap.get();
    [self drawWaveform:kDeckWaveRects[0u] map:map deck:0u style:style];
    [self drawWaveform:kDeckWaveRects[1u] map:map deck:1u style:style];
    // Core Animation owns on-screen motion so cursor presentation survives
    // host/AppKit stalls. PDF and documentation contexts do not composite
    // that layer tree, so render a static snapshot only off screen.
    if (map && ![NSGraphicsContext currentContextDrawingToScreen]) {
        for (std::size_t deck = 0u; deck < 2u; ++deck) {
            float position = _instance->headPositions[deck].load(
                std::memory_order_relaxed);
            if (position < 0.0f) position = static_cast<float>(paramValue(
                *_instance, kPositionParamIds[deck]));
            const NSRect wave = kDeckWaveRects[deck];
            const CGFloat x = NSMinX(wave) + NSWidth(wave)
                * std::clamp<CGFloat>(position, 0.0, 1.0);
            NSBezierPath* cursor = [NSBezierPath bezierPath];
            [cursor moveToPoint:NSMakePoint(x, NSMinY(wave))];
            [cursor lineToPoint:NSMakePoint(x, NSMaxY(wave))];
            [s3g::clap_gui::color(
                static_cast<int>(kHeadColors[deck])) setStroke];
            [cursor setLineWidth:2.0]; [cursor stroke];
        }
    }
    [self drawScope:kScopeRect map:map style:style attrs:values];

    s3g::clap_gui::drawPanelFrame(kGlobalPanel.origin.x,
        kGlobalPanel.origin.y, kGlobalPanel.size.width,
        kGlobalPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT / WAVESETS / MIDI", true,
        kGlobalPanel.origin.x, kGlobalPanel.origin.y,
        kGlobalPanel.size.width, 27.0, labels, style);
    [self drawSliderRow:kOutParamId name:@"OUT" y:414.0 panel:kGlobalPanel
        style:style labels:labels values:values];
    [self drawMenuRow:kAdvanceParamId name:@"TIME" y:435.0
        style:style labels:labels values:values];
    [self drawMenuRow:kGroupParamId name:@"GROUP" y:460.0
        style:style labels:labels values:values];
    [self drawMenuRow:kRepeatParamId name:@"REPEAT" y:485.0
        style:style labels:labels values:values];
    [self drawMenuRow:kStrideParamId name:@"STRIDE" y:510.0
        style:style labels:labels values:values];
    [self drawMenuRow:kDirectionParamId name:@"ORDER" y:535.0
        style:style labels:labels values:values];
    [self drawMenuRow:kShapeParamId name:@"PROCESS" y:560.0
        style:style labels:labels values:values];
    [self drawSliderRow:kProcessParamId name:@"DEPTH" y:585.0
        panel:kGlobalPanel style:style labels:labels values:values];
    [self drawSliderRow:kJoinParamId name:@"JOIN" y:610.0
        panel:kGlobalPanel style:style labels:labels values:values];
    [self drawMenuRow:kDetailParamId name:@"DETAIL" y:635.0
        style:style labels:labels values:values];
    [self drawMenuRow:kCurveParamId name:@"XFADE CURVE" y:660.0
        style:style labels:labels values:values];
    [self drawMenuRow:kMidiChannelParamId name:@"MIDI CH" y:685.0
        style:style labels:labels values:values];

    const uint8_t activeMask = _instance->headActiveMask.load(
        std::memory_order_acquire);
    const uint8_t playingMask = _instance->deckPlayingMask.load(
        std::memory_order_acquire);
    for (std::size_t head = 0u; head < 2u; ++head) {
        const NSRect panel = kHeadPanels[head];
        const bool selected = head == static_cast<std::size_t>(_selectedHead);
        s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y,
            panel.size.width, panel.size.height, style);
        NSString* header = [NSString stringWithFormat:@"WAVESET DECK %c",
            static_cast<char>('A' + head)];
        s3g::clap_gui::drawPanelHeader(header, true, panel.origin.x,
            panel.origin.y, panel.size.width, 27.0, labels, style);
        [s3g::clap_gui::color(
            static_cast<int>(kHeadColors[head])) setFill];
        NSRectFill(NSMakeRect(panel.origin.x, panel.origin.y,
            selected ? 5.0 : 3.0, panel.size.height));
        if ((activeMask & (1u << head)) != 0u) {
            [s3g::clap_gui::color(
                static_cast<int>(kHeadColors[head]), 0.8) setStroke];
            NSFrameRect(NSInsetRect(panel, 1.0, 1.0));
        }
        [self drawButton:kDeckPlayButtons[head]
            label:(playingMask & (1u << head)) != 0u
                ? @"PAUSE" : @"PLAY"
            active:(playingMask & (1u << head)) != 0u
                || (_feedbackMask & (head == 0u
                    ? kActionToggleA : kActionToggleB)) != 0u
            style:style attrs:values];
        [self drawButton:kDeckStopButtons[head] label:@"STOP"
            active:(_feedbackMask & (head == 0u
                ? kActionStopA : kActionStopB)) != 0u
            style:style attrs:values];
        [self drawButton:kDeckRestartButtons[head] label:@"RESTART"
            active:(_feedbackMask & (head == 0u
                ? kActionRestartA : kActionRestartB)) != 0u
            style:style attrs:values];
        const CGFloat baseY = 460.0;
        [self drawSliderRow:kPositionParamIds[head] name:@"POSITION"
            y:baseY panel:panel style:style labels:labels values:values];
        [self drawSliderRow:kRateParamIds[head] name:@"SPEED"
            y:baseY + 50.0 panel:panel style:style labels:labels values:values];
        [self drawSliderRow:kLevelParamIds[head] name:@"LEVEL"
            y:baseY + 100.0 panel:panel style:style labels:labels values:values];
        NSString* midi = [NSString stringWithFormat:
            @"NOTE %u P/P / %u RESTART / CC%zu POS / CC%zu SPEED",
            static_cast<unsigned>(40u + head),
            static_cast<unsigned>(42u + head), head + 17u, head + 19u];
        [midi drawAtPoint:NSMakePoint(panel.origin.x + 12.0,
            590.0) withAttributes:values];
    }
    s3g::clap_gui::drawPanelFrame(kTransportPanel.origin.x,
        kTransportPanel.origin.y, kTransportPanel.size.width,
        kTransportPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"LINKED TRANSPORT / CROSSFADER", true,
        kTransportPanel.origin.x, kTransportPanel.origin.y,
        kTransportPanel.size.width, 27.0, labels, style);
    [self drawButton:kRestartBothButton label:@"RESTART / 36"
        active:(_feedbackMask & kActionRestartBoth) != 0u
        style:style attrs:values];
    [self drawButton:kPlayBothButton label:@"PLAY / 38"
        active:playingMask == 3u
            || (_feedbackMask & kActionPlayBoth) != 0u
        style:style attrs:values];
    [self drawButton:kStopBothButton label:@"STOP / 37"
        active:(_feedbackMask & kActionStopBoth) != 0u
        style:style attrs:values];
    [self drawButton:kLinkButton label:@"LINK"
        active:paramValue(*_instance, kLinkParamId) >= 0.5
        style:style attrs:values];
    [@"A" drawAtPoint:NSMakePoint(382.0, 733.0) withAttributes:values];
    [@"B" drawAtPoint:NSMakePoint(976.0, 733.0) withAttributes:values];
    [style.strip setFill]; NSRectFill(kCrossfaderTrack);
    [style.grid setStroke]; NSFrameRect(kCrossfaderTrack);
    const CGFloat crossfadeNorm = static_cast<CGFloat>(
        (paramValue(*_instance, kCrossfaderParamId) + 1.0) * 0.5);
    const CGFloat handleX = NSMinX(kCrossfaderTrack)
        + NSWidth(kCrossfaderTrack) * crossfadeNorm;
    [style.text setFill]; NSRectFill(NSMakeRect(handleX - 7.0,
        NSMinY(kCrossfaderTrack) - 8.0, 14.0,
        NSHeight(kCrossfaderTrack) + 16.0));
    [@"CC1 / CC16 XFADE    39 PAUSE    44/45 STOP DECK    46 A    47 CENTER    48 B"
        drawAtPoint:NSMakePoint(374.0, 772.0) withAttributes:values];
    s3g::clap_gui::drawVerticalVuMeter(std::clamp<CGFloat>(
        _instance->outputPeak.load(std::memory_order_relaxed), 0.0, 1.0),
        NSMakeRect(334.0, 405.0, 4.0, 360.0), style);
    if (_presetMenuOpen) {
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
            kPresetMenuItems,
            static_cast<uint32_t>(std::size(kPresetMenuItems)),
            static_cast<int>(_presetIndex), _menuHover, values, style);
    } else if (_openMenuParam != CLAP_INVALID_ID) {
        const CanvasMenuSpec spec = canvasMenuSpec(_openMenuParam);
        const int selected = canvasMenuIndex(_openMenuParam,
            paramValue(*_instance, _openMenuParam));
        if (spec.columns > 1u) {
            s3g::clap_gui::drawMultiColumnDropdownMenu([self openMenuRect],
                20.0, spec.items, spec.count, spec.columns, selected,
                _menuHover, values, style);
        } else {
            s3g::clap_gui::drawDropdownMenu([self openMenuRect], 20.0,
                spec.items, spec.count, selected, _menuHover, values, style);
        }
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0 && !floating;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA; *floating = false; return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto& instance = *self(plugin);
    if (instance.guiView) return true;
    S3GSampleWavesetsView* view = [[S3GSampleWavesetsView alloc]
        initWithPlugin:&instance];
    if (!view) return false;
    instance.guiView = (__bridge_retained void*)view;
    if (!s3g::clap_gui::createResponsiveViewport(instance.guiViewport,
            view, kGuiWidth, kGuiHeight, 480u, 360u)) {
        void* owned = instance.guiView; instance.guiView = nullptr;
        (void)(__bridge_transfer NSView*)owned; return false;
    }
    return true;
}
void destroyGui(Plugin& instance)
{
    if (!instance.guiView) return;
    [(__bridge S3GSampleWavesetsView*)instance.guiView stopTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance.guiViewport,
        instance.guiView);
}
void guiDestroy(const clap_plugin_t* plugin) { destroyGui(*self(plugin)); }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        480u, 360u);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{ return s3g::clap_gui::getResponsiveResizeHints(hints); }
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
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance.guiViewport, false)) return false;
    [(__bridge S3GSampleWavesetsView*)instance.guiView startTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto& instance = *self(plugin);
    if (!instance.guiView) return false;
    [(__bridge S3GSampleWavesetsView*)instance.guiView stopTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance.guiViewport, true);
}
const clap_plugin_gui_t gui {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize,
    guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow,
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

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SAMPLER,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sample-wavesets",
    "s3g Sample Wavesets",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.1.0",
    "Two free-running waveset decks sharing one stereo sample and crossfader.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId || std::strcmp(pluginId, descriptor.id) != 0)
        return nullptr;
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
{ return index == 0u ? &descriptor : nullptr; }
const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
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
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
