#include "s3g_realtime.h"
#include "s3g_spectral_topology_processor.h"

#include <clap/clap.h>
#include <clap/ext/latency.h>
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

constexpr uint32_t kChannelCount = s3g::kSpectralTopologyChannels;
constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kGuiWidth = 1000;
constexpr uint32_t kGuiHeight = 620;
constexpr double kMotionRateMinHz = 0.01;
constexpr double kMotionRateMaxHz = 1.0;

constexpr clap_id kSprayBinsParamId = 1;
constexpr clap_id kDriftParamId = 2;
constexpr clap_id kHoldParamId = 3;
constexpr clap_id kFreezeParamId = 4;
constexpr clap_id kFeedbackParamId = 5;
constexpr clap_id kSmearParamId = 6;
constexpr clap_id kHolesParamId = 7;
constexpr clap_id kPhaseBlurParamId = 8;
constexpr clap_id kTiltParamId = 9;
constexpr clap_id kMixParamId = 10;
constexpr clap_id kGainParamId = 11;
constexpr clap_id kSafetyParamId = 12;

constexpr clap_id kTopologyShapeParamId = 30;
constexpr clap_id kTopologyAmountParamId = 31;
constexpr clap_id kTopologySeedParamId = 32;
constexpr clap_id kTopologyPullParamId = 33;
constexpr clap_id kTopologyXParamId = 34;
constexpr clap_id kTopologyYParamId = 35;
constexpr clap_id kTopologyZParamId = 36;
constexpr clap_id kTopologyTwistParamId = 37;
constexpr clap_id kTopologyFlareParamId = 38;
constexpr clap_id kTopologyMotionParamId = 39;
constexpr clap_id kTopologyVariantParamId = 40;
constexpr clap_id kTopologyRateParamId = 41;
constexpr clap_id kTopologyDepthParamId = 42;
constexpr clap_id kTopologyNeighborsParamId = 43;
constexpr clap_id kTopologyRadiusParamId = 44;
constexpr clap_id kTopologyCentroidParamId = 45;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::SpectralTopologySettings settings {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::SpectralTopologySettings settings {};
    s3g::SpectralTopologyProcessor processor;
    std::vector<std::vector<float>> input32;
    std::vector<std::vector<float>> output32;
    std::vector<const float*> inputPtrs;
    std::vector<float*> outputPtrs;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    void* macRealtimeActivity = nullptr;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

double clampMotionRate(double value)
{
    return std::clamp(value, kMotionRateMinHz, kMotionRateMaxHz);
}

s3g::TopologyState topologyStateForPlugin(const Plugin& p)
{
    return p.settings.topology;
}

void applyLaneParams(Plugin& p)
{
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p.processor.setLaneParams(ch, s3g::spectralTopologyLaneParams(p.settings, ch, kChannelCount));
    }
}

bool topologyMotionActive(const Plugin& p)
{
    return s3g::topologyMotionActive(p.settings.topology);
}

void advanceTopologyMotion(Plugin& p, uint32_t frames)
{
    if (!topologyMotionActive(p) || p.sampleRate <= 0.0 || frames == 0u) return;
    auto& t = p.settings.topology;
    t.motionPhase += (static_cast<double>(frames) / p.sampleRate) * t.motionRateHz;
    t.motionPhase -= std::floor(t.motionPhase);
    applyLaneParams(p);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    auto& prm = p.settings.base;
    auto& t = p.settings.topology;
    switch (id) {
    case kSprayBinsParamId: prm.sprayBins = static_cast<float>(std::clamp(value, 0.0, 192.0)); break;
    case kDriftParamId: prm.drift = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kHoldParamId: prm.hold = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kFreezeParamId: prm.freeze = static_cast<float>(std::clamp(value, 0.0, 0.92)); break;
    case kFeedbackParamId: prm.feedback = static_cast<float>(std::clamp(value, 0.0, 0.72)); break;
    case kSmearParamId: prm.smear = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kHolesParamId: prm.holes = static_cast<float>(std::clamp(value, 0.0, 0.60)); break;
    case kPhaseBlurParamId: prm.phaseBlur = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kTiltParamId: prm.tilt = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
    case kMixParamId: prm.mix = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kGainParamId: prm.gainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    case kSafetyParamId: prm.safety = static_cast<float>(std::clamp(value, 0.12, 0.92)); break;
    case kTopologyShapeParamId: t.shape = std::min<uint32_t>(s3g::kTopologyShapeCount - 1u, static_cast<uint32_t>(std::floor(value + 0.5))); break;
    case kTopologyAmountParamId: t.amount = std::clamp(value, 0.0, 1.0); break;
    case kTopologySeedParamId: t.jitter = std::clamp(value, 0.0, 1.0); break;
    case kTopologyPullParamId: t.collapse = std::clamp(value, 0.0, 1.0); break;
    case kTopologyXParamId: t.dirX = std::clamp(value, -1.0, 1.0); break;
    case kTopologyYParamId: t.dirY = std::clamp(value, -1.0, 1.0); break;
    case kTopologyZParamId: t.dirZ = std::clamp(value, -1.0, 1.0); break;
    case kTopologyTwistParamId: t.twist = std::clamp(value, -1.0, 1.0); break;
    case kTopologyFlareParamId: t.flare = std::clamp(value, -1.0, 1.0); break;
    case kTopologyMotionParamId: t.motionMode = std::min<uint32_t>(s3g::kTopologyMotionModeCount - 1u, static_cast<uint32_t>(std::floor(value + 0.5))); break;
    case kTopologyVariantParamId: t.motionVariant = std::min<uint32_t>(s3g::kTopologyVariantCount - 1u, static_cast<uint32_t>(std::floor(value + 0.5))); break;
    case kTopologyRateParamId: t.motionRateHz = clampMotionRate(value); break;
    case kTopologyDepthParamId: t.motionDepth = std::clamp(value, 0.0, 1.0); break;
    case kTopologyNeighborsParamId: t.neighborCount = std::clamp<uint32_t>(static_cast<uint32_t>(std::floor(value + 0.5)), 1u, 3u); break;
    case kTopologyRadiusParamId: t.neighborRadius = std::clamp(value, 0.0, 1.0); break;
    case kTopologyCentroidParamId: t.centroidAmount = std::clamp(value, 0.0, 1.0); break;
    default: break;
    }
    applyLaneParams(p);
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
    p->input32.assign(kChannelCount, std::vector<float>(p->maxFrames, 0.0f));
    p->output32.assign(kChannelCount, std::vector<float>(p->maxFrames, 0.0f));
    p->inputPtrs.assign(kChannelCount, nullptr);
    p->outputPtrs.assign(kChannelCount, nullptr);
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p->inputPtrs[ch] = p->input32[ch].data();
        p->outputPtrs[ch] = p->output32[ch].data();
    }
    if (!p->processor.prepare(sampleRate, kChannelCount, 2048u, 4u, p->maxFrames)) return false;
    applyLaneParams(*p);
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

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = std::min(proc->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kChannelCount) return CLAP_PROCESS_CONTINUE;

    advanceTopologyMotion(*p, frames);

    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            if (ch < input.channel_count && input.data32 && input.data32[ch]) p->input32[ch][i] = input.data32[ch][i];
            else if (ch < input.channel_count && input.data64 && input.data64[ch]) p->input32[ch][i] = static_cast<float>(input.data64[ch][i]);
            else p->input32[ch][i] = 0.0f;
        }
    }
    p->processor.process(p->inputPtrs.data(), kChannelCount, p->outputPtrs.data(), kChannelCount, frames);

    float blockPeak = 0.0f;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        for (uint32_t i = 0; i < frames; ++i) {
            const float v = p->output32[ch][i];
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = v;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(v);
            blockPeak = std::max(blockPeak, std::abs(v));
        }
    }
    for (uint32_t ch = kChannelCount; ch < output.channel_count; ++ch) {
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
    { kSprayBinsParamId, "Spray Bins", 0.0, 192.0, 18.0 },
    { kDriftParamId, "Drift", 0.0, 1.0, 0.18 },
    { kHoldParamId, "Hold", 0.0, 1.0, 0.72 },
    { kFreezeParamId, "Freeze", 0.0, 0.92, 0.0 },
    { kFeedbackParamId, "Feedback", 0.0, 0.72, 0.18 },
    { kSmearParamId, "Smear", 0.0, 1.0, 0.25 },
    { kHolesParamId, "Holes", 0.0, 0.60, 0.05 },
    { kPhaseBlurParamId, "Phase Blur", 0.0, 1.0, 0.28 },
    { kTiltParamId, "Tilt", -1.0, 1.0, 0.0 },
    { kMixParamId, "Mix", 0.0, 1.0, 1.0 },
    { kGainParamId, "Output", -60.0, 12.0, -2.5 },
    { kSafetyParamId, "Safety", 0.12, 0.92, 0.82 },
    { kTopologyShapeParamId, "Topology Shape", 0.0, static_cast<double>(s3g::kTopologyShapeCount - 1u), 0.0 },
    { kTopologyAmountParamId, "Topology Amount", 0.0, 1.0, 0.35 },
    { kTopologySeedParamId, "Topology Seed", 0.0, 1.0, 0.08 },
    { kTopologyPullParamId, "Topology Pull", 0.0, 1.0, 0.0 },
    { kTopologyXParamId, "Topology X", -1.0, 1.0, 0.0 },
    { kTopologyYParamId, "Topology Y", -1.0, 1.0, 0.0 },
    { kTopologyZParamId, "Topology Z", -1.0, 1.0, 1.0 },
    { kTopologyTwistParamId, "Topology Twist", -1.0, 1.0, 0.08 },
    { kTopologyFlareParamId, "Topology Flare", -1.0, 1.0, 0.0 },
    { kTopologyMotionParamId, "Topology Motion", 0.0, static_cast<double>(s3g::kTopologyMotionModeCount - 1u), 0.0 },
    { kTopologyVariantParamId, "Topology Variant", 0.0, static_cast<double>(s3g::kTopologyVariantCount - 1u), 0.0 },
    { kTopologyRateParamId, "Topology Rate", kMotionRateMinHz, kMotionRateMaxHz, 0.08 },
    { kTopologyDepthParamId, "Topology Depth", 0.0, 1.0, 0.0 },
    { kTopologyNeighborsParamId, "Topology Neighbors", 1.0, 3.0, 2.0 },
    { kTopologyRadiusParamId, "Topology Radius", 0.0, 1.0, 0.65 },
    { kTopologyCentroidParamId, "Topology Centroid", 0.0, 1.0, 0.18 },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, def.id < kTopologyShapeParamId ? "Spectral Engine" : "Topology", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& p = *self(plugin);
    const auto& prm = p.settings.base;
    const auto& t = p.settings.topology;
    switch (id) {
    case kSprayBinsParamId: *value = prm.sprayBins; return true;
    case kDriftParamId: *value = prm.drift; return true;
    case kHoldParamId: *value = prm.hold; return true;
    case kFreezeParamId: *value = prm.freeze; return true;
    case kFeedbackParamId: *value = prm.feedback; return true;
    case kSmearParamId: *value = prm.smear; return true;
    case kHolesParamId: *value = prm.holes; return true;
    case kPhaseBlurParamId: *value = prm.phaseBlur; return true;
    case kTiltParamId: *value = prm.tilt; return true;
    case kMixParamId: *value = prm.mix; return true;
    case kGainParamId: *value = prm.gainDb; return true;
    case kSafetyParamId: *value = prm.safety; return true;
    case kTopologyShapeParamId: *value = t.shape; return true;
    case kTopologyAmountParamId: *value = t.amount; return true;
    case kTopologySeedParamId: *value = t.jitter; return true;
    case kTopologyPullParamId: *value = t.collapse; return true;
    case kTopologyXParamId: *value = t.dirX; return true;
    case kTopologyYParamId: *value = t.dirY; return true;
    case kTopologyZParamId: *value = t.dirZ; return true;
    case kTopologyTwistParamId: *value = t.twist; return true;
    case kTopologyFlareParamId: *value = t.flare; return true;
    case kTopologyMotionParamId: *value = t.motionMode; return true;
    case kTopologyVariantParamId: *value = t.motionVariant; return true;
    case kTopologyRateParamId: *value = t.motionRateHz; return true;
    case kTopologyDepthParamId: *value = t.motionDepth; return true;
    case kTopologyNeighborsParamId: *value = t.neighborCount; return true;
    case kTopologyRadiusParamId: *value = t.neighborRadius; return true;
    case kTopologyCentroidParamId: *value = t.centroidAmount; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kSprayBinsParamId) std::snprintf(display, size, "%.0f bins", value);
    else if (id == kGainParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kTiltParamId || id == kTopologyXParamId || id == kTopologyYParamId ||
             id == kTopologyZParamId || id == kTopologyTwistParamId || id == kTopologyFlareParamId) std::snprintf(display, size, "%+.2f", value);
    else if (id == kTopologyShapeParamId) std::snprintf(display, size, "%s", s3g::topologyShapeName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyMotionParamId) std::snprintf(display, size, "%s", s3g::topologyMotionModeName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyVariantParamId) std::snprintf(display, size, "%s", s3g::topologyVariantName(static_cast<uint32_t>(std::floor(value + 0.5))));
    else if (id == kTopologyNeighborsParamId) std::snprintf(display, size, "%.0fNN", value);
    else if (id == kTopologyRateParamId) std::snprintf(display, size, "%.2f Hz", value);
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
    s.settings = self(plugin)->settings;
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->settings = s.settings;
    applyLaneParams(*p);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };
uint32_t latencyGet(const clap_plugin_t* plugin) { return self(plugin)->processor.latencyFrames(); }
const clap_plugin_latency_t latencyExt { latencyGet };

} // namespace

#if defined(__APPLE__)
@interface S3GSpectralTopologyView : NSView { void* _plugin; int _dragParam; NSTimer* _timer; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawEngineRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)updateDrag:(NSPoint)point;
@end

@implementation S3GSpectralTopologyView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) { _plugin = plugin; _dragParam = 0; _timer = nil; }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0/24.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}
- (void)drawEngineRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, small, style, 654, 750, 920, 150);
}
- (NSPoint)projectPoint:(s3g::TopologyPoint)point inRect:(NSRect)rect
{
    const CGFloat cx = NSMidX(rect);
    const CGFloat cy = NSMidY(rect) + 8.0;
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.34;
    const CGFloat px = cx + static_cast<CGFloat>(point.x * 0.78 + point.z * 0.36) * scale;
    const CGFloat py = cy + static_cast<CGFloat>(-point.y * 0.76 + point.z * 0.18) * scale;
    return NSMakePoint(px, py);
}
- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.cellBg setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);
    [@"SPECTRAL TOPOLOGY" drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + 10) withAttributes:attrs];

    const auto state = topologyStateForPlugin(*p);
    const auto controls = s3g::topologyControlsFromState(state);
    const uint32_t cols = 56;
    const uint32_t rows = 34;
    const CGFloat cellW = rect.size.width / static_cast<CGFloat>(cols);
    const CGFloat cellH = (rect.size.height - 46.0) / static_cast<CGFloat>(rows);
    const CGFloat y0 = rect.origin.y + 36.0;
    for (uint32_t y = 0; y < rows; ++y) {
        for (uint32_t x = 0; x < cols; ++x) {
            const double nx = (static_cast<double>(x) + 0.5) / static_cast<double>(cols) * 2.0 - 1.0;
            const double ny = (static_cast<double>(y) + 0.5) / static_cast<double>(rows) * 2.0 - 1.0;
            double sum = 0.0;
            for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
                const auto pt = s3g::topologyPointForLane(lane, kChannelCount, controls);
                const double px = pt.x * 0.78 + pt.z * 0.36;
                const double py = -pt.y * 0.76 + pt.z * 0.18;
                const double dx = nx - px;
                const double dy = ny - py;
                const double laneEnergy = 0.30 + std::clamp(pt.radius, 0.0, 1.8) * 0.28 + std::fabs(pt.lane) * 0.12;
                sum += laneEnergy * std::exp(-(dx * dx + dy * dy) * 8.0);
            }
            const double v = std::clamp(sum * 0.38, 0.0, 1.0);
            [s3g::clap_gui::heatColor(v, 0.74) setFill];
            NSRectFill(NSMakeRect(rect.origin.x + static_cast<CGFloat>(x) * cellW,
                                  y0 + static_cast<CGFloat>(y) * cellH,
                                  cellW + 0.35,
                                  cellH + 0.35));
        }
    }

    [s3g::clap_gui::color(0xb8b8b8, 0.60) setStroke];
    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        const auto pt = s3g::topologyPointForLane(lane, kChannelCount, controls);
        const NSPoint a = [self projectPoint:pt inRect:rect];
        const auto nn = s3g::nearestTopologyNeighbors(state, lane, kChannelCount);
        for (uint32_t i = 0; i < std::min<uint32_t>(state.neighborCount, 3u); ++i) {
            if (nn[i] < 0 || static_cast<uint32_t>(nn[i]) <= lane) continue;
            const auto other = s3g::topologyPointForLane(static_cast<uint32_t>(nn[i]), kChannelCount, controls);
            const NSPoint b = [self projectPoint:other inRect:rect];
            [NSBezierPath strokeLineFromPoint:a toPoint:b];
        }
    }
    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        const auto pt = s3g::topologyPointForLane(lane, kChannelCount, controls);
        const NSPoint c = [self projectPoint:pt inRect:rect];
        const CGFloat size = 8.0 + static_cast<CGFloat>(std::clamp(pt.radius, 0.0, 1.5)) * 2.0;
        [style.text setFill];
        NSRectFill(NSMakeRect(c.x - size * 0.5, c.y - size * 0.5, size, size));
        [[NSString stringWithFormat:@"L%u", lane + 1u] drawAtPoint:NSMakePoint(c.x + 7.0, c.y - 6.0) withAttributes:small];
    }

    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PK %+4.1f dB", 20.0 * std::log10(std::max(0.000001f, pk))]
        drawAtPoint:NSMakePoint(NSMaxX(rect) - 92.0, rect.origin.y + 10.0) withAttributes:small];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSDictionary* lab = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:titleFont };
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    [@"s3g SPECTRAL TOPOLOGY PROCESSOR" drawAtPoint:NSMakePoint(18, 14) withAttributes:lab];
    [@"8CH" drawAtPoint:NSMakePoint(944, 14) withAttributes:small];

    [self drawField:NSMakeRect(18, 42, 600, 560) attrs:lab small:small];

    s3g::clap_gui::drawPanelFrame(638, 42, 344, 208, style);
    s3g::clap_gui::drawPanelHeader(@"SPECTRAL ENGINE", true, 638, 42, 344, 21, lab, style);
    const auto& prm = p->settings.base;
    [self drawEngineRow:@"BINS" value:[NSString stringWithFormat:@"%.0f", prm.sprayBins] norm:prm.sprayBins / 192.0f y:76 attrs:small small:small];
    [self drawEngineRow:@"DRFT" value:[NSString stringWithFormat:@"%.0f%%", prm.drift * 100.0f] norm:prm.drift y:94 attrs:small small:small];
    [self drawEngineRow:@"HOLD" value:[NSString stringWithFormat:@"%.0f%%", prm.hold * 100.0f] norm:prm.hold y:112 attrs:small small:small];
    [self drawEngineRow:@"FRZ" value:[NSString stringWithFormat:@"%.0f%%", prm.freeze * 100.0f] norm:prm.freeze / 0.92f y:130 attrs:small small:small];
    [self drawEngineRow:@"FDBK" value:[NSString stringWithFormat:@"%.0f%%", prm.feedback * 100.0f] norm:prm.feedback / 0.72f y:148 attrs:small small:small];
    [self drawEngineRow:@"SMR" value:[NSString stringWithFormat:@"%.0f%%", prm.smear * 100.0f] norm:prm.smear y:166 attrs:small small:small];
    [self drawEngineRow:@"HOLE" value:[NSString stringWithFormat:@"%.0f%%", prm.holes * 100.0f] norm:prm.holes / 0.60f y:184 attrs:small small:small];
    [self drawEngineRow:@"PHAS" value:[NSString stringWithFormat:@"%.0f%%", prm.phaseBlur * 100.0f] norm:prm.phaseBlur y:202 attrs:small small:small];
    [self drawEngineRow:@"TILT" value:[NSString stringWithFormat:@"%+.2f", prm.tilt] norm:(prm.tilt + 1.0f) * 0.5f y:220 attrs:small small:small];
    [self drawEngineRow:@"MIX" value:[NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f] norm:prm.mix y:238 attrs:small small:small];

    s3g::clap_gui::drawPanelFrame(638, 264, 344, 338, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY", true, 638, 264, 344, 21, lab, style);
    const auto& t = p->settings.topology;
    s3g::clap_gui::TopologyUiValues values;
    values.shape = s3g::topologyShapeName(t.shape);
    values.amount = t.amount;
    values.pull = t.collapse;
    values.x = t.dirX;
    values.y = t.dirY;
    values.z = t.dirZ;
    values.twist = t.twist;
    values.flare = t.flare;
    values.seed = t.jitter;
    values.motion = s3g::topologyMotionModeName(t.motionMode);
    values.variant = s3g::topologyVariantName(t.motionVariant);
    values.rateHz = t.motionRateHz;
    values.rateMinHz = kMotionRateMinHz;
    values.rateMaxHz = kMotionRateMaxHz;
    values.depth = t.motionDepth;
    values.neighbors = t.neighborCount;
    values.radius = t.neighborRadius;
    values.centroid = t.centroidAmount;
    s3g::clap_gui::drawTopologyRows(values, 286, small, small, style);
    [@"FFT 2048 / 4x OLA  |  X tilt  Y freeze/smear  Z phase/fdbk  RAD intensity"
        drawAtPoint:NSMakePoint(656, 590) withAttributes:small];
}
- (void)updateDrag:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((point.x - 750.0) / 150.0, 0.0, 1.0);
    switch (_dragParam) {
    case kSprayBinsParamId: applyParam(*p, kSprayBinsParamId, n * 192.0); break;
    case kDriftParamId: applyParam(*p, kDriftParamId, n); break;
    case kHoldParamId: applyParam(*p, kHoldParamId, n); break;
    case kFreezeParamId: applyParam(*p, kFreezeParamId, n * 0.92); break;
    case kFeedbackParamId: applyParam(*p, kFeedbackParamId, n * 0.72); break;
    case kSmearParamId: applyParam(*p, kSmearParamId, n); break;
    case kHolesParamId: applyParam(*p, kHolesParamId, n * 0.60); break;
    case kPhaseBlurParamId: applyParam(*p, kPhaseBlurParamId, n); break;
    case kTiltParamId: applyParam(*p, kTiltParamId, -1.0 + n * 2.0); break;
    case kMixParamId: applyParam(*p, kMixParamId, n); break;
    case kTopologyAmountParamId: applyParam(*p, kTopologyAmountParamId, n); break;
    case kTopologyPullParamId: applyParam(*p, kTopologyPullParamId, n); break;
    case kTopologyXParamId: applyParam(*p, kTopologyXParamId, -1.0 + n * 2.0); break;
    case kTopologyYParamId: applyParam(*p, kTopologyYParamId, -1.0 + n * 2.0); break;
    case kTopologyZParamId: applyParam(*p, kTopologyZParamId, -1.0 + n * 2.0); break;
    case kTopologyTwistParamId: applyParam(*p, kTopologyTwistParamId, -1.0 + n * 2.0); break;
    case kTopologyFlareParamId: applyParam(*p, kTopologyFlareParamId, -1.0 + n * 2.0); break;
    case kTopologySeedParamId: applyParam(*p, kTopologySeedParamId, n); break;
    case kTopologyRateParamId: applyParam(*p, kTopologyRateParamId, kMotionRateMinHz + n * (kMotionRateMaxHz - kMotionRateMinHz)); break;
    case kTopologyDepthParamId: applyParam(*p, kTopologyDepthParamId, n); break;
    case kTopologyRadiusParamId: applyParam(*p, kTopologyRadiusParamId, n); break;
    case kTopologyCentroidParamId: applyParam(*p, kTopologyCentroidParamId, n); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat engineRows[] = {76,94,112,130,148,166,184,202,220,238};
    const clap_id engineIds[] = {kSprayBinsParamId,kDriftParamId,kHoldParamId,kFreezeParamId,kFeedbackParamId,kSmearParamId,kHolesParamId,kPhaseBlurParamId,kTiltParamId,kMixParamId};
    for (uint32_t i = 0; i < 10u; ++i) {
        if (NSPointInRect(pt, NSMakeRect(648, engineRows[i] - 7, 324, 18))) {
            _dragParam = static_cast<int>(engineIds[i]);
            [self updateDrag:pt];
            return;
        }
    }

    const auto row = s3g::clap_gui::hitTopologyRow(pt, 286);
    auto* p = static_cast<Plugin*>(_plugin);
    switch (row) {
    case s3g::clap_gui::TopologyRow::Shape:
        applyParam(*p, kTopologyShapeParamId, (p->settings.topology.shape + 1u) % s3g::kTopologyShapeCount); [self setNeedsDisplay:YES]; return;
    case s3g::clap_gui::TopologyRow::Motion:
        applyParam(*p, kTopologyMotionParamId, (p->settings.topology.motionMode + 1u) % s3g::kTopologyMotionModeCount); [self setNeedsDisplay:YES]; return;
    case s3g::clap_gui::TopologyRow::Variant:
        applyParam(*p, kTopologyVariantParamId, (p->settings.topology.motionVariant + 1u) % s3g::kTopologyVariantCount); [self setNeedsDisplay:YES]; return;
    case s3g::clap_gui::TopologyRow::Neighbors:
        applyParam(*p, kTopologyNeighborsParamId, p->settings.topology.neighborCount >= 3u ? 1.0 : p->settings.topology.neighborCount + 1u); [self setNeedsDisplay:YES]; return;
    case s3g::clap_gui::TopologyRow::Amount: _dragParam = kTopologyAmountParamId; break;
    case s3g::clap_gui::TopologyRow::Pull: _dragParam = kTopologyPullParamId; break;
    case s3g::clap_gui::TopologyRow::X: _dragParam = kTopologyXParamId; break;
    case s3g::clap_gui::TopologyRow::Y: _dragParam = kTopologyYParamId; break;
    case s3g::clap_gui::TopologyRow::Z: _dragParam = kTopologyZParamId; break;
    case s3g::clap_gui::TopologyRow::Twist: _dragParam = kTopologyTwistParamId; break;
    case s3g::clap_gui::TopologyRow::Flare: _dragParam = kTopologyFlareParamId; break;
    case s3g::clap_gui::TopologyRow::Seed: _dragParam = kTopologySeedParamId; break;
    case s3g::clap_gui::TopologyRow::Rate: _dragParam = kTopologyRateParamId; break;
    case s3g::clap_gui::TopologyRow::Depth: _dragParam = kTopologyDepthParamId; break;
    case s3g::clap_gui::TopologyRow::Radius: _dragParam = kTopologyRadiusParamId; break;
    case s3g::clap_gui::TopologyRow::Centroid: _dragParam = kTopologyCentroidParamId; break;
    default: _dragParam = 0; break;
    }
    if (_dragParam != 0) [self updateDrag:pt];
}
- (void)mouseDragged:(NSEvent*)event { if (_dragParam != 0) [self updateDrag:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragParam = 0; }
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GSpectralTopologyView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3GSpectralTopologyView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiWidth,kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GSpectralTopologyView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GSpectralTopologyView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
} // namespace
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.spectral-topology-processor",
    "s3g Spectral Topology Processor 8ch",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "8-channel topology-driven spectral processor built around the Spectral Spray engine.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->settings.base.sprayBins = 18.0f;
    p->settings.base.drift = 0.18f;
    p->settings.base.hold = 0.72f;
    p->settings.base.freeze = 0.0f;
    p->settings.base.feedback = 0.18f;
    p->settings.base.smear = 0.25f;
    p->settings.base.holes = 0.05f;
    p->settings.base.phaseBlur = 0.28f;
    p->settings.base.gainDb = -2.5f;
    p->settings.base.mix = 1.0f;
    p->settings.base.tilt = 0.0f;
    p->settings.base.safety = 0.82f;
    p->settings.topology.amount = 0.35;
    p->settings.topology.jitter = 0.08;
    p->settings.topology.collapse = 0.0;
    p->settings.topology.dirX = 0.0;
    p->settings.topology.dirY = 0.0;
    p->settings.topology.dirZ = 1.0;
    p->settings.topology.twist = 0.08;
    p->settings.topology.flare = 0.0;
    p->settings.topology.shape = 0;
    p->settings.topology.motionMode = 0;
    p->settings.topology.motionVariant = 0;
    p->settings.topology.motionRateHz = 0.08;
    p->settings.topology.motionDepth = 0.0;
    p->settings.topology.neighborCount = 2;
    p->settings.topology.neighborRadius = 0.65;
    p->settings.topology.centroidAmount = 0.18;
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
