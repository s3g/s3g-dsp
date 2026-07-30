#include "s3g_array_trim.h"
#include "s3g_realtime.h"
#include "../common/s3g_objc_class_name.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
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

#ifndef S3G_ARRAY_TRIM_CHANNELS
#define S3G_ARRAY_TRIM_CHANNELS 16
#endif

#define S3G_ARRAY_TRIM_VIEW_CLASS \
    S3G_OBJC_CLASS_JOIN(S3GArrayTrimView, S3G_ARRAY_TRIM_CHANNELS)
#define S3G_ARRAY_TRIM_TEXT_CLASS \
    S3G_OBJC_CLASS_JOIN(S3GArrayTrimNumberTextField, S3G_ARRAY_TRIM_CHANNELS)

namespace {

constexpr uint32_t kChannelCount = S3G_ARRAY_TRIM_CHANNELS;
constexpr uint32_t kStateVersion = 2;

constexpr clap_id kActiveParamId = 1;
constexpr clap_id kSelectedParamId = 2;
constexpr clap_id kGainParamId = 3;
constexpr clap_id kMuteParamId = 4;
constexpr clap_id kInvertParamId = 5;
constexpr clap_id kOutputParamId = 6;
constexpr clap_id kBypassParamId = 7;
constexpr clap_id kGainParamBaseId = 1000;
constexpr clap_id kMuteParamBaseId = 2000;
constexpr clap_id kInvertParamBaseId = 3000;

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t selectedChannel = 0;
    s3g::ArrayTrimParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    s3g::ArrayTrim trim {};
    s3g::ArrayTrimParams params {};
    uint32_t selectedChannel = 0;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    char presetName[64] { "INIT" };
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
        p.selectedChannel = std::min<uint32_t>(p.selectedChannel, p.params.activeChannels - 1u);
        break;
    case kSelectedParamId:
        p.selectedChannel = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, kChannelCount) - 1u;
        break;
    case kGainParamId:
        p.params.gainDb[p.selectedChannel] = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        break;
    case kMuteParamId:
        p.params.mute[p.selectedChannel] = value >= 0.5 ? 1u : 0u;
        break;
    case kInvertParamId:
        p.params.invert[p.selectedChannel] = value >= 0.5 ? 1u : 0u;
        break;
    case kOutputParamId:
        p.params.outputGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        break;
    case kBypassParamId:
        p.params.bypass = value >= 0.5;
        break;
    default:
        if (id >= kGainParamBaseId && id < kGainParamBaseId + kChannelCount) {
            p.params.gainDb[id - kGainParamBaseId] = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        } else if (id >= kMuteParamBaseId && id < kMuteParamBaseId + kChannelCount) {
            p.params.mute[id - kMuteParamBaseId] = value >= 0.5 ? 1u : 0u;
        } else if (id >= kInvertParamBaseId && id < kInvertParamBaseId + kChannelCount) {
            p.params.invert[id - kInvertParamBaseId] = value >= 0.5 ? 1u : 0u;
        }
        break;
    }
    p.trim.setParams(p.params);
    p.params = p.trim.params();
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
    p->trim.prepare(sampleRate);
    p->trim.setParams(p->params);
    p->params = p->trim.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->outputPeak.store(0.0f, std::memory_order_relaxed); }

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
    p->trim.setParams(p->params);
    p->trim.processBlock(input.data32, output.data32, inChannels, outChannels, frames);
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

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)) + kChannelCount * 3u; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    if (index >= std::size(kParams)) {
        const uint32_t offset = index - static_cast<uint32_t>(std::size(kParams));
        const uint32_t group = offset / kChannelCount;
        const uint32_t channel = offset % kChannelCount;
        info->id = (group == 0u ? kGainParamBaseId : (group == 1u ? kMuteParamBaseId : kInvertParamBaseId)) + channel;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | (group > 0u ? CLAP_PARAM_IS_STEPPED : 0);
        std::snprintf(info->name, sizeof(info->name), "%s %u", group == 0u ? "Trim" : (group == 1u ? "Mute" : "Invert"), channel + 1u);
        std::strncpy(info->module, "Array Trim", sizeof(info->module));
        info->min_value = group == 0u ? -60.0 : 0.0;
        info->max_value = group == 0u ? 18.0 : 1.0;
        info->default_value = 0.0;
        return true;
    }
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Array Trim", sizeof(info->module));
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
    case kSelectedParamId: *value = p->selectedChannel + 1u; return true;
    case kGainParamId: *value = p->params.gainDb[p->selectedChannel]; return true;
    case kMuteParamId: *value = p->params.mute[p->selectedChannel] ? 1.0 : 0.0; return true;
    case kInvertParamId: *value = p->params.invert[p->selectedChannel] ? 1.0 : 0.0; return true;
    case kOutputParamId: *value = p->params.outputGainDb; return true;
    case kBypassParamId: *value = p->params.bypass ? 1.0 : 0.0; return true;
    default:
        if (id >= kGainParamBaseId && id < kGainParamBaseId + kChannelCount) {
            *value = p->params.gainDb[id - kGainParamBaseId];
            return true;
        }
        if (id >= kMuteParamBaseId && id < kMuteParamBaseId + kChannelCount) {
            *value = p->params.mute[id - kMuteParamBaseId] ? 1.0 : 0.0;
            return true;
        }
        if (id >= kInvertParamBaseId && id < kInvertParamBaseId + kChannelCount) {
            *value = p->params.invert[id - kInvertParamBaseId] ? 1.0 : 0.0;
            return true;
        }
        return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kActiveParamId || id == kSelectedParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kGainParamId || id == kOutputParamId || (id >= kGainParamBaseId && id < kGainParamBaseId + kChannelCount)) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kMuteParamId || id == kInvertParamId || id == kBypassParamId || (id >= kMuteParamBaseId && id < kMuteParamBaseId + kChannelCount) || (id >= kInvertParamBaseId && id < kInvertParamBaseId + kChannelCount)) std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
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
    SavedState state { kStateVersion, p->selectedChannel, p->params };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readExact(stream, &state, sizeof(state)) || state.version > kStateVersion || state.version == 0u) return false;
    auto* p = self(plugin);
    p->selectedChannel = std::min<uint32_t>(state.selectedChannel, kChannelCount - 1u);
    p->params = state.params;
    p->trim.setParams(p->params);
    p->params = p->trim.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

#if defined(__APPLE__)
constexpr const auto& kArrayLayout = s3g::gui_layout::kArrayFamilyLayout;
constexpr uint32_t kGuiWidth =
    static_cast<uint32_t>(kArrayLayout.canvas.width);
constexpr uint32_t kGuiHeight =
    static_cast<uint32_t>(kArrayLayout.canvas.height);
constexpr uint32_t kRowsPerPage = kArrayLayout.rowsPerPage;
constexpr uint32_t kRowHeight =
    static_cast<uint32_t>(s3g::gui_layout::kStandardMetrics.rowPitch);

} // namespace

@interface S3G_ARRAY_TRIM_TEXT_CLASS : NSTextField
@end

@implementation S3G_ARRAY_TRIM_TEXT_CLASS
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

@interface S3G_ARRAY_TRIM_VIEW_CLASS : NSView {
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

@implementation S3G_ARRAY_TRIM_VIEW_CLASS
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
        for (uint32_t row = 0; row < kRowsPerPage; ++row) {
            NSTextField* field = [[S3G_ARRAY_TRIM_TEXT_CLASS alloc] initWithFrame:NSZeroRect];
            s3g::clap_gui::styleNumberTextField(field);
            [field setTarget:self];
            [field setAction:@selector(trimFieldChanged:)];
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
    if (!_plugin || [self isHidden] || !s3g::clap_support::hostAppIsActive()) return;
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

- (void)layoutTrimFields
{
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    const CGFloat rowTop = kArrayLayout.channelPlot.y;
    const CGFloat fieldX = kArrayLayout.channelValueColumn.x;
    const CGFloat fieldW = kArrayLayout.channelValueColumn.width;
    for (uint32_t row = 0; row < kRowsPerPage; ++row) {
        NSTextField* field = [_fields objectAtIndex:row];
        const uint32_t ch = pageStart + row;
        const BOOL active = ch < _plugin->params.activeChannels;
        const CGFloat y = rowTop + static_cast<CGFloat>(row) * static_cast<CGFloat>(kRowHeight);
        [field setTag:static_cast<NSInteger>(ch)];
        [field setFrame:NSMakeRect(fieldX, y - 4.0, fieldW, 22.0)];
        [field setHidden:!active];
        if (active && ![self isEditingField:field]) {
            [field setStringValue:[NSString stringWithFormat:@"%.2f", _plugin->params.gainDb[ch]]];
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
    s3g::clap_gui::styleNumberTextEditor(field);
}

- (void)controlTextDidBeginEditing:(NSNotification*)note
{
    NSTextField* field = (NSTextField*)[note object];
    s3g::clap_gui::styleActiveNumberTextField(field, true);
    [self styleEditorForField:field];
}

- (void)controlTextDidChange:(NSNotification*)note
{
    [self styleEditorForField:(NSTextField*)[note object]];
}

- (void)controlTextDidEndEditing:(NSNotification*)note
{
    NSTextField* field = (NSTextField*)[note object];
    s3g::clap_gui::styleActiveNumberTextField(field, false);
    [self trimFieldChanged:field];
}

- (void)trimFieldChanged:(id)sender
{
    NSTextField* field = (NSTextField*)sender;
    const uint32_t channel = static_cast<uint32_t>(std::clamp<long>(static_cast<long>([field tag]), 0L, static_cast<long>(kChannelCount - 1u)));
    const double value = std::clamp([[field stringValue] doubleValue], -60.0, 18.0);
    applyParam(*_plugin, kGainParamBaseId + channel, value);
    [field setStringValue:[NSString stringWithFormat:@"%.2f", _plugin->params.gainDb[channel]]];
    [self setNeedsDisplay:YES];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (commandSelector == @selector(insertNewline:) || commandSelector == @selector(insertTab:)) {
        [self trimFieldChanged:control];
        [[self window] makeFirstResponder:self];
        return YES;
    }
    return NO;
}

- (void)drawButton:(NSString*)label rect:(NSRect)rect active:(bool)active attrs:(NSDictionary*)attrs
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

- (void)drawTrimRows:(NSRect)rect attrs:(NSDictionary*)attrs dim:(NSDictionary*)dim style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"CHANNEL TRIMS", true, rect.origin.x, rect.origin.y, rect.size.width, 21, attrs, style);
    [[NSString stringWithFormat:@"%u/%u", _page + 1u, [self pageCount]] drawAtPoint:NSMakePoint(rect.origin.x + rect.size.width - 58.0, rect.origin.y + 5.0) withAttributes:dim];
    [self drawPageButton:@"<" rect:NSMakeRect(rect.origin.x + rect.size.width - 94.0, rect.origin.y + 3.0, 26.0, 17.0) enabled:_page > 0u attrs:attrs];
    [self drawPageButton:@">" rect:NSMakeRect(rect.origin.x + rect.size.width - 32.0, rect.origin.y + 3.0, 26.0, 17.0) enabled:_page + 1u < [self pageCount] attrs:attrs];
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    NSRect plot = s3g::clap_gui::cocoaRect(kArrayLayout.channelPlot);
    const CGFloat zeroX = plot.origin.x + plot.size.width * (60.0 / 78.0);
    [s3g::clap_gui::color(0x5b5b5b) setStroke];
    NSBezierPath* zero = [NSBezierPath bezierPath];
    [zero moveToPoint:NSMakePoint(zeroX, plot.origin.y)];
    [zero lineToPoint:NSMakePoint(zeroX, NSMaxY(plot))];
    [zero stroke];
    [@"-60" drawAtPoint:NSMakePoint(plot.origin.x, rect.origin.y + 20.0) withAttributes:dim];
    [@"0" drawAtPoint:NSMakePoint(zeroX - 4.0, rect.origin.y + 20.0) withAttributes:dim];
    [@"+18 dB" drawAtPoint:NSMakePoint(NSMaxX(plot) - 42.0, rect.origin.y + 20.0) withAttributes:dim];

    const uint32_t n = std::min<uint32_t>(kRowsPerPage, _plugin->params.activeChannels - pageStart);
    for (uint32_t row = 0; row < n; ++row) {
        const uint32_t ch = pageStart + row;
        const CGFloat norm = std::clamp<CGFloat>((_plugin->params.gainDb[ch] + 60.0f) / 78.0f, 0.0, 1.0);
        const CGFloat x = plot.origin.x + plot.size.width * norm;
        const CGFloat y = plot.origin.y
            + static_cast<CGFloat>(row) * static_cast<CGFloat>(kRowHeight);
        const bool off = _plugin->params.mute[ch] != 0u;
        const bool inv = _plugin->params.invert[ch] != 0u;
        [[NSString stringWithFormat:@"%02u", ch + 1u] drawAtPoint:NSMakePoint(rect.origin.x + 16.0, y - 1.0) withAttributes:dim];
        [s3g::clap_gui::color(0x202020) setFill];
        NSRectFill(NSMakeRect(plot.origin.x, y, plot.size.width, 14.0));
        [s3g::clap_gui::color(0x555555) setStroke];
        NSFrameRect(NSMakeRect(plot.origin.x, y, plot.size.width, 14.0));
        NSRect bar = NSMakeRect(std::min(x, zeroX), y, std::max<CGFloat>(1.0, std::fabs(x - zeroX)), 14.0);
        [s3g::clap_gui::color(off ? 0x4a4a4a : (inv ? 0x9c9c9c : 0xa0a0a0)) setFill];
        NSRectFill(bar);
        [self drawButton:@"M" rect:NSMakeRect(
            kArrayLayout.channelMuteColumn.x, y - 4.0,
            kArrayLayout.channelMuteColumn.width, 20.0)
            active:off attrs:attrs];
        [self drawButton:@"INV" rect:NSMakeRect(
            kArrayLayout.channelInvertColumn.x, y - 4.0,
            kArrayLayout.channelInvertColumn.width, 20.0)
            active:inv attrs:attrs];
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
    const auto titleBand =
        s3g::gui_layout::arrayTitleBand(kArrayLayout.canvas);

    [style.bg setFill];
    NSRectFill(self.bounds);
    s3g::clap_gui::drawArrayTitleBand(
        [NSString stringWithFormat:@"s3g ARRAY TRIM %uCH", kChannelCount],
        [NSString stringWithUTF8String:_plugin->presetName],
        s3g::clap_gui::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);

    const auto drawPanel =
        [&](NSString* title, const s3g::gui_layout::Panel& panel) {
            s3g::clap_gui::drawPanelFrame(panel, style);
            s3g::clap_gui::drawPanelHeader(
                title, true, panel, attrs, style);
        };
    drawPanel(@"OUTPUT", kArrayLayout.output);
    drawPanel(@"ARRAY", kArrayLayout.array);

    [self clampPage];
    [self drawTrimRows:s3g::clap_gui::cocoaRect(kArrayLayout.editor.frame)
        attrs:attrs dim:dim style:style];
    s3g::clap_gui::drawProcessorSlider(
        @"OUT", [self textForParam:kOutputParamId
            value:_plugin->params.outputGainDb],
        (_plugin->params.outputGainDb + 60.0) / 78.0,
        s3g::gui_layout::rowY(kArrayLayout.output, 0u),
        kArrayLayout.output.frame.x, kArrayLayout.output.frame.width,
        attrs, dim, style);
    s3g::clap_gui::drawToggle(
        @"BYPASS", _plugin->params.bypass,
        s3g::gui_layout::rowY(kArrayLayout.output, 1u),
        attrs, dim, style,
        s3g::gui_layout::processorLabelX(
            kArrayLayout.output.frame.x),
        s3g::gui_layout::processorControlX(
            kArrayLayout.output.frame.x),
        74.0);
    s3g::clap_gui::drawProcessorSlider(
        @"ACTIVE", [self textForParam:kActiveParamId
            value:_plugin->params.activeChannels],
        (_plugin->params.activeChannels - 1.0)
            / std::max(1.0, static_cast<double>(kChannelCount - 1u)),
        s3g::gui_layout::rowY(kArrayLayout.array, 0u),
        kArrayLayout.array.frame.x, kArrayLayout.array.frame.width,
        attrs, dim, style);
    [self layoutTrimFields];
}

- (void)updateDrag:(NSPoint)pt
{
    if (!_plugin || _dragControl < 0) return;
    switch (_dragControl) {
    case 0: {
        const double n = std::clamp(
            (static_cast<double>(pt.x)
                - s3g::gui_layout::processorControlX(
                    kArrayLayout.array.frame.x))
            / s3g::gui_layout::processorTrackWidth(
                kArrayLayout.array.frame.width), 0.0, 1.0);
        applyParam(*_plugin, kActiveParamId,
            1.0 + n * static_cast<double>(kChannelCount - 1u));
        break;
    }
    case 1: {
        const double n = std::clamp(
            (static_cast<double>(pt.x)
                - s3g::gui_layout::processorControlX(
                    kArrayLayout.output.frame.x))
            / s3g::gui_layout::processorTrackWidth(
                kArrayLayout.output.frame.width), 0.0, 1.0);
        applyParam(*_plugin, kOutputParamId, -60.0 + n * 78.0);
        break;
    }
    case 2:
        [self updateTrimRowAtPoint:pt];
        break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)updateTrimRowAtPoint:(NSPoint)pt
{
    const uint32_t pageStart = _page * kRowsPerPage;
    const uint32_t n = std::min<uint32_t>(kRowsPerPage, _plugin->params.activeChannels - pageStart);
    const int row = static_cast<int>(std::floor(
        (pt.y - kArrayLayout.channelPlot.y)
            / static_cast<CGFloat>(kRowHeight)));
    if (row < 0 || row >= static_cast<int>(n)) return;
    const uint32_t ch = pageStart + static_cast<uint32_t>(row);
    _dragChannel = static_cast<int>(ch);
    const double rowN = std::clamp(
        (static_cast<double>(pt.x) - kArrayLayout.channelPlot.x)
            / kArrayLayout.channelPlot.width,
        0.0, 1.0);
    applyParam(*_plugin, kGainParamBaseId + ch, -60.0 + rowN * 78.0);
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self clampPage];
    const auto titleBand =
        s3g::gui_layout::arrayTitleBand(kArrayLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &_plugin->plugin, @"Array Trim", titleBand,
            _plugin->presetName, sizeof(_plugin->presetName), kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect rowPanel =
        s3g::clap_gui::cocoaRect(kArrayLayout.editor.frame);
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
    const NSRect bypassBox = NSMakeRect(
        s3g::gui_layout::processorControlX(kArrayLayout.output.frame.x),
        s3g::gui_layout::rowY(kArrayLayout.output, 1u) - 1.0,
        74.0, 15.0);
    if (NSPointInRect(pt, NSInsetRect(bypassBox, 0.0, -4.0))) {
        applyParam(*_plugin, kBypassParamId, _plugin->params.bypass ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    struct SliderHit {
        s3g::gui_layout::Rect rect;
        int control;
        clap_id param;
    };
    const SliderHit topHits[] {
        { s3g::gui_layout::sliderHitRect(kArrayLayout.output, 0u),
            1, kOutputParamId },
        { s3g::gui_layout::sliderHitRect(kArrayLayout.array, 0u),
            0, kActiveParamId },
    };
    for (const auto& hit : topHits) {
        if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(hit.rect))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &_plugin->plugin, hit.param, &defaultValue)) {
                applyParam(*_plugin, hit.param, defaultValue);
                _dragControl = -1;
            } else {
                _dragControl = hit.control;
                [self updateDrag:pt];
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const uint32_t pageStart = _page * kRowsPerPage;
    const uint32_t n = std::min<uint32_t>(kRowsPerPage, _plugin->params.activeChannels - pageStart);
    for (uint32_t row = 0; row < n; ++row) {
        const uint32_t ch = pageStart + row;
        const CGFloat y = kArrayLayout.channelPlot.y
            + static_cast<CGFloat>(row) * static_cast<CGFloat>(kRowHeight);
        if (NSPointInRect(pt, NSMakeRect(
                kArrayLayout.channelMuteColumn.x, y - 7.0,
                kArrayLayout.channelMuteColumn.width, 24.0))) {
            applyParam(*_plugin, kMuteParamBaseId + ch, _plugin->params.mute[ch] ? 0.0 : 1.0);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(pt, NSMakeRect(
                kArrayLayout.channelInvertColumn.x, y - 7.0,
                kArrayLayout.channelInvertColumn.width, 24.0))) {
            applyParam(*_plugin, kInvertParamBaseId + ch, _plugin->params.invert[ch] ? 0.0 : 1.0);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(pt, NSMakeRect(
                kArrayLayout.channelPlot.x, y - 7.0,
                kArrayLayout.channelPlot.width, 24.0))) {
            const clap_id param = kGainParamBaseId + ch;
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &_plugin->plugin, param, &defaultValue)) {
                applyParam(*_plugin, param, defaultValue);
                _dragControl = -1;
                _dragChannel = -1;
            } else {
                _dragControl = 2;
                _dragChannel = static_cast<int>(ch);
                [self updateDrag:pt];
            }
            [self setNeedsDisplay:YES];
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
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G_ARRAY_TRIM_VIEW_CLASS alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p && p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3G_ARRAY_TRIM_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<S3G_ARRAY_TRIM_VIEW_CLASS*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3G_ARRAY_TRIM_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    case 16: return "org.s3g.s3g-dsp.array-trim-16";
    case 26: return "org.s3g.s3g-dsp.array-trim-26";
    case 32: return "org.s3g.s3g-dsp.array-trim-32";
    default: return "org.s3g.s3g-dsp.array-trim-64";
    }
}

const char* pluginName()
{
    switch (kChannelCount) {
    case 16: return "s3g Array Trim 16";
    case 26: return "s3g Array Trim 26";
    case 32: return "s3g Array Trim 32";
    default: return "s3g Array Trim 64";
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
    "Per-channel speaker calibration gain, mute, and polarity trim for multichannel arrays.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params.activeChannels = kChannelCount;
    p->trim.setParams(p->params);
    p->params = p->trim.params();
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
