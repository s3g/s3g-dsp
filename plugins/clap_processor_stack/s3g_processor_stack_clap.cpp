#include "s3g_processor_stack.h"
#include "s3g_processor_stack_presets.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_drum_midi_receive.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
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

constexpr uint32_t kStateMagic = 0x31545350u; // "PST1" little endian.
constexpr uint32_t kStateVersion = 3u;
constexpr uint32_t kVersionTwoStateVersion = 2u;
constexpr uint32_t kVersionTwoParamCount = 32u;
constexpr uint32_t kVersionOneStateVersion = 1u;
constexpr uint32_t kVersionOneParamCount = 27u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 980u;
constexpr uint32_t kGuiHeight = 856u;

enum ParamId : clap_id {
    kModeParamId = 1u,
    kShapeParamId,
    kWireParamId,
    kPickParamId,
    kDampingParamId,
    kGlideParamId,
    kCrookedParamId,
    kSpillParamId,
    kCircuitParamId,
    kBiteParamId,
    kPedalToneParamId,
    kBiasParamId,
    kStackParamId,
    kSagParamId,
    kFocusParamId,
    kConeParamId,
    kCabinetParamId,
    kMicParamId,
    kFeedbackParamId,
    kProximityParamId,
    kHarmonicParamId,
    kTrackingParamId,
    kPolarityParamId,
    kRootParamId,
    kChaosParamId,
    kOutputParamId,
    kMidiReceiveParamId,
    kArpPatternParamId,
    kScaleParamId,
    kArpRateParamId,
    kArpOctavesParamId,
    kArpGateParamId,
    kCustomLengthParamId,
    kCustomStep1ParamId,
    kCustomStep2ParamId,
    kCustomStep3ParamId,
    kCustomStep4ParamId,
    kCustomStep5ParamId,
    kCustomStep6ParamId,
    kCustomStep7ParamId,
    kCustomStep8ParamId,
    kPierceParamId,
    kSelfListenParamId,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr std::array<ParamDef, 43u> kParamDefs {{
    { kModeParamId, "Mode", "Play", 0.0, 2.0, 0.0, true },
    { kShapeParamId, "Shape", "Play", 0.0, 1.0, 0.58, false },
    { kWireParamId, "String", "Play", 0.0, 1.0, 0.56, false },
    { kPickParamId, "Pick", "Play", 0.0, 1.0, 0.72, false },
    { kDampingParamId, "Damp", "Play", 0.0, 1.0, 0.38, false },
    { kGlideParamId, "Glide", "Play", 0.0, 2000.0, 34.0, false },
    { kCrookedParamId, "Crooked", "Play", 0.0, 1.0, 0.36, false },
    { kSpillParamId, "Spill", "Play", 0.0, 1.0, 0.32, false },
    { kCircuitParamId, "Circuit", "Pedal", 0.0, 7.0, 2.0, true },
    { kBiteParamId, "Bite", "Pedal", 0.0, 1.0, 0.56, false },
    { kPedalToneParamId, "Tone", "Pedal", 0.0, 1.0, 0.54, false },
    { kBiasParamId, "Bias", "Pedal", 0.0, 1.0, 0.52, false },
    { kStackParamId, "Stack", "Amplifier", 0.0, 1.0, 0.62, false },
    { kSagParamId, "Sag", "Amplifier", 0.0, 1.0, 0.46, false },
    { kFocusParamId, "Focus", "Amplifier", 0.0, 1.0, 0.55, false },
    { kConeParamId, "Cone", "Amplifier", 0.0, 1.0, 0.64, false },
    { kCabinetParamId, "Cab", "Amplifier", 0.0, 1.0, 0.52, false },
    { kMicParamId, "Mic", "Amplifier", 0.0, 1.0, 0.34, false },
    { kFeedbackParamId, "Feedback", "Loop", 0.0, 1.0, 0.56, false },
    { kProximityParamId, "Proximity", "Loop", 0.0, 1.0, 0.58, false },
    { kHarmonicParamId, "Harmonic", "Loop", 0.0, 1.0, 0.42, false },
    { kTrackingParamId, "Track", "Loop", 0.0, 1.0, 0.72, false },
    { kPolarityParamId, "Polarity", "Loop", 0.0, 1.0, 0.78, false },
    { kRootParamId, "Root", "Loop", 0.0, 1.0, 0.28, false },
    { kChaosParamId, "Chaos", "Loop", 0.0, 1.0, 0.32, false },
    { kOutputParamId, "Output", "Output", -36.0, 6.0, -12.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "Routing", 0.0, 16.0, 0.0, true },
    { kArpPatternParamId, "Arp Pattern", "Arpeggiator", 0.0, 6.0, 0.0, true },
    { kScaleParamId, "Scale Rule", "Arpeggiator", 0.0, 4.0, 1.0, true },
    { kArpRateParamId, "Arp Rate", "Arpeggiator", 0.0, 5.0, 2.0, true },
    { kArpOctavesParamId, "Arp Octaves", "Arpeggiator", 1.0, 4.0, 2.0, true },
    { kArpGateParamId, "Arp Gate", "Arpeggiator", 0.05, 1.0, 0.62, false },
    { kCustomLengthParamId, "Pattern Length", "Arpeggiator", 1.0, 8.0, 8.0, true },
    { kCustomStep1ParamId, "Pattern Step 1", "Arpeggiator", -8.0, 15.0, 0.0, true },
    { kCustomStep2ParamId, "Pattern Step 2", "Arpeggiator", -8.0, 15.0, 1.0, true },
    { kCustomStep3ParamId, "Pattern Step 3", "Arpeggiator", -8.0, 15.0, 2.0, true },
    { kCustomStep4ParamId, "Pattern Step 4", "Arpeggiator", -8.0, 15.0, 4.0, true },
    { kCustomStep5ParamId, "Pattern Step 5", "Arpeggiator", -8.0, 15.0, 3.0, true },
    { kCustomStep6ParamId, "Pattern Step 6", "Arpeggiator", -8.0, 15.0, 6.0, true },
    { kCustomStep7ParamId, "Pattern Step 7", "Arpeggiator", -8.0, 15.0, 5.0, true },
    { kCustomStep8ParamId, "Pattern Step 8", "Arpeggiator", -8.0, 15.0, 1.0, true },
    { kPierceParamId, "Pierce", "Loop", 0.0, 1.0, 0.68, false },
    { kSelfListenParamId, "Self Listen", "Loop", 0.0, 1.0, 0.72, false },
}};

constexpr uint32_t kSynthParamCount = 42u;
constexpr uint32_t kPublishedParamCount =
    static_cast<uint32_t>(kSelfListenParamId) + 1u;

constexpr std::array<clap_id, kSynthParamCount> kSynthParamIds {{
    kModeParamId, kShapeParamId, kWireParamId, kPickParamId,
    kDampingParamId, kGlideParamId, kCrookedParamId, kSpillParamId,
    kCircuitParamId, kBiteParamId, kPedalToneParamId, kBiasParamId,
    kStackParamId, kSagParamId, kFocusParamId, kConeParamId,
    kCabinetParamId, kMicParamId, kFeedbackParamId, kProximityParamId,
    kHarmonicParamId, kTrackingParamId, kPolarityParamId, kRootParamId,
    kChaosParamId, kOutputParamId,
    kArpPatternParamId, kScaleParamId, kArpRateParamId,
    kArpOctavesParamId, kArpGateParamId,
    kCustomLengthParamId, kCustomStep1ParamId, kCustomStep2ParamId,
    kCustomStep3ParamId, kCustomStep4ParamId, kCustomStep5ParamId,
    kCustomStep6ParamId, kCustomStep7ParamId, kCustomStep8ParamId,
    kPierceParamId, kSelfListenParamId,
}};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t valueCount = static_cast<uint32_t>(kParamDefs.size());
    uint32_t reserved = 0u;
};

struct SavedState {
    SavedStateHeader header {};
    std::array<double, kParamDefs.size()> values {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    s3g::ProcessorStack engine {};
    s3g::ProcessorStackParams params {};
    double midiReceive = 0.0;
    std::array<std::atomic<double>, kPublishedParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<bool> tailChangePending { false };
    std::atomic<uint64_t> parameterRevision { 0u };
    std::atomic<float> outputPeak { 0.0f };
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
    const auto found = std::find_if(kParamDefs.begin(), kParamDefs.end(),
        [id](const ParamDef& def) { return def.id == id; });
    return found == kParamDefs.end() ? nullptr : &*found;
}

double clampValue(const ParamDef& def, double value)
{
    value = std::isfinite(value) ? value : def.defaultValue;
    value = std::clamp(value, def.minimum, def.maximum);
    return def.stepped ? std::round(value) : value;
}

void publishParam(Plugin& plugin, clap_id id, double value)
{
    if (id >= kPublishedParamCount || !paramDef(id)) return;
    plugin.publishedParams[id].store(value, std::memory_order_release);
}

double paramValue(const Plugin& plugin, clap_id id)
{
    if (id >= kPublishedParamCount || !paramDef(id)) return 0.0;
    return plugin.publishedParams[id].load(std::memory_order_acquire);
}

double rawParamValue(const Plugin& plugin, clap_id id)
{
    const auto& params = plugin.params;
    switch (id) {
    case kModeParamId: return static_cast<double>(params.mode);
    case kShapeParamId: return params.shape;
    case kWireParamId: return params.wire;
    case kPickParamId: return params.pick;
    case kDampingParamId: return params.damping;
    case kGlideParamId: return params.glideMs;
    case kCrookedParamId: return params.crooked;
    case kSpillParamId: return params.spill;
    case kCircuitParamId: return static_cast<double>(params.circuit);
    case kBiteParamId: return params.bite;
    case kPedalToneParamId: return params.pedalTone;
    case kBiasParamId: return params.bias;
    case kStackParamId: return params.stack;
    case kSagParamId: return params.sag;
    case kFocusParamId: return params.focus;
    case kConeParamId: return params.cone;
    case kCabinetParamId: return params.cabinet;
    case kMicParamId: return params.mic;
    case kFeedbackParamId: return params.feedback;
    case kProximityParamId: return params.proximity;
    case kHarmonicParamId: return params.harmonic;
    case kTrackingParamId: return params.tracking;
    case kPolarityParamId: return params.polarity;
    case kRootParamId: return params.root;
    case kChaosParamId: return params.chaos;
    case kOutputParamId: return params.outputGainDb;
    case kMidiReceiveParamId: return plugin.midiReceive;
    case kArpPatternParamId: return static_cast<double>(params.arpPattern);
    case kScaleParamId: return static_cast<double>(params.scale);
    case kArpRateParamId: return static_cast<double>(params.arpRate);
    case kArpOctavesParamId: return params.arpOctaves;
    case kArpGateParamId: return params.arpGate;
    case kCustomLengthParamId: return params.customPatternLength;
    case kCustomStep1ParamId: return params.customPattern[0u];
    case kCustomStep2ParamId: return params.customPattern[1u];
    case kCustomStep3ParamId: return params.customPattern[2u];
    case kCustomStep4ParamId: return params.customPattern[3u];
    case kCustomStep5ParamId: return params.customPattern[4u];
    case kCustomStep6ParamId: return params.customPattern[5u];
    case kCustomStep7ParamId: return params.customPattern[6u];
    case kCustomStep8ParamId: return params.customPattern[7u];
    case kPierceParamId: return params.pierce;
    case kSelfListenParamId: return params.selfListen;
    default: return 0.0;
    }
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return;
    value = clampValue(*def, value);
    if (id == kMidiReceiveParamId) {
        plugin.midiReceive = value;
        publishParam(plugin, id, value);
        plugin.parameterRevision.fetch_add(1u, std::memory_order_release);
        return;
    }

    const float normalized = static_cast<float>(value);
    const bool tailChanged = id == kFeedbackParamId || id == kSpillParamId;
    switch (id) {
    case kModeParamId:
        plugin.params.mode = static_cast<s3g::ProcessorStackMode>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kShapeParamId: plugin.params.shape = normalized; break;
    case kWireParamId: plugin.params.wire = normalized; break;
    case kPickParamId: plugin.params.pick = normalized; break;
    case kDampingParamId: plugin.params.damping = normalized; break;
    case kGlideParamId: plugin.params.glideMs = normalized; break;
    case kCrookedParamId: plugin.params.crooked = normalized; break;
    case kSpillParamId: plugin.params.spill = normalized; break;
    case kCircuitParamId:
        plugin.params.circuit = static_cast<s3g::ProcessorStackCircuit>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kBiteParamId: plugin.params.bite = normalized; break;
    case kPedalToneParamId: plugin.params.pedalTone = normalized; break;
    case kBiasParamId: plugin.params.bias = normalized; break;
    case kStackParamId: plugin.params.stack = normalized; break;
    case kSagParamId: plugin.params.sag = normalized; break;
    case kFocusParamId: plugin.params.focus = normalized; break;
    case kConeParamId: plugin.params.cone = normalized; break;
    case kCabinetParamId: plugin.params.cabinet = normalized; break;
    case kMicParamId: plugin.params.mic = normalized; break;
    case kFeedbackParamId: plugin.params.feedback = normalized; break;
    case kProximityParamId: plugin.params.proximity = normalized; break;
    case kHarmonicParamId: plugin.params.harmonic = normalized; break;
    case kTrackingParamId: plugin.params.tracking = normalized; break;
    case kPolarityParamId: plugin.params.polarity = normalized; break;
    case kRootParamId: plugin.params.root = normalized; break;
    case kChaosParamId: plugin.params.chaos = normalized; break;
    case kOutputParamId: plugin.params.outputGainDb = normalized; break;
    case kArpPatternParamId:
        plugin.params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kScaleParamId:
        plugin.params.scale = static_cast<s3g::ProcessorStackScale>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kArpRateParamId:
        plugin.params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    case kArpOctavesParamId:
        plugin.params.arpOctaves = static_cast<uint32_t>(std::lround(value));
        break;
    case kArpGateParamId: plugin.params.arpGate = normalized; break;
    case kCustomLengthParamId:
        plugin.params.customPatternLength = static_cast<uint32_t>(
            std::lround(value));
        break;
    case kCustomStep1ParamId:
        plugin.params.customPattern[0u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep2ParamId:
        plugin.params.customPattern[1u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep3ParamId:
        plugin.params.customPattern[2u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep4ParamId:
        plugin.params.customPattern[3u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep5ParamId:
        plugin.params.customPattern[4u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep6ParamId:
        plugin.params.customPattern[5u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep7ParamId:
        plugin.params.customPattern[6u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kCustomStep8ParamId:
        plugin.params.customPattern[7u] = static_cast<int32_t>(
            std::lround(value));
        break;
    case kPierceParamId: plugin.params.pierce = normalized; break;
    case kSelfListenParamId: plugin.params.selfListen = normalized; break;
    default: return;
    }
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
    publishParam(plugin, id, rawParamValue(plugin, id));
    plugin.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (tailChanged) {
        plugin.tailChangePending.store(true, std::memory_order_release);
        if (plugin.host && plugin.host->request_process) {
            plugin.host->request_process(plugin.host);
        }
    }
}

void noteOn(Plugin& plugin, int key, float velocity)
{
    if (velocity <= 0.0f) {
        plugin.engine.noteOff(key);
        return;
    }
    plugin.engine.noteOn(key, velocity);
    plugin.active = true;
}

void allNotesOff(Plugin& plugin)
{
    plugin.engine.allNotesOff();
    plugin.engine.setPressure(0.0f);
}

void applyEvent(Plugin& plugin, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(plugin, param->param_id, param->value);
        return;
    }
    if ((event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF
            || event->type == CLAP_EVENT_NOTE_CHOKE)
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (!s3g::drum_midi::accepts(plugin.midiReceive, note->channel)) {
            return;
        }
        if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
            noteOn(plugin, note->key, static_cast<float>(note->velocity));
        } else if (note->key < 0) {
            allNotesOff(plugin);
        } else {
            plugin.engine.noteOff(note->key);
        }
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_EXPRESSION
        && event->size >= sizeof(clap_event_note_expression_t)) {
        const auto* expression = reinterpret_cast<
            const clap_event_note_expression_t*>(event);
        if (!s3g::drum_midi::accepts(
                plugin.midiReceive, expression->channel)) return;
        if (expression->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) {
            plugin.engine.setPressure(
                static_cast<float>(expression->value));
        } else if (expression->expression_id == CLAP_NOTE_EXPRESSION_TUNING) {
            plugin.engine.setPitchBendSemitones(
                static_cast<float>(expression->value));
        }
        return;
    }
    if (event->type != CLAP_EVENT_MIDI
        || event->size < sizeof(clap_event_midi_t)) return;
    const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
    const uint8_t command = midi->data[0] & 0xf0u;
    const int channel = midi->data[0] & 0x0fu;
    if (!s3g::drum_midi::accepts(plugin.midiReceive, channel)) return;
    const int key = midi->data[1] & 0x7fu;
    if (command == 0x90u && midi->data[2] != 0u) {
        noteOn(plugin, key,
            static_cast<float>(midi->data[2]) / 127.0f);
    } else if (command == 0x80u
        || (command == 0x90u && midi->data[2] == 0u)) {
        plugin.engine.noteOff(key);
    } else if (command == 0xd0u) {
        plugin.engine.setPressure(
            static_cast<float>(midi->data[1]) / 127.0f);
    } else if (command == 0xe0u) {
        const int bend = (static_cast<int>(midi->data[2]) << 7)
            | static_cast<int>(midi->data[1]);
        plugin.engine.setPitchBendSemitones(
            static_cast<float>(bend - 8192) * (2.0f / 8192.0f));
    } else if (command == 0xb0u
        && (midi->data[1] == 120u || midi->data[1] == 123u)) {
        allNotesOff(plugin);
    }
}

bool init(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->host && instance->host->get_extension) {
        instance->hostParams = static_cast<const clap_host_params_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_PARAMS));
        instance->hostTail = static_cast<const clap_host_tail_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_TAIL));
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
    if (const auto* def = paramDef(id)) {
        value = clampValue(*def, value);
        publishParam(plugin, id, value);
    }
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

std::array<double, kSynthParamCount> paramValues(
    const s3g::ProcessorStackParams& params)
{
    return {{
        static_cast<double>(params.mode), params.shape, params.wire,
        params.pick, params.damping, params.glideMs, params.crooked,
        params.spill, static_cast<double>(params.circuit), params.bite,
        params.pedalTone, params.bias, params.stack, params.sag,
        params.focus, params.cone, params.cabinet, params.mic,
        params.feedback, params.proximity, params.harmonic, params.tracking,
        params.polarity, params.root, params.chaos, params.outputGainDb,
        static_cast<double>(params.arpPattern),
        static_cast<double>(params.scale),
        static_cast<double>(params.arpRate),
        static_cast<double>(params.arpOctaves),
        params.arpGate,
        static_cast<double>(params.customPatternLength),
        static_cast<double>(params.customPattern[0u]),
        static_cast<double>(params.customPattern[1u]),
        static_cast<double>(params.customPattern[2u]),
        static_cast<double>(params.customPattern[3u]),
        static_cast<double>(params.customPattern[4u]),
        static_cast<double>(params.customPattern[5u]),
        static_cast<double>(params.customPattern[6u]),
        static_cast<double>(params.customPattern[7u]),
        params.pierce, params.selfListen,
    }};
}

bool queueGuiParams(Plugin& plugin,
    const s3g::ProcessorStackParams& params)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    const auto values = paramValues(params);
    std::array<s3g::clap_gui::ParamEvent, kSynthParamCount * 3u> events {};
    std::array<double, kSynthParamCount> clamped {};
    for (uint32_t index = 0u; index < kSynthParamCount; ++index) {
        const clap_id id = kSynthParamIds[index];
        clamped[index] = clampValue(*paramDef(id), values[index]);
        events[index * 3u] = { Kind::GestureBegin, id, 0.0 };
        events[index * 3u + 1u] = { Kind::Value, id, clamped[index] };
        events[index * 3u + 2u] = { Kind::GestureEnd, id, 0.0 };
    }
    if (!plugin.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t index = 0u; index < kSynthParamCount; ++index) {
        publishParam(plugin, kSynthParamIds[index], clamped[index]);
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

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(plugin, pending.paramId, pending.value);
        }
        plugin.guiParamEvents.pop();
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
    auto* instance = self(plugin);
    instance->sampleRate = std::clamp(sampleRate, 8000.0, 768000.0);
    instance->engine.prepare(instance->sampleRate);
    instance->engine.setParams(instance->params);
    instance->engine.setPressure(0.0f);
    instance->engine.setPitchBendSemitones(0.0f);
    instance->engine.setTempoBpm(120.0f);
    instance->active = false;
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->engine.reset();
    instance->engine.setParams(instance->params);
    instance->active = false;
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processInfo)
{
    auto* instance = self(plugin);
    if (!processInfo) return CLAP_PROCESS_ERROR;
    if (processInfo->transport
        && (processInfo->transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(processInfo->transport->tempo)
        && processInfo->transport->tempo > 0.0) {
        instance->engine.setTempoBpm(
            static_cast<float>(processInfo->transport->tempo));
    }
    if (instance->tailChangePending.exchange(
            false, std::memory_order_acq_rel)
        && instance->host && instance->hostTail
        && instance->hostTail->changed) {
        instance->hostTail->changed(instance->host);
    }
    serviceGuiParamEvents(*instance, processInfo->out_events);
    const clap_input_events_t* events = processInfo->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;

    if (processInfo->audio_outputs_count == 0u
        || !processInfo->audio_outputs) {
        while (eventIndex < eventCount) {
            applyEvent(*instance, events->get(events, eventIndex++));
        }
        instance->active = instance->engine.active();
        return instance->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
    }
    const auto& output = processInfo->audio_outputs[0u];
    if (output.channel_count == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }

    float blockPeak = 0.0f;
    for (uint32_t sample = 0u; sample < processInfo->frames_count; ++sample) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > sample) break;
            applyEvent(*instance, event);
            ++eventIndex;
        }
        float left = 0.0f;
        float right = 0.0f;
        instance->engine.processFrame(left, right);
        blockPeak = std::max(blockPeak,
            std::max(std::abs(left), std::abs(right)));
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
        applyEvent(*instance, events->get(events, eventIndex++));
    }
    instance->active = instance->engine.active();
    const float previous = instance->outputPeak.load(
        std::memory_order_relaxed);
    instance->outputPeak.store(instance->active
            ? std::max(blockPeak, previous * 0.84f) : 0.0f,
        std::memory_order_relaxed);
    return instance->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
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
    std::strncpy(info->name, "Processor Stack MIDI In",
        sizeof(info->name) - 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kParamDefs.size());
}

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
    if (id == kModeParamId) {
        const auto mode = static_cast<s3g::ProcessorStackMode>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackModeCount - 1u));
        std::snprintf(display, size, "%s", s3g::processorStackModeName(mode));
    } else if (id == kCircuitParamId) {
        const auto circuit = static_cast<s3g::ProcessorStackCircuit>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackCircuitCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackCircuitName(circuit));
    } else if (id == kArpPatternParamId) {
        const auto pattern = static_cast<s3g::ProcessorStackArpPattern>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackArpPatternCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackArpPatternName(pattern));
    } else if (id == kScaleParamId) {
        const auto scale = static_cast<s3g::ProcessorStackScale>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackScaleCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackScaleName(scale));
    } else if (id == kArpRateParamId) {
        const auto rate = static_cast<s3g::ProcessorStackArpRate>(
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)),
                s3g::kProcessorStackArpRateCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::processorStackArpRateName(rate));
    } else if (id == kMidiReceiveParamId) {
        s3g::drum_midi::valueToText(value, display, size);
    } else if (id == kGlideParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kArpOctavesParamId || id == kCustomLengthParamId) {
        std::snprintf(display, size, "%.0f", value);
    } else if (id >= kCustomStep1ParamId && id <= kCustomStep8ParamId) {
        std::snprintf(display, size, "%+.0f", value);
    } else if (id == kPolarityParamId) {
        std::snprintf(display, size, "%+.0f%%", (value - 0.5) * 200.0);
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
        for (uint32_t index = 0u; index < s3g::kProcessorStackModeCount;
             ++index) {
            const auto mode = static_cast<s3g::ProcessorStackMode>(index);
            if (std::strcmp(display, s3g::processorStackModeName(mode)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kCircuitParamId) {
        for (uint32_t index = 0u; index < s3g::kProcessorStackCircuitCount;
             ++index) {
            const auto circuit = static_cast<s3g::ProcessorStackCircuit>(index);
            if (std::strcmp(display,
                    s3g::processorStackCircuitName(circuit)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kArpPatternParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackArpPatternCount; ++index) {
            const auto pattern = static_cast<
                s3g::ProcessorStackArpPattern>(index);
            if (std::strcmp(display,
                    s3g::processorStackArpPatternName(pattern)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kScaleParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackScaleCount; ++index) {
            const auto scale = static_cast<s3g::ProcessorStackScale>(index);
            if (std::strcmp(display, s3g::processorStackScaleName(scale))
                == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kArpRateParamId) {
        for (uint32_t index = 0u;
             index < s3g::kProcessorStackArpRateCount; ++index) {
            const auto rate = static_cast<s3g::ProcessorStackArpRate>(index);
            if (std::strcmp(display, s3g::processorStackArpRateName(rate))
                == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    while (*end != '\0' && std::isspace(
            static_cast<unsigned char>(*end)) != 0) ++end;
    const char* suffix = end;
    size_t suffixLength = std::strlen(suffix);
    while (suffixLength > 0u && std::isspace(
            static_cast<unsigned char>(suffix[suffixLength - 1u])) != 0) {
        --suffixLength;
    }
    const auto suffixIs = [suffix, suffixLength](const char* expected) {
        return suffixLength == std::strlen(expected)
            && std::strncmp(suffix, expected, suffixLength) == 0;
    };
    double converted = parsed;
    if (suffixLength > 0u) {
        if (id == kGlideParamId && suffixIs("ms")) {
        } else if (id == kOutputParamId && suffixIs("dB")) {
        } else if (suffixIs("%") && id != kGlideParamId
            && id != kOutputParamId && id != kArpOctavesParamId
            && id != kCustomLengthParamId
            && !(id >= kCustomStep1ParamId
                && id <= kCustomStep8ParamId)) {
            converted = id == kPolarityParamId
                ? converted * 0.005 + 0.5 : converted * 0.01;
        } else {
            return false;
        }
    }
    *value = clampValue(*def, converted);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* instance = self(plugin);
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE
            || event->size < sizeof(clap_event_param_value_t)) continue;
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(*instance, param->param_id, param->value);
    }
    serviceGuiParamEvents(*instance, output);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    SavedState state;
    const auto* instance = self(plugin);
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        state.values[index] = paramValue(*instance, kParamDefs[index].id);
    }
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedStateHeader header;
    std::array<double, kParamDefs.size()> values {};
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))) {
        return false;
    }
    const bool current = header.magic == kStateMagic
        && header.version == kStateVersion
        && header.valueCount == kParamDefs.size();
    const bool versionTwo = header.magic == kStateMagic
        && header.version == kVersionTwoStateVersion
        && header.valueCount == kVersionTwoParamCount;
    const bool versionOne = header.magic == kStateMagic
        && header.version == kVersionOneStateVersion
        && header.valueCount == kVersionOneParamCount;
    if (!current && !versionTwo && !versionOne) return false;
    if (!s3g::clap_state::readAll(stream, values.data(),
            static_cast<size_t>(header.valueCount) * sizeof(double))) {
        return false;
    }
    auto* instance = self(plugin);
    for (uint32_t index = 0u; index < values.size(); ++index) {
        const double value = index < header.valueCount
            ? values[index] : kParamDefs[index].defaultValue;
        applyParam(*instance, kParamDefs[index].id, value);
    }
    instance->engine.reset();
    instance->engine.setParams(instance->params);
    instance->active = false;
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
    if (instance->host && instance->hostParams
        && instance->hostParams->rescan) {
        instance->hostParams->rescan(
            instance->host, CLAP_PARAM_RESCAN_VALUES);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* instance = self(plugin);
    const double seconds = 0.25
        + paramValue(*instance, kSpillParamId) * 15.0
        + paramValue(*instance, kFeedbackParamId) * 1.0;
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * instance->sampleRate)));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct StackUiRow {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

namespace layout = s3g::gui_layout;

constexpr CGFloat kLeftPanelX = 16.0;
constexpr CGFloat kRightPanelX = 506.0;
constexpr CGFloat kPanelWidth = 458.0;
constexpr layout::Canvas kStackCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr layout::Column kLeftColumn {
    kLeftPanelX, kPanelWidth, layout::kStandardMetrics.contentTop
};
constexpr layout::Column kRightColumn {
    kRightPanelX, kPanelWidth, layout::kStandardMetrics.contentTop
};

constexpr auto kPlayPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Engine,
    kLeftColumn, layout::kStandardMetrics.contentTop, 8u);
constexpr auto kArpPanel = layout::fittedStackPanel(
    layout::PanelRole::EventTiming, kPlayPanel, 14u);
constexpr auto kPedalPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kArpPanel, 4u);

constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kRightColumn, layout::kStandardMetrics.contentTop, 1u);
constexpr auto kAmplifierPanel = layout::fittedStackPanel(
    layout::PanelRole::Topology, kOutputPanel, 6u);
constexpr auto kLoopPanel = layout::fittedStackPanel(
    layout::PanelRole::Motion, kAmplifierPanel, 9u);
constexpr auto kRoutingPanel = layout::fittedStackPanel(
    layout::PanelRole::Utility, kLoopPanel, 1u);

constexpr std::array kLeftPanels {
    kPlayPanel, kArpPanel, kPedalPanel,
};
constexpr std::array kRightPanels {
    kOutputPanel, kAmplifierPanel, kLoopPanel, kRoutingPanel,
};

static_assert(layout::validateColumn(kLeftPanels, kStackCanvas, false));
static_assert(layout::validateColumn(kRightPanels, kStackCanvas, false));

constexpr std::array<StackUiRow, 43u> kUiRows {{
    { kModeParamId, "MODE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 0u) },
    { kShapeParamId, "SHAPE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 1u) },
    { kWireParamId, "STRING", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 2u) },
    { kPickParamId, "PICK", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 3u) },
    { kDampingParamId, "DAMP", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 4u) },
    { kGlideParamId, "GLIDE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 5u) },
    { kCrookedParamId, "CROOKED", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 6u) },
    { kSpillParamId, "SPILL", kLeftPanelX, kPanelWidth,
        layout::rowY(kPlayPanel, 7u) },

    { kArpPatternParamId, "PATTERN", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 0u) },
    { kScaleParamId, "SCALE RULE", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 1u) },
    { kArpRateParamId, "RATE", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 2u) },
    { kArpOctavesParamId, "OCTAVES", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 3u) },
    { kArpGateParamId, "GATE", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 4u) },
    { kCustomLengthParamId, "LENGTH", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 5u) },
    { kCustomStep1ParamId, "STEP 1", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 6u) },
    { kCustomStep2ParamId, "STEP 2", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 7u) },
    { kCustomStep3ParamId, "STEP 3", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 8u) },
    { kCustomStep4ParamId, "STEP 4", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 9u) },
    { kCustomStep5ParamId, "STEP 5", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 10u) },
    { kCustomStep6ParamId, "STEP 6", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 11u) },
    { kCustomStep7ParamId, "STEP 7", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 12u) },
    { kCustomStep8ParamId, "STEP 8", kLeftPanelX, kPanelWidth,
        layout::rowY(kArpPanel, 13u) },

    { kCircuitParamId, "CIRCUIT", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalPanel, 0u) },
    { kBiteParamId, "BITE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalPanel, 1u) },
    { kPedalToneParamId, "TONE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalPanel, 2u) },
    { kBiasParamId, "BIAS", kLeftPanelX, kPanelWidth,
        layout::rowY(kPedalPanel, 3u) },

    { kMidiReceiveParamId, "MIDI RECEIVE", kRightPanelX, kPanelWidth,
        layout::rowY(kRoutingPanel, 0u) },

    { kOutputParamId, "OUT", kRightPanelX, kPanelWidth,
        layout::rowY(kOutputPanel, 0u) },

    { kStackParamId, "STACK", kRightPanelX, kPanelWidth,
        layout::rowY(kAmplifierPanel, 0u) },
    { kSagParamId, "SAG", kRightPanelX, kPanelWidth,
        layout::rowY(kAmplifierPanel, 1u) },
    { kFocusParamId, "FOCUS", kRightPanelX, kPanelWidth,
        layout::rowY(kAmplifierPanel, 2u) },
    { kConeParamId, "CONE", kRightPanelX, kPanelWidth,
        layout::rowY(kAmplifierPanel, 3u) },
    { kCabinetParamId, "CAB", kRightPanelX, kPanelWidth,
        layout::rowY(kAmplifierPanel, 4u) },
    { kMicParamId, "MIC", kRightPanelX, kPanelWidth,
        layout::rowY(kAmplifierPanel, 5u) },

    { kFeedbackParamId, "FEEDBACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 0u) },
    { kProximityParamId, "PROXIMITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 1u) },
    { kHarmonicParamId, "HARMONIC", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 2u) },
    { kTrackingParamId, "TRACK", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 3u) },
    { kPolarityParamId, "POLARITY", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 4u) },
    { kRootParamId, "ROOT", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 5u) },
    { kChaosParamId, "CHAOS", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 6u) },
    { kPierceParamId, "PIERCE", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 7u) },
    { kSelfListenParamId, "SELF LISTEN", kRightPanelX, kPanelWidth,
        layout::rowY(kLoopPanel, 8u) },
}};

bool isUiMenuParam(clap_id id)
{
    return id == kModeParamId || id == kCircuitParamId
        || id == kMidiReceiveParamId || id == kArpPatternParamId
        || id == kScaleParamId || id == kArpRateParamId
        || id == kArpOctavesParamId || id == kCustomLengthParamId;
}

uint32_t uiMenuItemCount(clap_id id)
{
    if (id == kModeParamId) return s3g::kProcessorStackModeCount;
    if (id == kCircuitParamId) return s3g::kProcessorStackCircuitCount;
    if (id == kMidiReceiveParamId) return 17u;
    if (id == kArpPatternParamId) return s3g::kProcessorStackArpPatternCount;
    if (id == kScaleParamId) return s3g::kProcessorStackScaleCount;
    if (id == kArpRateParamId) return s3g::kProcessorStackArpRateCount;
    if (id == kArpOctavesParamId) return 4u;
    if (id == kCustomLengthParamId) return 8u;
    return 0u;
}

bool isLogUiParam(clap_id id)
{
    return id == kGlideParamId;
}

double uiNormalizedValue(clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return 0.0;
    if (isLogUiParam(id)) {
        return std::clamp(std::log1p(value)
            / std::log1p(def->maximum), 0.0, 1.0);
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
        ? std::expm1(std::log1p(def->maximum) * normalized)
        : def->minimum + (def->maximum - def->minimum) * normalized;
    return clampValue(*def, value);
}

s3g::ProcessorStackParams publishedParamsSnapshot(const Plugin& plugin)
{
    s3g::ProcessorStackParams params;
    params.mode = static_cast<s3g::ProcessorStackMode>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kModeParamId))));
    params.shape = static_cast<float>(paramValue(plugin, kShapeParamId));
    params.wire = static_cast<float>(paramValue(plugin, kWireParamId));
    params.pick = static_cast<float>(paramValue(plugin, kPickParamId));
    params.damping = static_cast<float>(paramValue(plugin, kDampingParamId));
    params.glideMs = static_cast<float>(paramValue(plugin, kGlideParamId));
    params.crooked = static_cast<float>(paramValue(plugin, kCrookedParamId));
    params.spill = static_cast<float>(paramValue(plugin, kSpillParamId));
    params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpPatternParamId))));
    params.scale = static_cast<s3g::ProcessorStackScale>(
        static_cast<uint32_t>(std::lround(paramValue(plugin, kScaleParamId))));
    params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kArpRateParamId))));
    params.arpOctaves = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kArpOctavesParamId)));
    params.arpGate = static_cast<float>(paramValue(plugin, kArpGateParamId));
    params.customPatternLength = static_cast<uint32_t>(std::lround(
        paramValue(plugin, kCustomLengthParamId)));
    for (uint32_t index = 0u; index < params.customPattern.size(); ++index) {
        params.customPattern[index] = static_cast<int32_t>(std::lround(
            paramValue(plugin, kCustomStep1ParamId + index)));
    }
    params.pierce = static_cast<float>(paramValue(plugin, kPierceParamId));
    params.selfListen = static_cast<float>(
        paramValue(plugin, kSelfListenParamId));
    params.circuit = static_cast<s3g::ProcessorStackCircuit>(
        static_cast<uint32_t>(std::lround(
            paramValue(plugin, kCircuitParamId))));
    params.bite = static_cast<float>(paramValue(plugin, kBiteParamId));
    params.pedalTone = static_cast<float>(
        paramValue(plugin, kPedalToneParamId));
    params.bias = static_cast<float>(paramValue(plugin, kBiasParamId));
    params.stack = static_cast<float>(paramValue(plugin, kStackParamId));
    params.sag = static_cast<float>(paramValue(plugin, kSagParamId));
    params.focus = static_cast<float>(paramValue(plugin, kFocusParamId));
    params.cone = static_cast<float>(paramValue(plugin, kConeParamId));
    params.cabinet = static_cast<float>(paramValue(plugin, kCabinetParamId));
    params.mic = static_cast<float>(paramValue(plugin, kMicParamId));
    params.feedback = static_cast<float>(
        paramValue(plugin, kFeedbackParamId));
    params.proximity = static_cast<float>(
        paramValue(plugin, kProximityParamId));
    params.harmonic = static_cast<float>(
        paramValue(plugin, kHarmonicParamId));
    params.tracking = static_cast<float>(paramValue(plugin, kTrackingParamId));
    params.polarity = static_cast<float>(paramValue(plugin, kPolarityParamId));
    params.root = static_cast<float>(paramValue(plugin, kRootParamId));
    params.chaos = static_cast<float>(paramValue(plugin, kChaosParamId));
    params.outputGainDb = static_cast<float>(
        paramValue(plugin, kOutputParamId));
    return s3g::sanitizeProcessorStackParams(params);
}

float randomUnit(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

s3g::ProcessorStackParams safeRandomParams(
    const Plugin& plugin, uint32_t seed)
{
    auto params = publishedParamsSnapshot(plugin);
    params.mode = static_cast<s3g::ProcessorStackMode>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed) * 3.0f), 2u));
    params.shape = randomUnit(seed);
    params.wire = 0.34f + randomUnit(seed) * 0.64f;
    params.pick = 0.34f + randomUnit(seed) * 0.66f;
    params.damping = 0.18f + randomUnit(seed) * 0.68f;
    params.glideMs = randomUnit(seed) * randomUnit(seed) * 280.0f;
    params.crooked = randomUnit(seed) * 0.94f;
    params.spill = randomUnit(seed) * 0.78f;
    params.arpPattern = static_cast<s3g::ProcessorStackArpPattern>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpPatternCount)),
            s3g::kProcessorStackArpPatternCount - 1u));
    params.scale = static_cast<s3g::ProcessorStackScale>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackScaleCount)),
            s3g::kProcessorStackScaleCount - 1u));
    params.arpRate = static_cast<s3g::ProcessorStackArpRate>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackArpRateCount)),
            s3g::kProcessorStackArpRateCount - 1u));
    params.arpOctaves = 1u + std::min<uint32_t>(3u,
        static_cast<uint32_t>(randomUnit(seed) * 4.0f));
    params.arpGate = 0.22f + randomUnit(seed) * 0.68f;
    params.customPatternLength = 3u + std::min<uint32_t>(5u,
        static_cast<uint32_t>(randomUnit(seed) * 6.0f));
    for (auto& step : params.customPattern) {
        step = -4 + static_cast<int32_t>(randomUnit(seed) * 16.0f);
    }
    params.pierce = 0.34f + randomUnit(seed) * 0.66f;
    params.selfListen = 0.42f + randomUnit(seed) * 0.58f;
    params.circuit = static_cast<s3g::ProcessorStackCircuit>(
        std::min<uint32_t>(static_cast<uint32_t>(randomUnit(seed)
            * static_cast<float>(s3g::kProcessorStackCircuitCount)),
            s3g::kProcessorStackCircuitCount - 1u));
    params.bite = 0.22f + randomUnit(seed) * 0.70f;
    params.pedalTone = 0.20f + randomUnit(seed) * 0.68f;
    params.bias = 0.16f + randomUnit(seed) * 0.72f;
    params.stack = 0.34f + randomUnit(seed) * 0.58f;
    params.sag = randomUnit(seed) * 0.88f;
    params.focus = 0.16f + randomUnit(seed) * 0.76f;
    params.cone = 0.28f + randomUnit(seed) * 0.68f;
    params.cabinet = randomUnit(seed);
    params.mic = randomUnit(seed) * 0.88f;
    params.feedback = 0.18f + randomUnit(seed) * 0.70f;
    params.proximity = 0.18f + randomUnit(seed) * 0.72f;
    params.harmonic = randomUnit(seed);
    params.tracking = 0.18f + randomUnit(seed) * 0.78f;
    params.polarity = 0.12f + randomUnit(seed) * 0.76f;
    params.root = randomUnit(seed) * 0.58f;
    params.chaos = randomUnit(seed) * 0.84f;
    params.outputGainDb = -18.0f + randomUnit(seed) * 7.0f;
    return s3g::sanitizeProcessorStackParams(params);
}

} // namespace

@interface S3GProcessorStackView : NSView {
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
- (void)markCustomPreset;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
@end

@implementation S3GProcessorStackView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (!self) return nil;
    _plugin = plugin;
    _dragParam = -1;
    _openMenu = CLAP_INVALID_ID;
    _hoverMenuItem = -1;
    _menuItemCount = 0u;
    _timer = nil;
    auto* instance = static_cast<Plugin*>(_plugin);
    _observedParamRevision = instance
        ? instance->parameterRevision.load(std::memory_order_acquire) : 0u;
    _factoryPresetIndex = instance
        ? s3g::processorStackFactoryPresetIndex(
            publishedParamsSnapshot(*instance)) : 0;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::processorStackFactoryPresetInfo(
                static_cast<uint32_t>(_factoryPresetIndex)).name
            : "CUSTOM");
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

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
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kProcessorStackFactoryPresetCount - 1u));
    if (!queueGuiParams(*instance, s3g::processorStackFactoryPreset(
            static_cast<uint32_t>(index)))) {
        NSBeep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::processorStackFactoryPresetInfo(
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
    NSRect anchor = NSZeroRect;
    if (_openMenu == kFactoryPresetMenuId) {
        const auto band = s3g::clap_gui::encoderTitleBand(
            kGuiWidth, kGuiHeight);
        anchor = s3g::clap_gui::cocoaRect(band.presetMenu);
    } else {
        const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
            [=](const StackUiRow& candidate) {
                return candidate.id == _openMenu;
            });
        if (row == kUiRows.end() || !isUiMenuParam(row->id)) {
            return NSZeroRect;
        }
        anchor = NSMakeRect(
            s3g::gui_layout::processorControlX(row->panelX), row->y - 1.0,
            s3g::gui_layout::processorMenuWidth(row->panelWidth), 15.0);
    }
    const CGFloat menuHeight = 18.0 * _menuItemCount;
    CGFloat menuY = NSMaxY(anchor) + 2.0;
    if (menuY + menuHeight > kGuiHeight) {
        menuY = anchor.origin.y - 2.0 - menuHeight;
    }
    return NSMakeRect(anchor.origin.x, menuY,
        anchor.size.width, menuHeight);
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    NSString* items[32] {};
    const uint32_t count = std::min<uint32_t>(_menuItemCount, 32u);
    if (_openMenu == kFactoryPresetMenuId) {
        for (uint32_t index = 0u; index < count; ++index) {
            items[index] = [NSString stringWithUTF8String:
                s3g::processorStackFactoryPresetInfo(index).name];
        }
    } else {
        for (uint32_t index = 0u; index < count; ++index) {
            char text[64] {};
            const auto* def = paramDef(_openMenu);
            const double menuValue = def
                ? def->minimum + static_cast<double>(index)
                : static_cast<double>(index);
            if (paramsValueToText(
                    &static_cast<Plugin*>(_plugin)->plugin, _openMenu,
                    menuValue, text, sizeof(text))) {
                items[index] = [NSString stringWithUTF8String:text];
            } else {
                items[index] = @"—";
            }
        }
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(paramValue(
            *static_cast<Plugin*>(_plugin), _openMenu)
                - paramDef(_openMenu)->minimum));
    s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
        items, count, selected, _hoverMenuItem, attrs, style);
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    auto* instance = static_cast<Plugin*>(_plugin);
    if (instance) {
        const uint64_t revision = instance->parameterRevision.load(
            std::memory_order_acquire);
        if (revision != _observedParamRevision) {
            _observedParamRevision = revision;
            _factoryPresetIndex = s3g::processorStackFactoryPresetIndex(
                publishedParamsSnapshot(*instance));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::processorStackFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex)).name
                    : "CUSTOM");
        }
    }
    if (![self isHidden] && instance
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g PROCESSOR STACK",
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(
            instance->outputPeak.load(std::memory_order_relaxed)),
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const auto drawPanel = [&](NSString* name, const layout::Panel& panel) {
        const NSRect rect = s3g::clap_gui::cocoaRect(panel.frame);
        s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        s3g::clap_gui::drawPanelHeader(name, true, rect.origin.x,
            rect.origin.y, rect.size.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labelAttrs, style);
    };
    drawPanel(@"PLAY", kPlayPanel);
    drawPanel(@"ARPEGGIATOR", kArpPanel);
    drawPanel(@"PEDAL", kPedalPanel);
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"AMPLIFIER / SPEAKER", kAmplifierPanel);
    drawPanel(@"MIC FEEDBACK LOOP", kLoopPanel);
    drawPanel(@"ROUTING", kRoutingPanel);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*instance, row.id);
        char text[64] {};
        paramsValueToText(&instance->plugin,
            row.id, value, text, sizeof(text));
        NSString* label = [NSString stringWithUTF8String:row.label];
        NSString* display = [NSString stringWithUTF8String:text];
        if (isUiMenuParam(row.id)) {
            s3g::clap_gui::drawProcessorMenu(label, display, row.y,
                row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        } else {
            s3g::clap_gui::drawProcessorSlider(label, display,
                static_cast<CGFloat>(uiNormalizedValue(row.id, value)),
                row.y, row.panelX, row.panelWidth,
                labelAttrs, valueAttrs, style);
        }
    }
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const clap_id id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const StackUiRow& candidate) { return candidate.id == id; });
    if (row == kUiRows.end()) return;
    const double controlX = s3g::gui_layout::processorControlX(row->panelX);
    const double trackWidth =
        s3g::gui_layout::processorTrackWidth(row->panelWidth);
    const double normalized = std::clamp(
        (point.x - controlX) / trackWidth, 0.0, 1.0);
    auto* instance = static_cast<Plugin*>(_plugin);
    queueGuiParamValue(*instance, id,
        uiValueFromNormalized(id, normalized));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    auto* instance = static_cast<Plugin*>(_plugin);
    if (!instance) return;
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
                const auto* def = paramDef(menuParam);
                queueGuiParamGesture(*instance, menuParam,
                    (def ? def->minimum : 0.0) + static_cast<double>(hit));
                if (menuParam != kMidiReceiveParamId) {
                    [self markCustomPreset];
                }
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
        _menuItemCount = s3g::kProcessorStackFactoryPresetCount;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &instance->plugin, @"Processor Stack", kOutputParamId,
                &name)) {
            [self markCustomPreset];
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
                &instance->plugin, @"Processor Stack", &name)) {
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
        if (queueGuiParams(*instance,
                safeRandomParams(*instance, arc4random()))) {
            [self markCustomPreset];
        } else {
            NSBeep();
        }
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
        if (isUiMenuParam(row.id)) {
            _openMenu = row.id;
            _hoverMenuItem = -1;
            _menuItemCount = uiMenuItemCount(row.id);
            [self setNeedsDisplay:YES];
            return;
        }
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &instance->plugin, row.id, &defaultValue)) {
            queueGuiParamGesture(*instance, row.id, defaultValue);
            _dragParam = -1;
        } else {
            _dragParam = static_cast<int>(row.id);
            queueGuiParamGestureBegin(*instance, row.id);
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
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
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
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GProcessorStackView alloc] initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(instance->guiView) release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance || !instance->guiView) return;
    instance->guiVisible = false;
    [static_cast<S3GProcessorStackView*>(instance->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        instance->guiViewport, instance->guiView);
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
    if (!instance->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    instance->guiVisible = true;
    [static_cast<S3GProcessorStackView*>(instance->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GProcessorStackView*>(instance->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
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
    CLAP_PLUGIN_FEATURE_STEREO,
    CLAP_PLUGIN_FEATURE_DISTORTION,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.processor-stack",
    "s3g Processor Stack",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.3.0",
    "A plucked-string, programmable scale-arpeggio, shared amplifier, nonlinear speaker, and self-listening microphone-feedback instrument for power chords and crooked leads.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    for (const auto& def : kParamDefs) {
        applyParam(*instance, def.id, def.defaultValue);
    }
    for (const auto& def : kParamDefs) {
        publishParam(*instance, def.id, rawParamValue(*instance, def.id));
    }
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
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
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
