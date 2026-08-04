#include "s3g_ambi_membrane_kick.h"
#include "s3g_ambi_membrane_kick_presets.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_gui_layout.h"
#endif

namespace {

constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kLegacyStateVersion = 1u;
constexpr uint32_t kOutputChannels = s3g::kAmbiMembraneKickChannels;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 680u;

constexpr clap_id kOrderParamId = 1u;
constexpr clap_id kShapeParamId = 2u;
constexpr clap_id kTuneParamId = 3u;
constexpr clap_id kPitchSweepParamId = 4u;
constexpr clap_id kPitchSweepTimeParamId = 5u;
constexpr clap_id kDecayParamId = 6u;
constexpr clap_id kDampingParamId = 7u;
constexpr clap_id kPunchParamId = 8u;
constexpr clap_id kClickParamId = 9u;
constexpr clap_id kDriveParamId = 10u;
constexpr clap_id kStrikeXParamId = 11u;
constexpr clap_id kStrikeYParamId = 12u;
constexpr clap_id kSpreadParamId = 13u;
constexpr clap_id kDepthParamId = 14u;
constexpr clap_id kRotationParamId = 15u;
constexpr clap_id kShapeAmountParamId = 16u;
constexpr clap_id kVelocityParamId = 17u;
constexpr clap_id kNoteTrackingParamId = 18u;
constexpr clap_id kOutputParamId = 19u;
constexpr clap_id kTriggerParamId = 20u;
constexpr clap_id kStrikeModeParamId = 21u;
constexpr uint32_t kParamCount = 21u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, kParamCount> kParamDefs {{
    { kOrderParamId, "Ambisonic Order", "Output", 1.0, 3.0, 3.0, true },
    { kShapeParamId, "Membrane Shape", "Membrane", 0.0, 4.0, 0.0, true },
    { kTuneParamId, "Fundamental", "Body", 25.0, 90.0, 43.0, false },
    { kPitchSweepParamId, "Pitch Drop", "Impact", 0.0, 48.0, 31.0, false },
    { kPitchSweepTimeParamId, "Pitch Drop Time", "Impact", 5.0, 250.0, 42.0, false },
    { kDecayParamId, "Decay", "Body", 0.08, 6.0, 1.45, false },
    { kDampingParamId, "Damping", "Body", 0.0, 1.0, 0.26, false },
    { kPunchParamId, "Punch", "Impact", 0.0, 1.0, 0.76, false },
    { kClickParamId, "Click", "Impact", 0.0, 1.0, 0.16, false },
    { kDriveParamId, "Drive", "Body", 0.0, 1.0, 0.28, false },
    { kStrikeXParamId, "Strike X", "Membrane / Strike", -1.0, 1.0, 0.18, false },
    { kStrikeYParamId, "Strike Y", "Membrane / Strike", -1.0, 1.0, -0.08, false },
    { kSpreadParamId, "Spatial Spread", "Space", 0.0, 1.0, 0.72, false },
    { kDepthParamId, "Membrane Depth", "Space", 0.0, 1.0, 0.42, false },
    { kRotationParamId, "Rotation", "Space", -180.0, 180.0, 0.0, false },
    { kShapeAmountParamId, "Shape Amount", "Membrane", 0.0, 1.0, 0.72, false },
    { kVelocityParamId, "Velocity Sensitivity", "MIDI", 0.0, 1.0, 1.0, false },
    { kNoteTrackingParamId, "Note Tracking", "MIDI", 0.0, 1.0, 1.0, false },
    { kOutputParamId, "Output Gain", "Output", -60.0, 6.0, -8.0, false },
    { kStrikeModeParamId, "Strike Placement", "Membrane / Strike", 0.0, 2.0, 0.0, true },
    { kTriggerParamId, "Trigger", "Performance", 0.0, 1.0, 0.0, true },
}};

struct SavedStateHeader {
    uint32_t version = kStateVersion;
    uint32_t reserved = 0u;
};

struct SavedState {
    SavedStateHeader header {};
    std::array<double, kParamCount - 1u> values {};
};

struct LegacySavedStateValues {
    std::array<double, 19u> values {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiMembraneKickParams params {};
    s3g::AmbiMembraneKick kick;
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<uint32_t> pendingTriggers { 0u };
    std::atomic<float> visualActivity { 0.0f };
    std::atomic<float> visualStrikeX { 0.18f };
    std::atomic<float> visualStrikeY { -0.08f };
    bool triggerGate = false;
    bool active = false;
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

const ParamDef* paramDef(clap_id id)
{
    for (const auto& def : kParamDefs) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

double clampValue(const ParamDef& def, double value)
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

double rawParamValue(const Plugin& p, clap_id id);

void publishParam(Plugin& p, clap_id id, double value)
{
    if (id < kOrderParamId || id > kStrikeModeParamId) return;
    p.publishedParams[id - kOrderParamId].store(
        value, std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    if (id < kOrderParamId || id > kStrikeModeParamId) return 0.0;
    return p.publishedParams[id - kOrderParamId].load(
        std::memory_order_acquire);
}

void publishActualStrike(Plugin& p)
{
    const auto strike = p.kick.actualStrikePosition();
    p.visualStrikeX.store(strike[0u], std::memory_order_relaxed);
    p.visualStrikeY.store(strike[1u], std::memory_order_relaxed);
}

void triggerKick(Plugin& p, float velocity, int midiNote)
{
    p.kick.trigger(velocity, midiNote);
    publishActualStrike(p);
    p.active = true;
}

void notifyTailChanged(Plugin& p)
{
    if (!p.host || !p.host->get_extension) return;
    const auto* tail = static_cast<const clap_host_tail_t*>(
        p.host->get_extension(p.host, CLAP_EXT_TAIL));
    if (tail && tail->changed) tail->changed(p.host);
}

void applyParam(Plugin& p, clap_id id, double value,
    bool triggerImmediately = false)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kTriggerParamId) {
        const bool gate = value >= 0.5;
        if (gate && !p.triggerGate) {
            if (triggerImmediately) {
                triggerKick(p, 1.0f, 36);
            } else {
                p.pendingTriggers.fetch_add(1u, std::memory_order_relaxed);
                if (p.host && p.host->request_process) {
                    p.host->request_process(p.host);
                }
            }
        }
        p.triggerGate = gate;
        publishParam(p, id, gate ? 1.0 : 0.0);
        return;
    }

    const float v = static_cast<float>(value);
    switch (id) {
    case kOrderParamId: p.params.order = static_cast<uint32_t>(value); break;
    case kShapeParamId:
        p.params.shape = static_cast<s3g::AmbiMembraneShape>(
            static_cast<uint32_t>(value));
        break;
    case kTuneParamId: p.params.tuneHz = v; break;
    case kPitchSweepParamId: p.params.pitchSweepSemitones = v; break;
    case kPitchSweepTimeParamId: p.params.pitchSweepMs = v; break;
    case kDecayParamId: {
        const bool changed = std::fabs(p.params.decaySeconds - v) > 1.0e-6f;
        p.params.decaySeconds = v;
        if (changed) notifyTailChanged(p);
        break;
    }
    case kDampingParamId: p.params.damping = v; break;
    case kPunchParamId: p.params.punch = v; break;
    case kClickParamId: p.params.click = v; break;
    case kDriveParamId: p.params.drive = v; break;
    case kStrikeXParamId: p.params.strikeX = v; break;
    case kStrikeYParamId: p.params.strikeY = v; break;
    case kSpreadParamId: p.params.spatialSpread = v; break;
    case kDepthParamId: p.params.membraneDepth = v; break;
    case kRotationParamId: p.params.rotationDeg = v; break;
    case kShapeAmountParamId: p.params.shapeAmount = v; break;
    case kVelocityParamId: p.params.velocitySensitivity = v; break;
    case kNoteTrackingParamId: p.params.noteTracking = v; break;
    case kOutputParamId: p.params.outputGainDb = v; break;
    case kStrikeModeParamId:
        p.params.strikeMode = static_cast<s3g::AmbiMembraneStrikeMode>(
            static_cast<uint32_t>(value));
        break;
    default: return;
    }
    p.kick.setParams(p.params);
    p.params = p.kick.params();
    if (id == kStrikeXParamId || id == kStrikeYParamId
        || id == kStrikeModeParamId) {
        publishActualStrike(p);
    }
    publishParam(p, id, rawParamValue(p, id));
    if (id == kStrikeXParamId || id == kStrikeYParamId) {
        publishParam(p, kStrikeXParamId, p.params.strikeX);
        publishParam(p, kStrikeYParamId, p.params.strikeY);
    }
}

double rawParamValue(const Plugin& p, clap_id id)
{
    switch (id) {
    case kOrderParamId: return p.params.order;
    case kShapeParamId: return static_cast<uint32_t>(p.params.shape);
    case kTuneParamId: return p.params.tuneHz;
    case kPitchSweepParamId: return p.params.pitchSweepSemitones;
    case kPitchSweepTimeParamId: return p.params.pitchSweepMs;
    case kDecayParamId: return p.params.decaySeconds;
    case kDampingParamId: return p.params.damping;
    case kPunchParamId: return p.params.punch;
    case kClickParamId: return p.params.click;
    case kDriveParamId: return p.params.drive;
    case kStrikeXParamId: return p.params.strikeX;
    case kStrikeYParamId: return p.params.strikeY;
    case kSpreadParamId: return p.params.spatialSpread;
    case kDepthParamId: return p.params.membraneDepth;
    case kRotationParamId: return p.params.rotationDeg;
    case kShapeAmountParamId: return p.params.shapeAmount;
    case kVelocityParamId: return p.params.velocitySensitivity;
    case kNoteTrackingParamId: return p.params.noteTracking;
    case kOutputParamId: return p.params.outputGainDb;
    case kStrikeModeParamId:
        return static_cast<uint32_t>(p.params.strikeMode);
    case kTriggerParamId: return p.triggerGate ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void applyEvent(Plugin& p, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(p, param->param_id, param->value, true);
    } else if (event->type == CLAP_EVENT_NOTE_ON
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (note->velocity > 0.0) {
            triggerKick(p, static_cast<float>(note->velocity), note->key);
        }
    } else if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if ((midi->data[0] & 0xf0u) == 0x90u && midi->data[2] > 0u) {
            triggerKick(p, static_cast<float>(midi->data[2]) / 127.0f,
                midi->data[1]);
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

void requestGuiParamService(Plugin& p)
{
    if (p.hostParams && p.hostParams->request_flush) {
        p.hostParams->request_flush(p.host);
    } else if (p.host && p.host->request_process) {
        p.host->request_process(p.host);
    }
}

bool queueGuiParamEvent(Plugin& p,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!p.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(p);
    return true;
}

void queueGuiParamGestureBegin(Plugin& p, clap_id id)
{
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& p, clap_id id, double value)
{
    if (const auto* def = paramDef(id)) {
        value = clampValue(*def, value);
        publishParam(p, id, value);
    }
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& p, clap_id id)
{
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queueGuiParamGesture(Plugin& p, clap_id id, double value)
{
    queueGuiParamGestureBegin(p, id);
    queueGuiParamValue(p, id, value);
    queueGuiParamGestureEnd(p, id);
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

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (p.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(p, pending.paramId, pending.value, true);
        }
        p.guiParamEvents.pop();
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
    p->sampleRate = std::clamp(sampleRate, 8000.0, 768000.0);
    p->kick.prepare(p->sampleRate);
    p->kick.setParams(p->params);
    publishActualStrike(*p);
    p->active = false;
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->kick.reset();
    publishActualStrike(*p);
    p->pendingTriggers.store(0u, std::memory_order_relaxed);
    p->triggerGate = false;
    publishParam(*p, kTriggerParamId, 0.0);
    p->active = false;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto* p = self(plugin);
    if (!process) return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(*p, process->out_events);
    uint32_t pending = p->pendingTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) {
        triggerKick(*p, 1.0f, 36);
    }

    const clap_input_events_t* events = process->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (process->audio_outputs_count == 0u
        || !process->audio_outputs) {
        while (eventIndex < eventCount) {
            applyEvent(*p, events->get(events, eventIndex++));
        }
        return p->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }

    const auto& output = process->audio_outputs[0u];
    const uint32_t channels = std::min(output.channel_count, kOutputChannels);
    if (channels == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }
    std::array<float, kOutputChannels> frame {};
    for (uint32_t sample = 0u; sample < process->frames_count; ++sample) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > sample) break;
            applyEvent(*p, event);
            ++eventIndex;
        }
        p->kick.processFrame(frame.data(), channels);
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][sample] = frame[channel];
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][sample] = frame[channel];
            }
        }
        for (uint32_t channel = channels;
             channel < output.channel_count; ++channel) {
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][sample] = 0.0f;
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][sample] = 0.0;
            }
        }
    }
    while (eventIndex < eventCount) {
        applyEvent(*p, events->get(events, eventIndex++));
    }
    p->active = p->kick.active();
    p->visualActivity.store(p->kick.activity(), std::memory_order_relaxed);
    return p->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
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
    std::strncpy(info->name, "3OA ACN/SN3D Membrane Field",
        sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts {
    audioPortsCount, audioPortsGet
};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1u : 0u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || !isInput || index != 0u) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Membrane Strike In",
        sizeof(info->name) - 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamDefs.size()) return false;
    const auto& def = kParamDefs[index];
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (def.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, def.name, sizeof(info->name) - 1u);
    std::strncpy(info->module, def.module, sizeof(info->module) - 1u);
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id,
    double* value)
{
    if (!value || !paramDef(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kOrderParamId) {
        std::snprintf(display, size, "%uOA",
            static_cast<uint32_t>(std::round(value)));
    } else if (id == kShapeParamId) {
        constexpr const char* names[] {
            "Circle", "Ellipse", "Square", "Triangle", "Irregular"
        };
        const uint32_t shape = std::min<uint32_t>(4u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[shape]);
    } else if (id == kStrikeModeParamId) {
        constexpr const char* names[] {
            "Fixed", "Random Area", "Random Rim"
        };
        const uint32_t mode = std::min<uint32_t>(2u,
            static_cast<uint32_t>(std::round(value)));
        std::snprintf(display, size, "%s", names[mode]);
    } else if (id == kTuneParamId) {
        std::snprintf(display, size, "%.1f Hz", value);
    } else if (id == kPitchSweepParamId) {
        std::snprintf(display, size, "%.1f st", value);
    } else if (id == kPitchSweepTimeParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kDecayParamId) {
        std::snprintf(display, size, "%.2f s", value);
    } else if (id == kRotationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kStrikeXParamId || id == kStrikeYParamId) {
        std::snprintf(display, size, "%+.2f", value);
    } else if (id == kTriggerParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "Strike" : "Ready");
    } else {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    const auto* def = paramDef(id);
    if (!display || !value || !def) return false;
    if (id == kShapeParamId) {
        constexpr const char* names[] {
            "circle", "ellipse", "square", "triangle", "irregular"
        };
        for (uint32_t shape = 0u; shape < 5u; ++shape) {
            if (std::strstr(display, names[shape])) {
                *value = shape;
                return true;
            }
        }
    } else if (id == kStrikeModeParamId) {
        constexpr const char* names[] {
            "fixed", "random area", "random rim"
        };
        for (uint32_t mode = 0u; mode < 3u; ++mode) {
            if (std::strstr(display, names[mode])) {
                *value = mode;
                return true;
            }
        }
    }
    *value = std::atof(display);
    if (id != kOrderParamId && id != kShapeParamId
        && id != kStrikeModeParamId
        && id != kTuneParamId && id != kPitchSweepParamId
        && id != kPitchSweepTimeParamId && id != kDecayParamId
        && id != kRotationParamId && id != kOutputParamId
        && id != kStrikeXParamId && id != kStrikeYParamId
        && id != kTriggerParamId && std::strchr(display, '%')) {
        *value *= 0.01;
    }
    *value = clampValue(*def, *value);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* p = self(plugin);
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE
            || event->size < sizeof(clap_event_param_value_t)) continue;
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(*p, param->param_id, param->value, false);
    }
    serviceGuiParamEvents(*p, output);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state {};
    const auto* p = self(plugin);
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        state.values[index] = paramValue(*p, kParamDefs[index].id);
    }
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedStateHeader header {};
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))) {
        return false;
    }
    auto* p = self(plugin);
    if (header.version == kStateVersion) {
        std::array<double, kParamCount - 1u> values {};
        if (!s3g::clap_state::readAll(
                stream, values.data(), sizeof(values))) return false;
        for (uint32_t index = 0u; index < values.size(); ++index) {
            applyParam(*p, kParamDefs[index].id, values[index]);
        }
    } else if (header.version == kLegacyStateVersion) {
        LegacySavedStateValues legacy {};
        if (!s3g::clap_state::readAll(
                stream, &legacy, sizeof(legacy))) return false;
        for (uint32_t index = 0u; index < legacy.values.size(); ++index) {
            applyParam(*p, kParamDefs[index].id, legacy.values[index]);
        }
        applyParam(*p, kStrikeModeParamId, 0.0);
    } else {
        return false;
    }
    p->triggerGate = false;
    publishParam(*p, kTriggerParamId, 0.0);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const double seconds = std::clamp(
        paramValue(*p, kDecayParamId) * 16.2 + 0.1,
        1.0, 100.0);
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * p->sampleRate)));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct MembraneUiRow {
    clap_id id;
    const char* label;
    CGFloat y;
    int membranePage;
};

constexpr CGFloat kUiPanelX = 580.0;
constexpr CGFloat kUiPanelWidth = 324.0;
constexpr std::array<MembraneUiRow, 20u> kUiRows {{
    { kOrderParamId, "ORDER", 80.0, -1 },
    { kOutputParamId, "OUT", 104.0, -1 },
    { kShapeParamId, "SHAPE", 184.0, 0 },
    { kShapeAmountParamId, "AMOUNT", 208.0, 0 },
    { kVelocityParamId, "VELOCITY", 232.0, 0 },
    { kStrikeModeParamId, "MODE", 184.0, 1 },
    { kStrikeXParamId, "X", 208.0, 1 },
    { kStrikeYParamId, "Y", 232.0, 1 },
    { kTuneParamId, "TUNE", 314.0, -1 },
    { kDecayParamId, "DECAY", 338.0, -1 },
    { kDampingParamId, "DAMP", 362.0, -1 },
    { kDriveParamId, "DRIVE", 386.0, -1 },
    { kNoteTrackingParamId, "TRACK", 410.0, -1 },
    { kPitchSweepParamId, "DROP", 468.0, -1 },
    { kPitchSweepTimeParamId, "TIME", 492.0, -1 },
    { kPunchParamId, "PUNCH", 516.0, -1 },
    { kClickParamId, "CLICK", 540.0, -1 },
    { kSpreadParamId, "SPREAD", 598.0, -1 },
    { kDepthParamId, "DEPTH", 622.0, -1 },
    { kRotationParamId, "ROTATE", 646.0, -1 },
}};

bool uiRowVisible(const MembraneUiRow& row, int membranePage)
{
    return row.membranePage < 0 || row.membranePage == membranePage;
}

bool isUiMenuParam(clap_id id)
{
    return id == kOrderParamId || id == kShapeParamId
        || id == kStrikeModeParamId;
}

NSRect membranePageButtonRect(uint32_t index)
{
    return NSMakeRect(kUiPanelX + 206.0 + index * 52.0,
        158.0, 48.0, 13.0);
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if ((id == kTuneParamId || id == kPitchSweepTimeParamId
            || id == kDecayParamId)
        && def->minimum > 0.0) {
        return std::clamp(std::log(value / def->minimum)
            / std::log(def->maximum / def->minimum), 0.0, 1.0);
    }
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = (id == kTuneParamId
            || id == kPitchSweepTimeParamId || id == kDecayParamId)
        ? def->minimum * std::pow(def->maximum / def->minimum, normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

std::array<float, 2u> visualMembranePoint(
    const s3g::AmbiMembraneKickParams& params, float radius, float theta)
{
    constexpr float pi = 3.14159265358979323846f;
    float x = radius * std::cos(theta);
    float y = radius * std::sin(theta);
    const float amount = params.shapeAmount;
    switch (params.shape) {
    case s3g::AmbiMembraneShape::Ellipse:
        x *= 1.0f + amount * 0.42f;
        y *= 1.0f - amount * 0.30f;
        break;
    case s3g::AmbiMembraneShape::Square: {
        const float maximum = std::max(0.001f,
            std::max(std::fabs(std::cos(theta)),
                std::fabs(std::sin(theta))));
        const float scale = 1.0f + (1.0f / maximum - 1.0f) * amount;
        x *= scale;
        y *= scale;
        break;
    }
    case s3g::AmbiMembraneShape::Triangle: {
        const float local = std::remainder(theta, 2.0f * pi / 3.0f);
        const float boundary = 0.5f / std::max(0.51f,
            std::cos(pi / 3.0f - std::fabs(local)));
        const float scale = 1.0f + (boundary - 1.0f) * amount;
        x *= scale;
        y *= scale;
        break;
    }
    case s3g::AmbiMembraneShape::Irregular: {
        const float wobble = 1.0f + amount
            * (0.17f * std::sin(theta * 3.0f + 0.7f)
                + 0.09f * std::sin(theta * 5.0f - 0.4f));
        const float originalX = x;
        x = x * wobble + amount * y * y * 0.10f;
        y = y * wobble - amount * originalX * 0.08f;
        break;
    }
    case s3g::AmbiMembraneShape::Circle:
    default:
        break;
    }
    return { x, y };
}

s3g::AmbiMembraneKickParams publishedParamsSnapshot(const Plugin& p)
{
    s3g::AmbiMembraneKickParams params;
    params.order = static_cast<uint32_t>(paramValue(p, kOrderParamId));
    params.shape = static_cast<s3g::AmbiMembraneShape>(
        static_cast<uint32_t>(paramValue(p, kShapeParamId)));
    params.tuneHz = static_cast<float>(paramValue(p, kTuneParamId));
    params.pitchSweepSemitones = static_cast<float>(
        paramValue(p, kPitchSweepParamId));
    params.pitchSweepMs = static_cast<float>(
        paramValue(p, kPitchSweepTimeParamId));
    params.decaySeconds = static_cast<float>(paramValue(p, kDecayParamId));
    params.damping = static_cast<float>(paramValue(p, kDampingParamId));
    params.punch = static_cast<float>(paramValue(p, kPunchParamId));
    params.click = static_cast<float>(paramValue(p, kClickParamId));
    params.drive = static_cast<float>(paramValue(p, kDriveParamId));
    params.strikeX = static_cast<float>(paramValue(p, kStrikeXParamId));
    params.strikeY = static_cast<float>(paramValue(p, kStrikeYParamId));
    params.strikeMode = static_cast<s3g::AmbiMembraneStrikeMode>(
        static_cast<uint32_t>(paramValue(p, kStrikeModeParamId)));
    params.spatialSpread = static_cast<float>(paramValue(p, kSpreadParamId));
    params.membraneDepth = static_cast<float>(paramValue(p, kDepthParamId));
    params.rotationDeg = static_cast<float>(paramValue(p, kRotationParamId));
    params.shapeAmount = static_cast<float>(paramValue(p, kShapeAmountParamId));
    params.velocitySensitivity = static_cast<float>(
        paramValue(p, kVelocityParamId));
    params.noteTracking = static_cast<float>(
        paramValue(p, kNoteTrackingParamId));
    params.outputGainDb = static_cast<float>(paramValue(p, kOutputParamId));
    return params;
}

} // namespace

@interface S3GAmbiMembraneKickView : NSView {
    void* _plugin;
    int _dragParam;
    int _factoryPresetIndex;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    int _membranePage;
    NSTimer* _timer;
    char _presetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)applyFactoryPreset:(int)index;
- (void)markCustomPreset;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
- (void)updateDraggedParam:(NSPoint)point;
- (void)drawControl:(NSString*)label value:(NSString*)value
    normalized:(double)normalized y:(CGFloat)y menu:(BOOL)menu
    labelAttrs:(NSDictionary*)labelAttrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style;
@end

@implementation S3GAmbiMembraneKickView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        auto* p = static_cast<Plugin*>(plugin);
        _factoryPresetIndex = p
            ? s3g::ambiMembraneKickFactoryPresetIndex(
                publishedParamsSnapshot(*p)) : 0;
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _membranePage = 0;
        _timer = nil;
        if (_factoryPresetIndex >= 0) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                s3g::ambiMembraneKickFactoryPresetInfo(
                    static_cast<uint32_t>(_factoryPresetIndex)).name);
        } else {
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        }
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

- (void)applyFactoryPreset:(int)index
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kAmbiMembraneKickFactoryPresetCount - 1u));
    const auto preset = s3g::ambiMembraneKickFactoryPreset(
        static_cast<uint32_t>(index));
    const auto set = [&](clap_id id, double value) {
        queueGuiParamGestureBegin(*p, id);
        queueGuiParamValue(*p, id, value);
        queueGuiParamGestureEnd(*p, id);
    };
    set(kShapeParamId, static_cast<uint32_t>(preset.shape));
    set(kTuneParamId, preset.tuneHz);
    set(kPitchSweepParamId, preset.pitchSweepSemitones);
    set(kPitchSweepTimeParamId, preset.pitchSweepMs);
    set(kDecayParamId, preset.decaySeconds);
    set(kDampingParamId, preset.damping);
    set(kPunchParamId, preset.punch);
    set(kClickParamId, preset.click);
    set(kDriveParamId, preset.drive);
    set(kStrikeXParamId, preset.strikeX);
    set(kStrikeYParamId, preset.strikeY);
    set(kStrikeModeParamId, static_cast<uint32_t>(preset.strikeMode));
    set(kSpreadParamId, preset.spatialSpread);
    set(kDepthParamId, preset.membraneDepth);
    set(kRotationParamId, preset.rotationDeg);
    set(kShapeAmountParamId, preset.shapeAmount);
    set(kVelocityParamId, preset.velocitySensitivity);
    set(kNoteTrackingParamId, preset.noteTracking);
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::ambiMembraneKickFactoryPresetInfo(
            static_cast<uint32_t>(index)).name);
    [self setNeedsDisplay:YES];
}

- (void)markCustomPreset
{
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
}

- (NSRect)openMenuRect
{
    if (_openMenu == kFactoryPresetMenuId) {
        const auto band = s3g::clap_gui::encoderTitleBand(
            kGuiWidth, kGuiHeight);
        const NSRect anchor = s3g::clap_gui::cocoaRect(band.presetMenu);
        return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
            anchor.size.width, 18.0 * _menuItemCount);
    }
    CGFloat y = 0.0;
    if (_openMenu == kOrderParamId) y = 80.0;
    else if (_openMenu == kShapeParamId
        || _openMenu == kStrikeModeParamId) y = 184.0;
    else return NSZeroRect;
    const NSRect anchor = NSMakeRect(
        s3g::gui_layout::processorControlX(kUiPanelX), y - 1.0,
        s3g::gui_layout::processorMenuWidth(kUiPanelWidth), 15.0);
    return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width, 18.0 * _menuItemCount);
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    NSString* items[s3g::kAmbiMembraneKickFactoryPresetCount] {};
    if (_openMenu == kFactoryPresetMenuId) {
        for (uint32_t index = 0u;
             index < s3g::kAmbiMembraneKickFactoryPresetCount; ++index) {
            items[index] = [NSString stringWithUTF8String:
                s3g::ambiMembraneKickFactoryPresetInfo(index).name];
        }
    } else if (_openMenu == kOrderParamId) {
        items[0u] = @"1OA / 4CH";
        items[1u] = @"2OA / 9CH";
        items[2u] = @"3OA / 16CH";
    } else if (_openMenu == kShapeParamId) {
        items[0u] = @"CIRCLE";
        items[1u] = @"ELLIPSE";
        items[2u] = @"SQUARE";
        items[3u] = @"TRIANGLE";
        items[4u] = @"IRREGULAR";
    } else if (_openMenu == kStrikeModeParamId) {
        items[0u] = @"FIXED";
        items[1u] = @"RANDOM AREA";
        items[2u] = @"RANDOM RIM";
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(paramValue(
            *static_cast<Plugin*>(_plugin), _openMenu)))
            - (_openMenu == kOrderParamId ? 1 : 0);
    s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
        items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    const float activity = p->visualActivity.load(std::memory_order_relaxed);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g AMBI ENCODER MEMBRANE KICK 16",
        [NSString stringWithUTF8String:_presetName],
        [NSString stringWithFormat:@"BODY %3.0f%%", activity * 100.0f],
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const NSRect field = NSMakeRect(16.0, 50.0, 550.0, 614.0);
    const NSRect outputPanel = NSMakeRect(kUiPanelX, 50.0,
        kUiPanelWidth, 96.0);
    const NSRect membranePanel = NSMakeRect(kUiPanelX, 154.0,
        kUiPanelWidth, 122.0);
    const NSRect bodyPanel = NSMakeRect(kUiPanelX, 284.0,
        kUiPanelWidth, 146.0);
    const NSRect impactPanel = NSMakeRect(kUiPanelX, 438.0,
        kUiPanelWidth, 122.0);
    const NSRect spacePanel = NSMakeRect(kUiPanelX, 568.0,
        kUiPanelWidth, 96.0);
    const auto drawPanel = [&](NSString* name, NSRect rect) {
        s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        s3g::clap_gui::drawPanelHeader(name, true, rect.origin.x,
            rect.origin.y, rect.size.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labelAttrs, style);
    };
    drawPanel(@"DISTRIBUTED MEMBRANE", field);
    drawPanel(@"OUTPUT / STRIKE", outputPanel);
    drawPanel(@"MEMBRANE", membranePanel);
    drawPanel(@"LOW BODY", bodyPanel);
    drawPanel(@"IMPACT", impactPanel);
    drawPanel(@"AMBISONIC SPACE", spacePanel);
    s3g::clap_gui::drawHeaderButton(membranePageButtonRect(0u),
        membranePanel, @"BODY", _membranePage == 0, valueAttrs, style);
    s3g::clap_gui::drawHeaderButton(membranePageButtonRect(1u),
        membranePanel, @"STRIKE", _membranePage == 1, valueAttrs, style);

    auto paramsSnapshot = publishedParamsSnapshot(*p);
    paramsSnapshot.strikeX = p->visualStrikeX.load(
        std::memory_order_relaxed);
    paramsSnapshot.strikeY = p->visualStrikeY.load(
        std::memory_order_relaxed);
    const NSPoint center = NSMakePoint(291.0, 338.0);
    constexpr CGFloat radiusPixels = 202.0;
    NSBezierPath* membrane = [NSBezierPath bezierPath];
    for (uint32_t point = 0u; point <= 128u; ++point) {
        const float theta = static_cast<float>(point) / 128.0f
            * 6.28318530717958647692f;
        const auto position = visualMembranePoint(paramsSnapshot, 1.0f, theta);
        const NSPoint screen = NSMakePoint(
            center.x + position[0] * radiusPixels,
            center.y - position[1] * radiusPixels);
        point == 0u ? [membrane moveToPoint:screen]
                    : [membrane lineToPoint:screen];
    }
    [membrane closePath];
    [[NSColor colorWithCalibratedRed:0.055 green:0.070 blue:0.074
        alpha:1.0] setFill];
    [membrane fill];
    [[NSColor colorWithCalibratedRed:0.30 + activity * 0.40
        green:0.62 + activity * 0.22 blue:0.66 + activity * 0.18
        alpha:0.88] setStroke];
    [membrane setLineWidth:1.5 + activity * 3.5];
    [membrane stroke];

    for (uint32_t ring = 0u; ring < 4u; ++ring) {
        for (uint32_t spoke = 0u; spoke < 4u; ++spoke) {
            const float localRadius = 0.18f + static_cast<float>(ring) * 0.22f;
            const float theta = static_cast<float>(spoke)
                    * 1.57079632679489661923f
                + static_cast<float>(ring & 1u) * 0.78539816339744830962f;
            const auto position = visualMembranePoint(
                paramsSnapshot, localRadius, theta);
            const NSPoint node = NSMakePoint(
                center.x + position[0] * radiusPixels,
                center.y - position[1] * radiusPixels);
            const CGFloat nodeRadius = 3.0 + activity
                * (2.0 + static_cast<CGFloat>(3u - ring));
            [[NSColor colorWithCalibratedRed:0.32 green:0.76
                blue:0.80 alpha:0.42 + activity * 0.52] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                node.x - nodeRadius, node.y - nodeRadius,
                nodeRadius * 2.0, nodeRadius * 2.0)] fill];
        }
    }
    const NSPoint strike = NSMakePoint(
        center.x + paramsSnapshot.strikeX * radiusPixels,
        center.y - paramsSnapshot.strikeY * radiusPixels);
    [[NSColor colorWithCalibratedWhite:0.96 alpha:0.94] setStroke];
    NSBezierPath* strikeMark = [NSBezierPath bezierPath];
    [strikeMark appendBezierPathWithOvalInRect:NSMakeRect(
        strike.x - 9.0, strike.y - 9.0, 18.0, 18.0)];
    [strikeMark moveToPoint:NSMakePoint(strike.x - 13.0, strike.y)];
    [strikeMark lineToPoint:NSMakePoint(strike.x + 13.0, strike.y)];
    [strikeMark moveToPoint:NSMakePoint(strike.x, strike.y - 13.0)];
    [strikeMark lineToPoint:NSMakePoint(strike.x, strike.y + 13.0)];
    [strikeMark setLineWidth:1.2];
    [strikeMark stroke];
    [@"CLICK THE MEMBRANE TO PLACE + STRIKE" drawAtPoint:
        NSMakePoint(field.origin.x + 14.0, NSMaxY(field) - 29.0)
        withAttributes:labelAttrs];

    for (const auto& row : kUiRows) {
        if (!uiRowVisible(row, _membranePage)) continue;
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        [self drawControl:[NSString stringWithUTF8String:row.label]
            value:[NSString stringWithUTF8String:text]
            normalized:uiNormalizedValue(row.id, value) y:row.y
            menu:isUiMenuParam(row.id)
            labelAttrs:labelAttrs valueAttrs:valueAttrs style:style];
    }
    const NSRect trigger = NSMakeRect(kUiPanelX + 12.0, 122.0,
        kUiPanelWidth - 24.0, 18.0);
    s3g::clap_gui::drawHeaderButton(trigger, outputPanel, @"STRIKE",
        activity > 0.05f, valueAttrs, style);
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)drawControl:(NSString*)label value:(NSString*)value
    normalized:(double)normalized y:(CGFloat)y menu:(BOOL)menu
    labelAttrs:(NSDictionary*)labelAttrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    if (menu) {
        s3g::clap_gui::drawProcessorMenu(label, value, y,
            kUiPanelX, kUiPanelWidth, labelAttrs, valueAttrs, style);
        return;
    }
    s3g::clap_gui::drawProcessorSlider(label, value,
        static_cast<CGFloat>(normalized), y,
        kUiPanelX, kUiPanelWidth, labelAttrs, valueAttrs, style);
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const double controlX = s3g::gui_layout::processorControlX(kUiPanelX);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(kUiPanelWidth);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*p, static_cast<clap_id>(_dragParam),
        uiValueFromNormalized(static_cast<clap_id>(_dragParam), normalized));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            [self openMenuRect], 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                [self applyFactoryPreset:hit];
            } else {
                const clap_id menuParam = _openMenu;
                const double value = hit
                    + (menuParam == kOrderParamId ? 1.0 : 0.0);
                queueGuiParamGesture(*p, menuParam, value);
                if (menuParam != kOrderParamId) [self markCustomPreset];
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = s3g::kAmbiMembraneKickFactoryPresetCount;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &p->plugin, @"Ambi Encoder Membrane Kick",
                kOutputParamId, &name)) {
            _factoryPresetIndex = -1;
            std::snprintf(_presetName, sizeof(_presetName), "%s",
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
                &p->plugin, @"Ambi Encoder Membrane Kick", &name)) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        [self applyFactoryPreset:static_cast<int>(arc4random_uniform(
            s3g::kAmbiMembraneKickFactoryPresetCount))];
        return;
    }
    for (uint32_t page = 0u; page < 2u; ++page) {
        if (NSPointInRect(point, membranePageButtonRect(page))) {
            _membranePage = static_cast<int>(page);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const NSRect trigger = NSMakeRect(kUiPanelX + 12.0, 122.0,
        kUiPanelWidth - 24.0, 18.0);
    if (NSPointInRect(point, trigger)) {
        queueGuiParamGesture(*p, kTriggerParamId, 0.0);
        queueGuiParamGesture(*p, kTriggerParamId, 1.0);
        [self setNeedsDisplay:YES];
        return;
    }

    const NSPoint center = NSMakePoint(291.0, 338.0);
    constexpr CGFloat radiusPixels = 202.0;
    const float x = static_cast<float>((point.x - center.x) / radiusPixels);
    const float y = static_cast<float>((center.y - point.y) / radiusPixels);
    if (point.x >= 42.0 && point.x <= 540.0
        && point.y >= 86.0 && point.y <= 594.0
        && x * x + y * y <= 1.0f) {
        queueGuiParamGesture(*p, kStrikeModeParamId, 0.0);
        queueGuiParamGesture(*p, kStrikeXParamId, x);
        queueGuiParamGesture(*p, kStrikeYParamId, y);
        queueGuiParamGesture(*p, kTriggerParamId, 0.0);
        queueGuiParamGesture(*p, kTriggerParamId, 1.0);
        [self markCustomPreset];
        [self setNeedsDisplay:YES];
        return;
    }

    for (const auto& row : kUiRows) {
        if (!uiRowVisible(row, _membranePage)) continue;
        const NSRect hit = NSMakeRect(
            kUiPanelX + s3g::gui_layout::kStandardMetrics.hitInset,
            row.y - 9.0,
            kUiPanelWidth
                - s3g::gui_layout::kStandardMetrics.hitInset * 2.0,
            s3g::gui_layout::kStandardMetrics.hitHeight);
        if (!NSPointInRect(point, hit)) continue;
        const auto* def = paramDef(row.id);
        if (!def) return;
        if (isUiMenuParam(row.id)) {
            _openMenu = row.id;
            _hoverMenuItem = -1;
            _menuItemCount = row.id == kShapeParamId ? 5u : 3u;
            [self setNeedsDisplay:YES];
            return;
        }
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*p, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*p, row.id);
            [self updateDraggedParam:point];
        }
        if (row.id != kOutputParamId) [self markCustomPreset];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam > 0) {
        [self updateDraggedParam:[self convertPoint:
            [event locationInWindow] fromView:nil]];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam > 0) {
        queueGuiParamGestureEnd(*static_cast<Plugin*>(_plugin),
            static_cast<clap_id>(_dragParam));
    }
    _dragParam = -1;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const NSPoint point =
        [self convertPoint:[event locationInWindow] fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self openMenuRect], 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
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
    p->guiView = [[S3GAmbiMembraneKickView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiMembraneKickView*>(p->guiView) stopRefreshTimer];
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
    [static_cast<S3GAmbiMembraneKickView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GAmbiMembraneKickView*>(p->guiView) stopRefreshTimer];
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

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    CLAP_PLUGIN_FEATURE_SURROUND,
    CLAP_PLUGIN_FEATURE_DRUM,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-encoder-membrane-kick-16",
    "s3g Ambi Encoder Membrane Kick 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Deep pitch-dropping kick synthesized by a shaped twelve-mode membrane whose sixteen radiating surface patches are distributed through first- to third-order ACN/SN3D ambisonic space.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->kick.setParams(p->params);
    publishActualStrike(*p);
    for (const auto& def : kParamDefs) {
        publishParam(*p, def.id, rawParamValue(*p, def.id));
    }
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0
        ? create(host) : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin
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
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
