#include "s3g_resonant_terrain.h"
#include "s3g_realtime.h"
#include "s3g_topology.h"
#include "s3g_topology_heatmap.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
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
#include <new>

namespace {

constexpr uint32_t kChannelCount = s3g::kResonantTerrainChannels;
constexpr uint32_t kStateVersion = 2;
constexpr uint32_t kLegacyStateVersion = 1;
constexpr double kTopologyMotionMinHz = 0.01;
constexpr double kTopologyMotionMaxHz = 4.0;

constexpr clap_id kSourceParamId = 1;
constexpr clap_id kBaseParamId = 2;
constexpr clap_id kDensityParamId = 3;
constexpr clap_id kDecayParamId = 4;
constexpr clap_id kBrightParamId = 5;
constexpr clap_id kHarmParamId = 6;
constexpr clap_id kExciterParamId = 7;
constexpr clap_id kMidiParamId = 8;
constexpr clap_id kOutputParamId = 9;
constexpr clap_id kTopologyShapeParamId = 10;
constexpr clap_id kTopologyAmountParamId = 11;
constexpr clap_id kTopologyPullParamId = 12;
constexpr clap_id kTopologyXParamId = 13;
constexpr clap_id kTopologyYParamId = 14;
constexpr clap_id kTopologyZParamId = 15;
constexpr clap_id kTopologyTwistParamId = 16;
constexpr clap_id kTopologyFlareParamId = 17;
constexpr clap_id kTopologySeedParamId = 18;
constexpr clap_id kTopologyMotionParamId = 19;
constexpr clap_id kTopologyVariantParamId = 20;
constexpr clap_id kTopologyRateParamId = 21;
constexpr clap_id kTopologyDepthParamId = 22;
constexpr clap_id kTopologyNeighborsParamId = 23;
constexpr clap_id kTopologyRadiusParamId = 24;
constexpr clap_id kTopologyCentroidParamId = 25;

const char* sourceName(uint32_t source)
{
    static constexpr const char* kNames[] = { "INT", "MIDI", "HYB" };
    return kNames[std::min<uint32_t>(source, 2u)];
}

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::floor(value + 0.5)));
}

double clamp01(double value) { return std::clamp(value, 0.0, 1.0); }
double clampBipolar(double value) { return std::clamp(value, -1.0, 1.0); }
double clampRate(double value) { return std::clamp(value, kTopologyMotionMinHz, kTopologyMotionMaxHz); }

float dbToGain(double db)
{
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

struct __attribute__((packed)) SavedState {
    uint32_t version = kStateVersion;
    uint32_t source = 2;
    double baseHz = 110.0;
    double density = 0.22;
    double decay = 0.58;
    double brightness = 0.62;
    double harmonicity = 0.35;
    double exciterTone = 0.45;
    double midiInfluence = 0.75;
    double outputGainDb = -18.0;
    uint32_t outputMask = 0xff;
    uint32_t topologyShape = 0;
    uint32_t topologyMotionMode = 0;
    uint32_t topologyMotionVariant = 0;
    double topologyAmount = 0.35;
    double topologyPull = 0.0;
    double topologyX = 0.0;
    double topologyY = 0.0;
    double topologyZ = 1.0;
    double topologyTwist = 0.0;
    double topologyFlare = 0.0;
    double topologySeed = 0.12;
    double topologyRateHz = 0.10;
    double topologyDepth = 0.0;
    uint32_t topologyNeighbors = 2;
    double topologyRadius = 0.65;
    double topologyCentroid = 0.22;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0;
    uint32_t source = 2;
    s3g::ResonantTerrainParams params {};
    s3g::ResonantTerrain synth;
    uint32_t outputMask = 0xff;
    uint32_t topologyShape = 0;
    uint32_t topologyMotionMode = 0;
    uint32_t topologyMotionVariant = 0;
    double topologyAmount = 0.35;
    double topologyPull = 0.0;
    double topologyX = 0.0;
    double topologyY = 0.0;
    double topologyZ = 1.0;
    double topologyTwist = 0.0;
    double topologyFlare = 0.0;
    double topologySeed = 0.12;
    double topologyRateHz = 0.10;
    double topologyDepth = 0.0;
    double topologyPhase = 0.0;
    uint32_t topologyNeighbors = 2;
    double topologyRadius = 0.65;
    double topologyCentroid = 0.22;
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<bool> outputClip { false };
    uint32_t meterRedrawCountdown = 0;
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* macRealtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
    std::atomic<bool> guiDirty { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

s3g::TopologyState topologyStateForPlugin(const Plugin& p)
{
    s3g::TopologyState state {};
    state.amount = p.topologyAmount;
    state.jitter = p.topologySeed;
    state.collapse = p.topologyPull;
    state.dirX = p.topologyX;
    state.dirY = p.topologyY;
    state.dirZ = p.topologyZ;
    state.twist = p.topologyTwist;
    state.flare = p.topologyFlare;
    state.shape = p.topologyShape;
    state.motionMode = p.topologyMotionMode;
    state.motionVariant = p.topologyMotionVariant;
    state.motionRateHz = p.topologyRateHz;
    state.motionDepth = p.topologyDepth;
    state.motionPhase = p.topologyPhase;
    state.neighborCount = p.topologyNeighbors;
    state.neighborRadius = p.topologyRadius;
    state.centroidAmount = p.topologyCentroid;
    return state;
}

bool topologyMotionActive(const Plugin& p)
{
    return s3g::topologyMotionActive(topologyStateForPlugin(p));
}

bool outputActive(const Plugin& p, uint32_t channel)
{
    return channel < kChannelCount && ((p.outputMask & (1u << channel)) != 0u);
}

uint32_t activeOutputCount(const Plugin& p)
{
    uint32_t count = 0;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        if (outputActive(p, ch)) {
            ++count;
        }
    }
    return count > 0 ? count : kChannelCount;
}

uint32_t activeOutputAt(const Plugin& p, uint32_t activeIndex)
{
    uint32_t seen = 0;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        if (!outputActive(p, ch)) {
            continue;
        }
        if (seen == activeIndex) {
            return ch;
        }
        ++seen;
    }
    return std::min<uint32_t>(activeIndex, kChannelCount - 1u);
}

int activeIndexForOutput(const Plugin& p, uint32_t channel)
{
    uint32_t seen = 0;
    for (uint32_t ch = 0; ch < kChannelCount; ++ch) {
        if (!outputActive(p, ch)) {
            continue;
        }
        if (ch == channel) {
            return static_cast<int>(seen);
        }
        ++seen;
    }
    return -1;
}

s3g::TopologyPoint topologyPointForActiveLane(const Plugin& p, uint32_t activeLane)
{
    return s3g::topologyPointForLane(activeLane, activeOutputCount(p), s3g::topologyControlsFromState(topologyStateForPlugin(p)));
}

s3g::TopologyPoint topologyPoint(const Plugin& p, uint32_t lane)
{
    const int activeIndex = activeIndexForOutput(p, lane);
    return topologyPointForActiveLane(p, activeIndex >= 0 ? static_cast<uint32_t>(activeIndex) : lane);
}

void requestGuiRedraw(Plugin& p)
{
#if defined(__APPLE__)
    if (p.guiView && p.guiVisible.load(std::memory_order_relaxed)) {
        p.guiDirty.store(true, std::memory_order_release);
        if (p.host && p.host->request_callback) {
            p.host->request_callback(p.host);
        }
    }
#else
    (void)p;
#endif
}

void applyTopologyMotionSceneDefaults(Plugin& p, uint32_t mode)
{
    p.topologyMotionMode = std::min<uint32_t>(s3g::kTopologyMotionModeCount - 1u, mode);
    p.topologyMotionVariant = 0;
    p.topologyPhase = 0.0;
    switch (p.topologyMotionMode) {
    case 1: p.topologyShape = 11; p.topologyAmount = 0.55; p.topologyPull = 0.22; p.topologyX = 0.35; p.topologyY = -0.20; p.topologyZ = 0.85; p.topologyTwist = 0.25; p.topologyFlare = 0.14; p.topologySeed = 0.18; p.topologyRateHz = 0.16; p.topologyDepth = 0.72; break;
    case 2: p.topologyShape = 0; p.topologyAmount = 0.42; p.topologyPull = 0.08; p.topologyX = 0.22; p.topologyY = -0.16; p.topologyZ = 0.92; p.topologyTwist = 0.08; p.topologyFlare = 0.10; p.topologySeed = 0.12; p.topologyRateHz = 0.04; p.topologyDepth = 0.65; break;
    case 3: p.topologyShape = 4; p.topologyAmount = 0.60; p.topologyPull = 0.36; p.topologyX = 0.0; p.topologyY = 0.0; p.topologyZ = 1.0; p.topologyTwist = 0.0; p.topologyFlare = 0.32; p.topologySeed = 0.06; p.topologyRateHz = 0.18; p.topologyDepth = 0.74; break;
    case 4: p.topologyShape = 3; p.topologyAmount = 0.62; p.topologyPull = 0.0; p.topologyX = 0.0; p.topologyY = 1.0; p.topologyZ = 0.12; p.topologyTwist = 0.12; p.topologyFlare = 0.0; p.topologySeed = 0.10; p.topologyRateHz = 0.10; p.topologyDepth = 0.78; break;
    case 5: p.topologyShape = 2; p.topologyAmount = 0.64; p.topologyPull = 0.42; p.topologyX = 0.0; p.topologyY = 0.2; p.topologyZ = 1.0; p.topologyTwist = 0.42; p.topologyFlare = -0.25; p.topologySeed = 0.08; p.topologyRateHz = 0.12; p.topologyDepth = 0.70; break;
    default: break;
    }
}

void applyParamsToDsp(Plugin& p)
{
    s3g::ResonantTerrainParams params = p.params;
    if (p.source == 0) {
        params.midiInfluence = 0.0f;
    } else if (p.source == 1) {
        params.density = 0.0f;
        params.midiInfluence = 1.0f;
    }
    p.synth.setParams(params);
    const auto state = topologyStateForPlugin(p);
    const auto controls = s3g::topologyControlsFromState(state);
    const double amount = std::max({ p.topologyAmount, p.topologyDepth * 0.75, p.topologyPull * 0.55, std::fabs(p.topologyTwist) * 0.45, std::fabs(p.topologyFlare) * 0.45 });
    const uint32_t activeCount = activeOutputCount(p);
    std::array<s3g::TopologyPoint, kChannelCount> activeTopo {};
    double centroidX = 0.0;
    double centroidY = 0.0;
    double centroidZ = 0.0;
    for (uint32_t i = 0; i < activeCount && i < kChannelCount; ++i) {
        activeTopo[i] = s3g::topologyPointForLane(i, activeCount, controls);
        centroidX += activeTopo[i].x;
        centroidY += activeTopo[i].y;
        centroidZ += activeTopo[i].z;
    }
    if (activeCount > 0) {
        centroidX /= static_cast<double>(activeCount);
        centroidY /= static_cast<double>(activeCount);
        centroidZ /= static_cast<double>(activeCount);
    }
    for (uint32_t lane = 0; lane < kChannelCount; ++lane) {
        const int activeIndex = activeIndexForOutput(p, lane);
        s3g::ResonantTerrainLaneParams lp {};
        if (activeIndex < 0) {
            lp.pitchSemitones = 0.0f;
            lp.brightness = 0.0f;
            lp.decay = 0.0f;
            lp.strike = 0.0f;
            lp.density = 0.0f;
            lp.material = 0.5f;
            lp.articulation = 0.5f;
            lp.size = 0.5f;
            lp.energy = 0.0f;
            lp.coupling = 0.0f;
            lp.pressure = 0.0f;
            p.synth.setLaneParams(lane, lp);
            continue;
        }
        const auto topo = activeTopo[static_cast<uint32_t>(activeIndex)];
        const float n = static_cast<float>(s3g::laneNoise(lane + 17u));
        const double radial = std::clamp((topo.radius - 0.35) / 1.35, 0.0, 1.0);
        const double size = std::clamp((topo.z + 1.0) * 0.5, 0.0, 1.0);
        const double material = std::clamp((topo.x + 1.0) * 0.5, 0.0, 1.0);
        const double articulation = std::clamp((topo.y + 1.0) * 0.5, 0.0, 1.0);
        const double dx = topo.x - centroidX;
        const double dy = topo.y - centroidY;
        const double dz = topo.z - centroidZ;
        const double centroidDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double pressure = std::clamp(p.topologyCentroid * 0.45 + p.topologyPull * 0.35 + (1.0 - std::min(1.0, centroidDistance / 1.65)) * 0.55, 0.0, 1.0);
        double nearestDistance = 2.0;
        if (activeCount > 1) {
            const auto neighbors = s3g::nearestTopologyNeighbors(state, static_cast<uint32_t>(activeIndex), activeCount);
            const int neighbor = neighbors[0];
            if (neighbor >= 0 && static_cast<uint32_t>(neighbor) < activeCount) {
                const auto other = activeTopo[static_cast<uint32_t>(neighbor)];
                const double ndx = topo.x - other.x;
                const double ndy = topo.y - other.y;
                const double ndz = topo.z - other.z;
                nearestDistance = std::sqrt(ndx * ndx + ndy * ndy + ndz * ndz);
            }
        }
        const double coupling = std::clamp((1.0 - nearestDistance / 2.25) * (0.25 + p.topologyNeighbors * 0.25) + p.topologyRadius * 0.18, 0.0, 1.0);
        const double vertical = size * 1.2 - 0.55 + articulation * 0.28;
        const double pitchField = vertical * 18.0 + (material - 0.5) * 7.0 + radial * 8.0 + n * p.topologySeed * 9.0;
        lp.pitchSemitones = static_cast<float>(std::clamp(pitchField * std::max(0.30, amount), -24.0, 24.0));
        lp.brightness = static_cast<float>(std::clamp(0.18 + material * 0.48 + articulation * 0.30 + size * 0.26 + radial * 0.20, 0.0, 1.0));
        lp.decay = static_cast<float>(std::clamp(0.24 + (1.0 - articulation) * 0.42 + pressure * 0.30 + (1.0 - size) * 0.20 + radial * 0.12, 0.0, 1.0));
        lp.strike = static_cast<float>(std::clamp(0.10 + articulation * 0.58 + radial * 0.26 + pressure * 0.12, 0.0, 1.0));
        const double densityField = 0.04 + radial * 0.68 + articulation * 0.18 + pressure * 0.18 + coupling * 0.24 + p.topologySeed * 0.12;
        lp.density = static_cast<float>(std::clamp(densityField, 0.0, 1.0));
        lp.material = static_cast<float>(material);
        lp.articulation = static_cast<float>(articulation);
        lp.size = static_cast<float>(size);
        lp.energy = static_cast<float>(radial);
        lp.coupling = static_cast<float>(coupling);
        lp.pressure = static_cast<float>(pressure);
        p.synth.setLaneParams(lane, lp);
    }
}

void advanceTopologyMotion(Plugin& p, uint32_t frames)
{
    if (!topologyMotionActive(p) || p.sampleRate <= 0.0) {
        return;
    }
    p.topologyPhase += (static_cast<double>(frames) * p.topologyRateHz) / p.sampleRate;
    p.topologyPhase -= std::floor(p.topologyPhase);
    applyParamsToDsp(p);
    requestGuiRedraw(p);
}

void setParam(Plugin& p, clap_id paramId, double value)
{
    switch (paramId) {
    case kSourceParamId: p.source = std::min<uint32_t>(2u, roundedUint(value)); break;
    case kBaseParamId: p.params.baseHz = static_cast<float>(std::clamp(value, 24.0, 880.0)); break;
    case kDensityParamId: p.params.density = static_cast<float>(clamp01(value)); break;
    case kDecayParamId: p.params.decay = static_cast<float>(clamp01(value)); break;
    case kBrightParamId: p.params.brightness = static_cast<float>(clamp01(value)); break;
    case kHarmParamId: p.params.harmonicity = static_cast<float>(clamp01(value)); break;
    case kExciterParamId: p.params.exciterTone = static_cast<float>(clamp01(value)); break;
    case kMidiParamId: p.params.midiInfluence = static_cast<float>(clamp01(value)); break;
    case kOutputParamId: p.params.outputGainDb = static_cast<float>(std::clamp(value, -48.0, -3.0)); break;
    case kTopologyShapeParamId: p.topologyShape = std::min<uint32_t>(s3g::kTopologyShapeCount - 1u, roundedUint(value)); break;
    case kTopologyAmountParamId: p.topologyAmount = clamp01(value); break;
    case kTopologyPullParamId: p.topologyPull = clamp01(value); break;
    case kTopologyXParamId: p.topologyX = clampBipolar(value); break;
    case kTopologyYParamId: p.topologyY = clampBipolar(value); break;
    case kTopologyZParamId: p.topologyZ = clampBipolar(value); break;
    case kTopologyTwistParamId: p.topologyTwist = clampBipolar(value); break;
    case kTopologyFlareParamId: p.topologyFlare = clampBipolar(value); break;
    case kTopologySeedParamId: p.topologySeed = clamp01(value); break;
    case kTopologyMotionParamId: p.topologyMotionMode = std::min<uint32_t>(s3g::kTopologyMotionModeCount - 1u, roundedUint(value)); if (p.topologyMotionMode == 0) p.topologyPhase = 0.0; break;
    case kTopologyVariantParamId: p.topologyMotionVariant = std::min<uint32_t>(s3g::kTopologyVariantCount - 1u, roundedUint(value)); break;
    case kTopologyRateParamId: p.topologyRateHz = clampRate(value); break;
    case kTopologyDepthParamId: p.topologyDepth = clamp01(value); break;
    case kTopologyNeighborsParamId: p.topologyNeighbors = std::clamp<uint32_t>(roundedUint(value), 1u, 3u); break;
    case kTopologyRadiusParamId: p.topologyRadius = clamp01(value); break;
    case kTopologyCentroidParamId: p.topologyCentroid = clamp01(value); break;
    default: return;
    }
    applyParamsToDsp(p);
    if (p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
    requestGuiRedraw(p);
}

bool applyEvent(Plugin& p, const clap_event_header_t* ev)
{
    if (!ev || ev->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return false;
    }
    if (ev->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
        setParam(p, param->param_id, param->value);
        return true;
    }
    if (ev->type == CLAP_EVENT_NOTE_ON) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(ev);
        p.synth.noteOn(note->key, static_cast<float>(note->velocity));
        return true;
    }
    if (ev->type == CLAP_EVENT_NOTE_OFF || ev->type == CLAP_EVENT_NOTE_CHOKE) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(ev);
        p.synth.noteOff(note->key);
        return true;
    }
    return false;
}

void readEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) applyEvent(p, in->get(in, i));
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->hostTail = static_cast<const clap_host_tail_t*>(p->host && p->host->get_extension ? p->host->get_extension(p->host, CLAP_EXT_TAIL) : nullptr);
    return true;
}

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    s3g::clap_support::beginRealtimeActivity(p->macRealtimeActivity);
#endif
    p->sampleRate = sampleRate;
    p->maxFrames = maxFrames;
    p->meterRedrawCountdown = static_cast<uint32_t>(std::max(1.0, sampleRate / 24.0));
    p->synth.prepare(sampleRate);
    applyParamsToDsp(*p);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    s3g::clap_support::endRealtimeActivity(self(plugin)->macRealtimeActivity);
#else
    (void)plugin;
#endif
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { auto* p = self(plugin); p->synth.reset(); applyParamsToDsp(*p); }

void publishMeter(Plugin& p, float peak, bool clip, uint32_t frames)
{
    const float prev = p.outputPeak.load(std::memory_order_relaxed);
    p.outputPeak.store(std::max(prev * 0.94f, peak), std::memory_order_relaxed);
    if (clip) p.outputClip.store(true, std::memory_order_relaxed);
#if defined(__APPLE__)
    if (p.guiView) {
        if (p.meterRedrawCountdown <= frames) { p.meterRedrawCountdown = static_cast<uint32_t>(std::max(1.0, p.sampleRate / 24.0)); requestGuiRedraw(p); }
        else { p.meterRedrawCountdown -= frames; }
    }
#else
    (void)frames;
#endif
}

void processSegment(Plugin& p, const clap_audio_buffer_t& output, uint32_t start, uint32_t frames)
{
    advanceTopologyMotion(p, frames);
    std::array<float, kChannelCount> frame {};
    float peak = 0.0f;
    bool clip = false;
    for (uint32_t i = start; i < start + frames; ++i) {
        p.synth.processFrame(frame.data());
        for (uint32_t ch = 0; ch < output.channel_count; ++ch) {
            const float value = (ch < kChannelCount && outputActive(p, ch)) ? frame[ch] : 0.0f;
            peak = std::max(peak, std::fabs(value));
            clip = clip || std::fabs(value) >= 0.98f;
            if (output.data32 && output.data32[ch]) output.data32[ch][i] = value;
            if (output.data64 && output.data64[ch]) output.data64[ch][i] = static_cast<double>(value);
        }
    }
    publishMeter(p, peak, clip, frames);
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    const uint32_t frames = proc->frames_count;
    if (proc->audio_outputs_count == 0) { readEvents(*p, proc->in_events); return CLAP_PROCESS_CONTINUE; }
    const auto& output = proc->audio_outputs[0];
    const uint32_t eventCount = proc->in_events ? proc->in_events->size(proc->in_events) : 0;
    uint32_t cursor = 0;
    for (uint32_t e = 0; e < eventCount; ++e) {
        const clap_event_header_t* ev = proc->in_events->get(proc->in_events, e);
        if (!ev) continue;
        const uint32_t t = std::min<uint32_t>(ev->time, frames);
        if (t > cursor) { processSegment(*p, output, cursor, t - cursor); cursor = t; }
        applyEvent(*p, ev);
    }
    if (cursor < frames) processSegment(*p, output, cursor, frames - cursor);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p && p->guiView && p->guiDirty.exchange(false, std::memory_order_acquire)) {
        [static_cast<NSView*>(p->guiView) setNeedsDisplay:YES];
    }
#else
    (void)plugin;
#endif
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0 || !info) return false;
    info->id = 20;
    std::strncpy(info->name, "8ch Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kChannelCount;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0 || !info) return false;
    info->id = 30;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "MIDI / Notes", sizeof(info->name));
    return true;
}

const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

struct ParamDef { clap_id id; const char* name; const char* module; double min; double max; double def; bool stepped; };

const ParamDef kParamDefs[] = {
    { kSourceParamId, "Source", "Resonant Terrain", 0, 2, 2, true },
    { kBaseParamId, "Base Frequency", "Resonant Terrain", 24, 880, 72, false },
    { kDensityParamId, "Density", "Resonant Terrain", 0, 1, 0.62, false },
    { kDecayParamId, "Decay", "Resonant Terrain", 0, 1, 0.72, false },
    { kBrightParamId, "Brightness", "Resonant Terrain", 0, 1, 0.58, false },
    { kHarmParamId, "Harmonicity", "Resonant Terrain", 0, 1, 0.48, false },
    { kExciterParamId, "Exciter Tone", "Resonant Terrain", 0, 1, 0.42, false },
    { kMidiParamId, "MIDI Influence", "Resonant Terrain", 0, 1, 0.65, false },
    { kOutputParamId, "Output", "Resonant Terrain", -48, -3, -12, false },
    { kTopologyShapeParamId, "Topology Shape", "Topology", 0, double(s3g::kTopologyShapeCount - 1u), 0, true },
    { kTopologyAmountParamId, "Topology Amount", "Topology", 0, 1, 0.35, false },
    { kTopologyPullParamId, "Topology Pull", "Topology", 0, 1, 0, false },
    { kTopologyXParamId, "Topology X", "Topology", -1, 1, 0, false },
    { kTopologyYParamId, "Topology Y", "Topology", -1, 1, 0, false },
    { kTopologyZParamId, "Topology Z", "Topology", -1, 1, 1, false },
    { kTopologyTwistParamId, "Topology Twist", "Topology", -1, 1, 0, false },
    { kTopologyFlareParamId, "Topology Flare", "Topology", -1, 1, 0, false },
    { kTopologySeedParamId, "Topology Seed", "Topology", 0, 1, 0.12, false },
    { kTopologyMotionParamId, "Topology Animation", "Topology", 0, double(s3g::kTopologyMotionModeCount - 1u), 0, true },
    { kTopologyVariantParamId, "Topology Variant", "Topology", 0, double(s3g::kTopologyVariantCount - 1u), 0, true },
    { kTopologyRateParamId, "Topology Rate", "Topology", kTopologyMotionMinHz, kTopologyMotionMaxHz, 0.10, false },
    { kTopologyDepthParamId, "Topology Depth", "Topology", 0, 1, 0, false },
    { kTopologyNeighborsParamId, "Topology Neighbors", "Topology", 1, 3, 2, true },
    { kTopologyRadiusParamId, "Topology Radius", "Topology", 0, 1, 0.65, false },
    { kTopologyCentroidParamId, "Topology Centroid", "Topology", 0, 1, 0.22, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(sizeof(kParamDefs) / sizeof(kParamDefs[0])); }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& d = kParamDefs[index];
    info->id = d.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (d.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, d.name, sizeof(info->name));
    std::strncpy(info->module, d.module, sizeof(info->module));
    info->min_value = d.min; info->max_value = d.max; info->default_value = d.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    const auto* p = self(plugin);
    switch (id) {
    case kSourceParamId: *value = double(p->source); return true;
    case kBaseParamId: *value = p->params.baseHz; return true;
    case kDensityParamId: *value = p->params.density; return true;
    case kDecayParamId: *value = p->params.decay; return true;
    case kBrightParamId: *value = p->params.brightness; return true;
    case kHarmParamId: *value = p->params.harmonicity; return true;
    case kExciterParamId: *value = p->params.exciterTone; return true;
    case kMidiParamId: *value = p->params.midiInfluence; return true;
    case kOutputParamId: *value = p->params.outputGainDb; return true;
    case kTopologyShapeParamId: *value = double(p->topologyShape); return true;
    case kTopologyAmountParamId: *value = p->topologyAmount; return true;
    case kTopologyPullParamId: *value = p->topologyPull; return true;
    case kTopologyXParamId: *value = p->topologyX; return true;
    case kTopologyYParamId: *value = p->topologyY; return true;
    case kTopologyZParamId: *value = p->topologyZ; return true;
    case kTopologyTwistParamId: *value = p->topologyTwist; return true;
    case kTopologyFlareParamId: *value = p->topologyFlare; return true;
    case kTopologySeedParamId: *value = p->topologySeed; return true;
    case kTopologyMotionParamId: *value = double(p->topologyMotionMode); return true;
    case kTopologyVariantParamId: *value = double(p->topologyMotionVariant); return true;
    case kTopologyRateParamId: *value = p->topologyRateHz; return true;
    case kTopologyDepthParamId: *value = p->topologyDepth; return true;
    case kTopologyNeighborsParamId: *value = double(p->topologyNeighbors); return true;
    case kTopologyRadiusParamId: *value = p->topologyRadius; return true;
    case kTopologyCentroidParamId: *value = p->topologyCentroid; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0) return false;
    switch (id) {
    case kSourceParamId: std::snprintf(display, size, "%s", sourceName(roundedUint(value))); return true;
    case kBaseParamId: std::snprintf(display, size, "%.1f Hz", value); return true;
    case kOutputParamId: std::snprintf(display, size, "%+.1f dB", value); return true;
    case kTopologyShapeParamId: std::snprintf(display, size, "%s", s3g::topologyShapeName(roundedUint(value))); return true;
    case kTopologyMotionParamId: std::snprintf(display, size, "%s", s3g::topologyMotionModeName(roundedUint(value))); return true;
    case kTopologyVariantParamId: std::snprintf(display, size, "%s", s3g::topologyVariantName(roundedUint(value))); return true;
    case kTopologyRateParamId: std::snprintf(display, size, "%.3f Hz", value); return true;
    case kTopologyNeighborsParamId: std::snprintf(display, size, "%u", std::clamp<uint32_t>(roundedUint(value), 1u, 3u)); return true;
    default:
        if (id == kTopologyXParamId || id == kTopologyYParamId || id == kTopologyZParamId || id == kTopologyTwistParamId || id == kTopologyFlareParamId) std::snprintf(display, size, "%+.1f %%", value * 100.0);
        else std::snprintf(display, size, "%.1f %%", value * 100.0);
        return true;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kSourceParamId) { for (uint32_t i = 0; i < 3; ++i) if (std::strcmp(display, sourceName(i)) == 0) { *value = i; return true; } }
    if (id == kTopologyShapeParamId) { for (uint32_t i = 0; i < s3g::kTopologyShapeCount; ++i) if (std::strcmp(display, s3g::topologyShapeName(i)) == 0) { *value = i; return true; } }
    if (id == kTopologyMotionParamId) { for (uint32_t i = 0; i < s3g::kTopologyMotionModeCount; ++i) if (std::strcmp(display, s3g::topologyMotionModeName(i)) == 0) { *value = i; return true; } }
    if (id == kTopologyVariantParamId) { for (uint32_t i = 0; i < s3g::kTopologyVariantCount; ++i) if (std::strcmp(display, s3g::topologyVariantName(i)) == 0) { *value = i; return true; } }
    *value = std::atof(display);
    if (std::strchr(display, '%') && id != kOutputParamId) *value *= 0.01;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const auto* p = self(plugin);
    SavedState s {};
    s.source = p->source; s.outputMask = p->outputMask; s.baseHz = p->params.baseHz; s.density = p->params.density; s.decay = p->params.decay; s.brightness = p->params.brightness; s.harmonicity = p->params.harmonicity; s.exciterTone = p->params.exciterTone; s.midiInfluence = p->params.midiInfluence; s.outputGainDb = p->params.outputGainDb;
    s.topologyShape = p->topologyShape; s.topologyMotionMode = p->topologyMotionMode; s.topologyMotionVariant = p->topologyMotionVariant; s.topologyAmount = p->topologyAmount; s.topologyPull = p->topologyPull; s.topologyX = p->topologyX; s.topologyY = p->topologyY; s.topologyZ = p->topologyZ; s.topologyTwist = p->topologyTwist; s.topologyFlare = p->topologyFlare; s.topologySeed = p->topologySeed; s.topologyRateHz = p->topologyRateHz; s.topologyDepth = p->topologyDepth; s.topologyNeighbors = p->topologyNeighbors; s.topologyRadius = p->topologyRadius; s.topologyCentroid = p->topologyCentroid;
    return stream->write(stream, &s, sizeof(s)) == static_cast<int64_t>(sizeof(s));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState s {};
    if (stream->read(stream, &s, sizeof(s)) != static_cast<int64_t>(sizeof(s)) || s.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->source = std::min<uint32_t>(2u, s.source); p->outputMask = (s.outputMask & 0xffu) != 0u ? (s.outputMask & 0xffu) : 0xffu; p->params.baseHz = s.baseHz; p->params.density = std::max(0.0, s.density); p->params.decay = s.decay; p->params.brightness = s.brightness; p->params.harmonicity = s.harmonicity; p->params.exciterTone = s.exciterTone; p->params.midiInfluence = s.midiInfluence; p->params.outputGainDb = s.outputGainDb;
    p->topologyShape = std::min<uint32_t>(s3g::kTopologyShapeCount - 1u, s.topologyShape); p->topologyMotionMode = std::min<uint32_t>(s3g::kTopologyMotionModeCount - 1u, s.topologyMotionMode); p->topologyMotionVariant = std::min<uint32_t>(s3g::kTopologyVariantCount - 1u, s.topologyMotionVariant); p->topologyAmount = clamp01(s.topologyAmount); p->topologyPull = clamp01(s.topologyPull); p->topologyX = clampBipolar(s.topologyX); p->topologyY = clampBipolar(s.topologyY); p->topologyZ = clampBipolar(s.topologyZ); p->topologyTwist = clampBipolar(s.topologyTwist); p->topologyFlare = clampBipolar(s.topologyFlare); p->topologySeed = clamp01(s.topologySeed); p->topologyRateHz = clampRate(s.topologyRateHz); p->topologyDepth = clamp01(s.topologyDepth); p->topologyNeighbors = std::clamp<uint32_t>(s.topologyNeighbors, 1u, 3u); p->topologyRadius = clamp01(s.topologyRadius); p->topologyCentroid = clamp01(s.topologyCentroid);
    applyParamsToDsp(*p); requestGuiRedraw(*p); return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };
uint32_t latencyGet(const clap_plugin_t*) { return 0; }
const clap_plugin_latency_t latencyExt { latencyGet };
uint32_t tailGet(const clap_plugin_t* plugin) { return static_cast<uint32_t>(std::ceil(self(plugin)->sampleRate * 8.0)); }
const clap_plugin_tail_t tailExt { tailGet };

#if defined(__APPLE__)
} // namespace

@interface S3GResonantTerrainView : NSView { void* _plugin; int _dragSlider; double _yaw; double _pitch; bool _showEngine; bool _showTopo; bool _showPins; int _openMenu; NSPoint _menuOrigin; uint32_t _menuItems; NSTimer* _timer; }
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)pt;
@end

static NSColor* c(int rgb) { return s3g::clap_gui::color(rgb); }
static NSColor* heat(double value, double alpha) { return s3g::clap_gui::heatColor(value, alpha); }

@implementation S3GResonantTerrainView
- (id)initWithPlugin:(void*)plugin { self=[super initWithFrame:NSMakeRect(0,0,1000,700)]; if(self){_plugin=plugin;_dragSlider=-1;_yaw=-0.52;_pitch=0.34;_showEngine=true;_showTopo=true;_showPins=true;_openMenu=0;_menuItems=0;_timer=nil;} return self; }
- (BOOL)isFlipped { return YES; }
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (void)startRefreshTimer { if(_timer) return; _timer=[NSTimer timerWithTimeInterval:1.0/30.0 target:self selector:@selector(refresh:) userInfo:nil repeats:YES]; [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes]; }
- (void)stopRefreshTimer { if(_timer){[_timer invalidate];_timer=nil;} }
- (void)refresh:(NSTimer*)t { (void)t; if([self isHidden]||!_plugin||!s3g::clap_support::hostAppIsActive()) return; if(topologyMotionActive(*static_cast<Plugin*>(_plugin))) [self setNeedsDisplay:YES]; }
- (void)drawSlider:(NSString*)name value:(NSString*)val norm:(CGFloat)n y:(CGFloat)y label:(NSDictionary*)lab small:(NSDictionary*)small { s3g::clap_gui::Style style; s3g::clap_gui::drawSlider(name, val, n, y, lab, small, style); }
- (void)drawMenu:(NSString*)name value:(NSString*)val y:(CGFloat)y label:(NSDictionary*)lab small:(NSDictionary*)small { s3g::clap_gui::Style style; s3g::clap_gui::drawMenu(name, val, y, lab, small, style); }
- (void)drawRect:(NSRect)dirty { (void)dirty; auto* p=static_cast<Plugin*>(_plugin); [c(0x0c0c0c) setFill]; NSRectFill([self bounds]); NSFont* mono=[NSFont fontWithName:@"Menlo" size:10]?:[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular]; NSFont* bold=[NSFont fontWithName:@"Menlo-Bold" size:10]?:[NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightBold]; NSDictionary* lab=@{NSForegroundColorAttributeName:c(0xf0f0f0),NSFontAttributeName:bold}; NSDictionary* small=@{NSForegroundColorAttributeName:c(0x9e9e9e),NSFontAttributeName:mono}; [@"s3g Resonant Terrain" drawAtPoint:NSMakePoint(18,13) withAttributes:lab]; [[NSString stringWithFormat:@"%uCH",kChannelCount] drawAtPoint:NSMakePoint(948,14) withAttributes:small]; float pk=p->outputPeak.load(std::memory_order_relaxed); bool cl=p->outputClip.exchange(false,std::memory_order_relaxed); [[NSString stringWithFormat:cl?@"PK %+4.1f CLIP":@"PK %+4.1f",20.0*std::log10(std::max(0.000001f,pk))] drawAtPoint:NSMakePoint(808,14) withAttributes:cl?lab:small]; NSRect panel=NSMakeRect(12,34,620,654); [c(0x1d1d1d) setFill]; NSRectFill(panel); [c(0x626262) setStroke]; NSFrameRect(panel); [@"TOPOLOGY" drawAtPoint:NSMakePoint(18,39) withAttributes:lab]; NSRect topo=NSMakeRect(52,72,540,330), hm=NSMakeRect(52,420,540,180); [c(0x101010) setFill]; NSRectFill(topo); NSRectFill(hm); [c(0x626262) setStroke]; NSFrameRect(topo); NSFrameRect(hm); constexpr uint32_t cols=54, rows=18; std::array<double,cols*rows> h{}; const uint32_t visualLanes=activeOutputCount(*p); double hmax=s3g::fillTopologyHeatmap(topologyStateForPlugin(*p),visualLanes,cols,rows,h.data()); for(uint32_t r=0;r<rows;++r) for(uint32_t col=0;col<cols;++col){ double n=std::pow(std::clamp(h[r*cols+col]/hmax,0.0,1.0),0.72); [heat(n, 1.0) setFill]; NSRectFill(NSMakeRect(hm.origin.x+col*(hm.size.width/cols),hm.origin.y+r*(hm.size.height/rows),hm.size.width/cols,hm.size.height/rows)); }
    auto proj=[&](double x,double y,double z){ double cy=std::cos(_yaw),sy=std::sin(_yaw),cp=std::cos(_pitch),sp=std::sin(_pitch); double xr=x*cy-z*sy,zr=x*sy+z*cy,yr=y*cp-zr*sp,zz=y*sp+zr*cp,sc=0.82+zz*0.08; return NSMakePoint(topo.origin.x+topo.size.width*0.5+xr*topo.size.width*0.27*sc,topo.origin.y+topo.size.height*0.52-yr*topo.size.height*0.38*sc); };
    std::array<NSPoint,kChannelCount> pts{}; for(uint32_t i=0;i<visualLanes;++i){ auto tp=topologyPointForActiveLane(*p,i); double rad=0.82+tp.radius*0.28+p->topologyAmount*0.24; pts[i]=proj(tp.x*rad,tp.y*rad,tp.z*rad); }
    bool drawn[kChannelCount][kChannelCount]{}; [c(0xb0b0b0) setStroke]; for(uint32_t i=0;i<visualLanes;++i){ auto nn=s3g::nearestTopologyNeighbors(topologyStateForPlugin(*p),i,visualLanes); for(uint32_t s=0;s<std::clamp<uint32_t>(p->topologyNeighbors,1,3);++s){ int j=nn[s]; if(j<0||uint32_t(j)>=visualLanes||j==int(i)) continue; uint32_t a=std::min<uint32_t>(i,j),b=std::max<uint32_t>(i,j); if(!drawn[a][b]){drawn[a][b]=true; [NSBezierPath strokeLineFromPoint:pts[a] toPoint:pts[b]];}}}
    for(uint32_t i=0;i<visualLanes;++i){ const uint32_t out=activeOutputAt(*p,i); [c(0xd1d1d1) setFill]; NSRectFill(NSMakeRect(pts[i].x-5,pts[i].y-5,10,10)); [[NSString stringWithFormat:@"%u",out+1] drawAtPoint:NSMakePoint(pts[i].x+7,pts[i].y-8) withAttributes:small]; }
    [[NSString stringWithFormat:@"SRC %@  OUT %u  SHAPE %@",[NSString stringWithUTF8String:sourceName(p->source)],visualLanes,[NSString stringWithUTF8String:s3g::topologyShapeName(p->topologyShape)]] drawAtPoint:NSMakePoint(360,82) withAttributes:small]; [[NSString stringWithFormat:@"ANIM %@",[NSString stringWithUTF8String:s3g::topologyMotionModeName(p->topologyMotionMode)]] drawAtPoint:NSMakePoint(392,97) withAttributes:small];
    CGFloat x=644,y=34,w=344,hh=21,gap=8; auto frame=[&](CGFloat yy,CGFloat h){[c(0x1d1d1d) setFill];NSRectFill(NSMakeRect(x,yy,w,h));[c(0x626262) setStroke];NSFrameRect(NSMakeRect(x,yy,w,h));}; auto header=[&](NSString* title,bool open,CGFloat yy){[c(0x141414) setFill];NSRectFill(NSMakeRect(x,yy,w,hh));[c(0xd1d1d1) setFill];NSRectFill(NSMakeRect(x,yy,w,2));[(open?@"-":@"+") drawAtPoint:NSMakePoint(x+8,yy+5) withAttributes:lab];[title drawAtPoint:NSMakePoint(x+24,yy+5) withAttributes:lab];}; frame(y,_showEngine?218:hh); header(@"ENGINE",_showEngine,y); if(_showEngine){ [self drawMenu:@"SRC" value:[NSString stringWithUTF8String:sourceName(p->source)] y:y+26 label:small small:small]; [self drawSlider:@"BASE" value:[NSString stringWithFormat:@"%.1f",p->params.baseHz] norm:(p->params.baseHz-24)/(880-24) y:y+44 label:small small:small]; [self drawSlider:@"DEN" value:[NSString stringWithFormat:@"%.0f%%",p->params.density*100] norm:p->params.density y:y+62 label:small small:small]; [self drawSlider:@"DEC" value:[NSString stringWithFormat:@"%.0f%%",p->params.decay*100] norm:p->params.decay y:y+80 label:small small:small]; [self drawSlider:@"BRI" value:[NSString stringWithFormat:@"%.0f%%",p->params.brightness*100] norm:p->params.brightness y:y+98 label:small small:small]; [self drawSlider:@"HRM" value:[NSString stringWithFormat:@"%.0f%%",p->params.harmonicity*100] norm:p->params.harmonicity y:y+116 label:small small:small]; [self drawSlider:@"EXC" value:[NSString stringWithFormat:@"%.0f%%",p->params.exciterTone*100] norm:p->params.exciterTone y:y+134 label:small small:small]; [self drawSlider:@"MIDI" value:[NSString stringWithFormat:@"%.0f%%",p->params.midiInfluence*100] norm:p->params.midiInfluence y:y+152 label:small small:small]; [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f",p->params.outputGainDb] norm:(p->params.outputGainDb+48)/42 y:y+170 label:small small:small]; NSString* srcHint=p->source==1?@"MIDI notes excite terrain":(p->source==0?@"INT self-excites terrain":@"HYB self + MIDI excitation"); [srcHint drawAtPoint:NSMakePoint(750,y+192) withAttributes:small]; } y+=(_showEngine?218:hh)+gap; frame(y,_showTopo?328:hh); header(@"TOPOLOGY",_showTopo,y); [@"RESET" drawAtPoint:NSMakePoint(933,y+5) withAttributes:small]; if(_showTopo){ s3g::clap_gui::TopologyUiValues topoValues; topoValues.shape=s3g::topologyShapeName(p->topologyShape); topoValues.amount=p->topologyAmount; topoValues.pull=p->topologyPull; topoValues.x=p->topologyX; topoValues.y=p->topologyY; topoValues.z=p->topologyZ; topoValues.twist=p->topologyTwist; topoValues.flare=p->topologyFlare; topoValues.seed=p->topologySeed; topoValues.motion=s3g::topologyMotionModeName(p->topologyMotionMode); topoValues.variant=s3g::topologyVariantName(p->topologyMotionVariant); topoValues.rateHz=p->topologyRateHz; topoValues.rateMinHz=kTopologyMotionMinHz; topoValues.rateMaxHz=kTopologyMotionMaxHz; topoValues.depth=p->topologyDepth; topoValues.neighbors=p->topologyNeighbors; topoValues.neighborSuffix=false; topoValues.radius=p->topologyRadius; topoValues.centroid=p->topologyCentroid; s3g::clap_gui::Style style; s3g::clap_gui::drawTopologyRows(topoValues,y,small,small,style); } y+=(_showTopo?328:hh)+gap; frame(y,_showPins?96:hh); header(@"OUTPUT MATRIX",_showPins,y); if(_showPins){ CGFloat px=722,py=y+43,cell=26; [@"OUT" drawAtPoint:NSMakePoint(654,py+2) withAttributes:small]; for(uint32_t i=0;i<kChannelCount;++i){ [[NSString stringWithFormat:@"%u",i+1] drawAtPoint:NSMakePoint(px+i*cell+6,py-18) withAttributes:small]; NSRect r=NSMakeRect(px+i*cell,py,cell-5,cell-5); [c(0x141414) setFill]; NSRectFill(r); [c(0x626262) setStroke]; NSFrameRect(r); if(outputActive(*p,i)){ [c(0xd1d1d1) setFill]; NSRectFill(NSInsetRect(r,5,5)); } } [@"ALL" drawAtPoint:NSMakePoint(654,y+70) withAttributes:small]; [@"2CH" drawAtPoint:NSMakePoint(706,y+70) withAttributes:small]; [@"CLR" drawAtPoint:NSMakePoint(928,y+70) withAttributes:small]; }
    if(_openMenu>0&&_menuItems>0){ CGFloat itemH=18,mw=178; NSRect mr=NSMakeRect(_menuOrigin.x,_menuOrigin.y,mw,itemH*_menuItems); [c(0x1d1d1d) setFill]; NSRectFill(mr); [c(0x626262) setStroke]; NSFrameRect(mr); for(uint32_t i=0;i<_menuItems;++i){ NSRect row=NSMakeRect(_menuOrigin.x,_menuOrigin.y+i*itemH,mw,itemH); if((_openMenu==1&&i==p->source)||(_openMenu==2&&i==p->topologyShape)||(_openMenu==3&&i==p->topologyMotionMode)||(_openMenu==4&&i==p->topologyMotionVariant)||(_openMenu==5&&i+1==p->topologyNeighbors)){[c(0x2c2c2c) setFill];NSRectFill(NSInsetRect(row,1,1));} NSString* txt=@""; if(_openMenu==1)txt=[NSString stringWithUTF8String:sourceName(i)]; else if(_openMenu==2)txt=[NSString stringWithUTF8String:s3g::topologyShapeName(i)]; else if(_openMenu==3)txt=[NSString stringWithUTF8String:s3g::topologyMotionModeName(i)]; else if(_openMenu==4)txt=[NSString stringWithUTF8String:s3g::topologyVariantName(i)]; else txt=[NSString stringWithFormat:@"%u",i+1]; [txt drawAtPoint:NSMakePoint(row.origin.x+9,row.origin.y+3) withAttributes:small]; }} }
- (void)updateSlider:(NSPoint)pt { auto* p=static_cast<Plugin*>(_plugin); double n=std::clamp((pt.x-750.0)/150.0,0.0,1.0); switch(_dragSlider){ case 1:p->params.baseHz=24+n*(880-24);break; case 2:p->params.density=n;break; case 3:p->params.decay=n;break; case 4:p->params.brightness=n;break; case 5:p->params.harmonicity=n;break; case 6:p->params.exciterTone=n;break; case 7:p->params.midiInfluence=n;break; case 8:p->params.outputGainDb=-48+n*42;break; case 10:p->topologyAmount=n;break; case 11:p->topologyPull=n;break; case 12:p->topologyX=n*2-1;break; case 13:p->topologyY=n*2-1;break; case 14:p->topologyZ=n*2-1;break; case 15:p->topologyTwist=n*2-1;break; case 16:p->topologyFlare=n*2-1;break; case 17:p->topologySeed=n;break; case 20:p->topologyRateHz=kTopologyMotionMinHz+n*(kTopologyMotionMaxHz-kTopologyMotionMinHz);break; case 21:p->topologyDepth=n;break; case 23:p->topologyRadius=n;break; case 24:p->topologyCentroid=n;break; default:return;} applyParamsToDsp(*p); [self setNeedsDisplay:YES]; }
- (void)mouseDown:(NSEvent*)ev { NSPoint pt=[self convertPoint:[ev locationInWindow] fromView:nil]; auto* p=static_cast<Plugin*>(_plugin); if(_openMenu>0){ CGFloat itemH=18; NSRect mr=NSMakeRect(_menuOrigin.x,_menuOrigin.y,178,itemH*_menuItems); if(NSPointInRect(pt,mr)){ uint32_t idx=std::min<uint32_t>(_menuItems-1,(pt.y-_menuOrigin.y)/itemH); if(_openMenu==1)p->source=idx; else if(_openMenu==2)p->topologyShape=idx; else if(_openMenu==3)applyTopologyMotionSceneDefaults(*p,idx); else if(_openMenu==4)p->topologyMotionVariant=idx; else if(_openMenu==5)p->topologyNeighbors=idx+1; applyParamsToDsp(*p);} _openMenu=0;_menuItems=0;[self setNeedsDisplay:YES];return;} CGFloat y=34,hh=21,gap=8; if(NSPointInRect(pt,NSMakeRect(644,y,344,hh))){_showEngine=!_showEngine;[self setNeedsDisplay:YES];return;} if(_showEngine){ if(NSPointInRect(pt,NSMakeRect(650,y+24,330,20))){_openMenu=1;_menuItems=3;_menuOrigin=NSMakePoint(750,y+48);[self setNeedsDisplay:YES];return;} for(uint32_t i=1;i<=8;++i){ if(NSPointInRect(pt,NSMakeRect(650,y+26+i*18,330,20))){_dragSlider=i;[self updateSlider:pt];return;}} } y+=(_showEngine?218:hh)+gap; if(NSPointInRect(pt,NSMakeRect(924,y+4,54,15))){ p->topologyShape=0;p->topologyAmount=.35;p->topologyPull=0;p->topologyX=0;p->topologyY=0;p->topologyZ=1;p->topologyTwist=0;p->topologyFlare=0;p->topologySeed=.12;p->topologyMotionMode=0;p->topologyMotionVariant=0;p->topologyRateHz=.10;p->topologyDepth=0;p->topologyNeighbors=2;p->topologyRadius=.65;p->topologyCentroid=.22;p->topologyPhase=0;applyParamsToDsp(*p);[self setNeedsDisplay:YES];return;} if(NSPointInRect(pt,NSMakeRect(644,y,344,hh))){_showTopo=!_showTopo;[self setNeedsDisplay:YES];return;} if(_showTopo){ auto row=s3g::clap_gui::hitTopologyRow(pt,y); if(row==s3g::clap_gui::TopologyRow::Shape){_openMenu=2;_menuItems=s3g::kTopologyShapeCount;_menuOrigin=NSMakePoint(750,s3g::clap_gui::topologyRowY(y,row)+18);[self setNeedsDisplay:YES];return;} if(row==s3g::clap_gui::TopologyRow::Motion){_openMenu=3;_menuItems=s3g::kTopologyMotionModeCount;_menuOrigin=NSMakePoint(750,s3g::clap_gui::topologyRowY(y,row)+18);[self setNeedsDisplay:YES];return;} if(row==s3g::clap_gui::TopologyRow::Variant){_openMenu=4;_menuItems=s3g::kTopologyVariantCount;_menuOrigin=NSMakePoint(750,s3g::clap_gui::topologyRowY(y,row)+18);[self setNeedsDisplay:YES];return;} if(row==s3g::clap_gui::TopologyRow::Neighbors){_openMenu=5;_menuItems=3;_menuOrigin=NSMakePoint(750,s3g::clap_gui::topologyRowY(y,row)+18);[self setNeedsDisplay:YES];return;} switch(row){ case s3g::clap_gui::TopologyRow::Amount:_dragSlider=10;break; case s3g::clap_gui::TopologyRow::Pull:_dragSlider=11;break; case s3g::clap_gui::TopologyRow::X:_dragSlider=12;break; case s3g::clap_gui::TopologyRow::Y:_dragSlider=13;break; case s3g::clap_gui::TopologyRow::Z:_dragSlider=14;break; case s3g::clap_gui::TopologyRow::Twist:_dragSlider=15;break; case s3g::clap_gui::TopologyRow::Flare:_dragSlider=16;break; case s3g::clap_gui::TopologyRow::Seed:_dragSlider=17;break; case s3g::clap_gui::TopologyRow::Rate:_dragSlider=20;break; case s3g::clap_gui::TopologyRow::Depth:_dragSlider=21;break; case s3g::clap_gui::TopologyRow::Radius:_dragSlider=23;break; case s3g::clap_gui::TopologyRow::Centroid:_dragSlider=24;break; default:_dragSlider=-1;break;} if(_dragSlider>=0){[self updateSlider:pt];return;} } y+=(_showTopo?328:hh)+gap; if(NSPointInRect(pt,NSMakeRect(644,y,344,hh))){_showPins=!_showPins;[self setNeedsDisplay:YES];return;} if(_showPins){ CGFloat px=722,py=y+43,cell=26; if(NSPointInRect(pt,NSMakeRect(650,y+66,34,18))){p->outputMask=0xff;applyParamsToDsp(*p);[self setNeedsDisplay:YES];return;} if(NSPointInRect(pt,NSMakeRect(702,y+66,34,18))){p->outputMask=0x03;applyParamsToDsp(*p);[self setNeedsDisplay:YES];return;} if(NSPointInRect(pt,NSMakeRect(924,y+66,34,18))){p->outputMask=0x03;applyParamsToDsp(*p);[self setNeedsDisplay:YES];return;} for(uint32_t i=0;i<kChannelCount;++i){ if(NSPointInRect(pt,NSMakeRect(px+i*cell,py,cell-5,cell-5))){ uint32_t next=p->outputMask^(1u<<i); if((next&0xffu)==0u) next=(1u<<i); p->outputMask=next&0xffu; applyParamsToDsp(*p); [self setNeedsDisplay:YES]; return; } } } if(NSPointInRect(pt,NSMakeRect(52,72,540,330))){_dragSlider=100;return;} }
- (void)mouseDragged:(NSEvent*)ev { NSPoint pt=[self convertPoint:[ev locationInWindow] fromView:nil]; if(_dragSlider==100){_yaw += [ev deltaX]*0.015; _pitch=std::clamp(_pitch+[ev deltaY]*0.012,-0.75,0.95); [self setNeedsDisplay:YES]; return;} [self updateSlider:pt]; }
- (void)mouseUp:(NSEvent*)ev { (void)ev; _dragSlider=-1; }
@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA)==0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if(!api||!isFloating)return false; *api=CLAP_WINDOW_API_COCOA; *isFloating=false; return true; }
bool guiCreate(const clap_plugin_t* plugin,const char* api,bool isFloating){ if(!guiIsApiSupported(plugin,api,isFloating))return false; auto* p=self(plugin); if(p->guiView)return true; p->guiView=[[S3GResonantTerrainView alloc] initWithPlugin:p]; return p->guiView!=nullptr; }
void guiDestroy(const clap_plugin_t* plugin){ auto* p=self(plugin); if(p->guiView){ p->guiVisible.store(false); p->guiDirty.store(false); NSView* v=static_cast<NSView*>(p->guiView); if([v respondsToSelector:@selector(stopRefreshTimer)]) [static_cast<S3GResonantTerrainView*>(v) stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView=nullptr; } }
bool guiSetScale(const clap_plugin_t*,double){return true;} bool guiGetSize(const clap_plugin_t*,uint32_t* w,uint32_t* h){if(!w||!h)return false;*w=1000;*h=700;return true;} bool guiCanResize(const clap_plugin_t*){return false;} bool guiGetResizeHints(const clap_plugin_t*,clap_gui_resize_hints_t*){return false;} bool guiAdjustSize(const clap_plugin_t*,uint32_t*,uint32_t*){return false;} bool guiSetSize(const clap_plugin_t* plugin,uint32_t w,uint32_t h){auto* p=self(plugin); if(!p->guiView)return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w,h)]; return true;} bool guiSetParent(const clap_plugin_t* plugin,const clap_window_t* win){ if(!win||std::strcmp(win->api,CLAP_WINDOW_API_COCOA)!=0||!win->cocoa)return false; auto* p=self(plugin); if(!p->guiView)return false; NSView* parent=static_cast<NSView*>(win->cocoa); NSView* v=static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0,0,1000,700)]; return true;} bool guiSetTransient(const clap_plugin_t*,const clap_window_t*){return false;} void guiSuggestTitle(const clap_plugin_t*,const char*){} bool guiShow(const clap_plugin_t* plugin){auto* p=self(plugin); if(!p->guiView)return false; p->guiVisible.store(true); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GResonantTerrainView*>(p->guiView) startRefreshTimer]; requestGuiRedraw(*p); return true;} bool guiHide(const clap_plugin_t* plugin){auto* p=self(plugin); if(!p->guiView)return false; p->guiVisible.store(false); p->guiDirty.store(false); [static_cast<S3GResonantTerrainView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true;}
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor { CLAP_VERSION_INIT, "org.s3g.s3g-dsp.resonant-terrain-8ch", "s3g Resonant Terrain 8ch", "s3g", "https://github.com/s3g/s3g-dsp", "", "", "0.1.0", "8-channel resonant terrain instrument with topology-distributed resonators and internal/MIDI excitation.", features };

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin(); if(!p) return nullptr;
    p->host=host; p->plugin.desc=&descriptor; p->plugin.plugin_data=p; p->plugin.init=init; p->plugin.destroy=destroy; p->plugin.activate=activate; p->plugin.deactivate=deactivate; p->plugin.start_processing=startProcessing; p->plugin.stop_processing=stopProcessing; p->plugin.reset=reset; p->plugin.process=process; p->plugin.get_extension=pluginGetExtension; p->plugin.on_main_thread=onMainThread; return &p->plugin;
}
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index==0?&descriptor:nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; } void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)==0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
