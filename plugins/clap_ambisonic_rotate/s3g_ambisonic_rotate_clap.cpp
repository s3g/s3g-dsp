#include "s3g_ambisonic_utilities.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_state_stream.h"

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
constexpr uint32_t kGuiWidth = 820;
constexpr uint32_t kGuiHeight = 496;
constexpr uint32_t kStateVersion = 3;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamYaw = 2,
    kParamPitch = 3,
    kParamRoll = 4,
    kParamSpread = 5,
    kParamTilt = 6,
    kParamTwist = 7,
    kParamWidth = 8,
    kParamOutput = 9,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiRotateParams params {};
    int32_t guiViewMode = 2;
};

struct SavedStateV2 {
    uint32_t version = 2;
    s3g::AmbiRotateParams params {};
};

struct OldAmbiRotateParamsV1 {
    uint32_t order = 7;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
    float width = 1.0f;
    float outputGainDb = 0.0f;
};

struct OldSavedStateV1 {
    uint32_t version = 1;
    OldAmbiRotateParamsV1 params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    s3g::AmbiRotateParams audioParams {};
    std::array<std::atomic<float>, 9u> publishedParams {};
    std::atomic<uint64_t> parameterRevision { 0u };
    uint64_t audioRevision = 0u;
    s3g::AmbiRotateProcessor processor {};
    std::atomic<float> outputPeak { 0.0f };
    int32_t guiViewMode = 2;
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

bool setParamValue(s3g::AmbiRotateParams& params, clap_id id, double value)
{
    switch (id) {
    case kParamOrder: params.order = std::clamp<uint32_t>(roundedUint(value), 1u, s3g::kAmbiUtilityMaxOrder); break;
    case kParamYaw: params.yawDeg = static_cast<float>(value); break;
    case kParamPitch: params.pitchDeg = static_cast<float>(value); break;
    case kParamRoll: params.rollDeg = static_cast<float>(value); break;
    case kParamSpread: params.spread = static_cast<float>(value); break;
    case kParamTilt: params.tilt = static_cast<float>(value); break;
    case kParamTwist: params.twist = static_cast<float>(value); break;
    case kParamWidth: params.width = static_cast<float>(value); break;
    case kParamOutput: params.outputGainDb = static_cast<float>(value); break;
    default: return false;
    }
    return true;
}

double paramValue(const s3g::AmbiRotateParams& params, clap_id id)
{
    switch (id) {
    case kParamOrder: return params.order;
    case kParamYaw: return params.yawDeg;
    case kParamPitch: return params.pitchDeg;
    case kParamRoll: return params.rollDeg;
    case kParamSpread: return params.spread;
    case kParamTilt: return params.tilt;
    case kParamTwist: return params.twist;
    case kParamWidth: return params.width;
    case kParamOutput: return params.outputGainDb;
    default: return 0.0;
    }
}

s3g::AmbiRotateParams snapshotPublishedParams(const Plugin& p)
{
    s3g::AmbiRotateParams params {};
    params.order = std::clamp<uint32_t>(roundedUint(
        p.publishedParams[kParamOrder - 1u].load(std::memory_order_relaxed)),
        1u, s3g::kAmbiUtilityMaxOrder);
    params.yawDeg = p.publishedParams[kParamYaw - 1u].load(std::memory_order_relaxed);
    params.pitchDeg = p.publishedParams[kParamPitch - 1u].load(std::memory_order_relaxed);
    params.rollDeg = p.publishedParams[kParamRoll - 1u].load(std::memory_order_relaxed);
    params.spread = p.publishedParams[kParamSpread - 1u].load(std::memory_order_relaxed);
    params.tilt = p.publishedParams[kParamTilt - 1u].load(std::memory_order_relaxed);
    params.twist = p.publishedParams[kParamTwist - 1u].load(std::memory_order_relaxed);
    params.width = p.publishedParams[kParamWidth - 1u].load(std::memory_order_relaxed);
    params.outputGainDb = p.publishedParams[kParamOutput - 1u].load(std::memory_order_relaxed);
    return s3g::sanitizeAmbiRotateParams(params);
}

void storePublishedParam(Plugin& p, clap_id id, const s3g::AmbiRotateParams& params)
{
    if (id < kParamOrder || id > kParamOutput) return;
    p.publishedParams[id - 1u].store(static_cast<float>(paramValue(params, id)), std::memory_order_relaxed);
}

void publishParams(Plugin& p, const s3g::AmbiRotateParams& requested, bool notifyAudio)
{
    const auto params = s3g::sanitizeAmbiRotateParams(requested);
    for (clap_id id = kParamOrder; id <= kParamOutput; ++id) storePublishedParam(p, id, params);
    if (notifyAudio) {
        p.parameterRevision.fetch_add(1u, std::memory_order_release);
        if (p.host && p.host->request_process) p.host->request_process(p.host);
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    auto params = snapshotPublishedParams(p);
    if (!setParamValue(params, id, value)) return;
    params = s3g::sanitizeAmbiRotateParams(params);
    storePublishedParam(p, id, params);
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (p.host && p.host->request_process) p.host->request_process(p.host);
}

double getParam(const Plugin& p, clap_id id)
{
    if (id < kParamOrder || id > kParamOutput) return 0.0;
    return p.publishedParams[id - 1u].load(std::memory_order_relaxed);
}

bool syncPendingParams(Plugin& p)
{
    const uint64_t revision = p.parameterRevision.load(std::memory_order_acquire);
    if (revision == p.audioRevision) return false;
    p.audioParams = snapshotPublishedParams(p);
    p.audioRevision = revision;
    return true;
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
    p->audioParams = snapshotPublishedParams(*p);
    p->processor.setParams(p->audioParams);
    p->processor.reset();
    p->audioRevision = p->parameterRevision.load(std::memory_order_acquire);
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

void flushParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    auto params = snapshotPublishedParams(p);
    uint16_t changedMask = 0u;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            if (setParamValue(params, param->param_id, param->value)) {
                changedMask |= static_cast<uint16_t>(1u << (param->param_id - 1u));
            }
        }
    }
    if (changedMask == 0u) return;
    params = s3g::sanitizeAmbiRotateParams(params);
    for (clap_id id = kParamOrder; id <= kParamOutput; ++id) {
        if ((changedMask & static_cast<uint16_t>(1u << (id - 1u))) != 0u) {
            storePublishedParam(p, id, params);
        }
    }
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (p.host && p.host->request_process) p.host->request_process(p.host);
}

bool applyParamEventsForBlock(Plugin& p, const clap_input_events_t* events)
{
    auto params = p.audioParams;
    uint16_t changedMask = 0u;
    const uint32_t count = events ? events->size(events) : 0u;
    for (uint32_t i = 0; i < count; ++i) {
        const auto* event = events->get(events, i);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        if (setParamValue(params, value->param_id, value->value)) {
            changedMask |= static_cast<uint16_t>(1u << (value->param_id - 1u));
        }
    }
    if (changedMask == 0u) return false;
    p.audioParams = s3g::sanitizeAmbiRotateParams(params);
    for (clap_id id = kParamOrder; id <= kParamOutput; ++id) {
        if ((changedMask & static_cast<uint16_t>(1u << (id - 1u))) != 0u) {
            storePublishedParam(p, id, p.audioParams);
        }
    }
    return true;
}

template <typename Sample>
clap_process_status processTyped(Plugin& p, const clap_audio_buffer_t& input,
    const clap_audio_buffer_t& output, uint32_t frames, Sample** in, Sample** out)
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
    const bool pendingParamsChanged = syncPendingParams(*p);
    // This processor has historically treated CLAP automation as block-rate.
    // Fold the whole event list into one snapshot so dense host bursts cause a
    // single matrix build without changing the established timing contract.
    const bool eventParamsChanged = applyParamEventsForBlock(*p, proc->in_events);
    if (pendingParamsChanged || eventParamsChanged) p->processor.setParams(p->audioParams);
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
    std::strncpy(info->name, isInput ? "Ambisonic In" : "Ambisonic Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 9; }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Rotate", sizeof(info->module));
    switch (index) {
    case 0: info->id = kParamOrder; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Ambisonic order", sizeof(info->name)); info->min_value = 1; info->max_value = 7; info->default_value = 7; return true;
    case 1: info->id = kParamYaw; std::strncpy(info->name, "Yaw / azimuth", sizeof(info->name)); info->min_value = -180; info->max_value = 180; info->default_value = 0; return true;
    case 2: info->id = kParamPitch; std::strncpy(info->name, "Pitch / elevation", sizeof(info->name)); info->min_value = -90; info->max_value = 90; info->default_value = 0; return true;
    case 3: info->id = kParamRoll; std::strncpy(info->name, "Roll", sizeof(info->name)); info->min_value = -180; info->max_value = 180; info->default_value = 0; return true;
    case 4: info->id = kParamSpread; std::strncpy(info->name, "Dispersion spread", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 5: info->id = kParamTilt; std::strncpy(info->name, "Dispersion tilt", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 6: info->id = kParamTwist; std::strncpy(info->name, "Dispersion twist", sizeof(info->name)); info->min_value = -1; info->max_value = 1; info->default_value = 0; return true;
    case 7: info->id = kParamWidth; std::strncpy(info->name, "Order width", sizeof(info->name)); info->min_value = 0; info->max_value = 1.5; info->default_value = 1; return true;
    case 8: info->id = kParamOutput; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -60; info->max_value = 12; info->default_value = 0; return true;
    default: return false;
    }
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParam(*self(plugin), paramId);
    return paramId >= kParamOrder && paramId <= kParamOutput;
}
bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    switch (paramId) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); return true;
    case kParamYaw:
    case kParamPitch:
    case kParamRoll: std::snprintf(display, size, "%+.0f deg", value); return true;
    case kParamSpread: std::snprintf(display, size, "%+.0f%%", value * 100.0); return true;
    case kParamTilt:
    case kParamTwist: std::snprintf(display, size, "%+.0f%%", value * 100.0); return true;
    case kParamWidth: std::snprintf(display, size, "%.2f", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    default: return false;
    }
}
bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    if (paramId == kParamSpread || paramId == kParamTilt || paramId == kParamTwist) {
        *value *= 0.01;
    }
    return paramId >= kParamOrder && paramId <= kParamOutput;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { flushParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    const SavedState state { kStateVersion, snapshotPublishedParams(*p), p->guiViewMode };
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto* p = self(plugin);
    uint32_t version = 0u;
    if (!s3g::clap_state::readAll(stream, &version, sizeof(version))) return false;
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) return false;
        publishParams(*p, state.params, true);
        p->guiViewMode = std::clamp<int32_t>(state.guiViewMode, -1, 2);
    } else if (version == 2u) {
        SavedStateV2 old {};
        old.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&old) + sizeof(version),
                sizeof(old) - sizeof(version))) return false;
        publishParams(*p, old.params, true);
        p->guiViewMode = 2;
    } else if (version == 1u) {
        OldSavedStateV1 old {};
        old.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&old) + sizeof(version),
                sizeof(old) - sizeof(version))) return false;
        publishParams(*p, {
            old.params.order,
            old.params.yawDeg,
            old.params.pitchDeg,
            old.params.rollDeg,
            0.0f,
            0.0f,
            0.0f,
            old.params.width,
            old.params.outputGainDb,
        }, true);
    } else {
        return false;
    }
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {
constexpr const auto& kTransformLayout =
    s3g::gui_layout::kTransformFamilyLayout;
}

@interface S3GAmbisonicRotateView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    int _viewMode;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    double _viewAzDeg;
    double _viewElDeg;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (void)setViewPreset:(int)mode;
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (NSPoint)project:(s3g::Vec3)v rect:(NSRect)rect scale:(CGFloat)scale;
- (void)updateSliderAtPoint:(NSPoint)pt;
@end

@implementation S3GAmbisonicRotateView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        auto* p = static_cast<Plugin*>(_plugin);
        _viewMode = p ? std::clamp<int>(p->guiViewMode, -1, 2) : 2;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _viewAzDeg = -45.0;
        _viewElDeg = 26.0;
        _refreshTimer = nil;
        if (_viewMode == 0) [self setViewPreset:0];
        else if (_viewMode == 1) [self setViewPreset:1];
        else if (_viewMode == 2) [self setViewPreset:2];
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
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 38.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    const CGFloat x = NSMaxX(rect) - 10.0 - (3.0 - static_cast<CGFloat>(index)) * w - (2.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 4.0, w, h);
}
- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (_plugin) static_cast<Plugin*>(_plugin)->guiViewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 90.0;
    } else if (mode == 1) {
        _viewAzDeg = 180.0;
        _viewElDeg = 0.0;
    } else {
        _viewAzDeg = -45.0;
        _viewElDeg = 26.0;
    }
    [self setNeedsDisplay:YES];
}
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"TOP", @"BACK", @"3/4" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:rect], rect, labels[i], i == _viewMode, attrs, style);
    }
}
- (NSPoint)project:(s3g::Vec3)v rect:(NSRect)rect scale:(CGFloat)scale
{
    const CGFloat cx = rect.origin.x + rect.size.width * 0.5;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    if (_viewMode == 0) {
        return NSMakePoint(cx - v.y * scale, cy - v.x * scale);
    }
    if (_viewMode == 1) {
        return NSMakePoint(cx - v.y * scale, cy - v.z * scale);
    }
    const double az = _viewAzDeg * s3g::kPi / 180.0;
    const double el = _viewElDeg * s3g::kPi / 180.0;
    const double ca = std::cos(az);
    const double sa = std::sin(az);
    const double ce = std::cos(el);
    const double se = std::sin(el);
    const double x1 = static_cast<double>(v.x) * ca - static_cast<double>(v.y) * sa;
    const double y1 = static_cast<double>(v.x) * sa + static_cast<double>(v.y) * ca;
    const double z1 = static_cast<double>(v.z);
    const double x2 = x1 * ce + z1 * se;
    const double z2 = -x1 * se + z1 * ce;
    return NSMakePoint(cx - y1 * scale + x2 * scale * 0.14, cy - z2 * scale - x2 * scale * 0.10);
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    const auto params = snapshotPublishedParams(*p);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    const float pk = p->outputPeak.exchange(p->outputPeak.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    const auto titleBand =
        s3g::gui_layout::transformTitleBand(kTransformLayout.canvas);
    s3g::clap_gui::drawTransformTitleBand(
        @"s3g AMBI TRANSFORM ROTATE 64CH",
        [NSString stringWithUTF8String:p->presetName],
        s3g::clap_gui::peakDbText(pk), titleBand, style);

    NSRect fieldPanel =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPanel);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ROTATION FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    [self drawViewButtonsInRect:fieldPanel attrs:small];
    NSRect field =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPlot);
    [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);
    const CGFloat scale = std::min(field.size.width, field.size.height) * 0.36;
    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.54;
    [s3g::clap_gui::color(0x777777, 0.22) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 24, cy) toPoint:NSMakePoint(NSMaxX(field) - 24, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, field.origin.y + 18) toPoint:NSMakePoint(cx, NSMaxY(field) - 18)];

    const s3g::Vec3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    NSString* labels[3] = { @"0deg", @"+90", @"+EL" };
    for (int i = 0; i < 3; ++i) {
        const NSPoint a = [self project:axes[i] rect:field scale:scale];
        [s3g::clap_gui::color(0xbdbdbd, 0.46) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, cy) toPoint:a];
        [labels[i] drawAtPoint:NSMakePoint(a.x + 6, a.y - 6) withAttributes:small];
    }
    const float pts[8][3] = {
        { 1, -1, -1 }, { -1, -1, -1 }, { -1, 1, -1 }, { 1, 1, -1 },
        { 1, -1, 1 }, { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 },
    };
    const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
    };
    NSPoint projected[8];
    for (int i = 0; i < 8; ++i) {
        s3g::Vec3 v = s3g::normalize({ pts[i][0], pts[i][1], pts[i][2] });
        const float az = std::atan2(v.y, v.x);
        const float yawOffset = std::sin(az) * params.spread * 54.0f;
        const float pitchOffset = std::sin(az) * params.tilt * 42.0f;
        const float rollOffset = std::cos(az) * params.twist * 90.0f;
        v = s3g::ambiUtilityRotate(v,
            params.yawDeg + yawOffset,
            params.pitchDeg + pitchOffset,
            params.rollDeg + rollOffset);
        projected[i] = [self project:v rect:field scale:scale * 0.88];
    }
    [s3g::clap_gui::color(0xd0d0d0, 0.48) setStroke];
    for (const auto& e : edges) [NSBezierPath strokeLineFromPoint:projected[e[0]] toPoint:projected[e[1]]];
    [style.text setFill];
    for (auto pt : projected) NSRectFill(NSMakeRect(pt.x - 3, pt.y - 3, 6, 6));
    [[NSString stringWithFormat:@"%uOA ACN/SN3D  /  YAW %+.0f  PITCH %+.0f  ROLL %+.0f",
      params.order,
      static_cast<double>(params.yawDeg), static_cast<double>(params.pitchDeg), static_cast<double>(params.rollDeg)]
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
    drawPanel(@"ROTATION", kTransformLayout.primarySeven);
    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB",
            static_cast<double>(params.outputGainDb)]
        norm:(params.outputGainDb + 60.0) / 72.0
        y:s3g::gui_layout::rowY(kTransformLayout.output, 0u)
        attrs:small style:style];
    s3g::clap_gui::drawProcessorMenu(
        @"ORDER", [NSString stringWithFormat:@"%uOA", params.order],
        s3g::gui_layout::rowY(kTransformLayout.output, 1u),
        kTransformLayout.output.frame.x,
        kTransformLayout.output.frame.width,
        text, small, style);
    const CGFloat rows[] = {
        s3g::gui_layout::rowY(kTransformLayout.primarySeven, 0u),
        s3g::gui_layout::rowY(kTransformLayout.primarySeven, 1u),
        s3g::gui_layout::rowY(kTransformLayout.primarySeven, 2u),
        s3g::gui_layout::rowY(kTransformLayout.primarySeven, 3u),
        s3g::gui_layout::rowY(kTransformLayout.primarySeven, 4u),
        s3g::gui_layout::rowY(kTransformLayout.primarySeven, 5u),
        s3g::gui_layout::rowY(kTransformLayout.primarySeven, 6u),
    };
    [self drawSlider:@"YAW" value:[NSString stringWithFormat:@"%+.0f°", static_cast<double>(params.yawDeg)] norm:(params.yawDeg + 180.0) / 360.0 y:rows[0] attrs:small style:style];
    [self drawSlider:@"PITCH" value:[NSString stringWithFormat:@"%+.0f°", static_cast<double>(params.pitchDeg)] norm:(params.pitchDeg + 90.0) / 180.0 y:rows[1] attrs:small style:style];
    [self drawSlider:@"ROLL" value:[NSString stringWithFormat:@"%+.0f°", static_cast<double>(params.rollDeg)] norm:(params.rollDeg + 180.0) / 360.0 y:rows[2] attrs:small style:style];
    [self drawSlider:@"SPREAD" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(params.spread * 100.0f)] norm:(params.spread + 1.0) * 0.5 y:rows[3] attrs:small style:style];
    [self drawSlider:@"TILT" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(params.tilt * 100.0f)] norm:(params.tilt + 1.0) * 0.5 y:rows[4] attrs:small style:style];
    [self drawSlider:@"TWIST" value:[NSString stringWithFormat:@"%+.0f%%", static_cast<double>(params.twist * 100.0f)] norm:(params.twist + 1.0) * 0.5 y:rows[5] attrs:small style:style];
    [self drawSlider:@"WIDTH" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(params.width)] norm:params.width / 1.5 y:rows[6] attrs:small style:style];

    if (_openMenu == 1) {
        static NSString* items[] = {
            @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA"
        };
        const NSRect menuRect = NSMakeRect(
            _menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(
                kTransformLayout.output.frame.width),
            18.0 * 7.0);
        s3g::clap_gui::drawDropdownMenu(
            menuRect, 18.0, items, 7u,
            static_cast<int>(params.order) - 1,
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
    switch (_dragSlider) {
    case 0: [self setParam:kParamYaw value:-180.0 + norm * 360.0]; break;
    case 1: [self setParam:kParamPitch value:-90.0 + norm * 180.0]; break;
    case 2: [self setParam:kParamRoll value:-180.0 + norm * 360.0]; break;
    case 3: [self setParam:kParamSpread value:-1.0 + norm * 2.0]; break;
    case 4: [self setParam:kParamTilt value:-1.0 + norm * 2.0]; break;
    case 5: [self setParam:kParamTwist value:-1.0 + norm * 2.0]; break;
    case 6: [self setParam:kParamWidth value:norm * 1.5]; break;
    case 7: [self setParam:kParamOutput value:-60.0 + norm * 72.0]; break;
    default: break;
    }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const auto titleBand =
        s3g::gui_layout::transformTitleBand(kTransformLayout.canvas);
    auto* p = static_cast<Plugin*>(_plugin);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, @"Ambi Transform Rotate", titleBand,
            p->presetName, sizeof(p->presetName), kParamOutput)) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu == 1) {
        const NSRect menuRect = NSMakeRect(
            _menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(
                kTransformLayout.output.frame.width),
            18.0 * 7.0);
        const int hit = s3g::clap_gui::dropdownHitIndex(
            pt, menuRect, 18.0, 7u);
        if (hit >= 0) {
            [self setParam:kParamOrder value:hit + 1.0];
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect orderBox = NSMakeRect(
        s3g::gui_layout::processorControlX(
            kTransformLayout.output.frame.x),
        s3g::gui_layout::rowY(kTransformLayout.output, 1u) - 1.0,
        s3g::gui_layout::processorMenuWidth(
            kTransformLayout.output.frame.width),
        15.0);
    if (NSPointInRect(pt, NSInsetRect(orderBox, 0.0, -4.0))) {
        _openMenu = 1;
        _hoverMenuItem = -1;
        _menuOrigin = NSMakePoint(
            orderBox.origin.x, NSMaxY(orderBox) + 2.0);
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect fieldPanel =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPanel);
    const NSRect field =
        s3g::clap_gui::cocoaRect(kTransformLayout.fieldPlot);
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(pt, [self viewButtonRect:i inRect:fieldPanel])) {
            [self setViewPreset:i];
            return;
        }
    }
    struct SliderHit {
        s3g::gui_layout::Rect rect;
        int slider;
        clap_id param;
    };
    const SliderHit hits[] {
        { s3g::gui_layout::sliderHitRect(kTransformLayout.output, 0u),
            7, kParamOutput },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.primarySeven, 0u),
            0, kParamYaw },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.primarySeven, 1u),
            1, kParamPitch },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.primarySeven, 2u),
            2, kParamRoll },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.primarySeven, 3u),
            3, kParamSpread },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.primarySeven, 4u),
            4, kParamTilt },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.primarySeven, 5u),
            5, kParamTwist },
        { s3g::gui_layout::sliderHitRect(kTransformLayout.primarySeven, 6u),
            6, kParamWidth },
    };
    for (const auto& hit : hits) {
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
    if (NSPointInRect(pt, field)) {
        _dragView = YES;
        _lastDragPoint = pt;
        if (_viewMode != 2) {
            _viewMode = -1;
            if (_plugin) static_cast<Plugin*>(_plugin)->guiViewMode = -1;
        }
        return;
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu != 1) return;
    const NSPoint pt =
        [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect menuRect = NSMakeRect(
        _menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(
            kTransformLayout.output.frame.width),
        18.0 * 7.0);
    const int hover =
        s3g::clap_gui::dropdownHitIndex(pt, menuRect, 18.0, 7u);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragSlider >= 0) {
        [self updateSliderAtPoint:pt];
        return;
    }
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _viewMode = -1;
        if (_plugin) static_cast<Plugin*>(_plugin)->guiViewMode = -1;
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragView = NO; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbisonicRotateView alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p && p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicRotateView*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<S3GAmbisonicRotateView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicRotateView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    "org.s3g.s3g-dsp.ambisonic-rotate-64",
    "s3g Ambi Transform Rot 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "64-channel ACN/SN3D ambisonic rotation utility up to 7OA.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->audioParams = s3g::sanitizeAmbiRotateParams(p->audioParams);
    publishParams(*p, p->audioParams, false);
    p->processor.setParams(p->audioParams);
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
