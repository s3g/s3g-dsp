#if S3G_AMBI_EFFECT_PITCH
#include "s3g_ambi_effect_pitch.h"
#else
#include "s3g_ambi_effect_gain.h"
#endif
#include "s3g_realtime.h"

#include <clap/clap.h>
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

constexpr bool kPitch = S3G_AMBI_EFFECT_PITCH != 0;
constexpr uint32_t kChannels = s3g::kAmbiEffectDjFilterMaxChannels;
constexpr uint32_t kPickups = s3g::kAmbiEffectDjFilterMaxPickups;
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kPreviousPickups = 20u;
constexpr uint32_t kGuiWidth = 820u;
constexpr uint32_t kGuiHeight = 640u;

#if S3G_AMBI_EFFECT_PITCH
using Processor = s3g::AmbiEffectPitch;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-effect-pitch-64";
constexpr const char* kPluginName = "s3g Ambi Effect Pitch 64";
constexpr const char* kTitle = "s3g AMBI EFFECT PITCH 64CH";
constexpr const char* kDescription = "Auditory-body dual-grain pitch shifter with per-pickup interval and window variation, topology routing, and directional dry/wet masking for 1OA through 7OA ACN/SN3D fields.";
#else
using Processor = s3g::AmbiEffectGain;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-effect-gain-64";
constexpr const char* kPluginName = "s3g Ambi Effect Gain 64";
constexpr const char* kTitle = "s3g AMBI EFFECT GAIN 64CH";
constexpr const char* kDescription = "Auditory-body gain field with per-pickup level variation, topology routing, and directional dry/wet masking for 1OA through 7OA ACN/SN3D fields.";
#endif

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamBody,
    kParamTopology,
    kParamPrimary,
    kParamWindow,
    kParamGlide,
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
    kParamPickupPrimaryFirst = 100,
    kParamPickupPrimaryLast = kParamPickupPrimaryFirst + kPickups - 1u,
    kParamPickupSecondaryFirst = 200,
    kParamPickupSecondaryLast = kParamPickupSecondaryFirst + kPickups - 1u,
};

struct Params {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float primary = 0.0f;
    float windowMs = 80.0f;
    float glideMs = 250.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = kPitch ? 0.35f : 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kPickups> pickupPrimaryTrim {};
    std::array<float, kPickups> pickupSecondaryTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

struct ParamsV1 {
    uint32_t order = 7u;
    s3g::AmbiEffectBody body = s3g::AmbiEffectBody::Auto;
    s3g::AmbiEffectTopology topology = s3g::AmbiEffectTopology::Local;
    float primary = 0.0f;
    float windowMs = 80.0f;
    float glideMs = 250.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float topologyAmount = 0.65f;
    float roamingRateHz = 0.08f;
    float mix = kPitch ? 0.35f : 1.0f;
    float outputGainDb = 0.0f;
    std::array<float, kPreviousPickups> pickupPrimaryTrim {};
    std::array<float, kPreviousPickups> pickupSecondaryTrim {};
    float maskAmount = 0.0f;
    float maskAzimuthDeg = 0.0f;
    float maskElevationDeg = 0.0f;
    float maskWidth = 0.35f;
    float maskCurve = 0.5f;
    float maskDry = 1.0f;
};

Params sanitize(Params p)
{
    p.order = std::clamp<uint32_t>(p.order, 1u, 7u);
    p.body = static_cast<s3g::AmbiEffectBody>(std::min<uint32_t>(
        static_cast<uint32_t>(p.body), 5u));
    if (p.body == s3g::AmbiEffectBody::Tetra4
        || p.body == s3g::AmbiEffectBody::Cube8) {
        p.body = s3g::AmbiEffectBody::Icosa12;
    }
    p.topology = static_cast<s3g::AmbiEffectTopology>(std::min<uint32_t>(
        static_cast<uint32_t>(p.topology), 3u));
    p.primary = s3g::clamp(p.primary, -60.0f, kPitch ? 24.0f : 18.0f);
    if (kPitch) p.primary = s3g::clamp(p.primary, -24.0f, 24.0f);
    p.windowMs = s3g::clamp(p.windowMs, 20.0f, 180.0f);
    p.glideMs = s3g::clamp(p.glideMs, 10.0f, 2000.0f);
    p.spread = s3g::clamp(p.spread, 0.0f, 1.0f);
    p.deviation = s3g::clamp(p.deviation, 0.0f, 1.0f);
    p.topologyAmount = s3g::clamp(p.topologyAmount, 0.0f, 1.0f);
    p.roamingRateHz = s3g::clamp(p.roamingRateHz, 0.005f, 2.0f);
    p.mix = s3g::clamp(p.mix, 0.0f, 1.0f);
    p.outputGainDb = s3g::clamp(p.outputGainDb, -60.0f, 12.0f);
    for (float& value : p.pickupPrimaryTrim) value = s3g::clamp(value, -1.0f, 1.0f);
    for (float& value : p.pickupSecondaryTrim) value = s3g::clamp(value, -1.0f, 1.0f);
    p.maskAmount = s3g::clamp(p.maskAmount, 0.0f, 1.0f);
    p.maskAzimuthDeg = s3g::clamp(p.maskAzimuthDeg, -180.0f, 180.0f);
    p.maskElevationDeg = s3g::clamp(p.maskElevationDeg, -90.0f, 90.0f);
    p.maskWidth = s3g::clamp(p.maskWidth, 0.0f, 1.0f);
    p.maskCurve = s3g::clamp(p.maskCurve, 0.0f, 1.0f);
    p.maskDry = s3g::clamp(p.maskDry, 0.0f, 1.0f);
    return p;
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    Params params {};
    Processor processor {};
    double sampleRate = 48000.0;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kPickups> nodeLevel {};
    std::array<std::atomic<float>, kPickups> nodeWetMask {};
    std::atomic<uint32_t> resolvedBody {
        static_cast<uint32_t>(s3g::AmbiEffectBody::Sphere24) };
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

void updateProcessor(Plugin& p)
{
#if S3G_AMBI_EFFECT_PITCH
    s3g::AmbiEffectPitchParams value {};
    value.order = p.params.order;
    value.body = p.params.body;
    value.topology = p.params.topology;
    value.semitones = p.params.primary;
    value.windowMs = p.params.windowMs;
    value.glideMs = p.params.glideMs;
    value.spread = p.params.spread;
    value.deviation = p.params.deviation;
    value.topologyAmount = p.params.topologyAmount;
    value.roamingRateHz = p.params.roamingRateHz;
    value.mix = p.params.mix;
    value.outputGainDb = p.params.outputGainDb;
    value.pickupPitchTrim = p.params.pickupPrimaryTrim;
    value.pickupWindowTrim = p.params.pickupSecondaryTrim;
    value.maskAmount = p.params.maskAmount;
    value.maskAzimuthDeg = p.params.maskAzimuthDeg;
    value.maskElevationDeg = p.params.maskElevationDeg;
    value.maskWidth = p.params.maskWidth;
    value.maskCurve = p.params.maskCurve;
    value.maskDry = p.params.maskDry;
    p.processor.setParams(value);
#else
    s3g::AmbiEffectGainParams value {};
    value.order = p.params.order;
    value.body = p.params.body;
    value.topology = p.params.topology;
    value.gainDb = p.params.primary;
    value.spread = p.params.spread;
    value.deviation = p.params.deviation;
    value.topologyAmount = p.params.topologyAmount;
    value.roamingRateHz = p.params.roamingRateHz;
    value.mix = p.params.mix;
    value.outputGainDb = p.params.outputGainDb;
    value.pickupGainTrim = p.params.pickupPrimaryTrim;
    value.maskAmount = p.params.maskAmount;
    value.maskAzimuthDeg = p.params.maskAzimuthDeg;
    value.maskElevationDeg = p.params.maskElevationDeg;
    value.maskWidth = p.params.maskWidth;
    value.maskCurve = p.params.maskCurve;
    value.maskDry = p.params.maskDry;
    p.processor.setParams(value);
#endif
}

bool pickupPrimaryIndex(clap_id id, uint32_t& index)
{
    if (id < kParamPickupPrimaryFirst || id > kParamPickupPrimaryLast) return false;
    index = static_cast<uint32_t>(id - kParamPickupPrimaryFirst);
    return true;
}

bool pickupSecondaryIndex(clap_id id, uint32_t& index)
{
    if (!kPitch || id < kParamPickupSecondaryFirst
        || id > kParamPickupSecondaryLast) return false;
    index = static_cast<uint32_t>(id - kParamPickupSecondaryFirst);
    return true;
}

bool isParam(clap_id id)
{
    uint32_t index = 0u;
    if (pickupPrimaryIndex(id, index) || pickupSecondaryIndex(id, index)) return true;
    if (id >= kParamOrder && id <= kParamMaskCurve) {
        return kPitch || (id != kParamWindow && id != kParamGlide);
    }
    return false;
}

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

void applyParam(Plugin& p, clap_id id, double value)
{
    uint32_t pickup = 0u;
    if (pickupPrimaryIndex(id, pickup)) {
        p.params.pickupPrimaryTrim[pickup] = static_cast<float>(value);
    } else if (pickupSecondaryIndex(id, pickup)) {
        p.params.pickupSecondaryTrim[pickup] = static_cast<float>(value);
    } else switch (id) {
    case kParamOrder: p.params.order = roundedUint(value); break;
    case kParamBody: p.params.body = static_cast<s3g::AmbiEffectBody>(roundedUint(value)); break;
    case kParamTopology: p.params.topology = static_cast<s3g::AmbiEffectTopology>(roundedUint(value)); break;
    case kParamPrimary: p.params.primary = static_cast<float>(value); break;
    case kParamWindow: if (kPitch) p.params.windowMs = static_cast<float>(value); break;
    case kParamGlide: if (kPitch) p.params.glideMs = static_cast<float>(value); break;
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
    p.params = sanitize(p.params);
    updateProcessor(p);
}

double getParam(const Plugin& p, clap_id id)
{
    uint32_t pickup = 0u;
    if (pickupPrimaryIndex(id, pickup)) return p.params.pickupPrimaryTrim[pickup];
    if (pickupSecondaryIndex(id, pickup)) return p.params.pickupSecondaryTrim[pickup];
    switch (id) {
    case kParamOrder: return p.params.order;
    case kParamBody: return static_cast<uint32_t>(p.params.body);
    case kParamTopology: return static_cast<uint32_t>(p.params.topology);
    case kParamPrimary: return p.params.primary;
    case kParamWindow: return p.params.windowMs;
    case kParamGlide: return p.params.glideMs;
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
    case kParamMaskDry: return p.params.maskDry - 1.0f;
    case kParamMaskCurve: return p.params.maskCurve;
    default: return 0.0;
    }
}

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

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
    auto* p = self(plugin);
    p->sampleRate = std::max(1.0, sampleRate);
    p->params = sanitize(p->params);
    updateProcessor(*p);
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
    for (auto& value : p->nodeLevel) value.store(0.0f, std::memory_order_relaxed);
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
clap_process_status processTyped(Plugin& p, const clap_audio_buffer_t& input,
    const clap_audio_buffer_t& output, uint32_t frames, Sample** in, Sample** out)
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
    for (uint32_t node = 0u; node < kPickups; ++node) {
        p.nodeLevel[node].store(p.processor.nodeLevel(node), std::memory_order_relaxed);
        p.nodeWetMask[node].store(p.processor.nodeWetMask(node), std::memory_order_relaxed);
    }
    p.resolvedBody.store(static_cast<uint32_t>(p.processor.resolvedBody()),
        std::memory_order_relaxed);
    p.roamingPhase.store(p.processor.roamingPhase(), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* data)
{
    auto* p = self(plugin);
    readParamEvents(*p, data->in_events);
    if (data->audio_inputs_count == 0u || data->audio_outputs_count == 0u)
        return CLAP_PROCESS_CONTINUE;
    const auto& input = data->audio_inputs[0];
    const auto& output = data->audio_outputs[0];
    if (input.data32 && output.data32) return processTyped<float>(
        *p, input, output, data->frames_count, input.data32, output.data32);
    if (input.data64 && output.data64) return processTyped<double>(
        *p, input, output, data->frames_count, input.data64, output.data64);
    s3g::clearAudioBuffer(output, data->frames_count);
    return CLAP_PROCESS_CONTINUE;
}
void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name, isInput ? "7OA ACN/SN3D In" : "7OA ACN/SN3D Out",
        sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamSpec {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double initial;
    bool stepped;
};

constexpr std::array<ParamSpec, 18u> kPitchSpecs {{
    { kParamOrder, "Ambisonic order", 1, 7, 7, true },
    { kParamBody, "Auditory body", 0, 5, 0, true },
    { kParamTopology, "Topology", 0, 3, 0, true },
    { kParamPrimary, "Pitch", -24, 24, 0, false },
    { kParamWindow, "Grain window", 20, 180, 80, false },
    { kParamGlide, "Pitch glide", 10, 2000, 250, false },
    { kParamSpread, "Pickup spread", 0, 1, 0, false },
    { kParamDeviation, "Pickup deviation", 0, 1, 0, false },
    { kParamTopologyAmount, "Topology amount", 0, 1, .65, false },
    { kParamRoamingRate, "Roaming rate", .005, 2, .08, false },
    { kParamMix, "Mix", 0, 1, .35, false },
    { kParamOutput, "Output gain", -60, 12, 0, false },
    { kParamMaskAmount, "Directional mask amount", 0, 1, 0, false },
    { kParamMaskAzimuth, "Directional mask azimuth", -180, 180, 0, false },
    { kParamMaskElevation, "Directional mask elevation", -90, 90, 0, false },
    { kParamMaskWidth, "Directional mask width", 0, 1, .35, false },
    { kParamMaskDry, "Directional mask dry attenuation", -1, 0, 0, false },
    { kParamMaskCurve, "Directional mask curve", 0, 1, .5, false },
}};

constexpr std::array<ParamSpec, 16u> kGainSpecs {{
    { kParamOrder, "Ambisonic order", 1, 7, 7, true },
    { kParamBody, "Auditory body", 0, 5, 0, true },
    { kParamTopology, "Topology", 0, 3, 0, true },
    { kParamPrimary, "Gain", -60, 18, 0, false },
    { kParamSpread, "Pickup spread", 0, 1, 0, false },
    { kParamDeviation, "Pickup deviation", 0, 1, 0, false },
    { kParamTopologyAmount, "Topology amount", 0, 1, .65, false },
    { kParamRoamingRate, "Roaming rate", .005, 2, .08, false },
    { kParamMix, "Mix", 0, 1, 1, false },
    { kParamOutput, "Output gain", -60, 12, 0, false },
    { kParamMaskAmount, "Directional mask amount", 0, 1, 0, false },
    { kParamMaskAzimuth, "Directional mask azimuth", -180, 180, 0, false },
    { kParamMaskElevation, "Directional mask elevation", -90, 90, 0, false },
    { kParamMaskWidth, "Directional mask width", 0, 1, .35, false },
    { kParamMaskDry, "Directional mask dry attenuation", -1, 0, 0, false },
    { kParamMaskCurve, "Directional mask curve", 0, 1, .5, false },
}};

uint32_t paramsCount(const clap_plugin_t*)
{
    return (kPitch ? static_cast<uint32_t>(kPitchSpecs.size())
                   : static_cast<uint32_t>(kGainSpecs.size()))
        + kPickups * (kPitch ? 2u : 1u);
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    const uint32_t base = kPitch ? static_cast<uint32_t>(kPitchSpecs.size())
                                 : static_cast<uint32_t>(kGainSpecs.size());
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Ambi Effect", sizeof(info->module));
    if (index >= base && index < base + kPickups) {
        const uint32_t pickup = index - base;
        info->id = kParamPickupPrimaryFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups", sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name), "Pickup %02u %s trim",
            pickup + 1u, kPitch ? "pitch" : "gain");
        info->min_value = -1; info->max_value = 1; info->default_value = 0;
        return true;
    }
    if (kPitch && index >= base + kPickups && index < base + kPickups * 2u) {
        const uint32_t pickup = index - base - kPickups;
        info->id = kParamPickupSecondaryFirst + pickup;
        std::strncpy(info->module, "Ambi Effect/Pickups", sizeof(info->module));
        std::snprintf(info->name, sizeof(info->name),
            "Pickup %02u window trim", pickup + 1u);
        info->min_value = -1; info->max_value = 1; info->default_value = 0;
        return true;
    }
    if (index >= base) return false;
    const ParamSpec& spec = kPitch ? kPitchSpecs[index] : kGainSpecs[index];
    info->id = spec.id;
    if (spec.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, spec.name, sizeof(info->name));
    info->min_value = spec.minimum;
    info->max_value = spec.maximum;
    info->default_value = spec.initial;
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
    uint32_t pickup = 0u;
    if (pickupPrimaryIndex(id, pickup) || pickupSecondaryIndex(id, pickup)) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0); return true;
    }
    switch (id) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); break;
    case kParamBody: std::snprintf(display, size, "%s", s3g::ambiEffectBodyName(
        static_cast<s3g::AmbiEffectBody>(std::min<uint32_t>(roundedUint(value), 5u)))); break;
    case kParamTopology: std::snprintf(display, size, "%s", s3g::ambiEffectTopologyName(
        static_cast<s3g::AmbiEffectTopology>(std::min<uint32_t>(roundedUint(value), 3u)))); break;
    case kParamPrimary: std::snprintf(display, size, "%+.1f %s", value, kPitch ? "st" : "dB"); break;
    case kParamWindow: case kParamGlide: std::snprintf(display, size, "%.0f ms", value); break;
    case kParamRoamingRate: std::snprintf(display, size, "%.3f Hz", value); break;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); break;
    case kParamMaskAzimuth: case kParamMaskElevation: std::snprintf(display, size, "%+.0f deg", value); break;
    case kParamMaskDry:
        if (value <= -.995) std::snprintf(display, size, "-100%% FX ONLY");
        else std::snprintf(display, size, "%.0f%%", value * 100.0);
        break;
    default: std::snprintf(display, size, "%.0f%%", value * 100.0); break;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display,
    double* value)
{
    if (!display || !value || !isParam(id)) return false;
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
    uint32_t pickup = 0u;
    if (pickupPrimaryIndex(id, pickup) || pickupSecondaryIndex(id, pickup)
        || id == kParamSpread || id == kParamDeviation
        || id == kParamTopologyAmount || id == kParamMix
        || id == kParamMaskAmount || id == kParamMaskWidth
        || id == kParamMaskCurve || id == kParamMaskDry) {
        *value = parsed * .01;
    } else *value = parsed;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* events,
    const clap_output_events_t*)
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
    const auto* p = self(plugin);
    return stream && stream->write
        && writeAll(stream, &kStateVersion, sizeof(kStateVersion))
        && writeAll(stream, &p->params, sizeof(p->params))
        && writeAll(stream, &p->guiViewMode, sizeof(p->guiViewMode))
        && writeAll(stream, &p->guiViewAzDeg, sizeof(p->guiViewAzDeg))
        && writeAll(stream, &p->guiViewElDeg, sizeof(p->guiViewElDeg));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    Params params {};
    int32_t viewMode = 2;
    float azimuth = 35.0f;
    float elevation = 34.0f;
    if (!readAll(stream, &version, sizeof(version))) return false;
    if (version == kStateVersion) {
        if (!readAll(stream, &params, sizeof(params))) return false;
    } else if (version == 1u) {
        ParamsV1 old {};
        if (!readAll(stream, &old, sizeof(old))) return false;
        params.order = old.order;
        params.body = old.body;
        params.topology = old.topology;
        params.primary = old.primary;
        params.windowMs = old.windowMs;
        params.glideMs = old.glideMs;
        params.spread = old.spread;
        params.deviation = old.deviation;
        params.topologyAmount = old.topologyAmount;
        params.roamingRateHz = old.roamingRateHz;
        params.mix = old.mix;
        params.outputGainDb = old.outputGainDb;
        std::copy(old.pickupPrimaryTrim.begin(), old.pickupPrimaryTrim.end(),
            params.pickupPrimaryTrim.begin());
        std::copy(old.pickupSecondaryTrim.begin(), old.pickupSecondaryTrim.end(),
            params.pickupSecondaryTrim.begin());
        params.maskAmount = old.maskAmount;
        params.maskAzimuthDeg = old.maskAzimuthDeg;
        params.maskElevationDeg = old.maskElevationDeg;
        params.maskWidth = old.maskWidth;
        params.maskCurve = old.maskCurve;
        params.maskDry = old.maskDry;
    } else {
        return false;
    }
    if (!readAll(stream, &viewMode, sizeof(viewMode))
        || !readAll(stream, &azimuth, sizeof(azimuth))
        || !readAll(stream, &elevation, sizeof(elevation))) return false;
    auto* p = self(plugin);
    p->params = sanitize(params);
    p->guiViewMode = std::clamp<int32_t>(viewMode, -1, 2);
    p->guiViewAzDeg = std::isfinite(azimuth) ? azimuth : 35.0f;
    p->guiViewElDeg = std::clamp(std::isfinite(elevation) ? elevation : 34.0f,
        -85.0f, 85.0f);
    updateProcessor(*p);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {
constexpr const auto& kLayout = s3g::gui_layout::kTransformFamilyLayout;
constexpr s3g::gui_layout::Canvas kCanvas { 820.0, 640.0 };
constexpr s3g::gui_layout::Rect kFieldPanel { 18.0, 42.0, 506.0, 580.0 };
constexpr s3g::gui_layout::Rect kFieldPlot { 34.0, 78.0, 474.0, 526.0 };
constexpr s3g::gui_layout::Panel kEnginePanel {
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

struct ProjectedBody {
    std::array<NSPoint, kPickups> points {};
    std::array<float, kPickups> depths {};
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
    const auto directions = s3g::ambiEffectBodyDirections(body);
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

NSRect primaryAxisRect()
{
    const NSRect field = s3g::clap_gui::cocoaRect(kFieldPlot);
    return NSMakeRect(field.origin.x + 42.0,
        field.origin.y + field.size.height - (kPitch ? 132.0 : 96.0),
        field.size.width - 84.0, 16.0);
}
NSRect secondaryAxisRect()
{
    const NSRect field = s3g::clap_gui::cocoaRect(kFieldPlot);
    return NSMakeRect(field.origin.x + 42.0,
        field.origin.y + field.size.height - 66.0,
        field.size.width - 84.0, 16.0);
}
}

#if S3G_AMBI_EFFECT_PITCH
#define S3GAmbiEffectPitchGainView S3GAmbiEffectPitchView
#else
#define S3GAmbiEffectPitchGainView S3GAmbiEffectGainView
#endif

@interface S3GAmbiEffectPitchGainView : NSView {
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
@end

@implementation S3GAmbiEffectPitchGainView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin; _dragSlider = -1; _openMenu = 0;
        _hoverMenuItem = -1; _menuOrigin = NSZeroPoint; _menuItems = 0u;
        _selectedPickup = 0u; _dragView = NO; _lastDragPoint = NSZeroPoint;
        auto* p = static_cast<Plugin*>(plugin);
        _viewMode = p->guiViewMode; _viewAzDeg = p->guiViewAzDeg;
        _viewElDeg = p->guiViewElDeg; _refreshTimer = nil;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0)
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
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
    p->guiViewMode = _viewMode; p->guiViewAzDeg = static_cast<float>(_viewAzDeg);
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
        p->outputPeak.load(std::memory_order_relaxed) * .92f,
        std::memory_order_relaxed);
    const auto resolved = static_cast<s3g::AmbiEffectBody>(
        p->resolvedBody.load(std::memory_order_relaxed));
    NSString* status = [NSString stringWithFormat:@"%uOA · %s · %@",
        p->params.order, s3g::ambiEffectBodyName(resolved),
        s3g::clap_gui::peakDbText(peak)];
    s3g::clap_gui::drawTransformTitleBand(
        [NSString stringWithUTF8String:kTitle],
        [NSString stringWithUTF8String:p->presetName], status,
        s3g::gui_layout::transformTitleBand(kCanvas), style);

    const NSRect fieldPanel = s3g::clap_gui::cocoaRect(kFieldPanel);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y,
        fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(
        kPitch ? @"LISTENER PICKUPS / PITCH FIELD" : @"LISTENER PICKUPS / GAIN FIELD",
        true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width,
        21.0, text, style);
    s3g::clap_gui::drawTopologyProcessorCameraButtons(
        fieldPanel, _viewMode, text, style);
    const NSRect field = s3g::clap_gui::cocoaRect(kFieldPlot);
    [s3g::clap_gui::color(0x0d0f0f) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);

    const uint32_t count = s3g::ambiEffectBodyPickupCount(resolved);
    if (_selectedPickup >= count) _selectedPickup = 0u;
    const auto directions = s3g::ambiEffectBodyDirections(resolved);
    const auto projected = projectBody(resolved,
        static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
    float nearestDot = -2.0f;
    for (uint32_t a = 0u; a < count; ++a) {
        for (uint32_t b = a + 1u; b < count; ++b) {
            nearestDot = std::max(nearestDot,
                directions[a].x * directions[b].x
                + directions[a].y * directions[b].y
                + directions[a].z * directions[b].z);
        }
    }
    NSBezierPath* shell = [NSBezierPath bezierPath];
    for (uint32_t a = 0u; a < count; ++a) {
        for (uint32_t b = a + 1u; b < count; ++b) {
            const float relation = directions[a].x * directions[b].x
                + directions[a].y * directions[b].y
                + directions[a].z * directions[b].z;
            if (relation < nearestDot - .0001f) continue;
            [shell moveToPoint:projected.points[a]];
            [shell lineToPoint:projected.points[b]];
        }
    }
    [s3g::clap_gui::color(0x707674, .25) setStroke];
    [shell setLineWidth:.8]; [shell stroke];

    NSBezierPath* heardRoute = [NSBezierPath bezierPath];
    if (p->params.topology == s3g::AmbiEffectTopology::Cross) {
        uint32_t opposite = 0u; float minimum = 2.0f;
        for (uint32_t other = 0u; other < count; ++other) {
            const float relation = directions[_selectedPickup].x * directions[other].x
                + directions[_selectedPickup].y * directions[other].y
                + directions[_selectedPickup].z * directions[other].z;
            if (relation < minimum) { minimum = relation; opposite = other; }
        }
        [heardRoute moveToPoint:projected.points[_selectedPickup]];
        [heardRoute lineToPoint:projected.points[opposite]];
    } else if (p->params.topology == s3g::AmbiEffectTopology::Diffuse) {
        for (uint32_t other = 0u; other < count; ++other) {
            const float relation = directions[_selectedPickup].x * directions[other].x
                + directions[_selectedPickup].y * directions[other].y
                + directions[_selectedPickup].z * directions[other].z;
            if (other == _selectedPickup || relation < nearestDot - .0001f) continue;
            [heardRoute moveToPoint:projected.points[_selectedPickup]];
            [heardRoute lineToPoint:projected.points[other]];
        }
    } else {
        const CGFloat orbit = p->params.topology == s3g::AmbiEffectTopology::Roaming
            ? 14.0 + 4.0 * std::sin(p->roamingPhase.load(std::memory_order_relaxed)
                * 2.0f * s3g::kPi) : 11.0;
        [heardRoute appendBezierPathWithOvalInRect:NSMakeRect(
            projected.points[_selectedPickup].x - orbit,
            projected.points[_selectedPickup].y - orbit, orbit * 2.0, orbit * 2.0)];
    }
    [s3g::clap_gui::color(0xd7dcda,
        .25 + p->params.topologyAmount * .55) setStroke];
    [heardRoute setLineWidth:1.6]; [heardRoute stroke];

    if (p->params.maskAmount > .001f) {
        const float az = p->params.maskAzimuthDeg * s3g::kPi / 180.0f;
        const float el = p->params.maskElevationDeg * s3g::kPi / 180.0f;
        const s3g::Vec3 direction { std::cos(el) * std::cos(az),
            std::cos(el) * std::sin(az), std::sin(el) };
        const NSPoint point = projectDirection(direction,
            static_cast<float>(_viewAzDeg), static_cast<float>(_viewElDeg));
        const CGFloat radius = 13.0 + p->params.maskWidth * 34.0;
        [s3g::clap_gui::color(0xcbd0cd,
            .20 + p->params.maskAmount * .46) setStroke];
        NSBezierPath* mask = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(point.x - radius, point.y - radius,
                radius * 2.0, radius * 2.0)];
        [mask setLineWidth:.8 + p->params.maskCurve * 1.2];
        CGFloat dash[] { 4.0, 3.0 };
        [mask setLineDash:dash count:2 phase:0.0]; [mask stroke];
    }

    for (uint32_t pass = 0u; pass < 2u; ++pass) {
        for (uint32_t node = 0u; node < count; ++node) {
            if ((projected.depths[node] >= 0.0f) != (pass == 1u)) continue;
            const float level = p->nodeLevel[node].load(std::memory_order_relaxed);
            const float db = 20.0f * std::log10(std::max(.000001f, level));
            const float meter = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            const CGFloat halo = 7.0 + std::sqrt(meter) * 15.0;
            [s3g::clap_gui::color(pass ? 0xd8dcda : 0x858c89,
                .025 + meter * .22) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                projected.points[node].x - halo, projected.points[node].y - halo,
                halo * 2.0, halo * 2.0)] fill];
            constexpr CGFloat size = 12.0;
            const NSRect marker = NSMakeRect(projected.points[node].x - size * .5,
                projected.points[node].y - size * .5, size, size);
            [s3g::clap_gui::color(pass ? 0xe9ecea : 0xb9bfbc,
                node == _selectedPickup ? 1.0 : (pass ? .94 : .76)) setFill];
            NSRectFill(marker);
            [s3g::clap_gui::color(pass ? 0x4a504d : 0x3d4240, .94) setFill];
            NSRectFill(NSInsetRect(marker, 2, 2));
            [s3g::clap_gui::color(node == _selectedPickup ? 0xffffff
                    : (pass ? 0xe9ecea : 0xb9bfbc),
                node == _selectedPickup ? 1.0 : (pass ? .94 : .76)) setStroke];
            NSFrameRect(marker);
            const float relation = kPitch
                ? s3g::ambiEffectPickupPitchSemitones(p->params.primary,
                    p->params.pickupPrimaryTrim[node], p->params.spread,
                    p->params.deviation, node, count) / 24.0f
                : s3g::ambiEffectPickupGainDb(p->params.primary,
                    p->params.pickupPrimaryTrim[node], p->params.spread,
                    p->params.deviation, node, count) / 60.0f;
            [s3g::clap_gui::color(0xcfd4d1,
                node == _selectedPickup ? .95 : .46) setStroke];
            NSBezierPath* rel = [NSBezierPath bezierPath];
            [rel moveToPoint:NSMakePoint(projected.points[node].x,
                projected.points[node].y - 7.0)];
            [rel lineToPoint:NSMakePoint(projected.points[node].x,
                projected.points[node].y - 7.0 - relation * 14.0)];
            [rel setLineWidth:1.2]; [rel stroke];
        }
    }

    const float selectedLevel = p->nodeLevel[_selectedPickup].load(
        std::memory_order_relaxed);
    const float selectedDb = std::max(-120.0f,
        20.0f * std::log10(std::max(.000001f, selectedLevel)));
    const float effectivePrimary = kPitch
        ? s3g::ambiEffectPickupPitchSemitones(p->params.primary,
            p->params.pickupPrimaryTrim[_selectedPickup], p->params.spread,
            p->params.deviation, _selectedPickup, count)
        : s3g::ambiEffectPickupGainDb(p->params.primary,
            p->params.pickupPrimaryTrim[_selectedPickup], p->params.spread,
            p->params.deviation, _selectedPickup, count);
    [[NSString stringWithFormat:@"HEARD AT PICKUP %02u  ·  IN %.0f dB  ·  %+.1f %s",
        _selectedPickup + 1u, selectedDb, effectivePrimary, kPitch ? "st" : "dB"]
        drawAtPoint:NSMakePoint(primaryAxisRect().origin.x,
            primaryAxisRect().origin.y - 22.0) withAttributes:value];

    const auto drawAxis = [&](NSRect axis, float trim, NSString* left, NSString* right) {
        [s3g::clap_gui::color(0x171a19) setFill]; NSRectFill(axis);
        [style.grid setStroke]; NSFrameRect(axis);
        [left drawAtPoint:NSMakePoint(axis.origin.x, NSMaxY(axis) + 5.0)
            withAttributes:value];
        [@"0" drawAtPoint:NSMakePoint(NSMidX(axis) - 3.0, NSMaxY(axis) + 5.0)
            withAttributes:value];
        [right drawAtPoint:NSMakePoint(NSMaxX(axis) - 54.0, NSMaxY(axis) + 5.0)
            withAttributes:value];
        const CGFloat cursor = axis.origin.x + axis.size.width * (trim + 1.0f) * .5f;
        [s3g::clap_gui::color(0x343937) setFill];
        NSRectFill(NSMakeRect(NSMidX(axis) - 1.0, axis.origin.y, 2.0, axis.size.height));
        [s3g::clap_gui::color(0xd9dddb) setFill];
        NSRectFill(NSMakeRect(cursor - 2.0, axis.origin.y - 3.0, 4.0, axis.size.height + 6.0));
    };
    drawAxis(primaryAxisRect(), p->params.pickupPrimaryTrim[_selectedPickup],
        kPitch ? @"PITCH -" : @"GAIN -", kPitch ? @"PITCH +" : @"GAIN +");
    if (kPitch) drawAxis(secondaryAxisRect(),
        p->params.pickupSecondaryTrim[_selectedPickup], @"WINDOW -", @"WINDOW +");

    s3g::clap_gui::drawPanelFrame(kLayout.output, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD", true, kLayout.output, text, style);
    s3g::clap_gui::drawPanelFrame(kEnginePanel, style);
    s3g::clap_gui::drawPanelHeader(
        kPitch ? @"PITCH / RELATIONSHIPS" : @"GAIN / RELATIONSHIPS",
        true, kEnginePanel, text, style);
    s3g::clap_gui::drawPanelFrame(kTopologyPanel, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY", true, kTopologyPanel, text, style);
    s3g::clap_gui::drawPanelFrame(kMaskPanel, style);
    s3g::clap_gui::drawPanelHeader(@"DIRECTIONAL WET MASK", true,
        kMaskPanel, text, style);
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB",
        p->params.outputGainDb] norm:(p->params.outputGainDb + 60.0f) / 72.0f
        panel:kLayout.output row:0u attrs:value style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p->params.order]
        panel:kLayout.output row:1u attrs:value style:style];
    NSString* bodyText = p->params.body == s3g::AmbiEffectBody::Auto
        ? [NSString stringWithFormat:@"AUTO → %s", s3g::ambiEffectBodyName(resolved)]
        : [NSString stringWithUTF8String:s3g::ambiEffectBodyName(p->params.body)];
    [self drawMenu:@"BODY" value:bodyText panel:kEnginePanel row:0u attrs:value style:style];
    [self drawSlider:(kPitch ? @"PCH" : @"GAIN")
        value:[NSString stringWithFormat:@"%+.1f %s", p->params.primary,
            kPitch ? "st" : "dB"]
        norm:(kPitch ? (p->params.primary + 24.0f) / 48.0f
                     : (p->params.primary + 60.0f) / 78.0f)
        panel:kEnginePanel row:1u attrs:value style:style];
    uint32_t relationshipRow = 2u;
    if (kPitch) {
        [self drawSlider:@"WIN" value:[NSString stringWithFormat:@"%.0f ms", p->params.windowMs]
            norm:(p->params.windowMs - 20.0f) / 160.0f panel:kEnginePanel row:2u attrs:value style:style];
        [self drawSlider:@"GLD" value:[NSString stringWithFormat:@"%.0f ms", p->params.glideMs]
            norm:std::log(p->params.glideMs / 10.0f) / std::log(200.0f)
            panel:kEnginePanel row:3u attrs:value style:style];
        relationshipRow = 4u;
    }
    [self drawSlider:@"SPRD" value:[NSString stringWithFormat:@"%.0f%%", p->params.spread * 100.0f]
        norm:p->params.spread panel:kEnginePanel row:relationshipRow attrs:value style:style];
    [self drawSlider:@"DEV" value:[NSString stringWithFormat:@"%.0f%%", p->params.deviation * 100.0f]
        norm:p->params.deviation panel:kEnginePanel row:relationshipRow + 1u attrs:value style:style];
    [self drawSlider:@"MIX" value:[NSString stringWithFormat:@"%.0f%%", p->params.mix * 100.0f]
        norm:p->params.mix panel:kEnginePanel row:relationshipRow + 2u attrs:value style:style];
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:
        s3g::ambiEffectTopologyName(p->params.topology)]
        panel:kTopologyPanel row:0u attrs:value style:style];
    [self drawSlider:@"AMT" value:[NSString stringWithFormat:@"%.0f%%",
        p->params.topologyAmount * 100.0f] norm:p->params.topologyAmount
        panel:kTopologyPanel row:1u attrs:value style:style];
    [self drawSlider:@"RATE" value:[NSString stringWithFormat:@"%.3f Hz",
        p->params.roamingRateHz]
        norm:std::log(p->params.roamingRateHz / .005f) / std::log(400.0f)
        panel:kTopologyPanel row:2u attrs:value style:style];
    [self drawSlider:@"MASK" value:(p->params.maskAmount < .005f ? @"OFF"
        : [NSString stringWithFormat:@"%.0f%%", p->params.maskAmount * 100.0f])
        norm:p->params.maskAmount panel:kMaskPanel row:0u attrs:value style:style];
    [self drawSlider:@"AZ" value:[NSString stringWithFormat:@"%+.0f deg", p->params.maskAzimuthDeg]
        norm:(p->params.maskAzimuthDeg + 180.0f) / 360.0f panel:kMaskPanel row:1u attrs:value style:style];
    [self drawSlider:@"EL" value:[NSString stringWithFormat:@"%+.0f deg", p->params.maskElevationDeg]
        norm:(p->params.maskElevationDeg + 90.0f) / 180.0f panel:kMaskPanel row:2u attrs:value style:style];
    [self drawSlider:@"WIDTH" value:[NSString stringWithFormat:@"%.0f%%", p->params.maskWidth * 100.0f]
        norm:p->params.maskWidth panel:kMaskPanel row:3u attrs:value style:style];
    [self drawSlider:@"CURVE" value:[NSString stringWithFormat:@"%.0f%%", p->params.maskCurve * 100.0f]
        norm:p->params.maskCurve panel:kMaskPanel row:4u attrs:value style:style];
    [self drawSlider:@"DRY" value:(p->params.maskDry < .005f ? @"-100% FX ONLY"
        : [NSString stringWithFormat:@"%.0f%%", (p->params.maskDry - 1.0f) * 100.0f])
        norm:p->params.maskDry panel:kMaskPanel row:5u attrs:value style:style];

    if (_openMenu > 0 && _menuItems > 0u) {
        NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
        NSString* bodyItems[] = { @"AUTO", @"ICOSA 12", @"DODECA 20", @"SPHERE 24" };
        NSString* topologyItems[] = { @"LOCAL", @"CROSS", @"DIFFUSE", @"ROAMING" };
        NSString** items = _openMenu == 1 ? orderItems
            : (_openMenu == 2 ? bodyItems : topologyItems);
        const int selected = _openMenu == 1 ? static_cast<int>(p->params.order) - 1
            : (_openMenu == 2 ? (p->params.body == s3g::AmbiEffectBody::Auto ? 0
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
    if (_dragSlider >= 0 && pickupPrimaryIndex(static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = primaryAxisRect();
        const double norm = std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider) value:norm * 2.0 - 1.0]; return;
    }
    if (_dragSlider >= 0 && pickupSecondaryIndex(static_cast<clap_id>(_dragSlider), pickup)) {
        const NSRect axis = secondaryAxisRect();
        const double norm = std::clamp((point.x - axis.origin.x) / axis.size.width, 0.0, 1.0);
        [self setParam:static_cast<clap_id>(_dragSlider) value:norm * 2.0 - 1.0]; return;
    }
    const double norm = std::clamp((point.x
        - s3g::gui_layout::processorControlX(kLayout.output.frame.x))
        / s3g::gui_layout::processorTrackWidth(kLayout.output.frame.width), 0.0, 1.0);
    switch (_dragSlider) {
    case kParamPrimary: [self setParam:kParamPrimary value:kPitch
        ? -24.0 + norm * 48.0 : -60.0 + norm * 78.0]; break;
    case kParamWindow: [self setParam:kParamWindow value:20.0 + norm * 160.0]; break;
    case kParamGlide: [self setParam:kParamGlide value:10.0 * std::pow(200.0, norm)]; break;
    case kParamSpread: [self setParam:kParamSpread value:norm]; break;
    case kParamDeviation: [self setParam:kParamDeviation value:norm]; break;
    case kParamTopologyAmount: [self setParam:kParamTopologyAmount value:norm]; break;
    case kParamRoamingRate: [self setParam:kParamRoamingRate value:.005 * std::pow(400.0, norm)]; break;
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
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (s3g::clap_gui::handleProcessorTitleClick(point, &p->plugin,
        [NSString stringWithUTF8String:kPluginName],
        s3g::gui_layout::transformTitleBand(kCanvas),
        p->presetName, sizeof(p->presetName))) {
        [self setNeedsDisplay:YES]; return;
    }
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
    const NSRect panel = s3g::clap_gui::cocoaRect(kFieldPanel);
    for (uint32_t i = 0u; i < 3u; ++i) {
        if (NSPointInRect(point,
            s3g::clap_gui::topologyProcessorCameraButtonRect(panel, i))) {
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
            [self setParam:kParamPickupPrimaryFirst + node value:0.0];
            if (kPitch) [self setParam:kParamPickupSecondaryFirst + node value:0.0];
        }
        [self setNeedsDisplay:YES]; return;
    }
    const auto beginPickup = [&](NSRect axis, clap_id id) {
        if (!NSPointInRect(point, NSInsetRect(axis, 0.0, -8.0))) return false;
        double initial = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(event, &p->plugin, id, &initial)) {
            [self setParam:id value:initial]; _dragSlider = -1;
        } else { _dragSlider = static_cast<int>(id); [self updateSliderAtPoint:point]; }
        return true;
    };
    if (beginPickup(primaryAxisRect(), kParamPickupPrimaryFirst + _selectedPickup)) return;
    if (kPitch && beginPickup(secondaryAxisRect(),
        kParamPickupSecondaryFirst + _selectedPickup)) return;

    struct MenuHit { s3g::gui_layout::Rect rect; int menu; uint32_t items; };
    const MenuHit menus[] {
        { s3g::gui_layout::sliderHitRect(kLayout.output, 1u), 1, 7u },
        { s3g::gui_layout::sliderHitRect(kEnginePanel, 0u), 2, 4u },
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
    std::array<SliderHit, 18u> sliders {{
        { s3g::gui_layout::sliderHitRect(kLayout.output, 0u), kParamOutput },
        { s3g::gui_layout::sliderHitRect(kEnginePanel, 1u), kParamPrimary },
        { s3g::gui_layout::sliderHitRect(kEnginePanel, 2u), kPitch ? kParamWindow : kParamSpread },
        { s3g::gui_layout::sliderHitRect(kEnginePanel, 3u), kPitch ? kParamGlide : kParamDeviation },
        { s3g::gui_layout::sliderHitRect(kEnginePanel, 4u), kPitch ? kParamSpread : kParamMix },
        { s3g::gui_layout::sliderHitRect(kEnginePanel, 5u), kPitch ? kParamDeviation : CLAP_INVALID_ID },
        { s3g::gui_layout::sliderHitRect(kEnginePanel, 6u), kPitch ? kParamMix : CLAP_INVALID_ID },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 1u), kParamTopologyAmount },
        { s3g::gui_layout::sliderHitRect(kTopologyPanel, 2u), kParamRoamingRate },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 0u), kParamMaskAmount },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 1u), kParamMaskAzimuth },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 2u), kParamMaskElevation },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 3u), kParamMaskWidth },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 4u), kParamMaskCurve },
        { s3g::gui_layout::sliderHitRect(kMaskPanel, 5u), kParamMaskDry },
        {}, {}, {},
    }};
    for (const auto& slider : sliders) {
        if (slider.param == CLAP_INVALID_ID || slider.param == 0u
            || !NSPointInRect(point, s3g::clap_gui::cocoaRect(slider.rect))) continue;
        double initial = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
            event, &p->plugin, slider.param, &initial)) {
            [self setParam:slider.param value:initial]; _dragSlider = -1;
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
        _viewAzDeg += (point.x - _lastDragPoint.x) * .35;
        _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * .35,
            -85.0, 85.0);
        _viewMode = -1; _lastDragPoint = point;
        [self storeViewState]; [self setNeedsDisplay:YES]; return;
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
{ return !floating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
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
    p->guiView = [[S3GAmbiEffectPitchGainView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiEffectPitchGainView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h)
{ return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{ return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h)
{ return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h)
{ return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
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
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false;
    p->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectPitchGainView*>(p->guiView) startRefreshTimer]; return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiEffectPitchGainView*>(p->guiView) stopRefreshTimer];
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
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}
const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT, kPluginId, kPluginName, "s3g",
    "https://github.com/s3g/s3g-dsp", "", "", "0.1.0",
    kDescription, features,
};
const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host; p->params = sanitize(p->params); updateProcessor(*p);
    p->plugin.desc = &descriptor; p->plugin.plugin_data = p;
    p->plugin.init = init; p->plugin.destroy = destroy;
    p->plugin.activate = activate; p->plugin.deactivate = deactivate;
    p->plugin.start_processing = startProcessing;
    p->plugin.stop_processing = stopProcessing; p->plugin.reset = reset;
    p->plugin.process = process; p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{ return index == 0u ? &descriptor : nullptr; }
const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
};
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id)
{ return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }
} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
