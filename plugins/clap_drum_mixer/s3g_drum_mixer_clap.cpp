#include "s3g_drum_mixer.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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
#include <strings.h>
#include <new>

namespace {

constexpr uint32_t kStateMagic = 0x5333444du; // S3DM
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kGuiWidth = 1320u;
constexpr uint32_t kGuiHeight = 600u;
constexpr uint32_t kGlobalParamCount = 9u;
constexpr uint32_t kLaneParamCount = 9u;
constexpr uint32_t kParamCount = kGlobalParamCount
    + s3g::kDrumMixerLaneCount * kLaneParamCount;
constexpr clap_id kLaneParamBase = 100u;
constexpr clap_id kLaneParamStride = 16u;

enum GlobalParamId : clap_id {
    kOutputModeParamId = 1u,
    kMasterLevelParamId,
    kBusEnabledParamId,
    kBusDriveParamId,
    kBusGlueParamId,
    kBusRoomParamId,
    kBusWeightParamId,
    kBusToneParamId,
    kBusReturnParamId,
};

enum LaneParamOffset : uint32_t {
    kLaneLevelOffset = 0u,
    kLanePanOffset,
    kLaneLowOffset,
    kLaneMidOffset,
    kLaneHighOffset,
    kLaneAuxOffset,
    kLaneMuteOffset,
    kLaneSoloOffset,
    kLaneMidFrequencyOffset,
};

enum class Unit : uint8_t {
    Db, Pan, Hz, Percent, SignedPercent, Bool, Mode,
};

struct ParamSpec {
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    Unit unit = Unit::Percent;
    bool stepped = false;
};

struct LegacyLaneParamsV1 {
    float levelDb = 0.0f;
    float pan = 0.0f;
    float lowEqDb = 0.0f;
    float midEqDb = 0.0f;
    float highEqDb = 0.0f;
    float auxSend = 0.0f;
    bool mute = false;
    bool solo = false;
};

struct LegacyParamsV1 {
    std::array<LegacyLaneParamsV1, s3g::kDrumMixerLaneCount> lanes {};
    s3g::DrumMixerOutputMode outputMode = s3g::DrumMixerOutputMode::Sum;
    float masterLevelDb = -6.0f;
    bool busEnabled = false;
    float busDrive = 0.38f;
    float busGlue = 0.34f;
    float busRoom = 0.0f;
    float busWeight = 0.68f;
    float busTone = 0.0f;
    float busReturnDb = -9.0f;
};

struct StateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
};

clap_id laneParamId(uint32_t lane, LaneParamOffset offset)
{
    return kLaneParamBase + lane * kLaneParamStride
        + static_cast<clap_id>(offset);
}

bool decodeLaneParam(clap_id id, uint32_t& lane, LaneParamOffset& offset)
{
    if (id < kLaneParamBase) return false;
    const uint32_t relative = id - kLaneParamBase;
    lane = relative / kLaneParamStride;
    const uint32_t rawOffset = relative % kLaneParamStride;
    if (lane >= s3g::kDrumMixerLaneCount
        || rawOffset >= kLaneParamCount) return false;
    offset = static_cast<LaneParamOffset>(rawOffset);
    return true;
}

int32_t paramIndex(clap_id id)
{
    if (id >= kOutputModeParamId && id <= kBusReturnParamId) {
        return static_cast<int32_t>(id - kOutputModeParamId);
    }
    uint32_t lane = 0u;
    LaneParamOffset offset {};
    if (!decodeLaneParam(id, lane, offset)) return -1;
    return static_cast<int32_t>(kGlobalParamCount
        + lane * kLaneParamCount + static_cast<uint32_t>(offset));
}

clap_id paramIdForIndex(uint32_t index)
{
    if (index < kGlobalParamCount) return kOutputModeParamId + index;
    index -= kGlobalParamCount;
    return laneParamId(index / kLaneParamCount,
        static_cast<LaneParamOffset>(index % kLaneParamCount));
}

bool paramSpec(clap_id id, ParamSpec& spec)
{
    switch (id) {
    case kOutputModeParamId: spec = { 0.0, 1.0, 0.0, Unit::Mode, true }; return true;
    case kMasterLevelParamId: spec = { -60.0, 12.0, -6.0, Unit::Db, false }; return true;
    case kBusEnabledParamId: spec = { 0.0, 1.0, 0.0, Unit::Bool, true }; return true;
    case kBusDriveParamId: spec = { 0.0, 1.0, 0.38, Unit::Percent, false }; return true;
    case kBusGlueParamId: spec = { 0.0, 1.0, 0.34, Unit::Percent, false }; return true;
    case kBusRoomParamId: spec = { -1.0, 1.0, 0.0, Unit::SignedPercent, false }; return true;
    case kBusWeightParamId: spec = { 0.0, 1.0, 0.68, Unit::Percent, false }; return true;
    case kBusToneParamId: spec = { -1.0, 1.0, 0.0, Unit::SignedPercent, false }; return true;
    case kBusReturnParamId: spec = { -60.0, 12.0, -9.0, Unit::Db, false }; return true;
    default: break;
    }
    uint32_t lane = 0u;
    LaneParamOffset offset {};
    if (!decodeLaneParam(id, lane, offset)) return false;
    (void)lane;
    switch (offset) {
    case kLaneLevelOffset: spec = { -60.0, 12.0, 0.0, Unit::Db, false }; break;
    case kLanePanOffset: spec = { -1.0, 1.0, 0.0, Unit::Pan, false }; break;
    case kLaneLowOffset:
    case kLaneMidOffset:
    case kLaneHighOffset: spec = { -12.0, 12.0, 0.0, Unit::Db, false }; break;
    case kLaneMidFrequencyOffset:
        spec = { 120.0, 8000.0, 900.0, Unit::Hz, false };
        break;
    case kLaneAuxOffset: spec = { 0.0, 1.0, 0.0, Unit::Percent, false }; break;
    case kLaneMuteOffset:
    case kLaneSoloOffset: spec = { 0.0, 1.0, 0.0, Unit::Bool, true }; break;
    }
    return true;
}

double clampParamValue(clap_id id, double value);

double normalizedParamValue(clap_id id, double value)
{
    ParamSpec spec;
    if (!paramSpec(id, spec)) return 0.0;
    value = clampParamValue(id, value);
    uint32_t lane = 0u;
    LaneParamOffset offset {};
    if (decodeLaneParam(id, lane, offset)
        && offset == kLaneMidFrequencyOffset) {
        return std::log(value / spec.minimum)
            / std::log(spec.maximum / spec.minimum);
    }
    return (value - spec.minimum) / (spec.maximum - spec.minimum);
}

double paramValueFromNormalized(clap_id id, double normalized)
{
    ParamSpec spec;
    if (!paramSpec(id, spec)) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    uint32_t lane = 0u;
    LaneParamOffset offset {};
    if (decodeLaneParam(id, lane, offset)
        && offset == kLaneMidFrequencyOffset) {
        return spec.minimum * std::pow(
            spec.maximum / spec.minimum, normalized);
    }
    return spec.minimum + normalized * (spec.maximum - spec.minimum);
}

double clampParamValue(clap_id id, double value)
{
    ParamSpec spec;
    if (!paramSpec(id, spec)) return 0.0;
    value = std::isfinite(value) ? value : spec.defaultValue;
    value = std::clamp(value, spec.minimum, spec.maximum);
    return spec.stepped ? std::round(value) : value;
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::DrumMixerParams params {};
    s3g::DrumMixer dsp {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    std::array<std::atomic<float>, s3g::kDrumMixerLaneCount> lanePeaks {};
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<float> busActivity { 0.0f };
    std::atomic<float> busReductionDb { 0.0f };
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

double valueFromParams(const s3g::DrumMixerParams& params, clap_id id)
{
    switch (id) {
    case kOutputModeParamId: return static_cast<uint32_t>(params.outputMode);
    case kMasterLevelParamId: return params.masterLevelDb;
    case kBusEnabledParamId: return params.busEnabled ? 1.0 : 0.0;
    case kBusDriveParamId: return params.busDrive;
    case kBusGlueParamId: return params.busGlue;
    case kBusRoomParamId: return params.busRoom;
    case kBusWeightParamId: return params.busWeight;
    case kBusToneParamId: return params.busTone;
    case kBusReturnParamId: return params.busReturnDb;
    default: break;
    }
    uint32_t lane = 0u;
    LaneParamOffset offset {};
    if (!decodeLaneParam(id, lane, offset)) return 0.0;
    const auto& strip = params.lanes[lane];
    switch (offset) {
    case kLaneLevelOffset: return strip.levelDb;
    case kLanePanOffset: return strip.pan;
    case kLaneLowOffset: return strip.lowEqDb;
    case kLaneMidOffset: return strip.midEqDb;
    case kLaneHighOffset: return strip.highEqDb;
    case kLaneAuxOffset: return strip.auxSend;
    case kLaneMuteOffset: return strip.mute ? 1.0 : 0.0;
    case kLaneSoloOffset: return strip.solo ? 1.0 : 0.0;
    case kLaneMidFrequencyOffset: return strip.midFrequencyHz;
    }
    return 0.0;
}

void publishParam(Plugin& plugin, clap_id id, double value)
{
    const int32_t index = paramIndex(id);
    if (index < 0) return;
    plugin.publishedParams[static_cast<uint32_t>(index)].store(
        value, std::memory_order_release);
}

void publishAll(Plugin& plugin)
{
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        const clap_id id = paramIdForIndex(index);
        publishParam(plugin, id, valueFromParams(plugin.params, id));
    }
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    value = clampParamValue(id, value);
    switch (id) {
    case kOutputModeParamId:
        plugin.params.outputMode = value >= 0.5
            ? s3g::DrumMixerOutputMode::Direct
            : s3g::DrumMixerOutputMode::Sum;
        break;
    case kMasterLevelParamId: plugin.params.masterLevelDb = static_cast<float>(value); break;
    case kBusEnabledParamId: plugin.params.busEnabled = value >= 0.5; break;
    case kBusDriveParamId: plugin.params.busDrive = static_cast<float>(value); break;
    case kBusGlueParamId: plugin.params.busGlue = static_cast<float>(value); break;
    case kBusRoomParamId: plugin.params.busRoom = static_cast<float>(value); break;
    case kBusWeightParamId: plugin.params.busWeight = static_cast<float>(value); break;
    case kBusToneParamId: plugin.params.busTone = static_cast<float>(value); break;
    case kBusReturnParamId: plugin.params.busReturnDb = static_cast<float>(value); break;
    default: {
        uint32_t lane = 0u;
        LaneParamOffset offset {};
        if (!decodeLaneParam(id, lane, offset)) return;
        auto& strip = plugin.params.lanes[lane];
        switch (offset) {
        case kLaneLevelOffset: strip.levelDb = static_cast<float>(value); break;
        case kLanePanOffset: strip.pan = static_cast<float>(value); break;
        case kLaneLowOffset: strip.lowEqDb = static_cast<float>(value); break;
        case kLaneMidOffset: strip.midEqDb = static_cast<float>(value); break;
        case kLaneHighOffset: strip.highEqDb = static_cast<float>(value); break;
        case kLaneAuxOffset: strip.auxSend = static_cast<float>(value); break;
        case kLaneMuteOffset: strip.mute = value >= 0.5; break;
        case kLaneSoloOffset: strip.solo = value >= 0.5; break;
        case kLaneMidFrequencyOffset:
            strip.midFrequencyHz = static_cast<float>(value);
            break;
        }
        break;
    }
    }
    plugin.dsp.setParams(plugin.params);
    plugin.params = plugin.dsp.params();
    publishParam(plugin, id, valueFromParams(plugin.params, id));
}

bool init(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->host && instance->host->get_extension) {
        instance->hostParams = static_cast<const clap_host_params_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_PARAMS));
    }
    instance->dsp.setParams(instance->params);
    instance->params = instance->dsp.params();
    publishAll(*instance);
    return true;
}

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
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    instance->dsp.setParams(instance->params);
    instance->dsp.prepare(sampleRate);
    for (auto& peak : instance->lanePeaks) {
        peak.store(0.0f, std::memory_order_relaxed);
    }
    instance->masterPeak.store(0.0f, std::memory_order_relaxed);
    instance->busActivity.store(0.0f, std::memory_order_relaxed);
    instance->busReductionDb.store(0.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->dsp.reset();
    for (auto& peak : instance->lanePeaks) {
        peak.store(0.0f, std::memory_order_relaxed);
    }
    instance->masterPeak.store(0.0f, std::memory_order_relaxed);
    instance->busActivity.store(0.0f, std::memory_order_relaxed);
    instance->busReductionDb.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        applyParam(plugin, event->param_id, event->value);
    }
}

bool pushGuiEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
    if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
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
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = pending.kind
            == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return output->try_push(output, &event.header);
}

void consumeGuiEvents(Plugin& plugin, const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending;
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(plugin, pending.paramId, pending.value);
        }
        plugin.guiParamEvents.pop();
    }
}

float readSample(const clap_audio_buffer_t& input,
    uint32_t channel, uint32_t frame)
{
    if (channel >= input.channel_count) return 0.0f;
    if (input.data32 && input.data32[channel]) {
        return input.data32[channel][frame];
    }
    if (input.data64 && input.data64[channel]) {
        return static_cast<float>(input.data64[channel][frame]);
    }
    return 0.0f;
}

void writeSample(const clap_audio_buffer_t& output,
    uint32_t channel, uint32_t frame, float value)
{
    if (channel >= output.channel_count) return;
    if (output.data32 && output.data32[channel]) {
        output.data32[channel][frame] = value;
    }
    if (output.data64 && output.data64[channel]) {
        output.data64[channel][frame] = static_cast<double>(value);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* context)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, context->in_events);
    consumeGuiEvents(*instance, context->out_events);
    if (context->audio_inputs_count == 0u
        || context->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = context->audio_inputs[0u];
    const auto& output = context->audio_outputs[0u];
    std::array<float, s3g::kDrumMixerLaneCount> blockLanePeaks {};
    float blockMasterPeak = 0.0f;
    std::array<float, s3g::kDrumMixerChannelCount> inputFrame {};
    std::array<float, s3g::kDrumMixerChannelCount> outputFrame {};
    for (uint32_t frame = 0u; frame < context->frames_count; ++frame) {
        for (uint32_t channel = 0u;
             channel < s3g::kDrumMixerChannelCount; ++channel) {
            inputFrame[channel] = readSample(input, channel, frame);
        }
        instance->dsp.processFrame(inputFrame, outputFrame);
        for (uint32_t channel = 0u;
             channel < s3g::kDrumMixerChannelCount; ++channel) {
            writeSample(output, channel, frame, outputFrame[channel]);
            blockMasterPeak = std::max(
                blockMasterPeak, std::abs(outputFrame[channel]));
        }
        for (uint32_t lane = 0u;
             lane < s3g::kDrumMixerLaneCount; ++lane) {
            const auto laneOutput = instance->dsp.laneOutput(lane);
            blockLanePeaks[lane] = std::max(blockLanePeaks[lane],
                std::max(std::abs(laneOutput[0u]),
                    std::abs(laneOutput[1u])));
        }
    }
    s3g::clearAudioBufferFromChannel(
        output, s3g::kDrumMixerChannelCount, context->frames_count);
    for (uint32_t lane = 0u;
         lane < s3g::kDrumMixerLaneCount; ++lane) {
        const float previous = instance->lanePeaks[lane].load(
            std::memory_order_relaxed);
        instance->lanePeaks[lane].store(
            std::max(blockLanePeaks[lane], previous * 0.88f),
            std::memory_order_relaxed);
    }
    instance->masterPeak.store(std::max(blockMasterPeak,
        instance->masterPeak.load(std::memory_order_relaxed) * 0.90f),
        std::memory_order_relaxed);
    instance->busActivity.store(instance->dsp.busActivity(),
        std::memory_order_relaxed);
    instance->busReductionDb.store(instance->dsp.busGainReductionDb(),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "8 Stereo Lane Inputs" : "Stereo Sum / 8 Stereo Direct Outs");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = s3g::kDrumMixerChannelCount;
    info->port_type = nullptr;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const clap_id id = paramIdForIndex(index);
    ParamSpec spec;
    if (!paramSpec(id, spec)) return false;
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (spec.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    if (index < kGlobalParamCount) {
        static constexpr const char* names[kGlobalParamCount] {
            "Output Mode", "Master Level", "Bus Enabled", "Bus Drive",
            "Bus Glue", "Bus Room", "Bus Weight", "Bus Tone", "Bus Return",
        };
        std::snprintf(info->name, sizeof(info->name), "%s", names[index]);
        std::snprintf(info->module, sizeof(info->module), "%s",
            index < 2u ? "Master" : "Drum Bus");
    } else {
        uint32_t lane = 0u;
        LaneParamOffset offset {};
        decodeLaneParam(id, lane, offset);
        static constexpr const char* names[kLaneParamCount] {
            "Level", "Pan", "Low EQ", "Mid EQ", "High EQ", "Aux Send",
            "Mute", "Solo", "Mid Frequency",
        };
        std::snprintf(info->name, sizeof(info->name), "Lane %u %s",
            lane + 1u, names[static_cast<uint32_t>(offset)]);
        std::snprintf(info->module, sizeof(info->module), "Lane %u (In %u/%u)",
            lane + 1u, lane * 2u + 1u, lane * 2u + 2u);
    }
    info->min_value = spec.minimum;
    info->max_value = spec.maximum;
    info->default_value = spec.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const int32_t index = paramIndex(id);
    if (index < 0) return false;
    *value = self(plugin)->publishedParams[static_cast<uint32_t>(index)].load(
        std::memory_order_acquire);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    ParamSpec spec;
    if (!paramSpec(id, spec)) return false;
    value = clampParamValue(id, value);
    switch (spec.unit) {
    case Unit::Db:
        if (value <= -59.95) std::snprintf(display, size, "-INF");
        else std::snprintf(display, size, "%+.1f dB", value);
        break;
    case Unit::Pan:
        if (std::abs(value) < 0.005) std::snprintf(display, size, "C");
        else std::snprintf(display, size, "%c%.0f",
            value < 0.0 ? 'L' : 'R', std::abs(value) * 100.0);
        break;
    case Unit::Hz:
        if (value >= 1000.0) {
            std::snprintf(display, size, "%.2f kHz", value * 0.001);
        } else {
            std::snprintf(display, size, "%.0f Hz", value);
        }
        break;
    case Unit::Percent: std::snprintf(display, size, "%.0f%%", value * 100.0); break;
    case Unit::SignedPercent: std::snprintf(display, size, "%+.0f%%", value * 100.0); break;
    case Unit::Bool: std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF"); break;
    case Unit::Mode: std::snprintf(display, size, "%s",
        value >= 0.5 ? "DIRECT" : "SUM"); break;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    ParamSpec spec;
    if (!paramSpec(id, spec)) return false;
    if (spec.unit == Unit::Mode) {
        if (strcasecmp(display, "SUM") == 0) { *value = 0.0; return true; }
        if (strcasecmp(display, "DIRECT") == 0) { *value = 1.0; return true; }
    }
    if (spec.unit == Unit::Bool) {
        if (strcasecmp(display, "ON") == 0) { *value = 1.0; return true; }
        if (strcasecmp(display, "OFF") == 0) { *value = 0.0; return true; }
    }
    if (spec.unit == Unit::Db && strcasecmp(display, "-INF") == 0) {
        *value = spec.minimum;
        return true;
    }
    if (spec.unit == Unit::Pan) {
        if (strcasecmp(display, "C") == 0) {
            *value = 0.0;
            return true;
        }
        if (display[0] == 'L' || display[0] == 'l'
            || display[0] == 'R' || display[0] == 'r') {
            char* end = nullptr;
            const double magnitude = std::strtod(display + 1, &end);
            if (end == display + 1) return false;
            const double direction = display[0] == 'L' || display[0] == 'l'
                ? -1.0 : 1.0;
            *value = clampParamValue(id,
                direction * std::abs(magnitude) * 0.01);
            return true;
        }
    }
    char* end = nullptr;
    *value = std::strtod(display, &end);
    if (end == display) return false;
    if (spec.unit == Unit::Hz
        && (std::strchr(display, 'k') || std::strchr(display, 'K'))) {
        *value *= 1000.0;
    }
    if (std::strchr(display, '%')) *value *= 0.01;
    *value = clampParamValue(id, *value);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, input);
    consumeGuiEvents(*instance, output);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const StateHeader header;
    const auto& params = self(plugin)->params;
    return s3g::clap_state::writeAll(stream, &header, sizeof(header))
        && s3g::clap_state::writeAll(stream, &params, sizeof(params));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    StateHeader header;
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic) return false;
    s3g::DrumMixerParams restored;
    if (header.version == kStateVersion) {
        if (!s3g::clap_state::readAll(
                stream, &restored, sizeof(restored))) return false;
    } else if (header.version == 1u) {
        LegacyParamsV1 legacy;
        if (!s3g::clap_state::readAll(
                stream, &legacy, sizeof(legacy))) return false;
        restored.outputMode = legacy.outputMode;
        restored.masterLevelDb = legacy.masterLevelDb;
        restored.busEnabled = legacy.busEnabled;
        restored.busDrive = legacy.busDrive;
        restored.busGlue = legacy.busGlue;
        restored.busRoom = legacy.busRoom;
        restored.busWeight = legacy.busWeight;
        restored.busTone = legacy.busTone;
        restored.busReturnDb = legacy.busReturnDb;
        for (uint32_t lane = 0u;
             lane < s3g::kDrumMixerLaneCount; ++lane) {
            const auto& source = legacy.lanes[lane];
            auto& destination = restored.lanes[lane];
            destination.levelDb = source.levelDb;
            destination.pan = source.pan;
            destination.lowEqDb = source.lowEqDb;
            destination.midEqDb = source.midEqDb;
            destination.highEqDb = source.highEqDb;
            destination.auxSend = source.auxSend;
            destination.mute = source.mute;
            destination.solo = source.solo;
        }
    } else {
        return false;
    }
    auto* instance = self(plugin);
    instance->dsp.setParams(restored);
    instance->params = instance->dsp.params();
    instance->dsp.reset();
    publishAll(*instance);
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!plugin.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiValue(Plugin& plugin, clap_id id, double value)
{
    value = clampParamValue(id, value);
    if (!plugin.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) return false;
    publishParam(plugin, id, value);
    requestGuiParamService(plugin);
    return true;
}

bool queueGuiGesture(Plugin& plugin, clap_id id, double value)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    value = clampParamValue(id, value);
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, value },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    publishParam(plugin, id, value);
    requestGuiParamService(plugin);
    return true;
}

} // namespace

#if defined(__APPLE__)
namespace {

constexpr CGFloat kLanePanelX = 18.0;
constexpr CGFloat kLanePanelY = static_cast<CGFloat>(
    s3g::gui_layout::kStandardMetrics.contentTop);
constexpr CGFloat kLanePanelWidth = 128.0;
constexpr CGFloat kLanePanelHeight = 546.0;
constexpr CGFloat kLaneGap = 6.0;
constexpr CGFloat kMasterPanelX = 1096.0;
constexpr CGFloat kMasterPanelWidth = 206.0;
constexpr CGFloat kOutputPanelHeight = 134.0;
constexpr CGFloat kBusPanelY = 188.0;
constexpr CGFloat kBusPanelHeight = 248.0;

static_assert(kLanePanelY
    == s3g::gui_layout::kStandardMetrics.contentTop);
static_assert(kLanePanelX + 8.0 * kLanePanelWidth
    + 7.0 * kLaneGap < kMasterPanelX);
static_assert(kMasterPanelX + kMasterPanelWidth
    <= static_cast<CGFloat>(kGuiWidth) - 18.0);

NSRect lanePanelRect(uint32_t lane)
{
    return NSMakeRect(kLanePanelX + lane * (kLanePanelWidth + kLaneGap),
        kLanePanelY, kLanePanelWidth, kLanePanelHeight);
}

bool isLaneDialOffset(LaneParamOffset offset)
{
    return offset == kLanePanOffset || offset == kLaneLowOffset
        || offset == kLaneMidOffset || offset == kLaneMidFrequencyOffset
        || offset == kLaneHighOffset;
}

uint32_t laneDialIndex(LaneParamOffset offset)
{
    switch (offset) {
    case kLanePanOffset: return 0u;
    case kLaneLowOffset: return 1u;
    case kLaneMidOffset: return 2u;
    case kLaneMidFrequencyOffset: return 3u;
    case kLaneHighOffset: return 4u;
    default: return 0u;
    }
}

NSRect laneDialRect(uint32_t lane, LaneParamOffset offset)
{
    const NSRect panel = lanePanelRect(lane);
    const uint32_t index = laneDialIndex(offset);
    return NSMakeRect(panel.origin.x + 12.0 + (index % 2u) * 56.0,
        69.0 + (index / 2u) * 62.0, 48.0, 58.0);
}

CGFloat laneAuxY(uint32_t lane)
{
    (void)lane;
    return 266.0;
}

NSRect laneAuxHitRect(uint32_t lane)
{
    const NSRect panel = lanePanelRect(lane);
    return s3g::clap_gui::mixerStripSliderHitRect(
        panel.origin.x, panel.size.width, laneAuxY(lane));
}

NSRect laneAuxTrackRect(uint32_t lane)
{
    const NSRect panel = lanePanelRect(lane);
    return s3g::clap_gui::mixerStripSliderTrackRect(
        panel.origin.x, panel.size.width, laneAuxY(lane));
}

NSRect laneMuteRect(uint32_t lane)
{
    const NSRect panel = lanePanelRect(lane);
    return NSMakeRect(panel.origin.x + 16.0, 294.0, 44.0, 24.0);
}

NSRect laneSoloRect(uint32_t lane)
{
    const NSRect panel = lanePanelRect(lane);
    return NSMakeRect(panel.origin.x + 68.0, 294.0, 44.0, 24.0);
}

NSRect laneFaderRect(uint32_t lane)
{
    const NSRect panel = lanePanelRect(lane);
    return NSMakeRect(panel.origin.x + 18.0, 354.0, 54.0, 168.0);
}

NSRect outputPanelRect()
{
    return NSMakeRect(kMasterPanelX, kLanePanelY,
        kMasterPanelWidth, kOutputPanelHeight);
}

NSRect busPanelRect()
{
    return NSMakeRect(kMasterPanelX, kBusPanelY,
        kMasterPanelWidth, kBusPanelHeight);
}

CGFloat outputSliderY()
{
    return kLanePanelY + static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.firstRowOffset);
}

NSRect outputSliderHitRect()
{
    return s3g::clap_gui::mixerStripSliderHitRect(
        kMasterPanelX, kMasterPanelWidth, outputSliderY());
}

NSRect outputSliderTrackRect()
{
    return s3g::clap_gui::mixerStripSliderTrackRect(
        kMasterPanelX, kMasterPanelWidth, outputSliderY());
}

NSRect sumButtonRect()
{
    return NSMakeRect(kMasterPanelX + 68.0, 104.0, 55.0, 24.0);
}

NSRect directButtonRect()
{
    return NSMakeRect(kMasterPanelX + 131.0, 104.0, 59.0, 24.0);
}

NSRect busButtonRect()
{
    return NSMakeRect(kMasterPanelX + 16.0, kBusPanelY + 28.0,
        kMasterPanelWidth - 32.0, 22.0);
}

CGFloat busSliderY(clap_id id)
{
    const uint32_t row = static_cast<uint32_t>(id - kBusDriveParamId) + 1u;
    return kBusPanelY + static_cast<CGFloat>(
        s3g::gui_layout::kStandardMetrics.firstRowOffset
        + row * s3g::gui_layout::kStandardMetrics.rowPitch);
}

NSRect busSliderHitRect(clap_id id)
{
    return s3g::clap_gui::mixerStripSliderHitRect(
        kMasterPanelX, kMasterPanelWidth, busSliderY(id));
}

NSRect busSliderTrackRect(clap_id id)
{
    return s3g::clap_gui::mixerStripSliderTrackRect(
        kMasterPanelX, kMasterPanelWidth, busSliderY(id));
}

} // namespace

@interface S3GDrumMixerView : NSView {
    void* _plugin;
    int _dragParam;
    BOOL _dragIsDial;
    NSPoint _dragStartPoint;
    double _dragStartValue;
    char _presetName[64];
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateDraggedParam:(NSPoint)point;
@end

@implementation S3GDrumMixerView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        _dragIsDial = NO;
        _dragStartPoint = NSZeroPoint;
        _dragStartValue = 0.0;
        _timer = nil;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
        target:self selector:@selector(refresh:)
        userInfo:nil repeats:YES];
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

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    double outputMode = 0.0;
    paramsGetValue(&plugin->plugin, kOutputModeParamId, &outputMode);
    NSString* peakText = s3g::clap_gui::peakDbText(
        plugin->masterPeak.load(std::memory_order_relaxed));
    s3g::clap_gui::drawMixerTitleBand(
        @"s3g DRUM MIXER 16",
        [NSString stringWithUTF8String:_presetName],
        [NSString stringWithFormat:@"%@  //  %@",
            outputMode >= 0.5 ? @"DIRECT 8x2" : @"SUM 16>2", peakText],
        titleBand, style);

    static constexpr const char* dialLabels[] {
        "PAN", "LOW", "MID", "FREQ", "HIGH",
    };
    static constexpr LaneParamOffset dialOffsets[] {
        kLanePanOffset, kLaneLowOffset, kLaneMidOffset,
        kLaneMidFrequencyOffset, kLaneHighOffset,
    };
    for (uint32_t lane = 0u; lane < s3g::kDrumMixerLaneCount; ++lane) {
        const NSRect panel = lanePanelRect(lane);
        s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y,
            panel.size.width, panel.size.height, style);
        s3g::clap_gui::drawPanelHeader(
            [NSString stringWithFormat:@"%02u  IN %u/%u", lane + 1u,
                lane * 2u + 1u, lane * 2u + 2u], true,
            panel.origin.x, panel.origin.y, panel.size.width,
            static_cast<CGFloat>(
                s3g::gui_layout::kStandardMetrics.headerHeight),
            labels, style);
        for (uint32_t dial = 0u; dial < 5u; ++dial) {
            const clap_id id = laneParamId(lane, dialOffsets[dial]);
            double value = 0.0;
            paramsGetValue(&plugin->plugin, id, &value);
            char text[32] {};
            paramsValueToText(&plugin->plugin, id, value, text, sizeof(text));
            s3g::clap_gui::drawDial(
                [NSString stringWithUTF8String:dialLabels[dial]],
                [NSString stringWithUTF8String:text],
                normalizedParamValue(id, value),
                laneDialRect(lane, dialOffsets[dial]),
                labels, values, style);
        }
        const clap_id auxId = laneParamId(lane, kLaneAuxOffset);
        double aux = 0.0;
        paramsGetValue(&plugin->plugin, auxId, &aux);
        char auxText[32] {};
        paramsValueToText(&plugin->plugin, auxId, aux,
            auxText, sizeof(auxText));
        s3g::clap_gui::drawMixerStripSlider(@"AUX",
            [NSString stringWithUTF8String:auxText],
            normalizedParamValue(auxId, aux), laneAuxY(lane),
            panel.origin.x, panel.size.width, labels, values, style);
        double mute = 0.0;
        double solo = 0.0;
        paramsGetValue(&plugin->plugin,
            laneParamId(lane, kLaneMuteOffset), &mute);
        paramsGetValue(&plugin->plugin,
            laneParamId(lane, kLaneSoloOffset), &solo);
        s3g::clap_gui::drawHeaderButton(laneMuteRect(lane), panel, @"MUTE",
            mute >= 0.5, values, style);
        s3g::clap_gui::drawHeaderButton(laneSoloRect(lane), panel, @"SOLO",
            solo >= 0.5, values, style);

        const clap_id levelId = laneParamId(lane, kLaneLevelOffset);
        double level = 0.0;
        paramsGetValue(&plugin->plugin, levelId, &level);
        const CGFloat levelNorm = (level + 60.0) / 72.0;
        const NSRect fader = laneFaderRect(lane);
        [@"LVL" drawAtPoint:NSMakePoint(panel.origin.x + 16.0, 332.0)
            withAttributes:labels];
        char levelText[32] {};
        paramsValueToText(&plugin->plugin, levelId, level,
            levelText, sizeof(levelText));
        s3g::clap_gui::drawBoundedRightText(
            [NSString stringWithUTF8String:levelText],
            NSMakeRect(panel.origin.x + 16.0, 534.0,
                panel.size.width - 32.0, 15.0), values);
        s3g::clap_gui::drawMixerFader(levelNorm, fader, style);

        const float peak = plugin->lanePeaks[lane].load(
            std::memory_order_relaxed);
        const float peakDb = 20.0f * std::log10(std::max(peak, 0.000001f));
        const CGFloat meterNorm = std::clamp((peakDb + 60.0f) / 60.0f,
            0.0f, 1.0f);
        const NSRect meter = NSMakeRect(panel.origin.x + 98.0,
            fader.origin.y, 10.0, fader.size.height);
        s3g::clap_gui::drawVerticalVuMeter(meterNorm, meter, style);
    }

    const NSRect outputPanel = outputPanelRect();
    s3g::clap_gui::drawPanelFrame(outputPanel.origin.x, outputPanel.origin.y,
        outputPanel.size.width, outputPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true,
        outputPanel.origin.x, outputPanel.origin.y,
        outputPanel.size.width,
        static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.headerHeight), labels, style);
    double masterLevel = 0.0;
    paramsGetValue(&plugin->plugin, kMasterLevelParamId, &masterLevel);
    ParamSpec masterSpec;
    paramSpec(kMasterLevelParamId, masterSpec);
    char masterText[32] {};
    paramsValueToText(&plugin->plugin, kMasterLevelParamId, masterLevel,
        masterText, sizeof(masterText));
    s3g::clap_gui::drawMixerStripSlider(@"OUT",
        [NSString stringWithUTF8String:masterText],
        (masterLevel - masterSpec.minimum)
            / (masterSpec.maximum - masterSpec.minimum),
        outputSliderY(), outputPanel.origin.x, outputPanel.size.width,
        labels, values, style);
    [@"ROUTE" drawAtPoint:NSMakePoint(kMasterPanelX + 16.0, 109.0)
        withAttributes:labels];
    s3g::clap_gui::drawHeaderButton(sumButtonRect(), outputPanel, @"SUM",
        outputMode < 0.5, values, style);
    s3g::clap_gui::drawHeaderButton(directButtonRect(), outputPanel, @"DIRECT",
        outputMode >= 0.5, values, style);
    [@"SUM 1/2  //  DIRECT 1-16"
        drawAtPoint:NSMakePoint(kMasterPanelX + 16.0, 146.0)
        withAttributes:values];

    const NSRect busPanel = busPanelRect();
    s3g::clap_gui::drawPanelFrame(busPanel.origin.x, busPanel.origin.y,
        busPanel.size.width, busPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"DRUM BUS", true,
        busPanel.origin.x, busPanel.origin.y, busPanel.size.width,
        static_cast<CGFloat>(
            s3g::gui_layout::kStandardMetrics.headerHeight), labels, style);
    double busEnabled = 0.0;
    paramsGetValue(&plugin->plugin, kBusEnabledParamId, &busEnabled);
    s3g::clap_gui::drawHeaderButton(busButtonRect(), busPanel, @"AUX BUS",
        busEnabled >= 0.5, values, style);

    static constexpr const char* globalLabels[] {
        "DRV", "GLU", "ROM", "WGT", "TON", "RET",
    };
    constexpr std::array<clap_id, 6u> globalIds {{
        kBusDriveParamId, kBusGlueParamId, kBusRoomParamId,
        kBusWeightParamId, kBusToneParamId, kBusReturnParamId,
    }};
    for (uint32_t row = 0u; row < globalIds.size(); ++row) {
        const clap_id id = globalIds[row];
        double value = 0.0;
        paramsGetValue(&plugin->plugin, id, &value);
        ParamSpec spec;
        paramSpec(id, spec);
        char text[32] {};
        paramsValueToText(&plugin->plugin, id, value, text, sizeof(text));
        s3g::clap_gui::drawMixerStripSlider(
            [NSString stringWithUTF8String:globalLabels[row]],
            [NSString stringWithUTF8String:text],
            (value - spec.minimum) / (spec.maximum - spec.minimum),
            busSliderY(id), busPanel.origin.x, busPanel.size.width,
            labels, values, style);
    }
    const float activity = plugin->busActivity.load(
        std::memory_order_relaxed);
    const float reduction = plugin->busReductionDb.load(
        std::memory_order_relaxed);
    [[NSString stringWithFormat:@"BUS %.0f%%  //  GR %+.1f dB",
        activity * 100.0f, reduction]
        drawAtPoint:NSMakePoint(kMasterPanelX + 16.0, 412.0)
        withAttributes:values];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    ParamSpec spec;
    if (!paramSpec(id, spec)) return;
    NSRect rect = NSZeroRect;
    NSRect track = NSZeroRect;
    uint32_t lane = 0u;
    LaneParamOffset offset {};
    bool vertical = false;
    if (decodeLaneParam(id, lane, offset)) {
        if (offset == kLaneLevelOffset) {
            rect = laneFaderRect(lane);
            vertical = true;
        } else if (offset == kLaneAuxOffset) {
            rect = laneAuxHitRect(lane);
            track = laneAuxTrackRect(lane);
        } else if (isLaneDialOffset(offset)) {
            rect = laneDialRect(lane, offset);
        }
    } else if (id == kMasterLevelParamId) {
        rect = outputSliderHitRect();
        track = outputSliderTrackRect();
    } else {
        rect = busSliderHitRect(id);
        track = busSliderTrackRect(id);
    }
    double normalized = 0.0;
    if (_dragIsDial) {
        const double delta = (_dragStartPoint.y - point.y)
            + (point.x - _dragStartPoint.x);
        normalized = normalizedParamValue(id, _dragStartValue)
            + delta / 160.0;
    } else if (vertical) {
        normalized = 1.0
            - (point.y - rect.origin.y) / rect.size.height;
    } else {
        normalized = (point.x - track.origin.x) / track.size.width;
    }
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = paramValueFromNormalized(id, normalized);
    if (!queueGuiValue(*static_cast<Plugin*>(_plugin), id, value)) NSBeep();
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    auto* plugin = static_cast<Plugin*>(_plugin);
    if (!plugin) return;
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (s3g::clap_gui::handleProcessorTitleClick(point,
            &plugin->plugin, @"Drum Mixer 16", titleBand,
            _presetName, sizeof(_presetName))) {
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect sumButton = sumButtonRect();
    const NSRect directButton = directButtonRect();
    if (NSPointInRect(point, sumButton) || NSPointInRect(point, directButton)) {
        queueGuiGesture(*plugin, kOutputModeParamId,
            NSPointInRect(point, directButton) ? 1.0 : 0.0);
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect busButton = busButtonRect();
    if (NSPointInRect(point, busButton)) {
        double enabled = 0.0;
        paramsGetValue(&plugin->plugin, kBusEnabledParamId, &enabled);
        queueGuiGesture(*plugin, kBusEnabledParamId, enabled >= 0.5 ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t lane = 0u; lane < s3g::kDrumMixerLaneCount; ++lane) {
        const clap_id muteId = laneParamId(lane, kLaneMuteOffset);
        const clap_id soloId = laneParamId(lane, kLaneSoloOffset);
        if (NSPointInRect(point, laneMuteRect(lane))
            || NSPointInRect(point, laneSoloRect(lane))) {
            const clap_id id = NSPointInRect(point, laneSoloRect(lane))
                ? soloId : muteId;
            double value = 0.0;
            paramsGetValue(&plugin->plugin, id, &value);
            queueGuiGesture(*plugin, id, value >= 0.5 ? 0.0 : 1.0);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, laneFaderRect(lane))) {
            const clap_id id = laneParamId(lane, kLaneLevelOffset);
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &plugin->plugin, id, &defaultValue)) {
                queueGuiGesture(*plugin, id, defaultValue);
            } else {
                _dragParam = static_cast<int>(id);
                _dragIsDial = NO;
                queueGuiEvent(*plugin,
                    s3g::clap_gui::ParamEventKind::GestureBegin, id);
                [self updateDraggedParam:point];
            }
            return;
        }
        constexpr std::array<LaneParamOffset, 5u> dialOffsets {{
            kLanePanOffset, kLaneLowOffset, kLaneMidOffset,
            kLaneMidFrequencyOffset, kLaneHighOffset,
        }};
        for (const auto offset : dialOffsets) {
            if (!NSPointInRect(point, laneDialRect(lane, offset))) continue;
            const clap_id id = laneParamId(lane, offset);
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &plugin->plugin, id, &defaultValue)) {
                queueGuiGesture(*plugin, id, defaultValue);
            } else {
                _dragParam = static_cast<int>(id);
                _dragIsDial = YES;
                _dragStartPoint = point;
                paramsGetValue(&plugin->plugin, id, &_dragStartValue);
                queueGuiEvent(*plugin,
                    s3g::clap_gui::ParamEventKind::GestureBegin, id);
            }
            return;
        }
        if (NSPointInRect(point, laneAuxHitRect(lane))) {
            const clap_id id = laneParamId(lane, kLaneAuxOffset);
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &plugin->plugin, id, &defaultValue)) {
                queueGuiGesture(*plugin, id, defaultValue);
            } else {
                _dragParam = static_cast<int>(id);
                _dragIsDial = NO;
                queueGuiEvent(*plugin,
                    s3g::clap_gui::ParamEventKind::GestureBegin, id);
                [self updateDraggedParam:point];
            }
            return;
        }
    }
    constexpr std::array<clap_id, 7u> globalIds {{
        kBusDriveParamId, kBusGlueParamId, kBusRoomParamId,
        kBusWeightParamId, kBusToneParamId, kBusReturnParamId,
        kMasterLevelParamId,
    }};
    for (uint32_t row = 0u; row < globalIds.size(); ++row) {
        const clap_id id = globalIds[row];
        const NSRect rect = id == kMasterLevelParamId
            ? outputSliderHitRect() : busSliderHitRect(id);
        if (!NSPointInRect(point, rect)) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &plugin->plugin, id, &defaultValue)) {
            queueGuiGesture(*plugin, id, defaultValue);
        } else {
            _dragParam = static_cast<int>(id);
            _dragIsDial = NO;
            queueGuiEvent(*plugin,
                s3g::clap_gui::ParamEventKind::GestureBegin, id);
            [self updateDraggedParam:point];
        }
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam > 0) {
        [self updateDraggedParam:[self convertPoint:
            [event locationInWindow] fromView:nil]];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam > 0) {
        queueGuiEvent(*static_cast<Plugin*>(_plugin),
            s3g::clap_gui::ParamEventKind::GestureEnd,
            static_cast<clap_id>(_dragParam));
    }
    _dragParam = -1;
    _dragIsDial = NO;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GDrumMixerView alloc] initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView),
            kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return;
    instance->guiVisible = false;
    [static_cast<S3GDrumMixerView*>(instance->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        instance->guiViewport, instance->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height);
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
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height);
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
    auto* instance = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        instance->guiViewport, static_cast<NSView*>(window->cocoa),
        instance->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView
        || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    instance->guiVisible = true;
    [static_cast<S3GDrumMixerView*>(instance->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GDrumMixerView*>(instance->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

} // namespace
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_MIXING,
    CLAP_PLUGIN_FEATURE_MULTI_EFFECTS,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.drum-mixer-16",
    "s3g Drum Mixer 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.2.0",
    "Eight stereo drum lanes with sum/direct routing and a parallel drum bus.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
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
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin
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
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
