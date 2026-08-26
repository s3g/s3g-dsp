#include "s3g_macro_fracture.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

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

#ifndef S3G_MACRO_FRACTURE_CHANNEL_COUNT
#define S3G_MACRO_FRACTURE_CHANNEL_COUNT 8
#endif

constexpr uint32_t kChannelCount = S3G_MACRO_FRACTURE_CHANNEL_COUNT;
static_assert(kChannelCount > 0u
        && kChannelCount <= s3g::kMacroFractureChannels,
    "S3G_MACRO_FRACTURE_CHANNEL_COUNT is outside the supported range.");
constexpr bool kPassExtraHostChannels = kChannelCount >= 24u;

#ifndef S3G_MACRO_FRACTURE_PLUGIN_ID
#define S3G_MACRO_FRACTURE_PLUGIN_ID \
    "org.s3g.s3g-dsp.macro-fracture-8ch"
#endif
#ifndef S3G_MACRO_FRACTURE_PLUGIN_NAME
#define S3G_MACRO_FRACTURE_PLUGIN_NAME "s3g Macro Fracture 8"
#endif
#ifndef S3G_MACRO_FRACTURE_DESCRIPTION
#define S3G_MACRO_FRACTURE_DESCRIPTION \
    "8-channel macro fracture processor with ten selectable models."
#endif

constexpr uint32_t kStateMagic = 0x3146524du;
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kGuiWidth = kChannelCount == 1u ? 416u : 760u;
constexpr uint32_t kGuiHeight = kChannelCount == 1u ? 498u : 620u;

constexpr clap_id kInputParamId = 1;
constexpr clap_id kProcessorParamId = 2;
constexpr clap_id kAmountParamId = 3;
constexpr clap_id kColorParamId = 4;
constexpr clap_id kBiasParamId = 5;
constexpr clap_id kReactParamId = 6;
constexpr clap_id kMemoryParamId = 7;
constexpr clap_id kSpreadParamId = 8;
constexpr clap_id kDeviationParamId = 9;
constexpr clap_id kSkewParamId = 10;
constexpr clap_id kCenterParamId = 11;
constexpr clap_id kGlideParamId = 12;
constexpr clap_id kMixParamId = 13;
constexpr clap_id kOutputParamId = 14;

struct SavedStateV1 {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    float inputGainDb = 0.0f;
    uint32_t processor = 0u;
    float amount = 0.55f;
    float color = 0.50f;
    float bias = 0.0f;
    float react = 0.25f;
    float memory = 0.0f;
    float spread = 0.0f;
    float deviation = 0.0f;
    float skew = 0.0f;
    float center = 0.5f;
    float glideMs = 250.0f;
    float mix = 0.72f;
    float outputGainDb = -3.0f;
};

static_assert(sizeof(SavedStateV1) == 64u,
    "Macro Fracture state wire layout changed.");

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    s3g::MacroFractureParams params {};
    s3g::MacroFracture fracture;
    std::array<float, kChannelCount> frameIn {};
    std::array<float, kChannelCount> frameOut {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> fractureActivity { 0.0f };
    std::atomic<bool> panicRequested { false };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool paramAffectsTail(clap_id id)
{
    switch (id) {
    case kProcessorParamId:
    case kAmountParamId:
    case kColorParamId:
    case kBiasParamId:
    case kReactParamId:
    case kMemoryParamId:
    case kSpreadParamId:
    case kDeviationParamId:
    case kSkewParamId:
    case kCenterParamId:
    case kGlideParamId:
        return true;
    default:
        return false;
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kInputParamId:
        p.params.inputGainDb =
            static_cast<float>(std::clamp(value, -24.0, 36.0));
        break;
    case kProcessorParamId:
        p.params.processor = static_cast<s3g::FractureProcessor>(
            std::clamp<uint32_t>(
                static_cast<uint32_t>(std::lround(value)), 0u,
                s3g::kFractureProcessorCount - 1u));
        break;
    case kAmountParamId:
        p.params.amount =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kColorParamId:
        p.params.color =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kBiasParamId:
        p.params.bias =
            static_cast<float>(std::clamp(value, -1.0, 1.0));
        break;
    case kReactParamId:
        p.params.react =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kMemoryParamId:
        p.params.memory =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kSpreadParamId:
        p.params.spread =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kDeviationParamId:
        p.params.deviation =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kSkewParamId:
        p.params.skew =
            static_cast<float>(std::clamp(value, -1.0, 1.0));
        break;
    case kCenterParamId:
        p.params.center =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kGlideParamId:
        p.params.glideMs =
            static_cast<float>(std::clamp(value, 10.0, 2000.0));
        break;
    case kMixParamId:
        p.params.mix =
            static_cast<float>(std::clamp(value, 0.0, 1.0));
        break;
    case kOutputParamId:
        p.params.outputGainDb =
            static_cast<float>(std::clamp(value, -60.0, 6.0));
        break;
    default:
        return;
    }
    p.fracture.setParams(p.params);
    if (paramAffectsTail(id)
        && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
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

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->fracture.prepare(sampleRate, kChannelCount);
    p->fracture.setParams(p->params);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->fractureActivity.store(0.0f, std::memory_order_relaxed);
    p->panicRequested.store(false, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->fracture.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->fractureActivity.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t i = 0u; i < count; ++i) {
        const clap_event_header_t* event = events->get(events, i);
        if (!event
            || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) {
            continue;
        }
        const auto* parameter =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(p, parameter->param_id, parameter->value);
    }
}

void finishExtraChannels(const clap_audio_buffer_t& input,
    const clap_audio_buffer_t& output, uint32_t channels,
    uint32_t frames)
{
    for (uint32_t ch = channels; ch < output.channel_count; ++ch) {
        if constexpr (kPassExtraHostChannels) {
            if (ch < input.channel_count) {
                if (output.data32 && output.data32[ch]) {
                    if (input.data32 && input.data32[ch]) {
                        std::memcpy(output.data32[ch], input.data32[ch],
                            sizeof(float) * frames);
                        continue;
                    }
                    if (input.data64 && input.data64[ch]) {
                        for (uint32_t i = 0u; i < frames; ++i) {
                            output.data32[ch][i] =
                                static_cast<float>(input.data64[ch][i]);
                        }
                        continue;
                    }
                }
                if (output.data64 && output.data64[ch]) {
                    if (input.data64 && input.data64[ch]) {
                        std::memcpy(output.data64[ch], input.data64[ch],
                            sizeof(double) * frames);
                        continue;
                    }
                    if (input.data32 && input.data32[ch]) {
                        for (uint32_t i = 0u; i < frames; ++i) {
                            output.data64[ch][i] =
                                static_cast<double>(input.data32[ch][i]);
                        }
                        continue;
                    }
                }
            }
        }
        if (output.data32 && output.data32[ch]) {
            std::fill(output.data32[ch],
                output.data32[ch] + frames, 0.0f);
        }
        if (output.data64 && output.data64[ch]) {
            std::fill(output.data64[ch],
                output.data64[ch] + frames, 0.0);
        }
    }
}

clap_process_status process(
    const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (p->panicRequested.exchange(
            false, std::memory_order_acq_rel)) {
        p->fracture.panic();
    }
    if (proc->audio_inputs_count == 0u
        || proc->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    const uint32_t channels = std::min(
        { input.channel_count, output.channel_count, kChannelCount });
    const uint32_t frames = proc->frames_count;
    if (channels == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_CONTINUE;
    }

    p->fracture.setParams(p->params);
    float blockPeak = 0.0f;
    for (uint32_t i = 0u; i < frames; ++i) {
        for (uint32_t ch = 0u; ch < channels; ++ch) {
            if (input.data32 && input.data32[ch]) {
                p->frameIn[ch] = input.data32[ch][i];
            } else if (input.data64 && input.data64[ch]) {
                p->frameIn[ch] =
                    static_cast<float>(input.data64[ch][i]);
            } else {
                p->frameIn[ch] = 0.0f;
            }
        }
        for (uint32_t ch = channels; ch < kChannelCount; ++ch) {
            p->frameIn[ch] = 0.0f;
        }
        p->fracture.processFrame(
            p->frameIn.data(), p->frameOut.data());
        for (uint32_t ch = 0u; ch < channels; ++ch) {
            if (output.data32 && output.data32[ch]) {
                output.data32[ch][i] = p->frameOut[ch];
            }
            if (output.data64 && output.data64[ch]) {
                output.data64[ch][i] =
                    static_cast<double>(p->frameOut[ch]);
            }
            blockPeak =
                std::max(blockPeak, std::abs(p->frameOut[ch]));
        }
    }
    finishExtraChannels(input, output, channels, frames);
    p->outputPeak.store(std::max(
        p->outputPeak.load(std::memory_order_relaxed) * 0.90f,
        blockPeak), std::memory_order_relaxed);
    p->fractureActivity.store(std::max(
        p->fractureActivity.load(std::memory_order_relaxed) * 0.92f,
        p->fracture.activity()), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name),
        "%uch %s", kChannelCount, isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type =
        kChannelCount == 1u ? CLAP_PORT_MONO : CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

struct ParamDef {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped = false;
};

constexpr ParamDef kParamDefs[] {
    { kProcessorParamId, "Processor", 0.0,
        static_cast<double>(s3g::kFractureProcessorCount - 1u),
        0.0, true },
    { kInputParamId, "Input", -24.0, 36.0, 0.0 },
    { kAmountParamId, "Amount", 0.0, 1.0, 0.55 },
    { kColorParamId, "Color", 0.0, 1.0, 0.50 },
    { kBiasParamId, "Bias", -1.0, 1.0, 0.0 },
    { kReactParamId, "React", 0.0, 1.0, 0.25 },
    { kMemoryParamId, "Memory", 0.0, 1.0, 0.0 },
#if S3G_MACRO_FRACTURE_CHANNEL_COUNT > 1
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.0 },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.0 },
    { kSkewParamId, "Skew", -1.0, 1.0, 0.0 },
    { kCenterParamId, "Center", 0.0, 1.0, 0.5 },
    { kGlideParamId, "Glide", 10.0, 2000.0, 250.0 },
#endif
    { kMixParamId, "Mix", 0.0, 1.0, 0.72 },
    { kOutputParamId, "Out", -60.0, 6.0, -3.0 },
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(
        sizeof(kParamDefs) / sizeof(kParamDefs[0]));
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& definition = kParamDefs[index];
    info->id = definition.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (definition.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::snprintf(info->name, sizeof(info->name),
        "%s", definition.name);
    std::snprintf(info->module, sizeof(info->module),
        "%s", "Macro Fracture");
    info->min_value = definition.minimum;
    info->max_value = definition.maximum;
    info->default_value = definition.defaultValue;
    return true;
}

bool paramsGetValue(
    const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& p = self(plugin)->params;
    switch (id) {
    case kInputParamId: *value = p.inputGainDb; return true;
    case kProcessorParamId:
        *value = static_cast<uint32_t>(p.processor);
        return true;
    case kAmountParamId: *value = p.amount; return true;
    case kColorParamId: *value = p.color; return true;
    case kBiasParamId: *value = p.bias; return true;
    case kReactParamId: *value = p.react; return true;
    case kMemoryParamId: *value = p.memory; return true;
    case kSpreadParamId: *value = p.spread; return true;
    case kDeviationParamId: *value = p.deviation; return true;
    case kSkewParamId: *value = p.skew; return true;
    case kCenterParamId: *value = p.center; return true;
    case kGlideParamId: *value = p.glideMs; return true;
    case kMixParamId: *value = p.mix; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id,
    double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kProcessorParamId) {
        const uint32_t index = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 0u,
            s3g::kFractureProcessorCount - 1u);
        std::snprintf(display, size, "%s",
            s3g::fractureProcessorName(
                static_cast<s3g::FractureProcessor>(index)));
    } else if (id == kInputParamId || id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kGlideParamId) {
        std::snprintf(display, size, "%.0f ms", value);
    } else if (id == kBiasParamId || id == kSkewParamId) {
        std::snprintf(display, size, "%+.2f", value);
    } else {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kProcessorParamId) {
        for (uint32_t index = 0u;
             index < s3g::kFractureProcessorCount; ++index) {
            if (std::strcmp(display,
                    s3g::fractureProcessorName(
                        static_cast<s3g::FractureProcessor>(index)))
                == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    *value = std::atof(display);
    if (std::strchr(display, '%')) *value *= 0.01;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input,
    const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool writeStateBytes(
    const clap_ostream_t* stream, const void* source, uint64_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(source);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t written =
            stream->write(stream, bytes + offset, size - offset);
        if (written <= 0) return false;
        offset += static_cast<uint64_t>(written);
    }
    return true;
}

bool readStateBytes(
    const clap_istream_t* stream, void* destination, uint64_t size)
{
    auto* bytes = static_cast<uint8_t*>(destination);
    uint64_t offset = 0u;
    while (offset < size) {
        const int64_t read =
            stream->read(stream, bytes + offset, size - offset);
        if (read <= 0) return false;
        offset += static_cast<uint64_t>(read);
    }
    return true;
}

bool stateSave(
    const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto& p = self(plugin)->params;
    SavedStateV1 state;
    state.inputGainDb = p.inputGainDb;
    state.processor = static_cast<uint32_t>(p.processor);
    state.amount = p.amount;
    state.color = p.color;
    state.bias = p.bias;
    state.react = p.react;
    state.memory = p.memory;
    state.spread = p.spread;
    state.deviation = p.deviation;
    state.skew = p.skew;
    state.center = p.center;
    state.glideMs = p.glideMs;
    state.mix = p.mix;
    state.outputGainDb = p.outputGainDb;
    return writeStateBytes(stream, &state, sizeof(state));
}

bool stateLoad(
    const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedStateV1 state;
    if (!readStateBytes(stream, &state, sizeof(state))
        || state.magic != kStateMagic
        || state.version != kStateVersion) {
        return false;
    }
    auto* p = self(plugin);
    p->params.inputGainDb = state.inputGainDb;
    p->params.processor =
        static_cast<s3g::FractureProcessor>(state.processor);
    p->params.amount = state.amount;
    p->params.color = state.color;
    p->params.bias = state.bias;
    p->params.react = state.react;
    p->params.memory = state.memory;
    p->params.spread = state.spread;
    p->params.deviation = state.deviation;
    p->params.skew = state.skew;
    p->params.center = state.center;
    p->params.glideMs = state.glideMs;
    p->params.mix = state.mix;
    p->params.outputGainDb = state.outputGainDb;
    p->fracture.setParams(p->params);
    p->params = p->fracture.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    if (!p || p->params.mix <= 0.0001f) return 0u;
    const double seconds = 0.10
        + static_cast<double>(p->params.memory) * 2.5;
    return static_cast<uint32_t>(
        std::ceil(seconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)
#if S3G_MACRO_FRACTURE_CHANNEL_COUNT == 1
#define S3GMacroFractureView S3GMacroFractureMonoView
#elif S3G_MACRO_FRACTURE_CHANNEL_COUNT == 8
#define S3GMacroFractureView S3GMacroFracture8View
#else
#define S3GMacroFractureView S3GMacroFracture24View
#endif

@interface S3GMacroFractureView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm y:(CGFloat)y
    panel:(const s3g::gui_layout::Panel&)panel
    attrs:(NSDictionary*)attrs;
- (void)drawRelationshipPreview:
    (const s3g::MacroFractureParams&)params
    rect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
@end

static NSColor* fractureColor(int rgb)
{
    return s3g::clap_gui::color(rgb);
}

@implementation S3GMacroFractureView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:
        NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _timer = nil;
        std::snprintf(_titlePresetName,
            sizeof(_titlePresetName), "%s", "INIT");
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
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
        target:self selector:@selector(refresh:)
        userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop]
        addTimer:_timer forMode:NSRunLoopCommonModes];
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
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawSlider:(NSString*)name value:(NSString*)value
    norm:(CGFloat)norm y:(CGFloat)y
    panel:(const s3g::gui_layout::Panel&)panel
    attrs:(NSDictionary*)attrs
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y, panel.frame.x, panel.frame.width,
        attrs, attrs, style);
}

- (void)drawRelationshipPreview:
    (const s3g::MacroFractureParams&)params
    rect:(NSRect)rect attrs:(NSDictionary*)attrs
{
    [fractureColor(0x111111) setFill];
    NSRectFill(rect);
    [fractureColor(0x444444) setStroke];
    NSFrameRect(rect);
    const CGFloat baseY = rect.origin.y + 28.0;
    const CGFloat rowHeight = (rect.size.height - 38.0)
        / static_cast<CGFloat>(std::max<uint32_t>(1u, kChannelCount));
    const CGFloat labelX = rect.origin.x + 10.0;
    const CGFloat barX = rect.origin.x + 48.0;
    const CGFloat barWidth = rect.size.width - 64.0;
    for (uint32_t ch = 0u; ch < kChannelCount; ++ch) {
        const float u = kChannelCount > 1u
            ? static_cast<float>(ch)
                / static_cast<float>(kChannelCount - 1u)
            : 0.5f;
        const float centered =
            std::clamp((u - params.center) * 2.0f, -1.0f, 1.0f);
        uint32_t hash = ch * 747796405u + 2891336453u;
        hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash)
            * 277803737u;
        hash = (hash >> 22u) ^ hash;
        const float random =
            static_cast<float>(hash & 0xffffu) / 32767.5f - 1.0f;
        const float laneColor = std::clamp(params.color
            + centered * params.spread * 0.42f
            + random * params.deviation * 0.18f, 0.0f, 1.0f);
        const float laneBias = std::clamp(params.bias
            + params.skew * (u - 0.5f) * 0.65f
            + random * params.deviation * 0.18f, -1.0f, 1.0f);
        const CGFloat y = baseY + static_cast<CGFloat>(ch) * rowHeight;
        const uint32_t labelStride =
            std::max<uint32_t>(1u, (kChannelCount + 7u) / 8u);
        if (ch % labelStride == 0u || ch + 1u == kChannelCount) {
            [[NSString stringWithFormat:@"L%u", ch + 1u]
                drawAtPoint:NSMakePoint(labelX, y - 4.0)
                withAttributes:attrs];
        }
        const NSRect track =
            NSMakeRect(barX, y, barWidth, 6.0);
        [fractureColor(0x171717) setFill];
        NSRectFill(track);
        [fractureColor(0x333333) setStroke];
        NSFrameRect(track);
        const CGFloat colorX = track.origin.x + 2.0
            + (track.size.width - 4.0) * laneColor;
        [fractureColor(0xb8b8b8) setFill];
        NSRectFill(NSMakeRect(
            colorX - 2.0, track.origin.y - 2.0, 4.0, 10.0));
        const CGFloat biasX = track.origin.x + 2.0
            + (track.size.width - 4.0) * (laneBias + 1.0f) * 0.5f;
        [fractureColor(0x6f6f6f) setFill];
        NSRectFill(NSMakeRect(
            biasX - 1.0, track.origin.y, 2.0, 6.0));
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* label = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    const auto& family = kChannelCount == 1u
        ? s3g::gui_layout::kMacroShredMonoFamilyLayout
        : s3g::gui_layout::kMacroShredFamilyLayout;
    const auto titleBand =
        s3g::gui_layout::macroTitleBand(family.canvas);
    NSString* title = kChannelCount == 1u
        ? @"s3g MACRO FRACTURE MONO"
        : [NSString stringWithFormat:
            @"s3g MACRO FRACTURE %uCH", kChannelCount];
    s3g::clap_gui::drawMacroTitleBand(
        title,
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);

    const auto drawPanel = [&](NSString* name,
        const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.frame.x, panel.frame.y,
            panel.frame.width, panel.frame.height, style);
        s3g::clap_gui::drawPanelHeader(
            name, true, panel.frame.x, panel.frame.y,
            panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            label, style);
    };
    drawPanel(@"OUTPUT", family.output);
    drawPanel(@"ENGINE", family.engine);
    drawPanel(@"FRACTURE ACTIVITY", family.containment);
    if constexpr (kChannelCount > 1u) {
        drawPanel(@"RELATIONSHIPS", family.relationships);
        drawPanel(@"LANE COLOR / BIAS", family.preview);
    }

    const auto& prm = p->params;
    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB", prm.outputGainDb]
        norm:(prm.outputGainDb + 60.0f) / 66.0f
        y:s3g::gui_layout::rowY(family.output, 0u)
        panel:family.output attrs:small];
    [self drawSlider:@"MIX"
        value:[NSString stringWithFormat:@"%.0f%%", prm.mix * 100.0f]
        norm:prm.mix
        y:s3g::gui_layout::rowY(family.output, 1u)
        panel:family.output attrs:small];
    s3g::clap_gui::drawProcessorMenu(
        @"PROCESSOR",
        [NSString stringWithUTF8String:
            s3g::fractureProcessorName(prm.processor)],
        s3g::gui_layout::rowY(family.engine, 0u),
        family.engine.frame.x, family.engine.frame.width,
        label, small, style);
    [self drawSlider:@"INPUT"
        value:[NSString stringWithFormat:@"%+.1f dB", prm.inputGainDb]
        norm:(prm.inputGainDb + 24.0f) / 60.0f
        y:s3g::gui_layout::rowY(family.engine, 1u)
        panel:family.engine attrs:small];
    [self drawSlider:
        [NSString stringWithUTF8String:
            s3g::fractureAmountLabel(prm.processor)]
        value:[NSString stringWithFormat:@"%.0f%%", prm.amount * 100.0f]
        norm:prm.amount
        y:s3g::gui_layout::rowY(family.engine, 2u)
        panel:family.engine attrs:small];
    [self drawSlider:
        [NSString stringWithUTF8String:
            s3g::fractureColorLabel(prm.processor)]
        value:[NSString stringWithFormat:@"%.0f%%", prm.color * 100.0f]
        norm:prm.color
        y:s3g::gui_layout::rowY(family.engine, 3u)
        panel:family.engine attrs:small];
    [self drawSlider:
        [NSString stringWithUTF8String:
            s3g::fractureBiasLabel(prm.processor)]
        value:[NSString stringWithFormat:@"%+.2f", prm.bias]
        norm:(prm.bias + 1.0f) * 0.5f
        y:s3g::gui_layout::rowY(family.engine, 4u)
        panel:family.engine attrs:small];
    [self drawSlider:@"REACT"
        value:[NSString stringWithFormat:@"%.0f%%", prm.react * 100.0f]
        norm:prm.react
        y:s3g::gui_layout::rowY(family.engine, 5u)
        panel:family.engine attrs:small];
    [self drawSlider:@"MEMORY"
        value:[NSString stringWithFormat:@"%.0f%%", prm.memory * 100.0f]
        norm:prm.memory
        y:s3g::gui_layout::rowY(family.engine, 6u)
        panel:family.engine attrs:small];

    if constexpr (kChannelCount > 1u) {
        [self drawSlider:@"SPRD"
            value:[NSString stringWithFormat:@"%.0f%%", prm.spread * 100.0f]
            norm:prm.spread
            y:s3g::gui_layout::rowY(family.relationships, 0u)
            panel:family.relationships attrs:small];
        [self drawSlider:@"DEV"
            value:[NSString stringWithFormat:@"%.0f%%", prm.deviation * 100.0f]
            norm:prm.deviation
            y:s3g::gui_layout::rowY(family.relationships, 1u)
            panel:family.relationships attrs:small];
        [self drawSlider:@"SKW"
            value:[NSString stringWithFormat:@"%+.2f", prm.skew]
            norm:(prm.skew + 1.0f) * 0.5f
            y:s3g::gui_layout::rowY(family.relationships, 2u)
            panel:family.relationships attrs:small];
        [self drawSlider:@"CTR"
            value:[NSString stringWithFormat:@"%.0f%%", prm.center * 100.0f]
            norm:prm.center
            y:s3g::gui_layout::rowY(family.relationships, 3u)
            panel:family.relationships attrs:small];
        [self drawSlider:@"GLD"
            value:[NSString stringWithFormat:@"%.0f ms", prm.glideMs]
            norm:(prm.glideMs - 10.0f) / 1990.0f
            y:s3g::gui_layout::rowY(family.relationships, 4u)
            panel:family.relationships attrs:small];
        [self drawRelationshipPreview:prm
            rect:NSMakeRect(
                family.preview.frame.x + 12.0,
                family.preview.frame.y + 32.0,
                family.preview.frame.width - 24.0,
                family.preview.frame.height - 44.0)
            attrs:small];

        const NSRect field =
            s3g::clap_gui::cocoaRect(family.containmentField);
        [fractureColor(0x111111) setFill];
        NSRectFill(field);
        [fractureColor(0x444444) setStroke];
        NSFrameRect(field);
        const CGFloat activity = std::clamp<CGFloat>(
            p->fractureActivity.load(std::memory_order_relaxed),
            0.0, 1.0);
        NSBezierPath* crack = [NSBezierPath bezierPath];
        [crack setLineWidth:1.0 + activity * 2.0];
        [crack moveToPoint:NSMakePoint(
            NSMidX(field), field.origin.y + 12.0)];
        for (int segment = 1; segment <= 8; ++segment) {
            const CGFloat y = field.origin.y + 12.0
                + (field.size.height - 24.0)
                    * static_cast<CGFloat>(segment) / 8.0;
            const CGFloat side = segment % 2 == 0 ? -1.0 : 1.0;
            const CGFloat x = NSMidX(field) + side
                * (12.0 + activity * 58.0)
                * (0.35 + 0.65
                    * static_cast<CGFloat>(segment) / 8.0);
            [crack lineToPoint:NSMakePoint(x, y)];
        }
        [fractureColor(0xb8b8b8) setStroke];
        [crack stroke];
        [@"BOUNDED RECURRENCE" drawAtPoint:
            NSMakePoint(field.origin.x + 12.0,
                NSMaxY(field) - 26.0)
            withAttributes:small];
    }

    const NSRect meter =
        s3g::clap_gui::cocoaRect(family.containmentMeter);
    [fractureColor(0x111111) setFill];
    NSRectFill(meter);
    [fractureColor(0x444444) setStroke];
    NSFrameRect(meter);
    NSRect meterFill = NSInsetRect(meter, 1.0, 1.0);
    meterFill.size.width *= std::clamp<CGFloat>(
        p->fractureActivity.load(std::memory_order_relaxed),
        0.0, 1.0);
    [fractureColor(0xb8b8b8) setFill];
    NSRectFill(meterFill);
    [@"ACTIVITY" drawAtPoint:NSMakePoint(
        family.containment.frame.x + 16.0,
        family.containmentMeter.y - 2.0)
        withAttributes:label];

    const NSRect panicRect =
        s3g::clap_gui::cocoaRect(family.panicButton);
    [fractureColor(0x161616) setFill];
    NSRectFill(panicRect);
    [fractureColor(0x565656) setStroke];
    NSFrameRect(panicRect);
    const NSSize panicSize =
        [@"PANIC" sizeWithAttributes:label];
    [@"PANIC" drawAtPoint:NSMakePoint(
        panicRect.origin.x
            + (panicRect.size.width - panicSize.width) * 0.5,
        panicRect.origin.y
            + (panicRect.size.height - panicSize.height) * 0.5)
        withAttributes:label];

    if (_openMenu == 1) {
        static NSString* processorItems[] = {
            @"RELAY", @"CRUSH", @"SPLICE", @"LOGIC", @"VOID",
            @"THROAT", @"ROBOT", @"OCT DOWN", @"OCT UP", @"OCT STACK"
        };
        static_assert(std::size(processorItems)
            == s3g::kFractureProcessorCount);
        constexpr CGFloat itemHeight = 17.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(
                family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 0u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.engine.frame.width),
            itemHeight * static_cast<CGFloat>(
                s3g::kFractureProcessorCount));
        s3g::clap_gui::drawDropdownMenu(
            menuRect, itemHeight, processorItems,
            s3g::kFractureProcessorCount,
            static_cast<int>(prm.processor), -1,
            small, style);
    }
}

- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto& family = kChannelCount == 1u
        ? s3g::gui_layout::kMacroShredMonoFamilyLayout
        : s3g::gui_layout::kMacroShredFamilyLayout;
    const bool outputSlider =
        _dragSlider == kMixParamId
        || _dragSlider == kOutputParamId;
    const bool engineSlider =
        _dragSlider == kInputParamId
        || _dragSlider == kAmountParamId
        || _dragSlider == kColorParamId
        || _dragSlider == kBiasParamId
        || _dragSlider == kReactParamId
        || _dragSlider == kMemoryParamId;
    const auto& panel = outputSlider ? family.output
        : (engineSlider ? family.engine : family.relationships);
    const double controlX =
        s3g::gui_layout::processorControlX(panel.frame.x);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(panel.frame.width);
    const double norm = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    switch (_dragSlider) {
    case kInputParamId:
        applyParam(*p, kInputParamId, -24.0 + norm * 60.0);
        break;
    case kAmountParamId: applyParam(*p, kAmountParamId, norm); break;
    case kColorParamId: applyParam(*p, kColorParamId, norm); break;
    case kBiasParamId:
        applyParam(*p, kBiasParamId, -1.0 + norm * 2.0);
        break;
    case kReactParamId: applyParam(*p, kReactParamId, norm); break;
    case kMemoryParamId: applyParam(*p, kMemoryParamId, norm); break;
    case kSpreadParamId: applyParam(*p, kSpreadParamId, norm); break;
    case kDeviationParamId:
        applyParam(*p, kDeviationParamId, norm);
        break;
    case kSkewParamId:
        applyParam(*p, kSkewParamId, -1.0 + norm * 2.0);
        break;
    case kCenterParamId: applyParam(*p, kCenterParamId, norm); break;
    case kGlideParamId:
        applyParam(*p, kGlideParamId, 10.0 + norm * 1990.0);
        break;
    case kMixParamId: applyParam(*p, kMixParamId, norm); break;
    case kOutputParamId:
        applyParam(*p, kOutputParamId, -60.0 + norm * 66.0);
        break;
    default:
        break;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point =
        [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto& family = kChannelCount == 1u
        ? s3g::gui_layout::kMacroShredMonoFamilyLayout
        : s3g::gui_layout::kMacroShredFamilyLayout;
    if (_openMenu == 1) {
        constexpr CGFloat itemHeight = 17.0;
        const NSRect menuRect = NSMakeRect(
            s3g::gui_layout::processorControlX(
                family.engine.frame.x),
            s3g::gui_layout::rowY(family.engine, 0u) + 17.0,
            s3g::gui_layout::processorMenuWidth(
                family.engine.frame.width),
            itemHeight * static_cast<CGFloat>(
                s3g::kFractureProcessorCount));
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, menuRect, itemHeight,
            s3g::kFractureProcessorCount);
        _openMenu = 0;
        if (hit >= 0) {
            applyParam(*p, kProcessorParamId,
                static_cast<double>(hit));
        }
        [self setNeedsDisplay:YES];
        return;
    }

    const auto titleBand =
        s3g::gui_layout::macroTitleBand(family.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            point, &p->plugin, @"Macro Fracture", titleBand,
            _titlePresetName, sizeof(_titlePresetName),
            kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(family.panicButton))) {
        p->panicRequested.store(true, std::memory_order_release);
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect processorBox = NSMakeRect(
        s3g::gui_layout::processorControlX(family.engine.frame.x),
        s3g::gui_layout::rowY(family.engine, 0u) - 5.0,
        s3g::gui_layout::processorMenuWidth(
            family.engine.frame.width),
        24.0);
    if (NSPointInRect(point, processorBox)) {
        _openMenu = 1;
        [self setNeedsDisplay:YES];
        return;
    }

    const clap_id outputIds[] = {
        kOutputParamId, kMixParamId
    };
    const clap_id engineIds[] = {
        kInputParamId, kAmountParamId, kColorParamId,
        kBiasParamId, kReactParamId, kMemoryParamId
    };
    const clap_id relationshipIds[] = {
        kSpreadParamId, kDeviationParamId, kSkewParamId,
        kCenterParamId, kGlideParamId
    };
    const auto beginSlider = [&](clap_id parameterId) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, parameterId, &defaultValue)) {
            applyParam(*p, parameterId, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(parameterId);
            [self updateSlider:point];
        }
        [self setNeedsDisplay:YES];
    };
    for (uint32_t row = 0u; row < 2u; ++row) {
        if (NSPointInRect(point,
                s3g::clap_gui::cocoaRect(
                    s3g::gui_layout::sliderHitRect(
                        family.output, row)))) {
            beginSlider(outputIds[row]);
            return;
        }
    }
    for (uint32_t row = 0u; row < 6u; ++row) {
        if (NSPointInRect(point,
                s3g::clap_gui::cocoaRect(
                    s3g::gui_layout::sliderHitRect(
                        family.engine, row + 1u)))) {
            beginSlider(engineIds[row]);
            return;
        }
    }
    if constexpr (kChannelCount > 1u) {
        for (uint32_t row = 0u; row < 5u; ++row) {
            if (NSPointInRect(point,
                    s3g::clap_gui::cocoaRect(
                        s3g::gui_layout::sliderHitRect(
                            family.relationships, row)))) {
                beginSlider(relationshipIds[row]);
                return;
            }
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragSlider > 0) {
        [self updateSlider:
            [self convertPoint:[event locationInWindow] fromView:nil]];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api,
    bool isFloating)
{
    return !isFloating
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*,
    const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin,
    const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView =
        [[S3GMacroFractureView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(
            p->guiViewport, static_cast<NSView*>(p->guiView),
            kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible = false;
        [static_cast<S3GMacroFractureView*>(p->guiView)
            stopRefreshTimer];
        s3g::clap_gui::destroyResponsiveViewport(
            p->guiViewport, p->guiView);
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport,
        kGuiWidth, kGuiHeight, width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*,
    clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport,
        kGuiWidth, kGuiHeight, width, height);
}

bool guiSetSize(const clap_plugin_t* plugin,
    uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin,
    const clap_window_t* window)
{
    if (!window
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        p->guiViewport, static_cast<NSView*>(window->cocoa),
        p->host);
}

bool guiSetTransient(const clap_plugin_t*,
    const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView
        || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) {
        return false;
    }
    p->guiVisible = true;
    [static_cast<S3GMacroFractureView*>(p->guiView)
        startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GMacroFractureView*>(p->guiView)
        stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

#endif

const void* pluginGetExtension(
    const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_DISTORTION,
#if S3G_MACRO_FRACTURE_CHANNEL_COUNT == 1
    CLAP_PLUGIN_FEATURE_MONO,
#else
    CLAP_PLUGIN_FEATURE_SURROUND,
#endif
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    S3G_MACRO_FRACTURE_PLUGIN_ID,
    S3G_MACRO_FRACTURE_PLUGIN_NAME,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    S3G_MACRO_FRACTURE_DESCRIPTION,
    features
};

const clap_plugin_t* createPlugin(
    const clap_plugin_factory*, const clap_host_t* host,
    const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->hostTail = host && host->get_extension
        ? static_cast<const clap_host_tail_t*>(
            host->get_extension(host, CLAP_EXT_TAIL))
        : nullptr;
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

uint32_t factoryGetPluginCount(
    const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
