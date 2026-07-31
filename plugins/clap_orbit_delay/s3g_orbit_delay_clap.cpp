#include "s3g_realtime.h"
#include "s3g_orbit_delay.h"
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
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = 2;
constexpr uint32_t kOutputChannels = s3g::kOrbitDelayChannels;
constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kGuiWidth = 760;
constexpr uint32_t kGuiHeight = 376;

constexpr clap_id kPosParamId = 1;
constexpr clap_id kSpreadParamId = 2;
constexpr clap_id kRotateParamId = 3;
constexpr clap_id kWidthParamId = 4;
constexpr clap_id kFocusParamId = 5;
constexpr clap_id kStereoParamId = 6;
constexpr clap_id kDelayParamId = 7;
constexpr clap_id kFeedbackParamId = 8;
constexpr clap_id kOrbitParamId = 9;
constexpr clap_id kDampParamId = 10;
constexpr clap_id kWetParamId = 11;
constexpr clap_id kGainParamId = 12;

struct SavedState { uint32_t version = kStateVersion; s3g::OrbitDelayParams params {}; };
struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::OrbitDelayParams params {};
    s3g::OrbitDelay dsp;
    std::vector<float> inputL;
    std::vector<float> inputR;
    std::vector<std::vector<float>> output32;
    std::vector<float*> outputPtrs;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};
Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

struct ParamDef { clap_id id; const char* name; const char* label; double min; double max; double def; const char* unit; };
constexpr ParamDef kParamDefs[] {
    { kPosParamId, "Pos", "POS", 1.0, 16.0, 1.0, "" },
    { kSpreadParamId, "Spread", "SPRD", 0.05, 16.0, 3.8, "" },
    { kRotateParamId, "Rotate", "ROT", -8.0, 8.0, 0.03, "" },
    { kWidthParamId, "Width", "WIDTH", 0.0, 16.0, 1.2, "" },
    { kFocusParamId, "Focus", "FOCUS", 0.1, 8.0, 1.5, "" },
    { kStereoParamId, "Stereo", "ST", 0.0, 1.0, 0.0, "pct" },
    { kDelayParamId, "Delay", "DLY", 5.0, 1800.0, 180.0, "ms" },
    { kFeedbackParamId, "Feedback", "FDBK", 0.0, 0.82, 0.35, "pct" },
    { kOrbitParamId, "Orbit", "ORBIT", -6.0, 6.0, 1.0, "" },
    { kDampParamId, "Damp", "DAMP", 0.0, 1.0, 0.35, "pct" },
    { kWetParamId, "Wet", "WET", 0.0, 1.0, 0.55, "pct" },
    { kGainParamId, "Gain", "GAIN", -60.0, 12.0, -3.5, "db" }
};
constexpr uint32_t kParamCount = static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0]));

const ParamDef* findParam(clap_id id)
{
    for (const auto& def : kParamDefs) if (def.id == id) return &def;
    return nullptr;
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kPosParamId: p.params.pos = static_cast<float>(std::clamp(value, 1.0, 16.0)); break;
    case kSpreadParamId: p.params.spread = static_cast<float>(std::clamp(value, 0.05, 16.0)); break;
    case kRotateParamId: p.params.rotate = static_cast<float>(std::clamp(value, -8.0, 8.0)); break;
    case kWidthParamId: p.params.width = static_cast<float>(std::clamp(value, 0.0, 16.0)); break;
    case kFocusParamId: p.params.focus = static_cast<float>(std::clamp(value, 0.1, 8.0)); break;
    case kStereoParamId: p.params.stereo = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDelayParamId: p.params.delayMs = static_cast<float>(std::clamp(value, 5.0, 1800.0)); break;
    case kFeedbackParamId: p.params.feedback = static_cast<float>(std::clamp(value, 0.0, 0.82)); break;
    case kOrbitParamId: p.params.orbit = static_cast<float>(std::clamp(value, -6.0, 6.0)); break;
    case kDampParamId: p.params.damp = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kWetParamId: p.params.wet = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: p.params.gainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    default: break;
    }
    p.dsp.setParams(p.params);
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
bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->inputL.assign(p->maxFrames, 0.0f);
    p->inputR.assign(p->maxFrames, 0.0f);
    p->output32.assign(kOutputChannels, std::vector<float>(p->maxFrames, 0.0f));
    p->outputPtrs.assign(kOutputChannels, nullptr);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) p->outputPtrs[ch] = p->output32[ch].data();
    if (!p->dsp.prepare(sampleRate, p->maxFrames)) return false;
    p->dsp.setParams(p->params);
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->dsp.reset(); }

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

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t* input = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = std::min(proc->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kOutputChannels) return CLAP_PROCESS_CONTINUE;
    for (uint32_t i = 0; i < frames; ++i) {
        if (input && input->channel_count > 0u && input->data32 && input->data32[0]) p->inputL[i] = input->data32[0][i];
        else if (input && input->channel_count > 0u && input->data64 && input->data64[0]) p->inputL[i] = static_cast<float>(input->data64[0][i]);
        else p->inputL[i] = 0.0f;
        if (input && input->channel_count > 1u && input->data32 && input->data32[1]) p->inputR[i] = input->data32[1][i];
        else if (input && input->channel_count > 1u && input->data64 && input->data64[1]) p->inputR[i] = static_cast<float>(input->data64[1][i]);
        else p->inputR[i] = p->inputL[i];
    }
    p->dsp.setParams(p->params);
    p->dsp.process(p->inputL.data(), p->inputR.data(), p->outputPtrs.data(), frames);
    float blockPeak = 0.0f;
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float v = p->output32[ch][i];
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = v;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(v);
            blockPeak = std::max(blockPeak, std::abs(v));
        }
    }
    for (uint32_t ch = kOutputChannels; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}
void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo In" : "16ch Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_STEREO : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Orbit Delay", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& p = self(plugin)->params;
    switch (id) {
    case kPosParamId: *value = p.pos; return true;
    case kSpreadParamId: *value = p.spread; return true;
    case kRotateParamId: *value = p.rotate; return true;
    case kWidthParamId: *value = p.width; return true;
    case kFocusParamId: *value = p.focus; return true;
    case kStereoParamId: *value = p.stereo; return true;
    case kDelayParamId: *value = p.delayMs; return true;
    case kFeedbackParamId: *value = p.feedback; return true;
    case kOrbitParamId: *value = p.orbit; return true;
    case kDampParamId: *value = p.damp; return true;
    case kWetParamId: *value = p.wet; return true;
    case kGainParamId: *value = p.gainDb; return true;
    default: return false;
    }
}
bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    const ParamDef* def = findParam(id);
    if (!def) return false;
    if (std::strcmp(def->unit, "ms") == 0) std::snprintf(display, size, "%.0f ms", value);
    else if (std::strcmp(def->unit, "db") == 0) std::snprintf(display, size, "%+.1f dB", value);
    else if (std::strcmp(def->unit, "pct") == 0) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else std::snprintf(display, size, "%.2f", value);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value) { if (!display || !value) return false; *value = std::atof(display); if (std::strchr(display, '%')) *value *= 0.01; return true; }
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {}; s.params = self(plugin)->params;
    return s3g::clap_state::writeAll(stream, &s, sizeof(s));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (!s3g::clap_state::readAll(stream, &s, sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin); p->params = s.params; p->dsp.setParams(p->params); return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
constexpr auto kOutputPanel = s3g::gui_layout::compactEffectOutputPanel(2u);
constexpr auto kDelayPanel =
    s3g::gui_layout::compactEffectLeftPanel(
        kOutputPanel, s3g::gui_layout::PanelRole::EventTiming, 4u);
constexpr auto kOrbitPanel =
    s3g::gui_layout::compactEffectRightPanel(
        s3g::gui_layout::PanelRole::Projection, 6u);
constexpr std::array kFirstColumnPanels { kOutputPanel, kDelayPanel };
constexpr std::array kSecondColumnPanels { kOrbitPanel };
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas,
    false));
constexpr uint32_t kOutputParamIndices[] { 11u, 10u };
constexpr uint32_t kDelayParamIndices[] { 6u, 7u, 8u, 9u };
constexpr uint32_t kOrbitParamIndices[] { 0u, 1u, 2u, 3u, 4u, 5u };

@interface S3GOrbitDelayView : NSView {
    void* _plugin;
    int _dragSlider;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GOrbitDelayView
- (id)initWithPlugin:(void*)plugin { self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; if (self) { _plugin = plugin; _dragSlider = -1; _timer = nil; std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT"); } return self; }
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(name, value, norm, y,
        panel.frame.x, panel.frame.width, attrs,
        s3g::clap_gui::softValueAttrs(), style);
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* lab = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    s3g::clap_gui::drawCompactEffectTitleBand(
        @"s3g EFFECT ORBIT DELAY",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(pk), titleBand, style);
    const auto drawPanel = [&](NSString* name,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.frame.x, panel.frame.y, panel.frame.width, panel.frame.height,
            style);
        s3g::clap_gui::drawPanelHeader(
            name, true, panel.frame.x, panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, lab, style);
    };
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"DELAY", kDelayPanel);
    drawPanel(@"ORBIT", kOrbitPanel);
    const auto drawParam = [&](uint32_t i, uint32_t row,
                               const s3g::gui_layout::Panel& panel) {
        double value = 0.0;
        paramsGetValue(&p->plugin, kParamDefs[i].id, &value);
        const double span = std::max(0.000001, kParamDefs[i].max - kParamDefs[i].min);
        const CGFloat norm = static_cast<CGFloat>((value - kParamDefs[i].min) / span);
        char text[32] {};
        paramsValueToText(&p->plugin, kParamDefs[i].id, value, text, sizeof(text));
        NSString* label = i == 11u ? @"OUT" : [NSString stringWithUTF8String:kParamDefs[i].label];
        [self drawRow:label value:[NSString stringWithUTF8String:text] norm:norm
            y:s3g::gui_layout::rowY(panel, row) panel:panel attrs:lab];
    };
    for (uint32_t row = 0u; row < std::size(kOutputParamIndices); ++row) {
        drawParam(kOutputParamIndices[row], row, kOutputPanel);
    }
    for (uint32_t row = 0u; row < std::size(kDelayParamIndices); ++row) {
        drawParam(kDelayParamIndices[row], row, kDelayPanel);
    }
    for (uint32_t row = 0u; row < std::size(kOrbitParamIndices); ++row) {
        drawParam(kOrbitParamIndices[row], row, kOrbitPanel);
    }
    [@"circulating 16ch echo cloud" drawAtPoint:NSMakePoint(
        kOrbitPanel.frame.x + 16.0,
        kOrbitPanel.frame.y + kOrbitPanel.frame.height + 12.0)
        withAttributes:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto* def = findParam(static_cast<clap_id>(_dragSlider));
    if (!def) return;
    const bool output = def->id == kGainParamId || def->id == kWetParamId;
    const bool delay = def->id >= kDelayParamId && def->id <= kDampParamId;
    const auto& panel = output ? kOutputPanel : (delay ? kDelayPanel : kOrbitPanel);
    const double x0 = s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double n = std::clamp((point.x - x0) / trackWidth, 0.0, 1.0);
    applyParam(*p, def->id, def->min + n * (def->max - def->min));
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Effect Orbit Delay", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kGainParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    const auto beginSlider = [&](clap_id paramId) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, paramId, &defaultValue)) {
            applyParam(*p, paramId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(paramId);
            [self updateSlider:pt];
        }
        [self setNeedsDisplay:YES];
    };
    const auto hitPanel = [&](const s3g::gui_layout::Panel& panel,
                              const uint32_t* indices, uint32_t count) {
        for (uint32_t row = 0u; row < count; ++row) {
            if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
                    s3g::gui_layout::sliderHitRect(panel, row)))) {
                beginSlider(kParamDefs[indices[row]].id);
                return true;
            }
        }
        return false;
    };
    if (hitPanel(kOutputPanel, kOutputParamIndices,
            static_cast<uint32_t>(std::size(kOutputParamIndices)))
        || hitPanel(kDelayPanel, kDelayParamIndices,
            static_cast<uint32_t>(std::size(kDelayParamIndices)))
        || hitPanel(kOrbitPanel, kOrbitParamIndices,
            static_cast<uint32_t>(std::size(kOrbitParamIndices)))) {
        return;
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GOrbitDelayView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3GOrbitDelayView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GOrbitDelayView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GOrbitDelayView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

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

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.orbit-delay",
    "s3g Effect Orbit Delay",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "2-in to 16-out orbiting delay cloud adapted from the ten_orbit_delay Gen patch.",
    features
};
const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
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
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
