#include "s3g_ambi_stochastic_encoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
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
#include <iterator>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiStochasticMaxChannels;
constexpr uint32_t kStateVersion = 3u;
constexpr uint32_t kGuiWidth = 1160u;
constexpr uint32_t kGuiHeight = 860u;
constexpr uint32_t kGuiWaveSamples = 256u;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kVoicesParamId = 2;
constexpr clap_id kModeParamId = 3;
constexpr clap_id kSystemParamId = 4;
constexpr clap_id kAmplitudeDistributionParamId = 5;
constexpr clap_id kBaseNoteParamId = 6;
constexpr clap_id kPitchSpreadParamId = 7;
constexpr clap_id kDetuneParamId = 8;
constexpr clap_id kBreakpointsParamId = 9;
constexpr clap_id kAmplitudeStepParamId = 10;
constexpr clap_id kTimeStepParamId = 11;
constexpr clap_id kInertiaParamId = 12;
constexpr clap_id kActivityParamId = 13;
constexpr clap_id kCouplingParamId = 14;
constexpr clap_id kMemoryParamId = 15;
constexpr clap_id kReactivityParamId = 16;
constexpr clap_id kAttackParamId = 17;
constexpr clap_id kDecayParamId = 18;
constexpr clap_id kSustainParamId = 19;
constexpr clap_id kReleaseParamId = 20;
constexpr clap_id kMotionParamId = 21;
constexpr clap_id kMotionRateParamId = 22;
constexpr clap_id kMotionAmountParamId = 23;
constexpr clap_id kMotionSpreadParamId = 24;
constexpr clap_id kAzimuthParamId = 25;
constexpr clap_id kElevationParamId = 26;
constexpr clap_id kDistanceParamId = 27;
constexpr clap_id kOutputParamId = 28;
constexpr clap_id kDurationDistributionParamId = 29;
constexpr clap_id kModelParamId = 30;
constexpr clap_id kDynamicsParamId = 31;
constexpr clap_id kDynamicsDriveParamId = 32;
constexpr clap_id kDynamicsBounceParamId = 33;
constexpr clap_id kDynamicsDragParamId = 34;
constexpr clap_id kDynamicsRadiusParamId = 35;
constexpr clap_id kSynthesisDepthParamId = 36;
constexpr clap_id kSpatialDepthParamId = 37;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiStochasticParams params {};
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    int32_t guiFieldPage = 0;
};

struct LegacyParamsV2 {
    uint32_t order = 3;
    uint32_t voices = 12;
    s3g::AmbiStochasticMode mode = s3g::AmbiStochasticMode::Free;
    s3g::AmbiStochasticSystem system = s3g::AmbiStochasticSystem::Network;
    s3g::AmbiStochasticModel model = s3g::AmbiStochasticModel::Delta;
    s3g::AmbiStochasticDistribution amplitudeDistribution = s3g::AmbiStochasticDistribution::Gaussian;
    s3g::AmbiStochasticDistribution durationDistribution = s3g::AmbiStochasticDistribution::Gaussian;
    float baseNote = 40.0f;
    float pitchSpreadSemitones = 19.0f;
    float detuneCents = 9.0f;
    uint32_t breakpoints = 12;
    float amplitudeStep = 0.34f;
    float timeStep = 0.28f;
    float inertia = 0.76f;
    float activity = 0.82f;
    float coupling = 0.58f;
    float memory = 0.74f;
    float reactivity = 0.64f;
    float attackMs = 80.0f;
    float decayMs = 480.0f;
    float sustain = 0.72f;
    float releaseMs = 1800.0f;
    s3g::AmbiStochasticMotion motion = s3g::AmbiStochasticMotion::Feedback;
    float motionRateHz = 0.028f;
    float motionAmount = 0.72f;
    float motionSpread = 0.82f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float outputGainDb = -24.0f;
};

struct SavedStateV2 {
    uint32_t version = 2u;
    LegacyParamsV2 params {};
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct LegacyParamsV1 {
    uint32_t order = 3;
    uint32_t voices = 12;
    s3g::AmbiStochasticMode mode = s3g::AmbiStochasticMode::Free;
    s3g::AmbiStochasticSystem system = s3g::AmbiStochasticSystem::Network;
    s3g::AmbiStochasticDistribution distribution = s3g::AmbiStochasticDistribution::Gaussian;
    float baseNote = 40.0f;
    float pitchSpreadSemitones = 19.0f;
    float detuneCents = 9.0f;
    uint32_t breakpoints = 12;
    float amplitudeStep = 0.34f;
    float timeStep = 0.28f;
    float inertia = 0.76f;
    float activity = 0.82f;
    float coupling = 0.58f;
    float memory = 0.74f;
    float reactivity = 0.64f;
    float attackMs = 80.0f;
    float decayMs = 480.0f;
    float sustain = 0.72f;
    float releaseMs = 1800.0f;
    s3g::AmbiStochasticMotion motion = s3g::AmbiStochasticMotion::Feedback;
    float motionRateHz = 0.028f;
    float motionAmount = 0.72f;
    float motionSpread = 0.82f;
    float centerAzimuthDeg = 0.0f;
    float centerElevationDeg = 0.0f;
    float centerDistance = 1.0f;
    float outputGainDb = -24.0f;
};

struct SavedStateV1 {
    uint32_t version = 1u;
    LegacyParamsV1 params {};
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::AmbiStochasticEncoder engine {};
    s3g::AmbiStochasticParams params {};
    std::array<std::vector<float>, kOutputChannels> scratch {};
    std::array<float*, kOutputChannels> scratchPointers {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<uint32_t> lastMidiNote { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* realtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
    std::atomic<uint32_t> guiSelectedVoice { 0u };
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiTopologyX {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiTopologyY {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiTopologyZ {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiEnergy {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticMaxVoices> guiNeighbor {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticMaxVoices> guiSecondaryNeighbor {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiKinetic {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiContact {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiCrowding {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiTension {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiNetworkPulse {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiBondStrength {};
    std::array<std::atomic<float>, kGuiWaveSamples> guiWaveform {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxBreakpoints> guiBreakpointPosition {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxBreakpoints> guiBreakpointAmplitude {};
    std::atomic<uint32_t> guiBreakpointCount { 0u };
    std::atomic<float> guiGlobalEnergy { 0.0f };
    std::atomic<float> guiGlobalKinetic { 0.0f };
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    int guiFieldPage = 0;
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: plugin.params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kVoicesParamId: plugin.params.voices = static_cast<uint32_t>(std::lround(value)); break;
    case kModeParamId: plugin.params.mode = static_cast<s3g::AmbiStochasticMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kSystemParamId: plugin.params.system = static_cast<s3g::AmbiStochasticSystem>(static_cast<uint32_t>(std::lround(value))); break;
    case kAmplitudeDistributionParamId: plugin.params.amplitudeDistribution = static_cast<s3g::AmbiStochasticDistribution>(static_cast<uint32_t>(std::lround(value))); break;
    case kDurationDistributionParamId: plugin.params.durationDistribution = static_cast<s3g::AmbiStochasticDistribution>(static_cast<uint32_t>(std::lround(value))); break;
    case kModelParamId: plugin.params.model = static_cast<s3g::AmbiStochasticModel>(static_cast<uint32_t>(std::lround(value))); break;
    case kBaseNoteParamId: plugin.params.baseNote = static_cast<float>(value); break;
    case kPitchSpreadParamId: plugin.params.pitchSpreadSemitones = static_cast<float>(value); break;
    case kDetuneParamId: plugin.params.detuneCents = static_cast<float>(value); break;
    case kBreakpointsParamId: plugin.params.breakpoints = static_cast<uint32_t>(std::lround(value)); break;
    case kAmplitudeStepParamId: plugin.params.amplitudeStep = static_cast<float>(value); break;
    case kTimeStepParamId: plugin.params.timeStep = static_cast<float>(value); break;
    case kInertiaParamId: plugin.params.inertia = static_cast<float>(value); break;
    case kActivityParamId: plugin.params.activity = static_cast<float>(value); break;
    case kCouplingParamId: plugin.params.coupling = static_cast<float>(value); break;
    case kMemoryParamId: plugin.params.memory = static_cast<float>(value); break;
    case kReactivityParamId: plugin.params.reactivity = static_cast<float>(value); break;
    case kAttackParamId: plugin.params.attackMs = static_cast<float>(value); break;
    case kDecayParamId: plugin.params.decayMs = static_cast<float>(value); break;
    case kSustainParamId: plugin.params.sustain = static_cast<float>(value); break;
    case kReleaseParamId: plugin.params.releaseMs = static_cast<float>(value); break;
    case kMotionParamId: plugin.params.motion = static_cast<s3g::AmbiStochasticMotion>(static_cast<uint32_t>(std::lround(value))); break;
    case kMotionRateParamId: plugin.params.motionRateHz = static_cast<float>(value); break;
    case kMotionAmountParamId: plugin.params.motionAmount = static_cast<float>(value); break;
    case kMotionSpreadParamId: plugin.params.motionSpread = static_cast<float>(value); break;
    case kAzimuthParamId: plugin.params.centerAzimuthDeg = static_cast<float>(value); break;
    case kElevationParamId: plugin.params.centerElevationDeg = static_cast<float>(value); break;
    case kDistanceParamId: plugin.params.centerDistance = static_cast<float>(value); break;
    case kOutputParamId: plugin.params.outputGainDb = static_cast<float>(value); break;
    case kDynamicsParamId: plugin.params.dynamics = static_cast<s3g::AmbiStochasticDynamics>(static_cast<uint32_t>(std::lround(value))); break;
    case kDynamicsDriveParamId: plugin.params.dynamicsDrive = static_cast<float>(value); break;
    case kDynamicsBounceParamId: plugin.params.dynamicsBounce = static_cast<float>(value); break;
    case kDynamicsDragParamId: plugin.params.dynamicsDrag = static_cast<float>(value); break;
    case kDynamicsRadiusParamId: plugin.params.dynamicsRadius = static_cast<float>(value); break;
    case kSynthesisDepthParamId: plugin.params.synthesisDepth = static_cast<float>(value); break;
    case kSpatialDepthParamId: plugin.params.spatialDepth = static_cast<float>(value); break;
    default: return;
    }
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
}

void readEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* parameter = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, parameter->param_id, parameter->value);
        } else if (event->type == CLAP_EVENT_NOTE_ON
            || event->type == CLAP_EVENT_NOTE_OFF
            || event->type == CLAP_EVENT_NOTE_CHOKE) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
            if (event->type == CLAP_EVENT_NOTE_ON && note->velocity > 0.0) {
                plugin.engine.noteOn(note->key, static_cast<float>(note->velocity));
                plugin.lastMidiNote.store(static_cast<uint32_t>(note->key), std::memory_order_relaxed);
            } else {
                plugin.engine.noteOff(note->key);
            }
        }
    }
}

#if defined(__APPLE__)
void publishGuiSnapshot(Plugin& plugin)
{
    if (!plugin.guiVisible.load(std::memory_order_relaxed)) return;
    const auto& points = plugin.engine.points();
    const auto& neighbors = plugin.engine.neighborIndices();
    const auto& secondaryNeighbors = plugin.engine.secondaryNeighborIndices();
    for (uint32_t i = 0; i < s3g::kAmbiStochasticMaxVoices; ++i) {
        plugin.guiAzimuth[i].store(points[i].azimuthDeg, std::memory_order_relaxed);
        plugin.guiElevation[i].store(points[i].elevationDeg, std::memory_order_relaxed);
        plugin.guiDistance[i].store(points[i].distance, std::memory_order_relaxed);
        const auto topology = plugin.engine.topologyPosition(i);
        plugin.guiTopologyX[i].store(topology.x, std::memory_order_relaxed);
        plugin.guiTopologyY[i].store(topology.y, std::memory_order_relaxed);
        plugin.guiTopologyZ[i].store(topology.z, std::memory_order_relaxed);
        plugin.guiEnergy[i].store(plugin.engine.voiceEnergy(i), std::memory_order_relaxed);
        plugin.guiNeighbor[i].store(neighbors[i], std::memory_order_relaxed);
        plugin.guiSecondaryNeighbor[i].store(secondaryNeighbors[i], std::memory_order_relaxed);
        plugin.guiKinetic[i].store(plugin.engine.voiceKinetic(i), std::memory_order_relaxed);
        plugin.guiContact[i].store(plugin.engine.voiceContact(i), std::memory_order_relaxed);
        plugin.guiCrowding[i].store(plugin.engine.voiceCrowding(i), std::memory_order_relaxed);
        plugin.guiTension[i].store(plugin.engine.voiceTension(i), std::memory_order_relaxed);
        plugin.guiNetworkPulse[i].store(plugin.engine.voiceNetworkPulse(i), std::memory_order_relaxed);
        plugin.guiBondStrength[i].store(plugin.engine.voiceBondStrength(i), std::memory_order_relaxed);
    }
    plugin.guiGlobalEnergy.store(plugin.engine.globalEnergy(), std::memory_order_relaxed);
    plugin.guiGlobalKinetic.store(plugin.engine.globalKinetic(), std::memory_order_relaxed);
    const uint32_t selected = std::min<uint32_t>(
        plugin.guiSelectedVoice.load(std::memory_order_relaxed),
        std::max<uint32_t>(1u, plugin.params.voices) - 1u);
    const auto& waveform = plugin.engine.waveform(selected);
    for (uint32_t sample = 0; sample < kGuiWaveSamples; ++sample) {
        plugin.guiWaveform[sample].store(waveform[sample * 2u], std::memory_order_relaxed);
    }
    const uint32_t breakpointCount = plugin.engine.breakpointCount(selected);
    float durationTotal = 0.0f;
    for (uint32_t point = 0; point < breakpointCount; ++point) {
        durationTotal += plugin.engine.breakpointDuration(selected, point);
    }
    float position = 0.0f;
    for (uint32_t point = 0; point < breakpointCount; ++point) {
        plugin.guiBreakpointPosition[point].store(position / std::max(0.000001f, durationTotal), std::memory_order_relaxed);
        plugin.guiBreakpointAmplitude[point].store(plugin.engine.breakpointAmplitude(selected, point), std::memory_order_relaxed);
        position += plugin.engine.breakpointDuration(selected, point);
    }
    plugin.guiBreakpointCount.store(breakpointCount, std::memory_order_release);
}

void guiDestroy(const clap_plugin_t* plugin);
#endif

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
#if defined(__APPLE__)
    guiDestroy(plugin);
    s3g::clap_support::endRealtimeActivity(state->realtimeActivity);
#endif
    delete state;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* state = self(plugin);
    state->sampleRate = sampleRate;
    state->maxFrames = std::max<uint32_t>(1u, maxFrames);
    for (uint32_t ch = 0; ch < kOutputChannels; ++ch) {
        state->scratch[ch].assign(state->maxFrames, 0.0f);
        state->scratchPointers[ch] = state->scratch[ch].data();
    }
    state->engine.prepare(sampleRate);
    state->engine.setParams(state->params);
    return true;
}

void deactivate(const clap_plugin_t*) {}

bool startProcessing(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(self(plugin)->realtimeActivity);
#else
    (void)plugin;
#endif
    return true;
}

void stopProcessing(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->realtimeActivity);
#else
    (void)plugin;
#endif
}

void reset(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    state->engine.reset();
    state->engine.setParams(state->params);
    state->outputPeak.store(0.0f, std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* processData)
{
    auto* state = self(plugin);
    if (!processData) return CLAP_PROCESS_CONTINUE;
    readEvents(*state, processData->in_events);
    if (processData->audio_outputs_count == 0u || !processData->audio_outputs) return CLAP_PROCESS_CONTINUE;

    auto& output = processData->audio_outputs[0];
    const uint32_t frames = processData->frames_count;
    const uint32_t outputChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (frames == 0u) return CLAP_PROCESS_CONTINUE;
    if (frames > state->maxFrames) {
        for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
            if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
            if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
        }
        return CLAP_PROCESS_CONTINUE;
    }

    std::array<float*, kOutputChannels> outputs {};
    const bool useScratch = output.data32 == nullptr;
    for (uint32_t ch = 0; ch < outputChannels; ++ch) {
        outputs[ch] = useScratch ? state->scratchPointers[ch] : output.data32[ch];
    }
    state->engine.setParams(state->params);
    state->engine.process(outputs.data(), outputChannels, frames);

    float blockPeak = 0.0f;
    for (uint32_t ch = 0; ch < outputChannels; ++ch) {
        if (!outputs[ch]) continue;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            const float value = outputs[ch][frame];
            blockPeak = std::max(blockPeak, std::fabs(value));
            if (useScratch && output.data64 && output.data64[ch]) output.data64[ch][frame] = static_cast<double>(value);
        }
    }
    for (uint32_t ch = outputChannels; ch < output.channel_count; ++ch) {
        if (output.data32 && output.data32[ch]) std::fill(output.data32[ch], output.data32[ch] + frames, 0.0f);
        if (output.data64 && output.data64[ch]) std::fill(output.data64[ch], output.data64[ch] + frames, 0.0);
    }
    state->outputPeak.store(
        std::max(state->outputPeak.load(std::memory_order_relaxed) * 0.92f, blockPeak),
        std::memory_order_relaxed);
#if defined(__APPLE__)
    publishGuiSnapshot(*state);
#endif
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0u || !info) return false;
    info->id = 20;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_AMBISONIC;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    info->id = 30;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI In", sizeof(info->name));
    return true;
}

const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

struct ParamDef {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr ParamDef kParams[] {
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 12.0, true },
    { kModeParamId, "Mode", 0.0, 2.0, 0.0, true },
    { kSystemParamId, "System", 0.0, 3.0, 3.0, true },
    { kAmplitudeDistributionParamId, "Amplitude Distribution", 0.0, 6.0, 2.0, true },
    { kBaseNoteParamId, "Base Note", 12.0, 96.0, 40.0, false },
    { kPitchSpreadParamId, "Pitch Spread", 0.0, 48.0, 19.0, false },
    { kDetuneParamId, "Deviation", 0.0, 100.0, 9.0, false },
    { kBreakpointsParamId, "Breakpoints", 4.0, 32.0, 16.0, true },
    { kAmplitudeStepParamId, "Amplitude Step", 0.0, 1.0, 0.58, false },
    { kTimeStepParamId, "Time Step", 0.0, 1.0, 0.52, false },
    { kInertiaParamId, "Inertia", 0.0, 1.0, 0.72, false },
    { kActivityParamId, "Activity", 0.0, 1.0, 0.74, false },
    { kCouplingParamId, "Coupling", 0.0, 1.0, 0.58, false },
    { kMemoryParamId, "Memory", 0.0, 1.0, 0.74, false },
    { kReactivityParamId, "Reactivity", 0.0, 1.0, 0.64, false },
    { kAttackParamId, "Attack", 1.0, 4000.0, 80.0, false },
    { kDecayParamId, "Decay", 5.0, 8000.0, 480.0, false },
    { kSustainParamId, "Sustain", 0.0, 1.0, 0.72, false },
    { kReleaseParamId, "Release", 5.0, 12000.0, 1800.0, false },
    { kMotionParamId, "Motion", 0.0, 3.0, 3.0, true },
    { kMotionRateParamId, "Motion Rate", 0.001, 1.0, 0.028, false },
    { kMotionAmountParamId, "Motion Amount", 0.0, 1.0, 0.72, false },
    { kMotionSpreadParamId, "Motion Spread", 0.0, 1.0, 0.82, false },
    { kAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Center Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Center Distance", 0.15, 2.0, 1.0, false },
    { kOutputParamId, "Output", -60.0, 6.0, -24.0, false },
    { kDurationDistributionParamId, "Timing Distribution", 0.0, 6.0, 3.0, true },
    { kModelParamId, "Stochastic Model", 0.0, 3.0, 3.0, true },
    { kDynamicsParamId, "Dynamics", 0.0, 3.0, 3.0, true },
    { kDynamicsDriveParamId, "Dynamics Drive", 0.0, 1.0, 0.72, false },
    { kDynamicsBounceParamId, "Dynamics Bounce", 0.0, 1.0, 0.88, false },
    { kDynamicsDragParamId, "Dynamics Drag", 0.0, 1.0, 0.16, false },
    { kDynamicsRadiusParamId, "Collision Radius", 0.0, 1.0, 0.42, false },
    { kSynthesisDepthParamId, "Synthesis Map", 0.0, 1.0, 0.86, false },
    { kSpatialDepthParamId, "Spatial Map", 0.0, 1.0, 0.68, false },
};

const ParamDef* paramDef(clap_id id)
{
    for (const auto& definition : kParams) {
        if (definition.id == id) return &definition;
    }
    return nullptr;
}

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(kParams)) return false;
    const auto& definition = kParams[index];
    info->id = definition.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (definition.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, definition.name, sizeof(info->name));
    std::strncpy(info->module, "Ambi Stochastic Encoder", sizeof(info->module));
    info->min_value = definition.minimum;
    info->max_value = definition.maximum;
    info->default_value = definition.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto& params = self(plugin)->params;
    switch (id) {
    case kOrderParamId: *value = params.order; return true;
    case kVoicesParamId: *value = params.voices; return true;
    case kModeParamId: *value = static_cast<uint32_t>(params.mode); return true;
    case kSystemParamId: *value = static_cast<uint32_t>(params.system); return true;
    case kAmplitudeDistributionParamId: *value = static_cast<uint32_t>(params.amplitudeDistribution); return true;
    case kDurationDistributionParamId: *value = static_cast<uint32_t>(params.durationDistribution); return true;
    case kModelParamId: *value = static_cast<uint32_t>(params.model); return true;
    case kBaseNoteParamId: *value = params.baseNote; return true;
    case kPitchSpreadParamId: *value = params.pitchSpreadSemitones; return true;
    case kDetuneParamId: *value = params.detuneCents; return true;
    case kBreakpointsParamId: *value = params.breakpoints; return true;
    case kAmplitudeStepParamId: *value = params.amplitudeStep; return true;
    case kTimeStepParamId: *value = params.timeStep; return true;
    case kInertiaParamId: *value = params.inertia; return true;
    case kActivityParamId: *value = params.activity; return true;
    case kCouplingParamId: *value = params.coupling; return true;
    case kMemoryParamId: *value = params.memory; return true;
    case kReactivityParamId: *value = params.reactivity; return true;
    case kAttackParamId: *value = params.attackMs; return true;
    case kDecayParamId: *value = params.decayMs; return true;
    case kSustainParamId: *value = params.sustain; return true;
    case kReleaseParamId: *value = params.releaseMs; return true;
    case kMotionParamId: *value = static_cast<uint32_t>(params.motion); return true;
    case kMotionRateParamId: *value = params.motionRateHz; return true;
    case kMotionAmountParamId: *value = params.motionAmount; return true;
    case kMotionSpreadParamId: *value = params.motionSpread; return true;
    case kAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kElevationParamId: *value = params.centerElevationDeg; return true;
    case kDistanceParamId: *value = params.centerDistance; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kDynamicsParamId: *value = static_cast<uint32_t>(params.dynamics); return true;
    case kDynamicsDriveParamId: *value = params.dynamicsDrive; return true;
    case kDynamicsBounceParamId: *value = params.dynamicsBounce; return true;
    case kDynamicsDragParamId: *value = params.dynamicsDrag; return true;
    case kDynamicsRadiusParamId: *value = params.dynamicsRadius; return true;
    case kSynthesisDepthParamId: *value = params.synthesisDepth; return true;
    case kSpatialDepthParamId: *value = params.spatialDepth; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kModeParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticModeName(static_cast<s3g::AmbiStochasticMode>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kSystemParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticSystemName(static_cast<s3g::AmbiStochasticSystem>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kAmplitudeDistributionParamId || id == kDurationDistributionParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticDistributionName(static_cast<s3g::AmbiStochasticDistribution>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kModelParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticModelName(static_cast<s3g::AmbiStochasticModel>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kMotionParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticMotionName(static_cast<s3g::AmbiStochasticMotion>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kDynamicsParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticDynamicsName(static_cast<s3g::AmbiStochasticDynamics>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kVoicesParamId || id == kBreakpointsParamId) {
        std::snprintf(display, size, "%.0f", value);
    } else if (id == kBaseNoteParamId) {
        std::snprintf(display, size, "M%.1f", value);
    } else if (id == kPitchSpreadParamId) {
        std::snprintf(display, size, "%.1f ST", value);
    } else if (id == kDetuneParamId) {
        std::snprintf(display, size, "%.1f CT", value);
    } else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) {
        std::snprintf(display, size, "%.0f MS", value);
    } else if (id == kMotionRateParamId) {
        std::snprintf(display, size, "%.3f HZ", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.0f DEG", value);
    } else if (id == kDistanceParamId) {
        std::snprintf(display, size, "%.2f", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f DB", value);
    } else {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    const auto* definition = paramDef(id);
    if (!definition) return false;
    *value = std::clamp(std::atof(display), definition->minimum, definition->maximum);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    readEvents(*self(plugin), input);
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
    SavedState saved {};
    auto* state = self(plugin);
    saved.params = state->params;
#if defined(__APPLE__)
    saved.guiViewMode = state->guiViewMode;
    saved.guiViewAzDeg = state->guiViewAzDeg;
    saved.guiViewElDeg = state->guiViewElDeg;
    saved.guiViewZoom = state->guiViewZoom;
    saved.guiFieldPage = state->guiFieldPage;
#endif
    return stream->write(stream, &saved, sizeof(saved)) == static_cast<int64_t>(sizeof(saved));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    auto readExact = [stream](void* destination, uint64_t bytes) {
        auto* write = static_cast<uint8_t*>(destination);
        uint64_t remaining = bytes;
        while (remaining > 0u) {
            const int64_t read = stream->read(stream, write, remaining);
            if (read <= 0) return false;
            write += read;
            remaining -= static_cast<uint64_t>(read);
        }
        return true;
    };
    uint32_t version = 0u;
    if (!readExact(&version, sizeof(version))) return false;

    s3g::AmbiStochasticParams params {};
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    int32_t guiFieldPage = 0;
    if (version == kStateVersion) {
        SavedState saved {};
        saved.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        params = saved.params;
        guiViewMode = saved.guiViewMode;
        guiViewAzDeg = saved.guiViewAzDeg;
        guiViewElDeg = saved.guiViewElDeg;
        guiViewZoom = saved.guiViewZoom;
        guiFieldPage = saved.guiFieldPage;
    } else if (version == 2u) {
        SavedStateV2 saved {};
        saved.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        const auto& old = saved.params;
        params.order = old.order;
        params.voices = old.voices;
        params.mode = old.mode;
        params.system = old.system;
        params.model = old.model;
        params.amplitudeDistribution = old.amplitudeDistribution;
        params.durationDistribution = old.durationDistribution;
        params.baseNote = old.baseNote;
        params.pitchSpreadSemitones = old.pitchSpreadSemitones;
        params.detuneCents = old.detuneCents;
        params.breakpoints = old.breakpoints;
        params.amplitudeStep = old.amplitudeStep;
        params.timeStep = old.timeStep;
        params.inertia = old.inertia;
        params.activity = old.activity;
        params.coupling = old.coupling;
        params.memory = old.memory;
        params.reactivity = old.reactivity;
        params.attackMs = old.attackMs;
        params.decayMs = old.decayMs;
        params.sustain = old.sustain;
        params.releaseMs = old.releaseMs;
        params.motion = old.motion;
        params.motionRateHz = old.motionRateHz;
        params.motionAmount = old.motionAmount;
        params.motionSpread = old.motionSpread;
        params.centerAzimuthDeg = old.centerAzimuthDeg;
        params.centerElevationDeg = old.centerElevationDeg;
        params.centerDistance = old.centerDistance;
        params.outputGainDb = old.outputGainDb;
        params.dynamics = s3g::AmbiStochasticDynamics::Off;
        guiViewMode = saved.guiViewMode;
        guiViewAzDeg = saved.guiViewAzDeg;
        guiViewElDeg = saved.guiViewElDeg;
        guiViewZoom = saved.guiViewZoom;
    } else if (version == 1u) {
        SavedStateV1 saved {};
        saved.version = version;
        if (!readExact(reinterpret_cast<uint8_t*>(&saved) + sizeof(version), sizeof(saved) - sizeof(version))) return false;
        const auto& old = saved.params;
        params.order = old.order;
        params.voices = old.voices;
        params.mode = old.mode;
        params.system = old.system;
        params.model = s3g::AmbiStochasticModel::Delta;
        const auto oldDistribution = static_cast<uint32_t>(old.distribution) == 3u
            ? s3g::AmbiStochasticDistribution::Binary : old.distribution;
        params.amplitudeDistribution = oldDistribution;
        params.durationDistribution = oldDistribution;
        params.baseNote = old.baseNote;
        params.pitchSpreadSemitones = old.pitchSpreadSemitones;
        params.detuneCents = old.detuneCents;
        params.breakpoints = old.breakpoints;
        params.amplitudeStep = old.amplitudeStep;
        params.timeStep = old.timeStep;
        params.inertia = old.inertia;
        params.activity = old.activity;
        params.coupling = old.coupling;
        params.memory = old.memory;
        params.reactivity = old.reactivity;
        params.attackMs = old.attackMs;
        params.decayMs = old.decayMs;
        params.sustain = old.sustain;
        params.releaseMs = old.releaseMs;
        params.motion = old.motion;
        params.motionRateHz = old.motionRateHz;
        params.motionAmount = old.motionAmount;
        params.motionSpread = old.motionSpread;
        params.centerAzimuthDeg = old.centerAzimuthDeg;
        params.centerElevationDeg = old.centerElevationDeg;
        params.centerDistance = old.centerDistance;
        params.outputGainDb = old.outputGainDb;
        params.dynamics = s3g::AmbiStochasticDynamics::Off;
        guiViewMode = saved.guiViewMode;
        guiViewAzDeg = saved.guiViewAzDeg;
        guiViewElDeg = saved.guiViewElDeg;
        guiViewZoom = saved.guiViewZoom;
    } else {
        return false;
    }
    auto* state = self(plugin);
    state->params = params;
    state->engine.setParams(state->params);
    state->params = state->engine.params();
#if defined(__APPLE__)
    state->guiViewMode = std::clamp(guiViewMode, -1, 2);
    state->guiViewAzDeg = std::clamp(guiViewAzDeg, -180.0f, 180.0f);
    state->guiViewElDeg = std::clamp(guiViewElDeg, -85.0f, 85.0f);
    state->guiViewZoom = std::clamp(guiViewZoom, 0.55f, 2.4f);
    state->guiFieldPage = std::clamp(guiFieldPage, 0, 2);
#endif
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

struct GuiSliderSpec {
    clap_id id;
    CGFloat panelX;
    CGFloat y;
    double minimum;
    double maximum;
    bool logarithmic;
};

constexpr std::array<GuiSliderSpec, 30> kGuiSliders {{
    { kVoicesParamId, 630, 156, 1.0, 64.0, false },
    { kBaseNoteParamId, 630, 182, 12.0, 96.0, false },
    { kPitchSpreadParamId, 630, 208, 0.0, 48.0, false },
    { kDetuneParamId, 630, 234, 0.0, 100.0, false },
    { kBreakpointsParamId, 630, 396, 4.0, 32.0, false },
    { kAmplitudeStepParamId, 630, 422, 0.0, 1.0, false },
    { kTimeStepParamId, 630, 448, 0.0, 1.0, false },
    { kInertiaParamId, 630, 474, 0.0, 1.0, false },
    { kActivityParamId, 630, 500, 0.0, 1.0, false },
    { kAttackParamId, 630, 584, 1.0, 4000.0, true },
    { kDecayParamId, 630, 610, 5.0, 8000.0, true },
    { kSustainParamId, 630, 636, 0.0, 1.0, false },
    { kReleaseParamId, 630, 662, 5.0, 12000.0, true },
    { kOutputParamId, 630, 734, -60.0, 6.0, false },
    { kDynamicsDriveParamId, 896, 104, 0.0, 1.0, false },
    { kDynamicsBounceParamId, 896, 130, 0.0, 1.0, false },
    { kDynamicsDragParamId, 896, 156, 0.0, 1.0, false },
    { kDynamicsRadiusParamId, 896, 182, 0.0, 1.0, false },
    { kCouplingParamId, 896, 208, 0.0, 1.0, false },
    { kMemoryParamId, 896, 234, 0.0, 1.0, false },
    { kReactivityParamId, 896, 260, 0.0, 1.0, false },
    { kMotionRateParamId, 896, 360, 0.001, 1.0, true },
    { kMotionAmountParamId, 896, 386, 0.0, 1.0, false },
    { kMotionSpreadParamId, 896, 412, 0.0, 1.0, false },
    { kAzimuthParamId, 896, 438, -180.0, 180.0, false },
    { kElevationParamId, 896, 464, -90.0, 90.0, false },
    { kDistanceParamId, 896, 490, 0.15, 2.0, false },
    { kSynthesisDepthParamId, 896, 564, 0.0, 1.0, false },
    { kSpatialDepthParamId, 896, 590, 0.0, 1.0, false },
}};

const GuiSliderSpec* guiSliderSpec(clap_id id)
{
    for (const auto& spec : kGuiSliders) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

double sliderNorm(const GuiSliderSpec& spec, double value)
{
    value = std::clamp(value, spec.minimum, spec.maximum);
    if (spec.logarithmic) {
        return std::log(value / spec.minimum) / std::log(spec.maximum / spec.minimum);
    }
    return (value - spec.minimum) / (spec.maximum - spec.minimum);
}

double sliderValue(const GuiSliderSpec& spec, double norm)
{
    norm = std::clamp(norm, 0.0, 1.0);
    if (spec.logarithmic) return spec.minimum * std::pow(spec.maximum / spec.minimum, norm);
    return spec.minimum + (spec.maximum - spec.minimum) * norm;
}

float linearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

NSColor* pointColor(float azimuthDeg, float elevationDeg, float distance, bool selected)
{
    const float hue = std::fmod(azimuthDeg / 360.0f + 1.0f, 1.0f);
    const float light = std::clamp((std::clamp(elevationDeg, -90.0f, 90.0f) + 90.0f) / 180.0f, 0.30f, 0.86f);
    const float chroma = std::clamp(distance / 2.0f, 0.10f, 1.0f) * 0.34f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float red = linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float green = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float blue = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.06f : 0.17f;
    red = red * (1.0f - grayMix) + 0.72f * grayMix;
    green = green * (1.0f - grayMix) + 0.72f * grayMix;
    blue = blue * (1.0f - grayMix) + 0.72f * grayMix;
    return [NSColor colorWithCalibratedRed:red green:green blue:blue alpha:selected ? 1.0 : 0.90];
}

} // namespace

@interface S3GAmbiStochasticEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
    uint32_t _selectedVoice;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    int _fieldPage;
    BOOL _dragView;
    NSPoint _lastDragPoint;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiStochasticEncoderView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _openMenuRect = NSZeroRect;
        _selectedVoice = 0u;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _fieldPage = plugin ? plugin->guiFieldPage : 0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) [self removeTrackingArea:area];
    [super updateTrackingAreas];
    const NSTrackingAreaOptions options = NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    NSTrackingArea* area = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:[area autorelease]];
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
    _plugin->guiFieldPage = _fieldPage;
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES];
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
    if ([self isHidden] || !_plugin || !_plugin->guiVisible.load(std::memory_order_relaxed)) return;
    if (!s3g::clap_support::hostAppIsActive()) return;
    [self setNeedsDisplay:YES];
}

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [super dealloc];
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 500); }
- (NSRect)fieldRect { return NSMakeRect(34, 72, 564, 454); }
- (NSRect)wavePanelRect { return NSMakeRect(18, 554, 596, 288); }
- (NSRect)waveRect { return NSMakeRect(34, 584, 564, 242); }

- (NSRect)viewButtonRect:(int)index
{
    return NSMakeRect(430.0 + static_cast<CGFloat>(index) * 49.0, 46.0, 43.0, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    return NSMakeRect(378.0 + static_cast<CGFloat>(index) * 23.0, 46.0, 18.0, 13.0);
}

- (NSRect)fieldPageButtonRect:(int)index
{
    return NSMakeRect(208.0 + static_cast<CGFloat>(index) * 54.0, 46.0, 49.0, 13.0);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 38.0;
        _viewElDeg = 32.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (CGFloat)viewScale
{
    const NSRect rect = [self fieldRect];
    return std::min(rect.size.width, rect.size.height) * 0.22 * std::clamp(_viewZoom, 0.55, 2.20);
}

- (NSPoint)projectWorld:(s3g::Vec3)point depth:(CGFloat*)depth
{
    const NSRect rect = [self fieldRect];
    const CGFloat centerX = NSMidX(rect);
    const CGFloat centerY = NSMidY(rect);
    const CGFloat scale = [self viewScale];
    if (_viewMode == 0) {
        if (depth) *depth = point.z;
        return NSMakePoint(centerX - point.y * scale, centerY - point.x * scale);
    }
    if (_viewMode == 1) {
        if (depth) *depth = point.x;
        return NSMakePoint(centerX - point.y * scale, centerY - point.z * scale);
    }
    const float azimuth = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 - se * point.z;
    const float z2 = se * y1 + ce * point.z;
    if (depth) *depth = z2;
    return NSMakePoint(centerX + x1 * scale, centerY - y2 * scale);
}

- (s3g::AmbiStochasticPoint)snapshotPoint:(uint32_t)index
{
    s3g::AmbiStochasticPoint point {};
    if (!_plugin || index >= s3g::kAmbiStochasticMaxVoices) return point;
    point.azimuthDeg = _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    point.elevationDeg = _plugin->guiElevation[index].load(std::memory_order_relaxed);
    point.distance = _plugin->guiDistance[index].load(std::memory_order_relaxed);
    return point;
}

- (NSPoint)projectPoint:(uint32_t)index depth:(CGFloat*)depth
{
    const auto point = [self snapshotPoint:index];
    const s3g::Vec3 direction = s3g::directionFromAed(point.azimuthDeg, point.elevationDeg);
    return [self projectWorld:{ direction.x * point.distance, direction.y * point.distance, direction.z * point.distance } depth:depth];
}

- (s3g::Vec3)topologyWorld:(uint32_t)index
{
    return {
        _plugin->guiTopologyX[index].load(std::memory_order_relaxed),
        _plugin->guiTopologyY[index].load(std::memory_order_relaxed),
        _plugin->guiTopologyZ[index].load(std::memory_order_relaxed)
    };
}

- (NSPoint)projectTopologyPoint:(uint32_t)index depth:(CGFloat*)depth
{
    return [self projectWorld:[self topologyWorld:index] depth:depth];
}

- (int)hitPoint:(NSPoint)point
{
    if (_fieldPage == 2 || !NSPointInRect(point, [self fieldRect])) return -1;
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiStochasticMaxVoices);
    int best = -1;
    CGFloat bestDistance = 13.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const NSPoint projected = _fieldPage == 1
            ? [self projectTopologyPoint:voice depth:nullptr]
            : [self projectPoint:voice depth:nullptr];
        const CGFloat distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(voice);
        }
    }
    return best;
}

- (void)drawTopologyMapInRect:(NSRect)field valueAttrs:(NSDictionary*)valueAttrs
{
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1, 1)] addClip];
    [s3g::clap_gui::color(0x262626) setStroke];
    const NSPoint origin = [self projectWorld:{ 0.0f, 0.0f, 0.0f } depth:nullptr];
    const s3g::Vec3 axes[] = { { 0.95f, 0.0f, 0.0f }, { 0.0f, 0.82f, 0.0f }, { 0.0f, 0.0f, 0.82f } };
    for (const auto& axis : axes) {
        [NSBezierPath strokeLineFromPoint:origin toPoint:[self projectWorld:axis depth:nullptr]];
    }

    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiStochasticMaxVoices);
    std::array<NSPoint, s3g::kAmbiStochasticMaxVoices> projected {};
    for (uint32_t voice = 0; voice < voices; ++voice) {
        projected[voice] = [self projectTopologyPoint:voice depth:nullptr];
    }
    for (uint32_t voice = 0; voice < voices; ++voice) {
        if (_plugin->guiBondStrength[voice].load(std::memory_order_relaxed) <= 0.0f) continue;
        const uint32_t neighbors[] {
            std::min<uint32_t>(_plugin->guiNeighbor[voice].load(std::memory_order_relaxed), voices - 1u),
            std::min<uint32_t>(_plugin->guiSecondaryNeighbor[voice].load(std::memory_order_relaxed), voices - 1u)
        };
        const float transfer = std::clamp(std::fabs(_plugin->guiNetworkPulse[voice].load(std::memory_order_relaxed))
                + _plugin->guiTension[voice].load(std::memory_order_relaxed) * 0.65f,
            0.0f, 1.0f);
        for (uint32_t edge = 0; edge < 2u; ++edge) {
            if (neighbors[edge] == voice
                || _plugin->guiBondStrength[neighbors[edge]].load(std::memory_order_relaxed) <= 0.0f) continue;
            NSBezierPath* link = [NSBezierPath bezierPath];
            [link moveToPoint:projected[voice]];
            [link lineToPoint:projected[neighbors[edge]]];
            [s3g::clap_gui::color(edge == 0u ? 0xa0a0a0 : 0x737373,
                (edge == 0u ? 0.17 : 0.10) + transfer * 0.58) setStroke];
            [link setLineWidth:0.65 + transfer * 1.25];
            [link stroke];
        }
    }

    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x080808), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const bool selected = voice == _selectedVoice;
        const float contact = _plugin->guiContact[voice].load(std::memory_order_relaxed);
        const float pulse = std::fabs(_plugin->guiNetworkPulse[voice].load(std::memory_order_relaxed));
        const float tension = _plugin->guiTension[voice].load(std::memory_order_relaxed);
        const CGFloat size = (voices > 32u ? 7.0 : 9.0) + std::clamp(contact + pulse + tension * 0.5f, 0.0f, 1.0f) * 7.0;
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        const auto point = [self snapshotPoint:voice];
        [[pointColor(point.azimuthDeg, point.elevationDeg, point.distance, selected)
            colorWithAlphaComponent:selected ? 1.0 : 0.72] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe0e0e0 : 0x4a4a4a) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5,
            NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
    }
    [NSGraphicsContext restoreGraphicsState];

    [@"X MUTATION     Y EVENT     Z PERIOD" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19)
        withAttributes:valueAttrs];
    const auto topology = [self topologyWorld:_selectedVoice];
    const float radius = std::clamp(std::sqrt(topology.x * topology.x
            + topology.y * topology.y + topology.z * topology.z),
        0.0f, 1.0f);
    NSString* readout = [NSString stringWithFormat:@"V%02u   X %+.2f   Y %+.2f   Z %+.2f   R %.2f",
        _selectedVoice + 1u, topology.x, topology.y, topology.z, radius];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
}

- (void)drawControlLoopsInRect:(NSRect)field valueAttrs:(NSDictionary*)valueAttrs
{
    const uint32_t voices = std::max<uint32_t>(1u, _plugin->params.voices);
    const uint32_t neighbor = std::min<uint32_t>(
        _plugin->guiNeighbor[_selectedVoice].load(std::memory_order_relaxed), voices - 1u);
    const float local = std::clamp(_plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed) * 4.0f, 0.0f, 1.0f);
    const float targetSpeed = 0.035f + _plugin->params.dynamicsDrive
        * _plugin->params.dynamicsDrive * 0.36f;
    const float motion = std::clamp(_plugin->guiKinetic[_selectedVoice].load(std::memory_order_relaxed)
            / std::max(0.03f, targetSpeed * 1.55f),
        0.0f, 1.0f);
    const float contact = std::clamp(_plugin->guiContact[_selectedVoice].load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float near = std::clamp(_plugin->guiEnergy[neighbor].load(std::memory_order_relaxed) * 4.0f
            + std::fabs(_plugin->guiNetworkPulse[_selectedVoice].load(std::memory_order_relaxed)) * 0.55f,
        0.0f, 1.0f);
    const float global = std::clamp(_plugin->guiGlobalEnergy.load(std::memory_order_relaxed) * 4.0f, 0.0f, 1.0f);
    const float curve = std::clamp(contact * 0.30f
            + std::fabs(_plugin->guiNetworkPulse[_selectedVoice].load(std::memory_order_relaxed)) * 0.28f
            + _plugin->guiTension[_selectedVoice].load(std::memory_order_relaxed) * 0.20f
            + _plugin->guiCrowding[_selectedVoice].load(std::memory_order_relaxed) * 0.12f
            + motion * 0.10f,
        0.0f, 1.0f);
    const float values[] { local, motion, contact, near, global, curve };
    static NSString* labels[] { @"LOCAL", @"MOTION", @"CONTACT", @"NEAR", @"FIELD", @"CURVE" };
    const NSPoint centers[] {
        { field.origin.x + 88, field.origin.y + 150 },
        { field.origin.x + 282, field.origin.y + 78 },
        { field.origin.x + 476, field.origin.y + 150 },
        { field.origin.x + 476, field.origin.y + 306 },
        { field.origin.x + 282, field.origin.y + 378 },
        { field.origin.x + 88, field.origin.y + 306 }
    };
    for (uint32_t node = 0; node < 6u; ++node) {
        const uint32_t next = (node + 1u) % 6u;
        NSBezierPath* edge = [NSBezierPath bezierPath];
        [edge moveToPoint:centers[node]];
        [edge lineToPoint:centers[next]];
        [s3g::clap_gui::color(0x8a8a8a, 0.13 + values[node] * 0.72) setStroke];
        [edge setLineWidth:0.7 + values[node] * 1.7];
        [edge stroke];
        const NSPoint marker {
            centers[node].x + (centers[next].x - centers[node].x) * 0.58,
            centers[node].y + (centers[next].y - centers[node].y) * 0.58
        };
        [s3g::clap_gui::color(0xb0b0b0, 0.20 + values[node] * 0.78) setFill];
        NSRectFill(NSMakeRect(marker.x - 2.0, marker.y - 2.0, 4.0, 4.0));
    }
    const auto selectedPoint = [self snapshotPoint:_selectedVoice];
    for (uint32_t node = 0; node < 6u; ++node) {
        const NSRect box = NSMakeRect(centers[node].x - 42, centers[node].y - 19, 84, 38);
        [s3g::clap_gui::color(0x0e0e0e) setFill];
        NSRectFill(box);
        [s3g::clap_gui::color(0x555555) setStroke];
        NSFrameRect(box);
        [[pointColor(selectedPoint.azimuthDeg, selectedPoint.elevationDeg, selectedPoint.distance, true)
            colorWithAlphaComponent:0.78] setFill];
        NSRectFill(NSMakeRect(box.origin.x + 2, NSMaxY(box) - 5, (box.size.width - 4) * values[node], 3));
        const NSSize labelSize = [labels[node] sizeWithAttributes:valueAttrs];
        [labels[node] drawAtPoint:NSMakePoint(NSMidX(box) - labelSize.width * 0.5, box.origin.y + 8)
            withAttributes:valueAttrs];
    }
    NSString* status = [NSString stringWithFormat:@"%@   SYN %.0f%%   SPAT %.0f%%",
        [NSString stringWithUTF8String:s3g::ambiStochasticDynamicsName(_plugin->params.dynamics)],
        _plugin->params.synthesisDepth * 100.0f, _plugin->params.spatialDepth * 100.0f];
    s3g::clap_gui::drawRightStatus(status, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"STOCHASTIC FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4" };
    static NSString* pageLabels[] = { @"FIELD", @"MAP", @"LOOPS" };
    for (int index = 0; index < 3; ++index) {
        s3g::clap_gui::drawHeaderButton([self fieldPageButtonRect:index], header, pageLabels[index], index == _fieldPage, attrs, style);
    }
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
    for (int index = 0; index < 3; ++index) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:index], header, viewLabels[index], index == _viewMode, attrs, style);
    }

    [s3g::clap_gui::color(0x0a0a0a) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    if (_fieldPage == 1) {
        [self drawTopologyMapInRect:field valueAttrs:valueAttrs];
        return;
    }
    if (_fieldPage == 2) {
        [self drawControlLoopsInRect:field valueAttrs:valueAttrs];
        return;
    }
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1, 1)] addClip];
    const CGFloat radius = [self viewScale];
    [s3g::clap_gui::color(0x303030) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMidX(field) - radius, NSMidY(field) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];

    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiStochasticMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    _plugin->guiSelectedVoice.store(_selectedVoice, std::memory_order_relaxed);
    std::array<NSPoint, s3g::kAmbiStochasticMaxVoices> projected {};
    for (uint32_t voice = 0; voice < voices; ++voice) projected[voice] = [self projectPoint:voice depth:nullptr];

    if (_plugin->params.system != s3g::AmbiStochasticSystem::Independent && _plugin->params.coupling > 0.001f) {
        for (uint32_t voice = 0; voice < voices; ++voice) {
            if (_plugin->guiBondStrength[voice].load(std::memory_order_relaxed) <= 0.0f) continue;
            const uint32_t neighbors[] {
                std::min<uint32_t>(_plugin->guiNeighbor[voice].load(std::memory_order_relaxed), voices - 1u),
                std::min<uint32_t>(_plugin->guiSecondaryNeighbor[voice].load(std::memory_order_relaxed), voices - 1u)
            };
            const float activity = std::clamp(std::fabs(_plugin->guiNetworkPulse[voice].load(std::memory_order_relaxed))
                    + _plugin->guiTension[voice].load(std::memory_order_relaxed) * 0.6f,
                0.0f, 1.0f);
            for (uint32_t edge = 0; edge < 2u; ++edge) {
                if (neighbors[edge] == voice
                    || _plugin->guiBondStrength[neighbors[edge]].load(std::memory_order_relaxed) <= 0.0f) continue;
                NSBezierPath* link = [NSBezierPath bezierPath];
                [link moveToPoint:projected[voice]];
                [link lineToPoint:projected[neighbors[edge]]];
                const CGFloat alpha = (edge == 0u ? 0.12 : 0.07)
                    + _plugin->params.coupling * 0.18 + activity * 0.48;
                [s3g::clap_gui::color(edge == 0u ? 0xa0a0a0 : 0x737373, alpha) setStroke];
                [link setLineWidth:0.55 + activity * 1.25];
                [link stroke];
            }
        }
    }

    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x0b0b0b), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const bool selected = voice == _selectedVoice;
        const auto point = [self snapshotPoint:voice];
        const float energy = _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
        const float contact = _plugin->guiContact[voice].load(std::memory_order_relaxed);
        const CGFloat size = selected ? 15.0 : (voices > 32u ? 8.0 + contact * 3.0 : 11.0 + contact * 4.0);
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        [[pointColor(point.azimuthDeg, point.elevationDeg, point.distance, selected)
            colorWithAlphaComponent:std::clamp(0.34f + energy * 4.0f, 0.38f, 0.98f)] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xd8d8d8 : (energy > 0.18f ? 0x777777 : 0x202020)) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5, NSMidY(marker) - labelSize.height * 0.5 - 0.5)
            withAttributes:idAttrs];
    }
    [NSGraphicsContext restoreGraphicsState];

    const uint32_t neighbor = std::min<uint32_t>(
        _plugin->guiNeighbor[_selectedVoice].load(std::memory_order_relaxed), voices - 1u);
    const float energy = _plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed);
    NSString* readout = [NSString stringWithFormat:@"V%02u   E %.3f   N%02u", _selectedVoice + 1u, energy, neighbor + 1u];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
}

- (void)drawWaveform:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self wavePanelRect];
    const NSRect wave = [self waveRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PRESSURE CURVE", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    NSString* status = [NSString stringWithFormat:@"%u PTS   %@   A %@   T %@", _plugin->params.breakpoints,
        [NSString stringWithUTF8String:s3g::ambiStochasticModelName(_plugin->params.model)],
        [NSString stringWithUTF8String:s3g::ambiStochasticDistributionName(_plugin->params.amplitudeDistribution)],
        [NSString stringWithUTF8String:s3g::ambiStochasticDistributionName(_plugin->params.durationDistribution)]];
    s3g::clap_gui::drawRightStatus(status, NSMaxX(panel), panel.origin.y + 5, valueAttrs, 8.0);
    [s3g::clap_gui::color(0x0a0a0a) setFill];
    NSRectFill(wave);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(wave);
    [s3g::clap_gui::color(0x292929) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(wave.origin.x + 1, NSMidY(wave))
                              toPoint:NSMakePoint(NSMaxX(wave) - 1, NSMidY(wave))];
    for (uint32_t division = 1u; division < 4u; ++division) {
        const CGFloat x = wave.origin.x + wave.size.width * static_cast<CGFloat>(division) / 4.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, wave.origin.y + 1)
                                  toPoint:NSMakePoint(x, NSMaxY(wave) - 1)];
    }

    NSBezierPath* curve = [NSBezierPath bezierPath];
    for (uint32_t sample = 0; sample < kGuiWaveSamples; ++sample) {
        const float value = _plugin->guiWaveform[sample].load(std::memory_order_relaxed);
        const NSPoint point = NSMakePoint(
            wave.origin.x + static_cast<CGFloat>(sample) / static_cast<CGFloat>(kGuiWaveSamples - 1u) * wave.size.width,
            NSMidY(wave) - value * wave.size.height * 0.42);
        if (sample == 0u) [curve moveToPoint:point];
        else [curve lineToPoint:point];
    }
    const auto selectedPoint = [self snapshotPoint:_selectedVoice];
    [[pointColor(selectedPoint.azimuthDeg, selectedPoint.elevationDeg, selectedPoint.distance, true)
        colorWithAlphaComponent:0.88] setStroke];
    [curve setLineWidth:1.2];
    [curve stroke];

    const uint32_t count = std::min<uint32_t>(
        _plugin->guiBreakpointCount.load(std::memory_order_acquire), s3g::kAmbiStochasticMaxBreakpoints);
    for (uint32_t pointIndex = 0; pointIndex < count; ++pointIndex) {
        const float position = _plugin->guiBreakpointPosition[pointIndex].load(std::memory_order_relaxed);
        const float amplitude = _plugin->guiBreakpointAmplitude[pointIndex].load(std::memory_order_relaxed);
        const NSPoint point = NSMakePoint(wave.origin.x + position * wave.size.width,
            NSMidY(wave) - amplitude * wave.size.height * 0.42);
        [s3g::clap_gui::color(0xc0c0c0, 0.82) setFill];
        NSRectFill(NSMakeRect(point.x - 2.0, point.y - 2.0, 4.0, 4.0));
    }
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    char display[64] {};
    paramsValueToText(nullptr, param, value, display, sizeof(display));
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:display], sliderNorm(*spec, value), spec->y,
        attrs, valueAttrs, style, spec->panelX + 16, spec->panelX + 108, spec->panelX + 196, 82);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value menu:(int)menu panelX:(CGFloat)panelX y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    (void)menu;
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto& params = _plugin->params;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 228, style);
    s3g::clap_gui::drawPanelHeader(@"GENERATOR", true, 630, 42, 250, 21, attrs, style);
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:s3g::ambiStochasticModeName(params.mode)] menu:1 panelX:630 y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"SYSTEM" value:[NSString stringWithUTF8String:s3g::ambiStochasticSystemName(params.system)] menu:2 panelX:630 y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", params.order] menu:3 panelX:630 y:130 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:params.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BASE" param:kBaseNoteParamId value:params.baseNote attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RANGE" param:kPitchSpreadParamId value:params.pitchSpreadSemitones attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEV" param:kDetuneParamId value:params.detuneCents attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 282, 250, 254, style);
    s3g::clap_gui::drawPanelHeader(@"STOCHASTIC", true, 630, 282, 250, 21, attrs, style);
    [self drawMenu:@"MODEL" value:[NSString stringWithUTF8String:s3g::ambiStochasticModelName(params.model)] menu:4 panelX:630 y:318 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"A-DIST" value:[NSString stringWithUTF8String:s3g::ambiStochasticDistributionName(params.amplitudeDistribution)] menu:5 panelX:630 y:344 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"T-DIST" value:[NSString stringWithUTF8String:s3g::ambiStochasticDistributionName(params.durationDistribution)] menu:6 panelX:630 y:370 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"PTS" param:kBreakpointsParamId value:params.breakpoints attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AMP" param:kAmplitudeStepParamId value:params.amplitudeStep attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TIME" param:kTimeStepParamId value:params.timeStep attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"INERT" param:kInertiaParamId value:params.inertia attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ACT" param:kActivityParamId value:params.activity attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 548, 250, 150, style);
    s3g::clap_gui::drawPanelHeader(@"ENVELOPE", true, 630, 548, 250, 21, attrs, style);
    [self drawSlider:@"ATTACK" param:kAttackParamId value:params.attackMs attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DECAY" param:kDecayParamId value:params.decayMs attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SUSTAIN" param:kSustainParamId value:params.sustain attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RELEASE" param:kReleaseParamId value:params.releaseMs attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 710, 250, 48, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 630, 710, 250, 21, attrs, style);
    [self drawSlider:@"OUT" param:kOutputParamId value:params.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 244, style);
    s3g::clap_gui::drawPanelHeader(@"DYNAMICS", true, 896, 42, 246, 21, attrs, style);
    [self drawMenu:@"DYN" value:[NSString stringWithUTF8String:s3g::ambiStochasticDynamicsName(params.dynamics)] menu:8 panelX:896 y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DRIVE" param:kDynamicsDriveParamId value:params.dynamicsDrive attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BOUNCE" param:kDynamicsBounceParamId value:params.dynamicsBounce attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DRAG" param:kDynamicsDragParamId value:params.dynamicsDrag attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RADIUS" param:kDynamicsRadiusParamId value:params.dynamicsRadius attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"COUPLE" param:kCouplingParamId value:params.coupling attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"MEM" param:kMemoryParamId value:params.memory attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"REACT" param:kReactivityParamId value:params.reactivity attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 298, 246, 218, style);
    s3g::clap_gui::drawPanelHeader(@"SPACE", true, 896, 298, 246, 21, attrs, style);
    [self drawMenu:@"MOTION" value:[NSString stringWithUTF8String:s3g::ambiStochasticMotionName(params.motion)] menu:7 panelX:896 y:334 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RATE" param:kMotionRateParamId value:params.motionRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AMOUNT" param:kMotionAmountParamId value:params.motionAmount attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPREAD" param:kMotionSpreadParamId value:params.motionSpread attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AZIMUTH" param:kAzimuthParamId value:params.centerAzimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEV" param:kElevationParamId value:params.centerElevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DIST" param:kDistanceParamId value:params.centerDistance attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 528, 246, 126, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY MAP", true, 896, 528, 246, 21, attrs, style);
    [self drawSlider:@"SYN" param:kSynthesisDepthParamId value:params.synthesisDepth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPAT" param:kSpatialDepthParamId value:params.spatialDepth attrs:attrs valueAttrs:valueAttrs style:style];
    NSString* energy = [NSString stringWithFormat:@"E %.3f   K %.3f",
        _plugin->guiGlobalEnergy.load(std::memory_order_relaxed),
        _plugin->guiGlobalKinetic.load(std::memory_order_relaxed)];
    [energy drawAtPoint:NSMakePoint(912, 622) withAttributes:valueAttrs];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* modeItems[] = { @"FREE", @"MIDI", @"BOTH" };
    static NSString* systemItems[] = { @"INDEPENDENT", @"NEIGHBOR", @"FIELD", @"NETWORK" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* modelItems[] = { @"DIRECT", @"DELTA", @"CURVED", @"FREE PERIOD" };
    static NSString* distributionItems[] = { @"UNIFORM", @"GAUSS", @"CAUCHY", @"LOGISTIC", @"ARCSINE", @"EXPON", @"BINARY" };
    static NSString* motionItems[] = { @"FIELD", @"ORBIT", @"DRIFT", @"FEEDBACK" };
    static NSString* dynamicsItems[] = { @"OFF", @"GAS", @"NET", @"CASCADE" };
    NSString** items = modeItems;
    int selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.mode));
    if (_openMenu == 2) {
        items = systemItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.system));
    } else if (_openMenu == 3) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
    } else if (_openMenu == 4) {
        items = modelItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.model));
    } else if (_openMenu == 5) {
        items = distributionItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.amplitudeDistribution));
    } else if (_openMenu == 6) {
        items = distributionItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.durationDistribution));
    } else if (_openMenu == 7) {
        items = motionItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.motion));
    } else if (_openMenu == 8) {
        items = dynamicsItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.dynamics));
    }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    [@"s3g AMBI STOCHASTIC ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)), kGuiWidth, 14, valueAttrs, 18);
    [self drawField:attrs valueAttrs:valueAttrs style:style];
    [self drawWaveform:attrs valueAttrs:valueAttrs style:style];
    [self drawPanels:attrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return NSMakeRect(738, 77, 124, 15);
    case 2: return NSMakeRect(738, 103, 124, 15);
    case 3: return NSMakeRect(738, 129, 124, 15);
    case 4: return NSMakeRect(738, 317, 124, 15);
    case 5: return NSMakeRect(738, 343, 124, 15);
    case 6: return NSMakeRect(738, 369, 124, 15);
    case 7: return NSMakeRect(1004, 333, 124, 15);
    case 8: return NSMakeRect(1004, 77, 124, 15);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return 3u;
    case 2: return 4u;
    case 3: return 7u;
    case 4: return 4u;
    case 5: return 7u;
    case 6: return 7u;
    case 7: return 4u;
    case 8: return 4u;
    default: return 0u;
    }
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _menuItemCount = [self menuCount:menu];
    _hoverMenuItem = -1;
    const NSRect box = [self menuBoxRect:menu];
    _openMenuRect = NSMakeRect(box.origin.x, NSMaxY(box) + 2, box.size.width, 21.0 * _menuItemCount);
    [self setNeedsDisplay:YES];
}

- (void)chooseMenuItem:(int)item
{
    if (item < 0) return;
    switch (_openMenu) {
    case 1: applyParam(*_plugin, kModeParamId, item); break;
    case 2: applyParam(*_plugin, kSystemParamId, item); break;
    case 3: applyParam(*_plugin, kOrderParamId, item + 1); break;
    case 4: applyParam(*_plugin, kModelParamId, item); break;
    case 5: applyParam(*_plugin, kAmplitudeDistributionParamId, item); break;
    case 6: applyParam(*_plugin, kDurationDistributionParamId, item); break;
    case 7: applyParam(*_plugin, kMotionParamId, item); break;
    case 8: applyParam(*_plugin, kDynamicsParamId, item); break;
    default: break;
    }
    _openMenu = 0;
    _hoverMenuItem = -1;
    [self setNeedsDisplay:YES];
}

- (void)updateSliderAtPoint:(NSPoint)point
{
    const auto* spec = guiSliderSpec(_dragParam);
    if (!spec) return;
    const double norm = std::clamp((point.x - (spec->panelX + 108.0)) / 82.0, 0.0, 1.0);
    double value = sliderValue(*spec, norm);
    if (_dragParam == kVoicesParamId || _dragParam == kBreakpointsParamId) value = std::lround(value);
    applyParam(*_plugin, _dragParam, value);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            [self chooseMenuItem:hit];
            return;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
    }
    for (int menu = 1; menu <= 8; ++menu) {
        if (NSPointInRect(point, [self menuBoxRect:menu])) {
            [self openMenu:menu];
            return;
        }
    }
    for (int index = 0; index < 3; ++index) {
        if (NSPointInRect(point, [self fieldPageButtonRect:index])) {
            _fieldPage = index;
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    for (int index = 0; index < 2; ++index) {
        if (NSPointInRect(point, [self zoomButtonRect:index])) {
            _viewZoom = std::clamp(_viewZoom * (index == 0 ? 0.88 : 1.14), 0.55, 2.20);
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    for (int index = 0; index < 3; ++index) {
        if (NSPointInRect(point, [self viewButtonRect:index])) {
            [self setViewPreset:index];
            return;
        }
    }
    const int voice = [self hitPoint:point];
    if (voice >= 0) {
        _selectedVoice = static_cast<uint32_t>(voice);
        _plugin->guiSelectedVoice.store(_selectedVoice, std::memory_order_relaxed);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_fieldPage != 2 && NSPointInRect(point, [self fieldRect])) {
        _dragView = YES;
        _lastDragPoint = point;
        return;
    }
    for (const auto& spec : kGuiSliders) {
        const NSRect hit = NSMakeRect(spec.panelX + 8, spec.y - 8, 232, 24);
        if (!NSPointInRect(point, hit)) continue;
        if ([event clickCount] >= 2) {
            const auto* definition = paramDef(spec.id);
            if (definition) applyParam(*_plugin, spec.id, definition->defaultValue);
            [self setNeedsDisplay:YES];
            return;
        }
        _dragParam = spec.id;
        [self updateSliderAtPoint:point];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragParam != CLAP_INVALID_ID) {
        [self updateSliderAtPoint:point];
        return;
    }
    if (_dragView) {
        _viewMode = -1;
        _viewAzDeg = std::fmod(_viewAzDeg + (point.x - _lastDragPoint.x) * 0.55 + 540.0, 360.0) - 180.0;
        _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.45, -85.0, 85.0);
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragView = NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* state = self(plugin);
    if (state->guiView) return true;
    state->guiView = [[S3GAmbiStochasticEncoderView alloc] initWithPlugin:state];
    return state->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return;
    state->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GAmbiStochasticEncoderView*>(state->guiView);
    [view stopRefreshTimer];
    [view removeFromSuperview];
    [view release];
    state->guiView = nullptr;
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
    auto* state = self(plugin);
    if (!state->guiView) return false;
    [static_cast<NSView*>(state->guiView) setFrameSize:NSMakeSize(width, height)];
    return true;
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->api || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* state = self(plugin);
    if (!state->guiView) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(state->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    return true;
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    state->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<NSView*>(state->guiView) setHidden:NO];
    [static_cast<S3GAmbiStochasticEncoderView*>(state->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    state->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiStochasticEncoderView*>(state->guiView) stopRefreshTimer];
    [static_cast<NSView*>(state->guiView) setHidden:YES];
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

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-stochastic-encoder-64",
    "s3g Ambi Stochastic Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Dynamic stochastic synthesis with network feedback and first- through seventh-order ACN/SN3D output.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* state = new (std::nothrow) Plugin();
    if (!state) return nullptr;
    state->host = host;
    state->plugin.desc = &descriptor;
    state->plugin.plugin_data = state;
    state->plugin.init = init;
    state->plugin.destroy = destroy;
    state->plugin.activate = activate;
    state->plugin.deactivate = deactivate;
    state->plugin.start_processing = startProcessing;
    state->plugin.stop_processing = stopProcessing;
    state->plugin.reset = reset;
    state->plugin.process = process;
    state->plugin.get_extension = pluginGetExtension;
    state->plugin.on_main_thread = onMainThread;
    return &state->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const CLAP_EXPORT clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
