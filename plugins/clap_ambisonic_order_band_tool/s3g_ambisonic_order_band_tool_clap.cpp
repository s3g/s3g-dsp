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
#include "../common/s3g_clap_macos.h"
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
constexpr uint32_t kGuiWidth = 820;
constexpr uint32_t kGuiHeight = 496;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamWeighting = 2,
    kParamBlend = 3,
    kParamOutput = 4,
    kParamOrder0 = 10,
    kParamOrder1 = 11,
    kParamOrder2 = 12,
    kParamOrder3 = 13,
    kParamOrder4 = 14,
    kParamOrder5 = 15,
    kParamOrder6 = 16,
    kParamOrder7 = 17,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiOrderBandParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    s3g::AmbiOrderBandParams params {};
    s3g::AmbiOrderBandProcessor processor {};
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
uint32_t roundedUint(double value) { return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5))); }

const char* weightingName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 3u)) {
    case 1: return "MaxRE";
    case 2: return "In-phase";
    case 3: return "Custom";
    default: return "Flat";
    }
}

s3g::AmbiOrderBandParams defaultParams()
{
    s3g::AmbiOrderBandParams params {};
    params.order = 7;
    params.weighting = s3g::AmbiUtilityWeighting::Flat;
    params.blend = 1.0f;
    params.outputGainDb = 0.0f;
    params.orderGain.fill(1.0f);
    return params;
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kParamOrder: p.params.order = std::clamp<uint32_t>(roundedUint(value), 1u, s3g::kAmbiUtilityMaxOrder); break;
    case kParamWeighting: p.params.weighting = static_cast<s3g::AmbiUtilityWeighting>(std::min<uint32_t>(roundedUint(value), 3u)); break;
    case kParamBlend: p.params.blend = static_cast<float>(value); break;
    case kParamOutput: p.params.outputGainDb = static_cast<float>(value); break;
    case kParamOrder0:
    case kParamOrder1:
    case kParamOrder2:
    case kParamOrder3:
    case kParamOrder4:
    case kParamOrder5:
    case kParamOrder6:
    case kParamOrder7:
        p.params.orderGain[static_cast<uint32_t>(id - kParamOrder0)] = static_cast<float>(value);
        break;
    default: return;
    }
    p.params = s3g::sanitizeAmbiOrderBandParams(p.params);
    p.processor.setParams(p.params);
}

double getParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kParamOrder: return p.params.order;
    case kParamWeighting: return static_cast<uint32_t>(p.params.weighting);
    case kParamBlend: return p.params.blend;
    case kParamOutput: return p.params.outputGainDb;
    case kParamOrder0:
    case kParamOrder1:
    case kParamOrder2:
    case kParamOrder3:
    case kParamOrder4:
    case kParamOrder5:
    case kParamOrder6:
    case kParamOrder7:
        return p.params.orderGain[static_cast<uint32_t>(id - kParamOrder0)];
    default: return 0.0;
    }
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}
bool activate(const clap_plugin_t* plugin, double, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->params = s3g::sanitizeAmbiOrderBandParams(p->params);
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
    std::strncpy(info->name, isInput ? "Ambi In" : "Ambi Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 12; }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Order / Band", sizeof(info->module));
    if (index == 0) { info->id = kParamOrder; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Active order", sizeof(info->name)); info->min_value = 1; info->max_value = 7; info->default_value = 7; return true; }
    if (index == 1) { info->id = kParamWeighting; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Weighting preset", sizeof(info->name)); info->min_value = 0; info->max_value = 3; info->default_value = 0; return true; }
    if (index == 2) { info->id = kParamBlend; std::strncpy(info->name, "Weighting amount", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 1; return true; }
    if (index == 3) { info->id = kParamOutput; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; return true; }
    if (index >= 4 && index <= 11) {
        const uint32_t order = index - 4u;
        info->id = kParamOrder0 + order;
        std::snprintf(info->name, sizeof(info->name), "Order %u gain", order);
        info->min_value = 0;
        info->max_value = 2;
        info->default_value = 1;
        return true;
    }
    return false;
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParam(*self(plugin), paramId);
    return (paramId >= kParamOrder && paramId <= kParamOutput) || (paramId >= kParamOrder0 && paramId <= kParamOrder7);
}
bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (paramId == kParamOrder) { std::snprintf(display, size, "%uOA", roundedUint(value)); return true; }
    if (paramId == kParamWeighting) { std::snprintf(display, size, "%s", weightingName(roundedUint(value))); return true; }
    if (paramId == kParamBlend) { std::snprintf(display, size, "%.0f %%", value * 100.0); return true; }
    if (paramId == kParamOutput) { std::snprintf(display, size, "%+.1f dB", value); return true; }
    if (paramId >= kParamOrder0 && paramId <= kParamOrder7) { std::snprintf(display, size, "%.0f %%", value * 100.0); return true; }
    return false;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return (paramId >= kParamOrder && paramId <= kParamOutput) || (paramId >= kParamOrder0 && paramId <= kParamOrder7);
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
    p->params = s3g::sanitizeAmbiOrderBandParams(state.params);
    p->processor.setParams(p->params);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {
constexpr const auto& kTransformLayout =
    s3g::gui_layout::kTransformFamilyLayout;
}

@interface S3GAmbisonicOrderBandView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItems;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)updateSliderAtPoint:(NSPoint)pt;
- (void)updateMenuHover:(NSPoint)pt;
@end

@implementation S3GAmbisonicOrderBandView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSMakePoint(0, 0);
        _menuItems = 0;
        _refreshTimer = nil;
    }
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
- (void)refreshTimerFired:(NSTimer*)timer { (void)timer; if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y,
        kTransformLayout.output.frame.x,
        kTransformLayout.output.frame.width,
        attrs, attrs, style);
}
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(
        name, [value uppercaseString], y,
        kTransformLayout.output.frame.x,
        kTransformLayout.output.frame.width,
        attrs, attrs, style);
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    const float pk = p->outputPeak.exchange(
        p->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    const auto titleBand =
        s3g::gui_layout::transformTitleBand(kTransformLayout.canvas);
    s3g::clap_gui::drawTransformTitleBand(
        @"s3g AMBI TRANSFORM ORDER BAND 64CH",
        [NSString stringWithUTF8String:p->presetName],
        s3g::clap_gui::peakDbText(pk), titleBand, style);

    NSRect fieldPanel =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPanel);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ORDER WEIGHT MAP", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    NSRect field =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPlot);
    [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);

    const CGFloat slotW = field.size.width / 8.0;
    for (uint32_t order = 0; order <= 7; ++order) {
        const CGFloat x = field.origin.x + slotW * order + 12.0;
        const CGFloat w = slotW - 24.0;
        const float standard = s3g::ambiUtilityStandardOrderWeight(p->params.weighting, order, p->params.order);
        const float custom = p->params.orderGain[order];
        const float active = order <= p->params.order ? s3g::lerp(custom, standard * custom, p->params.blend) : 0.0f;
        const CGFloat norm = std::clamp<CGFloat>(active / 2.0f, 0.0, 1.0);
        const CGFloat h = (field.size.height - 66.0) * norm;
        [s3g::clap_gui::color(order <= p->params.order ? 0x1a1a1a : 0x0b0b0b) setFill];
        NSRectFill(NSMakeRect(x, field.origin.y + 28, w, field.size.height - 66));
        [s3g::clap_gui::color(0xd0d0d0, order <= p->params.order ? 0.70 : 0.16) setFill];
        NSRectFill(NSMakeRect(x + 2, field.origin.y + field.size.height - 38 - h, w - 4, h));
        [style.grid setStroke];
        NSFrameRect(NSMakeRect(x, field.origin.y + 28, w, field.size.height - 66));
        [[NSString stringWithFormat:@"%u", order] drawAtPoint:NSMakePoint(x + w * 0.5 - 3, field.origin.y + field.size.height - 28) withAttributes:small];
        [[NSString stringWithFormat:@"%.0f", static_cast<double>(active * 100.0f)] drawAtPoint:NSMakePoint(x + 1, field.origin.y + 12) withAttributes:small];
    }
    [@"ORDER 0..7 / PER-ORDER GAIN AFTER WEIGHTING"
        drawAtPoint:NSMakePoint(
            fieldPanel.origin.x + 16.0, NSMaxY(field) + 12.0)
        withAttributes:small];

    const auto drawPanel =
        [&](NSString* title, const s3g::gui_layout::Panel& panel) {
            s3g::clap_gui::drawPanelFrame(panel, style);
            s3g::clap_gui::drawPanelHeader(
                title, true, panel, text, style);
        };
    drawPanel(@"OUTPUT", kTransformLayout.output);
    drawPanel(@"WEIGHTING", kTransformLayout.weighting);
    drawPanel(@"ORDER BANDS", kTransformLayout.orderBands);

    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB",
            static_cast<double>(p->params.outputGainDb)]
        norm:(p->params.outputGainDb + 60.0) / 72.0
        y:s3g::gui_layout::rowY(kTransformLayout.output, 0u)
        attrs:small style:style];
    [self drawMenu:@"ORDER"
        value:[NSString stringWithFormat:@"%uOA", p->params.order]
        y:s3g::gui_layout::rowY(kTransformLayout.output, 1u)
        attrs:small style:style];
    [self drawMenu:@"WEIGHTING"
        value:[NSString stringWithUTF8String:weightingName(
            static_cast<uint32_t>(p->params.weighting))]
        y:s3g::gui_layout::rowY(kTransformLayout.weighting, 0u)
        attrs:small style:style];
    [self drawSlider:@"AMOUNT"
        value:[NSString stringWithFormat:@"%.0f%%",
            static_cast<double>(p->params.blend * 100.0f)]
        norm:p->params.blend
        y:s3g::gui_layout::rowY(kTransformLayout.weighting, 1u)
        attrs:small style:style];

    for (uint32_t order = 0; order <= 7; ++order) {
        [self drawSlider:[NSString stringWithFormat:@"ORDER %u", order]
                   value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.orderGain[order] * 100.0f)]
                    norm:p->params.orderGain[order] / 2.0
                       y:s3g::gui_layout::rowY(
                           kTransformLayout.orderBands, order)
                   attrs:small
                   style:style];
    }

    if (_openMenu > 0 && _menuItems > 0) {
        NSString* weights[] = {
            @"FLAT", @"MAXRE", @"IN-PHASE", @"CUSTOM"
        };
        NSString* orders[] = {
            @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA"
        };
        NSString** items = _openMenu == 1 ? weights : orders;
        const int selected = _openMenu == 1
            ? static_cast<int>(p->params.weighting)
            : static_cast<int>(p->params.order) - 1;
        s3g::clap_gui::drawDropdownMenu(
            NSMakeRect(_menuOrigin.x, _menuOrigin.y,
                s3g::gui_layout::processorMenuWidth(
                    kTransformLayout.output.frame.width),
                18.0 * _menuItems),
            18.0, items, _menuItems, selected,
            _hoverMenuItem, small, style);
    }
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp(
        (pt.x - s3g::gui_layout::processorControlX(
            kTransformLayout.output.frame.x))
            / s3g::gui_layout::processorTrackWidth(
                kTransformLayout.output.frame.width),
        0.0, 1.0);
    if (_dragSlider == 1) [self setParam:kParamBlend value:norm];
    else if (_dragSlider >= 10 && _dragSlider <= 17) [self setParam:static_cast<clap_id>(_dragSlider) value:norm * 2.0];
    else if (_dragSlider == 20) [self setParam:kParamOutput value:-60.0 + norm * 72.0];
}
- (void)updateMenuHover:(NSPoint)pt
{
    if (_openMenu <= 0 || _menuItems == 0) return;
    const int hover = s3g::clap_gui::dropdownHitIndex(
        pt, NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(
                kTransformLayout.output.frame.width),
            18.0 * _menuItems),
        18.0, _menuItems);
    if (hover != _hoverMenuItem) { _hoverMenuItem = hover; [self setNeedsDisplay:YES]; }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand =
        s3g::gui_layout::transformTitleBand(kTransformLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Ambi Transform Order Band",
            titleBand, p->presetName, sizeof(p->presetName))) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(
            pt, NSMakeRect(_menuOrigin.x, _menuOrigin.y,
                s3g::gui_layout::processorMenuWidth(
                    kTransformLayout.output.frame.width),
                18.0 * _menuItems),
            18.0, _menuItems);
        if (hit >= 0) {
            if (_openMenu == 1)
                [self setParam:kParamWeighting value:hit];
            else
                [self setParam:kParamOrder value:hit + 1.0];
        }
        _openMenu = 0; _hoverMenuItem = -1; [self setNeedsDisplay:YES]; return;
    }
    struct MenuHit {
        s3g::gui_layout::Rect rect;
        int menu;
        uint32_t items;
    };
    const MenuHit menus[] {
        { s3g::gui_layout::sliderHitRect(
              kTransformLayout.weighting, 0u),
            1, 4u },
        { s3g::gui_layout::sliderHitRect(
              kTransformLayout.output, 1u),
            2, 7u },
    };
    for (const auto& menu : menus) {
        if (!NSPointInRect(pt, s3g::clap_gui::cocoaRect(menu.rect)))
            continue;
        _openMenu = menu.menu;
        _menuItems = menu.items;
        _menuOrigin = NSMakePoint(
            s3g::gui_layout::processorControlX(
                kTransformLayout.output.frame.x),
            menu.rect.y + 24.0);
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    struct SliderHit {
        s3g::gui_layout::Rect rect;
        int slider;
        clap_id param;
    };
    SliderHit hits[10];
    uint32_t count = 0u;
    hits[count++] = {
        s3g::gui_layout::sliderHitRect(
            kTransformLayout.output, 0u),
        20, kParamOutput
    };
    hits[count++] = {
        s3g::gui_layout::sliderHitRect(
            kTransformLayout.weighting, 1u),
        1, kParamBlend
    };
    for (uint32_t order = 0; order <= 7; ++order) {
        hits[count++] = {
            s3g::gui_layout::sliderHitRect(
                kTransformLayout.orderBands, order),
            static_cast<int>(kParamOrder0 + order),
            static_cast<clap_id>(kParamOrder0 + order)
        };
    }
    for (uint32_t i = 0u; i < count; ++i) {
        const auto& hit = hits[i];
        if (!NSPointInRect(pt, s3g::clap_gui::cocoaRect(hit.rect)))
            continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, hit.param, &defaultValue)) {
            [self setParam:hit.param value:defaultValue];
            _dragSlider = -1;
        } else {
            _dragSlider = hit.slider;
            [self updateSliderAtPoint:pt];
        }
        return;
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:pt];
    if (_dragSlider >= 0) [self updateSliderAtPoint:pt];
}
- (void)mouseMoved:(NSEvent*)event { [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbisonicOrderBandView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p && p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicOrderBandView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<S3GAmbisonicOrderBandView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicOrderBandView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };

} // namespace
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
    "org.s3g.s3g-dsp.ambisonic-order-band-tool-64",
    "s3g Ambi Transform Order Band 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "64-channel ACN/SN3D order-band gain and weighting utility up to 7OA.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = defaultParams();
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
