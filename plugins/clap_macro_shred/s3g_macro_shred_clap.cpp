#include "s3g_macro_shred.h"
#include "s3g_realtime.h"

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
#include <new>

namespace {

#ifndef S3G_MACRO_SHRED_CHANNEL_COUNT
#define S3G_MACRO_SHRED_CHANNEL_COUNT 8
#endif

constexpr uint32_t kChannelCount = S3G_MACRO_SHRED_CHANNEL_COUNT;
static_assert(kChannelCount > 0 && kChannelCount <= s3g::kMacroShredChannels,
    "S3G_MACRO_SHRED_CHANNEL_COUNT must be in the supported Macro Shred range.");
constexpr bool kPassExtraHostChannels = kChannelCount >= 24;

#ifndef S3G_MACRO_SHRED_PLUGIN_ID
#define S3G_MACRO_SHRED_PLUGIN_ID "org.s3g.s3g-dsp.macro-shred-8ch"
#endif
#ifndef S3G_MACRO_SHRED_PLUGIN_NAME
#define S3G_MACRO_SHRED_PLUGIN_NAME "s3g Macro Shred 8ch"
#endif
#ifndef S3G_MACRO_SHRED_DESCRIPTION
#define S3G_MACRO_SHRED_DESCRIPTION "8-channel macro pressure, wavefolding, and bounded resonant-feedback processor."
#endif

constexpr uint32_t kStateVersion = 2;
constexpr uint32_t kGuiWidth = kChannelCount == 1u ? 416u : 820u;
constexpr uint32_t kGuiHeight = kChannelCount == 1u ? 502u : 558u;
constexpr double kEngineTrackX = 166.0;
constexpr double kRelationshipTrackX = 528.0;
constexpr double kSliderTrackWidth = 150.0;

constexpr clap_id kInputParamId = 1;
constexpr clap_id kPressureParamId = 2;
constexpr clap_id kShredParamId = 3;
constexpr clap_id kFeedbackParamId = 4;
constexpr clap_id kColorParamId = 5;
constexpr clap_id kReactParamId = 6;
constexpr clap_id kBodyParamId = 7;
constexpr clap_id kSpreadParamId = 8;
constexpr clap_id kDeviationParamId = 9;
constexpr clap_id kSkewParamId = 10;
constexpr clap_id kCenterParamId = 11;
constexpr clap_id kGlideParamId = 12;
constexpr clap_id kMixParamId = 13;
constexpr clap_id kOutputParamId = 14;
constexpr clap_id kTuneParamId = 15;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::MacroShredParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::MacroShredParams params {};
    s3g::MacroShred shred;
    std::array<float, kChannelCount> frameIn {};
    std::array<float, kChannelCount> frameOut {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> loopActivity { 0.0f };
    std::atomic<float> loopFrequencyHz { 900.0f };
    std::atomic<bool> panicRequested { false };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool paramAffectsTail(clap_id id)
{
    switch (id) {
    case kFeedbackParamId:
    case kColorParamId:
    case kReactParamId:
    case kTuneParamId:
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
    case kInputParamId: p.params.inputGainDb = static_cast<float>(std::clamp(value, -24.0, 36.0)); break;
    case kPressureParamId: p.params.pressure = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kShredParamId: p.params.shred = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFeedbackParamId: p.params.feedback = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kColorParamId: p.params.color = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kReactParamId: p.params.react = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kTuneParamId: p.params.tune = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kBodyParamId: p.params.body = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSpreadParamId: p.params.spread = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDeviationParamId: p.params.deviation = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSkewParamId: p.params.skew = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kCenterParamId: p.params.center = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGlideParamId: p.params.glideMs = static_cast<float>(std::clamp(value, 10.0, 2000.0)); break;
    case kMixParamId: p.params.mix = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(std::clamp(value, -60.0, 6.0)); break;
    default: break;
    }
    p.shred.setParams(p.params);
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
    p->shred.prepare(sampleRate, kChannelCount);
    p->shred.setParams(p->params);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->loopActivity.store(0.0f, std::memory_order_relaxed);
    p->loopFrequencyHz.store(900.0f, std::memory_order_relaxed);
    p->panicRequested.store(false, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->shred.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->loopActivity.store(0.0f, std::memory_order_relaxed);
    p->loopFrequencyHz.store(900.0f, std::memory_order_relaxed);
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
    if (p->panicRequested.exchange(false, std::memory_order_acq_rel)) {
        p->shred.panic();
    }

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

    p->shred.setParams(p->params);
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
        p->shred.processFrame(p->frameIn.data(), p->frameOut.data());
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (output.data32 && output.data32[ch]) {
                output.data32[ch][i] = p->frameOut[ch];
            }
            if (output.data64 && output.data64[ch]) {
                output.data64[ch][i] = static_cast<double>(p->frameOut[ch]);
            }
            blockPeak = std::max(blockPeak, std::abs(p->frameOut[ch]));
        }
    }
    finishExtraChannels(input, output, channels, frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    const float loop = p->shred.feedbackActivity();
    p->loopActivity.store(std::max(p->loopActivity.load(std::memory_order_relaxed) * 0.92f, loop), std::memory_order_relaxed);
    p->loopFrequencyHz.store(p->shred.feedbackFrequencyHz(), std::memory_order_relaxed);
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
    info->port_type = kChannelCount == 1 ? CLAP_PORT_MONO : CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kInputParamId, "Input", -24.0, 36.0, 0.0 },
    { kPressureParamId, "Pressure", 0.0, 1.0, 0.28 },
    { kShredParamId, "Shred", 0.0, 1.0, 0.18 },
    { kFeedbackParamId, "Feedback", 0.0, 1.0, 0.12 },
    { kColorParamId, "Color", 0.0, 1.0, 0.55 },
    { kReactParamId, "React", 0.0, 1.0, 0.25 },
    { kTuneParamId, "Tune", 0.0, 1.0, 0.65 },
    { kBodyParamId, "Body", 0.0, 1.0, 0.65 },
#if S3G_MACRO_SHRED_CHANNEL_COUNT > 1
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.0 },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.0 },
    { kSkewParamId, "Skew", -1.0, 1.0, 0.0 },
    { kCenterParamId, "Center", 0.0, 1.0, 0.5 },
    { kGlideParamId, "Glide", 10.0, 2000.0, 250.0 },
#endif
    { kMixParamId, "Mix", 0.0, 1.0, 0.65 },
    { kOutputParamId, "Output", -60.0, 6.0, -3.0 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Macro Shred", sizeof(info->module));
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
    case kInputParamId: *value = p.inputGainDb; return true;
    case kPressureParamId: *value = p.pressure; return true;
    case kShredParamId: *value = p.shred; return true;
    case kFeedbackParamId: *value = p.feedback; return true;
    case kColorParamId: *value = p.color; return true;
    case kReactParamId: *value = p.react; return true;
    case kTuneParamId: *value = p.tune; return true;
    case kBodyParamId: *value = p.body; return true;
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
    if (id == kInputParamId || id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kGlideParamId) std::snprintf(display, size, "%.0f ms", value);
    else if (id == kSkewParamId) std::snprintf(display, size, "%+.2f", value);
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    if (std::strchr(display, '%')) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state {};
    state.params = self(plugin)->params;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&state);
    uint64_t offset = 0u;
    while (offset < sizeof(state)) {
        const int64_t written = stream->write(stream, bytes + offset, sizeof(state) - offset);
        if (written <= 0) return false;
        offset += static_cast<uint64_t>(written);
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    auto* bytes = reinterpret_cast<uint8_t*>(&state);
    uint64_t offset = 0u;
    while (offset < sizeof(state)) {
        const int64_t read = stream->read(stream, bytes + offset, sizeof(state) - offset);
        if (read <= 0) return false;
        offset += static_cast<uint64_t>(read);
    }
    if (state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = state.params;
    p->shred.setParams(p->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p) return 0u;
    const double feedback = std::clamp(
        static_cast<double>(p->params.feedback) * 0.94 + static_cast<double>(p->params.react) * 0.025,
        0.0, 0.965);
    if (feedback < 0.001) return 0u;
    const double repeatsToMinus60 = std::ceil(std::log(0.001) / std::log(feedback));
    const double tailSeconds = std::clamp(repeatsToMinus60 / 30.0 + 0.1, 0.1, 30.0);
    return static_cast<uint32_t>(std::ceil(tailSeconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
#if S3G_MACRO_SHRED_CHANNEL_COUNT == 1
#define S3GMacroShredView S3GMacroShredMonoView
#elif S3G_MACRO_SHRED_CHANNEL_COUNT == 8
#define S3GMacroShredView S3GMacroShred8View
#else
#define S3GMacroShredView S3GMacroShred24View
#endif
@interface S3GMacroShredView : NSView { void* _plugin; int _dragSlider; NSTimer* _timer; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawEngineSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs;
- (void)drawRelationshipPreview:(const s3g::MacroShredParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* shredColor(int rgb) { return s3g::clap_gui::color(rgb); }

@implementation S3GMacroShredView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _timer = nil;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
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
    if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}
- (void)drawEngineSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style,
        64, kEngineTrackX, 368, kSliderTrackWidth);
}
- (void)drawRelationshipPreview:(const s3g::MacroShredParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [shredColor(0x111111) setFill]; NSRectFill(rect);
    [shredColor(0x444444) setStroke]; NSFrameRect(rect);
    const CGFloat baseY = rect.origin.y + 28.0;
    const CGFloat rowH = (rect.size.height - 38.0) / static_cast<CGFloat>(std::max<uint32_t>(1u, kChannelCount));
    const CGFloat labelX = rect.origin.x + 10.0;
    const CGFloat barX = rect.origin.x + 48.0;
    const CGFloat barW = rect.size.width - 64.0;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const float u = kChannelCount > 1u ? static_cast<float>(ch) / static_cast<float>(kChannelCount - 1u) : 0.5f;
        const float centered = std::clamp((u - params.center) * 2.0f, -1.0f, 1.0f);
        uint32_t x = ch * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        x = (x >> 22u) ^ x;
        const float random = static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
        const float shift = centered * params.spread * 1.5f + random * params.deviation * 0.75f + params.skew * (u - 0.5f) * 0.75f;
        const float norm = std::clamp(0.5f + shift * 0.22f, 0.0f, 1.0f);
        const CGFloat y = baseY + static_cast<CGFloat>(ch) * rowH;
        [[NSString stringWithFormat:@"L%u", ch + 1u] drawAtPoint:NSMakePoint(labelX, y - 4.0) withAttributes:attrs];
        NSRect track = NSMakeRect(barX, y, barW, 6.0);
        [shredColor(0x171717) setFill]; NSRectFill(track);
        [shredColor(0x333333) setStroke]; NSFrameRect(track);
        const CGFloat markerX = track.origin.x + 2.0 + (track.size.width - 4.0) * norm;
        [shredColor(0xb8b8b8) setFill];
        NSRectFill(NSMakeRect(markerX - 2.0, track.origin.y - 2.0, 4.0, 10.0));
    }
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    [@"s3g MACRO SHRED" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    const CGFloat peakX = kChannelCount == 1u ? 264.0 : 666.0;
    const CGFloat channelX = kChannelCount == 1u ? 368.0 : 768.0;
    [s3g::clap_gui::peakDbText(p->outputPeak.load(std::memory_order_relaxed)) drawAtPoint:NSMakePoint(peakX, 14) withAttributes:small];
    [[NSString stringWithFormat:@"%uCH", kChannelCount] drawAtPoint:NSMakePoint(channelX, 14) withAttributes:small];

    s3g::clap_gui::drawPanelFrame(18, 42, 380, 266, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE INPUT", true, 18, 42, 380, 21, label, style);
    if constexpr (kChannelCount == 1u) {
        s3g::clap_gui::drawPanelFrame(18, 320, 380, 92, style);
        s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 18, 320, 380, 21, label, style);
        s3g::clap_gui::drawPanelFrame(18, 424, 380, 60, style);
        s3g::clap_gui::drawPanelHeader(@"CONTAINMENT", true, 18, 424, 380, 21, label, style);
    } else {
        s3g::clap_gui::drawPanelFrame(18, 320, 380, 220, style);
        s3g::clap_gui::drawPanelHeader(@"LANE COLOR REL", true, 18, 320, 380, 21, label, style);
        s3g::clap_gui::drawPanelFrame(416, 42, 386, 166, style);
        s3g::clap_gui::drawPanelHeader(@"RELATIONSHIPS", true, 416, 42, 386, 21, label, style);
        s3g::clap_gui::drawPanelFrame(416, 220, 386, 92, style);
        s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 416, 220, 386, 21, label, style);
        s3g::clap_gui::drawPanelFrame(416, 324, 386, 216, style);
        s3g::clap_gui::drawPanelHeader(@"CONTAINMENT", true, 416, 324, 386, 21, label, style);
    }

    const auto& prm = p->params;
    [self drawEngineSlider:@"INPUT" value:[NSString stringWithFormat:@"%+.1f", prm.inputGainDb] norm:(prm.inputGainDb + 24.0f) / 60.0f y:78 attrs:small];
    [self drawEngineSlider:@"PRS" value:[NSString stringWithFormat:@"%.0f%%", prm.pressure * 100.0f] norm:prm.pressure y:104 attrs:small];
    [self drawEngineSlider:@"SHRED" value:[NSString stringWithFormat:@"%.0f%%", prm.shred * 100.0f] norm:prm.shred y:130 attrs:small];
    [self drawEngineSlider:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", prm.feedback * 100.0f] norm:prm.feedback y:156 attrs:small];
    [self drawEngineSlider:@"COLOR" value:[NSString stringWithFormat:@"%.0f%%", prm.color * 100.0f] norm:prm.color y:182 attrs:small];
    [self drawEngineSlider:@"REACT" value:[NSString stringWithFormat:@"%.0f%%", prm.react * 100.0f] norm:prm.react y:208 attrs:small];
    [self drawEngineSlider:@"TUNE" value:[NSString stringWithFormat:@"%.0f%%", prm.tune * 100.0f] norm:prm.tune y:234 attrs:small];
    [self drawEngineSlider:@"BODY" value:[NSString stringWithFormat:@"%.0f%%", prm.body * 100.0f] norm:prm.body y:260 attrs:small];

    NSRect loopTrack;
    NSRect panicRect;
    const float loopHz = p->loopFrequencyHz.load(std::memory_order_relaxed);
    NSString* loopText = loopHz >= 1000.0f
        ? [NSString stringWithFormat:@"LOOP %.1fK", loopHz * 0.001f]
        : [NSString stringWithFormat:@"LOOP %.0f", loopHz];
    if constexpr (kChannelCount == 1u) {
        s3g::clap_gui::drawSlider(@"MIX", [NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f], prm.mix, 356, small, small, style, 28, kEngineTrackX, 368, kSliderTrackWidth);
        s3g::clap_gui::drawSlider(@"OUT", [NSString stringWithFormat:@"%+.1f", prm.outputGainDb], (prm.outputGainDb + 60.0f) / 66.0f, 382, small, small, style, 28, kEngineTrackX, 368, kSliderTrackWidth);
        [loopText drawAtPoint:NSMakePoint(30, 456) withAttributes:label];
        loopTrack = NSMakeRect(120, 458, 90, 10);
        panicRect = NSMakeRect(238, 449, 142, 26);
    } else {
        s3g::clap_gui::drawSlider(@"SPRD", [NSString stringWithFormat:@"%.0f%%", prm.spread * 100.0f], prm.spread, 78, small, small, style, 426, kRelationshipTrackX, 754, kSliderTrackWidth);
        s3g::clap_gui::drawSlider(@"DEV", [NSString stringWithFormat:@"%.0f%%", prm.deviation * 100.0f], prm.deviation, 104, small, small, style, 426, kRelationshipTrackX, 754, kSliderTrackWidth);
        s3g::clap_gui::drawSlider(@"SKW", [NSString stringWithFormat:@"%+.2f", prm.skew], (prm.skew + 1.0f) * 0.5f, 130, small, small, style, 426, kRelationshipTrackX, 754, kSliderTrackWidth);
        s3g::clap_gui::drawSlider(@"CTR", [NSString stringWithFormat:@"%.0f%%", prm.center * 100.0f], prm.center, 156, small, small, style, 426, kRelationshipTrackX, 754, kSliderTrackWidth);
        s3g::clap_gui::drawSlider(@"GLD", [NSString stringWithFormat:@"%.0f", prm.glideMs], (prm.glideMs - 10.0f) / 1990.0f, 182, small, small, style, 426, kRelationshipTrackX, 754, kSliderTrackWidth);
        s3g::clap_gui::drawSlider(@"MIX", [NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f], prm.mix, 256, small, small, style, 426, kRelationshipTrackX, 754, kSliderTrackWidth);
        s3g::clap_gui::drawSlider(@"OUT", [NSString stringWithFormat:@"%+.1f", prm.outputGainDb], (prm.outputGainDb + 60.0f) / 66.0f, 282, small, small, style, 426, kRelationshipTrackX, 754, kSliderTrackWidth);
        [self drawRelationshipPreview:prm rect:NSMakeRect(30, 352, 356, 176) attrs:small];
        [loopText drawAtPoint:NSMakePoint(438, 374) withAttributes:label];
        loopTrack = NSMakeRect(528, 376, 226, 10);
        panicRect = NSMakeRect(610, 424, 144, 34);
    }
    [shredColor(0x111111) setFill]; NSRectFill(loopTrack);
    [shredColor(0x444444) setStroke]; NSFrameRect(loopTrack);
    NSRect loopFill = NSInsetRect(loopTrack, 1.0, 1.0);
    loopFill.size.width *= std::clamp<CGFloat>(p->loopActivity.load(std::memory_order_relaxed), 0.0, 1.0);
    [shredColor(0xb8b8b8) setFill]; NSRectFill(loopFill);

    [shredColor(0x161616) setFill]; NSRectFill(panicRect);
    [shredColor(0x565656) setStroke]; NSFrameRect(panicRect);
    [@"PANIC" drawInRect:NSInsetRect(panicRect, 46, kChannelCount == 1u ? 5 : 9) withAttributes:label];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const bool engineSlider = (_dragSlider >= 1 && _dragSlider <= 7) || _dragSlider == kTuneParamId;
    if (engineSlider) {
        const double n = std::clamp((point.x - kEngineTrackX) / kSliderTrackWidth, 0.0, 1.0);
        switch (_dragSlider) {
        case 1: applyParam(*p, kInputParamId, -24.0 + n * 60.0); break;
        case 2: applyParam(*p, kPressureParamId, n); break;
        case 3: applyParam(*p, kShredParamId, n); break;
        case 4: applyParam(*p, kFeedbackParamId, n); break;
        case 5: applyParam(*p, kColorParamId, n); break;
        case 6: applyParam(*p, kReactParamId, n); break;
        case 7: applyParam(*p, kBodyParamId, n); break;
        case kTuneParamId: applyParam(*p, kTuneParamId, n); break;
        default: break;
        }
    } else if (_dragSlider >= 8 && _dragSlider <= 14) {
        if constexpr (kChannelCount == 1u) {
            const double n = std::clamp((point.x - kEngineTrackX) / kSliderTrackWidth, 0.0, 1.0);
            if (_dragSlider == 13) applyParam(*p, kMixParamId, n);
            else if (_dragSlider == 14) applyParam(*p, kOutputParamId, -60.0 + n * 66.0);
        } else {
            const double n = std::clamp((point.x - kRelationshipTrackX) / kSliderTrackWidth, 0.0, 1.0);
            switch (_dragSlider) {
            case 8: applyParam(*p, kSpreadParamId, n); break;
            case 9: applyParam(*p, kDeviationParamId, n); break;
            case 10: applyParam(*p, kSkewParamId, -1.0 + n * 2.0); break;
            case 11: applyParam(*p, kCenterParamId, n); break;
            case 12: applyParam(*p, kGlideParamId, 10.0 + n * 1990.0); break;
            case 13: applyParam(*p, kMixParamId, n); break;
            case 14: applyParam(*p, kOutputParamId, -60.0 + n * 66.0); break;
            default: break;
            }
        }
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect panicRect = kChannelCount == 1u
        ? NSMakeRect(238, 449, 142, 26)
        : NSMakeRect(610, 424, 144, 34);
    if (NSPointInRect(point, panicRect)) {
        static_cast<Plugin*>(_plugin)->panicRequested.store(true, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat engineRows[] = { 78, 104, 130, 156, 182, 208, 234, 260 };
    const int engineParamIds[] = {
        kInputParamId, kPressureParamId, kShredParamId, kFeedbackParamId,
        kColorParamId, kReactParamId, kTuneParamId, kBodyParamId
    };
    const CGFloat relationshipRows[] = { 78, 104, 130, 156, 182 };
    const CGFloat outputRows[] = { 256, 282 };
    for (int i = 0; i < 8; ++i) {
        if (NSPointInRect(point, NSMakeRect(60, engineRows[i] - 8, 326, 24))) {
            _dragSlider = engineParamIds[i];
            [self updateSlider:point];
            return;
        }
    }
    if constexpr (kChannelCount > 1u) {
        for (int i = 0; i < 5; ++i) {
            if (NSPointInRect(point, NSMakeRect(422, relationshipRows[i] - 8, 344, 24))) {
                _dragSlider = i + 8;
                [self updateSlider:point];
                return;
            }
        }
    }
    if constexpr (kChannelCount == 1u) {
        const CGFloat monoOutputRows[] = { 356, 382 };
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, NSMakeRect(24, monoOutputRows[i] - 8, 362, 24))) {
                _dragSlider = i + 13;
                [self updateSlider:point];
                return;
            }
        }
    } else {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, NSMakeRect(422, outputRows[i] - 8, 344, 24))) {
                _dragSlider = i + 13;
                [self updateSlider:point];
                return;
            }
        }
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
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
    p->guiView = [[S3GMacroShredView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible = false;
        auto* view = static_cast<S3GMacroShredView*>(p->guiView);
        [view stopRefreshTimer];
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    *width = kGuiWidth;
    *height = kGuiHeight;
    return true;
}
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(width, height)];
    return true;
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* p = self(plugin);
    if (!p->guiView) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = true;
    [static_cast<NSView*>(p->guiView) setHidden:NO];
    [static_cast<S3GMacroShredView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GMacroShredView*>(p->guiView) stopRefreshTimer];
    [static_cast<NSView*>(p->guiView) setHidden:YES];
    return true;
}
const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide
};
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

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DISTORTION,
#if S3G_MACRO_SHRED_CHANNEL_COUNT == 1
    CLAP_PLUGIN_FEATURE_MONO,
#else
    CLAP_PLUGIN_FEATURE_SURROUND,
#endif
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    S3G_MACRO_SHRED_PLUGIN_ID,
    S3G_MACRO_SHRED_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    S3G_MACRO_SHRED_DESCRIPTION,
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostTail = host && host->get_extension
        ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL))
        : nullptr;
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
