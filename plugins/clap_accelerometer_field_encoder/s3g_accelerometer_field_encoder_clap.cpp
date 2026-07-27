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
#include <new>
#include <vector>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAccelerometerFieldMaxChannels;
constexpr uint32_t kInputChannels = 1u;
constexpr uint32_t kStateVersion = 6u;
constexpr uint32_t kFactoryPresetCount = s3g::kAccelerometerFieldPresetCount;
constexpr uint32_t kCustomPresetIndex = kFactoryPresetCount;
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
};

enum class DisplayKind : uint8_t {
    Menu,
    Percent,
    Hertz,
    Decibels,
    Degrees,
    Position,
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

constexpr std::array<ParamSpec, 36u> kParamSpecs {{
    { kParamPreset, "Preset", "Preset", 0.0, static_cast<double>(kFactoryPresetCount), 0.0, DisplayKind::Menu, false, false },
    { kParamSubstrate, "Gong body", "Modal Body", 0.0, 12.0, 10.0, DisplayKind::Menu },
    { kParamExcitation, "Exciter", "Exciter", 0.0, 5.0, 5.0, DisplayKind::Menu },
    { kParamReadout, "Legacy sensor readout", "Advanced", 0.0, 2.0, 0.0, DisplayKind::Menu, false, true, true },
    { kParamEventRate, "Event rate", "Exciter", 0.01, 80.0, 0.18, DisplayKind::Hertz, true },
    { kParamActivity, "Activity", "Exciter", 0.0, 1.0, 0.78, DisplayKind::Percent },
    { kParamForce, "Mallet force", "Exciter", 0.0, 1.0, 0.62, DisplayKind::Percent },
    { kParamTexture, "Mallet texture", "Exciter", 0.0, 1.0, 0.12, DisplayKind::Percent },
    { kParamAmbientDrive, "Ambient drive", "Exciter", 0.0, 1.0, 0.012, DisplayKind::Percent },
    { kParamExternalDrive, "Force input", "Exciter", 0.0, 1.0, 0.0, DisplayKind::Percent },
    { kParamSize, "Size", "Modal Body", 0.0, 1.0, 0.50, DisplayKind::Percent },
    { kParamDamping, "Damping", "Modal Body", 0.0, 1.0, 0.08, DisplayKind::Percent },
    { kParamIrregularity, "Irregularity", "Modal Body", 0.0, 1.0, 0.025, DisplayKind::Percent },
    { kParamPropagationLoss, "Propagation loss", "Modal Body", 0.0, 1.0, 0.16, DisplayKind::Percent },
    { kParamContactDetail, "Contact detail", "Modal Body", 0.0, 1.0, 0.055, DisplayKind::Percent },
    { kParamSourcePosition, "Strike position", "Modal Body", 0.0, 1.0, 0.43, DisplayKind::Position },
    { kParamPickupPosition, "Radiation center", "Radiation", 0.0, 1.0, 0.50, DisplayKind::Position },
    { kParamPickupAxis, "Modal angle", "Radiation", 0.0, 1.0, 0.34, DisplayKind::Percent },
    { kParamSensorMass, "Legacy sensor mass", "Advanced", 0.0, 1.0, 0.01, DisplayKind::Percent, false, true, true },
    { kParamMountStiffness, "Legacy mount stiffness", "Advanced", 0.0, 1.0, 0.90, DisplayKind::Percent, false, true, true },
    { kParamConditionerHighpass, "Legacy conditioner HPF", "Advanced", 0.25, 180.0, 0.50, DisplayKind::Hertz, true, true, true },
    { kParamSensorNoise, "Legacy sensor noise", "Advanced", 0.0, 1.0, 0.005, DisplayKind::Percent, false, true, true },
    { kParamAirRadiation, "Air radiation", "Radiation", 0.0, 1.0, 0.24, DisplayKind::Percent },
    { kParamContactRadiation, "Contact / radiation", "Projection", 0.0, 1.0, 0.52, DisplayKind::Percent },
    { kParamSpatialExtent, "AED spread", "Projection", 0.0, 1.0, 0.90, DisplayKind::Percent },
    { kParamFieldAzimuth, "Field azimuth", "Projection", -180.0, 180.0, 0.0, DisplayKind::Degrees },
    { kParamFieldElevation, "Field elevation", "Projection", -90.0, 90.0, 0.0, DisplayKind::Degrees },
    { kParamOrder, "Ambisonic order", "Output", 1.0, 3.0, 3.0, DisplayKind::Menu },
    { kParamOutputMode, "Legacy output mode", "Advanced", 0.0, 1.0, 0.0, DisplayKind::Menu, false, false, true },
    { kParamOutputGain, "Output gain", "Output", -60.0, 12.0, -11.0, DisplayKind::Decibels },
    { kParamArraySpread, "Radiation spread", "Radiation", 0.0, 1.0, 0.94, DisplayKind::Percent },
    { kParamFieldListenMode, "Listen mode", "Listener", 0.0, 3.0, 0.0, DisplayKind::Menu },
    { kParamFieldListenAmount, "Listen influence", "Listener", 0.0, 1.0, 0.62, DisplayKind::Percent },
    { kParamFieldListenResponse, "Listen response", "Listener", 0.0, 2.0, 0.0, DisplayKind::Menu },
    { kParamCoupling, "Modal coupling", "Modal Body", 0.0, 1.0, 0.72, DisplayKind::Percent },
    { kParamEnergy, "Nonlinear energy", "Modal Body", 0.0, 1.0, 0.64, DisplayKind::Percent },
}};

constexpr const char* kSubstrateNames[] {
    "GONG AGENG", "BELL BRONZE", "JING", "KKWAENGGWARI"
};
constexpr const char* kExcitationNames[] {
    "AMBIENT", "DOUBLE MALLET", "MALLET ROLL", "SCRAPE / RUB",
    "BOWED RIM", "STRIKE"
};
constexpr const char* kReadoutNames[] {
    "ACCELERATION", "VELOCITY", "DISPLACEMENT"
};
constexpr const char* kOrderNames[] { "1OA / 4CH", "2OA / 9CH", "3OA / 16CH" };
constexpr const char* kOutputModeNames[] { "ACN/SN3D", "8 SENSOR STEMS" };
constexpr const char* kFieldListenModeNames[] {
    "OFF", "FOLLOW", "COUNTER", "BALANCE"
};
constexpr const char* kFieldListenResponseNames[] {
    "EXCITE", "SETTLE", "IMPRINT"
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
    case kParamFieldListenMode:
        return kFieldListenModeNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kFieldListenModeNames)) - 1u)];
    case kParamFieldListenResponse:
        return kFieldListenResponseNames[std::min<uint32_t>(index,
            static_cast<uint32_t>(std::size(kFieldListenResponseNames)) - 1u)];
    default: return "";
    }
}

uint32_t menuCount(clap_id id);

uint32_t menuIndexForValue(clap_id id, double value)
{
    if (id == kParamOrder) {
        return roundedIndex(value - 1.0, menuCount(id));
    }
    if (id == kParamSubstrate) {
        switch (static_cast<s3g::AccelerometerSubstrate>(
            roundedIndex(value, 13u))) {
        case s3g::AccelerometerSubstrate::BellBronze: return 1u;
        case s3g::AccelerometerSubstrate::Jing: return 2u;
        case s3g::AccelerometerSubstrate::Kkwaenggwari: return 3u;
        default: return 0u;
        }
    }
    return roundedIndex(value, menuCount(id));
}

double menuValueForIndex(clap_id id, uint32_t index)
{
    if (id == kParamOrder) return static_cast<double>(index + 1u);
    if (id == kParamSubstrate) {
        constexpr std::array<s3g::AccelerometerSubstrate, 4u> bodies {{
            s3g::AccelerometerSubstrate::GongAgeng,
            s3g::AccelerometerSubstrate::BellBronze,
            s3g::AccelerometerSubstrate::Jing,
            s3g::AccelerometerSubstrate::Kkwaenggwari,
        }};
        return static_cast<double>(static_cast<uint32_t>(
            bodies[std::min<uint32_t>(index, bodies.size() - 1u)]));
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
    case kParamFieldListenMode: return static_cast<uint32_t>(std::size(kFieldListenModeNames));
    case kParamFieldListenResponse: return static_cast<uint32_t>(std::size(kFieldListenResponseNames));
    default: return 0u;
    }
}

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t presetIndex = 0u;
    s3g::AccelerometerFieldParams params =
        s3g::accelerometerFieldFactoryPreset(0u);
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

void focusGongParams(s3g::AccelerometerFieldParams& params)
{
    if (params.substrate != s3g::AccelerometerSubstrate::BellBronze
        && params.substrate != s3g::AccelerometerSubstrate::GongAgeng
        && params.substrate != s3g::AccelerometerSubstrate::Jing
        && params.substrate != s3g::AccelerometerSubstrate::Kkwaenggwari) {
        params.substrate = s3g::AccelerometerSubstrate::GongAgeng;
    }
    params = s3g::sanitizeAccelerometerFieldParams(params);
}

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    s3g::AccelerometerFieldEncoder engine {};
    s3g::AccelerometerFieldParams params =
        s3g::accelerometerFieldFactoryPreset(0u);
    uint32_t presetIndex = 0u;
    std::array<std::vector<float>, kOutputChannels> scratchOutputs {};
    std::vector<float> scratchInput {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> listenerActivity { 0.0f };
    std::atomic<float> listenerTarget { 0.5f };
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    std::atomic<bool> guiVisible { false };
    char presetName[64] { "Gong Ageng" };
#endif
};

struct MidiStrike {
    uint32_t time = 0u;
    int32_t key = 60;
    float velocity = 0.0f;
};

struct ProcessEventBatch {
    static constexpr uint32_t kMaximumStrikes = 256u;
    std::array<MidiStrike, kMaximumStrikes> strikes {};
    uint32_t strikeCount = 0u;
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

double getParam(const Plugin& plugin, clap_id id)
{
    const auto& p = plugin.params;
    switch (id) {
    case kParamPreset: return plugin.presetIndex;
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
    default: return 0.0;
    }
}

void applyParam(Plugin& plugin, clap_id id, double value)
{
    if (id == kParamPreset) {
        const uint32_t index = roundedIndex(
            value, kFactoryPresetCount + 1u);
        if (index < kFactoryPresetCount) {
            plugin.params = s3g::accelerometerFieldFactoryPreset(index);
            plugin.presetIndex = index;
#if defined(__APPLE__)
            std::snprintf(plugin.presetName, sizeof(plugin.presetName), "%s",
                s3g::accelerometerFieldFactoryPresetInfo(index).name);
#endif
            plugin.engine.setParams(plugin.params);
            plugin.engine.reset();
        }
        return;
    }

    auto& p = plugin.params;
    switch (id) {
    case kParamSubstrate:
        switch (static_cast<s3g::AccelerometerSubstrate>(
            roundedIndex(value, 13u))) {
        case s3g::AccelerometerSubstrate::BellBronze:
            p.substrate = s3g::AccelerometerSubstrate::BellBronze; break;
        case s3g::AccelerometerSubstrate::Jing:
            p.substrate = s3g::AccelerometerSubstrate::Jing; break;
        case s3g::AccelerometerSubstrate::Kkwaenggwari:
            p.substrate = s3g::AccelerometerSubstrate::Kkwaenggwari; break;
        default: p.substrate = s3g::AccelerometerSubstrate::GongAgeng; break;
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
    case kParamOutputMode: p.outputMode = static_cast<s3g::AccelerometerFieldOutputMode>(roundedIndex(value, 2u)); break;
    case kParamOutputGain: p.outputGainDb = static_cast<float>(value); break;
    case kParamFieldListenMode: p.fieldListenMode = static_cast<s3g::AmbiFieldListenMode>(roundedIndex(value, 4u)); break;
    case kParamFieldListenAmount: p.fieldListenAmount = static_cast<float>(value); break;
    case kParamFieldListenResponse: p.fieldListenResponse = static_cast<s3g::AmbiFieldListenerResponse>(roundedIndex(value, 3u) + 1u); break;
    case kParamCoupling: p.coupling = static_cast<float>(value); break;
    case kParamEnergy: p.energy = static_cast<float>(value); break;
    default: return;
    }
    plugin.params = s3g::sanitizeAccelerometerFieldParams(plugin.params);
    plugin.presetIndex = kCustomPresetIndex;
#if defined(__APPLE__)
    std::snprintf(plugin.presetName, sizeof(plugin.presetName), "%s", "Custom");
#endif
    plugin.engine.setParams(plugin.params);
}

bool init(const clap_plugin_t*) { return true; }

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
    p->sampleRate = std::max(1000.0, sampleRate);
    p->maxFrames = std::max(1u, maxFrames);
    p->scratchInput.assign(p->maxFrames, 0.0f);
    for (auto& channel : p->scratchOutputs) {
        channel.assign(p->maxFrames, 0.0f);
    }
    p->engine.prepare(p->sampleRate);
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
    p->listenerActivity.store(0.0f, std::memory_order_relaxed);
    p->listenerTarget.store(
        p->params.sourcePosition, std::memory_order_relaxed);
}

ProcessEventBatch readInputEvents(Plugin& plugin,
    const clap_input_events_t* events, uint32_t frames)
{
    ProcessEventBatch batch;
    if (!events) return batch;
    const uint32_t count = events->size(events);
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = events->get(events, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (event->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* param =
                reinterpret_cast<const clap_event_param_value_t*>(event);
            applyParam(plugin, param->param_id, param->value);
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
    while (offset < process.frames_count) {
        while (strikeIndex < events.strikeCount
            && events.strikes[strikeIndex].time <= offset) {
            const auto& strike = events.strikes[strikeIndex++];
            plugin.engine.strikeMidi(strike.key, strike.velocity);
        }
        uint32_t end = process.frames_count;
        if (strikeIndex < events.strikeCount) {
            end = std::min(end, events.strikes[strikeIndex].time);
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
    plugin.listenerActivity.store(
        plugin.engine.listenerActivity(), std::memory_order_relaxed);
    plugin.listenerTarget.store(
        plugin.engine.listenerTargetPosition(), std::memory_order_relaxed);
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
    float peak = 0.0f;
    while (offset < process.frames_count) {
        while (strikeIndex < events.strikeCount
            && events.strikes[strikeIndex].time <= offset) {
            const auto& strike = events.strikes[strikeIndex++];
            plugin.engine.strikeMidi(strike.key, strike.velocity);
        }
        uint32_t end = std::min<uint32_t>(
            process.frames_count, offset + plugin.maxFrames);
        if (strikeIndex < events.strikeCount) {
            end = std::min(end, events.strikes[strikeIndex].time);
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
    plugin.listenerActivity.store(
        plugin.engine.listenerActivity(), std::memory_order_relaxed);
    plugin.listenerTarget.store(
        plugin.engine.listenerTargetPosition(), std::memory_order_relaxed);
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
    const ProcessEventBatch events = readInputEvents(
        *p, process->in_events, process->frames_count);
    const clap_audio_buffer_t* input = process->audio_inputs_count > 0u
        ? &process->audio_inputs[0] : nullptr;
    auto& output = process->audio_outputs[0];
    if (output.data32) {
        return processFloat(*p, *process, input, output, events);
    }
    if (output.data64) {
        return processDouble(*p, *process, input, output, events);
    }
    return CLAP_PROCESS_ERROR;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "Gong Force In" : "3OA ACN/SN3D Gong Field",
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
    std::strncpy(info->name, "Gong MIDI Strike In", sizeof(info->name));
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
        std::snprintf(text, size, "%+.0f deg", value);
        return true;
    case DisplayKind::Position:
        std::snprintf(text, size, "%.0f %%", value * 100.0);
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
    (void)readInputEvents(*self(plugin), input, 0u);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    const auto* p = self(plugin);
    const SavedState state { kStateVersion, p->presetIndex, p->params };
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    SavedStateHeader header {};
    if (!readExact(stream, &header, sizeof(header))) {
        return false;
    }
    auto* p = self(plugin);
    if (header.version == kStateVersion || header.version == 5u) {
        s3g::AccelerometerFieldParams params {};
        if (!readExact(stream, &params, sizeof(params))) return false;
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
    if (header.version < kStateVersion) {
        focusGongParams(p->params);
        p->presetIndex = kCustomPresetIndex;
    } else {
        p->presetIndex = std::min<uint32_t>(
            header.presetIndex, kCustomPresetIndex);
    }
#if defined(__APPLE__)
    std::snprintf(p->presetName, sizeof(p->presetName), "%s",
        menuName(kParamPreset, p->presetIndex));
#endif
    p->engine.setParams(p->params);
    p->engine.reset();
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

constexpr std::array<clap_id, 2u> kOutputControls {{
    kParamOrder, kParamOutputGain,
}};
constexpr std::array<clap_id, 7u> kExcitationControls {{
    kParamExcitation, kParamEventRate, kParamActivity, kParamForce,
    kParamTexture, kParamAmbientDrive, kParamExternalDrive,
}};
constexpr std::array<clap_id, 9u> kStructureControls {{
    kParamSubstrate, kParamSize, kParamDamping, kParamIrregularity,
    kParamCoupling, kParamEnergy, kParamPropagationLoss,
    kParamContactDetail, kParamSourcePosition,
}};
constexpr std::array<clap_id, 5u> kProjectionControls {{
    kParamContactRadiation, kParamSpatialExtent, kParamFieldAzimuth,
    kParamFieldElevation, kParamAirRadiation,
}};
constexpr std::array<clap_id, 3u> kRadiationControls {{
    kParamPickupPosition, kParamArraySpread, kParamPickupAxis,
}};
constexpr std::array<clap_id, 3u> kListenerControls {{
    kParamFieldListenMode, kParamFieldListenAmount,
    kParamFieldListenResponse,
}};

NSRect fieldPanelRect() { return NSMakeRect(18.0, 42.0, 594.0, 700.0); }
NSRect fieldPlotRect() { return NSMakeRect(30.0, 74.0, 570.0, 654.0); }
NSRect outputPanelRect() { return NSMakeRect(630.0, 42.0, 250.0, 96.0); }
NSRect excitationPanelRect() { return NSMakeRect(630.0, 150.0, 250.0, 216.0); }
NSRect structurePanelRect() { return NSMakeRect(630.0, 378.0, 250.0, 286.0); }
NSRect projectionPanelRect() { return NSMakeRect(896.0, 42.0, 246.0, 164.0); }
NSRect radiationPanelRect() { return NSMakeRect(896.0, 218.0, 246.0, 112.0); }
NSRect listenerPanelRect() { return NSMakeRect(896.0, 342.0, 246.0, 112.0); }
NSRect signalPanelRect() { return NSMakeRect(896.0, 466.0, 246.0, 198.0); }

NSRect panelForParam(clap_id id)
{
    switch (id) {
    case kParamOrder:
    case kParamOutputGain:
        return outputPanelRect();
    case kParamContactRadiation:
    case kParamSpatialExtent:
    case kParamFieldAzimuth:
    case kParamFieldElevation:
    case kParamAirRadiation:
        return projectionPanelRect();
    case kParamFieldListenMode:
    case kParamFieldListenAmount:
    case kParamFieldListenResponse:
        return listenerPanelRect();
    case kParamExcitation:
    case kParamEventRate:
    case kParamActivity:
    case kParamForce:
    case kParamTexture:
    case kParamAmbientDrive:
    case kParamExternalDrive:
        return excitationPanelRect();
    case kParamSubstrate:
    case kParamSize:
    case kParamDamping:
    case kParamIrregularity:
    case kParamCoupling:
    case kParamEnergy:
    case kParamPropagationLoss:
    case kParamContactDetail:
    case kParamSourcePosition:
        return structurePanelRect();
    case kParamPickupPosition:
    case kParamArraySpread:
    case kParamPickupAxis:
        return radiationPanelRect();
    default:
        return outputPanelRect();
    }
}

CGFloat controlRowY(NSRect panel, uint32_t row)
{
    return panel.origin.y + 36.0 + static_cast<CGFloat>(row) * 26.0;
}

const char* shortParamName(clap_id id)
{
    switch (id) {
    case kParamExcitation: return "EXCITER";
    case kParamEventRate: return "RATE";
    case kParamActivity: return "ACTIVE";
    case kParamForce: return "FORCE";
    case kParamTexture: return "TEXTURE";
    case kParamAmbientDrive: return "AMBIENT";
    case kParamExternalDrive: return "FORCE IN";
    case kParamSubstrate: return "BODY";
    case kParamSize: return "SIZE";
    case kParamDamping: return "DAMP";
    case kParamIrregularity: return "IRREG";
    case kParamCoupling: return "COUPLING";
    case kParamEnergy: return "ENERGY";
    case kParamPropagationLoss: return "LOSS";
    case kParamContactDetail: return "DETAIL";
    case kParamSourcePosition: return "STRIKE POS";
    case kParamReadout: return "READOUT";
    case kParamPickupPosition: return "CENTER";
    case kParamArraySpread: return "SPREAD";
    case kParamPickupAxis: return "MODE ANGLE";
    case kParamSensorMass: return "MASS";
    case kParamMountStiffness: return "MOUNT";
    case kParamConditionerHighpass: return "HPF";
    case kParamSensorNoise: return "NOISE";
    case kParamAirRadiation: return "RADIATE";
    case kParamOutputMode: return "MODE";
    case kParamOrder: return "ORDER";
    case kParamContactRadiation: return "C / RAD";
    case kParamSpatialExtent: return "AED SPREAD";
    case kParamFieldAzimuth: return "AZIM";
    case kParamFieldElevation: return "ELEV";
    case kParamOutputGain: return "GAIN";
    case kParamFieldListenMode: return "LISTEN";
    case kParamFieldListenAmount: return "INFLUENCE";
    case kParamFieldListenResponse: return "RESPONSE";
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
bool findInGroup(NSPoint point, NSRect panel,
    const std::array<clap_id, Count>& controls,
    GuiControlLocation& result)
{
    const CGFloat controlX = static_cast<CGFloat>(
        s3g::gui_layout::processorControlX(panel.origin.x));
    const CGFloat width = static_cast<CGFloat>(
        s3g::gui_layout::processorMenuWidth(panel.size.width));
    for (uint32_t row = 0u; row < Count; ++row) {
        const NSRect hit = NSMakeRect(
            controlX - 6.0, controlRowY(panel, row) - 5.0,
            width + 12.0, 24.0);
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
    const uint32_t order = plugin.params.ambisonicOrder;
    const float outputGain = plugin.params.outputGainDb;
    const auto listenMode = plugin.params.fieldListenMode;
    const float listenAmount = plugin.params.fieldListenAmount;
    const auto listenResponse = plugin.params.fieldListenResponse;
    auto params = s3g::accelerometerFieldFactoryPreset(
        arc4random_uniform(kFactoryPresetCount));
    const auto vary = [](float value, float amount) {
        return s3g::clamp(value + guiRandomSigned() * amount, 0.0f, 1.0f);
    };
    params.eventRateHz = s3g::clamp(params.eventRateHz
            * (1.0f + guiRandomSigned() * 0.22f),
        0.04f, 8.0f);
    params.activity = vary(params.activity, 0.10f);
    params.force = vary(params.force, 0.10f);
    params.texture = vary(params.texture, 0.12f);
    params.ambientDrive = vary(params.ambientDrive, 0.06f);
    params.size = vary(params.size, 0.08f);
    params.damping = vary(params.damping, 0.08f);
    params.irregularity = vary(params.irregularity, 0.07f);
    params.coupling = vary(params.coupling, 0.12f);
    params.energy = vary(params.energy, 0.12f);
    params.sourcePosition = vary(params.sourcePosition, 0.10f);
    params.pickupPosition = vary(params.pickupPosition, 0.10f);
    params.arraySpread = s3g::clamp(
        vary(params.arraySpread, 0.10f), 0.42f, 1.0f);
    params.spatialExtent = s3g::clamp(
        vary(params.spatialExtent, 0.10f), 0.36f, 1.0f);
    params.ambisonicOrder = order;
    params.outputGainDb = outputGain;
    params.outputMode = s3g::AccelerometerFieldOutputMode::Ambisonic;
    params.fieldListenMode = listenMode;
    params.fieldListenAmount = listenAmount;
    params.fieldListenResponse = listenResponse;
    params.seed ^= arc4random();
    plugin.params = s3g::sanitizeAccelerometerFieldParams(params);
    plugin.presetIndex = kCustomPresetIndex;
    plugin.engine.setParams(plugin.params);
    plugin.engine.reset();
}

} // namespace

@interface S3GAccelerometerFieldEncoderView : NSView {
    void* _plugin;
    clap_id _dragParam;
    clap_id _openMenu;
    GuiControlLocation _openMenuLocation;
    NSTimer* _refreshTimer;
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
        [self setNeedsDisplay:YES];
    }
}

- (void)setParam:(clap_id)param value:(double)value
{
    applyParam(*static_cast<Plugin*>(_plugin), param, value);
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
    auto* p = static_cast<Plugin*>(_plugin);
    const auto& params = p->params;
    const NSRect panel = fieldPanelRect();
    const NSRect field = fieldPlotRect();
    s3g::clap_gui::drawPanelFrame(
        panel.origin.x, panel.origin.y,
        panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(
        @"GONG MODAL FIELD", true,
        panel.origin.x, panel.origin.y,
        panel.size.width, 21.0, attrs, style);
    [s3g::clap_gui::color(0x0a0a0a) setFill];
    NSRectFill(field);
    [style.grid setStroke];
    NSFrameRect(field);

    const CGFloat left = field.origin.x + 30.0;
    const CGFloat right = NSMaxX(field) - 30.0;
    const CGFloat centerX = (left + right) * 0.5;
    const CGFloat centerY = field.origin.y + field.size.height * 0.46;
    const CGFloat gongRadius = std::min<CGFloat>(
        (right - left) * 0.42, field.size.height * 0.35);
    const bool leafBody = params.substrate
            == s3g::AccelerometerSubstrate::Leaf
        || params.substrate == s3g::AccelerometerSubstrate::DryLeaf;
    const CGFloat bodyHeight = leafBody
        ? 76.0 : 96.0;
    NSBezierPath* body = [NSBezierPath bezierPath];
    if (leafBody) {
        [body moveToPoint:NSMakePoint(left, centerY)];
        [body curveToPoint:NSMakePoint(right, centerY)
            controlPoint1:NSMakePoint(left + 116.0, centerY - bodyHeight)
            controlPoint2:NSMakePoint(right - 116.0, centerY - bodyHeight * 0.72)];
        [body curveToPoint:NSMakePoint(left, centerY)
            controlPoint1:NSMakePoint(right - 116.0, centerY + bodyHeight * 0.72)
            controlPoint2:NSMakePoint(left + 116.0, centerY + bodyHeight)];
    } else if (params.substrate == s3g::AccelerometerSubstrate::BellBronze
        || params.substrate == s3g::AccelerometerSubstrate::GongAgeng
        || params.substrate == s3g::AccelerometerSubstrate::Jing
        || params.substrate == s3g::AccelerometerSubstrate::Kkwaenggwari) {
        [body appendBezierPathWithOvalInRect:NSMakeRect(
            centerX - gongRadius, centerY - gongRadius,
            gongRadius * 2.0, gongRadius * 2.0)];
    } else if (params.substrate
        == s3g::AccelerometerSubstrate::ShellChitin) {
        [body moveToPoint:NSMakePoint(left + 46.0, centerY + 52.0)];
        [body curveToPoint:NSMakePoint(right - 46.0, centerY + 52.0)
            controlPoint1:NSMakePoint(left + 92.0, centerY - 92.0)
            controlPoint2:NSMakePoint(right - 92.0, centerY - 92.0)];
        [body curveToPoint:NSMakePoint(left + 46.0, centerY + 52.0)
            controlPoint1:NSMakePoint(right - 122.0, centerY + 76.0)
            controlPoint2:NSMakePoint(left + 122.0, centerY + 76.0)];
        [body closePath];
    } else if (params.substrate == s3g::AccelerometerSubstrate::Stem
        || params.substrate == s3g::AccelerometerSubstrate::Wire) {
        [body moveToPoint:NSMakePoint(left, centerY + 34.0)];
        [body curveToPoint:NSMakePoint(right, centerY - 30.0)
            controlPoint1:NSMakePoint(left + 145.0, centerY - 54.0)
            controlPoint2:NSMakePoint(right - 145.0, centerY + 52.0)];
        [body setLineWidth:params.substrate == s3g::AccelerometerSubstrate::Stem
                ? 8.0 : 2.0];
    } else if (params.substrate
        == s3g::AccelerometerSubstrate::PolymerMembrane) {
        [body appendBezierPathWithRoundedRect:NSMakeRect(
            left + 16.0, centerY - 66.0,
            right - left - 32.0, 132.0) xRadius:7.0 yRadius:7.0];
    } else if (params.substrate
        == s3g::AccelerometerSubstrate::PaperCardboard) {
        [body appendBezierPathWithRoundedRect:NSMakeRect(
            left + 24.0, centerY - 60.0,
            right - left - 48.0, 120.0) xRadius:4.0 yRadius:4.0];
    } else {
        [body appendBezierPathWithRoundedRect:NSMakeRect(
            left + 28.0, centerY - 58.0,
            right - left - 56.0, 116.0) xRadius:18.0 yRadius:18.0];
    }
    [s3g::clap_gui::color(0x202020, 0.92) setFill];
    if (params.substrate != s3g::AccelerometerSubstrate::Stem
        && params.substrate != s3g::AccelerometerSubstrate::Wire) {
        [body fill];
    }
    [s3g::clap_gui::color(0x797979, 0.82) setStroke];
    [body stroke];

    [s3g::clap_gui::color(0x696969, 0.54) setStroke];
    for (const CGFloat ring : { 0.28, 0.58, 0.82 }) {
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            centerX - gongRadius * ring, centerY - gongRadius * ring,
            gongRadius * ring * 2.0, gongRadius * ring * 2.0)] stroke];
    }
    const CGFloat bossRadius = 24.0 + 18.0 * params.size;
    [s3g::clap_gui::color(0xaaaaaa, 0.46) setStroke];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        centerX - bossRadius, centerY - bossRadius,
        bossRadius * 2.0, bossRadius * 2.0)] stroke];

    const char* bodyName = menuName(kParamSubstrate,
        menuIndexForValue(kParamSubstrate, getParam(*p, kParamSubstrate)));
    [[NSString stringWithFormat:@"%@  /  24 COUPLED MODES",
        [NSString stringWithUTF8String:bodyName]]
        drawAtPoint:NSMakePoint(field.origin.x + 12.0, field.origin.y + 12.0)
        withAttributes:attrs];

    [s3g::clap_gui::color(0x737373, 0.52) setStroke];
    if (params.substrate == s3g::AccelerometerSubstrate::DryLeaf) {
        for (uint32_t vein = 1u; vein < 6u; ++vein) {
            const CGFloat x = left
                + (right - left) * static_cast<CGFloat>(vein) / 6.0;
            const CGFloat reach = 18.0 + 8.0 * (vein & 1u);
            [NSBezierPath strokeLineFromPoint:NSMakePoint(x, centerY)
                toPoint:NSMakePoint(x - 24.0, centerY - reach)];
            [NSBezierPath strokeLineFromPoint:NSMakePoint(x, centerY)
                toPoint:NSMakePoint(x + 20.0, centerY + reach)];
        }
    } else if (params.substrate
        == s3g::AccelerometerSubstrate::PaperCardboard) {
        for (uint32_t layer = 0u; layer < 5u; ++layer) {
            const CGFloat y = centerY - 40.0 + layer * 20.0;
            [NSBezierPath strokeLineFromPoint:NSMakePoint(left + 32.0, y)
                toPoint:NSMakePoint(right - 32.0, y)];
        }
    } else if (params.substrate
        == s3g::AccelerometerSubstrate::PolymerMembrane) {
        NSFrameRectWithWidth(NSMakeRect(left + 28.0, centerY - 54.0,
            right - left - 56.0, 108.0), 1.0);
        [NSBezierPath strokeLineFromPoint:NSMakePoint(left + 28.0, centerY - 54.0)
            toPoint:NSMakePoint(right - 28.0, centerY + 54.0)];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(right - 28.0, centerY - 54.0)
            toPoint:NSMakePoint(left + 28.0, centerY + 54.0)];
    } else if (params.substrate
        == s3g::AccelerometerSubstrate::ShellChitin) {
        for (uint32_t rib = 1u; rib < 5u; ++rib) {
            const CGFloat x = left + 46.0
                + (right - left - 92.0) * static_cast<CGFloat>(rib) / 5.0;
            [NSBezierPath strokeLineFromPoint:NSMakePoint(x, centerY + 50.0)
                toPoint:NSMakePoint(x, centerY - 34.0 - 8.0 * (rib & 1u))];
        }
    }

    [s3g::clap_gui::color(0x5f5f5f, 0.55) setStroke];
    [NSBezierPath strokeLineFromPoint:NSMakePoint(
        centerX - gongRadius, centerY)
        toPoint:NSMakePoint(centerX + gongRadius, centerY)];

    const CGFloat sourceX = centerX
        + (params.sourcePosition - 0.5f) * gongRadius * 1.4;
    NSBezierPath* source = [NSBezierPath bezierPath];
    [source moveToPoint:NSMakePoint(sourceX, centerY - 86.0)];
    [source lineToPoint:NSMakePoint(sourceX - 7.0, centerY - 72.0)];
    [source lineToPoint:NSMakePoint(sourceX + 7.0, centerY - 72.0)];
    [source closePath];
    [s3g::clap_gui::color(0xd0d0d0, 0.90) setFill];
    [source fill];
    [@"STRIKE" drawAtPoint:NSMakePoint(sourceX - 19.0, centerY - 108.0)
        withAttributes:attrs];

    if (params.fieldListenMode != s3g::AmbiFieldListenMode::Off) {
        const CGFloat activity = std::clamp<CGFloat>(
            p->listenerActivity.load(std::memory_order_relaxed), 0.0, 1.0);
        const CGFloat targetPosition = std::clamp<CGFloat>(
            p->listenerTarget.load(std::memory_order_relaxed), 0.0, 1.0);
        const CGFloat targetX = centerX
            + (targetPosition - 0.5) * gongRadius * 1.4;
        const CGFloat targetY = centerY + 86.0;
        const CGFloat radius = 4.0 + activity * 5.0;
        NSBezierPath* listener = [NSBezierPath bezierPath];
        [listener moveToPoint:NSMakePoint(targetX, targetY - radius)];
        [listener lineToPoint:NSMakePoint(targetX + radius, targetY)];
        [listener lineToPoint:NSMakePoint(targetX, targetY + radius)];
        [listener lineToPoint:NSMakePoint(targetX - radius, targetY)];
        [listener closePath];
        [s3g::clap_gui::color(0xd8d8d8,
            0.42 + activity * 0.50) setStroke];
        [listener setLineWidth:1.0 + activity];
        [listener stroke];
        [@"LISTENER" drawAtPoint:NSMakePoint(targetX - 25.0, targetY + 12.0)
            withAttributes:attrs];
    }

    const CGFloat pulse = std::clamp<CGFloat>(
        peak * 7.0f, 0.0f, 1.0f);
    for (uint32_t sensor = 0u;
        sensor < s3g::kAccelerometerFieldSensorCount; ++sensor) {
        const float unit = (static_cast<float>(sensor) + 0.5f)
            / static_cast<float>(s3g::kAccelerometerFieldSensorCount);
        const float position = s3g::accelerometerFieldSensorPosition(
            params, sensor);
        const float axis = s3g::clamp(params.pickupAxis
                + (unit - 0.5f) * 0.72f,
            0.0f, 1.0f);
        const CGFloat bearing = (-145.0 + position * 290.0)
            * s3g::kPi / 180.0;
        const CGFloat x = centerX + std::cos(bearing) * gongRadius * 0.76;
        const CGFloat y = centerY + std::sin(bearing) * gongRadius * 0.76;
        const CGFloat radius = 7.0 + pulse * 8.0
            + params.contactRadiation * 4.0;
        [s3g::clap_gui::color(0xbebebe,
            0.10 + pulse * 0.20) setStroke];
        NSFrameRectWithWidth(NSMakeRect(
            x - radius, y - radius, radius * 2.0, radius * 2.0), 1.0);
        [s3g::clap_gui::color(0xc8c8c8,
            0.50 + pulse * 0.45) setFill];
        NSRectFill(NSMakeRect(x - 4.0, y - 4.0, 8.0, 8.0));
        const CGFloat angle = (-70.0 + axis * 140.0) * s3g::kPi / 180.0;
        [s3g::clap_gui::color(0xe0e0e0, 0.72) setStroke];
        [NSBezierPath strokeLineFromPoint:NSMakePoint(x, y)
            toPoint:NSMakePoint(x + std::cos(angle) * 15.0,
                y - std::sin(angle) * 15.0)];
        [[NSString stringWithFormat:@"%u", sensor + 1u]
            drawAtPoint:NSMakePoint(x - 3.0, y + 10.0)
            withAttributes:attrs];
    }

    NSString* perspective = [NSString stringWithFormat:
        @"CONTACT %3.0f%%     RADIATION %3.0f%%",
        static_cast<double>((1.0f - params.contactRadiation) * 100.0f),
        static_cast<double>(params.contactRadiation * 100.0f)];
    [perspective drawAtPoint:NSMakePoint(
        field.origin.x + 12.0, NSMaxY(field) - 27.0)
        withAttributes:attrs];
    NSString* format = params.outputMode
            == s3g::AccelerometerFieldOutputMode::Ambisonic
        ? [NSString stringWithFormat:@"%uOA  ACN/SN3D",
            params.ambisonicOrder]
        : @"8 DISCRETE SENSOR STEMS";
    [format drawAtPoint:NSMakePoint(
        field.origin.x + 12.0, NSMaxY(field) - 13.0)
        withAttributes:attrs];
}

- (void)drawOpenMenuWithStyle:(s3g::clap_gui::Style&)style
    attrs:(NSDictionary*)attrs
{
    if (_openMenu == CLAP_INVALID_ID) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const uint32_t count = menuCount(_openMenu);
    if (!p || count == 0u || count > 16u) return;
    NSRect anchor = _openMenu == kParamPreset
        ? s3g::clap_gui::cocoaRect(kTitleBand.presetMenu)
        : menuAnchorRect(_openMenuLocation);
    const CGFloat itemHeight = 19.0;
    NSRect menu = NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
        anchor.size.width, itemHeight * count);
    std::array<NSString*, 16u> items {};
    for (uint32_t index = 0u; index < count; ++index) {
        items[index] = [NSString stringWithUTF8String:
            menuName(_openMenu, index)];
    }
    const int selected = static_cast<int>(menuIndexForValue(
        _openMenu, getParam(*p, _openMenu)));
    s3g::clap_gui::drawDropdownMenu(
        menu, itemHeight, items.data(), count,
        selected, -1, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    auto* p = static_cast<Plugin*>(_plugin);
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
    const auto drawPanel = [&](NSString* title, NSRect panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.origin.x, panel.origin.y,
            panel.size.width, panel.size.height, style);
        s3g::clap_gui::drawPanelHeader(title, true,
            panel.origin.x, panel.origin.y,
            panel.size.width, 21.0, labels, style);
    };
    drawPanel(@"OUTPUT", outputPanelRect());
    drawPanel(@"EXCITER", excitationPanelRect());
    drawPanel(@"MODAL BODY", structurePanelRect());
    drawPanel(@"PROJECTION", projectionPanelRect());
    drawPanel(@"RADIATION ARRAY", radiationPanelRect());
    drawPanel(@"LISTENER", listenerPanelRect());
    drawPanel(@"SIGNAL PATH", signalPanelRect());

    for (uint32_t row = 0u; row < kOutputControls.size(); ++row) {
        [self drawControl:kOutputControls[row]
            panel:outputPanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    for (uint32_t row = 0u; row < kExcitationControls.size(); ++row) {
        [self drawControl:kExcitationControls[row]
            panel:excitationPanelRect() row:row
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
    for (uint32_t row = 0u; row < kListenerControls.size(); ++row) {
        [self drawControl:kListenerControls[row]
            panel:listenerPanelRect() row:row
            labelAttrs:labels valueAttrs:values style:style];
    }
    const NSRect signal = signalPanelRect();
    const char* bodyName = menuName(kParamSubstrate,
        menuIndexForValue(kParamSubstrate, getParam(*p, kParamSubstrate)));
    [[NSString stringWithFormat:@"%@ / 24 MODES",
        [NSString stringWithUTF8String:bodyName]]
        drawAtPoint:NSMakePoint(signal.origin.x + 12.0, signal.origin.y + 38.0)
        withAttributes:values];
    [@"EXCITER" drawAtPoint:NSMakePoint(
        signal.origin.x + 12.0, signal.origin.y + 67.0)
        withAttributes:labels];
    [@"↓" drawAtPoint:NSMakePoint(
        signal.origin.x + 15.0, signal.origin.y + 83.0)
        withAttributes:values];
    [@"COUPLED MODAL BODY" drawAtPoint:NSMakePoint(
        signal.origin.x + 12.0, signal.origin.y + 104.0)
        withAttributes:values];
    [@"↓  8 RADIATION POINTS" drawAtPoint:NSMakePoint(
        signal.origin.x + 12.0, signal.origin.y + 130.0)
        withAttributes:values];
    [@"ACN / SN3D FIELD" drawAtPoint:NSMakePoint(
        signal.origin.x + 12.0, signal.origin.y + 158.0)
        withAttributes:values];
    [self drawOpenMenuWithStyle:style attrs:values];
}

- (void)updateDraggedParamAtPoint:(NSPoint)point
{
    const auto* spec = paramSpec(_dragParam);
    if (!spec || spec->display == DisplayKind::Menu) return;
    GuiControlLocation location {};
    const bool found = findInGroup(point, outputPanelRect(),
            kOutputControls, location)
        || findInGroup(point, excitationPanelRect(),
            kExcitationControls, location)
        || findInGroup(point, structurePanelRect(),
            kStructureControls, location)
        || findInGroup(point, projectionPanelRect(),
            kProjectionControls, location)
        || findInGroup(point, radiationPanelRect(),
            kRadiationControls, location)
        || findInGroup(point, listenerPanelRect(),
            kListenerControls, location);
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
        const NSRect menu = NSMakeRect(anchor.origin.x,
            NSMaxY(anchor) + 2.0, anchor.size.width,
            itemHeight * count);
        const int selected = s3g::clap_gui::dropdownHitIndex(
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
            kTitleBand, p->presetName, sizeof(p->presetName))) {
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

    GuiControlLocation location {};
    if (!findInGroup(point, outputPanelRect(), kOutputControls, location)
        && !findInGroup(point, excitationPanelRect(), kExcitationControls, location)
        && !findInGroup(point, structurePanelRect(), kStructureControls, location)
        && !findInGroup(point, projectionPanelRect(), kProjectionControls, location)
        && !findInGroup(point, radiationPanelRect(), kRadiationControls, location)
        && !findInGroup(point, listenerPanelRect(), kListenerControls, location)) {
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
    if (_dragParam == CLAP_INVALID_ID) return;
    const NSPoint point = [self convertPoint:[event locationInWindow]
        fromView:nil];
    [self updateDraggedParamAtPoint:point];
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
    "0.5.0",
    "Gong-focused modal percussion for Gong Ageng, jing, kkwaenggwari, and bronze bodies with coupled nonlinear evolution and third-order ACN/SN3D encoding.",
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
    p->engine.setParams(p->params);
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
