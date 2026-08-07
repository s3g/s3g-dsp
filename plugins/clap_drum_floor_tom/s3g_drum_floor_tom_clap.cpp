#include "s3g_drum_floor_tom.h"
#include "s3g_drum_floor_tom_presets.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-name.h>
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

constexpr uint32_t kStateMagic = 0x46473353u; // "S3GF" in little endian.
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 680u;

constexpr clap_id kTuneParamId = 1u;
constexpr clap_id kNoteTrackingParamId = 2u;
constexpr clap_id kPitchDropParamId = 3u;
constexpr clap_id kPitchSweepTimeParamId = 4u;
constexpr clap_id kShellSpreadParamId = 5u;
constexpr clap_id kBodyParamId = 6u;
constexpr clap_id kRingParamId = 7u;
constexpr clap_id kBodyDecayParamId = 8u;
constexpr clap_id kPunchParamId = 9u;
constexpr clap_id kDampingParamId = 10u;
constexpr clap_id kRimLevelParamId = 11u;
constexpr clap_id kRimCharacterParamId = 12u;
constexpr clap_id kRimDecayParamId = 13u;
constexpr clap_id kStickLevelParamId = 14u;
constexpr clap_id kStickToneParamId = 15u;
constexpr clap_id kStickDecayParamId = 16u;
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
constexpr uint32_t kParamCount = 27u;
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

constexpr std::array<ParamDef, kParamCount> kParamDefs {{
    { kTuneParamId, "Tune", "Shell", 35.0, 180.0, 72.0, false },
    { kNoteTrackingParamId, "Note Tracking", "Shell / MIDI", 0.0, 1.0, 0.75, false },
    { kPitchDropParamId, "Pitch Drop", "Shell / Pitch", -6.0, 30.0, 8.0, false },
    { kPitchSweepTimeParamId, "Sweep Time", "Shell / Pitch", 1.0, 300.0, 45.0, false },
    { kShellSpreadParamId, "Shell Spread", "Shell", 0.0, 1.0, 0.42, false },
    { kBodyParamId, "Body", "Shell", 0.0, 1.0, 0.70, false },
    { kRingParamId, "Ring", "Shell", 0.0, 1.0, 0.42, false },
    { kBodyDecayParamId, "Body Decay", "Shell", 0.04, 4.0, 0.70, false },
    { kPunchParamId, "Punch", "Head", 0.0, 1.0, 0.72, false },
    { kDampingParamId, "Damping", "Head / Shell", 0.0, 1.0, 0.35, false },
    { kRimLevelParamId, "Rim Level", "Rim", 0.0, 1.0, 0.55, false },
    { kRimCharacterParamId, "Rim Character", "Rim", 0.0, 1.0, 0.48, false },
    { kRimDecayParamId, "Rim Decay", "Rim", 0.02, 0.35, 0.07, false },
    { kStickLevelParamId, "Stick Level", "Stick", 0.0, 1.0, 0.35, false },
    { kStickToneParamId, "Stick Tone", "Stick", 0.0, 1.0, 0.58, false },
    { kStickDecayParamId, "Stick Decay", "Stick", 0.5, 40.0, 4.5, false },
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
    { kTriggerParamId, "Trigger", "Performance", 0.0, 2.0, 0.0, true },
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
    s3g::DrumFloorTomParams params {};
    s3g::DrumFloorTom floorTom;
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    std::array<std::atomic<double>, kSavedParamCount> pendingStateValues {};
    std::atomic<uint64_t> pendingStateSequence { 0u };
    uint64_t consumedStateSequence = 0u;
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<uint32_t> pendingHeadTriggers { 0u };
    std::atomic<uint32_t> pendingRimTriggers { 0u };
    std::atomic<uint64_t> activeTailSamples { 0u };
    std::atomic<bool> tailChangePending { false };
    std::atomic<bool> guiRetryPending { false };
    std::atomic<bool> activated { false };
    std::atomic<bool> preserveOutputOnNextStateLoad { false };
    std::atomic<double> preservedOutputValue { -6.0 };
    std::atomic<uint64_t> parameterRevision { 0u };
    std::atomic<float> visualActivity { 0.0f };
    uint32_t triggerCode = 0u;
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
    if (id < kTuneParamId || id > kTriggerParamId) return nullptr;
    const auto& def = kParamDefs[id - kTuneParamId];
    return def.id == id ? &def : nullptr;
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
    if (id < kTuneParamId || id > kTriggerParamId) return;
    p.publishedParams[id - kTuneParamId].store(
        value, std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    if (id < kTuneParamId || id > kTriggerParamId) return 0.0;
    return p.publishedParams[id - kTuneParamId].load(
        std::memory_order_acquire);
}

s3g::DrumFloorTomParams publishedParamsSnapshot(const Plugin& p)
{
    s3g::DrumFloorTomParams params;
    params.tuneHz = static_cast<float>(paramValue(p, kTuneParamId));
    params.noteTracking = static_cast<float>(
        paramValue(p, kNoteTrackingParamId));
    params.pitchDropSemitones = static_cast<float>(
        paramValue(p, kPitchDropParamId));
    params.pitchSweepMs = static_cast<float>(
        paramValue(p, kPitchSweepTimeParamId));
    params.shellSpread = static_cast<float>(
        paramValue(p, kShellSpreadParamId));
    params.body = static_cast<float>(paramValue(p, kBodyParamId));
    params.ring = static_cast<float>(paramValue(p, kRingParamId));
    params.bodyDecaySeconds = static_cast<float>(
        paramValue(p, kBodyDecayParamId));
    params.punch = static_cast<float>(paramValue(p, kPunchParamId));
    params.damping = static_cast<float>(paramValue(p, kDampingParamId));
    params.rimLevel = static_cast<float>(paramValue(p, kRimLevelParamId));
    params.rimCharacter = static_cast<float>(
        paramValue(p, kRimCharacterParamId));
    params.rimDecaySeconds = static_cast<float>(
        paramValue(p, kRimDecayParamId));
    params.stickLevel = static_cast<float>(
        paramValue(p, kStickLevelParamId));
    params.stickTone = static_cast<float>(paramValue(p, kStickToneParamId));
    params.stickDecayMs = static_cast<float>(
        paramValue(p, kStickDecayParamId));
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

uint64_t tailSamplesForParams(const s3g::DrumFloorTomParams& params,
    double sampleRate)
{
    const double seconds = s3g::drumFloorTomTailSeconds(params, sampleRate);
    return static_cast<uint64_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * sampleRate)));
}

void extendActiveTail(Plugin& p, uint64_t samples)
{
    uint64_t current = p.activeTailSamples.load(std::memory_order_relaxed);
    while (current < samples
        && !p.activeTailSamples.compare_exchange_weak(current, samples,
            std::memory_order_release, std::memory_order_relaxed)) {}
}

void ageActiveTail(Plugin& p, uint32_t frames)
{
    uint64_t current = p.activeTailSamples.load(std::memory_order_relaxed);
    while (current > 0u) {
        const uint64_t remaining = current > frames ? current - frames : 0u;
        if (p.activeTailSamples.compare_exchange_weak(current, remaining,
                std::memory_order_release, std::memory_order_relaxed)) {
            break;
        }
    }
}

void triggerFloorTom(Plugin& p, s3g::DrumTomArticulation articulation,
    float velocity, int midiNote)
{
    p.floorTom.trigger(articulation, std::clamp(velocity, 0.0f, 1.0f),
        std::clamp(midiNote, 0, 127));
    extendActiveTail(p, tailSamplesForParams(p.params, p.sampleRate));
    p.active = true;
}

void releaseTransientTrigger(Plugin& p)
{
    if (p.triggerCode == 0u) return;
    p.triggerCode = 0u;
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
    if (id == kTriggerParamId) {
        const uint32_t code = static_cast<uint32_t>(value);
        if (code > 0u && code != p.triggerCode) {
            const auto articulation = code == 2u
                ? s3g::DrumTomArticulation::RimStick
                : s3g::DrumTomArticulation::Head;
            if (triggerImmediately) {
                triggerFloorTom(p, articulation, 1.0f, 43);
            } else {
                auto& pending = code == 2u
                    ? p.pendingRimTriggers : p.pendingHeadTriggers;
                pending.fetch_add(1u, std::memory_order_relaxed);
                if (p.host && p.host->request_process) {
                    p.host->request_process(p.host);
                }
            }
        }
        p.triggerCode = code;
        publishParam(p, id, static_cast<double>(code));
        return;
    }

    const float v = static_cast<float>(value);
    bool tailChanged = false;
    switch (id) {
    case kTuneParamId: p.params.tuneHz = v; break;
    case kNoteTrackingParamId: p.params.noteTracking = v; break;
    case kPitchDropParamId: p.params.pitchDropSemitones = v; break;
    case kPitchSweepTimeParamId:
        tailChanged = std::fabs(p.params.pitchSweepMs - v) > 1.0e-6f;
        p.params.pitchSweepMs = v;
        break;
    case kShellSpreadParamId: p.params.shellSpread = v; break;
    case kBodyParamId:
        tailChanged = std::fabs(p.params.body - v) > 1.0e-6f;
        p.params.body = v;
        break;
    case kRingParamId:
        tailChanged = std::fabs(p.params.ring - v) > 1.0e-6f;
        p.params.ring = v;
        break;
    case kBodyDecayParamId:
        tailChanged = std::fabs(
            p.params.bodyDecaySeconds - v) > 1.0e-6f;
        p.params.bodyDecaySeconds = v;
        break;
    case kPunchParamId:
        tailChanged = std::fabs(p.params.punch - v) > 1.0e-6f;
        p.params.punch = v;
        break;
    case kDampingParamId:
        tailChanged = std::fabs(p.params.damping - v) > 1.0e-6f;
        p.params.damping = v;
        break;
    case kRimLevelParamId:
        tailChanged = std::fabs(p.params.rimLevel - v) > 1.0e-6f;
        p.params.rimLevel = v;
        break;
    case kRimCharacterParamId: p.params.rimCharacter = v; break;
    case kRimDecayParamId:
        tailChanged = std::fabs(
            p.params.rimDecaySeconds - v) > 1.0e-6f;
        p.params.rimDecaySeconds = v;
        break;
    case kStickLevelParamId:
        tailChanged = std::fabs(p.params.stickLevel - v) > 1.0e-6f;
        p.params.stickLevel = v;
        break;
    case kStickToneParamId: p.params.stickTone = v; break;
    case kStickDecayParamId:
        tailChanged = std::fabs(p.params.stickDecayMs - v) > 1.0e-6f;
        p.params.stickDecayMs = v;
        break;
    case kDriveParamId:
        tailChanged = std::fabs(
            p.params.character.drive - v) > 1.0e-6f;
        p.params.character.drive = v;
        break;
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
    p.floorTom.setParams(p.params);
    p.params = p.floorTom.params();
    publishParam(p, id, rawParamValue(p, id));
    p.parameterRevision.fetch_add(1u, std::memory_order_release);
    if (tailChanged) {
        // Some character state is live rather than voice-latched. Lengthening
        // a tail-affecting control while a hit is active must therefore extend
        // the conservative bound as well; compare/exchange never shortens it.
        if (p.active) {
            extendActiveTail(p, tailSamplesForParams(p.params, p.sampleRate));
        }
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
    case kShellSpreadParamId: return p.params.shellSpread;
    case kBodyParamId: return p.params.body;
    case kRingParamId: return p.params.ring;
    case kBodyDecayParamId: return p.params.bodyDecaySeconds;
    case kPunchParamId: return p.params.punch;
    case kDampingParamId: return p.params.damping;
    case kRimLevelParamId: return p.params.rimLevel;
    case kRimCharacterParamId: return p.params.rimCharacter;
    case kRimDecayParamId: return p.params.rimDecaySeconds;
    case kStickLevelParamId: return p.params.stickLevel;
    case kStickToneParamId: return p.params.stickTone;
    case kStickDecayParamId: return p.params.stickDecayMs;
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
    case kTriggerParamId: return static_cast<double>(p.triggerCode);
    default: return 0.0;
    }
}

void assignSavedStateValues(s3g::DrumFloorTomParams& params,
    const std::array<double, kSavedParamCount>& values)
{
    const auto value = [&](clap_id id) {
        return static_cast<float>(values[id - kTuneParamId]);
    };
    params.tuneHz = value(kTuneParamId);
    params.noteTracking = value(kNoteTrackingParamId);
    params.pitchDropSemitones = value(kPitchDropParamId);
    params.pitchSweepMs = value(kPitchSweepTimeParamId);
    params.shellSpread = value(kShellSpreadParamId);
    params.body = value(kBodyParamId);
    params.ring = value(kRingParamId);
    params.bodyDecaySeconds = value(kBodyDecayParamId);
    params.punch = value(kPunchParamId);
    params.damping = value(kDampingParamId);
    params.rimLevel = value(kRimLevelParamId);
    params.rimCharacter = value(kRimCharacterParamId);
    params.rimDecaySeconds = value(kRimDecayParamId);
    params.stickLevel = value(kStickLevelParamId);
    params.stickTone = value(kStickToneParamId);
    params.stickDecayMs = value(kStickDecayParamId);
    params.character.drive = value(kDriveParamId);
    params.character.bias = value(kBiasParamId);
    params.character.compression = value(kCompressionParamId);
    params.character.sampleRateReduction = value(kRateReductionParamId);
    params.character.bitDepthReduction = value(kBitDepthParamId);
    params.character.reconstruction = value(kReconstructionParamId);
    params.character.tone = value(kCharacterToneParamId);
    params.stereoWidth = value(kStereoWidthParamId);
    params.velocitySensitivity = value(kVelocityParamId);
    params.outputGainDb = value(kOutputParamId);
}

void stageSavedState(Plugin& p,
    const std::array<double, kSavedParamCount>& values)
{
    // State loading is a main-thread operation, while process() can be active.
    // An odd/even sequence makes the atomic value array a bounded seqlock. All
    // sequence and slot operations are sequentially consistent so their one
    // total order either yields one complete state or defers it by one block.
    uint64_t sequence = p.pendingStateSequence.load(
        std::memory_order_seq_cst);
    if ((sequence & 1u) != 0u) ++sequence;
    p.pendingStateSequence.store(sequence + 1u, std::memory_order_seq_cst);
    for (uint32_t index = 0u; index < values.size(); ++index) {
        p.pendingStateValues[index].store(
            values[index], std::memory_order_seq_cst);
    }
    p.pendingStateSequence.store(sequence + 2u, std::memory_order_seq_cst);
}

bool consumePendingState(Plugin& p)
{
    const uint64_t before = p.pendingStateSequence.load(
        std::memory_order_seq_cst);
    if ((before & 1u) != 0u || before == p.consumedStateSequence) {
        return false;
    }
    std::array<double, kSavedParamCount> values {};
    for (uint32_t index = 0u; index < values.size(); ++index) {
        values[index] = p.pendingStateValues[index].load(
            std::memory_order_seq_cst);
    }
    const uint64_t after = p.pendingStateSequence.load(
        std::memory_order_seq_cst);
    if (before != after || (after & 1u) != 0u) return false;

    s3g::DrumFloorTomParams next = p.params;
    assignSavedStateValues(next, values);
    p.floorTom.setParams(next);
    p.params = p.floorTom.params();
    p.pendingHeadTriggers.store(0u, std::memory_order_relaxed);
    p.pendingRimTriggers.store(0u, std::memory_order_relaxed);
    p.triggerCode = 0u;
    publishParam(p, kTriggerParamId, 0.0);
    if (p.active) {
        extendActiveTail(p, tailSamplesForParams(p.params, p.sampleRate));
    }
    p.consumedStateSequence = after;
    return true;
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
            const bool rim = note->key == 37 || note->key == 53
                || note->key == 55;
            const int normalizedNote = note->key == 53 ? 41
                : (rim ? 43 : note->key);
            triggerFloorTom(p, rim ? s3g::DrumTomArticulation::RimStick
                                   : s3g::DrumTomArticulation::Head,
                static_cast<float>(note->velocity), normalizedNote);
        }
    } else if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if ((midi->data[0] & 0xf0u) == 0x90u && midi->data[2] > 0u) {
            const int note = midi->data[1];
            const bool rim = note == 37 || note == 53 || note == 55;
            const int normalizedNote = note == 53 ? 41
                : (rim ? 43 : note);
            triggerFloorTom(p, rim ? s3g::DrumTomArticulation::RimStick
                                   : s3g::DrumTomArticulation::Head,
                static_cast<float>(midi->data[2]) / 127.0f,
                normalizedNote);
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

void requestGuiParamRetry(Plugin& p)
{
    if (p.guiRetryPending.exchange(true, std::memory_order_acq_rel)) return;
    if (p.activated.load(std::memory_order_acquire)) {
        // The consumer is the audio thread while active. Bounce through the
        // required main-thread callback before asking the params extension for
        // another flush; request_flush() is not an audio-thread operation.
        if (p.host && p.host->request_callback) {
            p.host->request_callback(p.host);
        } else if (p.host && p.host->request_process) {
            p.host->request_process(p.host);
        }
    } else {
        // params.flush() is a main-thread operation while inactive.
        p.guiRetryPending.store(false, std::memory_order_release);
        requestGuiParamService(p);
    }
}

bool queueGuiParamEvent(Plugin& p,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!p.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(p);
    return true;
}

bool queueGuiParamGestureBegin(Plugin& p, clap_id id)
{
    return queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

bool queueGuiParamValue(Plugin& p, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return false;
    value = clampValue(*def, value);
    if (!p.guiParamEvents.push({
            s3g::clap_gui::ParamEventKind::Value, id, value })) {
        return false;
    }
    // params.get_value() may reflect a GUI edit as soon as, but never before,
    // the corresponding host event has a durable queue slot.
    publishParam(p, id, value);
    requestGuiParamService(p);
    return true;
}

bool queueGuiParamGestureEnd(Plugin& p, clap_id id)
{
    return queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

bool queueGuiParamGesture(Plugin& p, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return false;
    value = clampValue(*def, value);
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, value },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (!p.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    publishParam(p, id, value);
    requestGuiParamService(p);
    return true;
}

bool queueGuiParamSet(Plugin& p,
    const std::array<double, kSavedParamCount>& sourceValues)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    std::array<double, kSavedParamCount> values {};
    std::array<s3g::clap_gui::ParamEvent, kSavedParamCount * 3u> events {};
    for (uint32_t index = 0u; index < values.size(); ++index) {
        const auto& def = kParamDefs[index];
        values[index] = clampValue(def, sourceValues[index]);
        const uint32_t eventIndex = index * 3u;
        events[eventIndex] = { Kind::GestureBegin, def.id, 0.0 };
        events[eventIndex + 1u] = { Kind::Value, def.id, values[index] };
        events[eventIndex + 2u] = { Kind::GestureEnd, def.id, 0.0 };
    }
    if (!p.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t index = 0u; index < values.size(); ++index) {
        publishParam(p, kParamDefs[index].id, values[index]);
    }
    requestGuiParamService(p);
    return true;
}

bool queueGuiTrigger(Plugin& p, uint32_t code)
{
    if (code < 1u || code > 2u) return false;
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 4u> events {{
        { Kind::GestureBegin, kTriggerParamId, 0.0 },
        { Kind::Value, kTriggerParamId, static_cast<double>(code) },
        { Kind::Value, kTriggerParamId, 0.0 },
        { Kind::GestureEnd, kTriggerParamId, 0.0 },
    }};
    if (p.guiParamEvents.pushBatch(events.data(), events.size())) {
        publishParam(p, kTriggerParamId, 0.0);
        requestGuiParamService(p);
        return true;
    }
    return false;
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return false;
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
        if (!pushGuiParamEvent(output, pending)) {
            // A null/bounded/rejecting sink does not acknowledge the event.
            // Leave it at the queue head and arrange a legal retry.
            requestGuiParamRetry(p);
            return;
        }
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(p, pending.paramId, pending.value, true);
        }
        p.guiParamEvents.pop();
    }
    p.guiRetryPending.store(false, std::memory_order_release);
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
    p->floorTom.prepare(p->sampleRate);
    p->floorTom.setParams(p->params);
    p->activeTailSamples.store(0u, std::memory_order_relaxed);
    p->active = false;
    (void)consumePendingState(*p);
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->activated.store(true, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->activated.store(false, std::memory_order_release);
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->floorTom.reset();
    p->pendingHeadTriggers.store(0u, std::memory_order_relaxed);
    p->pendingRimTriggers.store(0u, std::memory_order_relaxed);
    p->activeTailSamples.store(0u, std::memory_order_relaxed);
    p->triggerCode = 0u;
    publishParam(*p, kTriggerParamId, 0.0);
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->active = false;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    auto* p = self(plugin);
    if (!process) return CLAP_PROCESS_ERROR;
    // Age only tails that existed before this block. A trigger anywhere in the
    // current block receives its complete bound and is therefore conservative
    // even when the event occurs at the final sample.
    ageActiveTail(*p, process->frames_count);
    (void)consumePendingState(*p);
    serviceGuiParamEvents(*p, process->out_events);
    if (p->tailChangePending.exchange(false, std::memory_order_acq_rel)) {
        notifyTailChanged(*p);
    }
    uint32_t pending = p->pendingHeadTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerFloorTom(*p,
        s3g::DrumTomArticulation::Head, 1.0f, 43);
    pending = p->pendingRimTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerFloorTom(*p,
        s3g::DrumTomArticulation::RimStick, 1.0f, 43);

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
        p->floorTom.processFrame(left, right);
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
    p->active = p->floorTom.active();
    const float previous = p->visualActivity.load(std::memory_order_relaxed);
    p->visualActivity.store(std::max(blockPeak, previous * 0.82f),
        std::memory_order_relaxed);
    releaseTransientTrigger(*p);
    return p->active ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->guiRetryPending.exchange(false, std::memory_order_acq_rel)) {
        requestGuiParamService(*p);
    }
}

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

struct FloorTomNoteName {
    int16_t key;
    const char* name;
};

constexpr std::array<FloorTomNoteName, 5u> kFloorTomNoteNames {{
    { 37, "Floor Tom Rim / Side Stick" },
    { 41, "Low Floor Tom Head" },
    { 43, "Floor Tom Head" },
    { 53, "Low Floor Tom Rim" },
    { 55, "Floor Tom Rim" },
}};

uint32_t noteNameCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kFloorTomNoteNames.size());
}

bool noteNameGet(const clap_plugin_t*, uint32_t index,
    clap_note_name_t* noteName)
{
    if (!noteName || index >= kFloorTomNoteNames.size()) return false;
    *noteName = {};
    noteName->port = 0;
    noteName->key = kFloorTomNoteNames[index].key;
    noteName->channel = -1;
    std::strncpy(noteName->name, kFloorTomNoteNames[index].name,
        sizeof(noteName->name) - 1u);
    return true;
}

const clap_plugin_note_name_t noteNameExt {
    noteNameCount, noteNameGet
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
    } else if (id == kPitchSweepTimeParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kBodyDecayParamId || id == kRimDecayParamId) {
        std::snprintf(display, size, "%.3g s", value);
    } else if (id == kStickDecayParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kBiasParamId || id == kCharacterToneParamId) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
    } else if (id == kTriggerParamId) {
        const uint32_t code = static_cast<uint32_t>(std::round(value));
        std::snprintf(display, size, "%s",
            code == 2u ? "Rim" : (code == 1u ? "Head" : "Ready"));
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
    if (id == kTriggerParamId) {
        if (std::strcmp(display, "Head") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "Rim") == 0) {
            *value = 2.0;
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
        && id != kBodyDecayParamId && id != kRimDecayParamId
        && id != kStickDecayParamId
        && id != kOutputParamId
        && id != kTriggerParamId;
    const char* expectedSuffix = id == kTuneParamId ? "Hz"
        : (id == kPitchDropParamId ? "st"
        : (id == kPitchSweepTimeParamId ? "ms"
        : ((id == kBodyDecayParamId || id == kRimDecayParamId) ? "s"
        : (id == kStickDecayParamId ? "ms"
        : (id == kOutputParamId ? "dB" : nullptr)))));
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
    if (!s3g::clap_state::readAll(stream, &state, sizeof(state))) return false;
    if (state.header.magic != kStateMagic
        || state.header.version != kStateVersion
        || state.header.valueCount != kSavedParamCount) return false;
    auto* p = self(plugin);
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        state.values[index] = clampValue(kParamDefs[index],
            state.values[index]);
    }
    if (p->preserveOutputOnNextStateLoad.exchange(
            false, std::memory_order_acq_rel)) {
        const uint32_t outputIndex = kOutputParamId - kTuneParamId;
        state.values[outputIndex] = clampValue(kParamDefs[outputIndex],
            p->preservedOutputValue.load(std::memory_order_acquire));
    }

    // Publish host-visible values immediately, but never mutate the running
    // synth from this main-thread state callback. process() consumes the whole
    // snapshot at its next block boundary; activate() does the same while
    // inactive before the first process call.
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        publishParam(*p, kParamDefs[index].id, state.values[index]);
    }
    stageSavedState(*p, state.values);
    // With no realtime consumer, apply synchronously so a later legal
    // inactive params.flush() remains later in callback order. Leaving this
    // snapshot pending until activate() would incorrectly overwrite that edit.
    if (!p->activated.load(std::memory_order_acquire)) {
        (void)consumePendingState(*p);
    }
    p->pendingHeadTriggers.store(0u, std::memory_order_relaxed);
    p->pendingRimTriggers.store(0u, std::memory_order_relaxed);
    publishParam(*p, kTriggerParamId, 0.0);
    p->parameterRevision.fetch_add(1u, std::memory_order_release);
    p->tailChangePending.store(true, std::memory_order_release);
    if (p->host && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
    if (p->host && p->host->request_process) {
        p->host->request_process(p->host);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const double seconds = s3g::drumFloorTomTailSeconds(
        publishedParamsSnapshot(*p), p->sampleRate);
    const uint64_t prospective = static_cast<uint64_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max() - 1u,
        std::ceil(seconds * p->sampleRate)));
    const uint64_t active = p->activeTailSamples.load(
        std::memory_order_acquire);
    return static_cast<uint32_t>(std::max(prospective, active));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct FloorTomUiRow {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

constexpr CGFloat kLeftPanelX = 16.0;
constexpr CGFloat kRightPanelX = 470.0;
constexpr CGFloat kPanelWidth = 434.0;

constexpr std::array<FloorTomUiRow, kSavedParamCount> kUiRows {{
    { kTuneParamId, "TUNE", kLeftPanelX, kPanelWidth, 80.0 },
    { kNoteTrackingParamId, "NOTE TRACK", kLeftPanelX, kPanelWidth, 104.0 },
    { kPitchDropParamId, "PITCH DROP", kLeftPanelX, kPanelWidth, 128.0 },
    { kPitchSweepTimeParamId, "SWEEP TIME", kLeftPanelX, kPanelWidth, 152.0 },
    { kShellSpreadParamId, "SHELL SPREAD", kLeftPanelX, kPanelWidth, 176.0 },
    { kBodyParamId, "BODY", kLeftPanelX, kPanelWidth, 200.0 },
    { kRingParamId, "RING", kLeftPanelX, kPanelWidth, 224.0 },
    { kBodyDecayParamId, "BODY DECAY", kLeftPanelX, kPanelWidth, 248.0 },

    { kPunchParamId, "PUNCH", kRightPanelX, kPanelWidth, 80.0 },
    { kDampingParamId, "DAMPING", kRightPanelX, kPanelWidth, 104.0 },
    { kRimLevelParamId, "RIM LEVEL", kRightPanelX, kPanelWidth, 128.0 },
    { kRimCharacterParamId, "RIM CHARACTER", kRightPanelX, kPanelWidth, 152.0 },
    { kRimDecayParamId, "RIM DECAY", kRightPanelX, kPanelWidth, 176.0 },
    { kStickLevelParamId, "STICK LEVEL", kRightPanelX, kPanelWidth, 200.0 },
    { kStickToneParamId, "STICK TONE", kRightPanelX, kPanelWidth, 224.0 },
    { kStickDecayParamId, "STICK DECAY", kRightPanelX, kPanelWidth, 248.0 },

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
}};

bool isLogUiParam(clap_id id)
{
    return id == kTuneParamId || id == kPitchSweepTimeParamId
        || id == kBodyDecayParamId || id == kRimDecayParamId
        || id == kStickDecayParamId;
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

constexpr std::array<clap_id, 23u> kSafeRandomParamIds {{
    kTuneParamId,
    kPitchDropParamId,
    kPitchSweepTimeParamId,
    kShellSpreadParamId,
    kBodyParamId,
    kRingParamId,
    kBodyDecayParamId,
    kPunchParamId,
    kDampingParamId,
    kRimLevelParamId,
    kRimCharacterParamId,
    kRimDecayParamId,
    kStickLevelParamId,
    kStickToneParamId,
    kStickDecayParamId,
    kDriveParamId,
    kBiasParamId,
    kCompressionParamId,
    kRateReductionParamId,
    kBitDepthParamId,
    kReconstructionParamId,
    kCharacterToneParamId,
    kStereoWidthParamId,
}};

double safeRandomParamValue(
    const s3g::DrumFloorTomParams& params, clap_id id)
{
    switch (id) {
    case kTuneParamId: return params.tuneHz;
    case kPitchDropParamId: return params.pitchDropSemitones;
    case kPitchSweepTimeParamId: return params.pitchSweepMs;
    case kShellSpreadParamId: return params.shellSpread;
    case kBodyParamId: return params.body;
    case kRingParamId: return params.ring;
    case kBodyDecayParamId: return params.bodyDecaySeconds;
    case kPunchParamId: return params.punch;
    case kDampingParamId: return params.damping;
    case kRimLevelParamId: return params.rimLevel;
    case kRimCharacterParamId: return params.rimCharacter;
    case kRimDecayParamId: return params.rimDecaySeconds;
    case kStickLevelParamId: return params.stickLevel;
    case kStickToneParamId: return params.stickTone;
    case kStickDecayParamId: return params.stickDecayMs;
    case kDriveParamId: return params.character.drive;
    case kBiasParamId: return params.character.bias;
    case kCompressionParamId: return params.character.compression;
    case kRateReductionParamId:
        return params.character.sampleRateReduction;
    case kBitDepthParamId: return params.character.bitDepthReduction;
    case kReconstructionParamId: return params.character.reconstruction;
    case kCharacterToneParamId: return params.character.tone;
    case kStereoWidthParamId: return params.stereoWidth;
    default: return 0.0;
    }
}

bool queueGuiSafeRandomParamSet(Plugin& p,
    const s3g::DrumFloorTomParams& sourceParams)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    std::array<double, kSafeRandomParamIds.size()> values {};
    std::array<s3g::clap_gui::ParamEvent,
        kSafeRandomParamIds.size() * 3u> events {};
    for (uint32_t index = 0u; index < kSafeRandomParamIds.size(); ++index) {
        const clap_id id = kSafeRandomParamIds[index];
        const auto* def = paramDef(id);
        if (!def) return false;
        values[index] = clampValue(*def,
            safeRandomParamValue(sourceParams, id));
        const uint32_t eventIndex = index * 3u;
        events[eventIndex] = { Kind::GestureBegin, id, 0.0 };
        events[eventIndex + 1u] = { Kind::Value, id, values[index] };
        events[eventIndex + 2u] = { Kind::GestureEnd, id, 0.0 };
    }

    // One queue publication makes RANDOM all-or-nothing to both the host and
    // the realtime consumer.  In particular there is no event or publication
    // for note tracking, velocity response, output trim, or Trigger.
    if (!p.guiParamEvents.pushBatch(events.data(), events.size())) {
        return false;
    }
    for (uint32_t index = 0u; index < kSafeRandomParamIds.size(); ++index) {
        publishParam(p, kSafeRandomParamIds[index], values[index]);
    }
    requestGuiParamService(p);
    return true;
}

} // namespace

@interface S3GDrumFloorTomView : NSView {
    void* _plugin;
    int _dragParam;
    int _factoryPresetIndex;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    uint64_t _observedParamRevision;
    uint32_t _randomState;
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
@end

@implementation S3GDrumFloorTomView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        auto* p = static_cast<Plugin*>(plugin);
        _factoryPresetIndex = p
            ? s3g::drumFloorTomFactoryPresetIndex(publishedParamsSnapshot(*p))
            : 0;
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _observedParamRevision = p
            ? p->parameterRevision.load(std::memory_order_acquire) : 0u;
        _randomState = arc4random();
        if (_randomState == 0u) _randomState = 1u;
        _timer = nil;
        if (_factoryPresetIndex >= 0) {
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                s3g::drumFloorTomFactoryPresetInfo(
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
    if (!p || s3g::kDrumFloorTomFactoryPresetCount == 0u) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kDrumFloorTomFactoryPresetCount - 1u));
    const auto preset = s3g::drumFloorTomFactoryPreset(
        static_cast<uint32_t>(index));
    std::array<double, kSavedParamCount> values {};
    const auto set = [&](clap_id id, double value) {
        values[id - kTuneParamId] = value;
    };
    set(kTuneParamId, preset.tuneHz);
    set(kNoteTrackingParamId, preset.noteTracking);
    set(kPitchDropParamId, preset.pitchDropSemitones);
    set(kPitchSweepTimeParamId, preset.pitchSweepMs);
    set(kShellSpreadParamId, preset.shellSpread);
    set(kBodyParamId, preset.body);
    set(kRingParamId, preset.ring);
    set(kBodyDecayParamId, preset.bodyDecaySeconds);
    set(kPunchParamId, preset.punch);
    set(kDampingParamId, preset.damping);
    set(kRimLevelParamId, preset.rimLevel);
    set(kRimCharacterParamId, preset.rimCharacter);
    set(kRimDecayParamId, preset.rimDecaySeconds);
    set(kStickLevelParamId, preset.stickLevel);
    set(kStickToneParamId, preset.stickTone);
    set(kStickDecayParamId, preset.stickDecayMs);
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
    if (!queueGuiParamSet(*p, values)) {
        NSBeep();
        return;
    }
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        s3g::drumFloorTomFactoryPresetInfo(static_cast<uint32_t>(index)).name);
    [self setNeedsDisplay:YES];
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
            s3g::drumFloorTomFactoryPresetInfo(index).name];
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
            _factoryPresetIndex = s3g::drumFloorTomFactoryPresetIndex(
                publishedParamsSnapshot(*p));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::drumFloorTomFactoryPresetInfo(static_cast<uint32_t>(
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
        @"s3g DRUM FLOOR TOM",
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
    drawPanel(@"HEAD / SHELL", voicePanel);
    drawPanel(@"RIM / STICK", attackPanel);
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

    const CGFloat padWidth = (kPanelWidth - 30.0) * 0.5;
    const NSRect headPad = NSMakeRect(kRightPanelX + 12.0, 456.0,
        padWidth, 24.0);
    const NSRect rimPad = NSMakeRect(NSMaxX(headPad) + 6.0, 456.0,
        padWidth, 24.0);
    s3g::clap_gui::drawHeaderButton(headPad, outputPanel, @"HEAD",
        activity > 0.02f, valueAttrs, style);
    s3g::clap_gui::drawHeaderButton(rimPad, outputPanel, @"RIM",
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
    [[NSColor colorWithCalibratedRed:0.72 green:0.86 blue:0.82
        alpha:0.45 + activity * 0.45] setStroke];
    for (uint32_t wire = 0u; wire < 7u; ++wire) {
        const CGFloat y = center.y - 21.0 + wire * 7.0;
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:NSMakePoint(center.x - 57.0, y)];
        [path curveToPoint:NSMakePoint(center.x + 57.0, y)
            controlPoint1:NSMakePoint(center.x - 18.0,
                y + activity * 5.0)
            controlPoint2:NSMakePoint(center.x + 18.0,
                y - activity * 5.0)];
        [path setLineWidth:0.8 + activity * 0.8];
        [path stroke];
    }
    [@"SYNTHESIZED HEAD  //  RIM + STICK"
        drawAtPoint:NSMakePoint(kRightPanelX + 78.0, 646.0)
        withAttributes:labelAttrs];
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const auto id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const FloorTomUiRow& candidate) { return candidate.id == id; });
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
            s3g::kDrumFloorTomFactoryPresetCount, 64u);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        p->preservedOutputValue.store(paramValue(*p, kOutputParamId),
            std::memory_order_release);
        p->preserveOutputOnNextStateLoad.store(
            true, std::memory_order_release);
        const bool loaded = s3g::clap_gui::loadPluginStatePreset(
            &p->plugin, @"Drum Floor Tom", &name);
        // A cancelled/invalid load never reaches stateLoad(), so disarm the
        // one-shot here. A successful load already consumed it atomically.
        p->preserveOutputOnNextStateLoad.store(
            false, std::memory_order_release);
        if (loaded) {
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
                &p->plugin, @"Drum Floor Tom", &name)) {
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
        const auto randomized = s3g::drumFloorTomSafeRandomParams(
            publishedParamsSnapshot(*p), _randomState);
        if (!queueGuiSafeRandomParamSet(*p, randomized)) {
            NSBeep();
            return;
        }
        [self markCustomPreset];
        [self setNeedsDisplay:YES];
        return;
    }
    const CGFloat padWidth = (kPanelWidth - 30.0) * 0.5;
    const NSRect headPad = NSMakeRect(kRightPanelX + 12.0, 456.0,
        padWidth, 24.0);
    const NSRect rimPad = NSMakeRect(NSMaxX(headPad) + 6.0, 456.0,
        padWidth, 24.0);
    if (NSPointInRect(point, headPad) || NSPointInRect(point, rimPad)) {
        const uint32_t code = NSPointInRect(point, rimPad) ? 2u : 1u;
        if (!queueGuiTrigger(*p, code)) NSBeep();
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
    p->guiView = [[S3GDrumFloorTomView alloc] initWithPlugin:p];
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
    [static_cast<S3GDrumFloorTomView*>(p->guiView) stopRefreshTimer];
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
    [static_cast<S3GDrumFloorTomView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GDrumFloorTomView*>(p->guiView) stopRefreshTimer];
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
    if (std::strcmp(id, CLAP_EXT_NOTE_NAME) == 0) return &noteNameExt;
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
    "org.s3g.s3g-dsp.drum-floor-tom",
    "s3g Drum Floor Tom",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "A procedural mono-compatible stereo floor tom synthesizer with head, shell, rim, stick, and reusable drum character stages.",
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
