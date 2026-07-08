#include "s3g_ambisonic_utilities.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/ambisonic.h>
#include <clap/ext/audio-ports.h>
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

constexpr uint32_t kChannels = s3g::kAmbiUtilityChannels;
constexpr uint32_t kStateVersion = 1;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamYaw = 2,
    kParamPitch = 3,
    kParamRoll = 4,
    kParamWidth = 5,
    kParamOutput = 6,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiRotateParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    s3g::AmbiRotateParams params {};
    s3g::AmbiRotateProcessor processor {};
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }
uint32_t roundedUint(double value) { return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5))); }

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kParamOrder: p.params.order = std::clamp<uint32_t>(roundedUint(value), 1u, s3g::kAmbiUtilityMaxOrder); break;
    case kParamYaw: p.params.yawDeg = static_cast<float>(value); break;
    case kParamPitch: p.params.pitchDeg = static_cast<float>(value); break;
    case kParamRoll: p.params.rollDeg = static_cast<float>(value); break;
    case kParamWidth: p.params.width = static_cast<float>(value); break;
    case kParamOutput: p.params.outputGainDb = static_cast<float>(value); break;
    default: return;
    }
    p.params = s3g::sanitizeAmbiRotateParams(p.params);
    p.processor.setParams(p.params);
}

double getParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kParamOrder: return p.params.order;
    case kParamYaw: return p.params.yawDeg;
    case kParamPitch: return p.params.pitchDeg;
    case kParamRoll: return p.params.rollDeg;
    case kParamWidth: return p.params.width;
    case kParamOutput: return p.params.outputGainDb;
    default: return 0.0;
    }
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin) { delete self(plugin); }

bool activate(const clap_plugin_t* plugin, double, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->params = s3g::sanitizeAmbiRotateParams(p->params);
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
    p.processor.process(in, out, inChannels, outChannels, frames);
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
    std::strncpy(info->name, isInput ? "Ambisonic In" : "Ambisonic Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 6; }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Rotate", sizeof(info->module));
    switch (index) {
    case 0: info->id = kParamOrder; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Ambisonic order", sizeof(info->name)); info->min_value = 1; info->max_value = 7; info->default_value = 7; return true;
    case 1: info->id = kParamYaw; std::strncpy(info->name, "Yaw / azimuth", sizeof(info->name)); info->min_value = -180; info->max_value = 180; info->default_value = 0; return true;
    case 2: info->id = kParamPitch; std::strncpy(info->name, "Pitch / elevation", sizeof(info->name)); info->min_value = -90; info->max_value = 90; info->default_value = 0; return true;
    case 3: info->id = kParamRoll; std::strncpy(info->name, "Roll", sizeof(info->name)); info->min_value = -180; info->max_value = 180; info->default_value = 0; return true;
    case 4: info->id = kParamWidth; std::strncpy(info->name, "Order width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; return true;
    case 5: info->id = kParamOutput; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; return true;
    default: return false;
    }
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParam(*self(plugin), paramId);
    return paramId >= kParamOrder && paramId <= kParamOutput;
}
bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    switch (paramId) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); return true;
    case kParamYaw:
    case kParamPitch:
    case kParamRoll: std::snprintf(display, size, "%+.0f deg", value); return true;
    case kParamWidth: std::snprintf(display, size, "%.2f", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    default: return false;
    }
}
bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return paramId >= kParamOrder && paramId <= kParamOutput;
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
    if (got != static_cast<int64_t>(sizeof(state)) || state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = s3g::sanitizeAmbiRotateParams(state.params);
    p->processor.setParams(p->params);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GAmbisonicRotateView : NSView {
    void* _plugin;
    int _dragSlider;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (NSPoint)project:(s3g::Vec3)v rect:(NSRect)rect scale:(CGFloat)scale;
- (void)updateSliderAtPoint:(NSPoint)pt;
@end

@implementation S3GAmbisonicRotateView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 760, 430)];
    if (self) { _plugin = plugin; _dragSlider = -1; _refreshTimer = nil; }
    return self;
}
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 24.0) target:self selector:@selector(refreshTimerFired:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_refreshTimer) { [_refreshTimer invalidate]; _refreshTimer = nil; } }
- (void)refreshTimerFired:(NSTimer*)timer { (void)timer; if (![self isHidden]) [self setNeedsDisplay:YES]; }
- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 496, 570, 698, 112);
}
- (NSPoint)project:(s3g::Vec3)v rect:(NSRect)rect scale:(CGFloat)scale
{
    const CGFloat cx = rect.origin.x + rect.size.width * 0.5;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    return NSMakePoint(cx - v.y * scale + v.x * scale * 0.42, cy - v.z * scale - v.x * scale * 0.28);
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSDictionary* text = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:mono };
    [@"s3g AMBISONIC ROTATE 64" drawAtPoint:NSMakePoint(18, 13) withAttributes:text];
    [[NSString stringWithFormat:@"%uOA ACN/SN3D / 64CH", p->params.order] drawAtPoint:NSMakePoint(590, 13) withAttributes:small];

    NSRect fieldPanel = NSMakeRect(12, 34, 456, 366);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ROTATION FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    NSRect field = NSMakeRect(28, 70, 424, 304);
    [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);
    const CGFloat scale = std::min(field.size.width, field.size.height) * 0.36;
    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.54;
    [s3g::clap_gui::color(0x777777, 0.22) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 24, cy) toPoint:NSMakePoint(NSMaxX(field) - 24, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, field.origin.y + 18) toPoint:NSMakePoint(cx, NSMaxY(field) - 18)];

    const s3g::Vec3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    NSString* labels[3] = { @"FRONT", @"LEFT", @"UP" };
    for (int i = 0; i < 3; ++i) {
        const NSPoint a = [self project:axes[i] rect:field scale:scale];
        [s3g::clap_gui::color(0xbdbdbd, 0.46) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, cy) toPoint:a];
        [labels[i] drawAtPoint:NSMakePoint(a.x + 6, a.y - 6) withAttributes:small];
    }
    const float pts[8][3] = {
        { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
        { 1, -1, 1 }, { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 },
    };
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    NSPoint projected[8];
    for (int i = 0; i < 8; ++i) {
        s3g::Vec3 v = s3g::normalize({ pts[i][0], pts[i][1], pts[i][2] });
        v = s3g::ambiUtilityRotate(v, p->params.yawDeg, p->params.pitchDeg, p->params.rollDeg);
        projected[i] = [self project:v rect:field scale:scale * 0.88];
    }
    [s3g::clap_gui::color(0xd0d0d0, 0.48) setStroke];
    for (const auto& e : edges) [NSBezierPath strokeLineFromPoint:projected[e[0]] toPoint:projected[e[1]]];
    [style.text setFill];
    for (auto pt : projected) NSRectFill(NSMakeRect(pt.x - 3, pt.y - 3, 6, 6));
    [[NSString stringWithFormat:@"YAW %+.0f / PIT %+.0f / ROLL %+.0f",
      static_cast<double>(p->params.yawDeg), static_cast<double>(p->params.pitchDeg), static_cast<double>(p->params.rollDeg)]
        drawAtPoint:NSMakePoint(28, 382) withAttributes:small];

    NSRect rotate = NSMakeRect(482, 34, 258, 184);
    NSRect output = NSMakeRect(482, 230, 258, 96);
    s3g::clap_gui::drawPanelFrame(rotate.origin.x, rotate.origin.y, rotate.size.width, rotate.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ROTATE", true, rotate.origin.x, rotate.origin.y, rotate.size.width, 21, text, style);
    [self drawSlider:@"ORD" value:[NSString stringWithFormat:@"%uOA", p->params.order] norm:(p->params.order - 1.0) / 6.0 y:74 attrs:small style:style];
    [self drawSlider:@"YAW" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.yawDeg)] norm:(p->params.yawDeg + 180.0) / 360.0 y:96 attrs:small style:style];
    [self drawSlider:@"PIT" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.pitchDeg)] norm:(p->params.pitchDeg + 90.0) / 180.0 y:118 attrs:small style:style];
    [self drawSlider:@"ROL" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.rollDeg)] norm:(p->params.rollDeg + 180.0) / 360.0 y:140 attrs:small style:style];
    [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.width)] norm:p->params.width / 1.5 y:162 attrs:small style:style];

    s3g::clap_gui::drawPanelFrame(output.origin.x, output.origin.y, output.size.width, output.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, output.origin.x, output.origin.y, output.size.width, 21, text, style);
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(p->params.outputGainDb)] norm:(p->params.outputGainDb + 60.0) / 72.0 y:270 attrs:small style:style];
    const float pk = p->outputPeak.exchange(p->outputPeak.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    const double db = 20.0 * std::log10(std::max(0.000001f, pk));
    const CGFloat norm = std::clamp<CGFloat>((db + 60.0) / 60.0, 0.0, 1.0);
    [@"PK" drawAtPoint:NSMakePoint(496, 296) withAttributes:small];
    [style.strip setFill]; NSRectFill(NSMakeRect(570, 298, 112, 12));
    [style.fill setFill]; NSRectFill(NSMakeRect(571, 299, 110 * norm, 10));
    [style.grid setStroke]; NSFrameRect(NSMakeRect(570, 298, 112, 12));
    [[NSString stringWithFormat:@"%+4.1f", db] drawAtPoint:NSMakePoint(698, 294) withAttributes:small];
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp((pt.x - 570.0) / 112.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 0: [self setParam:kParamOrder value:1.0 + norm * 6.0]; break;
    case 1: [self setParam:kParamYaw value:-180.0 + norm * 360.0]; break;
    case 2: [self setParam:kParamPitch value:-90.0 + norm * 180.0]; break;
    case 3: [self setParam:kParamRoll value:-180.0 + norm * 360.0]; break;
    case 4: [self setParam:kParamWidth value:norm * 1.5]; break;
    case 5: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    default: break;
    }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat ys[] = { 74, 96, 118, 140, 162, 270 };
    for (int i = 0; i < 6; ++i) {
        if (NSPointInRect(pt, NSMakeRect(486, ys[i] - 6, 238, 22))) {
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
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbisonicRotateView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GAmbisonicRotateView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 760; *h = 430; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = false; hints->can_resize_vertically = false; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 760; *h = 430; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, 760, 430)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbisonicRotateView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicRotateView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
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

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_AMBISONIC, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambisonic-rotate-64",
    "s3g Ambisonic Rotate 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "64-channel ACN/SN3D ambisonic rotation utility up to 7OA.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::sanitizeAmbiRotateParams(p->params);
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
