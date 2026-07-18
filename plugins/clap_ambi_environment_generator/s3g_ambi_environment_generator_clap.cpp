#include "s3g_ambi_environment_generator.h"
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
#include <strings.h>
#include <iterator>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiEnvironmentMaxChannels;
constexpr uint32_t kStateVersion = 3u;
constexpr uint32_t kGuiWidth = 900u;
constexpr uint32_t kGuiHeight = 744u;

constexpr clap_id kOrderParamId = 1;
constexpr clap_id kSceneParamId = 2;
constexpr clap_id kSeedParamId = 3;
constexpr clap_id kActivityParamId = 4;
constexpr clap_id kEvolveParamId = 5;
constexpr clap_id kWindParamId = 6;
constexpr clap_id kRainParamId = 7;
constexpr clap_id kWaterParamId = 8;
constexpr clap_id kFireParamId = 9;
constexpr clap_id kInsectsParamId = 10;
constexpr clap_id kMachineParamId = 11;
constexpr clap_id kNearFarParamId = 12;
constexpr clap_id kSpaceParamId = 13;
constexpr clap_id kOutputParamId = 14;
constexpr clap_id kFieldAzimuthParamId = 15;
constexpr clap_id kFieldElevationParamId = 16;
constexpr clap_id kWidthParamId = 17;
constexpr clap_id kHeadRollParamId = 18;
constexpr clap_id kWalkRateParamId = 19;
constexpr clap_id kWalkDepthParamId = 20;
constexpr clap_id kSourceMotionParamId = 21;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiEnvironmentParams params {};
    int32_t guiViewMode = 0;
    float guiViewAzDeg = 90.0f;
    float guiViewElDeg = 0.0f;
    float guiViewZoom = 1.0f;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::AmbiEnvironmentGenerator engine {};
    s3g::AmbiEnvironmentParams params {};
    std::array<std::vector<float>, kOutputChannels> scratch {};
    std::array<float*, kOutputChannels> scratchPointers {};
    std::atomic<float> outputPeak { 0.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    void* realtimeActivity = nullptr;
    std::atomic<bool> guiVisible { false };
    std::array<std::atomic<float>, s3g::kAmbiEnvironmentCellCount> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiEnvironmentCellCount> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiEnvironmentCellCount> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiEnvironmentCellCount> guiEnergy {};
    std::array<std::atomic<float>, 6> guiLayerLevel {};
    int guiViewMode = 0;
    float guiViewAzDeg = 90.0f;
    float guiViewElDeg = 0.0f;
    float guiViewZoom = 1.0f;
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

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

void applyParam(Plugin& state, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: state.params.order = static_cast<uint32_t>(std::lround(value)); break;
    case kSceneParamId: state.params.scene = static_cast<s3g::AmbiEnvironmentScene>(static_cast<uint32_t>(std::lround(value))); break;
    case kSeedParamId: state.params.seed = static_cast<uint32_t>(std::lround(value)); break;
    case kActivityParamId: state.params.activity = static_cast<float>(value); break;
    case kEvolveParamId: state.params.evolve = static_cast<float>(value); break;
    case kWindParamId: state.params.wind = static_cast<float>(value); break;
    case kRainParamId: state.params.rain = static_cast<float>(value); break;
    case kWaterParamId: state.params.water = static_cast<float>(value); break;
    case kFireParamId: state.params.fire = static_cast<float>(value); break;
    case kInsectsParamId: state.params.insects = static_cast<float>(value); break;
    case kMachineParamId: state.params.machine = static_cast<float>(value); break;
    case kNearFarParamId: state.params.nearFar = static_cast<float>(value); break;
    case kSpaceParamId: state.params.space = static_cast<float>(value); break;
    case kOutputParamId: state.params.outputGainDb = static_cast<float>(value); break;
    case kFieldAzimuthParamId: state.params.fieldAzimuthDeg = static_cast<float>(value); break;
    case kFieldElevationParamId: state.params.fieldElevationDeg = static_cast<float>(value); break;
    case kWidthParamId: state.params.width = static_cast<float>(value); break;
    case kHeadRollParamId: state.params.headRollDeg = static_cast<float>(value); break;
    case kWalkRateParamId: state.params.walkRate = static_cast<float>(value); break;
    case kWalkDepthParamId: state.params.walkDepth = static_cast<float>(value); break;
    case kSourceMotionParamId: state.params.sourceMotion = static_cast<float>(value); break;
    default: return;
    }
    state.engine.setParams(state.params);
    state.params = state.engine.params();
}

void readEvents(Plugin& state, const clap_input_events_t* events)
{
    if (!events) return;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* parameter = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyParam(state, parameter->param_id, parameter->value);
    }
}

#if defined(__APPLE__)
void publishGuiSnapshot(Plugin& state)
{
    if (!state.guiVisible.load(std::memory_order_relaxed)) return;
    const auto& cells = state.engine.cells();
    for (uint32_t i = 0u; i < s3g::kAmbiEnvironmentCellCount; ++i) {
        state.guiAzimuth[i].store(cells[i].azimuthDeg, std::memory_order_relaxed);
        state.guiElevation[i].store(cells[i].elevationDeg, std::memory_order_relaxed);
        state.guiDistance[i].store(cells[i].distance, std::memory_order_relaxed);
        state.guiEnergy[i].store(cells[i].energy, std::memory_order_relaxed);
    }
    const auto& levels = state.engine.layerLevels();
    for (uint32_t i = 0u; i < levels.size(); ++i) {
        state.guiLayerLevel[i].store(levels[i], std::memory_order_relaxed);
    }
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
    state->engine.processBlock(outputs.data(), outputChannels, frames);

    float blockPeak = 0.0f;
    for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
        if (!outputs[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = outputs[channel][frame];
            blockPeak = std::max(blockPeak, std::fabs(value));
            if (useScratch && output.data64 && output.data64[channel]) output.data64[channel][frame] = static_cast<double>(value);
        }
    }
    for (uint32_t channel = outputChannels; channel < output.channel_count; ++channel) {
        if (output.data32 && output.data32[channel]) std::fill(output.data32[channel], output.data32[channel] + frames, 0.0f);
        if (output.data64 && output.data64[channel]) std::fill(output.data64[channel], output.data64[channel] + frames, 0.0);
    }
    state->outputPeak.store(std::max(state->outputPeak.load(std::memory_order_relaxed) * 0.92f, blockPeak), std::memory_order_relaxed);
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
    { kSceneParamId, "Scene", 0.0, 6.0, 0.0, true },
    { kSeedParamId, "Seed", 1.0, 9999.0, 1977.0, true },
    { kActivityParamId, "Activity", 0.0, 1.0, 0.55, false },
    { kEvolveParamId, "Evolve", 0.0, 1.0, 0.35, false },
    { kWindParamId, "Wind", 0.0, 1.0, 0.70, false },
    { kRainParamId, "Rain", 0.0, 1.0, 0.45, false },
    { kWaterParamId, "Water", 0.0, 1.0, 0.45, false },
    { kFireParamId, "Fire", 0.0, 1.0, 0.30, false },
    { kInsectsParamId, "Insects", 0.0, 1.0, 0.50, false },
    { kMachineParamId, "Machine", 0.0, 1.0, 0.35, false },
    { kNearFarParamId, "Near Far", 0.0, 1.0, 0.55, false },
    { kSpaceParamId, "Space", 0.0, 1.0, 0.65, false },
    { kFieldAzimuthParamId, "Head Yaw", -180.0, 180.0, 0.0, false },
    { kFieldElevationParamId, "Head Pitch", -90.0, 90.0, 0.0, false },
    { kWidthParamId, "Width", 0.0, 1.0, 1.0, false },
    { kHeadRollParamId, "Head Roll", -180.0, 180.0, 0.0, false },
    { kWalkRateParamId, "Walk Rate", 0.0, 1.0, 0.16, false },
    { kWalkDepthParamId, "Walk Depth", 0.0, 1.0, 0.35, false },
    { kSourceMotionParamId, "Source Motion", 0.0, 1.0, 0.30, false },
    { kOutputParamId, "Output", -60.0, 6.0, -18.0, false },
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
    std::strncpy(info->module, "Ambi Environment Generator", sizeof(info->module));
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
    case kSceneParamId: *value = static_cast<uint32_t>(params.scene); return true;
    case kSeedParamId: *value = params.seed; return true;
    case kActivityParamId: *value = params.activity; return true;
    case kEvolveParamId: *value = params.evolve; return true;
    case kWindParamId: *value = params.wind; return true;
    case kRainParamId: *value = params.rain; return true;
    case kWaterParamId: *value = params.water; return true;
    case kFireParamId: *value = params.fire; return true;
    case kInsectsParamId: *value = params.insects; return true;
    case kMachineParamId: *value = params.machine; return true;
    case kNearFarParamId: *value = params.nearFar; return true;
    case kSpaceParamId: *value = params.space; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kFieldAzimuthParamId: *value = params.fieldAzimuthDeg; return true;
    case kFieldElevationParamId: *value = params.fieldElevationDeg; return true;
    case kWidthParamId: *value = params.width; return true;
    case kHeadRollParamId: *value = params.headRollDeg; return true;
    case kWalkRateParamId: *value = params.walkRate; return true;
    case kWalkDepthParamId: *value = params.walkDepth; return true;
    case kSourceMotionParamId: *value = params.sourceMotion; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kOrderParamId) std::snprintf(display, size, "%.0fOA", value);
    else if (id == kSceneParamId) std::snprintf(display, size, "%s", s3g::ambiEnvironmentSceneName(static_cast<s3g::AmbiEnvironmentScene>(static_cast<uint32_t>(std::lround(value)))));
    else if (id == kSeedParamId) std::snprintf(display, size, "%.0f", value);
    else if (id == kOutputParamId) std::snprintf(display, size, "%+.1f dB", value);
    else if (id == kFieldAzimuthParamId || id == kFieldElevationParamId || id == kHeadRollParamId) std::snprintf(display, size, "%+.1f deg", value);
    else std::snprintf(display, size, "%.0f%%", value * 100.0);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;
    if (id == kSceneParamId) {
        for (uint32_t scene = 0u; scene <= 6u; ++scene) {
            if (strcasecmp(display, s3g::ambiEnvironmentSceneName(static_cast<s3g::AmbiEnvironmentScene>(scene))) == 0) {
                *value = scene;
                return true;
            }
        }
    }
    *value = std::atof(display);
    if ((id >= kActivityParamId && id <= kSpaceParamId) || id == kWidthParamId
        || (id >= kWalkRateParamId && id <= kSourceMotionParamId)) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    readEvents(*self(plugin), in);
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
    auto* state = self(plugin);
    SavedState saved { kStateVersion, state->params };
#if defined(__APPLE__)
    saved.guiViewMode = state->guiViewMode;
    saved.guiViewAzDeg = state->guiViewAzDeg;
    saved.guiViewElDeg = state->guiViewElDeg;
    saved.guiViewZoom = state->guiViewZoom;
#endif
    return writeExact(stream, &saved, sizeof(saved));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState saved {};
    if (!readExact(stream, &saved, sizeof(saved)) || saved.version != kStateVersion) return false;
    auto* state = self(plugin);
    state->params = saved.params;
    state->engine.setParams(state->params);
    state->params = state->engine.params();
    state->engine.reset();
#if defined(__APPLE__)
    state->guiViewMode = std::clamp<int>(saved.guiViewMode, -1, 2);
    state->guiViewAzDeg = std::clamp(saved.guiViewAzDeg, -180.0f, 180.0f);
    state->guiViewElDeg = std::clamp(saved.guiViewElDeg, -90.0f, 90.0f);
    state->guiViewZoom = std::clamp(saved.guiViewZoom, 0.55f, 2.20f);
#endif
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

struct GuiSliderSpec {
    clap_id id;
    double minimum;
    double maximum;
    double defaultValue;
    CGFloat y;
};

constexpr GuiSliderSpec kGuiSliders[] {
    { kSeedParamId, 1.0, 9999.0, 1977.0, 130.0 },
    { kActivityParamId, 0.0, 1.0, 0.55, 156.0 },
    { kEvolveParamId, 0.0, 1.0, 0.35, 182.0 },
    { kNearFarParamId, 0.0, 1.0, 0.55, 208.0 },
    { kSpaceParamId, 0.0, 1.0, 0.65, 234.0 },
    { kWidthParamId, 0.0, 1.0, 1.0, 260.0 },
    { kFieldAzimuthParamId, -180.0, 180.0, 0.0, 330.0 },
    { kFieldElevationParamId, -90.0, 90.0, 0.0, 356.0 },
    { kHeadRollParamId, -180.0, 180.0, 0.0, 382.0 },
    { kWalkRateParamId, 0.0, 1.0, 0.16, 408.0 },
    { kWalkDepthParamId, 0.0, 1.0, 0.35, 434.0 },
    { kSourceMotionParamId, 0.0, 1.0, 0.30, 460.0 },
    { kWindParamId, 0.0, 1.0, 0.70, 534.0 },
    { kRainParamId, 0.0, 1.0, 0.45, 560.0 },
    { kWaterParamId, 0.0, 1.0, 0.45, 586.0 },
    { kFireParamId, 0.0, 1.0, 0.30, 612.0 },
    { kInsectsParamId, 0.0, 1.0, 0.50, 638.0 },
    { kMachineParamId, 0.0, 1.0, 0.35, 664.0 },
    { kOutputParamId, -60.0, 6.0, -18.0, 690.0 },
};

float linearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

NSColor* environmentPointColor(float azimuthDeg, float elevationDeg, float distance, float alpha)
{
    const float hue = std::fmod(azimuthDeg / 360.0f + 1.0f, 1.0f);
    const float lightness = std::clamp((elevationDeg + 90.0f) / 180.0f, 0.34f, 0.78f);
    const float chroma = std::clamp(distance / 3.2f, 0.10f, 1.0f) * 0.24f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = lightness + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = lightness - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = lightness - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    const float r = linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    const float g = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    const float blue = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    return [NSColor colorWithCalibratedRed:r green:g blue:blue alpha:std::clamp(alpha, 0.20f, 1.0f)];
}

} // namespace

@interface S3GAmbiEnvironmentGeneratorView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    clap_id _dragParam;
    int _openMenu;
    int _hoverMenuItem;
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

@implementation S3GAmbiEnvironmentGeneratorView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = CLAP_INVALID_ID;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _viewMode = plugin->guiViewMode;
        _viewAzDeg = plugin->guiViewAzDeg;
        _viewElDeg = plugin->guiViewElDeg;
        _viewZoom = plugin->guiViewZoom;
        _dragView = NO;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
                                             target:self
                                           selector:@selector(timerTick:)
                                           userInfo:nil
                                            repeats:YES];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)timerTick:(NSTimer*)timer
{
    (void)timer;
    if (!_plugin->guiVisible.load(std::memory_order_relaxed) || !s3g::clap_support::hostAppIsActive()) return;
    [self setNeedsDisplay:YES];
}

- (void)storeViewState
{
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
}

- (NSRect)viewButtonRect:(int)index panel:(NSRect)panel
{
    const CGFloat width = 38.0;
    const CGFloat gap = 5.0;
    const CGFloat x = NSMaxX(panel) - 10.0 - (3.0 - index) * width - (2.0 - index) * gap;
    return NSMakeRect(x, panel.origin.y + 4.0, width, 13.0);
}

- (NSRect)zoomButtonRect:(int)index panel:(NSRect)panel
{
    const CGFloat width = 18.0;
    const CGFloat gap = 4.0;
    const CGFloat viewStart = [self viewButtonRect:0 panel:panel].origin.x;
    return NSMakeRect(viewStart - 12.0 - (2.0 - index) * width - (1.0 - index) * gap,
        panel.origin.y + 4.0, width, 13.0);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = 90.0;
    } else {
        _viewAzDeg = 35.0;
        _viewElDeg = 34.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (NSPoint)projectDirection:(s3g::Vec3)direction distance:(float)distance rect:(NSRect)rect
{
    const CGFloat scale = std::min(rect.size.width, rect.size.height) * 0.34 * std::clamp(_viewZoom, 0.55, 2.20);
    const float radius = std::clamp(std::pow(std::max(0.05f, distance), 0.52f), 0.30f, 2.20f);
    direction = { direction.x * radius, direction.y * radius, direction.z * radius };
    const float azimuth = static_cast<float>(_viewAzDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * direction.x - sa * direction.y;
    const float y1 = sa * direction.x + ca * direction.y;
    const float y2 = ce * y1 + se * direction.z;
    return NSMakePoint(NSMidX(rect) + x1 * scale, NSMidY(rect) - y2 * scale);
}

- (void)drawSlider:(NSString*)name value:(double)value min:(double)minimum max:(double)maximum y:(CGFloat)y suffix:(NSString*)suffix style:(const s3g::clap_gui::Style&)style
{
    NSString* text = nil;
    if ([suffix isEqualToString:@"%"] ) text = [NSString stringWithFormat:@"%.0f%%", value * 100.0];
    else if ([suffix isEqualToString:@"dB"]) text = [NSString stringWithFormat:@"%+.1f", value];
    else if ([suffix isEqualToString:@"deg"]) text = [NSString stringWithFormat:@"%+.0f", value];
    else text = [NSString stringWithFormat:@"%.0f", value];
    const CGFloat norm = static_cast<CGFloat>((value - minimum) / std::max(0.000001, maximum - minimum));
    s3g::clap_gui::drawSlider(name, text, norm, y, s3g::clap_gui::softLabelAttrs(),
        s3g::clap_gui::softValueAttrs(), style, 642, 738, 826, 82);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value y:(CGFloat)y style:(const s3g::clap_gui::Style&)style width:(CGFloat)width
{
    s3g::clap_gui::drawMenu(name, value, y, s3g::clap_gui::softLabelAttrs(),
        s3g::clap_gui::softValueAttrs(), style, 642, 738, width);
}

- (NSRect)openMenuRect
{
    if (_openMenu == 1) return NSMakeRect(738, 96, 122, 18.0 * 7.0);
    if (_openMenu == 2) return NSMakeRect(738, 122, 102, 18.0 * 7.0);
    return NSZeroRect;
}

- (void)drawOpenMenu:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0) return;
    NSString* sceneItems[] { @"WOODLAND", @"WETLAND", @"SHORE", @"RAIN", @"URBAN", @"INDUSTRIAL", @"INTERIOR" };
    NSString* orderItems[] { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    NSString* const* items = _openMenu == 1 ? sceneItems : orderItems;
    const int selected = _openMenu == 1 ? static_cast<int>(_plugin->params.scene) : static_cast<int>(_plugin->params.order) - 1;
    s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0, items, 7u, selected, _hoverMenuItem,
        s3g::clap_gui::softLabelAttrs(), style);
}

- (void)drawField:(NSRect)rect
{
    const auto style = s3g::clap_gui::softTextStyle();
    [s3g::clap_gui::color(0x111111) setFill];
    NSRectFill(rect);
    [style.grid setStroke];
    NSFrameRect(rect);

    std::array<NSPoint, s3g::kAmbiEnvironmentCellCount> points {};
    for (uint32_t i = 0u; i < s3g::kAmbiEnvironmentCellCount; ++i) {
        const float azimuth = _plugin->guiAzimuth[i].load(std::memory_order_relaxed);
        const float elevation = _plugin->guiElevation[i].load(std::memory_order_relaxed);
        const float distance = _plugin->guiDistance[i].load(std::memory_order_relaxed);
        points[i] = [self projectDirection:s3g::directionFromAed(azimuth, elevation) distance:distance rect:rect];
    }

    [s3g::clap_gui::color(0x777777, 0.24) setStroke];
    NSBezierPath* flow = [NSBezierPath bezierPath];
    [flow setLineWidth:0.8];
    for (uint32_t i = 0u; i < s3g::kAmbiEnvironmentCellCount; ++i) {
        const NSPoint a = points[i];
        const NSPoint b = points[(i + 1u) % s3g::kAmbiEnvironmentCellCount];
        [flow moveToPoint:a];
        [flow lineToPoint:b];
    }
    [flow stroke];

    const NSRect listenerRect = NSMakeRect(std::round(NSMidX(rect) - 5.0), std::round(NSMidY(rect) - 5.0), 10.0, 10.0);
    [s3g::clap_gui::color(0x3a3a3a) setFill];
    NSRectFill(listenerRect);
    [s3g::clap_gui::color(0xb0b0b0, 0.72) setStroke];
    NSFrameRect(listenerRect);
    [@"L" drawAtPoint:NSMakePoint(NSMidX(listenerRect) - 2.7, NSMidY(listenerRect) - 5.0)
        withAttributes:s3g::clap_gui::textAttrs(s3g::clap_gui::color(0xc0c0c0), 8.0)];

    const auto idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x151515), 8.0);
    for (uint32_t i = 0u; i < s3g::kAmbiEnvironmentCellCount; ++i) {
        const float azimuth = _plugin->guiAzimuth[i].load(std::memory_order_relaxed);
        const float elevation = _plugin->guiElevation[i].load(std::memory_order_relaxed);
        const float distance = _plugin->guiDistance[i].load(std::memory_order_relaxed);
        const float energy = std::clamp(_plugin->guiEnergy[i].load(std::memory_order_relaxed) * 18.0f, 0.0f, 1.0f);
        const NSRect pointRect = NSMakeRect(std::round(points[i].x - 6.0), std::round(points[i].y - 6.0), 12.0, 12.0);
        [environmentPointColor(azimuth, elevation, distance, 0.58f + energy * 0.40f) setFill];
        NSRectFill(pointRect);
        [s3g::clap_gui::color(0xb0b0b0, 0.24 + energy * 0.58) setStroke];
        NSFrameRect(NSInsetRect(pointRect, -2.0, -2.0));
        NSString* label = [NSString stringWithFormat:@"%u", i + 1u];
        const NSSize size = [label sizeWithAttributes:idAttrs];
        [label drawAtPoint:NSMakePoint(NSMidX(pointRect) - size.width * 0.5, NSMidY(pointRect) - size.height * 0.5)
            withAttributes:idAttrs];
    }

    static NSString* layerNames[] { @"WIND", @"RAIN", @"WATER", @"FIRE", @"INSECT", @"MACHINE" };
    const CGFloat meterY = NSMaxY(rect) - 25.0;
    const CGFloat meterW = (rect.size.width - 34.0) / 6.0;
    for (uint32_t i = 0u; i < 6u; ++i) {
        const CGFloat x = rect.origin.x + 10.0 + i * meterW;
        const float level = std::clamp(_plugin->guiLayerLevel[i].load(std::memory_order_relaxed), 0.0f, 1.0f);
        [layerNames[i] drawAtPoint:NSMakePoint(x, meterY) withAttributes:s3g::clap_gui::softValueAttrs()];
        [s3g::clap_gui::color(0x242424) setFill];
        NSRectFill(NSMakeRect(x, meterY + 13.0, meterW - 12.0, 3.0));
        [s3g::clap_gui::color(0x8c8c8c, 0.72) setFill];
        NSRectFill(NSMakeRect(x, meterY + 13.0, (meterW - 12.0) * level, 3.0));
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    const auto style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    [@"s3g AMBI ENVIRONMENT GENERATOR" drawAtPoint:NSMakePoint(18, 14) withAttributes:s3g::clap_gui::softTitleAttrs()];
    s3g::clap_gui::drawRightStatus(@"64CH", kGuiWidth, 14, s3g::clap_gui::softValueAttrs(), 18.0);
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)),
        kGuiWidth, 14, s3g::clap_gui::softValueAttrs(), 72.0);

    const NSRect fieldPanel = NSMakeRect(18, 42, 596, 682);
    s3g::clap_gui::drawPanelFrame(fieldPanel.origin.x, fieldPanel.origin.y, fieldPanel.size.width, fieldPanel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ENVIRONMENT FIELD", true, fieldPanel.origin.x, fieldPanel.origin.y,
        fieldPanel.size.width, 21, s3g::clap_gui::softLabelAttrs(), style);
    static NSString* viewLabels[] { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:i panel:fieldPanel], fieldPanel,
            viewLabels[i], i == _viewMode, s3g::clap_gui::softValueAttrs(), style);
    }
    static NSString* zoomLabels[] { @"-", @"+" };
    for (int i = 0; i < 2; ++i) {
        s3g::clap_gui::drawHeaderButton([self zoomButtonRect:i panel:fieldPanel], fieldPanel,
            zoomLabels[i], false, s3g::clap_gui::softValueAttrs(), style);
    }
    [self drawField:NSMakeRect(34, 76, 564, 632)];

    s3g::clap_gui::drawPanelFrame(630, 42, 250, 240, style);
    s3g::clap_gui::drawPanelHeader(@"WORLD / AMBISONICS", true, 630, 42, 250, 21, s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawPanelFrame(630, 294, 250, 192, style);
    s3g::clap_gui::drawPanelHeader(@"LISTENER", true, 630, 294, 250, 21, s3g::clap_gui::softLabelAttrs(), style);
    s3g::clap_gui::drawPanelFrame(630, 498, 250, 226, style);
    s3g::clap_gui::drawPanelHeader(@"LAYERS", true, 630, 498, 250, 21, s3g::clap_gui::softLabelAttrs(), style);

    const auto params = _plugin->params;
    [self drawMenu:@"SCENE" value:[NSString stringWithUTF8String:s3g::ambiEnvironmentSceneName(params.scene)] y:78 style:style width:122];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", params.order] y:104 style:style width:102];
    [self drawSlider:@"SEED" value:params.seed min:1 max:9999 y:130 suffix:@"" style:style];
    [self drawSlider:@"ACT" value:params.activity min:0 max:1 y:156 suffix:@"%" style:style];
    [self drawSlider:@"EVOLVE" value:params.evolve min:0 max:1 y:182 suffix:@"%" style:style];
    [self drawSlider:@"N/F" value:params.nearFar min:0 max:1 y:208 suffix:@"%" style:style];
    [self drawSlider:@"SPACE" value:params.space min:0 max:1 y:234 suffix:@"%" style:style];
    [self drawSlider:@"WIDTH" value:params.width min:0 max:1 y:260 suffix:@"%" style:style];
    [self drawSlider:@"YAW" value:params.fieldAzimuthDeg min:-180 max:180 y:330 suffix:@"deg" style:style];
    [self drawSlider:@"PITCH" value:params.fieldElevationDeg min:-90 max:90 y:356 suffix:@"deg" style:style];
    [self drawSlider:@"ROLL" value:params.headRollDeg min:-180 max:180 y:382 suffix:@"deg" style:style];
    [self drawSlider:@"WALK" value:params.walkRate min:0 max:1 y:408 suffix:@"%" style:style];
    [self drawSlider:@"DEPTH" value:params.walkDepth min:0 max:1 y:434 suffix:@"%" style:style];
    [self drawSlider:@"MOTION" value:params.sourceMotion min:0 max:1 y:460 suffix:@"%" style:style];
    [self drawSlider:@"WIND" value:params.wind min:0 max:1 y:534 suffix:@"%" style:style];
    [self drawSlider:@"RAIN" value:params.rain min:0 max:1 y:560 suffix:@"%" style:style];
    [self drawSlider:@"WATER" value:params.water min:0 max:1 y:586 suffix:@"%" style:style];
    [self drawSlider:@"FIRE" value:params.fire min:0 max:1 y:612 suffix:@"%" style:style];
    [self drawSlider:@"INSECT" value:params.insects min:0 max:1 y:638 suffix:@"%" style:style];
    [self drawSlider:@"MACHINE" value:params.machine min:0 max:1 y:664 suffix:@"%" style:style];
    [self drawSlider:@"OUT" value:params.outputGainDb min:-60 max:6 y:690 suffix:@"dB" style:style];
    [self drawOpenMenu:style];
}

- (void)setSlider:(const GuiSliderSpec&)spec point:(NSPoint)point
{
    const double norm = std::clamp((static_cast<double>(point.x) - 738.0) / 82.0, 0.0, 1.0);
    applyParam(*_plugin, spec.id, spec.minimum + norm * (spec.maximum - spec.minimum));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int index = s3g::clap_gui::dropdownHitIndex(point, [self openMenuRect], 18.0, 7u);
        if (index >= 0) {
            applyParam(*_plugin, _openMenu == 1 ? kSceneParamId : kOrderParamId,
                _openMenu == 1 ? index : index + 1);
            _openMenu = 0;
            _hoverMenuItem = -1;
            [self setNeedsDisplay:YES];
            return;
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
    }

    const NSRect fieldPanel = NSMakeRect(18, 42, 596, 682);
    for (int i = 0; i < 3; ++i) {
        if (NSPointInRect(point, [self viewButtonRect:i panel:fieldPanel])) {
            [self setViewPreset:i];
            return;
        }
    }
    for (int i = 0; i < 2; ++i) {
        if (NSPointInRect(point, [self zoomButtonRect:i panel:fieldPanel])) {
            _viewZoom = std::clamp(_viewZoom * (i == 0 ? 0.88 : 1.14), 0.55, 2.20);
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, NSMakeRect(34, 76, 564, 632))) {
        _dragView = YES;
        _lastDragPoint = point;
        return;
    }
    if (NSPointInRect(point, NSMakeRect(738, 77, 122, 17))) {
        _openMenu = 1;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(738, 103, 102, 17))) {
        _openMenu = 2;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    for (const auto& spec : kGuiSliders) {
        if (!NSPointInRect(point, NSMakeRect(638, spec.y - 8.0, 230, 24))) continue;
        if ([event clickCount] >= 2) {
            applyParam(*_plugin, spec.id, spec.defaultValue);
            [self setNeedsDisplay:YES];
            return;
        }
        _dragParam = spec.id;
        [self setSlider:spec point:point];
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        _viewMode = -1;
        _viewAzDeg = std::fmod(_viewAzDeg + (point.x - _lastDragPoint.x) * 0.45 + 540.0, 360.0) - 180.0;
        _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.40, -85.0, 85.0);
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam == CLAP_INVALID_ID) return;
    for (const auto& spec : kGuiSliders) {
        if (spec.id == _dragParam) {
            [self setSlider:spec point:point];
            return;
        }
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
    const int hover = s3g::clap_gui::dropdownHitIndex(point, [self openMenuRect], 18.0, 7u);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
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
    state->guiView = [[S3GAmbiEnvironmentGeneratorView alloc] initWithPlugin:state];
    return state->guiView != nullptr;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state || !state->guiView) return;
    state->guiVisible.store(false, std::memory_order_relaxed);
    auto* view = static_cast<S3GAmbiEnvironmentGeneratorView*>(state->guiView);
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
    [static_cast<S3GAmbiEnvironmentGeneratorView*>(state->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* state = self(plugin);
    if (!state->guiView) return false;
    state->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAmbiEnvironmentGeneratorView*>(state->guiView) stopRefreshTimer];
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

const char* const features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_AMBISONIC,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-environment-generator-64",
    "s3g Ambi Environment Generator 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Procedural environmental soundscape instrument with first- through seventh-order ACN/SN3D output.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*, const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    auto* state = new (std::nothrow) Plugin();
    if (!state) return nullptr;
    state->host = host;
    state->engine.prepare(state->sampleRate);
    state->engine.setParams(state->params);
    state->params = state->engine.params();
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
    state->plugin.get_extension = getExtension;
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
