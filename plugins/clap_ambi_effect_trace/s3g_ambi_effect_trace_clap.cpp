#ifndef S3G_PARTIAL_TRACE
#define S3G_PARTIAL_TRACE 1
#endif

#if S3G_PARTIAL_TRACE
#include "s3g_ambi_effect_partial_trace.h"
#else
#include "s3g_ambi_effect_response_trace.h"
#endif
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/latency.h>
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
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr bool kPartial = S3G_PARTIAL_TRACE != 0;
constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kPickups = s3g::kAmbiEffectDjFilterMaxPickups;
constexpr uint32_t kStateMagic = kPartial ? 0x50545243u : 0x52545243u;
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 820u;

#if S3G_PARTIAL_TRACE
using Processor = s3g::AmbiEffectPartialTrace;
using Params = s3g::AmbiEffectPartialTraceParams;
constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.ambi-effect-partial-trace-64";
constexpr const char* kPluginName = "s3g Ambi Effect Partial Trace 64";
constexpr const char* kPluginDescription =
    "Live partial tracking and sinusoidal resynthesis through an order-adaptive Listener body for 1OA through 7OA ACN/SN3D fields.";
#else
using Processor = s3g::AmbiEffectResponseTrace;
using Params = s3g::AmbiEffectResponseTraceParams;
constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.ambi-effect-response-trace-64";
constexpr const char* kPluginName = "s3g Ambi Effect Response Trace 64";
constexpr const char* kPluginDescription =
    "Captured response convolution through an order-adaptive Listener body for 1OA through 7OA ACN/SN3D fields.";
#endif

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamBody,
    kParamTopology,
    kParamEnabled,
    kParamCapture,
    kParamClear,
    kParamCaptureSeconds,
    kParamPartialCount,
    kParamSensitivity,
    kParamMinimumFrequency,
    kParamMaximumFrequency,
    kParamTracking,
    kParamRelease,
    kParamTranspose,
    kParamEngineGain,
    kParamTone,
    kParamTopologyAmount,
    kParamRoamingRate,
    kParamMix,
    kParamOutput,
    kParamMaskAmount,
    kParamMaskAzimuth,
    kParamMaskElevation,
    kParamMaskWidth,
    kParamMaskCurve,
    kParamMaskDry,
    kParamFreeze,
    kParamSmear,
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    Params params {};
    Processor processor {};
    double sampleRate = 48000.0;
    bool prepared = false;
    std::atomic<bool> captureRequest { false };
    std::atomic<bool> clearRequest { false };
    bool captureGate = false;
    bool clearGate = false;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kPickups> nodeLevel {};
    std::array<std::atomic<float>, kPickups> nodeEffectLevel {};
    std::array<std::atomic<float>, kPickups> nodeWetMask {};
    std::atomic<uint32_t> resolvedBody {
        static_cast<uint32_t>(s3g::AmbiEffectBody::Sphere24) };
    std::atomic<float> roamingPhase { 0.0f };
    std::atomic<float> safetyGain { 1.0f };
    std::atomic<float> excitationGain { 1.0f };
    std::atomic<uint32_t> activePartials { 0u };
    std::atomic<float> strongestFrequency { 0.0f };
    std::atomic<bool> hasResponse { false };
    std::atomic<bool> capturing { false };
    std::atomic<float> captureProgress { 0.0f };
    std::atomic<bool> preparingResponse { false };
    std::atomic<float> preparationProgress { 0.0f };
    std::atomic<uint32_t> capturedPickupCount { 0u };
    uint32_t lastTailFrames = 0u;
    char presetName[64] { "INIT" };
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 35.0f;
    float guiViewElDeg = 34.0f;
#if S3G_PARTIAL_TRACE
    std::array<s3g::PartialTraceFrozenVoiceState,
        s3g::kPartialTraceMaxPartials> pendingFrozenVoices {};
    s3g::AmbiEffectBody pendingFrozenBody = s3g::AmbiEffectBody::Auto;
    bool pendingFrozenValid = false;
#else
    std::vector<float> pendingResponse;
    double pendingResponseSampleRate = 0.0;
    s3g::AmbiEffectBody pendingResponseBody = s3g::AmbiEffectBody::Auto;
    uint32_t pendingResponsePickupCount = 0u;
#endif
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
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

Params sanitize(Params params)
{
#if S3G_PARTIAL_TRACE
    return s3g::sanitizeAmbiEffectPartialTraceParams(params);
#else
    return s3g::sanitizeAmbiEffectResponseTraceParams(params);
#endif
}

void updateProcessor(Plugin& plugin, clap_id changed = CLAP_INVALID_ID)
{
    plugin.params = sanitize(plugin.params);
    if (changed == kParamOutput) {
        plugin.processor.setOutputGainTarget(plugin.params.outputGainDb);
    } else {
        plugin.processor.setParams(plugin.params);
    }
}

bool isParam(clap_id id)
{
    switch (id) {
    case kParamOrder: case kParamBody: case kParamTopology:
    case kParamEnabled: case kParamEngineGain: case kParamTopologyAmount:
    case kParamRoamingRate: case kParamMix: case kParamOutput:
    case kParamMaskAmount: case kParamMaskAzimuth: case kParamMaskElevation:
    case kParamMaskWidth: case kParamMaskCurve: case kParamMaskDry:
        return true;
#if S3G_PARTIAL_TRACE
    case kParamPartialCount: case kParamSensitivity:
    case kParamMinimumFrequency: case kParamMaximumFrequency:
    case kParamTracking: case kParamRelease: case kParamTranspose:
    case kParamFreeze: case kParamSmear:
        return true;
#else
    case kParamCapture: case kParamClear: case kParamCaptureSeconds:
    case kParamTone:
        return true;
#endif
    default: return false;
    }
}

void markTailChanged(Plugin& plugin)
{
#if !S3G_PARTIAL_TRACE
    const uint32_t tail = plugin.processor.tailFrames();
    if (tail == plugin.lastTailFrames) return;
    plugin.lastTailFrames = tail;
    if (plugin.host && plugin.hostTail && plugin.hostTail->changed) {
        plugin.hostTail->changed(plugin.host);
    }
#else
    (void)plugin;
#endif
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kParamOrder: plugin.params.order = roundedUint(value); break;
    case kParamBody:
        plugin.params.body = static_cast<s3g::AmbiEffectBody>(roundedUint(value));
        break;
    case kParamTopology:
        plugin.params.topology = static_cast<s3g::AmbiEffectTopology>(
            roundedUint(value));
        break;
#if S3G_PARTIAL_TRACE
    case kParamEnabled: plugin.params.enabled = value > 0.5 ? 1u : 0u; break;
    case kParamPartialCount: plugin.params.partialCount = roundedUint(value); break;
    case kParamSensitivity: plugin.params.sensitivity = static_cast<float>(value); break;
    case kParamMinimumFrequency:
        plugin.params.minimumFrequencyHz = static_cast<float>(value); break;
    case kParamMaximumFrequency:
        plugin.params.maximumFrequencyHz = static_cast<float>(value); break;
    case kParamTracking: plugin.params.trackingMs = static_cast<float>(value); break;
    case kParamRelease: plugin.params.releaseMs = static_cast<float>(value); break;
    case kParamFreeze: plugin.params.freeze = value > 0.5 ? 1u : 0u; break;
    case kParamSmear: plugin.params.smear = static_cast<float>(value); break;
    case kParamTranspose:
        plugin.params.transposeSemitones = static_cast<float>(value); break;
    case kParamEngineGain: plugin.params.traceGainDb = static_cast<float>(value); break;
#else
    case kParamEnabled:
        plugin.params.responseEnabled = value > 0.5 ? 1u : 0u; break;
    case kParamCapture:
        if (value > 0.5 && !plugin.captureGate) {
            plugin.captureRequest.store(true, std::memory_order_release);
        }
        plugin.captureGate = value > 0.5;
        return;
    case kParamClear:
        if (value > 0.5 && !plugin.clearGate) {
            plugin.clearRequest.store(true, std::memory_order_release);
        }
        plugin.clearGate = value > 0.5;
        return;
    case kParamCaptureSeconds:
        plugin.params.captureSeconds = static_cast<float>(value); break;
    case kParamEngineGain:
        plugin.params.responseGainDb = static_cast<float>(value); break;
    case kParamTone: plugin.params.tone = static_cast<float>(value); break;
#endif
    case kParamTopologyAmount:
        plugin.params.topologyAmount = static_cast<float>(value); break;
    case kParamRoamingRate:
        plugin.params.roamingRateHz = static_cast<float>(value); break;
    case kParamMix: plugin.params.mix = static_cast<float>(value); break;
    case kParamOutput: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kParamMaskAmount:
        plugin.params.maskAmount = static_cast<float>(value); break;
    case kParamMaskAzimuth:
        plugin.params.maskAzimuthDeg = static_cast<float>(value); break;
    case kParamMaskElevation:
        plugin.params.maskElevationDeg = static_cast<float>(value); break;
    case kParamMaskWidth: plugin.params.maskWidth = static_cast<float>(value); break;
    case kParamMaskCurve: plugin.params.maskCurve = static_cast<float>(value); break;
    case kParamMaskDry: plugin.params.maskDry = static_cast<float>(value + 1.0); break;
    default: return;
    }
    updateProcessor(plugin, id);
    if (id == kParamEnabled) markTailChanged(plugin);
}

double getParam(const Plugin& plugin, clap_id id)
{
    switch (id) {
    case kParamOrder: return plugin.params.order;
    case kParamBody: return static_cast<uint32_t>(plugin.params.body);
    case kParamTopology: return static_cast<uint32_t>(plugin.params.topology);
#if S3G_PARTIAL_TRACE
    case kParamEnabled: return plugin.params.enabled;
    case kParamPartialCount: return plugin.params.partialCount;
    case kParamSensitivity: return plugin.params.sensitivity;
    case kParamMinimumFrequency: return plugin.params.minimumFrequencyHz;
    case kParamMaximumFrequency: return plugin.params.maximumFrequencyHz;
    case kParamTracking: return plugin.params.trackingMs;
    case kParamRelease: return plugin.params.releaseMs;
    case kParamFreeze: return plugin.params.freeze;
    case kParamSmear: return plugin.params.smear;
    case kParamTranspose: return plugin.params.transposeSemitones;
    case kParamEngineGain: return plugin.params.traceGainDb;
#else
    case kParamEnabled: return plugin.params.responseEnabled;
    case kParamCapture: case kParamClear: return 0.0;
    case kParamCaptureSeconds: return plugin.params.captureSeconds;
    case kParamEngineGain: return plugin.params.responseGainDb;
    case kParamTone: return plugin.params.tone;
#endif
    case kParamTopologyAmount: return plugin.params.topologyAmount;
    case kParamRoamingRate: return plugin.params.roamingRateHz;
    case kParamMix: return plugin.params.mix;
    case kParamOutput: return plugin.params.outputGainDb;
    case kParamMaskAmount: return plugin.params.maskAmount;
    case kParamMaskAzimuth: return plugin.params.maskAzimuthDeg;
    case kParamMaskElevation: return plugin.params.maskElevationDeg;
    case kParamMaskWidth: return plugin.params.maskWidth;
    case kParamMaskCurve: return plugin.params.maskCurve;
    case kParamMaskDry: return plugin.params.maskDry - 1.0f;
    default: return 0.0;
    }
}

void serviceCommands(Plugin& plugin)
{
#if !S3G_PARTIAL_TRACE
    if (plugin.clearRequest.exchange(false, std::memory_order_acq_rel)) {
        plugin.params.responseEnabled = 0u;
        plugin.processor.clearResponse();
        plugin.processor.setParams(plugin.params);
        markTailChanged(plugin);
    }
    if (plugin.captureRequest.exchange(false, std::memory_order_acq_rel)) {
        plugin.params.responseEnabled = 0u;
        plugin.processor.setParams(plugin.params);
        plugin.processor.startCapture();
        markTailChanged(plugin);
    }
#else
    (void)plugin;
#endif
}

void publishStatus(Plugin& plugin)
{
    plugin.resolvedBody.store(static_cast<uint32_t>(
        plugin.processor.resolvedBody()), std::memory_order_relaxed);
    plugin.roamingPhase.store(plugin.processor.roamingPhase(),
        std::memory_order_relaxed);
    plugin.safetyGain.store(plugin.processor.safetyGain(),
        std::memory_order_relaxed);
    for (uint32_t node = 0u; node < kPickups; ++node) {
        plugin.nodeLevel[node].store(plugin.processor.nodeLevel(node),
            std::memory_order_relaxed);
        plugin.nodeWetMask[node].store(plugin.processor.nodeWetMask(node),
            std::memory_order_relaxed);
#if S3G_PARTIAL_TRACE
        plugin.nodeEffectLevel[node].store(plugin.processor.nodeTraceLevel(node),
            std::memory_order_relaxed);
#else
        plugin.nodeEffectLevel[node].store(plugin.processor.nodeResponseLevel(node),
            std::memory_order_relaxed);
#endif
    }
#if S3G_PARTIAL_TRACE
    plugin.activePartials.store(plugin.processor.activePartialCount(),
        std::memory_order_relaxed);
    plugin.strongestFrequency.store(plugin.processor.strongestFrequencyHz(),
        std::memory_order_relaxed);
    plugin.excitationGain.store(plugin.processor.traceGovernorGain(),
        std::memory_order_relaxed);
#else
    plugin.excitationGain.store(plugin.processor.excitationGovernorGain(),
        std::memory_order_relaxed);
    plugin.hasResponse.store(plugin.processor.hasResponse(),
        std::memory_order_relaxed);
    plugin.capturing.store(plugin.processor.capturing(),
        std::memory_order_relaxed);
    plugin.captureProgress.store(plugin.processor.captureProgress(),
        std::memory_order_relaxed);
    plugin.preparingResponse.store(plugin.processor.preparingResponse(),
        std::memory_order_relaxed);
    plugin.preparationProgress.store(plugin.processor.preparationProgress(),
        std::memory_order_relaxed);
    plugin.capturedPickupCount.store(plugin.processor.capturedPickupCount(),
        std::memory_order_relaxed);
    markTailChanged(plugin);
#endif
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events || !events->size || !events->get) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        if (isParam(event->param_id)) applyParam(plugin,
            event->param_id, event->value);
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
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    instance->processor.setParams(instance->params);
    if (!instance->processor.prepare(sampleRate)) return false;
    instance->prepared = true;
#if S3G_PARTIAL_TRACE
    if (instance->pendingFrozenValid && instance->params.freeze) {
        instance->processor.restoreFrozenState(instance->pendingFrozenBody,
            instance->pendingFrozenVoices.data(),
            static_cast<uint32_t>(instance->pendingFrozenVoices.size()));
        instance->pendingFrozenValid = false;
    }
#else
    if (!instance->pendingResponse.empty()) {
        const uint32_t pickupCount = instance->pendingResponsePickupCount;
        const uint32_t frames = pickupCount > 0u
            ? static_cast<uint32_t>(instance->pendingResponse.size()
                / pickupCount) : 0u;
        instance->processor.loadResponse(instance->pendingResponse.data(),
            pickupCount, instance->pendingResponseBody, frames,
            instance->pendingResponseSampleRate, false);
        instance->pendingResponse.clear();
        instance->processor.setParams(instance->params);
    }
    instance->lastTailFrames = instance->processor.tailFrames();
#endif
    publishStatus(*instance);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
#if S3G_PARTIAL_TRACE
    instance->pendingFrozenValid = instance->params.freeze != 0u;
    instance->pendingFrozenBody = instance->processor.resolvedBody();
    if (instance->pendingFrozenValid) {
        for (uint32_t index = 0u;
            index < instance->pendingFrozenVoices.size(); ++index) {
            instance->pendingFrozenVoices[index]
                = instance->processor.frozenVoiceState(index);
        }
    }
#else
    const uint32_t frames = instance->processor.responseFrames();
    const uint32_t pickupCount = instance->processor.capturedPickupCount();
    instance->pendingResponse.assign(
        static_cast<size_t>(pickupCount) * frames, 0.0f);
    for (uint32_t node = 0u; node < pickupCount; ++node) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            instance->pendingResponse[static_cast<size_t>(node) * frames + frame]
                = instance->processor.responseSample(node, frame);
        }
    }
    instance->pendingResponseSampleRate = instance->processor.responseSampleRate();
    instance->pendingResponseBody = instance->processor.capturedBody();
    instance->pendingResponsePickupCount = pickupCount;
#endif
    instance->prepared = false;
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->processor.reset(); }

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    if (!processData || processData->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    auto* instance = self(plugin);
    readParamEvents(*instance, processData->in_events);
    serviceCommands(*instance);
    const clap_audio_buffer_t* input = processData->audio_inputs_count > 0u
        ? &processData->audio_inputs[0] : nullptr;
    clap_audio_buffer_t& output = processData->audio_outputs[0];
    const uint32_t inputChannels = input ? input->channel_count : 0u;
    if (output.data32) {
        instance->processor.process(input ? input->data32 : nullptr,
            output.data32, inputChannels, output.channel_count,
            processData->frames_count);
    } else if (output.data64) {
        instance->processor.process(input ? input->data64 : nullptr,
            output.data64, inputChannels, output.channel_count,
            processData->frames_count);
    }
    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        if (output.data32 && output.data32[channel]) {
            for (uint32_t frame = 0u; frame < processData->frames_count; ++frame) {
                peak = std::max(peak, std::abs(output.data32[channel][frame]));
            }
        } else if (output.data64 && output.data64[channel]) {
            for (uint32_t frame = 0u; frame < processData->frames_count; ++frame) {
                peak = std::max(peak, static_cast<float>(
                    std::abs(output.data64[channel][frame])));
            }
        }
    }
    instance->outputPeak.store(peak, std::memory_order_relaxed);
    publishStatus(*instance);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = isInput ? 0u : 1u;
    std::strncpy(info->name, isInput ? "Ambisonic Input" : "Ambisonic Output",
        sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = nullptr;
    info->in_place_pair = isInput ? 1u : 0u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

#if S3G_PARTIAL_TRACE
constexpr ParamSpec paramSpecs[] {
    { kParamOrder, "Ambisonic order", 1, 7, 7, true },
    { kParamBody, "Listener body", 0, 5, 0, true },
    { kParamTopology, "Topology", 0, 3, 0, true },
    { kParamEnabled, "Trace bypass", 0, 1, 1, true },
    { kParamPartialCount, "Partial count", 1, 16, 10, true },
    { kParamSensitivity, "Sensitivity", 0, 1, 0.62, false },
    { kParamMinimumFrequency, "Minimum frequency", 20, 4000, 55, false },
    { kParamMaximumFrequency, "Maximum frequency", 200, 20000, 12000, false },
    { kParamTracking, "Tracking time", 20, 1000, 120, false },
    { kParamRelease, "Trace release", 40, 4000, 420, false },
    { kParamSmear, "Temporal smear", 0, 1, 0, false },
    { kParamTranspose, "Transpose", -24, 24, 0, false },
    { kParamEngineGain, "Trace gain", -36, 12, -3, false },
    { kParamTopologyAmount, "Topology amount", 0, 1, 0.65, false },
    { kParamRoamingRate, "Roaming rate", 0.005, 2, 0.08, false },
    { kParamMix, "Mix", 0, 1, 0.55, false },
    { kParamOutput, "Output gain", -60, 12, 0, false },
    { kParamMaskAmount, "Directional mask amount", 0, 1, 0, false },
    { kParamMaskAzimuth, "Directional mask azimuth", -180, 180, 0, false },
    { kParamMaskElevation, "Directional mask elevation", -90, 90, 0, false },
    { kParamMaskWidth, "Directional mask width", 0, 1, 0.35, false },
    { kParamMaskCurve, "Directional mask curve", 0, 1, 0.5, false },
    { kParamMaskDry, "Directional mask dry attenuation", -1, 0, 0, false },
    { kParamFreeze, "Freeze trace", 0, 1, 0, true },
};
#else
constexpr ParamSpec paramSpecs[] {
    { kParamOrder, "Ambisonic order", 1, 7, 7, true },
    { kParamBody, "Listener body", 0, 5, 0, true },
    { kParamTopology, "Topology", 0, 3, 0, true },
    { kParamEnabled, "Apply response", 0, 1, 0, true },
    { kParamCapture, "Capture response", 0, 1, 0, true },
    { kParamClear, "Clear response", 0, 1, 0, true },
    { kParamCaptureSeconds, "Capture duration", 0.05, 1.5, 0.5, false },
    { kParamEngineGain, "Response gain", -36, 12, -6, false },
    { kParamTone, "Response tone", 0, 1, 0.72, false },
    { kParamTopologyAmount, "Topology amount", 0, 1, 0.65, false },
    { kParamRoamingRate, "Roaming rate", 0.005, 2, 0.08, false },
    { kParamMix, "Mix", 0, 1, 0.5, false },
    { kParamOutput, "Output gain", -60, 12, 0, false },
    { kParamMaskAmount, "Directional mask amount", 0, 1, 0, false },
    { kParamMaskAzimuth, "Directional mask azimuth", -180, 180, 0, false },
    { kParamMaskElevation, "Directional mask elevation", -90, 90, 0, false },
    { kParamMaskWidth, "Directional mask width", 0, 1, 0.35, false },
    { kParamMaskCurve, "Directional mask curve", 0, 1, 0.5, false },
    { kParamMaskDry, "Directional mask dry attenuation", -1, 0, 0, false },
};
#endif

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(std::size(paramSpecs));
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(paramSpecs)) return false;
    const auto& spec = paramSpecs[index];
    std::memset(info, 0, sizeof(*info));
    info->id = spec.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (spec.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, spec.name, sizeof(info->name) - 1u);
    std::strncpy(info->module, "Ambi Effect Trace", sizeof(info->module) - 1u);
    info->min_value = spec.minimum;
    info->max_value = spec.maximum;
    info->default_value = spec.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !isParam(id)) return false;
    *value = getParam(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !isParam(id)) return false;
    switch (id) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); break;
    case kParamBody:
        std::snprintf(display, size, "%s", s3g::ambiEffectBodyName(
            static_cast<s3g::AmbiEffectBody>(std::min<uint32_t>(
                roundedUint(value), 5u)))); break;
    case kParamTopology:
        std::snprintf(display, size, "%s", s3g::ambiEffectTopologyName(
            static_cast<s3g::AmbiEffectTopology>(std::min<uint32_t>(
                roundedUint(value), 3u)))); break;
    case kParamEnabled:
        std::snprintf(display, size, "%s", value > 0.5
            ? (kPartial ? "ON" : "APPLIED")
            : (kPartial ? "BYPASSED" : "BYPASSED")); break;
    case kParamFreeze:
        std::snprintf(display, size, "%s", value > 0.5
            ? "FROZEN" : "LIVE"); break;
    case kParamCapture: case kParamClear:
        std::snprintf(display, size, "%s", value > 0.5 ? "TRIGGER" : "READY"); break;
    case kParamPartialCount: std::snprintf(display, size, "%u", roundedUint(value)); break;
    case kParamMinimumFrequency: case kParamMaximumFrequency:
        if (value >= 1000.0) {
            std::snprintf(display, size, "%.1f kHz", value * 0.001);
        } else {
            std::snprintf(display, size, "%.0f Hz", value);
        }
        break;
    case kParamTracking: case kParamRelease:
        std::snprintf(display, size, "%.0f ms", value); break;
    case kParamTranspose: std::snprintf(display, size, "%+.1f st", value); break;
    case kParamEngineGain: case kParamOutput:
        std::snprintf(display, size, "%+.1f dB", value); break;
    case kParamCaptureSeconds: std::snprintf(display, size, "%.2f s", value); break;
    case kParamRoamingRate: std::snprintf(display, size, "%.3f Hz", value); break;
    case kParamMaskAzimuth: case kParamMaskElevation:
        std::snprintf(display, size, "%+.0f deg", value); break;
    case kParamMaskDry:
        std::snprintf(display, size, value <= -0.995 ? "FX ONLY" : "%.0f%%",
            value * 100.0); break;
    default: std::snprintf(display, size, "%.0f%%", value * 100.0); break;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display,
    double* value)
{
    if (!display || !value || !isParam(id)) return false;
    if (id == kParamBody) {
        for (uint32_t body = 0u; body <= 5u; ++body) {
            if (std::strcmp(display, s3g::ambiEffectBodyName(
                static_cast<s3g::AmbiEffectBody>(body))) == 0) {
                *value = body; return true;
            }
        }
        return false;
    }
    if (id == kParamTopology) {
        for (uint32_t topology = 0u; topology <= 3u; ++topology) {
            if (std::strcmp(display, s3g::ambiEffectTopologyName(
                static_cast<s3g::AmbiEffectTopology>(topology))) == 0) {
                *value = topology; return true;
            }
        }
        return false;
    }
    if (id == kParamEnabled) {
        *value = std::strcmp(display, "ON") == 0
                || std::strcmp(display, "APPLIED") == 0
            ? 1.0 : 0.0;
        return true;
    }
#if S3G_PARTIAL_TRACE
    if (id == kParamFreeze) {
        *value = std::strcmp(display, "FROZEN") == 0 ? 1.0 : 0.0;
        return true;
    }
#endif
#if !S3G_PARTIAL_TRACE
    if (id == kParamCapture || id == kParamClear) {
        *value = std::strcmp(display, "TRIGGER") == 0 ? 1.0 : 0.0;
        return true;
    }
#endif
    if (id == kParamMaskDry && std::strcmp(display, "FX ONLY") == 0) {
        *value = -1.0;
        return true;
    }
    const double parsed = std::atof(display);
    switch (id) {
    case kParamMinimumFrequency: case kParamMaximumFrequency:
        *value = parsed * (std::strstr(display, "kHz")
                || std::strstr(display, "KHZ") ? 1000.0 : 1.0);
        break;
    case kParamSensitivity: case kParamSmear: case kParamTone:
    case kParamTopologyAmount:
    case kParamMix: case kParamMaskAmount: case kParamMaskWidth:
    case kParamMaskCurve: case kParamMaskDry:
        *value = parsed * 0.01; break;
    default: *value = parsed; break;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* events, const clap_output_events_t*)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, events);
    serviceCommands(*instance);
    publishStatus(*instance);
}

const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo,
    paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool writeAll(const clap_ostream_t* stream, const void* source, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    size_t position = 0u;
    while (position < size) {
        const int64_t wrote = stream->write(stream, bytes + position,
            size - position);
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
        const int64_t read = stream->read(stream, bytes + position,
            size - position);
        if (read <= 0 || static_cast<size_t>(read) > size - position) return false;
        position += static_cast<size_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* instance = self(plugin);
    if (!writeAll(stream, &kStateMagic, sizeof(kStateMagic))
        || !writeAll(stream, &kStateVersion, sizeof(kStateVersion))
        || !writeAll(stream, &instance->params, sizeof(instance->params))
        || !writeAll(stream, &instance->guiViewMode, sizeof(instance->guiViewMode))
        || !writeAll(stream, &instance->guiViewAzDeg, sizeof(instance->guiViewAzDeg))
        || !writeAll(stream, &instance->guiViewElDeg,
            sizeof(instance->guiViewElDeg))) return false;
#if S3G_PARTIAL_TRACE
    std::array<s3g::PartialTraceFrozenVoiceState,
        s3g::kPartialTraceMaxPartials> frozenVoices {};
    const uint32_t voiceCount = instance->params.freeze
        ? s3g::kPartialTraceMaxPartials : 0u;
    s3g::AmbiEffectBody frozenBody = s3g::AmbiEffectBody::Auto;
    if (voiceCount > 0u) {
        const bool usePending = !instance->prepared
            && instance->pendingFrozenValid;
        frozenBody = usePending ? instance->pendingFrozenBody
            : instance->processor.resolvedBody();
        for (uint32_t index = 0u; index < voiceCount; ++index) {
            frozenVoices[index] = usePending
                ? instance->pendingFrozenVoices[index]
                : instance->processor.frozenVoiceState(index);
        }
    }
    const uint32_t frozenBodyValue = static_cast<uint32_t>(frozenBody);
    if (!writeAll(stream, &frozenBodyValue, sizeof(frozenBodyValue))
        || !writeAll(stream, &voiceCount, sizeof(voiceCount))
        || (voiceCount > 0u && !writeAll(stream, frozenVoices.data(),
            static_cast<size_t>(voiceCount) * sizeof(frozenVoices[0])))) {
        return false;
    }
#else
    uint32_t frames = instance->processor.responseFrames();
    uint32_t pickupCount = instance->processor.capturedPickupCount();
    s3g::AmbiEffectBody capturedBody = instance->processor.capturedBody();
    double rate = instance->processor.responseSampleRate();
    const bool usePending = !instance->prepared
        && !instance->pendingResponse.empty();
    if (usePending) {
        pickupCount = instance->pendingResponsePickupCount;
        capturedBody = instance->pendingResponseBody;
        frames = pickupCount > 0u ? static_cast<uint32_t>(
            instance->pendingResponse.size() / pickupCount) : 0u;
        rate = instance->pendingResponseSampleRate;
    }
    const uint32_t bodyValue = static_cast<uint32_t>(capturedBody);
    if (!writeAll(stream, &bodyValue, sizeof(bodyValue))
        || !writeAll(stream, &pickupCount, sizeof(pickupCount))
        || !writeAll(stream, &frames, sizeof(frames))
        || !writeAll(stream, &rate, sizeof(rate))) return false;
    if (!usePending) {
        for (uint32_t node = 0u; node < pickupCount; ++node) {
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                const float value = instance->processor.responseSample(
                    node, frame);
                if (!writeAll(stream, &value, sizeof(value))) return false;
            }
        }
    } else if (frames > 0u && !writeAll(stream,
        instance->pendingResponse.data(), static_cast<size_t>(pickupCount)
            * frames * sizeof(float))) return false;
#endif
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t magic = 0u;
    uint32_t version = 0u;
    Params params {};
    int32_t viewMode = 2;
    float azimuth = 35.0f;
    float elevation = 34.0f;
    if (!readAll(stream, &magic, sizeof(magic))
        || !readAll(stream, &version, sizeof(version))
        || magic != kStateMagic) return false;
#if S3G_PARTIAL_TRACE
    if (version != 1u && version != kStateVersion) return false;
    if (version == 1u) {
        constexpr size_t previousParamsSize = offsetof(Params, freeze);
        if (!readAll(stream, &params, previousParamsSize)) return false;
    } else if (!readAll(stream, &params, sizeof(params))) {
        return false;
    }
#else
    if (version != 1u && version != kStateVersion) return false;
    if (!readAll(stream, &params, sizeof(params))) return false;
#endif
    if (!readAll(stream, &viewMode, sizeof(viewMode))
        || !readAll(stream, &azimuth, sizeof(azimuth))
        || !readAll(stream, &elevation, sizeof(elevation))) return false;
    auto* instance = self(plugin);
    instance->captureRequest.store(false, std::memory_order_release);
    instance->clearRequest.store(false, std::memory_order_release);
    instance->captureGate = false;
    instance->clearGate = false;
    instance->params = sanitize(params);
    instance->guiViewMode = std::clamp<int32_t>(viewMode, -1, 2);
    instance->guiViewAzDeg = std::isfinite(azimuth) ? azimuth : 35.0f;
    instance->guiViewElDeg = std::clamp(
        std::isfinite(elevation) ? elevation : 34.0f, -85.0f, 85.0f);
#if S3G_PARTIAL_TRACE
    instance->pendingFrozenValid = false;
    instance->pendingFrozenBody = s3g::AmbiEffectBody::Auto;
    instance->pendingFrozenVoices.fill({});
    if (version == kStateVersion) {
        uint32_t frozenBodyValue = 0u;
        uint32_t voiceCount = 0u;
        if (!readAll(stream, &frozenBodyValue, sizeof(frozenBodyValue))
            || !readAll(stream, &voiceCount, sizeof(voiceCount))
            || frozenBodyValue > static_cast<uint32_t>(
                s3g::AmbiEffectBody::Sphere24)
            || voiceCount > s3g::kPartialTraceMaxPartials) return false;
        if (voiceCount > 0u && !readAll(stream,
            instance->pendingFrozenVoices.data(),
            static_cast<size_t>(voiceCount)
                * sizeof(instance->pendingFrozenVoices[0]))) return false;
        instance->pendingFrozenBody = static_cast<s3g::AmbiEffectBody>(
            frozenBodyValue);
        instance->pendingFrozenValid = voiceCount > 0u
            && instance->params.freeze != 0u;
    }
#else
    uint32_t bodyValue = 0u;
    uint32_t pickupCount = 0u;
    uint32_t frames = 0u;
    double rate = 0.0;
    if (version == kStateVersion) {
        if (!readAll(stream, &bodyValue, sizeof(bodyValue))
            || !readAll(stream, &pickupCount, sizeof(pickupCount))) {
            return false;
        }
    }
    if (!readAll(stream, &frames, sizeof(frames))
        || !readAll(stream, &rate, sizeof(rate))
        || frames > 1200000u || (frames > 0u
            && (!std::isfinite(rate) || rate < 1000.0 || rate > 768000.0))) {
        return false;
    }
    s3g::AmbiEffectBody capturedBody = s3g::AmbiEffectBody::Auto;
    if (version == 1u && frames > 0u) {
        capturedBody = s3g::resolveAmbiEffectBody(
            instance->params.body, instance->params.order, true);
        pickupCount = s3g::ambiEffectBodyPickupCount(capturedBody);
        std::vector<float> mono(frames, 0.0f);
        if (!readAll(stream, mono.data(), frames * sizeof(float))) return false;
        instance->pendingResponse.assign(
            static_cast<size_t>(pickupCount) * frames, 0.0f);
        for (uint32_t node = 0u; node < pickupCount; ++node) {
            std::copy(mono.begin(), mono.end(),
                instance->pendingResponse.begin()
                    + static_cast<size_t>(node) * frames);
        }
    } else {
        if (bodyValue > static_cast<uint32_t>(s3g::AmbiEffectBody::Sphere24)
            || (frames > 0u && (bodyValue == 0u || pickupCount == 0u
                || pickupCount > kPickups
                || pickupCount > s3g::ambiEffectBodyPickupCount(
                    static_cast<s3g::AmbiEffectBody>(bodyValue))))) return false;
        capturedBody = static_cast<s3g::AmbiEffectBody>(bodyValue);
        instance->pendingResponse.assign(
            static_cast<size_t>(pickupCount) * frames, 0.0f);
        if (frames > 0u && !readAll(stream,
            instance->pendingResponse.data(), static_cast<size_t>(pickupCount)
                * frames * sizeof(float))) return false;
    }
    instance->pendingResponseSampleRate = rate;
    instance->pendingResponseBody = capturedBody;
    instance->pendingResponsePickupCount = pickupCount;
#endif
    instance->processor.setParams(instance->params);
#if S3G_PARTIAL_TRACE
    if (instance->prepared && instance->pendingFrozenValid
        && instance->params.freeze) {
        instance->processor.restoreFrozenState(instance->pendingFrozenBody,
            instance->pendingFrozenVoices.data(),
            static_cast<uint32_t>(instance->pendingFrozenVoices.size()));
        instance->pendingFrozenValid = false;
    }
#else
    if (instance->prepared && !instance->pendingResponse.empty()) {
        instance->processor.loadResponse(instance->pendingResponse.data(),
            instance->pendingResponsePickupCount,
            instance->pendingResponseBody, frames, rate, false);
        instance->pendingResponse.clear();
        instance->processor.setParams(instance->params);
    } else if (instance->prepared && frames == 0u) {
        instance->processor.clearResponse();
        instance->processor.setParams(instance->params);
    }
#endif
    publishStatus(*instance);
    markTailChanged(*instance);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
#if S3G_PARTIAL_TRACE
    (void)plugin;
    return 0u;
#else
    return self(plugin)->processor.tailFrames();
#endif
}
const clap_plugin_tail_t tailExt { tailGet };

uint32_t latencyGet(const clap_plugin_t*)
{
#if S3G_PARTIAL_TRACE
    return 0u;
#else
    return s3g::kResponseTracePartitionSize;
#endif
}
const clap_plugin_latency_t latencyExt { latencyGet };

} // namespace

#if defined(__APPLE__)
#include "s3g_ambi_effect_trace_gui.inc"
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
#if S3G_PARTIAL_TRACE
    CLAP_PLUGIN_FEATURE_PITCH_SHIFTER,
#else
    CLAP_PLUGIN_FEATURE_REVERB,
#endif
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT, kPluginId, kPluginName, "s3g",
    "https://github.com/s3g/s3g-dsp", "", "", "0.1.0",
    kPluginDescription, features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->hostTail = host && host->get_extension
        ? static_cast<const clap_host_tail_t*>(host->get_extension(
            host, CLAP_EXT_TAIL)) : nullptr;
    instance->params = sanitize(instance->params);
    instance->processor.setParams(instance->params);
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
const clap_plugin_factory_t factory { factoryGetPluginCount,
    factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id)
{
    return id && std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
