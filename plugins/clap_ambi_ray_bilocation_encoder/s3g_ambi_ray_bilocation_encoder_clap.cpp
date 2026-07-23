#include "s3g_ambi_ray_bilocation_encoder.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#if defined(__APPLE__)
#include <clap/ext/gui.h>
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_ray_field_loader_macos.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = 1u;
constexpr uint32_t kOutputChannels = 64u;
constexpr uint32_t kGuiWidth = 1240u;
constexpr uint32_t kGuiHeight = 900u;
constexpr uint32_t kStateMagic = 0x5347424cu;
constexpr uint32_t kStateVersion = 2u;
constexpr const char* kPluginId = "org.s3g.s3g-dsp.ambi-ray-bilocation-encoder";
constexpr const char* kPluginName = "s3g Ambi Ray Bilocation Encoder";
constexpr const char* kPluginDesc = "One mono source simultaneously inhabiting two contrasting ambisonic ray fields.";

enum ParamId : clap_id {
    kParamOrder = 1,
    kParamSourceX,
    kParamSourceY,
    kParamSourceZ,
    kParamListenerX,
    kParamListenerY,
    kParamListenerZ,
    kParamPlace,
    kParamPermeability,
    kParamMemory,
    kParamSeparation,
    kParamMapMode,
    kParamDirect,
    kParamEarly,
    kParamLate,
    kParamSizeA,
    kParamSizeB,
    kParamScatterA,
    kParamScatterB,
    kParamWidthA,
    kParamWidthB,
    kParamAirA,
    kParamAirB,
    kParamMovement,
    kParamDoppler,
    kParamOutput,
    kParamBypass,
    kParamFieldListen,
};

struct PairPreset {
    const char* title;
    const char* nameA;
    const char* resourceA;
    const char* nameB;
    const char* resourceB;
    float permeability;
    float memory;
    float separation;
    s3g::AmbiRayBilocationMapMode mapMode;
    float sizeA;
    float sizeB;
    float scatterA;
    float scatterB;
    float widthA;
    float widthB;
    float airA;
    float airB;
};

constexpr std::array<PairPreset, 8> kPairPresets {{
    { "TIMBER / METAL", "WOOD STUDIO", "architecture_wood_studio", "METAL AXIAL TUNNEL", "echo_metal_axial_tunnel", 0.48f, 1.8f, 100.0f, s3g::AmbiRayBilocationMapMode::MirrorX, 0.82f, 1.28f, 0.22f, 0.76f, 0.86f, 1.22f, 0.10f, 0.42f },
    { "ICE / DRY STONE", "ICE GROTTO", "cave_ice_grotto", "DRY SLOT", "canyon_dry_slot", 0.72f, 3.2f, 120.0f, s3g::AmbiRayBilocationMapMode::MirrorY, 1.10f, 0.84f, 0.30f, 0.62f, 1.20f, 0.78f, 0.08f, 0.58f },
    { "OPEN / BURIED", "FOREST RING", "clearing_forest_ring", "DEEP STONE VAULT", "cavern_deep_stone_vault", 0.38f, 5.5f, 150.0f, s3g::AmbiRayBilocationMapMode::Counter, 0.72f, 1.45f, 0.78f, 0.26f, 1.30f, 0.86f, 0.48f, 0.14f },
    { "MEADOW / IMPOSSIBLE", "WATER MEADOW", "clearing_water_meadow", "IMPOSSIBLE RELAY", "echo_impossible_relay", 0.90f, 4.0f, 75.0f, s3g::AmbiRayBilocationMapMode::Linked, 0.78f, 1.32f, 0.64f, 0.88f, 1.16f, 1.30f, 0.34f, 0.18f },
    { "CONCRETE / WATER", "CONCRETE GALLERY", "architecture_concrete_gallery", "WATER CHAMBER", "cavern_water_chamber", 0.60f, 2.8f, 90.0f, s3g::AmbiRayBilocationMapMode::MirrorX, 0.92f, 1.18f, 0.18f, 0.74f, 0.80f, 1.24f, 0.28f, 0.08f },
    { "BRICK / FOLDED", "BRICK BEND", "tunnel_brick_bend", "FOLDED CHAMBER", "abstract_folded_chamber", 0.82f, 3.5f, 135.0f, s3g::AmbiRayBilocationMapMode::Counter, 1.08f, 0.76f, 0.34f, 0.84f, 0.72f, 1.32f, 0.38f, 0.16f },
    { "LIMESTONE / CONDUIT", "LIMESTONE POCKET", "cave_limestone_pocket", "METAL CONDUIT", "tunnel_metal_conduit", 0.44f, 1.4f, 110.0f, s3g::AmbiRayBilocationMapMode::MirrorY, 1.20f, 0.66f, 0.72f, 0.16f, 1.18f, 0.68f, 0.22f, 0.52f },
    { "FLUTTER / POROUS", "STONE FLUTTER", "echo_stone_flutter_gallery", "POROUS GORGE", "canyon_porous_gorge", 0.96f, 4.8f, 60.0f, s3g::AmbiRayBilocationMapMode::Linked, 0.88f, 1.36f, 0.12f, 0.92f, 0.82f, 1.38f, 0.12f, 0.66f },
}};

struct FieldState {
    s3g::AmbiRayDescriptor descriptor = s3g::makeDefaultAmbiRayDescriptor();
#if defined(__APPLE__)
    s3g::ray_field_loader::VisualGeometry visual {};
#endif
    std::string json;
    std::string name = "BUILT-IN ROOM";
};

struct SavedAmbiRayBilocationParamsV1 {
    uint32_t order = 3u;
    float sourceX = 0.5f;
    float sourceY = 0.25f;
    float sourceZ = 0.5f;
    float listenerX = 0.5f;
    float listenerY = 0.5f;
    float listenerZ = 0.5f;
    float place = 0.5f;
    float permeability = 0.65f;
    float memorySeconds = 2.0f;
    float separationDeg = 90.0f;
    s3g::AmbiRayBilocationMapMode mapMode =
        s3g::AmbiRayBilocationMapMode::Linked;
    float direct = 1.0f;
    float early = 0.72f;
    float late = 0.42f;
    float sizeA = 0.90f;
    float sizeB = 1.20f;
    float scatterA = 0.30f;
    float scatterB = 0.70f;
    float widthA = 0.90f;
    float widthB = 1.15f;
    float airA = 0.12f;
    float airB = 0.48f;
    float movementMs = 60.0f;
    float doppler = 0.50f;
    float outputGainDb = -9.0f;
    bool bypassRoom = false;
};

static_assert(sizeof(SavedAmbiRayBilocationParamsV1) == 108u,
    "Ambi Ray Bilocation v1 compatibility requires the previous parameter layout");

s3g::AmbiRayBilocationParams paramsFromV1(
    const SavedAmbiRayBilocationParamsV1& saved)
{
    s3g::AmbiRayBilocationParams params;
    params.order = saved.order;
    params.sourceX = saved.sourceX;
    params.sourceY = saved.sourceY;
    params.sourceZ = saved.sourceZ;
    params.listenerX = saved.listenerX;
    params.listenerY = saved.listenerY;
    params.listenerZ = saved.listenerZ;
    params.place = saved.place;
    params.permeability = saved.permeability;
    params.memorySeconds = saved.memorySeconds;
    params.separationDeg = saved.separationDeg;
    params.mapMode = saved.mapMode;
    params.direct = saved.direct;
    params.early = saved.early;
    params.late = saved.late;
    params.sizeA = saved.sizeA;
    params.sizeB = saved.sizeB;
    params.scatterA = saved.scatterA;
    params.scatterB = saved.scatterB;
    params.widthA = saved.widthA;
    params.widthB = saved.widthB;
    params.airA = saved.airA;
    params.airB = saved.airB;
    params.movementMs = saved.movementMs;
    params.doppler = saved.doppler;
    params.outputGainDb = saved.outputGainDb;
    params.bypassRoom = saved.bypassRoom;
    params.fieldListenMode = s3g::AmbiFieldListenMode::Off;
    return params;
}

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    s3g::AmbiRayBilocationParams params {};
    uint32_t jsonBytesA = 0u;
    uint32_t jsonBytesB = 0u;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    s3g::AmbiRayBilocationParams params {};
    FieldState fieldA {};
    FieldState fieldB {};
    std::string pairName = "BUILT-IN TWINS";
    std::string status = "READY";
    int selectedPair = -1;
    std::mutex stateMutex;
    std::vector<std::unique_ptr<s3g::AmbiRayBilocationEncoder>> runtimes;
    std::atomic<s3g::AmbiRayBilocationEncoder*> activeProcessor { nullptr };
    std::atomic<bool> active { false };
    std::atomic<float> outputPeak { 0.0f };
    double sampleRate = 48000.0;
    uint32_t maximumFrames = 1024u;
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

bool streamWriteAll(const clap_ostream_t* stream, const void* source, uint64_t bytes)
{
    const auto* data = static_cast<const uint8_t*>(source);
    uint64_t position = 0u;
    while (position < bytes) {
        const int64_t wrote = stream->write(stream, data + position, bytes - position);
        if (wrote <= 0) return false;
        position += static_cast<uint64_t>(wrote);
    }
    return true;
}

bool streamReadAll(const clap_istream_t* stream, void* destination, uint64_t bytes)
{
    auto* data = static_cast<uint8_t*>(destination);
    uint64_t position = 0u;
    while (position < bytes) {
        const int64_t got = stream->read(stream, data + position, bytes - position);
        if (got <= 0) return false;
        position += static_cast<uint64_t>(got);
    }
    return true;
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    auto& p = plugin.params;
    switch (id) {
    case kParamOrder: p.order = static_cast<uint32_t>(std::lround(value)); break;
    case kParamSourceX: p.sourceX = static_cast<float>(value); break;
    case kParamSourceY: p.sourceY = static_cast<float>(value); break;
    case kParamSourceZ: p.sourceZ = static_cast<float>(value); break;
    case kParamListenerX: p.listenerX = static_cast<float>(value); break;
    case kParamListenerY: p.listenerY = static_cast<float>(value); break;
    case kParamListenerZ: p.listenerZ = static_cast<float>(value); break;
    case kParamPlace: p.place = static_cast<float>(value); break;
    case kParamPermeability: p.permeability = static_cast<float>(value); break;
    case kParamMemory: p.memorySeconds = static_cast<float>(value); break;
    case kParamSeparation: p.separationDeg = static_cast<float>(value); break;
    case kParamMapMode: p.mapMode = static_cast<s3g::AmbiRayBilocationMapMode>(static_cast<uint32_t>(std::lround(value))); break;
    case kParamDirect: p.direct = static_cast<float>(value); break;
    case kParamEarly: p.early = static_cast<float>(value); break;
    case kParamLate: p.late = static_cast<float>(value); break;
    case kParamSizeA: p.sizeA = static_cast<float>(value); break;
    case kParamSizeB: p.sizeB = static_cast<float>(value); break;
    case kParamScatterA: p.scatterA = static_cast<float>(value); break;
    case kParamScatterB: p.scatterB = static_cast<float>(value); break;
    case kParamWidthA: p.widthA = static_cast<float>(value); break;
    case kParamWidthB: p.widthB = static_cast<float>(value); break;
    case kParamAirA: p.airA = static_cast<float>(value); break;
    case kParamAirB: p.airB = static_cast<float>(value); break;
    case kParamMovement: p.movementMs = static_cast<float>(value); break;
    case kParamDoppler: p.doppler = static_cast<float>(value); break;
    case kParamOutput: p.outputGainDb = static_cast<float>(value); break;
    case kParamBypass: p.bypassRoom = value >= 0.5; break;
    case kParamFieldListen:
        p.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
            static_cast<uint32_t>(std::lround(value)));
        break;
    default: return;
    }
    p = s3g::sanitizeAmbiRayBilocationParams(p);
    if (auto* processor = plugin.activeProcessor.load(std::memory_order_acquire)) processor->setParams(p);
}

double getParam(const Plugin& plugin, clap_id id)
{
    const auto& p = plugin.params;
    switch (id) {
    case kParamOrder: return p.order;
    case kParamSourceX: return p.sourceX;
    case kParamSourceY: return p.sourceY;
    case kParamSourceZ: return p.sourceZ;
    case kParamListenerX: return p.listenerX;
    case kParamListenerY: return p.listenerY;
    case kParamListenerZ: return p.listenerZ;
    case kParamPlace: return p.place;
    case kParamPermeability: return p.permeability;
    case kParamMemory: return p.memorySeconds;
    case kParamSeparation: return p.separationDeg;
    case kParamMapMode: return static_cast<uint32_t>(p.mapMode);
    case kParamDirect: return p.direct;
    case kParamEarly: return p.early;
    case kParamLate: return p.late;
    case kParamSizeA: return p.sizeA;
    case kParamSizeB: return p.sizeB;
    case kParamScatterA: return p.scatterA;
    case kParamScatterB: return p.scatterB;
    case kParamWidthA: return p.widthA;
    case kParamWidthB: return p.widthB;
    case kParamAirA: return p.airA;
    case kParamAirB: return p.airB;
    case kParamMovement: return p.movementMs;
    case kParamDoppler: return p.doppler;
    case kParamOutput: return p.outputGainDb;
    case kParamBypass: return p.bypassRoom ? 1.0 : 0.0;
    case kParamFieldListen: return static_cast<uint32_t>(p.fieldListenMode);
    default: return 0.0;
    }
}

bool buildRuntime(Plugin& plugin,
                  const s3g::AmbiRayDescriptor& descriptorA,
                  const s3g::AmbiRayDescriptor& descriptorB,
                  std::string& error)
{
    auto runtime = std::make_unique<s3g::AmbiRayBilocationEncoder>();
    runtime->setParams(plugin.params);
    if (!runtime->prepare(plugin.sampleRate, plugin.maximumFrames, descriptorA, descriptorB)) {
        error = "BILOCATION FIELD BUILD FAILED";
        return false;
    }
    auto* next = runtime.get();
    plugin.runtimes.push_back(std::move(runtime));
    plugin.activeProcessor.store(next, std::memory_order_release);
    return true;
}

bool installFields(Plugin& plugin,
                   FieldState fieldA,
                   FieldState fieldB,
                   std::string pairName,
                   int selectedPair,
                   std::string& error)
{
    if (fieldA.descriptor.cells.empty() || fieldB.descriptor.cells.empty()) {
        error = "BOTH RAY FIELDS REQUIRE CELLS";
        return false;
    }
    if (plugin.active.load(std::memory_order_acquire)
        && !buildRuntime(plugin, fieldA.descriptor, fieldB.descriptor, error)) return false;
    {
        std::lock_guard<std::mutex> lock(plugin.stateMutex);
        plugin.fieldA = std::move(fieldA);
        plugin.fieldB = std::move(fieldB);
        plugin.pairName = std::move(pairName);
        plugin.selectedPair = selectedPair;
        plugin.status = "READY";
    }
    if (plugin.hostTail && plugin.host) plugin.hostTail->changed(plugin.host);
    return true;
}

bool init(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->hostTail = instance->host && instance->host->get_extension
        ? static_cast<const clap_host_tail_t*>(instance->host->get_extension(instance->host, CLAP_EXT_TAIL))
        : nullptr;
    return true;
}

void destroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
#if defined(__APPLE__)
    if (instance->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(instance->guiViewport, instance->guiView);
    }
#endif
    delete instance;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t maximumFrames)
{
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    // Some hosts and validators advertise very small scheduling quanta while
    // still exercising larger buffers. Keep enough preallocated workspace to
    // avoid repeatedly entering both room engines for tiny internal chunks.
    instance->maximumFrames = std::max<uint32_t>(4096u, maximumFrames);
    instance->runtimes.clear();
    instance->activeProcessor.store(nullptr, std::memory_order_release);
    s3g::AmbiRayDescriptor descriptorA;
    s3g::AmbiRayDescriptor descriptorB;
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        descriptorA = instance->fieldA.descriptor;
        descriptorB = instance->fieldB.descriptor;
    }
    std::string error;
    if (!buildRuntime(*instance, descriptorA, descriptorB, error)) return false;
    instance->active.store(true, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    instance->active.store(false, std::memory_order_release);
    instance->activeProcessor.store(nullptr, std::memory_order_release);
    instance->runtimes.clear();
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) processor->reset();
    instance->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* inputEvents)
{
    if (!inputEvents) return;
    const uint32_t count = inputEvents->size(inputEvents);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = inputEvents->get(inputEvents, index);
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, value->param_id, value->value);
        }
    }
}

template <typename Sample>
clap_process_status processTyped(Plugin& plugin,
                                 const clap_audio_buffer_t& input,
                                 const clap_audio_buffer_t& output,
                                 uint32_t frames,
                                 Sample** inputData,
                                 Sample** outputData)
{
    s3g::clearAudioBuffer(output, frames);
    auto* processor = plugin.activeProcessor.load(std::memory_order_acquire);
    if (!processor || !outputData) return CLAP_PROCESS_CONTINUE;
    processor->setParams(plugin.params);
    const Sample* mono = inputData && input.channel_count > 0u ? inputData[0] : nullptr;
    processor->process(mono, outputData, output.channel_count, frames);
    float peak = 0.0f;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (!outputData[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame)
            peak = std::max(peak, static_cast<float>(std::abs(outputData[channel][frame])));
    }
    const float previous = plugin.outputPeak.load(std::memory_order_relaxed);
    plugin.outputPeak.store(std::max(previous * 0.90f, peak), std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* processData)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, processData->in_events);
    if (processData->audio_inputs_count == 0u || processData->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    const auto& input = processData->audio_inputs[0];
    const auto& output = processData->audio_outputs[0];
    if (input.data32 && output.data32) return processTyped<float>(*instance, input, output, processData->frames_count, input.data32, output.data32);
    if (input.data64 && output.data64) return processTyped<double>(*instance, input, output, processData->frames_count, input.data64, output.data64);
    s3g::clearAudioBuffer(output, processData->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index != 0u || !info) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name, isInput ? "Bilocation Source In" : "Ambisonic Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_MONO : CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return 28u; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info) return false;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    auto set = [&](clap_id id, const char* name, const char* module, double minimum, double maximum, double defaultValue, bool stepped = false) {
        info->id = id;
        if (stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, name, sizeof(info->name));
        std::strncpy(info->module, module, sizeof(info->module));
        info->min_value = minimum;
        info->max_value = maximum;
        info->default_value = defaultValue;
        return true;
    };
    switch (index) {
    case 0: return set(kParamOrder, "Order", "Output", 1, 7, 3, true);
    case 1: return set(kParamSourceX, "Source X", "Position", 0, 1, 0.5);
    case 2: return set(kParamSourceY, "Source Y", "Position", 0, 1, 0.25);
    case 3: return set(kParamSourceZ, "Source Z", "Position", 0, 1, 0.5);
    case 4: return set(kParamListenerX, "Listener X", "Position", 0, 1, 0.5);
    case 5: return set(kParamListenerY, "Listener Y", "Position", 0, 1, 0.5);
    case 6: return set(kParamListenerZ, "Listener Z", "Position", 0, 1, 0.5);
    case 7: return set(kParamPlace, "Place", "Bilocation", 0, 1, 0.5);
    case 8: return set(kParamPermeability, "Permeability", "Bilocation", 0, 1, 0.65);
    case 9: return set(kParamMemory, "Memory", "Bilocation", 0, 12, 2);
    case 10: return set(kParamSeparation, "Separation", "Bilocation", 0, 180, 90);
    case 11: return set(kParamMapMode, "Position map", "Bilocation", 0, 3, 0, true);
    case 12: return set(kParamDirect, "Direct", "Room", 0, 1.5, 1);
    case 13: return set(kParamEarly, "Early", "Room", 0, 1.5, 0.72);
    case 14: return set(kParamLate, "Late", "Room", 0, 1.5, 0.42);
    case 15: return set(kParamSizeA, "Size A", "Space A", 0.5, 2, 0.9);
    case 16: return set(kParamSizeB, "Size B", "Space B", 0.5, 2, 1.2);
    case 17: return set(kParamScatterA, "Scatter A", "Space A", 0, 1, 0.3);
    case 18: return set(kParamScatterB, "Scatter B", "Space B", 0, 1, 0.7);
    case 19: return set(kParamWidthA, "Width A", "Space A", 0, 1.5, 0.9);
    case 20: return set(kParamWidthB, "Width B", "Space B", 0, 1.5, 1.15);
    case 21: return set(kParamAirA, "Air A", "Space A", 0, 1, 0.12);
    case 22: return set(kParamAirB, "Air B", "Space B", 0, 1, 0.48);
    case 23: return set(kParamMovement, "Movement smoothing", "Motion", 10, 500, 60);
    case 24: return set(kParamDoppler, "Doppler", "Motion", 0, 2, 0.5);
    case 25: return set(kParamOutput, "Output gain", "Output", -60, 12, -9);
    case 26:
        info->flags |= CLAP_PARAM_IS_BYPASS;
        return set(kParamBypass, "Bypass room", "Output", 0, 1, 0, true);
    case 27: return set(kParamFieldListen, "Field listen", "Room", 0, 3, 0, true);
    default: return false;
    }
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || id < kParamOrder || id > kParamFieldListen) return false;
    *value = getParam(*self(plugin), id);
    return true;
}

const char* mapModeName(uint32_t mode)
{
    constexpr const char* names[] { "Linked", "Mirror X", "Mirror Y", "Counter" };
    return names[std::min<uint32_t>(mode, 3u)];
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    switch (id) {
    case kParamOrder: std::snprintf(display, size, "%.0fOA", value); return true;
    case kParamSourceX: case kParamSourceY: case kParamSourceZ:
    case kParamListenerX: case kParamListenerY: case kParamListenerZ:
    case kParamPlace: case kParamPermeability: case kParamScatterA: case kParamScatterB:
    case kParamAirA: case kParamAirB: std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    case kParamMemory: std::snprintf(display, size, "%.2f s", value); return true;
    case kParamSeparation: std::snprintf(display, size, "%.0f deg", value); return true;
    case kParamMapMode: std::snprintf(display, size, "%s", mapModeName(static_cast<uint32_t>(std::lround(value)))); return true;
    case kParamDirect: case kParamEarly: case kParamLate: case kParamDoppler:
        std::snprintf(display, size, "%.0f%%", value * 100.0); return true;
    case kParamSizeA: case kParamSizeB: case kParamWidthA: case kParamWidthB:
        std::snprintf(display, size, "%.2f", value); return true;
    case kParamMovement: std::snprintf(display, size, "%.0f ms", value); return true;
    case kParamOutput: std::snprintf(display, size, "%+.1f dB", value); return true;
    case kParamBypass: std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off"); return true;
    case kParamFieldListen: {
        static constexpr const char* names[] { "Off", "Follow", "Counter", "Balance" };
        const uint32_t index = std::clamp<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 0u, 3u);
        std::snprintf(display, size, "%s", names[index]);
        return true;
    }
    default: return false;
    }
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value || id < kParamOrder || id > kParamFieldListen) return false;
    if (id == kParamMapMode) {
        for (uint32_t mode = 0u; mode < 4u; ++mode) {
            if (std::strstr(display, mapModeName(mode))) { *value = mode; return true; }
        }
    }
    const double parsed = std::atof(display);
    switch (id) {
    case kParamSourceX: case kParamSourceY: case kParamSourceZ:
    case kParamListenerX: case kParamListenerY: case kParamListenerZ:
    case kParamPlace: case kParamPermeability: case kParamScatterA: case kParamScatterB:
    case kParamAirA: case kParamAirB: case kParamDirect: case kParamEarly: case kParamLate:
    case kParamDoppler: *value = parsed * 0.01; break;
    case kParamBypass:
        *value = (display[0] == 'O' || display[0] == 'o')
            && (display[1] == 'N' || display[1] == 'n') ? 1.0 : 0.0;
        break;
    case kParamFieldListen:
        if (display[0] == 'F' || display[0] == 'f') *value = 1.0;
        else if (display[0] == 'C' || display[0] == 'c') *value = 2.0;
        else if (display[0] == 'B' || display[0] == 'b') *value = 3.0;
        else *value = std::atof(display);
        break;
    default: *value = parsed; break;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    readParamEvents(*self(plugin), input);
}

const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* instance = self(plugin);
    std::string jsonA;
    std::string jsonB;
    {
        std::lock_guard<std::mutex> lock(instance->stateMutex);
        jsonA = instance->fieldA.json;
        jsonB = instance->fieldB.json;
    }
#if defined(__APPLE__)
    if (jsonA.size() > s3g::ray_field_loader::kMaximumJsonBytes
        || jsonB.size() > s3g::ray_field_loader::kMaximumJsonBytes) return false;
#endif
    const SavedStateHeader header { kStateMagic, kStateVersion, instance->params,
        static_cast<uint32_t>(jsonA.size()), static_cast<uint32_t>(jsonB.size()) };
    return streamWriteAll(stream, &header, sizeof(header))
        && (jsonA.empty() || streamWriteAll(stream, jsonA.data(), jsonA.size()))
        && (jsonB.empty() || streamWriteAll(stream, jsonB.data(), jsonB.size()));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t magic = 0u;
    uint32_t version = 0u;
    if (!streamReadAll(stream, &magic, sizeof(magic))
        || !streamReadAll(stream, &version, sizeof(version))
        || magic != kStateMagic) return false;
    s3g::AmbiRayBilocationParams loadedParams;
    uint32_t jsonBytesA = 0u;
    uint32_t jsonBytesB = 0u;
    if (version == 1u) {
        SavedAmbiRayBilocationParamsV1 saved;
        if (!streamReadAll(stream, &saved, sizeof(saved))) return false;
        loadedParams = paramsFromV1(saved);
    } else if (version == kStateVersion) {
        if (!streamReadAll(stream, &loadedParams, sizeof(loadedParams))) return false;
    } else {
        return false;
    }
    if (!streamReadAll(stream, &jsonBytesA, sizeof(jsonBytesA))
        || !streamReadAll(stream, &jsonBytesB, sizeof(jsonBytesB))) return false;
#if defined(__APPLE__)
    if (jsonBytesA > s3g::ray_field_loader::kMaximumJsonBytes
        || jsonBytesB > s3g::ray_field_loader::kMaximumJsonBytes) return false;
#endif
    auto* instance = self(plugin);
    instance->params = s3g::sanitizeAmbiRayBilocationParams(loadedParams);
    FieldState fieldA;
    FieldState fieldB;
    auto readField = [&](uint32_t bytes, FieldState& field, const char* name) {
        if (bytes == 0u) return true;
        std::string json(bytes, '\0');
        if (!streamReadAll(stream, json.data(), json.size())) return false;
#if defined(__APPLE__)
        NSData* data = [NSData dataWithBytes:json.data() length:json.size()];
        std::string error;
        if (!s3g::ray_field_loader::parse(data, field.descriptor, field.visual, field.json, error)) return false;
        field.name = name;
        return true;
#else
        (void)field; (void)name;
        return false;
#endif
    };
    if (!readField(jsonBytesA, fieldA, "PROJECT A")
        || !readField(jsonBytesB, fieldB, "PROJECT B")) return false;
    if (jsonBytesA == 0u) fieldA = instance->fieldA;
    if (jsonBytesB == 0u) fieldB = instance->fieldB;
    std::string error;
    if (!installFields(*instance, std::move(fieldA), std::move(fieldB), "PROJECT PAIR", -1, error)) return false;
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) processor->setParams(instance->params);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->params.bypassRoom) return 0u;
    if (auto* processor = instance->activeProcessor.load(std::memory_order_acquire)) return processor->tailFrames();
    return 0u;
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

// GUI implementation is below so the audio/state core also builds on non-macOS hosts.

#if defined(__APPLE__)
namespace {

struct GuiFieldSnapshot {
    s3g::AmbiRayRoom room {};
    s3g::ray_field_loader::VisualGeometry visual {};
    std::string name;
    std::vector<s3g::Vec3> cells;
    std::vector<s3g::AmbiRayReflection> reflections;
    s3g::Vec3 source {};
    s3g::Vec3 listener {};
};

struct GuiSnapshot {
    GuiFieldSnapshot fieldA;
    GuiFieldSnapshot fieldB;
    s3g::AmbiRayBilocationParams params {};
    std::string pairName;
    std::string status;
    int selectedPair = -1;
};

s3g::Vec3 normalizedPosition(const s3g::AmbiRayRoom& room, s3g::Vec3 normalized)
{
    const auto& minimum = room.navigationMinimumMetres;
    const auto& maximum = room.navigationMaximumMetres;
    return {
        s3g::lerp(minimum.x, maximum.x, normalized.x),
        s3g::lerp(minimum.y, maximum.y, normalized.y),
        s3g::lerp(minimum.z, maximum.z, normalized.z)
    };
}

GuiSnapshot guiSnapshot(Plugin& plugin)
{
    std::lock_guard<std::mutex> lock(plugin.stateMutex);
    GuiSnapshot result;
    result.params = plugin.params;
    result.pairName = plugin.pairName;
    result.status = plugin.status;
    result.selectedPair = plugin.selectedPair;
    const s3g::Vec3 sourceA { result.params.sourceX, result.params.sourceY, result.params.sourceZ };
    const s3g::Vec3 sourceB = s3g::mapAmbiRayBilocationSource(sourceA, result.params.mapMode);
    const s3g::Vec3 listener { result.params.listenerX, result.params.listenerY, result.params.listenerZ };
    auto build = [&](const FieldState& field, s3g::Vec3 sourceNormalized, GuiFieldSnapshot& snapshot) {
        snapshot.room = field.descriptor.room;
        snapshot.visual = field.visual;
        snapshot.name = field.name;
        snapshot.source = normalizedPosition(snapshot.room, sourceNormalized);
        snapshot.listener = normalizedPosition(snapshot.room, listener);
        snapshot.cells.reserve(field.descriptor.cells.size());
        uint32_t nearest = 0u;
        float nearestDistance = std::numeric_limits<float>::max();
        for (uint32_t index = 0u; index < field.descriptor.cells.size(); ++index) {
            const auto& cell = field.descriptor.cells[index];
            snapshot.cells.push_back(cell.positionMetres);
            const float dx = snapshot.source.x - cell.positionMetres.x;
            const float dy = snapshot.source.y - cell.positionMetres.y;
            const float dz = snapshot.source.z - cell.positionMetres.z;
            const float distance = dx * dx + dy * dy + dz * dz;
            if (distance < nearestDistance) { nearestDistance = distance; nearest = index; }
        }
        if (nearest < field.descriptor.cells.size()) {
            const auto& reflections = field.descriptor.cells[nearest].reflections;
            const uint32_t count = std::min<uint32_t>(12u, static_cast<uint32_t>(reflections.size()));
            snapshot.reflections.assign(reflections.begin(), reflections.begin() + count);
        }
    };
    build(plugin.fieldA, sourceA, result.fieldA);
    build(plugin.fieldB, sourceB, result.fieldB);
    return result;
}

NSRect fieldARect() { return NSMakeRect(12, 34, 570, 560); }
NSRect fieldBRect() { return NSMakeRect(658, 34, 570, 560); }
NSRect fieldPlanPlotRect(NSRect field) { return NSMakeRect(field.origin.x + 16, field.origin.y + 31, field.size.width - 32, 326); }
NSRect fieldElevationPlotRect(NSRect field) { return NSMakeRect(field.origin.x + 16, field.origin.y + 377, field.size.width - 32, 133); }
NSRect pairButtonRect() { return NSMakeRect(590, 36, 60, 18); }
NSRect pairMenuRect() { return NSMakeRect(430, 58, 380, 20.0 * static_cast<CGFloat>(kPairPresets.size())); }
NSRect loadAButtonRect() { return NSMakeRect(500, 36, 68, 18); }
NSRect loadBButtonRect() { return NSMakeRect(1146, 36, 68, 18); }
NSRect sourceModeRect() { return NSMakeRect(594, 78, 52, 20); }
NSRect listenerModeRect() { return NSMakeRect(594, 104, 52, 20); }
NSRect placeTrackRect() { return NSMakeRect(36, 616, 1168, 12); }
NSRect fieldListenButtonRect(uint32_t mode)
{
    return NSMakeRect(1080 + static_cast<CGFloat>(mode) * 35.0, 840, 32, 17);
}
NSRect bypassButtonRect() { return NSMakeRect(1098, 868, 54, 15); }

struct SliderLayout {
    clap_id id = CLAP_INVALID_ID;
    const char* label = "";
    NSRect row {};
};

const std::array<SliderLayout, 21>& sliderLayouts()
{
    static const std::array<SliderLayout, 21> layouts {{
        { kParamSizeA, "SIZE", NSMakeRect(26, 688, 250, 18) },
        { kParamScatterA, "SCAT", NSMakeRect(26, 720, 250, 18) },
        { kParamWidthA, "WIDTH", NSMakeRect(26, 752, 250, 18) },
        { kParamAirA, "AIR", NSMakeRect(26, 784, 250, 18) },
        { kParamSourceZ, "SRC Z", NSMakeRect(26, 816, 250, 18) },
        { kParamPermeability, "PERMEABILITY", NSMakeRect(320, 688, 388, 18) },
        { kParamMemory, "MEMORY", NSMakeRect(320, 720, 388, 18) },
        { kParamSeparation, "SEPARATION", NSMakeRect(320, 752, 388, 18) },
        { kParamMovement, "MOVEMENT", NSMakeRect(320, 784, 388, 18) },
        { kParamDoppler, "DOPPLER", NSMakeRect(320, 816, 388, 18) },
        { kParamSizeB, "SIZE", NSMakeRect(752, 688, 250, 18) },
        { kParamScatterB, "SCAT", NSMakeRect(752, 720, 250, 18) },
        { kParamWidthB, "WIDTH", NSMakeRect(752, 752, 250, 18) },
        { kParamAirB, "AIR", NSMakeRect(752, 784, 250, 18) },
        { kParamListenerZ, "LIS Z", NSMakeRect(752, 816, 250, 18) },
        { kParamDirect, "DIRECT", NSMakeRect(1044, 672, 168, 18) },
        { kParamEarly, "EARLY", NSMakeRect(1044, 700, 168, 18) },
        { kParamLate, "LATE", NSMakeRect(1044, 728, 168, 18) },
        { kParamOutput, "OUTPUT", NSMakeRect(1044, 756, 168, 18) },
        { kParamOrder, "ORDER", NSMakeRect(1044, 784, 168, 18) },
        { kParamMapMode, "MAP", NSMakeRect(1044, 812, 168, 18) },
    }};
    return layouts;
}

double normalizedParamValue(clap_id id, const s3g::AmbiRayBilocationParams& p)
{
    switch (id) {
    case kParamSourceZ: return p.sourceZ;
    case kParamListenerZ: return p.listenerZ;
    case kParamPermeability: return p.permeability;
    case kParamMemory: return p.memorySeconds / 12.0;
    case kParamSeparation: return p.separationDeg / 180.0;
    case kParamMovement: return (p.movementMs - 10.0) / 490.0;
    case kParamDoppler: return p.doppler / 2.0;
    case kParamSizeA: return (p.sizeA - 0.5) / 1.5;
    case kParamSizeB: return (p.sizeB - 0.5) / 1.5;
    case kParamScatterA: return p.scatterA;
    case kParamScatterB: return p.scatterB;
    case kParamWidthA: return p.widthA / 1.5;
    case kParamWidthB: return p.widthB / 1.5;
    case kParamAirA: return p.airA;
    case kParamAirB: return p.airB;
    case kParamDirect: return p.direct / 1.5;
    case kParamEarly: return p.early / 1.5;
    case kParamLate: return p.late / 1.5;
    case kParamOutput: return (p.outputGainDb + 60.0) / 72.0;
    case kParamOrder: return (p.order - 1.0) / 6.0;
    case kParamMapMode: return static_cast<uint32_t>(p.mapMode) / 3.0;
    default: return 0.0;
    }
}

double valueFromNormalized(clap_id id, double norm)
{
    norm = std::clamp(norm, 0.0, 1.0);
    switch (id) {
    case kParamSourceZ: case kParamListenerZ: case kParamPermeability:
    case kParamScatterA: case kParamScatterB: case kParamAirA: case kParamAirB: return norm;
    case kParamMemory: return norm * 12.0;
    case kParamSeparation: return norm * 180.0;
    case kParamMovement: return 10.0 + norm * 490.0;
    case kParamDoppler: return norm * 2.0;
    case kParamSizeA: case kParamSizeB: return 0.5 + norm * 1.5;
    case kParamWidthA: case kParamWidthB: case kParamDirect: case kParamEarly: case kParamLate: return norm * 1.5;
    case kParamOutput: return -60.0 + norm * 72.0;
    case kParamOrder: return std::lround(1.0 + norm * 6.0);
    case kParamMapMode: return std::lround(norm * 3.0);
    default: return norm;
    }
}

NSString* parameterText(clap_id id, const s3g::AmbiRayBilocationParams& p)
{
    char text[64] {};
    double value = 0.0;
    switch (id) {
    case kParamSourceZ: value = p.sourceZ; break;
    case kParamListenerZ: value = p.listenerZ; break;
    case kParamPermeability: value = p.permeability; break;
    case kParamMemory: value = p.memorySeconds; break;
    case kParamSeparation: value = p.separationDeg; break;
    case kParamMovement: value = p.movementMs; break;
    case kParamDoppler: value = p.doppler; break;
    case kParamSizeA: value = p.sizeA; break;
    case kParamSizeB: value = p.sizeB; break;
    case kParamScatterA: value = p.scatterA; break;
    case kParamScatterB: value = p.scatterB; break;
    case kParamWidthA: value = p.widthA; break;
    case kParamWidthB: value = p.widthB; break;
    case kParamAirA: value = p.airA; break;
    case kParamAirB: value = p.airB; break;
    case kParamDirect: value = p.direct; break;
    case kParamEarly: value = p.early; break;
    case kParamLate: value = p.late; break;
    case kParamOutput: value = p.outputGainDb; break;
    case kParamOrder: value = p.order; break;
    case kParamMapMode: value = static_cast<uint32_t>(p.mapMode); break;
    default: break;
    }
    paramsValueToText(nullptr, id, value, text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

struct FieldBounds {
    float minX = 0.0f;
    float maxX = 1.0f;
    float minY = 0.0f;
    float maxY = 1.0f;
};

FieldBounds fieldBounds(const GuiFieldSnapshot& field)
{
    FieldBounds bounds { field.room.navigationMinimumMetres.x,
        field.room.navigationMaximumMetres.x,
        field.room.navigationMinimumMetres.y,
        field.room.navigationMaximumMetres.y };
    auto include = [&](float x, float y) {
        bounds.minX = std::min(bounds.minX, x);
        bounds.maxX = std::max(bounds.maxX, x);
        bounds.minY = std::min(bounds.minY, y);
        bounds.maxY = std::max(bounds.maxY, y);
    };
    for (const auto& point : field.room.polygon) include(point[0], point[1]);
    for (const auto& region : field.visual.regions) for (const auto& point : region.polygon) include(point[0], point[1]);
    const float padX = std::max(0.1f, (bounds.maxX - bounds.minX) * 0.06f);
    const float padY = std::max(0.1f, (bounds.maxY - bounds.minY) * 0.06f);
    bounds.minX -= padX; bounds.maxX += padX;
    bounds.minY -= padY; bounds.maxY += padY;
    return bounds;
}

NSPoint projectPlanPosition(const GuiFieldSnapshot& field, NSRect plot, s3g::Vec3 position)
{
    const auto bounds = fieldBounds(field);
    const float x = (position.x - bounds.minX) / std::max(0.001f, bounds.maxX - bounds.minX);
    const float y = (position.y - bounds.minY) / std::max(0.001f, bounds.maxY - bounds.minY);
    return NSMakePoint(plot.origin.x + x * plot.size.width, plot.origin.y + (1.0f - y) * plot.size.height);
}

struct ElevationBounds {
    float minX = 0.0f;
    float maxX = 1.0f;
    float minZ = 0.0f;
    float maxZ = 1.0f;
};

ElevationBounds elevationBounds(const GuiFieldSnapshot& field)
{
    ElevationBounds bounds {
        field.room.navigationMinimumMetres.x,
        field.room.navigationMaximumMetres.x,
        std::min(0.0f, field.room.navigationMinimumMetres.z),
        std::max(field.room.heightMetres, field.room.navigationMaximumMetres.z)
    };
    auto include = [&](float x, float z) {
        bounds.minX = std::min(bounds.minX, x);
        bounds.maxX = std::max(bounds.maxX, x);
        bounds.minZ = std::min(bounds.minZ, z);
        bounds.maxZ = std::max(bounds.maxZ, z);
    };
    for (const auto& point : field.room.ceilingProfile) include(point[0], point[1]);
    for (const auto& region : field.visual.regions) {
        for (const auto& point : region.polygon) {
            include(point[0], region.baseHeight);
            include(point[0], region.baseHeight + region.height);
        }
    }
    for (const auto& portal : field.visual.portals) include(portal.x, portal.z);
    for (const auto& cell : field.cells) include(cell.x, cell.z);
    for (const auto& reflection : field.reflections)
        if (reflection.hasBouncePosition) include(reflection.bouncePositionMetres.x, reflection.bouncePositionMetres.z);
    include(field.source.x, field.source.z);
    include(field.listener.x, field.listener.z);
    if (bounds.maxX < bounds.minX + 0.1f) bounds.maxX = bounds.minX + 0.1f;
    if (bounds.maxZ < bounds.minZ + 0.1f) bounds.maxZ = bounds.minZ + 0.1f;
    const float padX = std::max(0.1f, (bounds.maxX - bounds.minX) * 0.05f);
    const float padZ = std::max(0.1f, (bounds.maxZ - bounds.minZ) * 0.08f);
    bounds.minX -= padX;
    bounds.maxX += padX;
    bounds.minZ -= padZ;
    bounds.maxZ += padZ;
    return bounds;
}

NSPoint projectElevationPosition(const GuiFieldSnapshot& field, NSRect plot, s3g::Vec3 position)
{
    const auto bounds = elevationBounds(field);
    const float x = (position.x - bounds.minX) / std::max(0.001f, bounds.maxX - bounds.minX);
    const float z = (position.z - bounds.minZ) / std::max(0.001f, bounds.maxZ - bounds.minZ);
    return NSMakePoint(plot.origin.x + x * plot.size.width, plot.origin.y + (1.0f - z) * plot.size.height);
}

} // namespace

@interface S3GAmbiRayBilocationView : NSView {
    Plugin* _plugin;
    clap_id _dragParam;
    NSInteger _dragField;
    bool _editListener;
    bool _pairMenuOpen;
    int _pairMenuHover;
    NSTimer* _refreshTimer;
}
- (id)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)setStatus:(const std::string&)status;
- (bool)readAtlasResource:(const char*)resource name:(const char*)name field:(FieldState&)field error:(std::string&)error;
- (void)loadPairAtIndex:(NSUInteger)index;
- (void)loadField:(NSInteger)fieldIndex;
@end

@implementation S3GAmbiRayBilocationView

- (id)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _dragField = 0;
        _editListener = false;
        _pairMenuOpen = false;
        _pairMenuHover = -1;
        _refreshTimer = nil;
    }
    return self;
}

- (void)dealloc { [self stopRefreshTimer]; [super dealloc]; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)updateTrackingAreas
{
    for (NSTrackingArea* area in [self trackingAreas]) [self removeTrackingArea:area];
    NSTrackingArea* area = [[[NSTrackingArea alloc] initWithRect:[self bounds]
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow
        owner:self userInfo:nil] autorelease];
    [self addTrackingArea:area];
    [super updateTrackingAreas];
}

- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 30.0) target:self selector:@selector(refreshTimerFired:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_refreshTimer) return;
    [_refreshTimer invalidate];
    _refreshTimer = nil;
}

- (void)refreshTimerFired:(NSTimer*)timer
{
    (void)timer;
    if (_plugin && ![self isHidden] && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}

- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*_plugin, param, value);
    [self setNeedsDisplay:YES];
}

- (void)setStatus:(const std::string&)status
{
    std::lock_guard<std::mutex> lock(_plugin->stateMutex);
    _plugin->status = status;
}

- (bool)readAtlasResource:(const char*)resource name:(const char*)name field:(FieldState&)field error:(std::string&)error
{
    NSString* resourceName = [NSString stringWithUTF8String:resource];
    NSBundle* bundle = [NSBundle bundleForClass:[self class]];
    NSString* path = [bundle pathForResource:resourceName ofType:@"s3gray" inDirectory:@"Ray Atlas"];
    if (!path) path = [[NSBundle mainBundle] pathForResource:resourceName ofType:@"s3gray" inDirectory:@"Ray Atlas"];
    if (!path) { error = "ATLAS RESOURCE NOT FOUND"; return false; }
    NSData* data = [NSData dataWithContentsOfFile:path];
    if (!s3g::ray_field_loader::parse(data, field.descriptor, field.visual, field.json, error)) return false;
    field.name = name;
    return true;
}

- (void)loadPairAtIndex:(NSUInteger)index
{
    if (index >= kPairPresets.size()) return;
    const auto& preset = kPairPresets[index];
    FieldState fieldA;
    FieldState fieldB;
    std::string error;
    [self setStatus:"BUILDING CONTRAST PAIR"];
    [self displayIfNeeded];
    if (![self readAtlasResource:preset.resourceA name:preset.nameA field:fieldA error:error]
        || ![self readAtlasResource:preset.resourceB name:preset.nameB field:fieldB error:error]) {
        [self setStatus:error];
        [self setNeedsDisplay:YES];
        return;
    }
    auto params = _plugin->params;
    params.permeability = preset.permeability;
    params.memorySeconds = preset.memory;
    params.separationDeg = preset.separation;
    params.mapMode = preset.mapMode;
    params.sizeA = preset.sizeA; params.sizeB = preset.sizeB;
    params.scatterA = preset.scatterA; params.scatterB = preset.scatterB;
    params.widthA = preset.widthA; params.widthB = preset.widthB;
    params.airA = preset.airA; params.airB = preset.airB;
    _plugin->params = s3g::sanitizeAmbiRayBilocationParams(params);
    if (!installFields(*_plugin, std::move(fieldA), std::move(fieldB), preset.title, static_cast<int>(index), error))
        [self setStatus:error];
    [self setNeedsDisplay:YES];
}

- (void)loadField:(NSInteger)fieldIndex
{
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [panel setAllowedFileTypes:@[@"s3gray"]];
#pragma clang diagnostic pop
    if ([panel runModal] != NSModalResponseOK) return;
    FieldState replacement;
    std::string error;
    NSData* data = [NSData dataWithContentsOfURL:[panel URL]];
    if (!s3g::ray_field_loader::parse(data, replacement.descriptor, replacement.visual, replacement.json, error)) {
        [self setStatus:error];
        return;
    }
    NSString* filename = [[panel URL] lastPathComponent];
    replacement.name = filename ? std::string([filename UTF8String]) : std::string("RAY FIELD");
    FieldState fieldA;
    FieldState fieldB;
    {
        std::lock_guard<std::mutex> lock(_plugin->stateMutex);
        fieldA = _plugin->fieldA;
        fieldB = _plugin->fieldB;
    }
    if (fieldIndex == 1) fieldA = std::move(replacement);
    else fieldB = std::move(replacement);
    if (!installFields(*_plugin, std::move(fieldA), std::move(fieldB), "CUSTOM PAIR", -1, error)) [self setStatus:error];
    [self setNeedsDisplay:YES];
}

- (void)drawField:(const GuiFieldSnapshot&)field
             rect:(NSRect)rect
           source:(s3g::Vec3)sourcePosition
         listener:(s3g::Vec3)listenerPosition
             gain:(float)gain
            label:(NSString*)label
            attrs:(NSDictionary*)attrs
{
    const NSRect plan = fieldPlanPlotRect(rect);
    const NSRect elevation = fieldElevationPlotRect(rect);
    NSColor* sourceColor = s3g::clap_gui::color([label isEqualToString:@"A"] ? 0x69c7d2 : 0xd8a24a, 0.42 + gain * 0.58);

    [s3g::clap_gui::color(0x0b1012) setFill];
    NSRectFill(plan);
    NSRectFill(elevation);
    [s3g::clap_gui::color(0x343b3e) setStroke];
    NSFrameRect(plan);
    NSFrameRect(elevation);

    [NSGraphicsContext saveGraphicsState];
    NSBezierPath* clip = [NSBezierPath bezierPathWithRect:NSInsetRect(plan, 1, 1)];
    [clip addClip];
    auto drawPolygon = [&](const std::vector<std::array<float, 2>>& polygon, NSColor* color, CGFloat width) {
        if (polygon.size() < 2u) return;
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:projectPlanPosition(field, plan, { polygon[0][0], polygon[0][1], 0.0f })];
        for (size_t index = 1u; index < polygon.size(); ++index)
            [path lineToPoint:projectPlanPosition(field, plan, { polygon[index][0], polygon[index][1], 0.0f })];
        [path closePath];
        [path setLineWidth:width];
        [color setStroke];
        [path stroke];
    };
    for (const auto& region : field.visual.regions)
        drawPolygon(region.polygon, s3g::clap_gui::color(0x526167, 0.48), 1.0);
    drawPolygon(field.room.polygon, s3g::clap_gui::color(0xa9b1b3, 0.80), 1.7);
    for (const auto& portal : field.visual.portals) {
        NSPoint point = projectPlanPosition(field, plan, portal);
        [s3g::clap_gui::color(0xd8a24a, 0.72) setFill];
        NSRectFill(NSMakeRect(point.x - 3, point.y - 3, 6, 6));
    }
    for (const auto& cell : field.cells) {
        NSPoint point = projectPlanPosition(field, plan, cell);
        [s3g::clap_gui::color(0x607076, 0.64) setFill];
        NSRectFill(NSMakeRect(point.x - 1.5, point.y - 1.5, 3, 3));
    }
    const NSPoint sourcePoint = projectPlanPosition(field, plan, sourcePosition);
    const NSPoint listenerPoint = projectPlanPosition(field, plan, listenerPosition);
    for (const auto& reflection : field.reflections) {
        if (!reflection.hasBouncePosition) continue;
        NSPoint bounce = projectPlanPosition(field, plan, reflection.bouncePositionMetres);
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:sourcePoint]; [path lineToPoint:bounce]; [path lineToPoint:listenerPoint];
        [path setLineWidth:0.75];
        [s3g::clap_gui::color(0xd8a24a, 0.12 + gain * 0.34) setStroke];
        [path stroke];
    }
    NSBezierPath* direct = [NSBezierPath bezierPath];
    [direct moveToPoint:sourcePoint]; [direct lineToPoint:listenerPoint]; [direct setLineWidth:1.1];
    [s3g::clap_gui::color(0xc2c6c7, 0.28 + gain * 0.55) setStroke]; [direct stroke];
    [s3g::clap_gui::color(0xbcbcbc) setFill];
    NSRectFill(NSMakeRect(listenerPoint.x - 4, listenerPoint.y - 4, 8, 8));
    [sourceColor setFill];
    NSRectFill(NSMakeRect(sourcePoint.x - 7, sourcePoint.y - 7, 14, 14));
    [NSGraphicsContext restoreGraphicsState];

    [@"XY / PLAN" drawAtPoint:NSMakePoint(plan.origin.x + 7, plan.origin.y + 5) withAttributes:attrs];
    [@"XZ / ELEVATION  ·  DRAG FOR HEIGHT" drawAtPoint:NSMakePoint(elevation.origin.x, elevation.origin.y - 17) withAttributes:attrs];

    [NSGraphicsContext saveGraphicsState];
    NSBezierPath* elevationClip = [NSBezierPath bezierPathWithRect:NSInsetRect(elevation, 1, 1)];
    [elevationClip addClip];
    const auto sideBounds = elevationBounds(field);
    auto sidePoint = [&](float x, float z) {
        return projectElevationPosition(field, elevation, { x, 0.0f, z });
    };

    [s3g::clap_gui::color(0x24292b) setStroke];
    for (int index = 1; index < 5; ++index) {
        const CGFloat x = elevation.origin.x + elevation.size.width * static_cast<CGFloat>(index) / 5.0;
        const CGFloat y = elevation.origin.y + elevation.size.height * static_cast<CGFloat>(index) / 5.0;
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, elevation.origin.y) toPoint:NSMakePoint(x, NSMaxY(elevation))];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(elevation.origin.x, y) toPoint:NSMakePoint(NSMaxX(elevation), y)];
    }

    NSBezierPath* roomSide = [NSBezierPath bezierPath];
    [roomSide moveToPoint:sidePoint(sideBounds.minX, 0.0f)];
    [roomSide lineToPoint:sidePoint(sideBounds.maxX, 0.0f)];
    if (field.room.ceilingProfile.size() >= 2u) {
        for (auto iterator = field.room.ceilingProfile.rbegin(); iterator != field.room.ceilingProfile.rend(); ++iterator)
            [roomSide lineToPoint:sidePoint((*iterator)[0], (*iterator)[1])];
    } else {
        [roomSide lineToPoint:sidePoint(sideBounds.maxX, field.room.heightMetres)];
        [roomSide lineToPoint:sidePoint(sideBounds.minX, field.room.heightMetres)];
    }
    [roomSide closePath];
    [s3g::clap_gui::color(0x788084, 0.09) setFill];
    [roomSide fill];
    [s3g::clap_gui::color(0xa9b1b3, 0.78) setStroke];
    [roomSide setLineWidth:1.4];
    [roomSide stroke];

    for (const auto& region : field.visual.regions) {
        if (region.polygon.empty()) continue;
        float minimumX = region.polygon.front()[0];
        float maximumX = minimumX;
        for (const auto& point : region.polygon) {
            minimumX = std::min(minimumX, point[0]);
            maximumX = std::max(maximumX, point[0]);
        }
        NSPoint topLeft = sidePoint(minimumX, region.baseHeight + region.height);
        NSPoint bottomRight = sidePoint(maximumX, region.baseHeight);
        NSBezierPath* regionSide = [NSBezierPath bezierPathWithRect:NSMakeRect(
            topLeft.x, topLeft.y, std::max<CGFloat>(1.0, bottomRight.x - topLeft.x),
            std::max<CGFloat>(1.0, bottomRight.y - topLeft.y))];
        [s3g::clap_gui::color(0x668b78, 0.07) setFill];
        [regionSide fill];
        [s3g::clap_gui::color(0x78ad91, 0.50) setStroke];
        [regionSide stroke];
    }

    const auto& navMin = field.room.navigationMinimumMetres;
    const auto& navMax = field.room.navigationMaximumMetres;
    NSPoint navTopLeft = sidePoint(navMin.x, navMax.z);
    NSPoint navBottomRight = sidePoint(navMax.x, navMin.z);
    NSBezierPath* navigation = [NSBezierPath bezierPathWithRect:NSMakeRect(
        navTopLeft.x, navTopLeft.y, std::max<CGFloat>(1.0, navBottomRight.x - navTopLeft.x),
        std::max<CGFloat>(1.0, navBottomRight.y - navTopLeft.y))];
    const CGFloat navigationDash[] = { 4.0, 3.0 };
    [navigation setLineDash:navigationDash count:2 phase:0.0];
    [s3g::clap_gui::color(0x7199a1, 0.48) setStroke];
    [navigation stroke];

    const float depthRange = std::max(0.001f, navMax.y - navMin.y);
    for (const auto& cell : field.cells) {
        NSPoint point = projectElevationPosition(field, elevation, cell);
        const float depth = s3g::clamp((cell.y - navMin.y) / depthRange, 0.0f, 1.0f);
        [s3g::clap_gui::color(0x607076, 0.24 + depth * 0.46) setFill];
        NSRectFill(NSMakeRect(point.x - 1.5, point.y - 1.5, 3, 3));
    }
    for (const auto& portal : field.visual.portals) {
        NSPoint point = projectElevationPosition(field, elevation, portal);
        [s3g::clap_gui::color(0xd8a24a, 0.68) setFill];
        NSRectFill(NSMakeRect(point.x - 2.5, point.y - 2.5, 5, 5));
    }

    const NSPoint sourceSide = projectElevationPosition(field, elevation, sourcePosition);
    const NSPoint listenerSide = projectElevationPosition(field, elevation, listenerPosition);
    for (const auto& reflection : field.reflections) {
        if (!reflection.hasBouncePosition) continue;
        const NSPoint bounce = projectElevationPosition(field, elevation, reflection.bouncePositionMetres);
        NSBezierPath* ray = [NSBezierPath bezierPath];
        [ray moveToPoint:sourceSide]; [ray lineToPoint:bounce]; [ray lineToPoint:listenerSide];
        [ray setLineWidth:0.65];
        [s3g::clap_gui::color(0xd8a24a, 0.10 + gain * 0.30) setStroke];
        [ray stroke];
    }
    NSBezierPath* heightRule = [NSBezierPath bezierPath];
    [heightRule moveToPoint:NSMakePoint(elevation.origin.x, sourceSide.y)];
    [heightRule lineToPoint:NSMakePoint(NSMaxX(elevation), sourceSide.y)];
    const CGFloat heightDash[] = { 2.0, 4.0 };
    [heightRule setLineDash:heightDash count:2 phase:0.0];
    [sourceColor setStroke];
    [heightRule stroke];
    NSBezierPath* directSide = [NSBezierPath bezierPath];
    [directSide moveToPoint:sourceSide]; [directSide lineToPoint:listenerSide];
    [directSide setLineWidth:1.0];
    [s3g::clap_gui::color(0xc2c6c7, 0.25 + gain * 0.50) setStroke];
    [directSide stroke];
    [s3g::clap_gui::color(0xbcbcbc) setFill];
    NSRectFill(NSMakeRect(listenerSide.x - 4, listenerSide.y - 4, 8, 8));
    [sourceColor setFill];
    NSRectFill(NSMakeRect(sourceSide.x - 6, sourceSide.y - 6, 12, 12));
    [NSGraphicsContext restoreGraphicsState];

    NSString* sourceHeight = [NSString stringWithFormat:@"SRC %.2f m", sourcePosition.z];
    NSString* listenerHeight = [NSString stringWithFormat:@"LIS %.2f m", listenerPosition.z];
    [sourceHeight drawAtPoint:NSMakePoint(elevation.origin.x + 7, elevation.origin.y + 5) withAttributes:attrs];
    const CGFloat listenerWidth = [listenerHeight sizeWithAttributes:attrs].width;
    [listenerHeight drawAtPoint:NSMakePoint(NSMaxX(elevation) - listenerWidth - 7, elevation.origin.y + 5) withAttributes:attrs];

    [label drawAtPoint:NSMakePoint(rect.origin.x + 12, rect.origin.y + 5) withAttributes:attrs];
    NSString* name = [NSString stringWithUTF8String:field.name.c_str()];
    [name drawAtPoint:NSMakePoint(rect.origin.x + 34, rect.origin.y + 5) withAttributes:attrs];
    NSString* details = [NSString stringWithFormat:@"%zu CELLS  %.1f x %.1f x %.1f m  PRES %.0f%%",
        field.cells.size(), field.room.widthMetres, field.room.depthMetres,
        field.room.heightMetres, gain * 100.0f];
    [details drawAtPoint:NSMakePoint(rect.origin.x + 16, NSMaxY(rect) - 27) withAttributes:attrs];
}

- (void)drawSlider:(const SliderLayout&)layout params:(const s3g::AmbiRayBilocationParams&)params attrs:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    NSString* label = [NSString stringWithUTF8String:layout.label];
    [label drawAtPoint:NSMakePoint(layout.row.origin.x, layout.row.origin.y + 1) withAttributes:attrs];
    const CGFloat labelWidth = layout.row.size.width > 300 ? 106.0 : (layout.row.size.width > 200 ? 58.0 : 54.0);
    NSRect track = NSMakeRect(layout.row.origin.x + labelWidth, layout.row.origin.y + 4,
        layout.row.size.width - labelWidth - 54.0, 9);
    [style.strip setFill]; NSRectFill(track);
    [style.grid setStroke]; NSFrameRect(track);
    const CGFloat norm = std::clamp(static_cast<CGFloat>(normalizedParamValue(layout.id, params)), 0.0, 1.0);
    NSRect fill = NSInsetRect(track, 1, 1); fill.size.width *= norm;
    [(layout.id == kParamSizeA || layout.id == kParamScatterA || layout.id == kParamWidthA || layout.id == kParamAirA
        ? s3g::clap_gui::color(0x69c7d2, 0.72)
        : layout.id == kParamSizeB || layout.id == kParamScatterB || layout.id == kParamWidthB || layout.id == kParamAirB
            ? s3g::clap_gui::color(0xd8a24a, 0.72) : style.fill) setFill];
    NSRectFill(fill);
    [parameterText(layout.id, params) drawAtPoint:NSMakePoint(NSMaxX(layout.row) - 49, layout.row.origin.y + 1) withAttributes:attrs];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    s3g::clap_gui::Style style;
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* labelAttrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const auto gains = s3g::ambiRayBilocationGains(snapshot.params.place, snapshot.params.permeability);
    [@"s3g AMBI RAY BILOCATION ENCODER" drawAtPoint:NSMakePoint(18, 13) withAttributes:labelAttrs];
    NSString* peakText = s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed));
    NSString* status = [NSString stringWithFormat:@"%@   %@   %@",
        [NSString stringWithUTF8String:snapshot.pairName.c_str()],
        [NSString stringWithUTF8String:snapshot.status.c_str()],
        peakText];
    [status drawAtPoint:NSMakePoint(kGuiWidth - [status sizeWithAttributes:valueAttrs].width - 18, 13) withAttributes:valueAttrs];

    s3g::clap_gui::drawPanelFrame(12, 34, 570, 560, style);
    s3g::clap_gui::drawPanelFrame(658, 34, 570, 560, style);
    [self drawField:snapshot.fieldA rect:fieldARect()
        source:snapshot.fieldA.source listener:snapshot.fieldA.listener gain:gains.a label:@"A" attrs:valueAttrs];
    [self drawField:snapshot.fieldB rect:fieldBRect()
        source:snapshot.fieldB.source listener:snapshot.fieldB.listener gain:gains.b label:@"B" attrs:valueAttrs];

    const NSRect elevationA = fieldElevationPlotRect(fieldARect());
    const NSRect elevationB = fieldElevationPlotRect(fieldBRect());
    const CGFloat sourceYA = projectElevationPosition(snapshot.fieldA, elevationA, snapshot.fieldA.source).y;
    const CGFloat sourceYB = projectElevationPosition(snapshot.fieldB, elevationB, snapshot.fieldB.source).y;
    NSBezierPath* zLink = [NSBezierPath bezierPath];
    [zLink moveToPoint:NSMakePoint(NSMaxX(elevationA), sourceYA)];
    [zLink lineToPoint:NSMakePoint(NSMinX(elevationB), sourceYB)];
    const CGFloat zDash[] = { 3.0, 3.0 };
    [zLink setLineDash:zDash count:2 phase:0.0];
    [zLink setLineWidth:1.2];
    [s3g::clap_gui::color(0xbba56f, 0.62) setStroke];
    [zLink stroke];
    [s3g::clap_gui::color(0x69c7d2, 0.88) setFill];
    NSRectFill(NSMakeRect(NSMaxX(elevationA) - 2, sourceYA - 2, 4, 4));
    [s3g::clap_gui::color(0xd8a24a, 0.88) setFill];
    NSRectFill(NSMakeRect(NSMinX(elevationB) - 2, sourceYB - 2, 4, 4));
    NSString* zMode = snapshot.params.mapMode == s3g::AmbiRayBilocationMapMode::Counter ? @"Z COUNTER" : @"Z LINK";
    const CGFloat zModeWidth = [zMode sizeWithAttributes:labelAttrs].width;
    [zMode drawAtPoint:NSMakePoint(620.0 - zModeWidth * 0.5, (sourceYA + sourceYB) * 0.5 - 16.0) withAttributes:labelAttrs];

    s3g::clap_gui::drawHeaderActionButton(loadAButtonRect(), NSMakeRect(12, 34, 570, 21), @"LOAD A", valueAttrs, style);
    s3g::clap_gui::drawHeaderActionButton(loadBButtonRect(), NSMakeRect(658, 34, 570, 21), @"LOAD B", valueAttrs, style);
    s3g::clap_gui::drawHeaderActionButton(pairButtonRect(), NSMakeRect(584, 34, 72, 21), @"PAIR", valueAttrs, style);
    s3g::clap_gui::drawHeaderButton(sourceModeRect(), NSMakeRect(584, 76, 72, 22), @"SRC", !_editListener, valueAttrs, style);
    s3g::clap_gui::drawHeaderButton(listenerModeRect(), NSMakeRect(584, 102, 72, 22), @"LIS", _editListener, valueAttrs, style);
    [@"TWO" drawAtPoint:NSMakePoint(608, 178) withAttributes:labelAttrs];
    [@"PLACES" drawAtPoint:NSMakePoint(594, 194) withAttributes:labelAttrs];
    [@"ONE" drawAtPoint:NSMakePoint(607, 230) withAttributes:labelAttrs];
    [@"SOURCE" drawAtPoint:NSMakePoint(592, 246) withAttributes:labelAttrs];
    [s3g::clap_gui::color(0x69c7d2, gains.a) setFill]; NSRectFill(NSMakeRect(606, 286, 10, 122 * gains.a));
    [s3g::clap_gui::color(0xd8a24a, gains.b) setFill]; NSRectFill(NSMakeRect(624, 286, 10, 122 * gains.b));

    [style.strip setFill]; NSRectFill(placeTrackRect()); [style.grid setStroke]; NSFrameRect(placeTrackRect());
    const CGFloat seamHalf = 10.0 + snapshot.params.permeability * (placeTrackRect().size.width * 0.48 - 10.0);
    [s3g::clap_gui::color(0x796f62, 0.28) setFill];
    NSRectFill(NSMakeRect(NSMidX(placeTrackRect()) - seamHalf, placeTrackRect().origin.y + 1, seamHalf * 2, placeTrackRect().size.height - 2));
    [s3g::clap_gui::color(0x69c7d2, 0.72) setFill];
    NSRectFill(NSMakeRect(placeTrackRect().origin.x + 1, placeTrackRect().origin.y + 1,
        (placeTrackRect().size.width - 2) * (1.0 - snapshot.params.place), placeTrackRect().size.height - 2));
    const CGFloat markerX = placeTrackRect().origin.x + snapshot.params.place * placeTrackRect().size.width;
    [s3g::clap_gui::color(0xf0f0f0) setFill]; NSRectFill(NSMakeRect(markerX - 2, placeTrackRect().origin.y - 4, 4, placeTrackRect().size.height + 8));
    [@"A" drawAtPoint:NSMakePoint(18, 613) withAttributes:labelAttrs];
    [@"PLACE / TRANSIT / OVERLAP" drawAtPoint:NSMakePoint(532, 634) withAttributes:labelAttrs];
    [@"B" drawAtPoint:NSMakePoint(1213, 613) withAttributes:labelAttrs];

    s3g::clap_gui::drawPanelFrame(12, 656, 280, 230, style);
    s3g::clap_gui::drawPanelHeader(@"SPACE A CHARACTER", true, 12, 656, 280, 21, labelAttrs, style);
    s3g::clap_gui::drawPanelFrame(304, 656, 420, 230, style);
    s3g::clap_gui::drawPanelHeader(@"BILOCATION MEMBRANE", true, 304, 656, 420, 21, labelAttrs, style);
    s3g::clap_gui::drawPanelFrame(736, 656, 280, 230, style);
    s3g::clap_gui::drawPanelHeader(@"SPACE B CHARACTER", true, 736, 656, 280, 21, labelAttrs, style);
    s3g::clap_gui::drawPanelFrame(1028, 656, 200, 230, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD / OUTPUT", true, 1028, 656, 200, 21, labelAttrs, style);
    for (const auto& layout : sliderLayouts()) [self drawSlider:layout params:snapshot.params attrs:valueAttrs style:style];
    [@"LST" drawAtPoint:NSMakePoint(1044, 841) withAttributes:labelAttrs];
    static NSString* listenLabels[] = { @"OFF", @"FOL", @"CTR", @"BAL" };
    const uint32_t listenMode =
        static_cast<uint32_t>(snapshot.params.fieldListenMode);
    const NSRect outputPanel = NSMakeRect(1028, 656, 200, 230);
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        s3g::clap_gui::drawHeaderButton(
            fieldListenButtonRect(mode), outputPanel, listenLabels[mode],
            mode == listenMode, valueAttrs, style);
    }
    s3g::clap_gui::drawToggle(@"BYP", snapshot.params.bypassRoom, 869,
        labelAttrs, valueAttrs, style, 1044, 1098, 54);

    if (_pairMenuOpen) {
        NSString* names[kPairPresets.size()];
        for (size_t index = 0u; index < kPairPresets.size(); ++index)
            names[index] = [NSString stringWithUTF8String:kPairPresets[index].title];
        s3g::clap_gui::drawDropdownMenu(pairMenuRect(), 20, names, static_cast<uint32_t>(kPairPresets.size()),
            snapshot.selectedPair, _pairMenuHover, valueAttrs, style);
    }
}

- (void)updateSlider:(clap_id)param point:(NSPoint)point
{
    if (param == kParamPlace) {
        const double norm = (point.x - placeTrackRect().origin.x) / placeTrackRect().size.width;
        [self setParam:param value:std::clamp(norm, 0.0, 1.0)];
        return;
    }
    for (const auto& layout : sliderLayouts()) {
        if (layout.id != param) continue;
        const CGFloat labelWidth = layout.row.size.width > 300 ? 106.0 : (layout.row.size.width > 200 ? 58.0 : 54.0);
        const NSRect track = NSMakeRect(layout.row.origin.x + labelWidth, layout.row.origin.y,
            layout.row.size.width - labelWidth - 54.0, layout.row.size.height);
        const double norm = (point.x - track.origin.x) / std::max<CGFloat>(1.0, track.size.width);
        [self setParam:param value:valueFromNormalized(param, norm)];
        return;
    }
}

- (void)updateMapPosition:(NSPoint)point field:(NSInteger)fieldIndex
{
    const GuiSnapshot snapshot = guiSnapshot(*_plugin);
    const bool isElevation = fieldIndex >= 3;
    const bool isFieldA = fieldIndex == 1 || fieldIndex == 3;
    const GuiFieldSnapshot& field = isFieldA ? snapshot.fieldA : snapshot.fieldB;
    const NSRect fieldRect = isFieldA ? fieldARect() : fieldBRect();
    const NSRect plot = isElevation ? fieldElevationPlotRect(fieldRect) : fieldPlanPlotRect(fieldRect);
    const auto planBounds = fieldBounds(field);
    const auto sideBounds = elevationBounds(field);
    const float minimumX = isElevation ? sideBounds.minX : planBounds.minX;
    const float maximumX = isElevation ? sideBounds.maxX : planBounds.maxX;
    const float worldX = s3g::lerp(minimumX, maximumX,
        s3g::clamp(static_cast<float>((point.x - plot.origin.x) / plot.size.width), 0.0f, 1.0f));
    const auto& minimum = field.room.navigationMinimumMetres;
    const auto& maximum = field.room.navigationMaximumMetres;
    s3g::Vec3 normalized {
        (worldX - minimum.x) / std::max(0.0001f, maximum.x - minimum.x),
        0.0f
    };
    normalized.x = s3g::clamp(normalized.x, 0.0f, 1.0f);
    if (isElevation) {
        const float worldZ = s3g::lerp(sideBounds.minZ, sideBounds.maxZ,
            s3g::clamp(static_cast<float>(1.0 - (point.y - plot.origin.y) / plot.size.height), 0.0f, 1.0f));
        normalized.z = s3g::clamp((worldZ - minimum.z) / std::max(0.0001f, maximum.z - minimum.z), 0.0f, 1.0f);
    } else {
        const float worldY = s3g::lerp(planBounds.minY, planBounds.maxY,
            s3g::clamp(static_cast<float>(1.0 - (point.y - plot.origin.y) / plot.size.height), 0.0f, 1.0f));
        normalized.y = s3g::clamp((worldY - minimum.y) / std::max(0.0001f, maximum.y - minimum.y), 0.0f, 1.0f);
    }
    if (!_editListener && !isFieldA)
        normalized = s3g::mapAmbiRayBilocationSource(normalized, snapshot.params.mapMode);
    [self setParam:_editListener ? kParamListenerX : kParamSourceX value:normalized.x];
    [self setParam:_editListener
        ? (isElevation ? kParamListenerZ : kParamListenerY)
        : (isElevation ? kParamSourceZ : kParamSourceY)
        value:isElevation ? normalized.z : normalized.y];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_pairMenuOpen) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, pairMenuRect(), 20, static_cast<uint32_t>(kPairPresets.size()));
        _pairMenuOpen = false; _pairMenuHover = -1;
        if (hit >= 0) [self loadPairAtIndex:static_cast<NSUInteger>(hit)];
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, pairButtonRect())) { _pairMenuOpen = true; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, loadAButtonRect())) { [self loadField:1]; return; }
    if (NSPointInRect(point, loadBButtonRect())) { [self loadField:2]; return; }
    if (NSPointInRect(point, sourceModeRect())) { _editListener = false; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, listenerModeRect())) { _editListener = true; [self setNeedsDisplay:YES]; return; }
    if (NSPointInRect(point, NSInsetRect(bypassButtonRect(), -4, -3))) {
        [self setParam:kParamBypass value:_plugin->params.bypassRoom ? 0.0 : 1.0]; return;
    }
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        if (NSPointInRect(point, fieldListenButtonRect(mode))) {
            [self setParam:kParamFieldListen value:mode];
            return;
        }
    }
    if (NSPointInRect(point, NSInsetRect(placeTrackRect(), -8, -8))) {
        _dragParam = kParamPlace; [self updateSlider:_dragParam point:point]; return;
    }
    const NSRect planA = fieldPlanPlotRect(fieldARect());
    const NSRect planB = fieldPlanPlotRect(fieldBRect());
    const NSRect elevationA = fieldElevationPlotRect(fieldARect());
    const NSRect elevationB = fieldElevationPlotRect(fieldBRect());
    if (NSPointInRect(point, planA) || NSPointInRect(point, planB)
        || NSPointInRect(point, elevationA) || NSPointInRect(point, elevationB)) {
        _dragField = NSPointInRect(point, planA) ? 1
            : NSPointInRect(point, planB) ? 2
            : NSPointInRect(point, elevationA) ? 3 : 4;
        [self updateMapPosition:point field:_dragField]; return;
    }
    for (const auto& layout : sliderLayouts()) {
        if (!NSPointInRect(point, NSInsetRect(layout.row, -2, -4))) continue;
        _dragParam = layout.id;
        [self updateSlider:_dragParam point:point]; return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragField != 0) [self updateMapPosition:point field:_dragField];
    else if (_dragParam != CLAP_INVALID_ID) [self updateSlider:_dragParam point:point];
}

- (void)mouseUp:(NSEvent*)event { (void)event; _dragParam = CLAP_INVALID_ID; _dragField = 0; }

- (void)mouseMoved:(NSEvent*)event
{
    if (!_pairMenuOpen) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _pairMenuHover = s3g::clap_gui::dropdownHitIndex(point, pairMenuRect(), 20, static_cast<uint32_t>(kPairPresets.size()));
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event { (void)event; _pairMenuHover = -1; [self setNeedsDisplay:YES]; }

@end

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating)
{
    if (!api || !isFloating) return false;
    *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    instance->guiView = [[S3GAmbiRayBilocationView alloc] initWithPlugin:instance];
    if (!instance->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            static_cast<NSView*>(instance->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(instance->guiView) release]; instance->guiView = nullptr; return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return;
    instance->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GAmbiRayBilocationView*>(instance->guiView);
    [view stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(instance->guiViewport, instance->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, width, height); }
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window) { if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false; auto* instance = self(plugin); return s3g::clap_gui::setResponsiveViewportParent(instance->guiViewport, static_cast<NSView*>(window->cocoa), instance->host); }
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* instance = self(plugin); if (!instance->guiView || !s3g::clap_gui::setResponsiveViewportHidden(instance->guiViewport, false)) return false; instance->guiVisible.store(true, std::memory_order_relaxed); [static_cast<S3GAmbiRayBilocationView*>(instance->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* instance = self(plugin); if (!instance->guiView) return false; instance->guiVisible.store(false, std::memory_order_relaxed); [static_cast<S3GAmbiRayBilocationView*>(instance->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(instance->guiViewport, true); }

const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };
#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExt;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_SURROUND,
    CLAP_PLUGIN_FEATURE_REVERB,
    nullptr
};

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
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    instance->fieldA.name = "BUILT-IN NEAR";
    instance->fieldB.name = "BUILT-IN FAR";
    instance->pairName = "NEAR / FAR BUILT-IN";
    instance->fieldB.descriptor.durationSeconds = 4.5f;
    for (auto& cell : instance->fieldB.descriptor.cells) {
        for (auto& reflection : cell.reflections) {
            reflection.delayMs *= 1.45f;
            reflection.damping = s3g::clamp(reflection.damping + 0.22f, 0.0f, 1.0f);
        }
        cell.late.startMs *= 1.35f;
        cell.late.decaySeconds *= 2.1f;
        cell.late.damping = s3g::clamp(cell.late.damping + 0.18f, 0.0f, 1.0f);
    }
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
}

uint32_t factoryGetPluginCount(const clap_plugin_factory*) { return 1u; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory*, uint32_t index) { return index == 0u ? &descriptor : nullptr; }
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin };
bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
