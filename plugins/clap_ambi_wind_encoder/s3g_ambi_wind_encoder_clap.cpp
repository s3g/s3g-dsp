#include "s3g_ambi_wind_encoder.h"
#include "s3g_ambi_wind_presets.h"
#include "s3g_parameter_surface.h"
#include "s3g_realtime.h"
#include "../common/s3g_clap_gui_param_queue.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_parameter_surface_cocoa.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiWindMaxChannels;
constexpr uint32_t kStateVersion = 18;
constexpr uint32_t kCustomPresetMagic = 0x31445741u; // AWD1
constexpr uint32_t kCustomPresetVersion = 15;

constexpr clap_id kPresetParamId = 1;
constexpr clap_id kOrderParamId = 2;
constexpr clap_id kVoicesParamId = 3;
constexpr clap_id kRateAParamId = 4;
constexpr clap_id kRateBParamId = 5;
constexpr clap_id kFmAtoBParamId = 6;
constexpr clap_id kFmBtoAParamId = 7;
constexpr clap_id kFlutterParamId = 8;
constexpr clap_id kMaterialParamId = 9;
constexpr clap_id kSpreadParamId = 10;
constexpr clap_id kDeviationParamId = 11;
constexpr clap_id kGustShapeParamId = 12;
constexpr clap_id kRateModeAParamId = 13;
constexpr clap_id kThresholdParamId = 14;
constexpr clap_id kColorParamId = 15;
constexpr clap_id kFilterParamId = 16;
constexpr clap_id kResonanceParamId = 17;
constexpr clap_id kFilterRunParamId = 18;
constexpr clap_id kFilterSweepParamId = 19;
constexpr clap_id kSaturationParamId = 20;
constexpr clap_id kMotionRateParamId = 23;
constexpr clap_id kMotionFlowParamId = 24;
constexpr clap_id kMotionShearParamId = 25;
constexpr clap_id kMotionCurlParamId = 26;
constexpr clap_id kMotionUpdraftParamId = 27;
constexpr clap_id kAzimuthParamId = 28;
constexpr clap_id kElevationParamId = 29;
constexpr clap_id kDistanceParamId = 30;
constexpr clap_id kSpatialFollowParamId = 31;
constexpr clap_id kOutputParamId = 32;
constexpr clap_id kFieldParamId = 33;
constexpr clap_id kPwmAParamId = 37;
constexpr clap_id kPwmBParamId = 38;
constexpr clap_id kRateModeBParamId = 43;
constexpr clap_id kGustEdgeParamId = 46;
constexpr clap_id kPlaceParamId = 47;
constexpr clap_id kSpaceParamId = 48;
constexpr clap_id kEnvironmentSizeParamId = 49;
constexpr clap_id kEnvironmentDecayParamId = 50;
constexpr clap_id kEnvironmentDampingParamId = 51;
constexpr clap_id kFieldListenModeParamId = 52;
constexpr clap_id kFieldListenAmountParamId = 53;
constexpr clap_id kFieldListenResponseParamId = 54;
constexpr clap_id kSurfaceXParamId = 55;
constexpr clap_id kSurfaceYParamId = 56;
constexpr clap_id kParticlesParamId = 57;
constexpr clap_id kVortexParamId = 58;
constexpr clap_id kPressureParamId = 59;
constexpr uint32_t kParamCount = 59u;
constexpr clap_id kRandomizeActionId = CLAP_INVALID_ID - 1u;
constexpr clap_id kReplaceCustomStateActionId = CLAP_INVALID_ID - 3u;
constexpr clap_id kReplaceSavedStateActionId = CLAP_INVALID_ID - 4u;

using WindSurface = s3g::ParameterSurfaceState<s3g::AmbiWindParams>;

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiWindParams params {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    WindSurface surface {};
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiWindParams params {};
};

struct ControlSnapshot {
    s3g::AmbiWindParams params {};
    s3g::AmbiWindParams effectiveParams {};
    WindSurface surface {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

struct StateCommand {
    s3g::AmbiWindParams params {};
    WindSurface surface {};
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

template <typename T, uint32_t Capacity>
class GuiCommandQueue {
public:
    bool canPush(uint32_t count = 1u) const noexcept
    {
        const uint32_t write = writeIndex_.load(std::memory_order_relaxed);
        const uint32_t read = readIndex_.load(std::memory_order_acquire);
        const uint32_t used = write >= read
            ? write - read : Capacity - (read - write);
        return count <= Capacity - 1u - used;
    }

    bool push(const T& command) noexcept
    {
        const uint32_t write = writeIndex_.load(std::memory_order_relaxed);
        const uint32_t next = (write + 1u) % Capacity;
        if (next == readIndex_.load(std::memory_order_acquire)) return false;
        commands_[write] = command;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    bool peek(T& command) const noexcept
    {
        const uint32_t read = readIndex_.load(std::memory_order_relaxed);
        if (read == writeIndex_.load(std::memory_order_acquire)) return false;
        command = commands_[read];
        return true;
    }

    const T* front() const noexcept
    {
        const uint32_t read = readIndex_.load(std::memory_order_relaxed);
        if (read == writeIndex_.load(std::memory_order_acquire)) return nullptr;
        return &commands_[read];
    }

    void pop() noexcept
    {
        const uint32_t read = readIndex_.load(std::memory_order_relaxed);
        if (read != writeIndex_.load(std::memory_order_acquire)) {
            readIndex_.store((read + 1u) % Capacity,
                std::memory_order_release);
        }
    }

private:
    std::array<T, Capacity> commands_ {};
    std::atomic<uint32_t> readIndex_ { 0u };
    std::atomic<uint32_t> writeIndex_ { 0u };
};

struct OrderedParamEvent {
    uint64_t sequence = 0u;
    s3g::clap_gui::ParamEvent event {};
};

struct OrderedSurfaceCommand {
    uint64_t sequence = 0u;
    WindSurface surface {};
};

struct OrderedStateCommand {
    uint64_t sequence = 0u;
    clap_id actionId = CLAP_INVALID_ID;
    StateCommand state {};
};

struct OrderedMetadataCommand {
    uint64_t sequence = 0u;
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiWindEncoder engine {};
    s3g::AmbiWindParams params {};
    s3g::AmbiWindParams effectiveParams {};
    WindSurface surface {};
    std::atomic<float> effectiveSurfaceX { 0.5f };
    std::atomic<float> effectiveSurfaceY { 0.5f };
    std::atomic<bool> active { false };
    uint32_t presetIndex = 0u;
    char customPresetName[64] {};
    uint32_t randomSeed = 0x6d2b79f5u;
    std::atomic<float> outputPeak { 0.0f };
    GuiCommandQueue<OrderedParamEvent, 512u> guiParamEvents {};
    GuiCommandQueue<OrderedSurfaceCommand, 32u> guiSurfaceCommands {};
    GuiCommandQueue<OrderedStateCommand, 8u> guiStateCommands {};
    GuiCommandQueue<OrderedMetadataCommand, 32u> guiMetadataCommands {};
    std::atomic<uint64_t> nextGuiCommandSequence { 1u };
    std::atomic<uint64_t> queuedSurfaceCommandSequence { 0u };
    std::atomic<uint64_t> appliedSurfaceCommandSequence { 0u };
    uint64_t pendingSurfacePublicationSequence = 0u;
    std::atomic_flag guiCommandProducerLock = ATOMIC_FLAG_INIT;
    std::atomic_flag controlSnapshotLock = ATOMIC_FLAG_INIT;
    ControlSnapshot publishedControl {};
    std::atomic<bool> publishedParamsDirty { true };
    std::atomic<bool> publishedSurfaceDirty { true };
    std::atomic<bool> pendingParamValuesRescan { false };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    uint32_t guiTelemetryCountdown = 0u;
    int guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiGust {};
    std::array<std::atomic<float>, s3g::kAmbiWindMaxVoices> guiRenderGain {};
    std::atomic<uint32_t> guiVoiceCount { 1u };
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

void lockNonAudio(std::atomic_flag& lock)
{
    while (lock.test_and_set(std::memory_order_acquire)) {
        // Fixed-size snapshots are only waited on by non-audio threads.
    }
}

template <size_t Capacity>
void copyFixedName(char (&destination)[Capacity], const char* source) noexcept
{
    static_assert(Capacity > 0u);
    const char* safe = source ? source : "";
    size_t index = 0u;
    for (; index + 1u < Capacity && safe[index] != '\0'; ++index) {
        destination[index] = safe[index];
    }
    destination[index] = '\0';
}

void publishControlSnapshot(Plugin& plugin)
{
    plugin.publishedParamsDirty.store(true, std::memory_order_release);
    plugin.publishedSurfaceDirty.store(true, std::memory_order_release);
    if (plugin.controlSnapshotLock.test_and_set(
            std::memory_order_acquire)) return;
    plugin.publishedControl.params = plugin.params;
    plugin.publishedControl.effectiveParams = plugin.effectiveParams;
    plugin.publishedControl.surface = plugin.surface;
    plugin.publishedControl.presetIndex = plugin.presetIndex;
    copyFixedName(plugin.publishedControl.customPresetName,
        plugin.customPresetName);
    if (plugin.pendingSurfacePublicationSequence != 0u) {
        plugin.appliedSurfaceCommandSequence.store(
            plugin.pendingSurfacePublicationSequence,
            std::memory_order_release);
    }
    plugin.publishedParamsDirty.store(false, std::memory_order_release);
    plugin.publishedSurfaceDirty.store(false, std::memory_order_release);
    plugin.controlSnapshotLock.clear(std::memory_order_release);
}

void publishParamSnapshot(Plugin& plugin)
{
    plugin.publishedParamsDirty.store(true, std::memory_order_release);
    if (plugin.controlSnapshotLock.test_and_set(
            std::memory_order_acquire)) return;
    plugin.publishedControl.params = plugin.params;
    plugin.publishedControl.effectiveParams = plugin.effectiveParams;
    plugin.publishedControl.presetIndex = plugin.presetIndex;
    copyFixedName(plugin.publishedControl.customPresetName,
        plugin.customPresetName);
    plugin.publishedParamsDirty.store(false, std::memory_order_release);
    plugin.controlSnapshotLock.clear(std::memory_order_release);
}

void retryControlSnapshotPublication(Plugin& plugin)
{
    if (plugin.publishedSurfaceDirty.load(std::memory_order_acquire)) {
        publishControlSnapshot(plugin);
    } else if (plugin.publishedParamsDirty.load(std::memory_order_acquire)) {
        publishParamSnapshot(plugin);
    }
}

ControlSnapshot controlSnapshot(Plugin& plugin)
{
    lockNonAudio(plugin.controlSnapshotLock);
    const auto result = plugin.publishedControl;
    plugin.controlSnapshotLock.clear(std::memory_order_release);
    return result;
}

void requestGuiParamService(Plugin& plugin);

void queueGuiSurface(Plugin& plugin, const WindSurface& surface)
{
    lockNonAudio(plugin.guiCommandProducerLock);
    const uint64_t sequence = plugin.nextGuiCommandSequence.fetch_add(
        1u, std::memory_order_relaxed);
    const bool queued = plugin.guiSurfaceCommands.push({ sequence, surface });
    if (queued) plugin.queuedSurfaceCommandSequence.store(
        sequence, std::memory_order_release);
    plugin.guiCommandProducerLock.clear(std::memory_order_release);
    if (!queued) return;
    requestGuiParamService(plugin);
}

void sanitizeStatePayload(s3g::AmbiWindParams& params,
    WindSurface& surface)
{
    s3g::AmbiWindEncoder sanitizer {};
    sanitizer.setParams(params);
    params = sanitizer.params();
    s3g::sanitizeParameterSurface(surface);
    for (uint32_t index = 0u; index < surface.cellCount; ++index) {
        sanitizer.setParams(surface.cells[index].params);
        surface.cells[index].params = sanitizer.params();
    }
}

bool queueStateCommand(Plugin& plugin,
    s3g::AmbiWindParams params, WindSurface surface,
    uint32_t presetIndex, const char* customName, clap_id actionId)
{
    sanitizeStatePayload(params, surface);
    StateCommand state {};
    state.params = params;
    state.surface = surface;
    state.presetIndex = std::min<uint32_t>(presetIndex,
        s3g::kAmbiWindFactoryPresetCount - 1u);
    copyFixedName(state.customPresetName, customName);

    lockNonAudio(plugin.controlSnapshotLock);
    lockNonAudio(plugin.guiCommandProducerLock);
    const uint64_t sequence = plugin.nextGuiCommandSequence.fetch_add(
        1u, std::memory_order_relaxed);
    const bool queued = plugin.guiStateCommands.push(
        { sequence, actionId, state });
    if (queued) {
        plugin.publishedControl.params = state.params;
        plugin.publishedControl.effectiveParams = state.params;
        plugin.publishedControl.surface = state.surface;
        plugin.publishedControl.presetIndex = state.presetIndex;
        copyFixedName(plugin.publishedControl.customPresetName,
            state.customPresetName);
    }
    plugin.guiCommandProducerLock.clear(std::memory_order_release);
    plugin.controlSnapshotLock.clear(std::memory_order_release);
    if (queued) requestGuiParamService(plugin);
    return queued;
}

bool writeExact(const clap_ostream_t* stream, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->write(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* data, size_t size)
{
    auto* bytes = static_cast<uint8_t*>(data);
    size_t done = 0;
    while (done < size) {
        const int64_t n = stream->read(stream, bytes + done, size - done);
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

bool saveCustomPresetFile(const char* path,
    const s3g::AmbiWindParams& params, const char* name)
{
    if (!path || !*path) return false;
    CustomPresetFile file {};
    std::snprintf(file.name, sizeof(file.name), "%s", name && *name ? name : "Custom");
    file.params = params;
    FILE* handle = std::fopen(path, "wb");
    if (!handle) return false;
    const bool ok = std::fwrite(&file, 1, sizeof(file), handle) == sizeof(file);
    std::fclose(handle);
    return ok;
}

bool loadCustomPresetFile(const char* path, CustomPresetFile& file)
{
    if (!path || !*path) return false;
    FILE* handle = std::fopen(path, "rb");
    if (!handle) return false;
    file = {};
    bool ok = std::fread(&file.magic, 1, sizeof(file.magic), handle) == sizeof(file.magic)
        && std::fread(&file.version, 1, sizeof(file.version), handle) == sizeof(file.version)
        && file.magic == kCustomPresetMagic
        && (file.version == 10u || file.version == 11u || file.version == 12u
            || file.version == 13u || file.version == 14u
            || file.version == kCustomPresetVersion)
        && std::fread(file.name, 1, sizeof(file.name), handle) == sizeof(file.name);
    if (ok) {
        const size_t paramsSize = file.version == 10u
            ? offsetof(s3g::AmbiWindParams, place)
            : (file.version == 11u
                    ? offsetof(s3g::AmbiWindParams, environmentSize)
                    : (file.version == 12u
                            ? offsetof(s3g::AmbiWindParams, fieldListenMode)
                            : (file.version == 13u
                                    ? offsetof(s3g::AmbiWindParams, fieldListenAmount)
                                    : (file.version == 14u
                                            ? offsetof(s3g::AmbiWindParams, particles)
                                            : sizeof(file.params)))));
        ok = std::fread(&file.params, 1, paramsSize, handle) == paramsSize;
    }
    std::fclose(handle);
    return ok;
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
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x00ffffffu);
}

float randomRange(uint32_t& seed, float minValue, float maxValue)
{
    return minValue + (maxValue - minValue) * randomUnit(seed);
}

uint32_t randomChoice(uint32_t& seed, uint32_t count)
{
    return std::min<uint32_t>(count - 1u, static_cast<uint32_t>(randomUnit(seed) * static_cast<float>(count)));
}

constexpr const char* kMaterialNames[] = {
    "OPEN", "LEAF", "HOLLOW", "WIRE", "METAL", "CHIMES", "BLOCKS",
    "HARP", "REEDS", "FABRIC", "SAND", "DEBRIS", "STRUCTURE"
};

constexpr const char* kPlaceNames[] = {
    "OPEN", "CANOPY", "PORCH", "ROOM", "HANGAR", "CANYON", "TUNNEL"
};

constexpr const char* kFieldListenNames[] = {
    "OFF", "FOLLOW", "COUNTER", "BALANCE"
};

constexpr const char* kFieldListenResponseNames[] = {
    "LEGACY", "CHARGE", "SETTLE", "RESONATE"
};

void applyEffectiveParams(Plugin& plugin);

void randomizeSafe(Plugin& plugin)
{
    auto p = plugin.params;
    const uint32_t order = p.order;
    const float outputGainDb = p.outputGainDb;
    const auto fieldListenMode = p.fieldListenMode;
    const float fieldListenAmount = p.fieldListenAmount;
    const auto fieldListenResponse = p.fieldListenResponse;
    uint32_t seed = plugin.randomSeed ^ static_cast<uint32_t>(std::lround(plugin.outputPeak.load(std::memory_order_relaxed) * 1000000.0f));
    p.voices = 10u + randomChoice(seed, 23u);
    p.wind = randomRange(seed, 0.08f, 0.46f);
    p.gustRate = randomRange(seed, 0.025f, 0.52f);
    if (randomUnit(seed) < 0.32f) {
        p.wind *= 0.38f;
        p.gustRate *= 0.44f;
    }
    p.gustDepth = randomRange(seed, 0.04f, 0.48f);
    p.turbulence = randomRange(seed, 0.02f, 0.42f);
    p.flutter = randomRange(seed, 0.03f, 0.58f);
    p.material = randomRange(seed, 0.03f, 0.56f);
    p.air = randomRange(seed, 0.18f, 0.54f);
    p.hiss = randomRange(seed, 0.14f, 0.50f);
    p.sweep = randomRange(seed, 0.24f, 0.76f);
    p.body = randomRange(seed, 0.24f, 0.76f);
    p.spread = randomRange(seed, 0.06f, 0.42f);
    p.deviation = randomRange(seed, 0.025f, 0.22f);
    p.gustShape = randomChoice(seed, 6u);
    p.vectorRateHz = randomRange(seed, 0.004f, 0.080f);
    p.materialMode = randomChoice(seed, s3g::kAmbiWindMaterialCount);
    const bool resonantObject = p.materialMode == 5u || p.materialMode == 6u || p.materialMode == 7u;
    if (resonantObject) {
        p.material = randomRange(seed, 0.18f, 0.66f);
    }
    p.gustEdge = randomUnit(seed) < 0.72f ? 0u : (randomUnit(seed) < 0.62f ? 1u : 2u);
    p.center = randomRange(seed, 0.20f, 0.82f);
    p.sweep = randomRange(seed, 0.16f, 0.78f);
    p.q = resonantObject ? randomRange(seed, 0.22f, 0.62f) : randomRange(seed, 0.14f, 0.54f);
    p.shrill = randomRange(seed, 0.06f, 0.42f);
    p.body = randomRange(seed, 0.12f, 0.56f);
    p.breath = randomRange(seed, 0.08f, 0.54f);
    p.grit = randomRange(seed, 0.02f, 0.26f);
    p.field = randomRange(seed, 0.18f, 0.86f);
    p.motionRateHz = randomRange(seed, 0.008f, 0.220f);
    p.motionFlow = randomRange(seed, 0.12f, 0.72f);
    p.motionShear = randomRange(seed, 0.10f, 0.72f);
    p.motionCurl = randomUnit(seed) < 0.18f ? randomRange(seed, 0.78f, 1.0f) : randomRange(seed, 0.18f, 0.74f);
    p.motionUpdraft = randomUnit(seed) < 0.18f ? randomRange(seed, 0.55f, 1.0f) : randomRange(seed, 0.02f, 0.42f);
    p.centerAzimuthDeg = randomRange(seed, -35.0f, 35.0f);
    p.centerElevationDeg = randomRange(seed, -18.0f, 24.0f);
    p.centerDistance = randomRange(seed, 0.88f, 1.30f);
    p.spatialFollow = randomRange(seed, 0.12f, 0.70f);
    p.place = randomChoice(seed, s3g::kAmbiWindPlaceCount);
    p.space = randomRange(seed, 0.06f, p.place == 0u ? 0.24f : 0.50f);
    p.environmentSize = randomRange(seed, 0.30f, 0.76f);
    p.environmentDecay = randomRange(seed, 0.32f, 0.76f);
    p.environmentDamping = randomRange(seed, 0.24f, 0.74f);
    p.particles = p.materialMode == 10u || p.materialMode == 11u
        ? randomRange(seed, 0.38f, 0.92f) : randomRange(seed, 0.0f, 0.28f);
    p.vortex = randomUnit(seed) < 0.20f
        ? randomRange(seed, 0.48f, 0.94f) : randomRange(seed, 0.0f, 0.26f);
    p.pressure = p.materialMode == 12u
        ? randomRange(seed, 0.44f, 0.92f) : randomRange(seed, 0.0f, 0.34f);
    p.order = order;
    p.outputGainDb = outputGainDb;
    p.fieldListenMode = fieldListenMode;
    p.fieldListenAmount = fieldListenAmount;
    p.fieldListenResponse = fieldListenResponse;

    plugin.randomSeed = seed;
    plugin.params = p;
    plugin.presetIndex = 0u;
    copyFixedName(plugin.customPresetName, "Random");
    s3g::bypassParameterSurfaceForSceneChange(plugin.surface);
    applyEffectiveParams(plugin);
    plugin.engine.beginTransition();
    publishControlSnapshot(plugin);
}

bool assignParam(s3g::AmbiWindParams& params, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: params.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kVoicesParamId: params.voices = static_cast<uint32_t>(std::lround(value)); return true;
    case kRateAParamId: params.wind = static_cast<float>(value); return true;
    case kRateBParamId: params.gustRate = static_cast<float>(value); return true;
    case kFmAtoBParamId: params.gustDepth = static_cast<float>(value); return true;
    case kFmBtoAParamId: params.turbulence = static_cast<float>(value); return true;
    case kFlutterParamId: params.flutter = static_cast<float>(value); return true;
    case kMaterialParamId: params.material = static_cast<float>(value); return true;
    case kPwmAParamId: params.air = static_cast<float>(value); return true;
    case kPwmBParamId: params.hiss = static_cast<float>(value); return true;
    case kSpreadParamId: params.spread = static_cast<float>(value); return true;
    case kDeviationParamId: params.deviation = static_cast<float>(value); return true;
    case kGustShapeParamId: params.gustShape = static_cast<uint32_t>(std::lround(value)); return true;
    case kRateModeAParamId: params.vectorRateHz = static_cast<float>(value); return true;
    case kRateModeBParamId: params.materialMode = static_cast<uint32_t>(std::lround(value)); return true;
    case kGustEdgeParamId: params.gustEdge = static_cast<uint32_t>(std::lround(value)); return true;
    case kThresholdParamId: params.center = static_cast<float>(value); return true;
    case kColorParamId: params.sweep = static_cast<float>(value); return true;
    case kFilterParamId: params.q = static_cast<float>(value); return true;
    case kResonanceParamId: params.shrill = static_cast<float>(value); return true;
    case kFilterRunParamId: params.body = static_cast<float>(value); return true;
    case kFilterSweepParamId: params.breath = static_cast<float>(value); return true;
    case kSaturationParamId: params.grit = static_cast<float>(value); return true;
    case kFieldParamId: params.field = static_cast<float>(value); return true;
    case kMotionRateParamId: params.motionRateHz = static_cast<float>(value); return true;
    case kMotionFlowParamId: params.motionFlow = static_cast<float>(value); return true;
    case kMotionShearParamId: params.motionShear = static_cast<float>(value); return true;
    case kMotionCurlParamId: params.motionCurl = static_cast<float>(value); return true;
    case kMotionUpdraftParamId: params.motionUpdraft = static_cast<float>(value); return true;
    case kAzimuthParamId: params.centerAzimuthDeg = static_cast<float>(value); return true;
    case kElevationParamId: params.centerElevationDeg = static_cast<float>(value); return true;
    case kDistanceParamId: params.centerDistance = static_cast<float>(value); return true;
    case kSpatialFollowParamId: params.spatialFollow = static_cast<float>(value); return true;
    case kOutputParamId: params.outputGainDb = static_cast<float>(value); return true;
    case kPlaceParamId: params.place = static_cast<uint32_t>(std::lround(value)); return true;
    case kSpaceParamId: params.space = static_cast<float>(value); return true;
    case kEnvironmentSizeParamId: params.environmentSize = static_cast<float>(value); return true;
    case kEnvironmentDecayParamId: params.environmentDecay = static_cast<float>(value); return true;
    case kEnvironmentDampingParamId: params.environmentDamping = static_cast<float>(value); return true;
    case kFieldListenModeParamId:
        params.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
            static_cast<uint32_t>(std::lround(value)));
        return true;
    case kFieldListenAmountParamId:
        params.fieldListenAmount = static_cast<float>(value);
        return true;
    case kFieldListenResponseParamId:
        params.fieldListenResponse =
            static_cast<s3g::AmbiFieldListenerResponse>(
                static_cast<uint32_t>(std::lround(value)));
        return true;
    case kSurfaceXParamId: params.surfaceX = static_cast<float>(value); return true;
    case kSurfaceYParamId: params.surfaceY = static_cast<float>(value); return true;
    case kParticlesParamId: params.particles = static_cast<float>(value); return true;
    case kVortexParamId: params.vortex = static_cast<float>(value); return true;
    case kPressureParamId: params.pressure = static_cast<float>(value); return true;
    default: return false;
    }
}

s3g::AmbiWindParams windSurfaceParams(
    const Plugin& plugin, float cursorX, float cursorY)
{
    const auto& base = plugin.params;
    if (!plugin.surface.enabled || plugin.surface.cellCount < 2u) return base;
    const auto weights = s3g::parameterSurfaceWeights(
        plugin.surface, cursorX, cursorY);
    if (weights.activeCount < 2u) return base;
    const auto& nearest = s3g::parameterSurfaceNearestParams(
        plugin.surface, weights, base);
    auto result = nearest;
#define S3G_WIND_SURFACE_BLEND(member) \
    result.member = s3g::parameterSurfaceBlend(plugin.surface, weights, \
        [](const s3g::AmbiWindParams& p) { return p.member; }, base.member)
    S3G_WIND_SURFACE_BLEND(wind);
    S3G_WIND_SURFACE_BLEND(gustRate);
    S3G_WIND_SURFACE_BLEND(gustDepth);
    S3G_WIND_SURFACE_BLEND(turbulence);
    S3G_WIND_SURFACE_BLEND(flutter);
    S3G_WIND_SURFACE_BLEND(material);
    S3G_WIND_SURFACE_BLEND(air);
    S3G_WIND_SURFACE_BLEND(hiss);
    S3G_WIND_SURFACE_BLEND(spread);
    S3G_WIND_SURFACE_BLEND(deviation);
    S3G_WIND_SURFACE_BLEND(vectorRateHz);
    S3G_WIND_SURFACE_BLEND(center);
    S3G_WIND_SURFACE_BLEND(sweep);
    S3G_WIND_SURFACE_BLEND(q);
    S3G_WIND_SURFACE_BLEND(shrill);
    S3G_WIND_SURFACE_BLEND(body);
    S3G_WIND_SURFACE_BLEND(breath);
    S3G_WIND_SURFACE_BLEND(grit);
    S3G_WIND_SURFACE_BLEND(field);
    S3G_WIND_SURFACE_BLEND(motionRateHz);
    S3G_WIND_SURFACE_BLEND(motionFlow);
    S3G_WIND_SURFACE_BLEND(motionShear);
    S3G_WIND_SURFACE_BLEND(motionCurl);
    S3G_WIND_SURFACE_BLEND(motionUpdraft);
    result.centerAzimuthDeg = s3g::parameterSurfaceBlendAngleDegrees(
        plugin.surface, weights,
        [](const s3g::AmbiWindParams& p) { return p.centerAzimuthDeg; },
        base.centerAzimuthDeg);
    S3G_WIND_SURFACE_BLEND(centerElevationDeg);
    S3G_WIND_SURFACE_BLEND(centerDistance);
    S3G_WIND_SURFACE_BLEND(spatialFollow);
    S3G_WIND_SURFACE_BLEND(space);
    S3G_WIND_SURFACE_BLEND(environmentSize);
    S3G_WIND_SURFACE_BLEND(environmentDecay);
    S3G_WIND_SURFACE_BLEND(environmentDamping);
    S3G_WIND_SURFACE_BLEND(fieldListenAmount);
    S3G_WIND_SURFACE_BLEND(particles);
    S3G_WIND_SURFACE_BLEND(vortex);
    S3G_WIND_SURFACE_BLEND(pressure);
#undef S3G_WIND_SURFACE_BLEND
    // Routing, final level, and cursor remain live.
    result.order = base.order;
    result.outputGainDb = base.outputGainDb;
    result.surfaceX = base.surfaceX;
    result.surfaceY = base.surfaceY;
    return result;
}

s3g::AmbiWindParams windSurfaceParams(const Plugin& plugin)
{
    if (!plugin.active.load(std::memory_order_acquire)) {
        return windSurfaceParams(
            plugin, plugin.params.surfaceX, plugin.params.surfaceY);
    }
    return windSurfaceParams(plugin,
        plugin.effectiveSurfaceX.load(std::memory_order_relaxed),
        plugin.effectiveSurfaceY.load(std::memory_order_relaxed));
}

void snapSurfaceCursor(Plugin& plugin)
{
    plugin.effectiveSurfaceX.store(
        plugin.params.surfaceX, std::memory_order_relaxed);
    plugin.effectiveSurfaceY.store(
        plugin.params.surfaceY, std::memory_order_relaxed);
}

bool advanceSurfaceCursor(Plugin& plugin, float deltaSeconds)
{
    const bool gliding = plugin.surface.enabled
        && plugin.surface.cellCount >= 2u
        && plugin.surface.glideMs > 0.0f;
    const float glideMs = gliding ? plugin.surface.glideMs : 0.0f;
    const float currentX = plugin.effectiveSurfaceX.load(
        std::memory_order_relaxed);
    const float currentY = plugin.effectiveSurfaceY.load(
        std::memory_order_relaxed);
    const float nextX = s3g::parameterSurfaceGlideValue(
        currentX, plugin.params.surfaceX, glideMs, deltaSeconds);
    const float nextY = s3g::parameterSurfaceGlideValue(
        currentY, plugin.params.surfaceY, glideMs, deltaSeconds);
    plugin.effectiveSurfaceX.store(nextX,
        std::memory_order_relaxed);
    plugin.effectiveSurfaceY.store(nextY,
        std::memory_order_relaxed);
    return std::fabs(nextX - currentX) > 1.0e-7f
        || std::fabs(nextY - currentY) > 1.0e-7f;
}

void applyEffectiveParams(Plugin& plugin)
{
    plugin.engine.setParameterSurfaceGlideMs(
        plugin.surface.enabled && plugin.surface.cellCount >= 2u
            ? plugin.surface.glideMs : 0.0f);
    const bool audioActive = plugin.active.load(
        std::memory_order_acquire);
    const float cursorX = audioActive
        ? plugin.effectiveSurfaceX.load(std::memory_order_relaxed)
        : plugin.params.surfaceX;
    const float cursorY = audioActive
        ? plugin.effectiveSurfaceY.load(std::memory_order_relaxed)
        : plugin.params.surfaceY;
    plugin.engine.setParams(windSurfaceParams(
        plugin, cursorX, cursorY));
    if (plugin.surface.enabled && plugin.surface.cellCount >= 2u) {
        const auto weights = s3g::parameterSurfaceWeights(
            plugin.surface, cursorX, cursorY);
        plugin.engine.setParameterSurfaceVoiceMembership(
            s3g::parameterSurfaceVoiceMembership<
                s3g::kAmbiWindMaxVoices>(
                plugin.surface, weights, plugin.params.voices));
    } else {
        plugin.engine.clearParameterSurfaceVoiceMembership();
    }
    plugin.effectiveParams = plugin.engine.params();
    publishParamSnapshot(plugin);
}

void sanitizeWindState(Plugin& plugin)
{
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
    s3g::sanitizeParameterSurface(plugin.surface);
    for (uint32_t index = 0u; index < plugin.surface.cellCount; ++index) {
        plugin.engine.setParams(plugin.surface.cells[index].params);
        plugin.surface.cells[index].params = plugin.engine.params();
    }
    applyEffectiveParams(plugin);
    publishControlSnapshot(plugin);
}

void requestSurfaceProcess(Plugin& plugin)
{
    if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void applyParam(Plugin& p, clap_id id, double value)
{
    if (id == kPresetParamId) {
        p.presetIndex = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u, s3g::kAmbiWindFactoryPresetCount - 1u);
        p.customPresetName[0] = '\0';
        const float outputGainDb = p.params.outputGainDb;
        p.params = s3g::ambiWindFactoryPreset(p.presetIndex);
        p.params.outputGainDb = outputGainDb;
        applyEffectiveParams(p);
        p.engine.beginTransition();
        return;
    }
    if (!assignParam(p.params, id, value)) return;
    applyEffectiveParams(p);
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiParamEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    lockNonAudio(plugin.guiCommandProducerLock);
    const uint64_t sequence = plugin.nextGuiCommandSequence.fetch_add(
        1u, std::memory_order_relaxed);
    const bool queued = plugin.guiParamEvents.push(
        { sequence, { kind, id, value } });
    plugin.guiCommandProducerLock.clear(std::memory_order_release);
    if (!queued) return false;
    requestGuiParamService(plugin);
    return true;
}

void queueGuiParamValue(Plugin& plugin, clap_id id, double value)
{
    if (id >= kReplaceSavedStateActionId) {
        (void)queueGuiParamEvent(plugin,
            s3g::clap_gui::ParamEventKind::Value, id, value);
        return;
    }
    lockNonAudio(plugin.guiCommandProducerLock);
    const uint64_t first = plugin.nextGuiCommandSequence.fetch_add(
        3u, std::memory_order_relaxed);
    const bool queued = plugin.guiParamEvents.canPush(3u)
        && plugin.guiParamEvents.push(
            { first, { s3g::clap_gui::ParamEventKind::GestureBegin,
                         id, 0.0 } })
        && plugin.guiParamEvents.push(
            { first + 1u, { s3g::clap_gui::ParamEventKind::Value,
                              id, value } })
        && plugin.guiParamEvents.push(
            { first + 2u, { s3g::clap_gui::ParamEventKind::GestureEnd,
                              id, 0.0 } });
    plugin.guiCommandProducerLock.clear(std::memory_order_release);
    if (queued) requestGuiParamService(plugin);
}

bool queueMetadataCommand(Plugin& plugin, uint32_t presetIndex,
    const char* customName)
{
    OrderedMetadataCommand command {};
    command.presetIndex = presetIndex;
    copyFixedName(command.customPresetName, customName);
    lockNonAudio(plugin.guiCommandProducerLock);
    command.sequence = plugin.nextGuiCommandSequence.fetch_add(
        1u, std::memory_order_relaxed);
    const bool queued = plugin.guiMetadataCommands.push(command);
    plugin.guiCommandProducerLock.clear(std::memory_order_release);
    if (queued) requestGuiParamService(plugin);
    return queued;
}

bool pushGuiParamEvent(const clap_output_events_t* out,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (pending.paramId > kParamCount) return true;
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
    event.header.type = pending.kind
            == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return out->try_push(out, &event.header);
}

void requestParamValuesRescan(Plugin& plugin)
{
    plugin.pendingParamValuesRescan.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_callback) {
        plugin.host->request_callback(plugin.host);
    }
}

void applyStateCommand(Plugin& plugin, const StateCommand& command)
{
    plugin.params = command.params;
    plugin.surface = command.surface;
    plugin.presetIndex = std::min<uint32_t>(command.presetIndex,
        s3g::kAmbiWindFactoryPresetCount - 1u);
    copyFixedName(plugin.customPresetName, command.customPresetName);
    sanitizeWindState(plugin);
    plugin.engine.beginTransition();
    publishControlSnapshot(plugin);
    requestParamValuesRescan(plugin);
}

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* out)
{
    for (;;) {
        const auto* paramCommand = plugin.guiParamEvents.front();
        const auto* surfaceCommand = plugin.guiSurfaceCommands.front();
        const auto* stateCommand = plugin.guiStateCommands.front();
        const auto* metadataCommand = plugin.guiMetadataCommands.front();
        if (!paramCommand && !surfaceCommand && !stateCommand
            && !metadataCommand) break;
        const uint64_t paramSequence = paramCommand ? paramCommand->sequence : UINT64_MAX;
        const uint64_t surfaceSequence = surfaceCommand ? surfaceCommand->sequence : UINT64_MAX;
        const uint64_t stateSequence = stateCommand ? stateCommand->sequence : UINT64_MAX;
        const uint64_t metadataSequence = metadataCommand
            ? metadataCommand->sequence : UINT64_MAX;
        if (surfaceSequence < paramSequence && surfaceSequence < stateSequence
            && surfaceSequence < metadataSequence) {
            plugin.surface = surfaceCommand->surface;
            s3g::sanitizeParameterSurface(plugin.surface);
            applyEffectiveParams(plugin);
            plugin.pendingSurfacePublicationSequence = surfaceSequence;
            publishControlSnapshot(plugin);
            plugin.guiSurfaceCommands.pop();
            continue;
        }
        if (stateSequence < paramSequence && stateSequence < metadataSequence) {
            applyStateCommand(plugin, stateCommand->state);
            plugin.guiStateCommands.pop();
            continue;
        }
        if (metadataSequence < paramSequence) {
            plugin.presetIndex = metadataCommand->presetIndex;
            copyFixedName(plugin.customPresetName,
                metadataCommand->customPresetName);
            publishControlSnapshot(plugin);
            requestParamValuesRescan(plugin);
            plugin.guiMetadataCommands.pop();
            continue;
        }

        const auto& pending = paramCommand->event;
        const bool internalAction = pending.paramId >= kReplaceSavedStateActionId;
        if (!internalAction && !pushGuiParamEvent(out, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            if (pending.paramId == kRandomizeActionId) {
                randomizeSafe(plugin);
                requestParamValuesRescan(plugin);
            } else {
                applyParam(plugin, pending.paramId, pending.value);
            }
        }
        plugin.guiParamEvents.pop();
    }
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
void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    }
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->active.store(false, std::memory_order_release);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    snapSurfaceCursor(*p);
    sanitizeWindState(*p);
    p->active.store(true, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->active.store(false, std::memory_order_release);
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    snapSurfaceCursor(*p);
    applyEffectiveParams(*p);
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void readParamEvents(Plugin& p, const clap_input_events_t* in)
{
    if (!in) return;
    bool paramsChanged = false;
    bool presetChanged = false;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* ev = in->get(in, i);
        if (ev && ev->space_id == CLAP_CORE_EVENT_SPACE_ID && ev->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param = reinterpret_cast<const clap_event_param_value_t*>(ev);
            if (param->param_id == kPresetParamId) {
                p.presetIndex = std::clamp<uint32_t>(
                    static_cast<uint32_t>(std::lround(param->value)), 0u,
                    s3g::kAmbiWindFactoryPresetCount - 1u);
                p.customPresetName[0] = '\0';
                const float outputGainDb = p.params.outputGainDb;
                p.params = s3g::ambiWindFactoryPreset(p.presetIndex);
                p.params.outputGainDb = outputGainDb;
                paramsChanged = true;
                presetChanged = true;
            } else {
                paramsChanged |= assignParam(
                    p.params, param->param_id, param->value);
            }
        }
    }
    if (paramsChanged) {
        applyEffectiveParams(p);
        if (presetChanged) p.engine.beginTransition();
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* proc)
{
    auto* p = self(plugin);
    readParamEvents(*p, proc->in_events);
    serviceGuiParamEvents(*p, proc->out_events);
    retryControlSnapshotPublication(*p);
    if (proc->audio_outputs_count == 0) return CLAP_PROCESS_CONTINUE;
    auto& output = proc->audio_outputs[0];
    const uint32_t frames = proc->frames_count;
    const uint32_t outChannels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (!output.data32 || outChannels == 0u) return CLAP_PROCESS_CONTINUE;

    constexpr uint32_t kSurfaceControlFrames = 64u;
    uint32_t surfaceOffset = 0u;
    while (surfaceOffset < frames) {
        const float currentX = p->effectiveSurfaceX.load(
            std::memory_order_relaxed);
        const float currentY = p->effectiveSurfaceY.load(
            std::memory_order_relaxed);
        const bool cursorMoving = p->surface.enabled
            && p->surface.cellCount >= 2u
            && p->surface.glideMs > 0.0f
            && (std::fabs(currentX - p->params.surfaceX) > 1.0e-6f
                || std::fabs(currentY - p->params.surfaceY) > 1.0e-6f);
        const uint32_t spanFrames = cursorMoving
            ? std::min<uint32_t>(kSurfaceControlFrames,
                frames - surfaceOffset)
            : frames - surfaceOffset;
        if (advanceSurfaceCursor(*p,
                static_cast<float>(spanFrames)
                    / static_cast<float>(p->sampleRate))) {
            applyEffectiveParams(*p);
        }
        std::array<float*, kOutputChannels> spanOutputs {};
        for (uint32_t ch = 0u; ch < outChannels; ++ch) {
            spanOutputs[ch] = output.data32[ch]
                ? output.data32[ch] + surfaceOffset : nullptr;
        }
        p->engine.process(spanOutputs.data(), outChannels, spanFrames);
        surfaceOffset += spanFrames;
    }
    p->effectiveParams = p->engine.params();
    s3g::clearAudioBufferFromChannel(output, outChannels, frames);

#if defined(__APPLE__)
    const bool guiIsVisible = p->guiVisible.load(std::memory_order_acquire);
    if (guiIsVisible && p->guiTelemetryCountdown <= frames) {
        float peak = 0.0f;
        for (uint32_t ch = 0u; ch < outChannels; ++ch) {
            if (!output.data32[ch]) continue;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                peak = std::max(peak, std::fabs(output.data32[ch][frame]));
            }
        }
        p->outputPeak.store(std::max(
            p->outputPeak.load(std::memory_order_relaxed) * 0.90f, peak),
            std::memory_order_relaxed);
        const uint32_t voices = std::min<uint32_t>(
            p->engine.processingVoiceCount(), s3g::kAmbiWindMaxVoices);
        p->guiVoiceCount.store(voices, std::memory_order_relaxed);
        for (uint32_t voice = 0u; voice < voices; ++voice) {
            const auto point = p->engine.voicePoint(voice);
            p->guiAzimuth[voice].store(point.azimuthDeg, std::memory_order_relaxed);
            p->guiElevation[voice].store(point.elevationDeg, std::memory_order_relaxed);
            p->guiDistance[voice].store(point.distance, std::memory_order_relaxed);
            p->guiEnergy[voice].store(p->engine.voiceEnergy(voice), std::memory_order_relaxed);
            p->guiGust[voice].store(p->engine.voiceGustLevel(voice), std::memory_order_relaxed);
            p->guiRenderGain[voice].store(
                p->engine.voiceRenderGain(voice),
                std::memory_order_relaxed);
        }
        p->guiTelemetryCountdown = std::max<uint32_t>(
            1u, static_cast<uint32_t>(p->sampleRate / 30.0));
    } else if (guiIsVisible) {
        p->guiTelemetryCountdown -= frames;
    } else {
        p->guiTelemetryCountdown = 0u;
    }
#endif
    retryControlSnapshotPublication(*p);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->pendingParamValuesRescan.exchange(false,
            std::memory_order_acq_rel)
        && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    info->id = 20;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

struct ParamDef { clap_id id; const char* name; double min; double max; double def; bool stepped; };
constexpr ParamDef kParams[] {
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiWindFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Order", 1.0, 7.0, 3.0, true },
    { kVoicesParamId, "Voices", 1.0, 64.0, 16.0, true },
    { kRateAParamId, "Wind", 0.0, 1.0, 0.55, false },
    { kRateBParamId, "Gust Rate", 0.0, 1.0, 0.20, false },
    { kFmAtoBParamId, "Gust Depth", 0.0, 1.0, 0.48, false },
    { kFmBtoAParamId, "Turbulence", 0.0, 1.0, 0.36, false },
    { kFlutterParamId, "Flutter", 0.0, 1.0, 0.28, false },
    { kMaterialParamId, "Material", 0.0, 1.0, 0.34, false },
    { kSpreadParamId, "Spread", 0.0, 1.0, 0.26, false },
    { kDeviationParamId, "Deviation", 0.0, 1.0, 0.12, false },
    { kGustShapeParamId, "Gust Shape", 0.0, 5.0, 2.0, true },
    { kRateModeAParamId, "Vector LFO", 0.0, 0.5, 0.024, false },
    { kRateModeBParamId, "Material Type", 0.0, static_cast<double>(s3g::kAmbiWindMaterialCount - 1u), 0.0, true },
    { kGustEdgeParamId, "Gust Edge", 0.0, 2.0, 0.0, true },
    { kThresholdParamId, "Center", 0.0, 1.0, 0.38, false },
    { kColorParamId, "Sweep", 0.0, 1.0, 0.48, false },
    { kFilterParamId, "Q", 0.0, 1.0, 0.42, false },
    { kResonanceParamId, "Shrill", 0.0, 1.0, 0.24, false },
    { kFilterRunParamId, "Body", 0.0, 1.0, 0.52, false },
    { kFilterSweepParamId, "Breath", 0.0, 1.0, 0.36, false },
    { kSaturationParamId, "Grit", 0.0, 1.0, 0.18, false },
    { kMotionRateParamId, "Field Rate", 0.001, 2.0, 0.024, false },
    { kMotionFlowParamId, "Flow Push", 0.0, 1.0, 0.24, false },
    { kMotionShearParamId, "Shear", 0.0, 1.0, 0.22, false },
    { kMotionCurlParamId, "Curl", 0.0, 1.0, 0.32, false },
    { kMotionUpdraftParamId, "Updraft", 0.0, 1.0, 0.05, false },
    { kAzimuthParamId, "Direction", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Elevation", -90.0, 90.0, 0.0, false },
    { kDistanceParamId, "Range", 0.15, 2.0, 1.0, false },
    { kSpatialFollowParamId, "Inertia", 0.0, 1.0, 0.90, false },
    { kOutputParamId, "Output", -60.0, 12.0, -6.0, false },
    { kFieldParamId, "Depth Push", 0.0, 1.0, 0.30, false },
    { kPwmAParamId, "Air", 0.0, 1.0, 0.42, false },
    { kPwmBParamId, "Hiss", 0.0, 1.0, 0.34, false },
    { kPlaceParamId, "Place", 0.0, static_cast<double>(s3g::kAmbiWindPlaceCount - 1u), 0.0, true },
    { kSpaceParamId, "Env Return", 0.0, 1.0, 0.14, false },
    { kEnvironmentSizeParamId, "Env Size", 0.0, 1.0, 0.5, false },
    { kEnvironmentDecayParamId, "Env Decay", 0.0, 1.0, 0.5, false },
    { kEnvironmentDampingParamId, "Env Damping", 0.0, 1.0, 0.5, false },
    { kFieldListenModeParamId, "Field Listen", 0.0, 3.0, 0.0, true },
    { kFieldListenAmountParamId, "Listen Amount", 0.0, 1.0, 1.0, false },
    { kFieldListenResponseParamId, "Listen Response", 0.0, 3.0, 0.0, true },
    { kSurfaceXParamId, "Surface X", 0.0, 1.0, 0.5, false },
    { kSurfaceYParamId, "Surface Y", 0.0, 1.0, 0.5, false },
    { kParticlesParamId, "Particles", 0.0, 1.0, 0.0, false },
    { kVortexParamId, "Vortex", 0.0, 1.0, 0.0, false },
    { kPressureParamId, "Pressure", 0.0, 1.0, 0.0, false },
};

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

const char* paramModule(clap_id id)
{
    switch (id) {
    case kPresetParamId: return "Global";
    case kOrderParamId:
    case kRateModeAParamId:
    case kRateModeBParamId:
    case kVoicesParamId:
    case kRateAParamId:
    case kRateBParamId:
    case kSpreadParamId:
    case kDeviationParamId: return "Source and Gust";
    case kGustEdgeParamId:
    case kFmAtoBParamId:
    case kFmBtoAParamId:
    case kFlutterParamId:
    case kMaterialParamId:
    case kGustShapeParamId:
    case kThresholdParamId: return "Gust and Material";
    case kColorParamId:
    case kFilterParamId:
    case kResonanceParamId:
    case kFilterRunParamId:
    case kFilterSweepParamId:
    case kSaturationParamId: return "Filter and Tone";
    case kPwmAParamId:
    case kPwmBParamId:
    case kOutputParamId: return "Air and Output";
    case kMotionRateParamId:
    case kMotionFlowParamId:
    case kMotionShearParamId:
    case kMotionCurlParamId:
    case kMotionUpdraftParamId:
    case kFieldParamId:
    case kSpatialFollowParamId: return "Microweather Motion";
    case kAzimuthParamId:
    case kElevationParamId:
    case kDistanceParamId: return "Macro Wind Vector";
    case kPlaceParamId:
    case kSpaceParamId:
    case kEnvironmentSizeParamId:
    case kEnvironmentDecayParamId:
    case kEnvironmentDampingParamId:
    case kFieldListenModeParamId:
    case kFieldListenAmountParamId:
    case kFieldListenResponseParamId: return "Environment Field";
    case kSurfaceXParamId:
    case kSurfaceYParamId: return "Parameter Surface";
    case kParticlesParamId:
    case kVortexParamId:
    case kPressureParamId: return "Weather Objects";
    default: return "Ambi Wind Encoder";
    }
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= paramsCount(nullptr)) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, paramModule(def.id), sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value) return false;
    auto* p = self(plugin);
    const auto snapshot = controlSnapshot(*p);
    const auto params = snapshot.params;
    switch (id) {
    case kPresetParamId: *value = snapshot.presetIndex; return true;
    case kOrderParamId: *value = params.order; return true;
    case kVoicesParamId: *value = params.voices; return true;
    case kRateAParamId: *value = params.wind; return true;
    case kRateBParamId: *value = params.gustRate; return true;
    case kFmAtoBParamId: *value = params.gustDepth; return true;
    case kFmBtoAParamId: *value = params.turbulence; return true;
    case kFlutterParamId: *value = params.flutter; return true;
    case kMaterialParamId: *value = params.material; return true;
    case kPwmAParamId: *value = params.air; return true;
    case kPwmBParamId: *value = params.hiss; return true;
    case kSpreadParamId: *value = params.spread; return true;
    case kDeviationParamId: *value = params.deviation; return true;
    case kGustShapeParamId: *value = params.gustShape; return true;
    case kRateModeAParamId: *value = params.vectorRateHz; return true;
    case kRateModeBParamId: *value = params.materialMode; return true;
    case kGustEdgeParamId: *value = params.gustEdge; return true;
    case kThresholdParamId: *value = params.center; return true;
    case kColorParamId: *value = params.sweep; return true;
    case kFilterParamId: *value = params.q; return true;
    case kResonanceParamId: *value = params.shrill; return true;
    case kFilterRunParamId: *value = params.body; return true;
    case kFilterSweepParamId: *value = params.breath; return true;
    case kSaturationParamId: *value = params.grit; return true;
    case kMotionRateParamId: *value = params.motionRateHz; return true;
    case kMotionFlowParamId: *value = params.motionFlow; return true;
    case kMotionShearParamId: *value = params.motionShear; return true;
    case kMotionCurlParamId: *value = params.motionCurl; return true;
    case kMotionUpdraftParamId: *value = params.motionUpdraft; return true;
    case kAzimuthParamId: *value = params.centerAzimuthDeg; return true;
    case kElevationParamId: *value = params.centerElevationDeg; return true;
    case kDistanceParamId: *value = params.centerDistance; return true;
    case kSpatialFollowParamId: *value = params.spatialFollow; return true;
    case kOutputParamId: *value = params.outputGainDb; return true;
    case kFieldParamId: *value = params.field; return true;
    case kPlaceParamId: *value = params.place; return true;
    case kSpaceParamId: *value = params.space; return true;
    case kEnvironmentSizeParamId: *value = params.environmentSize; return true;
    case kEnvironmentDecayParamId: *value = params.environmentDecay; return true;
    case kEnvironmentDampingParamId: *value = params.environmentDamping; return true;
    case kFieldListenModeParamId: *value = static_cast<uint32_t>(params.fieldListenMode); return true;
    case kFieldListenAmountParamId: *value = params.fieldListenAmount; return true;
    case kFieldListenResponseParamId:
        *value = static_cast<uint32_t>(params.fieldListenResponse);
        return true;
    case kSurfaceXParamId: *value = params.surfaceX; return true;
    case kSurfaceYParamId: *value = params.surfaceY; return true;
    case kParticlesParamId: *value = params.particles; return true;
    case kVortexParamId: *value = params.vortex; return true;
    case kPressureParamId: *value = params.pressure; return true;
    default: return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", s3g::ambiWindFactoryPresetInfo(static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kRateModeAParamId) {
        std::snprintf(display, size, "%.3f Hz", value);
    } else if (id == kRateModeBParamId) {
        std::snprintf(display, size, "%s", kMaterialNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)),
            s3g::kAmbiWindMaterialCount - 1u)]);
    } else if (id == kPlaceParamId) {
        std::snprintf(display, size, "%s", kPlaceNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), s3g::kAmbiWindPlaceCount - 1u)]);
    } else if (id == kFieldListenModeParamId) {
        std::snprintf(display, size, "%s", kFieldListenNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kFieldListenResponseParamId) {
        std::snprintf(display, size, "%s", kFieldListenResponseNames[
            std::min<uint32_t>(
                static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kGustShapeParamId) {
        static constexpr const char* kGustShapeNames[] = { "SINE", "SWELL", "SURGE", "TRI", "BLAST", "GATE" };
        std::snprintf(display, size, "%s", kGustShapeNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 5u)]);
    } else if (id == kGustEdgeParamId) {
        static constexpr const char* kEdgeNames[] = { "SOFT", "BEND", "HARD" };
        std::snprintf(display, size, "%s", kEdgeNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 2u)]);
    } else if (id == kMotionRateParamId) {
        std::snprintf(display, size, "%.3f Hz", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kRateAParamId || id == kRateBParamId) {
        std::snprintf(display, size, value < 0.01 ? "%.3f%%" : "%.2f%%", value * 100.0);
    } else if (id == kFmAtoBParamId || id == kFmBtoAParamId
        || id == kFlutterParamId || id == kMaterialParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kPwmAParamId || id == kPwmBParamId
        || id == kThresholdParamId || id == kColorParamId || id == kFilterParamId || id == kResonanceParamId
        || id == kFilterRunParamId || id == kFilterSweepParamId || id == kSaturationParamId
        || id == kFieldParamId
        || id == kMotionFlowParamId || id == kMotionShearParamId || id == kMotionUpdraftParamId
        || id == kSpatialFollowParamId || id == kSpaceParamId
        || id == kEnvironmentSizeParamId || id == kEnvironmentDecayParamId
        || id == kEnvironmentDampingParamId
        || id == kFieldListenAmountParamId || id == kParticlesParamId
        || id == kVortexParamId || id == kPressureParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else {
        std::snprintf(display, size, "%.2f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    if (!display || !value) return false;

    if (id == kPresetParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWindFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiWindFactoryPresetInfo(index).name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }
    if (id == kGustEdgeParamId) {
        static constexpr const char* edgeNames[] = { "SOFT", "BEND", "HARD" };
        for (uint32_t index = 0u; index < std::size(edgeNames); ++index) {
            if (std::strcmp(display, edgeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kGustShapeParamId) {
        static constexpr const char* shapeNames[] = { "SINE", "SWELL", "SURGE", "TRI", "BLAST", "GATE" };
        for (uint32_t index = 0u; index < std::size(shapeNames); ++index) {
            if (std::strcmp(display, shapeNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kRateModeBParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWindMaterialCount; ++index) {
            if (std::strcmp(display, kMaterialNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kPlaceParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiWindPlaceCount; ++index) {
            if (std::strcmp(display, kPlaceNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kFieldListenModeParamId) {
        for (uint32_t index = 0u; index < std::size(kFieldListenNames); ++index) {
            if (std::strcmp(display, kFieldListenNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kFieldListenResponseParamId) {
        for (uint32_t index = 0u;
            index < std::size(kFieldListenResponseNames); ++index) {
            if (std::strcmp(display, kFieldListenResponseNames[index]) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }

    *value = std::atof(display);
    if (id == kRateAParamId || id == kRateBParamId || id == kFmAtoBParamId || id == kFmBtoAParamId
        || id == kFlutterParamId || id == kMaterialParamId || id == kSpreadParamId || id == kDeviationParamId
        || id == kPwmAParamId || id == kPwmBParamId
        || id == kThresholdParamId || id == kColorParamId || id == kFilterParamId || id == kResonanceParamId
        || id == kFilterRunParamId || id == kFilterSweepParamId || id == kSaturationParamId
        || id == kFieldParamId
        || id == kMotionFlowParamId || id == kMotionShearParamId || id == kMotionUpdraftParamId
        || id == kSpatialFollowParamId || id == kSpaceParamId
        || id == kEnvironmentSizeParamId || id == kEnvironmentDecayParamId
        || id == kEnvironmentDampingParamId
        || id == kFieldListenAmountParamId || id == kParticlesParamId
        || id == kVortexParamId || id == kPressureParamId) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in,
    const clap_output_events_t* out)
{
    auto* p = self(plugin);
    serviceGuiParamEvents(*p, out);
    readParamEvents(*p, in);
    retryControlSnapshotPublication(*p);
}
const clap_plugin_params_t paramsExt { paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush };

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    const auto snapshot = controlSnapshot(*p);
    SavedState state {};
    state.version = kStateVersion;
    state.params = snapshot.params;
    state.presetIndex = snapshot.presetIndex;
    std::snprintf(state.customPresetName,
        sizeof(state.customPresetName), "%s",
        snapshot.customPresetName);
    state.surface = snapshot.surface;
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    auto* p = self(plugin);
    const auto previous = controlSnapshot(*p);
    SavedState loaded {};
    loaded.version = kStateVersion;
    loaded.params = previous.params;
    loaded.presetIndex = previous.presetIndex;
    std::snprintf(loaded.customPresetName,
        sizeof(loaded.customPresetName), "%s",
        previous.customPresetName);
    loaded.surface = previous.surface;

    if (version == kStateVersion) {
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&loaded)
                    + sizeof(loaded.version),
                sizeof(loaded) - sizeof(loaded.version))) return false;
    } else if (version >= 13u && version <= 17u) {
        loaded.params = {};
        uint32_t presetIndex = 0u;
        char customPresetName[64] {};
        size_t legacyParamsSize = 0u;
        switch (version) {
        case 17u:
            legacyParamsSize = offsetof(s3g::AmbiWindParams, particles);
            break;
        case 16u:
            legacyParamsSize = offsetof(
                s3g::AmbiWindParams, fieldListenAmount);
            break;
        case 15u:
            legacyParamsSize = offsetof(
                s3g::AmbiWindParams, fieldListenMode);
            break;
        case 14u:
            legacyParamsSize = offsetof(
                s3g::AmbiWindParams, environmentSize);
            break;
        default:
            legacyParamsSize = offsetof(s3g::AmbiWindParams, place);
            break;
        }
        if (!readExact(stream, &loaded.params, legacyParamsSize)
            || !readExact(stream, &presetIndex, sizeof(presetIndex))
            || !readExact(stream, customPresetName,
                sizeof(customPresetName))) return false;
        loaded.presetIndex = std::min<uint32_t>(
            presetIndex, s3g::kAmbiWindFactoryPresetCount - 1u);
        std::snprintf(loaded.customPresetName,
            sizeof(loaded.customPresetName), "%s", customPresetName);
    } else {
        return false;
    }

    loaded.presetIndex = std::min<uint32_t>(loaded.presetIndex,
        s3g::kAmbiWindFactoryPresetCount - 1u);
    if (p->active.load(std::memory_order_acquire)) {
        if (!queueStateCommand(*p, loaded.params, loaded.surface,
                loaded.presetIndex, loaded.customPresetName,
                kReplaceSavedStateActionId)) return false;
    } else {
        p->params = loaded.params;
        p->surface = loaded.surface;
        p->presetIndex = loaded.presetIndex;
        std::snprintf(p->customPresetName,
            sizeof(p->customPresetName), "%s",
            loaded.customPresetName);
        sanitizeWindState(*p);
        p->engine.beginTransition();
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

constexpr uint32_t kGuiWidth = 1160;
constexpr uint32_t kGuiHeight = 858;

struct GuiSliderSpec {
    clap_id id;
    CGFloat panelX;
    CGFloat y;
    double min;
    double max;
    bool logarithmic;
};

constexpr GuiSliderSpec kGuiSliders[] {
    { kOutputParamId, 630, 78, -60.0, 12.0, false },
    { kPwmAParamId, 630, 130, 0.0, 1.0, false },
    { kPwmBParamId, 630, 156, 0.0, 1.0, false },
    { kRateModeAParamId, 630, 222, 0.0, 0.5, true },
    { kVoicesParamId, 630, 274, 1.0, 64.0, false },
    { kRateAParamId, 630, 300, 0.0, 1.0, false },
    { kRateBParamId, 630, 326, 0.0, 1.0, false },
    { kSpreadParamId, 630, 352, 0.0, 1.0, false },
    { kDeviationParamId, 630, 378, 0.0, 1.0, false },
    { kFmAtoBParamId, 630, 470, 0.0, 1.0, false },
    { kFmBtoAParamId, 630, 496, 0.0, 1.0, false },
    { kFlutterParamId, 630, 522, 0.0, 1.0, false },
    { kMaterialParamId, 630, 548, 0.0, 1.0, false },
    { kGustShapeParamId, 630, 574, 0.0, 5.0, false },
    { kThresholdParamId, 630, 600, 0.0, 1.0, false },
    { kColorParamId, 630, 666, 0.0, 1.0, false },
    { kFilterParamId, 630, 692, 0.0, 1.0, false },
    { kResonanceParamId, 630, 718, 0.0, 1.0, false },
    { kFilterRunParamId, 630, 744, 0.0, 1.0, false },
    { kFilterSweepParamId, 630, 770, 0.0, 1.0, false },
    { kSaturationParamId, 630, 796, 0.0, 1.0, false },
    { kMotionRateParamId, 896, 78, 0.001, 2.0, true },
    { kMotionFlowParamId, 896, 104, 0.0, 1.0, false },
    { kMotionShearParamId, 896, 130, 0.0, 1.0, false },
    { kMotionCurlParamId, 896, 156, 0.0, 1.0, false },
    { kMotionUpdraftParamId, 896, 182, 0.0, 1.0, false },
    { kFieldParamId, 896, 208, 0.0, 1.0, false },
    { kSpatialFollowParamId, 896, 234, 0.0, 1.0, false },
    { kAzimuthParamId, 896, 300, -180.0, 180.0, false },
    { kElevationParamId, 896, 326, -90.0, 90.0, false },
    { kDistanceParamId, 896, 352, 0.15, 2.0, false },
    { kSpaceParamId, 896, 444, 0.0, 1.0, false },
    { kEnvironmentSizeParamId, 896, 470, 0.0, 1.0, false },
    { kEnvironmentDecayParamId, 896, 496, 0.0, 1.0, false },
    { kEnvironmentDampingParamId, 896, 522, 0.0, 1.0, false },
    { kFieldListenAmountParamId, 896, 574, 0.0, 1.0, false },
    { kParticlesParamId, 896, 666, 0.0, 1.0, false },
    { kVortexParamId, 896, 692, 0.0, 1.0, false },
    { kPressureParamId, 896, 718, 0.0, 1.0, false },
};

const GuiSliderSpec* guiSliderSpec(clap_id id)
{
    for (const auto& spec : kGuiSliders) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

double sliderNorm(const GuiSliderSpec& spec, double value)
{
    if (spec.id == kAzimuthParamId) return
        s3g::aedAzimuthSliderNorm(static_cast<float>(value));
    if (spec.logarithmic) {
        if (spec.min <= 0.0) {
            if (value <= 0.0) return 0.0;
            constexpr double zeroZone = 0.02;
            const double minPositive = std::max(0.000001, spec.max * 0.001);
            const double logNorm = std::log(std::max(minPositive, value) / minPositive)
                / std::log(spec.max / minPositive);
            return std::clamp(zeroZone + (1.0 - zeroZone) * logNorm, zeroZone, 1.0);
        }
        const double minValue = std::max(0.000001, spec.min);
        return std::clamp(std::log(std::max(minValue, value) / minValue) / std::log(spec.max / minValue), 0.0, 1.0);
    }
    return std::clamp((value - spec.min) / std::max(0.000001, spec.max - spec.min), 0.0, 1.0);
}

double sliderValue(const GuiSliderSpec& spec, NSPoint point)
{
    const double norm = std::clamp((static_cast<double>(point.x) - (spec.panelX + 108.0)) / 82.0, 0.0, 1.0);
    if (spec.id == kAzimuthParamId) return
        s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm));
    if (spec.logarithmic) {
        if (spec.min <= 0.0) {
            constexpr double zeroZone = 0.02;
            if (norm <= zeroZone) return 0.0;
            const double minPositive = std::max(0.000001, spec.max * 0.001);
            const double logNorm = (norm - zeroZone) / (1.0 - zeroZone);
            return minPositive * std::pow(spec.max / minPositive, logNorm);
        }
        return spec.min * std::pow(spec.max / spec.min, norm);
    }
    return spec.min + norm * (spec.max - spec.min);
}

double rateNormToHzForDisplay(double value)
{
    return 0.012 * std::pow(1.25 / 0.012, std::clamp(value, 0.0, 1.0));
}

@interface S3GAmbiWindEncoderView : NSView <NSWindowDelegate> {
    Plugin* _plugin;
    s3g::AmbiWindParams _paramsSnapshot;
    s3g::AmbiWindParams _displayParamsSnapshot;
    WindSurface _surfaceSnapshot;
    uint32_t _presetIndexSnapshot;
    char _customPresetNameSnapshot[64];
    NSTimer* _timer;
    uint32_t _selectedVoice;
    int _dragParam;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    int _fieldPage;
    int _dragBreakpointRow;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
    BOOL _surfaceEdit;
    int _selectedSurfaceCell;
    int _dragSurfaceCell;
    BOOL _dragSurfaceCursor;
    BOOL _surfacePopupChild;
    S3GAmbiWindEncoderView* _surfacePopupOwner;
    NSPanel* _surfacePanel;
    S3GAmbiWindEncoderView* _surfacePopupView;
    NSClipView* _surfacePopupClip;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)openSurfacePopup;
- (void)hideSurfacePopup;
- (void)destroySurfacePopup;
- (void)refreshControlSnapshot;
@end

@implementation S3GAmbiWindEncoderView
- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        if (plugin) {
            const auto snapshot = controlSnapshot(*plugin);
            _paramsSnapshot = snapshot.params;
            _displayParamsSnapshot = snapshot.effectiveParams;
            _surfaceSnapshot = snapshot.surface;
            _presetIndexSnapshot = snapshot.presetIndex;
            std::snprintf(_customPresetNameSnapshot,
                sizeof(_customPresetNameSnapshot), "%s",
                snapshot.customPresetName);
        } else {
            _paramsSnapshot = {};
            _displayParamsSnapshot = {};
            _surfaceSnapshot = {};
            _presetIndexSnapshot = 0u;
            _customPresetNameSnapshot[0] = '\0';
        }
        _timer = nil;
        _selectedVoice = 0;
        _dragParam = 0;
        _dragView = NO;
        _lastDragPoint = NSMakePoint(0, 0);
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _fieldPage = 0;
        _dragBreakpointRow = -1;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0;
        _openMenuRect = NSZeroRect;
        _surfaceEdit = NO;
        _selectedSurfaceCell = -1;
        _dragSurfaceCell = -1;
        _dragSurfaceCursor = NO;
        _surfacePopupChild = NO;
        _surfacePopupOwner = nil;
        _surfacePanel = nil;
        _surfacePopupView = nil;
        _surfacePopupClip = nil;
        [self setWantsLayer:YES];
    }
    return self;
}

- (BOOL)isFlipped { return YES; }

- (void)dealloc
{
    if (!_surfacePopupChild) [self destroySurfacePopup];
    [self stopRefreshTimer];
    [super dealloc];
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
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
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
    [self refreshControlSnapshot];
    [self setNeedsDisplay:YES];
}

- (void)refreshControlSnapshot
{
    if (!_plugin) return;
    const auto snapshot = controlSnapshot(*_plugin);
    _paramsSnapshot = snapshot.params;
    _displayParamsSnapshot = snapshot.effectiveParams;
    if (_plugin->appliedSurfaceCommandSequence.load(std::memory_order_acquire)
        >= _plugin->queuedSurfaceCommandSequence.load(std::memory_order_acquire)) {
        _surfaceSnapshot = snapshot.surface;
    }
    _presetIndexSnapshot = snapshot.presetIndex;
    std::snprintf(_customPresetNameSnapshot,
        sizeof(_customPresetNameSnapshot), "%s",
        snapshot.customPresetName);
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 608); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 558); }
- (NSRect)presetMenuRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Preset); }
- (NSRect)loadPresetButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Load); }
- (NSRect)savePresetButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Save); }
- (NSRect)randomizeButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, s3g::gui_layout::EncoderTitleAction::Random); }

- (NSRect)pageButtonRect:(int)index
{
    return s3g::clap_gui::environmentalFieldPageButtonRect(
        [self fieldPanelRect], static_cast<uint32_t>(index));
}

- (NSRect)surfacePlotRect
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 10.0, field.origin.y + 76.0,
        field.size.width - 20.0, field.size.height - 88.0);
}

- (NSRect)surfaceButtonRect:(int)index
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 10.0 + index * 52.0,
        field.origin.y + 10.0, 46.0, 16.0);
}

- (void)syncSurfaceEditMode:(BOOL)editing
{
    _surfaceEdit = editing;
    if (_surfacePopupChild && _surfacePopupOwner) {
        _surfacePopupOwner->_surfaceEdit = editing;
        [_surfacePopupOwner setNeedsDisplay:YES];
    } else if (_surfacePopupView) {
        _surfacePopupView->_surfaceEdit = editing;
        [_surfacePopupView setNeedsDisplay:YES];
    }
}

- (void)openSurfacePopup
{
    if (_surfacePopupChild) return;
    const NSRect source = [self fieldPanelRect];
    if (!_surfacePanel) {
        _surfacePanel = [[NSPanel alloc] initWithContentRect:
            NSMakeRect(0.0, 0.0, source.size.width, source.size.height)
            styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                | NSWindowStyleMaskUtilityWindow)
            backing:NSBackingStoreBuffered defer:NO];
        [_surfacePanel setTitle:@"s3g AMBI ENCODER WIND — SURF"];
        [_surfacePanel setReleasedWhenClosed:NO];
        [_surfacePanel setHidesOnDeactivate:YES];
        [_surfacePanel setDelegate:self];
        _surfacePopupView = [[S3GAmbiWindEncoderView alloc]
            initWithPlugin:_plugin];
        _surfacePopupView->_surfacePopupChild = YES;
        _surfacePopupView->_surfacePopupOwner = self;
        _surfacePopupView->_fieldPage = 1;
        _surfacePopupView->_surfaceEdit = _surfaceEdit;
        _surfacePopupClip = [[NSClipView alloc] initWithFrame:
            NSMakeRect(0.0, 0.0, source.size.width, source.size.height)];
        [_surfacePopupClip setDrawsBackground:NO];
        [_surfacePopupClip setDocumentView:_surfacePopupView];
        [_surfacePanel setContentView:_surfacePopupClip];
        [_surfacePopupClip setBoundsOrigin:source.origin];
        [_surfacePopupView release];
        [_surfacePopupClip release];

        NSWindow* parent = [self window];
        const NSRect parentFrame = parent ? [parent frame]
            : [[NSScreen mainScreen] visibleFrame];
        const NSRect panelFrame = [_surfacePanel frame];
        CGFloat x = NSMaxX(parentFrame) + 8.0;
        CGFloat y = NSMaxY(parentFrame) - panelFrame.size.height;
        NSScreen* screen = parent ? [parent screen] : [NSScreen mainScreen];
        const NSRect visible = screen ? [screen visibleFrame] : parentFrame;
        if (x + panelFrame.size.width > NSMaxX(visible)) {
            x = NSMinX(parentFrame) - panelFrame.size.width - 8.0;
        }
        [_surfacePanel setFrameOrigin:NSMakePoint(
            std::max(NSMinX(visible), x),
            std::clamp(y, NSMinY(visible), std::max(NSMinY(visible),
                NSMaxY(visible) - panelFrame.size.height)))];
    }
    _surfacePopupView->_fieldPage = 1;
    [self syncSurfaceEditMode:_surfaceEdit];
    _fieldPage = 0;
    NSWindow* parent = [self window];
    NSWindow* previousParent = [_surfacePanel parentWindow];
    if (previousParent && previousParent != parent) {
        [previousParent removeChildWindow:_surfacePanel];
    }
    if (parent && [_surfacePanel parentWindow] != parent) {
        [parent addChildWindow:_surfacePanel ordered:NSWindowAbove];
    }
    [_surfacePopupView startRefreshTimer];
    [_surfacePanel makeKeyAndOrderFront:nil];
    [self setNeedsDisplay:YES];
}

- (void)hideSurfacePopup
{
    if (!_surfacePanel) return;
    [_surfacePopupView stopRefreshTimer];
    NSWindow* parent = [_surfacePanel parentWindow];
    if (parent) [parent removeChildWindow:_surfacePanel];
    [_surfacePanel orderOut:nil];
}

- (void)destroySurfacePopup
{
    if (!_surfacePanel) return;
    [_surfacePopupView stopRefreshTimer];
    [_surfacePanel setDelegate:nil];
    NSWindow* parent = [_surfacePanel parentWindow];
    if (parent) [parent removeChildWindow:_surfacePanel];
    [_surfacePanel orderOut:nil];
    [_surfacePanel release];
    _surfacePanel = nil;
    _surfacePopupView = nil;
    _surfacePopupClip = nil;
}

- (void)windowWillClose:(NSNotification*)notification
{
    if ([notification object] != _surfacePanel) return;
    [_surfacePopupView stopRefreshTimer];
    NSWindow* parent = [_surfacePanel parentWindow];
    if (parent) [parent removeChildWindow:_surfacePanel];
}

- (NSRect)surfaceCurveRect
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 10.0, field.origin.y + 38.0, 104.0, 18.0);
}

- (NSRect)surfaceFocusRect:(int)index
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 170.0 + index * 24.0,
        field.origin.y + 38.0, 20.0, 18.0);
}

- (NSRect)surfaceGlideRect:(int)index
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 336.0 + index * 24.0,
        field.origin.y + 38.0, 20.0, 18.0);
}

- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    const CGFloat w = 38.0;
    const CGFloat gap = 5.0;
    return NSMakeRect(NSMaxX(panel) - 10.0 - (3.0 - index) * w - (2.0 - index) * gap, panel.origin.y + 4.0, w, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    const CGFloat w = 18.0;
    const CGFloat gap = 4.0;
    const CGFloat x = [self viewButtonRect:0].origin.x - 12.0 - (2.0 - index) * w - (1.0 - index) * gap;
    return NSMakeRect(x, [self fieldPanelRect].origin.y + 4.0, w, 13.0);
}

- (s3g::Vec3)voiceWorld:(uint32_t)voice
{
    if (!_plugin || voice >= s3g::kAmbiWindMaxVoices) return { 0.0f, 0.0f, 0.0f };
    const float az = _plugin->guiAzimuth[voice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[voice].load(std::memory_order_relaxed);
    const s3g::Vec3 dir = s3g::directionFromAed(az, el);
    return { dir.x * dist, dir.y * dist, dir.z * dist };
}

- (NSPoint)projectWorld:(s3g::Vec3)point depth:(CGFloat*)depth
{
    const NSRect field = [self fieldRect];
    const CGFloat scale = std::min(field.size.width, field.size.height) * 0.36 * std::clamp(_viewZoom, 0.55, 2.20);
    const CGFloat centerX = NSMidX(field);
    const CGFloat centerY = NSMidY(field);
    const float azimuth = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
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

- (NSPoint)projectVoice:(uint32_t)voice depth:(CGFloat*)depth
{
    return [self projectWorld:[self voiceWorld:voice] depth:depth];
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzDeg = 0.0;
        _viewElDeg = 0.0;
    } else if (mode == 1) {
        _viewAzDeg = 0.0;
        _viewElDeg = -90.0;
    } else {
        _viewAzDeg = 38.0;
        _viewElDeg = 32.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (NSString*)customPresetDirectory
{
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Wind Encoder"];
}

- (void)saveCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gwind" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3gwind", [self presetDisplayName]]];
    if ([panel runModal] != NSModalResponseOK) return;
    NSString* name = [[[[panel URL] lastPathComponent] stringByDeletingPathExtension] copy];
    if (saveCustomPresetFile([[[panel URL] path] UTF8String],
            _paramsSnapshot, [name UTF8String])) {
        std::snprintf(_customPresetNameSnapshot,
            sizeof(_customPresetNameSnapshot), "%s", [name UTF8String]);
        (void)queueMetadataCommand(*_plugin, _presetIndexSnapshot,
            _customPresetNameSnapshot);
    }
    [name release];
    [self setNeedsDisplay:YES];
}

- (void)loadCustomPreset
{
    if (!_plugin) return;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:nil error:nil];
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gwind" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return;
    file.params.outputGainDb = _paramsSnapshot.outputGainDb;
    const char* name = file.name[0] ? file.name : "Custom";
    (void)queueStateCommand(*_plugin, file.params, _surfaceSnapshot,
        _presetIndexSnapshot, name, kReplaceCustomStateActionId);
    _paramsSnapshot = file.params;
    _displayParamsSnapshot = file.params;
    std::snprintf(_customPresetNameSnapshot,
        sizeof(_customPresetNameSnapshot), "%s", name);
    [self setNeedsDisplay:YES];
}

- (NSColor*)voiceColor:(uint32_t)voice selected:(BOOL)selected
{
    const float az = _plugin->guiAzimuth[voice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[voice].load(std::memory_order_relaxed);
    const float hue = std::fmod((az + 180.0f) / 360.0f + 0.08f, 1.0f);
    const float sat = selected ? 0.72f : 0.52f;
    const float bri = selected ? 0.96f : 0.72f + std::max(0.0f, el) / 90.0f * 0.18f;
    return [NSColor colorWithCalibratedHue:hue saturation:sat brightness:bri alpha:selected ? 1.0 : 0.84];
}

- (void)drawSurfacePage:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    const NSRect header = NSMakeRect(field.origin.x, field.origin.y,
        field.size.width, 28.0);
    static NSString* labels[] = { @"PLAY", @"ON", @"ADD", @"DEL", @"CAP", @"POP" };
    labels[0] = _surfaceEdit ? @"EDIT" : @"PLAY";
    labels[1] = _surfaceSnapshot.enabled ? @"ON" : @"OFF";
    for (int index = 0; index < 6; ++index) {
        const BOOL active = (index == 0 && _surfaceEdit)
            || (index == 1 && _surfaceSnapshot.enabled)
            || (index == 5 && (_surfacePopupChild || [_surfacePanel isVisible]));
        s3g::clap_gui::drawHeaderButton([self surfaceButtonRect:index],
            header, labels[index], active, attrs, style);
    }
    const NSRect curve = [self surfaceCurveRect];
    [style.strip setFill]; NSRectFill(curve);
    [style.grid setStroke]; NSFrameRect(curve);
    [[NSString stringWithFormat:@"CURVE  %s",
        s3g::parameterSurfaceCurveName(_surfaceSnapshot.curve)]
        drawAtPoint:NSMakePoint(curve.origin.x + 5.0, curve.origin.y + 2.0)
        withAttributes:valueAttrs];
    [@"FOCUS" drawAtPoint:NSMakePoint(field.origin.x + 122.0,
        field.origin.y + 40.0) withAttributes:attrs];
    s3g::clap_gui::drawHeaderButton([self surfaceFocusRect:0], header,
        @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self surfaceFocusRect:1], header,
        @"+", false, attrs, style);
    [[NSString stringWithFormat:@"%.2f", _surfaceSnapshot.focus]
        drawAtPoint:NSMakePoint(field.origin.x + 222.0, field.origin.y + 40.0)
        withAttributes:valueAttrs];
    [@"GLIDE" drawAtPoint:NSMakePoint(field.origin.x + 280.0,
        field.origin.y + 40.0) withAttributes:attrs];
    s3g::clap_gui::drawHeaderButton([self surfaceGlideRect:0], header,
        @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self surfaceGlideRect:1], header,
        @"+", false, attrs, style);
    NSString* glide = _surfaceSnapshot.glideMs < 0.5f ? @"OFF"
        : [NSString stringWithFormat:@"%.0f MS", _surfaceSnapshot.glideMs];
    [glide drawAtPoint:NSMakePoint(field.origin.x + 388.0,
        field.origin.y + 40.0) withAttributes:valueAttrs];
    const NSRect plot = [self surfacePlotRect];
    const bool audioActive = _plugin->active.load(
        std::memory_order_acquire);
    const float effectiveX = audioActive
        ? _plugin->effectiveSurfaceX.load(std::memory_order_relaxed)
        : _paramsSnapshot.surfaceX;
    const float effectiveY = audioActive
        ? _plugin->effectiveSurfaceY.load(std::memory_order_relaxed)
        : _paramsSnapshot.surfaceY;
    s3g::clap_gui::drawParameterSurfaceVoronoi(_surfaceSnapshot, plot,
        effectiveX, effectiveY,
        _selectedSurfaceCell, valueAttrs,
        _paramsSnapshot.surfaceX, _paramsSnapshot.surfaceY);
    [[NSString stringWithFormat:@"T %.3f %.3f   /   A %.3f %.3f   /   %u CELLS   /   %@",
        _paramsSnapshot.surfaceX, _paramsSnapshot.surfaceY,
        effectiveX, effectiveY,
        _surfaceSnapshot.cellCount,
        _surfaceSnapshot.cellCount < 2u ? @"ADD TWO CELLS TO ENABLE" :
            (_surfaceSnapshot.enabled ? @"INTERPOLATING" : @"BYPASSED")]
        drawAtPoint:NSMakePoint(plot.origin.x + 8.0, NSMaxY(plot) - 18.0)
        withAttributes:valueAttrs];
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"WIND FIELD", true, panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:0], header, @"FIELD", _fieldPage == 0, attrs, style);
    s3g::clap_gui::drawHeaderButton([self pageButtonRect:1], header, @"SURF", _fieldPage == 1, attrs, style);
    if (_fieldPage == 1) {
        [self drawSurfacePage:attrs valueAttrs:valueAttrs style:style];
        return;
    }
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
    static NSString* labels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int i = 0; i < 3; ++i) s3g::clap_gui::drawHeaderButton([self viewButtonRect:i], header, labels[i], i == _viewMode, attrs, style);
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1, 1)] addClip];
    const CGFloat radius = std::min(field.size.width, field.size.height) * 0.36 * _viewZoom;
    [s3g::clap_gui::color(0x303030) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMidX(field) - radius, NSMidY(field) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];
    [s3g::clap_gui::color(0x242424) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMinX(field) + 18, NSMidY(field)) toPoint:NSMakePoint(NSMaxX(field) - 18, NSMidY(field))];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(NSMidX(field), NSMinY(field) + 18) toPoint:NSMakePoint(NSMidX(field), NSMaxY(field) - 18)];

    const uint32_t voices = std::clamp<uint32_t>(
        _surfaceEdit ? _paramsSnapshot.voices
            : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiWindMaxVoices);
    _selectedVoice = std::min<uint32_t>(_selectedVoice, voices - 1u);
    std::array<NSPoint, s3g::kAmbiWindMaxVoices> projected {};
    for (uint32_t voice = 0; voice < voices; ++voice) projected[voice] = [self projectVoice:voice depth:nullptr];
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const uint32_t next = (voice + 1u) % voices;
        const float edgeMembership = _surfaceEdit ? 1.0f
            : std::min(
                _plugin->guiRenderGain[voice].load(
                    std::memory_order_relaxed),
                _plugin->guiRenderGain[next].load(
                    std::memory_order_relaxed));
        [s3g::clap_gui::color(0x5c5c5c,
            0.18 * std::clamp(edgeMembership, 0.0f, 1.0f)) setStroke];
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:projected[voice]];
        [path lineToPoint:projected[next]];
        [path setLineWidth:0.55];
        [path stroke];
    }
    NSDictionary* idAttrs = s3g::clap_gui::textAttrs(s3g::clap_gui::color(0x080808), voices > 32u ? 5.5 : 7.0);
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const BOOL selected = voice == _selectedVoice;
        const float energy = _plugin->guiEnergy[voice].load(std::memory_order_relaxed);
        const float gust = std::clamp(_plugin->guiGust[voice].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float membership = _surfaceEdit ? 1.0f
            : std::clamp(_plugin->guiRenderGain[voice].load(
                std::memory_order_relaxed), 0.0f, 1.0f);
        const float activity = std::clamp(std::sqrt(std::max(0.0f, energy)) * 18.0f, 0.0f, 1.0f);
        const CGFloat base = voices > 32u ? 7.0 : 9.0;
        const CGFloat size = ((selected ? base + 5.0 : base)
            + activity * 5.0f + gust * 7.0f)
            * (0.45f + membership * 0.55f);
        const NSRect marker = NSMakeRect(projected[voice].x - size * 0.5, projected[voice].y - size * 0.5, size, size);
        if (gust > 0.04f || activity > 0.04f) {
            const CGFloat halo = size * (1.15 + gust * 1.9 + activity * 0.6);
            NSRect haloRect = NSMakeRect(projected[voice].x - halo * 0.5, projected[voice].y - halo * 0.5, halo, halo);
            [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:(0.05 + gust * 0.18 + activity * 0.08) * membership] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:haloRect] fill];
        }
        [[[self voiceColor:voice selected:selected] colorWithAlphaComponent:(selected ? 0.98 : 0.22 + gust * 0.54 + activity * 0.20) * membership] setFill];
        NSRectFill(marker);
        [s3g::clap_gui::color(selected ? 0xe6e6e6 : 0x4f4f4f, (selected ? 1.0 : 0.22 + gust * 0.46 + activity * 0.18) * membership) setStroke];
        NSFrameRect(marker);
        NSString* label = [NSString stringWithFormat:@"%u", voice + 1u];
        const NSSize labelSize = [label sizeWithAttributes:idAttrs];
        if (membership > 0.45f && (gust > 0.28f || selected)) {
            [label drawAtPoint:NSMakePoint(NSMidX(marker) - labelSize.width * 0.5, NSMidY(marker) - labelSize.height * 0.5 - 0.5) withAttributes:idAttrs];
        }
    }
    [NSGraphicsContext restoreGraphicsState];

    const float az = _plugin->guiAzimuth[_selectedVoice].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[_selectedVoice].load(std::memory_order_relaxed);
    const float dist = _plugin->guiDistance[_selectedVoice].load(std::memory_order_relaxed);
    const float energy = _plugin->guiEnergy[_selectedVoice].load(std::memory_order_relaxed);
    const float gust = _plugin->guiGust[_selectedVoice].load(std::memory_order_relaxed);
    NSString* readout = [NSString stringWithFormat:@"V%02u  AZ%+.1f  EL%+.1f  D%.2f  G%.2f  E%.3f", _selectedVoice + 1u, az, el, dist, gust, energy];
    s3g::clap_gui::drawRightStatus(readout, NSMaxX(field), field.origin.y + 7, valueAttrs, 8.0);
    [@"MICROWEATHER FIELD     GUST SIZE + XYZ FLOW MOTION     ACN/SN3D" drawAtPoint:NSMakePoint(field.origin.x + 9, NSMaxY(field) - 19) withAttributes:valueAttrs];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    char display[64] {};
    if (param == kRateBParamId) {
        const double hz = rateNormToHzForDisplay(value);
        if (hz < 1.0) std::snprintf(display, sizeof(display), "%.4f Hz", hz);
        else std::snprintf(display, sizeof(display), "%.2f Hz", hz);
    } else {
        paramsValueToText(nullptr, param, value, display, sizeof(display));
    }
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:display], sliderNorm(*spec, value), spec->y,
        attrs, valueAttrs, style, spec->panelX + 16, spec->panelX + 108, spec->panelX + 196, 82);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value panelX:(CGFloat)panelX y:(CGFloat)y attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, y, attrs, valueAttrs, style, panelX + 16, panelX + 108, 124);
}

- (NSString*)presetDisplayName
{
    if (_customPresetNameSnapshot[0]) return [NSString stringWithFormat:
        @"CUSTOM: %s", _customPresetNameSnapshot];
    return [NSString stringWithUTF8String:s3g::ambiWindFactoryPresetInfo(
        _presetIndexSnapshot).name];
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _surfaceEdit
        ? _paramsSnapshot : _displayParamsSnapshot;
    s3g::clap_gui::drawPanelFrame(630, 42, 250, 132, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT / AIR", true, 630, 42, 250, 21, attrs, style);
    [self drawSlider:@"OUT" param:kOutputParamId value:p.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] panelX:630 y:104 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AIR" param:kPwmAParamId value:p.air attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"HISS" param:kPwmBParamId value:p.hiss attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 186, 250, 210, style);
    s3g::clap_gui::drawPanelHeader(@"SOURCE AND GUST", true, 630, 186, 250, 21, attrs, style);
    [self drawSlider:@"VECTOR LFO" param:kRateModeAParamId value:p.vectorRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"MATERIAL TYPE" value:[NSString stringWithUTF8String:
        kMaterialNames[std::min<uint32_t>(p.materialMode,
            s3g::kAmbiWindMaterialCount - 1u)]] panelX:630 y:248
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VOICES" param:kVoicesParamId value:p.voices attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"WIND" param:kRateAParamId value:p.wind attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GUST RATE" param:kRateBParamId value:p.gustRate attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SPREAD" param:kSpreadParamId value:p.spread attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEVIATION" param:kDeviationParamId value:p.deviation attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 408, 250, 210, style);
    s3g::clap_gui::drawPanelHeader(@"GUST AND MATERIAL", true, 630, 408, 250, 21, attrs, style);
    static constexpr const char* kEdgeNames[] = { "SOFT", "BEND", "HARD" };
    [self drawMenu:@"GUST EDGE" value:[NSString stringWithUTF8String:kEdgeNames[std::min<uint32_t>(p.gustEdge, 2u)]] panelX:630 y:444 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GUST DEPTH" param:kFmAtoBParamId value:p.gustDepth attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TURBULENCE" param:kFmBtoAParamId value:p.turbulence attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FLUTTER" param:kFlutterParamId value:p.flutter attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"MATERIAL" param:kMaterialParamId value:p.material attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GUST SHAPE" param:kGustShapeParamId value:p.gustShape attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CENTER" param:kThresholdParamId value:p.center attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(630, 630, 250, 184, style);
    s3g::clap_gui::drawPanelHeader(@"FILTER AND TONE", true, 630, 630, 250, 21, attrs, style);
    [self drawSlider:@"SWEEP" param:kColorParamId value:p.sweep attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"Q" param:kFilterParamId value:p.q attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SHRILL" param:kResonanceParamId value:p.shrill attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BODY" param:kFilterRunParamId value:p.body attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"BREATH" param:kFilterSweepParamId value:p.breath attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"GRIT" param:kSaturationParamId value:p.grit attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 210, style);
    s3g::clap_gui::drawPanelHeader(@"MICROWEATHER MOTION", true, 896, 42, 246, 21, attrs, style);
    [self drawSlider:@"FIELD RATE" param:kMotionRateParamId value:p.motionRateHz attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"FLOW PUSH" param:kMotionFlowParamId value:p.motionFlow attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SHEAR" param:kMotionShearParamId value:p.motionShear attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CURL" param:kMotionCurlParamId value:p.motionCurl attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"UPDRAFT" param:kMotionUpdraftParamId value:p.motionUpdraft attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DEPTH PUSH" param:kFieldParamId value:p.field attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"INERTIA" param:kSpatialFollowParamId value:p.spatialFollow attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 264, 246, 106, style);
    s3g::clap_gui::drawPanelHeader(@"MACRO WIND VECTOR", true, 896, 264, 246, 21, attrs, style);
    [self drawSlider:@"DIRECTION" param:kAzimuthParamId value:p.centerAzimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEVATION" param:kElevationParamId value:p.centerElevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"RANGE" param:kDistanceParamId value:p.centerDistance attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 382, 246, 236, style);
    s3g::clap_gui::drawPanelHeader(@"ENVIRONMENT FIELD", true, 896, 382, 246, 21, attrs, style);
    [self drawMenu:@"PLACE" value:[NSString stringWithUTF8String:kPlaceNames[
        std::min<uint32_t>(p.place, s3g::kAmbiWindPlaceCount - 1u)]] panelX:896 y:418 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV RETURN" param:kSpaceParamId value:p.space attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV SIZE" param:kEnvironmentSizeParamId value:p.environmentSize attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DECAY" param:kEnvironmentDecayParamId value:p.environmentDecay attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENV DAMPING" param:kEnvironmentDampingParamId value:p.environmentDamping attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"LISTEN" value:[NSString stringWithUTF8String:kFieldListenNames[
        std::min<uint32_t>(static_cast<uint32_t>(p.fieldListenMode), 3u)]]
        panelX:896 y:548 attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"LISTEN AMOUNT" param:kFieldListenAmountParamId
        value:p.fieldListenAmount attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"RESPONSE" value:[NSString stringWithUTF8String:
        kFieldListenResponseNames[std::min<uint32_t>(
            static_cast<uint32_t>(p.fieldListenResponse), 3u)]]
        panelX:896 y:600 attrs:attrs valueAttrs:valueAttrs style:style];

    s3g::clap_gui::drawPanelFrame(896, 630, 246, 106, style);
    s3g::clap_gui::drawPanelHeader(@"WEATHER OBJECTS", true,
        896, 630, 246, 21, attrs, style);
    [self drawSlider:@"PARTICLES" param:kParticlesParamId value:p.particles
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"VORTEX" param:kVortexParamId value:p.vortex
        attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"PRESSURE" param:kPressureParamId value:p.pressure
        attrs:attrs valueAttrs:valueAttrs style:style];
}

- (NSRect)menuBoxRect:(int)menu
{
    switch (menu) {
    case 1: return [self presetMenuRect];
    case 2: return NSZeroRect;
    case 3: return NSMakeRect(738, 247, 124, 15);
    case 4: return NSMakeRect(1004, 417, 124, 15);
    case 5: return NSMakeRect(1004, 547, 124, 15);
    case 6: return NSMakeRect(1004, 599, 124, 15);
    case 7: return NSZeroRect;
    case 8: return NSZeroRect;
    case 9: return NSMakeRect(738, 443, 124, 15);
    case 10: return NSMakeRect(738, 103, 124, 15);
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return s3g::kAmbiWindFactoryPresetCount;
    case 2: return 0u;
    case 3: return s3g::kAmbiWindMaterialCount;
    case 4: return s3g::kAmbiWindPlaceCount;
    case 5: return 4u;
    case 6: return 4u;
    case 7: return 0u;
    case 8: return 0u;
    case 9: return 3u;
    case 10: return 7u;
    default: return 0u;
    }
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _menuItemCount = [self menuCount:menu];
    _hoverMenuItem = -1;
    const NSRect box = [self menuBoxRect:menu];
    const CGFloat itemH = 21.0;
    CGFloat y = NSMaxY(box) + 2.0;
    const CGFloat height = itemH * _menuItemCount;
    if (y + height > kGuiHeight - 8.0) y = box.origin.y - height - 2.0;
    _openMenuRect = NSMakeRect(box.origin.x, y, box.size.width, height);
    [self setNeedsDisplay:YES];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu <= 0 || _menuItemCount == 0u) return;
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* materialItems[] = { @"OPEN", @"LEAF", @"HOLLOW",
        @"WIRE", @"METAL", @"CHIMES", @"BLOCKS", @"HARP", @"REEDS",
        @"FABRIC", @"SAND", @"DEBRIS", @"STRUCTURE" };
    static NSString* placeItems[] = { @"OPEN", @"CANOPY", @"PORCH", @"ROOM", @"HANGAR", @"CANYON", @"TUNNEL" };
    static NSString* listenItems[] = { @"OFF", @"FOLLOW", @"COUNTER", @"BALANCE" };
    static NSString* responseItems[] = {
        @"LEGACY", @"CHARGE", @"SETTLE", @"RESONATE"
    };
    static NSString* edgeItems[] = { @"SOFT", @"BEND", @"HARD" };
    static NSString* presetItems[s3g::kAmbiWindFactoryPresetCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t i = 0; i < s3g::kAmbiWindFactoryPresetCount; ++i) presetItems[i] = [[NSString stringWithUTF8String:s3g::ambiWindFactoryPresetInfo(i).name] retain];
    });
    NSString** items = presetItems;
    int selected = static_cast<int>(_presetIndexSnapshot);
    if (_openMenu == 3) {
        items = materialItems;
        selected = static_cast<int>(_paramsSnapshot.materialMode);
    } else if (_openMenu == 4) {
        items = placeItems;
        selected = static_cast<int>(_paramsSnapshot.place);
    } else if (_openMenu == 5) {
        items = listenItems;
        selected = static_cast<int>(_paramsSnapshot.fieldListenMode);
    } else if (_openMenu == 6) {
        items = responseItems;
        selected = static_cast<int>(_paramsSnapshot.fieldListenResponse);
    } else if (_openMenu == 9) {
        items = edgeItems;
        selected = static_cast<int>(_paramsSnapshot.gustEdge);
    } else if (_openMenu == 10) {
        items = orderItems;
        selected = static_cast<int>(_paramsSnapshot.order) - 1;
    }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    [self refreshControlSnapshot];
    const s3g::clap_gui::Style style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    NSDictionary* titleAttrs = s3g::clap_gui::softTitleAttrs();
    [@"s3g AMBI ENCODER WIND" drawAtPoint:NSMakePoint(18, 14) withAttributes:titleAttrs];
    s3g::clap_gui::drawEncoderPresetMenu(
        [self presetDisplayName],
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight),
        attrs, valueAttrs, style);
    s3g::clap_gui::drawHeaderActionButton([self loadPresetButtonRect], [self loadPresetButtonRect], @"LOAD", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self savePresetButtonRect], [self savePresetButtonRect], @"SAVE", attrs, style);
    s3g::clap_gui::drawHeaderActionButton([self randomizeButtonRect], [self randomizeButtonRect], @"RANDOM", attrs, style);
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(_plugin->outputPeak.load(std::memory_order_relaxed)), kGuiWidth, 14, valueAttrs, 18);
    [self drawField:attrs valueAttrs:valueAttrs style:style];
    [self drawPanels:attrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (int)hitVoice:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    const uint32_t voices = std::clamp<uint32_t>(
        _surfaceEdit ? _paramsSnapshot.voices
            : _plugin->guiVoiceCount.load(std::memory_order_relaxed),
        1u, s3g::kAmbiWindMaxVoices);
    int best = -1;
    CGFloat bestDistance = 15.0;
    for (uint32_t voice = 0; voice < voices; ++voice) {
        const NSPoint projected = [self projectVoice:voice depth:nullptr];
        const CGFloat distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(voice);
        }
    }
    return best;
}

- (int)hitSurfaceCell:(NSPoint)point
{
    const NSRect plot = [self surfacePlotRect];
    int best = -1;
    CGFloat bestDistance = 15.0;
    for (uint32_t index = 0u; index < _surfaceSnapshot.cellCount; ++index) {
        const auto& cell = _surfaceSnapshot.cells[index];
        const NSPoint site = NSMakePoint(plot.origin.x + cell.x * plot.size.width,
            NSMaxY(plot) - cell.y * plot.size.height);
        const CGFloat distance = std::hypot(point.x - site.x, point.y - site.y);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(index);
        }
    }
    return best;
}

- (void)updateSurfacePosition:(NSPoint)point cursor:(BOOL)cursor
{
    const NSRect plot = [self surfacePlotRect];
    const float x = std::clamp(static_cast<float>(
        (point.x - plot.origin.x) / plot.size.width), 0.0f, 1.0f);
    const float y = std::clamp(static_cast<float>(
        (NSMaxY(plot) - point.y) / plot.size.height), 0.0f, 1.0f);
    if (cursor) {
        _paramsSnapshot.surfaceX = x;
        _paramsSnapshot.surfaceY = y;
        queueGuiParamValue(*_plugin, kSurfaceXParamId, x);
        queueGuiParamValue(*_plugin, kSurfaceYParamId, y);
    } else if (_dragSurfaceCell >= 0
        && static_cast<uint32_t>(_dragSurfaceCell)
            < _surfaceSnapshot.cellCount) {
        auto& cell = _surfaceSnapshot.cells[
            static_cast<uint32_t>(_dragSurfaceCell)];
        cell.x = x;
        cell.y = y;
        queueGuiSurface(*_plugin, _surfaceSnapshot);
    }
    requestSurfaceProcess(*_plugin);
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)point
{
    const auto* spec = guiSliderSpec(param);
    if (!spec) return;
    const double value = sliderValue(*spec, point);
    assignParam(_paramsSnapshot, param, value);
    queueGuiParamValue(*_plugin, param, value);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    [self refreshControlSnapshot];
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            clap_id param = CLAP_INVALID_ID;
            double value = hit;
            if (_openMenu == 1) param = kPresetParamId;
            else if (_openMenu == 3) param = kRateModeBParamId;
            else if (_openMenu == 4) param = kPlaceParamId;
            else if (_openMenu == 5) param = kFieldListenModeParamId;
            else if (_openMenu == 6) param = kFieldListenResponseParamId;
            else if (_openMenu == 9) param = kGustEdgeParamId;
            else if (_openMenu == 10) {
                param = kOrderParamId;
                value = hit + 1;
            }
            if (param != CLAP_INVALID_ID) {
                if (param == kPresetParamId) {
                    const float outputGainDb = _paramsSnapshot.outputGainDb;
                    _presetIndexSnapshot = static_cast<uint32_t>(hit);
                    _paramsSnapshot = s3g::ambiWindFactoryPreset(
                        _presetIndexSnapshot);
                    _paramsSnapshot.outputGainDb = outputGainDb;
                    _customPresetNameSnapshot[0] = '\0';
                } else {
                    assignParam(_paramsSnapshot, param, value);
                }
                queueGuiParamValue(*_plugin, param, value);
            }
        }
        _openMenu = 0;
        _hoverMenuItem = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self presetMenuRect])) { [self openMenu:1]; return; }
    if (NSPointInRect(point, [self savePresetButtonRect])) { [self saveCustomPreset]; return; }
    if (NSPointInRect(point, [self loadPresetButtonRect])) { [self loadCustomPreset]; return; }
    if (NSPointInRect(point, [self randomizeButtonRect])) {
        queueGuiParamValue(*_plugin, kRandomizeActionId, 0.0);
        requestSurfaceProcess(*_plugin);
        _selectedVoice = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, NSMakeRect(738, 103, 124, 15))) { [self openMenu:10]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 247, 124, 15))) { [self openMenu:3]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 417, 124, 15))) { [self openMenu:4]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 547, 124, 15))) { [self openMenu:5]; return; }
    if (NSPointInRect(point, NSMakeRect(1004, 599, 124, 15))) { [self openMenu:6]; return; }
    if (NSPointInRect(point, NSMakeRect(738, 443, 124, 15))) { [self openMenu:9]; return; }
    const NSRect panel = [self fieldPanelRect];
    if (NSPointInRect(point, panel)) {
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, [self pageButtonRect:i])) {
                if (_surfacePopupChild) return;
                _fieldPage = i;
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (_fieldPage == 1) {
            if (NSPointInRect(point, [self surfaceButtonRect:5])) {
                if (_surfacePopupChild) [_surfacePopupOwner hideSurfacePopup];
                else [self openSurfacePopup];
                [self setNeedsDisplay:YES];
                return;
            }
            if (NSPointInRect(point, [self surfaceButtonRect:0])) {
                [self syncSurfaceEditMode:!_surfaceEdit];
            } else if (NSPointInRect(point, [self surfaceButtonRect:1])) {
                if (_surfaceSnapshot.cellCount < 2u) NSBeep();
                else {
                    _surfaceSnapshot.enabled = !_surfaceSnapshot.enabled;
                    queueGuiSurface(*_plugin, _surfaceSnapshot);
                    requestSurfaceProcess(*_plugin);
                }
            } else if (NSPointInRect(point, [self surfaceButtonRect:2])) {
                NSString* name = [self presetDisplayName];
                if (s3g::addParameterSurfaceCell(_surfaceSnapshot,
                        _paramsSnapshot,
                        static_cast<int32_t>(_presetIndexSnapshot),
                        [name UTF8String])) {
                    _selectedSurfaceCell = static_cast<int>(
                        _surfaceSnapshot.cellCount) - 1;
                    queueGuiSurface(*_plugin, _surfaceSnapshot);
                } else NSBeep();
            } else if (NSPointInRect(point, [self surfaceButtonRect:3])) {
                if (_selectedSurfaceCell < 0
                    || !s3g::removeParameterSurfaceCell(_surfaceSnapshot,
                        static_cast<uint32_t>(_selectedSurfaceCell))) NSBeep();
                else queueGuiSurface(*_plugin, _surfaceSnapshot);
                _selectedSurfaceCell = _surfaceSnapshot.cellCount == 0u ? -1
                    : std::min(_selectedSurfaceCell,
                        static_cast<int>(_surfaceSnapshot.cellCount) - 1);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceButtonRect:4])) {
                if (_selectedSurfaceCell < 0
                    || static_cast<uint32_t>(_selectedSurfaceCell)
                        >= _surfaceSnapshot.cellCount) NSBeep();
                else {
                    auto& cell = _surfaceSnapshot.cells[
                        static_cast<uint32_t>(_selectedSurfaceCell)];
                    cell.params = _paramsSnapshot;
                    cell.presetIndex = static_cast<int32_t>(
                        _presetIndexSnapshot);
                    std::snprintf(cell.name, sizeof(cell.name), "%s",
                        [[self presetDisplayName] UTF8String]);
                    queueGuiSurface(*_plugin, _surfaceSnapshot);
                    requestSurfaceProcess(*_plugin);
                }
            } else if (NSPointInRect(point, [self surfaceCurveRect])) {
                const uint32_t next = (static_cast<uint32_t>(
                    _surfaceSnapshot.curve) + 1u)
                    % s3g::kParameterSurfaceCurveCount;
                _surfaceSnapshot.curve =
                    static_cast<s3g::ParameterSurfaceCurve>(next);
                queueGuiSurface(*_plugin, _surfaceSnapshot);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceFocusRect:0])
                || NSPointInRect(point, [self surfaceFocusRect:1])) {
                const float scale = NSPointInRect(point,
                    [self surfaceFocusRect:0]) ? 0.8f : 1.25f;
                _surfaceSnapshot.focus = std::clamp(
                    _surfaceSnapshot.focus * scale, 0.25f, 8.0f);
                queueGuiSurface(*_plugin, _surfaceSnapshot);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfaceGlideRect:0])
                || NSPointInRect(point, [self surfaceGlideRect:1])) {
                const int direction = NSPointInRect(point,
                    [self surfaceGlideRect:0]) ? -1 : 1;
                _surfaceSnapshot.glideMs =
                    s3g::parameterSurfaceSteppedGlide(
                        _surfaceSnapshot.glideMs, direction);
                queueGuiSurface(*_plugin, _surfaceSnapshot);
                requestSurfaceProcess(*_plugin);
            } else if (NSPointInRect(point, [self surfacePlotRect])) {
                if (_surfaceEdit) {
                    const int cell = [self hitSurfaceCell:point];
                    if (cell >= 0) {
                        _selectedSurfaceCell = cell;
                        _dragSurfaceCell = cell;
                    }
                } else {
                    _dragSurfaceCursor = YES;
                    [self updateSurfacePosition:point cursor:YES];
                }
            }
            [self setNeedsDisplay:YES];
            return;
        }
        for (int i = 0; i < 2; ++i) {
            if (NSPointInRect(point, [self zoomButtonRect:i])) {
                _viewZoom = std::clamp(_viewZoom + (i == 0 ? -0.15 : 0.15), 0.55, 2.20);
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (int i = 0; i < 3; ++i) {
            if (NSPointInRect(point, [self viewButtonRect:i])) {
                [self setViewPreset:i];
                return;
            }
        }
        const int hit = [self hitVoice:point];
        if (hit >= 0) {
            _selectedVoice = static_cast<uint32_t>(hit);
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, [self fieldRect])) {
            _dragView = YES;
            _lastDragPoint = point;
            _viewMode = -1;
            [self storeViewState];
            return;
        }
    }
    _dragParam = 0;
    for (const auto& spec : kGuiSliders) {
        if (NSPointInRect(point, NSMakeRect(spec.panelX + 8, spec.y - 8, 230, 24))) {
            double defaultValue = 0.0;
            if (s3g::clap_gui::sliderDoubleClickDefault(
                    event, &_plugin->plugin, spec.id, &defaultValue)) {
                assignParam(_paramsSnapshot, spec.id, defaultValue);
                queueGuiParamValue(*_plugin, spec.id, defaultValue);
                _dragParam = 0;
                [self setNeedsDisplay:YES];
                return;
            }
            _dragParam = static_cast<int>(spec.id);
            [self setParam:spec.id fromPoint:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_fieldPage == 1 && (_dragSurfaceCursor || _dragSurfaceCell >= 0)) {
        [self updateSurfacePosition:point cursor:_dragSurfaceCursor];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragView) {
        const CGFloat dx = point.x - _lastDragPoint.x;
        const CGFloat dy = point.y - _lastDragPoint.y;
        _viewAzDeg += dx * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + dy * 0.35, -85.0, 85.0);
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam) [self setParam:static_cast<clap_id>(_dragParam) fromPoint:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = 0;
    _dragView = NO;
    _dragBreakpointRow = -1;
    _dragSurfaceCell = -1;
    _dragSurfaceCursor = NO;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const int next = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
    if (next != _hoverMenuItem) {
        _hoverMenuItem = next;
        [self setNeedsDisplay:YES];
    }
}
@end

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
    p->guiView = [[S3GAmbiWindEncoderView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
        [static_cast<NSView*>(p->guiView) release]; p->guiView = nullptr; return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return;
    [static_cast<S3GAmbiWindEncoderView*>(p->guiView) destroySurfacePopup];
    [static_cast<S3GAmbiWindEncoderView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible.store(false, std::memory_order_release);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) { return s3g::clap_gui::getResponsiveResizeHints(hints); }
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* w, uint32_t* h) { return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport, kGuiWidth, kGuiHeight, w, h); }
bool guiSetSize(const clap_plugin_t* plugin, uint32_t w, uint32_t h) { return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, w, h); }

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* win)
{
    if (!win || std::strcmp(win->api, CLAP_WINDOW_API_COCOA) != 0 || !win->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(win->cocoa), p->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false; p->guiVisible.store(true, std::memory_order_release); [static_cast<S3GAmbiWindEncoderView*>(p->guiView) startRefreshTimer]; return true; }
bool guiHide(const clap_plugin_t* plugin) { auto* p = self(plugin); if (!p->guiView) return false; p->guiVisible.store(false, std::memory_order_release); [static_cast<S3GAmbiWindEncoderView*>(p->guiView) hideSurfacePopup]; [static_cast<S3GAmbiWindEncoderView*>(p->guiView) stopRefreshTimer]; return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true); }
const clap_plugin_gui_t guiExt { guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide };

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

constexpr const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_SURROUND, nullptr };
const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-wind-encoder-64",
    "s3g Ambi Encoder Wind",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.2.0",
    "Microweather wind environment whose 7OA ACN/SN3D field can steer its own flow, gusts, and turbulence.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiWindFactoryPreset(0u);
    p->engine.prepare(p->sampleRate);
    sanitizeWindState(*p);
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

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index) { return index == 0u ? &descriptor : nullptr; }
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}
const clap_plugin_factory_t factory { factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin };

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId) { return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
