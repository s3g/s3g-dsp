#include "s3g_ambi_acid_encoder.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_gui_param_queue.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_gui_layout.h"
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

constexpr uint32_t kOutputChannels = s3g::kAmbiAcidChannels;
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 560u;

constexpr clap_id kOrderParamId = 1u;
constexpr clap_id kTempoParamId = 2u;
constexpr clap_id kDivisionParamId = 3u;
constexpr clap_id kLengthParamId = 4u;
constexpr clap_id kRootParamId = 5u;
constexpr clap_id kGateLengthParamId = 6u;
constexpr clap_id kWaveParamId = 7u;
constexpr clap_id kPulseWidthParamId = 8u;
constexpr clap_id kCutoffParamId = 9u;
constexpr clap_id kResonanceParamId = 10u;
constexpr clap_id kFilterEnvelopeParamId = 11u;
constexpr clap_id kFilterDecayParamId = 12u;
constexpr clap_id kAccentParamId = 13u;
constexpr clap_id kDriveParamId = 14u;
constexpr clap_id kSlideParamId = 15u;
constexpr clap_id kCenterAzimuthParamId = 16u;
constexpr clap_id kPathTurnsParamId = 17u;
constexpr clap_id kElevationSpreadParamId = 18u;
constexpr clap_id kSpatialSpreadParamId = 19u;
constexpr clap_id kEdgeLeadParamId = 20u;
constexpr clap_id kWakeAmountParamId = 21u;
constexpr clap_id kWakeTimeParamId = 22u;
constexpr clap_id kListenModeParamId = 23u;
constexpr clap_id kListenAmountParamId = 24u;
constexpr clap_id kListenMemoryParamId = 25u;
constexpr clap_id kOutputParamId = 26u;
constexpr clap_id kTransportSyncParamId = 27u;
constexpr uint32_t kBaseParamCount = 27u;

constexpr clap_id kStepParamBase = 100u;
constexpr uint32_t kStepParamStride = 4u;
constexpr uint32_t kStepParamCount =
    s3g::kAmbiAcidStepCount * kStepParamStride;
constexpr uint32_t kParamCount = kBaseParamCount + kStepParamCount;

enum class StepParamKind : uint32_t {
    Note = 0u,
    Gate = 1u,
    Accent = 2u,
    Slide = 3u,
};

struct ParamSpec {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamSpec, kBaseParamCount> kParamSpecs {{
    { kOrderParamId, "Order", "Output", 1.0, 3.0, 3.0, true },
    { kTempoParamId, "Tempo", "Line", 30.0, 300.0, 126.0, false },
    { kDivisionParamId, "Steps per Beat", "Line", 1.0, 8.0, 4.0, true },
    { kLengthParamId, "Pattern Length", "Line", 1.0, 16.0, 16.0, true },
    { kRootParamId, "Root Note", "Line", 12.0, 72.0, 36.0, true },
    { kGateLengthParamId, "Gate Length", "Line", 0.05, 1.0, 0.58, false },
    { kWaveParamId, "Wave Saw-Pulse", "Voice", 0.0, 1.0, 0.16, false },
    { kPulseWidthParamId, "Pulse Width", "Voice", 0.12, 0.88, 0.50, false },
    { kCutoffParamId, "Cutoff", "Voice", 30.0, 12000.0, 310.0, false },
    { kResonanceParamId, "Resonance", "Voice", 0.0, 1.0, 0.78, false },
    { kFilterEnvelopeParamId, "Filter Envelope", "Voice", 0.0, 7.0, 3.25, false },
    { kFilterDecayParamId, "Filter Decay", "Voice", 20.0, 2000.0, 185.0, false },
    { kAccentParamId, "Accent", "Voice", 0.0, 1.0, 0.78, false },
    { kDriveParamId, "Drive", "Voice", 0.0, 1.0, 0.46, false },
    { kSlideParamId, "Slide Time", "Voice", 5.0, 500.0, 82.0, false },
    { kCenterAzimuthParamId, "Center Azimuth", "Gesture", -180.0, 180.0, 0.0, false },
    { kPathTurnsParamId, "Path Turns", "Gesture", -4.0, 4.0, 1.0, false },
    { kElevationSpreadParamId, "Elevation Spread", "Gesture", 0.0, 70.0, 22.0, false },
    { kSpatialSpreadParamId, "Spatial Spread", "Gesture", 0.0, 1.0, 0.78, false },
    { kEdgeLeadParamId, "Edge Lead", "Gesture", -90.0, 90.0, 24.0, false },
    { kWakeAmountParamId, "Wake", "Gesture", 0.0, 1.0, 0.52, false },
    { kWakeTimeParamId, "Wake Time", "Gesture", 5.0, 240.0, 92.0, false },
    { kListenModeParamId, "Listener Mode", "Listener", 0.0, 3.0, 0.0, true },
    { kListenAmountParamId, "Listener Amount", "Listener", 0.0, 1.0, 0.62, false },
    { kListenMemoryParamId, "Listener Memory", "Listener", 0.05, 4.0, 0.56, false },
    { kOutputParamId, "Output", "Output", -36.0, 6.0, -10.0, false },
    { kTransportSyncParamId, "Clock Source", "Line", 0.0, 1.0, 0.0, true },
}};

struct LegacySavedState {
    uint32_t version = 1u;
    s3g::AmbiAcidParams params {};
    std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount> pattern =
        s3g::kAmbiAcidChromeBurrowPattern;
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiAcidParams params {};
    std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount> pattern =
        s3g::kAmbiAcidChromeBurrowPattern;
    uint32_t transportSync = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiAcidEncoder engine {};
    s3g::AmbiAcidParams params {};
    bool transportSync = false;
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<uint32_t> visualCurrentStep { 0u };
    std::atomic<float> visualActivity { 0.0f };
    std::atomic<float> visualListenerActivity { 0.0f };
    std::atomic<float> visualDirectionX { 1.0f };
    std::atomic<float> visualDirectionY { 0.0f };
    std::atomic<float> visualDirectionZ { 0.0f };
    std::atomic<bool> visualTransportPlaying { false };
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

bool decodeStepParam(clap_id id, uint32_t& step, StepParamKind& kind)
{
    if (id < kStepParamBase) return false;
    const uint32_t relative = id - kStepParamBase;
    step = relative / kStepParamStride;
    const uint32_t kindValue = relative % kStepParamStride;
    if (step >= s3g::kAmbiAcidStepCount
        || kindValue > static_cast<uint32_t>(StepParamKind::Slide)) {
        return false;
    }
    kind = static_cast<StepParamKind>(kindValue);
    return true;
}

uint32_t paramIndex(clap_id id)
{
    if (id >= kOrderParamId && id <= kTransportSyncParamId) {
        return id - kOrderParamId;
    }
    uint32_t step = 0u;
    StepParamKind kind = StepParamKind::Note;
    if (!decodeStepParam(id, step, kind)) return kParamCount;
    return kBaseParamCount + step * kStepParamStride
        + static_cast<uint32_t>(kind);
}

double clampParamValue(clap_id id, double value)
{
    const uint32_t index = paramIndex(id);
    if (index >= kParamCount) return 0.0;
    double minimum = 0.0;
    double maximum = 1.0;
    double fallback = 0.0;
    bool stepped = true;
    if (index < kBaseParamCount) {
        const auto& spec = kParamSpecs[index];
        minimum = spec.minimum;
        maximum = spec.maximum;
        fallback = spec.defaultValue;
        stepped = spec.stepped;
    } else {
        const uint32_t relative = index - kBaseParamCount;
        const uint32_t step = relative / kStepParamStride;
        const auto kind = static_cast<StepParamKind>(
            relative % kStepParamStride);
        if (kind == StepParamKind::Note) {
            minimum = -36.0;
            maximum = 36.0;
            fallback = s3g::kAmbiAcidChromeBurrowPattern[step]
                .semitoneOffset;
        } else {
            fallback = kind == StepParamKind::Gate
                    ? (s3g::kAmbiAcidChromeBurrowPattern[step].gate
                        ? 1.0 : 0.0)
                : kind == StepParamKind::Accent
                    ? (s3g::kAmbiAcidChromeBurrowPattern[step].accent
                        ? 1.0 : 0.0)
                    : (s3g::kAmbiAcidChromeBurrowPattern[step].slide
                        ? 1.0 : 0.0);
        }
    }
    value = std::isfinite(value) ? value : fallback;
    value = std::clamp(value, minimum, maximum);
    return stepped ? std::round(value) : value;
}

void publishParam(Plugin& plugin, clap_id id, double value)
{
    const uint32_t index = paramIndex(id);
    if (index >= kParamCount) return;
    plugin.publishedParams[index].store(value, std::memory_order_release);
}

double paramValue(const Plugin& plugin, clap_id id)
{
    const uint32_t index = paramIndex(id);
    if (index >= kParamCount) return 0.0;
    return plugin.publishedParams[index].load(std::memory_order_acquire);
}

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t position = 0u;
    while (position < size) {
        const int64_t written = stream->write(
            stream, bytes + position, size - position);
        if (written <= 0) return false;
        position += static_cast<size_t>(written);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t position = 0u;
    while (position < size) {
        const int64_t read = stream->read(
            stream, bytes + position, size - position);
        if (read <= 0) return false;
        position += static_cast<size_t>(read);
    }
    return true;
}

double rawParamValue(const Plugin& plugin, clap_id id);

bool applyParam(Plugin& plugin, clap_id id, double value)
{
    if (paramIndex(id) >= kParamCount) return false;
    value = clampParamValue(id, value);
    uint32_t stepIndex = 0u;
    StepParamKind stepKind = StepParamKind::Note;
    if (decodeStepParam(id, stepIndex, stepKind)) {
        auto step = plugin.engine.step(stepIndex);
        switch (stepKind) {
        case StepParamKind::Note:
            step.semitoneOffset = static_cast<int32_t>(std::lround(value));
            break;
        case StepParamKind::Gate:
            step.gate = value >= 0.5;
            break;
        case StepParamKind::Accent:
            step.accent = value >= 0.5;
            break;
        case StepParamKind::Slide:
            step.slide = value >= 0.5;
            break;
        }
        plugin.engine.setStep(stepIndex, step);
        publishParam(plugin, id, value);
        return false;
    }

    switch (id) {
    case kOrderParamId: plugin.params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kTempoParamId: plugin.params.tempoBpm = static_cast<float>(value); break;
    case kDivisionParamId: plugin.params.stepsPerBeat = static_cast<uint32_t>(std::lround(value)); break;
    case kLengthParamId: plugin.params.patternLength = static_cast<uint32_t>(std::lround(value)); break;
    case kRootParamId: plugin.params.rootMidiNote = static_cast<int32_t>(std::lround(value)); break;
    case kGateLengthParamId: plugin.params.gateLength = static_cast<float>(value); break;
    case kWaveParamId: plugin.params.waveShape = static_cast<float>(value); break;
    case kPulseWidthParamId: plugin.params.pulseWidth = static_cast<float>(value); break;
    case kCutoffParamId: plugin.params.cutoffHz = static_cast<float>(value); break;
    case kResonanceParamId: plugin.params.resonance = static_cast<float>(value); break;
    case kFilterEnvelopeParamId: plugin.params.filterEnvelopeOctaves = static_cast<float>(value); break;
    case kFilterDecayParamId: plugin.params.filterDecayMs = static_cast<float>(value); break;
    case kAccentParamId: plugin.params.accentAmount = static_cast<float>(value); break;
    case kDriveParamId: plugin.params.drive = static_cast<float>(value); break;
    case kSlideParamId: plugin.params.slideMs = static_cast<float>(value); break;
    case kCenterAzimuthParamId: plugin.params.centerAzimuthDeg = static_cast<float>(value); break;
    case kPathTurnsParamId: plugin.params.pathTurns = static_cast<float>(value); break;
    case kElevationSpreadParamId: plugin.params.elevationSpreadDeg = static_cast<float>(value); break;
    case kSpatialSpreadParamId: plugin.params.spatialSpread = static_cast<float>(value); break;
    case kEdgeLeadParamId: plugin.params.edgeLeadDeg = static_cast<float>(value); break;
    case kWakeAmountParamId: plugin.params.wakeAmount = static_cast<float>(value); break;
    case kWakeTimeParamId: plugin.params.wakeMs = static_cast<float>(value); break;
    case kListenModeParamId:
        plugin.params.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kListenAmountParamId: plugin.params.fieldListenAmount = static_cast<float>(value); break;
    case kListenMemoryParamId: plugin.params.listenerMemorySeconds = static_cast<float>(value); break;
    case kOutputParamId: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kTransportSyncParamId: plugin.transportSync = value >= 0.5; break;
    default: return false;
    }
    publishParam(plugin, id, rawParamValue(plugin, id));
    return true;
}

double rawParamValue(const Plugin& plugin, clap_id id)
{
    uint32_t stepIndex = 0u;
    StepParamKind stepKind = StepParamKind::Note;
    if (decodeStepParam(id, stepIndex, stepKind)) {
        const auto step = plugin.engine.step(stepIndex);
        switch (stepKind) {
        case StepParamKind::Note: return step.semitoneOffset;
        case StepParamKind::Gate: return step.gate ? 1.0 : 0.0;
        case StepParamKind::Accent: return step.accent ? 1.0 : 0.0;
        case StepParamKind::Slide: return step.slide ? 1.0 : 0.0;
        }
    }
    const auto& params = plugin.params;
    switch (id) {
    case kOrderParamId: return params.order;
    case kTempoParamId: return params.tempoBpm;
    case kDivisionParamId: return params.stepsPerBeat;
    case kLengthParamId: return params.patternLength;
    case kRootParamId: return params.rootMidiNote;
    case kGateLengthParamId: return params.gateLength;
    case kWaveParamId: return params.waveShape;
    case kPulseWidthParamId: return params.pulseWidth;
    case kCutoffParamId: return params.cutoffHz;
    case kResonanceParamId: return params.resonance;
    case kFilterEnvelopeParamId: return params.filterEnvelopeOctaves;
    case kFilterDecayParamId: return params.filterDecayMs;
    case kAccentParamId: return params.accentAmount;
    case kDriveParamId: return params.drive;
    case kSlideParamId: return params.slideMs;
    case kCenterAzimuthParamId: return params.centerAzimuthDeg;
    case kPathTurnsParamId: return params.pathTurns;
    case kElevationSpreadParamId: return params.elevationSpreadDeg;
    case kSpatialSpreadParamId: return params.spatialSpread;
    case kEdgeLeadParamId: return params.edgeLeadDeg;
    case kWakeAmountParamId: return params.wakeAmount;
    case kWakeTimeParamId: return params.wakeMs;
    case kListenModeParamId: return static_cast<uint32_t>(params.fieldListenMode);
    case kListenAmountParamId: return params.fieldListenAmount;
    case kListenMemoryParamId: return params.listenerMemorySeconds;
    case kOutputParamId: return params.outputGainDb;
    case kTransportSyncParamId: return plugin.transportSync ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void publishAllParams(Plugin& plugin)
{
    for (const auto& spec : kParamSpecs) {
        publishParam(plugin, spec.id, rawParamValue(plugin, spec.id));
    }
    for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
        for (uint32_t kind = 0u; kind < kStepParamStride; ++kind) {
            const clap_id id = kStepParamBase + step * kStepParamStride + kind;
            publishParam(plugin, id, rawParamValue(plugin, id));
        }
    }
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->host && p->host->get_extension) {
        p->hostParams = static_cast<const clap_host_params_t*>(
            p->host->get_extension(p->host, CLAP_EXT_PARAMS));
    }
    return true;
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiParamEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!plugin.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(plugin);
    return true;
}

void queueGuiParamGestureBegin(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    value = clampParamValue(id, value);
    publishParam(plugin, id, value);
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queueGuiParamGesture(Plugin& plugin, clap_id id, double value)
{
    queueGuiParamGestureBegin(plugin, id);
    queueGuiParamValue(plugin, id, value);
    queueGuiParamGestureEnd(plugin, id);
}

bool queueGuiPattern(Plugin& plugin,
    const std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount>& pattern)
{
    constexpr uint32_t eventsPerParam = 3u;
    constexpr uint32_t patternParamCount =
        s3g::kAmbiAcidStepCount * kStepParamStride;
    std::array<s3g::clap_gui::ParamEvent,
        patternParamCount * eventsPerParam> events {};
    uint32_t eventIndex = 0u;
    const auto append = [&](clap_id id, double value) {
        events[eventIndex++] = {
            s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 };
        events[eventIndex++] = {
            s3g::clap_gui::ParamEventKind::Value, id, value };
        events[eventIndex++] = {
            s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 };
        publishParam(plugin, id, value);
    };
    for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
        const clap_id base = kStepParamBase + step * kStepParamStride;
        append(base, pattern[step].semitoneOffset);
        append(base + 1u, pattern[step].gate ? 1.0 : 0.0);
        append(base + 2u, pattern[step].accent ? 1.0 : 0.0);
        append(base + 3u, pattern[step].slide ? 1.0 : 0.0);
    }
    if (!plugin.guiParamEvents.pushBatch(events.data(), eventIndex)) {
        publishAllParams(plugin);
        return false;
    }
    requestGuiParamService(plugin);
    return true;
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
    if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = pending.value;
        return output->try_push(output, &event.header);
    }
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = pending.kind
            == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return output->try_push(output, &event.header);
}

void serviceGuiParamEvents(Plugin& plugin, const clap_output_events_t* output)
{
    bool globalsChanged = false;
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            globalsChanged = applyParam(
                plugin, pending.paramId, pending.value) || globalsChanged;
        }
        plugin.guiParamEvents.pop();
    }
    if (globalsChanged) {
        plugin.engine.setParams(plugin.params);
        plugin.params = plugin.engine.params();
        publishAllParams(plugin);
    }
}

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
    p->engine.prepare(sampleRate);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
    publishAllParams(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->engine.reset();
    p->visualCurrentStep.store(0u, std::memory_order_relaxed);
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->visualListenerActivity.store(0.0f, std::memory_order_relaxed);
}

struct TransportClock {
    bool playing = false;
    bool hasPosition = false;
    double beat = 0.0;
    double tempo = 120.0;
    double tempoIncrement = 0.0;
};

void updateTransportClock(TransportClock& clock,
    const clap_event_transport_t* transport, double fallbackTempo)
{
    if (!transport) {
        clock = {};
        clock.tempo = fallbackTempo;
        return;
    }
    clock.playing = (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    clock.tempo = (transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
            && std::isfinite(transport->tempo) && transport->tempo > 0.0
        ? transport->tempo : fallbackTempo;
    clock.tempoIncrement = std::isfinite(transport->tempo_inc)
        ? transport->tempo_inc : 0.0;
    if ((transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u) {
        clock.beat = static_cast<double>(transport->song_pos_beats)
            / static_cast<double>(CLAP_BEATTIME_FACTOR);
        clock.hasPosition = std::isfinite(clock.beat);
    } else if ((transport->flags
            & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0u) {
        const double seconds = static_cast<double>(
            transport->song_pos_seconds)
            / static_cast<double>(CLAP_SECTIME_FACTOR);
        clock.beat = seconds * clock.tempo / 60.0;
        clock.hasPosition = std::isfinite(clock.beat);
    } else {
        clock.beat = 0.0;
        clock.hasPosition = false;
    }
}

void advanceTransportClock(TransportClock& clock, double sampleRate)
{
    if (clock.hasPosition && clock.playing) {
        clock.beat += clock.tempo / (60.0 * sampleRate);
    }
    clock.tempo = std::max(1.0, clock.tempo + clock.tempoIncrement);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* p = self(plugin);
    if (!processData) return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(*p, processData->out_events);
    TransportClock transportClock;
    updateTransportClock(transportClock,
        processData->transport, p->params.tempoBpm);
    if (processData->audio_outputs_count == 0u) {
        p->visualTransportPlaying.store(false, std::memory_order_relaxed);
        return CLAP_PROCESS_CONTINUE;
    }
    auto& output = processData->audio_outputs[0u];
    const uint32_t outputChannels = std::min<uint32_t>(
        output.channel_count, kOutputChannels);
    s3g::clearAudioBufferFromChannel(output, 0u, processData->frames_count);
    if ((!output.data32 && !output.data64) || outputChannels == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }

    uint32_t eventIndex = 0u;
    const uint32_t eventCount = processData->in_events
        ? processData->in_events->size(processData->in_events) : 0u;
    std::array<float, kOutputChannels> frame {};
    for (uint32_t sample = 0u; sample < processData->frames_count; ++sample) {
        bool paramsChanged = false;
        while (eventIndex < eventCount) {
            const clap_event_header_t* event = processData->in_events->get(
                processData->in_events, eventIndex);
            if (!event || event->time > sample) break;
            if (event->space_id == CLAP_CORE_EVENT_SPACE_ID
                && event->type == CLAP_EVENT_PARAM_VALUE
                && event->size >= sizeof(clap_event_param_value_t)) {
                const auto* param =
                    reinterpret_cast<const clap_event_param_value_t*>(event);
                paramsChanged = applyParam(*p, param->param_id, param->value)
                    || paramsChanged;
            } else if (event->space_id == CLAP_CORE_EVENT_SPACE_ID
                && event->type == CLAP_EVENT_TRANSPORT
                && event->size >= sizeof(clap_event_transport_t)) {
                updateTransportClock(transportClock,
                    reinterpret_cast<const clap_event_transport_t*>(event),
                    p->params.tempoBpm);
            }
            ++eventIndex;
        }
        if (paramsChanged) {
            p->engine.setParams(p->params);
            p->params = p->engine.params();
        }
        if (p->transportSync) {
            p->engine.processFrameSynced(frame.data(), outputChannels,
                transportClock.beat,
                transportClock.playing && transportClock.hasPosition);
        } else {
            p->engine.processFrame(frame.data(), outputChannels);
        }
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][sample] = frame[channel];
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][sample] = frame[channel];
            }
        }
        advanceTransportClock(transportClock, p->sampleRate);
    }
    const auto direction = p->engine.targetDirection();
    p->visualCurrentStep.store(
        p->engine.currentStep(), std::memory_order_relaxed);
    p->visualActivity.store(
        p->engine.activity(), std::memory_order_relaxed);
    p->visualListenerActivity.store(
        p->engine.fieldListenActivity(), std::memory_order_relaxed);
    p->visualDirectionX.store(direction.x, std::memory_order_relaxed);
    p->visualDirectionY.store(direction.y, std::memory_order_relaxed);
    p->visualDirectionZ.store(direction.z, std::memory_order_relaxed);
    p->visualTransportPlaying.store(p->transportSync
            && transportClock.playing && transportClock.hasPosition,
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    *info = {};
    info->id = 20u;
    std::strncpy(info->name, "3OA ACN/SN3D Out", sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount,
    audioPortsGet,
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    *info = {};
    if (index < kBaseParamCount) {
        const auto& spec = kParamSpecs[index];
        info->id = spec.id;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE
            | (spec.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
        std::strncpy(info->name, spec.name, sizeof(info->name) - 1u);
        std::strncpy(info->module, spec.module, sizeof(info->module) - 1u);
        info->min_value = spec.minimum;
        info->max_value = spec.maximum;
        info->default_value = spec.defaultValue;
        return true;
    }

    const uint32_t relative = index - kBaseParamCount;
    const uint32_t step = relative / kStepParamStride;
    const auto kind = static_cast<StepParamKind>(relative % kStepParamStride);
    info->id = kStepParamBase + step * kStepParamStride
        + static_cast<uint32_t>(kind);
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED;
    std::snprintf(info->module, sizeof(info->module),
        "Pattern/Step %02u", step + 1u);
    switch (kind) {
    case StepParamKind::Note:
        std::strncpy(info->name, "Note Offset", sizeof(info->name) - 1u);
        info->min_value = -36.0;
        info->max_value = 36.0;
        info->default_value =
            s3g::kAmbiAcidChromeBurrowPattern[step].semitoneOffset;
        break;
    case StepParamKind::Gate:
        std::strncpy(info->name, "Gate", sizeof(info->name) - 1u);
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value =
            s3g::kAmbiAcidChromeBurrowPattern[step].gate ? 1.0 : 0.0;
        break;
    case StepParamKind::Accent:
        std::strncpy(info->name, "Accent", sizeof(info->name) - 1u);
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value =
            s3g::kAmbiAcidChromeBurrowPattern[step].accent ? 1.0 : 0.0;
        break;
    case StepParamKind::Slide:
        std::strncpy(info->name, "Slide", sizeof(info->name) - 1u);
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value =
            s3g::kAmbiAcidChromeBurrowPattern[step].slide ? 1.0 : 0.0;
        break;
    }
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    bool known = id >= 1u && id <= kBaseParamCount;
    uint32_t step = 0u;
    StepParamKind kind = StepParamKind::Note;
    known = known || decodeStepParam(id, step, kind);
    if (!known) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    uint32_t step = 0u;
    StepParamKind stepKind = StepParamKind::Note;
    if (decodeStepParam(id, step, stepKind)) {
        if (stepKind == StepParamKind::Note) {
            std::snprintf(display, size, "%+.0f st", value);
        } else {
            std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off");
        }
        return true;
    }
    if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kTempoParamId) {
        std::snprintf(display, size, "%.1f BPM", value);
    } else if (id == kRootParamId) {
        static constexpr const char* names[] {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B",
        };
        const int note = std::clamp(static_cast<int>(std::lround(value)), 0, 127);
        std::snprintf(display, size, "%s%d", names[note % 12], note / 12 - 1);
    } else if (id == kListenModeParamId) {
        static constexpr const char* names[] {
            "Off", "Follow", "Counter", "Balance",
        };
        const uint32_t mode = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 0u, 3u);
        std::snprintf(display, size, "%s", names[mode]);
    } else if (id == kTransportSyncParamId) {
        std::snprintf(display, size, "%s",
            value >= 0.5 ? "Host" : "Internal");
    } else if (id == kCutoffParamId) {
        std::snprintf(display, size, "%.0f Hz", value);
    } else if (id == kFilterDecayParamId || id == kSlideParamId
        || id == kWakeTimeParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kListenMemoryParamId) {
        std::snprintf(display, size, "%.2f s", value);
    } else if (id == kCenterAzimuthParamId || id == kElevationSpreadParamId
        || id == kEdgeLeadParamId) {
        std::snprintf(display, size, "%.1f deg", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%.1f dB", value);
    } else if (id >= 1u && id <= kBaseParamCount) {
        std::snprintf(display, size, "%.3g", value);
    } else {
        return false;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kRootParamId) {
        int semitone = -1;
        switch (display[0]) {
        case 'C': case 'c': semitone = 0; break;
        case 'D': case 'd': semitone = 2; break;
        case 'E': case 'e': semitone = 4; break;
        case 'F': case 'f': semitone = 5; break;
        case 'G': case 'g': semitone = 7; break;
        case 'A': case 'a': semitone = 9; break;
        case 'B': case 'b': semitone = 11; break;
        default: break;
        }
        if (semitone >= 0) {
            const char* octaveText = display + 1;
            if (*octaveText == '#') {
                semitone = (semitone + 1) % 12;
                ++octaveText;
            }
            char* end = nullptr;
            const long octave = std::strtol(octaveText, &end, 10);
            if (end != octaveText) {
                *value = std::clamp<long>((octave + 1) * 12 + semitone,
                    0l, 127l);
                return true;
            }
        }
        *value = std::atof(display);
        return true;
    }
    if (id == kListenModeParamId) {
        if (std::strcmp(display, "Off") == 0) *value = 0.0;
        else if (std::strcmp(display, "Follow") == 0) *value = 1.0;
        else if (std::strcmp(display, "Counter") == 0) *value = 2.0;
        else if (std::strcmp(display, "Balance") == 0) *value = 3.0;
        else return false;
        return true;
    }
    if (id == kTransportSyncParamId) {
        if (std::strcmp(display, "Host") == 0) *value = 1.0;
        else if (std::strcmp(display, "Internal") == 0) *value = 0.0;
        else *value = std::atof(display);
        return true;
    }
    uint32_t step = 0u;
    StepParamKind kind = StepParamKind::Note;
    if (decodeStepParam(id, step, kind) && kind != StepParamKind::Note) {
        if (std::strcmp(display, "On") == 0) *value = 1.0;
        else if (std::strcmp(display, "Off") == 0) *value = 0.0;
        else *value = std::atof(display);
        return true;
    }
    if (!(id >= 1u && id <= kBaseParamCount)
        && !decodeStepParam(id, step, kind)) {
        return false;
    }
    *value = std::atof(display);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* p = self(plugin);
    bool paramsChanged = false;
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = input->get(input, index);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param =
                reinterpret_cast<const clap_event_param_value_t*>(event);
            paramsChanged = applyParam(*p, param->param_id, param->value)
                || paramsChanged;
        }
    }
    if (paramsChanged) {
        p->engine.setParams(p->params);
        p->params = p->engine.params();
        publishAllParams(*p);
    }
    serviceGuiParamEvents(*p, output);
}

const clap_plugin_params_t params {
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
    const auto* p = self(plugin);
    const SavedState state {
        kStateVersion,
        p->params,
        p->engine.pattern(),
        p->transportSync ? 1u : 0u,
    };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    s3g::AmbiAcidParams savedParams {};
    std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount> savedPattern {};
    bool savedTransportSync = false;
    if (version == 1u) {
        LegacySavedState saved {};
        saved.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&saved) + sizeof(version),
                sizeof(saved) - sizeof(version))) return false;
        savedParams = saved.params;
        savedPattern = saved.pattern;
    } else if (version == kStateVersion) {
        SavedState saved {};
        saved.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&saved) + sizeof(version),
                sizeof(saved) - sizeof(version))) return false;
        savedParams = saved.params;
        savedPattern = saved.pattern;
        savedTransportSync = saved.transportSync != 0u;
    } else {
        return false;
    }
    auto* p = self(plugin);
    p->engine.setPattern(savedPattern);
    p->engine.setParams(savedParams);
    p->params = p->engine.params();
    p->transportSync = savedTransportSync;
    publishAllParams(*p);
    return true;
}

const clap_plugin_state_t state {
    stateSave,
    stateLoad,
};

#if defined(__APPLE__)

struct AcidUiRow {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

constexpr CGFloat kPatternX = 16.0;
constexpr CGFloat kPatternY = 46.0;
constexpr CGFloat kPatternWidth = 888.0;
constexpr CGFloat kPatternHeight = 214.0;
constexpr CGFloat kStepStartX = 24.0;
constexpr CGFloat kStepPitch = 54.5;
constexpr CGFloat kStepWidth = 51.5;

constexpr std::array<AcidUiRow, kBaseParamCount> kUiRows {{
    { kTempoParamId, "TEMPO", 16.0, 286.0, 300.0 },
    { kTransportSyncParamId, "CLOCK", 16.0, 286.0, 331.0 },
    { kDivisionParamId, "DIVIDE", 16.0, 286.0, 362.0 },
    { kLengthParamId, "LENGTH", 16.0, 286.0, 393.0 },
    { kRootParamId, "ROOT", 16.0, 286.0, 424.0 },
    { kGateLengthParamId, "GATE", 16.0, 286.0, 455.0 },
    { kOrderParamId, "ORDER", 16.0, 286.0, 486.0 },
    { kOutputParamId, "OUTPUT", 16.0, 286.0, 517.0 },

    { kWaveParamId, "WAVE", 309.0, 286.0, 302.0 },
    { kPulseWidthParamId, "PULSE", 309.0, 286.0, 329.0 },
    { kCutoffParamId, "CUTOFF", 309.0, 286.0, 356.0 },
    { kResonanceParamId, "RESO", 309.0, 286.0, 383.0 },
    { kFilterEnvelopeParamId, "ENV", 309.0, 286.0, 410.0 },
    { kFilterDecayParamId, "DECAY", 309.0, 286.0, 437.0 },
    { kAccentParamId, "ACCENT", 309.0, 286.0, 464.0 },
    { kDriveParamId, "DRIVE", 309.0, 286.0, 491.0 },
    { kSlideParamId, "SLIDE", 309.0, 286.0, 518.0 },

    { kCenterAzimuthParamId, "AZIM", 602.0, 302.0, 300.0 },
    { kPathTurnsParamId, "TURNS", 602.0, 302.0, 324.0 },
    { kElevationSpreadParamId, "ELEV", 602.0, 302.0, 348.0 },
    { kSpatialSpreadParamId, "SPREAD", 602.0, 302.0, 372.0 },
    { kEdgeLeadParamId, "EDGE", 602.0, 302.0, 396.0 },
    { kWakeAmountParamId, "WAKE", 602.0, 302.0, 420.0 },
    { kWakeTimeParamId, "WAKE T", 602.0, 302.0, 444.0 },
    { kListenModeParamId, "LISTEN", 602.0, 302.0, 468.0 },
    { kListenAmountParamId, "AMOUNT", 602.0, 302.0, 492.0 },
    { kListenMemoryParamId, "MEMORY", 602.0, 302.0, 516.0 },
}};

NSRect stepCellRect(uint32_t step)
{
    return NSMakeRect(kStepStartX + kStepPitch * step,
        74.0, kStepWidth, 178.0);
}

NSRect stepNoteRect(uint32_t step)
{
    const NSRect cell = stepCellRect(step);
    return NSMakeRect(cell.origin.x + 4.0, 96.0,
        cell.size.width - 8.0, 104.0);
}

NSRect stepToggleRect(uint32_t step, StepParamKind kind)
{
    const NSRect cell = stepCellRect(step);
    const CGFloat width = (cell.size.width - 8.0) / 3.0;
    const uint32_t toggle = static_cast<uint32_t>(kind) - 1u;
    return NSMakeRect(cell.origin.x + 4.0 + width * toggle,
        226.0, width - 1.0, 16.0);
}

const ParamSpec* paramSpec(clap_id id)
{
    if (id < kOrderParamId || id > kTransportSyncParamId) return nullptr;
    return &kParamSpecs[id - kOrderParamId];
}

bool logarithmicUiParam(clap_id id)
{
    return id == kCutoffParamId || id == kFilterDecayParamId
        || id == kSlideParamId || id == kWakeTimeParamId
        || id == kListenMemoryParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* spec = paramSpec(id);
    if (!spec) return 0.0;
    if (logarithmicUiParam(id) && spec->minimum > 0.0) {
        return std::clamp(std::log(value / spec->minimum)
            / std::log(spec->maximum / spec->minimum), 0.0, 1.0);
    }
    return std::clamp((value - spec->minimum)
        / std::max(1.0e-12, spec->maximum - spec->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* spec = paramSpec(id);
    if (!spec) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = logarithmicUiParam(id) && spec->minimum > 0.0
        ? spec->minimum * std::pow(
            spec->maximum / spec->minimum, normalized)
        : spec->minimum + (spec->maximum - spec->minimum) * normalized;
    return clampParamValue(id, value);
}

NSString* midiNoteName(int note)
{
    static constexpr const char* names[] {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B",
    };
    note = std::clamp(note, 0, 127);
    return [NSString stringWithFormat:@"%s%d", names[note % 12],
        note / 12 - 1];
}

void drawCenteredText(NSString* text, NSRect rect, NSDictionary* attrs)
{
    const NSSize size = [text sizeWithAttributes:attrs];
    [text drawAtPoint:NSMakePoint(
        rect.origin.x + (rect.size.width - size.width) * 0.5,
        rect.origin.y + (rect.size.height - size.height) * 0.5)
        withAttributes:attrs];
}

NSRect patternPresetButtonRect()
{
    const auto band = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    return s3g::clap_gui::cocoaRect(band.presetMenu);
}

NSRect patternPresetMenuRect()
{
    const NSRect anchor = patternPresetButtonRect();
    return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width,
        18.0 * s3g::kAmbiAcidPatternPresets.size());
}

int publishedPatternPresetIndex(const Plugin& plugin)
{
    for (uint32_t preset = 0u;
         preset < s3g::kAmbiAcidPatternPresets.size(); ++preset) {
        bool matches = true;
        for (uint32_t step = 0u;
             step < s3g::kAmbiAcidStepCount && matches; ++step) {
            const auto& expected =
                s3g::kAmbiAcidPatternPresets[preset].steps[step];
            const clap_id base = kStepParamBase + step * kStepParamStride;
            matches = std::lround(paramValue(plugin, base))
                    == expected.semitoneOffset
                && (paramValue(plugin, base + 1u) >= 0.5) == expected.gate
                && (paramValue(plugin, base + 2u) >= 0.5) == expected.accent
                && (paramValue(plugin, base + 3u) >= 0.5) == expected.slide;
        }
        if (matches) return static_cast<int>(preset);
    }
    return -1;
}

} // namespace

@interface S3GAmbiAcidEncoderView : NSView {
    void* _plugin;
    int _dragParam;
    int _dragStep;
    NSInteger _selectedStep;
    int _presetIndex;
    BOOL _presetMenuOpen;
    int _presetHover;
    char _patternName[64];
    char _titlePresetName[64];
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (NSInteger)selectedStep;
- (int)presetIndex;
- (BOOL)presetMenuOpen;
- (void)applyPatternPreset:(int)index;
- (void)randomizePattern;
- (void)markCustomPattern;
- (void)markCustomState;
- (void)updateDraggedParam:(NSPoint)point;
- (void)updateDraggedNote:(NSPoint)point;
@end

@implementation S3GAmbiAcidEncoderView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(
        0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        _dragStep = -1;
        _selectedStep = 0;
        _presetIndex = publishedPatternPresetIndex(
            *static_cast<Plugin*>(plugin));
        _presetMenuOpen = NO;
        _presetHover = -1;
        std::snprintf(_patternName, sizeof(_patternName), "%s",
            _presetIndex >= 0
                ? s3g::kAmbiAcidPatternPresets[
                    static_cast<uint32_t>(_presetIndex)].name
                : "CUSTOM");
        std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
            _presetIndex >= 0
                ? s3g::kAmbiAcidPatternPresets[
                    static_cast<uint32_t>(_presetIndex)].name
                : "CUSTOM");
        _timer = nil;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (NSInteger)selectedStep { return _selectedStep; }
- (int)presetIndex { return _presetIndex; }
- (BOOL)presetMenuOpen { return _presetMenuOpen; }

- (void)markCustomPattern
{
    _presetIndex = -1;
    std::snprintf(_patternName, sizeof(_patternName), "%s", "CUSTOM");
    [self markCustomState];
}

- (void)markCustomState
{
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
        "CUSTOM");
}

- (void)applyPatternPreset:(int)index
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kAmbiAcidPatternPresets.size() - 1u));
    if (!queueGuiPattern(*p,
            s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(index)].steps)) {
        NSBeep();
        return;
    }
    _presetIndex = index;
    std::snprintf(_patternName, sizeof(_patternName), "%s",
        s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(index)].name);
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
        s3g::kAmbiAcidPatternPresets[static_cast<uint32_t>(index)].name);
    [self setNeedsDisplay:YES];
}

- (void)randomizePattern
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    static constexpr int scale[] { 0, 2, 3, 5, 7, 10, 12, 15 };
    static constexpr int octaves[] { -12, 0, 0, 0, 0, 12 };
    std::array<s3g::AmbiAcidStep, s3g::kAmbiAcidStepCount> pattern {};
    for (uint32_t step = 0u; step < pattern.size(); ++step) {
        const int degree = scale[arc4random_uniform(
            static_cast<uint32_t>(std::size(scale)))];
        const int octave = octaves[arc4random_uniform(
            static_cast<uint32_t>(std::size(octaves)))];
        pattern[step].semitoneOffset = std::clamp(degree + octave, -24, 24);
        pattern[step].gate = step == 0u || arc4random_uniform(100u) < 82u;
        pattern[step].accent = pattern[step].gate
            && (step % 4u == 0u || arc4random_uniform(100u) < 22u);
    }
    pattern[0u].semitoneOffset = 0;
    pattern[0u].gate = true;
    pattern[0u].accent = true;
    for (uint32_t step = 0u; step < pattern.size(); ++step) {
        const uint32_t next = (step + 1u) % pattern.size();
        pattern[step].slide = pattern[step].gate && pattern[next].gate
            && arc4random_uniform(100u) < 30u;
    }
    if (!queueGuiPattern(*p, pattern)) {
        NSBeep();
        return;
    }
    _presetIndex = -1;
    std::snprintf(_patternName, sizeof(_patternName), "%s", "RANDOM");
    std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
        "RANDOM");
    [self setNeedsDisplay:YES];
}

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0
        target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    auto* p = static_cast<Plugin*>(_plugin);
    if (p && !_presetMenuOpen && _dragStep < 0) {
        const int detected = publishedPatternPresetIndex(*p);
        if (detected >= 0 && detected != _presetIndex) {
            _presetIndex = detected;
            std::snprintf(_patternName, sizeof(_patternName), "%s",
                s3g::kAmbiAcidPatternPresets[
                    static_cast<uint32_t>(detected)].name);
        } else if (detected < 0 && _presetIndex >= 0) {
            [self markCustomPattern];
        }
    }
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    s3g::clap_gui::Style style;
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    NSDictionary* noteAttrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0xd4d4d4), 11.0);
    NSDictionary* tinyAttrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0x8f8f8f), 8.5);
    [style.bg setFill];
    NSRectFill([self bounds]);
    [style.strip setFill];
    NSRectFill(NSMakeRect(0.0, 0.0, kGuiWidth, 40.0));
    [style.grid setFill];
    NSRectFill(NSMakeRect(0.0, 39.0, kGuiWidth, 1.0));
    const float activity = p->visualActivity.load(std::memory_order_relaxed);
    const float listener = p->visualListenerActivity.load(
        std::memory_order_relaxed);
    const bool transportSync = paramValue(*p, kTransportSyncParamId) >= 0.5;
    const bool transportPlaying = p->visualTransportPlaying.load(
        std::memory_order_relaxed);
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g AMBI ENCODER ACID 16",
        [NSString stringWithUTF8String:_titlePresetName],
        [NSString stringWithFormat:@"%@%@  BODY %3.0f%%  LISTEN %3.0f%%",
            transportSync ? @"HOST" : @"INT",
            transportSync && transportPlaying ? @" >" : @"",
            activity * 100.0f, listener * 100.0f],
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    s3g::clap_gui::drawPanelFrame(kPatternX, kPatternY,
        kPatternWidth, kPatternHeight, style);
    s3g::clap_gui::drawPanelHeader(
        @"16-STEP NOTE LINE  ·  DRAG PITCH  ·  GATE / ACCENT / SLIDE",
        true, kPatternX, kPatternY, kPatternWidth, 22.0,
        labelAttrs, style);
    const uint32_t currentStep = p->visualCurrentStep.load(
        std::memory_order_relaxed);
    const uint32_t patternLength = static_cast<uint32_t>(
        std::lround(paramValue(*p, kLengthParamId)));
    const int root = static_cast<int>(
        std::lround(paramValue(*p, kRootParamId)));
    for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
        const NSRect cell = stepCellRect(step);
        const NSRect noteRect = stepNoteRect(step);
        const bool selected = step == static_cast<uint32_t>(_selectedStep);
        const bool playing = step == currentStep && activity > 0.002f;
        const bool inLength = step < patternLength;
        const clap_id noteId = kStepParamBase + step * kStepParamStride;
        const int offset = static_cast<int>(
            std::lround(paramValue(*p, noteId)));
        const bool gate = paramValue(*p, noteId + 1u) >= 0.5;
        const bool accent = paramValue(*p, noteId + 2u) >= 0.5;
        const bool slide = paramValue(*p, noteId + 3u) >= 0.5;

        [s3g::clap_gui::color(inLength ? 0x191919 : 0x111111) setFill];
        NSRectFill(cell);
        [s3g::clap_gui::color(selected ? 0xb8b8b8 : 0x3d3d3d) setStroke];
        NSFrameRect(cell);
        if (playing) {
            [s3g::clap_gui::color(0xd5d5d5) setFill];
            NSRectFill(NSMakeRect(cell.origin.x, cell.origin.y,
                cell.size.width, 3.0));
        }
        drawCenteredText([NSString stringWithFormat:@"%02u", step + 1u],
            NSMakeRect(cell.origin.x, 78.0, cell.size.width, 12.0),
            playing ? noteAttrs : tinyAttrs);

        [s3g::clap_gui::color(gate ? 0x202020 : 0x141414) setFill];
        NSRectFill(noteRect);
        [s3g::clap_gui::color(0x373737) setStroke];
        NSFrameRect(noteRect);
        const CGFloat rootY = noteRect.origin.y + noteRect.size.height * 0.5;
        [s3g::clap_gui::color(0x4b4b4b) setFill];
        NSRectFill(NSMakeRect(noteRect.origin.x + 1.0, rootY,
            noteRect.size.width - 2.0, 1.0));
        const CGFloat markerY = noteRect.origin.y + 2.0
            + (36.0 - offset) / 72.0 * (noteRect.size.height - 5.0);
        [s3g::clap_gui::color(gate ? 0xb0b0b0 : 0x4d4d4d) setFill];
        NSRectFill(NSMakeRect(noteRect.origin.x + 3.0, markerY,
            noteRect.size.width - 6.0, playing ? 3.0 : 2.0));
        drawCenteredText(midiNoteName(root + offset),
            NSMakeRect(noteRect.origin.x, 112.0,
                noteRect.size.width, 18.0), noteAttrs);
        drawCenteredText([NSString stringWithFormat:@"%+d", offset],
            NSMakeRect(noteRect.origin.x, 137.0,
                noteRect.size.width, 14.0), tinyAttrs);
        if (!gate) {
            drawCenteredText(@"REST", NSMakeRect(noteRect.origin.x,
                173.0, noteRect.size.width, 14.0), tinyAttrs);
        }

        const bool toggles[] { gate, accent, slide };
        NSString* labels[] { @"G", @"A", @"S" };
        for (uint32_t toggle = 0u; toggle < 3u; ++toggle) {
            const auto kind = static_cast<StepParamKind>(toggle + 1u);
            const NSRect rect = stepToggleRect(step, kind);
            [s3g::clap_gui::color(toggles[toggle]
                    ? (toggle == 1u ? 0x454545 : 0x353535) : 0x151515)
                setFill];
            NSRectFill(rect);
            [s3g::clap_gui::color(toggles[toggle] ? 0xa8a8a8 : 0x383838)
                setStroke];
            NSFrameRect(rect);
            drawCenteredText(labels[toggle], rect,
                toggles[toggle] ? valueAttrs : tinyAttrs);
        }
    }

    const NSRect linePanel = NSMakeRect(16.0, 268.0, 286.0, 276.0);
    const NSRect voicePanel = NSMakeRect(309.0, 268.0, 286.0, 276.0);
    const NSRect fieldPanel = NSMakeRect(602.0, 268.0, 302.0, 276.0);
    const auto drawPanel = [&](NSString* title, NSRect rect) {
        s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        s3g::clap_gui::drawPanelHeader(title, true,
            rect.origin.x, rect.origin.y, rect.size.width, 22.0,
            labelAttrs, style);
    };
    drawPanel(@"LINE / OUTPUT", linePanel);
    drawPanel(@"ACID VOICE", voicePanel);
    drawPanel(@"AMBISONIC FIELD / LISTENER", fieldPanel);
    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        if (row.id == kTempoParamId) {
            std::snprintf(text, sizeof(text), "%.1f", value);
        } else if (row.id == kTransportSyncParamId) {
            std::snprintf(text, sizeof(text), "%s",
                value >= 0.5 ? "HOST" : "INT");
        }
        s3g::clap_gui::drawProcessorSlider(
            [NSString stringWithUTF8String:row.label],
            [NSString stringWithUTF8String:text],
            uiNormalizedValue(row.id, value), row.y,
            row.panelX, row.panelWidth,
            labelAttrs, valueAttrs, style);
    }

    const float directionX = p->visualDirectionX.load(
        std::memory_order_relaxed);
    const float directionY = p->visualDirectionY.load(
        std::memory_order_relaxed);
    const NSPoint compass = NSMakePoint(875.0, 281.0);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(NSMakeRect(compass.x - 10.0, compass.y - 10.0, 20.0, 20.0));
    [s3g::clap_gui::color(0x303030) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(compass.x, compass.y - 8.0)
        toPoint:NSMakePoint(compass.x, compass.y + 8.0)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(compass.x - 8.0, compass.y)
        toPoint:NSMakePoint(compass.x + 8.0, compass.y)];
    // Top view: +X/front is up, +Y/listener-left is screen-left.
    [s3g::clap_gui::color(0x858585) setFill];
    NSRectFill(NSMakeRect(compass.x - 1.0, compass.y - 9.0, 2.0, 2.0));
    [s3g::clap_gui::color(0xc2c2c2) setFill];
    NSRectFill(NSMakeRect(compass.x - directionY * 7.0 - 1.5,
        compass.y - directionX * 7.0 - 1.5,
        3.0, 3.0));
    if (_presetMenuOpen) {
        NSString* items[s3g::kAmbiAcidPatternPresets.size()] {};
        for (uint32_t index = 0u;
             index < s3g::kAmbiAcidPatternPresets.size(); ++index) {
            items[index] = [NSString stringWithUTF8String:
                s3g::kAmbiAcidPatternPresets[index].name];
        }
        s3g::clap_gui::drawDropdownMenu(patternPresetMenuRect(), 18.0,
            items, static_cast<uint32_t>(std::size(items)),
            _presetIndex, _presetHover, valueAttrs, style);
    }
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    for (const auto& row : kUiRows) {
        if (row.id != static_cast<clap_id>(_dragParam)) continue;
        const double controlX = s3g::gui_layout::processorControlX(row.panelX);
        const double trackWidth =
            s3g::gui_layout::processorTrackWidth(row.panelWidth);
        const double normalized = std::clamp(
            (point.x - controlX) / trackWidth, 0.0, 1.0);
        queueGuiParamValue(*static_cast<Plugin*>(_plugin), row.id,
            uiValueFromNormalized(row.id, normalized));
        [self setNeedsDisplay:YES];
        return;
    }
}

- (void)updateDraggedNote:(NSPoint)point
{
    if (_dragStep < 0) return;
    const NSRect rect = stepNoteRect(static_cast<uint32_t>(_dragStep));
    const double normalized = std::clamp(
        (point.y - rect.origin.y) / rect.size.height, 0.0, 1.0);
    const double note = std::round(36.0 - normalized * 72.0);
    const clap_id id = kStepParamBase
        + static_cast<clap_id>(_dragStep) * kStepParamStride;
    queueGuiParamValue(*static_cast<Plugin*>(_plugin), id, note);
    [self markCustomPattern];
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    [[self window] makeFirstResponder:self];
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    if (_presetMenuOpen) {
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, patternPresetMenuRect(), 18.0,
            static_cast<uint32_t>(s3g::kAmbiAcidPatternPresets.size()));
        _presetMenuOpen = NO;
        _presetHover = -1;
        if (hit >= 0) [self applyPatternPreset:hit];
        [self setNeedsDisplay:YES];
        return;
    }
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _presetMenuOpen = YES;
        _presetHover = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &p->plugin, @"Ambi Encoder Acid",
                kOutputParamId, &name)) {
            _presetIndex = publishedPatternPresetIndex(*p);
            std::snprintf(_patternName, sizeof(_patternName), "%s",
                _presetIndex >= 0
                    ? s3g::kAmbiAcidPatternPresets[
                        static_cast<uint32_t>(_presetIndex)].name
                    : "CUSTOM");
            std::snprintf(_titlePresetName,
                sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &p->plugin, @"Ambi Encoder Acid", &name)) {
            std::snprintf(_titlePresetName,
                sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        [self randomizePattern];
        return;
    }
    for (uint32_t step = 0u; step < s3g::kAmbiAcidStepCount; ++step) {
        if (NSPointInRect(point, stepNoteRect(step))) {
            _selectedStep = step;
            const clap_id id = kStepParamBase + step * kStepParamStride;
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &p->plugin, id, &defaultValue)) {
                queueGuiParamGesture(*p, id, defaultValue);
            } else {
                _dragStep = static_cast<int>(step);
                queueGuiParamGestureBegin(*p, id);
                [self updateDraggedNote:point];
            }
            [self markCustomPattern];
            [self setNeedsDisplay:YES];
            return;
        }
        for (uint32_t toggle = 0u; toggle < 3u; ++toggle) {
            const auto kind = static_cast<StepParamKind>(toggle + 1u);
            if (!NSPointInRect(point, stepToggleRect(step, kind))) continue;
            _selectedStep = step;
            const clap_id id = kStepParamBase + step * kStepParamStride
                + toggle + 1u;
            queueGuiParamGesture(*p, id,
                paramValue(*p, id) >= 0.5 ? 0.0 : 1.0);
            [self markCustomPattern];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    for (const auto& row : kUiRows) {
        const NSRect hit = NSMakeRect(
            row.panelX + s3g::gui_layout::kStandardMetrics.hitInset,
            row.y - 9.0,
            row.panelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight);
        if (!NSPointInRect(point, hit)) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*p, row.id, defaultValue);
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*p, row.id);
            [self updateDraggedParam:point];
        }
        if (row.id != kOutputParamId) [self markCustomState];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    if (_dragStep >= 0) [self updateDraggedNote:point];
    else if (_dragParam > 0) [self updateDraggedParam:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    auto* p = static_cast<Plugin*>(_plugin);
    if (p && _dragStep >= 0) {
        queueGuiParamGestureEnd(*p, kStepParamBase
            + static_cast<clap_id>(_dragStep) * kStepParamStride);
    }
    if (p && _dragParam > 0) {
        queueGuiParamGestureEnd(*p, static_cast<clap_id>(_dragParam));
    }
    _dragStep = -1;
    _dragParam = -1;
}

- (void)scrollWheel:(NSEvent*)event
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p || [event deltaY] == 0.0) return;
    const clap_id id = kStepParamBase
        + static_cast<clap_id>(_selectedStep) * kStepParamStride;
    const double increment = [event deltaY] > 0.0 ? 1.0 : -1.0;
    queueGuiParamGesture(*p, id, paramValue(*p, id) + increment);
    [self markCustomPattern];
    [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent*)event
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const unsigned short code = [event keyCode];
    if (code == 123 || code == 124) {
        _selectedStep = (_selectedStep + (code == 123 ? 15 : 1)) % 16;
        [self setNeedsDisplay:YES];
        return;
    }
    if (code == 125 || code == 126) {
        const clap_id id = kStepParamBase
            + static_cast<clap_id>(_selectedStep) * kStepParamStride;
        const double amount = ([event modifierFlags]
            & NSEventModifierFlagShift) ? 12.0 : 1.0;
        queueGuiParamGesture(*p, id,
            paramValue(*p, id) + (code == 126 ? amount : -amount));
        [self markCustomPattern];
        [self setNeedsDisplay:YES];
        return;
    }
    NSString* characters = [[event charactersIgnoringModifiers] lowercaseString];
    StepParamKind kind = StepParamKind::Note;
    if ([characters isEqualToString:@"g"] || [characters isEqualToString:@" "])
        kind = StepParamKind::Gate;
    else if ([characters isEqualToString:@"a"])
        kind = StepParamKind::Accent;
    else if ([characters isEqualToString:@"s"])
        kind = StepParamKind::Slide;
    else {
        [super keyDown:event];
        return;
    }
    const clap_id id = kStepParamBase
        + static_cast<clap_id>(_selectedStep) * kStepParamStride
        + static_cast<uint32_t>(kind);
    queueGuiParamGesture(*p, id, paramValue(*p, id) >= 0.5 ? 0.0 : 1.0);
    [self markCustomPattern];
    [self setNeedsDisplay:YES];
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (!_presetMenuOpen) return;
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, patternPresetMenuRect(), 18.0,
        static_cast<uint32_t>(s3g::kAmbiAcidPatternPresets.size()));
    if (hover != _presetHover) {
        _presetHover = hover;
        [self setNeedsDisplay:YES];
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
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
    p->guiView = [[S3GAmbiAcidEncoderView alloc] initWithPlugin:p];
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
    if (!p || !p->guiView) return;
    p->guiVisible = false;
    [static_cast<S3GAmbiAcidEncoderView*>(p->guiView) stopRefreshTimer];
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

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
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
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        p->guiViewport, static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GAmbiAcidEncoderView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GAmbiAcidEncoderView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
}

const clap_plugin_gui_t gui {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide,
};

#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-encoder-acid-16",
    "s3g Ambi Encoder Acid 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "A monophonic spatial bassline instrument with resonant wake and bounded Listener Mode.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
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
    publishAllParams(*p);
    return &p->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin,
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    if (factoryId
        && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &factory;
    }
    return nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
