#include "s3g_ambi_group_matrix_128.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/fixedpoint.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kStateVersion = 2;
constexpr uint32_t kGuiWidth = 1040;
constexpr uint32_t kGuiHeight = 648;
constexpr uint32_t kCrosspointCount = s3g::kAmbiGroupMatrix128Groups * s3g::kAmbiGroupMatrix128Groups;

constexpr clap_id kCrosspointBase = 100;
constexpr clap_id kParamFlow = 1000;
constexpr clap_id kParamSpread = 1001;
constexpr clap_id kParamVortex = 1002;
constexpr clap_id kParamMotion = 1003;
constexpr clap_id kParamMode = 1004;
constexpr clap_id kParamRate = 1005;
constexpr clap_id kParamDivision = 1006;
constexpr clap_id kParamPhase = 1007;
constexpr clap_id kParamSmoothing = 1008;
constexpr clap_id kParamOutput = 1009;
constexpr clap_id kParamShape = 1010;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiGroupMatrix128Params params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::AmbiGroupMatrix128Params params = s3g::makeDefaultAmbiGroupMatrix128Params();
    s3g::AmbiGroupMatrix128 matrix;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool isCrosspointParam(clap_id id)
{
    return id >= kCrosspointBase && id < kCrosspointBase + kCrosspointCount;
}

uint32_t crosspointIndexFromParam(clap_id id)
{
    return static_cast<uint32_t>(id - kCrosspointBase);
}

double defaultValueForParam(clap_id id)
{
    if (isCrosspointParam(id)) {
        const uint32_t idx = crosspointIndexFromParam(id);
        const uint32_t src = idx / s3g::kAmbiGroupMatrix128Groups;
        const uint32_t dst = idx % s3g::kAmbiGroupMatrix128Groups;
        return src == dst ? 0.0 : -80.0;
    }
    switch (id) {
    case kParamFlow: return 0.0;
    case kParamSpread: return 0.0;
    case kParamVortex: return 0.0;
    case kParamMotion: return 0.0;
    case kParamMode: return 0.0;
    case kParamShape: return 0.0;
    case kParamRate: return 0.15;
    case kParamDivision: return 16.0;
    case kParamPhase: return 0.0;
    case kParamSmoothing: return 35.0;
    case kParamOutput: return 0.0;
    default: return 0.0;
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (isCrosspointParam(id)) {
        p.params.crosspointDb[crosspointIndexFromParam(id)] = static_cast<float>(std::clamp(value, -80.0, 12.0));
    } else {
        switch (id) {
        case kParamFlow: p.params.flow = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
        case kParamSpread: p.params.spread = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
        case kParamVortex: p.params.vortex = static_cast<float>(std::clamp(value, -1.0, 1.0)); break;
        case kParamMotion: p.params.motion = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
        case kParamShape: p.params.shape = s3g::matrixFlowShapeFromIndex(static_cast<uint32_t>(std::round(std::clamp(value, 0.0, 5.0)))); break;
        case kParamMode: p.params.mode = value >= 0.5 ? s3g::AmbiGroupMatrix128FlowMode::Sync : s3g::AmbiGroupMatrix128FlowMode::Free; break;
        case kParamRate: p.params.rate = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
        case kParamDivision: p.params.divisionBeats = static_cast<float>(std::clamp(value, 0.25, 64.0)); break;
        case kParamPhase: p.params.phaseOffset = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
        case kParamSmoothing: p.params.smoothingMs = static_cast<float>(std::clamp(value, 1.0, 500.0)); break;
        case kParamOutput: p.params.outputGainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
        default: break;
        }
    }
    p.matrix.setParams(p.params);
}

void updateTransportPhase(Plugin& p, const clap_event_transport_t* transport)
{
    if (p.params.mode != s3g::AmbiGroupMatrix128FlowMode::Sync) {
        p.matrix.useFreePhase();
        return;
    }
    if (transport && (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0) {
        const double beats = static_cast<double>(transport->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
        const double div = std::max(0.25, static_cast<double>(p.params.divisionBeats));
        const double phase = std::fmod(beats / div, 1.0);
        p.matrix.setExternalPhase(static_cast<float>(phase < 0.0 ? phase + 1.0 : phase));
    } else {
        p.matrix.useFreePhase();
    }
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) {
        return;
    }
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            applyParam(p, param->param_id, param->value);
        }
    }
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiView) {
        NSView* v = static_cast<NSView*>(p->guiView);
        [v removeFromSuperview];
        [v release];
        p->guiView = nullptr;
    }
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->matrix.prepare(sampleRate);
    p->matrix.setParams(p->params);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->matrix.prepare(p->sampleRate);
    p->matrix.setParams(p->params);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

template <typename Sample>
float peakForBuffer(Sample* const* outputs, uint32_t channels, uint32_t frames)
{
    float peak = 0.0f;
    if (!outputs) {
        return peak;
    }
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (!outputs[ch]) {
            continue;
        }
        for (uint32_t frame = 0; frame < frames; ++frame) {
            peak = std::max(peak, static_cast<float>(std::fabs(outputs[ch][frame])));
        }
    }
    return peak;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    updateTransportPhase(*p, process->transport);
    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t outChannels = output.channel_count;

    if (input.data32 && output.data32) {
        p->matrix.process(input.data32, input.channel_count, output.data32, outChannels, frames);
        s3g::clearAudioBufferFromChannel(output, s3g::kAmbiGroupMatrix128Channels, frames);
        p->outputPeak.store(peakForBuffer(output.data32, std::min<uint32_t>(outChannels, s3g::kAmbiGroupMatrix128Channels), frames),
            std::memory_order_relaxed);
    } else if (input.data64 && output.data64) {
        p->matrix.process(input.data64, input.channel_count, output.data64, outChannels, frames);
        s3g::clearAudioBufferFromChannel(output, s3g::kAmbiGroupMatrix128Channels, frames);
        p->outputPeak.store(peakForBuffer(output.data64, std::min<uint32_t>(outChannels, s3g::kAmbiGroupMatrix128Channels), frames),
            std::memory_order_relaxed);
    }
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) {
        return false;
    }
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "128ch In" : "128ch Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = s3g::kAmbiGroupMatrix128Channels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kCrosspointCount + 11; }

clap_id paramIdForIndex(uint32_t index)
{
    if (index < kCrosspointCount) {
        return kCrosspointBase + index;
    }
    switch (index - kCrosspointCount) {
    case 0: return kParamFlow;
    case 1: return kParamSpread;
    case 2: return kParamVortex;
    case 3: return kParamMotion;
    case 4: return kParamShape;
    case 5: return kParamMode;
    case 6: return kParamRate;
    case 7: return kParamDivision;
    case 8: return kParamPhase;
    case 9: return kParamSmoothing;
    case 10: return kParamOutput;
    default: return CLAP_INVALID_ID;
    }
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) {
        return false;
    }
    const clap_id id = paramIdForIndex(index);
    if (id == CLAP_INVALID_ID) {
        return false;
    }
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::memset(info->module, 0, sizeof(info->module));
    if (isCrosspointParam(id)) {
        const uint32_t idx = crosspointIndexFromParam(id);
        const uint32_t src = idx / s3g::kAmbiGroupMatrix128Groups + 1u;
        const uint32_t dst = idx % s3g::kAmbiGroupMatrix128Groups + 1u;
        std::snprintf(info->name, sizeof(info->name), "S%u to D%u", src, dst);
        std::strncpy(info->module, "Group Matrix", sizeof(info->module));
        info->min_value = -80.0;
        info->max_value = 12.0;
        info->default_value = defaultValueForParam(id);
        return true;
    }
    switch (id) {
    case kParamFlow:
        std::strncpy(info->name, "Depth", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0; break;
    case kParamSpread:
        std::strncpy(info->name, "Spread", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0; break;
    case kParamVortex:
        std::strncpy(info->name, "Vortex", sizeof(info->name));
        info->min_value = -1.0; info->max_value = 1.0; break;
    case kParamMotion:
        std::strncpy(info->name, "Motion", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0; break;
    case kParamShape:
        std::strncpy(info->name, "Shape", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 5.0; break;
    case kParamMode:
        std::strncpy(info->name, "Mode", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0; break;
    case kParamRate:
        std::strncpy(info->name, "Rate", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0; break;
    case kParamDivision:
        std::strncpy(info->name, "Division", sizeof(info->name));
        info->min_value = 0.25; info->max_value = 64.0; break;
    case kParamPhase:
        std::strncpy(info->name, "Phase", sizeof(info->name));
        info->min_value = 0.0; info->max_value = 1.0; break;
    case kParamSmoothing:
        std::strncpy(info->name, "Smoothing", sizeof(info->name));
        info->min_value = 1.0; info->max_value = 500.0; break;
    case kParamOutput:
        std::strncpy(info->name, "Output", sizeof(info->name));
        info->min_value = -60.0; info->max_value = 12.0; break;
    default:
        return false;
    }
    std::strncpy(info->module, "Motion", sizeof(info->module));
    info->default_value = defaultValueForParam(id);
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) {
        return false;
    }
    const auto& p = self(plugin)->params;
    if (isCrosspointParam(paramId)) {
        *value = p.crosspointDb[crosspointIndexFromParam(paramId)];
        return true;
    }
    switch (paramId) {
    case kParamFlow: *value = p.flow; return true;
    case kParamSpread: *value = p.spread; return true;
    case kParamVortex: *value = p.vortex; return true;
    case kParamMotion: *value = p.motion; return true;
    case kParamShape: *value = static_cast<double>(static_cast<uint32_t>(p.shape)); return true;
    case kParamMode: *value = p.mode == s3g::AmbiGroupMatrix128FlowMode::Sync ? 1.0 : 0.0; return true;
    case kParamRate: *value = p.rate; return true;
    case kParamDivision: *value = p.divisionBeats; return true;
    case kParamPhase: *value = p.phaseOffset; return true;
    case kParamSmoothing: *value = p.smoothingMs; return true;
    case kParamOutput: *value = p.outputGainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) {
        return false;
    }
    if (isCrosspointParam(paramId) || paramId == kParamOutput) {
        std::snprintf(display, size, value <= -79.9 ? "-inf" : "%+.1f dB", value);
        return true;
    }
    if (paramId == kParamSmoothing) {
        std::snprintf(display, size, "%.0f ms", value);
        return true;
    }
    if (paramId == kParamMode) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "SYNC" : "FREE");
        return true;
    }
    if (paramId == kParamShape) {
        const auto shape = s3g::matrixFlowShapeFromIndex(static_cast<uint32_t>(std::round(std::clamp(value, 0.0, 5.0))));
        std::snprintf(display, size, "%s", s3g::matrixFlowShapeName(shape));
        return true;
    }
    if (paramId == kParamDivision) {
        std::snprintf(display, size, "%.2g beats", value);
        return true;
    }
    if (paramId == kParamVortex) {
        std::snprintf(display, size, "%+.2f", value);
        return true;
    }
    std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* display, double* value)
{
    if (!display || !value) {
        return false;
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) {
        return false;
    }
    const SavedState state { kStateVersion, self(plugin)->params };
    return stream->write(stream, &state, sizeof(state)) == static_cast<int64_t>(sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) {
        return false;
    }
    SavedState state {};
    if (stream->read(stream, &state, sizeof(state)) != static_cast<int64_t>(sizeof(state)) || state.version != kStateVersion) {
        return false;
    }
    auto* p = self(plugin);
    p->params = state.params;
    p->matrix.setParams(p->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GAmbiGroupMatrix128View : NSView {
@private
    void* _plugin;
    NSTimer* _timer;
    NSInteger _dragCell;
    NSInteger _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    bool _showGlossary;
    bool _dragRandomDev;
    CGFloat _randomDev;
    uint32_t _randomState;
}
- (instancetype)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (NSDictionary*)attrs:(NSColor*)color size:(CGFloat)size;
- (NSRect)shapeMenuBoxRect;
- (NSRect)shapeDropdownRect;
- (NSRect)modeMenuBoxRect;
- (NSRect)modeDropdownRect;
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)updateMenuHover:(NSPoint)pt;
- (void)drawFlowPreview:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawGlossary:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (NSRect)randomButtonRect;
- (NSRect)randomDevSliderRect;
- (float)randomUnit;
- (void)randomizeMatrix;
- (void)updateRandomDev:(NSPoint)pt;
- (void)updateCell:(NSPoint)pt;
- (void)updateSlider:(NSPoint)pt;
@end

@implementation S3GAmbiGroupMatrix128View
- (instancetype)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragCell = -1;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _showGlossary = true;
        _dragRandomDev = false;
        _randomDev = 0.50;
        _randomState = 0x1285137u;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) {
        [self removeTrackingArea:area];
    }
    [super updateTrackingAreas];
    NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer
{
    if (!_timer) {
        _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 24.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    }
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
    [self setNeedsDisplay:YES];
}
- (NSDictionary*)attrs:(NSColor*)color size:(CGFloat)size
{
    NSFont* font = [NSFont fontWithName:@"Menlo" size:size] ?: [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    return @{ NSForegroundColorAttributeName:color, NSFontAttributeName:font };
}
- (NSRect)modeMenuBoxRect
{
    return NSMakeRect(838.0, 107.0, 118.0, 17.0);
}
- (NSRect)modeDropdownRect
{
    return NSMakeRect(838.0, 125.0, 118.0, 40.0);
}
- (NSRect)shapeMenuBoxRect
{
    return NSMakeRect(838.0, 81.0, 118.0, 17.0);
}
- (NSRect)shapeDropdownRect
{
    return NSMakeRect(838.0, 99.0, 118.0, 120.0);
}
- (NSRect)randomButtonRect
{
    return NSMakeRect(390.0, 45.0, 48.0, 15.0);
}
- (NSRect)randomDevSliderRect
{
    return NSMakeRect(286.0, 454.0, 92.0, 14.0);
}
- (float)randomUnit
{
    _randomState = _randomState * 1664525u + 1013904223u;
    return static_cast<float>((_randomState >> 8) & 0x00ffffffu) / 16777215.0f;
}
- (void)randomizeMatrix
{
    auto* p = static_cast<Plugin*>(_plugin);
    for (uint32_t i = 0; i < kCrosspointCount; ++i) {
        applyParam(*p, kCrosspointBase + i, -80.0);
    }
    for (uint32_t src = 0; src < s3g::kAmbiGroupMatrix128Groups; ++src) {
        for (uint32_t dst = 0; dst < s3g::kAmbiGroupMatrix128Groups; ++dst) {
            const bool diagonal = src == dst;
            const float chance = diagonal ? (0.95f - 0.35f * static_cast<float>(_randomDev))
                                          : (0.10f + 0.65f * static_cast<float>(_randomDev));
            if ([self randomUnit] > chance) {
                continue;
            }
            const float r = [self randomUnit];
            const double db = diagonal
                ? -12.0 + r * 12.0
                : -42.0 + r * (36.0 + 6.0 * static_cast<float>(_randomDev));
            applyParam(*p, kCrosspointBase + s3g::AmbiGroupMatrix128Index(src, dst), db);
        }
    }
    [self setNeedsDisplay:YES];
}
- (void)updateRandomDev:(NSPoint)pt
{
    _randomDev = std::clamp<CGFloat>((pt.x - [self randomDevSliderRect].origin.x) / [self randomDevSliderRect].size.width, 0.0, 1.0);
    [self setNeedsDisplay:YES];
}
- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0) {
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    static NSString* const shapeItems[] = { @"FLOW", @"PULSE", @"CHASE", @"SWIRL", @"SCAT", @"HOLD" };
    static NSString* const modeItems[] = { @"FREE", @"SYNC" };
    if (_openMenu == 1) {
        const int selected = p->params.mode == s3g::AmbiGroupMatrix128FlowMode::Sync ? 1 : 0;
        s3g::clap_gui::drawDropdownMenu([self modeDropdownRect], 20.0, modeItems, 2, selected, _hoverMenuItem, attrs, style);
    } else if (_openMenu == 2) {
        const int selected = static_cast<int>(static_cast<uint32_t>(p->params.shape));
        s3g::clap_gui::drawDropdownMenu([self shapeDropdownRect], 20.0, shapeItems, 6, selected, _hoverMenuItem, attrs, style);
    }
}
- (void)updateMenuHover:(NSPoint)pt
{
    if (_openMenu <= 0) {
        return;
    }
    const NSRect rect = _openMenu == 1 ? [self modeDropdownRect] : [self shapeDropdownRect];
    const uint32_t count = _openMenu == 1 ? 2u : 6u;
    const int next = s3g::clap_gui::dropdownHitIndex(pt, rect, 20.0, count);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)drawFlowPreview:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    [s3g::clap_gui::color(0x101010) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    const CGFloat leftX = rect.origin.x + 42.0;
    const CGFloat rightX = rect.origin.x + rect.size.width - 42.0;
    const CGFloat topY = rect.origin.y + 30.0;
    const CGFloat rowGap = (rect.size.height - 60.0) / static_cast<CGFloat>(s3g::kAmbiGroupMatrix128Groups - 1u);
    const CGFloat centerX = rect.origin.x + rect.size.width * 0.5;
    const float audioPhase = p->matrix.previewPhase();
    const auto generated = p->matrix.generatedFlowPreview(audioPhase);

    auto pointFor = [&](bool dest, uint32_t index) -> NSPoint {
        return NSMakePoint(dest ? rightX : leftX, topY + static_cast<CGFloat>(index) * rowGap);
    };

    for (uint32_t src = 0; src < s3g::kAmbiGroupMatrix128Groups; ++src) {
        const NSPoint a = pointFor(false, src);
        for (uint32_t dst = 0; dst < s3g::kAmbiGroupMatrix128Groups; ++dst) {
            const uint32_t idx = s3g::AmbiGroupMatrix128Index(src, dst);
            const float manual = s3g::AmbiGroupMatrix128DbToGain(p->params.crosspointDb[idx]);
            if (manual <= 0.000001f) {
                continue;
            }
            const float manualNorm = std::clamp((p->params.crosspointDb[idx] + 80.0f) / 92.0f, 0.0f, 1.0f);
            const float flowWeight = generated[idx];
            const float currentWeight = manualNorm * ((1.0f - p->params.motion) + flowWeight * p->params.motion);
            const float drawWeight = std::max(manualNorm * 0.30f, currentWeight);
            if (drawWeight < 0.012f) {
                continue;
            }
            const NSPoint b = pointFor(true, dst);
            NSBezierPath* path = [NSBezierPath bezierPath];
            [path moveToPoint:a];
            const CGFloat arc = (static_cast<CGFloat>(dst) - static_cast<CGFloat>(src)) * 18.0
                + static_cast<CGFloat>(p->params.vortex) * 58.0;
            [path curveToPoint:b
                 controlPoint1:NSMakePoint(centerX - 48.0, (a.y + b.y) * 0.5 + arc)
                 controlPoint2:NSMakePoint(centerX + 48.0, (a.y + b.y) * 0.5 + arc)];
            [path setLineWidth:0.45 + 3.2 * static_cast<CGFloat>(drawWeight)];
            const CGFloat greenMix = static_cast<CGFloat>(std::clamp(p->params.motion * flowWeight, 0.0f, 1.0f));
            const CGFloat gray = 0.42 + 0.42 * static_cast<CGFloat>(manualNorm);
            const CGFloat red = gray * (1.0 - greenMix) + 0.10 * greenMix;
            const CGFloat green = gray * (1.0 - greenMix) + 0.92 * greenMix;
            const CGFloat blue = gray * (1.0 - greenMix) + 0.30 * greenMix;
            [[NSColor colorWithCalibratedRed:red green:green blue:blue alpha:std::min<CGFloat>(0.86, 0.08 + drawWeight * 0.74)] setStroke];
            [path stroke];
        }
    }

    for (uint32_t i = 0; i < s3g::kAmbiGroupMatrix128Groups; ++i) {
        const NSPoint srcPt = pointFor(false, i);
        const NSPoint dstPt = pointFor(true, i);
        [style.strip setFill];
        NSRectFill(NSMakeRect(srcPt.x - 10.0, srcPt.y - 10.0, 20.0, 20.0));
        NSRectFill(NSMakeRect(dstPt.x - 10.0, dstPt.y - 10.0, 20.0, 20.0));
        [style.grid setStroke];
        NSFrameRect(NSMakeRect(srcPt.x - 10.0, srcPt.y - 10.0, 20.0, 20.0));
        NSFrameRect(NSMakeRect(dstPt.x - 10.0, dstPt.y - 10.0, 20.0, 20.0));
        [[NSString stringWithFormat:@"S%u", i + 1u] drawAtPoint:NSMakePoint(srcPt.x - 7.0, srcPt.y - 6.0) withAttributes:attrs];
        [[NSString stringWithFormat:@"D%u", i + 1u] drawAtPoint:NSMakePoint(dstPt.x - 7.0, dstPt.y - 6.0) withAttributes:attrs];
    }

    NSString* caption = p->params.motion <= 0.001f
        ? @"MOTN 0: static matrix"
        : [NSString stringWithFormat:@"MOTN %.0f%% inside ceiling", static_cast<double>(p->params.motion * 100.0f)];
    [caption drawAtPoint:NSMakePoint(rect.origin.x + 12.0, rect.origin.y + rect.size.height - 24.0) withAttributes:attrs];
}
- (void)drawGlossary:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y, rect.size.width, rect.size.height, style);
    s3g::clap_gui::drawDisclosurePanelHeader(@"PATTERN TERMS", _showGlossary, rect.origin.x, rect.origin.y, rect.size.width, 21.0, attrs, style);
    if (!_showGlossary) {
        return;
    }

    const CGFloat col1 = rect.origin.x + 16.0;
    const CGFloat col2 = rect.origin.x + 344.0;
    const CGFloat col3 = rect.origin.x + 672.0;
    const CGFloat y1 = rect.origin.y + 36.0;
    const CGFloat y2 = rect.origin.y + 60.0;
    const CGFloat y3 = rect.origin.y + 84.0;
    const CGFloat y4 = rect.origin.y + 108.0;

    [@"CEIL   manual cell is the max/permission for a route" drawAtPoint:NSMakePoint(col1, y1) withAttributes:attrs];
    [@"-INF   closed cell: motion cannot pass" drawAtPoint:NSMakePoint(col1, y2) withAttributes:attrs];
    [@"MOTN   movement depth; 0 = normal matrix" drawAtPoint:NSMakePoint(col1, y3) withAttributes:attrs];
    [@"PHAS   offsets the visible/audio motion cycle" drawAtPoint:NSMakePoint(col1, y4) withAttributes:attrs];

    [@"DPTH   generated pattern depth" drawAtPoint:NSMakePoint(col2, y1) withAttributes:attrs];
    [@"SPRD   widens allowed route distribution" drawAtPoint:NSMakePoint(col2, y2) withAttributes:attrs];
    [@"VORT   rotates the generated motion shape" drawAtPoint:NSMakePoint(col2, y3) withAttributes:attrs];
    [@"SMTH   gain smoothing for route changes" drawAtPoint:NSMakePoint(col2, y4) withAttributes:attrs];

    [@"MODE   FREE runs internally; SYNC follows transport" drawAtPoint:NSMakePoint(col3, y1) withAttributes:attrs];
    [@"RATE   free-running cycle speed" drawAtPoint:NSMakePoint(col3, y2) withAttributes:attrs];
    [@"DIV    synced cycle length in beats" drawAtPoint:NSMakePoint(col3, y3) withAttributes:attrs];
    [@"SHAPE  generated gain pattern" drawAtPoint:NSMakePoint(col3, y4) withAttributes:attrs];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* text = [self attrs:style.text size:10.0];
    NSDictionary* small = [self attrs:style.dim size:10.0];
    NSDictionary* title = [self attrs:style.text size:10.5];

    [@"s3g AMBI GROUP MATRIX 128" drawAtPoint:NSMakePoint(18, 14) withAttributes:title];
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [s3g::clap_gui::peakDbText(pk)
        drawAtPoint:NSMakePoint(846, 14) withAttributes:small];
    [@"8 x 3OA / 128CH" drawAtPoint:NSMakePoint(926, 14) withAttributes:small];

    s3g::clap_gui::drawPanelFrame(18, 42, 430, 440, style);
    s3g::clap_gui::drawPanelHeader(@"GROUP MATRIX", true, 18, 42, 430, 21, text, style);
    s3g::clap_gui::drawHeaderActionButton([self randomButtonRect], NSMakeRect(18, 42, 430, 21), @"RAND", small, style);
    s3g::clap_gui::drawPanelFrame(466, 42, 254, 440, style);
    s3g::clap_gui::drawPanelHeader(@"PATTERN PREVIEW", true, 466, 42, 254, 21, text, style);
    s3g::clap_gui::drawPanelFrame(738, 42, 284, 288, style);
    s3g::clap_gui::drawPanelHeader(@"PATTERN", true, 738, 42, 284, 21, text, style);
    s3g::clap_gui::drawPanelFrame(738, 346, 284, 96, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 738, 346, 284, 21, text, style);

    const NSRect matrixRect = NSMakeRect(72.0, 90.0, 344.0, 344.0);
    const CGFloat cell = matrixRect.size.width / static_cast<CGFloat>(s3g::kAmbiGroupMatrix128Groups);
    const CGFloat gap = 4.0;
    const float livePhase = p->matrix.previewPhase();
    const auto liveFlow = p->matrix.generatedFlowPreview(livePhase);
    for (uint32_t i = 0; i < s3g::kAmbiGroupMatrix128Groups; ++i) {
        [[NSString stringWithFormat:@"%u", i + 1u] drawAtPoint:NSMakePoint(matrixRect.origin.x + i * cell + cell * 0.38, matrixRect.origin.y - 18.0) withAttributes:small];
        [[NSString stringWithFormat:@"%u", i + 1u] drawAtPoint:NSMakePoint(matrixRect.origin.x - 24.0, matrixRect.origin.y + i * cell + cell * 0.38) withAttributes:small];
    }

    for (uint32_t src = 0; src < s3g::kAmbiGroupMatrix128Groups; ++src) {
        for (uint32_t dst = 0; dst < s3g::kAmbiGroupMatrix128Groups; ++dst) {
            const uint32_t idx = s3g::AmbiGroupMatrix128Index(src, dst);
            const float db = p->params.crosspointDb[idx];
            NSRect r = NSMakeRect(matrixRect.origin.x + dst * cell, matrixRect.origin.y + src * cell, cell - gap, cell - gap);
            [style.strip setFill];
            NSRectFill(r);
            [style.grid setStroke];
            NSFrameRect(r);
            const CGFloat manualNorm = static_cast<CGFloat>(std::clamp((db + 80.0f) / 92.0f, 0.0f, 1.0f));
            if (manualNorm > 0.01) {
                const CGFloat inset = 5.0;
                NSRect fill = NSInsetRect(r, inset, inset);
                fill.size.height *= manualNorm;
                fill.origin.y = r.origin.y + r.size.height - inset - fill.size.height;
                const CGFloat ceilingGray = 0.18 + 0.22 * manualNorm;
                [[NSColor colorWithCalibratedWhite:ceilingGray alpha:0.96] setFill];
                NSRectFill(fill);

                const float flowWeight = liveFlow[idx];
                const CGFloat liveNorm = static_cast<CGFloat>(manualNorm * ((1.0f - p->params.motion) + flowWeight * p->params.motion));
                NSRect liveFill = NSInsetRect(r, inset, inset);
                liveFill.size.height *= liveNorm;
                liveFill.origin.y = r.origin.y + r.size.height - inset - liveFill.size.height;
                const CGFloat greenMix = static_cast<CGFloat>(std::clamp(p->params.motion * flowWeight, 0.0f, 1.0f));
                const CGFloat alpha = 0.16 + 0.82 * greenMix * liveNorm;
                [[NSColor colorWithCalibratedRed:0.08 green:1.00 blue:0.26 alpha:alpha] setFill];
                NSRectFill(liveFill);
            }
            NSString* label = db <= -79.9f ? @"-inf" : [NSString stringWithFormat:@"%+.0f", db];
            [label drawAtPoint:NSMakePoint(r.origin.x + 5.0, r.origin.y + r.size.height * 0.42) withAttributes:text];
        }
    }
    s3g::clap_gui::drawSlider(@"DEV", [NSString stringWithFormat:@"%.0f%%", static_cast<double>(_randomDev * 100.0)],
                              _randomDev, 454, small, small, style, 244, 286, 390, 92);
    [self drawFlowPreview:NSMakeRect(486, 78, 214, 378) attrs:small style:style];

    const auto& prm = p->params;
    s3g::clap_gui::drawMenu(@"SHAPE", [NSString stringWithUTF8String:s3g::matrixFlowShapeName(prm.shape)], 82, small, small, style, 752, 838, 118);
    s3g::clap_gui::drawMenu(@"MODE", prm.mode == s3g::AmbiGroupMatrix128FlowMode::Sync ? @"SYNC" : @"FREE", 108, small, small, style, 752, 838, 118);
    s3g::clap_gui::drawSlider(@"DPTH", [NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.flow * 100.0f)], prm.flow, 134, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"SPRD", [NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.spread * 100.0f)], prm.spread, 160, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"VORT", [NSString stringWithFormat:@"%+.2f", static_cast<double>(prm.vortex)], (prm.vortex + 1.0f) * 0.5f, 186, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"MOTN", [NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.motion * 100.0f)], prm.motion, 212, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"RATE", [NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.rate * 100.0f)], prm.rate, 238, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"DIV", [NSString stringWithFormat:@"%.2g", static_cast<double>(prm.divisionBeats)], (prm.divisionBeats - 0.25f) / 63.75f, 264, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"PHAS", [NSString stringWithFormat:@"%.0f%%", static_cast<double>(prm.phaseOffset * 100.0f)], prm.phaseOffset, 290, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"SMTH", [NSString stringWithFormat:@"%.0f", static_cast<double>(prm.smoothingMs)], (prm.smoothingMs - 1.0f) / 499.0f, 316, small, small, style, 752, 838, 978, 118);
    s3g::clap_gui::drawSlider(@"OUT", [NSString stringWithFormat:@"%+.1f", static_cast<double>(prm.outputGainDb)], (prm.outputGainDb + 60.0f) / 72.0f, 386, small, small, style, 752, 838, 978, 118);
    const CGFloat glossaryH = _showGlossary ? 132.0 : 21.0;
    [self drawGlossary:NSMakeRect(18, 500, 1004, glossaryH) attrs:small style:style];
    [self drawOpenMenu:small style:style];
}
- (void)updateCell:(NSPoint)pt
{
    if (_dragCell < 0) {
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t idx = static_cast<uint32_t>(_dragCell);
    const NSRect matrixRect = NSMakeRect(72.0, 90.0, 344.0, 344.0);
    const CGFloat cell = matrixRect.size.width / static_cast<CGFloat>(s3g::kAmbiGroupMatrix128Groups);
    const uint32_t src = idx / s3g::kAmbiGroupMatrix128Groups;
    const double n = std::clamp(1.0 - (pt.y - (matrixRect.origin.y + static_cast<CGFloat>(src) * cell)) / cell, 0.0, 1.0);
    applyParam(*p, kCrosspointBase + idx, -80.0 + n * 92.0);
    [self setNeedsDisplay:YES];
}
- (void)updateSlider:(NSPoint)pt
{
    if (_dragSlider < 0) {
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((pt.x - 838.0) / 118.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 0: applyParam(*p, kParamFlow, n); break;
    case 1: applyParam(*p, kParamSpread, n); break;
    case 2: applyParam(*p, kParamVortex, -1.0 + n * 2.0); break;
    case 3: applyParam(*p, kParamMotion, n); break;
    case 4: applyParam(*p, kParamRate, n); break;
    case 5: applyParam(*p, kParamDivision, 0.25 + n * 63.75); break;
    case 6: applyParam(*p, kParamPhase, n); break;
    case 7: applyParam(*p, kParamSmoothing, 1.0 + n * 499.0); break;
    case 8: applyParam(*p, kParamOutput, -60.0 + n * 72.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if ([[self window] firstResponder] != self) {
        [[self window] makeFirstResponder:self];
    }
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu > 0) {
        const NSRect menuRect = _openMenu == 1 ? [self modeDropdownRect] : [self shapeDropdownRect];
        const uint32_t count = _openMenu == 1 ? 2u : 6u;
        if (NSPointInRect(pt, menuRect)) {
            const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuRect, 20.0, count);
            if (hit >= 0) {
                if (_openMenu == 1) {
                    applyParam(*p, kParamMode, hit == 1 ? 1.0 : 0.0);
                } else {
                    applyParam(*p, kParamShape, static_cast<double>(hit));
                }
            }
            _openMenu = 0;
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
    }
    if (NSPointInRect(pt, NSMakeRect(18, 500, 1004, 21))) {
        _showGlossary = !_showGlossary;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, [self randomButtonRect])) {
        [self randomizeMatrix];
        return;
    }
    if (NSPointInRect(pt, NSInsetRect([self randomDevSliderRect], -40.0, -8.0))) {
        _dragRandomDev = true;
        [self updateRandomDev:pt];
        return;
    }
    const NSRect matrixRect = NSMakeRect(72.0, 90.0, 344.0, 344.0);
    const CGFloat cell = matrixRect.size.width / static_cast<CGFloat>(s3g::kAmbiGroupMatrix128Groups);
    const CGFloat gap = 4.0;
    for (uint32_t src = 0; src < s3g::kAmbiGroupMatrix128Groups; ++src) {
        for (uint32_t dst = 0; dst < s3g::kAmbiGroupMatrix128Groups; ++dst) {
            NSRect r = NSMakeRect(matrixRect.origin.x + dst * cell, matrixRect.origin.y + src * cell, cell - gap, cell - gap);
            if (NSPointInRect(pt, r)) {
                _dragCell = s3g::AmbiGroupMatrix128Index(src, dst);
                [self updateCell:pt];
                return;
            }
        }
    }
    if (NSPointInRect(pt, [self modeMenuBoxRect])) {
        _openMenu = 1;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, [self shapeMenuBoxRect])) {
        _openMenu = 2;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat ys[] = { 134, 160, 186, 212, 238, 264, 290, 316, 386 };
    for (int i = 0; i < 9; ++i) {
        if (NSPointInRect(pt, NSMakeRect(744, ys[i] - 9, 276, 24))) {
            _dragSlider = i;
            [self updateSlider:pt];
            return;
        }
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]];
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:pt];
    if (_dragCell >= 0) {
        [self updateCell:pt];
    } else if (_dragSlider >= 0) {
        [self updateSlider:pt];
    } else if (_dragRandomDev) {
        [self updateRandomDev:pt];
    }
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragCell = -1;
    _dragSlider = -1;
    _dragRandomDev = false;
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiGroupMatrix128View alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible = false;
        auto* v = static_cast<S3GAmbiGroupMatrix128View*>(p->guiView);
        [v stopRefreshTimer];
        [v removeFromSuperview];
        [v release];
        p->guiView = nullptr;
    }
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
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
    NSView* v = static_cast<NSView*>(p->guiView);
    [parent addSubview:v];
    [v setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbiGroupMatrix128View*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiGroupMatrix128View*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    "org.s3g.s3g-dsp.ambi-group-matrix-128",
    "s3g Ambi Group Matrix 128",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "128-channel lane-locked 8x3OA group matrix mixer with automatable bus crosspoints.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->matrix.setParams(p->params);
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
    p->plugin.on_main_thread = [](const clap_plugin_t*) {};
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
