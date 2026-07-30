#include "s3g_ambi_effect_delay.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

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

constexpr uint32_t kChannels = s3g::kAmbiEffectDelayMaxChannels;
constexpr uint32_t kStateVersion = 4u;
constexpr uint32_t kLegacyPickupCount = 12u;
constexpr uint32_t kPreviousPickupCount = 20u;
constexpr uint32_t kGuiWidth = 820u;
constexpr uint32_t kGuiHeight = 640u;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamBody,
    kParamTopology,
    kParamTime,
    kParamFeedback,
    kParamTone,
    kParamSpread,
    kParamDeviation,
    kParamTopologyAmount,
    kParamRoamingRate,
    kParamMix,
    kParamOutput,
    kParamMaskAmount,
    kParamMaskAzimuth,
    kParamMaskElevation,
    kParamMaskWidth,
    kParamMaskDry,
    kParamMaskCurve,
    kParamPickupTimeFirst = 100,
    kParamPickupTimeLast = kParamPickupTimeFirst
        + s3g::kAmbiEffectDelayMaxPickups - 1u,
    kParamPickupFeedbackFirst = 200,
    kParamPickupFeedbackLast = kParamPickupFeedbackFirst
        + s3g::kAmbiEffectDelayMaxPickups - 1u,
};

struct AmbiEffectDelayParamsV3 {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float timeMs = 320.0f;
    float feedback = 0.32f;
    float tone = 0.62f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.35f;
    float outputGainDb = 0.0f;
    std::array<float, kPreviousPickupCount> pickupTimeTrim {};
    std::array<float, kPreviousPickupCount> pickupFeedbackTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

struct AmbiEffectDelayParamsV2 {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float timeMs = 320.0f;
    float feedback = 0.32f;
    float tone = 0.62f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.35f;
    float outputGainDb = 0.0f;
    std::array<float, kLegacyPickupCount> pickupTimeTrim {};
    std::array<float, kLegacyPickupCount> pickupFeedbackTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

struct AmbiEffectDelayParamsV1 {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float timeMs = 320.0f;
    float feedback = 0.32f;
    float tone = 0.62f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.35f;
    float outputGainDb = 0.0f;
    std::array<float, kLegacyPickupCount> pickupTimeTrim {};
    std::array<float, kLegacyPickupCount> pickupFeedbackTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskDry = 1.0f;
};

bool pickupTimeIndex(clap_id id, uint32_t& index)
{
    if (id < kParamPickupTimeFirst || id > kParamPickupTimeLast) return false;
    index = static_cast<uint32_t>(id - kParamPickupTimeFirst);
    return true;
}

bool pickupFeedbackIndex(clap_id id, uint32_t& index)
{
    if (id < kParamPickupFeedbackFirst
        || id > kParamPickupFeedbackLast) return false;
    index = static_cast<uint32_t>(id - kParamPickupFeedbackFirst);
    return true;
}

bool isParam(clap_id id)
{
    uint32_t index = 0u;
    return (id >= kParamOrder && id <= kParamMaskCurve)
        || pickupTimeIndex(id, index) || pickupFeedbackIndex(id, index);
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    s3g::AmbiEffectDelayParams params {};
    s3g::AmbiEffectDelay processor {};
    double sampleRate = 48000.0;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kAmbiEffectDelayMaxPickups>
        nodeLevel {};
    std::array<std::atomic<float>, s3g::kAmbiEffectDelayMaxPickups>
        nodeWetMask {};
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
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

void applyParam(Plugin& p, clap_id id, double value)
{
    uint32_t pickup = 0u;
    if (pickupTimeIndex(id, pickup)) {
        p.params.pickupTimeTrim[pickup] = static_cast<float>(value);
    } else if (pickupFeedbackIndex(id, pickup)) {
        p.params.pickupFeedbackTrim[pickup] = static_cast<float>(value);
    } else switch (id) {
    case kParamOrder: p.params.order = roundedUint(value); break;
    case kParamBody: p.params.body = static_cast<s3g::AmbiEffectBody>(roundedUint(value)); break;
    case kParamTopology: p.params.topology = static_cast<s3g::AmbiEffectTopology>(roundedUint(value)); break;
    case kParamTime: p.params.timeMs = static_cast<float>(value); break;
    case kParamFeedback: p.params.feedback = static_cast<float>(value); break;
    case kParamTone: p.params.tone = static_cast<float>(value); break;
    case kParamSpread: p.params.spread = static_cast<float>(value); break;
    case kParamDeviation: p.params.deviation = static_cast<float>(value); break;
    case kParamTopologyAmount: p.params.topologyAmount = static_cast<float>(value); break;
    case kParamRoamingRate: p.params.roamingRateHz = static_cast<float>(value); break;
    case kParamMix: p.params.mix = static_cast<float>(value); break;
    case kParamOutput: p.params.outputGainDb = static_cast<float>(value); break;
    case kParamMaskAmount: p.params.maskAmount = static_cast<float>(value); break;
    case kParamMaskAzimuth: p.params.maskAzimuthDeg = static_cast<float>(value); break;
    case kParamMaskElevation: p.params.maskElevationDeg = static_cast<float>(value); break;
    case kParamMaskWidth: p.params.maskWidth = static_cast<float>(value); break;
    case kParamMaskDry: p.params.maskDry = static_cast<float>(value + 1.0); break;
    case kParamMaskCurve: p.params.maskCurve = static_cast<float>(value); break;
    default: return;
    }
    p.params = s3g::sanitizeAmbiEffectDelayParams(p.params);
    p.processor.setParams(p.params);
}

double getParam(const Plugin& p, clap_id id)
{
    uint32_t pickup = 0u;
    if (pickupTimeIndex(id, pickup)) return p.params.pickupTimeTrim[pickup];
    if (pickupFeedbackIndex(id, pickup)) return p.params.pickupFeedbackTrim[pickup];
    switch (id) {
    case kParamOrder: return p.params.order;
    case kParamBody: return static_cast<uint32_t>(p.params.body);
    case kParamTopology: return static_cast<uint32_t>(p.params.topology);
    case kParamTime: return p.params.timeMs;
    case kParamFeedback: return p.params.feedback;
    case kParamTone: return p.params.tone;
    case kParamSpread: return p.params.spread;
    case kParamDeviation: return p.params.deviation;
    case kParamTopologyAmount: return p.params.topologyAmount;
    case kParamRoamingRate: return p.params.roamingRateHz;
    case kParamMix: return p.params.mix;
    case kParamOutput: return p.params.outputGainDb;
    case kParamMaskAmount: return p.params.maskAmount;
    case kParamMaskAzimuth: return p.params.maskAzimuthDeg;
    case kParamMaskElevation: return p.params.maskElevationDeg;
    case kParamMaskWidth: return p.params.maskWidth;
    case kParamMaskDry: return p.params.maskDry - 1.0;
    case kParamMaskCurve: return p.params.maskCurve;
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
    p->sampleRate = std::max(1.0, sampleRate);
    p->params = s3g::sanitizeAmbiEffectDelayParams(p->params);
    p->processor.setParams(p->params);
    p->processor.prepare(sampleRate);
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
    for (auto& level : p->nodeLevel) level.store(0.0f, std::memory_order_relaxed);
    for (auto& wet : p->nodeWetMask) wet.store(1.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* events)
{
    if (!events) return;
    for (uint32_t i = 0u; i < events->size(events); ++i) {
        const auto* header = events->get(events, i);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        applyParam(p, event->param_id, event->value);
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& p,
    const clap_audio_buffer_t& input, const clap_audio_buffer_t& output,
    uint32_t frames, Sample** in, Sample** out)
{
    s3g::clearAudioBuffer(output, frames);
    if (!in || !out) return CLAP_PROCESS_CONTINUE;
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kChannels);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kChannels);
    p.processor.process(in, out, inChannels, outChannels, frames);
    float peak = 0.0f;
    for (uint32_t ch = 0u; ch < outChannels; ++ch) {
        if (!out[ch]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            peak = std::max(peak, static_cast<float>(std::fabs(out[ch][frame])));
        }
    }
    const float old = p.outputPeak.load(std::memory_order_relaxed);
    p.outputPeak.store(std::max(old * 0.90f, peak), std::memory_order_relaxed);
    for (uint32_t node = 0u; node < s3g::kAmbiEffectDelayMaxPickups; ++node) {
        p.nodeLevel[node].store(p.processor.nodeLevel(node), std::memory_order_relaxed);
        p.nodeWetMask[node].store(p.processor.nodeWetMask(node), std::memory_order_relaxed);
    }
    p.resolvedBody.store(static_cast<uint32_t>(p.processor.resolvedBody()),
        std::memory_order_relaxed);
    p.roamingPhase.store(p.processor.roamingPhase(),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    if (process->audio_inputs_count == 0u || process->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    if (input.data32 && output.data32) return processTyped<float>(
        *p, input, output, process->frames_count, input.data32, output.data32);
    if (input.data64 && output.data64) return processTyped<double>(
        *p, input, output, process->frames_count, input.data64, output.data64);
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
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*)
{
    return 18u + s3g::kAmbiEffectDelayMaxPickups * 2u;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Ambi Effect", sizeof(info->module));
    if (index >= 18u && index < 18u + s3g::kAmbiEffectDelayMaxPickups) {
        const uint32_t pickup = index - 18u;
        info->id = kParamPickupTimeFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups", sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name),
            "Pickup %02u time trim", pickup + 1u);
        info->min_value = -1.0; info->max_value = 1.0; info->default_value = 0.0;
        return true;
    }
    if (index >= 18u + s3g::kAmbiEffectDelayMaxPickups
        && index < 18u + s3g::kAmbiEffectDelayMaxPickups * 2u) {
        const uint32_t pickup = index - 18u - s3g::kAmbiEffectDelayMaxPickups;
        info->id = kParamPickupFeedbackFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups", sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name),
            "Pickup %02u feedback trim", pickup + 1u);
        info->min_value = -1.0; info->max_value = 1.0; info->default_value = 0.0;
        return true;
    }
    struct Spec { clap_id id; const char* name; double min; double max; double def; bool stepped; };
    static constexpr Spec specs[] {
        { kParamOrder, "Ambisonic order", 1.0, 7.0, 7.0, true },
        { kParamBody, "Auditory body", 0.0, 5.0, 0.0, true },
        { kParamTopology, "Topology", 0.0, 3.0, 0.0, true },
        { kParamTime, "Delay time", 5.0, 2000.0, 320.0, false },
        { kParamFeedback, "Feedback", 0.0, 0.88, 0.32, false },
        { kParamTone, "Feedback tone", 0.0, 1.0, 0.62, false },
        { kParamSpread, "Pickup spread", 0.0, 1.0, 0.0, false },
        { kParamDeviation, "Pickup deviation", 0.0, 1.0, 0.0, false },
        { kParamTopologyAmount, "Topology amount", 0.0, 1.0, 0.65, false },
        { kParamRoamingRate, "Roaming rate", 0.005, 2.0, 0.08, false },
        { kParamMix, "Mix", 0.0, 1.0, 0.35, false },
        { kParamOutput, "Output gain", -60.0, 12.0, 0.0, false },
        { kParamMaskAmount, "Directional mask amount", 0.0, 1.0, 0.0, false },
        { kParamMaskAzimuth, "Directional mask azimuth", -180.0, 180.0, 0.0, false },
        { kParamMaskElevation, "Directional mask elevation", -90.0, 90.0, 0.0, false },
        { kParamMaskWidth, "Directional mask width", 0.0, 1.0, 0.35, false },
        { kParamMaskDry, "Directional mask dry attenuation", -1.0, 0.0, 0.0, false },
        { kParamMaskCurve, "Directional mask curve", 0.0, 1.0, 0.5, false },
    };
    if (index >= std::size(specs)) return false;
    const auto& spec = specs[index];
    info->id = spec.id;
    if (spec.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, spec.name, sizeof(info->name));
    info->min_value = spec.min;
    info->max_value = spec.max;
    info->default_value = spec.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !isParam(id)) return false;
    *value = getParam(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id,
    double value, char* display, uint32_t size)
{
    if (!display || size == 0u || !isParam(id)) return false;
    uint32_t pickup = 0u;
    if (pickupTimeIndex(id, pickup)) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
        return true;
    }
    if (pickupFeedbackIndex(id, pickup)) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
        return true;
    }
    switch (id) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); return true;
    case kParamBody:
        std::snprintf(display, size, "%s", s3g::ambiEffectBodyName(
            static_cast<s3g::AmbiEffectBody>(std::min<uint32_t>(roundedUint(value), 5u))));
        return true;
    case kParamTopology:
        std::snprintf(display, size, "%s", s3g::ambiEffectTopologyName(
            static_cast<s3g::AmbiEffectTopology>(std::min<uint32_t>(roundedUint(value), 3u))));
        return true;
    case kParamTime: std::snprintf(display, size, "%.1f ms", value); return true;
    case kParamRoamingRate: std::snprintf(display, size, "%.3f Hz", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    case kParamMaskAzimuth:
    case kParamMaskElevation: std::snprintf(display, size, "%+.0f deg", value); return true;
    case kParamMaskDry:
        if (value <= -0.995) std::snprintf(display, size, "-100%% FX ONLY");
        else std::snprintf(display, size, "%.0f%%", value * 100.0);
        return true;
    default: std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !isParam(id)) return false;
    uint32_t pickup = 0u;
    if (pickupTimeIndex(id, pickup) || pickupFeedbackIndex(id, pickup)) {
        *value = std::atof(display) * 0.01;
        return true;
    }
    if (id == kParamBody) {
        for (uint32_t i = 0u; i <= 5u; ++i) {
            if (std::strcmp(display, s3g::ambiEffectBodyName(
                static_cast<s3g::AmbiEffectBody>(i))) == 0) {
                *value = i; return true;
            }
        }
        return false;
    }
    if (id == kParamTopology) {
        for (uint32_t i = 0u; i <= 3u; ++i) {
            if (std::strcmp(display, s3g::ambiEffectTopologyName(
                static_cast<s3g::AmbiEffectTopology>(i))) == 0) {
                *value = i; return true;
            }
        }
        return false;
    }
    const double parsed = std::atof(display);
    if (id == kParamMaskDry
        && (std::strcmp(display, "-100% FX ONLY") == 0
            || std::strcmp(display, "FX ONLY") == 0)) {
        *value = -1.0;
        return true;
    }
    switch (id) {
    case kParamFeedback: case kParamTone: case kParamSpread:
    case kParamDeviation: case kParamTopologyAmount: case kParamMix:
    case kParamMaskAmount: case kParamMaskWidth: case kParamMaskDry:
    case kParamMaskCurve:
        *value = parsed * 0.01; break;
    default: *value = parsed; break;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* events, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), events);
}
const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush,
};

bool writeAll(const clap_ostream_t* stream, const void* source, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    size_t position = 0u;
    while (position < size) {
        const int64_t wrote = stream->write(stream, bytes + position, size - position);
        if (wrote <= 0 || static_cast<size_t>(wrote) > size - position) return false;
        position += static_cast<size_t>(wrote);
    }
    return true;
}

bool readAll(const clap_istream_t* stream, void* destination, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(destination);
    size_t position = 0u;
    while (position < size) {
        const int64_t read = stream->read(stream, bytes + position, size - position);
        if (read <= 0 || static_cast<size_t>(read) > size - position) return false;
        position += static_cast<size_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    return writeAll(stream, &kStateVersion, sizeof(kStateVersion))
        && writeAll(stream, &p->params, sizeof(p->params))
        && writeAll(stream, &p->guiViewMode, sizeof(p->guiViewMode))
        && writeAll(stream, &p->guiViewAzDeg, sizeof(p->guiViewAzDeg))
        && writeAll(stream, &p->guiViewElDeg, sizeof(p->guiViewElDeg));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    s3g::AmbiEffectDelayParams params {};
    int32_t viewMode = 2;
    float azimuth = 35.0f;
    float elevation = 34.0f;
    if (!readAll(stream, &version, sizeof(version))) return false;
    if (version == kStateVersion) {
        if (!readAll(stream, &params, sizeof(params))) return false;
    } else if (version == 3u) {
        AmbiEffectDelayParamsV3 old {};
        if (!readAll(stream, &old, sizeof(old))) return false;
        params.order = old.order;
        params.body = old.body;
        params.topology = old.topology;
        params.timeMs = old.timeMs;
        params.feedback = old.feedback;
        params.tone = old.tone;
        params.spread = old.spread;
        params.deviation = old.deviation;
        params.topologyAmount = old.topologyAmount;
        params.roamingRateHz = old.roamingRateHz;
        params.mix = old.mix;
        params.outputGainDb = old.outputGainDb;
        std::copy(old.pickupTimeTrim.begin(), old.pickupTimeTrim.end(),
            params.pickupTimeTrim.begin());
        std::copy(old.pickupFeedbackTrim.begin(), old.pickupFeedbackTrim.end(),
            params.pickupFeedbackTrim.begin());
        params.maskAmount = old.maskAmount;
        params.maskAzimuthDeg = old.maskAzimuthDeg;
        params.maskElevationDeg = old.maskElevationDeg;
        params.maskWidth = old.maskWidth;
        params.maskCurve = old.maskCurve;
        params.maskDry = old.maskDry;
    } else if (version == 2u) {
        AmbiEffectDelayParamsV2 old {};
        if (!readAll(stream, &old, sizeof(old))) return false;
        params.order = old.order;
        params.body = old.body;
        params.topology = old.topology;
        params.timeMs = old.timeMs;
        params.feedback = old.feedback;
        params.tone = old.tone;
        params.spread = old.spread;
        params.deviation = old.deviation;
        params.topologyAmount = old.topologyAmount;
        params.roamingRateHz = old.roamingRateHz;
        params.mix = old.mix;
        params.outputGainDb = old.outputGainDb;
        std::copy(old.pickupTimeTrim.begin(), old.pickupTimeTrim.end(),
            params.pickupTimeTrim.begin());
        std::copy(old.pickupFeedbackTrim.begin(), old.pickupFeedbackTrim.end(),
            params.pickupFeedbackTrim.begin());
        params.maskAmount = old.maskAmount;
        params.maskAzimuthDeg = old.maskAzimuthDeg;
        params.maskElevationDeg = old.maskElevationDeg;
        params.maskWidth = old.maskWidth;
        params.maskCurve = old.maskCurve;
        params.maskDry = old.maskDry;
    } else if (version == 1u) {
        AmbiEffectDelayParamsV1 old {};
        if (!readAll(stream, &old, sizeof(old))) return false;
        params.order = old.order;
        params.body = old.body;
        params.topology = old.topology;
        params.timeMs = old.timeMs;
        params.feedback = old.feedback;
        params.tone = old.tone;
        params.spread = old.spread;
        params.deviation = old.deviation;
        params.topologyAmount = old.topologyAmount;
        params.roamingRateHz = old.roamingRateHz;
        params.mix = old.mix;
        params.outputGainDb = old.outputGainDb;
        std::copy(old.pickupTimeTrim.begin(), old.pickupTimeTrim.end(),
            params.pickupTimeTrim.begin());
        std::copy(old.pickupFeedbackTrim.begin(), old.pickupFeedbackTrim.end(),
            params.pickupFeedbackTrim.begin());
        params.maskAmount = old.maskAmount;
        params.maskAzimuthDeg = old.maskAzimuthDeg;
        params.maskElevationDeg = old.maskElevationDeg;
        params.maskWidth = old.maskWidth;
        params.maskDry = old.maskDry;
    } else {
        return false;
    }
    if (!readAll(stream, &viewMode, sizeof(viewMode))
        || !readAll(stream, &azimuth, sizeof(azimuth))
        || !readAll(stream, &elevation, sizeof(elevation))) return false;
    auto* p = self(plugin);
    p->params = s3g::sanitizeAmbiEffectDelayParams(params);
    p->guiViewMode = std::clamp<int32_t>(viewMode, -1, 2);
    p->guiViewAzDeg = std::isfinite(azimuth) ? azimuth : 35.0f;
    p->guiViewElDeg = std::clamp(std::isfinite(elevation) ? elevation : 34.0f,
        -85.0f, 85.0f);
    p->processor.setParams(p->params);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const double feedback = std::clamp(
        static_cast<double>(p->params.feedback) + 0.35, 0.0, 0.88);
    const double repeats = feedback > 0.001
        ? std::ceil(std::log(0.001) / std::log(feedback)) : 1.0;
    const double seconds = std::clamp(2.0 * repeats + 0.25, 0.25, 90.0);
    return static_cast<uint32_t>(std::ceil(seconds * p->sampleRate));
}
const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
namespace {
constexpr const auto& kLayout = s3g::gui_layout::kTransformFamilyLayout;
constexpr s3g::gui_layout::Canvas kCanvas { 820.0, 640.0 };
constexpr s3g::gui_layout::Rect kFieldPanel { 18.0, 42.0, 506.0, 580.0 };
constexpr s3g::gui_layout::Rect kFieldPlot { 34.0, 78.0, 474.0, 526.0 };
constexpr s3g::gui_layout::Panel kDelayPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Engine,
    { 536.0, 134.0, 266.0, 198.0 }, 34.0, 24.0, 7u,
};
constexpr s3g::gui_layout::Panel kTopologyPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Topology,
    { 536.0, 344.0, 266.0, 102.0 }, 34.0, 24.0, 3u,
};
constexpr s3g::gui_layout::Panel kMaskPanel {
    s3g::gui_layout::PluginClass::CompactUtility,
    s3g::gui_layout::PanelRole::Utility,
    { 536.0, 458.0, 266.0, 174.0 }, 34.0, 24.0, 6u,
};
constexpr CGFloat kBodyRadius = 150.0;

std::array<s3g::Vec3, s3g::kAmbiEffectDelayMaxPickups>
bodyDirections(s3g::AmbiEffectBody body)
{
    return s3g::ambiEffectBodyDirections(body);
}

struct ProjectedBody {
    std::array<NSPoint, s3g::kAmbiEffectDelayMaxPickups> points {};
    std::array<float, s3g::kAmbiEffectDelayMaxPickups> depths {};
};

NSPoint projectDirection(s3g::Vec3 direction, float azDeg, float elDeg)
{
    const NSRect field = s3g::clap_gui::cocoaRect(kFieldPlot);
    const float yaw = -azDeg * s3g::kPi / 180.0f;
    const float elevation = elDeg * s3g::kPi / 180.0f;
    const float x = direction.x * std::cos(yaw) - direction.y * std::sin(yaw);
    const float y = direction.x * std::sin(yaw) + direction.y * std::cos(yaw);
    const float projectedY = y * std::cos(elevation) - direction.z * std::sin(elevation);
    return NSMakePoint(NSMidX(field) + x * kBodyRadius,
        field.origin.y + 190.0 - projectedY * kBodyRadius);
}

ProjectedBody projectBody(s3g::AmbiEffectBody body, float azDeg, float elDeg)
{
    ProjectedBody result {};
    const auto directions = bodyDirections(body);
    const uint32_t count = s3g::ambiEffectBodyPickupCount(body);
    const float yaw = -azDeg * s3g::kPi / 180.0f;
    const float elevation = elDeg * s3g::kPi / 180.0f;
    for (uint32_t node = 0u; node < count; ++node) {
        result.points[node] = projectDirection(directions[node], azDeg, elDeg);
        const float y = directions[node].x * std::sin(yaw)
            + directions[node].y * std::cos(yaw);
        result.depths[node] = y * std::sin(elevation)
            + directions[node].z * std::cos(elevation);
    }
    return result;
}

NSRect timeAxisRect()
{
    const NSRect field = s3g::clap_gui::cocoaRect(kFieldPlot);
    return NSMakeRect(field.origin.x + 42.0,
        field.origin.y + field.size.height - 132.0,
        field.size.width - 84.0, 16.0);
}
NSRect feedbackAxisRect()
{
    const NSRect field = s3g::clap_gui::cocoaRect(kFieldPlot);
    return NSMakeRect(field.origin.x + 42.0,
        field.origin.y + field.size.height - 66.0,
        field.size.width - 84.0, 16.0);
}
} // namespace

@interface S3GAmbiEffectDelayView : NSView {
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
- (void)storeViewState;
- (void)setViewPreset:(int)mode;
@end

@implementation S3GAmbiEffectDelayView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1; _openMenu = 0; _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint; _menuItems = 0u; _selectedPickup = 0u;
        auto* p = static_cast<Plugin*>(plugin);
        _viewMode = p ? p->guiViewMode : 2;
        _viewAzDeg = p ? p->guiViewAzDeg : 35.0;
        _viewElDeg = p ? p->guiViewElDeg : 34.0;
        _dragView = NO; _lastDragPoint = NSZeroPoint; _refreshTimer = nil;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self storeViewState]; [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:1.0/24.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer
{
    if (_refreshTimer) { [_refreshTimer invalidate]; _refreshTimer = nil; }
}
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive())
        [self setNeedsDisplay:YES];
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
    if (mode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 90.0; _viewElDeg = 90.0; }
    else { _viewAzDeg = 35.0; _viewElDeg = 34.0; }
    [self storeViewState]; [self setNeedsDisplay:YES];
}
- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm
    panel:(const s3g::gui_layout::Panel&)panel row:(uint32_t)row
    attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorSlider(name, value, norm,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, attrs, style);
}
- (void)drawMenu:(NSString*)name value:(NSString*)value
    panel:(const s3g::gui_layout::Panel&)panel row:(uint32_t)row
    attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(name, value,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, attrs, style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
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
        @"s3g AMBI EFFECT DELAY 64CH",
        [NSString stringWithUTF8String:p->presetName], status,
        s3g::gui_layout::transformTitleBand(kCanvas), style);

    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(kFieldPanel);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
        fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"LISTENER PICKUPS / DELAY FIELD", true,
        fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width,
        21.0, text, style);
    s3g::clap_gui::drawTopologyProcessorCameraButtons(
        fieldPanel, _viewMode, text, style);
    const NSRect field = s3g::clap_gui::cocoaRect(kFieldPlot);
    [s3g::clap_gui::color(0x0d0f0f) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);

    const uint32_t bodyCount = s3g::ambiEffectBodyPickupCount(resolved);
    if (_selectedPickup >= bodyCount) _selectedPickup = 0u;
    const auto directions = bodyDirections(resolved);
    const auto projected = projectBody(resolved,
        static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
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
            [shell moveToPoint:projected.points[a]];
            [shell lineToPoint:projected.points[b]];
        }
    }
    [s3g::clap_gui::color(0x707674, 0.34) setStroke];
    [shell setLineWidth:1.0]; [shell stroke];

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
        [heardRoute moveToPoint:projected.points[_selectedPickup]];
        [heardRoute lineToPoint:projected.points[opposite]];
    } else if (p->params.topology == s3g::AmbiEffectTopology::Diffuse) {
        for (uint32_t other = 0u; other < bodyCount; ++other) {
            const float relation = directions[_selectedPickup].x * directions[other].x
                + directions[_selectedPickup].y * directions[other].y
                + directions[_selectedPickup].z * directions[other].z;
            if (other == _selectedPickup || relation < nearestDot - 0.0001f) continue;
            [heardRoute moveToPoint:projected.points[_selectedPickup]];
            [heardRoute lineToPoint:projected.points[other]];
        }
    } else {
        const CGFloat orbit = p->params.topology == s3g::AmbiEffectTopology::Roaming
            ? 14.0 + 4.0 * std::sin(p->roamingPhase.load(
                std::memory_order_relaxed) * 2.0f * s3g::kPi) : 11.0;
        [heardRoute appendBezierPathWithOvalInRect:NSMakeRect(
            projected.points[_selectedPickup].x - orbit,
            projected.points[_selectedPickup].y - orbit,
            orbit * 2.0, orbit * 2.0)];
    }
    [s3g::clap_gui::color(0xd0d4d2,
        0.24 + p->params.topologyAmount * 0.58) setStroke];
    [heardRoute setLineWidth:1.5]; [heardRoute stroke];

    if (p->params.maskAmount > 0.001f) {
        const float az = p->params.maskAzimuthDeg * s3g::kPi / 180.0f;
        const float el = p->params.maskElevationDeg * s3g::kPi / 180.0f;
        const s3g::Vec3 direction {
            std::cos(el) * std::cos(az), std::cos(el) * std::sin(az),
            std::sin(el),
        };
        const NSPoint point = projectDirection(direction,
            static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
        const CGFloat radius = 16.0 + p->params.maskWidth * 38.0;
        [s3g::clap_gui::color(0xcbd0cd,
            0.20 + p->params.maskAmount * 0.46) setStroke];
        NSBezierPath* mask = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(point.x - radius, point.y - radius,
                radius * 2.0, radius * 2.0)];
        [mask setLineWidth:0.8 + p->params.maskCurve * 1.2];
        CGFloat dash[] { 4.0, 3.0 };
        [mask setLineDash:dash count:2 phase:0.0]; [mask stroke];
    }

    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        for (uint32_t node = 0u; node < bodyCount; ++node) {
            if ((projected.depths[node] >= 0.0f) != (pass == 1u)) continue;
            const float level = p->nodeLevel[node].load(std::memory_order_relaxed);
            const float db = 20.0f * std::log10(std::max(0.000001f, level));
            const float meter = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            constexpr CGFloat size = 14.0;
            NSRect marker = NSMakeRect(projected.points[node].x - size * 0.5,
                projected.points[node].y - size * 0.5, size, size);
            const CGFloat halo = 8.0 + std::sqrt(meter) * 15.0;
            [s3g::clap_gui::color(pass ? 0xd8dcda : 0x858c89,
                0.025 + meter * 0.22) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                projected.points[node].x - halo,
                projected.points[node].y - halo,
                halo * 2.0, halo * 2.0)] fill];
            [s3g::clap_gui::color(pass ? 0xe9ecea : 0xb9bfbc,
                node == _selectedPickup ? 1.0 : (pass ? 0.94 : 0.76)) setFill];
            NSRectFill(marker);
            [s3g::clap_gui::color(pass ? 0x4a504d : 0x3d4240,
                0.94) setFill];
            NSRectFill(NSInsetRect(marker, 2.0, 2.0));
            [s3g::clap_gui::color(
                node == _selectedPickup ? 0xffffff
                    : (pass ? 0xe9ecea : 0xb9bfbc),
                node == _selectedPickup ? 1.0 : (pass ? 0.94 : 0.76)) setStroke];
            NSFrameRect(marker);
            const float effectiveTime = s3g::ambiEffectPickupDelayMs(
                p->params.timeMs, p->params.pickupTimeTrim[node],
                p->params.spread, p->params.deviation, node, bodyCount);
            const float timeTrim = std::log(effectiveTime / 5.0f)
                / std::log(2000.0f / 5.0f);
            const CGFloat timeX = marker.origin.x
                + timeTrim * marker.size.width;
            [s3g::clap_gui::color(0xd9dddb, 0.78) setFill];
            NSRectFill(NSMakeRect(timeX - 1.0, NSMaxY(marker) + 1.0, 2.0, 6.0));
            const float feedbackTrim = s3g::ambiEffectPickupDelayFeedback(
                p->params.feedback, p->params.pickupFeedbackTrim[node],
                p->params.spread, p->params.deviation, node, bodyCount)
                / 0.88f;
            const CGFloat feedbackY = marker.origin.y
                + (1.0f - feedbackTrim) * marker.size.height;
            NSRectFill(NSMakeRect(marker.origin.x - 5.0, feedbackY - 1.0, 6.0, 2.0));
            [[NSString stringWithFormat:@"%u", node + 1u]
                drawAtPoint:NSMakePoint(NSMaxX(marker) + 3.0,
                    marker.origin.y - 2.0) withAttributes:text];
        }
    }

    const NSRect timeAxis = timeAxisRect();
    const NSRect feedbackAxis = feedbackAxisRect();
    const float timeTrim = p->params.pickupTimeTrim[_selectedPickup];
    const float feedbackTrim = p->params.pickupFeedbackTrim[_selectedPickup];
    const float effectiveTime = s3g::ambiEffectPickupDelayMs(
        p->params.timeMs, timeTrim, p->params.spread, p->params.deviation,
        _selectedPickup, bodyCount);
    const float effectiveFeedback = s3g::ambiEffectPickupDelayFeedback(
        p->params.feedback, feedbackTrim, p->params.spread,
        p->params.deviation, _selectedPickup, bodyCount);
    const float selectedLevel = p->nodeLevel[_selectedPickup].load(
        std::memory_order_relaxed);
    const float selectedDb = std::max(-120.0f,
        20.0f * std::log10(std::max(0.000001f, selectedLevel)));
    [[NSString stringWithFormat:
        @"HEARD AT PICKUP %02u  ·  IN %.0f dB  ·  %.1f ms  ·  FB %.0f%%",
        _selectedPickup + 1u, selectedDb, effectiveTime,
        effectiveFeedback * 100.0f]
        drawAtPoint:NSMakePoint(timeAxis.origin.x, timeAxis.origin.y - 22.0)
        withAttributes:value];
    const auto drawAxis = [&](NSRect axis, float trim,
        NSString* left, NSString* right) {
        [s3g::clap_gui::color(0x171a19) setFill]; NSRectFill(axis);
        [style.grid setStroke]; NSFrameRect(axis);
        [left drawAtPoint:NSMakePoint(axis.origin.x, NSMaxY(axis) + 5.0)
            withAttributes:value];
        [@"0" drawAtPoint:NSMakePoint(NSMidX(axis) - 3.0, NSMaxY(axis) + 5.0)
            withAttributes:value];
        [right drawAtPoint:NSMakePoint(NSMaxX(axis) - 46.0, NSMaxY(axis) + 5.0)
            withAttributes:value];
        const CGFloat centerX = NSMidX(axis);
        const CGFloat cursorX = axis.origin.x
            + axis.size.width * (trim + 1.0f) * 0.5f;
        [s3g::clap_gui::color(0x343937) setFill];
        NSRectFill(NSMakeRect(centerX - 1.0, axis.origin.y, 2.0, axis.size.height));
        [s3g::clap_gui::color(0xd9dddb) setFill];
        NSRectFill(NSMakeRect(cursorX - 2.0, axis.origin.y - 3.0,
            4.0, axis.size.height + 6.0));
    };
    drawAxis(timeAxis, timeTrim, @"TIME -", @"TIME +");
    drawAxis(feedbackAxis, feedbackTrim, @"FB -", @"FB +");

    s3g::clap_gui::drawPanelFrame(kLayout.output, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD", true, kLayout.output, text, style);
    s3g::clap_gui::drawPanelFrame(kDelayPanel, style);
    s3g::clap_gui::drawPanelHeader(@"DELAY / OLA / RELATIONSHIPS", true,
        kDelayPanel, text, style);
    s3g::clap_gui::drawPanelFrame(kTopologyPanel, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY", true, kTopologyPanel, text, style);
    s3g::clap_gui::drawPanelFrame(kMaskPanel, style);
    s3g::clap_gui::drawPanelHeader(@"DIRECTIONAL WET MASK", true,
        kMaskPanel, text, style);
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB",
        p->params.outputGainDb] norm:(p->params.outputGainDb + 60.0f) / 72.0f
        panel:kLayout.output row:0u attrs:value style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA",
        p->params.order] panel:kLayout.output row:1u attrs:value style:style];
    NSString* bodyText = p->params.body == s3g::AmbiEffectBody::Auto
        ? [NSString stringWithFormat:@"AUTO → %s", s3g::ambiEffectBodyName(resolved)]
        : [NSString stringWithUTF8String:s3g::ambiEffectBodyName(p->params.body)];
    [self drawMenu:@"BODY" value:bodyText panel:kDelayPanel row:0u attrs:value style:style];
    const double timeNorm = std::log(p->params.timeMs / 5.0f) / std::log(2000.0 / 5.0);
    [self drawSlider:@"TIME" value:[NSString stringWithFormat:@"%.1f ms",
        p->params.timeMs] norm:timeNorm panel:kDelayPanel row:1u attrs:value style:style];
    [self drawSlider:@"FEEDBACK" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.feedback * 100.0f] norm:p->params.feedback / 0.88f
        panel:kDelayPanel row:2u attrs:value style:style];
    [self drawSlider:@"TONE" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.tone * 100.0f] norm:p->params.tone panel:kDelayPanel row:3u attrs:value style:style];
    [self drawSlider:@"SPRD" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.spread * 100.0f] norm:p->params.spread panel:kDelayPanel row:4u attrs:value style:style];
    [self drawSlider:@"DEV" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.deviation * 100.0f] norm:p->params.deviation panel:kDelayPanel row:5u attrs:value style:style];
    [self drawSlider:@"MIX" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.mix * 100.0f] norm:p->params.mix panel:kDelayPanel row:6u attrs:value style:style];
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:
        s3g::ambiEffectTopologyName(p->params.topology)]
        panel:kTopologyPanel row:0u attrs:value style:style];
    [self drawSlider:@"AMT" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.topologyAmount * 100.0f] norm:p->params.topologyAmount
        panel:kTopologyPanel row:1u attrs:value style:style];
    const double rateNorm = std::log(p->params.roamingRateHz / 0.005f)
        / std::log(2.0 / 0.005);
    [self drawSlider:@"RATE" value:[NSString stringWithFormat:@"%.3f Hz",
        p->params.roamingRateHz] norm:rateNorm panel:kTopologyPanel row:2u attrs:value style:style];
    [self drawSlider:@"MASK" value:(p->params.maskAmount < 0.005f ? @"OFF"
        : [NSString stringWithFormat:@"%.0f%%", p->params.maskAmount * 100.0f])
        norm:p->params.maskAmount panel:kMaskPanel row:0u attrs:value style:style];
    [self drawSlider:@"AZ" value:[NSString stringWithFormat:@"%+.0f deg",
        p->params.maskAzimuthDeg] norm:(p->params.maskAzimuthDeg + 180.0f) / 360.0f
        panel:kMaskPanel row:1u attrs:value style:style];
    [self drawSlider:@"EL" value:[NSString stringWithFormat:@"%+.0f deg",
        p->params.maskElevationDeg] norm:(p->params.maskElevationDeg + 90.0f) / 180.0f
        panel:kMaskPanel row:2u attrs:value style:style];
    [self drawSlider:@"WIDTH" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.maskWidth * 100.0f] norm:p->params.maskWidth
        panel:kMaskPanel row:3u attrs:value style:style];
    [self drawSlider:@"CURVE" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.maskCurve * 100.0f] norm:p->params.maskCurve
        panel:kMaskPanel row:4u attrs:value style:style];
    [self drawSlider:@"DRY" value:(p->params.maskDry < 0.005f
        ? @"-100% FX ONLY"
        : [NSString stringWithFormat:@"%.0f%%",
            (p->params.maskDry - 1.0f) * 100.0f]) norm:p->params.maskDry
        panel:kMaskPanel row:5u attrs:value style:style];

    if (_openMenu > 0 && _menuItems > 0u) {
        NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
        NSString* bodyItems[] = { @"AUTO", @"ICOSA 12", @"DODECA 20", @"SPHERE 24" };
        NSString* topologyItems[] = { @"LOCAL", @"CROSS", @"DIFFUSE", @"ROAMING" };
        NSString** items = _openMenu == 1 ? orderItems
            : (_openMenu == 2 ? bodyItems : topologyItems);
        const int selected = _openMenu == 1 ? static_cast<int>(p->params.order) - 1
            : (_openMenu == 2
                ? (p->params.body == s3g::AmbiEffectBody::Auto ? 0
                    : (p->params.body == s3g::AmbiEffectBody::Dodeca20 ? 2
                        : (p->params.body == s3g::AmbiEffectBody::Sphere24 ? 3 : 1)))
                : static_cast<int>(p->params.topology));
        const CGFloat width = s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            width, 18.0 * _menuItems), 18.0, items, _menuItems, selected,
            _hoverMenuItem, value, style);
    }
}

- (void)updateSliderAtPoint:(NSPoint)point
{
    uint32_t pickup = 0u;
    if (_dragSlider >= 0 && pickupTimeIndex(static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = timeAxisRect();
        const double norm = std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider) value:norm * 2.0 - 1.0];
        return;
    }
    if (_dragSlider >= 0 && pickupFeedbackIndex(static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = feedbackAxisRect();
        const double norm = std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider) value:norm * 2.0 - 1.0];
        return;
    }
    const double norm = std::clamp((point.x
        - s3g::gui_layout::processorControlX(kLayout.output.frame.x))
        / s3g::gui_layout::processorTrackWidth(kLayout.output.frame.width), 0.0, 1.0);
    switch (_dragSlider) {
    case kParamTime: [self setParam:kParamTime value:5.0 * std::pow(2000.0/5.0, norm)]; break;
    case kParamFeedback: [self setParam:kParamFeedback value:norm * 0.88]; break;
    case kParamTone: [self setParam:kParamTone value:norm]; break;
    case kParamSpread: [self setParam:kParamSpread value:norm]; break;
    case kParamDeviation: [self setParam:kParamDeviation value:norm]; break;
    case kParamTopologyAmount: [self setParam:kParamTopologyAmount value:norm]; break;
    case kParamRoamingRate: [self setParam:kParamRoamingRate value:0.005 * std::pow(2.0/0.005, norm)]; break;
    case kParamMix: [self setParam:kParamMix value:norm]; break;
    case kParamOutput: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    case kParamMaskAmount: [self setParam:kParamMaskAmount value:norm]; break;
    case kParamMaskAzimuth: [self setParam:kParamMaskAzimuth value:-180.0 + norm * 360.0]; break;
    case kParamMaskElevation: [self setParam:kParamMaskElevation value:-90.0 + norm * 180.0]; break;
    case kParamMaskWidth: [self setParam:kParamMaskWidth value:norm]; break;
    case kParamMaskCurve: [self setParam:kParamMaskCurve value:norm]; break;
    case kParamMaskDry: [self setParam:kParamMaskDry value:norm - 1.0]; break;
    default: break;
    }
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (s3g::clap_gui::handleProcessorTitleClick(point, &p->plugin,
        @"Ambi Effect Delay", s3g::gui_layout::transformTitleBand(kCanvas),
        p->presetName, sizeof(p->presetName), kParamOutput)) { [self setNeedsDisplay:YES]; return; }
    if (_openMenu > 0) {
        const CGFloat width = s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            NSMakeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
            18.0, _menuItems);
        if (hit >= 0) {
            if (_openMenu == 1) [self setParam:kParamOrder value:hit + 1.0];
            else if (_openMenu == 2) [self setParam:kParamBody
                value:(hit == 0 ? 0.0 : hit + 2.0)];
            else [self setParam:kParamTopology value:hit];
        }
        _openMenu = 0; _hoverMenuItem = -1; [self setNeedsDisplay:YES]; return;
    }
    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(kFieldPanel);
    for (uint32_t i = 0u; i < 3u; ++i) {
        if (NSPointInRect(point,
            s3g::clap_gui::topologyProcessorCameraButtonRect(fieldPanel, i))) {
            [self setViewPreset:static_cast<int>(i)]; return;
        }
    }
    const auto resolved = s3g::resolveAmbiEffectBody(p->params.body, p->params.order);
    const uint32_t count = s3g::ambiEffectBodyPickupCount(resolved);
    const auto projected = projectBody(resolved,
        static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
    for (uint32_t node = 0u; node < count; ++node) {
        const CGFloat dx = point.x - projected.points[node].x;
        const CGFloat dy = point.y - projected.points[node].y;
        if (dx * dx + dy * dy > 15.0 * 15.0) continue;
        _selectedPickup = node;
        if ([event clickCount] >= 2) {
            [self setParam:kParamPickupTimeFirst + node value:0.0];
            [self setParam:kParamPickupFeedbackFirst + node value:0.0];
        }
        [self setNeedsDisplay:YES]; return;
    }
    const auto beginPickup = [&](NSRect axis, clap_id id) {
        if (!NSPointInRect(point, NSInsetRect(axis, 0.0, -8.0))) return false;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &p->plugin, id, &defaultValue)) {
            [self setParam:id value:defaultValue]; _dragSlider = -1;
        } else { _dragSlider = static_cast<int>(id); [self updateSliderAtPoint:point]; }
        return true;
    };
    if (beginPickup(timeAxisRect(), kParamPickupTimeFirst + _selectedPickup)) return;
    if (beginPickup(feedbackAxisRect(), kParamPickupFeedbackFirst + _selectedPickup)) return;

    struct MenuHit { s3g::gui_layout::Rect rect; int menu; uint32_t items; };
    const MenuHit menus[] {
        { s3g::gui_layout::sliderHitRect(kLayout.output, 1u), 1, 7u },
        { s3g::gui_layout::sliderHitRect(kDelayPanel, 0u), 2, 4u },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 0u), 3, 4u },
    };
    for (const auto& menu : menus) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(menu.rect))) continue;
        _openMenu = menu.menu; _menuItems = menu.items;
        _menuOrigin = NSMakePoint(s3g::gui_layout::processorControlX(
            kLayout.output.frame.x), menu.rect.y + 24.0);
        _hoverMenuItem = -1; [self setNeedsDisplay:YES]; return;
    }
    struct SliderHit { s3g::gui_layout::Rect rect; clap_id param; };
    const SliderHit sliders[] {
        { s3g::gui_layout::sliderHitRect(kLayout.output, 0u), kParamOutput },
        { s3g::gui_layout::sliderHitRect(kDelayPanel, 1u), kParamTime },
        { s3g::gui_layout::sliderHitRect(kDelayPanel, 2u), kParamFeedback },
        { s3g::gui_layout::sliderHitRect(kDelayPanel, 3u), kParamTone },
        { s3g::gui_layout::sliderHitRect(kDelayPanel, 4u), kParamSpread },
        { s3g::gui_layout::sliderHitRect(kDelayPanel, 5u), kParamDeviation },
        { s3g::gui_layout::sliderHitRect(kDelayPanel, 6u), kParamMix },
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
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(slider.rect))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &p->plugin, slider.param, &defaultValue)) {
            [self setParam:slider.param value:defaultValue]; _dragSlider = -1;
        } else { _dragSlider = static_cast<int>(slider.param); [self updateSliderAtPoint:point]; }
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(kFieldPlot))) {
        _dragView = YES; _lastDragPoint = point;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
        _viewElDeg = std::clamp(_viewElDeg
            + (point.y - _lastDragPoint.y) * 0.35, -85.0, 85.0);
        _viewMode = -1; _lastDragPoint = point;
        [self storeViewState]; [self setNeedsDisplay:YES]; return;
    }
    if (_openMenu > 0) {
        const CGFloat width = s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
        _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(point,
            NSMakeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
            18.0, _menuItems);
        [self setNeedsDisplay:YES];
    }
    if (_dragSlider >= 0) [self updateSliderAtPoint:point];
}
- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat width = s3g::gui_layout::processorMenuWidth(kLayout.output.frame.width);
    _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(point,
        NSMakeRect(_menuOrigin.x, _menuOrigin.y, width, 18.0 * _menuItems),
        18.0, _menuItems);
    [self setNeedsDisplay:YES];
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event; _dragSlider = -1; _dragView = NO;
}
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA; *floating = false; return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiEffectDelayView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
        static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr;
        return false;
    }
    return true;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p || !p->guiView) return;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectDelayView*>(p->guiView) stopRefreshTimer];
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
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
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
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, false)) return false;
    p->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectDelayView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectDelayView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}
const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide,
};
} // namespace
#endif

namespace {
const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DELAY,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-effect-delay-64",
    "s3g Ambi Effect Delay 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.1.0",
    "Auditory-body overlap-add delay with per-pickup time and feedback variation, topology routing, and directional dry/wet masking for 1OA through 7OA ACN/SN3D fields.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::sanitizeAmbiEffectDelayParams(p->params);
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
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
};
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id)
{
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}
} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
