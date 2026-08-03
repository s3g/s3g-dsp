#include "s3g_node_track_mixer.h"
#include "s3g_realtime.h"
#include "../common/s3g_gui_layout.h"
#include "../common/s3g_clap_state_stream.h"
#include "../common/s3g_clap_gui_param_queue.h"

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
#define S3G_NODE_BUS_MIXER_VIEW_CLASS S3GAmbiNodeBusMixerView
constexpr bool kAmbi = true;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-node-bus-mixer";
constexpr const char* kHostName = "s3g Ambi Mixer Node Bus 128";
constexpr const char* kWindowTitle = "s3g AMBI MIXER NODE BUS 128CH";
constexpr const char* kPluginDesc = "128-channel ambisonic node/cursor bus mixer.";
constexpr const char* kPortName = "Ambi Node Bus Mix";
using Processor = s3g::AmbiNodeTrackMixer;
using Params = s3g::AmbiNodeTrackMixerParams;
#else
#define S3G_NODE_BUS_MIXER_VIEW_CLASS S3GNodeBusMixerView
constexpr bool kAmbi = false;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.node-bus-mixer";
constexpr const char* kHostName = "s3g Mixer Node Bus 128";
constexpr const char* kWindowTitle = "s3g MIXER NODE BUS 128CH";
constexpr const char* kPluginDesc = "128-channel multichannel node/cursor bus mixer.";
constexpr const char* kPortName = "Node Bus Mix";
using Processor = s3g::NodeTrackMixer;
using Params = s3g::NodeTrackMixerParams;
#endif

constexpr uint32_t kGuiWidth = 920;
constexpr uint32_t kGuiHeight = 920;
constexpr const auto& kMixerLayout = s3g::gui_layout::kMixerFamilyLayout;
constexpr uint32_t kStateVersion = 2;
constexpr clap_id kParamNodeBase = 1000;
constexpr uint32_t kParamNodeLimit = kAmbi ? s3g::kAmbiNodeBusMixerMaxNodes : s3g::kNodeTrackMixerMaxNodes;
constexpr clap_id kParamNodeStride = kAmbi ? 7 : 10;
constexpr clap_id kParamNodeRotateAzBase = 2000;
constexpr clap_id kParamNodeRotateElBase = 2100;
constexpr uint32_t kParameterBankSize = 2200u;

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

constexpr std::array<clap_id, 14> kGlobalParamIds {
    kParamLayoutOrOrder,
    kParamOutputChannels,
    kParamNodeCount,
    kParamMixMode,
    kParamCursorInfluence,
    kParamCursorX,
    kParamCursorY,
    kParamCursorZ,
    kParamStackPosition,
    kParamCursorRadius,
    kParamCursorFocus,
    kParamCursorGate,
    kParamOutputGain,
    kParamLockZ,
};

struct SavedState {
    uint32_t version = kStateVersion;
    Params params {};
};

struct PublishedParamBank {
    std::array<std::atomic<double>, kParameterBankSize> values {};
    std::atomic<uint64_t> sequence { 0u };
    std::atomic<uint64_t> stamp { 0u };
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    Params params {};
    Params audioParams {};
    Processor processor {};
    PublishedParamBank controlParamBank {};
    PublishedParamBank audioParamBank {};
    std::atomic<uint64_t> publicationClock { 0u };
    uint64_t audioConsumedControlStamp = 0u;
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kNodeTrackMixerMaxNodes> nodePeaks {};
    std::array<std::atomic<float>, s3g::kNodeTrackMixerMaxNodes> publishedNodeWeights {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    char presetName[64] { "INIT" };
#endif
};

static_assert(std::atomic<double>::is_always_lock_free,
    "Node Bus parameter publication must remain lock-free");

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

Params sanitizeParams(Params params)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    return s3g::sanitizeAmbiNodeTrackMixerParams(params);
#else
    return s3g::sanitizeNodeTrackMixerParams(params);
#endif
}

void setAudioParams(Plugin& p, Params params)
{
    p.audioParams = sanitizeParams(params);
    p.processor.setParams(p.audioParams);
    const auto weights = p.processor.nodeWeights();
    for (uint32_t node = 0u; node < weights.size(); ++node) {
        p.publishedNodeWeights[node].store(weights[node],
            std::memory_order_relaxed);
    }
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

bool applyNodeParam(Params& params, uint32_t node, uint32_t field, double value)
{
    if (node >= kParamNodeLimit) return false;
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    auto& n = params.nodes[node];
    switch (field) {
    case 0: n.active = value >= 0.5; break;
    case 1: n.levelDb = static_cast<float>(value); break;
    case 2: n.x = static_cast<float>(value); break;
    case 3: n.y = static_cast<float>(value); break;
    case 4: n.z = static_cast<float>(value); break;
    case 5: n.radius = static_cast<float>(value); break;
    case 6: n.focus = static_cast<float>(value); break;
    default: return false;
    }
#else
    auto& n = params.nodes[node];
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
    default: return false;
    }
#endif
    return true;
}

bool applyParam(Params& params, clap_id id, double value)
{
    if (id >= kParamNodeBase && id < kParamNodeBase + kParamNodeLimit * kParamNodeStride) {
        const uint32_t rel = id - kParamNodeBase;
        return applyNodeParam(params, rel / kParamNodeStride, rel % kParamNodeStride, value);
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    if (id >= kParamNodeRotateAzBase && id < kParamNodeRotateAzBase + s3g::kNodeTrackMixerMaxNodes) {
        params.nodes[id - kParamNodeRotateAzBase].rotateAzDeg = static_cast<float>(value);
        return true;
    }
    if (id >= kParamNodeRotateElBase && id < kParamNodeRotateElBase + s3g::kNodeTrackMixerMaxNodes) {
        params.nodes[id - kParamNodeRotateElBase].rotateElDeg = static_cast<float>(value);
        return true;
    }
#endif
    switch (id) {
    case kParamLayoutOrOrder:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        params.order = s3g::AmbiNodeTrackOrder::O3;
#else
        params.outputLayout = s3g::nodeTrackRegularLayoutFromIndex(roundedUint(value));
#endif
        break;
    case kParamOutputChannels:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        params.outputChannels = roundedUint(value);
#endif
        break;
    case kParamNodeCount: params.nodeCount = roundedUint(value); break;
    case kParamMixMode:
        params.mixMode = s3g::NodeTrackMixMode::SpatialObjects;
        break;
    case kParamCursorInfluence: params.cursorInfluence = static_cast<float>(value); break;
    case kParamCursorX: params.cursorX = static_cast<float>(value); break;
    case kParamCursorY: params.cursorY = static_cast<float>(value); break;
    case kParamCursorZ: params.cursorZ = static_cast<float>(value); break;
    case kParamStackPosition: params.stackPosition = static_cast<float>(value); break;
    case kParamCursorRadius: params.cursorRadius = static_cast<float>(value); break;
    case kParamCursorFocus: params.cursorFocus = static_cast<float>(value); break;
    case kParamCursorGate: params.cursorGate = static_cast<float>(value); break;
    case kParamOutputGain: params.outputGainDb = static_cast<float>(value); break;
    case kParamLockZ: params.lockZ = value >= 0.5; break;
    default: return false;
    }
    return true;
}

double nodeParamValue(const Params& params, uint32_t node, uint32_t field)
{
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    const auto& n = params.nodes[node];
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
    const auto& n = params.nodes[node];
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

double getParam(const Params& params, clap_id id)
{
    if (id >= kParamNodeBase && id < kParamNodeBase + kParamNodeLimit * kParamNodeStride) {
        const uint32_t rel = id - kParamNodeBase;
        return nodeParamValue(params, rel / kParamNodeStride, rel % kParamNodeStride);
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    if (id >= kParamNodeRotateAzBase && id < kParamNodeRotateAzBase + s3g::kNodeTrackMixerMaxNodes) {
        return params.nodes[id - kParamNodeRotateAzBase].rotateAzDeg;
    }
    if (id >= kParamNodeRotateElBase && id < kParamNodeRotateElBase + s3g::kNodeTrackMixerMaxNodes) {
        return params.nodes[id - kParamNodeRotateElBase].rotateElDeg;
    }
#endif
    switch (id) {
    case kParamLayoutOrOrder:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        return static_cast<uint32_t>(params.order);
#else
        return s3g::nodeTrackRegularLayoutIndex(params.outputLayout);
#endif
    case kParamOutputChannels:
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        return 128.0;
#else
        return params.outputChannels;
#endif
    case kParamNodeCount: return params.nodeCount;
    case kParamMixMode: return static_cast<uint32_t>(params.mixMode);
    case kParamCursorInfluence: return params.cursorInfluence;
    case kParamCursorX: return params.cursorX;
    case kParamCursorY: return params.cursorY;
    case kParamCursorZ: return params.cursorZ;
    case kParamStackPosition: return params.stackPosition;
    case kParamCursorRadius: return params.cursorRadius;
    case kParamCursorFocus: return params.cursorFocus;
    case kParamCursorGate: return params.cursorGate;
    case kParamOutputGain: return params.outputGainDb;
    case kParamLockZ: return params.lockZ ? 1.0 : 0.0;
    default: return 0.0;
    }
}

template <typename Fn>
void forEachStoredParamId(Fn&& fn)
{
    for (const clap_id id : kGlobalParamIds) fn(id);
    for (uint32_t node = 0; node < kParamNodeLimit; ++node) {
        for (uint32_t field = 0; field < kParamNodeStride; ++field) {
            fn(kParamNodeBase + node * kParamNodeStride + field);
        }
    }
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    for (uint32_t node = 0; node < s3g::kNodeTrackMixerMaxNodes; ++node) {
        fn(kParamNodeRotateAzBase + node);
        fn(kParamNodeRotateElBase + node);
    }
#endif
}

void writeParamsToBank(PublishedParamBank& bank, const Params& params,
                       uint64_t stamp)
{
    // Each bank has exactly one producer: the host/control thread for the
    // control bank and the audio thread for the audio-report bank. The stamp
    // becomes visible only after the complete even-sequence snapshot.
    bank.sequence.fetch_add(1u, std::memory_order_acq_rel);
    forEachStoredParamId([&](clap_id id) {
        bank.values[id].store(getParam(params, id),
            std::memory_order_relaxed);
    });
    bank.sequence.fetch_add(1u, std::memory_order_release);
    bank.stamp.store(stamp, std::memory_order_release);
}

bool tryParamsFromBank(const PublishedParamBank& bank, Params base,
                       Params& result)
{
    const uint64_t before = bank.sequence.load(std::memory_order_acquire);
    if ((before & 1u) != 0u) return false;
    forEachStoredParamId([&](clap_id id) {
        applyParam(base, id,
            bank.values[id].load(std::memory_order_relaxed));
    });
    const uint64_t after = bank.sequence.load(std::memory_order_acquire);
    if (before != after || (after & 1u) != 0u) return false;
    result = sanitizeParams(base);
    return true;
}

uint64_t nextPublicationStamp(Plugin& p)
{
    return p.publicationClock.fetch_add(
        1u, std::memory_order_relaxed) + 1u;
}

Params latestParamsSnapshot(const Plugin& p, Params fallback)
{
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint64_t controlStamp = p.controlParamBank.stamp.load(
            std::memory_order_acquire);
        const uint64_t audioStamp = p.audioParamBank.stamp.load(
            std::memory_order_acquire);
        const PublishedParamBank* first = audioStamp > controlStamp
            ? &p.audioParamBank : &p.controlParamBank;
        const PublishedParamBank* second = first == &p.audioParamBank
            ? &p.controlParamBank : &p.audioParamBank;
        Params snapshot {};
        if (tryParamsFromBank(*first, fallback, snapshot)) return snapshot;
        if (tryParamsFromBank(*second, fallback, snapshot)) return snapshot;
    }
    return sanitizeParams(fallback);
}

double latestPublishedValue(const Plugin& p, clap_id id)
{
    const uint64_t controlStamp = p.controlParamBank.stamp.load(
        std::memory_order_acquire);
    const uint64_t audioStamp = p.audioParamBank.stamp.load(
        std::memory_order_acquire);
    const auto& bank = audioStamp > controlStamp
        ? p.audioParamBank : p.controlParamBank;
    return bank.values[id].load(std::memory_order_acquire);
}

void syncGuiParams(Plugin& p)
{
    p.params = latestParamsSnapshot(p, p.params);
}

void syncAudioParams(Plugin& p, bool force = false)
{
    const uint64_t controlStamp = p.controlParamBank.stamp.load(
        std::memory_order_acquire);
    if (!force && controlStamp <= p.audioConsumedControlStamp) return;

    Params next {};
    const bool valid = force
        ? (next = latestParamsSnapshot(p, p.audioParams), true)
        : tryParamsFromBank(p.controlParamBank, p.audioParams, next);
    if (!valid) return;
    setAudioParams(p, next);
    p.audioConsumedControlStamp = controlStamp;
}

void publishControlParams(Plugin& p, Params params)
{
    p.params = sanitizeParams(params);
    writeParamsToBank(p.controlParamBank, p.params,
        nextPublicationStamp(p));
    if (p.host && p.host->request_process) p.host->request_process(p.host);
}

void publishAudioParams(Plugin& p)
{
    writeParamsToBank(p.audioParamBank, p.audioParams,
        nextPublicationStamp(p));
}

void readControlParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    Params next = latestParamsSnapshot(p, p.params);
    bool changed = false;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            changed = applyParam(next, param->param_id, param->value) || changed;
        }
    }
    if (changed) publishControlParams(p, next);
}

void applyProcessParamEvents(Plugin& p, const clap_input_events_t* in)
{
    syncAudioParams(p);
    if (!in) return;
    Params next = p.audioParams;
    bool changed = false;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (!ev || ev->space_id != CLAP_CORE_EVENT_SPACE_ID
            || ev->type != CLAP_EVENT_PARAM_VALUE) {
            continue;
        }
        const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
        if (param->param_id >= kParameterBankSize
            || !applyParam(next, param->param_id, param->value)) {
            continue;
        }
        changed = true;
    }
    if (!changed) return;

    setAudioParams(p, next);
    publishAudioParams(p);
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->host && p->host->get_extension) {
        p->hostParams = static_cast<const clap_host_params_t*>(
            p->host->get_extension(p->host, CLAP_EXT_PARAMS));
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

void queueGuiParamEvent(Plugin& p, s3g::clap_gui::ParamEventKind kind,
                        clap_id id, double value = 0.0)
{
    if (p.guiParamEvents.push({
            kind, id, value })) {
        requestGuiParamService(p);
    }
}

void queueGuiParamGestureBegin(Plugin& p, clap_id id)
{
    queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& p, clap_id id, double value)
{
    queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& p, clap_id id)
{
    queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

bool pushGuiParamEvent(const clap_output_events_t* out,
                       const s3g::clap_gui::ParamEvent& pending)
{
    if (!out || !out->try_push) return true;
    if (pending.kind != s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_gesture_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = pending.kind
                == s3g::clap_gui::ParamEventKind::GestureBegin
            ? CLAP_EVENT_PARAM_GESTURE_BEGIN
            : CLAP_EVENT_PARAM_GESTURE_END;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        return out->try_push(out, &event.header);
    }
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
    return out->try_push(out, &event.header);
}

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* out)
{
    Params next = latestParamsSnapshot(p, p.params);
    bool changed = false;
    s3g::clap_gui::ParamEvent pending {};
    while (p.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(out, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            changed = applyParam(next, pending.paramId, pending.value)
                || changed;
        }
        p.guiParamEvents.pop();
    }
    if (changed) publishControlParams(p, next);
}

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p && p->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
#endif
    delete self(plugin);
}
bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->processor.prepare(sampleRate);
    syncAudioParams(*p, true);
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
        p.publishedNodeWeights[node].store(weights[node],
            std::memory_order_relaxed);
        float peak = 0.0f;
        if (in && node < p.audioParams.nodeCount) {
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
            const uint32_t srcCh = s3g::kAmbiNodeBusMixerChannelsPerNode;
            const uint32_t inputStart = node * s3g::kAmbiNodeBusMixerChannelsPerNode;
#else
            const auto& n = p.audioParams.nodes[node];
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
    serviceGuiParamEvents(*p, proc->out_events);
    applyProcessParamEvents(*p, proc->in_events);
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
    std::strncpy(
        info->module,
        kAmbi ? "Ambi Mixer Node Bus" : "Mixer Node Bus",
        sizeof(info->module));
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
        double defaultValue = defs[field];
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
        if (field == 2u || field == 3u) {
            constexpr double kDefaultNodeDistance = 0.78;
            const double angle = static_cast<double>(node) * 2.0
                * static_cast<double>(s3g::kPi)
                / static_cast<double>(s3g::kAmbiNodeBusMixerMaxNodes);
            defaultValue = field == 2u
                ? -std::sin(angle) * kDefaultNodeDistance
                : std::cos(angle) * kDefaultNodeDistance;
        }
#endif
        fillInfo(
            info, kParamNodeBase + node * kParamNodeStride + field,
            name, mins[field], maxs[field], defaultValue);
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
    *value = latestPublishedValue(*self(plugin), paramId);
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
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    const bool isLayout = paramId == kParamLayoutOrOrder
        || (paramId >= kParamNodeBase
            && paramId < kParamNodeBase + kParamNodeLimit * kParamNodeStride
            && (paramId - kParamNodeBase) % kParamNodeStride == 2u);
    if (isLayout) {
        for (uint32_t index = 0u; index < s3g::kNodeTrackRegularLayoutCount; ++index) {
            if (std::strcmp(display, s3g::nodeTrackLayoutName(s3g::nodeTrackRegularLayoutFromIndex(index))) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
#endif
    if (paramId == kParamLockZ) {
        if (std::strcmp(display, "OFF") == 0) { *value = 0.0; return true; }
        if (std::strcmp(display, "ON") == 0) { *value = 1.0; return true; }
        return false;
    }
    *value = std::atof(display);
    return true;
}
void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in,
                 const clap_output_events_t* out)
{
    auto* p = self(plugin);
    readControlParamEvents(*p, in);
    serviceGuiParamEvents(*p, out);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    const SavedState state {
        kStateVersion, latestParamsSnapshot(*p, p->params)
    };
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}
bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    if (!s3g::clap_state::readAll(stream, &state, sizeof(state)) || state.version != kStateVersion) return false;
    auto* p = self(plugin);
    publishControlParams(*p, state.params);
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

@interface S3G_NODE_BUS_MIXER_VIEW_CLASS : NSView {
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
    clap_id _gestureParams[3];
    int _gestureParamCount;
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)beginGesture:(clap_id)param;
- (void)endGestures;
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
- (void)updateSliderAtPoint:(NSPoint)pt;
- (void)updateSpatialAtPoint:(NSPoint)pt cursor:(BOOL)cursor;
@end

@implementation S3G_NODE_BUS_MIXER_VIEW_CLASS
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
        _gestureParamCount = 0;
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
- (void)beginGesture:(clap_id)param
{
    for (int index = 0; index < _gestureParamCount; ++index) {
        if (_gestureParams[index] == param) return;
    }
    if (_gestureParamCount >= 3) return;
    _gestureParams[_gestureParamCount++] = param;
    queueGuiParamGestureBegin(*static_cast<Plugin*>(_plugin), param);
}
- (void)endGestures
{
    auto* p = static_cast<Plugin*>(_plugin);
    for (int index = 0; index < _gestureParamCount; ++index) {
        queueGuiParamGestureEnd(*p, _gestureParams[index]);
    }
    _gestureParamCount = 0;
}
- (void)refreshTimerFired:(NSTimer*)timer
{
    (void)timer;
    if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) {
        syncGuiParams(*static_cast<Plugin*>(_plugin));
        [self setNeedsDisplay:YES];
    }
}
- (void)setParam:(clap_id)param value:(double)value
{
    auto* p = static_cast<Plugin*>(_plugin);
    bool gestureActive = false;
    for (int index = 0; index < _gestureParamCount; ++index) {
        gestureActive = gestureActive || _gestureParams[index] == param;
    }
    if (!gestureActive) queueGuiParamGestureBegin(*p, param);
    syncGuiParams(*p);
    if (!applyParam(p->params, param, value)) {
        if (!gestureActive) queueGuiParamGestureEnd(*p, param);
        return;
    }
    p->params = sanitizeParams(p->params);
    const double publishedValue = getParam(p->params, param);
    queueGuiParamValue(*p, param, publishedValue);
    if (!gestureActive) queueGuiParamGestureEnd(*p, param);
    _selectedNode = std::min<int>(_selectedNode,
        static_cast<int>(p->params.nodeCount) - 1);
    [self setNeedsDisplay:YES];
}
- (void)drawSlider:(NSString*)name value:(NSString*)value norm:(CGFloat)norm y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorSlider(
        name, value, norm, y,
        kMixerLayout.output.frame.x, kMixerLayout.output.frame.width,
        attrs, attrs, style);
}
- (void)drawMenuRow:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawProcessorMenu(
        name, value, y,
        kMixerLayout.output.frame.x, kMixerLayout.output.frame.width,
        attrs, attrs, style);
}
- (void)drawReadoutRow:(NSString*)name value:(NSString*)value y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    (void)style;
    [[name uppercaseString] drawAtPoint:NSMakePoint(
        s3g::gui_layout::processorLabelX(kMixerLayout.output.frame.x),
        y - 4.0) withAttributes:attrs];
    [[value uppercaseString] drawAtPoint:NSMakePoint(
        s3g::gui_layout::processorControlX(kMixerLayout.output.frame.x),
        y - 4.0) withAttributes:attrs];
}
- (void)drawCheckRow:(NSString*)name checked:(BOOL)checked y:(CGFloat)y attrs:(NSDictionary*)attrs style:(s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawToggle(
        [name uppercaseString], checked, y, attrs, attrs, style,
        s3g::gui_layout::processorLabelX(kMixerLayout.output.frame.x),
        s3g::gui_layout::processorControlX(kMixerLayout.output.frame.x),
        64.0);
}
- (NSRect)fieldRect
{
    return s3g::clap_gui::cocoaRect(kMixerLayout.fieldPlot);
}
- (NSRect)menuRect:(int)menu
{
    const CGFloat rows =
        static_cast<CGFloat>(s3g::kNodeTrackRegularLayoutCount);
    const auto& panel = menu == 1
        ? kMixerLayout.busCursor : kMixerLayout.selectedNode;
    const uint32_t row = menu == 1 ? 0u : 1u;
    return NSMakeRect(
        s3g::gui_layout::processorControlX(panel.frame.x),
        s3g::gui_layout::rowY(panel, row) - 7.0,
        s3g::gui_layout::processorMenuWidth(panel.frame.width),
        21.0 * rows);
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
        NSMakeRect(372, 47, 44, 18),
        NSMakeRect(418, 47, 44, 18),
        NSMakeRect(464, 47, 52, 18),
        NSMakeRect(522, 47, 20, 18),
        NSMakeRect(546, 47, 20, 18),
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
    syncGuiParams(*p);
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* small = s3g::clap_gui::softValueAttrs();
    NSDictionary* text = s3g::clap_gui::softLabelAttrs();
    const float pk = p->outputPeak.exchange(
        p->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    const auto titleBand =
        s3g::gui_layout::mixerTitleBand(kMixerLayout.canvas);
    s3g::clap_gui::drawMixerTitleBand(
        ns(kWindowTitle), ns(p->presetName),
        s3g::clap_gui::peakDbText(pk), titleBand, style);

    NSRect fieldPanel =
        s3g::clap_gui::cocoaRect(kMixerLayout.fieldPanel);
    s3g::clap_gui::drawPanelFrame(
        fieldPanel.origin.x, fieldPanel.origin.y,
        fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(
        @"NODE FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y,
        fieldPanel.size.width, 21, text, style);
    [self drawViewButton:@"TOP" rect:NSMakeRect(372, 47, 44, 18) active:(_viewMode == 0) attrs:small style:style];
    [self drawViewButton:@"SIDE" rect:NSMakeRect(418, 47, 44, 18) active:(_viewMode == 1) attrs:small style:style];
    [self drawViewButton:@"3/4" rect:NSMakeRect(464, 47, 52, 18) active:(_viewMode == 2) attrs:small style:style];
    [self drawViewButton:@"-" rect:NSMakeRect(522, 47, 20, 18) active:NO attrs:small style:style];
    [self drawViewButton:@"+" rect:NSMakeRect(546, 47, 20, 18) active:NO attrs:small style:style];

#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    [[NSString stringWithFormat:@"%u NODES / 3OA / 8 x 16CH / 128CH",
        p->params.nodeCount]
        drawAtPoint:NSMakePoint(34, 610) withAttributes:small];
#else
    [[NSString stringWithFormat:@"%u NODES / %u OUT / 128CH",
        p->params.nodeCount, p->params.outputChannels]
        drawAtPoint:NSMakePoint(34, 610) withAttributes:small];
#endif

    NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x101010) setFill]; NSRectFill(field);
    [style.grid setStroke]; NSFrameRect(field);

    const CGFloat cx = field.origin.x + field.size.width * 0.5;
    const CGFloat cy = field.origin.y + field.size.height * 0.52;
    [s3g::clap_gui::color(0x777777, 0.22) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(field.origin.x + 22, cy) toPoint:NSMakePoint(NSMaxX(field) - 22, cy)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(cx, field.origin.y + 22) toPoint:NSMakePoint(cx, NSMaxY(field) - 22)];

    const NSPoint cursorPt = [self projectX:p->params.cursorX y:p->params.cursorY z:p->params.cursorZ rect:field];
    std::array<float, s3g::kNodeTrackMixerMaxNodes> weights {};
    for (uint32_t node = 0; node < weights.size(); ++node) {
        weights[node] = p->publishedNodeWeights[node].load(
            std::memory_order_relaxed);
    }

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

    s3g::clap_gui::drawPanelFrame(kMixerLayout.output, style);
    const auto outputFrame =
        s3g::clap_gui::cocoaRect(kMixerLayout.output.frame);
    s3g::clap_gui::drawPanelHeader(
        @"OUTPUT", true, outputFrame.origin.x, outputFrame.origin.y,
        outputFrame.size.width, 21, text, style);
    [self drawSlider:@"OUT"
        value:[NSString stringWithFormat:@"%+.1f dB",
            static_cast<double>(p->params.outputGainDb)]
        norm:(p->params.outputGainDb + 60.0) / 72.0
        y:s3g::gui_layout::rowY(kMixerLayout.output, 0u)
        attrs:small style:style];

    const auto& cursorPanel = kAmbi
        ? kMixerLayout.ambiBusCursor : kMixerLayout.busCursor;
    s3g::clap_gui::drawPanelFrame(cursorPanel, style);
    const auto cursorFrame =
        s3g::clap_gui::cocoaRect(cursorPanel.frame);
    s3g::clap_gui::drawPanelHeader(
        @"BUS / CURSOR", true, cursorFrame.origin.x, cursorFrame.origin.y,
        cursorFrame.size.width, 21, text, style);
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    [self drawSlider:@"NODES" value:[NSString stringWithFormat:@"%u", p->params.nodeCount] norm:(p->params.nodeCount - 1.0) / static_cast<double>(s3g::kAmbiNodeBusMixerMaxNodes - 1u) y:s3g::gui_layout::rowY(cursorPanel, 0u) attrs:small style:style];
    [self drawSlider:@"INFLUENCE" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.cursorInfluence * 100.0f)] norm:p->params.cursorInfluence y:s3g::gui_layout::rowY(cursorPanel, 1u) attrs:small style:style];
    [self drawSlider:@"CURSOR X" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorX)] norm:(p->params.cursorX + 2.0) * 0.25 y:s3g::gui_layout::rowY(cursorPanel, 2u) attrs:small style:style];
    [self drawSlider:@"CURSOR Y" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorY)] norm:(p->params.cursorY + 2.0) * 0.25 y:s3g::gui_layout::rowY(cursorPanel, 3u) attrs:small style:style];
    if (zLocked(*p)) {
        [self drawReadoutRow:@"CURSOR Z" value:@"LOCKED" y:s3g::gui_layout::rowY(cursorPanel, 4u) attrs:small style:style];
    } else {
        [self drawSlider:@"CURSOR Z" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorZ)] norm:(p->params.cursorZ + 2.0) * 0.25 y:s3g::gui_layout::rowY(cursorPanel, 4u) attrs:small style:style];
    }
    [self drawSlider:@"RADIUS" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.cursorRadius)] norm:(p->params.cursorRadius - 0.05) / 7.95 y:s3g::gui_layout::rowY(cursorPanel, 5u) attrs:small style:style];
    [self drawSlider:@"FOCUS" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.cursorFocus)] norm:(p->params.cursorFocus - 0.5) / 1.5 y:s3g::gui_layout::rowY(cursorPanel, 6u) attrs:small style:style];
    [self drawCheckRow:@"LOCK Z" checked:zLocked(*p) y:s3g::gui_layout::rowY(cursorPanel, 7u) attrs:small style:style];
#else
    [self drawMenuRow:@"BED" value:orderOrLayoutText(*p) y:s3g::gui_layout::rowY(cursorPanel, 0u) attrs:small style:style];
    [self drawReadoutRow:@"CHANNELS" value:[NSString stringWithFormat:@"%u", p->params.outputChannels] y:s3g::gui_layout::rowY(cursorPanel, 1u) attrs:small style:style];
    [self drawSlider:@"NODES" value:[NSString stringWithFormat:@"%u", p->params.nodeCount] norm:(p->params.nodeCount - 1.0) / 15.0 y:s3g::gui_layout::rowY(cursorPanel, 2u) attrs:small style:style];
    [self drawSlider:@"INFLUENCE" value:[NSString stringWithFormat:@"%.0f%%", static_cast<double>(p->params.cursorInfluence * 100.0f)] norm:p->params.cursorInfluence y:s3g::gui_layout::rowY(cursorPanel, 3u) attrs:small style:style];
    [self drawSlider:@"CURSOR X" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorX)] norm:(p->params.cursorX + 2.0) * 0.25 y:s3g::gui_layout::rowY(cursorPanel, 4u) attrs:small style:style];
    [self drawSlider:@"CURSOR Y" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorY)] norm:(p->params.cursorY + 2.0) * 0.25 y:s3g::gui_layout::rowY(cursorPanel, 5u) attrs:small style:style];
    if (zLocked(*p)) {
        [self drawReadoutRow:@"CURSOR Z" value:@"LOCKED" y:s3g::gui_layout::rowY(cursorPanel, 6u) attrs:small style:style];
    } else {
        [self drawSlider:@"CURSOR Z" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(p->params.cursorZ)] norm:(p->params.cursorZ + 2.0) * 0.25 y:s3g::gui_layout::rowY(cursorPanel, 6u) attrs:small style:style];
    }
    [self drawSlider:@"RADIUS" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.cursorRadius)] norm:(p->params.cursorRadius - 0.05) / 7.95 y:s3g::gui_layout::rowY(cursorPanel, 7u) attrs:small style:style];
    [self drawSlider:@"FOCUS" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.cursorFocus)] norm:(p->params.cursorFocus - 0.5) / 1.5 y:s3g::gui_layout::rowY(cursorPanel, 8u) attrs:small style:style];
    [self drawSlider:@"GATE" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(p->params.cursorGate)] norm:p->params.cursorGate / 0.95 y:s3g::gui_layout::rowY(cursorPanel, 9u) attrs:small style:style];
    [self drawCheckRow:@"LOCK Z" checked:zLocked(*p) y:s3g::gui_layout::rowY(cursorPanel, 10u) attrs:small style:style];
#endif

    const auto& nodePanel = kAmbi
        ? kMixerLayout.ambiSelectedNode : kMixerLayout.selectedNode;
    s3g::clap_gui::drawPanelFrame(nodePanel, style);
    const auto nodeFrame =
        s3g::clap_gui::cocoaRect(nodePanel.frame);
    s3g::clap_gui::drawPanelHeader(
        [NSString stringWithFormat:@"NODE %02d", _selectedNode + 1], true,
        nodeFrame.origin.x, nodeFrame.origin.y, nodeFrame.size.width, 21,
        text, style);
    const uint32_t node = static_cast<uint32_t>(std::clamp(_selectedNode, 0, static_cast<int>(p->params.nodeCount) - 1));
    [self drawCheckRow:@"ACTIVE" checked:nodeActive(*p, node) y:s3g::gui_layout::rowY(nodePanel, 0u) attrs:small style:style];
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
    [self drawMenuRow:@"SOURCE" value:ns(s3g::nodeTrackLayoutName(p->params.nodes[node].sourceLayout)) y:s3g::gui_layout::rowY(nodePanel, 1u) attrs:small style:style];
    const uint32_t busStart = p->params.nodes[node].inputStart;
    const uint32_t busEnd = std::min<uint32_t>(s3g::kNodeTrackMixerMaxChannels, busStart + p->params.nodes[node].sourceChannels - 1u);
    NSString* busText = busStart > s3g::kNodeTrackMixerMaxChannels
        ? @"OVER"
        : [NSString stringWithFormat:@"%03u-%03u", busStart, busEnd];
    [self drawReadoutRow:@"BUS" value:busText y:s3g::gui_layout::rowY(nodePanel, 2u) attrs:small style:style];
#else
    [self drawReadoutRow:@"BUS"
        value:[NSString stringWithFormat:@"%03u-%03u",
            node * s3g::kAmbiNodeBusMixerChannelsPerNode + 1u,
            (node + 1u) * s3g::kAmbiNodeBusMixerChannelsPerNode]
        y:s3g::gui_layout::rowY(nodePanel, 1u)
        attrs:small style:style];
#endif
    const uint32_t levelRow = kAmbi ? 2u : 3u;
    [self drawSlider:@"LEVEL" value:[NSString stringWithFormat:@"%+.1f dB", static_cast<double>(nodeLevelDb(*p, node))] norm:(nodeLevelDb(*p, node) + 60.0) / 72.0 y:s3g::gui_layout::rowY(nodePanel, levelRow) attrs:small style:style];
    [self drawSlider:@"X" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(nodeX(*p, node))] norm:(nodeX(*p, node) + 2.0) * 0.25 y:s3g::gui_layout::rowY(nodePanel, levelRow + 1u) attrs:small style:style];
    [self drawSlider:@"Y" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(nodeY(*p, node))] norm:(nodeY(*p, node) + 2.0) * 0.25 y:s3g::gui_layout::rowY(nodePanel, levelRow + 2u) attrs:small style:style];
    if (zLocked(*p)) {
        [self drawReadoutRow:@"Z" value:@"LOCKED" y:s3g::gui_layout::rowY(nodePanel, levelRow + 3u) attrs:small style:style];
    } else {
        [self drawSlider:@"Z" value:[NSString stringWithFormat:@"%+.2f", static_cast<double>(nodeZ(*p, node))] norm:(nodeZ(*p, node) + 2.0) * 0.25 y:s3g::gui_layout::rowY(nodePanel, levelRow + 3u) attrs:small style:style];
    }
#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    [self drawSlider:@"RADIUS" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeSizeOrRadius(*p, node))] norm:(nodeSizeOrRadius(*p, node) - 0.05) / 7.95 y:s3g::gui_layout::rowY(nodePanel, 6u) attrs:small style:style];
    [self drawSlider:@"FOCUS" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeFocus(*p, node))] norm:(nodeFocus(*p, node) - 0.5) / 3.5 y:s3g::gui_layout::rowY(nodePanel, 7u) attrs:small style:style];
#else
    [self drawSlider:@"SCALE" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeSizeOrRadius(*p, node))] norm:(nodeSizeOrRadius(*p, node) - 0.05) / 3.95 y:s3g::gui_layout::rowY(nodePanel, 7u) attrs:small style:style];
    [self drawSlider:@"FOCUS" value:[NSString stringWithFormat:@"%.2f", static_cast<double>(nodeFocus(*p, node))] norm:(nodeFocus(*p, node) - 0.5) / 3.5 y:s3g::gui_layout::rowY(nodePanel, 8u) attrs:small style:style];
    [self drawSlider:@"ROT AZ" value:[NSString stringWithFormat:@"%+.0f°", static_cast<double>(nodeRotateAz(*p, node))] norm:(nodeRotateAz(*p, node) + 180.0) / 360.0 y:s3g::gui_layout::rowY(nodePanel, 9u) attrs:small style:style];
    [self drawSlider:@"ROT EL" value:[NSString stringWithFormat:@"%+.0f°", static_cast<double>(nodeRotateEl(*p, node))] norm:(nodeRotateEl(*p, node) + 90.0) / 180.0 y:s3g::gui_layout::rowY(nodePanel, 10u) attrs:small style:style];
#endif

    const uint32_t meterNodes = kParamNodeLimit;
    const uint32_t columns = meterNodes > 8u ? 4u : 2u;
    const uint32_t rows = (meterNodes + columns - 1u) / columns;
    const CGFloat meterW = columns > 2u ? 120.0 : 252.0;
    const CGFloat meterGap = columns > 2u ? 8.0 : 16.0;
    const CGFloat nodeGainTitleY = 624.0;
    const CGFloat nodeGainRowsY = 647.0;
    const CGFloat peakBaseY = 674.0;
    [@"NODE GAIN / POST CURSOR" drawAtPoint:NSMakePoint(34, nodeGainTitleY) withAttributes:text];
    for (uint32_t nodeIndex = 0; nodeIndex < meterNodes; ++nodeIndex) {
        const uint32_t col = nodeIndex / rows;
        const uint32_t row = nodeIndex % rows;
        const CGFloat x = 34.0 + static_cast<CGFloat>(col) * (meterW + meterGap);
        const CGFloat y = nodeGainRowsY + static_cast<CGFloat>(row) * 20.0;
        const float w = nodeIndex < p->params.nodeCount
            ? std::clamp<float>(weights[nodeIndex], 0.0f, 1.0f)
            : 0.0f;
        [self drawWeightSlider:[NSString stringWithFormat:@"N%02u", nodeIndex + 1u] weight:w rect:NSMakeRect(x, y, meterW, 13) attrs:small style:style];
    }
    const CGFloat peakY = peakBaseY + static_cast<CGFloat>(rows) * 20.0;
    [@"PEAK METER / POST CURSOR" drawAtPoint:NSMakePoint(34, peakY) withAttributes:text];
    [self drawPeakMeter:@"OUT" peak:pk rect:NSMakeRect(34, peakY + 23.0, 520, 13) attrs:small style:style];
    for (uint32_t nodeIndex = 0; nodeIndex < meterNodes; ++nodeIndex) {
        const uint32_t col = nodeIndex / rows;
        const uint32_t row = nodeIndex % rows;
        const CGFloat x = 34.0 + static_cast<CGFloat>(col) * (meterW + meterGap);
        const CGFloat y = peakY + 46.0 + static_cast<CGFloat>(row) * 20.0;
        const float nodePk = nodeIndex < p->params.nodeCount
            ? p->nodePeaks[nodeIndex].load(std::memory_order_relaxed)
            : 0.0f;
        [self drawPeakMeter:[NSString stringWithFormat:@"N%02u", nodeIndex + 1u] peak:nodePk rect:NSMakeRect(x, y, meterW, 13) attrs:small style:style];
    }

    if (_openMenu >= 0) {
        if (_openMenu == 1) {
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
            s3g::clap_gui::drawDropdownMenu([self menuRect:1], 21.0, regularLayoutMenuItems(), s3g::kNodeTrackRegularLayoutCount, static_cast<int>(s3g::nodeTrackRegularLayoutIndex(p->params.outputLayout)), _hoverMenuIndex, text, style);
#endif
        } else if (_openMenu == 2) {
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
            s3g::clap_gui::drawDropdownMenu([self menuRect:2], 21.0, regularLayoutMenuItems(), s3g::kNodeTrackRegularLayoutCount, static_cast<int>(s3g::nodeTrackRegularLayoutIndex(p->params.nodes[node].sourceLayout)), _hoverMenuIndex, text, style);
#endif
        }
    }
}
- (void)updateSliderAtPoint:(NSPoint)pt
{
    const double norm = std::clamp(
        (pt.x - s3g::gui_layout::processorControlX(
            kMixerLayout.output.frame.x))
            / s3g::gui_layout::processorTrackWidth(
                kMixerLayout.output.frame.width),
        0.0, 1.0);
    const uint32_t node = static_cast<uint32_t>(std::clamp(_selectedNode, 0, static_cast<int>(static_cast<Plugin*>(_plugin)->params.nodeCount) - 1));
    switch (_dragSlider) {
    case 0: [self setParam:kParamOutputGain value:-60.0 + norm * 72.0]; break;
    case 1: [self setParam:kParamNodeCount value:1.0 + norm * static_cast<double>((kAmbi ? s3g::kAmbiNodeBusMixerMaxNodes : s3g::kNodeTrackMixerMaxNodes) - 1u)]; break;
    case 2: [self setParam:kParamCursorInfluence value:norm]; break;
    case 3: [self setParam:kParamCursorX value:-2.0 + norm * 4.0]; break;
    case 4: [self setParam:kParamCursorY value:-2.0 + norm * 4.0]; break;
    case 5: [self setParam:kParamCursorZ value:-2.0 + norm * 4.0]; break;
    case 6: [self setParam:kParamCursorRadius value:0.05 + norm * 7.95]; break;
    case 7: [self setParam:kParamCursorFocus value:0.5 + norm * 1.5]; break;
    case 8: [self setParam:kParamCursorGate value:norm * 0.95]; break;
    case 10: [self setParam:kParamNodeBase + node * kParamNodeStride + 1 value:-60.0 + norm * 72.0]; break;
    case 11: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 2 : 5) value:-2.0 + norm * 4.0]; break;
    case 12: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 3 : 6) value:-2.0 + norm * 4.0]; break;
    case 13: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 4 : 7) value:-2.0 + norm * 4.0]; break;
    case 14: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 5 : 8) value:kAmbi ? (0.05 + norm * 7.95) : (0.05 + norm * 3.95)]; break;
    case 15: [self setParam:kParamNodeBase + node * kParamNodeStride + (kAmbi ? 6 : 9) value:0.5 + norm * 3.5]; break;
    case 16:
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
        [self setParam:kParamNodeRotateAzBase + node value:-180.0 + norm * 360.0];
#endif
        break;
    case 17:
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
    const auto titleBand =
        s3g::gui_layout::mixerTitleBand(kMixerLayout.canvas);
    if (s3g::clap_gui::handleProcessorTitleClick(
            pt, &p->plugin, ns(kHostName), titleBand,
            p->presetName, sizeof(p->presetName), kParamOutputGain)) {
        _selectedNode = std::min<int>(
            _selectedNode, static_cast<int>(p->params.nodeCount) - 1);
        [self setNeedsDisplay:YES];
        return;
    }
    if (_openMenu >= 0) {
        const uint32_t count = s3g::kNodeTrackRegularLayoutCount;
        const int hit = s3g::clap_gui::dropdownHitIndex(pt, [self menuRect:_openMenu], 21.0, count);
        if (hit >= 0) {
#if !defined(S3G_AMBI_NODE_TRACK_MIXER)
            if (_openMenu == 1) {
                [self setParam:kParamLayoutOrOrder value:hit];
                [self setParam:kParamOutputChannels value:s3g::nodeTrackDefaultChannelsForLayout(s3g::nodeTrackRegularLayoutFromIndex(static_cast<uint32_t>(hit)))];
            }
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
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(
                kMixerLayout.busCursor, 0u)))) {
        _openMenu = 1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(
                kMixerLayout.selectedNode, 1u)))) {
        _openMenu = 2;
        [self setNeedsDisplay:YES];
        return;
    }
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
            [self beginGesture:kParamCursorX];
            [self beginGesture:kParamCursorY];
            [self beginGesture:kParamCursorZ];
            [self updateSpatialAtPoint:pt cursor:YES];
            return;
        }
        for (uint32_t node = 0; node < p->params.nodeCount; ++node) {
            const NSPoint nodePt = [self projectX:nodeX(*p, node) y:nodeY(*p, node) z:nodeZ(*p, node) rect:field];
            if (std::hypot(nodePt.x - pt.x, nodePt.y - pt.y) <= 14.0) {
                _selectedNode = static_cast<int>(node);
                _dragNode = YES;
                const clap_id base = kParamNodeBase
                    + node * kParamNodeStride;
                [self beginGesture:base + (kAmbi ? 2u : 5u)];
                [self beginGesture:base + (kAmbi ? 3u : 6u)];
                [self beginGesture:base + (kAmbi ? 4u : 7u)];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        return;
    }
    const auto& cursorPanel = kAmbi
        ? kMixerLayout.ambiBusCursor : kMixerLayout.busCursor;
    const auto& nodePanel = kAmbi
        ? kMixerLayout.ambiSelectedNode : kMixerLayout.selectedNode;
    const uint32_t node =
        static_cast<uint32_t>(std::max(0, _selectedNode));
    const clap_id nodeBase =
        kParamNodeBase + node * kParamNodeStride;
    auto beginSlider = [&](const s3g::gui_layout::Panel& panel,
                           uint32_t row, int slider,
                           clap_id param) -> bool {
        if (!NSPointInRect(pt, s3g::clap_gui::cocoaRect(
                s3g::gui_layout::sliderHitRect(panel, row)))) {
            return false;
        }
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, param, &defaultValue)) {
            [self setParam:param value:defaultValue];
            _dragSlider = -1;
        } else {
            _dragSlider = slider;
            [self beginGesture:param];
            [self updateSliderAtPoint:pt];
        }
        return true;
    };

    if (beginSlider(kMixerLayout.output, 0u, 0, kParamOutputGain))
        return;

    const uint32_t lockRow = kAmbi ? 7u : 10u;
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(cursorPanel, lockRow)))) {
        [self setParam:kParamLockZ value:zLocked(*p) ? 0.0 : 1.0];
        return;
    }
    if (NSPointInRect(pt, s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(nodePanel, 0u)))) {
        [self setParam:nodeBase value:nodeActive(*p, node) ? 0.0 : 1.0];
        return;
    }

#if defined(S3G_AMBI_NODE_TRACK_MIXER)
    struct SliderHit {
        uint32_t row;
        int slider;
        clap_id param;
    };
    const SliderHit cursorHits[] {
        { 0u, 1, kParamNodeCount },
        { 1u, 2, kParamCursorInfluence },
        { 2u, 3, kParamCursorX },
        { 3u, 4, kParamCursorY },
        { 4u, 5, kParamCursorZ },
        { 5u, 6, kParamCursorRadius },
        { 6u, 7, kParamCursorFocus },
    };
    for (const auto& hit : cursorHits) {
        if (hit.param == kParamCursorZ && zLocked(*p)) continue;
        if (beginSlider(cursorPanel, hit.row, hit.slider, hit.param))
            return;
    }
    const SliderHit nodeHits[] {
        { 2u, 10, nodeBase + 1u },
        { 3u, 11, nodeBase + 2u },
        { 4u, 12, nodeBase + 3u },
        { 5u, 13, nodeBase + 4u },
        { 6u, 14, nodeBase + 5u },
        { 7u, 15, nodeBase + 6u },
    };
#else
    struct SliderHit {
        uint32_t row;
        int slider;
        clap_id param;
    };
    const SliderHit cursorHits[] {
        { 2u, 1, kParamNodeCount },
        { 3u, 2, kParamCursorInfluence },
        { 4u, 3, kParamCursorX },
        { 5u, 4, kParamCursorY },
        { 6u, 5, kParamCursorZ },
        { 7u, 6, kParamCursorRadius },
        { 8u, 7, kParamCursorFocus },
        { 9u, 8, kParamCursorGate },
    };
    for (const auto& hit : cursorHits) {
        if (hit.param == kParamCursorZ && zLocked(*p)) continue;
        if (beginSlider(cursorPanel, hit.row, hit.slider, hit.param))
            return;
    }
    const SliderHit nodeHits[] {
        { 3u, 10, nodeBase + 1u },
        { 4u, 11, nodeBase + 5u },
        { 5u, 12, nodeBase + 6u },
        { 6u, 13, nodeBase + 7u },
        { 7u, 14, nodeBase + 8u },
        { 8u, 15, nodeBase + 9u },
        { 9u, 16, kParamNodeRotateAzBase + node },
        { 10u, 17, kParamNodeRotateElBase + node },
    };
#endif
    for (const auto& hit : nodeHits) {
        const clap_id zParam =
            nodeBase + (kAmbi ? 4u : 7u);
        if (hit.param == zParam && zLocked(*p)) continue;
        if (beginSlider(nodePanel, hit.row, hit.slider, hit.param))
            return;
    }
}
- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu < 0) return;
    NSPoint pt = [self convertPoint:[event locationInWindow] fromView:nil];
    const uint32_t count = s3g::kNodeTrackRegularLayoutCount;
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
- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    [self endGestures];
    _dragSlider = -1;
    _dragNode = NO;
    _dragCursor = NO;
    _dragView = NO;
}
@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) { return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0; }
bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) { if (!api || !isFloating) return false; *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true; }
bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating) { if (!guiIsApiSupported(plugin, api, isFloating)) return false; auto* p = self(plugin); if (p->guiView) return true; p->guiView = [[S3G_NODE_BUS_MIXER_VIEW_CLASS alloc] initWithPlugin:p]; if (!p->guiView) return false; if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport, static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) { [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false; } return true; }
void guiDestroy(const clap_plugin_t* plugin) { auto* p = self(plugin); if (p->guiView) { p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3G_NODE_BUS_MIXER_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView); } }
bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win) { if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false; auto* p = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport, static_cast<NSView*>(win->cocoa), p->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible.store(true, std::memory_order_relaxed); [static_cast<S3G_NODE_BUS_MIXER_VIEW_CLASS*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3G_NODE_BUS_MIXER_VIEW_CLASS*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
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
    kHostName,
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
    p->params = sanitizeParams(p->params);
    p->audioParams = p->params;
    const uint64_t initialStamp = nextPublicationStamp(*p);
    writeParamsToBank(p->controlParamBank, p->params, initialStamp);
    setAudioParams(*p, p->audioParams);
    p->audioConsumedControlStamp = initialStamp;
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
