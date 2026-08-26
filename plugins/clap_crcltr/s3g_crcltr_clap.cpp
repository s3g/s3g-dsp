#include "s3g_crcltr.h"

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
#include <new>
#include <vector>

namespace {

constexpr uint32_t kChannelCount = 2u;
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kGuiWidth = 760u;
constexpr uint32_t kGuiHeight = 376u;

constexpr clap_id kLoop1RateParamId = 1u;
constexpr clap_id kLoop2RateParamId = 2u;
constexpr clap_id kCrossfadeModeParamId = 3u;
constexpr clap_id kCrossfadeParamId = 4u;
constexpr clap_id kBlendParamId = 5u;
constexpr clap_id kInputGainParamId = 6u;
constexpr clap_id kOutputGainParamId = 7u;
constexpr clap_id kRecordParamId = 8u;
constexpr clap_id kRecordTargetParamId = 9u;
constexpr clap_id kMonitorModeParamId = 10u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr ParamDef kParamDefs[] {
    { kLoop1RateParamId, "Loop 1 Rate", "Loops", 0.25, 2.75, 1.0, false },
    { kLoop2RateParamId, "Loop 2 Rate", "Loops", 0.25, 2.75, 1.0, false },
    { kCrossfadeModeParamId, "Xfade Type", "Crossfade", 0.0, 2.0, 0.0, true },
    { kCrossfadeParamId, "Xfade", "Crossfade", 0.0, 1.0, 0.5, false },
    { kBlendParamId, "Blend", "Mix", 0.0, 1.0, 0.5, false },
    { kInputGainParamId, "Input Gain", "Mix", 0.0, 1.0, 1.0, false },
    { kOutputGainParamId, "Output Gain", "Mix", 0.0, 1.0, 1.0, false },
    { kRecordParamId, "Record", "Record", 0.0, 1.0, 0.0, true },
    { kRecordTargetParamId, "Record Target", "Record", 0.0, 2.0, 1.0, true },
    { kMonitorModeParamId, "Record Monitor", "Record", 0.0, 2.0, 1.0, true },
};
constexpr uint32_t kParamCount = static_cast<uint32_t>(
    sizeof(kParamDefs) / sizeof(kParamDefs[0]));

struct SavedState {
    uint32_t version = kStateVersion;
    float loop1Rate = 1.0f;
    float loop2Rate = 1.0f;
    float crossfade = 0.5f;
    float blend = 0.5f;
    float inputGain = 1.0f;
    float outputGain = 1.0f;
    uint32_t crossfadeMode = 0u;
    uint32_t recordTarget = 1u;
    uint32_t monitorMode = 1u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::CrcltrParams params {};
    s3g::Crcltr dsp;
    std::vector<float> inputLeft;
    std::vector<float> inputRight;
    std::vector<float> outputLeft;
    std::vector<float> outputRight;
    std::atomic<float> outputPeak { 0.0f };
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

const ParamDef* findParam(clap_id id)
{
    for (const auto& def : kParamDefs)
        if (def.id == id) return &def;
    return nullptr;
}

uint32_t steppedValue(double value, uint32_t maximum)
{
    return std::min<uint32_t>(maximum,
        static_cast<uint32_t>(std::max(0.0, std::round(value))));
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kLoop1RateParamId:
        plugin.params.loop1Rate = static_cast<float>(std::clamp(value, 0.25, 2.75));
        break;
    case kLoop2RateParamId:
        plugin.params.loop2Rate = static_cast<float>(std::clamp(value, 0.25, 2.75));
        break;
    case kCrossfadeModeParamId:
        plugin.params.crossfadeMode = static_cast<s3g::CrcltrCrossfadeMode>(
            steppedValue(value, 2u));
        break;
    case kCrossfadeParamId:
        plugin.params.crossfade = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kBlendParamId:
        plugin.params.blend = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kInputGainParamId:
        plugin.params.inputGain = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kOutputGainParamId:
        plugin.params.outputGain = static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kRecordParamId:
        plugin.params.record = value >= 0.5;
        break;
    case kRecordTargetParamId:
        plugin.params.recordTarget = static_cast<s3g::CrcltrRecordTarget>(
            steppedValue(value, 2u));
        break;
    case kMonitorModeParamId:
        plugin.params.monitorMode = static_cast<s3g::CrcltrMonitorMode>(
            steppedValue(value, 2u));
        break;
    default:
        return;
    }
    plugin.dsp.setParams(plugin.params);
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

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t,
              uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    try {
        p->inputLeft.resize(p->maxFrames);
        p->inputRight.resize(p->maxFrames);
        p->outputLeft.resize(p->maxFrames);
        p->outputRight.resize(p->maxFrames);
    } catch (...) {
        return false;
    }
    p->params.record = false;
    if (!p->dsp.prepare(sampleRate, p->maxFrames)) return false;
    p->dsp.setParams(p->params);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->params.record = false;
    p->dsp.setParams(p->params);
    p->dsp.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, value->param_id, value->value);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
                            const clap_process_t* processInfo)
{
    auto* p = self(plugin);
    readParamEvents(*p, processInfo->in_events);
    if (processInfo->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;

    const auto* input = processInfo->audio_inputs_count > 0u
        ? &processInfo->audio_inputs[0] : nullptr;
    const auto& output = processInfo->audio_outputs[0];
    const uint32_t frames = std::min(processInfo->frames_count, p->maxFrames);
    if (output.channel_count < kChannelCount) return CLAP_PROCESS_CONTINUE;

    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (input && input->channel_count > 0u && input->data32
            && input->data32[0]) {
            p->inputLeft[frame] = input->data32[0][frame];
        } else if (input && input->channel_count > 0u && input->data64
                   && input->data64[0]) {
            p->inputLeft[frame] = static_cast<float>(input->data64[0][frame]);
        } else {
            p->inputLeft[frame] = 0.0f;
        }

        if (input && input->channel_count > 1u && input->data32
            && input->data32[1]) {
            p->inputRight[frame] = input->data32[1][frame];
        } else if (input && input->channel_count > 1u && input->data64
                   && input->data64[1]) {
            p->inputRight[frame] = static_cast<float>(input->data64[1][frame]);
        } else {
            p->inputRight[frame] = p->inputLeft[frame];
        }
    }

    p->dsp.setParams(p->params);
    p->dsp.process(p->inputLeft.data(), p->inputRight.data(),
        p->outputLeft.data(), p->outputRight.data(), frames);

    float blockPeak = 0.0f;
    for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = channel == 0u ? p->outputLeft[frame]
                : channel == 1u ? p->outputRight[frame] : 0.0f;
            if (output.data32 && output.data32[channel])
                output.data32[channel][frame] = value;
            if (output.data64 && output.data64[channel])
                output.data64[channel][frame] = static_cast<double>(value);
            blockPeak = std::max(blockPeak, std::abs(value));
        }
        for (uint32_t frame = frames; frame < processInfo->frames_count; ++frame) {
            if (output.data32 && output.data32[channel])
                output.data32[channel][frame] = 0.0f;
            if (output.data64 && output.data64[channel])
                output.data64[channel][frame] = 0.0;
        }
    }
    p->outputPeak.store(std::max(
        p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info)
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
    audioPortsCount,
    audioPortsGet,
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !findParam(id)) return false;
    const auto& params = self(plugin)->params;
    switch (id) {
    case kLoop1RateParamId: *value = params.loop1Rate; return true;
    case kLoop2RateParamId: *value = params.loop2Rate; return true;
    case kCrossfadeModeParamId:
        *value = static_cast<uint32_t>(params.crossfadeMode); return true;
    case kCrossfadeParamId: *value = params.crossfade; return true;
    case kBlendParamId: *value = params.blend; return true;
    case kInputGainParamId: *value = params.inputGain; return true;
    case kOutputGainParamId: *value = params.outputGain; return true;
    case kRecordParamId: *value = params.record ? 1.0 : 0.0; return true;
    case kRecordTargetParamId:
        *value = static_cast<uint32_t>(params.recordTarget); return true;
    case kMonitorModeParamId:
        *value = static_cast<uint32_t>(params.monitorMode); return true;
    default: return false;
    }
}

const char* crossfadeModeName(uint32_t value)
{
    constexpr const char* names[] { "Manual", "Sine LFO", "Trapezoid LFO" };
    return names[std::min<uint32_t>(value, 2u)];
}

const char* targetName(uint32_t value)
{
    constexpr const char* names[] { "Loop 1", "Both", "Loop 2" };
    return names[std::min<uint32_t>(value, 2u)];
}

const char* monitorName(uint32_t value)
{
    constexpr const char* names[] { "Thru Loop 1", "Silent", "Thru Loop 2" };
    return names[std::min<uint32_t>(value, 2u)];
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id id, double value,
                       char* display, uint32_t size)
{
    if (!display || size == 0u || !findParam(id)) return false;
    switch (id) {
    case kLoop1RateParamId:
    case kLoop2RateParamId:
        std::snprintf(display, size, "%.2fx", value);
        break;
    case kCrossfadeModeParamId:
        std::snprintf(display, size, "%s", crossfadeModeName(steppedValue(value, 2u)));
        break;
    case kCrossfadeParamId:
        if (self(plugin)->params.crossfadeMode == s3g::CrcltrCrossfadeMode::Manual)
            std::snprintf(display, size, "%.0f%%", value * 100.0);
        else
            std::snprintf(display, size, "%.2fx", 0.25 * std::pow(16.0, value));
        break;
    case kBlendParamId:
    case kInputGainParamId:
    case kOutputGainParamId:
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        break;
    case kRecordParamId:
        std::snprintf(display, size, "%s", value >= 0.5 ? "RECORD" : "Idle");
        break;
    case kRecordTargetParamId:
        std::snprintf(display, size, "%s", targetName(steppedValue(value, 2u)));
        break;
    case kMonitorModeParamId:
        std::snprintf(display, size, "%s", monitorName(steppedValue(value, 2u)));
        break;
    default:
        return false;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t* plugin, clap_id id, const char* display,
                       double* value)
{
    const auto* def = findParam(id);
    if (!def || !display || !value) return false;
    double parsed = std::atof(display);
    if (id == kCrossfadeModeParamId) {
        if (std::strcmp(display, "Manual") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Sine LFO") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Trapezoid LFO") == 0) parsed = 2.0;
    } else if (id == kRecordParamId) {
        parsed = std::strcmp(display, "RECORD") == 0 ? 1.0 : 0.0;
    } else if (id == kRecordTargetParamId) {
        if (std::strcmp(display, "Loop 1") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Both") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Loop 2") == 0) parsed = 2.0;
    } else if (id == kMonitorModeParamId) {
        if (std::strcmp(display, "Thru Loop 1") == 0) parsed = 0.0;
        else if (std::strcmp(display, "Silent") == 0) parsed = 1.0;
        else if (std::strcmp(display, "Thru Loop 2") == 0) parsed = 2.0;
    } else if (id == kCrossfadeParamId
               && self(plugin)->params.crossfadeMode
                    != s3g::CrcltrCrossfadeMode::Manual
               && std::strchr(display, 'x')) {
        parsed = std::log(std::max(0.25, parsed) / 0.25) / std::log(16.0);
    }
    if ((id == kCrossfadeParamId || id == kBlendParamId
         || id == kInputGainParamId || id == kOutputGainParamId)
        && std::strchr(display, '%')) parsed *= 0.01;
    *value = std::clamp(parsed, def->minimum, def->maximum);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* events,
                 const clap_output_events_t*)
{
    readParamEvents(*self(plugin), events);
}

const clap_plugin_params_t paramsExtension {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush,
};

SavedState savedStateFor(const s3g::CrcltrParams& params)
{
    SavedState state;
    state.loop1Rate = params.loop1Rate;
    state.loop2Rate = params.loop2Rate;
    state.crossfade = params.crossfade;
    state.blend = params.blend;
    state.inputGain = params.inputGain;
    state.outputGain = params.outputGain;
    state.crossfadeMode = static_cast<uint32_t>(params.crossfadeMode);
    state.recordTarget = static_cast<uint32_t>(params.recordTarget);
    state.monitorMode = static_cast<uint32_t>(params.monitorMode);
    return state;
}

bool writeAll(const clap_ostream_t* stream, const void* source, uint64_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t written = stream->write(stream, bytes + offset, size - offset);
        if (written <= 0) return false;
        offset += static_cast<uint64_t>(written);
    }
    return true;
}

bool readAll(const clap_istream_t* stream, void* destination, uint64_t size)
{
    auto* bytes = static_cast<uint8_t*>(destination);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t read = stream->read(stream, bytes + offset, size - offset);
        if (read <= 0) return false;
        offset += static_cast<uint64_t>(read);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state = savedStateFor(self(plugin)->params);
    return writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readAll(stream, &state, sizeof(state)) || state.version != kStateVersion)
        return false;

    auto* p = self(plugin);
    applyParam(*p, kLoop1RateParamId, state.loop1Rate);
    applyParam(*p, kLoop2RateParamId, state.loop2Rate);
    applyParam(*p, kCrossfadeModeParamId, state.crossfadeMode);
    applyParam(*p, kCrossfadeParamId, state.crossfade);
    applyParam(*p, kBlendParamId, state.blend);
    applyParam(*p, kInputGainParamId, state.inputGain);
    applyParam(*p, kOutputGainParamId, state.outputGain);
    applyParam(*p, kRecordTargetParamId, state.recordTarget);
    applyParam(*p, kMonitorModeParamId, state.monitorMode);
    applyParam(*p, kRecordParamId, 0.0);
    return true;
}

const clap_plugin_state_t stateExtension {
    stateSave,
    stateLoad,
};

} // namespace

#if defined(__APPLE__)

constexpr auto kOutputPanel =
    s3g::gui_layout::compactEffectOutputPanel(3u);
constexpr auto kCapturePanel =
    s3g::gui_layout::compactEffectLeftPanel(
        kOutputPanel, s3g::gui_layout::PanelRole::EventTiming, 3u);
constexpr auto kLoopsPanel =
    s3g::gui_layout::compactEffectRightPanel(
        s3g::gui_layout::PanelRole::Projection, 2u);
constexpr auto kCrossfadePanel =
    s3g::gui_layout::fittedStackPanel(
        s3g::gui_layout::PanelRole::Relationships, kLoopsPanel, 2u);
constexpr std::array kFirstColumnPanels { kOutputPanel, kCapturePanel };
constexpr std::array kSecondColumnPanels { kLoopsPanel, kCrossfadePanel };
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas,
    false));

NSRect processorMenuRect(const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

@interface S3GCrcltrView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    bool _recordHeld;
    NSPoint _menuOrigin;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GCrcltrView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _recordHeld = false;
        _menuOrigin = NSZeroPoint;
        _timer = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    NSArray* existingAreas = [[self trackingAreas] copy];
    for (NSTrackingArea* area in existingAreas) {
        [self removeTrackingArea:area];
    }
    [existingAreas release];
    NSTrackingAreaOptions options = NSTrackingMouseMoved
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
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_recordHeld && _plugin) {
        applyParam(*static_cast<Plugin*>(_plugin), kRecordParamId, 0.0);
        _recordHeld = false;
    }
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawSlider:(NSString*)label
              param:(clap_id)paramId
                row:(uint32_t)row
              panel:(const s3g::gui_layout::Panel&)panel
              attrs:(NSDictionary*)attrs
             values:(NSDictionary*)values
              style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const ParamDef* def = findParam(paramId);
    if (!p || !def) return;
    double value = 0.0;
    paramsGetValue(&p->plugin, paramId, &value);
    const double span = std::max(0.000001, def->maximum - def->minimum);
    const CGFloat norm = static_cast<CGFloat>(
        (value - def->minimum) / span);
    char text[32] {};
    paramsValueToText(&p->plugin, paramId, value, text, sizeof(text));
    s3g::clap_gui::drawProcessorSlider(
        label, [NSString stringWithUTF8String:text], norm,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, values, style);
}

- (void)drawMenu:(NSString*)label
            value:(NSString*)value
              row:(uint32_t)row
            panel:(const s3g::gui_layout::Panel&)panel
            attrs:(NSDictionary*)attrs
           values:(NSDictionary*)values
            style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(label, value,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, values, style);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    s3g::clap_gui::Style style;
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill([self bounds]);

    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    s3g::clap_gui::drawCompactEffectTitleBand(
        @"s3g EFFECT CRCLTR",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);

    const auto drawPanel = [&](NSString* title,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel, style);
        s3g::clap_gui::drawPanelHeader(title, true, panel, labels, style);
    };
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"CAPTURE", kCapturePanel);
    drawPanel(@"LOOPS", kLoopsPanel);
    drawPanel(@"CROSSFADE", kCrossfadePanel);

    [self drawSlider:@"IN" param:kInputGainParamId row:0u panel:kOutputPanel
        attrs:labels values:values style:style];
    [self drawSlider:@"BLEND" param:kBlendParamId row:1u panel:kOutputPanel
        attrs:labels values:values style:style];
    [self drawSlider:@"OUT" param:kOutputGainParamId row:2u panel:kOutputPanel
        attrs:labels values:values style:style];

    s3g::clap_gui::drawToggle(@"RECORD", p->params.record,
        s3g::gui_layout::rowY(kCapturePanel, 0u), labels, values, style,
        s3g::gui_layout::processorLabelX(kCapturePanel.frame.x),
        s3g::gui_layout::processorControlX(kCapturePanel.frame.x),
        s3g::gui_layout::processorMenuWidth(kCapturePanel.frame.width));
    [self drawMenu:@"TARGET"
        value:[NSString stringWithUTF8String:targetName(
            static_cast<uint32_t>(p->params.recordTarget))]
        row:1u panel:kCapturePanel attrs:labels values:values style:style];
    [self drawMenu:@"MONITOR"
        value:[NSString stringWithUTF8String:monitorName(
            static_cast<uint32_t>(p->params.monitorMode))]
        row:2u panel:kCapturePanel attrs:labels values:values style:style];

    [self drawSlider:@"LOOP 1" param:kLoop1RateParamId row:0u panel:kLoopsPanel
        attrs:labels values:values style:style];
    [self drawSlider:@"LOOP 2" param:kLoop2RateParamId row:1u panel:kLoopsPanel
        attrs:labels values:values style:style];
    [self drawMenu:@"TYPE"
        value:[NSString stringWithUTF8String:crossfadeModeName(
            static_cast<uint32_t>(p->params.crossfadeMode))]
        row:0u panel:kCrossfadePanel attrs:labels values:values style:style];
    [self drawSlider:(p->params.crossfadeMode == s3g::CrcltrCrossfadeMode::Manual
            ? @"XFADE" : @"RATE")
        param:kCrossfadeParamId row:1u panel:kCrossfadePanel
        attrs:labels values:values style:style];

    [@"hold RECORD while capturing; release to close the loop"
        drawAtPoint:NSMakePoint(kCapturePanel.frame.x + 16.0,
            kCapturePanel.frame.y + kCapturePanel.frame.height + 12.0)
        withAttributes:values];
    [@"dual loop rates feed manual or moving crossfade"
        drawAtPoint:NSMakePoint(kCrossfadePanel.frame.x + 16.0,
            kCrossfadePanel.frame.y + kCrossfadePanel.frame.height + 12.0)
        withAttributes:values];

    if (_openMenu > 0) {
        static NSString* modeItems[] = {
            @"MANUAL", @"SINE LFO", @"TRAPEZOID LFO",
        };
        static NSString* targetItems[] = { @"LOOP 1", @"BOTH", @"LOOP 2" };
        static NSString* monitorItems[] = {
            @"THRU LOOP 1", @"SILENT", @"THRU LOOP 2",
        };
        NSString** items = _openMenu == static_cast<int>(kCrossfadeModeParamId)
            ? modeItems
            : _openMenu == static_cast<int>(kRecordTargetParamId)
            ? targetItems : monitorItems;
        int selected = 0;
        if (_openMenu == static_cast<int>(kCrossfadeModeParamId)) {
            selected = static_cast<int>(p->params.crossfadeMode);
        } else if (_openMenu == static_cast<int>(kRecordTargetParamId)) {
            selected = static_cast<int>(p->params.recordTarget);
        } else {
            selected = static_cast<int>(p->params.monitorMode);
        }
        const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width),
            54.0);
        s3g::clap_gui::drawDropdownMenu(
            menu, 18.0, items, 3u, selected, _hoverMenuItem, values, style);
    }
}

- (void)openMenuForParam:(clap_id)paramId
                    panel:(const s3g::gui_layout::Panel&)panel
                      row:(uint32_t)row
{
    const NSRect box = processorMenuRect(panel, row);
    _openMenu = static_cast<int>(paramId);
    _hoverMenuItem = -1;
    _menuOrigin = NSMakePoint(box.origin.x, NSMaxY(box) + 3.0);
    [self setNeedsDisplay:YES];
}

- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0) return;
    const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width), 54.0);
    const int hover = s3g::clap_gui::dropdownHitIndex(point, menu, 18.0, 3u);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const ParamDef* def = p
        ? findParam(static_cast<clap_id>(_dragSlider)) : nullptr;
    if (!p || !def) return;
    const bool output = def->id == kInputGainParamId
        || def->id == kBlendParamId || def->id == kOutputGainParamId;
    const bool loop = def->id == kLoop1RateParamId
        || def->id == kLoop2RateParamId;
    const auto& panel = output ? kOutputPanel
        : loop ? kLoopsPanel : kCrossfadePanel;
    const double controlX =
        s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double norm = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    applyParam(*p, def->id,
        def->minimum + norm * (def->maximum - def->minimum));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;

    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            point, &p->plugin, @"Effect CRCLTR", titleBand,
            _titlePresetName, sizeof(_titlePresetName),
            kOutputGainParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }

    if (_openMenu > 0) {
        const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(kOutputPanel.frame.width),
            54.0);
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, menu, 18.0, 3u);
        if (hit >= 0) {
            applyParam(*p, static_cast<clap_id>(_openMenu),
                static_cast<double>(hit));
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    if (NSPointInRect(point, processorMenuRect(kCrossfadePanel, 0u))) {
        [self openMenuForParam:kCrossfadeModeParamId
            panel:kCrossfadePanel row:0u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kCapturePanel, 1u))) {
        [self openMenuForParam:kRecordTargetParamId
            panel:kCapturePanel row:1u];
        return;
    }
    if (NSPointInRect(point, processorMenuRect(kCapturePanel, 2u))) {
        [self openMenuForParam:kMonitorModeParamId
            panel:kCapturePanel row:2u];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kCapturePanel, 0u)))) {
        applyParam(*p, kRecordParamId, 1.0);
        _recordHeld = true;
        [self setNeedsDisplay:YES];
        return;
    }

    const struct {
        clap_id paramId;
        const s3g::gui_layout::Panel* panel;
        uint32_t row;
    } sliders[] {
        { kInputGainParamId, &kOutputPanel, 0u },
        { kBlendParamId, &kOutputPanel, 1u },
        { kOutputGainParamId, &kOutputPanel, 2u },
        { kLoop1RateParamId, &kLoopsPanel, 0u },
        { kLoop2RateParamId, &kLoopsPanel, 1u },
        { kCrossfadeParamId, &kCrossfadePanel, 1u },
    };
    for (const auto& slider : sliders) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(
                    *slider.panel, slider.row)))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, slider.paramId, &defaultValue)) {
            applyParam(*p, slider.paramId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(slider.paramId);
            [self updateSlider:point];
        }
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:
        [self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:point];
    if (_dragSlider > 0) [self updateSlider:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_recordHeld && _plugin) {
        applyParam(*static_cast<Plugin*>(_plugin), kRecordParamId, 0.0);
        _recordHeld = false;
        [self setNeedsDisplay:YES];
    }
    _dragSlider = -1;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
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
    p->guiView = [[S3GCrcltrView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(
            p->guiViewport, static_cast<NSView*>(p->guiView),
            kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p || !p->guiView) return;
    p->guiVisible = false;
    [static_cast<S3GCrcltrView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
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
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        p->guiViewport, static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GCrcltrView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GCrcltrView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide,
};

#else
namespace {
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
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.crcltr",
    "s3g Effect CRCLTR 2",
    "s3g",
    "https://github.com/s3g/crcltr",
    "",
    "",
    "0.1.0",
    "Stereo dual-loop CRCLTR port with a portable Daisy-ready DSP core.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory_t*,
                                  const clap_host_t* host,
                                  const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
