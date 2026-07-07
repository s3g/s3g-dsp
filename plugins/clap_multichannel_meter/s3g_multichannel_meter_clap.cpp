#include "s3g_3oafx.h"
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
#include <cstdint>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kChannelCount = 64;
constexpr uint32_t kGuiWidth = 980;
constexpr uint32_t kGuiHeight = 520;
constexpr uint32_t kStateVersion = 3;
constexpr clap_id kVisibleChannelsParamId = 1;
constexpr clap_id kViewModeParamId = 2;
constexpr clap_id kLayoutParamId = 3;
constexpr uint32_t kMeterLayoutCount = 14;

enum class MeterViewMode : uint32_t {
    Grid = 0,
    Field = 1,
    Heat = 2,
};

enum class MeterLayout : uint32_t {
    Auto = 0,
    Cube8 = 1,
    Cube17 = 2,
    Dodeca12 = 3,
    Dome24 = 4,
    Dome25 = 5,
    DoubleRing16 = 6,
    DoubleRing20 = 7,
    OctoRing = 8,
    Quad = 9,
    QuadOh = 10,
    Ring12 = 11,
    Ring16 = 12,
    Sphere24 = 13,
};

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t visibleChannels = kChannelCount;
    uint32_t viewMode = static_cast<uint32_t>(MeterViewMode::Grid);
    uint32_t layout = static_cast<uint32_t>(MeterLayout::Auto);
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    std::array<std::atomic<float>, kChannelCount> peak {};
    std::array<std::atomic<float>, kChannelCount> rms {};
    std::atomic<uint32_t> activeChannels { 0 };
    std::atomic<uint32_t> visibleChannels { kChannelCount };
    std::atomic<uint32_t> viewMode { static_cast<uint32_t>(MeterViewMode::Grid) };
    std::atomic<uint32_t> layout { static_cast<uint32_t>(MeterLayout::Auto) };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

uint32_t meterLayoutSpeakerCount(uint32_t layout)
{
    switch (static_cast<MeterLayout>(std::clamp<uint32_t>(layout, 0u, kMeterLayoutCount - 1u))) {
    case MeterLayout::Quad: return 4u;
    case MeterLayout::QuadOh: return 6u;
    case MeterLayout::Cube8: return 8u;
    case MeterLayout::Cube17: return 17u;
    case MeterLayout::Dome24: return 24u;
    case MeterLayout::Dome25: return 25u;
    case MeterLayout::Sphere24: return 24u;
    case MeterLayout::Dodeca12: return 12u;
    case MeterLayout::OctoRing: return 8u;
    case MeterLayout::Ring12: return 12u;
    case MeterLayout::Ring16: return 16u;
    case MeterLayout::DoubleRing16: return 16u;
    case MeterLayout::DoubleRing20: return 20u;
    case MeterLayout::Auto:
    default: return 0u;
    }
}

void syncVisibleWidthForFieldLayout(Plugin& p)
{
    if (p.viewMode.load(std::memory_order_relaxed) != static_cast<uint32_t>(MeterViewMode::Field)) return;
    const uint32_t count = meterLayoutSpeakerCount(p.layout.load(std::memory_order_relaxed));
    if (count > 0u) p.visibleChannels.store(std::min<uint32_t>(count, kChannelCount), std::memory_order_relaxed);
}

void resetMeters(Plugin& p)
{
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p.peak[ch].store(0.0f, std::memory_order_relaxed);
        p.rms[ch].store(0.0f, std::memory_order_relaxed);
    }
    p.activeChannels.store(0, std::memory_order_relaxed);
}

void applyVisibleChannels(Plugin& p, double value)
{
    p.visibleChannels.store(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, kChannelCount), std::memory_order_relaxed);
}

void applyViewMode(Plugin& p, double value)
{
    p.viewMode.store(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u), std::memory_order_relaxed);
    syncVisibleWidthForFieldLayout(p);
}

void applyLayout(Plugin& p, double value)
{
    p.layout.store(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, kMeterLayoutCount - 1u), std::memory_order_relaxed);
    syncVisibleWidthForFieldLayout(p);
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
    resetMeters(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { resetMeters(*self(plugin)); }

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    if (proc->in_events) {
        const uint32_t n = proc->in_events->size(proc->in_events);
        for (uint32_t i = 0; i < n; ++i) {
            const clap_event_header_t* ev = proc->in_events->get(proc->in_events, i);
            if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
                if (param->param_id == kVisibleChannelsParamId) applyVisibleChannels(*p, param->value);
                if (param->param_id == kViewModeParamId) applyViewMode(*p, param->value);
                if (param->param_id == kLayoutParamId) applyLayout(*p, param->value);
            }
        }
    }
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t channels = std::min({ input.channel_count, output.channel_count, kChannelCount });
    p->activeChannels.store(channels, std::memory_order_relaxed);

    if (output.data32 || output.data64) {
        s3g::clearAudioBufferFromChannel(output, 0, frames);
    }
    if ((!input.data32 && !input.data64) || (!output.data32 && !output.data64) || channels == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    for (uint32_t ch = 0; ch < channels; ++ch) {
        float peak = 0.0f;
        double energy = 0.0;
        if (input.data32 && output.data32 && input.data32[ch] && output.data32[ch]) {
            const float* in = input.data32[ch];
            float* out = output.data32[ch];
            for (uint32_t i = 0; i < frames; ++i) {
                const float sample = in[i];
                out[i] = sample;
                const float a = std::fabs(sample);
                peak = std::max(peak, a);
                energy += static_cast<double>(sample) * static_cast<double>(sample);
            }
        } else if (input.data64 && output.data64 && input.data64[ch] && output.data64[ch]) {
            const double* in = input.data64[ch];
            double* out = output.data64[ch];
            for (uint32_t i = 0; i < frames; ++i) {
                const double sample = in[i];
                out[i] = sample;
                const float a = static_cast<float>(std::fabs(sample));
                peak = std::max(peak, a);
                energy += sample * sample;
            }
        }
        const float rms = frames > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(frames))) : 0.0f;
        const float oldPeak = p->peak[ch].load(std::memory_order_relaxed);
        const float oldRms = p->rms[ch].load(std::memory_order_relaxed);
        p->peak[ch].store(std::max(oldPeak * 0.90f, peak), std::memory_order_relaxed);
        p->rms[ch].store(std::max(oldRms * 0.92f, rms), std::memory_order_relaxed);
    }

    for (uint32_t ch = channels; ch < kChannelCount; ++ch) {
        p->peak[ch].store(p->peak[ch].load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
        p->rms[ch].store(p->rms[ch].load(std::memory_order_relaxed) * 0.94f, std::memory_order_relaxed);
    }

    s3g::clearAudioBufferFromChannel(output, channels, frames);
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

uint32_t paramsCount(const clap_plugin_t*) { return 3; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index > 2) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->module, "Display", sizeof(info->module));
    if (index == 0) {
        info->id = kVisibleChannelsParamId;
        std::strncpy(info->name, "Visible Channels", sizeof(info->name));
        info->min_value = 1.0;
        info->max_value = static_cast<double>(kChannelCount);
        info->default_value = static_cast<double>(kChannelCount);
    } else if (index == 1) {
        info->id = kViewModeParamId;
        std::strncpy(info->name, "Meter View", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 2.0;
        info->default_value = 0.0;
    } else {
        info->id = kLayoutParamId;
        std::strncpy(info->name, "Meter Layout", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = static_cast<double>(kMeterLayoutCount - 1u);
        info->default_value = 0.0;
    }
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* p = self(plugin);
    if (id == kVisibleChannelsParamId) {
        *value = static_cast<double>(p->visibleChannels.load(std::memory_order_relaxed));
        return true;
    }
    if (id == kViewModeParamId) {
        *value = static_cast<double>(p->viewMode.load(std::memory_order_relaxed));
        return true;
    }
    if (id == kLayoutParamId) {
        *value = static_cast<double>(p->layout.load(std::memory_order_relaxed));
        return true;
    }
    return false;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kVisibleChannelsParamId) {
        std::snprintf(display, size, "%u ch", std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, kChannelCount));
        return true;
    }
    if (id == kViewModeParamId) {
        static constexpr const char* names[] = { "GRID", "FIELD", "HEAT" };
        std::snprintf(display, size, "%s", names[std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)]);
        return true;
    }
    if (id == kLayoutParamId) {
        static constexpr const char* names[] = { "AUTO", "CUBE 8", "CUBE 17", "DODECA 12", "DOME 24", "DOME 25", "DBL RING 16", "DBL RING 20", "OCTO RING", "QUAD", "QUAD+OH", "RING 12", "RING 16", "3OAFX 24" };
        std::snprintf(display, size, "%s", names[std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, kMeterLayoutCount - 1u)]);
        return true;
    }
    return false;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kVisibleChannelsParamId) {
        *value = std::atof(display);
        return true;
    }
    if (id == kViewModeParamId) {
        if (std::strstr(display, "FIELD") || std::strstr(display, "field")) *value = 1.0;
        else if (std::strstr(display, "HEAT") || std::strstr(display, "heat")) *value = 2.0;
        else *value = 0.0;
        return true;
    }
    if (id == kLayoutParamId) {
        *value = std::atof(display);
        return true;
    }
    return false;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    if (!in) return;
    auto* p = self(plugin);
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            if (param->param_id == kVisibleChannelsParamId) applyVisibleChannels(*p, param->value);
            if (param->param_id == kViewModeParamId) applyViewMode(*p, param->value);
            if (param->param_id == kLayoutParamId) applyLayout(*p, param->value);
        }
    }
}

const clap_plugin_params_t paramsExt {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state {};
    state.visibleChannels = self(plugin)->visibleChannels.load(std::memory_order_relaxed);
    state.viewMode = self(plugin)->viewMode.load(std::memory_order_relaxed);
    state.layout = self(plugin)->layout.load(std::memory_order_relaxed);
    return stream->write(stream, &state, sizeof(state)) == static_cast<int64_t>(sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0;
    uint32_t visibleChannels = kChannelCount;
    uint32_t viewMode = static_cast<uint32_t>(MeterViewMode::Grid);
    uint32_t layout = static_cast<uint32_t>(MeterLayout::Auto);
    if (stream->read(stream, &version, sizeof(version)) != static_cast<int64_t>(sizeof(version))) return false;
    if (version == 0u || version > kStateVersion) return false;
    if (stream->read(stream, &visibleChannels, sizeof(visibleChannels)) != static_cast<int64_t>(sizeof(visibleChannels))) return false;
    if (version >= 2u) {
        if (stream->read(stream, &viewMode, sizeof(viewMode)) != static_cast<int64_t>(sizeof(viewMode))) return false;
    }
    if (version >= 3u) {
        if (stream->read(stream, &layout, sizeof(layout)) != static_cast<int64_t>(sizeof(layout))) return false;
    }
    self(plugin)->visibleChannels.store(std::clamp<uint32_t>(visibleChannels, 1u, kChannelCount), std::memory_order_relaxed);
    self(plugin)->viewMode.store(std::clamp<uint32_t>(viewMode, 0u, 2u), std::memory_order_relaxed);
    self(plugin)->layout.store(std::clamp<uint32_t>(layout, 0u, kMeterLayoutCount - 1u), std::memory_order_relaxed);
    syncVisibleWidthForFieldLayout(*self(plugin));
    return true;
}

const clap_plugin_state_t stateExt {
    stateSave,
    stateLoad
};

} // namespace

#if defined(__APPLE__)
@interface S3GMultichannelMeterView : NSView {
    void* _plugin;
    NSTimer* _timer;
    BOOL _dragWidth;
    BOOL _dragView;
    int _openMenu;
    int _hoverMenuItem;
    int _fieldViewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    NSPoint _lastDragPoint;
    std::array<std::array<float, 144>, kChannelCount> _history;
    uint32_t _historyWrite;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)refresh:(NSTimer*)timer;
- (void)updateWidthFromPoint:(NSPoint)point;
- (void)advanceHeatHistory;
- (void)setFieldViewPreset:(int)mode;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (NSPoint)projectPoint:(s3g::Vec3)point inRect:(NSRect)rect;
- (void)updateMenuHover:(NSPoint)point;
@end

static NSColor* mmColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static CGFloat normDb(float value)
{
    const float db = 20.0f * std::log10(std::max(0.000001f, value));
    return std::clamp((static_cast<CGFloat>(db) + 60.0) / 60.0, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
}

static NSString* viewName(uint32_t mode)
{
    static NSString* names[] = { @"GRID", @"FIELD", @"HEAT" };
    return names[std::clamp<uint32_t>(mode, 0u, 2u)];
}

static NSString* layoutName(uint32_t layout)
{
    static NSString* names[] = { @"AUTO", @"CUBE 8", @"CUBE 17", @"DODECA 12", @"DOME 24", @"DOME 25", @"DBL RING 16", @"DBL RING 20", @"OCTO RING", @"QUAD", @"QUAD+OH", @"RING 12", @"RING 16", @"3OAFX 24" };
    return names[std::clamp<uint32_t>(layout, 0u, kMeterLayoutCount - 1u)];
}

static void drawMeterMenu(NSString* name,
                          NSString* value,
                          CGFloat y,
                          NSDictionary* attrs,
                          const s3g::clap_gui::Style& style,
                          CGFloat labelX,
                          CGFloat boxX,
                          CGFloat boxW)
{
    [name drawAtPoint:NSMakePoint(labelX, y - 2.0) withAttributes:attrs];
    NSRect box = NSMakeRect(boxX, y - 1.0, boxW, 15.0);
    [mmColor(0x202020) setFill];
    NSRectFill(box);
    [style.grid setStroke];
    NSFrameRect(box);
    [style.fill setFill];
    NSRectFill(NSMakeRect(box.origin.x + 1.0, box.origin.y + 1.0, 2.0, box.size.height - 2.0));
    [value drawAtPoint:NSMakePoint(box.origin.x + 8.0, y - 2.0) withAttributes:attrs];
    [@"v" drawAtPoint:NSMakePoint(box.origin.x + box.size.width - 12.0, y - 2.0) withAttributes:attrs];
}

static constexpr CGFloat kOpenMenuItemH = 20.0;

static CGFloat openMenuX(int menu, NSRect bar)
{
    return menu == 1 ? bar.origin.x + 72.0 : bar.origin.x + 238.0;
}

static CGFloat openMenuWidth(int menu)
{
    return menu == 1 ? 82.0 : 132.0;
}

static uint32_t openMenuCount(int menu)
{
    return menu == 1 ? 3u : kMeterLayoutCount;
}

static NSRect openMenuRect(int menu, NSRect bar)
{
    const uint32_t count = openMenuCount(menu);
    return NSMakeRect(openMenuX(menu, bar), NSMaxY(bar) + 3.0, openMenuWidth(menu), kOpenMenuItemH * static_cast<CGFloat>(count));
}

static NSRect openMenuRowRect(int menu, NSRect bar, uint32_t row)
{
    const NSRect rect = openMenuRect(menu, bar);
    return NSMakeRect(rect.origin.x, rect.origin.y + kOpenMenuItemH * static_cast<CGFloat>(row), rect.size.width, kOpenMenuItemH);
}

static NSColor* meterHeatColor(CGFloat value, CGFloat alpha = 1.0)
{
    return s3g::clap_gui::heatColor(static_cast<double>(value), static_cast<double>(alpha));
}

static NSColor* spatialMeterColor(CGFloat peakNorm, CGFloat rmsNorm)
{
    if (peakNorm > 0.965) return mmColor(0xff2a1d, 0.96);
    if (peakNorm > 0.86) return mmColor(0xff7f18, 0.94);
    if (rmsNorm > 0.66) return mmColor(0xe8d14a, 0.92);
    if (rmsNorm > 0.36) return mmColor(0x65d778, 0.90);
    return mmColor(0x2d7f4a, 0.82);
}

static uint32_t autoLayoutForWidth(uint32_t count)
{
    if (count <= 4u) return static_cast<uint32_t>(MeterLayout::Quad);
    if (count == 6u) return static_cast<uint32_t>(MeterLayout::QuadOh);
    if (count == 8u) return static_cast<uint32_t>(MeterLayout::Cube8);
    if (count == 12u) return static_cast<uint32_t>(MeterLayout::Dodeca12);
    if (count == 16u) return static_cast<uint32_t>(MeterLayout::Ring16);
    if (count == 20u) return static_cast<uint32_t>(MeterLayout::DoubleRing20);
    if (count == 17u) return static_cast<uint32_t>(MeterLayout::Cube17);
    if (count == 25u) return static_cast<uint32_t>(MeterLayout::Dome25);
    return static_cast<uint32_t>(MeterLayout::Dome24);
}

struct MeterFieldLayout {
    uint32_t count = 0;
    std::array<s3g::Vec3, kChannelCount> points {};
};

static s3g::Vec3 speakerVec(const s3g::LayoutPannerSpeaker& speaker)
{
    const auto dir = s3g::directionFromAed(speaker.azimuthDeg, speaker.elevationDeg);
    return { dir.x * speaker.distance, dir.y * speaker.distance, dir.z * speaker.distance };
}

static MeterFieldLayout makeFieldLayout(uint32_t requestedLayout, uint32_t visible)
{
    MeterFieldLayout out {};
    const uint32_t layout = requestedLayout == 0u ? autoLayoutForWidth(visible) : requestedLayout;
    if (layout == static_cast<uint32_t>(MeterLayout::Sphere24)) {
        out.count = s3g::k3OafxVirtualSpeakers;
        for (uint32_t i = 0; i < out.count; ++i) out.points[i] = s3g::k3OafxPoints[i];
        return out;
    }
    s3g::LayoutPanner panner;
    panner.prepare(48000.0);
    s3g::LayoutPannerParams params {};
    switch (static_cast<MeterLayout>(layout)) {
    case MeterLayout::Quad: params.layout = s3g::LayoutPannerPreset::Quad; break;
    case MeterLayout::QuadOh: params.layout = s3g::LayoutPannerPreset::QuadOverhead6; break;
    case MeterLayout::Cube8: params.layout = s3g::LayoutPannerPreset::Cube8; break;
    case MeterLayout::Cube17: params.layout = s3g::LayoutPannerPreset::Cube17; break;
    case MeterLayout::Dome25: params.layout = s3g::LayoutPannerPreset::Dome25; break;
    case MeterLayout::Dodeca12: params.layout = s3g::LayoutPannerPreset::Dodeca12; break;
    case MeterLayout::OctoRing: params.layout = s3g::LayoutPannerPreset::OctophonicRing; break;
    case MeterLayout::Ring12: params.layout = s3g::LayoutPannerPreset::Ring12; break;
    case MeterLayout::Ring16: params.layout = s3g::LayoutPannerPreset::Ring16; break;
    case MeterLayout::DoubleRing16: params.layout = s3g::LayoutPannerPreset::DoubleRing16; break;
    case MeterLayout::DoubleRing20: params.layout = s3g::LayoutPannerPreset::DoubleRing20; break;
    case MeterLayout::Dome24:
    default: params.layout = s3g::LayoutPannerPreset::Dome24NoOverhead; break;
    }
    panner.setParams(params);
    out.count = std::min<uint32_t>(panner.activeSpeakers(), kChannelCount);
    const auto& speakers = panner.speakers();
    for (uint32_t i = 0; i < out.count; ++i) out.points[i] = speakerVec(speakers[i]);
    return out;
}

@implementation S3GMultichannelMeterView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragWidth = NO;
        _dragView = NO;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _fieldViewMode = 2;
        _viewAzDeg = 35.0;
        _viewElDeg = 34.0;
        _viewZoom = 1.0;
        _lastDragPoint = NSMakePoint(0, 0);
        _historyWrite = 0;
        for (auto& lane : _history) lane.fill(0.0f);
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
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/24.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) { [self advanceHeatHistory]; [self setNeedsDisplay:YES]; } }
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 38.0;
    const CGFloat h = 14.0;
    const CGFloat gap = 5.0;
    const CGFloat x = NSMaxX(rect) - 12.0 - (3.0 - static_cast<CGFloat>(index)) * w - (2.0 - static_cast<CGFloat>(index)) * gap;
    return NSMakeRect(x, rect.origin.y + 9.0, w, h);
}
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 18.0;
    const CGFloat h = 14.0;
    const CGFloat gap = 4.0;
    const CGFloat viewStart = [self viewButtonRect:0 inRect:rect].origin.x;
    return NSMakeRect(viewStart - 12.0 - (2.0 - static_cast<CGFloat>(index)) * w - (1.0 - static_cast<CGFloat>(index)) * gap, rect.origin.y + 9.0, w, h);
}
- (void)setFieldViewPreset:(int)mode
{
    _fieldViewMode = mode;
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
- (NSPoint)projectPoint:(s3g::Vec3)point inRect:(NSRect)rect
{
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.34 * _viewZoom;
    const CGFloat cx = rect.origin.x + rect.size.width * 0.50;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.54;
    if (_fieldViewMode == 0) {
        return NSMakePoint(cx - static_cast<CGFloat>(point.y) * scale,
                           cy - static_cast<CGFloat>(point.x) * scale);
    }
    if (_fieldViewMode == 1) {
        return NSMakePoint(cx - static_cast<CGFloat>(point.y) * scale,
                           cy - static_cast<CGFloat>(point.z) * scale);
    }
    const CGFloat yaw = _viewAzDeg * M_PI / 180.0;
    const CGFloat pitch = _viewElDeg * M_PI / 180.0;
    const CGFloat cosYaw = std::cos(yaw);
    const CGFloat sy = std::sin(yaw);
    const CGFloat cp = std::cos(pitch);
    const CGFloat sp = std::sin(pitch);
    const CGFloat x1 = static_cast<CGFloat>(point.x) * cosYaw - static_cast<CGFloat>(point.y) * sy;
    const CGFloat y1 = static_cast<CGFloat>(point.x) * sy + static_cast<CGFloat>(point.y) * cosYaw;
    const CGFloat z1 = static_cast<CGFloat>(point.z);
    const CGFloat y2 = y1 * cp + z1 * sp;
    return NSMakePoint(cx + x1 * scale, cy - y2 * scale);
}
- (void)advanceHeatHistory
{
    if (!_plugin) return;
    auto* p = static_cast<Plugin*>(_plugin);
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        const float value = std::max(p->rms[ch].load(std::memory_order_relaxed), p->peak[ch].load(std::memory_order_relaxed) * 0.35f);
        _history[ch][_historyWrite] = static_cast<float>(normDb(value));
    }
    _historyWrite = (_historyWrite + 1u) % 144u;
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
    NSDictionary* titleAttrs = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:titleFont };
    NSDictionary* lab = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:mono };

    const NSRect bounds = [self bounds];
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const CGFloat viewH = std::max<CGFloat>(420.0, bounds.size.height);

    [@"s3g MULTICHANNEL METER" drawAtPoint:NSMakePoint(18,14) withAttributes:titleAttrs];
    const uint32_t active = p->activeChannels.load(std::memory_order_relaxed);
    const uint32_t visible = std::clamp<uint32_t>(p->visibleChannels.load(std::memory_order_relaxed), 1u, kChannelCount);
    const uint32_t mode = std::clamp<uint32_t>(p->viewMode.load(std::memory_order_relaxed), 0u, 2u);
    const uint32_t selectedLayout = std::clamp<uint32_t>(p->layout.load(std::memory_order_relaxed), 0u, kMeterLayoutCount - 1u);
    [[NSString stringWithFormat:@"%u/%uCH", active, visible] drawAtPoint:NSMakePoint(viewW - 104.0,14) withAttributes:small];

    const NSRect bar = NSMakeRect(18, 38, viewW - 36, 34);
    s3g::clap_gui::drawPanelFrame(bar.origin.x, bar.origin.y, bar.size.width, bar.size.height, style);
    [style.strip setFill]; NSRectFill(NSMakeRect(bar.origin.x, bar.origin.y, bar.size.width, bar.size.height));
    [style.accent setFill]; NSRectFill(NSMakeRect(bar.origin.x, bar.origin.y, bar.size.width, 2));
    drawMeterMenu(@"VIEW",
                  viewName(mode),
                  bar.origin.y + 10.0,
                  small,
                  style,
                  bar.origin.x + 14.0,
                  bar.origin.x + 72.0,
                  82.0);
    drawMeterMenu(@"LAYOUT",
                  layoutName(selectedLayout),
                  bar.origin.y + 10.0,
                  small,
                  style,
                  bar.origin.x + 172.0,
                  bar.origin.x + 238.0,
                  116.0);
    s3g::clap_gui::drawSlider(@"WIDTH",
                               [NSString stringWithFormat:@"%u", visible],
                               static_cast<CGFloat>(visible - 1u) / static_cast<CGFloat>(kChannelCount - 1u),
                               bar.origin.y + 10.0,
                               small,
                               small,
                               style,
                               bar.origin.x + 374.0,
                               bar.origin.x + 430.0,
                               bar.origin.x + 530.0,
                               92.0);
    if (mode == static_cast<uint32_t>(MeterViewMode::Field)) {
        for (int i = 0; i < 2; ++i) {
            s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:bar], bar, i == 0 ? @"-" : @"+", false, small, style);
        }
        static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4" };
        for (int i = 0; i < 3; ++i) {
            s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:bar], bar, viewLabels[i], i == _fieldViewMode, small, style);
        }
    }

    const NSRect panel = NSMakeRect(18, 82, viewW - 36, viewH - 108);
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"METER", true, panel.origin.x, panel.origin.y, panel.size.width, 21, lab, style);

    const CGFloat left = panel.origin.x + 14.0;
    const CGFloat top = panel.origin.y + 38.0;
    const CGFloat w = panel.size.width - 28.0;
    const CGFloat h = panel.size.height - 52.0;
    if (mode == static_cast<uint32_t>(MeterViewMode::Grid)) {
        const uint32_t cols = std::clamp<uint32_t>(static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(visible)))), 1u, 8u);
        const uint32_t rows = static_cast<uint32_t>(std::ceil(static_cast<double>(visible) / static_cast<double>(cols)));
        const CGFloat gapX = 8.0;
        const CGFloat gapY = 7.0;
        const CGFloat cellW = (w - gapX * static_cast<CGFloat>(cols - 1u)) / static_cast<CGFloat>(cols);
        const CGFloat cellH = (h - gapY * static_cast<CGFloat>(rows - 1u)) / static_cast<CGFloat>(rows);

        for (uint32_t ch = 0; ch < visible; ++ch) {
            const uint32_t row = ch / cols;
            const uint32_t col = ch % cols;
            const NSRect cell = NSMakeRect(left + static_cast<CGFloat>(col) * (cellW + gapX),
                                           top + static_cast<CGFloat>(row) * (cellH + gapY),
                                           cellW,
                                           cellH);
            [mmColor(ch < active ? 0x151515 : 0x0f0f0f) setFill]; NSRectFill(cell);
            [mmColor(ch < active ? 0x555555 : 0x2a2a2a) setStroke]; NSFrameRect(cell);

            const float peak = p->peak[ch].load(std::memory_order_relaxed);
            const float rms = p->rms[ch].load(std::memory_order_relaxed);
            const CGFloat peakNorm = normDb(peak);
            const CGFloat rmsNorm = normDb(rms);
            const CGFloat meterX = cell.origin.x + 31.0;
            const CGFloat meterY = cell.origin.y + 12.0;
            const CGFloat meterW = cell.size.width - 43.0;
            const CGFloat meterH = 9.0;
            NSRect track = NSMakeRect(meterX, meterY, meterW, meterH);
            [style.strip setFill]; NSRectFill(track);
            [mmColor(0x393939) setStroke]; NSFrameRect(track);
            NSRect rmsFill = NSInsetRect(track, 1.0, 1.0);
            rmsFill.size.width = std::max<CGFloat>(1.0, rmsFill.size.width * rmsNorm);
            [mmColor(0x8b8b8b) setFill]; NSRectFill(rmsFill);
            const CGFloat peakX = std::clamp(track.origin.x + track.size.width * peakNorm,
                                             track.origin.x + 1.0,
                                             track.origin.x + track.size.width - 2.0);
            [style.text setFill]; NSRectFill(NSMakeRect(peakX, track.origin.y - 2.0, 2.0, track.size.height + 4.0));

            NSString* channelLabel = [NSString stringWithFormat:@"%02u", ch + 1u];
            [channelLabel drawAtPoint:NSMakePoint(cell.origin.x + 8.0, cell.origin.y + 9.0) withAttributes:small];
            NSString* dbLabel = peak > 0.000001f
                ? [NSString stringWithFormat:@"%+.0f", 20.0f * std::log10(std::max(0.000001f, peak))]
                : @"-inf";
            [dbLabel drawAtPoint:NSMakePoint(cell.origin.x + cell.size.width - 36.0, cell.origin.y + 27.0) withAttributes:small];
        }

        [@"RMS  PK" drawAtPoint:NSMakePoint(panel.origin.x + 14.0, NSMaxY(panel) - 22.0) withAttributes:small];
    } else if (mode == static_cast<uint32_t>(MeterViewMode::Field)) {
        const NSRect field = NSMakeRect(left, top, w, h);
        [mmColor(0x111111) setFill]; NSRectFill(field);
        [mmColor(0x3a3a3a) setStroke]; NSFrameRect(field);
        const auto layout = makeFieldLayout(selectedLayout, visible);
        const uint32_t fieldCount = std::min<uint32_t>(layout.count, kChannelCount);
        std::array<NSPoint, kChannelCount> pts {};
        for (uint32_t ch = 0; ch < fieldCount; ++ch) pts[ch] = [self projectPoint:layout.points[ch] inRect:field];
        [mmColor(0x575757, 0.46) setStroke];
        NSBezierPath* links = [NSBezierPath bezierPath];
        auto edge = [&](uint32_t a, uint32_t b) {
            if (a >= fieldCount || b >= fieldCount) return;
            [links moveToPoint:pts[a]];
            [links lineToPoint:pts[b]];
        };
        auto ring = [&](uint32_t base, uint32_t count) {
            if (count < 2u || base >= fieldCount) return;
            count = std::min<uint32_t>(count, fieldCount - base);
            for (uint32_t i = 0; i < count; ++i) edge(base + i, base + ((i + 1u) % count));
        };
        auto meshEqualDistanceEdges = [&](uint32_t count) {
            struct Candidate { uint32_t a; uint32_t b; float d2; };
            std::array<Candidate, kChannelCount * kChannelCount> candidates {};
            uint32_t candidateCount = 0;
            count = std::min<uint32_t>(count, fieldCount);
            for (uint32_t a = 0; a < count; ++a) {
                for (uint32_t b = a + 1u; b < count; ++b) {
                    const float dx = layout.points[a].x - layout.points[b].x;
                    const float dy = layout.points[a].y - layout.points[b].y;
                    const float dz = layout.points[a].z - layout.points[b].z;
                    candidates[candidateCount++] = { a, b, dx * dx + dy * dy + dz * dz };
                }
            }
            if (candidateCount == 0u) return;
            std::sort(candidates.begin(), candidates.begin() + candidateCount,
                [](const Candidate& lhs, const Candidate& rhs) { return lhs.d2 < rhs.d2; });
            const float threshold = candidates[0].d2 * 1.18f;
            for (uint32_t i = 0; i < candidateCount; ++i) {
                if (candidates[i].d2 > threshold) break;
                edge(candidates[i].a, candidates[i].b);
            }
        };
        const uint32_t resolvedLayout = selectedLayout == 0u ? autoLayoutForWidth(visible) : selectedLayout;
        switch (static_cast<MeterLayout>(resolvedLayout)) {
        case MeterLayout::Quad:
            ring(0, 4);
            break;
        case MeterLayout::QuadOh:
            ring(0, 4);
            edge(4, 0); edge(4, 3);
            edge(5, 1); edge(5, 2);
            edge(4, 5);
            break;
        case MeterLayout::Cube8:
            ring(0, 4);
            ring(4, 4);
            for (uint32_t i = 0; i < 4; ++i) edge(i, i + 4u);
            break;
        case MeterLayout::Cube17:
            ring(0, 4);
            edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 8);
            edge(8, 9); edge(9, 10); edge(10, 11); edge(11, 4);
            ring(12, 4);
            edge(0, 4); edge(1, 6); edge(2, 8); edge(3, 10);
            edge(4, 12); edge(6, 13); edge(8, 14); edge(10, 15);
            edge(12, 16); edge(13, 16); edge(14, 16); edge(15, 16);
            break;
        case MeterLayout::Dome24:
        case MeterLayout::Dome25:
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
            if (resolvedLayout == static_cast<uint32_t>(MeterLayout::Dome25)) {
                for (uint32_t i = 0; i < 4; ++i) edge(24, 20u + i);
            }
            break;
        case MeterLayout::Dodeca12:
            meshEqualDistanceEdges(12);
            break;
        case MeterLayout::OctoRing:
            ring(0, 8);
            break;
        case MeterLayout::Ring12:
            ring(0, 12);
            break;
        case MeterLayout::Ring16:
            ring(0, 16);
            break;
        case MeterLayout::DoubleRing16:
            ring(0, 8);
            ring(8, 8);
            for (uint32_t i = 0; i < 8; ++i) edge(i, i + 8u);
            break;
        case MeterLayout::DoubleRing20:
            ring(0, 12);
            ring(12, 8);
            for (uint32_t i = 0; i < 8; ++i) edge(12u + i, (i * 3u / 2u) % 12u);
            break;
        case MeterLayout::Sphere24:
            meshEqualDistanceEdges(fieldCount);
            break;
        case MeterLayout::Auto:
        default:
            if (fieldCount > 2u) ring(0, fieldCount);
            break;
        }
        [links setLineWidth:1.0];
        [links stroke];
        for (uint32_t ch = 0; ch < fieldCount; ++ch) {
            const float peak = p->peak[ch].load(std::memory_order_relaxed);
            const float rms = p->rms[ch].load(std::memory_order_relaxed);
            const CGFloat peakNorm = normDb(peak);
            const CGFloat rmsNorm = normDb(rms);
            const CGFloat level = std::max(rmsNorm, peakNorm * 0.72);
            const NSPoint pt = pts[ch];
            const CGFloat size = 10.0 + 24.0 * level;
            const NSRect square = NSMakeRect(pt.x - size * 0.5, pt.y - size * 0.5, size, size);
            [mmColor(ch < active ? 0x1c1c1c : 0x101010) setFill]; NSRectFill(square);
            [spatialMeterColor(peakNorm, rmsNorm) setFill]; NSRectFill(NSInsetRect(square, 2.0, 2.0));
            [mmColor(peakNorm > 0.965 ? 0xff2a1d : (ch < active ? 0xb8b8b8 : 0x474747)) setStroke]; NSFrameRect(square);
            if (fieldCount <= 32) {
                [[NSString stringWithFormat:@"%u", ch + 1u] drawAtPoint:NSMakePoint(square.origin.x + 17.0, square.origin.y - 1.0) withAttributes:small];
            }
        }
        NSString* fieldLabel = selectedLayout == 0u
            ? [NSString stringWithFormat:@"%@->%@", layoutName(selectedLayout), layoutName(autoLayoutForWidth(visible))]
            : layoutName(selectedLayout);
        [[NSString stringWithFormat:@"%@  %uPT", fieldLabel, fieldCount] drawAtPoint:NSMakePoint(field.origin.x + 10.0, field.origin.y + 8.0) withAttributes:small];
    } else {
        const NSRect heat = NSMakeRect(left, top, w, h);
        [mmColor(0x101010) setFill]; NSRectFill(heat);
        [mmColor(0x3a3a3a) setStroke]; NSFrameRect(heat);
        const CGFloat laneH = heat.size.height / static_cast<CGFloat>(visible);
        const CGFloat colW = heat.size.width / 144.0;
        for (uint32_t ch = 0; ch < visible; ++ch) {
            for (uint32_t x = 0; x < 144; ++x) {
                const uint32_t idx = (_historyWrite + x) % 144u;
                const CGFloat value = std::clamp<CGFloat>(_history[ch][idx], 0.0, 1.0);
                [meterHeatColor(value, ch < active ? 0.96 : 0.25) setFill];
                NSRectFill(NSMakeRect(heat.origin.x + static_cast<CGFloat>(x) * colW,
                                      heat.origin.y + static_cast<CGFloat>(ch) * laneH,
                                      std::ceil(colW) + 0.5,
                                      std::ceil(laneH) + 0.5));
            }
            if (laneH >= 10.0 && ch < 32) {
                [mmColor(0x121212, 0.72) setFill];
                NSRectFill(NSMakeRect(heat.origin.x, heat.origin.y + static_cast<CGFloat>(ch) * laneH, 26.0, laneH));
                [[NSString stringWithFormat:@"%02u", ch + 1u] drawAtPoint:NSMakePoint(heat.origin.x + 5.0, heat.origin.y + static_cast<CGFloat>(ch) * laneH + 1.0) withAttributes:small];
            }
        }
    }

    if (_openMenu > 0) {
        const NSRect menuRect = openMenuRect(_openMenu, bar);
        const uint32_t count = openMenuCount(_openMenu);
        NSString* viewItems[] = { @"GRID", @"FIELD", @"HEAT" };
        NSString* layoutItems[] = { @"AUTO", @"CUBE 8", @"CUBE 17", @"DODECA 12", @"DOME 24", @"DOME 25", @"DBL RING 16", @"DBL RING 20", @"OCTO RING", @"QUAD", @"QUAD+OH", @"RING 12", @"RING 16", @"3OAFX 24" };
        s3g::clap_gui::drawDropdownMenu(menuRect,
                                        kOpenMenuItemH,
                                        _openMenu == 1 ? viewItems : layoutItems,
                                        count,
                                        static_cast<int>(_openMenu == 1 ? mode : selectedLayout),
                                        _hoverMenuItem,
                                        small,
                                        style);
    }
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0) return;
    const NSRect bounds = [self bounds];
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const NSRect bar = NSMakeRect(18, 38, viewW - 36, 34);
    const int next = s3g::clap_gui::dropdownHitIndex(point, openMenuRect(_openMenu, bar), kOpenMenuItemH, openMenuCount(_openMenu));
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)updateWidthFromPoint:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const NSRect bounds = [self bounds];
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const NSRect bar = NSMakeRect(18, 38, viewW - 36, 34);
    const CGFloat trackX = bar.origin.x + 430.0;
    const double n = std::clamp((point.x - trackX) / 92.0, 0.0, 1.0);
    applyVisibleChannels(*p, 1.0 + n * static_cast<double>(kChannelCount - 1u));
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [[self window] makeFirstResponder:self];
    const NSRect bounds = [self bounds];
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const CGFloat viewH = std::max<CGFloat>(420.0, bounds.size.height);
    const NSRect bar = NSMakeRect(18, 38, viewW - 36, 34);
    const NSRect panel = NSMakeRect(18, 82, viewW - 36, viewH - 108);
    const uint32_t mode = std::clamp<uint32_t>(static_cast<Plugin*>(_plugin)->viewMode.load(std::memory_order_relaxed), 0u, 2u);
    if (_openMenu > 0) {
        const uint32_t count = openMenuCount(_openMenu);
        const NSRect menuList = openMenuRect(_openMenu, bar);
        if (NSPointInRect(pt, menuList)) {
            const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuList, kOpenMenuItemH, count);
            if (hit >= 0) {
                const uint32_t index = static_cast<uint32_t>(hit);
                if (_openMenu == 1) applyViewMode(*static_cast<Plugin*>(_plugin), static_cast<double>(index));
                else applyLayout(*static_cast<Plugin*>(_plugin), static_cast<double>(index));
                _openMenu = 0;
                _hoverMenuItem = -1;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
    }
    const NSRect viewMenuBox = NSMakeRect(bar.origin.x + 72.0, bar.origin.y + 9.0, 82.0, 18.0);
    const NSRect layoutMenuBox = NSMakeRect(bar.origin.x + 238.0, bar.origin.y + 9.0, 116.0, 18.0);
    if (NSPointInRect(pt, viewMenuBox)) {
        _openMenu = 1;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, layoutMenuBox)) {
        _openMenu = 2;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (mode == static_cast<uint32_t>(MeterViewMode::Field)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:bar])) {
                _viewZoom = std::clamp<CGFloat>(_viewZoom * (i == 0 ? 0.86 : 1.16), 0.45, 2.75);
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(pt, [self viewButtonRect:i inRect:bar])) {
                [self setFieldViewPreset:i];
                return;
            }
        }
    }
    const NSRect hit = NSMakeRect(bar.origin.x + 424.0, bar.origin.y + 6.0, 126.0, 24.0);
    if (NSPointInRect(pt, hit)) {
        _dragWidth = YES;
        [self updateWidthFromPoint:pt];
        return;
    }
    if (mode == static_cast<uint32_t>(MeterViewMode::Field) && NSPointInRect(pt, panel)) {
        _dragView = YES;
        _lastDragPoint = pt;
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
    if (_dragWidth) [self updateWidthFromPoint:pt];
    if (_dragView) {
        _viewAzDeg += (pt.x - _lastDragPoint.x) * 0.35;
        _viewElDeg = std::clamp<CGFloat>(_viewElDeg + (pt.y - _lastDragPoint.y) * 0.25, -88.0, 88.0);
        _fieldViewMode = 2;
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragWidth = NO;
    _dragView = NO;
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GMultichannelMeterView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible = false; auto* v = static_cast<S3GMultichannelMeterView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = true; hints->can_resize_vertically = true; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = std::clamp<uint32_t>(*w, 720u, 1600u); *h = std::clamp<uint32_t>(*h, 420u, 1000u); return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiWidth,kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GMultichannelMeterView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GMultichannelMeterView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_ANALYZER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.multichannel-meter-64",
    "s3g Multichannel Meter 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "64-channel passthrough meter for REAPER multichannel diagnostics.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    resetMeters(*p);
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
