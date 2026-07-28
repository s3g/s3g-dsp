#include "s3g_no_input_mixer.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kChannelCount = s3g::kNoInputMixerChannels;
constexpr uint32_t kGuiWidth = 1356u;
constexpr uint32_t kGuiHeight = 820u;

constexpr clap_id kOutputParamId = 1u;
constexpr clap_id kCeilingParamId = 2u;
constexpr clap_id kLimiterParamId = 3u;
constexpr clap_id kDcBlockParamId = 4u;
constexpr clap_id kFeedbackParamId = 5u;
constexpr clap_id kCouplingParamId = 6u;
constexpr clap_id kPhaseParamId = 7u;
constexpr clap_id kDriftParamId = 8u;
constexpr clap_id kFormantParamId = 9u;
constexpr clap_id kQualityParamId = 10u;
constexpr clap_id kMatrixParamBase = 100u;
constexpr clap_id kLaneParamBase = 1000u;
constexpr clap_id kLaneParamStride = 100u;
constexpr clap_id kLaneBodyOffset = 0u;
constexpr clap_id kLaneLossOffset = 1u;
constexpr clap_id kLaneLevelOffset = 2u;
constexpr clap_id kLaneMuteOffset = 3u;
constexpr clap_id kLaneLowOffset = 4u;
constexpr clap_id kLaneMidFrequencyOffset = 5u;
constexpr clap_id kLaneMidGainOffset = 6u;
constexpr clap_id kLaneHighOffset = 7u;
constexpr clap_id kLaneInsertBaseOffset = 20u;
constexpr clap_id kLaneInsertStride = 10u;
constexpr clap_id kInsertTypeOffset = 0u;
constexpr clap_id kInsertGainOffset = 1u;
constexpr clap_id kInsertToneOffset = 2u;
constexpr clap_id kInsertBiasOffset = 3u;
constexpr clap_id kInsertLevelOffset = 4u;
constexpr clap_id kInsertBypassOffset = 5u;
constexpr uint32_t kGlobalParamCount = 10u;
constexpr uint32_t kLaneDirectParamCount = 8u;
constexpr uint32_t kInsertParamCount = 6u;
constexpr uint32_t kLaneParamCount = kLaneDirectParamCount
    + s3g::kNoInputMixerInsertSlots * kInsertParamCount;
constexpr uint32_t kTotalParamCount = kGlobalParamCount
    + s3g::kNoInputMixerMatrixCells
    + kChannelCount * kLaneParamCount;

const s3g::NoInputMixerParams kDefaultParams =
    s3g::defaultNoInputMixerParams();

constexpr clap_id matrixParamId(uint32_t destination, uint32_t source)
{
    return kMatrixParamBase + destination * kChannelCount + source;
}

constexpr clap_id laneParamId(uint32_t lane, clap_id offset)
{
    return kLaneParamBase + lane * kLaneParamStride + offset;
}

constexpr clap_id insertParamId(uint32_t lane, uint32_t slot,
    clap_id offset)
{
    return laneParamId(lane, kLaneInsertBaseOffset
        + slot * kLaneInsertStride + offset);
}

bool decodeMatrixParam(clap_id id, uint32_t& destination, uint32_t& source)
{
    if (id < kMatrixParamBase
        || id >= kMatrixParamBase + s3g::kNoInputMixerMatrixCells) {
        return false;
    }
    const uint32_t index = id - kMatrixParamBase;
    destination = index / kChannelCount;
    source = index % kChannelCount;
    return true;
}

bool decodeLaneParam(clap_id id, uint32_t& lane, clap_id& offset)
{
    if (id < kLaneParamBase) return false;
    lane = (id - kLaneParamBase) / kLaneParamStride;
    offset = (id - kLaneParamBase) % kLaneParamStride;
    return lane < kChannelCount;
}

bool decodeInsertOffset(clap_id laneOffset, uint32_t& slot,
    clap_id& insertOffset)
{
    if (laneOffset < kLaneInsertBaseOffset) return false;
    const clap_id relative = laneOffset - kLaneInsertBaseOffset;
    slot = relative / kLaneInsertStride;
    insertOffset = relative % kLaneInsertStride;
    return slot < s3g::kNoInputMixerInsertSlots
        && insertOffset < kInsertParamCount;
}

struct GlobalParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

const std::array<GlobalParamDef, kGlobalParamCount> kGlobalParamDefs {{
    { kOutputParamId, "Out", "Output", -60.0, 6.0,
        kDefaultParams.outputGainDb, false },
    { kCeilingParamId, "Ceiling", "Output", -18.0, 0.0,
        kDefaultParams.ceilingDb, false },
    { kLimiterParamId, "Limiter", "Output", 0.0, 1.0,
        static_cast<double>(kDefaultParams.limiterEnabled), true },
    { kDcBlockParamId, "DC Block", "Output", 0.0, 1.0,
        static_cast<double>(kDefaultParams.dcBlockEnabled), true },
    { kFeedbackParamId, "Feedback", "Network", 0.0, 1.25,
        kDefaultParams.feedback, false },
    { kCouplingParamId, "Coupling", "Network", 0.0, 1.25,
        kDefaultParams.coupling, false },
    { kPhaseParamId, "Phase", "Network", 0.0, 1.0,
        kDefaultParams.phase, false },
    { kDriftParamId, "Drift", "Network", 0.0, 1.0,
        kDefaultParams.drift, false },
    { kFormantParamId, "Formant", "Network", 0.0, 1.0,
        kDefaultParams.formant, false },
    { kQualityParamId, "Quality", "Containment", 0.0, 2.0,
        static_cast<double>(kDefaultParams.quality), true },
}};

struct ParamRange {
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    bool stepped = false;
};

bool paramRange(clap_id id, ParamRange& range)
{
    for (const auto& def : kGlobalParamDefs) {
        if (def.id == id) {
            range = { def.minimum, def.maximum, def.defaultValue,
                def.stepped };
            return true;
        }
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        range = { -1.0, 1.0,
            kDefaultParams.matrix[destination * kChannelCount + source],
            false };
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    const auto& defaults = kDefaultParams.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset:
        range = { 0.0, 1.0, defaults.body, false }; return true;
    case kLaneLossOffset:
        range = { 0.0, 1.0, defaults.loss, false }; return true;
    case kLaneLevelOffset:
        range = { -60.0, 12.0, defaults.levelDb, false }; return true;
    case kLaneMuteOffset:
        range = { 0.0, 1.0, static_cast<double>(defaults.mute), true };
        return true;
    case kLaneLowOffset:
        range = { -18.0, 18.0, defaults.lowDb, false }; return true;
    case kLaneMidFrequencyOffset:
        range = { 80.0, 8000.0, defaults.midFrequencyHz, false };
        return true;
    case kLaneMidGainOffset:
        range = { -18.0, 18.0, defaults.midGainDb, false }; return true;
    case kLaneHighOffset:
        range = { -18.0, 18.0, defaults.highDb, false }; return true;
    default: break;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    const auto& insert = defaults.inserts[slot];
    switch (insertOffset) {
    case kInsertTypeOffset:
        range = { 0.0,
            static_cast<double>(s3g::kNoInputDistortionTypeCount - 1u),
            static_cast<double>(insert.type), true }; return true;
    case kInsertGainOffset:
        range = { 0.0, 1.0, insert.gain, false }; return true;
    case kInsertToneOffset:
        range = { 0.0, 1.0, insert.tone, false }; return true;
    case kInsertBiasOffset:
        range = { -1.0, 1.0, insert.bias, false }; return true;
    case kInsertLevelOffset:
        range = { -24.0, 12.0, insert.levelDb, false }; return true;
    case kInsertBypassOffset:
        range = { 0.0, 1.0, static_cast<double>(insert.bypass), true };
        return true;
    default: return false;
    }
}

clap_id paramIdAtIndex(uint32_t index)
{
    if (index < kGlobalParamCount) return kGlobalParamDefs[index].id;
    index -= kGlobalParamCount;
    if (index < s3g::kNoInputMixerMatrixCells) {
        return kMatrixParamBase + index;
    }
    index -= s3g::kNoInputMixerMatrixCells;
    const uint32_t lane = index / kLaneParamCount;
    const uint32_t local = index % kLaneParamCount;
    if (lane >= kChannelCount) return CLAP_INVALID_ID;
    if (local < kLaneDirectParamCount) {
        return laneParamId(lane, local);
    }
    const uint32_t insertLocal = local - kLaneDirectParamCount;
    const uint32_t slot = insertLocal / kInsertParamCount;
    const uint32_t field = insertLocal % kInsertParamCount;
    return insertParamId(lane, slot, field);
}

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    uint32_t selectedLane = 2u;
    uint32_t selectedSlot = 0u;
    uint32_t selectedSource = 2u;
    uint32_t selectedDestination = 2u;
    uint32_t guiPage = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::NoInputMixerParams params = s3g::defaultNoInputMixerParams();
    s3g::NoInputMixer mixer;
    std::array<float, kChannelCount> frame {};
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kChannelCount> lanePeaks {};
    std::array<std::atomic<float>, kChannelCount> laneActivity {};
    std::atomic<float> networkActivity { 0.0f };
    std::atomic<float> minimumGovernor { 1.0f };
    std::atomic<uint32_t> containmentState {
        static_cast<uint32_t>(s3g::NoInputContainmentState::Quiet) };
    std::atomic<bool> seedRequested { false };
    std::atomic<bool> panicRequested { false };
    std::atomic<uint32_t> killMask { 0u };
    std::atomic<uint32_t> selectedLane { 2u };
    std::atomic<uint32_t> selectedSlot { 0u };
    std::atomic<uint32_t> selectedSource { 2u };
    std::atomic<uint32_t> selectedDestination { 2u };
    std::atomic<uint32_t> guiPage { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    for (const auto& def : kGlobalParamDefs) {
        if (def.id != id) continue;
        value = std::clamp(value, def.minimum, def.maximum);
        switch (id) {
        case kOutputParamId:
            plugin.params.outputGainDb = static_cast<float>(value); break;
        case kCeilingParamId:
            plugin.params.ceilingDb = static_cast<float>(value); break;
        case kLimiterParamId:
            plugin.params.limiterEnabled = value >= 0.5 ? 1u : 0u; break;
        case kDcBlockParamId:
            plugin.params.dcBlockEnabled = value >= 0.5 ? 1u : 0u; break;
        case kFeedbackParamId:
            plugin.params.feedback = static_cast<float>(value); break;
        case kCouplingParamId:
            plugin.params.coupling = static_cast<float>(value); break;
        case kPhaseParamId:
            plugin.params.phase = static_cast<float>(value); break;
        case kDriftParamId:
            plugin.params.drift = static_cast<float>(value); break;
        case kFormantParamId:
            plugin.params.formant = static_cast<float>(value); break;
        case kQualityParamId:
            plugin.params.quality = static_cast<uint32_t>(std::lround(value));
            break;
        default: break;
        }
        plugin.mixer.setParams(plugin.params);
        return;
    }

    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        plugin.params.matrix[destination * kChannelCount + source] =
            static_cast<float>(std::clamp(value, -1.0, 1.0));
        plugin.mixer.setParams(plugin.params);
        return;
    }

    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return;
    auto& laneParams = plugin.params.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset:
        laneParams.body = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kLaneLossOffset:
        laneParams.loss = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kLaneLevelOffset:
        laneParams.levelDb = static_cast<float>(
            std::clamp(value, -60.0, 12.0));
        break;
    case kLaneMuteOffset:
        laneParams.mute = value >= 0.5 ? 1u : 0u;
        break;
    case kLaneLowOffset:
        laneParams.lowDb = static_cast<float>(
            std::clamp(value, -18.0, 18.0));
        break;
    case kLaneMidFrequencyOffset:
        laneParams.midFrequencyHz = static_cast<float>(
            std::clamp(value, 80.0, 8000.0));
        break;
    case kLaneMidGainOffset:
        laneParams.midGainDb = static_cast<float>(
            std::clamp(value, -18.0, 18.0));
        break;
    case kLaneHighOffset:
        laneParams.highDb = static_cast<float>(
            std::clamp(value, -18.0, 18.0));
        break;
    default: {
        uint32_t slot = 0u;
        clap_id insertOffset = 0u;
        if (!decodeInsertOffset(offset, slot, insertOffset)) return;
        auto& insert = laneParams.inserts[slot];
        switch (insertOffset) {
        case kInsertTypeOffset:
            insert.type = static_cast<s3g::NoInputDistortionType>(
                static_cast<uint32_t>(std::clamp(
                    std::lround(value), 0l,
                    static_cast<long>(s3g::kNoInputDistortionTypeCount - 1u))));
            break;
        case kInsertGainOffset:
            insert.gain = static_cast<float>(std::clamp(value, 0.0, 1.0));
            break;
        case kInsertToneOffset:
            insert.tone = static_cast<float>(std::clamp(value, 0.0, 1.0));
            break;
        case kInsertBiasOffset:
            insert.bias = static_cast<float>(std::clamp(value, -1.0, 1.0));
            break;
        case kInsertLevelOffset:
            insert.levelDb = static_cast<float>(
                std::clamp(value, -24.0, 12.0));
            break;
        case kInsertBypassOffset:
            insert.bypass = value >= 0.5 ? 1u : 0u;
            break;
        default: return;
        }
        break;
    }
    }
    plugin.mixer.setParams(plugin.params);
}

bool paramValue(const Plugin& plugin, clap_id id, double& value)
{
    switch (id) {
    case kOutputParamId: value = plugin.params.outputGainDb; return true;
    case kCeilingParamId: value = plugin.params.ceilingDb; return true;
    case kLimiterParamId: value = plugin.params.limiterEnabled; return true;
    case kDcBlockParamId: value = plugin.params.dcBlockEnabled; return true;
    case kFeedbackParamId: value = plugin.params.feedback; return true;
    case kCouplingParamId: value = plugin.params.coupling; return true;
    case kPhaseParamId: value = plugin.params.phase; return true;
    case kDriftParamId: value = plugin.params.drift; return true;
    case kFormantParamId: value = plugin.params.formant; return true;
    case kQualityParamId: value = plugin.params.quality; return true;
    default: break;
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        value = plugin.params.matrix[destination * kChannelCount + source];
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    const auto& laneParams = plugin.params.lanes[lane];
    switch (offset) {
    case kLaneBodyOffset: value = laneParams.body; return true;
    case kLaneLossOffset: value = laneParams.loss; return true;
    case kLaneLevelOffset: value = laneParams.levelDb; return true;
    case kLaneMuteOffset: value = laneParams.mute; return true;
    case kLaneLowOffset: value = laneParams.lowDb; return true;
    case kLaneMidFrequencyOffset:
        value = laneParams.midFrequencyHz; return true;
    case kLaneMidGainOffset: value = laneParams.midGainDb; return true;
    case kLaneHighOffset: value = laneParams.highDb; return true;
    default: break;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    const auto& insert = laneParams.inserts[slot];
    switch (insertOffset) {
    case kInsertTypeOffset:
        value = static_cast<double>(insert.type); return true;
    case kInsertGainOffset: value = insert.gain; return true;
    case kInsertToneOffset: value = insert.tone; return true;
    case kInsertBiasOffset: value = insert.bias; return true;
    case kInsertLevelOffset: value = insert.levelDb; return true;
    case kInsertBypassOffset: value = insert.bypass; return true;
    default: return false;
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

void resetMeters(Plugin& plugin)
{
    plugin.outputPeak.store(0.0f, std::memory_order_relaxed);
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        plugin.lanePeaks[lane].store(0.0f, std::memory_order_relaxed);
        plugin.laneActivity[lane].store(0.0f, std::memory_order_relaxed);
    }
    plugin.networkActivity.store(0.0f, std::memory_order_relaxed);
    plugin.minimumGovernor.store(1.0f, std::memory_order_relaxed);
    plugin.containmentState.store(
        static_cast<uint32_t>(s3g::NoInputContainmentState::Quiet),
        std::memory_order_relaxed);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->mixer.prepare(sampleRate);
    p->mixer.setParams(p->params);
    p->mixer.reseed(p->params.seed, 0.48f);
    resetMeters(*p);
    p->seedRequested.store(false, std::memory_order_relaxed);
    p->panicRequested.store(false, std::memory_order_relaxed);
    p->killMask.store(0u, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->mixer.setParams(p->params);
    p->mixer.reseed(p->params.seed, 0.48f);
    resetMeters(*p);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) {
            continue;
        }
        const auto* param =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, param->param_id, param->value);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    if (p->seedRequested.exchange(false, std::memory_order_acq_rel)) {
        p->params.seed = p->params.seed * 1664525u + 1013904223u;
        if (p->params.seed == 0u) p->params.seed = 1u;
        p->mixer.setParams(p->params);
        p->mixer.reseed(p->params.seed, 0.56f);
    }
    if (p->panicRequested.exchange(false, std::memory_order_acq_rel)) {
        p->mixer.panic();
    }
    uint32_t killMask = p->killMask.exchange(0u,
        std::memory_order_acq_rel);
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        if ((killMask & (1u << lane)) != 0u) p->mixer.killLane(lane);
    }

    if (process->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t& output = process->audio_outputs[0];
    if (!output.data32 && !output.data64) return CLAP_PROCESS_CONTINUE;
    const uint32_t writableChannels = std::min<uint32_t>(
        output.channel_count, kChannelCount);
    std::array<float, kChannelCount> blockPeaks {};
    float blockPeak = 0.0f;

    for (uint32_t frame = 0u; frame < process->frames_count; ++frame) {
        p->mixer.processFrame(p->frame.data());
        for (uint32_t lane = 0u; lane < writableChannels; ++lane) {
            const float value = p->frame[lane];
            if (output.data32 && output.data32[lane]) {
                output.data32[lane][frame] = value;
            }
            if (output.data64 && output.data64[lane]) {
                output.data64[lane][frame] = static_cast<double>(value);
            }
            blockPeaks[lane] = std::max(blockPeaks[lane], std::abs(value));
            blockPeak = std::max(blockPeak, std::abs(value));
        }
        for (uint32_t lane = writableChannels;
             lane < output.channel_count; ++lane) {
            if (output.data32 && output.data32[lane]) {
                output.data32[lane][frame] = 0.0f;
            }
            if (output.data64 && output.data64[lane]) {
                output.data64[lane][frame] = 0.0;
            }
        }
    }

    p->outputPeak.store(std::max(
        p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak),
        std::memory_order_relaxed);
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        p->lanePeaks[lane].store(std::max(
            p->lanePeaks[lane].load(std::memory_order_relaxed) * 0.90f,
            blockPeaks[lane]), std::memory_order_relaxed);
        p->laneActivity[lane].store(p->mixer.laneActivity(lane),
            std::memory_order_relaxed);
    }
    p->networkActivity.store(p->mixer.networkActivity(),
        std::memory_order_relaxed);
    p->minimumGovernor.store(p->mixer.minimumGovernor(),
        std::memory_order_relaxed);
    p->containmentState.store(
        static_cast<uint32_t>(p->mixer.containmentState()),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20u;
    std::snprintf(info->name, sizeof(info->name), "8ch Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet,
};

uint32_t paramsCount(const clap_plugin_t*) { return kTotalParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kTotalParamCount) return false;
    const clap_id id = paramIdAtIndex(index);
    ParamRange range;
    if (id == CLAP_INVALID_ID || !paramRange(id, range)) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (range.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    info->min_value = range.minimum;
    info->max_value = range.maximum;
    info->default_value = range.defaultValue;

    for (const auto& def : kGlobalParamDefs) {
        if (def.id == id) {
            std::snprintf(info->name, sizeof(info->name), "%s", def.name);
            std::snprintf(info->module, sizeof(info->module), "%s",
                def.module);
            return true;
        }
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        std::snprintf(info->name, sizeof(info->name), "Route L%u to L%u",
            source + 1u, destination + 1u);
        std::snprintf(info->module, sizeof(info->module), "Matrix");
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    static constexpr const char* laneNames[] {
        "Body", "Loss", "Level", "Mute", "Low", "Mid Frequency",
        "Mid Gain", "High",
    };
    if (offset < kLaneDirectParamCount) {
        std::snprintf(info->name, sizeof(info->name), "%s",
            laneNames[offset]);
        std::snprintf(info->module, sizeof(info->module), "Lane %u",
            lane + 1u);
        return true;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    static constexpr const char* insertNames[] {
        "Type", "Gain", "Tone", "Bias", "Level", "Bypass",
    };
    std::snprintf(info->name, sizeof(info->name), "Slot %u %s",
        slot + 1u, insertNames[insertOffset]);
    std::snprintf(info->module, sizeof(info->module), "Lane %u / Insert %u",
        lane + 1u, slot + 1u);
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    return value && paramValue(*self(plugin), id, *value);
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kOutputParamId || id == kCeilingParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    }
    if (id == kLimiterParamId || id == kDcBlockParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
        return true;
    }
    if (id == kQualityParamId) {
        const uint32_t quality = static_cast<uint32_t>(
            std::clamp(std::lround(value), 0l, 2l));
        std::snprintf(display, size, "%uX", 1u << quality);
        return true;
    }
    if (id >= kFeedbackParamId && id <= kFormantParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    }
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(id, destination, source)) {
        std::snprintf(display, size, "%+.2f", value);
        return true;
    }
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(id, lane, offset)) return false;
    if (offset == kLaneBodyOffset || offset == kLaneLossOffset) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    }
    if (offset == kLaneLevelOffset || offset == kLaneLowOffset
        || offset == kLaneMidGainOffset || offset == kLaneHighOffset) {
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    }
    if (offset == kLaneMuteOffset) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
        return true;
    }
    if (offset == kLaneMidFrequencyOffset) {
        if (value >= 1000.0) std::snprintf(display, size, "%.2f kHz",
            value * 0.001);
        else std::snprintf(display, size, "%.0f Hz", value);
        return true;
    }
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (!decodeInsertOffset(offset, slot, insertOffset)) return false;
    if (insertOffset == kInsertTypeOffset) {
        const uint32_t type = static_cast<uint32_t>(std::clamp(
            std::lround(value), 0l,
            static_cast<long>(s3g::kNoInputDistortionTypeCount - 1u)));
        std::snprintf(display, size, "%s", s3g::noInputDistortionName(
            static_cast<s3g::NoInputDistortionType>(type)));
    } else if (insertOffset == kInsertGainOffset
        || insertOffset == kInsertToneOffset) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (insertOffset == kInsertBiasOffset) {
        std::snprintf(display, size, "%+.2f", value);
    } else if (insertOffset == kInsertLevelOffset) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (insertOffset == kInsertBypassOffset) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else {
        return false;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (decodeLaneParam(id, lane, offset)) {
        uint32_t slot = 0u;
        clap_id insertOffset = 0u;
        if (decodeInsertOffset(offset, slot, insertOffset)
            && insertOffset == kInsertTypeOffset) {
            for (uint32_t type = 0u;
                 type < s3g::kNoInputDistortionTypeCount; ++type) {
                if (std::strcmp(display, s3g::noInputDistortionName(
                        static_cast<s3g::NoInputDistortionType>(type))) == 0) {
                    *value = type;
                    return true;
                }
            }
        }
    }
    if (std::strcmp(display, "ON") == 0) { *value = 1.0; return true; }
    if (std::strcmp(display, "OFF") == 0) { *value = 0.0; return true; }
    if (id == kQualityParamId) {
        const double parsed = std::atof(display);
        *value = parsed >= 4.0 ? 2.0 : (parsed >= 2.0 ? 1.0 : 0.0);
        return true;
    }
    *value = std::atof(display);
    if (std::strchr(display, '%')) *value *= 0.01;
    if (decodeLaneParam(id, lane, offset)
        && offset == kLaneMidFrequencyOffset
        && (std::strstr(display, "kHz") || std::strstr(display, "khz"))) {
        *value *= 1000.0;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    SavedState state;
    state.params = p->params;
    state.selectedLane = p->selectedLane.load(std::memory_order_relaxed);
    state.selectedSlot = p->selectedSlot.load(std::memory_order_relaxed);
    state.selectedSource = p->selectedSource.load(std::memory_order_relaxed);
    state.selectedDestination = p->selectedDestination.load(
        std::memory_order_relaxed);
    state.guiPage = p->guiPage.load(std::memory_order_relaxed);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
    uint64_t offset = 0u;
    while (offset < sizeof(state)) {
        const int64_t written = stream->write(stream, bytes + offset,
            sizeof(state) - offset);
        if (written <= 0) return false;
        offset += static_cast<uint64_t>(written);
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state;
    auto* bytes = reinterpret_cast<uint8_t*>(&state);
    uint64_t offset = 0u;
    while (offset < sizeof(state)) {
        const int64_t read = stream->read(stream, bytes + offset,
            sizeof(state) - offset);
        if (read <= 0) return false;
        offset += static_cast<uint64_t>(read);
    }
    if (state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = s3g::sanitizeNoInputMixerParams(state.params);
    p->selectedLane.store(std::min<uint32_t>(state.selectedLane,
        kChannelCount - 1u), std::memory_order_relaxed);
    p->selectedSlot.store(std::min<uint32_t>(state.selectedSlot,
        s3g::kNoInputMixerInsertSlots - 1u), std::memory_order_relaxed);
    p->selectedSource.store(std::min<uint32_t>(state.selectedSource,
        kChannelCount - 1u), std::memory_order_relaxed);
    p->selectedDestination.store(std::min<uint32_t>(
        state.selectedDestination, kChannelCount - 1u),
        std::memory_order_relaxed);
    p->guiPage.store(std::min<uint32_t>(state.guiPage, 2u),
        std::memory_order_relaxed);
    p->mixer.setParams(p->params);
    p->mixer.reseed(p->params.seed, 0.48f);
    resetMeters(*p);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

enum OpenMenu : int {
    kMenuNone = -1,
    kMenuLane = 0,
    kMenuSource,
    kMenuDestination,
    kMenuSlot0,
    kMenuSlot1,
    kMenuSlot2,
    kMenuQuality,
};

NSRect processorMenuRect(const s3g::gui_layout::Panel& panel,
    uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

NSRect fieldTabRect(uint32_t index)
{
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    constexpr CGFloat width = 62.0;
    constexpr CGFloat gap = 6.0;
    return NSMakeRect(family.fieldPanel.x + family.fieldPanel.width
            - 3.0 * width - 2.0 * gap - 10.0
            + static_cast<CGFloat>(index) * (width + gap),
        family.fieldPanel.y + 4.0, width, 14.0);
}

NSColor* mixerColor(int rgb, CGFloat alpha = 1.0)
{
    return s3g::clap_gui::color(rgb, alpha);
}

void drawFlatButton(NSRect rect, NSString* text, bool active,
    NSDictionary* attrs)
{
    [mixerColor(active ? 0x303030 : 0x151515) setFill];
    NSRectFill(rect);
    [mixerColor(active ? 0xb8b8b8 : 0x555555) setStroke];
    NSFrameRect(rect);
    const NSSize size = [text sizeWithAttributes:attrs];
    [text drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
}

} // namespace

@interface S3GNoInputMixerView : NSView {
    void* _plugin;
    clap_id _dragParam;
    NSTimer* _timer;
    int _openMenu;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GNoInputMixerView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _timer = nil;
        _openMenu = kMenuNone;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            "INIT");
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
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawSlider:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm row:(uint32_t)row
    panel:(const s3g::gui_layout::Panel&)panel
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorSlider(name, value, norm,
        s3g::gui_layout::rowY(panel, row), panel.frame.x,
        panel.frame.width, label, valueAttrs, style);
}

- (void)drawPrimaryMatrix:(Plugin*)plugin rect:(NSRect)rect
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
{
    [mixerColor(0x101010) setFill];
    NSRectFill(rect);
    [mixerColor(0x454545) setStroke];
    NSFrameRect(rect);
    const CGFloat gridLeft = rect.origin.x + 54.0;
    const CGFloat gridTop = rect.origin.y + 36.0;
    const CGFloat spacing = 58.0;
    const CGFloat gridExtent = spacing * 7.0;
    const uint32_t selectedSource = plugin->selectedSource.load(
        std::memory_order_relaxed);
    const uint32_t selectedDestination = plugin->selectedDestination.load(
        std::memory_order_relaxed);

    [mixerColor(0x343434) setStroke];
    for (uint32_t index = 0u; index < kChannelCount; ++index) {
        const CGFloat x = gridLeft + spacing * index;
        const CGFloat y = gridTop + spacing * index;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, gridTop)
            toPoint:NSMakePoint(x, gridTop + gridExtent)];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(gridLeft, y)
            toPoint:NSMakePoint(gridLeft + gridExtent, y)];
        [[NSString stringWithFormat:@"%u", index + 1u]
            drawAtPoint:NSMakePoint(x - 3.0, gridTop - 24.0)
            withAttributes:valueAttrs];
        [[NSString stringWithFormat:@"%u", index + 1u]
            drawAtPoint:NSMakePoint(gridLeft - 28.0, y - 6.0)
            withAttributes:valueAttrs];
    }

    for (uint32_t destination = 0u; destination < kChannelCount;
         ++destination) {
        for (uint32_t source = 0u; source < kChannelCount; ++source) {
            const float gain = plugin->params.matrix[
                destination * kChannelCount + source];
            const CGFloat x = gridLeft + spacing * source;
            const CGFloat y = gridTop + spacing * destination;
            const NSRect node = NSMakeRect(x - 7.0, y - 7.0, 14.0, 14.0);
            [mixerColor(0x141414) setFill];
            NSRectFill(node);
            if (std::abs(gain) > 0.001f) {
                [mixerColor(gain >= 0.0f ? 0xc95e3b : 0x5daeb6,
                    0.30 + std::abs(gain) * 0.65) setFill];
                NSRectFill(NSInsetRect(node, 2.0, 2.0));
            }
            [mixerColor((source == selectedSource
                && destination == selectedDestination)
                    ? 0xc8c8c8 : 0x5a5a5a) setStroke];
            NSFrameRect(node);
            if (std::abs(gain) > 0.001f) {
                [(gain >= 0.0f ? @"+" : @"−") drawAtPoint:
                    NSMakePoint(x + 10.0, y - 8.0)
                    withAttributes:valueAttrs];
            }
        }
    }

    const CGFloat meterTop = gridTop + gridExtent + 42.0;
    const CGFloat meterHeight = 112.0;
    const CGFloat meterWidth = 48.0;
    const CGFloat meterGap = 20.0;
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const CGFloat x = rect.origin.x + 24.0
            + static_cast<CGFloat>(lane) * (meterWidth + meterGap);
        [[NSString stringWithFormat:@"L%u", lane + 1u]
            drawAtPoint:NSMakePoint(x + 13.0, meterTop - 19.0)
            withAttributes:label];
        NSRect track = NSMakeRect(x + 17.0, meterTop, 14.0, meterHeight);
        [mixerColor(0x070707) setFill]; NSRectFill(track);
        [mixerColor(0x3e3e3e) setStroke]; NSFrameRect(track);
        const CGFloat peak = std::clamp<CGFloat>(
            plugin->lanePeaks[lane].load(std::memory_order_relaxed),
            0.0, 1.0);
        NSRect fill = NSInsetRect(track, 2.0, 2.0);
        fill.origin.y += fill.size.height * (1.0 - peak);
        fill.size.height *= peak;
        [mixerColor(0x57bfc4, 0.85) setFill]; NSRectFill(fill);
    }
    [@"SEED → BODY/FORMANT → EQ → INSERTS → SEND → RETURN"
        drawAtPoint:NSMakePoint(rect.origin.x + 14.0,
            NSMaxY(rect) - 23.0) withAttributes:valueAttrs];
}

- (void)drawPrimaryLanes:(Plugin*)plugin rect:(NSRect)rect
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
{
    [mixerColor(0x101010) setFill]; NSRectFill(rect);
    [mixerColor(0x454545) setStroke]; NSFrameRect(rect);
    const uint32_t selected = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const CGFloat gap = 12.0;
    const CGFloat cellWidth = (rect.size.width - gap * 5.0) / 4.0;
    const CGFloat cellHeight = (rect.size.height - gap * 3.0) / 2.0;
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const uint32_t column = lane % 4u;
        const uint32_t row = lane / 4u;
        NSRect cell = NSMakeRect(rect.origin.x + gap
                + column * (cellWidth + gap),
            rect.origin.y + gap + row * (cellHeight + gap),
            cellWidth, cellHeight);
        [mixerColor(lane == selected ? 0x242424 : 0x171717) setFill];
        NSRectFill(cell);
        [mixerColor(lane == selected ? 0xb8b8b8 : 0x494949) setStroke];
        NSFrameRect(cell);
        [[NSString stringWithFormat:@"L%u", lane + 1u]
            drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 9.0) withAttributes:label];
        const auto& laneParams = plugin->params.lanes[lane];
        [[NSString stringWithFormat:@"BODY %.0f  LOSS %.0f",
            laneParams.body * 100.0f, laneParams.loss * 100.0f]
            drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 35.0) withAttributes:valueAttrs];
        [[NSString stringWithFormat:@"EQ %+.0f / %+.0f / %+.0f",
            laneParams.lowDb, laneParams.midGainDb, laneParams.highDb]
            drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 57.0) withAttributes:valueAttrs];
        for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots;
             ++slot) {
            NSString* text = [NSString stringWithFormat:@"%u %@%@",
                slot + 1u,
                [NSString stringWithUTF8String:s3g::noInputDistortionName(
                    laneParams.inserts[slot].type)],
                laneParams.inserts[slot].bypass != 0u ? @" BYP" : @""];
            [text drawAtPoint:NSMakePoint(cell.origin.x + 10.0,
                cell.origin.y + 91.0 + slot * 22.0)
                withAttributes:valueAttrs];
        }
        NSRect activity = NSMakeRect(cell.origin.x + 10.0,
            NSMaxY(cell) - 25.0, cell.size.width - 20.0, 8.0);
        [mixerColor(0x090909) setFill]; NSRectFill(activity);
        [mixerColor(0x404040) setStroke]; NSFrameRect(activity);
        NSRect activityFill = NSInsetRect(activity, 1.0, 1.0);
        activityFill.size.width *= std::clamp<CGFloat>(
            plugin->laneActivity[lane].load(std::memory_order_relaxed),
            0.0, 1.0);
        [mixerColor(0x57bfc4, 0.8) setFill]; NSRectFill(activityFill);
    }
}

- (void)drawPrimaryInserts:(Plugin*)plugin rect:(NSRect)rect
    label:(NSDictionary*)label valueAttrs:(NSDictionary*)valueAttrs
{
    [mixerColor(0x101010) setFill]; NSRectFill(rect);
    [mixerColor(0x454545) setStroke]; NSFrameRect(rect);
    const uint32_t selectedLane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const uint32_t selectedSlot = plugin->selectedSlot.load(
        std::memory_order_relaxed);
    const CGFloat left = rect.origin.x + 18.0;
    const CGFloat top = rect.origin.y + 28.0;
    const CGFloat rowHeight = 76.0;
    const CGFloat slotWidth = 142.0;
    for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
        const CGFloat y = top + lane * rowHeight;
        [[NSString stringWithFormat:@"L%u", lane + 1u]
            drawAtPoint:NSMakePoint(left, y + 20.0) withAttributes:label];
        for (uint32_t slot = 0u; slot < s3g::kNoInputMixerInsertSlots;
             ++slot) {
            NSRect cell = NSMakeRect(left + 42.0 + slot * (slotWidth + 12.0),
                y, slotWidth, 50.0);
            const bool active = lane == selectedLane && slot == selectedSlot;
            [mixerColor(active ? 0x292929 : 0x171717) setFill];
            NSRectFill(cell);
            [mixerColor(active ? 0xb8b8b8 : 0x4a4a4a) setStroke];
            NSFrameRect(cell);
            const auto& insert = plugin->params.lanes[lane].inserts[slot];
            [[NSString stringWithFormat:@"S%u  %@", slot + 1u,
                [NSString stringWithUTF8String:s3g::noInputDistortionName(
                    insert.type)]]
                drawAtPoint:NSMakePoint(cell.origin.x + 8.0,
                    cell.origin.y + 8.0) withAttributes:valueAttrs];
            [[NSString stringWithFormat:@"G %.0f  T %.0f%@",
                insert.gain * 100.0f, insert.tone * 100.0f,
                insert.bypass != 0u ? @"  BYP" : @""]
                drawAtPoint:NSMakePoint(cell.origin.x + 8.0,
                    cell.origin.y + 28.0) withAttributes:valueAttrs];
        }
    }
}

- (NSRect)menuAnchorRect:(int)menu
{
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    switch (menu) {
    case kMenuLane: return processorMenuRect(family.selectedLane, 0u);
    case kMenuSource: return processorMenuRect(family.crosspoint, 0u);
    case kMenuDestination: return processorMenuRect(family.crosspoint, 1u);
    case kMenuSlot0: return processorMenuRect(family.inserts, 0u);
    case kMenuSlot1: return processorMenuRect(family.inserts, 1u);
    case kMenuSlot2: return processorMenuRect(family.inserts, 2u);
    case kMenuQuality:
        return NSMakeRect(family.containment.frame.x + 108.0,
            family.containment.frame.y + 35.0, 110.0, 15.0);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuItemCount:(int)menu
{
    if (menu == kMenuSlot0 || menu == kMenuSlot1 || menu == kMenuSlot2) {
        return s3g::kNoInputDistortionTypeCount;
    }
    if (menu == kMenuQuality) return 3u;
    if (menu == kMenuLane || menu == kMenuSource
        || menu == kMenuDestination) return kChannelCount;
    return 0u;
}

- (NSRect)menuDropdownRect:(int)menu
{
    const NSRect anchor = [self menuAnchorRect:menu];
    return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width,
        18.0 * static_cast<CGFloat>([self menuItemCount:menu]));
}

- (void)drawOpenMenu:(Plugin*)plugin attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == kMenuNone) return;
    NSString* laneItems[kChannelCount] = {
        @"L1", @"L2", @"L3", @"L4", @"L5", @"L6", @"L7", @"L8",
    };
    NSString* typeItems[s3g::kNoInputDistortionTypeCount] = {
        @"BYPASS", @"MUFF", @"RAT", @"ZONE A", @"ZONE B",
        @"FUZZ I", @"FUZZ II", @"DIODE", @"RING",
    };
    NSString* qualityItems[3] = { @"1X", @"2X", @"4X" };
    NSString** items = laneItems;
    uint32_t count = kChannelCount;
    int selected = 0;
    if (_openMenu == kMenuLane) {
        selected = static_cast<int>(plugin->selectedLane.load(
            std::memory_order_relaxed));
    } else if (_openMenu == kMenuSource) {
        selected = static_cast<int>(plugin->selectedSource.load(
            std::memory_order_relaxed));
    } else if (_openMenu == kMenuDestination) {
        selected = static_cast<int>(plugin->selectedDestination.load(
            std::memory_order_relaxed));
    } else if (_openMenu == kMenuQuality) {
        items = qualityItems;
        count = 3u;
        selected = static_cast<int>(plugin->params.quality);
    } else {
        items = typeItems;
        count = s3g::kNoInputDistortionTypeCount;
        const uint32_t lane = plugin->selectedLane.load(
            std::memory_order_relaxed);
        const uint32_t slot = static_cast<uint32_t>(
            _openMenu - kMenuSlot0);
        selected = static_cast<int>(plugin->params.lanes[lane]
            .inserts[slot].type);
    }
    s3g::clap_gui::drawDropdownMenu([self menuDropdownRect:_openMenu],
        18.0, items, count, selected, -1, attrs, style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::gui_layout::matrixTitleBand(family.canvas);
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR NO INPUT MIXER 8CH",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(plugin->outputPeak.load(
            std::memory_order_relaxed)), titleBand, title, label, value,
        style);

    s3g::clap_gui::drawPanelFrame(family.fieldPanel.x,
        family.fieldPanel.y, family.fieldPanel.width,
        family.fieldPanel.height, style);
    s3g::clap_gui::drawPanelHeader(@"FEEDBACK MATRIX", true,
        family.fieldPanel.x, family.fieldPanel.y, family.fieldPanel.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, label, style);
    const uint32_t page = plugin->guiPage.load(std::memory_order_relaxed);
    static NSString* pageNames[3] = { @"MATRIX", @"LANES", @"INSERTS" };
    for (uint32_t index = 0u; index < 3u; ++index) {
        s3g::clap_gui::drawHeaderButton(fieldTabRect(index),
            s3g::clap_gui::cocoaRect(family.fieldPanel), pageNames[index],
            page == index, value, style);
    }
    const NSRect fieldPlot = s3g::clap_gui::cocoaRect(family.fieldPlot);
    if (page == 0u) [self drawPrimaryMatrix:plugin rect:fieldPlot
        label:label valueAttrs:value];
    else if (page == 1u) [self drawPrimaryLanes:plugin rect:fieldPlot
        label:label valueAttrs:value];
    else [self drawPrimaryInserts:plugin rect:fieldPlot
        label:label valueAttrs:value];

    const auto drawPanel = [&](NSString* name,
        const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel, style);
        s3g::clap_gui::drawPanelHeader(name, true, panel, label, style);
    };
    drawPanel(@"OUTPUT", family.output);
    drawPanel(@"NETWORK", family.network);
    drawPanel(@"SELECTED LANE", family.selectedLane);
    drawPanel(@"CROSSPOINT", family.crosspoint);
    const uint32_t lane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const uint32_t slot = plugin->selectedSlot.load(
        std::memory_order_relaxed);
    drawPanel([NSString stringWithFormat:@"EQ — L%u", lane + 1u], family.eq);
    drawPanel([NSString stringWithFormat:@"INSERTS — L%u / S%u",
        lane + 1u, slot + 1u], family.inserts);
    drawPanel(@"CONTAINMENT", family.containment);

    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB",
            plugin->params.outputGainDb]
        norm:(plugin->params.outputGainDb + 60.0f) / 66.0f row:0u
        panel:family.output label:label valueAttrs:value style:style];
    [self drawSlider:@"CEIL"
        value:[NSString stringWithFormat:@"%+.1f dB",
            plugin->params.ceilingDb]
        norm:(plugin->params.ceilingDb + 18.0f) / 18.0f row:1u
        panel:family.output label:label valueAttrs:value style:style];
    s3g::clap_gui::drawToggle(@"LIMIT",
        plugin->params.limiterEnabled != 0u,
        s3g::gui_layout::rowY(family.output, 2u), label, value, style,
        s3g::gui_layout::processorLabelX(family.output.frame.x),
        s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);
    s3g::clap_gui::drawToggle(@"DC BLOCK",
        plugin->params.dcBlockEnabled != 0u,
        s3g::gui_layout::rowY(family.output, 3u), label, value, style,
        s3g::gui_layout::processorLabelX(family.output.frame.x),
        s3g::gui_layout::processorControlX(family.output.frame.x), 82.0);

    [@"SEED" drawAtPoint:NSMakePoint(
        s3g::gui_layout::processorLabelX(family.network.frame.x),
        s3g::gui_layout::rowY(family.network, 0u) - 2.0)
        withAttributes:label];
    drawFlatButton(processorMenuRect(family.network, 0u), @"NEW", false,
        value);
    [self drawSlider:@"FDBK"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.feedback * 100.0f]
        norm:plugin->params.feedback / 1.25f row:1u panel:family.network
        label:label valueAttrs:value style:style];
    [self drawSlider:@"COUPL"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.coupling * 100.0f]
        norm:plugin->params.coupling / 1.25f row:2u panel:family.network
        label:label valueAttrs:value style:style];
    [self drawSlider:@"PHASE"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.phase * 100.0f]
        norm:plugin->params.phase row:3u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"DRIFT"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.drift * 100.0f]
        norm:plugin->params.drift row:4u panel:family.network label:label
        valueAttrs:value style:style];
    [self drawSlider:@"FORMANT"
        value:[NSString stringWithFormat:@"%.0f%%",
            plugin->params.formant * 100.0f]
        norm:plugin->params.formant row:5u panel:family.network label:label
        valueAttrs:value style:style];

    s3g::clap_gui::drawProcessorMenu(@"LANE",
        [NSString stringWithFormat:@"L%u", lane + 1u],
        s3g::gui_layout::rowY(family.selectedLane, 0u),
        family.selectedLane.frame.x, family.selectedLane.frame.width,
        label, value, style);
    const auto& laneParams = plugin->params.lanes[lane];
    [self drawSlider:@"BODY"
        value:[NSString stringWithFormat:@"%.0f%%", laneParams.body * 100.0f]
        norm:laneParams.body row:1u panel:family.selectedLane label:label
        valueAttrs:value style:style];
    [self drawSlider:@"LOSS"
        value:[NSString stringWithFormat:@"%.0f%%", laneParams.loss * 100.0f]
        norm:laneParams.loss row:2u panel:family.selectedLane label:label
        valueAttrs:value style:style];
    [self drawSlider:@"LEVEL"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.levelDb]
        norm:(laneParams.levelDb + 60.0f) / 72.0f row:3u
        panel:family.selectedLane label:label valueAttrs:value style:style];
    const CGFloat selectedButtonY = s3g::gui_layout::rowY(
        family.selectedLane, 4u) - 1.0;
    drawFlatButton(NSMakeRect(
        s3g::gui_layout::processorControlX(family.selectedLane.frame.x),
        selectedButtonY, 78.0, 17.0), @"MUTE",
        laneParams.mute != 0u, value);
    drawFlatButton(NSMakeRect(
        s3g::gui_layout::processorControlX(family.selectedLane.frame.x)
            + 88.0,
        selectedButtonY, 78.0, 17.0), @"KILL", false, value);

    const uint32_t source = plugin->selectedSource.load(
        std::memory_order_relaxed);
    const uint32_t destination = plugin->selectedDestination.load(
        std::memory_order_relaxed);
    const float route = plugin->params.matrix[
        destination * kChannelCount + source];
    s3g::clap_gui::drawProcessorMenu(@"SRC",
        [NSString stringWithFormat:@"L%u", source + 1u],
        s3g::gui_layout::rowY(family.crosspoint, 0u),
        family.crosspoint.frame.x, family.crosspoint.frame.width,
        label, value, style);
    s3g::clap_gui::drawProcessorMenu(@"DEST",
        [NSString stringWithFormat:@"L%u", destination + 1u],
        s3g::gui_layout::rowY(family.crosspoint, 1u),
        family.crosspoint.frame.x, family.crosspoint.frame.width,
        label, value, style);
    [self drawSlider:@"GAIN"
        value:[NSString stringWithFormat:@"%+.2f", route]
        norm:(route + 1.0f) * 0.5f row:2u panel:family.crosspoint
        label:label valueAttrs:value style:style];
    const CGFloat routeButtonX = s3g::gui_layout::processorControlX(
        family.crosspoint.frame.x);
    drawFlatButton(NSMakeRect(routeButtonX,
        s3g::gui_layout::rowY(family.crosspoint, 3u) - 1.0,
        98.0, 17.0), route < 0.0f ? @"NEGATIVE" : @"POSITIVE",
        route != 0.0f, value);
    drawFlatButton(NSMakeRect(routeButtonX,
        s3g::gui_layout::rowY(family.crosspoint, 4u) - 1.0,
        98.0, 17.0), @"CLEAR", false, value);

    [self drawSlider:@"LOW"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.lowDb]
        norm:(laneParams.lowDb + 18.0f) / 36.0f row:0u panel:family.eq
        label:label valueAttrs:value style:style];
    const float midNorm = std::log(laneParams.midFrequencyHz / 80.0f)
        / std::log(8000.0f / 80.0f);
    [self drawSlider:@"MID F"
        value:(laneParams.midFrequencyHz >= 1000.0f
            ? [NSString stringWithFormat:@"%.2f k", laneParams.midFrequencyHz * 0.001f]
            : [NSString stringWithFormat:@"%.0f Hz", laneParams.midFrequencyHz])
        norm:midNorm row:1u panel:family.eq label:label valueAttrs:value
        style:style];
    [self drawSlider:@"MID G"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.midGainDb]
        norm:(laneParams.midGainDb + 18.0f) / 36.0f row:2u panel:family.eq
        label:label valueAttrs:value style:style];
    [self drawSlider:@"HIGH"
        value:[NSString stringWithFormat:@"%+.1f dB", laneParams.highDb]
        norm:(laneParams.highDb + 18.0f) / 36.0f row:3u panel:family.eq
        label:label valueAttrs:value style:style];

    for (uint32_t insertSlot = 0u;
         insertSlot < s3g::kNoInputMixerInsertSlots; ++insertSlot) {
        const auto& insert = laneParams.inserts[insertSlot];
        s3g::clap_gui::drawProcessorMenu(
            [NSString stringWithFormat:@"SLOT %u", insertSlot + 1u],
            [NSString stringWithUTF8String:s3g::noInputDistortionName(
                insert.type)], s3g::gui_layout::rowY(
                family.inserts, insertSlot), family.inserts.frame.x,
            family.inserts.frame.width, label, value, style);
    }
    const auto& insert = laneParams.inserts[slot];
    [self drawSlider:@"GAIN"
        value:[NSString stringWithFormat:@"%.0f%%", insert.gain * 100.0f]
        norm:insert.gain row:3u panel:family.inserts label:label
        valueAttrs:value style:style];
    [self drawSlider:@"TONE"
        value:[NSString stringWithFormat:@"%.0f%%", insert.tone * 100.0f]
        norm:insert.tone row:4u panel:family.inserts label:label
        valueAttrs:value style:style];
    [self drawSlider:@"BIAS"
        value:[NSString stringWithFormat:@"%+.2f", insert.bias]
        norm:(insert.bias + 1.0f) * 0.5f row:5u panel:family.inserts
        label:label valueAttrs:value style:style];
    [self drawSlider:@"LEVEL"
        value:[NSString stringWithFormat:@"%+.1f dB", insert.levelDb]
        norm:(insert.levelDb + 24.0f) / 36.0f row:6u panel:family.inserts
        label:label valueAttrs:value style:style];
    s3g::clap_gui::drawToggle(@"BYPASS", insert.bypass != 0u,
        s3g::gui_layout::rowY(family.inserts, 7u), label, value, style,
        s3g::gui_layout::processorLabelX(family.inserts.frame.x),
        s3g::gui_layout::processorControlX(family.inserts.frame.x), 82.0);

    [@"QUALITY" drawAtPoint:NSMakePoint(
        family.containment.frame.x + 16.0,
        family.containment.frame.y + 34.0) withAttributes:label];
    s3g::clap_gui::drawMenu(@"",
        [NSString stringWithFormat:@"%uX", 1u << plugin->params.quality],
        family.containment.frame.y + 36.0, label, value, style,
        family.containment.frame.x + 16.0,
        family.containment.frame.x + 108.0, 110.0);
    [@"ENERGY" drawAtPoint:NSMakePoint(
        family.containment.frame.x + 16.0,
        family.containmentMeter.y - 2.0) withAttributes:label];
    NSRect energyTrack = s3g::clap_gui::cocoaRect(
        family.containmentMeter);
    [mixerColor(0x101010) setFill]; NSRectFill(energyTrack);
    [mixerColor(0x454545) setStroke]; NSFrameRect(energyTrack);
    NSRect energyFill = NSInsetRect(energyTrack, 1.0, 1.0);
    energyFill.size.width *= std::clamp<CGFloat>(
        plugin->networkActivity.load(std::memory_order_relaxed), 0.0, 1.0);
    const float governor = plugin->minimumGovernor.load(
        std::memory_order_relaxed);
    [mixerColor(governor > 0.72f ? 0x57bfc4
        : (governor > 0.28f ? 0xc95e3b : 0xb83b32), 0.85) setFill];
    NSRectFill(energyFill);

    NSRect containmentField = s3g::clap_gui::cocoaRect(
        family.containmentField);
    [mixerColor(0x101010) setFill]; NSRectFill(containmentField);
    [mixerColor(0x454545) setStroke]; NSFrameRect(containmentField);
    for (uint32_t ring = 0u; ring < 4u; ++ring) {
        const CGFloat inset = 12.0 + ring * 10.0;
        [mixerColor(0x4a4a4a + ring * 0x080808,
            0.35 + (1.0f - governor) * 0.45) setStroke];
        NSFrameRect(NSInsetRect(containmentField, inset, inset * 0.55));
    }
    for (uint32_t node = 0u; node < kChannelCount; ++node) {
        const CGFloat angle = static_cast<CGFloat>(node) * 2.0 * M_PI / 8.0;
        const CGFloat x = NSMidX(containmentField)
            + std::cos(angle) * containmentField.size.width * 0.36;
        const CGFloat y = NSMidY(containmentField)
            + std::sin(angle) * containmentField.size.height * 0.32;
        const CGFloat activity = std::clamp<CGFloat>(
            plugin->laneActivity[node].load(std::memory_order_relaxed),
            0.0, 1.0);
        [mixerColor(activity > 0.72 ? 0xc95e3b : 0x777777,
            0.45 + activity * 0.5) setFill];
        NSRectFill(NSMakeRect(x - 3.0, y - 3.0, 6.0, 6.0));
    }
    const auto containment = static_cast<s3g::NoInputContainmentState>(
        plugin->containmentState.load(std::memory_order_relaxed));
    [[NSString stringWithUTF8String:s3g::noInputContainmentName(containment)]
        drawAtPoint:NSMakePoint(family.containment.frame.x + 16.0,
            family.panicButton.y + 9.0) withAttributes:value];
    NSRect panicRect = s3g::clap_gui::cocoaRect(family.panicButton);
    [mixerColor(0x7e2924) setFill]; NSRectFill(panicRect);
    [mixerColor(0xc95e3b) setStroke]; NSFrameRect(panicRect);
    const NSSize panicSize = [@"PANIC" sizeWithAttributes:label];
    [@"PANIC" drawAtPoint:NSMakePoint(
        panicRect.origin.x + (panicRect.size.width - panicSize.width) * 0.5,
        panicRect.origin.y + (panicRect.size.height - panicSize.height) * 0.5)
        withAttributes:label];

    [self drawOpenMenu:plugin attrs:value style:style];
}

- (const s3g::gui_layout::Panel*)panelForParam:(clap_id)param
{
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;
    if (param == kOutputParamId || param == kCeilingParamId)
        return &family.output;
    if (param >= kFeedbackParamId && param <= kFormantParamId)
        return &family.network;
    uint32_t destination = 0u;
    uint32_t source = 0u;
    if (decodeMatrixParam(param, destination, source))
        return &family.crosspoint;
    uint32_t lane = 0u;
    clap_id offset = 0u;
    if (!decodeLaneParam(param, lane, offset)) return nullptr;
    if (offset <= kLaneLevelOffset) return &family.selectedLane;
    if (offset >= kLaneLowOffset && offset <= kLaneHighOffset)
        return &family.eq;
    uint32_t slot = 0u;
    clap_id insertOffset = 0u;
    if (decodeInsertOffset(offset, slot, insertOffset))
        return &family.inserts;
    return nullptr;
}

- (void)updateSlider:(NSPoint)point
{
    if (_dragParam == CLAP_INVALID_ID) return;
    auto* plugin = static_cast<Plugin*>(_plugin);
    const auto* panel = [self panelForParam:_dragParam];
    ParamRange range;
    if (!panel || !paramRange(_dragParam, range)) return;
    const double controlX = s3g::gui_layout::processorControlX(
        panel->frame.x);
    const double width = s3g::gui_layout::processorTrackWidth(
        panel->frame.width);
    const double normalized = std::clamp(
        (point.x - controlX) / width, 0.0, 1.0);
    double value = range.minimum
        + normalized * (range.maximum - range.minimum);
    if (_dragParam == laneParamId(
            plugin->selectedLane.load(std::memory_order_relaxed),
            kLaneMidFrequencyOffset)) {
        value = 80.0 * std::pow(8000.0 / 80.0, normalized);
    }
    applyParam(*plugin, _dragParam, value);
    [self setNeedsDisplay:YES];
}

- (void)applyMenuSelection:(uint32_t)index
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    const uint32_t lane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    switch (_openMenu) {
    case kMenuLane:
        plugin->selectedLane.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        plugin->selectedDestination.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        break;
    case kMenuSource:
        plugin->selectedSource.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        break;
    case kMenuDestination:
        plugin->selectedDestination.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        plugin->selectedLane.store(std::min<uint32_t>(index,
            kChannelCount - 1u), std::memory_order_relaxed);
        break;
    case kMenuSlot0:
    case kMenuSlot1:
    case kMenuSlot2: {
        const uint32_t slot = static_cast<uint32_t>(_openMenu - kMenuSlot0);
        plugin->selectedSlot.store(slot, std::memory_order_relaxed);
        applyParam(*plugin, insertParamId(lane, slot, kInsertTypeOffset),
            index);
        break;
    }
    case kMenuQuality:
        applyParam(*plugin, kQualityParamId, index);
        break;
    default: break;
    }
}

- (void)beginSlider:(clap_id)param event:(NSEvent*)event point:(NSPoint)point
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    double defaultValue = 0.0;
    if (s3g::clap_gui::sliderDoubleClickDefault(event,
            &plugin->plugin, param, &defaultValue)) {
        applyParam(*plugin, param, defaultValue);
        _dragParam = CLAP_INVALID_ID;
    } else {
        _dragParam = param;
        [self updateSlider:point];
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* plugin = static_cast<Plugin*>(_plugin);
    const auto& family = s3g::gui_layout::kNoInputMixerFamilyLayout;

    if (_openMenu != kMenuNone) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            [self menuDropdownRect:_openMenu], 18.0,
            [self menuItemCount:_openMenu]);
        if (hit >= 0) [self applyMenuSelection:static_cast<uint32_t>(hit)];
        _openMenu = kMenuNone;
        [self setNeedsDisplay:YES];
        if (hit >= 0) return;
    }

    const auto titleBand = s3g::gui_layout::matrixTitleBand(family.canvas);
    const bool resetRequested = NSPointInRect(point,
        s3g::clap_gui::cocoaRect(titleBand.presetMenu));
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &plugin->plugin, @"No Input Mixer", titleBand,
            _titlePresetName, sizeof(_titlePresetName))) {
        if (resetRequested) {
            plugin->seedRequested.store(true, std::memory_order_release);
        }
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t page = 0u; page < 3u; ++page) {
        if (NSPointInRect(point, fieldTabRect(page))) {
            plugin->guiPage.store(page, std::memory_order_relaxed);
            [self setNeedsDisplay:YES];
            return;
        }
    }

    const uint32_t page = plugin->guiPage.load(std::memory_order_relaxed);
    const NSRect plot = s3g::clap_gui::cocoaRect(family.fieldPlot);
    if (NSPointInRect(point, plot)) {
        if (page == 0u) {
            const CGFloat gridLeft = plot.origin.x + 54.0;
            const CGFloat gridTop = plot.origin.y + 36.0;
            const CGFloat spacing = 58.0;
            const int source = static_cast<int>(std::lround(
                (point.x - gridLeft) / spacing));
            const int destination = static_cast<int>(std::lround(
                (point.y - gridTop) / spacing));
            if (source >= 0 && source < static_cast<int>(kChannelCount)
                && destination >= 0
                && destination < static_cast<int>(kChannelCount)
                && std::abs(point.x - (gridLeft + spacing * source)) < 14.0
                && std::abs(point.y - (gridTop + spacing * destination)) < 14.0) {
                plugin->selectedSource.store(source,
                    std::memory_order_relaxed);
                plugin->selectedDestination.store(destination,
                    std::memory_order_relaxed);
                plugin->selectedLane.store(destination,
                    std::memory_order_relaxed);
                if ([event clickCount] >= 2) {
                    ParamRange range;
                    const clap_id id = matrixParamId(destination, source);
                    if (paramRange(id, range)) applyParam(*plugin, id,
                        range.defaultValue);
                }
                [self setNeedsDisplay:YES];
                return;
            }
        } else if (page == 1u) {
            const CGFloat gap = 12.0;
            const CGFloat cellWidth = (plot.size.width - gap * 5.0) / 4.0;
            const CGFloat cellHeight = (plot.size.height - gap * 3.0) / 2.0;
            for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
                const uint32_t column = lane % 4u;
                const uint32_t row = lane / 4u;
                NSRect cell = NSMakeRect(plot.origin.x + gap
                        + column * (cellWidth + gap),
                    plot.origin.y + gap + row * (cellHeight + gap),
                    cellWidth, cellHeight);
                if (NSPointInRect(point, cell)) {
                    plugin->selectedLane.store(lane,
                        std::memory_order_relaxed);
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
        } else {
            const CGFloat left = plot.origin.x + 18.0;
            const CGFloat top = plot.origin.y + 28.0;
            const CGFloat rowHeight = 76.0;
            const CGFloat slotWidth = 142.0;
            for (uint32_t lane = 0u; lane < kChannelCount; ++lane) {
                for (uint32_t slot = 0u;
                     slot < s3g::kNoInputMixerInsertSlots; ++slot) {
                    NSRect cell = NSMakeRect(left + 42.0
                            + slot * (slotWidth + 12.0),
                        top + lane * rowHeight, slotWidth, 50.0);
                    if (NSPointInRect(point, cell)) {
                        plugin->selectedLane.store(lane,
                            std::memory_order_relaxed);
                        plugin->selectedSlot.store(slot,
                            std::memory_order_relaxed);
                        [self setNeedsDisplay:YES];
                        return;
                    }
                }
            }
        }
    }

    const auto openMenuIfHit = [&](int menu) {
        if (NSPointInRect(point, [self menuAnchorRect:menu])) {
            _openMenu = menu;
            [self setNeedsDisplay:YES];
            return true;
        }
        return false;
    };
    if (openMenuIfHit(kMenuLane) || openMenuIfHit(kMenuSource)
        || openMenuIfHit(kMenuDestination) || openMenuIfHit(kMenuSlot0)
        || openMenuIfHit(kMenuSlot1) || openMenuIfHit(kMenuSlot2)
        || openMenuIfHit(kMenuQuality)) return;

    if (NSPointInRect(point, processorMenuRect(family.network, 0u))) {
        plugin->seedRequested.store(true, std::memory_order_release);
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            family.panicButton))) {
        plugin->panicRequested.store(true, std::memory_order_release);
        return;
    }

    const uint32_t lane = plugin->selectedLane.load(
        std::memory_order_relaxed);
    const uint32_t slot = plugin->selectedSlot.load(
        std::memory_order_relaxed);
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.output, 2u)))) {
        applyParam(*plugin, kLimiterParamId,
            plugin->params.limiterEnabled == 0u ? 1.0 : 0.0);
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.output, 3u)))) {
        applyParam(*plugin, kDcBlockParamId,
            plugin->params.dcBlockEnabled == 0u ? 1.0 : 0.0);
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.selectedLane, 4u)))) {
        const CGFloat split = s3g::gui_layout::processorControlX(
            family.selectedLane.frame.x) + 83.0;
        if (point.x < split) {
            applyParam(*plugin, laneParamId(lane, kLaneMuteOffset),
                plugin->params.lanes[lane].mute == 0u ? 1.0 : 0.0);
        } else {
            plugin->killMask.fetch_or(1u << lane,
                std::memory_order_release);
        }
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.crosspoint, 3u)))) {
        const uint32_t source = plugin->selectedSource.load(
            std::memory_order_relaxed);
        const uint32_t destination = plugin->selectedDestination.load(
            std::memory_order_relaxed);
        const clap_id id = matrixParamId(destination, source);
        double route = 0.0;
        paramValue(*plugin, id, route);
        applyParam(*plugin, id, std::abs(route) < 0.001
            ? -0.50 : -route);
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.crosspoint, 4u)))) {
        applyParam(*plugin, matrixParamId(
            plugin->selectedDestination.load(std::memory_order_relaxed),
            plugin->selectedSource.load(std::memory_order_relaxed)), 0.0);
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.inserts, 7u)))) {
        applyParam(*plugin, insertParamId(lane, slot, kInsertBypassOffset),
            plugin->params.lanes[lane].inserts[slot].bypass == 0u
                ? 1.0 : 0.0);
        return;
    }

    const clap_id outputIds[2] = { kOutputParamId, kCeilingParamId };
    for (uint32_t row = 0u; row < 2u; ++row) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.output, row)))) {
            [self beginSlider:outputIds[row] event:event point:point];
            return;
        }
    }
    const clap_id networkIds[5] = {
        kFeedbackParamId, kCouplingParamId, kPhaseParamId,
        kDriftParamId, kFormantParamId,
    };
    for (uint32_t row = 1u; row < 6u; ++row) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.network, row)))) {
            [self beginSlider:networkIds[row - 1u] event:event point:point];
            return;
        }
    }
    const clap_id selectedIds[3] = {
        laneParamId(lane, kLaneBodyOffset),
        laneParamId(lane, kLaneLossOffset),
        laneParamId(lane, kLaneLevelOffset),
    };
    for (uint32_t row = 1u; row < 4u; ++row) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.selectedLane, row)))) {
            [self beginSlider:selectedIds[row - 1u] event:event point:point];
            return;
        }
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(family.crosspoint, 2u)))) {
        [self beginSlider:matrixParamId(
            plugin->selectedDestination.load(std::memory_order_relaxed),
            plugin->selectedSource.load(std::memory_order_relaxed))
            event:event point:point];
        return;
    }
    const clap_id eqIds[4] = {
        laneParamId(lane, kLaneLowOffset),
        laneParamId(lane, kLaneMidFrequencyOffset),
        laneParamId(lane, kLaneMidGainOffset),
        laneParamId(lane, kLaneHighOffset),
    };
    for (uint32_t row = 0u; row < 4u; ++row) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.eq, row)))) {
            [self beginSlider:eqIds[row] event:event point:point];
            return;
        }
    }
    const clap_id insertIds[4] = {
        insertParamId(lane, slot, kInsertGainOffset),
        insertParamId(lane, slot, kInsertToneOffset),
        insertParamId(lane, slot, kInsertBiasOffset),
        insertParamId(lane, slot, kInsertLevelOffset),
    };
    for (uint32_t row = 3u; row < 7u; ++row) {
        if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.inserts, row)))) {
            [self beginSlider:insertIds[row - 3u] event:event point:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam != CLAP_INVALID_ID) {
        [self updateSlider:[self convertPoint:[event locationInWindow]
            fromView:nil]];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api,
    bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api,
    bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GNoInputMixerView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight,
            kGuiWidth, 360u)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible = false;
        [static_cast<S3GNoInputMixerView*>(p->guiView) stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport,
            p->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height,
        kGuiWidth, 360u);
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
        kGuiWidth, 360u);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width,
    uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
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
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GNoInputMixerView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GNoInputMixerView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

} // namespace

#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.no-input-mixer-8ch",
    "s3g Processor No Input Mixer 8ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Eight-channel zero-input feedback ecology with signed routing, per-lane EQ, and nonlinear inserts.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
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
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
