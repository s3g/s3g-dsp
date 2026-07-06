#include "s3g_realtime.h"
#include "s3g_macro_delay.h"

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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kChannelCount = s3g::kMacroDelayChannels;
constexpr uint32_t kStateVersion = 1;

constexpr clap_id kTimeParamId = 1;
constexpr clap_id kFeedbackParamId = 2;
constexpr clap_id kToneParamId = 3;
constexpr clap_id kCharacterParamId = 4;
constexpr clap_id kSmearParamId = 5;
constexpr clap_id kSpreadParamId = 6;
constexpr clap_id kDeviationParamId = 7;
constexpr clap_id kSkewParamId = 8;
constexpr clap_id kMixParamId = 9;
constexpr clap_id kOutputParamId = 10;
constexpr clap_id kCenterParamId = 11;
constexpr clap_id kGlideParamId = 12;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::MacroDelayParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::MacroDelayParams params {};
    s3g::MacroDelay delay;
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool paramAffectsTail(clap_id id)
{
    switch (id) {
    case kTimeParamId:
    case kFeedbackParamId:
    case kSpreadParamId:
    case kDeviationParamId:
    case kSkewParamId:
    case kCenterParamId:
    case kGlideParamId:
        return true;
    default:
        return false;
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    const bool tailWasAffected = paramAffectsTail(id);
    switch (id) {
    case kTimeParamId: p.params.timeMs = static_cast<float>(std::clamp(value, 5.0, 2000.0)); break;
    case kFeedbackParamId: p.params.feedback = static_cast<float>(std::clamp(value, 0.0, 0.78)); break;
    case kToneParamId: p.params.tone = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kCharacterParamId: p.params.character = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSmearParamId: p.params.smear = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSpreadParamId: p.params.spread = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDeviationParamId: p.params.deviation = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSkewParamId: p.params.skew = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kCenterParamId: p.params.center = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGlideParamId: p.params.glideMs = static_cast<float>(std::clamp(value, 10.0, 2000.0)); break;
    case kMixParamId: p.params.mix = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    default: break;
    }
    p.delay.setParams(p.params);
    if (tailWasAffected && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
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
    p->maxFrames = maxFrames;
    p->delay.prepare(sampleRate, kChannelCount, 2.5);
    p->delay.setParams(p->params);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->delay.reset(); }

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

    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, kChannelCount });
    const uint32_t frames = proc->frames_count;

    if (output.data32) {
        s3g::clearAudioBufferFromChannel(output, 0, frames);
    }
    if (!input.data32 || !output.data32 || channels == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    std::array<float, kChannelCount> frameIn {};
    std::array<float, kChannelCount> frameOut {};
    p->delay.setParams(p->params);

    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            frameIn[ch] = input.data32[ch] ? input.data32[ch][i] : 0.0f;
        }
        p->delay.processFrame(frameIn.data(), frameOut.data());
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (output.data32[ch]) {
                output.data32[ch][i] = frameOut[ch];
            }
        }
    }
    s3g::clearAudioBufferFromChannel(output, channels, frames);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "8ch In" : "8ch Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kTimeParamId, "Time", 5.0, 2000.0, 260.0 },
    { kFeedbackParamId, "Feedback", 0.0, 0.78, 0.32 },
    { kToneParamId, "Tone", 0.0, 1.0, 0.62 },
    { kCharacterParamId, "Character", 0.0, 1.0, 0.24 },
    { kSmearParamId, "Smear", 0.0, 1.0, 0.0 },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.0 },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.0 },
    { kSkewParamId, "Skew", -1.0, 1.0, 0.0 },
    { kCenterParamId, "Center", 0.0, 1.0, 0.5 },
    { kGlideParamId, "Glide", 10.0, 2000.0, 250.0 },
    { kMixParamId, "Mix", 0.0, 1.0, 0.35 },
    { kOutputParamId, "Output", -60.0, 12.0, 0.0 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Macro Delay", sizeof(info->module));
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
    case kTimeParamId: *value = p.timeMs; return true;
    case kFeedbackParamId: *value = p.feedback; return true;
    case kToneParamId: *value = p.tone; return true;
    case kCharacterParamId: *value = p.character; return true;
    case kSmearParamId: *value = p.smear; return true;
    case kSpreadParamId: *value = p.spread; return true;
    case kDeviationParamId: *value = p.deviation; return true;
    case kSkewParamId: *value = p.skew; return true;
    case kCenterParamId: *value = p.center; return true;
    case kGlideParamId: *value = p.glideMs; return true;
    case kMixParamId: *value = p.mix; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kTimeParamId) std::snprintf(display, size, "%.1f ms", value);
    else if (id == kGlideParamId) std::snprintf(display, size, "%.0f ms", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kFeedbackParamId || id == kToneParamId || id == kCharacterParamId || id == kSmearParamId || id == kSpreadParamId || id == kDeviationParamId || id == kCenterParamId || id == kMixParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else std::snprintf(display, size, "%+.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {};
    s.params = self(plugin)->params;
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = s.params;
    p->delay.setParams(p->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p) return 0u;
    const auto params = p->params;
    const double spreadFactor = std::pow(2.0, std::clamp(static_cast<double>(params.spread), 0.0, 1.0));
    const double deviationFactor = std::pow(2.0, std::clamp(static_cast<double>(params.deviation), 0.0, 1.0) * 0.5);
    const double skewFactor = std::pow(2.0, std::abs(std::clamp(static_cast<double>(params.skew), -1.0, 1.0)));
    const double maxDelaySeconds = std::clamp(
        static_cast<double>(params.timeMs) * 0.001 * spreadFactor * deviationFactor * skewFactor,
        0.005,
        2.5);
    const double feedback = std::clamp(static_cast<double>(params.feedback) * (1.0 - params.deviation * 0.08), 0.0, 0.95);
    const double repeatsToMinus60 = feedback > 0.001 ? std::ceil(std::log(0.001) / std::log(feedback)) : 1.0;
    const double tailSeconds = std::clamp(maxDelaySeconds * repeatsToMinus60 + 0.5, 0.5, 90.0);
    return static_cast<uint32_t>(std::ceil(tailSeconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
@interface S3GMacroDelayView : NSView { void* _plugin; int _dragSlider; NSTimer* _timer; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawRelationshipPreview:(const s3g::MacroDelayParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* udColor(int rgb) { return s3g::clap_gui::color(rgb); }

@implementation S3GMacroDelayView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 760, 420)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _timer = nil;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, small, style, 64, 166, 340);
}
- (void)drawRelationshipPreview:(const s3g::MacroDelayParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [udColor(0x111111) setFill]; NSRectFill(rect);
    [udColor(0x444444) setStroke]; NSFrameRect(rect);
    const CGFloat baseY = rect.origin.y + 32.0;
    const CGFloat rowH = (rect.size.height - 46.0) / static_cast<CGFloat>(std::max<uint32_t>(1u, s3g::kMacroDelayChannels - 1u));
    const CGFloat labelX = rect.origin.x + 10.0;
    const CGFloat barX = rect.origin.x + 48.0;
    const CGFloat barW = rect.size.width - 66.0;
    std::array<float, s3g::kMacroDelayChannels> ratios {};
    float maxRatio = 0.001f;
    for (uint32_t ch = 0; ch < s3g::kMacroDelayChannels; ++ch) {
        const float u = static_cast<float>(ch) / static_cast<float>(std::max<uint32_t>(1u, s3g::kMacroDelayChannels - 1u));
        const float centered = std::clamp((u - params.center) * 2.0f, -1.0f, 1.0f);
        uint32_t x = ch * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        x = (x >> 22u) ^ x;
        const float dev = static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
        ratios[ch] = std::pow(2.0f, centered * params.spread) * std::pow(2.0f, dev * params.deviation * 0.5f) * std::pow(2.0f, params.skew * u);
        maxRatio = std::max(maxRatio, ratios[ch]);
    }
    for (uint32_t ch = 0; ch < s3g::kMacroDelayChannels; ++ch) {
        const CGFloat y = baseY + static_cast<CGFloat>(ch) * rowH;
        [[NSString stringWithFormat:@"L%u", ch + 1u] drawAtPoint:NSMakePoint(labelX, y - 4.0) withAttributes:attrs];
        NSRect track = NSMakeRect(barX, y, barW, 6.0);
        [udColor(0x171717) setFill]; NSRectFill(track);
        [udColor(0x333333) setStroke]; NSFrameRect(track);
        NSRect fill = NSInsetRect(track, 1.0, 1.0);
        fill.size.width = std::max<CGFloat>(1.0, fill.size.width * ratios[ch] / maxRatio);
        [udColor(0xb8b8b8) setFill]; NSRectFill(fill);
    }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* bold = [NSFont fontWithName:@"Menlo-Bold" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightBold];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10.5] ?: [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    NSDictionary* lab = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:bold };
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSDictionary* titleAttrs = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:titleFont };
    [@"s3g MACRO DELAY" drawAtPoint:NSMakePoint(18,14) withAttributes:titleAttrs];
    [@"8CH RELATIONSHIP DELAY" drawAtPoint:NSMakePoint(572,14) withAttributes:small];

    s3g::clap_gui::drawPanelFrame(18, 42, 352, 188, style);
    s3g::clap_gui::drawPanelHeader(@"ENGINE", true, 18, 42, 352, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(18, 242, 352, 150, style);
    s3g::clap_gui::drawPanelHeader(@"LANE DELAY REL", true, 18, 242, 352, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(388, 42, 354, 258, style);
    s3g::clap_gui::drawPanelHeader(@"RELATIONSHIPS", true, 388, 42, 354, 21, lab, style);

    const auto& prm = p->params;
    [self drawSlider:@"TIME" value:[NSString stringWithFormat:@"%.0f", prm.timeMs] norm:(prm.timeMs - 5.0f) / 1995.0f y:78 attrs:small small:small];
    [self drawSlider:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", prm.feedback * 100.0f] norm:prm.feedback / 0.78f y:104 attrs:small small:small];
    [self drawSlider:@"TONE" value:[NSString stringWithFormat:@"%.0f%%", prm.tone * 100.0f] norm:prm.tone y:130 attrs:small small:small];
    [self drawSlider:@"CHR" value:[NSString stringWithFormat:@"%.0f%%", prm.character * 100.0f] norm:prm.character y:156 attrs:small small:small];
    [self drawSlider:@"SMR" value:[NSString stringWithFormat:@"%.0f%%", prm.smear * 100.0f] norm:prm.smear y:182 attrs:small small:small];

    s3g::clap_gui::drawSlider(@"SPRD", [NSString stringWithFormat:@"%.0f%%", prm.spread * 100.0f], prm.spread, 78, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"DEV", [NSString stringWithFormat:@"%.0f%%", prm.deviation * 100.0f], prm.deviation, 104, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"SKW", [NSString stringWithFormat:@"%+.2f", prm.skew], (prm.skew + 1.0f) * 0.5f, 130, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"CTR", [NSString stringWithFormat:@"%.0f%%", prm.center * 100.0f], prm.center, 156, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"GLD", [NSString stringWithFormat:@"%.0f", prm.glideMs], (prm.glideMs - 10.0f) / 1990.0f, 182, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"MIX", [NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f], prm.mix, 208, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"OUT", [NSString stringWithFormat:@"%+.1f", prm.outputGainDb], (prm.outputGainDb + 60.0f) / 72.0f, 234, small, small, style, 398, 500, 674);

    [self drawRelationshipPreview:prm rect:NSMakeRect(30,274,330,106) attrs:small];
    [@"same processor per channel" drawAtPoint:NSMakePoint(398,322) withAttributes:small];
    [@"relationship changes glide internally" drawAtPoint:NSMakePoint(398,340) withAttributes:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider >= 1 && _dragSlider <= 5) {
        const double n = std::clamp((point.x - 166.0) / 150.0, 0.0, 1.0);
        switch (_dragSlider) {
        case 1: applyParam(*p, kTimeParamId, 5.0 + n * 1995.0); break;
        case 2: applyParam(*p, kFeedbackParamId, n * 0.78); break;
        case 3: applyParam(*p, kToneParamId, n); break;
        case 4: applyParam(*p, kCharacterParamId, n); break;
        case 5: applyParam(*p, kSmearParamId, n); break;
        default: break;
        }
    } else if (_dragSlider >= 6 && _dragSlider <= 12) {
        const double n = std::clamp((point.x - 500.0) / 150.0, 0.0, 1.0);
        switch (_dragSlider) {
        case 6: applyParam(*p, kSpreadParamId, n); break;
        case 7: applyParam(*p, kDeviationParamId, n); break;
        case 8: applyParam(*p, kSkewParamId, -1.0 + n * 2.0); break;
        case 9: applyParam(*p, kCenterParamId, n); break;
        case 10: applyParam(*p, kGlideParamId, 10.0 + n * 1990.0); break;
        case 11: applyParam(*p, kMixParamId, n); break;
        case 12: applyParam(*p, kOutputParamId, -60.0 + n * 72.0); break;
        default: break;
        }
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat engineRows[] = { 78, 104, 130, 156, 182 };
    const CGFloat relationshipRows[] = { 78, 104, 130, 156, 182, 208, 234 };
    for (int i = 0; i < 5; ++i) {
        if (NSPointInRect(pt, NSMakeRect(60, engineRows[i] - 8, 300, 24))) {
            _dragSlider = i + 1;
            [self updateSlider:pt];
            return;
        }
    }
    for (int i = 0; i < 7; ++i) {
        if (NSPointInRect(pt, NSMakeRect(394, relationshipRows[i] - 8, 300, 24))) {
            _dragSlider = i + 6;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GMacroDelayView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3GMacroDelayView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 760; *h = 420; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,760,420)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GMacroDelayView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GMacroDelayView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.macro-delay-8ch",
    "s3g Macro Delay 8ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "8-channel macro delay with shared effect trims and smoothed channel-relationship controls.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostTail = host && host->get_extension ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL)) : nullptr;
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
