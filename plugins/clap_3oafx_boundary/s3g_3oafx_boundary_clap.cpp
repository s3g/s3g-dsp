#include "s3g_3oafx.h"
#include "s3g_math.h"

#include <clap/clap.h>
#include "s3g_realtime.h"
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
#include <cstdint>
#include <new>

namespace {

enum class PluginMode {
    Send,
    Return
};

constexpr uint32_t kStateVersion = 1;
constexpr clap_id kAzParamId = 1;
constexpr clap_id kElParamId = 2;
constexpr clap_id kSmoothParamId = 3;
constexpr clap_id kWidthParamId = 4;
constexpr clap_id kFocusParamId = 5;
constexpr clap_id kLevelParamId = 6;
constexpr clap_id kFloorParamId = 7;
constexpr clap_id kRearParamId = 8;
constexpr clap_id kCompParamId = 9;
constexpr clap_id kGammaParamId = 10;
constexpr clap_id kModeParamId = 11;
constexpr clap_id kWetTrimParamId = 12;
constexpr clap_id kDryTrimParamId = 13;
constexpr clap_id kOutTrimParamId = 14;
constexpr clap_id kContrastParamId = 15;
constexpr clap_id kCeilingParamId = 16;
constexpr clap_id kDuckParamId = 17;
constexpr clap_id kLimiterParamId = 18;
constexpr uint32_t kParamCount = 18;

struct __attribute__((packed)) SavedState {
    uint32_t version = kStateVersion;
    double azimuth = 0.0;
    double elevation = 0.0;
    double smooth = 0.20;
    double width = 0.75;
    double focus = 0.05;
    double level = 1.0;
    double floor = 0.02;
    double rear = 1.0;
    double comp = 0.65;
    double gamma = 1.25;
    double mode = 1.0;
    double wet = 1.0;
    double dry = 0.0;
    double out = 0.90;
    double contrast = 0.10;
    double ceiling = 0.92;
    double duck = 0.0;
    double limiter = 0.15;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    PluginMode mode = PluginMode::Send;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    SavedState params {};
    s3g::AedMaskState sendMask;
    s3g::AedMaskState returnMask;
    s3g::MixerState mixer;
    std::array<std::atomic<float>, s3g::k3OafxVirtualSpeakers> meterMask {};
    std::array<std::atomic<double>, kParamCount> pendingGuiValues {};
    std::array<std::atomic<bool>, kParamCount> pendingGuiValue {};
    std::array<std::atomic<bool>, kParamCount> pendingGuiGestureBegin {};
    std::array<std::atomic<bool>, kParamCount> pendingGuiGestureEnd {};
    std::atomic<float> outputPeak { 0.0f };
    uint32_t meterRedrawCountdown = 0;
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiDirty { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}


double clampParam(clap_id id, double value)
{
    switch (id) {
    case kAzParamId: return std::clamp(value, -179.9, 179.9);
    case kElParamId: return std::clamp(value, -90.0, 90.0);
    case kSmoothParamId:
    case kWidthParamId:
    case kFocusParamId:
    case kLevelParamId:
    case kRearParamId:
    case kCompParamId:
    case kModeParamId:
    case kWetTrimParamId:
    case kDryTrimParamId:
    case kOutTrimParamId:
    case kContrastParamId:
    case kDuckParamId:
    case kLimiterParamId:
        return std::clamp(value, 0.0, 1.0);
    case kFloorParamId: return std::clamp(value, 0.0, 0.25);
    case kGammaParamId: return std::clamp(value, 0.25, 4.0);
    case kCeilingParamId: return std::clamp(value, 0.5, 1.0);
    default: return value;
    }
}

double* paramPtr(Plugin& p, clap_id id)
{
    switch (id) {
    case kAzParamId: return &p.params.azimuth;
    case kElParamId: return &p.params.elevation;
    case kSmoothParamId: return &p.params.smooth;
    case kWidthParamId: return &p.params.width;
    case kFocusParamId: return &p.params.focus;
    case kLevelParamId: return &p.params.level;
    case kFloorParamId: return &p.params.floor;
    case kRearParamId: return &p.params.rear;
    case kCompParamId: return &p.params.comp;
    case kGammaParamId: return &p.params.gamma;
    case kModeParamId: return &p.params.mode;
    case kWetTrimParamId: return &p.params.wet;
    case kDryTrimParamId: return &p.params.dry;
    case kOutTrimParamId: return &p.params.out;
    case kContrastParamId: return &p.params.contrast;
    case kCeilingParamId: return &p.params.ceiling;
    case kDuckParamId: return &p.params.duck;
    case kLimiterParamId: return &p.params.limiter;
    default: return nullptr;
    }
}

int paramIndexForId(clap_id id)
{
    return id >= kAzParamId && id <= kLimiterParamId ? static_cast<int>(id - kAzParamId) : -1;
}

s3g::AedMaskParams maskParams(const Plugin& p)
{
    s3g::AedMaskParams out {};
    out.azimuthDeg = static_cast<float>(p.params.azimuth);
    out.elevationDeg = static_cast<float>(p.params.elevation);
    out.smoothing = static_cast<float>(p.params.smooth);
    out.width = static_cast<float>(p.params.width);
    out.focus = static_cast<float>(p.params.focus);
    out.level = static_cast<float>(p.params.level);
    out.floor = static_cast<float>(p.params.floor);
    out.rearReject = static_cast<float>(p.params.rear);
    out.energyComp = static_cast<float>(p.params.comp);
    out.gamma = static_cast<float>(p.params.gamma);
    return out;
}

s3g::MixerParams mixerParams(const Plugin& p)
{
    s3g::MixerParams out {};
    out.wetTrim = static_cast<float>(p.params.wet);
    out.dryTrim = static_cast<float>(p.params.dry);
    out.outputTrim = static_cast<float>(p.params.out);
    out.maskContrast = static_cast<float>(p.params.contrast);
    out.maskCeiling = static_cast<float>(p.params.ceiling);
    out.duckCurve = static_cast<float>(p.params.duck);
    out.wetLimiter = static_cast<float>(p.params.limiter);
    out.insertDuck = p.params.dry > 0.0001;
    out.useIncomingMask = p.params.mode >= 0.5;
    return out;
}

void setParam(Plugin& p, clap_id id, double value)
{
    if (double* ptr = paramPtr(p, id)) {
        const double clamped = clampParam(id, value);
        if (std::fabs(*ptr - clamped) > 0.000001) {
            *ptr = clamped;
#if defined(__APPLE__)
            const bool wasDirty = p.guiDirty.exchange(true, std::memory_order_release);
            if (!wasDirty && p.host && p.host->request_callback) {
                p.host->request_callback(p.host);
            }
#endif
        }
    }
}

void requestParamFlush(Plugin& p)
{
    if (p.hostParams && p.hostParams->request_flush) {
        p.hostParams->request_flush(p.host);
    }
}

void queueGuiParamChange(Plugin& p, clap_id id, double value)
{
    setParam(p, id, value);
    const int index = paramIndexForId(id);
    if (index < 0) {
        return;
    }
    p.pendingGuiValues[static_cast<size_t>(index)].store(clampParam(id, value), std::memory_order_release);
    p.pendingGuiValue[static_cast<size_t>(index)].store(true, std::memory_order_release);
    requestParamFlush(p);
}

void queueGuiGesture(Plugin& p, clap_id id, bool begin)
{
    const int index = paramIndexForId(id);
    if (index < 0) {
        return;
    }
    if (begin) {
        p.pendingGuiGestureBegin[static_cast<size_t>(index)].store(true, std::memory_order_release);
    } else {
        p.pendingGuiGestureEnd[static_cast<size_t>(index)].store(true, std::memory_order_release);
    }
    requestParamFlush(p);
}

bool pushGestureEvent(const clap_output_events_t* out, clap_id id, uint16_t type)
{
    if (!out || !out->try_push) {
        return false;
    }
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = type;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = id;
    return out->try_push(out, &event.header);
}

bool pushValueEvent(const clap_output_events_t* out, clap_id id, double value)
{
    if (!out || !out->try_push) {
        return false;
    }
    clap_event_param_value_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = id;
    event.cookie = nullptr;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    return out->try_push(out, &event.header);
}

void emitPendingGuiEvents(Plugin& p, const clap_output_events_t* out)
{
    if (!out || !out->try_push) {
        return;
    }
    for (uint32_t i = 0; i < kParamCount; ++i) {
        const clap_id id = kAzParamId + i;
        if (p.pendingGuiGestureBegin[i].exchange(false, std::memory_order_acq_rel)) {
            pushGestureEvent(out, id, CLAP_EVENT_PARAM_GESTURE_BEGIN);
        }
        if (p.pendingGuiValue[i].exchange(false, std::memory_order_acq_rel)) {
            pushValueEvent(out, id, p.pendingGuiValues[i].load(std::memory_order_acquire));
        }
        if (p.pendingGuiGestureEnd[i].exchange(false, std::memory_order_acq_rel)) {
            pushGestureEvent(out, id, CLAP_EVENT_PARAM_GESTURE_END);
        }
    }
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) {
        return;
    }
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParam(p, param->param_id, param->value);
        }
    }
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    s3g::clap_support::endRealtimeActivity(p->macRealtimeActivity);
    if (p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(p->macRealtimeActivity);
#endif
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->meterRedrawCountdown = static_cast<uint32_t>(std::max(1.0, sampleRate / 24.0));
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#endif
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->sendMask = {};
    p->sendMask.target = { 1.0f, 0.0f, 0.0f };
    p->returnMask = p->sendMask;
    p->mixer = {};
}

void onMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiDirty.exchange(false, std::memory_order_acquire) && p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
        [view setNeedsDisplay:YES];
    }
#else
    (void)plugin;
#endif
}

void updateMeters(Plugin& p, const float* mask, const float* output, uint32_t outputCount)
{
    for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
        p.meterMask[i].store(mask ? mask[i] : 0.0f, std::memory_order_relaxed);
    }
    float peak = 0.0f;
    for (uint32_t i = 0; i < outputCount; ++i) {
        peak = std::max(peak, std::fabs(output[i]));
    }
    p.outputPeak.store(peak, std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    emitPendingGuiEvents(*p, proc->out_events);
    if (proc->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& out = proc->audio_outputs[0];
    const clap_audio_buffer_t* in = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
    const uint32_t frames = proc->frames_count;
    const uint32_t requiredIn = p->mode == PluginMode::Send ? s3g::k3OaChannels : s3g::k3OafxBusChannels;
    const uint32_t requiredOut = p->mode == PluginMode::Send ? s3g::k3OafxBusChannels : s3g::k3OaChannels;

    if (out.data32) {
        for (uint32_t ch = 0; ch < out.channel_count; ++ch) {
            if (out.data32[ch]) {
                std::fill(out.data32[ch], out.data32[ch] + frames, 0.0f);
            }
        }
    }
    if (out.data64) {
        for (uint32_t ch = 0; ch < out.channel_count; ++ch) {
            if (out.data64[ch]) {
                std::fill(out.data64[ch], out.data64[ch] + frames, 0.0);
            }
        }
    }

    float inFrame[s3g::k3OafxBusChannels] {};
    float outFrame[s3g::k3OafxBusChannels] {};
    float maskFrame[s3g::k3OafxVirtualSpeakers] {};
    const auto mp = maskParams(*p);
    const auto mix = mixerParams(*p);

    for (uint32_t frame = 0; frame < frames; ++frame) {
        std::fill(inFrame, inFrame + s3g::k3OafxBusChannels, 0.0f);
        if (in) {
            const uint32_t readable = std::min(requiredIn, in->channel_count);
            for (uint32_t ch = 0; ch < readable; ++ch) {
                if (in->data32 && in->data32[ch]) {
                    inFrame[ch] = in->data32[ch][frame];
                } else if (in->data64 && in->data64[ch]) {
                    inFrame[ch] = static_cast<float>(in->data64[ch][frame]);
                }
            }
        }

        if (p->mode == PluginMode::Send) {
            s3g::process3OafxSendFrame(inFrame, outFrame, p->sendMask, mp, p->params.mode >= 0.5, true);
            for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
                maskFrame[i] = outFrame[i + 48];
            }
        } else {
            s3g::process3OafxReturnFrame(inFrame, outFrame, p->returnMask, p->mixer, mp, mix);
            for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
                maskFrame[i] = inFrame[i + 48];
            }
        }

        const uint32_t writable = std::min(requiredOut, out.channel_count);
        for (uint32_t ch = 0; ch < writable; ++ch) {
            if (out.data32 && out.data32[ch]) {
                out.data32[ch][frame] = outFrame[ch];
            }
            if (out.data64 && out.data64[ch]) {
                out.data64[ch][frame] = outFrame[ch];
            }
        }
    }
    updateMeters(*p, maskFrame, outFrame, requiredOut);
    if (p->meterRedrawCountdown <= frames) {
        p->meterRedrawCountdown = static_cast<uint32_t>(std::max(1.0, p->sampleRate / 24.0));
#if defined(__APPLE__)
        const bool wasDirty = p->guiDirty.exchange(true, std::memory_order_release);
        if (!wasDirty && p->host && p->host->request_callback) {
            p->host->request_callback(p->host);
        }
#endif
    } else {
        p->meterRedrawCountdown -= frames;
    }
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) {
        return false;
    }
    const auto* p = self(plugin);
    const bool send = p->mode == PluginMode::Send;
    const uint32_t channels = isInput
        ? (send ? s3g::k3OaChannels : s3g::k3OafxBusChannels)
        : (send ? s3g::k3OafxBusChannels : s3g::k3OaChannels);
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? (send ? "3OA In" : "3OAFX Bus In") : (send ? "3OAFX Bus Out" : "3OA Out"), sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = channels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double min;
    double max;
    double def;
};

constexpr ParamDef kParams[] = {
    { kAzParamId, "Azimuth", "Mask", -179.9, 179.9, 0.0 },
    { kElParamId, "Elevation", "Mask", -90.0, 90.0, 0.0 },
    { kSmoothParamId, "Smoothing", "Mask", 0.0, 1.0, 0.20 },
    { kWidthParamId, "Width", "Mask", 0.0, 1.0, 0.75 },
    { kFocusParamId, "Focus", "Mask", 0.0, 1.0, 0.05 },
    { kLevelParamId, "Level", "Mask", 0.0, 1.0, 1.0 },
    { kFloorParamId, "Beam Floor", "Mask", 0.0, 0.25, 0.02 },
    { kRearParamId, "Rear Reject", "Mask", 0.0, 1.0, 1.0 },
    { kCompParamId, "Energy Comp", "Mask", 0.0, 1.0, 0.65 },
    { kGammaParamId, "Mask Gamma", "Mask", 0.25, 4.0, 1.25 },
    { kModeParamId, "Dry Copy / Incoming Mask", "Routing", 0.0, 1.0, 1.0 },
    { kWetTrimParamId, "Wet Trim", "Mixer", 0.0, 1.0, 1.0 },
    { kDryTrimParamId, "Dry Trim", "Mixer", 0.0, 1.0, 0.0 },
    { kOutTrimParamId, "Output Trim", "Mixer", 0.0, 1.0, 0.90 },
    { kContrastParamId, "Mask Contrast", "Mixer", 0.0, 1.0, 0.10 },
    { kCeilingParamId, "Mask Ceiling", "Mixer", 0.5, 1.0, 0.92 },
    { kDuckParamId, "Duck Curve", "Mixer", 0.0, 1.0, 0.0 },
    { kLimiterParamId, "Wet Limiter", "Mixer", 0.0, 1.0, 0.15 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParams) / sizeof(kParams[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) {
        return false;
    }
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, def.module, sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) {
        return false;
    }
    auto* p = self(plugin);
    if (double* ptr = paramPtr(*p, paramId)) {
        *value = *ptr;
        return true;
    }
    return false;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) {
        return false;
    }
    if (paramId == kAzParamId || paramId == kElParamId) {
        std::snprintf(display, size, "%.1f deg", value);
    } else if (paramId == kModeParamId) {
        std::snprintf(display, size, value >= 0.5 ? "On" : "Off");
    } else {
        std::snprintf(display, size, "%.3f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) {
        return false;
    }
    if (paramId == kModeParamId) {
        if (std::strcmp(display, "On") == 0 || std::strcmp(display, "on") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "Off") == 0 || std::strcmp(display, "off") == 0) {
            *value = 0.0;
            return true;
        }
    }
    *value = clampParam(paramId, std::atof(display));
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
{
    auto* p = self(plugin);
    readParamEvents(*p, in);
    emitPendingGuiEvents(*p, out);
}

const clap_plugin_params_t params {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

bool writeFull(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* cursor = static_cast<const uint8_t*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        const int64_t written = stream->write(stream, cursor, remaining);
        if (written <= 0) {
            return false;
        }
        cursor += written;
        remaining -= static_cast<size_t>(written);
    }
    return true;
}

bool readFull(const clap_istream_t* stream, void* data, size_t size)
{
    auto* cursor = static_cast<uint8_t*>(data);
    size_t remaining = size;
    while (remaining > 0) {
        const int64_t read = stream->read(stream, cursor, remaining);
        if (read <= 0) {
            return false;
        }
        cursor += read;
        remaining -= static_cast<size_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) {
        return false;
    }
    auto state = self(plugin)->params;
    state.version = kStateVersion;
    return writeFull(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) {
        return false;
    }
    SavedState state {};
    if (!readFull(stream, &state, sizeof(state)) || state.version != kStateVersion) {
        return false;
    }
    auto* p = self(plugin);
    p->params = state;
    for (const auto& def : kParams) {
        if (double* ptr = paramPtr(*p, def.id)) {
            *ptr = clampParam(def.id, *ptr);
        }
    }
    return true;
}

const clap_plugin_state_t state { stateSave, stateLoad };

#if defined(__APPLE__)

} // namespace

static NSColor* color(int rgb, double alpha = 1.0)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:alpha];
}

@interface S3G3OAFXBoundaryView : NSView {
@private
    void* _plugin;
    int _dragSlider;
    bool _dragMap;
    clap_id _gestureParamA;
    clap_id _gestureParamB;
}
- (id)initWithPlugin:(void*)plugin;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs dim:(NSDictionary*)dim;
- (NSPoint)project:(s3g::Vec3)p rect:(NSRect)rect;
- (void)setDirectionFromPoint:(NSPoint)pt;
- (void)updateSlider:(NSPoint)pt;
@end

@implementation S3G3OAFXBoundaryView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 560)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _dragMap = false;
        _gestureParamA = CLAP_INVALID_ID;
        _gestureParamB = CLAP_INVALID_ID;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs dim:(NSDictionary*)dim
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, dim, attrs, style, 596, 674, 818, 132);
}

- (NSPoint)project:(s3g::Vec3)p rect:(NSRect)rect
{
    const double yaw = -0.58;
    const double pitch = 0.62;
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double x1 = p.x * cy - p.y * sy;
    const double y1 = p.x * sy + p.y * cy;
    const double y2 = y1 * cp - p.z * sp;
    const double z2 = y1 * sp + p.z * cp;
    const double scale = 0.82 + z2 * 0.10;
    return NSMakePoint(rect.origin.x + rect.size.width * 0.5 + x1 * rect.size.width * 0.34 * scale,
                       rect.origin.y + rect.size.height * 0.54 - y2 * rect.size.height * 0.34 * scale);
}

- (void)setDirectionFromPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    NSRect map = NSMakeRect(22, 58, 540, 460);
    const double dx = (pt.x - (map.origin.x + map.size.width * 0.5)) / (map.size.width * 0.34);
    const double dy = ((map.origin.y + map.size.height * 0.54) - pt.y) / (map.size.height * 0.34);
    queueGuiParamChange(*p, kAzParamId, std::clamp(std::atan2(dy, dx) * 180.0 / M_PI, -179.9, 179.9));
    queueGuiParamChange(*p, kElParamId, std::clamp((map.origin.y + map.size.height * 0.5 - pt.y) / (map.size.height * 0.5) * 90.0, -90.0, 90.0));
    [self setNeedsDisplay:YES];
}

- (void)updateSlider:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider < 0) {
        return;
    }
    const double n = std::clamp((pt.x - 674.0) / 132.0, 0.0, 1.0);
    const clap_id ids[] = { kAzParamId, kElParamId, kWidthParamId, kFocusParamId, kLevelParamId, kSmoothParamId, kFloorParamId, kRearParamId, kModeParamId, kWetTrimParamId, kDryTrimParamId, kOutTrimParamId, kContrastParamId, kCeilingParamId, kDuckParamId, kLimiterParamId };
    const clap_id id = ids[std::min<int>(_dragSlider, 15)];
    double value = n;
    if (id == kAzParamId) value = -179.9 + n * 359.8;
    else if (id == kElParamId) value = -90.0 + n * 180.0;
    else if (id == kFloorParamId) value = n * 0.25;
    else if (id == kCeilingParamId) value = 0.5 + n * 0.5;
    else if (id == kModeParamId) value = n >= 0.5 ? 1.0 : 0.0;
    queueGuiParamChange(*p, id, value);
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* bold = [NSFont fontWithName:@"Menlo-Bold" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightBold];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10.5] ?: [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    NSDictionary* attrs = @{ NSForegroundColorAttributeName: style.text, NSFontAttributeName: mono };
    NSDictionary* dim = @{ NSForegroundColorAttributeName: style.dim, NSFontAttributeName: mono };
    NSDictionary* head = @{ NSForegroundColorAttributeName: style.text, NSFontAttributeName: bold };
    NSDictionary* titleAttrs = @{ NSForegroundColorAttributeName: style.text, NSFontAttributeName: titleFont };
    NSString* title = p->mode == PluginMode::Send ? @"s3g 3OAFX SEND DECODER" : @"s3g 3OAFX RETURN ENCODER";
    [title drawAtPoint:NSMakePoint(18, 16) withAttributes:titleAttrs];
    [[NSString stringWithFormat:@"PK %.3f", p->outputPeak.load(std::memory_order_relaxed)] drawAtPoint:NSMakePoint(782, 16) withAttributes:dim];

    NSRect panel = NSMakeRect(12, 42, 560, 500);
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"24-POINT MASK SHELL", true, panel.origin.x, panel.origin.y, panel.size.width, 21, head, style);
    NSRect map = NSMakeRect(22, 78, 540, 430);
    [color(0x101010) setFill];
    NSRectFill(map);
    [color(0x565656) setStroke];
    NSFrameRect(map);
    NSPoint center = NSMakePoint(map.origin.x + map.size.width * 0.5, map.origin.y + map.size.height * 0.54);
    [color(0x333333) setStroke];
    NSBezierPath* circle = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(center.x - 150, center.y - 150, 300, 300)];
    [circle stroke];

    const auto mp = maskParams(*p);
    const s3g::Vec3 target = s3g::directionFromAed(mp.azimuthDeg, mp.elevationDeg);
    float predicted[s3g::k3OafxVirtualSpeakers] {};
    s3g::computeMask(target, mp, predicted);
    std::array<NSPoint, s3g::k3OafxVirtualSpeakers> points {};
    for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
        points[i] = [self project:s3g::k3OafxPoints[i] rect:map];
    }
    for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
        for (uint32_t j = i + 1; j < s3g::k3OafxVirtualSpeakers; ++j) {
            const auto a = s3g::k3OafxPoints[i];
            const auto b = s3g::k3OafxPoints[j];
            const float d = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
            if (d < 0.34f) {
                [color(0x777777, 0.14) setStroke];
                [NSBezierPath strokeLineFromPoint:points[i] toPoint:points[j]];
            }
        }
    }
    NSPoint dirPoint = [self project:target rect:map];
    [color(0xb0b0b0) setStroke];
    [NSBezierPath strokeLineFromPoint:center toPoint:dirPoint];
    [color(0xf0f0f0) setFill];
    NSRectFill(NSMakeRect(dirPoint.x - 5, dirPoint.y - 5, 10, 10));
    for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
        const float live = p->meterMask[i].load(std::memory_order_relaxed);
        const float m = p->mode == PluginMode::Return && p->params.mode >= 0.5 ? live : predicted[i];
        const int gray = static_cast<int>(0x52 + std::clamp(m, 0.0f, 1.0f) * 0xad);
        [color((gray << 16) | (gray << 8) | gray) setFill];
        const CGFloat r = 3.0 + m * 9.0;
        NSRectFill(NSMakeRect(points[i].x - r * 0.5, points[i].y - r * 0.5, r, r));
        [[NSString stringWithFormat:@"%u", i + 1] drawAtPoint:NSMakePoint(points[i].x + 7, points[i].y - 6) withAttributes:dim];
    }
    [[NSString stringWithFormat:@"AZ %+5.1f   EL %+5.1f", p->params.azimuth, p->params.elevation] drawAtPoint:NSMakePoint(34, 516) withAttributes:dim];
    [p->mode == PluginMode::Send ? @"LANES 1-24 WET / 25-48 DRY / 49-72 MASK" : @"LNK OFF: LOCAL MASK FROM LINKED AZ/EL"
        drawAtPoint:NSMakePoint(300, 516) withAttributes:dim];

    s3g::clap_gui::drawPanelFrame(584, 42, 304, 500, style);
    s3g::clap_gui::drawPanelHeader(@"CONTROL", true, 584, 42, 304, 21, head, style);
    struct Slider { const char* label; clap_id id; };
    const Slider common[] = {
        { "AZ", kAzParamId }, { "EL", kElParamId }, { "WID", kWidthParamId }, { "FOC", kFocusParamId },
        { "LVL", kLevelParamId }, { "SMT", kSmoothParamId }, { "FLR", kFloorParamId }, { "REJ", kRearParamId },
        { p->mode == PluginMode::Send ? "DRY" : "LNK", kModeParamId },
    };
    CGFloat y = 84;
    for (uint32_t i = 0; i < sizeof(common) / sizeof(common[0]); ++i) {
        double value = *paramPtr(*p, common[i].id);
        double norm = value;
        if (common[i].id == kAzParamId) norm = (value + 179.9) / 359.8;
        else if (common[i].id == kElParamId) norm = (value + 90.0) / 180.0;
        else if (common[i].id == kFloorParamId) norm = value / 0.25;
        NSString* text = common[i].id == kAzParamId || common[i].id == kElParamId ? [NSString stringWithFormat:@"%+5.1f", value] : [NSString stringWithFormat:@"%.2f", value];
        [self drawSlider:[NSString stringWithUTF8String:common[i].label] value:text norm:norm y:y attrs:attrs dim:dim];
        y += 24;
    }
    if (p->mode == PluginMode::Return) {
        [@"MIXER" drawAtPoint:NSMakePoint(596, y + 16) withAttributes:head];
        y += 48;
        const Slider ret[] = {
            { "WET", kWetTrimParamId }, { "DRY", kDryTrimParamId }, { "OUT", kOutTrimParamId },
            { "CON", kContrastParamId }, { "CEI", kCeilingParamId }, { "DCK", kDuckParamId }, { "LIM", kLimiterParamId },
        };
        for (uint32_t i = 0; i < sizeof(ret) / sizeof(ret[0]); ++i) {
            double value = *paramPtr(*p, ret[i].id);
            double norm = ret[i].id == kCeilingParamId ? (value - 0.5) / 0.5 : value;
            [self drawSlider:[NSString stringWithUTF8String:ret[i].label] value:[NSString stringWithFormat:@"%.2f", value] norm:norm y:y attrs:attrs dim:dim];
            y += 24;
        }
    } else {
        [@"OUTPUT LANES" drawAtPoint:NSMakePoint(596, y + 16) withAttributes:head];
        [@"1-24  WET MASKED" drawAtPoint:NSMakePoint(596, y + 42) withAttributes:dim];
        [@"25-48 DRY COPY" drawAtPoint:NSMakePoint(596, y + 60) withAttributes:dim];
        [@"49-72 MASK SIGNAL" drawAtPoint:NSMakePoint(596, y + 78) withAttributes:dim];
    }
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (NSPointInRect(pt, NSMakeRect(22, 78, 540, 430))) {
        _dragMap = true;
        _gestureParamA = kAzParamId;
        _gestureParamB = kElParamId;
        auto* p = static_cast<Plugin*>(_plugin);
        queueGuiGesture(*p, _gestureParamA, true);
        queueGuiGesture(*p, _gestureParamB, true);
        [self setDirectionFromPoint:pt];
        return;
    }
    CGFloat y = 84;
    const int commonCount = 9;
    for (int i = 0; i < commonCount; ++i) {
        if (NSPointInRect(pt, NSMakeRect(590, y - 6, 294, 22))) {
            _dragSlider = i;
            _gestureParamA = i == 0 ? kAzParamId : i == 1 ? kElParamId : i == 2 ? kWidthParamId : i == 3 ? kFocusParamId :
                i == 4 ? kLevelParamId : i == 5 ? kSmoothParamId : i == 6 ? kFloorParamId : i == 7 ? kRearParamId : kModeParamId;
            _gestureParamB = CLAP_INVALID_ID;
            auto* p = static_cast<Plugin*>(_plugin);
            queueGuiGesture(*p, _gestureParamA, true);
            [self updateSlider:pt];
            return;
        }
        y += 24;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    if (p->mode == PluginMode::Return) {
        y += 48;
        for (int i = 0; i < 7; ++i) {
            if (NSPointInRect(pt, NSMakeRect(590, y - 6, 294, 22))) {
                _dragSlider = commonCount + i;
                const clap_id retIds[] = { kWetTrimParamId, kDryTrimParamId, kOutTrimParamId, kContrastParamId, kCeilingParamId, kDuckParamId, kLimiterParamId };
                _gestureParamA = retIds[i];
                _gestureParamB = CLAP_INVALID_ID;
                queueGuiGesture(*p, _gestureParamA, true);
                [self updateSlider:pt];
                return;
            }
            y += 24;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragMap) {
        [self setDirectionFromPoint:pt];
    } else {
        [self updateSlider:pt];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    auto* p = static_cast<Plugin*>(_plugin);
    if (_gestureParamA != CLAP_INVALID_ID) {
        queueGuiGesture(*p, _gestureParamA, false);
    }
    if (_gestureParamB != CLAP_INVALID_ID) {
        queueGuiGesture(*p, _gestureParamB, false);
    }
    _dragSlider = -1;
    _dragMap = false;
    _gestureParamA = CLAP_INVALID_ID;
    _gestureParamB = CLAP_INVALID_ID;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
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
    if (!p->guiView) p->guiView = [[S3G3OAFXBoundaryView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    *width = 900;
    *height = 560;
    return true;
}
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(width, height)];
    return true;
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* p = self(plugin);
    if (!p->guiView) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, 900, 560)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    [static_cast<NSView*>(p->guiView) setHidden:NO];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    [static_cast<NSView*>(p->guiView) setHidden:YES];
    return true;
}

const clap_plugin_gui_t gui {
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
    guiHide
};
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t sendDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.3oafx-send-decoder",
    "s3g 3OAFX Send Decoder",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "3OA ACN/SN3D decoder to 24-channel 3OAFX wet/dry/mask bus.",
    features
};

const clap_plugin_descriptor_t returnDescriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.3oafx-return-encoder",
    "s3g 3OAFX Return Encoder",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "3OAFX 72-channel return mask/mixer and 3OA ACN/SN3D encoder.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    PluginMode mode;
    const clap_plugin_descriptor_t* desc = nullptr;
    if (std::strcmp(pluginId, sendDescriptor.id) == 0) {
        mode = PluginMode::Send;
        desc = &sendDescriptor;
    } else if (std::strcmp(pluginId, returnDescriptor.id) == 0) {
        mode = PluginMode::Return;
        desc = &returnDescriptor;
    } else {
        return nullptr;
    }
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostParams = host && host->get_extension ? static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS)) : nullptr;
    p->mode = mode;
    if (mode == PluginMode::Return) {
        p->params.mode = 0.0;
        p->params.dry = 0.0;
    }
    p->plugin.desc = desc;
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
    reset(&p->plugin);
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 2; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    if (index == 0) return &sendDescriptor;
    if (index == 1) return &returnDescriptor;
    return nullptr;
}

const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) return &factory;
    return nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
