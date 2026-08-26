#include "s3g_delay_field.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = 2u;
constexpr uint32_t kOutputChannels = s3g::kDelayFieldHostChannels;
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kGuiWidth = 760u;
constexpr uint32_t kGuiHeight = 520u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* label;
    const char* module;
    double min;
    double max;
    double def;
    const char* unit;
    clap_param_info_flags flags = CLAP_PARAM_IS_AUTOMATABLE;
};

constexpr clap_param_info_flags kStepped =
    CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;

constexpr ParamDef kParamDefs[] {
    { 1u, "Model", "MODEL", "Field", 0.0, 3.0, 0.0, "model", kStepped },
    { 2u, "Format", "FORMAT", "Field", 0.0, 3.0, 0.0, "format", kStepped },
    { 3u, "Out Rotate", "OUT ROTATE", "Field", -180.0, 180.0, 0.0, "deg" },

    { 101u, "Density", "DENSITY", "Shard", 0.2, 24.0, 5.0, "" },
    { 102u, "Grain", "GRAIN", "Shard", 20.0, 900.0, 180.0, "ms" },
    { 103u, "Guard", "GUARD", "Shard", 20.0, 1800.0, 260.0, "ms" },
    { 104u, "Scatter", "SCATTER", "Shard", 0.0, 2500.0, 700.0, "ms" },
    { 105u, "Pitch", "PITCH", "Shard", -2.0, 2.0, 1.0, "" },
    { 106u, "Pitch Spread", "PITCH SPREAD", "Shard", 0.0, 1.5, 0.15, "" },
    { 107u, "Rotate", "ROTATE", "Shard", -4.0, 4.0, 0.18, "" },
    { 108u, "Width", "WIDTH", "Shard", 0.0, 1.0, 0.18, "pct" },
    { 109u, "Feedback", "FEEDBACK", "Shard", 0.0, 0.72, 0.12, "pct" },
    { 110u, "Freeze", "FREEZE", "Shard", 0.0, 1.0, 0.0, "pct" },
    { 111u, "Direct", "DIRECT", "Shard", 0.0, 1.0, 0.08, "pct" },
    { 112u, "Wet", "WET", "Shard", 0.0, 1.0, 0.9, "pct" },
    { 113u, "Gain", "GAIN", "Shard", -60.0, 12.0, 0.0, "db" },
    { 114u, "Stereo", "STEREO", "Shard", 0.0, 1.0, 1.0, "pct" },

    { 201u, "Position", "POSITION", "Orbit", 1.0, 16.0, 1.0, "" },
    { 202u, "Spread", "SPREAD", "Orbit", 0.05, 16.0, 3.8, "" },
    { 203u, "Rotate", "ROTATE", "Orbit", -8.0, 8.0, 0.03, "" },
    { 204u, "Width", "WIDTH", "Orbit", 0.0, 16.0, 1.2, "" },
    { 205u, "Focus", "FOCUS", "Orbit", 0.1, 8.0, 1.5, "" },
    { 206u, "Stereo", "STEREO", "Orbit", 0.0, 1.0, 1.0, "pct" },
    { 207u, "Delay", "DELAY", "Orbit", 5.0, 1800.0, 180.0, "ms" },
    { 208u, "Feedback", "FEEDBACK", "Orbit", 0.0, 0.82, 0.35, "pct" },
    { 209u, "Orbit", "ORBIT", "Orbit", -6.0, 6.0, 1.0, "" },
    { 210u, "Damping", "DAMPING", "Orbit", 0.0, 1.0, 0.35, "pct" },
    { 211u, "Wet", "WET", "Orbit", 0.0, 1.0, 0.72, "pct" },
    { 212u, "Gain", "GAIN", "Orbit", -60.0, 12.0, 0.0, "db" },

    { 301u, "Position", "POSITION", "Cascade", 1.0, 16.0, 1.0, "" },
    { 302u, "Rotate", "ROTATE", "Cascade", -4.0, 4.0, 0.0, "" },
    { 303u, "Direction", "DIRECTION", "Cascade", -1.0, 1.0, 1.0, "" },
    { 304u, "First Delay", "FIRST DELAY", "Cascade", 1.0, 1000.0, 25.0, "ms" },
    { 305u, "Delay Step", "DELAY STEP", "Cascade", 1.0, 500.0, 85.0, "ms" },
    { 306u, "Decay", "DECAY", "Cascade", 0.0, 0.98, 0.78, "pct" },
    { 307u, "Damping", "DAMPING", "Cascade", 0.0, 1.0, 0.25, "pct" },
    { 308u, "Direct", "DIRECT", "Cascade", 0.0, 1.0, 0.12, "pct" },
    { 309u, "Wet", "WET", "Cascade", 0.0, 1.0, 1.0, "pct" },
    { 310u, "Gain", "GAIN", "Cascade", -60.0, 12.0, 0.0, "db" },
    { 311u, "Stereo", "STEREO", "Cascade", 0.0, 1.0, 1.0, "pct" },
    { 312u, "Smoothing", "SMOOTHING", "Cascade", 0.0, 1.0, 0.35, "pct" },

    { 401u, "Delay", "DELAY", "Iterate", 10.0, 4000.0, 260.0, "ms" },
    { 402u, "Delay Random", "DELAY RANDOM", "Iterate", 0.0, 1.0, 0.25, "pct" },
    { 403u, "Window", "WINDOW", "Iterate", 20.0, 2500.0, 420.0, "ms" },
    { 404u, "Pitch Random", "PITCH RANDOM", "Iterate", 0.0, 24.0, 2.0, "st" },
    { 405u, "Level Random", "LEVEL RANDOM", "Iterate", 0.0, 1.0, 0.15, "pct" },
    { 406u, "Fade", "FADE", "Iterate", 0.0, 0.95, 0.08, "pct" },
    { 407u, "Direct", "DIRECT", "Iterate", 0.0, 1.0, 0.1, "pct" },
    { 408u, "Wet", "WET", "Iterate", 0.0, 1.0, 0.9, "pct" },
    { 409u, "Gain", "GAIN", "Iterate", -60.0, 12.0, 0.0, "db" },
    { 410u, "Traversal", "TRAVERSAL", "Iterate", 0.0, 4.0, 4.0, "route", kStepped },
    { 411u, "Stereo Events", "STEREO", "Iterate", 0.0, 1.0, 1.0, "bool", kStepped },
    { 412u, "Split Stereo", "SPLIT", "Iterate", 0.0, 1.0, 0.0, "bool", kStepped },
    { 413u, "Avoid Adjacent", "APART", "Iterate", 0.0, 1.0, 0.0, "bool", kStepped },
    { 414u, "Seed", "SEED", "Iterate", 1.0, 4294967295.0, 1979.0, "seed", kStepped },
};

constexpr uint32_t kParamCount = static_cast<uint32_t>(
    sizeof(kParamDefs) / sizeof(kParamDefs[0]));
constexpr uint32_t kGlobalParamCount = 3u;
constexpr uint32_t kModelStarts[] { 3u, 17u, 29u, 41u };
constexpr uint32_t kModelCounts[] { 14u, 12u, 12u, 14u };

const ParamDef* findParam(clap_id id)
{
    for (const auto& def : kParamDefs) if (def.id == id) return &def;
    return nullptr;
}

#define S3G_DELAY_FIELD_FLOAT_PARAMS(X) \
    X(101u, params.shard.density) \
    X(102u, params.shard.grainMs) \
    X(103u, params.shard.guardMs) \
    X(104u, params.shard.scatterMs) \
    X(105u, params.shard.pitch) \
    X(106u, params.shard.pitchSpread) \
    X(107u, params.shard.rotate) \
    X(108u, params.shard.width) \
    X(109u, params.shard.feedback) \
    X(110u, params.shard.freeze) \
    X(111u, params.shard.dry) \
    X(112u, params.shard.wet) \
    X(113u, params.shard.gainDb) \
    X(114u, params.shard.stereo) \
    X(201u, params.orbit.pos) \
    X(202u, params.orbit.spread) \
    X(203u, params.orbit.rotate) \
    X(204u, params.orbit.width) \
    X(205u, params.orbit.focus) \
    X(206u, params.orbit.stereo) \
    X(207u, params.orbit.delayMs) \
    X(208u, params.orbit.feedback) \
    X(209u, params.orbit.orbit) \
    X(210u, params.orbit.damp) \
    X(211u, params.orbit.wet) \
    X(212u, params.orbit.gainDb) \
    X(301u, params.cascade.pos) \
    X(302u, params.cascade.rotate) \
    X(303u, params.cascade.direction) \
    X(304u, params.cascade.baseMs) \
    X(305u, params.cascade.stepMs) \
    X(306u, params.cascade.decay) \
    X(307u, params.cascade.damp) \
    X(308u, params.cascade.dry) \
    X(309u, params.cascade.wet) \
    X(310u, params.cascade.gainDb) \
    X(311u, params.cascade.stereo) \
    X(312u, params.cascade.soft) \
    X(401u, params.iterate.delayMs) \
    X(402u, params.iterate.delayRandom) \
    X(403u, params.iterate.windowMs) \
    X(404u, params.iterate.pitchRandomSemitones) \
    X(405u, params.iterate.amplitudeRandom) \
    X(406u, params.iterate.fade) \
    X(407u, params.iterate.dry) \
    X(408u, params.iterate.wet) \
    X(409u, params.iterate.gainDb)

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t parameterCount = kParamCount;
    double values[kParamCount] {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::DelayFieldParams params {};
    s3g::DelayField dsp;
    std::vector<float> inputLeft;
    std::vector<float> inputRight;
    std::vector<std::vector<float>> output;
    std::vector<float*> outputPointers;
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, kOutputChannels> sourcePeaks {};
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

void applyParam(Plugin& plugin, clap_id id, double value)
{
    const ParamDef* def = findParam(id);
    if (!def || !std::isfinite(value)) return;
    const double clamped = std::clamp(value, def->min, def->max);
    auto& params = plugin.params;
    switch (id) {
    case 1u:
        params.model = s3g::sanitizeDelayFieldModel(static_cast<uint32_t>(
            std::llround(clamped)));
        break;
    case 2u:
        params.format = s3g::sanitizeFixedBusRingFormat(static_cast<uint32_t>(
            std::llround(clamped)));
        break;
    case 3u:
        params.outputRotationDegrees = static_cast<float>(clamped);
        break;
#define S3G_SET_FLOAT(ID, FIELD) case ID: FIELD = static_cast<float>(clamped); break;
    S3G_DELAY_FIELD_FLOAT_PARAMS(S3G_SET_FLOAT)
#undef S3G_SET_FLOAT
    case 410u:
        params.iterate.traversal = static_cast<uint32_t>(std::llround(clamped));
        break;
    case 411u:
        params.iterate.stereoEvents = clamped >= 0.5;
        break;
    case 412u:
        params.iterate.splitStereo = clamped >= 0.5;
        break;
    case 413u:
        params.iterate.avoidAdjacent = clamped >= 0.5;
        break;
    case 414u:
        params.iterate.seed = static_cast<uint32_t>(std::llround(clamped));
        break;
    default:
        break;
    }
    plugin.dsp.setParams(params);
}

bool getParamValue(const s3g::DelayFieldParams& params, clap_id id,
                   double* value)
{
    if (!value) return false;
    switch (id) {
    case 1u: *value = static_cast<uint32_t>(params.model); return true;
    case 2u: *value = static_cast<uint32_t>(params.format); return true;
    case 3u: *value = params.outputRotationDegrees; return true;
#define S3G_GET_FLOAT(ID, FIELD) case ID: *value = FIELD; return true;
    S3G_DELAY_FIELD_FLOAT_PARAMS(S3G_GET_FLOAT)
#undef S3G_GET_FLOAT
    case 410u: *value = params.iterate.traversal; return true;
    case 411u: *value = params.iterate.stereoEvents ? 1.0 : 0.0; return true;
    case 412u: *value = params.iterate.splitStereo ? 1.0 : 0.0; return true;
    case 413u: *value = params.iterate.avoidAdjacent ? 1.0 : 0.0; return true;
    case 414u: *value = params.iterate.seed; return true;
    default: return false;
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

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t,
              uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    try {
        p->inputLeft.assign(p->maxFrames, 0.0f);
        p->inputRight.assign(p->maxFrames, 0.0f);
        p->output.assign(kOutputChannels,
            std::vector<float>(p->maxFrames, 0.0f));
        p->outputPointers.resize(kOutputChannels);
    } catch (...) {
        return false;
    }
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel)
        p->outputPointers[channel] = p->output[channel].data();
    if (!p->dsp.prepare(sampleRate, p->maxFrames)) return false;
    p->dsp.setParams(p->params);
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->dsp.reset(); }

void readParamEvents(Plugin& plugin, const clap_input_events_t* input)
{
    if (!input) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(plugin, param->param_id, param->value);
    }
}

clap_process_status process(const clap_plugin_t* plugin,
                            const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    if (process->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    const auto* input = process->audio_inputs_count > 0u
        ? &process->audio_inputs[0] : nullptr;
    const auto& output = process->audio_outputs[0];
    const uint32_t frames = std::min(process->frames_count, p->maxFrames);
    if (frames == 0u || output.channel_count < kOutputChannels)
        return CLAP_PROCESS_CONTINUE;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        if (input && input->channel_count > 0u && input->data32
            && input->data32[0])
            p->inputLeft[frame] = input->data32[0][frame];
        else if (input && input->channel_count > 0u && input->data64
                 && input->data64[0])
            p->inputLeft[frame] = static_cast<float>(input->data64[0][frame]);
        else p->inputLeft[frame] = 0.0f;
        if (input && input->channel_count > 1u && input->data32
            && input->data32[1])
            p->inputRight[frame] = input->data32[1][frame];
        else if (input && input->channel_count > 1u && input->data64
                 && input->data64[1])
            p->inputRight[frame] = static_cast<float>(input->data64[1][frame]);
        else p->inputRight[frame] = p->inputLeft[frame];
    }
    p->dsp.setParams(p->params);
    p->dsp.process(p->inputLeft.data(), p->inputRight.data(),
        p->outputPointers.data(), frames);
    float inputBlockPeak = 0.0f;
    for (uint32_t frame = 0u; frame < frames; ++frame)
        inputBlockPeak = std::max(inputBlockPeak, std::max(
            std::abs(p->inputLeft[frame]), std::abs(p->inputRight[frame])));
    float blockPeak = 0.0f;
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = s3g::flushDenormal(p->output[channel][frame]);
            if (output.data32 && output.data32[channel])
                output.data32[channel][frame] = value;
            if (output.data64 && output.data64[channel])
                output.data64[channel][frame] = value;
            blockPeak = std::max(blockPeak, std::abs(value));
        }
    }
    for (uint32_t channel = kOutputChannels; channel < output.channel_count;
         ++channel) {
        if (output.data32 && output.data32[channel])
            std::fill_n(output.data32[channel], frames, 0.0f);
        if (output.data64 && output.data64[channel])
            std::fill_n(output.data64[channel], frames, 0.0);
    }
    const float previous = p->outputPeak.load(std::memory_order_relaxed);
    p->outputPeak.store(std::max(previous * 0.90f, blockPeak),
        std::memory_order_relaxed);
    const float previousInput = p->inputPeak.load(std::memory_order_relaxed);
    p->inputPeak.store(std::max(previousInput * 0.90f, inputBlockPeak),
        std::memory_order_relaxed);
    const auto& sourcePeaks = p->dsp.sourcePeaks();
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel) {
        const float previousSource = p->sourcePeaks[channel].load(
            std::memory_order_relaxed);
        p->sourcePeaks[channel].store(std::max(previousSource * 0.88f,
            sourcePeaks[channel]), std::memory_order_relaxed);
    }
    return CLAP_PROCESS_CONTINUE;
}
void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Stereo In" : "16ch Format Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_STEREO : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
                   clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = def.flags;
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}
bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    return getParamValue(self(plugin)->params, id, value);
}

const char* traversalName(uint32_t traversal)
{
    switch (static_cast<s3g::routing::OutputTraversal>(traversal)) {
    case s3g::routing::OutputTraversal::Sequential: return "SEQUENTIAL";
    case s3g::routing::OutputTraversal::ReverseSequential: return "REVERSE";
    case s3g::routing::OutputTraversal::Palindrome: return "PALINDROME";
    case s3g::routing::OutputTraversal::Random: return "RANDOM";
    case s3g::routing::OutputTraversal::RandomCycle: return "RANDOM CYCLE";
    }
    return "RANDOM CYCLE";
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
                       char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    const ParamDef* def = findParam(id);
    if (!def) return false;
    if (id == 1u) {
        std::snprintf(display, size, "%s", s3g::delayFieldModelName(
            s3g::sanitizeDelayFieldModel(static_cast<uint32_t>(
                std::llround(value)))));
    } else if (id == 2u) {
        std::snprintf(display, size, "%s", s3g::fixedBusRingFormatName(
            s3g::sanitizeFixedBusRingFormat(static_cast<uint32_t>(
                std::llround(value)))));
    } else if (id == 410u) {
        std::snprintf(display, size, "%s", traversalName(static_cast<uint32_t>(
            std::llround(value))));
    } else if (std::strcmp(def->unit, "bool") == 0) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else if (std::strcmp(def->unit, "ms") == 0) {
        std::snprintf(display, size, "%.0f ms", value);
    } else if (std::strcmp(def->unit, "db") == 0) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (std::strcmp(def->unit, "pct") == 0) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (std::strcmp(def->unit, "deg") == 0) {
        std::snprintf(display, size, "%+.0f deg", value);
    } else if (std::strcmp(def->unit, "st") == 0) {
        std::snprintf(display, size, "%.1f st", value);
    } else if (std::strcmp(def->unit, "seed") == 0) {
        std::snprintf(display, size, "%u", static_cast<uint32_t>(
            std::llround(value)));
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display,
                       double* value)
{
    if (!display || !value) return false;
    const ParamDef* def = findParam(id);
    if (!def) return false;
    double parsed = std::atof(display);
    if (id == 1u) {
        if (std::strcmp(display, "SHARD") == 0) parsed = 0.0;
        else if (std::strcmp(display, "ORBIT") == 0) parsed = 1.0;
        else if (std::strcmp(display, "CASCADE") == 0) parsed = 2.0;
        else if (std::strcmp(display, "ITERATE") == 0) parsed = 3.0;
    } else if (id == 2u) {
        if (std::strcmp(display, "16CH DIRECT") == 0) parsed = 0.0;
        else if (std::strcmp(display, "8CH RING") == 0) parsed = 1.0;
        else if (std::strcmp(display, "QUAD RING") == 0) parsed = 2.0;
        else if (std::strcmp(display, "STEREO RING") == 0) parsed = 3.0;
    } else if (id == 410u) {
        if (std::strcmp(display, "SEQUENTIAL") == 0) parsed = 0.0;
        else if (std::strcmp(display, "REVERSE") == 0) parsed = 1.0;
        else if (std::strcmp(display, "PALINDROME") == 0) parsed = 2.0;
        else if (std::strcmp(display, "RANDOM") == 0) parsed = 3.0;
        else if (std::strcmp(display, "RANDOM CYCLE") == 0) parsed = 4.0;
    } else if (id == 411u || id == 412u || id == 413u) {
        parsed = std::strcmp(display, "ON") == 0 ? 1.0 : 0.0;
    } else if (std::strchr(display, '%')) {
        parsed *= 0.01;
    }
    *value = std::clamp(parsed, def->min, def->max);
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input,
                 const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}
const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state {};
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        if (!getParamValue(self(plugin)->params, kParamDefs[index].id,
                &state.values[index])) return false;
    }
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!s3g::clap_state::readAll(stream, &state, sizeof(state))
        || state.version != kStateVersion
        || state.parameterCount != kParamCount) return false;
    auto* p = self(plugin);
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        if (!std::isfinite(state.values[index])) return false;
        applyParam(*p, kParamDefs[index].id, state.values[index]);
    }
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
constexpr auto kFieldPanel = s3g::gui_layout::compactEffectOutputPanel(3u);
constexpr auto kModelLeftPanel = s3g::gui_layout::compactEffectLeftPanel(
    kFieldPanel, s3g::gui_layout::PanelRole::EventTiming, 7u);
constexpr auto kModelRightPanel = s3g::gui_layout::compactEffectRightPanel(
    s3g::gui_layout::PanelRole::Projection, 7u);
constexpr s3g::gui_layout::Rect kActivityPanel {
    18.0, 382.0, 724.0, 120.0,
};

bool isDelayFieldMenu(clap_id id)
{
    return id == 1u || id == 2u || id == 410u;
}

bool isDelayFieldToggle(clap_id id)
{
    return id == 411u || id == 412u || id == 413u;
}

uint32_t delayFieldMenuCount(clap_id id)
{
    return id == 410u ? 5u : 4u;
}

NSRect delayFieldMenuAnchor(const s3g::gui_layout::Panel& panel,
                            uint32_t row)
{
    return NSMakeRect(s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 1.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width), 15.0);
}

@interface S3GDelayFieldView : NSView {
    void* _plugin;
    int _dragSlider;
    clap_id _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    NSTimer* _timer;
    char _titlePresetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawRow:(const ParamDef&)def row:(uint32_t)row
    panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GDelayFieldView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuOrigin = NSZeroPoint;
        _timer = nil;
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    NSArray* existingAreas = [[self trackingAreas] copy];
    for (NSTrackingArea* area in existingAreas) [self removeTrackingArea:area];
    [existingAreas release];
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow
            | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:area];
    [area release];
}
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer
{
    if (_timer) { [_timer invalidate]; _timer = nil; }
}
- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive())
        [self setNeedsDisplay:YES];
}
- (void)drawRow:(const ParamDef&)def row:(uint32_t)row
    panel:(const s3g::gui_layout::Panel&)panel attrs:(NSDictionary*)attrs
{
    auto* p = static_cast<Plugin*>(_plugin);
    double value = 0.0;
    paramsGetValue(&p->plugin, def.id, &value);
    const CGFloat norm = static_cast<CGFloat>((value - def.min)
        / std::max(0.000001, def.max - def.min));
    char text[40] {};
    paramsValueToText(&p->plugin, def.id, value, text, sizeof(text));
    s3g::clap_gui::Style style;
    if (isDelayFieldMenu(def.id)) {
        s3g::clap_gui::drawProcessorMenu(
            [NSString stringWithUTF8String:def.label],
            [NSString stringWithUTF8String:text],
            s3g::gui_layout::rowY(panel, row), panel.frame.x,
            panel.frame.width, attrs, s3g::clap_gui::softValueAttrs(), style);
        return;
    }
    if (isDelayFieldToggle(def.id)) {
        s3g::clap_gui::drawToggle(
            [NSString stringWithUTF8String:def.label], value >= 0.5,
            s3g::gui_layout::rowY(panel, row), attrs,
            s3g::clap_gui::softValueAttrs(), style,
            s3g::gui_layout::processorLabelX(panel.frame.x),
            s3g::gui_layout::processorControlX(panel.frame.x),
            s3g::gui_layout::processorMenuWidth(panel.frame.width));
        return;
    }
    s3g::clap_gui::drawProcessorSlider(
        [NSString stringWithUTF8String:def.label],
        [NSString stringWithUTF8String:text], norm,
        s3g::gui_layout::rowY(panel, row), panel.frame.x, panel.frame.width,
        attrs, s3g::clap_gui::softValueAttrs(), style);
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    s3g::clap_gui::drawCompactEffectTitleBand(
        @"s3g EFFECT DELAY FIELD",
        [NSString stringWithUTF8String:_titlePresetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
        titleBand, style);
    const auto drawPanel = [&](NSString* title,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel.frame.x, panel.frame.y,
            panel.frame.width, panel.frame.height, style);
        s3g::clap_gui::drawPanelHeader(title, true, panel.frame.x,
            panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);
    };
    const uint32_t model = std::min<uint32_t>(3u,
        static_cast<uint32_t>(p->params.model));
    drawPanel(@"FIELD", kFieldPanel);
    drawPanel([NSString stringWithFormat:@"%s I",
        s3g::delayFieldModelName(p->params.model)], kModelLeftPanel);
    drawPanel([NSString stringWithFormat:@"%s II",
        s3g::delayFieldModelName(p->params.model)], kModelRightPanel);
    for (uint32_t row = 0u; row < kGlobalParamCount; ++row)
        [self drawRow:kParamDefs[row] row:row panel:kFieldPanel attrs:labels];
    const uint32_t start = kModelStarts[model];
    const uint32_t count = kModelCounts[model];
    for (uint32_t item = 0u; item < count; ++item) {
        const bool right = item >= 7u;
        [self drawRow:kParamDefs[start + item] row:(right ? item - 7u : item)
            panel:(right ? kModelRightPanel : kModelLeftPanel) attrs:labels];
    }
    const uint32_t lanes = s3g::fixedBusRingActiveChannels(p->params.format);
    s3g::clap_gui::drawPanelFrame(kActivityPanel.x, kActivityPanel.y,
        kActivityPanel.width, kActivityPanel.height, style);
    s3g::clap_gui::drawPanelHeader(
        [NSString stringWithFormat:@"16-LANE DELAY ACTIVITY  >  %s",
            s3g::fixedBusRingFormatName(p->params.format)], true,
        kActivityPanel.x, kActivityPanel.y, kActivityPanel.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);
    [[NSString stringWithFormat:@"IN %@   FOLDED OUT %@",
        s3g::clap_gui::peakDbText(
            p->inputPeak.load(std::memory_order_relaxed)),
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed))]
        drawAtPoint:NSMakePoint(514.0, kActivityPanel.y + 5.0)
        withAttributes:small];
    constexpr CGFloat meterY = 414.0;
    constexpr CGFloat meterHeight = 52.0;
    constexpr CGFloat lanePitch = 42.0;
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel) {
        const float peak = p->sourcePeaks[channel].load(
            std::memory_order_relaxed);
        const float db = peak > 0.000001f
            ? 20.0f * std::log10(peak) : -60.0f;
        const CGFloat norm = static_cast<CGFloat>(std::clamp(
            (db + 60.0f) / 60.0f, 0.0f, 1.0f));
        const CGFloat x = 34.0 + lanePitch * channel;
        s3g::clap_gui::drawVerticalVuMeter(norm,
            NSMakeRect(x, meterY, 24.0, meterHeight), style);
        [[NSString stringWithFormat:@"%u", channel + 1u]
            drawAtPoint:NSMakePoint(x + (channel < 9u ? 8.0 : 4.0),
                meterY + meterHeight + 3.0)
            withAttributes:small];
    }
    [[NSString stringWithFormat:@"all 16 delayed lanes remain live; FORMAT folds them to %u host lanes",
        lanes] drawAtPoint:NSMakePoint(34.0, 487.0) withAttributes:small];

    if (_openMenu != CLAP_INVALID_ID) {
        static NSString* modelItems[] = {
            @"SHARD", @"ORBIT", @"CASCADE", @"ITERATE",
        };
        static NSString* formatItems[] = {
            @"16CH DIRECT", @"8CH RING", @"QUAD RING", @"STEREO RING",
        };
        static NSString* traversalItems[] = {
            @"SEQUENTIAL", @"REVERSE", @"PALINDROME", @"RANDOM",
            @"RANDOM CYCLE",
        };
        NSString** items = _openMenu == 1u ? modelItems
            : _openMenu == 2u ? formatItems : traversalItems;
        const uint32_t itemCount = delayFieldMenuCount(_openMenu);
        double value = 0.0;
        paramsGetValue(&p->plugin, _openMenu, &value);
        s3g::clap_gui::drawDropdownMenu(
            NSMakeRect(_menuOrigin.x, _menuOrigin.y,
                s3g::gui_layout::processorMenuWidth(kFieldPanel.frame.width),
                18.0 * itemCount),
            18.0, items, itemCount, static_cast<int>(std::llround(value)),
            _hoverMenuItem, small, style);
    }
}
- (void)updateSlider:(NSPoint)point
{
    auto* p = static_cast<Plugin*>(_plugin);
    const ParamDef* def = findParam(static_cast<clap_id>(_dragSlider));
    if (!def) return;
    const uint32_t model = std::min<uint32_t>(3u,
        static_cast<uint32_t>(p->params.model));
    const uint32_t start = kModelStarts[model];
    const uint32_t count = kModelCounts[model];
    const ParamDef* first = &kParamDefs[start];
    const ptrdiff_t item = def >= first && def < first + count
        ? def - first : -1;
    const auto& panel = def->id <= 3u ? kFieldPanel
        : (item >= 7 ? kModelRightPanel : kModelLeftPanel);
    const double x0 = s3g::gui_layout::processorControlX(panel.frame.x);
    const double width = s3g::gui_layout::processorTrackWidth(
        panel.frame.width);
    double norm = std::clamp((point.x - x0) / width, 0.0, 1.0);
    double value = def->min + norm * (def->max - def->min);
    if ((def->flags & CLAP_PARAM_IS_STEPPED) != 0u) value = std::round(value);
    applyParam(*p, def->id, value);
    [self setNeedsDisplay:YES];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const auto titleBand = s3g::gui_layout::compactEffectTitleBand(
        s3g::gui_layout::kCompactEffectFamilyLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(point, &p->plugin,
            @"Effect Delay Field 16", titleBand, _titlePresetName,
            sizeof(_titlePresetName))) {
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu != CLAP_INVALID_ID) {
        const uint32_t itemCount = delayFieldMenuCount(_openMenu);
        const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(kFieldPanel.frame.width),
            18.0 * itemCount);
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, menu, 18.0, itemCount);
        if (hit >= 0) applyParam(*p, _openMenu, static_cast<double>(hit));
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    const auto begin = [&](const ParamDef& def) {
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(event, &p->plugin,
                def.id, &defaultValue)) {
            applyParam(*p, def.id, defaultValue);
            _dragSlider = -1;
        } else {
            _dragSlider = static_cast<int>(def.id);
            [self updateSlider:point];
        }
        [self setNeedsDisplay:YES];
    };
    const auto hit = [&](const ParamDef& def, uint32_t row,
                         const s3g::gui_layout::Panel& panel) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(panel, row)))) return false;
        if (isDelayFieldMenu(def.id)) {
            const NSRect anchor = delayFieldMenuAnchor(panel, row);
            _openMenu = def.id;
            _hoverMenuItem = -1;
            _menuOrigin = NSMakePoint(anchor.origin.x,
                NSMaxY(anchor) + 3.0);
            [self setNeedsDisplay:YES];
            return true;
        }
        if (isDelayFieldToggle(def.id)) {
            double value = 0.0;
            paramsGetValue(&p->plugin, def.id, &value);
            applyParam(*p, def.id, value >= 0.5 ? 0.0 : 1.0);
            [self setNeedsDisplay:YES];
            return true;
        }
        begin(def);
        return true;
    };
    for (uint32_t row = 0u; row < kGlobalParamCount; ++row)
        if (hit(kParamDefs[row], row, kFieldPanel)) return;
    const uint32_t model = std::min<uint32_t>(3u,
        static_cast<uint32_t>(p->params.model));
    const uint32_t start = kModelStarts[model];
    const uint32_t count = kModelCounts[model];
    for (uint32_t item = 0u; item < count; ++item) {
        const bool right = item >= 7u;
        if (hit(kParamDefs[start + item], right ? item - 7u : item,
                right ? kModelRightPanel : kModelLeftPanel)) return;
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu != CLAP_INVALID_ID) {
        const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
            s3g::gui_layout::processorMenuWidth(kFieldPanel.frame.width),
            18.0 * delayFieldMenuCount(_openMenu));
        _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(point, menu, 18.0,
            delayFieldMenuCount(_openMenu));
        [self setNeedsDisplay:YES];
    }
    if (_dragSlider > 0) [self updateSlider:point];
}
- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == CLAP_INVALID_ID) return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y,
        s3g::gui_layout::processorMenuWidth(kFieldPanel.frame.width),
        18.0 * delayFieldMenuCount(_openMenu));
    const int hover = s3g::clap_gui::dropdownHitIndex(point, menu, 18.0,
        delayFieldMenuCount(_openMenu));
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
}
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GDelayFieldView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}
void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    p->guiVisible = false;
    [static_cast<S3GDelayFieldView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
                   uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{ return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GDelayFieldView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GDelayFieldView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}
const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};
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
    CLAP_PLUGIN_FEATURE_DELAY,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.delay-field",
    "s3g Effect Delay Field 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.2.0",
    "Stereo-input delay field with Shard, Orbit, Cascade, and CDP-inspired Iterate models and 2/4/8/16-channel ring formats.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
                                  const clap_host_t* host,
                                  const char* pluginId)
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
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin,
};
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

#undef S3G_DELAY_FIELD_FLOAT_PARAMS

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory,
};
