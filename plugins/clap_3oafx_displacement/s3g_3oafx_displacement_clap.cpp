#include "s3g_3oafx_displacement.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

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
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace {

constexpr uint32_t kChannels = s3g::k3OaChannels;
constexpr uint32_t kStateMagic = 0x53334450u;
constexpr uint32_t kStateVersion = 1u;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.3oafx-displacement";
constexpr const char* kPluginName = "s3g 3OAFX Displacement";

enum class TransportMode : uint32_t {
    Sync = 0,
    Free = 1,
    Scrub = 2,
};

enum ParamId : clap_id {
    kParamTransport = 1,
    kParamPlayback = 2,
    kParamPosition = 3,
    kParamRate = 4,
    kParamLength = 5,
    kParamAmount = 6,
    kParamAzimuthScale = 7,
    kParamElevationScale = 8,
    kParamRadiusScale = 9,
    kParamDistanceMode = 10,
    kParamReferenceMeters = 11,
    kParamEnergy = 12,
    kParamOutput = 13,
    kParamBypass = 14,
};

struct PluginParams {
    s3g::ThreeOafxDisplacementParams dsp {};
    TransportMode transport = TransportMode::Free;
    s3g::ThreeOafxDisplacementPlaybackMode playback = s3g::ThreeOafxDisplacementPlaybackMode::Palindrome;
    float position = 0.0f;
    float rateHz = 0.05f;
    float lengthBeats = 16.0f;
};

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    PluginParams params {};
    s3g::ThreeOafxDisplacementScore score = s3g::makeDefaultThreeOafxDisplacementScore();
    double freePhase = 0.0;
    char scoreName[128] { "DEFAULT / IDENTITY" };
    int32_t guiViewMode = 2;
    double guiViewAzimuthDeg = 38.0;
    double guiViewElevationDeg = 28.0;
    double guiViewZoom = 1.0;
};

static_assert(std::is_trivially_copyable<SavedState>::value, "CLAP state must remain fixed and trivially copyable");

struct Runtime {
    s3g::ThreeOafxDisplacement processor;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    PluginParams params {};
    s3g::ThreeOafxDisplacementScore score = s3g::makeDefaultThreeOafxDisplacementScore();
    std::string scoreName = "DEFAULT / IDENTITY";
    std::string status = "LOAD A DISPLACEMENT SCORE";
    std::mutex stateMutex;
    std::vector<std::unique_ptr<Runtime>> runtimes;
    std::atomic<Runtime*> activeRuntime { nullptr };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> guiPhase { 0.0f };
    double sampleRate = 48000.0;
    double freePhase = 0.0;
    bool active = false;
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    int guiViewMode = 2;
    double guiViewAzimuthDeg = 38.0;
    double guiViewElevationDeg = 28.0;
    double guiViewZoom = 1.0;
#endif
};

struct ParamDef {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
    bool bypass;
};

constexpr ParamDef kParamDefs[] {
    { kParamTransport, "Clock", 0.0, 2.0, 1.0, true, false },
    { kParamPlayback, "Playback", 0.0, 1.0, 1.0, true, false },
    { kParamPosition, "Position", 0.0, 1.0, 0.0, false, false },
    { kParamRate, "Rate", 0.001, 2.0, 0.05, false, false },
    { kParamLength, "Sync Division", 0.25, 128.0, 16.0, false, false },
    { kParamAmount, "Amount", 0.0, 1.0, 0.65, false, false },
    { kParamAzimuthScale, "Azimuth Scale", 0.0, 2.0, 1.0, false, false },
    { kParamElevationScale, "Elevation Scale", 0.0, 2.0, 1.0, false, false },
    { kParamRadiusScale, "Radius Scale", 0.0, 2.0, 1.0, false, false },
    { kParamDistanceMode, "Distance", 0.0, 1.0, 0.0, true, false },
    { kParamReferenceMeters, "Reference Metres", 0.5, 10.0, 2.0, false, false },
    { kParamEnergy, "Energy", 0.0, 1.0, 0.65, false, false },
    { kParamOutput, "Output", -60.0, 12.0, 0.0, false, false },
    { kParamBypass, "Bypass", 0.0, 1.0, 0.0, true, true },
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

PluginParams sanitizeParams(PluginParams params)
{
    params.dsp = s3g::sanitizeThreeOafxDisplacementParams(params.dsp);
    params.transport = static_cast<TransportMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.transport), 2u));
    params.playback = static_cast<s3g::ThreeOafxDisplacementPlaybackMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.playback), 1u));
    params.position = s3g::clamp(params.position, 0.0f, 1.0f);
    params.rateHz = s3g::clamp(params.rateHz, 0.001f, 2.0f);
    params.lengthBeats = s3g::clamp(params.lengthBeats, 0.25f, 128.0f);
    return params;
}

const char* transportName(uint32_t value)
{
    static constexpr const char* names[] { "SYNC", "FREE", "SCRUB" };
    return names[std::min<uint32_t>(value, 2u)];
}

const char* playbackName(uint32_t value)
{
    static constexpr const char* names[] { "LOOP", "PALINDROME" };
    return names[std::min<uint32_t>(value, 1u)];
}

const char* distanceName(uint32_t value)
{
    static constexpr const char* names[] { "GAIN", "PHYSICAL" };
    return names[std::min<uint32_t>(value, 1u)];
}

bool streamWriteAll(const clap_ostream_t* stream, const void* source, uint64_t bytes)
{
    const auto* data = static_cast<const uint8_t*>(source);
    uint64_t position = 0u;
    while (position < bytes) {
        const int64_t wrote = stream->write(stream, data + position, bytes - position);
        if (wrote <= 0) return false;
        position += static_cast<uint64_t>(wrote);
    }
    return true;
}

bool streamReadAll(const clap_istream_t* stream, void* destination, uint64_t bytes)
{
    auto* data = static_cast<uint8_t*>(destination);
    uint64_t position = 0u;
    while (position < bytes) {
        const int64_t got = stream->read(stream, data + position, bytes - position);
        if (got <= 0) return false;
        position += static_cast<uint64_t>(got);
    }
    return true;
}

Runtime* installRuntime(Plugin& plugin)
{
    auto runtime = std::make_unique<Runtime>();
    runtime->processor.prepare(plugin.sampleRate);
    runtime->processor.setScore(plugin.score);
    runtime->processor.setParams(plugin.params.dsp);
    runtime->processor.reset();
    Runtime* result = runtime.get();
    plugin.runtimes.push_back(std::move(runtime));
    plugin.activeRuntime.store(result, std::memory_order_release);
    return result;
}

void notifyParamValuesChanged(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->rescan) {
        plugin.hostParams->rescan(plugin.host, CLAP_PARAM_RESCAN_VALUES);
    }
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kParamTransport:
        plugin.params.transport = static_cast<TransportMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u));
        break;
    case kParamPlayback:
        plugin.params.playback = static_cast<s3g::ThreeOafxDisplacementPlaybackMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u));
        break;
    case kParamPosition: plugin.params.position = static_cast<float>(value); break;
    case kParamRate: plugin.params.rateHz = static_cast<float>(value); break;
    case kParamLength: plugin.params.lengthBeats = static_cast<float>(value); break;
    case kParamAmount: plugin.params.dsp.amount = static_cast<float>(value); break;
    case kParamAzimuthScale: plugin.params.dsp.azimuthScale = static_cast<float>(value); break;
    case kParamElevationScale: plugin.params.dsp.elevationScale = static_cast<float>(value); break;
    case kParamRadiusScale: plugin.params.dsp.radiusScale = static_cast<float>(value); break;
    case kParamDistanceMode:
        plugin.params.dsp.distanceMode = static_cast<s3g::ThreeOafxDisplacementDistanceMode>(
            std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 1u));
        break;
    case kParamReferenceMeters: plugin.params.dsp.referenceDistanceMeters = static_cast<float>(value); break;
    case kParamEnergy: plugin.params.dsp.energy = static_cast<float>(value); break;
    case kParamOutput: plugin.params.dsp.outputGainDb = static_cast<float>(value); break;
    case kParamBypass: plugin.params.dsp.bypass = value >= 0.5; break;
    default: return;
    }
    plugin.params = sanitizeParams(plugin.params);
    if (plugin.params.transport == TransportMode::Scrub) {
        plugin.guiPhase.store(plugin.params.position, std::memory_order_relaxed);
    }
    if (auto* runtime = plugin.activeRuntime.load(std::memory_order_acquire)) {
        runtime->processor.setParams(plugin.params.dsp);
    }
}

double getParam(const Plugin& plugin, clap_id id)
{
    switch (id) {
    case kParamTransport: return static_cast<uint32_t>(plugin.params.transport);
    case kParamPlayback: return static_cast<uint32_t>(plugin.params.playback);
    case kParamPosition: return plugin.params.position;
    case kParamRate: return plugin.params.rateHz;
    case kParamLength: return plugin.params.lengthBeats;
    case kParamAmount: return plugin.params.dsp.amount;
    case kParamAzimuthScale: return plugin.params.dsp.azimuthScale;
    case kParamElevationScale: return plugin.params.dsp.elevationScale;
    case kParamRadiusScale: return plugin.params.dsp.radiusScale;
    case kParamDistanceMode: return static_cast<uint32_t>(plugin.params.dsp.distanceMode);
    case kParamReferenceMeters: return plugin.params.dsp.referenceDistanceMeters;
    case kParamEnergy: return plugin.params.dsp.energy;
    case kParamOutput: return plugin.params.dsp.outputGainDb;
    case kParamBypass: return plugin.params.dsp.bypass ? 1.0 : 0.0;
    default: return 0.0;
    }
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
#if defined(__APPLE__)
    if (instance && instance->guiView) {
        [static_cast<NSView*>(instance->guiView) removeFromSuperview];
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
    }
#endif
    delete instance;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* instance = self(plugin);
    instance->sampleRate = std::max(1.0, sampleRate);
    instance->runtimes.clear();
    installRuntime(*instance);
    instance->active = true;
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->activeRuntime.store(nullptr, std::memory_order_release);
    instance->runtimes.clear();
    instance->active = false;
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (auto* runtime = instance->activeRuntime.load(std::memory_order_acquire)) runtime->processor.reset();
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, param->param_id, param->value);
        }
    }
}

void playbackPosition(Plugin& plugin,
                      const clap_event_transport_t* transport,
                      uint32_t frames,
                      double& phaseStart,
                      double& phaseIncrement)
{
    phaseStart = 0.0;
    phaseIncrement = 0.0;
    switch (plugin.params.transport) {
    case TransportMode::Free:
        phaseStart = plugin.freePhase;
        phaseIncrement = static_cast<double>(plugin.params.rateHz) / plugin.sampleRate;
        plugin.freePhase += phaseIncrement * static_cast<double>(frames);
        if (std::abs(plugin.freePhase) > 1048576.0) plugin.freePhase = std::fmod(plugin.freePhase, 2.0);
        break;
    case TransportMode::Scrub:
        phaseStart = plugin.params.position;
        break;
    case TransportMode::Sync:
    default:
        if (transport && (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0) {
            const double beats = static_cast<double>(transport->song_pos_beats)
                / static_cast<double>(CLAP_BEATTIME_FACTOR);
            const double length = std::max(0.25, static_cast<double>(plugin.params.lengthBeats));
            phaseStart = beats / length;
            if ((transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0
                && (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0) {
                phaseIncrement = (transport->tempo / 60.0) / plugin.sampleRate / length;
            }
        } else {
            phaseStart = plugin.freePhase;
            phaseIncrement = static_cast<double>(plugin.params.rateHz) / plugin.sampleRate;
            plugin.freePhase += phaseIncrement * static_cast<double>(frames);
        }
        break;
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* instance = self(plugin);
    if (!process) return CLAP_PROCESS_CONTINUE;
    readParamEvents(*instance, process->in_events);
    if (process->audio_outputs_count == 0u || !process->audio_outputs) return CLAP_PROCESS_CONTINUE;

    auto& output = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t outputChannels = std::min<uint32_t>(output.channel_count, kChannels);
    if (!output.data32) {
        s3g::clearAudioBuffer(output, frames);
        return CLAP_PROCESS_CONTINUE;
    }
    const float* inputs[kChannels] {};
    float* outputs[kChannels] {};
    uint32_t inputChannels = 0u;
    if (process->audio_inputs_count > 0u && process->audio_inputs && process->audio_inputs[0].data32) {
        const auto& input = process->audio_inputs[0];
        inputChannels = std::min<uint32_t>(input.channel_count, kChannels);
        for (uint32_t channel = 0u; channel < inputChannels; ++channel) inputs[channel] = input.data32[channel];
    }
    for (uint32_t channel = 0u; channel < outputChannels; ++channel) outputs[channel] = output.data32[channel];

    double phaseStart = 0.0;
    double phaseIncrement = 0.0;
    playbackPosition(*instance, process->transport, frames, phaseStart, phaseIncrement);
    instance->guiPhase.store(
        s3g::threeOafxDisplacementPlaybackPhase(phaseStart, instance->params.playback),
        std::memory_order_relaxed);

    if (auto* runtime = instance->activeRuntime.load(std::memory_order_acquire)) {
        runtime->processor.processBlock(inputs,
                                        outputs,
                                        inputChannels,
                                        outputChannels,
                                        frames,
                                        phaseStart,
                                        phaseIncrement,
                                        instance->params.playback);
    } else {
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (!outputs[channel]) continue;
            if (channel < inputChannels && inputs[channel]) {
                std::copy(inputs[channel], inputs[channel] + frames, outputs[channel]);
            } else {
                std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
            }
        }
    }
    s3g::clearAudioBufferFromChannel(output, outputChannels, frames);

    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
        if (!outputs[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) peak = std::max(peak, std::abs(outputs[channel][frame]));
    }
    instance->outputPeak.store(
        std::max(peak, instance->outputPeak.load(std::memory_order_relaxed) * 0.92f),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "3OA ACN/SN3D %s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = isInput ? 20u : 10u;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(std::size(kParamDefs));
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(kParamDefs)) return false;
    const auto& def = kParamDefs[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u)
        | (def.bypass ? CLAP_PARAM_IS_BYPASS : 0u);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    std::snprintf(info->name, sizeof(info->name), "%s", def.name);
    std::snprintf(info->module, sizeof(info->module), "3OAFX Displacement");
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    for (const auto& def : kParamDefs) {
        if (def.id == id) {
            *value = getParam(*self(plugin), id);
            return true;
        }
    }
    return false;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    switch (id) {
    case kParamTransport:
        std::snprintf(display, size, "%s", transportName(static_cast<uint32_t>(std::lround(value))));
        break;
    case kParamPlayback:
        std::snprintf(display, size, "%s", playbackName(static_cast<uint32_t>(std::lround(value))));
        break;
    case kParamDistanceMode:
        std::snprintf(display, size, "%s", distanceName(static_cast<uint32_t>(std::lround(value))));
        break;
    case kParamBypass:
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
        break;
    case kParamPosition:
    case kParamAmount:
    case kParamEnergy:
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        break;
    case kParamRate:
        std::snprintf(display, size, "%.3f Hz", value);
        break;
    case kParamLength:
        std::snprintf(display, size, "%.2g beats", value);
        break;
    case kParamAzimuthScale:
    case kParamElevationScale:
    case kParamRadiusScale:
        std::snprintf(display, size, "%.2fx", value);
        break;
    case kParamReferenceMeters:
        std::snprintf(display, size, "%.2f m", value);
        break;
    case kParamOutput:
        std::snprintf(display, size, "%+.1f dB", value);
        break;
    default:
        std::snprintf(display, size, "%.3f", value);
        break;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kParamTransport) {
        if (std::strcmp(display, "SYNC") == 0 || std::strcmp(display, "HOST") == 0) *value = 0.0;
        else if (std::strcmp(display, "FREE") == 0) *value = 1.0;
        else if (std::strcmp(display, "SCRUB") == 0) *value = 2.0;
        else return false;
        return true;
    }
    if (id == kParamPlayback) {
        if (std::strcmp(display, "LOOP") == 0) *value = 0.0;
        else if (std::strcmp(display, "PALINDROME") == 0) *value = 1.0;
        else return false;
        return true;
    }
    if (id == kParamDistanceMode) {
        if (std::strcmp(display, "GAIN") == 0) *value = 0.0;
        else if (std::strcmp(display, "PHYSICAL") == 0) *value = 1.0;
        else return false;
        return true;
    }
    if (id == kParamBypass) {
        if (std::strcmp(display, "ON") == 0) *value = 1.0;
        else if (std::strcmp(display, "OFF") == 0) *value = 0.0;
        else return false;
        return true;
    }
    *value = std::atof(display);
    if (id == kParamPosition || id == kParamAmount || id == kParamEnergy) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush,
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* instance = self(plugin);
    SavedState state;
    std::memset(&state, 0, sizeof(state));
    state.magic = kStateMagic;
    state.version = kStateVersion;
    state.guiViewMode = 2;
    state.guiViewAzimuthDeg = 38.0;
    state.guiViewElevationDeg = 28.0;
    state.guiViewZoom = 1.0;
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        state.params = instance->params;
        state.score = instance->score;
        state.freePhase = 0.0;
        std::snprintf(state.scoreName, sizeof(state.scoreName), "%s", instance->scoreName.c_str());
    }
#if defined(__APPLE__)
    state.guiViewMode = instance->guiViewMode;
    state.guiViewAzimuthDeg = instance->guiViewAzimuthDeg;
    state.guiViewElevationDeg = instance->guiViewElevationDeg;
    state.guiViewZoom = instance->guiViewZoom;
#endif
    return streamWriteAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state;
    if (!streamReadAll(stream, &state, sizeof(state))) return false;
    if (state.magic != kStateMagic || state.version != kStateVersion) return false;
    auto* instance = self(plugin);
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        instance->params = sanitizeParams(state.params);
        instance->score = s3g::sanitizeThreeOafxDisplacementScore(state.score);
        instance->freePhase = 0.0;
        state.scoreName[sizeof(state.scoreName) - 1u] = '\0';
        instance->scoreName = state.scoreName;
        instance->status = "RESTORED WITH PROJECT";
    }
#if defined(__APPLE__)
    instance->guiViewMode = std::clamp<int32_t>(state.guiViewMode, -1, 3);
    instance->guiViewAzimuthDeg = std::clamp(state.guiViewAzimuthDeg, -180.0, 180.0);
    instance->guiViewElevationDeg = std::clamp(state.guiViewElevationDeg, -90.0, 90.0);
    instance->guiViewZoom = std::clamp(state.guiViewZoom, 0.55, 2.4);
#endif
    if (instance->active) installRuntime(*instance);
    notifyParamValuesChanged(*instance);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {

constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 610u;
constexpr NSUInteger kMaximumScoreBytes = 4u * 1024u * 1024u;

float numberValue(NSDictionary* dictionary, NSString* key, float fallback)
{
    id value = [dictionary objectForKey:key];
    return [value respondsToSelector:@selector(doubleValue)] ? static_cast<float>([value doubleValue]) : fallback;
}

uint32_t unsignedValue(NSDictionary* dictionary, NSString* key, uint32_t fallback)
{
    id value = [dictionary objectForKey:key];
    return [value respondsToSelector:@selector(unsignedIntValue)] ? [value unsignedIntValue] : fallback;
}

NSArray* arrayValue(NSDictionary* dictionary, NSString* key)
{
    id value = [dictionary objectForKey:key];
    return [value isKindOfClass:[NSArray class]] ? static_cast<NSArray*>(value) : nil;
}

NSDictionary* dictionaryValue(NSDictionary* dictionary, NSString* key)
{
    id value = [dictionary objectForKey:key];
    return [value isKindOfClass:[NSDictionary class]] ? static_cast<NSDictionary*>(value) : nil;
}

bool parsePointArray(NSArray* values,
                     const std::array<s3g::ThreeOafxDisplacementPoint, s3g::k3OafxVirtualSpeakers>& fallback,
                     std::array<s3g::ThreeOafxDisplacementPoint, s3g::k3OafxVirtualSpeakers>& output)
{
    if (![values isKindOfClass:[NSArray class]] || [values count] < s3g::k3OafxVirtualSpeakers) return false;
    output = fallback;
    std::array<bool, s3g::k3OafxVirtualSpeakers> seen {};
    const NSUInteger count = std::min<NSUInteger>([values count], s3g::k3OafxVirtualSpeakers);
    for (NSUInteger index = 0u; index < count; ++index) {
        id item = [values objectAtIndex:index];
        if (![item isKindOfClass:[NSDictionary class]]) return false;
        auto* point = static_cast<NSDictionary*>(item);
        const uint32_t channel = unsignedValue(point, @"channel", static_cast<uint32_t>(index + 1u));
        if (channel == 0u || channel > s3g::k3OafxVirtualSpeakers) return false;
        const uint32_t destination = channel - 1u;
        output[destination] = s3g::sanitizeThreeOafxDisplacementPoint({
            numberValue(point, @"azimuth", fallback[destination].azimuthDeg),
            numberValue(point, @"elevation", fallback[destination].elevationDeg),
            numberValue(point, @"radius", fallback[destination].radius),
        });
        seen[destination] = true;
    }
    return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
}

bool parseScoreData(NSData* data,
                    s3g::ThreeOafxDisplacementScore& score,
                    float& amount,
                    float& azimuthScale,
                    float& elevationScale,
                    float& radiusScale,
                    std::string& name,
                    std::string& error)
{
    if (!data || [data length] == 0u || [data length] > kMaximumScoreBytes) {
        error = "FILE SIZE IS INVALID";
        return false;
    }
    NSError* parseError = nil;
    id rootObject = [NSJSONSerialization JSONObjectWithData:data options:0 error:&parseError];
    if (parseError || ![rootObject isKindOfClass:[NSDictionary class]]) {
        error = "INVALID JSON";
        return false;
    }
    auto* root = static_cast<NSDictionary*>(rootObject);
    id format = [root objectForKey:@"format"];
    if (![format isKindOfClass:[NSString class]] || ![format isEqualToString:@"s3g-mc-displacement-score"]) {
        error = "NOT A DISPLACEMENT SCORE";
        return false;
    }
    if (unsignedValue(root, @"version", 0u) != 1u) {
        error = "UNSUPPORTED SCORE VERSION";
        return false;
    }

    auto parsed = s3g::makeDefaultThreeOafxDisplacementScore();
    parsePointArray(arrayValue(root, @"source"), parsed.source, parsed.source);
    parsed.sceneCount = 0u;
    if (NSDictionary* timeline = dictionaryValue(root, @"timeline")) {
        parsed.durationSeconds = numberValue(timeline, @"duration_seconds", 16.0f);
    }

    if (NSArray* scenes = arrayValue(root, @"scenes")) {
        const uint32_t count = std::min<uint32_t>(
            static_cast<uint32_t>([scenes count]), s3g::k3OafxDisplacementMaxScenes);
        for (uint32_t index = 0u; index < count; ++index) {
            id item = [scenes objectAtIndex:index];
            if (![item isKindOfClass:[NSDictionary class]]) continue;
            auto* scene = static_cast<NSDictionary*>(item);
            auto points = parsed.source;
            if (!parsePointArray(arrayValue(scene, @"target"), parsed.source, points)) continue;
            auto& destination = parsed.scenes[parsed.sceneCount++];
            destination.time = numberValue(scene, @"t", parsed.sceneCount > 1u
                ? static_cast<float>(parsed.sceneCount - 1u) / static_cast<float>(std::max<uint32_t>(1u, count - 1u))
                : 0.0f);
            destination.points = points;
        }
    }

    if (parsed.sceneCount == 0u) {
        parsed.sceneCount = 1u;
        parsed.scenes[0].time = 0.0f;
        parsed.scenes[0].points = parsed.source;
        std::array<s3g::ThreeOafxDisplacementPoint, s3g::k3OafxVirtualSpeakers> target {};
        if (parsePointArray(arrayValue(root, @"target"), parsed.source, target)) {
            parsed.sceneCount = 2u;
            parsed.scenes[1].time = 1.0f;
            parsed.scenes[1].points = target;
        }
    }
    if (parsed.sceneCount == 0u) {
        error = "NO VALID SCENES";
        return false;
    }
    parsed.scenes[0].time = 0.0f;
    parsed.scenes[0].points = parsed.source;
    score = s3g::sanitizeThreeOafxDisplacementScore(parsed);

    amount = numberValue(root, @"amount", 0.65f);
    if (NSDictionary* scales = dictionaryValue(root, @"scales")) {
        azimuthScale = numberValue(scales, @"azimuth", 1.0f);
        elevationScale = numberValue(scales, @"elevation", 1.0f);
        radiusScale = numberValue(scales, @"radius", 1.0f);
    } else {
        azimuthScale = elevationScale = radiusScale = 1.0f;
    }
    id scoreName = [root objectForKey:@"name"];
    if ([scoreName isKindOfClass:[NSString class]]) name = [static_cast<NSString*>(scoreName) UTF8String];
    return true;
}

void installScore(Plugin& plugin,
                  s3g::ThreeOafxDisplacementScore score,
                  float amount,
                  float azimuthScale,
                  float elevationScale,
                  float radiusScale,
                  const std::string& name)
{
    {
        std::lock_guard<std::mutex> lock(plugin.stateMutex);
        plugin.score = s3g::sanitizeThreeOafxDisplacementScore(score);
        plugin.params.dsp.amount = amount;
        plugin.params.dsp.azimuthScale = azimuthScale;
        plugin.params.dsp.elevationScale = elevationScale;
        plugin.params.dsp.radiusScale = radiusScale;
        plugin.params = sanitizeParams(plugin.params);
        plugin.scoreName = name.empty() ? "DISPLACEMENT SCORE" : name;
        plugin.status = std::to_string(plugin.score.sceneCount) + " SCENES / 24 POINTS";
    }
    if (plugin.active) installRuntime(plugin);
    notifyParamValuesChanged(plugin);
}

struct GuiProjection {
    NSPoint point {};
    float depth = 0.0f;
};

float linearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

NSColor* displacementPointColor(const s3g::ThreeOafxDisplacementPoint& point)
{
    const float hue = std::fmod(point.azimuthDeg / 360.0f + 1.0f, 1.0f);
    const float light = std::clamp((point.elevationDeg + 90.0f) / 180.0f, 0.30f, 0.84f);
    const float chroma = std::clamp(point.radius / 3.0f, 0.10f, 1.0f) * 0.34f;
    const float a = std::cos(hue * 2.0f * s3g::kPi) * chroma;
    const float b = std::sin(hue * 2.0f * s3g::kPi) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    return [NSColor colorWithCalibratedRed:linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s)
                                       green:linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s)
                                        blue:linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s)
                                       alpha:0.94f];
}

const std::vector<std::array<uint32_t, 2>>& displacementMeshEdges()
{
    static const std::vector<std::array<uint32_t, 2>> edges = [] {
        std::vector<std::array<uint32_t, 2>> result;
        for (uint32_t point = 0u; point < s3g::k3OafxVirtualSpeakers; ++point) {
            std::vector<std::pair<float, uint32_t>> neighbors;
            for (uint32_t other = 0u; other < s3g::k3OafxVirtualSpeakers; ++other) {
                if (point == other) continue;
                const auto& a = s3g::k3OafxPoints[point];
                const auto& b = s3g::k3OafxPoints[other];
                const float dx = a.x - b.x;
                const float dy = a.y - b.y;
                const float dz = a.z - b.z;
                neighbors.push_back({ dx * dx + dy * dy + dz * dz, other });
            }
            std::sort(neighbors.begin(), neighbors.end());
            for (uint32_t index = 0u; index < std::min<uint32_t>(4u, static_cast<uint32_t>(neighbors.size())); ++index) {
                const uint32_t a = std::min(point, neighbors[index].second);
                const uint32_t b = std::max(point, neighbors[index].second);
                const std::array<uint32_t, 2> edge { a, b };
                if (std::find(result.begin(), result.end(), edge) == result.end()) result.push_back(edge);
            }
        }
        return result;
    }();
    return edges;
}

} // namespace

@interface S3G3OAFXDisplacementView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
    int _openMenu;
    int _hoverMenuItem;
    int _viewMode;
    CGFloat _viewAzimuthDeg;
    CGFloat _viewElevationDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    BOOL _dragTimeline;
    NSPoint _lastDragPoint;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)storeViewState;
@end

@implementation S3G3OAFXDisplacementView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzimuthDeg = plugin ? plugin->guiViewAzimuthDeg : 38.0;
        _viewElevationDeg = plugin ? plugin->guiViewElevationDeg : 28.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _dragView = NO;
        _dragTimeline = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

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
    _plugin->guiViewAzimuthDeg = _viewAzimuthDeg;
    _plugin->guiViewElevationDeg = _viewElevationDeg;
    _plugin->guiViewZoom = _viewZoom;
}

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) [self removeTrackingArea:area];
    NSTrackingArea* area = [[[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow
        owner:self
        userInfo:nil] autorelease];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 24.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
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
    if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}

- (NSRect)fieldPanelRect { return NSMakeRect(14, 42, 620, 550); }
- (NSRect)fieldRect { return NSMakeRect(30, 76, 588, 438); }
- (NSRect)timelineRect { return NSMakeRect(38, 536, 572, 12); }
- (NSRect)loadButtonRect { return NSMakeRect(172, 45, 92, 16); }

- (NSRect)viewButtonRect:(int)index
{
    const CGFloat width = index == 3 ? 42.0 : 38.0;
    const CGFloat starts[] = { 430.0, 474.0, 518.0, 562.0 };
    return NSMakeRect(starts[index], 45, width, 16);
}

- (NSRect)zoomButtonRect:(int)index
{
    return NSMakeRect(372.0 + static_cast<CGFloat>(index) * 26.0, 45, 20, 16);
}

- (NSRect)menuRectForId:(int)menu
{
    CGFloat y = 0.0;
    uint32_t count = 0u;
    if (menu == 1) { y = 78.0; count = 3u; }
    else if (menu == 2) { y = 104.0; count = 3u; }
    else if (menu == 3) { y = 435.0; count = 2u; }
    return NSMakeRect(758, y + 16.0, 132, 19.0 * count);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) { _viewAzimuthDeg = 90.0; _viewElevationDeg = 0.0; }
    else if (mode == 1) { _viewAzimuthDeg = 90.0; _viewElevationDeg = 90.0; }
    else if (mode == 2) { _viewAzimuthDeg = 38.0; _viewElevationDeg = 28.0; }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (GuiProjection)projectPoint:(const s3g::ThreeOafxDisplacementPoint&)point inRect:(NSRect)rect
{
    if (_viewMode == 3) {
        const CGFloat x = NSMidX(rect) - static_cast<CGFloat>(point.azimuthDeg / 180.0f) * rect.size.width * 0.47;
        const CGFloat y = NSMidY(rect) - std::sin(point.elevationDeg * s3g::kPi / 180.0f) * rect.size.height * 0.44;
        return { NSMakePoint(x, y), point.radius };
    }
    const s3g::Vec3 vector = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    const float radius = std::min(1.65f, std::max(0.10f, point.radius));
    const float azimuth = static_cast<float>(_viewAzimuthDeg * s3g::kPi / 180.0);
    const float elevation = static_cast<float>(_viewElevationDeg * s3g::kPi / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = (ca * vector.x - sa * vector.y) * radius;
    const float y1 = (sa * vector.x + ca * vector.y) * radius;
    const float z1 = vector.z * radius;
    const float y2 = ce * y1 + se * z1;
    const float depth = -se * y1 + ce * z1;
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.34 * std::clamp(_viewZoom, 0.55, 2.4);
    return { NSMakePoint(NSMidX(rect) + x1 * scale, NSMidY(rect) - y2 * scale), depth };
}

- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*_plugin, param, value);
    [self setNeedsDisplay:YES];
}

- (NSString*)displayValueForParam:(clap_id)param
{
    char text[64] {};
    paramsValueToText(&_plugin->plugin, param, getParam(*_plugin, param), text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

- (void)drawSlider:(NSString*)label param:(clap_id)param minimum:(double)minimum maximum:(double)maximum y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    const double value = getParam(*_plugin, param);
    CGFloat normalized = static_cast<CGFloat>(std::clamp((value - minimum) / std::max(0.000001, maximum - minimum), 0.0, 1.0));
    if (param == kParamRate || param == kParamLength) {
        normalized = static_cast<CGFloat>(std::clamp(
            std::log(std::max(minimum, value) / minimum) / std::log(maximum / minimum), 0.0, 1.0));
    }
    s3g::clap_gui::drawSlider(label, [self displayValueForParam:param], normalized, y,
                              attrs, s3g::clap_gui::softValueAttrs(), style, 664, 758, 842, 75);
}

- (void)drawMenu:(NSString*)label param:(clap_id)param y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(label, [self displayValueForParam:param], y,
                            attrs, s3g::clap_gui::softValueAttrs(), style, 664, 758, 132);
}

- (void)drawFieldWithScore:(const s3g::ThreeOafxDisplacementScore&)score
                    params:(const PluginParams&)params
                     phase:(float)phase
                      rect:(NSRect)rect
                     attrs:(NSDictionary*)attrs
{
    [s3g::clap_gui::color(0x111111) setFill];
    NSRectFill(rect);
    const auto geometry = s3g::threeOafxDisplacementGeometry(score, params.dsp, phase, params.playback);
    std::array<GuiProjection, s3g::k3OafxVirtualSpeakers> sourceProjected {};
    std::array<GuiProjection, s3g::k3OafxVirtualSpeakers> targetProjected {};
    for (uint32_t point = 0u; point < s3g::k3OafxVirtualSpeakers; ++point) {
        sourceProjected[point] = [self projectPoint:score.source[point] inRect:rect];
        targetProjected[point] = [self projectPoint:geometry[point] inRect:rect];
    }

    auto drawEdgeSet = [&](const auto& projected, NSColor* color, CGFloat width) {
        [color setStroke];
        for (const auto& edge : displacementMeshEdges()) {
            const NSPoint a = projected[edge[0]].point;
            const NSPoint b = projected[edge[1]].point;
            if (_viewMode == 3 && std::abs(a.x - b.x) > rect.size.width * 0.55) continue;
            NSBezierPath* line = [NSBezierPath bezierPath];
            [line setLineWidth:width];
            [line moveToPoint:a];
            [line lineToPoint:b];
            [line stroke];
        }
    };
    drawEdgeSet(sourceProjected, s3g::clap_gui::color(0x747474, 0.13), 0.7);
    drawEdgeSet(targetProjected, s3g::clap_gui::color(0x74bdc6, 0.30), 0.9);

    [s3g::clap_gui::color(0x777777, 0.24) setStroke];
    for (uint32_t point = 0u; point < s3g::k3OafxVirtualSpeakers; ++point) {
        NSBezierPath* spoke = [NSBezierPath bezierPath];
        [spoke setLineWidth:0.7];
        [spoke moveToPoint:sourceProjected[point].point];
        [spoke lineToPoint:targetProjected[point].point];
        [spoke stroke];
    }

    std::array<uint32_t, s3g::k3OafxVirtualSpeakers> order {};
    for (uint32_t point = 0u; point < order.size(); ++point) order[point] = point;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return targetProjected[a].depth < targetProjected[b].depth;
    });
    for (uint32_t point : order) {
        const NSPoint source = sourceProjected[point].point;
        [s3g::clap_gui::color(0x777777, 0.70) setFill];
        NSRectFill(NSMakeRect(source.x - 2.0, source.y - 2.0, 4.0, 4.0));
        const NSPoint target = targetProjected[point].point;
        [displacementPointColor(geometry[point]) setFill];
        NSRectFill(NSMakeRect(target.x - 3.5, target.y - 3.5, 7.0, 7.0));
    }

    std::vector<NSRect> occupied;
    occupied.reserve(s3g::k3OafxVirtualSpeakers * 2u);
    for (uint32_t point = 0u; point < s3g::k3OafxVirtualSpeakers; ++point) {
        const NSPoint target = targetProjected[point].point;
        occupied.push_back(NSMakeRect(target.x - 5.0, target.y - 5.0, 10.0, 10.0));
    }
    for (uint32_t point : order) {
        const NSPoint target = targetProjected[point].point;
        NSString* label = [NSString stringWithFormat:@"%u", point + 1u];
        const NSSize size = [label sizeWithAttributes:attrs];
        const std::array<NSPoint, 8> offsets {{
            NSMakePoint(6.0, -size.height * 0.55),
            NSMakePoint(6.0, 3.0),
            NSMakePoint(-size.width - 6.0, -size.height * 0.55),
            NSMakePoint(-size.width - 6.0, 3.0),
            NSMakePoint(-size.width * 0.5, -size.height - 5.0),
            NSMakePoint(-size.width * 0.5, 6.0),
            NSMakePoint(10.0, -size.height - 4.0),
            NSMakePoint(-size.width - 10.0, -size.height - 4.0),
        }};
        NSRect chosen = NSMakeRect(target.x + 6.0, target.y - size.height * 0.55, size.width, size.height);
        for (const NSPoint offset : offsets) {
            const NSRect candidate = NSMakeRect(target.x + offset.x, target.y + offset.y, size.width, size.height);
            const NSRect padded = NSInsetRect(candidate, -1.5, -1.0);
            const bool inside = NSContainsRect(NSInsetRect(rect, 2.0, 2.0), candidate);
            const bool clear = std::none_of(occupied.begin(), occupied.end(),
                [&](NSRect used) { return NSIntersectsRect(padded, used); });
            if (inside && clear) {
                chosen = candidate;
                break;
            }
        }
        [label drawAtPoint:chosen.origin withAttributes:attrs];
        occupied.push_back(NSInsetRect(chosen, -1.5, -1.0));
    }
}

- (void)drawTimelineWithScore:(const s3g::ThreeOafxDisplacementScore&)score phase:(float)phase attrs:(NSDictionary*)attrs
{
    const NSRect rect = [self timelineRect];
    [s3g::clap_gui::color(0x151515) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0x4b4b4b) setStroke];
    NSFrameRect(rect);
    const CGFloat x = rect.origin.x + std::clamp<CGFloat>(phase, 0.0, 1.0) * rect.size.width;
    [s3g::clap_gui::color(0x4d777d, 0.50) setFill];
    NSRectFill(NSMakeRect(rect.origin.x + 1.0, rect.origin.y + 1.0, std::max<CGFloat>(0.0, x - rect.origin.x - 1.0), rect.size.height - 2.0));
    [s3g::clap_gui::color(0xb9b9b9) setStroke];
    for (uint32_t scene = 0u; scene < score.sceneCount; ++scene) {
        const CGFloat marker = rect.origin.x + score.scenes[scene].time * rect.size.width;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(marker, rect.origin.y - 2.0)
                                  toPoint:NSMakePoint(marker, NSMaxY(rect) + 2.0)];
    }
    [s3g::clap_gui::color(0xd0d0d0) setFill];
    NSRectFill(NSMakeRect(x - 1.5, rect.origin.y - 3.0, 3.0, rect.size.height + 6.0));
    (void)attrs;
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == 0) return;
    static NSString* transportItems[] = { @"SYNC", @"FREE", @"SCRUB" };
    static NSString* playbackItems[] = { @"LOOP", @"PALINDROME" };
    static NSString* distanceItems[] = { @"GAIN", @"PHYSICAL" };
    NSString* const* items = transportItems;
    uint32_t count = 3u;
    int selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.transport));
    if (_openMenu == 2) {
        items = playbackItems;
        count = 2u;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.playback));
    } else if (_openMenu == 3) {
        items = distanceItems;
        count = 2u;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.dsp.distanceMode));
    }
    s3g::clap_gui::drawDropdownMenu([self menuRectForId:_openMenu], 19.0, items, count,
                                    selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSDictionary* titles = s3g::clap_gui::softTitleAttrs();
    [style.bg setFill];
    NSRectFill([self bounds]);

    s3g::ThreeOafxDisplacementScore score;
    std::string scoreName;
    std::string status;
    {
        std::lock_guard<std::mutex> lock(_plugin->stateMutex);
        score = _plugin->score;
        scoreName = _plugin->scoreName;
        status = _plugin->status;
    }
    const PluginParams params = _plugin->params;
    const float phase = params.transport == TransportMode::Scrub
        ? params.position
        : _plugin->guiPhase.load(std::memory_order_relaxed);

    [@"s3g 3OAFX DISPLACEMENT" drawAtPoint:NSMakePoint(14, 13) withAttributes:titles];
    [@"16 CH" drawAtPoint:NSMakePoint(806, 14) withAttributes:values];
    const float peak = _plugin->outputPeak.exchange(
        _plugin->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    [s3g::clap_gui::peakDbText(peak) drawAtPoint:NSMakePoint(856, 14) withAttributes:values];

    const NSRect fieldPanel = [self fieldPanelRect];
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"DISPLACEMENT FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, labels, style);
    s3g::clap_gui::drawHeaderActionButton([self loadButtonRect], fieldPanel, @"LOAD SCORE", labels, style);
    static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4", @"MAP" };
    for (int index = 0; index < 2; ++index) {
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:index], fieldPanel, index == 0 ? @"-" : @"+", false, labels, style);
    }
    for (int index = 0; index < 4; ++index) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:index], fieldPanel, viewLabels[index], _viewMode == index, labels, style);
    }
    [self drawFieldWithScore:score params:params phase:phase rect:[self fieldRect] attrs:values];
    [self drawTimelineWithScore:score phase:phase attrs:values];
    NSString* nameText = [[NSString stringWithUTF8String:scoreName.c_str()] uppercaseString];
    NSString* statusText = [[NSString stringWithUTF8String:status.c_str()] uppercaseString];
    [nameText drawAtPoint:NSMakePoint(38, 560) withAttributes:labels];
    [statusText drawAtPoint:NSMakePoint(350, 560) withAttributes:values];

    s3g::clap_gui::drawPanelFrame(648, 42, 258, 185, style);
    s3g::clap_gui::drawPanelHeader(@"PLAYBACK", true, 648, 42, 258, 21, labels, style);
    [self drawMenu:@"CLOCK" param:kParamTransport y:78 attrs:labels style:style];
    [self drawMenu:@"PLAY" param:kParamPlayback y:104 attrs:labels style:style];
    [self drawSlider:@"POS" param:kParamPosition minimum:0.0 maximum:1.0 y:130 attrs:labels style:style];
    [self drawSlider:@"RATE" param:kParamRate minimum:0.001 maximum:2.0 y:156 attrs:labels style:style];
    [self drawSlider:@"BEATS" param:kParamLength minimum:0.25 maximum:128.0 y:182 attrs:labels style:style];

    s3g::clap_gui::drawPanelFrame(648, 240, 258, 146, style);
    s3g::clap_gui::drawPanelHeader(@"WARP", true, 648, 240, 258, 21, labels, style);
    [self drawSlider:@"AMT" param:kParamAmount minimum:0.0 maximum:1.0 y:276 attrs:labels style:style];
    [self drawSlider:@"AZ" param:kParamAzimuthScale minimum:0.0 maximum:2.0 y:302 attrs:labels style:style];
    [self drawSlider:@"EL" param:kParamElevationScale minimum:0.0 maximum:2.0 y:328 attrs:labels style:style];
    [self drawSlider:@"RAD" param:kParamRadiusScale minimum:0.0 maximum:2.0 y:354 attrs:labels style:style];

    s3g::clap_gui::drawPanelFrame(648, 399, 258, 193, style);
    s3g::clap_gui::drawPanelHeader(@"DISTANCE", true, 648, 399, 258, 21, labels, style);
    [self drawMenu:@"DIST" param:kParamDistanceMode y:435 attrs:labels style:style];
    [self drawSlider:@"MTR" param:kParamReferenceMeters minimum:0.5 maximum:10.0 y:461 attrs:labels style:style];
    [self drawSlider:@"ENRG" param:kParamEnergy minimum:0.0 maximum:1.0 y:487 attrs:labels style:style];
    [self drawSlider:@"OUT" param:kParamOutput minimum:-60.0 maximum:12.0 y:513 attrs:labels style:style];
    s3g::clap_gui::drawToggle(@"BYP", params.dsp.bypass, 539, labels, values, style, 664, 758, 64);
    [self drawOpenMenu:values style:style];
}

- (void)loadScore
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[ @"json" ]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    NSData* data = [NSData dataWithContentsOfURL:[panel URL]];
    s3g::ThreeOafxDisplacementScore score;
    float amount = 0.65f;
    float azimuthScale = 1.0f;
    float elevationScale = 1.0f;
    float radiusScale = 1.0f;
    std::string name;
    std::string error;
    if (!parseScoreData(data, score, amount, azimuthScale, elevationScale, radiusScale, name, error)) {
        std::lock_guard<std::mutex> lock(_plugin->stateMutex);
        _plugin->status = error;
        [self setNeedsDisplay:YES];
        return;
    }
    if (name.empty()) {
        NSString* filename = [[[panel URL] lastPathComponent] stringByDeletingPathExtension];
        if (filename) name = [filename UTF8String];
    }
    installScore(*_plugin, score, amount, azimuthScale, elevationScale, radiusScale, name);
    [self setNeedsDisplay:YES];
}

- (void)updateParam:(clap_id)param fromPoint:(NSPoint)point
{
    const auto* definition = std::find_if(std::begin(kParamDefs), std::end(kParamDefs),
        [&](const ParamDef& item) { return item.id == param; });
    if (definition == std::end(kParamDefs)) return;
    const double normalized = std::clamp((point.x - 758.0) / 75.0, 0.0, 1.0);
    double value = definition->minimum + normalized * (definition->maximum - definition->minimum);
    if (param == kParamRate || param == kParamLength) {
        value = definition->minimum * std::pow(definition->maximum / definition->minimum, normalized);
    }
    [self setParam:param value:value];
}

- (void)updateTimeline:(NSPoint)point
{
    const NSRect rect = [self timelineRect];
    const double position = std::clamp((point.x - rect.origin.x) / rect.size.width, 0.0, 1.0);
    [self setParam:kParamPosition value:position];
    [self setParam:kParamTransport value:static_cast<double>(static_cast<uint32_t>(TransportMode::Scrub))];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu != 0) {
        const NSRect menu = [self menuRectForId:_openMenu];
        const uint32_t count = _openMenu == 1 ? 3u : 2u;
        const int hit = s3g::clap_gui::dropdownHitIndex(point, menu, 19.0, count);
        if (hit >= 0) {
            const clap_id param = _openMenu == 1 ? kParamTransport : (_openMenu == 2 ? kParamPlayback : kParamDistanceMode);
            [self setParam:param value:hit];
            _openMenu = 0;
            _hoverMenuItem = -1;
            return;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
    }
    if (NSPointInRect(point, [self loadButtonRect])) { [self loadScore]; return; }
    for (int index = 0; index < 4; ++index) {
        if (NSPointInRect(point, [self viewButtonRect:index])) { [self setViewPreset:index]; return; }
    }
    for (int index = 0; index < 2; ++index) {
        if (NSPointInRect(point, [self zoomButtonRect:index])) {
            _viewZoom = std::clamp(_viewZoom * (index == 0 ? 0.88 : 1.14), 0.55, 2.4);
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const struct { int menu; CGFloat y; } menus[] { { 1, 78 }, { 2, 104 }, { 3, 435 } };
    for (const auto& menu : menus) {
        if (NSPointInRect(point, NSMakeRect(758, menu.y - 2, 132, 19))) {
            _openMenu = menu.menu;
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, NSMakeRect(758, 536, 64, 20))) {
        [self setParam:kParamBypass value:_plugin->params.dsp.bypass ? 0.0 : 1.0];
        return;
    }
    const struct { clap_id param; CGFloat y; } sliders[] {
        { kParamPosition, 130 }, { kParamRate, 156 }, { kParamLength, 182 },
        { kParamAmount, 276 }, { kParamAzimuthScale, 302 }, { kParamElevationScale, 328 }, { kParamRadiusScale, 354 },
        { kParamReferenceMeters, 461 }, { kParamEnergy, 487 }, { kParamOutput, 513 },
    };
    for (const auto& slider : sliders) {
        if (NSPointInRect(point, NSMakeRect(752, slider.y - 5, 88, 20))) {
            _dragParam = slider.param;
            if ([event clickCount] >= 2) {
                const auto* definition = std::find_if(std::begin(kParamDefs), std::end(kParamDefs),
                    [&](const ParamDef& item) { return item.id == slider.param; });
                if (definition != std::end(kParamDefs)) [self setParam:slider.param value:definition->defaultValue];
            } else {
                [self updateParam:slider.param fromPoint:point];
            }
            return;
        }
    }
    if (NSPointInRect(point, [self timelineRect])) {
        _dragTimeline = YES;
        [self updateTimeline:point];
        return;
    }
    if (NSPointInRect(point, [self fieldRect]) && _viewMode != 3) {
        _dragView = YES;
        _lastDragPoint = point;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragParam != CLAP_INVALID_ID) {
        [self updateParam:_dragParam fromPoint:point];
    } else if (_dragTimeline) {
        [self updateTimeline:point];
    } else if (_dragView) {
        _viewAzimuthDeg = std::clamp(_viewAzimuthDeg + (point.x - _lastDragPoint.x) * 0.45, -180.0, 180.0);
        _viewElevationDeg = std::clamp(_viewElevationDeg + (point.y - _lastDragPoint.y) * 0.35, -90.0, 90.0);
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragTimeline = NO;
    _dragView = NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const uint32_t count = _openMenu == 3 ? 2u : 3u;
    _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(point, [self menuRectForId:_openMenu], 19.0, count);
    [self setNeedsDisplay:YES];
}

- (void)scrollWheel:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (!NSPointInRect(point, [self fieldRect]) || _viewMode == 3) return;
    _viewZoom = std::clamp(_viewZoom * std::exp(-[event scrollingDeltaY] * 0.035), 0.55, 2.4);
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool)
{
    return api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool)
{
    if (!guiIsApiSupported(plugin, api, false)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3G3OAFXDisplacementView alloc] initWithPlugin:instance];
    return instance->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return;
    [static_cast<S3G3OAFXDisplacementView*>(instance->guiView) stopRefreshTimer];
    [static_cast<NSView*>(instance->guiView) removeFromSuperview];
    [static_cast<NSView*>(instance->guiView) release];
    instance->guiView = nullptr;
    instance->guiVisible = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    if (!width || !height) return false;
    *width = kGuiWidth;
    *height = kGuiHeight;
    return true;
}
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*) { return false; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    [static_cast<NSView*>(instance->guiView) setFrameSize:NSMakeSize(width, height)];
    return true;
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(instance->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = true;
    [static_cast<NSView*>(instance->guiView) setHidden:NO];
    [static_cast<S3G3OAFXDisplacementView*>(instance->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3G3OAFXDisplacementView*>(instance->guiView) stopRefreshTimer];
    [static_cast<NSView*>(instance->guiView) setHidden:YES];
    return true;
}

const clap_plugin_gui_t guiExt {
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
    guiHide,
};

} // namespace
#endif

namespace {

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

constexpr const char* features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kPluginName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0-pre",
    "3OA field displacement player for s3g-mc Displacement Score files.",
    features,
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    if (host && host->get_extension) {
        instance->hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
    }
    instance->params = sanitizeParams(instance->params);
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
    instance->plugin.get_extension = getExtension;
    instance->plugin.on_main_thread = [](const clap_plugin_t*) {};
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, kPluginId) == 0 ? create(host) : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* id)
{
    return id && std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const CLAP_EXPORT clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
