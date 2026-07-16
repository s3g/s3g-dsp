#include "s3g_ambisonic_sub_decoder.h"
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
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>

namespace {

constexpr uint32_t kInputChannels = s3g::kAmbiSubDecoderMaxInputChannels;
constexpr uint32_t kOutputChannels = s3g::kAmbiSubDecoderMaxSubs;
constexpr uint32_t kStateVersion = 1;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kSubCountParamId = 2;
constexpr clap_id kCutoffParamId = 3;
constexpr clap_id kWidthParamId = 4;
constexpr clap_id kOutputParamId = 5;
constexpr clap_id kBypassParamId = 6;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiSubDecoderParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiSubDecoder decoder {};
    s3g::AmbiSubDecoderParams params {};
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
    case kOrderParamId: p.params.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, s3g::kAmbiSpeakerDecoderMaxOrder); break;
    case kSubCountParamId: p.params.subCount = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiSubDecoderMaxSubs); break;
    case kCutoffParamId: p.params.cutoffHz = s3g::clamp(static_cast<float>(value), 20.0f, 240.0f); break;
    case kWidthParamId: p.params.directionWidth = s3g::clamp(static_cast<float>(value), 0.0f, 2.0f); break;
    case kOutputParamId: p.params.outputGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f); break;
    case kBypassParamId: p.params.bypass = value >= 0.5; break;
    default: break;
    }
    p.decoder.setParams(p.params);
    p.params = p.decoder.params();
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
    p->decoder.prepare(sampleRate);
    p->decoder.setParams(p->params);
    p->params = p->decoder.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->decoder.reset();
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
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kInputChannels);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (!input.data32 || !output.data32 || outChannels == 0u) {
        if (output.data32) s3g::clearAudioBufferFromChannel(output, 0, frames);
        return CLAP_PROCESS_CONTINUE;
    }
    p->decoder.setParams(p->params);
    p->decoder.processBlock(input.data32, output.data32, inChannels, outChannels, frames);
    s3g::clearAudioBufferFromChannel(output, std::min<uint32_t>(outChannels, p->params.subCount), frames);
    const float peak = peakForChannels(output.data32, std::min<uint32_t>(outChannels, p->params.subCount), frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "7OA ACN/SN3D In" : "8 Sub Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kOrderParamId, "Order", 0.0, static_cast<double>(s3g::kAmbiSpeakerDecoderMaxOrder), 3.0, true },
    { kSubCountParamId, "Subs", 1.0, static_cast<double>(s3g::kAmbiSubDecoderMaxSubs), 1.0, true },
    { kCutoffParamId, "Cutoff", 20.0, 240.0, 90.0, false },
    { kWidthParamId, "Direction Width", 0.0, 2.0, 1.0, false },
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
    std::strncpy(info->module, "Ambi Sub Decoder", sizeof(info->module));
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
    case kOrderParamId: *value = p.order; return true;
    case kSubCountParamId: *value = p.subCount; return true;
    case kCutoffParamId: *value = p.cutoffHz; return true;
    case kWidthParamId: *value = p.directionWidth; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    case kBypassParamId: *value = p.bypass ? 1.0 : 0.0; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kSubCountParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kCutoffParamId) std::snprintf(display, size, "%.1f Hz", value);
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
    p->decoder.setParams(p->params);
    p->params = p->decoder.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

#if defined(__APPLE__)
constexpr uint32_t kGuiWidth = 600;
constexpr uint32_t kGuiHeight = 330;

} // namespace

@interface S3GAmbiSubDecoderView : NSView {
@private
    Plugin* _plugin;
    int _dragControl;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItemCount;
    NSTimer* _timer;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiSubDecoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragControl = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _menuItemCount = 0;
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

- (void)openOrderMenuAt:(NSPoint)origin
{
    _openMenu = 1;
    _hoverMenuItem = -1;
    _menuOrigin = origin;
    _menuItemCount = s3g::kAmbiSpeakerDecoderMaxOrder + 1u;
    [self setNeedsDisplay:YES];
}

- (void)closeMenu
{
    _openMenu = 0;
    _hoverMenuItem = -1;
    _menuItemCount = 0;
    [self setNeedsDisplay:YES];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu != 1 || _menuItemCount == 0) return;
    static NSString* orderItems[] = { @"0OA", @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    const CGFloat itemH = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 92.0, itemH * static_cast<CGFloat>(_menuItemCount));
    const int selected = static_cast<int>(std::clamp<uint32_t>(_plugin ? _plugin->params.order : 3u, 0u, s3g::kAmbiSpeakerDecoderMaxOrder));
    s3g::clap_gui::drawDropdownMenu(menuRect, itemH, orderItems, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawSubField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SUB FIELD", true, rect.origin.x, rect.origin.y, rect.size.width, 21, attrs, style);
    const CGFloat cx = NSMidX(rect);
    const CGFloat cy = rect.origin.y + 122.0;
    const CGFloat radius = 82.0;
    [s3g::clap_gui::color(0x575757, 0.22) setStroke];
    NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - radius, cy - radius, radius * 2.0, radius * 2.0)];
    [ring setLineWidth:1.0];
    [ring stroke];
    [s3g::clap_gui::color(0x383838, 0.55) setStroke];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - radius * 0.5, cy - radius * 0.5, radius, radius)] stroke];

    const uint32_t count = std::clamp<uint32_t>(_plugin->params.subCount, 1u, s3g::kAmbiSubDecoderMaxSubs);
    for (uint32_t i = 0; i < count; ++i) {
        const double az = (-45.0 - static_cast<double>(i) * 360.0 / static_cast<double>(count)) * M_PI / 180.0;
        const CGFloat x = cx - std::sin(az) * radius;
        const CGFloat y = cy - std::cos(az) * radius;
        NSRect sq = NSMakeRect(x - 6.0, y - 6.0, 12.0, 12.0);
        [style.fill setFill];
        NSRectFill(sq);
        [style.text setStroke];
        NSFrameRect(sq);
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        [label drawAtPoint:NSMakePoint(x + 9.0, y - 6.0) withAttributes:attrs];
    }

    NSString* caption = count == 1u ? @"W / mono sub decode" : @"horizontal FOA sub decode";
    [caption drawAtPoint:NSMakePoint(rect.origin.x + 20.0, NSMaxY(rect) - 34.0) withAttributes:attrs];
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
    [@"s3g AMBI SUB DECODER" drawAtPoint:NSMakePoint(20, 17) withAttributes:attrs];
    [[NSString stringWithFormat:@"%uOA / %u subs / %.1f Hz", _plugin->params.order, _plugin->params.subCount, _plugin->params.cutoffHz]
        drawAtPoint:NSMakePoint(382, 17)
        withAttributes:dim];

    [self drawSubField:NSMakeRect(20, 66, 244, 230) attrs:attrs style:style];
    s3g::clap_gui::drawPanelFrame(286, 66, 284, 230, style);
    s3g::clap_gui::drawPanelHeader(@"DECODER", true, 286, 66, 284, 21, attrs, style);
    s3g::clap_gui::drawMenu(@"ORDER", [self textForParam:kOrderParamId value:_plugin->params.order], 112, attrs, attrs, style, 306, 402, 92);
    s3g::clap_gui::drawSlider(@"SUBS", [self textForParam:kSubCountParamId value:_plugin->params.subCount], (_plugin->params.subCount - 1.0) / 7.0, 146, attrs, attrs, style, 306, 402, 512, 92);
    s3g::clap_gui::drawSlider(@"CUTOFF", [self textForParam:kCutoffParamId value:_plugin->params.cutoffHz], (_plugin->params.cutoffHz - 20.0f) / 220.0f, 180, attrs, attrs, style, 306, 402, 512, 92);
    s3g::clap_gui::drawSlider(@"WIDTH", [self textForParam:kWidthParamId value:_plugin->params.directionWidth], _plugin->params.directionWidth / 2.0f, 214, attrs, attrs, style, 306, 402, 512, 92);
    s3g::clap_gui::drawSlider(@"OUTPUT", [self textForParam:kOutputParamId value:_plugin->params.outputGainDb], (_plugin->params.outputGainDb + 60.0f) / 78.0f, 248, attrs, attrs, style, 306, 402, 512, 92);
    s3g::clap_gui::drawToggle(@"BYPASS", _plugin->params.bypass, 278, attrs, attrs, style, 306, 402, 92);
    [self drawOpenMenu:attrs style:style];

}

- (void)updateDrag:(NSPoint)pt
{
    if (!_plugin || _dragControl < 0) return;
    const double n = std::clamp((static_cast<double>(pt.x) - 402.0) / 92.0, 0.0, 1.0);
    switch (_dragControl) {
    case 1: applyParam(*_plugin, kSubCountParamId, 1.0 + n * 7.0); break;
    case 2: applyParam(*_plugin, kCutoffParamId, 20.0 + n * 220.0); break;
    case 3: applyParam(*_plugin, kWidthParamId, n * 2.0); break;
    case 4: applyParam(*_plugin, kOutputParamId, -60.0 + n * 78.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu == 1) {
        const CGFloat itemH = 18.0;
        const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 92.0, itemH * static_cast<CGFloat>(_menuItemCount));
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuRect, itemH, _menuItemCount);
        if (hit >= 0) {
            applyParam(*_plugin, kOrderParamId, static_cast<double>(hit));
            [self closeMenu];
            return;
        }
        [self closeMenu];
    }
    if (NSPointInRect(pt, NSMakeRect(402, 110, 92, 22))) {
        [self openOrderMenuAt:NSMakePoint(402, 128)];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(402, 276, 92, 20))) {
        applyParam(*_plugin, kBypassParamId, _plugin->params.bypass ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat ys[] = { 146, 180, 214, 248 };
    const int controls[] = { 1, 2, 3, 4 };
    for (int i = 0; i < 4; ++i) {
        if (NSPointInRect(pt, NSMakeRect(402, ys[i] - 5, 92, 22))) {
            _dragControl = controls[i];
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
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbiSubDecoderView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p && p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GAmbiSubDecoderView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbiSubDecoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbiSubDecoderView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambisonic-sub-decoder",
    "s3g Ambi Sub Decoder",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Ambisonic low-frequency subwoofer decoder using W-only mono or horizontal FOA decode for 1-8 subs.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->decoder.setParams(p->params);
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
