#include "s3g_realtime.h"
#include "s3g_macro_delay.h"
#include "../common/s3g_objc_class_name.h"
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
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

#ifndef S3G_MACRO_DELAY_CHANNEL_COUNT
#define S3G_MACRO_DELAY_CHANNEL_COUNT 8
#endif

#define S3G_MACRO_DELAY_VIEW_CLASS \
    S3G_OBJC_CLASS_JOIN(S3GMacroDelayView, S3G_MACRO_DELAY_CHANNEL_COUNT)

constexpr uint32_t kChannelCount = S3G_MACRO_DELAY_CHANNEL_COUNT;
static_assert(kChannelCount > 0 && kChannelCount <= s3g::kMacroDelayChannels,
    "S3G_MACRO_DELAY_CHANNEL_COUNT must be in the supported Macro Delay range.");
constexpr bool kPassExtraHostChannels = kChannelCount >= 24;

#ifndef S3G_MACRO_DELAY_PLUGIN_ID
#define S3G_MACRO_DELAY_PLUGIN_ID "org.s3g.s3g-dsp.macro-delay-8ch"
#endif
#ifndef S3G_MACRO_DELAY_PLUGIN_NAME
#define S3G_MACRO_DELAY_PLUGIN_NAME "s3g Macro Delay 8"
#endif
#ifndef S3G_MACRO_DELAY_DESCRIPTION
#define S3G_MACRO_DELAY_DESCRIPTION "8-channel macro delay with shared effect trims and smoothed channel-relationship controls."
#endif

constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kGuiWidth = 760;
constexpr uint32_t kGuiHeight = 496;

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
    std::array<float, kChannelCount> frameIn {};
    std::array<float, kChannelCount> frameOut {};
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
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

void finishExtraChannels(const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = channels; ch < output.channel_count; ++ch) {
        if constexpr (kPassExtraHostChannels) {
            if (ch < input.channel_count) {
                if (output.data32 && output.data32[ch]) {
                    if (input.data32 && input.data32[ch]) {
                        std::memcpy(output.data32[ch], input.data32[ch], sizeof(float) * frames);
                        continue;
                    }
                    if (input.data64 && input.data64[ch]) {
                        for (uint32_t i = 0; i < frames; ++i) output.data32[ch][i] = static_cast<float>(input.data64[ch][i]);
                        continue;
                    }
                }
                if (output.data64 && output.data64[ch]) {
                    if (input.data64 && input.data64[ch]) {
                        std::memcpy(output.data64[ch], input.data64[ch], sizeof(double) * frames);
                        continue;
                    }
                    if (input.data32 && input.data32[ch]) {
                        for (uint32_t i = 0; i < frames; ++i) output.data64[ch][i] = static_cast<double>(input.data32[ch][i]);
                        continue;
                    }
                }
            }
        }
        if (output.data32 && output.data32[ch]) {
            std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        }
        if (output.data64 && output.data64[ch]) {
            std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
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

    if (channels == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_CONTINUE;
    }

    p->delay.setParams(p->params);
    float blockPeak = 0.0f;

    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (input.data32 && input.data32[ch]) {
                p->frameIn[ch] = input.data32[ch][i];
            } else if (input.data64 && input.data64[ch]) {
                p->frameIn[ch] = static_cast<float>(input.data64[ch][i]);
            } else {
                p->frameIn[ch] = 0.0f;
            }
        }
        for (uint32_t ch = channels; ch < kChannelCount; ++ch) {
            p->frameIn[ch] = 0.0f;
        }
        p->delay.processFrame(p->frameIn.data(), p->frameOut.data());
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (output.data32 && output.data32[ch]) {
                output.data32[ch][i] = p->frameOut[ch];
            }
            if (output.data64 && output.data64[ch]) {
                output.data64[ch][i] = static_cast<double>(p->frameOut[ch]);
            }
            blockPeak = std::max(blockPeak, std::fabs(p->frameOut[ch]));
        }
    }
    finishExtraChannels(input, output, channels, frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "%uch %s", kChannelCount, isInput ? "In" : "Out");
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
    { kOutputParamId, "Out", -60.0, 12.0, 0.0 },
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

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    if (id == kFeedbackParamId || id == kToneParamId || id == kCharacterParamId
        || id == kSmearParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kCenterParamId || id == kMixParamId) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {};
    s.params = self(plugin)->params;
    return s3g::clap_state::writeAll(stream, &s, sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (!s3g::clap_state::readAll(stream, &s, sizeof(s)) || s.version != kStateVersion) return false;
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
@interface S3G_MACRO_DELAY_VIEW_CLASS : NSView { void* _plugin; int _dragSlider; NSTimer* _timer; char _titlePresetName[64]; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs;
- (void)drawRelationshipPreview:(const s3g::MacroDelayParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* udColor(int rgb) { return s3g::clap_gui::color(rgb); }

@implementation S3G_MACRO_DELAY_VIEW_CLASS
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _timer = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y, panel.frame.x, panel.frame.width,
        attrs, attrs, style);
}
- (void)drawRelationshipPreview:(const s3g::MacroDelayParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [udColor(0x111111) setFill]; NSRectFill(rect);
    [udColor(0x444444) setStroke]; NSFrameRect(rect);
    const CGFloat baseY = rect.origin.y + 32.0;
    const CGFloat rowH = (rect.size.height - 46.0) / static_cast<CGFloat>(std::max<uint32_t>(1u, kChannelCount - 1u));
    const CGFloat labelX = rect.origin.x + 10.0;
    const CGFloat barX = rect.origin.x + 48.0;
    const CGFloat barW = rect.size.width - 66.0;
    std::array<float, kChannelCount> ratios {};
    float maxRatio = 0.001f;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const float u = static_cast<float>(ch) / static_cast<float>(std::max<uint32_t>(1u, kChannelCount - 1u));
        const float centered = std::clamp((u - params.center) * 2.0f, -1.0f, 1.0f);
        uint32_t x = ch * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        x = (x >> 22u) ^ x;
        const float dev = static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
        ratios[ch] = std::pow(2.0f, centered * params.spread) * std::pow(2.0f, dev * params.deviation * 0.5f) * std::pow(2.0f, params.skew * u);
        maxRatio = std::max(maxRatio, ratios[ch]);
    }
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
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
    NSDictionary* lab = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    const auto& family = s3g::gui_layout::kMacroFamilyLayout;
    const auto titleBand = s3g::gui_layout::macroTitleBand(family.canvas);
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    s3g::clap_gui::drawMacroTitleBand(
        [NSString stringWithFormat:@"s3g MACRO DELAY %uCH", kChannelCount],
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(pk), titleBand, style);

    const auto drawPanel = [&](NSString* name, const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.frame.x, panel.frame.y, panel.frame.width, panel.frame.height, style);
        s3g::clap_gui::drawPanelHeader(
            name, true, panel.frame.x, panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, lab, style);
    };
    drawPanel(@"OUTPUT", family.output);
    drawPanel(@"ENGINE", family.delayEngine);
    drawPanel(@"RELATIONSHIPS", family.delayRelationships);
    drawPanel(@"LANE DELAY REL", family.preview);

    const auto& prm = p->params;
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f dB", prm.outputGainDb] norm:(prm.outputGainDb + 60.0f) / 72.0f y:s3g::gui_layout::rowY(family.output, 0u) panel:family.output attrs:small];
    [self drawSlider:@"MIX" value:[NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f] norm:prm.mix y:s3g::gui_layout::rowY(family.output, 1u) panel:family.output attrs:small];
    [self drawSlider:@"TIME" value:[NSString stringWithFormat:@"%.0f ms", prm.timeMs] norm:(prm.timeMs - 5.0f) / 1995.0f y:s3g::gui_layout::rowY(family.delayEngine, 0u) panel:family.delayEngine attrs:small];
    [self drawSlider:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", prm.feedback * 100.0f] norm:prm.feedback / 0.78f y:s3g::gui_layout::rowY(family.delayEngine, 1u) panel:family.delayEngine attrs:small];
    [self drawSlider:@"TONE" value:[NSString stringWithFormat:@"%.0f%%", prm.tone * 100.0f] norm:prm.tone y:s3g::gui_layout::rowY(family.delayEngine, 2u) panel:family.delayEngine attrs:small];
    [self drawSlider:@"CHR" value:[NSString stringWithFormat:@"%.0f%%", prm.character * 100.0f] norm:prm.character y:s3g::gui_layout::rowY(family.delayEngine, 3u) panel:family.delayEngine attrs:small];
    [self drawSlider:@"SMR" value:[NSString stringWithFormat:@"%.0f%%", prm.smear * 100.0f] norm:prm.smear y:s3g::gui_layout::rowY(family.delayEngine, 4u) panel:family.delayEngine attrs:small];
    [self drawSlider:@"SPRD" value:[NSString stringWithFormat:@"%.0f%%", prm.spread * 100.0f] norm:prm.spread y:s3g::gui_layout::rowY(family.delayRelationships, 0u) panel:family.delayRelationships attrs:small];
    [self drawSlider:@"DEV" value:[NSString stringWithFormat:@"%.0f%%", prm.deviation * 100.0f] norm:prm.deviation y:s3g::gui_layout::rowY(family.delayRelationships, 1u) panel:family.delayRelationships attrs:small];
    [self drawSlider:@"SKW" value:[NSString stringWithFormat:@"%+.2f", prm.skew] norm:(prm.skew + 1.0f) * 0.5f y:s3g::gui_layout::rowY(family.delayRelationships, 2u) panel:family.delayRelationships attrs:small];
    [self drawSlider:@"CTR" value:[NSString stringWithFormat:@"%.0f%%", prm.center * 100.0f] norm:prm.center y:s3g::gui_layout::rowY(family.delayRelationships, 3u) panel:family.delayRelationships attrs:small];
    [self drawSlider:@"GLD" value:[NSString stringWithFormat:@"%.0f ms", prm.glideMs] norm:(prm.glideMs - 10.0f) / 1990.0f y:s3g::gui_layout::rowY(family.delayRelationships, 4u) panel:family.delayRelationships attrs:small];

    [self drawRelationshipPreview:prm rect:NSMakeRect(
        family.preview.frame.x + 12.0, family.preview.frame.y + 32.0,
        family.preview.frame.width - 24.0, family.preview.frame.height - 44.0)
        attrs:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto& family = s3g::gui_layout::kMacroFamilyLayout;
    const bool outputSlider = _dragSlider == kOutputParamId || _dragSlider == kMixParamId;
    const bool engineSlider = _dragSlider >= kTimeParamId && _dragSlider <= kSmearParamId;
    const auto& panel = outputSlider ? family.output
        : (engineSlider ? family.delayEngine : family.delayRelationships);
    const double controlX = s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double n = std::clamp((point.x - controlX) / trackWidth, 0.0, 1.0);
    if (_dragSlider >= 1 && _dragSlider <= 5) {
        switch (_dragSlider) {
        case 1: applyParam(*p, kTimeParamId, 5.0 + n * 1995.0); break;
        case 2: applyParam(*p, kFeedbackParamId, n * 0.78); break;
        case 3: applyParam(*p, kToneParamId, n); break;
        case 4: applyParam(*p, kCharacterParamId, n); break;
        case 5: applyParam(*p, kSmearParamId, n); break;
        default: break;
        }
    } else {
        switch (_dragSlider) {
        case 6: applyParam(*p, kSpreadParamId, n); break;
        case 7: applyParam(*p, kDeviationParamId, n); break;
        case 8: applyParam(*p, kSkewParamId, -1.0 + n * 2.0); break;
        case kCenterParamId: applyParam(*p, kCenterParamId, n); break;
        case kGlideParamId: applyParam(*p, kGlideParamId, 10.0 + n * 1990.0); break;
        case kMixParamId: applyParam(*p, kMixParamId, n); break;
        case kOutputParamId: applyParam(*p, kOutputParamId, -60.0 + n * 72.0); break;
        default: break;
        }
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto& family = s3g::gui_layout::kMacroFamilyLayout;
    const auto titleBand = s3g::gui_layout::macroTitleBand(family.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Macro Delay", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kOutputParamId)) {
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
    const clap_id engineIds[] = {
        kTimeParamId, kFeedbackParamId, kToneParamId,
        kCharacterParamId, kSmearParamId
    };
    const clap_id relationshipIds[] = {
        kSpreadParamId, kDeviationParamId, kSkewParamId,
        kCenterParamId, kGlideParamId
    };
    const clap_id outputIds[] = { kOutputParamId, kMixParamId };
    for (uint32_t i = 0u; i < 2u; ++i) {
        if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.output, i)))) {
            beginSlider(outputIds[i]);
            return;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.delayEngine, i)))) {
            beginSlider(engineIds[i]);
            return;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(family.delayRelationships, i)))) {
            beginSlider(relationshipIds[i]);
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
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G_MACRO_DELAY_VIEW_CLASS alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; [static_cast<S3G_MACRO_DELAY_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3G_MACRO_DELAY_VIEW_CLASS*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3G_MACRO_DELAY_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    S3G_MACRO_DELAY_PLUGIN_ID,
    S3G_MACRO_DELAY_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    S3G_MACRO_DELAY_DESCRIPTION,
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
