#include "s3g_ambisonic_speaker_decoder.h"
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

constexpr uint32_t kInputChannels = s3g::kAmbiSpeakerDecoderMaxChannels;
constexpr uint32_t kOutputChannels = s3g::kAmbiSpeakerDecoderMaxSpeakers;
constexpr uint32_t kStateVersion = 4;

constexpr clap_id kLayoutParamId = 1;
constexpr clap_id kModeParamId = 2;
constexpr clap_id kOrderParamId = 3;
constexpr clap_id kActiveSpeakersParamId = 4;
constexpr clap_id kSelectedSpeakerParamId = 5;
constexpr clap_id kAzimuthParamId = 6;
constexpr clap_id kElevationParamId = 7;
constexpr clap_id kDistanceParamId = 8;
constexpr clap_id kSpeakerGainParamId = 9;
constexpr clap_id kSpeakerEnabledParamId = 10;
constexpr clap_id kRegularizationParamId = 11;
constexpr clap_id kWidthParamId = 12;
constexpr clap_id kEnergyParamId = 13;
constexpr clap_id kOutputParamId = 14;
constexpr clap_id kWeightingParamId = 15;
constexpr clap_id kCustomFieldParamId = 16;
constexpr NSInteger kMixerGainFieldTagBase = 1000;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiSpeakerDecoderParams params {};
    std::array<s3g::AmbiSpeaker, s3g::kAmbiSpeakerDecoderMaxSpeakers> speakers {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::AmbiSpeakerDecoderParams params {};
    s3g::AmbiSpeakerDecoder decoder;
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

const char* layoutName(uint32_t value)
{
    switch (value) {
    case 1: return "QUAD";
    case 2: return "CUBE 8";
    case 3: return "CUBE 17";
    case 4: return "DOME 24";
    case 5: return "DOME 25";
    case 6: return "QUAD+OH";
    case 7: return "3OAFX 24";
    default: return "CUSTOM";
    }
}

uint32_t layoutPresetForMenuIndex(uint32_t index)
{
    static constexpr uint32_t values[] = {
        0u, // CUSTOM
        2u, // CUBE 8
        3u, // CUBE 17
        4u, // DOME 24
        5u, // DOME 25
        1u, // QUAD
        6u, // QUAD+OH
        7u, // 3OAFX 24
    };
    return values[std::min<uint32_t>(index, static_cast<uint32_t>(std::size(values) - 1u))];
}

const char* modeName(uint32_t value)
{
    switch (value) {
    case 1: return "EPAD";
    case 2: return "MMD";
    default: return "BASIC";
    }
}

const char* weightingName(uint32_t value)
{
    switch (value) {
    case 1: return "MAXRE";
    case 2: return "INPHASE";
    default: return "NONE";
    }
}

const char* customFieldName(uint32_t value)
{
    return value == 1 ? "HEMI" : "SPHERE";
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kLayoutParamId: p.params.layout = static_cast<s3g::AmbiSpeakerLayoutPreset>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 7u)); break;
    case kModeParamId: p.params.mode = static_cast<s3g::AmbiSpeakerDecoderMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kOrderParamId: p.params.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiSpeakerDecoderMaxOrder); break;
    case kActiveSpeakersParamId: p.params.activeSpeakers = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 2u, s3g::kAmbiSpeakerDecoderMaxSpeakers); p.params.layout = s3g::AmbiSpeakerLayoutPreset::Custom; break;
    case kSelectedSpeakerParamId: p.params.selectedSpeaker = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiSpeakerDecoderMaxSpeakers) - 1u; break;
    case kAzimuthParamId: p.params.selectedAzimuthDeg = static_cast<float>(std::clamp(value, -180.0, 180.0)); p.params.layout = s3g::AmbiSpeakerLayoutPreset::Custom; break;
    case kElevationParamId: p.params.selectedElevationDeg = static_cast<float>(std::clamp(value, -90.0, 90.0)); p.params.layout = s3g::AmbiSpeakerLayoutPreset::Custom; break;
    case kDistanceParamId: p.params.selectedDistance = static_cast<float>(std::clamp(value, 0.15, 2.0)); p.params.layout = s3g::AmbiSpeakerLayoutPreset::Custom; break;
    case kSpeakerGainParamId:
        p.params.selectedGain = static_cast<float>(std::clamp(value, 0.0, 2.0));
        p.decoder.setParams(p.params);
        p.decoder.setSpeakerGain(p.params.selectedSpeaker, p.params.selectedGain);
        p.params = p.decoder.params();
        return;
    case kSpeakerEnabledParamId: p.params.selectedEnabled = true; break;
    case kRegularizationParamId: p.params.regularization = static_cast<float>(std::clamp(value, 0.0, 0.20)); break;
    case kWidthParamId: p.params.width = static_cast<float>(std::clamp(value, 0.0, 1.50)); break;
    case kEnergyParamId: p.params.energy = static_cast<float>(std::clamp(value, 0.0, 1.50)); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(std::clamp(value, -60.0, 12.0)); break;
    case kWeightingParamId: p.params.weighting = static_cast<s3g::AmbiSpeakerDecoderWeighting>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kCustomFieldParamId: p.params.customField = static_cast<s3g::AmbiSpeakerCustomField>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 1u)); p.params.layout = s3g::AmbiSpeakerLayoutPreset::Custom; break;
    default: break;
    }
    p.decoder.setParams(p.params);
    p.params = p.decoder.params();
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin) { delete self(plugin); }

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->decoder.prepare(sampleRate);
    p->decoder.setParams(p->params);
    p->params = p->decoder.params();
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

    float blockPeak = 0.0f;
    p->decoder.processBlock(input.data32, output.data32, inChannels, outChannels, frames);
    const uint32_t peakChannels = std::min<uint32_t>(outChannels, p->decoder.params().activeSpeakers);
    for (uint32_t ch = 0; ch < peakChannels; ++ch) {
        if (!output.data32[ch]) continue;
        for (uint32_t i = 0; i < frames; ++i) {
            blockPeak = std::max(blockPeak, std::fabs(output.data32[ch][i]));
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
    std::strncpy(info->name, isInput ? "7OA ACN/SN3D In" : "64 Speaker Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParamDefs[] {
    { kLayoutParamId, "Layout", 0.0, 7.0, 7.0, true },
    { kModeParamId, "Mode", 0.0, 2.0, 1.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kActiveSpeakersParamId, "Active Speakers", 2.0, 64.0, 24.0, true },
    { kSelectedSpeakerParamId, "Selected Speaker", 1.0, 64.0, 1.0, true },
    { kAzimuthParamId, "Speaker Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Speaker Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Speaker Distance", 0.15, 2.0, 1.0, false },
    { kSpeakerGainParamId, "Speaker Gain", 0.0, 2.0, 1.0, false },
    { kWidthParamId, "Width", 0.0, 1.50, 1.0, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kWeightingParamId, "Weighting", 0.0, 2.0, 1.0, true },
    { kCustomFieldParamId, "Custom Field", 0.0, 1.0, 0.0, true },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Speaker Decoder", sizeof(info->module));
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
    case kLayoutParamId: *value = static_cast<double>(static_cast<uint32_t>(p.layout)); return true;
    case kModeParamId: *value = static_cast<double>(static_cast<uint32_t>(p.mode)); return true;
    case kOrderParamId: *value = static_cast<double>(p.order); return true;
    case kActiveSpeakersParamId: *value = static_cast<double>(p.activeSpeakers); return true;
    case kSelectedSpeakerParamId: *value = static_cast<double>(p.selectedSpeaker + 1u); return true;
    case kAzimuthParamId: *value = p.selectedAzimuthDeg; return true;
    case kElevationParamId: *value = p.selectedElevationDeg; return true;
    case kDistanceParamId: *value = p.selectedDistance; return true;
    case kSpeakerGainParamId: *value = p.selectedGain; return true;
    case kSpeakerEnabledParamId: *value = 1.0; return true;
    case kRegularizationParamId: *value = p.regularization; return true;
    case kWidthParamId: *value = p.width; return true;
    case kEnergyParamId: *value = p.energy; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    case kWeightingParamId: *value = static_cast<double>(static_cast<uint32_t>(p.weighting)); return true;
    case kCustomFieldParamId: *value = static_cast<double>(static_cast<uint32_t>(p.customField)); return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kLayoutParamId) std::snprintf(display, size, "%s", layoutName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kModeParamId) std::snprintf(display, size, "%s", modeName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kWeightingParamId) std::snprintf(display, size, "%s", weightingName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kCustomFieldParamId) std::snprintf(display, size, "%s", customFieldName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kActiveSpeakersParamId || id == kSelectedSpeakerParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kAzimuthParamId || id == kElevationParamId) std::snprintf(display, size, "%+.1f deg", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kSpeakerEnabledParamId) std::snprintf(display, size, "ON");
    else std::snprintf(display, size, "%.3f", value);
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
    auto* p = self(plugin);
    s.params = p->params;
    s.speakers = p->decoder.speakers();
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = s.params;
    p->decoder.setParams(p->params);
    p->decoder.setSpeakers(s.speakers);
    p->params = p->decoder.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3G3OAFXSpeakerDecoderView : NSView <NSTextFieldDelegate> {
    void* _plugin;
    int _dragSlider;
    NSTimer* _timer;
    int _viewMode;
    int _rightPage;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSPoint _menuOrigin;
    NSTextField* _azField;
    NSTextField* _elField;
    NSTextField* _distField;
    BOOL _azFieldDirty;
    BOOL _elFieldDirty;
    BOOL _distFieldDirty;
    BOOL _hasSpeakerSelection;
    int _mixerPage;
    NSTextField* _gainFields[16];
    BOOL _gainFieldDirty[16];
    int _dragMixerSpeaker;
    BOOL _dragMixerOutput;
}
- (id)initWithPlugin:(void*)plugin;
- (BOOL)acceptsFirstResponder;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (NSTextField*)makeValueField:(NSInteger)tag;
- (NSTextField*)makeMixerGainField:(NSInteger)tag;
- (void)updateValueFields;
- (void)updateMixerGainFieldsForRect:(NSRect)rect;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)drawOpenMenu:(NSDictionary*)attrs;
- (void)updateMenuHover:(NSPoint)point;
- (NSRect)fieldPageButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawFieldPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)setViewPreset:(int)mode;
- (CGFloat)viewScaleForRect:(NSRect)rect;
- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect depth:(CGFloat*)depth;
- (void)drawSpeakerField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (void)drawDecodeMap:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style;
- (NSRect)mixerOutputTrackRect:(NSRect)rect;
- (NSRect)mixerSpeakerRect:(uint32_t)index inRect:(NSRect)rect;
- (NSRect)mixerGainFieldRect:(uint32_t)slot inRect:(NSRect)rect;
- (NSRect)mixerMuteRect:(uint32_t)slot inRect:(NSRect)rect;
- (NSRect)mixerSoloRect:(uint32_t)slot inRect:(NSRect)rect;
- (NSRect)mixerPageButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawMixerPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawSpeakerMixer:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style;
- (void)updateMixerSpeakerGain:(NSPoint)point inRect:(NSRect)rect;
- (void)updateMixerOutput:(NSPoint)point inRect:(NSRect)rect;
- (void)toggleMixerSpeakerMute:(uint32_t)index;
- (void)toggleMixerSpeakerSolo:(uint32_t)index;
- (uint32_t)hitSpeakerAt:(NSPoint)pt inRect:(NSRect)rect found:(BOOL*)found;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* sdColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static float linearToSrgb(float v)
{
    const float x = std::clamp(v, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static NSColor* speakerColorFromAed(float azDeg, float elDeg, float distance, bool selected)
{
    const float hue = std::fmod((azDeg / 360.0f) + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(elDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.28f, 0.88f);
    const float chroma = std::clamp(distance / 2.4f, 0.08f, 1.0f) * 0.37f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float r = linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float bl = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.08f : 0.18f;
    r = r * (1.0f - grayMix) + 0.74f * grayMix;
    g = g * (1.0f - grayMix) + 0.74f * grayMix;
    bl = bl * (1.0f - grayMix) + 0.74f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:selected ? 1.0 : 0.88];
}

@implementation S3G3OAFXSpeakerDecoderView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 620)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _timer = nil;
        _viewMode = 2;
        _rightPage = 0;
        _viewAzDeg = 35.0;
        _viewElDeg = 34.0;
        _viewZoom = 1.0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _menuOrigin = NSMakePoint(0, 0);
        _azField = [self makeValueField:kAzimuthParamId];
        _elField = [self makeValueField:kElevationParamId];
        _distField = [self makeValueField:kDistanceParamId];
        [self addSubview:_azField];
        [self addSubview:_elField];
        [self addSubview:_distField];
        for (int i = 0; i < 16; ++i) {
            _gainFields[i] = [self makeMixerGainField:kMixerGainFieldTagBase + i];
            _gainFieldDirty[i] = NO;
            [self addSubview:_gainFields[i]];
        }
        _azFieldDirty = NO;
        _elFieldDirty = NO;
        _distFieldDirty = NO;
        _hasSpeakerSelection = NO;
        _mixerPage = 0;
        _dragMixerSpeaker = -1;
        _dragMixerOutput = NO;
        [self updateValueFields];
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
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if ([self isHidden] || !_plugin || !s3g::clap_support::hostAppIsActive()) return;
    if (_rightPage == 2 && !_dragView) return;
    [self setNeedsDisplay:YES];
}
- (NSTextField*)makeValueField:(NSInteger)tag
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(812, 0, 54, 16)];
    [field setTag:tag];
    [field setDelegate:self];
    [field setFont:[NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular]];
    [field setTextColor:sdColor(0xf0f0f0)];
    [field setBackgroundColor:sdColor(0x131313)];
    [field setDrawsBackground:YES];
    [field setBezeled:YES];
    [field setBordered:YES];
    [field setFocusRingType:NSFocusRingTypeNone];
    [field setAlignment:NSTextAlignmentCenter];
    [field setFormatter:nil];
    return [field autorelease];
}
- (NSTextField*)makeMixerGainField:(NSInteger)tag
{
    NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 38, 16)];
    [field setTag:tag];
    [field setDelegate:self];
    [field setFont:[NSFont fontWithName:@"Menlo" size:8.5] ?: [NSFont monospacedSystemFontOfSize:8.5 weight:NSFontWeightRegular]];
    [field setTextColor:sdColor(0xf0f0f0)];
    [field setBackgroundColor:sdColor(0x131313)];
    [field setDrawsBackground:YES];
    [field setBezeled:YES];
    [field setBordered:YES];
    [field setFocusRingType:NSFocusRingTypeNone];
    [field setAlignment:NSTextAlignmentCenter];
    [field setFormatter:nil];
    [field setHidden:YES];
    return [field autorelease];
}
- (BOOL)fieldIsEditing:(NSTextField*)field
{
    return [[self window] firstResponder] == [field currentEditor];
}
- (void)updateValueFields
{
    if (!_plugin) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const auto prm = p->decoder.params();
    [_azField setHidden:NO];
    [_elField setHidden:NO];
    [_distField setHidden:NO];
    if (_rightPage != 1) {
        for (int i = 0; i < 16; ++i) [_gainFields[i] setHidden:YES];
    }
    [_azField setFrame:NSMakeRect(812, 382, 54, 16)];
    [_elField setFrame:NSMakeRect(812, 408, 54, 16)];
    [_distField setFrame:NSMakeRect(812, 434, 54, 16)];
    if (![self fieldIsEditing:_azField]) {
        [_azField setStringValue:[NSString stringWithFormat:@"%+.1f", prm.selectedAzimuthDeg]];
    }
    if (![self fieldIsEditing:_elField]) {
        [_elField setStringValue:[NSString stringWithFormat:@"%+.1f", prm.selectedElevationDeg]];
    }
    if (![self fieldIsEditing:_distField]) {
        [_distField setStringValue:[NSString stringWithFormat:@"%.2f", prm.selectedDistance]];
    }
}
- (void)updateMixerGainFieldsForRect:(NSRect)rect
{
    if (!_plugin) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const auto params = p->decoder.params();
    const auto speakers = p->decoder.speakers();
    const uint32_t n = std::min<uint32_t>(params.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
    for (uint32_t slot = 0; slot < 16u; ++slot) {
        NSTextField* field = _gainFields[slot];
        const uint32_t index = pageStart + slot;
        const BOOL visible = _rightPage == 1 && index < n;
        [field setHidden:!visible];
        if (!visible) continue;
        [field setFrame:[self mixerGainFieldRect:slot inRect:rect]];
        if (![self fieldIsEditing:field]) {
            [field setStringValue:[NSString stringWithFormat:@"%.2f", speakers[index].gain]];
        }
    }
}
- (void)controlTextDidChange:(NSNotification*)notification
{
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    if (!field) return;
    if ([field tag] == kAzimuthParamId) {
        _azFieldDirty = YES;
    } else if ([field tag] == kElevationParamId) {
        _elFieldDirty = YES;
    } else if ([field tag] == kDistanceParamId) {
        _distFieldDirty = YES;
    } else if ([field tag] >= kMixerGainFieldTagBase && [field tag] < kMixerGainFieldTagBase + 16) {
        _gainFieldDirty[[field tag] - kMixerGainFieldTagBase] = YES;
    }
}
- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    if (!field || !_plugin) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const double value = [[field stringValue] doubleValue];
    if ([field tag] == kAzimuthParamId) {
        if (!_azFieldDirty) return;
        _azFieldDirty = NO;
        applyParam(*p, kAzimuthParamId, value);
    } else if ([field tag] == kElevationParamId) {
        if (!_elFieldDirty) return;
        _elFieldDirty = NO;
        applyParam(*p, kElevationParamId, value);
    } else if ([field tag] == kDistanceParamId) {
        if (!_distFieldDirty) return;
        _distFieldDirty = NO;
        applyParam(*p, kDistanceParamId, value);
    } else if ([field tag] >= kMixerGainFieldTagBase && [field tag] < kMixerGainFieldTagBase + 16) {
        const NSInteger slot = [field tag] - kMixerGainFieldTagBase;
        if (!_gainFieldDirty[slot]) return;
        _gainFieldDirty[slot] = NO;
        const uint32_t index = static_cast<uint32_t>(_mixerPage) * 16u + static_cast<uint32_t>(slot);
        const uint32_t n = std::min<uint32_t>(p->decoder.params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
        if (index < n) {
            applyParam(*p, kSelectedSpeakerParamId, static_cast<double>(index + 1u));
            applyParam(*p, kSpeakerGainParamId, std::clamp(value, 0.0, 2.0));
            _hasSpeakerSelection = YES;
        }
    }
    [self updateValueFields];
    [self updateMixerGainFieldsForRect:NSMakeRect(34, 76, 564, 506)];
    [self setNeedsDisplay:YES];
}
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
    static NSString* layoutItems[] = { @"CUSTOM", @"CUBE 8", @"CUBE 17", @"DOME 24", @"DOME 25", @"QUAD", @"QUAD+OH", @"3OAFX 24" };
    static NSString* modeItems[] = { @"BASIC", @"EPAD", @"MMD" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* weightItems[] = { @"NONE", @"MAXRE", @"INPHASE" };
    static NSString* fieldItems[] = { @"SPHERE", @"HEMI" };
    NSString** items = layoutItems;
    if (_openMenu == 2) items = modeItems;
    else if (_openMenu == 3) items = orderItems;
    else if (_openMenu == 4) items = weightItems;
    else if (_openMenu == 5) items = fieldItems;
    const CGFloat itemH = 18.0;
    const CGFloat w = 124.0;
    NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, w, itemH * static_cast<CGFloat>(_menuItemCount));
    int selected = 0;
    if (_plugin) {
        auto* p = static_cast<Plugin*>(_plugin);
        const auto prm = p->decoder.params();
        if (_openMenu == 1) {
            const uint32_t layoutValue = static_cast<uint32_t>(prm.layout);
            for (uint32_t i = 0; i < _menuItemCount; ++i) {
                if (layoutPresetForMenuIndex(i) == layoutValue) selected = static_cast<int>(i);
            }
        } else if (_openMenu == 2) selected = static_cast<int>(static_cast<uint32_t>(prm.mode));
        else if (_openMenu == 3) selected = static_cast<int>(prm.order - 1u);
        else if (_openMenu == 4) selected = static_cast<int>(static_cast<uint32_t>(prm.weighting));
        else if (_openMenu == 5) selected = static_cast<int>(static_cast<uint32_t>(prm.customField));
    }
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawDropdownMenu(menuRect, itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    const CGFloat itemH = 18.0;
    const CGFloat w = 124.0;
    const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, w, itemH * static_cast<CGFloat>(_menuItemCount));
    const int next = s3g::clap_gui::dropdownHitIndex(point, menuRect, itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (NSRect)fieldPageButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 56.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(rect.origin.x + 132.0 + static_cast<CGFloat>(index) * (w + gap),
                      rect.origin.y + 4.0,
                      w,
                      h);
}
- (void)drawFieldPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"FIELD", @"MIXER", @"MAP" };
    s3g::clap_gui::Style style;
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self fieldPageButtonRect:i inRect:rect], rect, labels[i], i == _rightPage, attrs, style);
    }
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
- (CGFloat)viewScaleForRect:(NSRect)rect
{
    CGFloat layoutScale = 1.0;
    if (_plugin && _viewMode != 0 && _viewMode != 1) {
        auto* p = static_cast<Plugin*>(_plugin);
        const auto layout = p->decoder.params().layout;
        if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8 || layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
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
- (void)drawSpeakerField:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto prm = p->decoder.params();
    [sdColor(0x111111) setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);

    const auto speakers = p->decoder.speakers();
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    std::array<NSPoint, s3g::kAmbiSpeakerDecoderMaxSpeakers> points {};
    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const s3g::Vec3 dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 world { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        points[i] = [self projectWorldPoint:world rect:rect depth:nil];
        points[i].x = std::round(points[i].x);
        points[i].y = std::round(points[i].y);
    }
    [sdColor(0x777777, 0.72) setStroke];
    NSBezierPath* links = [NSBezierPath bezierPath];
    auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= n || b >= n) return;
        [links moveToPoint:points[a]];
        [links lineToPoint:points[b]];
    };
    auto ring = [&](uint32_t base, uint32_t count) {
        if (count < 2u) return;
        for (uint32_t i = 0; i < count; ++i) edge(base + i, base + ((i + 1u) % count));
    };
    const auto layout = prm.layout;
    if (layout == s3g::AmbiSpeakerLayoutPreset::Quad) {
        ring(0, 4);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8) {
        ring(0, 4);
        ring(4, 4);
        for (uint32_t i = 0; i < 4; ++i) edge(i, i + 4u);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
        ring(0, 4);
        edge(4, 5); edge(5, 6);
        edge(6, 7); edge(7, 8);
        edge(8, 9); edge(9, 10);
        edge(10, 11); edge(11, 4);
        ring(12, 4);
        edge(0, 4); edge(1, 6); edge(2, 8); edge(3, 10);
        edge(4, 12); edge(6, 13); edge(8, 14); edge(10, 15);
        edge(12, 16); edge(13, 16); edge(14, 16); edge(15, 16);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dome24 || layout == s3g::AmbiSpeakerLayoutPreset::Dome25) {
        ring(0, 12);
        ring(12, 8);
        ring(20, 4);
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
        if (layout == s3g::AmbiSpeakerLayoutPreset::Dome25) {
            for (uint32_t i = 0; i < 4; ++i) edge(24, 20u + i);
        }
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::QuadOverhead6) {
        ring(0, 4);
        edge(4, 0); edge(4, 3);
        edge(5, 1); edge(5, 2);
        edge(4, 5);
    }
    [links setLineWidth:1.0];
    [links stroke];

    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const bool selected = _hasSpeakerSelection && i == prm.selectedSpeaker;
        const CGFloat r = selected ? 6.0 : 4.8;
        [speakerColorFromAed(sp.azimuthDeg, sp.elevationDeg, sp.distance, selected) setFill];
        NSRectFill(NSMakeRect(points[i].x - r, points[i].y - r, r * 2.0, r * 2.0));
        [sdColor(0x050505, 0.90) setStroke];
        NSFrameRect(NSMakeRect(points[i].x - r, points[i].y - r, r * 2.0, r * 2.0));
        if (selected) {
            [sdColor(0xf2f2f2) setStroke];
            NSFrameRect(NSMakeRect(points[i].x - 10.0, points[i].y - 10.0, 20.0, 20.0));
        }
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        NSDictionary* idAttrs = @{ NSForegroundColorAttributeName:selected ? sdColor(0xf4f4f4) : sdColor(0x151515),
                                   NSFontAttributeName:[NSFont fontWithName:@"Menlo-Bold" size:7.5] ?: [NSFont monospacedSystemFontOfSize:7.5 weight:NSFontWeightBold] };
        NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(points[i].x - labelSize.width * 0.5,
                                       points[i].y - labelSize.height * 0.5 - 0.5)
            withAttributes:idAttrs];
    }
    NSString* viewText = _viewMode == 0 ? @"TOP VIEW   0 front/top  -90 right  +90 left"
        : (_viewMode == 1 ? @"SIDE VIEW   +90 elevation up" : @"3/4 VIEW   drag blank space to rotate");
    [viewText drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + rect.size.height - 23) withAttributes:attrs];
}
- (void)drawDecodeMap:(NSRect)rect attrs:(NSDictionary*)attrs small:(NSDictionary*)small style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto prm = p->decoder.params();
    const auto speakers = p->decoder.speakers();
    const auto& matrix = p->decoder.matrix();
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const uint32_t ambiCh = s3g::ambiChannelsForOrder(prm.order);
    [sdColor(0x111111) setFill]; NSRectFill(rect);
    [style.grid setStroke]; NSFrameRect(rect);

    std::array<NSPoint, s3g::kAmbiSpeakerDecoderMaxSpeakers> points {};
    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const s3g::Vec3 dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 world { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        points[i] = [self projectWorldPoint:world rect:rect depth:nil];
    }

    NSBezierPath* links = [NSBezierPath bezierPath];
    auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= n || b >= n) return;
        [links moveToPoint:points[a]];
        [links lineToPoint:points[b]];
    };
    auto ring = [&](uint32_t base, uint32_t count) {
        if (count < 2u || base >= n) return;
        count = std::min<uint32_t>(count, n - base);
        for (uint32_t i = 0; i < count; ++i) edge(base + i, base + ((i + 1u) % count));
    };
    const auto layout = prm.layout;
    if (layout == s3g::AmbiSpeakerLayoutPreset::Quad) {
        ring(0, 4);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube8) {
        ring(0, 4); ring(4, 4);
        for (uint32_t i = 0; i < 4; ++i) edge(i, i + 4u);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube17) {
        ring(0, 4);
        edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 8);
        edge(8, 9); edge(9, 10); edge(10, 11); edge(11, 4);
        ring(12, 4);
        edge(0, 4); edge(1, 6); edge(2, 8); edge(3, 10);
        edge(4, 12); edge(6, 13); edge(8, 14); edge(10, 15);
        edge(12, 16); edge(13, 16); edge(14, 16); edge(15, 16);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dome24 || layout == s3g::AmbiSpeakerLayoutPreset::Dome25) {
        ring(0, 12); ring(12, 8); ring(20, 4);
        for (uint32_t i = 0; i < 8; ++i) {
            const uint32_t lowerA = (i * 3u) / 2u;
            edge(12u + i, lowerA);
            edge(12u + i, (lowerA + 1u) % 12u);
        }
        for (uint32_t i = 0; i < 4; ++i) {
            edge(20u + i, 12u + i * 2u);
            edge(20u + i, 12u + ((i * 2u + 1u) % 8u));
        }
        if (layout == s3g::AmbiSpeakerLayoutPreset::Dome25) {
            for (uint32_t i = 0; i < 4; ++i) edge(24, 20u + i);
        }
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::QuadOverhead6) {
        ring(0, 4);
        edge(4, 0); edge(4, 3); edge(5, 1); edge(5, 2); edge(4, 5);
    }
    const uint32_t probeIndex = std::min<uint32_t>(prm.selectedSpeaker, n > 0u ? n - 1u : 0u);
    const auto& probeSpeaker = speakers[probeIndex];
    const auto basis = s3g::acnSn3dBasis7(s3g::directionFromAed(probeSpeaker.azimuthDeg, probeSpeaker.elevationDeg));
    std::array<float, s3g::kAmbiSpeakerDecoderMaxSpeakers> energy {};
    float maxEnergy = 0.000001f;
    for (uint32_t sp = 0; sp < n; ++sp) {
        float value = 0.0f;
        for (uint32_t ch = 0; ch < ambiCh; ++ch) {
            value += basis[ch] * matrix[sp][ch];
        }
        value *= speakers[sp].enabled ? speakers[sp].gain : 0.0f;
        energy[sp] = std::fabs(value);
        maxEnergy = std::max(maxEnergy, energy[sp]);
    }

    for (uint32_t i = 0; i < n; ++i) {
        const CGFloat e = std::pow(std::clamp<CGFloat>(energy[i] / maxEnergy, 0.0, 1.0), 0.70);
        const CGFloat r = 7.0;
        const NSRect square = NSMakeRect(points[i].x - r, points[i].y - r, r * 2.0, r * 2.0);
        [sdColor(0x151515) setFill]; NSRectFill(square);
        [s3g::clap_gui::heatColor(e, speakers[i].enabled ? 0.96 : 0.24) setFill];
        NSRectFill(NSInsetRect(square, 2.0, 2.0));
        [sdColor(i == probeIndex ? 0xf5f5f5 : (e > 0.92 ? 0xf0f0f0 : 0x777777)) setStroke];
        NSFrameRect(square);
        if (i == probeIndex) {
            [sdColor(0xf5f5f5, 0.92) setStroke];
            NSFrameRect(NSInsetRect(square, -4.0, -4.0));
            [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(square) - 13.0, NSMidY(square))
                                      toPoint:NSMakePoint(NSMidX(square) + 13.0, NSMidY(square))];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(square), NSMidY(square) - 13.0)
                                      toPoint:NSMakePoint(NSMidX(square), NSMidY(square) + 13.0)];
        }
        if (n <= 32) {
            NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
            NSSize labelSize = [label sizeWithAttributes:small];
            [label drawAtPoint:NSMakePoint(NSMidX(square) - labelSize.width * 0.5,
                                           NSMidY(square) - labelSize.height * 0.5 - 0.5)
                withAttributes:small];
        }
    }
    [sdColor(0xd0d0d0, 0.76) setStroke];
    [links setLineWidth:1.15];
    [links stroke];
    [[NSString stringWithFormat:@"PROBE S%u   AZ %+.1f  EL %+.1f   %uOA",
        probeIndex + 1u,
        probeSpeaker.azimuthDeg,
        probeSpeaker.elevationDeg,
        prm.order]
        drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + rect.size.height - 23)
        withAttributes:attrs];
}
- (NSRect)mixerOutputTrackRect:(NSRect)rect
{
    return NSMakeRect(rect.origin.x + 88.0, rect.origin.y + 42.0, rect.size.width - 178.0, 9.0);
}
- (NSRect)mixerSpeakerRect:(uint32_t)index inRect:(NSRect)rect
{
    const uint32_t slot = index % 16u;
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(slot) * laneW;
    return NSMakeRect(x + 8.0, rect.origin.y + 128.0, 12.0, rect.size.height - 196.0);
}
- (NSRect)mixerGainFieldRect:(uint32_t)slot inRect:(NSRect)rect
{
    const NSRect fader = [self mixerSpeakerRect:slot inRect:rect];
    const CGFloat w = 30.0;
    return NSMakeRect(NSMidX(fader) - w * 0.5, NSMaxY(fader) + 7.0, w, 15.0);
}
- (NSRect)mixerMuteRect:(uint32_t)slot inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(slot) * laneW;
    return NSMakeRect(x + 1.0, NSMaxY(rect) - 42.0, 14.0, 14.0);
}
- (NSRect)mixerSoloRect:(uint32_t)slot inRect:(NSRect)rect
{
    const CGFloat laneW = 34.0;
    const CGFloat x = rect.origin.x + 12.0 + static_cast<CGFloat>(slot) * laneW;
    return NSMakeRect(x + 17.0, NSMaxY(rect) - 42.0, 14.0, 14.0);
}
- (NSRect)mixerPageButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 54.0;
    const CGFloat h = 13.0;
    const CGFloat gap = 5.0;
    const int count = 4;
    return NSMakeRect(NSMaxX(rect) - 18.0 - static_cast<CGFloat>(count - index) * w - static_cast<CGFloat>(count - index - 1) * gap,
                      rect.origin.y + 17.0,
                      w,
                      h);
}
- (void)drawMixerPageButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    static NSString* labels[] = { @"1-16", @"17-32", @"33-48", @"49-64" };
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t n = p ? std::min<uint32_t>(p->decoder.params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers) : 0u;
    const int pageCount = static_cast<int>((n + 15u) / 16u);
    if (pageCount <= 1) return;
    for (int i = 0; i < pageCount; ++i) {
        NSRect button = [self mixerPageButtonRect:i inRect:rect];
        [sdColor(i == _mixerPage ? 0x303030 : 0x151515) setFill];
        NSRectFill(button);
        [sdColor(i == _mixerPage ? 0xd1d1d1 : 0x555555) setStroke];
        NSFrameRect(button);
        NSSize size = [labels[i] sizeWithAttributes:attrs];
        [labels[i] drawAtPoint:NSMakePoint(button.origin.x + (button.size.width - size.width) * 0.5,
                                           button.origin.y + (button.size.height - size.height) * 0.5 - 0.5)
                 withAttributes:attrs];
    }
}
- (void)drawSpeakerMixer:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto speakers = p->decoder.speakers();
    const auto params = p->decoder.params();
    const uint32_t n = std::min<uint32_t>(params.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    const int pageCount = std::max<int>(1, static_cast<int>((n + 15u) / 16u));
    _mixerPage = std::clamp(_mixerPage, 0, pageCount - 1);
    const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
    [self drawMixerPageButtonsInRect:rect attrs:attrs];
    s3g::clap_gui::drawSlider(@"OUT",
                               [NSString stringWithFormat:@"%+.1f", params.outputGainDb],
                               (params.outputGainDb + 60.0f) / 72.0f,
                               rect.origin.y + 36.0,
                               attrs,
                               attrs,
                               style,
                               rect.origin.x + 18.0,
                               rect.origin.x + 88.0,
                               rect.origin.x + rect.size.width - 70.0,
                               rect.size.width - 178.0);
    bool anySolo = false;
    for (uint32_t i = 0; i < n; ++i) anySolo = anySolo || speakers[i].solo;
    const uint32_t pageEnd = std::min<uint32_t>(n, pageStart + 16u);
    for (uint32_t i = pageStart; i < pageEnd; ++i) {
        const auto& sp = speakers[i];
        const bool selected = _hasSpeakerSelection && i == params.selectedSpeaker;
        const bool audible = sp.enabled && (!anySolo || sp.solo);
        const uint32_t pageSlot = i - pageStart;
        const CGFloat laneW = 34.0;
        const CGFloat laneX = rect.origin.x + 12.0 + static_cast<CGFloat>(pageSlot) * laneW;
        if (selected) {
            [sdColor(0x242424) setFill];
            NSRectFill(NSMakeRect(laneX - 2.0, rect.origin.y + 96.0, laneW - 3.0, rect.size.height - 102.0));
            [sdColor(0x777777) setStroke];
            NSFrameRect(NSMakeRect(laneX - 2.0, rect.origin.y + 96.0, laneW - 3.0, rect.size.height - 102.0));
        }
        const NSRect slot = [self mixerSpeakerRect:i inRect:rect];
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        [label drawAtPoint:NSMakePoint(laneX + (i < 9 ? 10.0 : 7.0), rect.origin.y + 97.0) withAttributes:attrs];
        [sdColor(audible ? 0x181818 : 0x0d0d0d) setFill];
        NSRectFill(slot);
        [sdColor(audible ? 0x545454 : 0x333333) setStroke];
        NSFrameRect(slot);
        const CGFloat norm = std::clamp<CGFloat>(sp.gain / 2.0f, 0.0, 1.0);
        NSRect fill = NSInsetRect(slot, 2.0, 2.0);
        const CGFloat fullH = fill.size.height;
        fill.origin.y += fullH * (1.0 - norm);
        fill.size.height = std::max<CGFloat>(1.0, fullH * norm);
        [speakerColorFromAed(sp.azimuthDeg, sp.elevationDeg, sp.distance, selected) setFill];
        NSRectFill(fill);
        [sdColor(selected ? 0xf2f2f2 : 0x9a9a9a) setFill];
        NSRectFill(NSMakeRect(slot.origin.x - 2.0,
                              slot.origin.y + slot.size.height * (1.0 - norm) - 1.0,
                              slot.size.width + 4.0,
                              3.0));
        NSRect mute = [self mixerMuteRect:pageSlot inRect:rect];
        [sdColor(sp.enabled ? 0x151515 : 0x3a3a3a) setFill]; NSRectFill(mute);
        [sdColor(sp.enabled ? 0x5a5a5a : 0xd1d1d1) setStroke]; NSFrameRect(mute);
        [@"M" drawAtPoint:NSMakePoint(mute.origin.x + 3.0, mute.origin.y + 2.0) withAttributes:attrs];
        NSRect solo = [self mixerSoloRect:pageSlot inRect:rect];
        [sdColor(sp.solo ? 0xd1d1d1 : 0x151515) setFill]; NSRectFill(solo);
        [sdColor(sp.solo ? 0xf2f2f2 : 0x5a5a5a) setStroke]; NSFrameRect(solo);
        NSDictionary* soloAttrs = sp.solo
            ? @{ NSForegroundColorAttributeName:sdColor(0x111111), NSFontAttributeName:[attrs objectForKey:NSFontAttributeName] }
            : attrs;
        [@"S" drawAtPoint:NSMakePoint(solo.origin.x + 3.0, solo.origin.y + 2.0) withAttributes:soloAttrs];
    }
    [self updateMixerGainFieldsForRect:rect];
}
- (void)updateMixerSpeakerGain:(NSPoint)point inRect:(NSRect)rect
{
    if (_dragMixerSpeaker < 0) return;
    const uint32_t index = static_cast<uint32_t>(_dragMixerSpeaker);
    NSRect track = [self mixerSpeakerRect:index inRect:rect];
    const CGFloat norm = std::clamp((NSMaxY(track) - point.y) / track.size.height, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    applyParam(*p, kSelectedSpeakerParamId, static_cast<double>(index + 1u));
    applyParam(*p, kSpeakerGainParamId, norm * 2.0);
    _hasSpeakerSelection = YES;
    [self setNeedsDisplay:YES];
}
- (void)updateMixerOutput:(NSPoint)point inRect:(NSRect)rect
{
    NSRect track = [self mixerOutputTrackRect:rect];
    const CGFloat norm = std::clamp((point.x - track.origin.x) / track.size.width, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    applyParam(*p, kOutputParamId, -60.0 + norm * 72.0);
    [self setNeedsDisplay:YES];
}
- (void)toggleMixerSpeakerMute:(uint32_t)index
{
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t n = std::min<uint32_t>(p->decoder.params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    if (index >= n) return;
    const bool enabled = p->decoder.speakers()[index].enabled;
    p->decoder.setSpeakerEnabled(index, !enabled);
    p->params = p->decoder.params();
    _hasSpeakerSelection = YES;
    [self setNeedsDisplay:YES];
}
- (void)toggleMixerSpeakerSolo:(uint32_t)index
{
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t n = std::min<uint32_t>(p->decoder.params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    if (index >= n) return;
    const bool solo = p->decoder.speakers()[index].solo;
    p->decoder.setSpeakerSolo(index, !solo);
    p->params = p->decoder.params();
    _hasSpeakerSelection = YES;
    [self setNeedsDisplay:YES];
}
- (uint32_t)hitSpeakerAt:(NSPoint)pt inRect:(NSRect)rect found:(BOOL*)found
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto prm = p->decoder.params();
    const auto speakers = p->decoder.speakers();
    const uint32_t n = std::min<uint32_t>(prm.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    uint32_t best = prm.selectedSpeaker;
    CGFloat bestD = 999999.0;
    for (uint32_t i = 0; i < n; ++i) {
        const auto& sp = speakers[i];
        const s3g::Vec3 dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 world { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        const NSPoint spPt = [self projectWorldPoint:world rect:rect depth:nil];
        const CGFloat d = std::hypot(pt.x - spPt.x, pt.y - spPt.y);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (found) *found = bestD < 18.0;
    return best;
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10] ?: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
    NSFont* titleFont = [NSFont fontWithName:@"Menlo" size:10.5] ?: [NSFont monospacedSystemFontOfSize:10.5 weight:NSFontWeightRegular];
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSDictionary* lab = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:mono };
    NSDictionary* titleAttrs = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:titleFont };
    [@"s3g AMBI SPEAKER DECODER" drawAtPoint:NSMakePoint(18,14) withAttributes:titleAttrs];
    const float pk = p->outputPeak.load(std::memory_order_relaxed);
    [[NSString stringWithFormat:@"PK %+4.1f", 20.0 * std::log10(std::max(0.000001f, pk))] drawAtPoint:NSMakePoint(728,14) withAttributes:small];
    [@"64CH" drawAtPoint:NSMakePoint(838,14) withAttributes:small];

    s3g::clap_gui::drawPanelFrame(18, 42, 596, 556, style);
    s3g::clap_gui::drawPanelHeader(_rightPage == 0 ? @"SPEAKER FIELD" : (_rightPage == 1 ? @"SPEAKER MIXER" : @"DECODE MAP"), true, 18, 42, 596, 21, lab, style);
    [self drawFieldPageButtonsInRect:NSMakeRect(18, 42, 596, 556) attrs:small];
    if (_rightPage == 0 || _rightPage == 2) {
        for (int i = 0; i < 16; ++i) [_gainFields[i] setHidden:YES];
        [self drawZoomButtonsInRect:NSMakeRect(18, 42, 596, 556) attrs:small];
        [self drawViewButtonsInRect:NSMakeRect(18, 42, 596, 556) attrs:small];
        if (_rightPage == 0) {
            [self drawSpeakerField:NSMakeRect(34, 76, 564, 506) attrs:small small:small style:style];
        } else {
            [self drawDecodeMap:NSMakeRect(34, 76, 564, 506) attrs:small small:small style:style];
        }
    } else {
        [self drawSpeakerMixer:NSMakeRect(34, 76, 564, 506) attrs:small style:style];
    }

    const NSRect lowerPanel = NSMakeRect(630, 322, 250, 276);
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 268, style);
    s3g::clap_gui::drawPanelHeader(@"DECODER", true, 630, 42, 250, 21, lab, style);
    s3g::clap_gui::drawPanelFrame(lowerPanel.origin.x, lowerPanel.origin.y, lowerPanel.size.width, lowerPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SPEAKER", true, lowerPanel.origin.x, lowerPanel.origin.y, lowerPanel.size.width, 21, lab, style);

    const auto prm = p->decoder.params();
    [self drawMenu:@"LAYOUT" value:[NSString stringWithUTF8String:layoutName(static_cast<uint32_t>(prm.layout))] y:78 attrs:small style:style];
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:modeName(static_cast<uint32_t>(prm.mode))] y:104 attrs:small style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", prm.order] y:130 attrs:small style:style];
    [self drawMenu:@"WGT" value:[NSString stringWithUTF8String:weightingName(static_cast<uint32_t>(prm.weighting))] y:156 attrs:small style:style];
    [self drawMenu:@"FIELD" value:[NSString stringWithUTF8String:customFieldName(static_cast<uint32_t>(prm.customField))] y:182 attrs:small style:style];
    [self drawSlider:@"COUNT" value:[NSString stringWithFormat:@"%u", prm.activeSpeakers] norm:(prm.activeSpeakers - 2.0) / 62.0 y:208 attrs:small style:style];
    [self drawSlider:@"WID" value:[NSString stringWithFormat:@"%.2f", prm.width] norm:prm.width / 1.50f y:234 attrs:small style:style];

    const CGFloat selectedNorm = prm.activeSpeakers > 1u
        ? static_cast<CGFloat>(prm.selectedSpeaker) / static_cast<CGFloat>(prm.activeSpeakers - 1u)
        : 0.0;
    [self drawSlider:@"SEL" value:[NSString stringWithFormat:@"%u", prm.selectedSpeaker + 1u] norm:selectedNorm y:358 attrs:small style:style];
    [self drawSlider:@"AZ" value:@"" norm:(prm.selectedAzimuthDeg + 180.0f) / 360.0f y:384 attrs:small style:style];
    [self drawSlider:@"EL" value:@"" norm:(prm.selectedElevationDeg + 90.0f) / 180.0f y:410 attrs:small style:style];
    [self drawSlider:@"DST" value:@"" norm:(prm.selectedDistance - 0.15f) / 1.85f y:436 attrs:small style:style];
    [self updateValueFields];
    [self drawOpenMenu:small];
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const double n = std::clamp((point.x - 738.0) / 82.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 4: applyParam(*p, kActiveSpeakersParamId, 2.0 + n * 62.0); break;
    case 6: applyParam(*p, kWidthParamId, n * 1.50); break;
    case 7: applyParam(*p, kSelectedSpeakerParamId, 1.0 + n * static_cast<double>(std::max<uint32_t>(1u, p->decoder.params().activeSpeakers) - 1u)); break;
    case 8: applyParam(*p, kAzimuthParamId, -180.0 + n * 360.0); break;
    case 9: applyParam(*p, kElevationParamId, -90.0 + n * 180.0); break;
    case 10: applyParam(*p, kDistanceParamId, 0.15 + n * 1.85); break;
    case 11: applyParam(*p, kSpeakerGainParamId, n * 2.0); break;
    case 13: applyParam(*p, kOutputParamId, -60.0 + n * 72.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const BOOL textFieldHit = NSPointInRect(pt, [_azField frame])
        || NSPointInRect(pt, [_elField frame])
        || NSPointInRect(pt, [_distField frame]);
    BOOL mixerFieldHit = NO;
    for (int i = 0; i < 16; ++i) {
        if (![_gainFields[i] isHidden] && NSPointInRect(pt, [_gainFields[i] frame])) {
            mixerFieldHit = YES;
            break;
        }
    }
    if (!textFieldHit && !mixerFieldHit && [[self window] firstResponder] != self) {
        [[self window] makeFirstResponder:self];
    }
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const CGFloat menuW = 124.0;
        const NSRect menuRect = NSMakeRect(_menuOrigin.x, _menuOrigin.y, menuW, itemH * static_cast<CGFloat>(_menuItemCount));
        if (NSPointInRect(pt, menuRect)) {
            const uint32_t index = std::min<uint32_t>(_menuItemCount - 1u,
                static_cast<uint32_t>((pt.y - _menuOrigin.y) / itemH));
            switch (_openMenu) {
            case 1: applyParam(*p, kLayoutParamId, layoutPresetForMenuIndex(index)); break;
            case 2: applyParam(*p, kModeParamId, index); break;
            case 3: applyParam(*p, kOrderParamId, index + 1u); break;
            case 4: applyParam(*p, kWeightingParamId, index); break;
            case 5: applyParam(*p, kCustomFieldParamId, index); break;
            default: break;
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
    const NSRect fieldPanel = NSMakeRect(18, 42, 596, 556);
    const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
    const NSRect mixerRect = NSMakeRect(34, 76, 564, 506);
    if (_rightPage == 1 && NSPointInRect(pt, mixerRect)) {
        const uint32_t n = std::min<uint32_t>(p->decoder.params().activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
        const int pageCount = static_cast<int>((n + 15u) / 16u);
        if (pageCount > 1) {
            for (int i = 0; i < pageCount; ++i) {
                if (NSPointInRect(pt, [self mixerPageButtonRect:i inRect:mixerRect])) {
                    _mixerPage = i;
                    _dragMixerSpeaker = -1;
                    _dragMixerOutput = NO;
                    [self updateMixerGainFieldsForRect:mixerRect];
                    [self setNeedsDisplay:YES];
                    return;
                }
            }
        }
        if (NSPointInRect(pt, NSInsetRect([self mixerOutputTrackRect:mixerRect], -8.0, -8.0))) {
            _dragMixerOutput = YES;
            [self updateMixerOutput:pt inRect:mixerRect];
            return;
        }
        const uint32_t pageStart = static_cast<uint32_t>(_mixerPage) * 16u;
        const uint32_t pageEnd = std::min<uint32_t>(n, pageStart + 16u);
        for (uint32_t i = pageStart; i < pageEnd; ++i) {
            const uint32_t slot = i - pageStart;
            if (NSPointInRect(pt, NSInsetRect([self mixerMuteRect:slot inRect:mixerRect], -4.0, -4.0))) {
                [self toggleMixerSpeakerMute:i];
                return;
            }
            if (NSPointInRect(pt, NSInsetRect([self mixerSoloRect:slot inRect:mixerRect], -4.0, -4.0))) {
                [self toggleMixerSpeakerSolo:i];
                return;
            }
            if (NSPointInRect(pt, NSInsetRect([self mixerSpeakerRect:i inRect:mixerRect], -6.0, -7.0))) {
                _dragMixerSpeaker = static_cast<int>(i);
                [self updateMixerSpeakerGain:pt inRect:mixerRect];
                return;
            }
        }
    }
    if (NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self fieldPageButtonRect:i inRect:fieldPanel])) {
                _rightPage = i;
                _dragMixerSpeaker = -1;
                _dragMixerOutput = NO;
                if (_rightPage == 1) _hasSpeakerSelection = NO;
                [self updateValueFields];
                [self updateMixerGainFieldsForRect:mixerRect];
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    if ((_rightPage == 0 || _rightPage == 2) && NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:fieldPanel])) {
                const CGFloat step = i == 0 ? -0.15 : 0.15;
                _viewZoom = std::clamp(_viewZoom + step, 0.55, 2.20);
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
        if (_rightPage == 0 && NSPointInRect(pt, fieldRect)) {
            BOOL found = NO;
            const uint32_t best = [self hitSpeakerAt:pt inRect:fieldRect found:&found];
            if (found) {
                applyParam(*p, kSelectedSpeakerParamId, best + 1u);
                _hasSpeakerSelection = YES;
                [self setNeedsDisplay:YES];
                return;
            }
            _hasSpeakerSelection = NO;
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
        if (_rightPage == 2 && NSPointInRect(pt, fieldRect)) {
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(pt, NSMakeRect(738, 77, 102, 17))) { openMenu(1, 8, 96); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 103, 102, 17))) { openMenu(2, 3, 122); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 129, 102, 17))) { openMenu(3, 7, 148); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 155, 102, 17))) { openMenu(4, 3, 174); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 181, 102, 17))) { openMenu(5, 2, 200); return; }
    const CGFloat rows[] = { 208, 234, 358, 384, 410, 436 };
    const int ids[] = { 4, 6, 7, 8, 9, 10 };
    for (int i = 0; i < 6; ++i) {
        if (NSPointInRect(pt, NSMakeRect(638, rows[i] - 8, 230, 24))) {
            _dragSlider = ids[i];
            if (_dragSlider >= 7 && _dragSlider <= 10) _hasSpeakerSelection = YES;
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
    if (_dragMixerSpeaker >= 0) {
        [self updateMixerSpeakerGain:pt inRect:NSMakeRect(34, 76, 564, 506)];
        return;
    }
    if (_dragMixerOutput) {
        [self updateMixerOutput:pt inRect:NSMakeRect(34, 76, 564, 506)];
        return;
    }
    if (_dragSlider > 0) [self updateSlider:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragView = NO; _dragMixerSpeaker = -1; _dragMixerOutput = NO; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G3OAFXSpeakerDecoderView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3G3OAFXSpeakerDecoderView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 900; *h = 620; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,900,620)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3G3OAFXSpeakerDecoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3G3OAFXSpeakerDecoderView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    "org.s3g.s3g-dsp.3oafx-speaker-decoder-64",
    "s3g Ambi Speaker Decoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "1OA-7OA ACN/SN3D ambisonic speaker decoder with 64-channel output and curated s3g layouts.",
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
