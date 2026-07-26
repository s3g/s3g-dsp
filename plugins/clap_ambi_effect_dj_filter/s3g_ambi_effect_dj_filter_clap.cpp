#include "s3g_ambi_effect_dj_filter.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/ambisonic.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#include <clap/ext/gui.h>
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

constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kStateVersion = 5u;
constexpr uint32_t kLegacyPickupCount = 12u;
constexpr uint32_t kGuiWidth = 820u;
constexpr uint32_t kGuiHeight = 640u;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamBody = 2,
    kParamTopology = 3,
    kParamFilter = 4,
    kParamResonance = 5,
    kParamTopologyAmount = 6,
    kParamRoamingRate = 7,
    kParamMix = 8,
    kParamOutput = 9,
    kParamMaskAmount = 10,
    kParamMaskAzimuth = 11,
    kParamMaskElevation = 12,
    kParamMaskWidth = 13,
    kParamMaskDry = 14,
    kParamSpread = 15,
    kParamDeviation = 16,
    kParamMaskCurve = 17,
    kParamPickupFilterFirst = 100,
    kParamPickupFilterLast = kParamPickupFilterFirst
        + s3g::kAmbiEffectDjFilterMaxPickups - 1u,
    kParamPickupResonanceFirst = 200,
    kParamPickupResonanceLast = kParamPickupResonanceFirst
        + s3g::kAmbiEffectDjFilterMaxPickups - 1u,
};

struct AmbiEffectDjFilterParamsV4 {
    s3g::AmbiEffectEngine engine = s3g::AmbiEffectEngine::DjFilter;
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float filter = 0.5f;
    float resonance = 0.12f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kLegacyPickupCount> pickupFilterTrim {};
    std::array<float, kLegacyPickupCount> pickupResonanceTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
    float delayTimeMs = 320.0f;
    float delayFeedback = 0.32f;
    float delayTone = 0.62f;
    std::array<float, kLegacyPickupCount> pickupDelayTimeTrim {};
    std::array<float, kLegacyPickupCount> pickupDelayFeedbackTrim {};
};

struct AmbiEffectDjFilterParamsV3 {
    s3g::AmbiEffectEngine engine = s3g::AmbiEffectEngine::DjFilter;
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float filter = 0.5f;
    float resonance = 0.12f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kLegacyPickupCount>
        pickupFilterTrim {};
    std::array<float, kLegacyPickupCount>
        pickupResonanceTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskDry = 1.0f;
    float delayTimeMs = 320.0f;
    float delayFeedback = 0.32f;
    float delayTone = 0.62f;
    std::array<float, kLegacyPickupCount>
        pickupDelayTimeTrim {};
    std::array<float, kLegacyPickupCount>
        pickupDelayFeedbackTrim {};
};

struct AmbiEffectDjFilterParamsV2 {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float filter = 0.5f;
    float resonance = 0.12f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kLegacyPickupCount>
        pickupFilterTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
};

struct AmbiEffectDjFilterParamsV1 {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float filter = 0.5f;
    float resonance = 0.12f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 1.0f;
    float outputGainDb = 0.0f;
};

bool pickupFilterParamIndex(clap_id id, uint32_t& index)
{
    if (id < kParamPickupFilterFirst || id > kParamPickupFilterLast) {
        return false;
    }
    index = static_cast<uint32_t>(id - kParamPickupFilterFirst);
    return true;
}

bool pickupResonanceParamIndex(clap_id id, uint32_t& index)
{
    if (id < kParamPickupResonanceFirst
        || id > kParamPickupResonanceLast) return false;
    index = static_cast<uint32_t>(id - kParamPickupResonanceFirst);
    return true;
}

bool isAmbiEffectParam(clap_id id)
{
    uint32_t pickup = 0u;
    return (id >= kParamOrder && id <= kParamMaskCurve)
        || pickupFilterParamIndex(id, pickup)
        || pickupResonanceParamIndex(id, pickup);
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    s3g::AmbiEffectDjFilterParams params {};
    s3g::AmbiEffectDjFilter processor {};
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>,
        s3g::kAmbiEffectDjFilterMaxPickups> nodeLevel {};
    std::array<std::atomic<float>,
        s3g::kAmbiEffectDjFilterMaxPickups> nodeWetMask {};
    std::atomic<uint32_t> resolvedBody {
        static_cast<uint32_t>(s3g::AmbiEffectBody::Icosa12) };
    std::atomic<float> roamingPhase { 0.0f };
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 35.0f;
    float guiViewElDeg = 34.0f;
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    char presetName[64] { "INIT" };
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(
        std::max(0.0, std::floor(value + 0.5)));
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    uint32_t pickup = 0u;
    if (pickupFilterParamIndex(id, pickup)) {
        plugin.params.pickupFilterTrim[pickup] = static_cast<float>(value);
        plugin.params = s3g::sanitizeAmbiEffectDjFilterParams(plugin.params);
        plugin.processor.setParams(plugin.params);
        return;
    }
    if (pickupResonanceParamIndex(id, pickup)) {
        plugin.params.pickupResonanceTrim[pickup] = static_cast<float>(value);
        plugin.params = s3g::sanitizeAmbiEffectDjFilterParams(plugin.params);
        plugin.processor.setParams(plugin.params);
        return;
    }
    switch (id) {
    case kParamOrder:
        plugin.params.order = std::clamp<uint32_t>(
            roundedUint(value), 1u, s3g::kAmbiEffectDjFilterMaxOrder);
        break;
    case kParamBody:
        plugin.params.body = static_cast<s3g::AmbiEffectBody>(
            std::min<uint32_t>(roundedUint(value), 3u));
        break;
    case kParamTopology:
        plugin.params.topology = static_cast<s3g::AmbiEffectTopology>(
            std::min<uint32_t>(roundedUint(value), 3u));
        break;
    case kParamFilter:
        plugin.params.filter = static_cast<float>(value);
        break;
    case kParamResonance:
        plugin.params.resonance = static_cast<float>(value);
        break;
    case kParamSpread:
        plugin.params.spread = static_cast<float>(value);
        break;
    case kParamDeviation:
        plugin.params.deviation = static_cast<float>(value);
        break;
    case kParamMaskCurve:
        plugin.params.maskCurve = static_cast<float>(value);
        break;
    case kParamTopologyAmount:
        plugin.params.topologyAmount = static_cast<float>(value);
        break;
    case kParamRoamingRate:
        plugin.params.roamingRateHz = static_cast<float>(value);
        break;
    case kParamMix:
        plugin.params.mix = static_cast<float>(value);
        break;
    case kParamOutput:
        plugin.params.outputGainDb = static_cast<float>(value);
        break;
    case kParamMaskAmount:
        plugin.params.maskAmount = static_cast<float>(value);
        break;
    case kParamMaskAzimuth:
        plugin.params.maskAzimuthDeg = static_cast<float>(value);
        break;
    case kParamMaskElevation:
        plugin.params.maskElevationDeg = static_cast<float>(value);
        break;
    case kParamMaskWidth:
        plugin.params.maskWidth = static_cast<float>(value);
        break;
    case kParamMaskDry:
        plugin.params.maskDry = static_cast<float>(value + 1.0);
        break;
    default:
        return;
    }
    plugin.params = s3g::sanitizeAmbiEffectDjFilterParams(plugin.params);
    plugin.processor.setParams(plugin.params);
}

double getParam(const Plugin& plugin, clap_id id)
{
    uint32_t pickup = 0u;
    if (pickupFilterParamIndex(id, pickup)) {
        return plugin.params.pickupFilterTrim[pickup];
    }
    if (pickupResonanceParamIndex(id, pickup)) {
        return plugin.params.pickupResonanceTrim[pickup];
    }
    switch (id) {
    case kParamOrder: return plugin.params.order;
    case kParamBody: return static_cast<uint32_t>(plugin.params.body);
    case kParamTopology:
        return static_cast<uint32_t>(plugin.params.topology);
    case kParamFilter: return plugin.params.filter;
    case kParamResonance: return plugin.params.resonance;
    case kParamSpread: return plugin.params.spread;
    case kParamDeviation: return plugin.params.deviation;
    case kParamMaskCurve: return plugin.params.maskCurve;
    case kParamTopologyAmount: return plugin.params.topologyAmount;
    case kParamRoamingRate: return plugin.params.roamingRateHz;
    case kParamMix: return plugin.params.mix;
    case kParamOutput: return plugin.params.outputGainDb;
    case kParamMaskAmount: return plugin.params.maskAmount;
    case kParamMaskAzimuth: return plugin.params.maskAzimuthDeg;
    case kParamMaskElevation: return plugin.params.maskElevationDeg;
    case kParamMaskWidth: return plugin.params.maskWidth;
    case kParamMaskDry: return plugin.params.maskDry - 1.0;
    default: return 0.0;
    }
}

bool init(const clap_plugin_t*) { return true; }

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
    p->params = s3g::sanitizeAmbiEffectDjFilterParams(p->params);
    p->processor.prepare(sampleRate);
    p->processor.setParams(p->params);
    p->processor.reset();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->processor.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    for (auto& level : p->nodeLevel) {
        level.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& wet : p->nodeWetMask) {
        wet.store(1.0f, std::memory_order_relaxed);
    }
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
        const auto* value =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, value->param_id, value->value);
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& plugin,
    const clap_audio_buffer_t& input,
    const clap_audio_buffer_t& output,
    uint32_t frames, Sample** in, Sample** out)
{
    s3g::clearAudioBuffer(output, frames);
    if (!in || !out) return CLAP_PROCESS_CONTINUE;
    const uint32_t inChannels = std::min<uint32_t>(
        input.channel_count, kChannels);
    const uint32_t outChannels = std::min<uint32_t>(
        output.channel_count, kChannels);
    if (inChannels == 0u || outChannels == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    plugin.processor.process(in, out, inChannels, outChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0u; ch < outChannels; ++ch) {
        if (!out[ch]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            peak = std::max(peak,
                static_cast<float>(std::fabs(out[ch][frame])));
        }
    }
    const float previous = plugin.outputPeak.load(std::memory_order_relaxed);
    plugin.outputPeak.store(std::max(previous * 0.90f, peak),
        std::memory_order_relaxed);
    for (uint32_t node = 0u;
        node < s3g::kAmbiEffectDjFilterMaxPickups; ++node) {
        plugin.nodeLevel[node].store(
            plugin.processor.nodeLevel(node), std::memory_order_relaxed);
        plugin.nodeWetMask[node].store(
            plugin.processor.nodeWetMask(node), std::memory_order_relaxed);
    }
    plugin.resolvedBody.store(
        static_cast<uint32_t>(plugin.processor.resolvedBody()),
        std::memory_order_relaxed);
    plugin.roamingPhase.store(
        plugin.processor.roamingPhase(), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(
    const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    if (process->audio_inputs_count == 0u
        || process->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    if (input.data32 && output.data32) {
        return processTyped<float>(*p, input, output,
            process->frames_count, input.data32, output.data32);
    }
    if (input.data64 && output.data64) {
        return processTyped<double>(*p, input, output,
            process->frames_count, input.data64, output.data64);
    }
    s3g::clearAudioBuffer(output, process->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "7OA ACN/SN3D In" : "7OA ACN/SN3D Out",
        sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return 17u + s3g::kAmbiEffectDjFilterMaxPickups * 2u;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Ambi Effect", sizeof(info->module));
    if (index >= 17u
        && index < 17u + s3g::kAmbiEffectDjFilterMaxPickups) {
        const uint32_t pickup = index - 17u;
        info->id = kParamPickupFilterFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups",
            sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name),
            "Pickup %02u filter trim", pickup + 1u);
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    }
    if (index >= 17u + s3g::kAmbiEffectDjFilterMaxPickups
        && index < 17u + s3g::kAmbiEffectDjFilterMaxPickups * 2u) {
        const uint32_t pickup = index - 17u
            - s3g::kAmbiEffectDjFilterMaxPickups;
        info->id = kParamPickupResonanceFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups",
            sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name),
            "Pickup %02u resonance trim", pickup + 1u);
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = 0.0;
        return true;
    }
    switch (index) {
    case 0:
        info->id = kParamOrder;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, "Ambisonic order", sizeof(info->name));
        info->min_value = 1.0; info->max_value = 7.0;
        info->default_value = 7.0; return true;
    case 1:
        info->id = kParamBody;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, "Auditory body", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 4.0;
        info->default_value = 0.0; return true;
    case 2:
        info->id = kParamTopology;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, "Topology", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 3.0;
        info->default_value = 0.0; return true;
    case 3:
        info->id = kParamFilter;
        std::strncpy(info->name, "DJ filter", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.5; return true;
    case 4:
        info->id = kParamResonance;
        std::strncpy(info->name, "Resonance", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.12; return true;
    case 5:
        info->id = kParamTopologyAmount;
        std::strncpy(info->name, "Topology amount", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.65; return true;
    case 6:
        info->id = kParamRoamingRate;
        std::strncpy(info->name, "Roaming rate", sizeof(info->name));
        info->min_value = 0.005; info->max_value = 2.0;
        info->default_value = 0.08; return true;
    case 7:
        info->id = kParamMix;
        std::strncpy(info->name, "Mix", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 1.0; return true;
    case 8:
        info->id = kParamOutput;
        std::strncpy(info->name, "Output gain", sizeof(info->name));
        info->min_value = -60.0; info->max_value = 12.0;
        info->default_value = 0.0; return true;
    case 9:
        info->id = kParamMaskAmount;
        std::strncpy(info->name, "Directional mask amount", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.0; return true;
    case 10:
        info->id = kParamMaskAzimuth;
        std::strncpy(info->name, "Directional mask azimuth", sizeof(info->name));
        info->min_value = -180.0; info->max_value = 180.0;
        info->default_value = 0.0; return true;
    case 11:
        info->id = kParamMaskElevation;
        std::strncpy(info->name, "Directional mask elevation", sizeof(info->name));
        info->min_value = -90.0; info->max_value = 90.0;
        info->default_value = 0.0; return true;
    case 12:
        info->id = kParamMaskWidth;
        std::strncpy(info->name, "Directional mask width", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.35; return true;
    case 13:
        info->id = kParamMaskDry;
        std::strncpy(info->name, "Directional mask dry attenuation", sizeof(info->name));
        info->min_value = -1.0; info->max_value = 0.0;
        info->default_value = 0.0; return true;
    case 14:
        info->id = kParamSpread;
        std::strncpy(info->name, "Pickup spread", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.0; return true;
    case 15:
        info->id = kParamDeviation;
        std::strncpy(info->name, "Pickup deviation", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.0; return true;
    case 16:
        info->id = kParamMaskCurve;
        std::strncpy(info->name, "Directional mask curve", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0;
        info->default_value = 0.5; return true;
    default:
        return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin,
    clap_id paramId, double* value)
{
    if (!value || !isAmbiEffectParam(paramId)) {
        return false;
    }
    *value = getParam(*self(plugin), paramId);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId,
    double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    uint32_t pickup = 0u;
    if (pickupFilterParamIndex(paramId, pickup)) {
        (void)pickup;
        if (value < -0.005) {
            std::snprintf(display, size, "LP %.0f%%", -value * 100.0);
        } else if (value > 0.005) {
            std::snprintf(display, size, "HP %.0f%%", value * 100.0);
        } else {
            std::snprintf(display, size, "OPEN");
        }
        return true;
    }
    if (pickupResonanceParamIndex(paramId, pickup)) {
        (void)pickup;
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
        return true;
    }
    switch (paramId) {
    case kParamOrder:
        std::snprintf(display, size, "%uOA", roundedUint(value));
        return true;
    case kParamBody:
        std::snprintf(display, size, "%s",
            s3g::ambiEffectBodyName(static_cast<s3g::AmbiEffectBody>(
                std::min<uint32_t>(roundedUint(value), 4u))));
        return true;
    case kParamTopology:
        std::snprintf(display, size, "%s",
            s3g::ambiEffectTopologyName(static_cast<s3g::AmbiEffectTopology>(
                std::min<uint32_t>(roundedUint(value), 3u))));
        return true;
    case kParamFilter: {
        const float position = s3g::clamp(static_cast<float>(value), 0.0f, 1.0f);
        if (position < 0.495f) {
            std::snprintf(display, size, "LP %.0f%%",
                static_cast<double>((0.5f - position) * 200.0f));
        } else if (position > 0.505f) {
            std::snprintf(display, size, "HP %.0f%%",
                static_cast<double>((position - 0.5f) * 200.0f));
        } else {
            std::snprintf(display, size, "OPEN");
        }
        return true;
    }
    case kParamResonance:
    case kParamSpread:
    case kParamDeviation:
    case kParamMaskCurve:
    case kParamTopologyAmount:
    case kParamMix:
    case kParamMaskAmount:
    case kParamMaskWidth:
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    case kParamMaskDry:
        if (value <= -0.995) std::snprintf(display, size, "-100%% FX ONLY");
        else std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    case kParamRoamingRate:
        std::snprintf(display, size, "%.3f Hz", value);
        return true;
    case kParamOutput:
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    case kParamMaskAzimuth:
    case kParamMaskElevation:
        std::snprintf(display, size, "%+.0f deg", value);
        return true;
    default:
        return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId,
    const char* display, double* value)
{
    if (!display || !value || !isAmbiEffectParam(paramId)) return false;
    uint32_t pickup = 0u;
    if (pickupFilterParamIndex(paramId, pickup)) {
        (void)pickup;
        if (std::strcmp(display, "OPEN") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strncmp(display, "LP ", 3u) == 0) {
            *value = -std::atof(display + 3u) * 0.01;
            return true;
        }
        if (std::strncmp(display, "HP ", 3u) == 0) {
            *value = std::atof(display + 3u) * 0.01;
            return true;
        }
        return false;
    }
    if (pickupResonanceParamIndex(paramId, pickup)) {
        (void)pickup;
        *value = std::atof(display) * 0.01;
        return true;
    }
    switch (paramId) {
    case kParamOrder:
        *value = std::atof(display);
        return true;
    case kParamBody:
        for (uint32_t index = 0u; index <= 4u; ++index) {
            const auto body = static_cast<s3g::AmbiEffectBody>(index);
            if (std::strcmp(display, s3g::ambiEffectBodyName(body)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    case kParamTopology:
        for (uint32_t index = 0u; index <= 3u; ++index) {
            const auto topology = static_cast<s3g::AmbiEffectTopology>(index);
            if (std::strcmp(
                    display, s3g::ambiEffectTopologyName(topology)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    case kParamFilter:
        if (std::strcmp(display, "OPEN") == 0) {
            *value = 0.5;
            return true;
        }
        if (std::strncmp(display, "LP ", 3u) == 0) {
            *value = 0.5 - std::atof(display + 3u) * 0.005;
            return true;
        }
        if (std::strncmp(display, "HP ", 3u) == 0) {
            *value = 0.5 + std::atof(display + 3u) * 0.005;
            return true;
        }
        return false;
    case kParamResonance:
    case kParamSpread:
    case kParamDeviation:
    case kParamMaskCurve:
    case kParamTopologyAmount:
    case kParamMix:
    case kParamMaskAmount:
    case kParamMaskWidth:
        *value = std::atof(display) * 0.01;
        return true;
    case kParamMaskDry:
        if (std::strcmp(display, "-100% FX ONLY") == 0
            || std::strcmp(display, "FX ONLY") == 0) {
            *value = -1.0;
        } else {
            *value = std::atof(display) * 0.01;
        }
        return true;
    case kParamRoamingRate:
    case kParamOutput:
    case kParamMaskAzimuth:
    case kParamMaskElevation:
        *value = std::atof(display);
        return true;
    default:
        return false;
    }
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* events, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), events);
}

const clap_plugin_params_t paramsExt {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush,
};

bool streamWriteAll(const clap_ostream_t* stream,
    const void* source, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    size_t position = 0u;
    while (position < size) {
        const int64_t wrote = stream->write(
            stream, bytes + position, size - position);
        if (wrote <= 0
            || static_cast<size_t>(wrote) > size - position) return false;
        position += static_cast<size_t>(wrote);
    }
    return true;
}

bool streamReadAll(const clap_istream_t* stream,
    void* destination, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(destination);
    size_t position = 0u;
    while (position < size) {
        const int64_t read = stream->read(
            stream, bytes + position, size - position);
        if (read <= 0
            || static_cast<size_t>(read) > size - position) return false;
        position += static_cast<size_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    const auto& current = p->params;
    return streamWriteAll(stream, &kStateVersion, sizeof(kStateVersion))
        && streamWriteAll(stream, &current, sizeof(current))
        && streamWriteAll(stream, &p->guiViewMode, sizeof(p->guiViewMode))
        && streamWriteAll(stream, &p->guiViewAzDeg, sizeof(p->guiViewAzDeg))
        && streamWriteAll(stream, &p->guiViewElDeg, sizeof(p->guiViewElDeg));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!streamReadAll(stream, &version, sizeof(version))) return false;
    s3g::AmbiEffectDjFilterParams loaded {};
    int32_t loadedViewMode = 2;
    float loadedViewAzDeg = 35.0f;
    float loadedViewElDeg = 34.0f;
    if (version == kStateVersion) {
        if (!streamReadAll(stream, &loaded, sizeof(loaded))) return false;
        if (!streamReadAll(stream, &loadedViewMode,
                sizeof(loadedViewMode))
            || !streamReadAll(stream, &loadedViewAzDeg,
                sizeof(loadedViewAzDeg))
            || !streamReadAll(stream, &loadedViewElDeg,
                sizeof(loadedViewElDeg))) return false;
    } else if (version == 4u) {
        AmbiEffectDjFilterParamsV4 old {};
        if (!streamReadAll(stream, &old, sizeof(old))) return false;
        loaded.engine = old.engine;
        loaded.order = old.order;
        loaded.body = old.body;
        loaded.topology = old.topology;
        loaded.filter = old.filter;
        loaded.resonance = old.resonance;
        loaded.spread = old.spread;
        loaded.deviation = old.deviation;
        loaded.topologyAmount = old.topologyAmount;
        loaded.roamingRateHz = old.roamingRateHz;
        loaded.mix = old.mix;
        loaded.outputGainDb = old.outputGainDb;
        std::copy(old.pickupFilterTrim.begin(), old.pickupFilterTrim.end(),
            loaded.pickupFilterTrim.begin());
        std::copy(old.pickupResonanceTrim.begin(), old.pickupResonanceTrim.end(),
            loaded.pickupResonanceTrim.begin());
        loaded.maskAmount = old.maskAmount;
        loaded.maskAzimuthDeg = old.maskAzimuthDeg;
        loaded.maskElevationDeg = old.maskElevationDeg;
        loaded.maskWidth = old.maskWidth;
        loaded.maskCurve = old.maskCurve;
        loaded.maskDry = old.maskDry;
        loaded.delayTimeMs = old.delayTimeMs;
        loaded.delayFeedback = old.delayFeedback;
        loaded.delayTone = old.delayTone;
        std::copy(old.pickupDelayTimeTrim.begin(), old.pickupDelayTimeTrim.end(),
            loaded.pickupDelayTimeTrim.begin());
        std::copy(old.pickupDelayFeedbackTrim.begin(), old.pickupDelayFeedbackTrim.end(),
            loaded.pickupDelayFeedbackTrim.begin());
        if (!streamReadAll(stream, &loadedViewMode,
                sizeof(loadedViewMode))
            || !streamReadAll(stream, &loadedViewAzDeg,
                sizeof(loadedViewAzDeg))
            || !streamReadAll(stream, &loadedViewElDeg,
                sizeof(loadedViewElDeg))) return false;
    } else if (version == 3u) {
        AmbiEffectDjFilterParamsV3 old {};
        if (!streamReadAll(stream, &old, sizeof(old))) return false;
        loaded.engine = old.engine;
        loaded.order = old.order;
        loaded.body = old.body;
        loaded.topology = old.topology;
        loaded.filter = old.filter;
        loaded.resonance = old.resonance;
        loaded.spread = old.spread;
        loaded.deviation = old.deviation;
        loaded.topologyAmount = old.topologyAmount;
        loaded.roamingRateHz = old.roamingRateHz;
        loaded.mix = old.mix;
        loaded.outputGainDb = old.outputGainDb;
        std::copy(old.pickupFilterTrim.begin(), old.pickupFilterTrim.end(),
            loaded.pickupFilterTrim.begin());
        std::copy(old.pickupResonanceTrim.begin(), old.pickupResonanceTrim.end(),
            loaded.pickupResonanceTrim.begin());
        loaded.maskAmount = old.maskAmount;
        loaded.maskAzimuthDeg = old.maskAzimuthDeg;
        loaded.maskElevationDeg = old.maskElevationDeg;
        loaded.maskWidth = old.maskWidth;
        loaded.maskDry = old.maskDry;
        loaded.delayTimeMs = old.delayTimeMs;
        loaded.delayFeedback = old.delayFeedback;
        loaded.delayTone = old.delayTone;
        std::copy(old.pickupDelayTimeTrim.begin(), old.pickupDelayTimeTrim.end(),
            loaded.pickupDelayTimeTrim.begin());
        std::copy(old.pickupDelayFeedbackTrim.begin(), old.pickupDelayFeedbackTrim.end(),
            loaded.pickupDelayFeedbackTrim.begin());
        if (!streamReadAll(stream, &loadedViewMode,
                sizeof(loadedViewMode))
            || !streamReadAll(stream, &loadedViewAzDeg,
                sizeof(loadedViewAzDeg))
            || !streamReadAll(stream, &loadedViewElDeg,
                sizeof(loadedViewElDeg))) return false;
    } else if (version == 2u) {
        AmbiEffectDjFilterParamsV2 old {};
        if (!streamReadAll(stream, &old, sizeof(old))) return false;
        loaded.order = old.order;
        loaded.body = old.body;
        loaded.topology = old.topology;
        loaded.filter = old.filter;
        loaded.resonance = old.resonance;
        loaded.topologyAmount = old.topologyAmount;
        loaded.roamingRateHz = old.roamingRateHz;
        loaded.mix = old.mix;
        loaded.outputGainDb = old.outputGainDb;
        std::copy(old.pickupFilterTrim.begin(), old.pickupFilterTrim.end(),
            loaded.pickupFilterTrim.begin());
        loaded.maskAmount = old.maskAmount;
        loaded.maskAzimuthDeg = old.maskAzimuthDeg;
        loaded.maskElevationDeg = old.maskElevationDeg;
        loaded.maskWidth = old.maskWidth;
    } else if (version == 1u) {
        AmbiEffectDjFilterParamsV1 old {};
        if (!streamReadAll(stream, &old, sizeof(old))) return false;
        loaded.order = old.order;
        loaded.body = old.body;
        loaded.topology = old.topology;
        loaded.filter = old.filter;
        loaded.resonance = old.resonance;
        loaded.topologyAmount = old.topologyAmount;
        loaded.roamingRateHz = old.roamingRateHz;
        loaded.mix = old.mix;
        loaded.outputGainDb = old.outputGainDb;
    } else {
        return false;
    }
    auto* p = self(plugin);
    p->params = s3g::sanitizeAmbiEffectDjFilterParams(loaded);
    p->guiViewMode = std::clamp<int32_t>(loadedViewMode, -1, 2);
    p->guiViewAzDeg = std::isfinite(loadedViewAzDeg)
        ? loadedViewAzDeg : 35.0f;
    p->guiViewElDeg = std::clamp(
        std::isfinite(loadedViewElDeg) ? loadedViewElDeg : 34.0f,
        -85.0f, 85.0f);
    p->processor.setParams(p->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

std::array<s3g::Vec3, s3g::kAmbiEffectDjFilterMaxPickups>
bodyDirections(s3g::AmbiEffectBody body)
{
    return s3g::ambiEffectBodyDirections(body);
}

} // namespace

#if defined(__APPLE__)
namespace {
constexpr const auto& kLayout = s3g::gui_layout::kTransformFamilyLayout;
constexpr s3g::gui_layout::Canvas kAmbiEffectCanvas { 820.0, 640.0 };
constexpr s3g::gui_layout::Rect kAmbiEffectFieldPanel {
    18.0, 42.0, 506.0, 580.0 };
constexpr s3g::gui_layout::Rect kAmbiEffectFieldPlot {
    34.0, 78.0, 474.0, 526.0 };
constexpr s3g::gui_layout::Panel kFilterPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    { 536.0, 134.0, 266.0, 184.0 }, 36.0, 26.0, 6u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Topology,
    { 536.0, 330.0, 266.0, 106.0 }, 36.0, 26.0, 3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Utility,
    { 536.0, 448.0, 266.0, 184.0 }, 36.0, 26.0, 6u,
};
constexpr CGFloat kBodyRadius = 150.0;

struct ProjectedBody {
    std::array<NSPoint, s3g::kAmbiEffectDjFilterMaxPickups> points {};
    std::array<float, s3g::kAmbiEffectDjFilterMaxPickups> depths {};
};

NSPoint projectBodyDirection(
    s3g::Vec3 direction, float azimuthDeg, float elevationDeg)
{
    const NSRect field = s3g::clap_gui::cocoaRect(kAmbiEffectFieldPlot);
    const float yaw = -azimuthDeg * s3g::kPi / 180.0f;
    const float elevation = elevationDeg * s3g::kPi / 180.0f;
    const float rotatedX = direction.x * std::cos(yaw)
        - direction.y * std::sin(yaw);
    const float rotatedY = direction.x * std::sin(yaw)
        + direction.y * std::cos(yaw);
    const float projectedY = rotatedY * std::cos(elevation)
        - direction.z * std::sin(elevation);
    return NSMakePoint(
        NSMidX(field) + rotatedX * kBodyRadius,
        field.origin.y + 190.0 - projectedY * kBodyRadius);
}

ProjectedBody projectBody(
    s3g::AmbiEffectBody body, float azimuthDeg, float elevationDeg)
{
    ProjectedBody result {};
    const auto directions = bodyDirections(body);
    const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
    for (uint32_t node = 0u; node < count; ++node) {
        result.points[node] = projectBodyDirection(
            directions[node], azimuthDeg, elevationDeg);
        const float yaw = -azimuthDeg * s3g::kPi / 180.0f;
        const float elevation = elevationDeg * s3g::kPi / 180.0f;
        const float rotatedY = directions[node].x * std::sin(yaw)
            + directions[node].y * std::cos(yaw);
        result.depths[node] = rotatedY * std::sin(elevation)
            + directions[node].z * std::cos(elevation);
    }
    return result;
}

NSRect pickupFilterAxisRect()
{
    const NSRect field = s3g::clap_gui::cocoaRect(kAmbiEffectFieldPlot);
    return NSMakeRect(field.origin.x + 42.0,
        field.origin.y + field.size.height - 132.0,
        field.size.width - 84.0, 16.0);
}

NSRect pickupResonanceAxisRect()
{
    const NSRect field = s3g::clap_gui::cocoaRect(kAmbiEffectFieldPlot);
    return NSMakeRect(field.origin.x + 42.0,
        field.origin.y + field.size.height - 66.0,
        field.size.width - 84.0, 16.0);
}
}

@interface S3GAmbiEffectDjFilterView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItems;
    uint32_t _selectedPickup;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)updateSliderAtPoint:(NSPoint)point;
- (void)updateMenuHover:(NSPoint)point;
- (void)storeViewState;
- (void)setViewPreset:(int)mode;
@end

@implementation S3GAmbiEffectDjFilterView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _menuItems = 0u;
        _selectedPickup = 0u;
        auto* p = static_cast<Plugin*>(plugin);
        _viewMode = p ? p->guiViewMode : 2;
        _viewAzDeg = p ? p->guiViewAzDeg : 35.0;
        _viewElDeg = p ? p->guiViewElDeg : 34.0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _refreshTimer = nil;
    }
    return self;
}

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)storeViewState
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    p->guiViewMode = _viewMode;
    p->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    p->guiViewElDeg = static_cast<float>(_viewElDeg);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 35.0;
        _viewElDeg = 34.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 24.0)
        target:self selector:@selector(refreshTimerFired:)
        userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer
        forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_refreshTimer) {
        [_refreshTimer invalidate];
        _refreshTimer = nil;
    }
}

- (void)refreshTimerFired:(NSTimer*)timer
{
    (void)timer;
    if (_plugin && ![self isHidden]
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}

- (void)drawSlider:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm panel:(const s3g::gui_layout::Panel&)panel
    row:(uint32_t)row attrs:(NSDictionary*)attrs
    style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorSlider(name, value, norm,
        s3g::gui_layout::rowY(panel, row), panel.frame.x,
        panel.frame.width, attrs, attrs, style);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value
    panel:(const s3g::gui_layout::Panel&)panel row:(uint32_t)row
    attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(name, value,
        s3g::gui_layout::rowY(panel, row), panel.frame.x,
        panel.frame.width, attrs, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    NSDictionary* value = s3g::clap_gui::softValueAttrs();
    const float peak = p->outputPeak.exchange(
        p->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    const auto resolved = static_cast<s3g::AmbiEffectBody>(
        p->resolvedBody.load(std::memory_order_relaxed));
    NSString* status = [NSString stringWithFormat:@"%uOA · %s · %@",
        p->params.order, s3g::ambiEffectBodyName(resolved),
        s3g::clap_gui::peakDbText(peak)];
    s3g::clap_gui::drawTransformTitleBand(
        @"s3g AMBI EFFECT DJ FILTER 64CH",
        [NSString stringWithUTF8String:p->presetName], status,
        s3g::gui_layout::transformTitleBand(kAmbiEffectCanvas), style);

    NSRect fieldPanel = s3g::clap_gui::cocoaRect(kAmbiEffectFieldPanel);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x,
        fieldPanel.origin.y, fieldPanel.size.width,
        fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(
        @"LISTENER PICKUPS / FILTER FIELD", true,
        fieldPanel.origin.x, fieldPanel.origin.y,
        fieldPanel.size.width, 21.0, text, style);
    s3g::clap_gui::drawTopologyProcessorCameraButtons(
        fieldPanel, _viewMode, text, style);
    NSRect field = s3g::clap_gui::cocoaRect(kAmbiEffectFieldPlot);
    [s3g::clap_gui::color(0x0d0f0f) setFill];
    NSRectFill(field);
    [style.grid setStroke];
    NSFrameRect(field);

    const uint32_t bodyCount = s3g::ambiEffectBodyPickupCount(resolved);
    if (_selectedPickup >= bodyCount) _selectedPickup = 0u;
    const auto directions = bodyDirections(resolved);
    const ProjectedBody projected = projectBody(resolved,
        static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
    const auto& points = projected.points;
    const auto& depths = projected.depths;

    float nearestDot = -2.0f;
    for (uint32_t a = 0u; a < bodyCount; ++a) {
        for (uint32_t b = a + 1u; b < bodyCount; ++b) {
            nearestDot = std::max(nearestDot,
                directions[a].x * directions[b].x
                + directions[a].y * directions[b].y
                + directions[a].z * directions[b].z);
        }
    }
    NSBezierPath* shell = [NSBezierPath bezierPath];
    for (uint32_t a = 0u; a < bodyCount; ++a) {
        for (uint32_t b = a + 1u; b < bodyCount; ++b) {
            const float relation = directions[a].x * directions[b].x
                + directions[a].y * directions[b].y
                + directions[a].z * directions[b].z;
            if (relation < nearestDot - 0.0001f) continue;
            [shell moveToPoint:points[a]];
            [shell lineToPoint:points[b]];
        }
    }
    [s3g::clap_gui::color(0x707674, 0.34) setStroke];
    [shell setLineWidth:1.0];
    [shell stroke];

    NSBezierPath* heardRoute = [NSBezierPath bezierPath];
    if (p->params.topology == s3g::AmbiEffectTopology::Cross) {
        uint32_t opposite = 0u;
        float minimum = 2.0f;
        for (uint32_t other = 0u; other < bodyCount; ++other) {
            const float relation = directions[_selectedPickup].x * directions[other].x
                + directions[_selectedPickup].y * directions[other].y
                + directions[_selectedPickup].z * directions[other].z;
            if (relation < minimum) { minimum = relation; opposite = other; }
        }
        [heardRoute moveToPoint:points[_selectedPickup]];
        [heardRoute lineToPoint:points[opposite]];
    } else if (p->params.topology == s3g::AmbiEffectTopology::Diffuse) {
        for (uint32_t other = 0u; other < bodyCount; ++other) {
            const float relation = directions[_selectedPickup].x * directions[other].x
                + directions[_selectedPickup].y * directions[other].y
                + directions[_selectedPickup].z * directions[other].z;
            if (other == _selectedPickup || relation < nearestDot - 0.0001f) continue;
            [heardRoute moveToPoint:points[_selectedPickup]];
            [heardRoute lineToPoint:points[other]];
        }
    } else {
        const CGFloat orbit = p->params.topology == s3g::AmbiEffectTopology::Roaming
            ? 14.0 + 4.0 * std::sin(p->roamingPhase.load(
                std::memory_order_relaxed) * 2.0f * s3g::kPi) : 11.0;
        [heardRoute appendBezierPathWithOvalInRect:NSMakeRect(
            points[_selectedPickup].x - orbit,
            points[_selectedPickup].y - orbit, orbit * 2.0, orbit * 2.0)];
    }
    [s3g::clap_gui::color(0xd0d4d2,
        0.24 + p->params.topologyAmount * 0.58) setStroke];
    [heardRoute setLineWidth:1.5]; [heardRoute stroke];

    if (p->params.maskAmount > 0.001f) {
        const float azimuth = p->params.maskAzimuthDeg
            * s3g::kPi / 180.0f;
        const float elevation = p->params.maskElevationDeg
            * s3g::kPi / 180.0f;
        const float cosElevation = std::cos(elevation);
        const s3g::Vec3 direction {
            cosElevation * std::cos(azimuth),
            cosElevation * std::sin(azimuth),
            std::sin(elevation),
        };
        const NSPoint maskPoint = projectBodyDirection(direction,
            static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
        const CGFloat maskRadius = 16.0 + p->params.maskWidth * 38.0;
        [s3g::clap_gui::color(0xcbd0cd,
            0.20 + p->params.maskAmount * 0.46) setStroke];
        NSBezierPath* mask = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(maskPoint.x - maskRadius,
                maskPoint.y - maskRadius,
                maskRadius * 2.0, maskRadius * 2.0)];
        [mask setLineWidth:0.8 + p->params.maskCurve * 1.2];
        CGFloat dash[] { 4.0, 3.0 };
        [mask setLineDash:dash count:2 phase:0.0];
        [mask stroke];
        NSBezierPath* aim = [NSBezierPath bezierPath];
        [aim moveToPoint:NSMakePoint(maskPoint.x - 7.0, maskPoint.y)];
        [aim lineToPoint:NSMakePoint(maskPoint.x + 7.0, maskPoint.y)];
        [aim moveToPoint:NSMakePoint(maskPoint.x, maskPoint.y - 7.0)];
        [aim lineToPoint:NSMakePoint(maskPoint.x, maskPoint.y + 7.0)];
        [aim stroke];
    }

    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        for (uint32_t node = 0u; node < bodyCount; ++node) {
            if ((depths[node] >= 0.0f) != (pass == 1u)) continue;
            const float level = p->nodeLevel[node].load(std::memory_order_relaxed);
            const float levelDb = 20.0f * std::log10(
                std::max(0.000001f, level));
            const float meter = std::clamp(
                (levelDb + 60.0f) / 60.0f, 0.0f, 1.0f);
            const float wet = p->params.maskAmount < 0.005f
                ? 1.0f
                : p->nodeWetMask[node].load(std::memory_order_relaxed);
            const CGFloat size = 14.0;
            NSRect marker = NSMakeRect(points[node].x - size * 0.5,
                points[node].y - size * 0.5, size, size);
            const CGFloat halo = 8.0 + std::sqrt(meter) * 15.0;
            [s3g::clap_gui::color(
                pass == 1u ? 0xd8dcda : 0x858c89,
                0.025 + meter * 0.22) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                points[node].x - halo, points[node].y - halo,
                halo * 2.0, halo * 2.0)] fill];
            [s3g::clap_gui::color(0x161918, 0.94) setFill];
            NSRectFill(marker);
            [s3g::clap_gui::color(
                pass == 1u ? 0xd8dcda : 0x858c89,
                0.10 + meter * 0.28) setFill];
            NSRectFill(NSInsetRect(marker, 2.0, 2.0));
            [s3g::clap_gui::color(
                node == _selectedPickup ? 0xe2e5e3 : 0x8c9490,
                node == _selectedPickup ? 0.94 : 0.20 + wet * 0.66)
                setStroke];
            NSFrameRect(marker);
            const float trim = s3g::ambiEffectPickupFilterPosition(
                p->params.filter, p->params.pickupFilterTrim[node],
                p->params.spread, p->params.deviation, node, bodyCount);
            const CGFloat trimX = marker.origin.x
                + trim * marker.size.width;
            [s3g::clap_gui::color(0x363c39) setFill];
            NSRectFill(NSMakeRect(marker.origin.x,
                NSMaxY(marker) + 3.0, marker.size.width, 2.0));
            [s3g::clap_gui::color(0xd9dddb,
                node == _selectedPickup ? 1.0 : 0.72) setFill];
            NSRectFill(NSMakeRect(trimX - 1.0,
                NSMaxY(marker) + 1.0, 2.0, 6.0));
            const float resTrim = s3g::ambiEffectPickupResonance(
                p->params.resonance, p->params.pickupResonanceTrim[node],
                p->params.spread, p->params.deviation, node, bodyCount);
            const CGFloat resY = marker.origin.y
                + (1.0f - resTrim) * marker.size.height;
            [s3g::clap_gui::color(0xd9dddb,
                node == _selectedPickup ? 1.0 : 0.72) setFill];
            NSRectFill(NSMakeRect(marker.origin.x - 5.0,
                resY - 1.0, 6.0, 2.0));
            [[NSString stringWithFormat:@"%u", node + 1u]
                drawAtPoint:NSMakePoint(NSMaxX(marker) + 3.0,
                    marker.origin.y - 2.0)
                withAttributes:text];
        }
    }

    const NSRect filterAxis = pickupFilterAxisRect();
    const NSRect resonanceAxis = pickupResonanceAxisRect();
    const CGFloat axisX = filterAxis.origin.x;
    const CGFloat axisWidth = filterAxis.size.width;
    const float selectedTrim = p->params.pickupFilterTrim[_selectedPickup];
    const float selectedResTrim =
        p->params.pickupResonanceTrim[_selectedPickup];
    const float selectedLevel = p->nodeLevel[_selectedPickup].load(
        std::memory_order_relaxed);
    const float selectedDb = std::max(-120.0f,
        20.0f * std::log10(std::max(0.000001f, selectedLevel)));
    const float selectedWet = p->params.maskAmount < 0.005f
        ? 1.0f
        : p->nodeWetMask[_selectedPickup].load(std::memory_order_relaxed);
    const float effectiveFilter = s3g::ambiEffectPickupFilterPosition(
        p->params.filter, selectedTrim, p->params.spread,
        p->params.deviation, _selectedPickup, bodyCount);
    const float effectiveResonance = s3g::ambiEffectPickupResonance(
        p->params.resonance, selectedResTrim, p->params.spread,
        p->params.deviation, _selectedPickup, bodyCount);
    [[NSString stringWithFormat:
        @"HEARD AT PICKUP %02u  ·  IN %.0f dB  ·  WET %.0f%%  ·  F %.0f%%  R %.0f%%",
        _selectedPickup + 1u, selectedDb, selectedWet * 100.0f,
        effectiveFilter * 100.0f, effectiveResonance * 100.0f]
        drawAtPoint:NSMakePoint(axisX, filterAxis.origin.y - 22.0)
        withAttributes:value];
    const auto drawPickupAxis = [&](NSRect axis, float trim,
        NSString* left, NSString* center, NSString* right) {
        [s3g::clap_gui::color(0x171a19) setFill];
        NSRectFill(axis);
        [style.grid setStroke];
        NSFrameRect(axis);
        [left drawAtPoint:NSMakePoint(axis.origin.x, NSMaxY(axis) + 5.0)
            withAttributes:value];
        [center drawAtPoint:NSMakePoint(NSMidX(axis) - 3.0,
            NSMaxY(axis) + 5.0) withAttributes:value];
        [right drawAtPoint:NSMakePoint(NSMaxX(axis) - 42.0,
            NSMaxY(axis) + 5.0) withAttributes:value];
        const CGFloat centerX = NSMidX(axis);
        const CGFloat cursorX = axis.origin.x
            + axis.size.width * (trim + 1.0f) * 0.5f;
        [s3g::clap_gui::color(0x343937) setFill];
        NSRectFill(NSMakeRect(centerX - 1.0, axis.origin.y,
            2.0, axis.size.height));
        [s3g::clap_gui::color(0xd9dddb) setFill];
        NSRectFill(NSMakeRect(cursorX - 2.0, axis.origin.y - 3.0,
            4.0, axis.size.height + 6.0));
    };
    drawPickupAxis(filterAxis, selectedTrim,
        @"LP TRIM", @"0", @"HP TRIM");
    drawPickupAxis(resonanceAxis, selectedResTrim,
        @"RES -", @"0", @"RES +");

    s3g::clap_gui::drawPanelFrame(kLayout.output, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD", true,
        kLayout.output, text, style);
    s3g::clap_gui::drawPanelFrame(kFilterPanel, style);
    s3g::clap_gui::drawPanelHeader(@"DJ FILTER / RELATIONSHIPS", true,
        kFilterPanel, text, style);
    s3g::clap_gui::drawPanelFrame(kTopologyPanel, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY", true,
        kTopologyPanel, text, style);
    s3g::clap_gui::drawPanelFrame(kMaskPanel, style);
    s3g::clap_gui::drawPanelHeader(@"DIRECTIONAL WET MASK", true,
        kMaskPanel, text, style);

    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB", p->params.outputGainDb]
        norm:(p->params.outputGainDb + 60.0) / 72.0
        panel:kLayout.output row:0u attrs:value style:style];
    [self drawMenu:@"ORDER"
        value:[NSString stringWithFormat:@"%uOA", p->params.order]
        panel:kLayout.output row:1u attrs:value style:style];
    NSString* bodyText = p->params.body == s3g::AmbiEffectBody::Auto
        ? [NSString stringWithFormat:@"AUTO → %s",
            s3g::ambiEffectBodyName(resolved)]
        : [NSString stringWithUTF8String:s3g::ambiEffectBodyName(p->params.body)];
    [self drawMenu:@"BODY" value:bodyText
        panel:kFilterPanel row:0u attrs:value style:style];
    NSString* filterText = nil;
    if (p->params.filter < 0.495f) {
        filterText = [NSString stringWithFormat:@"LP %.0f%%",
            (0.5f - p->params.filter) * 200.0f];
    } else if (p->params.filter > 0.505f) {
        filterText = [NSString stringWithFormat:@"HP %.0f%%",
            (p->params.filter - 0.5f) * 200.0f];
    } else filterText = @"OPEN";
    [self drawSlider:@"LP→OPEN→HP" value:filterText
        norm:p->params.filter panel:kFilterPanel row:1u
        attrs:value style:style];
    [self drawSlider:@"RES" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.resonance * 100.0f] norm:p->params.resonance
        panel:kFilterPanel row:2u attrs:value style:style];
    [self drawSlider:@"SPRD" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.spread * 100.0f] norm:p->params.spread
        panel:kFilterPanel row:3u attrs:value style:style];
    [self drawSlider:@"DEV" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.deviation * 100.0f] norm:p->params.deviation
        panel:kFilterPanel row:4u attrs:value style:style];
    [self drawSlider:@"MIX" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.mix * 100.0f] norm:p->params.mix
        panel:kFilterPanel row:5u attrs:value style:style];
    [self drawMenu:@"MODE"
        value:[NSString stringWithUTF8String:
            s3g::ambiEffectTopologyName(p->params.topology)]
        panel:kTopologyPanel row:0u attrs:value style:style];
    [self drawSlider:@"AMT" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.topologyAmount * 100.0f]
        norm:p->params.topologyAmount panel:kTopologyPanel row:1u
        attrs:value style:style];
    const double roamNorm = std::log(
        p->params.roamingRateHz / 0.005f) / std::log(2.0f / 0.005f);
    [self drawSlider:@"RATE"
        value:[NSString stringWithFormat:@"%.3f Hz", p->params.roamingRateHz]
        norm:roamNorm panel:kTopologyPanel row:2u
        attrs:value style:style];
    [self drawSlider:@"MASK"
        value:(p->params.maskAmount < 0.005f
            ? @"OFF"
            : [NSString stringWithFormat:@"%.0f%%",
                p->params.maskAmount * 100.0f])
        norm:p->params.maskAmount panel:kMaskPanel row:0u
        attrs:value style:style];
    [self drawSlider:@"AZ"
        value:[NSString stringWithFormat:@"%+.0f deg",
            p->params.maskAzimuthDeg]
        norm:(p->params.maskAzimuthDeg + 180.0f) / 360.0f
        panel:kMaskPanel row:1u attrs:value style:style];
    [self drawSlider:@"EL"
        value:[NSString stringWithFormat:@"%+.0f deg",
            p->params.maskElevationDeg]
        norm:(p->params.maskElevationDeg + 90.0f) / 180.0f
        panel:kMaskPanel row:2u attrs:value style:style];
    [self drawSlider:@"WIDTH"
        value:[NSString stringWithFormat:@"%.0f%%",
            p->params.maskWidth * 100.0f]
        norm:p->params.maskWidth panel:kMaskPanel row:3u
        attrs:value style:style];
    [self drawSlider:@"CURVE"
        value:[NSString stringWithFormat:@"%.0f%%",
            p->params.maskCurve * 100.0f]
        norm:p->params.maskCurve panel:kMaskPanel row:4u
        attrs:value style:style];
    [self drawSlider:@"DRY" value:(p->params.maskDry < 0.005f
        ? @"-100% FX ONLY"
        : [NSString stringWithFormat:@"%.0f%%",
            (p->params.maskDry - 1.0f) * 100.0f])
        norm:p->params.maskDry panel:kMaskPanel row:5u
        attrs:value style:style];

    if (_openMenu > 0 && _menuItems > 0u) {
        NSString* orderItems[] = {
            @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA"
        };
        NSString* bodyItems[] = {
            @"AUTO", @"ICOSA 12", @"DODECA 20"
        };
        NSString* topologyItems[] = {
            @"LOCAL", @"CROSS", @"DIFFUSE", @"ROAMING"
        };
        NSString** items = _openMenu == 1 ? orderItems
            : (_openMenu == 2 ? bodyItems : topologyItems);
        const int selected = _openMenu == 1
            ? static_cast<int>(p->params.order) - 1
            : (_openMenu == 2
                ? (p->params.body == s3g::AmbiEffectBody::Auto ? 0
                    : (p->params.body == s3g::AmbiEffectBody::Dodeca20 ? 2 : 1))
                : static_cast<int>(p->params.topology));
        const CGFloat width = s3g::gui_layout::processorMenuWidth(
            kLayout.output.frame.width);
        s3g::clap_gui::drawDropdownMenu(
            NSMakeRect(_menuOrigin.x, _menuOrigin.y,
                width, 18.0 * _menuItems),
            18.0, items, _menuItems, selected,
            _hoverMenuItem, value, style);
    }
}

- (void)updateSliderAtPoint:(NSPoint)point
{
    uint32_t pickup = 0u;
    if (_dragSlider >= 0 && pickupFilterParamIndex(
            static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = pickupFilterAxisRect();
        const double norm = std::clamp(
            (point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider)
            value:norm * 2.0 - 1.0];
        return;
    }
    if (_dragSlider >= 0 && pickupResonanceParamIndex(
            static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = pickupResonanceAxisRect();
        const double norm = std::clamp(
            (point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider)
            value:norm * 2.0 - 1.0];
        return;
    }
    const double norm = std::clamp(
        (point.x - s3g::gui_layout::processorControlX(
            kLayout.output.frame.x))
            / s3g::gui_layout::processorTrackWidth(
                kLayout.output.frame.width),
        0.0, 1.0);
    switch (_dragSlider) {
    case kParamFilter: [self setParam:kParamFilter value:norm]; break;
    case kParamResonance: [self setParam:kParamResonance value:norm]; break;
    case kParamSpread: [self setParam:kParamSpread value:norm]; break;
    case kParamDeviation: [self setParam:kParamDeviation value:norm]; break;
    case kParamMaskCurve: [self setParam:kParamMaskCurve value:norm]; break;
    case kParamTopologyAmount:
        [self setParam:kParamTopologyAmount value:norm]; break;
    case kParamRoamingRate:
        [self setParam:kParamRoamingRate
            value:0.005 * std::pow(2.0 / 0.005, norm)]; break;
    case kParamMix: [self setParam:kParamMix value:norm]; break;
    case kParamOutput:
        [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    case kParamMaskAmount:
        [self setParam:kParamMaskAmount value:norm]; break;
    case kParamMaskAzimuth:
        [self setParam:kParamMaskAzimuth value:-180.0 + norm * 360.0]; break;
    case kParamMaskElevation:
        [self setParam:kParamMaskElevation value:-90.0 + norm * 180.0]; break;
    case kParamMaskWidth:
        [self setParam:kParamMaskWidth value:norm]; break;
    case kParamMaskDry:
        [self setParam:kParamMaskDry value:norm - 1.0]; break;
    default: break;
    }
}

- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItems == 0u) return;
    const CGFloat width = s3g::gui_layout::processorMenuWidth(
        kLayout.output.frame.width);
    const int hover = s3g::clap_gui::dropdownHitIndex(point,
        NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            width, 18.0 * _menuItems),
        18.0, _menuItems);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (s3g::clap_gui::handleProcessorTitleClick(point, &p->plugin,
        @"Ambi Effect DJ Filter",
        s3g::gui_layout::transformTitleBand(kAmbiEffectCanvas),
        p->presetName, sizeof(p->presetName))) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu > 0) {
        const CGFloat width = s3g::gui_layout::processorMenuWidth(
            kLayout.output.frame.width);
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            NSMakeRect(_menuOrigin.x, _menuOrigin.y,
                width, 18.0 * _menuItems),
            18.0, _menuItems);
        if (hit >= 0) {
            if (_openMenu == 1) [self setParam:kParamOrder value:hit + 1.0];
            else if (_openMenu == 2) [self setParam:kParamBody
                value:(hit == 0 ? 0.0 : (hit == 1 ? 3.0 : 4.0))];
            else [self setParam:kParamTopology value:hit];
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(
        kAmbiEffectFieldPanel);
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (NSPointInRect(point,
            s3g::clap_gui::topologyProcessorCameraButtonRect(
                fieldPanel, index))) {
            [self setViewPreset:static_cast<int>(index)];
            return;
        }
    }

    const auto resolved = s3g::resolveAmbiEffectBody(
        p->params.body, p->params.order);
    const uint32_t bodyCount = s3g::ambiEffectBodyPickupCount(resolved);
    const ProjectedBody projected = projectBody(resolved,
        static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
    for (uint32_t node = 0u; node < bodyCount; ++node) {
        const CGFloat dx = point.x - projected.points[node].x;
        const CGFloat dy = point.y - projected.points[node].y;
        if (dx * dx + dy * dy > 15.0 * 15.0) continue;
        _selectedPickup = node;
        if ([event clickCount] >= 2) {
            [self setParam:kParamPickupFilterFirst + node value:0.0];
            [self setParam:kParamPickupResonanceFirst + node value:0.0];
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
        NSInsetRect(pickupFilterAxisRect(), 0.0, -8.0))) {
        const clap_id pickupParam = kParamPickupFilterFirst + _selectedPickup;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &p->plugin, pickupParam, &defaultValue)) {
            [self setParam:pickupParam value:defaultValue];
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(pickupParam);
            [self updateSliderAtPoint:point];
        }
        return;
    }
    if (NSPointInRect(point,
        NSInsetRect(pickupResonanceAxisRect(), 0.0, -8.0))) {
        const clap_id pickupParam =
            kParamPickupResonanceFirst + _selectedPickup;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &p->plugin, pickupParam, &defaultValue)) {
            [self setParam:pickupParam value:defaultValue];
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(pickupParam);
            [self updateSliderAtPoint:point];
        }
        return;
    }

    struct MenuHit {
        s3g::gui_layout::Rect rect;
        int menu;
        uint32_t items;
    };
    const MenuHit menus[] {
        { s3g::gui_layout::sliderHitRect(kLayout.output, 1u), 1, 7u },
        { s3g::gui_layout::sliderHitRect(kFilterPanel, 0u), 2, 3u },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 0u), 3, 4u },
    };
    for (const auto& menu : menus) {
        if (!NSPointInRect(point,
            s3g::clap_gui::cocoaRect(menu.rect))) continue;
        _openMenu = menu.menu;
        _menuItems = menu.items;
        _menuOrigin = NSMakePoint(
            s3g::gui_layout::processorControlX(
                kLayout.output.frame.x),
            menu.rect.y + 24.0);
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    struct SliderHit {
        s3g::gui_layout::Rect rect;
        clap_id param;
    };
    const SliderHit sliders[] {
        { s3g::gui_layout::sliderHitRect(kLayout.output, 0u), kParamOutput },
        { s3g::gui_layout::sliderHitRect(kFilterPanel, 1u), kParamFilter },
        { s3g::gui_layout::sliderHitRect(kFilterPanel, 2u), kParamResonance },
        { s3g::gui_layout::sliderHitRect(kFilterPanel, 3u), kParamSpread },
        { s3g::gui_layout::sliderHitRect(kFilterPanel, 4u), kParamDeviation },
        { s3g::gui_layout::sliderHitRect(kFilterPanel, 5u), kParamMix },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 1u), kParamTopologyAmount },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 2u), kParamRoamingRate },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 0u), kParamMaskAmount },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 1u), kParamMaskAzimuth },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 2u), kParamMaskElevation },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 3u), kParamMaskWidth },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 4u), kParamMaskCurve },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 5u), kParamMaskDry },
    };
    for (const auto& slider : sliders) {
        if (!NSPointInRect(point,
            s3g::clap_gui::cocoaRect(slider.rect))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &p->plugin, slider.param, &defaultValue)) {
            [self setParam:slider.param value:defaultValue];
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(slider.param);
            [self updateSliderAtPoint:point];
        }
        return;
    }
    if (NSPointInRect(point,
        s3g::clap_gui::cocoaRect(kAmbiEffectFieldPlot))) {
        _dragView = YES;
        _lastDragPoint = point;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:point];
    if (_dragView) {
        _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
        _viewElDeg = std::clamp(_viewElDeg
            + (point.y - _lastDragPoint.y) * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragSlider >= 0) [self updateSliderAtPoint:point];
}

- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:
        [self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
    _dragView = NO;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*,
    const char* api, bool isFloating)
{
    return !isFloating
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*,
    const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin,
    const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiEffectDjFilterView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiEffectDjFilterView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        p->guiViewport, p->guiView);
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
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
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
    p->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectDjFilterView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectDjFilterView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
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
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_FILTER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-effect-dj-filter-64",
    "s3g Ambi Effect DJ Filter 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Auditory-body DJ filter with per-pickup variation and directional residual masking for 1OA through 7OA ACN/SN3D fields.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::sanitizeAmbiEffectDjFilterParams(p->params);
    p->processor.setParams(p->params);
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
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
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
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
