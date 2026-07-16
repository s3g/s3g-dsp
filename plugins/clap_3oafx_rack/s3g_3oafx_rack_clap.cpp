#include "s3g_3oafx_rack.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#if defined(__APPLE__)
#include <clap/ext/gui.h>
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kStateVersion = 6;
constexpr uint32_t kRackHostChannels = s3g::k3OaChannels;

enum ParamId : clap_id {
    kMode = 1,
    kDry,
    kWet,
    kOut,
    kDelayAz,
    kDelayEl,
    kDelayWidth,
    kDelaySend,
    kDelayReturn,
    kDelayTime,
    kDelayFeedback,
    kDelayMix,
    kPitchAz,
    kPitchEl,
    kPitchWidth,
    kPitchSend,
    kPitchReturn,
    kPitchSemis,
    kPitchMix,
    kFilterAz,
    kFilterEl,
    kFilterWidth,
    kFilterTone,
    kFilterMix,
    kDriveAz,
    kDriveEl,
    kDriveWidth,
    kDriveAmount,
    kDriveMix,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double min;
    double max;
    double def;
};

constexpr ParamDef kParams[] {
    { kMode, "Mode", "Rack", 0.0, 1.0, 0.0 },
    { kDry, "Dry", "Rack", 0.0, 1.0, 0.65 },
    { kOut, "Output", "Rack", 0.0, 1.0, 0.90 },
    { kDelayAz, "Delay Azimuth", "Delay Slot", -179.9, 179.9, -30.0 },
    { kDelayEl, "Delay Elevation", "Delay Slot", -90.0, 90.0, 10.0 },
    { kDelayWidth, "Delay Width", "Delay Slot", 0.0, 1.0, 0.78 },
    { kDelaySend, "Delay Send", "Delay Slot", 0.0, 1.0, 1.0 },
    { kDelayReturn, "Delay Return", "Delay Slot", 0.0, 1.0, 1.0 },
    { kDelayTime, "Delay Time", "Delay", 5.0, 2000.0, 320.0 },
    { kDelayFeedback, "Delay Feedback", "Delay", 0.0, 0.78, 0.22 },
    { kDelayMix, "Delay Mix", "Delay", 0.0, 1.0, 0.0 },
    { kPitchAz, "Pitch Azimuth", "Pitch Slot", -179.9, 179.9, 38.0 },
    { kPitchEl, "Pitch Elevation", "Pitch Slot", -90.0, 90.0, -8.0 },
    { kPitchWidth, "Pitch Width", "Pitch Slot", 0.0, 1.0, 0.72 },
    { kPitchSend, "Pitch Send", "Pitch Slot", 0.0, 1.0, 0.70 },
    { kPitchReturn, "Pitch Return", "Pitch Slot", 0.0, 1.0, 1.0 },
    { kPitchSemis, "Pitch", "Pitch", -24.0, 24.0, 0.0 },
    { kPitchMix, "Pitch Mix", "Pitch", 0.0, 1.0, 0.0 },
    { kFilterAz, "Filter Azimuth", "Filter Slot", -179.9, 179.9, -105.0 },
    { kFilterEl, "Filter Elevation", "Filter Slot", -90.0, 90.0, 18.0 },
    { kFilterWidth, "Filter Width", "Filter Slot", 0.0, 1.0, 0.62 },
    { kFilterTone, "Filter Tone", "Filter", 0.0, 1.0, 0.55 },
    { kFilterMix, "Filter Mix", "Filter", 0.0, 1.0, 0.0 },
    { kDriveAz, "Drive Azimuth", "Drive Slot", -179.9, 179.9, 112.0 },
    { kDriveEl, "Drive Elevation", "Drive Slot", -90.0, 90.0, -22.0 },
    { kDriveWidth, "Drive Width", "Drive Slot", 0.0, 1.0, 0.58 },
    { kDriveAmount, "Drive Amount", "Drive", 0.0, 1.0, 0.35 },
    { kDriveMix, "Drive Mix", "Drive", 0.0, 1.0, 0.0 },
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::ThreeOafxRackParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::ThreeOafxRack rack {};
    s3g::ThreeOafxRackParams params {};
    std::array<float, s3g::k3OaChannels> inFrame {};
    std::array<float, s3g::k3OaChannels> outFrame {};
    std::atomic<float> peak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiDirty { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

double clampParam(clap_id id, double value)
{
    for (const auto& def : kParams) {
        if (def.id == id) return std::clamp(value, def.min, def.max);
    }
    return value;
}

void setParam(Plugin& p, clap_id id, double value)
{
    const float v = static_cast<float>(clampParam(id, value));
    switch (id) {
    case kMode: p.params.mode = v >= 0.5f ? s3g::ThreeOafxRackMode::Series : s3g::ThreeOafxRackMode::Parallel; break;
    case kDry: p.params.dry = v; break;
    case kWet: p.params.wet = 1.0f; break;
    case kOut: p.params.output = v; break;
    case kDelayAz: p.params.delay.sendMask.azimuthDeg = v; p.params.delay.returnMask.azimuthDeg = v; break;
    case kDelayEl: p.params.delay.sendMask.elevationDeg = v; p.params.delay.returnMask.elevationDeg = v; break;
    case kDelayWidth: p.params.delay.sendMask.width = v; p.params.delay.returnMask.width = v; break;
    case kDelaySend: p.params.delay.send = v; break;
    case kDelayReturn: p.params.delay.returnGain = v; break;
    case kDelayTime: p.params.delayFx.timeMs = v; break;
    case kDelayFeedback: p.params.delayFx.feedback = v; break;
    case kDelayMix: p.params.delayFx.mix = v; break;
    case kPitchAz: p.params.pitch.sendMask.azimuthDeg = v; p.params.pitch.returnMask.azimuthDeg = v; break;
    case kPitchEl: p.params.pitch.sendMask.elevationDeg = v; p.params.pitch.returnMask.elevationDeg = v; break;
    case kPitchWidth: p.params.pitch.sendMask.width = v; p.params.pitch.returnMask.width = v; break;
    case kPitchSend: p.params.pitch.send = v; break;
    case kPitchReturn: p.params.pitch.returnGain = v; break;
    case kPitchSemis: p.params.pitchFx.pitchSemitones = v; break;
    case kPitchMix: p.params.pitchFx.mix = v; break;
    case kFilterAz: p.params.filter.sendMask.azimuthDeg = v; p.params.filter.returnMask.azimuthDeg = v; break;
    case kFilterEl: p.params.filter.sendMask.elevationDeg = v; p.params.filter.returnMask.elevationDeg = v; break;
    case kFilterWidth: p.params.filter.sendMask.width = v; p.params.filter.returnMask.width = v; break;
    case kFilterTone: p.params.filterTone = v; break;
    case kFilterMix: p.params.filterMix = v; break;
    case kDriveAz: p.params.drive.sendMask.azimuthDeg = v; p.params.drive.returnMask.azimuthDeg = v; break;
    case kDriveEl: p.params.drive.sendMask.elevationDeg = v; p.params.drive.returnMask.elevationDeg = v; break;
    case kDriveWidth: p.params.drive.sendMask.width = v; p.params.drive.returnMask.width = v; break;
    case kDriveAmount: p.params.driveAmount = v; break;
    case kDriveMix: p.params.driveMix = v; break;
    default: return;
    }
    p.rack.setParams(p.params);
    p.params = p.rack.params();
#if defined(__APPLE__)
    p.guiDirty.store(true, std::memory_order_release);
    if (p.host && p.host->request_callback) p.host->request_callback(p.host);
#endif
}

double getParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kMode: return p.params.mode == s3g::ThreeOafxRackMode::Series ? 1.0 : 0.0;
    case kDry: return p.params.dry;
    case kWet: return 1.0;
    case kOut: return p.params.output;
    case kDelayAz: return p.params.delay.sendMask.azimuthDeg;
    case kDelayEl: return p.params.delay.sendMask.elevationDeg;
    case kDelayWidth: return p.params.delay.sendMask.width;
    case kDelaySend: return p.params.delay.send;
    case kDelayReturn: return p.params.delay.returnGain;
    case kDelayTime: return p.params.delayFx.timeMs;
    case kDelayFeedback: return p.params.delayFx.feedback;
    case kDelayMix: return p.params.delayFx.mix;
    case kPitchAz: return p.params.pitch.sendMask.azimuthDeg;
    case kPitchEl: return p.params.pitch.sendMask.elevationDeg;
    case kPitchWidth: return p.params.pitch.sendMask.width;
    case kPitchSend: return p.params.pitch.send;
    case kPitchReturn: return p.params.pitch.returnGain;
    case kPitchSemis: return p.params.pitchFx.pitchSemitones;
    case kPitchMix: return p.params.pitchFx.mix;
    case kFilterAz: return p.params.filter.sendMask.azimuthDeg;
    case kFilterEl: return p.params.filter.sendMask.elevationDeg;
    case kFilterWidth: return p.params.filter.sendMask.width;
    case kFilterTone: return p.params.filterTone;
    case kFilterMix: return p.params.filterMix;
    case kDriveAz: return p.params.drive.sendMask.azimuthDeg;
    case kDriveEl: return p.params.drive.sendMask.elevationDeg;
    case kDriveWidth: return p.params.drive.sendMask.width;
    case kDriveAmount: return p.params.driveAmount;
    case kDriveMix: return p.params.driveMix;
    default: return 0.0;
    }
}

void readEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParam(p, param->param_id, param->value);
        }
    }
}

void initDefaults(Plugin& p)
{
    p.params = {};
    p.params.dry = 0.65f;
    p.params.wet = 1.0f;
    p.params.output = 0.90f;
    p.params.delay.sendMask.azimuthDeg = -30.0f;
    p.params.delay.sendMask.elevationDeg = 10.0f;
    p.params.delay.sendMask.width = 0.78f;
    p.params.delay.sendMask.focus = 0.05f;
    p.params.delay.sendMask.level = 1.0f;
    p.params.delay.returnMask = p.params.delay.sendMask;
    p.params.delay.send = 1.0f;
    p.params.delay.returnGain = 1.0f;
    p.params.delayFx.timeMs = 320.0f;
    p.params.delayFx.feedback = 0.22f;
    p.params.delayFx.mix = 0.0f;
    p.params.pitch.sendMask.azimuthDeg = 38.0f;
    p.params.pitch.sendMask.elevationDeg = -8.0f;
    p.params.pitch.sendMask.width = 0.72f;
    p.params.pitch.sendMask.focus = 0.05f;
    p.params.pitch.sendMask.level = 1.0f;
    p.params.pitch.returnMask = p.params.pitch.sendMask;
    p.params.pitch.send = 0.70f;
    p.params.pitch.returnGain = 1.0f;
    p.params.pitchFx.pitchSemitones = 0.0f;
    p.params.pitchFx.mix = 0.0f;
    p.params.filter.sendMask.azimuthDeg = -105.0f;
    p.params.filter.sendMask.elevationDeg = 18.0f;
    p.params.filter.sendMask.width = 0.62f;
    p.params.filter.sendMask.focus = 0.05f;
    p.params.filter.sendMask.level = 1.0f;
    p.params.filter.returnMask = p.params.filter.sendMask;
    p.params.filter.send = 1.0f;
    p.params.filter.returnGain = 1.0f;
    p.params.filterTone = 0.55f;
    p.params.filterMix = 0.0f;
    p.params.drive.sendMask.azimuthDeg = 112.0f;
    p.params.drive.sendMask.elevationDeg = -22.0f;
    p.params.drive.sendMask.width = 0.58f;
    p.params.drive.sendMask.focus = 0.05f;
    p.params.drive.sendMask.level = 1.0f;
    p.params.drive.returnMask = p.params.drive.sendMask;
    p.params.drive.send = 1.0f;
    p.params.drive.returnGain = 1.0f;
    p.params.driveAmount = 0.35f;
    p.params.driveMix = 0.0f;
    p.rack.setParams(p.params);
    p.params = p.rack.params();
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* view = static_cast<NSView*>(p->guiView);
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->rack.prepare(sampleRate);
    p->rack.setParams(p->params);
    p->params = p->rack.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->rack.reset(); }

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, s3g::k3OaChannels });
    float blockPeak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (input.data32 && input.data32[ch]) p->inFrame[ch] = input.data32[ch][i];
            else if (input.data64 && input.data64[ch]) p->inFrame[ch] = static_cast<float>(input.data64[ch][i]);
            else p->inFrame[ch] = 0.0f;
        }
        for (uint32_t ch = channels; ch < s3g::k3OaChannels; ++ch) p->inFrame[ch] = 0.0f;
        p->rack.processFrame(p->inFrame.data(), p->outFrame.data());
        for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
            const float value = ch < s3g::k3OaChannels ? p->outFrame[ch] : 0.0f;
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = value;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(value);
            if (ch < s3g::k3OaChannels) blockPeak = std::max(blockPeak, std::fabs(value));
        }
    }
    p->peak.store(std::max(p->peak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiDirty.exchange(false, std::memory_order_acquire) && p->guiView) {
        [static_cast<NSView*>(p->guiView) setNeedsDisplay:YES];
    }
#else
    (void)plugin;
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "3OA %s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kRackHostChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParams) / sizeof(kParams[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
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
    if (!value) return false;
    *value = getParam(*self(plugin), paramId);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (paramId == kMode) std::snprintf(display, size, value >= 0.5 ? "SER" : "PAR");
    else if (paramId == kDelayAz || paramId == kDelayEl ||
             paramId == kPitchAz || paramId == kPitchEl ||
             paramId == kFilterAz || paramId == kFilterEl ||
             paramId == kDriveAz || paramId == kDriveEl) std::snprintf(display, size, "%+.1f deg", value);
    else std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
    if (paramId == kMode) {
        *value = (std::strcmp(display, "SER") == 0 || std::strcmp(display, "ser") == 0) ? 1.0 : 0.0;
        return true;
    }
    *value = clampParam(paramId, std::atof(display));
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readEvents(*self(plugin), in);
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
    const uint8_t* cursor = static_cast<const uint8_t*>(data);
    while (size > 0) {
        const int64_t n = stream->write(stream, cursor, size);
        if (n <= 0) return false;
        cursor += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool readFull(const clap_istream_t* stream, void* data, size_t size)
{
    uint8_t* cursor = static_cast<uint8_t*>(data);
    while (size > 0) {
        const int64_t n = stream->read(stream, cursor, size);
        if (n <= 0) return false;
        cursor += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state {};
    state.params = self(plugin)->params;
    return writeFull(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readFull(stream, &state, sizeof(state)) || state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = state.params;
    p->rack.setParams(p->params);
    p->params = p->rack.params();
    return true;
}

const clap_plugin_state_t state { stateSave, stateLoad };

#if defined(__APPLE__)
} // namespace

@interface S3G3OAFXRackView : NSView {
@private
    void* _plugin;
    int _drag;
    int _selectedFx;
}
- (id)initWithPlugin:(void*)plugin;
@end

static NSColor* rackColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

@implementation S3G3OAFXRackView
- (id)initWithPlugin:(void*)plugin
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, 1040, 640)])) {
        _plugin = plugin;
        _drag = -1;
        _selectedFx = 0;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (NSPoint)mapAzimuth:(double)azimuth elevation:(double)elevation rect:(NSRect)rect
{
    const double az = std::clamp(azimuth, -180.0, 180.0);
    const double el = std::clamp(elevation, -90.0, 90.0) * static_cast<double>(s3g::kPi) / 180.0;
    const CGFloat x = rect.origin.x + static_cast<CGFloat>((180.0 - az) / 360.0) * rect.size.width;
    const CGFloat y = rect.origin.y + static_cast<CGFloat>((1.0 - std::sin(el)) * 0.5) * rect.size.height;
    return NSMakePoint(x, y);
}
- (NSPoint)mapVector:(s3g::Vec3)v rect:(NSRect)rect
{
    const double az = std::atan2(static_cast<double>(v.y), static_cast<double>(v.x)) * 180.0 / static_cast<double>(s3g::kPi);
    const double el = std::asin(std::clamp(static_cast<double>(v.z), -1.0, 1.0)) * 180.0 / static_cast<double>(s3g::kPi);
    return [self mapAzimuth:az elevation:el rect:rect];
}
- (NSRect)rackMapRect
{
    return NSMakeRect(38, 86, 620, 390);
}
- (NSPoint)maskCenterForFx:(int)fx rect:(NSRect)rect
{
    auto* p = static_cast<Plugin*>(_plugin);
    const s3g::AedMaskParams* mask = &p->params.delay.returnMask;
    if (fx == 1) mask = &p->params.pitch.returnMask;
    else if (fx == 2) mask = &p->params.filter.returnMask;
    else if (fx == 3) mask = &p->params.drive.returnMask;
    return [self mapAzimuth:mask->azimuthDeg elevation:mask->elevationDeg rect:rect];
}
- (void)setSelectedFxPositionFromMapPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    const NSRect map = [self rackMapRect];
    const double nx = std::clamp((pt.x - map.origin.x) / map.size.width, 0.0, 1.0);
    const double ny = std::clamp((pt.y - map.origin.y) / map.size.height, 0.0, 1.0);
    const double azimuth = 180.0 - nx * 360.0;
    const double elevation = std::asin(std::clamp(1.0 - ny * 2.0, -1.0, 1.0)) * 180.0 / static_cast<double>(s3g::kPi);
    if (_selectedFx == 0) {
        setParam(*p, kDelayAz, azimuth);
        setParam(*p, kDelayEl, elevation);
    } else if (_selectedFx == 1) {
        setParam(*p, kPitchAz, azimuth);
        setParam(*p, kPitchEl, elevation);
    } else if (_selectedFx == 2) {
        setParam(*p, kFilterAz, azimuth);
        setParam(*p, kFilterEl, elevation);
    } else {
        setParam(*p, kDriveAz, azimuth);
        setParam(*p, kDriveEl, elevation);
    }
    [self setNeedsDisplay:YES];
}
- (void)drawMaskArea:(s3g::AedMaskParams)mask rect:(NSRect)rect color:(NSColor*)color label:(NSString*)label attrs:(NSDictionary*)attrs
{
    NSPoint c = [self mapAzimuth:mask.azimuthDeg elevation:mask.elevationDeg rect:rect];
    const CGFloat rx = 18.0 + static_cast<CGFloat>(mask.width) * 132.0;
    const CGFloat ry = 12.0 + static_cast<CGFloat>(mask.width) * 66.0;
    [color setStroke];
    NSBezierPath* area = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - rx, c.y - ry, rx * 2.0, ry * 2.0)];
    [area setLineWidth:1.0];
    [area stroke];
    [color setFill];
    NSRectFill(NSMakeRect(c.x - 5, c.y - 5, 10, 10));
    [label drawAtPoint:NSMakePoint(c.x + 9, c.y - 7) withAttributes:attrs];
}
- (void)drawRackMap:(NSRect)map text:(NSDictionary*)text dim:(NSDictionary*)dim
{
    auto* p = static_cast<Plugin*>(_plugin);
    [rackColor(0x0f0f0f) setFill];
    NSRectFill(map);
    [rackColor(0x5e5e5e) setStroke];
    NSFrameRect(map);

    [rackColor(0x2c2c2c) setStroke];
    for (int az = -90; az <= 90; az += 90) {
        NSPoint a = [self mapAzimuth:az elevation:90.0 rect:map];
        NSPoint b = [self mapAzimuth:az elevation:-90.0 rect:map];
        [NSBezierPath strokeLineFromPoint:a toPoint:b];
    }
    for (int el = -45; el <= 45; el += 45) {
        NSPoint a = [self mapAzimuth:-180.0 elevation:el rect:map];
        NSPoint b = [self mapAzimuth:180.0 elevation:el rect:map];
        [NSBezierPath strokeLineFromPoint:a toPoint:b];
    }
    [@"AZ +180       +90        0       -90      -180" drawAtPoint:NSMakePoint(map.origin.x + 8, map.origin.y + map.size.height + 8) withAttributes:dim];
    [@"+90" drawAtPoint:NSMakePoint(map.origin.x + 5, map.origin.y + 5) withAttributes:dim];
    [@"0" drawAtPoint:NSMakePoint(map.origin.x + 5, NSMidY(map) - 6) withAttributes:dim];
    [@"-90" drawAtPoint:NSMakePoint(map.origin.x + 5, map.origin.y + map.size.height - 15) withAttributes:dim];

    float delayMask[s3g::k3OafxVirtualSpeakers] {};
    float pitchMask[s3g::k3OafxVirtualSpeakers] {};
    float filterMask[s3g::k3OafxVirtualSpeakers] {};
    float driveMask[s3g::k3OafxVirtualSpeakers] {};
    s3g::computeMask(s3g::directionFromAed(p->params.delay.returnMask.azimuthDeg, p->params.delay.returnMask.elevationDeg),
                     p->params.delay.returnMask,
                     delayMask);
    s3g::computeMask(s3g::directionFromAed(p->params.pitch.returnMask.azimuthDeg, p->params.pitch.returnMask.elevationDeg),
                     p->params.pitch.returnMask,
                     pitchMask);
    s3g::computeMask(s3g::directionFromAed(p->params.filter.returnMask.azimuthDeg, p->params.filter.returnMask.elevationDeg),
                     p->params.filter.returnMask,
                     filterMask);
    s3g::computeMask(s3g::directionFromAed(p->params.drive.returnMask.azimuthDeg, p->params.drive.returnMask.elevationDeg),
                     p->params.drive.returnMask,
                     driveMask);

    for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
        const float activity = std::max(std::max(delayMask[i] * p->params.delayFx.mix,
                                                 pitchMask[i] * p->params.pitchFx.mix),
                                        std::max(filterMask[i] * p->params.filterMix,
                                                 driveMask[i] * p->params.driveMix));
        const int gray = 0x5b + static_cast<int>(std::clamp(activity, 0.0f, 1.0f) * 0x96);
        [rackColor((gray << 16) | (gray << 8) | gray) setFill];
        NSPoint pt = [self mapVector:s3g::k3OafxPoints[i] rect:map];
        const CGFloat sz = 5.0 + static_cast<CGFloat>(activity) * 7.0;
        NSRectFill(NSMakeRect(pt.x - sz * 0.5, pt.y - sz * 0.5, sz, sz));
        [[NSString stringWithFormat:@"%u", i + 1u] drawAtPoint:NSMakePoint(pt.x + 5, pt.y - 6) withAttributes:dim];
    }

    [self drawMaskArea:p->params.delay.returnMask rect:map color:rackColor(0xe6e6e6) label:@"DLY" attrs:text];
    [self drawMaskArea:p->params.pitch.returnMask rect:map color:rackColor(0xb8b8b8) label:@"PIT" attrs:text];
    [self drawMaskArea:p->params.filter.returnMask rect:map color:rackColor(0x9f9f9f) label:@"FIL" attrs:text];
    [self drawMaskArea:p->params.drive.returnMask rect:map color:rackColor(0x858585) label:@"DRV" attrs:text];
}
- (void)drawFxSelector:(NSRect)rect text:(NSDictionary*)text dim:(NSDictionary*)dim
{
    (void)dim;
    const char* labels[] = { "DEL", "PIT", "FIL", "DRV" };
    [rackColor(0x151515) setFill];
    NSRectFill(rect);
    [rackColor(0x565656) setStroke];
    NSFrameRect(rect);
    for (int i = 0; i < 4; ++i) {
        NSRect r = NSMakeRect(rect.origin.x + 8 + i * 50, rect.origin.y + 5, 42, 18);
        [rackColor(i == _selectedFx ? 0xdddddd : 0x282828) setFill];
        NSRectFill(r);
        [rackColor(0x686868) setStroke];
        NSFrameRect(r);
        NSDictionary* attrs = @{ NSFontAttributeName:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular],
                                 NSForegroundColorAttributeName:(i == _selectedFx ? rackColor(0x050505) : rackColor(0xc9c9c9)) };
        [[NSString stringWithUTF8String:labels[i]] drawAtPoint:NSMakePoint(r.origin.x + 10, r.origin.y + 3) withAttributes:attrs];
    }
    NSRect clear = NSMakeRect(rect.origin.x + rect.size.width - 52, rect.origin.y + 5, 42, 18);
    [rackColor(0x202020) setFill];
    NSRectFill(clear);
    [rackColor(0x686868) setStroke];
    NSFrameRect(clear);
    [@"CLR" drawAtPoint:NSMakePoint(clear.origin.x + 10, clear.origin.y + 3) withAttributes:text];
}
- (uint32_t)selectedControlIds:(clap_id*)ids labels:(const char**)labels
{
    ids[0] = kMode; labels[0] = "MODE";
    ids[1] = kDry; labels[1] = "DRY";
    ids[2] = kOut; labels[2] = "OUT";
    if (_selectedFx == 0) {
        ids[3] = kDelayAz; labels[3] = "D-AZ";
        ids[4] = kDelayEl; labels[4] = "D-EL";
        ids[5] = kDelayWidth; labels[5] = "D-WID";
        ids[6] = kDelayTime; labels[6] = "TIME";
        ids[7] = kDelayFeedback; labels[7] = "FBK";
        ids[8] = kDelayMix; labels[8] = "MIX";
        return 9;
    }
    if (_selectedFx == 1) {
        ids[3] = kPitchAz; labels[3] = "P-AZ";
        ids[4] = kPitchEl; labels[4] = "P-EL";
        ids[5] = kPitchWidth; labels[5] = "P-WID";
        ids[6] = kPitchSemis; labels[6] = "PIT";
        ids[7] = kPitchMix; labels[7] = "MIX";
        return 8;
    }
    if (_selectedFx == 2) {
        ids[3] = kFilterAz; labels[3] = "F-AZ";
        ids[4] = kFilterEl; labels[4] = "F-EL";
        ids[5] = kFilterWidth; labels[5] = "F-WID";
        ids[6] = kFilterTone; labels[6] = "TONE";
        ids[7] = kFilterMix; labels[7] = "MIX";
        return 8;
    }
    ids[3] = kDriveAz; labels[3] = "X-AZ";
    ids[4] = kDriveEl; labels[4] = "X-EL";
    ids[5] = kDriveWidth; labels[5] = "X-WID";
    ids[6] = kDriveAmount; labels[6] = "DRV";
    ids[7] = kDriveMix; labels[7] = "MIX";
    return 8;
}
- (void)clearSelectedFx
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_selectedFx == 0) {
        setParam(*p, kDelayFeedback, 0.0);
        setParam(*p, kDelayMix, 0.0);
    } else if (_selectedFx == 1) {
        setParam(*p, kPitchSemis, 0.0);
        setParam(*p, kPitchMix, 0.0);
    } else if (_selectedFx == 2) {
        setParam(*p, kFilterTone, 0.55);
        setParam(*p, kFilterMix, 0.0);
    } else {
        setParam(*p, kDriveAmount, 0.35);
        setParam(*p, kDriveMix, 0.0);
    }
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill(self.bounds);
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    NSDictionary* dim = s3g::clap_gui::softValueAttrs();
    [@"s3g 3OAFX Rack" drawAtPoint:NSMakePoint(18, 16) withAttributes:text];
    [s3g::clap_gui::peakDbText(p->peak.load(std::memory_order_relaxed)) drawAtPoint:NSMakePoint(940, 16) withAttributes:dim];
    NSRect field = NSMakeRect(18, 48, 660, 566);
    s3g::clap_gui::drawPanelFrame(field.origin.x, field.origin.y, field.size.width, field.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SLOT MAP", false, field.origin.x, field.origin.y, field.size.width, 21, text, style);
    NSRect map = [self rackMapRect];
    [self drawRackMap:map text:text dim:dim];
    [[NSString stringWithFormat:@"MODE %@   DRY %.2f   OUT %.2f",
        p->params.mode == s3g::ThreeOafxRackMode::Series ? @"SER" : @"PAR", p->params.dry, p->params.output]
        drawAtPoint:NSMakePoint(38, 520) withAttributes:dim];

    s3g::clap_gui::drawPanelFrame(700, 48, 300, 566, style);
    s3g::clap_gui::drawPanelHeader(@"RACK", false, 700, 48, 300, 21, text, style);
    [self drawFxSelector:NSMakeRect(712, 80, 256, 28) text:text dim:dim];
    clap_id ids[12] {};
    const char* labels[12] {};
    const uint32_t count = [self selectedControlIds:ids labels:labels];
    CGFloat y = 132;
    for (uint32_t i = 0; i < count; ++i) {
        double value = getParam(*p, ids[i]);
        double norm = value;
        if (ids[i] == kMode) norm = value;
        else if (ids[i] == kDelayAz || ids[i] == kPitchAz || ids[i] == kFilterAz || ids[i] == kDriveAz) norm = (value + 179.9) / 359.8;
        else if (ids[i] == kDelayEl || ids[i] == kPitchEl || ids[i] == kFilterEl || ids[i] == kDriveEl) norm = (value + 90.0) / 180.0;
        else if (ids[i] == kDelayTime) norm = (value - 5.0) / 1995.0;
        else if (ids[i] == kPitchSemis) norm = (value + 24.0) / 48.0;
        NSString* val = ids[i] == kMode ? (value >= 0.5 ? @"SER" : @"PAR") : [NSString stringWithFormat:@"%.2f", value];
        s3g::clap_gui::drawSlider([NSString stringWithUTF8String:labels[i]], val, static_cast<CGFloat>(std::clamp(norm, 0.0, 1.0)), y, dim, text, style, 712, 786, 948, 116);
        y += 26;
    }
}
- (void)setParamFromPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    clap_id ids[12] {};
    const char* labels[12] {};
    const uint32_t count = [self selectedControlIds:ids labels:labels];
    if (_drag < 0 || _drag >= static_cast<int>(count)) return;
    const clap_id id = ids[_drag];
    double n = std::clamp((pt.x - 786.0) / 116.0, 0.0, 1.0);
    double value = n;
    if (id == kMode) value = n >= 0.5 ? 1.0 : 0.0;
    else if (id == kDelayAz || id == kPitchAz || id == kFilterAz || id == kDriveAz) value = -179.9 + n * 359.8;
    else if (id == kDelayEl || id == kPitchEl || id == kFilterEl || id == kDriveEl) value = -90.0 + n * 180.0;
    else if (id == kDelayTime) value = 5.0 + n * 1995.0;
    else if (id == kPitchSemis) value = -24.0 + n * 48.0;
    setParam(*p, id, value);
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(pt, NSMakeRect(720 + i * 50, 85, 42, 18))) {
            _selectedFx = i;
            _drag = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(pt, NSMakeRect(916, 85, 42, 18))) {
        [self clearSelectedFx];
        return;
    }
    NSRect map = [self rackMapRect];
    if (NSPointInRect(pt, map)) {
        CGFloat bestDistance = 999999.0;
        int bestFx = _selectedFx;
        for (int i = 0; i < 4; ++i) {
            NSPoint c = [self maskCenterForFx:i rect:map];
            const CGFloat dx = pt.x - c.x;
            const CGFloat dy = pt.y - c.y;
            const CGFloat distance = std::sqrt(dx * dx + dy * dy);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestFx = i;
            }
        }
        if (bestDistance <= 22.0) _selectedFx = bestFx;
        _drag = -2;
        [self setSelectedFxPositionFromMapPoint:pt];
        return;
    }
    clap_id ids[12] {};
    const char* labels[12] {};
    const uint32_t count = [self selectedControlIds:ids labels:labels];
    CGFloat y = 132;
    for (uint32_t i = 0; i < count; ++i) {
        if (NSPointInRect(pt, NSMakeRect(706, y - 8, 284, 24))) {
            _drag = i;
            [self setParamFromPoint:pt];
            return;
        }
        y += 26;
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:event.locationInWindow fromView:nil];
    if (_drag == -2) {
        [self setSelectedFxPositionFromMapPoint:pt];
        return;
    }
    [self setParamFromPoint:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _drag = -1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
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
    if (!p->guiView) p->guiView = [[S3G3OAFXRackView alloc] initWithPlugin:p];
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
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 1040; *h = 640; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)];
    return true;
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* p = self(plugin);
    if (!p->guiView) return false;
    NSView* view = static_cast<NSView*>(p->guiView);
    [static_cast<NSView*>(window->cocoa) addSubview:view];
    [view setFrame:NSMakeRect(0, 0, 1040, 640)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:NO]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }

const clap_plugin_gui_t gui { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.3oafx-rack",
    "s3g 3OAFX Rack",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.2",
    "Self-contained 3OA to internal 24-channel spatial effects rack with Macro Delay and Macro Pitch slots.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostParams = host && host->get_extension ? static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS)) : nullptr;
    initDefaults(*p);
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
    p->plugin.get_extension = getExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
