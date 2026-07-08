#include "s3g_ambisonic_stereo_decoder.h"

#include <clap/clap.h>
#include <clap/ext/ambisonic.h>
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

constexpr uint32_t kStateVersion = 1;
constexpr uint32_t kInputChannels = s3g::kAmbiStereoDecoderMaxChannels;
constexpr uint32_t kStereoChannels = 2;
constexpr double kCoeffRampMs = 36.0;
constexpr uint32_t kGuiWidth = 940;
constexpr uint32_t kGuiHeight = 650;

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamLayout = 2,
    kParamMethod = 3,
    kParamWidth = 4,
    kParamAngle = 5,
    kParamRotation = 6,
    kParamDirectivity = 7,
    kParamRearReject = 8,
    kParamHeightFold = 9,
    kParamDiffuse = 10,
    kParamWeighting = 11,
    kParamAutogain = 12,
    kParamOutputGain = 13,
    kParamAbSpacing = 14,
    kParamBassMono = 15,
    kParamMicElevation = 16,
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiStereoParams params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    s3g::AmbiStereoParams params {};
    s3g::AmbiStereoDecoder decoder {};
    s3g::AmbiStereoDecoder targetDecoder {};
    std::array<float, kInputChannels> frameIn {};
    std::array<float, kInputChannels> leftCoeffs {};
    std::array<float, kInputChannels> rightCoeffs {};
    std::array<float, kInputChannels> startLeftCoeffs {};
    std::array<float, kInputChannels> startRightCoeffs {};
    std::array<float, kInputChannels> targetLeftCoeffs {};
    std::array<float, kInputChannels> targetRightCoeffs {};
    std::array<float, 4096> abLeft {};
    std::array<float, 4096> abRight {};
    uint32_t abPos = 0;
    float bassLeft = 0.0f;
    float bassRight = 0.0f;
    uint32_t coeffRampRemaining = 0;
    uint32_t coeffRampTotal = 0;
    bool coeffsInitialized = false;
    bool coeffsDirty = true;
    std::atomic<float> outputPeakLeft { 0.0f };
    std::atomic<float> outputPeakRight { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

s3g::AmbiStereoParams sanitizeParams(s3g::AmbiStereoParams params)
{
    params.order = std::clamp<uint32_t>(params.order, 1u, s3g::kAmbiStereoDecoderMaxOrder);
    params.layout = static_cast<s3g::AmbiStereoVirtualLayout>(std::min<uint32_t>(static_cast<uint32_t>(params.layout), 4u));
    params.method = static_cast<s3g::AmbiStereoMethod>(std::min<uint32_t>(static_cast<uint32_t>(params.method), 9u));
    params.stereoWidthPercent = s3g::clamp(params.stereoWidthPercent, 0.0f, 200.0f);
    params.micAngleDeg = s3g::clamp(params.micAngleDeg, 20.0f, 140.0f);
    params.rotationDeg = s3g::clamp(params.rotationDeg, -180.0f, 180.0f);
    params.directivityPercent = s3g::clamp(params.directivityPercent, 0.0f, 100.0f);
    params.rearRejectPercent = s3g::clamp(params.rearRejectPercent, 0.0f, 100.0f);
    params.heightFoldPercent = s3g::clamp(params.heightFoldPercent, 0.0f, 100.0f);
    params.diffuseBlendPercent = s3g::clamp(params.diffuseBlendPercent, 0.0f, 100.0f);
    params.weighting = static_cast<s3g::AmbiStereoWeighting>(std::min<uint32_t>(static_cast<uint32_t>(params.weighting), 3u));
    params.autogain = static_cast<s3g::AmbiStereoAutogain>(std::min<uint32_t>(static_cast<uint32_t>(params.autogain), 2u));
    params.outputGainDb = s3g::clamp(params.outputGainDb, -24.0f, 24.0f);
    params.abSpacingCm = s3g::clamp(params.abSpacingCm, 0.0f, 120.0f);
    params.bassMonoHz = s3g::clamp(params.bassMonoHz, 0.0f, 300.0f);
    params.micElevationDeg = s3g::clamp(params.micElevationDeg, -90.0f, 90.0f);
    return params;
}

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

const char* layoutName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 5u)) {
    case 0: return "Quad virtual";
    case 1: return "8ch cube";
    case 2: return "12ch dodeca";
    case 3: return "24ch dome";
    case 4:
    default: return "32ch sphere";
    }
}

const char* methodName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 9u)) {
    case 0: return "XY cardioid";
    case 1: return "ORTF-style";
    case 2: return "MS cardioid";
    case 3: return "Blumlein";
    case 4: return "Spaced omni";
    case 5: return "Dual shotgun";
    case 6: return "Wide cardioid";
    case 7: return "Supercardioid XY";
    case 8: return "Hypercardioid XY";
    case 9:
    default: return "Height focus";
    }
}

const char* weightingName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 3u)) {
    case 0: return "Projection";
    case 1: return "Energy-normalized";
    case 2: return "Max-rE";
    case 3:
    default: return "Custom";
    }
}

const char* autogainName(uint32_t value)
{
    switch (std::min<uint32_t>(value, 2u)) {
    case 0: return "Off";
    case 1: return "Power/sqrt(N)";
    case 2:
    default: return "Energy sum";
    }
}

float outputScale(const s3g::AmbiStereoParams& params)
{
    const uint32_t channels = s3g::ambiChannelsForOrder(params.order);
    float ag = 1.0f;
    if (params.autogain == s3g::AmbiStereoAutogain::PowerSqrtN) {
        ag = 1.0f / std::sqrt(static_cast<float>(channels));
    } else if (params.autogain == s3g::AmbiStereoAutogain::EnergySum) {
        ag = 1.0f / static_cast<float>(std::max(1u, channels));
    }
    return ag * s3g::dbToGain(params.outputGainDb);
}

void markCoeffsDirty(Plugin& p)
{
    p.coeffsDirty = true;
}

void setParamValue(Plugin& p, clap_id paramId, double value)
{
    switch (paramId) {
    case kParamOrder: p.params.order = std::clamp<uint32_t>(roundedUint(value), 1u, s3g::kAmbiStereoDecoderMaxOrder); break;
    case kParamLayout: p.params.layout = static_cast<s3g::AmbiStereoVirtualLayout>(std::min<uint32_t>(roundedUint(value), 4u)); break;
    case kParamMethod: p.params.method = static_cast<s3g::AmbiStereoMethod>(std::min<uint32_t>(roundedUint(value), 9u)); break;
    case kParamWidth: p.params.stereoWidthPercent = static_cast<float>(value); break;
    case kParamAngle: p.params.micAngleDeg = static_cast<float>(value); break;
    case kParamRotation: p.params.rotationDeg = static_cast<float>(value); break;
    case kParamDirectivity: p.params.directivityPercent = static_cast<float>(value); break;
    case kParamRearReject: p.params.rearRejectPercent = static_cast<float>(value); break;
    case kParamHeightFold: p.params.heightFoldPercent = static_cast<float>(value); break;
    case kParamDiffuse: p.params.diffuseBlendPercent = static_cast<float>(value); break;
    case kParamWeighting: p.params.weighting = static_cast<s3g::AmbiStereoWeighting>(std::min<uint32_t>(roundedUint(value), 3u)); break;
    case kParamAutogain: p.params.autogain = static_cast<s3g::AmbiStereoAutogain>(std::min<uint32_t>(roundedUint(value), 2u)); break;
    case kParamOutputGain: p.params.outputGainDb = static_cast<float>(value); break;
    case kParamAbSpacing: p.params.abSpacingCm = static_cast<float>(value); break;
    case kParamBassMono: p.params.bassMonoHz = static_cast<float>(value); break;
    case kParamMicElevation: p.params.micElevationDeg = static_cast<float>(value); break;
    default: return;
    }
    p.params = sanitizeParams(p.params);
    markCoeffsDirty(p);
}

double getParamValue(const Plugin& p, clap_id paramId)
{
    switch (paramId) {
    case kParamOrder: return static_cast<double>(p.params.order);
    case kParamLayout: return static_cast<double>(static_cast<uint32_t>(p.params.layout));
    case kParamMethod: return static_cast<double>(static_cast<uint32_t>(p.params.method));
    case kParamWidth: return p.params.stereoWidthPercent;
    case kParamAngle: return p.params.micAngleDeg;
    case kParamRotation: return p.params.rotationDeg;
    case kParamDirectivity: return p.params.directivityPercent;
    case kParamRearReject: return p.params.rearRejectPercent;
    case kParamHeightFold: return p.params.heightFoldPercent;
    case kParamDiffuse: return p.params.diffuseBlendPercent;
    case kParamWeighting: return static_cast<double>(static_cast<uint32_t>(p.params.weighting));
    case kParamAutogain: return static_cast<double>(static_cast<uint32_t>(p.params.autogain));
    case kParamOutputGain: return p.params.outputGainDb;
    case kParamAbSpacing: return p.params.abSpacingCm;
    case kParamBassMono: return p.params.bassMonoHz;
    case kParamMicElevation: return p.params.micElevationDeg;
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
    p->decoder.prepare(sampleRate);
    p->targetDecoder.prepare(sampleRate);
    p->decoder.setParams(p->params);
    p->targetDecoder.setParams(p->params);
    p->leftCoeffs = p->decoder.leftCoeffs();
    p->rightCoeffs = p->decoder.rightCoeffs();
    p->targetLeftCoeffs = p->leftCoeffs;
    p->targetRightCoeffs = p->rightCoeffs;
    p->coeffsDirty = false;
    p->coeffsInitialized = true;
    p->coeffRampRemaining = 0;
    p->coeffRampTotal = 0;
    p->abLeft.fill(0.0f);
    p->abRight.fill(0.0f);
    p->abPos = 0;
    p->bassLeft = 0.0f;
    p->bassRight = 0.0f;
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
    p->decoder.reset();
    p->targetDecoder.reset();
    p->coeffsDirty = true;
    p->coeffsInitialized = false;
    p->coeffRampRemaining = 0;
    p->coeffRampTotal = 0;
    p->abLeft.fill(0.0f);
    p->abRight.fill(0.0f);
    p->abPos = 0;
    p->bassLeft = 0.0f;
    p->bassRight = 0.0f;
    p->outputPeakLeft.store(0.0f, std::memory_order_relaxed);
    p->outputPeakRight.store(0.0f, std::memory_order_relaxed);
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

void updateCoeffsIfNeeded(Plugin& p)
{
    if (!p.coeffsDirty && p.coeffsInitialized) return;
    p.targetDecoder.setParams(p.params);
    p.targetLeftCoeffs = p.targetDecoder.leftCoeffs();
    p.targetRightCoeffs = p.targetDecoder.rightCoeffs();
    if (!p.coeffsInitialized) {
        p.leftCoeffs = p.targetLeftCoeffs;
        p.rightCoeffs = p.targetRightCoeffs;
        p.coeffsInitialized = true;
        p.coeffRampRemaining = 0;
        p.coeffRampTotal = 0;
    } else {
        p.startLeftCoeffs = p.leftCoeffs;
        p.startRightCoeffs = p.rightCoeffs;
        const uint32_t rampSamples = static_cast<uint32_t>(std::max(1.0, p.sampleRate * kCoeffRampMs * 0.001));
        p.coeffRampRemaining = rampSamples;
        p.coeffRampTotal = rampSamples;
    }
    p.decoder.setParams(p.params);
    p.coeffsDirty = false;
}

void advanceCoeffRamp(Plugin& p)
{
    if (p.coeffRampRemaining == 0 || p.coeffRampTotal == 0) return;
    const float wet = 1.0f - static_cast<float>(p.coeffRampRemaining) / static_cast<float>(p.coeffRampTotal);
    const uint32_t channels = s3g::ambiChannelsForOrder(p.params.order);
    for (uint32_t ch = 0; ch < channels; ++ch) {
        p.leftCoeffs[ch] = s3g::lerp(p.startLeftCoeffs[ch], p.targetLeftCoeffs[ch], wet);
        p.rightCoeffs[ch] = s3g::lerp(p.startRightCoeffs[ch], p.targetRightCoeffs[ch], wet);
    }
    --p.coeffRampRemaining;
    if (p.coeffRampRemaining == 0) {
        p.leftCoeffs = p.targetLeftCoeffs;
        p.rightCoeffs = p.targetRightCoeffs;
        p.coeffRampTotal = 0;
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t frames, Sample** in, Sample** out)
{
    s3g::clearAudioBuffer(output, frames);
    if (!in || !out || output.channel_count < kStereoChannels || !out[0] || !out[1]) {
        return CLAP_PROCESS_CONTINUE;
    }
    updateCoeffsIfNeeded(p);
    const uint32_t available = std::min<uint32_t>(input.channel_count, kInputChannels);
    const uint32_t channels = std::min<uint32_t>(s3g::ambiChannelsForOrder(p.params.order), available);
    const float outScale = outputScale(p.params);
    const float abDelaySamples = std::min<float>(4094.0f, p.params.abSpacingCm * 0.01f / 343.0f * static_cast<float>(p.sampleRate));
    const float bassA = p.params.bassMonoHz > 0.0f ? std::exp(-2.0f * s3g::kPi * p.params.bassMonoHz / static_cast<float>(p.sampleRate)) : 0.0f;
    float peakL = 0.0f;
    float peakR = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        advanceCoeffRamp(p);
        double left = 0.0;
        double right = 0.0;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const double x = in[ch] ? static_cast<double>(in[ch][i]) : 0.0;
            left += x * static_cast<double>(p.leftCoeffs[ch]);
            right += x * static_cast<double>(p.rightCoeffs[ch]);
        }
        if (p.params.method == s3g::AmbiStereoMethod::SpacedOmni && abDelaySamples > 0.0f) {
            const uint32_t delayA = static_cast<uint32_t>(std::floor(abDelaySamples));
            const uint32_t readA = (p.abPos + 4096u - delayA) % 4096u;
            const uint32_t readB = (readA + 4095u) % 4096u;
            const float frac = abDelaySamples - std::floor(abDelaySamples);
            const double delayedL = p.abLeft[readA] * (1.0f - frac) + p.abLeft[readB] * frac;
            const double delayedR = p.abRight[readA] * (1.0f - frac) + p.abRight[readB] * frac;
            p.abLeft[p.abPos] = static_cast<float>(left);
            p.abRight[p.abPos] = static_cast<float>(right);
            p.abPos = (p.abPos + 1u) % 4096u;
            const double mid = (left + right) * 0.5;
            double side = (left - right) * 0.5;
            const double delayedSide = (delayedL - delayedR) * 0.5;
            const double sideMix = std::min(0.70, static_cast<double>(p.params.abSpacingCm) / 120.0 * 0.70);
            side = side * (1.0 - sideMix) + delayedSide * sideMix;
            left = mid + side;
            right = mid - side;
        } else {
            p.abLeft[p.abPos] = static_cast<float>(left);
            p.abRight[p.abPos] = static_cast<float>(right);
            p.abPos = (p.abPos + 1u) % 4096u;
        }
        if (p.params.bassMonoHz > 0.0f) {
            p.bassLeft = p.bassLeft * bassA + static_cast<float>(left) * (1.0f - bassA);
            p.bassRight = p.bassRight * bassA + static_cast<float>(right) * (1.0f - bassA);
            const double mono = (p.bassLeft + p.bassRight) * 0.5;
            left = left - p.bassLeft + mono;
            right = right - p.bassRight + mono;
        }
        left = std::clamp(left * outScale, -4.0, 4.0);
        right = std::clamp(right * outScale, -4.0, 4.0);
        out[0][i] = static_cast<Sample>(left);
        out[1][i] = static_cast<Sample>(right);
        peakL = std::max(peakL, static_cast<float>(std::abs(left)));
        peakR = std::max(peakR, static_cast<float>(std::abs(right)));
    }
    p.outputPeakLeft.store(std::max(p.outputPeakLeft.load(std::memory_order_relaxed) * 0.86f, peakL), std::memory_order_relaxed);
    p.outputPeakRight.store(std::max(p.outputPeakRight.load(std::memory_order_relaxed) * 0.86f, peakR), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    if (input.data32 && output.data32) return processTyped<float>(*p, input, output, proc->frames_count, input.data32, output.data32);
    if (input.data64 && output.data64) return processTyped<double>(*p, input, output, proc->frames_count, input.data64, output.data64);
    s3g::clearAudioBuffer(output, proc->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "Ambisonic In" : "Stereo Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kStereoChannels;
    info->port_type = isInput ? CLAP_PORT_AMBISONIC : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 16; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->module, "Decoder", sizeof(info->module));
    switch (index) {
    case 0: info->id = kParamOrder; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Ambisonic order", sizeof(info->name)); info->min_value = 1; info->max_value = 7; info->default_value = 3; return true;
    case 1: info->id = kParamLayout; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Virtual speaker field", sizeof(info->name)); info->min_value = 0; info->max_value = 4; info->default_value = 3; return true;
    case 2: info->id = kParamMethod; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Stereo method", sizeof(info->name)); info->min_value = 0; info->max_value = 9; info->default_value = 0; return true;
    case 3: info->id = kParamWidth; std::strncpy(info->name, "Stereo width", sizeof(info->name)); info->min_value = 0; info->max_value = 200; info->default_value = 110; return true;
    case 4: info->id = kParamAngle; std::strncpy(info->name, "Mic angle", sizeof(info->name)); info->min_value = 20; info->max_value = 140; info->default_value = 90; return true;
    case 5: info->id = kParamRotation; std::strncpy(info->name, "Listening rotation", sizeof(info->name)); info->min_value = -180; info->max_value = 180; info->default_value = 0; return true;
    case 6: info->id = kParamDirectivity; std::strncpy(info->name, "Directivity", sizeof(info->name)); info->min_value = 0; info->max_value = 100; info->default_value = 70; return true;
    case 7: info->id = kParamRearReject; std::strncpy(info->name, "Rear rejection", sizeof(info->name)); info->min_value = 0; info->max_value = 100; info->default_value = 35; return true;
    case 8: info->id = kParamHeightFold; std::strncpy(info->name, "Height fold", sizeof(info->name)); info->min_value = 0; info->max_value = 100; info->default_value = 30; return true;
    case 9: info->id = kParamDiffuse; std::strncpy(info->name, "Diffuse blend", sizeof(info->name)); info->min_value = 0; info->max_value = 100; info->default_value = 0; return true;
    case 10: info->id = kParamWeighting; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Decode weighting", sizeof(info->name)); info->min_value = 0; info->max_value = 3; info->default_value = 1; return true;
    case 11: info->id = kParamAutogain; info->flags |= CLAP_PARAM_IS_STEPPED; std::strncpy(info->name, "Autogain", sizeof(info->name)); info->min_value = 0; info->max_value = 2; info->default_value = 1; return true;
    case 12: info->id = kParamOutputGain; std::strncpy(info->name, "Output gain", sizeof(info->name)); info->min_value = -24; info->max_value = 24; info->default_value = 0; return true;
    case 13: info->id = kParamAbSpacing; std::strncpy(info->name, "A/B spacing", sizeof(info->name)); info->min_value = 0; info->max_value = 120; info->default_value = 40; return true;
    case 14: info->id = kParamBassMono; std::strncpy(info->name, "Bass mono below", sizeof(info->name)); info->min_value = 0; info->max_value = 300; info->default_value = 0; return true;
    case 15: info->id = kParamMicElevation; std::strncpy(info->name, "Mic elevation", sizeof(info->name)); info->min_value = -90; info->max_value = 90; info->default_value = 0; return true;
    default: return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value) return false;
    *value = getParamValue(*self(plugin), paramId);
    return paramId >= kParamOrder && paramId <= kParamMicElevation;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    switch (paramId) {
    case kParamOrder: std::snprintf(display, size, "%uOA", roundedUint(value)); return true;
    case kParamLayout: std::snprintf(display, size, "%s", layoutName(roundedUint(value))); return true;
    case kParamMethod: std::snprintf(display, size, "%s", methodName(roundedUint(value))); return true;
    case kParamWeighting: std::snprintf(display, size, "%s", weightingName(roundedUint(value))); return true;
    case kParamAutogain: std::snprintf(display, size, "%s", autogainName(roundedUint(value))); return true;
    case kParamWidth:
    case kParamDirectivity:
    case kParamRearReject:
    case kParamHeightFold:
    case kParamDiffuse:
        std::snprintf(display, size, "%.0f %%", value); return true;
    case kParamAngle:
    case kParamRotation:
    case kParamMicElevation:
        std::snprintf(display, size, "%+.0f deg", value); return true;
    case kParamOutputGain: std::snprintf(display, size, "%+.1f dB", value); return true;
    case kParamAbSpacing: std::snprintf(display, size, "%.0f cm", value); return true;
    case kParamBassMono: std::snprintf(display, size, "%.0f Hz", value); return true;
    default: return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value) return false;
    *value = std::atof(display);
    return paramId >= kParamOrder && paramId <= kParamMicElevation;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), in);
}

const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

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
    const int64_t got = stream->read(stream, &state, sizeof(state));
    if (got != static_cast<int64_t>(sizeof(state)) || state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = sanitizeParams(state.params);
    markCoeffsDirty(*p);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

static NSColor* s3gAmbiStereoColor(int rgb, CGFloat alpha = 1.0)
{
    return [NSColor colorWithCalibratedRed:((rgb >> 16) & 0xff) / 255.0
                                     green:((rgb >> 8) & 0xff) / 255.0
                                      blue:(rgb & 0xff) / 255.0
                                     alpha:alpha];
}

@interface S3GAmbisonicStereoDecoderView : NSView {
    void* _plugin;
    int _dragSlider;
    int _openMenu;
    int _hoverMenuItem;
    int _viewMode;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    float _viewYawDeg;
    float _viewPitchDeg;
    NSPoint _menuOrigin;
    uint32_t _menuItems;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y small:(NSDictionary*)small;
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y small:(NSDictionary*)small;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
@end

@implementation S3GAmbisonicStereoDecoderView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragSlider = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _viewMode = 0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewYawDeg = -35.0f;
        _viewPitchDeg = -42.0f;
        _menuItems = 0;
        _menuOrigin = NSMakePoint(0, 0);
        _refreshTimer = nil;
        [[self window] setAcceptsMouseMovedEvents:YES];
    }
    return self;
}
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)viewDidMoveToWindow { [[self window] setAcceptsMouseMovedEvents:YES]; }
- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 24.0) target:self selector:@selector(refreshTimerFired:) userInfo:nil repeats:YES];
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
    if (![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}
- (void)setParam:(clap_id)param value:(double)value
{
    setParamValue(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawSlider(name, value, norm, y, small, small, style, 606, 712, 852, 122);
}
- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y small:(NSDictionary*)small
{
    s3g::clap_gui::Style style;
    s3g::clap_gui::drawMenu(name, value, y, small, small, style, 606, 712, 176);
}
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    return NSMakeRect(rect.origin.x + rect.size.width - 168.0 + index * 52.0, rect.origin.y + 3.0, 46.0, 15.0);
}
- (NSPoint)projectAzimuth:(float)azDeg elevation:(float)elDeg radius:(CGFloat)radius inRect:(NSRect)field
{
    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.54;
    const float az = azDeg * s3g::kPi / 180.0f;
    const float el = elDeg * s3g::kPi / 180.0f;
    const float x = -std::sin(az) * std::cos(el);
    const float y = std::cos(az) * std::cos(el);
    const float z = std::sin(el);
    if (_viewMode == 0) {
        return NSMakePoint(cx + static_cast<CGFloat>(x) * radius,
                           cy - static_cast<CGFloat>(y) * radius);
    }
    if (_viewMode == 1) {
        return NSMakePoint(cx + static_cast<CGFloat>(y) * radius * 0.92,
                           cy - static_cast<CGFloat>(z) * radius * 0.92);
    }

    const float yaw = _viewYawDeg * s3g::kPi / 180.0f;
    const float pitch = _viewPitchDeg * s3g::kPi / 180.0f;
    const float x1 = x * std::cos(yaw) - y * std::sin(yaw);
    const float y1 = x * std::sin(yaw) + y * std::cos(yaw);
    const float z1 = z * std::cos(pitch) - y1 * std::sin(pitch);
    const float y2 = z * std::sin(pitch) + y1 * std::cos(pitch);
    return NSMakePoint(cx + static_cast<CGFloat>(x1) * radius * 0.92,
                       cy - static_cast<CGFloat>(z1 + y2 * 0.10f) * radius * 0.92);
}
- (NSPoint)projectPoint:(s3g::AmbiStereoVirtualPoint)pt inRect:(NSRect)field
{
    const CGFloat maxR = std::min(field.size.width, field.size.height) * 0.42;
    return [self projectAzimuth:pt.azimuthDeg elevation:pt.elevationDeg radius:maxR inRect:field];
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSDictionary* text = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:mono };
    [@"s3g AMBISONIC STEREO DECODER" drawAtPoint:NSMakePoint(18, 13) withAttributes:text];
    [[NSString stringWithFormat:@"%uOA ACN/SN3D / TRUE 2OUT", p->params.order] drawAtPoint:NSMakePoint(760, 13) withAttributes:small];

    NSRect fieldPanel = NSMakeRect(12, 34, 568, 514);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"STEREO IMAGE", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:fieldPanel], fieldPanel, viewLabels[i], i == _viewMode, small, style);
    }
    NSRect field = NSMakeRect(28, 70, 536, 402);
    [s3gAmbiStereoColor(0x101010) setFill];
    NSRectFill(field);
    [style.grid setStroke];
    NSFrameRect(field);
    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.54;
    const CGFloat maxR = std::min(field.size.width, field.size.height) * 0.42;
    [s3gAmbiStereoColor(0x5d5d5d, 0.20) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 28, cy) toPoint:NSMakePoint(NSMaxX(field) - 28, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, field.origin.y + 24) toPoint:NSMakePoint(cx, NSMaxY(field) - 24)];
    if (_viewMode == 0) {
        [@"0" drawAtPoint:NSMakePoint(cx - 4, cy - maxR - 20) withAttributes:small];
        [@"-90" drawAtPoint:NSMakePoint(cx + maxR + 10, cy - 7) withAttributes:small];
        [@"+90" drawAtPoint:NSMakePoint(cx - maxR - 34, cy - 7) withAttributes:small];
        [@"180" drawAtPoint:NSMakePoint(cx - 10, cy + maxR + 6) withAttributes:small];
    } else if (_viewMode == 1) {
        [@"FRONT" drawAtPoint:NSMakePoint(cx + maxR * 0.78, cy + 8) withAttributes:small];
        [@"BACK" drawAtPoint:NSMakePoint(cx - maxR * 0.94, cy + 8) withAttributes:small];
        [@"+EL" drawAtPoint:NSMakePoint(cx + 8, cy - maxR - 2) withAttributes:small];
        [@"-EL" drawAtPoint:NSMakePoint(cx + 8, cy + maxR - 12) withAttributes:small];
    }

    auto viewPointForAzEl = [&](float azDeg, float elDeg, CGFloat radius) -> NSPoint {
        return [self projectAzimuth:azDeg elevation:elDeg radius:radius inRect:field];
    };

    const float widthScale = std::clamp(p->params.stereoWidthPercent / 100.0f, 0.0f, 2.0f);
    const float visualAngle = p->params.micAngleDeg * 0.5f * widthScale;
    const float directivity = std::clamp(p->params.directivityPercent / 100.0f, 0.0f, 1.0f);
    const auto method = p->params.method;
    auto visualResponse = [&](s3g::AmbiStereoMethod visualMethod, float relr) -> float {
        if (visualMethod == s3g::AmbiStereoMethod::SpacedOmni) {
            return 1.0f;
        }
        if (visualMethod == s3g::AmbiStereoMethod::Blumlein) {
            return std::cos(relr);
        }
        const float cardioid = 0.5f + 0.5f * std::cos(relr);
        if (visualMethod == s3g::AmbiStereoMethod::DualShotgun) {
            return std::pow((1.0f - directivity) + directivity * cardioid, 2.25f + directivity * 1.75f);
        }
        if (visualMethod == s3g::AmbiStereoMethod::WideCardioid) {
            return (1.0f - directivity * 0.42f) + directivity * 0.42f * cardioid;
        }
        if (visualMethod == s3g::AmbiStereoMethod::SupercardioidXy) {
            return 0.37f + 0.63f * std::cos(relr);
        }
        if (visualMethod == s3g::AmbiStereoMethod::HypercardioidXy) {
            return 0.25f + 0.75f * std::cos(relr);
        }
        if (visualMethod == s3g::AmbiStereoMethod::HeightFocus) {
            return ((1.0f - directivity) + directivity * cardioid) * 0.82f;
        }
        return (1.0f - directivity) + directivity * cardioid;
    };
    auto drawPickupLobe = [&](NSPoint origin, bool leftSide, float facingDeg, s3g::AmbiStereoMethod visualMethod) {
        NSBezierPath* path = [NSBezierPath bezierPath];
        constexpr int steps = 96;
        for (int i = 0; i <= steps; ++i) {
            const float rel = -180.0f + static_cast<float>(i) * 360.0f / static_cast<float>(steps);
            const float relr = rel * s3g::kPi / 180.0f;
            const float response = std::abs(visualResponse(visualMethod, relr));
            const float az = p->params.rotationDeg + facingDeg + rel;
            const CGFloat radius = maxR * (0.06 + (0.30 + 0.10 * widthScale) * response);
            const NSPoint raw = viewPointForAzEl(az, p->params.micElevationDeg, radius);
            const NSPoint center = viewPointForAzEl(az, p->params.micElevationDeg, 0.0);
            const NSPoint pt = NSMakePoint(origin.x + raw.x - center.x, origin.y + raw.y - center.y);
            if (i == 0) [path moveToPoint:pt];
            else [path lineToPoint:pt];
        }
        [path closePath];
        [s3gAmbiStereoColor(leftSide ? 0x8f8f8f : 0xd8d8d8, leftSide ? 0.17 : 0.13) setFill];
        [path fill];
        [s3gAmbiStereoColor(leftSide ? 0xb2b2b2 : 0xf0f0f0, leftSide ? 0.48 : 0.42) setStroke];
        [path stroke];
        if (visualMethod == s3g::AmbiStereoMethod::Blumlein
            || visualMethod == s3g::AmbiStereoMethod::SupercardioidXy
            || visualMethod == s3g::AmbiStereoMethod::HypercardioidXy) {
            NSBezierPath* negative = [NSBezierPath bezierPath];
            bool open = false;
            for (int i = 0; i <= steps; ++i) {
                const float rel = -180.0f + static_cast<float>(i) * 360.0f / static_cast<float>(steps);
                const float relr = rel * s3g::kPi / 180.0f;
                const float signedResponse = visualResponse(visualMethod, relr);
                const float response = std::abs(signedResponse);
                const float az = p->params.rotationDeg + facingDeg + rel;
                const CGFloat radius = maxR * (0.06 + (0.30 + 0.10 * widthScale) * response);
                const NSPoint raw = viewPointForAzEl(az, p->params.micElevationDeg, radius);
                const NSPoint center = viewPointForAzEl(az, p->params.micElevationDeg, 0.0);
                const NSPoint pt = NSMakePoint(origin.x + raw.x - center.x, origin.y + raw.y - center.y);
                if (signedResponse < 0.0f) {
                    if (!open) {
                        [negative moveToPoint:pt];
                        open = true;
                    } else {
                        [negative lineToPoint:pt];
                    }
                } else {
                    open = false;
                }
            }
            CGFloat dash[] = { 5.0, 4.0 };
            [negative setLineDash:dash count:2 phase:0.0];
            [negative setLineWidth:1.8];
            [s3gAmbiStereoColor(leftSide ? 0xb2b2b2 : 0xf0f0f0, 0.72) setStroke];
            [negative stroke];
        }
        const NSPoint axis = viewPointForAzEl(p->params.rotationDeg + facingDeg, p->params.micElevationDeg, maxR * 0.34);
        const NSPoint axisCenter = viewPointForAzEl(p->params.rotationDeg + facingDeg, p->params.micElevationDeg, 0.0);
        const NSPoint shiftedAxis = NSMakePoint(origin.x + axis.x - axisCenter.x, origin.y + axis.y - axisCenter.y);
        [s3gAmbiStereoColor(leftSide ? 0xb2b2b2 : 0xf0f0f0, leftSide ? 0.64 : 0.54) setStroke];
        [NSBezierPath strokeLineFromPoint:origin toPoint:shiftedAxis];
    };

    const float rot = p->params.rotationDeg;
    const NSPoint centerPoint = viewPointForAzEl(rot, p->params.micElevationDeg, 0.0);
    if (method == s3g::AmbiStereoMethod::MsCardioid) {
        drawPickupLobe(centerPoint, true, 0.0f, s3g::AmbiStereoMethod::XyCardioid);
        drawPickupLobe(centerPoint, false, 90.0f, s3g::AmbiStereoMethod::Blumlein);
        const NSPoint mid = viewPointForAzEl(rot, p->params.micElevationDeg, maxR * 0.34);
        const NSPoint sidePlus = viewPointForAzEl(rot + 90.0f, p->params.micElevationDeg, maxR * 0.27);
        const NSPoint sideMinus = viewPointForAzEl(rot - 90.0f, p->params.micElevationDeg, maxR * 0.27);
        [@"M" drawAtPoint:NSMakePoint(mid.x + 7, mid.y - 7) withAttributes:small];
        [@"S+" drawAtPoint:NSMakePoint(sidePlus.x + 7, sidePlus.y - 7) withAttributes:small];
        [@"S-" drawAtPoint:NSMakePoint(sideMinus.x + 7, sideMinus.y - 7) withAttributes:small];
        [s3gAmbiStereoColor(0xbdbdbd, 0.34) setStroke];
        [NSBezierPath strokeLineFromPoint:sidePlus toPoint:sideMinus];
        [@"L=M+S  R=M-S" drawAtPoint:NSMakePoint(cx + 12, cy + 10) withAttributes:small];
    } else {
        CGFloat capsuleOffset = 0.0;
        if (method == s3g::AmbiStereoMethod::SpacedOmni) {
            capsuleOffset = maxR * (0.02 + 0.20 * std::clamp<CGFloat>(p->params.abSpacingCm / 120.0f, 0.0, 1.0));
        } else if (method == s3g::AmbiStereoMethod::OrtfStyle) {
            capsuleOffset = maxR * 0.075;
        }
        const NSPoint leftOrigin = viewPointForAzEl(rot + 90.0f, p->params.micElevationDeg, capsuleOffset);
        const NSPoint rightOrigin = viewPointForAzEl(rot - 90.0f, p->params.micElevationDeg, capsuleOffset);
        drawPickupLobe(leftOrigin, true, visualAngle, method);
        drawPickupLobe(rightOrigin, false, -visualAngle, method);
        [s3gAmbiStereoColor(0xd1d1d1, 0.30) setStroke];
        [NSBezierPath strokeLineFromPoint:leftOrigin toPoint:rightOrigin];
        [s3gAmbiStereoColor(0xd1d1d1, 0.78) setFill];
        NSRectFill(NSMakeRect(leftOrigin.x - 2, leftOrigin.y - 2, 4, 4));
        NSRectFill(NSMakeRect(rightOrigin.x - 2, rightOrigin.y - 2, 4, 4));
        if (method != s3g::AmbiStereoMethod::OrtfStyle) {
            auto shiftedAxisPoint = [&](NSPoint origin, float facingDeg) -> NSPoint {
                const NSPoint axis = viewPointForAzEl(rot + facingDeg, p->params.micElevationDeg, maxR * 0.34);
                const NSPoint axisCenter = viewPointForAzEl(rot + facingDeg, p->params.micElevationDeg, 0.0);
                return NSMakePoint(origin.x + axis.x - axisCenter.x, origin.y + axis.y - axisCenter.y);
            };
            const NSPoint leftAxis = shiftedAxisPoint(leftOrigin, visualAngle);
            const NSPoint rightAxis = shiftedAxisPoint(rightOrigin, -visualAngle);
            [@"L MIC" drawAtPoint:NSMakePoint(leftAxis.x + 8, leftAxis.y - 7) withAttributes:small];
            [@"R MIC" drawAtPoint:NSMakePoint(rightAxis.x + 8, rightAxis.y - 7) withAttributes:small];
        }
        if (method == s3g::AmbiStereoMethod::SpacedOmni) {
            [[NSString stringWithFormat:@"AB %.0fcm", static_cast<double>(p->params.abSpacingCm)]
                drawAtPoint:NSMakePoint(cx - 28, cy + 24)
                withAttributes:small];
        }
    }
    [style.text setFill];
    NSRectFill(NSMakeRect(cx - 3, cy - 3, 6, 6));

    const CGFloat elevX = NSMaxX(field) - 36.0;
    const CGFloat elevTop = field.origin.y + 62.0;
    const CGFloat elevH = 112.0;
    const CGFloat elevMid = elevTop + elevH * 0.5;
    const CGFloat elevNorm = std::clamp<CGFloat>((p->params.micElevationDeg + 90.0f) / 180.0f, 0.0, 1.0);
    const CGFloat elevY = elevTop + elevH * (1.0 - elevNorm);
    [s3gAmbiStereoColor(0x5d5d5d, 0.32) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(elevX, elevTop) toPoint:NSMakePoint(elevX, elevTop + elevH)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(elevX - 10, elevMid) toPoint:NSMakePoint(elevX + 10, elevMid)];
    [s3gAmbiStereoColor(0xd1d1d1, 0.78) setFill];
    NSRectFill(NSMakeRect(elevX - 5, elevY - 3, 10, 6));
    [@"M-EL" drawAtPoint:NSMakePoint(elevX - 14, elevTop - 17) withAttributes:small];
    [[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.micElevationDeg)]
        drawAtPoint:NSMakePoint(elevX - 16, elevTop + elevH + 5)
        withAttributes:small];

    const uint32_t vcount = s3g::ambiStereoVirtualCount(p->params.layout);
    for (uint32_t i = 0; i < vcount; ++i) {
        const auto pt = s3g::ambiStereoVirtualPoint(p->params.layout, i);
        const NSPoint a = [self projectPoint:pt inRect:field];
        for (uint32_t j = i + 1; j < vcount; ++j) {
            const auto other = s3g::ambiStereoVirtualPoint(p->params.layout, j);
            const float daz = std::abs(s3g::ambiStereoWrapSignedDeg(pt.azimuthDeg - other.azimuthDeg));
            const float del = std::abs(pt.elevationDeg - other.elevationDeg);
            const bool sameTier = del < 4.0f && daz <= (vcount <= 12u ? 90.5f : 45.5f);
            const bool verticalTier = daz < 8.0f && del <= 70.0f;
            if (sameTier || verticalTier || (vcount <= 12u && daz <= 90.5f && del <= 72.0f)) {
                const NSPoint b = [self projectPoint:other inRect:field];
                [s3gAmbiStereoColor(0x9a9a9a, 0.22) setStroke];
                [NSBezierPath strokeLineFromPoint:a toPoint:b];
            }
        }
    }
    for (uint32_t i = 0; i < vcount; ++i) {
        const auto pt = s3g::ambiStereoVirtualPoint(p->params.layout, i);
        const NSPoint a = [self projectPoint:pt inRect:field];
        float l = 0.0f;
        float r = 0.0f;
        s3g::ambiStereoPickup(p->params, pt.azimuthDeg, pt.elevationDeg, l, r);
        const CGFloat energy = std::clamp<CGFloat>((std::abs(l) + std::abs(r)) * 0.42, 0.18, 1.0);
        const CGFloat elevationTone = std::clamp<CGFloat>((pt.elevationDeg + 90.0f) / 180.0f, 0.0, 1.0);
        const CGFloat size = 5.0 + energy * 3.0;
        [s3gAmbiStereoColor(0xc8c8c8, 0.42 + energy * 0.48) setFill];
        NSRectFill(NSMakeRect(a.x - size * 0.5, a.y - size * 0.5, size, size));
        [s3gAmbiStereoColor(elevationTone > 0.55 ? 0xf0f0f0 : 0x8a8a8a, 0.64) setStroke];
        NSFrameRect(NSMakeRect(a.x - size * 0.5, a.y - size * 0.5, size, size));
        if (i < 8u || (i % 4u) == 0u) {
            [[NSString stringWithFormat:@"%u", i + 1] drawAtPoint:NSMakePoint(a.x + 7, a.y - 6) withAttributes:small];
        }
    }
    NSString* viewName = _viewMode == 0 ? @"TOP VIEW / VIRTUAL MICS + SPEAKER FIELD"
        : (_viewMode == 1 ? @"SIDE VIEW / HEIGHT FOLD + VIRTUAL FIELD" : @"3/4 VIEW / VIRTUAL FIELD GEOMETRY");
    [viewName drawAtPoint:NSMakePoint(40, 84) withAttributes:small];
    [[NSString stringWithFormat:@"%@ / %@ / %@",
      [NSString stringWithUTF8String:layoutName(static_cast<uint32_t>(p->params.layout))],
      [NSString stringWithUTF8String:methodName(static_cast<uint32_t>(p->params.method))],
      [NSString stringWithUTF8String:weightingName(static_cast<uint32_t>(p->params.weighting))]]
        drawAtPoint:NSMakePoint(28, 488) withAttributes:small];

    NSRect side = NSMakeRect(592, 34, 336, 594);
    NSRect decoder = NSMakeRect(side.origin.x, 34, side.size.width, 128);
    NSRect pickup = NSMakeRect(side.origin.x, 174, side.size.width, 184);
    NSRect fieldMix = NSMakeRect(side.origin.x, 370, side.size.width, 118);
    NSRect output = NSMakeRect(side.origin.x, 500, side.size.width, 118);
    s3g::clap_gui::drawPanelFrame(decoder.origin.x, decoder.origin.y, decoder.size.width, decoder.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"DECODER", true, decoder.origin.x, decoder.origin.y, decoder.size.width, 21, text, style);
    [self drawSlider:@"ORD" value:[NSString stringWithFormat:@"%uOA", p->params.order] norm:(p->params.order - 1.0) / 6.0 y:74 small:small];
    [self drawMenu:@"FIELD" value:[NSString stringWithUTF8String:layoutName(static_cast<uint32_t>(p->params.layout))] y:96 small:small];
    [self drawMenu:@"WGT" value:[NSString stringWithUTF8String:weightingName(static_cast<uint32_t>(p->params.weighting))] y:118 small:small];
    [self drawMenu:@"AGN" value:[NSString stringWithUTF8String:autogainName(static_cast<uint32_t>(p->params.autogain))] y:140 small:small];

    s3g::clap_gui::drawPanelFrame(pickup.origin.x, pickup.origin.y, pickup.size.width, pickup.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"STEREO PICKUP", true, pickup.origin.x, pickup.origin.y, pickup.size.width, 21, text, style);
    [self drawMenu:@"MTHD" value:[NSString stringWithUTF8String:methodName(static_cast<uint32_t>(p->params.method))] y:214 small:small];
    [self drawSlider:@"WDTH" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.stereoWidthPercent)] norm:p->params.stereoWidthPercent / 200.0 y:236 small:small];
    [self drawSlider:@"ANG" value:[NSString stringWithFormat:@"%.0f", static_cast<double>(p->params.micAngleDeg)] norm:(p->params.micAngleDeg - 20.0) / 120.0 y:258 small:small];
    [self drawSlider:@"ROT" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.rotationDeg)] norm:(p->params.rotationDeg + 180.0) / 360.0 y:280 small:small];
    [self drawSlider:@"DIR" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.directivityPercent)] norm:p->params.directivityPercent / 100.0 y:302 small:small];
    [self drawSlider:@"AB" value:[NSString stringWithFormat:@"%.0fcm", static_cast<double>(p->params.abSpacingCm)] norm:p->params.abSpacingCm / 120.0 y:324 small:small];
    [self drawSlider:@"M-EL" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(p->params.micElevationDeg)] norm:(p->params.micElevationDeg + 90.0) / 180.0 y:346 small:small];

    s3g::clap_gui::drawPanelFrame(fieldMix.origin.x, fieldMix.origin.y, fieldMix.size.width, fieldMix.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD MIX", true, fieldMix.origin.x, fieldMix.origin.y, fieldMix.size.width, 21, text, style);
    [self drawSlider:@"REAR" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.rearRejectPercent)] norm:p->params.rearRejectPercent / 100.0 y:410 small:small];
    [self drawSlider:@"HGT" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.heightFoldPercent)] norm:p->params.heightFoldPercent / 100.0 y:432 small:small];
    [self drawSlider:@"DIF" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.diffuseBlendPercent)] norm:p->params.diffuseBlendPercent / 100.0 y:454 small:small];
    [self drawSlider:@"BASS" value:[NSString stringWithFormat:@"%.0f", static_cast<double>(p->params.bassMonoHz)] norm:p->params.bassMonoHz / 300.0 y:476 small:small];

    s3g::clap_gui::drawPanelFrame(output.origin.x, output.origin.y, output.size.width, output.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, output.origin.x, output.origin.y, output.size.width, 21, text, style);
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(p->params.outputGainDb)] norm:(p->params.outputGainDb + 24.0) / 48.0 y:538 small:small];

    const float pkL = p->outputPeakLeft.exchange(p->outputPeakLeft.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    const float pkR = p->outputPeakRight.exchange(p->outputPeakRight.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    auto drawMeter = [&](CGFloat y, NSString* name, float peak) {
        const CGFloat x = 638.0;
        const CGFloat w = 196.0;
        const double db = 20.0 * std::log10(std::max(0.000001f, peak));
        const CGFloat norm = std::clamp<CGFloat>((db + 60.0) / 60.0, 0.0, 1.0);
        NSRect r = NSMakeRect(x, y, w, 15);
        [style.strip setFill]; NSRectFill(r);
        [style.fill setFill]; NSRectFill(NSMakeRect(r.origin.x + 1, r.origin.y + 1, (r.size.width - 2) * norm, r.size.height - 2));
        [style.grid setStroke]; NSFrameRect(r);
        [name drawAtPoint:NSMakePoint(612, y - 1) withAttributes:small];
        [[NSString stringWithFormat:@"%+4.1f", db] drawAtPoint:NSMakePoint(850, y - 1) withAttributes:small];
    };
    drawMeter(566, @"L", pkL);
    drawMeter(588, @"R", pkR);

    if (_openMenu > 0 && _menuItems > 0) {
        NSString* layoutItems[] = { @"Quad virtual", @"8ch cube", @"12ch dodeca", @"24ch dome", @"32ch sphere" };
        NSString* methodItems[] = { @"XY cardioid", @"ORTF-style", @"MS cardioid", @"Blumlein", @"Spaced omni", @"Dual shotgun", @"Wide cardioid", @"Supercardioid XY", @"Hypercardioid XY", @"Height focus" };
        NSString* weightItems[] = { @"Projection", @"Energy-normalized", @"Max-rE", @"Custom" };
        NSString* gainItems[] = { @"Off", @"Power/sqrt(N)", @"Energy sum" };
        NSString** items = layoutItems;
        int selected = static_cast<int>(p->params.layout);
        if (_openMenu == 2) { items = methodItems; selected = static_cast<int>(p->params.method); }
        if (_openMenu == 3) { items = weightItems; selected = static_cast<int>(p->params.weighting); }
        if (_openMenu == 4) { items = gainItems; selected = static_cast<int>(p->params.autogain); }
        const CGFloat itemH = 18.0;
        NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 176, itemH * _menuItems);
        s3g::clap_gui::drawDropdownMenu(menu, itemH, items, _menuItems, selected, _hoverMenuItem, small, style);
    }
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp((pt.x - 712.0) / 122.0, 0.0, 1.0);
    switch (_dragSlider) {
    case 0: [self setParam:kParamOrder value:1.0 + norm * 6.0]; break;
    case 3: [self setParam:kParamWidth value:norm * 200.0]; break;
    case 4: [self setParam:kParamAngle value:20.0 + norm * 120.0]; break;
    case 5: [self setParam:kParamRotation value:-180.0 + norm * 360.0]; break;
    case 6: [self setParam:kParamDirectivity value:norm * 100.0]; break;
    case 7: [self setParam:kParamRearReject value:norm * 100.0]; break;
    case 8: [self setParam:kParamHeightFold value:norm * 100.0]; break;
    case 9: [self setParam:kParamDiffuse value:norm * 100.0]; break;
    case 12: [self setParam:kParamAbSpacing value:norm * 120.0]; break;
    case 13: [self setParam:kParamBassMono value:norm * 300.0]; break;
    case 14: [self setParam:kParamMicElevation value:-90.0 + norm * 180.0]; break;
    case 15: [self setParam:kParamOutputGain value:-24.0 + norm * 48.0]; break;
    default: break;
    }
}
- (void)updateMenuHover:(NSPoint)pt
{
    if (_openMenu <= 0 || _menuItems == 0) return;
    NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 176, 18.0 * _menuItems);
    const int hover = s3g::clap_gui::dropdownHitIndex(pt, menu, 18.0, _menuItems);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        NSRect menu = NSMakeRect(_menuOrigin.x, _menuOrigin.y, 176, 18.0 * _menuItems);
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, menu, 18.0, _menuItems);
        if (hit >= 0) {
            if (_openMenu == 1) [self setParam:kParamLayout value:hit];
            if (_openMenu == 2) [self setParam:kParamMethod value:hit];
            if (_openMenu == 3) [self setParam:kParamWeighting value:hit];
            if (_openMenu == 4) [self setParam:kParamAutogain value:hit];
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect fieldPanel = NSMakeRect(12, 34, 568, 514);
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(pt, [self viewButtonRect:i inRect:fieldPanel])) {
            _viewMode = i;
            if (i == 0) {
                _viewYawDeg = 0.0f;
                _viewPitchDeg = 0.0f;
            } else if (i == 1) {
                _viewYawDeg = 0.0f;
                _viewPitchDeg = -90.0f;
            } else {
                _viewYawDeg = -35.0f;
                _viewPitchDeg = -42.0f;
            }
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const NSRect field = NSMakeRect(28, 70, 536, 402);
    if (NSPointInRect(pt, field)) {
        if (_viewMode == 0) {
            _viewYawDeg = 0.0f;
            _viewPitchDeg = 0.0f;
        } else if (_viewMode == 1) {
            _viewYawDeg = 0.0f;
            _viewPitchDeg = -90.0f;
        }
        _viewMode = 2;
        _dragView = YES;
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    struct HitRow { int index; CGFloat y; bool menu; int openMenu; uint32_t menuItems; };
    const HitRow rows[] = {
        { 0, 74, false, 0, 0 },
        { 1, 96, true, 1, 5 },
        { 10, 118, true, 3, 4 },
        { 11, 140, true, 4, 3 },
        { 2, 214, true, 2, 10 },
        { 3, 236, false, 0, 0 },
        { 4, 258, false, 0, 0 },
        { 5, 280, false, 0, 0 },
        { 6, 302, false, 0, 0 },
        { 12, 324, false, 0, 0 },
        { 14, 346, false, 0, 0 },
        { 7, 410, false, 0, 0 },
        { 8, 432, false, 0, 0 },
        { 9, 454, false, 0, 0 },
        { 13, 476, false, 0, 0 },
        { 15, 538, false, 0, 0 },
    };
    for (const auto& row : rows) {
        NSRect r = NSMakeRect(596, row.y - 6, 316, 22);
        if (!NSPointInRect(pt, r)) continue;
        if (row.menu) {
            _openMenu = row.openMenu;
            _menuItems = row.menuItems;
            _menuOrigin = NSMakePoint(712, row.y + 17);
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
        _dragSlider = row.index;
        [self updateSliderAtPoint:pt];
        return;
    }
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    [self updateMenuHover:pt];
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewYawDeg += static_cast<float>(dx) * 0.45f;
        _viewPitchDeg = std::clamp(_viewPitchDeg + static_cast<float>(dy) * 0.35f, -89.0f, 89.0f);
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragSlider >= 0) [self updateSliderAtPoint:pt];
}
- (void)mouseMoved:(NSEvent*)event { [self updateMenuHover:[self convertPoint:[event locationInWindow] fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragView = NO; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GAmbisonicStereoDecoderView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GAmbisonicStereoDecoderView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = false; hints->can_resize_vertically = false; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,kGuiWidth,kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbisonicStereoDecoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbisonicStereoDecoderView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
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
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambisonic-stereo-decoder",
    "s3g Ambisonic Stereo Decoder",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "ACN/SN3D ambisonic input to true stereo virtual-microphone decoder.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
