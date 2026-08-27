#include "s3g_format_upscale.h"
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

namespace {

constexpr uint32_t kChannels = s3g::kFormatUpscaleMaxChannels;
constexpr uint32_t kStateVersion = 6u;
constexpr uint32_t kGuiWidth = 1120u;
constexpr uint32_t kGuiHeight = 720u;

enum ParamId : clap_id {
    kParamInputLayout = 1u,
    kParamOutputLayout = 2u,
    kParamBasis = 3u,
    kParamPlacement = 4u,
    kParamOrigin = 5u,
    kParamAmount = 6u,
    kParamCopies = 7u,
    kParamRotation = 8u,
    kParamSpread = 9u,
    kParamDelay = 10u,
    kParamDecor = 11u,
    kParamSmoothing = 12u,
    kParamOutputGain = 13u,
    kParamAutoRowShape = 14u,
    kParamNormalization = 15u,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* label;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr ParamDef kParamDefs[] {
    { kParamInputLayout, "Input Format", "IN", "Format", 0.0,
        static_cast<double>(s3g::kFormatUpscaleLayoutCount - 1u), 1.0, true },
    { kParamOutputLayout, "Output Format", "OUT", "Format", 0.0,
        static_cast<double>(s3g::kFormatUpscaleLayoutCount - 1u), 3.0, true },
    { kParamBasis, "Content Basis", "BASIS", "Method", 0.0, 2.0, 0.0, true },
    { kParamPlacement, "Placement", "AUTO", "Method", 0.0, 8.0, 1.0, true },
    { kParamOrigin, "Origin Policy", "ORIGIN", "Format", 0.0, 2.0, 1.0, true },
    { kParamAmount, "Upscale Amount", "AMOUNT", "Format", 0.0, 100.0, 100.0, false },
    { kParamCopies, "Copies", "COPIES", "Method", 1.0,
        static_cast<double>(kChannels), 2.0, true },
    { kParamRotation, "Copy Rotation", "ROT", "Method", -180.0, 180.0, 90.0, false },
    { kParamSpread, "Distribution Spread", "SPREAD", "Method", 0.0, 100.0, 35.0, false },
    { kParamDelay, "Global Route Delay", "G DELAY", "Extension", 0.0,
        s3g::kFormatUpscaleMaxDelayMs, 0.0, false },
    { kParamDecor, "Global Route Decorrelation", "G DECOR", "Extension", 0.0, 100.0, 0.0, false },
    { kParamSmoothing, "Route Smoothing", "SMOOTH", "Extension", 1.0, 500.0, 35.0, false },
    { kParamOutputGain, "Output Gain", "GAIN", "Output", -24.0, 12.0, 0.0, false },
    { kParamAutoRowShape, "Auto Row Shape", "SHAPE", "Method", 0.0,
        3.0, 0.0, true },
    { kParamNormalization, "Matrix Normalization", "NORM", "Matrix", 0.0,
        2.0, 2.0, true },
};

constexpr uint32_t kParamCount = static_cast<uint32_t>(
    sizeof(kParamDefs) / sizeof(kParamDefs[0]));

const ParamDef* findParam(clap_id id)
{
    for (const auto& def : kParamDefs) if (def.id == id) return &def;
    return nullptr;
}

struct SavedStateV1 {
    uint32_t version = 1u;
    s3g::FormatUpscaleParams params {};
};

struct SavedStateV2 {
    uint32_t version = 2u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
};

struct SavedStateV3 {
    uint32_t version = 3u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<uint8_t, kChannels * kChannels> manualRoutes {};
};

struct SavedStateV4 {
    uint32_t version = 4u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<float, kChannels * kChannels> manualWeights {};
};

struct SavedStateV5 {
    uint32_t version = 5u;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<float, kChannels * kChannels> manualWeights {};
    uint32_t autoRowShape = 0u;
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleLayoutData customInput {};
    s3g::FormatUpscaleLayoutData customOutput {};
    uint32_t manualRoutesActive = 0u;
    std::array<float, kChannels * kChannels> manualWeights {};
    uint32_t autoRowShape = 0u;
    uint32_t normalization = 2u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::FormatUpscaleParams params {};
    s3g::FormatUpscaleRowShape autoRowShape =
        s3g::FormatUpscaleRowShape::Flat;
    s3g::FormatUpscaleNormalization normalization =
        s3g::FormatUpscaleNormalization::DualLimit;
    s3g::FormatUpscale dsp {};
    std::array<float, kChannels> frameIn {};
    std::array<float, kChannels> frameOut {};
    std::array<std::atomic<float>, kChannels> outputPeaks {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
    s3g::clap_gui::ResponsiveViewport guiViewport {};
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

void applyParams(Plugin& plugin)
{
    plugin.params = s3g::sanitizeFormatUpscaleParams(plugin.params);
    plugin.dsp.setParams(plugin.params);
    plugin.dsp.setAutoRowShape(plugin.autoRowShape);
    plugin.dsp.setNormalization(plugin.normalization);
}

void setParamValue(Plugin& plugin, clap_id id, double value)
{
    const ParamDef* def = findParam(id);
    if (!def || !std::isfinite(value)) return;
    const double clamped = std::clamp(value, def->minimum, def->maximum);
    switch (id) {
    case kParamInputLayout:
        plugin.params.inputLayout = static_cast<s3g::FormatUpscaleLayout>(
            roundedUint(clamped));
        break;
    case kParamOutputLayout:
        plugin.params.outputLayout = static_cast<s3g::FormatUpscaleLayout>(
            roundedUint(clamped));
        break;
    case kParamBasis:
        plugin.params.basis = static_cast<s3g::FormatUpscaleBasis>(
            roundedUint(clamped));
        break;
    case kParamPlacement:
        plugin.params.placement = static_cast<s3g::FormatUpscalePlacement>(
            roundedUint(clamped));
        break;
    case kParamOrigin:
        plugin.params.origin = static_cast<s3g::FormatUpscaleOrigin>(
            roundedUint(clamped));
        break;
    case kParamAmount:
        plugin.params.amountPercent = static_cast<float>(clamped);
        break;
    case kParamCopies:
        plugin.params.copies = roundedUint(clamped);
        break;
    case kParamRotation:
        plugin.params.rotationDegrees = static_cast<float>(clamped);
        break;
    case kParamSpread:
        plugin.params.spreadPercent = static_cast<float>(clamped);
        break;
    case kParamDelay:
        plugin.params.delayMs = static_cast<float>(clamped);
        break;
    case kParamDecor:
        plugin.params.decorrelationPercent = static_cast<float>(clamped);
        break;
    case kParamSmoothing:
        plugin.params.smoothingMs = static_cast<float>(clamped);
        break;
    case kParamOutputGain:
        plugin.params.outputGainDb = static_cast<float>(clamped);
        break;
    case kParamAutoRowShape:
        plugin.autoRowShape = static_cast<s3g::FormatUpscaleRowShape>(
            roundedUint(clamped));
        break;
    case kParamNormalization:
        plugin.normalization = static_cast<s3g::FormatUpscaleNormalization>(
            roundedUint(clamped));
        break;
    default:
        return;
    }
    applyParams(plugin);
}

double getParamValue(const Plugin& plugin, clap_id id)
{
    switch (id) {
    case kParamInputLayout:
        return static_cast<uint32_t>(plugin.params.inputLayout);
    case kParamOutputLayout:
        return static_cast<uint32_t>(plugin.params.outputLayout);
    case kParamBasis:
        return static_cast<uint32_t>(plugin.params.basis);
    case kParamPlacement:
        return static_cast<uint32_t>(plugin.params.placement);
    case kParamOrigin:
        return static_cast<uint32_t>(plugin.params.origin);
    case kParamAmount: return plugin.params.amountPercent;
    case kParamCopies: return plugin.params.copies;
    case kParamRotation: return plugin.params.rotationDegrees;
    case kParamSpread: return plugin.params.spreadPercent;
    case kParamDelay: return plugin.params.delayMs;
    case kParamDecor: return plugin.params.decorrelationPercent;
    case kParamSmoothing: return plugin.params.smoothingMs;
    case kParamOutputGain: return plugin.params.outputGainDb;
    case kParamAutoRowShape:
        return static_cast<uint32_t>(plugin.autoRowShape);
    case kParamNormalization:
        return static_cast<uint32_t>(plugin.normalization);
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

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(instance->macRealtimeActivity);
#endif
    instance->dsp.prepare(sampleRate);
    applyParams(*instance);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#else
    (void)plugin;
#endif
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->dsp.reset(); }
void onMainThread(const clap_plugin_t*) {}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* value = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        setParamValue(plugin, value->param_id, value->value);
    }
}

float readSample(const clap_audio_buffer_t* input,
    uint32_t channel, uint32_t frame)
{
    if (!input || channel >= input->channel_count) return 0.0f;
    if (input->data32 && input->data32[channel])
        return input->data32[channel][frame];
    if (input->data64 && input->data64[channel])
        return static_cast<float>(input->data64[channel][frame]);
    return 0.0f;
}

void writeSample(const clap_audio_buffer_t& output,
    uint32_t channel, uint32_t frame, float value)
{
    if (channel >= output.channel_count) return;
    if (output.data32 && output.data32[channel])
        output.data32[channel][frame] = value;
    if (output.data64 && output.data64[channel])
        output.data64[channel][frame] = static_cast<double>(value);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processContext)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, processContext->in_events);
    if (processContext->audio_outputs_count == 0u)
        return CLAP_PROCESS_CONTINUE;
    const clap_audio_buffer_t* input = processContext->audio_inputs_count > 0u
        ? &processContext->audio_inputs[0] : nullptr;
    const clap_audio_buffer_t& output = processContext->audio_outputs[0];
    const uint32_t availableInputs = input
        ? std::min<uint32_t>(input->channel_count, kChannels) : 0u;
    const uint32_t availableOutputs = std::min<uint32_t>(
        output.channel_count, kChannels);
    std::array<float, kChannels> blockPeaks {};

    for (uint32_t frame = 0u; frame < processContext->frames_count; ++frame) {
        for (uint32_t channel = 0u; channel < kChannels; ++channel)
            instance->frameIn[channel] = channel < availableInputs
                ? readSample(input, channel, frame) : 0.0f;
        instance->dsp.processFrame(instance->frameIn.data(), availableInputs,
            instance->frameOut.data(), availableOutputs);
        for (uint32_t channel = 0u; channel < availableOutputs; ++channel) {
            const float value = instance->frameOut[channel];
            writeSample(output, channel, frame, value);
            blockPeaks[channel] = std::max(blockPeaks[channel], std::abs(value));
        }
        for (uint32_t channel = availableOutputs;
             channel < output.channel_count; ++channel)
            writeSample(output, channel, frame, 0.0f);
    }
    for (uint32_t channel = 0u; channel < kChannels; ++channel) {
        const float previous = instance->outputPeaks[channel].load(
            std::memory_order_relaxed);
        instance->outputPeaks[channel].store(
            std::max(previous * 0.90f, blockPeaks[channel]),
            std::memory_order_relaxed);
    }
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Format In 64" : "Speaker Out 64");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "%s", def.module);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !findParam(id)) return false;
    *value = getParamValue(*self(plugin), id);
    return true;
}

const char* menuValueName(clap_id id, uint32_t value)
{
    switch (id) {
    case kParamInputLayout:
    case kParamOutputLayout:
        return s3g::formatUpscaleLayoutName(
            static_cast<s3g::FormatUpscaleLayout>(std::min<uint32_t>(
                value, s3g::kFormatUpscaleLayoutCount - 1u)));
    case kParamBasis:
        return s3g::formatUpscaleBasisName(
            static_cast<s3g::FormatUpscaleBasis>(std::min<uint32_t>(value, 2u)));
    case kParamPlacement:
        return s3g::formatUpscalePlacementName(
            static_cast<s3g::FormatUpscalePlacement>(std::min<uint32_t>(value, 8u)));
    case kParamOrigin:
        return s3g::formatUpscaleOriginName(
            static_cast<s3g::FormatUpscaleOrigin>(std::min<uint32_t>(value, 2u)));
    case kParamAutoRowShape:
        return s3g::formatUpscaleRowShapeName(
            static_cast<s3g::FormatUpscaleRowShape>(
                std::min<uint32_t>(value, 3u)));
    case kParamNormalization:
        return s3g::formatUpscaleNormalizationName(
            static_cast<s3g::FormatUpscaleNormalization>(
                std::min<uint32_t>(value, 2u)));
    default:
        return "";
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !findParam(id)) return false;
    if ((id >= kParamInputLayout && id <= kParamOrigin)
        || id == kParamAutoRowShape || id == kParamNormalization) {
        std::snprintf(display, size, "%s", menuValueName(id, roundedUint(value)));
    } else if (id == kParamCopies) {
        std::snprintf(display, size, "%u", roundedUint(value));
    } else if (id == kParamRotation) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kParamDelay || id == kParamSmoothing) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kParamOutputGain) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else {
        std::snprintf(display, size, "%.1f%%", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    const ParamDef* def = findParam(id);
    if (!display || !value || !def) return false;
    if ((id >= kParamInputLayout && id <= kParamOrigin)
        || id == kParamAutoRowShape || id == kParamNormalization) {
        const uint32_t count = id == kParamInputLayout || id == kParamOutputLayout
            ? s3g::kFormatUpscaleLayoutCount
            : (id == kParamPlacement ? 9u
                : (id == kParamAutoRowShape ? 4u : 3u));
        for (uint32_t index = 0u; index < count; ++index) {
            if (std::strcmp(display, menuValueName(id, index)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    *value = std::clamp(std::atof(display), def->minimum, def->maximum);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    const auto* instance = self(plugin);
    const SavedState state { kStateVersion, instance->params,
        instance->dsp.customInputLayout(),
        instance->dsp.customOutputLayout(),
        instance->dsp.manualRoutesActive() ? 1u : 0u,
        instance->dsp.manualWeights(),
        static_cast<uint32_t>(instance->autoRowShape),
        static_cast<uint32_t>(instance->normalization) };
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    auto* instance = self(plugin);
    uint32_t version = 0u;
    if (!s3g::clap_state::readAll(stream, &version, sizeof(version)))
        return false;
    instance->autoRowShape = s3g::FormatUpscaleRowShape::Flat;
    instance->normalization = s3g::FormatUpscaleNormalization::Row;
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) return false;
        instance->params = s3g::sanitizeFormatUpscaleParams(state.params);
        instance->dsp.setCustomInputLayout(state.customInput);
        instance->dsp.setCustomOutputLayout(state.customOutput);
        instance->dsp.setManualWeights(state.manualWeights,
            state.manualRoutesActive != 0u);
        instance->autoRowShape = static_cast<s3g::FormatUpscaleRowShape>(
            std::min<uint32_t>(state.autoRowShape, 3u));
        instance->normalization =
            static_cast<s3g::FormatUpscaleNormalization>(
                std::min<uint32_t>(state.normalization, 2u));
    } else if (version == 5u) {
        SavedStateV5 state {};
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) return false;
        instance->params = s3g::sanitizeFormatUpscaleParams(state.params);
        instance->dsp.setCustomInputLayout(state.customInput);
        instance->dsp.setCustomOutputLayout(state.customOutput);
        instance->dsp.setManualWeights(state.manualWeights,
            state.manualRoutesActive != 0u);
        instance->autoRowShape = static_cast<s3g::FormatUpscaleRowShape>(
            std::min<uint32_t>(state.autoRowShape, 3u));
    } else if (version == 4u) {
        SavedStateV4 state {};
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) return false;
        instance->params = s3g::sanitizeFormatUpscaleParams(state.params);
        instance->dsp.setCustomInputLayout(state.customInput);
        instance->dsp.setCustomOutputLayout(state.customOutput);
        instance->dsp.setManualWeights(state.manualWeights,
            state.manualRoutesActive != 0u);
        instance->autoRowShape = s3g::FormatUpscaleRowShape::Flat;
    } else if (version == 3u) {
        SavedStateV3 state {};
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) return false;
        instance->params = s3g::sanitizeFormatUpscaleParams(state.params);
        instance->dsp.setCustomInputLayout(state.customInput);
        instance->dsp.setCustomOutputLayout(state.customOutput);
        instance->dsp.setManualRoutes(state.manualRoutes,
            state.manualRoutesActive != 0u);
    } else if (version == 2u) {
        SavedStateV2 state {};
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) return false;
        instance->params = s3g::sanitizeFormatUpscaleParams(state.params);
        instance->dsp.setCustomInputLayout(state.customInput);
        instance->dsp.setCustomOutputLayout(state.customOutput);
        instance->dsp.useAutomaticRoutes();
    } else if (version == 1u) {
        SavedStateV1 state {};
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) return false;
        instance->params = s3g::sanitizeFormatUpscaleParams(state.params);
        instance->dsp.useAutomaticRoutes();
    } else {
        return false;
    }
    applyParams(*instance);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

constexpr NSRect kFormatPanelRect = {
    { 824.0, 86.0 }, { 260.0, 175.0 } };
constexpr NSRect kMatrixPanelRect = {
    { 18.0, 48.0 }, { 1084.0, 654.0 } };
constexpr NSRect kSpatialPanelRect = {
    { 686.0, 126.0 }, { 416.0, 320.0 } };
constexpr NSRect kRightPanelRect = {
    { 824.0, 275.0 }, { 260.0, 409.0 } };
constexpr NSRect kSelectedPanelRect = {
    { 824.0, 275.0 }, { 260.0, 232.0 } };
constexpr NSRect kSpatialMapRect = {
    { 696.0, 156.0 }, { 396.0, 280.0 } };
constexpr NSRect kMatrixAvailableRect = {
    { 96.0, 160.0 }, { 700.0, 440.0 } };
constexpr NSRect kLayoutSurfaceRect = {
    { 38.0, 86.0 }, { 1044.0, 590.0 } };

// Keep coordinate annotations in a dedicated gutter instead of letting
// boundary speaker labels compete with the degree scale.
constexpr CGFloat kLayoutProjectionHorizontalInset = 42.0;
constexpr CGFloat kLayoutProjectionVerticalInset = 42.0;
constexpr CGFloat kTierRingDegreeLabelGutter = 34.0;
constexpr float kTierRingZenithElevation = 89.5f;

NSRect layoutProjectionRect(bool output)
{
    constexpr CGFloat outerMargin = 24.0;
    constexpr CGFloat mapGap = 96.0;
    constexpr CGFloat mapWidth =
        (kLayoutSurfaceRect.size.width - outerMargin * 2.0 - mapGap) * 0.5;
    const CGFloat left = kLayoutSurfaceRect.origin.x + outerMargin
        + (output ? mapWidth + mapGap : 0.0);
    return NSMakeRect(left, kLayoutSurfaceRect.origin.y + 112.0,
        mapWidth, kLayoutSurfaceRect.size.height - 166.0);
}

NSPoint layoutProjectionPoint(NSRect map, float azimuthDeg,
    float elevationDeg)
{
    const CGFloat xSpan = std::max<CGFloat>(1.0,
        map.size.width - kLayoutProjectionHorizontalInset * 2.0);
    const CGFloat ySpan = std::max<CGFloat>(1.0,
        map.size.height - kLayoutProjectionVerticalInset * 2.0);
    const CGFloat azimuth = std::clamp<CGFloat>(azimuthDeg, -180.0, 180.0);
    const CGFloat elevation = std::clamp<CGFloat>(elevationDeg, -90.0, 90.0);
    return NSMakePoint(
        map.origin.x + kLayoutProjectionHorizontalInset
            + (180.0 - azimuth) / 360.0 * xSpan,
        map.origin.y + kLayoutProjectionVerticalInset
            + (90.0 - elevation) / 180.0 * ySpan);
}

void layoutNodePoints(const s3g::FormatUpscaleLayoutData& layout,
    NSRect map, bool origami, std::array<NSPoint, kChannels>& points)
{
    if (!origami) {
        for (uint32_t index = 0u; index < layout.count; ++index)
            points[index] = layoutProjectionPoint(map,
                layout.speakers[index].azimuthDeg,
                layout.speakers[index].elevationDeg);
        return;
    }

    std::array<float, kChannels> tierHeights {};
    uint32_t tierCount = 0u;
    for (uint32_t index = 0u; index < layout.count; ++index) {
        const float height = s3g::formatUpscaleSpeakerHeight(
            layout.speakers[index]);
        uint32_t tier = 0u;
        while (tier < tierCount
            && std::abs(tierHeights[tier] - height)
                >= s3g::kFormatUpscaleTierHeightTolerance)
            ++tier;
        if (tier == tierCount) tierHeights[tierCount++] = height;
    }
    std::sort(tierHeights.begin(), tierHeights.begin() + tierCount,
        [](float a, float b) { return a < b; });
    std::array<float, kChannels> tierHorizontalExtents {};
    for (uint32_t index = 0u; index < layout.count; ++index) {
        const float height = s3g::formatUpscaleSpeakerHeight(
            layout.speakers[index]);
        uint32_t tier = 0u;
        while (tier + 1u < tierCount
            && std::abs(tierHeights[tier] - height)
                >= s3g::kFormatUpscaleTierHeightTolerance)
            ++tier;
        const auto direction = s3g::directionFromAed(
            layout.speakers[index].azimuthDeg,
            layout.speakers[index].elevationDeg);
        const float horizontal = std::max(std::abs(direction.x),
            std::abs(direction.y)) * layout.speakers[index].distance;
        tierHorizontalExtents[tier] = std::max(
            tierHorizontalExtents[tier], horizontal);
    }
    const NSPoint center = NSMakePoint(NSMidX(map), NSMidY(map));
    const CGFloat maximumExtent = std::max<CGFloat>(24.0,
        std::min(map.size.width, map.size.height) * 0.5 - 38.0);
    const CGFloat innerExtent = maximumExtent * 0.28;
    const CGFloat outerExtent = maximumExtent * 0.94;
    for (uint32_t index = 0u; index < layout.count; ++index) {
        const float height = s3g::formatUpscaleSpeakerHeight(
            layout.speakers[index]);
        uint32_t tier = 0u;
        while (tier + 1u < tierCount
            && std::abs(tierHeights[tier] - height)
                >= s3g::kFormatUpscaleTierHeightTolerance)
            ++tier;
        const CGFloat planExtent = tierCount <= 1u
            ? maximumExtent * 0.70
            : outerExtent - (outerExtent - innerExtent)
                * static_cast<CGFloat>(tier)
                / static_cast<CGFloat>(tierCount - 1u);
        const auto direction = s3g::directionFromAed(
            layout.speakers[index].azimuthDeg,
            layout.speakers[index].elevationDeg);
        const CGFloat horizontalExtent = std::max<CGFloat>(
            0.000001, tierHorizontalExtents[tier]);
        const CGFloat scale = planExtent / horizontalExtent;
        const CGFloat roomX = direction.x * layout.speakers[index].distance;
        const CGFloat roomY = direction.y * layout.speakers[index].distance;
        // Preserve the tier's real top-plan geometry instead of forcing every
        // speaker onto a circle. A zenith remains at the listener center.
        const bool zenith = layout.speakers[index].elevationDeg
            >= kTierRingZenithElevation;
        points[index] = NSMakePoint(
            center.x - (zenith ? 0.0 : roomY * scale),
            center.y - (zenith ? 0.0 : roomX * scale));
    }
}

// Legacy helper methods below still use the original map-oriented names.
// Keeping them aliased to the compact panels lets old session/UI support code
// remain available while the live interface is matrix-first.
constexpr NSRect kInputMapPanelRect = kSpatialPanelRect;
constexpr NSRect kOutputMapPanelRect = kSpatialPanelRect;
constexpr NSRect kEquationPanelRect = kRightPanelRect;
constexpr NSRect kMethodPanelRect = kMatrixPanelRect;
constexpr s3g::gui_layout::Panel kDistributionPanel {
    s3g::gui_layout::PluginClass::OutputUtility,
    s3g::gui_layout::PanelRole::Routing,
    { 18.0, 48.0, 1084.0, 654.0 }, 36.0, 30.0, 4u };
constexpr s3g::gui_layout::Panel kExtensionPanel {
    s3g::gui_layout::PluginClass::OutputUtility,
    s3g::gui_layout::PanelRole::Output,
    { 824.0, 521.0, 260.0, 163.0 }, 30.0, 25.0, 4u };

struct FormatUpscaleMatrixGeometry {
    NSRect grid = NSZeroRect;
    CGFloat cell = 1.0;
};

FormatUpscaleMatrixGeometry matrixGeometry(
    uint32_t inputCount, uint32_t outputCount)
{
    inputCount = std::max<uint32_t>(1u, inputCount);
    outputCount = std::max<uint32_t>(1u, outputCount);
    const CGFloat cell = std::max<CGFloat>(1.0, std::min<CGFloat>({
        52.0,
        kMatrixAvailableRect.size.width / static_cast<CGFloat>(outputCount),
        kMatrixAvailableRect.size.height / static_cast<CGFloat>(inputCount),
    }));
    const CGFloat width = cell * static_cast<CGFloat>(outputCount);
    const CGFloat height = cell * static_cast<CGFloat>(inputCount);
    return {
        NSMakeRect(NSMidX(kMatrixAvailableRect) - width * 0.5,
            NSMidY(kMatrixAvailableRect) - height * 0.5,
            width, height),
        cell,
    };
}

bool matrixCellAtPoint(NSPoint point, uint32_t inputCount,
    uint32_t outputCount, uint32_t& input, uint32_t& output)
{
    const auto geometry = matrixGeometry(inputCount, outputCount);
    if (!NSPointInRect(point, geometry.grid)) return false;
    output = std::min<uint32_t>(outputCount - 1u,
        static_cast<uint32_t>((point.x - geometry.grid.origin.x)
            / geometry.cell));
    input = std::min<uint32_t>(inputCount - 1u,
        static_cast<uint32_t>((point.y - geometry.grid.origin.y)
            / geometry.cell));
    return true;
}

NSRect cocoaPanelRect(const s3g::gui_layout::Panel& panel)
{
    return s3g::clap_gui::cocoaRect(panel.frame);
}

NSRect panelMapRect(NSRect panel)
{
    return NSMakeRect(panel.origin.x + 10.0, panel.origin.y + 64.0,
        panel.size.width - 20.0, panel.size.height - 72.0);
}

NSRect formatRowRect(NSRect panel)
{
    return NSMakeRect(panel.origin.x, panel.origin.y + 25.0,
        panel.size.width, 31.0);
}

NSRect compactFormatRowRect(bool output)
{
    return NSMakeRect(kFormatPanelRect.origin.x + 12.0,
        kFormatPanelRect.origin.y + 27.0 + (output ? 49.0 : 0.0),
        kFormatPanelRect.size.width - 24.0, 42.0);
}

NSRect sideFormatValueRect(bool output)
{
    const NSRect row = compactFormatRowRect(output);
    return NSMakeRect(row.origin.x, row.origin.y + 17.0,
        row.size.width, 19.0);
}

NSRect sideAutoModeRowRect()
{
    return NSMakeRect(kFormatPanelRect.origin.x + 12.0,
        kFormatPanelRect.origin.y + 125.0,
        kFormatPanelRect.size.width - 24.0, 42.0);
}

NSRect sideAutoModeValueRect()
{
    const NSRect row = sideAutoModeRowRect();
    return NSMakeRect(row.origin.x, row.origin.y + 17.0,
        row.size.width, 19.0);
}

NSRect spatialButtonRect(uint32_t button)
{
    constexpr CGFloat gap = 4.0;
    constexpr CGFloat widths[5] { 22.0, 22.0, 62.0, 61.0, 67.0 };
    CGFloat right = NSMaxX(kSpatialPanelRect) - 7.0;
    for (int index = 4; index >= 0; --index) {
        const CGFloat left = right - widths[index];
        if (button == static_cast<uint32_t>(index))
            return NSMakeRect(left, kSpatialPanelRect.origin.y + 3.0,
                widths[index], 15.0);
        right = left - gap;
    }
    return NSZeroRect;
}

NSRect viewSelectorRect(uint32_t view)
{
    constexpr CGFloat width = 86.0;
    constexpr CGFloat gap = 5.0;
    const CGFloat right = NSMaxX(kMatrixPanelRect) - 7.0;
    return NSMakeRect(right - width
            - static_cast<CGFloat>(2u - view) * (width + gap),
        kMatrixPanelRect.origin.y + 3.0, width, 15.0);
}

NSRect formatEditButtonRect(bool output)
{
    constexpr CGFloat width = 70.0;
    constexpr CGFloat gap = 4.0;
    const CGFloat right = NSMaxX(kFormatPanelRect) - 7.0;
    return NSMakeRect(right - width
            - (output ? 0.0 : width + gap),
        kFormatPanelRect.origin.y + 3.0, width, 15.0);
}

NSRect sideAutoActionRect(uint32_t action)
{
    constexpr CGFloat widths[2] { 70.0, 54.0 };
    constexpr CGFloat gap = 4.0;
    const NSRect row = sideAutoModeRowRect();
    const CGFloat right = NSMaxX(row);
    const CGFloat clearLeft = right - widths[1];
    const CGFloat left = action == 0u
        ? clearLeft - gap - widths[0] : clearLeft;
    return NSMakeRect(left, row.origin.y, widths[action], 17.0);
}

NSRect sideNormalizationRect(uint32_t normalization)
{
    constexpr CGFloat widths[3] { 58.0, 58.0, 67.0 };
    constexpr CGFloat gap = 3.0;
    CGFloat x = kSelectedPanelRect.origin.x + 59.0;
    for (uint32_t index = 0u; index < normalization; ++index)
        x += widths[index] + gap;
    return NSMakeRect(x, kSelectedPanelRect.origin.y + 158.0,
        widths[normalization], 17.0);
}

NSRect layoutPopupActionRect()
{
    constexpr CGFloat width = 86.0;
    return NSMakeRect(NSMaxX(kLayoutSurfaceRect) - width - 10.0,
        kLayoutSurfaceRect.origin.y + 9.0, width, 17.0);
}

NSRect mapVisualRect(NSRect panel)
{
    const NSRect map = panelMapRect(panel);
    return NSMakeRect(map.origin.x, map.origin.y,
        map.size.width, map.size.height - 29.0);
}

NSRect mapViewButtonRect(NSRect panel, uint32_t button)
{
    constexpr CGFloat gap = 4.0;
    constexpr CGFloat smallWidth = 23.0;
    constexpr CGFloat centerWidth = 68.0;
    constexpr CGFloat designWidth = 52.0;
    const CGFloat right = NSMaxX(panel) - 8.0;
    if (button == 2u)
        return NSMakeRect(right - centerWidth, panel.origin.y + 3.0,
            centerWidth, 15.0);
    const CGFloat centerLeft = right - centerWidth;
    if (button == 3u)
        return NSMakeRect(centerLeft - gap - smallWidth * 2.0 - gap * 2.0
                - designWidth,
            panel.origin.y + 3.0, designWidth, 15.0);
    const CGFloat x = centerLeft - gap - smallWidth
        - static_cast<CGFloat>(1u - button) * (smallWidth + gap);
    return NSMakeRect(x, panel.origin.y + 3.0, smallWidth, 15.0);
}

NSRect customEditorColumnRect(uint32_t column)
{
    return NSMakeRect(kRightPanelRect.origin.x,
        kRightPanelRect.origin.y + 22.0
            + static_cast<CGFloat>(column) * 35.0,
        kRightPanelRect.size.width, 31.0);
}

NSRect customEditorSliderHitRect(uint32_t column)
{
    const NSRect row = customEditorColumnRect(column);
    return NSMakeRect(
        static_cast<CGFloat>(s3g::gui_layout::processorControlX(row.origin.x)),
        row.origin.y + 3.0,
        static_cast<CGFloat>(s3g::gui_layout::processorTrackWidth(
            row.size.width)), 20.0);
}

NSRect customEditorFieldRect(uint32_t column)
{
    const NSRect row = customEditorColumnRect(column);
    return NSMakeRect(NSMaxX(row) - 57.0, row.origin.y + 4.0, 54.0, 17.0);
}

NSRect customEditorActionRect(uint32_t action)
{
    constexpr CGFloat widths[3] { 70.0, 108.0, 56.0 };
    constexpr CGFloat gap = 5.0;
    CGFloat x = kRightPanelRect.origin.x + 8.0;
    for (uint32_t index = 0u; index < action; ++index)
        x += widths[index] + gap;
    return NSMakeRect(x, NSMaxY(kRightPanelRect) - 24.0,
        widths[action], 17.0);
}

NSRect connectionActionRect(uint32_t action)
{
    constexpr CGFloat width = 132.0;
    constexpr CGFloat gap = 8.0;
    const CGFloat right = NSMaxX(kMethodPanelRect) - 10.0;
    return NSMakeRect(right - width
            - static_cast<CGFloat>(1u - action) * (width + gap),
        kMethodPanelRect.origin.y + 27.0, width, 23.0);
}

NSRect methodColumnRect(uint32_t column)
{
    constexpr CGFloat gap = 12.0;
    constexpr CGFloat inset = 8.0;
    const CGFloat width = (kMethodPanelRect.size.width - inset * 2.0
        - gap * 2.0) / 3.0;
    return NSMakeRect(kMethodPanelRect.origin.x + inset
            + static_cast<CGFloat>(column) * (width + gap),
        kMethodPanelRect.origin.y + 25.0, width, 31.0);
}

NSRect panelSliderRow(const s3g::gui_layout::Panel& panel, uint32_t row)
{
    return NSMakeRect(panel.frame.x,
        s3g::gui_layout::rowY(panel, row) - 5.0,
        panel.frame.width, 26.0);
}

NSRect valueRect(NSRect row)
{
    return NSMakeRect(
        static_cast<CGFloat>(s3g::gui_layout::processorControlX(row.origin.x)),
        row.origin.y + 4.0,
        static_cast<CGFloat>(s3g::gui_layout::processorMenuWidth(
            row.size.width)), 17.0);
}

uint32_t menuCountForParam(clap_id id)
{
    if (id == kParamInputLayout || id == kParamOutputLayout)
        return s3g::kFormatUpscaleLayoutCount;
    if (id == kParamPlacement) return 9u;
    if (id == kParamBasis || id == kParamOrigin) return 3u;
    return 0u;
}

NSRect menuRowForParam(clap_id id)
{
    switch (id) {
    case kParamInputLayout: return compactFormatRowRect(false);
    case kParamOutputLayout: return compactFormatRowRect(true);
    case kParamOrigin: return methodColumnRect(0u);
    case kParamBasis: return methodColumnRect(1u);
    case kParamPlacement: return sideAutoModeRowRect();
    default: return NSZeroRect;
    }
}

NSRect menuValueRectForParam(clap_id id)
{
    if (id == kParamInputLayout) return sideFormatValueRect(false);
    if (id == kParamOutputLayout) return sideFormatValueRect(true);
    if (id == kParamPlacement) return sideAutoModeValueRect();
    return valueRect(menuRowForParam(id));
}

struct FormatMenuGeometry {
    NSRect first = NSZeroRect;
    NSRect second = NSZeroRect;
    uint32_t firstCount = 0u;
    uint32_t secondCount = 0u;
    CGFloat itemHeight = 20.0;
};

FormatMenuGeometry formatMenuGeometry(clap_id id)
{
    const uint32_t count = menuCountForParam(id);
    const uint32_t firstCount = (count + 1u) / 2u;
    const uint32_t secondCount = count - firstCount;
    const NSRect row = menuRowForParam(id);
    const NSRect box = menuValueRectForParam(id);
    const bool format = id == kParamInputLayout || id == kParamOutputLayout;
    const CGFloat columnWidth = format ? 230.0 : row.size.width * 0.5;
    const CGFloat left = format
        ? kFormatPanelRect.origin.x - 8.0 - columnWidth * 2.0
        : row.origin.x;
    const CGFloat top = format ? kFormatPanelRect.origin.y : NSMaxY(box) + 3.0;
    return {
        NSMakeRect(left, top, columnWidth,
            20.0 * static_cast<CGFloat>(firstCount)),
        NSMakeRect(left + columnWidth, top, columnWidth,
            20.0 * static_cast<CGFloat>(secondCount)),
        firstCount, secondCount, 20.0,
    };
}

NSRect selectedWeightRowRect()
{
    return NSMakeRect(kRightPanelRect.origin.x,
        kRightPanelRect.origin.y + 118.0,
        kRightPanelRect.size.width, 31.0);
}

NSRect selectedWeightHitRect()
{
    const NSRect row = selectedWeightRowRect();
    return NSMakeRect(
        static_cast<CGFloat>(s3g::gui_layout::processorControlX(row.origin.x)),
        row.origin.y + 3.0,
        static_cast<CGFloat>(s3g::gui_layout::processorTrackWidth(
        row.size.width)), 20.0);
}

NSRect rowShapeButtonRect(uint32_t action)
{
    constexpr CGFloat width = 55.0;
    constexpr CGFloat gap = 4.0;
    return NSMakeRect(kSelectedPanelRect.origin.x + 12.0
            + static_cast<CGFloat>(action) * (width + gap),
        kSelectedPanelRect.origin.y + 205.0, width, 17.0);
}

NSRect shapeAxisButtonRect(bool column)
{
    return column
        ? NSMakeRect(kSelectedPanelRect.origin.x + 145.0,
            kSelectedPanelRect.origin.y + 181.0, 103.0, 17.0)
        : NSMakeRect(kSelectedPanelRect.origin.x + 61.0,
            kSelectedPanelRect.origin.y + 181.0, 80.0, 17.0);
}

NSRect sliderRowForParam(clap_id id)
{
    switch (id) {
    case kParamAmount: return panelSliderRow(kDistributionPanel, 0u);
    case kParamCopies: return panelSliderRow(kDistributionPanel, 1u);
    case kParamRotation: return panelSliderRow(kDistributionPanel, 2u);
    case kParamSpread: return panelSliderRow(kDistributionPanel, 3u);
    case kParamDelay: return panelSliderRow(kExtensionPanel, 0u);
    case kParamDecor: return panelSliderRow(kExtensionPanel, 1u);
    case kParamSmoothing: return panelSliderRow(kExtensionPanel, 2u);
    case kParamOutputGain: return panelSliderRow(kExtensionPanel, 3u);
    default: return NSZeroRect;
    }
}

NSRect sliderHitRectForParam(clap_id id)
{
    switch (id) {
    case kParamAmount:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kDistributionPanel, 0u));
    case kParamCopies:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kDistributionPanel, 1u));
    case kParamRotation:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kDistributionPanel, 2u));
    case kParamSpread:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kDistributionPanel, 3u));
    case kParamDelay:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kExtensionPanel, 0u));
    case kParamDecor:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kExtensionPanel, 1u));
    case kParamSmoothing:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kExtensionPanel, 2u));
    case kParamOutputGain:
        return s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(kExtensionPanel, 3u));
    default: return NSZeroRect;
    }
}

} // namespace

enum : NSInteger {
    kCustomAzimuthFieldTag = 8101,
    kCustomElevationFieldTag = 8102,
    kCustomDistanceFieldTag = 8103,
};

@interface S3GFormatUpscaleMapView : NSView
    <NSTextFieldDelegate, NSWindowDelegate> {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _openMenu;
    clap_id _dragParam;
    int _hoverMenuItem;
    uint32_t _selectedInput;
    uint32_t _selectedOutput;
    bool _selectionIsOutput;
    CGFloat _inputZoom;
    CGFloat _outputZoom;
    NSPoint _inputPan;
    NSPoint _outputPan;
    NSInteger _dragMap;
    NSPoint _mapDragOrigin;
    NSPoint _mapPanOrigin;
    BOOL _mapMoved;
    BOOL _matrixPainting;
    BOOL _matrixPaintConnected;
    BOOL _matrixWeightAdjusting;
    BOOL _dragWeight;
    BOOL _shapeColumn;
    NSPoint _matrixDragOrigin;
    float _matrixDragStartWeight;
    NSInteger _matrixPaintInput;
    NSInteger _matrixPaintOutput;
    NSInteger _routeDragSource;
    NSPoint _routeDragPoint;
    NSInteger _speakerDragMap;
    NSInteger _speakerDragIndex;
    NSInteger _designMap;
    NSInteger _dragCustom;
    NSTextField* _azField;
    NSTextField* _elField;
    NSTextField* _distField;
    BOOL _azFieldDirty;
    BOOL _elFieldDirty;
    BOOL _distFieldDirty;
    uint32_t _page;
    BOOL _layoutOrigami;
    BOOL _layoutPopupChild;
    S3GFormatUpscaleMapView* _layoutPopupOwner;
    NSPanel* _layoutPanel;
    S3GFormatUpscaleMapView* _layoutPopupView;
    uint32_t _lastLayoutInputLabelCount;
    uint32_t _lastLayoutOutputLabelCount;
    char _titlePresetName[64];
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)openLayoutPopup;
- (void)hideLayoutPopup;
- (void)dockLayoutPopup;
- (void)destroyLayoutPopup;
- (void)updateCustomValueFields;
- (void)loadDocumentationThreeTierLayout;
- (void)loadDocumentationCube41Layout;
- (void)setDocumentationLayoutPage:(BOOL)layout;
- (void)setDocumentationLayoutOrigami:(BOOL)origami;
@end

@implementation S3GFormatUpscaleMapView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _openMenu = CLAP_INVALID_ID;
        _dragParam = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _selectedInput = 0u;
        _selectedOutput = 0u;
        _selectionIsOutput = false;
        _inputZoom = 1.0;
        _outputZoom = 1.0;
        _inputPan = NSZeroPoint;
        _outputPan = NSZeroPoint;
        _dragMap = -1;
        _mapDragOrigin = NSZeroPoint;
        _mapPanOrigin = NSZeroPoint;
        _mapMoved = NO;
        _matrixPainting = NO;
        _matrixPaintConnected = NO;
        _matrixWeightAdjusting = NO;
        _dragWeight = NO;
        _shapeColumn = NO;
        _matrixDragOrigin = NSZeroPoint;
        _matrixDragStartWeight = 0.0f;
        _matrixPaintInput = -1;
        _matrixPaintOutput = -1;
        _routeDragSource = -1;
        _routeDragPoint = NSZeroPoint;
        _speakerDragMap = -1;
        _speakerDragIndex = -1;
        _designMap = -1;
        _dragCustom = -1;
        _azFieldDirty = NO;
        _elFieldDirty = NO;
        _distFieldDirty = NO;
        _page = 0u;
        _layoutOrigami = YES;
        _layoutPopupChild = NO;
        _layoutPopupOwner = nil;
        _layoutPanel = nil;
        _layoutPopupView = nil;
        auto makeField = ^NSTextField*(NSInteger tag) {
            NSTextField* field = [[[NSTextField alloc]
                initWithFrame:NSMakeRect(0.0, 0.0, 54.0, 17.0)] autorelease];
            [field setTag:tag];
            [field setDelegate:self];
            s3g::clap_gui::styleNumberTextField(
                field, 10.0, NSTextAlignmentCenter);
            [field setFormatter:nil];
            [field setHidden:YES];
            [self addSubview:field];
            return field;
        };
        _azField = makeField(kCustomAzimuthFieldTag);
        _elField = makeField(kCustomElevationFieldTag);
        _distField = makeField(kCustomDistanceFieldTag);
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc
{
    [self stopRefreshTimer];
    if (!_layoutPopupChild) [self destroyLayoutPopup];
    [super dealloc];
}

- (void)startRefreshTimer
{
    if (!_timer) {
        _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
            target:self selector:@selector(timerFired:) userInfo:nil repeats:YES];
    }
}

- (void)stopRefreshTimer
{
    [_timer invalidate];
    _timer = nil;
}

- (void)timerFired:(NSTimer*)timer
{
    (void)timer;
    if (_layoutPopupChild && _layoutPopupOwner) {
        _selectedInput = _layoutPopupOwner->_selectedInput;
        _selectedOutput = _layoutPopupOwner->_selectedOutput;
        _selectionIsOutput = _layoutPopupOwner->_selectionIsOutput;
        _layoutOrigami = _layoutPopupOwner->_layoutOrigami;
    }
    if (_plugin && _plugin->guiVisible.load(std::memory_order_relaxed))
        [self updateCustomValueFields];
    if (_plugin && _plugin->guiVisible.load(std::memory_order_relaxed))
        [self setNeedsDisplay:YES];
}

- (void)openLayoutPopup
{
    if (_layoutPopupChild || !_plugin) return;
    if (!_layoutPanel) {
        _layoutPanel = [[NSPanel alloc] initWithContentRect:
                NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                | NSWindowStyleMaskUtilityWindow)
            backing:NSBackingStoreBuffered defer:NO];
        [_layoutPanel setTitle:
            @"s3g OUTPUT FORMAT UPSCALE 64 — LAYOUT CONNECTIONS"];
        [_layoutPanel setReleasedWhenClosed:NO];
        // Keep the layout independent from the host window so it can remain
        // visible beside the matrix or on another display.
        [_layoutPanel setHidesOnDeactivate:NO];
        [_layoutPanel setDelegate:self];
        _layoutPopupView = [[S3GFormatUpscaleMapView alloc]
            initWithPlugin:_plugin];
        _layoutPopupView->_layoutPopupChild = YES;
        _layoutPopupView->_layoutPopupOwner = self;
        _layoutPopupView->_page = 1u;
        _layoutPopupView->_layoutOrigami = _layoutOrigami;
        _layoutPopupView->_selectedInput = _selectedInput;
        _layoutPopupView->_selectedOutput = _selectedOutput;
        _layoutPopupView->_selectionIsOutput = _selectionIsOutput;
        [_layoutPanel setContentView:_layoutPopupView];
        [_layoutPanel setContentSize:NSMakeSize(kGuiWidth, kGuiHeight)];
        [_layoutPopupView setFrame:NSMakeRect(
            0.0, 0.0, kGuiWidth, kGuiHeight)];
        [_layoutPopupView setBounds:NSMakeRect(
            0.0, 0.0, kGuiWidth, kGuiHeight)];
        [_layoutPopupView release];

        NSWindow* parent = [self window];
        const NSRect parentFrame = parent ? [parent frame]
            : [[NSScreen mainScreen] visibleFrame];
        const NSRect panelFrame = [_layoutPanel frame];
        NSScreen* screen = parent ? [parent screen] : [NSScreen mainScreen];
        const NSRect visible = screen ? [screen visibleFrame] : parentFrame;
        CGFloat x = NSMaxX(parentFrame) + 8.0;
        if (x + panelFrame.size.width > NSMaxX(visible))
            x = NSMinX(parentFrame) - panelFrame.size.width - 8.0;
        x = std::clamp(x, NSMinX(visible),
            std::max(NSMinX(visible),
                NSMaxX(visible) - panelFrame.size.width));
        const CGFloat y = std::clamp(
            NSMaxY(parentFrame) - panelFrame.size.height,
            NSMinY(visible), std::max(NSMinY(visible),
                NSMaxY(visible) - panelFrame.size.height));
        [_layoutPanel setFrameOrigin:NSMakePoint(x, y)];
    }
    _page = 0u;
    _designMap = -1;
    [self updateCustomValueFields];
    _layoutPopupView->_page = 1u;
    _layoutPopupView->_layoutOrigami = _layoutOrigami;
    _layoutPopupView->_selectedInput = _selectedInput;
    _layoutPopupView->_selectedOutput = _selectedOutput;
    _layoutPopupView->_selectionIsOutput = _selectionIsOutput;
    [_layoutPopupView startRefreshTimer];
    [_layoutPanel makeKeyAndOrderFront:nil];
    [self setNeedsDisplay:YES];
}

- (void)hideLayoutPopup
{
    if (!_layoutPanel) return;
    [_layoutPopupView stopRefreshTimer];
    [_layoutPanel orderOut:nil];
    [self setNeedsDisplay:YES];
}

- (void)dockLayoutPopup
{
    if (_layoutPopupChild && _layoutPopupOwner) {
        [_layoutPopupOwner dockLayoutPopup];
        return;
    }
    [self hideLayoutPopup];
    _page = 1u;
    _designMap = -1;
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)destroyLayoutPopup
{
    if (_layoutPopupChild || !_layoutPanel) return;
    [_layoutPopupView stopRefreshTimer];
    _layoutPopupView->_layoutPopupOwner = nil;
    [_layoutPanel setDelegate:nil];
    [_layoutPanel orderOut:nil];
    [_layoutPanel release];
    _layoutPanel = nil;
    _layoutPopupView = nil;
}

- (void)windowWillClose:(NSNotification*)notification
{
    if ([notification object] != _layoutPanel) return;
    [_layoutPopupView stopRefreshTimer];
    [self setNeedsDisplay:YES];
}

- (BOOL)fieldIsEditing:(NSTextField*)field
{
    NSText* editor = [field currentEditor];
    return editor && [[self window] firstResponder] == editor;
}

- (s3g::FormatUpscaleLayoutData)editableLayout
{
    if (!_plugin) return {};
    return _designMap == 1
        ? _plugin->dsp.customOutputLayout()
        : _plugin->dsp.customInputLayout();
}

- (uint32_t)editableSelection
{
    return _designMap == 1 ? _selectedOutput : _selectedInput;
}

- (void)setEditableLayout:(const s3g::FormatUpscaleLayoutData&)layout
{
    if (!_plugin || _designMap < 0) return;
    if (_designMap == 1)
        _plugin->dsp.setCustomOutputLayout(layout);
    else
        _plugin->dsp.setCustomInputLayout(layout);
    applyParams(*_plugin);
}

- (void)enterDesignMap:(NSInteger)map
{
    if (!_plugin || map < 0 || map > 1) return;
    const bool output = map == 1;
    const auto currentLayout = output
        ? _plugin->dsp.outputLayout() : _plugin->dsp.inputLayout();
    const auto currentFormat = output
        ? _plugin->params.outputLayout : _plugin->params.inputLayout;
    if (currentFormat != s3g::FormatUpscaleLayout::Custom) {
        if (output)
            _plugin->dsp.setCustomOutputLayout(currentLayout);
        else
            _plugin->dsp.setCustomInputLayout(currentLayout);
    }
    _designMap = map;
    setParamValue(*_plugin,
        output ? kParamOutputLayout : kParamInputLayout,
        static_cast<double>(
            static_cast<uint32_t>(s3g::FormatUpscaleLayout::Custom)));
    [self updateCustomValueFields];
}

- (void)updateCustomValueFields
{
    const BOOL visible = _plugin && _page == 0u && _designMap >= 0;
    [_azField setHidden:!visible];
    [_elField setHidden:!visible];
    [_distField setHidden:!visible];
    if (!visible) return;
    [_azField setFrame:customEditorFieldRect(2u)];
    [_elField setFrame:customEditorFieldRect(3u)];
    [_distField setFrame:customEditorFieldRect(4u)];
    const auto layout = [self editableLayout];
    const uint32_t selected = std::min<uint32_t>(
        [self editableSelection], std::max<uint32_t>(1u, layout.count) - 1u);
    const auto& speaker = layout.speakers[selected];
    if (![self fieldIsEditing:_azField])
        [_azField setStringValue:[NSString stringWithFormat:@"%+.1f",
            static_cast<double>(speaker.azimuthDeg)]];
    if (![self fieldIsEditing:_elField])
        [_elField setStringValue:[NSString stringWithFormat:@"%+.1f",
            static_cast<double>(speaker.elevationDeg)]];
    if (![self fieldIsEditing:_distField])
        [_distField setStringValue:[NSString stringWithFormat:@"%.2f",
            static_cast<double>(speaker.distance)]];
}

- (void)controlTextDidChange:(NSNotification*)notification
{
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    if ([field tag] == kCustomAzimuthFieldTag) _azFieldDirty = YES;
    else if ([field tag] == kCustomElevationFieldTag) _elFieldDirty = YES;
    else if ([field tag] == kCustomDistanceFieldTag) _distFieldDirty = YES;
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    if (!_plugin || _designMap < 0) return;
    NSTextField* field = static_cast<NSTextField*>([notification object]);
    if (!field) return;
    auto layout = [self editableLayout];
    const uint32_t selected = std::min<uint32_t>(
        [self editableSelection], std::max<uint32_t>(1u, layout.count) - 1u);
    auto& speaker = layout.speakers[selected];
    const float value = static_cast<float>([[field stringValue] doubleValue]);
    if ([field tag] == kCustomAzimuthFieldTag && _azFieldDirty) {
        _azFieldDirty = NO;
        speaker.azimuthDeg = value;
    } else if ([field tag] == kCustomElevationFieldTag && _elFieldDirty) {
        _elFieldDirty = NO;
        speaker.elevationDeg = value;
    } else if ([field tag] == kCustomDistanceFieldTag && _distFieldDirty) {
        _distFieldDirty = NO;
        speaker.distance = value;
    } else {
        return;
    }
    [self setEditableLayout:layout];
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)loadDocumentationThreeTierLayout
{
    if (!_plugin) return;
    _plugin->dsp.setCustomInputLayout(
        s3g::formatUpscaleDefaultCustomInputLayout());
    _plugin->dsp.setCustomOutputLayout(
        s3g::formatUpscaleDefaultThreeTierOutputLayout());
    _plugin->params.inputLayout = s3g::FormatUpscaleLayout::Custom;
    _plugin->params.outputLayout = s3g::FormatUpscaleLayout::Custom;
    _plugin->params.placement = s3g::FormatUpscalePlacement::Interleave;
    _plugin->params.copies = 3u;
    applyParams(*_plugin);
    std::array<uint8_t, kChannels * kChannels> routes {};
    for (uint32_t input = 0u; input < 3u; ++input) {
        for (uint32_t tier = 0u; tier < 3u; ++tier)
            routes[input * kChannels + tier * 3u + input] = 1u;
    }
    _plugin->dsp.setManualRoutes(routes, true);
    _page = 0u;
    _designMap = -1;
    _inputZoom = 1.05;
    _outputZoom = 1.15;
    _inputPan = NSZeroPoint;
    _outputPan = NSZeroPoint;
    _selectedInput = 0u;
    _selectedOutput = 0u;
    _selectionIsOutput = false;
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)loadDocumentationCube41Layout
{
    if (!_plugin) return;
    _plugin->params.inputLayout = s3g::FormatUpscaleLayout::Stereo;
    _plugin->params.outputLayout = s3g::FormatUpscaleLayout::Cube41;
    _plugin->params.basis = s3g::FormatUpscaleBasis::Direct;
    _plugin->params.placement = s3g::FormatUpscalePlacement::Match;
    _plugin->params.origin = s3g::FormatUpscaleOrigin::Share;
    _plugin->params.copies = 1u;
    applyParams(*_plugin);
    _plugin->dsp.useAutomaticRoutes();
    _page = 1u;
    _designMap = -1;
    _selectedInput = 0u;
    _selectedOutput = 0u;
    _selectionIsOutput = false;
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)loadDocumentationMidSideLayout
{
    if (!_plugin) return;
    s3g::FormatUpscaleLayoutData output {};
    output.count = 6u;
    s3g::formatUpscaleSetSpeaker(output, 0u, 0.0f, 0.0f,
        s3g::FormatUpscaleRole::Center);
    s3g::formatUpscaleSetSpeaker(output, 1u, 90.0f, 0.0f,
        s3g::FormatUpscaleRole::LeftSurround);
    s3g::formatUpscaleSetSpeaker(output, 2u, -90.0f, 0.0f,
        s3g::FormatUpscaleRole::RightSurround);
    s3g::formatUpscaleSetSpeaker(output, 3u, 90.0f, 55.0f,
        s3g::FormatUpscaleRole::TopLeftFront);
    s3g::formatUpscaleSetSpeaker(output, 4u, -90.0f, 55.0f,
        s3g::FormatUpscaleRole::TopRightFront);
    s3g::formatUpscaleSetSpeaker(output, 5u, 0.0f, 90.0f,
        s3g::FormatUpscaleRole::Center);
    _plugin->dsp.setCustomOutputLayout(output);
    _plugin->params.inputLayout = s3g::FormatUpscaleLayout::Stereo;
    _plugin->params.outputLayout = s3g::FormatUpscaleLayout::Custom;
    _plugin->params.basis = s3g::FormatUpscaleBasis::Direct;
    _plugin->params.placement =
        s3g::FormatUpscalePlacement::MidSideSpread;
    _plugin->autoRowShape = s3g::FormatUpscaleRowShape::Flat;
    _plugin->normalization = s3g::FormatUpscaleNormalization::DualLimit;
    applyParams(*_plugin);
    _plugin->dsp.useAutomaticRoutes();
    _page = 0u;
    _designMap = -1;
    _selectedInput = 0u;
    _selectedOutput = 1u;
    _selectionIsOutput = false;
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)setDocumentationLayoutPage:(BOOL)layout
{
    _page = layout ? 1u : 0u;
    _designMap = -1;
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)setDocumentationLayoutOrigami:(BOOL)origami
{
    S3GFormatUpscaleMapView* root = _layoutPopupChild && _layoutPopupOwner
        ? _layoutPopupOwner : self;
    root->_layoutOrigami = origami;
    if (root->_layoutPopupView)
        root->_layoutPopupView->_layoutOrigami = origami;
    [root setNeedsDisplay:YES];
    [root->_layoutPopupView setNeedsDisplay:YES];
    [self setNeedsDisplay:YES];
}

- (uint32_t)layoutPage
{
    return _page;
}

- (BOOL)layoutOrigami
{
    return _layoutOrigami;
}

- (double)layoutInputProjectionWidth
{
    return layoutProjectionRect(false).size.width;
}

- (double)layoutOutputProjectionWidth
{
    return layoutProjectionRect(true).size.width;
}

- (double)layoutInputAzimuthNinetySpan
{
    const NSRect map = layoutProjectionRect(false);
    return layoutProjectionPoint(map, 90.0f, 0.0f).x
        - layoutProjectionPoint(map, -90.0f, 0.0f).x;
}

- (double)layoutOutputAzimuthNinetySpan
{
    const NSRect map = layoutProjectionRect(true);
    return layoutProjectionPoint(map, 90.0f, 0.0f).x
        - layoutProjectionPoint(map, -90.0f, 0.0f).x;
}

- (double)layoutInputElevationSpan
{
    const NSRect map = layoutProjectionRect(false);
    return layoutProjectionPoint(map, 0.0f, -90.0f).y
        - layoutProjectionPoint(map, 0.0f, 90.0f).y;
}

- (double)layoutOutputElevationSpan
{
    const NSRect map = layoutProjectionRect(true);
    return layoutProjectionPoint(map, 0.0f, -90.0f).y
        - layoutProjectionPoint(map, 0.0f, 90.0f).y;
}

- (BOOL)layoutPopupVisible
{
    return _layoutPanel && [_layoutPanel isVisible];
}

- (S3GFormatUpscaleMapView*)layoutPopupView
{
    return _layoutPopupView;
}

- (BOOL)manualRoutesActive
{
    return _plugin && _plugin->dsp.manualRoutesActive();
}

- (uint32_t)autoModeValue
{
    return _plugin ? static_cast<uint32_t>(_plugin->params.placement) : 0u;
}

- (uint32_t)autoRowShapeValue
{
    return _plugin ? static_cast<uint32_t>(_plugin->autoRowShape) : 0u;
}

- (BOOL)manualRouteZeroToZero
{
    return _plugin && _plugin->dsp.manualRoute(0u, 0u);
}

- (BOOL)manualRouteZeroToOne
{
    return _plugin && _plugin->dsp.manualRoute(0u, 1u);
}

- (BOOL)manualRouteZeroToTwo
{
    return _plugin && _plugin->dsp.manualRoute(0u, 2u);
}

- (double)manualWeightZeroToZero
{
    return _plugin ? _plugin->dsp.manualWeight(0u, 0u) : 0.0;
}

- (double)manualWeightZeroToOne
{
    return _plugin ? _plugin->dsp.manualWeight(0u, 1u) : 0.0;
}

- (double)manualWeightZeroToTwo
{
    return _plugin ? _plugin->dsp.manualWeight(0u, 2u) : 0.0;
}

- (double)manualWeightOneToZero
{
    return _plugin ? _plugin->dsp.manualWeight(1u, 0u) : 0.0;
}

- (uint32_t)normalizationValue
{
    return _plugin ? static_cast<uint32_t>(_plugin->normalization) : 0u;
}

- (double)selectedColumnPower
{
    if (!_plugin) return 0.0;
    float power = 0.0f;
    for (uint32_t input = 0u; input < _plugin->dsp.activeInputs(); ++input) {
        const float gain = _plugin->dsp.targetAnchorGain(input, _selectedOutput)
            + _plugin->dsp.targetExtensionGain(input, _selectedOutput);
        power += gain * gain;
    }
    return power;
}

- (BOOL)shapeColumn
{
    return _shapeColumn;
}

- (BOOL)layoutSelectionIsOutput
{
    return _selectionIsOutput;
}

- (double)layoutFirstOutputPointX
{
    if (!_plugin || _plugin->dsp.outputLayout().count == 0u) return 0.0;
    std::array<NSPoint, kChannels> points {};
    layoutNodePoints(_plugin->dsp.outputLayout(), layoutProjectionRect(true),
        _layoutOrigami, points);
    return points[0u].x;
}

- (double)layoutFirstOutputPointY
{
    if (!_plugin || _plugin->dsp.outputLayout().count == 0u) return 0.0;
    std::array<NSPoint, kChannels> points {};
    layoutNodePoints(_plugin->dsp.outputLayout(), layoutProjectionRect(true),
        _layoutOrigami, points);
    return points[0u].y;
}

- (double)layoutOutputPointRadiusAtIndex:(uint32_t)index
{
    if (!_plugin || index >= _plugin->dsp.outputLayout().count) return -1.0;
    const NSRect map = layoutProjectionRect(true);
    std::array<NSPoint, kChannels> points {};
    layoutNodePoints(_plugin->dsp.outputLayout(), map,
        _layoutOrigami, points);
    const CGFloat dx = points[index].x - NSMidX(map);
    const CGFloat dy = points[index].y - NSMidY(map);
    return std::sqrt(dx * dx + dy * dy);
}

- (double)layoutDefaultTierRingRadiusAtIndex:(uint32_t)index
{
    const auto layout = s3g::formatUpscaleDefaultThreeTierOutputLayout();
    if (index >= layout.count) return -1.0;
    const NSRect map = layoutProjectionRect(true);
    std::array<NSPoint, kChannels> points {};
    layoutNodePoints(layout, map, true, points);
    const CGFloat dx = points[index].x - NSMidX(map);
    const CGFloat dy = points[index].y - NSMidY(map);
    return std::sqrt(dx * dx + dy * dy);
}

- (double)layoutDefaultTierOneRadius
{
    return [self layoutDefaultTierRingRadiusAtIndex:0u];
}

- (double)layoutDefaultTierTwoRadius
{
    return [self layoutDefaultTierRingRadiusAtIndex:3u];
}

- (double)layoutDefaultTierThreeRadius
{
    return [self layoutDefaultTierRingRadiusAtIndex:6u];
}

- (double)layoutOutputZenithRadius
{
    if (!_plugin) return -1.0;
    const auto& layout = _plugin->dsp.outputLayout();
    for (uint32_t index = 0u; index < layout.count; ++index) {
        if (layout.speakers[index].elevationDeg
            >= kTierRingZenithElevation)
            return [self layoutOutputPointRadiusAtIndex:index];
    }
    return -1.0;
}

- (double)layoutCubeFrontEdgeDeviation
{
    const auto layout = s3g::formatUpscaleLayoutData(
        s3g::FormatUpscaleLayout::Cube41);
    std::array<NSPoint, kChannels> points {};
    layoutNodePoints(layout, layoutProjectionRect(true), true, points);
    CGFloat minimumX = points[0u].x;
    CGFloat maximumX = points[0u].x;
    CGFloat minimumY = points[0u].y;
    CGFloat maximumY = points[0u].y;
    for (uint32_t index = 1u; index < 4u; ++index) {
        minimumX = std::min(minimumX, points[index].x);
        maximumX = std::max(maximumX, points[index].x);
        minimumY = std::min(minimumY, points[index].y);
        maximumY = std::max(maximumY, points[index].y);
    }
    return std::min(maximumX - minimumX, maximumY - minimumY);
}

- (uint32_t)layoutOutputLabelCount
{
    return _lastLayoutOutputLabelCount;
}

- (double)matrixCellSize
{
    if (!_plugin) return 0.0;
    return matrixGeometry(_plugin->dsp.activeInputs(),
        _plugin->dsp.activeOutputs()).cell;
}

- (uint32_t)matrixOutputLabelStride
{
    const double cell = [self matrixCellSize];
    return cell > 0.0 ? std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::ceil(28.0 / cell))) : 0u;
}

- (uint32_t)activeInputCount
{
    return _plugin ? _plugin->dsp.activeInputs() : 0u;
}

- (uint32_t)activeOutputCount
{
    return _plugin ? _plugin->dsp.activeOutputs() : 0u;
}

- (double)selectedManualWeight
{
    return _plugin
        ? _plugin->dsp.manualWeight(_selectedInput, _selectedOutput) : 0.0;
}

- (double)selectedCustomAzimuth
{
    if (!_plugin || _designMap < 0) return 0.0;
    const auto layout = [self editableLayout];
    const uint32_t selected = std::min<uint32_t>(
        [self editableSelection], layout.count - 1u);
    return layout.speakers[selected].azimuthDeg;
}

- (double)selectedCustomElevation
{
    if (!_plugin || _designMap < 0) return 0.0;
    const auto layout = [self editableLayout];
    const uint32_t selected = std::min<uint32_t>(
        [self editableSelection], layout.count - 1u);
    return layout.speakers[selected].elevationDeg;
}

- (void)computePointsForLayout:(const s3g::FormatUpscaleLayoutData&)layout
    rect:(NSRect)rect
    zoom:(CGFloat)zoom
    pan:(NSPoint)pan
    design:(bool)design
    points:(std::array<NSPoint, kChannels>&)points
{
    float minimumElevation = 90.0f;
    float maximumElevation = -90.0f;
    for (uint32_t index = 0u; index < layout.count; ++index) {
        minimumElevation = std::min(minimumElevation,
            layout.speakers[index].elevationDeg);
        maximumElevation = std::max(maximumElevation,
            layout.speakers[index].elevationDeg);
    }
    const float elevationRange = maximumElevation - minimumElevation;
    const NSPoint center = NSMakePoint(NSMidX(rect) + pan.x,
        NSMidY(rect) + 5.0 + pan.y);
    const CGFloat maximumRadius = std::min(rect.size.width,
        rect.size.height) * 0.43 * zoom;
    const bool compactFront = !design && layout.count <= 3u
        && elevationRange < 1.0f;
    for (uint32_t index = 0u; index < layout.count; ++index) {
        const auto& speaker = layout.speakers[index];
        float azimuthDegrees = speaker.azimuthDeg;
        if (compactFront) azimuthDegrees *= 1.8f;
        const CGFloat azimuth = static_cast<CGFloat>(azimuthDegrees)
            * 3.14159265358979323846 / 180.0;
        CGFloat radialFactor = design
            ? 0.38 + 0.58 * static_cast<CGFloat>(
                (speaker.elevationDeg + 90.0f) / 180.0f)
            : 0.76;
        if (!design && elevationRange >= 1.0f) {
            if (minimumElevation >= -1.0f) {
                const float normalized = maximumElevation > 1.0f
                    ? speaker.elevationDeg / maximumElevation : 0.0f;
                radialFactor = 0.55
                    + 0.37 * static_cast<CGFloat>(normalized);
            } else {
                const float normalized = (speaker.elevationDeg
                    - minimumElevation) / elevationRange;
                radialFactor = 0.40
                    + 0.52 * static_cast<CGFloat>(normalized);
            }
        }
        radialFactor *= static_cast<CGFloat>(s3g::formatUpscaleClamp(
            0.65f + speaker.distance * 0.35f, 0.55f, 1.50f));
        const CGFloat radius = maximumRadius * radialFactor;
        points[index] = NSMakePoint(
            center.x - std::sin(azimuth) * radius,
            center.y - std::cos(azimuth) * radius * 0.84);
    }
}

- (uint32_t)hitNodeAtPoint:(NSPoint)point map:(NSInteger)map found:(BOOL*)found
{
    if (found) *found = NO;
    if (!_plugin || map < 0 || map > 1) return 0u;
    const bool output = map == 1;
    const auto& layout = output
        ? _plugin->dsp.outputLayout() : _plugin->dsp.inputLayout();
    const NSRect visual = NSMakeRect(kSpatialMapRect.origin.x,
        kSpatialMapRect.origin.y, kSpatialMapRect.size.width,
        kSpatialMapRect.size.height - 29.0);
    std::array<NSPoint, kChannels> points {};
    [self computePointsForLayout:layout rect:visual
        zoom:_outputZoom
        pan:_outputPan
        design:_designMap == map points:points];
    CGFloat bestDistance = 17.0;
    uint32_t best = 0u;
    for (uint32_t index = 0u; index < layout.count; ++index) {
        const CGFloat dx = points[index].x - point.x;
        const CGFloat dy = points[index].y - point.y;
        const CGFloat distance = std::sqrt(dx * dx + dy * dy);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
            if (found) *found = YES;
        }
    }
    return best;
}

- (void)dragCustomSpeakerToPoint:(NSPoint)point
{
    if (!_plugin || _speakerDragMap < 0 || _speakerDragIndex < 0) return;
    const bool output = _speakerDragMap == 1;
    auto layout = output
        ? _plugin->dsp.customOutputLayout()
        : _plugin->dsp.customInputLayout();
    const uint32_t index = std::min<uint32_t>(
        static_cast<uint32_t>(_speakerDragIndex), layout.count - 1u);
    const NSRect visual = NSMakeRect(kSpatialMapRect.origin.x,
        kSpatialMapRect.origin.y, kSpatialMapRect.size.width,
        kSpatialMapRect.size.height - 29.0);
    const NSPoint pan = _outputPan;
    const CGFloat zoom = _outputZoom;
    const NSPoint center = NSMakePoint(NSMidX(visual) + pan.x,
        NSMidY(visual) + 5.0 + pan.y);
    const CGFloat maximumRadius = std::min(
        visual.size.width, visual.size.height) * 0.43 * zoom;
    const auto& current = layout.speakers[index];
    const CGFloat distanceFactor = static_cast<CGFloat>(
        s3g::formatUpscaleClamp(
            0.65f + current.distance * 0.35f, 0.55f, 1.50f));
    const CGFloat horizontal = center.x - point.x;
    const CGFloat vertical = (center.y - point.y) / 0.84;
    const CGFloat radius = std::sqrt(
        horizontal * horizontal + vertical * vertical);
    const float azimuth = radius < 0.001
        ? 0.0f
        : static_cast<float>(std::atan2(horizontal, vertical)
            * 180.0 / 3.14159265358979323846);
    const CGFloat radialFactor = radius
        / std::max<CGFloat>(1.0, maximumRadius * distanceFactor);
    const float elevation = static_cast<float>(
        (std::clamp<CGFloat>((radialFactor - 0.38) / 0.58, 0.0, 1.0)
            * 180.0) - 90.0);
    layout.speakers[index].azimuthDeg = azimuth;
    layout.speakers[index].elevationDeg = elevation;
    if (output)
        _plugin->dsp.setCustomOutputLayout(layout);
    else
        _plugin->dsp.setCustomInputLayout(layout);
    applyParams(*_plugin);
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)drawPanel:(NSRect)rect title:(NSString*)title
    attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
        rect.size.width, rect.size.height, style);
    s3g::clap_gui::drawPanelHeader(title, true,
        rect.origin.x, rect.origin.y, rect.size.width, 21.0, attrs, style);
}

- (void)drawMenuControl:(clap_id)param row:(NSRect)row
    attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const ParamDef* def = findParam(param);
    if (!def) return;
    s3g::clap_gui::drawProcessorMenu(
        [NSString stringWithUTF8String:def->label],
        [NSString stringWithUTF8String:menuValueName(param,
            roundedUint(getParamValue(*_plugin, param)))],
        row.origin.y + 5.0, row.origin.x, row.size.width,
        attrs, valueAttrs, style);
}

- (void)drawSideFormatControl:(clap_id)param output:(bool)output
    attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    NSString* label = output ? @"OUT FORMAT" : @"IN FORMAT";
    const NSRect row = compactFormatRowRect(output);
    const NSRect box = sideFormatValueRect(output);
    [label drawAtPoint:row.origin withAttributes:attrs];
    NSString* value = [NSString stringWithUTF8String:menuValueName(param,
        roundedUint(getParamValue(*_plugin, param)))];
    s3g::clap_gui::drawMenu(@"", value, box.origin.y + 1.0,
        attrs, valueAttrs, style, box.origin.x, box.origin.x, box.size.width);
}

- (void)drawSideAutoModeControl:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const NSRect row = sideAutoModeRowRect();
    const NSRect box = sideAutoModeValueRect();
    [@"AUTO MODE" drawAtPoint:row.origin withAttributes:attrs];
    NSString* value = [NSString stringWithUTF8String:menuValueName(
        kParamPlacement, roundedUint(getParamValue(
            *_plugin, kParamPlacement)))];
    s3g::clap_gui::drawMenu(@"", value, box.origin.y + 1.0,
        attrs, valueAttrs, style, box.origin.x, box.origin.x, box.size.width);
    s3g::clap_gui::drawToolboxHeaderButton(formatEditButtonRect(false),
        kFormatPanelRect, @"EDIT IN", _designMap == 0, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(formatEditButtonRect(true),
        kFormatPanelRect, @"EDIT OUT", _designMap == 1, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(sideAutoActionRect(0u),
        kFormatPanelRect, @"AUTO MAP", !_plugin->dsp.manualRoutesActive(),
        attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(sideAutoActionRect(1u),
        kFormatPanelRect, @"CLEAR", _plugin->dsp.manualRoutesActive(),
        attrs, style);
}

- (void)drawSliderControl:(clap_id)param row:(NSRect)row
    attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const ParamDef* def = findParam(param);
    if (!def) return;
    const double value = getParamValue(*_plugin, param);
    const CGFloat normalized = static_cast<CGFloat>(
        (value - def->minimum) / (def->maximum - def->minimum));
    char text[64] {};
    paramsValueToText(nullptr, param, value, text, sizeof(text));
    s3g::clap_gui::drawProcessorSlider(
        [NSString stringWithUTF8String:def->label],
        [NSString stringWithUTF8String:text], normalized,
        row.origin.y + 5.0, row.origin.x, row.size.width,
        attrs, valueAttrs, style);
}

- (void)drawMapSurface:(NSRect)rect
    layout:(const s3g::FormatUpscaleLayoutData&)layout
    points:(const std::array<NSPoint, kChannels>&)points
    output:(bool)isOutput
    pan:(NSPoint)pan
    attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    [s3g::clap_gui::color(0x151717) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    const NSRect visual = NSMakeRect(rect.origin.x, rect.origin.y,
        rect.size.width, rect.size.height - 29.0);
    [s3g::clap_gui::color(0x343a3a, 0.42) setStroke];
    for (uint32_t step = 1u; step < 8u; ++step) {
        const CGFloat x = visual.origin.x + visual.size.width
            * static_cast<CGFloat>(step) / 8.0;
        const CGFloat y = visual.origin.y + visual.size.height
            * static_cast<CGFloat>(step) / 8.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, visual.origin.y)
            toPoint:NSMakePoint(x, NSMaxY(visual))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(visual.origin.x, y)
            toPoint:NSMakePoint(NSMaxX(visual), y)];
    }

    struct ElevationBand {
        float elevation = 0.0f;
        std::array<uint32_t, kChannels> indices {};
        uint32_t count = 0u;
    };
    std::array<ElevationBand, kChannels> bands {};
    uint32_t bandCount = 0u;
    for (uint32_t index = 0u; index < layout.count; ++index) {
        uint32_t band = 0u;
        while (band < bandCount
            && std::abs(bands[band].elevation
                - layout.speakers[index].elevationDeg) >= 0.5f)
            ++band;
        if (band == bandCount) {
            bands[band].elevation = layout.speakers[index].elevationDeg;
            ++bandCount;
        }
        bands[band].indices[bands[band].count++] = index;
    }
    std::sort(bands.begin(), bands.begin() + bandCount,
        [](const ElevationBand& a, const ElevationBand& b) {
            return a.elevation < b.elevation;
        });
    for (uint32_t band = 0u; band < bandCount; ++band) {
        std::sort(bands[band].indices.begin(),
            bands[band].indices.begin() + bands[band].count,
            [&](uint32_t a, uint32_t b) {
                return layout.speakers[a].azimuthDeg
                    < layout.speakers[b].azimuthDeg;
            });
    }

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(visual);
    NSColor* facetFill = isOutput
        ? s3g::clap_gui::color(0x547d78, 0.10)
        : s3g::clap_gui::color(0x657277, 0.09);
    NSColor* facetStroke = isOutput
        ? s3g::clap_gui::color(0x628b85, 0.44)
        : s3g::clap_gui::color(0x788287, 0.40);
    const NSPoint center = NSMakePoint(NSMidX(visual) + pan.x,
        NSMidY(visual) + 5.0 + pan.y);
    for (uint32_t band = 0u; band < bandCount; ++band) {
        const auto& current = bands[band];
        if (current.count >= 3u) {
            for (uint32_t index = 0u; index < current.count; ++index) {
                const uint32_t next = (index + 1u) % current.count;
                NSBezierPath* facet = [NSBezierPath bezierPath];
                [facet moveToPoint:center];
                [facet lineToPoint:points[current.indices[index]]];
                [facet lineToPoint:points[current.indices[next]]];
                [facet closePath];
                [facetFill setFill];
                [facet fill];
                [facetStroke setStroke];
                [facet setLineWidth:0.65];
                [facet stroke];
            }
        } else if (current.count == 2u) {
            [facetStroke setStroke];
            [NSBezierPath strokeLineFromPoint:points[current.indices[0u]]
                toPoint:points[current.indices[1u]]];
        } else if (current.count == 1u) {
            [facetStroke setStroke];
            [NSBezierPath strokeLineFromPoint:center
                toPoint:points[current.indices[0u]]];
        }
    }
    for (uint32_t band = 1u; band < bandCount; ++band) {
        const auto& inner = bands[band - 1u];
        const auto& outer = bands[band];
        for (uint32_t outerIndex = 0u; outerIndex < outer.count;
             ++outerIndex) {
            const uint32_t node = outer.indices[outerIndex];
            uint32_t nearestA = 0u;
            uint32_t nearestB = inner.count > 1u ? 1u : 0u;
            float distanceA = 1000.0f;
            float distanceB = 1000.0f;
            for (uint32_t candidate = 0u; candidate < inner.count;
                 ++candidate) {
                const float distance = s3g::formatUpscaleAngularDistance(
                    layout.speakers[node].azimuthDeg,
                    layout.speakers[inner.indices[candidate]].azimuthDeg);
                if (distance < distanceA) {
                    distanceB = distanceA;
                    nearestB = nearestA;
                    distanceA = distance;
                    nearestA = candidate;
                } else if (distance < distanceB) {
                    distanceB = distance;
                    nearestB = candidate;
                }
            }
            NSBezierPath* flap = [NSBezierPath bezierPath];
            [flap moveToPoint:points[node]];
            [flap lineToPoint:points[inner.indices[nearestA]]];
            [flap lineToPoint:points[inner.indices[nearestB]]];
            [flap closePath];
            [facetFill setFill];
            [flap fill];
            [facetStroke setStroke];
            [flap setLineWidth:0.8];
            [flap stroke];
        }
    }
    [s3g::clap_gui::color(0x8a9292, 0.70) setStroke];
    NSBezierPath* hub = [NSBezierPath bezierPath];
    [hub moveToPoint:NSMakePoint(center.x, center.y - 3.0)];
    [hub lineToPoint:NSMakePoint(center.x + 3.0, center.y)];
    [hub lineToPoint:NSMakePoint(center.x, center.y + 3.0)];
    [hub lineToPoint:NSMakePoint(center.x - 3.0, center.y)];
    [hub closePath];
    [hub stroke];
    [NSGraphicsContext restoreGraphicsState];
    [@"FRONT" drawAtPoint:NSMakePoint(NSMidX(visual) - 19.0,
        visual.origin.y + 8.0) withAttributes:attrs];
    if (bandCount > 1u) {
        [@"UNFOLDED ELEVATION" drawAtPoint:NSMakePoint(
            NSMaxX(visual) - 144.0, visual.origin.y + 8.0)
            withAttributes:attrs];
    }
}

- (void)drawMapNodes:(const s3g::FormatUpscaleLayoutData&)layout
    points:(const std::array<NSPoint, kChannels>&)points
    output:(bool)isOutput
    valueAttrs:(NSDictionary*)valueAttrs
{
    const uint32_t labelStride = layout.count <= 24u ? 1u
        : (layout.count <= 40u ? 2u : 4u);
    for (uint32_t index = 0u; index < layout.count; ++index) {
        const bool selected = isOutput
            ? (_selectionIsOutput && index == _selectedOutput)
            : (!_selectionIsOutput && index == _selectedInput);
        const CGFloat size = selected ? 12.0 : 10.0;
        const NSRect marker = NSMakeRect(points[index].x - size * 0.5,
            points[index].y - size * 0.5, size, size);
        if (isOutput) {
            const float peak = _plugin->outputPeaks[index].load(
                std::memory_order_relaxed);
            [s3g::clap_gui::color(0x424848) setFill];
            NSRectFill(marker);
            if (peak > 0.0001f) {
                const CGFloat fillHeight = marker.size.height
                    * std::min<CGFloat>(1.0, std::sqrt(peak));
                [s3g::clap_gui::color(0x709c96) setFill];
                NSRectFill(NSMakeRect(marker.origin.x,
                    NSMaxY(marker) - fillHeight,
                    marker.size.width, fillHeight));
            }
            [s3g::clap_gui::color(selected ? 0xf0f0f0 : 0xd0d0d0) setStroke];
            NSFrameRect(marker);
        } else {
            NSBezierPath* circle = [NSBezierPath bezierPathWithOvalInRect:marker];
            [s3g::clap_gui::color(selected ? 0xe0e0e0 : 0x7d8587) setFill];
            [circle fill];
            [s3g::clap_gui::color(0x111111) setStroke];
            [circle stroke];
        }

        const char* role = s3g::formatUpscaleRoleName(
            layout.speakers[index].role);
        if (role[0] == '\0' && index % labelStride != 0u && !selected)
            continue;
        NSString* base = role[0] != '\0' && (_designMap < 0 || selected)
            ? [NSString stringWithFormat:@"%s%u %s",
                isOutput ? "O" : "I", index + 1u, role]
            : [NSString stringWithFormat:@"%s%u",
                isOutput ? "O" : "I", index + 1u];
        NSString* label = selected
            ? [NSString stringWithFormat:@"%@  A%+.0f E%+.0f D%.2f",
                base,
                static_cast<double>(layout.speakers[index].azimuthDeg),
                static_cast<double>(layout.speakers[index].elevationDeg),
                static_cast<double>(layout.speakers[index].distance)]
            : (_designMap < 0
                && std::abs(layout.speakers[index].elevationDeg) > 1.0f
                ? [NSString stringWithFormat:@"%@ %+.0f°", base,
                    static_cast<double>(layout.speakers[index].elevationDeg)]
                : base);
        [label drawAtPoint:NSMakePoint(points[index].x + 7.0,
            points[index].y - 7.0) withAttributes:valueAttrs];
    }
}

- (void)drawMappingInputRect:(NSRect)inputRect
    outputRect:(NSRect)outputRect
    attrs:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    _selectedInput = std::min<uint32_t>(_selectedInput,
        std::max<uint32_t>(1u, inputLayout.count) - 1u);
    _selectedOutput = std::min<uint32_t>(_selectedOutput,
        std::max<uint32_t>(1u, outputLayout.count) - 1u);
    const NSRect inputVisual = NSMakeRect(inputRect.origin.x,
        inputRect.origin.y, inputRect.size.width, inputRect.size.height - 29.0);
    const NSRect outputVisual = NSMakeRect(outputRect.origin.x,
        outputRect.origin.y, outputRect.size.width, outputRect.size.height - 29.0);
    std::array<NSPoint, kChannels> inputPoints {};
    std::array<NSPoint, kChannels> outputPoints {};
    [self computePointsForLayout:inputLayout rect:inputVisual
        zoom:_inputZoom pan:_inputPan design:_designMap == 0
        points:inputPoints];
    [self computePointsForLayout:outputLayout rect:outputVisual
        zoom:_outputZoom pan:_outputPan design:_designMap == 1
        points:outputPoints];
    [self drawMapSurface:inputRect layout:inputLayout points:inputPoints
        output:false pan:_inputPan attrs:attrs style:style];
    [self drawMapSurface:outputRect layout:outputLayout points:outputPoints
        output:true pan:_outputPan attrs:attrs style:style];

    const CGFloat bridgeX = (NSMaxX(inputRect) + outputRect.origin.x) * 0.5;
    auto drawRoute = [&](uint32_t input, uint32_t output) {
        const float anchor = _plugin->dsp.targetAnchorGain(input, output);
        const float extension = _plugin->dsp.targetExtensionGain(input, output);
        const float magnitude = std::abs(anchor) + std::abs(extension);
        if (magnitude < 0.001f) return;
        const bool isExtension = std::abs(extension) >= std::abs(anchor);
        NSColor* color = isExtension
            ? s3g::clap_gui::color(extension < 0.0f
                ? 0x9a7584 : 0x6b9b94, 0.96)
            : s3g::clap_gui::color(0xd1d1d1, 0.88);
        [color setStroke];
        NSBezierPath* route = [NSBezierPath bezierPath];
        [route moveToPoint:inputPoints[input]];
        [route curveToPoint:outputPoints[output]
            controlPoint1:NSMakePoint(bridgeX,
                inputPoints[input].y)
            controlPoint2:NSMakePoint(bridgeX, outputPoints[output].y)];
        [route setLineWidth:0.8 + std::min(2.8f, magnitude * 2.2f)];
        if (extension < -0.0001f) {
            CGFloat dash[2] { 5.0, 3.0 };
            [route setLineDash:dash count:2 phase:0.0];
        }
        [route stroke];
    };
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSMakeRect(inputVisual.origin.x, inputVisual.origin.y,
        NSMaxX(outputVisual) - inputVisual.origin.x,
        inputVisual.size.height));
    if (_selectionIsOutput) {
        for (uint32_t input = 0u; input < inputLayout.count; ++input)
            drawRoute(input, _selectedOutput);
    } else {
        for (uint32_t output = 0u; output < outputLayout.count; ++output)
            drawRoute(_selectedInput, output);
    }
    [NSGraphicsContext restoreGraphicsState];

    if (_routeDragSource >= 0
        && static_cast<uint32_t>(_routeDragSource) < inputLayout.count) {
        [s3g::clap_gui::color(0x83b9b1, 0.95) setStroke];
        NSBezierPath* preview = [NSBezierPath bezierPath];
        [preview moveToPoint:inputPoints[static_cast<uint32_t>(
            _routeDragSource)]];
        [preview lineToPoint:_routeDragPoint];
        [preview setLineWidth:2.2];
        CGFloat dash[2] { 6.0, 3.0 };
        [preview setLineDash:dash count:2 phase:0.0];
        [preview stroke];
    }

    [NSGraphicsContext saveGraphicsState];
    NSRectClip(inputVisual);
    [self drawMapNodes:inputLayout points:inputPoints output:false
        valueAttrs:valueAttrs];
    [NSGraphicsContext restoreGraphicsState];
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(outputVisual);
    [self drawMapNodes:outputLayout points:outputPoints output:true
        valueAttrs:valueAttrs];
    [NSGraphicsContext restoreGraphicsState];

    NSString* inputFooter = _designMap == 0
        ? @"DRAG SPEAKER = ARRANGE AZ / EL   •   EMPTY SPACE = PAN"
        : (_selectionIsOutput
            ? @"DRAG INPUT → OUTPUT = CONNECT   •   EMPTY SPACE = PAN"
            : [NSString stringWithFormat:
                @"INPUT %u   •   DRAG TO AN OUTPUT TO CONNECT",
                _selectedInput + 1u]);
    [inputFooter drawAtPoint:NSMakePoint(inputRect.origin.x + 10.0,
        NSMaxY(inputRect) - 22.0) withAttributes:valueAttrs];
    NSString* outputFooter = _designMap == 1
        ? @"DRAG SPEAKER = ARRANGE AZ / EL   •   EMPTY SPACE = PAN"
        : (_selectionIsOutput
            ? [NSString stringWithFormat:
                @"OUTPUT %u   •   DRAG SAME CABLE AGAIN TO REMOVE",
                _selectedOutput + 1u]
            : @"DROP CABLE ON OUTPUT   •   CLICK OUTPUT = INSPECT COLUMN");
    [outputFooter drawAtPoint:NSMakePoint(outputRect.origin.x + 10.0,
        NSMaxY(outputRect) - 22.0) withAttributes:valueAttrs];
}

- (void)drawMapViewControls:(NSRect)panel
    zoom:(CGFloat)zoom
    pan:(NSPoint)pan
    attrs:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawToolboxHeaderButton(mapViewButtonRect(panel, 0u),
        panel, @"−", false, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(mapViewButtonRect(panel, 1u),
        panel, @"+", false, attrs, style);
    const bool centered = std::abs(pan.x) < 0.5 && std::abs(pan.y) < 0.5;
    s3g::clap_gui::drawToolboxHeaderButton(mapViewButtonRect(panel, 2u),
        panel, @"RECENTER", centered, attrs, style);
    const NSInteger map = panel.origin.x < 100.0 ? 0 : 1;
    s3g::clap_gui::drawToolboxHeaderButton(mapViewButtonRect(panel, 3u),
        panel, @"DESIGN", _designMap == map, attrs, style);
    NSString* zoomText = [NSString stringWithFormat:@"%3.0f%%", zoom * 100.0];
    const NSSize size = [zoomText sizeWithAttributes:attrs];
    [zoomText drawAtPoint:NSMakePoint(
        mapViewButtonRect(panel, 3u).origin.x - size.width - 8.0,
        panel.origin.y + 4.0) withAttributes:attrs];
}

- (void)drawCustomEditor:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    if (!_plugin || _designMap < 0) return;
    const auto layout = [self editableLayout];
    const uint32_t selected = std::min<uint32_t>(
        [self editableSelection], std::max<uint32_t>(1u, layout.count) - 1u);
    const auto& speaker = layout.speakers[selected];
    const uint32_t countMinimum = _designMap == 1
        ? std::max<uint32_t>(1u, _plugin->dsp.activeInputs()) : 1u;
    const uint32_t countMaximum = _designMap == 0
        ? std::max<uint32_t>(countMinimum, _plugin->dsp.activeOutputs())
        : kChannels;
    const CGFloat norms[5] {
        countMaximum > countMinimum
            ? static_cast<CGFloat>(layout.count - countMinimum)
                / static_cast<CGFloat>(countMaximum - countMinimum)
            : 0.0,
        layout.count > 1u
            ? static_cast<CGFloat>(selected) / static_cast<CGFloat>(layout.count - 1u)
            : 0.0,
        static_cast<CGFloat>(s3g::aedAzimuthSliderNorm(speaker.azimuthDeg)),
        static_cast<CGFloat>((speaker.elevationDeg + 90.0f) / 180.0f),
        static_cast<CGFloat>((speaker.distance - 0.1f) / 2.9f),
    };
    NSString* labels[5] { @"COUNT", @"SELECT", @"AZ", @"EL", @"DST" };
    NSString* values[5] {
        [NSString stringWithFormat:@"%u", layout.count],
        [NSString stringWithFormat:@"%u", selected + 1u],
        @"", @"", @""
    };
    for (uint32_t column = 0u; column < 5u; ++column) {
        const NSRect row = customEditorColumnRect(column);
        s3g::clap_gui::drawProcessorSlider(labels[column], values[column],
            std::clamp<CGFloat>(norms[column], 0.0, 1.0),
            row.origin.y + 5.0, row.origin.x, row.size.width,
            attrs, valueAttrs, style);
    }
    const auto direction = s3g::directionFromAed(
        speaker.azimuthDeg, speaker.elevationDeg);
    [[NSString stringWithFormat:@"AED  A%+.1f  E%+.1f  D%.2f",
        static_cast<double>(speaker.azimuthDeg),
        static_cast<double>(speaker.elevationDeg),
        static_cast<double>(speaker.distance)]
        drawAtPoint:NSMakePoint(kRightPanelRect.origin.x + 12.0,
            kRightPanelRect.origin.y + 215.0)
        withAttributes:valueAttrs];
    [[NSString stringWithFormat:@"XYZ  %+.2f  %+.2f  %+.2f",
        static_cast<double>(direction.x * speaker.distance),
        static_cast<double>(direction.y * speaker.distance),
        static_cast<double>(direction.z * speaker.distance)]
        drawAtPoint:NSMakePoint(kRightPanelRect.origin.x + 12.0,
            kRightPanelRect.origin.y + 237.0)
        withAttributes:valueAttrs];
    [@"CHANNEL ORDER = LIST ORDER"
        drawAtPoint:NSMakePoint(kRightPanelRect.origin.x + 12.0,
            kRightPanelRect.origin.y + 269.0)
        withAttributes:attrs];
    s3g::clap_gui::drawToolboxHeaderButton(customEditorActionRect(0u),
        kEquationPanelRect,
        _designMap == 0 ? @"FRONT 3" : @"3 × 3 TIERS",
        false, valueAttrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(customEditorActionRect(1u),
        kEquationPanelRect, @"COPY OTHER MAP", false, valueAttrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(customEditorActionRect(2u),
        kEquationPanelRect, @"DONE", true, valueAttrs, style);
    [self updateCustomValueFields];
}

- (void)drawMixEquation:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    auto nodeName = [](const s3g::FormatUpscaleLayoutData& layout,
                        uint32_t index, bool input) -> NSString* {
        const char* role = s3g::formatUpscaleRoleName(
            layout.speakers[index].role);
        return role[0] != '\0'
            ? [NSString stringWithFormat:@"%s%u %s",
                input ? "IN" : "OUT", index + 1u, role]
            : [NSString stringWithFormat:@"%s%u",
                input ? "IN" : "OUT", index + 1u];
    };

    NSMutableString* equation = [NSMutableString stringWithString:
        _selectionIsOutput
            ? [NSString stringWithFormat:@"%@ = ",
                nodeName(outputLayout, _selectedOutput, false)]
            : [NSString stringWithFormat:@"%@ ↦ ",
                nodeName(inputLayout, _selectedInput, true)]];
    uint32_t routeCount = 0u;
    uint32_t shownRoutes = 0u;
    float totalPower = 0.0f;
    float anchorPower = 0.0f;
    float extensionPower = 0.0f;
    const uint32_t candidates = _selectionIsOutput
        ? inputLayout.count : outputLayout.count;
    for (uint32_t candidate = 0u; candidate < candidates; ++candidate) {
        const uint32_t input = _selectionIsOutput
            ? candidate : _selectedInput;
        const uint32_t output = _selectionIsOutput
            ? _selectedOutput : candidate;
        const float anchor = _plugin->dsp.targetAnchorGain(input, output);
        const float extension = _plugin->dsp.targetExtensionGain(input, output);
        const float gain = anchor + extension;
        if (std::abs(gain) < 0.001f) continue;
        totalPower += gain * gain;
        anchorPower += anchor * anchor;
        extensionPower += extension * extension;
        ++routeCount;
        if (shownRoutes >= 6u) continue;
        const bool negative = gain < 0.0f;
        NSString* sign = shownRoutes == 0u
            ? (negative ? @"−" : @"")
            : (negative ? @" − " : @" + ");
        NSString* kind = std::abs(anchor) > 0.001f
                && std::abs(extension) > 0.001f
            ? @"A+E" : (std::abs(extension) > 0.001f ? @"E" : @"A");
        [equation appendFormat:@"%@%.3f·%@ [%@]", sign,
            static_cast<double>(std::abs(gain)),
            _selectionIsOutput
                ? nodeName(inputLayout, input, true)
                : nodeName(outputLayout, output, false),
            kind];
        ++shownRoutes;
    }
    if (routeCount == 0u)
        [equation appendString:@"∅   NO ACTIVE ROUTES"];
    else if (routeCount > shownRoutes)
        [equation appendFormat:@"   … %u MORE", routeCount - shownRoutes];

    NSString* stats = [NSString stringWithFormat:
        @"%@ POWER Σg² %.3f   •   ANCHOR %.3f   •   EXTENSION %.3f   •   %u ROUTE%s   •   CLICK A NODE TO SWITCH ROW / COLUMN",
        _selectionIsOutput ? @"COLUMN" : @"ROW",
        static_cast<double>(totalPower),
        static_cast<double>(anchorPower),
        static_cast<double>(extensionPower), routeCount,
        routeCount == 1u ? "" : "S"];
    [equation drawAtPoint:NSMakePoint(kEquationPanelRect.origin.x + 12.0,
        kEquationPanelRect.origin.y + 27.0) withAttributes:valueAttrs];
    [stats drawAtPoint:NSMakePoint(kEquationPanelRect.origin.x + 12.0,
        kEquationPanelRect.origin.y + 50.0) withAttributes:valueAttrs];
    (void)style;
}

- (void)drawConnectionControls:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const bool manual = _plugin->dsp.manualRoutesActive();
    NSString* instruction = manual
        ? @"DRAW MODE  •  DRAG AN INPUT CIRCLE TO AN OUTPUT SQUARE  •  REPEAT THE SAME CABLE TO REMOVE IT"
        : @"AUTO RECIPE  •  DRAW THE FIRST CABLE TO TURN THE VISIBLE MAP INTO AN EDITABLE CONNECTION GRAPH";
    [instruction drawAtPoint:NSMakePoint(kMethodPanelRect.origin.x + 12.0,
        kMethodPanelRect.origin.y + 34.0) withAttributes:valueAttrs];
    s3g::clap_gui::drawToolboxHeaderButton(connectionActionRect(0u),
        kMethodPanelRect, @"AUTO RECIPE", !manual, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(connectionActionRect(1u),
        kMethodPanelRect, @"CLEAR DRAWN", manual, attrs, style);
}

- (void)drawRouteSummary:(NSDictionary*)valueAttrs
{
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    uint32_t connections = 0u;
    uint32_t selectedConnections = 0u;
    for (uint32_t input = 0u; input < inputLayout.count; ++input) {
        for (uint32_t output = 0u; output < outputLayout.count; ++output) {
            const bool connected = std::abs(
                    _plugin->dsp.targetAnchorGain(input, output)) > 0.0001f
                || std::abs(_plugin->dsp.targetExtensionGain(
                    input, output)) > 0.0001f;
            if (!connected) continue;
            ++connections;
            if (input == _selectedInput) ++selectedConnections;
        }
    }
    const NSRect panel = cocoaPanelRect(kDistributionPanel);
    NSString* mode = _plugin->dsp.manualRoutesActive()
        ? @"DRAWN CONNECTION GRAPH" : @"AUTOMATIC RECIPE";
    NSString* line1 = [NSString stringWithFormat:
        @"%@   •   %u CONNECTION%s", mode, connections,
        connections == 1u ? "" : "S"];
    NSString* line2 = [NSString stringWithFormat:
        @"SELECTED INPUT %u   •   %u DESTINATION%s",
        _selectedInput + 1u, selectedConnections,
        selectedConnections == 1u ? "" : "S"];
    [line1 drawAtPoint:NSMakePoint(panel.origin.x + 14.0,
        panel.origin.y + 48.0) withAttributes:valueAttrs];
    [line2 drawAtPoint:NSMakePoint(panel.origin.x + 14.0,
        panel.origin.y + 78.0) withAttributes:valueAttrs];
    [@"DRAWN ROUTES ARE NORMALIZED TO EQUAL POWER FOR EACH INPUT"
        drawAtPoint:NSMakePoint(panel.origin.x + 14.0,
            panel.origin.y + 108.0) withAttributes:valueAttrs];
    [@"DESIGN CHANGES SPEAKER COORDINATES; CABLES KEEP CHANNEL ORDER"
        drawAtPoint:NSMakePoint(panel.origin.x + 14.0,
            panel.origin.y + 132.0) withAttributes:valueAttrs];
}

- (void)drawMatrixEditor:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    const uint32_t inputCount = std::max<uint32_t>(1u, inputLayout.count);
    const uint32_t outputCount = std::max<uint32_t>(1u, outputLayout.count);
    _selectedInput = std::min<uint32_t>(_selectedInput, inputCount - 1u);
    _selectedOutput = std::min<uint32_t>(_selectedOutput, outputCount - 1u);
    const auto geometry = matrixGeometry(inputCount, outputCount);

    [s3g::clap_gui::color(0x111313) setFill];
    NSRectFill(NSInsetRect(geometry.grid, -1.0, -1.0));

    [s3g::clap_gui::color(0x3d4444, geometry.cell < 8.0 ? 0.35 : 0.70)
        setStroke];
    for (uint32_t output = 0u; output <= outputCount; ++output) {
        const CGFloat x = geometry.grid.origin.x
            + static_cast<CGFloat>(output) * geometry.cell;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x,
            geometry.grid.origin.y) toPoint:NSMakePoint(x,
            NSMaxY(geometry.grid))];
    }
    for (uint32_t input = 0u; input <= inputCount; ++input) {
        const CGFloat y = geometry.grid.origin.y
            + static_cast<CGFloat>(input) * geometry.cell;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(
            geometry.grid.origin.x, y) toPoint:NSMakePoint(
            NSMaxX(geometry.grid), y)];
    }

    const uint32_t outputLabelStride = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::ceil(28.0 / geometry.cell)));
    const uint32_t inputLabelStride = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::ceil(15.0 / geometry.cell)));
    [@"OUTPUTS →" drawAtPoint:NSMakePoint(geometry.grid.origin.x,
        geometry.grid.origin.y - 83.0) withAttributes:attrs];
    [@"INPUTS ↓" drawAtPoint:NSMakePoint(geometry.grid.origin.x - 78.0,
        geometry.grid.origin.y - 83.0) withAttributes:attrs];

    // Elevation policy is most useful as a matrix grouping. Contiguous
    // channels on the same layer receive one bracket and tier label.
    uint32_t tierStart = 0u;
    while (tierStart < outputCount) {
        uint32_t tierEnd = tierStart + 1u;
        const float elevation = outputLayout.speakers[tierStart].elevationDeg;
        while (tierEnd < outputCount
            && std::abs(outputLayout.speakers[tierEnd].elevationDeg
                - elevation) < 0.5f)
            ++tierEnd;
        const CGFloat left = geometry.grid.origin.x
            + static_cast<CGFloat>(tierStart) * geometry.cell + 2.0;
        const CGFloat right = geometry.grid.origin.x
            + static_cast<CGFloat>(tierEnd) * geometry.cell - 2.0;
        const CGFloat y = geometry.grid.origin.y - 55.0;
        [s3g::clap_gui::color(0x6b8f8a, 0.80) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(left, y)
            toPoint:NSMakePoint(right, y)];
        NSString* tier = [NSString stringWithFormat:@"E%+.0f° TIER",
            static_cast<double>(elevation)];
        const NSSize tierSize = [tier sizeWithAttributes:valueAttrs];
        if (right - left >= tierSize.width + 8.0)
            [tier drawAtPoint:NSMakePoint(
                (left + right - tierSize.width) * 0.5,
                y - 19.0) withAttributes:valueAttrs];
        tierStart = tierEnd;
    }
    for (uint32_t output = 0u; output < outputCount; ++output) {
        const bool selected = output == _selectedOutput;
        const bool edge = output == 0u || output + 1u == outputCount;
        if (!selected && !edge && output % outputLabelStride != 0u) continue;
        if (!edge && static_cast<CGFloat>(outputCount - 1u - output)
                * geometry.cell < 28.0)
            continue;
        if (!selected && std::abs(static_cast<int>(output)
                - static_cast<int>(_selectedOutput)) * geometry.cell < 24.0)
            continue;
        const auto& speaker = outputLayout.speakers[output];
        NSString* label = [NSString stringWithFormat:@"O%u", output + 1u];
        const NSSize size = [label sizeWithAttributes:valueAttrs];
        [label drawAtPoint:NSMakePoint(geometry.grid.origin.x
                + (static_cast<CGFloat>(output) + 0.5) * geometry.cell
                - size.width * 0.5,
            geometry.grid.origin.y - 42.0) withAttributes:valueAttrs];
        if (geometry.cell >= 38.0) {
            NSString* azimuth = [NSString stringWithFormat:@"A%+.0f",
                static_cast<double>(speaker.azimuthDeg)];
            const NSSize azimuthSize = [azimuth sizeWithAttributes:valueAttrs];
            [azimuth drawAtPoint:NSMakePoint(geometry.grid.origin.x
                    + (static_cast<CGFloat>(output) + 0.5) * geometry.cell
                    - azimuthSize.width * 0.5,
                geometry.grid.origin.y - 23.0) withAttributes:valueAttrs];
        }
    }
    for (uint32_t input = 0u; input < inputCount; ++input) {
        const bool selected = input == _selectedInput;
        const bool edge = input == 0u || input + 1u == inputCount;
        if (!selected && !edge && input % inputLabelStride != 0u) continue;
        if (!selected && std::abs(static_cast<int>(input)
                - static_cast<int>(_selectedInput)) * geometry.cell < 14.0)
            continue;
        const auto& speaker = inputLayout.speakers[input];
        const char* role = s3g::formatUpscaleRoleName(speaker.role);
        NSString* label = geometry.cell >= 24.0 && role[0]
            ? [NSString stringWithFormat:@"I%u %s", input + 1u, role]
            : [NSString stringWithFormat:@"I%u", input + 1u];
        NSString* aed = [NSString stringWithFormat:@"A%+.0f E%+.0f",
            static_cast<double>(speaker.azimuthDeg),
            static_cast<double>(speaker.elevationDeg)];
        const NSSize labelSize = [label sizeWithAttributes:valueAttrs];
        const NSSize aedSize = [aed sizeWithAttributes:valueAttrs];
        const CGFloat centerY = geometry.grid.origin.y
            + (static_cast<CGFloat>(input) + 0.5) * geometry.cell;
        [label drawAtPoint:NSMakePoint(geometry.grid.origin.x
                - labelSize.width - 11.0,
            centerY - (geometry.cell >= 38.0 ? 15.0 : 7.0))
            withAttributes:valueAttrs];
        if (geometry.cell >= 38.0)
            [aed drawAtPoint:NSMakePoint(geometry.grid.origin.x
                    - aedSize.width - 11.0, centerY + 1.0)
                withAttributes:valueAttrs];
    }

    // Large matrices retain a precise selection cue without filling or
    // whitening an entire row or column.
    [s3g::clap_gui::color(0x79a7a1, 0.95) setStroke];
    const CGFloat selectedOutputX = geometry.grid.origin.x
        + (static_cast<CGFloat>(_selectedOutput) + 0.5) * geometry.cell;
    [NSBezierPath strokeLineFromPoint:NSMakePoint(
        selectedOutputX, geometry.grid.origin.y - 5.0)
        toPoint:NSMakePoint(selectedOutputX, geometry.grid.origin.y)];
    const CGFloat selectedInputY = geometry.grid.origin.y
        + (static_cast<CGFloat>(_selectedInput) + 0.5) * geometry.cell;
    [NSBezierPath strokeLineFromPoint:NSMakePoint(
        geometry.grid.origin.x - 5.0, selectedInputY)
        toPoint:NSMakePoint(geometry.grid.origin.x, selectedInputY)];

    uint32_t connectionCount = 0u;
    uint32_t selectedRouteCount = 0u;
    uint32_t selectedContributorCount = 0u;
    float selectedPower = 0.0f;
    float selectedColumnPower = 0.0f;
    for (uint32_t input = 0u; input < inputCount; ++input) {
        for (uint32_t output = 0u; output < outputCount; ++output) {
            const float gain = _plugin->dsp.targetAnchorGain(input, output)
                + _plugin->dsp.targetExtensionGain(input, output);
            const bool connected = std::abs(gain) > 0.0001f;
            if (connected) ++connectionCount;
            if (input == _selectedInput) {
                if (connected) {
                    ++selectedRouteCount;
                    selectedPower += gain * gain;
                }
            }
            if (output == _selectedOutput && connected) {
                ++selectedContributorCount;
                selectedColumnPower += gain * gain;
            }
            const NSPoint center = NSMakePoint(geometry.grid.origin.x
                    + (static_cast<CGFloat>(output) + 0.5) * geometry.cell,
                geometry.grid.origin.y
                    + (static_cast<CGFloat>(input) + 0.5) * geometry.cell);
            const CGFloat inactiveSize = std::clamp<CGFloat>(
                geometry.cell * 0.13, 0.8, 4.0);
            if (connected) {
                const float displayWeight = _plugin->dsp.manualRoutesActive()
                    ? std::abs(_plugin->dsp.manualWeight(input, output))
                    : std::min(1.0f, std::abs(gain));
                const CGFloat minimumActive = std::max<CGFloat>(
                    1.2, geometry.cell * 0.24);
                const CGFloat activeSize = std::clamp<CGFloat>(
                    geometry.cell * (0.20 + 0.58 * std::sqrt(displayWeight)),
                    minimumActive,
                    std::max<CGFloat>(minimumActive, geometry.cell - 1.5));
                [s3g::clap_gui::color(gain < 0.0f
                    ? 0x9a7584 : 0x6fa49d, 0.96) setFill];
                NSRectFill(NSMakeRect(center.x - activeSize * 0.5,
                    center.y - activeSize * 0.5,
                    activeSize, activeSize));
            } else if (geometry.cell >= 4.0) {
                [s3g::clap_gui::color(0x687070, 0.58) setFill];
                NSRectFill(NSMakeRect(center.x - inactiveSize * 0.5,
                    center.y - inactiveSize * 0.5,
                    inactiveSize, inactiveSize));
            }
        }
    }

    const char* inputRole = s3g::formatUpscaleRoleName(
        inputLayout.speakers[_selectedInput].role);
    const char* outputRole = s3g::formatUpscaleRoleName(
        outputLayout.speakers[_selectedOutput].role);
    NSString* selection = [NSString stringWithFormat:
        @"I%u%s%s → O%u%s%s   •   ROW %u DESTINATION%s   •   COLUMN %u CONTRIBUTOR%s",
        _selectedInput + 1u, inputRole[0] ? " " : "", inputRole,
        _selectedOutput + 1u, outputRole[0] ? " " : "", outputRole,
        selectedRouteCount, selectedRouteCount == 1u ? "" : "S",
        selectedContributorCount,
        selectedContributorCount == 1u ? "" : "S"];
    const float selectedWeight = _plugin->dsp.manualRoutesActive()
        ? std::abs(_plugin->dsp.manualWeight(
            _selectedInput, _selectedOutput))
        : std::min(1.0f, std::abs(
            _plugin->dsp.targetAnchorGain(_selectedInput, _selectedOutput)
            + _plugin->dsp.targetExtensionGain(
                _selectedInput, _selectedOutput)));
    const float selectedGain = _plugin->dsp.targetAnchorGain(
            _selectedInput, _selectedOutput)
        + _plugin->dsp.targetExtensionGain(_selectedInput, _selectedOutput);
    NSString* equation = nil;
    const bool midSideRecipe = _plugin->params.placement
        == s3g::FormatUpscalePlacement::MidSideSpread;
    if (midSideRecipe) {
        auto sideOf = [](const s3g::FormatUpscaleSpeaker& speaker) {
            if (speaker.azimuthDeg < -5.0f) return -1;
            if (speaker.azimuthDeg > 5.0f) return 1;
            return 0;
        };
        const int selectedSide = sideOf(
            inputLayout.speakers[_selectedInput]);
        uint32_t mirror = _selectedInput;
        float bestScore = 1000000.0f;
        for (uint32_t candidate = 0u; candidate < inputCount; ++candidate) {
            if (candidate == _selectedInput
                || sideOf(inputLayout.speakers[candidate])
                    != -selectedSide) continue;
            const float score = s3g::formatUpscaleAngularDistance(
                    inputLayout.speakers[candidate].azimuthDeg,
                    -inputLayout.speakers[_selectedInput].azimuthDeg)
                + std::abs(inputLayout.speakers[candidate].elevationDeg
                    - inputLayout.speakers[_selectedInput].elevationDeg)
                    * 1.35f;
            if (score < bestScore) {
                bestScore = score;
                mirror = candidate;
            }
        }
        if (selectedSide != 0 && mirror != _selectedInput) {
            const uint32_t left = selectedSide > 0
                ? _selectedInput : mirror;
            const uint32_t right = left == _selectedInput
                ? mirror : _selectedInput;
            const float leftGain = _plugin->dsp.targetAnchorGain(
                    left, _selectedOutput)
                + _plugin->dsp.targetExtensionGain(left, _selectedOutput);
            const float rightGain = _plugin->dsp.targetAnchorGain(
                    right, _selectedOutput)
                + _plugin->dsp.targetExtensionGain(right, _selectedOutput);
            constexpr float invSqrt2 = 0.7071067811865475f;
            const float midGain = (leftGain + rightGain) * invSqrt2;
            const float sideGain = (leftGain - rightGain) * invSqrt2;
            equation = [NSString stringWithFormat:
                @"WEIGHT %.0f%%   •   APPLIED g %+.3f   •   O%u = %+.3f M %+.3f S   •   ROW Σg² %.3f   COL Σg² %.3f",
                static_cast<double>(selectedWeight * 100.0f),
                static_cast<double>(selectedGain), _selectedOutput + 1u,
                static_cast<double>(midGain),
                static_cast<double>(sideGain),
                static_cast<double>(selectedPower),
                static_cast<double>(selectedColumnPower)];
        }
    }
    if (!equation) {
        equation = [NSString stringWithFormat:
            @"WEIGHT %.0f%%   •   APPLIED g %.3f   •   ROW Σg² %.3f   •   COLUMN Σg² %.3f   •   %s",
            static_cast<double>(selectedWeight * 100.0f),
            static_cast<double>(selectedGain),
            static_cast<double>(selectedPower),
            static_cast<double>(selectedColumnPower),
            s3g::formatUpscaleNormalizationName(_plugin->normalization)];
    }
    NSString* routeMode = nil;
    if (_plugin->dsp.manualRoutesActive()) {
        routeMode = midSideRecipe
            ? @"EDITABLE M/S MATRIX   •   MAGENTA = NEGATIVE SIDE POLARITY"
            : @"EDITABLE MATRIX";
    } else if (midSideRecipe) {
        routeMode = [NSString stringWithFormat:
            @"AUTO: M/S SPREAD / WIDTH %.0f%% / %s / ALL OUTPUT TIERS",
            static_cast<double>(s3g::kFormatUpscaleMidSideWidth * 100.0f),
            s3g::formatUpscaleRowShapeName(_plugin->autoRowShape)];
    } else {
        routeMode = [NSString stringWithFormat:
            @"AUTO: %s / %s / BASE %u TARGET%s",
            s3g::formatUpscalePlacementName(_plugin->params.placement),
            s3g::formatUpscaleRowShapeName(_plugin->autoRowShape),
            _plugin->params.copies,
            _plugin->params.copies == 1u ? "" : "S"];
    }
    NSString* weightEdit = geometry.cell >= 10.0
        ? @"DRAG ↑↓ IN CELL = WEIGHT"
        : @"DIST WEIGHT SLIDER = WEIGHT";
    NSString* mode = [NSString stringWithFormat:
        @"%@   •   %u CONNECTION%s   •   CLICK = SELECT / ADD   •   %@   •   RIGHT-CLICK = ZERO",
        routeMode,
        connectionCount, connectionCount == 1u ? "" : "S", weightEdit];
    [selection drawAtPoint:NSMakePoint(kMatrixPanelRect.origin.x + 14.0,
        NSMaxY(kMatrixPanelRect) - 92.0) withAttributes:valueAttrs];
    [equation drawAtPoint:NSMakePoint(kMatrixPanelRect.origin.x + 14.0,
        NSMaxY(kMatrixPanelRect) - 65.0) withAttributes:valueAttrs];
    [mode drawAtPoint:NSMakePoint(kMatrixPanelRect.origin.x + 14.0,
        NSMaxY(kMatrixPanelRect) - 38.0) withAttributes:valueAttrs];

    NSString* viewNames[3] { @"MATRIX", @"TIER RINGS", @"AED FLAT" };
    for (uint32_t view = 0u; view < 3u; ++view)
        s3g::clap_gui::drawToolboxHeaderButton(viewSelectorRect(view),
            kMatrixPanelRect, viewNames[view], view == 0u, attrs, style);
}

- (void)drawConnectionInspector:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    _selectedInput = std::min<uint32_t>(_selectedInput,
        std::max<uint32_t>(1u, inputLayout.count) - 1u);
    _selectedOutput = std::min<uint32_t>(_selectedOutput,
        std::max<uint32_t>(1u, outputLayout.count) - 1u);
    const auto& input = inputLayout.speakers[_selectedInput];
    const auto& output = outputLayout.speakers[_selectedOutput];
    const auto inputDirection = s3g::directionFromAed(
        input.azimuthDeg, input.elevationDeg);
    const auto outputDirection = s3g::directionFromAed(
        output.azimuthDeg, output.elevationDeg);
    const char* inputRole = s3g::formatUpscaleRoleName(input.role);
    const char* outputRole = s3g::formatUpscaleRoleName(output.role);
    NSString* inputAed = [NSString stringWithFormat:
        @"I%u%s%s   A%+.1f E%+.1f D%.2f", _selectedInput + 1u,
        inputRole[0] ? " " : "", inputRole,
        static_cast<double>(input.azimuthDeg),
        static_cast<double>(input.elevationDeg),
        static_cast<double>(input.distance)];
    NSString* inputXyz = [NSString stringWithFormat:
        @"XYZ   %+.2f  %+.2f  %+.2f",
        static_cast<double>(inputDirection.x * input.distance),
        static_cast<double>(inputDirection.y * input.distance),
        static_cast<double>(inputDirection.z * input.distance)];
    NSString* outputAed = [NSString stringWithFormat:
        @"O%u%s%s   A%+.1f E%+.1f D%.2f", _selectedOutput + 1u,
        outputRole[0] ? " " : "", outputRole,
        static_cast<double>(output.azimuthDeg),
        static_cast<double>(output.elevationDeg),
        static_cast<double>(output.distance)];
    NSString* outputXyz = [NSString stringWithFormat:
        @"XYZ   %+.2f  %+.2f  %+.2f",
        static_cast<double>(outputDirection.x * output.distance),
        static_cast<double>(outputDirection.y * output.distance),
        static_cast<double>(outputDirection.z * output.distance)];
    [inputAed drawAtPoint:NSMakePoint(kSelectedPanelRect.origin.x + 12.0,
        kSelectedPanelRect.origin.y + 30.0) withAttributes:valueAttrs];
    [inputXyz drawAtPoint:NSMakePoint(kSelectedPanelRect.origin.x + 12.0,
        kSelectedPanelRect.origin.y + 51.0) withAttributes:valueAttrs];
    [outputAed drawAtPoint:NSMakePoint(kSelectedPanelRect.origin.x + 12.0,
        kSelectedPanelRect.origin.y + 75.0) withAttributes:valueAttrs];
    [outputXyz drawAtPoint:NSMakePoint(kSelectedPanelRect.origin.x + 12.0,
        kSelectedPanelRect.origin.y + 96.0) withAttributes:valueAttrs];

    float weight = _plugin->dsp.manualRoutesActive()
        ? std::abs(_plugin->dsp.manualWeight(
            _selectedInput, _selectedOutput))
        : std::min(1.0f, std::abs(
            _plugin->dsp.targetAnchorGain(_selectedInput, _selectedOutput)
            + _plugin->dsp.targetExtensionGain(
                _selectedInput, _selectedOutput)));
    NSString* weightText = [NSString stringWithFormat:@"%.0f%%",
        static_cast<double>(weight * 100.0f)];
    const NSRect weightRow = selectedWeightRowRect();
    s3g::clap_gui::drawProcessorSlider(@"DIST WEIGHT", weightText, weight,
        weightRow.origin.y + 5.0, weightRow.origin.x, weightRow.size.width,
        attrs, valueAttrs, style);
    [@"NORM" drawAtPoint:NSMakePoint(kSelectedPanelRect.origin.x + 12.0,
        kSelectedPanelRect.origin.y + 160.0) withAttributes:attrs];
    NSString* normalizationNames[3] {
        @"ROW NORM", @"COL NORM", @"DUAL LIMIT" };
    for (uint32_t normalization = 0u; normalization < 3u; ++normalization)
        s3g::clap_gui::drawToolboxHeaderButton(
            sideNormalizationRect(normalization), kSelectedPanelRect,
            normalizationNames[normalization],
            static_cast<uint32_t>(_plugin->normalization) == normalization,
            attrs, style);
    [@"SHAPE"
        drawAtPoint:NSMakePoint(kSelectedPanelRect.origin.x + 12.0,
            kSelectedPanelRect.origin.y + 184.0)
        withAttributes:attrs];
    NSString* rowAxis = [NSString stringWithFormat:@"ROW I%u",
        _selectedInput + 1u];
    NSString* columnAxis = [NSString stringWithFormat:@"COLUMN O%u",
        _selectedOutput + 1u];
    s3g::clap_gui::drawToolboxHeaderButton(shapeAxisButtonRect(false),
        kSelectedPanelRect, rowAxis, !_shapeColumn, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(shapeAxisButtonRect(true),
        kSelectedPanelRect, columnAxis, _shapeColumn, attrs, style);
    NSString* shapeNames[4] { @"FLAT", @"CENTER", @"EDGES", @"TAPER" };
    for (uint32_t action = 0u; action < 4u; ++action)
        s3g::clap_gui::drawToolboxHeaderButton(rowShapeButtonRect(action),
            kSelectedPanelRect, shapeNames[action],
            !_shapeColumn && !_plugin->dsp.manualRoutesActive()
                && static_cast<uint32_t>(_plugin->autoRowShape) == action,
            attrs, style);

    const clap_id sliders[4] { kParamDelay, kParamDecor,
        kParamSmoothing, kParamOutputGain };
    for (clap_id param : sliders) {
        [self drawSliderControl:param row:sliderRowForParam(param)
            attrs:attrs valueAttrs:valueAttrs style:style];
    }
    [@"G DELAY / G DECOR = GLOBAL AMOUNTS"
        drawAtPoint:NSMakePoint(kExtensionPanel.frame.x + 12.0,
            kExtensionPanel.frame.y + kExtensionPanel.frame.height - 43.0)
        withAttributes:attrs];
    [@"OUTPUT-VARIED   •   SMOOTH / GAIN = GLOBAL"
        drawAtPoint:NSMakePoint(kExtensionPanel.frame.x + 12.0,
            kExtensionPanel.frame.y + kExtensionPanel.frame.height - 24.0)
        withAttributes:attrs];
}

- (void)drawUnfoldedLayoutPage:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    _selectedInput = std::min<uint32_t>(_selectedInput,
        std::max<uint32_t>(1u, inputLayout.count) - 1u);
    _selectedOutput = std::min<uint32_t>(_selectedOutput,
        std::max<uint32_t>(1u, outputLayout.count) - 1u);

    struct UnfoldedTier {
        float height = 0.0f;
        float heightSum = 0.0f;
        float minimumElevation = 90.0f;
        float maximumElevation = -90.0f;
        std::array<uint32_t, kChannels> indices {};
        uint32_t count = 0u;
        CGFloat y = 0.0;
        CGFloat planExtent = 0.0;
        CGFloat horizontalExtent = 0.0;
    };
    const NSRect inputMap = layoutProjectionRect(false);
    const NSRect outputMap = layoutProjectionRect(true);
    std::array<NSPoint, kChannels> inputPoints {};
    std::array<NSPoint, kChannels> outputPoints {};
    std::array<UnfoldedTier, kChannels> inputTiers {};
    std::array<UnfoldedTier, kChannels> outputTiers {};

    auto unfold = [&](const s3g::FormatUpscaleLayoutData& layout,
                      NSRect map,
                      std::array<NSPoint, kChannels>& points,
                      std::array<UnfoldedTier, kChannels>& tiers) {
        uint32_t tierCount = 0u;
        for (uint32_t index = 0u; index < layout.count; ++index) {
            const float height = s3g::formatUpscaleSpeakerHeight(
                layout.speakers[index]);
            const float elevation = layout.speakers[index].elevationDeg;
            uint32_t tier = 0u;
            while (tier < tierCount
                && std::abs(tiers[tier].height - height)
                    >= s3g::kFormatUpscaleTierHeightTolerance)
                ++tier;
            if (tier == tierCount) {
                tiers[tier].height = height;
                ++tierCount;
            }
            tiers[tier].heightSum += height;
            tiers[tier].minimumElevation = std::min(
                tiers[tier].minimumElevation, elevation);
            tiers[tier].maximumElevation = std::max(
                tiers[tier].maximumElevation, elevation);
            tiers[tier].indices[tiers[tier].count++] = index;
        }
        std::sort(tiers.begin(), tiers.begin() + tierCount,
            [](const UnfoldedTier& a, const UnfoldedTier& b) {
                return a.height < b.height;
            });
        const NSPoint center = NSMakePoint(NSMidX(map), NSMidY(map));
        const CGFloat maximumExtent = std::max<CGFloat>(24.0,
            std::min(map.size.width, map.size.height) * 0.5 - 38.0);
        const CGFloat innerExtent = maximumExtent * 0.28;
        const CGFloat outerExtent = std::max<CGFloat>(
            innerExtent + 16.0,
            maximumExtent - kTierRingDegreeLabelGutter);
        for (uint32_t tier = 0u; tier < tierCount; ++tier) {
            tiers[tier].height = tiers[tier].heightSum
                / static_cast<float>(std::max<uint32_t>(1u,
                    tiers[tier].count));
            tiers[tier].y = layoutProjectionPoint(
                map, 0.0f,
                (tiers[tier].minimumElevation
                    + tiers[tier].maximumElevation) * 0.5f).y;
            tiers[tier].planExtent = tierCount == 1u
                ? maximumExtent * 0.70
                : outerExtent - (outerExtent - innerExtent)
                    * static_cast<CGFloat>(tier)
                    / static_cast<CGFloat>(tierCount - 1u);
            std::sort(tiers[tier].indices.begin(),
                tiers[tier].indices.begin() + tiers[tier].count,
                [&](uint32_t a, uint32_t b) {
                    return layout.speakers[a].azimuthDeg
                        < layout.speakers[b].azimuthDeg;
                });
            for (uint32_t offset = 0u; offset < tiers[tier].count;
                 ++offset) {
                const auto& speaker = layout.speakers[
                    tiers[tier].indices[offset]];
                const auto direction = s3g::directionFromAed(
                    speaker.azimuthDeg, speaker.elevationDeg);
                tiers[tier].horizontalExtent = std::max<CGFloat>(
                    tiers[tier].horizontalExtent,
                    std::max(std::abs(direction.x),
                        std::abs(direction.y)) * speaker.distance);
            }
            for (uint32_t offset = 0u; offset < tiers[tier].count;
                 ++offset) {
                const uint32_t index = tiers[tier].indices[offset];
                if (_layoutOrigami) {
                    const auto& speaker = layout.speakers[index];
                    const auto direction = s3g::directionFromAed(
                        speaker.azimuthDeg, speaker.elevationDeg);
                    const CGFloat scale = tiers[tier].planExtent
                        / std::max<CGFloat>(
                            0.000001, tiers[tier].horizontalExtent);
                    const bool zenith = speaker.elevationDeg
                        >= kTierRingZenithElevation;
                    points[index] = NSMakePoint(
                        center.x - (zenith ? 0.0
                            : direction.y * speaker.distance * scale),
                        center.y - (zenith ? 0.0
                            : direction.x * speaker.distance * scale));
                } else {
                    points[index] = layoutProjectionPoint(map,
                        layout.speakers[index].azimuthDeg,
                        layout.speakers[index].elevationDeg);
                }
            }
        }
        return tierCount;
    };
    const uint32_t inputTierCount = unfold(
        inputLayout, inputMap, inputPoints, inputTiers);
    const uint32_t outputTierCount = unfold(
        outputLayout, outputMap, outputPoints, outputTiers);

    [s3g::clap_gui::color(0x111313) setFill];
    NSRectFill(kLayoutSurfaceRect);
    [s3g::clap_gui::color(0x3f4747, 0.85) setStroke];
    NSFrameRect(kLayoutSurfaceRect);
    [s3g::clap_gui::color(0x171a1a, 0.92) setFill];
    NSRectFill(inputMap);
    NSRectFill(outputMap);
    [s3g::clap_gui::color(0x394141, 0.78) setStroke];
    NSFrameRect(inputMap);
    NSFrameRect(outputMap);

    auto drawProjectionGrid = [&](NSRect map) {
        if (_layoutOrigami) {
            const NSPoint center = NSMakePoint(NSMidX(map), NSMidY(map));
            const CGFloat radius = std::max<CGFloat>(24.0,
                std::min(map.size.width, map.size.height) * 0.5 - 38.0);
            [s3g::clap_gui::color(0x667070, 0.34) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(
                center.x - radius, center.y)
                toPoint:NSMakePoint(center.x + radius, center.y)];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(
                center.x, center.y - radius)
                toPoint:NSMakePoint(center.x, center.y + radius)];
            NSBezierPath* listener = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(center.x - 2.5, center.y - 2.5, 5.0, 5.0)];
            [s3g::clap_gui::color(0xaeb7b7, 0.74) setFill];
            [listener fill];

            NSString* front = @"FRONT  A0°";
            NSString* rear = @"REAR  A±180°";
            NSString* left = @"LEFT  A+90°";
            NSString* right = @"RIGHT  A−90°";
            const NSSize frontSize = [front sizeWithAttributes:attrs];
            const NSSize rearSize = [rear sizeWithAttributes:attrs];
            const NSSize leftSize = [left sizeWithAttributes:attrs];
            [front drawAtPoint:NSMakePoint(
                center.x - frontSize.width * 0.5,
                center.y - radius - 19.0) withAttributes:attrs];
            [rear drawAtPoint:NSMakePoint(
                center.x - rearSize.width * 0.5,
                center.y + radius + 5.0) withAttributes:attrs];
            [left drawAtPoint:NSMakePoint(
                center.x - radius - leftSize.width - 6.0,
                center.y - 8.0) withAttributes:attrs];
            [right drawAtPoint:NSMakePoint(
                center.x + radius + 6.0,
                center.y - 8.0) withAttributes:attrs];
            return;
        }
        constexpr float azimuthGuides[3] { -90.0f, 0.0f, 90.0f };
        constexpr float elevationGuides[3] { 90.0f, 0.0f, -90.0f };
        for (const float azimuth : azimuthGuides) {
            const CGFloat x = layoutProjectionPoint(
                map, azimuth, 0.0f).x;
            [s3g::clap_gui::color(0x667070,
                std::abs(azimuth) < 0.5f ? 0.42 : 0.27) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(
                x, map.origin.y + kLayoutProjectionVerticalInset)
                toPoint:NSMakePoint(
                    x, NSMaxY(map) - kLayoutProjectionVerticalInset)];
            NSString* label = std::abs(azimuth) < 0.5f
                ? @"A0° FRONT"
                : [NSString stringWithFormat:@"A%+.0f°",
                    static_cast<double>(azimuth)];
            const NSSize size = [label sizeWithAttributes:attrs];
            [label drawAtPoint:NSMakePoint(x - size.width * 0.5,
                map.origin.y + 5.0) withAttributes:attrs];
        }
        for (const float elevation : elevationGuides) {
            const CGFloat y = layoutProjectionPoint(
                map, 0.0f, elevation).y;
            [s3g::clap_gui::color(0x667070,
                std::abs(elevation) < 0.5f ? 0.42 : 0.27) setStroke];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(
                map.origin.x + kLayoutProjectionHorizontalInset, y)
                toPoint:NSMakePoint(
                    NSMaxX(map) - kLayoutProjectionHorizontalInset, y)];
            NSString* label = [NSString stringWithFormat:@"E%+.0f°",
                static_cast<double>(elevation)];
            [label drawAtPoint:NSMakePoint(map.origin.x + 5.0, y - 8.0)
                withAttributes:attrs];
        }
    };
    drawProjectionGrid(inputMap);
    drawProjectionGrid(outputMap);

    auto drawTierGuides = [&](const std::array<UnfoldedTier, kChannels>& tiers,
                              uint32_t tierCount, NSRect map,
                              const std::array<NSPoint, kChannels>& points) {
        const uint32_t labelStride = tierCount <= 8u ? 1u
            : static_cast<uint32_t>(std::ceil(
                static_cast<double>(tierCount) / 8.0));
        for (uint32_t tier = 0u; tier < tierCount; ++tier) {
            if (_layoutOrigami) {
                const NSPoint center = NSMakePoint(NSMidX(map), NSMidY(map));
                [s3g::clap_gui::color(0x54716e,
                    tier % labelStride == 0u ? 0.58 : 0.24) setStroke];
                NSBezierPath* contour = [NSBezierPath bezierPath];
                uint32_t contourCount = 0u;
                for (uint32_t offset = 0u; offset < tiers[tier].count;
                     ++offset) {
                    const NSPoint point = points[
                        tiers[tier].indices[offset]];
                    if (std::hypot(point.x - center.x,
                            point.y - center.y) < 2.0)
                        continue;
                    if (contourCount++ == 0u) [contour moveToPoint:point];
                    else [contour lineToPoint:point];
                }
                if (contourCount >= 3u) [contour closePath];
                if (contourCount >= 2u) {
                    [contour setLineWidth:tier == 0u ? 1.35 : 0.85];
                    [contour stroke];
                }
                continue;
            }
            const CGFloat top = layoutProjectionPoint(map, 0.0f,
                tiers[tier].maximumElevation).y;
            const CGFloat bottom = layoutProjectionPoint(map, 0.0f,
                tiers[tier].minimumElevation).y;
            const bool elevationBand = tiers[tier].maximumElevation
                    - tiers[tier].minimumElevation > 0.75f;
            [s3g::clap_gui::color(0x54716e,
                tier % labelStride == 0u ? 0.48 : 0.20) setStroke];
            if (elevationBand) {
                // AED tier ranges remain unfilled: Cube/LPAC planes use only
                // this outline bracket and never read as selection blocks.
                const CGFloat bracketX = NSMaxX(map)
                    - kLayoutProjectionHorizontalInset;
                NSBezierPath* bracket = [NSBezierPath bezierPath];
                [bracket moveToPoint:NSMakePoint(bracketX - 8.0, top)];
                [bracket lineToPoint:NSMakePoint(bracketX, top)];
                [bracket lineToPoint:NSMakePoint(bracketX, bottom)];
                [bracket lineToPoint:NSMakePoint(bracketX - 8.0, bottom)];
                [bracket setLineWidth:1.0];
                [bracket stroke];
            } else {
                [NSBezierPath strokeLineFromPoint:NSMakePoint(
                    map.origin.x + kLayoutProjectionHorizontalInset,
                    tiers[tier].y)
                    toPoint:NSMakePoint(
                        NSMaxX(map) - kLayoutProjectionHorizontalInset,
                        tiers[tier].y)];
            }
            if (tier % labelStride != 0u) continue;
            NSString* tierTag = [NSString stringWithFormat:@"T%u", tier + 1u];
            const CGFloat tagY = (elevationBand
                ? (top + bottom) * 0.5 : tiers[tier].y) - 7.0;
            const NSRect tagRect = NSMakeRect(
                NSMaxX(map) - kLayoutProjectionHorizontalInset + 3.0,
                tagY, 22.0, 14.0);
            [tierTag drawAtPoint:NSMakePoint(
                tagRect.origin.x + 3.0, tagRect.origin.y)
                withAttributes:attrs];
        }
    };
    drawTierGuides(inputTiers, inputTierCount, inputMap, inputPoints);
    drawTierGuides(outputTiers, outputTierCount, outputMap, outputPoints);

    auto drawTierLegend = [&](const std::array<UnfoldedTier, kChannels>& tiers,
                              uint32_t tierCount, NSRect map) {
        if (tierCount == 0u) return;
        const uint32_t stride = tierCount <= 8u ? 1u
            : static_cast<uint32_t>(std::ceil(
                static_cast<double>(tierCount) / 8.0));
        const uint32_t visibleCount = static_cast<uint32_t>(std::ceil(
            static_cast<double>(tierCount) / static_cast<double>(stride)));
        const uint32_t columns = visibleCount > 4u ? 2u : 1u;
        const uint32_t rows = (visibleCount + columns - 1u) / columns;
        const CGFloat columnWidth = map.size.width
            / static_cast<CGFloat>(columns);
        uint32_t visible = 0u;
        for (uint32_t tier = 0u; tier < tierCount; tier += stride) {
            const uint32_t column = visible / rows;
            const uint32_t row = visible % rows;
            const bool elevationBand = tiers[tier].maximumElevation
                    - tiers[tier].minimumElevation > 0.75f;
            NSString* detail = elevationBand
                ? [NSString stringWithFormat:
                    @"T%u  Z%+.2f  E%+.1f…%+.1f°  ×%u",
                    tier + 1u,
                    static_cast<double>(tiers[tier].height),
                    static_cast<double>(tiers[tier].minimumElevation),
                    static_cast<double>(tiers[tier].maximumElevation),
                    tiers[tier].count]
                : [NSString stringWithFormat:@"T%u  E%+.1f°  ×%u",
                    tier + 1u,
                    static_cast<double>((tiers[tier].minimumElevation
                        + tiers[tier].maximumElevation) * 0.5f),
                    tiers[tier].count];
            [detail drawAtPoint:NSMakePoint(
                map.origin.x + static_cast<CGFloat>(column) * columnWidth,
                kLayoutSurfaceRect.origin.y + 56.0
                    + static_cast<CGFloat>(row) * 14.0)
                withAttributes:valueAttrs];
            ++visible;
        }
    };
    drawTierLegend(inputTiers, inputTierCount, inputMap);
    drawTierLegend(outputTiers, outputTierCount, outputMap);

    uint32_t connectionCount = 0u;
    for (uint32_t input = 0u; input < inputLayout.count; ++input) {
        for (uint32_t output = 0u; output < outputLayout.count; ++output) {
            const float gain = _plugin->dsp.targetAnchorGain(input, output)
                + _plugin->dsp.targetExtensionGain(input, output);
            if (std::abs(gain) > 0.0001f) ++connectionCount;
        }
    }
    struct FocusRoute {
        float anchor = 0.0f;
        float extension = 0.0f;
        float gain = 0.0f;
    };
    std::array<FocusRoute, kChannels> inputFocus {};
    std::array<FocusRoute, kChannels> outputFocus {};
    if (_selectionIsOutput) {
        for (uint32_t input = 0u; input < inputLayout.count; ++input) {
            inputFocus[input].anchor =
                _plugin->dsp.targetAnchorGain(input, _selectedOutput);
            inputFocus[input].extension =
                _plugin->dsp.targetExtensionGain(input, _selectedOutput);
            inputFocus[input].gain = inputFocus[input].anchor
                + inputFocus[input].extension;
        }
    } else {
        for (uint32_t output = 0u; output < outputLayout.count; ++output) {
            outputFocus[output].anchor =
                _plugin->dsp.targetAnchorGain(_selectedInput, output);
            outputFocus[output].extension =
                _plugin->dsp.targetExtensionGain(_selectedInput, output);
            outputFocus[output].gain = outputFocus[output].anchor
                + outputFocus[output].extension;
        }
    }
    auto maximumTierSize = [](const std::array<UnfoldedTier, kChannels>& tiers,
                              uint32_t tierCount) {
        uint32_t maximum = 1u;
        for (uint32_t tier = 0u; tier < tierCount; ++tier)
            maximum = std::max(maximum, tiers[tier].count);
        return maximum;
    };
    const CGFloat inputNodeSize = std::clamp<CGFloat>(
        (inputMap.size.width - 56.0)
            / static_cast<CGFloat>(maximumTierSize(
                inputTiers, inputTierCount)) * 0.62,
        3.0, 12.0);
    const CGFloat outputNodeSize = std::clamp<CGFloat>(
        (outputMap.size.width - 56.0)
            / static_cast<CGFloat>(maximumTierSize(
                outputTiers, outputTierCount)) * 0.62,
        3.0, 12.0);
    auto drawNodes = [&](const s3g::FormatUpscaleLayoutData& layout,
                         const std::array<NSPoint, kChannels>& points,
                         const std::array<FocusRoute, kChannels>& routes,
                         bool output, CGFloat baseNodeSize) {
        std::array<CGFloat, kChannels> nodeSizes {};
        std::array<NSRect, kChannels> nodeRects {};
        std::array<uint8_t, kChannels> selectedFlags {};
        std::array<uint8_t, kChannels> mappedFlags {};
        for (uint32_t index = 0u; index < layout.count; ++index) {
            const bool selected = output
                ? index == _selectedOutput : index == _selectedInput;
            const bool focusNode = output
                ? (_selectionIsOutput && selected)
                : (!_selectionIsOutput && selected);
            const bool mapped = std::abs(routes[index].gain) > 0.0001f;
            const CGFloat magnitude = std::min<CGFloat>(
                1.0, std::abs(routes[index].gain));
            CGFloat size = mapped
                ? baseNodeSize * (0.82 + 0.70 * std::sqrt(magnitude))
                : baseNodeSize;
            if (selected) size = std::max<CGFloat>(8.0, size + 3.0);
            if (focusNode) size = std::max<CGFloat>(10.0, size + 2.0);
            const NSRect node = NSMakeRect(points[index].x - size * 0.5,
                points[index].y - size * 0.5, size, size);
            nodeSizes[index] = size;
            nodeRects[index] = node;
            selectedFlags[index] = selected ? 1u : 0u;
            mappedFlags[index] = mapped ? 1u : 0u;
            NSColor* fill = s3g::clap_gui::color(0x3d4444, 0.94);
            if (focusNode)
                fill = s3g::clap_gui::color(output ? 0x75aaa3 : 0xd8d8d8,
                    0.99);
            else if (mapped)
                fill = routes[index].gain < 0.0f
                    ? s3g::clap_gui::color(0xa17386, 0.99)
                    : (std::abs(routes[index].extension)
                            >= std::abs(routes[index].anchor)
                        ? s3g::clap_gui::color(0x6eaaa2, 0.99)
                        : s3g::clap_gui::color(0xb8b8b8, 0.99));
            if (output) {
                [fill setFill];
                NSRectFill(node);
                [s3g::clap_gui::color(selected || mapped
                    ? 0xe0e0e0 : 0x777f7f, 0.95) setStroke];
                NSFrameRect(node);
            } else {
                NSBezierPath* circle =
                    [NSBezierPath bezierPathWithOvalInRect:node];
                [fill setFill];
                [circle fill];
                [s3g::clap_gui::color(selected || mapped
                    ? 0xe0e0e0 : 0x171919, 0.98) setStroke];
                [circle setLineWidth:selected || mapped ? 1.8 : 1.0];
                [circle stroke];
            }
        }

        // Attempt every label. Density, measured by actual projected label
        // and node collisions, is the only reason to omit one. Focused and
        // mapped labels reserve space before inactive labels.
        const NSRect map = output ? outputMap : inputMap;
        const NSPoint mapCenter = NSMakePoint(NSMidX(map), NSMidY(map));
        std::array<NSRect, kChannels> occupiedLabels {};
        uint32_t occupiedCount = 0u;
        auto candidateIsClear = [&](NSRect candidate, uint32_t ownNode) {
            if (!NSContainsRect(NSInsetRect(map, 3.0, 3.0), candidate))
                return false;
            const NSRect padded = NSInsetRect(candidate, -1.5, -1.5);
            for (uint32_t node = 0u; node < layout.count; ++node) {
                if (node == ownNode) continue;
                if (NSIntersectsRect(padded,
                        NSInsetRect(nodeRects[node], -2.0, -2.0)))
                    return false;
            }
            for (uint32_t label = 0u; label < occupiedCount; ++label)
                if (NSIntersectsRect(padded, occupiedLabels[label]))
                    return false;
            return true;
        };
        auto drawLabelPass = [&](bool priority) {
            for (uint32_t index = 0u; index < layout.count; ++index) {
                const bool important = selectedFlags[index] != 0u
                    || mappedFlags[index] != 0u;
                if (important != priority) continue;
                const char* role = s3g::formatUpscaleRoleName(
                    layout.speakers[index].role);
                NSString* label = layout.count <= 12u && role[0]
                    ? [NSString stringWithFormat:@"%s%u %s",
                        output ? "O" : "I", index + 1u, role]
                    : [NSString stringWithFormat:@"%s%u",
                        output ? "O" : "I", index + 1u];
                const NSSize labelSize = [label sizeWithAttributes:valueAttrs];
                const CGFloat gap = nodeSizes[index] * 0.5 + 4.0;
                std::array<NSPoint, 4u> candidates {};
                uint32_t candidateCount = 1u;
                if (!_layoutOrigami) {
                    candidates[0u] = NSMakePoint(
                        points[index].x - labelSize.width * 0.5,
                        points[index].y + gap);
                } else {
                    const CGFloat dx = points[index].x - mapCenter.x;
                    const CGFloat dy = points[index].y - mapCenter.y;
                    if (std::abs(dx) >= std::abs(dy)) {
                        candidates[0u] = dx >= 0.0
                            ? NSMakePoint(points[index].x + gap,
                                points[index].y - labelSize.height * 0.5)
                            : NSMakePoint(points[index].x - gap
                                    - labelSize.width,
                                points[index].y - labelSize.height * 0.5);
                    } else {
                        candidates[0u] = dy >= 0.0
                            ? NSMakePoint(points[index].x
                                    - labelSize.width * 0.5,
                                points[index].y + gap)
                            : NSMakePoint(points[index].x
                                    - labelSize.width * 0.5,
                                points[index].y - gap - labelSize.height);
                    }
                    candidates[1u] = NSMakePoint(points[index].x + gap,
                        points[index].y - labelSize.height * 0.5);
                    candidates[2u] = NSMakePoint(
                        points[index].x - labelSize.width * 0.5,
                        points[index].y + gap);
                    candidates[3u] = NSMakePoint(points[index].x - gap
                            - labelSize.width,
                        points[index].y - labelSize.height * 0.5);
                    candidateCount = 4u;
                }
                bool placed = false;
                NSRect chosen = NSZeroRect;
                for (uint32_t candidate = 0u;
                     candidate < candidateCount; ++candidate) {
                    const NSRect rect = NSMakeRect(candidates[candidate].x,
                        candidates[candidate].y,
                        labelSize.width, labelSize.height);
                    if (!candidateIsClear(rect, index)) continue;
                    chosen = rect;
                    placed = true;
                    break;
                }
                if (!placed && important) {
                    chosen = NSMakeRect(candidates[0u].x, candidates[0u].y,
                        labelSize.width, labelSize.height);
                    placed = true;
                }
                if (!placed) continue;
                [label drawAtPoint:chosen.origin withAttributes:valueAttrs];
                occupiedLabels[occupiedCount++] = chosen;
            }
        };
        drawLabelPass(true);
        drawLabelPass(false);
        if (output) _lastLayoutOutputLabelCount = occupiedCount;
        else _lastLayoutInputLabelCount = occupiedCount;
    };
    drawNodes(inputLayout, inputPoints, inputFocus, false, inputNodeSize);
    drawNodes(outputLayout, outputPoints, outputFocus, true, outputNodeSize);

    NSString* inputTitle = [NSString stringWithFormat:
        @"INPUT %s   %s   •   %u CHANNEL%s   •   %u TIER%s",
        _layoutOrigami ? "PLAN" : "AED",
        s3g::formatUpscaleLayoutName(_plugin->params.inputLayout),
        inputLayout.count, inputLayout.count == 1u ? "" : "S",
        inputTierCount, inputTierCount == 1u ? "" : "S"];
    NSString* outputTitle = [NSString stringWithFormat:
        @"OUTPUT %s   %s   •   %u CHANNEL%s   •   %u TIER%s",
        _layoutOrigami ? "PLAN" : "AED",
        s3g::formatUpscaleLayoutName(_plugin->params.outputLayout),
        outputLayout.count, outputLayout.count == 1u ? "" : "S",
        outputTierCount, outputTierCount == 1u ? "" : "S"];
    [inputTitle drawAtPoint:NSMakePoint(
        inputMap.origin.x, kLayoutSurfaceRect.origin.y + 36.0)
        withAttributes:attrs];
    [outputTitle drawAtPoint:NSMakePoint(
        outputMap.origin.x, kLayoutSurfaceRect.origin.y + 36.0)
        withAttributes:attrs];
    auto drawAzimuthAxis = [&](NSRect map) {
        const CGFloat y = NSMaxY(map) + 13.0;
        if (_layoutOrigami) {
            NSString* label =
                @"LOW / T1 = OUTER   •   HIGH = INNER   •   E+90° = CENTER";
            const NSSize size = [label sizeWithAttributes:valueAttrs];
            [label drawAtPoint:NSMakePoint(
                NSMidX(map) - size.width * 0.5, y)
                withAttributes:valueAttrs];
            return;
        }
        constexpr float ticks[5] {
            -180.0f, -90.0f, 0.0f, 90.0f, 180.0f };
        for (const float tick : ticks) {
            NSString* label = [NSString stringWithFormat:@"%+.0f°",
                static_cast<double>(tick)];
            const NSSize size = [label sizeWithAttributes:valueAttrs];
            const CGFloat x = layoutProjectionPoint(map, tick, 0.0f).x;
            [label drawAtPoint:NSMakePoint(x - size.width * 0.5, y)
                withAttributes:valueAttrs];
        }
    };
    drawAzimuthAxis(inputMap);
    drawAzimuthAxis(outputMap);
    uint32_t focusedCount = 0u;
    if (_selectionIsOutput) {
        for (uint32_t input = 0u; input < inputLayout.count; ++input)
            if (std::abs(inputFocus[input].gain) > 0.0001f) ++focusedCount;
    } else {
        for (uint32_t output = 0u; output < outputLayout.count; ++output)
            if (std::abs(outputFocus[output].gain) > 0.0001f) ++focusedCount;
    }
    NSString* footer = [NSString stringWithFormat:
        @"FOCUS %s%u   •   %u OF %u ROUTES   •   CLICK POINT = FOCUS   •   COLOR: GRAY ANCHOR / TEAL EXT / MAGENTA NEG   •   SIZE = |GAIN|",
        _selectionIsOutput ? "O" : "I",
        (_selectionIsOutput ? _selectedOutput : _selectedInput) + 1u,
        focusedCount, connectionCount];
    [footer drawAtPoint:NSMakePoint(
        kLayoutSurfaceRect.origin.x + 10.0,
        NSMaxY(kLayoutSurfaceRect) - 22.0)
        withAttributes:valueAttrs];

    if (!_layoutPopupChild)
        s3g::clap_gui::drawToolboxHeaderButton(viewSelectorRect(0u),
            kMatrixPanelRect, @"MATRIX", false, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(viewSelectorRect(1u),
        kMatrixPanelRect, @"TIER RINGS", _layoutOrigami, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(viewSelectorRect(2u),
        kMatrixPanelRect, @"AED FLAT", !_layoutOrigami, attrs, style);
    s3g::clap_gui::drawToolboxHeaderButton(layoutPopupActionRect(),
        kLayoutSurfaceRect, _layoutPopupChild ? @"DOCK" : @"POP OUT",
        _layoutPopupChild || (_layoutPanel && [_layoutPanel isVisible]),
        attrs, style);
}

- (void)drawSpatialSummary:(NSDictionary*)attrs
    valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const bool designing = _designMap >= 0;
    const bool designOutput = _designMap == 1;
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    const auto& displayLayout = designing
        ? (designOutput ? outputLayout : inputLayout) : outputLayout;
    const NSRect visual = NSMakeRect(kSpatialMapRect.origin.x,
        kSpatialMapRect.origin.y, kSpatialMapRect.size.width,
        kSpatialMapRect.size.height - 29.0);
    std::array<NSPoint, kChannels> displayPoints {};
    [self computePointsForLayout:displayLayout rect:visual
        zoom:_outputZoom pan:_outputPan design:designing
        points:displayPoints];
    [self drawMapSurface:kSpatialMapRect layout:displayLayout
        points:displayPoints output:designing ? designOutput : true
        pan:_outputPan attrs:attrs style:style];

    if (designing) {
        [NSGraphicsContext saveGraphicsState];
        NSRectClip(visual);
        [self drawMapNodes:displayLayout points:displayPoints
            output:designOutput valueAttrs:valueAttrs];
        [NSGraphicsContext restoreGraphicsState];
        [@"DRAG SPEAKER = ARRANGE AZ / EL   •   EMPTY SPACE = PAN"
            drawAtPoint:NSMakePoint(kSpatialMapRect.origin.x + 9.0,
                NSMaxY(kSpatialMapRect) - 22.0)
            withAttributes:valueAttrs];
        return;
    }

    std::array<NSPoint, kChannels> inputPoints {};
    [self computePointsForLayout:inputLayout rect:visual
        zoom:_outputZoom pan:_outputPan design:false points:inputPoints];
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(visual);
    if (_selectionIsOutput) {
        for (uint32_t input = 0u; input < inputLayout.count; ++input) {
            const float gain = _plugin->dsp.targetAnchorGain(
                    input, _selectedOutput)
                + _plugin->dsp.targetExtensionGain(input, _selectedOutput);
            if (std::abs(gain) < 0.0001f) continue;
            [s3g::clap_gui::color(0x76aaa3, 0.80) setStroke];
            [NSBezierPath strokeLineFromPoint:inputPoints[input]
                toPoint:displayPoints[_selectedOutput]];
            NSBezierPath* source = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(inputPoints[input].x - 4.0,
                    inputPoints[input].y - 4.0, 8.0, 8.0)];
            [s3g::clap_gui::color(0xd5d5d5, 0.90) setFill];
            [source fill];
        }
    } else {
        for (uint32_t output = 0u; output < outputLayout.count; ++output) {
            const float gain = _plugin->dsp.targetAnchorGain(
                    _selectedInput, output)
                + _plugin->dsp.targetExtensionGain(_selectedInput, output);
            if (std::abs(gain) < 0.0001f) continue;
            [s3g::clap_gui::color(0x76aaa3, 0.82) setStroke];
            [NSBezierPath strokeLineFromPoint:inputPoints[_selectedInput]
                toPoint:displayPoints[output]];
        }
        NSBezierPath* source = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(inputPoints[_selectedInput].x - 5.0,
                inputPoints[_selectedInput].y - 5.0, 10.0, 10.0)];
        [s3g::clap_gui::color(0xe0e0e0, 0.95) setFill];
        [source fill];
        [s3g::clap_gui::color(0x111111) setStroke];
        [source stroke];
    }

    const uint32_t labelStride = outputLayout.count <= 12u ? 1u
        : (outputLayout.count <= 32u ? 2u : 4u);
    for (uint32_t output = 0u; output < outputLayout.count; ++output) {
        bool connected = false;
        if (_selectionIsOutput) {
            if (output == _selectedOutput) {
                for (uint32_t input = 0u; input < inputLayout.count; ++input) {
                    const float gain = _plugin->dsp.targetAnchorGain(
                            input, output)
                        + _plugin->dsp.targetExtensionGain(input, output);
                    connected = connected || std::abs(gain) > 0.0001f;
                }
            }
        } else {
            const float gain = _plugin->dsp.targetAnchorGain(
                    _selectedInput, output)
                + _plugin->dsp.targetExtensionGain(_selectedInput, output);
            connected = std::abs(gain) > 0.0001f;
        }
        const bool selected = output == _selectedOutput;
        const CGFloat size = selected ? 11.0 : 8.0;
        const NSRect marker = NSMakeRect(displayPoints[output].x - size * 0.5,
            displayPoints[output].y - size * 0.5, size, size);
        [s3g::clap_gui::color(connected ? 0x719f99 : 0x404747,
            connected ? 0.98 : 0.88) setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xf0f0f0 : 0xaeb6b5, 0.90)
            setStroke];
        NSFrameRect(marker);
        if (output % labelStride != 0u && !connected && !selected) continue;
        NSString* label = [NSString stringWithFormat:@"O%u", output + 1u];
        [label drawAtPoint:NSMakePoint(displayPoints[output].x + 6.0,
            displayPoints[output].y - 7.0) withAttributes:valueAttrs];
    }
    [NSGraphicsContext restoreGraphicsState];
    NSString* footer = _selectionIsOutput
        ? [NSString stringWithFormat:@"OUTPUT O%u   •   CONTRIBUTING INPUTS",
            _selectedOutput + 1u]
        : [NSString stringWithFormat:@"INPUT I%u   •   CONNECTED OUTPUT ARRAY",
            _selectedInput + 1u];
    [footer drawAtPoint:NSMakePoint(kSpatialMapRect.origin.x + 9.0,
        NSMaxY(kSpatialMapRect) - 22.0) withAttributes:valueAttrs];
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const uint32_t count = menuCountForParam(_openMenu);
    if (count == 0u) return;
    std::array<NSString*, s3g::kFormatUpscaleLayoutCount> items {};
    for (uint32_t index = 0u; index < count; ++index)
        items[index] = [NSString stringWithUTF8String:
            menuValueName(_openMenu, index)];
    const auto menu = formatMenuGeometry(_openMenu);
    const int selected = static_cast<int>(roundedUint(
        getParamValue(*_plugin, _openMenu)));
    s3g::clap_gui::drawDropdownMenu(menu.first, menu.itemHeight,
        items.data(), menu.firstCount,
        selected < static_cast<int>(menu.firstCount) ? selected : -1,
        _hoverMenuItem >= 0
                && _hoverMenuItem < static_cast<int>(menu.firstCount)
            ? _hoverMenuItem : -1,
        attrs, style);
    s3g::clap_gui::drawDropdownMenu(menu.second, menu.itemHeight,
        items.data() + menu.firstCount, menu.secondCount,
        selected >= static_cast<int>(menu.firstCount)
            ? selected - static_cast<int>(menu.firstCount) : -1,
        _hoverMenuItem >= static_cast<int>(menu.firstCount)
            ? _hoverMenuItem - static_cast<int>(menu.firstCount) : -1,
        attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const s3g::clap_gui::Style style {};
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill(self.bounds);
    const s3g::gui_layout::Canvas canvas {
        static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight) };
    const auto titleBand = s3g::gui_layout::outputUtilityTitleBand(canvas);
    NSString* summary = [NSString stringWithFormat:@"%s → %s / %s%s%s",
        s3g::formatUpscaleLayoutName(_plugin->params.inputLayout),
        s3g::formatUpscaleLayoutName(_plugin->params.outputLayout),
        _page == 1u ? "LAYOUT" :
            (_plugin->dsp.manualRoutesActive() ? "MATRIX" : "AUTO "),
        _page == 1u ? "" :
            (_plugin->dsp.manualRoutesActive() ? "" :
                s3g::formatUpscalePlacementName(_plugin->params.placement)),
        _layoutPopupChild ? " / DETACHED" : ""];
    const char* presetName = _layoutPopupChild && _layoutPopupOwner
        ? _layoutPopupOwner->_titlePresetName : _titlePresetName;
    s3g::clap_gui::drawOutputUtilityTitleBand(@"s3g OUTPUT FORMAT UPSCALE 64",
        [NSString stringWithUTF8String:presetName], summary,
        titleBand, style);

    if (_page == 1u) {
        [self drawPanel:kMatrixPanelRect
            title:_layoutOrigami
                ? @"LAYOUT MAPPING — TIER RINGS / OVERHEAD PLAN"
                : @"LAYOUT MAPPING — COMMON FLATTENED AED PROJECTION"
            attrs:attrs style:style];
        [self drawUnfoldedLayoutPage:attrs
            valueAttrs:valueAttrs style:style];
        return;
    }

    [self drawPanel:kMatrixPanelRect
        title:@"CONNECTION MATRIX — INPUT ROWS / OUTPUT COLUMNS"
        attrs:attrs style:style];
    [self drawMatrixEditor:attrs valueAttrs:valueAttrs style:style];

    [self drawPanel:kFormatPanelRect title:@"FORMATS"
        attrs:attrs style:style];
    [self drawSideFormatControl:kParamInputLayout output:false
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSideFormatControl:kParamOutputLayout output:true
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSideAutoModeControl:attrs valueAttrs:valueAttrs style:style];

    if (_designMap >= 0) {
        [self drawPanel:kRightPanelRect title:_designMap == 0
                ? @"CUSTOM INPUT — AED / XYZ"
                : @"CUSTOM OUTPUT — AED / XYZ"
            attrs:attrs style:style];
        [self drawCustomEditor:attrs valueAttrs:valueAttrs style:style];
    } else {
        [self drawPanel:kSelectedPanelRect title:@"SELECTED CROSSPOINT"
            attrs:attrs style:style];
        [self drawPanel:cocoaPanelRect(kExtensionPanel)
            title:@"OUTPUT TREATMENT" attrs:attrs style:style];
        [self drawConnectionInspector:attrs valueAttrs:valueAttrs style:style];
    }
    [self drawOpenMenu:attrs style:style];
}

- (void)updateSlider:(NSPoint)point
{
    const ParamDef* def = findParam(_dragParam);
    if (!def) return;
    NSRect row = sliderRowForParam(_dragParam);
    const CGFloat start = static_cast<CGFloat>(
        s3g::gui_layout::processorControlX(row.origin.x));
    const CGFloat width = std::max<CGFloat>(20.0,
        static_cast<CGFloat>(s3g::gui_layout::processorTrackWidth(
            row.size.width)));
    const double normalized = std::clamp(
        static_cast<double>((point.x - start) / width), 0.0, 1.0);
    double value = def->minimum
        + normalized * (def->maximum - def->minimum);
    if (def->stepped) value = std::floor(value + 0.5);
    setParamValue(*_plugin, _dragParam, value);
    [self setNeedsDisplay:YES];
}

- (void)updateSelectedWeight:(NSPoint)point
{
    if (!_plugin) return;
    const NSRect hit = selectedWeightHitRect();
    const float magnitude = static_cast<float>(std::clamp<CGFloat>(
        (point.x - hit.origin.x) / std::max<CGFloat>(20.0, hit.size.width),
        0.0, 1.0));
    if (!_plugin->dsp.manualRoutesActive())
        _plugin->dsp.beginManualRoutesFromCurrent();
    const float current = _plugin->dsp.manualWeight(
        _selectedInput, _selectedOutput);
    const float polarity = current < 0.0f ? -1.0f : 1.0f;
    _plugin->dsp.setManualWeight(
        _selectedInput, _selectedOutput, polarity * magnitude);
    [self setNeedsDisplay:YES];
}

- (void)applyRowShape:(uint32_t)shape
{
    if (!_plugin || shape > 3u) return;
    if (!_plugin->dsp.manualRoutesActive()) {
        setParamValue(*_plugin, kParamAutoRowShape,
            static_cast<double>(shape));
        [self setNeedsDisplay:YES];
        return;
    }
    const auto& outputLayout = _plugin->dsp.outputLayout();
    std::array<uint32_t, kChannels> connected {};
    uint32_t count = 0u;
    for (uint32_t output = 0u; output < outputLayout.count; ++output) {
        if (_plugin->dsp.manualRoute(_selectedInput, output))
            connected[count++] = output;
    }
    if (!_plugin->dsp.manualRoute(_selectedInput, _selectedOutput)) {
        _plugin->dsp.setManualWeight(
            _selectedInput, _selectedOutput, 1.0f);
        connected[count++] = _selectedOutput;
        std::sort(connected.begin(), connected.begin() + count);
    }
    if (count == 0u) return;

    const auto focus = s3g::directionFromAed(
        outputLayout.speakers[_selectedOutput].azimuthDeg,
        outputLayout.speakers[_selectedOutput].elevationDeg);
    for (uint32_t rank = 0u; rank < count; ++rank) {
        float weight = 1.0f;
        if (count > 1u && (shape == 1u || shape == 2u)) {
            const float position = static_cast<float>(rank)
                / static_cast<float>(count - 1u);
            const float edgeDistance = std::abs(position * 2.0f - 1.0f);
            weight = shape == 1u
                ? 0.5f + 0.5f * (1.0f - edgeDistance)
                : 0.5f + 0.5f * edgeDistance;
        } else if (shape == 3u) {
            const auto point = s3g::directionFromAed(
                outputLayout.speakers[connected[rank]].azimuthDeg,
                outputLayout.speakers[connected[rank]].elevationDeg);
            const float dot = std::clamp(
                focus.x * point.x + focus.y * point.y + focus.z * point.z,
                -1.0f, 1.0f);
            const float angle = std::acos(dot);
            weight = std::max(0.25f, std::cos(angle * 0.5f));
        }
        const float current = _plugin->dsp.manualWeight(
            _selectedInput, connected[rank]);
        _plugin->dsp.setManualWeight(_selectedInput, connected[rank],
            (current < 0.0f ? -1.0f : 1.0f) * weight);
    }
    [self setNeedsDisplay:YES];
}

- (void)applyColumnShape:(uint32_t)shape
{
    if (!_plugin || shape > 3u) return;
    if (!_plugin->dsp.manualRoutesActive())
        _plugin->dsp.beginManualRoutesFromCurrent();
    const auto& inputLayout = _plugin->dsp.inputLayout();
    std::array<uint32_t, kChannels> connected {};
    uint32_t count = 0u;
    for (uint32_t input = 0u; input < inputLayout.count; ++input) {
        if (_plugin->dsp.manualRoute(input, _selectedOutput))
            connected[count++] = input;
    }
    if (!_plugin->dsp.manualRoute(_selectedInput, _selectedOutput)) {
        _plugin->dsp.setManualWeight(
            _selectedInput, _selectedOutput, 1.0f);
        connected[count++] = _selectedInput;
        std::sort(connected.begin(), connected.begin() + count);
    }
    if (count == 0u) return;

    const auto focus = s3g::directionFromAed(
        inputLayout.speakers[_selectedInput].azimuthDeg,
        inputLayout.speakers[_selectedInput].elevationDeg);
    for (uint32_t rank = 0u; rank < count; ++rank) {
        float weight = 1.0f;
        if (count > 1u && (shape == 1u || shape == 2u)) {
            const float position = static_cast<float>(rank)
                / static_cast<float>(count - 1u);
            const float edgeDistance = std::abs(position * 2.0f - 1.0f);
            weight = shape == 1u
                ? 0.5f + 0.5f * (1.0f - edgeDistance)
                : 0.5f + 0.5f * edgeDistance;
        } else if (shape == 3u) {
            const auto point = s3g::directionFromAed(
                inputLayout.speakers[connected[rank]].azimuthDeg,
                inputLayout.speakers[connected[rank]].elevationDeg);
            const float dot = std::clamp(
                focus.x * point.x + focus.y * point.y + focus.z * point.z,
                -1.0f, 1.0f);
            const float angle = std::acos(dot);
            weight = std::max(0.25f, std::cos(angle * 0.5f));
        }
        const float current = _plugin->dsp.manualWeight(
            connected[rank], _selectedOutput);
        _plugin->dsp.setManualWeight(connected[rank], _selectedOutput,
            (current < 0.0f ? -1.0f : 1.0f) * weight);
    }
    [self setNeedsDisplay:YES];
}

- (void)updateCustomSlider:(NSPoint)point
{
    if (!_plugin || _designMap < 0 || _dragCustom < 0
        || _dragCustom > 4) return;
    const uint32_t column = static_cast<uint32_t>(_dragCustom);
    const NSRect hit = customEditorSliderHitRect(column);
    const CGFloat normalized = std::clamp<CGFloat>(
        (point.x - hit.origin.x) / std::max<CGFloat>(20.0, hit.size.width),
        0.0, 1.0);
    auto layout = [self editableLayout];
    uint32_t selected = std::min<uint32_t>(
        [self editableSelection], std::max<uint32_t>(1u, layout.count) - 1u);
    if (column == 0u) {
        const uint32_t minimum = _designMap == 1
            ? std::max<uint32_t>(1u, _plugin->dsp.activeInputs()) : 1u;
        const uint32_t maximum = _designMap == 0
            ? std::max<uint32_t>(minimum, _plugin->dsp.activeOutputs())
            : kChannels;
        const uint32_t oldCount = layout.count;
        layout.count = std::clamp<uint32_t>(static_cast<uint32_t>(std::floor(
            normalized * static_cast<CGFloat>(maximum - minimum) + 0.5))
                + minimum,
            minimum, maximum);
        if (layout.count > oldCount) {
            const float step = 360.0f / static_cast<float>(layout.count);
            for (uint32_t index = oldCount; index < layout.count; ++index) {
                const float azimuth = s3g::formatUpscaleWrapDegrees(
                    -30.0f - step * static_cast<float>(index));
                s3g::formatUpscaleSetSpeaker(layout, index,
                    azimuth, 0.0f,
                    s3g::formatUpscaleRoleForAed(azimuth, 0.0f));
            }
        }
        selected = std::min<uint32_t>(selected, layout.count - 1u);
        if (_designMap == 1) _selectedOutput = selected;
        else _selectedInput = selected;
    } else if (column == 1u) {
        selected = layout.count > 1u
            ? static_cast<uint32_t>(std::floor(normalized
                * static_cast<CGFloat>(layout.count - 1u) + 0.5))
            : 0u;
        if (_designMap == 1) _selectedOutput = selected;
        else _selectedInput = selected;
    } else {
        auto& speaker = layout.speakers[selected];
        if (column == 2u)
            speaker.azimuthDeg = s3g::aedAzimuthFromSliderNorm(normalized);
        else if (column == 3u)
            speaker.elevationDeg = static_cast<float>(normalized * 180.0 - 90.0);
        else
            speaker.distance = static_cast<float>(0.1 + normalized * 2.9);
    }
    [self setEditableLayout:layout];
    [self updateCustomValueFields];
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const s3g::gui_layout::Canvas canvas {
        static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight) };
    const auto titleBand = s3g::gui_layout::outputUtilityTitleBand(canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            point, &_plugin->plugin, @"Format Upscale", titleBand,
            _titlePresetName, sizeof(_titlePresetName), kParamOutputGain)) {
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t view = 0u; view < 3u; ++view) {
        if (!NSPointInRect(point, viewSelectorRect(view))) continue;
        if (view == 0u) {
            if (_layoutPopupChild) return;
            _page = 0u;
            _designMap = -1;
            [self updateCustomValueFields];
            [self setNeedsDisplay:YES];
            return;
        }
        if (!_layoutPopupChild) {
            _page = 1u;
            _designMap = -1;
            [self updateCustomValueFields];
        }
        [self setDocumentationLayoutOrigami:view == 1u];
        return;
    }
    if (_page == 1u) {
        if (NSPointInRect(point, layoutPopupActionRect())) {
            if (_layoutPopupChild && _layoutPopupOwner)
                [_layoutPopupOwner dockLayoutPopup];
            else
                [self openLayoutPopup];
            return;
        }
        const auto selectLayoutNode = [&](bool output) {
            const auto& layout = output
                ? _plugin->dsp.outputLayout() : _plugin->dsp.inputLayout();
            const NSRect map = layoutProjectionRect(output);
            if (!NSPointInRect(point, map)) return false;
            std::array<NSPoint, kChannels> points {};
            layoutNodePoints(layout, map, _layoutOrigami, points);
            uint32_t nearest = 0u;
            CGFloat nearestDistance = 1000000.0;
            for (uint32_t index = 0u; index < layout.count; ++index) {
                const CGFloat dx = point.x - points[index].x;
                const CGFloat dy = point.y - points[index].y;
                const CGFloat distance = dx * dx + dy * dy;
                if (distance < nearestDistance) {
                    nearestDistance = distance;
                    nearest = index;
                }
            }
            if (nearestDistance > 14.0 * 14.0) return false;
            S3GFormatUpscaleMapView* root =
                _layoutPopupChild && _layoutPopupOwner
                ? _layoutPopupOwner : self;
            if (output) root->_selectedOutput = nearest;
            else root->_selectedInput = nearest;
            root->_selectionIsOutput = output;
            root->_shapeColumn = output;
            if (root->_layoutPopupView) {
                root->_layoutPopupView->_selectedInput = root->_selectedInput;
                root->_layoutPopupView->_selectedOutput = root->_selectedOutput;
                root->_layoutPopupView->_selectionIsOutput = output;
            }
            _selectedInput = root->_selectedInput;
            _selectedOutput = root->_selectedOutput;
            _selectionIsOutput = output;
            _shapeColumn = output;
            [root setNeedsDisplay:YES];
            [root->_layoutPopupView setNeedsDisplay:YES];
            [self setNeedsDisplay:YES];
            return true;
        };
        if (selectLayoutNode(false) || selectLayoutNode(true)) return;
        return;
    }
    if (_openMenu != CLAP_INVALID_ID) {
        const auto menu = formatMenuGeometry(_openMenu);
        int hit = s3g::clap_gui::dropdownHitIndex(point,
            menu.first, menu.itemHeight, menu.firstCount);
        if (hit < 0) {
            const int secondHit = s3g::clap_gui::dropdownHitIndex(point,
                menu.second, menu.itemHeight, menu.secondCount);
            if (secondHit >= 0)
                hit = static_cast<int>(menu.firstCount) + secondHit;
        }
        if (hit >= 0) {
            setParamValue(*_plugin, _openMenu, static_cast<double>(hit));
            if (_openMenu == kParamPlacement)
                _plugin->dsp.useAutomaticRoutes();
            if ((_openMenu == kParamInputLayout
                    || _openMenu == kParamOutputLayout)
                && static_cast<uint32_t>(hit)
                    != static_cast<uint32_t>(
                        s3g::FormatUpscaleLayout::Custom)) {
                _designMap = -1;
                [self updateCustomValueFields];
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t output = 0u; output < 2u; ++output) {
        if (!NSPointInRect(point, formatEditButtonRect(output != 0u)))
            continue;
        const NSInteger map = static_cast<NSInteger>(output);
        if (_designMap == map) {
            _designMap = -1;
            [self updateCustomValueFields];
        } else {
            [self enterDesignMap:map];
            _selectionIsOutput = output != 0u;
        }
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t action = 0u; action < 2u; ++action) {
        if (!NSPointInRect(point, sideAutoActionRect(action))) continue;
        if (action == 0u) _plugin->dsp.useAutomaticRoutes();
        else _plugin->dsp.clearManualRoutes();
        _designMap = -1;
        [self updateCustomValueFields];
        [self setNeedsDisplay:YES];
        return;
    }

    if (_designMap >= 0) {
        for (uint32_t action = 0u; action < 3u; ++action) {
            if (!NSPointInRect(point, customEditorActionRect(action)))
                continue;
            if (action == 0u) {
                const auto layout = _designMap == 0
                    ? s3g::formatUpscaleDefaultCustomInputLayout()
                    : s3g::formatUpscaleDefaultThreeTierOutputLayout();
                [self setEditableLayout:layout];
                if (_designMap == 0) _selectedInput = 0u;
                else _selectedOutput = 0u;
            } else if (action == 1u) {
                const auto layout = _designMap == 0
                    ? _plugin->dsp.outputLayout()
                    : _plugin->dsp.inputLayout();
                [self setEditableLayout:layout];
                if (_designMap == 0) _selectedInput = 0u;
                else _selectedOutput = 0u;
            } else {
                _designMap = -1;
            }
            [self updateCustomValueFields];
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t column = 0u; column < 5u; ++column) {
            if (!NSPointInRect(point, customEditorSliderHitRect(column)))
                continue;
            _dragCustom = static_cast<NSInteger>(column);
            [self updateCustomSlider:point];
            return;
        }
    }

    if (_designMap < 0) {
        for (uint32_t normalization = 0u; normalization < 3u;
             ++normalization) {
            if (!NSPointInRect(point,
                    sideNormalizationRect(normalization)))
                continue;
            setParamValue(*_plugin, kParamNormalization,
                static_cast<double>(normalization));
            [self setNeedsDisplay:YES];
            return;
        }
    }

    const clap_id menus[3] {
        kParamInputLayout, kParamOutputLayout, kParamPlacement };
    for (clap_id param : menus) {
        if (NSPointInRect(point, menuValueRectForParam(param))) {
            _openMenu = param;
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }

    if (_designMap < 0) {
        if (NSPointInRect(point, shapeAxisButtonRect(false))) {
            _shapeColumn = NO;
            _selectionIsOutput = false;
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, shapeAxisButtonRect(true))) {
            _shapeColumn = YES;
            _selectionIsOutput = true;
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t shape = 0u; shape < 4u; ++shape) {
            if (!NSPointInRect(point, rowShapeButtonRect(shape))) continue;
            if (_shapeColumn) [self applyColumnShape:shape];
            else [self applyRowShape:shape];
            return;
        }
        if (NSPointInRect(point, selectedWeightHitRect())) {
            _dragWeight = YES;
            [self updateSelectedWeight:point];
            return;
        }
        const clap_id sliders[4] { kParamDelay, kParamDecor,
            kParamSmoothing, kParamOutputGain };
        for (clap_id param : sliders) {
            if (!NSPointInRect(point, sliderHitRectForParam(param))) continue;
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(event,
                    &_plugin->plugin, param, &defaultValue)) {
                setParamValue(*_plugin, param, defaultValue);
            } else {
                _dragParam = param;
                [self updateSlider:point];
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }

    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    const auto geometry = matrixGeometry(inputLayout.count,
        outputLayout.count);
    for (uint32_t input = 0u; input < inputLayout.count; ++input) {
        const NSRect labelRect = NSMakeRect(geometry.grid.origin.x - 92.0,
            geometry.grid.origin.y
                + static_cast<CGFloat>(input) * geometry.cell,
            88.0, geometry.cell);
        if (!NSPointInRect(point, labelRect)) continue;
        _selectedInput = input;
        _selectionIsOutput = false;
        _shapeColumn = NO;
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t output = 0u; output < outputLayout.count; ++output) {
        const NSRect labelRect = NSMakeRect(geometry.grid.origin.x
                + static_cast<CGFloat>(output) * geometry.cell,
            geometry.grid.origin.y - 45.0, geometry.cell, 43.0);
        if (!NSPointInRect(point, labelRect)) continue;
        _selectedOutput = output;
        _selectionIsOutput = true;
        _shapeColumn = YES;
        [self setNeedsDisplay:YES];
        return;
    }
    uint32_t matrixInput = 0u;
    uint32_t matrixOutput = 0u;
    if (matrixCellAtPoint(point, inputLayout.count, outputLayout.count,
            matrixInput, matrixOutput)) {
        const bool connected = std::abs(_plugin->dsp.targetAnchorGain(
                matrixInput, matrixOutput)) > 0.0001f
            || std::abs(_plugin->dsp.targetExtensionGain(
                matrixInput, matrixOutput)) > 0.0001f;
        if (!_plugin->dsp.manualRoutesActive())
            _plugin->dsp.beginManualRoutesFromCurrent();
        _selectedInput = matrixInput;
        _selectedOutput = matrixOutput;
        _selectionIsOutput = false;
        if ([event clickCount] >= 2) {
            _plugin->dsp.setManualWeight(matrixInput, matrixOutput,
                connected ? 0.0f : 1.0f);
        } else {
            if (!connected)
                _plugin->dsp.setManualWeight(matrixInput, matrixOutput, 1.0f);
            _matrixPainting = YES;
            _matrixPaintConnected = YES;
            _matrixWeightAdjusting = NO;
            _matrixDragOrigin = point;
            _matrixDragStartWeight = _plugin->dsp.manualWeight(
                matrixInput, matrixOutput);
            _matrixPaintInput = static_cast<NSInteger>(matrixInput);
            _matrixPaintOutput = static_cast<NSInteger>(matrixOutput);
        }
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)rightMouseDown:(NSEvent*)event
{
    if (!_plugin || _page == 1u || _designMap >= 0) {
        [super rightMouseDown:event];
        return;
    }
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_openMenu != CLAP_INVALID_ID) {
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    const auto& inputLayout = _plugin->dsp.inputLayout();
    const auto& outputLayout = _plugin->dsp.outputLayout();
    uint32_t input = 0u;
    uint32_t output = 0u;
    if (!matrixCellAtPoint(point, inputLayout.count, outputLayout.count,
            input, output)) {
        [super rightMouseDown:event];
        return;
    }
    if (!_plugin->dsp.manualRoutesActive())
        _plugin->dsp.beginManualRoutesFromCurrent();
    _plugin->dsp.setManualWeight(input, output, 0.0f);
    _selectedInput = input;
    _selectedOutput = output;
    _selectionIsOutput = false;
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    const auto menu = formatMenuGeometry(_openMenu);
    _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(
        point, menu.first, menu.itemHeight, menu.firstCount);
    if (_hoverMenuItem < 0) {
        const int second = s3g::clap_gui::dropdownHitIndex(
            point, menu.second, menu.itemHeight, menu.secondCount);
        if (second >= 0)
            _hoverMenuItem = static_cast<int>(menu.firstCount) + second;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_page == 1u) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_matrixPainting) {
        uint32_t input = 0u;
        uint32_t output = 0u;
        const auto& inputLayout = _plugin->dsp.inputLayout();
        const auto& outputLayout = _plugin->dsp.outputLayout();
        const auto geometry = matrixGeometry(
            inputLayout.count, outputLayout.count);
        const bool inside = matrixCellAtPoint(point,
            inputLayout.count, outputLayout.count, input, output);
        const bool sameCell = inside
            && static_cast<NSInteger>(input) == _matrixPaintInput
            && static_cast<NSInteger>(output) == _matrixPaintOutput;
        if (!_matrixWeightAdjusting && sameCell
            && geometry.cell >= 10.0
            && std::abs(point.y - _matrixDragOrigin.y) >= 2.0) {
            _matrixWeightAdjusting = YES;
        }
        if (_matrixWeightAdjusting) {
            const float polarity = _matrixDragStartWeight < 0.0f
                ? -1.0f : 1.0f;
            const float magnitude = static_cast<float>(std::clamp<CGFloat>(
                std::abs(_matrixDragStartWeight)
                    + (_matrixDragOrigin.y - point.y)
                        / std::max<CGFloat>(12.0, geometry.cell),
                0.0, 1.0));
            _plugin->dsp.setManualWeight(
                static_cast<uint32_t>(_matrixPaintInput),
                static_cast<uint32_t>(_matrixPaintOutput),
                polarity * magnitude);
            _selectedInput = static_cast<uint32_t>(_matrixPaintInput);
            _selectedOutput = static_cast<uint32_t>(_matrixPaintOutput);
            [self setNeedsDisplay:YES];
        } else if (inside && !sameCell) {
            _plugin->dsp.setManualRoute(input, output,
                _matrixPaintConnected);
            _matrixPaintInput = static_cast<NSInteger>(input);
            _matrixPaintOutput = static_cast<NSInteger>(output);
            _matrixDragOrigin = point;
            _matrixDragStartWeight = _plugin->dsp.manualWeight(input, output);
            _selectedInput = input;
            _selectedOutput = output;
            _selectionIsOutput = false;
            [self setNeedsDisplay:YES];
        }
        return;
    }
    if (_dragWeight) {
        [self updateSelectedWeight:point];
        return;
    }
    if (_dragCustom >= 0) {
        [self updateCustomSlider:point];
        return;
    }
    if (_dragParam != CLAP_INVALID_ID) [self updateSlider:point];
}

- (void)mouseUp:(NSEvent*)event
{
    if (_matrixPainting) {
        _matrixPainting = NO;
        _matrixWeightAdjusting = NO;
        _matrixPaintInput = -1;
        _matrixPaintOutput = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    (void)event;
    _dragWeight = NO;
    _dragParam = CLAP_INVALID_ID;
    _dragCustom = -1;
}

- (void)scrollWheel:(NSEvent*)event
{
    [super scrollWheel:event];
}

@end


namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GFormatUpscaleMapView alloc]
        initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView),
            kGuiWidth, kGuiHeight, 480u, 360u)) {
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return;
    auto* view = static_cast<S3GFormatUpscaleMapView*>(instance->guiView);
    [view stopRefreshTimer];
    [view destroyLayoutPopup];
    s3g::clap_gui::destroyResponsiveViewport(
        instance->guiViewport, instance->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height, 480u, 360u);
}

bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height, 480u, 360u);
}

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* instance = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        instance->guiViewport, static_cast<NSView*>(window->cocoa),
        instance->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView
        || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    instance->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3GFormatUpscaleMapView*>(instance->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GFormatUpscaleMapView*>(instance->guiView);
    [view hideLayoutPopup];
    [view stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

} // namespace

#endif

namespace {

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
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.format-upscale-64",
    "s3g Output Format Upscale 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Direct non-ambisonic format upscaler with a format-sized routing matrix.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    applyParams(*instance);
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
