#include "s3g_realtime.h"
#include "s3g_shard_scatter.h"

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
constexpr uint32_t kOutputChannels = s3g::kShardScatterChannels;
constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kGuiWidth = 760;
constexpr uint32_t kGuiHeight = 376;

constexpr clap_id kDensityParamId = 1;
constexpr clap_id kGrainParamId = 2;
constexpr clap_id kGuardParamId = 3;
constexpr clap_id kScatterParamId = 4;
constexpr clap_id kPitchParamId = 5;
constexpr clap_id kPitchSpreadParamId = 6;
constexpr clap_id kRotateParamId = 7;
constexpr clap_id kWidthParamId = 8;
constexpr clap_id kFeedbackParamId = 9;
constexpr clap_id kFreezeParamId = 10;
constexpr clap_id kDryParamId = 11;
constexpr clap_id kWetParamId = 12;
constexpr clap_id kGainParamId = 13;
constexpr clap_id kStereoParamId = 14;

struct SavedState { uint32_t version = kStateVersion; s3g::ShardScatterParams params {}; };
struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::ShardScatterParams params {};
    s3g::ShardScatter scatter;
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

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kDensityParamId: p.params.density = static_cast<float>(std::clamp(value, 0.2, 24.0)); break;
    case kGrainParamId: p.params.grainMs = static_cast<float>(std::clamp(value, 20.0, 900.0)); break;
    case kGuardParamId: p.params.guardMs = static_cast<float>(std::clamp(value, 20.0, 1800.0)); break;
    case kScatterParamId: p.params.scatterMs = static_cast<float>(std::clamp(value, 0.0, 2500.0)); break;
    case kPitchParamId: p.params.pitch = static_cast<float>(std::clamp(value, -2.0, 2.0)); break;
    case kPitchSpreadParamId: p.params.pitchSpread = static_cast<float>(std::clamp(value, 0.0, 1.5)); break;
    case kRotateParamId: p.params.rotate = static_cast<float>(std::clamp(value, -4.0, 4.0)); break;
    case kWidthParamId: p.params.width = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFeedbackParamId: p.params.feedback = static_cast<float>(std::clamp(value, 0.0, 0.72)); break;
    case kFreezeParamId: p.params.freeze = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDryParamId: p.params.dry = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kWetParamId: p.params.wet = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: p.params.gainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    case kStereoParamId: p.params.stereo = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    default: break;
    }
    p.scatter.setParams(p.params);
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
    if (!p->scatter.prepare(sampleRate, p->maxFrames)) return false;
    p->scatter.setParams(p->params);
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->scatter.reset(); }

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
    p->scatter.setParams(p->params);
    p->scatter.process(p->inputL.data(), p->inputR.data(), p->outputPtrs.data(), frames);
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
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo In" : "16ch Shards");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_STEREO : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kDensityParamId, "Density", 0.2, 24.0, 5.0 },
    { kGrainParamId, "Grain", 20.0, 900.0, 180.0 },
    { kGuardParamId, "Guard", 20.0, 1800.0, 260.0 },
    { kScatterParamId, "Scatter", 0.0, 2500.0, 700.0 },
    { kPitchParamId, "Pitch", -2.0, 2.0, 1.0 },
    { kPitchSpreadParamId, "Pitch Spread", 0.0, 1.5, 0.15 },
    { kRotateParamId, "Rotate", -4.0, 4.0, 0.18 },
    { kWidthParamId, "Width", 0.0, 1.0, 0.18 },
    { kFeedbackParamId, "Feedback", 0.0, 0.72, 0.12 },
    { kFreezeParamId, "Freeze", 0.0, 1.0, 0.0 },
    { kDryParamId, "Dry", 0.0, 1.0, 0.08 },
    { kWetParamId, "Wet", 0.0, 1.0, 0.9 },
    { kGainParamId, "Gain", -60.0, 12.0, -2.5 },
    { kStereoParamId, "Stereo", 0.0, 1.0, 0.0 },
};
uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Shard Scatter", sizeof(info->module));
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
    case kDensityParamId: *value = p.density; return true;
    case kGrainParamId: *value = p.grainMs; return true;
    case kGuardParamId: *value = p.guardMs; return true;
    case kScatterParamId: *value = p.scatterMs; return true;
    case kPitchParamId: *value = p.pitch; return true;
    case kPitchSpreadParamId: *value = p.pitchSpread; return true;
    case kRotateParamId: *value = p.rotate; return true;
    case kWidthParamId: *value = p.width; return true;
    case kFeedbackParamId: *value = p.feedback; return true;
    case kFreezeParamId: *value = p.freeze; return true;
    case kDryParamId: *value = p.dry; return true;
    case kWetParamId: *value = p.wet; return true;
    case kGainParamId: *value = p.gainDb; return true;
    case kStereoParamId: *value = p.stereo; return true;
    default: return false;
    }
}
bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kDensityParamId) std::snprintf(display, size, "%.1f/s", value);
    else if (id == kGrainParamId || id == kGuardParamId || id == kScatterParamId) std::snprintf(display, size, "%.0f ms", value);
    else if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kPitchParamId || id == kRotateParamId || id == kPitchSpreadParamId) std::snprintf(display, size, "%+.2f", value);
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value) { if (!display || !value) return false; *value = std::atof(display); return true; }
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {}; s.params = self(plugin)->params;
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin); p->params = s.params; p->scatter.setParams(p->params); return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };


} // namespace

#if defined(__APPLE__)
constexpr auto kOutputPanel = s3g::gui_layout::compactEffectOutputPanel(5u);
constexpr auto kRingPanel =
    s3g::gui_layout::compactEffectLeftPanel(
        kOutputPanel, s3g::gui_layout::PanelRole::Engine, 2u);
constexpr auto kShardsPanel =
    s3g::gui_layout::compactEffectRightPanel(
        s3g::gui_layout::PanelRole::EventTiming, 7u);
constexpr std::array kFirstColumnPanels { kOutputPanel, kRingPanel };
constexpr std::array kSecondColumnPanels { kShardsPanel };
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnPanels, s3g::gui_layout::kCompactEffectFamilyLayout.canvas,
    false));

@interface S3GShardScatterView : NSView {
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

@implementation S3GShardScatterView
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
        @"s3g EFFECT SHARD SCATTER",
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
    drawPanel(@"RING", kRingPanel);
    drawPanel(@"SHARDS", kShardsPanel);
    const auto& prm = p->params;
    [self drawRow:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB", static_cast<double>(prm.gainDb)] norm:(prm.gainDb + 60.0f) / 72.0f y:s3g::gui_layout::rowY(kOutputPanel, 0u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"DRY" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.dry * 100.0f)] norm:prm.dry y:s3g::gui_layout::rowY(kOutputPanel, 1u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"WET" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.wet * 100.0f)] norm:prm.wet y:s3g::gui_layout::rowY(kOutputPanel, 2u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"ST" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.stereo * 100.0f)] norm:prm.stereo y:s3g::gui_layout::rowY(kOutputPanel, 3u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"WIDTH" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.width * 100.0f)] norm:prm.width y:s3g::gui_layout::rowY(kOutputPanel, 4u) panel:kOutputPanel attrs:lab];
    [self drawRow:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.feedback * 100.0f)] norm:prm.feedback / 0.72f y:s3g::gui_layout::rowY(kRingPanel, 0u) panel:kRingPanel attrs:lab];
    [self drawRow:@"FRZ" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.freeze * 100.0f)] norm:prm.freeze y:s3g::gui_layout::rowY(kRingPanel, 1u) panel:kRingPanel attrs:lab];
    [self drawRow:@"DENS" value:[NSString stringWithFormat:@"%.1f", static_cast<double>(prm.density)] norm:(prm.density - 0.2f) / 23.8f y:s3g::gui_layout::rowY(kShardsPanel, 0u) panel:kShardsPanel attrs:lab];
    [self drawRow:@"GRAIN" value:[NSString stringWithFormat:@"%.0f ms", static_cast<double>(prm.grainMs)] norm:(prm.grainMs - 20.0f) / 880.0f y:s3g::gui_layout::rowY(kShardsPanel, 1u) panel:kShardsPanel attrs:lab];
    [self drawRow:@"GUARD" value:[NSString stringWithFormat:@"%.0f ms", static_cast<double>(prm.guardMs)] norm:(prm.guardMs - 20.0f) / 1780.0f y:s3g::gui_layout::rowY(kShardsPanel, 2u) panel:kShardsPanel attrs:lab];
    [self drawRow:@"SCAT" value:[NSString stringWithFormat:@"%.0f ms", static_cast<double>(prm.scatterMs)] norm:prm.scatterMs / 2500.0f y:s3g::gui_layout::rowY(kShardsPanel, 3u) panel:kShardsPanel attrs:lab];
    [self drawRow:@"PITCH" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(prm.pitch)] norm:(prm.pitch + 2.0f) * 0.25f y:s3g::gui_layout::rowY(kShardsPanel, 4u) panel:kShardsPanel attrs:lab];
    [self drawRow:@"PSPR" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(prm.pitchSpread)] norm:prm.pitchSpread / 1.5f y:s3g::gui_layout::rowY(kShardsPanel, 5u) panel:kShardsPanel attrs:lab];
    [self drawRow:@"ROT" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(prm.rotate)] norm:(prm.rotate + 4.0f) * 0.125f y:s3g::gui_layout::rowY(kShardsPanel, 6u) panel:kShardsPanel attrs:lab];
    [@"live buffer shard ring" drawAtPoint:NSMakePoint(
        kShardsPanel.frame.x + 16.0,
        kShardsPanel.frame.y + kShardsPanel.frame.height + 12.0)
        withAttributes:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const bool output = _dragSlider == 13 || _dragSlider == 11
        || _dragSlider == 12 || _dragSlider == 14 || _dragSlider == 8;
    const bool ring = _dragSlider == 9 || _dragSlider == 10;
    const auto& panel = output ? kOutputPanel : (ring ? kRingPanel : kShardsPanel);
    const double x0 = s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double n = std::clamp((point.x - x0) / trackWidth, 0.0, 1.0);
    switch (_dragSlider) {
    case 1: applyParam(*p, kDensityParamId, 0.2 + n * 23.8); break;
    case 2: applyParam(*p, kGrainParamId, 20.0 + n * 880.0); break;
    case 3: applyParam(*p, kGuardParamId, 20.0 + n * 1780.0); break;
    case 4: applyParam(*p, kScatterParamId, n * 2500.0); break;
    case 5: applyParam(*p, kPitchParamId, -2.0 + n * 4.0); break;
    case 6: applyParam(*p, kPitchSpreadParamId, n * 1.5); break;
    case 7: applyParam(*p, kRotateParamId, -4.0 + n * 8.0); break;
    case 8: applyParam(*p, kWidthParamId, n); break;
    case 9: applyParam(*p, kFeedbackParamId, n * 0.72); break;
    case 10: applyParam(*p, kFreezeParamId, n); break;
    case 11: applyParam(*p, kDryParamId, n); break;
    case 12: applyParam(*p, kWetParamId, n); break;
    case 13: applyParam(*p, kGainParamId, -60.0 + n * 72.0); break;
    case 14: applyParam(*p, kStereoParamId, n); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Effect Shard Scatter", titleBand,
            _titlePresetName, sizeof(_titlePresetName))) {
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
    const clap_id outputIds[] {
        kGainParamId, kDryParamId, kWetParamId, kStereoParamId, kWidthParamId
    };
    const clap_id ringIds[] { kFeedbackParamId, kFreezeParamId };
    const clap_id shardIds[] {
        kDensityParamId, kGrainParamId, kGuardParamId, kScatterParamId,
        kPitchParamId, kPitchSpreadParamId, kRotateParamId
    };
    const auto hitPanel = [&](const s3g::gui_layout::Panel& panel,
                              const clap_id* ids, uint32_t count) {
        for (uint32_t row = 0u; row < count; ++row) {
            if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
                    s3g::gui_layout::sliderHitRect(panel, row)))) {
                beginSlider(ids[row]);
                return true;
            }
        }
        return false;
    };
    if (hitPanel(kOutputPanel, outputIds, 5u)
        || hitPanel(kRingPanel, ringIds, 2u)
        || hitPanel(kShardsPanel, shardIds, 7u)) {
        return;
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GShardScatterView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3GShardScatterView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GShardScatterView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GShardScatterView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    "org.s3g.s3g-dsp.shard-scatter",
    "s3g Effect Shard Scatter",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "2-in to 16-out live-buffer shard scatter adapted from the ten_shard_scatter Gen patch.",
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
