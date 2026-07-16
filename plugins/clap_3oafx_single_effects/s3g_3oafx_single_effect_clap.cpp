#include "s3g_3oafx_single_effect.h"
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

#ifndef S3G_3OAFX_SINGLE_KIND
#define S3G_3OAFX_SINGLE_KIND 0
#endif

#ifndef S3G_3OAFX_SINGLE_PLUGIN_ID
#define S3G_3OAFX_SINGLE_PLUGIN_ID "org.s3g.s3g-dsp.3oafx-single"
#endif

#ifndef S3G_3OAFX_SINGLE_PLUGIN_NAME
#define S3G_3OAFX_SINGLE_PLUGIN_NAME "s3g 3OAFX Single"
#endif

#ifndef S3G_3OAFX_SINGLE_DESCRIPTION
#define S3G_3OAFX_SINGLE_DESCRIPTION "Single-slot 3OAFX processor."
#endif

namespace {

constexpr uint32_t kStateVersion = 1;
constexpr s3g::ThreeOafxEffectKind kEffectKind = static_cast<s3g::ThreeOafxEffectKind>(S3G_3OAFX_SINGLE_KIND);

enum ParamId : clap_id {
    kAzimuth = 1,
    kElevation,
    kWidth,
    kDry,
    kOut,
    kMix,
    kDelayTime,
    kDelayFeedback,
    kPitchSemis,
    kFilterTone,
    kGain,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double min;
    double max;
    double def;
};

#if S3G_3OAFX_SINGLE_KIND == 0
constexpr ParamDef kParams[] {
    { kAzimuth, "Azimuth", "Return Mask", -179.9, 179.9, -30.0 },
    { kElevation, "Elevation", "Return Mask", -90.0, 90.0, 10.0 },
    { kWidth, "Width", "Return Mask", 0.0, 1.0, 0.78 },
    { kDry, "Dry", "Mixer", 0.0, 1.0, 0.65 },
    { kOut, "Output", "Mixer", 0.0, 1.0, 0.90 },
    { kDelayTime, "Time", "Delay", 20.0, 2000.0, 320.0 },
    { kDelayFeedback, "Feedback", "Delay", 0.0, 0.62, 0.22 },
    { kMix, "Mix", "Delay", 0.0, 1.0, 0.0 },
};
#elif S3G_3OAFX_SINGLE_KIND == 1
constexpr ParamDef kParams[] {
    { kAzimuth, "Azimuth", "Return Mask", -179.9, 179.9, 38.0 },
    { kElevation, "Elevation", "Return Mask", -90.0, 90.0, -8.0 },
    { kWidth, "Width", "Return Mask", 0.0, 1.0, 0.72 },
    { kDry, "Dry", "Mixer", 0.0, 1.0, 0.65 },
    { kOut, "Output", "Mixer", 0.0, 1.0, 0.90 },
    { kPitchSemis, "Semitones", "Pitch", -24.0, 24.0, 0.0 },
    { kMix, "Mix", "Pitch", 0.0, 1.0, 0.0 },
};
#elif S3G_3OAFX_SINGLE_KIND == 2
constexpr ParamDef kParams[] {
    { kAzimuth, "Azimuth", "Return Mask", -179.9, 179.9, -105.0 },
    { kElevation, "Elevation", "Return Mask", -90.0, 90.0, 18.0 },
    { kWidth, "Width", "Return Mask", 0.0, 1.0, 0.62 },
    { kDry, "Dry", "Mixer", 0.0, 1.0, 0.65 },
    { kOut, "Output", "Mixer", 0.0, 1.0, 0.90 },
    { kFilterTone, "Tone", "Filter", 0.0, 1.0, 0.55 },
    { kMix, "Mix", "Filter", 0.0, 1.0, 0.0 },
};
#else
constexpr ParamDef kParams[] {
    { kAzimuth, "Azimuth", "Return Mask", -179.9, 179.9, 0.0 },
    { kElevation, "Elevation", "Return Mask", -90.0, 90.0, 0.0 },
    { kWidth, "Width", "Return Mask", 0.0, 1.0, 0.72 },
    { kDry, "Dry", "Mixer", 0.0, 1.0, 0.90 },
    { kOut, "Output", "Mixer", 0.0, 1.0, 0.90 },
    { kGain, "Gain", "Gain", 0.0, 2.0, 1.0 },
};
#endif

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    s3g::ThreeOafxSingleEffect processor {};
    s3g::ThreeOafxSingleEffectParams params {};
    double sampleRate = 48000.0;
    std::array<float, s3g::k3OaChannels> inFrame {};
    std::array<float, s3g::k3OaChannels> outFrame {};
    std::atomic<float> peak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiDirty { false };
#endif
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::ThreeOafxSingleEffectParams params {};
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
    case kAzimuth: p.params.mask.azimuthDeg = v; break;
    case kElevation: p.params.mask.elevationDeg = v; break;
    case kWidth: p.params.mask.width = v; break;
    case kDry: p.params.dry = v; break;
    case kOut: p.params.output = v; break;
    case kMix: p.params.mix = v; break;
    case kDelayTime: p.params.delayTimeMs = v; break;
    case kDelayFeedback: p.params.delayFeedback = v; break;
    case kPitchSemis: p.params.pitchSemitones = v; break;
    case kFilterTone: p.params.filterTone = v; break;
    case kGain: p.params.gain = v; break;
    default: return;
    }
    p.processor.setParams(p.params);
    p.params = p.processor.params();
#if defined(__APPLE__)
    p.guiDirty.store(true, std::memory_order_release);
    if (p.host && p.host->request_callback) p.host->request_callback(p.host);
#endif
}

double getParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kAzimuth: return p.params.mask.azimuthDeg;
    case kElevation: return p.params.mask.elevationDeg;
    case kWidth: return p.params.mask.width;
    case kDry: return p.params.dry;
    case kOut: return p.params.output;
    case kMix: return p.params.mix;
    case kDelayTime: return p.params.delayTimeMs;
    case kDelayFeedback: return p.params.delayFeedback;
    case kPitchSemis: return p.params.pitchSemitones;
    case kFilterTone: return p.params.filterTone;
    case kGain: return p.params.gain;
    default: return 0.0;
    }
}

void readEvents(Plugin& p, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* header = events->get(events, i);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
            setParam(p, event->param_id, event->value);
        }
    }
}

void initDefaults(Plugin& p)
{
    p.params = {};
    p.params.kind = kEffectKind;
    p.params.mask.smoothing = 0.20f;
    p.params.mask.focus = 0.0f;
    p.params.mask.level = 1.0f;
    p.params.mask.floor = 0.03f;
    p.params.mask.rearReject = 1.0f;
    p.params.mask.energyComp = 0.50f;
    p.params.mask.gamma = 1.25f;
    for (const auto& def : kParams) {
        setParam(p, def.id, def.def);
    }
    p.processor.setParams(p.params);
    p.params = p.processor.params();
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
    p->processor.prepare(sampleRate);
    p->processor.setParams(p->params);
    p->params = p->processor.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->processor.reset(); }

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
        p->processor.processFrame(p->inFrame.data(), p->outFrame.data());
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
    info->channel_count = s3g::k3OaChannels;
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
    if (paramId == kAzimuth || paramId == kElevation) std::snprintf(display, size, "%+.1f deg", value);
    else if (paramId == kDelayTime) std::snprintf(display, size, "%.1f ms", value);
    else if (paramId == kPitchSemis) std::snprintf(display, size, "%+.2f st", value);
    else std::snprintf(display, size, "%.3f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
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
    p->params.kind = kEffectKind;
    p->processor.setParams(p->params);
    p->params = p->processor.params();
    return true;
}

const clap_plugin_state_t state { stateSave, stateLoad };

#if defined(__APPLE__)
} // namespace

@interface S3G3OAFXSingleView : NSView {
@private
    void* _plugin;
    int _drag;
}
- (id)initWithPlugin:(void*)plugin;
@end

static NSColor* uiColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static constexpr CGFloat kViewW = 880.0;
static constexpr CGFloat kViewH = 500.0;
static constexpr CGFloat kSliderTrackX = 610.0;
static constexpr CGFloat kSliderTrackW = 132.0;
static constexpr CGFloat kSliderStartY = 90.0;
static constexpr CGFloat kSliderStepY = 36.0;

static const char* guiLabelForParam(clap_id id)
{
    switch (id) {
    case kAzimuth: return "AZ";
    case kElevation: return "EL";
    case kWidth: return "WID";
    case kDry: return "DRY";
    case kOut: return "OUT";
    case kMix: return "MIX";
    case kDelayTime: return "TIM";
    case kDelayFeedback: return "FDB";
    case kPitchSemis: return "PIT";
    case kFilterTone: return "TON";
    case kGain: return "GAIN";
    default: return "";
    }
}

@implementation S3G3OAFXSingleView
- (id)initWithPlugin:(void*)plugin
{
    if ((self = [super initWithFrame:NSMakeRect(0, 0, kViewW, kViewH)])) {
        _plugin = plugin;
        _drag = -1;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (NSRect)mapRect { return NSMakeRect(34, 82, 448, 292); }
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
- (void)setMaskFromPoint:(NSPoint)pt
{
    auto* p = static_cast<Plugin*>(_plugin);
    const NSRect map = [self mapRect];
    const double nx = std::clamp((pt.x - map.origin.x) / map.size.width, 0.0, 1.0);
    const double ny = std::clamp((pt.y - map.origin.y) / map.size.height, 0.0, 1.0);
    setParam(*p, kAzimuth, 180.0 - nx * 360.0);
    setParam(*p, kElevation, std::asin(std::clamp(1.0 - ny * 2.0, -1.0, 1.0)) * 180.0 / static_cast<double>(s3g::kPi));
    [self setNeedsDisplay:YES];
}
- (NSRect)sliderTrackForIndex:(uint32_t)index
{
    return NSMakeRect(kSliderTrackX, kSliderStartY + 1.0 + static_cast<CGFloat>(index) * kSliderStepY, kSliderTrackW, 11);
}
- (uint32_t)sliderIds:(clap_id*)ids labels:(const char**)labels
{
    uint32_t count = 0;
    for (const auto& def : kParams) {
        ids[count] = def.id;
        labels[count] = guiLabelForParam(def.id);
        ++count;
    }
    return count;
}
- (double)normForParam:(clap_id)id value:(double)value
{
    for (const auto& def : kParams) {
        if (def.id == id) return (value - def.min) / std::max(0.000001, def.max - def.min);
    }
    return 0.0;
}
- (double)valueForParam:(clap_id)id norm:(double)norm
{
    for (const auto& def : kParams) {
        if (def.id == id) return def.min + std::clamp(norm, 0.0, 1.0) * (def.max - def.min);
    }
    return 0.0;
}
- (void)setParamFromPoint:(NSPoint)pt
{
    if (_drag < 0) return;
    auto* p = static_cast<Plugin*>(_plugin);
    clap_id ids[16] {};
    const char* labels[16] {};
    const uint32_t count = [self sliderIds:ids labels:labels];
    if (static_cast<uint32_t>(_drag) >= count) return;
    const NSRect track = [self sliderTrackForIndex:static_cast<uint32_t>(_drag)];
    const double n = std::clamp((pt.x - track.origin.x) / track.size.width, 0.0, 1.0);
    setParam(*p, ids[_drag], [self valueForParam:ids[_drag] norm:n]);
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
    [@S3G_3OAFX_SINGLE_PLUGIN_NAME drawAtPoint:NSMakePoint(18, 16) withAttributes:text];
    [s3g::clap_gui::peakDbText(p->peak.load(std::memory_order_relaxed)) drawAtPoint:NSMakePoint(kViewW - 110.0, 16) withAttributes:dim];

    s3g::clap_gui::drawPanelFrame(18, 48, 480, 406, style);
    s3g::clap_gui::drawPanelHeader(@"POINT FIELD", true, 18, 48, 480, 21, text, style);
    NSRect map = [self mapRect];
    [uiColor(0x0f0f0f) setFill];
    NSRectFill(map);
    [uiColor(0x5e5e5e) setStroke];
    NSFrameRect(map);
    for (uint32_t i = 0; i < s3g::k3OafxVirtualSpeakers; ++i) {
        NSPoint pt = [self mapVector:s3g::k3OafxPoints[i] rect:map];
        [uiColor(0x777777, 0.45) setFill];
        NSRectFill(NSMakeRect(pt.x - 2, pt.y - 2, 4, 4));
    }
    NSPoint c = [self mapAzimuth:p->params.mask.azimuthDeg elevation:p->params.mask.elevationDeg rect:map];
    const CGFloat rx = 18.0 + static_cast<CGFloat>(p->params.mask.width) * 132.0;
    const CGFloat ry = 12.0 + static_cast<CGFloat>(p->params.mask.width) * 66.0;
    [uiColor(0xd1d1d1) setStroke];
    NSBezierPath* area = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - rx, c.y - ry, rx * 2.0, ry * 2.0)];
    [area setLineWidth:1.0];
    [area stroke];
    [uiColor(0xf0f0f0) setFill];
    NSRectFill(NSMakeRect(c.x - 5, c.y - 5, 10, 10));
    [[NSString stringWithFormat:@"AZ %+.1f   EL %+.1f", p->params.mask.azimuthDeg, p->params.mask.elevationDeg]
        drawAtPoint:NSMakePoint(34, 392) withAttributes:dim];

    s3g::clap_gui::drawPanelFrame(518, 48, 342, 406, style);
    s3g::clap_gui::drawPanelHeader(@"EFFECT", true, 518, 48, 342, 21, text, style);
    clap_id ids[16] {};
    const char* labels[16] {};
    const uint32_t count = [self sliderIds:ids labels:labels];
    for (uint32_t i = 0; i < count; ++i) {
        const double value = getParam(*p, ids[i]);
        char valueText[32] {};
        paramsValueToText(nullptr, ids[i], value, valueText, sizeof(valueText));
        s3g::clap_gui::drawSlider([NSString stringWithUTF8String:labels[i]],
                                  [NSString stringWithUTF8String:valueText],
                                  static_cast<CGFloat>([self normForParam:ids[i] value:value]),
                                  kSliderStartY + static_cast<CGFloat>(i) * kSliderStepY,
                                  text,
                                  dim,
                                  style,
                                  536,
                                  kSliderTrackX,
                                  760,
                                  kSliderTrackW);
    }
}
- (void)mouseDown:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (NSPointInRect(pt, [self mapRect])) {
        _drag = -2;
        [self setMaskFromPoint:pt];
        return;
    }
    clap_id ids[16] {};
    const char* labels[16] {};
    const uint32_t count = [self sliderIds:ids labels:labels];
    (void)ids;
    (void)labels;
    for (uint32_t i = 0; i < count; ++i) {
        if (NSPointInRect(pt, NSInsetRect([self sliderTrackForIndex:i], -8, -10))) {
            _drag = static_cast<int>(i);
            [self setParamFromPoint:pt];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_drag == -2) {
        [self setMaskFromPoint:pt];
    } else {
        [self setParamFromPoint:pt];
    }
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _drag = -1;
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char*, bool)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        p->guiView = [[S3G3OAFXSingleView alloc] initWithPlugin:p];
    }
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
bool guiSetScale(const clap_plugin_t*, double) { return false; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    NSRect frame = [static_cast<NSView*>(p->guiView) frame];
    *w = static_cast<uint32_t>(frame.size.width);
    *h = static_cast<uint32_t>(frame.size.height);
    return true;
}
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
    auto* p = self(plugin);
    if (!p->guiView || !window || !window->cocoa) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [view setFrame:NSMakeRect(0, 0, kViewW, kViewH)];
    [parent addSubview:view];
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
    S3G_3OAFX_SINGLE_PLUGIN_ID,
    S3G_3OAFX_SINGLE_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    S3G_3OAFX_SINGLE_DESCRIPTION,
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
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory* factory, const clap_host_t* host, const char* pluginId) { return createPlugin(factory, host, pluginId); }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    [](const char*) -> bool { return true; },
    []() {},
    [](const char* factoryId) -> const void* {
        if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) return &factory;
        return nullptr;
    }
};
