#include "s3g_ambi_horizon_encoder.h"
#include "s3g_ambi_horizon_presets.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include "../common/s3g_clap_gui_param_queue.h"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_gui_layout.h"
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

constexpr uint32_t kOutputChannels = s3g::kAmbiHorizonMaxChannels;
constexpr uint32_t kStateVersion = 5u;

constexpr clap_id kPresetParamId = 1u;
constexpr clap_id kOrderParamId = 2u;
constexpr clap_id kEntitiesParamId = 3u;
constexpr clap_id kEcologyParamId = 4u;
constexpr clap_id kActivityParamId = 5u;
constexpr clap_id kOccupancyParamId = 6u;
constexpr clap_id kPaceParamId = 7u;
constexpr clap_id kMemoryParamId = 8u;
constexpr clap_id kCascadeParamId = 9u;
constexpr clap_id kSignalsParamId = 10u;
constexpr clap_id kBedParamId = 11u;
constexpr clap_id kFloorParamId = 12u;
constexpr clap_id kRangeParamId = 13u;
constexpr clap_id kAzimuthParamId = 14u;
constexpr clap_id kElevationParamId = 15u;
constexpr clap_id kArcParamId = 16u;
constexpr clap_id kDetailParamId = 17u;
constexpr clap_id kAirParamId = 18u;
constexpr clap_id kGroundParamId = 19u;
constexpr clap_id kTerrainParamId = 20u;
constexpr clap_id kCarryParamId = 21u;
constexpr clap_id kTurbulenceParamId = 22u;
constexpr clap_id kEdgeParamId = 23u;
constexpr clap_id kOutputParamId = 24u;
constexpr clap_id kSeedParamId = 25u;
constexpr clap_id kAirNoiseParamId = 26u;
constexpr clap_id kMachinesParamId = 27u;
constexpr clap_id kBellsParamId = 28u;
constexpr clap_id kTrafficParamId = 29u;
constexpr clap_id kAircraftParamId = 30u;
constexpr clap_id kFoghornsParamId = 31u;
constexpr clap_id kSurfParamId = 32u;
constexpr clap_id kTrafficSpeedParamId = 33u;
constexpr clap_id kEngineLoadParamId = 34u;
constexpr clap_id kAircraftFlightParamId = 35u;
constexpr clap_id kAircraftSpeedParamId = 36u;
constexpr clap_id kAircraftPowerParamId = 37u;
constexpr clap_id kAircraftToneParamId = 38u;
constexpr clap_id kFoghornPitchParamId = 39u;
constexpr clap_id kFoghornPressureParamId = 40u;
constexpr clap_id kFoghornLengthParamId = 41u;
constexpr clap_id kWaveRateParamId = 42u;
constexpr clap_id kWaveBreakParamId = 43u;
constexpr clap_id kMachineToneParamId = 44u;
constexpr clap_id kBellPitchParamId = 45u;
constexpr clap_id kBellDecayParamId = 46u;
constexpr clap_id kFieldListenModeParamId = 47u;
constexpr clap_id kFieldListenAmountParamId = 48u;
constexpr clap_id kFieldListenResponseParamId = 49u;
constexpr clap_id kParamCount = kFieldListenResponseParamId;

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double min;
    double max;
    double def;
    bool stepped;
    bool logarithmic;
};

constexpr std::array<ParamDef, kParamCount> kParams {{
    { kPresetParamId, "Preset", "Global", 0.0,
        static_cast<double>(s3g::kAmbiHorizonFactoryPresetCount - 1u), 0.0, true, false },
    { kOrderParamId, "Order", "Output", 1.0, 7.0, 3.0, true, false },
    { kEntitiesParamId, "Entities", "Ecology", 4.0, 32.0, 24.0, true, false },
    { kEcologyParamId, "Ecology", "Ecology", 0.0, 8.0, 0.0, true, false },
    { kActivityParamId, "Activity", "Ecology", 0.0, 1.0, 0.48, false, false },
    { kOccupancyParamId, "Occupancy", "Ecology", 0.0, 1.0, 0.36, false, false },
    { kPaceParamId, "Pace", "Score", 0.0, 1.0, 0.42, false, false },
    { kMemoryParamId, "Memory", "Score", 0.0, 1.0, 0.68, false, false },
    { kCascadeParamId, "Cascade", "Score", 0.0, 1.0, 0.48, false, false },
    { kSignalsParamId, "Signals", "Source Balance", 0.0, 1.0, 0.48, false, false },
    { kBedParamId, "Landscape Bed", "Source Balance", 0.0, 1.0, 0.35, false, false },
    { kFloorParamId, "Local Floor", "Source Balance", 0.0, 1.0, 0.22, false, false },
    { kRangeParamId, "Range", "Horizon", 0.03, 20.0, 2.8, false, true },
    { kAzimuthParamId, "Azimuth", "Horizon", -180.0, 180.0, 0.0, false, false },
    { kElevationParamId, "Elevation", "Horizon", -20.0, 20.0, 0.0, false, false },
    { kArcParamId, "Arc", "Horizon", 0.0, 360.0, 240.0, false, false },
    { kDetailParamId, "Detail", "Horizon", 0.0, 1.0, 0.54, false, false },
    { kAirParamId, "Air Loss", "Atmosphere", 0.0, 1.0, 0.58, false, false },
    { kGroundParamId, "Ground", "Atmosphere", 0.0, 4.0, 2.0, true, false },
    { kTerrainParamId, "Terrain", "Atmosphere", 0.0, 1.0, 0.42, false, false },
    { kCarryParamId, "Carry", "Atmosphere", -1.0, 1.0, 0.0, false, false },
    { kTurbulenceParamId, "Turbulence", "Atmosphere", 0.0, 1.0, 0.24, false, false },
    { kEdgeParamId, "Edge", "Horizon", -18.0, 9.0, 0.0, false, false },
    { kOutputParamId, "Output", "Output", -60.0, 12.0, -6.0, false, false },
    { kSeedParamId, "Seed", "Identity", 1.0, 65535.0, 1979.0, true, false },
    { kAirNoiseParamId, "Air Noise", "Atmosphere", 0.0, 1.0, 0.35, false, false },
    { kMachinesParamId, "Machines", "Generators", 0.0, 1.0, 0.45, false, false },
    { kBellsParamId, "Bells", "Generators", 0.0, 1.0, 0.25, false, false },
    { kTrafficParamId, "Traffic", "Generators", 0.0, 1.0, 0.45, false, false },
    { kAircraftParamId, "Aircraft", "Generators", 0.0, 1.0, 0.0, false, false },
    { kFoghornsParamId, "Foghorns", "Generators", 0.0, 1.0, 0.0, false, false },
    { kSurfParamId, "Surf", "Generators", 0.0, 1.0, 0.15, false, false },
    { kTrafficSpeedParamId, "Traffic Speed", "Traffic", 0.0, 1.0, 0.50, false, false },
    { kEngineLoadParamId, "Engine Load", "Traffic", 0.0, 1.0, 0.55, false, false },
    { kAircraftFlightParamId, "Aircraft Flight", "Aircraft", 0.0, 1.0, 0.80, false, false },
    { kAircraftSpeedParamId, "Aircraft Speed", "Aircraft", 0.0, 1.0, 0.52, false, false },
    { kAircraftPowerParamId, "Aircraft Power", "Aircraft", 0.0, 1.0, 0.62, false, false },
    { kAircraftToneParamId, "Aircraft Tone", "Aircraft", 0.0, 1.0, 0.35, false, false },
    { kFoghornPitchParamId, "Foghorn Pitch", "Foghorn", 0.0, 1.0, 0.42, false, false },
    { kFoghornPressureParamId, "Foghorn Pressure", "Foghorn", 0.0, 1.0, 0.75, false, false },
    { kFoghornLengthParamId, "Foghorn Length", "Foghorn", 0.0, 1.0, 0.55, false, false },
    { kWaveRateParamId, "Wave Rate", "Surf", 0.0, 1.0, 0.45, false, false },
    { kWaveBreakParamId, "Wave Break", "Surf", 0.0, 1.0, 0.58, false, false },
    { kMachineToneParamId, "Machine Tone", "Machines", 0.0, 1.0, 0.50, false, false },
    { kBellPitchParamId, "Bell Pitch", "Bells", 0.0, 1.0, 0.52, false, false },
    { kBellDecayParamId, "Bell Decay", "Bells", 0.0, 1.0, 0.68, false, false },
    { kFieldListenModeParamId, "Field Listen", "Listener", 0.0, 3.0, 0.0, true, false },
    { kFieldListenAmountParamId, "Listen Amount", "Listener", 0.0, 1.0, 0.65, false, false },
    { kFieldListenResponseParamId, "Listen Response", "Listener", 0.0, 3.0, 0.0, true, false },
}};

constexpr const char* kEcologyNames[] {
    "MIXED", "RURAL", "TRAFFIC", "CITY", "INDUSTRIAL", "WATER", "WEATHER",
    "AIRPORT", "COAST"
};
constexpr const char* kGroundNames[] {
    "WATER", "HARD", "MIXED", "GRASS", "FOREST"
};
constexpr const char* kFieldListenNames[] {
    "OFF", "FOLLOW", "COUNTER", "BALANCE"
};
constexpr const char* kFieldListenResponseNames[] {
    "REACH", "GLIMPSE", "SETTLE", "DISTANCE"
};

const ParamDef* paramDef(clap_id id)
{
    if (id < 1u || id > kParamCount) return nullptr;
    return &kParams[id - 1u];
}

// Parameter additions remain append-only so old state and custom-preset
// payloads can be read without guessing their binary layout.
struct AmbiHorizonEncoderParamsV1 {
    uint32_t order = 3u;
    uint32_t entities = 24u;
    s3g::AmbiHorizonEcology ecology = s3g::AmbiHorizonEcology::Mixed;
    float activity = 0.48f;
    float occupancy = 0.36f;
    float pace = 0.42f;
    float memory = 0.68f;
    float cascade = 0.48f;
    float signals = 0.48f;
    float horizonBed = 0.62f;
    float localFloor = 0.22f;
    float rangeKm = 2.8f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float arcDeg = 240.0f;
    float detail = 0.54f;
    float air = 0.58f;
    s3g::AmbiHorizonGround ground = s3g::AmbiHorizonGround::Mixed;
    float terrain = 0.42f;
    float carry = 0.0f;
    float turbulence = 0.24f;
    float edgeDb = 0.0f;
    float outputGainDb = -6.0f;
    uint32_t seed = 1979u;
};

struct AmbiHorizonEncoderParamsV2 {
    uint32_t order = 3u;
    uint32_t entities = 24u;
    s3g::AmbiHorizonEcology ecology = s3g::AmbiHorizonEcology::Mixed;
    float activity = 0.48f;
    float occupancy = 0.36f;
    float pace = 0.42f;
    float memory = 0.68f;
    float cascade = 0.48f;
    float signals = 0.48f;
    float horizonBed = 0.62f;
    float localFloor = 0.22f;
    float rangeKm = 2.8f;
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float arcDeg = 240.0f;
    float detail = 0.54f;
    float air = 0.58f;
    s3g::AmbiHorizonGround ground = s3g::AmbiHorizonGround::Mixed;
    float terrain = 0.42f;
    float carry = 0.0f;
    float turbulence = 0.24f;
    float edgeDb = 0.0f;
    float outputGainDb = -6.0f;
    uint32_t seed = 1979u;
    float airNoise = 0.35f;
};

struct AmbiHorizonEncoderParamsV3 {
    AmbiHorizonEncoderParamsV2 prefix {};
    float machines = 0.45f;
    float bells = 0.25f;
    float traffic = 0.45f;
    float aircraft = 0.0f;
    float foghorns = 0.0f;
    float surf = 0.15f;
};

struct AmbiHorizonEncoderParamsV4 {
    AmbiHorizonEncoderParamsV3 prefix {};
    float trafficSpeed = 0.50f;
    float engineLoad = 0.55f;
    float aircraftFlight = 0.80f;
    float aircraftSpeed = 0.52f;
    float aircraftPower = 0.62f;
    float aircraftTone = 0.35f;
    float foghornPitch = 0.42f;
    float foghornPressure = 0.75f;
    float foghornLength = 0.55f;
    float waveRate = 0.45f;
    float waveBreak = 0.58f;
    float machineTone = 0.50f;
    float bellPitch = 0.52f;
    float bellDecay = 0.68f;
};
static_assert(sizeof(AmbiHorizonEncoderParamsV1) + sizeof(float)
    == sizeof(AmbiHorizonEncoderParamsV2));
static_assert(sizeof(AmbiHorizonEncoderParamsV2) + 6u * sizeof(float)
    == sizeof(AmbiHorizonEncoderParamsV3));
static_assert(sizeof(AmbiHorizonEncoderParamsV3) + 14u * sizeof(float)
    == sizeof(AmbiHorizonEncoderParamsV4));
static_assert(sizeof(AmbiHorizonEncoderParamsV4)
        + sizeof(s3g::AmbiFieldListenMode) + sizeof(float)
        + sizeof(s3g::AmbiFieldListenerResponse)
    == sizeof(s3g::AmbiHorizonEncoderParams));

template <typename OldParams>
s3g::AmbiHorizonEncoderParams migratePrefix(const OldParams& old)
{
    s3g::AmbiHorizonEncoderParams p {};
    p.order = old.order; p.entities = old.entities; p.ecology = old.ecology;
    p.activity = old.activity; p.occupancy = old.occupancy; p.pace = old.pace;
    p.memory = old.memory; p.cascade = old.cascade; p.signals = old.signals;
    p.horizonBed = old.horizonBed; p.localFloor = old.localFloor;
    p.rangeKm = old.rangeKm; p.azimuthDeg = old.azimuthDeg;
    p.elevationDeg = old.elevationDeg; p.arcDeg = old.arcDeg;
    p.detail = old.detail; p.air = old.air; p.ground = old.ground;
    p.terrain = old.terrain; p.carry = old.carry;
    p.turbulence = old.turbulence; p.edgeDb = old.edgeDb;
    p.outputGainDb = old.outputGainDb; p.seed = old.seed;
    return p;
}

void setMigratedGeneratorMix(s3g::AmbiHorizonEncoderParams& p)
{
    // Earlier scenes always contained both hidden motor and modal engines.
    // Keep those paths present, then approximate the newly dedicated beds
    // from the old ecology without introducing aircraft or marine calls.
    p.machines = 1.0f;
    p.bells = 1.0f;
    p.traffic = p.ecology == s3g::AmbiHorizonEcology::Traffic ? 1.0f
        : (p.ecology == s3g::AmbiHorizonEcology::City ? 0.72f
        : (p.ecology == s3g::AmbiHorizonEcology::Industrial ? 0.30f
        : (p.ecology == s3g::AmbiHorizonEcology::Mixed ? 0.35f : 0.0f)));
    p.aircraft = 0.0f;
    p.foghorns = 0.0f;
    p.surf = p.ecology == s3g::AmbiHorizonEcology::Water ? 0.75f : 0.0f;
}

s3g::AmbiHorizonEncoderParams migrateParams(
    const AmbiHorizonEncoderParamsV1& old)
{
    auto p = migratePrefix(old);
    p.airNoise = 1.0f; // Preserve the version-1 broadband-air contribution.
    setMigratedGeneratorMix(p);
    return p;
}

s3g::AmbiHorizonEncoderParams migrateParams(
    const AmbiHorizonEncoderParamsV2& old)
{
    auto p = migratePrefix(old);
    p.airNoise = old.airNoise;
    setMigratedGeneratorMix(p);
    return p;
}

s3g::AmbiHorizonEncoderParams migrateParams(
    const AmbiHorizonEncoderParamsV3& old)
{
    auto p = migratePrefix(old.prefix);
    p.airNoise = old.prefix.airNoise;
    p.machines = old.machines;
    p.bells = old.bells;
    p.traffic = old.traffic;
    p.aircraft = old.aircraft;
    p.foghorns = old.foghorns;
    p.surf = old.surf;
    return p;
}

s3g::AmbiHorizonEncoderParams migrateParams(
    const AmbiHorizonEncoderParamsV4& old)
{
    auto p = migrateParams(old.prefix);
    p.trafficSpeed = old.trafficSpeed;
    p.engineLoad = old.engineLoad;
    p.aircraftFlight = old.aircraftFlight;
    p.aircraftSpeed = old.aircraftSpeed;
    p.aircraftPower = old.aircraftPower;
    p.aircraftTone = old.aircraftTone;
    p.foghornPitch = old.foghornPitch;
    p.foghornPressure = old.foghornPressure;
    p.foghornLength = old.foghornLength;
    p.waveRate = old.waveRate;
    p.waveBreak = old.waveBreak;
    p.machineTone = old.machineTone;
    p.bellPitch = old.bellPitch;
    p.bellDecay = old.bellDecay;
    return p;
}

struct SavedStateV1 {
    uint32_t version = 1u;
    AmbiHorizonEncoderParamsV1 params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct SavedStateV2 {
    uint32_t version = 2u;
    AmbiHorizonEncoderParamsV2 params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct SavedStateV3 {
    uint32_t version = 3u;
    AmbiHorizonEncoderParamsV3 params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct SavedStateV4 {
    uint32_t version = 4u;
    AmbiHorizonEncoderParamsV4 params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiHorizonEncoderParams params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 30.0f;
    float guiViewZoom = 1.0f;
};

struct PresetFile {
    uint32_t magic = 0x53483331u; // SH31
    uint32_t version = 5u;
    s3g::AmbiHorizonEncoderParams params {};
    char name[96] {};
};

struct PresetFileV1 {
    uint32_t magic = 0x53483331u;
    uint32_t version = 1u;
    AmbiHorizonEncoderParamsV1 params {};
    char name[96] {};
};

struct PresetFileV2 {
    uint32_t magic = 0x53483331u;
    uint32_t version = 2u;
    AmbiHorizonEncoderParamsV2 params {};
    char name[96] {};
};

struct PresetFileV3 {
    uint32_t magic = 0x53483331u;
    uint32_t version = 3u;
    AmbiHorizonEncoderParamsV3 params {};
    char name[96] {};
};

struct PresetFileV4 {
    uint32_t magic = 0x53483331u;
    uint32_t version = 4u;
    AmbiHorizonEncoderParamsV4 params {};
    char name[96] {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiHorizonEncoder engine {};
    s3g::AmbiHorizonEncoderParams params {};
    std::array<std::atomic<double>, kParamCount + 1u> visible {};
    std::atomic<uint32_t> presetIndex { 0u };
    s3g::clap_gui::ParamEventQueue<512u> guiEvents {};
    std::atomic<bool> active { false };
    std::atomic<bool> pendingRescan { false };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kAmbiHorizonMaxEntities> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiHorizonMaxEntities> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiHorizonMaxEntities> guiRange {};
    std::array<std::atomic<float>, s3g::kAmbiHorizonMaxEntities> guiEnergy {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiHorizonMaxEntities> guiLayer {};
    std::atomic<uint32_t> guiEntityCount { 0u };
    std::array<std::atomic<float>, s3g::kAmbiFieldListenerMaxLobes>
        guiListenerEnvelope {};
    std::atomic<float> guiListenerActivity { 0.0f };
    std::atomic<float> guiListenerReturnShare { 0.0f };
    std::atomic<int32_t> guiViewMode { 2 };
    std::atomic<float> guiViewAzDeg { 38.0f };
    std::atomic<float> guiViewElDeg { 30.0f };
    std::atomic<float> guiViewZoom { 1.0f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
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

double valueFromParams(const s3g::AmbiHorizonEncoderParams& p, clap_id id)
{
    switch (id) {
    case kOrderParamId: return p.order;
    case kEntitiesParamId: return p.entities;
    case kEcologyParamId: return static_cast<uint32_t>(p.ecology);
    case kActivityParamId: return p.activity;
    case kOccupancyParamId: return p.occupancy;
    case kPaceParamId: return p.pace;
    case kMemoryParamId: return p.memory;
    case kCascadeParamId: return p.cascade;
    case kSignalsParamId: return p.signals;
    case kBedParamId: return p.horizonBed;
    case kFloorParamId: return p.localFloor;
    case kRangeParamId: return p.rangeKm;
    case kAzimuthParamId: return p.azimuthDeg;
    case kElevationParamId: return p.elevationDeg;
    case kArcParamId: return p.arcDeg;
    case kDetailParamId: return p.detail;
    case kAirParamId: return p.air;
    case kGroundParamId: return static_cast<uint32_t>(p.ground);
    case kTerrainParamId: return p.terrain;
    case kCarryParamId: return p.carry;
    case kTurbulenceParamId: return p.turbulence;
    case kEdgeParamId: return p.edgeDb;
    case kOutputParamId: return p.outputGainDb;
    case kSeedParamId: return p.seed;
    case kAirNoiseParamId: return p.airNoise;
    case kMachinesParamId: return p.machines;
    case kBellsParamId: return p.bells;
    case kTrafficParamId: return p.traffic;
    case kAircraftParamId: return p.aircraft;
    case kFoghornsParamId: return p.foghorns;
    case kSurfParamId: return p.surf;
    case kTrafficSpeedParamId: return p.trafficSpeed;
    case kEngineLoadParamId: return p.engineLoad;
    case kAircraftFlightParamId: return p.aircraftFlight;
    case kAircraftSpeedParamId: return p.aircraftSpeed;
    case kAircraftPowerParamId: return p.aircraftPower;
    case kAircraftToneParamId: return p.aircraftTone;
    case kFoghornPitchParamId: return p.foghornPitch;
    case kFoghornPressureParamId: return p.foghornPressure;
    case kFoghornLengthParamId: return p.foghornLength;
    case kWaveRateParamId: return p.waveRate;
    case kWaveBreakParamId: return p.waveBreak;
    case kMachineToneParamId: return p.machineTone;
    case kBellPitchParamId: return p.bellPitch;
    case kBellDecayParamId: return p.bellDecay;
    case kFieldListenModeParamId:
        return static_cast<uint32_t>(p.fieldListenMode);
    case kFieldListenAmountParamId: return p.fieldListenAmount;
    case kFieldListenResponseParamId:
        return static_cast<uint32_t>(p.fieldListenResponse);
    default: return 0.0;
    }
}

void assignParam(s3g::AmbiHorizonEncoderParams& p, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def || id == kPresetParamId) return;
    value = std::clamp(std::isfinite(value) ? value : def->def, def->min, def->max);
    if (def->stepped) value = std::round(value);
    switch (id) {
    case kOrderParamId: p.order = static_cast<uint32_t>(value); break;
    case kEntitiesParamId: p.entities = static_cast<uint32_t>(value); break;
    case kEcologyParamId: p.ecology = static_cast<s3g::AmbiHorizonEcology>(static_cast<uint32_t>(value)); break;
    case kActivityParamId: p.activity = static_cast<float>(value); break;
    case kOccupancyParamId: p.occupancy = static_cast<float>(value); break;
    case kPaceParamId: p.pace = static_cast<float>(value); break;
    case kMemoryParamId: p.memory = static_cast<float>(value); break;
    case kCascadeParamId: p.cascade = static_cast<float>(value); break;
    case kSignalsParamId: p.signals = static_cast<float>(value); break;
    case kBedParamId: p.horizonBed = static_cast<float>(value); break;
    case kFloorParamId: p.localFloor = static_cast<float>(value); break;
    case kRangeParamId: p.rangeKm = static_cast<float>(value); break;
    case kAzimuthParamId: p.azimuthDeg = static_cast<float>(value); break;
    case kElevationParamId: p.elevationDeg = static_cast<float>(value); break;
    case kArcParamId: p.arcDeg = static_cast<float>(value); break;
    case kDetailParamId: p.detail = static_cast<float>(value); break;
    case kAirParamId: p.air = static_cast<float>(value); break;
    case kGroundParamId: p.ground = static_cast<s3g::AmbiHorizonGround>(static_cast<uint32_t>(value)); break;
    case kTerrainParamId: p.terrain = static_cast<float>(value); break;
    case kCarryParamId: p.carry = static_cast<float>(value); break;
    case kTurbulenceParamId: p.turbulence = static_cast<float>(value); break;
    case kEdgeParamId: p.edgeDb = static_cast<float>(value); break;
    case kOutputParamId: p.outputGainDb = static_cast<float>(value); break;
    case kSeedParamId: p.seed = static_cast<uint32_t>(value); break;
    case kAirNoiseParamId: p.airNoise = static_cast<float>(value); break;
    case kMachinesParamId: p.machines = static_cast<float>(value); break;
    case kBellsParamId: p.bells = static_cast<float>(value); break;
    case kTrafficParamId: p.traffic = static_cast<float>(value); break;
    case kAircraftParamId: p.aircraft = static_cast<float>(value); break;
    case kFoghornsParamId: p.foghorns = static_cast<float>(value); break;
    case kSurfParamId: p.surf = static_cast<float>(value); break;
    case kTrafficSpeedParamId: p.trafficSpeed = static_cast<float>(value); break;
    case kEngineLoadParamId: p.engineLoad = static_cast<float>(value); break;
    case kAircraftFlightParamId: p.aircraftFlight = static_cast<float>(value); break;
    case kAircraftSpeedParamId: p.aircraftSpeed = static_cast<float>(value); break;
    case kAircraftPowerParamId: p.aircraftPower = static_cast<float>(value); break;
    case kAircraftToneParamId: p.aircraftTone = static_cast<float>(value); break;
    case kFoghornPitchParamId: p.foghornPitch = static_cast<float>(value); break;
    case kFoghornPressureParamId: p.foghornPressure = static_cast<float>(value); break;
    case kFoghornLengthParamId: p.foghornLength = static_cast<float>(value); break;
    case kWaveRateParamId: p.waveRate = static_cast<float>(value); break;
    case kWaveBreakParamId: p.waveBreak = static_cast<float>(value); break;
    case kMachineToneParamId: p.machineTone = static_cast<float>(value); break;
    case kBellPitchParamId: p.bellPitch = static_cast<float>(value); break;
    case kBellDecayParamId: p.bellDecay = static_cast<float>(value); break;
    case kFieldListenModeParamId:
        p.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(
            static_cast<uint32_t>(value));
        break;
    case kFieldListenAmountParamId:
        p.fieldListenAmount = static_cast<float>(value);
        break;
    case kFieldListenResponseParamId:
        p.fieldListenResponse = static_cast<s3g::AmbiFieldListenerResponse>(
            static_cast<uint32_t>(value));
        break;
    default: break;
    }
}

s3g::AmbiHorizonEncoderParams visibleParams(const Plugin& plugin)
{
    s3g::AmbiHorizonEncoderParams p {};
    for (clap_id id = kOrderParamId; id <= kParamCount; ++id) {
        assignParam(p, id, plugin.visible[id].load(std::memory_order_acquire));
    }
    return p;
}

void publishParams(Plugin& plugin)
{
    for (clap_id id = kOrderParamId; id <= kParamCount; ++id) {
        plugin.visible[id].store(valueFromParams(plugin.params, id),
            std::memory_order_release);
    }
    plugin.visible[kPresetParamId].store(
        static_cast<double>(plugin.presetIndex.load(std::memory_order_relaxed)),
        std::memory_order_release);
}

void requestRescan(Plugin& plugin)
{
    plugin.pendingRescan.store(true, std::memory_order_release);
    if (plugin.host && plugin.host->request_callback) {
        plugin.host->request_callback(plugin.host);
    }
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    if (id == kPresetParamId) {
        const uint32_t preset = std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)),
            s3g::kAmbiHorizonFactoryPresetCount - 1u);
        const uint32_t order = plugin.params.order;
        const float output = plugin.params.outputGainDb;
        const auto listenMode = plugin.params.fieldListenMode;
        const float listenAmount = plugin.params.fieldListenAmount;
        const auto listenResponse = plugin.params.fieldListenResponse;
        plugin.params = s3g::ambiHorizonFactoryPreset(preset);
        plugin.params.order = order;
        plugin.params.outputGainDb = output;
        plugin.params.fieldListenMode = listenMode;
        plugin.params.fieldListenAmount = listenAmount;
        plugin.params.fieldListenResponse = listenResponse;
        plugin.presetIndex.store(preset, std::memory_order_release);
    } else {
        assignParam(plugin.params, id, value);
    }
    plugin.engine.setParams(plugin.params);
    plugin.params = plugin.engine.params();
    publishParams(plugin);
}

void requestGuiService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush) {
        plugin.hostParams->request_flush(plugin.host);
    } else if (plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

bool queueGuiValue(Plugin& plugin, clap_id id, double value)
{
    const auto* def = paramDef(id);
    if (!def) return false;
    value = std::clamp(std::isfinite(value) ? value : def->def, def->min, def->max);
    if (def->stepped) value = std::round(value);
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { s3g::clap_gui::ParamEventKind::GestureBegin, id, 0.0 },
        { s3g::clap_gui::ParamEventKind::Value, id, value },
        { s3g::clap_gui::ParamEventKind::GestureEnd, id, 0.0 },
    }};
    if (!plugin.guiEvents.pushBatch(events.data(), static_cast<uint32_t>(events.size()))) {
        return false;
    }
    plugin.visible[id].store(value, std::memory_order_release);
    requestGuiService(plugin);
    return true;
}

void queueParams(Plugin& plugin, const s3g::AmbiHorizonEncoderParams& params,
                 uint32_t presetIndex)
{
    plugin.presetIndex.store(std::min<uint32_t>(presetIndex,
        s3g::kAmbiHorizonFactoryPresetCount - 1u), std::memory_order_release);
    plugin.visible[kPresetParamId].store(presetIndex, std::memory_order_release);
    for (clap_id id = kOrderParamId; id <= kParamCount; ++id) {
        (void)queueGuiValue(plugin, id, valueFromParams(params, id));
    }
    requestRescan(plugin);
}

bool pushGuiEvent(const clap_output_events_t* out,
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
    event.header.type = pending.kind == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return out->try_push(out, &event.header);
}

void serviceGuiEvents(Plugin& plugin, const clap_output_events_t* out)
{
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiEvents.peek(pending)) {
        if (!pushGuiEvent(out, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(plugin, pending.paramId, pending.value);
        }
        plugin.guiEvents.pop();
    }
}

void readParamEvents(Plugin& plugin, const clap_input_events_t* in)
{
    if (!in) return;
    const uint32_t count = in->size(in);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* header = in->get(in, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        applyParam(plugin, event->param_id, event->value);
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
    p->sampleRate = sampleRate;
    p->params = visibleParams(*p);
    p->engine.prepare(sampleRate);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
    publishParams(*p);
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
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_release);
}

clap_process_status process(const clap_plugin_t* plugin,
                            const clap_process_t* proc)
{
    auto* p = self(plugin);
    serviceGuiEvents(*p, proc->out_events);
    readParamEvents(*p, proc->in_events);
    if (proc->audio_outputs_count == 0u) return CLAP_PROCESS_CONTINUE;
    auto& output = proc->audio_outputs[0];
    s3g::clearAudioBuffer(output, proc->frames_count);
    if (!output.data32) return CLAP_PROCESS_CONTINUE;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    std::array<float*, kOutputChannels> pointers {};
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        pointers[channel] = output.data32[channel];
    }
    p->engine.processBlock(pointers.data(), channels, proc->frames_count);

    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (!pointers[channel]) continue;
        for (uint32_t frame = 0u; frame < proc->frames_count; ++frame) {
            peak = std::max(peak, std::abs(pointers[channel][frame]));
        }
    }
    const float previous = p->outputPeak.load(std::memory_order_relaxed);
    p->outputPeak.store(std::max(peak, previous * 0.94f), std::memory_order_relaxed);

#if defined(__APPLE__)
    if (p->guiVisible.load(std::memory_order_relaxed)) {
        const uint32_t entities = p->engine.activeEntities();
        p->guiEntityCount.store(entities, std::memory_order_relaxed);
        for (uint32_t index = 0u; index < entities; ++index) {
            const auto point = p->engine.entityTelemetry(index);
            p->guiAzimuth[index].store(point.azimuthDeg, std::memory_order_relaxed);
            p->guiElevation[index].store(point.elevationDeg, std::memory_order_relaxed);
            p->guiRange[index].store(point.rangeNorm, std::memory_order_relaxed);
            p->guiEnergy[index].store(point.energy, std::memory_order_relaxed);
            p->guiLayer[index].store(static_cast<uint32_t>(point.layer), std::memory_order_relaxed);
        }
        for (uint32_t lobe = 0u;
             lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
            p->guiListenerEnvelope[lobe].store(
                p->engine.fieldListenEnvelope(lobe),
                std::memory_order_relaxed);
        }
        p->guiListenerActivity.store(
            p->engine.fieldListenActivity(), std::memory_order_relaxed);
        p->guiListenerReturnShare.store(
            p->engine.fieldListenReturnShare(), std::memory_order_relaxed);
    }
#endif
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->pendingRescan.exchange(false, std::memory_order_acq_rel)
        && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
                   clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    info->id = 20u;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
                   clap_param_info_t* info)
{
    if (!info || index >= kParams.size()) return false;
    const auto& def = kParams[index];
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, def.name, sizeof(info->name));
    std::strncpy(info->module, def.module, sizeof(info->module));
    info->min_value = def.min;
    info->max_value = def.max;
    info->default_value = def.def;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramDef(id)) return false;
    *value = self(plugin)->visible[id].load(std::memory_order_acquire);
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
                       char* display, uint32_t size)
{
    if (!display || size == 0u || !paramDef(id)) return false;
    if (id == kPresetParamId) {
        const uint32_t index = std::min<uint32_t>(static_cast<uint32_t>(
            std::lround(value)), s3g::kAmbiHorizonFactoryPresetCount - 1u);
        std::snprintf(display, size, "%s", s3g::kAmbiHorizonPresetInfo[index].name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kEcologyParamId) {
        std::snprintf(display, size, "%s", kEcologyNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 8u)]);
    } else if (id == kGroundParamId) {
        std::snprintf(display, size, "%s", kGroundNames[std::min<uint32_t>(
            static_cast<uint32_t>(std::lround(value)), 4u)]);
    } else if (id == kFieldListenModeParamId) {
        std::snprintf(display, size, "%s", kFieldListenNames[
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kFieldListenResponseParamId) {
        std::snprintf(display, size, "%s", kFieldListenResponseNames[
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kRangeParamId) {
        std::snprintf(display, size, value < 1.0 ? "%.2f km" : "%.1f km", value);
    } else if (id == kAircraftFlightParamId) {
        const char* stage = value < 0.12 ? "LANDED"
            : (value < 0.32 ? "TAXI"
            : (value < 0.56 ? "TKOFF"
            : (value < 0.82 ? "APPRCH" : "OVRHD")));
        std::snprintf(display, size, "%s", stage);
    } else if (id == kFoghornPitchParamId) {
        std::snprintf(display, size, "%.0f Hz",
            55.0 * std::pow(2.0, value * 1.80));
    } else if (id == kFoghornLengthParamId) {
        std::snprintf(display, size, "%.1f s", 1.8 + value * 6.8);
    } else if (id == kAzimuthParamId || id == kElevationParamId || id == kArcParamId) {
        std::snprintf(display, size, "%.0f deg", value);
    } else if (id == kEdgeParamId || id == kOutputParamId) {
        std::snprintf(display, size, "%.1f dB", value);
    } else if (id == kEntitiesParamId || id == kSeedParamId) {
        std::snprintf(display, size, "%.0f", value);
    } else {
        std::snprintf(display, size, "%.0f %%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display,
                       double* value)
{
    if (!display || !value || !paramDef(id)) return false;
    if (id == kPresetParamId) {
        for (uint32_t index = 0u;
             index < s3g::kAmbiHorizonFactoryPresetCount; ++index) {
            if (std::strcmp(display,
                    s3g::kAmbiHorizonPresetInfo[index].name) == 0) {
                *value = static_cast<double>(index);
                return true;
            }
        }
    }
    if (id == kEcologyParamId) {
        for (uint32_t index = 0u; index < std::size(kEcologyNames); ++index) {
            if (std::strcmp(display, kEcologyNames[index]) == 0) {
                *value = index; return true;
            }
        }
    }
    if (id == kGroundParamId) {
        for (uint32_t index = 0u; index < std::size(kGroundNames); ++index) {
            if (std::strcmp(display, kGroundNames[index]) == 0) {
                *value = index; return true;
            }
        }
    }
    if (id == kFieldListenModeParamId) {
        for (uint32_t index = 0u; index < std::size(kFieldListenNames); ++index) {
            if (std::strcmp(display, kFieldListenNames[index]) == 0) {
                *value = index; return true;
            }
        }
    }
    if (id == kFieldListenResponseParamId) {
        for (uint32_t index = 0u;
             index < std::size(kFieldListenResponseNames); ++index) {
            if (std::strcmp(display, kFieldListenResponseNames[index]) == 0) {
                *value = index; return true;
            }
        }
    }
    if (id == kAircraftFlightParamId) {
        if (std::strcmp(display, "LANDED") == 0) { *value = 0.0; return true; }
        if (std::strcmp(display, "TAXI") == 0) { *value = 0.22; return true; }
        if (std::strcmp(display, "TAKEOFF") == 0
            || std::strcmp(display, "TKOFF") == 0) {
            *value = 0.44; return true;
        }
        if (std::strcmp(display, "APPROACH") == 0
            || std::strcmp(display, "APPRCH") == 0) {
            *value = 0.70; return true;
        }
        if (std::strcmp(display, "OVERHEAD") == 0
            || std::strcmp(display, "OVRHD") == 0) {
            *value = 1.0; return true;
        }
    }
    if (id == kFoghornPitchParamId) {
        const double hz = std::max(55.0, std::atof(display));
        *value = std::clamp(std::log2(hz / 55.0) / 1.80, 0.0, 1.0);
        return true;
    }
    if (id == kFoghornLengthParamId) {
        *value = std::clamp((std::atof(display) - 1.8) / 6.8, 0.0, 1.0);
        return true;
    }
    *value = std::atof(display);
    if (std::strchr(display, '%')) *value *= 0.01;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in,
                 const clap_output_events_t* out)
{
    auto* p = self(plugin);
    serviceGuiEvents(*p, out);
    readParamEvents(*p, in);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    SavedState state {};
    state.params = visibleParams(*p);
    state.presetIndex = p->presetIndex.load(std::memory_order_acquire);
    state.guiViewMode = p->guiViewMode.load(std::memory_order_acquire);
    state.guiViewAzDeg = p->guiViewAzDeg.load(std::memory_order_acquire);
    state.guiViewElDeg = p->guiViewElDeg.load(std::memory_order_acquire);
    state.guiViewZoom = p->guiViewZoom.load(std::memory_order_acquire);
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedState state {};
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    if (version == 1u) {
        SavedStateV1 legacy {};
        legacy.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        state.params = migrateParams(legacy.params);
        state.presetIndex = legacy.presetIndex;
        state.guiViewMode = legacy.guiViewMode;
        state.guiViewAzDeg = legacy.guiViewAzDeg;
        state.guiViewElDeg = legacy.guiViewElDeg;
        state.guiViewZoom = legacy.guiViewZoom;
    } else if (version == 2u) {
        SavedStateV2 legacy {};
        legacy.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        state.params = migrateParams(legacy.params);
        state.presetIndex = legacy.presetIndex;
        state.guiViewMode = legacy.guiViewMode;
        state.guiViewAzDeg = legacy.guiViewAzDeg;
        state.guiViewElDeg = legacy.guiViewElDeg;
        state.guiViewZoom = legacy.guiViewZoom;
    } else if (version == 3u) {
        SavedStateV3 legacy {};
        legacy.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        state.params = migrateParams(legacy.params);
        state.presetIndex = legacy.presetIndex;
        state.guiViewMode = legacy.guiViewMode;
        state.guiViewAzDeg = legacy.guiViewAzDeg;
        state.guiViewElDeg = legacy.guiViewElDeg;
        state.guiViewZoom = legacy.guiViewZoom;
    } else if (version == 4u) {
        SavedStateV4 legacy {};
        legacy.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        state.params = migrateParams(legacy.params);
        state.presetIndex = legacy.presetIndex;
        state.guiViewMode = legacy.guiViewMode;
        state.guiViewAzDeg = legacy.guiViewAzDeg;
        state.guiViewElDeg = legacy.guiViewElDeg;
        state.guiViewZoom = legacy.guiViewZoom;
    } else if (version == kStateVersion) {
        state.version = version;
        if (!readExact(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) {
            return false;
        }
    } else {
        return false;
    }
    auto* p = self(plugin);
    p->presetIndex.store(std::min<uint32_t>(state.presetIndex,
        s3g::kAmbiHorizonFactoryPresetCount - 1u), std::memory_order_release);
    p->guiViewMode.store(state.guiViewMode, std::memory_order_release);
    p->guiViewAzDeg.store(state.guiViewAzDeg, std::memory_order_release);
    p->guiViewElDeg.store(state.guiViewElDeg, std::memory_order_release);
    p->guiViewZoom.store(state.guiViewZoom, std::memory_order_release);
    if (p->active.load(std::memory_order_acquire)) {
        queueParams(*p, state.params, state.presetIndex);
    } else {
        p->params = state.params;
        p->engine.setParams(p->params);
        p->params = p->engine.params();
        publishParams(*p);
    }
    requestRescan(*p);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

constexpr uint32_t kGuiWidth = 1160u;
constexpr uint32_t kGuiHeight = 760u;
namespace layout = s3g::gui_layout;

inline constexpr layout::Canvas kCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
inline constexpr auto kOutputPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Output,
    layout::kLargeEncoderFirstColumn, 42.0, 2u);
inline constexpr auto kEcologyPanel = layout::fittedStackPanel(
    layout::PanelRole::Engine, kOutputPanel, 5u);
inline constexpr auto kGeneratorsPanel = layout::fittedStackPanel(
    layout::PanelRole::Source, kEcologyPanel, 8u, 24.0, 34.0);
inline constexpr auto kScorePanel = layout::fittedStackPanel(
    layout::PanelRole::Modulation, kGeneratorsPanel, 5u);
inline constexpr auto kIdentityPanel = layout::fittedStackPanel(
    layout::PanelRole::Utility, kScorePanel, 1u);
inline constexpr std::array<layout::Panel, 5u> kFirstColumnPanels {{
    kOutputPanel, kEcologyPanel, kGeneratorsPanel, kScorePanel, kIdentityPanel
}};
inline constexpr auto kHorizonPanel = layout::fittedPanel(
    layout::PluginClass::ProceduralEncoder, layout::PanelRole::Topology,
    layout::kLargeEncoderSecondColumn, 42.0, 6u);
inline constexpr auto kAtmospherePanel = layout::fittedStackPanel(
    layout::PanelRole::Listener, kHorizonPanel, 6u);
inline constexpr auto kListenerPanel = layout::fittedStackPanel(
    layout::PanelRole::Listener, kAtmospherePanel, 3u);
inline constexpr std::array<layout::Panel, 3u> kSecondColumnPanels {{
    kHorizonPanel, kAtmospherePanel, kListenerPanel
}};
static_assert(layout::validateColumn(kFirstColumnPanels, kCanvas));
static_assert(layout::validateColumn(kSecondColumnPanels, kCanvas, false));
static_assert(layout::controlMatchesSlot(kOutputPanel,
    layout::kLargeEncoderOrderSlot));
static_assert(layout::panelMatchesAnchor(kHorizonPanel,
    layout::kLargeEncoderTopologyAnchor));

struct GuiSliderSpec {
    clap_id id;
    const layout::Panel* panel;
    uint32_t row;
};

inline constexpr std::array<GuiSliderSpec, 23u> kGuiSliders {{
    { kOutputParamId, &kOutputPanel, 0u },
    { kEntitiesParamId, &kEcologyPanel, 1u },
    { kActivityParamId, &kEcologyPanel, 2u },
    { kOccupancyParamId, &kEcologyPanel, 3u },
    { kPaceParamId, &kEcologyPanel, 4u },
    { kMemoryParamId, &kScorePanel, 0u },
    { kCascadeParamId, &kScorePanel, 1u },
    { kSignalsParamId, &kScorePanel, 2u },
    { kBedParamId, &kScorePanel, 3u },
    { kFloorParamId, &kScorePanel, 4u },
    { kSeedParamId, &kIdentityPanel, 0u },
    { kRangeParamId, &kHorizonPanel, 0u },
    { kAzimuthParamId, &kHorizonPanel, 1u },
    { kElevationParamId, &kHorizonPanel, 2u },
    { kArcParamId, &kHorizonPanel, 3u },
    { kDetailParamId, &kHorizonPanel, 4u },
    { kEdgeParamId, &kHorizonPanel, 5u },
    { kAirParamId, &kAtmospherePanel, 0u },
    { kAirNoiseParamId, &kAtmospherePanel, 1u },
    { kTerrainParamId, &kAtmospherePanel, 3u },
    { kCarryParamId, &kAtmospherePanel, 4u },
    { kTurbulenceParamId, &kAtmospherePanel, 5u },
    { kFieldListenAmountParamId, &kListenerPanel, 1u },
}};

struct GeneratorControl {
    clap_id id = CLAP_INVALID_ID;
    const char* label = "";
};

struct GeneratorPanelControls {
    std::array<GeneratorControl, 8u> rows {};
};

inline constexpr GeneratorPanelControls kMixedGeneratorControls { {{
    { kMachinesParamId, "MACHINES" }, { kMachineToneParamId, "MACHINE TONE" },
    { kBellsParamId, "BELLS" }, { kBellPitchParamId, "BELL PITCH" },
    { kTrafficParamId, "TRAFFIC" }, { kEngineLoadParamId, "ENGINE LOAD" },
    { kAircraftParamId, "AIRCRAFT" }, { kAircraftFlightParamId, "FLIGHT" },
}} };
inline constexpr GeneratorPanelControls kRuralGeneratorControls { {{
    { kBellsParamId, "BELLS" }, { kBellPitchParamId, "BELL PITCH" },
    { kBellDecayParamId, "BELL DECAY" }, { kMachinesParamId, "MACHINES" },
    { kMachineToneParamId, "MACHINE TONE" }, { kAircraftParamId, "AIRCRAFT" },
    { kAircraftFlightParamId, "FLIGHT" }, {},
}} };
inline constexpr GeneratorPanelControls kTrafficGeneratorControls { {{
    { kTrafficParamId, "TRAFFIC" }, { kTrafficSpeedParamId, "ROAD SPEED" },
    { kEngineLoadParamId, "ENGINE LOAD" }, { kMachinesParamId, "MACHINES" },
    { kMachineToneParamId, "MACHINE TONE" }, { kAircraftParamId, "AIRCRAFT" },
    { kAircraftFlightParamId, "FLIGHT" }, { kAircraftSpeedParamId, "PASS SPEED" },
}} };
inline constexpr GeneratorPanelControls kIndustrialGeneratorControls { {{
    { kMachinesParamId, "MACHINES" }, { kMachineToneParamId, "MACHINE TONE" },
    { kBellsParamId, "BELLS" }, { kBellPitchParamId, "BELL PITCH" },
    { kBellDecayParamId, "BELL DECAY" }, { kTrafficParamId, "TRAFFIC" },
    { kTrafficSpeedParamId, "ROAD SPEED" }, { kEngineLoadParamId, "ENGINE LOAD" },
}} };
inline constexpr GeneratorPanelControls kWeatherGeneratorControls { {{
    { kMachinesParamId, "MACHINES" }, { kMachineToneParamId, "MACHINE TONE" },
    { kAircraftParamId, "AIRCRAFT" }, { kAircraftFlightParamId, "FLIGHT" },
    { kSurfParamId, "SURF" }, { kWaveRateParamId, "WAVE RATE" },
    { kWaveBreakParamId, "WAVE BREAK" }, {},
}} };
inline constexpr GeneratorPanelControls kAirportGeneratorControls { {{
    { kAircraftParamId, "AIRCRAFT" }, { kAircraftFlightParamId, "FLIGHT" },
    { kAircraftSpeedParamId, "PASS SPEED" }, { kAircraftPowerParamId, "ENGINE POWER" },
    { kAircraftToneParamId, "ENGINE TONE" }, { kTrafficParamId, "TRAFFIC" },
    { kTrafficSpeedParamId, "ROAD SPEED" }, { kEngineLoadParamId, "ENGINE LOAD" },
}} };
inline constexpr GeneratorPanelControls kCoastGeneratorControls { {{
    { kFoghornsParamId, "FOGHORNS" }, { kFoghornPitchParamId, "HORN PITCH" },
    { kFoghornPressureParamId, "HORN PRESSURE" },
    { kFoghornLengthParamId, "CALL LENGTH" }, { kSurfParamId, "SURF" },
    { kWaveRateParamId, "WAVE RATE" }, { kWaveBreakParamId, "WAVE BREAK" }, {},
}} };

const GeneratorPanelControls& generatorPanelControls(
    s3g::AmbiHorizonEcology ecology)
{
    switch (ecology) {
    case s3g::AmbiHorizonEcology::Rural: return kRuralGeneratorControls;
    case s3g::AmbiHorizonEcology::Traffic:
    case s3g::AmbiHorizonEcology::City: return kTrafficGeneratorControls;
    case s3g::AmbiHorizonEcology::Industrial: return kIndustrialGeneratorControls;
    case s3g::AmbiHorizonEcology::Water:
    case s3g::AmbiHorizonEcology::Coast: return kCoastGeneratorControls;
    case s3g::AmbiHorizonEcology::Weather: return kWeatherGeneratorControls;
    case s3g::AmbiHorizonEcology::Airport: return kAirportGeneratorControls;
    default: return kMixedGeneratorControls;
    }
}

bool guiSliderSpec(clap_id id, s3g::AmbiHorizonEcology ecology,
                   GuiSliderSpec& result)
{
    for (const auto& spec : kGuiSliders) {
        if (spec.id == id) { result = spec; return true; }
    }
    const auto& controls = generatorPanelControls(ecology);
    for (uint32_t row = 0u; row < controls.rows.size(); ++row) {
        if (controls.rows[row].id == id) {
            result = { id, &kGeneratorsPanel, row };
            return true;
        }
    }
    return false;
}

double sliderNorm(const GuiSliderSpec& spec, double value)
{
    const auto* def = paramDef(spec.id);
    if (!def) return 0.0;
    if (spec.id == kAzimuthParamId) {
        return s3g::aedAzimuthSliderNorm(static_cast<float>(value));
    }
    if (def->logarithmic) {
        return std::log(std::max(def->min, value) / def->min)
            / std::log(def->max / def->min);
    }
    return (value - def->min) / (def->max - def->min);
}

double sliderValue(const GuiSliderSpec& spec, NSPoint point)
{
    const auto* def = paramDef(spec.id);
    if (!def) return 0.0;
    constexpr double kTrackOffset = 108.0;
    constexpr double kTrackWidth = 82.0;
    const double norm = std::clamp(
        (point.x - (spec.panel->frame.x + kTrackOffset)) / kTrackWidth,
        0.0, 1.0);
    if (spec.id == kAzimuthParamId) {
        return s3g::aedAzimuthFromSliderNorm(static_cast<float>(norm));
    }
    double value = def->logarithmic
        ? def->min * std::pow(def->max / def->min, norm)
        : def->min + norm * (def->max - def->min);
    if (def->stepped) value = std::round(value);
    return value;
}

bool savePresetFile(const char* path, const PresetFile& preset)
{
    if (!path) return false;
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const bool ok = std::fwrite(&preset, sizeof(preset), 1u, file) == 1u;
    std::fclose(file);
    return ok;
}

bool loadPresetFile(const char* path, PresetFile& preset)
{
    if (!path) return false;
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    uint32_t header[2] {};
    bool ok = std::fread(header, sizeof(header), 1u, file) == 1u
        && header[0] == 0x53483331u;
    if (ok && header[1] == 1u) {
        PresetFileV1 legacy {};
        legacy.magic = header[0]; legacy.version = header[1];
        ok = std::fread(reinterpret_cast<uint8_t*>(&legacy) + sizeof(header),
            sizeof(legacy) - sizeof(header), 1u, file) == 1u;
        if (ok) {
            preset = {};
            preset.params = migrateParams(legacy.params);
            std::memcpy(preset.name, legacy.name, sizeof(preset.name));
        }
    } else if (ok && header[1] == 2u) {
        PresetFileV2 legacy {};
        legacy.magic = header[0]; legacy.version = header[1];
        ok = std::fread(reinterpret_cast<uint8_t*>(&legacy) + sizeof(header),
            sizeof(legacy) - sizeof(header), 1u, file) == 1u;
        if (ok) {
            preset = {};
            preset.params = migrateParams(legacy.params);
            std::memcpy(preset.name, legacy.name, sizeof(preset.name));
        }
    } else if (ok && header[1] == 3u) {
        PresetFileV3 legacy {};
        legacy.magic = header[0]; legacy.version = header[1];
        ok = std::fread(reinterpret_cast<uint8_t*>(&legacy) + sizeof(header),
            sizeof(legacy) - sizeof(header), 1u, file) == 1u;
        if (ok) {
            preset = {};
            preset.params = migrateParams(legacy.params);
            std::memcpy(preset.name, legacy.name, sizeof(preset.name));
        }
    } else if (ok && header[1] == 4u) {
        PresetFileV4 legacy {};
        legacy.magic = header[0]; legacy.version = header[1];
        ok = std::fread(reinterpret_cast<uint8_t*>(&legacy) + sizeof(header),
            sizeof(legacy) - sizeof(header), 1u, file) == 1u;
        if (ok) {
            preset = {};
            preset.params = migrateParams(legacy.params);
            std::memcpy(preset.name, legacy.name, sizeof(preset.name));
        }
    } else if (ok && header[1] == 5u) {
        preset.magic = header[0]; preset.version = header[1];
        ok = std::fread(reinterpret_cast<uint8_t*>(&preset) + sizeof(header),
            sizeof(preset) - sizeof(header), 1u, file) == 1u;
    } else {
        ok = false;
    }
    std::fclose(file);
    return ok;
}

} // namespace

@interface S3GAmbiHorizonEncoderFutureView : NSView {
@private
    Plugin* _plugin;
    NSTimer* _timer;
    s3g::AmbiHorizonEncoderParams _snapshot;
    uint32_t _presetSnapshot;
    int _dragParam;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _viewMode;
    double _viewAzDeg;
    double _viewElDeg;
    double _viewZoom;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiHorizonEncoderFutureView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _snapshot = plugin ? visibleParams(*plugin) : s3g::AmbiHorizonEncoderParams {};
        _presetSnapshot = plugin ? plugin->presetIndex.load(std::memory_order_acquire) : 0u;
        _dragParam = 0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _viewMode = plugin ? plugin->guiViewMode.load(std::memory_order_acquire) : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg.load(std::memory_order_acquire) : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg.load(std::memory_order_acquire) : 30.0;
        _viewZoom = plugin ? plugin->guiViewZoom.load(std::memory_order_acquire) : 1.0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _openMenuRect = NSZeroRect;
        [self setWantsLayer:YES];
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
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0
        target:self selector:@selector(timerTick:) userInfo:nil repeats:YES];
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
    [self refreshSnapshot];
    [self setNeedsDisplay:YES];
}

- (void)refreshSnapshot
{
    if (!_plugin) return;
    _snapshot = visibleParams(*_plugin);
    _presetSnapshot = _plugin->presetIndex.load(std::memory_order_acquire);
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 700); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 650); }
- (NSRect)presetMenuRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, layout::EncoderTitleAction::Preset); }
- (NSRect)loadPresetButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, layout::EncoderTitleAction::Load); }
- (NSRect)savePresetButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, layout::EncoderTitleAction::Save); }
- (NSRect)randomizeButtonRect { return s3g::clap_gui::encoderTitleActionRect(kGuiWidth, kGuiHeight, layout::EncoderTitleAction::Random); }

- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(NSMaxX(panel) - 148.0 + index * 46.0,
        panel.origin.y + 4.0, 40.0, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    return NSMakeRect(NSMaxX([self fieldPanelRect]) - 204.0 + index * 24.0,
        [self fieldPanelRect].origin.y + 4.0, 19.0, 13.0);
}

- (void)storeViewState
{
    if (!_plugin) return;
    _plugin->guiViewMode.store(_viewMode, std::memory_order_release);
    _plugin->guiViewAzDeg.store(static_cast<float>(_viewAzDeg), std::memory_order_release);
    _plugin->guiViewElDeg.store(static_cast<float>(_viewElDeg), std::memory_order_release);
    _plugin->guiViewZoom.store(static_cast<float>(_viewZoom), std::memory_order_release);
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 0.0; _viewElDeg = 0.0; }
    else if (mode == 1) { _viewAzDeg = 0.0; _viewElDeg = -90.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 30.0; }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (s3g::Vec3)entityWorld:(uint32_t)index
{
    if (!_plugin || index >= s3g::kAmbiHorizonMaxEntities) return {};
    const float az = _plugin->guiAzimuth[index].load(std::memory_order_relaxed);
    const float el = _plugin->guiElevation[index].load(std::memory_order_relaxed);
    const float range = _plugin->guiRange[index].load(std::memory_order_relaxed);
    const auto direction = s3g::directionFromAed(az, el);
    const float radius = 0.16f + range * 0.84f;
    return { direction.x * radius, direction.y * radius, direction.z * radius };
}

- (NSPoint)projectWorld:(s3g::Vec3)point depth:(CGFloat*)depth
{
    const NSRect field = [self fieldRect];
    const CGFloat scale = std::min(field.size.width, field.size.height)
        * 0.40 * std::clamp(_viewZoom, 0.55, 2.20);
    const float azimuth = static_cast<float>(_viewAzDeg * s3g::kPi / 180.0);
    const float elevation = static_cast<float>(_viewElDeg * s3g::kPi / 180.0);
    const float ca = std::cos(azimuth), sa = std::sin(azimuth);
    const float ce = std::cos(elevation), se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 - se * point.z;
    const float z2 = se * y1 + ce * point.z;
    if (depth) *depth = z2;
    return NSMakePoint(NSMidX(field) + x1 * scale,
        NSMidY(field) - y2 * scale);
}

- (NSPoint)projectGroundPointX:(double)x y:(double)y
{
    return [self projectWorld:{
        static_cast<float>(x), static_cast<float>(y), 0.0f
    } depth:nullptr];
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y,
        panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"ACOUSTIC HORIZON", true,
        panel.origin.x, panel.origin.y, panel.size.width, 21.0, attrs, style);
    // Camera mode 0 projects the horizontal XY plane; mode 1 rotates to the
    // XZ elevation plane. Keep the labels aligned with the shared encoder
    // family convention and with the actual AED projection.
    static NSString* viewNames[] = { @"TOP", @"SIDE", @"3/4" };
    for (int index = 0; index < 3; ++index) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:index], panel,
            viewNames[index], _viewMode == index, attrs, style);
    }
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], panel,
        @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], panel,
        @"+", false, attrs, style);

    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x4c4c4c) setStroke];
    NSFrameRect(field);

    // A sparse ground lattice gives the field a real world-space reference
    // without suggesting that the range display itself is a sounding object.
    // Every endpoint shares the entity camera transform, so the lattice turns,
    // tilts, and becomes edge-on in SIDE view instead of remaining a fixed
    // screen-space ellipse.
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1.0, 1.0)] addClip];
    constexpr std::array<double, 7> kGridOffsets {
        -0.75, -0.50, -0.25, 0.0, 0.25, 0.50, 0.75
    };
    for (double offset : kGridOffsets) {
        const double extent = std::sqrt(std::max(0.0, 1.0 - offset * offset));
        const bool axis = std::fabs(offset) < 0.001;
        [s3g::clap_gui::color(axis ? 0x484848 : 0x292929) setStroke];

        NSBezierPath* northSouth = [NSBezierPath bezierPath];
        [northSouth moveToPoint:[self projectGroundPointX:offset y:-extent]];
        [northSouth lineToPoint:[self projectGroundPointX:offset y:extent]];
        [northSouth setLineWidth:axis ? 0.75 : 0.45];
        [northSouth stroke];

        NSBezierPath* eastWest = [NSBezierPath bezierPath];
        [eastWest moveToPoint:[self projectGroundPointX:-extent y:offset]];
        [eastWest lineToPoint:[self projectGroundPointX:extent y:offset]];
        [eastWest setLineWidth:axis ? 0.75 : 0.45];
        [eastWest stroke];
    }

    const uint32_t count = std::min<uint32_t>(
        _plugin->guiEntityCount.load(std::memory_order_relaxed),
        s3g::kAmbiHorizonMaxEntities);
    for (uint32_t index = 0u; index < count; ++index) {
        CGFloat depth = 0.0;
        const NSPoint point = [self projectWorld:[self entityWorld:index] depth:&depth];
        const float energy = _plugin->guiEnergy[index].load(std::memory_order_relaxed);
        const uint32_t layer = _plugin->guiLayer[index].load(std::memory_order_relaxed);
        const CGFloat size = std::clamp<CGFloat>(3.0 + energy * 90.0, 3.0, 10.0);
        const CGFloat shade = std::clamp<CGFloat>(0.34 + energy * 10.0 + depth * 0.06, 0.30, 0.86);
        [[NSColor colorWithCalibratedWhite:shade alpha:0.90] setFill];
        if (layer == static_cast<uint32_t>(s3g::AmbiHorizonLayer::HorizonSignal)) {
            NSBezierPath* diamond = [NSBezierPath bezierPath];
            [diamond moveToPoint:NSMakePoint(point.x, point.y - size)];
            [diamond lineToPoint:NSMakePoint(point.x + size, point.y)];
            [diamond lineToPoint:NSMakePoint(point.x, point.y + size)];
            [diamond lineToPoint:NSMakePoint(point.x - size, point.y)];
            [diamond closePath]; [diamond fill];
        } else if (layer == static_cast<uint32_t>(s3g::AmbiHorizonLayer::LocalFloor)) {
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                point.x - size, point.y - size, size * 2.0, size * 2.0)] fill];
        } else {
            NSRectFill(NSMakeRect(point.x - size, point.y - 1.5,
                size * 2.0, 3.0));
        }
    }
    [NSGraphicsContext restoreGraphicsState];
    [@"CIRCLE LOCAL FLOOR    BAR LANDSCAPE BODY    DIAMOND DISTANT SIGNAL"
        drawAtPoint:NSMakePoint(field.origin.x + 9.0, NSMaxY(field) - 19.0)
        withAttributes:valueAttrs];
    NSString* readout = [NSString stringWithFormat:
        @"%.2f KM    %u ENTITIES    %@ GROUND    ACN/SN3D",
        _snapshot.rangeKm, _snapshot.entities,
        [NSString stringWithUTF8String:kGroundNames[
            std::min<uint32_t>(static_cast<uint32_t>(_snapshot.ground), 4u)]]];
    [readout drawAtPoint:NSMakePoint(field.origin.x + 9.0, field.origin.y + 8.0)
        withAttributes:valueAttrs];
}

- (void)drawSlider:(NSString*)name param:(clap_id)param value:(double)value
    attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    GuiSliderSpec spec {};
    if (!guiSliderSpec(param, _snapshot.ecology, spec)) return;
    char display[64] {};
    paramsValueToText(nullptr, param, value, display, sizeof(display));
    const CGFloat panelX = spec.panel->frame.x;
    s3g::clap_gui::drawSlider(name, [NSString stringWithUTF8String:display],
        sliderNorm(spec, value), layout::rowY(*spec.panel, spec.row),
        attrs, valueAttrs, style, panelX + 16.0, panelX + 108.0,
        panelX + 196.0, 82.0);
}

- (void)drawMenu:(NSString*)name value:(NSString*)value
    panel:(const layout::Panel&)panel row:(uint32_t)row
    attrs:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(name, value, layout::rowY(panel, row),
        attrs, valueAttrs, style, panel.frame.x + 16.0,
        panel.frame.x + 108.0, 124.0);
}

- (void)drawPanels:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
    style:(const s3g::clap_gui::Style&)style
{
    for (const auto& panel : kFirstColumnPanels) s3g::clap_gui::drawPanelFrame(panel, style);
    for (const auto& panel : kSecondColumnPanels) s3g::clap_gui::drawPanelFrame(panel, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT", true, kOutputPanel, attrs, style);
    s3g::clap_gui::drawPanelHeader(@"ECOLOGY", true, kEcologyPanel, attrs, style);
    NSString* generatorTitle = [NSString stringWithFormat:@"%s GENERATORS",
        kEcologyNames[std::min<uint32_t>(
            static_cast<uint32_t>(_snapshot.ecology), 8u)]];
    s3g::clap_gui::drawPanelHeader(generatorTitle, true,
        kGeneratorsPanel, attrs, style);
    s3g::clap_gui::drawPanelHeader(@"ENVIRONMENTAL SCORE", true, kScorePanel, attrs, style);
    s3g::clap_gui::drawPanelHeader(@"IDENTITY", true, kIdentityPanel, attrs, style);
    s3g::clap_gui::drawPanelHeader(@"HORIZON", true, kHorizonPanel, attrs, style);
    s3g::clap_gui::drawPanelHeader(@"ATMOSPHERE", true, kAtmospherePanel, attrs, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD LISTENER", true, kListenerPanel, attrs, style);
    for (uint32_t lobe = 0u;
         lobe < s3g::kAmbiFieldListenerMaxLobes; ++lobe) {
        const float envelope = _plugin->guiListenerEnvelope[lobe].load(
            std::memory_order_relaxed);
        const CGFloat level = std::clamp<CGFloat>(
            envelope / (envelope + 0.012f), 0.0, 1.0);
        [[NSColor colorWithCalibratedWhite:0.28 + level * 0.62
            alpha:0.92] setFill];
        NSRectFill(NSMakeRect(kListenerPanel.frame.x
                + kListenerPanel.frame.width - 62.0
                + static_cast<CGFloat>(lobe) * 6.0,
            kListenerPanel.frame.y + 8.0, 3.0, 6.0));
    }

    [self drawSlider:@"OUT" param:kOutputParamId value:_snapshot.outputGainDb attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", _snapshot.order]
        panel:kOutputPanel row:1u attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"ECOLOGY" value:[NSString stringWithUTF8String:kEcologyNames[
        std::min<uint32_t>(static_cast<uint32_t>(_snapshot.ecology), 8u)]]
        panel:kEcologyPanel row:0u attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ENTITIES" param:kEntitiesParamId value:_snapshot.entities attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ACTIVITY" param:kActivityParamId value:_snapshot.activity attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"OCCUPANCY" param:kOccupancyParamId value:_snapshot.occupancy attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"PACE" param:kPaceParamId value:_snapshot.pace attrs:attrs valueAttrs:valueAttrs style:style];
    const auto& generatorControls = generatorPanelControls(_snapshot.ecology);
    for (const auto& control : generatorControls.rows) {
        if (control.id == CLAP_INVALID_ID) continue;
        [self drawSlider:[NSString stringWithUTF8String:control.label]
            param:control.id value:valueFromParams(_snapshot, control.id)
            attrs:attrs valueAttrs:valueAttrs style:style];
    }
    [self drawSlider:@"MEMORY" param:kMemoryParamId value:_snapshot.memory attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CASCADE" param:kCascadeParamId value:_snapshot.cascade attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SIGNALS" param:kSignalsParamId value:_snapshot.signals attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"LANDSCAPE" param:kBedParamId value:_snapshot.horizonBed attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"LOCAL FLOOR" param:kFloorParamId value:_snapshot.localFloor attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"SEED" param:kSeedParamId value:_snapshot.seed attrs:attrs valueAttrs:valueAttrs style:style];

    [self drawSlider:@"RANGE" param:kRangeParamId value:_snapshot.rangeKm attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AZIMUTH" param:kAzimuthParamId value:_snapshot.azimuthDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ELEVATION" param:kElevationParamId value:_snapshot.elevationDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"ARC" param:kArcParamId value:_snapshot.arcDeg attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"DETAIL" param:kDetailParamId value:_snapshot.detail attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"EDGE" param:kEdgeParamId value:_snapshot.edgeDb attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AIR LOSS" param:kAirParamId value:_snapshot.air attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AIR NOISE" param:kAirNoiseParamId value:_snapshot.airNoise attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"GROUND" value:[NSString stringWithUTF8String:kGroundNames[
        std::min<uint32_t>(static_cast<uint32_t>(_snapshot.ground), 4u)]]
        panel:kAtmospherePanel row:2u attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TERRAIN" param:kTerrainParamId value:_snapshot.terrain attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"CARRY" param:kCarryParamId value:_snapshot.carry attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"TURBULENCE" param:kTurbulenceParamId value:_snapshot.turbulence attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"LISTEN" value:[NSString stringWithUTF8String:
        kFieldListenNames[std::min<uint32_t>(static_cast<uint32_t>(
            _snapshot.fieldListenMode), 3u)]]
        panel:kListenerPanel row:0u attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawSlider:@"AMOUNT" param:kFieldListenAmountParamId
        value:_snapshot.fieldListenAmount attrs:attrs valueAttrs:valueAttrs style:style];
    [self drawMenu:@"RESPONSE" value:[NSString stringWithUTF8String:
        kFieldListenResponseNames[std::min<uint32_t>(static_cast<uint32_t>(
            _snapshot.fieldListenResponse), 3u)]]
        panel:kListenerPanel row:2u attrs:attrs valueAttrs:valueAttrs style:style];
}

- (NSString*)presetName
{
    return [NSString stringWithUTF8String:s3g::kAmbiHorizonPresetInfo[
        std::min<uint32_t>(_presetSnapshot,
            s3g::kAmbiHorizonFactoryPresetCount - 1u)].name];
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _hoverMenuItem = -1;
    NSRect anchor = NSZeroRect;
    if (menu == 1) {
        anchor = [self presetMenuRect];
        _menuItemCount = s3g::kAmbiHorizonFactoryPresetCount;
    } else if (menu == 2) {
        anchor = s3g::clap_gui::cocoaRect(layout::menuBoxRect(kOutputPanel, 1u));
        anchor.origin.x = kOutputPanel.frame.x + 108.0; anchor.size.width = 124.0;
        _menuItemCount = 7u;
    } else if (menu == 3) {
        anchor = s3g::clap_gui::cocoaRect(layout::menuBoxRect(kEcologyPanel, 0u));
        anchor.origin.x = kEcologyPanel.frame.x + 108.0; anchor.size.width = 124.0;
        _menuItemCount = static_cast<uint32_t>(std::size(kEcologyNames));
    } else if (menu == 4) {
        anchor = s3g::clap_gui::cocoaRect(layout::menuBoxRect(kAtmospherePanel, 2u));
        anchor.origin.x = kAtmospherePanel.frame.x + 108.0; anchor.size.width = 124.0;
        _menuItemCount = 5u;
    } else if (menu == 5) {
        anchor = s3g::clap_gui::cocoaRect(layout::menuBoxRect(kListenerPanel, 0u));
        anchor.origin.x = kListenerPanel.frame.x + 108.0; anchor.size.width = 124.0;
        _menuItemCount = 4u;
    } else {
        anchor = s3g::clap_gui::cocoaRect(layout::menuBoxRect(kListenerPanel, 2u));
        anchor.origin.x = kListenerPanel.frame.x + 108.0; anchor.size.width = 124.0;
        _menuItemCount = 4u;
    }
    _openMenuRect = NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        menu == 1 ? 230.0 : anchor.size.width,
        21.0 * _menuItemCount);
    [self setNeedsDisplay:YES];
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == 0) return;
    static NSString* presetItems[s3g::kAmbiHorizonFactoryPresetCount] {};
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* ecologyItems[] = { @"MIXED", @"RURAL", @"TRAFFIC", @"CITY", @"INDUSTRIAL", @"WATER", @"WEATHER", @"AIRPORT", @"COAST" };
    static NSString* groundItems[] = { @"WATER", @"HARD", @"MIXED", @"GRASS", @"FOREST" };
    static NSString* listenItems[] = { @"OFF", @"FOLLOW", @"COUNTER", @"BALANCE" };
    static NSString* responseItems[] = { @"REACH", @"GLIMPSE", @"SETTLE", @"DISTANCE" };
    for (uint32_t index = 0u; index < s3g::kAmbiHorizonFactoryPresetCount; ++index) {
        // These labels outlive the autorelease pool for any individual Cocoa
        // event. REAPER drains that pool between menu-open and redraw events,
        // so static pointers must own their NSStrings explicitly.
        if (!presetItems[index]) presetItems[index] = [[NSString alloc]
            initWithUTF8String:s3g::kAmbiHorizonPresetInfo[index].name];
    }
    NSString* const* items = presetItems;
    int selected = static_cast<int>(_presetSnapshot);
    if (_openMenu == 2) { items = orderItems; selected = static_cast<int>(_snapshot.order) - 1; }
    else if (_openMenu == 3) { items = ecologyItems; selected = static_cast<int>(_snapshot.ecology); }
    else if (_openMenu == 4) { items = groundItems; selected = static_cast<int>(_snapshot.ground); }
    else if (_openMenu == 5) { items = listenItems; selected = static_cast<int>(_snapshot.fieldListenMode); }
    else if (_openMenu == 6) { items = responseItems; selected = static_cast<int>(_snapshot.fieldListenResponse); }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items,
        _menuItemCount, selected, _hoverMenuItem, attrs, style);
}

- (void)saveCustomPreset
{
    PresetFile preset {};
    preset.params = _snapshot;
    std::snprintf(preset.name, sizeof(preset.name), "%s", [[self presetName] UTF8String]);
    NSString* directory = [NSHomeDirectory() stringByAppendingPathComponent:
        @"Music/s3g/Presets/Ambi Horizon Encoder"];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
        withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3ghorizon" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3ghorizon", [self presetName]]];
    if ([panel runModal] == NSModalResponseOK) {
        (void)savePresetFile([[[panel URL] path] UTF8String], preset);
    }
}

- (void)loadCustomPreset
{
    NSString* directory = [NSHomeDirectory() stringByAppendingPathComponent:
        @"Music/s3g/Presets/Ambi Horizon Encoder"];
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3ghorizon" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    if ([panel runModal] != NSModalResponseOK) return;
    PresetFile preset {};
    if (!loadPresetFile([[[panel URL] path] UTF8String], preset)) { NSBeep(); return; }
    preset.params.outputGainDb = _snapshot.outputGainDb;
    preset.params.order = _snapshot.order;
    queueParams(*_plugin, preset.params, _presetSnapshot);
    _snapshot = preset.params;
    [self setNeedsDisplay:YES];
}

- (void)randomize
{
    uint32_t rng = _snapshot.seed ^ 0x68bc21ebu;
    auto next = [&rng]() {
        rng ^= rng << 13u; rng ^= rng >> 17u; rng ^= rng << 5u;
        return static_cast<float>(rng & 0x00ffffffu) / 16777216.0f;
    };
    const uint32_t preset = static_cast<uint32_t>(next()
        * s3g::kAmbiHorizonFactoryPresetCount)
        % s3g::kAmbiHorizonFactoryPresetCount;
    auto params = s3g::ambiHorizonFactoryPreset(preset);
    params.order = _snapshot.order;
    params.outputGainDb = _snapshot.outputGainDb;
    params.fieldListenMode = _snapshot.fieldListenMode;
    params.fieldListenAmount = _snapshot.fieldListenAmount;
    params.fieldListenResponse = _snapshot.fieldListenResponse;
    params.azimuthDeg = next() * 360.0f - 180.0f;
    params.arcDeg = std::clamp(params.arcDeg * (0.72f + next() * 0.56f), 0.0f, 360.0f);
    params.rangeKm = std::clamp(params.rangeKm * (0.68f + next() * 0.72f), 0.03f, 20.0f);
    params.seed = 1u + static_cast<uint32_t>(next() * 65534.0f);
    queueParams(*_plugin, params, preset);
    _snapshot = params;
    _presetSnapshot = preset;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    [self refreshSnapshot];
    const auto style = s3g::clap_gui::softTextStyle();
    [style.bg setFill]; NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* valueAttrs = s3g::clap_gui::softValueAttrs();
    s3g::clap_gui::drawEncoderTitleBand(@"s3g AMBI ENCODER HORIZON",
        [self presetName], s3g::clap_gui::peakDbText(
            _plugin->outputPeak.load(std::memory_order_relaxed)),
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight),
        s3g::clap_gui::softTitleAttrs(), attrs, valueAttrs, style);
    [self drawField:attrs valueAttrs:valueAttrs style:style];
    [self drawPanels:attrs valueAttrs:valueAttrs style:style];
    [self drawOpenMenu:valueAttrs style:style];
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)point
{
    GuiSliderSpec spec {};
    if (!guiSliderSpec(param, _snapshot.ecology, spec)) return;
    const double value = sliderValue(spec, point);
    assignParam(_snapshot, param, value);
    (void)queueGuiValue(*_plugin, param, value);
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    [self refreshSnapshot];
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu != 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point,
            _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) {
                const uint32_t order = _snapshot.order;
                const float output = _snapshot.outputGainDb;
                const auto listenMode = _snapshot.fieldListenMode;
                const float listenAmount = _snapshot.fieldListenAmount;
                const auto listenResponse = _snapshot.fieldListenResponse;
                _snapshot = s3g::ambiHorizonFactoryPreset(static_cast<uint32_t>(hit));
                _snapshot.order = order; _snapshot.outputGainDb = output;
                _snapshot.fieldListenMode = listenMode;
                _snapshot.fieldListenAmount = listenAmount;
                _snapshot.fieldListenResponse = listenResponse;
                _presetSnapshot = static_cast<uint32_t>(hit);
                (void)queueGuiValue(*_plugin, kPresetParamId, hit);
            } else if (_openMenu == 2) {
                _snapshot.order = static_cast<uint32_t>(hit + 1);
                (void)queueGuiValue(*_plugin, kOrderParamId, hit + 1);
            } else if (_openMenu == 3) {
                _snapshot.ecology = static_cast<s3g::AmbiHorizonEcology>(hit);
                (void)queueGuiValue(*_plugin, kEcologyParamId, hit);
            } else if (_openMenu == 4) {
                _snapshot.ground = static_cast<s3g::AmbiHorizonGround>(hit);
                (void)queueGuiValue(*_plugin, kGroundParamId, hit);
            } else if (_openMenu == 5) {
                _snapshot.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(hit);
                (void)queueGuiValue(*_plugin, kFieldListenModeParamId, hit);
            } else {
                _snapshot.fieldListenResponse =
                    static_cast<s3g::AmbiFieldListenerResponse>(hit);
                (void)queueGuiValue(*_plugin, kFieldListenResponseParamId, hit);
            }
        }
        _openMenu = 0; _hoverMenuItem = -1;
        [self setNeedsDisplay:YES]; return;
    }
    if (NSPointInRect(point, [self presetMenuRect])) { [self openMenu:1]; return; }
    if (NSPointInRect(point, [self loadPresetButtonRect])) { [self loadCustomPreset]; return; }
    if (NSPointInRect(point, [self savePresetButtonRect])) { [self saveCustomPreset]; return; }
    if (NSPointInRect(point, [self randomizeButtonRect])) { [self randomize]; return; }

    NSRect orderRect = NSMakeRect(kOutputPanel.frame.x + 108.0,
        layout::rowY(kOutputPanel, 1u) - 1.0, 124.0, 15.0);
    NSRect ecologyRect = NSMakeRect(kEcologyPanel.frame.x + 108.0,
        layout::rowY(kEcologyPanel, 0u) - 1.0, 124.0, 15.0);
    NSRect groundRect = NSMakeRect(kAtmospherePanel.frame.x + 108.0,
        layout::rowY(kAtmospherePanel, 2u) - 1.0, 124.0, 15.0);
    NSRect listenRect = NSMakeRect(kListenerPanel.frame.x + 108.0,
        layout::rowY(kListenerPanel, 0u) - 1.0, 124.0, 15.0);
    NSRect responseRect = NSMakeRect(kListenerPanel.frame.x + 108.0,
        layout::rowY(kListenerPanel, 2u) - 1.0, 124.0, 15.0);
    if (NSPointInRect(point, orderRect)) { [self openMenu:2]; return; }
    if (NSPointInRect(point, ecologyRect)) { [self openMenu:3]; return; }
    if (NSPointInRect(point, groundRect)) { [self openMenu:4]; return; }
    if (NSPointInRect(point, listenRect)) { [self openMenu:5]; return; }
    if (NSPointInRect(point, responseRect)) { [self openMenu:6]; return; }

    const NSRect fieldPanel = [self fieldPanelRect];
    if (NSPointInRect(point, fieldPanel)) {
        for (int index = 0; index < 2; ++index) {
            if (NSPointInRect(point, [self zoomButtonRect:index])) {
                _viewZoom = std::clamp(_viewZoom + (index == 0 ? -0.15 : 0.15), 0.55, 2.20);
                [self storeViewState]; [self setNeedsDisplay:YES]; return;
            }
        }
        for (int index = 0; index < 3; ++index) {
            if (NSPointInRect(point, [self viewButtonRect:index])) {
                [self setViewPreset:index]; return;
            }
        }
        if (NSPointInRect(point, [self fieldRect])) {
            _dragView = YES; _lastDragPoint = point; _viewMode = -1;
            [self storeViewState]; return;
        }
    }
    _dragParam = 0;
    for (const auto& spec : kGuiSliders) {
        const NSRect hit = s3g::clap_gui::cocoaRect(layout::sliderHitRect(
            *spec.panel, spec.row));
        if (!NSPointInRect(point, hit)) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &_plugin->plugin, spec.id, &defaultValue)) {
            assignParam(_snapshot, spec.id, defaultValue);
            (void)queueGuiValue(*_plugin, spec.id, defaultValue);
            [self setNeedsDisplay:YES]; return;
        }
        _dragParam = static_cast<int>(spec.id);
        [self setParam:spec.id fromPoint:point]; return;
    }
    const auto& generatorControls = generatorPanelControls(_snapshot.ecology);
    for (uint32_t row = 0u; row < generatorControls.rows.size(); ++row) {
        const clap_id id = generatorControls.rows[row].id;
        if (id == CLAP_INVALID_ID) continue;
        const GuiSliderSpec spec { id, &kGeneratorsPanel, row };
        const NSRect hit = s3g::clap_gui::cocoaRect(
            layout::sliderHitRect(*spec.panel, spec.row));
        if (!NSPointInRect(point, hit)) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &_plugin->plugin, id, &defaultValue)) {
            assignParam(_snapshot, id, defaultValue);
            (void)queueGuiValue(*_plugin, id, defaultValue);
            [self setNeedsDisplay:YES]; return;
        }
        _dragParam = static_cast<int>(id);
        [self setParam:id fromPoint:point]; return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.35,
            -85.0, 85.0);
        _lastDragPoint = point; [self storeViewState]; [self setNeedsDisplay:YES];
    } else if (_dragParam != 0) {
        [self setParam:static_cast<clap_id>(_dragParam) fromPoint:point];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event; _dragParam = 0; _dragView = NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(point,
        _openMenuRect, 21.0, _menuItemCount);
    if (hover != _hoverMenuItem) { _hoverMenuItem = hover; [self setNeedsDisplay:YES]; }
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
    *api = CLAP_WINDOW_API_COCOA; *isFloating = false; return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiHorizonEncoderFutureView alloc] initWithPlugin:p];
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
    [static_cast<S3GAmbiHorizonEncoderFutureView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible.store(false, std::memory_order_release);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(self(plugin)->guiViewport,
        kGuiWidth, kGuiHeight, width, height);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}
bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(self(plugin)->guiViewport,
        kGuiWidth, kGuiHeight, width, height);
}
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport,
        width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0
        || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) return false;
    p->guiVisible.store(true, std::memory_order_release);
    [static_cast<S3GAmbiHorizonEncoderFutureView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_release);
    [static_cast<S3GAmbiHorizonEncoderFutureView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
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
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-horizon-encoder-64",
    "s3g Ambi Encoder Horizon",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "", "", "0.1.0",
    "Autonomous 1OA-7OA synthesis of faint rural, traffic, city, industrial, water, and weather horizons.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiHorizonFactoryPreset(0u);
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->params);
    p->params = p->engine.params();
    publishParams(*p);
    p->guiEntityCount.store(p->engine.activeEntities(),
        std::memory_order_relaxed);
    for (uint32_t index = 0u; index < p->engine.activeEntities(); ++index) {
        const auto point = p->engine.entityTelemetry(index);
        p->guiAzimuth[index].store(point.azimuthDeg, std::memory_order_relaxed);
        p->guiElevation[index].store(point.elevationDeg, std::memory_order_relaxed);
        p->guiRange[index].store(point.rangeNorm, std::memory_order_relaxed);
        p->guiEnergy[index].store(point.energy, std::memory_order_relaxed);
        p->guiLayer[index].store(static_cast<uint32_t>(point.layer),
            std::memory_order_relaxed);
    }
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(
    const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*,
    const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0
        ? create(host) : nullptr;
}
const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0
        ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
