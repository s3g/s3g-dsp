#include "s3g_ambi_pulsar_encoder.h"
#include "s3g_ambi_pulsar_presets.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiPulsarMaxChannels;
constexpr uint32_t kStateVersion = 2u;

constexpr clap_id kPresetParamId = 1u;
constexpr clap_id kOrderParamId = 2u;
constexpr clap_id kEmissionParamId = 3u;
constexpr clap_id kEmissionModRateParamId = 4u;
constexpr clap_id kEmissionModDepthParamId = 5u;
constexpr clap_id kFormantModRateParamId = 6u;
constexpr clap_id kFormantModDepthParamId = 7u;
constexpr clap_id kFormantScatterParamId = 8u;
constexpr clap_id kPhaseScatterParamId = 9u;
constexpr clap_id kProbabilityParamId = 10u;
constexpr clap_id kBurstOnParamId = 11u;
constexpr clap_id kBurstOffParamId = 12u;
constexpr clap_id kSieveModuloParamId = 13u;
constexpr clap_id kSieveResidueParamId = 14u;
constexpr clap_id kDistributionParamId = 15u;

constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneFormantIds {{ 20u, 25u, 30u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneOverlapIds {{ 21u, 26u, 31u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneLevelIds {{ 22u, 27u, 32u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneOffsetIds {{ 23u, 28u, 33u }};
constexpr std::array<clap_id, s3g::kAmbiPulsarLanes> kLaneWaveformIds {{ 24u, 29u, 34u }};

constexpr clap_id kEnvelopeParamId = 35u;
constexpr clap_id kEnvelopeEdgeParamId = 36u;
constexpr clap_id kQualityParamId = 37u;
constexpr clap_id kAzimuthParamId = 38u;
constexpr clap_id kElevationParamId = 39u;
constexpr clap_id kDistanceParamId = 40u;
constexpr clap_id kWidthParamId = 41u;
constexpr clap_id kSpatialScatterParamId = 42u;
constexpr clap_id kOrbitRateParamId = 43u;
constexpr clap_id kOrbitDepthParamId = 44u;
constexpr clap_id kSpatialFollowParamId = 45u;
constexpr clap_id kAirParamId = 46u;
constexpr clap_id kDopplerParamId = 47u;
constexpr clap_id kOutputParamId = 48u;

constexpr clap_id kNeuralLevelParamId = 60u;
constexpr clap_id kNeuralDriveParamId = 61u;
constexpr clap_id kNeuralFeedbackParamId = 62u;
constexpr clap_id kNeuralCouplingParamId = 63u;
constexpr clap_id kNeuralHierarchyParamId = 64u;
constexpr clap_id kNeuralPhaseParamId = 65u;
constexpr clap_id kNeuralBrownianParamId = 66u;
constexpr clap_id kNeuralDriftParamId = 67u;
constexpr clap_id kNeuralSelfModParamId = 68u;
constexpr clap_id kNeuralAudioFeedbackParamId = 69u;
constexpr clap_id kNeuralPulsaretParamId = 70u;
constexpr clap_id kNeuralEnvelopeParamId = 71u;
constexpr clap_id kNeuralFmParamId = 72u;
constexpr clap_id kNeuralCaptureParamId = 73u;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiPulsarParams params {};
    uint32_t presetIndex = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiPulsarEncoder engine {};
    s3g::AmbiPulsarParams params {};
    uint32_t presetIndex = 0u;
    uint32_t randomSeed = 0x7158492du;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kAmbiPulsarLanes> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarLanes> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarLanes> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiPulsarLanes> guiEnergy {};
    std::array<std::atomic<float>, s3g::kNeuralSynthesisNodes> guiNeuralNode {};
    std::array<std::atomic<float>, s3g::kNeuralSynthesisClusters> guiNeuralCluster {};
    std::atomic<float> guiCaptureProgress { 0.0f };
    std::atomic<uint32_t> guiCaptureGeneration { 0u };
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0u;
    while (done < size) {
        const int64_t count = stream->write(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0u;
    while (done < size) {
        const int64_t count = stream->read(stream, bytes + done, size - done);
        if (count <= 0) return false;
        done += static_cast<size_t>(count);
    }
    return true;
}

float randomUnit(uint32_t& seed)
{
    seed += 0x9e3779b9u;
    uint32_t value = seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float randomRange(uint32_t& seed, float low, float high)
{
    return low + (high - low) * randomUnit(seed);
}

void randomizeSafe(Plugin& plugin)
{
    auto p = plugin.params;
    uint32_t seed = plugin.randomSeed;
    p.order = 3u;
    p.emissionHz = std::pow(2.0f, randomRange(seed, std::log2(1.2f), std::log2(120.0f)));
    p.emissionModRateHz = std::pow(2.0f, randomRange(seed, std::log2(0.008f), std::log2(4.0f)));
    p.emissionModDepth = randomRange(seed, 0.02f, 0.68f);
    p.formantModRateHz = std::pow(2.0f, randomRange(seed, std::log2(0.05f), std::log2(18.0f)));
    p.formantModDepthSemitones = randomRange(seed, 0.0f, 5.0f);
    p.formantScatterSemitones = randomRange(seed, 0.0f, 4.5f);
    p.phaseScatter = randomRange(seed, 0.0f, 0.65f);
    p.probability = randomRange(seed, 0.45f, 1.0f);
    p.burstOn = 3u + static_cast<uint32_t>(randomUnit(seed) * 14.0f);
    p.burstOff = randomUnit(seed) < 0.55f ? 0u : 1u + static_cast<uint32_t>(randomUnit(seed) * 8.0f);
    p.sieveModulo = randomUnit(seed) < 0.68f ? 1u : 2u + static_cast<uint32_t>(randomUnit(seed) * 6.0f);
    p.sieveResidue = static_cast<uint32_t>(randomUnit(seed) * static_cast<float>(p.sieveModulo));
    p.laneDistribution = randomRange(seed, 0.0f, 0.88f);
    const float root = std::pow(2.0f, randomRange(seed, std::log2(45.0f), std::log2(620.0f)));
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        p.lanes[lane].formantHz = root * std::pow(randomRange(seed, 1.35f, 2.75f), static_cast<float>(lane));
        p.lanes[lane].overlap = std::pow(2.0f, randomRange(seed, std::log2(0.12f), std::log2(4.5f)));
        p.lanes[lane].level = randomRange(seed, 0.38f, 0.98f);
        p.lanes[lane].triggerOffset = lane == 0u ? 0.0f : randomRange(seed, 0.04f, 0.72f);
        p.lanes[lane].waveform = static_cast<s3g::AmbiPulsarWaveform>(static_cast<uint32_t>(randomUnit(seed) * 8.0f) % 8u);
    }
    p.envelope = static_cast<s3g::AmbiPulsarEnvelope>(static_cast<uint32_t>(randomUnit(seed) * 5.0f) % 5u);
    p.envelopeEdge = randomRange(seed, 0.12f, 0.88f);
    p.quality = s3g::AmbiPulsarQuality::High;
    p.centerAzimuthDeg = randomRange(seed, -35.0f, 35.0f);
    p.centerElevationDeg = randomRange(seed, -18.0f, 22.0f);
    p.centerDistance = randomRange(seed, 0.72f, 1.48f);
    p.spatialWidth = randomRange(seed, 0.24f, 0.96f);
    p.spatialScatter = randomRange(seed, 0.0f, 0.42f);
    p.orbitRateHz = randomRange(seed, -0.15f, 0.15f);
    p.orbitDepth = randomRange(seed, 0.12f, 0.88f);
    p.spatialFollow = randomRange(seed, 0.35f, 0.92f);
    p.air = randomRange(seed, 0.0f, 0.42f);
    p.doppler = randomUnit(seed) < 0.75f ? 0.0f : randomRange(seed, 0.05f, 0.35f);
    p.outputGainDb = -12.0f;
    p.seed = seed;
    p.neuralLevel = randomUnit(seed) < 0.58f ? 0.0f : randomRange(seed, 0.08f, 0.52f);
    p.neural.drive = randomRange(seed, 1.15f, 3.35f);
    p.neural.feedback = randomRange(seed, 0.48f, 1.04f);
    p.neural.coupling = randomRange(seed, 0.12f, 0.82f);
    p.neural.hierarchy = randomRange(seed, 0.12f, 0.92f);
    p.neural.phaseShift = randomRange(seed, 0.0f, 0.88f);
    p.neural.brownian = randomRange(seed, 0.0f, 0.68f);
    p.neural.drift = randomRange(seed, 0.0f, 0.72f);
    p.neural.selfModulation = randomRange(seed, 0.0f, 0.92f);
    p.neural.audioFeedback = randomUnit(seed) < 0.64f ? 0.0f : randomRange(seed, 0.04f, 0.52f);
    p.neural.seed = seed ^ 0x4e455552u;
    p.neuralPulsaretMix = randomUnit(seed) < 0.60f ? 0.0f : randomRange(seed, 0.08f, 0.72f);
    p.neuralEnvelopeMix = randomUnit(seed) < 0.68f ? 0.0f : randomRange(seed, 0.08f, 0.62f);
    p.neuralFmDepthSemitones = randomUnit(seed) < 0.66f ? 0.0f : randomRange(seed, 0.25f, 7.0f);
    plugin.randomSeed = seed;
    plugin.presetIndex = 0u;
    plugin.params = s3g::sanitizeAmbiPulsarParams(p);
    plugin.engine.setParams(plugin.params);
}

template <typename F>
bool laneForParam(clap_id id, const std::array<clap_id, s3g::kAmbiPulsarLanes>& ids, F&& fn)
{
    for (uint32_t lane = 0u; lane < ids.size(); ++lane) {
        if (id == ids[lane]) {
            fn(lane);
            return true;
        }
    }
    return false;
}

bool assignParam(s3g::AmbiPulsarParams& p, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: p.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kEmissionParamId: p.emissionHz = static_cast<float>(value); return true;
    case kEmissionModRateParamId: p.emissionModRateHz = static_cast<float>(value); return true;
    case kEmissionModDepthParamId: p.emissionModDepth = static_cast<float>(value); return true;
    case kFormantModRateParamId: p.formantModRateHz = static_cast<float>(value); return true;
    case kFormantModDepthParamId: p.formantModDepthSemitones = static_cast<float>(value); return true;
    case kFormantScatterParamId: p.formantScatterSemitones = static_cast<float>(value); return true;
    case kPhaseScatterParamId: p.phaseScatter = static_cast<float>(value); return true;
    case kProbabilityParamId: p.probability = static_cast<float>(value); return true;
    case kBurstOnParamId: p.burstOn = static_cast<uint32_t>(std::lround(value)); return true;
    case kBurstOffParamId: p.burstOff = static_cast<uint32_t>(std::lround(value)); return true;
    case kSieveModuloParamId: p.sieveModulo = static_cast<uint32_t>(std::lround(value)); return true;
    case kSieveResidueParamId: p.sieveResidue = static_cast<uint32_t>(std::lround(value)); return true;
    case kDistributionParamId: p.laneDistribution = static_cast<float>(value); return true;
    case kEnvelopeParamId: p.envelope = static_cast<s3g::AmbiPulsarEnvelope>(static_cast<uint32_t>(std::lround(value))); return true;
    case kEnvelopeEdgeParamId: p.envelopeEdge = static_cast<float>(value); return true;
    case kQualityParamId: p.quality = static_cast<s3g::AmbiPulsarQuality>(static_cast<uint32_t>(std::lround(value))); return true;
    case kAzimuthParamId: p.centerAzimuthDeg = static_cast<float>(value); return true;
    case kElevationParamId: p.centerElevationDeg = static_cast<float>(value); return true;
    case kDistanceParamId: p.centerDistance = static_cast<float>(value); return true;
    case kWidthParamId: p.spatialWidth = static_cast<float>(value); return true;
    case kSpatialScatterParamId: p.spatialScatter = static_cast<float>(value); return true;
    case kOrbitRateParamId: p.orbitRateHz = static_cast<float>(value); return true;
    case kOrbitDepthParamId: p.orbitDepth = static_cast<float>(value); return true;
    case kSpatialFollowParamId: p.spatialFollow = static_cast<float>(value); return true;
    case kAirParamId: p.air = static_cast<float>(value); return true;
    case kDopplerParamId: p.doppler = static_cast<float>(value); return true;
    case kOutputParamId: p.outputGainDb = static_cast<float>(value); return true;
    case kNeuralLevelParamId: p.neuralLevel = static_cast<float>(value); return true;
    case kNeuralDriveParamId: p.neural.drive = static_cast<float>(value); return true;
    case kNeuralFeedbackParamId: p.neural.feedback = static_cast<float>(value); return true;
    case kNeuralCouplingParamId: p.neural.coupling = static_cast<float>(value); return true;
    case kNeuralHierarchyParamId: p.neural.hierarchy = static_cast<float>(value); return true;
    case kNeuralPhaseParamId: p.neural.phaseShift = static_cast<float>(value); return true;
    case kNeuralBrownianParamId: p.neural.brownian = static_cast<float>(value); return true;
    case kNeuralDriftParamId: p.neural.drift = static_cast<float>(value); return true;
    case kNeuralSelfModParamId: p.neural.selfModulation = static_cast<float>(value); return true;
    case kNeuralAudioFeedbackParamId: p.neural.audioFeedback = static_cast<float>(value); return true;
    case kNeuralPulsaretParamId: p.neuralPulsaretMix = static_cast<float>(value); return true;
    case kNeuralEnvelopeParamId: p.neuralEnvelopeMix = static_cast<float>(value); return true;
    case kNeuralFmParamId: p.neuralFmDepthSemitones = static_cast<float>(value); return true;
    case kNeuralCaptureParamId: p.neuralCapture = static_cast<uint32_t>(std::lround(value)); return true;
    default: break;
    }
    if (laneForParam(id, kLaneFormantIds, [&](uint32_t lane) { p.lanes[lane].formantHz = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneOverlapIds, [&](uint32_t lane) { p.lanes[lane].overlap = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneLevelIds, [&](uint32_t lane) { p.lanes[lane].level = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneOffsetIds, [&](uint32_t lane) { p.lanes[lane].triggerOffset = static_cast<float>(value); })) return true;
    if (laneForParam(id, kLaneWaveformIds, [&](uint32_t lane) {
        p.lanes[lane].waveform = static_cast<s3g::AmbiPulsarWaveform>(static_cast<uint32_t>(std::lround(value)));
    })) return true;
    return false;
}

double paramValue(const s3g::AmbiPulsarParams& p, clap_id id)
{
    switch (id) {
    case kOrderParamId: return p.order;
    case kEmissionParamId: return p.emissionHz;
    case kEmissionModRateParamId: return p.emissionModRateHz;
    case kEmissionModDepthParamId: return p.emissionModDepth;
    case kFormantModRateParamId: return p.formantModRateHz;
    case kFormantModDepthParamId: return p.formantModDepthSemitones;
    case kFormantScatterParamId: return p.formantScatterSemitones;
    case kPhaseScatterParamId: return p.phaseScatter;
    case kProbabilityParamId: return p.probability;
    case kBurstOnParamId: return p.burstOn;
    case kBurstOffParamId: return p.burstOff;
    case kSieveModuloParamId: return p.sieveModulo;
    case kSieveResidueParamId: return p.sieveResidue;
    case kDistributionParamId: return p.laneDistribution;
    case kEnvelopeParamId: return static_cast<uint32_t>(p.envelope);
    case kEnvelopeEdgeParamId: return p.envelopeEdge;
    case kQualityParamId: return static_cast<uint32_t>(p.quality);
    case kAzimuthParamId: return p.centerAzimuthDeg;
    case kElevationParamId: return p.centerElevationDeg;
    case kDistanceParamId: return p.centerDistance;
    case kWidthParamId: return p.spatialWidth;
    case kSpatialScatterParamId: return p.spatialScatter;
    case kOrbitRateParamId: return p.orbitRateHz;
    case kOrbitDepthParamId: return p.orbitDepth;
    case kSpatialFollowParamId: return p.spatialFollow;
    case kAirParamId: return p.air;
    case kDopplerParamId: return p.doppler;
    case kOutputParamId: return p.outputGainDb;
    case kNeuralLevelParamId: return p.neuralLevel;
    case kNeuralDriveParamId: return p.neural.drive;
    case kNeuralFeedbackParamId: return p.neural.feedback;
    case kNeuralCouplingParamId: return p.neural.coupling;
    case kNeuralHierarchyParamId: return p.neural.hierarchy;
    case kNeuralPhaseParamId: return p.neural.phaseShift;
    case kNeuralBrownianParamId: return p.neural.brownian;
    case kNeuralDriftParamId: return p.neural.drift;
    case kNeuralSelfModParamId: return p.neural.selfModulation;
    case kNeuralAudioFeedbackParamId: return p.neural.audioFeedback;
    case kNeuralPulsaretParamId: return p.neuralPulsaretMix;
    case kNeuralEnvelopeParamId: return p.neuralEnvelopeMix;
    case kNeuralFmParamId: return p.neuralFmDepthSemitones;
    case kNeuralCaptureParamId: return p.neuralCapture;
    default: break;
    }
    double value = 0.0;
    if (laneForParam(id, kLaneFormantIds, [&](uint32_t lane) { value = p.lanes[lane].formantHz; })) return value;
    if (laneForParam(id, kLaneOverlapIds, [&](uint32_t lane) { value = p.lanes[lane].overlap; })) return value;
    if (laneForParam(id, kLaneLevelIds, [&](uint32_t lane) { value = p.lanes[lane].level; })) return value;
    if (laneForParam(id, kLaneOffsetIds, [&](uint32_t lane) { value = p.lanes[lane].triggerOffset; })) return value;
    if (laneForParam(id, kLaneWaveformIds, [&](uint32_t lane) { value = static_cast<uint32_t>(p.lanes[lane].waveform); })) return value;
    return value;
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    if (id == kPresetParamId) {
        plugin.presetIndex = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u,
            s3g::kAmbiPulsarFactoryPresetCount - 1u);
        plugin.params = s3g::ambiPulsarFactoryPreset(plugin.presetIndex);
        plugin.engine.setParams(plugin.params);
        return;
    }
    if (!assignParam(plugin.params, id, value)) return;
    plugin.params = s3g::sanitizeAmbiPulsarParams(plugin.params);
    plugin.engine.setParams(plugin.params);
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) {
        [static_cast<NSView*>(p->guiView) removeFromSuperview];
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
    }
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    p->engine.setParams(p->params);
    p->engine.reset();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* input)
{
    if (!input) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(plugin, value->param_id, value->value);
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    readParamEvents(*p, process->in_events);
    if (process->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    auto& output = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0u, frames);
    if (!output.data32 || channels == 0u) return CLAP_PROCESS_CONTINUE;

    std::array<float*, kOutputChannels> outputs {};
    for (uint32_t channel = 0u; channel < channels; ++channel) outputs[channel] = output.data32[channel];
    p->engine.setParams(p->params);
    p->engine.process(outputs.data(), channels, frames);
    s3g::clearAudioBufferFromChannel(output, channels, frames);

    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (!output.data32[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) peak = std::max(peak, std::fabs(output.data32[channel][frame]));
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.91f, peak), std::memory_order_relaxed);
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        const auto point = p->engine.lanePoint(lane);
        p->guiAzimuth[lane].store(point.azimuthDeg, std::memory_order_relaxed);
        p->guiElevation[lane].store(point.elevationDeg, std::memory_order_relaxed);
        p->guiDistance[lane].store(point.distance, std::memory_order_relaxed);
        p->guiEnergy[lane].store(p->engine.laneEnergy(lane), std::memory_order_relaxed);
    }
    for (uint32_t node = 0u; node < s3g::kNeuralSynthesisNodes; ++node) {
        p->guiNeuralNode[node].store(p->engine.neuralNode(node), std::memory_order_relaxed);
    }
    for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
        p->guiNeuralCluster[cluster].store(p->engine.neuralCluster(cluster), std::memory_order_relaxed);
    }
    p->guiCaptureProgress.store(p->engine.neuralCaptureProgress(), std::memory_order_relaxed);
    p->guiCaptureGeneration.store(p->engine.neuralCaptureGeneration(), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    info->id = 20u;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr ParamDef kParams[] {
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiPulsarFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Ambisonic Order", 1.0, 7.0, 3.0, true },
    { kEmissionParamId, "Emission Rate", 0.05, 2000.0, 18.0, false },
    { kEmissionModRateParamId, "Emission Mod Rate", 0.001, 80.0, 0.13, false },
    { kEmissionModDepthParamId, "Emission Mod Depth", 0.0, 0.95, 0.12, false },
    { kFormantModRateParamId, "Formant Mod Rate", 0.001, 200.0, 1.7, false },
    { kFormantModDepthParamId, "Formant Mod Depth", 0.0, 24.0, 0.45, false },
    { kFormantScatterParamId, "Formant Scatter", 0.0, 24.0, 0.22, false },
    { kPhaseScatterParamId, "Phase Scatter", 0.0, 1.0, 0.04, false },
    { kProbabilityParamId, "Pulse Probability", 0.0, 1.0, 0.92, false },
    { kBurstOnParamId, "Burst On", 1.0, 64.0, 8.0, true },
    { kBurstOffParamId, "Burst Rest", 0.0, 64.0, 0.0, true },
    { kSieveModuloParamId, "Sieve Modulo", 1.0, 32.0, 1.0, true },
    { kSieveResidueParamId, "Sieve Residue", 0.0, 31.0, 0.0, true },
    { kDistributionParamId, "Lane Distribution", 0.0, 1.0, 0.08, false },
    { kLaneFormantIds[0], "Lane A Formant", 12.0, 22000.0, 240.0, false },
    { kLaneOverlapIds[0], "Lane A Overlap", 0.025, 8.0, 0.72, false },
    { kLaneLevelIds[0], "Lane A Level", 0.0, 1.5, 0.82, false },
    { kLaneOffsetIds[0], "Lane A Offset", 0.0, 0.95, 0.0, false },
    { kLaneWaveformIds[0], "Lane A Pulsaret", 0.0, 7.0, 0.0, true },
    { kLaneFormantIds[1], "Lane B Formant", 12.0, 22000.0, 720.0, false },
    { kLaneOverlapIds[1], "Lane B Overlap", 0.025, 8.0, 0.48, false },
    { kLaneLevelIds[1], "Lane B Level", 0.0, 1.5, 0.58, false },
    { kLaneOffsetIds[1], "Lane B Offset", 0.0, 0.95, 0.19, false },
    { kLaneWaveformIds[1], "Lane B Pulsaret", 0.0, 7.0, 4.0, true },
    { kLaneFormantIds[2], "Lane C Formant", 12.0, 22000.0, 1680.0, false },
    { kLaneOverlapIds[2], "Lane C Overlap", 0.025, 8.0, 0.32, false },
    { kLaneLevelIds[2], "Lane C Level", 0.0, 1.5, 0.42, false },
    { kLaneOffsetIds[2], "Lane C Offset", 0.0, 0.95, 0.41, false },
    { kLaneWaveformIds[2], "Lane C Pulsaret", 0.0, 7.0, 1.0, true },
    { kEnvelopeParamId, "Pulsaret Envelope", 0.0, 4.0, 0.0, true },
    { kEnvelopeEdgeParamId, "Envelope Edge", 0.01, 1.0, 0.38, false },
    { kQualityParamId, "Render Quality", 0.0, 2.0, 1.0, true },
    { kAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Center Elevation", -89.0, 89.0, 0.0, false },
    { kDistanceParamId, "Center Distance", 0.1, 8.0, 1.0, false },
    { kWidthParamId, "Spatial Width", 0.0, 1.0, 0.54, false },
    { kSpatialScatterParamId, "Spatial Scatter", 0.0, 1.0, 0.08, false },
    { kOrbitRateParamId, "Orbit Rate", -4.0, 4.0, 0.025, false },
    { kOrbitDepthParamId, "Orbit Depth", 0.0, 1.0, 0.36, false },
    { kSpatialFollowParamId, "Spatial Inertia", 0.0, 1.0, 0.72, false },
    { kAirParamId, "Air Absorption", 0.0, 1.0, 0.10, false },
    { kDopplerParamId, "Doppler", 0.0, 1.0, 0.0, false },
    { kOutputParamId, "Output Gain", -60.0, 6.0, -12.0, false },
    { kNeuralLevelParamId, "Neural Direct Level", 0.0, 1.5, 0.0, false },
    { kNeuralDriveParamId, "Neural Sigmoid Drive", 0.25, 5.0, 1.85, false },
    { kNeuralFeedbackParamId, "Neural Ring Feedback", 0.0, 1.25, 0.76, false },
    { kNeuralCouplingParamId, "Neural Matrix Coupling", 0.0, 1.25, 0.38, false },
    { kNeuralHierarchyParamId, "Neural Hierarchy Gate", 0.0, 1.0, 0.52, false },
    { kNeuralPhaseParamId, "Neural Phase Paths", 0.0, 1.0, 0.28, false },
    { kNeuralBrownianParamId, "Neural Brownian Weights", 0.0, 1.0, 0.08, false },
    { kNeuralDriftParamId, "Neural Autonomous Drift", 0.0, 1.0, 0.10, false },
    { kNeuralSelfModParamId, "Neural Slow Fast Mod", 0.0, 1.0, 0.32, false },
    { kNeuralAudioFeedbackParamId, "Neural Audio Feedback", 0.0, 1.0, 0.0, false },
    { kNeuralPulsaretParamId, "Captured Pulsaret Mix", 0.0, 1.0, 0.0, false },
    { kNeuralEnvelopeParamId, "Captured Envelope Mix", 0.0, 1.0, 0.0, false },
    { kNeuralFmParamId, "Captured FM Depth", 0.0, 24.0, 0.0, false },
    { kNeuralCaptureParamId, "Capture Neural Tables", 0.0, 65535.0, 0.0, true },
};

const ParamDef* findParam(clap_id id)
{
    for (const auto& param : kParams) if (param.id == id) return &param;
    return nullptr;
}

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(kParams)) return false;
    const auto& param = kParams[index];
    *info = {};
    info->id = param.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (param.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, param.name, sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    std::strncpy(info->module, param.id >= kNeuralLevelParamId ? "Neural Synthesis" : "Pulsar Encoder", sizeof(info->module));
    info->module[sizeof(info->module) - 1u] = '\0';
    info->min_value = param.minimum;
    info->max_value = param.maximum;
    info->default_value = param.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !findParam(id)) return false;
    const auto* p = self(plugin);
    *value = id == kPresetParamId ? p->presetIndex : paramValue(p->params, id);
    return true;
}

constexpr const char* kWaveformNames[] { "SINE", "TRIANGLE", "SAW", "SQUARE", "OVERTONE", "FOLD", "IMPULSE", "NOISE" };
constexpr const char* kEnvelopeNames[] { "HANN", "TUKEY", "WELCH", "PERCUSSIVE", "REVERSE" };
constexpr const char* kQualityNames[] { "ECO", "HIGH", "ULTRA" };

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t capacity)
{
    if (!display || capacity == 0u || !findParam(id)) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, capacity, "%s", s3g::ambiPulsarFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, capacity, "%uOA", static_cast<uint32_t>(std::lround(value)));
    } else if (id == kEnvelopeParamId) {
        std::snprintf(display, capacity, "%s", kEnvelopeNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 4)]);
    } else if (id == kQualityParamId) {
        std::snprintf(display, capacity, "%s", kQualityNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 2)]);
    } else if (id == kLaneWaveformIds[0] || id == kLaneWaveformIds[1] || id == kLaneWaveformIds[2]) {
        std::snprintf(display, capacity, "%s", kWaveformNames[std::clamp<int>(static_cast<int>(std::lround(value)), 0, 7)]);
    } else if (id == kEmissionParamId || id == kEmissionModRateParamId || id == kFormantModRateParamId
        || id == kLaneFormantIds[0] || id == kLaneFormantIds[1] || id == kLaneFormantIds[2]) {
        std::snprintf(display, capacity, "%.7g Hz", value);
    } else if (id == kFormantModDepthParamId || id == kFormantScatterParamId || id == kNeuralFmParamId) {
        std::snprintf(display, capacity, "%.2f st", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, capacity, "%+.1f deg", value);
    } else if (id == kDistanceParamId) {
        std::snprintf(display, capacity, "%.2f", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, capacity, "%+.1f dB", value);
    } else if (id == kNeuralCaptureParamId) {
        std::snprintf(display, capacity, "capture %u", static_cast<uint32_t>(std::lround(value)));
    } else if (findParam(id)->stepped) {
        std::snprintf(display, capacity, "%d", static_cast<int>(std::lround(value)));
    } else {
        std::snprintf(display, capacity, "%.3f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value || !findParam(id)) return false;
    if (id == kPresetParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiPulsarFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiPulsarFactoryPresetInfo(index).name) == 0) {
                *value = index;
                return true;
            }
        }
    }
    if (id == kEnvelopeParamId) {
        for (uint32_t index = 0u; index < std::size(kEnvelopeNames); ++index) {
            if (std::strcmp(display, kEnvelopeNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kQualityParamId) {
        for (uint32_t index = 0u; index < std::size(kQualityNames); ++index) {
            if (std::strcmp(display, kQualityNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kLaneWaveformIds[0] || id == kLaneWaveformIds[1] || id == kLaneWaveformIds[2]) {
        for (uint32_t index = 0u; index < std::size(kWaveformNames); ++index) {
            if (std::strcmp(display, kWaveformNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kNeuralCaptureParamId && std::strncmp(display, "capture ", 8u) == 0) display += 8u;
    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display) return false;
    const auto* param = findParam(id);
    *value = std::clamp(parsed, param->minimum, param->maximum);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    SavedState state;
    state.params = self(plugin)->params;
    state.presetIndex = self(plugin)->presetIndex;
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    if (version == 1u) {
        constexpr size_t kV1ParamsSize = offsetof(s3g::AmbiPulsarParams, neuralLevel);
        std::array<uint8_t, kV1ParamsSize> bytes {};
        uint32_t presetIndex = 0u;
        if (!readExact(stream, bytes.data(), bytes.size()) || !readExact(stream, &presetIndex, sizeof(presetIndex))) return false;
        s3g::AmbiPulsarParams upgraded {};
        std::memcpy(&upgraded, bytes.data(), bytes.size());
        p->params = s3g::sanitizeAmbiPulsarParams(upgraded);
        p->presetIndex = std::min<uint32_t>(presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
    } else if (version == kStateVersion) {
        SavedState state;
        state.version = version;
        if (!readExact(stream, reinterpret_cast<uint8_t*>(&state) + sizeof(version), sizeof(state) - sizeof(version))) return false;
        p->params = s3g::sanitizeAmbiPulsarParams(state.params);
        p->presetIndex = std::min<uint32_t>(state.presetIndex, s3g::kAmbiPulsarFactoryPresetCount - 1u);
    } else {
        return false;
    }
    p->engine.setParams(p->params);
    p->engine.reset();
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {

constexpr uint32_t kGuiWidth = 1060u;
constexpr uint32_t kGuiHeight = 920u;

struct GuiSliderSpec {
    clap_id id;
    const char* label;
    CGFloat panelX;
    CGFloat y;
    CGFloat trackWidth;
};

constexpr GuiSliderSpec kGuiSliders[] {
    { kEmissionParamId, "EMISSION", 514.0, 82.0, 82.0 },
    { kEmissionModRateParamId, "EMIT RATE", 514.0, 106.0, 82.0 },
    { kEmissionModDepthParamId, "EMIT DEPTH", 514.0, 130.0, 82.0 },
    { kFormantModRateParamId, "FORM RATE", 514.0, 154.0, 82.0 },
    { kFormantModDepthParamId, "FORM DEPTH", 514.0, 178.0, 82.0 },
    { kFormantScatterParamId, "FORM SCAT", 514.0, 202.0, 82.0 },
    { kPhaseScatterParamId, "PHASE SCAT", 514.0, 226.0, 82.0 },
    { kEnvelopeEdgeParamId, "ENV EDGE", 514.0, 250.0, 82.0 },
    { kOutputParamId, "OUTPUT", 514.0, 274.0, 82.0 },

    { kProbabilityParamId, "PROB", 780.0, 82.0, 82.0 },
    { kBurstOnParamId, "BURST ON", 780.0, 106.0, 82.0 },
    { kBurstOffParamId, "REST", 780.0, 130.0, 82.0 },
    { kSieveModuloParamId, "SIEVE MOD", 780.0, 154.0, 82.0 },
    { kSieveResidueParamId, "RESIDUE", 780.0, 178.0, 82.0 },
    { kDistributionParamId, "DISTRIB", 780.0, 202.0, 82.0 },
    { kWidthParamId, "WIDTH", 780.0, 226.0, 82.0 },
    { kSpatialScatterParamId, "SPAT SCAT", 780.0, 250.0, 82.0 },

    { kAzimuthParamId, "AZIMUTH", 514.0, 352.0, 82.0 },
    { kElevationParamId, "ELEVATION", 514.0, 376.0, 82.0 },
    { kDistanceParamId, "DISTANCE", 514.0, 400.0, 82.0 },
    { kOrbitRateParamId, "ORBIT RATE", 514.0, 424.0, 82.0 },

    { kOrbitDepthParamId, "ORBIT DEPTH", 780.0, 352.0, 82.0 },
    { kSpatialFollowParamId, "INERTIA", 780.0, 376.0, 82.0 },
    { kAirParamId, "AIR", 780.0, 400.0, 82.0 },
    { kDopplerParamId, "DOPPLER", 780.0, 424.0, 82.0 },

    { kNeuralLevelParamId, "DIRECT", 514.0, 522.0, 82.0 },
    { kNeuralDriveParamId, "SIGMOID", 514.0, 546.0, 82.0 },
    { kNeuralFeedbackParamId, "RING FB", 514.0, 570.0, 82.0 },
    { kNeuralCouplingParamId, "MATRIX", 514.0, 594.0, 82.0 },
    { kNeuralHierarchyParamId, "HIERARCHY", 514.0, 618.0, 82.0 },
    { kNeuralPhaseParamId, "PHASE", 514.0, 642.0, 82.0 },
    { kNeuralBrownianParamId, "BROWNIAN", 514.0, 666.0, 82.0 },

    { kNeuralDriftParamId, "DRIFT", 780.0, 522.0, 82.0 },
    { kNeuralSelfModParamId, "SLOW > FAST", 780.0, 546.0, 82.0 },
    { kNeuralAudioFeedbackParamId, "AUDIO FB", 780.0, 570.0, 82.0 },
    { kNeuralPulsaretParamId, "PULSARET", 780.0, 594.0, 82.0 },
    { kNeuralEnvelopeParamId, "ENVELOPE", 780.0, 618.0, 82.0 },
    { kNeuralFmParamId, "FM DEPTH", 780.0, 642.0, 82.0 },

    { kLaneFormantIds[0], "FORMANT", 18.0, 730.0, 124.0 },
    { kLaneOverlapIds[0], "OVERLAP", 18.0, 754.0, 124.0 },
    { kLaneLevelIds[0], "LEVEL", 18.0, 778.0, 124.0 },
    { kLaneOffsetIds[0], "OFFSET", 18.0, 802.0, 124.0 },
    { kLaneFormantIds[1], "FORMANT", 360.0, 730.0, 124.0 },
    { kLaneOverlapIds[1], "OVERLAP", 360.0, 754.0, 124.0 },
    { kLaneLevelIds[1], "LEVEL", 360.0, 778.0, 124.0 },
    { kLaneOffsetIds[1], "OFFSET", 360.0, 802.0, 124.0 },
    { kLaneFormantIds[2], "FORMANT", 702.0, 730.0, 124.0 },
    { kLaneOverlapIds[2], "OVERLAP", 702.0, 754.0, 124.0 },
    { kLaneLevelIds[2], "LEVEL", 702.0, 778.0, 124.0 },
    { kLaneOffsetIds[2], "OFFSET", 702.0, 802.0, 124.0 },
};

const GuiSliderSpec* guiSlider(clap_id id)
{
    for (const auto& slider : kGuiSliders) if (slider.id == id) return &slider;
    return nullptr;
}

bool logarithmicParam(clap_id id)
{
    return id == kEmissionParamId || id == kEmissionModRateParamId || id == kFormantModRateParamId
        || id == kDistanceParamId || id == kLaneFormantIds[0] || id == kLaneFormantIds[1]
        || id == kLaneFormantIds[2] || id == kLaneOverlapIds[0] || id == kLaneOverlapIds[1]
        || id == kLaneOverlapIds[2];
}

CGFloat guiNormalize(clap_id id, double value)
{
    const auto* param = findParam(id);
    if (!param) return 0.0;
    if (logarithmicParam(id)) {
        const double low = std::log(std::max(1.0e-9, param->minimum));
        const double high = std::log(std::max(1.0e-9, param->maximum));
        return static_cast<CGFloat>((std::log(std::max(1.0e-9, value)) - low) / std::max(1.0e-9, high - low));
    }
    return static_cast<CGFloat>((value - param->minimum) / std::max(1.0e-9, param->maximum - param->minimum));
}

double guiValue(clap_id id, CGFloat normalized)
{
    const auto* param = findParam(id);
    if (!param) return 0.0;
    normalized = std::clamp(normalized, static_cast<CGFloat>(0.0), static_cast<CGFloat>(1.0));
    double value = 0.0;
    if (logarithmicParam(id)) {
        value = std::exp(std::log(std::max(1.0e-9, param->minimum))
            + static_cast<double>(normalized) * (std::log(std::max(1.0e-9, param->maximum))
                - std::log(std::max(1.0e-9, param->minimum))));
    } else {
        value = param->minimum + static_cast<double>(normalized) * (param->maximum - param->minimum);
    }
    return param->stepped ? std::round(value) : value;
}

NSRect guiSliderHitRect(const GuiSliderSpec& slider)
{
    return NSMakeRect(slider.panelX + 8.0, slider.y - 7.0, slider.trackWidth + 222.0, 22.0);
}

float displayWave(s3g::AmbiPulsarWaveform waveform, float phase)
{
    phase -= std::floor(phase);
    switch (waveform) {
    case s3g::AmbiPulsarWaveform::Triangle: return 1.0f - 4.0f * std::fabs(phase - 0.5f);
    case s3g::AmbiPulsarWaveform::Saw: return phase * 2.0f - 1.0f;
    case s3g::AmbiPulsarWaveform::Square: return phase < 0.5f ? 1.0f : -1.0f;
    case s3g::AmbiPulsarWaveform::Overtone:
        return (std::sin(s3g::kAmbiPulsarTwoPi * phase)
            + 0.42f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 2.0f)
            + 0.22f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 3.0f)) / 1.64f;
    case s3g::AmbiPulsarWaveform::Fold:
        return std::tanh((std::sin(s3g::kAmbiPulsarTwoPi * phase)
            + 0.34f * std::sin(s3g::kAmbiPulsarTwoPi * phase * 2.0f)) * 2.25f);
    case s3g::AmbiPulsarWaveform::Impulse: return std::exp(-phase * phase * 1200.0f);
    case s3g::AmbiPulsarWaveform::Noise:
        return std::sin(phase * 1927.0f) * std::sin(phase * 733.0f);
    case s3g::AmbiPulsarWaveform::Sine:
    default: return std::sin(s3g::kAmbiPulsarTwoPi * phase);
    }
}

} // namespace

@interface S3GAmbiPulsarEncoderView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiPulsarEncoderView

- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
    }
    return self;
}

- (BOOL)isFlipped { return NO; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    [self setNeedsDisplay:YES];
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [[NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
                                               target:self
                                             selector:@selector(refresh:)
                                             userInfo:nil
                                              repeats:YES] retain];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    [_timer release];
    _timer = nil;
}

- (NSRect)presetRect { return NSMakeRect(310, 12, 190, 18); }
- (NSRect)randomRect { return NSMakeRect(508, 12, 70, 18); }
- (NSRect)orderRect { return NSMakeRect(620, 307, 126, 16); }
- (NSRect)envelopeRect { return NSMakeRect(620, 283, 126, 16); }
- (NSRect)qualityRect { return NSMakeRect(886, 283, 126, 16); }
- (NSRect)captureRect { return NSMakeRect(920, 483, 112, 16); }
- (NSRect)waveformRect:(uint32_t)lane
{
    return NSMakeRect(126.0 + static_cast<CGFloat>(lane) * 342.0, 838.0, 180.0, 17.0);
}

- (void)drawMenuLabel:(NSString*)label value:(NSString*)value rect:(NSRect)rect style:(const s3g::clap_gui::Style&)style
{
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [label drawAtPoint:NSMakePoint(rect.origin.x - 102.0, rect.origin.y + 1.0) withAttributes:attrs];
    [style.strip setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    [style.fill setFill];
    NSRectFill(NSMakeRect(rect.origin.x + 1.0, rect.origin.y + 1.0, 2.0, rect.size.height - 2.0));
    [value drawAtPoint:NSMakePoint(rect.origin.x + 8.0, rect.origin.y + 2.0) withAttributes:values];
    [@">" drawAtPoint:NSMakePoint(NSMaxX(rect) - 14.0, rect.origin.y + 1.0) withAttributes:values];
}

- (void)drawSlider:(const GuiSliderSpec&)slider style:(const s3g::clap_gui::Style&)style
{
    char text[64] {};
    const double value = paramValue(_plugin->params, slider.id);
    paramsValueToText(nullptr, slider.id, value, text, sizeof(text));
    NSString* label = [NSString stringWithUTF8String:slider.label];
    NSString* valueText = [NSString stringWithUTF8String:text];
    s3g::clap_gui::drawSlider(label, valueText, guiNormalize(slider.id, value), slider.y,
        s3g::clap_gui::softLabelAttrs(), s3g::clap_gui::softValueAttrs(), style,
        slider.panelX + 12.0, slider.panelX + 104.0, slider.panelX + 194.0, slider.trackWidth);
}

- (void)drawPulsaret:(uint32_t)lane rect:(NSRect)rect style:(const s3g::clap_gui::Style&)style
{
    [style.strip setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    NSBezierPath* path = [NSBezierPath bezierPath];
    const auto waveform = _plugin->params.lanes[lane].waveform;
    constexpr uint32_t points = 96u;
    for (uint32_t index = 0u; index < points; ++index) {
        const float phase = static_cast<float>(index) / static_cast<float>(points - 1u);
        const float value = displayWave(waveform, phase);
        const NSPoint point = NSMakePoint(rect.origin.x + phase * rect.size.width,
            NSMidY(rect) + value * rect.size.height * 0.38);
        index == 0u ? [path moveToPoint:point] : [path lineToPoint:point];
    }
    [style.accent setStroke];
    [path setLineWidth:1.0];
    [path stroke];
}

- (void)drawField:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = NSMakeRect(18, 42, 480, 426);
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PULSAR FIELD / AED", true, panel.origin.x, panel.origin.y,
        panel.size.width, 21, s3g::clap_gui::softLabelAttrs(), style);
    const NSRect field = NSMakeRect(32, 76, 452, 356);
    [s3g::clap_gui::color(0x101010) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x383838) setStroke];
    for (uint32_t x = 0u; x <= 8u; ++x) {
        const CGFloat px = field.origin.x + field.size.width * static_cast<CGFloat>(x) / 8.0;
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(px, field.origin.y)];
        [line lineToPoint:NSMakePoint(px, NSMaxY(field))];
        [line stroke];
    }
    for (uint32_t y = 0u; y <= 4u; ++y) {
        const CGFloat py = field.origin.y + field.size.height * static_cast<CGFloat>(y) / 4.0;
        NSBezierPath* line = [NSBezierPath bezierPath];
        [line moveToPoint:NSMakePoint(field.origin.x, py)];
        [line lineToPoint:NSMakePoint(NSMaxX(field), py)];
        [line stroke];
    }
    [s3g::clap_gui::color(0x626262) setStroke];
    NSFrameRect(field);

    const NSPoint origin = NSMakePoint(field.origin.x + (s3g::ambiPulsarWrapSignedDeg(_plugin->params.centerAzimuthDeg) + 180.0f) / 360.0f * field.size.width,
        field.origin.y + (_plugin->params.centerElevationDeg + 90.0f) / 180.0f * field.size.height);
    static constexpr int laneColors[] { 0xcfcfcf, 0x8f8f8f, 0x5f5f5f };
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        const float azimuth = _plugin->guiAzimuth[lane].load(std::memory_order_relaxed);
        const float elevation = _plugin->guiElevation[lane].load(std::memory_order_relaxed);
        const float distance = _plugin->guiDistance[lane].load(std::memory_order_relaxed);
        const float energy = _plugin->guiEnergy[lane].load(std::memory_order_relaxed);
        const NSPoint point = NSMakePoint(field.origin.x + (s3g::ambiPulsarWrapSignedDeg(azimuth) + 180.0f) / 360.0f * field.size.width,
            field.origin.y + (std::clamp(elevation, -90.0f, 90.0f) + 90.0f) / 180.0f * field.size.height);
        [s3g::clap_gui::color(laneColors[lane], 0.38) setStroke];
        NSBezierPath* ray = [NSBezierPath bezierPath];
        [ray moveToPoint:origin];
        [ray lineToPoint:point];
        [ray stroke];
        const CGFloat radius = 5.0 + std::min(20.0f, energy * 36.0f) + (1.0f / std::max(0.25f, distance)) * 2.0f;
        [s3g::clap_gui::color(laneColors[lane], 0.16) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(point.x - radius, point.y - radius, radius * 2.0, radius * 2.0)] fill];
        [s3g::clap_gui::color(laneColors[lane], 0.92) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(point.x - 3.0, point.y - 3.0, 6.0, 6.0)] fill];
        NSString* laneName = [NSString stringWithFormat:@"%c  %+.0f / %+.0f  %.2f", 'A' + lane, azimuth, elevation, distance];
        [laneName drawAtPoint:NSMakePoint(field.origin.x + 10.0, NSMaxY(field) - 22.0 - lane * 18.0)
                withAttributes:s3g::clap_gui::softValueAttrs()];
    }
    [@"-180                         0                         +180" drawAtPoint:NSMakePoint(field.origin.x + 6.0, field.origin.y - 17.0)
        withAttributes:s3g::clap_gui::softValueAttrs()];
}

- (void)drawNeuralNetwork:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = NSMakeRect(18, 480, 480, 196);
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"16-NEURON NETWORK / SIGNED RINGS", true, panel.origin.x, panel.origin.y,
        panel.size.width, 21, s3g::clap_gui::softLabelAttrs(), style);

    static constexpr const char* labels[] { "80 MS", "32 MS", "9.5 MS", "2 MS" };
    std::array<NSPoint, s3g::kNeuralSynthesisClusters> centers {};
    for (uint32_t cluster = 0u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
        const CGFloat x = 32.0 + static_cast<CGFloat>(cluster) * 113.0;
        const NSRect box = NSMakeRect(x, 514, 101, 142);
        centers[cluster] = NSMakePoint(NSMidX(box), NSMidY(box));
        [s3g::clap_gui::color(0x111111) setFill];
        NSRectFill(box);
        [s3g::clap_gui::color(0x3b3b3b) setStroke];
        NSFrameRect(box);
        [[NSString stringWithUTF8String:labels[cluster]] drawAtPoint:NSMakePoint(x + 8.0, 637.0)
            withAttributes:s3g::clap_gui::softLabelAttrs()];

        std::array<NSPoint, s3g::kNeuralNodesPerCluster> points {{
            NSMakePoint(x + 27.0, 602.0), NSMakePoint(x + 74.0, 602.0),
            NSMakePoint(x + 74.0, 548.0), NSMakePoint(x + 27.0, 548.0),
        }};
        NSBezierPath* ring = [NSBezierPath bezierPath];
        [ring moveToPoint:points[0]];
        for (uint32_t local = 1u; local < points.size(); ++local) [ring lineToPoint:points[local]];
        [ring closePath];
        [s3g::clap_gui::color(0x565656, 0.72) setStroke];
        [ring setLineWidth:1.0];
        [ring stroke];

        for (uint32_t local = 0u; local < s3g::kNeuralNodesPerCluster; ++local) {
            const uint32_t node = cluster * s3g::kNeuralNodesPerCluster + local;
            const float value = _plugin->guiNeuralNode[node].load(std::memory_order_relaxed);
            const CGFloat radius = 5.0 + std::fabs(value) * 6.0;
            const int shade = static_cast<int>(std::clamp(112.0f + value * 112.0f, 22.0f, 234.0f));
            [s3g::clap_gui::color((shade << 16) | (shade << 8) | shade, 0.94) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(points[local].x - radius,
                points[local].y - radius, radius * 2.0, radius * 2.0)] fill];
            [s3g::clap_gui::color(value >= 0.0f ? 0xd8d8d8 : 0x777777, 0.88) setStroke];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(points[local].x - radius,
                points[local].y - radius, radius * 2.0, radius * 2.0)] stroke];
        }
        const float clusterValue = _plugin->guiNeuralCluster[cluster].load(std::memory_order_relaxed);
        [[NSString stringWithFormat:@"%+.3f", clusterValue] drawAtPoint:NSMakePoint(x + 29.0, 519.0)
            withAttributes:s3g::clap_gui::softValueAttrs()];
    }

    [s3g::clap_gui::color(0x777777, 0.46) setStroke];
    for (uint32_t cluster = 1u; cluster < s3g::kNeuralSynthesisClusters; ++cluster) {
        NSBezierPath* hierarchy = [NSBezierPath bezierPath];
        [hierarchy moveToPoint:NSMakePoint(centers[cluster - 1u].x + 36.0, centers[cluster - 1u].y)];
        [hierarchy lineToPoint:NSMakePoint(centers[cluster].x - 36.0, centers[cluster].y)];
        [hierarchy stroke];
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    const auto style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    [@"s3g AMBI PULSAR ENCODER 64" drawAtPoint:NSMakePoint(18, 14) withAttributes:s3g::clap_gui::softTitleAttrs()];
    [self drawMenuLabel:@"PRESET" value:[NSString stringWithUTF8String:s3g::ambiPulsarFactoryPresetInfo(_plugin->presetIndex).name]
                   rect:[self presetRect] style:style];
    s3g::clap_gui::drawHeaderActionButton([self randomRect], [self randomRect], @"RANDOM",
        s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)),
        kGuiWidth, 14, s3g::clap_gui::softValueAttrs(), 18);

    [self drawField:style];
    s3g::clap_gui::drawPanelFrame(514, 42, 250, 276, style);
    s3g::clap_gui::drawPanelHeader(@"CLOCK / MODULATION", true, 514, 42, 250, 21, s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawPanelFrame(780, 42, 262, 276, style);
    s3g::clap_gui::drawPanelHeader(@"MASK / SPREAD", true, 780, 42, 262, 21, s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawPanelFrame(514, 330, 250, 124, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD ORIGIN", true, 514, 330, 250, 21, s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawPanelFrame(780, 330, 262, 124, style);
    s3g::clap_gui::drawPanelHeader(@"DEPTH / MOTION", true, 780, 330, 262, 21, s3g::clap_gui::softLabelAttrs(), style);
    [self drawNeuralNetwork:style];
    s3g::clap_gui::drawPanelFrame(514, 480, 528, 196, style);
    s3g::clap_gui::drawPanelHeader(@"NEURAL CIRCUIT / CAPTURE UTILITY", true, 514, 480, 528, 21,
        s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawHeaderActionButton([self captureRect], [self captureRect], @"CAPTURE",
        s3g::clap_gui::softLabelAttrs(), style);
    const float captureProgress = _plugin->guiCaptureProgress.load(std::memory_order_relaxed);
    const uint32_t captureGeneration = _plugin->guiCaptureGeneration.load(std::memory_order_relaxed);
    NSString* captureStatus = captureGeneration > 0u
        ? [NSString stringWithFormat:@"TABLE %u", captureGeneration]
        : [NSString stringWithFormat:@"ARM %2.0f%%", captureProgress * 100.0f];
    [captureStatus drawAtPoint:NSMakePoint(828, 485) withAttributes:s3g::clap_gui::softValueAttrs()];
    for (const auto& slider : kGuiSliders) [self drawSlider:slider style:style];
    [self drawMenuLabel:@"ORDER" value:[NSString stringWithFormat:@"%uOA", _plugin->params.order]
                   rect:[self orderRect] style:style];
    [self drawMenuLabel:@"ENVELOPE" value:[NSString stringWithUTF8String:kEnvelopeNames[static_cast<uint32_t>(_plugin->params.envelope)]]
                   rect:[self envelopeRect] style:style];
    [self drawMenuLabel:@"QUALITY" value:[NSString stringWithUTF8String:kQualityNames[static_cast<uint32_t>(_plugin->params.quality)]]
                   rect:[self qualityRect] style:style];

    static NSString* laneTitles[] = { @"FORMANT LANE A", @"FORMANT LANE B", @"FORMANT LANE C" };
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        const CGFloat x = 18.0 + static_cast<CGFloat>(lane) * 342.0;
        s3g::clap_gui::drawPanelFrame(x, 700, 322, 202, style);
        s3g::clap_gui::drawPanelHeader(laneTitles[lane], true, x, 700, 322, 21, s3g::clap_gui::softLabelAttrs(), style);
        [self drawPulsaret:lane rect:NSMakeRect(x + 14.0, 864.0, 294.0, 25.0) style:style];
        NSString* pulsaretName = [NSString stringWithFormat:@"%s / N%02.0f",
            kWaveformNames[static_cast<uint32_t>(_plugin->params.lanes[lane].waveform)],
            _plugin->params.neuralPulsaretMix * 100.0f];
        [self drawMenuLabel:@"PULSARET" value:pulsaretName
                       rect:[self waveformRect:lane] style:style];
    }
}

- (void)setSlider:(const GuiSliderSpec&)slider point:(NSPoint)point
{
    const CGFloat normalized = (point.x - (slider.panelX + 104.0)) / slider.trackWidth;
    applyParam(*_plugin, slider.id, guiValue(slider.id, normalized));
    [self setNeedsDisplay:YES];
}

- (void)cycleParam:(clap_id)id count:(uint32_t)count event:(NSEvent*)event
{
    const int direction = ([event modifierFlags] & NSEventModifierFlagShift) ? -1 : 1;
    int value = static_cast<int>(std::lround(id == kPresetParamId ? _plugin->presetIndex : paramValue(_plugin->params, id)));
    value = (value + direction + static_cast<int>(count)) % static_cast<int>(count);
    applyParam(*_plugin, id, value);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (NSPointInRect(point, [self presetRect])) { [self cycleParam:kPresetParamId count:s3g::kAmbiPulsarFactoryPresetCount event:event]; return; }
    if (NSPointInRect(point, [self randomRect])) { randomizeSafe(*_plugin); [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, [self orderRect])) {
        const int direction = ([event modifierFlags] & NSEventModifierFlagShift) ? -1 : 1;
        const int order = 1 + (static_cast<int>(_plugin->params.order) - 1 + direction + 7) % 7;
        applyParam(*_plugin, kOrderParamId, order);
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self envelopeRect])) { [self cycleParam:kEnvelopeParamId count:5u event:event]; return; }
    if (NSPointInRect(point, [self qualityRect])) { [self cycleParam:kQualityParamId count:3u event:event]; return; }
    if (NSPointInRect(point, [self captureRect])) {
        applyParam(*_plugin, kNeuralCaptureParamId, (_plugin->params.neuralCapture + 1u) & 0xffffu);
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t lane = 0u; lane < s3g::kAmbiPulsarLanes; ++lane) {
        if (NSPointInRect(point, [self waveformRect:lane])) {
            [self cycleParam:kLaneWaveformIds[lane] count:8u event:event];
            return;
        }
    }
    _dragParam = CLAP_INVALID_ID;
    for (const auto& slider : kGuiSliders) {
        if (NSPointInRect(point, guiSliderHitRect(slider))) {
            _dragParam = slider.id;
            [self setSlider:slider point:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam == CLAP_INVALID_ID) return;
    const auto* slider = guiSlider(_dragParam);
    if (!slider) return;
    [self setSlider:*slider point:[self convertPoint:[event locationInWindow] fromView:nil]];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
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
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiPulsarEncoderView alloc] initWithPlugin:p];
    return p->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    [static_cast<S3GAmbiPulsarEncoderView*>(p->guiView) stopRefreshTimer];
    [static_cast<NSView*>(p->guiView) removeFromSuperview];
    [static_cast<NSView*>(p->guiView) release];
    p->guiView = nullptr;
    p->guiVisible = false;
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
    auto* p = self(plugin);
    if (!p->guiView) return false;
    [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(width, height)];
    return true;
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* p = self(plugin);
    if (!p->guiView) return false;
    NSView* parent = static_cast<NSView*>(window->cocoa);
    NSView* view = static_cast<NSView*>(p->guiView);
    [parent addSubview:view];
    [view setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = true;
    [static_cast<NSView*>(p->guiView) setHidden:NO];
    [static_cast<S3GAmbiPulsarEncoderView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GAmbiPulsarEncoderView*>(p->guiView) stopRefreshTimer];
    [static_cast<NSView*>(p->guiView) setHidden:YES];
    return true;
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

} // namespace
#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

constexpr const char* features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-pulsar-encoder-64",
    "s3g Ambi Pulsar Encoder 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.2.0",
    "Three-lane pulsar and 16-neuron recurrent synthesis instrument with direct 1-7OA ACN/SN3D output.",
    features,
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiPulsarFactoryPreset(0u);
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->params);
    p->engine.reset();
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
