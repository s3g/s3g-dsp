#include "s3g_ambi_object_decoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

namespace {

constexpr uint32_t kInputChannels = s3g::kAmbiSpeakerDecoderMaxChannels;
constexpr uint32_t kOutputChannels = s3g::kAmbiSpeakerDecoderMaxSpeakers;
constexpr uint32_t kStateVersion = 3;
constexpr uint32_t kLayoutMenuCount = 14;

constexpr clap_id kLayoutParamId = 1;
constexpr clap_id kModeParamId = 2;
constexpr clap_id kOrderParamId = 3;
constexpr clap_id kWeightingParamId = 4;
constexpr clap_id kObjectMethodParamId = 5;
constexpr clap_id kBlendParamId = 6;
constexpr clap_id kFieldGainParamId = 7;
constexpr clap_id kObjectGainParamId = 8;
constexpr clap_id kDirectionSmoothingParamId = 9;
constexpr clap_id kOutputParamId = 10;
constexpr clap_id kObjectConfidenceParamId = 11;
constexpr clap_id kObjectHighpassParamId = 12;

struct LegacyAmbiObjectDecoderParamsV2 {
    s3g::AmbiSpeakerDecoderParams decoder {};
    s3g::AmbiObjectMethod objectMethod = s3g::AmbiObjectMethod::Vbap;
    float objectBlend = 0.35f;
    float objectGainDb = 0.0f;
    float fieldGainDb = 0.0f;
    float directionSmoothingMs = 25.0f;
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiObjectDecoderParams params {};
    int32_t guiViewMode = 2;
    double guiViewAzDeg = 35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
};

struct SavedStateV1 {
    uint32_t version = 1;
    LegacyAmbiObjectDecoderParamsV2 params {};
};

struct SavedStateV2 {
    uint32_t version = 2;
    LegacyAmbiObjectDecoderParamsV2 params {};
    int32_t guiViewMode = 2;
    double guiViewAzDeg = 35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiObjectDecoder decoder {};
    s3g::AmbiObjectDecoderParams params {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> objectDirX { 1.0f };
    std::atomic<float> objectDirY { 0.0f };
    std::atomic<float> objectDirZ { 0.0f };
    std::atomic<float> objectCue { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    int guiViewMode = 2;
    double guiViewAzDeg = 35.0;
    double guiViewElDeg = 34.0;
    double guiViewZoom = 1.0;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->write(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->read(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

uint32_t layoutPresetForMenuIndex(uint32_t index)
{
    static constexpr uint32_t values[kLayoutMenuCount] {
        0u, 2u, 3u, 11u, 8u, 4u, 5u, 9u, 12u, 10u, 1u, 6u, 13u, 7u
    };
    return values[std::min<uint32_t>(index, kLayoutMenuCount - 1u)];
}

uint32_t menuIndexForLayoutPreset(uint32_t preset)
{
    for (uint32_t i = 0; i < kLayoutMenuCount; ++i) {
        if (layoutPresetForMenuIndex(i) == preset) return i;
    }
    return 13u;
}

const char* layoutName(uint32_t value)
{
    switch (value) {
    case 1: return "QUAD";
    case 2: return "CUBE 8";
    case 3: return "CUBE 17";
    case 4: return "DOME 24";
    case 5: return "DOME 25";
    case 6: return "QUAD+OH";
    case 7: return "SPHERE 24";
    case 8: return "DODECA 12";
    case 9: return "ICOSAHEDRON 20";
    case 10: return "OCTO RING";
    case 11: return "CUBE 41";
    case 12: return "LPAC 41";
    case 13: return "SRST 25";
    default: return "CUSTOM";
    }
}

const char* modeName(s3g::AmbiSpeakerDecoderMode mode)
{
    switch (mode) {
    case s3g::AmbiSpeakerDecoderMode::Basic: return "BASIC";
    case s3g::AmbiSpeakerDecoderMode::Mmd: return "MMD";
    case s3g::AmbiSpeakerDecoderMode::Epad:
    default: return "EPAD";
    }
}

const char* weightingName(s3g::AmbiSpeakerDecoderWeighting weighting)
{
    switch (weighting) {
    case s3g::AmbiSpeakerDecoderWeighting::None: return "NONE";
    case s3g::AmbiSpeakerDecoderWeighting::InPhase: return "INPHASE";
    case s3g::AmbiSpeakerDecoderWeighting::MaxRe:
    default: return "MAXRE";
    }
}

s3g::AmbiObjectDecoderParams upgradeLegacyParams(const LegacyAmbiObjectDecoderParamsV2& old)
{
    s3g::AmbiObjectDecoderParams next {};
    next.decoder = old.decoder;
    next.objectMethod = old.objectMethod;
    next.objectBlend = old.objectBlend;
    next.objectGainDb = old.objectGainDb;
    next.fieldGainDb = old.fieldGainDb;
    next.directionSmoothingMs = old.directionSmoothingMs;
    return next;
}

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kLayoutParamId:
        p.params.decoder.layout = static_cast<s3g::AmbiSpeakerLayoutPreset>(layoutPresetForMenuIndex(static_cast<uint32_t>(std::lround(value))));
        break;
    case kModeParamId:
        p.params.decoder.mode = static_cast<s3g::AmbiSpeakerDecoderMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u));
        break;
    case kOrderParamId:
        p.params.decoder.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, s3g::kAmbiSpeakerDecoderMaxOrder);
        break;
    case kWeightingParamId:
        p.params.decoder.weighting = static_cast<s3g::AmbiSpeakerDecoderWeighting>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u));
        break;
    case kObjectMethodParamId:
        p.params.objectMethod = static_cast<s3g::AmbiObjectMethod>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u));
        break;
    case kBlendParamId:
        p.params.objectBlend = s3g::clamp(static_cast<float>(value), 0.0f, 1.0f);
        break;
    case kFieldGainParamId:
        p.params.fieldGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        break;
    case kObjectGainParamId:
        p.params.objectGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 18.0f);
        break;
    case kDirectionSmoothingParamId:
        p.params.directionSmoothingMs = s3g::clamp(static_cast<float>(value), 0.0f, 500.0f);
        break;
    case kOutputParamId:
        p.params.decoder.outputGainDb = s3g::clamp(static_cast<float>(value), -60.0f, 12.0f);
        break;
    case kObjectConfidenceParamId:
        p.params.objectConfidence = s3g::clamp(static_cast<float>(value), 0.0f, 0.95f);
        break;
    case kObjectHighpassParamId:
        p.params.objectHighpassHz = s3g::clamp(static_cast<float>(value), 0.0f, 5000.0f);
        break;
    default:
        break;
    }
    p.decoder.setParams(p.params);
    p.params = p.decoder.params();
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) {
        [static_cast<NSView*>(p->guiView) removeFromSuperview];
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
    }
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->decoder.prepare(sampleRate);
    p->decoder.setParams(p->params);
    p->params = p->decoder.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->decoder.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->objectDirX.store(1.0f, std::memory_order_relaxed);
    p->objectDirY.store(0.0f, std::memory_order_relaxed);
    p->objectDirZ.store(0.0f, std::memory_order_relaxed);
    p->objectCue.store(0.0f, std::memory_order_relaxed);
}

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

float peakForChannels(float* const* output, uint32_t channels, uint32_t frames)
{
    float peak = 0.0f;
    if (!output) return peak;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (!output[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) peak = std::max(peak, std::fabs(output[ch][frame]));
    }
    return peak;
}

void updateObjectDirectionMeter(Plugin& p, float* const* input, uint32_t channels, uint32_t frames)
{
    if (!input || channels < 4u || frames == 0u) {
        p.objectCue.store(0.0f, std::memory_order_relaxed);
        return;
    }
    double ix = 0.0;
    double iy = 0.0;
    double iz = 0.0;
    double cue = 0.0;
    const float* wIn = input[0];
    const float* yIn = input[1];
    const float* zIn = input[2];
    const float* xIn = input[3];
    if (!wIn || !xIn || !yIn || !zIn) {
        p.objectCue.store(0.0f, std::memory_order_relaxed);
        return;
    }
    const double cutoff = p.params.objectHighpassHz;
    const double hpAlpha = cutoff <= 1.0 ? 1.0 : (1.0 - std::exp(-2.0 * s3g::kPi * cutoff / std::max(1.0, p.sampleRate)));
    double lpW = 0.0;
    double lpX = 0.0;
    double lpY = 0.0;
    double lpZ = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        auto hp = [&](double x, double& lp) {
            if (cutoff <= 1.0) return x;
            lp += hpAlpha * (x - lp);
            return x - lp;
        };
        const double w = hp(wIn[i], lpW);
        const double x = hp(xIn[i], lpX);
        const double y = hp(yIn[i], lpY);
        const double z = hp(zIn[i], lpZ);
        ix += w * x;
        iy += w * y;
        iz += w * z;
        const double directional = std::sqrt(x * x + y * y + z * z);
        const double confidence = directional / std::max(0.000001, directional + 0.70710678 * std::abs(w));
        const double threshold = p.params.objectConfidence;
        const double t = std::clamp((confidence - threshold) / std::max(0.000001, 1.0 - threshold), 0.0, 1.0);
        const double gate = t * t * (3.0 - 2.0 * t);
        cue += std::abs(w) * gate;
    }
    const double mag = std::sqrt(ix * ix + iy * iy + iz * iz);
    if (mag > 0.000000001) {
        p.objectDirX.store(static_cast<float>(ix / mag), std::memory_order_relaxed);
        p.objectDirY.store(static_cast<float>(iy / mag), std::memory_order_relaxed);
        p.objectDirZ.store(static_cast<float>(iz / mag), std::memory_order_relaxed);
    }
    p.objectCue.store(static_cast<float>(std::clamp(cue / static_cast<double>(frames), 0.0, 1.0)), std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t inChannels = std::min<uint32_t>(input.channel_count, kInputChannels);
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (!input.data32 || !output.data32 || outChannels == 0u) {
        if (output.data32) s3g::clearAudioBufferFromChannel(output, 0, frames);
        return CLAP_PROCESS_CONTINUE;
    }
    updateObjectDirectionMeter(*p, input.data32, inChannels, frames);
    p->decoder.setParams(p->params);
    p->decoder.processBlock(input.data32, output.data32, inChannels, outChannels, frames);
    s3g::clearAudioBufferFromChannel(output, std::min<uint32_t>(outChannels, p->params.decoder.activeSpeakers), frames);
    const float peak = peakForChannels(output.data32, std::min<uint32_t>(outChannels, p->params.decoder.activeSpeakers), frames);
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
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
constexpr ParamDef kParams[] {
    { kLayoutParamId, "Layout", 0.0, static_cast<double>(kLayoutMenuCount - 1u), 13.0, true },
    { kModeParamId, "Field Mode", 0.0, 2.0, 1.0, true },
    { kOrderParamId, "Order", 1.0, static_cast<double>(s3g::kAmbiSpeakerDecoderMaxOrder), 3.0, true },
    { kWeightingParamId, "Weighting", 0.0, 2.0, 1.0, true },
    { kObjectMethodParamId, "Object Method", 0.0, 2.0, 0.0, true },
    { kBlendParamId, "Object Blend", 0.0, 1.0, 0.35, false },
    { kFieldGainParamId, "Field Gain", -60.0, 18.0, 0.0, false },
    { kObjectGainParamId, "Object Gain", -60.0, 18.0, 0.0, false },
    { kDirectionSmoothingParamId, "Direction Smooth", 0.0, 500.0, 25.0, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kObjectConfidenceParamId, "Object Confidence", 0.0, 0.95, 0.18, false },
    { kObjectHighpassParamId, "Object Crossover", 0.0, 5000.0, 250.0, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Object Decoder", sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto p = self(plugin)->params;
    switch (id) {
    case kLayoutParamId: *value = menuIndexForLayoutPreset(static_cast<uint32_t>(p.decoder.layout)); return true;
    case kModeParamId: *value = static_cast<uint32_t>(p.decoder.mode); return true;
    case kOrderParamId: *value = p.decoder.order; return true;
    case kWeightingParamId: *value = static_cast<uint32_t>(p.decoder.weighting); return true;
    case kObjectMethodParamId: *value = static_cast<uint32_t>(p.objectMethod); return true;
    case kBlendParamId: *value = p.objectBlend; return true;
    case kFieldGainParamId: *value = p.fieldGainDb; return true;
    case kObjectGainParamId: *value = p.objectGainDb; return true;
    case kDirectionSmoothingParamId: *value = p.directionSmoothingMs; return true;
    case kOutputParamId: *value = p.decoder.outputGainDb; return true;
    case kObjectConfidenceParamId: *value = p.objectConfidence; return true;
    case kObjectHighpassParamId: *value = p.objectHighpassHz; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kLayoutParamId) std::snprintf(display, size, "%s", layoutName(layoutPresetForMenuIndex(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kModeParamId) std::snprintf(display, size, "%s", modeName(static_cast<s3g::AmbiSpeakerDecoderMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u))));
    else if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kWeightingParamId) std::snprintf(display, size, "%s", weightingName(static_cast<s3g::AmbiSpeakerDecoderWeighting>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u))));
    else if (id == kObjectMethodParamId) std::snprintf(display, size, "%s", s3g::ambiObjectMethodName(static_cast<s3g::AmbiObjectMethod>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u))));
    else if (id == kFieldGainParamId || id == kObjectGainParamId || id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kDirectionSmoothingParamId) std::snprintf(display, size, "%.1f ms", value);
    else if (id == kObjectConfidenceParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else if (id == kObjectHighpassParamId) std::snprintf(display, size, "%.0f Hz", value);
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
    SavedState state { kStateVersion, p->params };
#if defined(__APPLE__)
    state.guiViewMode = p->guiViewMode;
    state.guiViewAzDeg = p->guiViewAzDeg;
    state.guiViewElDeg = p->guiViewElDeg;
    state.guiViewZoom = p->guiViewZoom;
#endif
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    if (version == kStateVersion) {
        SavedState state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        p->params = state.params;
#if defined(__APPLE__)
        p->guiViewMode = std::clamp<int>(state.guiViewMode, 0, 2);
        p->guiViewAzDeg = std::clamp(state.guiViewAzDeg, -180.0, 180.0);
        p->guiViewElDeg = std::clamp(state.guiViewElDeg, -90.0, 90.0);
        p->guiViewZoom = std::clamp(state.guiViewZoom, 0.55, 2.20);
#endif
    } else if (version == 2u) {
        SavedStateV2 state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        p->params = upgradeLegacyParams(state.params);
#if defined(__APPLE__)
        p->guiViewMode = std::clamp<int>(state.guiViewMode, 0, 2);
        p->guiViewAzDeg = std::clamp(state.guiViewAzDeg, -180.0, 180.0);
        p->guiViewElDeg = std::clamp(state.guiViewElDeg, -90.0, 90.0);
        p->guiViewZoom = std::clamp(state.guiViewZoom, 0.55, 2.20);
#endif
    } else if (version == 1u) {
        SavedStateV1 state {};
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        p->params = upgradeLegacyParams(state.params);
    } else {
        return false;
    }
    p->decoder.setParams(p->params);
    p->params = p->decoder.params();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
@interface S3GAmbiObjectDecoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    int _dragParam;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    NSPoint _lastDragPoint;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)storeViewState;
- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect;
- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect;
- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs;
- (void)setViewPreset:(int)mode;
- (CGFloat)viewScaleForRect:(NSRect)rect;
- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect depth:(CGFloat*)depth;
- (void)addPolyhedronShellToPath:(NSBezierPath*)links dodecaShell:(BOOL)dodecaShell rect:(NSRect)rect;
@end

static NSString* objectDecoderMethodItem(uint32_t index)
{
    return [NSString stringWithUTF8String:s3g::ambiObjectMethodName(static_cast<s3g::AmbiObjectMethod>(std::min<uint32_t>(index, 2u)))];
}

static NSColor* odColor(int rgb, double alpha = 1.0) { return s3g::clap_gui::color(rgb, alpha); }

static float odLinearToSrgb(float v)
{
    const float x = std::clamp(v, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

static NSColor* odSpeakerColorFromAed(float azDeg, float elDeg, float distance)
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
    float r = odLinearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float g = odLinearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float bl = odLinearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    constexpr float grayMix = 0.18f;
    r = r * (1.0f - grayMix) + 0.74f * grayMix;
    g = g * (1.0f - grayMix) + 0.74f * grayMix;
    bl = bl * (1.0f - grayMix) + 0.74f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:0.88];
}

@implementation S3GAmbiObjectDecoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 620)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _dragParam = 0;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 35.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 34.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = _viewAzDeg;
    _plugin->guiViewElDeg = _viewElDeg;
    _plugin->guiViewZoom = _viewZoom;
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 24.0
                                             target:self
                                           selector:@selector(timerTick:)
                                           userInfo:nil
                                            repeats:YES];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)timerTick:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (NSPoint)speakerPointForAzimuth:(float)azimuth elevation:(float)elevation inRect:(NSRect)rect
{
    const double az = static_cast<double>(azimuth) * M_PI / 180.0;
    const double elevScale = 0.78 + 0.22 * std::cos(static_cast<double>(elevation) * M_PI / 180.0);
    const double r = std::min(rect.size.width, rect.size.height) * 0.43 * elevScale;
    const double cx = NSMidX(rect);
    const double cy = NSMidY(rect);
    return NSMakePoint(cx - std::sin(az) * r, cy - std::cos(az) * r);
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
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (CGFloat)viewScaleForRect:(NSRect)rect
{
    CGFloat layoutScale = 1.0;
    const auto layout = _plugin ? _plugin->decoder.params().decoder.layout : s3g::AmbiSpeakerLayoutPreset::Sphere24;
    if (_viewMode != 0 && _viewMode != 1
        && (layout == s3g::AmbiSpeakerLayoutPreset::Cube8 || layout == s3g::AmbiSpeakerLayoutPreset::Cube17)) {
        layoutScale = 0.82;
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

- (void)addPolyhedronShellToPath:(NSBezierPath*)links dodecaShell:(BOOL)dodecaShell rect:(NSRect)rect
{
    if (!links) return;
    constexpr float phi = 1.61803398875f;
    constexpr float invPhi = 1.0f / phi;
    std::array<s3g::Vec3, 20> verts {};
    const uint32_t count = dodecaShell ? 20u : 12u;
    if (dodecaShell) {
        const float pts[20][3] {
            { 1, 1, 1 }, { 1, 1, -1 }, { 1, -1, 1 }, { 1, -1, -1 },
            { -1, 1, 1 }, { -1, 1, -1 }, { -1, -1, 1 }, { -1, -1, -1 },
            { 0, invPhi, phi }, { 0, invPhi, -phi }, { 0, -invPhi, phi }, { 0, -invPhi, -phi },
            { invPhi, phi, 0 }, { invPhi, -phi, 0 }, { -invPhi, phi, 0 }, { -invPhi, -phi, 0 },
            { phi, 0, invPhi }, { phi, 0, -invPhi }, { -phi, 0, invPhi }, { -phi, 0, -invPhi },
        };
        for (uint32_t i = 0; i < count; ++i) verts[i] = { pts[i][0], pts[i][1], pts[i][2] };
    } else {
        const float pts[12][3] {
            { 0, 1, phi }, { 0, -1, phi }, { 0, 1, -phi }, { 0, -1, -phi },
            { 1, phi, 0 }, { -1, phi, 0 }, { 1, -phi, 0 }, { -1, -phi, 0 },
            { phi, 0, 1 }, { -phi, 0, 1 }, { phi, 0, -1 }, { -phi, 0, -1 },
        };
        for (uint32_t i = 0; i < count; ++i) verts[i] = { pts[i][0], pts[i][1], pts[i][2] };
    }

    std::array<NSPoint, 20> points {};
    for (uint32_t i = 0; i < count; ++i) {
        const float d = std::sqrt(verts[i].x * verts[i].x + verts[i].y * verts[i].y + verts[i].z * verts[i].z);
        if (d > 0.000001f) {
            verts[i].x /= d;
            verts[i].y /= d;
            verts[i].z /= d;
        }
        points[i] = [self projectWorldPoint:verts[i] rect:rect depth:nil];
    }

    float minD2 = 999999.0f;
    for (uint32_t a = 0; a < count; ++a) {
        for (uint32_t b = a + 1u; b < count; ++b) {
            const float dx = verts[a].x - verts[b].x;
            const float dy = verts[a].y - verts[b].y;
            const float dz = verts[a].z - verts[b].z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > 0.0001f) minD2 = std::min(minD2, d2);
        }
    }
    const float maxD2 = minD2 * 1.08f;
    for (uint32_t a = 0; a < count; ++a) {
        for (uint32_t b = a + 1u; b < count; ++b) {
            const float dx = verts[a].x - verts[b].x;
            const float dy = verts[a].y - verts[b].y;
            const float dz = verts[a].z - verts[b].z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= maxD2) {
                [links moveToPoint:points[a]];
                [links lineToPoint:points[b]];
            }
        }
    }
}

- (void)drawLayout:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [odColor(0x111111) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    const auto& params = _plugin->params;
    const auto& speakers = _plugin->decoder.fieldDecoder().speakers();
    const uint32_t count = std::min<uint32_t>(params.decoder.activeSpeakers, s3g::kAmbiSpeakerDecoderMaxSpeakers);
    NSMutableDictionary* labelAttrs = [attrs mutableCopy];
    labelAttrs[NSForegroundColorAttributeName] = style.text;

    std::array<NSPoint, s3g::kAmbiSpeakerDecoderMaxSpeakers> points {};
    for (uint32_t i = 0; i < count; ++i) {
        const auto& sp = speakers[i];
        const s3g::Vec3 dir = s3g::directionFromAed(sp.azimuthDeg, sp.elevationDeg);
        const s3g::Vec3 world { dir.x * sp.distance, dir.y * sp.distance, dir.z * sp.distance };
        points[i] = [self projectWorldPoint:world rect:rect depth:nil];
        points[i].x = std::round(points[i].x);
        points[i].y = std::round(points[i].y);
    }

    [odColor(0x777777, 0.72) setStroke];
    NSBezierPath* links = [NSBezierPath bezierPath];
    auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= count || b >= count) return;
        [links moveToPoint:points[a]];
        [links lineToPoint:points[b]];
    };
    auto ring = [&](uint32_t base, uint32_t n) {
        if (n < 2u) return;
        for (uint32_t i = 0; i < n; ++i) edge(base + i, base + ((i + 1u) % n));
    };
    const auto layout = params.decoder.layout;
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
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Cube41 || layout == s3g::AmbiSpeakerLayoutPreset::Lpac41) {
        ring(0, 16);
        ring(16, 12);
        ring(28, 8);
        ring(36, 4);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dome24 || layout == s3g::AmbiSpeakerLayoutPreset::Dome25 || layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
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
        if (layout == s3g::AmbiSpeakerLayoutPreset::Dome25 || layout == s3g::AmbiSpeakerLayoutPreset::Srst25) {
            for (uint32_t i = 0; i < 4; ++i) edge(24, 20u + i);
        }
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::QuadOverhead6) {
        ring(0, 4);
        edge(4, 0); edge(4, 3);
        edge(5, 1); edge(5, 2);
        edge(4, 5);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::OctophonicRing) {
        ring(0, 8);
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Dodeca12) {
        [self addPolyhedronShellToPath:links dodecaShell:YES rect:rect];
    } else if (layout == s3g::AmbiSpeakerLayoutPreset::Icosahedron20) {
        [self addPolyhedronShellToPath:links dodecaShell:NO rect:rect];
    }
    [links setLineWidth:1.0];
    [links stroke];

    for (uint32_t i = 0; i < count; ++i) {
        const NSPoint p = points[i];
        const auto& sp = speakers[i];
        const CGFloat r = 4.8;
        [odSpeakerColorFromAed(sp.azimuthDeg, sp.elevationDeg, sp.distance) setFill];
        NSRectFill(NSMakeRect(p.x - r, p.y - r, r * 2.0, r * 2.0));
        [odColor(0x050505, 0.90) setStroke];
        NSFrameRect(NSMakeRect(p.x - r, p.y - r, r * 2.0, r * 2.0));
        NSString* number = [NSString stringWithFormat:@"%u", i + 1u];
        NSDictionary* idAttrs = @{ NSForegroundColorAttributeName:odColor(0x151515),
                                   NSFontAttributeName:s3g::clap_gui::uiFont(7.5) };
        NSSize labelSize = [number sizeWithAttributes:idAttrs];
        [number drawAtPoint:NSMakePoint(p.x - labelSize.width * 0.5,
                                       p.y - labelSize.height * 0.5 - 0.5)
            withAttributes:idAttrs];
    }

    const CGFloat blend = std::clamp<CGFloat>(params.objectBlend, 0.0, 1.0);
    const CGFloat gainNorm = std::clamp<CGFloat>((params.objectGainDb + 60.0f) / 78.0f, 0.0, 1.0);
    const CGFloat cue = std::clamp<CGFloat>(_plugin->objectCue.load(std::memory_order_relaxed) * 8.0, 0.0, 1.0);
    s3g::Vec3 objectDir {
        _plugin->objectDirX.load(std::memory_order_relaxed),
        _plugin->objectDirY.load(std::memory_order_relaxed),
        _plugin->objectDirZ.load(std::memory_order_relaxed),
    };
    objectDir = s3g::normalize(objectDir);
    const NSPoint objectPt = [self projectWorldPoint:objectDir rect:rect depth:nil];
    const CGFloat alpha = std::clamp<CGFloat>(0.12 + blend * 0.72, 0.0, 0.92) * cue;
    const CGFloat objectRing = 8.0 + 8.0 * gainNorm;
    [odColor(0xf0d35d, 0.12 * cue + 0.24 * blend * gainNorm) setFill];
    NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(objectPt.x - objectRing, objectPt.y - objectRing, objectRing * 2.0, objectRing * 2.0)];
    [halo fill];
    [odColor(0xf0d35d, alpha) setFill];
    const CGFloat r = 6.5;
    NSRectFill(NSMakeRect(objectPt.x - r, objectPt.y - r, r * 2.0, r * 2.0));
    [odColor(0x151515, 0.95) setStroke];
    NSFrameRect(NSMakeRect(objectPt.x - r, objectPt.y - r, r * 2.0, r * 2.0));
    [odColor(0xf0d35d, std::clamp<CGFloat>(0.20 + gainNorm * 0.60, 0.0, 0.92) * cue) setStroke];
    NSFrameRect(NSMakeRect(objectPt.x - objectRing, objectPt.y - objectRing, objectRing * 2.0, objectRing * 2.0));
    NSDictionary* objAttrs = @{ NSForegroundColorAttributeName:odColor(0x111111, alpha),
                                NSFontAttributeName:s3g::clap_gui::uiFont(7.5) };
    [@"O" drawAtPoint:NSMakePoint(objectPt.x - 2.8, objectPt.y - 5.0) withAttributes:objAttrs];

    [labelAttrs release];

    NSString* viewText = _viewMode == 0 ? @"TOP VIEW   0 front/top  -90 right  +90 left"
        : (_viewMode == 1 ? @"SIDE VIEW   +90 elevation up" : @"3/4 VIEW   drag blank space to rotate");
    [viewText drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + rect.size.height - 23) withAttributes:attrs];
}

- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, s3g::clap_gui::softValueAttrs(), style, 642, 738, 102);
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0) return;
    std::array<NSString*, kLayoutMenuCount> layoutItems {};
    for (uint32_t i = 0; i < kLayoutMenuCount; ++i) layoutItems[i] = [NSString stringWithUTF8String:layoutName(layoutPresetForMenuIndex(i))];
    NSString* modeItems[] = { @"BASIC", @"EPAD", @"MMD" };
    NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    NSString* weightItems[] = { @"NONE", @"MAXRE", @"INPHASE" };
    NSString* methodItems[] = { objectDecoderMethodItem(0), objectDecoderMethodItem(1), objectDecoderMethodItem(2) };
    NSString** items = layoutItems.data();
    int selected = 0;
    CGFloat y = 96.0;
    CGFloat w = _openMenu == 1 ? 150.0 : 124.0;
    if (_openMenu == 1) {
        items = layoutItems.data();
        selected = static_cast<int>(menuIndexForLayoutPreset(static_cast<uint32_t>(_plugin->params.decoder.layout)));
        _menuItemCount = kLayoutMenuCount;
    } else if (_openMenu == 2) {
        items = modeItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.decoder.mode));
        _menuItemCount = 3;
        y = 122.0;
    } else if (_openMenu == 3) {
        items = orderItems;
        selected = static_cast<int>(std::clamp<uint32_t>(_plugin->params.decoder.order, 1u, s3g::kAmbiSpeakerDecoderMaxOrder) - 1u);
        _menuItemCount = s3g::kAmbiSpeakerDecoderMaxOrder;
        y = 148.0;
    } else if (_openMenu == 4) {
        items = weightItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.decoder.weighting));
        _menuItemCount = 3;
        y = 174.0;
    } else if (_openMenu == 5) {
        items = methodItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.objectMethod));
        _menuItemCount = 3;
        y = 376.0;
    }
    const CGFloat itemH = 18.0;
    NSRect menuRect = NSMakeRect(738, y, w, itemH * _menuItemCount);
    s3g::clap_gui::drawDropdownMenu(menuRect, itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawSlider:(NSString*)name value:(double)value min:(double)min max:(double)max y:(CGFloat)y suffix:(NSString*)suffix attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const double norm = (value - min) / std::max(0.000001, max - min);
    NSString* text = nil;
    if (suffix && [suffix isEqualToString:@"OA"]) text = [NSString stringWithFormat:@"%.0fOA", value];
    else if (suffix && [suffix isEqualToString:@"ms"]) text = [NSString stringWithFormat:@"%.0fms", value];
    else if (suffix && [suffix isEqualToString:@"Hz"]) text = [NSString stringWithFormat:@"%.0fHz", value];
    else if (suffix && [suffix isEqualToString:@"%"]) text = [NSString stringWithFormat:@"%.0f%%", value * 100.0];
    else if (suffix && [suffix isEqualToString:@"dB"]) text = [NSString stringWithFormat:@"%+.1f", value];
    else text = [NSString stringWithFormat:@"%.2f", value];
    s3g::clap_gui::drawSlider(name, text, norm, y, attrs, s3g::clap_gui::softValueAttrs(), style, 642, 738, 826, 82);
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* dimAttrs = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();

    [@"s3g AMBI OBJECT DECODER" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    const float peak = _plugin->outputPeak.load(std::memory_order_relaxed);
    [s3g::clap_gui::peakDbText(peak)
        drawAtPoint:NSMakePoint(728, 14)
        withAttributes:dimAttrs];
    [@"64CH" drawAtPoint:NSMakePoint(838, 14) withAttributes:dimAttrs];

    const NSRect fieldPanel = NSMakeRect(18, 42, 596, 556);
    const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SPEAKER FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, attrs, style);
    [self drawZoomButtonsInRect:fieldPanel attrs:dimAttrs];
    [self drawViewButtonsInRect:fieldPanel attrs:dimAttrs];
    [self drawLayout:fieldRect attrs:dimAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 42, 250, 268, style);
    s3g::clap_gui::drawPanelHeader(@"DECODER", true, 630, 42, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(630, 322, 250, 276, style);
    s3g::clap_gui::drawPanelHeader(@"OBJECT", true, 630, 322, 250, 21, attrs, style);

    const auto params = _plugin->params;
    [self drawMenu:@"LAYOUT" value:[NSString stringWithUTF8String:layoutName(static_cast<uint32_t>(params.decoder.layout))] y:78 attrs:attrs style:style];
    [self drawMenu:@"FIELD" value:[NSString stringWithUTF8String:modeName(params.decoder.mode)] y:104 attrs:attrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", params.decoder.order] y:130 attrs:attrs style:style];
    [self drawMenu:@"WGT" value:[NSString stringWithUTF8String:weightingName(params.decoder.weighting)] y:156 attrs:attrs style:style];
    [self drawMenu:@"OBJECT" value:[NSString stringWithUTF8String:s3g::ambiObjectMethodName(params.objectMethod)] y:358 attrs:attrs style:style];
    [self drawSlider:@"BLEND" value:params.objectBlend min:0.0 max:1.0 y:384 suffix:nil attrs:attrs style:style];
    [self drawSlider:@"FIELD" value:params.fieldGainDb min:-60.0 max:18.0 y:410 suffix:@"dB" attrs:attrs style:style];
    [self drawSlider:@"OBJ" value:params.objectGainDb min:-60.0 max:18.0 y:436 suffix:@"dB" attrs:attrs style:style];
    [self drawSlider:@"CONF" value:params.objectConfidence min:0.0 max:0.95 y:462 suffix:@"%" attrs:attrs style:style];
    [self drawSlider:@"XOVR" value:params.objectHighpassHz min:0.0 max:5000.0 y:488 suffix:@"Hz" attrs:attrs style:style];
    [self drawSlider:@"SMTH" value:params.directionSmoothingMs min:0.0 max:500.0 y:514 suffix:@"ms" attrs:attrs style:style];
    [self drawSlider:@"OUT" value:params.decoder.outputGainDb min:-60.0 max:12.0 y:540 suffix:@"dB" attrs:attrs style:style];

    [self drawOpenMenu:attrs style:style];
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)pt
{
    auto slider = [&](double min, double max) {
        const double norm = std::clamp((static_cast<double>(pt.x) - 738.0) / 82.0, 0.0, 1.0);
        applyParam(*_plugin, param, min + norm * (max - min));
    };
    switch (param) {
    case kBlendParamId: slider(0.0, 1.0); break;
    case kFieldGainParamId: slider(-60.0, 18.0); break;
    case kObjectGainParamId: slider(-60.0, 18.0); break;
    case kObjectConfidenceParamId: slider(0.0, 0.95); break;
    case kObjectHighpassParamId: slider(0.0, 5000.0); break;
    case kDirectionSmoothingParamId: slider(0.0, 500.0); break;
    case kOutputParamId: slider(-60.0, 12.0); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        CGFloat y = _openMenu == 1 ? 96.0 : (_openMenu == 2 ? 122.0 : (_openMenu == 3 ? 148.0 : (_openMenu == 4 ? 174.0 : 376.0)));
        const CGFloat itemH = 18.0;
        const CGFloat menuW = _openMenu == 1 ? 150.0 : 124.0;
        NSRect menuRect = NSMakeRect(738, y, menuW, itemH * _menuItemCount);
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, menuRect, itemH, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kLayoutParamId, hit);
            else if (_openMenu == 2) applyParam(*_plugin, kModeParamId, hit);
            else if (_openMenu == 3) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 4) applyParam(*_plugin, kWeightingParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kObjectMethodParamId, hit);
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    auto openMenu = [&](int menu, uint32_t count) {
        _openMenu = menu;
        _menuItemCount = count;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
    };
    const NSRect fieldPanel = NSMakeRect(18, 42, 596, 556);
    const NSRect fieldRect = NSMakeRect(34, 76, 564, 506);
    if (NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:fieldPanel])) {
                const CGFloat step = i == 0 ? -0.15 : 0.15;
                _viewZoom = std::clamp(_viewZoom + step, 0.55, 2.20);
                [self storeViewState];
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
        if (NSPointInRect(pt, fieldRect)) {
            _dragView = YES;
            _lastDragPoint = pt;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(pt, NSMakeRect(738, 77, 102, 17))) { openMenu(1, kLayoutMenuCount); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 103, 102, 17))) { openMenu(2, 3); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 129, 102, 17))) { openMenu(3, s3g::kAmbiSpeakerDecoderMaxOrder); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 155, 102, 17))) { openMenu(4, 3); return; }
    if (NSPointInRect(pt, NSMakeRect(738, 357, 102, 17))) { openMenu(5, 3); return; }

    _dragParam = 0;
    if (NSPointInRect(pt, NSMakeRect(638, 376, 230, 24))) _dragParam = kBlendParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 402, 230, 24))) _dragParam = kFieldGainParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 428, 230, 24))) _dragParam = kObjectGainParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 454, 230, 24))) _dragParam = kObjectConfidenceParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 480, 230, 24))) _dragParam = kObjectHighpassParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 506, 230, 24))) _dragParam = kDirectionSmoothingParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 532, 230, 24))) _dragParam = kOutputParamId;
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = pt;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (!_dragParam) return;
    [self setParam:static_cast<clap_id>(_dragParam) fromPoint:pt];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = 0;
    _dragView = NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    CGFloat y = _openMenu == 1 ? 96.0 : (_openMenu == 2 ? 122.0 : (_openMenu == 3 ? 148.0 : (_openMenu == 4 ? 174.0 : 376.0)));
    const CGFloat menuW = _openMenu == 1 ? 150.0 : 124.0;
    const int next = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738, y, menuW, 18.0 * _menuItemCount), 18.0, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiObjectDecoderView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    [static_cast<S3GAmbiObjectDecoderView*>(p->guiView) stopRefreshTimer];
    [static_cast<NSView*>(p->guiView) removeFromSuperview];
    [static_cast<NSView*>(p->guiView) release];
    p->guiView = nullptr;
    p->guiVisible = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = 900; *h = 620; return true; }
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
    [v setFrame:NSMakeRect(0, 0, 900, 620)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = true; [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GAmbiObjectDecoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiObjectDecoderView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-object-decoder-64",
    "s3g Ambi Object Decoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0-pre",
    "Hybrid ambisonic speaker decoder with a directional object panning path blended into the decoded field.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->decoder.setParams(p->params);
    p->params = p->decoder.params();
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
    p->plugin.get_extension = getExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
