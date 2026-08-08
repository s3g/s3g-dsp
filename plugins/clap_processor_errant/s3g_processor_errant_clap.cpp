#include "s3g_processor_errant.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_drum_midi_receive.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
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

constexpr uint32_t kStateMagic = 0x45524753u; // "SGRE" in little endian.
constexpr uint32_t kStateVersion = 3u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 582u;
constexpr uint32_t kVisualScopeFrames = 128u;

constexpr clap_id kModeParamId = 1u;
constexpr clap_id kMaterialParamId = 2u;
constexpr clap_id kSpanParamId = 3u;
constexpr clap_id kDensityParamId = 4u;
constexpr clap_id kAncestryParamId = 5u;
constexpr clap_id kMutationParamId = 6u;
constexpr clap_id kRepeatParamId = 7u;
constexpr clap_id kCoherenceParamId = 8u;
constexpr clap_id kRegisterParamId = 9u;
constexpr clap_id kNoteTrackingParamId = 10u;
constexpr clap_id kToneParamId = 11u;
constexpr clap_id kDriveParamId = 12u;
constexpr clap_id kTopologyParamId = 13u;
constexpr clap_id kWidthParamId = 14u;
constexpr clap_id kSeedParamId = 15u;
constexpr clap_id kVelocityParamId = 16u;
constexpr clap_id kOutputParamId = 17u;
constexpr clap_id kMidiReceiveParamId = 18u;
constexpr clap_id kTriggerParamId = 19u;
constexpr clap_id kKeyRoleParamId = 20u;
constexpr clap_id kSubParamId = 21u;
constexpr clap_id kResonanceParamId = 22u;
constexpr clap_id kFilterContourParamId = 23u;
constexpr clap_id kCrosswireParamId = 24u;
constexpr uint32_t kParamCount = 24u;
constexpr uint32_t kSavedParamCount = kParamCount - 1u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

// Keep Trigger last so the first kSavedParamCount definitions remain the
// persistent state payload. IDs stay stable across the v0.1 -> v0.2 update.
constexpr std::array<ParamDef, kParamCount> kParamDefs {{
    { kModeParamId, "Mode", "Grammar", 0.0, 2.0, 1.0, true },
    { kMaterialParamId, "Growl", "Bass Circuit / High Gain", 0.0, 1.0, 0.52, false },
    { kSpanParamId, "Span", "Grammar", 0.0, 1.0, 0.46, false },
    { kDensityParamId, "Density", "Grammar", 0.0, 1.0, 0.58, false },
    { kAncestryParamId, "Ancestry", "Grammar", 0.0, 1.0, 0.68, false },
    { kMutationParamId, "Mutation", "Grammar", 0.0, 1.0, 0.42, false },
    { kRepeatParamId, "Repeat", "Grammar", 0.0, 1.0, 0.38, false },
    { kCoherenceParamId, "Coherence", "Grammar", 0.0, 1.0, 0.62, false },
    { kRegisterParamId, "Register", "Voice / Pitch", -36.0, 36.0, 0.0, false },
    { kNoteTrackingParamId, "Pitch Gravity", "Voice / MIDI", 0.0, 1.0, 1.0, false },
    { kToneParamId, "Cutoff", "Bass Circuit / Filter", -1.0, 1.0, 0.18, false },
    { kDriveParamId, "Drive", "Voice / Character", 0.0, 1.0, 0.20, false },
    { kTopologyParamId, "Stereo Topology", "Stereo", 0.0, 3.0, 1.0, true },
    { kWidthParamId, "Width", "Stereo", 0.0, 1.0, 0.56, false },
    { kSeedParamId, "Seed", "Performance", 1.0, 65535.0, 1979.0, true },
    { kVelocityParamId, "Velocity Sensitivity", "Performance / MIDI", 0.0, 1.0, 0.78, false },
    { kOutputParamId, "Output Gain", "Output", -36.0, 6.0, -8.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "Performance / MIDI", 0.0, 16.0, 0.0, true },
    { kKeyRoleParamId, "Key Role", "Voice / MIDI", 0.0, 2.0, 2.0, true },
    { kSubParamId, "Sub", "Bass Circuit / Oscillators", 0.0, 1.0, 0.62, false },
    { kResonanceParamId, "Resonance", "Bass Circuit / Filter", 0.0, 1.0, 0.38, false },
    { kFilterContourParamId, "Filter Contour", "Bass Circuit / Filter", -1.0, 1.0, 0.46, false },
    { kCrosswireParamId, "Crosswire", "Bass Circuit / Fault", 0.0, 1.0, 0.34, false },
    { kTriggerParamId, "Trigger", "Performance", 0.0, 1.0, 0.0, true },
}};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t valueCount = kSavedParamCount;
    uint32_t reserved = 0u;
};

struct SavedState {
    SavedStateHeader header {};
    std::array<double, kSavedParamCount> values {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::ProcessorErrantParams params {};
    s3g::ProcessorErrant engine {};
    double midiReceive = 0.0;
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<uint32_t> pendingTriggers { 0u };
    std::atomic<uint64_t> parameterRevision { 0u };
    std::atomic<float> visualActivity { 0.0f };
    std::atomic<int32_t> visualNote { -1 };
    std::atomic<int32_t> visualInterval { 0 };
    std::atomic<uint32_t> visualGeneration { 0u };
    std::array<std::atomic<float>, kVisualScopeFrames> visualScopeL {};
    std::array<std::atomic<float>, kVisualScopeFrames> visualScopeR {};
    std::atomic<uint32_t> visualScopeWrite { 0u };
    uint32_t visualScopeDivider = 0u;
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

double rawParamValue(const Plugin& p, clap_id id)
{
    switch (id) {
    case kModeParamId: return static_cast<uint32_t>(p.params.mode);
    case kMaterialParamId: return p.params.material;
    case kSpanParamId: return p.params.span;
    case kDensityParamId: return p.params.density;
    case kAncestryParamId: return p.params.ancestry;
    case kMutationParamId: return p.params.mutation;
    case kRepeatParamId: return p.params.repeat;
    case kCoherenceParamId: return p.params.coherence;
    case kRegisterParamId: return p.params.registerSemitones;
    case kNoteTrackingParamId: return p.params.noteTracking;
    case kToneParamId: return p.params.tone;
    case kDriveParamId: return p.params.drive;
    case kTopologyParamId: return static_cast<uint32_t>(p.params.topology);
    case kWidthParamId: return p.params.width;
    case kSeedParamId: return p.params.seed;
    case kVelocityParamId: return p.params.velocitySensitivity;
    case kOutputParamId: return p.params.outputGainDb;
    case kMidiReceiveParamId: return p.midiReceive;
    case kKeyRoleParamId: return static_cast<uint32_t>(p.params.keyRole);
    case kSubParamId: return p.params.sub;
    case kResonanceParamId: return p.params.resonance;
    case kFilterContourParamId: return p.params.filterContour;
    case kCrosswireParamId: return p.params.crosswire;
    case kTriggerParamId: return p.triggerGate ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void publishParam(Plugin& p, clap_id id, double value)
{
    if (id < kModeParamId || id > kCrosswireParamId) return;
    p.publishedParams[id - kModeParamId].store(
        value, std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    if (id < kModeParamId || id > kCrosswireParamId) return 0.0;
    return p.publishedParams[id - kModeParamId].load(
        std::memory_order_acquire);
}

void triggerVoice(Plugin& p, float velocity, int note,
    int32_t noteId = -1, int16_t channel = -1)
{
    p.engine.noteOn(note, velocity, noteId, channel);
    p.visualNote.store(note, std::memory_order_relaxed);
    p.visualInterval.store(
        p.engine.lastNoteInterval(), std::memory_order_relaxed);
    p.visualGeneration.store(
        p.engine.lineageGeneration(), std::memory_order_relaxed);
    p.active = true;
}

void applyParam(Plugin& p, clap_id id, double value,
    bool triggerImmediately = false)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kMidiReceiveParamId) {
        p.midiReceive = value;
    } else if (id == kTriggerParamId) {
        const bool gate = value >= 0.5;
        if (gate && !p.triggerGate) {
            if (triggerImmediately) {
                triggerVoice(p, 1.0f, 60);
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
    } else {
        const float v = static_cast<float>(value);
        switch (id) {
        case kModeParamId:
            p.params.mode = static_cast<s3g::ErrantMode>(
                static_cast<uint32_t>(value));
            break;
        case kMaterialParamId: p.params.material = v; break;
        case kSpanParamId: p.params.span = v; break;
        case kDensityParamId: p.params.density = v; break;
        case kAncestryParamId: p.params.ancestry = v; break;
        case kMutationParamId: p.params.mutation = v; break;
        case kRepeatParamId: p.params.repeat = v; break;
        case kCoherenceParamId: p.params.coherence = v; break;
        case kRegisterParamId: p.params.registerSemitones = v; break;
        case kNoteTrackingParamId: p.params.noteTracking = v; break;
        case kToneParamId: p.params.tone = v; break;
        case kDriveParamId: p.params.drive = v; break;
        case kTopologyParamId:
            p.params.topology = static_cast<s3g::ErrantTopology>(
                static_cast<uint32_t>(value));
            break;
        case kWidthParamId: p.params.width = v; break;
        case kSeedParamId: p.params.seed = static_cast<uint32_t>(value); break;
        case kVelocityParamId: p.params.velocitySensitivity = v; break;
        case kOutputParamId: p.params.outputGainDb = v; break;
        case kKeyRoleParamId:
            p.params.keyRole = static_cast<s3g::ErrantKeyRole>(
                static_cast<uint32_t>(value));
            break;
        case kSubParamId: p.params.sub = v; break;
        case kResonanceParamId: p.params.resonance = v; break;
        case kFilterContourParamId: p.params.filterContour = v; break;
        case kCrosswireParamId: p.params.crosswire = v; break;
        default: return;
        }
    }
    p.params = s3g::sanitizeProcessorErrantParams(p.params);
    p.engine.setParams(p.params);
    publishParam(p, id, rawParamValue(p, id));
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
}

void releaseTransientTrigger(Plugin& p)
{
    if (!p.triggerGate) return;
    p.triggerGate = false;
    publishParam(p, kTriggerParamId, 0.0);
}

void clearVisualScope(Plugin& p)
{
    for (auto& sample : p.visualScopeL) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& sample : p.visualScopeR) {
        sample.store(0.0f, std::memory_order_relaxed);
    }
    p.visualScopeWrite.store(0u, std::memory_order_relaxed);
    p.visualScopeDivider = 0u;
}

void applyEvent(Plugin& p, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(p, param->param_id, param->value, true);
        return;
    }
    if ((event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF
            || event->type == CLAP_EVENT_NOTE_CHOKE
            || event->type == CLAP_EVENT_NOTE_END)
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (!s3g::drum_midi::accepts(p.midiReceive, note->channel)) return;
        if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
            triggerVoice(p, static_cast<float>(note->velocity), note->key,
                note->note_id, note->channel);
        } else {
            p.engine.noteOff(note->key, note->note_id, note->channel);
        }
        return;
    }
    if (event->type != CLAP_EVENT_MIDI
        || event->size < sizeof(clap_event_midi_t)) return;
    const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
    const uint8_t status = midi->data[0] & 0xf0u;
    const int channel = midi->data[0] & 0x0fu;
    if (!s3g::drum_midi::accepts(p.midiReceive, channel)) return;
    if (status == 0x90u && midi->data[2] > 0u) {
        triggerVoice(p, static_cast<float>(midi->data[2]) / 127.0f,
            midi->data[1], -1, static_cast<int16_t>(channel));
    } else if (status == 0x80u || (status == 0x90u && midi->data[2] == 0u)) {
        p.engine.noteOff(midi->data[1], -1, static_cast<int16_t>(channel));
    } else if (status == 0xb0u
        && (midi->data[1] == 120u || midi->data[1] == 123u)) {
        p.engine.allNotesOff();
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

void queueGuiTrigger(Plugin& p)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 4u> events {{
        { Kind::GestureBegin, kTriggerParamId, 0.0 },
        { Kind::Value, kTriggerParamId, 1.0 },
        { Kind::Value, kTriggerParamId, 0.0 },
        { Kind::GestureEnd, kTriggerParamId, 0.0 },
    }};
    if (p.guiParamEvents.pushBatch(events.data(), events.size())) {
        publishParam(p, kTriggerParamId, 0.0);
        requestGuiParamService(p);
    }
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
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->params);
    p->active = false;
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->visualNote.store(-1, std::memory_order_relaxed);
    p->visualInterval.store(0, std::memory_order_relaxed);
    p->visualGeneration.store(0u, std::memory_order_relaxed);
    clearVisualScope(*p);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->engine.reset();
    p->pendingTriggers.store(0u, std::memory_order_relaxed);
    p->triggerGate = false;
    publishParam(*p, kTriggerParamId, 0.0);
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->visualNote.store(-1, std::memory_order_relaxed);
    p->visualInterval.store(0, std::memory_order_relaxed);
    p->visualGeneration.store(0u, std::memory_order_relaxed);
    clearVisualScope(*p);
    p->active = false;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* p = self(plugin);
    if (!processData) return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(*p, processData->out_events);
    uint32_t pending = p->pendingTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerVoice(*p, 1.0f, 60);

    const clap_input_events_t* events = processData->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (processData->audio_outputs_count == 0u
        || !processData->audio_outputs) {
        while (eventIndex < eventCount) {
            applyEvent(*p, events->get(events, eventIndex++));
        }
        releaseTransientTrigger(*p);
        return p->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }

    const auto& output = processData->audio_outputs[0u];
    if (output.channel_count == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }
    float blockPeak = 0.0f;
    for (uint32_t sample = 0u; sample < processData->frames_count; ++sample) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > sample) break;
            applyEvent(*p, event);
            ++eventIndex;
        }
        float left = 0.0f;
        float right = 0.0f;
        p->engine.processFrame(left, right);
        blockPeak = std::max(blockPeak,
            std::max(std::fabs(left), std::fabs(right)));
        if (++p->visualScopeDivider >= 32u) {
            p->visualScopeDivider = 0u;
            const uint32_t write = p->visualScopeWrite.load(
                std::memory_order_relaxed) % kVisualScopeFrames;
            p->visualScopeL[write].store(left, std::memory_order_relaxed);
            p->visualScopeR[write].store(right, std::memory_order_relaxed);
            p->visualScopeWrite.store((write + 1u) % kVisualScopeFrames,
                std::memory_order_release);
        }
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            const float value = channel == 0u ? left
                : (channel == 1u ? right : 0.0f);
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][sample] = value;
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][sample] = value;
            }
        }
    }
    while (eventIndex < eventCount) {
        applyEvent(*p, events->get(events, eventIndex++));
    }
    p->active = p->engine.active();
    const float previous = p->visualActivity.load(std::memory_order_relaxed);
    p->visualActivity.store(std::max(blockPeak, previous * 0.84f),
        std::memory_order_relaxed);
    releaseTransientTrigger(*p);
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
    std::strncpy(info->name, "Stereo Out", sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_STEREO;
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
    std::strncpy(info->name, "Errant Notes In", sizeof(info->name) - 1u);
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

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramDef(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

const char* modeName(double value)
{
    switch (static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 2.0))) {
    case 0u: return "CELL";
    case 1u: return "PHRASE";
    default: return "FIELD";
    }
}

const char* topologyName(double value)
{
    switch (static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 3.0))) {
    case 0u: return "SPINE";
    case 1u: return "WINGS";
    case 2u: return "EXCHANGE";
    default: return "SIDE";
    }
}

const char* keyRoleName(double value)
{
    switch (static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 2.0))) {
    case 0u: return "PITCH";
    case 1u: return "CLOCK";
    default: return "BOTH";
    }
}

double cutoffFrequency(double value)
{
    const double normalized = std::clamp((value + 1.0) * 0.5, 0.0, 1.0);
    return 45.0 * std::pow(220.0, normalized);
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kModeParamId) {
        std::snprintf(display, size, "%s", modeName(value));
    } else if (id == kTopologyParamId) {
        std::snprintf(display, size, "%s", topologyName(value));
    } else if (id == kKeyRoleParamId) {
        std::snprintf(display, size, "%s", keyRoleName(value));
    } else if (id == kRegisterParamId) {
        std::snprintf(display, size, "%+.1f st", value);
    } else if (id == kToneParamId) {
        std::snprintf(display, size, "%.0f Hz", cutoffFrequency(value));
    } else if (id == kFilterContourParamId) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
    } else if (id == kSeedParamId) {
        std::snprintf(display, size, "%u",
            static_cast<uint32_t>(std::round(value)));
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kMidiReceiveParamId) {
        s3g::drum_midi::valueToText(value, display, size);
    } else if (id == kTriggerParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "RUN" : "READY");
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
    if (id == kMidiReceiveParamId) {
        return s3g::drum_midi::textToValue(display, value);
    }
    if (id == kModeParamId) {
        if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "CELL")) *value = 0.0;
        else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "PHRASE")) *value = 1.0;
        else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "FIELD")) *value = 2.0;
        else return false;
        return true;
    }
    if (id == kTopologyParamId) {
        if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "SPINE")) *value = 0.0;
        else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "WINGS")) *value = 1.0;
        else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "EXCHANGE")) *value = 2.0;
        else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "SIDE")) *value = 3.0;
        else return false;
        return true;
    }
    if (id == kKeyRoleParamId) {
        if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "PITCH")) *value = 0.0;
        else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "CLOCK")) *value = 1.0;
        else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "BOTH")) *value = 2.0;
        else return false;
        return true;
    }
    if (id == kTriggerParamId) {
        if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "READY")) {
            *value = 0.0;
        } else if (s3g::drum_midi::asciiEqualsIgnoreCase(display, "RUN")) {
            *value = 1.0;
        } else {
            return false;
        }
        return true;
    }
    if (id == kToneParamId) {
        errno = 0;
        char* end = nullptr;
        const double parsed = std::strtod(display, &end);
        if (end == display || errno == ERANGE || !std::isfinite(parsed)
            || parsed <= 0.0) return false;
        while (*end != '\0' && std::isspace(
                static_cast<unsigned char>(*end)) != 0) ++end;
        if ((end[0] == 'H' || end[0] == 'h')
            && (end[1] == 'Z' || end[1] == 'z')) end += 2;
        while (*end != '\0' && std::isspace(
                static_cast<unsigned char>(*end)) != 0) ++end;
        if (*end != '\0') return false;
        const double normalized = std::log(parsed / 45.0) / std::log(220.0);
        *value = clampValue(*def, normalized * 2.0 - 1.0);
        return true;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || errno == ERANGE || !std::isfinite(parsed)) return false;
    while (*end != '\0' && std::isspace(
            static_cast<unsigned char>(*end)) != 0) ++end;
    bool percent = false;
    if (*end == '%') {
        percent = true;
        ++end;
    }
    while (*end != '\0' && std::isspace(
            static_cast<unsigned char>(*end)) != 0) ++end;
    if (*end != '\0' && id != kRegisterParamId && id != kOutputParamId) {
        return false;
    }
    *value = clampValue(*def, parsed * (percent ? 0.01 : 1.0));
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
    releaseTransientTrigger(*p);
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
    SavedState state {};
    if (!s3g::clap_state::readAll(
            stream, &state.header, sizeof(state.header))) return false;
    const bool current = state.header.magic == kStateMagic
        && state.header.version == kStateVersion
        && state.header.valueCount == kSavedParamCount;
    const bool versionTwo = state.header.magic == kStateMagic
        && state.header.version == 2u && state.header.valueCount == 19u;
    const bool versionOne = state.header.magic == kStateMagic
        && state.header.version == 1u && state.header.valueCount == 18u;
    if (!current && !versionTwo && !versionOne) return false;
    state.values.fill(0.0);
    if (!s3g::clap_state::readAll(stream, state.values.data(),
            static_cast<size_t>(state.header.valueCount) * sizeof(double))) {
        return false;
    }
    // v0.1 states predate Key Role and therefore retain their original
    // pitch-only MIDI interpretation.
    if (state.header.version == 1u) state.values[18u] = 0.0;
    if (state.header.version < 3u) {
        state.values[19u] = 0.62;
        state.values[20u] = 0.38;
        state.values[21u] = 0.46;
        state.values[22u] = 0.34;
    }
    auto* p = self(plugin);
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        applyParam(*p, kParamDefs[index].id, state.values[index]);
    }
    p->pendingTriggers.store(0u, std::memory_order_relaxed);
    p->triggerGate = false;
    publishParam(*p, kTriggerParamId, 0.0);
    if (p->host && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const double seconds = 2.5 + 13.0 * paramValue(*p, kSpanParamId);
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * p->sampleRate)));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)

constexpr CGFloat kLeftPanelX = 16.0;
constexpr CGFloat kRightPanelX = 470.0;
constexpr CGFloat kPanelWidth = 434.0;
constexpr auto kGrammarPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::EventTiming,
    { kLeftPanelX, 50.0, kPanelWidth, 216.0 }, 36.0, 24.0, 7u };
constexpr auto kVoicePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Source,
    { kLeftPanelX, 278.0, kPanelWidth, 288.0 }, 36.0, 24.0, 10u };
constexpr auto kStereoPanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Relationships,
    { kRightPanelX, 50.0, kPanelWidth, 96.0 }, 36.0, 24.0, 2u };
constexpr auto kPerformancePanel = s3g::gui_layout::Panel {
    s3g::gui_layout::PluginClass::EffectProcessor,
    s3g::gui_layout::PanelRole::Output,
    { kRightPanelX, 158.0, kPanelWidth, 144.0 }, 36.0, 24.0, 4u };
constexpr s3g::gui_layout::Rect kAncestryFrame {
    kRightPanelX, 314.0, kPanelWidth, 200.0 };

struct ErrantUiRow {
    clap_id id;
    const char* label;
    const s3g::gui_layout::Panel* panel;
    uint32_t row;
};

constexpr std::array<ErrantUiRow, kSavedParamCount> kUiRows {{
    { kModeParamId, "MODE", &kGrammarPanel, 0u },
    { kSpanParamId, "SPAN", &kGrammarPanel, 1u },
    { kDensityParamId, "DENSITY", &kGrammarPanel, 2u },
    { kAncestryParamId, "ANCESTRY", &kGrammarPanel, 3u },
    { kMutationParamId, "MUTATION", &kGrammarPanel, 4u },
    { kRepeatParamId, "REPEAT", &kGrammarPanel, 5u },
    { kCoherenceParamId, "COHERENCE", &kGrammarPanel, 6u },
    { kMaterialParamId, "GROWL", &kVoicePanel, 0u },
    { kRegisterParamId, "REGISTER", &kVoicePanel, 1u },
    { kKeyRoleParamId, "KEY ROLE", &kVoicePanel, 2u },
    { kNoteTrackingParamId, "PITCH GRAVITY", &kVoicePanel, 3u },
    { kToneParamId, "CUTOFF", &kVoicePanel, 4u },
    { kResonanceParamId, "RESONANCE", &kVoicePanel, 5u },
    { kFilterContourParamId, "CONTOUR", &kVoicePanel, 6u },
    { kSubParamId, "SUB", &kVoicePanel, 7u },
    { kDriveParamId, "DRIVE", &kVoicePanel, 8u },
    { kCrosswireParamId, "CROSSWIRE", &kVoicePanel, 9u },
    { kTopologyParamId, "TOPOLOGY", &kStereoPanel, 0u },
    { kWidthParamId, "WIDTH", &kStereoPanel, 1u },
    { kOutputParamId, "OUTPUT", &kPerformancePanel, 0u },
    { kSeedParamId, "SEED", &kPerformancePanel, 1u },
    { kVelocityParamId, "VELOCITY", &kPerformancePanel, 2u },
    { kMidiReceiveParamId, "MIDI RECEIVE", &kPerformancePanel, 3u },
}};

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    return std::clamp((value - def->minimum)
        / std::max(1.0e-12, def->maximum - def->minimum), 0.0, 1.0);
}

double uiValueFromNormalized(clap_id id, double normalized)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    normalized = std::clamp(normalized, 0.0, 1.0);
    return clampValue(*def,
        def->minimum + (def->maximum - def->minimum) * normalized);
}

@interface S3GProcessorErrantView : NSView {
    void* _plugin;
    int _dragParam;
    NSTimer* _timer;
    char _presetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateDraggedParam:(NSPoint)point;
@end

@implementation S3GProcessorErrantView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        _timer = nil;
        std::snprintf(_presetName, sizeof(_presetName), "%s", "INIT");
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 24.0
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
    if (![self isHidden] && p && s3g::clap_support::hostAppIsActive()) {
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
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    const float activity = std::clamp(
        p->visualActivity.load(std::memory_order_relaxed), 0.0f, 1.0f);
    s3g::clap_gui::drawProcessorTitleBand(
        @"s3g PROCESSOR ERRANT",
        [NSString stringWithUTF8String:_presetName],
        [NSString stringWithFormat:@"PK %3.0f%%", activity * 100.0f],
        titleBand, s3g::clap_gui::softTitleAttrs(), labels, values, style);

    const auto drawPanel = [&](NSString* name,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel.frame.x, panel.frame.y,
            panel.frame.width, panel.frame.height, style);
        s3g::clap_gui::drawPanelHeader(name, true,
            panel.frame.x, panel.frame.y, panel.frame.width,
            s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);
    };
    drawPanel(@"GENEALOGY / EVENT GRAMMAR", kGrammarPanel);
    drawPanel(@"BASS LAB / MIDI-ROOTED LOW CORE", kVoicePanel);
    drawPanel(@"STEREO / UPPER BODY", kStereoPanel);
    drawPanel(@"PATCH / PERFORMANCE", kPerformancePanel);
    s3g::clap_gui::drawPanelFrame(kAncestryFrame.x, kAncestryFrame.y,
        kAncestryFrame.width, kAncestryFrame.height, style);
    s3g::clap_gui::drawPanelHeader(@"2 CHANNEL OUTPUT / FAMILY ARCHIVE", true,
        kAncestryFrame.x, kAncestryFrame.y, kAncestryFrame.width,
        s3g::gui_layout::kStandardMetrics.headerHeight, labels, style);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        s3g::clap_gui::drawProcessorSlider(
            [NSString stringWithUTF8String:row.label],
            [NSString stringWithUTF8String:text],
            uiNormalizedValue(row.id, value),
            s3g::gui_layout::rowY(*row.panel, row.row),
            row.panel->frame.x, row.panel->frame.width,
            labels, values, style);
    }

    const NSRect trigger = NSMakeRect(
        kAncestryFrame.x + 16.0, kAncestryFrame.y + 154.0,
        kAncestryFrame.width - 32.0, 25.0);
    s3g::clap_gui::drawHeaderButton(trigger,
        s3g::clap_gui::cocoaRect(kAncestryFrame), @"GENERATE",
        activity > 0.02f, values, style);

    const NSRect scope = NSMakeRect(kAncestryFrame.x + 16.0,
        kAncestryFrame.y + 32.0, kAncestryFrame.width - 32.0, 70.0);
    [style.cellBg setFill];
    NSRectFill(scope);
    [[NSColor colorWithCalibratedWhite:0.34 alpha:0.36] setStroke];
    for (uint32_t lane = 0u; lane < 2u; ++lane) {
        const CGFloat y = scope.origin.y + scope.size.height
            * (0.25 + 0.50 * lane);
        NSBezierPath* rail = [NSBezierPath bezierPath];
        [rail moveToPoint:NSMakePoint(scope.origin.x, y)];
        [rail lineToPoint:NSMakePoint(NSMaxX(scope), y)];
        [rail setLineWidth:0.7];
        [rail stroke];
    }
    const uint32_t scopeWrite = p->visualScopeWrite.load(
        std::memory_order_acquire);
    const std::array<NSColor*, 2u> traceColors {{
        [NSColor colorWithCalibratedRed:0.35 green:0.73 blue:0.79
            alpha:0.58 + activity * 0.38],
        [NSColor colorWithCalibratedRed:0.56 green:0.46 blue:0.78
            alpha:0.58 + activity * 0.38],
    }};
    for (uint32_t lane = 0u; lane < 2u; ++lane) {
        NSBezierPath* trace = [NSBezierPath bezierPath];
        [trace setLineWidth:0.9 + activity * 0.8];
        const CGFloat center = scope.origin.y + scope.size.height
            * (0.25 + 0.50 * lane);
        for (uint32_t point = 0u; point < kVisualScopeFrames; ++point) {
            const uint32_t index = (scopeWrite + point) % kVisualScopeFrames;
            const float sample = lane == 0u
                ? p->visualScopeL[index].load(std::memory_order_relaxed)
                : p->visualScopeR[index].load(std::memory_order_relaxed);
            const CGFloat x = scope.origin.x + scope.size.width
                * point / static_cast<CGFloat>(kVisualScopeFrames - 1u);
            const CGFloat y = center - std::clamp(sample, -1.0f, 1.0f) * 13.0;
            if (point == 0u) [trace moveToPoint:NSMakePoint(x, y)];
            else [trace lineToPoint:NSMakePoint(x, y)];
        }
        [traceColors[lane] setStroke];
        [trace stroke];
        [[NSString stringWithFormat:@"%u", lane + 1u]
            drawAtPoint:NSMakePoint(scope.origin.x + 5.0, center - 7.0)
            withAttributes:labels];
    }
    const int32_t note = p->visualNote.load(std::memory_order_relaxed);
    const int32_t interval = p->visualInterval.load(std::memory_order_relaxed);
    const uint32_t generation = p->visualGeneration.load(
        std::memory_order_relaxed);
    static const char* noteNames[] {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    NSString* noteText = note >= 0
        ? [NSString stringWithFormat:@"MIDI %s%d    INTERVAL %+d    BRANCH %03u",
            noteNames[note % 12], note / 12 - 1, interval, generation]
        : @"MIDI —    FAMILY ARCHIVE READY";
    [noteText drawAtPoint:NSMakePoint(kAncestryFrame.x + 24.0,
        kAncestryFrame.y + 110.0) withAttributes:labels];
    [@"MIDI ROOT → OSC SPINE → LADDER → PROTECTED SUB"
        drawAtPoint:NSMakePoint(kAncestryFrame.x + 24.0,
            kAncestryFrame.y + 126.0) withAttributes:labels];
    [@"FAMILY ARCHIVE → CROSSWIRE → UPPER BODY"
        drawAtPoint:NSMakePoint(kAncestryFrame.x + 24.0,
            kAncestryFrame.y + 141.0) withAttributes:labels];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const auto id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const ErrantUiRow& candidate) { return candidate.id == id; });
    if (row == kUiRows.end()) return;
    const double controlX = s3g::gui_layout::processorControlX(
        row->panel->frame.x);
    const double trackWidth = s3g::gui_layout::processorTrackWidth(
        row->panel->frame.width);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:
        [event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (s3g::clap_gui::handleProcessorTitleClick(point, &p->plugin,
            @"Processor Errant", titleBand,
            _presetName, sizeof(_presetName), kOutputParamId)) {
        [self setNeedsDisplay:YES];
        return;
    }
    const NSRect trigger = NSMakeRect(
        kAncestryFrame.x + 16.0, kAncestryFrame.y + 154.0,
        kAncestryFrame.width - 32.0, 25.0);
    if (NSPointInRect(point, trigger)) {
        queueGuiTrigger(*p);
        [self setNeedsDisplay:YES];
        return;
    }
    for (const auto& row : kUiRows) {
        if (!NSPointInRect(point, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(*row.panel, row.row)))) {
            continue;
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
        if (row.id != kOutputParamId && row.id != kMidiReceiveParamId) {
            std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
        }
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
    p->guiView = [[S3GProcessorErrantView alloc] initWithPlugin:p];
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
    [static_cast<S3GProcessorErrantView*>(p->guiView) stopRefreshTimer];
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
    [static_cast<S3GProcessorErrantView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GProcessorErrantView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
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
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.processor-errant",
    "s3g Processor Errant",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.4.1",
    "A MIDI-rooted heavy bass synthesizer with a protected sub spine and cross-wired causal genealogy.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    for (uint32_t index = 0u; index < kSavedParamCount; ++index) {
        applyParam(*p, kParamDefs[index].id, kParamDefs[index].defaultValue);
    }
    applyParam(*p, kTriggerParamId, 0.0);
    p->host = host;
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
