#include "s3g_node_track_mixer.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
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
#include <new>
#include <type_traits>

namespace {

#if defined(S3G_AMBI_NODE_TRACK_MIXER)
constexpr bool kAmbi = true;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-node-bus-mixer";
constexpr const char* kPluginName = "s3g Ambi Node Bus Mixer 128";
constexpr const char* kPluginDesc = "128-channel ambisonic node/cursor bus mixer.";
constexpr const char* kPortName = "Ambi Node Bus Mix";
using Processor = s3g::AmbiNodeTrackMixer;
using Params = s3g::AmbiNodeTrackMixerParams;
#else
constexpr bool kAmbi = false;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.node-bus-mixer";
constexpr const char* kPluginName = "s3g Node Bus Mixer 128";
constexpr const char* kPluginDesc = "128-channel multichannel node/cursor bus mixer.";
constexpr const char* kPortName = "Node Bus Mix";
using Processor = s3g::NodeTrackMixer;
using Params = s3g::NodeTrackMixerParams;
#endif

constexpr uint32_t kGuiWidth = 920;
constexpr uint32_t kGuiHeight = 920;
constexpr uint32_t kStateVersion = 2;
constexpr clap_id kParamNodeBase = 1000;
constexpr uint32_t kParamNodeLimit = kAmbi ? s3g::kAmbiNodeBusMixerMaxNodes : s3g::kNodeTrackMixerMaxNodes;
constexpr clap_id kParamNodeStride = kAmbi ? 7 : 10;
constexpr clap_id kParamNodeRotateAzBase = 2000;
constexpr clap_id kParamNodeRotateElBase = 2100;

enum ParamId : clap_id {
    kParamLayoutOrOrder = 1,
    kParamOutputChannels = 2,
    kParamNodeCount = 3,
    kParamMixMode = 4,
    kParamCursorInfluence = 5,
    kParamCursorX = 6,
    kParamCursorY = 7,
    kParamCursorZ = 8,
    kParamStackPosition = 9,
    kParamCursorRadius = 10,
    kParamCursorFocus = 11,
    kParamCursorGate = 12,
    kParamOutputGain = 13,
    kParamLockZ = 14,
};

struct SavedState {
    uint32_t version = kStateVersion;
    Params params {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    Params params {};
    Processor processor {};
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kNodeTrackMixerMaxNodes> nodePeaks {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

uint32_t roundedUint(double value)
{
    return static_cast<uint32_t>(std::max(0.0, std::round(value)));
}

const char* mixModeName(uint32_t mode)
{
    (void)mode;
    return "SPATIAL";
}

void sanitizeAndSet(Plugin& p)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    p.params = s3g::sanitizeAmbiNodeTrackMixerParams(p.params);
#else
    p.params = s3g::sanitizeNodeTrackMixerParams(p.params);
#endif
    p.processor.setParams(p.params);
}

void initializeDefaultParams(Plugin& p)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    constexpr float kNodeDistance = 0.78f;
    p.params.cursorInfluence = 1.0f;
    p.params.cursorX = 0.0f;
    p.params.cursorY = 0.0f;
    p.params.cursorZ = 0.0f;
    p.params.lockZ = true;
    p.params.cursorRadius = 1.25f;
    p.params.cursorFocus = 1.0f;
    p.params.nodeCount = s3g::kAmbiNodeBusMixerMaxNodes;
    for (uint32_t i = 0; i < s3g::kAmbiNodeBusMixerMaxNodes; ++i) {
        auto& n = p.params.nodes[i];
        const float az = static_cast<float>(i) * 2.0f * s3g::kPi / static_cast<float>(s3g::kAmbiNodeBusMixerMaxNodes);
        n.inputStart = i * s3g::kAmbiNodeBusMixerChannelsPerNode + 1u;
        n.x = -std::sin(az) * kNodeDistance;
        n.y = std::cos(az) * kNodeDistance;
        n.z = 0.0f;
        n.radius = 0.65f;
        n.focus = 1.0f;
    }
#else
    (void)p;
#endif
}

void applyNodeParam(Plugin& p, uint32_t node, uint32_t field, double value)
{
    if (node >= kParamNodeLimit) return;
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    auto& n = p.params.nodes[node];
    switch (field) {
    case 0: n.active = value >= 0.5; break;
    case 1: n.levelDb = static_cast<float>(value); break;
    case 2: n.x = static_cast<float>(value); break;
    case 3: n.y = static_cast<float>(value); break;
    case 4: n.z = static_cast<float>(value); break;
    case 5: n.radius = static_cast<float>(value); break;
    case 6: n.focus = static_cast<float>(value); break;
    default: return;
    }
#else
    auto& n = p.params.nodes[node];
    switch (field) {
    case 0: n.active = value >= 0.5; break;
    case 1: n.levelDb = static_cast<float>(value); break;
    case 2: n.sourceLayout = s3g::nodeTrackRegularLayoutFromIndex(roundedUint(value)); break;
    case 3: n.sourceChannels = roundedUint(value); break;
    case 4: n.inputStart = roundedUint(value); break;
    case 5: n.x = static_cast<float>(value); break;
    case 6: n.y = static_cast<float>(value); break;
    case 7: n.z = static_cast<float>(value); break;
    case 8: n.scale = static_cast<float>(value); break;
    case 9: n.focus = static_cast<float>(value); break;
    default: return;
    }
#endif
    sanitizeAndSet(p);
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id >= kParamNodeBase && id < kParamNodeBase + kParamNodeLimit * kParamNodeStride) {
        const uint32_t rel = id - kParamNodeBase;
        applyNodeParam(p, rel / kParamNodeStride, rel % kParamNodeStride, value);
        return;
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    if (id >= kParamNodeRotateAzBase && id < kParamNodeRotateAzBase + s3g::kNodeTrackMixerMaxNodes) {
        p.params.nodes[id - kParamNodeRotateAzBase].rotateAzDeg = static_cast<float>(value);
        sanitizeAndSet(p);
        return;
    }
    if (id >= kParamNodeRotateElBase && id < kParamNodeRotateElBase + s3g::kNodeTrackMixerMaxNodes) {
        p.params.nodes[id - kParamNodeRotateElBase].rotateElDeg = static_cast<float>(value);
        sanitizeAndSet(p);
        return;
    }
#endif
    switch (id) {
    case kParamLayoutOrOrder:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        p.params.order = s3g::AmbiNodeTrackOrder::O3;
#else
        p.params.outputLayout = s3g::nodeTrackRegularLayoutFromIndex(roundedUint(value));
#endif
        break;
    case kParamOutputChannels:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        p.params.outputChannels = roundedUint(value);
#endif
        break;
    case kParamNodeCount: p.params.nodeCount = roundedUint(value); break;
    case kParamMixMode:
        p.params.mixMode = s3g::NodeTrackMixMode::SpatialObjects;
        break;
    case kParamCursorInfluence: p.params.cursorInfluence = static_cast<float>(value); break;
    case kParamCursorX: p.params.cursorX = static_cast<float>(value); break;
    case kParamCursorY: p.params.cursorY = static_cast<float>(value); break;
    case kParamCursorZ: p.params.cursorZ = static_cast<float>(value); break;
    case kParamStackPosition: p.params.stackPosition = static_cast<float>(value); break;
    case kParamCursorRadius: p.params.cursorRadius = static_cast<float>(value); break;
    case kParamCursorFocus: p.params.cursorFocus = static_cast<float>(value); break;
    case kParamCursorGate: p.params.cursorGate = static_cast<float>(value); break;
    case kParamOutputGain: p.params.outputGainDb = static_cast<float>(value); break;
    case kParamLockZ: p.params.lockZ = value >= 0.5; break;
    default: return;
    }
    sanitizeAndSet(p);
}

double nodeParamValue(const Plugin& p, uint32_t node, uint32_t field)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    const auto& n = p.params.nodes[node];
    switch (field) {
    case 0: return n.active ? 1.0 : 0.0;
    case 1: return n.levelDb;
    case 2: return n.x;
    case 3: return n.y;
    case 4: return n.z;
    case 5: return n.radius;
    case 6: return n.focus;
    default: return 0.0;
    }
#else
    const auto& n = p.params.nodes[node];
    switch (field) {
    case 0: return n.active ? 1.0 : 0.0;
    case 1: return n.levelDb;
    case 2: return s3g::nodeTrackRegularLayoutIndex(n.sourceLayout);
    case 3: return n.sourceChannels;
    case 4: return n.inputStart;
    case 5: return n.x;
    case 6: return n.y;
    case 7: return n.z;
    case 8: return n.scale;
    case 9: return n.focus;
    default: return 0.0;
    }
#endif
}

double getParam(const Plugin& p, clap_id id)
{
    if (id >= kParamNodeBase && id < kParamNodeBase + kParamNodeLimit * kParamNodeStride) {
        const uint32_t rel = id - kParamNodeBase;
        return nodeParamValue(p, rel / kParamNodeStride, rel % kParamNodeStride);
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    if (id >= kParamNodeRotateAzBase && id < kParamNodeRotateAzBase + s3g::kNodeTrackMixerMaxNodes) {
        return p.params.nodes[id - kParamNodeRotateAzBase].rotateAzDeg;
    }
    if (id >= kParamNodeRotateElBase && id < kParamNodeRotateElBase + s3g::kNodeTrackMixerMaxNodes) {
        return p.params.nodes[id - kParamNodeRotateElBase].rotateElDeg;
    }
#endif
    switch (id) {
    case kParamLayoutOrOrder:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        return static_cast<uint32_t>(p.params.order);
#else
        return s3g::nodeTrackRegularLayoutIndex(p.params.outputLayout);
#endif
    case kParamOutputChannels:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        return 128.0;
#else
        return p.params.outputChannels;
#endif
    case kParamNodeCount: return p.params.nodeCount;
    case kParamMixMode: return static_cast<uint32_t>(p.params.mixMode);
    case kParamCursorInfluence: return p.params.cursorInfluence;
    case kParamCursorX: return p.params.cursorX;
    case kParamCursorY: return p.params.cursorY;
    case kParamCursorZ: return p.params.cursorZ;
    case kParamStackPosition: return p.params.stackPosition;
    case kParamCursorRadius: return p.params.cursorRadius;
    case kParamCursorFocus: return p.params.cursorFocus;
    case kParamCursorGate: return p.params.cursorGate;
    case kParamOutputGain: return p.params.outputGainDb;
    case kParamLockZ: return p.params.lockZ ? 1.0 : 0.0;
    default: return 0.0;
    }
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            applyParam(p, param->param_id, param->value);
        }
    }
}

bool init(const clap_plugin_t*) { return true; }
void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p && p->guiView) {
        NSView* v = static_cast<NSView*>(p->guiView);
        [v removeFromSuperview];
        [v release];
        p->guiView = nullptr;
    }
#endif
    delete self(plugin);
}
bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->processor.prepare(sampleRate);
    sanitizeAndSet(*p);
    p->processor.reset();
    return true;
}
void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->processor.reset();
    p->outputPeak.store(0.0f);
    for (auto& peak : p->nodePeaks) peak.store(0.0f);
}

template <typename Sample>
float peakFor(Sample* const* out, uint32_t channels, uint32_t frames)
{
    float peak = 0.0f;
    if (!out) return peak;
    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (!out[ch]) continue;
        for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, std::abs(static_cast<float>(out[ch][i])));
    }
    return peak;
}

template <typename Sample>
void updateNodePeaks(Plugin& p, Sample** in, uint32_t inputChannels, uint32_t frames)
{
    const auto weights = p.processor.nodeWeights();
    for (uint32_t node = 0; node < s3g::kNodeTrackMixerMaxNodes; ++node) {
        float peak = 0.0f;
        if (in && node < p.params.nodeCount) {
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
            const uint32_t srcCh = s3g::kAmbiNodeBusMixerChannelsPerNode;
            const uint32_t inputStart = node * s3g::kAmbiNodeBusMixerChannelsPerNode;
#else
            const auto& n = p.params.nodes[node];
            const uint32_t srcCh = std::min<uint32_t>(n.sourceChannels, s3g::kNodeTrackMixerMaxChannels);
            const uint32_t inputStart = n.inputStart - 1u;
#endif
            for (uint32_t ch = 0; ch < srcCh; ++ch) {
                const uint32_t inCh = inputStart + ch;
                if (inCh >= inputChannels || !in[inCh]) continue;
                for (uint32_t frame = 0; frame < frames; ++frame) {
                    peak = std::max(peak, std::abs(static_cast<float>(in[inCh][frame])));
                }
            }
            peak *= std::abs(weights[node]);
        }
        const float decayed = p.nodePeaks[node].load(std::memory_order_relaxed) * 0.90f;
        p.nodePeaks[node].store(std::max(decayed, peak), std::memory_order_relaxed);
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& p, const clap_audio_buffer_t& input, const clap_audio_buffer_t& output, uint32_t frames, Sample** in, Sample** out)
{
    s3g::clearAudioBuffer(output, frames);
    p.processor.process(in, input.channel_count, out, output.channel_count, frames);
    s3g::clearAudioBufferFromChannel(output, s3g::kNodeTrackMixerMaxChannels, frames);
    const uint32_t outCount = std::min<uint32_t>(output.channel_count, s3g::kNodeTrackMixerMaxChannels);
    p.outputPeak.store(std::max(p.outputPeak.load(std::memory_order_relaxed) * 0.90f, peakFor(out, outCount, frames)), std::memory_order_relaxed);
    updateNodePeaks(p, in, input.channel_count, frames);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_inputs_count == 0 || proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    const auto& input = proc->audio_inputs[0];
    const auto& output = proc->audio_outputs[0];
    if (input.data32 && output.data32) return processTyped<float>(*p, input, output, proc->frames_count, input.data32, output.data32);
    if (input.data64 && output.data64) return processTyped<double>(*p, input, output, proc->frames_count, input.data64, output.data64);
    s3g::clearAudioBuffer(output, proc->frames_count);
    return CLAP_PROCESS_CONTINUE;
}
void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1; }
bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0 || !info) return false;
    info->id = isInput ? 10 : 20;
    std::strncpy(info->name, isInput ? "Node In" : kPortName, sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = s3g::kNodeTrackMixerMaxChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*)
{
    return (kAmbi ? 9u : 12u) + kParamNodeLimit * kParamNodeStride + (kAmbi ? 0u : s3g::kNodeTrackMixerMaxNodes * 2u);
}

void fillInfo(clap_param_info_t* info, clap_id id, const char* name, double min, double max, double def)
{
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, name, sizeof(info->name));
    std::strncpy(info->module, kAmbi ? "Ambi Node Bus Mixer" : "Node Bus Mixer", sizeof(info->module));
    info->min_value = min;
    info->max_value = max;
    info->default_value = def;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    switch (index) {
    case 0: fillInfo(info, kParamNodeCount, "Node count", 1, s3g::kAmbiNodeBusMixerMaxNodes, s3g::kAmbiNodeBusMixerMaxNodes); return true;
    case 1: fillInfo(info, kParamCursorInfluence, "Cursor influence", 0, 1, 1); return true;
    case 2: fillInfo(info, kParamCursorX, "Cursor X", -2, 2, 0); return true;
    case 3: fillInfo(info, kParamCursorY, "Cursor Y", -2, 2, 0); return true;
    case 4: fillInfo(info, kParamCursorZ, "Cursor Z", -2, 2, 0); return true;
    case 5: fillInfo(info, kParamCursorRadius, "Cursor radius", 0.05, 8, 1.25); info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE; return true;
    case 6: fillInfo(info, kParamCursorFocus, "Cursor focus", 0.5, 2, 1); info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE; return true;
    case 7: fillInfo(info, kParamLockZ, "Lock Z plane", 0, 1, 1); info->flags |= CLAP_PARAM_IS_STEPPED; info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE; return true;
    case 8: fillInfo(info, kParamOutputGain, "Output gain", -60, 12, 0); return true;
    default: break;
    }
    index -= 9u;
#else
    switch (index) {
    case 0: fillInfo(info, kParamLayoutOrOrder, "Mix bed shape", 0, s3g::kNodeTrackRegularLayoutCount - 1u, 5); info->flags |= CLAP_PARAM_IS_STEPPED; return true;
    case 1: fillInfo(info, kParamOutputChannels, "Output channels", 2, 128, 8); info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE; return true;
    case 2: fillInfo(info, kParamNodeCount, "Node count", 1, 16, 4); return true;
    case 3: fillInfo(info, kParamCursorInfluence, "Cursor influence", 0, 1, 1); return true;
    case 4: fillInfo(info, kParamCursorX, "Cursor X", -2, 2, 0); return true;
    case 5: fillInfo(info, kParamCursorY, "Cursor Y", -2, 2, 0); return true;
    case 6: fillInfo(info, kParamCursorZ, "Cursor Z", -2, 2, 0); return true;
    case 7: fillInfo(info, kParamCursorRadius, "Cursor radius", 0.05, 8, 1); info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE; return true;
    case 8: fillInfo(info, kParamCursorFocus, "Cursor focus", 0.5, 2, 1); info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE; return true;
    case 9: fillInfo(info, kParamCursorGate, "Cursor gate", 0, 0.95, 0.02); return true;
    case 10: fillInfo(info, kParamLockZ, "Lock Z plane", 0, 1, 1); info->flags |= CLAP_PARAM_IS_STEPPED; info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE; return true;
    case 11: fillInfo(info, kParamOutputGain, "Output gain", -60, 12, 0); return true;
    default: break;
    }
    index -= 12u;
#endif
    if (index < kParamNodeLimit * kParamNodeStride) {
        const uint32_t node = index / kParamNodeStride;
        const uint32_t field = index % kParamNodeStride;
        char name[64] {};
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        static constexpr const char* names[] { "active", "level", "X", "Y", "Z", "radius", "focus" };
        const double mins[] { 0, -60, -2, -2, -2, 0.05, 0.5 };
        const double maxs[] { 1, 12, 2, 2, 2, 8, 4 };
        const double defs[] { 1, 0, 0, 0, 0, 0.65, 1 };
#else
        static constexpr const char* names[] { "active", "level", "source format", "source channels", "bus start", "X", "Y", "Z", "shape scale", "focus" };
        const double mins[] { 0, -60, 0, 1, 1, -2, -2, -2, 0.05, 0.5 };
        const double maxs[] { 1, 12, s3g::kNodeTrackRegularLayoutCount - 1u, 128, 128, 2, 2, 2, 4, 4 };
        const double defs[] { 1, 0, 5, 8, 1, 0, 0, 0, 1, 1 };
#endif
        std::snprintf(name, sizeof(name), "Node %02u %s", node + 1u, names[field]);
        fillInfo(info, kParamNodeBase + node * kParamNodeStride + field, name, mins[field], maxs[field], defs[field]);
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        if (field == 2u) info->flags |= CLAP_PARAM_IS_STEPPED;
        if (field == 3u || field == 4u) info->flags &= ~CLAP_PARAM_IS_AUTOMATABLE;
#endif
        if (field == 0u) info->flags |= CLAP_PARAM_IS_STEPPED;
        return true;
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    index -= s3g::kNodeTrackMixerMaxNodes * kParamNodeStride;
    if (index < s3g::kNodeTrackMixerMaxNodes) {
        char name[64] {};
        std::snprintf(name, sizeof(name), "Node %02u azimuth rotate", index + 1u);
        fillInfo(info, kParamNodeRotateAzBase + index, name, -180, 180, 0);
        info->flags |= CLAP_PARAM_IS_STEPPED;
        return true;
    }
    index -= s3g::kNodeTrackMixerMaxNodes;
    if (index < s3g::kNodeTrackMixerMaxNodes) {
        char name[64] {};
        std::snprintf(name, sizeof(name), "Node %02u elevation rotate", index + 1u);
        fillInfo(info, kParamNodeRotateElBase + index, name, -90, 90, 0);
        info->flags |= CLAP_PARAM_IS_STEPPED;
        return true;
    }
#endif
    return false;
}

bool isParamId(clap_id id)
{
    if (id >= kParamNodeBase && id < kParamNodeBase + kParamNodeLimit * kParamNodeStride) return true;
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    if (id >= kParamNodeRotateAzBase && id < kParamNodeRotateAzBase + s3g::kNodeTrackMixerMaxNodes) return true;
    if (id >= kParamNodeRotateElBase && id < kParamNodeRotateElBase + s3g::kNodeTrackMixerMaxNodes) return true;
    switch (id) {
    case kParamLayoutOrOrder:
    case kParamOutputChannels:
    case kParamNodeCount:
    case kParamCursorInfluence:
    case kParamCursorX:
    case kParamCursorY:
    case kParamCursorZ:
    case kParamCursorRadius:
    case kParamCursorFocus:
    case kParamCursorGate:
    case kParamOutputGain:
    case kParamLockZ:
        return true;
    default:
        return false;
    }
#endif
    switch (id) {
    case kParamNodeCount:
    case kParamCursorInfluence:
    case kParamCursorX:
    case kParamCursorY:
    case kParamCursorZ:
    case kParamCursorRadius:
    case kParamCursorFocus:
    case kParamOutputGain:
    case kParamLockZ:
        return true;
    default:
        return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id paramId, double* value)
{
    if (!value || !isParamId(paramId)) return false;
    *value = getParam(*self(plugin), paramId);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id paramId, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    if (paramId >= kParamNodeRotateAzBase && paramId < kParamNodeRotateAzBase + s3g::kNodeTrackMixerMaxNodes) {
        std::snprintf(display, size, "%+d deg", static_cast<int>(std::round(value)));
        return true;
    }
    if (paramId >= kParamNodeRotateElBase && paramId < kParamNodeRotateElBase + s3g::kNodeTrackMixerMaxNodes) {
        std::snprintf(display, size, "%+d deg", static_cast<int>(std::round(value)));
        return true;
    }
#endif
    if (paramId == kParamLayoutOrOrder) {
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        std::snprintf(display, size, "%uOA", roundedUint(value) + 1u);
#else
        std::snprintf(display, size, "%s", s3g::nodeTrackLayoutName(s3g::nodeTrackRegularLayoutFromIndex(roundedUint(value))));
#endif
        return true;
    }
    if (paramId == kParamMixMode) {
        std::snprintf(display, size, "%s", mixModeName(roundedUint(value)));
        return true;
    }
    if (paramId == kParamLockZ) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
        return true;
    }
    if (paramId >= kParamNodeBase) {
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        if (paramId < kParamNodeBase + kParamNodeLimit * kParamNodeStride) {
            const uint32_t field = (paramId - kParamNodeBase) % kParamNodeStride;
            if (field == 2u) {
                std::snprintf(display, size, "%s", s3g::nodeTrackLayoutName(s3g::nodeTrackRegularLayoutFromIndex(roundedUint(value))));
                return true;
            }
        }
#endif
        std::snprintf(display, size, "%.3g", value);
        return true;
    }
    std::snprintf(display, size, "%.3g", value);
    return true;
}
bool paramsTextToValue(const clap_plugin_t*, clap_id paramId, const char* display, double* value)
{
    if (!display || !value || !isParamId(paramId)) return false;
    *value = std::atof(display);
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) { readParamEvents(*self(plugin), in); }
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    const SavedState state { kStateVersion, self(plugin)->params };
    return stream->write(stream, &state, sizeof(state)) == static_cast<int64_t>(sizeof(state));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (stream->read(stream, &state, sizeof(state)) != static_cast<int64_t>(sizeof(state)) || state.version != kStateVersion) return false;
    auto* p = self(plugin);
    p->params = state.params;
    sanitizeAndSet(*p);
    return true;
}
const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {

NSString* ns(const char* text) { return [NSString stringWithUTF8String:text]; }

NSString* const* regularLayoutMenuItems()
{
    static NSString* items[] = {
        @"STEREO", @"QUAD", @"5.0", @"6.0", @"7.0",
        @"OCTO", @"CUBE", @"5.0.2", @"7.0.2", @"5.0.4",
        @"7.0.4", @"RING12", @"RING16", @"DBL16", @"DBL24",
    };
    return items;
}

NSString* orderOrLayoutText(const Plugin& p)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    return [NSString stringWithFormat:@"%uOA", static_cast<uint32_t>(p.params.order) + 1u];
#else
    return ns(s3g::nodeTrackLayoutName(p.params.outputLayout));
#endif
}

NSString* busModeText(const Plugin& p)
{
    return ns(mixModeName(static_cast<uint32_t>(p.params.mixMode)));
}

float nodeX(const Plugin& p, uint32_t node) { return p.params.nodes[node].x; }
float nodeY(const Plugin& p, uint32_t node) { return p.params.nodes[node].y; }
float nodeZ(const Plugin& p, uint32_t node) { return p.params.nodes[node].z; }
bool nodeActive(const Plugin& p, uint32_t node) { return p.params.nodes[node].active; }
float nodeLevelDb(const Plugin& p, uint32_t node) { return p.params.nodes[node].levelDb; }
uint32_t nodeInputStart(const Plugin& p, uint32_t node) { return p.params.nodes[node].inputStart; }
float nodeFocus(const Plugin& p, uint32_t node) { return p.params.nodes[node].focus; }
float nodeRotateAz(const Plugin& p, uint32_t node)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    (void)p;
    (void)node;
    return 0.0f;
#else
    return p.params.nodes[node].rotateAzDeg;
#endif
}

float nodeRotateEl(const Plugin& p, uint32_t node)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    (void)p;
    (void)node;
    return 0.0f;
#else
    return p.params.nodes[node].rotateElDeg;
#endif
}

float nodeSizeOrRadius(const Plugin& p, uint32_t node)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    return p.params.nodes[node].radius;
#else
    return p.params.nodes[node].scale;
#endif
}
bool zLocked(const Plugin& p) { return p.params.lockZ; }

} // namespace

@interface S3GNodeBusMixerView : NSView {
    void* _plugin;
    int _selectedNode;
    int _dragSlider;
    BOOL _dragNode;
    BOOL _dragCursor;
    BOOL _dragView;
    int _viewMode;
    CGFloat _viewYaw;
    CGFloat _viewPitch;
    CGFloat _viewZoom;
    NSPoint _lastDragPoint;
    int _openMenu;
    int _hoverMenuIndex;
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)drawMenuRow:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)drawReadoutRow:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)drawCheckRow:(NSString*)name checked:(BOOL)checked y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (NSRect)fieldRect;
- (NSRect)menuRect:(int)menu;
- (CGFloat)viewScaleForRect:(NSRect)rect;
- (NSPoint)projectX:(float)x y:(float)y z:(float)z rect:(NSRect)rect;
- (void)drawViewButton:(NSString*)label rect:(NSRect)rect active:(BOOL)active attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)drawWeightSlider:(NSString*)label weight:(float)weight rect:(NSRect)rect attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (void)drawPeakMeter:(NSString*)label peak:(float)peak rect:(NSRect)rect attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style;
- (int)viewButtonHit:(NSPoint)pt;
- (void)resetSlider:(int)index;
- (void)updateSliderAtPoint:(NSPoint)pt;
- (void)updateSpatialAtPoint:(NSPoint)pt cursor:(BOOL)cursor;
@end

@implementation S3GNodeBusMixerView
- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _selectedNode = 0;
        _dragSlider = -1;
        _dragNode = NO;
        _dragCursor = NO;
        _dragView = NO;
        _viewMode = 2;
        _viewYaw = -35.0;
        _viewPitch = 30.0;
        _viewZoom = 1.0;
        _lastDragPoint = NSZeroPoint;
        _openMenu = -1;
        _hoverMenuIndex = -1;
        _timer = nil;
    }
    return self;
}
- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (void)viewDidMoveToWindow { [[self window] setAcceptsMouseMovedEvents:YES]; }
- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:(1.0 / 24.0) target:self selector:@selector(refreshTimerFired:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}
- (void)stopRefreshTimer { if (_timer) { [_timer invalidate]; _timer = nil; } }
- (void)refreshTimerFired:(NSTimer*)timer { (void)timer; if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES]; }
- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*static_cast<Plugin*>(_plugin), param, value);
    _selectedNode = std::min<int>(_selectedNode, static_cast<int>(static_cast<Plugin*>(_plugin)->params.nodeCount) - 1);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawSlider(name, value, norm, y, attrs, attrs, style, 610, 714, 856, 128);
}
- (void)drawMenuRow:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, attrs, style, 610, 714, 170);
}
- (void)drawReadoutRow:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    (void)style;
    [name drawAtPoint:NSMakePoint(610, y - 4.0) withAttributes:attrs];
    [value drawAtPoint:NSMakePoint(714, y - 4.0) withAttributes:attrs];
}
- (void)drawCheckRow:(NSString*)name checked:(BOOL)checked y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    [name drawAtPoint:NSMakePoint(610, y - 4.0) withAttributes:attrs];
    NSRect box = NSMakeRect(714, y - 7.0, 13.0, 13.0);
    [style.strip setFill];
    NSRectFill(box);
    [style.grid setStroke];
    NSFrameRect(box);
    if (checked) {
        [style.text setFill];
        NSRectFill(NSInsetRect(box, 3.0, 3.0));
    }
    [(checked ? @"ON" : @"OFF") drawAtPoint:NSMakePoint(736, y - 4.0) withAttributes:attrs];
}
- (NSRect)fieldRect { return NSMakeRect(28, 70, 536, 536); }
- (NSRect)menuRect:(int)menu
{
    const CGFloat rows = kAmbi ? 3.0 : static_cast<CGFloat>(s3g::kNodeTrackRegularLayoutCount);
    if (menu == 1) return NSMakeRect(714, 81, 170, 21.0 * rows);
    return NSMakeRect(714, 395, 170, 21.0 * rows);
}
- (NSPoint)projectX:(float)x y:(float)y z:(float)z rect:(NSRect)rect
{
    const CGFloat scale = [self viewScaleForRect:rect];
    const CGFloat cx = rect.origin.x + rect.size.width * 0.5;
    const CGFloat cy = rect.origin.y + rect.size.height * 0.52;
    if (_viewMode == 0) {
        return NSMakePoint(cx - static_cast<CGFloat>(y) * scale, cy - static_cast<CGFloat>(x) * scale);
    }
    if (_viewMode == 1) {
        return NSMakePoint(cx - static_cast<CGFloat>(y) * scale, cy - static_cast<CGFloat>(z) * scale);
    }
    const CGFloat yaw = _viewYaw * static_cast<CGFloat>(s3g::kPi / 180.0);
    const CGFloat pitch = _viewPitch * static_cast<CGFloat>(s3g::kPi / 180.0);
    const CGFloat xr = static_cast<CGFloat>(x) * std::cos(yaw) - static_cast<CGFloat>(y) * std::sin(yaw);
    const CGFloat yr = static_cast<CGFloat>(x) * std::sin(yaw) + static_cast<CGFloat>(y) * std::cos(yaw);
    const CGFloat v = static_cast<CGFloat>(z) * std::cos(pitch) - xr * std::sin(pitch);
    return NSMakePoint(cx - yr * scale, cy - v * scale);
}
- (CGFloat)viewScaleForRect:(NSRect)rect
{
    return std::min(rect.size.width, rect.size.height) * 0.31 * _viewZoom;
}
- (void)drawViewButton:(NSString*)label rect:(NSRect)rect active:(BOOL)active attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    [(active ? style.text : s3g::clap_gui::color(0x1d1d1d)) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);
    NSDictionary* buttonAttrs = @{ NSForegroundColorAttributeName:(active ? style.bg : style.dim), NSFontAttributeName:attrs[NSFontAttributeName] };
    NSSize size = [label sizeWithAttributes:buttonAttrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                                   rect.origin.y + (rect.size.height - size.height) * 0.5 - 1.0)
        withAttributes:buttonAttrs];
}
- (void)drawWeightSlider:(NSString*)label weight:(float)weight rect:(NSRect)rect attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    const CGFloat norm = std::clamp<CGFloat>(weight, 0.0, 1.0);
    [label drawAtPoint:NSMakePoint(rect.origin.x, rect.origin.y - 1.0) withAttributes:attrs];
    const CGFloat barX = rect.origin.x + 34.0;
    const CGFloat barW = rect.size.width - 78.0;
    [style.strip setFill]; NSRectFill(NSMakeRect(barX, rect.origin.y + 2.0, barW, 8.0));
    [style.text setFill]; NSRectFill(NSMakeRect(barX + 1.0, rect.origin.y + 3.0, std::max<CGFloat>(0.0, (barW - 2.0) * norm), 6.0));
    [style.grid setStroke]; NSFrameRect(NSMakeRect(barX, rect.origin.y + 2.0, barW, 8.0));
    [[NSString stringWithFormat:@"%3.0f%%", norm * 100.0] drawAtPoint:NSMakePoint(NSMaxX(rect) - 38.0, rect.origin.y - 1.0) withAttributes:attrs];
}
- (void)drawPeakMeter:(NSString*)label peak:(float)peak rect:(NSRect)rect attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    const double db = 20.0 * std::log10(std::max(0.000001f, peak));
    const CGFloat norm = std::clamp<CGFloat>((db + 60.0) / 60.0, 0.0, 1.0);
    [label drawAtPoint:NSMakePoint(rect.origin.x, rect.origin.y - 1.0) withAttributes:attrs];
    const CGFloat barX = rect.origin.x + 34.0;
    const CGFloat barW = rect.size.width - 70.0;
    [style.strip setFill]; NSRectFill(NSMakeRect(barX, rect.origin.y + 1.0, barW, 10.0));
    [style.fill setFill]; NSRectFill(NSMakeRect(barX + 1.0, rect.origin.y + 2.0, std::max<CGFloat>(0.0, (barW - 2.0) * norm), 8.0));
    [style.grid setStroke]; NSFrameRect(NSMakeRect(barX, rect.origin.y + 1.0, barW, 10.0));
    [[NSString stringWithFormat:@"%+3.0f", db] drawAtPoint:NSMakePoint(NSMaxX(rect) - 30.0, rect.origin.y - 1.0) withAttributes:attrs];
}
- (int)viewButtonHit:(NSPoint)pt
{
    NSRect buttons[] = {
        NSMakeRect(372, 39, 44, 18),
        NSMakeRect(418, 39, 44, 18),
        NSMakeRect(464, 39, 52, 18),
        NSMakeRect(522, 39, 20, 18),
        NSMakeRect(546, 39, 20, 18),
    };
    for (int i = 0; i < 5; ++i) {
        if (NSPointInRect(pt, buttons[i])) return i;
    }
    return -1;
}
- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSFont* mono = [NSFont fontWithName:@"Menlo" size:10.0] ?: [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
    NSDictionary* small = @{ NSForegroundColorAttributeName:style.dim, NSFontAttributeName:mono };
    NSDictionary* text = @{ NSForegroundColorAttributeName:style.text, NSFontAttributeName:mono };

    [ns(kPluginName) drawAtPoint:NSMakePoint(18, 13) withAttributes:text];
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    [[NSString stringWithFormat:@"%u NODES / 3OA / 128CH", p->params.nodeCount] drawAtPoint:NSMakePoint(684, 13) withAttributes:small];
#else
    [[NSString stringWithFormat:@"%u NODES / %u OUT / 128CH", p->params.nodeCount, p->params.outputChannels] drawAtPoint:NSMakePoint(660, 13) withAttributes:small];
#endif

    NSRect fieldPanel = NSMakeRect(12, 34, 570, 872);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"NODE FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, 21, text, style);
    [self drawViewButton:@"TOP" rect:NSMakeRect(372, 39, 44, 18) active:(_viewMode == 0) attrs:small style:style];
    [self drawViewButton:@"SIDE" rect:NSMakeRect(418, 39, 44, 18) active:(_viewMode == 1) attrs:small style:style];
    [self drawViewButton:@"3/4" rect:NSMakeRect(464, 39, 52, 18) active:(_viewMode == 2) attrs:small style:style];
    [self drawViewButton:@"-" rect:NSMakeRect(522, 39, 20, 18) active:NO attrs:small style:style];
    [self drawViewButton:@"+" rect:NSMakeRect(546, 39, 20, 18) active:NO attrs:small style:style];
    NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);

    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.52;
    [s3g::clap_gui::color(0x777777, 0.22) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 22, cy) toPoint:NSMakePoint(NSMaxX(field) - 22, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, field.origin.y + 22) toPoint:NSMakePoint(cx, NSMaxY(field) - 22)];

    const NSPoint cursorPt = [self projectX:p->params.cursorX y:p->params.cursorY z:p->params.cursorZ rect:field];
    const auto weights = p->processor.nodeWeights();

    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:field] addClip];
    const CGFloat fieldScale = [self viewScaleForRect:field];
    for (uint32_t node = 0; node < p->params.nodeCount; ++node) {
        if (!nodeActive(*p, node)) continue;
        const NSPoint pt = [self projectX:nodeX(*p, node) y:nodeY(*p, node) z:nodeZ(*p, node) rect:field];
        const BOOL selected = static_cast<int>(node) == _selectedNode;
        const CGFloat active = std::clamp<CGFloat>(weights[node], 0.0, 1.0);
        const float dx = nodeX(*p, node) - p->params.cursorX;
        const float dy = nodeY(*p, node) - p->params.cursorY;
        const float dz = nodeZ(*p, node) - p->params.cursorZ;
        const BOOL cursorOverlap = std::sqrt(dx * dx + dy * dy + dz * dz) <= nodeSizeOrRadius(*p, node);
        const CGFloat radius = std::max<CGFloat>(6.0, nodeSizeOrRadius(*p, node) * fieldScale);
        if (cursorOverlap) {
            [[NSColor colorWithCalibratedRed:0.08
                                       green:1.00
                                        blue:0.26
                                       alpha:selected ? 0.46 : (0.14 + active * 0.36)] setStroke];
        } else {
            [s3g::clap_gui::color(selected ? 0xd8d8d8 : 0x8f8f8f, selected ? 0.34 : (0.10 + active * 0.18)) setStroke];
        }
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(pt.x - radius, pt.y - radius, radius * 2.0, radius * 2.0)];
        [ring setLineWidth:selected ? 1.2 : 0.75];
        [ring stroke];
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    for (uint32_t node = 0; node < p->params.nodeCount; ++node) {
        if (!nodeActive(*p, node)) continue;
        const auto& n = p->params.nodes[node];
        const BOOL selected = static_cast<int>(node) == _selectedNode;
        const uint32_t count = std::min<uint32_t>(std::max<uint32_t>(1u, n.sourceChannels), 32u);
        std::array<NSPoint, 32> pts {};
        for (uint32_t ch = 0; ch < count; ++ch) {
            auto rel = s3g::nodeTrackLayoutPoint(ch, count, n.sourceLayout);
            rel = s3g::nodeTrackRotatePoint(rel, n.rotateAzDeg, n.rotateElDeg);
            pts[ch] = [self projectX:n.x + rel.x * n.scale
                                    y:n.y + rel.y * n.scale
                                    z:n.z + rel.z * n.scale
                                 rect:field];
        }
        [s3g::clap_gui::color(selected ? 0xd8d8d8 : 0x9a9a9a, selected ? 0.44 : 0.18) setStroke];
        auto strokeEdge = [&](uint32_t a, uint32_t b) {
            if (a < count && b < count) [NSBezierPath strokeLineFromPoint:pts[a] toPoint:pts[b]];
        };
        auto fillTri = [&](uint32_t a, uint32_t b, uint32_t c) {
            if (a >= count || b >= count || c >= count) return;
            NSBezierPath* face = [NSBezierPath bezierPath];
            [face moveToPoint:pts[a]];
            [face lineToPoint:pts[b]];
            [face lineToPoint:pts[c]];
            [face closePath];
            [s3g::clap_gui::color(selected ? 0xbfbfbf : 0x8a8a8a, selected ? 0.055 : 0.030) setFill];
            [face fill];
        };
        auto fillQuad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
            if (a >= count || b >= count || c >= count || d >= count) return;
            NSBezierPath* face = [NSBezierPath bezierPath];
            [face moveToPoint:pts[a]];
            [face lineToPoint:pts[b]];
            [face lineToPoint:pts[c]];
            [face lineToPoint:pts[d]];
            [face closePath];
            [s3g::clap_gui::color(selected ? 0xbfbfbf : 0x8a8a8a, selected ? 0.050 : 0.026) setFill];
            [face fill];
        };
        [s3g::clap_gui::color(selected ? 0xd8d8d8 : 0x9a9a9a, selected ? 0.44 : 0.18) setStroke];
        if (n.sourceLayout == s3g::NodeTrackLayout::Cube && count >= 8u) {
            fillQuad(0, 1, 3, 2);
            fillQuad(4, 5, 7, 6);
            static constexpr uint32_t edges[][2] {
                {0, 1}, {1, 3}, {3, 2}, {2, 0},
                {4, 5}, {5, 7}, {7, 6}, {6, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7},
            };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::Stereo && count >= 2u) {
            strokeEdge(0, 1);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::Quad && count >= 4u) {
            fillQuad(0, 1, 2, 3);
            static constexpr uint32_t edges[][2] { {0, 1}, {1, 2}, {2, 3}, {3, 0} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::FiveZero && count >= 5u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 4, 1);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {1, 4}, {4, 3}, {3, 0}, {0, 1}, {3, 4} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SixZero && count >= 6u) {
            fillQuad(0, 1, 2, 5);
            fillQuad(5, 2, 3, 4);
            static constexpr uint32_t edges[][2] { {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}, {2, 5} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SevenZero && count >= 7u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 5, 6);
            fillQuad(1, 4, 6, 5);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {0, 3}, {3, 5}, {5, 6}, {6, 4}, {4, 1}, {3, 4} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::FiveZeroTwo && count >= 7u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 4, 1);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {1, 4}, {4, 3}, {3, 0}, {5, 6}, {0, 5}, {1, 6}, {2, 5}, {2, 6} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SevenZeroTwo && count >= 9u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 5, 6);
            fillQuad(1, 4, 6, 5);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {0, 3}, {3, 5}, {5, 6}, {6, 4}, {4, 1}, {7, 8}, {0, 7}, {1, 8}, {2, 7}, {2, 8} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::FiveZeroFour && count >= 9u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 4, 1);
            fillQuad(5, 6, 7, 8);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {1, 4}, {4, 3}, {3, 0}, {5, 6}, {6, 7}, {7, 8}, {8, 5}, {0, 5}, {1, 6}, {4, 7}, {3, 8} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if (n.sourceLayout == s3g::NodeTrackLayout::SevenZeroFour && count >= 11u) {
            fillTri(0, 2, 1);
            fillQuad(0, 3, 5, 6);
            fillQuad(1, 4, 6, 5);
            fillQuad(7, 8, 9, 10);
            static constexpr uint32_t edges[][2] { {0, 2}, {2, 1}, {0, 3}, {3, 5}, {5, 6}, {6, 4}, {4, 1}, {7, 8}, {8, 9}, {9, 10}, {10, 7}, {0, 7}, {1, 8}, {6, 9}, {5, 10} };
            for (const auto& edge : edges) strokeEdge(edge[0], edge[1]);
        } else if ((n.sourceLayout == s3g::NodeTrackLayout::DoubleRing16 || n.sourceLayout == s3g::NodeTrackLayout::DoubleRing24) && count >= 4u) {
            const uint32_t half = std::max<uint32_t>(1u, count / 2u);
            for (uint32_t i = 0; i < half; ++i) strokeEdge(i, (i + 1u) % half);
            for (uint32_t i = half; i < count; ++i) strokeEdge(i, half + ((i + 1u - half) % std::max<uint32_t>(1u, count - half)));
            for (uint32_t i = 0; i < std::min<uint32_t>(half, count - half); ++i) strokeEdge(i, half + i);
        } else if (count > 1u) {
            for (uint32_t i = 0; i < count; ++i) strokeEdge(i, (i + 1u) % count);
        }
        for (uint32_t ch = 0; ch < count; ++ch) {
            const CGFloat s = selected ? 4.0 : 2.8;
            [[NSBezierPath bezierPathWithRect:NSMakeRect(pts[ch].x - s * 0.5, pts[ch].y - s * 0.5, s, s)] stroke];
        }
    }
#endif
    [NSGraphicsContext restoreGraphicsState];

    [[NSColor colorWithCalibratedRed:0.08 green:1.00 blue:0.26 alpha:0.88] setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(cursorPt.x - 6.5, cursorPt.y - 6.5, 13, 13)] fill];
    [s3g::clap_gui::color(0x071008, 0.92) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cursorPt.x - 10, cursorPt.y) toPoint:NSMakePoint(cursorPt.x + 10, cursorPt.y)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cursorPt.x, cursorPt.y - 10) toPoint:NSMakePoint(cursorPt.x, cursorPt.y + 10)];
    [@"CUR" drawAtPoint:NSMakePoint(cursorPt.x + 10, cursorPt.y - 7) withAttributes:small];

    for (uint32_t node = 0; node < p->params.nodeCount; ++node) {
        const NSPoint pt = [self projectX:nodeX(*p, node) y:nodeY(*p, node) z:nodeZ(*p, node) rect:field];
        const BOOL selected = static_cast<int>(node) == _selectedNode;
        const BOOL active = nodeActive(*p, node);
        const CGFloat w = std::clamp<CGFloat>(weights[node], 0.0, 1.0);
        const float dx = nodeX(*p, node) - p->params.cursorX;
        const float dy = nodeY(*p, node) - p->params.cursorY;
        const float dz = nodeZ(*p, node) - p->params.cursorZ;
        const BOOL cursorOverlap = std::sqrt(dx * dx + dy * dy + dz * dz) <= nodeSizeOrRadius(*p, node);
        if (active && cursorOverlap) {
            const CGFloat gray = 0.30 + w * 0.22;
            const CGFloat greenMix = std::clamp<CGFloat>(w, 0.0, 1.0);
            [[NSColor colorWithCalibratedRed:gray * (1.0 - greenMix) + 0.08 * greenMix
                                       green:gray * (1.0 - greenMix) + 1.00 * greenMix
                                        blue:gray * (1.0 - greenMix) + 0.26 * greenMix
                                       alpha:1.0] setFill];
        } else {
            [[NSColor colorWithCalibratedWhite:active ? (0.34 + w * 0.56) : 0.18 alpha:1.0] setFill];
        }
        const CGFloat size = selected ? 17.0 : 12.0;
        NSRect box = NSMakeRect(pt.x - size * 0.5, pt.y - size * 0.5, size, size);
        [[NSBezierPath bezierPathWithOvalInRect:box] fill];
        [s3g::clap_gui::color(selected ? 0xf0f0f0 : 0x0c0c0c, 0.95) setStroke];
        [[NSBezierPath bezierPathWithOvalInRect:box] stroke];
        [[NSString stringWithFormat:@"%u", node + 1u] drawAtPoint:NSMakePoint(pt.x + size * 0.62, pt.y - 7.0) withAttributes:small];
    }

    NSRect cursorPanel = NSMakeRect(596, 34, 312, 278);
    s3g::clap_gui::drawPanelFrame(cursorPanel.origin.x, cursorPanel.origin.y, cursorPanel.size.width, cursorPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"BUS RELATIONSHIP", true, cursorPanel.origin.x, cursorPanel.origin.y, cursorPanel.size.width, 21, text, style);
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    [self drawSlider:@"NODES" value:[NSString stringWithFormat:@"%u", p->params.nodeCount] norm:(p->params.nodeCount - 1.0) / static_cast<double>(s3g::kAmbiNodeBusMixerMaxNodes - 1u) y:88 attrs:small style:style];
    [self drawSlider:@"INFL" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.cursorInfluence * 100.0f)] norm:p->params.cursorInfluence y:114 attrs:small style:style];
    [self drawSlider:@"X" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorX)] norm:(p->params.cursorX + 2.0) * 0.25 y:140 attrs:small style:style];
    [self drawSlider:@"Y" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorY)] norm:(p->params.cursorY + 2.0) * 0.25 y:166 attrs:small style:style];
    if (zLocked(*p)) {
        [self drawReadoutRow:@"Z" value:@"LOCKED" y:192 attrs:small style:style];
    } else {
        [self drawSlider:@"Z" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorZ)] norm:(p->params.cursorZ + 2.0) * 0.25 y:192 attrs:small style:style];
    }
    [self drawCheckRow:@"LOCK Z" checked:zLocked(*p) y:218 attrs:small style:style];
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(p->params.outputGainDb)] norm:(p->params.outputGainDb + 60.0) / 72.0 y:244 attrs:small style:style];
#else
    [self drawMenuRow:@"BED" value:orderOrLayoutText(*p) y:88 attrs:small style:style];
    [self drawReadoutRow:@"CH" value:[NSString stringWithFormat:@"%u", p->params.outputChannels] y:114 attrs:small style:style];
    [self drawSlider:@"NODES" value:[NSString stringWithFormat:@"%u", p->params.nodeCount] norm:(p->params.nodeCount - 1.0) / 15.0 y:140 attrs:small style:style];
    [self drawSlider:@"INFL" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.cursorInfluence * 100.0f)] norm:p->params.cursorInfluence y:166 attrs:small style:style];
    [self drawSlider:@"X" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorX)] norm:(p->params.cursorX + 2.0) * 0.25 y:192 attrs:small style:style];
    [self drawSlider:@"Y" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorY)] norm:(p->params.cursorY + 2.0) * 0.25 y:218 attrs:small style:style];
    if (zLocked(*p)) {
        [self drawReadoutRow:@"Z" value:@"LOCKED" y:244 attrs:small style:style];
    } else {
        [self drawSlider:@"Z" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorZ)] norm:(p->params.cursorZ + 2.0) * 0.25 y:244 attrs:small style:style];
    }
    [self drawCheckRow:@"LOCK Z" checked:zLocked(*p) y:270 attrs:small style:style];
    [self drawSlider:@"OUT" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(p->params.outputGainDb)] norm:(p->params.outputGainDb + 60.0) / 72.0 y:296 attrs:small style:style];
#endif

    NSRect nodePanel = NSMakeRect(596, 328, 312, kAmbi ? 242 : 326);
    s3g::clap_gui::drawPanelFrame(nodePanel.origin.x, nodePanel.origin.y, nodePanel.size.width, nodePanel.size.height, style);
    s3g::clap_gui::drawPanelHeader([NSString stringWithFormat:@"NODE %02d", _selectedNode + 1], true, nodePanel.origin.x, nodePanel.origin.y, nodePanel.size.width, 21, text, style);
    const uint32_t node = static_cast<uint32_t>(std::clamp(_selectedNode, 0, static_cast<int>(p->params.nodeCount) - 1));
    [self drawSlider:@"ACT" value:(nodeActive(*p, node) ? @"ON" : @"OFF") norm:nodeActive(*p, node) ? 1.0 : 0.0 y:366 attrs:small style:style];
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    [self drawMenuRow:@"SRC" value:ns(s3g::nodeTrackLayoutName(p->params.nodes[node].sourceLayout)) y:392 attrs:small style:style];
    const uint32_t busStart = p->params.nodes[node].inputStart;
    const uint32_t busEnd = std::min<uint32_t>(s3g::kNodeTrackMixerMaxChannels, busStart + p->params.nodes[node].sourceChannels - 1u);
    NSString* busText = busStart > s3g::kNodeTrackMixerMaxChannels
        ? @"OVER"
        : [NSString stringWithFormat:@"%03u-%03u", busStart, busEnd];
    [self drawReadoutRow:@"BUS" value:busText y:418 attrs:small style:style];
#endif
    [self drawSlider:@"LVL" value:[NSString stringWithFormat:@"%+.1f", static_cast<double>(nodeLevelDb(*p, node))] norm:(nodeLevelDb(*p, node) + 60.0) / 72.0 y:(kAmbi ? 392 : 444) attrs:small style:style];
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    [[NSString stringWithFormat:@"BUS %03u-%03u", node * s3g::kAmbiNodeBusMixerChannelsPerNode + 1u, (node + 1u) * s3g::kAmbiNodeBusMixerChannelsPerNode] drawAtPoint:NSMakePoint(610, 420) withAttributes:small];
#endif
    [self drawSlider:@"X" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(nodeX(*p, node))] norm:(nodeX(*p, node) + 2.0) * 0.25 y:(kAmbi ? 444 : 470) attrs:small style:style];
    [self drawSlider:@"Y" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(nodeY(*p, node))] norm:(nodeY(*p, node) + 2.0) * 0.25 y:(kAmbi ? 470 : 496) attrs:small style:style];
    if (zLocked(*p)) {
        [self drawReadoutRow:@"Z" value:@"LOCKED" y:(kAmbi ? 496 : 522) attrs:small style:style];
    } else {
        [self drawSlider:@"Z" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(nodeZ(*p, node))] norm:(nodeZ(*p, node) + 2.0) * 0.25 y:(kAmbi ? 496 : 522) attrs:small style:style];
    }
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    [self drawSlider:@"RAD" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeSizeOrRadius(*p, node))] norm:nodeSizeOrRadius(*p, node) / 8.0 y:522 attrs:small style:style];
    [self drawSlider:@"FOC" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeFocus(*p, node))] norm:(nodeFocus(*p, node) - 0.5) / 3.5 y:548 attrs:small style:style];
#else
    [self drawSlider:@"RAD" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeSizeOrRadius(*p, node))] norm:nodeSizeOrRadius(*p, node) / 4.0 y:548 attrs:small style:style];
    [self drawSlider:@"FOC" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeFocus(*p, node))] norm:(nodeFocus(*p, node) - 0.5) / 3.5 y:574 attrs:small style:style];
    [self drawSlider:@"AZR" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(nodeRotateAz(*p, node))] norm:(nodeRotateAz(*p, node) + 180.0) / 360.0 y:600 attrs:small style:style];
    [self drawSlider:@"ELR" value:[NSString stringWithFormat:@"%+.0f", static_cast<double>(nodeRotateEl(*p, node))] norm:(nodeRotateEl(*p, node) + 90.0) / 180.0 y:626 attrs:small style:style];
#endif

    const uint32_t meterNodes = std::min<uint32_t>(p->params.nodeCount, kParamNodeLimit);
    const uint32_t columns = meterNodes > 8u ? 4u : 2u;
    const uint32_t rows = (meterNodes + columns - 1u) / columns;
    const CGFloat meterW = columns > 2u ? 124.0 : 252.0;
    const CGFloat nodeGainTitleY = 624.0;
    const CGFloat nodeGainRowsY = 647.0;
    const CGFloat peakBaseY = 674.0;
    [@"NODE GAIN / POST CURSOR" drawAtPoint:NSMakePoint(28, nodeGainTitleY) withAttributes:text];
    for (uint32_t nodeIndex = 0; nodeIndex < meterNodes; ++nodeIndex) {
        const uint32_t col = nodeIndex / rows;
        const uint32_t row = nodeIndex % rows;
        const CGFloat x = 28.0 + static_cast<CGFloat>(col) * (meterW + 16.0);
        const CGFloat y = nodeGainRowsY + static_cast<CGFloat>(row) * 20.0;
        const float w = std::clamp<float>(weights[nodeIndex], 0.0f, 1.0f);
        [self drawWeightSlider:[NSString stringWithFormat:@"N%02u", nodeIndex + 1u] weight:w rect:NSMakeRect(x, y, meterW, 13) attrs:small style:style];
    }
    const CGFloat peakY = peakBaseY + static_cast<CGFloat>(rows) * 20.0;
    const float pk = p->outputPeak.exchange(p->outputPeak.load(std::memory_order_relaxed) * 0.92f, std::memory_order_relaxed);
    [@"PEAK METER / POST CURSOR" drawAtPoint:NSMakePoint(28, peakY) withAttributes:text];
    [self drawPeakMeter:@"OUT" peak:pk rect:NSMakeRect(28, peakY + 23.0, 520, 13) attrs:small style:style];
    for (uint32_t nodeIndex = 0; nodeIndex < meterNodes; ++nodeIndex) {
        const uint32_t col = nodeIndex / rows;
        const uint32_t row = nodeIndex % rows;
        const CGFloat x = 28.0 + static_cast<CGFloat>(col) * (meterW + 16.0);
        const CGFloat y = peakY + 46.0 + static_cast<CGFloat>(row) * 20.0;
        const float nodePk = p->nodePeaks[nodeIndex].load(std::memory_order_relaxed);
        [self drawPeakMeter:[NSString stringWithFormat:@"N%02u", nodeIndex + 1u] peak:nodePk rect:NSMakeRect(x, y, meterW, 13) attrs:small style:style];
    }

    if (_openMenu >= 0) {
        if (_openMenu == 1) {
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
            NSString* items[] = { @"1OA", @"2OA", @"3OA" };
            s3g::clap_gui::drawDropdownMenu([self menuRect:1], 21.0, items, 3, static_cast<int>(p->params.order), _hoverMenuIndex, text, style);
#else
            s3g::clap_gui::drawDropdownMenu([self menuRect:1], 21.0, regularLayoutMenuItems(), s3g::kNodeTrackRegularLayoutCount, static_cast<int>(s3g::nodeTrackRegularLayoutIndex(p->params.outputLayout)), _hoverMenuIndex, text, style);
#endif
        } else if (_openMenu == 2) {
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
            s3g::clap_gui::drawDropdownMenu([self menuRect:2], 21.0, regularLayoutMenuItems(), s3g::kNodeTrackRegularLayoutCount, static_cast<int>(s3g::nodeTrackRegularLayoutIndex(p->params.nodes[node].sourceLayout)), _hoverMenuIndex, text, style);
#endif
        }
    }
}
- (void)resetSlider:(int)index
{
    const uint32_t node = static_cast<uint32_t>(std::max(0, _selectedNode));
    switch (index) {
    case 0: [self setParam:kParamNodeCount value:kAmbi ? static_cast<double>(s3g::kAmbiNodeBusMixerMaxNodes) : 4.0]; break;
    case 1: [self setParam:kParamCursorInfluence value:kAmbi ? 1.0 : 0.0]; break;
    case 2: [self setParam:kParamCursorX value:0.0]; break;
    case 3: [self setParam:kParamCursorY value:0.0]; break;
    case 4: [self setParam:kParamCursorZ value:0.0]; break;
    case 5:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
#else
        [self setParam:kParamOutputGain value:0.0];
#endif
        break;
    case 6:
        [self setParam:kParamLockZ value:1.0];
        break;
    case 7:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamOutputGain value:0.0];
#endif
        break;
    case 8:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamLayoutOrOrder value:s3g::nodeTrackRegularLayoutIndex(static_cast<Plugin*>(_plugin)->params.outputLayout)];
#endif
        break;
    case 10: [self setParam:kParamNodeBase + node * kParamNodeStride + 0 value:1.0]; break;
    case 11: [self setParam:kParamNodeBase + node * kParamNodeStride + 1 value:0.0]; break;
    case 12:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamNodeBase + node * kParamNodeStride + 2 value:s3g::nodeTrackRegularLayoutIndex(static_cast<Plugin*>(_plugin)->params.nodes[node].sourceLayout)];
#endif
        break;
    case 13: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 2 : 5) value:0.0]; break;
    case 14: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 3 : 6) value:0.0]; break;
    case 15: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 4 : 7) value:0.0]; break;
    case 16: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 5 : 8) value:kAmbi ? 0.65 : 1.0]; break;
    case 17:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
#else
        [self setParam:kParamNodeBase + node * kParamNodeStride + 9 value:1.0];
#endif
        if (kAmbi) [self setParam:kParamNodeBase + node * kParamNodeStride + 6 value:1.0];
        break;
    case 18:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamNodeBase + node * kParamNodeStride + 2 value:s3g::nodeTrackRegularLayoutIndex(static_cast<Plugin*>(_plugin)->params.nodes[node].sourceLayout)];
#endif
        break;
    case 19:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamNodeRotateAzBase + node value:0.0];
#endif
        break;
    case 20:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamNodeRotateElBase + node value:0.0];
#endif
        break;
    default: break;
    }
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp((pt.x - 714.0) / 128.0, 0.0, 1.0);
    const uint32_t node = static_cast<uint32_t>(std::clamp(_selectedNode, 0, static_cast<int>(static_cast<Plugin*>(_plugin)->params.nodeCount) - 1));
    switch (_dragSlider) {
    case 0: [self setParam:kParamNodeCount value:1.0 + norm * static_cast<double>((kAmbi ? s3g::kAmbiNodeBusMixerMaxNodes : s3g::kNodeTrackMixerMaxNodes) - 1u)]; break;
    case 1: [self setParam:kParamCursorInfluence value:norm]; break;
    case 2: [self setParam:kParamCursorX value:-2.0 + norm * 4.0]; break;
    case 3: [self setParam:kParamCursorY value:-2.0 + norm * 4.0]; break;
    case 4: [self setParam:kParamCursorZ value:-2.0 + norm * 4.0]; break;
    case 5:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
#else
        [self setParam:kParamOutputGain value:-60.0 + norm * 72.0];
#endif
        break;
    case 6:
        [self setParam:kParamLockZ value:norm >= 0.5 ? 1.0 : 0.0];
        break;
    case 7:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamOutputGain value:-60.0 + norm * 72.0];
#endif
        break;
    case 8:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
#endif
        break;
    case 10: [self setParam:kParamNodeBase + node * kParamNodeStride + 0 value:norm >= 0.5 ? 1.0 : 0.0]; break;
    case 11: [self setParam:kParamNodeBase + node * kParamNodeStride + 1 value:-60.0 + norm * 72.0]; break;
    case 12:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
#endif
        break;
    case 13: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 2 : 5) value:-2.0 + norm * 4.0]; break;
    case 14: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 3 : 6) value:-2.0 + norm * 4.0]; break;
    case 15: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 4 : 7) value:-2.0 + norm * 4.0]; break;
    case 16: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 5 : 8) value:kAmbi ? (0.05 + norm * 7.95) : (0.05 + norm * 3.95)]; break;
    case 17:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
#else
        [self setParam:kParamNodeBase + node * kParamNodeStride + 9 value:0.5 + norm * 3.5];
#endif
        if (kAmbi) [self setParam:kParamNodeBase + node * kParamNodeStride + 6 value:0.5 + norm * 3.5];
        break;
    case 18:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
#endif
        break;
    case 19:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamNodeRotateAzBase + node value:-180.0 + norm * 360.0];
#endif
        break;
    case 20:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamNodeRotateElBase + node value:-90.0 + norm * 180.0];
#endif
        break;
    default: break;
    }
}
- (void)updateSpatialAtPoint:(NSPoint)pt cursor:(BOOL)cursor
{
    const NSRect field = [self fieldRect];
    const CGFloat scale = [self viewScaleForRect:field];
    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.52;
    auto* plug = static_cast<Plugin*>(_plugin);
    double x = cursor ? plug->params.cursorX : nodeX(*plug, static_cast<uint32_t>(_selectedNode));
    double y = cursor ? plug->params.cursorY : nodeY(*plug, static_cast<uint32_t>(_selectedNode));
    double z = cursor ? plug->params.cursorZ : nodeZ(*plug, static_cast<uint32_t>(_selectedNode));
    if (_viewMode == 0) {
        x = std::clamp(static_cast<double>((cy - pt.y) / scale), -2.0, 2.0);
        y = std::clamp(static_cast<double>((cx - pt.x) / scale), -2.0, 2.0);
    } else if (_viewMode == 1) {
        y = std::clamp(static_cast<double>((cx - pt.x) / scale), -2.0, 2.0);
        z = std::clamp(static_cast<double>((cy - pt.y) / scale), -2.0, 2.0);
    } else {
        const CGFloat yaw = _viewYaw * static_cast<CGFloat>(s3g::kPi / 180.0);
        const CGFloat pitch = _viewPitch * static_cast<CGFloat>(s3g::kPi / 180.0);
        const double yr = static_cast<double>((cx - pt.x) / scale);
        const double denom = std::max(0.15, std::abs(static_cast<double>(std::sin(pitch))));
        const double xr = (z * std::cos(pitch) - static_cast<double>((cy - pt.y) / scale)) / denom;
        x = std::clamp(xr * std::cos(yaw) + yr * std::sin(yaw), -2.0, 2.0);
        y = std::clamp(-xr * std::sin(yaw) + yr * std::cos(yaw), -2.0, 2.0);
    }
    if (zLocked(*plug)) z = 0.0;
    if (cursor) {
        [self setParam:kParamCursorX value:x];
        [self setParam:kParamCursorY value:y];
        [self setParam:kParamCursorZ value:z];
        return;
    }
    const uint32_t node = static_cast<uint32_t>(std::clamp(_selectedNode, 0, static_cast<int>(static_cast<Plugin*>(_plugin)->params.nodeCount) - 1));
    [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 2 : 5) value:x];
    [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 3 : 6) value:y];
    [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 4 : 7) value:z];
}
- (void)mouseDown:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    if (_openMenu >= 0) {
        const uint32_t count = kAmbi ? 3u : s3g::kNodeTrackRegularLayoutCount;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, [self menuRect:_openMenu], 21.0, count);
        if (hit >= 0) {
            if (_openMenu == 1) {
                [self setParam:kParamLayoutOrOrder value:hit];
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
                [self setParam:kParamOutputChannels value:s3g::nodeTrackDefaultChannelsForLayout(s3g::nodeTrackRegularLayoutFromIndex(static_cast<uint32_t>(hit)))];
#endif
            }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
            else if (_openMenu == 2) {
                const uint32_t selected = static_cast<uint32_t>(_selectedNode);
                [self setParam:kParamNodeBase + selected * kParamNodeStride + 2 value:hit];
                [self setParam:kParamNodeBase + selected * kParamNodeStride + 3 value:s3g::nodeTrackDefaultChannelsForLayout(s3g::nodeTrackRegularLayoutFromIndex(static_cast<uint32_t>(hit)))];
            }
#endif
        }
        _openMenu = -1;
        _hoverMenuIndex = -1;
        [self setNeedsDisplay:YES];
        return;
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    if (NSPointInRect(pt, NSMakeRect(604, 79, 292, 24))) { _openMenu = 1; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(pt, NSMakeRect(604, 383, 292, 24))) { _openMenu = 2; [self setNeedsDisplay:YES]; return; }
#endif
    const int viewHit = [self viewButtonHit:pt];
    if (viewHit >= 0) {
        if (viewHit <= 2) {
            _viewMode = viewHit;
            if (_viewMode == 2) { _viewYaw = -35.0; _viewPitch = 30.0; }
        } else if (viewHit == 3) {
            _viewZoom = std::max<CGFloat>(0.50, _viewZoom / 1.25);
        } else if (viewHit == 4) {
            _viewZoom = std::min<CGFloat>(3.00, _viewZoom * 1.25);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    NSRect field = [self fieldRect];
    if (NSPointInRect(pt, field)) {
        if (([event modifierFlags] & NSEventModifierFlagShift) != 0) {
            _dragView = YES;
            _lastDragPoint = pt;
            return;
        }
        const NSPoint cursorPt = [self projectX:p->params.cursorX y:p->params.cursorY z:p->params.cursorZ rect:field];
        if (std::hypot(cursorPt.x - pt.x, cursorPt.y - pt.y) <= 15.0) {
            _dragCursor = YES;
            [self updateSpatialAtPoint:pt cursor:YES];
            return;
        }
        for (uint32_t node = 0; node < p->params.nodeCount; ++node) {
            const NSPoint nodePt = [self projectX:nodeX(*p, node) y:nodeY(*p, node) z:nodeZ(*p, node) rect:field];
            if (std::hypot(nodePt.x - pt.x, nodePt.y - pt.y) <= 14.0) {
                _selectedNode = static_cast<int>(node);
                _dragNode = YES;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        return;
    }
    const CGFloat cursorYsAmbi[] = { 88, 114, 140, 166, 192, 218, 244 };
    const int cursorIdxAmbi[] = { 0, 1, 2, 3, 4, 6, 7 };
    const CGFloat cursorYsGeneral[] = { 140, 166, 192, 218, 244, 270, 296 };
    const int cursorIdxGeneral[] = { 0, 1, 2, 3, 4, 6, 5 };
    const CGFloat* cursorYs = kAmbi ? cursorYsAmbi : cursorYsGeneral;
    const int* cursorIdx = kAmbi ? cursorIdxAmbi : cursorIdxGeneral;
    const int cursorCount = kAmbi ? 7 : 7;
    for (int i = 0; i < cursorCount; ++i) {
        if (NSPointInRect(pt, NSMakeRect(596, cursorYs[i] - 8, 304, 24))) {
            const int sliderIndex = cursorIdx[i];
            if (sliderIndex == 4 && zLocked(*p)) return;
            if (sliderIndex == 6) {
                [self setParam:kParamLockZ value:zLocked(*p) ? 0.0 : 1.0];
                return;
            }
            if ([event clickCount] >= 2) { [self resetSlider:sliderIndex]; return; }
            _dragSlider = sliderIndex;
            [self updateSliderAtPoint:pt];
            return;
        }
    }
    const CGFloat nodeYsAmbi[] = { 366, 392, 444, 470, 496, 522, 548 };
    const int nodeIdxAmbi[] = { 10, 11, 13, 14, 15, 16, 17 };
    const CGFloat nodeYsGeneral[] = { 366, 444, 470, 496, 522, 548, 574, 600, 626 };
    const int nodeIdxGeneral[] = { 10, 11, 13, 14, 15, 16, 17, 19, 20 };
    const CGFloat* ys = kAmbi ? nodeYsAmbi : nodeYsGeneral;
    const int* sliderIdx = kAmbi ? nodeIdxAmbi : nodeIdxGeneral;
    const int count = kAmbi ? 7 : 9;
    for (int i = 0; i < count; ++i) {
        if (NSPointInRect(pt, NSMakeRect(596, ys[i] - 8, 304, 24))) {
            const int sliderIndex = sliderIdx[i];
            if (sliderIndex == 15 && zLocked(*p)) return;
            if ([event clickCount] >= 2) { [self resetSlider:sliderIndex]; return; }
            _dragSlider = sliderIndex;
            [self updateSliderAtPoint:pt];
            return;
        }
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu < 0) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const uint32_t count = kAmbi ? 3u : s3g::kNodeTrackRegularLayoutCount;
    _hoverMenuIndex = s3g::clap_gui::dropdownHitIndex(pt, [self menuRect:_openMenu], 21.0, count);
    [self setNeedsDisplay:YES];
}
- (void)mouseDragged:(NSEvent*)event
{
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragCursor) { [self updateSpatialAtPoint:pt cursor:YES]; return; }
    if (_dragNode) { [self updateSpatialAtPoint:pt cursor:NO]; return; }
    if (_dragView) {
        _viewMode = 2;
        _viewYaw += (pt.x - _lastDragPoint.x) * 0.35;
        _viewPitch = std::clamp<CGFloat>(_viewPitch + (pt.y - _lastDragPoint.y) * 0.35, -75.0, 75.0);
        _lastDragPoint = pt;
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragSlider >= 0) [self updateSliderAtPoint:pt];
}
- (void)mouseUp:(NSEvent*)event { (void)event; _dragSlider = -1; _dragNode = NO; _dragCursor = NO; _dragView = NO; }
@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3GNodeBusMixerView alloc] initWithPlugin:p]; return p->guiView != nullptr; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); auto* v = static_cast<S3GNodeBusMixerView*>(p->guiView); [v stopRefreshTimer]; [v removeFromSuperview]; [v release]; p->guiView = nullptr; } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiCanResize(const clap_plugin_t*) { return false; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { if (!hints) return false; hints->can_resize_horizontally = false; hints->can_resize_vertically = false; hints->preserve_aspect_ratio = false; hints->aspect_ratio_width = 0; hints->aspect_ratio_height = 0; return true; }
bool guiAdjustSize(const clap_plugin_t*, uint32_t* w, uint32_t* h) { if (!w || !h) return false; *w = kGuiWidth; *h = kGuiHeight; return true; }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { auto* p = self(plugin); if (!p->guiView) return false; [static_cast<NSView*>(p->guiView) setFrameSize:NSMakeSize(w, h)]; return true; }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); if (!p->guiView) return false; NSView* parent = static_cast<NSView*>(win->cocoa); NSView* v = static_cast<NSView*>(p->guiView); [parent addSubview:v]; [v setFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)]; return true; }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<NSView*>(p->guiView) setHidden:NO]; [static_cast<S3GNodeBusMixerView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GNodeBusMixerView*>(p->guiView) stopRefreshTimer]; [static_cast<NSView*>(p->guiView) setHidden:YES]; return true; }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };

#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kPluginName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    kPluginDesc,
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    initializeDefaultParams(*p);
    sanitizeAndSet(*p);
    p->processor.prepare(48000.0);
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
    p->plugin.get_extension = pluginGetExtension;
    p->plugin.on_main_thread = onMainThread;
    return &p->plugin;
}
uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0 ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
