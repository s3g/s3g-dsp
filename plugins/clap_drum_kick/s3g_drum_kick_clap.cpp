#include "s3g_drum_kick.h"
#include "s3g_drum_kick_presets.h"
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

constexpr uint32_t kStateMagic = 0x4b473353u; // "S3GK" in little endian.
constexpr uint32_t kStateVersion = 2u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 680u;

constexpr clap_id kTuneParamId = 1u;
constexpr clap_id kNoteTrackingParamId = 2u;
constexpr clap_id kPitchDropParamId = 3u;
constexpr clap_id kPitchSweepTimeParamId = 4u;
constexpr clap_id kPitchSettleParamId = 5u;
constexpr clap_id kBodyParamId = 6u;
constexpr clap_id kHarmonicsParamId = 7u;
constexpr clap_id kDecayParamId = 8u;
constexpr clap_id kTailParamId = 9u;
constexpr clap_id kPunchParamId = 10u;
constexpr clap_id kClickParamId = 11u;
constexpr clap_id kClickToneParamId = 12u;
constexpr clap_id kClickDecayParamId = 13u;
constexpr clap_id kTextureParamId = 14u;
constexpr clap_id kTextureToneParamId = 15u;
constexpr clap_id kTextureDecayParamId = 16u;
constexpr clap_id kDriveParamId = 17u;
constexpr clap_id kBiasParamId = 18u;
constexpr clap_id kCompressionParamId = 19u;
constexpr clap_id kRateReductionParamId = 20u;
constexpr clap_id kBitDepthParamId = 21u;
constexpr clap_id kReconstructionParamId = 22u;
constexpr clap_id kCharacterToneParamId = 23u;
constexpr clap_id kStereoWidthParamId = 24u;
constexpr clap_id kVelocityParamId = 25u;
constexpr clap_id kOutputParamId = 26u;
constexpr clap_id kTriggerParamId = 27u;
constexpr clap_id kMidiReceiveParamId = 28u;
constexpr uint32_t kParamCount = 28u;
constexpr uint32_t kSavedParamCount = kParamCount - 1u;
constexpr uint32_t kLegacySavedParamCount = kSavedParamCount - 1u;

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
    { kTuneParamId, "Tune", "Voice", 20.0, 180.0, 48.0, false },
    { kNoteTrackingParamId, "Note Tracking", "Voice / MIDI", 0.0, 1.0, 1.0, false },
    { kPitchDropParamId, "Pitch Drop", "Voice / Pitch", -12.0, 60.0, 24.0, false },
    { kPitchSweepTimeParamId, "Sweep Time", "Voice / Pitch", 1.0, 500.0, 45.0, false },
    { kPitchSettleParamId, "Pitch Settle", "Voice / Pitch", 0.0, 1.0, 0.25, false },
    { kBodyParamId, "Body", "Voice", 0.0, 1.0, 0.25, false },
    { kHarmonicsParamId, "Harmonics", "Voice", 0.0, 1.0, 0.10, false },
    { kDecayParamId, "Decay", "Voice", 0.02, 8.0, 0.85, false },
    { kTailParamId, "Tail", "Voice", 0.0, 1.0, 0.22, false },
    { kPunchParamId, "Punch", "Attack", 0.0, 1.0, 0.72, false },
    { kClickParamId, "Click", "Attack", 0.0, 1.0, 0.16, false },
    { kClickToneParamId, "Click Tone", "Attack", 0.0, 1.0, 0.55, false },
    { kClickDecayParamId, "Click Decay", "Attack", 0.25, 40.0, 6.5, false },
    { kTextureParamId, "Texture", "Texture", 0.0, 1.0, 0.04, false },
    { kTextureToneParamId, "Texture Tone", "Texture", 0.0, 1.0, 0.45, false },
    { kTextureDecayParamId, "Texture Decay", "Texture", 0.01, 4.0, 0.12, false },
    { kDriveParamId, "Drive", "Character", 0.0, 1.0, 0.0, false },
    { kBiasParamId, "Bias", "Character", -1.0, 1.0, 0.0, false },
    { kCompressionParamId, "Compression", "Character", 0.0, 1.0, 0.0, false },
    { kRateReductionParamId, "Rate Reduction", "Character / Sampler", 0.0, 1.0, 0.0, false },
    { kBitDepthParamId, "Bit Depth Reduction", "Character / Sampler", 0.0, 1.0, 0.0, false },
    { kReconstructionParamId, "Reconstruction", "Character / Sampler", 0.0, 1.0, 0.0, false },
    { kCharacterToneParamId, "Character Tone", "Character", -1.0, 1.0, 0.0, false },
    { kStereoWidthParamId, "Stereo Width", "Output", 0.0, 1.0, 0.0, false },
    { kVelocityParamId, "Velocity Sensitivity", "MIDI", 0.0, 1.0, 0.90, false },
    { kOutputParamId, "Output Gain", "Output", -36.0, 12.0, -6.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "MIDI / Routing", 0.0, 16.0, 0.0, true },
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
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    s3g::DrumKickParams params {};
    s3g::DrumKick kick;
    double midiReceive = 0.0;
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<uint32_t> pendingTriggers { 0u };
    std::atomic<bool> tailChangePending { false };
    std::atomic<uint64_t> parameterRevision { 0u };
    std::atomic<float> visualActivity { 0.0f };
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
    if (id < kTuneParamId || id > kMidiReceiveParamId) return;
    p.publishedParams[id - kTuneParamId].store(
        value, std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    if (id < kTuneParamId || id > kMidiReceiveParamId) return 0.0;
    return p.publishedParams[id - kTuneParamId].load(
        std::memory_order_acquire);
}

void triggerKick(Plugin& p, float velocity, int midiNote)
{
    p.kick.trigger(std::clamp(velocity, 0.0f, 1.0f),
        std::clamp(midiNote, 0, 127));
    p.active = true;
}

void releaseTransientTrigger(Plugin& p)
{
    if (!p.triggerGate) return;
    p.triggerGate = false;
    publishParam(p, kTriggerParamId, 0.0);
}

void notifyTailChanged(Plugin& p)
{
    if (p.host && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

void applyParam(Plugin& p, clap_id id, double value,
    bool triggerImmediately = false)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kMidiReceiveParamId) {
        p.midiReceive = value;
        publishParam(p, id, value);
        p.parameterRevision.fetch_add(1u, std::memory_order_release);
        return;
    }
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
    bool tailChanged = false;
    switch (id) {
    case kTuneParamId: p.params.tuneHz = v; break;
    case kNoteTrackingParamId: p.params.noteTracking = v; break;
    case kPitchDropParamId: p.params.pitchDropSemitones = v; break;
    case kPitchSweepTimeParamId: p.params.pitchSweepMs = v; break;
    case kPitchSettleParamId: p.params.pitchSettle = v; break;
    case kBodyParamId: p.params.body = v; break;
    case kHarmonicsParamId: p.params.harmonics = v; break;
    case kDecayParamId:
        tailChanged = std::fabs(p.params.decaySeconds - v) > 1.0e-6f;
        p.params.decaySeconds = v;
        break;
    case kTailParamId:
        tailChanged = std::fabs(p.params.tail - v) > 1.0e-6f;
        p.params.tail = v;
        break;
    case kPunchParamId: p.params.punch = v; break;
    case kClickParamId: p.params.click = v; break;
    case kClickToneParamId: p.params.clickTone = v; break;
    case kClickDecayParamId:
        tailChanged = std::fabs(p.params.clickDecayMs - v) > 1.0e-6f;
        p.params.clickDecayMs = v;
        break;
    case kTextureParamId: p.params.texture = v; break;
    case kTextureToneParamId: p.params.textureTone = v; break;
    case kTextureDecayParamId:
        tailChanged = std::fabs(
            p.params.textureDecaySeconds - v) > 1.0e-6f;
        p.params.textureDecaySeconds = v;
        break;
    case kDriveParamId: p.params.character.drive = v; break;
    case kBiasParamId: p.params.character.bias = v; break;
    case kCompressionParamId: p.params.character.compression = v; break;
    case kRateReductionParamId:
        p.params.character.sampleRateReduction = v;
        break;
    case kBitDepthParamId:
        p.params.character.bitDepthReduction = v;
        break;
    case kReconstructionParamId:
        p.params.character.reconstruction = v;
        break;
    case kCharacterToneParamId: p.params.character.tone = v; break;
    case kStereoWidthParamId: p.params.stereoWidth = v; break;
    case kVelocityParamId: p.params.velocitySensitivity = v; break;
    case kOutputParamId: p.params.outputGainDb = v; break;
    default: return;
    }
    p.kick.setParams(p.params);
    p.params = p.kick.params();
    publishParam(p, id, rawParamValue(p, id));
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (tailChanged) {
        // CLAP permits host_tail.changed() on the audio thread only. Parameter
        // updates can also arrive from state loading or an inactive flush, so
        // coalesce the notification and deliver it from process().
        p.tailChangePending.store(true, std::memory_order_release);
        if (p.host && p.host->request_process) {
            p.host->request_process(p.host);
        }
    }
}

double rawParamValue(const Plugin& p, clap_id id)
{
    switch (id) {
    case kTuneParamId: return p.params.tuneHz;
    case kNoteTrackingParamId: return p.params.noteTracking;
    case kPitchDropParamId: return p.params.pitchDropSemitones;
    case kPitchSweepTimeParamId: return p.params.pitchSweepMs;
    case kPitchSettleParamId: return p.params.pitchSettle;
    case kBodyParamId: return p.params.body;
    case kHarmonicsParamId: return p.params.harmonics;
    case kDecayParamId: return p.params.decaySeconds;
    case kTailParamId: return p.params.tail;
    case kPunchParamId: return p.params.punch;
    case kClickParamId: return p.params.click;
    case kClickToneParamId: return p.params.clickTone;
    case kClickDecayParamId: return p.params.clickDecayMs;
    case kTextureParamId: return p.params.texture;
    case kTextureToneParamId: return p.params.textureTone;
    case kTextureDecayParamId: return p.params.textureDecaySeconds;
    case kDriveParamId: return p.params.character.drive;
    case kBiasParamId: return p.params.character.bias;
    case kCompressionParamId: return p.params.character.compression;
    case kRateReductionParamId: return p.params.character.sampleRateReduction;
    case kBitDepthParamId: return p.params.character.bitDepthReduction;
    case kReconstructionParamId: return p.params.character.reconstruction;
    case kCharacterToneParamId: return p.params.character.tone;
    case kStereoWidthParamId: return p.params.stereoWidth;
    case kVelocityParamId: return p.params.velocitySensitivity;
    case kOutputParamId: return p.params.outputGainDb;
    case kMidiReceiveParamId: return p.midiReceive;
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
        if (note->velocity > 0.0
            && s3g::drum_midi::accepts(p.midiReceive, note->channel)) {
            triggerKick(p, static_cast<float>(note->velocity), note->key);
        }
    } else if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if ((midi->data[0] & 0xf0u) == 0x90u && midi->data[2] > 0u
            && s3g::drum_midi::accepts(
                p.midiReceive, midi->data[0] & 0x0fu)) {
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
        p->hostTail = static_cast<const clap_host_tail_t*>(
            p->host->get_extension(p->host, CLAP_EXT_TAIL));
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

bool queueGuiSafeRandomParams(Plugin& p,
    const s3g::DrumKickParams& params)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    struct ParamValue {
        clap_id id;
        double value;
    };
    // Performance/monitoring controls are deliberately absent: Note Tracking
    // (2), Velocity Sensitivity (25), Output Gain (26), and Trigger (27).
    const std::array<ParamValue, 23u> values {{
        { kTuneParamId, params.tuneHz },
        { kPitchDropParamId, params.pitchDropSemitones },
        { kPitchSweepTimeParamId, params.pitchSweepMs },
        { kPitchSettleParamId, params.pitchSettle },
        { kBodyParamId, params.body },
        { kHarmonicsParamId, params.harmonics },
        { kDecayParamId, params.decaySeconds },
        { kTailParamId, params.tail },
        { kPunchParamId, params.punch },
        { kClickParamId, params.click },
        { kClickToneParamId, params.clickTone },
        { kClickDecayParamId, params.clickDecayMs },
        { kTextureParamId, params.texture },
        { kTextureToneParamId, params.textureTone },
        { kTextureDecayParamId, params.textureDecaySeconds },
        { kDriveParamId, params.character.drive },
        { kBiasParamId, params.character.bias },
        { kCompressionParamId, params.character.compression },
        { kRateReductionParamId, params.character.sampleRateReduction },
        { kBitDepthParamId, params.character.bitDepthReduction },
        { kReconstructionParamId, params.character.reconstruction },
        { kCharacterToneParamId, params.character.tone },
        { kStereoWidthParamId, params.stereoWidth },
    }};
    std::array<double, values.size()> clampedValues {};
    std::array<s3g::clap_gui::ParamEvent, values.size() * 3u> events {};
    for (uint32_t index = 0u; index < values.size(); ++index) {
        const auto* def = paramDef(values[index].id);
        if (!def) return false;
        clampedValues[index] = clampValue(*def, values[index].value);
        const uint32_t eventIndex = index * 3u;
        events[eventIndex] = {
            Kind::GestureBegin, values[index].id, 0.0 };
        events[eventIndex + 1u] = {
            Kind::Value, values[index].id, clampedValues[index] };
        events[eventIndex + 2u] = {
            Kind::GestureEnd, values[index].id, 0.0 };
    }
    // pushBatch publishes its write index only after all 69 events are copied.
    // A full queue therefore changes neither host-visible events nor values.
    if (!p.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t index = 0u; index < values.size(); ++index) {
        publishParam(p, values[index].id, clampedValues[index]);
    }
    requestGuiParamService(p);
    return true;
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
    p->kick.prepare(p->sampleRate);
    p->kick.setParams(p->params);
    p->active = false;
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->kick.reset();
    p->pendingTriggers.store(0u, std::memory_order_relaxed);
    p->triggerGate = false;
    publishParam(*p, kTriggerParamId, 0.0);
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->active = false;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto* p = self(plugin);
    if (!process) return CLAP_PROCESS_ERROR;
    if (p->tailChangePending.exchange(false, std::memory_order_acq_rel)) {
        notifyTailChanged(*p);
    }
    serviceGuiParamEvents(*p, process->out_events);
    uint32_t pending = p->pendingTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerKick(*p, 1.0f, 36);

    const clap_input_events_t* events = process->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (process->audio_outputs_count == 0u || !process->audio_outputs) {
        while (eventIndex < eventCount) {
            applyEvent(*p, events->get(events, eventIndex++));
        }
        releaseTransientTrigger(*p);
        return p->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }

    const auto& output = process->audio_outputs[0u];
    if (output.channel_count == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }

    float blockPeak = 0.0f;
    for (uint32_t sample = 0u; sample < process->frames_count; ++sample) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > sample) break;
            applyEvent(*p, event);
            ++eventIndex;
        }

        float left = 0.0f;
        float right = 0.0f;
        p->kick.processFrame(left, right);
        blockPeak = std::max(blockPeak,
            std::max(std::fabs(left), std::fabs(right)));
        for (uint32_t channel = 0u; channel < output.channel_count;
             ++channel) {
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
    p->active = p->kick.active();
    const float previous = p->visualActivity.load(std::memory_order_relaxed);
    p->visualActivity.store(std::max(blockPeak, previous * 0.82f),
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
    std::strncpy(info->name, "Drum Trigger In", sizeof(info->name) - 1u);
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

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kTuneParamId) {
        std::snprintf(display, size, "%.1f Hz", value);
    } else if (id == kPitchDropParamId) {
        std::snprintf(display, size, "%.1f st", value);
    } else if (id == kPitchSweepTimeParamId
        || id == kClickDecayParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kDecayParamId || id == kTextureDecayParamId) {
        std::snprintf(display, size, "%.3g s", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kBiasParamId || id == kCharacterToneParamId) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
    } else if (id == kMidiReceiveParamId) {
        s3g::drum_midi::valueToText(value, display, size);
    } else if (id == kTriggerParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "Hit" : "Ready");
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
    if (id == kTriggerParamId) {
        if (std::strcmp(display, "Hit") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "Ready") == 0) {
            *value = 0.0;
            return true;
        }
    }

    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (*end != '\0' && std::isspace(
            static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }

    const bool percentParam = id != kTuneParamId
        && id != kPitchDropParamId && id != kPitchSweepTimeParamId
        && id != kClickDecayParamId && id != kDecayParamId
        && id != kTextureDecayParamId && id != kOutputParamId
        && id != kMidiReceiveParamId && id != kTriggerParamId;
    const char* expectedSuffix = id == kTuneParamId ? "Hz"
        : (id == kPitchDropParamId ? "st"
        : ((id == kPitchSweepTimeParamId || id == kClickDecayParamId) ? "ms"
        : ((id == kDecayParamId || id == kTextureDecayParamId) ? "s"
        : (id == kOutputParamId ? "dB" : nullptr))));
    bool displayedAsPercent = false;
    if (*end != '\0') {
        const char* suffix = end;
        size_t suffixLength = std::strlen(suffix);
        while (suffixLength > 0u && std::isspace(
                static_cast<unsigned char>(suffix[suffixLength - 1u])) != 0) {
            --suffixLength;
        }
        if (percentParam && suffixLength == 1u && suffix[0] == '%') {
            displayedAsPercent = true;
        } else if (!expectedSuffix
            || suffixLength != std::strlen(expectedSuffix)
            || std::strncmp(suffix, expectedSuffix, suffixLength) != 0) {
            return false;
        }
    }

    *value = parsed * (displayedAsPercent ? 0.01 : 1.0);
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
    if (!s3g::clap_state::readVersionedValues(stream,
            state.header, state.values, kStateMagic, kStateVersion,
            1u, kLegacySavedParamCount)) return false;
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
    const double tailScale = 1.15
        + (4.0 - 1.15) * paramValue(*p, kTailParamId);
    // This mirrors DrumKick's hard upper bound for a voice. Reporting the
    // bound (rather than an envelope estimate) keeps host sleep decisions safe.
    const double seconds = std::min(40.0, std::max({
        2.0,
        paramValue(*p, kDecayParamId) * tailScale * 1.75,
        paramValue(*p, kClickDecayParamId) * 0.001 * 1.75,
        paramValue(*p, kTextureDecayParamId) * 1.75,
    }));
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * p->sampleRate)));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct KickUiRow {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

constexpr CGFloat kLeftPanelX = 16.0;
constexpr CGFloat kRightPanelX = 470.0;
constexpr CGFloat kPanelWidth = 434.0;

constexpr std::array<KickUiRow, kSavedParamCount> kUiRows {{
    { kTuneParamId, "TUNE", kLeftPanelX, kPanelWidth, 80.0 },
    { kNoteTrackingParamId, "NOTE TRACK", kLeftPanelX, kPanelWidth, 104.0 },
    { kPitchDropParamId, "PITCH DROP", kLeftPanelX, kPanelWidth, 128.0 },
    { kPitchSweepTimeParamId, "SWEEP TIME", kLeftPanelX, kPanelWidth, 152.0 },
    { kPitchSettleParamId, "SETTLE", kLeftPanelX, kPanelWidth, 176.0 },
    { kBodyParamId, "BODY", kLeftPanelX, kPanelWidth, 200.0 },
    { kHarmonicsParamId, "HARMONICS", kLeftPanelX, kPanelWidth, 224.0 },
    { kDecayParamId, "DECAY", kLeftPanelX, kPanelWidth, 248.0 },
    { kTailParamId, "TAIL", kLeftPanelX, kPanelWidth, 272.0 },

    { kPunchParamId, "PUNCH", kRightPanelX, kPanelWidth, 80.0 },
    { kClickParamId, "CLICK", kRightPanelX, kPanelWidth, 104.0 },
    { kClickToneParamId, "CLICK TONE", kRightPanelX, kPanelWidth, 128.0 },
    { kClickDecayParamId, "CLICK DECAY", kRightPanelX, kPanelWidth, 152.0 },
    { kTextureParamId, "TEXTURE", kRightPanelX, kPanelWidth, 176.0 },
    { kTextureToneParamId, "TEXTURE TONE", kRightPanelX, kPanelWidth, 200.0 },
    { kTextureDecayParamId, "TEXTURE DECAY", kRightPanelX, kPanelWidth, 224.0 },

    { kDriveParamId, "DRIVE", kLeftPanelX, kPanelWidth, 374.0 },
    { kBiasParamId, "BIAS", kLeftPanelX, kPanelWidth, 398.0 },
    { kCompressionParamId, "COMPRESSION", kLeftPanelX, kPanelWidth, 422.0 },
    { kRateReductionParamId, "RATE REDUCTION", kLeftPanelX, kPanelWidth, 446.0 },
    { kBitDepthParamId, "BIT REDUCTION", kLeftPanelX, kPanelWidth, 470.0 },
    { kReconstructionParamId, "RECONSTRUCTION", kLeftPanelX, kPanelWidth, 494.0 },
    { kCharacterToneParamId, "TONE", kLeftPanelX, kPanelWidth, 518.0 },

    { kStereoWidthParamId, "STEREO WIDTH", kRightPanelX, kPanelWidth, 374.0 },
    { kVelocityParamId, "VELOCITY", kRightPanelX, kPanelWidth, 398.0 },
    { kOutputParamId, "OUTPUT", kRightPanelX, kPanelWidth, 422.0 },
    { kMidiReceiveParamId, "MIDI RECEIVE", kRightPanelX, kPanelWidth, 446.0 },
}};

bool isLogUiParam(clap_id id)
{
    return id == kTuneParamId || id == kPitchSweepTimeParamId
        || id == kDecayParamId || id == kClickDecayParamId
        || id == kTextureDecayParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if (isLogUiParam(id) && def->minimum > 0.0) {
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
    const double value = isLogUiParam(id)
        ? def->minimum * std::pow(def->maximum / def->minimum, normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

s3g::DrumKickParams publishedParamsSnapshot(const Plugin& p)
{
    s3g::DrumKickParams params;
    params.tuneHz = static_cast<float>(paramValue(p, kTuneParamId));
    params.noteTracking = static_cast<float>(paramValue(p, kNoteTrackingParamId));
    params.pitchDropSemitones = static_cast<float>(paramValue(p, kPitchDropParamId));
    params.pitchSweepMs = static_cast<float>(paramValue(p, kPitchSweepTimeParamId));
    params.pitchSettle = static_cast<float>(paramValue(p, kPitchSettleParamId));
    params.body = static_cast<float>(paramValue(p, kBodyParamId));
    params.harmonics = static_cast<float>(paramValue(p, kHarmonicsParamId));
    params.decaySeconds = static_cast<float>(paramValue(p, kDecayParamId));
    params.tail = static_cast<float>(paramValue(p, kTailParamId));
    params.punch = static_cast<float>(paramValue(p, kPunchParamId));
    params.click = static_cast<float>(paramValue(p, kClickParamId));
    params.clickTone = static_cast<float>(paramValue(p, kClickToneParamId));
    params.clickDecayMs = static_cast<float>(paramValue(p, kClickDecayParamId));
    params.texture = static_cast<float>(paramValue(p, kTextureParamId));
    params.textureTone = static_cast<float>(paramValue(p, kTextureToneParamId));
    params.textureDecaySeconds = static_cast<float>(
        paramValue(p, kTextureDecayParamId));
    params.character.drive = static_cast<float>(paramValue(p, kDriveParamId));
    params.character.bias = static_cast<float>(paramValue(p, kBiasParamId));
    params.character.compression = static_cast<float>(
        paramValue(p, kCompressionParamId));
    params.character.sampleRateReduction = static_cast<float>(
        paramValue(p, kRateReductionParamId));
    params.character.bitDepthReduction = static_cast<float>(
        paramValue(p, kBitDepthParamId));
    params.character.reconstruction = static_cast<float>(
        paramValue(p, kReconstructionParamId));
    params.character.tone = static_cast<float>(
        paramValue(p, kCharacterToneParamId));
    params.stereoWidth = static_cast<float>(paramValue(p, kStereoWidthParamId));
    params.velocitySensitivity = static_cast<float>(
        paramValue(p, kVelocityParamId));
    params.outputGainDb = static_cast<float>(paramValue(p, kOutputParamId));
    return params;
}

} // namespace

@interface S3GDrumKickView : NSView {
    void* _plugin;
    int _dragParam;
    int _factoryPresetIndex;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    uint64_t _observedParamRevision;
    NSTimer* _timer;
    char _presetName[64];
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)applyFactoryPreset:(int)index;
- (BOOL)applySafeRandom;
- (void)markCustomPreset;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
- (void)updateDraggedParam:(NSPoint)point;
@end

@implementation S3GDrumKickView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        auto* p = static_cast<Plugin*>(plugin);
        _factoryPresetIndex = p
            ? s3g::drumKickFactoryPresetIndex(publishedParamsSnapshot(*p))
            : 0;
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _observedParamRevision = p
            ? p->parameterRevision.load(std::memory_order_acquire) : 0u;
        _timer = nil;
        if (_factoryPresetIndex >= 0) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                s3g::drumKickFactoryPresetInfo(
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
    if (!p || s3g::kDrumKickFactoryPresetCount == 0u) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kDrumKickFactoryPresetCount - 1u));
    const auto preset = s3g::drumKickFactoryPreset(
        static_cast<uint32_t>(index));
    const auto set = [&](clap_id id, double value) {
        queueGuiParamGestureBegin(*p, id);
        queueGuiParamValue(*p, id, value);
        queueGuiParamGestureEnd(*p, id);
    };
    set(kTuneParamId, preset.tuneHz);
    set(kNoteTrackingParamId, preset.noteTracking);
    set(kPitchDropParamId, preset.pitchDropSemitones);
    set(kPitchSweepTimeParamId, preset.pitchSweepMs);
    set(kPitchSettleParamId, preset.pitchSettle);
    set(kBodyParamId, preset.body);
    set(kHarmonicsParamId, preset.harmonics);
    set(kDecayParamId, preset.decaySeconds);
    set(kTailParamId, preset.tail);
    set(kPunchParamId, preset.punch);
    set(kClickParamId, preset.click);
    set(kClickToneParamId, preset.clickTone);
    set(kClickDecayParamId, preset.clickDecayMs);
    set(kTextureParamId, preset.texture);
    set(kTextureToneParamId, preset.textureTone);
    set(kTextureDecayParamId, preset.textureDecaySeconds);
    set(kDriveParamId, preset.character.drive);
    set(kBiasParamId, preset.character.bias);
    set(kCompressionParamId, preset.character.compression);
    set(kRateReductionParamId, preset.character.sampleRateReduction);
    set(kBitDepthParamId, preset.character.bitDepthReduction);
    set(kReconstructionParamId, preset.character.reconstruction);
    set(kCharacterToneParamId, preset.character.tone);
    set(kStereoWidthParamId, preset.stereoWidth);
    set(kVelocityParamId, preset.velocitySensitivity);
    set(kOutputParamId, preset.outputGainDb);
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::drumKickFactoryPresetInfo(static_cast<uint32_t>(index)).name);
    [self setNeedsDisplay:YES];
}

- (BOOL)applySafeRandom
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return NO;
    const auto randomized = s3g::drumKickSafeRandomParams(
        publishedParamsSnapshot(*p), arc4random());
    if (!queueGuiSafeRandomParams(*p, randomized)) return NO;
    [self markCustomPreset];
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)markCustomPreset
{
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s", "CUSTOM");
}

- (NSRect)openMenuRect
{
    if (_openMenu != kFactoryPresetMenuId) return NSZeroRect;
    const auto band = s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight);
    const NSRect anchor = s3g::clap_gui::cocoaRect(band.presetMenu);
    return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width, 18.0 * _menuItemCount);
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu != kFactoryPresetMenuId || _menuItemCount == 0u) return;
    NSString* items[64] {};
    const uint32_t count = std::min<uint32_t>(_menuItemCount, 64u);
    for (uint32_t index = 0u; index < count; ++index) {
        items[index] = [NSString stringWithUTF8String:
            s3g::drumKickFactoryPresetInfo(index).name];
    }
    s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
        items, count, _factoryPresetIndex, _hoverMenuItem, attrs, style);
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    auto* p = static_cast<Plugin*>(_plugin);
    if (p) {
        const uint64_t revision = p->parameterRevision.load(
            std::memory_order_acquire);
        if (revision != _observedParamRevision) {
            _observedParamRevision = revision;
            _factoryPresetIndex = s3g::drumKickFactoryPresetIndex(
                publishedParamsSnapshot(*p));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::drumKickFactoryPresetInfo(static_cast<uint32_t>(
                        _factoryPresetIndex)).name
                    : "CUSTOM");
        }
    }
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
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    const float activity = std::clamp(
        p->visualActivity.load(std::memory_order_relaxed), 0.0f, 1.0f);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g DRUM KICK",
        [NSString stringWithUTF8String:_presetName],
        [NSString stringWithFormat:@"LEVEL %3.0f%%", activity * 100.0f],
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const NSRect voicePanel = NSMakeRect(kLeftPanelX, 50.0,
        kPanelWidth, 262.0);
    const NSRect attackPanel = NSMakeRect(kRightPanelX, 50.0,
        kPanelWidth, 262.0);
    const NSRect characterPanel = NSMakeRect(kLeftPanelX, 344.0,
        kPanelWidth, 218.0);
    const NSRect outputPanel = NSMakeRect(kRightPanelX, 344.0,
        kPanelWidth, 320.0);
    const auto drawPanel = [&](NSString* name, NSRect rect) {
        s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        s3g::clap_gui::drawPanelHeader(name, true, rect.origin.x,
            rect.origin.y, rect.size.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labelAttrs, style);
    };
    drawPanel(@"VOICE / PITCH", voicePanel);
    drawPanel(@"ATTACK / TEXTURE", attackPanel);
    drawPanel(@"SHARED DRUM CHARACTER", characterPanel);
    drawPanel(@"PERFORMANCE / OUTPUT", outputPanel);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
        s3g::clap_gui::drawProcessorSlider(
            [NSString stringWithUTF8String:row.label],
            [NSString stringWithUTF8String:text],
            static_cast<CGFloat>(uiNormalizedValue(row.id, value)), row.y,
            row.panelX, row.panelWidth, labelAttrs, valueAttrs, style);
    }

    const NSRect trigger = NSMakeRect(kRightPanelX + 12.0, 480.0,
        kPanelWidth - 24.0, 24.0);
    s3g::clap_gui::drawHeaderButton(trigger, outputPanel, @"TRIGGER",
        activity > 0.02f, valueAttrs, style);

    const NSPoint center = NSMakePoint(687.0, 568.0);
    for (uint32_t ring = 0u; ring < 4u; ++ring) {
        const CGFloat baseRadius = 22.0 + ring * 17.0;
        const CGFloat pulse = activity * (26.0 - ring * 4.0);
        const CGFloat radius = baseRadius + pulse;
        const CGFloat alpha = 0.16 + activity * (0.65 - ring * 0.08);
        [[NSColor colorWithCalibratedRed:0.30 + activity * 0.38
            green:0.62 + activity * 0.24 blue:0.66 + activity * 0.20
            alpha:alpha] setStroke];
        NSBezierPath* path = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            center.x - radius, center.y - radius,
            radius * 2.0, radius * 2.0)];
        [path setLineWidth:1.0 + activity * 2.0];
        [path stroke];
    }
    [[NSColor colorWithCalibratedWhite:0.90 alpha:0.85] setFill];
    const CGFloat coreRadius = 5.0 + activity * 12.0;
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        center.x - coreRadius, center.y - coreRadius,
        coreRadius * 2.0, coreRadius * 2.0)] fill];
    [@"SYNTHESIZED VOICE  //  CENTERED LOW BODY"
        drawAtPoint:NSMakePoint(kRightPanelX + 78.0, 646.0)
        withAttributes:labelAttrs];
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const auto id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const KickUiRow& candidate) { return candidate.id == id; });
    if (row == kUiRows.end()) return;
    const double controlX = s3g::gui_layout::processorControlX(row->panelX);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(row->panelWidth);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* p = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*p, id, uiValueFromNormalized(id, normalized));
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
        if (hit >= 0) [self applyFactoryPreset:hit];
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
        _menuItemCount = std::min<uint32_t>(
            s3g::kDrumKickFactoryPresetCount, 64u);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &p->plugin, @"Drum Kick", kOutputParamId, &name)) {
            _factoryPresetIndex = -1;
            _observedParamRevision = p->parameterRevision.load(
                std::memory_order_acquire);
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
                &p->plugin, @"Drum Kick", &name)) {
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
        if (![self applySafeRandom]) NSBeep();
        return;
    }
    const NSRect trigger = NSMakeRect(kRightPanelX + 12.0, 480.0,
        kPanelWidth - 24.0, 24.0);
    if (NSPointInRect(point, trigger)) {
        queueGuiTrigger(*p);
        [self setNeedsDisplay:YES];
        return;
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
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*p, row.id);
            [self updateDraggedParam:point];
        }
        if (row.id != kOutputParamId && row.id != kMidiReceiveParamId) {
            [self markCustomPreset];
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
    p->guiView = [[S3GDrumKickView alloc] initWithPlugin:p];
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
    [static_cast<S3GDrumKickView*>(p->guiView) stopRefreshTimer];
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
    [static_cast<S3GDrumKickView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GDrumKickView*>(p->guiView) stopRefreshTimer];
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
    CLAP_PLUGIN_FEATURE_DRUM,
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.drum-kick",
    "s3g Drum Kick 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "A procedural mono-compatible stereo kick synthesizer with layered body, attack, texture, and reusable drum character stages.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    // Establish the CLAP defaults as the wrapper's source of truth. The host
    // is installed afterwards so construction does not emit host callbacks.
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
