#include "s3g_ambi_path_encoder.h"
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
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = s3g::kAmbiPathEncoderMaxInputs;
constexpr uint32_t kOutputChannels = s3g::kAmbiPathEncoderMaxChannels;
constexpr uint32_t kStateVersion = 4;

constexpr clap_id kInputsParamId = 1;
constexpr clap_id kOrderParamId = 2;
constexpr clap_id kPathsParamId = 3;
constexpr clap_id kSelectedPathParamId = 4;
constexpr clap_id kSelectedSourceParamId = 5;
constexpr clap_id kAssignParamId = 6;
constexpr clap_id kPlaybackParamId = 7;
constexpr clap_id kLoopParamId = 8;
constexpr clap_id kInterpParamId = 9;
constexpr clap_id kRateParamId = 10;
constexpr clap_id kPhaseParamId = 11;
constexpr clap_id kPhaseSpreadParamId = 12;
constexpr clap_id kSmoothParamId = 13;
constexpr clap_id kEaseParamId = 14;
constexpr clap_id kDistanceScaleParamId = 15;
constexpr clap_id kOutputParamId = 16;
constexpr clap_id kSyncParamId = 17;
constexpr clap_id kDivisionParamId = 18;
constexpr clap_id kDopplerParamId = 19;
constexpr clap_id kAirParamId = 20;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiPathEncoderParams params {};
    std::array<s3g::AmbiPath, s3g::kAmbiPathEncoderMaxPaths> paths {};
    int32_t guiViewMode = 0;
    double guiViewAzDeg = 90.0;
    double guiViewElDeg = 0.0;
    double guiViewZoom = 1.0;
};

struct SavedStateV3 {
    uint32_t version = 3;
    s3g::AmbiPathEncoderParams params {};
    std::array<s3g::AmbiPath, s3g::kAmbiPathEncoderMaxPaths> paths {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiPathEncoder encoder {};
    s3g::AmbiPathEncoderParams params {};
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
    int guiViewMode = 0;
    double guiViewAzDeg = 90.0;
    double guiViewElDeg = 0.0;
    double guiViewZoom = 1.0;
#endif
};

struct ParamInfo {
    clap_id id;
    const char* name;
    double min;
    double max;
    double def;
    bool stepped;
};

constexpr ParamInfo kParams[] {
    { kInputsParamId, "Inputs", 1.0, 64.0, 64.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kPathsParamId, "Active Paths", 1.0, 16.0, 1.0, true },
    { kSelectedPathParamId, "Selected Path", 1.0, 16.0, 1.0, true },
    { kSelectedSourceParamId, "Selected Source", 1.0, 64.0, 1.0, true },
    { kAssignParamId, "Assign", 0.0, 2.0, 1.0, true },
    { kPlaybackParamId, "Playback", 0.0, 2.0, 1.0, true },
    { kLoopParamId, "Loop Mode", 0.0, 2.0, 1.0, true },
    { kInterpParamId, "Interpolation", 0.0, 2.0, 1.0, true },
    { kRateParamId, "Rate", 0.001, 4.0, 0.08, false },
    { kSyncParamId, "Sync", 0.0, 1.0, 0.0, true },
    { kDivisionParamId, "Division", 0.25, 64.0, 4.0, false },
    { kPhaseParamId, "Phase", 0.0, 1.0, 0.0, false },
    { kPhaseSpreadParamId, "Phase Spread", 0.0, 1.0, 0.0, false },
    { kSmoothParamId, "Smooth", 0.0, 0.995, 0.12, false },
    { kEaseParamId, "Ease", 0.0, 1.0, 0.0, false },
    { kDistanceScaleParamId, "Distance Scale", 0.05, 8.0, 1.0, false },
    { kDopplerParamId, "Doppler", 0.0, 1.0, 0.0, false },
    { kAirParamId, "Air", 0.0, 1.0, 0.0, false },
    { kOutputParamId, "Output", -60.0, 12.0, -12.0, false },
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

const char* assignName(uint32_t index)
{
    static constexpr const char* kNames[] { "ONE", "ROUND", "SOURCE" };
    return kNames[std::min<uint32_t>(index, 2u)];
}

const char* playbackName(uint32_t index)
{
    static constexpr const char* kNames[] { "OFF", "RUN", "SCRUB" };
    return kNames[std::min<uint32_t>(index, 2u)];
}

const char* syncName(uint32_t index)
{
    static constexpr const char* kNames[] { "FREE", "SYNC" };
    return kNames[std::min<uint32_t>(index, 1u)];
}

const char* loopName(uint32_t index)
{
    static constexpr const char* kNames[] { "ONE", "LOOP", "PAL" };
    return kNames[std::min<uint32_t>(index, 2u)];
}

const char* interpName(uint32_t index)
{
    static constexpr const char* kNames[] { "LINEAR", "CATMULL", "HOLD" };
    return kNames[std::min<uint32_t>(index, 2u)];
}

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

void applyParam(Plugin& p, clap_id id, double value)
{
    switch (id) {
    case kInputsParamId: p.params.activeInputs = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, 64u); break;
    case kOrderParamId: p.params.order = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, 7u); break;
    case kPathsParamId: p.params.activePaths = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, 16u); break;
    case kSelectedPathParamId: p.params.selectedPath = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, 16u) - 1u; break;
    case kSelectedSourceParamId: p.params.selectedSource = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u, 64u) - 1u; break;
    case kAssignParamId: p.params.assignMode = static_cast<s3g::AmbiPathAssignMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kPlaybackParamId: p.params.playback = static_cast<s3g::AmbiPathPlaybackMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kSyncParamId: p.params.syncMode = static_cast<s3g::AmbiPathSyncMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 1u)); break;
    case kLoopParamId: p.params.loopMode = static_cast<s3g::AmbiPathLoopMode>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kInterpParamId: p.params.interpolation = static_cast<s3g::AmbiPathInterpolation>(std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, 2u)); break;
    case kRateParamId: p.params.rateHz = static_cast<float>(value); break;
    case kDivisionParamId: p.params.syncDivisionBeats = static_cast<float>(value); break;
    case kPhaseParamId: p.params.phase = static_cast<float>(value); break;
    case kPhaseSpreadParamId: p.params.phaseSpread = static_cast<float>(value); break;
    case kSmoothParamId: p.params.smooth = static_cast<float>(value); break;
    case kEaseParamId: p.params.ease = static_cast<float>(value); break;
    case kDistanceScaleParamId: p.params.distanceScale = static_cast<float>(value); break;
    case kDopplerParamId: p.params.doppler = static_cast<float>(value); break;
    case kAirParamId: p.params.air = static_cast<float>(value); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(value); break;
    default: break;
    }
    p.encoder.setParams(p.params);
    p.params = p.encoder.params();
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->encoder.prepare(sampleRate);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->encoder.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
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

void updateTransportPhase(Plugin& p, const clap_event_transport_t* transport)
{
    if (p.params.syncMode != s3g::AmbiPathSyncMode::Sync) {
        p.encoder.useFreePhase();
        return;
    }
    if (transport && (transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0) {
        const double beats = static_cast<double>(transport->song_pos_beats) / static_cast<double>(CLAP_BEATTIME_FACTOR);
        const double div = std::max(0.25, static_cast<double>(p.params.syncDivisionBeats));
        const double phase = std::fmod(beats / div, 1.0);
        p.encoder.setExternalPhase(static_cast<float>(phase < 0.0 ? phase + 1.0 : phase));
    } else {
        p.encoder.useFreePhase();
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc ? proc->in_events : nullptr);
    if (!proc || proc->frames_count == 0u || !proc->audio_outputs || proc->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    updateTransportPhase(*p, proc->transport);

    const float* inputs[kInputChannels] {};
    float* outputs[kOutputChannels] {};
    const uint32_t inChans = proc->audio_inputs_count ? std::min<uint32_t>(proc->audio_inputs[0].channel_count, kInputChannels) : 0u;
    const uint32_t outChans = std::min<uint32_t>(proc->audio_outputs[0].channel_count, kOutputChannels);
    for (uint32_t ch = 0; ch < inChans; ++ch) inputs[ch] = proc->audio_inputs[0].data32 ? proc->audio_inputs[0].data32[ch] : nullptr;
    for (uint32_t ch = 0; ch < outChans; ++ch) outputs[ch] = proc->audio_outputs[0].data32 ? proc->audio_outputs[0].data32[ch] : nullptr;

    p->encoder.processBlock<float>(inputs, outputs, inChans, outChans, proc->frames_count);

    float peak = 0.0f;
    for (uint32_t ch = 0; ch < outChans; ++ch) {
        if (!outputs[ch]) continue;
        for (uint32_t i = 0; i < proc->frames_count; ++i) peak = std::max(peak, std::fabs(outputs[ch][i]));
    }
    const float prev = p->outputPeak.load(std::memory_order_relaxed);
    p->outputPeak.store(std::max(peak, prev * 0.90f), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0) return false;
    info->id = isInput ? 0 : 1;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Audio In" : "Ambisonic Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }
bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(kParams)) return false;
    const auto& p = kParams[index];
    info->id = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (p.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    info->min_value = p.min;
    info->max_value = p.max;
    info->default_value = p.def;
    std::snprintf(info->name, sizeof(info->name), "%s", p.name);
    std::snprintf(info->module, sizeof(info->module), "Ambi Path Encoder");
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& p = self(plugin)->params;
    switch (id) {
    case kInputsParamId: *value = p.activeInputs; return true;
    case kOrderParamId: *value = p.order; return true;
    case kPathsParamId: *value = p.activePaths; return true;
    case kSelectedPathParamId: *value = p.selectedPath + 1u; return true;
    case kSelectedSourceParamId: *value = p.selectedSource + 1u; return true;
    case kAssignParamId: *value = static_cast<uint32_t>(p.assignMode); return true;
    case kPlaybackParamId: *value = static_cast<uint32_t>(p.playback); return true;
    case kSyncParamId: *value = static_cast<uint32_t>(p.syncMode); return true;
    case kLoopParamId: *value = static_cast<uint32_t>(p.loopMode); return true;
    case kInterpParamId: *value = static_cast<uint32_t>(p.interpolation); return true;
    case kRateParamId: *value = p.rateHz; return true;
    case kDivisionParamId: *value = p.syncDivisionBeats; return true;
    case kPhaseParamId: *value = p.phase; return true;
    case kPhaseSpreadParamId: *value = p.phaseSpread; return true;
    case kSmoothParamId: *value = p.smooth; return true;
    case kEaseParamId: *value = p.ease; return true;
    case kDistanceScaleParamId: *value = p.distanceScale; return true;
    case kDopplerParamId: *value = p.doppler; return true;
    case kAirParamId: *value = p.air; return true;
    case kOutputParamId: *value = p.outputGainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kAssignParamId) std::snprintf(display, size, "%s", assignName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kPlaybackParamId) std::snprintf(display, size, "%s", playbackName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kSyncParamId) std::snprintf(display, size, "%s", syncName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kLoopParamId) std::snprintf(display, size, "%s", loopName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kInterpParamId) std::snprintf(display, size, "%s", interpName(static_cast<uint32_t>(std::lround(value))));
    else if (id == kRateParamId) std::snprintf(display, size, "%.3f Hz", value);
    else if (id == kDivisionParamId) std::snprintf(display, size, "%.2g beats", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kDistanceScaleParamId) std::snprintf(display, size, "%.2fx", value);
    else if (id == kPhaseParamId || id == kPhaseSpreadParamId || id == kSmoothParamId || id == kEaseParamId || id == kDopplerParamId || id == kAirParamId) std::snprintf(display, size, "%.0f%%", value * 100.0);
    else std::snprintf(display, size, "%.0f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;

    static constexpr const char* assign[] { "ONE", "ROUND", "SOURCE" };
    static constexpr const char* playback[] { "OFF", "RUN", "SCRUB" };
    static constexpr const char* sync[] { "FREE", "SYNC" };
    static constexpr const char* loop[] { "ONE", "LOOP", "PAL" };
    static constexpr const char* interpolation[] { "LINEAR", "CATMULL", "HOLD" };

    const char* const* names = nullptr;
    uint32_t count = 0u;
    if (id == kAssignParamId) { names = assign; count = static_cast<uint32_t>(std::size(assign)); }
    else if (id == kPlaybackParamId) { names = playback; count = static_cast<uint32_t>(std::size(playback)); }
    else if (id == kSyncParamId) { names = sync; count = static_cast<uint32_t>(std::size(sync)); }
    else if (id == kLoopParamId) { names = loop; count = static_cast<uint32_t>(std::size(loop)); }
    else if (id == kInterpParamId) { names = interpolation; count = static_cast<uint32_t>(std::size(interpolation)); }
    if (names) {
        for (uint32_t index = 0u; index < count; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }

    *value = std::atof(display);
    if (id == kPhaseParamId || id == kPhaseSpreadParamId || id == kSmoothParamId
        || id == kEaseParamId || id == kDopplerParamId || id == kAirParamId) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    SavedState state { kStateVersion, p->params, p->encoder.paths() };
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
    SavedState state {};
    uint32_t version = 0;
    if (!readExact(stream, &version, sizeof(version))) return false;
    if (version == kStateVersion) {
        state.version = version;
        if (!readExact(stream, reinterpret_cast<char*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
    } else if (version == 3) {
        SavedStateV3 old {};
        old.version = version;
        if (!readExact(stream, reinterpret_cast<char*>(&old) + sizeof(version), sizeof(old) - sizeof(version))) return false;
        state.params = old.params;
        state.paths = old.paths;
    } else {
        return false;
    }
    auto* p = self(plugin);
    p->params = state.params;
    p->encoder.setPaths(state.paths);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
#if defined(__APPLE__)
    p->guiViewMode = std::clamp<int>(state.guiViewMode, -1, 2);
    p->guiViewAzDeg = std::clamp(state.guiViewAzDeg, -180.0, 180.0);
    p->guiViewElDeg = std::clamp(state.guiViewElDeg, -90.0, 90.0);
    p->guiViewZoom = std::clamp(state.guiViewZoom, 0.55, 2.4);
#endif
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {
double numberFromObject(id obj, double fallback)
{
    return [obj respondsToSelector:@selector(doubleValue)] ? [obj doubleValue] : fallback;
}

s3g::AmbiPathPoint pointFromDict(NSDictionary* dict, uint32_t index, uint32_t count)
{
    s3g::AmbiPathPoint p {};
    p.x = static_cast<float>(numberFromObject([dict objectForKey:@"x"], 0.0));
    p.y = static_cast<float>(numberFromObject([dict objectForKey:@"y"], 1.0));
    p.z = static_cast<float>(numberFromObject([dict objectForKey:@"z"], 0.0));
    p.time = static_cast<float>(numberFromObject([dict objectForKey:@"time"], count > 1 ? static_cast<double>(index) / static_cast<double>(count - 1u) : 0.0));
    return p;
}

std::vector<double> numbersFromString(NSString* text)
{
    std::vector<double> out;
    NSString* source = text ?: @"";
    NSRegularExpression* re = [NSRegularExpression regularExpressionWithPattern:@"[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?" options:0 error:nil];
    NSArray<NSTextCheckingResult*>* matches = [re matchesInString:source options:0 range:NSMakeRange(0, [source length])];
    for (NSTextCheckingResult* result in matches) {
        NSString* token = [source substringWithRange:[result range]];
        out.push_back([token doubleValue]);
    }
    return out;
}

NSString* attributeValue(NSString* tag, NSString* name)
{
    NSString* pattern = [NSString stringWithFormat:@"%@=\"", name];
    NSRange r = [tag rangeOfString:pattern options:NSCaseInsensitiveSearch];
    if (r.location == NSNotFound) {
        pattern = [NSString stringWithFormat:@"%@='", name];
        r = [tag rangeOfString:pattern options:NSCaseInsensitiveSearch];
        if (r.location == NSNotFound) return nil;
    }
    NSUInteger start = r.location + r.length;
    unichar quote = [tag characterAtIndex:start - 1];
    NSRange rest = NSMakeRange(start, [tag length] - start);
    NSRange end = [tag rangeOfString:[NSString stringWithCharacters:&quote length:1] options:0 range:rest];
    if (end.location == NSNotFound) return nil;
    return [tag substringWithRange:NSMakeRange(start, end.location - start)];
}

s3g::AmbiPath pathFromXYNumbers(const std::vector<double>& nums)
{
    s3g::AmbiPath path {};
    if (nums.size() < 4) return path;
    double minX = nums[0], maxX = nums[0], minY = nums[1], maxY = nums[1];
    for (size_t i = 0; i + 1 < nums.size(); i += 2) {
        minX = std::min(minX, nums[i]);
        maxX = std::max(maxX, nums[i]);
        minY = std::min(minY, nums[i + 1]);
        maxY = std::max(maxY, nums[i + 1]);
    }
    const double cx = (minX + maxX) * 0.5;
    const double cy = (minY + maxY) * 0.5;
    const double scale = std::max(1.0, std::max(maxX - minX, maxY - minY) * 0.5);
    const uint32_t count = std::min<uint32_t>(s3g::kAmbiPathEncoderMaxPoints, static_cast<uint32_t>(nums.size() / 2));
    path.pointCount = count;
    for (uint32_t i = 0; i < count; ++i) {
        const double x = nums[i * 2u];
        const double y = nums[i * 2u + 1u];
        path.points[i].x = static_cast<float>((x - cx) / scale);
        path.points[i].y = static_cast<float>(-(y - cy) / scale);
        path.points[i].z = 0.0f;
        path.points[i].time = count > 1u ? static_cast<float>(i) / static_cast<float>(count - 1u) : 0.0f;
    }
    return path;
}

float linearToSrgb(float v)
{
    const float x = std::clamp(v, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

NSColor* pathPointColorFromXyz(float x, float y, float z, bool selected, bool activePath)
{
    const float distance = std::sqrt(x * x + y * y + z * z);
    if (distance < 0.0001f) return s3g::clap_gui::color(0x707070, activePath ? 0.85 : 0.42);
    const float azDeg = std::atan2(y, x) * 180.0f / static_cast<float>(M_PI);
    const float elDeg = std::asin(std::clamp(z / std::max(0.0001f, distance), -1.0f, 1.0f)) * 180.0f / static_cast<float>(M_PI);
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
    const float grayMix = selected ? 0.05f : 0.14f;
    r = r * (1.0f - grayMix) + 0.72f * grayMix;
    g = g * (1.0f - grayMix) + 0.72f * grayMix;
    bl = bl * (1.0f - grayMix) + 0.72f * grayMix;
    return [NSColor colorWithCalibratedRed:r green:g blue:bl alpha:selected ? 1.0 : (activePath ? 0.9 : 0.45)];
}

NSColor* sourceMarkerColor(uint32_t source, bool selected)
{
    static constexpr int kPalette[] {
        0x00e5ff, 0xfff000, 0xff4fd8, 0x00ff7a,
        0xff5a36, 0x7c6cff, 0x00b7ff, 0xc8ff00,
        0xff9f1c, 0x45ffdd, 0xff3f7f, 0xb7ff5a,
    };
    const int rgb = kPalette[source % (sizeof(kPalette) / sizeof(kPalette[0]))];
    return s3g::clap_gui::color(rgb, selected ? 1.0 : 0.92);
}
} // namespace

@interface S3GAmbiPathEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    int _dragParam;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    BOOL _dragPoint;
    BOOL _dragRandomDev;
    BOOL _dragRandomPoints;
    BOOL _editMode;
    CGFloat _randomDev;
    uint32_t _randomPoints;
    uint32_t _randomState;
    NSPoint _lastDragPoint;
    int _selectedPoint;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param fromPoint:(NSPoint)pt;
- (void)loadPathJson;
- (void)savePathJson;
- (void)importSvg;
- (void)exportSvg;
- (void)storeViewState;
@end

@implementation S3GAmbiPathEncoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, 900, 792)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = 0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _viewMode = plugin ? plugin->guiViewMode : 0;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 90.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 0.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _dragView = NO;
        _dragPoint = NO;
        _dragRandomDev = NO;
        _dragRandomPoints = NO;
        _editMode = NO;
        _randomDev = 0.45;
        _randomPoints = 8;
        _randomState = 0x5137a11du;
        _selectedPoint = -1;
        [self setWantsLayer:YES];
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)dealloc { [self storeViewState]; [self stopRefreshTimer]; [super dealloc]; }
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
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
}
- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}
- (void)timerTick:(NSTimer*)timer { (void)timer; [self setNeedsDisplay:YES]; }

- (NSPoint)project:(s3g::Vec3)p inRect:(NSRect)rect
{
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.38 * std::clamp(_viewZoom, 0.55, 2.4);
    const float az = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float el = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(az), sa = std::sin(az);
    const float ce = std::cos(el), se = std::sin(el);
    const float x1 = ca * p.x - sa * p.y;
    const float y1 = sa * p.x + ca * p.y;
    const float y2 = ce * y1 + se * p.z;
    return NSMakePoint(NSMidX(rect) + x1 * scale, NSMidY(rect) - y2 * scale);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 90.0; _viewElDeg = 90.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 732); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 638); }
- (NSRect)fileButtonRect:(int)index
{
    const CGFloat w = 126.0;
    const CGFloat gap = 10.0;
    const CGFloat x = 48.0 + static_cast<CGFloat>(index) * (w + gap);
    return NSMakeRect(x, 736.0, w, 22.0);
}

- (NSRect)viewButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 38.0;
    const CGFloat gap = 6.0;
    const CGFloat start = NSMaxX([self zoomButtonRect:1 inRect:rect]) + 12.0;
    return NSMakeRect(start + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

- (NSRect)editButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 48.0;
    const CGFloat gap = 6.0;
    const CGFloat start = rect.origin.x + 104.0;
    return NSMakeRect(start + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

- (NSRect)zoomButtonRect:(int)index inRect:(NSRect)rect
{
    const CGFloat w = 22.0;
    const CGFloat gap = 6.0;
    const CGFloat viewTotal = 3.0 * 38.0 + 2.0 * 6.0;
    const CGFloat zoomTotal = 2.0 * w + gap;
    const CGFloat viewStart = NSMaxX(rect) - 10.0 - viewTotal;
    const CGFloat start = viewStart - 12.0 - zoomTotal;
    return NSMakeRect(start + static_cast<CGFloat>(index) * (w + gap), rect.origin.y + 3.0, w, 16.0);
}

- (void)drawViewButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) s3g::clap_gui::drawHeaderButton([self viewButtonRect:i inRect:rect], rect, labels[i], i == _viewMode, attrs, style);
    s3g::clap_gui::drawHeaderButton([self editButtonRect:0 inRect:rect], rect, @"EDIT", _editMode, attrs, style);
    s3g::clap_gui::drawHeaderButton([self editButtonRect:1 inRect:rect], rect, @"PLAY", !_editMode, attrs, style);
}

- (void)drawZoomButtonsInRect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    static NSString* labels[] = { @"-", @"+" };
    for (int i = 0; i < 2; ++i) s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i inRect:rect], rect, labels[i], false, attrs, style);
}

- (NSString*)displayValueForParam:(clap_id)param value:(double)value
{
    char text[64] {};
    paramsValueToText(&_plugin->plugin, param, value, text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value min:(double)min max:(double)max y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    CGFloat norm = static_cast<CGFloat>(std::clamp((value - min) / std::max(0.000001, max - min), 0.0, 1.0));
    if (param == kRateParamId || param == kDivisionParamId) {
        const double safeMin = std::max(0.000001, min);
        norm = static_cast<CGFloat>(std::clamp(std::log(std::max(safeMin, value) / safeMin) / std::log(max / safeMin), 0.0, 1.0));
    }
    s3g::clap_gui::drawSlider(name, [self displayValueForParam:param value:value], norm, y, attrs, s3g::clap_gui::softValueAttrs(), style, 646.0, 738.0, 826.0, 82.0);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, s3g::clap_gui::softValueAttrs(), style, 646.0, 738.0, 126.0);
}

- (void)drawActionButton:(NSString*)label rect:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [s3g::clap_gui::color(0x202020) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0xb8b8b8) setStroke];
    NSFrameRect(rect);
    [s3g::clap_gui::color(0x343434) setStroke];
    NSFrameRect(NSInsetRect(rect, 1.0, 1.0));
    const NSSize size = [label sizeWithAttributes:attrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                                   rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0)
        withAttributes:attrs];
    (void)style;
}

- (NSRect)randomButtonRect { return NSMakeRect(642, 238, 104, 22); }
- (NSRect)deleteButtonRect { return NSMakeRect(758, 238, 104, 22); }
- (NSRect)clearButtonRect { return NSMakeRect(642, 268, 104, 22); }
- (NSRect)randomDevSliderRect { return NSMakeRect(738, 299, 82, 12); }
- (NSRect)randomPointsSliderRect { return NSMakeRect(738, 323, 82, 12); }

- (float)randomUnit
{
    _randomState = _randomState * 1664525u + 1013904223u;
    return static_cast<float>((_randomState >> 8) & 0x00ffffffu) / 16777215.0f;
}

- (float)randomSigned { return [self randomUnit] * 2.0f - 1.0f; }

- (void)randomizePaths
{
    auto paths = _plugin->encoder.paths();
    const uint32_t active = std::clamp<uint32_t>(_plugin->params.activePaths, 1u, s3g::kAmbiPathEncoderMaxPaths);
    const uint32_t count = std::clamp<uint32_t>(_randomPoints, 2u, s3g::kAmbiPathEncoderMaxPoints);
    const float dev = static_cast<float>(std::clamp(_randomDev, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0)));
    for (uint32_t pi = 0; pi < active; ++pi) {
        auto& path = paths[pi];
        path.pointCount = count;
        const float pathTurn = static_cast<float>(pi) * 2.39996323f;
        const float baseRadius = 0.55f + 0.34f * [self randomUnit];
        const float swirl = 1.0f + std::floor([self randomUnit] * 3.0f);
        const float elevBase = [self randomSigned] * 0.18f * dev;
        for (uint32_t i = 0; i < count; ++i) {
            const float t = count > 1u ? static_cast<float>(i) / static_cast<float>(count) : 0.0f;
            const float wander = [self randomSigned] * dev;
            const float az = t * 2.0f * static_cast<float>(M_PI) + pathTurn + wander * (0.18f + dev * 0.75f);
            const float radius = std::clamp(baseRadius + [self randomSigned] * dev * 0.46f, 0.08f, 1.65f);
            const float elevWave = std::sin(t * 2.0f * static_cast<float>(M_PI) * swirl + pathTurn) * (0.10f + dev * 0.42f);
            path.points[i].x = std::cos(az) * radius;
            path.points[i].y = std::sin(az) * radius;
            path.points[i].z = std::clamp(elevBase + elevWave + [self randomSigned] * dev * 0.34f, -1.35f, 1.35f);
            path.points[i].time = count > 1u ? static_cast<float>(i) / static_cast<float>(count - 1u) : 0.0f;
        }
    }
    _selectedPoint = -1;
    _plugin->encoder.setPaths(paths);
    [self setNeedsDisplay:YES];
}

- (void)updateRandomDev:(NSPoint)pt
{
    const NSRect r = [self randomDevSliderRect];
    _randomDev = std::clamp<CGFloat>((pt.x - r.origin.x) / r.size.width, 0.0, 1.0);
    [self setNeedsDisplay:YES];
}

- (void)updateRandomPoints:(NSPoint)pt
{
    const NSRect r = [self randomPointsSliderRect];
    const CGFloat n = std::clamp<CGFloat>((pt.x - r.origin.x) / r.size.width, 0.0, 1.0);
    _randomPoints = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(2.0 + n * 62.0)), 2u, 64u);
    [self setNeedsDisplay:YES];
}

- (CGFloat)menuRowY:(int)menu
{
    switch (menu) {
    case 1: return 104.0;
    case 2: return 208.0;
    case 3: return 414.0;
    case 4: return 436.0;
    case 5: return 480.0;
    case 6: return 502.0;
    default: return 0.0;
    }
}

- (CGFloat)menuY { return [self menuRowY:_openMenu] + 17.0; }
- (CGFloat)menuW { return 126.0; }

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* assignItems[] = { @"ONE", @"ROUND", @"SOURCE" };
    static NSString* playbackItems[] = { @"OFF", @"RUN", @"SCRUB" };
    static NSString* syncItems[] = { @"FREE", @"SYNC" };
    static NSString* loopItems[] = { @"ONE", @"LOOP", @"PAL" };
    static NSString* interpItems[] = { @"LINEAR", @"CATMULL", @"HOLD" };
    NSString* const* items = orderItems;
    int selected = 0;
    if (_openMenu == 1) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
        _menuItemCount = 7;
    } else if (_openMenu == 2) {
        items = assignItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.assignMode));
        _menuItemCount = 3;
    } else if (_openMenu == 3) {
        items = playbackItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.playback));
        _menuItemCount = 3;
    } else if (_openMenu == 4) {
        items = syncItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.syncMode));
        _menuItemCount = 2;
    } else if (_openMenu == 5) {
        items = loopItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.loopMode));
        _menuItemCount = 3;
    } else if (_openMenu == 6) {
        items = interpItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.interpolation));
        _menuItemCount = 3;
    }
    const CGFloat itemH = 18.0;
    s3g::clap_gui::drawDropdownMenu(NSMakeRect(738, [self menuY], [self menuW], itemH * _menuItemCount),
        itemH, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (s3g::AmbiPathPoint)pointFromScreen:(NSPoint)pt inRect:(NSRect)rect preserving:(s3g::AmbiPathPoint)oldPoint
{
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.38 * std::clamp(_viewZoom, 0.55, 2.4);
    const double x1 = std::clamp((pt.x - NSMidX(rect)) / std::max<CGFloat>(1.0, scale), -2.0, 2.0);
    const double y2 = std::clamp((NSMidY(rect) - pt.y) / std::max<CGFloat>(1.0, scale), -2.0, 2.0);
    const double az = _viewAzDeg * M_PI / 180.0;
    const double el = _viewElDeg * M_PI / 180.0;
    const double ca = std::cos(az), sa = std::sin(az);
    const double ce = std::cos(el), se = std::sin(el);
    const double oldY1 = sa * oldPoint.x + ca * oldPoint.y;
    const double oldZ2 = -se * oldY1 + ce * oldPoint.z;
    const double y1 = ce * y2 - se * oldZ2;
    const double z = se * y2 + ce * oldZ2;
    oldPoint.x = static_cast<float>(std::clamp(ca * x1 + sa * y1, -2.0, 2.0));
    oldPoint.y = static_cast<float>(std::clamp(-sa * x1 + ca * y1, -2.0, 2.0));
    oldPoint.z = static_cast<float>(std::clamp(z, -2.0, 2.0));
    return oldPoint;
}

- (int)hitPathPoint:(NSPoint)pt inRect:(NSRect)rect
{
    const auto paths = _plugin->encoder.paths();
    const auto p = _plugin->params;
    const auto& path = paths[p.selectedPath];
    int hit = -1;
    CGFloat best = 9.0;
    for (uint32_t i = 0; i < path.pointCount; ++i) {
        const auto& pp = path.points[i];
        const s3g::Vec3 v { pp.x, pp.y, pp.z };
        const NSPoint sp = [self project:v inRect:rect];
        const CGFloat d = std::hypot(sp.x - pt.x, sp.y - pt.y);
        if (d < best) {
            best = d;
            hit = static_cast<int>(i);
        }
    }
    return hit;
}

- (void)updateSelectedPointFromScreen:(NSPoint)pt
{
    if (_selectedPoint < 0) return;
    auto paths = _plugin->encoder.paths();
    const uint32_t pathIndex = _plugin->params.selectedPath;
    auto& path = paths[pathIndex];
    if (static_cast<uint32_t>(_selectedPoint) >= path.pointCount) return;
    path.points[_selectedPoint] = [self pointFromScreen:pt inRect:[self fieldRect] preserving:path.points[_selectedPoint]];
    path.points[_selectedPoint].time = path.pointCount > 1u ? static_cast<float>(_selectedPoint) / static_cast<float>(path.pointCount - 1u) : 0.0f;
    _plugin->encoder.setPath(pathIndex, path);
    [self setNeedsDisplay:YES];
}

- (void)retimePath:(s3g::AmbiPath&)path
{
    for (uint32_t i = 0; i < path.pointCount; ++i) path.points[i].time = path.pointCount > 1u ? static_cast<float>(i) / static_cast<float>(path.pointCount - 1u) : 0.0f;
}

- (void)addPointAtScreen:(NSPoint)pt
{
    auto paths = _plugin->encoder.paths();
    const uint32_t pathIndex = _plugin->params.selectedPath;
    auto& path = paths[pathIndex];
    if (path.pointCount >= s3g::kAmbiPathEncoderMaxPoints) return;
    s3g::AmbiPathPoint seed {};
    if (path.pointCount > 0u) seed = path.points[path.pointCount - 1u];
    path.points[path.pointCount] = [self pointFromScreen:pt inRect:[self fieldRect] preserving:seed];
    ++path.pointCount;
    [self retimePath:path];
    _selectedPoint = static_cast<int>(path.pointCount - 1u);
    _plugin->encoder.setPath(pathIndex, path);
}

- (void)deleteSelectedPoint
{
    if (!_editMode) return;
    if (_selectedPoint < 0) return;
    auto paths = _plugin->encoder.paths();
    const uint32_t pathIndex = _plugin->params.selectedPath;
    auto& path = paths[pathIndex];
    if (static_cast<uint32_t>(_selectedPoint) >= path.pointCount) return;
    for (uint32_t i = static_cast<uint32_t>(_selectedPoint); i + 1u < path.pointCount; ++i) path.points[i] = path.points[i + 1u];
    if (path.pointCount > 0u) --path.pointCount;
    [self retimePath:path];
    _selectedPoint = path.pointCount == 0u ? -1 : static_cast<int>(std::min<uint32_t>(static_cast<uint32_t>(_selectedPoint), path.pointCount - 1u));
    _plugin->encoder.setPath(pathIndex, path);
    [self setNeedsDisplay:YES];
}

- (void)clearSelectedPath
{
    if (!_editMode) return;
    auto paths = _plugin->encoder.paths();
    const uint32_t pathIndex = _plugin->params.selectedPath;
    paths[pathIndex].pointCount = 0;
    _selectedPoint = -1;
    _plugin->encoder.setPath(pathIndex, paths[pathIndex]);
    [self setNeedsDisplay:YES];
}

- (void)drawField:(NSRect)rect attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    [s3g::clap_gui::color(0x111111) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    const auto paths = _plugin->encoder.paths();
    const auto params = _plugin->params;
    for (uint32_t pi = 0; pi < params.activePaths; ++pi) {
        const auto& path = paths[pi];
        if (path.pointCount == 0u) continue;
        const BOOL pathSelected = _editMode && pi == params.selectedPath;
        if (path.pointCount >= 2u) {
            NSBezierPath* bez = [NSBezierPath bezierPath];
            [bez setLineWidth:pathSelected ? 1.4 : 0.7];
            for (uint32_t i = 0; i < path.pointCount; ++i) {
                const auto& pp = path.points[i];
                const s3g::Vec3 v { pp.x, pp.y, pp.z };
                NSPoint pt = [self project:v inRect:rect];
                if (i == 0) [bez moveToPoint:pt];
                else [bez lineToPoint:pt];
            }
            const auto& p0 = path.points[0];
            const s3g::Vec3 v0 { p0.x, p0.y, p0.z };
            [bez lineToPoint:[self project:v0 inRect:rect]];
            [[NSColor colorWithCalibratedWhite:(pathSelected ? 0.78 : 0.42) alpha:(pathSelected ? 0.90 : 0.45)] setStroke];
            [bez stroke];
        }
        for (uint32_t i = 0; i < path.pointCount; ++i) {
            const auto& pp = path.points[i];
            const s3g::Vec3 v { pp.x, pp.y, pp.z };
            NSPoint pt = [self project:v inRect:rect];
            const BOOL selected = _editMode && pi == params.selectedPath && static_cast<int>(i) == _selectedPoint;
            [pathPointColorFromXyz(pp.x, pp.y, pp.z, selected, pathSelected) setFill];
            const CGFloat r = selected ? 5.0 : 3.5;
            NSRect box = NSMakeRect(std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0);
            NSRectFill(box);
            if (selected) {
                [[NSColor colorWithCalibratedWhite:0.05 alpha:0.95] setStroke];
                NSFrameRect(box);
                [style.text setStroke];
                NSFrameRect(NSInsetRect(box, -2.0, -2.0));
            }
        }
    }
    const uint32_t active = std::min<uint32_t>(params.activeInputs, s3g::kAmbiPathEncoderMaxInputs);
    for (uint32_t src = 0; src < active; ++src) {
        s3g::Vec3 pos = _plugin->encoder.sourcePositionForDisplay(src);
        NSPoint pt = [self project:pos inRect:rect];
        const BOOL selected = src == params.selectedSource;
        const CGFloat r = selected ? 5.0 : 4.0;
        const NSRect outer = NSMakeRect(std::round(pt.x - r - 1.0), std::round(pt.y - r - 1.0), (r + 1.0) * 2.0, (r + 1.0) * 2.0);
        [[NSColor colorWithCalibratedWhite:0.02 alpha:0.95] setFill];
        NSRectFill(outer);
        [sourceMarkerColor(src, selected) setFill];
        NSRectFill(NSMakeRect(std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
        [[NSColor colorWithCalibratedWhite:selected ? 0.96 : 0.12 alpha:selected ? 0.95 : 0.75] setStroke];
        NSFrameRect(NSMakeRect(std::round(pt.x - r), std::round(pt.y - r), r * 2.0, r * 2.0));
    }
    NSString* viewText = _editMode
        ? (_viewMode == 0 ? @"EDIT TOP: CLICK/DRAG POINTS" : (_viewMode == 1 ? @"EDIT SIDE: CLICK/DRAG POINTS" : @"EDIT: CLICK/DRAG POINTS"))
        : @"PLAY: CLICK/DRAG CAMERA";
    [viewText drawAtPoint:NSMakePoint(rect.origin.x + 10, NSMaxY(rect) - 22) withAttributes:attrs];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* dim = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();

    [@"s3g AMBI PATH ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    const float pk = _plugin->outputPeak.load(std::memory_order_relaxed);
    [s3g::clap_gui::peakDbText(pk) drawAtPoint:NSMakePoint(735, 14) withAttributes:dim];
    [@"64 CH" drawAtPoint:NSMakePoint(832, 14) withAttributes:dim];

    const NSRect fieldPanel = [self fieldPanelRect];
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PATH FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, attrs, style);
    [self drawViewButtonsInRect:fieldPanel attrs:dim style:style];
    [self drawZoomButtonsInRect:fieldPanel attrs:dim style:style];
    [self drawField:[self fieldRect] attrs:dim style:style];

    s3g::clap_gui::drawPanelFrame(630, 42, 250, 330, style);
    s3g::clap_gui::drawPanelHeader(@"PATH", true, 630, 42, 250, 21, attrs, style);
    s3g::clap_gui::drawPanelFrame(630, 384, 250, 320, style);
    s3g::clap_gui::drawPanelHeader(@"MOTION", true, 630, 384, 250, 21, attrs, style);

    const auto p = _plugin->params;
    [self drawSlider:@"INPUTS" param:kInputsParamId value:p.activeInputs min:1 max:64 y:78 attrs:attrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] y:104 attrs:attrs style:style];
    [self drawSlider:@"PATHS" param:kPathsParamId value:p.activePaths min:1 max:16 y:130 attrs:attrs style:style];
    [self drawSlider:@"PATH" param:kSelectedPathParamId value:p.selectedPath + 1u min:1 max:16 y:156 attrs:attrs style:style];
    [self drawSlider:@"SOURCE" param:kSelectedSourceParamId value:p.selectedSource + 1u min:1 max:64 y:182 attrs:attrs style:style];
    [self drawMenu:@"ASSIGN" value:[NSString stringWithUTF8String:assignName(static_cast<uint32_t>(p.assignMode))] y:208 attrs:attrs style:style];
    [self drawActionButton:@"RAND" rect:[self randomButtonRect] attrs:dim style:style];
    [self drawActionButton:@"DEL PT" rect:[self deleteButtonRect] attrs:dim style:style];
    [self drawActionButton:@"CLEAR" rect:[self clearButtonRect] attrs:dim style:style];
    s3g::clap_gui::drawSlider(@"DEV", [NSString stringWithFormat:@"%.0f%%", static_cast<double>(_randomDev * 100.0)], _randomDev, 300, attrs, dim, style, 646.0, 738.0, 826.0, 82.0);
    s3g::clap_gui::drawSlider(@"PTS", [NSString stringWithFormat:@"%u", _randomPoints], (static_cast<CGFloat>(_randomPoints) - 2.0) / 62.0, 324, attrs, dim, style, 646.0, 738.0, 826.0, 82.0);
    [self drawSlider:@"OUTPUT" param:kOutputParamId value:p.outputGainDb min:-60 max:12 y:354 attrs:attrs style:style];

    [self drawMenu:@"PLAY" value:[NSString stringWithUTF8String:playbackName(static_cast<uint32_t>(p.playback))] y:414 attrs:attrs style:style];
    [self drawMenu:@"SYNC" value:[NSString stringWithUTF8String:syncName(static_cast<uint32_t>(p.syncMode))] y:436 attrs:attrs style:style];
    [self drawSlider:@"DIV" param:kDivisionParamId value:p.syncDivisionBeats min:0.25 max:64 y:458 attrs:attrs style:style];
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:loopName(static_cast<uint32_t>(p.loopMode))] y:480 attrs:attrs style:style];
    [self drawMenu:@"INTERP" value:[NSString stringWithUTF8String:interpName(static_cast<uint32_t>(p.interpolation))] y:502 attrs:attrs style:style];
    [self drawSlider:@"RATE" param:kRateParamId value:p.rateHz min:0.001 max:4 y:528 attrs:attrs style:style];
    [self drawSlider:@"PHASE" param:kPhaseParamId value:p.phase min:0 max:1 y:550 attrs:attrs style:style];
    [self drawSlider:@"SPREAD" param:kPhaseSpreadParamId value:p.phaseSpread min:0 max:1 y:572 attrs:attrs style:style];
    [self drawSlider:@"SMOOTH" param:kSmoothParamId value:p.smooth min:0 max:0.995 y:594 attrs:attrs style:style];
    [self drawSlider:@"EASE" param:kEaseParamId value:p.ease min:0 max:1 y:616 attrs:attrs style:style];
    [self drawSlider:@"DIST" param:kDistanceScaleParamId value:p.distanceScale min:0.05 max:8 y:638 attrs:attrs style:style];
    [self drawSlider:@"DOPPLER" param:kDopplerParamId value:p.doppler min:0 max:1 y:660 attrs:attrs style:style];
    [self drawSlider:@"AIR" param:kAirParamId value:p.air min:0 max:1 y:682 attrs:attrs style:style];

    [self drawActionButton:@"LOAD JSON" rect:[self fileButtonRect:0] attrs:dim style:style];
    [self drawActionButton:@"SAVE JSON" rect:[self fileButtonRect:1] attrs:dim style:style];
    [self drawActionButton:@"IMPORT SVG" rect:[self fileButtonRect:2] attrs:dim style:style];
    [self drawActionButton:@"EXPORT SVG" rect:[self fileButtonRect:3] attrs:dim style:style];
    [self drawOpenMenu:attrs style:style];
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)pt
{
    auto slider = [&](double lo, double hi) {
        const double n = std::clamp((pt.x - 738.0) / 82.0, 0.0, 1.0);
        applyParam(*_plugin, param, lo + n * (hi - lo));
    };
    auto logSlider = [&](double lo, double hi) {
        const double n = std::clamp((pt.x - 738.0) / 82.0, 0.0, 1.0);
        applyParam(*_plugin, param, lo * std::pow(hi / lo, n));
    };
    switch (param) {
    case kInputsParamId: slider(1, 64); break;
    case kOrderParamId: slider(1, 7); break;
    case kPathsParamId: slider(1, 16); break;
    case kSelectedPathParamId: slider(1, 16); break;
    case kSelectedSourceParamId: slider(1, 64); break;
    case kRateParamId: logSlider(0.001, 4.0); break;
    case kDivisionParamId: logSlider(0.25, 64.0); break;
    case kPhaseParamId: slider(0, 1); break;
    case kPhaseSpreadParamId: slider(0, 1); break;
    case kSmoothParamId: slider(0, 0.995); break;
    case kEaseParamId: slider(0, 1); break;
    case kDistanceScaleParamId: slider(0.05, 8.0); break;
    case kDopplerParamId: slider(0, 1); break;
    case kAirParamId: slider(0, 1); break;
    case kOutputParamId: slider(-60, 12); break;
    default: break;
    }
    [self setNeedsDisplay:YES];
}

- (NSDictionary*)pathJsonDictionary
{
    const auto paths = _plugin->encoder.paths();
    NSMutableArray* pathList = [NSMutableArray array];
    for (uint32_t pi = 0; pi < _plugin->params.activePaths; ++pi) {
        NSMutableArray* pts = [NSMutableArray array];
        const auto& path = paths[pi];
        for (uint32_t i = 0; i < path.pointCount; ++i) {
            const auto& p = path.points[i];
            [pts addObject:@{ @"x": @(p.x), @"y": @(p.y), @"z": @(p.z), @"time": @(p.time) }];
        }
        [pathList addObject:@{ @"name": [NSString stringWithFormat:@"path %u", pi + 1u], @"points": pts }];
    }
    return @{
        @"type": @"s3g_ambi_path",
        @"version": @1,
        @"paths": pathList
    };
}

- (void)loadPathJson
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"json"]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    NSData* data = [NSData dataWithContentsOfURL:[panel URL]];
    if (!data) return;
    NSError* error = nil;
    id root = [NSJSONSerialization JSONObjectWithData:data options:0 error:&error];
    if (error || ![root isKindOfClass:[NSDictionary class]]) return;
    NSArray* list = [static_cast<NSDictionary*>(root) objectForKey:@"paths"];
    if (![list isKindOfClass:[NSArray class]]) return;
    auto paths = _plugin->encoder.paths();
    const uint32_t pathCount = std::min<uint32_t>(s3g::kAmbiPathEncoderMaxPaths, static_cast<uint32_t>([list count]));
    for (uint32_t pi = 0; pi < pathCount; ++pi) {
        NSDictionary* pd = [[list objectAtIndex:pi] isKindOfClass:[NSDictionary class]] ? static_cast<NSDictionary*>([list objectAtIndex:pi]) : nil;
        NSArray* pts = [pd objectForKey:@"points"];
        if (![pts isKindOfClass:[NSArray class]]) continue;
        paths[pi].pointCount = std::min<uint32_t>(s3g::kAmbiPathEncoderMaxPoints, static_cast<uint32_t>([pts count]));
        for (uint32_t i = 0; i < paths[pi].pointCount; ++i) {
            id obj = [pts objectAtIndex:i];
            if ([obj isKindOfClass:[NSDictionary class]]) paths[pi].points[i] = pointFromDict(static_cast<NSDictionary*>(obj), i, paths[pi].pointCount);
        }
    }
    _plugin->encoder.setPaths(paths);
    applyParam(*_plugin, kPathsParamId, pathCount);
    [self setNeedsDisplay:YES];
}

- (void)savePathJson
{
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:[self pathJsonDictionary] options:NSJSONWritingPrettyPrinted error:&error];
    if (error || !data) return;
    NSSavePanel* panel = [NSSavePanel savePanel];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"json"]];
#pragma clang diagnostic pop
    [panel setNameFieldStringValue:@"s3g-ambi-path.json"];
    if ([panel runModal] != NSModalResponseOK) return;
    [data writeToURL:[panel URL] atomically:YES];
}

- (void)importSvg
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"svg"]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* text = [NSString stringWithContentsOfURL:[panel URL] encoding:NSUTF8StringEncoding error:nil];
    if (![text length]) return;
    NSMutableArray<NSString*>* tags = [NSMutableArray array];
    NSRegularExpression* re = [NSRegularExpression regularExpressionWithPattern:@"<(path|polyline|polygon)\\b[^>]*>" options:NSRegularExpressionCaseInsensitive error:nil];
    [re enumerateMatchesInString:text options:0 range:NSMakeRange(0, [text length]) usingBlock:^(NSTextCheckingResult* result, NSMatchingFlags, BOOL*) {
        if (result) [tags addObject:[text substringWithRange:[result range]]];
    }];
    if (![tags count]) return;
    auto paths = _plugin->encoder.paths();
    const uint32_t pathCount = std::min<uint32_t>(s3g::kAmbiPathEncoderMaxPaths, static_cast<uint32_t>([tags count]));
    for (uint32_t i = 0; i < pathCount; ++i) {
        NSString* tag = [tags objectAtIndex:i];
        NSString* data = attributeValue(tag, @"d");
        if (![data length]) data = attributeValue(tag, @"points");
        paths[i] = pathFromXYNumbers(numbersFromString(data));
    }
    _plugin->encoder.setPaths(paths);
    applyParam(*_plugin, kPathsParamId, pathCount);
    [self setNeedsDisplay:YES];
}

- (void)exportSvg
{
    const auto paths = _plugin->encoder.paths();
    NSMutableString* out = [NSMutableString stringWithString:@"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1000\" height=\"1000\" viewBox=\"0 0 1000 1000\">\n<metadata>s3g-ambi-path-svg-v1; JSON remains the canonical format.</metadata>\n<g fill=\"none\" stroke=\"#444\" stroke-width=\"2\">\n"];
    for (uint32_t pi = 0; pi < _plugin->params.activePaths; ++pi) {
        const auto& path = paths[pi];
        if (!path.pointCount) continue;
        [out appendFormat:@"<polyline data-path=\"%u\" points=\"", pi + 1u];
        for (uint32_t i = 0; i < path.pointCount; ++i) {
            const auto& p = path.points[i];
            const double x = 500.0 + p.x * 400.0;
            const double y = 500.0 - p.y * 400.0;
            [out appendFormat:@"%.3f,%.3f ", x, y];
        }
        [out appendString:@"\"/>\n"];
    }
    [out appendString:@"</g>\n</svg>\n"];
    NSSavePanel* panel = [NSSavePanel savePanel];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"svg"]];
#pragma clang diagnostic pop
    [panel setNameFieldStringValue:@"s3g-ambi-path.svg"];
    if ([panel runModal] != NSModalResponseOK) return;
    [out writeToURL:[panel URL] atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const CGFloat itemH = 18.0;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738, [self menuY], [self menuW], itemH * _menuItemCount), itemH, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 2) applyParam(*_plugin, kAssignParamId, hit);
            else if (_openMenu == 3) applyParam(*_plugin, kPlaybackParamId, hit);
            else if (_openMenu == 4) applyParam(*_plugin, kSyncParamId, hit);
            else if (_openMenu == 5) applyParam(*_plugin, kLoopParamId, hit);
            else if (_openMenu == 6) applyParam(*_plugin, kInterpParamId, hit);
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
    if (NSPointInRect(pt, [self fileButtonRect:0])) { [self loadPathJson]; return; }
    if (NSPointInRect(pt, [self fileButtonRect:1])) { [self savePathJson]; return; }
    if (NSPointInRect(pt, [self fileButtonRect:2])) { [self importSvg]; return; }
    if (NSPointInRect(pt, [self fileButtonRect:3])) { [self exportSvg]; return; }
    if (NSPointInRect(pt, [self randomButtonRect])) { [self randomizePaths]; return; }
    if (NSPointInRect(pt, [self deleteButtonRect])) { [self deleteSelectedPoint]; return; }
    if (NSPointInRect(pt, [self clearButtonRect])) { [self clearSelectedPath]; return; }
    if (NSPointInRect(pt, NSInsetRect([self randomDevSliderRect], -40.0, -8.0))) { _dragRandomDev = YES; [self updateRandomDev:pt]; return; }
    if (NSPointInRect(pt, NSInsetRect([self randomPointsSliderRect], -40.0, -8.0))) { _dragRandomPoints = YES; [self updateRandomPoints:pt]; return; }

    const NSRect fieldPanel = [self fieldPanelRect];
    if (NSPointInRect(pt, fieldPanel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self zoomButtonRect:i inRect:fieldPanel])) {
                _viewZoom = std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.4);
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
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(pt, [self editButtonRect:i inRect:fieldPanel])) {
                _editMode = (i == 0);
                _dragPoint = NO;
                _dragView = NO;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        const NSRect field = [self fieldRect];
        if (NSPointInRect(pt, field)) {
            const int hit = [self hitPathPoint:pt inRect:field];
            if (hit >= 0) {
                _selectedPoint = hit;
                _dragPoint = _editMode;
                [self setNeedsDisplay:YES];
                if (_editMode) return;
            }
            if (_editMode) {
                [self addPointAtScreen:pt];
                _dragPoint = YES;
                [self setNeedsDisplay:YES];
                return;
            }
            if (_editMode) return;
            _dragView = YES;
            _lastDragPoint = pt;
            _viewMode = -1;
            [self storeViewState];
            return;
        }
    }

    _dragParam = 0;
    if (NSPointInRect(pt, NSMakeRect(638, 70, 230, 24))) _dragParam = kInputsParamId;
    else if (NSPointInRect(pt, NSMakeRect(738, 103, 126, 17))) { openMenu(1, 7); return; }
    else if (NSPointInRect(pt, NSMakeRect(638, 122, 230, 24))) _dragParam = kPathsParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 148, 230, 24))) { _selectedPoint = -1; _dragParam = kSelectedPathParamId; }
    else if (NSPointInRect(pt, NSMakeRect(638, 174, 230, 24))) _dragParam = kSelectedSourceParamId;
    else if (NSPointInRect(pt, NSMakeRect(738, 207, 126, 17))) { openMenu(2, 3); return; }
    else if (NSPointInRect(pt, NSMakeRect(638, 346, 230, 24))) _dragParam = kOutputParamId;
    else if (NSPointInRect(pt, NSMakeRect(738, 413, 126, 17))) { openMenu(3, 3); return; }
    else if (NSPointInRect(pt, NSMakeRect(738, 435, 126, 17))) { openMenu(4, 2); return; }
    else if (NSPointInRect(pt, NSMakeRect(638, 450, 230, 24))) _dragParam = kDivisionParamId;
    else if (NSPointInRect(pt, NSMakeRect(738, 479, 126, 17))) { openMenu(5, 3); return; }
    else if (NSPointInRect(pt, NSMakeRect(738, 501, 126, 17))) { openMenu(6, 3); return; }
    else if (NSPointInRect(pt, NSMakeRect(638, 520, 230, 24))) _dragParam = kRateParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 542, 230, 24))) _dragParam = kPhaseParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 564, 230, 24))) _dragParam = kPhaseSpreadParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 586, 230, 24))) _dragParam = kSmoothParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 608, 230, 24))) _dragParam = kEaseParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 630, 230, 24))) _dragParam = kDistanceScaleParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 652, 230, 24))) _dragParam = kDopplerParamId;
    else if (NSPointInRect(pt, NSMakeRect(638, 674, 230, 24))) _dragParam = kAirParamId;
    if (_dragParam) [self setParam:_dragParam fromPoint:pt];
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragPoint) {
        [self updateSelectedPointFromScreen:pt];
        return;
    }
    if (_dragRandomDev) {
        [self updateRandomDev:pt];
        return;
    }
    if (_dragRandomPoints) {
        [self updateRandomPoints:pt];
        return;
    }
    if (_dragView) {
        const CGFloat dx = pt.x - _lastDragPoint.x;
        const CGFloat dy = pt.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.45;
        _viewElDeg = std::clamp<CGFloat>(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = pt;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam) [self setParam:_dragParam fromPoint:pt];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = 0;
    _dragView = NO;
    _dragPoint = NO;
    _dragRandomDev = NO;
    _dragRandomPoints = NO;
}

- (void)keyDown:(NSEvent*)event
{
    NSString* chars = [event charactersIgnoringModifiers] ?: @"";
    if ([chars length] > 0) {
        const unichar c = [chars characterAtIndex:0];
        if (c == NSDeleteCharacter || c == NSBackspaceCharacter) {
            [self deleteSelectedPoint];
            return;
        }
    }
    [super keyDown:event];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat itemH = 18.0;
    const int next = s3g::clap_gui::dropdownHitIndex(pt, NSMakeRect(738, [self menuY], [self menuW], itemH * _menuItemCount), itemH, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
@end

namespace {
bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool) { return std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool)
{
    if (!api || std::strcmp(api, CLAP_WINDOW_API_COCOA) != 0) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiPathEncoderView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), 900u, 792u)) {
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
    [static_cast<S3GAmbiPathEncoderView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible = false;
}
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, 900u, 792u, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, 900u, 792u, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win)
{
    if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(win->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible = true; [static_cast<S3GAmbiPathEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible = false; [static_cast<S3GAmbiPathEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
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

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-path-encoder-64",
    "s3g Ambi Path Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.0-pre",
    "64-input ambisonic encoder driven by drawable and importable 3D paths.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->encoder.prepare(p->sampleRate);
    p->encoder.setParams(p->params);
    p->params = p->encoder.params();
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
    p->plugin.on_main_thread = [](const clap_plugin_t*) {};
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
const void* entryGetFactory(const char* id) { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }
} // namespace

extern "C" const CLAP_EXPORT clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
