#include "s3g_layout_panner.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
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
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kInputChannels = s3g::kLayoutPannerSources;
constexpr uint32_t kOutputChannels = s3g::kLayoutPannerMaxSpeakers;
constexpr uint32_t kStateVersion = 5;
constexpr uint32_t kLayoutCount = 13;

constexpr clap_id kLayoutParamId = 1;
constexpr clap_id kMethodParamId = 2;
constexpr clap_id kFocusParamId = 3;
constexpr clap_id kRolloffParamId = 4;
constexpr clap_id kSmoothingParamId = 5;
constexpr clap_id kGlobalAzimuthParamId = 6;
constexpr clap_id kGlobalElevationParamId = 7;
constexpr clap_id kGlobalDistanceParamId = 8;
constexpr clap_id kDiffusionParamId = 9;
constexpr clap_id kOutputParamId = 10;
constexpr clap_id kSelectedSourceParamId = 11;
constexpr clap_id kSelectedAzimuthParamId = 12;
constexpr clap_id kSelectedElevationParamId = 13;
constexpr clap_id kSelectedDistanceParamId = 14;
constexpr clap_id kSelectedGainParamId = 15;
constexpr clap_id kSourceParamBase = 100;

enum SourceParamOffset : clap_id {
    kSourceAzOffset = 0,
    kSourceElOffset = 1,
    kSourceDistanceOffset = 2,
    kSourceGainOffset = 3,
    kSourceMuteOffset = 4,
    kSourceSoloOffset = 5,
    kSourceParamStride = 6,
};

constexpr clap_id sourceParamId(uint32_t source, SourceParamOffset offset)
{
    return kSourceParamBase + source * kSourceParamStride + offset;
}

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::LayoutPannerParams params {};
    std::array<s3g::LayoutPannerSource, s3g::kLayoutPannerSources> sources {};
    std::array<s3g::LayoutPannerSpeaker, s3g::kLayoutPannerMaxSpeakers> speakers {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::LayoutPannerParams params {};
    s3g::LayoutPanner panner;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void setSelectedSourceValue(Plugin& p, clap_id id, double value)
{
    const uint32_t index = p.params.selectedSource;
    auto source = p.panner.source(index);
    switch (id) {
    case kSelectedAzimuthParamId: source.azimuthDeg = static_cast<float>(std::clamp(value, -180.0, 180.0)); break;
    case kSelectedElevationParamId: source.elevationDeg = static_cast<float>(std::clamp(value, -90.0, 90.0)); break;
    case kSelectedDistanceParamId: source.distance = static_cast<float>(std::clamp(value, 0.1, 3.0)); break;
    case kSelectedGainParamId: source.gainDb = static_cast<float>(std::clamp(value, -60.0, 24.0)); break;
    default: break;
    }
    p.panner.setSource(index, source);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id >= kSourceParamBase && id < kSourceParamBase + s3g::kLayoutPannerSources * kSourceParamStride) {
        const uint32_t sourceIndex = static_cast<uint32_t>((id - kSourceParamBase) / kSourceParamStride);
        const uint32_t offset = static_cast<uint32_t>((id - kSourceParamBase) % kSourceParamStride);
        auto source = p.panner.source(sourceIndex);
        switch (offset) {
        case kSourceAzOffset: source.azimuthDeg = static_cast<float>(std::clamp(value, -180.0, 180.0)); break;
        case kSourceElOffset: source.elevationDeg = static_cast<float>(std::clamp(value, -90.0, 90.0)); break;
        case kSourceDistanceOffset: source.distance = static_cast<float>(std::clamp(value, 0.1, 3.0)); break;
        case kSourceGainOffset: source.gainDb = static_cast<float>(std::clamp(value, -60.0, 24.0)); break;
        case kSourceMuteOffset: source.muted = value >= 0.5; break;
        case kSourceSoloOffset: source.solo = value >= 0.5; break;
        default: break;
        }
        p.panner.setSource(sourceIndex, source);
        p.params.selectedSource = sourceIndex;
        p.panner.setParams(p.params);
        p.params = p.panner.params();
        return;
    }

    switch (id) {
    case kLayoutParamId: p.params.layout = static_cast<s3g::LayoutPannerPreset>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, kLayoutCount - 1u)); break;
    case kMethodParamId: p.params.method = static_cast<s3g::LayoutPannerMethod>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 1u)); break;
    case kFocusParamId: p.params.focus = static_cast<float>(std::clamp(value, 0.25, 4.0)); break;
    case kRolloffParamId: p.params.distanceRolloffDb = static_cast<float>(std::clamp(value, 0.0, 48.0)); break;
    case kSmoothingParamId: p.params.smoothingMs = static_cast<float>(std::clamp(value, 1.0, 250.0)); break;
    case kGlobalAzimuthParamId: p.params.globalAzimuthDeg = static_cast<float>(std::clamp(value, -180.0, 180.0)); break;
    case kGlobalElevationParamId: p.params.globalElevationDeg = static_cast<float>(std::clamp(value, -90.0, 90.0)); break;
    case kGlobalDistanceParamId: p.params.globalDistanceOffset = static_cast<float>(std::clamp(value, -3.0, 3.0)); break;
    case kDiffusionParamId: p.params.distanceDiffusion = static_cast<float>(std::clamp(value, 0.0, 1.0)); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    case kSelectedSourceParamId: p.params.selectedSource = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kLayoutPannerSources) - 1u; break;
    case kSelectedAzimuthParamId:
    case kSelectedElevationParamId:
    case kSelectedDistanceParamId:
    case kSelectedGainParamId:
        setSelectedSourceValue(p, id, value);
        break;
    default: break;
    }
    p.panner.setParams(p.params);
    p.params = p.panner.params();
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
    p->panner.prepare(sampleRate);
    p->panner.setParams(p->params);
    p->params = p->panner.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->outputPeak.store(0.0f, std::memory_order_relaxed); }

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
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& input = proc->audio_inputs[0];
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kInputChannels);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);

    if (output.data32) {
        s3g::clearAudioBufferFromChannel(output, 0, frames);
    }
    if (!input.data32 || !output.data32 || outChannels == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    std::array<float, kInputChannels> frameIn {};
    std::array<float, kOutputChannels> frameOut {};
    p->panner.setParams(p->params);
    p->params = p->panner.params();
    float blockPeak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        for (uint32_t ch = 0; ch < inChannels; ++ch) {
            frameIn[ch] = input.data32[ch] ? input.data32[ch][i] : 0.0f;
        }
        p->panner.processFrame(frameIn.data(), frameOut.data(), inChannels);
        for (uint32_t ch = 0; ch < outChannels; ++ch) {
            if (output.data32[ch]) {
                output.data32[ch][i] = frameOut[ch];
                blockPeak = std::max(blockPeak, std::fabs(frameOut[ch]));
            }
        }
    }
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, blockPeak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "16 Source In" : "64 Speaker Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };

constexpr uint32_t kBaseParamCount = 15;
uint32_t paramsCount(const clap_plugin_t*) { return kBaseParamCount + s3g::kLayoutPannerSources * kSourceParamStride; }

bool fillBaseParam(uint32_t index, ParamDef& def)
{
    static constexpr ParamDef defs[kBaseParamCount] {
        { kLayoutParamId, "Layout", 0.0, static_cast<double>(kLayoutCount - 1u), static_cast<double>(static_cast<uint32_t>(s3g::LayoutPannerPreset::Dome24NoOverhead)), true },
        { kMethodParamId, "Method", 0.0, 1.0, 0.0, true },
        { kFocusParamId, "Focus", 0.25, 4.0, 1.0, false },
        { kRolloffParamId, "Distance Rolloff", 0.0, 48.0, 6.0, false },
        { kSmoothingParamId, "Smoothing", 1.0, 250.0, 35.0, false },
        { kGlobalAzimuthParamId, "Global Azimuth", -180.0, 180.0, 0.0, false },
        { kGlobalElevationParamId, "Global Elevation", -90.0, 90.0, 0.0, false },
        { kGlobalDistanceParamId, "Global Distance", -3.0, 3.0, 0.0, false },
        { kDiffusionParamId, "Distance Diffusion", 0.0, 1.0, 0.35, false },
        { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
        { kSelectedSourceParamId, "Selected Source", 1.0, static_cast<double>(s3g::kLayoutPannerSources), 1.0, true },
        { kSelectedAzimuthParamId, "Selected Azimuth", -180.0, 180.0, -30.0, false },
        { kSelectedElevationParamId, "Selected Elevation", -90.0, 90.0, 0.0, false },
        { kSelectedDistanceParamId, "Selected Distance", 0.1, 3.0, 1.0, false },
        { kSelectedGainParamId, "Selected Gain", -60.0, 24.0, 0.0, false },
    };
    if (index >= kBaseParamCount) return false;
    def = defs[index];
    return true;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    ParamDef def {};
    if (!fillBaseParam(index, def)) {
        const uint32_t source = (index - kBaseParamCount) / kSourceParamStride;
        const uint32_t offset = (index - kBaseParamCount) % kSourceParamStride;
        static constexpr const char* names[] = { "Azimuth", "Elevation", "Distance", "Gain", "Mute", "Solo" };
        char name[CLAP_NAME_SIZE] {};
        std::snprintf(name, sizeof(name), "S%u %s", source + 1u, names[offset]);
        const bool isSwitch = offset == kSourceMuteOffset || offset == kSourceSoloOffset;
        def = {
            sourceParamId(source, static_cast<SourceParamOffset>(offset)),
            name,
            offset == kSourceAzOffset ? -180.0 : (offset == kSourceElOffset ? -90.0 : (offset == kSourceDistanceOffset ? 0.1 : (offset == kSourceGainOffset ? -60.0 : 0.0))),
            offset == kSourceAzOffset ? 180.0 : (offset == kSourceElOffset ? 90.0 : (offset == kSourceDistanceOffset ? 3.0 : (offset == kSourceGainOffset ? 24.0 : 1.0))),
            offset == kSourceAzOffset ? -30.0 : (offset == kSourceDistanceOffset ? 1.0 : 0.0),
            isSwitch
        };
        info->id = def.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
        std::strncpy(info->name, name, sizeof(info->name));
        std::strncpy(info->module, "Layout Panner Sources", sizeof(info->module));
        info->min_value = def.min;
        info->max_value = def.max;
        info->default_value = def.def;
        return true;
    }
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Layout Panner", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* p = self(plugin);
    const auto params = p->panner.params();
    const auto sources = p->panner.sources();
    if (id >= kSourceParamBase && id < kSourceParamBase + s3g::kLayoutPannerSources * kSourceParamStride) {
        const uint32_t source = static_cast<uint32_t>((id - kSourceParamBase) / kSourceParamStride);
        const uint32_t offset = static_cast<uint32_t>((id - kSourceParamBase) % kSourceParamStride);
        const auto& s = sources[source];
        switch (offset) {
        case kSourceAzOffset: *value = s.azimuthDeg; return true;
        case kSourceElOffset: *value = s.elevationDeg; return true;
        case kSourceDistanceOffset: *value = s.distance; return true;
        case kSourceGainOffset: *value = s.gainDb; return true;
        case kSourceMuteOffset: *value = s.muted ? 1.0 : 0.0; return true;
        case kSourceSoloOffset: *value = s.solo ? 1.0 : 0.0; return true;
        default: return false;
        }
    }
    const auto& selected = sources[params.selectedSource];
    switch (id) {
    case kLayoutParamId: *value = static_cast<double>(static_cast<uint32_t>(params.layout)); return true;
    case kMethodParamId: *value = static_cast<double>(static_cast<uint32_t>(params.method)); return true;
    case kFocusParamId: *value = params.focus; return true;
    case kRolloffParamId: *value = params.distanceRolloffDb; return true;
    case kSmoothingParamId: *value = params.smoothingMs; return true;
    case kGlobalAzimuthParamId: *value = params.globalAzimuthDeg; return true;
    case kGlobalElevationParamId: *value = params.globalElevationDeg; return true;
    case kGlobalDistanceParamId: *value = params.globalDistanceOffset; return true;
    case kDiffusionParamId: *value = params.distanceDiffusion; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kSelectedSourceParamId: *value = params.selectedSource + 1u; return true;
    case kSelectedAzimuthParamId: *value = selected.azimuthDeg; return true;
    case kSelectedElevationParamId: *value = selected.elevationDeg; return true;
    case kSelectedDistanceParamId: *value = selected.distance; return true;
    case kSelectedGainParamId: *value = selected.gainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kLayoutParamId) std::snprintf(display, size, "%s", s3g::layoutPannerPresetName(static_cast<s3g::LayoutPannerPreset>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, kLayoutCount - 1u))));
    else if (id == kMethodParamId) std::snprintf(display, size, "%s", s3g::layoutPannerMethodName(static_cast<s3g::LayoutPannerMethod>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 1u))));
    else if (id == kSelectedSourceParamId) std::snprintf(display, size, "S%.0f", value);
    else if (id == kOutputParamId || id == kSelectedGainParamId || (id >= kSourceParamBase && ((id - kSourceParamBase) % kSourceParamStride) == kSourceGainOffset)) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kGlobalAzimuthParamId || id == kGlobalElevationParamId || id == kSelectedAzimuthParamId || id == kSelectedElevationParamId || (id >= kSourceParamBase && ((id - kSourceParamBase) % kSourceParamStride <= kSourceElOffset))) std::snprintf(display, size, "%+.1f deg", value);
    else if (id >= kSourceParamBase && (((id - kSourceParamBase) % kSourceParamStride) == kSourceMuteOffset || ((id - kSourceParamBase) % kSourceParamStride) == kSourceSoloOffset)) std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
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
    SavedState s {};
    s.params = p->panner.params();
    s.sources = p->panner.sources();
    s.speakers = p->panner.speakers();
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = s.params;
    p->panner.setParams(p->params);
    for (uint32_t i = 0; i < s3g::kLayoutPannerSources; ++i) p->panner.setSource(i, s.sources[i]);
    if (s.params.layout == s3g::LayoutPannerPreset::Custom) {
        p->panner.setSpeakers(s.speakers, s.params.activeSpeakers);
    }
    p->params = p->panner.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GLayoutPannerView : NSView {
    void* _plugin;
    NSTimer* _timer;
    int _page;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    BOOL _dragSource;
    BOOL _dragSpeaker;
    NSPoint _lastDragPoint;
    int _dragSlider;
    int _dragMixerSource;
    BOOL _dragMixerOutput;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSPoint _menuOrigin;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawOpenMenu:(NSDictionary*)attrs;
- (void)updateMenuHover:(NSPoint)point;
- (NSRect)pageButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (void)setViewPreset:(int)mode;
- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect depth:(CGFloat*)depth;
- (s3g::Vec3)worldFromPoint:(NSPoint)point rect:(NSRect)rect previous:(s3g::Vec3)previous;
- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawMixer:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (NSRect)designButtonRect:(int)index;
- (void)drawDesignButton:(NSString*)label index:(int)index attrs:(NSDictionary*)attrs;
- (void)copyCurrentLayoutToCustom;
- (void)loadLayoutJson;
- (void)saveLayoutJson;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* lpColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static s3g::LayoutPannerCustomShape lpShapeFromString(NSString* value)
{
    if (!value) return s3g::LayoutPannerCustomShape::Auto;
    NSString* upper = [value uppercaseString];
    if ([upper isEqualToString:@"RING"]) return s3g::LayoutPannerCustomShape::Ring;
    if ([upper isEqualToString:@"DOME"]) return s3g::LayoutPannerCustomShape::Dome;
    if ([upper isEqualToString:@"TETRA"]) return s3g::LayoutPannerCustomShape::Tetra;
    if ([upper isEqualToString:@"OCTA"]) return s3g::LayoutPannerCustomShape::Octa;
    if ([upper isEqualToString:@"CUBE"]) return s3g::LayoutPannerCustomShape::Cube;
    if ([upper isEqualToString:@"ICO"]) return s3g::LayoutPannerCustomShape::Icosa;
    if ([upper isEqualToString:@"ICOSA"]) return s3g::LayoutPannerCustomShape::Icosa;
    if ([upper isEqualToString:@"DODECA"]) return s3g::LayoutPannerCustomShape::Dodeca;
    if ([upper isEqualToString:@"GEO"]) return s3g::LayoutPannerCustomShape::Geo;
    if ([upper isEqualToString:@"STACK"]) return s3g::LayoutPannerCustomShape::Stack;
    return s3g::LayoutPannerCustomShape::Auto;
}

constexpr uint32_t kDesignShapeMenuCount = 5;

static s3g::LayoutPannerCustomShape lpDesignShapeForMenuIndex(uint32_t index)
{
    static constexpr s3g::LayoutPannerCustomShape shapes[kDesignShapeMenuCount] {
        s3g::LayoutPannerCustomShape::Auto,
        s3g::LayoutPannerCustomShape::Ring,
        s3g::LayoutPannerCustomShape::Dome,
        s3g::LayoutPannerCustomShape::Geo,
        s3g::LayoutPannerCustomShape::Stack,
    };
    return shapes[std::min<uint32_t>(index, kDesignShapeMenuCount - 1u)];
}

static float lpLinearToSrgb(float v)
{
    const float x = std::clamp(v, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static NSColor* lpAedColor(float azDeg, float elDeg, float distance, bool selected)
{
    const float hue = std::fmod((azDeg / 360.0f) + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(elDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.30f, 0.86f);
    const float chroma = std::clamp(distance / 2.6f, 0.08f, 1.0f) * 0.36f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float r = lpLinearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = lpLinearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float bl = lpLinearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.06f : 0.18f;
    r = r * (1.0f - grayMix) + 0.74f * grayMix;
    g = g * (1.0f - grayMix) + 0.74f * grayMix;
    bl = bl * (1.0f - grayMix) + 0.74f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:selected ? 1.0 : 0.88];
}

@implementation S3GLayoutPannerView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 620)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _page = 0;
        _viewMode = 2;
        _viewAzDeg = 35.0;
        _viewElDeg = 34.0;
        _viewZoom = 1.0;
        _dragView = NO;
        _dragSource = NO;
        _dragSpeaker = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _dragSlider = -1;
        _dragMixerSource = -1;
        _dragMixerOutput = NO;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _menuOrigin = NSMakePoint(0, 0);
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
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/20.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 642, 738, 826, 82);
}
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, attrs, style, 642, 738, 102);
}
- (void)drawOpenMenu:(NSDictionary*)attrs
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    static NSString* layoutItems[] = { @"CUSTOM", @"CUBE 8", @"CUBE 17", @"DODECA 12", @"DOME 24", @"DOME 25", @"DBL RING 16", @"DBL RING 20", @"OCTO RING", @"QUAD", @"QUAD+OH", @"RING 12", @"RING 16" };
    static NSString* methodItems[] = { @"DIST", @"COS" };
    static NSString* shapeItems[] = { @"AUTO", @"RING", @"DOME", @"GEO", @"STACK" };
    NSString** items = _openMenu == 1 ? layoutItems : (_openMenu == 3 ? shapeItems : methodItems);
    const CGFloat itemH = 18.0;
    const CGFloat w = _openMenu == 1 ? 148.0 : (_openMenu == 3 ? 92.0 : 78.0);
    NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, w, itemH * static_cast<CGFloat>(_menuItemCount));
    int selected = 0;
    if (_plugin) {
        auto* p = static_cast<Plugin*>(_plugin);
        const auto params = p->panner.params();
        if (_openMenu == 1) selected = static_cast<int>(static_cast<uint32_t>(params.layout));
        else if (_openMenu == 2) selected = static_cast<int>(static_cast<uint32_t>(params.method));
        else if (_openMenu == 3) selected = static_cast<int>(static_cast<uint32_t>(params.customShape));
    }
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawDropdownMenu(menuRect, itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    const CGFloat itemH = 18.0;
    const CGFloat w = _openMenu == 1 ? 148.0 : (_openMenu == 3 ? 92.0 : 78.0);
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, w, itemH * static_cast<CGFloat>(_menuItemCount));
    const int next = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (NSRect)pageButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 56.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(rect.origin.x + 132.0 + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 4.0, w, h);
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
    return NSMakeRect(viewStart - 12.0 - (2.0 - static_cast<CGFloat>(index)) * w - (1.0 - static_cast<CGFloat>(index)) * gap, rect.origin.y + 4.0, w, h);
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
- (CGFloat)viewScaleForRect:(NSRect)rect
{
    CGFloat layoutScale = 1.0;
    if (_plugin && _viewMode != 0 && _viewMode != 1) {
        auto* p = static_cast<Plugin*>(_plugin);
        const auto layout = p->panner.params().layout;
        if (layout == s3g::LayoutPannerPreset::Cube8 || layout == s3g::LayoutPannerPreset::Cube17) {
            layoutScale = 0.82;
        }
    }
    return std::min(rect.size.width, rect.size.height) * 0.34 * layoutScale * std::clamp(_viewZoom, 0.55, 2.20);
}
- (NSPoint)projectWorldPoint:(s3g::Vec3)p rect:(NSRect)rect depth:(CGFloat*)depth
{
    const CGFloat cx = rect.origin.x + rect.size.width * 0.50;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 + se * p.z;
    const float z2 = -se * y1 + ce * p.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return NSMakePoint(cx + static_cast<CGFloat>(x1) * scale, cy - static_cast<CGFloat>(y2) * scale);
}
- (s3g::Vec3)worldFromPoint:(NSPoint)point rect:(NSRect)rect previous:(s3g::Vec3)previous
{
    const CGFloat cx = rect.origin.x + rect.size.width * 0.50;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    const float sx = static_cast<float>((point.x - cx) / scale);
    const float sy = static_cast<float>((cy - point.y) / scale);
    CGFloat depth = 0.0;
    (void)[self projectWorldPoint:previous rect:rect depth:&depth];
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az);
    const float sa = std::sin(az);
    const float ce = std::cos(el);
    const float se = std::sin(el);
    const float y1 = ce * sy - se * static_cast<float>(depth);
    const float z = se * sy + ce * static_cast<float>(depth);
    return {
        ca * sx + sa * y1,
        -sa * sx + ca * y1,
        z
    };
}
- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto params = p->panner.params();
    const auto speakers = p->panner.speakers();
    const auto sources = p->panner.sources();
    const uint32_t n = p->panner.activeSpeakers();
    [lpColor(0x111111) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    std::array<NSPoint, s3g::kLayoutPannerMaxSpeakers> spPts {};
    std::array<s3g::Vec3, s3g::kLayoutPannerMaxSpeakers> spWorld {};
    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const auto dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        spWorld[i] = { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        spPts[i] = [self projectWorldPoint:spWorld[i] rect:rect depth:nil];
    }
    [lpColor(0x6e6e6e, 0.68) setStroke];
    NSBezierPath* links = [NSBezierPath bezierPath];
    auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= n || b >= n) return;
        [links moveToPoint:spPts[a]];
        [links lineToPoint:spPts[b]];
    };
    auto ring = [&](uint32_t base, uint32_t count) {
        if (count < 2u || base >= n) return;
        const uint32_t end = std::min<uint32_t>(n, base + count);
        for (uint32_t i = base; i < end; ++i) edge(i, i + 1u < end ? i + 1u : base);
    };
    auto meshEqualDistanceEdges = [&](uint32_t count) {
        if (count < 3u || count > n) return;
        float minD2 = 999999.0f;
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1u; b < count; ++b) {
                const float dx = spWorld[a].x - spWorld[b].x;
                const float dy = spWorld[a].y - spWorld[b].y;
                const float dz = spWorld[a].z - spWorld[b].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.0001f) minD2 = std::min(minD2, d2);
            }
        }
        const float maxD2 = minD2 * 1.08f;
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1u; b < count; ++b) {
                const float dx = spWorld[a].x - spWorld[b].x;
                const float dy = spWorld[a].y - spWorld[b].y;
                const float dz = spWorld[a].z - spWorld[b].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 <= maxD2) edge(a, b);
            }
        }
    };
    auto drawLimitedDistanceMesh = [&](uint32_t count, uint32_t maxDegree) {
        if (count < 3u || count > n) return;
        struct Candidate {
            uint32_t a;
            uint32_t b;
            float d2;
        };
        std::array<Candidate, s3g::kLayoutPannerMaxSpeakers * s3g::kLayoutPannerMaxSpeakers> candidates {};
        uint32_t candidateCount = 0;
        for (uint32_t a = 0; a < count; ++a) {
            for (uint32_t b = a + 1u; b < count; ++b) {
                const float dx = spWorld[a].x - spWorld[b].x;
                const float dy = spWorld[a].y - spWorld[b].y;
                const float dz = spWorld[a].z - spWorld[b].z;
                candidates[candidateCount++] = { a, b, dx * dx + dy * dy + dz * dz };
            }
        }
        std::sort(candidates.begin(), candidates.begin() + candidateCount,
            [](const Candidate& lhs, const Candidate& rhs) { return lhs.d2 < rhs.d2; });
        std::array<uint32_t, s3g::kLayoutPannerMaxSpeakers> degree {};
        const uint32_t edgeLimit = std::min<uint32_t>(candidateCount, std::max<uint32_t>(count, (count * maxDegree) / 2u));
        uint32_t edges = 0;
        for (uint32_t i = 0; i < candidateCount && edges < edgeLimit; ++i) {
            const auto& c = candidates[i];
            if (degree[c.a] >= maxDegree || degree[c.b] >= maxDegree) continue;
            edge(c.a, c.b);
            ++degree[c.a];
            ++degree[c.b];
            ++edges;
        }
    };
    auto drawHemisphereBands = [&]() {
        if (n <= 4u) {
            ring(0, n);
            return;
        }
        std::array<uint32_t, s3g::kLayoutPannerMaxSpeakers> order {};
        for (uint32_t i = 0; i < n; ++i) order[i] = i;
        std::sort(order.begin(), order.begin() + n, [&](uint32_t a, uint32_t b) {
            return speakers[a].elevationDeg < speakers[b].elevationDeg;
        });
        const uint32_t bandCount = n <= 8u ? 2u : (n <= 20u ? 3u : 4u);
        std::array<uint32_t, 5> starts {};
        for (uint32_t b = 0; b <= bandCount; ++b) {
            starts[b] = (n * b) / bandCount;
        }
        for (uint32_t b = 0; b < bandCount; ++b) {
            const uint32_t start = starts[b];
            const uint32_t end = starts[b + 1u];
            const uint32_t count = end - start;
            if (count < 2u) continue;
            std::sort(order.begin() + start, order.begin() + end, [&](uint32_t a, uint32_t bIndex) {
                return speakers[a].azimuthDeg < speakers[bIndex].azimuthDeg;
            });
            for (uint32_t i = start; i < end; ++i) {
                edge(order[i], order[i + 1u < end ? i + 1u : start]);
            }
        }
        for (uint32_t b = 0; b + 1u < bandCount; ++b) {
            const uint32_t lowerStart = starts[b];
            const uint32_t lowerEnd = starts[b + 1u];
            const uint32_t upperStart = starts[b + 1u];
            const uint32_t upperEnd = starts[b + 2u];
            for (uint32_t ui = upperStart; ui < upperEnd; ++ui) {
                uint32_t best = order[lowerStart];
                float bestD = 999999.0f;
                for (uint32_t li = lowerStart; li < lowerEnd; ++li) {
                    const float da = std::fabs(s3g::layoutPannerWrapDeg(speakers[order[ui]].azimuthDeg - speakers[order[li]].azimuthDeg));
                    if (da < bestD) {
                        bestD = da;
                        best = order[li];
                    }
                }
                edge(order[ui], best);
            }
        }
    };
    auto resolvedCustomShape = [&]() {
        if (params.customShape != s3g::LayoutPannerCustomShape::Auto) return params.customShape;
        if (n == 4u) return s3g::LayoutPannerCustomShape::Tetra;
        if (n == 6u) return s3g::LayoutPannerCustomShape::Octa;
        if (n == 8u) return s3g::LayoutPannerCustomShape::Cube;
        if (n == 12u) return s3g::LayoutPannerCustomShape::Icosa;
        if (n == 20u) return s3g::LayoutPannerCustomShape::Dodeca;
        if (n <= 16u) return s3g::LayoutPannerCustomShape::Geo;
        return s3g::LayoutPannerCustomShape::Dome;
    };
    auto drawCustomMesh = [&]() {
        const auto shape = resolvedCustomShape();
        if (shape == s3g::LayoutPannerCustomShape::Dome) {
            drawHemisphereBands();
        } else if (shape == s3g::LayoutPannerCustomShape::Ring) {
            ring(0, n);
        } else if (shape == s3g::LayoutPannerCustomShape::Stack) {
            const uint32_t lower = (n + 1u) / 2u;
            ring(0, lower);
            if (n > lower) {
                ring(lower, n - lower);
                for (uint32_t i = lower; i < n; ++i) edge(i, (i - lower) % lower);
            }
        } else if (shape == s3g::LayoutPannerCustomShape::Cube) {
            ring(0, 4); ring(4, 4); for (uint32_t i = 0; i < 4; ++i) edge(i, i + 4u);
        } else if (shape == s3g::LayoutPannerCustomShape::Tetra
            || shape == s3g::LayoutPannerCustomShape::Octa
            || shape == s3g::LayoutPannerCustomShape::Icosa
            || shape == s3g::LayoutPannerCustomShape::Dodeca) {
            meshEqualDistanceEdges(n);
        } else if (n == 4u || n == 6u) {
            drawLimitedDistanceMesh(n, 3);
        } else {
            drawLimitedDistanceMesh(n, n <= 16u ? 3u : 4u);
        }
    };
    switch (params.layout) {
    case s3g::LayoutPannerPreset::Custom:
        drawCustomMesh();
        break;
    case s3g::LayoutPannerPreset::Cube8:
        ring(0, 4); ring(4, 4); for (uint32_t i = 0; i < 4; ++i) edge(i, i + 4u); break;
    case s3g::LayoutPannerPreset::Cube17:
        ring(0, 4);
        edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 8);
        edge(8, 9); edge(9, 10); edge(10, 11); edge(11, 4);
        ring(12, 4);
        edge(0, 4); edge(1, 6); edge(2, 8); edge(3, 10);
        edge(4, 12); edge(6, 13); edge(8, 14); edge(10, 15);
        edge(12, 16); edge(13, 16); edge(14, 16); edge(15, 16);
        break;
    case s3g::LayoutPannerPreset::Dodeca12:
        meshEqualDistanceEdges(12);
        break;
    case s3g::LayoutPannerPreset::Dome24NoOverhead:
    case s3g::LayoutPannerPreset::Dome25:
        ring(0, 12); ring(12, 8); ring(20, 4);
        for (uint32_t i = 0; i < 8; ++i) {
            const uint32_t lowerA = (i * 3u) / 2u;
            const uint32_t lowerB = (lowerA + 1u) % 12u;
            edge(12u + i, lowerA);
            edge(12u + i, lowerB);
        }
        for (uint32_t i = 0; i < 4; ++i) {
            edge(20u + i, 12u + i * 2u);
            edge(20u + i, 12u + ((i * 2u + 1u) % 8u));
        }
        if (params.layout == s3g::LayoutPannerPreset::Dome25) {
            for (uint32_t i = 0; i < 4; ++i) edge(24, 20u + i);
        }
        break;
    case s3g::LayoutPannerPreset::DoubleRing16:
        ring(0, 8); ring(8, 8); for (uint32_t i = 0; i < 8; ++i) edge(i, i + 8u); break;
    case s3g::LayoutPannerPreset::DoubleRing20:
        ring(0, 12); ring(12, 8); for (uint32_t i = 0; i < 8; ++i) edge(12u + i, (i * 3u / 2u) % 12u); break;
    case s3g::LayoutPannerPreset::OctophonicRing: ring(0, 8); break;
    case s3g::LayoutPannerPreset::Quad: ring(0, 4); break;
    case s3g::LayoutPannerPreset::QuadOverhead6:
        ring(0, 4); edge(4, 0); edge(4, 3); edge(5, 1); edge(5, 2); edge(4, 5); break;
    case s3g::LayoutPannerPreset::Ring12: ring(0, 12); break;
    case s3g::LayoutPannerPreset::Ring16: ring(0, 16); break;
    default: ring(0, n); break;
    }
    [links setLineWidth:1.0];
    [links stroke];

    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const bool selectedSpeaker = _page == 2 && i == params.selectedSpeaker;
        [lpColor(0x8a8a8a, 0.82) setFill];
        NSRectFill(NSMakeRect(spPts[i].x - 4.2, spPts[i].y - 4.2, 8.4, 8.4));
        [lpColor(0x050505, 0.9) setStroke];
        NSFrameRect(NSMakeRect(spPts[i].x - 4.2, spPts[i].y - 4.2, 8.4, 8.4));
        if (selectedSpeaker) {
            [lpColor(0xf2f2f2) setStroke];
            NSFrameRect(NSMakeRect(spPts[i].x - 8.0, spPts[i].y - 8.0, 16.0, 16.0));
        }
        if (_page == 2) {
            NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
            NSDictionary* idAttrs = @{ NSForegroundColorAttributeName:selectedSpeaker ? lpColor(0xf4f4f4) : lpColor(0x151515),
                                       NSFontAttributeName:[NSFont fontWithName:@"Menlo-Bold" size:7.0] ?: [NSFont monospacedSystemFontOfSize:7.0 weight:NSFontWeightBold] };
            NSSize size = [label sizeWithAttributes:idAttrs];
            [label drawAtPoint:NSMakePoint(spPts[i].x - size.width * 0.5, spPts[i].y - size.height * 0.5 - 0.5) withAttributes:idAttrs];
        }
    }

    if (_page != 2) for (uint32_t i = 0; i < s3g::kLayoutPannerSources; ++i) {
        const auto& s = sources[i];
        if (s.muted) continue;
        NSPoint pt = [self projectWorldPoint:s3g::layoutPannerSourcePosition(s) rect:rect depth:nil];
        const bool selected = i == params.selectedSource;
        const CGFloat r = selected ? 8.5 : 7.0;
        [lpAedColor(s.azimuthDeg, s.elevationDeg, s.distance, selected) setFill];
        NSRectFill(NSMakeRect(pt.x - r, pt.y - r, r * 2.0, r * 2.0));
        [lpColor(s.muted ? 0x222222 : 0x050505, 0.92) setStroke];
        NSFrameRect(NSMakeRect(pt.x - r, pt.y - r, r * 2.0, r * 2.0));
        if (selected) {
            [lpColor(0xf2f2f2) setStroke];
            NSFrameRect(NSMakeRect(pt.x - 12.0, pt.y - 12.0, 24.0, 24.0));
        }
        NSString* label = [NSString stringWithFormat:@"S%u", i + 1u];
        NSDictionary* idAttrs = @{ NSForegroundColorAttributeName:selected ? lpColor(0xf4f4f4) : lpColor(0x151515),
                                   NSFontAttributeName:[NSFont fontWithName:@"Menlo-Bold" size:7.5] ?: [NSFont monospacedSystemFontOfSize:7.5 weight:NSFontWeightBold] };
        NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(pt.x - labelSize.width * 0.5, pt.y - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
    }
    NSString* viewText = _viewMode == 0 ? @"TOP VIEW   drag sources / blank drag rotates"
        : (_viewMode == 1 ? @"SIDE VIEW   source drag edits height" : @"3/4 VIEW   click source to select");
    if (_page == 2) {
        viewText = _viewMode == 0 ? @"TOP VIEW   drag speakers / blank drag rotates"
            : (_viewMode == 1 ? @"SIDE VIEW   speaker drag edits height" : @"3/4 VIEW   click speaker to select");
    }
    [viewText drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + rect.size.height - 23) withAttributes:attrs];
}
- (NSRect)mixerSourceRect:(uint32_t)index inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 13.0 + static_cast<CGFloat>(index) * laneW;
    return NSMakeRect(x + 10.0, rect.origin.y + 118.0, 12.0, rect.size.height - 212.0);
}
- (NSRect)mixerMuteRect:(uint32_t)index inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 13.0 + static_cast<CGFloat>(index) * laneW;
    return NSMakeRect(x + 1.0, NSMaxY(rect) - 58.0, 14.0, 14.0);
}
- (NSRect)mixerSoloRect:(uint32_t)index inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 13.0 + static_cast<CGFloat>(index) * laneW;
    return NSMakeRect(x + 17.0, NSMaxY(rect) - 58.0, 14.0, 14.0);
}
- (NSRect)mixerOutputTrackRect:(NSRect)rect
{
    return NSMakeRect(rect.origin.x + 88.0, rect.origin.y + 42.0, rect.size.width - 178.0, 9.0);
}
- (void)drawMixer:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto params = p->panner.params();
    const auto sources = p->panner.sources();
    s3g::clap_gui::drawSlider(@"OUT",
                               [NSString stringWithFormat:@"%+.1f", params.outputGainDb],
                               (params.outputGainDb + 60.0f) / 72.0f,
                               rect.origin.y + 36.0,
                               attrs, attrs, style,
                               rect.origin.x + 18.0, rect.origin.x + 88.0,
                               rect.origin.x + rect.size.width - 70.0,
                               rect.size.width - 178.0);
    bool anySolo = false;
    for (const auto& source : sources) anySolo = anySolo || source.solo;
    for (uint32_t i = 0; i < s3g::kLayoutPannerSources; ++i) {
        const auto& s = sources[i];
        const bool selected = i == params.selectedSource;
        const bool audible = !s.muted && (!anySolo || s.solo);
        const CGFloat laneW = 34.0;
        const CGFloat laneX = rect.origin.x + 13.0 + static_cast<CGFloat>(i) * laneW;
        if (selected) {
            [lpColor(0x242424) setFill];
            NSRectFill(NSMakeRect(laneX, rect.origin.y + 92.0, laneW - 2.0, rect.size.height - 108.0));
            [lpColor(0x777777) setStroke];
            NSFrameRect(NSMakeRect(laneX, rect.origin.y + 92.0, laneW - 2.0, rect.size.height - 108.0));
        }
        NSString* label = [NSString stringWithFormat:@"S%u", i + 1u];
        NSSize labelSize = [label sizeWithAttributes:attrs];
        [label drawAtPoint:NSMakePoint(laneX + (laneW - labelSize.width) * 0.5 - 1.0, rect.origin.y + 96.0) withAttributes:attrs];
        NSRect slot = [self mixerSourceRect:i inRect:rect];
        [lpColor(audible ? 0x181818 : 0x0d0d0d) setFill];
        NSRectFill(slot);
        [lpColor(audible ? 0x545454 : 0x333333) setStroke];
        NSFrameRect(slot);
        const CGFloat norm = std::clamp<CGFloat>((s.gainDb + 60.0f) / 84.0f, 0.0, 1.0);
        NSRect fill = NSInsetRect(slot, 2.0, 2.0);
        const CGFloat fullH = fill.size.height;
        fill.origin.y += fullH * (1.0 - norm);
        fill.size.height = std::max<CGFloat>(1.0, fullH * norm);
        [lpAedColor(s.azimuthDeg, s.elevationDeg, s.distance, selected) setFill];
        NSRectFill(fill);
        [lpColor(selected ? 0xf2f2f2 : 0x9a9a9a) setFill];
        NSRectFill(NSMakeRect(slot.origin.x - 2.0, slot.origin.y + slot.size.height * (1.0 - norm) - 1.0, slot.size.width + 4.0, 3.0));
        NSRect mute = [self mixerMuteRect:i inRect:rect];
        [lpColor(s.muted ? 0x3a3a3a : 0x151515) setFill]; NSRectFill(mute);
        [lpColor(s.muted ? 0xd1d1d1 : 0x5a5a5a) setStroke]; NSFrameRect(mute);
        [@"M" drawAtPoint:NSMakePoint(mute.origin.x + 3.0, mute.origin.y + 2.0) withAttributes:attrs];
        NSRect solo = [self mixerSoloRect:i inRect:rect];
        [lpColor(s.solo ? 0xd1d1d1 : 0x151515) setFill]; NSRectFill(solo);
        [lpColor(s.solo ? 0xf2f2f2 : 0x5a5a5a) setStroke]; NSFrameRect(solo);
        NSDictionary* soloAttrs = s.solo ? @{ NSForegroundColorAttributeName:lpColor(0x111111), NSFontAttributeName:[attrs objectForKey:NSFontAttributeName] } : attrs;
        [@"S" drawAtPoint:NSMakePoint(solo.origin.x + 3.0, solo.origin.y + 2.0) withAttributes:soloAttrs];
        NSString* gainText = [NSString stringWithFormat:@"%+.0f", s.gainDb];
        NSSize gainSize = [gainText sizeWithAttributes:attrs];
        [gainText drawAtPoint:NSMakePoint(laneX + (laneW - gainSize.width) * 0.5 - 1.0, NSMaxY(slot) + 8.0) withAttributes:attrs];
    }
}
- (NSRect)designButtonRect:(int)index
{
    return NSMakeRect(642.0 + static_cast<CGFloat>(index) * 62.0, 518.0, 56.0, 18.0);
}
- (void)drawDesignButton:(NSString*)label index:(int)index attrs:(NSDictionary*)attrs
{
    NSRect r = [self designButtonRect:index];
    [lpColor(0x151515) setFill]; NSRectFill(r);
    [lpColor(0x666666) setStroke]; NSFrameRect(r);
    NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(r.origin.x + (r.size.width - size.width) * 0.5,
                                   r.origin.y + (r.size.height - size.height) * 0.5 - 0.5)
        withAttributes:attrs];
}
- (void)copyCurrentLayoutToCustom
{
    auto* p = static_cast<Plugin*>(_plugin);
    p->panner.copyCurrentLayoutToCustom();
    p->params = p->panner.params();
    [self setNeedsDisplay:YES];
}
- (NSDictionary*)layoutJsonDictionary
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto params = p->panner.params();
    const auto speakers = p->panner.speakers();
    NSMutableArray* list = [NSMutableArray arrayWithCapacity:params.activeSpeakers];
    for (uint32_t i = 0; i < params.activeSpeakers; ++i) {
        const auto& sp = speakers[i];
        [list addObject:@{
            @"azimuth": @(sp.azimuthDeg),
            @"elevation": @(sp.elevationDeg),
            @"distance": @(sp.distance)
        }];
    }
    return @{
        @"format": @"s3g-layout-panner-speakers-v1",
        @"shape": [NSString stringWithUTF8String:s3g::layoutPannerCustomShapeName(params.customShape)],
        @"speaker_count": @(params.activeSpeakers),
        @"speakers": list
    };
}
- (void)loadLayoutJson
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"json"]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    NSData* data = [NSData dataWithContentsOfURL:[panel URL]];
    if (!data) return;
    NSError* error = nil;
    id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if (error || ![root isKindOfClass:[NSDictionary class]]) return;
    NSDictionary* dict = static_cast<NSDictionary*>(root);
    NSArray* list = [dict objectForKey:@"speakers"];
    if (![list isKindOfClass:[NSArray class]]) return;
    if ([list count] < 2) return;
    uint32_t count = static_cast<uint32_t>(std::clamp<NSInteger>([list count], 2, s3g::kLayoutPannerMaxSpeakers));
    NSNumber* countValue = [dict objectForKey:@"speaker_count"];
    if ([countValue respondsToSelector:@selector(unsignedIntValue)]) {
        count = std::clamp<uint32_t>([countValue unsignedIntValue], 2u, s3g::kLayoutPannerMaxSpeakers);
    }
    count = std::min<uint32_t>(count, static_cast<uint32_t>([list count]));
    std::array<s3g::LayoutPannerSpeaker, s3g::kLayoutPannerMaxSpeakers> speakers {};
    for (uint32_t i = 0; i < count; ++i) {
        id item = [list objectAtIndex:i];
        if (![item isKindOfClass:[NSDictionary class]]) continue;
        NSDictionary* sp = static_cast<NSDictionary*>(item);
        speakers[i].azimuthDeg = [[sp objectForKey:@"azimuth"] respondsToSelector:@selector(floatValue)] ? [[sp objectForKey:@"azimuth"] floatValue] : 0.0f;
        speakers[i].elevationDeg = [[sp objectForKey:@"elevation"] respondsToSelector:@selector(floatValue)] ? [[sp objectForKey:@"elevation"] floatValue] : 0.0f;
        speakers[i].distance = [[sp objectForKey:@"distance"] respondsToSelector:@selector(floatValue)] ? [[sp objectForKey:@"distance"] floatValue] : 1.0f;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    p->panner.setSpeakers(speakers, count);
    id shapeValue = [dict objectForKey:@"shape"];
    if ([shapeValue isKindOfClass:[NSString class]]) {
        p->panner.setCustomShapeMetadata(lpShapeFromString(static_cast<NSString*>(shapeValue)));
    }
    p->params = p->panner.params();
    _page = 2;
    [self setNeedsDisplay:YES];
}
- (void)saveLayoutJson
{
    NSDictionary* dict = [self layoutJsonDictionary];
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:dict options:NSJSONWritingPrettyPrinted error:&error];
    if (error || !data) return;
    NSSavePanel* panel = [NSSavePanel savePanel];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"json"]];
#pragma clang diagnostic pop
    [panel setNameFieldStringValue:@"s3g-layout-speakers.json"];
    if ([panel runModal] != NSModalResponseOK) return;
    [data writeToURL:[panel URL] atomically:YES];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10.5] ?: [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSDictionary* lab = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:mono };
    NSDictionary* titleAttrs = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:titleFont };
    [@"s3g LAYOUT PANNER" drawAtPoint:NSMakePoint(18,14) withAttributes:titleAttrs];
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PK %+4.1f", 20.0 * std::log10(std::max(0.000001f, pk))] drawAtPoint:NSMakePoint(728,14) withAttributes:small];
    [@"16x64" drawAtPoint:NSMakePoint(832,14) withAttributes:small];

    const NSRect mainPanel = NSMakeRect(18, 42, 596, 556);
    const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
    s3g::clap_gui::drawPanelFrame(mainPanel.origin.x, mainPanel.origin.y, mainPanel.size.width, mainPanel.size.height, style);
    NSString* panelTitle = _page == 0 ? @"LAYOUT FIELD" : (_page == 1 ? @"SOURCE MIXER" : @"LAYOUT DESIGN");
    s3g::clap_gui::drawPanelHeader(panelTitle, true, mainPanel.origin.x, mainPanel.origin.y, mainPanel.size.width, 21, lab, style);
    static NSString* pageLabels[] = { @"FIELD", @"MIXER", @"DESIGN" };
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self pageButtonRect:i inRect:mainPanel], mainPanel, pageLabels[i], i == _page, small, style);
    }
    if (_page == 0 || _page == 2) {
        static NSString* zoomLabels[] = { @"-", @"+" };
        for (int i = 0; i < 2; ++i) {
            s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:mainPanel], mainPanel, zoomLabels[i], false, small, style);
        }
        static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4" };
        for (int i = 0; i < 3; ++i) {
            s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:mainPanel], mainPanel, viewLabels[i], i == _viewMode, small, style);
        }
        [self drawField:fieldRect attrs:small style:style];
    } else {
        [self drawMixer:fieldRect attrs:small style:style];
    }

    s3g::clap_gui::drawPanelFrame(630, 42, 250, 270, style);
    s3g::clap_gui::drawPanelHeader(@"PANNER", true, 630, 42, 250, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(630, 324, 250, 274, style);
    s3g::clap_gui::drawPanelHeader(_page == 2 ? @"SPEAKER" : @"SOURCE", true, 630, 324, 250, 21, lab, style);

    const auto params = p->panner.params();
    const auto source = p->panner.source(params.selectedSource);
    [self drawMenu:@"LAYOUT" value:[NSString stringWithUTF8String:s3g::layoutPannerPresetName(params.layout)] y:78 attrs:small style:style];
    if (_page == 2) {
        [self drawMenu:@"SHAPE" value:[NSString stringWithUTF8String:s3g::layoutPannerCustomShapeName(params.customShape)] y:104 attrs:small style:style];
        [self drawSlider:@"COUNT" value:[NSString stringWithFormat:@"%u", params.activeSpeakers] norm:(params.activeSpeakers - 2.0) / 62.0 y:130 attrs:small style:style];
        [self drawDesignButton:@"COPY" index:0 attrs:small];
        [self drawDesignButton:@"LOAD" index:1 attrs:small];
        [self drawDesignButton:@"SAVE" index:2 attrs:small];
        const auto speaker = p->panner.speaker(params.selectedSpeaker);
        const CGFloat speakerNorm = params.activeSpeakers > 1u
            ? static_cast<CGFloat>(params.selectedSpeaker) / static_cast<CGFloat>(params.activeSpeakers - 1u)
            : 0.0;
        [self drawSlider:@"SEL" value:[NSString stringWithFormat:@"%u", params.selectedSpeaker + 1u] norm:speakerNorm y:360 attrs:small style:style];
        [self drawSlider:@"AZ" value:[NSString stringWithFormat:@"%+.0f", speaker.azimuthDeg] norm:(speaker.azimuthDeg + 180.0f) / 360.0f y:386 attrs:small style:style];
        [self drawSlider:@"EL" value:[NSString stringWithFormat:@"%+.0f", speaker.elevationDeg] norm:(speaker.elevationDeg + 90.0f) / 180.0f y:412 attrs:small style:style];
        [self drawSlider:@"DST" value:[NSString stringWithFormat:@"%.2f", speaker.distance] norm:(speaker.distance - 0.1f) / 2.9f y:438 attrs:small style:style];
        [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", params.outputGainDb] norm:(params.outputGainDb + 60.0f) / 72.0f y:490 attrs:small style:style];
    } else {
        [self drawMenu:@"METHOD" value:[NSString stringWithUTF8String:s3g::layoutPannerMethodName(params.method)] y:104 attrs:small style:style];
        [self drawSlider:@"FOC" value:[NSString stringWithFormat:@"%.2f", params.focus] norm:(params.focus - 0.25f) / 3.75f y:130 attrs:small style:style];
        [self drawSlider:@"ROLL" value:[NSString stringWithFormat:@"%.1f", params.distanceRolloffDb] norm:params.distanceRolloffDb / 48.0f y:156 attrs:small style:style];
        [self drawSlider:@"SMTH" value:[NSString stringWithFormat:@"%.0f", params.smoothingMs] norm:(params.smoothingMs - 1.0f) / 249.0f y:182 attrs:small style:style];
        [self drawSlider:@"GAZ" value:[NSString stringWithFormat:@"%+.0f", params.globalAzimuthDeg] norm:(params.globalAzimuthDeg + 180.0f) / 360.0f y:208 attrs:small style:style];
        [self drawSlider:@"GEL" value:[NSString stringWithFormat:@"%+.0f", params.globalElevationDeg] norm:(params.globalElevationDeg + 90.0f) / 180.0f y:234 attrs:small style:style];
        [self drawSlider:@"GDST" value:[NSString stringWithFormat:@"%+.2f", params.globalDistanceOffset] norm:(params.globalDistanceOffset + 3.0f) / 6.0f y:260 attrs:small style:style];
        [self drawSlider:@"DIF" value:[NSString stringWithFormat:@"%.2f", params.distanceDiffusion] norm:params.distanceDiffusion y:286 attrs:small style:style];

        [self drawSlider:@"SEL" value:[NSString stringWithFormat:@"S%u", params.selectedSource + 1u] norm:static_cast<CGFloat>(params.selectedSource) / static_cast<CGFloat>(s3g::kLayoutPannerSources - 1u) y:360 attrs:small style:style];
        [self drawSlider:@"AZ" value:[NSString stringWithFormat:@"%+.0f", source.azimuthDeg] norm:(source.azimuthDeg + 180.0f) / 360.0f y:386 attrs:small style:style];
        [self drawSlider:@"EL" value:[NSString stringWithFormat:@"%+.0f", source.elevationDeg] norm:(source.elevationDeg + 90.0f) / 180.0f y:412 attrs:small style:style];
        [self drawSlider:@"DST" value:[NSString stringWithFormat:@"%.2f", source.distance] norm:(source.distance - 0.1f) / 2.9f y:438 attrs:small style:style];
        [self drawSlider:@"GAIN" value:[NSString stringWithFormat:@"%+.1f", source.gainDb] norm:(source.gainDb + 60.0f) / 84.0f y:464 attrs:small style:style];
        [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", params.outputGainDb] norm:(params.outputGainDb + 60.0f) / 72.0f y:490 attrs:small style:style];
    }
    [self drawOpenMenu:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((point.x - 738.0) / 82.0, 0.0, 1.0);
    if (_page == 2) {
        auto params = p->panner.params();
        switch (_dragSlider) {
        case 21:
            params.layout = s3g::LayoutPannerPreset::Custom;
            params.activeSpeakers = static_cast<uint32_t>(std::lround(2.0 + n * 62.0));
            p->panner.setParams(params);
            break;
        case 24:
            params.selectedSpeaker = static_cast<uint32_t>(std::lround(1.0 + n * static_cast<double>(std::max<uint32_t>(1u, params.activeSpeakers) - 1u))) - 1u;
            p->panner.setParams(params);
            break;
        case 25: {
            auto sp = p->panner.speaker(params.selectedSpeaker);
            sp.azimuthDeg = static_cast<float>(-180.0 + n * 360.0);
            p->panner.setSpeaker(params.selectedSpeaker, sp);
            break;
        }
        case 26: {
            auto sp = p->panner.speaker(params.selectedSpeaker);
            sp.elevationDeg = static_cast<float>(-90.0 + n * 180.0);
            p->panner.setSpeaker(params.selectedSpeaker, sp);
            break;
        }
        case 27: {
            auto sp = p->panner.speaker(params.selectedSpeaker);
            sp.distance = static_cast<float>(0.1 + n * 2.9);
            p->panner.setSpeaker(params.selectedSpeaker, sp);
            break;
        }
        case 28:
            applyParam(*p, kOutputParamId, -60.0 + n * 72.0);
            break;
        default:
            break;
        }
        p->params = p->panner.params();
        [self setNeedsDisplay:YES];
        return;
    }
    switch (_dragSlider) {
    case 3: applyParam(*p, kFocusParamId, 0.25 + n * 3.75); break;
    case 4: applyParam(*p, kRolloffParamId, n * 48.0); break;
    case 5: applyParam(*p, kSmoothingParamId, 1.0 + n * 249.0); break;
    case 6: applyParam(*p, kGlobalAzimuthParamId, -180.0 + n * 360.0); break;
    case 7: applyParam(*p, kGlobalElevationParamId, -90.0 + n * 180.0); break;
    case 8: applyParam(*p, kGlobalDistanceParamId, -3.0 + n * 6.0); break;
    case 9: applyParam(*p, kDiffusionParamId, n); break;
    case 11: applyParam(*p, kSelectedSourceParamId, 1.0 + n * static_cast<double>(s3g::kLayoutPannerSources - 1u)); break;
    case 12: applyParam(*p, kSelectedAzimuthParamId, -180.0 + n * 360.0); break;
    case 13: applyParam(*p, kSelectedElevationParamId, -90.0 + n * 180.0); break;
    case 14: applyParam(*p, kSelectedDistanceParamId, 0.1 + n * 2.9); break;
    case 15: applyParam(*p, kSelectedGainParamId, -60.0 + n * 84.0); break;
    case 16: applyParam(*p, kOutputParamId, -60.0 + n * 72.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if ([[self window] firstResponder] != self) [[self window] makeFirstResponder:self];
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const CGFloat menuW = _openMenu == 1 ? 148.0 : (_openMenu == 3 ? 92.0 : 78.0);
        const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, menuW, itemH * static_cast<CGFloat>(_menuItemCount));
        if (NSPointInRect(pt, menuRect)) {
            const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuRect, itemH, _menuItemCount);
            const uint32_t index = static_cast<uint32_t>(std::max(0, hit));
            if (_openMenu == 1) {
                applyParam(*p, kLayoutParamId, index);
            } else if (_openMenu == 3) {
                auto params = p->panner.params();
                params.layout = s3g::LayoutPannerPreset::Custom;
                params.customShape = lpDesignShapeForMenuIndex(index);
                p->panner.setParams(params);
                p->params = p->panner.params();
            } else {
                applyParam(*p, kMethodParamId, index);
            }
            _openMenu = 0;
            _hoverMenuItem = -1;
            _menuItemCount = 0;
            [self setNeedsDisplay:YES];
            return;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        [self setNeedsDisplay:YES];
    }
    auto openMenu = [&](int menu, uint32_t count, CGFloat preferredY) {
        const CGFloat itemH = 18.0;
        _openMenu = menu;
        _hoverMenuItem = -1;
        _menuItemCount = count;
        _menuOrigin = NSMakePoint(738.0, std::clamp(preferredY, 28.0, 616.0 - itemH * static_cast<CGFloat>(count)));
        [self setNeedsDisplay:YES];
    };
    const NSRect mainPanel = NSMakeRect(18, 42, 596, 556);
    const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
    if (NSPointInRect(pt, mainPanel)) {
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self pageButtonRect:i inRect:mainPanel])) {
                _page = i;
                _dragSource = NO;
                _dragSpeaker = NO;
                _dragView = NO;
                _dragMixerSource = -1;
                _dragMixerOutput = NO;
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    if ((_page == 0 || _page == 2) && NSPointInRect(pt, mainPanel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:mainPanel])) {
                _viewZoom = std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self viewButtonRect:i inRect:mainPanel])) {
                [self setViewPreset:i];
                return;
            }
        }
        if (_page == 2 && NSPointInRect(pt, fieldRect)) {
            const auto params = p->panner.params();
            const auto speakers = p->panner.speakers();
            CGFloat bestD = 999999.0;
            uint32_t best = params.selectedSpeaker;
            for (uint32_t i = 0; i < params.activeSpeakers; ++i) {
                const auto& sp = speakers[i];
                const auto dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
                NSPoint spt = [self projectWorldPoint:{ dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance } rect:fieldRect depth:nil];
                const CGFloat d = std::hypot(pt.x - spt.x, pt.y - spt.y);
                if (d < bestD) { bestD = d; best = i; }
            }
            if (bestD < 18.0) {
                auto next = params;
                next.selectedSpeaker = best;
                p->panner.setParams(next);
                p->params = p->panner.params();
                _dragSpeaker = YES;
                [self setNeedsDisplay:YES];
                return;
            }
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
        if (_page == 0 && NSPointInRect(pt, fieldRect)) {
            const auto params = p->panner.params();
            const auto sources = p->panner.sources();
            CGFloat bestD = 999999.0;
            uint32_t best = params.selectedSource;
            for (uint32_t i = 0; i < s3g::kLayoutPannerSources; ++i) {
                const auto& s = sources[i];
                NSPoint spt = [self projectWorldPoint:s3g::layoutPannerSourcePosition(s) rect:fieldRect depth:nil];
                const CGFloat d = std::hypot(pt.x - spt.x, pt.y - spt.y);
                if (d < bestD) { bestD = d; best = i; }
            }
            if (bestD < 20.0) {
                applyParam(*p, kSelectedSourceParamId, best + 1u);
                _dragSource = YES;
                [self setNeedsDisplay:YES];
                return;
            }
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_page == 1 && NSPointInRect(pt, fieldRect)) {
        if (NSPointInRect(pt, NSInsetRect([self mixerOutputTrackRect:fieldRect], -8.0, -8.0))) {
            _dragMixerOutput = YES;
            const double n = std::clamp((pt.x - [self mixerOutputTrackRect:fieldRect].origin.x) / [self mixerOutputTrackRect:fieldRect].size.width, 0.0, 1.0);
            applyParam(*p, kOutputParamId, -60.0 + n * 72.0);
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t i = 0; i < s3g::kLayoutPannerSources; ++i) {
            if (NSPointInRect(pt, NSInsetRect([self mixerMuteRect:i inRect:fieldRect], -4.0, -4.0))) {
                const auto source = p->panner.source(i);
                applyParam(*p, sourceParamId(i, kSourceMuteOffset), source.muted ? 0.0 : 1.0);
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(pt, NSInsetRect([self mixerSoloRect:i inRect:fieldRect], -4.0, -4.0))) {
                const auto source = p->panner.source(i);
                applyParam(*p, sourceParamId(i, kSourceSoloOffset), source.solo ? 0.0 : 1.0);
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(pt, NSInsetRect([self mixerSourceRect:i inRect:fieldRect], -8.0, -7.0))) {
                _dragMixerSource = static_cast<int>(i);
                const NSRect track = [self mixerSourceRect:i inRect:fieldRect];
                const double n = std::clamp((NSMaxY(track) - pt.y) / track.size.height, 0.0, 1.0);
                applyParam(*p, sourceParamId(i, kSourceGainOffset), -60.0 + n * 84.0);
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    if (NSPointInRect(pt, NSMakeRect(738, 77, 102, 17))) { openMenu(1, kLayoutCount, 96); return; }
    if (_page != 2 && NSPointInRect(pt, NSMakeRect(738, 103, 102, 17))) { openMenu(2, 2, 122); return; }
    if (_page == 2 && NSPointInRect(pt, NSMakeRect(738, 103, 102, 17))) { openMenu(3, kDesignShapeMenuCount, 122); return; }
    if (_page == 2) {
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self designButtonRect:i])) {
                if (i == 0) [self copyCurrentLayoutToCustom];
                else if (i == 1) [self loadLayoutJson];
                else [self saveLayoutJson];
                return;
            }
        }
        const CGFloat rows[] = { 130, 360, 386, 412, 438, 490 };
        const int ids[] = { 21, 24, 25, 26, 27, 28 };
        for (int i = 0; i < 6; ++i) {
            if (NSPointInRect(pt, NSMakeRect(638, rows[i] - 8, 230, 24))) {
                _dragSlider = ids[i];
                [self updateSlider:pt];
                return;
            }
        }
    }
    const CGFloat rows[] = { 130, 156, 182, 208, 234, 260, 286, 360, 386, 412, 438, 464, 490 };
    const int ids[] = { 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16 };
    for (int i = 0; i < 13; ++i) {
        if (NSPointInRect(pt, NSMakeRect(638, rows[i] - 8, 230, 24))) {
            _dragSlider = ids[i];
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
    auto* p = static_cast<Plugin*>(_plugin);
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
    if (_dragSource) {
        const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
        const auto prm = p->panner.params();
        auto source = p->panner.source(prm.selectedSource);
        s3g::Vec3 v = [self worldFromPoint:pt rect:fieldRect previous:s3g::layoutPannerSourcePosition(source)];
        p->panner.setSourcePosition(prm.selectedSource, v);
        p->params = p->panner.params();
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragSpeaker) {
        const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
        const auto prm = p->panner.params();
        const auto sp = p->panner.speaker(prm.selectedSpeaker);
        const auto dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 previous { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        s3g::Vec3 v = [self worldFromPoint:pt rect:fieldRect previous:previous];
        p->panner.setSpeakerPosition(prm.selectedSpeaker, v);
        p->params = p->panner.params();
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragMixerOutput) {
        const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
        const NSRect track = [self mixerOutputTrackRect:fieldRect];
        const double n = std::clamp((pt.x - track.origin.x) / track.size.width, 0.0, 1.0);
        applyParam(*p, kOutputParamId, -60.0 + n * 72.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragMixerSource >= 0) {
        const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
        const NSRect track = [self mixerSourceRect:static_cast<uint32_t>(_dragMixerSource) inRect:fieldRect];
        const double n = std::clamp((NSMaxY(track) - pt.y) / track.size.height, 0.0, 1.0);
        applyParam(*p, sourceParamId(static_cast<uint32_t>(_dragMixerSource), kSourceGainOffset), -60.0 + n * 84.0);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragSlider > 0) [self updateSlider:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragView = NO; _dragSource = NO; _dragSpeaker = NO; _dragMixerSource = -1; _dragMixerOutput = NO; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GLayoutPannerView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3GLayoutPannerView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 900; *h = 620; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,900,620)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GLayoutPannerView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GLayoutPannerView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    "org.s3g.s3g-dsp.layout-panner",
    "s3g Layout Panner",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "16-source direct layout panner with selectable multichannel speaker fields.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->panner.prepare(48000.0);
    p->panner.setParams(p->params);
    p->params = p->panner.params();
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
