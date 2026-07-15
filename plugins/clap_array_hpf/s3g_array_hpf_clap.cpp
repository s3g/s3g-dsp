#include "s3g_array_hpf.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iterator>
#include <new>

#ifndef S3G_ARRAY_HPF_CHANNELS
#define S3G_ARRAY_HPF_CHANNELS 16
#endif

namespace {

constexpr uint32_t kChannelCount = S3G_ARRAY_HPF_CHANNELS;
constexpr uint32_t kStateVersion = 1;

constexpr clap_id kActiveParamId = 1;
constexpr clap_id kCutoffParamId = 2;
constexpr clap_id kPolesParamId = 3;
constexpr clap_id kOutputParamId = 4;
constexpr clap_id kBypassParamId = 5;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::ArrayHpfParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::ArrayHpf hpf {};
    s3g::ArrayHpfParams params {};
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->write(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->read(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kActiveParamId: p.params.activeChannels = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, kChannelCount); break;
    case kCutoffParamId: p.params.cutoffHz = s3g::clamp(static_cast<float>(value), 20.0f, 240.0f); break;
    case kPolesParamId: p.params.poles = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, 4u); break;
    case kOutputParamId: p.params.outputGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f); break;
    case kBypassParamId: p.params.bypass = value >= 0.5; break;
    default: break;
    }
    p.hpf.setParams(p.params);
    p.params = p.hpf.params();
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
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->hpf.prepare(sampleRate);
    p->hpf.setParams(p->params);
    p->params = p->hpf.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->hpf.reset();
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

float peakForChannels(float* const* output, uint32_t channels, uint32_t frames)
{
    float peak = 0.0f;
    if (!output) return peak;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (!output[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, std::fabs(output[ch][frame]));
    }
    return peak;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kChannelCount);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kChannelCount);
    if (!input.data32 || !output.data32 || outChannels == 0u) {
        if (output.data32) s3g::clearAudioBufferFromChannel(output, 0, frames);
        return CLAP_PROCESS_CONTINUE;
    }
    p->hpf.setParams(p->params);
    p->hpf.processBlock(input.data32, output.data32, inChannels, outChannels, frames);
    s3g::clearAudioBufferFromChannel(output, std::min<uint32_t>(outChannels, p->params.activeChannels), frames);
    const float peak = peakForChannels(output.data32, std::min<uint32_t>(outChannels, p->params.activeChannels), frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "%u Channel %s", kChannelCount, isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kActiveParamId, "Active Channels", 1.0, static_cast<double>(kChannelCount), static_cast<double>(kChannelCount), true },
    { kCutoffParamId, "Cutoff", 20.0, 240.0, 90.0, false },
    { kPolesParamId, "Poles", 1.0, 4.0, 2.0, true },
    { kOutputParamId, "Output", -60.0, 18.0, 0.0, false },
    { kBypassParamId, "Bypass", 0.0, 1.0, 0.0, true },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Array HPF", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto p = self(plugin)->params;
    switch (id) {
    case kActiveParamId: *value = p.activeChannels; return true;
    case kCutoffParamId: *value = p.cutoffHz; return true;
    case kPolesParamId: *value = p.poles; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    case kBypassParamId: *value = p.bypass ? 1.0 : 0.0; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kActiveParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kCutoffParamId) std::snprintf(display, size, "%.1f Hz", value);
    else if (id == kPolesParamId) std::snprintf(display, size, "%.0f pole", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kBypassParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    else std::snprintf(display, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (std::strcmp(display, "ON") == 0 || std::strcmp(display, "on") == 0) {
        *value = 1.0;
        return true;
    }
    if (std::strcmp(display, "OFF") == 0 || std::strcmp(display, "off") == 0) {
        *value = 0.0;
        return true;
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state { kStateVersion, self(plugin)->params };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readExact(stream, &state, sizeof(state)) || state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = state.params;
    p->hpf.setParams(p->params);
    p->params = p->hpf.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

#if defined(__APPLE__)
constexpr uint32_t kGuiWidth = 560;
constexpr uint32_t kGuiHeight = 300;

} // namespace

@interface S3GArrayHpfView : NSView {
@private
    Plugin* _plugin;
    int _dragControl;
    NSTimer* _timer;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

static double arrayHpfGuiClamp(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

static double arrayHpfFreqNorm(double hz)
{
    return arrayHpfGuiClamp((std::log(hz) - std::log(20.0)) / (std::log(20000.0) - std::log(20.0)), 0.0, 1.0);
}

static double arrayHpfNormFreq(double n)
{
    return std::exp(std::log(20.0) + arrayHpfGuiClamp(n, 0.0, 1.0) * (std::log(20000.0) - std::log(20.0)));
}

static double arrayHpfMagnitudeDb(double hz, double cutoffHz, uint32_t poles)
{
    const double ratio = std::max(0.000001, hz / std::max(1.0, cutoffHz));
    const double onePole = ratio / std::sqrt(1.0 + ratio * ratio);
    const double mag = std::pow(onePole, std::max<uint32_t>(1u, poles));
    return 20.0 * std::log10(std::max(0.000001, mag));
}

static double arrayHpfDbNorm(double db)
{
    return 1.0 - arrayHpfGuiClamp((db + 48.0) / 54.0, 0.0, 1.0);
}

@implementation S3GArrayHpfView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragControl = -1;
        _timer = nil;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 24.0 target:self selector:@selector(refreshMeter:) userInfo:nil repeats:YES];
}

- (void)stopRefreshTimer
{
    [_timer invalidate];
    _timer = nil;
}

- (void)refreshMeter:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (NSString*)textForParam:(clap_id)param value:(double)value
{
    char buf[64] {};
    paramsValueToText(&_plugin->plugin, param, value, buf, sizeof(buf));
    return [NSString stringWithUTF8String:buf];
}

- (void)drawFilterGraph:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"FILTER GRAPH", true, rect.origin.x, rect.origin.y, rect.size.width, 21, attrs, style);
    NSRect graph = NSInsetRect(rect, 18.0, 34.0);
    graph.origin.y += 12.0;
    graph.size.height -= 6.0;
    [s3g::clap_gui::color(0x181818) setFill];
    NSRectFill(graph);
    [s3g::clap_gui::color(0x474747) setStroke];
    NSFrameRect(graph);

    [s3g::clap_gui::color(0x2e2e2e, 0.75) setStroke];
    for (int i = 1; i < 4; ++i) {
        const CGFloat y = graph.origin.y + graph.size.height * static_cast<CGFloat>(i) / 4.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(graph.origin.x, y) toPoint:NSMakePoint(NSMaxX(graph), y)];
    }
    const double freqs[] = { 20.0, 60.0, 120.0, 240.0, 1000.0, 10000.0 };
    for (double f : freqs) {
        const CGFloat x = graph.origin.x + graph.size.width * static_cast<CGFloat>(arrayHpfFreqNorm(f));
        [s3g::clap_gui::color(0x272727, 0.85) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, graph.origin.y) toPoint:NSMakePoint(x, NSMaxY(graph))];
    }

    NSBezierPath* response = [NSBezierPath bezierPath];
    [response setLineWidth:2.0];
    for (int i = 0; i <= 160; ++i) {
        const double n = static_cast<double>(i) / 160.0;
        double mag = arrayHpfMagnitudeDb(arrayHpfNormFreq(n), _plugin->params.cutoffHz, _plugin->params.poles) + _plugin->params.outputGainDb;
        if (_plugin->params.bypass) mag = _plugin->params.outputGainDb;
        const CGFloat x = graph.origin.x + graph.size.width * static_cast<CGFloat>(n);
        const CGFloat y = graph.origin.y + graph.size.height * static_cast<CGFloat>(arrayHpfDbNorm(mag));
        if (i == 0) [response moveToPoint:NSMakePoint(x, y)];
        else [response lineToPoint:NSMakePoint(x, y)];
    }
    [s3g::clap_gui::color(_plugin->params.bypass ? 0x7a7a7a : 0xc8c8c8) setStroke];
    [response stroke];

    const CGFloat cutoffX = graph.origin.x + graph.size.width * static_cast<CGFloat>(arrayHpfFreqNorm(_plugin->params.cutoffHz));
    [s3g::clap_gui::color(0xadadad, 0.75) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cutoffX, graph.origin.y) toPoint:NSMakePoint(cutoffX, NSMaxY(graph))];
    [[NSString stringWithFormat:@"%.0f Hz", _plugin->params.cutoffHz] drawAtPoint:NSMakePoint(cutoffX + 6.0, graph.origin.y + 10.0) withAttributes:attrs];
    [[NSString stringWithFormat:@"%u dB/oct", _plugin->params.poles * 6u] drawAtPoint:NSMakePoint(graph.origin.x + 6.0, NSMaxY(graph) - 18.0) withAttributes:attrs];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style {};
    NSFont* font = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* attrs = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:font };
    NSDictionary* dim = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:font };

    [style.bg setFill];
    NSRectFill(self.bounds);
    [style.strip setFill];
    NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 46));
    [[NSString stringWithFormat:@"s3g ARRAY HPF %u", kChannelCount] drawAtPoint:NSMakePoint(20, 17) withAttributes:attrs];
    [[NSString stringWithFormat:@"%u active / %.1f Hz / %u pole", _plugin->params.activeChannels, _plugin->params.cutoffHz, _plugin->params.poles]
        drawAtPoint:NSMakePoint(314, 17)
        withAttributes:dim];

    [self drawFilterGraph:NSMakeRect(20, 66, 236, 202) attrs:attrs style:style];
    s3g::clap_gui::drawPanelFrame(278, 66, 252, 202, style);
    s3g::clap_gui::drawPanelHeader(@"FILTER", true, 278, 66, 252, 21, attrs, style);
    s3g::clap_gui::drawSlider(@"ACTIVE", [self textForParam:kActiveParamId value:_plugin->params.activeChannels], (_plugin->params.activeChannels - 1.0) / std::max(1.0, static_cast<double>(kChannelCount - 1u)), 112, attrs, attrs, style, 298, 372, 472, 86);
    s3g::clap_gui::drawSlider(@"CUTOFF", [self textForParam:kCutoffParamId value:_plugin->params.cutoffHz], (_plugin->params.cutoffHz - 20.0f) / 220.0f, 146, attrs, attrs, style, 298, 372, 472, 86);
    s3g::clap_gui::drawSlider(@"POLES", [self textForParam:kPolesParamId value:_plugin->params.poles], (_plugin->params.poles - 1.0) / 3.0, 180, attrs, attrs, style, 298, 372, 472, 86);
    s3g::clap_gui::drawSlider(@"OUTPUT", [self textForParam:kOutputParamId value:_plugin->params.outputGainDb], (_plugin->params.outputGainDb + 60.0f) / 78.0f, 214, attrs, attrs, style, 298, 372, 472, 86);
    s3g::clap_gui::drawSlider(@"BYPASS", (_plugin->params.bypass ? @"ON" : @"OFF"), _plugin->params.bypass ? 1.0 : 0.0, 248, attrs, attrs, style, 298, 372, 472, 86);
}

- (void)updateDrag:(NSPoint)pt
{
    if (!_plugin || _dragControl < 0) return;
    const double n = std::clamp((static_cast<double>(pt.x) - 372.0) / 86.0, 0.0, 1.0);
    switch (_dragControl) {
    case 0: applyParam(*_plugin, kActiveParamId, 1.0 + n * static_cast<double>(kChannelCount - 1u)); break;
    case 1: applyParam(*_plugin, kCutoffParamId, 20.0 + n * 220.0); break;
    case 2: applyParam(*_plugin, kPolesParamId, 1.0 + n * 3.0); break;
    case 3: applyParam(*_plugin, kOutputParamId, -60.0 + n * 78.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (NSPointInRect(pt, NSMakeRect(372, 246, 86, 20))) {
        applyParam(*_plugin, kBypassParamId, _plugin->params.bypass ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat ys[] = { 112, 146, 180, 214 };
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(pt, NSMakeRect(372, ys[i] - 5, 86, 22))) {
            _dragControl = i;
            [self updateDrag:pt];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event { [self updateDrag:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragControl = -1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GArrayHpfView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p && p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GArrayHpfView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GArrayHpfView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GArrayHpfView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };

const char* pluginId()
{
    switch (kChannelCount) {
    case 16: return "org.s3g.s3g-dsp.array-hpf-16";
    case 32: return "org.s3g.s3g-dsp.array-hpf-32";
    default: return "org.s3g.s3g-dsp.array-hpf-64";
    }
}

const char* pluginName()
{
    switch (kChannelCount) {
    case 16: return "s3g Array HPF 16";
    case 32: return "s3g Array HPF 32";
    default: return "s3g Array HPF 64";
    }
}

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    pluginId(),
    pluginName(),
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Post-decoder multichannel highpass filter for main speaker arrays.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params.activeChannels = kChannelCount;
    p->hpf.setParams(p->params);
    p->params = p->hpf.params();
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

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
