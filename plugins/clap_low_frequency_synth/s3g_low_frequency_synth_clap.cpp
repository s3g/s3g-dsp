#include "s3g_low_frequency_synth.h"
#include "s3g_low_frequency_synth_presets.h"
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

constexpr uint32_t kStateMagic = 0x32464c53u; // "SLF2" little endian.
constexpr uint32_t kStateVersion = 7u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 980u;
constexpr uint32_t kGuiHeight = 672u;

enum ParamId : clap_id {
    kTransposeParamId = 1u,
    kFundamentalParamId = 3u,
    kBodyParamId = 4u,
    kLoadingParamId = 5u,
    kCouplingParamId = 6u,
    kExcitationParamId = 8u,
    kDampingParamId = 9u,
    kAttackParamId = 11u,
    kDecayParamId = 12u,
    kSustainParamId = 13u,
    kReleaseParamId = 14u,
    kGlideParamId = 15u,
    kPitchTransientParamId = 16u,
    kOutputParamId = 21u,
    kMidiReceiveParamId = 22u,
    kHarmonicsParamId = 23u,
    kFilterCutoffParamId = 24u,
    kFilterResonanceParamId = 25u,
    kDriveParamId = 28u,
    kProcessedMixParamId = 31u,
    kValvePreampParamId = 32u,
    kFilterMotionRateParamId = 46u,
    kFilterMotionDepthParamId = 47u,
    kFilterMotionClockParamId = 50u,
    kFilterMotionDivisionParamId = 51u,
    kShredParamId = 52u,
    kShredFeedbackParamId = 53u,
    kShredMixParamId = 54u,
    kShredCircuitParamId = 55u,
    kShredColorParamId = 56u,
    kAmplitudeMotionPositionParamId = 58u,
    kShredFeedbackToneLevelParamId = 59u,
};

constexpr uint32_t kLegacyParamCount = 25u;
constexpr uint32_t kVersionFourParamCount = 28u;
constexpr uint32_t kVersionFiveParamCount = 30u;
constexpr uint32_t kVersionSixParamCount = 31u;
constexpr uint32_t kParamCount = 32u;
constexpr uint32_t kSynthParamCount = 31u;
constexpr uint32_t kPublishedParamCount =
    static_cast<uint32_t>(kShredFeedbackToneLevelParamId) + 1u;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
    bool hidden = false;
};

constexpr std::array<ParamDef, kParamCount> kParamDefs {{
    { kOutputParamId, "Output", "Output", -36.0, 6.0, -8.0, false },
    { kFundamentalParamId, "Sub", "Modal Core", 0.0, 1.0, 0.96, false },
    { kBodyParamId, "Modal Body", "Modal Core", 0.0, 1.0, 0.70, false },
    { kLoadingParamId, "Mass Load", "Modal Core", 0.0, 1.0, 0.82, false },
    { kCouplingParamId, "Coupling", "Modal Core", 0.0, 1.0, 0.50, false },
    { kDampingParamId, "Damping", "Modal Core", 0.0, 1.0, 0.28, false },
    { kExcitationParamId, "Excitation", "Modal Core", 0.0, 1.0, 0.25, false },
    { kHarmonicsParamId, "Upper Modes", "Tone", 0.0, 1.0, 0.18, false },
    { kFilterCutoffParamId, "Cutoff", "Tone", 30.0, 12000.0, 1200.0, false },
    { kFilterResonanceParamId, "Resonance", "Tone", 0.0, 1.0, 0.18, false },
    { kDriveParamId, "Membrane Drive", "Tone", 0.0, 1.0, 0.28, false },
    { kProcessedMixParamId, "Filter Mix", "Tone", 0.0, 1.0, 0.36, false },
    { kValvePreampParamId, "Tube", "Tone", 0.0, 1.0, 0.36, false },
    { kAttackParamId, "Attack", "Envelope", 0.0005, 2.0, 0.008, false },
    { kDecayParamId, "Decay", "Envelope", 0.005, 5.0, 0.22, false },
    { kSustainParamId, "Sustain", "Envelope", 0.0, 1.0, 0.86, false },
    { kReleaseParamId, "Release", "Envelope", 0.005, 8.0, 0.35, false },
    { kTransposeParamId, "Transpose", "Pitch", -36.0, 24.0, 0.0, false },
    { kGlideParamId, "Glide", "Pitch", 0.0, 2000.0, 35.0, false },
    { kPitchTransientParamId, "Pitch Transient", "Pitch", -12.0, 36.0, 0.0, false },
    { kFilterMotionClockParamId, "Clock", "Amplitude LFO", 0.0, 1.0, 1.0, true },
    { kFilterMotionDivisionParamId, "Division", "Amplitude LFO", 0.0, 15.0, 7.0, true },
    { kFilterMotionRateParamId, "Free Rate", "Amplitude LFO", 0.05, 20.0, 2.0, false },
    { kFilterMotionDepthParamId, "Amount", "Amplitude LFO", 0.0, 1.0, 0.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "Routing", 0.0, 16.0, 0.0, true },
    { kShredParamId, "Shred", "Stereo Shred", 0.0, 1.0, 0.0, false },
    { kShredFeedbackParamId, "Feedback", "Stereo Shred", 0.0, 1.0, 0.0, false },
    { kShredMixParamId, "Shred Mix", "Stereo Shred", 0.0, 1.0, 0.0, false },
    { kShredCircuitParamId, "Circuit", "Stereo Shred", 0.0, 7.0, 0.0, true },
    { kShredColorParamId, "Color", "Stereo Shred", 0.0, 1.0, 0.55, false },
    { kAmplitudeMotionPositionParamId, "Position", "Amplitude LFO", 0.0, 1.0, 1.0, true },
    { kShredFeedbackToneLevelParamId, "Feedback Tone Level", "Stereo Shred", 0.0, 1.0, 1.0, false },
}};

constexpr std::array<clap_id, kSynthParamCount> kSynthParamIds {{
    kOutputParamId, kFundamentalParamId, kBodyParamId, kLoadingParamId,
    kCouplingParamId, kDampingParamId, kExcitationParamId,
    kHarmonicsParamId, kFilterCutoffParamId, kFilterResonanceParamId,
    kDriveParamId, kProcessedMixParamId, kValvePreampParamId,
    kAttackParamId, kDecayParamId, kSustainParamId, kReleaseParamId,
    kTransposeParamId, kGlideParamId, kPitchTransientParamId,
    kFilterMotionClockParamId, kFilterMotionDivisionParamId,
    kFilterMotionRateParamId, kFilterMotionDepthParamId,
    kShredParamId, kShredFeedbackParamId, kShredMixParamId,
    kShredCircuitParamId, kShredColorParamId,
    kAmplitudeMotionPositionParamId,
    kShredFeedbackToneLevelParamId,
}};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t valueCount = kParamCount;
    uint32_t reserved = 0u;
};

struct SavedState {
    SavedStateHeader header {};
    std::array<double, kParamCount> values {};
};

struct VoiceSlot {
    s3g::LowFrequencySynth synth {};
    int key = -1;
    bool held = false;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    s3g::LowFrequencySynthParams params {};
    std::array<VoiceSlot, 1u> voices {};
    double transportFallbackBeat = 0.0;
    double midiReceive = 0.0;
    std::array<std::atomic<double>, kPublishedParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::array<bool, 128u> heldNotes {};
    std::array<float, 128u> heldVelocities {};
    std::array<int16_t, 128u> noteStack {};
    uint32_t noteStackSize = 0u;
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

void publishParam(Plugin& p, clap_id id, double value)
{
    if (!paramDef(id)) return;
    p.publishedParams[id].store(value, std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    if (!paramDef(id)) return 0.0;
    return p.publishedParams[id].load(std::memory_order_acquire);
}

double rawParamValue(const Plugin& p, clap_id id)
{
    switch (id) {
    case kTransposeParamId: return p.params.transposeSemitones;
    case kFundamentalParamId: return p.params.fundamental;
    case kBodyParamId: return p.params.body;
    case kLoadingParamId: return p.params.loading;
    case kCouplingParamId: return p.params.coupling;
    case kExcitationParamId: return p.params.excitationPosition;
    case kDampingParamId: return p.params.damping;
    case kAttackParamId: return p.params.attackSeconds;
    case kDecayParamId: return p.params.decaySeconds;
    case kSustainParamId: return p.params.sustain;
    case kReleaseParamId: return p.params.releaseSeconds;
    case kGlideParamId: return p.params.glideMs;
    case kPitchTransientParamId: return p.params.pitchTransientSemitones;
    case kOutputParamId: return p.params.outputGainDb;
    case kMidiReceiveParamId: return p.midiReceive;
    case kHarmonicsParamId: return p.params.upperModeLevel;
    case kFilterCutoffParamId: return p.params.filterCutoffHz;
    case kFilterResonanceParamId: return p.params.filterResonance;
    case kDriveParamId: return p.params.membraneDrive;
    case kProcessedMixParamId: return p.params.processedMix;
    case kValvePreampParamId: return p.params.valvePreamp;
    case kFilterMotionRateParamId: return p.params.amplitudeMotionRateHz;
    case kFilterMotionDepthParamId: return p.params.amplitudeMotionDepth;
    case kFilterMotionClockParamId: return p.params.amplitudeMotionClock;
    case kFilterMotionDivisionParamId: return p.params.amplitudeMotionDivision;
    case kShredParamId: return p.params.shred;
    case kShredFeedbackParamId: return p.params.shredFeedback;
    case kShredFeedbackToneLevelParamId:
        return p.params.shredFeedbackToneLevel;
    case kShredMixParamId: return p.params.shredMix;
    case kShredCircuitParamId: return p.params.shredCircuit;
    case kShredColorParamId: return p.params.shredColor;
    case kAmplitudeMotionPositionParamId:
        return p.params.amplitudeMotionPosition;
    default: return 0.0;
    }
}

void notifyTailChanged(Plugin& p)
{
    if (p.host && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

void applyParam(Plugin& p, clap_id id, double value)
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

    const float v = static_cast<float>(value);
    const bool tailChanged = (id == kReleaseParamId
            && std::fabs(p.params.releaseSeconds - v) > 1.0e-6f)
        || (id == kShredFeedbackParamId
            && std::fabs(p.params.shredFeedback - v) > 1.0e-6f)
        || (id == kShredFeedbackToneLevelParamId
            && std::fabs(p.params.shredFeedbackToneLevel - v) > 1.0e-6f)
        || (id == kShredMixParamId
            && std::fabs(p.params.shredMix - v) > 1.0e-6f);
    switch (id) {
    case kTransposeParamId: p.params.transposeSemitones = v; break;
    case kFundamentalParamId: p.params.fundamental = v; break;
    case kBodyParamId: p.params.body = v; break;
    case kLoadingParamId: p.params.loading = v; break;
    case kCouplingParamId: p.params.coupling = v; break;
    case kExcitationParamId: p.params.excitationPosition = v; break;
    case kDampingParamId: p.params.damping = v; break;
    case kAttackParamId: p.params.attackSeconds = v; break;
    case kDecayParamId: p.params.decaySeconds = v; break;
    case kSustainParamId: p.params.sustain = v; break;
    case kReleaseParamId: p.params.releaseSeconds = v; break;
    case kGlideParamId: p.params.glideMs = v; break;
    case kPitchTransientParamId: p.params.pitchTransientSemitones = v; break;
    case kOutputParamId: p.params.outputGainDb = v; break;
    case kHarmonicsParamId: p.params.upperModeLevel = v; break;
    case kFilterCutoffParamId: p.params.filterCutoffHz = v; break;
    case kFilterResonanceParamId: p.params.filterResonance = v; break;
    case kDriveParamId: p.params.membraneDrive = v; break;
    case kProcessedMixParamId: p.params.processedMix = v; break;
    case kValvePreampParamId: p.params.valvePreamp = v; break;
    case kFilterMotionRateParamId: p.params.amplitudeMotionRateHz = v; break;
    case kFilterMotionDepthParamId: p.params.amplitudeMotionDepth = v; break;
    case kFilterMotionClockParamId: p.params.amplitudeMotionClock = v; break;
    case kFilterMotionDivisionParamId: p.params.amplitudeMotionDivision = v; break;
    case kShredParamId: p.params.shred = v; break;
    case kShredFeedbackParamId: p.params.shredFeedback = v; break;
    case kShredFeedbackToneLevelParamId:
        p.params.shredFeedbackToneLevel = v;
        break;
    case kShredMixParamId: p.params.shredMix = v; break;
    case kShredCircuitParamId: p.params.shredCircuit = v; break;
    case kShredColorParamId: p.params.shredColor = v; break;
    case kAmplitudeMotionPositionParamId:
        p.params.amplitudeMotionPosition = v;
        break;
    default: return;
    }
    for (auto& voice : p.voices) voice.synth.setParams(p.params);
    p.params = p.voices[0u].synth.params();
    publishParam(p, id, rawParamValue(p, id));
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (tailChanged) {
        p.tailChangePending.store(true, std::memory_order_release);
        if (p.host && p.host->request_process) p.host->request_process(p.host);
    }
}

void clearHeldNotes(Plugin& p)
{
    p.heldNotes.fill(false);
    p.heldVelocities.fill(0.0f);
    p.noteStackSize = 0u;
    for (auto& voice : p.voices) {
        voice.held = false;
        voice.key = -1;
        voice.synth.setPressure(0.0f);
    }
}

void removeFromNoteStack(Plugin& p, int key)
{
    uint32_t write = 0u;
    for (uint32_t read = 0u; read < p.noteStackSize; ++read) {
        if (p.noteStack[read] != key) {
            p.noteStack[write++] = p.noteStack[read];
        }
    }
    p.noteStackSize = write;
}

void reconcileVoice(Plugin& p, int retriggerKey = -1)
{
    auto& voice = p.voices[0u];
    if (p.noteStackSize == 0u) {
        if (voice.held) {
            voice.synth.noteOff();
            voice.synth.setPressure(0.0f);
            voice.held = false;
        }
        return;
    }

    const int desired = p.noteStack[p.noteStackSize - 1u];
    if (voice.held && voice.key == desired) {
        if (desired == retriggerKey) {
            voice.synth.noteOn(desired,
                p.heldVelocities[static_cast<uint32_t>(desired)], false);
        }
        return;
    }
    const bool legato = voice.held && voice.synth.active();
    voice.synth.noteOn(desired,
        p.heldVelocities[static_cast<uint32_t>(desired)], legato);
    voice.key = desired;
    voice.held = true;
}

void noteOn(Plugin& p, int key, float velocity)
{
    key = std::clamp(key, 0, 127);
    velocity = std::clamp(std::isfinite(velocity) ? velocity : 1.0f,
        0.0f, 1.0f);
    if (velocity <= 0.0f) return;
    removeFromNoteStack(p, key);
    if (p.noteStackSize < p.noteStack.size()) {
        p.noteStack[p.noteStackSize++] = static_cast<int16_t>(key);
    }
    p.heldNotes[static_cast<uint32_t>(key)] = true;
    p.heldVelocities[static_cast<uint32_t>(key)] = velocity;
    reconcileVoice(p, key);
    p.active = true;
}

void noteOff(Plugin& p, int key)
{
    key = std::clamp(key, 0, 127);
    p.heldNotes[static_cast<uint32_t>(key)] = false;
    removeFromNoteStack(p, key);
    reconcileVoice(p);
}

void allNotesOff(Plugin& p)
{
    for (auto& voice : p.voices) {
        voice.synth.noteOff();
        voice.synth.setPressure(0.0f);
        voice.held = false;
    }
    clearHeldNotes(p);
}

void setPressureForKey(Plugin& p, int key, float pressure)
{
    pressure = std::clamp(std::isfinite(pressure) ? pressure : 0.0f,
        0.0f, 1.0f);
    for (auto& voice : p.voices) {
        if (voice.held && (key < 0 || voice.key == key)) {
            voice.synth.setPressure(pressure);
        }
    }
}

void applyEvent(Plugin& p, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(p, param->param_id, param->value);
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_ON
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (s3g::drum_midi::accepts(p.midiReceive, note->channel)) {
            if (note->velocity > 0.0) {
                noteOn(p, note->key, static_cast<float>(note->velocity));
            } else {
                noteOff(p, note->key);
            }
        }
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_OFF
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (s3g::drum_midi::accepts(p.midiReceive, note->channel)) {
            noteOff(p, note->key);
        }
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_CHOKE
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (s3g::drum_midi::accepts(p.midiReceive, note->channel)) {
            if (note->key < 0) allNotesOff(p);
            else noteOff(p, note->key);
        }
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_EXPRESSION
        && event->size >= sizeof(clap_event_note_expression_t)) {
        const auto* expression = reinterpret_cast<
            const clap_event_note_expression_t*>(event);
        if (expression->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE
            && s3g::drum_midi::accepts(
                p.midiReceive, expression->channel)) {
            setPressureForKey(p, expression->key,
                static_cast<float>(expression->value));
        }
        return;
    }
    if (event->type != CLAP_EVENT_MIDI
        || event->size < sizeof(clap_event_midi_t)) return;
    const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
    const uint8_t status = midi->data[0] & 0xf0u;
    const int channel = midi->data[0] & 0x0fu;
    if (!s3g::drum_midi::accepts(p.midiReceive, channel)) return;
    const int key = midi->data[1] & 0x7fu;
    if (status == 0x90u && midi->data[2] != 0u) {
        noteOn(p, key, static_cast<float>(midi->data[2]) / 127.0f);
    } else if (status == 0x80u
        || (status == 0x90u && midi->data[2] == 0u)) {
        noteOff(p, key);
    } else if (status == 0xd0u) {
        setPressureForKey(p, -1,
            static_cast<float>(midi->data[1]) / 127.0f);
    } else if (status == 0xa0u) {
        setPressureForKey(p, key,
            static_cast<float>(midi->data[2]) / 127.0f);
    } else if (status == 0xb0u
        && (midi->data[1] == 120u || midi->data[1] == 123u)) {
        allNotesOff(p);
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

bool queueGuiParams(Plugin& p, const s3g::LowFrequencySynthParams& params)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<double, kSynthParamCount> values {{
        params.outputGainDb, params.fundamental, params.body,
        params.loading, params.coupling, params.damping,
        params.excitationPosition, params.upperModeLevel,
        params.filterCutoffHz, params.filterResonance, params.membraneDrive,
        params.processedMix, params.valvePreamp, params.attackSeconds,
        params.decaySeconds, params.sustain, params.releaseSeconds,
        params.transposeSemitones, params.glideMs,
        params.pitchTransientSemitones, params.amplitudeMotionClock,
        params.amplitudeMotionDivision, params.amplitudeMotionRateHz,
        params.amplitudeMotionDepth,
        params.shred, params.shredFeedback, params.shredMix,
        params.shredCircuit, params.shredColor,
        params.amplitudeMotionPosition,
        params.shredFeedbackToneLevel,
    }};
    std::array<s3g::clap_gui::ParamEvent, kSynthParamCount * 3u> events {};
    std::array<double, kSynthParamCount> clamped {};
    for (uint32_t index = 0u; index < kSynthParamCount; ++index) {
        const clap_id id = kSynthParamIds[index];
        clamped[index] = clampValue(*paramDef(id), values[index]);
        events[index * 3u] = { Kind::GestureBegin, id, 0.0 };
        events[index * 3u + 1u] = { Kind::Value, id, clamped[index] };
        events[index * 3u + 2u] = { Kind::GestureEnd, id, 0.0 };
    }
    if (!p.guiParamEvents.pushBatch(events.data(), events.size())) return false;
    for (uint32_t index = 0u; index < kSynthParamCount; ++index) {
        publishParam(p, kSynthParamIds[index], clamped[index]);
    }
    requestGuiParamService(p);
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

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (p.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(p, pending.paramId, pending.value);
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
    for (auto& voice : p->voices) {
        voice.synth.prepare(p->sampleRate);
        voice.synth.setParams(p->params);
    }
    p->transportFallbackBeat = 0.0;
    clearHeldNotes(*p);
    p->active = false;
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    for (auto& voice : p->voices) {
        voice.synth.reset();
    }
    p->transportFallbackBeat = 0.0;
    clearHeldNotes(*p);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->active = false;
}

struct TransportClock {
    bool playing = false;
    bool hasPosition = false;
    double beat = 0.0;
    double tempo = 120.0;
    double tempoIncrement = 0.0;
};

void updateTransportClock(TransportClock& clock,
    const clap_event_transport_t* transport, double fallbackBeat)
{
    clock = {};
    clock.beat = std::isfinite(fallbackBeat) ? fallbackBeat : 0.0;
    if (!transport) return;
    clock.playing = (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    if ((transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(transport->tempo) && transport->tempo > 0.0) {
        clock.tempo = transport->tempo;
    }
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
    }
}

void advanceTransportClock(TransportClock& clock, double sampleRate)
{
    if (clock.playing) {
        clock.beat += clock.tempo / (60.0 * sampleRate);
    }
    clock.tempo = std::max(1.0, clock.tempo + clock.tempoIncrement);
}

void applyProcessEvent(Plugin& p, const clap_event_header_t* event,
    TransportClock& clock)
{
    if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
        && event->type == CLAP_EVENT_TRANSPORT
        && event->size >= sizeof(clap_event_transport_t)) {
        updateTransportClock(clock,
            reinterpret_cast<const clap_event_transport_t*>(event),
            clock.beat);
        return;
    }
    applyEvent(p, event);
}

void updateVoiceMotionClock(Plugin& p, const TransportClock& clock)
{
    const bool synchronized = p.params.amplitudeMotionClock
        >= static_cast<float>(s3g::MotionClock::Transport);
    float phase = 0.0f;
    if (synchronized) {
        const uint32_t divisionIndex = std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(
                p.params.amplitudeMotionDivision)),
            s3g::kMotionDivisionCount - 1u);
        const double beats = s3g::motionDivisionInfo(
            divisionIndex).beats;
        double position = clock.beat / beats;
        position -= std::floor(position);
        phase = static_cast<float>(position);
    }
    for (auto& voice : p.voices) {
        voice.synth.setAmplitudeMotionTransportPhase(phase, synchronized);
    }
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
    TransportClock transportClock;
    updateTransportClock(transportClock,
        process->transport, p->transportFallbackBeat);

    const clap_input_events_t* events = process->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (process->audio_outputs_count == 0u || !process->audio_outputs) {
        while (eventIndex < eventCount) {
            applyProcessEvent(*p, events->get(events, eventIndex++),
                transportClock);
        }
        p->transportFallbackBeat = transportClock.beat;
        p->active = std::any_of(p->voices.begin(), p->voices.end(),
            [](const VoiceSlot& voice) { return voice.synth.active(); });
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
            applyProcessEvent(*p, event, transportClock);
            ++eventIndex;
        }
        float left = 0.0f;
        float right = 0.0f;
        updateVoiceMotionClock(*p, transportClock);
        for (auto& voice : p->voices) {
            float voiceLeft = 0.0f;
            float voiceRight = 0.0f;
            voice.synth.processFrame(voiceLeft, voiceRight);
            left += voiceLeft;
            right += voiceRight;
        }
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
        advanceTransportClock(transportClock, p->sampleRate);
    }
    while (eventIndex < eventCount) {
        applyProcessEvent(*p, events->get(events, eventIndex++),
            transportClock);
    }
    p->transportFallbackBeat = transportClock.beat;
    p->active = std::any_of(p->voices.begin(), p->voices.end(),
        [](const VoiceSlot& voice) { return voice.synth.active(); });
    const float previous = p->outputPeak.load(std::memory_order_relaxed);
    p->outputPeak.store(p->active
            ? std::max(blockPeak, previous * 0.84f) : 0.0f,
        std::memory_order_relaxed);
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
    std::strncpy(info->name, "Processor LF Synth MIDI In",
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
    if (def.hidden) info->flags |= CLAP_PARAM_IS_HIDDEN;
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
    if (id == kTransposeParamId || id == kPitchTransientParamId) {
        std::snprintf(display, size, "%+.2f st", value);
    } else if (id == kAttackParamId || id == kDecayParamId
        || id == kReleaseParamId) {
        if (value < 1.0) std::snprintf(display, size, "%.1f ms", value * 1000.0);
        else std::snprintf(display, size, "%.3g s", value);
    } else if (id == kGlideParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kFilterCutoffParamId) {
        if (value >= 1000.0) {
            std::snprintf(display, size, "%.2f kHz", value * 0.001);
        } else {
            std::snprintf(display, size, "%.0f Hz", value);
        }
    } else if (id == kFilterMotionRateParamId) {
        std::snprintf(display, size, "%.2f Hz", value);
    } else if (id == kFilterMotionDepthParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (id == kFilterMotionClockParamId) {
        const auto clock = static_cast<s3g::MotionClock>(
            std::min<uint32_t>(static_cast<uint32_t>(std::round(value)),
                s3g::kMotionClockCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::motionClockName(clock));
    } else if (id == kFilterMotionDivisionParamId) {
        std::snprintf(display, size, "%s",
            s3g::motionDivisionInfo(static_cast<uint32_t>(
                std::round(value))).name);
    } else if (id == kAmplitudeMotionPositionParamId) {
        const auto position = static_cast<s3g::AmplitudeMotionPosition>(
            std::min<uint32_t>(static_cast<uint32_t>(std::round(value)),
                s3g::kAmplitudeMotionPositionCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::amplitudeMotionPositionName(position));
    } else if (id == kShredCircuitParamId) {
        const auto circuit = static_cast<s3g::BassShredCircuit>(
            std::min<uint32_t>(static_cast<uint32_t>(std::round(value)),
                s3g::kBassShredCircuitCount - 1u));
        std::snprintf(display, size, "%s",
            s3g::bassShredCircuitName(circuit));
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kMidiReceiveParamId) {
        s3g::drum_midi::valueToText(value, display, size);
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
    if (id == kFilterMotionClockParamId) {
        for (uint32_t index = 0u;
             index < s3g::kMotionClockCount; ++index) {
            const auto clock = static_cast<s3g::MotionClock>(index);
            if (std::strcmp(display,
                    s3g::motionClockName(clock)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kFilterMotionDivisionParamId) {
        for (uint32_t index = 0u;
             index < s3g::kMotionDivisionCount; ++index) {
            if (std::strcmp(display,
                    s3g::motionDivisionInfo(index).name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kAmplitudeMotionPositionParamId) {
        for (uint32_t index = 0u;
             index < s3g::kAmplitudeMotionPositionCount; ++index) {
            const auto position =
                static_cast<s3g::AmplitudeMotionPosition>(index);
            if (std::strcmp(display,
                    s3g::amplitudeMotionPositionName(position)) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kShredCircuitParamId) {
        for (uint32_t index = 0u;
             index < s3g::kBassShredCircuitCount; ++index) {
            const auto circuit = static_cast<s3g::BassShredCircuit>(index);
            if (std::strcmp(display,
                    s3g::bassShredCircuitName(circuit)) == 0) {
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
    const bool normalized = id == kFundamentalParamId || id == kBodyParamId
        || id == kLoadingParamId || id == kCouplingParamId
        || id == kExcitationParamId || id == kDampingParamId
        || id == kSustainParamId
        || id == kHarmonicsParamId || id == kFilterResonanceParamId
        || id == kDriveParamId || id == kProcessedMixParamId
        || id == kValvePreampParamId
        || id == kFilterMotionDepthParamId
        || id == kShredParamId || id == kShredFeedbackParamId
        || id == kShredFeedbackToneLevelParamId
        || id == kShredMixParamId || id == kShredColorParamId;
    if (suffixLength > 0u) {
        if (normalized && suffixIs("%")) converted *= 0.01;
        else if ((id == kAttackParamId || id == kDecayParamId
                || id == kReleaseParamId) && suffixIs("ms")) {
            converted *= 0.001;
        } else if ((id == kAttackParamId || id == kDecayParamId
                || id == kReleaseParamId) && suffixIs("s")) {
        } else if ((id == kTransposeParamId || id == kPitchTransientParamId)
            && suffixIs("st")) {
        } else if (id == kGlideParamId && suffixIs("ms")) {
        } else if (id == kFilterCutoffParamId && suffixIs("Hz")) {
        } else if (id == kFilterCutoffParamId && suffixIs("kHz")) {
            converted *= 1000.0;
        } else if (id == kFilterMotionRateParamId && suffixIs("Hz")) {
        } else if (id == kOutputParamId && suffixIs("dB")) {
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
    auto* p = self(plugin);
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE
            || event->size < sizeof(clap_event_param_value_t)) continue;
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(*p, param->param_id, param->value);
    }
    serviceGuiParamEvents(*p, output);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    SavedState state {};
    const auto* p = self(plugin);
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        state.values[index] = paramValue(*p, kParamDefs[index].id);
    }
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedStateHeader header {};
    if (!s3g::clap_state::readAll(
            stream, &header, sizeof(header))) return false;
    if (header.magic != kStateMagic) return false;
    auto* p = self(plugin);
    if (header.version == kStateVersion
        && header.valueCount == kParamCount) {
        std::array<double, kParamCount> values {};
        if (!s3g::clap_state::readAll(
                stream, values.data(), sizeof(values))) return false;
        for (uint32_t index = 0u; index < kParamCount; ++index) {
            applyParam(*p, kParamDefs[index].id, values[index]);
        }
    } else if (header.version == 6u
        && header.valueCount == kVersionSixParamCount) {
        std::array<double, kVersionSixParamCount> values {};
        if (!s3g::clap_state::readAll(
                stream, values.data(), sizeof(values))) return false;
        for (uint32_t index = 0u;
             index < kVersionSixParamCount; ++index) {
            applyParam(*p, kParamDefs[index].id, values[index]);
        }
        applyParam(*p, kShredFeedbackToneLevelParamId,
            paramDef(kShredFeedbackToneLevelParamId)->defaultValue);
    } else if (header.version == 5u
        && header.valueCount == kVersionFiveParamCount) {
        std::array<double, kVersionFiveParamCount> values {};
        if (!s3g::clap_state::readAll(
                stream, values.data(), sizeof(values))) return false;
        for (uint32_t index = 0u;
             index < kVersionFiveParamCount; ++index) {
            applyParam(*p, kParamDefs[index].id, values[index]);
        }
        applyParam(*p, kAmplitudeMotionPositionParamId,
            paramDef(kAmplitudeMotionPositionParamId)->defaultValue);
        applyParam(*p, kShredFeedbackToneLevelParamId,
            paramDef(kShredFeedbackToneLevelParamId)->defaultValue);
    } else if (header.version == 4u
        && header.valueCount == kVersionFourParamCount) {
        std::array<double, kVersionFourParamCount> values {};
        if (!s3g::clap_state::readAll(
                stream, values.data(), sizeof(values))) return false;
        for (uint32_t index = 0u;
             index < kVersionFourParamCount; ++index) {
            applyParam(*p, kParamDefs[index].id, values[index]);
        }
        applyParam(*p, kShredCircuitParamId,
            paramDef(kShredCircuitParamId)->defaultValue);
        applyParam(*p, kShredColorParamId,
            paramDef(kShredColorParamId)->defaultValue);
        applyParam(*p, kAmplitudeMotionPositionParamId,
            paramDef(kAmplitudeMotionPositionParamId)->defaultValue);
        applyParam(*p, kShredFeedbackToneLevelParamId,
            paramDef(kShredFeedbackToneLevelParamId)->defaultValue);
    } else if ((header.version == 1u || header.version == 2u
            || header.version == 3u)
        && header.valueCount == kLegacyParamCount) {
        std::array<double, kLegacyParamCount> values {};
        if (!s3g::clap_state::readAll(
                stream, values.data(), sizeof(values))) return false;
        // v0.8 stored filter-motion depth in octaves (0..6) at this slot.
        if (header.version == 1u) {
            constexpr uint32_t kMotionAmountStateIndex = 23u;
            values[kMotionAmountStateIndex] = std::clamp(
                values[kMotionAmountStateIndex] / 6.0, 0.0, 1.0);
        }
        for (uint32_t index = 0u; index < kLegacyParamCount; ++index) {
            applyParam(*p, kParamDefs[index].id, values[index]);
        }
        applyParam(*p, kShredParamId, paramDef(kShredParamId)->defaultValue);
        applyParam(*p, kShredFeedbackParamId,
            paramDef(kShredFeedbackParamId)->defaultValue);
        applyParam(*p, kShredMixParamId,
            paramDef(kShredMixParamId)->defaultValue);
        applyParam(*p, kShredCircuitParamId,
            paramDef(kShredCircuitParamId)->defaultValue);
        applyParam(*p, kShredColorParamId,
            paramDef(kShredColorParamId)->defaultValue);
        applyParam(*p, kAmplitudeMotionPositionParamId,
            paramDef(kAmplitudeMotionPositionParamId)->defaultValue);
        applyParam(*p, kShredFeedbackToneLevelParamId,
            paramDef(kShredFeedbackToneLevelParamId)->defaultValue);
    } else {
        return false;
    }
    clearHeldNotes(*p);
    for (auto& voice : p->voices) {
        voice.synth.reset();
        voice.synth.setParams(p->params);
    }
    p->transportFallbackBeat = 0.0;
    p->active = false;
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    if (p->host && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const double samples = std::ceil(
        (paramValue(*p, kReleaseParamId) + 0.030
            + paramValue(*p, kShredFeedbackParamId)
                * paramValue(*p, kShredFeedbackToneLevelParamId)
                * paramValue(*p, kShredMixParamId) * 2.0)
            * p->sampleRate);
    return static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u, samples));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct SynthUiRow {
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
constexpr layout::Canvas kSynthCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr layout::Column kLeftColumn {
    kLeftPanelX, kPanelWidth, layout::kStandardMetrics.contentTop
};
constexpr layout::Column kRightColumn {
    kRightPanelX, kPanelWidth, layout::kStandardMetrics.contentTop
};

constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    kLeftColumn, layout::kStandardMetrics.contentTop, 1u);
constexpr auto kModalPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kOutputPanel, 6u);
constexpr auto kPitchPanel = layout::fittedStackPanel(
    layout::PanelRole::ToneShape, kModalPanel, 3u);
constexpr auto kEnvelopePanel = layout::fittedStackPanel(
    layout::PanelRole::Envelope, kPitchPanel, 4u);

constexpr auto kTonePanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Topology,
    kRightColumn, layout::kStandardMetrics.contentTop, 6u);
constexpr auto kShredPanel = layout::fittedStackPanel(
    layout::PanelRole::Projection, kTonePanel, 6u);
constexpr auto kMotionPanel = layout::fittedStackPanel(
    layout::PanelRole::Motion, kShredPanel, 5u);
constexpr auto kRoutingPanel = layout::fittedStackPanel(
    layout::PanelRole::Utility, kMotionPanel, 1u);

constexpr std::array kLeftPanels {
    kOutputPanel, kModalPanel, kPitchPanel, kEnvelopePanel,
};
constexpr std::array kRightPanels {
    kTonePanel, kShredPanel, kMotionPanel, kRoutingPanel,
};

static_assert(layout::validateColumn(kLeftPanels, kSynthCanvas));
static_assert(layout::validateColumn(kRightPanels, kSynthCanvas, false));
static_assert(layout::rolesFollowTemplate(
    kLeftPanels, layout::kProceduralEncoderTemplate, true));
static_assert(layout::rolesFollowTemplate(
    kRightPanels, layout::kProceduralEncoderTemplate, false));
static_assert(layout::rowY(kOutputPanel, 0u) == 78.0);
static_assert(kEnvelopePanel.frame.y + kEnvelopePanel.frame.height
    + 12.0 <= kGuiHeight);

constexpr std::array<SynthUiRow, 32u> kUiRows {{
    { kOutputParamId, "OUT", kLeftPanelX, kPanelWidth,
        layout::rowY(kOutputPanel, 0u) },

    { kFundamentalParamId, "SUB", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 0u) },
    { kBodyParamId, "MODAL BODY", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 1u) },
    { kLoadingParamId, "MASS LOAD", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 2u) },
    { kCouplingParamId, "COUPLING", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 3u) },
    { kDampingParamId, "DAMPING", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 4u) },
    { kExcitationParamId, "EXCITATION", kLeftPanelX, kPanelWidth,
        layout::rowY(kModalPanel, 5u) },

    { kTransposeParamId, "TRANSPOSE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPitchPanel, 0u) },
    { kGlideParamId, "GLIDE", kLeftPanelX, kPanelWidth,
        layout::rowY(kPitchPanel, 1u) },
    { kPitchTransientParamId, "PITCH TRANSIENT", kLeftPanelX, kPanelWidth,
        layout::rowY(kPitchPanel, 2u) },

    { kAttackParamId, "ATTACK", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 0u) },
    { kDecayParamId, "DECAY", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 1u) },
    { kSustainParamId, "SUSTAIN", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 2u) },
    { kReleaseParamId, "RELEASE", kLeftPanelX, kPanelWidth,
        layout::rowY(kEnvelopePanel, 3u) },

    { kHarmonicsParamId, "UPPER MODES", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 0u) },
    { kFilterCutoffParamId, "CUTOFF", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 1u) },
    { kFilterResonanceParamId, "RESONANCE", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 2u) },
    { kDriveParamId, "MEMBRANE DRIVE", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 3u) },
    { kProcessedMixParamId, "FILTER MIX", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 4u) },
    { kValvePreampParamId, "TUBE", kRightPanelX, kPanelWidth,
        layout::rowY(kTonePanel, 5u) },

    { kShredCircuitParamId, "CIRCUIT", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 0u) },
    { kShredParamId, "SHRED", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 1u) },
    { kShredFeedbackParamId, "FEEDBACK", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 2u) },
    { kShredFeedbackToneLevelParamId, "FB TONE LEVEL", kRightPanelX,
        kPanelWidth, layout::rowY(kShredPanel, 3u) },
    { kShredColorParamId, "COLOR", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 4u) },
    { kShredMixParamId, "MIX", kRightPanelX, kPanelWidth,
        layout::rowY(kShredPanel, 5u) },

    { kFilterMotionClockParamId, "CLOCK", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 0u) },
    { kFilterMotionDivisionParamId, "DIVISION", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 1u) },
    { kFilterMotionRateParamId, "FREE RATE", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 2u) },
    { kFilterMotionDepthParamId, "AMOUNT", kRightPanelX, kPanelWidth,
        layout::rowY(kMotionPanel, 3u) },
    { kAmplitudeMotionPositionParamId, "POSITION", kRightPanelX,
        kPanelWidth, layout::rowY(kMotionPanel, 4u) },

    { kMidiReceiveParamId, "MIDI RECEIVE", kRightPanelX, kPanelWidth,
        layout::rowY(kRoutingPanel, 0u) },
}};

static_assert(kUiRows[0u].id == kOutputParamId);

bool isUiMenuParam(clap_id id)
{
    return id == kMidiReceiveParamId || id == kFilterMotionClockParamId
        || id == kFilterMotionDivisionParamId
        || id == kAmplitudeMotionPositionParamId
        || id == kShredCircuitParamId;
}

uint32_t uiMenuItemCount(clap_id id)
{
    if (id == kMidiReceiveParamId) return 17u;
    if (id == kFilterMotionClockParamId) {
        return s3g::kMotionClockCount;
    }
    if (id == kFilterMotionDivisionParamId) {
        return s3g::kMotionDivisionCount;
    }
    if (id == kAmplitudeMotionPositionParamId) {
        return s3g::kAmplitudeMotionPositionCount;
    }
    if (id == kShredCircuitParamId) {
        return s3g::kBassShredCircuitCount;
    }
    return 0u;
}

bool isLogUiParam(clap_id id)
{
    return id == kAttackParamId || id == kDecayParamId
        || id == kReleaseParamId || id == kFilterCutoffParamId
        || id == kFilterMotionRateParamId;
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

s3g::LowFrequencySynthParams publishedParamsSnapshot(const Plugin& p)
{
    s3g::LowFrequencySynthParams params;
    params.outputGainDb = static_cast<float>(paramValue(p, kOutputParamId));
    params.fundamental = static_cast<float>(paramValue(p, kFundamentalParamId));
    params.body = static_cast<float>(paramValue(p, kBodyParamId));
    params.loading = static_cast<float>(paramValue(p, kLoadingParamId));
    params.coupling = static_cast<float>(paramValue(p, kCouplingParamId));
    params.damping = static_cast<float>(paramValue(p, kDampingParamId));
    params.excitationPosition = static_cast<float>(paramValue(p, kExcitationParamId));
    params.upperModeLevel = static_cast<float>(paramValue(p, kHarmonicsParamId));
    params.filterCutoffHz = static_cast<float>(paramValue(p, kFilterCutoffParamId));
    params.filterResonance = static_cast<float>(paramValue(p, kFilterResonanceParamId));
    params.membraneDrive = static_cast<float>(paramValue(p, kDriveParamId));
    params.processedMix = static_cast<float>(paramValue(p, kProcessedMixParamId));
    params.valvePreamp = static_cast<float>(paramValue(p, kValvePreampParamId));
    params.attackSeconds = static_cast<float>(paramValue(p, kAttackParamId));
    params.decaySeconds = static_cast<float>(paramValue(p, kDecayParamId));
    params.sustain = static_cast<float>(paramValue(p, kSustainParamId));
    params.releaseSeconds = static_cast<float>(paramValue(p, kReleaseParamId));
    params.transposeSemitones = static_cast<float>(paramValue(p, kTransposeParamId));
    params.glideMs = static_cast<float>(paramValue(p, kGlideParamId));
    params.pitchTransientSemitones = static_cast<float>(paramValue(p, kPitchTransientParamId));
    params.amplitudeMotionClock = static_cast<float>(paramValue(p, kFilterMotionClockParamId));
    params.amplitudeMotionDivision = static_cast<float>(paramValue(p, kFilterMotionDivisionParamId));
    params.amplitudeMotionRateHz = static_cast<float>(paramValue(p, kFilterMotionRateParamId));
    params.amplitudeMotionDepth = static_cast<float>(paramValue(p, kFilterMotionDepthParamId));
    params.amplitudeMotionPosition = static_cast<float>(
        paramValue(p, kAmplitudeMotionPositionParamId));
    params.shred = static_cast<float>(paramValue(p, kShredParamId));
    params.shredFeedback = static_cast<float>(paramValue(p, kShredFeedbackParamId));
    params.shredFeedbackToneLevel = static_cast<float>(
        paramValue(p, kShredFeedbackToneLevelParamId));
    params.shredMix = static_cast<float>(paramValue(p, kShredMixParamId));
    params.shredCircuit = static_cast<float>(paramValue(p, kShredCircuitParamId));
    params.shredColor = static_cast<float>(paramValue(p, kShredColorParamId));
    return params;
}

float randomUnit(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}

s3g::LowFrequencySynthParams safeRandomParams(const Plugin& p, uint32_t seed)
{
    auto result = publishedParamsSnapshot(p);
    result.fundamental = 0.82f + randomUnit(seed) * 0.18f;
    result.body = 0.48f + randomUnit(seed) * 0.48f;
    result.loading = 0.58f + randomUnit(seed) * 0.42f;
    result.coupling = 0.18f + randomUnit(seed) * 0.64f;
    result.excitationPosition = randomUnit(seed) * 0.92f;
    result.damping = 0.12f + randomUnit(seed) * 0.58f;
    result.attackSeconds = std::pow(10.0f,
        -3.1f + randomUnit(seed) * 2.0f);
    result.decaySeconds = 0.05f + randomUnit(seed) * 0.85f;
    result.sustain = 0.55f + randomUnit(seed) * 0.45f;
    result.releaseSeconds = 0.06f + randomUnit(seed) * 1.40f;
    result.glideMs = randomUnit(seed) * 320.0f;
    result.pitchTransientSemitones = randomUnit(seed) < 0.72f
        ? 0.0f : -2.0f + randomUnit(seed) * 8.0f;
    result.upperModeLevel = 0.05f + randomUnit(seed) * 0.31f;
    result.filterCutoffHz = std::pow(2.0f,
        std::log2(180.0f) + randomUnit(seed)
            * (std::log2(6200.0f) - std::log2(180.0f)));
    result.filterResonance = 0.04f + randomUnit(seed) * 0.32f;
    result.membraneDrive = 0.06f + randomUnit(seed) * 0.54f;
    result.processedMix = 0.16f + randomUnit(seed) * 0.48f;
    result.amplitudeMotionRateHz = std::pow(2.0f,
        std::log2(0.35f) + randomUnit(seed)
            * (std::log2(12.0f) - std::log2(0.35f)));
    result.amplitudeMotionDepth = randomUnit(seed) < 0.28f
        ? 0.0f : 0.18f + randomUnit(seed) * 0.62f;
    result.amplitudeMotionClock = randomUnit(seed) < 0.25f
        ? static_cast<float>(s3g::MotionClock::Free)
        : static_cast<float>(s3g::MotionClock::Transport);
    result.amplitudeMotionDivision = std::floor(
        6.0f + randomUnit(seed) * 10.0f);
    result.amplitudeMotionPosition = randomUnit(seed) < 0.5f
        ? static_cast<float>(s3g::AmplitudeMotionPosition::PreShred)
        : static_cast<float>(s3g::AmplitudeMotionPosition::PostShred);
    result.valvePreamp = 0.08f + randomUnit(seed) * 0.76f;
    result.shred = randomUnit(seed) < 0.42f
        ? 0.0f : 0.12f + randomUnit(seed) * 0.58f;
    result.shredFeedback = result.shred > 0.0f
        ? randomUnit(seed) * 0.52f : 0.0f;
    result.shredFeedbackToneLevel = result.shredFeedback > 0.0f
        ? 0.35f + randomUnit(seed) * 0.65f : 1.0f;
    result.shredMix = result.shred > 0.0f
        ? 0.12f + randomUnit(seed) * 0.50f : 0.0f;
    result.shredCircuit = result.shred > 0.0f
        ? std::floor(randomUnit(seed)
            * static_cast<float>(s3g::kBassShredCircuitCount))
        : static_cast<float>(s3g::BassShredCircuit::Shred);
    result.shredColor = 0.28f + randomUnit(seed) * 0.50f;
    return result;
}

} // namespace

@interface S3GLowFrequencySynthView : NSView {
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

@implementation S3GLowFrequencySynthView

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
    auto* p = static_cast<Plugin*>(_plugin);
    _observedParamRevision = p
        ? p->parameterRevision.load(std::memory_order_acquire) : 0u;
    _factoryPresetIndex = p ? s3g::lowFrequencySynthFactoryPresetIndex(
        publishedParamsSnapshot(*p)) : 0;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        _factoryPresetIndex >= 0
            ? s3g::lowFrequencySynthFactoryPresetInfo(
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
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kLowFrequencySynthFactoryPresetCount - 1u));
    if (!queueGuiParams(*p, s3g::lowFrequencySynthFactoryPreset(
            static_cast<uint32_t>(index)))) {
        NSBeep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::lowFrequencySynthFactoryPresetInfo(
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
            [=](const SynthUiRow& candidate) {
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
                s3g::lowFrequencySynthFactoryPresetInfo(index).name];
        }
    } else {
        for (uint32_t index = 0u; index < count; ++index) {
            char text[64] {};
            if (paramsValueToText(
                    &static_cast<Plugin*>(_plugin)->plugin, _openMenu,
                    static_cast<double>(index), text, sizeof(text))) {
                items[index] = [NSString stringWithUTF8String:text];
            } else {
                items[index] = @"—";
            }
        }
    }
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(paramValue(
            *static_cast<Plugin*>(_plugin), _openMenu)));
    s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
        items, count, selected, _hoverMenuItem, attrs, style);
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
            _factoryPresetIndex = s3g::lowFrequencySynthFactoryPresetIndex(
                publishedParamsSnapshot(*p));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::lowFrequencySynthFactoryPresetInfo(
                        static_cast<uint32_t>(_factoryPresetIndex)).name
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
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g PROCESSOR LF SYNTH",
        [NSString stringWithUTF8String:_presetName],
        s3g::clap_gui::peakDbText(
            p->outputPeak.load(std::memory_order_relaxed)),
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
    drawPanel(@"OUTPUT", kOutputPanel);
    drawPanel(@"MODAL CORE", kModalPanel);
    drawPanel(@"PITCH", kPitchPanel);
    drawPanel(@"ENVELOPE", kEnvelopePanel);
    drawPanel(@"TONE", kTonePanel);
    drawPanel(@"STEREO SHRED", kShredPanel);
    drawPanel(@"AMPLITUDE LFO", kMotionPanel);
    drawPanel(@"ROUTING", kRoutingPanel);

    for (const auto& row : kUiRows) {
        const double value = paramValue(*p, row.id);
        char text[64] {};
        paramsValueToText(&p->plugin, row.id, value, text, sizeof(text));
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
        [=](const SynthUiRow& candidate) { return candidate.id == id; });
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
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
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
                queueGuiParamGesture(*p, menuParam,
                    static_cast<double>(hit));
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
        _menuItemCount = s3g::kLowFrequencySynthFactoryPresetCount;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &p->plugin, @"Processor LF Synth", kOutputParamId, &name)) {
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
                &p->plugin, @"Processor LF Synth", &name)) {
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
        if (queueGuiParams(*p, safeRandomParams(*p, arc4random()))) {
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
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GLowFrequencySynthView alloc] initWithPlugin:p];
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
    [static_cast<S3GLowFrequencySynthView*>(p->guiView) stopRefreshTimer];
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
    [static_cast<S3GLowFrequencySynthView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GLowFrequencySynthView*>(p->guiView) stopRefreshTimer];
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
    CLAP_PLUGIN_FEATURE_STEREO,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.low-frequency-synth",
    "s3g Processor LF Synth",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "1.3.6",
    "A monophonic modal sub-bass engine with a protected membrane foundation, coordinated tube weight, and selectable bass-tuned stereo Shred circuits.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    for (const auto& def : kParamDefs) {
        applyParam(*p, def.id, def.defaultValue);
    }
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
