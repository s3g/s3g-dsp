#include "s3g_mc_to_quad.h"
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

constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kInputBusChannels = s3g::kMcToStereoMaxInputChannels;
constexpr uint32_t kOutputChannels = s3g::kMcToQuadOutputChannels;
constexpr uint32_t kGuiWidth = 920;
constexpr uint32_t kGuiHeight = 560;
constexpr double kGainRampMs = 48.0;

enum ParamId : clap_id {
    kParamInputChannels = 1,
    kParamWidth = 2,
    kParamRotation = 3,
    kParamAutogain = 4,
    kParamOutputGain = 5,
    kParamLayout = 6,
    kParamLayoutWeight = 7,
    kParamAttenuation3d = 8,
    kParamDistance3d = 9,
};

struct SavedState { uint32_t version = kStateVersion; s3g::McQuadParams params {}; };
struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::McQuadParams params {};
    std::array<s3g::McQuadChannelGains, kInputBusChannels> gains {};
    std::array<s3g::McQuadChannelGains, kInputBusChannels> currentGains {};
    std::array<s3g::McQuadChannelGains, kInputBusChannels> rampStartGains {};
    uint32_t cachedAvailableInputChannels = 0;
    uint32_t cachedResolvedInputChannels = 0;
    bool gainsDirty = true;
    bool gainsInitialized = false;
    uint32_t gainRampRemaining = 0;
    uint32_t gainRampTotal = 0;
    std::array<std::atomic<float>, kOutputChannels> outputPeaks {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }
uint32_t roundedUint(double value) { return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5))); }

const char* layoutName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 7u)) {
    case 1: return "Linear left-right";
    case 2: return "Odd/even stereo";
    case 3: return "Center-out";
    case 4: return "Pair-preserving";
    case 5: return "Sphere projection";
    case 6: return "Hemisphere projection";
    case 7: return "Cube projection";
    case 0:
    default: return "Ring projection";
    }
}

const char* autogainName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 2u)) {
    case 1: return "Power/sqrt(N)";
    case 2: return "Energy sum";
    case 0:
    default: return "Off";
    }
}

s3g::McQuadParams sanitizeParams(s3g::McQuadParams params)
{
    params.inputChannels = s3g::clampInputChannels(params.inputChannels);
    params.widthPercent = s3g::clampf(params.widthPercent, 0.0f, 200.0f);
    params.rotationDegrees = s3g::clampf(params.rotationDegrees, -180.0f, 180.0f);
    params.autogain = static_cast<s3g::McStereoAutogain>(std::min<uint32_t>(static_cast<uint32_t>(params.autogain), 2u));
    params.outputGainDb = s3g::clampf(params.outputGainDb, -24.0f, 24.0f);
    params.layout = static_cast<s3g::McStereoLayout>(std::min<uint32_t>(static_cast<uint32_t>(params.layout), 7u));
    params.layoutWeightPercent = s3g::clampf(params.layoutWeightPercent, 0.0f, 100.0f);
    params.attenuation3dPercent = s3g::clampf(params.attenuation3dPercent, 0.0f, 100.0f);
    params.distance3dPercent = s3g::clampf(params.distance3dPercent, 0.0f, 200.0f);
    return params;
}

bool floatChanged(float a, float b) { return std::abs(a - b) > 0.0001f; }
bool gainParamsChanged(const s3g::McQuadParams& a, const s3g::McQuadParams& b)
{
    return a.inputChannels != b.inputChannels || a.autogain != b.autogain || a.layout != b.layout
        || floatChanged(a.widthPercent, b.widthPercent) || floatChanged(a.rotationDegrees, b.rotationDegrees)
        || floatChanged(a.outputGainDb, b.outputGainDb) || floatChanged(a.layoutWeightPercent, b.layoutWeightPercent)
        || floatChanged(a.attenuation3dPercent, b.attenuation3dPercent) || floatChanged(a.distance3dPercent, b.distance3dPercent);
}

void setParamValue(Plugin& p, clap_id paramId, double value)
{
    const auto before = sanitizeParams(p.params);
    switch (paramId) {
    case kParamInputChannels: p.params.inputChannels = s3g::clampInputChannels(roundedUint(value)); break;
    case kParamWidth: p.params.widthPercent = s3g::clampf(static_cast<float>(value), 0.0f, 200.0f); break;
    case kParamRotation: p.params.rotationDegrees = s3g::clampf(static_cast<float>(value), -180.0f, 180.0f); break;
    case kParamAutogain: p.params.autogain = static_cast<s3g::McStereoAutogain>(std::min<uint32_t>(roundedUint(value), 2u)); break;
    case kParamOutputGain: p.params.outputGainDb = s3g::clampf(static_cast<float>(value), -24.0f, 24.0f); break;
    case kParamLayout: p.params.layout = static_cast<s3g::McStereoLayout>(std::min<uint32_t>(roundedUint(value), 7u)); break;
    case kParamLayoutWeight: p.params.layoutWeightPercent = s3g::clampf(static_cast<float>(value), 0.0f, 100.0f); break;
    case kParamAttenuation3d: p.params.attenuation3dPercent = s3g::clampf(static_cast<float>(value), 0.0f, 100.0f); break;
    case kParamDistance3d: p.params.distance3dPercent = s3g::clampf(static_cast<float>(value), 0.0f, 200.0f); break;
    default: return;
    }
    p.params = sanitizeParams(p.params);
    if (gainParamsChanged(before, p.params)) p.gainsDirty = true;
}

double getParamValue(const Plugin& p, clap_id paramId)
{
    switch (paramId) {
    case kParamInputChannels: return p.params.inputChannels;
    case kParamWidth: return p.params.widthPercent;
    case kParamRotation: return p.params.rotationDegrees;
    case kParamAutogain: return static_cast<uint32_t>(p.params.autogain);
    case kParamOutputGain: return p.params.outputGainDb;
    case kParamLayout: return static_cast<uint32_t>(p.params.layout);
    case kParamLayoutWeight: return p.params.layoutWeightPercent;
    case kParamAttenuation3d: return p.params.attenuation3dPercent;
    case kParamDistance3d: return p.params.distance3dPercent;
    default: return 0.0;
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
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
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
    p->maxFrames = maxFrames;
    p->params = sanitizeParams(p->params);
    p->gainsDirty = true;
    p->gainsInitialized = false;
    p->gainRampRemaining = 0;
    p->gainRampTotal = 0;
    return true;
}
void deactivate(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#endif
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t*) {}
void onMainThread(const clap_plugin_t*) {}

uint32_t resolvedInputChannels(const clap_process_t* process, const Plugin& p)
{
    const uint32_t available = process->audio_inputs_count > 0 ? std::min<uint32_t>(process->audio_inputs[0].channel_count, kInputBusChannels) : 0u;
    return std::min(s3g::clampInputChannels(p.params.inputChannels), std::max<uint32_t>(1u, available));
}

void updateGains(Plugin& p, uint32_t availableInputChannels, uint32_t resolved)
{
    if (!p.gainsDirty && p.cachedAvailableInputChannels == availableInputChannels && p.cachedResolvedInputChannels == resolved) return;
    p.rampStartGains = p.currentGains;
    s3g::makeMcToQuadGains(p.gains.data(), availableInputChannels, p.params);
    p.cachedAvailableInputChannels = availableInputChannels;
    p.cachedResolvedInputChannels = resolved;
    p.gainsDirty = false;
    if (!p.gainsInitialized) {
        p.currentGains = p.gains;
        p.gainsInitialized = true;
        p.gainRampRemaining = 0;
        p.gainRampTotal = 0;
    } else {
        p.gainRampTotal = std::max<uint32_t>(1u, static_cast<uint32_t>(p.sampleRate * kGainRampMs * 0.001));
        p.gainRampRemaining = p.gainRampTotal;
    }
}

float readInputSample(const clap_audio_buffer_t* input, uint32_t channel, uint32_t frame)
{
    if (!input || channel >= input->channel_count) return 0.0f;
    if (input->data32 && input->data32[channel]) return input->data32[channel][frame];
    if (input->data64 && input->data64[channel]) return static_cast<float>(input->data64[channel][frame]);
    return 0.0f;
}

void writeOutputSample(const clap_audio_buffer_t& output, uint32_t channel, uint32_t frame, float value)
{
    if (channel >= output.channel_count) return;
    if (output.data32 && output.data32[channel]) output.data32[channel][frame] = value;
    if (output.data64 && output.data64[channel]) output.data64[channel][frame] = static_cast<double>(value);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    if (proc->in_events) {
        const uint32_t n = proc->in_events->size(proc->in_events);
        for (uint32_t i = 0; i < n; ++i) {
            const clap_event_header_t* ev = proc->in_events->get(proc->in_events, i);
            if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
                setParamValue(*p, param->param_id, param->value);
            }
        }
    }
    if (proc->audio_outputs_count == 0 || proc->audio_outputs[0].channel_count < kOutputChannels) return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t* input = proc->audio_inputs_count > 0 ? &proc->audio_inputs[0] : nullptr;
    const clap_audio_buffer_t& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t available = input ? std::min<uint32_t>(input->channel_count, kInputBusChannels) : 0u;
    const uint32_t resolved = resolvedInputChannels(proc, *p);
    updateGains(*p, available, resolved);

    std::array<float, kOutputChannels> blockPeaks {};
    for (uint32_t frame = 0; frame < frames; ++frame) {
        float l = 0.0f, r = 0.0f, rb = 0.0f, lb = 0.0f;
        float rampT = 1.0f;
        if (p->gainRampRemaining > 0 && p->gainRampTotal > 0) {
            rampT = 1.0f - static_cast<float>(p->gainRampRemaining) / static_cast<float>(p->gainRampTotal);
            --p->gainRampRemaining;
        }
        for (uint32_t ch = 0; ch < resolved; ++ch) {
            const auto& target = p->gains[ch];
            const auto& start = p->rampStartGains[ch];
            auto& current = p->currentGains[ch];
            if (p->gainRampRemaining > 0) {
                current.left = s3g::lerp(start.left, target.left, rampT);
                current.right = s3g::lerp(start.right, target.right, rampT);
                current.rightBack = s3g::lerp(start.rightBack, target.rightBack, rampT);
                current.leftBack = s3g::lerp(start.leftBack, target.leftBack, rampT);
            } else {
                current = target;
            }
            const float x = readInputSample(input, ch, frame);
            l += x * current.left;
            r += x * current.right;
            rb += x * current.rightBack;
            lb += x * current.leftBack;
        }
        writeOutputSample(output, 0u, frame, l);
        writeOutputSample(output, 1u, frame, r);
        writeOutputSample(output, 2u, frame, rb);
        writeOutputSample(output, 3u, frame, lb);
        for (uint32_t ch = kOutputChannels; ch < output.channel_count; ++ch) writeOutputSample(output, ch, frame, 0.0f);
        blockPeaks[0] = std::max(blockPeaks[0], std::abs(l));
        blockPeaks[1] = std::max(blockPeaks[1], std::abs(r));
        blockPeaks[2] = std::max(blockPeaks[2], std::abs(rb));
        blockPeaks[3] = std::max(blockPeaks[3], std::abs(lb));
    }
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        p->outputPeaks[ch].store(std::max(p->outputPeaks[ch].load(std::memory_order_relaxed) * 0.90f, blockPeaks[ch]), std::memory_order_relaxed);
    }
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 10 : 20;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "MC In" : "Quad Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputBusChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_SURROUND : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; };
constexpr ParamDef kParamDefs[] {
    { kParamInputChannels, "Input Channels", 2.0, 128.0, 8.0 },
    { kParamWidth, "Width", 0.0, 200.0, 100.0 },
    { kParamRotation, "Rotation", -180.0, 180.0, 0.0 },
    { kParamAutogain, "Autogain", 0.0, 2.0, 1.0 },
    { kParamOutputGain, "Output Gain", -24.0, 24.0, 0.0 },
    { kParamLayout, "Layout", 0.0, 7.0, 0.0 },
    { kParamLayoutWeight, "Layout Weight", 0.0, 100.0, 100.0 },
    { kParamAttenuation3d, "3D Atten", 0.0, 100.0, 45.0 },
    { kParamDistance3d, "3D Distance", 0.0, 200.0, 100.0 },
};
uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.id == kParamAutogain || def.id == kParamLayout || def.id == kParamInputChannels) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "MC to Quad", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value) { if (!value) return false; *value = getParamValue(*self(plugin), id); return true; }
bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kParamAutogain) std::snprintf(display, size, "%s", autogainName(roundedUint(value)));
    else if (id == kParamLayout) std::snprintf(display, size, "%s", layoutName(roundedUint(value)));
    else if (id == kParamInputChannels) std::snprintf(display, size, "%u", roundedUint(value));
    else if (id == kParamRotation) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kParamOutputGain) std::snprintf(display, size, "%+.1f dB", value);
    else std::snprintf(display, size, "%.0f%%", value);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value) { if (!display || !value) return false; *value = std::atof(display); return true; }
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    auto* p = self(plugin);
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParamValue(*p, param->param_id, param->value);
        }
    }
}
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
    auto* p = self(plugin);
    p->params = sanitizeParams(s.params);
    p->gainsDirty = true;
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
static NSColor* s3gMcQuadColor(int rgb, CGFloat alpha = 1.0)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:alpha];
}

@interface S3GMcQuadView : NSView { void* _plugin; int _dragSlider; int _openMenu; int _hoverMenuItem; uint32_t _menuItems; NSPoint _menuOrigin; NSTimer* _timer; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(NSString*)name value:(NSString*)value norm:(CGFloat)norm x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawMenuRow:(NSString*)name value:(NSString*)value x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)updateMenuHover:(NSPoint)point;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GMcQuadView
- (id)initWithPlugin:(void*)plugin { self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; if (self) { _plugin = plugin; _dragSlider = -1; _openMenu = 0; _hoverMenuItem = -1; _menuItems = 0; _menuOrigin = NSMakePoint(0, 0); _timer = nil; } return self; }
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
- (void)drawMenuRow:(NSString*)name value:(NSString*)value x:(CGFloat)x y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawMenu(name, value, y, attrs, small, style, x, x + 94, 190);
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    NSColor* grid = style.grid;
    NSColor* dim = style.dim;
    NSColor* text = style.text;
    NSColor* fill = style.fill;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* bold = [NSFont fontWithName:@"Menlo-Bold" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightBold];
    NSDictionary* lab = @{ NSForegroundColorAttributeName:text, NSFontAttributeName:bold };
    NSDictionary* small = @{ NSForegroundColorAttributeName:dim, NSFontAttributeName:mono };
    NSDictionary* title = @{ NSForegroundColorAttributeName:text, NSFontAttributeName:mono };
    const auto& prm = p->params;
    const uint32_t count = s3g::clampInputChannels(prm.inputChannels);
    const uint32_t layout = static_cast<uint32_t>(prm.layout);

    [@"s3g MC TO QUAD AUTOGAIN" drawAtPoint:NSMakePoint(18, 13) withAttributes:title];
    [@"128IN / 4OUT" drawAtPoint:NSMakePoint(824, 13) withAttributes:small];

    NSRect mapPanel = NSMakeRect(12, 34, 564, 514);
    s3g::clap_gui::drawPanelFrame(mapPanel.origin.x, mapPanel.origin.y, mapPanel.size.width, mapPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"QUAD MAP", true, mapPanel.origin.x, mapPanel.origin.y, mapPanel.size.width, 21, lab, style);
    NSString* info = [NSString stringWithFormat:@"%u IN / %@ / W %.0f%% / ROT %.0f / %@", count, [NSString stringWithUTF8String:layoutName(layout)], static_cast<double>(prm.widthPercent), static_cast<double>(prm.rotationDegrees), [NSString stringWithUTF8String:autogainName(static_cast<uint32_t>(prm.autogain))]];
    [info drawAtPoint:NSMakePoint(150, 39) withAttributes:small];

    NSRect field = NSMakeRect(28, 68, 532, 404);
    [s3gMcQuadColor(0x101010) setFill]; NSRectFill(field);
    [grid setStroke]; NSFrameRect(field);
    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.5;
    const CGFloat radius = std::min(field.size.width, field.size.height) * 0.39;
    NSPoint speakerPts[4] = { NSMakePoint(cx - radius * 0.72, cy - radius * 0.72), NSMakePoint(cx + radius * 0.72, cy - radius * 0.72), NSMakePoint(cx + radius * 0.72, cy + radius * 0.72), NSMakePoint(cx - radius * 0.72, cy + radius * 0.72) };
    NSString* speakerNames[4] = { @"L", @"R", @"RB", @"LB" };
    [s3gMcQuadColor(0x747474, 0.18) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx - radius, cy) toPoint:NSMakePoint(cx + radius, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, cy - radius) toPoint:NSMakePoint(cx, cy + radius)];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cx - radius, cy - radius, radius * 2, radius * 2)] stroke];
    [s3gMcQuadColor(0x2e2e2e) setFill];
    for (uint32_t i = 0; i < 4; ++i) {
        NSRect box = NSMakeRect(speakerPts[i].x - 16, speakerPts[i].y - 16, 32, 32);
        NSRectFill(box);
        [grid setStroke]; NSFrameRect(box);
        NSSize ts = [speakerNames[i] sizeWithAttributes:lab];
        [speakerNames[i] drawAtPoint:NSMakePoint(speakerPts[i].x - ts.width * 0.5, speakerPts[i].y - 7) withAttributes:lab];
    }
    auto shouldLabel = [&](uint32_t i) -> bool { const uint32_t interval = count <= 16 ? 1u : count <= 32 ? 4u : count <= 64 ? 8u : 16u; return i == 0 || i + 1 == count || (i % interval) == 0; };
    for (uint32_t i = 0; i < count; ++i) {
        const auto pos = s3g::mcQuadPositionForChannel(i, count, prm);
        const double az = (pos.azimuthDegrees + prm.rotationDegrees - 90.0) * M_PI / 180.0;
        const CGFloat height = std::clamp<CGFloat>(std::abs(pos.elevationDegrees) / 90.0, 0.0, 1.0);
        const CGFloat dotRadius = radius * (0.62 + height * 0.28);
        const CGFloat x = cx + std::cos(az) * dotRadius;
        const CGFloat y = cy + std::sin(az) * dotRadius;
        const auto gains = s3g::quadGainsForChannel(i, count, prm);
        const float gainVals[4] = { gains.left, gains.right, gains.rightBack, gains.leftBack };
        float maxGain = 0.0f; for (float g : gainVals) maxGain = std::max(maxGain, std::abs(g));
        const CGFloat dot = count <= 16 ? 5.0 : count <= 32 ? 4.0 : 3.0;
        const CGFloat visible = std::clamp<CGFloat>(maxGain, 0.16, 1.0);
        for (uint32_t sp = 0; sp < 4; ++sp) {
            const CGFloat alpha = 0.08 + 0.30 * std::clamp<CGFloat>(std::abs(gainVals[sp]), 0.0, 1.0);
            [s3gMcQuadColor(sp < 2 ? 0xd8d8d8 : 0xb8b8b8, alpha) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(x, y) toPoint:NSMakePoint(speakerPts[sp].x, speakerPts[sp].y)];
        }
        [fill setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - dot * visible, y - dot * visible, dot * 2 * visible, dot * 2 * visible)] fill];
        if (shouldLabel(i)) [[NSString stringWithFormat:@"%u", i + 1] drawAtPoint:NSMakePoint(x + 6, y - 8) withAttributes:small];
    }
    [@"L R RB LB" drawAtPoint:NSMakePoint(42, 488) withAttributes:lab];
    [@"output order" drawAtPoint:NSMakePoint(110, 488) withAttributes:small];
    const CGFloat weightNorm = std::clamp<CGFloat>(prm.layoutWeightPercent / 100.0f, 0, 1);
    const CGFloat attenNorm = std::clamp<CGFloat>(prm.attenuation3dPercent / 100.0f, 0, 1);
    [@"WGT" drawAtPoint:NSMakePoint(300, 486) withAttributes:small]; [grid setStroke]; NSFrameRect(NSMakeRect(334, 489, 86, 7)); [fill setFill]; NSRectFill(NSMakeRect(335, 490, 84 * weightNorm, 5));
    [@"3D" drawAtPoint:NSMakePoint(436, 486) withAttributes:small]; [grid setStroke]; NSFrameRect(NSMakeRect(462, 489, 72, 7)); [fill setFill]; NSRectFill(NSMakeRect(463, 490, 70 * attenNorm, 5));

    NSRect side = NSMakeRect(592, 34, 316, 514);
    s3g::clap_gui::drawPanelFrame(side.origin.x, side.origin.y, side.size.width, side.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"AUDITION", true, side.origin.x, side.origin.y, side.size.width, 21, lab, style);
    [self drawRow:@"IN" value:[NSString stringWithFormat:@"%u", count] norm:(count - 2.0) / 126.0 x:600 y:74 attrs:lab small:small];
    [self drawRow:@"WDTH" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.widthPercent)] norm:prm.widthPercent / 200.0 x:600 y:96 attrs:lab small:small];
    [self drawRow:@"ROT" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(prm.rotationDegrees)] norm:(prm.rotationDegrees + 180.0) / 360.0 x:600 y:118 attrs:lab small:small];
    [self drawMenuRow:@"AGN" value:[NSString stringWithUTF8String:autogainName(static_cast<uint32_t>(prm.autogain))] x:600 y:140 attrs:lab small:small];
    [self drawRow:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(prm.outputGainDb)] norm:(prm.outputGainDb + 24.0) / 48.0 x:600 y:162 attrs:lab small:small];
    [self drawMenuRow:@"LAY" value:[NSString stringWithUTF8String:layoutName(layout)] x:600 y:198 attrs:lab small:small];
    [self drawRow:@"WGT" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.layoutWeightPercent)] norm:prm.layoutWeightPercent / 100.0 x:600 y:220 attrs:lab small:small];
    [self drawRow:@"ATT" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.attenuation3dPercent)] norm:prm.attenuation3dPercent / 100.0 x:600 y:242 attrs:lab small:small];
    [self drawRow:@"DST" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.distance3dPercent)] norm:prm.distance3dPercent / 200.0 x:600 y:264 attrs:lab small:small];

    NSRect meterPanel = NSMakeRect(604, 306, 292, 154);
    [s3gMcQuadColor(0x111111) setFill]; NSRectFill(meterPanel);
    [grid setStroke]; NSFrameRect(meterPanel);
    [@"QUAD OUT" drawAtPoint:NSMakePoint(616, 314) withAttributes:lab];
    auto drawMeter = [&](CGFloat y, NSString* name, uint32_t index) {
        const float current = p->outputPeaks[index].load(std::memory_order_relaxed);
        const float pk = p->outputPeaks[index].exchange(current * 0.92f, std::memory_order_relaxed);
        const double db = 20.0 * std::log10(std::max(0.000001f, pk));
        const CGFloat norm = std::clamp<CGFloat>((db + 60.0) / 60.0, 0.0, 1.0);
        NSRect r = NSMakeRect(650, y, 178, 15);
        [style.bg setFill]; NSRectFill(r);
        [fill setFill]; NSRectFill(NSMakeRect(r.origin.x + 2, r.origin.y + 2, (r.size.width - 4) * norm, r.size.height - 4));
        [grid setStroke]; NSFrameRect(r);
        [name drawAtPoint:NSMakePoint(616, y) withAttributes:small];
        [[NSString stringWithFormat:@"%+4.1f", db] drawAtPoint:NSMakePoint(838, y) withAttributes:small];
    };
    drawMeter(414, @"L", 0); drawMeter(390, @"R", 1); drawMeter(366, @"RB", 2); drawMeter(342, @"LB", 3);

    if (_openMenu > 0 && _menuItems > 0) {
        const CGFloat itemH = 18.0;
        NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 190, itemH * _menuItems);
        NSString* layoutItems[] = {
            [NSString stringWithUTF8String:layoutName(0)],
            [NSString stringWithUTF8String:layoutName(1)],
            [NSString stringWithUTF8String:layoutName(2)],
            [NSString stringWithUTF8String:layoutName(3)],
            [NSString stringWithUTF8String:layoutName(4)],
            [NSString stringWithUTF8String:layoutName(5)],
            [NSString stringWithUTF8String:layoutName(6)],
            [NSString stringWithUTF8String:layoutName(7)],
        };
        NSString* autogainItems[] = {
            [NSString stringWithUTF8String:autogainName(0)],
            [NSString stringWithUTF8String:autogainName(1)],
            [NSString stringWithUTF8String:autogainName(2)],
        };
        if (_openMenu == 1) {
            s3g::clap_gui::drawDropdownMenu(menu, itemH, autogainItems, _menuItems, static_cast<int>(prm.autogain), _hoverMenuItem, small, style);
        } else {
            s3g::clap_gui::drawDropdownMenu(menu, itemH, layoutItems, _menuItems, static_cast<int>(layout), _hoverMenuItem, small, style);
        }
    }
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItems == 0) return;
    const CGFloat itemH = 18.0;
    const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 190, itemH * _menuItems);
    const int next = s3g::clap_gui::dropdownHitIndex(point, menu, itemH, _menuItems);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((point.x - 694.0) / 150.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 1: setParamValue(*p, kParamInputChannels, 2.0 + n * 126.0); break;
    case 2: setParamValue(*p, kParamWidth, n * 200.0); break;
    case 3: setParamValue(*p, kParamRotation, -180.0 + n * 360.0); break;
    case 5: setParamValue(*p, kParamOutputGain, -24.0 + n * 48.0); break;
    case 7: setParamValue(*p, kParamLayoutWeight, n * 100.0); break;
    case 8: setParamValue(*p, kParamAttenuation3d, n * 100.0); break;
    case 9: setParamValue(*p, kParamDistance3d, n * 200.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 190, itemH * _menuItems);
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, menu, itemH, _menuItems);
        if (hit >= 0) {
            const uint32_t value = static_cast<uint32_t>(hit);
            setParamValue(*p, _openMenu == 1 ? kParamAutogain : kParamLayout, value);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItems = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat rows[] = {74,96,118,140,162,198,220,242,264};
    for (int i = 0; i < 9; ++i) {
        if (NSPointInRect(pt, NSMakeRect(596, rows[i] - 9, 300, 22))) {
            if (i == 3) {
                _openMenu = 1;
                _menuItems = 3;
                _menuOrigin = NSMakePoint(694, rows[i] + 17);
                _hoverMenuItem = -1;
                [self setNeedsDisplay:YES];
                return;
            }
            if (i == 5) {
                _openMenu = 2;
                _menuItems = 8;
                _menuOrigin = NSMakePoint(694, rows[i] + 17);
                _hoverMenuItem = -1;
                [self setNeedsDisplay:YES];
                return;
            }
            _dragSlider = i + 1;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseMoved:(NSEvent*)event { [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:pt];
    if (_dragSlider > 0) [self updateSlider:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; }
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GMcQuadView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { auto* v = static_cast<S3GMcQuadView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiWidth,kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GMcQuadView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<S3GMcQuadView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    "org.s3g.s3g-dsp.mc-to-quad-autogain",
    "s3g MC to Quad Autogain",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Multichannel fold-down to quad with output order L, R, RB, LB.",
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
