#include "s3g_ambi_effect_resonance_print.h"
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

constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kPickups = s3g::kAmbiEffectDjFilterMaxPickups;
constexpr uint32_t kStateMagic = 0x52504e54u; // RPNT
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 820u;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamBody,
    kParamTopology,
    kParamCapture,
    kParamClear,
    kParamCaptureSeconds,
    kParamSensitivity,
    kParamModalCount,
    kParamTranspose,
    kParamHarmonicPull,
    kParamHarmonicStretch,
    kParamDecay,
    kParamDecayTilt,
    kParamDrive,
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
    kParamMaskCurve,
    kParamMaskDry,
    kParamPrintEnabled,
    kParamPickupTuneFirst = 100,
    kParamPickupTuneLast = kParamPickupTuneFirst + kPickups - 1u,
    kParamPickupDecayFirst = 200,
    kParamPickupDecayLast = kParamPickupDecayFirst + kPickups - 1u,
};

constexpr uint32_t kBaseParamCount = 27u;

// Version 1 serialized the parameter block directly. Keep its exact layout so
// previously saved prints can be restored safely, but migrate them as bypassed.
struct AmbiEffectResonancePrintParamsV1 {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float captureSeconds = 0.75f;
    float sensitivity = 0.65f;
    uint32_t modalCount = 10u;
    float transposeSemitones = 0.0f;
    float harmonicPull = 0.0f;
    float harmonicStretch = 0.0f;
    float decaySeconds = 1.8f;
    float decayTilt = 0.15f;
    float drive = 0.35f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = 0.55f;
    float outputGainDb = 0.0f;
    std::array<float, kPickups> pickupTuneTrim {};
    std::array<float, kPickups> pickupDecayTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

static_assert(sizeof(AmbiEffectResonancePrintParamsV1) + sizeof(uint32_t)
    == sizeof(s3g::AmbiEffectResonancePrintParams));

s3g::AmbiEffectResonancePrintParams migrateParams(
    const AmbiEffectResonancePrintParamsV1& old)
{
    s3g::AmbiEffectResonancePrintParams params {};
    params.order = old.order;
    params.body = old.body;
    params.topology = old.topology;
    params.captureSeconds = old.captureSeconds;
    params.sensitivity = old.sensitivity;
    params.modalCount = old.modalCount;
    params.transposeSemitones = old.transposeSemitones;
    params.harmonicPull = old.harmonicPull;
    params.harmonicStretch = old.harmonicStretch;
    params.decaySeconds = old.decaySeconds;
    params.decayTilt = old.decayTilt;
    params.drive = old.drive;
    params.spread = old.spread;
    params.deviation = old.deviation;
    params.topologyAmount = old.topologyAmount;
    params.roamingRateHz = old.roamingRateHz;
    params.mix = old.mix;
    params.outputGainDb = old.outputGainDb;
    params.pickupTuneTrim = old.pickupTuneTrim;
    params.pickupDecayTrim = old.pickupDecayTrim;
    params.maskAmount = old.maskAmount;
    params.maskAzimuthDeg = old.maskAzimuthDeg;
    params.maskElevationDeg = old.maskElevationDeg;
    params.maskWidth = old.maskWidth;
    params.maskCurve = old.maskCurve;
    params.maskDry = old.maskDry;
    params.printEnabled = 0u;
    return params;
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    s3g::AmbiEffectResonancePrintParams params {};
    s3g::AmbiEffectResonancePrint processor {};
    double sampleRate = 48000.0;
    std::atomic<bool> captureRequest { false };
    std::atomic<bool> clearRequest { false };
    bool captureGate = false;
    bool clearGate = false;
    std::atomic<bool> tailChangePending { false };
    uint32_t lastTailFrames = 0u;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kPickups> nodeLevel {};
    std::array<std::atomic<float>, kPickups> nodeStrength {};
    std::array<std::atomic<float>, kPickups> nodeWetMask {};
    std::atomic<uint32_t> resolvedBody {
        static_cast<uint32_t>(s3g::AmbiEffectBody::Sphere24) };
    std::atomic<float> roamingPhase { 0.0f };
    std::atomic<float> safetyGain { 1.0f };
    std::atomic<float> excitationGovernorGain { 1.0f };
    std::atomic<float> captureProgress { 0.0f };
    std::atomic<float> fundamentalHz { 0.0f };
    std::atomic<float> pitchConfidence { 0.0f };
    std::atomic<uint32_t> printedModes { 0u };
    std::atomic<bool> hasPrint { false };
    std::atomic<bool> capturing { false };
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

bool pickupTuneIndex(clap_id id, uint32_t& index)
{
    if (id < kParamPickupTuneFirst || id > kParamPickupTuneLast) return false;
    index = static_cast<uint32_t>(id - kParamPickupTuneFirst);
    return true;
}

bool pickupDecayIndex(clap_id id, uint32_t& index)
{
    if (id < kParamPickupDecayFirst || id > kParamPickupDecayLast) return false;
    index = static_cast<uint32_t>(id - kParamPickupDecayFirst);
    return true;
}

bool isParam(clap_id id)
{
    uint32_t index = 0u;
    return (id >= kParamOrder && id <= kParamPrintEnabled)
        || pickupTuneIndex(id, index) || pickupDecayIndex(id, index);
}

bool paramAffectsTail(clap_id id)
{
    uint32_t index = 0u;
    return id == kParamDecay || id == kParamDecayTilt
        || id == kParamSpread || id == kParamDeviation
        || pickupDecayIndex(id, index) || id == kParamClear
        || id == kParamPrintEnabled;
}

void markTailChanged(Plugin& plugin)
{
    plugin.tailChangePending.store(true, std::memory_order_release);
}

void deliverTailChangedOnAudioThread(Plugin& plugin)
{
    if (plugin.tailChangePending.exchange(false, std::memory_order_acq_rel)
        && plugin.host && plugin.hostTail && plugin.hostTail->changed) {
        plugin.hostTail->changed(plugin.host);
    }
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    uint32_t pickup = 0u;
    if (pickupTuneIndex(id, pickup)) {
        plugin.params.pickupTuneTrim[pickup] = static_cast<float>(value);
    } else if (pickupDecayIndex(id, pickup)) {
        plugin.params.pickupDecayTrim[pickup] = static_cast<float>(value);
    } else switch (id) {
    case kParamOrder: plugin.params.order = roundedUint(value); break;
    case kParamBody:
        plugin.params.body = static_cast<s3g::AmbiEffectBody>(roundedUint(value));
        break;
    case kParamTopology:
        plugin.params.topology = static_cast<s3g::AmbiEffectTopology>(roundedUint(value));
        break;
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
    case kParamCaptureSeconds: plugin.params.captureSeconds = static_cast<float>(value); break;
    case kParamSensitivity: plugin.params.sensitivity = static_cast<float>(value); break;
    case kParamModalCount: plugin.params.modalCount = roundedUint(value); break;
    case kParamTranspose: plugin.params.transposeSemitones = static_cast<float>(value); break;
    case kParamHarmonicPull: plugin.params.harmonicPull = static_cast<float>(value); break;
    case kParamHarmonicStretch: plugin.params.harmonicStretch = static_cast<float>(value); break;
    case kParamDecay: plugin.params.decaySeconds = static_cast<float>(value); break;
    case kParamDecayTilt: plugin.params.decayTilt = static_cast<float>(value); break;
    case kParamDrive: plugin.params.drive = static_cast<float>(value); break;
    case kParamSpread: plugin.params.spread = static_cast<float>(value); break;
    case kParamDeviation: plugin.params.deviation = static_cast<float>(value); break;
    case kParamTopologyAmount: plugin.params.topologyAmount = static_cast<float>(value); break;
    case kParamRoamingRate: plugin.params.roamingRateHz = static_cast<float>(value); break;
    case kParamMix: plugin.params.mix = static_cast<float>(value); break;
    case kParamOutput: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kParamMaskAmount: plugin.params.maskAmount = static_cast<float>(value); break;
    case kParamMaskAzimuth: plugin.params.maskAzimuthDeg = static_cast<float>(value); break;
    case kParamMaskElevation: plugin.params.maskElevationDeg = static_cast<float>(value); break;
    case kParamMaskWidth: plugin.params.maskWidth = static_cast<float>(value); break;
    case kParamMaskCurve: plugin.params.maskCurve = static_cast<float>(value); break;
    case kParamMaskDry: plugin.params.maskDry = static_cast<float>(value + 1.0); break;
    case kParamPrintEnabled: plugin.params.printEnabled = value > 0.5 ? 1u : 0u; break;
    default: return;
    }
    plugin.params = s3g::sanitizeAmbiEffectResonancePrintParams(plugin.params);
    if (id == kParamOutput) {
        plugin.processor.setOutputGainTarget(plugin.params.outputGainDb);
    } else {
        plugin.processor.setParams(plugin.params);
    }
    if (paramAffectsTail(id)) markTailChanged(plugin);
}

double getParam(const Plugin& plugin, clap_id id)
{
    uint32_t pickup = 0u;
    if (pickupTuneIndex(id, pickup)) return plugin.params.pickupTuneTrim[pickup];
    if (pickupDecayIndex(id, pickup)) return plugin.params.pickupDecayTrim[pickup];
    switch (id) {
    case kParamOrder: return plugin.params.order;
    case kParamBody: return static_cast<uint32_t>(plugin.params.body);
    case kParamTopology: return static_cast<uint32_t>(plugin.params.topology);
    case kParamCapture: return 0.0;
    case kParamClear: return 0.0;
    case kParamCaptureSeconds: return plugin.params.captureSeconds;
    case kParamSensitivity: return plugin.params.sensitivity;
    case kParamModalCount: return plugin.params.modalCount;
    case kParamTranspose: return plugin.params.transposeSemitones;
    case kParamHarmonicPull: return plugin.params.harmonicPull;
    case kParamHarmonicStretch: return plugin.params.harmonicStretch;
    case kParamDecay: return plugin.params.decaySeconds;
    case kParamDecayTilt: return plugin.params.decayTilt;
    case kParamDrive: return plugin.params.drive;
    case kParamSpread: return plugin.params.spread;
    case kParamDeviation: return plugin.params.deviation;
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
    case kParamPrintEnabled: return plugin.params.printEnabled;
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

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* instance = self(plugin);
    instance->sampleRate = std::max(1.0, sampleRate);
    instance->params = s3g::sanitizeAmbiEffectResonancePrintParams(instance->params);
    instance->processor.setParams(instance->params);
    if (!instance->processor.prepare(instance->sampleRate)) return false;
    instance->processor.reset();
    instance->lastTailFrames = instance->processor.tailFrames();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->processor.reset();
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    for (auto& level : instance->nodeLevel) level.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    for (uint32_t index = 0u; index < events->size(events); ++index) {
        const auto* header = events->get(events, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        applyParam(plugin, event->param_id, event->value);
    }
}

void serviceCommands(Plugin& plugin)
{
    if (plugin.clearRequest.exchange(false, std::memory_order_acq_rel)) {
        plugin.params.printEnabled = 0u;
        plugin.processor.clearPrint();
        markTailChanged(plugin);
    }
    if (plugin.captureRequest.exchange(false, std::memory_order_acq_rel)) {
        plugin.params.printEnabled = 0u;
        plugin.processor.setParams(plugin.params);
        if (plugin.processor.isCapturing()) plugin.processor.cancelCapture();
        else plugin.processor.beginCapture();
        markTailChanged(plugin);
    }
}

void publishStatus(Plugin& plugin)
{
    plugin.params.printEnabled = plugin.processor.params().printEnabled;
    uint32_t modes = 0u;
    for (uint32_t node = 0u; node < kPickups; ++node) {
        plugin.nodeLevel[node].store(plugin.processor.nodeLevel(node),
            std::memory_order_relaxed);
        plugin.nodeStrength[node].store(plugin.processor.nodePrintStrength(node),
            std::memory_order_relaxed);
        plugin.nodeWetMask[node].store(plugin.processor.nodeWetMask(node),
            std::memory_order_relaxed);
        modes += plugin.processor.printedModeCount(node);
    }
    plugin.resolvedBody.store(static_cast<uint32_t>(plugin.processor.resolvedBody()),
        std::memory_order_relaxed);
    plugin.roamingPhase.store(plugin.processor.roamingPhase(), std::memory_order_relaxed);
    plugin.safetyGain.store(plugin.processor.safetyGain(), std::memory_order_relaxed);
    plugin.excitationGovernorGain.store(
        plugin.processor.excitationGovernorGain(), std::memory_order_relaxed);
    plugin.captureProgress.store(plugin.processor.captureProgress(), std::memory_order_relaxed);
    plugin.fundamentalHz.store(plugin.processor.fundamentalHz(), std::memory_order_relaxed);
    plugin.pitchConfidence.store(plugin.processor.pitchConfidence(), std::memory_order_relaxed);
    plugin.printedModes.store(modes, std::memory_order_relaxed);
    plugin.hasPrint.store(plugin.processor.hasPrint(), std::memory_order_relaxed);
    plugin.capturing.store(plugin.processor.isCapturing(), std::memory_order_relaxed);
    const uint32_t tail = plugin.processor.tailFrames();
    if (tail != plugin.lastTailFrames) {
        plugin.lastTailFrames = tail;
        markTailChanged(plugin);
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& plugin,
    const clap_audio_buffer_t& input, const clap_audio_buffer_t& output,
    uint32_t frames, Sample** in, Sample** out)
{
    s3g::clearAudioBuffer(output, frames);
    if (!in || !out) return CLAP_PROCESS_CONTINUE;
    plugin.processor.process(in, out,
        std::min<uint32_t>(input.channel_count, kChannels),
        std::min<uint32_t>(output.channel_count, kChannels), frames);
    float peak = 0.0f;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kChannels);
    for (uint32_t channel = 0u; channel < outChannels; ++channel) {
        if (!out[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            peak = std::max(peak, static_cast<float>(std::abs(out[channel][frame])));
        }
    }
    const float previous = plugin.outputPeak.load(std::memory_order_relaxed);
    plugin.outputPeak.store(std::max(previous * 0.90f, peak),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, processData->in_events);
    serviceCommands(*instance);
    deliverTailChangedOnAudioThread(*instance);
    clap_process_status status = CLAP_PROCESS_CONTINUE;
    if (processData->audio_inputs_count > 0u && processData->audio_outputs_count > 0u) {
        const auto& input = processData->audio_inputs[0];
        const auto& output = processData->audio_outputs[0];
        if (input.data32 && output.data32) {
            status = processTyped<float>(*instance, input, output,
                processData->frames_count, input.data32, output.data32);
        } else if (input.data64 && output.data64) {
            status = processTyped<double>(*instance, input, output,
                processData->frames_count, input.data64, output.data64);
        } else {
            s3g::clearAudioBuffer(output, processData->frames_count);
        }
    }
    publishStatus(*instance);
    deliverTailChangedOnAudioThread(*instance);
    return status;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "7OA ACN/SN3D In" : "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*)
{
    return kBaseParamCount + kPickups * 2u;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    *info = {};
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Ambi Effect/Resonance Print", sizeof(info->module));
    if (index >= kBaseParamCount && index < kBaseParamCount + kPickups) {
        const uint32_t pickup = index - kBaseParamCount;
        info->id = kParamPickupTuneFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups", sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name), "Pickup %02u tune trim", pickup + 1u);
        info->min_value = -1.0; info->max_value = 1.0; info->default_value = 0.0;
        return true;
    }
    if (index >= kBaseParamCount + kPickups
        && index < kBaseParamCount + kPickups * 2u) {
        const uint32_t pickup = index - kBaseParamCount - kPickups;
        info->id = kParamPickupDecayFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups", sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name), "Pickup %02u decay trim", pickup + 1u);
        info->min_value = -1.0; info->max_value = 1.0; info->default_value = 0.0;
        return true;
    }
    struct Spec {
        clap_id id; const char* name; double minimum; double maximum;
        double defaultValue; bool stepped;
    };
    static constexpr Spec specs[] {
        { kParamOrder, "Ambisonic order", 1.0, 7.0, 7.0, true },
        { kParamBody, "Auditory body", 0.0, 5.0, 0.0, true },
        { kParamTopology, "Topology", 0.0, 3.0, 0.0, true },
        { kParamCapture, "Capture print", 0.0, 1.0, 0.0, true },
        { kParamClear, "Clear print", 0.0, 1.0, 0.0, true },
        { kParamCaptureSeconds, "Capture duration", 0.25, 3.0, 0.75, false },
        { kParamSensitivity, "Peak sensitivity", 0.0, 1.0, 0.65, false },
        { kParamModalCount, "Modes per pickup", 2.0, 12.0, 10.0, true },
        { kParamTranspose, "Transpose", -24.0, 24.0, 0.0, false },
        { kParamHarmonicPull, "Harmonic pull", 0.0, 1.0, 0.0, false },
        { kParamHarmonicStretch, "Harmonic stretch", -1.0, 1.0, 0.0, false },
        { kParamDecay, "Decay", 0.08, 8.0, 1.8, false },
        { kParamDecayTilt, "Decay tilt", -1.0, 1.0, 0.15, false },
        { kParamDrive, "Excitation drive", 0.0, 1.0, 0.35, false },
        { kParamSpread, "Pickup spread", 0.0, 1.0, 0.0, false },
        { kParamDeviation, "Pickup deviation", 0.0, 1.0, 0.0, false },
        { kParamTopologyAmount, "Topology amount", 0.0, 1.0, 0.65, false },
        { kParamRoamingRate, "Roaming rate", 0.005, 2.0, 0.08, false },
        { kParamMix, "Mix", 0.0, 1.0, 0.55, false },
        { kParamOutput, "Output gain", -60.0, 12.0, 0.0, false },
        { kParamMaskAmount, "Directional mask amount", 0.0, 1.0, 0.0, false },
        { kParamMaskAzimuth, "Directional mask azimuth", -180.0, 180.0, 0.0, false },
        { kParamMaskElevation, "Directional mask elevation", -90.0, 90.0, 0.0, false },
        { kParamMaskWidth, "Directional mask width", 0.0, 1.0, 0.35, false },
        { kParamMaskCurve, "Directional mask curve", 0.0, 1.0, 0.5, false },
        { kParamMaskDry, "Directional mask dry attenuation", -1.0, 0.0, 0.0, false },
        { kParamPrintEnabled, "Apply print", 0.0, 1.0, 0.0, true },
    };
    if (index >= std::size(specs)) return false;
    const auto& spec = specs[index];
    info->id = spec.id;
    if (spec.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, spec.name, sizeof(info->name));
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

bool paramsValueToText(const clap_plugin_t*, clap_id id,
    double value, char* display, uint32_t size)
{
    if (!display || size == 0u || !isParam(id)) return false;
    uint32_t pickup = 0u;
    if (pickupTuneIndex(id, pickup)) {
        std::snprintf(display, size, "%+.1f st", value * 12.0); return true;
    }
    if (pickupDecayIndex(id, pickup)) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0); return true;
    }
    switch (id) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); break;
    case kParamBody:
        std::snprintf(display, size, "%s", s3g::ambiEffectBodyName(
            static_cast<s3g::AmbiEffectBody>(std::min<uint32_t>(roundedUint(value), 5u))));
        break;
    case kParamTopology:
        std::snprintf(display, size, "%s", s3g::ambiEffectTopologyName(
            static_cast<s3g::AmbiEffectTopology>(std::min<uint32_t>(roundedUint(value), 3u))));
        break;
    case kParamCapture: case kParamClear:
        std::snprintf(display, size, "%s", value > 0.5 ? "TRIGGER" : "READY"); break;
    case kParamPrintEnabled:
        std::snprintf(display, size, "%s", value > 0.5 ? "APPLIED" : "BYPASSED"); break;
    case kParamCaptureSeconds: case kParamDecay:
        std::snprintf(display, size, "%.2f s", value); break;
    case kParamModalCount: std::snprintf(display, size, "%u", roundedUint(value)); break;
    case kParamTranspose: std::snprintf(display, size, "%+.1f st", value); break;
    case kParamRoamingRate: std::snprintf(display, size, "%.3f Hz", value); break;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); break;
    case kParamMaskAzimuth: case kParamMaskElevation:
        std::snprintf(display, size, "%+.0f deg", value); break;
    case kParamMaskDry:
        std::snprintf(display, size, value <= -0.995 ? "FX ONLY" : "%.0f%%", value * 100.0);
        break;
    case kParamHarmonicStretch: case kParamDecayTilt:
        std::snprintf(display, size, "%+.0f%%", value * 100.0); break;
    default: std::snprintf(display, size, "%.0f%%", value * 100.0); break;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value || !isParam(id)) return false;
    if (id == kParamBody) {
        for (uint32_t body = 0u; body <= 5u; ++body) {
            if (std::strcmp(display, s3g::ambiEffectBodyName(
                static_cast<s3g::AmbiEffectBody>(body))) == 0) {
                *value = body;
                return true;
            }
        }
        return false;
    }
    if (id == kParamTopology) {
        for (uint32_t topology = 0u; topology <= 3u; ++topology) {
            if (std::strcmp(display, s3g::ambiEffectTopologyName(
                static_cast<s3g::AmbiEffectTopology>(topology))) == 0) {
                *value = topology;
                return true;
            }
        }
        return false;
    }
    if (id == kParamCapture || id == kParamClear) {
        *value = std::strcmp(display, "TRIGGER") == 0 ? 1.0 : 0.0;
        return true;
    }
    if (id == kParamPrintEnabled) {
        *value = std::strcmp(display, "APPLIED") == 0 ? 1.0 : 0.0;
        return true;
    }
    if (id == kParamMaskDry
        && (std::strcmp(display, "FX ONLY") == 0
            || std::strcmp(display, "-100% FX ONLY") == 0)) {
        *value = -1.0;
        return true;
    }
    const double parsed = std::atof(display);
    uint32_t pickup = 0u;
    if (pickupTuneIndex(id, pickup)) { *value = parsed / 12.0; return true; }
    if (pickupDecayIndex(id, pickup)) { *value = parsed * 0.01; return true; }
    switch (id) {
    case kParamSensitivity: case kParamHarmonicPull:
    case kParamHarmonicStretch: case kParamDecayTilt: case kParamDrive:
    case kParamSpread: case kParamDeviation: case kParamTopologyAmount:
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
    const auto* instance = self(plugin);
    const auto print = instance->processor.printData();
    return writeAll(stream, &kStateMagic, sizeof(kStateMagic))
        && writeAll(stream, &kStateVersion, sizeof(kStateVersion))
        && writeAll(stream, &instance->params, sizeof(instance->params))
        && writeAll(stream, &print, sizeof(print))
        && writeAll(stream, &instance->guiViewMode, sizeof(instance->guiViewMode))
        && writeAll(stream, &instance->guiViewAzDeg, sizeof(instance->guiViewAzDeg))
        && writeAll(stream, &instance->guiViewElDeg, sizeof(instance->guiViewElDeg));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t magic = 0u;
    uint32_t version = 0u;
    s3g::AmbiEffectResonancePrintParams params {};
    s3g::ResonancePrintData print {};
    int32_t viewMode = 2;
    float azimuth = 35.0f;
    float elevation = 34.0f;
    if (!readAll(stream, &magic, sizeof(magic))
        || !readAll(stream, &version, sizeof(version))
        || magic != kStateMagic
        || (version != 1u && version != kStateVersion)) return false;
    if (version == kStateVersion) {
        if (!readAll(stream, &params, sizeof(params))) return false;
    } else {
        AmbiEffectResonancePrintParamsV1 oldParams {};
        if (!readAll(stream, &oldParams, sizeof(oldParams))) return false;
        params = migrateParams(oldParams);
    }
    if (!readAll(stream, &print, sizeof(print))
        || !readAll(stream, &viewMode, sizeof(viewMode))
        || !readAll(stream, &azimuth, sizeof(azimuth))
        || !readAll(stream, &elevation, sizeof(elevation))) return false;
    auto* instance = self(plugin);
    instance->params = s3g::sanitizeAmbiEffectResonancePrintParams(params);
    instance->processor.setParams(instance->params);
    instance->processor.setPrint(print);
    instance->guiViewMode = std::clamp<int32_t>(viewMode, -1, 2);
    instance->guiViewAzDeg = std::isfinite(azimuth) ? azimuth : 35.0f;
    instance->guiViewElDeg = std::clamp(
        std::isfinite(elevation) ? elevation : 34.0f, -85.0f, 85.0f);
    publishStatus(*instance);
    markTailChanged(*instance);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    return self(plugin)->processor.tailFrames();
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
#include "s3g_ambi_effect_resonance_print_gui.inc"
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
    CLAP_PLUGIN_FEATURE_REVERB,
    CLAP_PLUGIN_FEATURE_FILTER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-effect-resonance-print-64",
    "s3g Ambi Effect Resonance Print 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.1.0",
    "Captures a directional modal fingerprint from an ACN/SN3D sound field and re-excites it as an auditory-body resonator for 1OA through 7OA.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->hostTail = host && host->get_extension
        ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL))
        : nullptr;
    instance->params = s3g::sanitizeAmbiEffectResonancePrintParams(instance->params);
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

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* id)
{
    return id && std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
