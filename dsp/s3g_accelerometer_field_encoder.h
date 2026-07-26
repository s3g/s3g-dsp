#pragma once

#include "s3g_ambisonic_speaker_decoder.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAccelerometerFieldModeCount = 24u;
constexpr uint32_t kAccelerometerFieldPresetCount = 8u;
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
    AccelerometerSubstrate substrate = AccelerometerSubstrate::Leaf;
    AccelerometerExcitation excitation = AccelerometerExcitation::Footsteps;
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
    return params;
}

inline const AccelerometerFieldPresetInfo&
accelerometerFieldFactoryPresetInfo(uint32_t index)
{
    static constexpr std::array<AccelerometerFieldPresetInfo,
        kAccelerometerFieldPresetCount> info {{
        { "Silent Bronze", "Environmental energy discloses long bell modes." },
        { "Bell Works", "Distant construction-like impacts enter a bronze body." },
        { "Leaf Footfalls", "Small paired contacts travel across a damped leaf." },
        { "Caterpillar Chew", "Soft closures pull irregular fibers from a leaf edge." },
        { "Petiole Duet", "Low tremulation bends a plant stem past one pickup axis." },
        { "Web Plucks", "Sparse contacts expose a dispersive tensioned filament." },
        { "Wood Grain", "Friction reveals short anisotropic wooden modes." },
        { "Glass Dust", "Minute hard impacts wake long, bright glass modes." },
    }};
    return info[std::min<uint32_t>(
        index, kAccelerometerFieldPresetCount - 1u)];
}

inline AccelerometerFieldParams accelerometerFieldFactoryPreset(
    uint32_t index)
{
    AccelerometerFieldParams params;
    params.seed = kAccelerometerFieldDefaultSeed + index * 0x9e3779b9u;
    switch (std::min<uint32_t>(
        index, kAccelerometerFieldPresetCount - 1u)) {
    case 0u:
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
        params.sensorMass = 0.02f;
        params.mountStiffness = 0.94f;
        params.sensorNoise = 0.035f;
        params.airRadiation = 0.04f;
        params.outputGainDb = -4.0f;
        break;
    case 1u:
        params.substrate = AccelerometerSubstrate::BellBronze;
        params.excitation = AccelerometerExcitation::Tap;
        params.eventRateHz = 0.42f;
        params.activity = 0.78f;
        params.force = 0.46f;
        params.texture = 0.62f;
        params.ambientDrive = 0.22f;
        params.size = 0.68f;
        params.damping = 0.18f;
        params.irregularity = 0.16f;
        params.contactDetail = 0.03f;
        params.sourcePosition = 0.86f;
        params.pickupPosition = 0.21f;
        params.pickupAxis = 0.74f;
        params.sensorMass = 0.025f;
        params.mountStiffness = 0.96f;
        params.airRadiation = 0.12f;
        params.outputGainDb = -8.0f;
        break;
    case 2u:
        params.substrate = AccelerometerSubstrate::Leaf;
        params.excitation = AccelerometerExcitation::Footsteps;
        params.eventRateHz = 2.8f;
        params.activity = 0.76f;
        params.force = 0.42f;
        params.texture = 0.48f;
        params.ambientDrive = 0.16f;
        params.size = 0.58f;
        params.damping = 0.56f;
        params.irregularity = 0.34f;
        params.propagationLoss = 0.46f;
        params.contactDetail = 0.32f;
        params.sourcePosition = 0.78f;
        params.pickupPosition = 0.20f;
        params.pickupAxis = 0.30f;
        params.sensorMass = 0.20f;
        params.mountStiffness = 0.64f;
        params.conditionerHighpassHz = 8.0f;
        params.airRadiation = 0.07f;
        params.outputGainDb = -5.0f;
        break;
    case 3u:
        params.substrate = AccelerometerSubstrate::Leaf;
        params.excitation = AccelerometerExcitation::Chewing;
        params.eventRateHz = 4.1f;
        params.activity = 0.94f;
        params.force = 0.52f;
        params.texture = 0.90f;
        params.ambientDrive = 0.055f;
        params.size = 0.45f;
        params.damping = 0.78f;
        params.irregularity = 0.72f;
        params.propagationLoss = 0.42f;
        params.contactDetail = 0.92f;
        params.sourcePosition = 0.70f;
        params.pickupPosition = 0.32f;
        params.pickupAxis = 0.58f;
        params.sensorMass = 0.14f;
        params.mountStiffness = 0.70f;
        params.conditionerHighpassHz = 12.0f;
        params.sensorNoise = 0.018f;
        params.airRadiation = 0.035f;
        params.outputGainDb = 8.0f;
        break;
    case 4u:
        params.substrate = AccelerometerSubstrate::Stem;
        params.excitation = AccelerometerExcitation::Tremulation;
        params.eventRateHz = 0.72f;
        params.activity = 0.70f;
        params.force = 0.56f;
        params.texture = 0.26f;
        params.ambientDrive = 0.08f;
        params.size = 0.70f;
        params.damping = 0.38f;
        params.irregularity = 0.22f;
        params.propagationLoss = 0.28f;
        params.contactDetail = 0.18f;
        params.sourcePosition = 0.88f;
        params.pickupPosition = 0.34f;
        params.pickupAxis = 0.90f;
        params.sensorMass = 0.10f;
        params.mountStiffness = 0.78f;
        params.conditionerHighpassHz = 2.0f;
        params.airRadiation = 0.025f;
        params.outputGainDb = -2.0f;
        break;
    case 5u:
        params.substrate = AccelerometerSubstrate::Wire;
        params.excitation = AccelerometerExcitation::Tap;
        params.eventRateHz = 1.15f;
        params.activity = 0.62f;
        params.force = 0.38f;
        params.texture = 0.68f;
        params.ambientDrive = 0.06f;
        params.size = 0.48f;
        params.damping = 0.30f;
        params.irregularity = 0.20f;
        params.propagationLoss = 0.20f;
        params.contactDetail = 0.24f;
        params.sourcePosition = 0.25f;
        params.pickupPosition = 0.73f;
        params.pickupAxis = 0.48f;
        params.sensorMass = 0.28f;
        params.mountStiffness = 0.82f;
        params.airRadiation = 0.08f;
        params.outputGainDb = -7.0f;
        break;
    case 6u:
        params.substrate = AccelerometerSubstrate::Wood;
        params.excitation = AccelerometerExcitation::Scrape;
        params.eventRateHz = 0.58f;
        params.activity = 0.82f;
        params.force = 0.36f;
        params.texture = 0.74f;
        params.ambientDrive = 0.08f;
        params.size = 0.54f;
        params.damping = 0.58f;
        params.irregularity = 0.30f;
        params.propagationLoss = 0.32f;
        params.contactDetail = 0.68f;
        params.sourcePosition = 0.15f;
        params.pickupPosition = 0.61f;
        params.pickupAxis = 0.68f;
        params.sensorMass = 0.08f;
        params.mountStiffness = 0.74f;
        params.airRadiation = 0.10f;
        params.outputGainDb = -5.0f;
        break;
    case 7u:
    default:
        params.substrate = AccelerometerSubstrate::Glass;
        params.excitation = AccelerometerExcitation::Tap;
        params.eventRateHz = 0.84f;
        params.activity = 0.48f;
        params.force = 0.20f;
        params.texture = 0.92f;
        params.ambientDrive = 0.12f;
        params.size = 0.40f;
        params.damping = 0.24f;
        params.irregularity = 0.14f;
        params.propagationLoss = 0.18f;
        params.contactDetail = 0.10f;
        params.sourcePosition = 0.44f;
        params.pickupPosition = 0.79f;
        params.pickupAxis = 0.40f;
        params.sensorMass = 0.035f;
        params.mountStiffness = 0.91f;
        params.airRadiation = 0.06f;
        params.outputGainDb = -9.0f;
        break;
    }
    return sanitizeAccelerometerFieldParams(params);
}

class AccelerometerFieldEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        params_ = sanitizeAccelerometerFieldParams(params_);
        rebuildModel();
        reset();
    }

    void reset()
    {
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
        for (auto& sensor : sensors_) sensor.reset();
        for (auto& mode : modes_) mode.reset();
    }

    void setParams(AccelerometerFieldParams params)
    {
        params_ = sanitizeAccelerometerFieldParams(params);
        if (sampleRate_ > 0.0) rebuildModel();
    }

    AccelerometerFieldParams params() const { return params_; }

    uint32_t outputChannelCount() const
    {
        return params_.outputMode == AccelerometerFieldOutputMode::SensorStems
            ? kAccelerometerFieldSensorCount
            : (params_.ambisonicOrder + 1u) * (params_.ambisonicOrder + 1u);
    }

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
        constexpr float sensorSumScale =
            1.0f / static_cast<float>(kAccelerometerFieldSensorCount);

        for (uint32_t frame = 0u; frame < frames; ++frame) {
            float excitation = proceduralExcitation();
            if (externalExcitation) {
                const float external = std::isfinite(externalExcitation[frame])
                    ? externalExcitation[frame] : 0.0f;
                excitation += external * params_.externalDrive;
            }

            std::array<float, kAccelerometerFieldSensorCount>
                localDisplacement {};
            std::array<float, kAccelerometerFieldSensorCount>
                localVelocity {};
            std::array<float, kAccelerometerFieldSensorCount>
                localAcceleration {};
            std::array<float, kAccelerometerFieldSensorCount>
                radiatedAcceleration {};
            // A chewing closure is heard mainly through the travelling contact
            // texture.  Sending much of it into the resonator makes the leaf
            // read as a struck rigid plate, so retain only a trace of body.
            const float modalExcitation = params_.excitation
                    == AccelerometerExcitation::Chewing
                ? excitation * 0.012f : excitation;
            for (auto& mode : modes_) {
                if (!mode.active) continue;
                const ModeSample response = mode.process(modalExcitation);
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

                if (stems) {
                    if (sensorIndex < activeChannels
                        && outputs[sensorIndex]) {
                        outputs[sensorIndex][frame] = contact;
                    }
                    continue;
                }

                const float fieldSample = lerp(
                    contact, radiation, params_.contactRadiation)
                    * sensorSumScale;
                for (uint32_t channel = 0u;
                    channel < activeChannels; ++channel) {
                    if (outputs[channel]) {
                        outputs[channel][frame] = flushDenormal(
                            outputs[channel][frame]
                            + fieldSample * sensor.basis[channel]);
                    }
                }
            }
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
            index, kAccelerometerFieldModeCount - 1u)].pickupWeight[
                std::min<uint32_t>(
                    sensor, kAccelerometerFieldSensorCount - 1u)];
    }

    float sensorPosition(uint32_t sensor) const
    {
        return sensors_[std::min<uint32_t>(
            sensor, kAccelerometerFieldSensorCount - 1u)].position;
    }

    Vec3 sensorDirection(uint32_t sensor) const
    {
        return sensors_[std::min<uint32_t>(
            sensor, kAccelerometerFieldSensorCount - 1u)].direction;
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
    };

    struct Mode {
        bool active = false;
        float frequencyHz = 0.0f;
        float coefficient = 0.0f;
        float radiusSquared = 0.0f;
        float drive = 0.0f;
        std::array<float, kAccelerometerFieldSensorCount> pickupWeight {};
        std::array<float, kAccelerometerFieldSensorCount> radiationWeight {};
        float velocityNormalization = 1.0f;
        float accelerationNormalization = 1.0f;
        float state1 = 0.0f;
        float state2 = 0.0f;

        void reset()
        {
            state1 = 0.0f;
            state2 = 0.0f;
        }

        ModeSample process(float input)
        {
            const float previous1 = state1;
            const float previous2 = state2;
            float current = coefficient * previous1
                - radiusSquared * previous2 + input * drive;
            if (!std::isfinite(current) || std::fabs(current) > 64.0f) {
                reset();
                current = 0.0f;
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
            return { 48.0f, 1.45f, 1.48f, 0.48f, 0.48f, 0.34f, 0.020f };
        case AccelerometerSubstrate::BellBronze:
            return { 155.0f, 8.50f, 1.18f, 0.28f, 0.20f, 0.025f, 0.055f };
        case AccelerometerSubstrate::Wood:
            return { 92.0f, 0.90f, 1.34f, 0.52f, 0.62f, 0.14f, 0.038f };
        case AccelerometerSubstrate::Glass:
            return { 210.0f, 4.20f, 1.43f, 0.34f, 0.30f, 0.055f, 0.080f };
        case AccelerometerSubstrate::Wire:
            return { 82.0f, 2.30f, 1.00f, 0.42f, 0.38f, 0.42f, 0.012f };
        case AccelerometerSubstrate::Leaf:
        default:
            return { 28.0f, 0.14f, 1.58f, 0.58f, 0.82f, 0.72f, 0.025f };
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
        if (params_.substrate == AccelerometerSubstrate::BellBronze) {
            const uint32_t pair = index / 2u;
            const float order = static_cast<float>(pair + 2u);
            const float phase = orthogonal ? 0.25f : 0.0f;
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
            sensor.position = clamp(
                0.04f + 0.92f * unit
                    + (params_.pickupPosition - 0.5f) * 0.18f,
                0.02f, 0.98f);
            sensor.axis = clamp(params_.pickupAxis
                    + (unit - 0.5f) * 0.72f,
                0.0f, 1.0f);

            float azimuthOffset = (unit - 0.5f) * 150.0f;
            float elevationOffset = std::sin(
                kAccelerometerFieldTwoPi * unit) * 24.0f;
            if (params_.substrate == AccelerometerSubstrate::BellBronze
                || params_.substrate == AccelerometerSubstrate::Glass) {
                azimuthOffset = (unit - 0.5f) * 360.0f;
                elevationOffset = std::sin(
                    kAccelerometerFieldTwoPi * unit) * 18.0f;
            } else if (params_.substrate == AccelerometerSubstrate::Stem
                || params_.substrate == AccelerometerSubstrate::Wire) {
                azimuthOffset = (unit - 0.5f) * 105.0f;
                elevationOffset = (unit - 0.5f) * 72.0f;
            }
            sensor.direction = directionFromAed(
                params_.fieldAzimuthDeg
                    + azimuthOffset * params_.spatialExtent,
                clamp(params_.fieldElevationDeg
                        + elevationOffset * params_.spatialExtent,
                    -89.0f, 89.0f));
            const auto fullBasis = acnSn3dBasis7(sensor.direction);
            for (uint32_t channel = 0u;
                channel < kAccelerometerFieldMaxChannels; ++channel) {
                sensor.basis[channel] = fullBasis[channel];
            }
        }

        for (uint32_t index = 0u;
            index < kAccelerometerFieldModeCount; ++index) {
            Mode& mode = modes_[index];
            const float modeProgress = static_cast<float>(index)
                / static_cast<float>(kAccelerometerFieldModeCount - 1u);
            const float irregular = hashSigned(
                params_.seed ^ (index + 1u) * 0x9e3779b9u);
            const float nominal = profile.baseFrequencyHz * sizeScale
                * nominalModeRatio(index, profile)
                * std::exp2(irregular * params_.irregularity * 0.10f);
            const float addedMass = params_.sensorMass
                * profile.massSensitivity * (0.72f + 0.55f * modeProgress);
            const float frequency = nominal / std::sqrt(1.0f + addedMass);
            mode.frequencyHz = frequency;
            mode.active = frequency >= 5.0f && frequency < nyquistLimit;
            if (!mode.active) {
                mode.reset();
                mode.coefficient = 0.0f;
                mode.radiusSquared = 0.0f;
                mode.drive = 0.0f;
                mode.pickupWeight.fill(0.0f);
                mode.radiationWeight.fill(0.0f);
                continue;
            }

            const float decayScale = std::exp2((0.5f - params_.damping) * 4.2f);
            const float decay = std::max(0.015f,
                profile.baseDecaySeconds * decayScale
                / std::pow(1.0f + modeProgress * 5.0f, profile.decayFalloff));
            const float radius = std::exp(
                -1.0f / (decay * static_cast<float>(sampleRate_)));
            const float omega = kAccelerometerFieldTwoPi
                * frequency / static_cast<float>(sampleRate_);
            mode.coefficient = 2.0f * radius * std::cos(omega);
            mode.radiusSquared = radius * radius;

            const float sourceShape = modeShape(
                index, params_.sourcePosition, false);
            const float modalGain = 1.0f
                / std::pow(1.0f + static_cast<float>(index), profile.modeFalloff);
            mode.drive = 0.0045f * modalGain * sourceShape;
            const float radiationEfficiency = std::sqrt(
                frequency / (frequency + 720.0f));
            for (uint32_t sensorIndex = 0u;
                sensorIndex < kAccelerometerFieldSensorCount; ++sensorIndex) {
                const Sensor& sensor = sensors_[sensorIndex];
                const float axisRadians = sensor.axis
                    * 1.57079632679489661923f;
                const float pickupShape = std::cos(axisRadians) * modeShape(
                    index, sensor.position, false)
                    + std::sin(axisRadians) * modeShape(
                        index, sensor.position, true);
                const float distance = std::fabs(
                    params_.sourcePosition - sensor.position);
                const float transmission = std::exp(-distance
                    * params_.propagationLoss
                    * (0.35f + 3.2f * modeProgress));
                mode.pickupWeight[sensorIndex] = pickupShape * transmission;
                mode.radiationWeight[sensorIndex] = modeShape(
                    index, sensor.position, false)
                    * modalGain * radiationEfficiency
                    * (0.58f + 0.42f * std::fabs(sourceShape));
            }
            mode.velocityNormalization = velocityNormalization;
            mode.accelerationNormalization = accelerationNormalization;
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
        eventPosition_ = clamp(params_.sourcePosition
                + randomSigned() * (0.025f + 0.24f * params_.irregularity),
            0.01f, 0.99f);
        float strength = params_.force * (0.65f + 0.55f * randomAccent);
        float duration = 0.012f;
        switch (params_.excitation) {
        case AccelerometerExcitation::Footsteps:
            duration = 0.010f + 0.022f * params_.texture;
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
            duration = 0.075f + 0.34f * (1.0f - params_.texture * 0.55f);
            break;
        case AccelerometerExcitation::Tremulation:
            duration = 0.16f + 0.62f * (1.0f - params_.texture * 0.40f);
            break;
        case AccelerometerExcitation::Tap:
            duration = 0.0015f + 0.012f * (1.0f - params_.texture);
            break;
        case AccelerometerExcitation::Ambient:
        default:
            duration = 0.016f + 0.090f * (1.0f - params_.texture);
            break;
        }
        eventEnvelope_ = std::max(eventEnvelope_, strength);
        eventDecay_ = std::exp(-1.0f / std::max(1.0f, duration * sr));
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
                secondaryImpact = true;
            }
        }

        const float toneFrequency = 34.0f + params_.texture * 310.0f;
        eventTonePhase_ += toneFrequency / static_cast<float>(sampleRate_);
        eventTonePhase_ -= std::floor(eventTonePhase_);
        const float tone = std::sin(
            kAccelerometerFieldTwoPi * eventTonePhase_);

        if (eventEnvelope_ > 1.0e-7f) {
            float gesture = 0.0f;
            switch (params_.excitation) {
            case AccelerometerExcitation::Footsteps:
                gesture = 0.58f + noise * (0.18f + 0.52f * params_.texture);
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

                chewingGrainPhase_ += (34.0f + 52.0f * params_.texture)
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
                        * (progress * (1.5f + 1.8f * params_.texture) + 0.17f));
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
                gesture = ambientFast_ * 5.5f + noise * params_.texture * 0.46f;
                break;
            case AccelerometerExcitation::Tremulation:
                gesture = tone * (0.84f - 0.28f * params_.texture)
                    + ambientFast_ * params_.texture * 2.2f;
                break;
            case AccelerometerExcitation::Tap:
                gesture = 0.86f + noise * params_.texture * 0.24f;
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
    AccelerometerFieldParams params_ {};
    std::array<Mode, kAccelerometerFieldModeCount> modes_ {};
    std::array<Sensor, kAccelerometerFieldSensorCount> sensors_ {};
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
};

} // namespace s3g
