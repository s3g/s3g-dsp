#include "s3g_ambi_cloud_encoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
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

constexpr uint32_t kInputChannels = s3g::kAmbiCloudEncoderMaxInputs;
constexpr uint32_t kOutputChannels = s3g::kAmbiCloudEncoderMaxChannels;
constexpr uint32_t kStateVersion = 2;

constexpr clap_id kInputsParamId = 1;
constexpr clap_id kCloudsParamId = 2;
constexpr clap_id kCloudParamId = 3;
constexpr clap_id kOrderParamId = 4;
constexpr clap_id kAzimuthParamId = 5;
constexpr clap_id kElevationParamId = 6;
constexpr clap_id kDistanceParamId = 7;
constexpr clap_id kCloudGainParamId = 8;
constexpr clap_id kSpreadParamId = 9;
constexpr clap_id kElevationSpreadParamId = 10;
constexpr clap_id kJitterParamId = 11;
constexpr clap_id kDriftParamId = 12;
constexpr clap_id kRateParamId = 13;
constexpr clap_id kDecorrelateParamId = 14;
constexpr clap_id kShapeParamId = 15;
constexpr clap_id kForceParamId = 16;
constexpr clap_id kOutputParamId = 17;
constexpr clap_id kCloudParamBase = 100;
constexpr clap_id kCloudParamStride = 4;

enum class CloudParamKind : uint32_t {
    Azimuth = 0,
    Elevation = 1,
    Distance = 2,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiCloudEncoderParams params {};
    std::array<s3g::AmbiCloud, s3g::kAmbiCloudEncoderMaxClouds> clouds {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiCloudEncoder encoder {};
    s3g::AmbiCloudEncoderParams params {};
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

const char* shapeName(uint32_t index)
{
    static constexpr const char* kNames[] { "CUMULUS", "STRATUS", "CIRRUS", "LENTIC", "STORM" };
    return kNames[std::min<uint32_t>(index, 4u)];
}

const char* forceName(uint32_t index)
{
    static constexpr const char* kNames[] { "CALM", "ADVECT", "SHEAR", "CONVECT", "TURB" };
    return kNames[std::min<uint32_t>(index, 4u)];
}

clap_id perCloudParamId(uint32_t cloud, CloudParamKind kind)
{
    return kCloudParamBase + cloud * kCloudParamStride + static_cast<uint32_t>(kind);
}

bool decodePerCloudParam(clap_id id, uint32_t& cloud, CloudParamKind& kind)
{
    if (id < kCloudParamBase) return false;
    const uint32_t rel = id - kCloudParamBase;
    cloud = rel / kCloudParamStride;
    const uint32_t k = rel % kCloudParamStride;
    if (cloud >= s3g::kAmbiCloudEncoderMaxClouds || k > static_cast<uint32_t>(CloudParamKind::Distance)) return false;
    kind = static_cast<CloudParamKind>(k);
    return true;
}

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->write(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->read(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

void applyParam(Plugin& p, clap_id id, double value)
{
    uint32_t cloudIndex = 0;
    CloudParamKind cloudKind = CloudParamKind::Azimuth;
    if (decodePerCloudParam(id, cloudIndex, cloudKind)) {
        auto clouds = p.encoder.clouds();
        auto& cloud = clouds[cloudIndex];
        switch (cloudKind) {
        case CloudParamKind::Azimuth: cloud.azimuthDeg = static_cast<float>(value); break;
        case CloudParamKind::Elevation: cloud.elevationDeg = static_cast<float>(value); break;
        case CloudParamKind::Distance: cloud.distance = static_cast<float>(value); break;
        }
        p.encoder.setParams(p.params);
        p.encoder.setClouds(clouds);
        p.params = p.encoder.params();
        return;
    }

    switch (id) {
    case kInputsParamId: p.params.activeInputs = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, kInputChannels); break;
    case kCloudsParamId: p.params.activeClouds = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiCloudEncoderMaxClouds); break;
    case kCloudParamId: p.params.selectedCloud = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiCloudEncoderMaxClouds) - 1u; break;
    case kOrderParamId: p.params.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiCloudEncoderMaxOrder); break;
    case kAzimuthParamId: p.params.selectedAzimuthDeg = static_cast<float>(value); break;
    case kElevationParamId: p.params.selectedElevationDeg = static_cast<float>(value); break;
    case kDistanceParamId: p.params.selectedDistance = static_cast<float>(value); break;
    case kCloudGainParamId: p.params.selectedGain = static_cast<float>(value); break;
    case kSpreadParamId: p.params.spread = static_cast<float>(value); break;
    case kElevationSpreadParamId: p.params.elevationSpread = static_cast<float>(value); break;
    case kJitterParamId: p.params.jitter = static_cast<float>(value); break;
    case kDriftParamId: p.params.drift = static_cast<float>(value); break;
    case kRateParamId: p.params.rateHz = static_cast<float>(value); break;
    case kDecorrelateParamId: p.params.decorrelate = static_cast<float>(value); break;
    case kShapeParamId: p.params.shape = static_cast<s3g::AmbiCloudShape>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 4u)); break;
    case kForceParamId: p.params.force = static_cast<s3g::AmbiCloudForce>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 4u)); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(value); break;
    default: break;
    }
    p.encoder.setParams(p.params);
    p.params = p.encoder.params();
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) {
        [static_cast<NSView*>(p->guiView) removeFromSuperview];
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
    }
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->encoder.prepare(sampleRate);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->encoder.reset();
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
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto* input = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = input ? std::min<uint32_t>(input->channel_count, kInputChannels) : 0u;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0, frames);
    if (!output.data32 || outChannels == 0u) return CLAP_PROCESS_CONTINUE;

    std::array<const float*, kInputChannels> inputPtrs {};
    std::array<float*, kOutputChannels> outputPtrs {};
    for (uint32_t ch = 0; ch < inChannels; ++ch) inputPtrs[ch] = input && input->data32 ? input->data32[ch] : nullptr;
    for (uint32_t ch = 0; ch < outChannels; ++ch) outputPtrs[ch] = output.data32[ch];

    p->encoder.setParams(p->params);
    p->encoder.processBlock(inputPtrs.data(), outputPtrs.data(), inChannels, outChannels, frames);
    p->params = p->encoder.params();
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[ch][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "64 Cloud In" : "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kInputsParamId, "Inputs", 1.0, 64.0, 64.0, true },
    { kCloudsParamId, "Clouds", 1.0, 4.0, 1.0, true },
    { kCloudParamId, "Cloud", 1.0, 4.0, 1.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kAzimuthParamId, "Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Distance", 0.05, 8.0, 1.0, false },
    { kCloudGainParamId, "Cloud Gain", 0.0, 2.0, 1.0, false },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.45, false },
    { kElevationSpreadParamId, "Elevation Spread", 0.0, 1.0, 0.35, false },
    { kJitterParamId, "Jitter", 0.0, 1.0, 0.0, false },
    { kDriftParamId, "Drift", 0.0, 1.0, 0.0, false },
    { kRateParamId, "Rate", 0.001, 2.0, 0.035, false },
    { kDecorrelateParamId, "Decorrelate", 0.0, 1.0, 0.0, false },
    { kShapeParamId, "Shape", 0.0, 4.0, 0.0, true },
    { kForceParamId, "Force", 0.0, 4.0, 0.0, true },
    { kOutputParamId, "Output", -60.0, 12.0, -12.0, false },
};

constexpr uint32_t kBaseParamCount = static_cast<uint32_t>(std::size(kParams));
constexpr uint32_t kPerCloudParamCount = s3g::kAmbiCloudEncoderMaxClouds * 3u;

uint32_t paramsCount(const clap_plugin_t*) { return kBaseParamCount + kPerCloudParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    if (index >= kBaseParamCount) {
        const uint32_t rel = index - kBaseParamCount;
        const uint32_t cloud = rel / 3u;
        const uint32_t kind = rel % 3u;
        info->id = perCloudParamId(cloud, static_cast<CloudParamKind>(kind));
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::snprintf(info->name, sizeof(info->name), "Cloud %u %s", cloud + 1u,
            kind == 0u ? "Azimuth" : (kind == 1u ? "Elevation" : "Distance"));
        std::strncpy(info->module, "Ambi Clouds", sizeof(info->module));
        info->min_value = kind == 0u ? -180.0 : (kind == 1u ? -90.0 : 0.05);
        info->max_value = kind == 0u ? 180.0 : (kind == 1u ? 90.0 : 8.0);
        info->default_value = kind == 2u ? 1.0 : 0.0;
        return true;
    }
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Cloud Encoder", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* pluginSelf = self(plugin);
    uint32_t cloudIndex = 0;
    CloudParamKind cloudKind = CloudParamKind::Azimuth;
    if (decodePerCloudParam(id, cloudIndex, cloudKind)) {
        const auto& cloud = pluginSelf->encoder.clouds()[cloudIndex];
        switch (cloudKind) {
        case CloudParamKind::Azimuth: *value = cloud.azimuthDeg; return true;
        case CloudParamKind::Elevation: *value = cloud.elevationDeg; return true;
        case CloudParamKind::Distance: *value = cloud.distance; return true;
        }
    }
    const auto p = pluginSelf->params;
    switch (id) {
    case kInputsParamId: *value = p.activeInputs; return true;
    case kCloudsParamId: *value = p.activeClouds; return true;
    case kCloudParamId: *value = p.selectedCloud + 1u; return true;
    case kOrderParamId: *value = p.order; return true;
    case kAzimuthParamId: *value = p.selectedAzimuthDeg; return true;
    case kElevationParamId: *value = p.selectedElevationDeg; return true;
    case kDistanceParamId: *value = p.selectedDistance; return true;
    case kCloudGainParamId: *value = p.selectedGain; return true;
    case kSpreadParamId: *value = p.spread; return true;
    case kElevationSpreadParamId: *value = p.elevationSpread; return true;
    case kJitterParamId: *value = p.jitter; return true;
    case kDriftParamId: *value = p.drift; return true;
    case kRateParamId: *value = p.rateHz; return true;
    case kDecorrelateParamId: *value = p.decorrelate; return true;
    case kShapeParamId: *value = static_cast<uint32_t>(p.shape); return true;
    case kForceParamId: *value = static_cast<uint32_t>(p.force); return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    uint32_t cloudIndex = 0;
    CloudParamKind cloudKind = CloudParamKind::Azimuth;
    if (decodePerCloudParam(id, cloudIndex, cloudKind)) {
        if (cloudKind == CloudParamKind::Distance) std::snprintf(display, size, "%.2f", value);
        else std::snprintf(display, size, "%+.1f deg", value);
        return true;
    }
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kShapeParamId) std::snprintf(display, size, "%s", shapeName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kForceParamId) std::snprintf(display, size, "%s", forceName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kAzimuthParamId || id == kElevationParamId) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kSpreadParamId || id == kElevationSpreadParamId || id == kJitterParamId || id == kDriftParamId || id == kDecorrelateParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kRateParamId) std::snprintf(display, size, "%.3f Hz", value);
    else std::snprintf(display, size, "%.2f", value);
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
    auto* p = self(plugin);
    SavedState state { kStateVersion, p->params, p->encoder.clouds() };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!readExact(stream, &state, sizeof(state))) return false;
    if (state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = state.params;
    p->encoder.setClouds(state.clouds);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GAmbiCloudEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    int _dragParam;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    NSPoint _lastDragPoint;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)setViewPreset:(int)mode;
- (NSPoint)projectDirection:(s3g::Vec3)dir inRect:(NSRect)rect;
- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawSlider:(NSString*)name value:(double)value min:(double)min max:(double)max y:(CGFloat)y suffix:(NSString*)suffix attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)setParam:(clap_id)param fromPoint:(NSPoint)pt;
@end

static NSColor* cloudColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

@implementation S3GAmbiCloudEncoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 672)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _dragParam = 0;
        _viewMode = 0;
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
        _viewZoom = 1.0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        [self setWantsLayer:YES];
        [[self window] setAcceptsMouseMovedEvents:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 20.0
                                             target:self
                                           selector:@selector(timerTick:)
                                           userInfo:nil
                                            repeats:YES];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)timerTick:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (NSPoint)projectDirection:(s3g::Vec3)dir inRect:(NSRect)rect
{
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.38 * std::clamp(_viewZoom, 0.55, 2.20);
    const CGFloat cx = NSMidX(rect);
    const CGFloat cy = NSMidY(rect);
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * dir.x - sa * dir.y;
    const float y1 = sa * dir.x + ca * dir.y;
    const float y2 = ce * y1 + se * dir.z;
    return NSMakePoint(cx + static_cast<CGFloat>(x1) * scale, cy - static_cast<CGFloat>(y2) * scale);
}

- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 38.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    const CGFloat x = NSMaxX(rect) - 10.0 - (3.0 - static_cast<CGFloat>(index)) * w - (2.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 4.0, w, h);
}

- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 18.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 4.0;
    const CGFloat viewStart = [self viewButtonRect:0 inRect:rect].origin.x;
    const CGFloat x = viewStart - 12.0 - (2.0 - static_cast<CGFloat>(index)) * w - (1.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 4.0, w, h);
}

- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:rect], rect, labels[i], i == _viewMode, attrs, style);
    }
}

- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"-", @"+" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 2; ++i) {
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:rect], rect, labels[i], false, attrs, style);
    }
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 35.0;
        _viewElDeg = 34.0;
    }
    [self setNeedsDisplay:YES];
}

- (void)drawSlider:(NSString*)name value:(double)value min:(double)min max:(double)max y:(CGFloat)y suffix:(NSString*)suffix attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const double norm = (value - min) / std::max(0.000001, max - min);
    NSString* text = nil;
    if (suffix && [suffix isEqualToString:@"OA"]) text = [NSString stringWithFormat:@"%.0fOA", value];
    else if (suffix && [suffix isEqualToString:@"deg"]) text = [NSString stringWithFormat:@"%+.0f", value];
    else if (suffix && [suffix isEqualToString:@"%"]) text = [NSString stringWithFormat:@"%.0f%%", value * 100.0];
    else if (suffix && [suffix isEqualToString:@"Hz"]) text = [NSString stringWithFormat:@"%.3f", value];
    else if (suffix && [suffix isEqualToString:@"dB"]) text = [NSString stringWithFormat:@"%+.1f", value];
    else if (suffix && [suffix isEqualToString:@"shape"]) text = [NSString stringWithUTF8String:shapeName(static_cast<uint32_t>(std::lround(value)))];
    else if (suffix && [suffix isEqualToString:@"force"]) text = [NSString stringWithUTF8String:forceName(static_cast<uint32_t>(std::lround(value)))];
    else text = [NSString stringWithFormat:@"%.2f", value];
    s3g::clap_gui::drawSlider(name, text, norm, y, attrs, attrs, style, 642, 738, 826, 82);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, attrs, style, 642, 738, 102);
}

- (CGFloat)menuY
{
    switch (_openMenu) {
    case 1: return 122.0;
    case 2: return 148.0;
    case 3: return 174.0;
    case 4: return 396.0;
    case 5: return 422.0;
    default: return 0.0;
    }
}

- (CGFloat)menuW
{
    return (_openMenu == 4 || _openMenu == 5) ? 124.0 : 102.0;
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    NSString* cloudItems[] = { @"1", @"2", @"3", @"4" };
    NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    NSString* shapeItems[] = { @"CUMULUS", @"STRATUS", @"CIRRUS", @"LENTIC", @"STORM" };
    NSString* forceItems[] = { @"CALM", @"ADVECT", @"SHEAR", @"CONVECT", @"TURB" };
    NSString** items = cloudItems;
    int selected = 0;
    if (_openMenu == 1) {
        items = cloudItems;
        selected = static_cast<int>(_plugin->params.activeClouds) - 1;
        _menuItemCount = 4;
    } else if (_openMenu == 2) {
        items = cloudItems;
        selected = static_cast<int>(_plugin->params.selectedCloud);
        _menuItemCount = 4;
    } else if (_openMenu == 3) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
        _menuItemCount = 7;
    } else if (_openMenu == 4) {
        items = shapeItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.shape));
        _menuItemCount = 5;
    } else if (_openMenu == 5) {
        items = forceItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.force));
        _menuItemCount = 5;
    }
    const CGFloat itemH = 18.0;
    s3g::clap_gui::drawDropdownMenu(NSMakeRect(738, [self menuY], [self menuW], itemH * _menuItemCount),
        itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [cloudColor(0x111111) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    const auto params = _plugin->params;
    const auto& clouds = _plugin->encoder.clouds();
    [cloudColor(0x2b2b2b, 0.75) setStroke];
    NSBezierPath* cross = [NSBezierPath bezierPath];
    [cross moveToPoint:NSMakePoint(NSMidX(rect), rect.origin.y + 16)];
    [cross lineToPoint:NSMakePoint(NSMidX(rect), NSMaxY(rect) - 16)];
    [cross moveToPoint:NSMakePoint(rect.origin.x + 16, NSMidY(rect))];
    [cross lineToPoint:NSMakePoint(NSMaxX(rect) - 16, NSMidY(rect))];
    [cross setLineWidth:0.75];
    [cross stroke];

    const uint32_t activeInputs = std::min<uint32_t>(params.activeInputs, s3g::kAmbiCloudEncoderMaxInputs);
    for (uint32_t i = 0; i < activeInputs; ++i) {
        const s3g::Vec3 dir = _plugin->encoder.sourcePositionForDisplay(i);
        const NSPoint p = [self projectDirection:dir inRect:rect];
        const uint32_t cloudIndex = i % std::max<uint32_t>(1u, params.activeClouds);
        const int colors[] { 0x9fd0ff, 0xf0d35d, 0xe59bd8, 0x8bd5a5 };
        [cloudColor(colors[std::min<uint32_t>(cloudIndex, 3u)], 0.58) setFill];
        const CGFloat r = 2.4;
        NSRectFill(NSMakeRect(std::round(p.x - r), std::round(p.y - r), r * 2.0, r * 2.0));
    }

    for (uint32_t i = 0; i < params.activeClouds; ++i) {
        const auto& c = clouds[i];
        s3g::Vec3 centerDir = s3g::directionFromAed(c.azimuthDeg, c.elevationDeg);
        const float centerRadius = s3g::AmbiCloudEncoder::displayDistance(c.distance);
        centerDir = { centerDir.x * centerRadius, centerDir.y * centerRadius, centerDir.z * centerRadius };
        const NSPoint p = [self projectDirection:centerDir inRect:rect];
        const CGFloat r = i == params.selectedCloud ? 8.0 : 6.0;
        [cloudColor(0xd8ddd8, i == params.selectedCloud ? 0.94 : 0.65) setFill];
        NSRectFill(NSMakeRect(std::round(p.x - r), std::round(p.y - r), r * 2.0, r * 2.0));
        [cloudColor(0x050505, 0.95) setStroke];
        NSFrameRect(NSMakeRect(std::round(p.x - r), std::round(p.y - r), r * 2.0, r * 2.0));
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        NSDictionary* idAttrs = @{ NSForegroundColorAttributeName:cloudColor(0x111111),
                                   NSFontAttributeName:[NSFont fontWithName:@"Menlo-Bold" size:8.0] ?: [NSFont monospacedSystemFontOfSize:8.0 weight:NSFontWeightBold] };
        NSSize sz = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(p.x - sz.width * 0.5, p.y - sz.height * 0.5 - 0.5) withAttributes:idAttrs];
    }

    [@"CLOUD FIELD" drawAtPoint:NSMakePoint(rect.origin.x + 10, rect.origin.y + 8) withAttributes:attrs];
    NSString* viewText = _viewMode == 0 ? @"TOP VIEW" : (_viewMode == 1 ? @"SIDE VIEW" : @"3/4 VIEW");
    [viewText drawAtPoint:NSMakePoint(rect.origin.x + 10, NSMaxY(rect) - 22) withAttributes:attrs];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const s3g::clap_gui::Style style {};
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSFont* font = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* attrs = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:font };
    NSDictionary* dimAttrs = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:font };
    [@"s3g AMBI CLOUD ENCODER" drawAtPoint:NSMakePoint(18, 14) withAttributes:attrs];
    const float peak = _plugin->outputPeak.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PK %+4.1f", 20.0 * std::log10(std::max(0.000001f, peak))] drawAtPoint:NSMakePoint(728, 14) withAttributes:dimAttrs];
    [@"64CH" drawAtPoint:NSMakePoint(838, 14) withAttributes:dimAttrs];

    s3g::clap_gui::drawPanelFrame(18, 42, 596, 608, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD", true, 18, 42, 596, 21, attrs, style);
    [self drawZoomButtonsInRect:NSMakeRect(18, 42, 596, 608) attrs:dimAttrs];
    [self drawViewButtonsInRect:NSMakeRect(18, 42, 596, 608) attrs:dimAttrs];
    [self drawField:NSMakeRect(34, 76, 564, 558) attrs:dimAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 42, 250, 236, style);
    s3g::clap_gui::drawPanelHeader(@"CLOUD", true, 630, 42, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(630, 290, 250, 360, style);
    s3g::clap_gui::drawPanelHeader(@"MATERIAL", true, 630, 290, 250, 21, attrs, style);
    const auto p = _plugin->params;
    [self drawSlider:@"INPUTS" value:p.activeInputs min:1 max:64 y:78 suffix:nil attrs:attrs style:style];
    [self drawMenu:@"CLOUDS" value:[NSString stringWithFormat:@"%u", p.activeClouds] y:104 attrs:attrs style:style];
    [self drawMenu:@"CLOUD" value:[NSString stringWithFormat:@"%u", p.selectedCloud + 1u] y:130 attrs:attrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] y:156 attrs:attrs style:style];
    [self drawSlider:@"AZIM" value:p.selectedAzimuthDeg min:-180 max:180 y:182 suffix:@"deg" attrs:attrs style:style];
    [self drawSlider:@"ELEV" value:p.selectedElevationDeg min:-90 max:90 y:208 suffix:@"deg" attrs:attrs style:style];
    [self drawSlider:@"DIST" value:p.selectedDistance min:0.05 max:8.0 y:234 suffix:nil attrs:attrs style:style];
    [self drawSlider:@"SPREAD" value:p.spread min:0 max:1 y:326 suffix:@"%" attrs:attrs style:style];
    [self drawSlider:@"ELEVSP" value:p.elevationSpread min:0 max:1 y:352 suffix:@"%" attrs:attrs style:style];
    [self drawMenu:@"SHAPE" value:[NSString stringWithUTF8String:shapeName(static_cast<uint32_t>(p.shape))] y:378 attrs:attrs style:style];
    [self drawMenu:@"FORCE" value:[NSString stringWithUTF8String:forceName(static_cast<uint32_t>(p.force))] y:404 attrs:attrs style:style];
    [self drawSlider:@"JITTER" value:p.jitter min:0 max:1 y:430 suffix:@"%" attrs:attrs style:style];
    [self drawSlider:@"DRIFT" value:p.drift min:0 max:1 y:456 suffix:@"%" attrs:attrs style:style];
    [self drawSlider:@"RATE" value:p.rateHz min:0.001 max:2.0 y:482 suffix:@"Hz" attrs:attrs style:style];
    [self drawSlider:@"DECOR" value:p.decorrelate min:0 max:1 y:508 suffix:@"%" attrs:attrs style:style];
    [self drawSlider:@"GAIN" value:p.selectedGain min:0 max:2 y:534 suffix:nil attrs:attrs style:style];
    [self drawSlider:@"OUT" value:p.outputGainDb min:-60 max:12 y:586 suffix:@"dB" attrs:attrs style:style];
    [self drawOpenMenu:attrs style:style];
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)pt
{
    auto slider = [&](double min, double max) {
        const double norm = std::clamp((static_cast<double>(pt.x) - 738.0) / 82.0, 0.0, 1.0);
        applyParam(*_plugin, param, min + norm * (max - min));
    };
    switch (param) {
    case kInputsParamId: slider(1, 64); break;
    case kCloudsParamId: slider(1, 4); break;
    case kCloudParamId: slider(1, 4); break;
    case kOrderParamId: slider(1, 7); break;
    case kAzimuthParamId: slider(-180, 180); break;
    case kElevationParamId: slider(-90, 90); break;
    case kDistanceParamId: slider(0.05, 8.0); break;
    case kSpreadParamId: slider(0, 1); break;
    case kElevationSpreadParamId: slider(0, 1); break;
    case kShapeParamId: slider(0, 4); break;
    case kForceParamId: slider(0, 4); break;
    case kJitterParamId: slider(0, 1); break;
    case kDriftParamId: slider(0, 1); break;
    case kRateParamId: slider(0.001, 2.0); break;
    case kDecorrelateParamId: slider(0, 1); break;
    case kCloudGainParamId: slider(0, 2); break;
    case kOutputParamId: slider(-60, 12); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738, [self menuY], [self menuW], itemH * _menuItemCount), itemH, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kCloudsParamId, hit + 1);
            else if (_openMenu == 2) applyParam(*_plugin, kCloudParamId, hit + 1);
            else if (_openMenu == 3) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 4) applyParam(*_plugin, kShapeParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kForceParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    auto openMenu = [&](int menu, uint32_t count) {
        _openMenu = menu;
        _menuItemCount = count;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
    };
    const NSRect fieldPanel = NSMakeRect(18, 42, 596, 608);
    if (NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:fieldPanel])) {
                _viewZoom = std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self viewButtonRect:i inRect:fieldPanel])) {
                [self setViewPreset:i];
                return;
            }
        }
        if (NSPointInRect(pt, NSMakeRect(34, 76, 564, 558))) {
            _dragView = YES;
            _lastDragPoint = pt;
            _viewMode = -1;
            return;
        }
    }
    _dragParam = 0;
    if (NSPointInRect(pt, NSMakeRect(638, 70, 230, 24))) _dragParam = kInputsParamId;
    else if (NSPointInRect(pt, NSMakeRect(738, 103, 102, 17))) { openMenu(1, 4); return; }
    else if (NSPointInRect(pt, NSMakeRect(738, 129, 102, 17))) { openMenu(2, 4); return; }
    else if (NSPointInRect(pt, NSMakeRect(738, 155, 102, 17))) { openMenu(3, 7); return; }
    else if (NSPointInRect(pt, NSMakeRect(638, 174, 230, 24))) _dragParam = kAzimuthParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 200, 230, 24))) _dragParam = kElevationParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 226, 230, 24))) _dragParam = kDistanceParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 318, 230, 24))) _dragParam = kSpreadParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 344, 230, 24))) _dragParam = kElevationSpreadParamId;
    else if (NSPointInRect(pt, NSMakeRect(738, 377, 102, 17))) { openMenu(4, 5); return; }
    else if (NSPointInRect(pt, NSMakeRect(738, 403, 102, 17))) { openMenu(5, 5); return; }
    else if (NSPointInRect(pt, NSMakeRect(638, 422, 230, 24))) _dragParam = kJitterParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 448, 230, 24))) _dragParam = kDriftParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 474, 230, 24))) _dragParam = kRateParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 500, 230, 24))) _dragParam = kDecorrelateParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 526, 230, 24))) _dragParam = kCloudGainParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 578, 230, 24))) _dragParam = kOutputParamId;
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    if (!_dragParam) return;
    [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = 0;
    _dragView = NO;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat itemH = 18.0;
    const int next = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738, [self menuY], [self menuW], itemH * _menuItemCount), itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
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
    p->guiView = [[S3GAmbiCloudEncoderView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    [static_cast<S3GAmbiCloudEncoderView*>(p->guiView) stopRefreshTimer];
    [static_cast<NSView*>(p->guiView) removeFromSuperview];
    [static_cast<NSView*>(p->guiView) release];
    p->guiView = nullptr;
    p->guiVisible = false;
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 900; *h = 672; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win)
{
    if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false;
    auto* p = self(plugin);
    if (!p->guiView) return false;
    NSView* parent = static_cast<NSView*>(win->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, 900, 672)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbiCloudEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiCloudEncoderView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
} // namespace
#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-cloud-encoder-64",
    "s3g Ambi Cloud Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0-pre",
    "64-input ambisonic cloud encoder for distributing source lanes into one or more spatial clouds.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->encoder.prepare(p->sampleRate);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
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
    p->plugin.get_extension = getExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
