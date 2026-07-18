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
constexpr uint32_t kStateVersion = 6u;
constexpr uint32_t kGuiWidth = 1160u;
constexpr uint32_t kGuiHeight = 860u;
constexpr uint32_t kGuiWaveSamples = 256u;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kVoicesParamId = 2;
constexpr clap_id kModeParamId = 3;
constexpr clap_id kSelectionParamId = 4;
constexpr clap_id kTransitionParamId = 5;
constexpr clap_id kAmplitudeDistributionParamId = 6;
constexpr clap_id kDurationDistributionParamId = 7;
constexpr clap_id kBaseNoteParamId = 8;
constexpr clap_id kSeedSpreadParamId = 9;
constexpr clap_id kDetuneParamId = 10;
constexpr clap_id kBreakpointsParamId = 11;
constexpr clap_id kAmplitudeStepParamId = 12;
constexpr clap_id kDurationStepParamId = 13;
constexpr clap_id kAmplitudeRangeParamId = 14;
constexpr clap_id kDurationRangeParamId = 15;
constexpr clap_id kFieldDensityParamId = 16;
constexpr clap_id kNeighborTransferParamId = 17;
constexpr clap_id kSelectionMemoryParamId = 18;
constexpr clap_id kFieldDurationParamId = 19;
constexpr clap_id kFieldContrastParamId = 20;
constexpr clap_id kAttackParamId = 21;
constexpr clap_id kDecayParamId = 22;
constexpr clap_id kSustainParamId = 23;
constexpr clap_id kReleaseParamId = 24;
constexpr clap_id kTopologyShapeParamId = 25;
constexpr clap_id kTopologyMotionParamId = 26;
constexpr clap_id kTopologyRateParamId = 27;
constexpr clap_id kTopologyAmountParamId = 28;
constexpr clap_id kTopologyDepthParamId = 29;
constexpr clap_id kTopologyScaleParamId = 30;
constexpr clap_id kTopologyCollapseParamId = 31;
constexpr clap_id kTopologyTwistParamId = 32;
constexpr clap_id kAzimuthParamId = 33;
constexpr clap_id kElevationParamId = 34;
constexpr clap_id kDistanceParamId = 35;
constexpr clap_id kSpatialFollowParamId = 36;
constexpr clap_id kOutputParamId = 37;
constexpr clap_id kFrequencyFloorParamId = 38;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiStochasticParams params {};
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
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiKinetic {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiNeighborInfluence {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiSelectionPulse {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticMaxVoices> guiNeighbor {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticMaxVoices> guiSecondaryNeighbor {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticMaxVoices> guiCurrentGenerator {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticMaxVoices> guiNextGenerator {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticMaxVoices> guiFieldActive {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxVoices> guiFrequency {};
    std::array<std::atomic<float>, kGuiWaveSamples> guiCurrentWaveform {};
    std::array<std::atomic<float>, kGuiWaveSamples> guiNextWaveform {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxBreakpoints> guiBreakpointPosition {};
    std::array<std::atomic<float>, s3g::kAmbiStochasticMaxBreakpoints> guiBreakpointAmplitude {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiStochasticHistorySize> guiHistory {};
    std::atomic<uint32_t> guiBreakpointCount { 0u };
    std::atomic<uint32_t> guiHistoryCursor { 0u };
    std::atomic<float> guiAmplitudeBarrier { 0.0f };
    std::atomic<float> guiDurationBarrier { 0.0f };
    std::atomic<float> guiGlobalEnergy { 0.0f };
    std::atomic<float> guiGlobalKinetic { 0.0f };
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
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
    case kSelectionParamId: plugin.params.selection = static_cast<s3g::AmbiStochasticSelection>(static_cast<uint32_t>(std::lround(value))); break;
    case kTransitionParamId: plugin.params.transition = static_cast<s3g::AmbiStochasticTransition>(static_cast<uint32_t>(std::lround(value))); break;
    case kAmplitudeDistributionParamId: plugin.params.amplitudeDistribution = static_cast<s3g::AmbiStochasticDistribution>(static_cast<uint32_t>(std::lround(value))); break;
    case kDurationDistributionParamId: plugin.params.durationDistribution = static_cast<s3g::AmbiStochasticDistribution>(static_cast<uint32_t>(std::lround(value))); break;
    case kBaseNoteParamId: plugin.params.baseNote = static_cast<float>(value); break;
    case kSeedSpreadParamId: plugin.params.seedSpreadSemitones = static_cast<float>(value); break;
    case kDetuneParamId: plugin.params.detuneCents = static_cast<float>(value); break;
    case kFrequencyFloorParamId: plugin.params.frequencyFloorHz = static_cast<float>(value); break;
    case kBreakpointsParamId: plugin.params.breakpoints = static_cast<uint32_t>(std::lround(value)); break;
    case kAmplitudeStepParamId: plugin.params.amplitudeStep = static_cast<float>(value); break;
    case kDurationStepParamId: plugin.params.durationStep = static_cast<float>(value); break;
    case kAmplitudeRangeParamId: plugin.params.amplitudeRange = static_cast<float>(value); break;
    case kDurationRangeParamId: plugin.params.durationRange = static_cast<float>(value); break;
    case kFieldDensityParamId: plugin.params.fieldDensity = static_cast<float>(value); break;
    case kNeighborTransferParamId: plugin.params.neighborTransfer = static_cast<float>(value); break;
    case kSelectionMemoryParamId: plugin.params.selectionMemory = static_cast<float>(value); break;
    case kFieldDurationParamId: plugin.params.fieldDurationSeconds = static_cast<float>(value); break;
    case kFieldContrastParamId: plugin.params.fieldContrast = static_cast<float>(value); break;
    case kAttackParamId: plugin.params.attackMs = static_cast<float>(value); break;
    case kDecayParamId: plugin.params.decayMs = static_cast<float>(value); break;
    case kSustainParamId: plugin.params.sustain = static_cast<float>(value); break;
    case kReleaseParamId: plugin.params.releaseMs = static_cast<float>(value); break;
    case kTopologyShapeParamId: plugin.params.topologyShape = static_cast<uint32_t>(std::lround(value)); break;
    case kTopologyMotionParamId: plugin.params.topologyMotion = static_cast<uint32_t>(std::lround(value)); break;
    case kTopologyRateParamId: plugin.params.topologyRateHz = static_cast<float>(value); break;
    case kTopologyAmountParamId: plugin.params.topologyAmount = static_cast<float>(value); break;
    case kTopologyDepthParamId: plugin.params.topologyDepth = static_cast<float>(value); break;
    case kTopologyScaleParamId: plugin.params.topologyScale = static_cast<float>(value); break;
    case kTopologyCollapseParamId: plugin.params.topologyCollapse = static_cast<float>(value); break;
    case kTopologyTwistParamId: plugin.params.topologyTwist = static_cast<float>(value); break;
    case kAzimuthParamId: plugin.params.centerAzimuthDeg = static_cast<float>(value); break;
    case kElevationParamId: plugin.params.centerElevationDeg = static_cast<float>(value); break;
    case kDistanceParamId: plugin.params.centerDistance = static_cast<float>(value); break;
    case kSpatialFollowParamId: plugin.params.spatialFollow = static_cast<float>(value); break;
    case kOutputParamId: plugin.params.outputGainDb = static_cast<float>(value); break;
    default: return;
    }
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
}

void readEvents(Plugin& plugin, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
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
    const auto& secondary = plugin.engine.secondaryNeighborIndices();
    for (uint32_t voice = 0u; voice < s3g::kAmbiStochasticMaxVoices; ++voice) {
        plugin.guiAzimuth[voice].store(points[voice].azimuthDeg, std::memory_order_relaxed);
        plugin.guiElevation[voice].store(points[voice].elevationDeg, std::memory_order_relaxed);
        plugin.guiDistance[voice].store(points[voice].distance, std::memory_order_relaxed);
        const auto topology = plugin.engine.topologyPosition(voice);
        plugin.guiTopologyX[voice].store(topology.x, std::memory_order_relaxed);
        plugin.guiTopologyY[voice].store(topology.y, std::memory_order_relaxed);
        plugin.guiTopologyZ[voice].store(topology.z, std::memory_order_relaxed);
        plugin.guiEnergy[voice].store(plugin.engine.voiceEnergy(voice), std::memory_order_relaxed);
        plugin.guiKinetic[voice].store(plugin.engine.voiceKinetic(voice), std::memory_order_relaxed);
        plugin.guiNeighborInfluence[voice].store(plugin.engine.voiceContact(voice), std::memory_order_relaxed);
        plugin.guiSelectionPulse[voice].store(plugin.engine.voiceNetworkPulse(voice), std::memory_order_relaxed);
        plugin.guiNeighbor[voice].store(neighbors[voice], std::memory_order_relaxed);
        plugin.guiSecondaryNeighbor[voice].store(secondary[voice], std::memory_order_relaxed);
        plugin.guiCurrentGenerator[voice].store(plugin.engine.currentGenerator(voice), std::memory_order_relaxed);
        plugin.guiNextGenerator[voice].store(plugin.engine.nextGenerator(voice), std::memory_order_relaxed);
        plugin.guiFieldActive[voice].store(plugin.engine.voiceFieldActive(voice) ? 1u : 0u, std::memory_order_relaxed);
        plugin.guiFrequency[voice].store(plugin.engine.voiceFrequency(voice), std::memory_order_relaxed);
    }
    plugin.guiGlobalEnergy.store(plugin.engine.globalEnergy(), std::memory_order_relaxed);
    plugin.guiGlobalKinetic.store(plugin.engine.globalKinetic(), std::memory_order_relaxed);
    const uint32_t selected = std::min<uint32_t>(plugin.guiSelectedVoice.load(std::memory_order_relaxed),
        std::max<uint32_t>(1u, plugin.params.voices) - 1u);
    const auto& currentWave = plugin.engine.waveform(selected);
    const auto& nextWave = plugin.engine.nextWaveform(selected);
    for (uint32_t sample = 0u; sample < kGuiWaveSamples; ++sample) {
        plugin.guiCurrentWaveform[sample].store(currentWave[sample * 2u], std::memory_order_relaxed);
        plugin.guiNextWaveform[sample].store(nextWave[sample * 2u], std::memory_order_relaxed);
    }
    const uint32_t breakpointCount = plugin.engine.breakpointCount(selected);
    float durationTotal = 0.0f;
    for (uint32_t point = 0u; point < breakpointCount; ++point) {
        durationTotal += plugin.engine.breakpointDuration(selected, point);
    }
    float position = 0.0f;
    for (uint32_t point = 0u; point < breakpointCount; ++point) {
        plugin.guiBreakpointPosition[point].store(position / std::max(0.000001f, durationTotal), std::memory_order_relaxed);
        plugin.guiBreakpointAmplitude[point].store(plugin.engine.breakpointAmplitude(selected, point), std::memory_order_relaxed);
        position += plugin.engine.breakpointDuration(selected, point);
    }
    plugin.guiBreakpointCount.store(breakpointCount, std::memory_order_release);
    const auto& history = plugin.engine.selectionHistory(selected);
    for (uint32_t item = 0u; item < s3g::kAmbiStochasticHistorySize; ++item) {
        plugin.guiHistory[item].store(history[item], std::memory_order_relaxed);
    }
    plugin.guiHistoryCursor.store(plugin.engine.selectionHistoryCursor(selected), std::memory_order_relaxed);
    plugin.guiAmplitudeBarrier.store(plugin.engine.amplitudePrimaryBarrier(selected), std::memory_order_relaxed);
    plugin.guiDurationBarrier.store(plugin.engine.durationPrimaryBarrier(selected), std::memory_order_relaxed);
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
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel) {
        state->scratch[channel].assign(state->maxFrames, 0.0f);
        state->scratchPointers[channel] = state->scratch[channel].data();
    }
    state->engine.prepare(sampleRate);
    state->engine.setParams(state->params);
    state->engine.reset();
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
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            if (output.data32 && output.data32[channel]) std::fill(output.data32[channel], output.data32[channel] + frames, 0.0f);
            if (output.data64 && output.data64[channel]) std::fill(output.data64[channel], output.data64[channel] + frames, 0.0);
        }
        return CLAP_PROCESS_CONTINUE;
    }

    std::array<float*, kOutputChannels> outputs {};
    const bool useScratch = output.data32 == nullptr;
    for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
        outputs[channel] = useScratch ? state->scratchPointers[channel] : output.data32[channel];
    }
    state->engine.process(outputs.data(), outputChannels, frames);

    float blockPeak = 0.0f;
    for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
        if (!outputs[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = outputs[channel][frame];
            blockPeak = std::max(blockPeak, std::fabs(value));
            if (useScratch && output.data64 && output.data64[channel]) output.data64[channel][frame] = value;
        }
    }
    for (uint32_t channel = outputChannels; channel < output.channel_count; ++channel) {
        if (output.data32 && output.data32[channel]) std::fill(output.data32[channel], output.data32[channel] + frames, 0.0f);
        if (output.data64 && output.data64[channel]) std::fill(output.data64[channel], output.data64[channel] + frames, 0.0);
    }
    state->outputPeak.store(std::max(state->outputPeak.load(std::memory_order_relaxed) * 0.92f, blockPeak),
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
    { kSelectionParamId, "Selection", 0.0, 5.0, 5.0, true },
    { kTransitionParamId, "Transition", 0.0, 2.0, 0.0, true },
    { kAmplitudeDistributionParamId, "Amplitude Distribution", 0.0, 6.0, 2.0, true },
    { kDurationDistributionParamId, "Duration Distribution", 0.0, 6.0, 3.0, true },
    { kBaseNoteParamId, "Base Seed", 12.0, 96.0, 40.0, false },
    { kSeedSpreadParamId, "Seed Spread", 0.0, 48.0, 19.0, false },
    { kDetuneParamId, "Seed Deviation", 0.0, 100.0, 9.0, false },
    { kFrequencyFloorParamId, "Frequency Floor", 2.0, 240.0, 18.0, false },
    { kBreakpointsParamId, "Breakpoints", 4.0, 32.0, 16.0, true },
    { kAmplitudeStepParamId, "Amplitude Step", 0.0, 1.0, 0.58, false },
    { kDurationStepParamId, "Duration Step", 0.0, 1.0, 0.52, false },
    { kAmplitudeRangeParamId, "Amplitude Range", 0.0, 1.0, 0.65, false },
    { kDurationRangeParamId, "Duration Range", 0.0, 1.0, 0.78, false },
    { kFieldDensityParamId, "Field Density", 0.0, 1.0, 0.94, false },
    { kNeighborTransferParamId, "Neighbor Transfer", 0.0, 1.0, 0.32, false },
    { kSelectionMemoryParamId, "Selection Memory", 0.0, 1.0, 0.82, false },
    { kFieldDurationParamId, "Field Duration", 0.05, 30.0, 0.45, false },
    { kFieldContrastParamId, "Field Contrast", 0.0, 1.0, 0.58, false },
    { kAttackParamId, "Attack", 1.0, 4000.0, 12.0, false },
    { kDecayParamId, "Decay", 5.0, 8000.0, 180.0, false },
    { kSustainParamId, "Sustain", 0.0, 1.0, 0.82, false },
    { kReleaseParamId, "Release", 5.0, 12000.0, 420.0, false },
    { kTopologyShapeParamId, "Topology Shape", 0.0, 11.0, 11.0, true },
    { kTopologyMotionParamId, "Topology Animation", 0.0, 17.0, 1.0, true },
    { kTopologyRateParamId, "Topology Rate", 0.001, 1.0, 0.035, false },
    { kTopologyAmountParamId, "Topology Amount", 0.0, 1.0, 0.82, false },
    { kTopologyDepthParamId, "Topology Depth", 0.0, 1.0, 0.78, false },
    { kTopologyScaleParamId, "Topology Scale", 0.25, 2.0, 1.20, false },
    { kTopologyCollapseParamId, "Topology Collapse", 0.0, 1.0, 0.0, false },
    { kTopologyTwistParamId, "Topology Twist", -1.0, 1.0, 0.0, false },
    { kAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Center Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Center Distance", 0.15, 2.0, 1.0, false },
    { kSpatialFollowParamId, "Spatial Follow", 0.0, 1.0, 0.92, false },
    { kOutputParamId, "Output", -60.0, 6.0, -24.0, false },
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
    case kSelectionParamId: *value = static_cast<uint32_t>(params.selection); return true;
    case kTransitionParamId: *value = static_cast<uint32_t>(params.transition); return true;
    case kAmplitudeDistributionParamId: *value = static_cast<uint32_t>(params.amplitudeDistribution); return true;
    case kDurationDistributionParamId: *value = static_cast<uint32_t>(params.durationDistribution); return true;
    case kBaseNoteParamId: *value = params.baseNote; return true;
    case kSeedSpreadParamId: *value = params.seedSpreadSemitones; return true;
    case kDetuneParamId: *value = params.detuneCents; return true;
    case kFrequencyFloorParamId: *value = params.frequencyFloorHz; return true;
    case kBreakpointsParamId: *value = params.breakpoints; return true;
    case kAmplitudeStepParamId: *value = params.amplitudeStep; return true;
    case kDurationStepParamId: *value = params.durationStep; return true;
    case kAmplitudeRangeParamId: *value = params.amplitudeRange; return true;
    case kDurationRangeParamId: *value = params.durationRange; return true;
    case kFieldDensityParamId: *value = params.fieldDensity; return true;
    case kNeighborTransferParamId: *value = params.neighborTransfer; return true;
    case kSelectionMemoryParamId: *value = params.selectionMemory; return true;
    case kFieldDurationParamId: *value = params.fieldDurationSeconds; return true;
    case kFieldContrastParamId: *value = params.fieldContrast; return true;
    case kAttackParamId: *value = params.attackMs; return true;
    case kDecayParamId: *value = params.decayMs; return true;
    case kSustainParamId: *value = params.sustain; return true;
    case kReleaseParamId: *value = params.releaseMs; return true;
    case kTopologyShapeParamId: *value = params.topologyShape; return true;
    case kTopologyMotionParamId: *value = params.topologyMotion; return true;
    case kTopologyRateParamId: *value = params.topologyRateHz; return true;
    case kTopologyAmountParamId: *value = params.topologyAmount; return true;
    case kTopologyDepthParamId: *value = params.topologyDepth; return true;
    case kTopologyScaleParamId: *value = params.topologyScale; return true;
    case kTopologyCollapseParamId: *value = params.topologyCollapse; return true;
    case kTopologyTwistParamId: *value = params.topologyTwist; return true;
    case kAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kElevationParamId: *value = params.centerElevationDeg; return true;
    case kDistanceParamId: *value = params.centerDistance; return true;
    case kSpatialFollowParamId: *value = params.spatialFollow; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kModeParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticModeName(static_cast<s3g::AmbiStochasticMode>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kSelectionParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticSelectionName(static_cast<s3g::AmbiStochasticSelection>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kTransitionParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticTransitionName(static_cast<s3g::AmbiStochasticTransition>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kAmplitudeDistributionParamId || id == kDurationDistributionParamId) {
        std::snprintf(display, size, "%s", s3g::ambiStochasticDistributionName(static_cast<s3g::AmbiStochasticDistribution>(static_cast<uint32_t>(std::lround(value)))));
    } else if (id == kTopologyShapeParamId) {
        std::snprintf(display, size, "%s", s3g::topologyShapeName(static_cast<uint32_t>(std::lround(value))));
    } else if (id == kTopologyMotionParamId) {
        std::snprintf(display, size, "%s", s3g::topologyMotionModeName(static_cast<uint32_t>(std::lround(value))));
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kVoicesParamId || id == kBreakpointsParamId) {
        std::snprintf(display, size, "%.0f", value);
    } else if (id == kBaseNoteParamId) {
        std::snprintf(display, size, "M%.1f", value);
    } else if (id == kSeedSpreadParamId) {
        std::snprintf(display, size, "%.1f ST", value);
    } else if (id == kDetuneParamId) {
        std::snprintf(display, size, "%.1f CT", value);
    } else if (id == kFrequencyFloorParamId) {
        std::snprintf(display, size, "%.1f HZ", value);
    } else if (id == kAttackParamId || id == kDecayParamId || id == kReleaseParamId) {
        std::snprintf(display, size, "%.0f MS", value);
    } else if (id == kFieldDurationParamId) {
        std::snprintf(display, size, "%.2f S", value);
    } else if (id == kTopologyRateParamId) {
        std::snprintf(display, size, "%.3f HZ", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.0f DEG", value);
    } else if (id == kDistanceParamId || id == kTopologyScaleParamId) {
        std::snprintf(display, size, "%.2f", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f DB", value);
    } else if (id == kTopologyTwistParamId) {
        std::snprintf(display, size, "%+.0f%%", value * 100.0);
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
#endif
    return stream->write(stream, &saved, sizeof(saved)) == static_cast<int64_t>(sizeof(saved));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState saved {};
    auto* destination = reinterpret_cast<uint8_t*>(&saved);
    uint64_t remaining = sizeof(saved);
    while (remaining > 0u) {
        const int64_t read = stream->read(stream, destination, remaining);
        if (read <= 0) return false;
        destination += read;
        remaining -= static_cast<uint64_t>(read);
    }
    if (saved.version != kStateVersion) return false;
    auto* state = self(plugin);
    state->params = saved.params;
    state->engine.setParams(state->params);
    state->params = state->engine.params();
#if defined(__APPLE__)
    state->guiViewMode = saved.guiViewMode;
    state->guiViewAzDeg = saved.guiViewAzDeg;
    state->guiViewElDeg = saved.guiViewElDeg;
    state->guiViewZoom = saved.guiViewZoom;
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
    { kVoicesParamId, 630, 130, 1.0, 64.0, false },
    { kBaseNoteParamId, 630, 156, 12.0, 96.0, false },
    { kSeedSpreadParamId, 630, 182, 0.0, 48.0, false },
    { kDetuneParamId, 630, 208, 0.0, 100.0, false },
    { kFrequencyFloorParamId, 630, 234, 2.0, 240.0, true },
    { kBreakpointsParamId, 630, 370, 4.0, 32.0, false },
    { kAmplitudeStepParamId, 630, 396, 0.0, 1.0, false },
    { kDurationStepParamId, 630, 422, 0.0, 1.0, false },
    { kAmplitudeRangeParamId, 630, 448, 0.0, 1.0, false },
    { kDurationRangeParamId, 630, 474, 0.0, 1.0, false },
    { kAttackParamId, 630, 558, 1.0, 4000.0, true },
    { kDecayParamId, 630, 584, 5.0, 8000.0, true },
    { kSustainParamId, 630, 610, 0.0, 1.0, false },
    { kReleaseParamId, 630, 636, 5.0, 12000.0, true },
    { kOutputParamId, 630, 720, -60.0, 6.0, false },
    { kNeighborTransferParamId, 896, 130, 0.0, 1.0, false },
    { kSelectionMemoryParamId, 896, 156, 0.0, 1.0, false },
    { kFieldDensityParamId, 896, 240, 0.0, 1.0, false },
    { kFieldDurationParamId, 896, 266, 0.05, 30.0, true },
    { kFieldContrastParamId, 896, 292, 0.0, 1.0, false },
    { kTopologyRateParamId, 896, 428, 0.001, 1.0, true },
    { kTopologyAmountParamId, 896, 454, 0.0, 1.0, false },
    { kTopologyDepthParamId, 896, 480, 0.0, 1.0, false },
    { kTopologyScaleParamId, 896, 506, 0.25, 2.0, false },
    { kTopologyCollapseParamId, 896, 532, 0.0, 1.0, false },
    { kTopologyTwistParamId, 896, 558, -1.0, 1.0, false },
    { kAzimuthParamId, 896, 642, -180.0, 180.0, false },
    { kElevationParamId, 896, 668, -90.0, 90.0, false },
    { kDistanceParamId, 896, 694, 0.15, 2.0, false },
    { kSpatialFollowParamId, 896, 720, 0.0, 1.0, false },
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
    if (spec.logarithmic) return std::log(value / spec.minimum) / std::log(spec.maximum / spec.minimum);
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
- (NSRect)waveRect { return NSMakeRect(34, 584, 564, 174); }
- (NSRect)historyRect { return NSMakeRect(34, 774, 564, 50); }

- (NSRect)viewButtonRect:(int)index
{
    return NSMakeRect(430.0 + static_cast<CGFloat>(index) * 49.0, 46.0, 43.0, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    return NSMakeRect(378.0 + static_cast<CGFloat>(index) * 23.0, 46.0, 18.0, 13.0);
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
    return std::min(rect.size.width, rect.size.height) * 0.36 * std::clamp(_viewZoom, 0.55, 2.20);
}

- (NSPoint)projectWorld:(s3g::Vec3)point depth:(CGFloat*)depth
{
    const NSRect rect = [self fieldRect];
    const CGFloat centerX = NSMidX(rect);
    const CGFloat centerY = NSMidY(rect);
    const CGFloat scale = [self viewScale];
    if (_viewMode == 0) {
        if (depth) *depth = point.y;
        return NSMakePoint(centerX - point.x * scale, centerY - point.z * scale);
    }
    if (_viewMode == 1) {
        if (depth) *depth = point.x;
        return NSMakePoint(centerX - point.z * scale, centerY - point.y * scale);
    }
    const float azimuth = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.z;
    const float z1 = sa * point.x + ca * point.z;
    const float y2 = ce * point.y - se * z1;
    const float z2 = se * point.y + ce * z1;
    if (depth) *depth = z2;
    return NSMakePoint(centerX - x1 * scale, centerY - y2 * scale);
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

- (s3g::Vec3)topologyWorld:(uint32_t)index
{
    return {
        _plugin->guiTopologyX[index].load(std::memory_order_relaxed),
        _plugin->guiTopologyY[index].load(std::memory_order_relaxed),
        _plugin->guiTopologyZ[index].load(std::memory_order_relaxed)
    };
}

- (NSPoint)projectVoice:(uint32_t)index depth:(CGFloat*)depth
{
    return [self projectWorld:[self topologyWorld:index] depth:depth];
}

- (int)hitPoint:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiStochasticMaxVoices);
    int best = -1;
    CGFloat bestDistance = 14.0;
    for (uint32_t voice = 0u; voice < voices; ++voice) {
        const NSPoint projected = [self projectVoice:voice depth:nullptr];
        const CGFloat distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(voice);
        }
    }
    return best;
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"VOICE FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4" };
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
    for (int index = 0; index < 3; ++index) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:index], header, viewLabels[index], index == _viewMode, attrs, style);
    }

    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1, 1)] addClip];
    const CGFloat radius = [self viewScale];
    [s3g::clap_gui::color(0x303030) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMidX(field) - radius,
        NSMidY(field) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];

    const uint32_t voices = std::clamp<uint32_t>(_plugin->params.voices, 1u, s3g::kAmbiStochasticMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    _plugin->guiSelectedVoice.store(_selectedVoice, std::memory_order_relaxed);
    std::array<NSPoint, s3g::kAmbiStochasticMaxVoices> projected {};
    for (uint32_t voice = 0u; voice < voices; ++voice) projected[voice] = [self projectVoice:voice depth:nullptr];

    for (uint32_t voice = 0u; voice < voices; ++voice) {
        const uint32_t neighbors[] {
            std::min<uint32_t>(_plugin->guiNeighbor[voice].load(std::memory_order_relaxed), voices - 1u),
            std::min<uint32_t>(_plugin->guiSecondaryNeighbor[voice].load(std::memory_order_relaxed), voices - 1u)
        };
        const float influence = _plugin->guiNeighborInfluence[voice].load(std::memory_order_relaxed);
        const float pulse = _plugin->guiSelectionPulse[voice].load(std::memory_order_relaxed);
        for (uint32_t edge = 0u; edge < 2u; ++edge) {
            if (neighbors[edge] == voice || neighbors[edge] < voice) continue;
            NSBezierPath* link = [NSBezierPath bezierPath];
            [link moveToPoint:projected[voice]];
            [link lineToPoint:projected[neighbors[edge]]];
            [s3g::clap_gui::color(edge == 0u ? 0xa0a0a0 : 0x686868,
                (edge == 0u ? 0.12 : 0.06) + influence * 0.32 + pulse * 0.22) setStroke];
            [link setLineWidth:0.55 + influence * 0.75];
            [link stroke];
        }
    }

    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x080808), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0u; voice < voices; ++voice) {
        const bool selected = voice == _selectedVoice;
        const bool active = _plugin->guiFieldActive[voice].load(std::memory_order_relaxed) != 0u;
        const float energy = _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
        const float kinetic = _plugin->guiKinetic[voice].load(std::memory_order_relaxed);
        const CGFloat baseSize = voices > 32u ? 7.0 : 9.0;
        const CGFloat size = (selected ? baseSize + 5.0 : baseSize) + std::clamp(energy * 9.0f + kinetic * 2.0f, 0.0f, 6.0f);
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        const auto point = [self snapshotPoint:voice];
        [[pointColor(point.azimuthDeg, point.elevationDeg, point.distance, selected)
            colorWithAlphaComponent:active ? (selected ? 1.0 : 0.82) : 0.22] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe0e0e0 : (active ? 0x565656 : 0x2a2a2a)) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5,
            NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
    }
    [NSGraphicsContext restoreGraphicsState];

    const auto topology = [self topologyWorld:_selectedVoice];
    const uint32_t neighbor = std::min<uint32_t>(_plugin->guiNeighbor[_selectedVoice].load(std::memory_order_relaxed), voices - 1u);
    const uint32_t current = _plugin->guiCurrentGenerator[_selectedVoice].load(std::memory_order_relaxed) + 1u;
    const uint32_t next = _plugin->guiNextGenerator[_selectedVoice].load(std::memory_order_relaxed) + 1u;
    const float frequency = _plugin->guiFrequency[_selectedVoice].load(std::memory_order_relaxed);
    const bool active = _plugin->guiFieldActive[_selectedVoice].load(std::memory_order_relaxed) != 0u;
    NSString* readout = [NSString stringWithFormat:@"V%02u  G%u>G%u  %.1fHZ  %@  N%02u",
        _selectedVoice + 1u, current, next, frequency, active ? @"ON" : @"REST", neighbor + 1u];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
    [@"X PRESSURE     Y EVENTS     Z PERIOD     R DRIVE" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19)
        withAttributes:valueAttrs];
    NSString* coordinates = [NSString stringWithFormat:@"%+.2f  %+.2f  %+.2f", topology.x, topology.y, topology.z];
    s3g::clap_gui::drawRightStatus(coordinates, NSMaxX(field), NSMaxY(field) - 22, valueAttrs, 8.0);
}

- (void)drawPressure:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self wavePanelRect];
    const NSRect wave = [self waveRect];
    const NSRect history = [self historyRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PRESSURE CURVE", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const uint32_t current = _plugin->guiCurrentGenerator[_selectedVoice].load(std::memory_order_relaxed) + 1u;
    const uint32_t next = _plugin->guiNextGenerator[_selectedVoice].load(std::memory_order_relaxed) + 1u;
    NSString* status = [NSString stringWithFormat:@"G%u > G%u   A1 +/-%0.3f   T1 +/-%0.1f SAMP",
        current, next, _plugin->guiAmplitudeBarrier.load(std::memory_order_relaxed),
        _plugin->guiDurationBarrier.load(std::memory_order_relaxed)];
    s3g::clap_gui::drawRightStatus(status, NSMaxX(panel), panel.origin.y + 5, valueAttrs, 8.0);
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(wave);
    NSRectFill(history);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(wave);
    NSFrameRect(history);
    [s3g::clap_gui::color(0x292929) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(wave.origin.x + 1, NSMidY(wave))
                              toPoint:NSMakePoint(NSMaxX(wave) - 1, NSMidY(wave))];
    for (uint32_t division = 1u; division < 4u; ++division) {
        const CGFloat x = wave.origin.x + wave.size.width * static_cast<CGFloat>(division) / 4.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, wave.origin.y + 1)
                                  toPoint:NSMakePoint(x, NSMaxY(wave) - 1)];
    }

    NSBezierPath* currentCurve = [NSBezierPath bezierPath];
    NSBezierPath* nextCurve = [NSBezierPath bezierPath];
    for (uint32_t sample = 0u; sample < kGuiWaveSamples; ++sample) {
        const CGFloat x = wave.origin.x + static_cast<CGFloat>(sample) / static_cast<CGFloat>(kGuiWaveSamples - 1u) * wave.size.width;
        const NSPoint currentPoint = NSMakePoint(x, NSMidY(wave)
            - _plugin->guiCurrentWaveform[sample].load(std::memory_order_relaxed) * wave.size.height * 0.42);
        const NSPoint nextPoint = NSMakePoint(x, NSMidY(wave)
            - _plugin->guiNextWaveform[sample].load(std::memory_order_relaxed) * wave.size.height * 0.42);
        if (sample == 0u) {
            [currentCurve moveToPoint:currentPoint];
            [nextCurve moveToPoint:nextPoint];
        } else {
            [currentCurve lineToPoint:currentPoint];
            [nextCurve lineToPoint:nextPoint];
        }
    }
    [s3g::clap_gui::color(0x686868, 0.72) setStroke];
    [currentCurve setLineWidth:0.8];
    [currentCurve stroke];
    const auto selectedPoint = [self snapshotPoint:_selectedVoice];
    [[pointColor(selectedPoint.azimuthDeg, selectedPoint.elevationDeg, selectedPoint.distance, true)
        colorWithAlphaComponent:0.92] setStroke];
    [nextCurve setLineWidth:1.25];
    [nextCurve stroke];

    const uint32_t count = std::min<uint32_t>(_plugin->guiBreakpointCount.load(std::memory_order_acquire),
        s3g::kAmbiStochasticMaxBreakpoints);
    for (uint32_t point = 0u; point < count; ++point) {
        const float position = _plugin->guiBreakpointPosition[point].load(std::memory_order_relaxed);
        const float amplitude = _plugin->guiBreakpointAmplitude[point].load(std::memory_order_relaxed);
        const NSPoint marker = NSMakePoint(wave.origin.x + position * wave.size.width,
            NSMidY(wave) - amplitude * wave.size.height * 0.42);
        [s3g::clap_gui::color(0xc0c0c0, 0.82) setFill];
        NSRectFill(NSMakeRect(marker.x - 1.5, marker.y - 1.5, 3.0, 3.0));
    }

    [@"SELECTION HISTORY" drawAtPoint:NSMakePoint(history.origin.x + 8, history.origin.y + 7) withAttributes:valueAttrs];
    const CGFloat cellY = history.origin.y + 27;
    const CGFloat cellWidth = (history.size.width - 16.0) / static_cast<CGFloat>(s3g::kAmbiStochasticHistorySize);
    const uint32_t cursor = _plugin->guiHistoryCursor.load(std::memory_order_relaxed);
    static const uint32_t shades[] { 0x4a4a4a, 0x707070, 0x999999, 0xc8c8c8 };
    for (uint32_t offset = 0u; offset < s3g::kAmbiStochasticHistorySize; ++offset) {
        const uint32_t index = (cursor + offset) % s3g::kAmbiStochasticHistorySize;
        const uint32_t generator = std::min<uint32_t>(_plugin->guiHistory[index].load(std::memory_order_relaxed), 3u);
        [s3g::clap_gui::color(shades[generator], 0.86) setFill];
        NSRectFill(NSMakeRect(history.origin.x + 8.0 + offset * cellWidth, cellY, std::max<CGFloat>(1.0, cellWidth - 2.0), 12.0));
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

- (void)drawMenu:(NSString*)name value:(NSString*)value panelX:(CGFloat)panelX y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto& params = _plugin->params;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 228, style);
    s3g::clap_gui::drawPanelHeader(@"ENGINE", true, 630, 42, 250, 21, attrs, style);
    [self drawMenu:@"MODE" value:[NSString stringWithUTF8String:s3g::ambiStochasticModeName(params.mode)] panelX:630 y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", params.order] panelX:630 y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:params.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BASE" param:kBaseNoteParamId value:params.baseNote attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPREAD" param:kSeedSpreadParamId value:params.seedSpreadSemitones attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEV" param:kDetuneParamId value:params.detuneCents attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"LOW" param:kFrequencyFloorParamId value:params.frequencyFloorHz attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 282, 250, 228, style);
    s3g::clap_gui::drawPanelHeader(@"SECOND-ORDER WALK", true, 630, 282, 250, 21, attrs, style);
    [self drawMenu:@"A-DIST" value:[NSString stringWithUTF8String:s3g::ambiStochasticDistributionName(params.amplitudeDistribution)] panelX:630 y:318 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"T-DIST" value:[NSString stringWithUTF8String:s3g::ambiStochasticDistributionName(params.durationDistribution)] panelX:630 y:344 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"PTS" param:kBreakpointsParamId value:params.breakpoints attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"A-STEP" param:kAmplitudeStepParamId value:params.amplitudeStep attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"T-STEP" param:kDurationStepParamId value:params.durationStep attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"A-RANGE" param:kAmplitudeRangeParamId value:params.amplitudeRange attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"T-RANGE" param:kDurationRangeParamId value:params.durationRange attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 522, 250, 150, style);
    s3g::clap_gui::drawPanelHeader(@"ENVELOPE", true, 630, 522, 250, 21, attrs, style);
    [self drawSlider:@"ATTACK" param:kAttackParamId value:params.attackMs attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DECAY" param:kDecayParamId value:params.decayMs attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SUSTAIN" param:kSustainParamId value:params.sustain attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RELEASE" param:kReleaseParamId value:params.releaseMs attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 684, 250, 72, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, 630, 684, 250, 21, attrs, style);
    [self drawSlider:@"OUT" param:kOutputParamId value:params.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 150, style);
    s3g::clap_gui::drawPanelHeader(@"SELECTION", true, 896, 42, 246, 21, attrs, style);
    [self drawMenu:@"LAW" value:[NSString stringWithUTF8String:s3g::ambiStochasticSelectionName(params.selection)] panelX:896 y:78 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"JOIN" value:[NSString stringWithUTF8String:s3g::ambiStochasticTransitionName(params.transition)] panelX:896 y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"XFER" param:kNeighborTransferParamId value:params.neighborTransfer attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"MEM" param:kSelectionMemoryParamId value:params.selectionMemory attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 204, 246, 124, style);
    s3g::clap_gui::drawPanelHeader(@"TIME FIELDS", true, 896, 204, 246, 21, attrs, style);
    [self drawSlider:@"DENS" param:kFieldDensityParamId value:params.fieldDensity attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DUR" param:kFieldDurationParamId value:params.fieldDurationSeconds attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CONT" param:kFieldContrastParamId value:params.fieldContrast attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 340, 246, 254, style);
    s3g::clap_gui::drawPanelHeader(@"TOPOLOGY", true, 896, 340, 246, 21, attrs, style);
    [self drawMenu:@"SHAPE" value:[NSString stringWithUTF8String:s3g::topologyShapeName(params.topologyShape)] panelX:896 y:376 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ANIM" value:[NSString stringWithUTF8String:s3g::topologyMotionModeName(params.topologyMotion)] panelX:896 y:402 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RATE" param:kTopologyRateParamId value:params.topologyRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AMT" param:kTopologyAmountParamId value:params.topologyAmount attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEPTH" param:kTopologyDepthParamId value:params.topologyDepth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SCALE" param:kTopologyScaleParamId value:params.topologyScale attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"COLL" param:kTopologyCollapseParamId value:params.topologyCollapse attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TWIST" param:kTopologyTwistParamId value:params.topologyTwist attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 606, 246, 150, style);
    s3g::clap_gui::drawPanelHeader(@"PROJECTION", true, 896, 606, 246, 21, attrs, style);
    [self drawSlider:@"AZIM" param:kAzimuthParamId value:params.centerAzimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEV" param:kElevationParamId value:params.centerElevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DIST" param:kDistanceParamId value:params.centerDistance attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FOLLOW" param:kSpatialFollowParamId value:params.spatialFollow attrs:attrs valueAttrs:valueAttrs style:style];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* modeItems[] = { @"FREE", @"MIDI", @"BOTH" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* selectionItems[] = { @"RANDOM", @"SERIES", @"WEIGHT", @"TENDENCY", @"MARKOV", @"WALK" };
    static NSString* transitionItems[] = { @"LINK", @"MERGE", @"VARY" };
    static NSString* distributionItems[] = { @"UNIFORM", @"GAUSS", @"CAUCHY", @"LOGISTIC", @"ARCSINE", @"EXPON", @"BINARY" };
    static NSString* shapeItems[] = { @"AED", @"SHEAR", @"FOLD", @"VORTEX", @"PINCH", @"RUPTURE", @"SCATTER", @"MIRROR", @"WAVE", @"LINE", @"PLANE", @"FORSY" };
    static NSString* motionItems[] = { @"OFF", @"FREE", @"DRIFT", @"PULSE", @"ORBIT", @"FOLD", @"WEAVE", @"GRID", @"TRACE", @"HOVER", @"LEAP", @"FIELD", @"PAIR", @"FLOW", @"GROUP", @"MARCH", @"PATH", @"SCAT" };
    NSString** items = modeItems;
    int selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.mode));
    if (_openMenu == 2) {
        items = orderItems;
        selected = static_cast<int>(_plugin->params.order) - 1;
    } else if (_openMenu == 3) {
        items = selectionItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.selection));
    } else if (_openMenu == 4) {
        items = transitionItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.transition));
    } else if (_openMenu == 5) {
        items = distributionItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.amplitudeDistribution));
    } else if (_openMenu == 6) {
        items = distributionItems;
        selected = static_cast<int>(static_cast<uint32_t>(_plugin->params.durationDistribution));
    } else if (_openMenu == 7) {
        items = shapeItems;
        selected = static_cast<int>(_plugin->params.topologyShape);
    } else if (_openMenu == 8) {
        items = motionItems;
        selected = static_cast<int>(_plugin->params.topologyMotion);
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
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)),
        kGuiWidth, 14, valueAttrs, 18);
    [self drawField:attrs valueAttrs:valueAttrs style:style];
    [self drawPressure:attrs valueAttrs:valueAttrs style:style];
    [self drawPanels:attrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return NSMakeRect(738, 77, 124, 15);
    case 2: return NSMakeRect(738, 103, 124, 15);
    case 3: return NSMakeRect(1004, 77, 124, 15);
    case 4: return NSMakeRect(1004, 103, 124, 15);
    case 5: return NSMakeRect(738, 317, 124, 15);
    case 6: return NSMakeRect(738, 343, 124, 15);
    case 7: return NSMakeRect(1004, 375, 124, 15);
    case 8: return NSMakeRect(1004, 401, 124, 15);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return 3u;
    case 2: return 7u;
    case 3: return 6u;
    case 4: return 3u;
    case 5:
    case 6: return 7u;
    case 7: return 12u;
    case 8: return 18u;
    default: return 0u;
    }
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _menuItemCount = [self menuCount:menu];
    _hoverMenuItem = -1;
    const NSRect box = [self menuBoxRect:menu];
    CGFloat y = NSMaxY(box) + 2.0;
    const CGFloat height = 21.0 * _menuItemCount;
    if (y + height > kGuiHeight - 8.0) y = box.origin.y - height - 2.0;
    _openMenuRect = NSMakeRect(box.origin.x, y, box.size.width, height);
    [self setNeedsDisplay:YES];
}

- (void)chooseMenuItem:(int)item
{
    if (item < 0) return;
    switch (_openMenu) {
    case 1: applyParam(*_plugin, kModeParamId, item); break;
    case 2: applyParam(*_plugin, kOrderParamId, item + 1); break;
    case 3: applyParam(*_plugin, kSelectionParamId, item); break;
    case 4: applyParam(*_plugin, kTransitionParamId, item); break;
    case 5: applyParam(*_plugin, kAmplitudeDistributionParamId, item); break;
    case 6: applyParam(*_plugin, kDurationDistributionParamId, item); break;
    case 7: applyParam(*_plugin, kTopologyShapeParamId, item); break;
    case 8: applyParam(*_plugin, kTopologyMotionParamId, item); break;
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
    if (NSPointInRect(point, [self fieldRect])) {
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
    "0.2.0",
    "Second-order stochastic generator banks with topology-driven ACN/SN3D output.",
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
