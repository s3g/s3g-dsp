#include "s3g_fractional_waveguide_network.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
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
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = 1u;
constexpr uint32_t kOutputChannels = 16u;
constexpr uint32_t kNodeCount = 8u;
constexpr uint32_t kEdgeCount = 12u;
constexpr uint32_t kStateVersion = 1u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 680u;

constexpr clap_id kOrderParamId = 1u;
constexpr clap_id kSpeedParamId = 2u;
constexpr clap_id kDecayParamId = 3u;
constexpr clap_id kAbsorptionParamId = 4u;
constexpr clap_id kNonlinearityParamId = 5u;
constexpr clap_id kRadiationParamId = 6u;
constexpr clap_id kSizeParamId = 7u;
constexpr clap_id kActuatorNodeParamId = 8u;
constexpr clap_id kActuatorGainParamId = 9u;
constexpr clap_id kStrikeGainParamId = 10u;
constexpr clap_id kOutputGainParamId = 11u;
constexpr uint32_t kParamCount = 11u;

struct ParamSpec {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
    bool logarithmic;
};

constexpr std::array<ParamSpec, kParamCount> kParamSpecs {{
    { kOrderParamId, "Ambisonic Order", "Output", 1.0, 3.0, 3.0, true, false },
    { kSpeedParamId, "Propagation Speed", "Medium", 20.0, 2000.0, 343.0, false, true },
    { kDecayParamId, "Decay", "Medium", 0.05, 60.0, 2.5, false, true },
    { kAbsorptionParamId, "High Frequency Absorption", "Medium", 0.0, 1.0, 0.22, false, false },
    { kNonlinearityParamId, "Junction Nonlinearity", "Medium", 0.0, 1.0, 0.0, false, false },
    { kRadiationParamId, "Velocity Radiation", "Radiation", 0.0, 1.0, 0.20, false, false },
    { kSizeParamId, "Cube Half Extent", "Structure", 0.02, 4.0, 0.5, false, true },
    { kActuatorNodeParamId, "Actuator Node", "Excitation", 1.0, 8.0, 1.0, true, false },
    { kActuatorGainParamId, "Actuator Gain", "Excitation", 0.0, 2.0, 1.0, false, false },
    { kStrikeGainParamId, "Strike Gain", "Excitation", 0.0, 2.0, 1.0, false, false },
    { kOutputGainParamId, "Output Gain", "Output", -60.0, 12.0, -12.0, false, false },
}};

const ParamSpec* paramSpec(clap_id id)
{
    for (const auto& spec : kParamSpecs) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

uint32_t paramIndex(clap_id id)
{
    return id >= kOrderParamId && id <= kOutputGainParamId
        ? static_cast<uint32_t>(id - kOrderParamId) : kParamCount;
}

double clampParamValue(const ParamSpec& spec, double value)
{
    value = std::isfinite(value) ? value : spec.defaultValue;
    value = std::clamp(value, spec.minimum, spec.maximum);
    return spec.stepped ? std::round(value) : value;
}

double normalizedParamValue(const ParamSpec& spec, double value)
{
    value = clampParamValue(spec, value);
    if (spec.logarithmic && spec.minimum > 0.0) {
        return std::log(value / spec.minimum)
            / std::log(spec.maximum / spec.minimum);
    }
    return (value - spec.minimum)
        / std::max(1.0e-12, spec.maximum - spec.minimum);
}

double valueFromNormalized(const ParamSpec& spec, double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    const double value = spec.logarithmic && spec.minimum > 0.0
        ? spec.minimum * std::pow(
            spec.maximum / spec.minimum, normalized)
        : spec.minimum + normalized * (spec.maximum - spec.minimum);
    return clampParamValue(spec, value);
}

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t reserved = 0u;
    std::array<double, kParamCount> values {};
};

struct Strike {
    uint32_t time = 0u;
    uint32_t node = 0u;
    float amplitude = 0.0f;
};

struct StrikeBatch {
    static constexpr uint32_t kCapacity = 128u;
    std::array<Strike, kCapacity> strikes {};
    uint32_t count = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    bool active = false;

    s3g::FractionalWaveguideParams waveguideParams {};
    float sizeMetres = 0.5f;
    float actuatorGain = 1.0f;
    float strikeGain = 1.0f;
    uint32_t actuatorNode = 0u;
    s3g::FractionalWaveguideNetwork network;

    std::vector<float> actuatorScratch;
    std::array<std::vector<float>, kOutputChannels> outputScratch {};
    std::array<float*, kOutputChannels> outputPointers {};

    std::array<std::atomic<double>, kParamCount> publishedParams {};
    std::array<std::atomic<float>, kNodeCount> nodeEnergy {};
    std::array<std::atomic<float>, kEdgeCount> edgeEnergy {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> guardGain { 1.0f };
    std::atomic<int32_t> previewStrikeNode { -1 };
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};

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

double appliedParamValue(const Plugin& p, clap_id id)
{
    switch (id) {
    case kOrderParamId: return p.waveguideParams.order;
    case kSpeedParamId: return p.waveguideParams.propagationSpeed;
    case kDecayParamId: return p.waveguideParams.decaySeconds;
    case kAbsorptionParamId: return p.waveguideParams.absorption;
    case kNonlinearityParamId: return p.waveguideParams.junctionNonlinearity;
    case kRadiationParamId: return p.waveguideParams.radiation;
    case kSizeParamId: return p.sizeMetres;
    case kActuatorNodeParamId: return p.actuatorNode + 1u;
    case kActuatorGainParamId: return p.actuatorGain;
    case kStrikeGainParamId: return p.strikeGain;
    case kOutputGainParamId: return p.waveguideParams.outputGainDb;
    default: return 0.0;
    }
}

void publishParam(Plugin& p, clap_id id, double value)
{
    const uint32_t index = paramIndex(id);
    const auto* spec = paramSpec(id);
    if (index < kParamCount && spec) {
        p.publishedParams[index].store(
            clampParamValue(*spec, value), std::memory_order_release);
    }
}

void publishAppliedParam(Plugin& p, clap_id id)
{
    publishParam(p, id, appliedParamValue(p, id));
}

void publishAllParams(Plugin& p)
{
    for (const auto& spec : kParamSpecs) {
        publishAppliedParam(p, spec.id);
    }
}

double publishedParamValue(const Plugin& p, clap_id id)
{
    const uint32_t index = paramIndex(id);
    return index < kParamCount
        ? p.publishedParams[index].load(std::memory_order_acquire) : 0.0;
}

bool paramAffectsTail(clap_id id)
{
    return id == kSpeedParamId || id == kDecayParamId
        || id == kAbsorptionParamId || id == kSizeParamId;
}

void applyParam(Plugin& p, clap_id id, double requested)
{
    const auto* spec = paramSpec(id);
    if (!spec) return;
    const double value = clampParamValue(*spec, requested);
    const bool geometryChange = id == kSizeParamId
        && std::abs(value - p.sizeMetres) > 1.0e-7;
    switch (id) {
    case kOrderParamId:
        p.waveguideParams.order = static_cast<uint32_t>(value);
        break;
    case kSpeedParamId:
        p.waveguideParams.propagationSpeed = static_cast<float>(value);
        break;
    case kDecayParamId:
        p.waveguideParams.decaySeconds = static_cast<float>(value);
        break;
    case kAbsorptionParamId:
        p.waveguideParams.absorption = static_cast<float>(value);
        break;
    case kNonlinearityParamId:
        p.waveguideParams.junctionNonlinearity = static_cast<float>(value);
        break;
    case kRadiationParamId:
        p.waveguideParams.radiation = static_cast<float>(value);
        break;
    case kSizeParamId:
        p.sizeMetres = static_cast<float>(value);
        break;
    case kActuatorNodeParamId:
        p.actuatorNode = static_cast<uint32_t>(value - 1.0);
        break;
    case kActuatorGainParamId:
        p.actuatorGain = static_cast<float>(value);
        break;
    case kStrikeGainParamId:
        p.strikeGain = static_cast<float>(value);
        break;
    case kOutputGainParamId:
        p.waveguideParams.outputGainDb = static_cast<float>(value);
        break;
    default:
        return;
    }
    p.waveguideParams =
        s3g::sanitizeFractionalWaveguideParams(p.waveguideParams);
    if (p.active) {
        if (geometryChange) p.network.configureCube(p.sizeMetres);
        p.network.setParams(p.waveguideParams);
    }
    publishAppliedParam(p, id);
    if (paramAffectsTail(id) && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

void loadPublishedState(Plugin& p)
{
    for (const auto& spec : kParamSpecs) {
        applyParam(p, spec.id, publishedParamValue(p, spec.id));
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
    s3g::clap_gui::ParamEventKind kind,
    clap_id id, double value = 0.0)
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
    if (const auto* spec = paramSpec(id)) {
        value = clampParamValue(*spec, value);
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

void queuePreviewStrike(Plugin& p, uint32_t node)
{
    p.previewStrikeNode.store(
        static_cast<int32_t>(std::min<uint32_t>(node, kNodeCount - 1u)),
        std::memory_order_release);
    if (p.host && p.host->request_process) p.host->request_process(p.host);
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
    uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = std::max(1.0, sampleRate);
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->actuatorScratch.assign(p->maxFrames, 0.0f);
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel) {
        p->outputScratch[channel].assign(p->maxFrames, 0.0f);
        p->outputPointers[channel] = p->outputScratch[channel].data();
    }
    loadPublishedState(*p);
    p->network.configureCube(p->sizeMetres);
    p->network.setParams(p->waveguideParams);
    p->network.prepare(p->sampleRate, 0.5f);
    p->active = true;
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->active = false;
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->network.reset(); }

bool pushGuiParamEvent(const clap_output_events_t* out,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!out || !out->try_push) return true;
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
        return out->try_push(out, &event.header);
    }
    clap_event_param_gesture_t event {};
    event.header.size = sizeof(event);
    event.header.time = 0u;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type =
        pending.kind == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN
        : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return out->try_push(out, &event.header);
}

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* out)
{
    s3g::clap_gui::ParamEvent pending {};
    while (p.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(out, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(p, pending.paramId, pending.value);
        }
        p.guiParamEvents.pop();
    }
}

StrikeBatch readInputEvents(Plugin& p,
    const clap_input_events_t* inputEvents, uint32_t frames)
{
    StrikeBatch batch;
    if (!inputEvents) return batch;
    const uint32_t eventCount = inputEvents->size(inputEvents);
    for (uint32_t index = 0u; index < eventCount; ++index) {
        const auto* event = inputEvents->get(inputEvents, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param =
                reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(p, param->param_id, param->value);
            continue;
        }
        int32_t key = -1;
        float velocity = 0.0f;
        if (event->type == CLAP_EVENT_NOTE_ON) {
            const auto* note =
                reinterpret_cast<const clap_event_note_t*>(event);
            key = note->key;
            velocity = static_cast<float>(note->velocity);
        } else if (event->type == CLAP_EVENT_MIDI) {
            const auto* midi =
                reinterpret_cast<const clap_event_midi_t*>(event);
            const uint8_t status = midi->data[0] & 0xf0u;
            if (status == 0x90u && midi->data[2] != 0u) {
                key = midi->data[1] & 0x7fu;
                velocity = static_cast<float>(midi->data[2] & 0x7fu)
                    / 127.0f;
            }
        }
        if (key >= 0 && velocity > 0.0f
            && batch.count < StrikeBatch::kCapacity) {
            auto& strike = batch.strikes[batch.count++];
            strike.time = frames > 0u
                ? std::min<uint32_t>(event->time, frames - 1u) : 0u;
            strike.node = static_cast<uint32_t>(key) % kNodeCount;
            strike.amplitude = std::clamp(velocity, 0.0f, 1.0f)
                * p.strikeGain;
        }
    }
    const int32_t preview =
        p.previewStrikeNode.exchange(-1, std::memory_order_acq_rel);
    if (preview >= 0 && batch.count < StrikeBatch::kCapacity) {
        auto& strike = batch.strikes[batch.count++];
        strike.time = 0u;
        strike.node = static_cast<uint32_t>(preview);
        strike.amplitude = p.strikeGain;
    }
    std::stable_sort(batch.strikes.begin(),
        batch.strikes.begin() + batch.count,
        [](const Strike& a, const Strike& b) { return a.time < b.time; });
    return batch;
}

void readParamEvents(Plugin& p, const clap_input_events_t* inputEvents)
{
    if (!inputEvents) return;
    const uint32_t eventCount = inputEvents->size(inputEvents);
    for (uint32_t index = 0u; index < eventCount; ++index) {
        const auto* event = inputEvents->get(inputEvents, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) {
            continue;
        }
        const auto* param =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(p, param->param_id, param->value);
    }
}

float inputSample(const clap_audio_buffer_t* input, uint32_t frame)
{
    if (!input || input->channel_count == 0u) return 0.0f;
    if (input->data32 && input->data32[0]) return input->data32[0][frame];
    if (input->data64 && input->data64[0]) {
        return static_cast<float>(input->data64[0][frame]);
    }
    return 0.0f;
}

void publishMeters(Plugin& p)
{
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        p.nodeEnergy[node].store(
            p.network.nodeEnergy(node), std::memory_order_relaxed);
    }
    for (uint32_t edge = 0u; edge < kEdgeCount; ++edge) {
        p.edgeEnergy[edge].store(
            p.network.edgeEnergy(edge), std::memory_order_relaxed);
    }
    p.outputPeak.store(
        p.network.outputPeak(), std::memory_order_relaxed);
    p.guardGain.store(
        p.network.guardGain(), std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    if (!processData || processData->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    auto* p = self(plugin);
    serviceGuiParamEvents(*p, processData->out_events);
    if (processData->frames_count > p->maxFrames) return CLAP_PROCESS_ERROR;
    const uint32_t frames = processData->frames_count;
    auto& output = processData->audio_outputs[0];
    if ((!output.data32 && !output.data64) || output.channel_count == 0u) {
        return CLAP_PROCESS_ERROR;
    }
    const StrikeBatch events =
        readInputEvents(*p, processData->in_events, frames);
    const clap_audio_buffer_t* input =
        processData->audio_inputs_count > 0u
        ? &processData->audio_inputs[0] : nullptr;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        p->actuatorScratch[frame] =
            inputSample(input, frame) * p->actuatorGain;
    }
    uint32_t offset = 0u;
    uint32_t strikeIndex = 0u;
    while (offset < frames) {
        while (strikeIndex < events.count
            && events.strikes[strikeIndex].time <= offset) {
            const auto& strike = events.strikes[strikeIndex++];
            p->network.strike(strike.node, strike.amplitude);
        }
        uint32_t end = frames;
        if (strikeIndex < events.count) {
            end = std::min(end, events.strikes[strikeIndex].time);
        }
        if (end <= offset) continue;
        for (uint32_t channel = 0u;
            channel < kOutputChannels; ++channel) {
            p->outputPointers[channel] =
                p->outputScratch[channel].data() + offset;
        }
        p->network.process(
            p->actuatorScratch.data() + offset,
            p->outputPointers.data(),
            kOutputChannels,
            end - offset,
            p->actuatorNode);
        offset = end;
    }
    if (frames == 0u) {
        while (strikeIndex < events.count) {
            const auto& strike = events.strikes[strikeIndex++];
            p->network.strike(strike.node, strike.amplitude);
        }
    }

    const uint32_t copiedChannels =
        std::min<uint32_t>(output.channel_count, kOutputChannels);
    for (uint32_t channel = 0u; channel < copiedChannels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = p->outputScratch[channel][frame];
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][frame] = value;
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][frame] = static_cast<double>(value);
            }
        }
    }
    for (uint32_t channel = kOutputChannels;
        channel < output.channel_count; ++channel) {
        if (output.data32 && output.data32[channel]) {
            std::fill(output.data32[channel],
                output.data32[channel] + frames, 0.0f);
        }
        if (output.data64 && output.data64[channel]) {
            std::fill(output.data64[channel],
                output.data64[channel] + frames, 0.0);
        }
    }
    publishMeters(*p);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "Waveguide Actuator In"
                : "3OA ACN/SN3D Waveguide Field",
        sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_MONO : CLAP_PORT_AMBISONIC;
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

bool notePortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_note_port_info_t* info)
{
    if (!info || !isInput || index != 0u) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects =
        CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Waveguide Strike In",
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
    if (!info || index >= kParamSpecs.size()) return false;
    const auto& spec = kParamSpecs[index];
    *info = {};
    info->id = spec.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (spec.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, spec.name, sizeof(info->name) - 1u);
    std::strncpy(info->module, spec.module, sizeof(info->module) - 1u);
    info->min_value = spec.minimum;
    info->max_value = spec.maximum;
    info->default_value = spec.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin,
    clap_id id, double* value)
{
    if (!value || !paramSpec(id)) return false;
    *value = publishedParamValue(*self(plugin), id);
    return true;
}

bool formatParamValue(clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramSpec(id)) return false;
    switch (id) {
    case kOrderParamId: {
        const double order = std::round(value);
        std::snprintf(display, size, "%.0fOA / %.0fch",
            order, (order + 1.0) * (order + 1.0));
        break;
    }
    case kSpeedParamId:
        std::snprintf(display, size, "%.0f m/s", value);
        break;
    case kDecayParamId:
        std::snprintf(display, size,
            value < 1.0 ? "%.0f ms" : "%.2f s",
            value < 1.0 ? value * 1000.0 : value);
        break;
    case kSizeParamId:
        std::snprintf(display, size,
            value < 0.1 ? "%.1f cm" : "%.2f m",
            value < 0.1 ? value * 100.0 : value);
        break;
    case kActuatorNodeParamId:
        std::snprintf(display, size, "Node %.0f", value);
        break;
    case kActuatorGainParamId:
    case kStrikeGainParamId:
        std::snprintf(display, size, "%.2fx", value);
        break;
    case kOutputGainParamId:
        std::snprintf(display, size, "%+.1f dB", value);
        break;
    default:
        std::snprintf(display, size, "%.0f%%", value * 100.0);
        break;
    }
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id,
    double value, char* display, uint32_t size)
{
    return formatParamValue(id, value, display, size);
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    const auto* spec = paramSpec(id);
    if (!display || !value || !spec) return false;
    const char* numeric = display;
    while (*numeric != '\0'
        && !std::isdigit(static_cast<unsigned char>(*numeric))
        && *numeric != '+' && *numeric != '-' && *numeric != '.') {
        ++numeric;
    }
    char* end = nullptr;
    double parsed = std::strtod(numeric, &end);
    if (end == numeric || !std::isfinite(parsed)) return false;
    if (id == kAbsorptionParamId || id == kNonlinearityParamId
        || id == kRadiationParamId) {
        if (std::strchr(display, '%')) parsed *= 0.01;
    }
    if (id == kDecayParamId && std::strstr(display, "ms")) {
        parsed *= 0.001;
    }
    if (id == kSizeParamId && std::strstr(display, "cm")) {
        parsed *= 0.01;
    }
    *value = clampParamValue(*spec, parsed);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* in, const clap_output_events_t* out)
{
    auto* p = self(plugin);
    readParamEvents(*p, in);
    serviceGuiParamEvents(*p, out);
}

const clap_plugin_params_t paramsExt {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    SavedState state {};
    const auto* p = self(plugin);
    for (const auto& spec : kParamSpecs) {
        state.values[paramIndex(spec.id)] =
            publishedParamValue(*p, spec.id);
    }
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedState state {};
    if (!s3g::clap_state::readAll(stream, &state, sizeof(state))
        || state.version != kStateVersion) {
        return false;
    }
    auto* p = self(plugin);
    for (const auto& spec : kParamSpecs) {
        applyParam(*p, spec.id, state.values[paramIndex(spec.id)]);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const double seconds = std::min(
        90.0, std::max(0.05,
            static_cast<double>(p->waveguideParams.decaySeconds) * 1.15
            + 16.0 * (2.0 * p->sizeMetres)
                / p->waveguideParams.propagationSpeed));
    return static_cast<uint32_t>(std::ceil(seconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)

namespace {

NSRect fieldPanelRect()
{
    return NSMakeRect(16.0, 50.0, 560.0, 614.0);
}

NSRect outputPanelRect()
{
    return NSMakeRect(594.0, 50.0, 310.0, 116.0);
}

NSRect mediumPanelRect()
{
    return NSMakeRect(594.0, 180.0, 310.0, 230.0);
}

NSRect excitationPanelRect()
{
    return NSMakeRect(594.0, 424.0, 310.0, 240.0);
}

CGFloat rowY(NSRect panel, uint32_t row)
{
    return panel.origin.y + 42.0 + static_cast<CGFloat>(row) * 34.0;
}

NSRect rowHitRect(NSRect panel, uint32_t row)
{
    return NSMakeRect(
        panel.origin.x + 8.0, rowY(panel, row) - 9.0,
        panel.size.width - 16.0, 27.0);
}

NSPoint projectedNodePoint(uint32_t node)
{
    const s3g::Vec3 direction {
        (node & 1u) != 0u ? 1.0f : -1.0f,
        (node & 2u) != 0u ? 1.0f : -1.0f,
        (node & 4u) != 0u ? 1.0f : -1.0f,
    };
    const auto projection =
        s3g::projectAedDirection(direction, -35.0f, 28.0f);
    return NSMakePoint(
        fieldPanelRect().origin.x
            + fieldPanelRect().size.width * 0.50
            + projection.horizontal * 128.0,
        fieldPanelRect().origin.y
            + fieldPanelRect().size.height * 0.52
            - projection.vertical * 128.0);
}

uint32_t edgeFirst(uint32_t edge)
{
    constexpr std::array<uint32_t, kEdgeCount> first {{
        0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 4u, 4u, 5u, 6u
    }};
    return first[edge];
}

uint32_t edgeSecond(uint32_t edge)
{
    constexpr std::array<uint32_t, kEdgeCount> second {{
        1u, 2u, 4u, 3u, 5u, 3u, 6u, 7u, 5u, 6u, 7u, 7u
    }};
    return second[edge];
}

NSString* displayText(clap_id id, double value)
{
    char text[64] {};
    formatParamValue(id, value, text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

void drawControlRow(Plugin& p, clap_id id, NSString* shortName,
    NSRect panel, uint32_t row, bool menu,
    NSDictionary* labels, const s3g::clap_gui::Style& style)
{
    const auto* spec = paramSpec(id);
    if (!spec) return;
    const double value = publishedParamValue(p, id);
    const CGFloat y = rowY(panel, row);
    if (menu) {
        s3g::clap_gui::drawMenu(
            shortName, displayText(id, value), y, labels,
            s3g::clap_gui::softValueAttrs(), style,
            panel.origin.x + 14.0,
            panel.origin.x + 102.0,
            panel.size.width - 116.0);
    } else {
        s3g::clap_gui::drawSlider(
            shortName, displayText(id, value),
            static_cast<CGFloat>(normalizedParamValue(*spec, value)),
            y, labels, s3g::clap_gui::softValueAttrs(), style,
            panel.origin.x + 14.0,
            panel.origin.x + 102.0,
            panel.origin.x + panel.size.width - 64.0,
            panel.size.width - 178.0,
            52.0);
    }
}

} // namespace

@interface S3GAmbiEncoderMediumView : NSView {
    void* _plugin;
    clap_id _dragParam;
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)point;
@end

@implementation S3GAmbiEncoderMediumView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _timer = nil;
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
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
        target:self selector:@selector(refresh:)
        userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer
        forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSDictionary* title = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(0xd8d8d8), 12.0);

    [@"s3g AMBI ENCODER MEDIUM 16"
        drawAtPoint:NSMakePoint(18.0, 16.0)
        withAttributes:title];
    [@"FRACTIONAL-DELAY EXCITABLE CUBE"
        drawAtPoint:NSMakePoint(300.0, 18.0)
        withAttributes:values];
    const float peak = p->outputPeak.load(std::memory_order_relaxed);
    const float guard = p->guardGain.load(std::memory_order_relaxed);
    NSString* status = guard < 0.999f
        ? [NSString stringWithFormat:@"SAFE %+.1f dB",
            20.0f * std::log10(std::max(guard, 0.000001f))]
        : s3g::clap_gui::peakDbText(peak);
    s3g::clap_gui::drawRightStatus(
        status, kGuiWidth, 18.0, values, 18.0);

    const auto drawPanel = [&](NSString* name, NSRect panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.origin.x, panel.origin.y,
            panel.size.width, panel.size.height, style);
        s3g::clap_gui::drawPanelHeader(
            name, true, panel.origin.x, panel.origin.y,
            panel.size.width, 24.0, labels, style);
    };
    drawPanel(@"WAVEGUIDE FIELD", fieldPanelRect());
    drawPanel(@"OUTPUT", outputPanelRect());
    drawPanel(@"MEDIUM / STRUCTURE", mediumPanelRect());
    drawPanel(@"EXCITATION / RADIATION", excitationPanelRect());

    [@"CLICK A NODE TO SELECT + STRIKE"
        drawAtPoint:NSMakePoint(
            fieldPanelRect().origin.x + 16.0,
            fieldPanelRect().origin.y + 36.0)
        withAttributes:values];
    [@"CUBE EDGES ARE BIDIRECTIONAL THIRAN WAVEGUIDES"
        drawAtPoint:NSMakePoint(
            fieldPanelRect().origin.x + 16.0,
            fieldPanelRect().origin.y
                + fieldPanelRect().size.height - 27.0)
        withAttributes:values];

    for (uint32_t edge = 0u; edge < kEdgeCount; ++edge) {
        const float energy = std::clamp(
            p->edgeEnergy[edge].load(std::memory_order_relaxed)
                * 1.8f, 0.0f, 1.0f);
        const NSPoint first = projectedNodePoint(edgeFirst(edge));
        const NSPoint second = projectedNodePoint(edgeSecond(edge));
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:first];
        [path lineToPoint:second];
        [path setLineWidth:1.0 + 4.0 * std::sqrt(energy)];
        [[NSColor colorWithCalibratedWhite:
            0.28 + 0.62 * energy alpha:1.0] setStroke];
        [path stroke];
    }
    const uint32_t selected = static_cast<uint32_t>(
        std::clamp(std::lround(
            publishedParamValue(*p, kActuatorNodeParamId)),
            1l, 8l) - 1l);
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        const NSPoint point = projectedNodePoint(node);
        const float energy = std::clamp(
            p->nodeEnergy[node].load(std::memory_order_relaxed)
                * 1.5f, 0.0f, 1.0f);
        if (energy > 0.002f) {
            const CGFloat radius = 12.0 + 26.0 * std::sqrt(energy);
            NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(point.x - radius, point.y - radius,
                    radius * 2.0, radius * 2.0)];
            [[NSColor colorWithCalibratedRed:0.55
                green:0.72 blue:0.78 alpha:0.10 + 0.22 * energy] setFill];
            [halo fill];
        }
        const bool active = node == selected;
        const CGFloat radius = active ? 8.0 : 6.0;
        NSBezierPath* body = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(point.x - radius, point.y - radius,
                radius * 2.0, radius * 2.0)];
        [(active ? s3g::clap_gui::color(0xdadada)
                 : s3g::clap_gui::color(0x888888)) setFill];
        [body fill];
        [[NSString stringWithFormat:@"%u", node + 1u]
            drawAtPoint:NSMakePoint(point.x + 10.0, point.y - 7.0)
            withAttributes:values];
    }

    drawControlRow(*p, kOrderParamId, @"ORDER",
        outputPanelRect(), 0u, true, labels, style);
    drawControlRow(*p, kOutputGainParamId, @"OUT",
        outputPanelRect(), 1u, false, labels, style);
    drawControlRow(*p, kSpeedParamId, @"SPEED",
        mediumPanelRect(), 0u, false, labels, style);
    drawControlRow(*p, kSizeParamId, @"SIZE",
        mediumPanelRect(), 1u, false, labels, style);
    drawControlRow(*p, kDecayParamId, @"DECAY",
        mediumPanelRect(), 2u, false, labels, style);
    drawControlRow(*p, kAbsorptionParamId, @"ABSORB",
        mediumPanelRect(), 3u, false, labels, style);
    drawControlRow(*p, kNonlinearityParamId, @"NONLINEAR",
        mediumPanelRect(), 4u, false, labels, style);
    drawControlRow(*p, kActuatorNodeParamId, @"NODE",
        excitationPanelRect(), 0u, true, labels, style);
    drawControlRow(*p, kActuatorGainParamId, @"INPUT",
        excitationPanelRect(), 1u, false, labels, style);
    drawControlRow(*p, kStrikeGainParamId, @"STRIKE",
        excitationPanelRect(), 2u, false, labels, style);
    drawControlRow(*p, kRadiationParamId, @"RADIATE",
        excitationPanelRect(), 3u, false, labels, style);
}

- (void)updateSlider:(NSPoint)point
{
    if (_dragParam == CLAP_INVALID_ID) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const auto* spec = paramSpec(_dragParam);
    if (!spec) return;
    NSRect panel = outputPanelRect();
    if (_dragParam == kSpeedParamId || _dragParam == kSizeParamId
        || _dragParam == kDecayParamId
        || _dragParam == kAbsorptionParamId
        || _dragParam == kNonlinearityParamId) {
        panel = mediumPanelRect();
    } else if (_dragParam == kActuatorGainParamId
        || _dragParam == kStrikeGainParamId
        || _dragParam == kRadiationParamId) {
        panel = excitationPanelRect();
    }
    const double trackX = panel.origin.x + 102.0;
    const double trackWidth = panel.size.width - 178.0;
    const double normalized = std::clamp(
        (point.x - trackX) / trackWidth, 0.0, 1.0);
    queueGuiParamValue(
        *p, _dragParam, valueFromNormalized(*spec, normalized));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point =
        [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        const NSPoint center = projectedNodePoint(node);
        const double dx = point.x - center.x;
        const double dy = point.y - center.y;
        if (dx * dx + dy * dy <= 18.0 * 18.0) {
            queueGuiParamGestureBegin(*p, kActuatorNodeParamId);
            queueGuiParamValue(*p, kActuatorNodeParamId, node + 1.0);
            queueGuiParamGestureEnd(*p, kActuatorNodeParamId);
            queuePreviewStrike(*p, node);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    const auto cycleMenu = [&](clap_id id) {
        const auto* spec = paramSpec(id);
        if (!spec) return;
        double value = publishedParamValue(*p, id) + 1.0;
        if (value > spec->maximum) value = spec->minimum;
        queueGuiParamGestureBegin(*p, id);
        queueGuiParamValue(*p, id, value);
        queueGuiParamGestureEnd(*p, id);
        [self setNeedsDisplay:YES];
    };
    if (NSPointInRect(point, rowHitRect(outputPanelRect(), 0u))) {
        cycleMenu(kOrderParamId);
        return;
    }
    if (NSPointInRect(point, rowHitRect(excitationPanelRect(), 0u))) {
        cycleMenu(kActuatorNodeParamId);
        return;
    }

    struct HitRow { clap_id id; NSRect panel; uint32_t row; };
    const std::array<HitRow, 9u> rows {{
        { kOutputGainParamId, outputPanelRect(), 1u },
        { kSpeedParamId, mediumPanelRect(), 0u },
        { kSizeParamId, mediumPanelRect(), 1u },
        { kDecayParamId, mediumPanelRect(), 2u },
        { kAbsorptionParamId, mediumPanelRect(), 3u },
        { kNonlinearityParamId, mediumPanelRect(), 4u },
        { kActuatorGainParamId, excitationPanelRect(), 1u },
        { kStrikeGainParamId, excitationPanelRect(), 2u },
        { kRadiationParamId, excitationPanelRect(), 3u },
    }};
    for (const auto& row : rows) {
        if (!NSPointInRect(point, rowHitRect(row.panel, row.row))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGestureBegin(*p, row.id);
            queueGuiParamValue(*p, row.id, defaultValue);
            queueGuiParamGestureEnd(*p, row.id);
            _dragParam = CLAP_INVALID_ID;
        } else {
            _dragParam = row.id;
            queueGuiParamGestureBegin(*p, row.id);
            [self updateSlider:point];
        }
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam != CLAP_INVALID_ID) {
        [self updateSlider:
            [self convertPoint:[event locationInWindow] fromView:nil]];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam != CLAP_INVALID_ID) {
        queueGuiParamGestureEnd(
            *static_cast<Plugin*>(_plugin), _dragParam);
    }
    _dragParam = CLAP_INVALID_ID;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*,
    const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin,
    const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiEncoderMediumView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(
            p->guiViewport, static_cast<NSView*>(p->guiView),
            kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release];
        p->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    p->guiVisible = false;
    [static_cast<S3GAmbiEncoderMediumView*>(
        p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        p->guiViewport, p->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*,
    clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height);
}

bool guiSetSize(const clap_plugin_t* plugin,
    uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin,
    const clap_window_t* window)
{
    if (!window || !window->api
        || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) {
        return false;
    }
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        p->guiViewport, static_cast<NSView*>(window->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) {
        return false;
    }
    p->guiVisible = true;
    [static_cast<S3GAmbiEncoderMediumView*>(
        p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GAmbiEncoderMediumView*>(
        p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
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
    guiHide
};

} // namespace

#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
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
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-encoder-medium-16",
    "s3g Ambi Encoder Medium 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.5.0",
    "Excitable eight-node fractional-delay waveguide cube encoded directly to first through third-order ACN/SN3D ambisonics.",
    features
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) {
        return nullptr;
    }
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    for (const auto& spec : kParamSpecs) {
        applyParam(*p, spec.id, spec.defaultValue);
    }
    publishAllParams(*p);
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

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId
        && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
