#include <clap/clap.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "../common/s3g_nim_gesture_session.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <vector>

namespace {

constexpr uint8_t kControlChannel = 15u;
constexpr uint8_t kRecordNote = 112u;
constexpr uint8_t kPlayNote = 113u;
constexpr uint8_t kClearLastNote = 114u;
constexpr uint8_t kClearAllNote = 115u;
constexpr uint8_t kCancelRecordNote = 116u;

constexpr clap_id kRecordParamId = 1u;
constexpr clap_id kPlayParamId = 2u;
constexpr clap_id kClearLastParamId = 3u;
constexpr clap_id kClearAllParamId = 4u;
constexpr clap_id kTakeoverParamId = 5u;
constexpr clap_id kLoopCountParamId = 6u;
constexpr clap_id kLastLengthParamId = 7u;

constexpr std::array<uint16_t, 58u> makeNimGlobalParameterIds()
{
    std::array<uint16_t, 58u> ids {};
    for (uint16_t index = 0u; index < 54u; ++index)
        ids[index] = static_cast<uint16_t>(index + 1u);
    ids[54u] = 57u;
    ids[55u] = 58u;
    ids[56u] = 59u;
    ids[57u] = 60u;
    return ids;
}

constexpr auto kNimGlobalParameterIds = makeNimGlobalParameterIds();
constexpr uint32_t kGlobalParameterCount =
    static_cast<uint32_t>(kNimGlobalParameterIds.size());
constexpr uint32_t kMatrixParameterCount = 64u;
constexpr uint32_t kLaneCount = 8u;
constexpr uint32_t kLaneParameterCount = 35u;
constexpr uint32_t kNimParameterCount = kGlobalParameterCount
    + kMatrixParameterCount + kLaneCount * kLaneParameterCount;
constexpr uint32_t kLegacyNimParameterCount = 403u;
static_assert(kNimParameterCount == 402u);

constexpr uint32_t kGuiWidth = 1180u;
constexpr uint32_t kGuiHeight = 820u;

constexpr uint8_t kUiValueSeen = 1u << 0u;
constexpr uint8_t kUiLoopActive = 1u << 1u;
constexpr uint8_t kUiTakeTouched = 1u << 2u;

constexpr uint32_t kGuiToggleRecord = 1u << 0u;
constexpr uint32_t kGuiTogglePlay = 1u << 1u;
constexpr uint32_t kGuiClearSelected = 1u << 2u;
constexpr uint32_t kGuiClearAll = 1u << 3u;
constexpr uint32_t kGuiCancelRecord = 1u << 4u;
constexpr uint32_t kGuiSetTakeover = 1u << 5u;

constexpr uint32_t kStateMagic = 0x474d494eu; // "NIMG"
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kMaximumStatePoints = 16u * 1024u * 1024u;
// Recording must never grow a vector from the audio thread. Each parameter
// owns two prepared buffers (the committed loop and the current take). This
// accommodates roughly 17 seconds at 60 updates/second per continuously moved
// control; when full, the final point is coalesced instead of allocating.
constexpr uint32_t kRealtimePointsPerParameter = 1024u;
constexpr double kMaximumLoopSeconds = 24.0 * 60.0 * 60.0;

constexpr std::array<uint8_t, 8u> kSessionMagic {
    'S', '3', 'G', 'N', 'I', 'M', 'G', 'S',
};
constexpr uint16_t kSessionVersion = 1u;
constexpr uint16_t kSessionHeaderBytes = 40u;
constexpr uint32_t kSessionFlags = 0u;
constexpr uint64_t kSessionLoopHeaderBytes = 16u;
constexpr uint64_t kSessionPointBytes = 12u;
constexpr long double kNanosecondsPerSecond = 1000000000.0L;

struct Point {
    double seconds = 0.0;
    uint16_t value = 0u;
};

struct Loop {
    std::vector<Point> points;
    double lengthSeconds = 0.0;
    uint32_t cursor = 0u;
    uint64_t cycleStartFrame = 0u;
    uint64_t nextEventFrame = 0u;
    uint64_t suppressUntilFrame = 0u;
};

struct ScheduledLoop {
    uint64_t frame = 0u;
    uint32_t index = 0u;
};

struct GestureSnapshot {
    bool playing = false;
    double takeoverMs = 650.0;
    std::array<Loop, kNimParameterCount> loops {};
};

struct ImportedGestureSession {
    std::array<Loop, kNimParameterCount> loops {};
    double takeoverMs = 650.0;
    int32_t lastTouchedIndex = -1;
};

struct NrpnDecoder {
    uint8_t parameterMsb = 0u;
    uint8_t parameterLsb = 0u;
    uint8_t dataMsb = 0u;
    uint8_t dataLsb = 0u;
    bool hasParameterMsb = false;
    bool hasParameterLsb = false;
    bool hasDataMsb = false;
    bool pendingMsb = false;
    uint32_t pendingTime = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint64_t framePosition = 0u;
    bool active = false;
    bool recording = false;
    bool playing = false;
    double takeoverMs = 650.0;
    uint64_t recordStartFrame = 0u;
    int32_t lastTouchedIndex = -1;
    std::array<Loop, kNimParameterCount> loops {};
    std::array<std::vector<Point>, kNimParameterCount> take {};
    std::array<bool, kNimParameterCount> takeTouched {};
    std::array<ScheduledLoop, kNimParameterCount> playbackHeap {};
    uint32_t playbackHeapSize = 0u;
    std::atomic<uint64_t> droppedRecordPoints { 0u };
    NrpnDecoder nrpn {};
    std::array<std::atomic<uint16_t>, kNimParameterCount> uiValues {};
    std::array<std::atomic<uint8_t>, kNimParameterCount> uiFlags {};
    std::array<std::atomic<float>, kNimParameterCount> uiLoopLengths {};
    std::atomic<bool> uiRecording { false };
    std::atomic<bool> uiPlaying { false };
    std::atomic<double> uiTakeoverMs { 650.0 };
    std::atomic<uint32_t> uiLoopCount { 0u };
    std::atomic<double> uiLastLength { 0.0 };
    std::atomic<int32_t> uiSelectedIndex { -1 };
    std::atomic<uint32_t> guiCommands { 0u };
    std::atomic<double> guiTakeoverMs { 650.0 };
    // File operations may happen while the standalone audio callback runs.
    // The callback never blocks: it passes input through if this lock is held.
    mutable std::mutex sessionMutex;
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

int32_t parameterIndex(uint16_t id)
{
    for (uint32_t index = 0u; index < kGlobalParameterCount; ++index) {
        if (kNimGlobalParameterIds[index] == id)
            return static_cast<int32_t>(index);
    }
    if (id >= 100u && id <= 163u) {
        return static_cast<int32_t>(kGlobalParameterCount + id - 100u);
    }
    if (id < 1000u || id > 1799u) return -1;
    const uint32_t lane = (id - 1000u) / 100u;
    const uint32_t offset = (id - 1000u) % 100u;
    if (lane >= kLaneCount) return -1;
    uint32_t laneIndex = 0u;
    if (offset <= 16u) {
        laneIndex = offset;
    } else if (offset >= 20u && offset <= 45u
        && (offset % 10u) <= 5u) {
        const uint32_t slot = (offset - 20u) / 10u;
        if (slot >= 3u) return -1;
        laneIndex = 17u + slot * 6u + offset % 10u;
    } else {
        return -1;
    }
    return static_cast<int32_t>(kGlobalParameterCount
        + kMatrixParameterCount + lane * kLaneParameterCount + laneIndex);
}

uint16_t parameterId(uint32_t index)
{
    if (index < kGlobalParameterCount) {
        return kNimGlobalParameterIds[index];
    }
    index -= kGlobalParameterCount;
    if (index < kMatrixParameterCount) {
        return static_cast<uint16_t>(100u + index);
    }
    index -= kMatrixParameterCount;
    const uint32_t lane = index / kLaneParameterCount;
    const uint32_t laneIndex = index % kLaneParameterCount;
    const uint32_t offset = laneIndex <= 16u ? laneIndex
        : 20u + ((laneIndex - 17u) / 6u) * 10u
            + (laneIndex - 17u) % 6u;
    return static_cast<uint16_t>(1000u + lane * 100u + offset);
}

uint64_t secondsToFrames(const Plugin& plugin, double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0) return 0u;
    const long double frames = static_cast<long double>(seconds)
        * static_cast<long double>(plugin.sampleRate);
    return static_cast<uint64_t>(std::min<long double>(
        std::llround(frames), std::numeric_limits<uint64_t>::max()));
}

uint64_t loopLengthFrames(const Plugin& plugin, const Loop& loop)
{
    return std::max<uint64_t>(1u,
        secondsToFrames(plugin, loop.lengthSeconds));
}

uint32_t loopCount(const Plugin& plugin)
{
    uint32_t count = 0u;
    for (const auto& loop : plugin.loops) {
        if (!loop.points.empty()) ++count;
    }
    return count;
}

double lastLoopLength(const Plugin& plugin)
{
    if (plugin.lastTouchedIndex < 0
        || plugin.lastTouchedIndex >= static_cast<int32_t>(kNimParameterCount)) {
        return 0.0;
    }
    return plugin.loops[static_cast<uint32_t>(
        plugin.lastTouchedIndex)].lengthSeconds;
}

void syncUiIndex(Plugin& plugin, uint32_t index)
{
    if (index >= kNimParameterCount) return;
    uint8_t flags = plugin.uiFlags[index].load(std::memory_order_relaxed)
        & kUiValueSeen;
    if (!plugin.loops[index].points.empty()) flags |= kUiLoopActive;
    if (plugin.takeTouched[index]) flags |= kUiTakeTouched;
    plugin.uiFlags[index].store(flags, std::memory_order_relaxed);
    plugin.uiLoopLengths[index].store(static_cast<float>(
        plugin.loops[index].lengthSeconds), std::memory_order_relaxed);
}

void syncUiSummary(Plugin& plugin)
{
    plugin.uiRecording.store(plugin.recording, std::memory_order_relaxed);
    plugin.uiPlaying.store(plugin.playing, std::memory_order_relaxed);
    plugin.uiTakeoverMs.store(plugin.takeoverMs, std::memory_order_relaxed);
    plugin.uiLoopCount.store(loopCount(plugin), std::memory_order_relaxed);
    plugin.uiLastLength.store(lastLoopLength(plugin),
        std::memory_order_relaxed);
}

void syncAllUi(Plugin& plugin)
{
    for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
        syncUiIndex(plugin, index);
    }
    syncUiSummary(plugin);
}

void showUiValue(Plugin& plugin, uint32_t index, uint16_t value)
{
    if (index >= kNimParameterCount) return;
    plugin.uiValues[index].store(value, std::memory_order_relaxed);
    plugin.uiFlags[index].fetch_or(kUiValueSeen, std::memory_order_relaxed);
}

bool pushEvent(const clap_output_events_t* output,
    const clap_event_header_t* event)
{
    return output && output->try_push && event
        && output->try_push(output, event);
}

bool emitMidi(const clap_output_events_t* output, uint32_t time,
    uint8_t status, uint8_t dataOne, uint8_t dataTwo, uint32_t flags = 0u)
{
    clap_event_midi_t event {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.header.flags = flags;
    event.port_index = 0u;
    event.data[0] = status;
    event.data[1] = dataOne;
    event.data[2] = dataTwo;
    return pushEvent(output, &event.header);
}

void emitNrpn(const clap_output_events_t* output, uint32_t time,
    uint16_t id, uint16_t value, uint32_t flags = 0u)
{
    const uint8_t status = static_cast<uint8_t>(0xb0u | kControlChannel);
    emitMidi(output, time, status, 99u,
        static_cast<uint8_t>((id >> 7u) & 0x7fu), flags);
    emitMidi(output, time, status, 98u,
        static_cast<uint8_t>(id & 0x7fu), flags);
    emitMidi(output, time, status, 6u,
        static_cast<uint8_t>((value >> 7u) & 0x7fu), flags);
    emitMidi(output, time, status, 38u,
        static_cast<uint8_t>(value & 0x7fu), flags);
}

void emitParam(const clap_output_events_t* output, uint32_t time,
    clap_id id, double value)
{
    clap_event_param_value_t event {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = CLAP_EVENT_IS_LIVE | CLAP_EVENT_DONT_RECORD;
    event.param_id = id;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    pushEvent(output, &event.header);
}

void prepareLoop(Plugin& plugin, Loop& loop, uint64_t origin)
{
    loop.cursor = 0u;
    loop.cycleStartFrame = origin;
    loop.suppressUntilFrame = 0u;
    loop.nextEventFrame = loop.points.empty() ? 0u
        : origin + secondsToFrames(plugin, loop.points.front().seconds);
}

void prepareAllLoops(Plugin& plugin, uint64_t origin)
{
    for (auto& loop : plugin.loops) prepareLoop(plugin, loop, origin);
}

void clearLoopContents(Loop& loop)
{
    loop.points.clear();
    loop.lengthSeconds = 0.0;
    loop.cursor = 0u;
    loop.cycleStartFrame = 0u;
    loop.nextEventFrame = 0u;
    loop.suppressUntilFrame = 0u;
}

bool prepareRealtimeStorage(Plugin& plugin)
{
    try {
        for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
            plugin.loops[index].points.reserve(
                std::max<size_t>(kRealtimePointsPerParameter,
                    plugin.loops[index].points.size()));
            plugin.take[index].reserve(
                std::max<size_t>(kRealtimePointsPerParameter,
                    plugin.take[index].size()));
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool scheduledBefore(const ScheduledLoop& left, const ScheduledLoop& right)
{
    return left.frame < right.frame
        || (left.frame == right.frame && left.index < right.index);
}

void clearPlaybackSchedule(Plugin& plugin)
{
    plugin.playbackHeapSize = 0u;
}

void pushPlaybackSchedule(Plugin& plugin, ScheduledLoop scheduled)
{
    if (plugin.playbackHeapSize >= plugin.playbackHeap.size()) return;
    uint32_t child = plugin.playbackHeapSize++;
    plugin.playbackHeap[child] = scheduled;
    while (child > 0u) {
        const uint32_t parent = (child - 1u) / 2u;
        if (!scheduledBefore(plugin.playbackHeap[child],
                plugin.playbackHeap[parent])) {
            break;
        }
        std::swap(plugin.playbackHeap[child], plugin.playbackHeap[parent]);
        child = parent;
    }
}

ScheduledLoop popPlaybackSchedule(Plugin& plugin)
{
    const ScheduledLoop result = plugin.playbackHeap[0u];
    --plugin.playbackHeapSize;
    if (plugin.playbackHeapSize == 0u) return result;
    plugin.playbackHeap[0u] = plugin.playbackHeap[plugin.playbackHeapSize];
    uint32_t parent = 0u;
    for (;;) {
        const uint32_t left = parent * 2u + 1u;
        if (left >= plugin.playbackHeapSize) break;
        const uint32_t right = left + 1u;
        uint32_t child = left;
        if (right < plugin.playbackHeapSize
            && scheduledBefore(plugin.playbackHeap[right],
                plugin.playbackHeap[left])) {
            child = right;
        }
        if (!scheduledBefore(plugin.playbackHeap[child],
                plugin.playbackHeap[parent])) {
            break;
        }
        std::swap(plugin.playbackHeap[parent], plugin.playbackHeap[child]);
        parent = child;
    }
    return result;
}

void rebuildPlaybackSchedule(Plugin& plugin)
{
    clearPlaybackSchedule(plugin);
    if (!plugin.playing) return;
    for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
        const auto& loop = plugin.loops[index];
        if (!loop.points.empty()) {
            pushPlaybackSchedule(plugin, { loop.nextEventFrame, index });
        }
    }
}

void beginRecording(Plugin& plugin, uint64_t absoluteFrame)
{
    if (plugin.recording) return;
    plugin.recording = true;
    plugin.recordStartFrame = absoluteFrame;
    plugin.takeTouched.fill(false);
    for (auto& points : plugin.take) points.clear();
    syncAllUi(plugin);
}

void cancelRecording(Plugin& plugin)
{
    plugin.recording = false;
    plugin.takeTouched.fill(false);
    for (auto& points : plugin.take) points.clear();
    syncAllUi(plugin);
}

void stopRecording(Plugin& plugin, uint64_t absoluteFrame)
{
    if (!plugin.recording) return;
    const uint64_t durationFrames = std::max<uint64_t>(
        1u, absoluteFrame > plugin.recordStartFrame
            ? absoluteFrame - plugin.recordStartFrame : 1u);
    const double lengthSeconds = static_cast<double>(durationFrames)
        / plugin.sampleRate;
    for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
        if (!plugin.takeTouched[index] || plugin.take[index].empty()) continue;
        auto& loop = plugin.loops[index];
        loop.points.swap(plugin.take[index]);
        loop.lengthSeconds = lengthSeconds;
        prepareLoop(plugin, loop, absoluteFrame);
    }
    plugin.recording = false;
    plugin.takeTouched.fill(false);
    rebuildPlaybackSchedule(plugin);
    syncAllUi(plugin);
}

void setRecording(Plugin& plugin, bool enabled, uint64_t absoluteFrame)
{
    if (enabled) beginRecording(plugin, absoluteFrame);
    else stopRecording(plugin, absoluteFrame);
}

void setPlaying(Plugin& plugin, bool enabled, uint64_t absoluteFrame)
{
    if (plugin.playing == enabled) return;
    plugin.playing = enabled;
    if (enabled) prepareAllLoops(plugin, absoluteFrame);
    rebuildPlaybackSchedule(plugin);
    syncUiSummary(plugin);
}

void clearLastLoop(Plugin& plugin)
{
    if (plugin.lastTouchedIndex < 0
        || plugin.lastTouchedIndex >= static_cast<int32_t>(kNimParameterCount)) {
        return;
    }
    const uint32_t index = static_cast<uint32_t>(plugin.lastTouchedIndex);
    clearLoopContents(plugin.loops[index]);
    rebuildPlaybackSchedule(plugin);
    syncUiIndex(plugin, index);
    syncUiSummary(plugin);
}

void clearLoop(Plugin& plugin, uint32_t index)
{
    if (index >= kNimParameterCount) return;
    clearLoopContents(plugin.loops[index]);
    plugin.take[index].clear();
    plugin.takeTouched[index] = false;
    plugin.lastTouchedIndex = static_cast<int32_t>(index);
    rebuildPlaybackSchedule(plugin);
    syncUiIndex(plugin, index);
    syncUiSummary(plugin);
}

void clearAllLoops(Plugin& plugin)
{
    for (auto& loop : plugin.loops) clearLoopContents(loop);
    clearPlaybackSchedule(plugin);
    plugin.lastTouchedIndex = -1;
    cancelRecording(plugin);
    syncAllUi(plugin);
}

void recordValue(Plugin& plugin, uint32_t index, uint16_t value,
    uint64_t absoluteFrame)
{
    if (!plugin.recording || index >= kNimParameterCount) return;
    plugin.takeTouched[index] = true;
    syncUiIndex(plugin, index);
    auto& points = plugin.take[index];
    const double seconds = absoluteFrame >= plugin.recordStartFrame
        ? static_cast<double>(absoluteFrame - plugin.recordStartFrame)
            / plugin.sampleRate
        : 0.0;
    if (!points.empty() && points.back().seconds == seconds) {
        points.back().value = value;
        return;
    }
    if (!points.empty() && points.back().value == value) return;
    if (points.size() < points.capacity()) {
        points.push_back({ seconds, value });
        return;
    }
    // Preserve the newest gesture endpoint while remaining allocation-free.
    // The overflow is observable in diagnostics even though the file format
    // and published parameter surface remain unchanged.
    if (!points.empty()) points.back() = { seconds, value };
    plugin.droppedRecordPoints.fetch_add(1u, std::memory_order_relaxed);
}

void acceptNrpnValue(Plugin& plugin, uint16_t id, uint16_t value,
    uint32_t time, uint64_t absoluteFrame,
    const clap_output_events_t* output)
{
    const int32_t index = parameterIndex(id);
    if (index >= 0) {
        plugin.lastTouchedIndex = index;
        plugin.uiSelectedIndex.store(index, std::memory_order_relaxed);
        showUiValue(plugin, static_cast<uint32_t>(index), value);
        recordValue(plugin, static_cast<uint32_t>(index), value,
            absoluteFrame);
        auto& loop = plugin.loops[static_cast<uint32_t>(index)];
        loop.suppressUntilFrame = absoluteFrame + secondsToFrames(
            plugin, plugin.takeoverMs * 0.001);
        syncUiSummary(plugin);
    }
    emitNrpn(output, time, id, value,
        CLAP_EVENT_IS_LIVE | CLAP_EVENT_DONT_RECORD);
}

bool decoderHasParameter(const NrpnDecoder& decoder)
{
    return decoder.hasParameterMsb && decoder.hasParameterLsb;
}

uint16_t decoderParameter(const NrpnDecoder& decoder)
{
    return static_cast<uint16_t>((decoder.parameterMsb << 7u)
        | decoder.parameterLsb);
}

void flushPendingNrpn(Plugin& plugin, uint64_t blockStart,
    const clap_output_events_t* output)
{
    auto& decoder = plugin.nrpn;
    if (!decoder.pendingMsb || !decoderHasParameter(decoder)
        || !decoder.hasDataMsb) {
        decoder.pendingMsb = false;
        return;
    }
    const uint16_t value = static_cast<uint16_t>(decoder.dataMsb << 7u);
    acceptNrpnValue(plugin, decoderParameter(decoder), value,
        decoder.pendingTime, blockStart + decoder.pendingTime, output);
    decoder.pendingMsb = false;
}

bool handleCommandNote(Plugin& plugin, uint8_t note, bool pressed,
    uint64_t absoluteFrame, uint32_t time,
    const clap_output_events_t* output)
{
    if (note < kRecordNote || note > kCancelRecordNote) return false;
    if (!pressed) return true;
    switch (note) {
    case kRecordNote:
        setRecording(plugin, !plugin.recording, absoluteFrame);
        emitParam(output, time, kRecordParamId, plugin.recording ? 1.0 : 0.0);
        emitParam(output, time, kLoopCountParamId, loopCount(plugin));
        emitParam(output, time, kLastLengthParamId, lastLoopLength(plugin));
        break;
    case kPlayNote:
        setPlaying(plugin, !plugin.playing, absoluteFrame);
        emitParam(output, time, kPlayParamId, plugin.playing ? 1.0 : 0.0);
        break;
    case kClearLastNote:
        clearLastLoop(plugin);
        emitParam(output, time, kLoopCountParamId, loopCount(plugin));
        emitParam(output, time, kLastLengthParamId, lastLoopLength(plugin));
        break;
    case kClearAllNote:
        clearAllLoops(plugin);
        emitParam(output, time, kRecordParamId, 0.0);
        emitParam(output, time, kLoopCountParamId, 0.0);
        emitParam(output, time, kLastLengthParamId, 0.0);
        break;
    case kCancelRecordNote:
        cancelRecording(plugin);
        emitParam(output, time, kRecordParamId, 0.0);
        break;
    default:
        break;
    }
    return true;
}

bool handleMidiInput(Plugin& plugin, const clap_event_midi_t& midi,
    uint64_t blockStart, const clap_output_events_t* output)
{
    const uint8_t status = midi.data[0] & 0xf0u;
    const uint8_t channel = midi.data[0] & 0x0fu;
    if (channel != kControlChannel) return false;

    const uint64_t absoluteFrame = blockStart + midi.header.time;
    if (status == 0x90u || status == 0x80u) {
        const uint8_t note = midi.data[1] & 0x7fu;
        const bool pressed = status == 0x90u && (midi.data[2] & 0x7fu) != 0u;
        if (handleCommandNote(plugin, note, pressed, absoluteFrame,
                midi.header.time, output)) {
            return true;
        }
        return false;
    }
    if (status != 0xb0u) return false;

    const uint8_t controller = midi.data[1] & 0x7fu;
    const uint8_t value = midi.data[2] & 0x7fu;
    auto& decoder = plugin.nrpn;
    switch (controller) {
    case 99u:
        flushPendingNrpn(plugin, blockStart, output);
        decoder.parameterMsb = value;
        decoder.hasParameterMsb = true;
        return true;
    case 98u:
        flushPendingNrpn(plugin, blockStart, output);
        decoder.parameterLsb = value;
        decoder.hasParameterLsb = true;
        return true;
    case 6u:
        if (!decoderHasParameter(decoder)) return false;
        flushPendingNrpn(plugin, blockStart, output);
        decoder.dataMsb = value;
        decoder.dataLsb = 0u;
        decoder.hasDataMsb = true;
        decoder.pendingMsb = true;
        decoder.pendingTime = midi.header.time;
        return true;
    case 38u:
        if (!decoderHasParameter(decoder) || !decoder.hasDataMsb) return false;
        decoder.dataLsb = value;
        decoder.pendingMsb = false;
        acceptNrpnValue(plugin, decoderParameter(decoder),
            static_cast<uint16_t>((decoder.dataMsb << 7u) | value),
            midi.header.time, absoluteFrame, output);
        return true;
    case 100u:
    case 101u:
        flushPendingNrpn(plugin, blockStart, output);
        decoder.hasParameterMsb = false;
        decoder.hasParameterLsb = false;
        decoder.hasDataMsb = false;
        return false;
    default:
        flushPendingNrpn(plugin, blockStart, output);
        return false;
    }
}

void applyParameter(Plugin& plugin, clap_id id, double value,
    uint64_t absoluteFrame)
{
    switch (id) {
    case kRecordParamId:
        setRecording(plugin, value >= 0.5, absoluteFrame);
        break;
    case kPlayParamId:
        setPlaying(plugin, value >= 0.5, absoluteFrame);
        break;
    case kClearLastParamId:
        if (value >= 0.5) clearLastLoop(plugin);
        break;
    case kClearAllParamId:
        if (value >= 0.5) clearAllLoops(plugin);
        break;
    case kTakeoverParamId:
        if (std::isfinite(value)) {
            plugin.takeoverMs = std::clamp(value, 0.0, 5000.0);
            syncUiSummary(plugin);
        }
        break;
    default:
        break;
    }
}

void requestGuiCommand(Plugin& plugin, uint32_t command)
{
    plugin.guiCommands.fetch_or(command, std::memory_order_release);
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void applyGuiCommands(Plugin& plugin, uint64_t absoluteFrame,
    const clap_output_events_t* output)
{
    const uint32_t commands = plugin.guiCommands.exchange(
        0u, std::memory_order_acq_rel);
    if (commands == 0u) return;

    if ((commands & kGuiToggleRecord) != 0u) {
        setRecording(plugin, !plugin.recording, absoluteFrame);
        emitParam(output, 0u, kRecordParamId,
            plugin.recording ? 1.0 : 0.0);
    }
    if ((commands & kGuiTogglePlay) != 0u) {
        setPlaying(plugin, !plugin.playing, absoluteFrame);
        emitParam(output, 0u, kPlayParamId,
            plugin.playing ? 1.0 : 0.0);
    }
    if ((commands & kGuiClearSelected) != 0u) {
        const int32_t selected = plugin.uiSelectedIndex.load(
            std::memory_order_relaxed);
        if (selected >= 0
            && selected < static_cast<int32_t>(kNimParameterCount)) {
            clearLoop(plugin, static_cast<uint32_t>(selected));
        } else {
            clearLastLoop(plugin);
        }
    }
    if ((commands & kGuiClearAll) != 0u) {
        clearAllLoops(plugin);
        emitParam(output, 0u, kRecordParamId, 0.0);
    }
    if ((commands & kGuiCancelRecord) != 0u) {
        cancelRecording(plugin);
        emitParam(output, 0u, kRecordParamId, 0.0);
    }
    if ((commands & kGuiSetTakeover) != 0u) {
        plugin.takeoverMs = std::clamp(plugin.guiTakeoverMs.load(
            std::memory_order_relaxed), 0.0, 5000.0);
        syncUiSummary(plugin);
        emitParam(output, 0u, kTakeoverParamId, plugin.takeoverMs);
    }
    emitParam(output, 0u, kLoopCountParamId, loopCount(plugin));
    emitParam(output, 0u, kLastLengthParamId, lastLoopLength(plugin));
}

bool applyParamEvent(Plugin& plugin, const clap_event_header_t* event,
    uint64_t blockStart)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
        || event->type != CLAP_EVENT_PARAM_VALUE
        || event->size < sizeof(clap_event_param_value_t)) {
        return false;
    }
    const auto* parameter =
        reinterpret_cast<const clap_event_param_value_t*>(event);
    applyParameter(plugin, parameter->param_id, parameter->value,
        blockStart + event->time);
    return true;
}

void processInputEvent(Plugin& plugin, const clap_event_header_t* event,
    uint64_t blockStart, const clap_output_events_t* output)
{
    if (!event) return;
    bool completesPendingNrpn = false;
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID
        && event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        completesPendingNrpn = (midi->data[0] & 0xffu)
                == static_cast<uint8_t>(0xb0u | kControlChannel)
            && (midi->data[1] & 0x7fu) == 38u;
    }
    if (plugin.nrpn.pendingMsb && !completesPendingNrpn) {
        flushPendingNrpn(plugin, blockStart, output);
    }
    if (applyParamEvent(plugin, event, blockStart)) return;
    if (event->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        pushEvent(output, event);
        return;
    }
    if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if (!handleMidiInput(plugin, *midi, blockStart, output)) {
            pushEvent(output, event);
        }
        return;
    }
    if ((event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF)
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (note->channel == kControlChannel
            && handleCommandNote(plugin, static_cast<uint8_t>(
                std::clamp<int32_t>(note->key, 0, 127)),
                event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0,
                blockStart + event->time, event->time, output)) {
            return;
        }
    }
    pushEvent(output, event);
}

bool isGestureCommandEvent(const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* parameter =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        return parameter->param_id >= kRecordParamId
            && parameter->param_id <= kLastLengthParamId;
    }
    if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        const uint8_t status = midi->data[0] & 0xf0u;
        const uint8_t channel = midi->data[0] & 0x0fu;
        const uint8_t note = midi->data[1] & 0x7fu;
        return channel == kControlChannel
            && (status == 0x80u || status == 0x90u)
            && note >= kRecordNote && note <= kCancelRecordNote;
    }
    if ((event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF)
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        return note->channel == kControlChannel
            && note->key >= kRecordNote && note->key <= kCancelRecordNote;
    }
    return false;
}

void passThroughWhileSessionBusy(const clap_input_events_t* input,
    const clap_output_events_t* output)
{
    if (!input || !input->size || !input->get) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = input->get(input, index);
        if (!isGestureCommandEvent(event)) pushEvent(output, event);
    }
}

void dispatchScheduledLoop(Plugin& plugin, uint32_t index,
    uint64_t absoluteFrame, uint32_t outputTime,
    const clap_output_events_t* output)
{
    auto& loop = plugin.loops[index];
    if (loop.points.empty()) return;
    const uint64_t length = loopLengthFrames(plugin, loop);
    uint32_t guard = 0u;
    while (loop.nextEventFrame <= absoluteFrame
        && guard++ <= loop.points.size()) {
        if (loop.nextEventFrame == absoluteFrame
            && absoluteFrame >= loop.suppressUntilFrame
            && !plugin.takeTouched[index]) {
            const uint16_t value = loop.points[loop.cursor].value;
            showUiValue(plugin, index, value);
            emitNrpn(output, outputTime, parameterId(index), value);
        }
        ++loop.cursor;
        if (loop.cursor >= loop.points.size()) {
            loop.cursor = 0u;
            loop.cycleStartFrame += length;
        }
        loop.nextEventFrame = loop.cycleStartFrame + secondsToFrames(
            plugin, loop.points[loop.cursor].seconds);
    }
    if (!loop.points.empty()) {
        pushPlaybackSchedule(plugin, { loop.nextEventFrame, index });
    }
}

bool init(const clap_plugin_t*) { return true; }

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* p = self(plugin);
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) return false;
    if (!prepareRealtimeStorage(*p)) return false;
    p->sampleRate = sampleRate;
    p->framePosition = 0u;
    p->active = true;
    p->nrpn = {};
    cancelRecording(*p);
    prepareAllLoops(*p, 0u);
    rebuildPlaybackSchedule(*p);
    p->droppedRecordPoints.store(0u, std::memory_order_relaxed);
    syncAllUi(*p);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->active = false;
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->framePosition = 0u;
    p->nrpn = {};
    cancelRecording(*p);
    prepareAllLoops(*p, 0u);
    rebuildPlaybackSchedule(*p);
    syncAllUi(*p);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    if (!processData) return CLAP_PROCESS_CONTINUE;
    auto* p = self(plugin);
    std::unique_lock<std::mutex> sessionLock(
        p->sessionMutex, std::try_to_lock);
    if (!sessionLock.owns_lock()) {
        passThroughWhileSessionBusy(processData->in_events,
            processData->out_events);
        return CLAP_PROCESS_CONTINUE;
    }
    const uint64_t blockStart = p->framePosition;
    applyGuiCommands(*p, blockStart, processData->out_events);
    const uint32_t eventCount = processData->in_events
        ? processData->in_events->size(processData->in_events) : 0u;
    uint32_t eventIndex = 0u;
    const uint64_t blockEnd = blockStart + processData->frames_count;

    // Merge host input and recorded events by timestamp. The fixed min-heap
    // visits only active loops when an event is actually due; the previous
    // implementation checked all 402 parameters for every audio frame.
    for (;;) {
        const clap_event_header_t* inputEvent = nullptr;
        while (eventIndex < eventCount && !inputEvent) {
            inputEvent = processData->in_events->get(
                processData->in_events, eventIndex);
            if (!inputEvent) ++eventIndex;
        }
        const bool inputInBlock = inputEvent
            && inputEvent->time < processData->frames_count;
        const uint64_t inputFrame = inputInBlock
            ? blockStart + inputEvent->time
            : std::numeric_limits<uint64_t>::max();
        const bool playbackInBlock = p->playing
            && p->playbackHeapSize > 0u
            && p->playbackHeap[0u].frame < blockEnd;
        const uint64_t playbackFrame = playbackInBlock
            ? p->playbackHeap[0u].frame
            : std::numeric_limits<uint64_t>::max();
        if (!inputInBlock && !playbackInBlock) break;

        // Live input wins ties, matching the old frame loop and ensuring that
        // takeover suppresses a recorded point scheduled for the same sample.
        if (inputFrame <= playbackFrame) {
            processInputEvent(*p, inputEvent, blockStart,
                processData->out_events);
            ++eventIndex;
            continue;
        }

        const ScheduledLoop scheduled = popPlaybackSchedule(*p);
        const uint32_t outputTime = static_cast<uint32_t>(std::min<uint64_t>(
            processData->frames_count - 1u,
            scheduled.frame > blockStart
                ? scheduled.frame - blockStart : 0u));
        dispatchScheduledLoop(*p, scheduled.index,
            std::max(blockStart, scheduled.frame),
            outputTime, processData->out_events);
    }
    while (eventIndex < eventCount) {
        const clap_event_header_t* event = processData->in_events->get(
            processData->in_events, eventIndex++);
        processInputEvent(*p, event, blockStart, processData->out_events);
    }
    flushPendingNrpn(*p, blockStart, processData->out_events);
    p->framePosition += processData->frames_count;
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t notePortsCount(const clap_plugin_t*, bool) { return 1u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || index != 0u) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = isInput ? 30u : 31u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "E16 / NIM MIDI In" : "NIM + E16 Feedback Out");
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet,
};

struct ParameterDefinition {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    uint32_t flags;
};

constexpr ParameterDefinition kParameterDefinitions[] {
    { kRecordParamId, "Record", 0.0, 1.0, 0.0,
        CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE },
    { kPlayParamId, "Playback", 0.0, 1.0, 0.0,
        CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE },
    { kClearLastParamId, "Clear Last", 0.0, 1.0, 0.0,
        CLAP_PARAM_IS_STEPPED },
    { kClearAllParamId, "Clear All", 0.0, 1.0, 0.0,
        CLAP_PARAM_IS_STEPPED },
    { kTakeoverParamId, "Live Takeover", 0.0, 5000.0, 650.0,
        CLAP_PARAM_IS_AUTOMATABLE },
    { kLoopCountParamId, "Loop Count", 0.0,
        static_cast<double>(kNimParameterCount), 0.0,
        CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_READONLY },
    { kLastLengthParamId, "Last Loop Length", 0.0,
        kMaximumLoopSeconds, 0.0, CLAP_PARAM_IS_READONLY },
};

const ParameterDefinition* parameterDefinition(clap_id id)
{
    for (const auto& definition : kParameterDefinitions) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(std::size(kParameterDefinitions));
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& definition = kParameterDefinitions[index];
    std::memset(info, 0, sizeof(*info));
    info->id = definition.id;
    info->flags = definition.flags;
    info->min_value = definition.minimum;
    info->max_value = definition.maximum;
    info->default_value = definition.defaultValue;
    std::snprintf(info->name, sizeof(info->name), "%s", definition.name);
    std::snprintf(info->module, sizeof(info->module), "NIM Gesture");
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* p = self(plugin);
    switch (id) {
    case kRecordParamId:
        *value = p->uiRecording.load(std::memory_order_relaxed) ? 1.0 : 0.0;
        return true;
    case kPlayParamId:
        *value = p->uiPlaying.load(std::memory_order_relaxed) ? 1.0 : 0.0;
        return true;
    case kClearLastParamId:
    case kClearAllParamId: *value = 0.0; return true;
    case kTakeoverParamId:
        *value = p->uiTakeoverMs.load(std::memory_order_relaxed); return true;
    case kLoopCountParamId:
        *value = p->uiLoopCount.load(std::memory_order_relaxed); return true;
    case kLastLengthParamId:
        *value = p->uiLastLength.load(std::memory_order_relaxed); return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !parameterDefinition(id)) return false;
    switch (id) {
    case kRecordParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "RECORDING" : "READY");
        break;
    case kPlayParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "PLAY" : "PAUSE");
        break;
    case kClearLastParamId:
    case kClearAllParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "CLEAR" : "-");
        break;
    case kTakeoverParamId:
        std::snprintf(display, size, "%.0f ms", value);
        break;
    case kLoopCountParamId:
        std::snprintf(display, size, "%u",
            static_cast<uint32_t>(std::max(0.0, std::round(value))));
        break;
    case kLastLengthParamId:
        std::snprintf(display, size, "%.3f s", value);
        break;
    default:
        return false;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !parameterDefinition(id)) return false;
    if (id == kRecordParamId) {
        *value = std::strcmp(display, "RECORDING") == 0 ? 1.0 : 0.0;
        return true;
    }
    if (id == kPlayParamId) {
        *value = std::strcmp(display, "PAUSE") == 0 ? 0.0 : 1.0;
        return true;
    }
    if (id == kClearLastParamId || id == kClearAllParamId) {
        *value = std::strcmp(display, "CLEAR") == 0 ? 1.0 : 0.0;
        return true;
    }
    *value = std::strtod(display, nullptr);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* p = self(plugin);
    std::unique_lock<std::mutex> sessionLock(
        p->sessionMutex, std::try_to_lock);
    if (!sessionLock.owns_lock()) {
        passThroughWhileSessionBusy(input, output);
        return;
    }
    applyGuiCommands(*p, p->framePosition, output);
    if (!input) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        processInputEvent(*p, input->get(input, index), p->framePosition,
            output);
    }
    flushPendingNrpn(*p, p->framePosition, output);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool writeExact(const clap_ostream_t* stream, const void* source,
    uint64_t size)
{
    if (!stream || !stream->write || (!source && size != 0u)) return false;
    const auto* bytes = static_cast<const uint8_t*>(source);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t written = stream->write(stream, bytes + offset,
            size - offset);
        if (written <= 0
            || static_cast<uint64_t>(written) > size - offset) {
            return false;
        }
        offset += static_cast<uint64_t>(written);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* destination,
    uint64_t size)
{
    if (!stream || !stream->read || (!destination && size != 0u)) return false;
    auto* bytes = static_cast<uint8_t*>(destination);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t read = stream->read(stream, bytes + offset,
            size - offset);
        if (read <= 0 || static_cast<uint64_t>(read) > size - offset)
            return false;
        offset += static_cast<uint64_t>(read);
    }
    return true;
}

void encodeU16Le(uint16_t value, uint8_t* bytes)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffu);
    bytes[1] = static_cast<uint8_t>((value >> 8u) & 0xffu);
}

void encodeU32Le(uint32_t value, uint8_t* bytes)
{
    for (uint32_t index = 0u; index < 4u; ++index)
        bytes[index] = static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
}

void encodeU64Le(uint64_t value, uint8_t* bytes)
{
    for (uint32_t index = 0u; index < 8u; ++index)
        bytes[index] = static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
}

uint16_t decodeU16Le(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0])
        | static_cast<uint16_t>(bytes[1] << 8u);
}

uint32_t decodeU32Le(const uint8_t* bytes)
{
    uint32_t value = 0u;
    for (uint32_t index = 0u; index < 4u; ++index)
        value |= static_cast<uint32_t>(bytes[index]) << (index * 8u);
    return value;
}

uint64_t decodeU64Le(const uint8_t* bytes)
{
    uint64_t value = 0u;
    for (uint32_t index = 0u; index < 8u; ++index)
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8u);
    return value;
}

bool writeU16Le(const clap_ostream_t* stream, uint16_t value)
{
    uint8_t bytes[2u] {};
    encodeU16Le(value, bytes);
    return writeExact(stream, bytes, sizeof(bytes));
}

bool writeU32Le(const clap_ostream_t* stream, uint32_t value)
{
    uint8_t bytes[4u] {};
    encodeU32Le(value, bytes);
    return writeExact(stream, bytes, sizeof(bytes));
}

bool writeU64Le(const clap_ostream_t* stream, uint64_t value)
{
    uint8_t bytes[8u] {};
    encodeU64Le(value, bytes);
    return writeExact(stream, bytes, sizeof(bytes));
}

bool readU16Le(const clap_istream_t* stream, uint16_t& value,
    uint32_t* crc = nullptr);
bool readU32Le(const clap_istream_t* stream, uint32_t& value,
    uint32_t* crc = nullptr);
bool readU64Le(const clap_istream_t* stream, uint64_t& value,
    uint32_t* crc = nullptr);

void updateCrc32(uint32_t& crc, const uint8_t* bytes, size_t size)
{
    for (size_t byteIndex = 0u; byteIndex < size; ++byteIndex) {
        crc ^= bytes[byteIndex];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u)
                ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
}

bool readU16Le(const clap_istream_t* stream, uint16_t& value, uint32_t* crc)
{
    uint8_t bytes[2u] {};
    if (!readExact(stream, bytes, sizeof(bytes))) return false;
    if (crc) updateCrc32(*crc, bytes, sizeof(bytes));
    value = decodeU16Le(bytes);
    return true;
}

bool readU32Le(const clap_istream_t* stream, uint32_t& value, uint32_t* crc)
{
    uint8_t bytes[4u] {};
    if (!readExact(stream, bytes, sizeof(bytes))) return false;
    if (crc) updateCrc32(*crc, bytes, sizeof(bytes));
    value = decodeU32Le(bytes);
    return true;
}

bool readU64Le(const clap_istream_t* stream, uint64_t& value, uint32_t* crc)
{
    uint8_t bytes[8u] {};
    if (!readExact(stream, bytes, sizeof(bytes))) return false;
    if (crc) updateCrc32(*crc, bytes, sizeof(bytes));
    value = decodeU64Le(bytes);
    return true;
}

void updateCrcU16(uint32_t& crc, uint16_t value)
{
    uint8_t bytes[2u] {};
    encodeU16Le(value, bytes);
    updateCrc32(crc, bytes, sizeof(bytes));
}

void updateCrcU32(uint32_t& crc, uint32_t value)
{
    uint8_t bytes[4u] {};
    encodeU32Le(value, bytes);
    updateCrc32(crc, bytes, sizeof(bytes));
}

void updateCrcU64(uint32_t& crc, uint64_t value)
{
    uint8_t bytes[8u] {};
    encodeU64Le(value, bytes);
    updateCrc32(crc, bytes, sizeof(bytes));
}

bool secondsToNanoseconds(double seconds, uint64_t& nanoseconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0
        || seconds > kMaximumLoopSeconds) {
        return false;
    }
    const long double value = static_cast<long double>(seconds)
        * kNanosecondsPerSecond;
    if (value > static_cast<long double>(
            std::numeric_limits<uint64_t>::max())) {
        return false;
    }
    nanoseconds = static_cast<uint64_t>(std::llround(value));
    return true;
}

bool sessionPayloadDescription(const GestureSnapshot& snapshot,
    uint32_t& loopTotal,
    uint32_t& pointTotal, uint64_t& payloadBytes, uint32_t& payloadCrc)
{
    loopTotal = 0u;
    uint64_t points = 0u;
    payloadBytes = 0u;
    uint32_t crc = 0xffffffffu;
    for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
        const auto& loop = snapshot.loops[index];
        if (loop.points.empty()) continue;
        if (loop.points.size() > std::numeric_limits<uint32_t>::max())
            return false;
        const uint32_t count = static_cast<uint32_t>(loop.points.size());
        points += count;
        if (points > kMaximumStatePoints) return false;
        uint64_t lengthNs = 0u;
        if (!secondsToNanoseconds(loop.lengthSeconds, lengthNs)
            || lengthNs == 0u) {
            return false;
        }
        ++loopTotal;
        updateCrcU16(crc, parameterId(index));
        updateCrcU16(crc, 0u);
        updateCrcU32(crc, count);
        updateCrcU64(crc, lengthNs);
        uint64_t previousTime = 0u;
        bool first = true;
        for (const auto& point : loop.points) {
            uint64_t timeNs = 0u;
            if (!secondsToNanoseconds(point.seconds, timeNs)
                || timeNs > lengthNs || point.value > 16383u
                || (!first && timeNs < previousTime)) {
                return false;
            }
            first = false;
            previousTime = timeNs;
            updateCrcU64(crc, timeNs);
            updateCrcU16(crc, point.value);
            updateCrcU16(crc, 0u);
        }
    }
    pointTotal = static_cast<uint32_t>(points);
    payloadBytes = static_cast<uint64_t>(loopTotal)
        * kSessionLoopHeaderBytes + points * kSessionPointBytes;
    payloadCrc = crc ^ 0xffffffffu;
    return true;
}

bool sessionSaveSnapshot(const GestureSnapshot& snapshot,
    const clap_ostream_t* stream)
{
    if (!std::isfinite(snapshot.takeoverMs)) return false;
    uint32_t loopTotal = 0u;
    uint32_t pointTotal = 0u;
    uint64_t payloadBytes = 0u;
    uint32_t payloadCrc = 0u;
    if (!sessionPayloadDescription(snapshot, loopTotal, pointTotal,
            payloadBytes, payloadCrc)) {
        return false;
    }
    const uint32_t takeoverMicros = static_cast<uint32_t>(std::llround(
        std::clamp(snapshot.takeoverMs, 0.0, 5000.0) * 1000.0));
    if (!writeExact(stream, kSessionMagic.data(), kSessionMagic.size())
        || !writeU16Le(stream, kSessionVersion)
        || !writeU16Le(stream, kSessionHeaderBytes)
        || !writeU32Le(stream, kSessionFlags)
        || !writeU32Le(stream, loopTotal)
        || !writeU32Le(stream, pointTotal)
        || !writeU64Le(stream, payloadBytes)
        || !writeU32Le(stream, payloadCrc)
        || !writeU32Le(stream, takeoverMicros)) {
        return false;
    }
    for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
        const auto& loop = snapshot.loops[index];
        if (loop.points.empty()) continue;
        uint64_t lengthNs = 0u;
        if (!secondsToNanoseconds(loop.lengthSeconds, lengthNs)
            || !writeU16Le(stream, parameterId(index))
            || !writeU16Le(stream, 0u)
            || !writeU32Le(stream,
                static_cast<uint32_t>(loop.points.size()))
            || !writeU64Le(stream, lengthNs)) {
            return false;
        }
        for (const auto& point : loop.points) {
            uint64_t timeNs = 0u;
            if (!secondsToNanoseconds(point.seconds, timeNs)
                || !writeU64Le(stream, timeNs)
                || !writeU16Le(stream, point.value)
                || !writeU16Le(stream, 0u)) {
                return false;
            }
        }
    }
    return true;
}

void resetImportedUiValues(Plugin& plugin)
{
    for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
        plugin.uiValues[index].store(0u, std::memory_order_relaxed);
        plugin.uiFlags[index].store(0u, std::memory_order_relaxed);
        plugin.uiLoopLengths[index].store(0.0f, std::memory_order_relaxed);
        if (!plugin.loops[index].points.empty()) {
            showUiValue(plugin, index, plugin.loops[index].points.front().value);
        }
    }
}

bool parseGestureSession(const clap_istream_t* stream,
    ImportedGestureSession& imported)
{
    std::array<uint8_t, 8u> magic {};
    uint16_t version = 0u;
    uint16_t headerBytes = 0u;
    uint32_t flags = 0u;
    uint32_t declaredLoops = 0u;
    uint32_t declaredPoints = 0u;
    uint64_t declaredPayloadBytes = 0u;
    uint32_t declaredCrc = 0u;
    uint32_t takeoverMicros = 0u;
    if (!readExact(stream, magic.data(), magic.size())
        || !readU16Le(stream, version)
        || !readU16Le(stream, headerBytes)
        || !readU32Le(stream, flags)
        || !readU32Le(stream, declaredLoops)
        || !readU32Le(stream, declaredPoints)
        || !readU64Le(stream, declaredPayloadBytes)
        || !readU32Le(stream, declaredCrc)
        || !readU32Le(stream, takeoverMicros)
        || magic != kSessionMagic || version != kSessionVersion
        || headerBytes != kSessionHeaderBytes || flags != kSessionFlags
        || declaredLoops > kNimParameterCount
        || declaredPoints > kMaximumStatePoints
        || takeoverMicros > 5000000u
        || declaredPayloadBytes != static_cast<uint64_t>(declaredLoops)
                * kSessionLoopHeaderBytes
            + static_cast<uint64_t>(declaredPoints) * kSessionPointBytes) {
        return false;
    }

    std::array<Loop, kNimParameterCount> loaded {};
    uint64_t pointsRead = 0u;
    uint64_t payloadRead = 0u;
    uint32_t crc = 0xffffffffu;
    int32_t lastTouched = -1;
    const uint64_t maximumLengthNs = static_cast<uint64_t>(
        kMaximumLoopSeconds * static_cast<double>(kNanosecondsPerSecond));
    for (uint32_t loopIndex = 0u; loopIndex < declaredLoops; ++loopIndex) {
        uint16_t id = 0u;
        uint16_t reserved = 0u;
        uint32_t pointCount = 0u;
        uint64_t lengthNs = 0u;
        if (!readU16Le(stream, id, &crc)
            || !readU16Le(stream, reserved, &crc)
            || !readU32Le(stream, pointCount, &crc)
            || !readU64Le(stream, lengthNs, &crc)) {
            return false;
        }
        payloadRead += kSessionLoopHeaderBytes;
        const int32_t index = parameterIndex(id);
        pointsRead += pointCount;
        if (reserved != 0u || index < 0 || pointCount == 0u
            || pointsRead > declaredPoints || pointsRead > kMaximumStatePoints
            || lengthNs == 0u || lengthNs > maximumLengthNs
            || !loaded[static_cast<uint32_t>(index)].points.empty()) {
            return false;
        }
        auto& loop = loaded[static_cast<uint32_t>(index)];
        loop.lengthSeconds = static_cast<double>(lengthNs)
            / static_cast<double>(kNanosecondsPerSecond);
        // Grow only as point records are actually read. A truncated file must
        // not be able to request a maximum-sized allocation from a tiny input.
        loop.points.reserve(std::min<uint32_t>(pointCount, 4096u));
        uint64_t previousTime = 0u;
        for (uint32_t pointIndex = 0u; pointIndex < pointCount;
                ++pointIndex) {
            uint64_t timeNs = 0u;
            uint16_t value = 0u;
            uint16_t pointReserved = 0u;
            if (!readU64Le(stream, timeNs, &crc)
                || !readU16Le(stream, value, &crc)
                || !readU16Le(stream, pointReserved, &crc)
                || pointReserved != 0u || timeNs > lengthNs
                || value > 16383u
                || (pointIndex != 0u && timeNs < previousTime)) {
                return false;
            }
            previousTime = timeNs;
            loop.points.push_back({
                static_cast<double>(timeNs)
                    / static_cast<double>(kNanosecondsPerSecond),
                value,
            });
            payloadRead += kSessionPointBytes;
        }
        lastTouched = index;
    }
    crc ^= 0xffffffffu;
    uint8_t trailing = 0u;
    if (pointsRead != declaredPoints || payloadRead != declaredPayloadBytes
        || crc != declaredCrc || !stream || !stream->read
        || stream->read(stream, &trailing, 1u) != 0) {
        return false;
    }

    imported.loops = std::move(loaded);
    imported.takeoverMs = static_cast<double>(takeoverMicros) * 0.001;
    imported.lastTouchedIndex = lastTouched;
    return true;
}

bool sessionSave(const clap_plugin_t* plugin,
    const clap_ostream_t* stream)
{
    if (!plugin || !stream) return false;
    auto* p = self(plugin);
    try {
        GestureSnapshot snapshot;
        {
            const std::lock_guard<std::mutex> lock(p->sessionMutex);
            // A take is not committed until recording stops. Refuse an
            // ambiguous partial export instead of silently omitting it.
            if (p->recording || !std::isfinite(p->takeoverMs)) return false;
            // Audio passes input through while the snapshot lock is held. Do
            // not retain a partial NRPN decode across that interval.
            p->nrpn = {};
            snapshot.playing = p->playing;
            snapshot.takeoverMs = p->takeoverMs;
            snapshot.loops = p->loops;
        }
        // CRC calculation and stream I/O happen after releasing the realtime
        // state lock. Processing can resume while the immutable copy is saved.
        return sessionSaveSnapshot(snapshot, stream);
    } catch (...) {
        return false;
    }
}

bool sessionLoad(const clap_plugin_t* plugin,
    const clap_istream_t* stream)
{
    if (!plugin || !stream) return false;
    auto* p = self(plugin);
    try {
        ImportedGestureSession imported;
        // Parse and validate without holding the realtime state lock. Only the
        // final replacement is synchronized with processing.
        if (!parseGestureSession(stream, imported)) return false;
        for (auto& loop : imported.loops) {
            loop.points.reserve(std::max<size_t>(
                kRealtimePointsPerParameter, loop.points.size()));
        }
        const std::lock_guard<std::mutex> lock(p->sessionMutex);
        p->loops = std::move(imported.loops);
        p->playing = false;
        p->takeoverMs = imported.takeoverMs;
        p->lastTouchedIndex = imported.lastTouchedIndex;
        p->nrpn = {};
        p->guiCommands.store(0u, std::memory_order_release);
        p->uiSelectedIndex.store(imported.lastTouchedIndex,
            std::memory_order_relaxed);
        cancelRecording(*p);
        prepareAllLoops(*p, p->framePosition);
        rebuildPlaybackSchedule(*p);
        resetImportedUiValues(*p);
        syncAllUi(*p);
        return true;
    } catch (...) {
        return false;
    }
}

bool sessionClear(const clap_plugin_t* plugin)
{
    if (!plugin) return false;
    auto* p = self(plugin);
    const std::lock_guard<std::mutex> lock(p->sessionMutex);
    p->playing = false;
    p->guiCommands.store(0u, std::memory_order_release);
    p->nrpn = {};
    clearAllLoops(*p);
    rebuildPlaybackSchedule(*p);
    return true;
}

const s3g_nim_gesture_session_t sessionExt {
    sessionSave, sessionLoad, sessionClear,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!plugin || !stream) return false;
    auto* p = self(plugin);
    try {
        GestureSnapshot snapshot;
        {
            const std::lock_guard<std::mutex> lock(p->sessionMutex);
            // Host state serialization has the same nonblocking process
            // fallback as explicit export. Do not retain an NRPN fragment
            // across the snapshot interval.
            p->nrpn = {};
            snapshot.playing = p->playing;
            snapshot.takeoverMs = p->takeoverMs;
            snapshot.loops = p->loops;
        }
        const uint8_t playing = snapshot.playing ? 1u : 0u;
        uint32_t count = 0u;
        for (const auto& loop : snapshot.loops) {
            if (!loop.points.empty()) ++count;
        }
        if (!writeExact(stream, &kStateMagic, sizeof(kStateMagic))
            || !writeExact(stream, &kStateVersion, sizeof(kStateVersion))
            || !writeExact(stream, &playing, sizeof(playing))
            || !writeExact(stream, &snapshot.takeoverMs,
                sizeof(snapshot.takeoverMs))
            || !writeExact(stream, &count, sizeof(count))) {
            return false;
        }
        for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
            const auto& loop = snapshot.loops[index];
            if (loop.points.empty()) continue;
            const uint16_t id = parameterId(index);
            const uint32_t pointCount = static_cast<uint32_t>(
                loop.points.size());
            if (!writeExact(stream, &id, sizeof(id))
                || !writeExact(stream, &loop.lengthSeconds,
                    sizeof(loop.lengthSeconds))
                || !writeExact(stream, &pointCount, sizeof(pointCount))) {
                return false;
            }
            for (const auto& point : loop.points) {
                if (!writeExact(stream, &point.seconds,
                        sizeof(point.seconds))
                    || !writeExact(stream, &point.value,
                        sizeof(point.value))) {
                    return false;
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool stateLoadImpl(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!plugin || !stream) return false;
    auto* p = self(plugin);
    uint32_t magic = 0u;
    uint32_t version = 0u;
    uint8_t playing = 0u;
    double takeoverMs = 0.0;
    uint32_t count = 0u;
    if (!readExact(stream, &magic, sizeof(magic))
        || !readExact(stream, &version, sizeof(version))
        || !readExact(stream, &playing, sizeof(playing))
        || !readExact(stream, &takeoverMs, sizeof(takeoverMs))
        || !readExact(stream, &count, sizeof(count))
        || magic != kStateMagic || version != kStateVersion
        || !std::isfinite(takeoverMs)
        || count > kLegacyNimParameterCount) {
        return false;
    }

    std::array<Loop, kNimParameterCount> loaded {};
    uint64_t totalPoints = 0u;
    int32_t lastTouched = -1;
    for (uint32_t loopIndex = 0u; loopIndex < count; ++loopIndex) {
        uint16_t id = 0u;
        double lengthSeconds = 0.0;
        uint32_t pointCount = 0u;
        if (!readExact(stream, &id, sizeof(id))
            || !readExact(stream, &lengthSeconds, sizeof(lengthSeconds))
            || !readExact(stream, &pointCount, sizeof(pointCount))) {
            return false;
        }
        const int32_t index = parameterIndex(id);
        const bool retired = id == 55u || id == 56u;
        totalPoints += pointCount;
        if ((index < 0 && !retired) || pointCount == 0u
            || totalPoints > kMaximumStatePoints
            || !std::isfinite(lengthSeconds) || lengthSeconds <= 0.0
            || lengthSeconds > kMaximumLoopSeconds) {
            return false;
        }
        Loop retiredLoop;
        auto& loop = index >= 0
            ? loaded[static_cast<uint32_t>(index)] : retiredLoop;
        if (index >= 0 && !loop.points.empty()) return false;
        loop.lengthSeconds = lengthSeconds;
        loop.points.reserve(std::min<uint32_t>(pointCount, 4096u));
        for (uint32_t pointIndex = 0u; pointIndex < pointCount;
                ++pointIndex) {
            Point point;
            if (!readExact(stream, &point.seconds, sizeof(point.seconds))
                || !readExact(stream, &point.value, sizeof(point.value))
                || !std::isfinite(point.seconds) || point.seconds < 0.0
                || point.seconds > lengthSeconds || point.value > 16383u) {
                return false;
            }
            loop.points.push_back(point);
        }
        if (index >= 0) lastTouched = index;
    }

    for (auto& loop : loaded) {
        loop.points.reserve(std::max<size_t>(
            kRealtimePointsPerParameter, loop.points.size()));
    }
    const std::lock_guard<std::mutex> lock(p->sessionMutex);
    p->loops = std::move(loaded);
    p->playing = playing != 0u;
    p->takeoverMs = std::clamp(takeoverMs, 0.0, 5000.0);
    p->lastTouchedIndex = lastTouched;
    p->nrpn = {};
    cancelRecording(*p);
    prepareAllLoops(*p, p->framePosition);
    rebuildPlaybackSchedule(*p);
    for (uint32_t index = 0u; index < kNimParameterCount; ++index) {
        if (!p->loops[index].points.empty()) {
            showUiValue(*p, index, p->loops[index].points.front().value);
        }
    }
    syncAllUi(*p);
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    try {
        return stateLoadImpl(plugin, stream);
    } catch (...) {
        return false;
    }
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

#if defined(__APPLE__)

struct GuiControl {
    char name[20] {};
    char abbreviation[6] {};
    uint16_t parameter = 0u;
    uint32_t color = 0x777777u;
    bool action = false;
};

struct GuiPage {
    const char* title = "";
    std::array<GuiControl, 16u> controls {};
};

constexpr uint32_t kNetworkColor = 0x607b7d;
constexpr uint32_t kMovementColor = 0x9a6851;
constexpr uint32_t kSurfaceColor = 0x697d8f;
constexpr uint32_t kOutputColor = 0xa4a4a4;
constexpr uint32_t kToneColor = 0x71858b;
constexpr uint32_t kAuxAColor = 0x977642;
constexpr uint32_t kAuxBColor = 0x745f82;
constexpr uint32_t kLevelColor = 0xa4a4a4;
constexpr uint32_t kBodyColor = 0x66806e;
constexpr uint32_t kEqColor = 0x5e7889;
constexpr uint32_t kTuneColor = 0x776a8f;
constexpr uint32_t kMatrixSelfColor = 0x7d654e;
constexpr uint32_t kMatrixNextColor = 0x607b7d;
constexpr uint32_t kMatrixFarColor = 0x735f73;

const std::array<GuiPage, 12u>& guiPages()
{
    static const std::array<GuiPage, 12u> pages = [] {
        std::array<GuiPage, 12u> result {};
        const char* titles[] {
            "LIVE", "MOTION", "MIXER", "MATRIX1", "MATRIX2", "SENDS",
            "AUXTONE", "EQ LOHI", "EQ MID", "TUNING", "RETURNS", "ACTIONS",
        };
        for (uint32_t page = 0u; page < result.size(); ++page) {
            result[page].title = titles[page];
        }
        const auto set = [&](uint32_t page, uint32_t slot, const char* name,
                             const char* abbreviation, uint16_t parameter,
                             uint32_t color, bool action = false) {
            auto& control = result[page].controls[slot];
            std::snprintf(control.name, sizeof(control.name), "%s", name);
            std::snprintf(control.abbreviation,
                sizeof(control.abbreviation), "%s", abbreviation);
            control.parameter = parameter;
            control.color = color;
            control.action = action;
        };
        const auto laneParameter = [](uint32_t lane, uint32_t offset) {
            return static_cast<uint16_t>(1000u + lane * 100u + offset);
        };
        const auto matrixParameter = [](uint32_t destination,
                                        uint32_t source) {
            return static_cast<uint16_t>(100u + destination * 8u + source);
        };

        const char* liveNames[] { "Feedback", "Coupling", "Flow", "Phase",
            "Agency", "Motion", "Spread", "Vortex", "Formant", "Space",
            "Variance", "R Mode", "Aux A Ret", "Aux B Ret", "Drift",
            "Output" };
        const char* liveLabels[] { "FDBK", "COUP", "FLOW", "PHAS", "AGCY",
            "MOTN", "SPRD", "VRTX", "FORM", "SPAC", "VAR", "RMOD",
            "ARET", "BRET", "DRFT", "OUT!" };
        const uint16_t liveIds[] { 5u, 6u, 16u, 7u, 11u, 19u, 17u, 18u,
            9u, 12u, 13u, 44u, 26u, 31u, 8u, 1u };
        const uint32_t liveColors[] { kNetworkColor, kNetworkColor,
            kMovementColor, kNetworkColor, kNetworkColor, kMovementColor,
            kMovementColor, kMovementColor, kToneColor, kNetworkColor,
            kSurfaceColor, kSurfaceColor, kAuxAColor, kAuxBColor,
            kNetworkColor, kOutputColor };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            set(0u, slot, liveNames[slot], liveLabels[slot], liveIds[slot],
                liveColors[slot]);
        }

        const char* motionNames[] { "Event Rate", "Event Length", "Density",
            "Chaos", "Slew", "Choke", "Move Rate", "Move Phase",
            "Random Depth", "Random Thresh", "Random Attack", "Random Release",
            "Random Polar", "Shape", "Behavior", "Output" };
        const char* motionLabels[] { "EVRT", "EVLN", "DENS", "CHAO", "SLEW",
            "CHOK", "MVRT", "MVPH", "RDEP", "RTHR", "RATK", "RREL",
            "RPOL", "SHAP", "BEHV", "OUT!" };
        const uint16_t motionIds[] { 36u, 37u, 38u, 39u, 40u, 41u, 21u, 22u,
            45u, 46u, 47u, 48u, 49u, 20u, 35u, 1u };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            set(1u, slot, motionNames[slot], motionLabels[slot],
                motionIds[slot], slot >= 8u && slot <= 12u
                    ? kSurfaceColor
                    : (slot == 15u ? kOutputColor : kMovementColor));
        }

        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "Lane %u Level", lane + 1u);
            std::snprintf(label, sizeof(label), "%uLVL", lane + 1u);
            set(2u, lane, name, label, laneParameter(lane, 2u), kLevelColor);
            std::snprintf(name, sizeof(name), "Lane %u Body", lane + 1u);
            std::snprintf(label, sizeof(label), "%uBDY", lane + 1u);
            set(2u, lane + 8u, name, label, laneParameter(lane, 0u),
                kBodyColor);
        }

        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "M%u Self", lane + 1u);
            std::snprintf(label, sizeof(label), "%uSLF", lane + 1u);
            set(3u, lane, name, label, matrixParameter(lane, lane),
                kMatrixSelfColor);
            const uint32_t destination = (lane + 1u) % 8u;
            std::snprintf(name, sizeof(name), "M%u to %u", lane + 1u,
                destination + 1u);
            std::snprintf(label, sizeof(label), "%u>%u", lane + 1u,
                destination + 1u);
            set(3u, lane + 8u, name, label,
                matrixParameter(destination, lane), kMatrixNextColor);
        }

        for (uint32_t slot = 0u; slot < 8u; ++slot) {
            const uint32_t source = (slot + 1u) % 8u;
            const uint32_t destination = slot;
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "M%u to %u", source + 1u,
                destination + 1u);
            std::snprintf(label, sizeof(label), "%u>%u", source + 1u,
                destination + 1u);
            set(4u, slot, name, label, matrixParameter(destination, source),
                kMatrixNextColor);
        }
        const uint8_t farPairs[8u][2u] {
            { 0u, 4u }, { 4u, 0u }, { 1u, 5u }, { 5u, 1u },
            { 2u, 6u }, { 6u, 2u }, { 3u, 7u }, { 7u, 3u },
        };
        for (uint32_t slot = 0u; slot < 8u; ++slot) {
            const uint32_t source = farPairs[slot][0u];
            const uint32_t destination = farPairs[slot][1u];
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "M%u to %u", source + 1u,
                destination + 1u);
            std::snprintf(label, sizeof(label), "%u>%u", source + 1u,
                destination + 1u);
            set(4u, slot + 8u, name, label,
                matrixParameter(destination, source), kMatrixFarColor);
        }

        const auto laneRows = [&](uint32_t page, uint32_t firstOffset,
                                  const char* firstName, const char* firstCode,
                                  uint32_t firstColor, uint32_t secondOffset,
                                  const char* secondName, const char* secondCode,
                                  uint32_t secondColor) {
            for (uint32_t lane = 0u; lane < 8u; ++lane) {
                char name[20] {};
                char label[6] {};
                std::snprintf(name, sizeof(name), "L%u %s", lane + 1u,
                    firstName);
                std::snprintf(label, sizeof(label), "%u%s", lane + 1u,
                    firstCode);
                set(page, lane, name, label, laneParameter(lane, firstOffset),
                    firstColor);
                std::snprintf(name, sizeof(name), "L%u %s", lane + 1u,
                    secondName);
                std::snprintf(label, sizeof(label), "%u%s", lane + 1u,
                    secondCode);
                set(page, lane + 8u, name, label,
                    laneParameter(lane, secondOffset), secondColor);
            }
        };
        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "Aux A Lane %u Send",
                lane + 1u);
            std::snprintf(label, sizeof(label), "A%uSD", lane + 1u);
            set(5u, lane, name, label, laneParameter(lane, 8u),
                kAuxAColor);
            std::snprintf(name, sizeof(name), "Aux B Lane %u Send",
                lane + 1u);
            std::snprintf(label, sizeof(label), "B%uSD", lane + 1u);
            set(5u, lane + 8u, name, label, laneParameter(lane, 9u),
                kAuxBColor);
        }

        const char* auxNames[] { "Interaction", "House Tone", "A Type",
            "B Type", "A Gain", "A Tone", "A Bias", "A Return", "B Gain",
            "B Tone", "B Bias", "B Return", "A Feedback", "B Feedback",
            "Ceiling", "Output" };
        const char* auxLabels[] { "INTR", "HOUS", "ATYP", "BTYP", "AGAN",
            "ATON", "ABIA", "AR/M", "BGAN", "BTON", "BBIA", "BR/M",
            "AFBK", "BFBK", "CEIL", "OUT!" };
        const uint16_t auxIds[] { 14u, 15u, 23u, 28u, 24u, 25u, 42u, 26u,
            29u, 30u, 43u, 31u, 27u, 32u, 2u, 1u };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            const uint32_t color = slot < 2u ? kToneColor
                : ((slot == 2u) || (slot >= 4u && slot <= 7u)
                        || slot == 12u)
                    ? kAuxAColor
                    : ((slot == 3u) || (slot >= 8u && slot <= 11u)
                            || slot == 13u)
                        ? kAuxBColor : kOutputColor;
            set(6u, slot, auxNames[slot], auxLabels[slot], auxIds[slot], color);
        }

        laneRows(7u, 4u, "Low", "LOW", kEqColor,
            7u, "High", "HIG", kEqColor);
        laneRows(8u, 5u, "Mid Hz", "MHZ", kEqColor,
            6u, "Mid Gain", "MGN", kEqColor);
        laneRows(9u, 10u, "Note", "NOT", kTuneColor,
            11u, "Cents", "CTS", kTuneColor);
        for (uint32_t lane = 0u; lane < 8u; ++lane) {
            char name[20] {};
            char label[6] {};
            std::snprintf(name, sizeof(name), "Aux A Lane %u Return",
                lane + 1u);
            std::snprintf(label, sizeof(label), "A%uRN", lane + 1u);
            set(10u, lane, name, label, laneParameter(lane, 15u),
                kAuxAColor);
            std::snprintf(name, sizeof(name), "Aux B Lane %u Return",
                lane + 1u);
            std::snprintf(label, sizeof(label), "B%uRN", lane + 1u);
            set(10u, lane + 8u, name, label, laneParameter(lane, 16u),
                kAuxBColor);
        }

        const char* actionNames[] { "Record", "Playback", "Clear Last",
            "Clear All", "Cancel", "Matrix Flip", "Matrix Latch",
            "New Sign", "Seed", "Random Low", "Random Mid", "Random High",
            "Forget", "Clear Matrix", "BU16 Ramp", "Output / Panic" };
        const char* actionLabels[] { "REC", "PLAY", "CLRL", "CLRA", "CNCL",
            "FLIP", "LTCH", "SIGN", "SEED", "RLO", "RMD", "RHI", "FORG",
            "MX0", "RAMP", "OUT!" };
        const uint16_t actionIds[] { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
            0u, 0u, 0u, 0u, 0u, 0u, 59u, 1u };
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            set(11u, slot, actionNames[slot], actionLabels[slot],
                actionIds[slot], slot == 5u ? kMatrixNextColor
                    : (slot == 6u || slot == 7u) ? kMatrixFarColor
                    : slot == 14u ? kMovementColor : kOutputColor,
                slot < 14u);
        }
        return result;
    }();
    return pages;
}

constexpr std::array<uint8_t, 12u> kPageLayoutOrder {
    0u, 1u, 6u, 11u,
    3u, 4u, 5u, 10u,
    2u, 7u, 8u, 9u,
};

NSRect guiCardRect(uint32_t layoutSlot)
{
    constexpr CGFloat margin = 18.0;
    constexpr CGFloat gap = 12.0;
    constexpr CGFloat width = 277.5;
    constexpr CGFloat height = 218.0;
    const uint32_t column = layoutSlot % 4u;
    const uint32_t row = layoutSlot / 4u;
    return NSMakeRect(margin + column * (width + gap),
        124.0 + row * (height + gap), width, height);
}

uint32_t guiLayoutSlotForPage(uint32_t page)
{
    for (uint32_t slot = 0u; slot < kPageLayoutOrder.size(); ++slot) {
        if (kPageLayoutOrder[slot] == page) return slot;
    }
    return 0u;
}

NSRect guiControlCellRect(uint32_t page, uint32_t slot)
{
    const NSRect card = guiCardRect(guiLayoutSlotForPage(page));
    constexpr CGFloat inset = 8.0;
    constexpr CGFloat headerHeight = 25.0;
    const CGFloat cellWidth = (card.size.width - inset * 2.0) / 4.0;
    const CGFloat cellHeight = (card.size.height - headerHeight - 5.0) / 4.0;
    return NSMakeRect(card.origin.x + inset + (slot % 4u) * cellWidth,
        card.origin.y + headerHeight + (slot / 4u) * cellHeight,
        cellWidth, cellHeight);
}

void strokeGuiArc(NSPoint center, CGFloat radius, CGFloat startDegrees,
    CGFloat spanDegrees, NSColor* color, CGFloat width)
{
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const uint32_t steps = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::ceil(std::fabs(spanDegrees) / 6.0)));
    NSBezierPath* path = [NSBezierPath bezierPath];
    for (uint32_t step = 0u; step <= steps; ++step) {
        const CGFloat angle = (startDegrees + spanDegrees
            * static_cast<CGFloat>(step) / static_cast<CGFloat>(steps))
            * radians;
        const NSPoint point = NSMakePoint(center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle));
        if (step == 0u) [path moveToPoint:point];
        else [path lineToPoint:point];
    }
    [color setStroke];
    [path setLineWidth:width];
    [path setLineCapStyle:NSLineCapStyleRound];
    [path stroke];
}

} // namespace

@interface S3GNimGestureView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    BOOL _draggingTakeover;
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GNimGestureView

- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _draggingTakeover = NO;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (NSRect)buttonRect:(uint32_t)index
{
    const CGFloat widths[] { 68.0, 68.0, 88.0, 82.0, 68.0 };
    CGFloat x = 30.0;
    for (uint32_t button = 0u; button < index; ++button) {
        x += widths[button] + 7.0;
    }
    return NSMakeRect(x, 76.0, widths[index], 25.0);
}

- (NSRect)takeoverTrackRect
{
    return NSMakeRect(510.0, 84.0, 204.0, 8.0);
}

- (NSRect)takeoverHitRect
{
    return NSMakeRect(440.0, 67.0, 350.0, 40.0);
}

- (void)drawButton:(uint32_t)index label:(NSString*)label active:(bool)active
    semanticColor:(NSColor*)semanticColor
{
    s3g::clap_gui::Style style;
    const NSRect button = [self buttonRect:index];
    s3g::clap_gui::drawHeaderButton(button, NSMakeRect(18.0, 42.0,
        kGuiWidth - 36.0, 72.0), label, active,
        s3g::clap_gui::textAttrs(s3g::clap_gui::color(0xb8b8b8), 9.0), style);
    if (active && semanticColor) {
        [semanticColor setFill];
        NSRectFill(NSMakeRect(button.origin.x, button.origin.y,
            button.size.width, 2.0));
    }
}

- (void)drawControl:(const GuiControl&)control page:(uint32_t)page
    slot:(uint32_t)slot
{
    const NSRect cell = guiControlCellRect(page, slot);
    const int32_t index = control.parameter != 0u
        ? parameterIndex(control.parameter) : -1;
    const bool selected = index >= 0 && _plugin->uiSelectedIndex.load(
        std::memory_order_relaxed) == index;
    if (selected) {
        [s3g::clap_gui::color(0x333333) setFill];
        NSRectFill(NSInsetRect(cell, 2.0, 1.0));
        [s3g::clap_gui::color(0x777777) setStroke];
        NSFrameRect(NSInsetRect(cell, 2.5, 1.5));
    }

    const NSPoint center = NSMakePoint(NSMidX(cell), cell.origin.y + 17.0);
    NSColor* signal = s3g::clap_gui::color(static_cast<int>(control.color));
    if (control.action) {
        strokeGuiArc(center, 12.0, 135.0, 270.0,
            s3g::clap_gui::color(0x464646), 2.0);
        [s3g::clap_gui::color(0x202020) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            center.x - 8.0, center.y - 8.0, 16.0, 16.0)] fill];
        [s3g::clap_gui::color(0x888888) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            center.x - 2.0, center.y - 2.0, 4.0, 4.0)] fill];
    } else {
        uint8_t flags = 0u;
        uint16_t raw = 0u;
        if (index >= 0) {
            flags = _plugin->uiFlags[static_cast<uint32_t>(index)].load(
                std::memory_order_relaxed);
            raw = _plugin->uiValues[static_cast<uint32_t>(index)].load(
                std::memory_order_relaxed);
        }
        const bool seen = (flags & kUiValueSeen) != 0u;
        const bool loopActive = (flags & kUiLoopActive) != 0u;
        const bool takeTouched = (flags & kUiTakeTouched) != 0u;
        const CGFloat normalized = static_cast<CGFloat>(raw) / 16383.0;
        if (loopActive || takeTouched) {
            strokeGuiArc(center, 15.0, 135.0, 270.0,
                takeTouched ? s3g::clap_gui::color(0xb56a5e) : signal,
                takeTouched ? 2.5 : 1.5);
        }
        strokeGuiArc(center, 12.0, 135.0, 270.0,
            s3g::clap_gui::color(0x3b3b3b), 2.5);
        if (seen) {
            strokeGuiArc(center, 12.0, 135.0, normalized * 270.0,
                signal, 2.5);
        }
        [s3g::clap_gui::color(0x181818) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            center.x - 8.5, center.y - 8.5, 17.0, 17.0)] fill];
        const CGFloat angle = (135.0 + normalized * 270.0)
            * 3.14159265358979323846 / 180.0;
        NSBezierPath* indicator = [NSBezierPath bezierPath];
        [indicator moveToPoint:center];
        [indicator lineToPoint:NSMakePoint(center.x + std::cos(angle) * 6.0,
            center.y + std::sin(angle) * 6.0)];
        [(seen ? signal : s3g::clap_gui::color(0x4c4c4c)) setStroke];
        [indicator setLineWidth:1.5];
        [indicator stroke];
        if (loopActive) {
            [signal setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                center.x + 10.0, center.y - 13.0, 4.0, 4.0)] fill];
        }
    }

    NSString* abbreviation = [NSString stringWithUTF8String:
        control.abbreviation];
    NSDictionary* labelAttrs = s3g::clap_gui::textAttrs(
        control.action ? s3g::clap_gui::color(0x888888)
            : s3g::clap_gui::color(0xaaaaaa), 7.5);
    const NSSize labelSize = [abbreviation sizeWithAttributes:labelAttrs];
    [abbreviation drawAtPoint:NSMakePoint(NSMidX(cell) - labelSize.width * 0.5,
        cell.origin.y + 33.0) withAttributes:labelAttrs];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);

    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [@"s3g UTILITY NIM GESTURE" drawAtPoint:NSMakePoint(18.0, 14.0)
        withAttributes:titleAttrs];
    NSString* titleStatus = [NSString stringWithFormat:
        @"%u LOOPS  ·  FREE  ·  NRPN CH 16",
        _plugin->uiLoopCount.load(std::memory_order_relaxed)];
    s3g::clap_gui::drawRightStatus(titleStatus, kGuiWidth, 14.0, valueAttrs);

    const NSRect transport = NSMakeRect(18.0, 42.0, kGuiWidth - 36.0, 72.0);
    s3g::clap_gui::drawPanelFrame(transport.origin.x, transport.origin.y,
        transport.size.width, transport.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"FREE MOTION / E16 CONTROL", true,
        transport.origin.x, transport.origin.y, transport.size.width, 22.0,
        labelAttrs, style);
    NSString* legend = @"INNER RING  VALUE    OUTER RING  LOOP";
    [legend drawAtPoint:NSMakePoint(NSMaxX(transport)
        - [legend sizeWithAttributes:valueAttrs].width - 10.0, 47.0)
        withAttributes:valueAttrs];

    const bool recording = _plugin->uiRecording.load(std::memory_order_relaxed);
    const bool playing = _plugin->uiPlaying.load(std::memory_order_relaxed);
    [self drawButton:0u label:@"RECORD" active:recording
        semanticColor:s3g::clap_gui::color(0xb56a5e)];
    [self drawButton:1u label:@"PLAY" active:playing
        semanticColor:s3g::clap_gui::color(0x70866f)];
    [self drawButton:2u label:@"CLEAR SEL" active:false semanticColor:nil];
    [self drawButton:3u label:@"CLEAR ALL" active:false semanticColor:nil];
    [self drawButton:4u label:@"CANCEL" active:false semanticColor:nil];

    const double takeover = _plugin->uiTakeoverMs.load(
        std::memory_order_relaxed);
    [@"TAKEOVER" drawAtPoint:NSMakePoint(445.0, 78.0)
        withAttributes:labelAttrs];
    const NSRect takeoverTrack = [self takeoverTrackRect];
    [s3g::clap_gui::color(0x101010) setFill];
    NSRectFill(takeoverTrack);
    [s3g::clap_gui::color(0x4f4f4f) setFill];
    NSRectFill(NSMakeRect(takeoverTrack.origin.x, takeoverTrack.origin.y,
        takeoverTrack.size.width * std::clamp(takeover / 5000.0, 0.0, 1.0),
        takeoverTrack.size.height));
    [s3g::clap_gui::color(0x666666) setStroke];
    NSFrameRect(takeoverTrack);
    NSString* takeoverText = [NSString stringWithFormat:@"%.0f ms", takeover];
    [takeoverText drawAtPoint:NSMakePoint(725.0, 78.0)
        withAttributes:valueAttrs];

    const int32_t selected = _plugin->uiSelectedIndex.load(
        std::memory_order_relaxed);
    NSString* selectedText = @"SELECT AN ENCODER";
    NSString* selectedValue = @"CLICK TO INSPECT · DOUBLE-CLICK TO CLEAR LOOP";
    if (selected >= 0
        && selected < static_cast<int32_t>(kNimParameterCount)) {
        const uint16_t id = parameterId(static_cast<uint32_t>(selected));
        const uint16_t raw = _plugin->uiValues[
            static_cast<uint32_t>(selected)].load(std::memory_order_relaxed);
        const uint8_t flags = _plugin->uiFlags[
            static_cast<uint32_t>(selected)].load(std::memory_order_relaxed);
        const float length = _plugin->uiLoopLengths[
            static_cast<uint32_t>(selected)].load(std::memory_order_relaxed);
        const char* name = "NIM PARAMETER";
        for (const auto& page : guiPages()) {
            for (const auto& control : page.controls) {
                if (control.parameter == id) {
                    name = control.name;
                    break;
                }
            }
        }
        selectedText = [NSString stringWithFormat:@"%s  ·  NRPN %u", name, id];
        selectedValue = [NSString stringWithFormat:@"RAW %u  ·  %s%.3f s",
            raw, (flags & kUiLoopActive) != 0u ? "LOOP " : "NO LOOP  ",
            length];
    }
    [selectedText drawAtPoint:NSMakePoint(812.0, 74.0)
        withAttributes:labelAttrs];
    [selectedValue drawAtPoint:NSMakePoint(812.0, 91.0)
        withAttributes:valueAttrs];

    const auto& pages = guiPages();
    for (uint32_t layoutSlot = 0u; layoutSlot < kPageLayoutOrder.size();
         ++layoutSlot) {
        const uint32_t pageIndex = kPageLayoutOrder[layoutSlot];
        const GuiPage& page = pages[pageIndex];
        const NSRect card = guiCardRect(layoutSlot);
        s3g::clap_gui::drawPanelFrame(card.origin.x, card.origin.y,
            card.size.width, card.size.height, style);
        NSString* header = [NSString stringWithFormat:@"P%02u  %s",
            layoutSlot + 1u, page.title];
        s3g::clap_gui::drawPanelHeader(header, true, card.origin.x,
            card.origin.y, card.size.width, 23.0, labelAttrs, style);
        uint32_t pageLoops = 0u;
        for (const auto& control : page.controls) {
            const int32_t controlIndex = parameterIndex(control.parameter);
            if (controlIndex >= 0
                && (_plugin->uiFlags[static_cast<uint32_t>(controlIndex)].load(
                    std::memory_order_relaxed) & kUiLoopActive) != 0u) {
                ++pageLoops;
            }
        }
        NSString* countText = pageLoops == 0u ? @"—"
            : [NSString stringWithFormat:@"%u", pageLoops];
        [countText drawAtPoint:NSMakePoint(NSMaxX(card)
            - [countText sizeWithAttributes:valueAttrs].width - 8.0,
            card.origin.y + 5.0) withAttributes:valueAttrs];
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            [self drawControl:page.controls[slot] page:pageIndex slot:slot];
        }
    }
}

- (void)updateTakeover:(NSPoint)point
{
    if (!_plugin) return;
    const NSRect track = [self takeoverTrackRect];
    const double normalized = std::clamp(
        (point.x - track.origin.x) / track.size.width, 0.0, 1.0);
    const double milliseconds = normalized * 5000.0;
    _plugin->guiTakeoverMs.store(milliseconds, std::memory_order_relaxed);
    _plugin->uiTakeoverMs.store(milliseconds, std::memory_order_relaxed);
    requestGuiCommand(*_plugin, kGuiSetTakeover);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!_plugin) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const uint32_t commands[] { kGuiToggleRecord, kGuiTogglePlay,
        kGuiClearSelected, kGuiClearAll, kGuiCancelRecord };
    for (uint32_t index = 0u; index < 5u; ++index) {
        if (NSPointInRect(point, [self buttonRect:index])) {
            requestGuiCommand(*_plugin, commands[index]);
            return;
        }
    }
    if (NSPointInRect(point, [self takeoverHitRect])) {
        _draggingTakeover = YES;
        if ([event clickCount] >= 2) {
            _plugin->guiTakeoverMs.store(650.0, std::memory_order_relaxed);
            _plugin->uiTakeoverMs.store(650.0, std::memory_order_relaxed);
            requestGuiCommand(*_plugin, kGuiSetTakeover);
        } else {
            [self updateTakeover:point];
        }
        return;
    }
    const auto& pages = guiPages();
    for (uint32_t page = 0u; page < pages.size(); ++page) {
        for (uint32_t slot = 0u; slot < 16u; ++slot) {
            if (!NSPointInRect(point, guiControlCellRect(page, slot))) continue;
            const GuiControl& control = pages[page].controls[slot];
            const int32_t selected = parameterIndex(control.parameter);
            if (selected < 0 || control.action) return;
            _plugin->uiSelectedIndex.store(selected, std::memory_order_relaxed);
            if ([event clickCount] >= 2) {
                requestGuiCommand(*_plugin, kGuiClearSelected);
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (!_draggingTakeover) return;
    [self updateTakeover:[self convertPoint:[event locationInWindow]
        fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _draggingTakeover = NO;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api,
    bool isFloating)
{
    return !isFloating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* isFloating)
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
    p->guiView = [[S3GNimGestureView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
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
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GNimGestureView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
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
        || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) {
        return false;
    }
    p->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3GNimGestureView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GNimGestureView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide,
};

#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, S3G_NIM_GESTURE_SESSION_EXTENSION) == 0)
        return &sessionExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.nim-gesture",
    "s3g Utility NIM Gesture",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Free-running NRPN gesture loops and E16 feedback for No Input Mixer.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
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

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

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

#ifndef S3G_CLAP_ENTRY_SYMBOL
#define S3G_CLAP_ENTRY_SYMBOL clap_entry
#define S3G_CLAP_ENTRY_EXPORT CLAP_EXPORT
#else
#define S3G_CLAP_ENTRY_EXPORT
#endif

extern "C" const S3G_CLAP_ENTRY_EXPORT clap_plugin_entry_t
    S3G_CLAP_ENTRY_SYMBOL {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
