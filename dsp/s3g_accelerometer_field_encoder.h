#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAccelerometerFieldModeCount = 24u;
constexpr uint32_t kAccelerometerFieldPresetCount = 13u;
constexpr uint32_t kAccelerometerFieldSensorCount = 8u;
constexpr uint32_t kAccelerometerFieldMaxOrder = 3u;
constexpr uint32_t kAccelerometerFieldMaxChannels = 16u;
constexpr float kAccelerometerFieldTwoPi = 6.28318530717958647692f;
constexpr uint32_t kAccelerometerFieldDefaultSeed = 0x6d2b79f5u;

enum class AccelerometerFieldOutputMode : uint32_t {
    Ambisonic = 0u,
    SensorStems,
    Count,
};

enum class AccelerometerSubstrate : uint32_t {
    Leaf = 0u,
    Stem,
    BellBronze,
    Wood,
    Glass,
    Wire,
    DryLeaf,
    PaperCardboard,
    ShellChitin,
    PolymerMembrane,
    GongAgeng,
    Jing,
    Kkwaenggwari,
    Count,
};

enum class AccelerometerExcitation : uint32_t {
    Ambient = 0u,
    Footsteps,
    Chewing,
    Scrape,
    Tremulation,
    Tap,
    Count,
};

enum class AccelerometerReadout : uint32_t {
    Acceleration = 0u,
    Velocity,
    Displacement,
    Count,
};

struct AccelerometerFieldParams {
    AccelerometerSubstrate substrate = AccelerometerSubstrate::GongAgeng;
    AccelerometerExcitation excitation = AccelerometerExcitation::Tap;
    AccelerometerReadout readout = AccelerometerReadout::Acceleration;

    float eventRateHz = 2.4f;
    float activity = 0.68f;
    float force = 0.48f;
    float texture = 0.52f;
    float ambientDrive = 0.12f;
    float externalDrive = 0.0f;

    float size = 0.55f;
    float damping = 0.52f;
    float irregularity = 0.18f;
    float propagationLoss = 0.38f;
    float contactDetail = 0.18f;

    float sourcePosition = 0.72f;
    float pickupPosition = 0.28f;
    float arraySpread = 0.82f;
    float pickupAxis = 0.25f;

    float sensorMass = 0.18f;
    float mountStiffness = 0.68f;
    float conditionerHighpassHz = 4.0f;
    float sensorNoise = 0.025f;

    float airRadiation = 0.18f;
    uint32_t ambisonicOrder = 3u;
    AccelerometerFieldOutputMode outputMode =
        AccelerometerFieldOutputMode::Ambisonic;
    float contactRadiation = 0.32f;
    float spatialExtent = 0.72f;
    float fieldAzimuthDeg = 0.0f;
    float fieldElevationDeg = 0.0f;
    float outputGainDb = -9.0f;
    uint32_t seed = kAccelerometerFieldDefaultSeed;

    AmbiFieldListenMode fieldListenMode = AmbiFieldListenMode::Off;
    float fieldListenAmount = 0.62f;
    AmbiFieldListenerResponse fieldListenResponse =
        AmbiFieldListenerResponse::Excite;

    // Coupling animates close modal families with synchronized AM/FM. Energy
    // approximates the bounded pitch relaxation and delayed high-mode buildup
    // of a nonlinear plate without placing an FDTD solver in the audio path.
    float coupling = 0.28f;
    float energy = 0.24f;
};

struct AccelerometerFieldPresetInfo {
    const char* name = "";
    const char* description = "";
};

inline AccelerometerFieldParams sanitizeAccelerometerFieldParams(
    AccelerometerFieldParams params)
{
    const auto finite = [](float value, float fallback, float low, float high) {
        return clamp(std::isfinite(value) ? value : fallback, low, high);
    };
    params.substrate = static_cast<AccelerometerSubstrate>(std::min<uint32_t>(
        static_cast<uint32_t>(params.substrate),
        static_cast<uint32_t>(AccelerometerSubstrate::Count) - 1u));
    params.excitation = static_cast<AccelerometerExcitation>(std::min<uint32_t>(
        static_cast<uint32_t>(params.excitation),
        static_cast<uint32_t>(AccelerometerExcitation::Count) - 1u));
    params.readout = static_cast<AccelerometerReadout>(std::min<uint32_t>(
        static_cast<uint32_t>(params.readout),
        static_cast<uint32_t>(AccelerometerReadout::Count) - 1u));
    params.eventRateHz = finite(params.eventRateHz, 2.4f, 0.01f, 80.0f);
    params.activity = finite(params.activity, 0.68f, 0.0f, 1.0f);
    params.force = finite(params.force, 0.48f, 0.0f, 1.0f);
    params.texture = finite(params.texture, 0.52f, 0.0f, 1.0f);
    params.ambientDrive = finite(params.ambientDrive, 0.12f, 0.0f, 1.0f);
    params.externalDrive = finite(params.externalDrive, 0.0f, 0.0f, 1.0f);
    params.size = finite(params.size, 0.55f, 0.0f, 1.0f);
    params.damping = finite(params.damping, 0.52f, 0.0f, 1.0f);
    params.irregularity = finite(params.irregularity, 0.18f, 0.0f, 1.0f);
    params.propagationLoss = finite(params.propagationLoss, 0.38f, 0.0f, 1.0f);
    params.contactDetail = finite(params.contactDetail, 0.18f, 0.0f, 1.0f);
    params.sourcePosition = finite(params.sourcePosition, 0.72f, 0.0f, 1.0f);
    params.pickupPosition = finite(params.pickupPosition, 0.28f, 0.0f, 1.0f);
    params.arraySpread = finite(params.arraySpread, 0.82f, 0.0f, 1.0f);
    params.pickupAxis = finite(params.pickupAxis, 0.25f, 0.0f, 1.0f);
    params.sensorMass = finite(params.sensorMass, 0.18f, 0.0f, 1.0f);
    params.mountStiffness = finite(params.mountStiffness, 0.68f, 0.0f, 1.0f);
    params.conditionerHighpassHz = finite(
        params.conditionerHighpassHz, 4.0f, 0.25f, 180.0f);
    params.sensorNoise = finite(params.sensorNoise, 0.025f, 0.0f, 1.0f);
    params.airRadiation = finite(params.airRadiation, 0.18f, 0.0f, 1.0f);
    params.ambisonicOrder = std::clamp<uint32_t>(
        params.ambisonicOrder, 1u, kAccelerometerFieldMaxOrder);
    params.outputMode = static_cast<AccelerometerFieldOutputMode>(
        std::min<uint32_t>(static_cast<uint32_t>(params.outputMode),
            static_cast<uint32_t>(AccelerometerFieldOutputMode::Count) - 1u));
    params.contactRadiation = finite(
        params.contactRadiation, 0.32f, 0.0f, 1.0f);
    params.spatialExtent = finite(params.spatialExtent, 0.72f, 0.0f, 1.0f);
    params.fieldAzimuthDeg = finite(
        params.fieldAzimuthDeg, 0.0f, -180.0f, 180.0f);
    params.fieldElevationDeg = finite(
        params.fieldElevationDeg, 0.0f, -90.0f, 90.0f);
    params.outputGainDb = finite(params.outputGainDb, -9.0f, -60.0f, 12.0f);
    if (params.seed == 0u) params.seed = kAccelerometerFieldDefaultSeed;
    params.fieldListenMode = sanitizeAmbiFieldListenMode(
        params.fieldListenMode);
    params.fieldListenAmount = finite(
        params.fieldListenAmount, 0.62f, 0.0f, 1.0f);
    params.fieldListenResponse = sanitizeAmbiFieldListenerResponse(
        params.fieldListenResponse);
    params.coupling = finite(params.coupling, 0.28f, 0.0f, 1.0f);
    params.energy = finite(params.energy, 0.24f, 0.0f, 1.0f);
    return params;
}

inline float accelerometerFieldSensorPosition(
    const AccelerometerFieldParams& params, uint32_t sensor)
{
    sensor = std::min<uint32_t>(
        sensor, kAccelerometerFieldSensorCount - 1u);
    const float unit = (static_cast<float>(sensor) + 0.5f)
        / static_cast<float>(kAccelerometerFieldSensorCount);
    const float spread = clamp(params.arraySpread, 0.0f, 1.0f);
    constexpr float normalizedSpan = 0.92f;
    constexpr float outerUnit = 0.5f
        - 0.5f / static_cast<float>(kAccelerometerFieldSensorCount);
    const float halfSpan = normalizedSpan * outerUnit * spread;
    const float center = lerp(0.02f + halfSpan, 0.98f - halfSpan,
        clamp(params.pickupPosition, 0.0f, 1.0f));
    return clamp(center + (unit - 0.5f) * normalizedSpan * spread,
        0.02f, 0.98f);
}

inline const AccelerometerFieldPresetInfo&
accelerometerFieldFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<AccelerometerFieldPresetInfo,
        kAccelerometerFieldPresetCount> info {{
        { "Gong Ageng", "Coupled rumble and ding clusters unfold across a great gong." },
        { "Silent Bronze", "Ambient energy discloses long bronze modes." },
        { "Temple Bronze", "Measured strikes wake a poised bronze bell field." },
        { "Double Mallet", "Paired mallet contacts cross the gong face." },
        { "Mallet Roll", "Dense articulated strokes accumulate into a bronze cloud." },
        { "Bowed Rim", "Continuous rim friction sustains shifting modal families." },
        { "Kkwaenggwari Lead", "A small Korean hand gong cuts an agile rhythmic line." },
        { "Bronze Scrape", "Granular friction travels across the gong surface." },
        { "Bright Boss", "Hard boss strikes launch a focused upper bloom." },
        { "Muted Bloom", "Soft paired strokes rise through a damped field." },
        { "Jing Wind", "A large Korean gong bends and beats around slow strokes." },
        { "Kkwaenggwari Chae", "Bright stick attacks articulate a compact hand gong." },
        { "Jing Ceremony", "Widely spaced jing strokes breathe through the full field." },
    }};
    return info[std::min<uint32_t>(
        index, kAccelerometerFieldPresetCount - 1u)];
}

inline AccelerometerFieldParams accelerometerFieldFactoryPreset(
    uint32_t index)
{
    const uint32_t preset = std::min<uint32_t>(
        index, kAccelerometerFieldPresetCount - 1u);
    AccelerometerFieldParams params;
    params.substrate = AccelerometerSubstrate::GongAgeng;
    params.excitation = AccelerometerExcitation::Tap;
    params.readout = AccelerometerReadout::Acceleration;
    params.eventRateHz = 0.18f;
    params.activity = 0.78f;
    params.force = 0.62f;
    params.texture = 0.12f;
    params.ambientDrive = 0.012f;
    params.externalDrive = 0.0f;
    params.size = 0.50f;
    params.damping = 0.08f;
    params.irregularity = 0.025f;
    params.propagationLoss = 0.16f;
    params.contactDetail = 0.055f;
    params.sourcePosition = 0.43f;
    params.pickupPosition = 0.50f;
    params.arraySpread = 0.94f;
    params.pickupAxis = 0.34f;
    params.sensorMass = 0.01f;
    params.mountStiffness = 0.90f;
    params.conditionerHighpassHz = 0.50f;
    params.sensorNoise = 0.005f;
    params.airRadiation = 0.24f;
    params.ambisonicOrder = 3u;
    params.outputMode = AccelerometerFieldOutputMode::Ambisonic;
    params.contactRadiation = 0.52f;
    params.spatialExtent = 0.90f;
    params.fieldAzimuthDeg = 0.0f;
    params.fieldElevationDeg = 0.0f;
    params.outputGainDb = -11.0f;
    params.seed = kAccelerometerFieldDefaultSeed + preset * 0x9e3779b9u;
    params.fieldListenMode = AmbiFieldListenMode::Off;
    params.fieldListenAmount = 0.62f;
    params.fieldListenResponse = AmbiFieldListenerResponse::Excite;
    params.coupling = 0.72f;
    params.energy = 0.64f;

    switch (preset) {
    case 0u: // Gong Ageng reference
        break;
    case 1u: // Silent Bronze
        params.substrate = AccelerometerSubstrate::BellBronze;
        params.excitation = AccelerometerExcitation::Ambient;
        params.eventRateHz = 0.11f;
        params.activity = 0.34f;
        params.force = 0.22f;
        params.texture = 0.30f;
        params.ambientDrive = 0.42f;
        params.size = 0.82f;
        params.damping = 0.12f;
        params.irregularity = 0.10f;
        params.contactDetail = 0.015f;
        params.sourcePosition = 0.12f;
        params.pickupPosition = 0.63f;
        params.pickupAxis = 0.18f;
        params.airRadiation = 0.08f;
        params.outputGainDb = -8.0f;
        params.coupling = 0.54f;
        params.energy = 0.18f;
        break;
    case 2u: // Temple Bronze
        params.substrate = AccelerometerSubstrate::BellBronze;
        params.eventRateHz = 0.42f;
        params.activity = 0.72f;
        params.force = 0.46f;
        params.texture = 0.58f;
        params.ambientDrive = 0.12f;
        params.size = 0.68f;
        params.damping = 0.18f;
        params.irregularity = 0.16f;
        params.sourcePosition = 0.82f;
        params.pickupPosition = 0.24f;
        params.pickupAxis = 0.74f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.46f;
        params.energy = 0.40f;
        break;
    case 3u: // Double Mallet
        params.excitation = AccelerometerExcitation::Footsteps;
        params.eventRateHz = 1.35f;
        params.activity = 0.74f;
        params.force = 0.52f;
        params.texture = 0.34f;
        params.ambientDrive = 0.04f;
        params.sourcePosition = 0.72f;
        params.pickupPosition = 0.30f;
        params.arraySpread = 0.78f;
        params.outputGainDb = -13.0f;
        params.coupling = 0.68f;
        params.energy = 0.54f;
        break;
    case 4u: // Mallet Roll
        params.excitation = AccelerometerExcitation::Chewing;
        params.eventRateHz = 4.4f;
        params.activity = 0.86f;
        params.force = 0.34f;
        params.texture = 0.76f;
        params.ambientDrive = 0.025f;
        params.damping = 0.22f;
        params.irregularity = 0.18f;
        params.contactDetail = 0.48f;
        params.sourcePosition = 0.66f;
        params.pickupPosition = 0.38f;
        params.outputGainDb = -15.0f;
        params.coupling = 0.58f;
        params.energy = 0.42f;
        break;
    case 5u: // Bowed Rim
        params.excitation = AccelerometerExcitation::Tremulation;
        params.eventRateHz = 0.62f;
        params.activity = 0.82f;
        params.force = 0.44f;
        params.texture = 0.28f;
        params.ambientDrive = 0.08f;
        params.size = 0.60f;
        params.damping = 0.28f;
        params.irregularity = 0.12f;
        params.contactDetail = 0.18f;
        params.sourcePosition = 0.92f;
        params.pickupAxis = 0.88f;
        params.contactRadiation = 0.38f;
        params.outputGainDb = -13.0f;
        params.coupling = 0.82f;
        params.energy = 0.30f;
        break;
    case 6u: // Kkwaenggwari Lead
        params.substrate = AccelerometerSubstrate::Kkwaenggwari;
        params.eventRateHz = 0.84f;
        params.activity = 0.68f;
        params.force = 0.40f;
        params.texture = 0.68f;
        params.ambientDrive = 0.05f;
        params.size = 0.40f;
        params.damping = 0.26f;
        params.irregularity = 0.14f;
        params.sourcePosition = 0.48f;
        params.pickupPosition = 0.76f;
        params.spatialExtent = 0.68f;
        params.outputGainDb = -6.0f;
        params.coupling = 0.42f;
        params.energy = 0.30f;
        break;
    case 7u: // Bronze Scrape
        params.substrate = AccelerometerSubstrate::BellBronze;
        params.excitation = AccelerometerExcitation::Scrape;
        params.eventRateHz = 0.48f;
        params.activity = 0.76f;
        params.force = 0.32f;
        params.texture = 0.80f;
        params.ambientDrive = 0.06f;
        params.size = 0.72f;
        params.damping = 0.34f;
        params.irregularity = 0.26f;
        params.propagationLoss = 0.32f;
        params.contactDetail = 0.72f;
        params.sourcePosition = 0.16f;
        params.pickupPosition = 0.62f;
        params.contactRadiation = 0.28f;
        params.outputGainDb = -15.0f;
        params.coupling = 0.44f;
        params.energy = 0.22f;
        break;
    case 8u: // Bright Boss
        params.eventRateHz = 0.72f;
        params.activity = 0.64f;
        params.force = 0.70f;
        params.texture = 0.82f;
        params.ambientDrive = 0.02f;
        params.size = 0.38f;
        params.damping = 0.12f;
        params.irregularity = 0.08f;
        params.propagationLoss = 0.10f;
        params.contactDetail = 0.20f;
        params.sourcePosition = 0.50f;
        params.pickupPosition = 0.72f;
        params.airRadiation = 0.38f;
        params.outputGainDb = -15.0f;
        params.coupling = 0.64f;
        params.energy = 0.78f;
        break;
    case 9u: // Muted Bloom
        params.excitation = AccelerometerExcitation::Footsteps;
        params.eventRateHz = 0.56f;
        params.activity = 0.62f;
        params.force = 0.38f;
        params.texture = 0.22f;
        params.ambientDrive = 0.05f;
        params.size = 0.68f;
        params.damping = 0.58f;
        params.irregularity = 0.10f;
        params.propagationLoss = 0.46f;
        params.sourcePosition = 0.36f;
        params.pickupPosition = 0.58f;
        params.contactRadiation = 0.62f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.52f;
        params.energy = 0.56f;
        break;
    case 10u: // Jing Wind
        params.substrate = AccelerometerSubstrate::Jing;
        params.excitation = AccelerometerExcitation::Scrape;
        params.eventRateHz = 0.20f;
        params.activity = 0.84f;
        params.force = 0.30f;
        params.texture = 0.36f;
        params.ambientDrive = 0.16f;
        params.size = 0.86f;
        params.damping = 0.36f;
        params.irregularity = 0.20f;
        params.propagationLoss = 0.54f;
        params.contactDetail = 0.46f;
        params.sourcePosition = 0.10f;
        params.pickupPosition = 0.52f;
        params.airRadiation = 0.12f;
        params.contactRadiation = 0.22f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.86f;
        params.energy = 0.24f;
        break;
    case 11u: // Kkwaenggwari Chae
        params.substrate = AccelerometerSubstrate::Kkwaenggwari;
        params.eventRateHz = 1.10f;
        params.activity = 0.72f;
        params.force = 0.48f;
        params.texture = 0.62f;
        params.ambientDrive = 0.04f;
        params.size = 0.32f;
        params.damping = 0.24f;
        params.irregularity = 0.16f;
        params.sourcePosition = 0.54f;
        params.pickupPosition = 0.24f;
        params.arraySpread = 0.72f;
        params.spatialExtent = 0.78f;
        params.outputGainDb = -8.0f;
        params.coupling = 0.60f;
        params.energy = 0.46f;
        break;
    case 12u: // Jing Ceremony
    default:
        params.substrate = AccelerometerSubstrate::Jing;
        params.excitation = AccelerometerExcitation::Footsteps;
        params.eventRateHz = 0.16f;
        params.activity = 0.66f;
        params.force = 0.64f;
        params.texture = 0.16f;
        params.ambientDrive = 0.10f;
        params.size = 0.76f;
        params.damping = 0.10f;
        params.irregularity = 0.05f;
        params.sourcePosition = 0.40f;
        params.pickupPosition = 0.56f;
        params.pickupAxis = 0.44f;
        params.outputGainDb = -13.0f;
        params.coupling = 0.76f;
        params.energy = 0.68f;
        break;
    }
    return sanitizeAccelerometerFieldParams(params);
}

class AccelerometerFieldEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        geometrySmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.035f * static_cast<float>(sampleRate_)));
        params_ = sanitizeAccelerometerFieldParams(params_);
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.42f);
        fieldListener_.setExtendedAnalysisEnabled(true);
        rebuildModel();
        reset();
    }

    void reset()
    {
        const bool restoreReferencePitch = performancePitchRatio_ != 1.0f;
        performancePitchRatio_ = 1.0f;
        if (restoreReferencePitch && sampleRate_ > 0.0) rebuildModel();
        rngState_ = params_.seed;
        eventPosition_ = params_.sourcePosition;
        eventPhase_ = 0.92f;
        eventEnvelope_ = 0.0f;
        eventDecay_ = 0.0f;
        eventTonePhase_ = 0.0f;
        chewingEventAge_ = 0u;
        chewingEventDuration_ = 1u;
        chewingGrainPhase_ = 0.0f;
        chewingGrainEnvelope_ = 0.0f;
        chewingNoiseLow_ = 0.0f;
        chewingNoiseMid_ = 0.0f;
        secondaryCountdown_ = 0u;
        chewingPauseCountdown_ = 0u;
        chewingClosureCount_ = 0u;
        chewingClosureTarget_ = 3u + randomU32() % 4u;
        ambientSlow_ = 0.0f;
        ambientFast_ = 0.0f;
        eventTexture_ = params_.texture;
        listenerTargetPosition_ = params_.sourcePosition;
        couplingSlowPhase_ = 0.0f;
        couplingFastPhase_ = 1.57079632679489661923f;
        couplingSlowValue_ = 0.0f;
        couplingFastValue_ = 1.0f;
        smoothedCoupling_ = params_.coupling;
        smoothedEnergy_ = params_.energy;
        impactAgeSeconds_ = 8.0f;
        impactStrength_ = 0.0f;
        currentPitchScale_ = 1.0f;
        currentCascadeLevel_ = 0.0f;
        externalTransientEnvelope_ = 0.0f;
        midiStrikeEnvelope_ = 0.0f;
        midiStrikeDecay_ = 0.0f;
        midiStrikeTonePhase_ = 0.0f;
        dynamicCoefficientCountdown_ = 0u;
        fieldListener_.reset();
        for (auto& sensor : sensors_) {
            sensor.reset();
            sensor.snapGeometry();
        }
        for (auto& mode : modes_) {
            mode.reset();
            mode.snapGeometry();
        }
    }

    void setParams(AccelerometerFieldParams params)
    {
        const AccelerometerFieldParams sanitized =
            sanitizeAccelerometerFieldParams(params);
        const bool rebuild = params_.substrate != sanitized.substrate
            || params_.size != sanitized.size
            || params_.damping != sanitized.damping
            || params_.irregularity != sanitized.irregularity
            || params_.propagationLoss != sanitized.propagationLoss
            || params_.sourcePosition != sanitized.sourcePosition
            || params_.pickupPosition != sanitized.pickupPosition
            || params_.arraySpread != sanitized.arraySpread
            || params_.pickupAxis != sanitized.pickupAxis
            || params_.sensorMass != sanitized.sensorMass
            || params_.spatialExtent != sanitized.spatialExtent
            || params_.fieldAzimuthDeg != sanitized.fieldAzimuthDeg
            || params_.fieldElevationDeg != sanitized.fieldElevationDeg
            || params_.seed != sanitized.seed;
        params_ = sanitized;
        if (sampleRate_ > 0.0 && rebuild) rebuildModel();
    }

    AccelerometerFieldParams params() const { return params_; }

    uint32_t outputChannelCount() const
    {
        return params_.outputMode == AccelerometerFieldOutputMode::SensorStems
            ? kAccelerometerFieldSensorCount
            : (params_.ambisonicOrder + 1u) * (params_.ambisonicOrder + 1u);
    }

    // MIDI is a second physical-force inlet: velocity sets impact energy and
    // the most recent note transposes the one shared gong body. C4 (note 60)
    // is the measured/reference body pitch; note-off deliberately does not
    // damp a freely ringing metal plate.
    void strikeMidi(int32_t note, float velocity)
    {
        if (sampleRate_ <= 0.0) prepare(48000.0);
        velocity = clamp(std::isfinite(velocity) ? velocity : 0.0f,
            0.0f, 1.0f);
        if (velocity <= 0.0f) return;
        const float pitchRatio = clamp(std::exp2(
            (static_cast<float>(std::clamp(note, 0, 127)) - 60.0f) / 12.0f),
            0.125f, 8.0f);
        if (std::fabs(pitchRatio - performancePitchRatio_) > 1.0e-6f) {
            performancePitchRatio_ = pitchRatio;
            rebuildModel();
        }
        const float shapedVelocity = std::pow(velocity, 0.72f);
        const float strength = shapedVelocity
            * (0.30f + 0.70f * params_.force);
        midiStrikeEnvelope_ = std::max(midiStrikeEnvelope_, strength);
        const float contactSeconds = 0.0025f
            + 0.010f * (1.0f - params_.texture);
        midiStrikeDecay_ = std::exp(-1.0f / std::max(
            1.0f, contactSeconds * static_cast<float>(sampleRate_)));
        midiStrikeTonePhase_ = 0.0f;
        eventPosition_ = params_.sourcePosition;
        eventTexture_ = params_.texture;
        triggerNonlinearImpact(strength);
    }

    float performancePitchRatio() const { return performancePitchRatio_; }

    // The default output is ACN/SN3D. SensorStems deliberately bypasses the
    // spherical-harmonic projection and places one conditioned virtual
    // accelerometer on each of the first eight channels.
    void process(const float* externalExcitation, float* const* outputs,
        uint32_t outputChannels, uint32_t frames)
    {
        if (!outputs || frames == 0u) return;
        if (sampleRate_ <= 0.0) prepare(48000.0);
        outputChannels = std::min<uint32_t>(
            outputChannels, kAccelerometerFieldMaxChannels);
        for (uint32_t channel = 0u; channel < outputChannels; ++channel) {
            if (outputs[channel]) {
                std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
            }
        }

        const float outputGain = std::pow(10.0f, params_.outputGainDb / 20.0f);
        const float mountCutoff = std::min(
            static_cast<float>(sampleRate_) * 0.44f,
            280.0f * std::exp2(params_.mountStiffness * 6.0f));
        const float mountCoefficient = 1.0f - std::exp(
            -kAccelerometerFieldTwoPi * mountCutoff
            / static_cast<float>(sampleRate_));
        const float highpassPole = std::exp(
            -kAccelerometerFieldTwoPi * params_.conditionerHighpassHz
            / static_cast<float>(sampleRate_));
        const float airCoefficient = 1.0f - std::exp(
            -kAccelerometerFieldTwoPi * 17500.0f
            / static_cast<float>(sampleRate_));
        const float directHighpassPole = std::exp(
            -kAccelerometerFieldTwoPi * 48.0f
            / static_cast<float>(sampleRate_));
        const uint32_t activeChannels = std::min<uint32_t>(
            outputChannelCount(), outputChannels);
        const bool stems = params_.outputMode
            == AccelerometerFieldOutputMode::SensorStems;
        const float substrateModalCoupling =
            substrateProfile(params_.substrate).modalCoupling;
        constexpr float sensorSumScale =
            1.0f / static_cast<float>(kAccelerometerFieldSensorCount);
        const uint32_t listenerChannels =
            (params_.ambisonicOrder + 1u) * (params_.ambisonicOrder + 1u);

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            smoothGeometry();
            float excitation = proceduralExcitation();
            float externalForce = 0.0f;
            if (externalExcitation) {
                const float external = std::isfinite(externalExcitation[frame])
                    ? externalExcitation[frame] : 0.0f;
                externalForce = external * params_.externalDrive;
                excitation += externalForce;
            }
            float midiForce = 0.0f;
            if (midiStrikeEnvelope_ > 1.0e-7f) {
                const float strikeToneHz = params_.substrate
                        == AccelerometerSubstrate::Kkwaenggwari
                    ? 2200.0f : (params_.substrate
                            == AccelerometerSubstrate::Jing
                        ? 420.0f : 900.0f);
                midiStrikeTonePhase_ += strikeToneHz
                    / static_cast<float>(sampleRate_);
                midiStrikeTonePhase_ -= std::floor(midiStrikeTonePhase_);
                const float contactTone = std::sin(
                    kAccelerometerFieldTwoPi * midiStrikeTonePhase_);
                midiForce = midiStrikeEnvelope_
                    * (0.78f + 0.12f * contactTone
                        + 0.10f * params_.texture * randomSigned());
                midiStrikeEnvelope_ = flushDenormal(
                    midiStrikeEnvelope_ * midiStrikeDecay_);
                excitation += midiForce;
            }
            detectExternalImpact(externalForce + midiForce);
            advanceModalEvolution();
            if (dynamicCoefficientCountdown_ == 0u) {
                updateDynamicCoefficients();
                dynamicCoefficientCountdown_ = 15u;
            } else {
                --dynamicCoefficientCountdown_;
            }

            std::array<float, kAccelerometerFieldSensorCount>
                localDisplacement {};
            std::array<float, kAccelerometerFieldSensorCount>
                localVelocity {};
            std::array<float, kAccelerometerFieldSensorCount>
                localAcceleration {};
            std::array<float, kAccelerometerFieldSensorCount>
                radiatedAcceleration {};
            std::array<float, kAccelerometerFieldMaxChannels> listenerHoa {};
            // A chewing closure is heard mainly through the travelling contact
            // texture.  Sending much of it into the resonator makes the leaf
            // read as a struck rigid plate, so retain only a trace of body.
            // A compliant leaf converts much less of a local contact into the
            // normalized resonant acceleration used by the rigid profiles.
            // Without this coupling calibration, footsteps pin every virtual
            // sensor against its safety saturator. Chewing retains its still
            // smaller body path so the travelling fibre texture dominates.
            float modalCoupling = substrateModalCoupling;
            if (params_.excitation == AccelerometerExcitation::Chewing) {
                modalCoupling = 0.012f;
            }
            const float modalExcitation = excitation * modalCoupling;
            const float cascadeNoise = currentCascadeLevel_ > 1.0e-7f
                ? randomSigned() * currentCascadeLevel_ * 0.012f : 0.0f;
            for (uint32_t index = 0u;
                index < kAccelerometerFieldModeCount; ++index) {
                auto& mode = modes_[index];
                if (!mode.active) continue;
                ModeSample response = mode.process(modalExcitation
                    + cascadeNoise * mode.cascadeWeight);
                const float modulation = modalCouplingValue(index);
                const float lowModeWeight = 1.0f - mode.pairProgress;
                const float amplitudeDepth = smoothedCoupling_
                    * (0.10f + 0.28f * lowModeWeight);
                const float amplitudeScale = clamp(
                    1.0f - amplitudeDepth * modulation, 0.38f, 1.38f);
                response.displacement *= amplitudeScale;
                response.velocity *= amplitudeScale;
                response.acceleration *= amplitudeScale;
                for (uint32_t sensor = 0u;
                    sensor < kAccelerometerFieldSensorCount; ++sensor) {
                    localDisplacement[sensor] += response.displacement
                        * mode.pickupWeight[sensor];
                    localVelocity[sensor] += response.velocity
                        * mode.pickupWeight[sensor];
                    localAcceleration[sensor] += response.acceleration
                        * mode.pickupWeight[sensor];
                    radiatedAcceleration[sensor] += response.acceleration
                        * mode.radiationWeight[sensor];
                }
            }

            for (uint32_t sensorIndex = 0u;
                sensorIndex < kAccelerometerFieldSensorCount; ++sensorIndex) {
                Sensor& sensor = sensors_[sensorIndex];
                float contact = localAcceleration[sensorIndex];
                if (params_.readout == AccelerometerReadout::Velocity) {
                    contact = localVelocity[sensorIndex];
                } else if (params_.readout
                    == AccelerometerReadout::Displacement) {
                    contact = localDisplacement[sensorIndex];
                }

                const float eventDistance = std::fabs(
                    eventPosition_ - sensor.position);
                const float directTransmission = std::exp(-eventDistance
                    * (0.65f + 2.8f * params_.propagationLoss));
                const float directCutoff = std::min(
                    static_cast<float>(sampleRate_) * 0.44f,
                    1800.0f + 14500.0f * directTransmission);
                const float directCoefficient = 1.0f - std::exp(
                    -kAccelerometerFieldTwoPi * directCutoff
                    / static_cast<float>(sampleRate_));
                sensor.directLowpass += directCoefficient
                    * (excitation - sensor.directLowpass);
                const float direct = sensor.directLowpass
                    - sensor.directHighpassInput
                    + directHighpassPole * sensor.directHighpassOutput;
                sensor.directHighpassInput = sensor.directLowpass;
                sensor.directHighpassOutput = flushDenormal(direct);
                float directDomainScale = params_.excitation
                        == AccelerometerExcitation::Chewing
                    ? 0.66f : 0.34f;
                if (params_.readout == AccelerometerReadout::Velocity) {
                    directDomainScale = 0.11f;
                } else if (params_.readout
                    == AccelerometerReadout::Displacement) {
                    directDomainScale = 0.035f;
                }
                contact += sensor.directHighpassOutput
                    * params_.contactDetail * directTransmission
                    * directDomainScale;

                sensor.mountLowpass += mountCoefficient
                    * (contact - sensor.mountLowpass);
                const float conditioned = sensor.mountLowpass
                    - sensor.conditionerInput
                    + highpassPole * sensor.conditionerOutput;
                sensor.conditionerInput = sensor.mountLowpass;
                sensor.conditionerOutput = flushDenormal(conditioned);
                contact = sensor.conditionerOutput + randomSigned()
                    * params_.sensorNoise * 0.00035f;
                contact = std::tanh(contact * outputGain * 0.72f);

                sensor.radiationLowpass += airCoefficient
                    * (radiatedAcceleration[sensorIndex]
                        - sensor.radiationLowpass);
                const float radiation = std::tanh(
                    sensor.radiationLowpass * params_.airRadiation
                    * outputGain * 0.46f);

                const float fieldSample = lerp(
                    contact, radiation, params_.contactRadiation)
                    * sensorSumScale;
                for (uint32_t channel = 0u;
                    channel < listenerChannels; ++channel) {
                    listenerHoa[channel] = flushDenormal(
                        listenerHoa[channel]
                        + fieldSample * sensor.basis[channel]);
                }

                if (stems) {
                    if (sensorIndex < activeChannels
                        && outputs[sensorIndex]) {
                        outputs[sensorIndex][frame] = contact;
                    }
                    continue;
                }

                for (uint32_t channel = 0u;
                    channel < activeChannels; ++channel) {
                    if (outputs[channel]) {
                        outputs[channel][frame] = flushDenormal(
                            outputs[channel][frame]
                            + fieldSample * sensor.basis[channel]);
                    }
                }
            }
            // Listening remains attached to the encoded structural field even
            // when the host receives raw sensor stems. It is a causal control
            // score for later events, never an audio feedback return.
            fieldListener_.processFrame(
                listenerHoa.data(), listenerChannels);
        }
    }

    float modeFrequencyHz(uint32_t index) const
    {
        return modes_[std::min<uint32_t>(
            index, kAccelerometerFieldModeCount - 1u)].frequencyHz;
    }

    float modePickupWeight(uint32_t index, uint32_t sensor = 0u) const
    {
        return modes_[std::min<uint32_t>(
            index, kAccelerometerFieldModeCount - 1u)].targetPickupWeight[
                std::min<uint32_t>(
                    sensor, kAccelerometerFieldSensorCount - 1u)];
    }

    float sensorPosition(uint32_t sensor) const
    {
        return sensors_[std::min<uint32_t>(
            sensor, kAccelerometerFieldSensorCount - 1u)].targetPosition;
    }

    Vec3 sensorDirection(uint32_t sensor) const
    {
        return sensors_[std::min<uint32_t>(
            sensor, kAccelerometerFieldSensorCount - 1u)].targetDirection;
    }

    float currentSensorPosition(uint32_t sensor) const
    {
        return sensors_[std::min<uint32_t>(
            sensor, kAccelerometerFieldSensorCount - 1u)].position;
    }

    float listenerActivity() const { return fieldListener_.activity(); }
    float listenerTargetPosition() const { return listenerTargetPosition_; }
    float listenerPickupEnergy(uint32_t sensor) const
    {
        return fieldListener_.envelope(std::min<uint32_t>(
            sensor, kAccelerometerFieldSensorCount - 1u));
    }

private:
    struct SubstrateProfile {
        float baseFrequencyHz = 35.0f;
        float baseDecaySeconds = 0.8f;
        float exponent = 1.5f;
        float modeFalloff = 0.55f;
        float decayFalloff = 0.55f;
        float massSensitivity = 0.5f;
        float dispersion = 0.0f;
        float modalCoupling = 1.0f;
    };

    struct ModeSample {
        float displacement = 0.0f;
        float velocity = 0.0f;
        float acceleration = 0.0f;
    };

    struct Sensor {
        float position = 0.5f;
        float axis = 0.5f;
        Vec3 direction { 1.0f, 0.0f, 0.0f };
        std::array<float, kAccelerometerFieldMaxChannels> basis {};
        float targetPosition = 0.5f;
        float targetAxis = 0.5f;
        Vec3 targetDirection { 1.0f, 0.0f, 0.0f };
        std::array<float, kAccelerometerFieldMaxChannels> targetBasis {};
        float directLowpass = 0.0f;
        float directHighpassInput = 0.0f;
        float directHighpassOutput = 0.0f;
        float mountLowpass = 0.0f;
        float conditionerInput = 0.0f;
        float conditionerOutput = 0.0f;
        float radiationLowpass = 0.0f;

        void reset()
        {
            directLowpass = 0.0f;
            directHighpassInput = 0.0f;
            directHighpassOutput = 0.0f;
            mountLowpass = 0.0f;
            conditionerInput = 0.0f;
            conditionerOutput = 0.0f;
            radiationLowpass = 0.0f;
        }

        void snapGeometry()
        {
            position = targetPosition;
            axis = targetAxis;
            direction = targetDirection;
            basis = targetBasis;
        }

        void smoothGeometry(float coefficient)
        {
            position += (targetPosition - position) * coefficient;
            axis += (targetAxis - axis) * coefficient;
            direction = normalize(Vec3 {
                direction.x + (targetDirection.x - direction.x) * coefficient,
                direction.y + (targetDirection.y - direction.y) * coefficient,
                direction.z + (targetDirection.z - direction.z) * coefficient,
            });
            for (uint32_t channel = 0u;
                channel < kAccelerometerFieldMaxChannels; ++channel) {
                basis[channel] +=
                    (targetBasis[channel] - basis[channel]) * coefficient;
            }
        }
    };

    struct Mode {
        bool active = false;
        float frequencyHz = 0.0f;
        float pairProgress = 0.0f;
        float coefficient = 0.0f;
        float radius = 0.0f;
        float radiusSquared = 0.0f;
        float drive = 0.0f;
        float cascadeWeight = 0.0f;
        float pitchEvolutionWeight = 1.0f;
        std::array<float, kAccelerometerFieldSensorCount> pickupWeight {};
        std::array<float, kAccelerometerFieldSensorCount> radiationWeight {};
        std::array<float, kAccelerometerFieldSensorCount> targetPickupWeight {};
        std::array<float, kAccelerometerFieldSensorCount> targetRadiationWeight {};
        float velocityNormalization = 1.0f;
        float accelerationNormalization = 1.0f;
        float state1 = 0.0f;
        float state2 = 0.0f;

        void reset()
        {
            state1 = 0.0f;
            state2 = 0.0f;
        }

        void snapGeometry()
        {
            pickupWeight = targetPickupWeight;
            radiationWeight = targetRadiationWeight;
        }

        void smoothGeometry(float coefficient)
        {
            for (uint32_t sensor = 0u;
                sensor < kAccelerometerFieldSensorCount; ++sensor) {
                pickupWeight[sensor] +=
                    (targetPickupWeight[sensor] - pickupWeight[sensor])
                    * coefficient;
                radiationWeight[sensor] +=
                    (targetRadiationWeight[sensor] - radiationWeight[sensor])
                    * coefficient;
            }
        }

        ModeSample process(float input)
        {
            const float previous1 = state1;
            const float previous2 = state2;
            float current = coefficient * previous1
                - radiusSquared * previous2 + input * drive;
            if (!std::isfinite(current) || std::fabs(current) > 64.0f) {
                reset();
                return {};
            }
            state2 = previous1;
            state1 = flushDenormal(current);
            return {
                current,
                (current - previous1) * velocityNormalization,
                (current - 2.0f * previous1 + previous2)
                    * accelerationNormalization,
            };
        }
    };

    static SubstrateProfile substrateProfile(AccelerometerSubstrate substrate)
    {
        switch (substrate) {
        case AccelerometerSubstrate::Stem:
            return { 48.0f, 1.45f, 1.48f, 0.48f, 0.48f, 0.34f, 0.020f, 1.00f };
        case AccelerometerSubstrate::BellBronze:
            return { 155.0f, 8.50f, 1.18f, 0.28f, 0.20f, 0.025f, 0.055f, 1.00f };
        case AccelerometerSubstrate::Wood:
            return { 92.0f, 0.90f, 1.34f, 0.52f, 0.62f, 0.14f, 0.038f, 1.00f };
        case AccelerometerSubstrate::Glass:
            return { 210.0f, 4.20f, 1.43f, 0.34f, 0.30f, 0.055f, 0.080f, 1.00f };
        case AccelerometerSubstrate::Wire:
            return { 82.0f, 2.30f, 1.00f, 0.42f, 0.38f, 0.42f, 0.012f, 1.00f };
        case AccelerometerSubstrate::DryLeaf:
            return { 42.0f, 0.09f, 1.74f, 0.68f, 0.88f, 0.86f, 0.035f, 0.45f };
        case AccelerometerSubstrate::PaperCardboard:
            return { 55.0f, 0.24f, 1.55f, 0.70f, 0.78f, 0.58f, 0.025f, 0.60f };
        case AccelerometerSubstrate::ShellChitin:
            return { 135.0f, 0.52f, 1.36f, 0.48f, 0.50f, 0.40f, 0.065f, 0.85f };
        case AccelerometerSubstrate::PolymerMembrane:
            return { 46.0f, 0.46f, 1.18f, 0.40f, 0.44f, 0.34f, 0.015f, 0.52f };
        case AccelerometerSubstrate::GongAgeng:
            return { 44.5f, 11.50f, 1.16f, 0.34f, 0.34f, 0.020f, 0.040f, 1.00f };
        case AccelerometerSubstrate::Jing:
            // Cho measured a 114 Hz resting fundamental on a 382 mm jing.
            return { 114.0f, 10.80f, 1.14f, 0.32f, 0.30f, 0.018f, 0.038f, 1.00f };
        case AccelerometerSubstrate::Kkwaenggwari:
            // Low radiation is intentionally weak; the modal bank is weighted
            // toward the documented 1, 2, and 4 kHz radiation/attack regions.
            return { 250.0f, 4.20f, 1.20f, 0.20f, 0.38f, 0.018f, 0.070f, 1.00f };
        case AccelerometerSubstrate::Leaf:
        default:
            return { 28.0f, 0.14f, 1.58f, 0.58f, 0.82f, 0.72f, 0.025f, 0.65f };
        }
    }

    static float hashSigned(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu)
            * (2.0f / 16777215.0f) - 1.0f;
    }

    float nominalModeRatio(uint32_t index, const SubstrateProfile& profile) const
    {
        if (params_.substrate == AccelerometerSubstrate::GongAgeng) {
            // Twelve perceptually selected clusters after Ayers and Horner:
            // the first/second partials provide rumble, the fifth/sixth region
            // provides the focused ding, and upper groups mostly color attack.
            // Adjacent members share one spatial lobe family so their beating
            // changes the encoded field as well as the monophonic spectrum.
            static constexpr std::array<float,
                kAccelerometerFieldModeCount> ratios {{
                1.0000f, 1.0375f, 1.6630f, 2.0000f,
                2.6970f, 3.0000f, 5.0000f, 5.1200f,
                5.8430f, 6.1800f, 6.3370f, 6.4700f,
                7.0000f, 7.1600f, 9.0000f, 9.2000f,
                10.000f, 10.220f, 11.000f, 11.250f,
                12.000f, 12.280f, 14.000f, 14.300f,
            }};
            return ratios[index];
        }
        if (params_.substrate == AccelerometerSubstrate::Jing) {
            // Cho's 114 Hz jing produced five exact resting harmonics. The
            // measured 240/351/460/572 Hz impact components and 338/342,
            // 408/416, and 1155/1163 Hz late pairs supply its two beat regimes.
            static constexpr std::array<float,
                kAccelerometerFieldModeCount> ratios {{
                1.0000f, 1.0200f, 2.0000f, 2.1053f,
                2.9649f, 3.0000f, 3.5789f, 3.6491f,
                4.0000f, 4.0351f, 5.0000f, 5.0175f,
                6.0000f, 6.0526f, 7.0000f, 7.0702f,
                8.0000f, 8.0877f, 9.0000f, 9.0965f,
                10.1316f, 10.2018f, 11.0000f, 11.0702f,
            }};
            return ratios[index];
        }
        if (params_.substrate == AccelerometerSubstrate::Kkwaenggwari) {
            // Analysis establishes broad radiation landmarks rather than a
            // single stable pitch: little below 125 Hz, weak 250 Hz, and the
            // defining metal/attack energy around 1, 2, and 4 kHz. These
            // inharmonic pairs place energy in those zones without pretending
            // that one handmade instrument supplies universal exact partials.
            static constexpr std::array<float,
                kAccelerometerFieldModeCount> ratios {{
                1.00f, 1.08f, 2.00f, 2.08f,
                4.00f, 4.12f, 5.10f, 5.35f,
                7.60f, 7.95f, 8.20f, 8.65f,
                11.20f, 11.80f, 15.60f, 16.20f,
                18.00f, 18.80f, 22.00f, 23.00f,
                26.00f, 27.20f, 31.00f, 32.50f,
            }};
            return ratios[index];
        }
        if (params_.substrate == AccelerometerSubstrate::BellBronze) {
            // Hum, prime, tierce, quint, nominal, then paired/non-axisymmetric
            // upper modes. Close pairs deliberately retain bell warble.
            static constexpr std::array<float,
                kAccelerometerFieldModeCount> ratios {{
                1.00f, 1.006f, 2.00f, 2.014f, 2.40f, 2.414f,
                3.00f, 3.025f, 4.00f, 4.038f, 5.10f, 5.16f,
                6.35f, 6.44f, 7.85f, 7.98f, 9.55f, 9.72f,
                11.50f, 11.72f, 13.70f, 14.00f, 16.20f, 16.62f,
            }};
            return ratios[index];
        }
        if (params_.substrate == AccelerometerSubstrate::ShellChitin) {
            const float family = std::pow(
                static_cast<float>(index / 2u + 1u), profile.exponent);
            return family * ((index & 1u) == 0u ? 1.0f : 1.018f);
        }
        if (params_.substrate == AccelerometerSubstrate::PolymerMembrane) {
            // Ordered low modes of a slightly rectangular tensioned membrane.
            static constexpr std::array<float,
                kAccelerometerFieldModeCount> ratios {{
                1.00f, 1.27f, 1.61f, 1.95f, 2.18f, 2.43f,
                2.72f, 2.94f, 3.18f, 3.42f, 3.67f, 3.88f,
                4.12f, 4.37f, 4.61f, 4.84f, 5.10f, 5.34f,
                5.58f, 5.83f, 6.08f, 6.31f, 6.57f, 6.82f,
            }};
            return ratios[index]
                * (1.0f + profile.dispersion * ratios[index]);
        }
        const float n = static_cast<float>(index + 1u);
        float ratio = std::pow(n, profile.exponent);
        if (params_.substrate == AccelerometerSubstrate::Wire) {
            ratio = n * (1.0f + profile.dispersion * n * n);
        }
        return ratio;
    }

    float modeShape(uint32_t index, float position, bool orthogonal) const
    {
        position = clamp(position, 0.0f, 1.0f);
        if (params_.substrate == AccelerometerSubstrate::BellBronze
            || params_.substrate == AccelerometerSubstrate::GongAgeng
            || params_.substrate == AccelerometerSubstrate::Jing
            || params_.substrate == AccelerometerSubstrate::Kkwaenggwari
            || params_.substrate == AccelerometerSubstrate::ShellChitin) {
            const uint32_t pair = index / 2u;
            const float order = static_cast<float>(pair
                + (params_.substrate == AccelerometerSubstrate::BellBronze
                        || params_.substrate == AccelerometerSubstrate::GongAgeng
                        || params_.substrate == AccelerometerSubstrate::Jing
                        || params_.substrate == AccelerometerSubstrate::Kkwaenggwari
                    ? 2u : 1u));
            const float pairPhase = (index & 1u) == 0u ? 0.0f : 0.25f;
            const float phase = pairPhase + (orthogonal
                ? (params_.substrate == AccelerometerSubstrate::ShellChitin
                    ? 0.18f : 0.25f) : 0.0f);
            return std::cos(kAccelerometerFieldTwoPi
                * (order * position + phase));
        }
        const float order = static_cast<float>(index + 1u);
        const float boundary = 0.12f + 0.88f
            * std::sin(3.14159265358979323846f
                * clamp(position * 0.94f + 0.03f, 0.0f, 1.0f));
        const float phase = orthogonal
            ? 1.57079632679489661923f : 0.31f * static_cast<float>(index & 1u);
        return boundary * std::sin(
            3.14159265358979323846f * order * position + phase);
    }

    float pitchEvolutionWeight(uint32_t index) const
    {
        if (params_.substrate != AccelerometerSubstrate::Jing) return 1.0f;
        // The exact harmonics soften with impact level; the short-lived
        // components around them remain comparatively fixed and create the
        // observed beat rate that changes as the harmonics recover upward.
        static constexpr std::array<float,
            kAccelerometerFieldModeCount> weights {{
            1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        }};
        return weights[index];
    }

    void smoothGeometry()
    {
        for (auto& sensor : sensors_) {
            sensor.smoothGeometry(geometrySmoothingCoefficient_);
        }
        for (auto& mode : modes_) {
            mode.smoothGeometry(geometrySmoothingCoefficient_);
        }
    }

    void rebuildModel()
    {
        const SubstrateProfile profile = substrateProfile(params_.substrate);
        const float sizeScale = std::exp2((0.5f - params_.size) * 3.2f);
        const float nyquistLimit = static_cast<float>(sampleRate_) * 0.44f;
        const float velocityNormalization = static_cast<float>(sampleRate_)
            / (kAccelerometerFieldTwoPi * 1000.0f);
        const float accelerationNormalization = velocityNormalization
            * velocityNormalization;

        for (uint32_t sensorIndex = 0u;
            sensorIndex < kAccelerometerFieldSensorCount; ++sensorIndex) {
            Sensor& sensor = sensors_[sensorIndex];
            const float unit = (static_cast<float>(sensorIndex) + 0.5f)
                / static_cast<float>(kAccelerometerFieldSensorCount);
            sensor.targetPosition = accelerometerFieldSensorPosition(
                params_, sensorIndex);
            sensor.targetAxis = clamp(params_.pickupAxis
                    + (unit - 0.5f) * 0.72f,
                0.0f, 1.0f);

            float azimuthOffset = (unit - 0.5f) * 150.0f;
            float elevationOffset = std::sin(
                kAccelerometerFieldTwoPi * unit) * 24.0f;
            if (params_.substrate == AccelerometerSubstrate::BellBronze
                || params_.substrate == AccelerometerSubstrate::GongAgeng
                || params_.substrate == AccelerometerSubstrate::Jing
                || params_.substrate == AccelerometerSubstrate::Kkwaenggwari
                || params_.substrate == AccelerometerSubstrate::Glass
                || params_.substrate == AccelerometerSubstrate::ShellChitin) {
                azimuthOffset = (unit - 0.5f) * 360.0f;
                elevationOffset = std::sin(
                    kAccelerometerFieldTwoPi * unit) * 18.0f;
            } else if (params_.substrate == AccelerometerSubstrate::Stem
                || params_.substrate == AccelerometerSubstrate::Wire) {
                azimuthOffset = (unit - 0.5f) * 105.0f;
                elevationOffset = (unit - 0.5f) * 72.0f;
            } else if (params_.substrate
                == AccelerometerSubstrate::PolymerMembrane) {
                azimuthOffset = (unit - 0.5f) * 220.0f;
                elevationOffset = std::sin(
                    kAccelerometerFieldTwoPi * unit) * 34.0f;
            } else if (params_.substrate
                == AccelerometerSubstrate::PaperCardboard) {
                azimuthOffset = (unit - 0.5f) * 170.0f;
                elevationOffset = std::sin(
                    kAccelerometerFieldTwoPi * unit) * 16.0f;
            }
            sensor.targetDirection = directionFromAed(
                params_.fieldAzimuthDeg
                    + azimuthOffset * params_.spatialExtent,
                clamp(params_.fieldElevationDeg
                        + elevationOffset * params_.spatialExtent,
                    -89.0f, 89.0f));
            const auto fullBasis = acnSn3dBasis7(sensor.targetDirection);
            for (uint32_t channel = 0u;
                channel < kAccelerometerFieldMaxChannels; ++channel) {
                sensor.targetBasis[channel] = fullBasis[channel];
            }
        }

        // The shared listener's virtual ears coincide with the eight encoded
        // accelerometer bearings, keeping its score tied to this measurement
        // geometry rather than to an unrelated loudspeaker layout.
        std::array<Vec3, kAccelerometerFieldSensorCount>
            listenerDirections {};
        for (uint32_t sensorIndex = 0u;
            sensorIndex < kAccelerometerFieldSensorCount; ++sensorIndex) {
            listenerDirections[sensorIndex] =
                sensors_[sensorIndex].targetDirection;
        }
        fieldListener_.setDirections(
            listenerDirections.data(), listenerDirections.size());

        for (uint32_t index = 0u;
            index < kAccelerometerFieldModeCount; ++index) {
            Mode& mode = modes_[index];
            const float modeProgress = static_cast<float>(index)
                / static_cast<float>(kAccelerometerFieldModeCount - 1u);
            const float irregular = hashSigned(
                params_.seed ^ (index + 1u) * 0x9e3779b9u);
            const float nominal = profile.baseFrequencyHz * sizeScale
                * nominalModeRatio(index, profile)
                * performancePitchRatio_
                * std::exp2(irregular * params_.irregularity * 0.10f);
            const float addedMass = params_.sensorMass
                * profile.massSensitivity * (0.72f + 0.55f * modeProgress);
            const float frequency = nominal / std::sqrt(1.0f + addedMass);
            mode.frequencyHz = frequency;
            mode.pitchEvolutionWeight = pitchEvolutionWeight(index);
            mode.pairProgress = static_cast<float>(index / 2u)
                / static_cast<float>(kAccelerometerFieldModeCount / 2u - 1u);
            mode.active = frequency >= 5.0f && frequency < nyquistLimit;
            if (!mode.active) {
                mode.reset();
                mode.coefficient = 0.0f;
                mode.radius = 0.0f;
                mode.radiusSquared = 0.0f;
                mode.drive = 0.0f;
                mode.cascadeWeight = 0.0f;
                mode.targetPickupWeight.fill(0.0f);
                mode.targetRadiationWeight.fill(0.0f);
                continue;
            }

            const float decayScale = std::exp2((0.5f - params_.damping) * 4.2f);
            float decay = std::max(0.015f,
                profile.baseDecaySeconds * decayScale
                / std::pow(1.0f + modeProgress * 5.0f, profile.decayFalloff));
            if (params_.substrate == AccelerometerSubstrate::GongAgeng
                || params_.substrate == AccelerometerSubstrate::Jing) {
                static constexpr std::array<float, 12u> decayWeights {{
                    1.35f, 1.00f, 0.82f, 0.58f, 0.88f, 0.66f,
                    0.52f, 0.43f, 0.36f, 0.30f, 0.26f, 0.22f,
                }};
                decay *= decayWeights[index / 2u];
            }
            const float radius = std::exp(
                -1.0f / (decay * static_cast<float>(sampleRate_)));
            const float omega = kAccelerometerFieldTwoPi
                * frequency / static_cast<float>(sampleRate_);
            mode.coefficient = 2.0f * radius * std::cos(omega);
            mode.radius = radius;
            mode.radiusSquared = radius * radius;

            const float sourceShape = modeShape(
                index, params_.sourcePosition, false);
            float modalGain = 1.0f
                / std::pow(1.0f + static_cast<float>(index), profile.modeFalloff);
            if (params_.substrate == AccelerometerSubstrate::GongAgeng
                || params_.substrate == AccelerometerSubstrate::Jing
                || params_.substrate == AccelerometerSubstrate::Kkwaenggwari) {
                static constexpr std::array<float, 12u> clusterWeights {{
                    0.82f, 1.00f, 0.58f, 0.42f, 0.76f, 0.46f,
                    0.34f, 0.27f, 0.22f, 0.18f, 0.15f, 0.13f,
                }};
                static constexpr std::array<float, 12u> kkwaengWeights {{
                    0.16f, 0.32f, 0.68f, 0.78f, 1.00f, 0.96f,
                    0.88f, 0.82f, 0.66f, 0.52f, 0.40f, 0.30f,
                }};
                modalGain *= params_.substrate
                        == AccelerometerSubstrate::Kkwaenggwari
                    ? kkwaengWeights[index / 2u]
                    : clusterWeights[index / 2u];
            }
            mode.drive = 0.0045f * modalGain * sourceShape;
            mode.cascadeWeight = std::pow(modeProgress, 1.45f)
                * (0.32f + 0.68f * std::fabs(sourceShape));
            const float radiationEfficiency = std::sqrt(
                frequency / (frequency + 720.0f));
            for (uint32_t sensorIndex = 0u;
                sensorIndex < kAccelerometerFieldSensorCount; ++sensorIndex) {
                const Sensor& sensor = sensors_[sensorIndex];
                const float axisRadians = sensor.targetAxis
                    * 1.57079632679489661923f;
                const float pickupShape = std::cos(axisRadians) * modeShape(
                    index, sensor.targetPosition, false)
                    + std::sin(axisRadians) * modeShape(
                        index, sensor.targetPosition, true);
                const float distance = std::fabs(
                    params_.sourcePosition - sensor.targetPosition);
                const float transmission = std::exp(-distance
                    * params_.propagationLoss
                    * (0.35f + 3.2f * modeProgress));
                mode.targetPickupWeight[sensorIndex] =
                    pickupShape * transmission;
                mode.targetRadiationWeight[sensorIndex] = modeShape(
                    index, sensor.targetPosition, false)
                    * modalGain * radiationEfficiency
                    * (0.58f + 0.42f * std::fabs(sourceShape));
            }
            mode.velocityNormalization = velocityNormalization;
            mode.accelerationNormalization = accelerationNormalization;
        }
    }

    float modalCouplingValue(uint32_t index) const
    {
        const float progress = static_cast<float>(index / 2u)
            / static_cast<float>(kAccelerometerFieldModeCount / 2u - 1u);
        const float slowWeight = 0.10f + 0.72f * (1.0f - progress);
        return couplingSlowValue_ * slowWeight
            + couplingFastValue_ * (1.0f - slowWeight);
    }

    void triggerNonlinearImpact(float strength)
    {
        strength = clamp(strength, 0.0f, 1.25f);
        if (strength < 0.01f) return;
        impactStrength_ = std::max(impactStrength_, strength);
        impactAgeSeconds_ = 0.0f;
        dynamicCoefficientCountdown_ = 0u;
    }

    void detectExternalImpact(float externalForce)
    {
        const float magnitude = std::fabs(externalForce);
        const float onset = std::max(
            0.0f, magnitude - externalTransientEnvelope_ * 1.8f);
        const float coefficient = magnitude > externalTransientEnvelope_
            ? 0.08f : 0.0008f;
        externalTransientEnvelope_ += coefficient
            * (magnitude - externalTransientEnvelope_);
        if (onset > 0.02f) {
            triggerNonlinearImpact(clamp(onset * 1.15f, 0.0f, 1.25f));
        }
    }

    void advanceModalEvolution()
    {
        const float inverseSampleRate = 1.0f
            / static_cast<float>(sampleRate_);
        smoothedCoupling_ += geometrySmoothingCoefficient_
            * (params_.coupling - smoothedCoupling_);
        smoothedEnergy_ += geometrySmoothingCoefficient_
            * (params_.energy - smoothedEnergy_);
        const float slowRateHz = 0.80f + 1.30f * smoothedCoupling_;
        const float fastRateHz = 8.0f + 10.0f * smoothedCoupling_;
        couplingSlowPhase_ += kAccelerometerFieldTwoPi
            * slowRateHz * inverseSampleRate;
        couplingFastPhase_ += kAccelerometerFieldTwoPi
            * fastRateHz * inverseSampleRate;
        if (couplingSlowPhase_ >= kAccelerometerFieldTwoPi) {
            couplingSlowPhase_ -= kAccelerometerFieldTwoPi;
        }
        if (couplingFastPhase_ >= kAccelerometerFieldTwoPi) {
            couplingFastPhase_ -= kAccelerometerFieldTwoPi;
        }
        couplingSlowValue_ = std::sin(couplingSlowPhase_);
        couplingFastValue_ = std::sin(couplingFastPhase_);

        const bool isJing = params_.substrate
            == AccelerometerSubstrate::Jing;
        const float glideSeconds = isJing
            ? 0.55f + 1.80f * params_.size
            : 0.12f + 0.46f * params_.size;
        const float glide = impactStrength_
            * std::exp(-impactAgeSeconds_ / glideSeconds);
        // Jing harmonics begin flat and recover upward (softening). Other gong
        // families retain the bounded hardening trajectory of the earlier
        // reduced model until instrument-specific measurements replace it.
        currentPitchScale_ = isJing
            ? 1.0f - smoothedEnergy_ * glide * 0.055f
            : 1.0f + smoothedEnergy_ * glide * 0.10f;

        const float cascadeRiseSeconds = 0.006f
            + 0.054f * params_.size;
        const float cascadeDecaySeconds = 0.09f
            + 0.52f * params_.size;
        const float cascadeRise = 1.0f
            - std::exp(-impactAgeSeconds_ / cascadeRiseSeconds);
        currentCascadeLevel_ = smoothedEnergy_ * impactStrength_
            * cascadeRise
            * std::exp(-impactAgeSeconds_ / cascadeDecaySeconds);
        impactAgeSeconds_ += inverseSampleRate;
        if (impactAgeSeconds_ > 5.0f) {
            impactStrength_ = 0.0f;
            currentPitchScale_ = 1.0f;
            currentCascadeLevel_ = 0.0f;
        }
    }

    void updateDynamicCoefficients()
    {
        const float nyquistLimit = static_cast<float>(sampleRate_) * 0.44f;
        for (uint32_t index = 0u;
            index < kAccelerometerFieldModeCount; ++index) {
            auto& mode = modes_[index];
            if (!mode.active) continue;
            const float modulation = modalCouplingValue(index);
            const float frequencyDepth = smoothedCoupling_
                * (0.0025f + 0.0065f * (1.0f - mode.pairProgress));
            const float nonlinearScale = 1.0f
                + (currentPitchScale_ - 1.0f)
                    * mode.pitchEvolutionWeight;
            const float frequencyScale = nonlinearScale
                * std::max(0.96f, 1.0f + frequencyDepth * modulation);
            const float frequency = std::min(
                nyquistLimit, mode.frequencyHz * frequencyScale);
            const float omega = kAccelerometerFieldTwoPi
                * frequency / static_cast<float>(sampleRate_);
            mode.coefficient = 2.0f * mode.radius * std::cos(omega);
        }
    }

    uint32_t randomU32()
    {
        uint32_t value = rngState_;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        rngState_ = value == 0u ? kAccelerometerFieldDefaultSeed : value;
        return rngState_;
    }

    float randomUnit()
    {
        return static_cast<float>(randomU32() & 0x00ffffffu)
            * (1.0f / 16777215.0f);
    }

    float randomSigned() { return randomUnit() * 2.0f - 1.0f; }

    void triggerEvent(float randomAccent)
    {
        const float sr = static_cast<float>(sampleRate_);
        const float authoredPosition = clamp(params_.sourcePosition
                + randomSigned() * (0.025f + 0.24f * params_.irregularity),
            0.01f, 0.99f);
        eventPosition_ = authoredPosition;
        eventTexture_ = params_.texture;
        float listenerStrengthScale = 1.0f;
        if (params_.fieldListenMode != AmbiFieldListenMode::Off) {
            const float influence = fieldListener_.activity()
                * params_.fieldListenAmount;
            float weightSum = 0.0f;
            float positionSum = 0.0f;
            AmbiFieldListenerScore heard {};
            for (uint32_t sensor = 0u;
                sensor < kAccelerometerFieldSensorCount; ++sensor) {
                const float preference = fieldListener_.preference(
                    sensors_[sensor].direction, params_.fieldListenMode);
                const float weight = 0.025f + preference * preference;
                const auto score = fieldListener_.score(
                    sensors_[sensor].direction, params_.fieldListenMode);
                weightSum += weight;
                positionSum += sensors_[sensor].position * weight;
                heard.relativeEnergy += score.relativeEnergy * weight;
                heard.novelty += score.novelty * weight;
                heard.roughness += score.roughness * weight;
                heard.spectralTilt += score.spectralTilt * weight;
                heard.habituation += score.habituation * weight;
                heard.charge += score.charge * weight;
            }
            if (weightSum > 1.0e-7f) {
                listenerTargetPosition_ = positionSum / weightSum;
                heard.relativeEnergy /= weightSum;
                heard.novelty /= weightSum;
                heard.roughness /= weightSum;
                heard.spectralTilt /= weightSum;
                heard.habituation /= weightSum;
                heard.charge /= weightSum;
            }
            eventPosition_ = clamp(lerp(authoredPosition,
                listenerTargetPosition_, influence * 0.82f), 0.01f, 0.99f);
            if (params_.fieldListenResponse
                == AmbiFieldListenerResponse::Excite) {
                listenerStrengthScale += influence
                    * (heard.novelty * 0.18f + heard.charge * 0.22f
                        + heard.roughness * 0.08f);
                eventTexture_ = clamp(params_.texture + influence
                        * (heard.roughness * 0.10f + heard.novelty * 0.06f),
                    0.0f, 1.0f);
            } else if (params_.fieldListenResponse
                == AmbiFieldListenerResponse::Settle) {
                listenerStrengthScale -= influence
                    * (heard.habituation * 0.28f
                        + heard.relativeEnergy * 0.10f);
                eventTexture_ = clamp(params_.texture - influence
                        * heard.habituation * 0.12f,
                    0.0f, 1.0f);
            } else if (params_.fieldListenResponse
                == AmbiFieldListenerResponse::Imprint) {
                const float roughnessShape = heard.roughness * 2.0f - 1.0f;
                eventTexture_ = clamp(params_.texture + influence
                        * (roughnessShape * 0.22f
                            + heard.spectralTilt * 0.12f),
                    0.0f, 1.0f);
                listenerStrengthScale += influence
                    * (heard.relativeEnergy - 0.5f) * 0.12f;
            }
        } else {
            listenerTargetPosition_ = params_.sourcePosition;
        }
        float strength = params_.force * (0.65f + 0.55f * randomAccent);
        float duration = 0.012f;
        switch (params_.excitation) {
        case AccelerometerExcitation::Footsteps:
            duration = 0.010f + 0.022f * eventTexture_;
            secondaryCountdown_ = static_cast<uint32_t>(sr
                * (0.018f + randomUnit() * 0.055f));
            break;
        case AccelerometerExcitation::Chewing:
            strength = params_.force * (0.18f
                + 1.55f * std::pow(randomAccent, 1.6f));
            duration = 0.060f + 0.170f * randomUnit();
            chewingEventAge_ = 0u;
            chewingEventDuration_ = std::max<uint32_t>(1u,
                static_cast<uint32_t>(duration * sr));
            chewingGrainPhase_ = randomUnit();
            chewingGrainEnvelope_ = 0.0f;
            break;
        case AccelerometerExcitation::Scrape:
            duration = 0.075f + 0.34f * (1.0f - eventTexture_ * 0.55f);
            break;
        case AccelerometerExcitation::Tremulation:
            duration = 0.16f + 0.62f * (1.0f - eventTexture_ * 0.40f);
            break;
        case AccelerometerExcitation::Tap:
            duration = 0.0015f + 0.012f * (1.0f - eventTexture_);
            break;
        case AccelerometerExcitation::Ambient:
        default:
            duration = 0.016f + 0.090f * (1.0f - eventTexture_);
            break;
        }
        strength *= clamp(listenerStrengthScale, 0.55f, 1.55f);
        eventEnvelope_ = std::max(eventEnvelope_, strength);
        eventDecay_ = std::exp(-1.0f / std::max(1.0f, duration * sr));
        triggerNonlinearImpact(strength);
    }

    float proceduralExcitation()
    {
        const float noise = randomSigned();
        ambientSlow_ += 0.00055f * (noise - ambientSlow_);
        ambientFast_ += 0.027f * (noise - ambientFast_);
        float output = params_.ambientDrive
            * (ambientSlow_ * 0.19f + ambientFast_ * 0.026f);

        const float timingWander = 1.0f + params_.irregularity
            * (ambientSlow_ * 1.8f + 0.16f * randomSigned());
        bool eventClockRunning = true;
        if (params_.excitation == AccelerometerExcitation::Chewing
            && chewingPauseCountdown_ > 0u) {
            --chewingPauseCountdown_;
            eventClockRunning = false;
        }
        if (eventClockRunning) {
            eventPhase_ += params_.eventRateHz
                * std::max(0.25f, timingWander)
                / static_cast<float>(sampleRate_);
        }
        if (eventClockRunning && eventPhase_ >= 1.0f) {
            eventPhase_ -= std::floor(eventPhase_);
            if (randomUnit() <= params_.activity) {
                triggerEvent(randomUnit());
                if (params_.excitation == AccelerometerExcitation::Chewing) {
                    eventPhase_ = 0.10f * randomSigned();
                    ++chewingClosureCount_;
                    if (chewingClosureCount_ >= chewingClosureTarget_) {
                        chewingClosureCount_ = 0u;
                        chewingClosureTarget_ = 3u + randomU32() % 4u;
                        chewingPauseCountdown_ = static_cast<uint32_t>(
                            static_cast<float>(sampleRate_)
                            * (0.08f + 0.18f * randomUnit()));
                    }
                }
            }
        }

        bool secondaryImpact = false;
        if (secondaryCountdown_ > 0u) {
            --secondaryCountdown_;
            if (secondaryCountdown_ == 0u) {
                eventEnvelope_ = std::max(
                    eventEnvelope_, params_.force * 0.54f);
                eventDecay_ = std::exp(-1.0f / std::max(
                    1.0f, 0.012f * static_cast<float>(sampleRate_)));
                triggerNonlinearImpact(params_.force * 0.54f);
                secondaryImpact = true;
            }
        }

        const float gestureTexture = params_.fieldListenMode
                == AmbiFieldListenMode::Off
            ? params_.texture : eventTexture_;
        const float toneFrequency = 34.0f + gestureTexture * 310.0f;
        eventTonePhase_ += toneFrequency / static_cast<float>(sampleRate_);
        eventTonePhase_ -= std::floor(eventTonePhase_);
        const float tone = std::sin(
            kAccelerometerFieldTwoPi * eventTonePhase_);

        if (eventEnvelope_ > 1.0e-7f) {
            float gesture = 0.0f;
            switch (params_.excitation) {
            case AccelerometerExcitation::Footsteps:
                gesture = 0.58f + noise * (0.18f + 0.52f * gestureTexture);
                break;
            case AccelerometerExcitation::Chewing:
            {
                const float progress = clamp(
                    static_cast<float>(chewingEventAge_)
                        / static_cast<float>(chewingEventDuration_),
                    0.0f, 1.0f);
                // One closure is not an impact.  It begins with compliant
                // contact, grows into a fibrous strip-removal gesture, then
                // leaves a smaller frayed release.
                const auto smoothStep = [](float value) {
                    value = clamp(value, 0.0f, 1.0f);
                    return value * value * (3.0f - 2.0f * value);
                };
                const float contactRise = smoothStep(progress / 0.18f);
                const float tearRise = smoothStep((progress - 0.08f) / 0.20f);
                const float tearFall = 1.0f
                    - smoothStep((progress - 0.67f) / 0.27f);
                const float tearEnvelope = tearRise * tearFall;
                const float releaseEnvelope = smoothStep(
                    (progress - 0.66f) / 0.14f)
                    * (1.0f - smoothStep((progress - 0.82f) / 0.18f));
                const float contactEnvelope = contactRise
                    * (1.0f - smoothStep((progress - 0.24f) / 0.24f));

                chewingNoiseLow_ += 0.038f * (noise - chewingNoiseLow_);
                chewingNoiseMid_ += 0.18f * (noise - chewingNoiseMid_);
                const float compression = chewingNoiseLow_ * 0.86f;
                const float fiber = chewingNoiseLow_ * 0.62f
                    + chewingNoiseMid_ * 0.38f;

                chewingGrainPhase_ += (34.0f + 52.0f * gestureTexture)
                    / static_cast<float>(sampleRate_);
                if (chewingGrainPhase_ >= 1.0f) {
                    chewingGrainPhase_ -= std::floor(chewingGrainPhase_);
                    if (randomUnit() < 0.62f) {
                        chewingGrainEnvelope_ = 0.10f + 0.62f * randomUnit();
                    }
                }
                const float grain = chewingGrainEnvelope_
                    * (chewingNoiseMid_ * 0.70f + chewingNoiseLow_ * 0.30f);
                chewingGrainEnvelope_ = flushDenormal(
                    chewingGrainEnvelope_ * 0.991f);
                const float undulation = 0.70f + 0.30f
                    * std::sin(kAccelerometerFieldTwoPi
                        * (progress * (1.5f + 1.8f * gestureTexture) + 0.17f));
                gesture = contactEnvelope * compression * 0.32f
                    + tearEnvelope * undulation
                        * (fiber * 0.92f + grain * 0.48f)
                    + releaseEnvelope
                        * (chewingNoiseLow_ * 0.40f + grain * 0.22f);
                ++chewingEventAge_;
                if (chewingEventAge_ >= chewingEventDuration_) {
                    eventEnvelope_ = 0.0f;
                }
                break;
            }
            case AccelerometerExcitation::Scrape:
                gesture = ambientFast_ * 5.5f + noise * gestureTexture * 0.46f;
                break;
            case AccelerometerExcitation::Tremulation:
                gesture = tone * (0.84f - 0.28f * gestureTexture)
                    + ambientFast_ * gestureTexture * 2.2f;
                break;
            case AccelerometerExcitation::Tap:
                gesture = 0.86f + noise * gestureTexture * 0.24f;
                break;
            case AccelerometerExcitation::Ambient:
            default:
                gesture = ambientFast_ * 3.0f + noise * 0.12f;
                break;
            }
            output += eventEnvelope_ * gesture;
            if (params_.excitation != AccelerometerExcitation::Chewing) {
                eventEnvelope_ = flushDenormal(eventEnvelope_ * eventDecay_);
            }
        }
        if (secondaryImpact) output += params_.force * 0.22f;
        return output;
    }

    double sampleRate_ = 0.0;
    float geometrySmoothingCoefficient_ = 1.0f;
    AccelerometerFieldParams params_ = accelerometerFieldFactoryPreset(0u);
    std::array<Mode, kAccelerometerFieldModeCount> modes_ {};
    std::array<Sensor, kAccelerometerFieldSensorCount> sensors_ {};
    AmbiFieldListener fieldListener_ {};
    uint32_t rngState_ = kAccelerometerFieldDefaultSeed;
    float eventPosition_ = 0.5f;
    float eventPhase_ = 0.92f;
    float eventEnvelope_ = 0.0f;
    float eventDecay_ = 0.0f;
    float eventTonePhase_ = 0.0f;
    uint32_t chewingEventAge_ = 0u;
    uint32_t chewingEventDuration_ = 1u;
    float chewingGrainPhase_ = 0.0f;
    float chewingGrainEnvelope_ = 0.0f;
    float chewingNoiseLow_ = 0.0f;
    float chewingNoiseMid_ = 0.0f;
    uint32_t secondaryCountdown_ = 0u;
    uint32_t chewingPauseCountdown_ = 0u;
    uint32_t chewingClosureCount_ = 0u;
    uint32_t chewingClosureTarget_ = 3u;
    float ambientSlow_ = 0.0f;
    float ambientFast_ = 0.0f;
    float eventTexture_ = 0.5f;
    float listenerTargetPosition_ = 0.5f;
    float couplingSlowPhase_ = 0.0f;
    float couplingFastPhase_ = 1.57079632679489661923f;
    float couplingSlowValue_ = 0.0f;
    float couplingFastValue_ = 1.0f;
    float smoothedCoupling_ = 0.28f;
    float smoothedEnergy_ = 0.24f;
    float impactAgeSeconds_ = 8.0f;
    float impactStrength_ = 0.0f;
    float currentPitchScale_ = 1.0f;
    float currentCascadeLevel_ = 0.0f;
    float externalTransientEnvelope_ = 0.0f;
    float performancePitchRatio_ = 1.0f;
    float midiStrikeEnvelope_ = 0.0f;
    float midiStrikeDecay_ = 0.0f;
    float midiStrikeTonePhase_ = 0.0f;
    uint32_t dynamicCoefficientCountdown_ = 0u;
};

} // namespace s3g
