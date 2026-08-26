#include "s3g_array_calibrate.h"
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

#ifndef S3G_ARRAY_CALIBRATE_CHANNELS
#define S3G_ARRAY_CALIBRATE_CHANNELS 16
#endif

#define S3G_ARRAY_CALIBRATE_VIEW_CLASS \
    S3G_OBJC_CLASS_JOIN(S3GArrayCalibrateView, S3G_ARRAY_CALIBRATE_CHANNELS)
#define S3G_ARRAY_CALIBRATE_TEXT_CLASS \
    S3G_OBJC_CLASS_JOIN(S3GArrayCalibrateNumberTextField, S3G_ARRAY_CALIBRATE_CHANNELS)

namespace {

constexpr uint32_t kChannelCount = S3G_ARRAY_CALIBRATE_CHANNELS;
constexpr uint32_t kStateVersion = 1u;
constexpr float kFixedMaxDelayMs = s3g::kArrayDelayDefaultMaxMs;

constexpr clap_id kActiveParamId = 1;
constexpr clap_id kOutputParamId = 2;
constexpr clap_id kBypassParamId = 3;
constexpr clap_id kCutoffParamId = 10;
constexpr clap_id kPolesParamId = 11;
constexpr clap_id kHpfBypassParamId = 12;
constexpr clap_id kDelayBypassParamId = 20;
constexpr clap_id kTrimBypassParamId = 30;
constexpr clap_id kDelayParamBaseId = 1000;
constexpr clap_id kGainParamBaseId = 2000;
constexpr clap_id kMuteParamBaseId = 3000;
constexpr clap_id kInvertParamBaseId = 4000;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::ArrayCalibrateParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    s3g::ArrayCalibrate calibrate {};
    s3g::ArrayCalibrateParams params {};
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
    size_t done = 0u;
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
    size_t done = 0u;
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
        p.params.activeChannels = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 1u, kChannelCount);
        break;
    case kOutputParamId:
        p.params.outputGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        break;
    case kBypassParamId: p.params.bypass = value >= 0.5; break;
    case kCutoffParamId:
        p.params.cutoffHz = s3g::clamp(static_cast<float>(value), 20.0f, 240.0f);
        break;
    case kPolesParamId:
        p.params.poles = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 1u, 4u);
        break;
    case kHpfBypassParamId: p.params.hpfBypass = value >= 0.5; break;
    case kDelayBypassParamId: p.params.delayBypass = value >= 0.5; break;
    case kTrimBypassParamId: p.params.trimBypass = value >= 0.5; break;
    default:
        if (id >= kDelayParamBaseId && id < kDelayParamBaseId + kChannelCount) {
            p.params.delayMs[id - kDelayParamBaseId] =
                s3g::clamp(static_cast<float>(value), 0.0f, kFixedMaxDelayMs);
        } else if (id >= kGainParamBaseId && id < kGainParamBaseId + kChannelCount) {
            p.params.gainDb[id - kGainParamBaseId] =
                s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        } else if (id >= kMuteParamBaseId && id < kMuteParamBaseId + kChannelCount) {
            p.params.mute[id - kMuteParamBaseId] = value >= 0.5 ? 1u : 0u;
        } else if (id >= kInvertParamBaseId && id < kInvertParamBaseId + kChannelCount) {
            p.params.invert[id - kInvertParamBaseId] = value >= 0.5 ? 1u : 0u;
        }
        break;
    }
    p.calibrate.setParams(p.params);
    p.params = p.calibrate.params();
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maxFrameCount)
{
    auto* p = self(plugin);
    p->calibrate.prepare(sampleRate, std::max<uint32_t>(1u, maxFrameCount));
    p->calibrate.setParams(p->params);
    p->params = p->calibrate.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->calibrate.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID
            && ev->type == CLAP_EVENT_PARAM_VALUE) {
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
        for (uint32_t frame = 0; frame < frames; ++frame)
            peak = std::max(peak, std::fabs(output[ch][frame]));
    }
    return peak;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0u || proc->audio_outputs_count == 0u)
        return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kChannelCount);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kChannelCount);
    if (!input.data32 || !output.data32 || outChannels == 0u) {
        if (output.data32) s3g::clearAudioBufferFromChannel(output, 0u, frames);
        return CLAP_PROCESS_CONTINUE;
    }
    p->calibrate.setParams(p->params);
    p->calibrate.processBlock(input.data32, output.data32,
        inChannels, outChannels, frames);
    s3g::clearAudioBufferFromChannel(output,
        std::min<uint32_t>(outChannels, p->params.activeChannels), frames);
    const float peak = peakForChannels(output.data32,
        std::min<uint32_t>(outChannels, p->params.activeChannels), frames);
    p->outputPeak.store(std::max(
        p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%u Channel %s",
        kChannelCount, isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
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
    bool stepped;
};

constexpr ParamDef kParams[] {
    { kActiveParamId, "Active Channels", "Array", 1.0,
        static_cast<double>(kChannelCount), static_cast<double>(kChannelCount), true },
    { kOutputParamId, "Output", "Array", -60.0, 18.0, 0.0, false },
    { kBypassParamId, "Bypass", "Array", 0.0, 1.0, 0.0, true },
    { kCutoffParamId, "Cutoff", "HPF", 20.0, 240.0, 90.0, false },
    { kPolesParamId, "Poles", "HPF", 1.0, 4.0, 2.0, true },
    { kHpfBypassParamId, "HPF Bypass", "HPF", 0.0, 1.0, 0.0, true },
    { kDelayBypassParamId, "Delay Bypass", "Delay", 0.0, 1.0, 0.0, true },
    { kTrimBypassParamId, "Trim Bypass", "Trim", 0.0, 1.0, 0.0, true },
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(std::size(kParams)) + kChannelCount * 4u;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    std::memset(info, 0, sizeof(*info));
    if (index < std::size(kParams)) {
        const auto& def = kParams[index];
        info->id = def.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE
            | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
        std::strncpy(info->name, def.name, sizeof(info->name) - 1u);
        std::strncpy(info->module, def.module, sizeof(info->module) - 1u);
        info->min_value = def.min;
        info->max_value = def.max;
        info->default_value = def.def;
        return true;
    }
    const uint32_t offset = index - static_cast<uint32_t>(std::size(kParams));
    const uint32_t group = offset / kChannelCount;
    const uint32_t channel = offset % kChannelCount;
    static constexpr clap_id bases[] {
        kDelayParamBaseId, kGainParamBaseId, kMuteParamBaseId, kInvertParamBaseId
    };
    static constexpr const char* names[] { "Delay", "Trim", "Mute", "Invert" };
    static constexpr const char* modules[] { "Delay", "Trim", "Trim", "Trim" };
    info->id = bases[group] + channel;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (group >= 2u ? CLAP_PARAM_IS_STEPPED : 0u);
    std::snprintf(info->name, sizeof(info->name), "%s %u", names[group], channel + 1u);
    std::strncpy(info->module, modules[group], sizeof(info->module) - 1u);
    if (group == 0u) {
        info->min_value = 0.0;
        info->max_value = kFixedMaxDelayMs;
    } else if (group == 1u) {
        info->min_value = -60.0;
        info->max_value = 18.0;
    } else {
        info->min_value = 0.0;
        info->max_value = 1.0;
    }
    info->default_value = 0.0;
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
    case kCutoffParamId: *value = p->params.cutoffHz; return true;
    case kPolesParamId: *value = p->params.poles; return true;
    case kHpfBypassParamId: *value = p->params.hpfBypass ? 1.0 : 0.0; return true;
    case kDelayBypassParamId: *value = p->params.delayBypass ? 1.0 : 0.0; return true;
    case kTrimBypassParamId: *value = p->params.trimBypass ? 1.0 : 0.0; return true;
    default:
        if (id >= kDelayParamBaseId && id < kDelayParamBaseId + kChannelCount) {
            *value = p->params.delayMs[id - kDelayParamBaseId]; return true;
        }
        if (id >= kGainParamBaseId && id < kGainParamBaseId + kChannelCount) {
            *value = p->params.gainDb[id - kGainParamBaseId]; return true;
        }
        if (id >= kMuteParamBaseId && id < kMuteParamBaseId + kChannelCount) {
            *value = p->params.mute[id - kMuteParamBaseId] ? 1.0 : 0.0; return true;
        }
        if (id >= kInvertParamBaseId && id < kInvertParamBaseId + kChannelCount) {
            *value = p->params.invert[id - kInvertParamBaseId] ? 1.0 : 0.0; return true;
        }
        return false;
    }
}

bool isToggleParam(clap_id id)
{
    return id == kBypassParamId || id == kHpfBypassParamId
        || id == kDelayBypassParamId || id == kTrimBypassParamId
        || (id >= kMuteParamBaseId && id < kMuteParamBaseId + kChannelCount)
        || (id >= kInvertParamBaseId && id < kInvertParamBaseId + kChannelCount);
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kActiveParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kPolesParamId) std::snprintf(display, size, "%.0fP", value);
    else if (id == kCutoffParamId) std::snprintf(display, size, "%.0f Hz", value);
    else if (id >= kDelayParamBaseId && id < kDelayParamBaseId + kChannelCount)
        std::snprintf(display, size, "%.3f ms", value);
    else if (id == kOutputParamId
        || (id >= kGainParamBaseId && id < kGainParamBaseId + kChannelCount))
        std::snprintf(display, size, "%+.1f dB", value);
    else if (isToggleParam(id))
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    else std::snprintf(display, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display,
    double* value)
{
    if (!display || !value) return false;
    if (std::strcmp(display, "ON") == 0 || std::strcmp(display, "on") == 0) {
        *value = 1.0; return true;
    }
    if (std::strcmp(display, "OFF") == 0 || std::strcmp(display, "off") == 0) {
        *value = 0.0; return true;
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in,
    const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state { kStateVersion, self(plugin)->params };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readExact(stream, &state, sizeof(state)) || state.version != kStateVersion)
        return false;
    auto* p = self(plugin);
    p->params = state.params;
    p->calibrate.setParams(p->params);
    p->params = p->calibrate.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

#if defined(__APPLE__)
constexpr auto kArrayLayout = s3g::gui_layout::arrayFamilyLayoutForRows(
    s3g::gui_layout::arrayRowsPerPageForChannels(kChannelCount));
constexpr uint32_t kGuiWidth = static_cast<uint32_t>(kArrayLayout.canvas.width);
constexpr uint32_t kGuiHeight = static_cast<uint32_t>(kArrayLayout.canvas.height);
constexpr uint32_t kRowsPerPage = kArrayLayout.rowsPerPage;
constexpr uint32_t kRowHeight =
    static_cast<uint32_t>(s3g::gui_layout::kStandardMetrics.rowPitch);
constexpr CGFloat kFilterCutoffX = 34.0;
constexpr CGFloat kFilterCutoffWidth = 318.0;
constexpr CGFloat kFilterPolesX = 366.0;
constexpr CGFloat kFilterPolesWidth = 320.0;
constexpr CGFloat kFilterGraphHeight = 362.0;

} // namespace

@interface S3G_ARRAY_CALIBRATE_TEXT_CLASS : NSTextField
@end

@implementation S3G_ARRAY_CALIBRATE_TEXT_CLASS
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

@interface S3G_ARRAY_CALIBRATE_VIEW_CLASS : NSView {
@private
    Plugin* _plugin;
    int _dragControl;
    int _dragChannel;
    uint32_t _tab;
    uint32_t _page;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    NSTimer* _timer;
    NSMutableArray<NSTextField*>* _fields;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

static double arrayCalibrateClamp(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

static double arrayCalibrateFreqNorm(double hz)
{
    return arrayCalibrateClamp((std::log(hz) - std::log(20.0))
        / (std::log(20000.0) - std::log(20.0)), 0.0, 1.0);
}

static double arrayCalibrateNormFreq(double n)
{
    return std::exp(std::log(20.0) + arrayCalibrateClamp(n, 0.0, 1.0)
        * (std::log(20000.0) - std::log(20.0)));
}

static double arrayCalibrateMagnitudeDb(double hz, double cutoffHz, uint32_t poles)
{
    const double ratio = std::max(0.000001, hz / std::max(1.0, cutoffHz));
    const double onePole = ratio / std::sqrt(1.0 + ratio * ratio);
    return 20.0 * std::log10(std::max(0.000001,
        std::pow(onePole, std::max<uint32_t>(1u, poles))));
}

static double arrayCalibrateDbNorm(double db)
{
    return 1.0 - arrayCalibrateClamp((db + 48.0) / 54.0, 0.0, 1.0);
}

@implementation S3G_ARRAY_CALIBRATE_VIEW_CLASS

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragControl = -1;
        _dragChannel = -1;
        _tab = 0u;
        _page = 0u;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _timer = nil;
        _fields = [[NSMutableArray alloc] initWithCapacity:kRowsPerPage];
        [self setWantsLayer:YES];
        for (uint32_t row = 0u; row < kRowsPerPage; ++row) {
            NSTextField* field = [[S3G_ARRAY_CALIBRATE_TEXT_CLASS alloc]
                initWithFrame:NSZeroRect];
            s3g::clap_gui::styleNumberTextField(field);
            [field setTarget:self];
            [field setAction:@selector(valueFieldChanged:)];
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
- (void)startRefreshTimer
{
    if (!_timer) _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 24.0
        target:self selector:@selector(refreshMeter:) userInfo:nil repeats:YES];
}
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

- (BOOL)isEditingField:(NSTextField*)field
{
    NSResponder* first = [[self window] firstResponder];
    return first == field || first == [field currentEditor];
}

- (NSString*)textForParam:(clap_id)param value:(double)value
{
    char buf[64] {};
    paramsValueToText(&_plugin->plugin, param, value, buf, sizeof(buf));
    return [NSString stringWithUTF8String:buf];
}

- (uint32_t)pageCount
{
    return std::max<uint32_t>(1u,
        (_plugin->params.activeChannels + kRowsPerPage - 1u) / kRowsPerPage);
}

- (void)clampPage { _page = std::min<uint32_t>(_page, [self pageCount] - 1u); }

- (void)layoutValueFields
{
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    for (uint32_t row = 0u; row < kRowsPerPage; ++row) {
        NSTextField* field = [_fields objectAtIndex:row];
        const uint32_t ch = pageStart + row;
        const BOOL visible = _tab != 0u && ch < _plugin->params.activeChannels;
        const CGFloat y = kArrayLayout.channelPlot.y
            + static_cast<CGFloat>(row) * static_cast<CGFloat>(kRowHeight);
        [field setTag:static_cast<NSInteger>(ch)];
        [field setFrame:NSMakeRect(kArrayLayout.channelValueColumn.x,
            y - 4.0, kArrayLayout.channelValueColumn.width, 22.0)];
        [field setHidden:!visible];
        if (visible && ![self isEditingField:field]) {
            if (_tab == 1u)
                [field setStringValue:[NSString stringWithFormat:@"%.3f",
                    _plugin->params.delayMs[ch]]];
            else
                [field setStringValue:[NSString stringWithFormat:@"%.2f",
                    _plugin->params.gainDb[ch]]];
        }
    }
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
    [self valueFieldChanged:field];
}

- (void)valueFieldChanged:(id)sender
{
    NSTextField* field = (NSTextField*)sender;
    const uint32_t ch = static_cast<uint32_t>(std::clamp<long>(
        static_cast<long>([field tag]), 0L, static_cast<long>(kChannelCount - 1u)));
    if (_tab == 1u) {
        const double value = std::clamp([[field stringValue] doubleValue],
            0.0, static_cast<double>(kFixedMaxDelayMs));
        applyParam(*_plugin, kDelayParamBaseId + ch, value);
        [field setStringValue:[NSString stringWithFormat:@"%.3f",
            _plugin->params.delayMs[ch]]];
    } else if (_tab == 2u) {
        const double value = std::clamp([[field stringValue] doubleValue], -60.0, 18.0);
        applyParam(*_plugin, kGainParamBaseId + ch, value);
        [field setStringValue:[NSString stringWithFormat:@"%.2f",
            _plugin->params.gainDb[ch]]];
    }
    [self setNeedsDisplay:YES];
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView
    doCommandBySelector:(SEL)commandSelector
{
    (void)textView;
    if (commandSelector == @selector(insertNewline:)
        || commandSelector == @selector(insertTab:)) {
        [self valueFieldChanged:control];
        [[self window] makeFirstResponder:self];
        return YES;
    }
    return NO;
}

- (void)drawHeaderButton:(NSString*)label rect:(NSRect)rect active:(bool)active
    enabled:(bool)enabled attrs:(NSDictionary*)attrs
{
    const uint32_t fill = !enabled ? 0x242424 : (active ? 0xb8b8b8 : 0x3b3b3b);
    const uint32_t text = !enabled ? 0x686868 : (active ? 0x151515 : 0xc0c0c0);
    [s3g::clap_gui::color(fill) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x5c5c5c) setStroke];
    NSFrameRect(rect);
    NSDictionary* textAttrs = @{
        NSForegroundColorAttributeName:s3g::clap_gui::color(text),
        NSFontAttributeName:[attrs objectForKey:NSFontAttributeName]
    };
    const NSSize size = [label sizeWithAttributes:textAttrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0)
        withAttributes:textAttrs];
}

- (void)drawToggleButton:(NSString*)label rect:(NSRect)rect active:(bool)active
    attrs:(NSDictionary*)attrs
{
    [self drawHeaderButton:label rect:rect active:active enabled:true attrs:attrs];
}

- (bool)selectedStageBypassed
{
    if (_tab == 0u) return _plugin->params.hpfBypass;
    if (_tab == 1u) return _plugin->params.delayBypass;
    return _plugin->params.trimBypass;
}

- (clap_id)selectedStageBypassParam
{
    if (_tab == 0u) return kHpfBypassParamId;
    if (_tab == 1u) return kDelayBypassParamId;
    return kTrimBypassParamId;
}

- (void)drawEditorHeader:(NSDictionary*)attrs
{
    const NSRect panel = s3g::clap_gui::cocoaRect(kArrayLayout.editor.frame);
    const CGFloat y = panel.origin.y + 3.0;
    [self drawHeaderButton:@"HPF" rect:NSMakeRect(panel.origin.x + 112.0, y, 52.0, 17.0)
        active:_tab == 0u enabled:true attrs:attrs];
    [self drawHeaderButton:@"DELAY" rect:NSMakeRect(panel.origin.x + 170.0, y, 60.0, 17.0)
        active:_tab == 1u enabled:true attrs:attrs];
    [self drawHeaderButton:@"TRIM" rect:NSMakeRect(panel.origin.x + 236.0, y, 52.0, 17.0)
        active:_tab == 2u enabled:true attrs:attrs];
    const bool stageOn = ![self selectedStageBypassed];
    NSString* stageLabel = stageOn ? @"STAGE ON" : @"STAGE OFF";
    [self drawHeaderButton:stageLabel
        rect:NSMakeRect(panel.origin.x + 330.0, y, 82.0, 17.0)
        active:stageOn enabled:!_plugin->params.bypass attrs:attrs];
    if (_tab != 0u && [self pageCount] > 1u) {
        [self clampPage];
        [self drawHeaderButton:@"<" rect:NSMakeRect(NSMaxX(panel) - 94.0, y, 26.0, 17.0)
            active:false enabled:_page > 0u attrs:attrs];
        [self drawHeaderButton:@">" rect:NSMakeRect(NSMaxX(panel) - 32.0, y, 26.0, 17.0)
            active:false enabled:_page + 1u < [self pageCount] attrs:attrs];
        [[NSString stringWithFormat:@"%u/%u", _page + 1u, [self pageCount]]
            drawAtPoint:NSMakePoint(NSMaxX(panel) - 61.0, y + 1.0)
            withAttributes:attrs];
    }
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu != 1) return;
    static NSString* items[] = { @"1P", @"2P", @"3P", @"4P" };
    const CGFloat itemH = 18.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(kFilterPolesWidth), itemH * 4.0);
    const int selected = static_cast<int>(_plugin->params.poles - 1u);
    s3g::clap_gui::drawDropdownMenu(menuRect, itemH, items, 4u,
        selected, _hoverMenuItem, attrs, style);
}

- (void)drawFilterGraph:(NSRect)rect attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
        rect.size.width, rect.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"RESPONSE", true, rect.origin.x, rect.origin.y,
        rect.size.width, 21.0, attrs, style);
    const NSRect graph = NSMakeRect(rect.origin.x + 12.0, rect.origin.y + 30.0,
        rect.size.width - 24.0, rect.size.height - 42.0);
    [s3g::clap_gui::color(0x181818) setFill];
    NSRectFill(graph);
    [s3g::clap_gui::color(0x474747) setStroke];
    NSFrameRect(graph);
    [s3g::clap_gui::color(0x2e2e2e, 0.75) setStroke];
    for (int i = 1; i < 4; ++i) {
        const CGFloat y = graph.origin.y + graph.size.height * i / 4.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(graph.origin.x, y)
            toPoint:NSMakePoint(NSMaxX(graph), y)];
    }
    NSBezierPath* response = [NSBezierPath bezierPath];
    [response setLineWidth:2.0];
    for (int i = 0; i <= 160; ++i) {
        const double n = static_cast<double>(i) / 160.0;
        double db = arrayCalibrateMagnitudeDb(arrayCalibrateNormFreq(n),
            _plugin->params.cutoffHz, _plugin->params.poles);
        if (_plugin->params.hpfBypass || _plugin->params.bypass) db = 0.0;
        const CGFloat x = graph.origin.x + graph.size.width * n;
        const CGFloat y = graph.origin.y + graph.size.height * arrayCalibrateDbNorm(db);
        if (i == 0) [response moveToPoint:NSMakePoint(x, y)];
        else [response lineToPoint:NSMakePoint(x, y)];
    }
    [s3g::clap_gui::color((_plugin->params.hpfBypass || _plugin->params.bypass)
        ? 0x777777 : 0xc8c8c8) setStroke];
    [response stroke];
    const CGFloat cutoffX = graph.origin.x + graph.size.width
        * arrayCalibrateFreqNorm(_plugin->params.cutoffHz);
    [s3g::clap_gui::color(0xadadad, 0.75) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cutoffX, graph.origin.y)
        toPoint:NSMakePoint(cutoffX, NSMaxY(graph))];
    [[NSString stringWithFormat:@"%.0f Hz", _plugin->params.cutoffHz]
        drawAtPoint:NSMakePoint(cutoffX + 6.0, graph.origin.y + 10.0)
        withAttributes:attrs];
    [[NSString stringWithFormat:@"%u dB/oct", _plugin->params.poles * 6u]
        drawAtPoint:NSMakePoint(graph.origin.x + 6.0, NSMaxY(graph) - 18.0)
        withAttributes:attrs];
}

- (void)drawChannelRows:(NSDictionary*)attrs dim:(NSDictionary*)dim
    style:(const s3g::clap_gui::Style&)style
{
    (void)style;
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    const uint32_t n = std::min<uint32_t>(kRowsPerPage,
        _plugin->params.activeChannels - pageStart);
    const NSRect plot = s3g::clap_gui::cocoaRect(kArrayLayout.channelPlot);
    const bool delay = _tab == 1u;
    const CGFloat zeroX = plot.origin.x + plot.size.width * (60.0 / 78.0);
    if (delay) {
        [@"0 ms" drawAtPoint:NSMakePoint(plot.origin.x, 154.0) withAttributes:dim];
        [@"4000 ms" drawAtPoint:NSMakePoint(NSMaxX(plot) - 52.0, 154.0)
            withAttributes:dim];
    } else {
        [@"-60" drawAtPoint:NSMakePoint(plot.origin.x, 154.0) withAttributes:dim];
        [@"0" drawAtPoint:NSMakePoint(zeroX - 4.0, 154.0) withAttributes:dim];
        [@"+18 dB" drawAtPoint:NSMakePoint(NSMaxX(plot) - 42.0, 154.0)
            withAttributes:dim];
        [s3g::clap_gui::color(0x5b5b5b) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(zeroX, plot.origin.y)
            toPoint:NSMakePoint(zeroX, NSMaxY(plot))];
    }
    for (uint32_t row = 0u; row < n; ++row) {
        const uint32_t ch = pageStart + row;
        const CGFloat y = plot.origin.y + static_cast<CGFloat>(row) * kRowHeight;
        const CGFloat norm = delay
            ? std::clamp<CGFloat>(_plugin->params.delayMs[ch] / kFixedMaxDelayMs, 0.0, 1.0)
            : std::clamp<CGFloat>((_plugin->params.gainDb[ch] + 60.0) / 78.0, 0.0, 1.0);
        const CGFloat x = plot.origin.x + plot.size.width * norm;
        [[NSString stringWithFormat:@"%02u", ch + 1u]
            drawAtPoint:NSMakePoint(34.0, y - 1.0) withAttributes:dim];
        [s3g::clap_gui::color(0x202020) setFill];
        NSRectFill(NSMakeRect(plot.origin.x, y, plot.size.width, 14.0));
        [s3g::clap_gui::color(0x555555) setStroke];
        NSFrameRect(NSMakeRect(plot.origin.x, y, plot.size.width, 14.0));
        NSRect bar = delay
            ? NSMakeRect(plot.origin.x, y, std::max<CGFloat>(1.0, x - plot.origin.x), 14.0)
            : NSMakeRect(std::min(x, zeroX), y,
                std::max<CGFloat>(1.0, std::fabs(x - zeroX)), 14.0);
        const bool muted = !delay && _plugin->params.mute[ch] != 0u;
        const bool inverted = !delay && _plugin->params.invert[ch] != 0u;
        [s3g::clap_gui::color(muted ? 0x4a4a4a
            : (inverted ? 0x9c9c9c : 0xa0a0a0)) setFill];
        NSRectFill(bar);
        if (!delay) {
            [self drawToggleButton:@"M" rect:NSMakeRect(
                kArrayLayout.channelMuteColumn.x, y - 4.0,
                kArrayLayout.channelMuteColumn.width, 20.0)
                active:muted attrs:attrs];
            [self drawToggleButton:@"INV" rect:NSMakeRect(
                kArrayLayout.channelInvertColumn.x, y - 4.0,
                kArrayLayout.channelInvertColumn.width, 20.0)
                active:inverted attrs:attrs];
        }
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style {};
    NSFont* font = [NSFont fontWithName:@"Menlo" size:10.0]
        ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* attrs = @{
        NSForegroundColorAttributeName:style.text,
        NSFontAttributeName:font
    };
    NSDictionary* dim = @{
        NSForegroundColorAttributeName:style.dim,
        NSFontAttributeName:font
    };
    const auto titleBand = s3g::gui_layout::arrayTitleBand(kArrayLayout.canvas);
    [style.bg setFill];
    NSRectFill(self.bounds);
    s3g::clap_gui::drawArrayTitleBand(
        [NSString stringWithFormat:@"s3g ARRAY CALIBRATE %uCH", kChannelCount],
        [NSString stringWithUTF8String:_plugin->presetName],
        s3g::clap_gui::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);
    const auto drawPanel = [&](NSString* title, const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel, style);
        s3g::clap_gui::drawPanelHeader(title, true, panel, attrs, style);
    };
    drawPanel(@"OUTPUT", kArrayLayout.output);
    drawPanel(@"ARRAY", kArrayLayout.array);
    drawPanel(@"CALIBRATION", kArrayLayout.editor);
    s3g::clap_gui::drawProcessorSlider(@"OUT",
        [self textForParam:kOutputParamId value:_plugin->params.outputGainDb],
        (_plugin->params.outputGainDb + 60.0) / 78.0,
        s3g::gui_layout::rowY(kArrayLayout.output, 0u),
        kArrayLayout.output.frame.x, kArrayLayout.output.frame.width,
        attrs, dim, style);
    s3g::clap_gui::drawToggle(@"BYPASS", _plugin->params.bypass,
        s3g::gui_layout::rowY(kArrayLayout.output, 1u), attrs, dim, style,
        s3g::gui_layout::processorLabelX(kArrayLayout.output.frame.x),
        s3g::gui_layout::processorControlX(kArrayLayout.output.frame.x), 74.0);
    s3g::clap_gui::drawProcessorSlider(@"ACTIVE",
        [self textForParam:kActiveParamId value:_plugin->params.activeChannels],
        (_plugin->params.activeChannels - 1.0)
            / std::max(1.0, static_cast<double>(kChannelCount - 1u)),
        s3g::gui_layout::rowY(kArrayLayout.array, 0u),
        kArrayLayout.array.frame.x, kArrayLayout.array.frame.width,
        attrs, dim, style);
    [self drawEditorHeader:attrs];
    if (_tab == 0u) {
        [self drawFilterGraph:NSMakeRect(
                34.0, 204.0, 652.0, kFilterGraphHeight)
            attrs:attrs style:style];
        s3g::clap_gui::drawProcessorSlider(@"CUTOFF",
            [self textForParam:kCutoffParamId value:_plugin->params.cutoffHz],
            (_plugin->params.cutoffHz - 20.0) / 220.0,
            s3g::gui_layout::rowY(kArrayLayout.editor, 0u),
            kFilterCutoffX, kFilterCutoffWidth, attrs, dim, style);
        s3g::clap_gui::drawProcessorMenu(@"POLES",
            [self textForParam:kPolesParamId value:_plugin->params.poles],
            s3g::gui_layout::rowY(kArrayLayout.editor, 0u),
            kFilterPolesX, kFilterPolesWidth, attrs, dim, style);
        [self drawOpenMenu:attrs style:style];
    } else {
        [self drawChannelRows:attrs dim:dim style:style];
    }
    [self layoutValueFields];
}

- (void)updateRowAtPoint:(NSPoint)pt
{
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    const uint32_t n = std::min<uint32_t>(kRowsPerPage,
        _plugin->params.activeChannels - pageStart);
    const int row = static_cast<int>(std::floor(
        (pt.y - kArrayLayout.channelPlot.y) / static_cast<CGFloat>(kRowHeight)));
    if (row < 0 || row >= static_cast<int>(n)) return;
    const uint32_t ch = pageStart + static_cast<uint32_t>(row);
    _dragChannel = static_cast<int>(ch);
    const double nrm = std::clamp(
        (static_cast<double>(pt.x) - kArrayLayout.channelPlot.x)
            / kArrayLayout.channelPlot.width, 0.0, 1.0);
    if (_tab == 1u) applyParam(*_plugin, kDelayParamBaseId + ch,
        nrm * kFixedMaxDelayMs);
    else if (_tab == 2u) applyParam(*_plugin, kGainParamBaseId + ch,
        -60.0 + nrm * 78.0);
}

- (void)updateDrag:(NSPoint)pt
{
    if (!_plugin || _dragControl < 0) return;
    if (_dragControl == 0) {
        const double n = std::clamp((static_cast<double>(pt.x)
            - s3g::gui_layout::processorControlX(kArrayLayout.output.frame.x))
            / s3g::gui_layout::processorTrackWidth(kArrayLayout.output.frame.width),
            0.0, 1.0);
        applyParam(*_plugin, kOutputParamId, -60.0 + n * 78.0);
    } else if (_dragControl == 1) {
        const double n = std::clamp((static_cast<double>(pt.x)
            - s3g::gui_layout::processorControlX(kArrayLayout.array.frame.x))
            / s3g::gui_layout::processorTrackWidth(kArrayLayout.array.frame.width),
            0.0, 1.0);
        applyParam(*_plugin, kActiveParamId,
            1.0 + n * static_cast<double>(kChannelCount - 1u));
    } else if (_dragControl == 2) {
        const double n = std::clamp((static_cast<double>(pt.x)
            - s3g::gui_layout::processorControlX(kFilterCutoffX))
            / s3g::gui_layout::processorTrackWidth(kFilterCutoffWidth), 0.0, 1.0);
        applyParam(*_plugin, kCutoffParamId, 20.0 + n * 220.0);
    } else if (_dragControl == 3) {
        [self updateRowAtPoint:pt];
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const auto titleBand = s3g::gui_layout::arrayTitleBand(kArrayLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(pt, &_plugin->plugin,
            @"Array Calibrate", titleBand, _plugin->presetName,
            sizeof(_plugin->presetName), kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu == 1) {
        const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(kFilterPolesWidth), 72.0);
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuRect, 18.0, 4u);
        _openMenu = 0;
        _hoverMenuItem = -1;
        if (hit >= 0) {
            applyParam(*_plugin, kPolesParamId, hit + 1.0);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const NSRect panel = s3g::clap_gui::cocoaRect(kArrayLayout.editor.frame);
    const CGFloat headerY = panel.origin.y + 3.0;
    const NSRect tabRects[] {
        NSMakeRect(panel.origin.x + 112.0, headerY, 52.0, 17.0),
        NSMakeRect(panel.origin.x + 170.0, headerY, 60.0, 17.0),
        NSMakeRect(panel.origin.x + 236.0, headerY, 52.0, 17.0),
    };
    for (uint32_t i = 0u; i < 3u; ++i) {
        if (NSPointInRect(pt, NSInsetRect(tabRects[i], 0.0, -3.0))) {
            _tab = i;
            _page = 0u;
            [[self window] makeFirstResponder:self];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const NSRect stageRect = NSMakeRect(panel.origin.x + 330.0,
        headerY, 82.0, 17.0);
    if (NSPointInRect(pt, NSInsetRect(stageRect, 0.0, -3.0))
        && !_plugin->params.bypass) {
        applyParam(*_plugin, [self selectedStageBypassParam],
            [self selectedStageBypassed] ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_tab != 0u && [self pageCount] > 1u) {
        if (NSPointInRect(pt, NSMakeRect(NSMaxX(panel) - 94.0,
                headerY, 26.0, 17.0))) {
            if (_page > 0u) --_page;
            [self setNeedsDisplay:YES]; return;
        }
        if (NSPointInRect(pt, NSMakeRect(NSMaxX(panel) - 32.0,
                headerY, 26.0, 17.0))) {
            if (_page + 1u < [self pageCount]) ++_page;
            [self setNeedsDisplay:YES]; return;
        }
    }
    const NSRect bypassBox = NSMakeRect(
        s3g::gui_layout::processorControlX(kArrayLayout.output.frame.x),
        s3g::gui_layout::rowY(kArrayLayout.output, 1u) - 1.0, 74.0, 15.0);
    if (NSPointInRect(pt, NSInsetRect(bypassBox, 0.0, -4.0))) {
        applyParam(*_plugin, kBypassParamId, _plugin->params.bypass ? 0.0 : 1.0);
        [self setNeedsDisplay:YES]; return;
    }
    if (_tab == 0u) {
        const NSRect polesBox = NSMakeRect(
            s3g::gui_layout::processorControlX(kFilterPolesX),
            s3g::gui_layout::rowY(kArrayLayout.editor, 0u) - 1.0,
            s3g::gui_layout::processorMenuWidth(kFilterPolesWidth), 15.0);
        if (NSPointInRect(pt, NSInsetRect(polesBox, 0.0, -4.0))) {
            _openMenu = 1;
            _hoverMenuItem = -1;
            _menuOrigin = NSMakePoint(polesBox.origin.x, NSMaxY(polesBox) + 2.0);
            [self setNeedsDisplay:YES]; return;
        }
    }
    struct SliderHit { NSRect rect; int control; clap_id param; };
    const SliderHit hits[] {
        { s3g::clap_gui::cocoaRect(s3g::gui_layout::sliderHitRect(
            kArrayLayout.output, 0u)), 0, kOutputParamId },
        { s3g::clap_gui::cocoaRect(s3g::gui_layout::sliderHitRect(
            kArrayLayout.array, 0u)), 1, kActiveParamId },
        { NSMakeRect(kFilterCutoffX + 8.0,
            s3g::gui_layout::rowY(kArrayLayout.editor, 0u) - 8.0,
            kFilterCutoffWidth - 16.0, 24.0), 2, kCutoffParamId },
    };
    const uint32_t hitCount = _tab == 0u ? 3u : 2u;
    for (uint32_t i = 0u; i < hitCount; ++i) {
        if (NSPointInRect(pt, hits[i].rect)) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &_plugin->plugin, hits[i].param, &defaultValue)) {
                applyParam(*_plugin, hits[i].param, defaultValue);
                _dragControl = -1;
            } else {
                _dragControl = hits[i].control;
                [self updateDrag:pt];
            }
            [self setNeedsDisplay:YES]; return;
        }
    }
    if (_tab == 0u) return;
    [self clampPage];
    const uint32_t pageStart = _page * kRowsPerPage;
    const uint32_t n = std::min<uint32_t>(kRowsPerPage,
        _plugin->params.activeChannels - pageStart);
    for (uint32_t row = 0u; row < n; ++row) {
        const uint32_t ch = pageStart + row;
        const CGFloat y = kArrayLayout.channelPlot.y + row * kRowHeight;
        if (_tab == 2u && NSPointInRect(pt, NSMakeRect(
                kArrayLayout.channelMuteColumn.x, y - 7.0,
                kArrayLayout.channelMuteColumn.width, 24.0))) {
            applyParam(*_plugin, kMuteParamBaseId + ch,
                _plugin->params.mute[ch] ? 0.0 : 1.0);
            [self setNeedsDisplay:YES]; return;
        }
        if (_tab == 2u && NSPointInRect(pt, NSMakeRect(
                kArrayLayout.channelInvertColumn.x, y - 7.0,
                kArrayLayout.channelInvertColumn.width, 24.0))) {
            applyParam(*_plugin, kInvertParamBaseId + ch,
                _plugin->params.invert[ch] ? 0.0 : 1.0);
            [self setNeedsDisplay:YES]; return;
        }
        if (NSPointInRect(pt, NSMakeRect(kArrayLayout.channelPlot.x,
                y - 7.0, kArrayLayout.channelPlot.width, 24.0))) {
            const clap_id param = (_tab == 1u ? kDelayParamBaseId : kGainParamBaseId) + ch;
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &_plugin->plugin, param, &defaultValue)) {
                applyParam(*_plugin, param, defaultValue);
                _dragControl = -1;
                _dragChannel = -1;
            } else {
                _dragControl = 3;
                _dragChannel = static_cast<int>(ch);
                [self updateDrag:pt];
            }
            [self setNeedsDisplay:YES]; return;
        }
    }
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu != 1) return;
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(kFilterPolesWidth), 72.0);
    _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(pt, menuRect, 18.0, 4u);
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    [self updateDrag:[self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragControl = -1;
    _dragChannel = -1;
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
    if (p->guiView) return true;
    p->guiView = [[S3G_ARRAY_CALIBRATE_VIEW_CLASS alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p && p->guiView) {
        p->guiVisible.store(false, std::memory_order_relaxed);
        [static_cast<S3G_ARRAY_CALIBRATE_VIEW_CLASS*>(p->guiView) stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h);
}
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, w, h);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win)
{
    if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa)
        return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(win->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3G_ARRAY_CALIBRATE_VIEW_CLASS*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3G_ARRAY_CALIBRATE_VIEW_CLASS*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};
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

constexpr const char* features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const char* pluginId()
{
    switch (kChannelCount) {
    case 16: return "org.s3g.s3g-dsp.array-calibrate-16";
    case 26: return "org.s3g.s3g-dsp.array-calibrate-26";
    case 32: return "org.s3g.s3g-dsp.array-calibrate-32";
    default: return "org.s3g.s3g-dsp.array-calibrate-64";
    }
}

const char* pluginName()
{
    switch (kChannelCount) {
    case 16: return "s3g Array Calibrate 16";
    case 26: return "s3g Array Calibrate 26";
    case 32: return "s3g Array Calibrate 32";
    default: return "s3g Array Calibrate 64";
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
    "Fixed-width speaker calibration combining HPF, per-channel delay, and trim.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params.activeChannels = kChannelCount;
    p->calibrate.setParams(p->params);
    p->params = p->calibrate.params();
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* id)
{
    return std::strcmp(id, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
