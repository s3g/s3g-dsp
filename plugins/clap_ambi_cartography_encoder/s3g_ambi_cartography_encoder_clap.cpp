#include "s3g_ambi_cartography_encoder.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_gui_param_queue.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
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

constexpr uint32_t kInputChannels = 2u;
constexpr uint32_t kOutputChannels = s3g::kAmbiCartographyMaxChannels;
constexpr uint32_t kStateVersion = 1u;

enum ParamId : clap_id {
    kSitesParamId = 1u,
    kSiteParamId,
    kOrderParamId,
    kLayoutParamId,
    kStereoMapParamId,
    kTimeReferenceParamId,
    kMapScaleParamId,
    kListenerXParamId,
    kListenerYParamId,
    kListenerZParamId,
    kNetworkSpreadParamId,
    kPropagationParamId,
    kAirParamId,
    kDistanceLossParamId,
    kCarryParamId,
    kTurbulenceParamId,
    kMacroEngineParamId,
    kMacroMetricParamId,
    kMacroParamId,
    kColorParamId,
    kMemoryParamId,
    kSpreadParamId,
    kDeviationParamId,
    kSkewParamId,
    kCenterParamId,
    kProcessMixParamId,
    kListenModeParamId,
    kListenerAmountParamId,
    kOutputParamId,
    kSiteXParamId,
    kSiteYParamId,
    kSiteZParamId,
    kSiteGainParamId,
    kSiteNetworkTrimParamId,
    kSiteEnabledParamId,
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

constexpr ParamDef kParams[] {
    { kSitesParamId, "Sites", "Cartography", 1.0, 24.0, 12.0, true },
    { kSiteParamId, "Selected Site", "Cartography", 1.0, 24.0, 1.0, true },
    { kOrderParamId, "Order", "Output", 1.0, 7.0, 3.0, true },
    { kLayoutParamId, "Layout", "Cartography", 0.0, 4.0, 0.0, true },
    { kStereoMapParamId, "Stereo Map", "Feed", 0.0, 2.0, 0.0, true },
    { kTimeReferenceParamId, "Time Reference", "Propagation", 0.0, 1.0, 0.0, true },
    { kMapScaleParamId, "Map Scale", "Cartography", 10.0, 2000.0, 240.0, false },
    { kListenerXParamId, "Listener X", "Listener", -1.5, 1.5, 0.0, false },
    { kListenerYParamId, "Listener Y", "Listener", -1.5, 1.5, 0.0, false },
    { kListenerZParamId, "Listener Z", "Listener", -1.0, 1.0, 0.0, false },
    { kNetworkSpreadParamId, "Network Delay", "Feed", 0.0, 2000.0, 420.0, false },
    { kPropagationParamId, "Propagation Time", "Propagation", 0.0, 1.0, 0.35, false },
    { kAirParamId, "Air Loss", "Propagation", 0.0, 1.0, 0.28, false },
    { kDistanceLossParamId, "Distance Loss", "Propagation", 0.0, 1.0, 0.55, false },
    { kCarryParamId, "Carry", "Propagation", 0.0, 1.0, 0.20, false },
    { kTurbulenceParamId, "Turbulence", "Propagation", 0.0, 1.0, 0.08, false },
    { kListenModeParamId, "Field Listen", "Listener", 0.0, 3.0, 0.0, true },
    { kListenerAmountParamId, "Listen Amount", "Listener", 0.0, 1.0, 0.0, false },
    { kMacroEngineParamId, "Process", "Site Process", 0.0, 4.0, 1.0, true },
    { kMacroMetricParamId, "Macro Metric", "Site Process", 0.0, 3.0, 0.0, true },
    { kMacroParamId, "Macro", "Site Process", 0.0, 1.0, 0.50, false },
    { kColorParamId, "Color", "Site Process", 0.0, 1.0, 0.55, false },
    { kMemoryParamId, "Memory", "Site Process", 0.0, 1.0, 0.28, false },
    { kSpreadParamId, "Spread", "Site Process", 0.0, 1.0, 0.35, false },
    { kDeviationParamId, "Deviation", "Site Process", 0.0, 1.0, 0.12, false },
    { kSkewParamId, "Skew", "Site Process", -1.0, 1.0, 0.0, false },
    { kCenterParamId, "Center", "Site Process", 0.0, 1.0, 0.5, false },
    { kProcessMixParamId, "Process Mix", "Site Process", 0.0, 1.0, 0.42, false },
    { kOutputParamId, "Output", "Output", -60.0, 12.0, -6.0, false },
    { kSiteXParamId, "Site X", "Selected Site", -1.5, 1.5, 0.0, false },
    { kSiteYParamId, "Site Y", "Selected Site", -1.5, 1.5, 1.0, false },
    { kSiteZParamId, "Site Z", "Selected Site", -1.0, 1.0, 0.0, false },
    { kSiteGainParamId, "Site Gain", "Selected Site", 0.0, 2.0, 1.0, false },
    { kSiteNetworkTrimParamId, "Site Network Trim", "Selected Site", -500.0, 500.0, 0.0, false },
    { kSiteEnabledParamId, "Site Enabled", "Selected Site", 0.0, 1.0, 1.0, true },
};

constexpr uint32_t kParamCount = static_cast<uint32_t>(std::size(kParams));

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiCartographyEncoderParams params {};
    std::array<s3g::AmbiCartographySite,
        s3g::kAmbiCartographyMaxSites> sites {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiCartographyEncoder encoder {};
    s3g::AmbiCartographyEncoderParams params {};
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<bool> layoutResetPending { false };
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

const char* layoutName(uint32_t index)
{
    static constexpr const char* names[] {
        "RADIAL", "RIDGE", "CORRIDOR", "GRID", "WATERFRONT"
    };
    return names[std::min<uint32_t>(index, 4u)];
}

const char* stereoMapName(uint32_t index)
{
    static constexpr const char* names[] { "MID/SIDE", "MONO", "ALTERNATE" };
    return names[std::min<uint32_t>(index, 2u)];
}

const char* timeReferenceName(uint32_t index)
{
    return index == 0u ? "RELATIVE" : "ABSOLUTE";
}

const char* macroEngineName(uint32_t index)
{
    static constexpr const char* names[] {
        "CLEAN", "DELAY", "PITCH", "SHRED", "FRACTURE"
    };
    return names[std::min<uint32_t>(index, 4u)];
}

const char* macroMetricName(uint32_t index)
{
    static constexpr const char* names[] {
        "NETWORK", "ARRIVAL", "BEARING", "RANGE"
    };
    return names[std::min<uint32_t>(index, 3u)];
}

const char* listenModeName(uint32_t index)
{
    static constexpr const char* names[] {
        "OFF", "FOLLOW", "COUNTER", "BALANCE"
    };
    return names[std::min<uint32_t>(index, 3u)];
}

uint32_t paramIndex(clap_id id)
{
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        if (kParams[index].id == id) return index;
    }
    return kParamCount;
}

double rawParamValue(const Plugin& plugin, clap_id id)
{
    const auto& params = plugin.params;
    switch (id) {
    case kSitesParamId: return params.activeSites;
    case kSiteParamId: return params.selectedSite + 1u;
    case kOrderParamId: return params.order;
    case kLayoutParamId: return static_cast<uint32_t>(params.layout);
    case kStereoMapParamId: return static_cast<uint32_t>(params.stereoMap);
    case kTimeReferenceParamId:
        return static_cast<uint32_t>(params.timeReference);
    case kMapScaleParamId: return params.mapScaleMeters;
    case kListenerXParamId: return params.listenerX;
    case kListenerYParamId: return params.listenerY;
    case kListenerZParamId: return params.listenerZ;
    case kNetworkSpreadParamId: return params.networkSpreadMs;
    case kPropagationParamId: return params.propagationScale;
    case kAirParamId: return params.air;
    case kDistanceLossParamId: return params.distanceLoss;
    case kCarryParamId: return params.carry;
    case kTurbulenceParamId: return params.turbulence;
    case kMacroEngineParamId:
        return static_cast<uint32_t>(params.macroEngine);
    case kMacroMetricParamId:
        return static_cast<uint32_t>(params.macroMetric);
    case kMacroParamId: return params.macro;
    case kColorParamId: return params.color;
    case kMemoryParamId: return params.memory;
    case kSpreadParamId: return params.spread;
    case kDeviationParamId: return params.deviation;
    case kSkewParamId: return params.skew;
    case kCenterParamId: return params.center;
    case kProcessMixParamId: return params.processMix;
    case kListenModeParamId:
        return static_cast<uint32_t>(params.listenMode);
    case kListenerAmountParamId: return params.listenerAmount;
    case kOutputParamId: return params.outputGainDb;
    case kSiteXParamId: return params.selectedX;
    case kSiteYParamId: return params.selectedY;
    case kSiteZParamId: return params.selectedZ;
    case kSiteGainParamId: return params.selectedGain;
    case kSiteNetworkTrimParamId: return params.selectedNetworkTrimMs;
    case kSiteEnabledParamId: return params.selectedEnabled ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void publishParam(Plugin& plugin, clap_id id)
{
    const uint32_t index = paramIndex(id);
    if (index < kParamCount) {
        plugin.publishedParams[index].store(
            rawParamValue(plugin, id), std::memory_order_relaxed);
    }
}

void publishAllParams(Plugin& plugin)
{
    for (const auto& definition : kParams) {
        publishParam(plugin, definition.id);
    }
}

double publishedParamValue(const Plugin& plugin, clap_id id)
{
    const uint32_t index = paramIndex(id);
    return index < kParamCount
        ? plugin.publishedParams[index].load(std::memory_order_relaxed)
        : 0.0;
}

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t written = stream->write(
            stream, bytes + offset, size - offset);
        if (written <= 0) return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t read = stream->read(
            stream, bytes + offset, size - offset);
        if (read <= 0) return false;
        offset += static_cast<size_t>(read);
    }
    return true;
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    auto& params = plugin.params;
    switch (id) {
    case kSitesParamId:
        params.activeSites = static_cast<uint32_t>(std::lround(value));
        break;
    case kSiteParamId:
        params.selectedSite = static_cast<uint32_t>(
            std::max(1.0, std::round(value))) - 1u;
        break;
    case kOrderParamId:
        params.order = static_cast<uint32_t>(std::lround(value));
        break;
    case kLayoutParamId:
        params.layout = static_cast<s3g::AmbiCartographyLayout>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kStereoMapParamId:
        params.stereoMap = static_cast<s3g::AmbiCartographyStereoMap>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kTimeReferenceParamId:
        params.timeReference = static_cast<s3g::AmbiCartographyTimeReference>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kMapScaleParamId: params.mapScaleMeters = static_cast<float>(value); break;
    case kListenerXParamId: params.listenerX = static_cast<float>(value); break;
    case kListenerYParamId: params.listenerY = static_cast<float>(value); break;
    case kListenerZParamId: params.listenerZ = static_cast<float>(value); break;
    case kNetworkSpreadParamId: params.networkSpreadMs = static_cast<float>(value); break;
    case kPropagationParamId: params.propagationScale = static_cast<float>(value); break;
    case kAirParamId: params.air = static_cast<float>(value); break;
    case kDistanceLossParamId: params.distanceLoss = static_cast<float>(value); break;
    case kCarryParamId: params.carry = static_cast<float>(value); break;
    case kTurbulenceParamId: params.turbulence = static_cast<float>(value); break;
    case kMacroEngineParamId:
        params.macroEngine = static_cast<s3g::AmbiCartographyMacroEngine>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kMacroMetricParamId:
        params.macroMetric = static_cast<s3g::AmbiCartographyMacroMetric>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kMacroParamId: params.macro = static_cast<float>(value); break;
    case kColorParamId: params.color = static_cast<float>(value); break;
    case kMemoryParamId: params.memory = static_cast<float>(value); break;
    case kSpreadParamId: params.spread = static_cast<float>(value); break;
    case kDeviationParamId: params.deviation = static_cast<float>(value); break;
    case kSkewParamId: params.skew = static_cast<float>(value); break;
    case kCenterParamId: params.center = static_cast<float>(value); break;
    case kProcessMixParamId: params.processMix = static_cast<float>(value); break;
    case kListenModeParamId:
        params.listenMode = static_cast<s3g::AmbiFieldListenMode>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kListenerAmountParamId: params.listenerAmount = static_cast<float>(value); break;
    case kOutputParamId: params.outputGainDb = static_cast<float>(value); break;
    case kSiteXParamId: params.selectedX = static_cast<float>(value); break;
    case kSiteYParamId: params.selectedY = static_cast<float>(value); break;
    case kSiteZParamId: params.selectedZ = static_cast<float>(value); break;
    case kSiteGainParamId: params.selectedGain = static_cast<float>(value); break;
    case kSiteNetworkTrimParamId:
        params.selectedNetworkTrimMs = static_cast<float>(value);
        break;
    case kSiteEnabledParamId: params.selectedEnabled = value >= 0.5; break;
    default: return;
    }
    plugin.encoder.setParams(params);
    plugin.params = plugin.encoder.params();
    publishAllParams(plugin);
}

bool init(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->host && instance->host->get_extension) {
        instance->hostParams = static_cast<const clap_host_params_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_PARAMS));
    }
    return true;
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiParamEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
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
    const uint32_t index = paramIndex(id);
    if (index >= kParamCount) return;
    value = std::clamp(value,
        kParams[index].minimum, kParams[index].maximum);
    plugin.publishedParams[index].store(value, std::memory_order_relaxed);
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queueGuiParamGesture(Plugin& plugin, clap_id id, double value)
{
    queueGuiParamGestureBegin(plugin, id);
    queueGuiParamValue(plugin, id, value);
    queueGuiParamGestureEnd(plugin, id);
}

bool pushGuiParamEvent(const clap_output_events_t* output,
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

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(plugin, pending.paramId, pending.value);
        }
        plugin.guiParamEvents.pop();
    }
    s3g::clap_gui::ParamEvent remaining {};
    if (!plugin.guiParamEvents.peek(remaining)
        && plugin.layoutResetPending.exchange(
            false, std::memory_order_acq_rel)) {
        plugin.encoder.regenerateLayout();
        plugin.params = plugin.encoder.params();
        publishAllParams(plugin);
    }
}

void destroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
#if defined(__APPLE__)
    if (instance && instance->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(
            instance->guiViewport, instance->guiView);
    }
#endif
    delete instance;
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    instance->encoder.prepare(sampleRate);
    instance->encoder.setParams(instance->params);
    instance->params = instance->encoder.params();
    publishAllParams(*instance);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->encoder.reset();
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
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
        const auto* parameter =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, parameter->param_id, parameter->value);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* instance = self(plugin);
    serviceGuiParamEvents(*instance, processData->out_events);
    readParamEvents(*instance, processData->in_events);
    if (processData->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    auto& output = processData->audio_outputs[0];
    const auto* input = processData->audio_inputs_count > 0u
        ? &processData->audio_inputs[0] : nullptr;
    const uint32_t frames = processData->frames_count;
    const uint32_t inputCount = input
        ? std::min<uint32_t>(input->channel_count, kInputChannels) : 0u;
    const uint32_t outputCount = std::min<uint32_t>(
        output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0u, frames);
    if (!output.data32 || outputCount == 0u) return CLAP_PROCESS_CONTINUE;

    std::array<const float*, kInputChannels> inputPointers {};
    std::array<float*, kOutputChannels> outputPointers {};
    for (uint32_t channel = 0u; channel < inputCount; ++channel) {
        inputPointers[channel] = input && input->data32
            ? input->data32[channel] : nullptr;
    }
    for (uint32_t channel = 0u; channel < outputCount; ++channel) {
        outputPointers[channel] = output.data32[channel];
    }

    instance->encoder.setParams(instance->params);
    instance->encoder.processBlock(inputPointers.data(), outputPointers.data(),
        inputCount, outputCount, frames);
    instance->params = instance->encoder.params();
    s3g::clearAudioBufferFromChannel(output, outputCount, frames);

    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < outputCount; ++channel) {
        if (!output.data32[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            peak = std::max(peak, std::fabs(output.data32[channel][frame]));
        }
    }
    instance->outputPeak.store(std::max(
        instance->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "Stereo Cartography In" : "7OA ACN/SN3D Out",
        sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_STEREO : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(std::size(kParams));
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= std::size(kParams)) return false;
    const auto& definition = kParams[index];
    info->id = definition.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (definition.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, definition.name, sizeof(info->name));
    std::strncpy(info->module, definition.module, sizeof(info->module));
    info->min_value = definition.minimum;
    info->max_value = definition.maximum;
    info->default_value = definition.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    if (paramIndex(id) >= kParamCount) return false;
    *value = publishedParamValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    const uint32_t stepped = static_cast<uint32_t>(std::lround(value));
    if (id == kLayoutParamId) {
        std::snprintf(display, size, "%s", layoutName(stepped));
    } else if (id == kStereoMapParamId) {
        std::snprintf(display, size, "%s", stereoMapName(stepped));
    } else if (id == kTimeReferenceParamId) {
        std::snprintf(display, size, "%s", timeReferenceName(stepped));
    } else if (id == kMacroEngineParamId) {
        std::snprintf(display, size, "%s", macroEngineName(stepped));
    } else if (id == kMacroMetricParamId) {
        std::snprintf(display, size, "%s", macroMetricName(stepped));
    } else if (id == kListenModeParamId) {
        std::snprintf(display, size, "%s", listenModeName(stepped));
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%uOA", stepped);
    } else if (id == kSitesParamId || id == kSiteParamId) {
        std::snprintf(display, size, "%u", stepped);
    } else if (id == kSiteEnabledParamId) {
        std::snprintf(display, size, "%s", stepped ? "ON" : "OFF");
    } else if (id == kMapScaleParamId) {
        std::snprintf(display, size, "%.0f m", value);
    } else if (id == kNetworkSpreadParamId
        || id == kSiteNetworkTrimParamId) {
        std::snprintf(display, size, "%+.0f ms", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kListenerXParamId || id == kListenerYParamId
        || id == kListenerZParamId || id == kSiteXParamId
        || id == kSiteYParamId || id == kSiteZParamId
        || id == kSiteGainParamId) {
        std::snprintf(display, size, "%+.2f", value);
    } else {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    const auto parseName = [&](auto name, uint32_t count) {
        for (uint32_t index = 0u; index < count; ++index) {
            if (std::strcmp(display, name(index)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    };
    if (id == kLayoutParamId) return parseName(layoutName, 5u);
    if (id == kStereoMapParamId) return parseName(stereoMapName, 3u);
    if (id == kTimeReferenceParamId) return parseName(timeReferenceName, 2u);
    if (id == kMacroEngineParamId) return parseName(macroEngineName, 5u);
    if (id == kMacroMetricParamId) return parseName(macroMetricName, 4u);
    if (id == kListenModeParamId) return parseName(listenModeName, 4u);
    *value = std::atof(display);
    if (id == kPropagationParamId || id == kAirParamId
        || id == kDistanceLossParamId || id == kCarryParamId
        || id == kTurbulenceParamId || id == kMacroParamId
        || id == kColorParamId || id == kMemoryParamId
        || id == kSpreadParamId || id == kDeviationParamId
        || id == kSkewParamId || id == kCenterParamId
        || id == kProcessMixParamId || id == kListenerAmountParamId) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* instance = self(plugin);
    serviceGuiParamEvents(*instance, output);
    readParamEvents(*instance, input);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* instance = self(plugin);
    SavedState state {
        kStateVersion, instance->params, instance->encoder.sites()
    };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readExact(stream, &state, sizeof(state))
        || state.version != kStateVersion) {
        return false;
    }
    auto* instance = self(plugin);
    instance->encoder.setParams(state.params);
    instance->encoder.setSites(state.sites);
    instance->params = instance->encoder.params();
    publishAllParams(*instance);
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {

constexpr CGFloat kGuiWidth = 1356.0;
constexpr CGFloat kGuiHeight = 760.0;
constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;
constexpr uint32_t kFactoryPresetCount = 9u;

enum class CartographyPanel : uint8_t {
    Output,
    Cartography,
    Propagation,
    Listener,
    Process,
    SelectedSite,
    Guide,
};

enum class CartographyControlKind : uint8_t {
    Slider,
    Menu,
    Toggle,
};

struct CartographyControl {
    clap_id id;
    const char* label;
    CartographyPanel panel;
    uint32_t row;
    CartographyControlKind kind;
};

constexpr std::array<CartographyControl, kParamCount> kGuiControls {{
    { kOrderParamId, "ORDER", CartographyPanel::Output, 0u,
        CartographyControlKind::Menu },
    { kOutputParamId, "OUT", CartographyPanel::Output, 1u,
        CartographyControlKind::Slider },

    { kSitesParamId, "SITES", CartographyPanel::Cartography, 0u,
        CartographyControlKind::Slider },
    { kLayoutParamId, "LAYOUT", CartographyPanel::Cartography, 1u,
        CartographyControlKind::Menu },
    { kStereoMapParamId, "STEREO", CartographyPanel::Cartography, 2u,
        CartographyControlKind::Menu },
    { kMapScaleParamId, "SCALE", CartographyPanel::Cartography, 3u,
        CartographyControlKind::Slider },
    { kNetworkSpreadParamId, "NETWORK", CartographyPanel::Cartography, 4u,
        CartographyControlKind::Slider },

    { kTimeReferenceParamId, "TIME REF", CartographyPanel::Propagation, 0u,
        CartographyControlKind::Menu },
    { kPropagationParamId, "PROP TIME", CartographyPanel::Propagation, 1u,
        CartographyControlKind::Slider },
    { kAirParamId, "AIR", CartographyPanel::Propagation, 2u,
        CartographyControlKind::Slider },
    { kDistanceLossParamId, "DIST LOSS", CartographyPanel::Propagation, 3u,
        CartographyControlKind::Slider },
    { kCarryParamId, "CARRY", CartographyPanel::Propagation, 4u,
        CartographyControlKind::Slider },
    { kTurbulenceParamId, "TURB", CartographyPanel::Propagation, 5u,
        CartographyControlKind::Slider },

    { kListenModeParamId, "MODE", CartographyPanel::Listener, 0u,
        CartographyControlKind::Menu },
    { kListenerAmountParamId, "AMOUNT", CartographyPanel::Listener, 1u,
        CartographyControlKind::Slider },
    { kListenerXParamId, "X", CartographyPanel::Listener, 2u,
        CartographyControlKind::Slider },
    { kListenerYParamId, "Y", CartographyPanel::Listener, 3u,
        CartographyControlKind::Slider },
    { kListenerZParamId, "Z", CartographyPanel::Listener, 4u,
        CartographyControlKind::Slider },

    { kMacroEngineParamId, "ENGINE", CartographyPanel::Process, 0u,
        CartographyControlKind::Menu },
    { kMacroMetricParamId, "METRIC", CartographyPanel::Process, 1u,
        CartographyControlKind::Menu },
    { kMacroParamId, "MACRO", CartographyPanel::Process, 2u,
        CartographyControlKind::Slider },
    { kColorParamId, "COLOR", CartographyPanel::Process, 3u,
        CartographyControlKind::Slider },
    { kMemoryParamId, "MEMORY", CartographyPanel::Process, 4u,
        CartographyControlKind::Slider },
    { kSpreadParamId, "SPREAD", CartographyPanel::Process, 5u,
        CartographyControlKind::Slider },
    { kDeviationParamId, "DEVIATE", CartographyPanel::Process, 6u,
        CartographyControlKind::Slider },
    { kSkewParamId, "SKEW", CartographyPanel::Process, 7u,
        CartographyControlKind::Slider },
    { kCenterParamId, "CENTER", CartographyPanel::Process, 8u,
        CartographyControlKind::Slider },
    { kProcessMixParamId, "MIX", CartographyPanel::Process, 9u,
        CartographyControlKind::Slider },

    { kSiteParamId, "SITE", CartographyPanel::SelectedSite, 0u,
        CartographyControlKind::Slider },
    { kSiteEnabledParamId, "ENABLED", CartographyPanel::SelectedSite, 1u,
        CartographyControlKind::Toggle },
    { kSiteXParamId, "X", CartographyPanel::SelectedSite, 2u,
        CartographyControlKind::Slider },
    { kSiteYParamId, "Y", CartographyPanel::SelectedSite, 3u,
        CartographyControlKind::Slider },
    { kSiteZParamId, "Z", CartographyPanel::SelectedSite, 4u,
        CartographyControlKind::Slider },
    { kSiteGainParamId, "GAIN", CartographyPanel::SelectedSite, 5u,
        CartographyControlKind::Slider },
    { kSiteNetworkTrimParamId, "NET TRIM", CartographyPanel::SelectedSite, 6u,
        CartographyControlKind::Slider },
}};

static_assert(kGuiControls.size() == std::size(kParams));

struct FactoryPreset {
    const char* name;
    uint32_t sites;
    uint32_t layout;
    uint32_t stereo;
    uint32_t timeReference;
    float scale;
    float network;
    float propagation;
    float air;
    float distanceLoss;
    float carry;
    float turbulence;
    uint32_t engine;
    uint32_t metric;
    float macro;
    float color;
    float memory;
    float spread;
    float deviation;
    float skew;
    float center;
    float mix;
    uint32_t listenMode;
    float listenAmount;
    float listenerX;
    float listenerY;
    float listenerZ;
};

constexpr std::array<FactoryPreset, kFactoryPresetCount> kFactoryPresets {{
    { "INIT", 12u, 0u, 0u, 0u, 240.0f, 420.0f,
        0.35f, 0.28f, 0.55f, 0.20f, 0.08f,
        1u, 0u, 0.50f, 0.55f, 0.28f, 0.35f, 0.12f, 0.0f, 0.50f, 0.42f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f },
    { "PHYSICAL PLAZA", 8u, 0u, 0u, 0u, 55.0f, 0.0f,
        1.0f, 0.08f, 0.36f, 0.08f, 0.02f,
        0u, 3u, 0.18f, 0.40f, 0.10f, 0.18f, 0.02f, 0.0f, 0.50f, 0.0f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f },
    { "CITY RELAYS", 16u, 3u, 2u, 0u, 480.0f, 1450.0f,
        0.70f, 0.30f, 0.42f, 0.40f, 0.10f,
        1u, 0u, 0.62f, 0.58f, 0.62f, 0.74f, 0.18f, 0.20f, 0.45f, 0.62f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f },
    { "RIDGE AFTERIMAGE", 12u, 1u, 0u, 1u, 1200.0f, 620.0f,
        1.0f, 0.46f, 0.52f, 0.60f, 0.12f,
        1u, 1u, 0.72f, 0.42f, 0.76f, 0.58f, 0.16f, -0.18f, 0.56f, 0.68f,
        1u, 0.28f, -0.20f, -0.25f, 0.0f },
    { "WATERFRONT CARRY", 18u, 4u, 1u, 0u, 1800.0f, 880.0f,
        0.85f, 0.18f, 0.32f, 0.85f, 0.16f,
        2u, 3u, 0.42f, 0.68f, 0.48f, 0.66f, 0.10f, 0.16f, 0.62f, 0.54f,
        1u, 0.32f, 0.0f, -0.45f, 0.0f },
    { "CORRIDOR BENDS", 10u, 2u, 2u, 1u, 240.0f, 280.0f,
        0.45f, 0.15f, 0.60f, 0.25f, 0.05f,
        2u, 2u, 0.66f, 0.48f, 0.38f, 0.58f, 0.22f, -0.34f, 0.46f, 0.58f,
        2u, 0.40f, 0.18f, 0.0f, 0.0f },
    { "SHRED GRID", 24u, 3u, 2u, 0u, 700.0f, 1800.0f,
        0.50f, 0.38f, 0.65f, 0.30f, 0.24f,
        3u, 0u, 0.78f, 0.74f, 0.34f, 0.92f, 0.48f, 0.28f, 0.36f, 0.70f,
        0u, 0.0f, 0.0f, 0.0f, 0.0f },
    { "FRACTURE HORIZON", 20u, 4u, 0u, 1u, 1500.0f, 1550.0f,
        1.0f, 0.62f, 0.48f, 0.65f, 0.32f,
        4u, 1u, 0.86f, 0.82f, 0.72f, 0.88f, 0.54f, -0.22f, 0.68f, 0.72f,
        2u, 0.55f, 0.38f, 0.18f, 0.0f },
    { "LISTENER DRIFT", 14u, 0u, 0u, 0u, 520.0f, 780.0f,
        0.72f, 0.26f, 0.48f, 0.44f, 0.14f,
        1u, 2u, 0.56f, 0.52f, 0.58f, 0.62f, 0.20f, 0.12f, 0.52f, 0.56f,
        3u, 0.78f, -0.55f, 0.35f, 0.0f },
}};

NSRect panelRect(CartographyPanel panel)
{
    switch (panel) {
    case CartographyPanel::Output: return NSMakeRect(680.0, 42.0, 320.0, 96.0);
    case CartographyPanel::Cartography: return NSMakeRect(680.0, 150.0, 320.0, 164.0);
    case CartographyPanel::Propagation: return NSMakeRect(680.0, 326.0, 320.0, 190.0);
    case CartographyPanel::Listener: return NSMakeRect(680.0, 528.0, 320.0, 210.0);
    case CartographyPanel::Process: return NSMakeRect(1012.0, 42.0, 326.0, 294.0);
    case CartographyPanel::SelectedSite: return NSMakeRect(1012.0, 348.0, 326.0, 216.0);
    case CartographyPanel::Guide: return NSMakeRect(1012.0, 576.0, 326.0, 162.0);
    }
    return NSZeroRect;
}

NSRect cartographyMapRect()
{
    return NSMakeRect(34.0, 76.0, 620.0, 642.0);
}

CGFloat controlY(const CartographyControl& control)
{
    const NSRect panel = panelRect(control.panel);
    return panel.origin.y
        + s3g::gui_layout::kStandardMetrics.firstRowOffset
        + static_cast<CGFloat>(control.row)
            * s3g::gui_layout::kStandardMetrics.rowPitch;
}

NSRect controlHitRect(const CartographyControl& control)
{
    const NSRect panel = panelRect(control.panel);
    return NSMakeRect(panel.origin.x
            + s3g::gui_layout::kStandardMetrics.hitInset,
        controlY(control) - 7.0,
        panel.size.width
            - 2.0 * s3g::gui_layout::kStandardMetrics.hitInset,
        s3g::gui_layout::kStandardMetrics.hitHeight);
}

NSRect controlMenuBoxRect(const CartographyControl& control)
{
    const NSRect panel = panelRect(control.panel);
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.origin.x),
        controlY(control) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.size.width), 15.0);
}

const CartographyControl* guiControl(clap_id id)
{
    for (const auto& control : kGuiControls) {
        if (control.id == id) return &control;
    }
    return nullptr;
}

uint32_t menuItemCount(clap_id id)
{
    switch (id) {
    case kOrderParamId: return 7u;
    case kLayoutParamId: return 5u;
    case kStereoMapParamId: return 3u;
    case kTimeReferenceParamId: return 2u;
    case kMacroEngineParamId: return 5u;
    case kMacroMetricParamId: return 4u;
    case kListenModeParamId: return 4u;
    default: return 0u;
    }
}

double menuValue(clap_id id, uint32_t item)
{
    return id == kOrderParamId
        ? static_cast<double>(item + 1u) : static_cast<double>(item);
}

} // namespace

@interface S3GAmbiCartographyEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSInteger _dragSite;
    BOOL _dragListener;
    int _factoryPresetIndex;
    char _presetName[64];
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)applyFactoryPreset:(int)index;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
- (NSPoint)mapPointX:(float)x y:(float)y;
- (void)mapCoordinatesFromPoint:(NSPoint)point x:(float*)x y:(float*)y;
@end

@implementation S3GAmbiCartographyEncoderView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _dragSite = -1;
        _dragListener = NO;
        _factoryPresetIndex = 0;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
        [self setWantsLayer:YES];
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

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0
        target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)timerTick:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin && _plugin->guiVisible
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)applyFactoryPreset:(int)index
{
    if (!_plugin) return;
    index = std::clamp(index, 0,
        static_cast<int>(kFactoryPresetCount) - 1);
    const auto& preset = kFactoryPresets[static_cast<uint32_t>(index)];
    const auto set = [&](clap_id id, double value) {
        queueGuiParamGesture(*_plugin, id, value);
    };
    set(kSitesParamId, preset.sites);
    set(kLayoutParamId, preset.layout);
    set(kStereoMapParamId, preset.stereo);
    set(kTimeReferenceParamId, preset.timeReference);
    set(kMapScaleParamId, preset.scale);
    set(kNetworkSpreadParamId, preset.network);
    set(kPropagationParamId, preset.propagation);
    set(kAirParamId, preset.air);
    set(kDistanceLossParamId, preset.distanceLoss);
    set(kCarryParamId, preset.carry);
    set(kTurbulenceParamId, preset.turbulence);
    set(kMacroEngineParamId, preset.engine);
    set(kMacroMetricParamId, preset.metric);
    set(kMacroParamId, preset.macro);
    set(kColorParamId, preset.color);
    set(kMemoryParamId, preset.memory);
    set(kSpreadParamId, preset.spread);
    set(kDeviationParamId, preset.deviation);
    set(kSkewParamId, preset.skew);
    set(kCenterParamId, preset.center);
    set(kProcessMixParamId, preset.mix);
    set(kListenModeParamId, preset.listenMode);
    set(kListenerAmountParamId, preset.listenAmount);
    set(kListenerXParamId, preset.listenerX);
    set(kListenerYParamId, preset.listenerY);
    set(kListenerZParamId, preset.listenerZ);
    set(kSiteParamId, 1.0);
    set(kSiteGainParamId, 1.0);
    set(kSiteNetworkTrimParamId, 0.0);
    set(kSiteEnabledParamId, 1.0);
    _plugin->layoutResetPending.store(true, std::memory_order_release);
    requestGuiParamService(*_plugin);
    _factoryPresetIndex = index;
    std::snprintf(_presetName,
        sizeof(_presetName), "%s", preset.name);
    [self setNeedsDisplay:YES];
}

- (NSRect)openMenuRect
{
    if (_openMenu == kFactoryPresetMenuId) {
        const NSRect box = s3g::clap_gui::cocoaRect(
            s3g::clap_gui::encoderTitleBand(
                kGuiWidth, kGuiHeight).presetMenu);
        return NSMakeRect(box.origin.x, NSMaxY(box) + 2.0,
            box.size.width, 18.0 * _menuItemCount);
    }
    const auto* control = guiControl(_openMenu);
    if (!control) return NSZeroRect;
    const NSRect box = controlMenuBoxRect(*control);
    return NSMakeRect(box.origin.x, NSMaxY(box) + 2.0,
        box.size.width, 18.0 * _menuItemCount);
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    NSString* factoryItems[kFactoryPresetCount] = {
        @"INIT", @"PHYSICAL PLAZA", @"CITY RELAYS",
        @"RIDGE AFTERIMAGE", @"WATERFRONT CARRY",
        @"CORRIDOR BENDS", @"SHRED GRID",
        @"FRACTURE HORIZON", @"LISTENER DRIFT"
    };
    NSString* orderItems[7] = {
        @"1OA / 4CH", @"2OA / 9CH", @"3OA / 16CH", @"4OA / 25CH",
        @"5OA / 36CH", @"6OA / 49CH", @"7OA / 64CH"
    };
    NSString* layoutItems[5] = {
        @"RADIAL", @"RIDGE", @"CORRIDOR", @"GRID", @"WATERFRONT"
    };
    NSString* stereoItems[3] = { @"MID/SIDE", @"MONO", @"ALTERNATE" };
    NSString* timeItems[2] = { @"RELATIVE", @"ABSOLUTE" };
    NSString* engineItems[5] = {
        @"CLEAN", @"DELAY", @"PITCH", @"SHRED", @"FRACTURE"
    };
    NSString* metricItems[4] = {
        @"NETWORK", @"ARRIVAL", @"BEARING", @"RANGE"
    };
    NSString* listenerItems[4] = {
        @"OFF", @"FOLLOW", @"COUNTER", @"BALANCE"
    };
    NSString* const* items = factoryItems;
    if (_openMenu == kOrderParamId) items = orderItems;
    else if (_openMenu == kLayoutParamId) items = layoutItems;
    else if (_openMenu == kStereoMapParamId) items = stereoItems;
    else if (_openMenu == kTimeReferenceParamId) items = timeItems;
    else if (_openMenu == kMacroEngineParamId) items = engineItems;
    else if (_openMenu == kMacroMetricParamId) items = metricItems;
    else if (_openMenu == kListenModeParamId) items = listenerItems;
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(
            publishedParamValue(*_plugin, _openMenu)))
            - (_openMenu == kOrderParamId ? 1 : 0);
    s3g::clap_gui::drawDropdownMenu(
        [self openMenuRect], 18.0, items, _menuItemCount,
        selected, _hoverMenuItem, attrs, style);
}

- (NSPoint)mapPointX:(float)x y:(float)y
{
    const NSRect rect = cartographyMapRect();
    return NSMakePoint(NSMidX(rect) + x / 1.6f * rect.size.width * 0.46f,
        NSMidY(rect) - y / 1.6f * rect.size.height * 0.46f);
}

- (void)mapCoordinatesFromPoint:(NSPoint)point x:(float*)x y:(float*)y
{
    const NSRect rect = cartographyMapRect();
    if (x) *x = std::clamp(static_cast<float>(
        (point.x - NSMidX(rect)) / (rect.size.width * 0.46f) * 1.6f),
        -1.5f, 1.5f);
    if (y) *y = std::clamp(static_cast<float>(
        (NSMidY(rect) - point.y) / (rect.size.height * 0.46f) * 1.6f),
        -1.5f, 1.5f);
}

- (void)drawMap:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    const NSRect rect = cartographyMapRect();
    [s3g::clap_gui::color(0x101010) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    [s3g::clap_gui::color(0x292929) setStroke];
    NSBezierPath* grid = [NSBezierPath bezierPath];
    for (int line = -3; line <= 3; ++line) {
        const CGFloat x = NSMidX(rect) + line * rect.size.width / 8.0;
        const CGFloat y = NSMidY(rect) + line * rect.size.height / 8.0;
        [grid moveToPoint:NSMakePoint(x, rect.origin.y)];
        [grid lineToPoint:NSMakePoint(x, NSMaxY(rect))];
        [grid moveToPoint:NSMakePoint(rect.origin.x, y)];
        [grid lineToPoint:NSMakePoint(NSMaxX(rect), y)];
    }
    [grid setLineWidth:0.55];
    [grid stroke];

    const auto params = _plugin->params;
    const auto& sites = _plugin->encoder.sites();
    float maximumArrival = 0.0001f;
    for (uint32_t site = 0u; site < params.activeSites; ++site) {
        maximumArrival = std::max(maximumArrival,
            _plugin->encoder.siteArrivalSeconds(site));
    }

    [s3g::clap_gui::color(0x4b4b4b, 0.75) setStroke];
    NSBezierPath* network = [NSBezierPath bezierPath];
    for (uint32_t site = 1u; site < params.activeSites; ++site) {
        const NSPoint previous = [self mapPointX:sites[site - 1u].x
            y:sites[site - 1u].y];
        const NSPoint current = [self mapPointX:sites[site].x y:sites[site].y];
        [network moveToPoint:previous];
        [network lineToPoint:current];
    }
    [network setLineWidth:1.0];
    [network stroke];

    for (uint32_t site = 0u; site < params.activeSites; ++site) {
        const auto& cartographySite = sites[site];
        const NSPoint point = [self mapPointX:cartographySite.x y:cartographySite.y];
        const float arrival = _plugin->encoder.siteArrivalSeconds(site);
        const float arrivalNorm = s3g::clamp(
            arrival / maximumArrival, 0.0f, 1.0f);
        const float level = s3g::clamp(
            _plugin->encoder.siteLevel(site) * 7.0f,
            0.0f, 1.0f);
        const CGFloat radius = 4.0 + level * 7.0;
        const int red = static_cast<int>(100.0f + arrivalNorm * 130.0f);
        const int green = static_cast<int>(190.0f - arrivalNorm * 80.0f);
        const int blue = static_cast<int>(220.0f - arrivalNorm * 120.0f);
        const int rgb = (red << 16) | (green << 8) | blue;
        [s3g::clap_gui::color(rgb,
            cartographySite.enabled ? 0.90 : 0.25) setFill];
        NSRect node = NSMakeRect(point.x - radius, point.y - radius,
            radius * 2.0, radius * 2.0);
        [[NSBezierPath bezierPathWithOvalInRect:node] fill];
        if (site == params.selectedSite) {
            [style.text setStroke];
            NSBezierPath* selection = [NSBezierPath bezierPathWithOvalInRect:
                NSInsetRect(node, -4.0, -4.0)];
            [selection setLineWidth:1.3];
            [selection stroke];
        }
        NSString* label = [NSString stringWithFormat:@"%u", site + 1u];
        [label drawAtPoint:NSMakePoint(point.x + radius + 3.0,
            point.y - 6.0) withAttributes:values];
    }

    const NSPoint listener = [self mapPointX:params.listenerX y:params.listenerY];
    [s3g::clap_gui::color(0xf0d35d) setStroke];
    NSBezierPath* listenerMark = [NSBezierPath bezierPath];
    [listenerMark moveToPoint:NSMakePoint(listener.x - 10.0, listener.y)];
    [listenerMark lineToPoint:NSMakePoint(listener.x + 10.0, listener.y)];
    [listenerMark moveToPoint:NSMakePoint(listener.x, listener.y - 10.0)];
    [listenerMark lineToPoint:NSMakePoint(listener.x, listener.y + 10.0)];
    [listenerMark setLineWidth:1.8];
    [listenerMark stroke];

    NSString* status = [NSString stringWithFormat:
        @"%@ / %.0f m per unit / %@ time",
        [NSString stringWithUTF8String:layoutName(
            static_cast<uint32_t>(params.layout))],
        params.mapScaleMeters,
        [NSString stringWithUTF8String:timeReferenceName(
            static_cast<uint32_t>(params.timeReference))]];
    [status drawAtPoint:NSMakePoint(rect.origin.x + 10.0,
        rect.origin.y + 9.0) withAttributes:labels];

    const uint32_t selected = params.selectedSite;
    NSString* selectedStatus = [NSString stringWithFormat:
        @"SITE %u   %.1f m   ARRIVAL %.3f s",
        selected + 1u, _plugin->encoder.siteDistanceMeters(selected),
        _plugin->encoder.siteArrivalSeconds(selected)];
    [selectedStatus drawAtPoint:NSMakePoint(rect.origin.x + 10.0,
        NSMaxY(rect) - 24.0) withAttributes:values];
}

- (void)drawControls:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    for (const auto& control : kGuiControls) {
        const uint32_t index = paramIndex(control.id);
        if (index >= kParamCount) continue;
        const auto& definition = kParams[index];
        const double value = publishedParamValue(*_plugin, control.id);
        char display[64] {};
        paramsValueToText(&_plugin->plugin, control.id, value,
            display, sizeof(display));
        const NSRect panel = panelRect(control.panel);
        const CGFloat y = controlY(control);
        NSString* label = [NSString stringWithUTF8String:control.label];
        NSString* text = [NSString stringWithUTF8String:display];
        if (control.kind == CartographyControlKind::Menu) {
            s3g::clap_gui::drawProcessorMenu(
                label, text, y, panel.origin.x, panel.size.width,
                labels, values, style);
        } else if (control.kind == CartographyControlKind::Toggle) {
            s3g::clap_gui::drawToggle(
                label, value >= 0.5, y, labels, values, style,
                panel.origin.x
                    + s3g::gui_layout::kStandardMetrics.labelInset,
                s3g::gui_layout::processorControlX(panel.origin.x),
                s3g::gui_layout::processorMenuWidth(panel.size.width));
        } else {
            const CGFloat norm = static_cast<CGFloat>((value
                - definition.minimum)
                / std::max(0.000001,
                    definition.maximum - definition.minimum));
            s3g::clap_gui::drawProcessorSlider(
                label, text, norm, y, panel.origin.x, panel.size.width,
                labels, values, style);
        }
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const auto style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g AMBI ENCODER CARTOGRAPHY",
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight),
        title, labels, values, style);

    s3g::clap_gui::drawPanelFrame(18.0, 42.0, 650.0, 696.0, style);
    s3g::clap_gui::drawPanelHeader(@"LOUDSPEAKER CARTOGRAPHY", true,
        18.0, 42.0, 650.0, 21.0, labels, style);
    const auto drawPanel = [&](NSString* name, CartographyPanel panelId) {
        const NSRect panel = panelRect(panelId);
        s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y,
            panel.size.width, panel.size.height, style);
        s3g::clap_gui::drawPanelHeader(name, true,
            panel.origin.x, panel.origin.y,
            panel.size.width, 21.0, labels, style);
    };
    drawPanel(@"OUTPUT", CartographyPanel::Output);
    drawPanel(@"CARTOGRAPHY", CartographyPanel::Cartography);
    drawPanel(@"PROPAGATION", CartographyPanel::Propagation);
    drawPanel(@"LISTENER", CartographyPanel::Listener);
    drawPanel(@"SITE PROCESS", CartographyPanel::Process);
    drawPanel(@"SELECTED SITE", CartographyPanel::SelectedSite);
    drawPanel(@"MAP GUIDE", CartographyPanel::Guide);
    [self drawMap:style labels:labels values:values];
    [self drawControls:style labels:labels values:values];
    const NSRect guide = panelRect(CartographyPanel::Guide);
    [@"DRAG +  LISTENER   DRAG NODE  SITE"
        drawAtPoint:NSMakePoint(guide.origin.x + 16.0, guide.origin.y + 38.0)
        withAttributes:values];
    [@"NODE SIZE  LIVE LEVEL"
        drawAtPoint:NSMakePoint(guide.origin.x + 16.0, guide.origin.y + 62.0)
        withAttributes:values];
    [@"COOL -> WARM  LATE ARRIVAL"
        drawAtPoint:NSMakePoint(guide.origin.x + 16.0, guide.origin.y + 86.0)
        withAttributes:values];
    [@"DOUBLE-CLICK SLIDER  DEFAULT"
        drawAtPoint:NSMakePoint(guide.origin.x + 16.0, guide.origin.y + 110.0)
        withAttributes:values];
    [@"FACTORY PRESETS PRESERVE ORDER + OUT"
        drawAtPoint:NSMakePoint(guide.origin.x + 16.0, guide.origin.y + 134.0)
        withAttributes:labels];
    [self drawOpenMenu:values style:style];
}

- (void)setContinuousParam:(clap_id)param point:(NSPoint)point
{
    const uint32_t index = paramIndex(param);
    const auto* control = guiControl(param);
    if (index >= kParamCount || !control) return;
    const auto& definition = kParams[index];
    const NSRect panel = panelRect(control->panel);
    const CGFloat x = s3g::gui_layout::processorControlX(panel.origin.x);
    const CGFloat width =
        s3g::gui_layout::processorTrackWidth(panel.size.width);
    const double norm = std::clamp(
        static_cast<double>((point.x - x) / width), 0.0, 1.0);
    queueGuiParamValue(*_plugin, param, definition.minimum
        + norm * (definition.maximum - definition.minimum));
    [self setNeedsDisplay:YES];
}

- (void)applyMapDrag:(NSPoint)point
{
    float x = 0.0f;
    float y = 0.0f;
    [self mapCoordinatesFromPoint:point x:&x y:&y];
    if (_dragListener) {
        queueGuiParamValue(*_plugin, kListenerXParamId, x);
        queueGuiParamValue(*_plugin, kListenerYParamId, y);
    } else if (_dragSite >= 0) {
        queueGuiParamValue(*_plugin, kSiteXParamId, x);
        queueGuiParamValue(*_plugin, kSiteYParamId, y);
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];

    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, [self openMenuRect], 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                [self applyFactoryPreset:hit];
            } else {
                queueGuiParamGesture(*_plugin, _openMenu,
                    menuValue(_openMenu, static_cast<uint32_t>(hit)));
                _factoryPresetIndex = -1;
                std::snprintf(_presetName,
                    sizeof(_presetName), "%s", "CUSTOM");
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        [self setNeedsDisplay:YES];
        return;
    }

    const auto band = s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(band.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = kFactoryPresetCount;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(band.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &_plugin->plugin, @"Ambi Cartography Encoder",
                kOutputParamId, &name)) {
            _factoryPresetIndex = -1;
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(band.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &_plugin->plugin, @"Ambi Cartography Encoder", &name)) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(band.randomButton))) {
        const auto randomUnit = [] {
            return static_cast<double>(arc4random()) / 4294967295.0;
        };
        const auto set = [&](clap_id id, double value) {
            queueGuiParamGesture(*_plugin, id, value);
        };
        set(kSitesParamId, 6.0 + std::floor(randomUnit() * 19.0));
        set(kLayoutParamId,
            std::floor(randomUnit() * 5.0));
        set(kStereoMapParamId, std::floor(randomUnit() * 3.0));
        set(kTimeReferenceParamId, std::floor(randomUnit() * 2.0));
        set(kNetworkSpreadParamId,
            120.0 + randomUnit() * 1480.0);
        set(kMapScaleParamId,
            60.0 * std::pow(24.0, randomUnit()));
        set(kPropagationParamId, 0.25 + randomUnit() * 0.75);
        set(kAirParamId, 0.08 + randomUnit() * 0.58);
        set(kDistanceLossParamId, 0.20 + randomUnit() * 0.55);
        set(kCarryParamId, randomUnit() * 0.82);
        set(kTurbulenceParamId, randomUnit() * 0.38);
        set(kMacroEngineParamId,
            1.0 + std::floor(randomUnit() * 4.0));
        set(kMacroMetricParamId,
            std::floor(randomUnit() * 4.0));
        set(kMacroParamId, randomUnit());
        set(kColorParamId, randomUnit());
        set(kMemoryParamId, randomUnit());
        set(kSpreadParamId, randomUnit());
        set(kDeviationParamId, randomUnit() * 0.55);
        set(kSkewParamId, randomUnit() * 1.2 - 0.6);
        set(kCenterParamId, randomUnit());
        set(kProcessMixParamId, 0.25 + randomUnit() * 0.55);
        set(kListenModeParamId, std::floor(randomUnit() * 4.0));
        set(kListenerAmountParamId, randomUnit() * 0.72);
        set(kListenerXParamId, randomUnit() * 1.4 - 0.7);
        set(kListenerYParamId, randomUnit() * 1.4 - 0.7);
        _plugin->layoutResetPending.store(true, std::memory_order_release);
        requestGuiParamService(*_plugin);
        _factoryPresetIndex = -1;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "RANDOM");
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, cartographyMapRect())) {
        const auto params = _plugin->params;
        const auto& sites = _plugin->encoder.sites();
        CGFloat bestDistance = 14.0;
        NSInteger bestSite = -1;
        for (uint32_t site = 0u; site < params.activeSites; ++site) {
            const NSPoint sitePoint = [self mapPointX:sites[site].x
                y:sites[site].y];
            const CGFloat dx = point.x - sitePoint.x;
            const CGFloat dy = point.y - sitePoint.y;
            const CGFloat distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestSite = static_cast<NSInteger>(site);
            }
        }
        const NSPoint listener = [self mapPointX:params.listenerX
            y:params.listenerY];
        const CGFloat ldx = point.x - listener.x;
        const CGFloat ldy = point.y - listener.y;
        if (std::sqrt(ldx * ldx + ldy * ldy) < 15.0) {
            _dragListener = YES;
            queueGuiParamGestureBegin(*_plugin, kListenerXParamId);
            queueGuiParamGestureBegin(*_plugin, kListenerYParamId);
            [self applyMapDrag:point];
            _factoryPresetIndex = -1;
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            return;
        }
        if (bestSite >= 0) {
            queueGuiParamGesture(*_plugin, kSiteParamId,
                static_cast<double>(bestSite + 1));
            _dragSite = bestSite;
            queueGuiParamGestureBegin(*_plugin, kSiteXParamId);
            queueGuiParamGestureBegin(*_plugin, kSiteYParamId);
            [self applyMapDrag:point];
            _factoryPresetIndex = -1;
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
            return;
        }
    }

    for (const auto& control : kGuiControls) {
        if (!NSPointInRect(point, controlHitRect(control))) continue;
        const uint32_t index = paramIndex(control.id);
        if (index >= kParamCount) continue;
        const auto& definition = kParams[index];
        if (control.kind == CartographyControlKind::Menu) {
            _openMenu = control.id;
            _hoverMenuItem = -1;
            _menuItemCount = menuItemCount(control.id);
            [self setNeedsDisplay:YES];
            return;
        }
        double defaultValue = definition.defaultValue;
        if (s3g::clap_gui::sliderDoubleClickDefault(event,
                &_plugin->plugin, definition.id, &defaultValue)) {
            queueGuiParamGesture(*_plugin, definition.id, defaultValue);
        } else if (control.kind == CartographyControlKind::Toggle) {
            const bool current = publishedParamValue(
                *_plugin, definition.id) >= 0.5;
            queueGuiParamGesture(*_plugin,
                definition.id, current ? 0.0 : 1.0);
        } else {
            _dragParam = definition.id;
            queueGuiParamGestureBegin(*_plugin, _dragParam);
            [self setContinuousParam:_dragParam point:point];
        }
        _factoryPresetIndex = -1;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_dragListener || _dragSite >= 0) {
        [self applyMapDrag:point];
    } else if (_dragParam != CLAP_INVALID_ID) {
        [self setContinuousParam:_dragParam point:point];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam != CLAP_INVALID_ID) {
        queueGuiParamGestureEnd(*_plugin, _dragParam);
    }
    if (_dragListener) {
        queueGuiParamGestureEnd(*_plugin, kListenerXParamId);
        queueGuiParamGestureEnd(*_plugin, kListenerYParamId);
    }
    if (_dragSite >= 0) {
        queueGuiParamGestureEnd(*_plugin, kSiteXParamId);
        queueGuiParamGestureEnd(*_plugin, kSiteYParamId);
    }
    _dragParam = CLAP_INVALID_ID;
    _dragSite = -1;
    _dragListener = NO;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self openMenuRect], 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
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
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GAmbiCartographyEncoderView alloc]
        initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView),
            static_cast<uint32_t>(kGuiWidth),
            static_cast<uint32_t>(kGuiHeight),
            static_cast<uint32_t>(kGuiWidth), 360u)) {
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
    [static_cast<S3GAmbiCartographyEncoderView*>(instance->guiView)
        stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        instance->guiViewport, instance->guiView);
    instance->guiVisible = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, static_cast<uint32_t>(kGuiWidth),
        static_cast<uint32_t>(kGuiHeight), width, height);
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
        self(plugin)->guiViewport, static_cast<uint32_t>(kGuiWidth),
        static_cast<uint32_t>(kGuiHeight), width, height,
        static_cast<uint32_t>(kGuiWidth), 360u);
}
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) {
        return false;
    }
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
            instance->guiViewport, false)) {
        return false;
    }
    instance->guiVisible = true;
    [static_cast<S3GAmbiCartographyEncoderView*>(instance->guiView)
        startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GAmbiCartographyEncoderView*>(instance->guiView)
        stopRefreshTimer];
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

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
#endif
    return nullptr;
}

constexpr const char* features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-cartography-encoder-64",
    "s3g Ambi Encoder Cartography",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.6.0-pre",
    "Stereo-to-HOA loudspeaker cartography with network timing, outdoor propagation, macro site processing, and Listener Mode.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->encoder.prepare(instance->sampleRate);
    instance->encoder.setParams(instance->params);
    instance->params = instance->encoder.params();
    publishAllParams(*instance);
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
    instance->plugin.get_extension = getExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0
        ? create(host) : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId
            && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
