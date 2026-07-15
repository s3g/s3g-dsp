#include "s3g_array_delay.h"
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

#ifndef S3G_ARRAY_DELAY_CHANNELS
#define S3G_ARRAY_DELAY_CHANNELS 16
#endif

namespace {

constexpr uint32_t kChannelCount = S3G_ARRAY_DELAY_CHANNELS;
constexpr uint32_t kStateVersion = 2;

constexpr clap_id kActiveParamId = 1;
constexpr clap_id kOutputParamId = 5;
constexpr clap_id kBypassParamId = 6;
constexpr clap_id kDelayParamBaseId = 1000;
constexpr float kFixedMaxDelayMs = s3g::kArrayDelayDefaultMaxMs;

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t selectedChannel = 0;
    s3g::ArrayDelayParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::ArrayDelay delay {};
    s3g::ArrayDelayParams params {};
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
    case kActiveParamId:
        p.params.activeChannels = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, kChannelCount);
        break;
    case kOutputParamId:
        p.params.outputGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        break;
    case kBypassParamId:
        p.params.bypass = value >= 0.5;
        break;
    default:
        if (id >= kDelayParamBaseId && id < kDelayParamBaseId + kChannelCount) {
            p.params.delayMs[id - kDelayParamBaseId] = s3g::clamp(static_cast<float>(value), 0.0f, kFixedMaxDelayMs);
        }
        break;
    }
    p.params.maxDelayMs = kFixedMaxDelayMs;
    p.delay.setParams(p.params);
    p.params = p.delay.params();
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
    p->params.maxDelayMs = kFixedMaxDelayMs;
    p->delay.prepare(sampleRate);
    p->delay.setParams(p->params);
    p->params = p->delay.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->delay.reset();
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
    p->delay.setParams(p->params);
    p->delay.processBlock(input.data32, output.data32, inChannels, outChannels, frames);
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
    { kOutputParamId, "Output", -60.0, 18.0, 0.0, false },
    { kBypassParamId, "Bypass", 0.0, 1.0, 0.0, true },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)) + kChannelCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    if (index >= std::size(kParams)) {
        const uint32_t channel = index - static_cast<uint32_t>(std::size(kParams));
        info->id = kDelayParamBaseId + channel;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::snprintf(info->name, sizeof(info->name), "Delay %u", channel + 1u);
        std::strncpy(info->module, "Array Delay", sizeof(info->module));
        info->min_value = 0.0;
        info->max_value = kFixedMaxDelayMs;
        info->default_value = 0.0;
        return true;
    }
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Array Delay", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* p = self(plugin);
    switch (id) {
    case kActiveParamId: *value = p->params.activeChannels; return true;
    case kOutputParamId: *value = p->params.outputGainDb; return true;
    case kBypassParamId: *value = p->params.bypass ? 1.0 : 0.0; return true;
    default:
        if (id >= kDelayParamBaseId && id < kDelayParamBaseId + kChannelCount) {
            *value = p->params.delayMs[id - kDelayParamBaseId];
            return true;
        }
        return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kActiveParamId) std::snprintf(display, size, "%.0f", value);
    else if (id >= kDelayParamBaseId && id < kDelayParamBaseId + kChannelCount) std::snprintf(display, size, "%.3f ms", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kBypassParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    else std::snprintf(display, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (std::strcmp(display, "ON") == 0 || std::strcmp(display, "on") == 0) { *value = 1.0; return true; }
    if (std::strcmp(display, "OFF") == 0 || std::strcmp(display, "off") == 0) { *value = 0.0; return true; }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    SavedState state { kStateVersion, 0u, p->params };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readExact(stream, &state, sizeof(state)) || state.version > kStateVersion || state.version == 0u) return false;
    auto* p = self(plugin);
    p->params = state.params;
    p->params.maxDelayMs = kFixedMaxDelayMs;
    p->delay.setParams(p->params);
    p->params = p->delay.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

#if defined(__APPLE__)
constexpr uint32_t kGuiWidth = 720;
constexpr uint32_t kRowsPerPage = 8;
constexpr uint32_t kRowHeight = 26;
constexpr uint32_t kGuiHeight = 440;
constexpr CGFloat kActiveTrackX = 116.0;
constexpr CGFloat kActiveTrackW = 120.0;
constexpr CGFloat kOutputTrackX = 430.0;
constexpr CGFloat kOutputTrackW = 90.0;

} // namespace

@interface S3GNumberTextField : NSTextField
@end

@implementation S3GNumberTextField
- (void)mouseDown:(NSEvent*)event
{
    if ([event clickCount] > 1) {
        [[self window] makeFirstResponder:self];
        NSText* editor = [self currentEditor];
        if (editor) [editor setSelectedRange:NSMakeRange([[self stringValue] length], 0)];
        return;
    }
    [super mouseDown:event];
}
@end

@interface S3GArrayDelayView : NSView {
@private
    Plugin* _plugin;
    int _dragControl;
    int _dragChannel;
    uint32_t _page;
    NSTimer* _timer;
    NSMutableArray<NSTextField*>* _fields;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GArrayDelayView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragControl = -1;
        _dragChannel = -1;
        _page = 0;
        _timer = nil;
        _fields = [[NSMutableArray alloc] initWithCapacity:kRowsPerPage];
        [self setWantsLayer:YES];
        NSFont* font = [NSFont fontWithName:@"Menlo" size:11.0] ?: [NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular];
        for (uint32_t row = 0; row < kRowsPerPage; ++row) {
            NSTextField* field = [[S3GNumberTextField alloc] initWithFrame:NSZeroRect];
            [field setFont:font];
            [field setAlignment:NSTextAlignmentRight];
            [field setBezeled:YES];
            [field setBordered:YES];
            [field setEditable:YES];
            [field setSelectable:YES];
            [field setDrawsBackground:YES];
            [field setBackgroundColor:s3g::clap_gui::color(0x202020)];
            [field setTextColor:s3g::clap_gui::color(0xd0d0d0)];
            [field setFocusRingType:NSFocusRingTypeNone];
            [field setTarget:self];
            [field setAction:@selector(delayFieldChanged:)];
            [field setDelegate:(id<NSTextFieldDelegate>)self];
            [self addSubview:field];
            [_fields addObject:field];
            [field release];
        }
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)startRefreshTimer { if (!_timer) _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 24.0 target:self selector:@selector(refreshMeter:) userInfo:nil repeats:YES]; }
- (void)stopRefreshTimer { [_timer invalidate]; _timer = nil; }
- (void)refreshMeter:(NSTimer*)timer
{
    (void)timer;
    for (NSTextField* field in _fields) {
        if ([self isEditingField:field]) return;
    }
    [self setNeedsDisplay:YES];
}

- (void)dealloc
{
    [_fields release];
    [super dealloc];
}

- (NSString*)textForParam:(clap_id)param value:(double)value
{
    char buf[64] {};
    paramsValueToText(&_plugin->plugin, param, value, buf, sizeof(buf));
    return [NSString stringWithUTF8String:buf];
}

- (uint32_t)pageCount
{
    return std::max<uint32_t>(1u, (_plugin->params.activeChannels + kRowsPerPage - 1u) / kRowsPerPage);
}

- (void)clampPage
{
    _page = std::min<uint32_t>(_page, [self pageCount] - 1u);
}

- (void)layoutDelayFields
{
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    const CGFloat rowTop = 100.0;
    const CGFloat fieldX = 580.0;
    const CGFloat fieldW = 112.0;
    for (uint32_t row = 0; row < kRowsPerPage; ++row) {
        NSTextField* field = [_fields objectAtIndex:row];
        const uint32_t ch = pageStart + row;
        const BOOL active = ch < _plugin->params.activeChannels;
        const CGFloat y = rowTop + static_cast<CGFloat>(row) * static_cast<CGFloat>(kRowHeight);
        [field setTag:static_cast<NSInteger>(ch)];
        [field setFrame:NSMakeRect(fieldX, y - 4.0, fieldW, 22.0)];
        [field setHidden:!active];
        if (active && ![self isEditingField:field]) {
            [field setStringValue:[NSString stringWithFormat:@"%.3f", _plugin->params.delayMs[ch]]];
        }
    }
}

- (BOOL)isEditingField:(NSTextField*)field
{
    NSResponder* first = [[self window] firstResponder];
    return first == field || first == [field currentEditor];
}

- (void)styleEditorForField:(NSTextField*)field
{
    NSText* editor = [field currentEditor];
    if (!editor) return;
    if ([editor respondsToSelector:@selector(setSelectedTextAttributes:)]) {
        NSTextView* textView = (NSTextView*)editor;
        [textView setSelectedTextAttributes:@{
            NSBackgroundColorAttributeName: s3g::clap_gui::color(0x4a4a4a),
            NSForegroundColorAttributeName: s3g::clap_gui::color(0xf0f0f0)
        }];
        [textView setInsertionPointColor:s3g::clap_gui::color(0xd8d8d8)];
    }
}

- (void)controlTextDidBeginEditing:(NSNotification*)note
{
    NSTextField* field = (NSTextField*)[note object];
    [field setBackgroundColor:s3g::clap_gui::color(0x2a2a2a)];
    [self styleEditorForField:field];
}

- (void)controlTextDidChange:(NSNotification*)note
{
    [self styleEditorForField:(NSTextField*)[note object]];
}

- (void)controlTextDidEndEditing:(NSNotification*)note
{
    NSTextField* field = (NSTextField*)[note object];
    [field setBackgroundColor:s3g::clap_gui::color(0x202020)];
    [self delayFieldChanged:field];
}

- (void)delayFieldChanged:(id)sender
{
    NSTextField* field = (NSTextField*)sender;
    const uint32_t channel = static_cast<uint32_t>(std::clamp<long>(static_cast<long>([field tag]), 0L, static_cast<long>(kChannelCount - 1u)));
    const double value = std::clamp([[field stringValue] doubleValue], 0.0, static_cast<double>(kFixedMaxDelayMs));
    applyParam(*_plugin, kDelayParamBaseId + channel, value);
    [field setStringValue:[NSString stringWithFormat:@"%.3f", _plugin->params.delayMs[channel]]];
    [self setNeedsDisplay:YES];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (commandSelector == @selector(insertNewline:) || commandSelector == @selector(insertTab:)) {
        [self delayFieldChanged:control];
        [[self window] makeFirstResponder:self];
        return YES;
    }
    return NO;
}

- (void)drawToggleButton:(NSString*)label rect:(NSRect)rect active:(bool)active attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(active ? 0xb8b8b8 : 0x2a2a2a) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(rect);
    NSDictionary* textAttrs = @{ NSForegroundColorAttributeName:(active ? s3g::clap_gui::color(0x151515) : s3g::clap_gui::color(0xb0b0b0)),
                                 NSFontAttributeName:[attrs objectForKey:NSFontAttributeName] };
    NSSize size = [label sizeWithAttributes:textAttrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5, rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0) withAttributes:textAttrs];
}

- (void)drawPageButton:(NSString*)label rect:(NSRect)rect enabled:(bool)enabled attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(enabled ? 0x303030 : 0x202020) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(rect);
    NSDictionary* textAttrs = @{ NSForegroundColorAttributeName:(enabled ? s3g::clap_gui::color(0xd0d0d0) : s3g::clap_gui::color(0x666666)),
                                 NSFontAttributeName:[attrs objectForKey:NSFontAttributeName] };
    NSSize size = [label sizeWithAttributes:textAttrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5, rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0) withAttributes:textAttrs];
}

- (void)drawDelayRows:(NSRect)rect attrs:(NSDictionary*)attrs dim:(NSDictionary*)dim style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"CHANNEL DELAYS", true, rect.origin.x, rect.origin.y, rect.size.width, 21, attrs, style);
    [[NSString stringWithFormat:@"%u/%u", _page + 1u, [self pageCount]] drawAtPoint:NSMakePoint(rect.origin.x + rect.size.width - 58.0, rect.origin.y + 5.0) withAttributes:dim];
    [self drawPageButton:@"<" rect:NSMakeRect(rect.origin.x + rect.size.width - 94.0, rect.origin.y + 3.0, 26.0, 17.0) enabled:_page > 0u attrs:attrs];
    [self drawPageButton:@">" rect:NSMakeRect(rect.origin.x + rect.size.width - 32.0, rect.origin.y + 3.0, 26.0, 17.0) enabled:_page + 1u < [self pageCount] attrs:attrs];
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    NSRect plot = NSMakeRect(rect.origin.x + 48.0, rect.origin.y + 34.0, 500.0, rect.size.height - 54.0);
    [[NSString stringWithFormat:@"0 ms"] drawAtPoint:NSMakePoint(plot.origin.x, rect.origin.y + 20.0) withAttributes:dim];
    [[NSString stringWithFormat:@"%.0f ms", kFixedMaxDelayMs] drawAtPoint:NSMakePoint(NSMaxX(plot) - 54.0, rect.origin.y + 20.0) withAttributes:dim];
    const uint32_t n = std::min<uint32_t>(kRowsPerPage, _plugin->params.activeChannels - pageStart);
    for (uint32_t row = 0; row < n; ++row) {
        const uint32_t ch = pageStart + row;
        const CGFloat norm = std::clamp<CGFloat>(_plugin->params.delayMs[ch] / std::max(1.0f, _plugin->params.maxDelayMs), 0.0, 1.0);
        const CGFloat y = rect.origin.y + 34.0 + static_cast<CGFloat>(row) * static_cast<CGFloat>(kRowHeight);
        [[NSString stringWithFormat:@"%02u", ch + 1u] drawAtPoint:NSMakePoint(rect.origin.x + 16.0, y - 1.0) withAttributes:dim];
        [s3g::clap_gui::color(0x202020) setFill];
        NSRectFill(NSMakeRect(plot.origin.x, y, plot.size.width, 14.0));
        [s3g::clap_gui::color(0x555555) setStroke];
        NSFrameRect(NSMakeRect(plot.origin.x, y, plot.size.width, 14.0));
        NSRect bar = NSMakeRect(plot.origin.x, y, std::max<CGFloat>(1.0, plot.size.width * norm), 14.0);
        [s3g::clap_gui::color(0xa0a0a0) setFill];
        NSRectFill(bar);
    }
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
    [[NSString stringWithFormat:@"s3g ARRAY DELAY %u", kChannelCount] drawAtPoint:NSMakePoint(20, 17) withAttributes:attrs];
    [[NSString stringWithFormat:@"%u active / fixed %.0f ms capacity", _plugin->params.activeChannels, kFixedMaxDelayMs]
        drawAtPoint:NSMakePoint(360, 17)
        withAttributes:dim];

    [self clampPage];
    const CGFloat rowsH = static_cast<CGFloat>(kRowsPerPage) * static_cast<CGFloat>(kRowHeight) + 54.0;
    [self drawDelayRows:NSMakeRect(20, 66, 680, rowsH) attrs:attrs dim:dim style:style];
    const CGFloat controlsY = 84.0 + rowsH;
    s3g::clap_gui::drawPanelFrame(20, controlsY, 680, 68, style);
    s3g::clap_gui::drawPanelHeader(@"GLOBAL", true, 20, controlsY, 680, 21, attrs, style);
    s3g::clap_gui::drawSlider(@"ACTIVE", [self textForParam:kActiveParamId value:_plugin->params.activeChannels], (_plugin->params.activeChannels - 1.0) / std::max(1.0, static_cast<double>(kChannelCount - 1u)), controlsY + 38, attrs, attrs, style, 40, kActiveTrackX, 206, kActiveTrackW);
    s3g::clap_gui::drawSlider(@"OUTPUT", [self textForParam:kOutputParamId value:_plugin->params.outputGainDb], (_plugin->params.outputGainDb + 60.0) / 78.0, controlsY + 38, attrs, attrs, style, 350, kOutputTrackX, 516, kOutputTrackW);
    [self drawToggleButton:(_plugin->params.bypass ? @"BYPASS ON" : @"BYPASS") rect:NSMakeRect(626, controlsY + 30, 66, 24) active:_plugin->params.bypass attrs:attrs];
    [self layoutDelayFields];
}

- (void)updateDrag:(NSPoint)pt
{
    if (!_plugin || _dragControl < 0) return;
    const double n = std::clamp((static_cast<double>(pt.x) - kActiveTrackX) / kActiveTrackW, 0.0, 1.0);
    switch (_dragControl) {
    case 0: applyParam(*_plugin, kActiveParamId, 1.0 + n * static_cast<double>(kChannelCount - 1u)); break;
    case 1: applyParam(*_plugin, kOutputParamId, -60.0 + std::clamp((static_cast<double>(pt.x) - kOutputTrackX) / kOutputTrackW, 0.0, 1.0) * 78.0); break;
    case 2:
        [self updateDelayRowAtPoint:pt];
        break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)updateDelayRowAtPoint:(NSPoint)pt
{
    const uint32_t pageStart = _page * kRowsPerPage;
    const uint32_t n = std::min<uint32_t>(kRowsPerPage, _plugin->params.activeChannels - pageStart);
    const int row = static_cast<int>(std::floor((pt.y - 93.0) / static_cast<CGFloat>(kRowHeight)));
    if (row < 0 || row >= static_cast<int>(n)) return;
    const uint32_t ch = pageStart + static_cast<uint32_t>(row);
    _dragChannel = static_cast<int>(ch);
    const double rowN = std::clamp((static_cast<double>(pt.x) - 68.0) / 500.0, 0.0, 1.0);
    applyParam(*_plugin, kDelayParamBaseId + ch, rowN * kFixedMaxDelayMs);
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self clampPage];
    const CGFloat rowsH = static_cast<CGFloat>(kRowsPerPage) * static_cast<CGFloat>(kRowHeight) + 54.0;
    const CGFloat controlsY = 84.0 + rowsH;
    const NSRect rowPanel = NSMakeRect(20, 66, 680, rowsH);
    if (NSPointInRect(pt, NSMakeRect(rowPanel.origin.x + rowPanel.size.width - 94.0, rowPanel.origin.y + 3.0, 26.0, 17.0))) {
        if (_page > 0u) --_page;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(rowPanel.origin.x + rowPanel.size.width - 32.0, rowPanel.origin.y + 3.0, 26.0, 17.0))) {
        if (_page + 1u < [self pageCount]) ++_page;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(626, controlsY + 30, 66, 24))) {
        applyParam(*_plugin, kBypassParamId, _plugin->params.bypass ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(kActiveTrackX, controlsY + 33, kActiveTrackW, 22))) {
        _dragControl = 0;
        [self updateDrag:pt];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(kOutputTrackX, controlsY + 33, kOutputTrackW, 22))) {
        _dragControl = 1;
        [self updateDrag:pt];
        return;
    }
    const uint32_t pageStart = _page * kRowsPerPage;
    const uint32_t n = std::min<uint32_t>(kRowsPerPage, _plugin->params.activeChannels - pageStart);
    for (uint32_t row = 0; row < n; ++row) {
        const uint32_t ch = pageStart + row;
        const CGFloat y = 100.0 + static_cast<CGFloat>(row) * static_cast<CGFloat>(kRowHeight);
        if (NSPointInRect(pt, NSMakeRect(68, y - 7.0, 500, 24))) {
            _dragControl = 2;
            _dragChannel = static_cast<int>(ch);
            [self updateDrag:pt];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event { [self updateDrag:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragControl = -1; _dragChannel = -1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GArrayDelayView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p && p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GArrayDelayView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GArrayDelayView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GArrayDelayView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    case 16: return "org.s3g.s3g-dsp.array-delay-16";
    case 26: return "org.s3g.s3g-dsp.array-delay-26";
    case 32: return "org.s3g.s3g-dsp.array-delay-32";
    default: return "org.s3g.s3g-dsp.array-delay-64";
    }
}

const char* pluginName()
{
    switch (kChannelCount) {
    case 16: return "s3g Array Delay 16";
    case 26: return "s3g Array Delay 26";
    case 32: return "s3g Array Delay 32";
    default: return "s3g Array Delay 64";
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
    "Per-channel speaker calibration delay for multichannel arrays.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params.activeChannels = kChannelCount;
    p->params.maxDelayMs = kFixedMaxDelayMs;
    p->delay.setParams(p->params);
    p->params = p->delay.params();
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
