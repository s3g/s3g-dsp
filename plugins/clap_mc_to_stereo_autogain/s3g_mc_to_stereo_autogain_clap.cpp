#include "s3g_mc_to_stereo.h"

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
#include <cstdint>
#include <new>

namespace {

constexpr uint32_t kStateVersion = 2;
constexpr uint32_t kLegacyStateVersion = 1;
constexpr uint32_t kInputBusChannels = s3g::kMcToStereoMaxInputChannels;
constexpr uint32_t kStereoChannels = 2;
constexpr double kGainRampMs = 48.0;

enum ParamId : clap_id {
    kParamInputChannels = 1,
    kParamWidth = 2,
    kParamRotation = 3,
    kParamAutogain = 4,
    kParamOutputGain = 5,
    kParamLayout = 6,
    kParamLayoutWeight = 7,
    kParamAttenuation3d = 8,
    kParamDistance3d = 9,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::McStereoParams params {};
};

struct SavedStateV1 {
    uint32_t version = kLegacyStateVersion;
    uint32_t inputChannels = 8;
    float widthPercent = 100.0f;
    float rotationDegrees = 0.0f;
    s3g::McStereoAutogain autogain = s3g::McStereoAutogain::PowerSqrtN;
    float outputGainDb = 0.0f;
    s3g::McStereoLayout layout = s3g::McStereoLayout::RingProjection;
    float layoutWeightPercent = 100.0f;
    float attenuation3dPercent = 45.0f;
};

s3g::McStereoParams sanitizeParams(s3g::McStereoParams params);

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::McStereoParams params {};
    std::array<s3g::McStereoChannelGains, kInputBusChannels> gains {};
    std::array<s3g::McStereoChannelGains, kInputBusChannels> currentGains {};
    std::array<s3g::McStereoChannelGains, kInputBusChannels> rampStartGains {};
    uint32_t cachedAvailableInputChannels = 0;
    uint32_t cachedResolvedInputChannels = 0;
    bool gainsDirty = true;
    bool gainsInitialized = false;
    uint32_t gainRampRemaining = 0;
    uint32_t gainRampTotal = 0;
    std::atomic<float> outputPeakLeft { 0.0f };
    std::atomic<float> outputPeakRight { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

const char* layoutName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 7u)) {
    case 1: return "Linear left-right";
    case 2: return "Odd/even stereo";
    case 3: return "Center-out";
    case 4: return "Pair-preserving";
    case 5: return "Sphere projection";
    case 6: return "Hemisphere projection";
    case 7: return "Cube projection";
    case 0:
    default: return "Ring projection";
    }
}

const char* autogainName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 2u)) {
    case 1: return "Power/sqrt(N)";
    case 2: return "Energy sum";
    case 0:
    default: return "Off";
    }
}

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

bool isProjectionLayout(s3g::McStereoLayout layout)
{
    return layout == s3g::McStereoLayout::SphereProjection
        || layout == s3g::McStereoLayout::HemisphereProjection
        || layout == s3g::McStereoLayout::CubeProjection;
}

s3g::McStereoParams effectiveGainParams(s3g::McStereoParams params)
{
    params = sanitizeParams(params);
    if (!isProjectionLayout(params.layout)) {
        params.attenuation3dPercent = 0.0f;
        params.distance3dPercent = 100.0f;
    }
    return params;
}

bool floatChanged(float a, float b)
{
    return std::abs(a - b) > 0.0001f;
}

bool gainParamsChanged(const s3g::McStereoParams& a, const s3g::McStereoParams& b)
{
    return a.inputChannels != b.inputChannels
        || a.autogain != b.autogain
        || a.layout != b.layout
        || floatChanged(a.widthPercent, b.widthPercent)
        || floatChanged(a.rotationDegrees, b.rotationDegrees)
        || floatChanged(a.outputGainDb, b.outputGainDb)
        || floatChanged(a.layoutWeightPercent, b.layoutWeightPercent)
        || floatChanged(a.attenuation3dPercent, b.attenuation3dPercent)
        || floatChanged(a.distance3dPercent, b.distance3dPercent);
}

s3g::McStereoParams sanitizeParams(s3g::McStereoParams params)
{
    params.inputChannels = s3g::clampInputChannels(params.inputChannels);
    params.widthPercent = s3g::clampf(params.widthPercent, 0.0f, 200.0f);
    params.rotationDegrees = s3g::clampf(params.rotationDegrees, -180.0f, 180.0f);
    params.autogain = static_cast<s3g::McStereoAutogain>(std::min<uint32_t>(static_cast<uint32_t>(params.autogain), 2u));
    params.outputGainDb = s3g::clampf(params.outputGainDb, -24.0f, 24.0f);
    params.layout = static_cast<s3g::McStereoLayout>(std::min<uint32_t>(static_cast<uint32_t>(params.layout), 7u));
    params.layoutWeightPercent = s3g::clampf(params.layoutWeightPercent, 0.0f, 100.0f);
    params.attenuation3dPercent = s3g::clampf(params.attenuation3dPercent, 0.0f, 100.0f);
    params.distance3dPercent = s3g::clampf(params.distance3dPercent, 0.0f, 200.0f);
    return params;
}

void setParamValue(Plugin& p, clap_id paramId, double value)
{
    const s3g::McStereoParams before = effectiveGainParams(p.params);
    switch (paramId) {
    case kParamInputChannels:
        p.params.inputChannels = s3g::clampInputChannels(roundedUint(value));
        break;
    case kParamWidth:
        p.params.widthPercent = s3g::clampf(static_cast<float>(value), 0.0f, 200.0f);
        break;
    case kParamRotation:
        p.params.rotationDegrees = s3g::clampf(static_cast<float>(value), -180.0f, 180.0f);
        break;
    case kParamAutogain:
        p.params.autogain = static_cast<s3g::McStereoAutogain>(std::min<uint32_t>(roundedUint(value), 2u));
        break;
    case kParamOutputGain:
        p.params.outputGainDb = s3g::clampf(static_cast<float>(value), -24.0f, 24.0f);
        break;
    case kParamLayout:
        p.params.layout = static_cast<s3g::McStereoLayout>(std::min<uint32_t>(roundedUint(value), 7u));
        break;
    case kParamLayoutWeight:
        p.params.layoutWeightPercent = s3g::clampf(static_cast<float>(value), 0.0f, 100.0f);
        break;
    case kParamAttenuation3d:
        p.params.attenuation3dPercent = s3g::clampf(static_cast<float>(value), 0.0f, 100.0f);
        break;
    case kParamDistance3d:
        p.params.distance3dPercent = s3g::clampf(static_cast<float>(value), 0.0f, 200.0f);
        break;
    default:
        return;
    }
    p.params = sanitizeParams(p.params);
    const s3g::McStereoParams after = effectiveGainParams(p.params);
    if (gainParamsChanged(before, after)) {
        p.gainsDirty = true;
    }
}

double getParamValue(const Plugin& p, clap_id paramId)
{
    switch (paramId) {
    case kParamInputChannels: return static_cast<double>(p.params.inputChannels);
    case kParamWidth: return p.params.widthPercent;
    case kParamRotation: return p.params.rotationDegrees;
    case kParamAutogain: return static_cast<double>(static_cast<uint32_t>(p.params.autogain));
    case kParamOutputGain: return p.params.outputGainDb;
    case kParamLayout: return static_cast<double>(static_cast<uint32_t>(p.params.layout));
    case kParamLayoutWeight: return p.params.layoutWeightPercent;
    case kParamAttenuation3d: return p.params.attenuation3dPercent;
    case kParamDistance3d: return p.params.distance3dPercent;
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

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(p->macRealtimeActivity);
#endif
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->params = sanitizeParams(p->params);
    p->gainsDirty = true;
    p->gainsInitialized = false;
    p->gainRampRemaining = 0;
    p->gainRampTotal = 0;
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
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->gainsDirty = true;
    p->gainsInitialized = false;
    p->gainRampRemaining = 0;
    p->gainRampTotal = 0;
    p->outputPeakLeft.store(0.0f, std::memory_order_relaxed);
    p->outputPeakRight.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) {
        return;
    }

    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            setParamValue(p, param->param_id, param->value);
        }
    }
}

void updateGainsIfNeeded(Plugin& p, uint32_t availableInputChannels)
{
    const uint32_t available = std::min<uint32_t>(availableInputChannels, kInputBusChannels);
    const uint32_t resolved = std::min<uint32_t>(s3g::clampInputChannels(p.params.inputChannels), available);
    if (!p.gainsDirty && p.cachedAvailableInputChannels == available && p.cachedResolvedInputChannels == resolved) {
        return;
    }

    auto resolvedParams = effectiveGainParams(p.params);
    resolvedParams.inputChannels = resolved;
    s3g::makeMcToStereoGains(p.gains.data(), available, resolvedParams);

    if (!p.gainsInitialized) {
        p.currentGains = p.gains;
        p.rampStartGains = p.gains;
        p.gainsInitialized = true;
        p.gainRampRemaining = 0;
        p.gainRampTotal = 0;
    } else {
        p.rampStartGains = p.currentGains;
        const uint32_t rampSamples = static_cast<uint32_t>(std::max(1.0, p.sampleRate * kGainRampMs * 0.001));
        p.gainRampRemaining = rampSamples;
        p.gainRampTotal = rampSamples;
    }

    p.cachedAvailableInputChannels = available;
    p.cachedResolvedInputChannels = resolved;
    p.gainsDirty = false;
}

void advanceGainRamp(Plugin& p, uint32_t available)
{
    if (p.gainRampRemaining == 0 || p.gainRampTotal == 0) {
        return;
    }

    const float wet = 1.0f - static_cast<float>(p.gainRampRemaining) / static_cast<float>(p.gainRampTotal);
    for (uint32_t ch = 0; ch < available; ++ch) {
        p.currentGains[ch].left = p.rampStartGains[ch].left + (p.gains[ch].left - p.rampStartGains[ch].left) * wet;
        p.currentGains[ch].right = p.rampStartGains[ch].right + (p.gains[ch].right - p.rampStartGains[ch].right) * wet;
    }

    --p.gainRampRemaining;
    if (p.gainRampRemaining == 0) {
        p.currentGains = p.gains;
        p.gainRampTotal = 0;
    }
}

clap_process_status processFloat(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t frames)
{
    s3g::clearAudioBuffer(output, frames);
    if (!input.data32 || !output.data32 || output.channel_count < kStereoChannels || !output.data32[0] || !output.data32[1]) {
        return CLAP_PROCESS_CONTINUE;
    }

    const uint32_t available = std::min<uint32_t>(input.channel_count, kInputBusChannels);
    updateGainsIfNeeded(p, available);
    float peakL = 0.0f;
    float peakR = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        advanceGainRamp(p, available);
        float left = 0.0f;
        float right = 0.0f;
        for (uint32_t ch = 0; ch < available; ++ch) {
            if (!input.data32[ch]) {
                continue;
            }
            const float x = input.data32[ch][i];
            left += x * p.currentGains[ch].left;
            right += x * p.currentGains[ch].right;
        }
        output.data32[0][i] = left;
        output.data32[1][i] = right;
        peakL = std::max(peakL, std::abs(left));
        peakR = std::max(peakR, std::abs(right));
    }
    p.outputPeakLeft.store(std::max(p.outputPeakLeft.load(std::memory_order_relaxed) * 0.86f, peakL), std::memory_order_relaxed);
    p.outputPeakRight.store(std::max(p.outputPeakRight.load(std::memory_order_relaxed) * 0.86f, peakR), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status processDouble(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t frames)
{
    s3g::clearAudioBuffer(output, frames);
    if (!input.data64 || !output.data64 || output.channel_count < kStereoChannels || !output.data64[0] || !output.data64[1]) {
        return CLAP_PROCESS_CONTINUE;
    }

    const uint32_t available = std::min<uint32_t>(input.channel_count, kInputBusChannels);
    updateGainsIfNeeded(p, available);
    float peakL = 0.0f;
    float peakR = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        advanceGainRamp(p, available);
        double left = 0.0;
        double right = 0.0;
        for (uint32_t ch = 0; ch < available; ++ch) {
            if (!input.data64[ch]) {
                continue;
            }
            const double x = input.data64[ch][i];
            left += x * static_cast<double>(p.currentGains[ch].left);
            right += x * static_cast<double>(p.currentGains[ch].right);
        }
        output.data64[0][i] = left;
        output.data64[1][i] = right;
        peakL = std::max(peakL, static_cast<float>(std::abs(left)));
        peakR = std::max(peakR, static_cast<float>(std::abs(right)));
    }
    p.outputPeakLeft.store(std::max(p.outputPeakLeft.load(std::memory_order_relaxed) * 0.86f, peakL), std::memory_order_relaxed);
    p.outputPeakRight.store(std::max(p.outputPeakRight.load(std::memory_order_relaxed) * 0.86f, peakR), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);

    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = process->audio_inputs[0];
    const auto& output = process->audio_outputs[0];
    if (input.data32 && output.data32) {
        return processFloat(*p, input, output, process->frames_count);
    }
    if (input.data64 && output.data64) {
        return processDouble(*p, input, output, process->frames_count);
    }
    s3g::clearAudioBuffer(output, process->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) {
        return false;
    }
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "Multichannel In" : "Stereo Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputBusChannels : kStereoChannels;
    info->port_type = isInput ? CLAP_PORT_SURROUND : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return 9; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) {
        return false;
    }
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Downmix", sizeof(info->module));
    switch (index) {
    case 0:
        info->id = kParamInputChannels;
        std::strncpy(info->name, "Input channels", sizeof(info->name));
        info->min_value = 2.0;
        info->max_value = 128.0;
        info->default_value = 8.0;
        return true;
    case 1:
        info->id = kParamWidth;
        std::strncpy(info->name, "Spread/width", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 200.0;
        info->default_value = 100.0;
        return true;
    case 2:
        info->id = kParamRotation;
        std::strncpy(info->name, "Rotation", sizeof(info->name));
        info->min_value = -180.0;
        info->max_value = 180.0;
        info->default_value = 0.0;
        return true;
    case 3:
        info->id = kParamAutogain;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, "Autogain", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 2.0;
        info->default_value = 1.0;
        return true;
    case 4:
        info->id = kParamOutputGain;
        std::strncpy(info->name, "Output gain", sizeof(info->name));
        info->min_value = -24.0;
        info->max_value = 24.0;
        info->default_value = 0.0;
        return true;
    case 5:
        info->id = kParamLayout;
        info->flags |= CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, "Layout", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 7.0;
        info->default_value = 0.0;
        return true;
    case 6:
        info->id = kParamLayoutWeight;
        std::strncpy(info->name, "Layout weighting", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 100.0;
        info->default_value = 100.0;
        return true;
    case 7:
        info->id = kParamAttenuation3d;
        std::strncpy(info->name, "3D attenuation", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 100.0;
        info->default_value = 45.0;
        return true;
    case 8:
        info->id = kParamDistance3d;
        std::strncpy(info->name, "3D distance", sizeof(info->name));
        info->min_value = 0.0;
        info->max_value = 200.0;
        info->default_value = 100.0;
        return true;
    default:
        return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) {
        return false;
    }
    *value = getParamValue(*self(plugin), paramId);
    return paramId >= kParamInputChannels && paramId <= kParamDistance3d;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) {
        return false;
    }
    switch (paramId) {
    case kParamInputChannels:
        std::snprintf(display, size, "%u", s3g::clampInputChannels(roundedUint(value)));
        return true;
    case kParamWidth:
    case kParamLayoutWeight:
    case kParamAttenuation3d:
    case kParamDistance3d:
        std::snprintf(display, size, "%.0f %%", value);
        return true;
    case kParamRotation:
        std::snprintf(display, size, "%.0f deg", value);
        return true;
    case kParamAutogain:
        std::snprintf(display, size, "%s", autogainName(roundedUint(value)));
        return true;
    case kParamOutputGain:
        std::snprintf(display, size, "%+.1f dB", value);
        return true;
    case kParamLayout:
        std::snprintf(display, size, "%s", layoutName(roundedUint(value)));
        return true;
    default:
        return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) {
        return false;
    }
    if (paramId == kParamLayout) {
        for (uint32_t i = 0; i <= 7u; ++i) {
            if (std::strcmp(display, layoutName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
    }
    if (paramId == kParamAutogain) {
        for (uint32_t i = 0; i <= 2u; ++i) {
            if (std::strcmp(display, autogainName(i)) == 0) {
                *value = static_cast<double>(i);
                return true;
            }
        }
    }
    *value = std::atof(display);
    return paramId >= kParamInputChannels && paramId <= kParamDistance3d;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t params {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

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

    std::array<uint8_t, sizeof(SavedState)> bytes {};
    const int64_t bytesRead = stream->read(stream, bytes.data(), bytes.size());
    if (bytesRead == static_cast<int64_t>(sizeof(SavedState))) {
        SavedState state {};
        std::memcpy(&state, bytes.data(), sizeof(state));
        if (state.version != kStateVersion) {
            return false;
        }
        self(plugin)->params = sanitizeParams(state.params);
    } else if (bytesRead == static_cast<int64_t>(sizeof(SavedStateV1))) {
        SavedStateV1 state {};
        std::memcpy(&state, bytes.data(), sizeof(state));
        if (state.version != kLegacyStateVersion) {
            return false;
        }
        s3g::McStereoParams params {};
        params.inputChannels = state.inputChannels;
        params.widthPercent = state.widthPercent;
        params.rotationDegrees = state.rotationDegrees;
        params.autogain = state.autogain;
        params.outputGainDb = state.outputGainDb;
        params.layout = state.layout;
        params.layoutWeightPercent = state.layoutWeightPercent;
        params.attenuation3dPercent = state.attenuation3dPercent;
        params.distance3dPercent = 100.0f;
        self(plugin)->params = sanitizeParams(params);
    } else {
        return false;
    }

    self(plugin)->gainsDirty = true;
    self(plugin)->gainsInitialized = false;
    self(plugin)->gainRampRemaining = 0;
    self(plugin)->gainRampTotal = 0;
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad
};


} // namespace

#if defined(__APPLE__)

static NSColor* s3gMcColor(int rgb, CGFloat alpha = 1.0)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:alpha];
}

@interface S3GMcStereoView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    NSPoint _menuOrigin;
    uint32_t _menuItems;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small;
- (void)updateMenuHover:(NSPoint)point;
@end

@implementation S3GMcStereoView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 920, 560)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuOrigin = NSMakePoint(0, 0);
        _menuItems = 0;
        _refreshTimer = nil;
    }
    return self;
}

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }

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

- (void)startRefreshTimer
{
    if (_refreshTimer) {
        return;
    }
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 24.0)
                                            target:self
                                          selector:@selector(refreshTimerFired:)
                                          userInfo:nil
                                           repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_refreshTimer) {
        [_refreshTimer invalidate];
        _refreshTimer = nil;
    }
}

- (void)refreshTimerFired:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)setParam:(clap_id)param value:(double)value
{
    auto* p = static_cast<Plugin*>(_plugin);
    setParamValue(*p, param, value);
    [self setNeedsDisplay:YES];
}

- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    (void)attrs;
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, small, small, style, 600, 710, 846, 122);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs small:(NSDictionary*)small
{
    (void)attrs;
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawMenu(name, value, y, small, small, style, 600, 710, 160);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    NSColor* bg = style.bg;
    NSColor* grid = style.grid;
    NSColor* dim = style.dim;
    NSColor* text = style.text;
    NSColor* fill = style.fill;
    [style.bg setFill];
    NSRectFill([self bounds]);

    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSFont* bold = [NSFont fontWithName:@"Menlo-Bold" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightBold];
    NSDictionary* small = @{ NSForegroundColorAttributeName: dim, NSFontAttributeName: mono };
    NSDictionary* label = @{ NSForegroundColorAttributeName: text, NSFontAttributeName: bold };
    NSDictionary* title = @{ NSForegroundColorAttributeName: text, NSFontAttributeName: mono };

    [@"s3g MC TO STEREO AUTOGAIN" drawAtPoint:NSMakePoint(18, 13) withAttributes:title];
    [@"128IN / 2OUT" drawAtPoint:NSMakePoint(824, 13) withAttributes:small];

    NSRect mapPanel = NSMakeRect(12, 34, 564, 514);
    s3g::clap_gui::drawPanelFrame(mapPanel.origin.x, mapPanel.origin.y, mapPanel.size.width, mapPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"DOWNMIX MAP", true, mapPanel.origin.x, mapPanel.origin.y, mapPanel.size.width, 21, label, style);

    const uint32_t count = s3g::clampInputChannels(p->params.inputChannels);
    const uint32_t layout = static_cast<uint32_t>(p->params.layout);
    NSString* info = [NSString stringWithFormat:@"%u IN / %@ / W %.0f%% / D %.0f%% / ROT %.0f / %@",
                      count,
                      [NSString stringWithUTF8String:layoutName(layout)],
                      static_cast<double>(p->params.widthPercent),
                      static_cast<double>(p->params.distance3dPercent),
                      static_cast<double>(p->params.rotationDegrees),
                      [NSString stringWithUTF8String:autogainName(static_cast<uint32_t>(p->params.autogain))]];
    [info drawAtPoint:NSMakePoint(150, 39) withAttributes:small];

    NSRect field = NSMakeRect(28, 68, 532, 404);
    [s3gMcColor(0x101010) setFill];
    NSRectFill(field);
    [grid setStroke];
    NSFrameRect(field);

    const CGFloat leftX = field.origin.x + 54;
    const CGFloat rightX = field.origin.x + field.size.width - 54;
    const CGFloat speakerY = field.origin.y + field.size.height - 46;
    const CGFloat mapLeft = field.origin.x + 74;
    const CGFloat mapW = field.size.width - 148;
    const CGFloat centerX = field.origin.x + field.size.width * 0.5;
    const CGFloat mapY = field.origin.y + 120;
    const bool projection = layout >= 5 && layout <= 7;

    [s3gMcColor(0x2e2e2e) setFill];
    NSRectFill(NSMakeRect(leftX - 15, speakerY - 15, 30, 30));
    NSRectFill(NSMakeRect(rightX - 15, speakerY - 15, 30, 30));
    [@"L" drawAtPoint:NSMakePoint(leftX - 4, speakerY - 7) withAttributes:label];
    [@"R" drawAtPoint:NSMakePoint(rightX - 4, speakerY - 7) withAttributes:label];

    [s3gMcColor(0x747474, 0.25) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(mapLeft, mapY) toPoint:NSMakePoint(mapLeft + mapW, mapY)];
    [s3gMcColor(0x747474, 0.18) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(centerX, mapY - 28) toPoint:NSMakePoint(centerX, mapY + 28)];
    if (projection) {
        [s3gMcColor(0x747474, 0.12) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(mapLeft, mapY - 62) toPoint:NSMakePoint(mapLeft + mapW, mapY - 62)];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(mapLeft, mapY + 62) toPoint:NSMakePoint(mapLeft + mapW, mapY + 62)];
        [@"HIGH" drawAtPoint:NSMakePoint(mapLeft - 42, mapY - 69) withAttributes:small];
        [@"MID" drawAtPoint:NSMakePoint(mapLeft - 35, mapY - 7) withAttributes:small];
        [@"LOW" drawAtPoint:NSMakePoint(mapLeft - 35, mapY + 55) withAttributes:small];
    }

    auto positionInfo = [&](uint32_t index) -> s3g::McStereoPosition {
        if (p->params.layout == s3g::McStereoLayout::SphereProjection) return s3g::spherePosition(index, count);
        if (p->params.layout == s3g::McStereoLayout::HemisphereProjection) return s3g::hemispherePosition(index, count);
        if (p->params.layout == s3g::McStereoLayout::CubeProjection) return s3g::cubePosition(index);
        return s3g::ringPosition(index, count, 0.0f, 0.0f);
    };
    auto shouldLabel = [&](uint32_t i) -> bool {
        const uint32_t interval = count <= 16 ? 1u : count <= 32 ? 4u : count <= 64 ? 8u : 16u;
        return i == 0 || i + 1 == count || (i % interval) == 0;
    };

    const float baseGain = s3g::autogainForMode(p->params.autogain, count) * s3g::mcStereoDbToGain(p->params.outputGainDb);
    for (uint32_t i = 0; i < count; ++i) {
        const auto pg = s3g::panGainForChannel(i, count, p->params);
        s3g::McStereoParams refParams = p->params;
        refParams.layoutWeightPercent = 100.0f;
        refParams.attenuation3dPercent = 0.0f;
        refParams.distance3dPercent = 100.0f;
        const auto refPg = s3g::panGainForChannel(i, count, refParams);
        const CGFloat srcX = mapLeft + ((pg.pan + 1.0f) * 0.5f) * mapW;
        const CGFloat refX = mapLeft + ((refPg.pan + 1.0f) * 0.5f) * mapW;
        CGFloat srcY = mapY - 24 + static_cast<CGFloat>(i % 3u) * 18.0;
        if (projection) {
            const auto pos = positionInfo(i);
            const double azr = (pos.azimuthDegrees + p->params.rotationDegrees) * M_PI / 180.0;
            const double frontness = (std::cos(azr) + 1.0) * 0.5;
            const CGFloat distanceScale = static_cast<CGFloat>(s3g::clampf(p->params.distance3dPercent / 100.0f, 0.0f, 2.0f));
            const CGFloat baseY = -static_cast<CGFloat>(pos.elevationDegrees / 90.0f) * 62.0f;
            const CGFloat rearDrop = static_cast<CGFloat>((1.0 - frontness) * 16.0);
            srcY = mapY + (baseY + rearDrop) * distanceScale;
        }
        const float theta = (pg.pan + 1.0f) * static_cast<float>(M_PI) / 4.0f;
        const float leftGain = std::cos(theta);
        const float rightGain = std::sin(theta);
        const CGFloat dot = count <= 16 ? 5.0 : count <= 32 ? 4.0 : 3.1;
        const CGFloat visibleGain = std::clamp(static_cast<CGFloat>(pg.gain), static_cast<CGFloat>(0.15), static_cast<CGFloat>(1.0));
        [s3gMcColor(0xd8d8d8, 0.18) setStroke];
        NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(refX - dot - 2, srcY - dot - 2, (dot + 2) * 2, (dot + 2) * 2)];
        [halo stroke];
        if (std::abs(refX - srcX) > 2.0) {
            [NSBezierPath strokeLineFromPoint:NSMakePoint(refX, srcY) toPoint:NSMakePoint(srcX, srcY)];
        }
        [s3gMcColor(0xbdbdbd, 0.14 + 0.34 * leftGain * visibleGain) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(srcX, srcY + 5) toPoint:NSMakePoint(leftX, speakerY - 16)];
        [s3gMcColor(0xe4e4e4, 0.12 + 0.34 * rightGain * visibleGain) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(srcX, srcY + 5) toPoint:NSMakePoint(rightX, speakerY - 16)];
        [fill setFill];
        NSRectFill(NSMakeRect(srcX - dot * visibleGain, srcY - dot * visibleGain, dot * 2 * visibleGain, dot * 2 * visibleGain));
        if (shouldLabel(i)) {
            [[NSString stringWithFormat:@"%u", i + 1] drawAtPoint:NSMakePoint(srcX + 6, srcY - 8) withAttributes:small];
        }
    }
    [@"L" drawAtPoint:NSMakePoint(mapLeft - 7, mapY + 39) withAttributes:small];
    [@"C" drawAtPoint:NSMakePoint(centerX - 4, mapY + 39) withAttributes:small];
    [@"R" drawAtPoint:NSMakePoint(mapLeft + mapW - 7, mapY + 39) withAttributes:small];

    const CGFloat railY = field.origin.y + field.size.height - 92;
    const CGFloat weightNorm = std::clamp<CGFloat>(p->params.layoutWeightPercent / 100.0f, 0, 1);
    const CGFloat attenNorm = std::clamp<CGFloat>(p->params.attenuation3dPercent / 100.0f, 0, 1);
    [@"WEIGHT" drawAtPoint:NSMakePoint(mapLeft, railY - 22) withAttributes:small];
    [grid setStroke]; NSFrameRect(NSMakeRect(mapLeft, railY - 6, mapW * 0.44, 7));
    [fill setFill]; NSRectFill(NSMakeRect(mapLeft + 1, railY - 5, (mapW * 0.44 - 2) * weightNorm, 5));
    [@"3D ATT" drawAtPoint:NSMakePoint(mapLeft + mapW * 0.53, railY - 22) withAttributes:small];
    [grid setStroke]; NSFrameRect(NSMakeRect(mapLeft + mapW * 0.53, railY - 6, mapW * 0.44, 7));
    [fill setFill]; NSRectFill(NSMakeRect(mapLeft + mapW * 0.53 + 1, railY - 5, (mapW * 0.44 - 2) * attenNorm, 5));
    const CGFloat distNorm = std::clamp<CGFloat>(p->params.distance3dPercent / 200.0f, 0, 1);
    [@"3D DIST" drawAtPoint:NSMakePoint(mapLeft, railY + 11) withAttributes:small];
    [grid setStroke]; NSFrameRect(NSMakeRect(mapLeft + 62, railY + 15, mapW * 0.32, 7));
    [fill setFill]; NSRectFill(NSMakeRect(mapLeft + 63, railY + 16, (mapW * 0.32 - 2) * distNorm, 5));

    NSString* routeNote = [NSString stringWithFormat:@"ACTIVE %u / BASE GAIN %.3f / ZERO UNMAPPED OUTPUTS IN REAPER", count, baseGain];
    [routeNote drawAtPoint:NSMakePoint(28, 488) withAttributes:small];

    NSRect side = NSMakeRect(592, 34, 316, 514);
    s3g::clap_gui::drawPanelFrame(side.origin.x, side.origin.y, side.size.width, side.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"AUDITION", true, side.origin.x, side.origin.y, side.size.width, 21, label, style);

    [self drawSlider:@"IN" value:[NSString stringWithFormat:@"%u", count] norm:(count - 2.0) / 126.0 y:74 attrs:label small:small];
    [self drawSlider:@"WDTH" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.widthPercent)] norm:p->params.widthPercent / 200.0 y:96 attrs:label small:small];
    [self drawSlider:@"ROT" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.rotationDegrees)] norm:(p->params.rotationDegrees + 180.0) / 360.0 y:118 attrs:label small:small];
    [self drawMenu:@"LAY" value:[NSString stringWithUTF8String:layoutName(layout)] y:140 attrs:label small:small];
    [self drawSlider:@"WGT" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.layoutWeightPercent)] norm:p->params.layoutWeightPercent / 100.0 y:162 attrs:label small:small];
    [self drawSlider:@"ATT" value:(projection ? [NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.attenuation3dPercent)] : @"OFF") norm:(projection ? p->params.attenuation3dPercent / 100.0 : 0.0) y:184 attrs:label small:small];
    [self drawSlider:@"DST" value:(projection ? [NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.distance3dPercent)] : @"OFF") norm:(projection ? p->params.distance3dPercent / 200.0 : 0.5) y:206 attrs:label small:small];
    [self drawMenu:@"AGN" value:[NSString stringWithUTF8String:autogainName(static_cast<uint32_t>(p->params.autogain))] y:228 attrs:label small:small];
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(p->params.outputGainDb)] norm:(p->params.outputGainDb + 24.0) / 48.0 y:250 attrs:label small:small];

    NSRect meterPanel = NSMakeRect(604, 288, 292, 106);
    [s3gMcColor(0x111111) setFill]; NSRectFill(meterPanel);
    [grid setStroke]; NSFrameRect(meterPanel);
    [@"STEREO OUT" drawAtPoint:NSMakePoint(616, 294) withAttributes:label];
    const float pkL = p->outputPeakLeft.exchange(p->outputPeakLeft.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    const float pkR = p->outputPeakRight.exchange(p->outputPeakRight.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    auto drawMeter = [&](CGFloat y, NSString* name, float peak) {
        const CGFloat x = 638.0;
        const CGFloat w = 190.0;
        const CGFloat h = 18.0;
        const double db = 20.0 * std::log10(std::max(0.000001f, peak));
        const CGFloat norm = std::clamp<CGFloat>((db + 60.0) / 60.0, 0.0, 1.0);
        NSRect r = NSMakeRect(x, y, w, h);
        [bg setFill]; NSRectFill(r);
        [fill setFill]; NSRectFill(NSMakeRect(r.origin.x + 2, r.origin.y + 2, (r.size.width - 4) * norm, r.size.height - 4));
        [grid setStroke]; NSFrameRect(r);
        [s3gMcColor(0xd0d0d0, 0.55) setStroke];
        const CGFloat minus12 = r.origin.x + w * ((-12.0 + 60.0) / 60.0);
        [NSBezierPath strokeLineFromPoint:NSMakePoint(minus12, r.origin.y - 3) toPoint:NSMakePoint(minus12, r.origin.y + r.size.height + 3)];
        [name drawAtPoint:NSMakePoint(616, y + 2) withAttributes:small];
        [[NSString stringWithFormat:@"%+4.1f", db] drawAtPoint:NSMakePoint(838, y + 2) withAttributes:small];
    };
    drawMeter(342, @"L", pkL);
    drawMeter(316, @"R", pkR);

    [@"PIN NOTE" drawAtPoint:NSMakePoint(616, 508) withAttributes:label];
    [@"REAPER: enable zero unmapped outputs" drawAtPoint:NSMakePoint(684, 508) withAttributes:small];

    if (_openMenu > 0 && _menuItems > 0) {
        const CGFloat itemH = 18;
        NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 160, itemH * _menuItems);
        NSString* layoutItems[] = {
            [NSString stringWithUTF8String:layoutName(0)],
            [NSString stringWithUTF8String:layoutName(1)],
            [NSString stringWithUTF8String:layoutName(2)],
            [NSString stringWithUTF8String:layoutName(3)],
            [NSString stringWithUTF8String:layoutName(4)],
            [NSString stringWithUTF8String:layoutName(5)],
            [NSString stringWithUTF8String:layoutName(6)],
            [NSString stringWithUTF8String:layoutName(7)],
        };
        NSString* autogainItems[] = {
            [NSString stringWithUTF8String:autogainName(0)],
            [NSString stringWithUTF8String:autogainName(1)],
            [NSString stringWithUTF8String:autogainName(2)],
        };
        if (_openMenu == 1) {
            s3g::clap_gui::drawDropdownMenu(menu, itemH, layoutItems, _menuItems, static_cast<int>(layout), _hoverMenuItem, small, style);
        } else {
            s3g::clap_gui::drawDropdownMenu(menu, itemH, autogainItems, _menuItems, static_cast<int>(p->params.autogain), _hoverMenuItem, small, style);
        }
    }
}

- (void)updateMenuHover:(NSPoint)point
{
    if (_openMenu <= 0 || _menuItems == 0) return;
    const CGFloat itemH = 18;
    const NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 160, itemH * _menuItems);
    const int next = s3g::clap_gui::dropdownHitIndex(point, menu, itemH, _menuItems);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}

- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp((pt.x - 710.0) / 122.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 0: [self setParam:kParamInputChannels value:2.0 + norm * 126.0]; break;
    case 1: [self setParam:kParamWidth value:norm * 200.0]; break;
    case 2: [self setParam:kParamRotation value:-180.0 + norm * 360.0]; break;
    case 4: [self setParam:kParamLayoutWeight value:norm * 100.0]; break;
    case 5:
        if (isProjectionLayout(static_cast<Plugin*>(_plugin)->params.layout)) {
            [self setParam:kParamAttenuation3d value:norm * 100.0];
        }
        break;
    case 6:
        if (isProjectionLayout(static_cast<Plugin*>(_plugin)->params.layout)) {
            [self setParam:kParamDistance3d value:norm * 200.0];
        }
        break;
    case 8: [self setParam:kParamOutputGain value:-24.0 + norm * 48.0]; break;
    default: break;
    }
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu > 0) {
        const CGFloat itemH = 18;
        NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 160, itemH * _menuItems);
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, menu, itemH, _menuItems);
        if (hit >= 0) {
            const uint32_t i = static_cast<uint32_t>(hit);
            if (_openMenu == 1) {
                [self setParam:kParamLayout value:i];
            } else {
                [self setParam:kParamAutogain value:i];
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItems = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat rows[] = { 74, 96, 118, 140, 162, 184, 206, 228, 250 };
    for (int i = 0; i < 9; ++i) {
        NSRect r = NSMakeRect(596, rows[i] - 6, 296, 22);
        if (NSPointInRect(pt, r)) {
            if (i == 3) {
                _openMenu = 1;
                _menuItems = 8;
                _menuOrigin = NSMakePoint(710, rows[i] + 17);
                _hoverMenuItem = -1;
                [self setNeedsDisplay:YES];
                return;
            }
            if (i == 7) {
                _openMenu = 2;
                _menuItems = 3;
                _menuOrigin = NSMakePoint(710, rows[i] + 17);
                _hoverMenuItem = -1;
                [self setNeedsDisplay:YES];
                return;
            }
            if ((i == 5 || i == 6) && !isProjectionLayout(p->params.layout)) {
                return;
            }
            _dragSlider = i;
            [self updateSliderAtPoint:pt];
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
    [self updateSliderAtPoint:pt];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragSlider = -1;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) {
        return false;
    }
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) {
        return false;
    }
    auto* p = self(plugin);
    if (p->guiView) {
        return true;
    }
    p->guiView = [[S3GMcStereoView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiView) {
        p->guiVisible.store(false, std::memory_order_relaxed);
        NSView* view = static_cast<NSView*>(p->guiView);
        if ([view respondsToSelector:@selector(stopRefreshTimer)]) {
            [static_cast<S3GMcStereoView*>(view) stopRefreshTimer];
        }
        [view removeFromSuperview];
        [view release];
        p->guiView = nullptr;
    }
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) {
        return false;
    }
    *width = 920;
    *height = 560;
    return true;
}

bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }

bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    NSView* view = static_cast<NSView*>(p->guiView);
    [view setFrameSize:NSMakeSize(width, height)];
    return true;
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, 920, 560)];
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    p->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<NSView*>(p->guiView) setHidden:NO];
    if ([static_cast<NSView*>(p->guiView) respondsToSelector:@selector(startRefreshTimer)]) {
        [static_cast<S3GMcStereoView*>(p->guiView) startRefreshTimer];
    }
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) {
        return false;
    }
    p->guiVisible.store(false, std::memory_order_relaxed);
    if ([static_cast<NSView*>(p->guiView) respondsToSelector:@selector(stopRefreshTimer)]) {
        [static_cast<S3GMcStereoView*>(p->guiView) stopRefreshTimer];
    }
    [static_cast<NSView*>(p->guiView) setHidden:YES];
    return true;
}

const clap_plugin_gui_t gui {
    guiIsApiSupported,
    guiGetPreferredApi,
    guiCreate,
    guiDestroy,
    guiSetScale,
    guiGetSize,
    guiCanResize,
    guiGetResizeHints,
    guiAdjustSize,
    guiSetSize,
    guiSetParent,
    guiSetTransient,
    guiSuggestTitle,
    guiShow,
    guiHide
};

} // namespace

#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &audioPorts;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &params;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &state;
    }
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
        return &gui;
    }
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.mc-to-stereo-autogain",
    "s3g MC to Stereo Autogain",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Multichannel input to true stereo CLAP fold-down with autogain.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) {
        return nullptr;
    }
    auto* p = new (std::nothrow) Plugin();
    if (!p) {
        return nullptr;
    }
    p->host = host;
    p->params = sanitizeParams(p->params);
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

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
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
    if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &factory;
    }
    return nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
