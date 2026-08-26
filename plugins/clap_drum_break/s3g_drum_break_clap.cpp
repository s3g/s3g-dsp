#include "s3g_drum_break.h"
#include "s3g_drum_break_presets.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_drum_midi_receive.h"

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

constexpr uint32_t kStateMagic = 0x42473353u; // "S3GB" in little endian.
constexpr uint32_t kStateVersion = 3u;
constexpr uint32_t kStateVersionTwo = 2u;
constexpr uint32_t kStateVersionOne = 1u;
constexpr uint32_t kOutputChannels = 2u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 760u;

constexpr clap_id kLowTuneParamId = 1u;
constexpr clap_id kNoteTrackingParamId = 2u;
constexpr clap_id kLowDropParamId = 3u;
constexpr clap_id kLowDecayParamId = 4u;
constexpr clap_id kLowWeightParamId = 5u;
constexpr clap_id kMidTuneParamId = 6u;
constexpr clap_id kMidBodyParamId = 7u;
constexpr clap_id kMidCrackParamId = 8u;
constexpr clap_id kMidDecayParamId = 9u;
constexpr clap_id kHighToneParamId = 10u;
constexpr clap_id kHighTextureParamId = 11u;
constexpr clap_id kHighDecayParamId = 12u;
constexpr clap_id kTransientParamId = 13u;
constexpr clap_id kBleedParamId = 14u;
constexpr clap_id kRoomParamId = 15u;
constexpr clap_id kAgeParamId = 16u;
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
constexpr clap_id kTomTuneParamId = 29u;
constexpr clap_id kTomDecayParamId = 30u;
constexpr clap_id kKickLevelParamId = 31u;
constexpr clap_id kKickBandParamId = 32u;
constexpr clap_id kSnareLevelParamId = 33u;
constexpr clap_id kSnareBandParamId = 34u;
constexpr clap_id kTomLevelParamId = 35u;
constexpr clap_id kTomBandParamId = 36u;
constexpr clap_id kHiHatLevelParamId = 37u;
constexpr clap_id kHiHatBandParamId = 38u;
constexpr uint32_t kParamCount = 38u;
constexpr uint32_t kSavedParamCount = kParamCount - 1u;
constexpr uint32_t kVersionTwoSavedParamCount = 31u;
constexpr uint32_t kVersionOneSavedParamCount = 27u;

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
    { kLowTuneParamId, "Kick Tune", "Kick", 28.0, 96.0, 52.0, false },
    { kNoteTrackingParamId, "Note Tracking", "Voices / MIDI", 0.0, 1.0, 0.20, false },
    { kLowDropParamId, "Kick Drop", "Kick", 0.0, 42.0, 14.0, false },
    { kLowDecayParamId, "Kick Decay", "Kick", 0.05, 2.0, 0.42, false },
    { kLowWeightParamId, "Kick Weight", "Kick", 0.0, 1.0, 0.78, false },
    { kMidTuneParamId, "Snare Tune", "Snare", 90.0, 380.0, 175.0, false },
    { kMidBodyParamId, "Snare Body", "Snare", 0.0, 1.0, 0.56, false },
    { kMidCrackParamId, "Snare Wire", "Snare", 0.0, 1.0, 0.72, false },
    { kMidDecayParamId, "Snare Decay", "Snare", 0.04, 1.8, 0.32, false },
    { kHighToneParamId, "Hat Tone", "Hi-Hat", 0.0, 1.0, 0.56, false },
    { kHighTextureParamId, "Hat Texture", "Hi-Hat", 0.0, 1.0, 0.66, false },
    { kHighDecayParamId, "Hat Decay", "Hi-Hat", 0.018, 1.2, 0.12, false },
    { kTransientParamId, "Transient", "Cohesion", 0.0, 1.0, 0.62, false },
    { kBleedParamId, "Bleed", "Cohesion", 0.0, 1.0, 0.20, false },
    { kRoomParamId, "Room", "Cohesion", 0.0, 1.0, 0.28, false },
    { kAgeParamId, "Age", "Cohesion", 0.0, 1.0, 0.25, false },
    { kDriveParamId, "Drive", "Character", 0.0, 1.0, 0.0, false },
    { kBiasParamId, "Bias", "Character", -1.0, 1.0, 0.0, false },
    { kCompressionParamId, "Compression", "Character", 0.0, 1.0, 0.0, false },
    { kRateReductionParamId, "Rate Reduction", "Character / Sampler", 0.0, 1.0, 0.0, false },
    { kBitDepthParamId, "Bit Depth Reduction", "Character / Sampler", 0.0, 1.0, 0.0, false },
    { kReconstructionParamId, "Reconstruction", "Character / Sampler", 0.0, 1.0, 0.0, false },
    { kCharacterToneParamId, "Character Tone", "Character", -1.0, 1.0, 0.0, false },
    { kStereoWidthParamId, "Stereo Width", "Output", 0.0, 1.0, 0.38, false },
    { kVelocityParamId, "Velocity Sensitivity", "MIDI", 0.0, 1.0, 0.90, false },
    { kOutputParamId, "Output Gain", "Output", -36.0, 12.0, -8.0, false },
    { kMidiReceiveParamId, "MIDI Receive", "MIDI / Routing", 0.0, 16.0, 0.0, true },
    { kTomTuneParamId, "Tom Tune", "Tom", 58.0, 260.0, 118.0, false },
    { kTomDecayParamId, "Tom Decay", "Tom", 0.08, 2.4, 0.52, false },
    { kKickLevelParamId, "Kick Level", "Kick", -24.0, 12.0, 0.0, false },
    { kKickBandParamId, "Kick Band", "Kick", 55.0, 900.0, 140.0, false },
    { kSnareLevelParamId, "Snare Level", "Snare", -24.0, 12.0, 0.0, false },
    { kSnareBandParamId, "Snare Band", "Snare", 300.0, 6000.0, 1800.0, false },
    { kTomLevelParamId, "Tom Level", "Tom", -24.0, 12.0, 0.0, false },
    { kTomBandParamId, "Tom Band", "Tom", 80.0, 1800.0, 320.0, false },
    { kHiHatLevelParamId, "Hi-Hat Level", "Hi-Hat", -24.0, 12.0, 0.0, false },
    { kHiHatBandParamId, "Hi-Hat Band", "Hi-Hat", 2200.0, 14000.0, 8200.0, false },
    { kTriggerParamId, "Trigger", "Performance", 0.0, 4.0, 0.0, true },
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
    s3g::DrumBreakParams params {};
    s3g::DrumBreak drumBreak;
    double midiReceive = 0.0;
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    std::array<std::atomic<double>, kSavedParamCount> pendingStateValues {};
    std::atomic<uint64_t> pendingStateSequence { 0u };
    uint64_t consumedStateSequence = 0u;
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<uint32_t> pendingKickTriggers { 0u };
    std::atomic<uint32_t> pendingSnareTriggers { 0u };
    std::atomic<uint32_t> pendingTomTriggers { 0u };
    std::atomic<uint32_t> pendingHiHatTriggers { 0u };
    std::atomic<uint64_t> activeTailSamples { 0u };
    std::atomic<bool> tailChangePending { false };
    std::atomic<bool> guiRetryPending { false };
    std::atomic<bool> activated { false };
    std::atomic<bool> preserveOutputOnNextStateLoad { false };
    std::atomic<double> preservedOutputValue { -8.0 };
    std::atomic<uint64_t> parameterRevision { 0u };
    std::atomic<float> visualActivity { 0.0f };
    std::atomic<uint32_t> visualTriggerCode { 0u };
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
    if (id < kLowTuneParamId || id > kHiHatBandParamId) return;
    p.publishedParams[id - kLowTuneParamId].store(
        value, std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    if (id < kLowTuneParamId || id > kHiHatBandParamId) return 0.0;
    return p.publishedParams[id - kLowTuneParamId].load(
        std::memory_order_acquire);
}

s3g::DrumBreakParams publishedParamsSnapshot(const Plugin& p)
{
    s3g::DrumBreakParams params;
    params.lowTuneHz = static_cast<float>(paramValue(p, kLowTuneParamId));
    params.noteTracking = static_cast<float>(
        paramValue(p, kNoteTrackingParamId));
    params.lowDropSemitones = static_cast<float>(paramValue(p, kLowDropParamId));
    params.lowDecaySeconds = static_cast<float>(paramValue(p, kLowDecayParamId));
    params.lowWeight = static_cast<float>(paramValue(p, kLowWeightParamId));
    params.kickLevelDb = static_cast<float>(
        paramValue(p, kKickLevelParamId));
    params.kickBandHz = static_cast<float>(paramValue(p, kKickBandParamId));
    params.midTuneHz = static_cast<float>(paramValue(p, kMidTuneParamId));
    params.midBody = static_cast<float>(paramValue(p, kMidBodyParamId));
    params.midCrack = static_cast<float>(paramValue(p, kMidCrackParamId));
    params.midDecaySeconds = static_cast<float>(
        paramValue(p, kMidDecayParamId));
    params.snareLevelDb = static_cast<float>(
        paramValue(p, kSnareLevelParamId));
    params.snareBandHz = static_cast<float>(
        paramValue(p, kSnareBandParamId));
    params.highTone = static_cast<float>(
        paramValue(p, kHighToneParamId));
    params.highTexture = static_cast<float>(
        paramValue(p, kHighTextureParamId));
    params.highDecaySeconds = static_cast<float>(paramValue(p, kHighDecayParamId));
    params.hiHatLevelDb = static_cast<float>(
        paramValue(p, kHiHatLevelParamId));
    params.hiHatBandHz = static_cast<float>(
        paramValue(p, kHiHatBandParamId));
    params.tomTuneHz = static_cast<float>(paramValue(p, kTomTuneParamId));
    params.tomDecaySeconds = static_cast<float>(
        paramValue(p, kTomDecayParamId));
    params.tomLevelDb = static_cast<float>(paramValue(p, kTomLevelParamId));
    params.tomBandHz = static_cast<float>(paramValue(p, kTomBandParamId));
    params.transient = static_cast<float>(paramValue(p, kTransientParamId));
    params.bleed = static_cast<float>(paramValue(p, kBleedParamId));
    params.room = static_cast<float>(paramValue(p, kRoomParamId));
    params.age = static_cast<float>(
        paramValue(p, kAgeParamId));
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

uint64_t tailSamplesForParams(const s3g::DrumBreakParams& params,
    double sampleRate)
{
    const double seconds = std::max({
        s3g::drumBreakTailSeconds(params,
            s3g::DrumBreakArticulation::Kick, sampleRate),
        s3g::drumBreakTailSeconds(params,
            s3g::DrumBreakArticulation::Snare, sampleRate),
        s3g::drumBreakTailSeconds(params,
            s3g::DrumBreakArticulation::Tom, sampleRate),
        s3g::drumBreakTailSeconds(params,
            s3g::DrumBreakArticulation::HiHat, sampleRate),
    });
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

void triggerBreak(Plugin& p, s3g::DrumBreakArticulation articulation,
    float velocity, int midiNote)
{
    p.visualTriggerCode.store(static_cast<uint32_t>(articulation) + 1u,
        std::memory_order_relaxed);
    p.drumBreak.trigger(articulation, std::clamp(velocity, 0.0f, 1.0f),
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
    if (id == kMidiReceiveParamId) {
        p.midiReceive = value;
        publishParam(p, id, value);
        p.parameterRevision.fetch_add(1u, std::memory_order_release);
        return;
    }
    if (id == kTriggerParamId) {
        const uint32_t code = static_cast<uint32_t>(value);
        if (code > 0u && code != p.triggerCode) {
            const auto articulation = static_cast<
                s3g::DrumBreakArticulation>(code - 1u);
            if (triggerImmediately) {
                triggerBreak(p, articulation, 1.0f,
                    s3g::drumBreakCanonicalMidiNote(articulation));
            } else {
                switch (articulation) {
                case s3g::DrumBreakArticulation::Kick:
                    p.pendingKickTriggers.fetch_add(
                        1u, std::memory_order_relaxed);
                    break;
                case s3g::DrumBreakArticulation::Snare:
                    p.pendingSnareTriggers.fetch_add(
                        1u, std::memory_order_relaxed);
                    break;
                case s3g::DrumBreakArticulation::Tom:
                    p.pendingTomTriggers.fetch_add(
                        1u, std::memory_order_relaxed);
                    break;
                case s3g::DrumBreakArticulation::HiHat:
                    p.pendingHiHatTriggers.fetch_add(
                        1u, std::memory_order_relaxed);
                    break;
                }
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
    case kLowTuneParamId: p.params.lowTuneHz = v; break;
    case kNoteTrackingParamId: p.params.noteTracking = v; break;
    case kLowDropParamId: p.params.lowDropSemitones = v; break;
    case kLowDecayParamId:
        tailChanged = std::fabs(p.params.lowDecaySeconds - v) > 1.0e-6f;
        p.params.lowDecaySeconds = v;
        break;
    case kLowWeightParamId: p.params.lowWeight = v; break;
    case kKickLevelParamId: p.params.kickLevelDb = v; break;
    case kKickBandParamId: p.params.kickBandHz = v; break;
    case kMidTuneParamId: p.params.midTuneHz = v; break;
    case kMidBodyParamId: p.params.midBody = v; break;
    case kMidCrackParamId: p.params.midCrack = v; break;
    case kMidDecayParamId:
        tailChanged = std::fabs(
            p.params.midDecaySeconds - v) > 1.0e-6f;
        p.params.midDecaySeconds = v;
        break;
    case kSnareLevelParamId: p.params.snareLevelDb = v; break;
    case kSnareBandParamId: p.params.snareBandHz = v; break;
    case kHighToneParamId: p.params.highTone = v; break;
    case kHighTextureParamId: p.params.highTexture = v; break;
    case kHighDecayParamId:
        tailChanged = std::fabs(p.params.highDecaySeconds - v) > 1.0e-6f;
        p.params.highDecaySeconds = v;
        break;
    case kHiHatLevelParamId: p.params.hiHatLevelDb = v; break;
    case kHiHatBandParamId: p.params.hiHatBandHz = v; break;
    case kTomTuneParamId: p.params.tomTuneHz = v; break;
    case kTomDecayParamId:
        tailChanged = std::fabs(p.params.tomDecaySeconds - v) > 1.0e-6f;
        p.params.tomDecaySeconds = v;
        break;
    case kTomLevelParamId: p.params.tomLevelDb = v; break;
    case kTomBandParamId: p.params.tomBandHz = v; break;
    case kTransientParamId: p.params.transient = v; break;
    case kBleedParamId: p.params.bleed = v; break;
    case kRoomParamId:
        tailChanged = std::fabs(p.params.room - v) > 1.0e-6f;
        p.params.room = v;
        break;
    case kAgeParamId:
        tailChanged = std::fabs(p.params.age - v) > 1.0e-6f;
        p.params.age = v;
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
    p.drumBreak.setParams(p.params);
    p.params = p.drumBreak.params();
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
    case kLowTuneParamId: return p.params.lowTuneHz;
    case kNoteTrackingParamId: return p.params.noteTracking;
    case kLowDropParamId: return p.params.lowDropSemitones;
    case kLowDecayParamId: return p.params.lowDecaySeconds;
    case kLowWeightParamId: return p.params.lowWeight;
    case kKickLevelParamId: return p.params.kickLevelDb;
    case kKickBandParamId: return p.params.kickBandHz;
    case kMidTuneParamId: return p.params.midTuneHz;
    case kMidBodyParamId: return p.params.midBody;
    case kMidCrackParamId: return p.params.midCrack;
    case kMidDecayParamId: return p.params.midDecaySeconds;
    case kSnareLevelParamId: return p.params.snareLevelDb;
    case kSnareBandParamId: return p.params.snareBandHz;
    case kHighToneParamId: return p.params.highTone;
    case kHighTextureParamId: return p.params.highTexture;
    case kHighDecayParamId: return p.params.highDecaySeconds;
    case kHiHatLevelParamId: return p.params.hiHatLevelDb;
    case kHiHatBandParamId: return p.params.hiHatBandHz;
    case kTomTuneParamId: return p.params.tomTuneHz;
    case kTomDecayParamId: return p.params.tomDecaySeconds;
    case kTomLevelParamId: return p.params.tomLevelDb;
    case kTomBandParamId: return p.params.tomBandHz;
    case kTransientParamId: return p.params.transient;
    case kBleedParamId: return p.params.bleed;
    case kRoomParamId: return p.params.room;
    case kAgeParamId: return p.params.age;
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
    case kTriggerParamId: return static_cast<double>(p.triggerCode);
    default: return 0.0;
    }
}

int savedParamIndex(clap_id id)
{
    for (uint32_t index = 0u; index < kSavedParamCount; ++index) {
        if (kParamDefs[index].id == id) return static_cast<int>(index);
    }
    return -1;
}

void assignSavedStateValues(s3g::DrumBreakParams& params,
    const std::array<double, kSavedParamCount>& values)
{
    const auto value = [&](clap_id id) {
        const int index = savedParamIndex(id);
        return index >= 0 ? static_cast<float>(values[
            static_cast<uint32_t>(index)]) : 0.0f;
    };
    params.lowTuneHz = value(kLowTuneParamId);
    params.noteTracking = value(kNoteTrackingParamId);
    params.lowDropSemitones = value(kLowDropParamId);
    params.lowDecaySeconds = value(kLowDecayParamId);
    params.lowWeight = value(kLowWeightParamId);
    params.kickLevelDb = value(kKickLevelParamId);
    params.kickBandHz = value(kKickBandParamId);
    params.midTuneHz = value(kMidTuneParamId);
    params.midBody = value(kMidBodyParamId);
    params.midCrack = value(kMidCrackParamId);
    params.midDecaySeconds = value(kMidDecayParamId);
    params.snareLevelDb = value(kSnareLevelParamId);
    params.snareBandHz = value(kSnareBandParamId);
    params.highTone = value(kHighToneParamId);
    params.highTexture = value(kHighTextureParamId);
    params.highDecaySeconds = value(kHighDecayParamId);
    params.hiHatLevelDb = value(kHiHatLevelParamId);
    params.hiHatBandHz = value(kHiHatBandParamId);
    params.tomTuneHz = value(kTomTuneParamId);
    params.tomDecaySeconds = value(kTomDecayParamId);
    params.tomLevelDb = value(kTomLevelParamId);
    params.tomBandHz = value(kTomBandParamId);
    params.transient = value(kTransientParamId);
    params.bleed = value(kBleedParamId);
    params.room = value(kRoomParamId);
    params.age = value(kAgeParamId);
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

    s3g::DrumBreakParams next = p.params;
    assignSavedStateValues(next, values);
    p.drumBreak.setParams(next);
    p.params = p.drumBreak.params();
    const int midiIndex = savedParamIndex(kMidiReceiveParamId);
    p.midiReceive = midiIndex >= 0
        ? values[static_cast<uint32_t>(midiIndex)] : 0.0;
    p.pendingKickTriggers.store(0u, std::memory_order_relaxed);
    p.pendingSnareTriggers.store(0u, std::memory_order_relaxed);
    p.pendingTomTriggers.store(0u, std::memory_order_relaxed);
    p.pendingHiHatTriggers.store(0u, std::memory_order_relaxed);
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
        if (note->velocity > 0.0
            && s3g::drum_midi::accepts(p.midiReceive, note->channel)) {
            const auto articulation =
                s3g::drumBreakArticulationForMidiNote(note->key);
            triggerBreak(p, articulation,
                static_cast<float>(note->velocity), note->key);
        }
    } else if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if ((midi->data[0] & 0xf0u) == 0x90u && midi->data[2] > 0u
            && s3g::drum_midi::accepts(
                p.midiReceive, midi->data[0] & 0x0fu)) {
            const int note = midi->data[1];
            const auto articulation =
                s3g::drumBreakArticulationForMidiNote(note);
            triggerBreak(p, articulation,
                static_cast<float>(midi->data[2]) / 127.0f,
                note);
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
        values[index] = def.id == kMidiReceiveParamId
            ? paramValue(p, def.id)
            : clampValue(def, sourceValues[index]);
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
    if (code < 1u || code > 4u) return false;
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
    p->drumBreak.prepare(p->sampleRate);
    p->drumBreak.setParams(p->params);
    p->activeTailSamples.store(0u, std::memory_order_relaxed);
    p->active = false;
    (void)consumePendingState(*p);
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->visualTriggerCode.store(0u, std::memory_order_relaxed);
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
    p->drumBreak.reset();
    p->pendingKickTriggers.store(0u, std::memory_order_relaxed);
    p->pendingSnareTriggers.store(0u, std::memory_order_relaxed);
    p->pendingTomTriggers.store(0u, std::memory_order_relaxed);
    p->pendingHiHatTriggers.store(0u, std::memory_order_relaxed);
    p->activeTailSamples.store(0u, std::memory_order_relaxed);
    p->triggerCode = 0u;
    publishParam(*p, kTriggerParamId, 0.0);
    p->visualActivity.store(0.0f, std::memory_order_relaxed);
    p->visualTriggerCode.store(0u, std::memory_order_relaxed);
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
    uint32_t pending = p->pendingKickTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerBreak(*p,
        s3g::DrumBreakArticulation::Kick, 1.0f, 36);
    pending = p->pendingSnareTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerBreak(*p,
        s3g::DrumBreakArticulation::Snare, 1.0f, 38);
    pending = p->pendingTomTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerBreak(*p,
        s3g::DrumBreakArticulation::Tom, 1.0f, 45);
    pending = p->pendingHiHatTriggers.exchange(
        0u, std::memory_order_relaxed);
    while (pending-- > 0u) triggerBreak(*p,
        s3g::DrumBreakArticulation::HiHat, 1.0f, 42);

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
        p->drumBreak.processFrame(left, right);
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
    p->active = p->drumBreak.active();
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

struct BreakNoteName {
    int16_t key;
    const char* name;
};

constexpr std::array<BreakNoteName, 4u> kBreakNoteNames {{
    { 36, "Break Kick" },
    { 38, "Break Snare" },
    { 45, "Break Tom" },
    { 42, "Break Hi-Hat" },
}};

uint32_t noteNameCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kBreakNoteNames.size());
}

bool noteNameGet(const clap_plugin_t*, uint32_t index,
    clap_note_name_t* noteName)
{
    if (!noteName || index >= kBreakNoteNames.size()) return false;
    *noteName = {};
    noteName->port = 0;
    noteName->key = kBreakNoteNames[index].key;
    noteName->channel = -1;
    std::strncpy(noteName->name, kBreakNoteNames[index].name,
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
    if (id == kKickBandParamId || id == kSnareBandParamId
        || id == kTomBandParamId || id == kHiHatBandParamId) {
        std::snprintf(display, size, "%.0f Hz", value);
    } else if (id == kLowTuneParamId || id == kMidTuneParamId
        || id == kTomTuneParamId) {
        std::snprintf(display, size, "%.1f Hz", value);
    } else if (id == kLowDropParamId) {
        std::snprintf(display, size, "%.1f st", value);
    } else if (id == kLowDecayParamId || id == kMidDecayParamId
        || id == kHighDecayParamId || id == kTomDecayParamId) {
        std::snprintf(display, size, "%.3g s", value);
    } else if (id == kOutputParamId || id == kKickLevelParamId
        || id == kSnareLevelParamId || id == kTomLevelParamId
        || id == kHiHatLevelParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kBiasParamId || id == kCharacterToneParamId) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
    } else if (id == kMidiReceiveParamId) {
        s3g::drum_midi::valueToText(value, display, size);
    } else if (id == kTriggerParamId) {
        const uint32_t code = static_cast<uint32_t>(std::round(value));
        constexpr const char* names[] {
            "Ready", "Kick", "Snare", "Tom", "Hi-Hat"
        };
        std::snprintf(display, size, "%s", names[std::min(code, 4u)]);
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
        constexpr const char* names[] {
            "Ready", "Kick", "Snare", "Tom", "Hi-Hat"
        };
        for (uint32_t code = 0u; code < 5u; ++code) {
            if (std::strcmp(display, names[code]) == 0) {
                *value = static_cast<double>(code);
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
            static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }

    const bool percentParam = id != kLowTuneParamId
        && id != kMidTuneParamId && id != kTomTuneParamId
        && id != kKickBandParamId && id != kSnareBandParamId
        && id != kTomBandParamId && id != kHiHatBandParamId
        && id != kLowDropParamId
        && id != kLowDecayParamId && id != kMidDecayParamId
        && id != kHighDecayParamId && id != kTomDecayParamId
        && id != kKickLevelParamId && id != kSnareLevelParamId
        && id != kTomLevelParamId && id != kHiHatLevelParamId
        && id != kOutputParamId
        && id != kMidiReceiveParamId && id != kTriggerParamId;
    const char* expectedSuffix =
        (id == kLowTuneParamId || id == kMidTuneParamId
            || id == kTomTuneParamId || id == kKickBandParamId
            || id == kSnareBandParamId || id == kTomBandParamId
            || id == kHiHatBandParamId) ? "Hz"
        : (id == kLowDropParamId ? "st"
        : ((id == kLowDecayParamId || id == kMidDecayParamId
              || id == kHighDecayParamId || id == kTomDecayParamId) ? "s"
        : ((id == kOutputParamId || id == kKickLevelParamId
              || id == kSnareLevelParamId || id == kTomLevelParamId
              || id == kHiHatLevelParamId) ? "dB" : nullptr)));
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
    if (!s3g::clap_state::readAll(
            stream, &state.header, sizeof(state.header))) return false;
    const bool current = state.header.magic == kStateMagic
        && state.header.version == kStateVersion
        && state.header.valueCount == kSavedParamCount;
    const bool versionTwo = state.header.magic == kStateMagic
        && state.header.version == kStateVersionTwo
        && state.header.valueCount == kVersionTwoSavedParamCount;
    const bool versionOne = state.header.magic == kStateMagic
        && state.header.version == kStateVersionOne
        && state.header.valueCount == kVersionOneSavedParamCount;
    if (!current && !versionTwo && !versionOne) return false;
    for (uint32_t index = 0u; index < kSavedParamCount; ++index) {
        state.values[index] = kParamDefs[index].defaultValue;
    }
    if (!s3g::clap_state::readAll(stream, state.values.data(),
            static_cast<size_t>(state.header.valueCount)
                * sizeof(double))) return false;
    if (versionTwo) {
        // Version two stored cymbal tone/decay in the slots now assigned to
        // kick level/band. Never reinterpret those unrelated values.
        state.values[29u] = kParamDefs[29u].defaultValue;
        state.values[30u] = kParamDefs[30u].defaultValue;
    }
    auto* p = self(plugin);
    for (uint32_t index = 0u; index < state.values.size(); ++index) {
        state.values[index] = clampValue(kParamDefs[index],
            state.values[index]);
    }
    if (p->preserveOutputOnNextStateLoad.exchange(
            false, std::memory_order_acq_rel)) {
        const int outputIndex = savedParamIndex(kOutputParamId);
        if (outputIndex >= 0) {
            state.values[static_cast<uint32_t>(outputIndex)] = clampValue(
                kParamDefs[static_cast<uint32_t>(outputIndex)],
                p->preservedOutputValue.load(std::memory_order_acquire));
        }
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
    p->pendingKickTriggers.store(0u, std::memory_order_relaxed);
    p->pendingSnareTriggers.store(0u, std::memory_order_relaxed);
    p->pendingTomTriggers.store(0u, std::memory_order_relaxed);
    p->pendingHiHatTriggers.store(0u, std::memory_order_relaxed);
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
    const uint64_t prospective = tailSamplesForParams(
        publishedParamsSnapshot(*p), p->sampleRate);
    const uint64_t active = p->activeTailSamples.load(
        std::memory_order_acquire);
    return static_cast<uint32_t>(std::max(prospective, active));
}

const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

struct BreakUiRow {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat panelWidth;
    CGFloat y;
};

constexpr CGFloat kLeftPanelX = 16.0;
constexpr CGFloat kRightPanelX = 470.0;
constexpr CGFloat kPanelWidth = 434.0;

NSRect breakPerformancePadRect(uint32_t code)
{
    const CGFloat gap = 4.0;
    const CGFloat padWidth = (kPanelWidth - 24.0 - gap * 3.0) / 4.0;
    return NSMakeRect(kRightPanelX + 12.0
            + static_cast<CGFloat>(code - 1u) * (padWidth + gap),
        672.0, padWidth, 24.0);
}

constexpr std::array<BreakUiRow, kSavedParamCount> kUiRows {{
    { kLowTuneParamId, "KICK TUNE", kLeftPanelX, kPanelWidth, 80.0 },
    { kLowDropParamId, "KICK DROP", kLeftPanelX, kPanelWidth, 104.0 },
    { kLowDecayParamId, "KICK DECAY", kLeftPanelX, kPanelWidth, 128.0 },
    { kLowWeightParamId, "KICK WEIGHT", kLeftPanelX, kPanelWidth, 152.0 },
    { kKickLevelParamId, "KICK LEVEL", kLeftPanelX, kPanelWidth, 176.0 },
    { kKickBandParamId, "KICK BAND", kLeftPanelX, kPanelWidth, 200.0 },

    { kMidTuneParamId, "SNARE TUNE", kRightPanelX, kPanelWidth, 80.0 },
    { kMidBodyParamId, "SNARE BODY", kRightPanelX, kPanelWidth, 104.0 },
    { kMidCrackParamId, "SNARE WIRE", kRightPanelX, kPanelWidth, 128.0 },
    { kMidDecayParamId, "SNARE DECAY", kRightPanelX, kPanelWidth, 152.0 },
    { kSnareLevelParamId, "SNARE LEVEL", kRightPanelX, kPanelWidth, 176.0 },
    { kSnareBandParamId, "SNARE BAND", kRightPanelX, kPanelWidth, 200.0 },

    { kTomTuneParamId, "TOM TUNE", kLeftPanelX, kPanelWidth, 272.0 },
    { kTomDecayParamId, "TOM DECAY", kLeftPanelX, kPanelWidth, 296.0 },
    { kTomLevelParamId, "TOM LEVEL", kLeftPanelX, kPanelWidth, 320.0 },
    { kTomBandParamId, "TOM BAND", kLeftPanelX, kPanelWidth, 344.0 },

    { kHighToneParamId, "HAT TONE", kRightPanelX, kPanelWidth, 272.0 },
    { kHighTextureParamId, "HAT TEXTURE", kRightPanelX, kPanelWidth, 296.0 },
    { kHighDecayParamId, "HAT DECAY", kRightPanelX, kPanelWidth, 320.0 },
    { kHiHatLevelParamId, "HAT LEVEL", kRightPanelX, kPanelWidth, 344.0 },
    { kHiHatBandParamId, "HAT BAND", kRightPanelX, kPanelWidth, 368.0 },

    { kTransientParamId, "TRANSIENT", kLeftPanelX, kPanelWidth, 442.0 },
    { kBleedParamId, "BLEED", kLeftPanelX, kPanelWidth, 466.0 },
    { kRoomParamId, "ROOM", kRightPanelX, kPanelWidth, 442.0 },
    { kAgeParamId, "AGE", kRightPanelX, kPanelWidth, 466.0 },

    { kDriveParamId, "DRIVE", kLeftPanelX, kPanelWidth, 540.0 },
    { kBiasParamId, "BIAS", kLeftPanelX, kPanelWidth, 564.0 },
    { kCompressionParamId, "COMPRESSION", kLeftPanelX, kPanelWidth, 588.0 },
    { kRateReductionParamId, "RATE REDUCTION", kLeftPanelX, kPanelWidth, 612.0 },
    { kBitDepthParamId, "BIT REDUCTION", kLeftPanelX, kPanelWidth, 636.0 },
    { kReconstructionParamId, "RECONSTRUCTION", kLeftPanelX, kPanelWidth, 660.0 },
    { kCharacterToneParamId, "TONE", kLeftPanelX, kPanelWidth, 684.0 },

    { kStereoWidthParamId, "STEREO WIDTH", kRightPanelX, kPanelWidth, 540.0 },
    { kVelocityParamId, "VELOCITY", kRightPanelX, kPanelWidth, 564.0 },
    { kOutputParamId, "OUTPUT", kRightPanelX, kPanelWidth, 588.0 },
    { kMidiReceiveParamId, "MIDI RECEIVE", kRightPanelX, kPanelWidth, 612.0 },
    { kNoteTrackingParamId, "NOTE TRACK", kRightPanelX, kPanelWidth, 636.0 },
}};

bool isLogUiParam(clap_id id)
{
    return id == kLowTuneParamId || id == kLowDecayParamId
        || id == kMidTuneParamId || id == kMidDecayParamId
        || id == kHighDecayParamId || id == kTomTuneParamId
        || id == kTomDecayParamId || id == kKickBandParamId
        || id == kSnareBandParamId || id == kTomBandParamId
        || id == kHiHatBandParamId;
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

constexpr std::array<clap_id, 33u> kSafeRandomParamIds {{
    kLowTuneParamId,
    kLowDropParamId,
    kLowDecayParamId,
    kLowWeightParamId,
    kKickLevelParamId,
    kKickBandParamId,
    kMidTuneParamId,
    kMidBodyParamId,
    kMidCrackParamId,
    kMidDecayParamId,
    kSnareLevelParamId,
    kSnareBandParamId,
    kHighToneParamId,
    kHighTextureParamId,
    kHighDecayParamId,
    kHiHatLevelParamId,
    kHiHatBandParamId,
    kTomTuneParamId,
    kTomDecayParamId,
    kTomLevelParamId,
    kTomBandParamId,
    kTransientParamId,
    kBleedParamId,
    kRoomParamId,
    kAgeParamId,
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
    const s3g::DrumBreakParams& params, clap_id id)
{
    switch (id) {
    case kLowTuneParamId: return params.lowTuneHz;
    case kLowDropParamId: return params.lowDropSemitones;
    case kLowDecayParamId: return params.lowDecaySeconds;
    case kLowWeightParamId: return params.lowWeight;
    case kKickLevelParamId: return params.kickLevelDb;
    case kKickBandParamId: return params.kickBandHz;
    case kMidTuneParamId: return params.midTuneHz;
    case kMidBodyParamId: return params.midBody;
    case kMidCrackParamId: return params.midCrack;
    case kMidDecayParamId: return params.midDecaySeconds;
    case kSnareLevelParamId: return params.snareLevelDb;
    case kSnareBandParamId: return params.snareBandHz;
    case kHighToneParamId: return params.highTone;
    case kHighTextureParamId: return params.highTexture;
    case kHighDecayParamId: return params.highDecaySeconds;
    case kHiHatLevelParamId: return params.hiHatLevelDb;
    case kHiHatBandParamId: return params.hiHatBandHz;
    case kTomTuneParamId: return params.tomTuneHz;
    case kTomDecayParamId: return params.tomDecaySeconds;
    case kTomLevelParamId: return params.tomLevelDb;
    case kTomBandParamId: return params.tomBandHz;
    case kTransientParamId: return params.transient;
    case kBleedParamId: return params.bleed;
    case kRoomParamId: return params.room;
    case kAgeParamId: return params.age;
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
    const s3g::DrumBreakParams& sourceParams)
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

@interface S3GDrumBreakView : NSView {
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

@implementation S3GDrumBreakView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = -1;
        auto* p = static_cast<Plugin*>(plugin);
        _factoryPresetIndex = p
            ? s3g::drumBreakFactoryPresetIndex(publishedParamsSnapshot(*p))
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
                s3g::drumBreakFactoryPresetInfo(
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
    if (!p || s3g::kDrumBreakFactoryPresetCount == 0u) return;
    index = std::clamp(index, 0,
        static_cast<int>(s3g::kDrumBreakFactoryPresetCount - 1u));
    const auto preset = s3g::drumBreakFactoryPreset(
        static_cast<uint32_t>(index));
    std::array<double, kSavedParamCount> values {};
    const auto set = [&](clap_id id, double value) {
        const int valueIndex = savedParamIndex(id);
        if (valueIndex >= 0) {
            values[static_cast<uint32_t>(valueIndex)] = value;
        }
    };
    set(kLowTuneParamId, preset.lowTuneHz);
    set(kNoteTrackingParamId, preset.noteTracking);
    set(kLowDropParamId, preset.lowDropSemitones);
    set(kLowDecayParamId, preset.lowDecaySeconds);
    set(kLowWeightParamId, preset.lowWeight);
    set(kKickLevelParamId, preset.kickLevelDb);
    set(kKickBandParamId, preset.kickBandHz);
    set(kMidTuneParamId, preset.midTuneHz);
    set(kMidBodyParamId, preset.midBody);
    set(kMidCrackParamId, preset.midCrack);
    set(kMidDecayParamId, preset.midDecaySeconds);
    set(kSnareLevelParamId, preset.snareLevelDb);
    set(kSnareBandParamId, preset.snareBandHz);
    set(kHighToneParamId, preset.highTone);
    set(kHighTextureParamId, preset.highTexture);
    set(kHighDecayParamId, preset.highDecaySeconds);
    set(kHiHatLevelParamId, preset.hiHatLevelDb);
    set(kHiHatBandParamId, preset.hiHatBandHz);
    set(kTomTuneParamId, preset.tomTuneHz);
    set(kTomDecayParamId, preset.tomDecaySeconds);
    set(kTomLevelParamId, preset.tomLevelDb);
    set(kTomBandParamId, preset.tomBandHz);
    set(kTransientParamId, preset.transient);
    set(kBleedParamId, preset.bleed);
    set(kRoomParamId, preset.room);
    set(kAgeParamId, preset.age);
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
        s3g::drumBreakFactoryPresetInfo(static_cast<uint32_t>(index)).name);
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
            s3g::drumBreakFactoryPresetInfo(index).name];
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
            _factoryPresetIndex = s3g::drumBreakFactoryPresetIndex(
                publishedParamsSnapshot(*p));
            std::snprintf(_presetName, sizeof(_presetName), "%s",
                _factoryPresetIndex >= 0
                    ? s3g::drumBreakFactoryPresetInfo(static_cast<uint32_t>(
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
    const uint32_t visualTrigger = p->visualTriggerCode.load(
        std::memory_order_relaxed);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g DRUM BREAK",
        [NSString stringWithUTF8String:_presetName],
        [NSString stringWithFormat:@"LEVEL %3.0f%%", activity * 100.0f],
        titleBand, titleAttrs, labelAttrs, valueAttrs, style);

    const NSRect kickPanel = NSMakeRect(kLeftPanelX, 50.0,
        kPanelWidth, 176.0);
    const NSRect snarePanel = NSMakeRect(kRightPanelX, 50.0,
        kPanelWidth, 176.0);
    const NSRect tomPanel = NSMakeRect(kLeftPanelX, 242.0,
        kPanelWidth, 154.0);
    const NSRect hiHatPanel = NSMakeRect(kRightPanelX, 242.0,
        kPanelWidth, 154.0);
    const NSRect cohesionPanel = NSMakeRect(kLeftPanelX, 412.0,
        kPanelWidth * 2.0 + 20.0, 82.0);
    const NSRect characterPanel = NSMakeRect(kLeftPanelX, 510.0,
        kPanelWidth, 234.0);
    const NSRect outputPanel = NSMakeRect(kRightPanelX, 510.0,
        kPanelWidth, 234.0);
    const auto drawPanel = [&](NSString* name, NSRect rect) {
        s3g::clap_gui::drawPanelFrame(rect.origin.x, rect.origin.y,
            rect.size.width, rect.size.height, style);
        s3g::clap_gui::drawPanelHeader(name, true, rect.origin.x,
            rect.origin.y, rect.size.width,
            s3g::gui_layout::kStandardMetrics.headerHeight,
            labelAttrs, style);
    };
    drawPanel(@"KICK", kickPanel);
    drawPanel(@"SNARE", snarePanel);
    drawPanel(@"TOM", tomPanel);
    drawPanel(@"HI-HAT", hiHatPanel);
    drawPanel(@"COHESION", cohesionPanel);
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

    constexpr NSString* padLabels[] {
        @"KICK", @"SNARE", @"TOM", @"HI-HAT"
    };
    for (uint32_t code = 1u; code <= 4u; ++code) {
        s3g::clap_gui::drawHeaderButton(breakPerformancePadRect(code),
            outputPanel, padLabels[code - 1u],
            visualTrigger == code && activity > 0.02f,
            valueAttrs, style);
    }

    const CGFloat visualLeft = kRightPanelX + 58.0;
    const CGFloat visualWidth = 318.0;
    constexpr CGFloat cycles[] { 1.5, 4.5, 2.7, 11.0 };
    for (uint32_t band = 0u; band < 4u; ++band) {
        const CGFloat centerY = 710.0 + static_cast<CGFloat>(band) * 10.0;
        const bool struck = visualTrigger == band + 1u && activity > 0.02f;
        const CGFloat amplitude = 1.8 + static_cast<CGFloat>(band) * 0.35
            + (struck ? activity * 5.0 : activity);
        [[NSColor colorWithCalibratedRed:0.50 + (struck ? 0.22 : 0.0)
            green:0.72 + (struck ? 0.16 : 0.0)
            blue:0.70 + (struck ? 0.13 : 0.0)
            alpha:0.34 + activity * 0.46] setStroke];
        NSBezierPath* path = [NSBezierPath bezierPath];
        for (uint32_t point = 0u; point <= 48u; ++point) {
            const CGFloat phase = static_cast<CGFloat>(point) / 48.0;
            const CGFloat taper = std::sin(static_cast<CGFloat>(s3g::kPi)
                * phase);
            const NSPoint next = NSMakePoint(visualLeft + phase * visualWidth,
                centerY + std::sin(phase * cycles[band] * 2.0
                    * static_cast<CGFloat>(s3g::kPi)) * amplitude * taper);
            if (point == 0u) [path moveToPoint:next];
            else [path lineToPoint:next];
        }
        [path setLineWidth:band == 0u ? 2.2
            : (band == 2u ? 1.8 : (band == 1u ? 1.4 : 0.9))];
        [path stroke];
    }
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)updateDraggedParam:(NSPoint)point
{
    if (_dragParam <= 0) return;
    const auto id = static_cast<clap_id>(_dragParam);
    const auto row = std::find_if(kUiRows.begin(), kUiRows.end(),
        [=](const BreakUiRow& candidate) { return candidate.id == id; });
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
            s3g::kDrumBreakFactoryPresetCount, 64u);
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
            &p->plugin, @"Drum Break", &name);
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
                &p->plugin, @"Drum Break", &name)) {
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
        const auto randomized = s3g::drumBreakSafeRandomParams(
            publishedParamsSnapshot(*p), _randomState);
        _randomState = arc4random();
        if (_randomState == 0u) _randomState = 1u;
        if (!queueGuiSafeRandomParamSet(*p, randomized)) {
            NSBeep();
            return;
        }
        [self markCustomPreset];
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t code = 1u; code <= 4u; ++code) {
        if (NSPointInRect(point, breakPerformancePadRect(code))) {
            if (!queueGuiTrigger(*p, code)) NSBeep();
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
    p->guiView = [[S3GDrumBreakView alloc] initWithPlugin:p];
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
    [static_cast<S3GDrumBreakView*>(p->guiView) stopRefreshTimer];
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
    [static_cast<S3GDrumBreakView*>(p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GDrumBreakView*>(p->guiView) stopRefreshTimer];
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
    "org.s3g.s3g-dsp.drum-break",
    "s3g Drum Break 2",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.3.0",
    "A procedural stereo break kit with independently filtered and leveled kick, snare, tom and hi-hat synthesis voices.",
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
