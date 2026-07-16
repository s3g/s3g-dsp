#include "s3g_ambisonic_head_decoder.h"

#include <clap/clap.h>
#include "s3g_realtime.h"
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

constexpr uint32_t kStateVersion = 2;
constexpr uint32_t kInputChannels = s3g::kAmbiHeadMaxChannels;
constexpr uint32_t kOutputChannels = 2;
constexpr uint32_t kParamCount = 19;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamLayout = 2,
    kParamWeighting = 3,
    kParamAutogain = 4,
    kParamHead = 5,
    kParamMode = 6,
    kParamYaw = 7,
    kParamPitch = 8,
    kParamWidth = 9,
    kParamPinna = 10,
    kParamRoom = 11,
    kParamXtcAmount = 12,
    kParamXtcMode = 13,
    kParamSpeakerAngle = 14,
    kParamHeadWidth = 15,
    kParamLowProtect = 16,
    kParamPreserve = 17,
    kParamOutput = 18,
    kParamDecodeMode = 19,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiHeadParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiHeadParams params {};
    s3g::AmbiHeadDecoder decoder {};
    bool paramsDirty = true;
    std::atomic<float> peakL { 0.0f };
    std::atomic<float> peakR { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

const char* layoutName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 4u)) {
    case 0: return "Quad virtual";
    case 1: return "8ch cube";
    case 2: return "12ch dodeca";
    case 3: return "24ch dome";
    case 4:
    default: return "32ch sphere";
    }
}

const char* weightingName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 2u)) {
    case 0: return "Projection";
    case 1: return "Energy-normalized";
    case 2:
    default: return "Max-rE";
    }
}

const char* autogainName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 2u)) {
    case 0: return "Off";
    case 1: return "Power/sqrt(N)";
    case 2:
    default: return "Energy sum";
    }
}

const char* headName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 2u)) {
    case 1: return "Small head";
    case 2: return "Large head";
    case 0:
    default: return "Medium head";
    }
}

const char* decodeModeName(uint32_t value)
{
    return value == 1u ? "Virtual field" : "Direct";
}

const char* modeName(uint32_t value)
{
    return value == 1u ? "Transaural" : "Binaural";
}

const char* xtcModeName(uint32_t value)
{
    return value == 1u ? "Matrix inverse" : "Feedforward";
}

s3g::AmbiHeadParams sanitizeParams(s3g::AmbiHeadParams p)
{
    s3g::AmbiHeadDecoder d;
    d.updateParams(p);
    return d.params();
}

void markDirty(Plugin& p)
{
    p.paramsDirty = true;
}

void setParamValue(Plugin& p, clap_id paramId, double value)
{
    switch (paramId) {
    case kParamOrder: p.params.order = std::clamp<uint32_t>(roundedUint(value), 1u, s3g::kAmbiHeadMaxOrder); break;
    case kParamLayout: p.params.layout = static_cast<s3g::AmbiStereoVirtualLayout>(std::min<uint32_t>(roundedUint(value), 4u)); break;
    case kParamDecodeMode: p.params.decodeMode = static_cast<s3g::AmbiHeadDecodeMode>(std::min<uint32_t>(roundedUint(value), 1u)); break;
    case kParamWeighting: p.params.weighting = static_cast<s3g::AmbiStereoWeighting>(std::min<uint32_t>(roundedUint(value), 2u)); break;
    case kParamAutogain: p.params.autogain = static_cast<s3g::AmbiStereoAutogain>(std::min<uint32_t>(roundedUint(value), 2u)); break;
    case kParamHead: p.params.head = static_cast<s3g::AmbiHeadProfile>(std::min<uint32_t>(roundedUint(value), 4u)); break;
    case kParamMode: p.params.mode = static_cast<s3g::AmbiHeadMode>(std::min<uint32_t>(roundedUint(value), 1u)); break;
    case kParamYaw: p.params.yawDeg = static_cast<float>(value); break;
    case kParamPitch: p.params.pitchDeg = static_cast<float>(value); break;
    case kParamWidth: p.params.widthPercent = static_cast<float>(value); break;
    case kParamPinna: p.params.pinnaPercent = static_cast<float>(value); break;
    case kParamRoom: p.params.roomPercent = static_cast<float>(value); break;
    case kParamXtcAmount: p.params.xtcAmountPercent = static_cast<float>(value); break;
    case kParamXtcMode: p.params.xtcMode = static_cast<s3g::AmbiHeadXtcMode>(std::min<uint32_t>(roundedUint(value), 1u)); break;
    case kParamSpeakerAngle: p.params.speakerHalfAngleDeg = static_cast<float>(value); break;
    case kParamHeadWidth: p.params.headWidthCm = static_cast<float>(value); break;
    case kParamLowProtect: p.params.xtcLowProtectHz = static_cast<float>(value); break;
    case kParamPreserve: p.params.stereoPreservePercent = static_cast<float>(value); break;
    case kParamOutput: p.params.outputGainDb = static_cast<float>(value); break;
    default: return;
    }
    p.params = sanitizeParams(p.params);
    markDirty(p);
}

double getParamValue(const Plugin& p, clap_id paramId)
{
    switch (paramId) {
    case kParamOrder: return p.params.order;
    case kParamLayout: return static_cast<uint32_t>(p.params.layout);
    case kParamDecodeMode: return static_cast<uint32_t>(p.params.decodeMode);
    case kParamWeighting: return static_cast<uint32_t>(p.params.weighting);
    case kParamAutogain: return static_cast<uint32_t>(p.params.autogain);
    case kParamHead: return static_cast<uint32_t>(p.params.head);
    case kParamMode: return static_cast<uint32_t>(p.params.mode);
    case kParamYaw: return p.params.yawDeg;
    case kParamPitch: return p.params.pitchDeg;
    case kParamWidth: return p.params.widthPercent;
    case kParamPinna: return p.params.pinnaPercent;
    case kParamRoom: return p.params.roomPercent;
    case kParamXtcAmount: return p.params.xtcAmountPercent;
    case kParamXtcMode: return static_cast<uint32_t>(p.params.xtcMode);
    case kParamSpeakerAngle: return p.params.speakerHalfAngleDeg;
    case kParamHeadWidth: return p.params.headWidthCm;
    case kParamLowProtect: return p.params.xtcLowProtectHz;
    case kParamPreserve: return p.params.stereoPreservePercent;
    case kParamOutput: return p.params.outputGainDb;
    default: return 0.0;
    }
}

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(p->macRealtimeActivity);
#endif
    p->sampleRate = sampleRate;
    p->decoder.prepare(sampleRate);
    p->decoder.updateParams(p->params);
    p->paramsDirty = false;
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
void reset(const clap_plugin_t* plugin) { self(plugin)->decoder.reset(); }

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    if (!process || !process->audio_inputs || !process->audio_outputs || process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }
    if (p->paramsDirty) {
        p->decoder.updateParams(p->params);
        p->paramsDirty = false;
    }
    const auto& in = process->audio_inputs[0];
    const auto& out = process->audio_outputs[0];
    const uint32_t inChannels = std::min<uint32_t>(in.channel_count, kInputChannels);
    const uint32_t outChannels = out.channel_count;
    std::array<float, kInputChannels> frame {};
    float peakL = 0.0f;
    float peakR = 0.0f;
    for (uint32_t i = 0; i < process->frames_count; ++i) {
        frame.fill(0.0f);
        for (uint32_t ch = 0; ch < inChannels; ++ch) {
            frame[ch] = in.data32[ch] ? in.data32[ch][i] : 0.0f;
        }
        float l = 0.0f;
        float r = 0.0f;
        p->decoder.processFrame(frame.data(), l, r);
        if (outChannels > 0 && out.data32[0]) out.data32[0][i] = l;
        if (outChannels > 1 && out.data32[1]) out.data32[1][i] = r;
        for (uint32_t ch = 2; ch < outChannels; ++ch) {
            if (out.data32[ch]) out.data32[ch][i] = 0.0f;
        }
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
    }
    p->peakL.store(std::max(p->peakL.load(std::memory_order_relaxed), peakL), std::memory_order_relaxed);
    p->peakR.store(std::max(p->peakR.load(std::memory_order_relaxed), peakR), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

const void* getExtension(const clap_plugin_t* plugin, const char* id);
void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 1u; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 0 : 1;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = isInput ? CLAP_PORT_AMBISONIC : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "ACN/SN3D In" : "Stereo Out");
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    struct Def { clap_id id; const char* name; double min; double max; double def; bool stepped; };
    static constexpr Def defs[] {
        { kParamOrder, "Order", 1, 7, 3, true },
        { kParamDecodeMode, "Decode mode", 0, 1, 0, true },
        { kParamLayout, "Virtual field", 0, 4, 3, true },
        { kParamWeighting, "Weighting", 0, 2, 1, true },
        { kParamAutogain, "Autogain", 0, 2, 1, true },
        { kParamHead, "Head shape", 0, 2, 0, true },
        { kParamMode, "Mode", 0, 1, 0, true },
        { kParamYaw, "Yaw", -180, 180, 0, false },
        { kParamPitch, "Pitch", -90, 90, 0, false },
        { kParamWidth, "Width", 0, 200, 100, false },
        { kParamPinna, "Pinna", 0, 100, 55, false },
        { kParamRoom, "Room", 0, 100, 0, false },
        { kParamXtcAmount, "XTC amount", 0, 140, 80, false },
        { kParamXtcMode, "XTC mode", 0, 1, 1, true },
        { kParamSpeakerAngle, "Speaker angle", 10, 60, 30, false },
        { kParamHeadWidth, "Ear width", 12, 24, 18, false },
        { kParamLowProtect, "Low protect", 20, 500, 120, false },
        { kParamPreserve, "Stereo preserve", 0, 100, 0, false },
        { kParamOutput, "Output gain", -24, 12, -6, false },
    };
    const auto& d = defs[index];
    info->id = d.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (d.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    info->cookie = nullptr;
    info->min_value = d.min;
    info->max_value = d.max;
    info->default_value = d.def;
    std::snprintf(info->name, sizeof(info->name), "%s", d.name);
    std::snprintf(info->module, sizeof(info->module), "%s", "Head Decoder");
    return true;
}

bool paramsValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParamValue(*self(plugin), paramId);
    return true;
}

bool paramsValueToText(const clap_plugin_t* plugin, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    switch (paramId) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); break;
    case kParamLayout: std::snprintf(display, size, "%s", layoutName(roundedUint(value))); break;
    case kParamDecodeMode: std::snprintf(display, size, "%s", decodeModeName(roundedUint(value))); break;
    case kParamWeighting: std::snprintf(display, size, "%s", weightingName(roundedUint(value))); break;
    case kParamAutogain: std::snprintf(display, size, "%s", autogainName(roundedUint(value))); break;
    case kParamHead: std::snprintf(display, size, "%s", headName(roundedUint(value))); break;
    case kParamMode: std::snprintf(display, size, "%s", modeName(roundedUint(value))); break;
    case kParamXtcMode: std::snprintf(display, size, "%s", xtcModeName(roundedUint(value))); break;
    case kParamYaw:
    case kParamPitch: std::snprintf(display, size, "%+.1f deg", value); break;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); break;
    case kParamHeadWidth: std::snprintf(display, size, "%.1f cm", value); break;
    case kParamLowProtect: std::snprintf(display, size, "%.0f Hz", value); break;
    default: std::snprintf(display, size, "%.0f%%", value); break;
    }
    (void)plugin;
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::strtod(display, nullptr);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    auto* p = self(plugin);
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (!h || h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* ev = reinterpret_cast<const clap_event_param_value_t*>(h);
        setParamValue(*p, ev->param_id, ev->value);
    }
}
const clap_plugin_params_t paramsExt { paramsCount, paramsInfo, paramsValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state;
    state.params = self(plugin)->params;
    return stream->write(stream, &state, sizeof(state)) == sizeof(state);
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state;
    if (stream->read(stream, &state, sizeof(state)) != sizeof(state) || state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = sanitizeParams(state.params);
    markDirty(*p);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
static NSColor* c(int rgb, CGFloat alpha = 1.0)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:alpha];
}

@interface S3GAmbisonicHeadDecoderView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItems;
    NSTimer* _timer;
    bool _binauralOpen;
    bool _transauralOpen;
    int _viewMode;
    double _viewYawDeg;
    double _viewPitchDeg;
    double _viewZoom;
    bool _dragView;
    NSPoint _lastDragPoint;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)setViewPreset:(int)mode;
@end

@implementation S3GAmbisonicHeadDecoderView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 930, 720)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSMakePoint(0, 0);
        _menuItems = 0;
        _timer = nil;
        _binauralOpen = true;
        _transauralOpen = true;
        _viewMode = 0;
        _viewYawDeg = 0.0;
        _viewPitchDeg = 0.0;
        _viewZoom = 1.0;
        _dragView = false;
        _lastDragPoint = NSMakePoint(0, 0);
        [[self window] setAcceptsMouseMovedEvents:YES];
    }
    return self;
}
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)viewDidMoveToWindow { [[self window] setAcceptsMouseMovedEvents:YES]; }
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:(1.0 / 24.0) target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer
{
    if (_timer) { [_timer invalidate]; _timer = nil; }
}
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}
- (void)setParam:(clap_id)param value:(double)value
{
    setParamValue(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 606, 724, 852, 128);
}
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, attrs, style, 606, 724, 176);
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
    static NSString* labels[] = { @"TOP", @"BACK", @"3/4" };
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
        _viewYawDeg = 0.0;
        _viewPitchDeg = 0.0;
    } else if (mode == 1) {
        _viewYawDeg = 180.0;
        _viewPitchDeg = 82.0;
    } else {
        _viewYawDeg = 135.0;
        _viewPitchDeg = 42.0;
    }
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSDictionary* text = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:mono };
    [@"s3g AMBI HEAD DECODER" drawAtPoint:NSMakePoint(18, 13) withAttributes:text];
    [[NSString stringWithFormat:@"%uOA ACN/SN3D / TRUE 2OUT", p->params.order] drawAtPoint:NSMakePoint(748, 13) withAttributes:small];

    NSRect fieldPanel = NSMakeRect(12, 34, 568, 664);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"HEAD FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    [self drawZoomButtonsInRect:fieldPanel attrs:small];
    [self drawViewButtonsInRect:fieldPanel attrs:small];
    NSRect field = NSMakeRect(28, 82, 536, 502);
    [c(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);
    const CGFloat cx = field.origin.x + field.size.width * 0.50;
    const CGFloat cy = field.origin.y + field.size.height * 0.56;
    const CGFloat r = std::min(field.size.width, field.size.height) * 0.26 * std::clamp<CGFloat>(_viewZoom, 0.65, 1.80);
    [c(0x686868, 0.22) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 28, cy) toPoint:NSMakePoint(NSMaxX(field) - 28, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, field.origin.y + 28) toPoint:NSMakePoint(cx, NSMaxY(field) - 28)];

    auto project3 = [&](double x, double y, double z, CGFloat radius) -> NSPoint {
        if (_viewMode == 0) return NSMakePoint(cx + x * radius, cy + y * radius);
        if (_viewMode == 1) return NSMakePoint(cx - x * radius * 0.94, cy - z * radius * 0.94);
        const double yaw = _viewYawDeg * s3g::kPi / 180.0;
        const double pitch = _viewPitchDeg * s3g::kPi / 180.0;
        const double x1 = x * std::cos(yaw) - y * std::sin(yaw);
        const double y1 = x * std::sin(yaw) + y * std::cos(yaw);
        const double y2 = y1 * std::cos(pitch) - z * std::sin(pitch);
        return NSMakePoint(cx + x1 * radius, cy + y2 * radius * 0.86);
    };
    auto project = [&](float azDeg, float elDeg, CGFloat radius) -> NSPoint {
        const float az = (azDeg - p->params.yawDeg) * s3g::kPi / 180.0f;
        const float el = (elDeg - p->params.pitchDeg) * s3g::kPi / 180.0f;
        const double x = -std::sin(az) * std::cos(el);
        const double y = -std::cos(az) * std::cos(el);
        const double z = std::sin(el);
        return project3(x, y, z, radius);
    };

    const auto spec = s3g::ambiHeadProfileSpec(p->params.head);
    const CGFloat shapeScale = std::clamp<CGFloat>(spec.widthCm / 18.0f, 0.86, 1.18);
    const CGFloat earScale = 0.85 + (p->params.headWidthCm - 15.0) / 12.0;
    const CGFloat pin = std::clamp<CGFloat>(p->params.pinnaPercent / 100.0f, 0.0, 1.0);
    const CGFloat headW = r * 0.44 * shapeScale;
    const CGFloat headD = r * 0.40 * (0.96 + shapeScale * 0.04);
    const CGFloat headH = r * 0.88 * (0.96 + shapeScale * 0.04);
    auto headPoint = [&](double lateral, double forward, double height) -> NSPoint {
        return project3(lateral * headW / r, forward * headD / r, height * headH / r, r);
    };
    auto pathMove = [](NSBezierPath* path, NSPoint p0) { [path moveToPoint:p0]; };
    auto drawFacet = [&](int rgb, CGFloat alpha, std::initializer_list<NSPoint> pts) {
        NSBezierPath* facet = [NSBezierPath bezierPath];
        bool first = true;
        for (NSPoint pt : pts) {
            if (first) { [facet moveToPoint:pt]; first = false; }
            else [facet lineToPoint:pt];
        }
        [facet closePath];
        [c(rgb, alpha) setStroke];
        [facet setLineWidth:0.95];
        [facet stroke];
    };
    constexpr int ovalRings = 6;
    constexpr int ovalSegments = 8;
    const double ringH[ovalRings] = { -0.82, -0.54, -0.20, 0.16, 0.50, 0.80 };
    const double ringW[ovalRings] = { 0.22, 0.52, 0.76, 0.72, 0.48, 0.18 };
    const double ringD[ovalRings] = { 0.16, 0.34, 0.48, 0.46, 0.30, 0.12 };
    auto ovalPoint = [&](int ring, int segment) -> NSPoint {
        const double a = (2.0 * s3g::kPi * static_cast<double>(segment)) / static_cast<double>(ovalSegments);
        return headPoint(std::sin(a) * ringW[ring], -std::cos(a) * ringD[ring], ringH[ring]);
    };
    for (int ri = 0; ri < ovalRings - 1; ++ri) {
        for (int si = 0; si < ovalSegments; ++si) {
            const int sj = (si + 1) % ovalSegments;
            const bool front = (si == 0 || si == 7);
            const bool sideLight = (si == 5 || si == 6);
            const int rgb = front ? 0xd8d8d8 : sideLight ? 0xaaaaaa : 0x767676;
            const CGFloat alpha = front ? 0.92 : 0.58;
            drawFacet(rgb, alpha, { ovalPoint(ri, si), ovalPoint(ri, sj), ovalPoint(ri + 1, sj), ovalPoint(ri + 1, si) });
        }
    }
    drawFacet(0xe8e8e8, 0.88, { headPoint(-0.42, -0.50, 0.18), headPoint(0.42, -0.50, 0.18), headPoint(0.26, -0.58, -0.02), headPoint(-0.26, -0.58, -0.02) });
    drawFacet(0xd6d6d6, 0.90, { headPoint(-0.10, -0.62, 0.00), headPoint(0.10, -0.62, 0.00), headPoint(0.06, -0.72, -0.22), headPoint(-0.06, -0.72, -0.22) });
    drawFacet(0xcfcfcf, 0.82, { headPoint(-0.30, -0.52, -0.36), headPoint(0.30, -0.52, -0.36), headPoint(0.18, -0.42, -0.62), headPoint(-0.18, -0.42, -0.62) });
    const CGFloat earX = r * 0.66 * earScale;
    const CGFloat earRadius = earX / r;
    auto drawEar = [&](bool leftSide) {
        const double sign = leftSide ? -1.0 : 1.0;
        const CGFloat line = 1.0 + pin * 1.0;
        NSBezierPath* socket = [NSBezierPath bezierPath];
        pathMove(socket, project3(sign * (earRadius - 0.03), -0.01, -0.34, r));
        [socket lineToPoint:project3(sign * (earRadius - 0.08), 0.01, -0.02, r)];
        [socket lineToPoint:project3(sign * (earRadius - 0.01), 0.02, 0.42, r)];
        [socket setLineWidth:7.0 + pin * 2.0];
        [c(0x000000, 0.20) setStroke]; [socket stroke];
        NSBezierPath* outer = [NSBezierPath bezierPath];
        pathMove(outer, project3(sign * earRadius, -0.02, -0.32, r));
        [outer lineToPoint:project3(sign * (earRadius + 0.16 + pin * 0.04), -0.02, -0.22, r)];
        [outer lineToPoint:project3(sign * (earRadius + 0.24 + pin * 0.06), 0.00, 0.08, r)];
        [outer lineToPoint:project3(sign * (earRadius + 0.15 + pin * 0.04), 0.02, 0.36, r)];
        [outer lineToPoint:project3(sign * earRadius, 0.01, 0.44, r)];
        [outer closePath];
        [c(0x151515, 0.98) setFill]; [outer fill];
        [c(0xd8d8d8, 0.42 + pin * 0.30) setStroke]; [outer setLineWidth:line]; [outer stroke];
        NSBezierPath* cup = [NSBezierPath bezierPath];
        pathMove(cup, project3(sign * (earRadius + 0.05), -0.01, -0.18, r));
        [cup lineToPoint:project3(sign * (earRadius + 0.16 + pin * 0.05), 0.00, -0.02, r)];
        [cup lineToPoint:project3(sign * (earRadius + 0.08), 0.02, 0.28, r)];
        [cup lineToPoint:project3(sign * (earRadius + 0.01 - pin * 0.03), 0.01, 0.10, r)];
        [cup closePath];
        [c(0xe8b486, 0.07 + pin * 0.14) setFill]; [cup fill];
        [c(0xe8b486, 0.26 + pin * 0.42) setStroke]; [cup setLineWidth:0.8 + pin * 1.8]; [cup stroke];
        NSBezierPath* fold = [NSBezierPath bezierPath];
        pathMove(fold, project3(sign * (earRadius + 0.04), 0.00, -0.02, r));
        [fold lineToPoint:project3(sign * (earRadius + 0.17 + pin * 0.05), 0.01, 0.16, r)];
        [fold lineToPoint:project3(sign * (earRadius + 0.02 - pin * 0.03), 0.01, 0.18, r)];
        [fold setLineWidth:0.8 + pin * 1.2];
        [c(0xffd4a8, 0.20 + pin * 0.38) setStroke]; [fold stroke];
    };
    drawEar(true);
    drawEar(false);
    const NSPoint leftLabel = project3(-earRadius - 0.16, 0.0, 0.02, r);
    const NSPoint rightLabel = project3(earRadius + 0.08, 0.0, 0.02, r);
    [@"L" drawAtPoint:NSMakePoint(leftLabel.x, leftLabel.y - 7) withAttributes:small];
    [@"R" drawAtPoint:NSMakePoint(rightLabel.x, rightLabel.y - 7) withAttributes:small];
    const bool directDecode = p->params.decodeMode == s3g::AmbiHeadDecodeMode::Direct;
    const uint32_t vcount = directDecode ? s3g::kAmbiHeadMaxVirtualSpeakers : s3g::ambiStereoVirtualCount(p->params.layout);
    for (uint32_t i = 0; i < vcount; ++i) {
        const auto pt = directDecode ? s3g::ambiHeadDirectPoint(i) : s3g::ambiStereoVirtualPoint(p->params.layout, i);
        const NSPoint a = project(pt.azimuthDeg, pt.elevationDeg, r * 1.55);
        [c(0x9a9a9a, directDecode ? 0.13 : 0.28) setStroke];
        [NSBezierPath strokeLineFromPoint:a toPoint:NSMakePoint(cx, cy)];
        const CGFloat sz = directDecode ? 3.2 : 4.0 + 2.0 * std::max(0.0f, std::cos((pt.azimuthDeg - p->params.yawDeg) * s3g::kPi / 180.0f));
        [c(0xd6d6d6, directDecode ? 0.38 : 0.62) setFill]; NSRectFill(NSMakeRect(a.x - sz * 0.5, a.y - sz * 0.5, sz, sz));
    }

    if (p->params.mode == s3g::AmbiHeadMode::Transaural) {
        const float angleDeg = p->params.speakerHalfAngleDeg;
        const CGFloat amount = std::clamp<CGFloat>(p->params.xtcAmountPercent / 140.0f, 0.0, 1.0);
        const CGFloat preserve = std::clamp<CGFloat>(p->params.stereoPreservePercent / 100.0f, 0.0, 1.0);
        const CGFloat sr = r * 1.90;
        const NSPoint sl = project(angleDeg, 0.0f, sr);
        const NSPoint srp = project(-angleDeg, 0.0f, sr);
        const NSPoint earL = project3(-earRadius, 0.0, 0.02, r);
        const NSPoint earR = project3(earRadius, 0.0, 0.02, r);
        [c(0xcfcfcf, 0.84) setFill];
        NSRectFill(NSMakeRect(sl.x - 10, sl.y - 10, 20, 20));
        NSRectFill(NSMakeRect(srp.x - 10, srp.y - 10, 20, 20));
        NSBezierPath* directL = [NSBezierPath bezierPath];
        [directL moveToPoint:sl]; [directL lineToPoint:earL];
        [directL setLineWidth:1.2 + 1.8 * (1.0 - preserve)];
        [c(0x9ecf9c, 0.34 + 0.34 * (1.0 - preserve)) setStroke]; [directL stroke];
        NSBezierPath* directR = [NSBezierPath bezierPath];
        [directR moveToPoint:srp]; [directR lineToPoint:earR];
        [directR setLineWidth:1.2 + 1.8 * (1.0 - preserve)];
        [c(0x9ecf9c, 0.34 + 0.34 * (1.0 - preserve)) setStroke]; [directR stroke];
        NSBezierPath* crossL = [NSBezierPath bezierPath];
        const NSPoint crossLC1 = NSMakePoint(sl.x + (earR.x - sl.x) * 0.33, sl.y + (earR.y - sl.y) * 0.18 - 34.0);
        const NSPoint crossLC2 = NSMakePoint(sl.x + (earR.x - sl.x) * 0.70, sl.y + (earR.y - sl.y) * 0.86 + 34.0);
        [crossL moveToPoint:sl]; [crossL curveToPoint:earR controlPoint1:crossLC1 controlPoint2:crossLC2];
        [crossL setLineWidth:0.8 + 4.2 * amount];
        CGFloat dash[] = { 5.0, 4.0 };
        [crossL setLineDash:dash count:2 phase:0.0];
        [c(0xe08f72, 0.18 + 0.58 * amount) setStroke]; [crossL stroke];
        NSBezierPath* crossR = [NSBezierPath bezierPath];
        const NSPoint crossRC1 = NSMakePoint(srp.x + (earL.x - srp.x) * 0.33, srp.y + (earL.y - srp.y) * 0.18 - 34.0);
        const NSPoint crossRC2 = NSMakePoint(srp.x + (earL.x - srp.x) * 0.70, srp.y + (earL.y - srp.y) * 0.86 + 34.0);
        [crossR moveToPoint:srp]; [crossR curveToPoint:earL controlPoint1:crossRC1 controlPoint2:crossRC2];
        [crossR setLineWidth:0.8 + 4.2 * amount];
        [crossR setLineDash:dash count:2 phase:0.0];
        [c(0xe08f72, 0.18 + 0.58 * amount) setStroke]; [crossR stroke];
        [c(0xe08f72, 0.06 + 0.16 * amount) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(earL.x - 18 - amount * 8, earL.y - 28 - amount * 8, 36 + amount * 16, 56 + amount * 16)] fill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(earR.x - 18 - amount * 8, earR.y - 28 - amount * 8, 36 + amount * 16, 56 + amount * 16)] fill];
    }
    [[NSString stringWithFormat:@"%@ / %@ / %@",
      [NSString stringWithUTF8String:modeName(static_cast<uint32_t>(p->params.mode))],
      [NSString stringWithUTF8String:headName(static_cast<uint32_t>(p->params.head))],
      [NSString stringWithUTF8String:directDecode ? "Direct grid" : layoutName(static_cast<uint32_t>(p->params.layout))]]
        drawAtPoint:NSMakePoint(40, 550) withAttributes:small];

    NSRect side = NSMakeRect(592, 34, 336, 664);
    NSRect decoder = NSMakeRect(side.origin.x, 34, side.size.width, 156);
    NSRect binaural = NSMakeRect(side.origin.x, 202, side.size.width, _binauralOpen ? 198 : 24);
    NSRect transaural = NSMakeRect(side.origin.x, _binauralOpen ? 412 : 238, side.size.width, _transauralOpen ? 154 : 24);
    NSRect output = NSMakeRect(side.origin.x, (_binauralOpen ? 412 : 238) + (_transauralOpen ? 166 : 36), side.size.width, 128);
    s3g::clap_gui::drawPanelFrame(decoder.origin.x, decoder.origin.y, decoder.size.width, decoder.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"DECODER", true, decoder.origin.x, decoder.origin.y, decoder.size.width, 21, text, style);
    [self drawSlider:@"ORD" value:[NSString stringWithFormat:@"%uOA", p->params.order] norm:(p->params.order - 1.0) / 6.0 y:74 attrs:small style:style];
    [self drawMenu:@"DEC" value:[NSString stringWithUTF8String:decodeModeName(static_cast<uint32_t>(p->params.decodeMode))] y:96 attrs:small style:style];
    [self drawMenu:@"FIELD" value:[NSString stringWithUTF8String:directDecode ? "Internal grid" : layoutName(static_cast<uint32_t>(p->params.layout))] y:118 attrs:small style:style];
    [self drawMenu:@"WGT" value:[NSString stringWithUTF8String:weightingName(static_cast<uint32_t>(p->params.weighting))] y:140 attrs:small style:style];
    [self drawMenu:@"AGN" value:[NSString stringWithUTF8String:autogainName(static_cast<uint32_t>(p->params.autogain))] y:162 attrs:small style:style];

    s3g::clap_gui::drawPanelFrame(binaural.origin.x, binaural.origin.y, binaural.size.width, binaural.size.height, style);
    s3g::clap_gui::drawDisclosurePanelHeader(@"BINAURAL", _binauralOpen, binaural.origin.x, binaural.origin.y, binaural.size.width, 21, text, style);
    if (_binauralOpen) {
        [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:modeName(static_cast<uint32_t>(p->params.mode))] y:binaural.origin.y + 40 attrs:small style:style];
        [self drawMenu:@"HEAD" value:[NSString stringWithUTF8String:headName(static_cast<uint32_t>(p->params.head))] y:binaural.origin.y + 62 attrs:small style:style];
        [self drawSlider:@"YAW" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.yawDeg)] norm:(p->params.yawDeg + 180.0) / 360.0 y:binaural.origin.y + 84 attrs:small style:style];
        [self drawSlider:@"PIT" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.pitchDeg)] norm:(p->params.pitchDeg + 90.0) / 180.0 y:binaural.origin.y + 106 attrs:small style:style];
        [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.widthPercent)] norm:p->params.widthPercent / 200.0 y:binaural.origin.y + 128 attrs:small style:style];
        [self drawSlider:@"PIN" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.pinnaPercent)] norm:p->params.pinnaPercent / 100.0 y:binaural.origin.y + 150 attrs:small style:style];
        [self drawSlider:@"EAR" value:[NSString stringWithFormat:@"%.1f", static_cast<double>(p->params.headWidthCm)] norm:(p->params.headWidthCm - 12.0) / 12.0 y:binaural.origin.y + 172 attrs:small style:style];
    }

    s3g::clap_gui::drawPanelFrame(transaural.origin.x, transaural.origin.y, transaural.size.width, transaural.size.height, style);
    s3g::clap_gui::drawDisclosurePanelHeader(@"TRANSAURAL", _transauralOpen, transaural.origin.x, transaural.origin.y, transaural.size.width, 21, text, style);
    if (_transauralOpen) {
        [self drawSlider:@"ROOM" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.roomPercent)] norm:p->params.roomPercent / 100.0 y:transaural.origin.y + 40 attrs:small style:style];
        [self drawSlider:@"XTC" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.xtcAmountPercent)] norm:p->params.xtcAmountPercent / 140.0 y:transaural.origin.y + 62 attrs:small style:style];
        [self drawMenu:@"XMOD" value:[NSString stringWithUTF8String:xtcModeName(static_cast<uint32_t>(p->params.xtcMode))] y:transaural.origin.y + 84 attrs:small style:style];
        [self drawSlider:@"SPK" value:[NSString stringWithFormat:@"%.0f", static_cast<double>(p->params.speakerHalfAngleDeg)] norm:(p->params.speakerHalfAngleDeg - 10.0) / 50.0 y:transaural.origin.y + 106 attrs:small style:style];
        [self drawSlider:@"LOW" value:[NSString stringWithFormat:@"%.0f", static_cast<double>(p->params.xtcLowProtectHz)] norm:(p->params.xtcLowProtectHz - 20.0) / 480.0 y:transaural.origin.y + 128 attrs:small style:style];
    }

    s3g::clap_gui::drawPanelFrame(output.origin.x, output.origin.y, output.size.width, output.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, output.origin.x, output.origin.y, output.size.width, 21, text, style);
    [self drawSlider:@"KEEP" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.stereoPreservePercent)] norm:p->params.stereoPreservePercent / 100.0 y:output.origin.y + 38 attrs:small style:style];
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(p->params.outputGainDb)] norm:(p->params.outputGainDb + 24.0) / 36.0 y:output.origin.y + 60 attrs:small style:style];

    auto meter = [&](CGFloat y, NSString* label, float peak) {
        const double db = 20.0 * std::log10(std::max(0.000001f, peak));
        const CGFloat norm = std::clamp<CGFloat>((db + 60.0) / 60.0, 0.0, 1.0);
        [label drawAtPoint:NSMakePoint(612, y - 1) withAttributes:small];
        [style.strip setFill]; NSRectFill(NSMakeRect(638, y, 190, 14));
        [style.fill setFill]; NSRectFill(NSMakeRect(639, y + 1, 188 * norm, 12));
        [style.grid setStroke]; NSFrameRect(NSMakeRect(638, y, 190, 14));
        [[NSString stringWithFormat:@"%+4.1f", db] drawAtPoint:NSMakePoint(846, y - 1) withAttributes:small];
    };
    const float pkL = p->peakL.exchange(p->peakL.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    const float pkR = p->peakR.exchange(p->peakR.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    meter(output.origin.y + 84, @"L", pkL);
    meter(output.origin.y + 106, @"R", pkR);

    if (_openMenu > 0 && _menuItems > 0) {
        NSString* decodes[] = { @"Direct", @"Virtual field" };
        NSString* layouts[] = { @"Quad virtual", @"8ch cube", @"12ch dodeca", @"24ch dome", @"32ch sphere" };
        NSString* weights[] = { @"Projection", @"Energy-normalized", @"Max-rE" };
        NSString* gains[] = { @"Off", @"Power/sqrt(N)", @"Energy sum" };
        NSString* heads[] = { @"Medium head", @"Small head", @"Large head" };
        NSString* modes[] = { @"Binaural", @"Transaural" };
        NSString* xtcs[] = { @"Feedforward", @"Matrix inverse" };
        NSString** items = decodes;
        int selected = static_cast<int>(p->params.decodeMode);
        if (_openMenu == 2) { items = layouts; selected = static_cast<int>(p->params.layout); }
        if (_openMenu == 3) { items = weights; selected = static_cast<int>(p->params.weighting); }
        if (_openMenu == 4) { items = gains; selected = static_cast<int>(p->params.autogain); }
        if (_openMenu == 5) { items = heads; selected = static_cast<int>(p->params.head); }
        if (_openMenu == 6) { items = modes; selected = static_cast<int>(p->params.mode); }
        if (_openMenu == 7) { items = xtcs; selected = static_cast<int>(p->params.xtcMode); }
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(_menuOrigin.x, _menuOrigin.y, 176, 18.0 * _menuItems), 18.0, items, _menuItems, selected, _hoverMenuItem, small, style);
    }
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp((pt.x - 724.0) / 128.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 0: [self setParam:kParamOrder value:1.0 + norm * 6.0]; break;
    case 7: [self setParam:kParamYaw value:-180.0 + norm * 360.0]; break;
    case 8: [self setParam:kParamPitch value:-90.0 + norm * 180.0]; break;
    case 9: [self setParam:kParamWidth value:norm * 200.0]; break;
    case 10: [self setParam:kParamPinna value:norm * 100.0]; break;
    case 11: [self setParam:kParamRoom value:norm * 100.0]; break;
    case 12: [self setParam:kParamXtcAmount value:norm * 140.0]; break;
    case 14: [self setParam:kParamSpeakerAngle value:10.0 + norm * 50.0]; break;
    case 15: [self setParam:kParamHeadWidth value:12.0 + norm * 12.0]; break;
    case 16: [self setParam:kParamLowProtect value:20.0 + norm * 480.0]; break;
    case 17: [self setParam:kParamPreserve value:norm * 100.0]; break;
    case 18: [self setParam:kParamOutput value:-24.0 + norm * 36.0]; break;
    default: break;
    }
}
- (void)updateMenuHover:(NSPoint)pt
{
    if (_openMenu <= 0 || _menuItems == 0) return;
    const int hover = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(_menuOrigin.x, _menuOrigin.y, 176, 18.0 * _menuItems), 18.0, _menuItems);
    if (hover != _hoverMenuItem) { _hoverMenuItem = hover; [self setNeedsDisplay:YES]; }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(_menuOrigin.x, _menuOrigin.y, 176, 18.0 * _menuItems), 18.0, _menuItems);
        if (hit >= 0) {
            if (_openMenu == 1) [self setParam:kParamDecodeMode value:hit];
            if (_openMenu == 2) [self setParam:kParamLayout value:hit];
            if (_openMenu == 3) [self setParam:kParamWeighting value:hit];
            if (_openMenu == 4) [self setParam:kParamAutogain value:hit];
            if (_openMenu == 5) [self setParam:kParamHead value:hit];
            if (_openMenu == 6) [self setParam:kParamMode value:hit];
            if (_openMenu == 7) [self setParam:kParamXtcMode value:hit];
        }
        _openMenu = 0; _hoverMenuItem = -1; [self setNeedsDisplay:YES]; return;
    }
    const NSRect side = NSMakeRect(592, 34, 336, 664);
    const NSRect fieldPanel = NSMakeRect(12, 34, 568, 664);
    const NSRect field = NSMakeRect(28, 82, 536, 502);
    for (int i = 0; i < 2; ++i) {
        if (!NSPointInRect(pt, [self zoomButtonRect:i inRect:fieldPanel])) continue;
        _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.84 : 1.19), 0.65, 1.80);
        [self setNeedsDisplay:YES];
        return;
    }
    for (int i = 0; i < 3; ++i) {
        if (!NSPointInRect(pt, [self viewButtonRect:i inRect:fieldPanel])) continue;
        [self setViewPreset:i];
        return;
    }
    if (NSPointInRect(pt, field)) {
        _dragView = true;
        _lastDragPoint = pt;
        return;
    }
    const NSRect decoder = NSMakeRect(side.origin.x, 34, side.size.width, 156);
    const NSRect binaural = NSMakeRect(side.origin.x, 202, side.size.width, _binauralOpen ? 198 : 24);
    const NSRect transaural = NSMakeRect(side.origin.x, _binauralOpen ? 412 : 238, side.size.width, _transauralOpen ? 154 : 24);
    const NSRect output = NSMakeRect(side.origin.x, (_binauralOpen ? 412 : 238) + (_transauralOpen ? 166 : 36), side.size.width, 128);
    (void)decoder;
    if (NSPointInRect(pt, NSMakeRect(binaural.origin.x, binaural.origin.y, binaural.size.width, 24))) {
        _binauralOpen = !_binauralOpen;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(transaural.origin.x, transaural.origin.y, transaural.size.width, 24))) {
        _transauralOpen = !_transauralOpen;
        [self setNeedsDisplay:YES];
        return;
    }

    struct HitRow { int index; CGFloat y; bool menu; int openMenu; uint32_t menuItems; };
    HitRow rows[19];
    int count = 0;
    rows[count++] = { 0, 74, false, 0, 0 };
    rows[count++] = { 1, 96, true, 1, 2 };
    rows[count++] = { 2, 118, true, 2, 5 };
    rows[count++] = { 3, 140, true, 3, 3 };
    rows[count++] = { 4, 162, true, 4, 3 };
    if (_binauralOpen) {
        rows[count++] = { 6, binaural.origin.y + 40, true, 6, 2 };
        rows[count++] = { 5, binaural.origin.y + 62, true, 5, 3 };
        rows[count++] = { 7, binaural.origin.y + 84, false, 0, 0 };
        rows[count++] = { 8, binaural.origin.y + 106, false, 0, 0 };
        rows[count++] = { 9, binaural.origin.y + 128, false, 0, 0 };
        rows[count++] = { 10, binaural.origin.y + 150, false, 0, 0 };
        rows[count++] = { 15, binaural.origin.y + 172, false, 0, 0 };
    }
    if (_transauralOpen) {
        rows[count++] = { 11, transaural.origin.y + 40, false, 0, 0 };
        rows[count++] = { 12, transaural.origin.y + 62, false, 0, 0 };
        rows[count++] = { 13, transaural.origin.y + 84, true, 7, 2 };
        rows[count++] = { 14, transaural.origin.y + 106, false, 0, 0 };
        rows[count++] = { 16, transaural.origin.y + 128, false, 0, 0 };
    }
    rows[count++] = { 17, output.origin.y + 38, false, 0, 0 };
    rows[count++] = { 18, output.origin.y + 60, false, 0, 0 };
    for (int i = 0; i < count; ++i) {
        if (!NSPointInRect(pt, NSMakeRect(596, rows[i].y - 6, 316, 22))) continue;
        if (rows[i].menu) {
            _openMenu = rows[i].openMenu;
            _menuItems = rows[i].menuItems;
            _menuOrigin = NSMakePoint(724, rows[i].y + 17);
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
        _dragSlider = rows[i].index;
        [self updateSliderAtPoint:pt];
        return;
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:pt];
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        if (_viewMode == 0) {
            _viewYawDeg = 0.0;
            _viewPitchDeg = 0.0;
        } else if (_viewMode == 1) {
            _viewYawDeg = 180.0;
            _viewPitchDeg = 82.0;
        }
        _viewYawDeg += dx * 0.36;
        _viewPitchDeg = std::clamp(_viewPitchDeg - dy * 0.26, -82.0, 82.0);
        _viewMode = 2;
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragSlider >= 0) [self updateSliderAtPoint:pt];
}
- (void)mouseMoved:(NSEvent*)event { [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragView = false; }
- (void)scrollWheel:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect field = NSMakeRect(28, 82, 536, 502);
    if (!NSPointInRect(pt, field)) { [super scrollWheel:event]; return; }
    const double delta = [event scrollingDeltaY];
    _viewZoom = std::clamp(_viewZoom * (1.0 + delta * 0.018), 0.65, 1.80);
    [self setNeedsDisplay:YES];
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbisonicHeadDecoderView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GAmbisonicHeadDecoderView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 930; *h = 720; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = false; hints->can_resize_vertically = false; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 930; *h = 720; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, 930, 720)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbisonicHeadDecoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicHeadDecoderView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

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

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambisonic-head-decoder",
    "s3g Ambi Head Decoder",
    "s3g",
    "https://s3g.github.io/s3g-dsp/",
    "",
    "",
    "0.1.0",
    "Synthetic ambisonic binaural decoder with optional transaural crosstalk cancellation",
    features,
};

const clap_plugin_t pluginClass {
    &descriptor,
    nullptr,
    init,
    destroy,
    activate,
    deactivate,
    startProcessing,
    stopProcessing,
    reset,
    process,
    getExtension,
    onMainThread,
};

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->plugin = pluginClass;
    p->plugin.plugin_data = p;
    p->host = host;
    p->params = sanitizeParams(p->params);
    p->decoder.prepare(p->sampleRate);
    p->decoder.updateParams(p->params);
    return &p->plugin;
}

const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

} // namespace

extern "C" {
CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    [](const char*) -> bool { return true; },
    []() {},
    [](const char* factoryId) -> const void* {
        if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) return &factory;
        return nullptr;
    },
};
}
