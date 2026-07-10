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
@interface S3GShardScatterView : NSView { void* _plugin; int _dragSlider; NSTimer* _timer; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GShardScatterView
- (id)initWithPlugin:(void*)plugin { self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; if (self) { _plugin = plugin; _dragSlider = -1; _timer = nil; } return self; }
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, small, style, x, x + 94, x + 266, 150);
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* bold = [NSFont fontWithName:@"Menlo-Bold" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightBold];
    NSDictionary* lab = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:bold };
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    [@"s3g SHARD SCATTER" drawAtPoint:NSMakePoint(18,14) withAttributes:lab];
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PK %+4.1f", 20.0 * std::log10(std::max(0.000001f, pk))] drawAtPoint:NSMakePoint(596,14) withAttributes:small];
    [@"2>16" drawAtPoint:NSMakePoint(704,14) withAttributes:small];
    s3g::clap_gui::drawPanelFrame(18, 42, 354, 286, style);
    s3g::clap_gui::drawPanelHeader(@"SHARDS", true, 18, 42, 354, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(388, 42, 354, 286, style);
    s3g::clap_gui::drawPanelHeader(@"RING / OUTPUT", true, 388, 42, 354, 21, lab, style);
    const auto& prm = p->params;
    [self drawRow:@"DENS" value:[NSString stringWithFormat:@"%.1f", static_cast<double>(prm.density)] norm:(prm.density - 0.2f) / 23.8f x:36 y:82 attrs:small small:small];
    [self drawRow:@"GRAIN" value:[NSString stringWithFormat:@"%.0f", static_cast<double>(prm.grainMs)] norm:(prm.grainMs - 20.0f) / 880.0f x:36 y:116 attrs:small small:small];
    [self drawRow:@"GUARD" value:[NSString stringWithFormat:@"%.0f", static_cast<double>(prm.guardMs)] norm:(prm.guardMs - 20.0f) / 1780.0f x:36 y:150 attrs:small small:small];
    [self drawRow:@"SCAT" value:[NSString stringWithFormat:@"%.0f", static_cast<double>(prm.scatterMs)] norm:prm.scatterMs / 2500.0f x:36 y:184 attrs:small small:small];
    [self drawRow:@"PITCH" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(prm.pitch)] norm:(prm.pitch + 2.0f) * 0.25f x:36 y:218 attrs:small small:small];
    [self drawRow:@"PSPR" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(prm.pitchSpread)] norm:prm.pitchSpread / 1.5f x:36 y:252 attrs:small small:small];
    [self drawRow:@"ROT" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(prm.rotate)] norm:(prm.rotate + 4.0f) * 0.125f x:36 y:286 attrs:small small:small];
    [self drawRow:@"WIDTH" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.width * 100.0f)] norm:prm.width x:406 y:82 attrs:small small:small];
    [self drawRow:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.feedback * 100.0f)] norm:prm.feedback / 0.72f x:406 y:116 attrs:small small:small];
    [self drawRow:@"FRZ" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.freeze * 100.0f)] norm:prm.freeze x:406 y:150 attrs:small small:small];
    [self drawRow:@"DRY" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.dry * 100.0f)] norm:prm.dry x:406 y:184 attrs:small small:small];
    [self drawRow:@"WET" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.wet * 100.0f)] norm:prm.wet x:406 y:218 attrs:small small:small];
    [self drawRow:@"GAIN" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(prm.gainDb)] norm:(prm.gainDb + 60.0f) / 72.0f x:406 y:252 attrs:small small:small];
    [self drawRow:@"STEREO" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.stereo * 100.0f)] norm:prm.stereo x:406 y:286 attrs:small small:small];
    [@"live buffer shard ring" drawAtPoint:NSMakePoint(406, 334) withAttributes:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const bool left = _dragSlider >= 1 && _dragSlider <= 7;
    const double x0 = left ? 130.0 : 500.0;
    const double n = std::clamp((point.x - x0) / 150.0, 0.0, 1.0);
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
    const CGFloat rows[] = {82,116,150,184,218,252,286};
    for (int i = 0; i < 7; ++i) {
        if (NSPointInRect(pt, NSMakeRect(32, rows[i] - 9, 330, 24))) { _dragSlider = i + 1; [self updateSlider:pt]; return; }
        if (NSPointInRect(pt, NSMakeRect(402, rows[i] - 9, 330, 24))) { _dragSlider = i + 8; [self updateSlider:pt]; return; }
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GShardScatterView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3GShardScatterView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiWidth,kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GShardScatterView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GShardScatterView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    "s3g Shard Scatter",
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
