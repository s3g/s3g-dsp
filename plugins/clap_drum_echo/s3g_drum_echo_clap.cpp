#include "s3g_drum_echo.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#if defined(__APPLE__)
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
#include <iterator>
#include <new>

namespace {

constexpr uint32_t kChannelCount = 2u;
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kGuiWidth = 760u;
constexpr uint32_t kGuiHeight = 376u;

enum ParamId : clap_id {
    kHeadModeParamId = 1u,
    kClockParamId,
    kTimeParamId,
    kFeedbackParamId,
    kWearParamId,
    kFlutterParamId,
    kTransientParamId,
    kSensitivityParamId,
    kDuckParamId,
    kToneParamId,
    kSpreadParamId,
    kMixParamId,
    kOutputParamId,
    kBypassParamId,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* label;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    const char* unit;
    bool stepped;
};

constexpr ParamDef kParamDefs[] {
    { kHeadModeParamId, "Head Pattern", "HEADS", "Echo", 0.0, 6.0, 6.0, "heads", true },
    { kClockParamId, "Clock", "CLOCK", "Echo", 0.0, 9.0, 5.0, "clock", true },
    { kTimeParamId, "Free Time", "TIME", "Echo", 20.0, 1800.0, 180.0, "ms", false },
    { kFeedbackParamId, "Feedback", "FDBK", "Echo", 0.0, 0.92, 0.38, "pct", false },
    { kWearParamId, "Tape Wear", "WEAR", "Echo", 0.0, 1.0, 0.20, "pct", false },
    { kFlutterParamId, "Flutter", "FLUT", "Echo", 0.0, 1.0, 0.12, "pct", false },
    { kTransientParamId, "Transient Send", "HIT SEND", "Drum Response", -1.0, 1.0, 0.35, "signedpct", false },
    { kSensitivityParamId, "Sensitivity", "SENSE", "Drum Response", 0.0, 1.0, 0.55, "pct", false },
    { kDuckParamId, "Hit Duck", "DUCK", "Drum Response", 0.0, 1.0, 0.45, "pct", false },
    { kToneParamId, "Echo Tone", "TONE", "Drum Response", -1.0, 1.0, 0.0, "signedpct", false },
    { kSpreadParamId, "Head Spread", "SPREAD", "Drum Response", 0.0, 1.0, 0.55, "pct", false },
    { kMixParamId, "Mix", "MIX", "Output", 0.0, 1.0, 0.35, "pct", false },
    { kOutputParamId, "Output", "OUT", "Output", -36.0, 12.0, -3.0, "db", false },
    { kBypassParamId, "Bypass", "BYP", "Output", 0.0, 1.0, 0.0, "bool", true },
};

constexpr uint32_t kParamCount = static_cast<uint32_t>(std::size(kParamDefs));

const ParamDef* findParam(clap_id id)
{
    for (const auto& definition : kParamDefs) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::DrumEchoParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::DrumEchoParams params {};
    s3g::DrumEcho dsp {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> transientActivity { 0.0f };
    std::atomic<float> duckGain { 1.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kHeadModeParamId:
        plugin.params.headMode = static_cast<s3g::DrumEchoHeadMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kDrumEchoHeadModeCount - 1u));
        break;
    case kClockParamId:
        plugin.params.clock = static_cast<s3g::DrumEchoClock>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kDrumEchoClockCount - 1u));
        break;
    case kTimeParamId: plugin.params.timeMs = static_cast<float>(value); break;
    case kFeedbackParamId: plugin.params.feedback = static_cast<float>(value); break;
    case kWearParamId: plugin.params.wear = static_cast<float>(value); break;
    case kFlutterParamId: plugin.params.flutter = static_cast<float>(value); break;
    case kTransientParamId: plugin.params.transient = static_cast<float>(value); break;
    case kSensitivityParamId: plugin.params.sensitivity = static_cast<float>(value); break;
    case kDuckParamId: plugin.params.duck = static_cast<float>(value); break;
    case kToneParamId: plugin.params.tone = static_cast<float>(value); break;
    case kSpreadParamId: plugin.params.spread = static_cast<float>(value); break;
    case kMixParamId: plugin.params.mix = static_cast<float>(value); break;
    case kOutputParamId: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kBypassParamId: plugin.params.bypass = value >= 0.5; break;
    default: return;
    }
    plugin.dsp.setParams(plugin.params);
    plugin.params = plugin.dsp.params();
}

bool init(const clap_plugin_t*) { return true; }

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    instance->dsp.setParams(instance->params);
    instance->dsp.prepare(sampleRate);
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    instance->transientActivity.store(0.0f, std::memory_order_relaxed);
    instance->duckGain.store(1.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->dsp.reset();
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    instance->transientActivity.store(0.0f, std::memory_order_relaxed);
    instance->duckGain.store(1.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* parameter =
                reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, parameter->param_id, parameter->value);
        }
    }
}

float readInputSample(const clap_audio_buffer_t& input,
    uint32_t channel, uint32_t frame)
{
    if (channel >= input.channel_count) return 0.0f;
    if (input.data32 && input.data32[channel]) {
        return input.data32[channel][frame];
    }
    if (input.data64 && input.data64[channel]) {
        return static_cast<float>(input.data64[channel][frame]);
    }
    return 0.0f;
}

void writeOutputSample(const clap_audio_buffer_t& output,
    uint32_t channel, uint32_t frame, float sample)
{
    if (channel >= output.channel_count) return;
    if (output.data32 && output.data32[channel]) {
        output.data32[channel][frame] = sample;
    }
    if (output.data64 && output.data64[channel]) {
        output.data64[channel][frame] = static_cast<double>(sample);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processContext)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, processContext->in_events);
    const bool tempoValid = processContext->transport
        && (processContext->transport->flags
            & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(processContext->transport->tempo)
        && processContext->transport->tempo > 0.0;
    instance->dsp.setTempo(tempoValid
        ? processContext->transport->tempo : 120.0, tempoValid);
    if (processContext->audio_inputs_count == 0u
        || processContext->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = processContext->audio_inputs[0];
    const auto& output = processContext->audio_outputs[0];
    const uint32_t frames = processContext->frames_count;
    float blockPeak = 0.0f;
    float blockTransient = 0.0f;
    float blockDuckGain = 1.0f;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        float left = readInputSample(input, 0u, frame);
        float right = readInputSample(input, 1u, frame);
        instance->dsp.processFrame(left, right);
        writeOutputSample(output, 0u, frame, left);
        writeOutputSample(output, 1u, frame, right);
        blockPeak = std::max(blockPeak,
            std::max(std::abs(left), std::abs(right)));
        blockTransient = std::max(
            blockTransient, instance->dsp.transientActivity());
        blockDuckGain = std::min(
            blockDuckGain, instance->dsp.duckGain());
    }
    s3g::clearAudioBufferFromChannel(output, kChannelCount, frames);
    instance->outputPeak.store(std::max(
        instance->outputPeak.load(std::memory_order_relaxed) * 0.90f,
        blockPeak), std::memory_order_relaxed);
    instance->transientActivity.store(std::max(
        instance->transientActivity.load(std::memory_order_relaxed) * 0.88f,
        blockTransient), std::memory_order_relaxed);
    instance->duckGain.store(blockDuckGain,
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Stereo In" : "Stereo Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const auto& definition = kParamDefs[index];
    info->id = definition.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (definition.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::snprintf(info->name, sizeof(info->name), "%s", definition.name);
    std::snprintf(info->module, sizeof(info->module), "%s", definition.module);
    info->min_value = definition.minimum;
    info->max_value = definition.maximum;
    info->default_value = definition.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& params = self(plugin)->params;
    switch (id) {
    case kHeadModeParamId: *value = static_cast<uint32_t>(params.headMode); return true;
    case kClockParamId: *value = static_cast<uint32_t>(params.clock); return true;
    case kTimeParamId: *value = params.timeMs; return true;
    case kFeedbackParamId: *value = params.feedback; return true;
    case kWearParamId: *value = params.wear; return true;
    case kFlutterParamId: *value = params.flutter; return true;
    case kTransientParamId: *value = params.transient; return true;
    case kSensitivityParamId: *value = params.sensitivity; return true;
    case kDuckParamId: *value = params.duck; return true;
    case kToneParamId: *value = params.tone; return true;
    case kSpreadParamId: *value = params.spread; return true;
    case kMixParamId: *value = params.mix; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kBypassParamId: *value = params.bypass ? 1.0 : 0.0; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    const auto* definition = findParam(id);
    if (!definition) return false;
    if (id == kHeadModeParamId) {
        const auto mode = static_cast<s3g::DrumEchoHeadMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kDrumEchoHeadModeCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::drumEchoHeadModeName(mode));
    } else if (id == kClockParamId) {
        const auto clock = static_cast<s3g::DrumEchoClock>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kDrumEchoClockCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::drumEchoClockName(clock));
    } else if (std::strcmp(definition->unit, "db") == 0) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (std::strcmp(definition->unit, "ms") == 0) {
        std::snprintf(display, size, "%.0f ms", value);
    } else if (std::strcmp(definition->unit, "pct") == 0) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (std::strcmp(definition->unit, "signedpct") == 0) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
    } else if (std::strcmp(definition->unit, "bool") == 0) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kHeadModeParamId) {
        for (uint32_t mode = 0u;
             mode < s3g::kDrumEchoHeadModeCount; ++mode) {
            if (std::strcmp(display, s3g::drumEchoHeadModeName(
                    static_cast<s3g::DrumEchoHeadMode>(mode))) == 0) {
                *value = static_cast<double>(mode);
                return true;
            }
        }
        return false;
    }
    if (id == kClockParamId) {
        for (uint32_t clock = 0u;
             clock < s3g::kDrumEchoClockCount; ++clock) {
            if (std::strcmp(display, s3g::drumEchoClockName(
                    static_cast<s3g::DrumEchoClock>(clock))) == 0) {
                *value = static_cast<double>(clock);
                return true;
            }
        }
        return false;
    }
    if (id == kBypassParamId) {
        if (std::strcmp(display, "ON") == 0
            || std::strcmp(display, "on") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "OFF") == 0
            || std::strcmp(display, "off") == 0) {
            *value = 0.0;
            return true;
        }
    }
    *value = std::atof(display);
    if (std::strchr(display, '%')) *value *= 0.01;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state { kStateVersion, self(plugin)->params };
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!s3g::clap_state::readAll(stream, &state, sizeof(state))
        || state.version != kStateVersion) {
        return false;
    }
    auto* instance = self(plugin);
    instance->dsp.setParams(state.params);
    instance->params = instance->dsp.params();
    instance->dsp.reset();
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    return self(plugin)->dsp.tailSamples();
}

const clap_plugin_tail_t tailExtension { tailGet };

} // namespace

#if defined(__APPLE__)
constexpr auto kOutputPanel =
    s3g::gui_layout::compactEffectOutputPanel(3u);
constexpr auto kEchoPanel = s3g::gui_layout::compactEffectLeftPanel(
    kOutputPanel, s3g::gui_layout::PanelRole::Engine, 6u);
constexpr auto kResponsePanel = s3g::gui_layout::compactEffectRightPanel(
    s3g::gui_layout::PanelRole::EventTiming, 5u);
constexpr std::array kFirstColumnPanels { kOutputPanel, kEchoPanel };
constexpr std::array kSecondColumnPanels { kResponsePanel };
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnPanels,
    s3g::gui_layout::kCompactEffectFamilyLayout.canvas));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnPanels,
    s3g::gui_layout::kCompactEffectFamilyLayout.canvas, false));

constexpr uint32_t kOutputParamIndices[] { 12u, 11u, 13u };
constexpr uint32_t kEchoParamIndices[] { 0u, 1u, 2u, 3u, 4u, 5u };
constexpr uint32_t kResponseParamIndices[] { 6u, 7u, 8u, 9u, 10u };
constexpr uint32_t kEchoSliderParamIndices[] { 2u, 3u, 4u, 5u };

NSRect processorMenuRect(const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

@interface S3GDrumEchoView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm y:(CGFloat)y
    panel:(const s3g::gui_layout::Panel&)panel
    attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
- (NSRect)dropdownRect;
- (uint32_t)openMenuItemCount;
- (void)updateMenuHover:(NSPoint)point;
@end

@implementation S3GDrumEchoView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _timer = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    NSArray* existingAreas = [[self trackingAreas] copy];
    for (NSTrackingArea* area in existingAreas) {
        [self removeTrackingArea:area];
    }
    [existingAreas release];
    const NSTrackingAreaOptions options = NSTrackingMouseMoved
        | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:area];
    [area release];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
        target:self selector:@selector(refresh:)
        userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawRow:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm y:(CGFloat)y
    panel:(const s3g::gui_layout::Panel&)panel
    attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y, panel.frame.x, panel.frame.width,
        attrs, s3g::clap_gui::softValueAttrs(), style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* plugin = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    s3g::clap_gui::drawCompactEffectTitleBand(
        @"s3g DRUM ECHO",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            plugin->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);
    const auto drawPanel = [&](NSString* title,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.frame.x, panel.frame.y,
            panel.frame.width, panel.frame.height, style);
        s3g::clap_gui::drawPanelHeader(
            title, true, panel.frame.x, panel.frame.y,
            panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labels, style);
    };
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"MULTI-HEAD TAPE", kEchoPanel);
    drawPanel(@"DRUM RESPONSE", kResponsePanel);

    const auto drawParam = [&](uint32_t parameterIndex, uint32_t row,
                               const s3g::gui_layout::Panel& panel) {
        double value = 0.0;
        const auto& definition = kParamDefs[parameterIndex];
        paramsGetValue(&plugin->plugin, definition.id, &value);
        const double span = std::max(0.000001,
            definition.maximum - definition.minimum);
        const CGFloat normalized = static_cast<CGFloat>(
            (value - definition.minimum) / span);
        char text[32] {};
        paramsValueToText(&plugin->plugin, definition.id,
            value, text, sizeof(text));
        if (definition.id == kHeadModeParamId
            || definition.id == kClockParamId) {
            s3g::clap_gui::drawProcessorMenu(
                [NSString stringWithUTF8String:definition.label],
                [NSString stringWithUTF8String:text],
                s3g::gui_layout::rowY(panel, row),
                panel.frame.x, panel.frame.width,
                labels, values, style);
        } else {
            [self drawRow:[NSString stringWithUTF8String:definition.label]
                value:[NSString stringWithUTF8String:text]
                norm:normalized y:s3g::gui_layout::rowY(panel, row)
                panel:panel attrs:labels];
        }
    };
    for (uint32_t row = 0u; row < std::size(kOutputParamIndices); ++row) {
        drawParam(kOutputParamIndices[row], row, kOutputPanel);
    }
    for (uint32_t row = 0u; row < std::size(kEchoParamIndices); ++row) {
        drawParam(kEchoParamIndices[row], row, kEchoPanel);
    }
    for (uint32_t row = 0u; row < std::size(kResponseParamIndices); ++row) {
        drawParam(kResponseParamIndices[row], row, kResponsePanel);
    }

    const float transient = plugin->transientActivity.load(
        std::memory_order_relaxed);
    const float duck = plugin->duckGain.load(
        std::memory_order_relaxed);
    [[NSString stringWithFormat:@"HIT %.0f%%  //  WET DUCK %.0f%%",
        static_cast<double>(transient * 100.0f),
        static_cast<double>((1.0f - duck) * 100.0f)]
        drawAtPoint:NSMakePoint(
            kResponsePanel.frame.x + 16.0,
            kResponsePanel.frame.y + kResponsePanel.frame.height + 12.0)
        withAttributes:values];

    if (_openMenu == static_cast<int>(kHeadModeParamId)) {
        static NSString* headItems[] = {
            @"HEAD 1", @"HEAD 2", @"HEAD 3", @"HEAD 1+2",
            @"HEAD 2+3", @"HEAD 1+3", @"ALL HEADS",
        };
        s3g::clap_gui::drawDropdownMenu(
            [self dropdownRect], 18.0,
            headItems, s3g::kDrumEchoHeadModeCount,
            static_cast<int>(plugin->params.headMode),
            _hoverMenuItem, values, style);
    } else if (_openMenu == static_cast<int>(kClockParamId)) {
        static NSString* clockItems[] = {
            @"FREE", @"1/32", @"1/16T", @"1/16", @"1/8T",
            @"1/8", @"1/4T", @"1/4", @"1/2", @"1 BAR",
        };
        s3g::clap_gui::drawDropdownMenu(
            [self dropdownRect], 18.0,
            clockItems, s3g::kDrumEchoClockCount,
            static_cast<int>(plugin->params.clock),
            _hoverMenuItem, values, style);
    }
}

- (uint32_t)openMenuItemCount
{
    if (_openMenu == static_cast<int>(kHeadModeParamId)) {
        return s3g::kDrumEchoHeadModeCount;
    }
    if (_openMenu == static_cast<int>(kClockParamId)) {
        return s3g::kDrumEchoClockCount;
    }
    return 0u;
}

- (NSRect)dropdownRect
{
    return NSMakeRect(
        _menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(kEchoPanel.frame.width),
        18.0 * static_cast<CGFloat>([self openMenuItemCount]));
}

- (void)updateMenuHover:(NSPoint)point
{
    const uint32_t count = [self openMenuItemCount];
    if (count == 0u) return;
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self dropdownRect], 18.0, count);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)updateSlider:(NSPoint)point
{
    auto* plugin = static_cast<Plugin*>(_plugin);
    const auto* definition = findParam(
        static_cast<clap_id>(_dragSlider));
    if (!definition) return;
    const bool output = definition->id == kOutputParamId
        || definition->id == kMixParamId
        || definition->id == kBypassParamId;
    const bool echo = definition->id >= kHeadModeParamId
        && definition->id <= kFlutterParamId;
    const auto& panel = output ? kOutputPanel
        : (echo ? kEchoPanel : kResponsePanel);
    const double start = s3g::gui_layout::processorControlX(panel.frame.x);
    const double width =
        s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double normalized = std::clamp(
        (point.x - start) / width, 0.0, 1.0);
    double value = definition->minimum
        + normalized * (definition->maximum - definition->minimum);
    if (definition->stepped) value = std::round(value);
    applyParam(*plugin, definition->id, value);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    auto* plugin = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            point, &plugin->plugin, @"Drum Echo",
            titleBand, _titlePresetName,
            sizeof(_titlePresetName), kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }

    if ([self openMenuItemCount] > 0u) {
        const clap_id menuId = static_cast<clap_id>(_openMenu);
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, [self dropdownRect], 18.0,
            [self openMenuItemCount]);
        if (hit >= 0) {
            applyParam(*plugin, menuId,
                static_cast<double>(hit));
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t row = 0u; row < 2u; ++row) {
        if (NSPointInRect(point, processorMenuRect(kEchoPanel, row))) {
            const NSRect box = processorMenuRect(kEchoPanel, row);
            _openMenu = static_cast<int>(row == 0u
                ? kHeadModeParamId : kClockParamId);
            _hoverMenuItem = -1;
            const CGFloat menuHeight = 18.0
                * static_cast<CGFloat>([self openMenuItemCount]);
            const CGFloat below = NSMaxY(box) + 3.0;
            const CGFloat menuY = below + menuHeight
                    <= static_cast<CGFloat>(kGuiHeight) - 8.0
                ? below : box.origin.y - menuHeight - 3.0;
            _menuOrigin = NSMakePoint(box.origin.x, menuY);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const auto beginSlider = [&](clap_id parameterId) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &plugin->plugin, parameterId, &defaultValue)) {
            applyParam(*plugin, parameterId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(parameterId);
            [self updateSlider:point];
        }
        [self setNeedsDisplay:YES];
    };
    const auto hitPanel = [&](const s3g::gui_layout::Panel& panel,
                              const uint32_t* indices, uint32_t count,
                              uint32_t firstRow) {
        for (uint32_t row = 0u; row < count; ++row) {
            if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
                    s3g::gui_layout::sliderHitRect(
                        panel, firstRow + row)))) {
                beginSlider(kParamDefs[indices[row]].id);
                return true;
            }
        }
        return false;
    };
    if (hitPanel(kOutputPanel, kOutputParamIndices,
            static_cast<uint32_t>(std::size(kOutputParamIndices)), 0u)
        || hitPanel(kEchoPanel, kEchoSliderParamIndices,
            static_cast<uint32_t>(std::size(kEchoSliderParamIndices)), 2u)
        || hitPanel(kResponsePanel, kResponseParamIndices,
            static_cast<uint32_t>(std::size(kResponseParamIndices)), 0u)) {
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    [self updateMenuHover:point];
    if (_dragSlider > 0) {
        [self updateSlider:point];
    }
}

- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:[self convertPoint:
        [event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
}

@end

#endif

namespace {

#if defined(__APPLE__)
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GDrumEchoView alloc]
        initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(
            instance->guiViewport,
            static_cast<NSView*>(instance->guiView),
            kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->guiView) {
        instance->guiVisible = false;
        [static_cast<S3GDrumEchoView*>(instance->guiView)
            stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(
            instance->guiViewport, instance->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) {
        return false;
    }
    auto* instance = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        instance->guiViewport, static_cast<NSView*>(window->cocoa),
        instance->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView
        || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) {
        return false;
    }
    instance->guiVisible = true;
    [static_cast<S3GDrumEchoView*>(instance->guiView)
        startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GDrumEchoView*>(instance->guiView)
        stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExtension;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DELAY,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.drum-echo",
    "s3g Drum Echo 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Transient-aware multi-head tape echo tuned for drums and percussion.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
