#include "s3g_ambi_group_depth.h"
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

#if defined(S3G_AMBI_DEPTH_16)
constexpr uint32_t kGroups = 1;
constexpr uint32_t kChannels = 16;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-depth-16";
constexpr const char* kPluginName = "s3g Ambi Depth 16";
constexpr const char* kPluginDesc = "16-channel 3OA ambisonic depth utility.";
constexpr const char* kHeaderTitle = "s3g AMBI DEPTH 16";
constexpr const char* kHeaderInfo = "3OA / 16CH";
constexpr bool kSingleField = true;
#elif defined(S3G_AMBI_GROUP_DEPTH_128)
constexpr uint32_t kGroups = 8;
constexpr uint32_t kChannels = 128;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-group-depth-128";
constexpr const char* kPluginName = "s3g Ambi Group Depth 128";
constexpr const char* kPluginDesc = "128-channel lane-locked 8x3OA group depth utility.";
constexpr const char* kHeaderTitle = "s3g AMBI GROUP DEPTH 128";
constexpr const char* kHeaderInfo = "8 x 3OA / 128CH";
constexpr bool kSingleField = false;
#else
constexpr uint32_t kGroups = 4;
constexpr uint32_t kChannels = 64;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-group-depth-64";
constexpr const char* kPluginName = "s3g Ambi Group Depth 64";
constexpr const char* kPluginDesc = "64-channel lane-locked 4x3OA group depth utility.";
constexpr const char* kHeaderTitle = "s3g AMBI GROUP DEPTH 64";
constexpr const char* kHeaderInfo = "4 x 3OA / 64CH";
constexpr bool kSingleField = false;
#endif

constexpr uint32_t kGuiWidth = 820;
constexpr uint32_t kGuiHeight = 456;
constexpr uint32_t kStateVersion = 2;

enum ParamId : clap_id {
    kParamDepth = 1,
    kParamSpread = 2,
    kParamFocus = 3,
    kParamAir = 4,
    kParamLow = 5,
    kParamWidth = 6,
    kParamOutput = 7,
    kParamTail = 8,
};

using Processor = s3g::AmbiGroupDepthProcessor<kGroups>;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiGroupDepthParams params {};
};

struct OldAmbiGroupDepthParamsV1 {
    float depth = 0.0f;
    float spread = 0.0f;
    float focus = 0.0f;
    float air = 0.0f;
    float low = 0.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
};

struct OldSavedStateV1 {
    uint32_t version = 1;
    OldAmbiGroupDepthParamsV1 params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    s3g::AmbiGroupDepthParams params {};
    Processor processor {};
    double sampleRate = 48000.0;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kParamDepth: p.params.depth = static_cast<float>(value); break;
    case kParamSpread: p.params.spread = static_cast<float>(value); break;
    case kParamFocus: p.params.focus = static_cast<float>(value); break;
    case kParamAir: p.params.air = static_cast<float>(value); break;
    case kParamTail: p.params.tail = static_cast<float>(value); break;
    case kParamLow: p.params.low = static_cast<float>(value); break;
    case kParamWidth: p.params.width = static_cast<float>(value); break;
    case kParamOutput: p.params.outputGainDb = static_cast<float>(value); break;
    default: return;
    }
    p.params = s3g::sanitizeAmbiGroupDepthParams(p.params);
    p.processor.setParams(p.params);
    if (p.hostTail && p.host) {
        p.hostTail->changed(p.host);
    }
}

double getParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kParamDepth: return p.params.depth;
    case kParamSpread: return p.params.spread;
    case kParamFocus: return p.params.focus;
    case kParamAir: return p.params.air;
    case kParamTail: return p.params.tail;
    case kParamLow: return p.params.low;
    case kParamWidth: return p.params.width;
    case kParamOutput: return p.params.outputGainDb;
    default: return 0.0;
    }
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* v = static_cast<NSView*>(p->guiView);
        [v removeFromSuperview];
        [v release];
        p->guiView = nullptr;
    }
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->params = s3g::sanitizeAmbiGroupDepthParams(p->params);
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
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            applyParam(p, param->param_id, param->value);
        }
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t frames, Sample** in, Sample** out)
{
    s3g::clearAudioBuffer(output, frames);
    if (!in || !out) return CLAP_PROCESS_CONTINUE;
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kChannels);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kChannels);
    if (inChannels == 0u || outChannels == 0u) return CLAP_PROCESS_CONTINUE;
    p.processor.process(in, inChannels, out, outChannels, frames);
    s3g::clearAudioBufferFromChannel(output, kChannels, frames);
    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outChannels; ++ch) {
        if (!out[ch]) continue;
        for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, static_cast<float>(std::abs(out[ch][i])));
    }
    p.outputPeak.store(std::max(p.outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    if (input.data32 && output.data32) return processTyped<float>(*p, input, output, proc->frames_count, input.data32, output.data32);
    if (input.data64 && output.data64) return processTyped<double>(*p, input, output, proc->frames_count, input.data64, output.data64);
    s3g::clearAudioBuffer(output, proc->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name,
        isInput
            ? (kSingleField ? "Ambi Depth In" : "Group Depth In")
            : (kSingleField ? "Ambi Depth Out" : "Group Depth Out"),
        sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

bool isParamId(clap_id paramId)
{
    if constexpr (kSingleField) {
        return paramId == kParamDepth
            || paramId == kParamFocus
            || paramId == kParamAir
            || paramId == kParamTail
            || paramId == kParamLow
            || paramId == kParamWidth
            || paramId == kParamOutput;
    }
    return paramId >= kParamDepth && paramId <= kParamTail;
}

uint32_t paramsCount(const clap_plugin_t*) { return kSingleField ? 7u : 8u; }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, kSingleField ? "Ambi Depth" : "Ambi Group Depth", sizeof(info->module));
    if constexpr (kSingleField) {
        switch (index) {
        case 0: info->id = kParamDepth; std::strncpy(info->name, "Depth", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
        case 1: info->id = kParamFocus; std::strncpy(info->name, "Focus", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
        case 2: info->id = kParamAir; std::strncpy(info->name, "Air damping", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
        case 3: info->id = kParamTail; std::strncpy(info->name, "Distance tail", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0; return true;
        case 4: info->id = kParamLow; std::strncpy(info->name, "Low body", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
        case 5: info->id = kParamWidth; std::strncpy(info->name, "Order width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; return true;
        case 6: info->id = kParamOutput; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; return true;
        default: return false;
        }
    }
    switch (index) {
    case 0: info->id = kParamDepth; std::strncpy(info->name, "Depth", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 1: info->id = kParamSpread; std::strncpy(info->name, "Group spread", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 2: info->id = kParamFocus; std::strncpy(info->name, "Focus", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 3: info->id = kParamAir; std::strncpy(info->name, "Air damping", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 4: info->id = kParamTail; std::strncpy(info->name, "Distance tail", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0; return true;
    case 5: info->id = kParamLow; std::strncpy(info->name, "Low body", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 6: info->id = kParamWidth; std::strncpy(info->name, "Order width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; return true;
    case 7: info->id = kParamOutput; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; return true;
    default: return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParam(*self(plugin), paramId);
    return isParamId(paramId);
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    switch (paramId) {
    case kParamDepth:
    case kParamSpread:
    case kParamFocus:
    case kParamAir:
    case kParamLow: std::snprintf(display, size, "%+.0f%%", value * 100.0); return true;
    case kParamTail: std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    case kParamWidth: std::snprintf(display, size, "%.2f", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    default: return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return isParamId(paramId);
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state { kStateVersion, self(plugin)->params };
    return stream->write(stream, &state, sizeof(state)) == static_cast<int64_t>(sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    const int64_t got = stream->read(stream, &state, sizeof(state));
    auto* p = self(plugin);
    if (got == static_cast<int64_t>(sizeof(state)) && state.version == kStateVersion) {
        p->params = s3g::sanitizeAmbiGroupDepthParams(state.params);
    } else if (got == static_cast<int64_t>(sizeof(OldSavedStateV1))) {
        const auto* old = reinterpret_cast<const OldSavedStateV1*>(&state);
        if (old->version != 1u) return false;
        p->params = s3g::sanitizeAmbiGroupDepthParams({
            old->params.depth,
            old->params.spread,
            old->params.focus,
            old->params.air,
            0.0f,
            old->params.low,
            old->params.width,
            old->params.outputGainDb,
        });
    } else {
        return false;
    }
    p->processor.setParams(p->params);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p || p->params.tail <= 0.0001f) return 0u;
    const double amount = std::clamp(static_cast<double>(p->params.tail), 0.0, 1.0);
    const double far = std::max(0.0, static_cast<double>(p->params.depth));
    const double tailSeconds = std::clamp(1.5 + amount * 7.0 + far * 3.0, 1.0, 12.0);
    return static_cast<uint32_t>(std::ceil(tailSeconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
@interface S3GAmbiGroupDepthView : NSView {
    void* _plugin;
    int _dragSlider;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)resetSlider:(int)index;
- (void)updateSliderAtPoint:(NSPoint)pt;
@end

@implementation S3GAmbiGroupDepthView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) { _plugin = plugin; _dragSlider = -1; _refreshTimer = nil; }
    return self;
}
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (BOOL)isFlipped { return YES; }
- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 24.0) target:self selector:@selector(refreshTimerFired:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_refreshTimer) { [_refreshTimer invalidate]; _refreshTimer = nil; } }
- (void)refreshTimerFired:(NSTimer*)timer { (void)timer; if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 552, 632, 760, 128);
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    const float pk = p->outputPeak.exchange(p->outputPeak.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    NSString* peakText = s3g::clap_gui::peakDbText(pk);
    const CGFloat peakX = static_cast<CGFloat>(kGuiWidth) - [peakText sizeWithAttributes:small].width - 18.0;
    NSString* headerInfo = @(kHeaderInfo);
    const CGFloat infoX = peakX - [headerInfo sizeWithAttributes:small].width - 18.0;
    [@(kHeaderTitle) drawAtPoint:NSMakePoint(18, 13) withAttributes:text];
    [headerInfo drawAtPoint:NSMakePoint(infoX, 13) withAttributes:small];
    [peakText drawAtPoint:NSMakePoint(peakX, 13) withAttributes:small];

    NSRect fieldPanel = NSMakeRect(12, 34, 506, 370);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(kSingleField ? @"DEPTH FIELD" : @"GROUP DEPTH FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    NSRect field = NSMakeRect(28, 70, 474, 306);
    [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);

    [s3g::clap_gui::color(0x575757, 0.34) setStroke];
    for (int i = 1; i < 4; ++i) {
        const CGFloat y = field.origin.y + field.size.height * static_cast<CGFloat>(i) / 4.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 14, y) toPoint:NSMakePoint(NSMaxX(field) - 14, y)];
    }
    [@"NEAR / DIRECT" drawAtPoint:NSMakePoint(field.origin.x + 14, field.origin.y + 10) withAttributes:small];
    [@"NEUTRAL" drawAtPoint:NSMakePoint(NSMaxX(field) - 62, field.origin.y + field.size.height * 0.5 - 7) withAttributes:small];
    [@"FAR / SOFT" drawAtPoint:NSMakePoint(field.origin.x + 14, NSMaxY(field) - 22) withAttributes:small];

    NSPoint points[kGroups] {};
    for (uint32_t group = 0; group < kGroups; ++group) {
        const auto state = p->processor.groupState(group);
        const CGFloat u = kGroups <= 1 ? 0.5 : (static_cast<CGFloat>(group) + 0.5) / static_cast<CGFloat>(kGroups);
        const CGFloat y = field.origin.y + 42.0 + static_cast<CGFloat>(state.depth) * (field.size.height - 86.0);
        const CGFloat wobble = std::sin((static_cast<double>(group) + 0.25) * 1.9) * 18.0 * std::abs(p->params.spread);
        points[group] = NSMakePoint(field.origin.x + 34.0 + u * (field.size.width - 68.0), y + wobble);
    }
    const CGFloat airAmount = static_cast<CGFloat>(std::abs(p->params.air));
    if (airAmount > 0.01) {
        [[NSColor colorWithCalibratedWhite:(p->params.air >= 0.0f ? 0.72 : 0.92) alpha:0.07 + 0.18 * airAmount] setStroke];
        for (int band = 0; band < 4; ++band) {
            const CGFloat t = static_cast<CGFloat>(band) / 3.0;
            const CGFloat y = field.origin.y + 34.0 + t * (field.size.height - 68.0);
            NSBezierPath* haze = [NSBezierPath bezierPath];
            [haze moveToPoint:NSMakePoint(field.origin.x + 22.0, y)];
            [haze curveToPoint:NSMakePoint(NSMaxX(field) - 22.0, y + std::sin(t * 4.5 + p->params.air) * 10.0 * airAmount)
                 controlPoint1:NSMakePoint(field.origin.x + field.size.width * 0.35, y - 18.0 * airAmount)
                 controlPoint2:NSMakePoint(field.origin.x + field.size.width * 0.65, y + 18.0 * airAmount)];
            [haze setLineWidth:0.35 + 1.2 * airAmount];
            CGFloat pattern[] = { 2.0, 4.0 + 5.0 * (1.0 - airAmount) };
            [haze setLineDash:pattern count:2 phase:static_cast<CGFloat>(band) * 1.5];
            [haze stroke];
        }
    }
    const CGFloat tailAmount = static_cast<CGFloat>(p->params.tail * std::max(0.0f, p->params.depth));
    if (tailAmount > 0.01) {
        [[NSColor colorWithCalibratedWhite:0.78 alpha:0.06 + 0.20 * tailAmount] setStroke];
        for (uint32_t group = 0; group < kGroups; ++group) {
            const NSPoint p0 = points[group];
            const CGFloat w = 28.0 + 52.0 * tailAmount;
            const CGFloat h = 12.0 + 24.0 * tailAmount;
            NSBezierPath* wake = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p0.x - w * 0.5, p0.y - h * 0.5, w, h)];
            [wake setLineWidth:0.5 + 1.1 * tailAmount];
            [wake stroke];
        }
    }
    [s3g::clap_gui::color(0x666666, 0.38) setStroke];
    for (uint32_t group = 0; group + 1u < kGroups; ++group) {
        [NSBezierPath strokeLineFromPoint:points[group] toPoint:points[group + 1u]];
    }
    for (uint32_t group = 0; group < kGroups; ++group) {
        const auto state = p->processor.groupState(group);
        const CGFloat direct = static_cast<CGFloat>(1.0f - state.depth);
        const CGFloat size = 8.0 + direct * 8.0;
        [[NSColor colorWithCalibratedWhite:0.32 + direct * 0.58 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(points[group].x - size * 0.5, points[group].y - size * 0.5, size, size));
        [s3g::clap_gui::color(0x0f0f0f, 0.62) setStroke];
        NSFrameRect(NSMakeRect(points[group].x - size * 0.5, points[group].y - size * 0.5, size, size));
        [[NSString stringWithFormat:@"G%u", group + 1u] drawAtPoint:NSMakePoint(points[group].x + size * 0.62, points[group].y - 7.0) withAttributes:small];
    }

    NSRect meter = NSMakeRect(34, 377, 456, 10);
    [style.strip setFill]; NSRectFill(meter);
    [style.grid setStroke]; NSFrameRect(meter);
    for (uint32_t group = 0; group < kGroups; ++group) {
        const auto state = p->processor.groupState(group);
        const CGFloat x = meter.origin.x + (meter.size.width - 3.0) * static_cast<CGFloat>(state.depth);
        [[NSColor colorWithCalibratedWhite:0.88 - state.depth * 0.52 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(x, meter.origin.y - 3.0, 3.0, 16.0));
    }

    NSRect depth = NSMakeRect(532, 34, 270, 250);
    NSRect output = NSMakeRect(532, 300, 270, 96);
    s3g::clap_gui::drawPanelFrame(depth.origin.x, depth.origin.y, depth.size.width, depth.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"DEPTH", true, depth.origin.x, depth.origin.y, depth.size.width, 21, text, style);
    [self drawSlider:@"DEP" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.depth * 100.0f)] norm:(p->params.depth + 1.0) * 0.5 y:74 attrs:small style:style];
    if constexpr (!kSingleField) {
        [self drawSlider:@"SPRD" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.spread * 100.0f)] norm:(p->params.spread + 1.0) * 0.5 y:100 attrs:small style:style];
        [self drawSlider:@"FOC" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.focus * 100.0f)] norm:(p->params.focus + 1.0) * 0.5 y:126 attrs:small style:style];
        [self drawSlider:@"AIR" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.air * 100.0f)] norm:(p->params.air + 1.0) * 0.5 y:152 attrs:small style:style];
        [self drawSlider:@"TAIL" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.tail * 100.0f)] norm:p->params.tail y:178 attrs:small style:style];
        [self drawSlider:@"LOW" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.low * 100.0f)] norm:(p->params.low + 1.0) * 0.5 y:204 attrs:small style:style];
        [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.width)] norm:p->params.width / 1.5 y:230 attrs:small style:style];
    } else {
        [self drawSlider:@"FOC" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.focus * 100.0f)] norm:(p->params.focus + 1.0) * 0.5 y:100 attrs:small style:style];
        [self drawSlider:@"AIR" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.air * 100.0f)] norm:(p->params.air + 1.0) * 0.5 y:126 attrs:small style:style];
        [self drawSlider:@"TAIL" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.tail * 100.0f)] norm:p->params.tail y:152 attrs:small style:style];
        [self drawSlider:@"LOW" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.low * 100.0f)] norm:(p->params.low + 1.0) * 0.5 y:178 attrs:small style:style];
        [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.width)] norm:p->params.width / 1.5 y:204 attrs:small style:style];
    }

    s3g::clap_gui::drawPanelFrame(output.origin.x, output.origin.y, output.size.width, output.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, output.origin.x, output.origin.y, output.size.width, 21, text, style);
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(p->params.outputGainDb)] norm:(p->params.outputGainDb + 60.0) / 72.0 y:340 attrs:small style:style];
}
- (void)resetSlider:(int)index
{
    if constexpr (kSingleField) {
        switch (index) {
        case 0: [self setParam:kParamDepth value:0.0]; break;
        case 1: [self setParam:kParamFocus value:0.0]; break;
        case 2: [self setParam:kParamAir value:0.0]; break;
        case 3: [self setParam:kParamTail value:0.0]; break;
        case 4: [self setParam:kParamLow value:0.0]; break;
        case 5: [self setParam:kParamWidth value:1.0]; break;
        case 6: [self setParam:kParamOutput value:0.0]; break;
        default: break;
        }
    } else {
        switch (index) {
        case 0: [self setParam:kParamDepth value:0.0]; break;
        case 1: [self setParam:kParamSpread value:0.0]; break;
        case 2: [self setParam:kParamFocus value:0.0]; break;
        case 3: [self setParam:kParamAir value:0.0]; break;
        case 4: [self setParam:kParamTail value:0.0]; break;
        case 5: [self setParam:kParamLow value:0.0]; break;
        case 6: [self setParam:kParamWidth value:1.0]; break;
        case 7: [self setParam:kParamOutput value:0.0]; break;
        default: break;
        }
    }
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp((pt.x - 632.0) / 128.0, 0.0, 1.0);
    if constexpr (kSingleField) {
        switch (_dragSlider) {
        case 0: [self setParam:kParamDepth value:-1.0 + norm * 2.0]; break;
        case 1: [self setParam:kParamFocus value:-1.0 + norm * 2.0]; break;
        case 2: [self setParam:kParamAir value:-1.0 + norm * 2.0]; break;
        case 3: [self setParam:kParamTail value:norm]; break;
        case 4: [self setParam:kParamLow value:-1.0 + norm * 2.0]; break;
        case 5: [self setParam:kParamWidth value:norm * 1.5]; break;
        case 6: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
        default: break;
        }
        return;
    }
    switch (_dragSlider) {
    case 0: [self setParam:kParamDepth value:-1.0 + norm * 2.0]; break;
    case 1: [self setParam:kParamSpread value:-1.0 + norm * 2.0]; break;
    case 2: [self setParam:kParamFocus value:-1.0 + norm * 2.0]; break;
    case 3: [self setParam:kParamAir value:-1.0 + norm * 2.0]; break;
    case 4: [self setParam:kParamTail value:norm]; break;
    case 5: [self setParam:kParamLow value:-1.0 + norm * 2.0]; break;
    case 6: [self setParam:kParamWidth value:norm * 1.5]; break;
    case 7: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    default: break;
    }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat groupYs[] = { 74, 100, 126, 152, 178, 204, 230, 340 };
    const CGFloat singleYs[] = { 74, 100, 126, 152, 178, 204, 340 };
    const CGFloat* ys = kSingleField ? singleYs : groupYs;
    const int count = kSingleField ? 7 : 8;
    for (int i = 0; i < count; ++i) {
        if (NSPointInRect(pt, NSMakeRect(538, ys[i] - 8, 246, 24))) {
            if ([event clickCount] >= 2) {
                [self resetSlider:i];
                return;
            }
            _dragSlider = i;
            [self updateSliderAtPoint:pt];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider >= 0) [self updateSliderAtPoint:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbiGroupDepthView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GAmbiGroupDepthView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = false; hints->can_resize_vertically = false; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbiGroupDepthView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbiGroupDepthView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
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

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kPluginName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    kPluginDesc,
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostTail = host && host->get_extension ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL)) : nullptr;
    p->params = s3g::sanitizeAmbiGroupDepthParams(p->params);
    p->processor.prepare(48000.0);
    p->processor.setParams(p->params);
    p->processor.reset();
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
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
