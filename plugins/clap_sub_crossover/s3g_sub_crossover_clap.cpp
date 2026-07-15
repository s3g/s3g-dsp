#include "s3g_realtime.h"
#include "s3g_sub_crossover.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <new>

namespace {

constexpr uint32_t kChannelCount = s3g::kSubCrossoverMaxChannels;
constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kLayoutCount = 27;

constexpr std::array<uint32_t, kLayoutCount> kLayoutMenuOrder {
    1u, 2u, 0u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u,
    26u, 13u, 16u, 18u, 14u, 15u, 17u, 19u, 24u, 20u, 21u, 22u, 23u, 25u
};

constexpr uint32_t layoutPresetForMenuIndex(uint32_t index)
{
    return kLayoutMenuOrder[index < kLayoutMenuOrder.size() ? index : 0u];
}

constexpr uint32_t menuIndexForLayoutPreset(uint32_t preset)
{
    for (uint32_t i = 0; i < kLayoutMenuOrder.size(); ++i) {
        if (kLayoutMenuOrder[i] == preset) return i;
    }
    return 0u;
}

enum ParamId : clap_id {
    kParamLayout = 1,
    kParamMode = 2,
    kParamHighChannels = 3,
    kParamSubCount = 4,
    kParamSubOffset = 5,
    kParamCutoff = 6,
    kParamSubFocus = 7,
    kParamSubGain = 8,
    kParamHighGain = 9,
    kParamBypass = 10,
    kParamFoldBypass = 11,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::SubCrossoverParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::SubCrossoverParams params {};
    s3g::SubCrossover xover {};
    std::array<float, kChannelCount> frameIn {};
    std::array<float, kChannelCount> frameOut {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

uint32_t defaultSubOffsetForHighChannels(uint32_t highChannels)
{
    return std::min<uint32_t>(kChannelCount, std::clamp<uint32_t>(highChannels, 1u, kChannelCount) + 1u);
}

s3g::SubCrossoverParams sanitize(s3g::SubCrossoverParams p)
{
    p.layout = static_cast<s3g::LayoutPannerPreset>(
        std::min<uint32_t>(roundedUint(static_cast<double>(static_cast<uint32_t>(p.layout))), kLayoutCount - 1u));
    p.mode = static_cast<s3g::SubCrossoverMode>(
        std::min<uint32_t>(roundedUint(static_cast<double>(static_cast<uint32_t>(p.mode))), 1u));
    p.highChannels = std::clamp<uint32_t>(p.highChannels, 1u, kChannelCount);
    p.subCount = std::clamp<uint32_t>(p.subCount, 1u, s3g::kSubCrossoverMaxSubs);
    p.subOffset = std::clamp<uint32_t>(p.subOffset, 1u, kChannelCount);
    p.cutoffHz = s3g::clamp(p.cutoffHz, 20.0f, 240.0f);
    p.subFocus = s3g::clamp(p.subFocus, 0.25f, 8.0f);
    p.subGainDb = s3g::clamp(p.subGainDb, -60.0f, 18.0f);
    p.highGainDb = s3g::clamp(p.highGainDb, -60.0f, 18.0f);
    return p;
}

void apply(Plugin& p)
{
    p.params = sanitize(p.params);
    p.xover.setParams(p.params);
}

void setParamValue(Plugin& p, clap_id paramId, double value)
{
    switch (paramId) {
    case kParamLayout: {
        p.params.layout = static_cast<s3g::LayoutPannerPreset>(std::min<uint32_t>(roundedUint(value), kLayoutCount - 1u));
        p.params.highChannels = std::clamp<uint32_t>(
            s3g::layoutPannerPresetSpeakerCount(p.params.layout, p.params.highChannels),
            1u,
            kChannelCount);
        p.params.subOffset = defaultSubOffsetForHighChannels(p.params.highChannels);
        break;
    }
    case kParamMode: p.params.mode = static_cast<s3g::SubCrossoverMode>(std::min<uint32_t>(roundedUint(value), 1u)); break;
    case kParamHighChannels:
        p.params.highChannels = std::clamp<uint32_t>(roundedUint(value), 1u, kChannelCount);
        p.params.subOffset = defaultSubOffsetForHighChannels(p.params.highChannels);
        break;
    case kParamSubCount: p.params.subCount = std::clamp<uint32_t>(roundedUint(value), 1u, s3g::kSubCrossoverMaxSubs); break;
    case kParamSubOffset: p.params.subOffset = std::clamp<uint32_t>(roundedUint(value), 1u, kChannelCount); break;
    case kParamCutoff: p.params.cutoffHz = s3g::clamp(static_cast<float>(value), 20.0f, 240.0f); break;
    case kParamSubFocus: p.params.subFocus = s3g::clamp(static_cast<float>(value), 0.25f, 8.0f); break;
    case kParamSubGain: p.params.subGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f); break;
    case kParamHighGain: p.params.highGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f); break;
    case kParamBypass: p.params.bypass = value >= 0.5; break;
    case kParamFoldBypass: p.params.foldSubsOnBypass = value >= 0.5; break;
    default: return;
    }
    apply(p);
}

double getParamValue(const Plugin& p, clap_id paramId)
{
    switch (paramId) {
    case kParamLayout: return static_cast<double>(static_cast<uint32_t>(p.params.layout));
    case kParamMode: return static_cast<double>(static_cast<uint32_t>(p.params.mode));
    case kParamHighChannels: return static_cast<double>(p.params.highChannels);
    case kParamSubCount: return static_cast<double>(p.params.subCount);
    case kParamSubOffset: return static_cast<double>(p.params.subOffset);
    case kParamCutoff: return p.params.cutoffHz;
    case kParamSubFocus: return p.params.subFocus;
    case kParamSubGain: return p.params.subGainDb;
    case kParamHighGain: return p.params.highGainDb;
    case kParamBypass: return p.params.bypass ? 1.0 : 0.0;
    case kParamFoldBypass: return p.params.foldSubsOnBypass ? 1.0 : 0.0;
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
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->xover.prepare(sampleRate);
    apply(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->xover.prepare(p->sampleRate);
    apply(*p);
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParamValue(p, param->param_id, param->value);
        }
    }
}

template <typename Sample>
void processTyped(Plugin& p, Sample** in, Sample** out, uint32_t channels, uint32_t frames)
{
    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
            p.frameIn[ch] = (ch < channels && in && in[ch]) ? static_cast<float>(in[ch][frame]) : 0.0f;
        }
        p.xover.processFrame(p.frameIn.data(), p.frameOut.data(), channels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            if (out && out[ch]) out[ch][frame] = static_cast<Sample>(p.frameOut[ch]);
        }
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, kChannelCount });
    if (output.data32) processTyped(*p, input.data32, output.data32, channels, process->frames_count);
    else if (output.data64) processTyped(*p, input.data64, output.data64, channels, process->frames_count);
    s3g::clearAudioBufferFromChannel(output, channels, process->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}
uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "64ch In" : "64ch Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 11; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    struct Def { clap_id id; const char* name; double min; double max; double def; bool stepped; };
    static constexpr Def defs[] {
        { kParamLayout, "Layout", 0.0, static_cast<double>(kLayoutCount - 1u), 9.0, true },
        { kParamMode, "Mode", 0.0, 1.0, 0.0, true },
        { kParamHighChannels, "High Channels", 1.0, 64.0, 4.0, true },
        { kParamSubCount, "Sub Count", 1.0, 8.0, 1.0, true },
        { kParamSubOffset, "Sub Offset", 1.0, 64.0, 5.0, true },
        { kParamCutoff, "Cutoff", 20.0, 240.0, 90.0, false },
        { kParamSubFocus, "Sub Focus", 0.25, 8.0, 1.5, false },
        { kParamSubGain, "Sub Gain", -60.0, 18.0, 0.0, false },
        { kParamHighGain, "High Gain", -60.0, 18.0, 0.0, false },
        { kParamBypass, "Bypass", 0.0, 1.0, 0.0, true },
        { kParamFoldBypass, "Fold Subs On Bypass", 0.0, 1.0, 1.0, true },
    };
    const auto& d = defs[index];
    info->id = d.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (d.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, d.name, sizeof(info->name));
    std::strncpy(info->module, "Main", sizeof(info->module));
    info->min_value = d.min;
    info->max_value = d.max;
    info->default_value = d.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParamValue(*self(plugin), paramId);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (paramId == kParamLayout) {
        std::snprintf(display, size, "%s", s3g::layoutPannerPresetName(static_cast<s3g::LayoutPannerPreset>(std::min<uint32_t>(roundedUint(value), kLayoutCount - 1u))));
    } else if (paramId == kParamMode) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "SEND" : "SPLIT");
    } else if (paramId == kParamBypass || paramId == kParamFoldBypass) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else if (paramId == kParamCutoff) {
        std::snprintf(display, size, "%.1f Hz", value);
    } else if (paramId == kParamSubGain || paramId == kParamHighGain) {
        std::snprintf(display, size, "%.2f dB", value);
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t params { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state { kStateVersion, self(plugin)->params };
    return stream->write(stream, &state, sizeof(state)) == static_cast<int64_t>(sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (stream->read(stream, &state, sizeof(state)) != static_cast<int64_t>(sizeof(state))) return false;
    if (state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = sanitize(state.params);
    apply(*p);
    return true;
}

const clap_plugin_state_t state { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GSubCrossoverView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    int _dragControl;
    int _openMenu;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GSubCrossoverView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 760, 420)];
    if (self) {
        _plugin = plugin;
        _dragControl = -1;
        _openMenu = -1;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer
{
    if (!_timer) _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(timerFired:) userInfo:nil repeats:YES];
}
- (void)stopRefreshTimer
{
    [_timer invalidate];
    _timer = nil;
}
- (void)timerFired:(NSTimer*)timer
{
    (void)timer;
    if (_plugin && _plugin->guiVisible) [self setNeedsDisplay:YES];
}

- (void)drawPointAt:(NSPoint)p size:(CGFloat)s fill:(NSColor*)fill stroke:(NSColor*)stroke label:(NSString*)label attrs:(NSDictionary*)attrs
{
    NSRect r = NSMakeRect(p.x - s * 0.5, p.y - s * 0.5, s, s);
    [fill setFill];
    NSRectFill(r);
    [stroke setStroke];
    NSFrameRect(r);
    if (label) [label drawAtPoint:NSMakePoint(p.x + s * 0.55, p.y - 7.0) withAttributes:attrs];
}

- (NSPoint)fieldPointForSpeaker:(float)az elevation:(float)el distance:(float)distance radius:(CGFloat)radius center:(NSPoint)c
{
    const auto dir = s3g::directionFromAed(az, el);
    const CGFloat x = static_cast<CGFloat>(-dir.y * distance) * radius;
    const CGFloat y = static_cast<CGFloat>(-dir.x * distance) * radius;
    return NSMakePoint(c.x + x, c.y + y);
}

- (BOOL)point:(NSPoint)a overlapsPoint:(NSPoint)b
{
    return std::abs(a.x - b.x) <= 1.5 && std::abs(a.y - b.y) <= 1.5;
}

- (void)drawGroupedSpeakerLabels:(const std::array<NSPoint, kChannelCount>&)points count:(uint32_t)count attrs:(NSDictionary*)attrs
{
    std::array<bool, kChannelCount> used {};
    for (uint32_t i = 0; i < count; ++i) {
        if (used[i]) continue;
        used[i] = true;
        NSMutableString* label = [NSMutableString stringWithFormat:@"%u", i + 1u];
        for (uint32_t j = i + 1; j < count; ++j) {
            if (used[j]) continue;
            if (![self point:points[i] overlapsPoint:points[j]]) continue;
            used[j] = true;
            [label appendFormat:@"/%u", j + 1u];
        }
        [label drawAtPoint:NSMakePoint(points[i].x + 7.0, points[i].y - 7.0) withAttributes:attrs];
    }
}

- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [s3g::clap_gui::color(0x181818) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    const NSPoint c = NSMakePoint(NSMidX(rect), NSMidY(rect) + 8.0);
    const CGFloat r = std::min(rect.size.width, rect.size.height) * 0.34;
    [@"FRONT" drawAtPoint:NSMakePoint(c.x - 18.0, rect.origin.y + 16.0) withAttributes:attrs];

    s3g::LayoutPanner probe;
    probe.prepare(std::max(1.0, _plugin ? _plugin->sampleRate : 48000.0));
    s3g::LayoutPannerParams lp {};
    lp.layout = _plugin->params.layout;
    lp.activeSpeakers = _plugin->params.highChannels;
    lp.customShape = s3g::LayoutPannerCustomShape::Ring;
    probe.setParams(lp);
    if (probe.activeSpeakers() != _plugin->params.highChannels) {
        lp.layout = s3g::LayoutPannerPreset::Custom;
        probe.setParams(lp);
    }

    std::array<NSPoint, kChannelCount> speakerPoints {};
    std::array<s3g::LayoutPannerSpeaker, kChannelCount> speakerLayout {};
    const uint32_t highCount = std::min<uint32_t>(_plugin->params.highChannels, kChannelCount);
    for (uint32_t i = 0; i < highCount; ++i) {
        const auto sp = probe.speaker(i);
        speakerLayout[i] = sp;
        speakerPoints[i] = [self fieldPointForSpeaker:sp.azimuthDeg elevation:sp.elevationDeg distance:sp.distance radius:r center:c];
    }

    [s3g::clap_gui::color(0x565656, 0.62) setStroke];
    NSBezierPath* links = [NSBezierPath bezierPath];
    std::array<std::array<bool, kChannelCount>, kChannelCount> edgeSeen {};
    auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= highCount || b >= highCount || a == b) return;
        const uint32_t lo = std::min(a, b);
        const uint32_t hi = std::max(a, b);
        if (edgeSeen[lo][hi]) return;
        edgeSeen[lo][hi] = true;
        [links moveToPoint:speakerPoints[a]];
        [links lineToPoint:speakerPoints[b]];
    };
    auto drawPolyhedronShell = [&](bool dodecaShell) {
        constexpr float phi = 1.61803398875f;
        constexpr float invPhi = 1.0f / phi;
        std::array<s3g::Vec3, 20> verts {};
        std::array<NSPoint, 20> pts {};
        const uint32_t count = dodecaShell ? 20u : 12u;
        if (dodecaShell) {
            const float raw[20][3] {
                { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 },
                { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 },
                { 0, invPhi, phi }, { 0, invPhi, -phi }, { 0, -invPhi, phi }, { 0, -invPhi, -phi },
                { invPhi, phi, 0 }, { invPhi, -phi, 0 }, { -invPhi, phi, 0 }, { -invPhi, -phi, 0 },
                { phi, 0, invPhi }, { phi, 0, -invPhi }, { -phi, 0, invPhi }, { -phi, 0, -invPhi },
            };
            for (uint32_t i = 0; i < count; ++i) verts[i] = { raw[i][0], raw[i][1], raw[i][2] };
        } else {
            const float raw[12][3] {
                { 0, 1, phi }, { 0, -1, phi }, { 0, 1, -phi }, { 0, -1, -phi },
                { 1, phi, 0 }, { -1, phi, 0 }, { 1, -phi, 0 }, { -1, -phi, 0 },
                { phi, 0, 1 }, { -phi, 0, 1 }, { phi, 0, -1 }, { -phi, 0, -1 },
            };
            for (uint32_t i = 0; i < count; ++i) verts[i] = { raw[i][0], raw[i][1], raw[i][2] };
        }
        for (uint32_t i = 0; i < count; ++i) {
            const float d = std::sqrt(verts[i].x * verts[i].x + verts[i].y * verts[i].y + verts[i].z * verts[i].z);
            if (d > 0.000001f) {
                verts[i].x /= d;
                verts[i].y /= d;
                verts[i].z /= d;
            }
            pts[i] = NSMakePoint(c.x - static_cast<CGFloat>(verts[i].y) * r, c.y - static_cast<CGFloat>(verts[i].x) * r);
        }
        float minD2 = 999999.0f;
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1u; b < count; ++b) {
                const float dx = verts[a].x - verts[b].x;
                const float dy = verts[a].y - verts[b].y;
                const float dz = verts[a].z - verts[b].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.0001f) minD2 = std::min(minD2, d2);
            }
        }
        const float maxD2 = minD2 * 1.08f;
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1u; b < count; ++b) {
                const float dx = verts[a].x - verts[b].x;
                const float dy = verts[a].y - verts[b].y;
                const float dz = verts[a].z - verts[b].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 <= maxD2) {
                    [links moveToPoint:pts[a]];
                    [links lineToPoint:pts[b]];
                }
            }
        }
    };
    auto drawElevationPerimeters = [&]() {
        struct Band {
            float el = 0.0f;
            std::array<uint32_t, kChannelCount> ids {};
            uint32_t count = 0;
        };
        std::array<Band, 8> bands {};
        uint32_t bandCount = 0;
        for (uint32_t i = 0; i < highCount; ++i) {
            uint32_t band = bandCount;
            for (uint32_t b = 0; b < bandCount; ++b) {
                if (std::fabs(bands[b].el - speakerLayout[i].elevationDeg) < 8.0f) {
                    band = b;
                    break;
                }
            }
            if (band == bandCount && bandCount < bands.size()) bands[bandCount++].el = speakerLayout[i].elevationDeg;
            if (band < bandCount) bands[band].ids[bands[band].count++] = i;
        }
        std::sort(bands.begin(), bands.begin() + bandCount, [](const Band& a, const Band& b) { return a.el < b.el; });
        for (uint32_t b = 0; b < bandCount; ++b) {
            std::sort(bands[b].ids.begin(), bands[b].ids.begin() + bands[b].count, [&](uint32_t a, uint32_t bIndex) {
                float aAz = s3g::layoutPannerWrapDeg(speakerLayout[a].azimuthDeg);
                float bAz = s3g::layoutPannerWrapDeg(speakerLayout[bIndex].azimuthDeg);
                if (aAz < 0.0f) aAz += 360.0f;
                if (bAz < 0.0f) bAz += 360.0f;
                return aAz < bAz;
            });
            if (bands[b].count < 2u) continue;
            for (uint32_t i = 0; i < bands[b].count; ++i) {
                edge(bands[b].ids[i], bands[b].ids[(i + 1u) % bands[b].count]);
            }
        }
    };
    if (_plugin->params.layout == s3g::LayoutPannerPreset::Dodeca12) drawPolyhedronShell(true);
    else if (_plugin->params.layout == s3g::LayoutPannerPreset::Icosahedron20) drawPolyhedronShell(false);
    else drawElevationPerimeters();
#if 0
    auto ringPath = [&](uint32_t base, uint32_t count) {
        if (count < 2u || base >= highCount) return;
        const uint32_t end = std::min<uint32_t>(highCount, base + count);
        for (uint32_t i = base; i < end; ++i) edge(i, i + 1u < end ? i + 1u : base);
    };
    auto closedPath = [&](std::initializer_list<uint32_t> ids) {
        if (ids.size() < 2u) return;
        const uint32_t* first = ids.begin();
        const uint32_t* prev = first;
        for (const uint32_t* it = first + 1; it != ids.end(); ++it) {
            edge(*prev, *it);
            prev = it;
        }
        edge(*prev, *first);
    };
    auto drawSurroundBed = [&](uint32_t bed) {
        switch (bed) {
        case 5: closedPath({ 0, 1, 2, 3 }); edge(4, 0); edge(4, 3); break;
        case 6: closedPath({ 4, 0, 1, 2, 3 }); edge(5, 0); edge(5, 4); break;
        case 7: closedPath({ 5, 0, 1, 2, 3, 4 }); edge(6, 0); edge(6, 5); break;
        case 9: closedPath({ 7, 0, 1, 2, 3, 4, 5, 6 }); edge(8, 0); edge(8, 7); break;
        case 11: closedPath({ 9, 0, 1, 2, 3, 4, 5, 6, 7, 8 }); edge(10, 0); edge(10, 9); break;
        default: ringPath(0, bed); break;
        }
    };
    auto drawSurroundHeight = [&](uint32_t bed, std::initializer_list<std::initializer_list<uint32_t>> patches) {
        const uint32_t height = static_cast<uint32_t>(patches.size());
        if (height == 0u) return;
        ringPath(bed, height);
        uint32_t i = 0;
        for (auto patch : patches) {
            for (uint32_t anchor : patch) edge(bed + i, anchor);
            ++i;
        }
    };
    auto drawTriangulatedSurface = [&]() {
        std::array<NSPoint, kChannelCount> stablePoints {};
        for (uint32_t i = 0; i < highCount; ++i) {
            const auto dir = s3g::directionFromAed(speakerLayout[i].azimuthDeg, speakerLayout[i].elevationDeg);
            stablePoints[i] = NSMakePoint(dir.x * speakerLayout[i].distance, dir.y * speakerLayout[i].distance);
        }
        struct Band {
            float el = 0.0f;
            std::array<uint32_t, kChannelCount> ids {};
            uint32_t count = 0;
        };
        std::array<Band, 8> bands {};
        uint32_t bandCount = 0;
        for (uint32_t i = 0; i < highCount; ++i) {
            uint32_t band = bandCount;
            for (uint32_t b = 0; b < bandCount; ++b) {
                if (std::fabs(bands[b].el - speakerLayout[i].elevationDeg) < 8.0f) {
                    band = b;
                    break;
                }
            }
            if (band == bandCount && bandCount < bands.size()) bands[bandCount++].el = speakerLayout[i].elevationDeg;
            if (band < bandCount) bands[band].ids[bands[band].count++] = i;
        }
        std::sort(bands.begin(), bands.begin() + bandCount, [](const Band& a, const Band& b) { return a.el < b.el; });
        for (uint32_t b = 0; b < bandCount; ++b) {
            std::sort(bands[b].ids.begin(), bands[b].ids.begin() + bands[b].count, [&](uint32_t a, uint32_t bIndex) {
                float aAz = s3g::layoutPannerWrapDeg(speakerLayout[a].azimuthDeg);
                float bAz = s3g::layoutPannerWrapDeg(speakerLayout[bIndex].azimuthDeg);
                if (aAz < 0.0f) aAz += 360.0f;
                if (bAz < 0.0f) bAz += 360.0f;
                return aAz < bAz;
            });
        }
        struct AcceptedEdge { uint32_t a; uint32_t b; };
        std::array<AcceptedEdge, kChannelCount * 4> accepted {};
        uint32_t acceptedCount = 0;
        auto orient = [](NSPoint a, NSPoint b, NSPoint c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        auto intersects = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            const CGFloat o1 = orient(stablePoints[a], stablePoints[b], stablePoints[c]);
            const CGFloat o2 = orient(stablePoints[a], stablePoints[b], stablePoints[d]);
            const CGFloat o3 = orient(stablePoints[c], stablePoints[d], stablePoints[a]);
            const CGFloat o4 = orient(stablePoints[c], stablePoints[d], stablePoints[b]);
            return o1 * o2 < -0.000001 && o3 * o4 < -0.000001;
        };
        auto clearEdge = [&](uint32_t a, uint32_t b) {
            if (a >= highCount || b >= highCount || a == b) return;
            if (std::fabs(stablePoints[a].x - stablePoints[b].x) < 0.000001 && std::fabs(stablePoints[a].y - stablePoints[b].y) < 0.000001) return;
            for (uint32_t i = 0; i < acceptedCount; ++i) {
                const auto e = accepted[i];
                if (e.a == a || e.a == b || e.b == a || e.b == b) continue;
                if (intersects(a, b, e.a, e.b)) return;
            }
            edge(a, b);
            if (acceptedCount < accepted.size()) accepted[acceptedCount++] = { a, b };
        };
        auto bracket = [&](uint32_t upper, const Band& lower, std::array<uint32_t, 3>& out) {
            uint32_t outCount = 0;
            if (lower.count == 0u) return outCount;
            if (lower.count == 1u) {
                out[outCount++] = lower.ids[0];
                return outCount;
            }
            float upperAz = s3g::layoutPannerWrapDeg(speakerLayout[upper].azimuthDeg);
            if (upperAz < 0.0f) upperAz += 360.0f;
            for (uint32_t i = 0; i < lower.count; ++i) {
                float az = s3g::layoutPannerWrapDeg(speakerLayout[lower.ids[i]].azimuthDeg);
                if (az < 0.0f) az += 360.0f;
                if (std::fabs(az - upperAz) < 0.01f) {
                    out[outCount++] = lower.ids[(i + lower.count - 1u) % lower.count];
                    out[outCount++] = lower.ids[i];
                    out[outCount++] = lower.ids[(i + 1u) % lower.count];
                    return outCount;
                }
            }
            for (uint32_t i = 0; i < lower.count; ++i) {
                const uint32_t a = lower.ids[i];
                const uint32_t b = lower.ids[(i + 1u) % lower.count];
                float aAz = s3g::layoutPannerWrapDeg(speakerLayout[a].azimuthDeg);
                float bAz = s3g::layoutPannerWrapDeg(speakerLayout[b].azimuthDeg);
                if (aAz < 0.0f) aAz += 360.0f;
                if (bAz < 0.0f) bAz += 360.0f;
                if (bAz < aAz) bAz += 360.0f;
                const float testAz = upperAz < aAz ? upperAz + 360.0f : upperAz;
                if (testAz > aAz && testAz < bAz) {
                    out[outCount++] = a;
                    out[outCount++] = b;
                    return outCount;
                }
            }
            out[outCount++] = lower.ids[0];
            out[outCount++] = lower.ids[1];
            return outCount;
        };
        auto splitBand = [&](const Band& band, std::array<uint32_t, kChannelCount>& perimeter, uint32_t& perimeterCount, std::array<uint32_t, kChannelCount>& centers, uint32_t& centerCount) {
            perimeterCount = 0;
            centerCount = 0;
            for (uint32_t i = 0; i < band.count; ++i) {
                float az = s3g::layoutPannerWrapDeg(speakerLayout[band.ids[i]].azimuthDeg);
                if (band.count > 4u && std::fabs(az) < 1.0f) centers[centerCount++] = band.ids[i];
                else perimeter[perimeterCount++] = band.ids[i];
            }
        };
        for (uint32_t b = 0; b < bandCount; ++b) {
            std::array<uint32_t, kChannelCount> perimeter {};
            std::array<uint32_t, kChannelCount> centers {};
            uint32_t perimeterCount = 0;
            uint32_t centerCount = 0;
            splitBand(bands[b], perimeter, perimeterCount, centers, centerCount);
            if (perimeterCount > 1u) {
                for (uint32_t i = 0; i < perimeterCount; ++i) clearEdge(perimeter[i], perimeter[(i + 1u) % perimeterCount]);
            }
            Band perimeterBand {};
            perimeterBand.count = perimeterCount;
            for (uint32_t i = 0; i < perimeterCount; ++i) perimeterBand.ids[i] = perimeter[i];
            for (uint32_t c = 0; c < centerCount; ++c) {
                std::array<uint32_t, 3> lower {};
                const uint32_t count = bracket(centers[c], perimeterBand, lower);
                for (uint32_t i = 0; i < count; ++i) clearEdge(centers[c], lower[i]);
            }
        }
        for (uint32_t b = 0; b + 1u < bandCount; ++b) {
            std::array<uint32_t, kChannelCount> lowerPerimeter {};
            std::array<uint32_t, kChannelCount> lowerCenters {};
            uint32_t lowerCount = 0;
            uint32_t lowerCenterCount = 0;
            splitBand(bands[b], lowerPerimeter, lowerCount, lowerCenters, lowerCenterCount);
            Band lowerBand {};
            lowerBand.count = lowerCount;
            for (uint32_t i = 0; i < lowerCount; ++i) lowerBand.ids[i] = lowerPerimeter[i];
            std::array<uint32_t, kChannelCount> upperPerimeter {};
            std::array<uint32_t, kChannelCount> upperCenters {};
            uint32_t upperCount = 0;
            uint32_t upperCenterCount = 0;
            splitBand(bands[b + 1u], upperPerimeter, upperCount, upperCenters, upperCenterCount);
            for (uint32_t u = 0; u < upperCount + upperCenterCount; ++u) {
                const uint32_t upperId = u < upperCount ? upperPerimeter[u] : upperCenters[u - upperCount];
                std::array<uint32_t, 3> lower {};
                const uint32_t count = bracket(upperId, lowerBand, lower);
                for (uint32_t i = 0; i < count; ++i) clearEdge(upperId, lower[i]);
            }
        }
    };
    switch (_plugin->params.layout) {
    case s3g::LayoutPannerPreset::FiveZero:
    case s3g::LayoutPannerPreset::SixZero:
    case s3g::LayoutPannerPreset::SevenZero:
    case s3g::LayoutPannerPreset::NineZero:
    case s3g::LayoutPannerPreset::FiveZeroTwo:
    case s3g::LayoutPannerPreset::SevenZeroTwo:
    case s3g::LayoutPannerPreset::NineZeroTwo:
    case s3g::LayoutPannerPreset::FiveZeroFour:
    case s3g::LayoutPannerPreset::SevenZeroFour:
    case s3g::LayoutPannerPreset::NineZeroFour:
    case s3g::LayoutPannerPreset::SevenZeroSix:
    case s3g::LayoutPannerPreset::NineZeroSix:
    case s3g::LayoutPannerPreset::ElevenZeroEight:
        drawTriangulatedSurface(); break;
    case s3g::LayoutPannerPreset::Quad: ringPath(0, 4); break;
    case s3g::LayoutPannerPreset::QuadOverhead6:
        ringPath(0, 4); edge(4, 0); edge(4, 3); edge(5, 1); edge(5, 2); edge(4, 5); break;
    case s3g::LayoutPannerPreset::OctophonicRing: ringPath(0, 8); break;
    case s3g::LayoutPannerPreset::Ring12: ringPath(0, 12); break;
    case s3g::LayoutPannerPreset::Ring16: ringPath(0, 16); break;
    default: drawTriangulatedSurface(); break;
    }
#endif
    [links setLineWidth:1.0];
    [links stroke];

    for (uint32_t i = 0; i < highCount; ++i) {
        [self drawPointAt:speakerPoints[i] size:8.0 fill:s3g::clap_gui::color(0x7a7a7a) stroke:s3g::clap_gui::color(0xd2d2d2) label:nil attrs:attrs];
    }
    [self drawGroupedSpeakerLabels:speakerPoints count:highCount attrs:attrs];

    const uint32_t subCount = std::max<uint32_t>(1u, _plugin->params.subCount);
    for (uint32_t i = 0; i < subCount; ++i) {
        const float az = s3g::layoutPannerWrapDeg(-45.0f - static_cast<float>(i) * 360.0f / static_cast<float>(subCount));
        const NSPoint p = [self fieldPointForSpeaker:az elevation:0.0f distance:1.2f radius:r center:c];
        [self drawPointAt:p size:13.0 fill:s3g::clap_gui::color(0xbdbdbd) stroke:s3g::clap_gui::color(0xffffff) label:[NSString stringWithFormat:@"S%u", _plugin->params.subOffset + i] attrs:attrs];
    }

    NSString* summary = [NSString stringWithFormat:@"%s  high:%u  subs:%u  offset:%u",
                         s3g::layoutPannerPresetName(_plugin->params.layout),
                         _plugin->params.highChannels,
                         _plugin->params.subCount,
                         _plugin->params.subOffset];
    [summary drawAtPoint:NSMakePoint(rect.origin.x + 12.0, NSMaxY(rect) - 26.0) withAttributes:attrs];
}

- (void)drawControl:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, valueAttrs, style, 462.0, 548.0, 690.0, 124.0);
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect layoutMenuRect = NSMakeRect(548, 2, 136, 416);
    if (_openMenu == 0) {
        std::array<NSString*, kLayoutCount> items {};
        for (uint32_t i = 0; i < items.size(); ++i) {
            items[i] = [NSString stringWithUTF8String:s3g::layoutPannerPresetName(static_cast<s3g::LayoutPannerPreset>(layoutPresetForMenuIndex(i)))];
        }
        const int selected = static_cast<int>(menuIndexForLayoutPreset(static_cast<uint32_t>(_plugin->params.layout)));
        s3g::clap_gui::drawDropdownMenu(layoutMenuRect, 16, items.data(), static_cast<uint32_t>(items.size()), selected, -1, attrs, style);
    } else if (_openMenu == 1) {
        NSString* items[2] = { @"SPLIT", @"SEND" };
        s3g::clap_gui::drawDropdownMenu(NSMakeRect(548, 119, 96, 32), 16, items, 2, static_cast<int>(static_cast<uint32_t>(_plugin->params.mode)), -1, attrs, style);
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style {};
    NSFont* font = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* attrs = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:font };
    NSDictionary* dimAttrs = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:font };

    [style.bg setFill];
    NSRectFill(self.bounds);
    [style.strip setFill];
    NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 48));
    [@"s3g SUB CROSSOVER" drawAtPoint:NSMakePoint(22, 18) withAttributes:attrs];
    NSString* mode = [NSString stringWithFormat:@"%s / %.1f Hz", s3g::subCrossoverModeName(_plugin->params.mode), _plugin->params.cutoffHz];
    [mode drawAtPoint:NSMakePoint(548, 18) withAttributes:dimAttrs];

    [self drawField:NSMakeRect(22, 68, 400, 324) attrs:dimAttrs style:style];
    s3g::clap_gui::drawMenu(@"LAYOUT", [NSString stringWithUTF8String:s3g::layoutPannerPresetName(_plugin->params.layout)], 94, attrs, attrs, style, 462.0, 548.0, 136.0);
    s3g::clap_gui::drawMenu(@"MODE", [NSString stringWithUTF8String:s3g::subCrossoverModeName(_plugin->params.mode)], 120, attrs, attrs, style, 462.0, 548.0, 96.0);
    [self drawControl:@"HIGH" value:[NSString stringWithFormat:@"%u", _plugin->params.highChannels] norm:(_plugin->params.highChannels - 1.0) / 63.0 y:158 attrs:attrs valueAttrs:attrs style:style];
    [self drawControl:@"SUBS" value:[NSString stringWithFormat:@"%u", _plugin->params.subCount] norm:(_plugin->params.subCount - 1.0) / 7.0 y:184 attrs:attrs valueAttrs:attrs style:style];
    [self drawControl:@"OFFSET" value:[NSString stringWithFormat:@"%u", _plugin->params.subOffset] norm:(_plugin->params.subOffset - 1.0) / 63.0 y:210 attrs:attrs valueAttrs:attrs style:style];
    [self drawControl:@"CUTOFF" value:[NSString stringWithFormat:@"%.1f", _plugin->params.cutoffHz] norm:(_plugin->params.cutoffHz - 20.0f) / 220.0f y:248 attrs:attrs valueAttrs:attrs style:style];
    [self drawControl:@"FOCUS" value:[NSString stringWithFormat:@"%.2f", _plugin->params.subFocus] norm:(_plugin->params.subFocus - 0.25f) / 7.75f y:274 attrs:attrs valueAttrs:attrs style:style];
    [self drawControl:@"SUB DB" value:[NSString stringWithFormat:@"%+.1f", _plugin->params.subGainDb] norm:(_plugin->params.subGainDb + 60.0f) / 78.0f y:312 attrs:attrs valueAttrs:attrs style:style];
    [self drawControl:@"HIGH DB" value:[NSString stringWithFormat:@"%+.1f", _plugin->params.highGainDb] norm:(_plugin->params.highGainDb + 60.0f) / 78.0f y:338 attrs:attrs valueAttrs:attrs style:style];
    [self drawControl:@"BYPASS" value:(_plugin->params.bypass ? @"ON" : @"OFF") norm:(_plugin->params.bypass ? 1.0 : 0.0) y:376 attrs:attrs valueAttrs:attrs style:style];
    [self drawOpenMenu:attrs style:style];
}

- (void)updateDrag:(NSPoint)pt
{
    if (!_plugin || _dragControl < 0) return;
    const double n = std::clamp((static_cast<double>(pt.x) - 548.0) / 124.0, 0.0, 1.0);
    switch (_dragControl) {
    case 2: setParamValue(*_plugin, kParamHighChannels, 1.0 + n * 63.0); break;
    case 3: setParamValue(*_plugin, kParamSubCount, 1.0 + n * 7.0); break;
    case 4: setParamValue(*_plugin, kParamSubOffset, 1.0 + n * 63.0); break;
    case 5: setParamValue(*_plugin, kParamCutoff, 20.0 + n * 220.0); break;
    case 6: setParamValue(*_plugin, kParamSubFocus, 0.25 + n * 7.75); break;
    case 7: setParamValue(*_plugin, kParamSubGain, -60.0 + n * 78.0); break;
    case 8: setParamValue(*_plugin, kParamHighGain, -60.0 + n * 78.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect layoutMenuRect = NSMakeRect(548, 2, 136, 416);
    if (_openMenu == 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, layoutMenuRect, 16, kLayoutCount);
        if (hit >= 0) setParamValue(*_plugin, kParamLayout, layoutPresetForMenuIndex(static_cast<uint32_t>(hit)));
        _openMenu = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu == 1) {
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(548, 119, 96, 32), 16, 2);
        if (hit >= 0) setParamValue(*_plugin, kParamMode, hit);
        _openMenu = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, NSMakeRect(548, 93, 136, 18))) { _openMenu = 0; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(548, 119, 96, 18))) { _openMenu = 1; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(548, 373, 124, 18))) {
        setParamValue(*_plugin, kParamBypass, _plugin->params.bypass ? 0.0 : 1.0);
        [self setNeedsDisplay:YES];
        return;
    }

    const CGFloat ys[] = { 158, 184, 210, 248, 274, 312, 338 };
    for (int i = 0; i < 7; ++i) {
        if (NSPointInRect(pt, NSMakeRect(548, ys[i] - 5, 124, 22))) {
            _dragControl = i + 2;
            [self updateDrag:pt];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    [self updateDrag:[self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragControl = -1;
}
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GSubCrossoverView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3GSubCrossoverView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 760; *h = 420; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, 760, 420)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GSubCrossoverView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GSubCrossoverView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
}
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.sub-crossover",
    "s3g Sub Crossover",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Multichannel sub crossover and low-frequency send for Layout Panner speaker arrays.",
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

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
