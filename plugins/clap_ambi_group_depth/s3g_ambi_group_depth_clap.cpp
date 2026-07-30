#include "s3g_ambi_group_depth.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#if defined(__APPLE__)
#include <clap/ext/gui.h>
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

#if defined(S3G_AMBI_DEPTH_16)
#define S3G_AMBI_GROUP_DEPTH_VIEW_CLASS S3GAmbiDepth16View
constexpr uint32_t kGroups = 1;
constexpr uint32_t kChannels = 16;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-depth-16";
constexpr const char* kHostName = "s3g Ambi Transform Depth 16";
constexpr const char* kPluginDesc = "16-channel 3OA ambisonic depth utility.";
constexpr const char* kHeaderTitle = "s3g AMBI TRANSFORM DEPTH 16CH";
constexpr const char* kHeaderInfo = "3OA / 16CH";
constexpr bool kSingleField = true;
#elif defined(S3G_AMBI_GROUP_DEPTH_128)
#define S3G_AMBI_GROUP_DEPTH_VIEW_CLASS S3GAmbiGroupDepth128View
constexpr uint32_t kGroups = 8;
constexpr uint32_t kChannels = 128;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-group-depth-128";
constexpr const char* kHostName = "s3g Ambi Transform Grp Depth 128";
constexpr const char* kPluginDesc = "128-channel lane-locked 8x3OA group depth utility.";
constexpr const char* kHeaderTitle = "s3g AMBI TRANSFORM GROUP DEPTH 128CH";
constexpr const char* kHeaderInfo = "8 x 3OA / 128CH";
constexpr bool kSingleField = false;
#else
#define S3G_AMBI_GROUP_DEPTH_VIEW_CLASS S3GAmbiGroupDepth64View
constexpr uint32_t kGroups = 4;
constexpr uint32_t kChannels = 64;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-group-depth-64";
constexpr const char* kHostName = "s3g Ambi Transform Grp Depth 64";
constexpr const char* kPluginDesc = "64-channel lane-locked 4x3OA group depth utility.";
constexpr const char* kHeaderTitle = "s3g AMBI TRANSFORM GROUP DEPTH 64CH";
constexpr const char* kHeaderInfo = "4 x 3OA / 64CH";
constexpr bool kSingleField = false;
#endif

constexpr uint32_t kGuiWidth = 820;
constexpr uint32_t kGuiHeight = 496u;
constexpr uint32_t kStateVersion = 3;

enum ParamId : clap_id {
    kParamDepth = 1,
    kParamSpread = 2,
    kParamFocus = 3,
    kParamAir = 4,
    kParamLow = 5,
    kParamWidth = 6,
    kParamOutput = 7,
    kParamTail = 8,
    kParamEnvironmentSize = 9,
    kParamEnvironmentDecay = 10,
    kParamEnvironmentDamping = 11,
};

using Processor = s3g::AmbiGroupDepthProcessor<kGroups>;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiGroupDepthParams params {};
};

struct OldAmbiGroupDepthParamsV1 {
    float depth = 0.0f;
    float spread = 0.0f;
    float focus = 0.0f;
    float air = 0.0f;
    float low = 0.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
};

struct OldSavedStateV1 {
    uint32_t version = 1;
    OldAmbiGroupDepthParamsV1 params {};
};

struct OldAmbiGroupDepthParamsV2 {
    float depth = 0.0f;
    float spread = 0.0f;
    float focus = 0.0f;
    float air = 0.0f;
    float tail = 0.0f;
    float low = 0.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
};

struct OldSavedStateV2 {
    uint32_t version = 2;
    OldAmbiGroupDepthParamsV2 params {};
};

bool streamWriteAll(const clap_ostream_t* stream, const void* source, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    size_t position = 0u;
    while (position < size) {
        const int64_t wrote = stream->write(stream, bytes + position, size - position);
        if (wrote <= 0) return false;
        position += static_cast<size_t>(wrote);
    }
    return true;
}

bool streamReadAll(const clap_istream_t* stream, void* destination, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(destination);
    size_t position = 0u;
    while (position < size) {
        const int64_t got = stream->read(stream, bytes + position, size - position);
        if (got <= 0) return false;
        position += static_cast<size_t>(got);
    }
    return true;
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    s3g::AmbiGroupDepthParams params {};
    Processor processor {};
    double sampleRate = 48000.0;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    char presetName[64] { "INIT" };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kParamDepth: p.params.depth = static_cast<float>(value); break;
    case kParamSpread: p.params.spread = static_cast<float>(value); break;
    case kParamFocus: p.params.focus = static_cast<float>(value); break;
    case kParamAir: p.params.air = static_cast<float>(value); break;
    case kParamTail: p.params.tail = static_cast<float>(value); break;
    case kParamLow: p.params.low = static_cast<float>(value); break;
    case kParamWidth: p.params.width = static_cast<float>(value); break;
    case kParamOutput: p.params.outputGainDb = static_cast<float>(value); break;
    case kParamEnvironmentSize: p.params.environmentSize = static_cast<float>(value); break;
    case kParamEnvironmentDecay: p.params.environmentDecay = static_cast<float>(value); break;
    case kParamEnvironmentDamping: p.params.environmentDamping = static_cast<float>(value); break;
    default: return;
    }
    p.params = s3g::sanitizeAmbiGroupDepthParams(p.params);
    p.processor.setParams(p.params);
    if (p.hostTail && p.host) {
        p.hostTail->changed(p.host);
    }
}

double getParam(const Plugin& p, clap_id id)
{
    switch (id) {
    case kParamDepth: return p.params.depth;
    case kParamSpread: return p.params.spread;
    case kParamFocus: return p.params.focus;
    case kParamAir: return p.params.air;
    case kParamTail: return p.params.tail;
    case kParamLow: return p.params.low;
    case kParamWidth: return p.params.width;
    case kParamOutput: return p.params.outputGainDb;
    case kParamEnvironmentSize: return p.params.environmentSize;
    case kParamEnvironmentDecay: return p.params.environmentDecay;
    case kParamEnvironmentDamping: return p.params.environmentDamping;
    default: return 0.0;
    }
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(
            p->guiViewport, p->guiView);
    }
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->params = s3g::sanitizeAmbiGroupDepthParams(p->params);
    p->processor.prepare(sampleRate);
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
    p.processor.process(in, inChannels, out, outChannels, frames);
    s3g::clearAudioBufferFromChannel(output, kChannels, frames);
    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outChannels; ++ch) {
        if (!out[ch]) continue;
        for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, static_cast<float>(std::abs(out[ch][i])));
    }
    p.outputPeak.store(std::max(p.outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

template <typename Sample>
clap_process_status processTailOnly(Plugin& p, const clap_audio_buffer_t& output, uint32_t frames, Sample** out)
{
    s3g::clearAudioBuffer(output, frames);
    if (!out) return CLAP_PROCESS_CONTINUE;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kChannels);
    p.processor.process<Sample>(nullptr, kChannels, out, outChannels, frames);
    s3g::clearAudioBufferFromChannel(output, kChannels, frames);
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
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& output = proc->audio_outputs[0];
    if (proc->audio_inputs_count == 0) {
        if constexpr (kSingleField) {
            if (output.data32) return processTailOnly<float>(*p, output, proc->frames_count, output.data32);
            if (output.data64) return processTailOnly<double>(*p, output, proc->frames_count, output.data64);
        }
        s3g::clearAudioBuffer(output, proc->frames_count);
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = proc->audio_inputs[0];
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
    std::strncpy(info->name,
        isInput
            ? (kSingleField ? "Ambi Depth In" : "Group Depth In")
            : (kSingleField ? "Ambi Depth Out" : "Group Depth Out"),
        sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

bool isParamId(clap_id paramId)
{
    if constexpr (kSingleField) {
        return paramId == kParamDepth
            || paramId == kParamFocus
            || paramId == kParamAir
            || paramId == kParamTail
            || paramId == kParamLow
            || paramId == kParamWidth
            || paramId == kParamOutput
            || paramId == kParamEnvironmentSize
            || paramId == kParamEnvironmentDecay
            || paramId == kParamEnvironmentDamping;
    }
    return paramId >= kParamDepth && paramId <= kParamTail;
}

uint32_t paramsCount(const clap_plugin_t*) { return kSingleField ? 10u : 8u; }

const char* paramModule(clap_id id)
{
    if (id == kParamOutput) return "Output";
    if constexpr (kSingleField) {
        if (id == kParamTail || id == kParamEnvironmentSize
            || id == kParamEnvironmentDecay || id == kParamEnvironmentDamping) {
            return "Environment Field";
        }
    }
    return "Depth";
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if constexpr (kSingleField) {
        switch (index) {
        case 0: info->id = kParamDepth; std::strncpy(info->name, "Depth", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
        case 1: info->id = kParamFocus; std::strncpy(info->name, "Focus", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
        case 2: info->id = kParamAir; std::strncpy(info->name, "Air Damping", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
        case 3: info->id = kParamTail; std::strncpy(info->name, "Tail", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0; break;
        case 4: info->id = kParamEnvironmentSize; std::strncpy(info->name, "Env Size", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.5; break;
        case 5: info->id = kParamEnvironmentDecay; std::strncpy(info->name, "Env Decay", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.5; break;
        case 6: info->id = kParamEnvironmentDamping; std::strncpy(info->name, "Env Damping", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0.5; break;
        case 7: info->id = kParamLow; std::strncpy(info->name, "Low Body", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
        case 8: info->id = kParamWidth; std::strncpy(info->name, "Order Width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; break;
        case 9: info->id = kParamOutput; std::strncpy(info->name, "Output", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; break;
        default: return false;
        }
        std::strncpy(info->module, paramModule(info->id), sizeof(info->module));
        return true;
    }
    switch (index) {
    case 0: info->id = kParamDepth; std::strncpy(info->name, "Depth", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
    case 1: info->id = kParamSpread; std::strncpy(info->name, "Group Spread", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
    case 2: info->id = kParamFocus; std::strncpy(info->name, "Focus", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
    case 3: info->id = kParamAir; std::strncpy(info->name, "Air Damping", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
    case 4: info->id = kParamTail; std::strncpy(info->name, "Tail", sizeof(info->name)); info->min_value = 0; info->max_value = 1; info->default_value = 0; break;
    case 5: info->id = kParamLow; std::strncpy(info->name, "Low Body", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; break;
    case 6: info->id = kParamWidth; std::strncpy(info->name, "Order Width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; break;
    case 7: info->id = kParamOutput; std::strncpy(info->name, "Output", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; break;
    default: return false;
    }
    std::strncpy(info->module, paramModule(info->id), sizeof(info->module));
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParam(*self(plugin), paramId);
    return isParamId(paramId);
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    switch (paramId) {
    case kParamDepth:
    case kParamSpread:
    case kParamFocus:
    case kParamAir:
    case kParamLow: std::snprintf(display, size, "%+.0f%%", value * 100.0); return true;
    case kParamTail:
    case kParamEnvironmentSize:
    case kParamEnvironmentDecay:
    case kParamEnvironmentDamping: std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    case kParamWidth: std::snprintf(display, size, "%.2f", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    default: return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
    if (!isParamId(paramId)) return false;
    double parsed = std::atof(display);
    switch (paramId) {
    case kParamDepth:
    case kParamSpread:
    case kParamFocus:
    case kParamAir:
    case kParamLow:
    case kParamTail:
    case kParamEnvironmentSize:
    case kParamEnvironmentDecay:
    case kParamEnvironmentDamping: parsed *= 0.01; break;
    default: break;
    }
    *value = parsed;
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state { kStateVersion, self(plugin)->params };
    return streamWriteAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!streamReadAll(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        auto* remainder = reinterpret_cast<uint8_t*>(&state) + sizeof(version);
        if (!streamReadAll(stream, remainder, sizeof(state) - sizeof(version))) return false;
        p->params = s3g::sanitizeAmbiGroupDepthParams(state.params);
    } else if (version == 2u) {
        OldSavedStateV2 old {};
        old.version = version;
        auto* remainder = reinterpret_cast<uint8_t*>(&old) + sizeof(version);
        if (!streamReadAll(stream, remainder, sizeof(old) - sizeof(version))) return false;
        p->params = s3g::sanitizeAmbiGroupDepthParams({
            old.params.depth,
            old.params.spread,
            old.params.focus,
            old.params.air,
            old.params.tail,
            old.params.low,
            old.params.width,
            old.params.outputGainDb,
        });
    } else if (version == 1u) {
        OldSavedStateV1 old {};
        old.version = version;
        auto* remainder = reinterpret_cast<uint8_t*>(&old) + sizeof(version);
        if (!streamReadAll(stream, remainder, sizeof(old) - sizeof(version))) return false;
        p->params = s3g::sanitizeAmbiGroupDepthParams({
            old.params.depth,
            old.params.spread,
            old.params.focus,
            old.params.air,
            0.0f,
            old.params.low,
            old.params.width,
            old.params.outputGainDb,
        });
    } else {
        return false;
    }
    p->processor.setParams(p->params);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p || p->params.tail <= 0.0001f) return 0u;
    const double amount = std::clamp(static_cast<double>(p->params.tail), 0.0, 1.0);
    const double far = std::max(0.0, static_cast<double>(p->params.depth));
    const double decayScale = static_cast<double>(s3g::ambiEnvironmentDecayScale(p->params.environmentDecay));
    const double sizeScale = static_cast<double>(s3g::ambiEnvironmentSizeScale(p->params.environmentSize));
    const double tailSeconds = kSingleField
        ? std::clamp(0.35 + amount * (1.42 * decayScale + 0.22 * sizeScale) + far * 0.35, 0.5, 8.0)
        : std::clamp(1.5 + amount * 7.0 + far * 3.0, 1.0, 12.0);
    return static_cast<uint32_t>(std::ceil(tailSeconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
namespace {
constexpr const auto& kTransformLayout =
    s3g::gui_layout::kTransformFamilyLayout;
}

@interface S3G_AMBI_GROUP_DEPTH_VIEW_CLASS : NSView {
    void* _plugin;
    int _dragSlider;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)updateSliderAtPoint:(NSPoint)pt;
@end

@implementation S3G_AMBI_GROUP_DEPTH_VIEW_CLASS
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) { _plugin = plugin; _dragSlider = -1; _refreshTimer = nil; }
    return self;
}
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (BOOL)isFlipped { return YES; }
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
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    const float pk = p->outputPeak.exchange(p->outputPeak.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    const auto titleBand =
        s3g::gui_layout::transformTitleBand(kTransformLayout.canvas);
    s3g::clap_gui::drawTransformTitleBand(
        @(kHeaderTitle),
        [NSString stringWithUTF8String:p->presetName],
        s3g::clap_gui::peakDbText(pk), titleBand, style);

    NSRect fieldPanel =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPanel);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(kSingleField ? @"DEPTH FIELD" : @"GROUP DEPTH FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    NSRect field =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPlot);
    [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);

    [s3g::clap_gui::color(0x575757, 0.34) setStroke];
    for (int i = 1; i < 4; ++i) {
        const CGFloat y = field.origin.y + field.size.height * static_cast<CGFloat>(i) / 4.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 14, y) toPoint:NSMakePoint(NSMaxX(field) - 14, y)];
    }
    [@"NEAR / DIRECT" drawAtPoint:NSMakePoint(field.origin.x + 14, field.origin.y + 10) withAttributes:small];
    [@"NEUTRAL" drawAtPoint:NSMakePoint(
        NSMaxX(field) - 62,
        field.origin.y + field.size.height * 0.5 - 25)
        withAttributes:small];
    [@"FAR / SOFT" drawAtPoint:NSMakePoint(field.origin.x + 14, NSMaxY(field) - 22) withAttributes:small];

    NSPoint points[kGroups] {};
    for (uint32_t group = 0; group < kGroups; ++group) {
        const auto state = p->processor.groupState(group);
        const CGFloat u = kGroups <= 1 ? 0.5 : (static_cast<CGFloat>(group) + 0.5) / static_cast<CGFloat>(kGroups);
        const CGFloat y = field.origin.y + 42.0 + static_cast<CGFloat>(state.depth) * (field.size.height - 86.0);
        const CGFloat wobble = std::sin((static_cast<double>(group) + 0.25) * 1.9) * 18.0 * std::abs(p->params.spread);
        points[group] = NSMakePoint(field.origin.x + 34.0 + u * (field.size.width - 68.0), y + wobble);
    }
    const CGFloat airAmount = static_cast<CGFloat>(std::abs(p->params.air));
    if (airAmount > 0.01) {
        [[NSColor colorWithCalibratedWhite:(p->params.air >= 0.0f ? 0.72 : 0.92) alpha:0.07 + 0.18 * airAmount] setStroke];
        for (int band = 0; band < 4; ++band) {
            const CGFloat t = static_cast<CGFloat>(band) / 3.0;
            const CGFloat y = field.origin.y + 34.0 + t * (field.size.height - 68.0);
            NSBezierPath* haze = [NSBezierPath bezierPath];
            [haze moveToPoint:NSMakePoint(field.origin.x + 22.0, y)];
            [haze curveToPoint:NSMakePoint(NSMaxX(field) - 22.0, y + std::sin(t * 4.5 + p->params.air) * 10.0 * airAmount)
                 controlPoint1:NSMakePoint(field.origin.x + field.size.width * 0.35, y - 18.0 * airAmount)
                 controlPoint2:NSMakePoint(field.origin.x + field.size.width * 0.65, y + 18.0 * airAmount)];
            [haze setLineWidth:0.35 + 1.2 * airAmount];
            CGFloat pattern[] = { 2.0, 4.0 + 5.0 * (1.0 - airAmount) };
            [haze setLineDash:pattern count:2 phase:static_cast<CGFloat>(band) * 1.5];
            [haze stroke];
        }
    }
    const CGFloat tailDepth = kSingleField
        ? 0.52 + 0.43 * std::pow(std::max(0.0f, p->params.depth), 2.0f)
        : std::max(0.0f, p->params.depth);
    const CGFloat tailAmount = static_cast<CGFloat>(p->params.tail * tailDepth);
    if (tailAmount > 0.01) {
        [[NSColor colorWithCalibratedWhite:0.78 alpha:0.06 + 0.20 * tailAmount] setStroke];
        for (uint32_t group = 0; group < kGroups; ++group) {
            const NSPoint p0 = points[group];
            const CGFloat sizeShape = kSingleField ? 0.72 + p->params.environmentSize * 0.56 : 1.0;
            const CGFloat decayShape = kSingleField ? 0.74 + p->params.environmentDecay * 0.52 : 1.0;
            const CGFloat w = (28.0 + 52.0 * tailAmount) * sizeShape;
            const CGFloat h = (12.0 + 24.0 * tailAmount) * decayShape;
            NSBezierPath* wake = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(p0.x - w * 0.5, p0.y - h * 0.5, w, h)];
            [wake setLineWidth:0.5 + 1.1 * tailAmount];
            [wake stroke];
        }
    }
    [s3g::clap_gui::color(0x666666, 0.38) setStroke];
    for (uint32_t group = 0; group + 1u < kGroups; ++group) {
        [NSBezierPath strokeLineFromPoint:points[group] toPoint:points[group + 1u]];
    }
    for (uint32_t group = 0; group < kGroups; ++group) {
        const auto state = p->processor.groupState(group);
        const CGFloat direct = static_cast<CGFloat>(1.0f - state.depth);
        const CGFloat size = 8.0 + direct * 8.0;
        [[NSColor colorWithCalibratedWhite:0.32 + direct * 0.58 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(points[group].x - size * 0.5, points[group].y - size * 0.5, size, size));
        [s3g::clap_gui::color(0x0f0f0f, 0.62) setStroke];
        NSFrameRect(NSMakeRect(points[group].x - size * 0.5, points[group].y - size * 0.5, size, size));
        [[NSString stringWithFormat:@"G%u", group + 1u] drawAtPoint:NSMakePoint(points[group].x + size * 0.62, points[group].y - 7.0) withAttributes:small];
    }

    NSRect meter = NSMakeRect(
        field.origin.x, NSMaxY(field) + 12.0,
        field.size.width - 18.0, 10.0);
    [style.strip setFill]; NSRectFill(meter);
    [style.grid setStroke]; NSFrameRect(meter);
    for (uint32_t group = 0; group < kGroups; ++group) {
        const auto state = p->processor.groupState(group);
        const CGFloat x = meter.origin.x + (meter.size.width - 3.0) * static_cast<CGFloat>(state.depth);
        [[NSColor colorWithCalibratedWhite:0.88 - state.depth * 0.52 alpha:1.0] setFill];
        NSRectFill(NSMakeRect(x, meter.origin.y - 3.0, 3.0, 16.0));
    }

    const auto& primary = kSingleField
        ? kTransformLayout.primaryFour
        : kTransformLayout.primarySix;
    const auto drawPanel =
        [&](NSString* title, const s3g::gui_layout::Panel& panel) {
            s3g::clap_gui::drawPanelFrame(panel, style);
            s3g::clap_gui::drawPanelHeader(
                title, true, panel, text, style);
        };
    drawPanel(@"OUTPUT", kTransformLayout.output);
    drawPanel(@"DEPTH", primary);
    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB",
            static_cast<double>(p->params.outputGainDb)]
        norm:(p->params.outputGainDb + 60.0) / 72.0
        y:s3g::gui_layout::rowY(kTransformLayout.output, 0u)
        attrs:small style:style];
    [self drawSlider:@"WIDTH"
        value:[NSString stringWithFormat:@"%.2f",
            static_cast<double>(p->params.width)]
        norm:p->params.width / 1.5
        y:s3g::gui_layout::rowY(kTransformLayout.output, 1u)
        attrs:small style:style];
    [self drawSlider:@"DEPTH" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.depth * 100.0f)] norm:(p->params.depth + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 0u) attrs:small style:style];
    if constexpr (!kSingleField) {
        [self drawSlider:@"GROUP SPREAD" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.spread * 100.0f)] norm:(p->params.spread + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 1u) attrs:small style:style];
        [self drawSlider:@"FOCUS" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.focus * 100.0f)] norm:(p->params.focus + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 2u) attrs:small style:style];
        [self drawSlider:@"AIR DAMPING" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.air * 100.0f)] norm:(p->params.air + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 3u) attrs:small style:style];
        [self drawSlider:@"TAIL" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.tail * 100.0f)] norm:p->params.tail y:s3g::gui_layout::rowY(primary, 4u) attrs:small style:style];
        [self drawSlider:@"LOW BODY" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.low * 100.0f)] norm:(p->params.low + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 5u) attrs:small style:style];
    } else {
        [self drawSlider:@"FOCUS" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.focus * 100.0f)] norm:(p->params.focus + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 1u) attrs:small style:style];
        [self drawSlider:@"AIR DAMPING" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.air * 100.0f)] norm:(p->params.air + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 2u) attrs:small style:style];
        [self drawSlider:@"LOW BODY" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(p->params.low * 100.0f)] norm:(p->params.low + 1.0) * 0.5 y:s3g::gui_layout::rowY(primary, 3u) attrs:small style:style];
        drawPanel(@"ENVIRONMENT", kTransformLayout.secondaryFour);
        [self drawSlider:@"TAIL" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.tail * 100.0f)] norm:p->params.tail y:s3g::gui_layout::rowY(kTransformLayout.secondaryFour, 0u) attrs:small style:style];
        [self drawSlider:@"SIZE" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.environmentSize * 100.0f)] norm:p->params.environmentSize y:s3g::gui_layout::rowY(kTransformLayout.secondaryFour, 1u) attrs:small style:style];
        [self drawSlider:@"DECAY" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.environmentDecay * 100.0f)] norm:p->params.environmentDecay y:s3g::gui_layout::rowY(kTransformLayout.secondaryFour, 2u) attrs:small style:style];
        [self drawSlider:@"DAMPING" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.environmentDamping * 100.0f)] norm:p->params.environmentDamping y:s3g::gui_layout::rowY(kTransformLayout.secondaryFour, 3u) attrs:small style:style];
    }
    [[NSString stringWithUTF8String:kHeaderInfo]
        drawAtPoint:NSMakePoint(
            fieldPanel.origin.x + 16.0, NSMaxY(meter) + 5.0)
        withAttributes:small];
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp(
        (pt.x - s3g::gui_layout::processorControlX(
            kTransformLayout.output.frame.x))
            / s3g::gui_layout::processorTrackWidth(
                kTransformLayout.output.frame.width),
        0.0, 1.0);
    if constexpr (kSingleField) {
        switch (_dragSlider) {
        case 0: [self setParam:kParamDepth value:-1.0 + norm * 2.0]; break;
        case 1: [self setParam:kParamFocus value:-1.0 + norm * 2.0]; break;
        case 2: [self setParam:kParamAir value:-1.0 + norm * 2.0]; break;
        case 3: [self setParam:kParamLow value:-1.0 + norm * 2.0]; break;
        case 4: [self setParam:kParamWidth value:norm * 1.5]; break;
        case 5: [self setParam:kParamTail value:norm]; break;
        case 6: [self setParam:kParamEnvironmentSize value:norm]; break;
        case 7: [self setParam:kParamEnvironmentDecay value:norm]; break;
        case 8: [self setParam:kParamEnvironmentDamping value:norm]; break;
        case 9: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
        default: break;
        }
        return;
    }
    switch (_dragSlider) {
    case 0: [self setParam:kParamDepth value:-1.0 + norm * 2.0]; break;
    case 1: [self setParam:kParamSpread value:-1.0 + norm * 2.0]; break;
    case 2: [self setParam:kParamFocus value:-1.0 + norm * 2.0]; break;
    case 3: [self setParam:kParamAir value:-1.0 + norm * 2.0]; break;
    case 4: [self setParam:kParamTail value:norm]; break;
    case 5: [self setParam:kParamLow value:-1.0 + norm * 2.0]; break;
    case 6: [self setParam:kParamWidth value:norm * 1.5]; break;
    case 7: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    default: break;
    }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand =
        s3g::gui_layout::transformTitleBand(kTransformLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin,
            kSingleField ? @"Ambi Transform Depth"
                         : @"Ambi Transform Group Depth",
            titleBand, p->presetName, sizeof(p->presetName),
            kParamOutput)) {
        [self setNeedsDisplay:YES];
        return;
    }
    struct SliderHit {
        s3g::gui_layout::Rect rect;
        int slider;
        clap_id param;
    };
    const auto& primary = kSingleField
        ? kTransformLayout.primaryFour
        : kTransformLayout.primarySix;
    const SliderHit commonHits[] {
        { s3g::gui_layout::sliderHitRect(kTransformLayout.output, 0u),
            kSingleField ? 9 : 7, kParamOutput },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.output, 1u),
            kSingleField ? 4 : 6, kParamWidth },
        { s3g::gui_layout::sliderHitRect(primary, 0u),
            0, kParamDepth },
    };
    for (const auto& hit : commonHits) {
        if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(hit.rect))) {
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
    if constexpr (kSingleField) {
        const SliderHit hits[] {
            { s3g::gui_layout::sliderHitRect(primary, 1u),
                1, kParamFocus },
            { s3g::gui_layout::sliderHitRect(primary, 2u),
                2, kParamAir },
            { s3g::gui_layout::sliderHitRect(primary, 3u),
                3, kParamLow },
            { s3g::gui_layout::sliderHitRect(
                  kTransformLayout.secondaryFour, 0u),
                5, kParamTail },
            { s3g::gui_layout::sliderHitRect(
                  kTransformLayout.secondaryFour, 1u),
                6, kParamEnvironmentSize },
            { s3g::gui_layout::sliderHitRect(
                  kTransformLayout.secondaryFour, 2u),
                7, kParamEnvironmentDecay },
            { s3g::gui_layout::sliderHitRect(
                  kTransformLayout.secondaryFour, 3u),
                8, kParamEnvironmentDamping },
        };
        for (const auto& hit : hits) {
            if (!NSPointInRect(
                    pt, s3g::clap_gui::cocoaRect(hit.rect))) continue;
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
    } else {
        const SliderHit hits[] {
            { s3g::gui_layout::sliderHitRect(primary, 1u),
                1, kParamSpread },
            { s3g::gui_layout::sliderHitRect(primary, 2u),
                2, kParamFocus },
            { s3g::gui_layout::sliderHitRect(primary, 3u),
                3, kParamAir },
            { s3g::gui_layout::sliderHitRect(primary, 4u),
                4, kParamTail },
            { s3g::gui_layout::sliderHitRect(primary, 5u),
                5, kParamLow },
        };
        for (const auto& hit : hits) {
            if (!NSPointInRect(
                    pt, s3g::clap_gui::cocoaRect(hit.rect))) continue;
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
}
- (void)mouseDragged:(NSEvent*)event { if (_dragSlider >= 0) [self updateSliderAtPoint:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G_AMBI_GROUP_DEPTH_VIEW_CLASS alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p && p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3G_AMBI_GROUP_DEPTH_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<S3G_AMBI_GROUP_DEPTH_VIEW_CLASS*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3G_AMBI_GROUP_DEPTH_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

namespace {

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

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kHostName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    kPluginDesc,
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostTail = host && host->get_extension ? static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL)) : nullptr;
    p->params = s3g::sanitizeAmbiGroupDepthParams(p->params);
    p->processor.prepare(48000.0);
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
