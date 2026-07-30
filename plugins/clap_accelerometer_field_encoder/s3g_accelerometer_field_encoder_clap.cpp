#include "s3g_accelerometer_field_encoder.h"
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
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAccelerometerFieldMaxChannels;
constexpr uint32_t kInputChannels = 1u;
constexpr uint32_t kStateVersion = 12u;
constexpr uint32_t kFactoryPresetCount = s3g::kAccelerometerFieldPresetCount;
constexpr uint32_t kCustomPresetIndex = kFactoryPresetCount;
// Released v10 had thirteen factory presets and v11 had twenty. Their Custom
// sentinels now overlap appended factory presets, so preserve both boundaries
// explicitly while migrating state into v12's twenty-five-preset surface.
constexpr uint32_t kV10FactoryPresetCount = 13u;
constexpr uint32_t kV11FactoryPresetCount = 20u;
constexpr uint32_t kGuiWidth = 1160u;
constexpr uint32_t kGuiHeight = 760u;
constexpr const char* kPluginId =
    "org.s3g.s3g-dsp.accelerometer-field-encoder-16";
constexpr const char* kHostName =
    "s3g Ambi Encoder Modal 16";

enum ParamId : clap_id {
    kParamPreset = 1u,
    kParamSubstrate,
    kParamExcitation,
    kParamReadout,
    kParamEventRate,
    kParamActivity,
    kParamForce,
    kParamTexture,
    kParamAmbientDrive,
    kParamExternalDrive,
    kParamSize,
    kParamDamping,
    kParamIrregularity,
    kParamPropagationLoss,
    kParamContactDetail,
    kParamSourcePosition,
    kParamPickupPosition,
    kParamPickupAxis,
    kParamSensorMass,
    kParamMountStiffness,
    kParamConditionerHighpass,
    kParamSensorNoise,
    kParamAirRadiation,
    kParamContactRadiation,
    kParamSpatialExtent,
    kParamFieldAzimuth,
    kParamFieldElevation,
    kParamOrder,
    kParamOutputMode,
    kParamOutputGain,
    kParamArraySpread,
    kParamFieldListenMode,
    kParamFieldListenAmount,
    kParamFieldListenResponse,
    kParamCoupling,
    kParamEnergy,
    kParamBodyCount,
    kParamBody1AzimuthOffset,
    kParamBody1ElevationOffset,
    kParamBody1Distance,
    kParamBody2AzimuthOffset,
    kParamBody2ElevationOffset,
    kParamBody2Distance,
    kParamBody3AzimuthOffset,
    kParamBody3ElevationOffset,
    kParamBody3Distance,
    kParamBody4AzimuthOffset,
    kParamBody4ElevationOffset,
    kParamBody4Distance,
    kParamBody5AzimuthOffset,
    kParamBody5ElevationOffset,
    kParamBody5Distance,
    kParamBody6AzimuthOffset,
    kParamBody6ElevationOffset,
    kParamBody6Distance,
    kParamBody7AzimuthOffset,
    kParamBody7ElevationOffset,
    kParamBody7Distance,
    kParamBody8AzimuthOffset,
    kParamBody8ElevationOffset,
    kParamBody8Distance,
    kParamListenerPickupSet,
    kParamModalLift,
};

constexpr clap_id kBodyAedParamBase = kParamBody1AzimuthOffset;
constexpr uint32_t kBodyAedParamStride = 3u;
static_assert(kParamBody8Distance == 61u);
static_assert(kParamListenerPickupSet == 62u);
static_assert(kParamModalLift == 63u);

enum class DisplayKind : uint8_t {
    Menu,
    Percent,
    Hertz,
    Decibels,
    Degrees,
    Position,
    Distance,
};

struct ParamSpec {
    clap_id id = CLAP_INVALID_ID;
    const char* name = "";
    const char* module = "";
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    DisplayKind display = DisplayKind::Percent;
    bool logarithmic = false;
    bool automatable = true;
    bool hidden = false;
};

constexpr std::array<ParamSpec, 63u> kParamSpecs {{
    { kParamPreset, "Preset", "Preset", 0.0, static_cast<double>(kFactoryPresetCount), 0.0, DisplayKind::Menu, false, false },
    { kParamSubstrate, "Modal profile", "Modal Body", 0.0, 24.0, 10.0, DisplayKind::Menu },
    { kParamExcitation, "Legacy transient exciter", "Legacy", 0.0, 5.0, 0.0, DisplayKind::Menu, false, true, true },
    { kParamReadout, "Legacy sensor readout", "Advanced", 0.0, 2.0, 0.0, DisplayKind::Menu, false, true, true },
    { kParamEventRate, "Legacy event rate", "Legacy", 0.01, 80.0, 0.01, DisplayKind::Hertz, true, true, true },
    { kParamActivity, "Legacy activity", "Legacy", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamForce, "Legacy mallet force", "Legacy", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamTexture, "Legacy mallet texture", "Legacy", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamAmbientDrive, "Legacy ambient drive", "Legacy", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamExternalDrive, "Actuator input", "Listener / Actuator", 0.0, 1.0, 0.0, DisplayKind::Percent },
    { kParamSize, "Size", "Modal Body", 0.0, 1.0, 0.50, DisplayKind::Percent },
    { kParamDamping, "Damping", "Modal Body", 0.0, 1.0, 0.08, DisplayKind::Percent },
    { kParamIrregularity, "Irregularity", "Modal Body", 0.0, 1.0, 0.025, DisplayKind::Percent },
    { kParamPropagationLoss, "Legacy propagation loss", "Legacy", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamContactDetail, "Legacy contact detail", "Legacy", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamSourcePosition, "Actuator position", "Modal Body", 0.0, 1.0, 0.43, DisplayKind::Position },
    { kParamPickupPosition, "Tone center", "Body Character", 0.0, 1.0, 0.50, DisplayKind::Position },
    { kParamPickupAxis, "Modal angle", "Body Character", 0.0, 1.0, 0.34, DisplayKind::Percent },
    { kParamSensorMass, "Legacy sensor mass", "Advanced", 0.0, 1.0, 0.01, DisplayKind::Percent, false, true, true },
    { kParamMountStiffness, "Legacy mount stiffness", "Advanced", 0.0, 1.0, 0.90, DisplayKind::Percent, false, true, true },
    { kParamConditionerHighpass, "Legacy conditioner HPF", "Advanced", 0.25, 180.0, 0.50, DisplayKind::Hertz, true, true, true },
    { kParamSensorNoise, "Legacy sensor noise", "Advanced", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamAirRadiation, "Air radiation", "Radiation", 0.0, 1.0, 0.24, DisplayKind::Percent },
    { kParamContactRadiation, "Contact / radiation", "Projection", 0.0, 1.0, 0.52, DisplayKind::Percent },
    { kParamSpatialExtent, "Body spread", "Projection", 0.0, 1.0, 0.90, DisplayKind::Percent },
    { kParamFieldAzimuth, "Field azimuth", "Projection", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamFieldElevation, "Field elevation", "Projection", -90.0, 90.0, 0.0, DisplayKind::Degrees },
    { kParamOrder, "Ambisonic order", "Output", 1.0, 3.0, 3.0, DisplayKind::Menu },
    { kParamOutputMode, "Output format", "Output", 0.0, 2.0, 0.0, DisplayKind::Menu, false, false },
    { kParamOutputGain, "Output gain", "Output", -60.0, 12.0, -11.0, DisplayKind::Decibels },
    { kParamArraySpread, "Body variation", "Body Character", 0.0, 1.0, 0.94, DisplayKind::Percent },
    { kParamFieldListenMode, "Actuator routing", "Listener / Actuator", 0.0, 3.0, 3.0, DisplayKind::Menu },
    { kParamFieldListenAmount, "Actuator drive", "Listener / Actuator", 0.0, 1.0, 0.64, DisplayKind::Percent },
    { kParamFieldListenResponse, "Actuator behavior", "Listener / Actuator", 0.0, 2.0, 2.0, DisplayKind::Menu },
    { kParamCoupling, "Modal coupling", "Modal Body", 0.0, 1.0, 0.72, DisplayKind::Percent },
    { kParamEnergy, "Legacy nonlinear energy", "Legacy", 0.0, 1.0, 0.0, DisplayKind::Percent, false, true, true },
    { kParamBodyCount, "Body count", "Ensemble", 4.0, 8.0, 6.0, DisplayKind::Menu },
    { kParamBody1AzimuthOffset, "Body 1 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody1ElevationOffset, "Body 1 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody1Distance, "Body 1 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamBody2AzimuthOffset, "Body 2 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody2ElevationOffset, "Body 2 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody2Distance, "Body 2 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamBody3AzimuthOffset, "Body 3 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody3ElevationOffset, "Body 3 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody3Distance, "Body 3 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamBody4AzimuthOffset, "Body 4 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody4ElevationOffset, "Body 4 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody4Distance, "Body 4 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamBody5AzimuthOffset, "Body 5 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody5ElevationOffset, "Body 5 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody5Distance, "Body 5 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamBody6AzimuthOffset, "Body 6 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody6ElevationOffset, "Body 6 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody6Distance, "Body 6 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamBody7AzimuthOffset, "Body 7 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody7ElevationOffset, "Body 7 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody7Distance, "Body 7 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamBody8AzimuthOffset, "Body 8 azimuth offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody8ElevationOffset, "Body 8 elevation offset", "Body AED", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamBody8Distance, "Body 8 distance", "Body AED", 0.15, 2.0, 1.0, DisplayKind::Distance },
    { kParamListenerPickupSet, "Listener pickups", "Listener / Actuator", 0.0, 1.0, 1.0, DisplayKind::Menu },
    { kParamModalLift, "Modal lift", "Output", 0.0, 1.0, 0.65, DisplayKind::Percent },
}};

constexpr std::array<s3g::AccelerometerSubstrate, 16u> kPublicSubstrates {{
    s3g::AccelerometerSubstrate::DeepBronze,
    s3g::AccelerometerSubstrate::TieredBronze,
    s3g::AccelerometerSubstrate::BroadBronze,
    s3g::AccelerometerSubstrate::BrightBronze,
    s3g::AccelerometerSubstrate::CarbonLaminate,
    s3g::AccelerometerSubstrate::GlassPlate,
    s3g::AccelerometerSubstrate::SteelShell,
    s3g::AccelerometerSubstrate::AluminumPlate,
    s3g::AccelerometerSubstrate::PorcelainShell,
    s3g::AccelerometerSubstrate::PorousEarthenware,
    s3g::AccelerometerSubstrate::SprucePlate,
    s3g::AccelerometerSubstrate::TensionedSkin,
    s3g::AccelerometerSubstrate::LoadedMembrane,
    s3g::AccelerometerSubstrate::CoupledMembrane,
    s3g::AccelerometerSubstrate::CavityMembrane,
    s3g::AccelerometerSubstrate::LooseMembrane,
}};
constexpr std::array<const char*, kPublicSubstrates.size()> kSubstrateNames {{
    "DEEP BRONZE", "TIERED BRONZE", "BROAD BRONZE", "BRIGHT BRONZE",
    "CARBON LAM.", "GLASS PLATE", "STEEL SHELL", "ALUM. PLATE",
    "PORCELAIN", "EARTHENWARE", "SPRUCE PLATE",
    "TENSIONED SKIN", "LOADED MEM.", "COUPLED MEM.",
    "CAVITY MEM.", "LOOSE MEM.",
}};

bool isPublicSubstrate(s3g::AccelerometerSubstrate substrate)
{
    return std::find(kPublicSubstrates.begin(), kPublicSubstrates.end(),
        substrate) != kPublicSubstrates.end();
}
constexpr const char* kExcitationNames[] {
    "AMBIENT", "DOUBLE MALLET", "MALLET ROLL", "SCRAPE / RUB",
    "BOWED RIM", "STRIKE"
};
constexpr const char* kReadoutNames[] {
    "ACCELERATION", "VELOCITY", "DISPLACEMENT"
};
constexpr const char* kOrderNames[] { "1OA / 4CH", "2OA / 9CH", "3OA / 16CH" };
constexpr const char* kOutputModeNames[] { "ACN/SN3D", "8 BODY STEMS" };
constexpr const char* kBodyCountNames[] {
    "4 BODIES", "5 BODIES", "6 BODIES", "7 BODIES", "8 BODIES"
};
constexpr const char* kFieldListenModeNames[] {
    "OFF", "LOCAL", "CROSS", "DIFFUSE"
};
constexpr const char* kFieldListenResponseNames[] {
    "RESONATE", "BALANCE", "DRONE"
};
constexpr const char* kListenerPickupSetNames[] {
    "TETRA 4", "CUBE 8"
};

const ParamSpec* paramSpec(clap_id id)
{
    for (const auto& spec : kParamSpecs) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

uint32_t roundedIndex(double value, uint32_t count)
{
    if (count == 0u) return 0u;
    return std::min<uint32_t>(count - 1u,
        static_cast<uint32_t>(std::llround(std::max(0.0, value))));
}

enum class BodyAedParamKind : uint32_t {
    AzimuthOffset = 0u,
    ElevationOffset = 1u,
    Distance = 2u,
};

bool decodeBodyAedParam(
    clap_id id, uint32_t& body, BodyAedParamKind& kind)
{
    if (id < kBodyAedParamBase
        || id >= kBodyAedParamBase
            + s3g::kAccelerometerFieldMaxBodyCount * kBodyAedParamStride) {
        return false;
    }
    const uint32_t offset = id - kBodyAedParamBase;
    body = offset / kBodyAedParamStride;
    kind = static_cast<BodyAedParamKind>(offset % kBodyAedParamStride);
    return true;
}

clap_id bodyAedParamId(uint32_t body, BodyAedParamKind kind)
{
    body = std::min<uint32_t>(
        body, s3g::kAccelerometerFieldMaxBodyCount - 1u);
    return kBodyAedParamBase + body * kBodyAedParamStride
        + static_cast<uint32_t>(kind);
}

const char* menuName(clap_id id, uint32_t index)
{
    switch (id) {
    case kParamPreset:
        if (index < kFactoryPresetCount) {
            return s3g::accelerometerFieldFactoryPresetInfo(index).name;
        }
        return "Custom";
    case kParamSubstrate:
        return kSubstrateNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kSubstrateNames)) - 1u)];
    case kParamExcitation:
        return kExcitationNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kExcitationNames)) - 1u)];
    case kParamReadout:
        return kReadoutNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kReadoutNames)) - 1u)];
    case kParamOrder:
        return kOrderNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kOrderNames)) - 1u)];
    case kParamOutputMode:
        return kOutputModeNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kOutputModeNames)) - 1u)];
    case kParamBodyCount:
        return kBodyCountNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kBodyCountNames)) - 1u)];
    case kParamFieldListenMode:
        return kFieldListenModeNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kFieldListenModeNames)) - 1u)];
    case kParamFieldListenResponse:
        return kFieldListenResponseNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kFieldListenResponseNames)) - 1u)];
    case kParamListenerPickupSet:
        return kListenerPickupSetNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kListenerPickupSetNames)) - 1u)];
    default: return "";
    }
}

uint32_t menuCount(clap_id id);

uint32_t menuIndexForValue(clap_id id, double value)
{
    if (id == kParamOrder) {
        return roundedIndex(value - 1.0, menuCount(id));
    }
    if (id == kParamBodyCount) {
        return roundedIndex(value - 4.0, menuCount(id));
    }
    if (id == kParamOutputMode) {
        return roundedIndex(value, 3u) == 0u ? 0u : 1u;
    }
    if (id == kParamSubstrate) {
        const auto substrate = static_cast<s3g::AccelerometerSubstrate>(
            roundedIndex(value,
                static_cast<uint32_t>(s3g::AccelerometerSubstrate::Count)));
        const auto found = std::find(
            kPublicSubstrates.begin(), kPublicSubstrates.end(), substrate);
        if (found != kPublicSubstrates.end()) {
            return static_cast<uint32_t>(found - kPublicSubstrates.begin());
        }
        return 0u;
    }
    return roundedIndex(value, menuCount(id));
}

double menuValueForIndex(clap_id id, uint32_t index)
{
    if (id == kParamOrder) return static_cast<double>(index + 1u);
    if (id == kParamBodyCount) return static_cast<double>(index + 4u);
    if (id == kParamOutputMode) {
        return index == 0u ? 0.0 : static_cast<double>(
            static_cast<uint32_t>(s3g::AccelerometerFieldOutputMode::BodyStems));
    }
    if (id == kParamSubstrate) {
        return static_cast<double>(static_cast<uint32_t>(
            kPublicSubstrates[std::min<uint32_t>(
                index, kPublicSubstrates.size() - 1u)]));
    }
    return static_cast<double>(index);
}

uint32_t menuCount(clap_id id)
{
    switch (id) {
    case kParamPreset: return kFactoryPresetCount + 1u;
    case kParamSubstrate: return static_cast<uint32_t>(std::size(kSubstrateNames));
    case kParamExcitation: return static_cast<uint32_t>(std::size(kExcitationNames));
    case kParamReadout: return static_cast<uint32_t>(std::size(kReadoutNames));
    case kParamOrder: return static_cast<uint32_t>(std::size(kOrderNames));
    case kParamOutputMode: return static_cast<uint32_t>(std::size(kOutputModeNames));
    case kParamBodyCount: return static_cast<uint32_t>(std::size(kBodyCountNames));
    case kParamFieldListenMode: return static_cast<uint32_t>(std::size(kFieldListenModeNames));
    case kParamFieldListenResponse: return static_cast<uint32_t>(std::size(kFieldListenResponseNames));
    case kParamListenerPickupSet: return static_cast<uint32_t>(std::size(kListenerPickupSetNames));
    default: return 0u;
    }
}

constexpr uint32_t menuColumnCount(uint32_t itemCount)
{
    // Sixteen profile rows are the largest useful single-column menu in this
    // view. Split that surface as well as the larger preset menu so draw and
    // hit-test geometry remain compact inside the 760 px canvas.
    return itemCount >= 16u ? 2u : 1u;
}

struct SavedGuiState {
    int32_t viewMode = 2;
    float viewAzimuthDeg = -35.0f;
    float viewElevationDeg = 34.0f;
    float viewZoom = 1.0f;
    uint32_t selectedBody = 0u;
};

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t presetIndex = 0u;
    s3g::AccelerometerFieldParams params =
        s3g::accelerometerFieldFactoryPreset(0u);
    SavedGuiState gui {};
};

// Version 1 preceded the independent physical array-spread control. Keep its
// exact scalar layout so sessions made with the first installed build migrate
// with the former fully distributed attachment geometry.
struct AccelerometerFieldParamsV1 {
    s3g::AccelerometerSubstrate substrate;
    s3g::AccelerometerExcitation excitation;
    s3g::AccelerometerReadout readout;
    float eventRateHz;
    float activity;
    float force;
    float texture;
    float ambientDrive;
    float externalDrive;
    float size;
    float damping;
    float irregularity;
    float propagationLoss;
    float contactDetail;
    float sourcePosition;
    float pickupPosition;
    float pickupAxis;
    float sensorMass;
    float mountStiffness;
    float conditionerHighpassHz;
    float sensorNoise;
    float airRadiation;
    uint32_t ambisonicOrder;
    s3g::AccelerometerFieldOutputMode outputMode;
    float contactRadiation;
    float spatialExtent;
    float fieldAzimuthDeg;
    float fieldElevationDeg;
    float outputGainDb;
    uint32_t seed;
};

struct SavedStateHeader {
    uint32_t version = 0u;
    uint32_t presetIndex = 0u;
};

static_assert(offsetof(SavedState, params) == sizeof(SavedStateHeader));
static_assert(offsetof(SavedState, gui)
    == sizeof(SavedStateHeader) + sizeof(s3g::AccelerometerFieldParams));

SavedGuiState sanitizeSavedGuiState(
    SavedGuiState state, uint32_t bodyCount)
{
    const auto finiteOr = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    state.viewMode = std::clamp<int32_t>(state.viewMode, -1, 2);
    state.viewAzimuthDeg = std::clamp(
        finiteOr(state.viewAzimuthDeg, -35.0f), -720.0f, 720.0f);
    state.viewElevationDeg = std::clamp(
        finiteOr(state.viewElevationDeg, 34.0f), -85.0f, 85.0f);
    state.viewZoom = std::clamp(
        finiteOr(state.viewZoom, 1.0f), 0.55f, 2.20f);
    state.selectedBody = std::min<uint32_t>(
        state.selectedBody, std::max<uint32_t>(1u, bodyCount) - 1u);
    return state;
}

s3g::AccelerometerFieldParams migrateParams(
    const AccelerometerFieldParamsV1& old)
{
    s3g::AccelerometerFieldParams result;
    result.substrate = old.substrate;
    result.excitation = old.excitation;
    result.readout = old.readout;
    result.eventRateHz = old.eventRateHz;
    result.activity = old.activity;
    result.force = old.force;
    result.texture = old.texture;
    result.ambientDrive = old.ambientDrive;
    result.externalDrive = old.externalDrive;
    result.size = old.size;
    result.damping = old.damping;
    result.irregularity = old.irregularity;
    result.propagationLoss = old.propagationLoss;
    result.contactDetail = old.contactDetail;
    result.sourcePosition = old.sourcePosition;
    result.pickupPosition = old.pickupPosition;
    result.arraySpread = 1.0f;
    result.pickupAxis = old.pickupAxis;
    result.sensorMass = old.sensorMass;
    result.mountStiffness = old.mountStiffness;
    result.conditionerHighpassHz = old.conditionerHighpassHz;
    result.sensorNoise = old.sensorNoise;
    result.airRadiation = old.airRadiation;
    result.ambisonicOrder = old.ambisonicOrder;
    result.outputMode = old.outputMode;
    result.contactRadiation = old.contactRadiation;
    result.spatialExtent = old.spatialExtent;
    result.fieldAzimuthDeg = old.fieldAzimuthDeg;
    result.fieldElevationDeg = old.fieldElevationDeg;
    result.outputGainDb = old.outputGainDb;
    result.seed = old.seed;
    return s3g::sanitizeAccelerometerFieldParams(result);
}

void focusModalParams(s3g::AccelerometerFieldParams& params)
{
    if (!isPublicSubstrate(params.substrate)) {
        params.substrate = s3g::AccelerometerSubstrate::DeepBronze;
    }
    if (params.outputMode
        == s3g::AccelerometerFieldOutputMode::LegacySensorStems) {
        params.outputMode = s3g::AccelerometerFieldOutputMode::BodyStems;
    }
    if (params.fieldListenMode == s3g::AmbiFieldListenMode::Off
        && (params.activity > 1.0e-4f
            || params.ambientDrive > 1.0e-4f)) {
        params.fieldListenMode = s3g::AmbiFieldListenMode::Balance;
        params.fieldListenAmount = std::max(
            params.fieldListenAmount, 0.55f);
        params.fieldListenResponse =
            s3g::AmbiFieldListenerResponse::Imprint;
    }
    params = s3g::sanitizeAccelerometerFieldParams(params);
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    std::atomic<bool> active { false };

    // The renderer and its parameter copy are audio-thread-owned while the
    // plugin is active. Cocoa, state, and parameter-query code only touch the
    // control copy below and publish changes through the mailbox.
    s3g::AccelerometerFieldEncoder engine {};
    s3g::AccelerometerFieldParams audioParams =
        s3g::accelerometerFieldFactoryPreset(0u);
    uint32_t audioPresetIndex = 0u;

    std::atomic_flag controlLock = ATOMIC_FLAG_INIT;
    s3g::AccelerometerFieldParams params =
        s3g::accelerometerFieldFactoryPreset(0u);
    uint32_t presetIndex = 0u;

    // Latest-value control -> audio mailbox. Scalar edits carry a dirty mask
    // so a GUI move cannot replace unrelated host automation. Full snapshots
    // are reserved for presets, randomization, and state restore. Writers may
    // wait for one fixed-size copy; the audio thread only try-acquires once.
    std::atomic_flag paramsMailboxLock = ATOMIC_FLAG_INIT;
    s3g::AccelerometerFieldParams paramsMailbox =
        s3g::accelerometerFieldFactoryPreset(0u);
    uint32_t paramsMailboxPresetIndex = 0u;
    uint64_t paramsMailboxDirtyMask = 0u;
    bool paramsMailboxFullReplace = false;
    bool paramsMailboxResetEngine = false;
    std::atomic<uint64_t> paramsMailboxRevision { 0u };
    uint64_t audioMailboxRevision = 0u;
    std::array<uint64_t, 64u> controlParamEditRevision {};
    uint64_t controlPresetEditRevision = 0u;

    // Host automation is mirrored back without letting the audio thread touch
    // the control copy. Main-thread readers merge this report while holding
    // controlLock, preserving newer GUI edits by mailbox revision.
    std::atomic_flag audioReportLock = ATOMIC_FLAG_INIT;
    s3g::AccelerometerFieldParams audioReportParams =
        s3g::accelerometerFieldFactoryPreset(0u);
    uint32_t audioReportPresetIndex = 0u;
    uint64_t audioReportDirtyMask = 0u;
    bool audioReportFullReplace = false;
    bool audioReportPresetChanged = false;
    uint64_t audioReportMailboxRevision = 0u;
    std::atomic<uint64_t> audioReportRevision { 0u };
    uint64_t controlAudioReportRevision = 0u;
    uint64_t pendingAudioReportDirtyMask = 0u;
    bool pendingAudioReportFullReplace = false;
    bool pendingAudioReportPresetChanged = false;

    std::array<std::vector<float>, kOutputChannels> scratchOutputs {};
    std::vector<float> scratchInput {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> listenerActivity { 0.0f };
    std::atomic<float> actuatorActivity { 0.0f };
    std::atomic<uint32_t> listenerPickupCount { 8u };
    std::array<std::atomic<float>, s3g::kAmbiFieldListenerMaxLobes>
        listenerPickupEnergy {};
    std::array<std::atomic<float>, s3g::kAccelerometerFieldMaxBodyCount>
        actuatorBodyDrive {};
    std::array<std::atomic<float>, s3g::kAccelerometerFieldMaxBodyCount>
        bodyEnergy {};
    std::array<std::atomic<float>, s3g::kAccelerometerFieldMaxBodyCount>
        bodyDirectionX {};
    std::array<std::atomic<float>, s3g::kAccelerometerFieldMaxBodyCount>
        bodyDirectionY {};
    std::array<std::atomic<float>, s3g::kAccelerometerFieldMaxBodyCount>
        bodyDirectionZ {};
    std::array<std::atomic<float>, s3g::kAccelerometerFieldMaxBodyCount>
        bodyDistance {};
    SavedGuiState guiState {};
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    char presetName[64] { "Deep Field" };
#endif
};

struct MidiStrike {
    uint32_t time = 0u;
    int32_t key = 60;
    float velocity = 0.0f;
};

struct OutputStageChange {
    uint32_t time = 0u;
    clap_id id = CLAP_INVALID_ID;
    double value = 0.0;
};

struct ProcessEventBatch {
    static constexpr uint32_t kMaximumStrikes = 256u;
    static constexpr uint32_t kMaximumOutputStageChanges = 256u;
    std::array<MidiStrike, kMaximumStrikes> strikes {};
    std::array<OutputStageChange, kMaximumOutputStageChanges>
        outputStageChanges {};
    uint32_t strikeCount = 0u;
    uint32_t outputStageChangeCount = 0u;
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

bool writeExact(const clap_ostream_t* stream, const void* source, size_t size)
{
    if (!stream || !stream->write) return false;
    const auto* bytes = static_cast<const uint8_t*>(source);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t written = stream->write(
            stream, bytes + offset, size - offset);
        if (written <= 0) return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool readExact(const clap_istream_t* stream, void* destination, size_t size)
{
    if (!stream || !stream->read) return false;
    auto* bytes = static_cast<uint8_t*>(destination);
    size_t offset = 0u;
    while (offset < size) {
        const int64_t received = stream->read(
            stream, bytes + offset, size - offset);
        if (received <= 0) return false;
        offset += static_cast<size_t>(received);
    }
    return true;
}

double paramValueFromState(
    const s3g::AccelerometerFieldParams& p,
    uint32_t presetIndex, clap_id id)
{
    uint32_t body = 0u;
    BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
    if (decodeBodyAedParam(id, body, kind)) {
        if (kind == BodyAedParamKind::AzimuthOffset) {
            return p.bodyAzimuthOffsetDeg[body];
        }
        if (kind == BodyAedParamKind::ElevationOffset) {
            return p.bodyElevationOffsetDeg[body];
        }
        return p.bodyDistance[body];
    }
    switch (id) {
    case kParamPreset: return presetIndex;
    case kParamSubstrate: return static_cast<uint32_t>(p.substrate);
    case kParamExcitation: return static_cast<uint32_t>(p.excitation);
    case kParamReadout: return static_cast<uint32_t>(p.readout);
    case kParamEventRate: return p.eventRateHz;
    case kParamActivity: return p.activity;
    case kParamForce: return p.force;
    case kParamTexture: return p.texture;
    case kParamAmbientDrive: return p.ambientDrive;
    case kParamExternalDrive: return p.externalDrive;
    case kParamSize: return p.size;
    case kParamDamping: return p.damping;
    case kParamIrregularity: return p.irregularity;
    case kParamPropagationLoss: return p.propagationLoss;
    case kParamContactDetail: return p.contactDetail;
    case kParamSourcePosition: return p.sourcePosition;
    case kParamPickupPosition: return p.pickupPosition;
    case kParamArraySpread: return p.arraySpread;
    case kParamPickupAxis: return p.pickupAxis;
    case kParamSensorMass: return p.sensorMass;
    case kParamMountStiffness: return p.mountStiffness;
    case kParamConditionerHighpass: return p.conditionerHighpassHz;
    case kParamSensorNoise: return p.sensorNoise;
    case kParamAirRadiation: return p.airRadiation;
    case kParamContactRadiation: return p.contactRadiation;
    case kParamSpatialExtent: return p.spatialExtent;
    case kParamFieldAzimuth: return p.fieldAzimuthDeg;
    case kParamFieldElevation: return p.fieldElevationDeg;
    case kParamOrder: return p.ambisonicOrder;
    case kParamOutputMode: return static_cast<uint32_t>(p.outputMode);
    case kParamOutputGain: return p.outputGainDb;
    case kParamFieldListenMode:
        return static_cast<uint32_t>(p.fieldListenMode);
    case kParamFieldListenAmount: return p.fieldListenAmount;
    case kParamFieldListenResponse:
        return std::clamp<uint32_t>(
            static_cast<uint32_t>(p.fieldListenResponse), 1u, 3u) - 1u;
    case kParamCoupling: return p.coupling;
    case kParamEnergy: return p.energy;
    case kParamBodyCount: return p.bodyCount;
    case kParamListenerPickupSet:
        return static_cast<uint32_t>(p.listenerPickupSet);
    case kParamModalLift: return p.modalLift;
    default: return 0.0;
    }
}

bool assignParam(
    s3g::AccelerometerFieldParams& p, clap_id id, double value)
{
    uint32_t body = 0u;
    BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
    if (decodeBodyAedParam(id, body, kind)) {
        if (kind == BodyAedParamKind::AzimuthOffset) {
            p.bodyAzimuthOffsetDeg[body] = static_cast<float>(value);
        } else if (kind == BodyAedParamKind::ElevationOffset) {
            p.bodyElevationOffsetDeg[body] = static_cast<float>(value);
        } else {
            p.bodyDistance[body] = static_cast<float>(value);
        }
    } else switch (id) {
    case kParamSubstrate:
        p.substrate = static_cast<s3g::AccelerometerSubstrate>(roundedIndex(
            value,
            static_cast<uint32_t>(s3g::AccelerometerSubstrate::Count)));
        if (!isPublicSubstrate(p.substrate)) {
            p.substrate = s3g::AccelerometerSubstrate::DeepBronze;
        }
        break;
    case kParamExcitation: p.excitation = static_cast<s3g::AccelerometerExcitation>(roundedIndex(value, 6u)); break;
    case kParamReadout: p.readout = static_cast<s3g::AccelerometerReadout>(roundedIndex(value, 3u)); break;
    case kParamEventRate: p.eventRateHz = static_cast<float>(value); break;
    case kParamActivity: p.activity = static_cast<float>(value); break;
    case kParamForce: p.force = static_cast<float>(value); break;
    case kParamTexture: p.texture = static_cast<float>(value); break;
    case kParamAmbientDrive: p.ambientDrive = static_cast<float>(value); break;
    case kParamExternalDrive: p.externalDrive = static_cast<float>(value); break;
    case kParamSize: p.size = static_cast<float>(value); break;
    case kParamDamping: p.damping = static_cast<float>(value); break;
    case kParamIrregularity: p.irregularity = static_cast<float>(value); break;
    case kParamPropagationLoss: p.propagationLoss = static_cast<float>(value); break;
    case kParamContactDetail: p.contactDetail = static_cast<float>(value); break;
    case kParamSourcePosition: p.sourcePosition = static_cast<float>(value); break;
    case kParamPickupPosition: p.pickupPosition = static_cast<float>(value); break;
    case kParamArraySpread: p.arraySpread = static_cast<float>(value); break;
    case kParamPickupAxis: p.pickupAxis = static_cast<float>(value); break;
    case kParamSensorMass: p.sensorMass = static_cast<float>(value); break;
    case kParamMountStiffness: p.mountStiffness = static_cast<float>(value); break;
    case kParamConditionerHighpass: p.conditionerHighpassHz = static_cast<float>(value); break;
    case kParamSensorNoise: p.sensorNoise = static_cast<float>(value); break;
    case kParamAirRadiation: p.airRadiation = static_cast<float>(value); break;
    case kParamContactRadiation: p.contactRadiation = static_cast<float>(value); break;
    case kParamSpatialExtent: p.spatialExtent = static_cast<float>(value); break;
    case kParamFieldAzimuth: p.fieldAzimuthDeg = static_cast<float>(value); break;
    case kParamFieldElevation: p.fieldElevationDeg = static_cast<float>(value); break;
    case kParamOrder: p.ambisonicOrder = roundedIndex(value - 1.0, 3u) + 1u; break;
    case kParamOutputMode:
        p.outputMode = roundedIndex(value, 3u) == 0u
            ? s3g::AccelerometerFieldOutputMode::Ambisonic
            : s3g::AccelerometerFieldOutputMode::BodyStems;
        break;
    case kParamOutputGain: p.outputGainDb = static_cast<float>(value); break;
    case kParamFieldListenMode: p.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(roundedIndex(value, 4u)); break;
    case kParamFieldListenAmount: p.fieldListenAmount = static_cast<float>(value); break;
    case kParamFieldListenResponse: p.fieldListenResponse = static_cast<s3g::AmbiFieldListenerResponse>(roundedIndex(value, 3u) + 1u); break;
    case kParamCoupling: p.coupling = static_cast<float>(value); break;
    case kParamEnergy: p.energy = static_cast<float>(value); break;
    case kParamBodyCount: p.bodyCount = roundedIndex(value - 4.0, 5u) + 4u; break;
    case kParamListenerPickupSet:
        p.listenerPickupSet =
            static_cast<s3g::AccelerometerFieldListenerPickupSet>(
                roundedIndex(value, 2u));
        break;
    case kParamModalLift: p.modalLift = static_cast<float>(value); break;
    default: return false;
    }
    p = s3g::sanitizeAccelerometerFieldParams(p);
    return true;
}

static_assert(std::atomic<uint64_t>::is_always_lock_free,
    "Modal parameter snapshot revisions must be lock-free");

class AtomicFlagGuard {
public:
    explicit AtomicFlagGuard(std::atomic_flag& flag)
        : flag_(flag)
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Only control/state/GUI threads use the waiting form.
        }
    }

    ~AtomicFlagGuard()
    {
        flag_.clear(std::memory_order_release);
    }

    AtomicFlagGuard(const AtomicFlagGuard&) = delete;
    AtomicFlagGuard& operator=(const AtomicFlagGuard&) = delete;

private:
    std::atomic_flag& flag_;
};

struct ControlStateSnapshot {
    s3g::AccelerometerFieldParams params {};
    uint32_t presetIndex = 0u;
    uint64_t mailboxRevision = 0u;
};

void updatePresetName(Plugin& plugin)
{
#if defined(__APPLE__)
    std::snprintf(plugin.presetName, sizeof(plugin.presetName), "%s",
        menuName(kParamPreset, plugin.presetIndex));
#else
    (void)plugin;
#endif
}

bool applyParamToState(
    s3g::AccelerometerFieldParams& params, uint32_t& presetIndex,
    clap_id id, double value)
{
    if (id == kParamPreset) {
        const uint32_t index = roundedIndex(
            value, kFactoryPresetCount + 1u);
        if (index >= kFactoryPresetCount) return false;
        const float outputGainDb = params.outputGainDb;
        params = s3g::accelerometerFieldFactoryPreset(index);
        params.outputGainDb = outputGainDb;
        presetIndex = index;
        return true;
    }
    if (!assignParam(params, id, value)) return false;
    presetIndex = kCustomPresetIndex;
    return true;
}

void publishControlParamsLocked(Plugin& plugin, bool resetEngine)
{
    AtomicFlagGuard mailboxGuard(plugin.paramsMailboxLock);
    plugin.paramsMailbox = plugin.params;
    plugin.paramsMailboxPresetIndex = plugin.presetIndex;
    plugin.paramsMailboxDirtyMask = 0u;
    plugin.paramsMailboxFullReplace = true;
    plugin.paramsMailboxResetEngine =
        plugin.paramsMailboxResetEngine || resetEngine;
    const uint64_t revision = plugin.paramsMailboxRevision.fetch_add(
        1u, std::memory_order_release) + 1u;
    plugin.controlParamEditRevision.fill(revision);
    plugin.controlPresetEditRevision = revision;
}

void publishControlParamLocked(Plugin& plugin, clap_id id)
{
    if (id >= 64u) return;
    AtomicFlagGuard mailboxGuard(plugin.paramsMailboxLock);
    plugin.paramsMailbox = plugin.params;
    plugin.paramsMailboxPresetIndex = plugin.presetIndex;
    plugin.paramsMailboxDirtyMask |= uint64_t { 1u } << id;
    const uint64_t revision = plugin.paramsMailboxRevision.fetch_add(
        1u, std::memory_order_release) + 1u;
    plugin.controlParamEditRevision[id] = revision;
    plugin.controlPresetEditRevision = revision;
}

void updateBodyGeometryMeters(Plugin& plugin)
{
    for (uint32_t body = 0u;
        body < s3g::kAccelerometerFieldMaxBodyCount; ++body) {
        const auto direction = plugin.engine.bodyDirection(body);
        plugin.bodyDirectionX[body].store(
            direction.x, std::memory_order_relaxed);
        plugin.bodyDirectionY[body].store(
            direction.y, std::memory_order_relaxed);
        plugin.bodyDirectionZ[body].store(
            direction.z, std::memory_order_relaxed);
        plugin.bodyDistance[body].store(
            plugin.engine.bodyDistance(body), std::memory_order_relaxed);
    }
}

void syncInactiveEngineLocked(Plugin& plugin, bool resetEngine)
{
    if (plugin.active.load(std::memory_order_acquire)) return;
    plugin.audioParams = plugin.params;
    plugin.audioPresetIndex = plugin.presetIndex;
    plugin.engine.setParams(plugin.audioParams);
    if (resetEngine) plugin.engine.reset();
    updateBodyGeometryMeters(plugin);
}

bool tryConsumeControlParams(Plugin& plugin)
{
    const uint64_t advertised = plugin.paramsMailboxRevision.load(
        std::memory_order_acquire);
    if (advertised == plugin.audioMailboxRevision) return false;
    if (plugin.paramsMailboxLock.test_and_set(
            std::memory_order_acquire)) {
        return false;
    }

    const uint64_t revision = plugin.paramsMailboxRevision.load(
        std::memory_order_relaxed);
    bool changed = revision != plugin.audioMailboxRevision;
    bool resetEngine = false;
    if (changed) {
        if (plugin.paramsMailboxFullReplace) {
            plugin.audioParams = plugin.paramsMailbox;
        } else {
            for (clap_id id = 2u; id <= kParamModalLift; ++id) {
                if ((plugin.paramsMailboxDirtyMask
                        & (uint64_t { 1u } << id)) == 0u) {
                    continue;
                }
                assignParam(plugin.audioParams, id,
                    paramValueFromState(plugin.paramsMailbox,
                        plugin.paramsMailboxPresetIndex, id));
            }
        }
        plugin.audioPresetIndex = plugin.paramsMailboxPresetIndex;
        resetEngine = plugin.paramsMailboxResetEngine;
        plugin.audioMailboxRevision = revision;
        plugin.paramsMailboxDirtyMask = 0u;
        plugin.paramsMailboxFullReplace = false;
        plugin.paramsMailboxResetEngine = false;
    }
    plugin.paramsMailboxLock.clear(std::memory_order_release);

    if (changed) {
        plugin.engine.setParams(plugin.audioParams);
        if (resetEngine) plugin.engine.reset();
    }
    return changed;
}

bool applyAudioParamToState(
    Plugin& plugin, clap_id id, double value, bool& resetEngine)
{
    const bool preset = id == kParamPreset;
    if (!applyParamToState(plugin.audioParams,
            plugin.audioPresetIndex, id, value)) {
        return false;
    }
    if (preset) {
        plugin.pendingAudioReportFullReplace = true;
        resetEngine = true;
    } else {
        plugin.pendingAudioReportDirtyMask |= uint64_t { 1u } << id;
    }
    plugin.pendingAudioReportPresetChanged = true;
    return true;
}

void applyAudioParam(Plugin& plugin, clap_id id, double value)
{
    bool resetEngine = false;
    if (!applyAudioParamToState(plugin, id, value, resetEngine)) return;
    if (id == kParamOutputGain || id == kParamModalLift) {
        plugin.engine.setOutputStageTargets(
            plugin.audioParams.outputGainDb,
            plugin.audioParams.modalLift);
        return;
    }
    plugin.engine.setParams(plugin.audioParams);
    if (resetEngine) plugin.engine.reset();
}

bool publishAudioReportTry(Plugin& plugin)
{
    if (plugin.pendingAudioReportDirtyMask == 0u
        && !plugin.pendingAudioReportFullReplace
        && !plugin.pendingAudioReportPresetChanged) {
        return true;
    }
    if (plugin.audioReportLock.test_and_set(
            std::memory_order_acquire)) {
        return false;
    }
    plugin.audioReportParams = plugin.audioParams;
    plugin.audioReportPresetIndex = plugin.audioPresetIndex;
    plugin.audioReportDirtyMask |= plugin.pendingAudioReportDirtyMask;
    plugin.audioReportFullReplace = plugin.audioReportFullReplace
        || plugin.pendingAudioReportFullReplace;
    plugin.audioReportPresetChanged = plugin.audioReportPresetChanged
        || plugin.pendingAudioReportPresetChanged;
    plugin.audioReportMailboxRevision = plugin.audioMailboxRevision;
    plugin.pendingAudioReportDirtyMask = 0u;
    plugin.pendingAudioReportFullReplace = false;
    plugin.pendingAudioReportPresetChanged = false;
    plugin.audioReportRevision.fetch_add(
        1u, std::memory_order_release);
    plugin.audioReportLock.clear(std::memory_order_release);
    return true;
}

void mergeAudioReportLocked(Plugin& plugin)
{
    const uint64_t advertised = plugin.audioReportRevision.load(
        std::memory_order_acquire);
    if (advertised == plugin.controlAudioReportRevision) return;

    AtomicFlagGuard reportGuard(plugin.audioReportLock);
    const uint64_t revision = plugin.audioReportRevision.load(
        std::memory_order_relaxed);
    if (revision == plugin.controlAudioReportRevision) return;

    const uint64_t dirtyMask = plugin.audioReportFullReplace
        ? std::numeric_limits<uint64_t>::max()
        : plugin.audioReportDirtyMask;
    for (clap_id id = 2u; id <= kParamModalLift; ++id) {
        if ((dirtyMask & (uint64_t { 1u } << id)) == 0u
            || plugin.controlParamEditRevision[id]
                > plugin.audioReportMailboxRevision) {
            continue;
        }
        assignParam(plugin.params, id,
            paramValueFromState(plugin.audioReportParams,
                plugin.audioReportPresetIndex, id));
    }
    if (plugin.audioReportPresetChanged
        && plugin.controlPresetEditRevision
            <= plugin.audioReportMailboxRevision) {
        plugin.presetIndex = plugin.audioReportPresetIndex;
    }
    plugin.guiState.selectedBody = std::min<uint32_t>(
        plugin.guiState.selectedBody, plugin.params.bodyCount - 1u);
    updatePresetName(plugin);
    plugin.controlAudioReportRevision = revision;
    plugin.audioReportDirtyMask = 0u;
    plugin.audioReportFullReplace = false;
    plugin.audioReportPresetChanged = false;
}

ControlStateSnapshot controlStateSnapshot(Plugin& plugin)
{
    AtomicFlagGuard controlGuard(plugin.controlLock);
    mergeAudioReportLocked(plugin);
    return {
        plugin.params,
        plugin.presetIndex,
        plugin.paramsMailboxRevision.load(std::memory_order_relaxed),
    };
}

double getParam(Plugin& plugin, clap_id id)
{
    const auto snapshot = controlStateSnapshot(plugin);
    return paramValueFromState(snapshot.params, snapshot.presetIndex, id);
}

void applyControlParam(Plugin& plugin, clap_id id, double value)
{
    AtomicFlagGuard controlGuard(plugin.controlLock);
    mergeAudioReportLocked(plugin);
    const bool preset = id == kParamPreset;
    if (!applyParamToState(
            plugin.params, plugin.presetIndex, id, value)) {
        return;
    }
    plugin.guiState.selectedBody = std::min<uint32_t>(
        plugin.guiState.selectedBody, plugin.params.bodyCount - 1u);
    updatePresetName(plugin);
    if (preset) {
        publishControlParamsLocked(plugin, true);
    } else {
        publishControlParamLocked(plugin, id);
    }
    syncInactiveEngineLocked(plugin, preset);
}

void replaceControlState(Plugin& plugin,
    const s3g::AccelerometerFieldParams& params,
    uint32_t presetIndex, bool resetEngine)
{
    AtomicFlagGuard controlGuard(plugin.controlLock);
    mergeAudioReportLocked(plugin);
    plugin.params = s3g::sanitizeAccelerometerFieldParams(params);
    plugin.presetIndex = std::min<uint32_t>(
        presetIndex, kCustomPresetIndex);
    plugin.guiState.selectedBody = std::min<uint32_t>(
        plugin.guiState.selectedBody, plugin.params.bodyCount - 1u);
    updatePresetName(plugin);
    publishControlParamsLocked(plugin, resetEngine);
    syncInactiveEngineLocked(plugin, resetEngine);
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    // Hosts may create and show the editor before audio activation. Prepare a
    // default-rate model here so its editable body field is already truthful.
    p->engine.prepare(p->sampleRate);
    syncInactiveEngineLocked(*p, true);
    return true;
}

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    auto* p = self(plugin);
    if (p->guiView) {
        s3g::clap_gui::destroyResponsiveViewport(
            p->guiViewport, p->guiView);
    }
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    // Claim engine ownership before any activation work. A concurrent Cocoa
    // edit now remains control-only and is either captured by the snapshot or
    // left pending for the first process callback.
    p->active.store(true, std::memory_order_release);
    p->sampleRate = std::max(1000.0, sampleRate);
    p->maxFrames = std::max(1u, maxFrames);
    p->scratchInput.assign(p->maxFrames, 0.0f);
    for (auto& channel : p->scratchOutputs) {
        channel.assign(p->maxFrames, 0.0f);
    }
    const auto snapshot = controlStateSnapshot(*p);
    p->audioParams = snapshot.params;
    p->audioPresetIndex = snapshot.presetIndex;
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->audioParams);
    p->engine.reset();
    {
        AtomicFlagGuard mailboxGuard(p->paramsMailboxLock);
        const uint64_t currentRevision = p->paramsMailboxRevision.load(
            std::memory_order_relaxed);
        p->audioMailboxRevision = snapshot.mailboxRevision;
        if (currentRevision == snapshot.mailboxRevision) {
            p->paramsMailboxDirtyMask = 0u;
            p->paramsMailboxFullReplace = false;
            p->paramsMailboxResetEngine = false;
        }
    }
    updateBodyGeometryMeters(*p);
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    while (!publishAudioReportTry(*p)) {
        // Deactivation is non-real-time and no further process callback can
        // retry a report which briefly collided with a main-thread reader.
    }

    // Keep GUI edits mailbox-only until the final engine access is complete.
    // Holding controlLock makes this a coherent final snapshot; active flips
    // only after the engine and mailbox checkpoint are synchronized, so the
    // next GUI writer can safely use the inactive direct-update path.
    AtomicFlagGuard controlGuard(p->controlLock);
    mergeAudioReportLocked(*p);
    p->audioParams = p->params;
    p->audioPresetIndex = p->presetIndex;
    p->engine.setParams(p->audioParams);
    bool resetEngine = false;
    {
        AtomicFlagGuard mailboxGuard(p->paramsMailboxLock);
        resetEngine = p->paramsMailboxResetEngine;
        p->audioMailboxRevision = p->paramsMailboxRevision.load(
            std::memory_order_relaxed);
        p->paramsMailboxDirtyMask = 0u;
        p->paramsMailboxFullReplace = false;
        p->paramsMailboxResetEngine = false;
    }
    if (resetEngine) p->engine.reset();
    updateBodyGeometryMeters(*p);
    p->active.store(false, std::memory_order_release);
}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->listenerActivity.store(0.0f, std::memory_order_relaxed);
    p->actuatorActivity.store(0.0f, std::memory_order_relaxed);
    p->listenerPickupCount.store(
        p->engine.listenerPickupCount(), std::memory_order_relaxed);
    for (auto& energy : p->listenerPickupEnergy) {
        energy.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& drive : p->actuatorBodyDrive) {
        drive.store(0.0f, std::memory_order_relaxed);
    }
    for (auto& energy : p->bodyEnergy) {
        energy.store(0.0f, std::memory_order_relaxed);
    }
}

ProcessEventBatch readInputEvents(Plugin& plugin,
    const clap_input_events_t* events, uint32_t frames)
{
    ProcessEventBatch batch;
    if (!events) return batch;
    bool engineParamsChanged = false;
    bool resetEngine = false;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param =
                reinterpret_cast<const clap_event_param_value_t*>(event);
            if (frames > 0u
                && (param->param_id == kParamOutputGain
                    || param->param_id == kParamModalLift)
                && batch.outputStageChangeCount
                    < ProcessEventBatch::kMaximumOutputStageChanges) {
                auto& change = batch.outputStageChanges[
                    batch.outputStageChangeCount++];
                change.time = std::min<uint32_t>(
                    event->time, frames - 1u);
                change.id = param->param_id;
                change.value = param->value;
                continue;
            }
            if (param->param_id == kParamOutputGain
                || param->param_id == kParamModalLift) {
                applyAudioParam(plugin, param->param_id, param->value);
            } else {
                engineParamsChanged = applyAudioParamToState(
                    plugin, param->param_id, param->value,
                    resetEngine) || engineParamsChanged;
            }
            continue;
        }
        int32_t key = -1;
        float velocity = 0.0f;
        if (event->type == CLAP_EVENT_NOTE_ON) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
            key = note->key;
            velocity = static_cast<float>(note->velocity);
        } else if (event->type == CLAP_EVENT_MIDI) {
            const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
            const uint8_t status = midi->data[0] & 0xf0u;
            if (status == 0x90u && midi->data[2] != 0u) {
                key = midi->data[1] & 0x7fu;
                velocity = static_cast<float>(midi->data[2] & 0x7fu)
                    / 127.0f;
            }
        }
        if (key >= 0 && velocity > 0.0f
            && batch.strikeCount < ProcessEventBatch::kMaximumStrikes) {
            auto& strike = batch.strikes[batch.strikeCount++];
            strike.time = frames > 0u
                ? std::min<uint32_t>(event->time, frames - 1u) : 0u;
            strike.key = key;
            strike.velocity = std::clamp(velocity, 0.0f, 1.0f);
        }
    }
    if (engineParamsChanged) {
        // Host automation timestamps for modal-structure controls are block
        // quantized by this wrapper. Commit the final snapshot once so a
        // dense lane cannot rebuild all 96 modes for every event.
        plugin.engine.setParams(plugin.audioParams);
        if (resetEngine) plugin.engine.reset();
    }
    return batch;
}

void updatePeak(Plugin& plugin, float* const* outputs,
    uint32_t channels, uint32_t frames)
{
    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (!outputs[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            peak = std::max(peak, std::fabs(outputs[channel][frame]));
        }
    }
    plugin.outputPeak.store(std::max(
        plugin.outputPeak.load(std::memory_order_relaxed) * 0.90f, peak),
        std::memory_order_relaxed);
}

void updateEngineMeters(Plugin& plugin)
{
    plugin.listenerActivity.store(
        plugin.engine.fieldListenerActivity(), std::memory_order_relaxed);
    plugin.actuatorActivity.store(
        plugin.engine.actuatorActivity(), std::memory_order_relaxed);
    const uint32_t pickupCount = plugin.engine.listenerPickupCount();
    plugin.listenerPickupCount.store(pickupCount, std::memory_order_relaxed);
    for (uint32_t pickup = 0u;
        pickup < s3g::kAmbiFieldListenerMaxLobes; ++pickup) {
        plugin.listenerPickupEnergy[pickup].store(
            pickup < pickupCount
                ? plugin.engine.listenerPickupEnergy(pickup) : 0.0f,
            std::memory_order_relaxed);
    }
    updateBodyGeometryMeters(plugin);
    for (uint32_t body = 0u;
        body < s3g::kAccelerometerFieldMaxBodyCount; ++body) {
        plugin.actuatorBodyDrive[body].store(
            plugin.engine.actuatorBodyDrive(body),
            std::memory_order_relaxed);
        plugin.bodyEnergy[body].store(
            plugin.engine.bodyEnergy(body), std::memory_order_relaxed);
    }
}

clap_process_status processFloat(Plugin& plugin,
    const clap_process_t& process,
    const clap_audio_buffer_t* input,
    clap_audio_buffer_t& output,
    const ProcessEventBatch& events)
{
    if (!output.data32) return CLAP_PROCESS_ERROR;
    const float* excitation = input && input->data32
        && input->channel_count > 0u ? input->data32[0] : nullptr;
    const uint32_t outputChannels = std::min<uint32_t>(
        output.channel_count, kOutputChannels);
    uint32_t offset = 0u;
    uint32_t strikeIndex = 0u;
    uint32_t outputStageChangeIndex = 0u;
    while (offset < process.frames_count) {
        while (strikeIndex < events.strikeCount
            && events.strikes[strikeIndex].time <= offset) {
            const auto& strike = events.strikes[strikeIndex++];
            plugin.engine.strikeMidi(strike.key, strike.velocity);
        }
        while (outputStageChangeIndex < events.outputStageChangeCount
            && events.outputStageChanges[outputStageChangeIndex].time
                <= offset) {
            const auto& change = events.outputStageChanges[
                outputStageChangeIndex++];
            applyAudioParam(plugin, change.id, change.value);
        }
        uint32_t end = process.frames_count;
        if (strikeIndex < events.strikeCount) {
            end = std::min(end, events.strikes[strikeIndex].time);
        }
        if (outputStageChangeIndex < events.outputStageChangeCount) {
            end = std::min(end, events.outputStageChanges[
                outputStageChangeIndex].time);
        }
        if (end <= offset) continue;
        std::array<float*, kOutputChannels> pointers {};
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            pointers[channel] = output.data32[channel]
                ? output.data32[channel] + offset : nullptr;
        }
        plugin.engine.process(excitation ? excitation + offset : nullptr,
            pointers.data(), outputChannels, end - offset);
        offset = end;
    }
    if (plugin.guiVisible.load(std::memory_order_relaxed)) {
        updateEngineMeters(plugin);
    }
    s3g::clearAudioBufferFromChannel(
        output, kOutputChannels, process.frames_count);
    updatePeak(plugin, output.data32,
        std::min<uint32_t>(output.channel_count, kOutputChannels),
        process.frames_count);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status processDouble(Plugin& plugin,
    const clap_process_t& process,
    const clap_audio_buffer_t* input,
    clap_audio_buffer_t& output,
    const ProcessEventBatch& events)
{
    if (!output.data64 || plugin.maxFrames == 0u) {
        s3g::clearAudioBuffer(output, process.frames_count);
        return CLAP_PROCESS_ERROR;
    }
    const uint32_t outputChannels = std::min<uint32_t>(
        output.channel_count, kOutputChannels);
    uint32_t offset = 0u;
    uint32_t strikeIndex = 0u;
    uint32_t outputStageChangeIndex = 0u;
    float peak = 0.0f;
    while (offset < process.frames_count) {
        while (strikeIndex < events.strikeCount
            && events.strikes[strikeIndex].time <= offset) {
            const auto& strike = events.strikes[strikeIndex++];
            plugin.engine.strikeMidi(strike.key, strike.velocity);
        }
        while (outputStageChangeIndex < events.outputStageChangeCount
            && events.outputStageChanges[outputStageChangeIndex].time
                <= offset) {
            const auto& change = events.outputStageChanges[
                outputStageChangeIndex++];
            applyAudioParam(plugin, change.id, change.value);
        }
        uint32_t end = std::min<uint32_t>(
            process.frames_count, offset + plugin.maxFrames);
        if (strikeIndex < events.strikeCount) {
            end = std::min(end, events.strikes[strikeIndex].time);
        }
        if (outputStageChangeIndex < events.outputStageChangeCount) {
            end = std::min(end, events.outputStageChanges[
                outputStageChangeIndex].time);
        }
        if (end <= offset) continue;
        const uint32_t frames = end - offset;
        const float* excitation = nullptr;
        if (input && input->channel_count > 0u) {
            if (input->data64 && input->data64[0]) {
                for (uint32_t frame = 0u; frame < frames; ++frame) {
                    plugin.scratchInput[frame] = static_cast<float>(
                        input->data64[0][offset + frame]);
                }
                excitation = plugin.scratchInput.data();
            } else if (input->data32 && input->data32[0]) {
                excitation = input->data32[0] + offset;
            }
        }
        std::array<float*, kOutputChannels> pointers {};
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            pointers[channel] = plugin.scratchOutputs[channel].data();
        }
        plugin.engine.process(
            excitation, pointers.data(), outputChannels, frames);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (!output.data64[channel]) continue;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                const float value = plugin.scratchOutputs[channel][frame];
                output.data64[channel][offset + frame] = value;
                peak = std::max(peak, std::fabs(value));
            }
        }
        offset = end;
    }
    s3g::clearAudioBufferFromChannel(
        output, kOutputChannels, process.frames_count);
    if (plugin.guiVisible.load(std::memory_order_relaxed)) {
        updateEngineMeters(plugin);
    }
    plugin.outputPeak.store(std::max(
        plugin.outputPeak.load(std::memory_order_relaxed) * 0.90f, peak),
        std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* process)
{
    if (!process || process->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    auto* p = self(plugin);
    (void)tryConsumeControlParams(*p);
    const ProcessEventBatch events = readInputEvents(
        *p, process->in_events, process->frames_count);
    const clap_audio_buffer_t* input = process->audio_inputs_count > 0u
        ? &process->audio_inputs[0] : nullptr;
    auto& output = process->audio_outputs[0];
    clap_process_status status = CLAP_PROCESS_ERROR;
    if (output.data32) {
        status = processFloat(*p, *process, input, output, events);
    } else if (output.data64) {
        status = processDouble(*p, *process, input, output, events);
    }
    publishAudioReportTry(*p);
    return status;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "Modal Actuator In" : "3OA ACN/SN3D Modal Field",
        sizeof(info->name));
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_MONO : CLAP_PORT_SURROUND;
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

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    if (!info || !isInput || index != 0u) return false;
    info->id = 30u;
    info->supported_dialects =
        CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Modal Actuator MIDI In", sizeof(info->name));
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return static_cast<uint32_t>(kParamSpecs.size());
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamSpecs.size()) return false;
    const auto& spec = kParamSpecs[index];
    *info = {};
    info->id = spec.id;
    info->flags = spec.automatable ? CLAP_PARAM_IS_AUTOMATABLE : 0u;
    if (spec.hidden) info->flags |= CLAP_PARAM_IS_HIDDEN;
    if (spec.display == DisplayKind::Menu) {
        info->flags |= CLAP_PARAM_IS_STEPPED;
    }
    std::strncpy(info->name, spec.name, sizeof(info->name));
    std::strncpy(info->module, spec.module, sizeof(info->module));
    info->min_value = spec.minimum;
    info->max_value = spec.maximum;
    info->default_value = spec.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !paramSpec(id)) return false;
    *value = getParam(*self(plugin), id);
    return true;
}

bool formatParam(clap_id id, double value, char* text, uint32_t size)
{
    const auto* spec = paramSpec(id);
    if (!spec || !text || size == 0u) return false;
    switch (spec->display) {
    case DisplayKind::Menu:
        std::snprintf(text, size, "%s", menuName(
            id, menuIndexForValue(id, value)));
        return true;
    case DisplayKind::Percent:
        std::snprintf(text, size, "%.0f %%", value * 100.0);
        return true;
    case DisplayKind::Hertz:
        std::snprintf(text, size, value < 10.0 ? "%.2f Hz" : "%.1f Hz", value);
        return true;
    case DisplayKind::Decibels:
        std::snprintf(text, size, "%+.1f dB", value);
        return true;
    case DisplayKind::Degrees:
        std::snprintf(text, size, "%+.0f°", value);
        return true;
    case DisplayKind::Position:
        std::snprintf(text, size, "%.0f %%", value * 100.0);
        return true;
    case DisplayKind::Distance:
        std::snprintf(text, size, "%.2f", value);
        return true;
    }
    return false;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    return formatParam(id, value, display, size);
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    const auto* spec = paramSpec(id);
    if (!spec || !display || !value) return false;
    if (spec->display == DisplayKind::Menu) {
        const uint32_t count = menuCount(id);
        for (uint32_t index = 0u; index < count; ++index) {
            if (std::strcmp(display, menuName(id, index)) == 0) {
                *value = menuValueForIndex(id, index);
                return true;
            }
        }
        *value = std::atof(display);
        return true;
    }
    *value = std::atof(display);
    if ((spec->display == DisplayKind::Percent
            || spec->display == DisplayKind::Position)
        && std::strchr(display, '%')) {
        *value *= 0.01;
    }
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t*)
{
    auto* p = self(plugin);
    if (p->active.load(std::memory_order_acquire)) {
        (void)tryConsumeControlParams(*p);
        (void)readInputEvents(*p, input, 0u);
        publishAudioReportTry(*p);
        return;
    }
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) {
            continue;
        }
        const auto* param =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        applyControlParam(*p, param->param_id, param->value);
    }
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    auto* p = self(plugin);
    const auto snapshot = controlStateSnapshot(*p);
    const SavedState state {
        kStateVersion, snapshot.presetIndex, snapshot.params, p->guiState
    };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedStateHeader header {};
    if (!readExact(stream, &header, sizeof(header))) {
        return false;
    }
    auto* p = self(plugin);
    AtomicFlagGuard controlGuard(p->controlLock);
    mergeAudioReportLocked(*p);
    if (header.version == kStateVersion || header.version == 11u) {
        s3g::AccelerometerFieldParams loadedParams {};
        SavedGuiState loadedGui {};
        if (!readExact(stream, &loadedParams, sizeof(loadedParams))
            || !readExact(stream, &loadedGui, sizeof(loadedGui))) {
            return false;
        }
        p->params = s3g::sanitizeAccelerometerFieldParams(loadedParams);
        p->guiState = sanitizeSavedGuiState(
            loadedGui, p->params.bodyCount);
    } else if (header.version == 10u) {
        // Neither profile expansion changed the parameter or GUI aggregate.
        // Read released v10 exactly; its Custom identity is migrated below.
        s3g::AccelerometerFieldParams loadedParams {};
        SavedGuiState loadedGui {};
        if (!readExact(stream, &loadedParams, sizeof(loadedParams))
            || !readExact(stream, &loadedGui, sizeof(loadedGui))) {
            return false;
        }
        p->params = s3g::sanitizeAccelerometerFieldParams(loadedParams);
        p->guiState = sanitizeSavedGuiState(
            loadedGui, p->params.bodyCount);
    } else if (header.version == 9u) {
        // Modal Lift was appended in version 10. Existing sessions keep their
        // former level by entering the new adaptive stage fully bypassed.
        s3g::AccelerometerFieldParams params {};
        SavedGuiState loadedGui {};
        constexpr size_t legacyParamsSize = offsetof(
            s3g::AccelerometerFieldParams, modalLift);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &loadedGui, sizeof(loadedGui))) {
            return false;
        }
        params.modalLift = 0.0f;
        p->params = s3g::sanitizeAccelerometerFieldParams(params);
        p->guiState = sanitizeSavedGuiState(
            loadedGui, p->params.bodyCount);
    } else if (header.version == 8u) {
        // Listener pickup set was appended in version 9. Version 8 already
        // stored camera state directly after its shorter parameter block.
        s3g::AccelerometerFieldParams params {};
        SavedGuiState loadedGui {};
        constexpr size_t legacyParamsSize = offsetof(
            s3g::AccelerometerFieldParams, listenerPickupSet);
        if (!readExact(stream, &params, legacyParamsSize)
            || !readExact(stream, &loadedGui, sizeof(loadedGui))) {
            return false;
        }
        p->params = s3g::sanitizeAccelerometerFieldParams(params);
        p->guiState = sanitizeSavedGuiState(
            loadedGui, p->params.bodyCount);
    } else if (header.version == 7u) {
        // Per-body AED offsets and camera state were appended in version 8.
        s3g::AccelerometerFieldParams params {};
        constexpr size_t legacyParamsSize = offsetof(
            s3g::AccelerometerFieldParams, bodyAzimuthOffsetDeg);
        if (!readExact(stream, &params, legacyParamsSize)) return false;
        p->params = s3g::sanitizeAccelerometerFieldParams(params);
    } else if (header.version == 6u || header.version == 5u) {
        // Body count was appended in version 7. Older modal sessions enter
        // the new ensemble at its six-body default.
        s3g::AccelerometerFieldParams params {};
        constexpr size_t legacyParamsSize = offsetof(
            s3g::AccelerometerFieldParams, bodyCount);
        if (!readExact(stream, &params, legacyParamsSize)) return false;
        p->params = s3g::sanitizeAccelerometerFieldParams(params);
    } else if (header.version == 4u) {
        // Coupling and nonlinear energy were appended in version 5.
        s3g::AccelerometerFieldParams params {};
        constexpr size_t legacyParamsSize = offsetof(
            s3g::AccelerometerFieldParams, coupling);
        if (!readExact(stream, &params, legacyParamsSize)) return false;
        p->params = s3g::sanitizeAccelerometerFieldParams(params);
    } else if (header.version == 3u || header.version == 2u) {
        // Listener fields were appended in version 4. Reading the former
        // scalar prefix into default-initialized current params preserves old
        // sessions exactly and leaves listening Off.
        s3g::AccelerometerFieldParams params {};
        constexpr size_t legacyParamsSize = offsetof(
            s3g::AccelerometerFieldParams, fieldListenMode);
        if (!readExact(stream, &params, legacyParamsSize)) return false;
        p->params = s3g::sanitizeAccelerometerFieldParams(params);
    } else if (header.version == 1u) {
        AccelerometerFieldParamsV1 params {};
        if (!readExact(stream, &params, sizeof(params))) return false;
        p->params = migrateParams(params);
    } else {
        return false;
    }
    if (header.version < 10u) {
        p->params.modalLift = 0.0f;
    }
    if (header.version < 8u) {
        focusModalParams(p->params);
        p->presetIndex = kCustomPresetIndex;
    } else if (header.version <= 10u) {
        p->presetIndex = header.presetIndex < kV10FactoryPresetCount
            ? header.presetIndex : kCustomPresetIndex;
    } else if (header.version == 11u) {
        p->presetIndex = header.presetIndex < kV11FactoryPresetCount
            ? header.presetIndex : kCustomPresetIndex;
    } else {
        p->presetIndex = std::min<uint32_t>(
            header.presetIndex, kCustomPresetIndex);
    }
#if defined(__APPLE__)
    std::snprintf(p->presetName, sizeof(p->presetName), "%s",
        menuName(kParamPreset, p->presetIndex));
#endif
    publishControlParamsLocked(*p, true);
    syncInactiveEngineLocked(*p, true);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
namespace {

constexpr s3g::gui_layout::Canvas kGuiCanvas {
    static_cast<double>(kGuiWidth), static_cast<double>(kGuiHeight)
};
constexpr auto kTitleBand =
    s3g::gui_layout::encoderTitleBand(kGuiCanvas);

constexpr std::array<clap_id, 4u> kOutputControls {{
    kParamModalLift, kParamOutputGain, kParamOrder, kParamOutputMode,
}};
constexpr std::array<clap_id, 7u> kStructureControls {{
    kParamSubstrate, kParamBodyCount, kParamSize, kParamDamping, kParamIrregularity,
    kParamCoupling, kParamSourcePosition,
}};
constexpr std::array<clap_id, 3u> kProjectionControls {{
    kParamFieldAzimuth, kParamFieldElevation, kParamSpatialExtent,
}};
constexpr std::array<clap_id, 5u> kRadiationControls {{
    kParamPickupPosition, kParamArraySpread, kParamPickupAxis,
    kParamContactRadiation, kParamAirRadiation,
}};
constexpr std::array<clap_id, 5u> kActuatorControls {{
    kParamListenerPickupSet, kParamFieldListenMode,
    kParamFieldListenAmount, kParamFieldListenResponse,
    kParamExternalDrive,
}};
constexpr std::array<s3g::gui_layout::EncoderFamilyControl, 4u>
    kActuatorFamilyControls {{
        s3g::gui_layout::EncoderFamilyControl::PickupSet,
        s3g::gui_layout::EncoderFamilyControl::ListeningMode,
        s3g::gui_layout::EncoderFamilyControl::ListenerAmountReturn,
        s3g::gui_layout::EncoderFamilyControl::ListenerResponseBypass,
}};

std::array<clap_id, 3u> bodyPointControls(uint32_t body)
{
    return {{
        bodyAedParamId(body, BodyAedParamKind::AzimuthOffset),
        bodyAedParamId(body, BodyAedParamKind::ElevationOffset),
        bodyAedParamId(body, BodyAedParamKind::Distance),
    }};
}

constexpr s3g::gui_layout::Rect kFieldPanelFrame {
    18.0, 42.0, 596.0, 706.0
};
constexpr auto kOutputPanelLayout = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Output,
    s3g::gui_layout::kLargeEncoderFirstColumn,
    s3g::gui_layout::kStandardMetrics.contentTop,
    static_cast<uint32_t>(kOutputControls.size()));
constexpr auto kStructurePanelLayout = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::Engine,
    kOutputPanelLayout,
    static_cast<uint32_t>(kStructureControls.size()));
constexpr auto kActuatorPanelLayout = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::Listener,
    kStructurePanelLayout,
    static_cast<uint32_t>(kActuatorControls.size()));
constexpr auto kProjectionPanelLayout = s3g::gui_layout::fittedPanel(
    s3g::gui_layout::PluginClass::ProceduralEncoder,
    s3g::gui_layout::PanelRole::Projection,
    s3g::gui_layout::kLargeEncoderSecondColumn,
    s3g::gui_layout::kStandardMetrics.contentTop,
    static_cast<uint32_t>(kProjectionControls.size()));
constexpr auto kRadiationPanelLayout = s3g::gui_layout::fittedStackPanel(
    s3g::gui_layout::PanelRole::ToneShape,
    kProjectionPanelLayout,
    static_cast<uint32_t>(kRadiationControls.size()));
constexpr auto kBodyPointPanelLayout = s3g::gui_layout::stackPanel(
    s3g::gui_layout::PanelRole::SelectedObject,
    kRadiationPanelLayout,
    152.0,
    3u);
constexpr auto kSignalPanelLayout = s3g::gui_layout::stackPanel(
    s3g::gui_layout::PanelRole::Utility,
    kBodyPointPanelLayout,
    166.0,
    0u);

constexpr std::array<s3g::gui_layout::Panel, 3u> kFirstColumnLayouts {{
    kOutputPanelLayout, kStructurePanelLayout, kActuatorPanelLayout,
}};
constexpr std::array<s3g::gui_layout::Panel, 4u> kSecondColumnLayouts {{
    kProjectionPanelLayout, kRadiationPanelLayout,
    kBodyPointPanelLayout, kSignalPanelLayout,
}};
static_assert(s3g::gui_layout::validateColumn(
    kFirstColumnLayouts, kGuiCanvas));
static_assert(s3g::gui_layout::rolesFollowTemplate(
    kFirstColumnLayouts,
    s3g::gui_layout::kProceduralEncoderTemplate,
    true));
static_assert(s3g::gui_layout::controlsFollowFamilyOrder(
    kActuatorFamilyControls,
    s3g::gui_layout::kListenerControlOrder));
static_assert(s3g::gui_layout::controlMatchesSlot(
    kOutputPanelLayout,
    s3g::gui_layout::kLargeEncoderOrderSlot));
static_assert(s3g::gui_layout::validateColumn(
    kSecondColumnLayouts, kGuiCanvas, false));
static_assert(s3g::gui_layout::rectFitsCanvas(kFieldPanelFrame, kGuiCanvas));
static_assert(s3g::gui_layout::kLargeEncoderFirstColumn.x
    - (kFieldPanelFrame.x + kFieldPanelFrame.width) == 16.0);
static_assert(kFieldPanelFrame.y + kFieldPanelFrame.height
    + 12.0 == kGuiCanvas.height);

NSRect fieldPanelRect()
{
    return s3g::clap_gui::cocoaRect(kFieldPanelFrame);
}
NSRect fieldPlotRect()
{
    const NSRect panel = fieldPanelRect();
    return NSMakeRect(panel.origin.x + 16.0, panel.origin.y + 34.0,
        panel.size.width - 32.0, panel.size.height - 50.0);
}
NSRect outputPanelRect()
{
    return s3g::clap_gui::cocoaRect(kOutputPanelLayout.frame);
}
NSRect actuatorPanelRect()
{
    return s3g::clap_gui::cocoaRect(kActuatorPanelLayout.frame);
}
NSRect structurePanelRect()
{
    return s3g::clap_gui::cocoaRect(kStructurePanelLayout.frame);
}
NSRect projectionPanelRect()
{
    return s3g::clap_gui::cocoaRect(kProjectionPanelLayout.frame);
}
NSRect radiationPanelRect()
{
    return s3g::clap_gui::cocoaRect(kRadiationPanelLayout.frame);
}
NSRect bodyPointPanelRect()
{
    return s3g::clap_gui::cocoaRect(kBodyPointPanelLayout.frame);
}
NSRect signalPanelRect()
{
    return s3g::clap_gui::cocoaRect(kSignalPanelLayout.frame);
}
NSRect modalZoomButtonRect(uint32_t index)
{
    const NSRect panel = fieldPanelRect();
    const CGFloat cameraLeft = s3g::clap_gui::topologyProcessorCameraButtonRect(
        panel, 0u).origin.x;
    constexpr CGFloat width = 22.0;
    constexpr CGFloat gap = 6.0;
    constexpr CGFloat groupWidth = width * 2.0 + gap;
    return NSMakeRect(cameraLeft - gap - groupWidth
            + static_cast<CGFloat>(index) * (width + gap),
        panel.origin.y + 3.0, width, 15.0);
}
NSRect modalResetLayoutButtonRect()
{
    const NSRect zoomOut = modalZoomButtonRect(0u);
    return NSMakeRect(zoomOut.origin.x - 60.0,
        fieldPanelRect().origin.y + 3.0, 52.0, 15.0);
}

NSRect panelForParam(clap_id id)
{
    uint32_t body = 0u;
    BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
    if (decodeBodyAedParam(id, body, kind)) return bodyPointPanelRect();
    switch (id) {
    case kParamOrder:
    case kParamOutputMode:
    case kParamOutputGain:
    case kParamModalLift:
        return outputPanelRect();
    case kParamFieldAzimuth:
    case kParamFieldElevation:
    case kParamSpatialExtent:
        return projectionPanelRect();
    case kParamFieldListenMode:
    case kParamFieldListenAmount:
    case kParamFieldListenResponse:
    case kParamExternalDrive:
    case kParamListenerPickupSet:
        return actuatorPanelRect();
    case kParamSubstrate:
    case kParamBodyCount:
    case kParamSize:
    case kParamDamping:
    case kParamIrregularity:
    case kParamCoupling:
    case kParamSourcePosition:
        return structurePanelRect();
    case kParamPickupPosition:
    case kParamArraySpread:
    case kParamPickupAxis:
    case kParamContactRadiation:
    case kParamAirRadiation:
        return radiationPanelRect();
    default:
        return outputPanelRect();
    }
}

CGFloat controlRowY(NSRect panel, uint32_t row)
{
    return static_cast<CGFloat>(s3g::gui_layout::toolboxRowY(
        panel.origin.y, row));
}

const char* shortParamName(clap_id id)
{
    uint32_t body = 0u;
    BodyAedParamKind kind = BodyAedParamKind::AzimuthOffset;
    if (decodeBodyAedParam(id, body, kind)) {
        if (kind == BodyAedParamKind::AzimuthOffset) return "AZ OFFSET";
        if (kind == BodyAedParamKind::ElevationOffset) return "EL OFFSET";
        return "DISTANCE";
    }
    switch (id) {
    case kParamExcitation: return "EXCITER";
    case kParamEventRate: return "RATE";
    case kParamActivity: return "ACTIVE";
    case kParamForce: return "FORCE";
    case kParamTexture: return "TEXTURE";
    case kParamAmbientDrive: return "AMBIENT";
    case kParamExternalDrive: return "INPUT";
    case kParamSubstrate: return "BODY";
    case kParamBodyCount: return "BODIES";
    case kParamSize: return "SIZE";
    case kParamDamping: return "DAMP";
    case kParamIrregularity: return "IRREG";
    case kParamCoupling: return "COUPLING";
    case kParamEnergy: return "ENERGY";
    case kParamPropagationLoss: return "LOSS";
    case kParamContactDetail: return "DETAIL";
    case kParamSourcePosition: return "DRIVE POS";
    case kParamReadout: return "READOUT";
    case kParamPickupPosition: return "TONE CENTER";
    case kParamArraySpread: return "VARIATION";
    case kParamPickupAxis: return "MODE ANGLE";
    case kParamSensorMass: return "MASS";
    case kParamMountStiffness: return "MOUNT";
    case kParamConditionerHighpass: return "HPF";
    case kParamSensorNoise: return "NOISE";
    case kParamAirRadiation: return "AIR LEVEL";
    case kParamOutputMode: return "FORMAT";
    case kParamOrder: return "ORDER";
    case kParamContactRadiation: return "CONTACT / RAD";
    case kParamSpatialExtent: return "BODY SPREAD";
    case kParamFieldAzimuth: return "AZIMUTH";
    case kParamFieldElevation: return "ELEVATION";
    case kParamOutputGain: return "OUT";
    case kParamModalLift: return "LIFT";
    case kParamFieldListenMode: return "ROUTING";
    case kParamFieldListenAmount: return "DRIVE";
    case kParamFieldListenResponse: return "BEHAVIOR";
    case kParamListenerPickupSet: return "EARS";
    default: return "PARAM";
    }
}

double normalizedParam(const ParamSpec& spec, double value)
{
    value = std::clamp(value, spec.minimum, spec.maximum);
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
    if (spec.logarithmic && spec.minimum > 0.0) {
        return spec.minimum * std::pow(
            spec.maximum / spec.minimum, normalized);
    }
    return spec.minimum
        + (spec.maximum - spec.minimum) * normalized;
}

struct GuiControlLocation {
    clap_id id = CLAP_INVALID_ID;
    NSRect panel {};
    uint32_t row = 0u;
};

template <size_t Count>
bool findInGroup(NSPoint point, const s3g::gui_layout::Panel& layout,
    const std::array<clap_id, Count>& controls,
    GuiControlLocation& result)
{
    const NSRect panel = s3g::clap_gui::cocoaRect(layout.frame);
    for (uint32_t row = 0u; row < Count; ++row) {
        const NSRect hit = s3g::clap_gui::cocoaRect(
            s3g::gui_layout::sliderHitRect(layout, row));
        if (NSPointInRect(point, hit)) {
            result = { controls[row], panel, row };
            return true;
        }
    }
    return false;
}

NSRect menuAnchorRect(const GuiControlLocation& location)
{
    return NSMakeRect(
        s3g::gui_layout::processorControlX(location.panel.origin.x),
        controlRowY(location.panel, location.row) - 1.0,
        s3g::gui_layout::processorMenuWidth(location.panel.size.width),
        15.0);
}

float guiRandomSigned()
{
    return static_cast<float>(arc4random() & 0x00ffffffu)
        * (2.0f / 16777215.0f) - 1.0f;
}

void randomizeSafe(Plugin& plugin)
{
    const auto snapshot = controlStateSnapshot(plugin);
    const uint32_t order = snapshot.params.ambisonicOrder;
    const float outputGain = snapshot.params.outputGainDb;
    const float modalLift = snapshot.params.modalLift;
    const auto listenerPickupSet = snapshot.params.listenerPickupSet;
    const auto listenerMode = snapshot.params.fieldListenMode;
    const float listenerAmount = snapshot.params.fieldListenAmount;
    const auto listenerResponse = snapshot.params.fieldListenResponse;
    const float externalDrive = snapshot.params.externalDrive;
    auto params = s3g::accelerometerFieldFactoryPreset(
        arc4random_uniform(kFactoryPresetCount));
    const auto vary = [](float value, float amount) {
        return s3g::clamp(value + guiRandomSigned() * amount, 0.0f, 1.0f);
    };
    params.size = vary(params.size, 0.08f);
    params.damping = vary(params.damping, 0.08f);
    params.irregularity = vary(params.irregularity, 0.07f);
    params.coupling = vary(params.coupling, 0.12f);
    params.sourcePosition = vary(params.sourcePosition, 0.10f);
    params.pickupPosition = vary(params.pickupPosition, 0.10f);
    params.arraySpread = s3g::clamp(
        vary(params.arraySpread, 0.10f), 0.42f, 1.0f);
    params.spatialExtent = s3g::clamp(
        vary(params.spatialExtent, 0.10f), 0.36f, 1.0f);
    params.ambisonicOrder = order;
    params.outputGainDb = outputGain;
    params.modalLift = modalLift;
    params.outputMode = s3g::AccelerometerFieldOutputMode::Ambisonic;
    params.listenerPickupSet = listenerPickupSet;
    params.fieldListenMode = listenerMode;
    params.fieldListenAmount = listenerAmount;
    params.fieldListenResponse = listenerResponse;
    params.externalDrive = externalDrive;
    params.seed ^= arc4random();
    replaceControlState(plugin, params, kCustomPresetIndex, true);
}

struct GuiBodyAed {
    float azimuthDeg = 0.0f;
    float elevationDeg = 0.0f;
    float distance = 1.0f;
};

s3g::Vec3 guiBodyDirection(const Plugin& plugin, uint32_t body)
{
    return s3g::normalize({
        plugin.bodyDirectionX[body].load(std::memory_order_relaxed),
        plugin.bodyDirectionY[body].load(std::memory_order_relaxed),
        plugin.bodyDirectionZ[body].load(std::memory_order_relaxed),
    });
}

GuiBodyAed guiBodyAed(const Plugin& plugin, uint32_t body)
{
    const s3g::Vec3 direction = guiBodyDirection(plugin, body);
    return {
        std::atan2(direction.y, direction.x) * 180.0f / s3g::kPi,
        std::asin(s3g::clamp(direction.z, -1.0f, 1.0f))
            * 180.0f / s3g::kPi,
        plugin.bodyDistance[body].load(std::memory_order_relaxed),
    };
}

float wrapGuiDegrees(float value)
{
    value = std::fmod(value + 180.0f, 360.0f);
    if (value < 0.0f) value += 360.0f;
    return value - 180.0f;
}

float modalLinearToSrgb(float value)
{
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x <= 0.0031308f
        ? x * 12.92f
        : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

NSColor* modalBodyColorFromAed(
    float azimuthDeg, float elevationDeg, float distance, bool selected,
    double alpha = 1.0)
{
    const float hue = std::fmod(azimuthDeg / 360.0f + 1.0f, 1.0f);
    const float light = std::clamp(
        (std::clamp(elevationDeg, -90.0f, 90.0f) + 90.0f) / 180.0f,
        0.28f, 0.88f);
    const float chroma = std::clamp(distance / 2.4f, 0.08f, 1.0f) * 0.37f;
    const float a = std::cos(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float b = std::sin(hue * 2.0f * static_cast<float>(M_PI)) * chroma;
    const float l3 = light + 0.3963377774f * a + 0.2158037573f * b;
    const float m3 = light - 0.1055613458f * a - 0.0638541728f * b;
    const float s3 = light - 0.0894841775f * a - 1.2914855480f * b;
    const float l = l3 * l3 * l3;
    const float m = m3 * m3 * m3;
    const float s = s3 * s3 * s3;
    float red = modalLinearToSrgb(
        4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    float green = modalLinearToSrgb(
        -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    float blue = modalLinearToSrgb(
        -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
    const float grayMix = selected ? 0.08f : 0.18f;
    red = red * (1.0f - grayMix) + 0.74f * grayMix;
    green = green * (1.0f - grayMix) + 0.74f * grayMix;
    blue = blue * (1.0f - grayMix) + 0.74f * grayMix;
    return [NSColor colorWithCalibratedRed:red green:green blue:blue
        alpha:std::clamp(alpha, 0.0, 1.0)];
}

} // namespace

@interface S3GAccelerometerFieldEncoderView : NSView {
    void* _plugin;
    clap_id _dragParam;
    clap_id _openMenu;
    GuiControlLocation _openMenuLocation;
    NSTimer* _refreshTimer;
    int _viewMode;
    CGFloat _viewAzimuthDeg;
    CGFloat _viewElevationDeg;
    CGFloat _viewZoom;
    BOOL _dragView;
    int _dragBody;
    NSPoint _lastDragPoint;
    uint32_t _selectedBody;
    CGFloat _actuatorFlowPhase;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)setParam:(clap_id)param value:(double)value;
- (void)drawControl:(clap_id)param panel:(NSRect)panel row:(uint32_t)row
    labelAttrs:(NSDictionary*)labelAttrs valueAttrs:(NSDictionary*)valueAttrs
    style:(s3g::clap_gui::Style&)style;
- (void)drawFieldWithStyle:(s3g::clap_gui::Style&)style
    attrs:(NSDictionary*)attrs peak:(float)peak;
- (void)drawOpenMenuWithStyle:(s3g::clap_gui::Style&)style
    attrs:(NSDictionary*)attrs;
- (void)updateDraggedParamAtPoint:(NSPoint)point;
- (void)storeViewState;
- (void)setViewPreset:(int)mode;
- (CGFloat)viewScaleForRect:(NSRect)rect;
- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect
    depth:(CGFloat*)depth;
- (int)hitBodyAtPoint:(NSPoint)point inRect:(NSRect)rect;
- (void)updateDraggedBodyAtPoint:(NSPoint)point inRect:(NSRect)rect;
@end

@implementation S3GAccelerometerFieldEncoderView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _openMenu = CLAP_INVALID_ID;
        _openMenuLocation = {};
        _refreshTimer = nil;
        auto* p = static_cast<Plugin*>(plugin);
        _viewMode = p ? p->guiState.viewMode : 2;
        _viewAzimuthDeg = p ? p->guiState.viewAzimuthDeg : -35.0;
        _viewElevationDeg = p ? p->guiState.viewElevationDeg : 34.0;
        _viewZoom = p ? p->guiState.viewZoom : 1.0;
        _selectedBody = p ? p->guiState.selectedBody : 0u;
        _dragView = NO;
        _dragBody = -1;
        _lastDragPoint = NSZeroPoint;
        _actuatorFlowPhase = 0.0;
    }
    return self;
}

- (void)dealloc
{
    [self stopRefreshTimer];
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }

- (void)startRefreshTimer
{
    if (_refreshTimer) return;
    _refreshTimer = [NSTimer timerWithTimeInterval:(1.0 / 24.0)
        target:self selector:@selector(refreshTimerFired:)
        userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_refreshTimer
        forMode:NSRunLoopCommonModes];
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
    if (_plugin && ![self isHidden]
        && s3g::clap_support::hostAppIsActive()) {
        const auto* p = static_cast<const Plugin*>(_plugin);
        const CGFloat activity = std::clamp<CGFloat>(
            p->actuatorActivity.load(std::memory_order_relaxed), 0.0, 1.0);
        if (activity > 0.002) {
            _actuatorFlowPhase = std::fmod(
                _actuatorFlowPhase + 0.010 + activity * 0.022, 1.0);
        }
        [self setNeedsDisplay:YES];
    }
}

- (void)setParam:(clap_id)param value:(double)value
{
    applyControlParam(*static_cast<Plugin*>(_plugin), param, value);
    [self setNeedsDisplay:YES];
}

- (void)storeViewState
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    p->guiState.viewMode = _viewMode;
    p->guiState.viewAzimuthDeg = static_cast<float>(_viewAzimuthDeg);
    p->guiState.viewElevationDeg = static_cast<float>(_viewElevationDeg);
    p->guiState.viewZoom = static_cast<float>(_viewZoom);
    p->guiState.selectedBody = _selectedBody;
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) {
        _viewAzimuthDeg = 0.0;
        _viewElevationDeg = 0.0;
    } else if (mode == 1) {
        _viewAzimuthDeg = 90.0;
        _viewElevationDeg = 90.0;
    } else {
        _viewAzimuthDeg = -35.0;
        _viewElevationDeg = 34.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (CGFloat)viewScaleForRect:(NSRect)rect
{
    return std::min(rect.size.width, rect.size.height) * 0.34
        * std::clamp<CGFloat>(_viewZoom, 0.55, 2.20);
}

- (NSPoint)projectWorldPoint:(s3g::Vec3)point rect:(NSRect)rect
    depth:(CGFloat*)depth
{
    const CGFloat centerX = NSMidX(rect);
    const CGFloat centerY = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    if (_viewMode == 0) {
        if (depth) *depth = static_cast<CGFloat>(point.z);
        return NSMakePoint(centerX - static_cast<CGFloat>(point.y) * scale,
            centerY - static_cast<CGFloat>(point.x) * scale);
    }
    if (_viewMode == 1) {
        if (depth) *depth = static_cast<CGFloat>(point.x);
        return NSMakePoint(centerX - static_cast<CGFloat>(point.y) * scale,
            centerY - static_cast<CGFloat>(point.z) * scale);
    }
    const float azimuth = static_cast<float>(
        _viewAzimuthDeg * M_PI / 180.0);
    const float elevation = static_cast<float>(
        _viewElevationDeg * M_PI / 180.0);
    const float ca = std::cos(azimuth);
    const float sa = std::sin(azimuth);
    const float ce = std::cos(elevation);
    const float se = std::sin(elevation);
    const float x1 = ca * point.x - sa * point.y;
    const float y1 = sa * point.x + ca * point.y;
    const float y2 = ce * y1 - se * point.z;
    const float z2 = se * y1 + ce * point.z;
    if (depth) *depth = static_cast<CGFloat>(z2);
    return NSMakePoint(centerX + static_cast<CGFloat>(x1) * scale,
        centerY - static_cast<CGFloat>(y2) * scale);
}

- (int)hitBodyAtPoint:(NSPoint)point inRect:(NSRect)rect
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return -1;
    const auto snapshot = controlStateSnapshot(*p);
    const uint32_t count = std::clamp<uint32_t>(
        snapshot.params.bodyCount, 4u, 8u);
    const CGFloat drawnRadius = 15.0
        + (8.0 - static_cast<CGFloat>(count)) * 0.8;
    const CGFloat hitRadius = drawnRadius + 4.0;
    int hit = -1;
    CGFloat best = hitRadius * hitRadius;
    CGFloat bestDepth = -std::numeric_limits<CGFloat>::infinity();
    for (uint32_t body = 0u; body < count; ++body) {
        const s3g::Vec3 direction = guiBodyDirection(*p, body);
        const float distance = p->bodyDistance[body].load(
            std::memory_order_relaxed);
        const s3g::Vec3 world {
            direction.x * distance,
            direction.y * distance,
            direction.z * distance,
        };
        CGFloat depth = 0.0;
        const NSPoint projected = [self projectWorldPoint:world
            rect:rect depth:&depth];
        const CGFloat dx = point.x - projected.x;
        const CGFloat dy = point.y - projected.y;
        const CGFloat squared = dx * dx + dy * dy;
        if (squared < best - 0.5
            || (std::fabs(squared - best) <= 0.5 && depth > bestDepth)) {
            best = squared;
            bestDepth = depth;
            hit = static_cast<int>(body);
        }
    }
    return hit;
}

- (void)updateDraggedBodyAtPoint:(NSPoint)point inRect:(NSRect)rect
{
    if (_dragBody < 0
        || _dragBody >= static_cast<int>(
            s3g::kAccelerometerFieldMaxBodyCount)
        || (_viewMode != 0 && _viewMode != 1)) {
        return;
    }
    auto* p = static_cast<Plugin*>(_plugin);
    const auto snapshot = controlStateSnapshot(*p);
    const uint32_t body = static_cast<uint32_t>(_dragBody);
    const GuiBodyAed current = guiBodyAed(*p, body);
    const CGFloat centerX = NSMidX(rect);
    const CGFloat centerY = rect.origin.y + rect.size.height * 0.54;
    const CGFloat scale = [self viewScaleForRect:rect];
    if (scale <= 1.0) return;

    float desiredAzimuth = current.azimuthDeg;
    float desiredElevation = current.elevationDeg;
    float desiredDistance = current.distance;
    if (_viewMode == 0) {
        const float x = static_cast<float>(std::clamp(
            (centerY - point.y) / scale, -2.0, 2.0));
        const float y = static_cast<float>(std::clamp(
            (centerX - point.x) / scale, -2.0, 2.0));
        const float elevationRadians = current.elevationDeg
            * s3g::kPi / 180.0f;
        const float planar = std::sqrt(x * x + y * y);
        desiredDistance = std::clamp(
            planar / std::max(0.05f, std::cos(elevationRadians)),
            0.15f, 2.0f);
        desiredAzimuth = std::atan2(y, x) * 180.0f / s3g::kPi;
    } else {
        const float azimuthRadians = current.azimuthDeg
            * s3g::kPi / 180.0f;
        const float elevationRadians = current.elevationDeg
            * s3g::kPi / 180.0f;
        const float preservedX = std::cos(elevationRadians)
            * std::cos(azimuthRadians) * current.distance;
        const float y = static_cast<float>(std::clamp(
            (centerX - point.x) / scale, -2.0, 2.0));
        const float z = static_cast<float>(std::clamp(
            (centerY - point.y) / scale, -2.0, 2.0));
        desiredDistance = std::clamp(std::sqrt(
            preservedX * preservedX + y * y + z * z), 0.15f, 2.0f);
        desiredAzimuth = std::atan2(y, preservedX)
            * 180.0f / s3g::kPi;
        desiredElevation = std::asin(std::clamp(
            z / std::max(0.15f, desiredDistance), -1.0f, 1.0f))
            * 180.0f / s3g::kPi;
    }

    const float azimuthOffset = wrapGuiDegrees(
        snapshot.params.bodyAzimuthOffsetDeg[body]
            + wrapGuiDegrees(desiredAzimuth - current.azimuthDeg));
    const float elevationOffset = std::clamp(
        snapshot.params.bodyElevationOffsetDeg[body]
            + desiredElevation - current.elevationDeg,
        -180.0f, 180.0f);
    applyControlParam(*p, bodyAedParamId(
        body, BodyAedParamKind::AzimuthOffset), azimuthOffset);
    applyControlParam(*p, bodyAedParamId(
        body, BodyAedParamKind::ElevationOffset), elevationOffset);
    applyControlParam(*p, bodyAedParamId(
        body, BodyAedParamKind::Distance), desiredDistance);
    _selectedBody = body;
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (void)drawControl:(clap_id)param panel:(NSRect)panel row:(uint32_t)row
    labelAttrs:(NSDictionary*)labelAttrs valueAttrs:(NSDictionary*)valueAttrs
    style:(s3g::clap_gui::Style&)style
{
    auto* p = static_cast<Plugin*>(_plugin);
    const auto* spec = paramSpec(param);
    if (!p || !spec) return;
    const double value = getParam(*p, param);
    char formatted[64] {};
    formatParam(param, value, formatted, sizeof(formatted));
    NSString* label = [NSString stringWithUTF8String:shortParamName(param)];
    NSString* display = [NSString stringWithUTF8String:formatted];
    const CGFloat y = controlRowY(panel, row);
    if (spec->display == DisplayKind::Menu) {
        s3g::clap_gui::drawProcessorMenu(label, display, y,
            panel.origin.x, panel.size.width,
            labelAttrs, valueAttrs, style);
    } else {
        s3g::clap_gui::drawProcessorSlider(label, display,
            normalizedParam(*spec, value), y,
            panel.origin.x, panel.size.width,
            labelAttrs, valueAttrs, style);
    }
}

- (void)drawFieldWithStyle:(s3g::clap_gui::Style&)style
    attrs:(NSDictionary*)attrs peak:(float)peak
{
    (void)peak;
    auto* p = static_cast<Plugin*>(_plugin);
    const auto params = controlStateSnapshot(*p).params;
    const NSRect panel = fieldPanelRect();
    const NSRect field = fieldPlotRect();
    s3g::clap_gui::drawPanelFrame(
        panel.origin.x, panel.origin.y,
        panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(
        @"MODAL BODY FIELD", true,
        panel.origin.x, panel.origin.y,
        panel.size.width,
        static_cast<CGFloat>(s3g::gui_layout::kStandardMetrics.headerHeight),
        attrs, style);
    s3g::clap_gui::drawTopologyProcessorCameraButtons(
        panel, _viewMode, attrs, style);
    s3g::clap_gui::drawHeaderButton(
        modalZoomButtonRect(0u), panel, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton(
        modalZoomButtonRect(1u), panel, @"+", false, attrs, style);
    s3g::clap_gui::drawHeaderButton(
        modalResetLayoutButtonRect(), panel, @"RESET", false, attrs, style);
    [s3g::clap_gui::color(0x0a0a0a) setFill];
    NSRectFill(field);
    [style.grid setStroke];
    NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(NSInsetRect(field, 1.0, 1.0));
    const char* bodyName = menuName(kParamSubstrate,
        menuIndexForValue(kParamSubstrate, getParam(*p, kParamSubstrate)));
    const char* listenerName = menuName(kParamListenerPickupSet,
        menuIndexForValue(kParamListenerPickupSet,
            getParam(*p, kParamListenerPickupSet)));
    [[NSString stringWithFormat:
        @"%@  /  %u BODIES  /  96 MODES  /  %s LISTENER",
        [NSString stringWithUTF8String:bodyName], params.bodyCount,
        listenerName]
        drawAtPoint:NSMakePoint(field.origin.x + 12.0, field.origin.y + 12.0)
        withAttributes:attrs];

    const CGFloat centerX = NSMidX(field);
    const CGFloat centerY = field.origin.y + field.size.height * 0.54;
    const CGFloat plotRadius = [self viewScaleForRect:field];
    [s3g::clap_gui::color(0x414141, 0.48) setStroke];
    for (const CGFloat ring : { 0.34, 0.67, 1.0 }) {
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            centerX - plotRadius * ring, centerY - plotRadius * ring,
            plotRadius * ring * 2.0, plotRadius * ring * 2.0)] stroke];
    }
    [NSBezierPath strokeLineFromPoint:NSMakePoint(
        centerX - plotRadius, centerY)
        toPoint:NSMakePoint(centerX + plotRadius, centerY)];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(
        centerX, centerY - plotRadius)
        toPoint:NSMakePoint(centerX, centerY + plotRadius)];

    const uint32_t count = std::clamp<uint32_t>(params.bodyCount, 4u, 8u);
    _selectedBody = std::min<uint32_t>(_selectedBody, count - 1u);
    p->guiState.selectedBody = _selectedBody;
    std::array<NSPoint, s3g::kAccelerometerFieldMaxBodyCount> points {};
    std::array<CGFloat, s3g::kAccelerometerFieldMaxBodyCount> depths {};
    std::array<GuiBodyAed, s3g::kAccelerometerFieldMaxBodyCount> bodyAed {};
    std::array<s3g::Vec3, s3g::kAccelerometerFieldMaxBodyCount>
        bodyDirections {};
    std::array<uint32_t, s3g::kAccelerometerFieldMaxBodyCount> order {};
    for (uint32_t body = 0u; body < count; ++body) {
        order[body] = body;
        bodyAed[body] = guiBodyAed(*p, body);
        const s3g::Vec3 direction = guiBodyDirection(*p, body);
        bodyDirections[body] = direction;
        const float distance = p->bodyDistance[body].load(
            std::memory_order_relaxed);
        const s3g::Vec3 world {
            direction.x * distance,
            direction.y * distance,
            direction.z * distance,
        };
        points[body] = [self projectWorldPoint:world
            rect:field depth:&depths[body]];
    }
    std::sort(order.begin(), order.begin() + count,
        [&](uint32_t first, uint32_t second) {
            return depths[first] < depths[second];
        });

    const bool tetraListener = params.listenerPickupSet
        == s3g::AccelerometerFieldListenerPickupSet::Tetra4;
    const uint32_t pickupCount = tetraListener ? 4u : 8u;
    std::array<s3g::Vec3, s3g::kAmbiFieldListenerMaxLobes>
        listenerDirections {};
    if (tetraListener) {
        const auto& tetra = s3g::ambiFieldListenerTetraDirections();
        std::copy(tetra.begin(), tetra.end(), listenerDirections.begin());
    } else {
        listenerDirections = s3g::ambiFieldListenerCubeDirections();
    }
    std::array<NSPoint, s3g::kAmbiFieldListenerMaxLobes> ears {};
    std::array<CGFloat, s3g::kAmbiFieldListenerMaxLobes> earDepths {};
    std::array<uint32_t, s3g::kAmbiFieldListenerMaxLobes> earOrder {};
    float peakEarEnergy = 0.0f;
    for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
        earOrder[ear] = ear;
        const s3g::Vec3 direction = listenerDirections[ear];
        ears[ear] = [self projectWorldPoint:s3g::Vec3 {
                direction.x * 1.18f,
                direction.y * 1.18f,
                direction.z * 1.18f,
            }
            rect:field depth:&earDepths[ear]];
        peakEarEnergy = std::max(peakEarEnergy,
            p->listenerPickupEnergy[ear].load(std::memory_order_relaxed));
    }
    std::sort(earOrder.begin(), earOrder.begin() + pickupCount,
        [&](uint32_t first, uint32_t second) {
            return earDepths[first] < earDepths[second];
        });

    // The fixed listener shell is world-aligned and camera-projected. Its
    // neutral geometry stays distinct from the bodies' AED/OKLCH identity.
    [s3g::clap_gui::color(0x565656, 0.34) setStroke];
    for (uint32_t first = 0u; first < pickupCount; ++first) {
        for (uint32_t second = first + 1u;
            second < pickupCount; ++second) {
            const auto& a = listenerDirections[first];
            const auto& b = listenerDirections[second];
            const float dot = a.x * b.x + a.y * b.y + a.z * b.z;
            const bool edge = tetraListener
                || std::fabs(dot - 0.3333333333f) < 0.05f;
            if (edge) {
                [NSBezierPath strokeLineFromPoint:ears[first]
                    toPoint:ears[second]];
            }
        }
    }
    for (uint32_t sorted = 0u; sorted < pickupCount; ++sorted) {
        const uint32_t ear = earOrder[sorted];
        const float raw = p->listenerPickupEnergy[ear].load(
            std::memory_order_relaxed);
        const CGFloat relative = peakEarEnergy > 1.0e-7f
            ? std::clamp<CGFloat>(raw / peakEarEnergy, 0.0, 1.0) : 0.0;
        const CGFloat absolute = std::clamp<CGFloat>(
            raw / (raw + 0.015f), 0.0, 1.0);
        const CGFloat strength = std::sqrt(relative * absolute);
        const CGFloat halo = 6.0 + strength * 15.0;
        [s3g::clap_gui::color(0xc8c8c8,
            0.025 + strength * 0.17) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            ears[ear].x - halo, ears[ear].y - halo,
            halo * 2.0, halo * 2.0)] fill];
        const CGFloat radius = 5.5 + strength * 1.5;
        NSBezierPath* diamond = [NSBezierPath bezierPath];
        [diamond moveToPoint:NSMakePoint(ears[ear].x, ears[ear].y - radius)];
        [diamond lineToPoint:NSMakePoint(ears[ear].x + radius, ears[ear].y)];
        [diamond lineToPoint:NSMakePoint(ears[ear].x, ears[ear].y + radius)];
        [diamond lineToPoint:NSMakePoint(ears[ear].x - radius, ears[ear].y)];
        [diamond closePath];
        [s3g::clap_gui::color(0x8c8c8c,
            0.36 + strength * 0.54) setFill];
        [diamond fill];
        [s3g::clap_gui::color(0xd2d2d2,
            0.42 + strength * 0.46) setStroke];
        [diamond setLineWidth:0.8 + strength * 1.2];
        [diamond stroke];
        NSString* label = [NSString stringWithFormat:@"E%u", ear + 1u];
        const NSSize labelSize = [label sizeWithAttributes:attrs];
        const CGFloat labelX = ears[ear].x < centerX
            ? ears[ear].x + 8.0 : ears[ear].x - labelSize.width - 8.0;
        [label drawAtPoint:NSMakePoint(labelX, ears[ear].y - 6.0)
            withAttributes:attrs];
    }

    std::array<CGFloat, s3g::kAccelerometerFieldMaxBodyCount>
        actuatorStrength {};
    std::array<uint32_t, s3g::kAccelerometerFieldMaxBodyCount>
        actuatorEar {};
    for (uint32_t body = 0u; body < count; ++body) {
        const float rawDrive = p->actuatorBodyDrive[body].load(
            std::memory_order_relaxed);
        const CGFloat drive = std::clamp<CGFloat>(
            rawDrive / (rawDrive + 0.04f), 0.0, 1.0);
        actuatorStrength[body] = drive;
        s3g::Vec3 routeDirection = bodyDirections[body];
        if (params.fieldListenMode == s3g::AmbiFieldListenMode::Counter) {
            routeDirection = {
                -routeDirection.x, -routeDirection.y, -routeDirection.z,
            };
        }
        uint32_t bestEar = 0u;
        float bestDot = -2.0f;
        for (uint32_t ear = 0u; ear < pickupCount; ++ear) {
            const auto& direction = listenerDirections[ear];
            const float dot = routeDirection.x * direction.x
                + routeDirection.y * direction.y
                + routeDirection.z * direction.z;
            if (dot > bestDot) {
                bestDot = dot;
                bestEar = ear;
            }
        }
        actuatorEar[body] = bestEar;
        if (drive > 0.008) {
            [s3g::clap_gui::color(0xc0c0c0,
                0.08 + drive * 0.56) setStroke];
            NSBezierPath* route = [NSBezierPath bezierPath];
            [route moveToPoint:ears[bestEar]];
            [route lineToPoint:points[body]];
            [route setLineWidth:0.6 + drive * 2.0];
            [route stroke];
        }
    }
    for (uint32_t sorted = 0u; sorted < count; ++sorted) {
        const uint32_t body = order[sorted];
        const bool selected = body == _selectedBody;
        const CGFloat energy = std::clamp<CGFloat>(
            p->bodyEnergy[body].load(std::memory_order_relaxed) * 18.0f,
            0.0, 1.0);
        const CGFloat radius = 15.0
            + (8.0 - static_cast<CGFloat>(count)) * 0.8;
        if (energy > 0.004) {
            const CGFloat halo = radius + 4.0 + energy * 7.0;
            [modalBodyColorFromAed(
                bodyAed[body].azimuthDeg,
                bodyAed[body].elevationDeg,
                bodyAed[body].distance,
                selected, 0.04 + energy * 0.22) setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                points[body].x - halo, points[body].y - halo,
                halo * 2.0, halo * 2.0)] fill];
        }
        [s3g::clap_gui::color(0x1d1d1d, 0.96) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            points[body].x - radius, points[body].y - radius,
            radius * 2.0, radius * 2.0)] fill];
        [modalBodyColorFromAed(
            bodyAed[body].azimuthDeg,
            bodyAed[body].elevationDeg,
            bodyAed[body].distance,
            selected, 0.88) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            points[body].x - radius + 2.0,
            points[body].y - radius + 2.0,
            radius * 2.0 - 4.0, radius * 2.0 - 4.0)] fill];
        [modalBodyColorFromAed(
            bodyAed[body].azimuthDeg,
            bodyAed[body].elevationDeg,
            bodyAed[body].distance,
            true, 0.34 + energy * 0.62) setStroke];
        NSBezierPath* outline = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            points[body].x - radius, points[body].y - radius,
            radius * 2.0, radius * 2.0)];
        [outline setLineWidth:1.0 + energy * 2.2];
        [outline stroke];
        [s3g::clap_gui::color(0x111111, 0.42) setStroke];
        for (const CGFloat modeRing : { 0.38, 0.68 }) {
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                points[body].x - radius * modeRing,
                points[body].y - radius * modeRing,
                radius * modeRing * 2.0, radius * modeRing * 2.0)] stroke];
        }
        [s3g::clap_gui::color(0x111111, 0.88) setFill];
        NSRectFill(NSMakeRect(points[body].x - 6.0,
            points[body].y - 6.0, 12.0, 12.0));
        NSString* number = [NSString stringWithFormat:@"%u", body + 1u];
        const NSSize numberSize = [number sizeWithAttributes:attrs];
        [number drawAtPoint:NSMakePoint(
            points[body].x - numberSize.width * 0.5,
            points[body].y - numberSize.height * 0.5 - 0.5)
            withAttributes:attrs];
        if (selected) {
            [s3g::clap_gui::color(0xb8b8b8, 0.92) setStroke];
            NSFrameRect(NSMakeRect(points[body].x - radius - 4.0,
                points[body].y - radius - 4.0,
                radius * 2.0 + 8.0, radius * 2.0 + 8.0));
        }
    }

    uint32_t dominantBody = 0u;
    for (uint32_t body = 1u; body < count; ++body) {
        if (actuatorStrength[body] > actuatorStrength[dominantBody]) {
            dominantBody = body;
        }
    }
    for (uint32_t body = 0u; body < count; ++body) {
        const CGFloat drive = actuatorStrength[body];
        if (drive <= 0.008) continue;
        const NSPoint source = ears[actuatorEar[body]];
        const NSPoint destination = points[body];
        const CGFloat phase = std::fmod(_actuatorFlowPhase
            + static_cast<CGFloat>(body)
                / static_cast<CGFloat>(std::max<uint32_t>(1u, count)), 1.0);
        const NSPoint flow = NSMakePoint(
            source.x + (destination.x - source.x) * phase,
            source.y + (destination.y - source.y) * phase);
        const CGFloat radius = body == dominantBody
            ? 3.5 + drive * 3.0 : 2.0 + drive * 2.0;
        NSBezierPath* actuator = [NSBezierPath bezierPath];
        [actuator moveToPoint:NSMakePoint(flow.x, flow.y - radius)];
        [actuator lineToPoint:NSMakePoint(flow.x + radius, flow.y)];
        [actuator lineToPoint:NSMakePoint(flow.x, flow.y + radius)];
        [actuator lineToPoint:NSMakePoint(flow.x - radius, flow.y)];
        [actuator closePath];
        [s3g::clap_gui::color(0xe0e0e0,
            0.46 + drive * 0.48) setFill];
        [actuator fill];
        [s3g::clap_gui::color(0xf0f0f0,
            0.52 + drive * 0.42) setStroke];
        [actuator setLineWidth:0.8 + drive];
        [actuator stroke];
    }

    const CGFloat listenerActivity = std::clamp<CGFloat>(
        p->listenerActivity.load(std::memory_order_relaxed), 0.0, 1.0);
    const CGFloat listenerRadius = 5.0 + listenerActivity * 2.0;
    [s3g::clap_gui::color(0x0a0a0a, 0.92) setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        centerX - listenerRadius, centerY - listenerRadius,
        listenerRadius * 2.0, listenerRadius * 2.0)] fill];
    [s3g::clap_gui::color(0xc8c8c8,
        0.46 + listenerActivity * 0.42) setStroke];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        centerX - listenerRadius, centerY - listenerRadius,
        listenerRadius * 2.0, listenerRadius * 2.0)] stroke];
    [@"L" drawAtPoint:NSMakePoint(centerX - 3.0, centerY - 6.0)
        withAttributes:attrs];
    if (actuatorStrength[dominantBody] > 0.008) {
        NSString* actuatorLabel = params.fieldListenMode
                == s3g::AmbiFieldListenMode::Balance
            ? @"DISTRIBUTED ACTUATOR" : @"ACTUATOR FLOW";
        const NSSize labelSize = [actuatorLabel sizeWithAttributes:attrs];
        const NSPoint labelOrigin = NSMakePoint(
            centerX - labelSize.width * 0.5, centerY + listenerRadius + 8.0);
        [s3g::clap_gui::color(0x0a0a0a, 0.84) setFill];
        NSRectFill(NSInsetRect(NSMakeRect(
            labelOrigin.x, labelOrigin.y,
            labelSize.width, labelSize.height), -3.0, -2.0));
        [actuatorLabel drawAtPoint:labelOrigin withAttributes:attrs];
    }

    const GuiBodyAed selectedAed = bodyAed[_selectedBody];
    NSString* selectedReadout = [NSString stringWithFormat:
        @"B%u  AZ %+5.1f  EL %+5.1f  DST %.2f  /  %@",
        _selectedBody + 1u, selectedAed.azimuthDeg,
        selectedAed.elevationDeg, selectedAed.distance,
        (_viewMode == 0 || _viewMode == 1)
            ? @"DRAG BODY" : @"SELECT BODY / DRAG BLANK CAMERA"];
    [selectedReadout drawAtPoint:NSMakePoint(
        field.origin.x + 12.0, NSMaxY(field) - 57.0)
        withAttributes:attrs];
    [@"OKLCH  H=AZ  L=EL  C=DIST" drawAtPoint:NSMakePoint(
        field.origin.x + 12.0, NSMaxY(field) - 39.0)
        withAttributes:attrs];
    NSString* format = params.outputMode
            == s3g::AccelerometerFieldOutputMode::Ambisonic
        ? [NSString stringWithFormat:@"%uOA  ACN/SN3D",
            params.ambisonicOrder]
        : [NSString stringWithFormat:@"%u BODY STEMS + %u ZERO",
            count, 8u - count];
    [format drawAtPoint:NSMakePoint(
        field.origin.x + 12.0, NSMaxY(field) - 21.0)
        withAttributes:attrs];
    [NSGraphicsContext restoreGraphicsState];
}

- (void)drawOpenMenuWithStyle:(s3g::clap_gui::Style&)style
    attrs:(NSDictionary*)attrs
{
    if (_openMenu == CLAP_INVALID_ID) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t count = menuCount(_openMenu);
    constexpr uint32_t kMaximumMenuItems = kFactoryPresetCount + 1u;
    if (!p || count == 0u || count > kMaximumMenuItems) return;
    NSRect anchor = _openMenu == kParamPreset
        ? s3g::clap_gui::cocoaRect(kTitleBand.presetMenu)
        : menuAnchorRect(_openMenuLocation);
    const CGFloat itemHeight = 19.0;
    const uint32_t columns = menuColumnCount(count);
    const uint32_t rows = s3g::clap_gui::multiColumnMenuRows(
        count, columns);
    NSRect menu = NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width * static_cast<CGFloat>(columns),
        itemHeight * static_cast<CGFloat>(rows));
    std::array<NSString*, kMaximumMenuItems> items {};
    for (uint32_t index = 0u; index < count; ++index) {
        items[index] = [NSString stringWithUTF8String:
            menuName(_openMenu, index)];
    }
    const int selected = static_cast<int>(menuIndexForValue(
        _openMenu, getParam(*p, _openMenu)));
    if (columns > 1u) {
        s3g::clap_gui::drawMultiColumnDropdownMenu(
            menu, itemHeight, items.data(), count, columns,
            selected, -1, attrs, style);
    } else {
        s3g::clap_gui::drawDropdownMenu(
            menu, itemHeight, items.data(), count,
            selected, -1, attrs, style);
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
    if (!_dragView && _dragBody < 0) {
        _viewMode = p->guiState.viewMode;
        _viewAzimuthDeg = p->guiState.viewAzimuthDeg;
        _viewElevationDeg = p->guiState.viewElevationDeg;
        _viewZoom = p->guiState.viewZoom;
        _selectedBody = p->guiState.selectedBody;
    }
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    const float peak = p->outputPeak.exchange(
        p->outputPeak.load(std::memory_order_relaxed) * 0.92f,
        std::memory_order_relaxed);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g AMBI ENCODER MODAL",
        [NSString stringWithUTF8String:p->presetName],
        s3g::clap_gui::peakDbText(peak), kTitleBand,
        s3g::clap_gui::softTitleAttrs(), labels, values, style);

    [self drawFieldWithStyle:style attrs:values peak:peak];
    const auto drawPanel = [&](NSString* title,
                               const s3g::gui_layout::Panel& panel) {
        s3g::clap_gui::drawPanelFrame(panel, style);
        s3g::clap_gui::drawPanelHeader(title, true, panel, labels, style);
    };
    drawPanel(@"OUTPUT", kOutputPanelLayout);
    drawPanel(@"MODAL BODY", kStructurePanelLayout);
    drawPanel(@"LISTENER / ACTUATOR", kActuatorPanelLayout);
    drawPanel(@"PROJECTION", kProjectionPanelLayout);
    drawPanel(@"BODY CHARACTER", kRadiationPanelLayout);
    drawPanel(@"BODY AED", kBodyPointPanelLayout);
    drawPanel(@"SIGNAL PATH", kSignalPanelLayout);

    for (uint32_t row = 0u; row < kOutputControls.size(); ++row) {
        [self drawControl:kOutputControls[row]
            panel:outputPanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    for (uint32_t row = 0u; row < kActuatorControls.size(); ++row) {
        [self drawControl:kActuatorControls[row]
            panel:actuatorPanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    for (uint32_t row = 0u; row < kStructureControls.size(); ++row) {
        [self drawControl:kStructureControls[row]
            panel:structurePanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    for (uint32_t row = 0u; row < kProjectionControls.size(); ++row) {
        [self drawControl:kProjectionControls[row]
            panel:projectionPanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    for (uint32_t row = 0u; row < kRadiationControls.size(); ++row) {
        [self drawControl:kRadiationControls[row]
            panel:radiationPanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    const auto selectedControls = bodyPointControls(_selectedBody);
    for (uint32_t row = 0u; row < selectedControls.size(); ++row) {
        [self drawControl:selectedControls[row]
            panel:bodyPointPanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    const GuiBodyAed selectedAed = guiBodyAed(*p, _selectedBody);
    [[NSString stringWithFormat:@"B%u  ACTUAL  %+5.1f / %+5.1f / %.2f",
        _selectedBody + 1u, selectedAed.azimuthDeg,
        selectedAed.elevationDeg, selectedAed.distance]
        drawAtPoint:NSMakePoint(
            bodyPointPanelRect().origin.x + 16.0,
            bodyPointPanelRect().origin.y + 122.0)
        withAttributes:values];
    const NSRect signal = signalPanelRect();
    const CGFloat signalX = signal.origin.x + 16.0;
    const char* bodyName = menuName(kParamSubstrate,
        menuIndexForValue(kParamSubstrate, getParam(*p, kParamSubstrate)));
    [[NSString stringWithFormat:@"%@ PROFILE",
        [NSString stringWithUTF8String:bodyName]]
        drawAtPoint:NSMakePoint(signalX, signal.origin.y + 38.0)
        withAttributes:values];
    [@"LISTENER / ACTUATOR" drawAtPoint:NSMakePoint(
        signalX, signal.origin.y + 64.0)
        withAttributes:labels];
    [@"↓" drawAtPoint:NSMakePoint(
        signalX + 3.0, signal.origin.y + 80.0)
        withAttributes:values];
    [@"CONTINUOUS MODAL DRIVE" drawAtPoint:NSMakePoint(
        signalX, signal.origin.y + 96.0)
        withAttributes:values];
    [[NSString stringWithFormat:@"↓  %u BODIES / 96 MODES",
        static_cast<uint32_t>(getParam(*p, kParamBodyCount))]
        drawAtPoint:NSMakePoint(signalX, signal.origin.y + 116.0)
        withAttributes:values];
    [@"LIFT → OUT → LINKED GUARD" drawAtPoint:NSMakePoint(
        signalX, signal.origin.y + 136.0)
        withAttributes:values];
    [self drawOpenMenuWithStyle:style attrs:values];
}

- (void)updateDraggedParamAtPoint:(NSPoint)point
{
    const auto* spec = paramSpec(_dragParam);
    if (!spec || spec->display == DisplayKind::Menu) return;
    GuiControlLocation location {};
    const bool found = findInGroup(point, kOutputPanelLayout,
            kOutputControls, location)
        || findInGroup(point, kActuatorPanelLayout,
            kActuatorControls, location)
        || findInGroup(point, kStructurePanelLayout,
            kStructureControls, location)
        || findInGroup(point, kProjectionPanelLayout,
            kProjectionControls, location)
        || findInGroup(point, kRadiationPanelLayout,
            kRadiationControls, location)
        || findInGroup(point, kBodyPointPanelLayout,
            bodyPointControls(_selectedBody), location);
    const NSRect panel = found && location.id == _dragParam
        ? location.panel : panelForParam(_dragParam);
    const double x = s3g::gui_layout::processorControlX(panel.origin.x);
    const double width = s3g::gui_layout::processorTrackWidth(panel.size.width);
    const double normalized = std::clamp((point.x - x) / width, 0.0, 1.0);
    [self setParam:_dragParam
        value:valueFromNormalized(*spec, normalized)];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);
    const CGFloat itemHeight = 19.0;
    if (_openMenu != CLAP_INVALID_ID) {
        const uint32_t count = menuCount(_openMenu);
        NSRect anchor = _openMenu == kParamPreset
            ? s3g::clap_gui::cocoaRect(kTitleBand.presetMenu)
            : menuAnchorRect(_openMenuLocation);
        const uint32_t columns = menuColumnCount(count);
        const uint32_t rows = s3g::clap_gui::multiColumnMenuRows(
            count, columns);
        const NSRect menu = NSMakeRect(anchor.origin.x,
            NSMaxY(anchor) + 2.0,
            anchor.size.width * static_cast<CGFloat>(columns),
            itemHeight * static_cast<CGFloat>(rows));
        const int selected = columns > 1u
            ? s3g::clap_gui::multiColumnDropdownHitIndex(
                point, menu, itemHeight, count, columns)
            : s3g::clap_gui::dropdownHitIndex(
                point, menu, itemHeight, count);
        if (selected >= 0) {
            [self setParam:_openMenu value:menuValueForIndex(
                _openMenu, static_cast<uint32_t>(selected))];
            _openMenu = CLAP_INVALID_ID;
            return;
        }
        _openMenu = CLAP_INVALID_ID;
    }

    if (NSPointInRect(point,
        s3g::clap_gui::cocoaRect(kTitleBand.presetMenu))) {
        _openMenu = kParamPreset;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, s3g::clap_gui::cocoaRect(kTitleBand.loadButton))
        || NSPointInRect(point,
            s3g::clap_gui::cocoaRect(kTitleBand.saveButton))) {
        if (s3g::clap_gui::handleProcessorTitleClick(
            point, &p->plugin, @"Ambi Encoder Modal",
            kTitleBand, p->presetName, sizeof(p->presetName),
            kParamOutputGain)) {
            [self setNeedsDisplay:YES];
        }
        return;
    }
    if (NSPointInRect(point,
        s3g::clap_gui::cocoaRect(kTitleBand.randomButton))) {
        randomizeSafe(*p);
        std::snprintf(p->presetName, sizeof(p->presetName), "%s", "RANDOM");
        [self setNeedsDisplay:YES];
        return;
    }

    const NSRect fieldPanel = fieldPanelRect();
    for (uint32_t index = 0u; index < 2u; ++index) {
        if (NSPointInRect(point, modalZoomButtonRect(index))) {
            _viewZoom = std::clamp<CGFloat>(
                _viewZoom + (index == 0u ? -0.15 : 0.15), 0.55, 2.20);
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (NSPointInRect(point,
            s3g::clap_gui::topologyProcessorCameraButtonRect(
                fieldPanel, index))) {
            [self setViewPreset:static_cast<int>(index)];
            return;
        }
    }
    if (NSPointInRect(point, modalResetLayoutButtonRect())) {
        for (uint32_t body = 0u;
            body < s3g::kAccelerometerFieldMaxBodyCount; ++body) {
            applyControlParam(*p, bodyAedParamId(
                body, BodyAedParamKind::AzimuthOffset), 0.0);
            applyControlParam(*p, bodyAedParamId(
                body, BodyAedParamKind::ElevationOffset), 0.0);
            applyControlParam(*p, bodyAedParamId(
                body, BodyAedParamKind::Distance), 1.0);
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, fieldPlotRect())) {
        const int hit = [self hitBodyAtPoint:point inRect:fieldPlotRect()];
        if (hit >= 0) {
            _selectedBody = static_cast<uint32_t>(hit);
            p->guiState.selectedBody = _selectedBody;
            if (_viewMode == 0 || _viewMode == 1) {
                _dragBody = hit;
                [self updateDraggedBodyAtPoint:point inRect:fieldPlotRect()];
            }
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
        _dragView = YES;
        _lastDragPoint = point;
        [self setNeedsDisplay:YES];
        return;
    }

    GuiControlLocation location {};
    if (!findInGroup(point, kOutputPanelLayout, kOutputControls, location)
        && !findInGroup(point, kActuatorPanelLayout, kActuatorControls, location)
        && !findInGroup(point, kStructurePanelLayout, kStructureControls, location)
        && !findInGroup(point, kProjectionPanelLayout, kProjectionControls, location)
        && !findInGroup(point, kRadiationPanelLayout, kRadiationControls, location)
        && !findInGroup(point, kBodyPointPanelLayout,
            bodyPointControls(_selectedBody), location)) {
        return;
    }
    const auto* spec = paramSpec(location.id);
    if (!spec) return;
    if (spec->display == DisplayKind::Menu) {
        _openMenu = location.id;
        _openMenuLocation = location;
        [self setNeedsDisplay:YES];
        return;
    }
    double defaultValue = 0.0;
    if (s3g::clap_gui::sliderDoubleClickDefault(
        event, &p->plugin, location.id, &defaultValue)) {
        [self setParam:location.id value:defaultValue];
        return;
    }
    _dragParam = location.id;
    [self updateDraggedParamAtPoint:point];
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    if (_dragBody >= 0) {
        [self updateDraggedBodyAtPoint:point inRect:fieldPlotRect()];
        return;
    }
    if (_dragView) {
        _viewAzimuthDeg += (point.x - _lastDragPoint.x) * 0.35;
        _viewElevationDeg = std::clamp<CGFloat>(
            _viewElevationDeg + (point.y - _lastDragPoint.y) * 0.35,
            -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam != CLAP_INVALID_ID) {
        [self updateDraggedParamAtPoint:point];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = CLAP_INVALID_ID;
    _dragView = NO;
    _dragBody = -1;
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
    return !isFloating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api,
    bool* isFloating)
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
    p->guiView = [[S3GAccelerometerFieldEncoderView alloc]
        initWithPlugin:p];
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
    if (!p || !p->guiView) return;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAccelerometerFieldEncoderView*>(p->guiView)
        stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        p->guiViewport, p->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }
bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiCanResize(const clap_plugin_t*) { return true; }
bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}
bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight, width, height);
}
bool guiSetSize(const clap_plugin_t* plugin, uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
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
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, false)) {
        return false;
    }
    p->guiVisible.store(true, std::memory_order_relaxed);
    [static_cast<S3GAccelerometerFieldEncoderView*>(p->guiView)
        startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_relaxed);
    [static_cast<S3GAccelerometerFieldEncoderView*>(p->guiView)
        stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
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
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    kPluginId,
    kHostName,
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.9.0",
    "A transient-free, CPU-bounded ensemble of four to eight editable modal bodies with modal-aware level lift, a visible Tetra/Cube listener-actuator environment, and third-order ACN/SN3D encoding.",
    features,
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
    p->params = s3g::accelerometerFieldFactoryPreset(0u);
    p->presetIndex = 0u;
    p->audioParams = p->params;
    p->audioPresetIndex = p->presetIndex;
    p->paramsMailbox = p->params;
    p->paramsMailboxPresetIndex = p->presetIndex;
    p->engine.setParams(p->audioParams);
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
    factoryGetPluginCount, factoryGetPluginDescriptor, createPlugin
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
    CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory
};
