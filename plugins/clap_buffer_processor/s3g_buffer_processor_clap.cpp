#include "s3g_buffer_processor.h"
#include "s3g_realtime.h"

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

namespace {

constexpr uint32_t kChannelCount = s3g::kBufferProcessorChannels;
constexpr uint32_t kStateVersion = 2;
constexpr uint32_t kGuiWidth = 760;
constexpr uint32_t kGuiHeight = 584;

constexpr clap_id kSizeParamId = 1;
constexpr clap_id kRateParamId = 2;
constexpr clap_id kXfadeParamId = 3;
constexpr clap_id kRepeatParamId = 4;
constexpr clap_id kReverseParamId = 5;
constexpr clap_id kCrushParamId = 6;
constexpr clap_id kSpreadParamId = 7;
constexpr clap_id kDeviationParamId = 8;
constexpr clap_id kSkewParamId = 9;
constexpr clap_id kCenterParamId = 10;
constexpr clap_id kGlideParamId = 11;
constexpr clap_id kMixParamId = 12;
constexpr clap_id kOutputParamId = 13;
constexpr clap_id kSkipParamId = 14;
constexpr clap_id kErrorParamId = 15;
constexpr clap_id kSkipGridParamId = 16;
constexpr clap_id kSkipJamParamId = 17;
constexpr clap_id kSkipChaseParamId = 18;
constexpr clap_id kSkipSyncParamId = 19;
constexpr clap_id kErrorModeParamId = 20;
constexpr clap_id kMemoryParamId = 21;

const char* skipGridName(uint32_t index)
{
    static constexpr const char* names[] { "OFF", "1/8", "1/12", "1/16", "1/32" };
    return names[std::min<uint32_t>(index, 4u)];
}

const char* errorModeName(uint32_t index)
{
    static constexpr const char* names[] { "PCM", "BLK", "CRC", "HDR", "RAW" };
    return names[std::min<uint32_t>(index, 4u)];
}

uint32_t steppedIndex(double value, uint32_t maxValue)
{
    return static_cast<uint32_t>(std::clamp(std::floor(value + 0.5), 0.0, static_cast<double>(maxValue)));
}

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::BufferProcessorParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::BufferProcessorParams params {};
    s3g::BufferProcessor processor;
    std::atomic<float> outputPeak { 0.0f };
    bool transportWasPlaying = false;
    bool transportIsStopped = false;
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    void* macRealtimeActivity = nullptr;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kSizeParamId: p.params.sizeMs = static_cast<float>(std::clamp(value, 8.0, 1200.0)); break;
    case kRateParamId: p.params.rate = static_cast<float>(std::clamp(value, -2.0, 2.0)); break;
    case kXfadeParamId: p.params.crossfade = static_cast<float>(std::clamp(value, 0.0, 0.45)); break;
    case kRepeatParamId: p.params.repeat = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSkipParamId: p.params.skip = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSkipGridParamId: p.params.skipGrid = static_cast<float>(std::clamp(std::floor(value + 0.5), 0.0, 4.0)); break;
    case kSkipJamParamId: p.params.skipJam = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSkipChaseParamId: p.params.skipChase = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSkipSyncParamId: p.params.skipSync = static_cast<float>(std::clamp(std::floor(value + 0.5), 0.0, 1.0)); break;
    case kReverseParamId: p.params.reverse = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kCrushParamId: p.params.crush = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kErrorParamId: p.params.error = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kErrorModeParamId: p.params.errorMode = static_cast<float>(std::clamp(std::floor(value + 0.5), 0.0, 4.0)); break;
    case kMemoryParamId: p.params.memory = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSpreadParamId: p.params.spread = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kDeviationParamId: p.params.deviation = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kSkewParamId: p.params.skew = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kCenterParamId: p.params.center = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGlideParamId: p.params.glideMs = static_cast<float>(std::clamp(value, 10.0, 2000.0)); break;
    case kMixParamId: p.params.mix = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    default: break;
    }
    p.processor.setParams(p.params);
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
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(p->macRealtimeActivity);
#endif
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->processor.prepare(sampleRate, kChannelCount, 4.0);
    p->processor.setParams(p->params);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#else
    (void)plugin;
#endif
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->transportWasPlaying = false;
    p->transportIsStopped = false;
    p->processor.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->transportWasPlaying = false;
    p->transportIsStopped = false;
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

void finishExtraChannels(const clap_audio_buffer_t& output, uint32_t channels, uint32_t frames)
{
    for (uint32_t ch = channels; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;

    if (proc->transport) {
        const bool playing = (proc->transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        const bool hasTempo = (proc->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0;
        p->processor.setTransport(hasTempo ? proc->transport->tempo : 120.0, hasTempo);
        if (!playing && p->transportWasPlaying) {
            p->processor.reset();
            p->outputPeak.store(0.0f, std::memory_order_relaxed);
        }
        p->transportWasPlaying = playing;
        p->transportIsStopped = !playing;
    }

    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, kChannelCount });
    const uint32_t frames = proc->frames_count;
    if (channels == 0u || (!output.data32 && !output.data64)) return CLAP_PROCESS_CONTINUE;

    if (p->transportIsStopped) {
        float blockPeak = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            for (uint32_t i = 0; i < frames; ++i) {
                float value = 0.0f;
                if (input.data32 && input.data32[ch]) value = input.data32[ch][i];
                else if (input.data64 && input.data64[ch]) value = static_cast<float>(input.data64[ch][i]);
                if (output.data32 && output.data32[ch]) output.data32[ch][i] = value;
                if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(value);
                blockPeak = std::max(blockPeak, std::fabs(value));
            }
        }
        finishExtraChannels(output, channels, frames);
        p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
        return CLAP_PROCESS_CONTINUE;
    }

    p->processor.setParams(p->params);
    std::array<float, kChannelCount> frameIn {};
    std::array<float, kChannelCount> frameOut {};
    float blockPeak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (input.data32 && input.data32[ch]) frameIn[ch] = input.data32[ch][i];
            else if (input.data64 && input.data64[ch]) frameIn[ch] = static_cast<float>(input.data64[ch][i]);
            else frameIn[ch] = 0.0f;
        }
        for (uint32_t ch = channels; ch < kChannelCount; ++ch) frameIn[ch] = 0.0f;
        p->processor.processFrame(frameIn.data(), frameOut.data());
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = frameOut[ch];
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(frameOut[ch]);
            blockPeak = std::max(blockPeak, std::fabs(frameOut[ch]));
        }
    }
    finishExtraChannels(output, channels, frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "8ch %s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kSizeParamId, "Size", 8.0, 1200.0, 160.0 },
    { kRateParamId, "Rate", -2.0, 2.0, 1.0 },
    { kXfadeParamId, "Crossfade", 0.0, 0.45, 0.12 },
    { kRepeatParamId, "Repeat", 0.0, 1.0, 0.65 },
    { kSkipParamId, "Skip", 0.0, 1.0, 0.0 },
    { kSkipGridParamId, "Skip Grid", 0.0, 4.0, 0.0 },
    { kSkipJamParamId, "Skip Jam", 0.0, 1.0, 0.0 },
    { kSkipChaseParamId, "Skip Chase", 0.0, 1.0, 0.0 },
    { kSkipSyncParamId, "Skip Sync", 0.0, 1.0, 0.0 },
    { kReverseParamId, "Reverse", 0.0, 1.0, 0.0 },
    { kCrushParamId, "Crush", 0.0, 1.0, 0.0 },
    { kErrorParamId, "Error", 0.0, 1.0, 0.0 },
    { kErrorModeParamId, "Error Mode", 0.0, 4.0, 0.0 },
    { kMemoryParamId, "Ghost", 0.0, 1.0, 0.0 },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.0 },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.0 },
    { kSkewParamId, "Skew", -1.0, 1.0, 0.0 },
    { kCenterParamId, "Center", 0.0, 1.0, 0.5 },
    { kGlideParamId, "Glide", 10.0, 2000.0, 140.0 },
    { kMixParamId, "Mix", 0.0, 1.0, 0.45 },
    { kOutputParamId, "Output", -60.0, 12.0, -1.5 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.id == kSkipGridParamId || def.id == kSkipSyncParamId || def.id == kErrorModeParamId) {
        info->flags |= CLAP_PARAM_IS_STEPPED;
    }
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Buffer Processor", sizeof(info->module));
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
    case kSizeParamId: *value = p.sizeMs; return true;
    case kRateParamId: *value = p.rate; return true;
    case kXfadeParamId: *value = p.crossfade; return true;
    case kRepeatParamId: *value = p.repeat; return true;
    case kSkipParamId: *value = p.skip; return true;
    case kSkipGridParamId: *value = p.skipGrid; return true;
    case kSkipJamParamId: *value = p.skipJam; return true;
    case kSkipChaseParamId: *value = p.skipChase; return true;
    case kSkipSyncParamId: *value = p.skipSync; return true;
    case kReverseParamId: *value = p.reverse; return true;
    case kCrushParamId: *value = p.crush; return true;
    case kErrorParamId: *value = p.error; return true;
    case kErrorModeParamId: *value = p.errorMode; return true;
    case kMemoryParamId: *value = p.memory; return true;
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
    if (id == kSizeParamId || id == kGlideParamId) std::snprintf(display, size, "%.0f ms", value);
    else if (id == kRateParamId || id == kSkewParamId) std::snprintf(display, size, "%+.2f", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kSkipGridParamId) std::snprintf(display, size, "%s", skipGridName(steppedIndex(value, 4u)));
    else if (id == kSkipSyncParamId) std::snprintf(display, size, "%s", value >= 0.5 ? "SYNC" : "FREE");
    else if (id == kErrorModeParamId) std::snprintf(display, size, "%s", errorModeName(steppedIndex(value, 4u)));
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
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
    p->processor.setParams(p->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GBufferProcessorView : NSView { void* _plugin; int _dragSlider; int _openMenu; int _hoverMenuItem; NSTimer* _timer; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawBufferPreview:(const s3g::BufferProcessorParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawMenuControl:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawActionButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)updateMenuHover:(NSPoint)point;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* bpColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

@implementation S3GBufferProcessorView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
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
- (void)drawMenuControl:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawMenu(name, value, y, attrs, small, style, 398, 500, 174);
}
- (void)drawActionButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [bpColor(0x202020) setFill]; NSRectFill(rect);
    [bpColor(0xb8b8b8) setStroke]; NSFrameRect(rect);
    [bpColor(0x343434) setStroke]; NSFrameRect(NSInsetRect(rect, 1.0, 1.0));
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                                   rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0)
        withAttributes:attrs];
}
- (void)drawBufferPreview:(const s3g::BufferProcessorParams&)params rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [bpColor(0x111111) setFill]; NSRectFill(rect);
    [bpColor(0x444444) setStroke]; NSFrameRect(rect);
    const CGFloat labelX = rect.origin.x + 10.0;
    const CGFloat laneX = rect.origin.x + 48.0;
    const CGFloat laneW = rect.size.width - 66.0;
    const CGFloat rowH = (rect.size.height - 46.0) / static_cast<CGFloat>(std::max<uint32_t>(1u, kChannelCount - 1u));
    const CGFloat baseY = rect.origin.y + 32.0;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const float u = static_cast<float>(ch) / static_cast<float>(std::max<uint32_t>(1u, kChannelCount - 1u));
        const float centered = std::clamp((u - params.center) * 2.0f, -1.0f, 1.0f);
        uint32_t x = ch * 747796405u + 2891336453u;
        x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
        x = (x >> 22u) ^ x;
        const float dev = static_cast<float>(x & 0xffffu) / 32767.5f - 1.0f;
        const float ratio = std::pow(2.0f, centered * params.spread) * std::pow(2.0f, dev * params.deviation * 0.65f) * std::pow(2.0f, params.skew * u);
        const CGFloat y = baseY + static_cast<CGFloat>(ch) * rowH;
        [[NSString stringWithFormat:@"L%u", ch + 1u] drawAtPoint:NSMakePoint(labelX, y - 4.0) withAttributes:attrs];
        NSRect track = NSMakeRect(laneX, y, laneW, 7.0);
        [bpColor(0x171717) setFill]; NSRectFill(track);
        [bpColor(0x333333) setStroke]; NSFrameRect(track);
        const CGFloat windowW = std::clamp<CGFloat>(laneW * 0.20 * ratio, 8.0, laneW);
        const CGFloat pos = laneX + std::fmod(static_cast<CGFloat>(ch) * 31.0 + static_cast<CGFloat>(params.repeat) * 96.0, laneW - windowW + 1.0);
        [bpColor(params.skip > 0.01f ? 0xd8d8d8 : (params.reverse > 0.01f ? 0xc8c8c8 : 0x8f8f8f)) setFill];
        NSRectFill(NSMakeRect(pos, y + 1.0, windowW, 5.0));
        if (params.skip > 0.001f) {
            [bpColor(0xffffff, 0.35 + params.skip * 0.35) setFill];
            const CGFloat skipW = std::clamp<CGFloat>(windowW * (0.12 + params.skip * 0.22), 2.0, windowW);
            for (uint32_t marker = 0; marker < 3u; ++marker) {
                const CGFloat mx = pos + std::fmod(static_cast<CGFloat>(marker) * skipW * 2.1 + static_cast<CGFloat>(ch) * 5.0, std::max<CGFloat>(1.0, windowW - skipW));
                NSRectFill(NSMakeRect(mx, y + 1.0, skipW, 5.0));
            }
            if (params.skipChase > 0.001f) {
                [bpColor(0x9f9f9f, 0.35 + params.skipChase * 0.35) setFill];
                const CGFloat chaseX = laneX + std::fmod(static_cast<CGFloat>(ch) * laneW * 0.11 + static_cast<CGFloat>(params.skipChase) * laneW * 0.35, laneW - 2.0);
                NSRectFill(NSMakeRect(chaseX, y - 1.0, 2.0, 11.0));
            }
        }
        if (params.error > 0.001f) {
            [bpColor(0x101010, 0.35 + params.error * 0.45) setFill];
            const CGFloat blockW = std::max<CGFloat>(2.0, laneW * (0.012 + params.error * 0.025));
            for (uint32_t marker = 0; marker < 5u; ++marker) {
                const CGFloat mx = laneX + std::fmod(static_cast<CGFloat>(marker * 37u + ch * 19u) * (1.0 + params.error), laneW - blockW + 1.0);
                NSRectFill(NSMakeRect(mx, y + 1.0, blockW, 5.0));
            }
        }
        if (params.memory > 0.001f) {
            [bpColor(0xffffff, 0.05 + params.memory * 0.12) setFill];
            const CGFloat ghostW = std::max<CGFloat>(4.0, laneW * (0.08 + params.memory * 0.20));
            const CGFloat ghostX = laneX + std::fmod(static_cast<CGFloat>(ch) * 17.0 + static_cast<CGFloat>(params.memory) * 41.0, laneW - ghostW + 1.0);
            NSRectFill(NSMakeRect(ghostX, y + 2.0, ghostW, 3.0));
        }
        if (params.crossfade > 0.001f) {
            [bpColor(0xffffff, 0.55) setFill];
            const CGFloat xf = std::max<CGFloat>(1.0, windowW * params.crossfade);
            NSRectFill(NSMakeRect(pos, y + 1.0, xf, 5.0));
            NSRectFill(NSMakeRect(pos + windowW - xf, y + 1.0, xf, 5.0));
        }
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
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    [@"s3g BUFFER PROCESSOR" drawAtPoint:NSMakePoint(18,14) withAttributes:titleAttrs];
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [s3g::clap_gui::peakDbText(pk) drawAtPoint:NSMakePoint(604,14) withAttributes:small];
    [@"8CH" drawAtPoint:NSMakePoint(704,14) withAttributes:small];

    s3g::clap_gui::drawPanelFrame(18, 42, 352, 254, style);
    s3g::clap_gui::drawPanelHeader(@"ENGINE", true, 18, 42, 352, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(18, 308, 352, 248, style);
    s3g::clap_gui::drawPanelHeader(@"BUFFER WINDOWS", true, 18, 308, 352, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(388, 42, 354, 166, style);
    s3g::clap_gui::drawPanelHeader(@"RELATIONSHIPS", true, 388, 42, 354, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(388, 222, 354, 208, style);
    s3g::clap_gui::drawPanelHeader(@"CORRUPT", true, 388, 222, 354, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(388, 444, 354, 92, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 388, 444, 354, 21, lab, style);

    const auto& prm = p->params;
    [self drawSlider:@"SIZE" value:[NSString stringWithFormat:@"%.0f", prm.sizeMs] norm:(prm.sizeMs - 8.0f) / 1192.0f y:78 attrs:small small:small];
    [self drawSlider:@"RATE" value:[NSString stringWithFormat:@"%+.2f", prm.rate] norm:(prm.rate + 2.0f) * 0.25f y:104 attrs:small small:small];
    [self drawSlider:@"XFD" value:[NSString stringWithFormat:@"%.0f%%", prm.crossfade * 100.0f] norm:prm.crossfade / 0.45f y:130 attrs:small small:small];
    [self drawSlider:@"RPT" value:[NSString stringWithFormat:@"%.0f%%", prm.repeat * 100.0f] norm:prm.repeat y:156 attrs:small small:small];
    [self drawSlider:@"SKIP" value:[NSString stringWithFormat:@"%.0f%%", prm.skip * 100.0f] norm:prm.skip y:182 attrs:small small:small];
    [self drawSlider:@"REV" value:[NSString stringWithFormat:@"%.0f%%", prm.reverse * 100.0f] norm:prm.reverse y:208 attrs:small small:small];
    [self drawSlider:@"CRSH" value:[NSString stringWithFormat:@"%.0f%%", prm.crush * 100.0f] norm:prm.crush y:234 attrs:small small:small];
    [self drawSlider:@"ERR" value:[NSString stringWithFormat:@"%.0f%%", prm.error * 100.0f] norm:prm.error y:260 attrs:small small:small];

    s3g::clap_gui::drawSlider(@"SPRD", [NSString stringWithFormat:@"%.0f%%", prm.spread * 100.0f], prm.spread, 78, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"DEV", [NSString stringWithFormat:@"%.0f%%", prm.deviation * 100.0f], prm.deviation, 104, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"SKW", [NSString stringWithFormat:@"%+.2f", prm.skew], (prm.skew + 1.0f) * 0.5f, 130, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"CTR", [NSString stringWithFormat:@"%.0f%%", prm.center * 100.0f], prm.center, 156, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"GLD", [NSString stringWithFormat:@"%.0f", prm.glideMs], (prm.glideMs - 10.0f) / 1990.0f, 182, small, small, style, 398, 500, 674);

    [self drawMenuControl:@"GRID" value:[NSString stringWithUTF8String:skipGridName(steppedIndex(prm.skipGrid, 4u))] y:258 attrs:small small:small];
    [self drawMenuControl:@"SYNC" value:(prm.skipSync >= 0.5f ? @"SYNC" : @"FREE") y:284 attrs:small small:small];
    s3g::clap_gui::drawSlider(@"JAM", [NSString stringWithFormat:@"%.0f%%", prm.skipJam * 100.0f], prm.skipJam, 310, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"CHAS", [NSString stringWithFormat:@"%.0f%%", prm.skipChase * 100.0f], prm.skipChase, 336, small, small, style, 398, 500, 674);
    [self drawMenuControl:@"ERRM" value:[NSString stringWithUTF8String:errorModeName(steppedIndex(prm.errorMode, 4u))] y:362 attrs:small small:small];
    s3g::clap_gui::drawSlider(@"GHST", [NSString stringWithFormat:@"%.0f%%", prm.memory * 100.0f], prm.memory, 388, small, small, style, 398, 500, 674);
    [self drawActionButton:@"CAP" rect:NSMakeRect(500, 407, 54, 18) attrs:small];
    [self drawActionButton:@"CLR" rect:NSMakeRect(562, 407, 54, 18) attrs:small];

    s3g::clap_gui::drawSlider(@"MIX", [NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f], prm.mix, 480, small, small, style, 398, 500, 674);
    s3g::clap_gui::drawSlider(@"OUT", [NSString stringWithFormat:@"%+.1f", prm.outputGainDb], (prm.outputGainDb + 60.0f) / 72.0f, 506, small, small, style, 398, 500, 674);

    [self drawBufferPreview:prm rect:NSMakeRect(30,340,330,204) attrs:small];
    [self drawOpenMenu:small style:style];
}
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0) return;
    if (_openMenu == 1) {
        NSString* const items[] = { @"OFF", @"1/8", @"1/12", @"1/16", @"1/32" };
        auto* p = static_cast<Plugin*>(_plugin);
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(500, 273, 174, 18.0 * 5.0), 18.0, items, 5, steppedIndex(p->params.skipGrid, 4u), _hoverMenuItem, attrs, style);
    } else if (_openMenu == 2) {
        NSString* const items[] = { @"FREE", @"SYNC" };
        auto* p = static_cast<Plugin*>(_plugin);
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(500, 299, 174, 18.0 * 2.0), 18.0, items, 2, p->params.skipSync >= 0.5f ? 1 : 0, _hoverMenuItem, attrs, style);
    } else if (_openMenu == 3) {
        NSString* const items[] = { @"PCM", @"BLK", @"CRC", @"HDR", @"RAW" };
        auto* p = static_cast<Plugin*>(_plugin);
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(500, 377, 174, 18.0 * 5.0), 18.0, items, 5, steppedIndex(p->params.errorMode, 4u), _hoverMenuItem, attrs, style);
    }
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0) return;
    const NSRect rect = _openMenu == 1 ? NSMakeRect(500, 273, 174, 18.0 * 5.0)
        : (_openMenu == 2 ? NSMakeRect(500, 299, 174, 18.0 * 2.0) : NSMakeRect(500, 377, 174, 18.0 * 5.0));
    const uint32_t count = _openMenu == 2 ? 2u : 5u;
    const int next = s3g::clap_gui::dropdownHitIndex(point, rect, 18.0, count);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (_dragSlider >= 1 && _dragSlider <= 8) {
        const double n = std::clamp((point.x - 166.0) / 150.0, 0.0, 1.0);
        switch (_dragSlider) {
        case 1: applyParam(*p, kSizeParamId, 8.0 + n * 1192.0); break;
        case 2: applyParam(*p, kRateParamId, -2.0 + n * 4.0); break;
        case 3: applyParam(*p, kXfadeParamId, n * 0.45); break;
        case 4: applyParam(*p, kRepeatParamId, n); break;
        case 5: applyParam(*p, kSkipParamId, n); break;
        case 6: applyParam(*p, kReverseParamId, n); break;
        case 7: applyParam(*p, kCrushParamId, n); break;
        case 8: applyParam(*p, kErrorParamId, n); break;
        default: break;
        }
    } else if (_dragSlider >= 9 && _dragSlider <= 18) {
        const double n = std::clamp((point.x - 500.0) / 150.0, 0.0, 1.0);
        switch (_dragSlider) {
        case 9: applyParam(*p, kSpreadParamId, n); break;
        case 10: applyParam(*p, kDeviationParamId, n); break;
        case 11: applyParam(*p, kSkewParamId, -1.0 + n * 2.0); break;
        case 12: applyParam(*p, kCenterParamId, n); break;
        case 13: applyParam(*p, kGlideParamId, 10.0 + n * 1990.0); break;
        case 14: applyParam(*p, kSkipJamParamId, n); break;
        case 15: applyParam(*p, kSkipChaseParamId, n); break;
        case 16: applyParam(*p, kMemoryParamId, n); break;
        case 17: applyParam(*p, kMixParamId, n); break;
        case 18: applyParam(*p, kOutputParamId, -60.0 + n * 72.0); break;
        default: break;
        }
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu > 0) {
        const NSRect rect = _openMenu == 1 ? NSMakeRect(500, 273, 174, 18.0 * 5.0)
            : (_openMenu == 2 ? NSMakeRect(500, 299, 174, 18.0 * 2.0) : NSMakeRect(500, 377, 174, 18.0 * 5.0));
        const uint32_t count = _openMenu == 2 ? 2u : 5u;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, rect, 18.0, count);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*p, kSkipGridParamId, hit);
            else if (_openMenu == 2) applyParam(*p, kSkipSyncParamId, hit);
            else if (_openMenu == 3) applyParam(*p, kErrorModeParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(500, 257, 174, 16))) {
        _openMenu = 1;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(500, 283, 174, 16))) {
        _openMenu = 2;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(500, 361, 174, 16))) {
        _openMenu = 3;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(500, 407, 54, 18))) {
        p->processor.captureMemory();
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(562, 407, 54, 18))) {
        p->processor.clearMemory();
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat engineRows[] = { 78, 104, 130, 156, 182, 208, 234, 260 };
    const CGFloat relationshipRows[] = { 78, 104, 130, 156, 182 };
    const CGFloat corruptRows[] = { 310, 336, 388 };
    const CGFloat outputRows[] = { 480, 506 };
    for (int i = 0; i < 8; ++i) {
        if (NSPointInRect(pt, NSMakeRect(60, engineRows[i] - 8, 300, 24))) {
            _dragSlider = i + 1;
            [self updateSlider:pt];
            return;
        }
    }
    for (int i = 0; i < 5; ++i) {
        if (NSPointInRect(pt, NSMakeRect(394, relationshipRows[i] - 8, 300, 24))) {
            _dragSlider = i + 9;
            [self updateSlider:pt];
            return;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(pt, NSMakeRect(394, corruptRows[i] - 8, 300, 24))) {
            _dragSlider = i + 14;
            [self updateSlider:pt];
            return;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (NSPointInRect(pt, NSMakeRect(394, outputRows[i] - 8, 300, 24))) {
            _dragSlider = i + 17;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider > 0) [self updateSlider:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseMoved:(NSEvent*)event { [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GBufferProcessorView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3GBufferProcessorView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiWidth,kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GBufferProcessorView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GBufferProcessorView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DELAY, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.buffer-processor",
    "s3g Buffer Processor 8ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "8-channel buffer repeat, reverse, crush, and lane relationship processor.",
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
