#pragma once

#include "s3g_ambi_field_listener.h"
#include "s3g_math.h"
#include "s3g_realtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace s3g {

constexpr uint32_t kAccelerometerFieldModeCount = 24u;
// One invariant modal pool is divided between the active bodies. Four bodies
// receive 24 modes each; eight receive 12 each. Changing the body count never
// creates an unbounded bank of complete resonators on the audio thread.
constexpr uint32_t kAccelerometerFieldModeBudget = 96u;
constexpr uint32_t kAccelerometerFieldPresetCount = 25u;
constexpr uint32_t kAccelerometerFieldSensorCount = 8u;
constexpr uint32_t kAccelerometerFieldSkinPatchCount = 4u;
constexpr uint32_t kAccelerometerFieldMinBodyCount = 4u;
constexpr uint32_t kAccelerometerFieldMaxBodyCount = 8u;
constexpr uint32_t kAccelerometerFieldMaxOrder = 3u;
constexpr uint32_t kAccelerometerFieldMaxChannels = 16u;
constexpr float kAccelerometerFieldTwoPi = 6.28318530717958647692f;
constexpr uint32_t kAccelerometerFieldDefaultSeed = 0x6d2b79f5u;

enum class AccelerometerFieldOutputMode : uint32_t {
    Ambisonic = 0u,
    LegacySensorStems,
    BodyStems,
    Count,
};

enum class AccelerometerSubstrate : uint32_t {
    Leaf = 0u,
    Stem = 1u,
    TieredBronze = 2u,
    Wood = 3u,
    Glass = 4u,
    Wire = 5u,
    DryLeaf = 6u,
    PaperCardboard = 7u,
    ShellChitin = 8u,
    PolymerMembrane = 9u,
    DeepBronze = 10u,
    BroadBronze = 11u,
    BrightBronze = 12u,
    CarbonLaminate = 13u,
    GlassPlate = 14u,
    SteelShell = 15u,
    AluminumPlate = 16u,
    PorcelainShell = 17u,
    PorousEarthenware = 18u,
    SprucePlate = 19u,
    TensionedSkin = 20u,
    LoadedMembrane = 21u,
    CoupledMembrane = 22u,
    CavityMembrane = 23u,
    LooseMembrane = 24u,
    Count = 25u,
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

enum class AccelerometerFieldListenerPickupSet : uint32_t {
    Tetra4 = 0u,
    Cube8,
    Count,
};

struct AccelerometerFieldParams {
    AccelerometerSubstrate substrate = AccelerometerSubstrate::DeepBronze;
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
    // These serialized slots predate the current continuous modal player.
    // They are retained in place for state compatibility. propagationLoss is
    // the global Skin Extent; contactDetail/sourcePosition are the legacy
    // shared Skin Y/X values used when migrating pre-v13 sessions.
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

    // Appended in state version 7. The saved scalar layout above this point is
    // intentionally unchanged so older sessions can be read as a prefix.
    uint32_t bodyCount = 6u;

    // Appended in state version 8. Zero offsets retain the generated
    // constellation exactly. Direct point editing writes per-body AED offsets,
    // so Field Center and Body Spread remain useful after a manual edit.
    std::array<float, kAccelerometerFieldMaxBodyCount>
        bodyAzimuthOffsetDeg {};
    std::array<float, kAccelerometerFieldMaxBodyCount>
        bodyElevationOffsetDeg {};
    std::array<float, kAccelerometerFieldMaxBodyCount> bodyDistance {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};

    // Appended in state version 9. The listener body is fixed in world space
    // and remains independent of the editable modal-body constellation.
    AccelerometerFieldListenerPickupSet listenerPickupSet =
        AccelerometerFieldListenerPickupSet::Cube8;

    // Appended in state version 10. Modal lift is a post-resonator, HOA-linked
    // leveler. It never changes a body's drive, modal state, or listener score.
    float modalLift = 0.65f;

    // Appended in state version 13. Every modal body owns an independent
    // two-dimensional actuator contact. Keeping these arrays at the end
    // preserves the complete byte prefix used by released states.
    std::array<float, kAccelerometerFieldMaxBodyCount> bodySkinX {{
        0.72f, 0.72f, 0.72f, 0.72f, 0.72f, 0.72f, 0.72f, 0.72f,
    }};
    std::array<float, kAccelerometerFieldMaxBodyCount> bodySkinY {{
        0.18f, 0.18f, 0.18f, 0.18f, 0.18f, 0.18f, 0.18f, 0.18f,
    }};
};

inline void initializeAccelerometerFieldBodySkinsFromLegacy(
    AccelerometerFieldParams& params)
{
    params.bodySkinX.fill(params.sourcePosition);
    params.bodySkinY.fill(params.contactDetail);
}

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
    params.bodyCount = std::clamp<uint32_t>(params.bodyCount,
        kAccelerometerFieldMinBodyCount, kAccelerometerFieldMaxBodyCount);
    for (uint32_t body = 0u;
        body < kAccelerometerFieldMaxBodyCount; ++body) {
        params.bodyAzimuthOffsetDeg[body] = finite(
            params.bodyAzimuthOffsetDeg[body], 0.0f, -180.0f, 180.0f);
        params.bodyElevationOffsetDeg[body] = finite(
            params.bodyElevationOffsetDeg[body], 0.0f, -180.0f, 180.0f);
        params.bodyDistance[body] = finite(
            params.bodyDistance[body], 1.0f, 0.15f, 2.0f);
        params.bodySkinX[body] = finite(
            params.bodySkinX[body], params.sourcePosition, 0.0f, 1.0f);
        params.bodySkinY[body] = finite(
            params.bodySkinY[body], params.contactDetail, 0.0f, 1.0f);
    }
    params.listenerPickupSet =
        static_cast<AccelerometerFieldListenerPickupSet>(
            std::min<uint32_t>(
                static_cast<uint32_t>(params.listenerPickupSet),
                static_cast<uint32_t>(
                    AccelerometerFieldListenerPickupSet::Count) - 1u));
    params.modalLift = finite(params.modalLift, 0.65f, 0.0f, 1.0f);
    return params;
}

inline float accelerometerFieldBodyPosition(
    const AccelerometerFieldParams& params, uint32_t body)
{
    const uint32_t count = std::clamp<uint32_t>(params.bodyCount,
        kAccelerometerFieldMinBodyCount, kAccelerometerFieldMaxBodyCount);
    body = std::min<uint32_t>(body, count - 1u);
    const float unit = (static_cast<float>(body) + 0.5f)
        / static_cast<float>(count);
    const float spread = clamp(params.arraySpread, 0.0f, 1.0f);
    constexpr float normalizedSpan = 0.92f;
    const float outerUnit = 0.5f - 0.5f / static_cast<float>(count);
    const float halfSpan = normalizedSpan * outerUnit * spread;
    const float center = lerp(0.02f + halfSpan, 0.98f - halfSpan,
        clamp(params.pickupPosition, 0.0f, 1.0f));
    return clamp(center + (unit - 0.5f) * normalizedSpan * spread,
        0.02f, 0.98f);
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
        { "Deep Field", "Six low alloy bodies sustain coupled rumble and focused partial clusters." },
        { "Quiet Alloy", "Four long-decay bodies disclose a restrained continuous field." },
        { "Tiered Constellation", "Five poised bodies sustain layered and slowly beating overtones." },
        { "Paired Field", "Cross-coupled currents move through a six-body constellation." },
        { "Dense Constellation", "Eight independent bodies form a close, continuously driven cloud." },
        { "Slow Orbit", "Smooth actuation sustains shifting modal families around the listener." },
        { "Bright Drift", "Five compact bright bodies maintain an agile upper field." },
        { "Alloy Current", "A continuous current travels through seven resonant surfaces." },
        { "Bright Focus", "Four bright bodies gather around a concentrated modal center." },
        { "Muted Bloom", "A damped six-body field rises and settles without a transient layer." },
        { "Broad Wind", "The field player balances a slow, breathing eight-body drone." },
        { "Bright Halo", "A sustained high modal halo circulates through five bodies." },
        { "Slow Ceremony", "An even actuator maintains a spacious eight-body drone." },
        { "Carbon Veil", "Orthotropic laminate bodies divide long fiber modes from shorter cross-grain motion." },
        { "Glass Horizon", "Clear plate pairs sustain a sparse, slowly beating transparent field." },
        { "Steel Canopy", "Eight curved steel bodies maintain a dense low shell canopy." },
        { "Aluminum Current", "Light plate families carry a broad responsive current through seven bodies." },
        { "Porcelain Orbit", "Stiff ceramic shells circulate clear inharmonic middle and upper bands." },
        { "Earthen Bloom", "Porous fired bodies gather a rounded low bloom beneath quickly settling overtones." },
        { "Spruce Breath", "Grain-split wooden plates breathe across a continuously sustained diffuse field." },
        { "Tension Veil", "Tension-dominated circular bodies sustain clear radial families and lightly split angular pairs." },
        { "Loaded Drift", "Mode-selective loading draws a darker membrane field through uneven low and middle branches." },
        { "Coupled Current", "Paired membrane lattices exchange emphasis across a slowly moving eight-body current." },
        { "Cavity Breath", "Air-loaded branches join a low membrane field to a broad breathing radiation path." },
        { "Loose Horizon", "Uneven low-tension bodies spread soft unstable clusters across a diffuse horizon." },
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
    params.substrate = AccelerometerSubstrate::DeepBronze;
    // The serialized exciter fields remain neutral for compatibility with
    // older sessions. They no longer participate in synthesis.
    params.excitation = AccelerometerExcitation::Ambient;
    params.readout = AccelerometerReadout::Acceleration;
    params.eventRateHz = 0.01f;
    params.activity = 0.0f;
    params.force = 0.0f;
    params.texture = 0.0f;
    params.ambientDrive = 0.0f;
    params.externalDrive = 0.0f;
    params.size = 0.50f;
    params.damping = 0.08f;
    params.irregularity = 0.025f;
    params.propagationLoss = 0.0f;
    params.contactDetail = 0.50f;
    params.sourcePosition = 0.43f;
    params.pickupPosition = 0.50f;
    params.arraySpread = 0.94f;
    params.pickupAxis = 0.34f;
    params.sensorMass = 0.01f;
    params.mountStiffness = 0.90f;
    params.conditionerHighpassHz = 0.50f;
    params.sensorNoise = 0.0f;
    params.airRadiation = 0.24f;
    params.ambisonicOrder = 3u;
    params.outputMode = AccelerometerFieldOutputMode::Ambisonic;
    params.contactRadiation = 0.52f;
    params.spatialExtent = 0.90f;
    params.fieldAzimuthDeg = 0.0f;
    params.fieldElevationDeg = 0.0f;
    params.outputGainDb = -11.0f;
    params.seed = kAccelerometerFieldDefaultSeed + preset * 0x9e3779b9u;
    params.fieldListenMode = AmbiFieldListenMode::Balance;
    params.fieldListenAmount = 0.64f;
    params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
    params.coupling = 0.72f;
    params.energy = 0.0f;
    params.bodyCount = 6u;
    params.modalLift = 0.65f;

    switch (preset) {
    case 0u: // Deep Field
        break;
    case 1u: // Quiet Alloy
        params.bodyCount = 4u;
        params.substrate = AccelerometerSubstrate::TieredBronze;
        params.size = 0.82f;
        params.damping = 0.12f;
        params.irregularity = 0.10f;
        params.sourcePosition = 0.12f;
        params.pickupPosition = 0.63f;
        params.pickupAxis = 0.18f;
        params.airRadiation = 0.08f;
        params.outputGainDb = -8.0f;
        params.coupling = 0.54f;
        params.fieldListenMode = AmbiFieldListenMode::Follow;
        params.fieldListenAmount = 0.34f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Settle;
        break;
    case 2u: // Tiered Constellation
        params.bodyCount = 5u;
        params.substrate = AccelerometerSubstrate::TieredBronze;
        params.size = 0.68f;
        params.damping = 0.18f;
        params.irregularity = 0.16f;
        params.sourcePosition = 0.82f;
        params.pickupPosition = 0.24f;
        params.pickupAxis = 0.74f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.46f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.52f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 3u: // Paired Field
        params.sourcePosition = 0.72f;
        params.pickupPosition = 0.30f;
        params.arraySpread = 0.78f;
        params.outputGainDb = -13.0f;
        params.coupling = 0.68f;
        params.fieldListenMode = AmbiFieldListenMode::Counter;
        params.fieldListenAmount = 0.58f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Excite;
        break;
    case 4u: // Dense Constellation
        params.bodyCount = 8u;
        params.damping = 0.22f;
        params.irregularity = 0.18f;
        params.sourcePosition = 0.66f;
        params.pickupPosition = 0.38f;
        params.outputGainDb = -15.0f;
        params.coupling = 0.58f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.66f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 5u: // Slow Orbit
        params.size = 0.60f;
        params.damping = 0.28f;
        params.irregularity = 0.12f;
        params.sourcePosition = 0.92f;
        params.pickupAxis = 0.88f;
        params.contactRadiation = 0.38f;
        params.outputGainDb = -13.0f;
        params.coupling = 0.82f;
        params.fieldListenMode = AmbiFieldListenMode::Follow;
        params.fieldListenAmount = 0.60f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Settle;
        break;
    case 6u: // Bright Drift
        params.bodyCount = 5u;
        params.substrate = AccelerometerSubstrate::BrightBronze;
        params.size = 0.40f;
        params.damping = 0.26f;
        params.irregularity = 0.14f;
        params.sourcePosition = 0.48f;
        params.pickupPosition = 0.76f;
        params.spatialExtent = 0.68f;
        params.outputGainDb = -6.0f;
        params.coupling = 0.42f;
        params.fieldListenMode = AmbiFieldListenMode::Counter;
        params.fieldListenAmount = 0.52f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Excite;
        break;
    case 7u: // Alloy Current
        params.bodyCount = 7u;
        params.substrate = AccelerometerSubstrate::TieredBronze;
        params.size = 0.72f;
        params.damping = 0.34f;
        params.irregularity = 0.26f;
        params.sourcePosition = 0.16f;
        params.pickupPosition = 0.62f;
        params.contactRadiation = 0.28f;
        params.outputGainDb = -15.0f;
        params.coupling = 0.44f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.64f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 8u: // Bright Focus
        params.bodyCount = 4u;
        params.substrate = AccelerometerSubstrate::BrightBronze;
        params.size = 0.38f;
        params.damping = 0.12f;
        params.irregularity = 0.08f;
        params.sourcePosition = 0.50f;
        params.pickupPosition = 0.72f;
        params.airRadiation = 0.38f;
        params.outputGainDb = -15.0f;
        params.coupling = 0.64f;
        params.fieldListenMode = AmbiFieldListenMode::Follow;
        params.fieldListenAmount = 0.70f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Excite;
        break;
    case 9u: // Muted Bloom
        params.size = 0.68f;
        params.damping = 0.58f;
        params.irregularity = 0.10f;
        params.sourcePosition = 0.36f;
        params.pickupPosition = 0.58f;
        params.contactRadiation = 0.62f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.52f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.48f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Settle;
        break;
    case 10u: // Broad Wind
        params.bodyCount = 8u;
        params.substrate = AccelerometerSubstrate::BroadBronze;
        params.size = 0.86f;
        params.damping = 0.36f;
        params.irregularity = 0.20f;
        params.sourcePosition = 0.10f;
        params.pickupPosition = 0.52f;
        params.airRadiation = 0.12f;
        params.contactRadiation = 0.22f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.86f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.72f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 11u: // Bright Halo
        params.bodyCount = 5u;
        params.substrate = AccelerometerSubstrate::BrightBronze;
        params.size = 0.32f;
        params.damping = 0.24f;
        params.irregularity = 0.16f;
        params.sourcePosition = 0.54f;
        params.pickupPosition = 0.24f;
        params.arraySpread = 0.72f;
        params.spatialExtent = 0.78f;
        params.outputGainDb = -8.0f;
        params.coupling = 0.60f;
        params.fieldListenMode = AmbiFieldListenMode::Counter;
        params.fieldListenAmount = 0.58f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Excite;
        break;
    case 12u: // Slow Ceremony
        params.bodyCount = 8u;
        params.substrate = AccelerometerSubstrate::BroadBronze;
        params.size = 0.76f;
        params.damping = 0.10f;
        params.irregularity = 0.05f;
        params.sourcePosition = 0.40f;
        params.pickupPosition = 0.56f;
        params.pickupAxis = 0.44f;
        params.outputGainDb = -13.0f;
        params.coupling = 0.76f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.78f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 13u: // Carbon Veil
        params.bodyCount = 6u;
        params.substrate = AccelerometerSubstrate::CarbonLaminate;
        params.size = 0.64f;
        params.damping = 0.42f;
        params.irregularity = 0.045f;
        params.sourcePosition = 0.28f;
        params.pickupPosition = 0.70f;
        params.pickupAxis = 0.62f;
        params.airRadiation = 0.16f;
        params.contactRadiation = 0.34f;
        params.outputGainDb = -8.0f;
        params.coupling = 0.56f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.66f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 14u: // Glass Horizon
        params.bodyCount = 5u;
        params.substrate = AccelerometerSubstrate::GlassPlate;
        params.size = 0.58f;
        params.damping = 0.54f;
        params.irregularity = 0.015f;
        params.sourcePosition = 0.78f;
        params.pickupPosition = 0.22f;
        params.pickupAxis = 0.46f;
        params.airRadiation = 0.42f;
        params.contactRadiation = 0.58f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.38f;
        params.fieldListenMode = AmbiFieldListenMode::Follow;
        params.fieldListenAmount = 0.54f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Settle;
        break;
    case 15u: // Steel Canopy
        params.bodyCount = 8u;
        params.substrate = AccelerometerSubstrate::SteelShell;
        params.size = 0.78f;
        params.damping = 0.40f;
        params.irregularity = 0.035f;
        params.sourcePosition = 0.44f;
        params.pickupPosition = 0.62f;
        params.airRadiation = 0.24f;
        params.contactRadiation = 0.46f;
        params.outputGainDb = -13.0f;
        params.coupling = 0.74f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.76f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 16u: // Aluminum Current
        params.bodyCount = 7u;
        params.substrate = AccelerometerSubstrate::AluminumPlate;
        params.size = 0.58f;
        params.damping = 0.48f;
        params.irregularity = 0.070f;
        params.sourcePosition = 0.68f;
        params.pickupPosition = 0.34f;
        params.pickupAxis = 0.72f;
        params.airRadiation = 0.52f;
        params.contactRadiation = 0.42f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.60f;
        params.fieldListenMode = AmbiFieldListenMode::Counter;
        params.fieldListenAmount = 0.62f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Excite;
        break;
    case 17u: // Porcelain Orbit
        params.bodyCount = 6u;
        params.substrate = AccelerometerSubstrate::PorcelainShell;
        params.size = 0.62f;
        params.damping = 0.55f;
        params.irregularity = 0.025f;
        params.sourcePosition = 0.18f;
        params.pickupPosition = 0.74f;
        params.pickupAxis = 0.30f;
        params.airRadiation = 0.50f;
        params.contactRadiation = 0.64f;
        params.outputGainDb = -10.0f;
        params.coupling = 0.46f;
        params.fieldListenMode = AmbiFieldListenMode::Follow;
        params.fieldListenAmount = 0.60f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 18u: // Earthen Bloom
        params.bodyCount = 5u;
        params.substrate = AccelerometerSubstrate::PorousEarthenware;
        params.size = 0.72f;
        params.damping = 0.44f;
        params.irregularity = 0.20f;
        params.sourcePosition = 0.34f;
        params.pickupPosition = 0.58f;
        params.pickupAxis = 0.40f;
        params.airRadiation = 0.14f;
        params.contactRadiation = 0.68f;
        params.outputGainDb = -5.0f;
        params.coupling = 0.36f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.58f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Settle;
        break;
    case 19u: // Spruce Breath
        params.bodyCount = 8u;
        params.substrate = AccelerometerSubstrate::SprucePlate;
        params.size = 0.66f;
        params.damping = 0.58f;
        params.irregularity = 0.10f;
        params.sourcePosition = 0.30f;
        params.pickupPosition = 0.64f;
        params.pickupAxis = 0.54f;
        params.airRadiation = 0.34f;
        params.contactRadiation = 0.42f;
        params.outputGainDb = -4.0f;
        params.coupling = 0.52f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.74f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 20u: // Tension Veil
        params.bodyCount = 6u;
        params.substrate = AccelerometerSubstrate::TensionedSkin;
        params.size = 0.62f;
        params.damping = 0.40f;
        params.irregularity = 0.025f;
        params.sourcePosition = 0.22f;
        params.contactDetail = 0.36f;
        params.propagationLoss = 0.62f;
        params.pickupPosition = 0.68f;
        params.pickupAxis = 0.36f;
        params.airRadiation = 0.42f;
        params.contactRadiation = 0.48f;
        params.outputGainDb = -6.0f;
        params.coupling = 0.52f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.66f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 21u: // Loaded Drift
        params.bodyCount = 5u;
        params.substrate = AccelerometerSubstrate::LoadedMembrane;
        params.size = 0.74f;
        params.damping = 0.48f;
        params.irregularity = 0.10f;
        params.sourcePosition = 0.12f;
        params.contactDetail = 0.68f;
        params.propagationLoss = 0.42f;
        params.pickupPosition = 0.58f;
        params.pickupAxis = 0.30f;
        params.airRadiation = 0.18f;
        params.contactRadiation = 0.64f;
        params.outputGainDb = -4.0f;
        params.coupling = 0.44f;
        params.fieldListenMode = AmbiFieldListenMode::Follow;
        params.fieldListenAmount = 0.62f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Settle;
        break;
    case 22u: // Coupled Current
        params.bodyCount = 8u;
        params.substrate = AccelerometerSubstrate::CoupledMembrane;
        params.size = 0.68f;
        params.damping = 0.42f;
        params.irregularity = 0.045f;
        params.sourcePosition = 0.33f;
        params.contactDetail = 0.46f;
        params.propagationLoss = 0.76f;
        params.pickupPosition = 0.62f;
        params.pickupAxis = 0.58f;
        params.airRadiation = 0.38f;
        params.contactRadiation = 0.50f;
        params.outputGainDb = -7.0f;
        params.coupling = 0.78f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.78f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 23u: // Cavity Breath
        params.bodyCount = 6u;
        params.substrate = AccelerometerSubstrate::CavityMembrane;
        params.size = 0.80f;
        params.damping = 0.36f;
        params.irregularity = 0.030f;
        params.sourcePosition = 0.42f;
        params.contactDetail = 0.58f;
        params.propagationLoss = 0.68f;
        params.pickupPosition = 0.52f;
        params.pickupAxis = 0.26f;
        params.airRadiation = 0.72f;
        params.contactRadiation = 0.72f;
        params.outputGainDb = -8.0f;
        params.coupling = 0.60f;
        params.fieldListenMode = AmbiFieldListenMode::Balance;
        params.fieldListenAmount = 0.72f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Imprint;
        break;
    case 24u: // Loose Horizon
        params.bodyCount = 7u;
        params.substrate = AccelerometerSubstrate::LooseMembrane;
        params.size = 0.72f;
        params.damping = 0.52f;
        params.irregularity = 0.18f;
        params.sourcePosition = 0.74f;
        params.contactDetail = 0.28f;
        params.propagationLoss = 0.84f;
        params.pickupPosition = 0.36f;
        params.pickupAxis = 0.68f;
        params.airRadiation = 0.12f;
        params.contactRadiation = 0.38f;
        params.outputGainDb = -4.0f;
        params.coupling = 0.82f;
        params.fieldListenMode = AmbiFieldListenMode::Counter;
        params.fieldListenAmount = 0.68f;
        params.fieldListenResponse = AmbiFieldListenerResponse::Settle;
        break;
    default:
        break;
    }
    initializeAccelerometerFieldBodySkinsFromLegacy(params);
    // Factory voices expose the per-body skin immediately. Body 1 stays at
    // the historical preset contact while the other bodies receive a stable
    // spatial pattern; membrane profiles use the full offset range.
    constexpr std::array<float, kAccelerometerFieldMaxBodyCount>
        skinXOffset {{ 0.0f, 0.14f, -0.08f, 0.20f,
            0.04f, -0.15f, 0.10f, -0.02f }};
    constexpr std::array<float, kAccelerometerFieldMaxBodyCount>
        skinYOffset {{ 0.0f, -0.16f, 0.20f, -0.05f,
            -0.19f, 0.05f, 0.16f, -0.11f }};
    const float skinVariation = preset >= 20u ? 1.0f : 0.55f;
    for (uint32_t body = 1u;
        body < kAccelerometerFieldMaxBodyCount; ++body) {
        params.bodySkinX[body] = clamp(params.sourcePosition
            + skinXOffset[body] * skinVariation, 0.0f, 1.0f);
        params.bodySkinY[body] = clamp(params.contactDetail
            + skinYOffset[body] * skinVariation, 0.0f, 1.0f);
    }
    return sanitizeAccelerometerFieldParams(params);
}

// A spatial ensemble of independent modal bodies. The engine deliberately
// owns one fixed pool of resonators: body count changes how that pool is
// divided, never the worst-case work performed by the audio callback.
class AccelerometerFieldEncoder {
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = std::max(1000.0, sampleRate);
        geometrySmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.035f * static_cast<float>(sampleRate_)));
        energyAttackCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.018f * static_cast<float>(sampleRate_)));
        energyReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.72f * static_cast<float>(sampleRate_)));
        // The input is a physical force actuator, not a dry audio lane. A
        // short critically damped entry prevents an impulsive source sample
        // from recreating the removed click layer through modal acceleration.
        externalActuatorSmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.0015f * static_cast<float>(sampleRate_)));
        continuousWeightSmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.004f * static_cast<float>(sampleRate_)));
        // Live character edits retarget resonator poles, excitation weights,
        // pickup weights, and radiation balance. A physical-time morph keeps
        // the existing modal state continuous while those coefficients move.
        modalFrequencySmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.010f * static_cast<float>(sampleRate_)));
        modalDecaySmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.045f * static_cast<float>(sampleRate_)));
        modalWeightSmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.025f * static_cast<float>(sampleRate_)));
        // The sustaining rub is a physical-time process. The previous raw
        // per-sample coefficients moved its noise corner with sample rate,
        // and the ~248 Hz fast branch at 48 kHz exposed a snare-wire texture
        // when OUT revealed the quiet modal field. Retain the slow breathing
        // bandwidth while moving only the fast stochastic force below the
        // metallic-rattle region.
        continuousSlowNoiseCoefficient_ = 1.0f - std::exp(
            -kAccelerometerFieldTwoPi * 9.0f
                / static_cast<float>(sampleRate_));
        continuousFastNoiseCoefficient_ = 1.0f - std::exp(
            -kAccelerometerFieldTwoPi * 90.0f
                / static_cast<float>(sampleRate_));
        outputGainSmoothingCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.025f * static_cast<float>(sampleRate_)));
        levelDetectorAttackCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.045f * static_cast<float>(sampleRate_)));
        levelDetectorReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / (2.5f * static_cast<float>(sampleRate_)));
        liftRiseCoefficient_ = 1.0f - std::exp(
            -1.0f / (1.8f * static_cast<float>(sampleRate_)));
        liftFallCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.025f * static_cast<float>(sampleRate_)));
        outputGuardReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / (0.30f * static_cast<float>(sampleRate_)));
        // This is an input-energy memory, not an output envelope. It gives a
        // struck body its full headroom back gradually, so a long modal tail
        // cannot be force-fed by a dense MIDI stream.
        midiActuationEnergyReleaseCoefficient_ = 1.0f - std::exp(
            -1.0f / (2.0f * static_cast<float>(sampleRate_)));
        params_ = sanitizeAccelerometerFieldParams(params_);
        updateEvolutionIncrements();
        fieldListener_.prepare(sampleRate_);
        fieldListener_.setMemorySeconds(0.42f);
        fieldListener_.setExtendedAnalysisEnabled(true);
        configureFieldListener();
        rebuildModel(true);
        reset();
    }

    void reset()
    {
        externalBody_ = 0u;
        nextRoundRobinBody_ = 0u;
        lastActuatedBody_ = 0u;
        listenerTargetPosition_ = bodyUnitPosition(0u);
        externalLevelEnvelope_ = 0.0f;
        lastPerformancePitchRatio_ = 1.0f;
        couplingSlowSine_ = 0.0f;
        couplingSlowCosine_ = 1.0f;
        couplingFastSine_ = 1.0f;
        couplingFastCosine_ = 0.0f;
        couplingOscillatorRenormalizeCountdown_ =
            kCouplingOscillatorRenormalizeInterval;
        dynamicBodyIndex_ = 0u;
        dynamicCoefficientCountdown_ = 0u;
        continuousWeightCountdown_ = 0u;
        playerActivity_ = 0.0f;
        outputGainSmoothed_ = std::pow(
            10.0f, outputGainTargetDb_.load(
                std::memory_order_relaxed) / 20.0f);
        modalLevelEnvelope_ = 0.0f;
        modalLiftDb_ = modalLiftTarget_.load(
            std::memory_order_relaxed) * 9.0f;
        outputGuardGain_ = 1.0f;
        airRadiationSmoothed_ = params_.airRadiation;
        contactRadiationSmoothed_ = params_.contactRadiation;
        skinExtentSmoothed_ = params_.propagationLoss;
        couplingSmoothed_ = params_.coupling;
        fieldListenAmountSmoothed_ = params_.fieldListenMode
                == AmbiFieldListenMode::Off
            ? 0.0f : params_.fieldListenAmount;
        externalDriveSmoothed_ = params_.externalDrive;
        modalDriveScaleSmoothed_ = profile(params_.substrate).driveScale;
        actuatorDriveEnvelope_.fill(0.0f);
        continuousWeightTarget_.fill(1.0f);
        continuousWeight_.fill(1.0f);
        fieldListener_.reset();
        for (uint32_t body = 0u;
            body < kAccelerometerFieldMaxBodyCount; ++body) {
            bodies_[body].reset(params_.seed, body);
            // Dynamic modal targets are snapped immediately below. Restore
            // the per-body oscillator projections to the same phases as the
            // global coupling oscillators before deriving those targets, so
            // reset is deterministic after a long or heavily modulated run.
            bodies_[body].couplingSlowValue =
                bodies_[body].couplingSlowSin;
            bodies_[body].couplingFastValue =
                bodies_[body].couplingFastCos;
            bodies_[body].snapGeometry(body < activeBodyCount_);
        }
        for (auto& mode : modes_) mode.reset();
        updateAllDynamicCoefficients();
        for (auto& mode : modes_) mode.snapMorphTargets();
    }

    void setParams(AccelerometerFieldParams params)
    {
        const AccelerometerFieldParams sanitized =
            sanitizeAccelerometerFieldParams(params);
        const bool allocationChanged = params_.bodyCount != sanitized.bodyCount;
        const bool listenerChanged =
            params_.listenerPickupSet != sanitized.listenerPickupSet;
        const bool couplingRateChanged =
            params_.coupling != sanitized.coupling;
        const bool rebuild = allocationChanged
            || params_.substrate != sanitized.substrate
            || params_.size != sanitized.size
            || params_.damping != sanitized.damping
            || params_.irregularity != sanitized.irregularity
            || params_.propagationLoss != sanitized.propagationLoss
            || params_.bodySkinX != sanitized.bodySkinX
            || params_.bodySkinY != sanitized.bodySkinY
            || params_.pickupPosition != sanitized.pickupPosition
            || params_.arraySpread != sanitized.arraySpread
            || params_.pickupAxis != sanitized.pickupAxis
            || params_.sensorMass != sanitized.sensorMass
            || params_.seed != sanitized.seed;
        const bool geometryChanged =
            params_.spatialExtent != sanitized.spatialExtent
            || params_.fieldAzimuthDeg != sanitized.fieldAzimuthDeg
            || params_.fieldElevationDeg != sanitized.fieldElevationDeg
            || params_.bodyAzimuthOffsetDeg
                != sanitized.bodyAzimuthOffsetDeg
            || params_.bodyElevationOffsetDeg
                != sanitized.bodyElevationOffsetDeg
            || params_.bodyDistance != sanitized.bodyDistance;
        params_ = sanitized;
        outputGainTargetDb_.store(
            sanitized.outputGainDb, std::memory_order_relaxed);
        modalLiftTarget_.store(
            sanitized.modalLift, std::memory_order_relaxed);
        if (sampleRate_ > 0.0 && rebuild) {
            // A body-count change resets the modal pool below, so there is no
            // sounding state to morph. All other live structural edits keep
            // their current state and move toward new coefficient targets.
            rebuildModel(allocationChanged);
            if (allocationChanged) {
                externalBody_ = std::min<uint32_t>(
                    externalBody_, activeBodyCount_ - 1u);
                nextRoundRobinBody_ %= activeBodyCount_;
                lastActuatedBody_ = std::min<uint32_t>(
                    lastActuatedBody_, activeBodyCount_ - 1u);
                for (auto& mode : modes_) mode.reset();
                for (uint32_t body = 0u;
                    body < kAccelerometerFieldMaxBodyCount; ++body) {
                    bodies_[body].gain = 0.0f;
                    bodies_[body].energyEnvelope = 0.0f;
                    bodies_[body].midiPulsePhase = 1.0f;
                    bodies_[body].midiPulseStrength = 0.0f;
                    bodies_[body].midiActuationEnergy = 0.0f;
                }
            }
        } else if (sampleRate_ > 0.0 && geometryChanged) {
            rebuildGeometry(false);
        }
        if (sampleRate_ > 0.0 && listenerChanged) {
            configureFieldListener();
        }
        if (sampleRate_ > 0.0 && couplingRateChanged) {
            updateEvolutionIncrements();
        }
        continuousWeightCountdown_ = 0u;
    }

    // The CLAP editor can change these two audition-only controls while the
    // audio callback is active. Keeping their targets atomic avoids touching
    // the modal parameter block from the UI thread; process() still performs
    // all smoothing and gain-state updates on the audio thread.
    void setOutputStageTargets(float outputGainDb, float modalLift)
    {
        outputGainTargetDb_.store(clamp(
            std::isfinite(outputGainDb) ? outputGainDb : -9.0f,
            -60.0f, 12.0f), std::memory_order_relaxed);
        modalLiftTarget_.store(clamp(
            std::isfinite(modalLift) ? modalLift : 0.65f,
            0.0f, 1.0f), std::memory_order_relaxed);
    }

    AccelerometerFieldParams params() const { return params_; }
    uint32_t bodyCount() const { return activeBodyCount_; }
    uint32_t activeModeCount() const { return kAccelerometerFieldModeBudget; }

    uint32_t modesForBody(uint32_t body) const
    {
        body = std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u);
        return bodies_[body].modeCount;
    }

    uint32_t outputChannelCount() const
    {
        return params_.outputMode == AccelerometerFieldOutputMode::Ambisonic
            ? (params_.ambisonicOrder + 1u) * (params_.ambisonicOrder + 1u)
            : kAccelerometerFieldMaxBodyCount;
    }

    // MIDI selects one body through the same bounded player used by the
    // continuous actuator. A smooth, zero-endpoint force window enters only
    // the modal bank; there is no parallel click, noise burst, or dry layer.
    // Idle bodies are used before an active pulse is stolen. Each body also
    // owns a leaky actuation-energy reservoir: one full strike consumes its
    // immediate headroom and later notes receive only the recovered energy.
    void strikeMidi(int32_t note, float velocity)
    {
        if (sampleRate_ <= 0.0) prepare(48000.0);
        velocity = clamp(std::isfinite(velocity) ? velocity : 0.0f,
            0.0f, 1.0f);
        if (velocity <= 0.0f) return;
        const uint32_t bodyIndex = selectPlayerBody(true);
        Body& body = bodies_[bodyIndex];

        // Retain useful response at ordinary velocities while putting a soft
        // energy knee under the top of the MIDI range. The old v^0.72 curve
        // expanded mid velocities and made almost every played note a hard
        // strike.
        const float linearVelocity = velocity;
        constexpr float kVelocityEnergyKnee = 1.10f;
        const float conditionedVelocity = linearVelocity / std::sqrt(
            1.0f + kVelocityEnergyKnee
                * linearVelocity * linearVelocity);
        constexpr float kConditionedMaximum = 1.0f
            / 1.4491376746f; // sqrt(1 + kVelocityEnergyKnee)
        const float normalizedVelocity = clamp(
            conditionedVelocity / kConditionedMaximum, 0.0f, 1.0f);
        const float requestedEnergy = normalizedVelocity * normalizedVelocity;
        const float availableEnergy = std::max(
            0.0f, 1.0f - body.midiActuationEnergy);
        const float acceptedEnergy = std::min(
            requestedEnergy, availableEnergy);
        // Do not replace an active force pulse with an inaudibly small
        // retrigger while the reservoir is nearly empty. Four percent energy
        // is a 20-percent amplitude admission threshold.
        if (acceptedEnergy < requestedEnergy * 0.04f) return;
        const float energyAdmission = std::sqrt(acceptedEnergy
            / std::max(requestedEnergy, 1.0e-12f));
        body.midiActuationEnergy = clamp(
            body.midiActuationEnergy + acceptedEnergy, 0.0f, 1.0f);

        body.pitchRatio = clamp(std::exp2(
            (static_cast<float>(std::clamp(note, 0, 127)) - 60.0f) / 12.0f),
            0.125f, 8.0f);
        lastPerformancePitchRatio_ = body.pitchRatio;
        body.midiPulseStrength = conditionedVelocity
            * energyAdmission * 0.55f;
        body.midiPulsePhase = 0.0f;
        const float pulseSeconds = 0.14f
            + 0.22f * (1.0f - params_.damping);
        body.midiPulseIncrement = 1.0f / std::max(
            1.0f, pulseSeconds * static_cast<float>(sampleRate_));
        body.midiSine = 0.0f;
        body.midiCosine = 1.0f;
        const float toneHz = body.modeCount > 0u
            ? modes_[body.modeStart].frequencyHz * body.pitchRatio
            : 80.0f;
        const float increment = kAccelerometerFieldTwoPi * toneHz
            / static_cast<float>(sampleRate_);
        body.midiIncrementSine = std::sin(increment);
        body.midiIncrementCosine = std::cos(increment);
        markActuated(bodyIndex);
        dynamicCoefficientCountdown_ = 0u;
    }

    float performancePitchRatio() const
    {
        return lastPerformancePitchRatio_;
    }

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

        const float outputGainTargetDb = outputGainTargetDb_.load(
            std::memory_order_relaxed);
        const float modalLiftAmount = modalLiftTarget_.load(
            std::memory_order_relaxed);
        const float outputGainTarget = std::pow(
            10.0f, outputGainTargetDb / 20.0f);
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
        const uint32_t activeChannels = std::min<uint32_t>(
            outputChannelCount(), outputChannels);
        const bool stems = params_.outputMode
            != AccelerometerFieldOutputMode::Ambisonic;
        const uint32_t listenerChannels =
            (params_.ambisonicOrder + 1u) * (params_.ambisonicOrder + 1u);
        const float ensembleScale = 1.0f
            / std::sqrt(static_cast<float>(activeBodyCount_));
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            outputGainSmoothed_ = flushDenormal(outputGainSmoothed_
                + outputGainSmoothingCoefficient_
                    * (outputGainTarget - outputGainSmoothed_));
            airRadiationSmoothed_ = flushDenormal(airRadiationSmoothed_
                + modalWeightSmoothingCoefficient_
                    * (params_.airRadiation - airRadiationSmoothed_));
            contactRadiationSmoothed_ = flushDenormal(
                contactRadiationSmoothed_ + modalWeightSmoothingCoefficient_
                    * (params_.contactRadiation
                        - contactRadiationSmoothed_));
            skinExtentSmoothed_ = flushDenormal(skinExtentSmoothed_
                + modalWeightSmoothingCoefficient_
                    * (params_.propagationLoss - skinExtentSmoothed_));
            couplingSmoothed_ = flushDenormal(couplingSmoothed_
                + modalWeightSmoothingCoefficient_
                    * (params_.coupling - couplingSmoothed_));
            const float fieldListenAmountTarget = params_.fieldListenMode
                    == AmbiFieldListenMode::Off
                ? 0.0f : params_.fieldListenAmount;
            fieldListenAmountSmoothed_ = flushDenormal(
                fieldListenAmountSmoothed_ + modalWeightSmoothingCoefficient_
                    * (fieldListenAmountTarget
                        - fieldListenAmountSmoothed_));
            externalDriveSmoothed_ = flushDenormal(externalDriveSmoothed_
                + modalWeightSmoothingCoefficient_
                    * (params_.externalDrive - externalDriveSmoothed_));
            modalDriveScaleSmoothed_ = flushDenormal(
                modalDriveScaleSmoothed_ + modalWeightSmoothingCoefficient_
                    * (profile(params_.substrate).driveScale
                        - modalDriveScaleSmoothed_));
            smoothGeometry();
            advanceEvolution();
            if (dynamicCoefficientCountdown_ == 0u) {
                updateDynamicCoefficients(dynamicBodyIndex_);
                dynamicBodyIndex_ = (dynamicBodyIndex_ + 1u)
                    % activeBodyCount_;
                dynamicCoefficientCountdown_ = 15u;
            } else {
                --dynamicCoefficientCountdown_;
            }

            std::array<float, kAccelerometerFieldMaxBodyCount> bodyDrive {};

            float externalForce = 0.0f;
            if (externalExcitation) {
                const float external = std::isfinite(externalExcitation[frame])
                    ? externalExcitation[frame] : 0.0f;
                externalForce = external * externalDriveSmoothed_;
            }
            routeExternalActuator(externalForce);
            for (uint32_t bodyIndex = 0u;
                bodyIndex < activeBodyCount_; ++bodyIndex) {
                Body& body = bodies_[bodyIndex];
                const float target = bodyIndex == externalBody_
                    ? externalForce : 0.0f;
                body.externalActuatorStage1 +=
                    externalActuatorSmoothingCoefficient_
                    * (target - body.externalActuatorStage1);
                body.externalActuatorStage2 +=
                    externalActuatorSmoothingCoefficient_
                    * (body.externalActuatorStage1
                        - body.externalActuatorStage2);
                bodyDrive[bodyIndex] += body.externalActuatorStage2;
            }

            for (uint32_t bodyIndex = 0u;
                bodyIndex < activeBodyCount_; ++bodyIndex) {
                Body& body = bodies_[bodyIndex];
                body.midiActuationEnergy = flushDenormal(
                    body.midiActuationEnergy
                    + midiActuationEnergyReleaseCoefficient_
                        * (0.0f - body.midiActuationEnergy));
                if (body.midiPulsePhase < 1.0f) {
                    const float phase = clamp(
                        body.midiPulsePhase, 0.0f, 1.0f);
                    const float window = 16.0f * phase * phase
                        * (1.0f - phase) * (1.0f - phase);
                    const float nextSine = body.midiSine
                            * body.midiIncrementCosine
                        + body.midiCosine * body.midiIncrementSine;
                    const float nextCosine = body.midiCosine
                            * body.midiIncrementCosine
                        - body.midiSine * body.midiIncrementSine;
                    body.midiSine = flushDenormal(nextSine);
                    body.midiCosine = flushDenormal(nextCosine);
                    bodyDrive[bodyIndex] += body.midiPulseStrength
                        * window * body.midiSine;
                    body.midiPulsePhase += body.midiPulseIncrement;
                    if (body.midiPulsePhase >= 1.0f) {
                        body.midiPulsePhase = 1.0f;
                        body.midiPulseStrength = 0.0f;
                    }
                }
            }
            addContinuousPlayerDrive(bodyDrive);
            for (uint32_t bodyIndex = 0u;
                bodyIndex < kAccelerometerFieldMaxBodyCount; ++bodyIndex) {
                const float target = bodyIndex < activeBodyCount_
                    ? std::fabs(bodyDrive[bodyIndex]) : 0.0f;
                const float coefficient = target
                        > actuatorDriveEnvelope_[bodyIndex]
                    ? energyAttackCoefficient_ : energyReleaseCoefficient_;
                actuatorDriveEnvelope_[bodyIndex] = flushDenormal(
                    actuatorDriveEnvelope_[bodyIndex]
                    + coefficient * (target
                        - actuatorDriveEnvelope_[bodyIndex]));
            }

            std::array<float, kAccelerometerFieldMaxBodyCount> displacement {};
            std::array<float, kAccelerometerFieldMaxBodyCount> velocity {};
            std::array<float, kAccelerometerFieldMaxBodyCount> acceleration {};
            std::array<float, kAccelerometerFieldMaxBodyCount> radiation {};
            std::array<std::array<float,
                    kAccelerometerFieldSkinPatchCount>,
                kAccelerometerFieldMaxBodyCount> skinRadiation {};
            for (auto& mode : modes_) {
                if (!mode.active || mode.body >= activeBodyCount_) continue;
                mode.smoothMorph(modalFrequencySmoothingCoefficient_,
                    modalDecaySmoothingCoefficient_,
                    modalWeightSmoothingCoefficient_);
                ModeSample response = mode.process(
                    bodyDrive[mode.body] * modalDriveScaleSmoothed_
                        * mode.transpositionGain);
                const float modulation = modalCouplingValue(mode);
                const float amplitudeDepth = couplingSmoothed_
                    * (0.10f + 0.28f * (1.0f - mode.pairProgress));
                const float amplitudeScale = clamp(
                    1.0f - amplitudeDepth * modulation, 0.38f, 1.38f)
                    * mode.transpositionGain;
                displacement[mode.body] += response.displacement
                    * mode.pickupWeight * amplitudeScale;
                velocity[mode.body] += response.velocity
                    * mode.pickupWeight * amplitudeScale;
                acceleration[mode.body] += response.acceleration
                    * mode.pickupWeight * amplitudeScale;
                radiation[mode.body] += response.acceleration
                    * mode.radiationWeight * amplitudeScale;
                for (uint32_t patch = 0u;
                    patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                    skinRadiation[mode.body][patch] += response.acceleration
                        * mode.skinRadiationWeight[patch] * amplitudeScale;
                }
            }

            std::array<float, kAccelerometerFieldMaxChannels> listenerHoa {};
            float bodyPower = 0.0f;
            for (uint32_t bodyIndex = 0u;
                bodyIndex < activeBodyCount_; ++bodyIndex) {
                Body& body = bodies_[bodyIndex];
                float contact = acceleration[bodyIndex];
                if (params_.readout == AccelerometerReadout::Velocity) {
                    contact = velocity[bodyIndex];
                } else if (params_.readout
                    == AccelerometerReadout::Displacement) {
                    contact = displacement[bodyIndex];
                }

                body.mountLowpass += mountCoefficient
                    * (contact - body.mountLowpass);
                const float conditioned = body.mountLowpass
                    - body.conditionerInput
                    + highpassPole * body.conditionerOutput;
                body.conditionerInput = body.mountLowpass;
                body.conditionerOutput = flushDenormal(conditioned);
                // These are fixed pickup-protection curves, not output gain.
                // Their calibration remains inside each body while audition
                // level is applied once, after Listener analysis and HOA
                // encoding. Raising OUT therefore no longer hardens the body.
                contact = std::tanh(body.conditionerOutput * 0.72f);

                body.radiationLowpass += airCoefficient
                    * (radiation[bodyIndex] - body.radiationLowpass);
                const float radiated = std::tanh(body.radiationLowpass
                    * airRadiationSmoothed_ * 0.46f);
                const float bodySample = lerp(
                    contact, radiated, contactRadiationSmoothed_) * body.gain;
                std::array<float, kAccelerometerFieldSkinPatchCount>
                    skinSample {};
                for (uint32_t patch = 0u;
                    patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                    body.skinRadiationLowpass[patch] += airCoefficient
                        * (skinRadiation[bodyIndex][patch]
                            - body.skinRadiationLowpass[patch]);
                    const float patchRadiated = std::tanh(
                        body.skinRadiationLowpass[patch]
                            * airRadiationSmoothed_ * 0.46f);
                    skinSample[patch] = lerp(contact, patchRadiated,
                        contactRadiationSmoothed_) * body.gain;
                }
                bodyPower += bodySample * bodySample;
                const float envelopeInput = std::fabs(bodySample);
                const float envelopeCoefficient = envelopeInput
                        > body.energyEnvelope
                    ? energyAttackCoefficient_ : energyReleaseCoefficient_;
                body.energyEnvelope += envelopeCoefficient
                    * (envelopeInput - body.energyEnvelope);

                const float encoded = bodySample * ensembleScale;
                std::array<float, kAccelerometerFieldMaxChannels>
                    skinEncoded {};
                for (uint32_t channel = 0u;
                    channel < listenerChannels; ++channel) {
                    float distributed = 0.0f;
                    for (uint32_t patch = 0u;
                        patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                        distributed += skinSample[patch]
                            * body.skinBasis[patch][channel];
                    }
                    distributed *= ensembleScale
                        / static_cast<float>(
                            kAccelerometerFieldSkinPatchCount);
                    skinEncoded[channel] = lerp(
                        encoded * body.basis[channel], distributed,
                        skinExtentSmoothed_);
                    listenerHoa[channel] = flushDenormal(listenerHoa[channel]
                        + skinEncoded[channel]);
                }
                if (stems) {
                    if (bodyIndex < activeChannels && outputs[bodyIndex]) {
                        outputs[bodyIndex][frame] = bodySample;
                    }
                } else {
                    for (uint32_t channel = 0u;
                        channel < activeChannels; ++channel) {
                        if (outputs[channel]) {
                            outputs[channel][frame] = flushDenormal(
                                outputs[channel][frame]
                                + skinEncoded[channel]);
                        }
                    }
                }
            }
            // Listener / Actuator hears the fixed-calibration body field. It
            // is intentionally upstream of Lift, OUT, and peak protection so
            // changing audition level cannot redirect or excite the player.
            fieldListener_.processFrame(listenerHoa.data(), listenerChannels);

            const float bodyRms = std::sqrt(bodyPower
                / static_cast<float>(activeBodyCount_));
            const float detectorCoefficient = bodyRms > modalLevelEnvelope_
                ? levelDetectorAttackCoefficient_
                : levelDetectorReleaseCoefficient_;
            modalLevelEnvelope_ = flushDenormal(modalLevelEnvelope_
                + detectorCoefficient * (bodyRms - modalLevelEnvelope_));

            // Lift fills headroom rather than riding the modal tail. The slow
            // detector and slower upward gain motion retain beating and decay;
            // a new loud body lowers makeup quickly. Near silence, upward
            // movement freezes instead of magnifying the end of a resonance.
            constexpr float kLiftTargetAmplitude = 0.10f; // -20 dBFS
            constexpr float kLiftMaximumDb = 18.0f;
            constexpr float kLiftSilenceAmplitude = 0.0015f; // -56.5 dBFS
            float desiredLiftDb = modalLiftDb_;
            if (modalLevelEnvelope_ >= kLiftSilenceAmplitude) {
                const float levelDb = 20.0f * std::log10(
                    std::max(modalLevelEnvelope_, 1.0e-9f));
                const float targetDb = 20.0f * std::log10(
                    kLiftTargetAmplitude);
                desiredLiftDb = clamp(
                    targetDb - levelDb, 0.0f, kLiftMaximumDb)
                    * modalLiftAmount;
            } else {
                desiredLiftDb = std::min(desiredLiftDb,
                    modalLiftAmount * kLiftMaximumDb);
            }

            float rawFramePeak = 0.0f;
            for (uint32_t channel = 0u;
                channel < activeChannels; ++channel) {
                if (outputs[channel]) {
                    rawFramePeak = std::max(rawFramePeak,
                        std::fabs(outputs[channel][frame]));
                }
            }
            if (!stems && rawFramePeak > 1.0e-8f) {
                constexpr float kLiftHeadroom = 0.80f;
                const float headroomDb = 20.0f * std::log10(
                    kLiftHeadroom
                    / (rawFramePeak * outputGainSmoothed_));
                desiredLiftDb = std::min(desiredLiftDb,
                    std::max(0.0f, headroomDb));
            }
            const float liftCoefficient = desiredLiftDb < modalLiftDb_
                ? liftFallCoefficient_ : liftRiseCoefficient_;
            modalLiftDb_ = flushDenormal(modalLiftDb_
                + liftCoefficient * (desiredLiftDb - modalLiftDb_));

            const float liftGain = stems ? 1.0f
                : std::pow(10.0f, modalLiftDb_ / 20.0f);
            const float auditionGain = outputGainSmoothed_ * liftGain;
            float auditionPeak = 0.0f;
            for (uint32_t channel = 0u;
                channel < activeChannels; ++channel) {
                if (!outputs[channel]) continue;
                const float value = outputs[channel][frame] * auditionGain;
                outputs[channel][frame] = std::isfinite(value) ? value : 0.0f;
                auditionPeak = std::max(
                    auditionPeak, std::fabs(outputs[channel][frame]));
            }

            // One thresholded gain protects the complete output frame. A
            // frame below -1 dBFS requests unity; overshoots reduce
            // immediately and recover over 300 ms. The same scalar reaches
            // (or every body stem), preserving spatial and ensemble ratios.
            constexpr float kOutputCeiling = 0.89125094f;
            const float desiredGuardGain = auditionPeak > kOutputCeiling
                ? kOutputCeiling / auditionPeak : 1.0f;
            if (!std::isfinite(outputGuardGain_)
                || desiredGuardGain < outputGuardGain_) {
                outputGuardGain_ = desiredGuardGain;
            } else {
                outputGuardGain_ += outputGuardReleaseCoefficient_
                    * (1.0f - outputGuardGain_);
            }
            outputGuardGain_ = clamp(std::min(
                outputGuardGain_, desiredGuardGain), 0.0f, 1.0f);
            for (uint32_t channel = 0u;
                channel < activeChannels; ++channel) {
                if (outputs[channel]) {
                    outputs[channel][frame] = flushDenormal(
                        outputs[channel][frame] * outputGuardGain_);
                }
            }
        }
    }

    float modeFrequencyHz(uint32_t index) const
    {
        return bodyModeFrequencyHz(0u, index);
    }

    float modeDecaySeconds(uint32_t index) const
    {
        const Body& body = bodies_[0u];
        if (body.modeCount == 0u || sampleRate_ <= 0.0) return 0.0f;
        index = std::min<uint32_t>(index, body.modeCount - 1u);
        const double radius = modes_[body.modeStart + index].radiusTarget;
        if (!(radius > 0.0 && radius < 1.0)) return 0.0f;
        return static_cast<float>(-1.0 / (std::log(radius) * sampleRate_));
    }

    float bodyModeFrequencyHz(uint32_t bodyIndex, uint32_t index) const
    {
        bodyIndex = std::min<uint32_t>(bodyIndex, activeBodyCount_ - 1u);
        const Body& body = bodies_[bodyIndex];
        if (body.modeCount == 0u) return 0.0f;
        index = std::min<uint32_t>(index, body.modeCount - 1u);
        const Mode& mode = modes_[body.modeStart + index];
        return mode.frequencyHz * body.pitchRatio;
    }

    float bodyPitchRatio(uint32_t bodyIndex) const
    {
        return bodies_[std::min<uint32_t>(
            bodyIndex, activeBodyCount_ - 1u)].pitchRatio;
    }

    float modePickupWeight(uint32_t index, uint32_t body = 0u) const
    {
        body = std::min<uint32_t>(body, activeBodyCount_ - 1u);
        const Body& target = bodies_[body];
        if (target.modeCount == 0u) return 0.0f;
        index = std::min<uint32_t>(index, target.modeCount - 1u);
        return modes_[target.modeStart + index].pickupWeightTarget;
    }

    float modeDriveWeight(uint32_t index, uint32_t body = 0u) const
    {
        body = std::min<uint32_t>(body, activeBodyCount_ - 1u);
        const Body& target = bodies_[body];
        if (target.modeCount == 0u) return 0.0f;
        index = std::min<uint32_t>(index, target.modeCount - 1u);
        return modes_[target.modeStart + index].driveTarget;
    }

    float sensorPosition(uint32_t body) const { return bodyPosition(body); }
    float bodyPosition(uint32_t body) const
    {
        return bodies_[std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u)].targetPosition;
    }

    Vec3 sensorDirection(uint32_t body) const { return bodyDirection(body); }
    Vec3 bodyDirection(uint32_t body) const
    {
        return bodies_[std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u)].targetDirection;
    }
    Vec3 skinPatchDirection(uint32_t body, uint32_t patch) const
    {
        body = std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u);
        patch = std::min<uint32_t>(
            patch, kAccelerometerFieldSkinPatchCount - 1u);
        return bodies_[body].targetSkinDirection[patch];
    }
    float bodyDistance(uint32_t body) const
    {
        return bodies_[std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u)].targetDistance;
    }
    float currentBodyDistance(uint32_t body) const
    {
        return bodies_[std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u)].distance;
    }

    float currentSensorPosition(uint32_t body) const
    {
        return currentBodyPosition(body);
    }
    float currentBodyPosition(uint32_t body) const
    {
        return bodies_[std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u)].position;
    }

    float bodyEnergy(uint32_t body) const
    {
        return bodies_[std::min<uint32_t>(
            body, kAccelerometerFieldMaxBodyCount - 1u)].energyEnvelope;
    }

    uint32_t lastActuatedBody() const { return lastActuatedBody_; }
    float listenerActivity() const
    {
        return std::max(fieldListener_.activity(), playerActivity_);
    }
    float fieldListenerActivity() const { return fieldListener_.activity(); }
    float actuatorActivity() const
    {
        float peak = 0.0f;
        for (float envelope : actuatorDriveEnvelope_) {
            peak = std::max(peak, envelope);
        }
        return peak / (peak + 0.04f);
    }
    float listenerTargetPosition() const { return listenerTargetPosition_; }
    uint32_t listenerPickupCount() const { return fieldListener_.count(); }
    Vec3 listenerPickupDirection(uint32_t pickup) const
    {
        return fieldListener_.direction(pickup);
    }
    float listenerPickupEnergy(uint32_t pickup) const
    {
        return fieldListener_.envelope(pickup);
    }
    float actuatorBodyDrive(uint32_t body) const
    {
        return body < kAccelerometerFieldMaxBodyCount
            ? actuatorDriveEnvelope_[body] : 0.0f;
    }
    float currentModalLiftDb() const { return modalLiftDb_; }
    float currentOutputGuardGain() const { return outputGuardGain_; }

private:
    struct ModalProfile {
        float baseFrequencyHz;
        float baseDecaySeconds;
        float modeFalloff;
        float decayFalloff;
        float massSensitivity;
        float driveScale;
        float radiationCornerHz;
        float radiationExponent;
        float radiationGain;
        // Membrane profiles can raise their modal frequencies slightly under
        // actuator load. Rigid plate, shell, and legacy families keep this at
        // zero, preserving their released behavior exactly.
        float tensionHardening;
    };

    struct ModeSample {
        float displacement = 0.0f;
        float velocity = 0.0f;
        float acceleration = 0.0f;
    };

    struct Mode {
        bool active = false;
        uint32_t body = 0u;
        uint32_t localIndex = 0u;
        float frequencyHz = 0.0f;
        float pairProgress = 0.0f;
        // Long drone poles sit within a few float ULPs of unity. Keep their
        // morph state in double precision so a physical-time decay ramp does
        // not stall or advance in audible quantized steps.
        double coefficient = 0.0;
        double coefficientNorm = 1.0;
        double coefficientNormTarget = 1.0;
        double radius = 0.0;
        double radiusTarget = 0.0;
        double radiusSquared = 0.0;
        float drive = 0.0f;
        float driveTarget = 0.0f;
        float pickupWeight = 0.0f;
        float pickupWeightTarget = 0.0f;
        float radiationWeight = 0.0f;
        float radiationWeightTarget = 0.0f;
        std::array<float, kAccelerometerFieldSkinPatchCount>
            skinRadiationWeight {};
        std::array<float, kAccelerometerFieldSkinPatchCount>
            skinRadiationWeightTarget {};
        float transpositionGain = 1.0f;
        float transpositionGainTarget = 1.0f;
        float velocityNormalization = 1.0f;
        float accelerationNormalization = 1.0f;
        float state1 = 0.0f;
        float state2 = 0.0f;

        void reset()
        {
            state1 = 0.0f;
            state2 = 0.0f;
        }

        void snapMorphTargets()
        {
            coefficientNorm = coefficientNormTarget;
            radius = radiusTarget;
            coefficient = 2.0 * radius * std::clamp(
                coefficientNorm, -1.0, 1.0);
            radiusSquared = radius * radius;
            drive = driveTarget;
            pickupWeight = pickupWeightTarget;
            radiationWeight = radiationWeightTarget;
            skinRadiationWeight = skinRadiationWeightTarget;
            transpositionGain = transpositionGainTarget;
        }

        void smoothMorph(float frequencyAmount, float decayAmount,
            float weightAmount)
        {
            coefficientNorm += frequencyAmount
                * (coefficientNormTarget - coefficientNorm);
            radius = flushDenormal(radius
                + decayAmount * (radiusTarget - radius));
            coefficient = 2.0 * radius * std::clamp(
                coefficientNorm, -1.0, 1.0);
            radiusSquared = radius * radius;
            drive = flushDenormal(drive
                + weightAmount * (driveTarget - drive));
            pickupWeight = flushDenormal(pickupWeight + weightAmount
                * (pickupWeightTarget - pickupWeight));
            radiationWeight = flushDenormal(radiationWeight + weightAmount
                * (radiationWeightTarget - radiationWeight));
            for (uint32_t patch = 0u;
                patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                skinRadiationWeight[patch] = flushDenormal(
                    skinRadiationWeight[patch] + weightAmount
                        * (skinRadiationWeightTarget[patch]
                            - skinRadiationWeight[patch]));
            }
            transpositionGain = flushDenormal(transpositionGain + weightAmount
                * (transpositionGainTarget - transpositionGain));
        }

        ModeSample process(float input)
        {
            const float previous1 = state1;
            const float previous2 = state2;
            // Continuous excitation can accumulate far more displacement than
            // an isolated strike in these deliberately long modal decays. Add
            // amplitude-dependent pole damping before the state reaches the
            // safety range. This controls modal gain without clipping the
            // waveform or changing the small-signal decay.
            const float stateMagnitude = std::max(
                std::fabs(previous1), std::fabs(previous2));
            const float overload = std::max(
                0.0f, stateMagnitude * (1.0f / 32.0f) - 1.0f);
            const float nonlinearDamping = 1.0f
                / (1.0f + 0.0010f * overload * overload);
            float current = coefficient * nonlinearDamping * previous1
                - radiusSquared * nonlinearDamping * nonlinearDamping
                    * previous2
                + input * drive;
            if (!std::isfinite(current)) {
                reset();
                return {};
            }
            // A remote C1-continuous safety knee replaces the old hard reset.
            // Normal modal gain control above keeps this branch inactive.
            constexpr float kStateKnee = 256.0f;
            constexpr float kStateLimit = 1024.0f;
            const float magnitude = std::fabs(current);
            if (magnitude > kStateKnee) {
                const float excess = magnitude - kStateKnee;
                const float range = kStateLimit - kStateKnee;
                current = std::copysign(kStateKnee
                        + excess / (1.0f + excess / range),
                    current);
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

    struct Body {
        uint32_t modeStart = 0u;
        uint32_t modeCount = 0u;
        float position = 0.5f;
        float targetPosition = 0.5f;
        Vec3 direction { 1.0f, 0.0f, 0.0f };
        Vec3 targetDirection { 1.0f, 0.0f, 0.0f };
        float distance = 1.0f;
        float targetDistance = 1.0f;
        std::array<float, kAccelerometerFieldMaxChannels> basis {};
        std::array<float, kAccelerometerFieldMaxChannels> targetBasis {};
        std::array<Vec3, kAccelerometerFieldSkinPatchCount> skinDirection {};
        std::array<Vec3, kAccelerometerFieldSkinPatchCount>
            targetSkinDirection {};
        std::array<std::array<float, kAccelerometerFieldMaxChannels>,
            kAccelerometerFieldSkinPatchCount> skinBasis {};
        std::array<std::array<float, kAccelerometerFieldMaxChannels>,
            kAccelerometerFieldSkinPatchCount> targetSkinBasis {};
        float gain = 0.0f;
        float targetGain = 0.0f;
        float pitchRatio = 1.0f;
        float identityPitch = 1.0f;
        float couplingPhase = 0.0f;
        float couplingSlowSin = 0.0f;
        float couplingSlowCos = 1.0f;
        float couplingFastSin = 0.0f;
        float couplingFastCos = 1.0f;
        float couplingSlowValue = 0.0f;
        float couplingFastValue = 0.0f;
        float midiPulsePhase = 1.0f;
        float midiPulseIncrement = 0.0f;
        float midiPulseStrength = 0.0f;
        float midiActuationEnergy = 0.0f;
        float midiSine = 0.0f;
        float midiCosine = 1.0f;
        float midiIncrementSine = 0.0f;
        float midiIncrementCosine = 1.0f;
        float externalActuatorStage1 = 0.0f;
        float externalActuatorStage2 = 0.0f;
        float mountLowpass = 0.0f;
        float conditionerInput = 0.0f;
        float conditionerOutput = 0.0f;
        float radiationLowpass = 0.0f;
        std::array<float, kAccelerometerFieldSkinPatchCount>
            skinRadiationLowpass {};
        float energyEnvelope = 0.0f;
        float droneSlow = 0.0f;
        float droneFast = 0.0f;
        float dronePhase = 0.0f;
        float droneSine = 0.0f;
        float droneCosine = 1.0f;
        float droneIncrementSine = 0.0f;
        float droneIncrementCosine = 1.0f;
        uint32_t randomState = 1u;
        uint32_t ageCredit = 0u;

        void reset(uint32_t seed, uint32_t index)
        {
            pitchRatio = 1.0f;
            midiPulsePhase = 1.0f;
            midiPulseIncrement = 0.0f;
            midiPulseStrength = 0.0f;
            midiActuationEnergy = 0.0f;
            midiSine = 0.0f;
            midiCosine = 1.0f;
            midiIncrementSine = 0.0f;
            midiIncrementCosine = 1.0f;
            externalActuatorStage1 = 0.0f;
            externalActuatorStage2 = 0.0f;
            mountLowpass = 0.0f;
            conditionerInput = 0.0f;
            conditionerOutput = 0.0f;
            radiationLowpass = 0.0f;
            skinRadiationLowpass.fill(0.0f);
            energyEnvelope = 0.0f;
            droneSlow = 0.0f;
            droneFast = 0.0f;
            dronePhase = std::fmod(
                static_cast<float>(index) * 0.61803398875f, 1.0f);
            droneSine = std::sin(kAccelerometerFieldTwoPi * dronePhase);
            droneCosine = std::cos(kAccelerometerFieldTwoPi * dronePhase);
            randomState = seed ^ ((index + 1u) * 0x9e3779b9u);
            if (randomState == 0u) randomState = 1u;
            ageCredit = index;
        }

        void snapGeometry(bool active)
        {
            position = targetPosition;
            direction = targetDirection;
            distance = targetDistance;
            basis = targetBasis;
            skinDirection = targetSkinDirection;
            skinBasis = targetSkinBasis;
            gain = active ? targetGain : 0.0f;
        }

        void smoothGeometry(float coefficient)
        {
            position += (targetPosition - position) * coefficient;
            direction = normalize(Vec3 {
                direction.x + (targetDirection.x - direction.x) * coefficient,
                direction.y + (targetDirection.y - direction.y) * coefficient,
                direction.z + (targetDirection.z - direction.z) * coefficient,
            });
            distance += (targetDistance - distance) * coefficient;
            gain += (targetGain - gain) * coefficient;
            for (uint32_t channel = 0u;
                channel < kAccelerometerFieldMaxChannels; ++channel) {
                basis[channel] +=
                    (targetBasis[channel] - basis[channel]) * coefficient;
                for (uint32_t patch = 0u;
                    patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                    skinBasis[patch][channel] +=
                        (targetSkinBasis[patch][channel]
                            - skinBasis[patch][channel]) * coefficient;
                }
            }
            for (uint32_t patch = 0u;
                patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                skinDirection[patch] = normalize(Vec3 {
                    skinDirection[patch].x
                        + (targetSkinDirection[patch].x
                            - skinDirection[patch].x) * coefficient,
                    skinDirection[patch].y
                        + (targetSkinDirection[patch].y
                            - skinDirection[patch].y) * coefficient,
                    skinDirection[patch].z
                        + (targetSkinDirection[patch].z
                            - skinDirection[patch].z) * coefficient,
                });
            }
        }
    };

    static ModalProfile profile(AccelerometerSubstrate substrate)
    {
        switch (substrate) {
        case AccelerometerSubstrate::TieredBronze:
            return { 155.0f, 8.50f, 0.28f, 0.20f, 0.025f, 1.00f,
                720.0f, 0.50f, 1.00f, 0.0f };
        case AccelerometerSubstrate::BroadBronze:
            return { 114.0f, 10.80f, 0.32f, 0.30f, 0.018f, 1.00f,
                720.0f, 0.50f, 1.00f, 0.0f };
        case AccelerometerSubstrate::BrightBronze:
            return { 250.0f, 4.20f, 0.20f, 0.38f, 0.018f, 1.00f,
                720.0f, 0.50f, 1.00f, 0.0f };
        case AccelerometerSubstrate::CarbonLaminate:
            return { 78.0f, 5.80f, 0.30f, 0.62f, 0.060f, 0.78f,
                1500.0f, 0.68f, 0.72f, 0.0f };
        case AccelerometerSubstrate::GlassPlate:
            return { 172.0f, 9.20f, 0.22f, 0.48f, 0.050f, 0.58f,
                1100.0f, 0.55f, 0.90f, 0.0f };
        case AccelerometerSubstrate::SteelShell:
            return { 52.0f, 13.0f, 0.34f, 0.40f, 0.014f, 0.92f,
                360.0f, 0.40f, 1.05f, 0.0f };
        case AccelerometerSubstrate::AluminumPlate:
            return { 108.0f, 7.00f, 0.27f, 0.52f, 0.040f, 0.82f,
                760.0f, 0.52f, 0.98f, 0.0f };
        case AccelerometerSubstrate::PorcelainShell:
            return { 136.0f, 6.20f, 0.36f, 0.58f, 0.038f, 0.66f,
                620.0f, 0.50f, 0.88f, 0.0f };
        case AccelerometerSubstrate::PorousEarthenware:
            return { 88.0f, 3.10f, 0.52f, 0.78f, 0.052f, 0.78f,
                900.0f, 0.65f, 0.55f, 0.0f };
        case AccelerometerSubstrate::SprucePlate:
            return { 68.0f, 4.30f, 0.46f, 0.72f, 0.085f, 0.84f,
                480.0f, 0.44f, 1.03f, 0.0f };
        case AccelerometerSubstrate::TensionedSkin:
            return { 86.0f, 4.80f, 0.31f, 0.62f, 0.18f, 0.86f,
                230.0f, 0.82f, 1.08f, 0.004f };
        case AccelerometerSubstrate::LoadedMembrane:
            return { 38.0f, 3.90f, 0.47f, 0.82f, 0.10f, 0.92f,
                180.0f, 0.74f, 0.88f, 0.006f };
        case AccelerometerSubstrate::CoupledMembrane:
            return { 64.0f, 5.60f, 0.35f, 0.58f, 0.22f, 0.80f,
                210.0f, 0.78f, 1.05f, 0.008f };
        case AccelerometerSubstrate::CavityMembrane:
            return { 46.0f, 6.40f, 0.39f, 0.66f, 0.20f, 0.76f,
                95.0f, 0.48f, 1.22f, 0.005f };
        case AccelerometerSubstrate::LooseMembrane:
            return { 34.0f, 2.70f, 0.58f, 0.96f, 0.30f, 0.90f,
                260.0f, 0.92f, 0.72f, 0.018f };
        case AccelerometerSubstrate::DeepBronze:
        default:
            return { 44.5f, 11.50f, 0.34f, 0.34f, 0.020f, 1.00f,
                720.0f, 0.50f, 1.00f, 0.0f };
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

    float nominalModeRatio(uint32_t index) const
    {
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            deepRatios {{
                1.0000f, 1.0375f, 1.6630f, 2.0000f,
                2.6970f, 3.0000f, 5.0000f, 5.1200f,
                5.8430f, 6.1800f, 6.3370f, 6.4700f,
                7.0000f, 7.1600f, 9.0000f, 9.2000f,
                10.000f, 10.220f, 11.000f, 11.250f,
                12.000f, 12.280f, 14.000f, 14.300f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            broadRatios {{
                1.0000f, 1.0200f, 2.0000f, 2.1053f,
                2.9649f, 3.0000f, 3.5789f, 3.6491f,
                4.0000f, 4.0351f, 5.0000f, 5.0175f,
                6.0000f, 6.0526f, 7.0000f, 7.0702f,
                8.0000f, 8.0877f, 9.0000f, 9.0965f,
                10.1316f, 10.2018f, 11.0000f, 11.0702f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            brightRatios {{
                1.00f, 1.08f, 2.00f, 2.08f,
                4.00f, 4.12f, 5.10f, 5.35f,
                7.60f, 7.95f, 8.20f, 8.65f,
                11.20f, 11.80f, 15.60f, 16.20f,
                18.00f, 18.80f, 22.00f, 23.00f,
                26.00f, 27.20f, 31.00f, 32.50f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            tieredRatios {{
                1.00f, 1.006f, 2.00f, 2.014f, 2.40f, 2.414f,
                3.00f, 3.025f, 4.00f, 4.038f, 5.10f, 5.16f,
                6.35f, 6.44f, 7.85f, 7.98f, 9.55f, 9.72f,
                11.50f, 11.72f, 13.70f, 14.00f, 16.20f, 16.62f,
            }};
        // The plate centers follow normalized isotropic or orthotropic thin-
        // plate relationships. Their close companions model small boundary,
        // layup, grain, and firing asymmetries that keep sustained pairs alive.
        // Shell families use sparse curvature branches rather than plate
        // harmonics. These are material/form archetypes, not specimen fits.
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            carbonRatios {{
                1.0000f, 1.0060f, 2.7423f, 2.7670f,
                4.0000f, 4.0320f, 5.8420f, 5.9472f,
                6.7599f, 6.8951f, 9.0000f, 9.1980f,
                10.2194f, 10.3829f, 10.9694f, 11.1778f,
                12.7607f, 13.0287f, 15.8587f, 16.1442f,
                16.5211f, 16.9176f, 18.0200f, 18.3804f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            glassRatios {{
                1.0000f, 1.0030f, 2.0428f, 2.0510f,
                2.9572f, 2.9749f, 3.7808f, 3.7997f,
                4.0000f, 4.0280f, 5.7380f, 5.7724f,
                6.2139f, 6.2698f, 7.2620f, 7.3346f,
                8.1712f, 8.2366f, 9.0000f, 9.1080f,
                9.3423f, 9.4264f, 10.7861f, 10.9047f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            steelRatios {{
                1.0000f, 1.0015f, 1.6150f, 1.6182f,
                2.3200f, 2.3258f, 3.1150f, 3.1243f,
                4.0000f, 4.0140f, 4.9750f, 4.9949f,
                6.0400f, 6.0672f, 7.1950f, 7.2310f,
                8.4400f, 8.4864f, 9.7750f, 9.8337f,
                11.2000f, 11.2728f, 12.7150f, 12.8040f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            aluminumRatios {{
                1.0000f, 1.0040f, 1.9062f, 1.9176f,
                3.0938f, 3.1093f, 3.4166f, 3.4439f,
                4.0000f, 4.0280f, 5.5104f, 5.5655f,
                6.5834f, 6.6624f, 7.4896f, 7.5720f,
                7.6249f, 7.7316f, 8.2499f, 8.3571f,
                9.0000f, 9.1440f, 10.3436f, 10.4988f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            porcelainRatios {{
                1.0000f, 1.0060f, 1.7350f, 1.7558f,
                2.5800f, 2.6032f, 3.5350f, 3.5986f,
                4.6000f, 4.6506f, 5.7750f, 5.9078f,
                7.0600f, 7.1659f, 8.4550f, 8.6833f,
                9.9600f, 10.1293f, 11.5750f, 11.9223f,
                13.3000f, 13.5660f, 15.1350f, 15.6193f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            earthenwareRatios {{
                1.0000f, 1.0180f, 1.5000f, 1.5375f,
                2.0800f, 2.1216f, 2.7400f, 2.8359f,
                3.4800f, 3.5705f, 4.3000f, 4.4892f,
                5.2000f, 5.3664f, 6.1800f, 6.5014f,
                7.2400f, 7.5151f, 8.3800f, 8.8660f,
                9.6000f, 10.0224f, 10.9000f, 11.5976f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            spruceRatios {{
                1.0000f, 1.0080f, 2.7614f, 2.8001f,
                4.0000f, 4.0480f, 5.9390f, 6.0934f,
                6.7557f, 6.9584f, 9.0000f, 9.3060f,
                10.4329f, 10.6833f, 11.0455f, 11.3548f,
                12.7285f, 13.1358f, 16.0000f, 16.3520f,
                16.7321f, 17.3679f, 18.0541f, 18.5957f,
            }};
        // Circular membrane centers follow sorted fixed-edge Bessel J_m
        // zeros normalized by j_01. Companions represent split angular pairs.
        // Loaded, coupled, cavity-backed, and loose forms transform that
        // lattice in different ways without adding resonators to the pool.
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            tensionedSkinRatios {{
                1.0000f, 1.0040f, 1.5933f, 1.6045f,
                2.1355f, 2.1548f, 2.2954f, 2.3046f,
                2.6531f, 2.6822f, 2.9173f, 2.9406f,
                3.1555f, 3.1965f, 3.5001f, 3.5351f,
                3.5985f, 3.6165f, 3.6475f, 3.7022f,
                4.0589f, 4.1076f, 4.2304f, 4.2727f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            loadedMembraneRatios {{
                1.0000f, 1.0100f, 2.1073f, 2.1368f,
                2.7397f, 2.7890f, 3.2378f, 3.2701f,
                4.0466f, 4.1356f, 4.1936f, 4.2690f,
                4.6432f, 4.6989f, 5.0895f, 5.2218f,
                5.3631f, 5.4704f, 5.8830f, 6.0595f,
                6.1410f, 6.2883f, 6.4157f, 6.5954f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            coupledMembraneRatios {{
                1.0000f, 1.0280f, 1.5933f, 1.6491f,
                2.1355f, 2.2252f, 2.2954f, 2.3643f,
                2.6531f, 2.7778f, 2.9173f, 3.0282f,
                3.1555f, 3.3195f, 3.5001f, 3.5701f,
                3.5985f, 3.6345f, 3.6475f, 3.8590f,
                4.0589f, 4.1807f, 4.2304f, 4.4039f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            cavityMembraneRatios {{
                1.0000f, 1.0250f, 1.3400f, 1.3561f,
                2.1351f, 2.1692f, 2.8616f, 2.9131f,
                3.0759f, 3.1189f, 3.4200f, 3.4884f,
                3.5551f, 3.6298f, 3.9092f, 3.9756f,
                4.2283f, 4.3298f, 4.6902f, 4.7746f,
                4.8220f, 4.8654f, 4.8876f, 4.9951f,
            }};
        static constexpr std::array<float, kAccelerometerFieldModeCount>
            looseMembraneRatios {{
                1.0000f, 1.0180f, 1.6230f, 1.6619f,
                2.2263f, 2.2976f, 2.4120f, 2.4602f,
                2.8417f, 2.9554f, 3.1733f, 3.2685f,
                3.4831f, 3.6503f, 3.9513f, 4.0501f,
                4.0894f, 4.1303f, 4.1589f, 4.3876f,
                4.7637f, 4.9733f, 5.0271f, 5.2182f,
            }};
        index = std::min<uint32_t>(
            index, kAccelerometerFieldModeCount - 1u);
        switch (params_.substrate) {
        case AccelerometerSubstrate::TieredBronze: return tieredRatios[index];
        case AccelerometerSubstrate::BroadBronze: return broadRatios[index];
        case AccelerometerSubstrate::BrightBronze: return brightRatios[index];
        case AccelerometerSubstrate::CarbonLaminate:
            return carbonRatios[index];
        case AccelerometerSubstrate::GlassPlate: return glassRatios[index];
        case AccelerometerSubstrate::SteelShell: return steelRatios[index];
        case AccelerometerSubstrate::AluminumPlate:
            return aluminumRatios[index];
        case AccelerometerSubstrate::PorcelainShell:
            return porcelainRatios[index];
        case AccelerometerSubstrate::PorousEarthenware:
            return earthenwareRatios[index];
        case AccelerometerSubstrate::SprucePlate: return spruceRatios[index];
        case AccelerometerSubstrate::TensionedSkin:
            return tensionedSkinRatios[index];
        case AccelerometerSubstrate::LoadedMembrane:
            return loadedMembraneRatios[index];
        case AccelerometerSubstrate::CoupledMembrane:
            return coupledMembraneRatios[index];
        case AccelerometerSubstrate::CavityMembrane:
            return cavityMembraneRatios[index];
        case AccelerometerSubstrate::LooseMembrane:
            return looseMembraneRatios[index];
        case AccelerometerSubstrate::DeepBronze:
        default: return deepRatios[index];
        }
    }

    static float modalDecayWeight(
        AccelerometerSubstrate substrate, uint32_t local)
    {
        static constexpr std::array<float, 12u> bronze {{
            1.35f, 1.00f, 0.82f, 0.58f, 0.88f, 0.66f,
            0.52f, 0.43f, 0.36f, 0.30f, 0.26f, 0.22f,
        }};
        static constexpr std::array<float, 12u> carbon {{
            1.05f, 0.82f, 0.96f, 0.72f, 0.88f, 0.65f,
            0.78f, 0.58f, 0.70f, 0.50f, 0.61f, 0.44f,
        }};
        static constexpr std::array<float, 12u> glass {{
            1.15f, 0.96f, 0.82f, 0.91f, 0.70f, 0.78f,
            0.60f, 0.66f, 0.52f, 0.57f, 0.45f, 0.40f,
        }};
        static constexpr std::array<float, 12u> steel {{
            1.30f, 1.10f, 0.92f, 0.78f, 0.98f, 0.72f,
            0.84f, 0.62f, 0.70f, 0.54f, 0.60f, 0.46f,
        }};
        static constexpr std::array<float, 12u> aluminum {{
            1.18f, 0.94f, 0.82f, 0.72f, 0.88f, 0.66f,
            0.72f, 0.55f, 0.61f, 0.48f, 0.52f, 0.41f,
        }};
        static constexpr std::array<float, 12u> porcelain {{
            1.20f, 0.82f, 1.00f, 0.68f, 0.85f, 0.55f,
            0.72f, 0.45f, 0.60f, 0.38f, 0.49f, 0.32f,
        }};
        static constexpr std::array<float, 12u> earthenware {{
            1.05f, 0.78f, 0.62f, 0.50f, 0.42f, 0.34f,
            0.29f, 0.24f, 0.20f, 0.17f, 0.14f, 0.12f,
        }};
        static constexpr std::array<float, 12u> spruce {{
            1.10f, 0.72f, 0.98f, 0.60f, 0.84f, 0.52f,
            0.70f, 0.44f, 0.58f, 0.37f, 0.48f, 0.31f,
        }};
        static constexpr std::array<float, 12u> tensionedSkin {{
            1.10f, 1.00f, 0.92f, 0.86f, 0.82f, 0.76f,
            0.71f, 0.66f, 0.62f, 0.58f, 0.54f, 0.50f,
        }};
        static constexpr std::array<float, 12u> loadedMembrane {{
            1.25f, 0.82f, 0.74f, 1.05f, 0.62f, 0.58f,
            0.90f, 0.48f, 0.72f, 0.41f, 0.38f, 0.34f,
        }};
        static constexpr std::array<float, 12u> coupledMembrane {{
            1.15f, 1.05f, 0.98f, 0.90f, 0.84f, 0.78f,
            0.72f, 0.67f, 0.62f, 0.58f, 0.54f, 0.50f,
        }};
        static constexpr std::array<float, 12u> cavityMembrane {{
            1.45f, 1.15f, 0.90f, 0.76f, 0.82f, 1.25f,
            0.68f, 0.60f, 0.54f, 0.48f, 0.43f, 0.39f,
        }};
        static constexpr std::array<float, 12u> looseMembrane {{
            1.00f, 0.80f, 0.66f, 0.72f, 0.55f, 0.46f,
            0.42f, 0.34f, 0.30f, 0.25f, 0.21f, 0.18f,
        }};
        const uint32_t pair = std::min<uint32_t>(local / 2u, 11u);
        switch (substrate) {
        case AccelerometerSubstrate::DeepBronze:
        case AccelerometerSubstrate::BroadBronze: return bronze[pair];
        case AccelerometerSubstrate::CarbonLaminate:
            return carbon[pair] * ((local & 1u) == 0u ? 1.18f : 0.78f);
        case AccelerometerSubstrate::GlassPlate: return glass[pair];
        case AccelerometerSubstrate::SteelShell: return steel[pair];
        case AccelerometerSubstrate::AluminumPlate: return aluminum[pair];
        case AccelerometerSubstrate::PorcelainShell: return porcelain[pair];
        case AccelerometerSubstrate::PorousEarthenware:
            return earthenware[pair];
        case AccelerometerSubstrate::SprucePlate:
            return spruce[pair] * ((local & 1u) == 0u ? 1.12f : 0.72f);
        case AccelerometerSubstrate::TensionedSkin:
            return tensionedSkin[pair]
                * ((local & 1u) == 0u ? 1.0f : 0.94f);
        case AccelerometerSubstrate::LoadedMembrane:
            return loadedMembrane[pair]
                * ((local & 1u) == 0u ? 1.0f : 0.78f);
        case AccelerometerSubstrate::CoupledMembrane:
            return coupledMembrane[pair]
                * ((local & 1u) == 0u ? 1.08f : 0.78f);
        case AccelerometerSubstrate::CavityMembrane:
            return cavityMembrane[pair]
                * ((local & 1u) == 0u ? 1.0f : 0.86f);
        case AccelerometerSubstrate::LooseMembrane:
            return looseMembrane[pair]
                * ((local & 1u) == 0u ? 1.05f : 0.68f);
        default: return 1.0f;
        }
    }

    static float modalSpectralWeight(
        AccelerometerSubstrate substrate, uint32_t local)
    {
        static constexpr std::array<float, 12u> bronzeCluster {{
            0.82f, 1.00f, 0.58f, 0.42f, 0.76f, 0.46f,
            0.34f, 0.27f, 0.22f, 0.18f, 0.15f, 0.13f,
        }};
        static constexpr std::array<float, 12u> brightBronze {{
            0.16f, 0.32f, 0.68f, 0.78f, 1.00f, 0.96f,
            0.88f, 0.82f, 0.66f, 0.52f, 0.40f, 0.30f,
        }};
        static constexpr std::array<float, 12u> carbon {{
            0.92f, 1.00f, 0.78f, 0.90f, 0.70f, 0.82f,
            0.62f, 0.72f, 0.56f, 0.63f, 0.48f, 0.42f,
        }};
        static constexpr std::array<float, 12u> glass {{
            0.42f, 0.65f, 0.88f, 1.00f, 0.91f, 0.82f,
            0.74f, 0.67f, 0.60f, 0.52f, 0.45f, 0.38f,
        }};
        static constexpr std::array<float, 12u> steel {{
            1.00f, 0.92f, 0.82f, 0.88f, 0.72f, 0.78f,
            0.62f, 0.56f, 0.49f, 0.43f, 0.37f, 0.32f,
        }};
        static constexpr std::array<float, 12u> aluminum {{
            0.72f, 0.88f, 1.00f, 0.90f, 0.84f, 0.76f,
            0.68f, 0.61f, 0.54f, 0.48f, 0.42f, 0.36f,
        }};
        static constexpr std::array<float, 12u> porcelain {{
            1.00f, 0.62f, 0.90f, 0.52f, 0.76f, 0.44f,
            0.64f, 0.38f, 0.52f, 0.32f, 0.42f, 0.26f,
        }};
        static constexpr std::array<float, 12u> earthenware {{
            1.00f, 0.84f, 0.66f, 0.50f, 0.38f, 0.29f,
            0.22f, 0.17f, 0.13f, 0.10f, 0.08f, 0.06f,
        }};
        static constexpr std::array<float, 12u> spruce {{
            1.00f, 0.96f, 0.84f, 0.72f, 0.61f, 0.51f,
            0.43f, 0.36f, 0.30f, 0.25f, 0.21f, 0.17f,
        }};
        static constexpr std::array<float, 12u> tensionedSkin {{
            1.00f, 0.92f, 0.84f, 0.72f, 0.78f, 0.66f,
            0.60f, 0.54f, 0.48f, 0.43f, 0.38f, 0.34f,
        }};
        static constexpr std::array<float, 12u> loadedMembrane {{
            1.00f, 0.68f, 0.52f, 0.80f, 0.42f, 0.38f,
            0.62f, 0.30f, 0.46f, 0.24f, 0.20f, 0.17f,
        }};
        static constexpr std::array<float, 12u> coupledMembrane {{
            1.00f, 0.96f, 0.88f, 0.80f, 0.74f, 0.68f,
            0.62f, 0.57f, 0.52f, 0.47f, 0.42f, 0.38f,
        }};
        static constexpr std::array<float, 12u> cavityMembrane {{
            1.00f, 0.82f, 0.70f, 0.60f, 0.64f, 0.88f,
            0.52f, 0.45f, 0.39f, 0.34f, 0.30f, 0.26f,
        }};
        static constexpr std::array<float, 12u> looseMembrane {{
            1.00f, 0.82f, 0.64f, 0.72f, 0.50f, 0.41f,
            0.34f, 0.28f, 0.23f, 0.19f, 0.15f, 0.12f,
        }};
        const uint32_t pair = std::min<uint32_t>(local / 2u, 11u);
        switch (substrate) {
        case AccelerometerSubstrate::BrightBronze:
            return brightBronze[pair];
        case AccelerometerSubstrate::CarbonLaminate: return carbon[pair];
        case AccelerometerSubstrate::GlassPlate: return glass[pair];
        case AccelerometerSubstrate::SteelShell: return steel[pair];
        case AccelerometerSubstrate::AluminumPlate: return aluminum[pair];
        case AccelerometerSubstrate::PorcelainShell: return porcelain[pair];
        case AccelerometerSubstrate::PorousEarthenware:
            return earthenware[pair];
        case AccelerometerSubstrate::SprucePlate: return spruce[pair];
        case AccelerometerSubstrate::TensionedSkin:
            return tensionedSkin[pair];
        case AccelerometerSubstrate::LoadedMembrane:
            return loadedMembrane[pair]
                * ((local & 1u) == 0u ? 1.0f : 0.82f);
        case AccelerometerSubstrate::CoupledMembrane:
            return coupledMembrane[pair]
                * ((local & 1u) == 0u ? 1.0f : 0.92f);
        case AccelerometerSubstrate::CavityMembrane:
            return cavityMembrane[pair];
        case AccelerometerSubstrate::LooseMembrane:
            return looseMembrane[pair]
                * ((local & 1u) == 0u ? 1.0f : 0.78f);
        default: return bronzeCluster[pair];
        }
    }

    float modeShape(uint32_t index, float position, bool orthogonal) const
    {
        position = clamp(position, 0.0f, 1.0f);
        const float order = static_cast<float>(index / 2u + 2u);
        const float pairPhase = (index & 1u) == 0u ? 0.0f : 0.25f;
        const float phase = pairPhase + (orthogonal ? 0.25f : 0.0f);
        return std::cos(kAccelerometerFieldTwoPi
            * (order * position + phase));
    }

    static Vec3 add(Vec3 a, Vec3 b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    static Vec3 scale(Vec3 value, float amount)
    {
        return { value.x * amount, value.y * amount, value.z * amount };
    }

    static Vec3 rotateFromForward(Vec3 local, Vec3 forward)
    {
        forward = normalize(forward);
        const Vec3 up = std::fabs(forward.z) > 0.92f
            ? Vec3 { 0.0f, 1.0f, 0.0f }
            : Vec3 { 0.0f, 0.0f, 1.0f };
        Vec3 right {
            up.y * forward.z - up.z * forward.y,
            up.z * forward.x - up.x * forward.z,
            up.x * forward.y - up.y * forward.x,
        };
        right = normalize(right);
        const Vec3 vertical {
            forward.y * right.z - forward.z * right.y,
            forward.z * right.x - forward.x * right.z,
            forward.x * right.y - forward.y * right.x,
        };
        return normalize({
            forward.x * local.x + right.x * local.y + vertical.x * local.z,
            forward.y * local.x + right.y * local.y + vertical.y * local.z,
            forward.z * local.x + right.z * local.y + vertical.z * local.z,
        });
    }

    Vec3 fibonacciDirection(uint32_t body) const
    {
        const float value = static_cast<float>(body);
        const float count = static_cast<float>(activeBodyCount_);
        const float z = 1.0f - 2.0f * ((value + 0.5f) / count);
        const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float phase = kAccelerometerFieldTwoPi
            * std::fmod(value * 0.61803398875f, 1.0f);
        return { radius * std::cos(phase), radius * std::sin(phase), z };
    }

    static float wrapSignedDegrees(float value)
    {
        value = std::fmod(value + 180.0f, 360.0f);
        if (value < 0.0f) value += 360.0f;
        return value - 180.0f;
    }

    static std::array<float, 2u> directionAed(Vec3 direction)
    {
        direction = normalize(direction);
        return {{
            std::atan2(direction.y, direction.x) * 180.0f / kPi,
            std::asin(clamp(direction.z, -1.0f, 1.0f)) * 180.0f / kPi,
        }};
    }

    uint32_t allocatedPairs(uint32_t body) const
    {
        constexpr uint32_t pairBudget = kAccelerometerFieldModeBudget / 2u;
        const uint32_t base = pairBudget / activeBodyCount_;
        const uint32_t remainder = pairBudget % activeBodyCount_;
        const uint32_t rotation = params_.seed % activeBodyCount_;
        const uint32_t rotated = (body + activeBodyCount_ - rotation)
            % activeBodyCount_;
        return base + (rotated < remainder ? 1u : 0u);
    }

    void configureFieldListener()
    {
        if (params_.listenerPickupSet
            == AccelerometerFieldListenerPickupSet::Tetra4) {
            const auto& directions = ambiFieldListenerTetraDirections();
            fieldListener_.setDirections(
                directions.data(), static_cast<uint32_t>(directions.size()));
        } else {
            const auto& directions = ambiFieldListenerCubeDirections();
            fieldListener_.setDirections(
                directions.data(), static_cast<uint32_t>(directions.size()));
        }
        fieldListener_.reset();
    }

    void rebuildGeometry(bool snapGeometry)
    {
        const Vec3 center = directionFromAed(
            params_.fieldAzimuthDeg, params_.fieldElevationDeg);
        for (uint32_t bodyIndex = 0u;
            bodyIndex < kAccelerometerFieldMaxBodyCount; ++bodyIndex) {
            Body& body = bodies_[bodyIndex];
            const bool active = bodyIndex < activeBodyCount_;
            const Vec3 local = active
                ? fibonacciDirection(bodyIndex) : Vec3 { 1.0f, 0.0f, 0.0f };
            const Vec3 constellation = rotateFromForward(local, center);
            Vec3 direction = normalize(add(
                scale(center, 1.0f - params_.spatialExtent),
                scale(constellation, params_.spatialExtent)));
            const float azimuthOffset = params_.bodyAzimuthOffsetDeg[bodyIndex];
            const float elevationOffset =
                params_.bodyElevationOffsetDeg[bodyIndex];
            if (std::fabs(azimuthOffset) > 1.0e-7f
                || std::fabs(elevationOffset) > 1.0e-7f) {
                const auto aed = directionAed(direction);
                direction = directionFromAed(
                    wrapSignedDegrees(aed[0] + azimuthOffset),
                    clamp(aed[1] + elevationOffset, -90.0f, 90.0f));
            }
            body.targetDirection = direction;
            body.targetDistance = params_.bodyDistance[bodyIndex];
            const auto basis = acnSn3dBasis7(direction);
            for (uint32_t channel = 0u;
                channel < kAccelerometerFieldMaxChannels; ++channel) {
                body.targetBasis[channel] = basis[channel];
            }
            // Four bounded samples describe one radiating skin. Their angular
            // footprint grows inside the body's local tangent plane; no new
            // resonators are allocated, so the fixed 96-mode budget remains
            // the complete structural model.
            constexpr std::array<std::array<float, 2u>,
                kAccelerometerFieldSkinPatchCount> offsets {{
                {{ -1.0f, -1.0f }}, {{ 1.0f, -1.0f }},
                {{ -1.0f, 1.0f }}, {{ 1.0f, 1.0f }},
            }};
            const float skinAngle = params_.propagationLoss
                * (0.10f + params_.spatialExtent * 0.34f);
            for (uint32_t patch = 0u;
                patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                const float tangent = std::sin(skinAngle)
                    * 0.7071067811865475f;
                const Vec3 patchLocal = normalize(Vec3 {
                    std::cos(skinAngle),
                    offsets[patch][0] * tangent,
                    offsets[patch][1] * tangent,
                });
                body.targetSkinDirection[patch] = rotateFromForward(
                    patchLocal, direction);
                const auto patchBasis = acnSn3dBasis7(
                    body.targetSkinDirection[patch]);
                for (uint32_t channel = 0u;
                    channel < kAccelerometerFieldMaxChannels; ++channel) {
                    body.targetSkinBasis[patch][channel] =
                        patchBasis[channel];
                }
            }
            const float distanceGain = 1.0f
                / std::max(0.15f, body.targetDistance);
            body.targetGain = active
                ? clamp(distanceGain, 0.5f, 2.0f) : 0.0f;
            if (snapGeometry) body.snapGeometry(active);
        }
    }

    void rebuildModel(bool snapGeometry)
    {
        activeBodyCount_ = std::clamp<uint32_t>(params_.bodyCount,
            kAccelerometerFieldMinBodyCount, kAccelerometerFieldMaxBodyCount);
        uint32_t modeOffset = 0u;
        for (uint32_t bodyIndex = 0u;
            bodyIndex < kAccelerometerFieldMaxBodyCount; ++bodyIndex) {
            Body& body = bodies_[bodyIndex];
            body.modeStart = modeOffset;
            body.modeCount = bodyIndex < activeBodyCount_
                ? allocatedPairs(bodyIndex) * 2u : 0u;
            modeOffset += body.modeCount;
            body.targetPosition = bodyIndex < activeBodyCount_
                ? accelerometerFieldBodyPosition(params_, bodyIndex) : 0.5f;
            body.couplingPhase = kAccelerometerFieldTwoPi
                * std::fmod(static_cast<float>(bodyIndex) * 0.61803398875f,
                    1.0f);
            body.couplingSlowSin = std::sin(body.couplingPhase);
            body.couplingSlowCos = std::cos(body.couplingPhase);
            body.couplingFastSin = std::sin(body.couplingPhase * 1.37f);
            body.couplingFastCos = std::cos(body.couplingPhase * 1.37f);
            const float droneIncrement = kAccelerometerFieldTwoPi
                * (17.0f + 6.5f * static_cast<float>(bodyIndex))
                / static_cast<float>(sampleRate_);
            body.droneIncrementSine = std::sin(droneIncrement);
            body.droneIncrementCosine = std::cos(droneIncrement);
            const float identity = hashSigned(params_.seed
                ^ ((bodyIndex + 1u) * 0x85ebca6bu));
            body.identityPitch = std::exp2(identity
                * (0.008f + params_.arraySpread * 0.018f
                    + params_.irregularity * 0.035f));
        }
        rebuildGeometry(snapGeometry);

        const ModalProfile modalProfile = profile(params_.substrate);
        const float sizeScale = std::exp2((0.5f - params_.size) * 3.2f);
        const float nyquistLimit = static_cast<float>(sampleRate_) * 0.44f;
        const float velocityNormalization = static_cast<float>(sampleRate_)
            / (kAccelerometerFieldTwoPi * 1000.0f);
        const float accelerationNormalization = velocityNormalization
            * velocityNormalization;
        for (uint32_t bodyIndex = 0u;
            bodyIndex < activeBodyCount_; ++bodyIndex) {
            Body& body = bodies_[bodyIndex];
            float weightSum = 0.0f;
            for (uint32_t local = 0u; local < body.modeCount; ++local) {
                weightSum += 1.0f / std::pow(1.0f + static_cast<float>(local),
                    modalProfile.modeFalloff);
            }
            const float bodyNormalization = weightSum > 1.0e-6f
                ? std::sqrt(static_cast<float>(body.modeCount) / weightSum)
                : 1.0f;
            for (uint32_t local = 0u; local < body.modeCount; ++local) {
                Mode& mode = modes_[body.modeStart + local];
                mode.body = bodyIndex;
                mode.localIndex = local;
                const float progress = body.modeCount > 1u
                    ? static_cast<float>(local)
                        / static_cast<float>(body.modeCount - 1u)
                    : 0.0f;
                const float irregular = hashSigned(params_.seed
                    ^ ((bodyIndex + 1u) * 0x9e3779b9u)
                    ^ ((local + 1u) * 0x7f4a7c15u));
                const float nominal = modalProfile.baseFrequencyHz
                    * sizeScale * nominalModeRatio(local)
                    * body.identityPitch
                    * std::exp2(irregular * params_.irregularity * 0.10f);
                const float addedMass = params_.sensorMass
                    * modalProfile.massSensitivity
                    * (0.72f + 0.55f * progress);
                const float frequency = nominal / std::sqrt(1.0f + addedMass);
                mode.frequencyHz = frequency;
                mode.pairProgress = body.modeCount > 2u
                    ? static_cast<float>(local / 2u)
                        / static_cast<float>(body.modeCount / 2u - 1u)
                    : 0.0f;
                mode.active = true;
                const bool audible = frequency >= 5.0f
                    && frequency < nyquistLimit;
                if (!audible) {
                    mode.radiusTarget = 0.0;
                    // Retain the last pole angle while its radius and output
                    // weights fade. Sweeping an excluded high/low mode toward
                    // DC would turn an extreme Size move into a short chirp.
                    mode.driveTarget = 0.0f;
                    mode.pickupWeightTarget = 0.0f;
                    mode.radiationWeightTarget = 0.0f;
                    mode.skinRadiationWeightTarget.fill(0.0f);
                    mode.transpositionGainTarget = 0.0f;
                    continue;
                }
                const float decayScale = std::exp2(
                    (0.5f - params_.damping) * 4.2f);
                float decay = std::max(0.015f,
                    modalProfile.baseDecaySeconds * decayScale
                    / std::pow(1.0f + progress * 5.0f,
                        modalProfile.decayFalloff));
                decay = std::max(0.015f, decay * modalDecayWeight(
                    params_.substrate, local));
                mode.radiusTarget = std::exp(-1.0
                    / (static_cast<double>(decay) * sampleRate_));
                const float strikePosition = clamp(params_.bodySkinX[bodyIndex]
                        + (bodyUnitPosition(bodyIndex) - 0.5f)
                            * params_.arraySpread * 0.10f,
                    0.0f, 1.0f);
                const float sourceShapeX = modeShape(
                    local, strikePosition, false);
                const float sourceShapeY = modeShape(
                    local, params_.bodySkinY[bodyIndex], true);
                const float distributedSource = (sourceShapeX + sourceShapeY)
                    * 0.7071067811865475f;
                const float sourceShape = lerp(sourceShapeX,
                    distributedSource, params_.propagationLoss);
                float modalGain = 1.0f
                    / std::pow(1.0f + static_cast<float>(local),
                        modalProfile.modeFalloff);
                modalGain *= modalSpectralWeight(params_.substrate, local);
                modalGain *= bodyNormalization;
                mode.driveTarget = 0.0045f * modalGain * sourceShape;
                const float axisRadians = clamp(params_.pickupAxis
                        + (bodyUnitPosition(bodyIndex) - 0.5f) * 0.20f,
                    0.0f, 1.0f) * 1.57079632679489661923f;
                mode.pickupWeightTarget = std::cos(axisRadians) * modeShape(
                    local, body.targetPosition, false)
                    + std::sin(axisRadians) * modeShape(
                        local, body.targetPosition, true);
                const float radiationEfficiency = std::pow(
                    frequency / (frequency
                        + modalProfile.radiationCornerHz),
                    modalProfile.radiationExponent)
                    * modalProfile.radiationGain;
                mode.radiationWeightTarget = modeShape(
                    local, body.targetPosition, false)
                    * modalGain * radiationEfficiency
                    * (0.58f + 0.42f * std::fabs(sourceShape));
                constexpr std::array<std::array<float, 2u>,
                    kAccelerometerFieldSkinPatchCount> offsets {{
                    {{ -1.0f, -1.0f }}, {{ 1.0f, -1.0f }},
                    {{ -1.0f, 1.0f }}, {{ 1.0f, 1.0f }},
                }};
                const float patchRadius = params_.propagationLoss * 0.44f;
                for (uint32_t patch = 0u;
                    patch < kAccelerometerFieldSkinPatchCount; ++patch) {
                    const float patchX = clamp(params_.bodySkinX[bodyIndex]
                            + offsets[patch][0] * patchRadius,
                        0.0f, 1.0f);
                    const float patchY = clamp(params_.bodySkinY[bodyIndex]
                            + offsets[patch][1] * patchRadius,
                        0.0f, 1.0f);
                    const float patchShape = std::cos(axisRadians)
                            * modeShape(local, patchX, false)
                        + std::sin(axisRadians)
                            * modeShape(local, patchY, true);
                    mode.skinRadiationWeightTarget[patch] = patchShape
                        * modalGain * radiationEfficiency
                        * (0.58f + 0.42f * std::fabs(sourceShape));
                }
                mode.velocityNormalization = velocityNormalization;
                mode.accelerationNormalization = accelerationNormalization;
            }
        }
        for (uint32_t index = modeOffset;
            index < kAccelerometerFieldModeBudget; ++index) {
            modes_[index].active = false;
            modes_[index].reset();
        }
        updateAllDynamicCoefficients();
        if (snapGeometry) {
            for (uint32_t index = 0u; index < modeOffset; ++index) {
                modes_[index].snapMorphTargets();
            }
        }
    }

    void smoothGeometry()
    {
        for (auto& body : bodies_) {
            body.smoothGeometry(geometrySmoothingCoefficient_);
        }
    }

    float modalCouplingValue(const Mode& mode) const
    {
        const float slowWeight = 0.10f
            + 0.72f * (1.0f - mode.pairProgress);
        const Body& body = bodies_[mode.body];
        const float value = body.couplingSlowValue * slowWeight
            + body.couplingFastValue * (1.0f - slowWeight);
        // The two branches of Coupled Membrane represent slightly mismatched
        // skins. Anti-phase AM/FM makes their dominance alternate using the
        // existing bounded oscillators; it does not add an audio feedback
        // path or another modal bank.
        if (params_.substrate == AccelerometerSubstrate::CoupledMembrane
            && (mode.localIndex & 1u) != 0u) {
            return -value;
        }
        return value;
    }

    void routeExternalActuator(float externalForce)
    {
        const float magnitude = std::fabs(externalForce);
        const float onset = std::max(
            0.0f, magnitude - externalLevelEnvelope_ * 1.8f);
        const float coefficient = magnitude > externalLevelEnvelope_
            ? 0.08f : 0.0008f;
        externalLevelEnvelope_ += coefficient
            * (magnitude - externalLevelEnvelope_);
        if (onset > 0.02f) {
            const uint32_t selected = selectPlayerBody();
            externalBody_ = selected;
            markActuated(selected);
        }
    }

    void advanceEvolution()
    {
        const float nextSlowSine = couplingSlowSine_
                * couplingSlowIncrementCosine_
            + couplingSlowCosine_ * couplingSlowIncrementSine_;
        const float nextSlowCosine = couplingSlowCosine_
                * couplingSlowIncrementCosine_
            - couplingSlowSine_ * couplingSlowIncrementSine_;
        const float nextFastSine = couplingFastSine_
                * couplingFastIncrementCosine_
            + couplingFastCosine_ * couplingFastIncrementSine_;
        const float nextFastCosine = couplingFastCosine_
                * couplingFastIncrementCosine_
            - couplingFastSine_ * couplingFastIncrementSine_;
        couplingSlowSine_ = flushDenormal(nextSlowSine);
        couplingSlowCosine_ = flushDenormal(nextSlowCosine);
        couplingFastSine_ = flushDenormal(nextFastSine);
        couplingFastCosine_ = flushDenormal(nextFastCosine);
        if (couplingOscillatorRenormalizeCountdown_ == 0u) {
            const float slowScale = 1.0f / std::sqrt(std::max(
                1.0e-12f, couplingSlowSine_ * couplingSlowSine_
                    + couplingSlowCosine_ * couplingSlowCosine_));
            const float fastScale = 1.0f / std::sqrt(std::max(
                1.0e-12f, couplingFastSine_ * couplingFastSine_
                    + couplingFastCosine_ * couplingFastCosine_));
            couplingSlowSine_ *= slowScale;
            couplingSlowCosine_ *= slowScale;
            couplingFastSine_ *= fastScale;
            couplingFastCosine_ *= fastScale;
            couplingOscillatorRenormalizeCountdown_ =
                kCouplingOscillatorRenormalizeInterval;
        } else {
            --couplingOscillatorRenormalizeCountdown_;
        }
        for (uint32_t bodyIndex = 0u;
            bodyIndex < activeBodyCount_; ++bodyIndex) {
            Body& body = bodies_[bodyIndex];
            body.couplingSlowValue = couplingSlowSine_
                    * body.couplingSlowCos
                + couplingSlowCosine_ * body.couplingSlowSin;
            body.couplingFastValue = couplingFastSine_
                    * body.couplingFastCos
                + couplingFastCosine_ * body.couplingFastSin;
        }
    }

    void updateEvolutionIncrements()
    {
        const float inverseSampleRate = 1.0f
            / static_cast<float>(sampleRate_);
        const float slowIncrement = kAccelerometerFieldTwoPi
            * (0.80f + 1.30f * params_.coupling) * inverseSampleRate;
        const float fastIncrement = kAccelerometerFieldTwoPi
            * (8.0f + 10.0f * params_.coupling) * inverseSampleRate;
        couplingSlowIncrementSine_ = std::sin(slowIncrement);
        couplingSlowIncrementCosine_ = std::cos(slowIncrement);
        couplingFastIncrementSine_ = std::sin(fastIncrement);
        couplingFastIncrementCosine_ = std::cos(fastIncrement);
    }

    void updateDynamicCoefficients(uint32_t bodyIndex)
    {
        if (bodyIndex >= activeBodyCount_) return;
        const Body& body = bodies_[bodyIndex];
        const ModalProfile modalProfile = profile(params_.substrate);
        const float nyquistLimit = static_cast<float>(sampleRate_) * 0.44f;
        const float fadeStart = static_cast<float>(sampleRate_) * 0.38f;
        const float actuation = actuatorDriveEnvelope_[bodyIndex];
        const float normalizedActuation = actuation
            / (actuation + 0.12f);
        for (uint32_t local = 0u;
            local < body.modeCount; ++local) {
            Mode& mode = modes_[body.modeStart + local];
            if (!mode.active) continue;
            if (!(mode.radiusTarget > 0.0)) {
                mode.transpositionGainTarget = 0.0f;
                continue;
            }
            const float modulation = modalCouplingValue(mode);
            const float frequencyDepth = couplingSmoothed_
                * (0.0025f + 0.0065f * (1.0f - mode.pairProgress));
            const float tensionScale = 1.0f
                + modalProfile.tensionHardening
                    * normalizedActuation * normalizedActuation
                    * (1.0f - 0.35f * mode.pairProgress);
            const float frequencyScale = body.pitchRatio
                * tensionScale
                * std::max(0.96f, 1.0f + frequencyDepth * modulation);
            const float requestedFrequency = mode.frequencyHz * frequencyScale;
            mode.transpositionGainTarget = clamp(
                (nyquistLimit - requestedFrequency)
                    / (nyquistLimit - fadeStart),
                0.0f, 1.0f);
            const float frequency = std::min(
                nyquistLimit, requestedFrequency);
            const double omega = static_cast<double>(
                kAccelerometerFieldTwoPi) * static_cast<double>(frequency)
                / sampleRate_;
            mode.coefficientNormTarget = std::cos(omega);
        }
    }

    void updateAllDynamicCoefficients()
    {
        for (uint32_t body = 0u;
            body < activeBodyCount_; ++body) {
            updateDynamicCoefficients(body);
        }
    }

    static float bodyRandomSigned(Body& body)
    {
        uint32_t value = body.randomState;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        body.randomState = value == 0u ? 1u : value;
        return static_cast<float>(body.randomState & 0x00ffffffu)
            * (2.0f / 16777215.0f) - 1.0f;
    }

    float bodyUnitPosition(uint32_t body) const
    {
        return (static_cast<float>(body) + 0.5f)
            / static_cast<float>(std::max<uint32_t>(1u, activeBodyCount_));
    }

    void markActuated(uint32_t bodyIndex)
    {
        lastActuatedBody_ = bodyIndex;
        listenerTargetPosition_ = bodyUnitPosition(bodyIndex);
        for (uint32_t body = 0u; body < activeBodyCount_; ++body) {
            if (body == bodyIndex) {
                bodies_[body].ageCredit = 0u;
            } else {
                bodies_[body].ageCredit = std::min<uint32_t>(
                    bodies_[body].ageCredit + 1u, 64u);
            }
        }
    }

    uint32_t selectPlayerBody(bool preferIdleMidiBody = false)
    {
        bool hasIdleMidiBody = false;
        if (preferIdleMidiBody) {
            for (uint32_t body = 0u; body < activeBodyCount_; ++body) {
                if (bodies_[body].midiPulsePhase >= 1.0f) {
                    hasIdleMidiBody = true;
                    break;
                }
            }
        }
        if (params_.fieldListenMode == AmbiFieldListenMode::Off
            || params_.fieldListenAmount <= 1.0e-7f) {
            uint32_t selected = nextRoundRobinBody_;
            if (hasIdleMidiBody) {
                for (uint32_t offset = 0u; offset < activeBodyCount_;
                    ++offset) {
                    const uint32_t candidate = (nextRoundRobinBody_ + offset)
                        % activeBodyCount_;
                    if (bodies_[candidate].midiPulsePhase >= 1.0f) {
                        selected = candidate;
                        break;
                    }
                }
            }
            nextRoundRobinBody_ = (selected + 1u)
                % activeBodyCount_;
            return selected;
        }

        uint32_t strongest = 0u;
        uint32_t quietest = 0u;
        float strongestScore = -1.0e9f;
        float quietestScore = 1.0e9f;
        for (uint32_t body = 0u; body < activeBodyCount_; ++body) {
            if (hasIdleMidiBody && bodies_[body].midiPulsePhase < 1.0f) {
                continue;
            }
            const auto score = fieldListener_.score(
                bodies_[body].direction, params_.fieldListenMode);
            const float heard = score.relativeEnergy * 0.62f
                + score.charge * 0.20f + score.novelty * 0.18f;
            const float energy = bodies_[body].energyEnvelope;
            if (heard > strongestScore) {
                strongestScore = heard;
                strongest = body;
            }
            if (energy < quietestScore) {
                quietestScore = energy;
                quietest = body;
            }
        }

        // score() already evaluates Counter against the antipodal listener
        // direction. Applying an index offset here would invert Counter twice,
        // and array indices are not spatial antipodes after manual AED edits.
        uint32_t preferred = strongest;
        if (params_.fieldListenMode == AmbiFieldListenMode::Balance) {
            preferred = quietest;
        }
        uint32_t selected = preferred;
        float best = -1.0e9f;
        for (uint32_t body = 0u; body < activeBodyCount_; ++body) {
            if (hasIdleMidiBody && bodies_[body].midiPulsePhase < 1.0f) {
                continue;
            }
            const float preference = body == preferred ? 1.0f : 0.0f;
            const float fairness = static_cast<float>(bodies_[body].ageCredit)
                / static_cast<float>(activeBodyCount_);
            const float value = preference * params_.fieldListenAmount
                + fairness * 0.34f;
            if (value > best) {
                best = value;
                selected = body;
            }
        }
        return selected;
    }

    void addContinuousPlayerDrive(
        std::array<float, kAccelerometerFieldMaxBodyCount>& drive)
    {
        if (fieldListenAmountSmoothed_ <= 1.0e-7f) {
            continuousWeightCountdown_ = 0u;
            playerActivity_ += 0.001f * (0.0f - playerActivity_);
            return;
        }
        if (continuousWeightCountdown_ == 0u) {
            updateContinuousWeightTargets();
            continuousWeightCountdown_ = kContinuousWeightControlInterval - 1u;
        } else {
            --continuousWeightCountdown_;
        }
        float driveScale = 0.19f;
        if (params_.fieldListenResponse
            == AmbiFieldListenerResponse::Excite) {
            driveScale = 0.17f;
        } else if (params_.fieldListenResponse
            == AmbiFieldListenerResponse::Settle) {
            driveScale = 0.18f;
        }
        const float baseDrive = fieldListenAmountSmoothed_ * driveScale;
        for (uint32_t bodyIndex = 0u;
            bodyIndex < activeBodyCount_; ++bodyIndex) {
            continuousWeight_[bodyIndex] = flushDenormal(
                continuousWeight_[bodyIndex]
                + continuousWeightSmoothingCoefficient_
                    * (continuousWeightTarget_[bodyIndex]
                        - continuousWeight_[bodyIndex]));
            Body& body = bodies_[bodyIndex];
            const float noise = bodyRandomSigned(body);
            body.droneSlow += continuousSlowNoiseCoefficient_
                * (noise - body.droneSlow);
            body.droneFast += continuousFastNoiseCoefficient_
                * (noise - body.droneFast);
            const float nextSine = body.droneSine
                    * body.droneIncrementCosine
                + body.droneCosine * body.droneIncrementSine;
            const float nextCosine = body.droneCosine
                    * body.droneIncrementCosine
                - body.droneSine * body.droneIncrementSine;
            body.droneSine = flushDenormal(nextSine);
            body.droneCosine = flushDenormal(nextCosine);
            float rub = body.droneSlow * 1.8f
                + body.droneFast * 0.24f
                + body.droneSine * 0.16f;
            if (params_.fieldListenResponse
                == AmbiFieldListenerResponse::Excite) {
                rub = body.droneSlow * 1.10f
                    + body.droneFast * 0.10f
                    + body.droneSine * 0.34f;
            } else if (params_.fieldListenResponse
                == AmbiFieldListenerResponse::Settle) {
                rub = body.droneSlow * 1.70f
                    + body.droneFast * 0.14f
                    + body.droneSine * 0.16f;
            }
            drive[bodyIndex] += baseDrive
                * continuousWeight_[bodyIndex] * rub;
        }
        playerActivity_ += 0.001f
            * (fieldListenAmountSmoothed_ - playerActivity_);
    }

    void updateContinuousWeightTargets()
    {
        float energySum = 0.0f;
        for (uint32_t body = 0u; body < activeBodyCount_; ++body) {
            energySum += bodies_[body].energyEnvelope;
        }
        const float targetShare = 1.0f
            / static_cast<float>(activeBodyCount_);
        float weightSum = 0.0f;
        for (uint32_t body = 0u; body < activeBodyCount_; ++body) {
            const float share = energySum > 1.0e-8f
                ? bodies_[body].energyEnvelope / energySum : 0.0f;
            const AmbiFieldListenerScore heard = fieldListener_.score(
                bodies_[body].direction, params_.fieldListenMode);
            const float directionalActivity = clamp(
                heard.relativeEnergy * 0.58f
                    + heard.charge * 0.22f
                    + heard.novelty * 0.14f
                    + heard.roughness * 0.06f,
                0.0f, 1.0f);
            float weight = 1.0f;
            if (params_.fieldListenMode == AmbiFieldListenMode::Follow
                || params_.fieldListenMode == AmbiFieldListenMode::Counter) {
                // Counter is already evaluated against the antipodal listener
                // direction by AmbiFieldListener::score(). A nonzero floor
                // prevents a locally active region from starving the field.
                weight = 0.45f + directionalActivity * 2.40f;
            } else {
                const float bodyDeficit = clamp(
                    (targetShare - share)
                        * static_cast<float>(activeBodyCount_),
                    0.0f, 1.0f);
                // Balance inverts relative energy in score(), so this term
                // directly represents a quieter region in the fixed pickup
                // body. The body envelope remains a separate fairness guard.
                weight = 0.08f + bodyDeficit * 3.80f
                    + heard.relativeEnergy * 1.60f;
            }
            continuousWeightTarget_[body] = weight;
            weightSum += weight;
        }
        const float normalization = static_cast<float>(activeBodyCount_)
            / std::max(1.0e-6f, weightSum);
        for (uint32_t body = 0u; body < activeBodyCount_; ++body) {
            continuousWeightTarget_[body] *= normalization;
        }
        for (uint32_t body = activeBodyCount_;
            body < kAccelerometerFieldMaxBodyCount; ++body) {
            continuousWeightTarget_[body] = 0.0f;
            continuousWeight_[body] = 0.0f;
        }
    }

    double sampleRate_ = 0.0;
    float geometrySmoothingCoefficient_ = 1.0f;
    float energyAttackCoefficient_ = 0.01f;
    float energyReleaseCoefficient_ = 0.001f;
    float externalActuatorSmoothingCoefficient_ = 0.01f;
    float continuousWeightSmoothingCoefficient_ = 0.01f;
    float modalFrequencySmoothingCoefficient_ = 0.01f;
    float modalDecaySmoothingCoefficient_ = 0.001f;
    float modalWeightSmoothingCoefficient_ = 0.001f;
    float continuousSlowNoiseCoefficient_ = 0.0012f;
    float continuousFastNoiseCoefficient_ = 0.012f;
    float outputGainSmoothingCoefficient_ = 0.001f;
    float levelDetectorAttackCoefficient_ = 0.001f;
    float levelDetectorReleaseCoefficient_ = 0.00001f;
    float liftRiseCoefficient_ = 0.00001f;
    float liftFallCoefficient_ = 0.001f;
    float outputGuardReleaseCoefficient_ = 0.0001f;
    float midiActuationEnergyReleaseCoefficient_ = 0.00001f;
    float airRadiationSmoothed_ = 0.38f;
    float contactRadiationSmoothed_ = 0.58f;
    float skinExtentSmoothed_ = 0.0f;
    float couplingSmoothed_ = 0.35f;
    float fieldListenAmountSmoothed_ = 0.62f;
    float externalDriveSmoothed_ = 0.70f;
    float modalDriveScaleSmoothed_ = 1.0f;
    AccelerometerFieldParams params_ = accelerometerFieldFactoryPreset(0u);
    std::array<Mode, kAccelerometerFieldModeBudget> modes_ {};
    std::array<Body, kAccelerometerFieldMaxBodyCount> bodies_ {};
    std::array<float, kAccelerometerFieldMaxBodyCount>
        actuatorDriveEnvelope_ {};
    std::array<float, kAccelerometerFieldMaxBodyCount>
        continuousWeightTarget_ {};
    std::array<float, kAccelerometerFieldMaxBodyCount>
        continuousWeight_ {};
    AmbiFieldListener fieldListener_ {};
    uint32_t activeBodyCount_ = 6u;
    uint32_t externalBody_ = 0u;
    uint32_t nextRoundRobinBody_ = 0u;
    uint32_t lastActuatedBody_ = 0u;
    float externalLevelEnvelope_ = 0.0f;
    float couplingSlowSine_ = 0.0f;
    float couplingSlowCosine_ = 1.0f;
    float couplingFastSine_ = 1.0f;
    float couplingFastCosine_ = 0.0f;
    float couplingSlowIncrementSine_ = 0.0f;
    float couplingSlowIncrementCosine_ = 1.0f;
    float couplingFastIncrementSine_ = 0.0f;
    float couplingFastIncrementCosine_ = 1.0f;
    float listenerTargetPosition_ = 0.5f;
    float playerActivity_ = 0.0f;
    float outputGainSmoothed_ = 1.0f;
    float modalLevelEnvelope_ = 0.0f;
    float modalLiftDb_ = 0.0f;
    float outputGuardGain_ = 1.0f;
    std::atomic<float> outputGainTargetDb_ { -11.0f };
    std::atomic<float> modalLiftTarget_ { 0.65f };
    float lastPerformancePitchRatio_ = 1.0f;
    uint32_t dynamicBodyIndex_ = 0u;
    uint32_t dynamicCoefficientCountdown_ = 0u;
    static constexpr uint32_t kCouplingOscillatorRenormalizeInterval = 4096u;
    uint32_t couplingOscillatorRenormalizeCountdown_ =
        kCouplingOscillatorRenormalizeInterval;
    static constexpr uint32_t kContinuousWeightControlInterval = 32u;
    uint32_t continuousWeightCountdown_ = 0u;
};

} // namespace s3g
