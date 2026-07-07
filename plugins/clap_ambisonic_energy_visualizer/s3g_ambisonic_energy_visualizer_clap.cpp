#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
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
#include <vector>

namespace {

constexpr uint32_t kChannelCount = s3g::kAmbiSpeakerDecoderMaxChannels;
constexpr uint32_t kGuiWidth = 980;
constexpr uint32_t kGuiHeight = 560;
constexpr uint32_t kEnergyMapCols = 128;
constexpr uint32_t kEnergyMapRows = 64;
constexpr uint32_t kEnergyMapPixels = kEnergyMapCols * kEnergyMapRows;
constexpr uint32_t kEnergyPeakCount = 4;
constexpr uint32_t kEnergyTrailCount = 32;
constexpr float kEnergySilenceThreshold = 0.00001f;
constexpr float kEnergyBlackFloor = 0.0025f;
constexpr uint32_t kStateVersion = 3;
constexpr clap_id kMapParamId = 2;

enum EnergyViewMode : uint32_t {
    kEnergyViewField = 0,
    kEnergyViewPeaks = 1,
    kEnergyViewBlobs = 2,
};

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t order = 3;
    uint32_t mapMode = 0;
    // Kept so older state chunks from the experimental view-mode build can load.
    uint32_t viewMode = kEnergyViewField;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    std::array<std::atomic<float>, kChannelCount> rms {};
    std::array<std::atomic<float>, kChannelCount> peak {};
    std::array<float, kEnergyMapPixels * kChannelCount> projectionBasis {};
    std::array<s3g::Vec3, kEnergyMapPixels> projectionDirection {};
    std::atomic<float> dirX { 0.0f };
    std::atomic<float> dirY { 0.0f };
    std::atomic<float> dirZ { 0.0f };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<uint32_t> activeChannels { 0 };
    std::atomic<uint32_t> mapMode { 0 };
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void resetAnalysis(Plugin& p)
{
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        p.rms[ch].store(0.0f, std::memory_order_relaxed);
        p.peak[ch].store(0.0f, std::memory_order_relaxed);
    }
    p.dirX.store(0.0f, std::memory_order_relaxed);
    p.dirY.store(0.0f, std::memory_order_relaxed);
    p.dirZ.store(0.0f, std::memory_order_relaxed);
    p.inputLevel.store(0.0f, std::memory_order_relaxed);
    p.activeChannels.store(0, std::memory_order_relaxed);
}

void initProjection(Plugin& p)
{
    for (uint32_t y = 0; y < kEnergyMapRows; ++y) {
        const float el = -90.0f + 180.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(kEnergyMapRows);
        for (uint32_t x = 0; x < kEnergyMapCols; ++x) {
            const float az = 180.0f - 360.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(kEnergyMapCols);
            const uint32_t index = y * kEnergyMapCols + x;
            const s3g::Vec3 dir = s3g::directionFromAed(az, el);
            p.projectionDirection[index] = dir;
            const auto basis = s3g::acnSn3dBasis7(dir);
            for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
                p.projectionBasis[index * kChannelCount + ch] = basis[ch];
            }
        }
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kMapParamId) {
        p.mapMode.store(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 3u), std::memory_order_relaxed);
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

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    resetAnalysis(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { resetAnalysis(*self(plugin)); }

void readEvents(Plugin& p, const clap_input_events_t* in)
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
    readEvents(*p, proc->in_events);
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

    float blockDirX = 0.0f;
    float blockDirY = 0.0f;
    float blockDirZ = 0.0f;
    if (channels >= 4u && frames > 0u) {
        if (input.data32 && input.data32[0] && input.data32[1] && input.data32[2] && input.data32[3]) {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            for (uint32_t i = 0; i < frames; ++i) {
                const double w = input.data32[0][i];
                y += w * input.data32[1][i];
                z += w * input.data32[2][i];
                x += w * input.data32[3][i];
            }
            blockDirX = static_cast<float>(x / static_cast<double>(frames));
            blockDirY = static_cast<float>(y / static_cast<double>(frames));
            blockDirZ = static_cast<float>(z / static_cast<double>(frames));
        } else if (input.data64 && input.data64[0] && input.data64[1] && input.data64[2] && input.data64[3]) {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            for (uint32_t i = 0; i < frames; ++i) {
                const double w = input.data64[0][i];
                y += w * input.data64[1][i];
                z += w * input.data64[2][i];
                x += w * input.data64[3][i];
            }
            blockDirX = static_cast<float>(x / static_cast<double>(frames));
            blockDirY = static_cast<float>(y / static_cast<double>(frames));
            blockDirZ = static_cast<float>(z / static_cast<double>(frames));
        }
    }
    p->dirX.store(p->dirX.load(std::memory_order_relaxed) * 0.86f + blockDirX * 0.14f, std::memory_order_relaxed);
    p->dirY.store(p->dirY.load(std::memory_order_relaxed) * 0.86f + blockDirY * 0.14f, std::memory_order_relaxed);
    p->dirZ.store(p->dirZ.load(std::memory_order_relaxed) * 0.86f + blockDirZ * 0.14f, std::memory_order_relaxed);

    float maxInput = 0.0f;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        float peak = 0.0f;
        double energy = 0.0;
        if (input.data32 && output.data32 && input.data32[ch] && output.data32[ch]) {
            const float* in = input.data32[ch];
            float* out = output.data32[ch];
            for (uint32_t i = 0; i < frames; ++i) {
                const float sample = in[i];
                out[i] = sample;
                peak = std::max(peak, std::fabs(sample));
                energy += static_cast<double>(sample) * static_cast<double>(sample);
            }
        } else if (input.data64 && output.data64 && input.data64[ch] && output.data64[ch]) {
            const double* in = input.data64[ch];
            double* out = output.data64[ch];
            for (uint32_t i = 0; i < frames; ++i) {
                const double sample = in[i];
                out[i] = sample;
                peak = std::max(peak, static_cast<float>(std::fabs(sample)));
                energy += sample * sample;
            }
        }
        const float blockRms = frames > 0 ? static_cast<float>(std::sqrt(energy / static_cast<double>(frames))) : 0.0f;
        p->peak[ch].store(std::max(p->peak[ch].load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
        p->rms[ch].store(std::max(p->rms[ch].load(std::memory_order_relaxed) * 0.94f, blockRms), std::memory_order_relaxed);
        maxInput = std::max(maxInput, std::max(blockRms, peak * 0.25f));
    }

    for (uint32_t ch = channels; ch < kChannelCount; ++ch) {
        p->peak[ch].store(p->peak[ch].load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
        p->rms[ch].store(p->rms[ch].load(std::memory_order_relaxed) * 0.94f, std::memory_order_relaxed);
    }
    p->inputLevel.store(std::max(p->inputLevel.load(std::memory_order_relaxed) * 0.86f, maxInput), std::memory_order_relaxed);

    s3g::clearAudioBufferFromChannel(output, channels, frames);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "7OA ACN/SN3D In" : "64ch Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20 : 10;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 1; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= 1) return false;
    std::strncpy(info->module, "Ambisonic Energy", sizeof(info->module));
    if (index == 0) {
        info->id = kMapParamId;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, "Map", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 3.0;
        info->default_value = 0.0;
    }
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* p = self(plugin);
    if (id == kMapParamId) {
        *value = static_cast<double>(p->mapMode.load(std::memory_order_relaxed));
        return true;
    }
    return false;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kMapParamId) {
        static constexpr const char* names[] = { "FIELD", "BLUE", "INK", "VOLT" };
        const uint32_t index = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 3u);
        std::snprintf(display, size, "%s", names[index]);
    } else {
        return false;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kMapParamId) {
        if (std::strstr(display, "BLUE") || std::strstr(display, "blue")) { *value = 1.0; return true; }
        if (std::strstr(display, "INK") || std::strstr(display, "ink")) { *value = 2.0; return true; }
        if (std::strstr(display, "VOLT") || std::strstr(display, "volt")) { *value = 3.0; return true; }
        if (std::strstr(display, "FIELD") || std::strstr(display, "field")) { *value = 0.0; return true; }
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState s {};
    auto* p = self(plugin);
    s.order = s3g::kAmbiSpeakerDecoderMaxOrder;
    s.mapMode = p->mapMode.load(std::memory_order_relaxed);
    s.viewMode = kEnergyViewField;
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->mapMode.store(std::clamp<uint32_t>(s.mapMode, 0u, 3u), std::memory_order_relaxed);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
struct EnergyPeak {
    float x = 0.0f;
    float y = 0.0f;
    float value = 0.0f;
};

struct EnergyTrail {
    float x = 0.0f;
    float y = 0.0f;
    float value = 0.0f;
    float life = 0.0f;
};

@interface S3GEnergyMetalMapView : NSView {
    CAMetalLayer* _metalLayer;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipeline;
    id<MTLTexture> _texture;
    NSUInteger _textureWidth;
    NSUInteger _textureHeight;
    BOOL _ready;
}
- (BOOL)isReady;
- (void)updateMetalLayerGeometry;
- (void)renderPixels:(const uint8_t*)pixels width:(NSUInteger)width height:(NSUInteger)height;
@end

@interface S3GEnergyOverlayView : NSView {
    std::array<EnergyTrail, kEnergyTrailCount> _overlayTrails;
    std::array<EnergyPeak, kEnergyPeakCount> _overlayPeaks;
    uint32_t _viewMode;
}
- (void)setTrails:(const std::array<EnergyTrail, kEnergyTrailCount>&)trails
            peaks:(const std::array<EnergyPeak, kEnergyPeakCount>&)peaks
             view:(uint32_t)viewMode;
@end

@interface S3GAmbisonicEnergyVisualizerView : NSView {
    void* _plugin;
    NSTimer* _timer;
    S3GEnergyMetalMapView* _metalMapView;
    S3GEnergyOverlayView* _overlayView;
    int _openMenu;
    int _hoverMenuItem;
    std::array<float, kEnergyMapPixels> _mapSmooth;
    std::array<float, kEnergyMapPixels> _mapWake;
    std::array<uint8_t, kEnergyMapCols * kEnergyMapRows * 4u> _pixels;
    std::array<EnergyTrail, kEnergyTrailCount> _trails;
    std::array<EnergyPeak, kEnergyPeakCount> _heldPeaks;
    uint32_t _trailCursor;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)refresh:(NSTimer*)timer;
- (void)updateMenuHover:(NSPoint)point;
@end

static NSRect energyHeatRect(NSRect bounds)
{
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const CGFloat viewH = std::max<CGFloat>(430.0, bounds.size.height);
    const NSRect panel = NSMakeRect(18, 82, viewW - 36, viewH - 108);
    return NSMakeRect(panel.origin.x + 14.0, panel.origin.y + 38.0, panel.size.width - 28.0, panel.size.height - 54.0);
}

static NSColor* evColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static CGFloat evNormDb(float value)
{
    const float db = 20.0f * std::log10(std::max(0.000001f, value));
    return std::clamp((static_cast<CGFloat>(db) + 66.0) / 66.0, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
}

static NSString* mapModeName(uint32_t mode)
{
    static NSString* names[] = { @"FIELD", @"BLUE", @"INK", @"VOLT" };
    return names[std::min<uint32_t>(mode, 3u)];
}

static CGFloat heatXFromMap(float x, NSRect heat)
{
    return heat.origin.x + (static_cast<CGFloat>(x) + 0.5) / static_cast<CGFloat>(kEnergyMapCols) * heat.size.width;
}

static CGFloat heatYFromMap(float y, NSRect heat)
{
    const CGFloat flippedY = static_cast<CGFloat>(kEnergyMapRows - 1u) - static_cast<CGFloat>(y);
    return heat.origin.y + (flippedY + 0.5) / static_cast<CGFloat>(kEnergyMapRows) * heat.size.height;
}

static CGFloat localHeatXFromMap(float x, NSRect bounds)
{
    return (static_cast<CGFloat>(x) + 0.5) / static_cast<CGFloat>(kEnergyMapCols) * bounds.size.width;
}

static CGFloat localHeatYFromMap(float y, NSRect bounds)
{
    const CGFloat flippedY = static_cast<CGFloat>(kEnergyMapRows - 1u) - static_cast<CGFloat>(y);
    return (flippedY + 0.5) / static_cast<CGFloat>(kEnergyMapRows) * bounds.size.height;
}

static void blendPixel(std::array<uint8_t, kEnergyMapCols * kEnergyMapRows * 4u>& pixels,
                       int x,
                       int y,
                       uint8_t r,
                       uint8_t g,
                       uint8_t b,
                       float alpha)
{
    if (x < 0 || y < 0 || x >= static_cast<int>(kEnergyMapCols) || y >= static_cast<int>(kEnergyMapRows)) return;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const uint32_t offset = (static_cast<uint32_t>(y) * kEnergyMapCols + static_cast<uint32_t>(x)) * 4u;
    pixels[offset + 0u] = static_cast<uint8_t>(std::lround(static_cast<float>(pixels[offset + 0u]) * (1.0f - alpha) + static_cast<float>(r) * alpha));
    pixels[offset + 1u] = static_cast<uint8_t>(std::lround(static_cast<float>(pixels[offset + 1u]) * (1.0f - alpha) + static_cast<float>(g) * alpha));
    pixels[offset + 2u] = static_cast<uint8_t>(std::lround(static_cast<float>(pixels[offset + 2u]) * (1.0f - alpha) + static_cast<float>(b) * alpha));
    pixels[offset + 3u] = 255u;
}

static void drawPixelCross(std::array<uint8_t, kEnergyMapCols * kEnergyMapRows * 4u>& pixels,
                           float mapX,
                           float mapY,
                           float value,
                           float alpha)
{
    const int x = static_cast<int>(std::lround(mapX));
    const int y = static_cast<int>(std::lround(static_cast<float>(kEnergyMapRows - 1u) - mapY));
    const int radius = std::clamp<int>(1 + static_cast<int>(std::lround(value * 3.0f)), 1, 4);
    for (int dx = -radius; dx <= radius; ++dx) {
        blendPixel(pixels, x + dx, y, 242, 242, 236, alpha);
    }
    for (int dy = -radius; dy <= radius; ++dy) {
        blendPixel(pixels, x, y + dy, 242, 242, 236, alpha);
    }
    const int half = std::max(1, radius / 2);
    for (int yy = y - half; yy <= y + half; ++yy) {
        blendPixel(pixels, x - half, yy, 8, 8, 8, alpha * 0.72f);
        blendPixel(pixels, x + half, yy, 242, 242, 236, alpha);
    }
    for (int xx = x - half; xx <= x + half; ++xx) {
        blendPixel(pixels, xx, y - half, 8, 8, 8, alpha * 0.72f);
        blendPixel(pixels, xx, y + half, 242, 242, 236, alpha);
    }
}

static void mapRgb(float value, uint32_t mode, uint8_t& r, uint8_t& g, uint8_t& b)
{
    struct Stop { float t; uint8_t r; uint8_t g; uint8_t b; };
    static constexpr Stop field[] = {
        { 0.00f, 24, 36, 78 },
        { 0.26f, 34, 128, 170 },
        { 0.52f, 226, 214, 102 },
        { 0.76f, 226, 106, 62 },
        { 1.00f, 206, 38, 40 },
    };
    static constexpr Stop blue[] = {
        { 0.00f, 0, 0, 0 },
        { 0.30f, 16, 52, 104 },
        { 0.62f, 34, 160, 206 },
        { 1.00f, 220, 246, 255 },
    };
    static constexpr Stop ink[] = {
        { 0.00f, 0, 0, 0 },
        { 0.32f, 48, 52, 56 },
        { 0.70f, 154, 160, 164 },
        { 1.00f, 242, 242, 238 },
    };
    static constexpr Stop volt[] = {
        { 0.00f, 0, 0, 0 },
        { 0.24f, 42, 32, 104 },
        { 0.54f, 0, 212, 214 },
        { 0.78f, 238, 236, 98 },
        { 1.00f, 255, 255, 255 },
    };
    const Stop* stops = field;
    size_t count = sizeof(field) / sizeof(field[0]);
    if (mode == 1u) { stops = blue; count = sizeof(blue) / sizeof(blue[0]); }
    else if (mode == 2u) { stops = ink; count = sizeof(ink) / sizeof(ink[0]); }
    else if (mode == 3u) { stops = volt; count = sizeof(volt) / sizeof(volt[0]); }
    value = std::clamp(value, 0.0f, 1.0f);
    const Stop* a = &stops[0];
    const Stop* c = &stops[count - 1];
    for (size_t i = 1; i < count; ++i) {
        if (value <= stops[i].t) {
            a = &stops[i - 1];
            c = &stops[i];
            break;
        }
    }
    const float span = std::max(0.0001f, c->t - a->t);
    const float mix = (value - a->t) / span;
    r = static_cast<uint8_t>(std::lround(static_cast<float>(a->r) + (static_cast<float>(c->r) - static_cast<float>(a->r)) * mix));
    g = static_cast<uint8_t>(std::lround(static_cast<float>(a->g) + (static_cast<float>(c->g) - static_cast<float>(a->g)) * mix));
    b = static_cast<uint8_t>(std::lround(static_cast<float>(a->b) + (static_cast<float>(c->b) - static_cast<float>(a->b)) * mix));
}

static void drawEnergyMenu(NSString* name,
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
    [evColor(0x202020) setFill]; NSRectFill(box);
    [style.grid setStroke]; NSFrameRect(box);
    [style.fill setFill]; NSRectFill(NSMakeRect(box.origin.x + 1.0, box.origin.y + 1.0, 2.0, box.size.height - 2.0));
    [value drawAtPoint:NSMakePoint(box.origin.x + 8.0, y - 2.0) withAttributes:attrs];
    [@"v" drawAtPoint:NSMakePoint(box.origin.x + box.size.width - 12.0, y - 2.0) withAttributes:attrs];
}

@implementation S3GEnergyMetalMapView
- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        _device = MTLCreateSystemDefaultDevice();
        _commandQueue = nil;
        _pipeline = nil;
        _texture = nil;
        _textureWidth = 0;
        _textureHeight = 0;
        _ready = NO;
        if (_device) {
            _metalLayer = [[CAMetalLayer layer] retain];
            _metalLayer.device = _device;
            _metalLayer.pixelFormat = MTLPixelFormatRGBA8Unorm;
            _metalLayer.framebufferOnly = YES;
            _metalLayer.opaque = YES;
            _metalLayer.magnificationFilter = kCAFilterLinear;
            _metalLayer.minificationFilter = kCAFilterLinear;
            [self setWantsLayer:YES];
            [self setLayer:_metalLayer];
            _commandQueue = [_device newCommandQueue];
            NSString* shaderSource =
                @"#include <metal_stdlib>\n"
                 "using namespace metal;\n"
                 "struct Out { float4 position [[position]]; float2 uv; };\n"
                 "vertex Out vertexMain(uint vid [[vertex_id]]) {\n"
                 "  float2 pos[4] = { float2(-1.0, 1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, -1.0) };\n"
                 "  float2 uv[4] = { float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0), float2(1.0, 1.0) };\n"
                 "  Out out; out.position = float4(pos[vid], 0.0, 1.0); out.uv = uv[vid]; return out;\n"
                 "}\n"
                 "fragment float4 fragmentMain(Out in [[stage_in]], texture2d<float> tex [[texture(0)]]) {\n"
                 "  constexpr sampler s(address::clamp_to_edge, filter::linear);\n"
                 "  float2 texel = 1.0 / float2(tex.get_width(), tex.get_height());\n"
                 "  float3 c = tex.sample(s, in.uv).rgb;\n"
                 "  float3 blur = c * 0.40;\n"
                 "  blur += tex.sample(s, in.uv + float2(texel.x, 0.0)).rgb * 0.12;\n"
                 "  blur += tex.sample(s, in.uv - float2(texel.x, 0.0)).rgb * 0.12;\n"
                 "  blur += tex.sample(s, in.uv + float2(0.0, texel.y)).rgb * 0.12;\n"
                 "  blur += tex.sample(s, in.uv - float2(0.0, texel.y)).rgb * 0.12;\n"
                 "  blur += tex.sample(s, in.uv + texel).rgb * 0.03;\n"
                 "  blur += tex.sample(s, in.uv - texel).rgb * 0.03;\n"
                 "  blur += tex.sample(s, in.uv + float2(texel.x, -texel.y)).rgb * 0.03;\n"
                 "  blur += tex.sample(s, in.uv + float2(-texel.x, texel.y)).rgb * 0.03;\n"
                 "  float3 color = mix(blur, c, 0.58);\n"
                 "  color = pow(clamp(color * 1.10, 0.0, 1.0), float3(0.88));\n"
                 "  color = smoothstep(float3(0.0), float3(1.0), color);\n"
                 "  return float4(color, 1.0);\n"
                 "}\n";
            NSError* error = nil;
            id<MTLLibrary> library = [_device newLibraryWithSource:shaderSource options:nil error:&error];
            if (library) {
                MTLRenderPipelineDescriptor* desc = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
                desc.vertexFunction = [library newFunctionWithName:@"vertexMain"];
                desc.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
                desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
                _pipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
                [desc.vertexFunction release];
                [desc.fragmentFunction release];
                _ready = _pipeline != nil && _commandQueue != nil;
                [library release];
            }
        } else {
            _metalLayer = nil;
        }
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)isReady { return _ready; }
- (void)updateMetalLayerGeometry
{
    if (!_metalLayer) return;
    const NSRect bounds = [self bounds];
    CGFloat scale = 1.0;
    if ([self window]) {
        scale = [[self window] backingScaleFactor];
    } else if ([self respondsToSelector:@selector(convertSizeToBacking:)]) {
        const NSSize backing = [self convertSizeToBacking:NSMakeSize(1.0, 1.0)];
        scale = std::max<CGFloat>(1.0, backing.width);
    }
    _metalLayer.contentsScale = scale;
    _metalLayer.bounds = NSMakeRect(0, 0, bounds.size.width, bounds.size.height);
    _metalLayer.drawableSize = NSMakeSize(std::max<CGFloat>(1.0, bounds.size.width * scale),
                                          std::max<CGFloat>(1.0, bounds.size.height * scale));
}
- (void)setFrame:(NSRect)frame
{
    [super setFrame:frame];
    [self updateMetalLayerGeometry];
}
- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [self updateMetalLayerGeometry];
}
- (void)renderPixels:(const uint8_t*)pixels width:(NSUInteger)width height:(NSUInteger)height
{
    if (!_ready || !pixels || width == 0 || height == 0) return;
    [self updateMetalLayerGeometry];
    if (!_texture || _textureWidth != width || _textureHeight != height) {
        [_texture release];
        MTLTextureDescriptor* textureDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                width:width
                                                                                               height:height
                                                                                            mipmapped:NO];
        textureDesc.usage = MTLTextureUsageShaderRead;
        textureDesc.storageMode = MTLStorageModeManaged;
        _texture = [_device newTextureWithDescriptor:textureDesc];
        _textureWidth = width;
        _textureHeight = height;
    }
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [_texture replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:width * 4u];
    id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
    if (!drawable) return;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.02, 0.02, 0.02, 1.0);
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    MTLViewport viewport { 0.0, 0.0, static_cast<double>(drawable.texture.width), static_cast<double>(drawable.texture.height), 0.0, 1.0 };
    [encoder setViewport:viewport];
    [encoder setRenderPipelineState:_pipeline];
    [encoder setFragmentTexture:_texture atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}
- (void)dealloc
{
    [_texture release];
    [_pipeline release];
    [_commandQueue release];
    [_metalLayer release];
    [super dealloc];
}
@end

@implementation S3GEnergyOverlayView
- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        _viewMode = kEnergyViewField;
        [self setWantsLayer:YES];
        [[self layer] setBackgroundColor:[[NSColor clearColor] CGColor]];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (void)setTrails:(const std::array<EnergyTrail, kEnergyTrailCount>&)trails
            peaks:(const std::array<EnergyPeak, kEnergyPeakCount>&)peaks
             view:(uint32_t)viewMode
{
    _overlayTrails = trails;
    _overlayPeaks = peaks;
    _viewMode = viewMode;
    [self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const NSRect b = [self bounds];
    for (const auto& trail : _overlayTrails) {
        if (trail.life < 0.025f) continue;
        const CGFloat px = localHeatXFromMap(trail.x, b);
        const CGFloat py = localHeatYFromMap(trail.y, b);
        const CGFloat alpha = _viewMode == kEnergyViewPeaks
            ? std::clamp<CGFloat>(trail.life * 0.42, 0.03, 0.42)
            : std::clamp<CGFloat>(trail.life * 0.54, 0.04, 0.54);
        const CGFloat size = _viewMode == kEnergyViewPeaks
            ? 3.0 + static_cast<CGFloat>(trail.value) * 11.0
            : 2.5 + static_cast<CGFloat>(trail.value) * 7.0;
        [evColor(0xe8e8e2, alpha) setStroke];
        NSBezierPath* trailPath = [NSBezierPath bezierPath];
        [trailPath setLineWidth:1.0];
        [trailPath moveToPoint:NSMakePoint(px - size, py)];
        [trailPath lineToPoint:NSMakePoint(px + size, py)];
        [trailPath moveToPoint:NSMakePoint(px, py - size)];
        [trailPath lineToPoint:NSMakePoint(px, py + size)];
        [trailPath stroke];
    }
    for (const auto& peak : _overlayPeaks) {
        if (peak.value < 0.025f) continue;
        const CGFloat px = localHeatXFromMap(peak.x, b);
        const CGFloat py = localHeatYFromMap(peak.y, b);
        const CGFloat size = _viewMode == kEnergyViewPeaks
            ? 7.0 + static_cast<CGFloat>(peak.value) * 28.0
            : 5.0 + static_cast<CGFloat>(peak.value) * 10.0;
        const CGFloat alpha = std::clamp<CGFloat>(0.36 + static_cast<CGFloat>(peak.value) * 0.56, 0.36, 0.92);
        if (_viewMode == kEnergyViewPeaks) {
            for (int ring = 2; ring >= 0; --ring) {
                const CGFloat inset = -static_cast<CGFloat>(ring + 1) * size * 0.42;
                NSRect halo = NSInsetRect(NSMakeRect(px - size * 0.5, py - size * 0.5, size, size), inset, inset);
                [evColor(0xd8dedc, alpha * (0.10 + 0.07 * ring)) setStroke];
                NSBezierPath* haloPath = [NSBezierPath bezierPathWithOvalInRect:halo];
                [haloPath setLineWidth:1.0];
                [haloPath stroke];
            }
            [evColor(0xf3f3ee, alpha) setFill];
            NSRect core = NSMakeRect(std::round(px - 2.0), std::round(py - 2.0), 4.0, 4.0);
            NSRectFill(core);
            [evColor(0xf3f3ee, alpha * 0.82) setStroke];
            NSBezierPath* rays = [NSBezierPath bezierPath];
            [rays setLineWidth:1.0];
            [rays moveToPoint:NSMakePoint(px - size * 0.62, py)];
            [rays lineToPoint:NSMakePoint(px + size * 0.62, py)];
            [rays moveToPoint:NSMakePoint(px, py - size * 0.62)];
            [rays lineToPoint:NSMakePoint(px, py + size * 0.62)];
            [rays stroke];
            continue;
        }
        NSRect marker = NSMakeRect(std::round(px - size * 0.5), std::round(py - size * 0.5), std::round(size), std::round(size));
        [evColor(0x101010, 0.68) setFill]; NSRectFill(marker);
        [evColor(0xf3f3ee, alpha) setStroke]; NSFrameRect(marker);
        [evColor(0xf3f3ee, alpha * 0.68) setStroke];
        NSBezierPath* cross = [NSBezierPath bezierPath];
        [cross setLineWidth:1.0];
        [cross moveToPoint:NSMakePoint(px - size * 0.75, py)];
        [cross lineToPoint:NSMakePoint(px + size * 0.75, py)];
        [cross moveToPoint:NSMakePoint(px, py - size * 0.75)];
        [cross lineToPoint:NSMakePoint(px, py + size * 0.75)];
        [cross stroke];
    }
}
@end

@implementation S3GAmbisonicEnergyVisualizerView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _metalMapView = [[S3GEnergyMetalMapView alloc] initWithFrame:NSMakeRect(32, 120, 200, 100)];
        if ([_metalMapView isReady]) {
            [self addSubview:_metalMapView];
            _overlayView = [[S3GEnergyOverlayView alloc] initWithFrame:[_metalMapView frame]];
            [self addSubview:_overlayView positioned:NSWindowAbove relativeTo:_metalMapView];
        } else {
            [_metalMapView release];
            _metalMapView = nil;
            _overlayView = nil;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _mapSmooth.fill(0.0f);
        _mapWake.fill(0.0f);
        _trailCursor = 0;
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
- (void)dealloc { [self stopRefreshTimer]; [_overlayView release]; [_metalMapView release]; [super dealloc]; }
- (void)startRefreshTimer { if (_timer) return; _timer = [NSTimer timerWithTimeInterval:1.0/24.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refresh:(NSTimer*)timer { (void)timer; if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
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

    const NSRect bounds = [self bounds];
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const CGFloat viewH = std::max<CGFloat>(430.0, bounds.size.height);
    const uint32_t order = s3g::kAmbiSpeakerDecoderMaxOrder;
    const uint32_t ambiCh = s3g::ambiChannelsForOrder(order);
    const uint32_t active = p->activeChannels.load(std::memory_order_relaxed);
    const uint32_t mapMode = std::clamp<uint32_t>(p->mapMode.load(std::memory_order_relaxed), 0u, 3u);
    const uint32_t viewMode = kEnergyViewField;
    const NSRect heat = energyHeatRect(bounds);
    if (_metalMapView) {
        [_metalMapView setFrame:heat];
        [_metalMapView setHidden:(_openMenu != 0)];
    }
    if (_overlayView) {
        [_overlayView setFrame:heat];
        [_overlayView setHidden:YES];
    }

    [@"s3g AMBISONIC ENERGY" drawAtPoint:NSMakePoint(18,14) withAttributes:titleAttrs];
    [[NSString stringWithFormat:@"%u/%uCH", active, ambiCh] drawAtPoint:NSMakePoint(viewW - 106.0,14) withAttributes:small];

    const NSRect bar = NSMakeRect(18, 38, viewW - 36, 34);
    s3g::clap_gui::drawPanelFrame(bar.origin.x, bar.origin.y, bar.size.width, bar.size.height, style);
    [style.strip setFill]; NSRectFill(bar);
    [style.accent setFill]; NSRectFill(NSMakeRect(bar.origin.x, bar.origin.y, bar.size.width, 2));
    drawEnergyMenu(@"MAP",
                   mapModeName(mapMode),
                   bar.origin.y + 10.0,
                   small,
                   style,
                   bar.origin.x + 14.0,
                   bar.origin.x + 72.0,
                   86.0);
    const NSRect panel = NSMakeRect(18, 82, viewW - 36, viewH - 108);
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ENERGY MAP", true, panel.origin.x, panel.origin.y, panel.size.width, 21, lab, style);
    [evColor(0x101010) setFill]; NSRectFill(heat);
    [evColor(0x3a3a3a) setStroke]; NSFrameRect(heat);

    std::array<float, kChannelCount> level {};
    const float maxInput = p->inputLevel.load(std::memory_order_relaxed);
    const bool silent = maxInput < kEnergySilenceThreshold;
    const uint32_t ambiActive = std::min<uint32_t>(ambiCh, active);
    for (uint32_t ch = 0; ch < ambiActive; ++ch) {
        const float rms = p->rms[ch].load(std::memory_order_relaxed);
        const float peak = p->peak[ch].load(std::memory_order_relaxed);
        level[ch] = std::max(rms, peak * 0.25f);
    }
    s3g::Vec3 energyDir {
        p->dirX.load(std::memory_order_relaxed),
        p->dirY.load(std::memory_order_relaxed),
        p->dirZ.load(std::memory_order_relaxed)
    };
    const float dirMag = std::sqrt(energyDir.x * energyDir.x + energyDir.y * energyDir.y + energyDir.z * energyDir.z);
    if (dirMag > 0.000001f) {
        energyDir.x /= dirMag;
        energyDir.y /= dirMag;
        energyDir.z /= dirMag;
    } else {
        energyDir = { 1.0f, 0.0f, 0.0f };
    }

    float maxMap = 0.000001f;
    if (!silent && ambiActive > 0u) {
        const float orderNorm = static_cast<float>(order - 1u) / static_cast<float>(std::max<uint32_t>(1u, s3g::kAmbiSpeakerDecoderMaxOrder - 1u));
        const float lobePower = 2.6f + orderNorm * 7.6f;
        const float haloPower = 0.42f + orderNorm * 0.74f;
        const float focusMix = 0.54f + orderNorm * 0.26f;
        const float imageMix = 0.20f - orderNorm * 0.07f;
        float sceneEnergy = 0.0f;
        for (uint32_t ch = 0; ch < ambiActive; ++ch) {
            sceneEnergy += level[ch] * level[ch];
        }
        sceneEnergy = std::sqrt(sceneEnergy);
        for (uint32_t i = 0; i < kEnergyMapPixels; ++i) {
            const float* basis = &p->projectionBasis[i * kChannelCount];
            float directional = 0.0f;
            for (uint32_t ch = 1; ch < ambiActive; ++ch) {
                const float v = level[ch] * basis[ch];
                directional += v * v;
            }
            const float omniFloor = level[0] * level[0] * 0.035f;
            const s3g::Vec3& pixelDir = p->projectionDirection[i];
            const float dot = std::max(0.0f, pixelDir.x * energyDir.x + pixelDir.y * energyDir.y + pixelDir.z * energyDir.z);
            const float focus = std::pow(dot, lobePower) * maxInput * maxInput;
            const float halo = std::pow(dot, haloPower) * sceneEnergy * sceneEnergy * 0.36f;
            const float image = std::sqrt(std::max(0.0f, directional)) * sceneEnergy * 0.22f;
            const float next = focus * focusMix + halo * (1.0f - focusMix) + image * imageMix + omniFloor;
            _mapSmooth[i] = _mapSmooth[i] * 0.64f + next * 0.36f;
            maxMap = std::max(maxMap, _mapSmooth[i]);
        }
    } else {
        for (uint32_t i = 0; i < kEnergyMapPixels; ++i) {
            _mapSmooth[i] *= 0.70f;
            maxMap = std::max(maxMap, _mapSmooth[i]);
        }
    }
    const float activity = silent ? 0.0f : static_cast<float>(evNormDb(maxInput));
    for (uint32_t y = 0; y < kEnergyMapRows; ++y) {
        for (uint32_t x = 0; x < kEnergyMapCols; ++x) {
            const uint32_t index = y * kEnergyMapCols + x;
            const float normalized = (!silent && maxMap > 0.000001f) ? _mapSmooth[index] / maxMap : 0.0f;
            float value = std::pow(std::clamp(normalized * (0.18f + activity * 0.82f), 0.0f, 1.0f), 0.62f);
            value = std::clamp((value - 0.018f) / 0.982f, 0.0f, 1.0f);
            _mapWake[index] = std::max(_mapWake[index] * 0.965f, value);
            const float wake = std::clamp((_mapWake[index] - value * 0.72f) * 1.65f, 0.0f, 1.0f);
            uint8_t r = 0, g = 0, b = 0;
            if (value >= kEnergyBlackFloor) {
                mapRgb(value, mapMode, r, g, b);
            }
            if (wake >= 0.006f) {
                uint8_t wr = 0, wg = 0, wb = 0;
                mapRgb(std::pow(wake, 0.58f), mapMode, wr, wg, wb);
                const float a = std::clamp(0.10f + wake * 0.62f, 0.0f, 0.58f);
                const float invR = 38.0f + static_cast<float>(255u - wr) * 0.86f;
                const float invG = 38.0f + static_cast<float>(255u - wg) * 0.86f;
                const float invB = 38.0f + static_cast<float>(255u - wb) * 0.86f;
                r = static_cast<uint8_t>(std::lround(static_cast<float>(r) * (1.0f - a) + invR * a));
                g = static_cast<uint8_t>(std::lround(static_cast<float>(g) * (1.0f - a) + invG * a));
                b = static_cast<uint8_t>(std::lround(static_cast<float>(b) * (1.0f - a) + invB * a));
            }
            const uint32_t displayY = kEnergyMapRows - 1u - y;
            const uint32_t offset = (displayY * kEnergyMapCols + x) * 4u;
            _pixels[offset + 0u] = r;
            _pixels[offset + 1u] = g;
            _pixels[offset + 2u] = b;
            _pixels[offset + 3u] = 255u;
        }
    }

    std::array<EnergyPeak, kEnergyPeakCount> peaks {};
    uint32_t peakCount = 0;
    if (!silent && maxMap > 0.000001f) {
        constexpr float threshold = 0.035f;
        constexpr int suppressPx = 18;
        for (uint32_t n = 0; n < kEnergyPeakCount; ++n) {
            EnergyPeak best {};
            bool found = false;
            for (uint32_t y = 0; y < kEnergyMapRows; ++y) {
                for (uint32_t x = 0; x < kEnergyMapCols; ++x) {
                    const float value = _mapSmooth[y * kEnergyMapCols + x] / maxMap;
                    if (value < threshold || value <= best.value) continue;
                    bool clear = true;
                    for (uint32_t i = 0; i < peakCount; ++i) {
                        const float dxRaw = std::fabs(static_cast<float>(x) - peaks[i].x);
                        const float dx = std::min(dxRaw, static_cast<float>(kEnergyMapCols) - dxRaw);
                        const float dy = static_cast<float>(y) - peaks[i].y;
                        if (dx * dx + dy * dy < static_cast<float>(suppressPx * suppressPx)) {
                            clear = false;
                            break;
                        }
                    }
                    if (!clear) continue;
                    best.x = static_cast<float>(x);
                    best.y = static_cast<float>(y);
                    best.value = value;
                    found = true;
                }
            }
            if (!found && n > 0) break;
            if (!found) {
                for (uint32_t y = 0; y < kEnergyMapRows; ++y) {
                    for (uint32_t x = 0; x < kEnergyMapCols; ++x) {
                        const float value = _mapSmooth[y * kEnergyMapCols + x] / maxMap;
                        if (value > best.value) {
                            best.x = static_cast<float>(x);
                            best.y = static_cast<float>(y);
                            best.value = value;
                            found = true;
                        }
                    }
                }
            }
            if (!found) break;
            peaks[peakCount++] = best;
        }
    }
    for (uint32_t i = 0; i < kEnergyPeakCount; ++i) {
        if (i < peakCount) {
            _heldPeaks[i].x = peaks[i].x;
            _heldPeaks[i].y = peaks[i].y;
            _heldPeaks[i].value = std::max(peaks[i].value, _heldPeaks[i].value * 0.72f);
        } else {
            _heldPeaks[i].value *= 0.88f;
        }
    }

    for (auto& trail : _trails) {
        trail.life *= 0.78f;
    }
    if (!silent && activity > 0.03f) {
        for (uint32_t i = 0; i < peakCount; ++i) {
            EnergyTrail& trail = _trails[_trailCursor % kEnergyTrailCount];
            trail.x = peaks[i].x;
            trail.y = peaks[i].y;
            trail.value = peaks[i].value;
            trail.life = std::clamp(0.30f + peaks[i].value * activity * 0.88f, 0.0f, 1.0f);
            _trailCursor = (_trailCursor + 1u) % kEnergyTrailCount;
        }
    }

    if (_overlayView) {
        [_overlayView setTrails:_trails peaks:_heldPeaks view:viewMode];
    }

    if (_metalMapView && [_metalMapView isReady] && _openMenu == 0) {
        [_metalMapView renderPixels:_pixels.data() width:kEnergyMapCols height:kEnergyMapRows];
    } else {
        NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                           pixelsWide:kEnergyMapCols
                                                                           pixelsHigh:kEnergyMapRows
                                                                        bitsPerSample:8
                                                                      samplesPerPixel:4
                                                                             hasAlpha:YES
                                                                             isPlanar:NO
                                                                       colorSpaceName:NSCalibratedRGBColorSpace
                                                                          bytesPerRow:kEnergyMapCols * 4
                                                                         bitsPerPixel:32];
        std::memcpy([bitmap bitmapData], _pixels.data(), _pixels.size());
        NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(kEnergyMapCols, kEnergyMapRows)];
        [image addRepresentation:bitmap];
        [bitmap release];
        [[NSGraphicsContext currentContext] setImageInterpolation:NSImageInterpolationNone];
        [image drawInRect:heat fromRect:NSMakeRect(0, 0, kEnergyMapCols, kEnergyMapRows) operation:NSCompositingOperationSourceOver fraction:0.98];
        [image release];
    }

    [evColor(0x3a3a3a) setStroke]; NSFrameRect(heat);

    if (_openMenu == 1) {
        const CGFloat itemH = 20.0;
        const NSRect menu = NSMakeRect(bar.origin.x + 72.0, NSMaxY(bar) + 3.0, 86.0, itemH * 4.0);
        NSString* mapItems[] = { @"FIELD", @"BLUE", @"INK", @"VOLT" };
        s3g::clap_gui::drawDropdownMenu(menu, itemH, mapItems, 4u, static_cast<int>(mapMode), _hoverMenuItem, small, style);
    }
}
- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu != 1) return;
    const NSRect bounds = [self bounds];
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const NSRect bar = NSMakeRect(18, 38, viewW - 36, 34);
    const CGFloat itemH = 20.0;
    const uint32_t count = 4u;
    NSRect menu = NSMakeRect(bar.origin.x + 72.0, NSMaxY(bar) + 3.0, 86.0, itemH * static_cast<CGFloat>(count));
    const int next = s3g::clap_gui::dropdownHitIndex(point, menu, itemH, count);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [[self window] makeFirstResponder:self];
    const NSRect bounds = [self bounds];
    const CGFloat viewW = std::max<CGFloat>(720.0, bounds.size.width);
    const NSRect bar = NSMakeRect(18, 38, viewW - 36, 34);
    if (_openMenu == 1) {
        const CGFloat itemH = 20.0;
        const NSRect menu = NSMakeRect(bar.origin.x + 72.0, NSMaxY(bar) + 3.0, 86.0, itemH * 4.0);
        if (NSPointInRect(pt, menu)) {
            const uint32_t index = std::min<uint32_t>(3u, static_cast<uint32_t>((pt.y - menu.origin.y) / itemH));
            applyParam(*static_cast<Plugin*>(_plugin), kMapParamId, index);
            _openMenu = 0;
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_openMenu > 0) {
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
    }
    if (NSPointInRect(pt, NSMakeRect(bar.origin.x + 72.0, bar.origin.y + 8.0, 76.0, 19.0))) {
        _openMenu = 1;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
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
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbisonicEnergyVisualizerView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GAmbisonicEnergyVisualizerView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = true; hints->can_resize_vertically = true; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = std::clamp<uint32_t>(*w, 720u, 1800u); *h = std::clamp<uint32_t>(*h, 430u, 1100u); return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiWidth,kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbisonicEnergyVisualizerView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicEnergyVisualizerView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    "org.s3g.s3g-dsp.ambisonic-energy-visualizer-64",
    "s3g Ambisonic Energy Visualizer 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "64-channel passthrough analyzer for ACN/SN3D ambisonic energy heatmap visualization.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    initProjection(*p);
    resetAnalysis(*p);
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
