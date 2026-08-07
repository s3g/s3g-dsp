#include "s3g_drum_overload.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

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
    kCircuitParamId = 1u,
    kInputParamId,
    kOverloadParamId,
    kDensityParamId,
    kPunchParamId,
    kBiasParamId,
    kBreakupParamId,
    kWeightParamId,
    kToneParamId,
    kLinkParamId,
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
    { kCircuitParamId, "Circuit", "CIRCUIT", "Drive", 0.0, 7.0, 0.0, "circuit", true },
    { kInputParamId, "Input", "INPUT", "Drive", -18.0, 24.0, 0.0, "db", false },
    { kOverloadParamId, "Overload", "OVR", "Drive", 0.0, 1.0, 0.62, "pct", false },
    { kDensityParamId, "Density", "DENS", "Drive", 0.0, 1.0, 0.50, "pct", false },
    { kPunchParamId, "Punch", "PUNCH", "Drive", -1.0, 1.0, 0.24, "signedpct", false },
    { kBiasParamId, "Bias", "BIAS", "Color", -1.0, 1.0, 0.12, "signedpct", false },
    { kBreakupParamId, "Breakup", "BREAK", "Color", 0.0, 1.0, 0.16, "pct", false },
    { kWeightParamId, "Weight", "WEIGHT", "Color", 0.0, 1.0, 0.72, "pct", false },
    { kToneParamId, "Tone", "TONE", "Color", -1.0, 1.0, 0.0, "signedpct", false },
    { kLinkParamId, "Stereo Link", "LINK", "Color", 0.0, 1.0, 0.85, "pct", false },
    { kMixParamId, "Mix", "MIX", "Output", 0.0, 1.0, 0.82, "pct", false },
    { kOutputParamId, "Output", "OUT", "Output", -36.0, 12.0, -6.0, "db", false },
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
    s3g::DrumOverloadParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::DrumOverloadParams params {};
    s3g::DrumOverload dsp {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> overloadActivity { 0.0f };
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
    case kCircuitParamId:
        plugin.params.circuit = static_cast<s3g::DrumOverloadCircuit>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kDrumOverloadCircuitCount - 1u));
        break;
    case kInputParamId: plugin.params.inputGainDb = static_cast<float>(value); break;
    case kOverloadParamId: plugin.params.overload = static_cast<float>(value); break;
    case kDensityParamId: plugin.params.density = static_cast<float>(value); break;
    case kPunchParamId: plugin.params.punch = static_cast<float>(value); break;
    case kBiasParamId: plugin.params.bias = static_cast<float>(value); break;
    case kBreakupParamId: plugin.params.breakup = static_cast<float>(value); break;
    case kWeightParamId: plugin.params.weight = static_cast<float>(value); break;
    case kToneParamId: plugin.params.tone = static_cast<float>(value); break;
    case kLinkParamId: plugin.params.stereoLink = static_cast<float>(value); break;
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
    instance->gainReductionDb.store(0.0f, std::memory_order_relaxed);
    instance->overloadActivity.store(0.0f, std::memory_order_relaxed);
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
    instance->gainReductionDb.store(0.0f, std::memory_order_relaxed);
    instance->overloadActivity.store(0.0f, std::memory_order_relaxed);
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
    if (processContext->audio_inputs_count == 0u
        || processContext->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = processContext->audio_inputs[0];
    const auto& output = processContext->audio_outputs[0];
    const uint32_t frames = processContext->frames_count;
    float blockPeak = 0.0f;
    float blockReduction = 0.0f;
    float blockActivity = 0.0f;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        float left = readInputSample(input, 0u, frame);
        float right = readInputSample(input, 1u, frame);
        instance->dsp.processFrame(left, right);
        writeOutputSample(output, 0u, frame, left);
        writeOutputSample(output, 1u, frame, right);
        blockPeak = std::max(blockPeak,
            std::max(std::abs(left), std::abs(right)));
        blockReduction = std::min(
            blockReduction, instance->dsp.gainReductionDb());
        blockActivity = std::max(
            blockActivity, instance->dsp.overloadActivity());
    }
    s3g::clearAudioBufferFromChannel(output, kChannelCount, frames);
    instance->outputPeak.store(std::max(
        instance->outputPeak.load(std::memory_order_relaxed) * 0.90f,
        blockPeak), std::memory_order_relaxed);
    instance->gainReductionDb.store(blockReduction,
        std::memory_order_relaxed);
    instance->overloadActivity.store(std::max(
        instance->overloadActivity.load(std::memory_order_relaxed) * 0.88f,
        blockActivity), std::memory_order_relaxed);
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
    case kCircuitParamId: *value = static_cast<uint32_t>(params.circuit); return true;
    case kInputParamId: *value = params.inputGainDb; return true;
    case kOverloadParamId: *value = params.overload; return true;
    case kDensityParamId: *value = params.density; return true;
    case kPunchParamId: *value = params.punch; return true;
    case kBiasParamId: *value = params.bias; return true;
    case kBreakupParamId: *value = params.breakup; return true;
    case kWeightParamId: *value = params.weight; return true;
    case kToneParamId: *value = params.tone; return true;
    case kLinkParamId: *value = params.stereoLink; return true;
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
    if (id == kCircuitParamId) {
        const auto circuit = static_cast<s3g::DrumOverloadCircuit>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                0u, s3g::kDrumOverloadCircuitCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::drumOverloadCircuitName(circuit));
    } else if (std::strcmp(definition->unit, "db") == 0) {
        std::snprintf(display, size, "%+.1f dB", value);
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
    if (id == kCircuitParamId) {
        for (uint32_t circuit = 0u;
             circuit < s3g::kDrumOverloadCircuitCount; ++circuit) {
            if (std::strcmp(display, s3g::drumOverloadCircuitName(
                    static_cast<s3g::DrumOverloadCircuit>(circuit))) == 0) {
                *value = static_cast<double>(circuit);
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

} // namespace

#if defined(__APPLE__)
constexpr auto kOutputPanel =
    s3g::gui_layout::compactEffectOutputPanel(3u);
constexpr auto kDrivePanel = s3g::gui_layout::compactEffectLeftPanel(
    kOutputPanel, s3g::gui_layout::PanelRole::Engine, 5u);
constexpr auto kColorPanel = s3g::gui_layout::compactEffectRightPanel(
    s3g::gui_layout::PanelRole::ToneShape, 5u);
constexpr std::array kFirstColumnPanels { kOutputPanel, kDrivePanel };
constexpr std::array kSecondColumnPanels { kColorPanel };
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnPanels,
    s3g::gui_layout::kCompactEffectFamilyLayout.canvas));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnPanels,
    s3g::gui_layout::kCompactEffectFamilyLayout.canvas, false));

constexpr uint32_t kOutputParamIndices[] { 11u, 10u, 12u };
constexpr uint32_t kDriveParamIndices[] { 0u, 1u, 2u, 3u, 4u };
constexpr uint32_t kColorParamIndices[] { 5u, 6u, 7u, 8u, 9u };
constexpr uint32_t kDriveSliderParamIndices[] { 1u, 2u, 3u, 4u };

NSRect processorMenuRect(const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

@interface S3GDrumOverloadView : NSView {
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
- (NSRect)circuitDropdownRect;
- (void)updateMenuHover:(NSPoint)point;
@end

@implementation S3GDrumOverloadView

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
        @"s3g EFFECT DRUM OVERLOAD",
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
    drawPanel(@"DRIVE", kDrivePanel);
    drawPanel(@"COLOR", kColorPanel);

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
        if (definition.id == kCircuitParamId) {
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
    for (uint32_t row = 0u; row < std::size(kDriveParamIndices); ++row) {
        drawParam(kDriveParamIndices[row], row, kDrivePanel);
    }
    for (uint32_t row = 0u; row < std::size(kColorParamIndices); ++row) {
        drawParam(kColorParamIndices[row], row, kColorPanel);
    }

    const float reduction = plugin->gainReductionDb.load(
        std::memory_order_relaxed);
    const float activity = plugin->overloadActivity.load(
        std::memory_order_relaxed);
    [[NSString stringWithFormat:@"GR %+.1f dB  //  CORE %.0f%%",
        static_cast<double>(reduction), static_cast<double>(activity * 100.0f)]
        drawAtPoint:NSMakePoint(
            kColorPanel.frame.x + 16.0,
            kColorPanel.frame.y + kColorPanel.frame.height + 12.0)
        withAttributes:values];

    if (_openMenu == static_cast<int>(kCircuitParamId)) {
        static NSString* circuitItems[] = {
            @"CONSOLE", @"VALVE", @"CLIP", @"RUPTURE",
            @"TAPE", @"TRANSFORMER", @"DIODE", @"SPEAKER",
        };
        s3g::clap_gui::drawDropdownMenu(
            [self circuitDropdownRect], 18.0,
            circuitItems, s3g::kDrumOverloadCircuitCount,
            static_cast<int>(plugin->params.circuit),
            _hoverMenuItem, values, style);
    }
}

- (NSRect)circuitDropdownRect
{
    return NSMakeRect(
        _menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(kDrivePanel.frame.width),
        18.0 * static_cast<CGFloat>(s3g::kDrumOverloadCircuitCount));
}

- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu != static_cast<int>(kCircuitParamId)) return;
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self circuitDropdownRect], 18.0,
        s3g::kDrumOverloadCircuitCount);
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
    const bool drive = definition->id >= kCircuitParamId
        && definition->id <= kPunchParamId;
    const auto& panel = output ? kOutputPanel
        : (drive ? kDrivePanel : kColorPanel);
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
            point, &plugin->plugin, @"Effect Drum Overload",
            titleBand, _titlePresetName,
            sizeof(_titlePresetName), kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }

    if (_openMenu == static_cast<int>(kCircuitParamId)) {
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, [self circuitDropdownRect], 18.0,
            s3g::kDrumOverloadCircuitCount);
        if (hit >= 0) {
            applyParam(*plugin, kCircuitParamId,
                static_cast<double>(hit));
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, processorMenuRect(kDrivePanel, 0u))) {
        const NSRect box = processorMenuRect(kDrivePanel, 0u);
        _openMenu = static_cast<int>(kCircuitParamId);
        _hoverMenuItem = -1;
        _menuOrigin = NSMakePoint(box.origin.x, NSMaxY(box) + 3.0);
        [self setNeedsDisplay:YES];
        return;
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
        || hitPanel(kDrivePanel, kDriveSliderParamIndices,
            static_cast<uint32_t>(std::size(kDriveSliderParamIndices)), 1u)
        || hitPanel(kColorPanel, kColorParamIndices,
            static_cast<uint32_t>(std::size(kColorParamIndices)), 0u)) {
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
    instance->guiView = [[S3GDrumOverloadView alloc]
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
        [static_cast<S3GDrumOverloadView*>(instance->guiView)
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
    [static_cast<S3GDrumOverloadView*>(instance->guiView)
        startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GDrumOverloadView*>(instance->guiView)
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
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.drum-overload",
    "s3g Effect Drum Overload",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.2.1",
    "Stereo transient-aware distortion tuned to overload clean procedural drums.",
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
