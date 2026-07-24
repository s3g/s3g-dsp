#include "s3g_ambi_neural_ecology.h"
#include "s3g_ambi_neural_field_lattice.h"
#include "s3g_ambi_neural_ecology_presets.h"
#include "s3g_realtime.h"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_cocoa_gui.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kOutputChannels = s3g::kAmbiNeuralEcologyMaxChannels;
constexpr uint32_t kStateVersion = 11u;
constexpr uint32_t kParamBankSize = 64u;
constexpr uint32_t kCustomPresetMagic = 0x454e3353u; // "S3NE" on little-endian hosts
constexpr uint32_t kCustomPresetVersion = 8u;
constexpr uint32_t kGenomeValuesV2 = 101u;
constexpr uint32_t kGenomeValuesV3 = 117u;

constexpr clap_id kPresetParamId = 1u;
constexpr clap_id kOrderParamId = 2u;
constexpr clap_id kNodeSetParamId = 3u;
constexpr clap_id kActivityParamId = 4u;
constexpr clap_id kDriveParamId = 5u;
constexpr clap_id kRingParamId = 6u;
constexpr clap_id kMatrixParamId = 7u;
constexpr clap_id kHierarchyParamId = 8u;
constexpr clap_id kPhaseParamId = 9u;
constexpr clap_id kRegisterParamId = 10u;
constexpr clap_id kTimeSpreadParamId = 11u;
constexpr clap_id kDiversityParamId = 12u;
constexpr clap_id kBrownianParamId = 13u;
constexpr clap_id kDriftParamId = 14u;
constexpr clap_id kSelfModParamId = 15u;
constexpr clap_id kFieldReturnParamId = 16u;
constexpr clap_id kPropagationParamId = 17u;
constexpr clap_id kPickupFocusParamId = 18u;
constexpr clap_id kPlasticityParamId = 19u;
constexpr clap_id kPlasticityModeParamId = 20u;
constexpr clap_id kFreezeParamId = 21u;
constexpr clap_id kMutateParamId = 22u;
constexpr clap_id kAzimuthParamId = 23u;
constexpr clap_id kElevationParamId = 24u;
constexpr clap_id kDistanceParamId = 25u;
constexpr clap_id kFieldWidthParamId = 26u;
constexpr clap_id kCellWidthParamId = 27u;
constexpr clap_id kMobilityParamId = 28u;
constexpr clap_id kInertiaParamId = 29u;
constexpr clap_id kRotationParamId = 30u;
constexpr clap_id kAirParamId = 31u;
constexpr clap_id kDopplerParamId = 32u;
constexpr clap_id kOutputParamId = 33u;
constexpr clap_id kSeedParamId = 34u;
constexpr clap_id kListeningModeParamId = 35u;
constexpr clap_id kAuditoryPlasticityParamId = 36u;
constexpr clap_id kMetabolismParamId = 37u;
constexpr clap_id kAdaptationParamId = 38u;
constexpr clap_id kScoreVariationParamId = 39u;
constexpr clap_id kScoreRecombineParamId = 40u;
constexpr clap_id kScoreMemoryParamId = 41u;
constexpr clap_id kPickupSetParamId = 42u;
constexpr clap_id kPickupAdaptParamId = 43u;
constexpr clap_id kPickupAnchorParamId = 44u;
constexpr clap_id kScoreModeParamId = 45u;
constexpr clap_id kScoreAmountParamId = 46u;
constexpr clap_id kScoreDwellParamId = 47u;
constexpr clap_id kScoreTransitionParamId = 48u;
constexpr clap_id kScorePlanesParamId = 49u;

struct ParamDef {
    clap_id id;
    const char* name;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
};

constexpr ParamDef kParams[] {
    { kPresetParamId, "Preset", 0.0, static_cast<double>(s3g::kAmbiNeuralEcologyFactoryPresetCount - 1u), 0.0, true },
    { kOrderParamId, "Ambisonic Order", 1.0, 7.0, 3.0, true },
    { kNodeSetParamId, "Node Set", 0.0, 4.0, 2.0, true },
    { kActivityParamId, "Activity Bias", 0.0, 1.0, 0.52, false },
    { kDriveParamId, "Sigmoid Drive", 0.25, 5.0, 1.95, false },
    { kRingParamId, "Ring Feedback", 0.0, 1.25, 0.80, false },
    { kMatrixParamId, "Matrix Coupling", 0.0, 1.25, 0.42, false },
    { kHierarchyParamId, "Hierarchy", 0.0, 1.0, 0.54, false },
    { kPhaseParamId, "Phase Paths", 0.0, 1.0, 0.30, false },
    { kRegisterParamId, "Register", -48.0, 48.0, 0.0, false },
    { kTimeSpreadParamId, "Time Spread", 0.0, 1.6, 1.0, false },
    { kDiversityParamId, "Cell Diversity", 0.0, 1.0, 0.20, false },
    { kBrownianParamId, "Brownian Weights", 0.0, 1.0, 0.10, false },
    { kDriftParamId, "Autonomous Drift", 0.0, 1.0, 0.12, false },
    { kSelfModParamId, "Slow Fast Modulation", 0.0, 1.0, 0.34, false },
    { kFieldReturnParamId, "Field Return", 0.0, 1.0, 0.34, false },
    { kPropagationParamId, "Propagation", 0.0, 180.0, 24.0, false },
    { kPickupFocusParamId, "Pickup Focus", 0.0, 1.0, 0.72, false },
    { kPlasticityParamId, "Plasticity", 0.0, 1.0, 0.0, false },
    { kPlasticityModeParamId, "Plasticity Rule", 0.0, 3.0, 2.0, true },
    { kFreezeParamId, "Freeze Evolution", 0.0, 1.0, 0.0, true },
    { kAzimuthParamId, "Center Azimuth", -180.0, 180.0, 0.0, false },
    { kElevationParamId, "Center Elevation", -89.0, 89.0, 0.0, false },
    { kDistanceParamId, "Center Distance", 0.10, 8.0, 1.0, false },
    { kFieldWidthParamId, "Field Width", 0.0, 1.0, 0.82, false },
    { kCellWidthParamId, "Cell Width", 0.0, 1.0, 0.48, false },
    { kMobilityParamId, "Neural Mobility", 0.0, 1.0, 0.32, false },
    { kInertiaParamId, "Spatial Inertia", 0.0, 1.0, 0.78, false },
    { kRotationParamId, "Rotation Rate", -2.0, 2.0, 0.012, false },
    { kAirParamId, "Air", 0.0, 1.0, 0.10, false },
    { kDopplerParamId, "Doppler", 0.0, 1.0, 0.0, false },
    { kOutputParamId, "Output Gain", -60.0, 6.0, -18.0, false },
    { kSeedParamId, "Circuit Seed", 1.0, 65535.0, 17735.0, true },
    { kListeningModeParamId, "Listening Topology", 0.0, 3.0, 0.0, true },
    { kAuditoryPlasticityParamId, "Auditory Plasticity", 0.0, 1.0, 0.10, false },
    { kMetabolismParamId, "Metabolism Target", 0.0, 1.0, 0.32, false },
    { kAdaptationParamId, "Homeostatic Adaptation", 0.0, 1.0, 0.18, false },
    { kScoreVariationParamId, "Lattice Variation", 0.0, 1.0, 0.38, false },
    { kScoreRecombineParamId, "Lattice Recombine", 0.0, 1.0, 0.62, false },
    { kScoreMemoryParamId, "Lattice Memory", 0.0, 1.0, 0.45, false },
    { kPickupSetParamId, "Directional Pickups", 0.0, 1.0, 0.0, true },
    { kPickupAdaptParamId, "Pickup Adapt", 0.0, 1.0, 0.24, false },
    { kPickupAnchorParamId, "Pickup Anchor", 0.0, 1.0, 0.35, false },
    { kScoreModeParamId, "Field Lattice Mode", 0.0, 3.0, 0.0, true },
    { kScoreAmountParamId, "Field Lattice Amount", 0.0, 1.0, 0.72, false },
    { kScoreDwellParamId, "Field Lattice Dwell", 0.25, 60.0, 8.0, false },
    { kScoreTransitionParamId, "Field Lattice Transition", 0.05, 30.0, 3.0, false },
    { kScorePlanesParamId, "Field Lattice Planes", 0.0, 3.0, 0.0, true },
};

struct ParamsV1 {
    uint32_t order;
    s3g::AmbiNeuralNodeSet nodeSet;
    float activity;
    float drive;
    float ringFeedback;
    float matrixCoupling;
    float hierarchy;
    float phaseShift;
    float registerSemitones;
    float timeSpread;
    float diversity;
    float brownian;
    float drift;
    float selfModulation;
    float fieldReturn;
    float propagationMs;
    float pickupFocus;
    float plasticity;
    s3g::AmbiNeuralPlasticityMode plasticityMode;
    uint32_t freeze;
    uint32_t mutate;
    float centerAzimuthDeg;
    float centerElevationDeg;
    float centerDistance;
    float fieldWidth;
    float cellWidth;
    float mobility;
    float spatialInertia;
    float rotationRateHz;
    float air;
    float doppler;
    float outputGainDb;
    uint32_t seed;
};

struct SavedStateV1 {
    uint32_t version;
    ParamsV1 params;
    uint32_t presetIndex;
    int32_t guiViewMode;
    float guiViewAzDeg;
    float guiViewElDeg;
    float guiViewZoom;
};

static_assert(sizeof(ParamsV1) == 132u);
static_assert(sizeof(SavedStateV1) == 156u);

struct ParamsV2 {
    uint32_t order;
    s3g::AmbiNeuralNodeSet nodeSet;
    float activity;
    float drive;
    float ringFeedback;
    float matrixCoupling;
    float hierarchy;
    float phaseShift;
    float registerSemitones;
    float timeSpread;
    float diversity;
    float brownian;
    float drift;
    float selfModulation;
    float fieldReturn;
    float propagationMs;
    float pickupFocus;
    s3g::AmbiNeuralListeningMode listeningMode;
    float auditoryPlasticity;
    float metabolism;
    float adaptation;
    float plasticity;
    s3g::AmbiNeuralPlasticityMode plasticityMode;
    uint32_t freeze;
    uint32_t mutate;
    float centerAzimuthDeg;
    float centerElevationDeg;
    float centerDistance;
    float fieldWidth;
    float cellWidth;
    float mobility;
    float spatialInertia;
    float rotationRateHz;
    float air;
    float doppler;
    float outputGainDb;
    uint32_t seed;
};

struct SavedStateV2 {
    uint32_t version;
    ParamsV2 params;
    uint32_t presetIndex;
    int32_t guiViewMode;
    float guiViewAzDeg;
    float guiViewElDeg;
    float guiViewZoom;
    std::array<float, kGenomeValuesV2> genome;
};

static_assert(sizeof(ParamsV2) == 148u);
static_assert(sizeof(SavedStateV2) == 576u);

struct ParamsV3 {
    uint32_t order;
    s3g::AmbiNeuralNodeSet nodeSet;
    float activity;
    float drive;
    float ringFeedback;
    float matrixCoupling;
    float hierarchy;
    float phaseShift;
    float registerSemitones;
    float timeSpread;
    float diversity;
    float brownian;
    float drift;
    float selfModulation;
    float fieldReturn;
    float propagationMs;
    float pickupFocus;
    s3g::AmbiNeuralPickupSet pickupSet;
    s3g::AmbiNeuralListeningMode listeningMode;
    float auditoryPlasticity;
    float metabolism;
    float adaptation;
    float genomeMorph;
    float heredity;
    float mutationDepth;
    float plasticity;
    s3g::AmbiNeuralPlasticityMode plasticityMode;
    uint32_t freeze;
    uint32_t mutate;
    float centerAzimuthDeg;
    float centerElevationDeg;
    float centerDistance;
    float fieldWidth;
    float cellWidth;
    float mobility;
    float spatialInertia;
    float rotationRateHz;
    float air;
    float doppler;
    float outputGainDb;
    uint32_t seed;
};

struct SavedStateV3 {
    uint32_t version;
    ParamsV3 params;
    uint32_t presetIndex;
    int32_t guiViewMode;
    float guiViewAzDeg;
    float guiViewElDeg;
    float guiViewZoom;
    std::array<float, kGenomeValuesV3> genomeA;
    std::array<float, kGenomeValuesV3> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
    char customPresetName[64];
};

static_assert(sizeof(ParamsV3) == 164u);
static_assert(sizeof(SavedStateV3) == 1196u);

struct ParamsV4 {
    uint32_t order;
    s3g::AmbiNeuralNodeSet nodeSet;
    float activity;
    float drive;
    float ringFeedback;
    float matrixCoupling;
    float hierarchy;
    float phaseShift;
    float registerSemitones;
    float timeSpread;
    float diversity;
    float brownian;
    float drift;
    float selfModulation;
    float fieldReturn;
    float propagationMs;
    float pickupFocus;
    float pickupAdapt;
    float pickupAnchor;
    s3g::AmbiNeuralPickupSet pickupSet;
    s3g::AmbiNeuralListeningMode listeningMode;
    float auditoryPlasticity;
    float metabolism;
    float adaptation;
    float genomeMorph;
    float heredity;
    float mutationDepth;
    float plasticity;
    s3g::AmbiNeuralPlasticityMode plasticityMode;
    uint32_t freeze;
    uint32_t mutate;
    float centerAzimuthDeg;
    float centerElevationDeg;
    float centerDistance;
    float fieldWidth;
    float cellWidth;
    float mobility;
    float spatialInertia;
    float rotationRateHz;
    float air;
    float doppler;
    float outputGainDb;
    uint32_t seed;
};

struct SavedStateV4 {
    uint32_t version;
    ParamsV4 params;
    uint32_t presetIndex;
    int32_t guiViewMode;
    float guiViewAzDeg;
    float guiViewElDeg;
    float guiViewZoom;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
    char customPresetName[64];
};

static_assert(sizeof(ParamsV4) == 172u);
static_assert(sizeof(SavedStateV4) == 1332u);

struct ParamsV5 {
    uint32_t order;
    s3g::AmbiNeuralNodeSet nodeSet;
    float activity;
    float drive;
    float ringFeedback;
    float matrixCoupling;
    float hierarchy;
    float phaseShift;
    float registerSemitones;
    float timeSpread;
    float diversity;
    float brownian;
    float drift;
    float selfModulation;
    float fieldReturn;
    float propagationMs;
    float pickupFocus;
    float pickupAdapt;
    float pickupAnchor;
    s3g::AmbiNeuralPickupSet pickupSet;
    s3g::AmbiNeuralListeningMode listeningMode;
    float auditoryPlasticity;
    float metabolism;
    float adaptation;
    float genomeMorph;
    float heredity;
    float mutationDepth;
    float plasticity;
    s3g::AmbiNeuralPlasticityMode plasticityMode;
    uint32_t freeze;
    uint32_t mutate;
    s3g::AmbiNeuralScoreMode scoreMode;
    float scoreAmount;
    float scoreDwellSeconds;
    float scoreTransitionSeconds;
    float centerAzimuthDeg;
    float centerElevationDeg;
    float centerDistance;
    float fieldWidth;
    float cellWidth;
    float mobility;
    float spatialInertia;
    float rotationRateHz;
    float air;
    float doppler;
    float outputGainDb;
    uint32_t seed;
};

struct AmbiNeuralLatticeStorageV1 {
    std::array<s3g::AmbiNeuralLatticeCell, 16u> cells;
    std::array<uint32_t, s3g::kAmbiNeuralLatticeTrail> trail;
    uint32_t trailCount;
    uint32_t currentCell;
    uint32_t selectedCell;
};

struct SavedStateV5 {
    uint32_t version;
    ParamsV5 params;
    uint32_t presetIndex;
    int32_t guiViewMode;
    float guiViewAzDeg;
    float guiViewElDeg;
    float guiViewZoom;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
    char customPresetName[64];
    AmbiNeuralLatticeStorageV1 lattice;
    uint32_t guiScorePage;
};

static_assert(sizeof(ParamsV5) == 188u);

struct SavedStateV6 {
    uint32_t version;
    s3g::AmbiNeuralEcologyParams params;
    uint32_t presetIndex;
    int32_t guiViewMode;
    float guiViewAzDeg;
    float guiViewElDeg;
    float guiViewZoom;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
    char customPresetName[64];
    s3g::AmbiNeuralLatticeStorage lattice;
    uint32_t guiScorePage;
    uint32_t guiLatticeViewPlane;
};

struct SavedStateV7 {
    uint32_t version;
    s3g::AmbiNeuralEcologyParams params;
    uint32_t presetIndex;
    int32_t guiViewMode;
    float guiViewAzDeg;
    float guiViewElDeg;
    float guiViewZoom;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
    char customPresetName[64];
    s3g::AmbiNeuralLatticeStorage lattice;
    uint32_t guiScorePage;
    uint32_t guiLatticeViewPlane;
    // Legacy states had no separate transport: every non-Off score mode was running.
    uint32_t scoreTransportRunning;
};

struct SavedState {
    uint32_t version = kStateVersion;
    s3g::AmbiNeuralEcologyParams params {};
    uint32_t presetIndex = 0u;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA {};
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB {};
    uint32_t genomeAValid = 0u;
    uint32_t genomeBValid = 0u;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> liveGenome {};
    char customPresetName[64] {};
    s3g::AmbiNeuralLatticeStorage lattice = s3g::defaultAmbiNeuralLattice();
    uint32_t guiScorePage = 0u;
    uint32_t guiLatticeViewPlane = 0u;
    uint32_t scoreTransportRunning = 1u;
};

struct CustomPresetFile {
    uint32_t magic = kCustomPresetMagic;
    uint32_t version = kCustomPresetVersion;
    char name[64] {};
    s3g::AmbiNeuralEcologyParams params {};
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA {};
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB {};
    uint32_t genomeAValid = 0u;
    uint32_t genomeBValid = 0u;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> liveGenome {};
    s3g::AmbiNeuralLatticeStorage lattice = s3g::defaultAmbiNeuralLattice();
};

struct CustomPresetFileV1 {
    uint32_t magic;
    uint32_t version;
    char name[64];
    ParamsV3 params;
    std::array<float, kGenomeValuesV3> genomeA;
    std::array<float, kGenomeValuesV3> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
};

static_assert(sizeof(CustomPresetFileV1) == 1180u);

struct CustomPresetFileV2 {
    uint32_t magic;
    uint32_t version;
    char name[64];
    ParamsV4 params;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
};

static_assert(sizeof(CustomPresetFileV2) == 1316u);

struct CustomPresetFileV3 {
    uint32_t magic;
    uint32_t version;
    char name[64];
    ParamsV5 params;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
    AmbiNeuralLatticeStorageV1 lattice;
};


struct CustomPresetFileV4 {
    uint32_t magic;
    uint32_t version;
    char name[64];
    s3g::AmbiNeuralEcologyParams params;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB;
    uint32_t genomeAValid;
    uint32_t genomeBValid;
    s3g::AmbiNeuralLatticeStorage lattice;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    double sampleRate = 48000.0;
    s3g::AmbiNeuralEcology engine {};
    s3g::AmbiNeuralFieldLattice lattice {};
    s3g::AmbiNeuralEcologyParams params {};
    s3g::AmbiNeuralEcologyParams audioParams {};
    std::array<std::atomic<double>, kParamBankSize> parameterValues {};
    std::array<std::atomic<double>, kParamBankSize> guiEffectiveParameterValues {};
    std::atomic<bool> guiEffectiveParametersReady { false };
    std::atomic<uint64_t> guiRevision { 0u };
    uint64_t audioRevision = 0u;
    std::atomic<uint32_t> presetIndex { 0u };
    std::atomic<uint32_t> resetRequest { 0u };
    uint32_t audioResetSerial = 0u;
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyMaxNodes> guiAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyMaxNodes> guiElevation {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyMaxNodes> guiDistance {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyMaxNodes> guiEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyMaxNodes> guiValue {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyMaxNodes> guiActivation {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyPickups> guiPickup {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyPickups> guiPickupAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyPickups> guiPickupElevation {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyPickups> guiPickupAnchorAzimuth {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyPickups> guiPickupAnchorElevation {};
    std::atomic<bool> guiPickupDirectionsReady { false };
    std::array<std::atomic<float>,
        s3g::kAmbiNeuralEcologyLobes * s3g::kAmbiNeuralEcologyPickups> guiAuditoryWeight {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyLobes> guiAuditoryReturn {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyLobes> guiLobeEnergy {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyLobes> guiHomeostaticBias {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyGenomeValues> guiGenome {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyGenomeValues> genomeA {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyGenomeValues> genomeB {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyGenomeValues> pendingGenome {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyGenomeValues> pendingGenomeA {};
    std::array<std::atomic<float>, s3g::kAmbiNeuralEcologyGenomeValues> pendingGenomeB {};
    std::atomic<uint32_t> genomeAValid { 0u };
    std::atomic<uint32_t> genomeBValid { 0u };
    std::atomic<uint32_t> genomeRequest { 0u };
    uint32_t audioGenomeSerial = 0u;
    std::atomic<uint32_t> genomeSlotsRequest { 0u };
    uint32_t audioGenomeSlotsSerial = 0u;
    std::array<std::atomic<float>,
        s3g::kAmbiNeuralLatticeCells * s3g::kAmbiNeuralEcologyGenomeValues> latticeGenomes {};
    std::array<std::atomic<float>,
        s3g::kAmbiNeuralLatticeCells * s3g::kAmbiNeuralLatticeExpressionValues>
        latticeExpressions {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiNeuralLatticeCells> latticeGenerations {};
    std::array<std::atomic<uint32_t>,
        s3g::kAmbiNeuralLatticeCells * s3g::kAmbiNeuralLatticeDirections> latticeEdges {};
    std::array<std::atomic<uint32_t>,
        s3g::kAmbiNeuralLatticeMaxPlanes> latticeIngressCells {};
    std::array<std::atomic<uint32_t>,
        s3g::kAmbiNeuralLatticeMaxPlanes> latticeEgressCells {};
    std::atomic<uint32_t> latticePlaneCount { 1u };
    std::atomic<uint32_t> latticeBreedingSeed { 0x4c415454u };
    std::atomic<uint32_t> latticeBirthCount { 0u };
    std::atomic<uint32_t> latticeCellsRevision { 0u };
    uint32_t audioLatticeCellsRevision = 0u;
    std::array<std::atomic<uint32_t>, s3g::kAmbiNeuralLatticeTrail> pendingLatticeTrail {};
    std::atomic<uint32_t> pendingLatticeTrailCount { 1u };
    std::atomic<uint32_t> pendingLatticeCurrentCell { 5u };
    std::atomic<uint32_t> pendingLatticePlaneCount { 1u };
    std::atomic<uint32_t> latticeLoadRequest { 0u };
    uint32_t audioLatticeLoadSerial = 0u;
    std::atomic<uint32_t> pendingScoreCell { 5u };
    std::atomic<float> pendingScoreForce { 1.0f };
    std::atomic<uint32_t> scoreCellRequest { 0u };
    uint32_t audioScoreCellSerial = 0u;
    std::atomic<uint32_t> pendingScoreRecallCell { 5u };
    std::atomic<uint32_t> scoreRecallRequest { 0u };
    uint32_t audioScoreRecallSerial = 0u;
    uint32_t audioLatticeEventSerial = 0u;
    std::array<float, s3g::kAmbiNeuralLatticeExpressionValues> audioExpressionCurrent {};
    std::array<float, s3g::kAmbiNeuralLatticeExpressionValues> audioExpressionFrom {};
    std::array<float, s3g::kAmbiNeuralLatticeExpressionValues> audioExpressionTarget {};
    float audioExpressionProgress = 1.0f;
    float audioExpressionDuration = 0.0f;
    bool audioExpressionActive = false;
    std::atomic<bool> scoreTransportRunning { false };
    bool audioScoreTransportRunning = false;
    std::atomic<uint32_t> guiLatticeCurrentCell { 5u };
    std::atomic<uint32_t> guiLatticeTargetCell { 5u };
    std::atomic<uint32_t> guiLatticePlaneCount { 1u };
    std::atomic<float> guiLatticeTransition { 1.0f };
    std::atomic<float> guiLatticeDwell { 0.0f };
    std::array<std::atomic<float>, 8u> guiLatticeVotes {};
    std::array<std::atomic<uint32_t>, s3g::kAmbiNeuralLatticeTrail> guiLatticeTrail {};
    std::atomic<uint32_t> guiLatticeTrailCount { 1u };
    char customPresetName[64] {};
    std::atomic<bool> customPresetActive { false };
    uint32_t randomSeed = 0x4e45554cu;
    int32_t guiViewMode = 2;
    float guiViewAzDeg = 38.0f;
    float guiViewElDeg = 32.0f;
    float guiViewZoom = 1.0f;
    uint32_t guiScorePage = 0u;
    uint32_t guiSelectedLatticeCell = 5u;
    uint32_t guiLatticeViewPlane = 0u;
#if defined(__APPLE__)
    void* guiView = nullptr;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
    bool guiVisible = false;
#endif
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

s3g::AmbiNeuralLatticeStorage snapshotLattice(const Plugin& plugin);
void requestLatticeStorage(Plugin& plugin, s3g::AmbiNeuralLatticeStorage source,
    bool requestProcess = true);

s3g::AmbiNeuralEcologyParams upgradeParams(const ParamsV1& old)
{
    s3g::AmbiNeuralEcologyParams params {};
    params.order = old.order;
    params.nodeSet = old.nodeSet;
    params.activity = old.activity;
    params.drive = old.drive;
    params.ringFeedback = old.ringFeedback;
    params.matrixCoupling = old.matrixCoupling;
    params.hierarchy = old.hierarchy;
    params.phaseShift = old.phaseShift;
    params.registerSemitones = old.registerSemitones;
    params.timeSpread = old.timeSpread;
    params.diversity = old.diversity;
    params.brownian = old.brownian;
    params.drift = old.drift;
    params.selfModulation = old.selfModulation;
    params.fieldReturn = old.fieldReturn;
    params.propagationMs = old.propagationMs;
    params.pickupFocus = old.pickupFocus;
    params.plasticity = old.plasticity;
    params.plasticityMode = old.plasticityMode;
    params.freeze = old.freeze;
    params.mutate = old.mutate;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.fieldWidth = old.fieldWidth;
    params.cellWidth = old.cellWidth;
    params.mobility = old.mobility;
    params.spatialInertia = old.spatialInertia;
    params.rotationRateHz = old.rotationRateHz;
    params.air = old.air;
    params.doppler = old.doppler;
    params.outputGainDb = old.outputGainDb;
    params.seed = old.seed;
    return s3g::sanitizeAmbiNeuralEcologyParams(params);
}

s3g::AmbiNeuralEcologyParams upgradeParams(const ParamsV2& old)
{
    s3g::AmbiNeuralEcologyParams params {};
    params.order = old.order;
    params.nodeSet = old.nodeSet;
    params.activity = old.activity;
    params.drive = old.drive;
    params.ringFeedback = old.ringFeedback;
    params.matrixCoupling = old.matrixCoupling;
    params.hierarchy = old.hierarchy;
    params.phaseShift = old.phaseShift;
    params.registerSemitones = old.registerSemitones;
    params.timeSpread = old.timeSpread;
    params.diversity = old.diversity;
    params.brownian = old.brownian;
    params.drift = old.drift;
    params.selfModulation = old.selfModulation;
    params.fieldReturn = old.fieldReturn;
    params.propagationMs = old.propagationMs;
    params.pickupFocus = old.pickupFocus;
    params.listeningMode = old.listeningMode;
    params.auditoryPlasticity = old.auditoryPlasticity;
    params.metabolism = old.metabolism;
    params.adaptation = old.adaptation;
    params.plasticity = old.plasticity;
    params.plasticityMode = old.plasticityMode;
    params.freeze = old.freeze;
    params.mutate = old.mutate;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.fieldWidth = old.fieldWidth;
    params.cellWidth = old.cellWidth;
    params.mobility = old.mobility;
    params.spatialInertia = old.spatialInertia;
    params.rotationRateHz = old.rotationRateHz;
    params.air = old.air;
    params.doppler = old.doppler;
    params.outputGainDb = old.outputGainDb;
    params.seed = old.seed;
    return s3g::sanitizeAmbiNeuralEcologyParams(params);
}

s3g::AmbiNeuralEcologyParams upgradeParams(const ParamsV3& old)
{
    s3g::AmbiNeuralEcologyParams params {};
    params.order = old.order;
    params.nodeSet = old.nodeSet;
    params.activity = old.activity;
    params.drive = old.drive;
    params.ringFeedback = old.ringFeedback;
    params.matrixCoupling = old.matrixCoupling;
    params.hierarchy = old.hierarchy;
    params.phaseShift = old.phaseShift;
    params.registerSemitones = old.registerSemitones;
    params.timeSpread = old.timeSpread;
    params.diversity = old.diversity;
    params.brownian = old.brownian;
    params.drift = old.drift;
    params.selfModulation = old.selfModulation;
    params.fieldReturn = old.fieldReturn;
    params.propagationMs = old.propagationMs;
    params.pickupFocus = old.pickupFocus;
    params.pickupSet = old.pickupSet;
    params.listeningMode = old.listeningMode;
    params.auditoryPlasticity = old.auditoryPlasticity;
    params.metabolism = old.metabolism;
    params.adaptation = old.adaptation;
    params.genomeMorph = old.genomeMorph;
    params.heredity = old.heredity;
    params.mutationDepth = old.mutationDepth;
    params.plasticity = old.plasticity;
    params.plasticityMode = old.plasticityMode;
    params.freeze = old.freeze;
    params.mutate = old.mutate;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.fieldWidth = old.fieldWidth;
    params.cellWidth = old.cellWidth;
    params.mobility = old.mobility;
    params.spatialInertia = old.spatialInertia;
    params.rotationRateHz = old.rotationRateHz;
    params.air = old.air;
    params.doppler = old.doppler;
    params.outputGainDb = old.outputGainDb;
    params.seed = old.seed;
    return s3g::sanitizeAmbiNeuralEcologyParams(params);
}

s3g::AmbiNeuralEcologyParams upgradeParams(const ParamsV4& old)
{
    s3g::AmbiNeuralEcologyParams params {};
    params.order = old.order;
    params.nodeSet = old.nodeSet;
    params.activity = old.activity;
    params.drive = old.drive;
    params.ringFeedback = old.ringFeedback;
    params.matrixCoupling = old.matrixCoupling;
    params.hierarchy = old.hierarchy;
    params.phaseShift = old.phaseShift;
    params.registerSemitones = old.registerSemitones;
    params.timeSpread = old.timeSpread;
    params.diversity = old.diversity;
    params.brownian = old.brownian;
    params.drift = old.drift;
    params.selfModulation = old.selfModulation;
    params.fieldReturn = old.fieldReturn;
    params.propagationMs = old.propagationMs;
    params.pickupFocus = old.pickupFocus;
    params.pickupAdapt = old.pickupAdapt;
    params.pickupAnchor = old.pickupAnchor;
    params.pickupSet = old.pickupSet;
    params.listeningMode = old.listeningMode;
    params.auditoryPlasticity = old.auditoryPlasticity;
    params.metabolism = old.metabolism;
    params.adaptation = old.adaptation;
    params.genomeMorph = old.genomeMorph;
    params.heredity = old.heredity;
    params.mutationDepth = old.mutationDepth;
    params.plasticity = old.plasticity;
    params.plasticityMode = old.plasticityMode;
    params.freeze = old.freeze;
    params.mutate = old.mutate;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.fieldWidth = old.fieldWidth;
    params.cellWidth = old.cellWidth;
    params.mobility = old.mobility;
    params.spatialInertia = old.spatialInertia;
    params.rotationRateHz = old.rotationRateHz;
    params.air = old.air;
    params.doppler = old.doppler;
    params.outputGainDb = old.outputGainDb;
    params.seed = old.seed;
    return s3g::sanitizeAmbiNeuralEcologyParams(params);
}

s3g::AmbiNeuralEcologyParams upgradeParams(const ParamsV5& old)
{
    s3g::AmbiNeuralEcologyParams params {};
    params.order = old.order;
    params.nodeSet = old.nodeSet;
    params.activity = old.activity;
    params.drive = old.drive;
    params.ringFeedback = old.ringFeedback;
    params.matrixCoupling = old.matrixCoupling;
    params.hierarchy = old.hierarchy;
    params.phaseShift = old.phaseShift;
    params.registerSemitones = old.registerSemitones;
    params.timeSpread = old.timeSpread;
    params.diversity = old.diversity;
    params.brownian = old.brownian;
    params.drift = old.drift;
    params.selfModulation = old.selfModulation;
    params.fieldReturn = old.fieldReturn;
    params.propagationMs = old.propagationMs;
    params.pickupFocus = old.pickupFocus;
    params.pickupAdapt = old.pickupAdapt;
    params.pickupAnchor = old.pickupAnchor;
    params.pickupSet = old.pickupSet;
    params.listeningMode = old.listeningMode;
    params.auditoryPlasticity = old.auditoryPlasticity;
    params.metabolism = old.metabolism;
    params.adaptation = old.adaptation;
    params.genomeMorph = old.genomeMorph;
    params.heredity = old.heredity;
    params.mutationDepth = old.mutationDepth;
    params.plasticity = old.plasticity;
    params.plasticityMode = old.plasticityMode;
    params.freeze = old.freeze;
    params.mutate = old.mutate;
    params.scoreMode = old.scoreMode;
    params.scoreAmount = old.scoreAmount;
    params.scoreDwellSeconds = old.scoreDwellSeconds;
    params.scoreTransitionSeconds = old.scoreTransitionSeconds;
    params.centerAzimuthDeg = old.centerAzimuthDeg;
    params.centerElevationDeg = old.centerElevationDeg;
    params.centerDistance = old.centerDistance;
    params.fieldWidth = old.fieldWidth;
    params.cellWidth = old.cellWidth;
    params.mobility = old.mobility;
    params.spatialInertia = old.spatialInertia;
    params.rotationRateHz = old.rotationRateHz;
    params.air = old.air;
    params.doppler = old.doppler;
    params.outputGainDb = old.outputGainDb;
    params.seed = old.seed;
    return s3g::sanitizeAmbiNeuralEcologyParams(params);
}

s3g::AmbiNeuralLatticeStorage upgradeLattice(const AmbiNeuralLatticeStorageV1& old)
{
    auto storage = s3g::defaultAmbiNeuralLattice(1u);
    std::copy(old.cells.begin(), old.cells.end(), storage.cells.begin());
    storage.trail = old.trail;
    storage.trailCount = old.trailCount;
    storage.currentCell = old.currentCell;
    storage.selectedCell = old.selectedCell;
    return s3g::sanitizeAmbiNeuralLatticeStorage(storage);
}

const ParamDef* findParam(clap_id id)
{
    for (const auto& param : kParams) if (param.id == id) return &param;
    return nullptr;
}

bool latticeExpressionGovernsParam(clap_id id)
{
    switch (id) {
    case kActivityParamId:
    case kRingParamId:
    case kMatrixParamId:
    case kDiversityParamId:
    case kFieldReturnParamId:
    case kPickupAdaptParamId:
    case kFieldWidthParamId:
    case kMobilityParamId:
        return true;
    default:
        return false;
    }
}

bool assignParam(s3g::AmbiNeuralEcologyParams& p, clap_id id, double value)
{
    switch (id) {
    case kOrderParamId: p.order = static_cast<uint32_t>(std::lround(value)); return true;
    case kNodeSetParamId: p.nodeSet = static_cast<s3g::AmbiNeuralNodeSet>(static_cast<uint32_t>(std::lround(value))); return true;
    case kActivityParamId: p.activity = static_cast<float>(value); return true;
    case kDriveParamId: p.drive = static_cast<float>(value); return true;
    case kRingParamId: p.ringFeedback = static_cast<float>(value); return true;
    case kMatrixParamId: p.matrixCoupling = static_cast<float>(value); return true;
    case kHierarchyParamId: p.hierarchy = static_cast<float>(value); return true;
    case kPhaseParamId: p.phaseShift = static_cast<float>(value); return true;
    case kRegisterParamId: p.registerSemitones = static_cast<float>(value); return true;
    case kTimeSpreadParamId: p.timeSpread = static_cast<float>(value); return true;
    case kDiversityParamId: p.diversity = static_cast<float>(value); return true;
    case kBrownianParamId: p.brownian = static_cast<float>(value); return true;
    case kDriftParamId: p.drift = static_cast<float>(value); return true;
    case kSelfModParamId: p.selfModulation = static_cast<float>(value); return true;
    case kFieldReturnParamId: p.fieldReturn = static_cast<float>(value); return true;
    case kPropagationParamId: p.propagationMs = static_cast<float>(value); return true;
    case kPickupFocusParamId: p.pickupFocus = static_cast<float>(value); return true;
    case kPickupAdaptParamId: p.pickupAdapt = static_cast<float>(value); return true;
    case kPickupAnchorParamId: p.pickupAnchor = static_cast<float>(value); return true;
    case kPickupSetParamId: p.pickupSet = static_cast<s3g::AmbiNeuralPickupSet>(static_cast<uint32_t>(std::lround(value))); return true;
    case kListeningModeParamId: p.listeningMode = static_cast<s3g::AmbiNeuralListeningMode>(static_cast<uint32_t>(std::lround(value))); return true;
    case kAuditoryPlasticityParamId: p.auditoryPlasticity = static_cast<float>(value); return true;
    case kMetabolismParamId: p.metabolism = static_cast<float>(value); return true;
    case kAdaptationParamId: p.adaptation = static_cast<float>(value); return true;
    case kScoreVariationParamId: p.scoreVariation = static_cast<float>(value); return true;
    case kScoreRecombineParamId: p.scoreRecombine = static_cast<float>(value); return true;
    case kScoreMemoryParamId: p.scoreMemory = static_cast<float>(value); return true;
    case kPlasticityParamId: p.plasticity = static_cast<float>(value); return true;
    case kPlasticityModeParamId: p.plasticityMode = static_cast<s3g::AmbiNeuralPlasticityMode>(static_cast<uint32_t>(std::lround(value))); return true;
    case kFreezeParamId: p.freeze = static_cast<uint32_t>(std::lround(value)); return true;
    case kMutateParamId: p.mutate = static_cast<uint32_t>(std::lround(value)); return true;
    case kScoreModeParamId: p.scoreMode = static_cast<s3g::AmbiNeuralScoreMode>(
        static_cast<uint32_t>(std::lround(value))); return true;
    case kScorePlanesParamId: p.scorePlanes = static_cast<s3g::AmbiNeuralScorePlanes>(
        static_cast<uint32_t>(std::lround(value))); return true;
    case kScoreAmountParamId: p.scoreAmount = static_cast<float>(value); return true;
    case kScoreDwellParamId: p.scoreDwellSeconds = static_cast<float>(value); return true;
    case kScoreTransitionParamId: p.scoreTransitionSeconds = static_cast<float>(value); return true;
    case kAzimuthParamId: p.centerAzimuthDeg = static_cast<float>(value); return true;
    case kElevationParamId: p.centerElevationDeg = static_cast<float>(value); return true;
    case kDistanceParamId: p.centerDistance = static_cast<float>(value); return true;
    case kFieldWidthParamId: p.fieldWidth = static_cast<float>(value); return true;
    case kCellWidthParamId: p.cellWidth = static_cast<float>(value); return true;
    case kMobilityParamId: p.mobility = static_cast<float>(value); return true;
    case kInertiaParamId: p.spatialInertia = static_cast<float>(value); return true;
    case kRotationParamId: p.rotationRateHz = static_cast<float>(value); return true;
    case kAirParamId: p.air = static_cast<float>(value); return true;
    case kDopplerParamId: p.doppler = static_cast<float>(value); return true;
    case kOutputParamId: p.outputGainDb = static_cast<float>(value); return true;
    case kSeedParamId: p.seed = static_cast<uint32_t>(std::lround(value)); return true;
    default: return false;
    }
}

double paramValue(const s3g::AmbiNeuralEcologyParams& p, clap_id id)
{
    switch (id) {
    case kOrderParamId: return p.order;
    case kNodeSetParamId: return static_cast<uint32_t>(p.nodeSet);
    case kActivityParamId: return p.activity;
    case kDriveParamId: return p.drive;
    case kRingParamId: return p.ringFeedback;
    case kMatrixParamId: return p.matrixCoupling;
    case kHierarchyParamId: return p.hierarchy;
    case kPhaseParamId: return p.phaseShift;
    case kRegisterParamId: return p.registerSemitones;
    case kTimeSpreadParamId: return p.timeSpread;
    case kDiversityParamId: return p.diversity;
    case kBrownianParamId: return p.brownian;
    case kDriftParamId: return p.drift;
    case kSelfModParamId: return p.selfModulation;
    case kFieldReturnParamId: return p.fieldReturn;
    case kPropagationParamId: return p.propagationMs;
    case kPickupFocusParamId: return p.pickupFocus;
    case kPickupAdaptParamId: return p.pickupAdapt;
    case kPickupAnchorParamId: return p.pickupAnchor;
    case kPickupSetParamId: return static_cast<uint32_t>(p.pickupSet);
    case kListeningModeParamId: return static_cast<uint32_t>(p.listeningMode);
    case kAuditoryPlasticityParamId: return p.auditoryPlasticity;
    case kMetabolismParamId: return p.metabolism;
    case kAdaptationParamId: return p.adaptation;
    case kScoreVariationParamId: return p.scoreVariation;
    case kScoreRecombineParamId: return p.scoreRecombine;
    case kScoreMemoryParamId: return p.scoreMemory;
    case kPlasticityParamId: return p.plasticity;
    case kPlasticityModeParamId: return static_cast<uint32_t>(p.plasticityMode);
    case kFreezeParamId: return p.freeze;
    case kMutateParamId: return p.mutate;
    case kScoreModeParamId: return static_cast<uint32_t>(p.scoreMode);
    case kScorePlanesParamId: return static_cast<uint32_t>(p.scorePlanes);
    case kScoreAmountParamId: return p.scoreAmount;
    case kScoreDwellParamId: return p.scoreDwellSeconds;
    case kScoreTransitionParamId: return p.scoreTransitionSeconds;
    case kAzimuthParamId: return p.centerAzimuthDeg;
    case kElevationParamId: return p.centerElevationDeg;
    case kDistanceParamId: return p.centerDistance;
    case kFieldWidthParamId: return p.fieldWidth;
    case kCellWidthParamId: return p.cellWidth;
    case kMobilityParamId: return p.mobility;
    case kInertiaParamId: return p.spatialInertia;
    case kRotationParamId: return p.rotationRateHz;
    case kAirParamId: return p.air;
    case kDopplerParamId: return p.doppler;
    case kOutputParamId: return p.outputGainDb;
    case kSeedParamId: return p.seed;
    default: return 0.0;
    }
}

s3g::AmbiNeuralEcologyParams paramsFromBank(const Plugin& plugin, s3g::AmbiNeuralEcologyParams base)
{
    for (const auto& param : kParams) {
        if (param.id == kPresetParamId || param.id >= kParamBankSize) continue;
        assignParam(base, param.id, plugin.parameterValues[param.id].load(std::memory_order_relaxed));
    }
    return s3g::sanitizeAmbiNeuralEcologyParams(base);
}

void storeParamBank(Plugin& plugin, const s3g::AmbiNeuralEcologyParams& params, uint32_t preset)
{
    plugin.presetIndex.store(preset, std::memory_order_relaxed);
    for (const auto& param : kParams) {
        if (param.id >= kParamBankSize) continue;
        const double value = param.id == kPresetParamId ? preset : paramValue(params, param.id);
        plugin.parameterValues[param.id].store(value, std::memory_order_relaxed);
        plugin.guiEffectiveParameterValues[param.id].store(value, std::memory_order_relaxed);
    }
}

void publishParams(Plugin& plugin, s3g::AmbiNeuralEcologyParams params, uint32_t preset, bool requestProcess)
{
    params = s3g::sanitizeAmbiNeuralEcologyParams(params);
    preset = std::min<uint32_t>(preset, s3g::kAmbiNeuralEcologyFactoryPresetCount - 1u);
    plugin.params = params;
    storeParamBank(plugin, params, preset);
    plugin.guiRevision.fetch_add(1u, std::memory_order_release);
    if (requestProcess && plugin.host && plugin.host->request_process) plugin.host->request_process(plugin.host);
}

void syncGuiParams(Plugin& plugin) { plugin.params = paramsFromBank(plugin, plugin.params); }

void syncAudioParams(Plugin& plugin)
{
    const uint64_t revision = plugin.guiRevision.load(std::memory_order_acquire);
    if (revision == plugin.audioRevision) return;
    plugin.audioParams = paramsFromBank(plugin, plugin.audioParams);
    plugin.audioRevision = revision;
    plugin.engine.setParams(plugin.audioParams);
}

void applyGuiParam(Plugin& plugin, clap_id id, double value)
{
    syncGuiParams(plugin);
    if (id == kPresetParamId) {
        const uint32_t preset = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u,
            s3g::kAmbiNeuralEcologyFactoryPresetCount - 1u);
        plugin.customPresetName[0] = '\0';
        plugin.customPresetActive.store(false, std::memory_order_relaxed);
        publishParams(plugin, s3g::ambiNeuralEcologyFactoryPreset(preset), preset, true);
        plugin.scoreTransportRunning.store(
            plugin.params.scoreMode != s3g::AmbiNeuralScoreMode::Off,
            std::memory_order_release);
        auto storage = s3g::resizeAmbiNeuralLattice(snapshotLattice(plugin),
            s3g::ambiNeuralScorePlaneCount(plugin.params.scorePlanes));
        requestLatticeStorage(plugin, storage);
        return;
    }
    if (!assignParam(plugin.params, id, value)) return;
    publishParams(plugin, plugin.params, plugin.presetIndex.load(std::memory_order_relaxed), true);
    if (id == kScoreModeParamId) {
        plugin.scoreTransportRunning.store(
            plugin.params.scoreMode != s3g::AmbiNeuralScoreMode::Off,
            std::memory_order_release);
    }
    if (id == kScorePlanesParamId) {
        auto storage = s3g::resizeAmbiNeuralLattice(snapshotLattice(plugin),
            s3g::ambiNeuralScorePlaneCount(plugin.params.scorePlanes));
        requestLatticeStorage(plugin, storage);
    }
}

void applyAudioParam(Plugin& plugin, clap_id id, double value)
{
    if (!findParam(id)) return;
    if (id == kPresetParamId) {
        const uint32_t preset = std::clamp<uint32_t>(static_cast<uint32_t>(std::lround(value)), 0u,
            s3g::kAmbiNeuralEcologyFactoryPresetCount - 1u);
        plugin.audioParams = s3g::ambiNeuralEcologyFactoryPreset(preset);
        storeParamBank(plugin, plugin.audioParams, preset);
    } else {
        if (!assignParam(plugin.audioParams, id, value)) return;
        plugin.audioParams = s3g::sanitizeAmbiNeuralEcologyParams(plugin.audioParams);
        plugin.parameterValues[id].store(paramValue(plugin.audioParams, id), std::memory_order_relaxed);
    }
    plugin.engine.setParams(plugin.audioParams);
    if (id == kScoreModeParamId) {
        plugin.scoreTransportRunning.store(
            plugin.audioParams.scoreMode != s3g::AmbiNeuralScoreMode::Off,
            std::memory_order_release);
    }
    if (plugin.audioParams.scoreMode == s3g::AmbiNeuralScoreMode::Off) {
        plugin.lattice.stop();
    }
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

std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> upgradeGenomeV2(
    const std::array<float, kGenomeValuesV2>& old)
{
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genome {};
    std::copy(old.begin(), old.begin() + 80u, genome.begin());
    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
        for (uint32_t pickup = 0u; pickup < 4u; ++pickup) {
            genome[80u + lobe * 8u + pickup] = old[80u + lobe * 4u + pickup];
        }
        genome[112u + lobe] = old[96u + lobe];
    }
    genome[116u] = old[100u];
    return genome;
}

std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> upgradeGenomeV3(
    const std::array<float, kGenomeValuesV3>& old)
{
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genome {};
    std::copy(old.begin(), old.end(), genome.begin());
    return genome;
}

std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> morphGenomes(
    const std::array<float, s3g::kAmbiNeuralEcologyGenomeValues>& genomeA,
    const std::array<float, s3g::kAmbiNeuralEcologyGenomeValues>& genomeB,
    bool validA, bool validB, float morph)
{
    if (!validA && !validB) return {};
    if (!validA) return genomeB;
    if (!validB) return genomeA;
    morph = std::clamp(morph, 0.0f, 1.0f);
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> result {};
    for (uint32_t index = 0u; index < 116u; ++index) {
        result[index] = genomeA[index] + (genomeB[index] - genomeA[index]) * morph;
    }
    float phaseDelta = genomeB[116u] - genomeA[116u];
    phaseDelta -= std::round(phaseDelta);
    result[116u] = genomeA[116u] + phaseDelta * morph;
    result[116u] -= std::floor(result[116u]);
    for (uint32_t index = 117u; index < result.size(); ++index) {
        result[index] = genomeA[index] + (genomeB[index] - genomeA[index]) * morph;
    }
    return result;
}

void storeGenomeSlots(Plugin& plugin,
    const std::array<float, s3g::kAmbiNeuralEcologyGenomeValues>& genomeA,
    const std::array<float, s3g::kAmbiNeuralEcologyGenomeValues>& genomeB,
    bool validA, bool validB)
{
    for (uint32_t index = 0u; index < genomeA.size(); ++index) {
        const float a = std::isfinite(genomeA[index]) ? genomeA[index] : 0.0f;
        const float b = std::isfinite(genomeB[index]) ? genomeB[index] : 0.0f;
        plugin.genomeA[index].store(a, std::memory_order_relaxed);
        plugin.genomeB[index].store(b, std::memory_order_relaxed);
        plugin.pendingGenomeA[index].store(a, std::memory_order_relaxed);
        plugin.pendingGenomeB[index].store(b, std::memory_order_relaxed);
    }
    plugin.genomeAValid.store(validA ? 1u : 0u, std::memory_order_relaxed);
    plugin.genomeBValid.store(validB ? 1u : 0u, std::memory_order_relaxed);
    plugin.genomeSlotsRequest.fetch_add(1u, std::memory_order_release);
}

void requestGenomeRecall(Plugin& plugin,
    const std::array<float, s3g::kAmbiNeuralEcologyGenomeValues>& genome)
{
    for (uint32_t index = 0u; index < genome.size(); ++index) {
        plugin.pendingGenome[index].store(
            std::isfinite(genome[index]) ? genome[index] : 0.0f, std::memory_order_relaxed);
    }
    plugin.genomeRequest.fetch_add(1u, std::memory_order_release);
    if (plugin.host && plugin.host->request_process) plugin.host->request_process(plugin.host);
}

void storeLatticeCells(Plugin& plugin, const s3g::AmbiNeuralLatticeStorage& source)
{
    const auto storage = s3g::sanitizeAmbiNeuralLatticeStorage(source);
    for (uint32_t cell = 0u; cell < s3g::kAmbiNeuralLatticeCells; ++cell) {
        for (uint32_t gene = 0u; gene < s3g::kAmbiNeuralEcologyGenomeValues; ++gene) {
            plugin.latticeGenomes[
                cell * s3g::kAmbiNeuralEcologyGenomeValues + gene].store(
                storage.cells[cell].genome[gene], std::memory_order_relaxed);
        }
        for (uint32_t trait = 0u;
            trait < s3g::kAmbiNeuralLatticeExpressionValues; ++trait) {
            plugin.latticeExpressions[
                cell * s3g::kAmbiNeuralLatticeExpressionValues + trait].store(
                storage.cells[cell].expression[trait], std::memory_order_relaxed);
        }
        plugin.latticeGenerations[cell].store(
            storage.cells[cell].generation, std::memory_order_relaxed);
        for (uint32_t direction = 0u; direction < s3g::kAmbiNeuralLatticeDirections; ++direction) {
            plugin.latticeEdges[cell * s3g::kAmbiNeuralLatticeDirections + direction].store(
                storage.edges[cell * s3g::kAmbiNeuralLatticeDirections + direction],
                std::memory_order_relaxed);
        }
    }
    for (uint32_t plane = 0u;
        plane < s3g::kAmbiNeuralLatticeMaxPlanes; ++plane) {
        plugin.latticeIngressCells[plane].store(
            storage.ingressCells[plane], std::memory_order_relaxed);
        plugin.latticeEgressCells[plane].store(
            storage.egressCells[plane], std::memory_order_relaxed);
    }
    plugin.latticePlaneCount.store(storage.planeCount, std::memory_order_relaxed);
    plugin.latticeBreedingSeed.store(storage.breedingSeed, std::memory_order_relaxed);
    plugin.latticeBirthCount.store(storage.birthCount, std::memory_order_relaxed);
    plugin.latticeCellsRevision.fetch_add(1u, std::memory_order_release);
}

s3g::AmbiNeuralLatticeStorage snapshotLattice(const Plugin& plugin)
{
    auto storage = s3g::defaultAmbiNeuralLattice(
        plugin.latticePlaneCount.load(std::memory_order_relaxed));
    for (uint32_t cell = 0u; cell < s3g::kAmbiNeuralLatticeCells; ++cell) {
        for (uint32_t gene = 0u; gene < s3g::kAmbiNeuralEcologyGenomeValues; ++gene) {
            storage.cells[cell].genome[gene] = plugin.latticeGenomes[
                cell * s3g::kAmbiNeuralEcologyGenomeValues + gene]
                    .load(std::memory_order_relaxed);
        }
        for (uint32_t trait = 0u;
            trait < s3g::kAmbiNeuralLatticeExpressionValues; ++trait) {
            storage.cells[cell].expression[trait] = plugin.latticeExpressions[
                cell * s3g::kAmbiNeuralLatticeExpressionValues + trait]
                    .load(std::memory_order_relaxed);
        }
        storage.cells[cell].generation =
            plugin.latticeGenerations[cell].load(std::memory_order_relaxed);
        for (uint32_t direction = 0u; direction < s3g::kAmbiNeuralLatticeDirections; ++direction) {
            storage.edges[cell * s3g::kAmbiNeuralLatticeDirections + direction] =
                plugin.latticeEdges[cell * s3g::kAmbiNeuralLatticeDirections + direction]
                    .load(std::memory_order_relaxed);
        }
    }
    for (uint32_t plane = 0u;
        plane < s3g::kAmbiNeuralLatticeMaxPlanes; ++plane) {
        storage.ingressCells[plane] =
            plugin.latticeIngressCells[plane].load(std::memory_order_relaxed);
        storage.egressCells[plane] =
            plugin.latticeEgressCells[plane].load(std::memory_order_relaxed);
    }
    storage.planeCount = plugin.guiLatticePlaneCount.load(std::memory_order_relaxed);
    storage.breedingSeed = plugin.latticeBreedingSeed.load(std::memory_order_relaxed);
    storage.birthCount = plugin.latticeBirthCount.load(std::memory_order_relaxed);
    storage.currentCell = plugin.guiLatticeCurrentCell.load(std::memory_order_relaxed);
    storage.selectedCell = std::min<uint32_t>(
        plugin.guiSelectedLatticeCell, s3g::kAmbiNeuralLatticeCells - 1u);
    storage.trailCount = plugin.guiLatticeTrailCount.load(std::memory_order_relaxed);
    for (uint32_t index = 0u; index < s3g::kAmbiNeuralLatticeTrail; ++index) {
        storage.trail[index] = plugin.guiLatticeTrail[index].load(std::memory_order_relaxed);
    }
    return s3g::sanitizeAmbiNeuralLatticeStorage(storage);
}

void requestLatticeStorage(Plugin& plugin, s3g::AmbiNeuralLatticeStorage source,
    bool requestProcess)
{
    const auto storage = s3g::sanitizeAmbiNeuralLatticeStorage(source);
    storeLatticeCells(plugin, storage);
    plugin.pendingLatticePlaneCount.store(storage.planeCount, std::memory_order_relaxed);
    plugin.pendingLatticeCurrentCell.store(storage.currentCell, std::memory_order_relaxed);
    plugin.pendingLatticeTrailCount.store(storage.trailCount, std::memory_order_relaxed);
    for (uint32_t index = 0u; index < s3g::kAmbiNeuralLatticeTrail; ++index) {
        plugin.pendingLatticeTrail[index].store(storage.trail[index], std::memory_order_relaxed);
        plugin.guiLatticeTrail[index].store(storage.trail[index], std::memory_order_relaxed);
    }
    plugin.guiLatticeCurrentCell.store(storage.currentCell, std::memory_order_relaxed);
    plugin.guiLatticeTargetCell.store(storage.currentCell, std::memory_order_relaxed);
    plugin.guiLatticePlaneCount.store(storage.planeCount, std::memory_order_relaxed);
    plugin.guiLatticeTrailCount.store(storage.trailCount, std::memory_order_relaxed);
    plugin.guiSelectedLatticeCell = storage.selectedCell;
    plugin.latticeLoadRequest.fetch_add(1u, std::memory_order_release);
    if (requestProcess && plugin.host && plugin.host->request_process) {
        plugin.host->request_process(plugin.host);
    }
}

void requestLatticeCell(Plugin& plugin, uint32_t cell, float force = 1.0f)
{
    plugin.pendingScoreCell.store(
        std::min<uint32_t>(cell,
            plugin.guiLatticePlaneCount.load(std::memory_order_relaxed)
                * s3g::kAmbiNeuralLatticeCellsPerPlane - 1u),
        std::memory_order_relaxed);
    plugin.pendingScoreForce.store(std::clamp(force, 0.0f, 1.0f), std::memory_order_relaxed);
    plugin.scoreCellRequest.fetch_add(1u, std::memory_order_release);
    if (plugin.host && plugin.host->request_process) plugin.host->request_process(plugin.host);
}

void requestLatticeRecall(Plugin& plugin, uint32_t cell)
{
    const uint32_t activeCells =
        plugin.guiLatticePlaneCount.load(std::memory_order_relaxed)
        * s3g::kAmbiNeuralLatticeCellsPerPlane;
    plugin.pendingScoreRecallCell.store(
        std::min<uint32_t>(cell, activeCells - 1u), std::memory_order_relaxed);
    plugin.scoreRecallRequest.fetch_add(1u, std::memory_order_release);
    if (plugin.host && plugin.host->request_process) plugin.host->request_process(plugin.host);
}

bool saveCustomPresetFile(const char* path, const Plugin& plugin, const char* name)
{
    if (!path || !*path) return false;
    CustomPresetFile file {};
    std::snprintf(file.name, sizeof(file.name), "%s", name && *name ? name : "Custom");
    file.params = s3g::sanitizeAmbiNeuralEcologyParams(plugin.params);
    for (uint32_t index = 0u; index < file.genomeA.size(); ++index) {
        file.genomeA[index] = plugin.genomeA[index].load(std::memory_order_relaxed);
        file.genomeB[index] = plugin.genomeB[index].load(std::memory_order_relaxed);
        file.liveGenome[index] = plugin.guiGenome[index].load(std::memory_order_relaxed);
    }
    file.genomeAValid = plugin.genomeAValid.load(std::memory_order_relaxed);
    file.genomeBValid = plugin.genomeBValid.load(std::memory_order_relaxed);
    file.lattice = snapshotLattice(plugin);
    FILE* handle = std::fopen(path, "wb");
    if (!handle) return false;
    const bool ok = std::fwrite(&file, 1u, sizeof(file), handle) == sizeof(file);
    std::fclose(handle);
    return ok;
}

bool loadCustomPresetFile(const char* path, CustomPresetFile& file)
{
    if (!path || !*path) return false;
    FILE* handle = std::fopen(path, "rb");
    if (!handle) return false;
    file = {};
    uint32_t header[2] {};
    bool ok = std::fread(header, 1u, sizeof(header), handle) == sizeof(header)
        && header[0] == kCustomPresetMagic;
    if (!ok || header[1] != kCustomPresetVersion) {
        std::fclose(handle);
        return false;
    }
    std::rewind(handle);
    file.version = header[1];
    if (ok && header[1] == 1u) {
        CustomPresetFileV1 old {};
        ok = std::fread(&old, 1u, sizeof(old), handle) == sizeof(old);
        if (ok) {
            std::memcpy(file.name, old.name, sizeof(file.name));
            file.params = upgradeParams(old.params);
            file.genomeA = upgradeGenomeV3(old.genomeA);
            file.genomeB = upgradeGenomeV3(old.genomeB);
            file.genomeAValid = old.genomeAValid;
            file.genomeBValid = old.genomeBValid;
        }
    } else if (ok && header[1] == 2u) {
        CustomPresetFileV2 old {};
        ok = std::fread(&old, 1u, sizeof(old), handle) == sizeof(old);
        if (ok) {
            std::memcpy(file.name, old.name, sizeof(file.name));
            file.params = upgradeParams(old.params);
            file.genomeA = old.genomeA;
            file.genomeB = old.genomeB;
            file.genomeAValid = old.genomeAValid;
            file.genomeBValid = old.genomeBValid;
            file.lattice = s3g::defaultAmbiNeuralLattice();
        }
    } else if (ok && header[1] == 3u) {
        CustomPresetFileV3 old {};
        ok = std::fread(&old, 1u, sizeof(old), handle) == sizeof(old);
        if (ok) {
            std::memcpy(file.name, old.name, sizeof(file.name));
            file.params = upgradeParams(old.params);
            file.genomeA = old.genomeA;
            file.genomeB = old.genomeB;
            file.genomeAValid = old.genomeAValid;
            file.genomeBValid = old.genomeBValid;
            file.lattice = upgradeLattice(old.lattice);
        }
    } else if (ok && header[1] == 4u) {
        CustomPresetFileV4 old {};
        ok = std::fread(&old, 1u, sizeof(old), handle) == sizeof(old);
        if (ok) {
            std::memcpy(file.name, old.name, sizeof(file.name));
            file.params = old.params;
            file.genomeA = old.genomeA;
            file.genomeB = old.genomeB;
            file.genomeAValid = old.genomeAValid;
            file.genomeBValid = old.genomeBValid;
            file.lattice = old.lattice;
        }
    } else if (ok && header[1] == kCustomPresetVersion) {
        ok = std::fread(&file, 1u, sizeof(file), handle) == sizeof(file);
    } else {
        ok = false;
    }
    std::fclose(handle);
    if (!ok) return false;
    file.name[sizeof(file.name) - 1u] = '\0';
    file.params = s3g::sanitizeAmbiNeuralEcologyParams(file.params);
    for (float& value : file.genomeA) if (!std::isfinite(value)) value = 0.0f;
    for (float& value : file.genomeB) if (!std::isfinite(value)) value = 0.0f;
    for (float& value : file.liveGenome) if (!std::isfinite(value)) value = 0.0f;
    file.genomeAValid = std::min<uint32_t>(file.genomeAValid, 1u);
    file.genomeBValid = std::min<uint32_t>(file.genomeBValid, 1u);
    file.lattice = s3g::sanitizeAmbiNeuralLatticeStorage(file.lattice);
    return true;
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
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

bool breedCurrentGenome(Plugin& plugin)
{
    const bool validA = plugin.genomeAValid.load(std::memory_order_relaxed) != 0u;
    const bool validB = plugin.genomeBValid.load(std::memory_order_relaxed) != 0u;
    if (!validA || !validB) return false;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> a {};
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> b {};
    for (uint32_t index = 0u; index < a.size(); ++index) {
        a[index] = plugin.genomeA[index].load(std::memory_order_relaxed);
        b[index] = plugin.genomeB[index].load(std::memory_order_relaxed);
    }

    syncGuiParams(plugin);
    const float bProbability = plugin.params.genomeMorph;
    uint32_t seed = plugin.randomSeed;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> child {};
    auto inheritRows = [&](uint32_t offset, uint32_t rows, uint32_t columns) {
        for (uint32_t row = 0u; row < rows; ++row) {
            const bool fromB = randomUnit(seed) < bProbability;
            for (uint32_t column = 0u; column < columns; ++column) {
                const uint32_t index = offset + row * columns + column;
                child[index] = fromB ? b[index] : a[index];
            }
        }
    };
    inheritRows(0u, 16u, 4u);
    inheritRows(64u, 4u, 4u);
    inheritRows(80u, 4u, 8u);
    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
        child[112u + lobe] = randomUnit(seed) < bProbability ? b[112u + lobe] : a[112u + lobe];
    }
    child[116u] = randomUnit(seed) < bProbability ? b[116u] : a[116u];
    for (uint32_t pickup = 0u; pickup < s3g::kAmbiNeuralEcologyPickups; ++pickup) {
        const bool fromB = randomUnit(seed) < bProbability;
        child[117u + pickup * 2u] = fromB ? b[117u + pickup * 2u] : a[117u + pickup * 2u];
        child[117u + pickup * 2u + 1u] = fromB
            ? b[117u + pickup * 2u + 1u] : a[117u + pickup * 2u + 1u];
    }
    plugin.randomSeed = seed;
    requestGenomeRecall(plugin, child);
    return true;
}

void randomizeSafe(Plugin& plugin)
{
    syncGuiParams(plugin);
    uint32_t seed = plugin.randomSeed;
    const auto params = s3g::randomizeAmbiNeuralEcologyParams(
        plugin.params,
        seed,
        false,
        false);
    plugin.randomSeed = seed;
    std::snprintf(plugin.customPresetName, sizeof(plugin.customPresetName), "%s", "Random");
    plugin.customPresetActive.store(true, std::memory_order_relaxed);
    publishParams(plugin, params, 0u, true);
}

void growCurrentLattice(Plugin& plugin)
{
    syncGuiParams(plugin);
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> founder {};
    for (uint32_t index = 0u; index < founder.size(); ++index) {
        founder[index] = plugin.guiGenome[index].load(std::memory_order_relaxed);
    }
    plugin.randomSeed = plugin.randomSeed * 1664525u + 1013904223u;
    const auto founderExpression =
        s3g::ambiNeuralLatticeExpressionFromParams(plugin.params);
    const auto lattice = s3g::growAmbiNeuralLattice(
        founder, founderExpression, plugin.randomSeed,
        s3g::ambiNeuralScorePlaneCount(plugin.params.scorePlanes),
        plugin.params.scoreVariation, plugin.params.scoreRecombine);
    std::snprintf(plugin.customPresetName, sizeof(plugin.customPresetName), "%s", "Grown Lattice");
    plugin.customPresetActive.store(true, std::memory_order_relaxed);
    requestLatticeStorage(plugin, lattice);
    plugin.guiLatticeViewPlane =
        lattice.currentCell / s3g::kAmbiNeuralLatticeCellsPerPlane;
}

bool init(const clap_plugin_t*) { return true; }

void destroy(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
#if defined(__APPLE__)
    if (p && p->guiView) s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
#endif
    delete p;
}

bool activate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = sampleRate;
    p->engine.prepare(sampleRate);
    p->engine.setParams(p->audioParams);
    p->engine.reset();
    return true;
}

void deactivate(const clap_plugin_t*) {}
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->guiPickupDirectionsReady.store(false, std::memory_order_release);
    p->engine.reset();
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
}

void syncAudioLattice(Plugin& plugin)
{
    const uint32_t loadSerial = plugin.latticeLoadRequest.load(std::memory_order_acquire);
    if (loadSerial != plugin.audioLatticeLoadSerial) {
        plugin.audioLatticeLoadSerial = loadSerial;
        auto storage = s3g::defaultAmbiNeuralLattice(
            plugin.pendingLatticePlaneCount.load(std::memory_order_relaxed));
        for (uint32_t cell = 0u; cell < s3g::kAmbiNeuralLatticeCells; ++cell) {
            for (uint32_t gene = 0u; gene < s3g::kAmbiNeuralEcologyGenomeValues; ++gene) {
                storage.cells[cell].genome[gene] = plugin.latticeGenomes[
                    cell * s3g::kAmbiNeuralEcologyGenomeValues + gene]
                        .load(std::memory_order_relaxed);
            }
            for (uint32_t trait = 0u;
                trait < s3g::kAmbiNeuralLatticeExpressionValues; ++trait) {
                storage.cells[cell].expression[trait] = plugin.latticeExpressions[
                    cell * s3g::kAmbiNeuralLatticeExpressionValues + trait]
                        .load(std::memory_order_relaxed);
            }
            storage.cells[cell].generation =
                plugin.latticeGenerations[cell].load(std::memory_order_relaxed);
            for (uint32_t direction = 0u;
                direction < s3g::kAmbiNeuralLatticeDirections; ++direction) {
                storage.edges[cell * s3g::kAmbiNeuralLatticeDirections + direction] =
                    plugin.latticeEdges[cell * s3g::kAmbiNeuralLatticeDirections + direction]
                        .load(std::memory_order_relaxed);
            }
        }
        for (uint32_t plane = 0u;
            plane < s3g::kAmbiNeuralLatticeMaxPlanes; ++plane) {
            storage.ingressCells[plane] =
                plugin.latticeIngressCells[plane].load(
                    std::memory_order_relaxed);
            storage.egressCells[plane] =
                plugin.latticeEgressCells[plane].load(
                    std::memory_order_relaxed);
        }
        storage.planeCount = plugin.pendingLatticePlaneCount.load(std::memory_order_relaxed);
        storage.currentCell = plugin.pendingLatticeCurrentCell.load(std::memory_order_relaxed);
        storage.selectedCell = plugin.guiSelectedLatticeCell;
        storage.breedingSeed =
            plugin.latticeBreedingSeed.load(std::memory_order_relaxed);
        storage.birthCount = plugin.latticeBirthCount.load(std::memory_order_relaxed);
        storage.trailCount = plugin.pendingLatticeTrailCount.load(std::memory_order_relaxed);
        for (uint32_t index = 0u; index < s3g::kAmbiNeuralLatticeTrail; ++index) {
            storage.trail[index] = plugin.pendingLatticeTrail[index].load(std::memory_order_relaxed);
        }
        plugin.lattice.setStorage(storage);
        plugin.audioExpressionCurrent =
            plugin.lattice.cell(plugin.lattice.currentCell()).expression;
        plugin.audioExpressionFrom = plugin.audioExpressionCurrent;
        plugin.audioExpressionTarget = plugin.audioExpressionCurrent;
        plugin.audioExpressionProgress = 1.0f;
        plugin.audioExpressionActive = false;
        plugin.audioLatticeCellsRevision =
            plugin.latticeCellsRevision.load(std::memory_order_acquire);
    } else {
        const uint32_t cellsRevision = plugin.latticeCellsRevision.load(std::memory_order_acquire);
        if (cellsRevision != plugin.audioLatticeCellsRevision) {
            plugin.audioLatticeCellsRevision = cellsRevision;
            for (uint32_t cell = 0u; cell < s3g::kAmbiNeuralLatticeCells; ++cell) {
                s3g::AmbiNeuralLatticeCell value {};
                for (uint32_t gene = 0u; gene < s3g::kAmbiNeuralEcologyGenomeValues; ++gene) {
                    value.genome[gene] = plugin.latticeGenomes[
                        cell * s3g::kAmbiNeuralEcologyGenomeValues + gene]
                            .load(std::memory_order_relaxed);
                }
                for (uint32_t trait = 0u;
                    trait < s3g::kAmbiNeuralLatticeExpressionValues; ++trait) {
                    value.expression[trait] = plugin.latticeExpressions[
                        cell * s3g::kAmbiNeuralLatticeExpressionValues + trait]
                            .load(std::memory_order_relaxed);
                }
                value.generation =
                    plugin.latticeGenerations[cell].load(std::memory_order_relaxed);
                plugin.lattice.setCell(cell, value);
                for (uint32_t direction = 0u;
                    direction < s3g::kAmbiNeuralLatticeDirections; ++direction) {
                    plugin.lattice.setEdge(cell, direction,
                        plugin.latticeEdges[cell * s3g::kAmbiNeuralLatticeDirections + direction]
                            .load(std::memory_order_relaxed));
                }
            }
        }
    }
    const uint32_t desiredPlanes =
        s3g::ambiNeuralScorePlaneCount(plugin.audioParams.scorePlanes);
    if (plugin.lattice.planeCount() != desiredPlanes) {
        const auto resized = s3g::resizeAmbiNeuralLattice(plugin.lattice.storage(), desiredPlanes);
        plugin.lattice.setStorage(resized);
        plugin.audioExpressionCurrent =
            plugin.lattice.cell(plugin.lattice.currentCell()).expression;
        plugin.audioExpressionFrom = plugin.audioExpressionCurrent;
        plugin.audioExpressionTarget = plugin.audioExpressionCurrent;
        plugin.audioExpressionProgress = 1.0f;
        plugin.audioExpressionActive = false;
        storeLatticeCells(plugin, resized);
        plugin.audioLatticeCellsRevision =
            plugin.latticeCellsRevision.load(std::memory_order_acquire);
        plugin.pendingLatticePlaneCount.store(resized.planeCount, std::memory_order_relaxed);
        plugin.guiLatticePlaneCount.store(resized.planeCount, std::memory_order_relaxed);
        plugin.guiLatticeCurrentCell.store(resized.currentCell, std::memory_order_relaxed);
        plugin.guiLatticeTargetCell.store(resized.currentCell, std::memory_order_relaxed);
        plugin.guiSelectedLatticeCell = resized.selectedCell;
        plugin.guiLatticeViewPlane = std::min<uint32_t>(
            plugin.guiLatticeViewPlane, resized.planeCount - 1u);
    }
    const uint32_t cellSerial = plugin.scoreCellRequest.load(std::memory_order_acquire);
    if (cellSerial != plugin.audioScoreCellSerial) {
        plugin.audioScoreCellSerial = cellSerial;
        plugin.lattice.requestCell(
            plugin.pendingScoreCell.load(std::memory_order_relaxed),
            plugin.pendingScoreForce.load(std::memory_order_relaxed),
            plugin.audioParams.scoreTransitionSeconds);
    }
    const bool transportRunning =
        plugin.scoreTransportRunning.load(std::memory_order_acquire);
    if (transportRunning != plugin.audioScoreTransportRunning) {
        plugin.audioScoreTransportRunning = transportRunning;
        if (!transportRunning) {
            plugin.lattice.stop();
            plugin.audioExpressionFrom = plugin.audioExpressionCurrent;
            plugin.audioExpressionTarget = plugin.audioExpressionCurrent;
            plugin.audioExpressionProgress = 1.0f;
            plugin.audioExpressionActive = false;
        }
    }
    const uint32_t recallSerial = plugin.scoreRecallRequest.load(std::memory_order_acquire);
    if (recallSerial != plugin.audioScoreRecallSerial) {
        plugin.audioScoreRecallSerial = recallSerial;
        plugin.lattice.requestCell(
            plugin.pendingScoreRecallCell.load(std::memory_order_relaxed),
            0.5f, plugin.audioParams.scoreTransitionSeconds, false);
    }
    if (plugin.audioParams.scoreMode == s3g::AmbiNeuralScoreMode::Off) {
        plugin.lattice.stop();
    }
}

bool handleScoreEvent(Plugin& plugin, const clap_event_header_t* event)
{
    if (!plugin.scoreTransportRunning.load(std::memory_order_acquire)) return false;
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    int32_t key = -1;
    float velocity = 0.0f;
    if (event->type == CLAP_EVENT_NOTE_ON) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (note->velocity <= 0.0) return false;
        key = note->key;
        velocity = static_cast<float>(note->velocity);
    } else if (event->type == CLAP_EVENT_MIDI) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        const uint8_t status = midi->data[0] & 0xf0u;
        if (status != 0x90u || midi->data[2] == 0u) return false;
        key = midi->data[1] & 0x7fu;
        velocity = static_cast<float>(midi->data[2]) / 127.0f;
    } else {
        return false;
    }
    const uint32_t pickupCount = plugin.audioParams.pickupSet == s3g::AmbiNeuralPickupSet::Cube8
        ? 8u : 4u;
    const uint32_t noteDirection = static_cast<uint32_t>(std::max(0, key)) % pickupCount;
    const uint32_t direction = pickupCount == 4u ? noteDirection * 2u : noteDirection;
    plugin.lattice.requestDirection(direction, velocity, plugin.audioParams.scoreMode,
        plugin.audioParams.scoreTransitionSeconds);
    return true;
}

void publishLatticeResident(Plugin& plugin, uint32_t cell)
{
    const auto& resident = plugin.lattice.cell(cell);
    for (uint32_t gene = 0u; gene < s3g::kAmbiNeuralEcologyGenomeValues; ++gene) {
        plugin.latticeGenomes[
            cell * s3g::kAmbiNeuralEcologyGenomeValues + gene].store(
            resident.genome[gene], std::memory_order_relaxed);
    }
    for (uint32_t trait = 0u;
        trait < s3g::kAmbiNeuralLatticeExpressionValues; ++trait) {
        plugin.latticeExpressions[
            cell * s3g::kAmbiNeuralLatticeExpressionValues + trait].store(
            resident.expression[trait], std::memory_order_relaxed);
    }
    plugin.latticeGenerations[cell].store(
        resident.generation, std::memory_order_relaxed);
    const auto& storage = plugin.lattice.storage();
    plugin.latticeBreedingSeed.store(storage.breedingSeed, std::memory_order_relaxed);
    plugin.latticeBirthCount.store(storage.birthCount, std::memory_order_relaxed);
}

void applyLatticeGenomeEvent(Plugin& plugin)
{
    const uint32_t serial = plugin.lattice.eventSerial();
    if (serial == plugin.audioLatticeEventSerial) return;
    plugin.audioLatticeEventSerial = serial;
    const uint32_t target = plugin.lattice.eventTargetCell();
    s3g::AmbiNeuralLatticeOffspring offspring {};
    if (plugin.lattice.eventIsReproductive()) {
        offspring = plugin.lattice.performBirth(
            plugin.audioParams.scoreRecombine,
            plugin.audioParams.scoreVariation,
            plugin.audioParams.scoreMemory);
        publishLatticeResident(plugin, target);
    } else {
        offspring.genome = plugin.lattice.cell(target).genome;
        offspring.expression = plugin.lattice.cell(target).expression;
    }
    plugin.audioExpressionFrom = plugin.audioExpressionCurrent;
    plugin.audioExpressionTarget = offspring.expression;
    plugin.audioExpressionProgress = 0.0f;
    plugin.audioExpressionDuration = plugin.lattice.eventTransitionDuration();
    plugin.audioExpressionActive = true;
    plugin.engine.setGenomeTarget(
        offspring.genome, plugin.lattice.eventTransitionDuration(),
        plugin.audioParams.scoreAmount);
}

void advanceLatticeExpression(Plugin& plugin, float seconds)
{
    if (!plugin.audioExpressionActive) return;
    plugin.audioExpressionProgress = std::min(1.0f,
        plugin.audioExpressionProgress
            + seconds / std::max(0.05f, plugin.audioExpressionDuration));
    const float amount = plugin.audioExpressionProgress
        * plugin.audioExpressionProgress
        * (3.0f - 2.0f * plugin.audioExpressionProgress);
    for (uint32_t index = 0u;
        index < plugin.audioExpressionCurrent.size(); ++index) {
        plugin.audioExpressionCurrent[index] = s3g::lerp(
            plugin.audioExpressionFrom[index],
            plugin.audioExpressionTarget[index], amount);
    }
    if (plugin.audioExpressionProgress >= 1.0f) {
        plugin.audioExpressionActive = false;
    }
}

void publishLatticeMeters(Plugin& plugin)
{
    plugin.guiLatticePlaneCount.store(plugin.lattice.planeCount(), std::memory_order_relaxed);
    plugin.guiLatticeCurrentCell.store(plugin.lattice.currentCell(), std::memory_order_relaxed);
    plugin.guiLatticeTargetCell.store(plugin.lattice.targetCell(), std::memory_order_relaxed);
    plugin.guiLatticeTransition.store(plugin.lattice.transitionProgress(), std::memory_order_relaxed);
    plugin.guiLatticeDwell.store(
        plugin.lattice.dwellProgress(plugin.audioParams.scoreDwellSeconds), std::memory_order_relaxed);
    for (uint32_t direction = 0u; direction < 8u; ++direction) {
        plugin.guiLatticeVotes[direction].store(
            plugin.lattice.pickupVote(direction), std::memory_order_relaxed);
    }
    const auto& storage = plugin.lattice.storage();
    plugin.guiLatticeTrailCount.store(storage.trailCount, std::memory_order_relaxed);
    for (uint32_t index = 0u; index < s3g::kAmbiNeuralLatticeTrail; ++index) {
        plugin.guiLatticeTrail[index].store(storage.trail[index], std::memory_order_relaxed);
    }
}

clap_process_status process(const clap_plugin_t* plugin, const clap_process_t* process)
{
    auto* p = self(plugin);
    syncAudioParams(*p);
    syncAudioLattice(*p);
    const uint32_t resetSerial = p->resetRequest.load(std::memory_order_acquire);
    if (resetSerial != p->audioResetSerial) {
        p->audioResetSerial = resetSerial;
        p->guiPickupDirectionsReady.store(false, std::memory_order_release);
        p->engine.setParams(p->audioParams);
        p->engine.reset();
    }
    const uint32_t slotSerial = p->genomeSlotsRequest.load(std::memory_order_acquire);
    if (slotSerial != p->audioGenomeSlotsSerial) {
        p->audioGenomeSlotsSerial = slotSerial;
        std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeA {};
        std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genomeB {};
        for (uint32_t index = 0u; index < genomeA.size(); ++index) {
            genomeA[index] = p->pendingGenomeA[index].load(std::memory_order_relaxed);
            genomeB[index] = p->pendingGenomeB[index].load(std::memory_order_relaxed);
        }
        p->engine.setGenomeSlot(0u, genomeA, p->genomeAValid.load(std::memory_order_relaxed) != 0u);
        p->engine.setGenomeSlot(1u, genomeB, p->genomeBValid.load(std::memory_order_relaxed) != 0u);
    }
    const uint32_t genomeSerial = p->genomeRequest.load(std::memory_order_acquire);
    if (genomeSerial != p->audioGenomeSerial) {
        p->audioGenomeSerial = genomeSerial;
        std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genome {};
        for (uint32_t index = 0u; index < genome.size(); ++index) {
            genome[index] = p->pendingGenome[index].load(std::memory_order_relaxed);
        }
        p->engine.restoreGenome(genome);
    }

    if (process->audio_outputs_count == 0u) {
        if (process->in_events) {
            const uint32_t count = process->in_events->size(process->in_events);
            for (uint32_t index = 0u; index < count; ++index) {
                const clap_event_header_t* event = process->in_events->get(process->in_events, index);
                if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE) {
                    const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
                    applyAudioParam(*p, value->param_id, value->value);
                } else handleScoreEvent(*p, event);
            }
        }
        publishLatticeMeters(*p);
        return CLAP_PROCESS_CONTINUE;
    }

    auto& output = process->audio_outputs[0];
    const uint32_t frames = process->frames_count;
    const uint32_t channels = std::min<uint32_t>(output.channel_count, kOutputChannels);
    if (output.data32) s3g::clearAudioBufferFromChannel(output, 0u, frames);
    if (!output.data32 || channels == 0u) return CLAP_PROCESS_CONTINUE;

    auto renderRange = [&](uint32_t firstFrame, uint32_t frameCount) {
        if (frameCount == 0u) return;
        std::array<float, s3g::kAmbiNeuralEcologyPickups> pickups {};
        for (uint32_t pickup = 0u; pickup < pickups.size(); ++pickup) {
            pickups[pickup] = p->engine.pickupValue(pickup);
        }
        const uint32_t pickupCount = p->audioParams.pickupSet == s3g::AmbiNeuralPickupSet::Cube8
            ? 8u : 4u;
        const float seconds = static_cast<float>(frameCount / p->sampleRate);
        if (p->audioParams.scoreMode != s3g::AmbiNeuralScoreMode::Off) {
            if (p->scoreTransportRunning.load(std::memory_order_acquire)) {
                p->lattice.advance(seconds, pickups, pickupCount,
                    p->audioParams.scoreMode, p->audioParams.scoreDwellSeconds,
                    p->audioParams.scoreTransitionSeconds);
            } else {
                p->lattice.advanceTransition(seconds);
            }
        }
        applyLatticeGenomeEvent(*p);
        advanceLatticeExpression(*p, seconds);
        const auto renderParams =
            p->audioParams.scoreMode == s3g::AmbiNeuralScoreMode::Off
            ? p->audioParams
            : s3g::applyAmbiNeuralLatticeExpression(
                p->audioParams, p->audioExpressionCurrent,
                p->audioParams.scoreAmount);
        for (const auto& param : kParams) {
            if (param.id < kParamBankSize) {
                p->guiEffectiveParameterValues[param.id].store(
                    paramValue(renderParams, param.id), std::memory_order_relaxed);
            }
        }
        p->guiEffectiveParametersReady.store(true, std::memory_order_release);
        p->engine.setParams(renderParams);
        std::array<float*, kOutputChannels> outputs {};
        for (uint32_t channel = 0u; channel < channels; ++channel) {
            outputs[channel] = output.data32[channel] ? output.data32[channel] + firstFrame : nullptr;
        }
        p->engine.process(outputs.data(), channels, frameCount);
    };

    uint32_t rendered = 0u;
    if (process->in_events) {
        const uint32_t count = process->in_events->size(process->in_events);
        for (uint32_t index = 0u; index < count; ++index) {
            const clap_event_header_t* event = process->in_events->get(process->in_events, index);
            if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
            const bool relevant = event->type == CLAP_EVENT_PARAM_VALUE
                || event->type == CLAP_EVENT_NOTE_ON || event->type == CLAP_EVENT_MIDI;
            if (!relevant) continue;
            const uint32_t eventFrame = std::clamp<uint32_t>(event->time, rendered, frames);
            renderRange(rendered, eventFrame - rendered);
            rendered = eventFrame;
            if (event->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
                applyAudioParam(*p, value->param_id, value->value);
            } else {
                handleScoreEvent(*p, event);
            }
        }
    }
    renderRange(rendered, frames - rendered);
    s3g::clearAudioBufferFromChannel(output, channels, frames);

    float peak = 0.0f;
    for (uint32_t channel = 0u; channel < channels; ++channel) {
        if (!output.data32[channel]) continue;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            peak = std::max(peak, std::fabs(output.data32[channel][frame]));
        }
    }
    p->outputPeak.store(std::max(p->outputPeak.load(std::memory_order_relaxed) * 0.91f, peak),
        std::memory_order_relaxed);
    for (uint32_t node = 0u; node < s3g::kAmbiNeuralEcologyMaxNodes; ++node) {
        const auto point = p->engine.nodePosition(node);
        p->guiAzimuth[node].store(point.azimuthDeg, std::memory_order_relaxed);
        p->guiElevation[node].store(point.elevationDeg, std::memory_order_relaxed);
        p->guiDistance[node].store(point.distance, std::memory_order_relaxed);
        p->guiEnergy[node].store(p->engine.nodeEnergy(node), std::memory_order_relaxed);
        p->guiValue[node].store(p->engine.nodeValue(node), std::memory_order_relaxed);
        p->guiActivation[node].store(p->engine.nodeActivation(node), std::memory_order_relaxed);
    }
    for (uint32_t pickup = 0u; pickup < s3g::kAmbiNeuralEcologyPickups; ++pickup) {
        p->guiPickup[pickup].store(p->engine.pickupValue(pickup), std::memory_order_relaxed);
        const auto direction = p->engine.pickupDirection(pickup);
        const auto anchor = p->engine.pickupAnchorDirection(pickup);
        p->guiPickupAzimuth[pickup].store(direction.azimuthDeg, std::memory_order_relaxed);
        p->guiPickupElevation[pickup].store(direction.elevationDeg, std::memory_order_relaxed);
        p->guiPickupAnchorAzimuth[pickup].store(anchor.azimuthDeg, std::memory_order_relaxed);
        p->guiPickupAnchorElevation[pickup].store(anchor.elevationDeg, std::memory_order_relaxed);
    }
    p->guiPickupDirectionsReady.store(true, std::memory_order_release);
    for (uint32_t lobe = 0u; lobe < s3g::kAmbiNeuralEcologyLobes; ++lobe) {
        p->guiAuditoryReturn[lobe].store(p->engine.auditoryReturn(lobe), std::memory_order_relaxed);
        p->guiLobeEnergy[lobe].store(p->engine.lobeEnergy(lobe), std::memory_order_relaxed);
        p->guiHomeostaticBias[lobe].store(p->engine.homeostaticBias(lobe), std::memory_order_relaxed);
        for (uint32_t pickup = 0u; pickup < s3g::kAmbiNeuralEcologyPickups; ++pickup) {
            p->guiAuditoryWeight[lobe * s3g::kAmbiNeuralEcologyPickups + pickup].store(
                p->engine.auditoryWeight(lobe, pickup), std::memory_order_relaxed);
        }
    }
    const auto genome = p->engine.genomeValues();
    for (uint32_t index = 0u; index < genome.size(); ++index) {
        p->guiGenome[index].store(genome[index], std::memory_order_relaxed);
    }
    publishLatticeMeters(*p);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (!info || isInput || index != 0u) return false;
    *info = {};
    info->id = 20u;
    std::strncpy(info->name, "7OA ACN/SN3D Out", sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = kOutputChannels;
    info->port_type = CLAP_PORT_SURROUND;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0u || !info) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Lattice MIDI In", sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    return true;
}

const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return static_cast<uint32_t>(std::size(kParams)); }

const char* paramModule(clap_id id)
{
    if (id >= kScoreModeParamId && id <= kScorePlanesParamId) return "Field Lattice Score";
    if (id <= kNodeSetParamId || id == kOutputParamId || id == kSeedParamId) return "Organism";
    if (id <= kSelfModParamId) return "Circuit";
    if (id <= kMutateParamId || id >= kListeningModeParamId) return "Listening and Evolution";
    return "Ambisonic Field";
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (!info || index >= std::size(kParams)) return false;
    const auto& def = kParams[index];
    *info = {};
    info->id = def.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | (def.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::strncpy(info->name, def.name, sizeof(info->name));
    info->name[sizeof(info->name) - 1u] = '\0';
    std::strncpy(info->module, paramModule(def.id), sizeof(info->module));
    info->module[sizeof(info->module) - 1u] = '\0';
    info->min_value = def.minimum;
    info->max_value = def.maximum;
    info->default_value = def.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !findParam(id)) return false;
    const auto* p = self(plugin);
    if (id == kPresetParamId) {
        *value = p->presetIndex.load(std::memory_order_relaxed);
        return true;
    }
    if (id >= kParamBankSize) return false;
    *value = p->parameterValues[id].load(std::memory_order_relaxed);
    return true;
}

constexpr const char* kNodeSetNames[] { "RING 4", "DUAL 8", "CELL 16", "PAIR 32", "FIELD 64" };
constexpr const char* kPlasticityNames[] { "REINFORCE", "INHIBIT", "BALANCE", "PRUNE" };
constexpr const char* kListeningNames[] { "LOCAL", "CROSS", "DIFFUSE", "ROAMING" };
constexpr const char* kPickupSetNames[] { "TETRA 4", "CUBE 8" };
constexpr const char* kScoreModeNames[] { "OFF", "FIELD", "MIDI", "COUPLED" };
constexpr const char* kScorePlaneNames[] { "1 PLANE", "2 PLANES", "4 PLANES", "8 PLANES" };

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* display, uint32_t size)
{
    if (!display || size == 0u) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", s3g::ambiNeuralEcologyFactoryPresetInfo(
            static_cast<uint32_t>(std::lround(value))).name);
    } else if (id == kOrderParamId) {
        std::snprintf(display, size, "%.0fOA", value);
    } else if (id == kNodeSetParamId) {
        std::snprintf(display, size, "%s", kNodeSetNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 4u)]);
    } else if (id == kPlasticityModeParamId) {
        std::snprintf(display, size, "%s", kPlasticityNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kListeningModeParamId) {
        std::snprintf(display, size, "%s", kListeningNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kPickupSetParamId) {
        std::snprintf(display, size, "%s", kPickupSetNames[std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 1u)]);
    } else if (id == kScoreModeParamId) {
        std::snprintf(display, size, "%s", kScoreModeNames[
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kScorePlanesParamId) {
        std::snprintf(display, size, "%s", kScorePlaneNames[
            std::min<uint32_t>(static_cast<uint32_t>(std::lround(value)), 3u)]);
    } else if (id == kFreezeParamId) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "FROZEN" : "EVOLVING");
    } else if (id == kPropagationParamId) {
        std::snprintf(display, size, "%.1f ms", value);
    } else if (id == kRegisterParamId) {
        std::snprintf(display, size, "%+.1f st", value);
    } else if (id == kAzimuthParamId || id == kElevationParamId) {
        std::snprintf(display, size, "%+.1f deg", value);
    } else if (id == kRotationParamId) {
        std::snprintf(display, size, "%+.3f Hz", value);
    } else if (id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kDistanceParamId) {
        std::snprintf(display, size, "%.2f", value);
    } else if (id == kScoreDwellParamId || id == kScoreTransitionParamId) {
        std::snprintf(display, size, "%.2f s", value);
    } else if (id == kDriveParamId || id == kRingParamId || id == kMatrixParamId
        || id == kTimeSpreadParamId) {
        std::snprintf(display, size, "%.2f", value);
    } else if (id == kMutateParamId || id == kSeedParamId) {
        std::snprintf(display, size, "%.0f", value);
    } else {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* display, double* value)
{
    const auto* param = findParam(id);
    if (!display || !value || !param) return false;
    if (id == kPresetParamId) {
        for (uint32_t index = 0u; index < s3g::kAmbiNeuralEcologyFactoryPresetCount; ++index) {
            if (std::strcmp(display, s3g::ambiNeuralEcologyFactoryPresetInfo(index).name) == 0) {
                *value = index;
                return true;
            }
        }
        return false;
    }
    if (id == kNodeSetParamId) {
        for (uint32_t index = 0u; index < 5u; ++index) {
            if (std::strcmp(display, kNodeSetNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kPlasticityModeParamId) {
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (std::strcmp(display, kPlasticityNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kListeningModeParamId) {
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (std::strcmp(display, kListeningNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kPickupSetParamId) {
        for (uint32_t index = 0u; index < 2u; ++index) {
            if (std::strcmp(display, kPickupSetNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kScoreModeParamId) {
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (std::strcmp(display, kScoreModeNames[index]) == 0) { *value = index; return true; }
        }
    }
    if (id == kScorePlanesParamId) {
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (std::strcmp(display, kScorePlaneNames[index]) == 0) {
                *value = index;
                return true;
            }
        }
    }
    if (id == kFreezeParamId) {
        if (std::strcmp(display, "EVOLVING") == 0) { *value = 0.0; return true; }
        if (std::strcmp(display, "FROZEN") == 0) { *value = 1.0; return true; }
    }
    char* end = nullptr;
    double parsed = std::strtod(display, &end);
    if (end == display) return false;
    if (std::strchr(display, '%')) parsed *= 0.01;
    *value = std::clamp(parsed, param->minimum, param->maximum);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* input, const clap_output_events_t*)
{
    if (!input) return;
    auto* p = self(plugin);
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        applyAudioParam(*p, value->param_id, value->value);
    }
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    auto* p = self(plugin);
    syncGuiParams(*p);
    SavedState state {};
    state.params = p->params;
    state.presetIndex = p->presetIndex.load(std::memory_order_relaxed);
    state.guiViewMode = p->guiViewMode;
    state.guiViewAzDeg = p->guiViewAzDeg;
    state.guiViewElDeg = p->guiViewElDeg;
    state.guiViewZoom = p->guiViewZoom;
    for (uint32_t index = 0u; index < state.genomeA.size(); ++index) {
        state.genomeA[index] = p->genomeA[index].load(std::memory_order_relaxed);
        state.genomeB[index] = p->genomeB[index].load(std::memory_order_relaxed);
        state.liveGenome[index] = p->guiGenome[index].load(std::memory_order_relaxed);
    }
    state.genomeAValid = p->genomeAValid.load(std::memory_order_relaxed);
    state.genomeBValid = p->genomeBValid.load(std::memory_order_relaxed);
    if (p->customPresetActive.load(std::memory_order_relaxed)) {
        std::strncpy(state.customPresetName, p->customPresetName, sizeof(state.customPresetName) - 1u);
    }
    state.lattice = snapshotLattice(*p);
    state.guiScorePage = std::min<uint32_t>(p->guiScorePage, 1u);
    state.guiLatticeViewPlane = std::min<uint32_t>(
        p->guiLatticeViewPlane, state.lattice.planeCount - 1u);
    state.scoreTransportRunning =
        p->scoreTransportRunning.load(std::memory_order_relaxed) ? 1u : 0u;
    return writeExact(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    uint32_t version = 0u;
    if (!readExact(stream, &version, sizeof(version))) return false;
    if (version != kStateVersion) return false;
    SavedState state {};
    if (version == 1u) {
        SavedStateV1 old {};
        old.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&old);
        if (!readExact(stream, bytes + sizeof(version), sizeof(old) - sizeof(version))) return false;
        state.params = upgradeParams(old.params);
        state.presetIndex = old.presetIndex;
        state.guiViewMode = old.guiViewMode;
        state.guiViewAzDeg = old.guiViewAzDeg;
        state.guiViewElDeg = old.guiViewElDeg;
        state.guiViewZoom = old.guiViewZoom;
    } else if (version == 2u) {
        SavedStateV2 old {};
        old.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&old);
        if (!readExact(stream, bytes + sizeof(version), sizeof(old) - sizeof(version))) return false;
        state.params = upgradeParams(old.params);
        state.presetIndex = old.presetIndex;
        state.guiViewMode = old.guiViewMode;
        state.guiViewAzDeg = old.guiViewAzDeg;
        state.guiViewElDeg = old.guiViewElDeg;
        state.guiViewZoom = old.guiViewZoom;
        state.genomeA = upgradeGenomeV2(old.genome);
        state.genomeB = state.genomeA;
        state.genomeAValid = 1u;
        state.genomeBValid = 1u;
    } else if (version == 3u) {
        SavedStateV3 old {};
        old.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&old);
        if (!readExact(stream, bytes + sizeof(version), sizeof(old) - sizeof(version))) return false;
        state.params = upgradeParams(old.params);
        state.presetIndex = old.presetIndex;
        state.guiViewMode = old.guiViewMode;
        state.guiViewAzDeg = old.guiViewAzDeg;
        state.guiViewElDeg = old.guiViewElDeg;
        state.guiViewZoom = old.guiViewZoom;
        state.genomeA = upgradeGenomeV3(old.genomeA);
        state.genomeB = upgradeGenomeV3(old.genomeB);
        state.genomeAValid = old.genomeAValid;
        state.genomeBValid = old.genomeBValid;
        std::memcpy(state.customPresetName, old.customPresetName, sizeof(state.customPresetName));
    } else if (version == 4u) {
        SavedStateV4 old {};
        old.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&old);
        if (!readExact(stream, bytes + sizeof(version), sizeof(old) - sizeof(version))) return false;
        state.params = upgradeParams(old.params);
        state.presetIndex = old.presetIndex;
        state.guiViewMode = old.guiViewMode;
        state.guiViewAzDeg = old.guiViewAzDeg;
        state.guiViewElDeg = old.guiViewElDeg;
        state.guiViewZoom = old.guiViewZoom;
        state.genomeA = old.genomeA;
        state.genomeB = old.genomeB;
        state.genomeAValid = old.genomeAValid;
        state.genomeBValid = old.genomeBValid;
        std::memcpy(state.customPresetName, old.customPresetName, sizeof(state.customPresetName));
        state.lattice = s3g::defaultAmbiNeuralLattice();
    } else if (version == 5u) {
        SavedStateV5 old {};
        old.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&old);
        if (!readExact(stream, bytes + sizeof(version), sizeof(old) - sizeof(version))) return false;
        state.params = upgradeParams(old.params);
        state.presetIndex = old.presetIndex;
        state.guiViewMode = old.guiViewMode;
        state.guiViewAzDeg = old.guiViewAzDeg;
        state.guiViewElDeg = old.guiViewElDeg;
        state.guiViewZoom = old.guiViewZoom;
        state.genomeA = old.genomeA;
        state.genomeB = old.genomeB;
        state.genomeAValid = old.genomeAValid;
        state.genomeBValid = old.genomeBValid;
        std::memcpy(state.customPresetName, old.customPresetName, sizeof(state.customPresetName));
        state.lattice = upgradeLattice(old.lattice);
        state.guiScorePage = old.guiScorePage;
    } else if (version == 6u) {
        SavedStateV6 old {};
        old.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&old);
        if (!readExact(stream, bytes + sizeof(version), sizeof(old) - sizeof(version))) return false;
        state.params = old.params;
        state.presetIndex = old.presetIndex;
        state.guiViewMode = old.guiViewMode;
        state.guiViewAzDeg = old.guiViewAzDeg;
        state.guiViewElDeg = old.guiViewElDeg;
        state.guiViewZoom = old.guiViewZoom;
        state.genomeA = old.genomeA;
        state.genomeB = old.genomeB;
        state.genomeAValid = old.genomeAValid;
        state.genomeBValid = old.genomeBValid;
        std::memcpy(state.customPresetName, old.customPresetName, sizeof(state.customPresetName));
        state.lattice = old.lattice;
        state.guiScorePage = old.guiScorePage;
        state.guiLatticeViewPlane = old.guiLatticeViewPlane;
        state.scoreTransportRunning =
            old.params.scoreMode == s3g::AmbiNeuralScoreMode::Off ? 0u : 1u;
    } else if (version == 7u) {
        SavedStateV7 old {};
        old.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&old);
        if (!readExact(stream, bytes + sizeof(version), sizeof(old) - sizeof(version))) return false;
        std::memcpy(&state, &old, sizeof(old));
    } else if (version == kStateVersion) {
        state.version = version;
        auto* bytes = reinterpret_cast<uint8_t*>(&state);
        if (!readExact(stream, bytes + sizeof(version), sizeof(state) - sizeof(version))) return false;
    } else {
        return false;
    }
    auto* p = self(plugin);
    p->guiViewMode = std::clamp<int32_t>(state.guiViewMode, -1, 2);
    p->guiViewAzDeg = std::clamp(state.guiViewAzDeg, -180.0f, 180.0f);
    p->guiViewElDeg = std::clamp(state.guiViewElDeg, -85.0f, 85.0f);
    p->guiViewZoom = std::clamp(state.guiViewZoom, 0.55f, 2.20f);
    p->guiScorePage = std::min<uint32_t>(state.guiScorePage, 1u);
    p->guiLatticeViewPlane = std::min<uint32_t>(
        state.guiLatticeViewPlane,
        s3g::ambiNeuralScorePlaneCount(state.params.scorePlanes) - 1u);
    state.customPresetName[sizeof(state.customPresetName) - 1u] = '\0';
    std::strncpy(p->customPresetName, state.customPresetName, sizeof(p->customPresetName) - 1u);
    p->customPresetName[sizeof(p->customPresetName) - 1u] = '\0';
    p->customPresetActive.store(p->customPresetName[0] != '\0', std::memory_order_relaxed);
    p->scoreTransportRunning.store(
        state.scoreTransportRunning != 0u
            && state.params.scoreMode != s3g::AmbiNeuralScoreMode::Off,
        std::memory_order_release);
    publishParams(*p, state.params, state.presetIndex, false);
    p->resetRequest.fetch_add(1u, std::memory_order_release);
    const bool validA = state.genomeAValid != 0u;
    const bool validB = state.genomeBValid != 0u;
    storeGenomeSlots(*p, state.genomeA, state.genomeB, validA, validB);
    requestLatticeStorage(*p, state.lattice);
    if (version == kStateVersion) {
        requestGenomeRecall(*p, state.liveGenome);
    } else if (validA || validB) {
        const auto target = morphGenomes(state.genomeA, state.genomeB,
            validA, validB, state.params.genomeMorph);
        requestGenomeRecall(*p, target);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

constexpr uint32_t kGuiWidth = 1160u;
constexpr uint32_t kGuiHeight = 858u;

enum GuiActionFeedback : int {
    kFeedbackNone = 0,
    kFeedbackSave,
    kFeedbackLoad,
    kFeedbackRandom,
    kFeedbackMutate,
    kFeedbackReseed,
    kFeedbackCaptureA,
    kFeedbackRecallA,
    kFeedbackCaptureB,
    kFeedbackRecallB,
    kFeedbackBreed,
    kFeedbackScorePlay,
    kFeedbackScoreStop,
    kFeedbackScoreGo,
    kFeedbackGrowLattice,
};

struct GuiSliderSpec {
    clap_id id;
    const char* label;
    CGFloat x;
    CGFloat y;
    double minimum;
    double maximum;
    bool logarithmic;
};

constexpr GuiSliderSpec kGuiSliders[] {
    { kActivityParamId, "ACTIVITY", 630, 104, 0.0, 1.0, false },
    { kDriveParamId, "SIGMOID", 630, 130, 0.25, 5.0, false },
    { kRingParamId, "RING FB", 630, 156, 0.0, 1.25, false },
    { kMatrixParamId, "MATRIX", 630, 182, 0.0, 1.25, false },
    { kHierarchyParamId, "HIERARCHY", 630, 208, 0.0, 1.0, false },
    { kPhaseParamId, "PHASE", 630, 234, 0.0, 1.0, false },
    { kRegisterParamId, "REGISTER", 630, 260, -48.0, 48.0, false },
    { kTimeSpreadParamId, "TIME SPREAD", 630, 286, 0.0, 1.6, false },
    { kDiversityParamId, "DIVERSITY", 630, 312, 0.0, 1.0, false },
    { kBrownianParamId, "BROWNIAN", 630, 338, 0.0, 1.0, false },
    { kDriftParamId, "DRIFT", 630, 364, 0.0, 1.0, false },
    { kSelfModParamId, "SLOW > FAST", 630, 390, 0.0, 1.0, false },

    { kFieldReturnParamId, "FIELD RETURN", 896, 130, 0.0, 1.0, false },
    { kPropagationParamId, "PROPAGATION", 896, 154, 0.0, 180.0, true },
    { kPickupFocusParamId, "PICKUP FOCUS", 896, 178, 0.0, 1.0, false },
    { kPickupAdaptParamId, "PICKUP ADAPT", 896, 202, 0.0, 1.0, false },
    { kPickupAnchorParamId, "PICKUP ANCHOR", 896, 226, 0.0, 1.0, false },
    { kAuditoryPlasticityParamId, "AUDITORY PLASTIC", 896, 250, 0.0, 1.0, false },
    { kMetabolismParamId, "METABOLISM", 896, 274, 0.0, 1.0, false },
    { kAdaptationParamId, "ADAPTATION", 896, 298, 0.0, 1.0, false },
    { kPlasticityParamId, "CIRCUIT PLASTIC", 896, 322, 0.0, 1.0, false },

    { kScoreVariationParamId, "VARIATION", 48, 664, 0.0, 1.0, false },
    { kScoreRecombineParamId, "RECOMBINE", 48, 690, 0.0, 1.0, false },
    { kScoreMemoryParamId, "MEMORY", 48, 716, 0.0, 1.0, false },
    { kScoreAmountParamId, "AMOUNT", 896, 778, 0.0, 1.0, false },
    { kScoreDwellParamId, "DWELL", 896, 802, 0.25, 60.0, true },
    { kScoreTransitionParamId, "TRANSITION", 896, 826, 0.05, 30.0, true },

    { kAzimuthParamId, "AZIMUTH", 630, 506, -180.0, 180.0, false },
    { kElevationParamId, "ELEVATION", 630, 532, -89.0, 89.0, false },
    { kDistanceParamId, "DISTANCE", 630, 558, 0.10, 8.0, true },
    { kFieldWidthParamId, "FIELD WIDTH", 630, 584, 0.0, 1.0, false },
    { kCellWidthParamId, "CELL WIDTH", 630, 610, 0.0, 1.0, false },
    { kMobilityParamId, "MOBILITY", 630, 636, 0.0, 1.0, false },
    { kInertiaParamId, "INERTIA", 630, 662, 0.0, 1.0, false },
    { kRotationParamId, "ROTATION", 630, 688, -2.0, 2.0, false },
    { kAirParamId, "AIR", 630, 714, 0.0, 1.0, false },
    { kDopplerParamId, "DOPPLER", 630, 740, 0.0, 1.0, false },
    { kOutputParamId, "OUTPUT", 630, 766, -60.0, 6.0, false },
};

const GuiSliderSpec* guiSlider(clap_id id)
{
    for (const auto& slider : kGuiSliders) if (slider.id == id) return &slider;
    return nullptr;
}

double sliderNorm(const GuiSliderSpec& slider, double value)
{
    if (slider.logarithmic) {
        const double minimum = std::max(0.001, slider.minimum);
        const double current = std::max(minimum, value);
        return std::clamp(std::log(current / minimum) / std::log(slider.maximum / minimum), 0.0, 1.0);
    }
    return std::clamp((value - slider.minimum) / std::max(0.000001, slider.maximum - slider.minimum), 0.0, 1.0);
}

double sliderValue(const GuiSliderSpec& slider, NSPoint point)
{
    const double normalized = std::clamp((static_cast<double>(point.x) - (slider.x + 108.0)) / 82.0, 0.0, 1.0);
    if (slider.logarithmic) {
        const double minimum = std::max(0.001, slider.minimum);
        return minimum * std::pow(slider.maximum / minimum, normalized);
    }
    return slider.minimum + normalized * (slider.maximum - slider.minimum);
}

@interface S3GAmbiNeuralEcologyView : NSView {
    Plugin* _plugin;
    NSTimer* _timer;
    int _dragParam;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    uint32_t _selectedNode;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    int _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    NSRect _openMenuRect;
    int _feedbackAction;
    BOOL _feedbackSuccess;
    NSTimeInterval _feedbackUntil;
    BOOL _scorePage;
    uint32_t _selectedLatticeCell;
    uint32_t _viewLatticePlane;
    BOOL _followLatticePlane;
    uint32_t _lastScoreMode;
}
- (instancetype)initWithPlugin:(Plugin*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
@end

@implementation S3GAmbiNeuralEcologyView

- (instancetype)initWithPlugin:(Plugin*)plugin
{
    self = [super initWithFrame:NSMakeRect(0, 0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _timer = nil;
        _dragParam = 0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _selectedNode = 0u;
        _viewMode = plugin ? plugin->guiViewMode : 2;
        _viewAzDeg = plugin ? plugin->guiViewAzDeg : 38.0;
        _viewElDeg = plugin ? plugin->guiViewElDeg : 32.0;
        _viewZoom = plugin ? plugin->guiViewZoom : 1.0;
        _openMenu = 0;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _openMenuRect = NSZeroRect;
        _feedbackAction = kFeedbackNone;
        _feedbackSuccess = YES;
        _feedbackUntil = 0.0;
        _scorePage = plugin ? plugin->guiScorePage != 0u : NO;
        _selectedLatticeCell = plugin
            ? std::min<uint32_t>(plugin->guiSelectedLatticeCell, s3g::kAmbiNeuralLatticeCells - 1u)
            : 5u;
        _viewLatticePlane = plugin ? std::min<uint32_t>(
            plugin->guiLatticeViewPlane,
            plugin->guiLatticePlaneCount.load(std::memory_order_relaxed) - 1u) : 0u;
        _followLatticePlane = YES;
        _lastScoreMode = plugin
            && plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off
            ? static_cast<uint32_t>(plugin->params.scoreMode)
            : static_cast<uint32_t>(s3g::AmbiNeuralScoreMode::Field);
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
    _timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 30.0 target:self
        selector:@selector(timerTick:) userInfo:nil repeats:YES];
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
    syncGuiParams(*_plugin);
    if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off) {
        _lastScoreMode = static_cast<uint32_t>(_plugin->params.scoreMode);
    }
    [self setNeedsDisplay:YES];
}

- (void)showActionFeedback:(int)action success:(BOOL)success
{
    _feedbackAction = action;
    _feedbackSuccess = success;
    _feedbackUntil = [NSDate timeIntervalSinceReferenceDate] + 0.58;
    [self setNeedsDisplay:YES];
}

- (BOOL)feedbackActive:(int)action
{
    return _feedbackAction == action
        && [NSDate timeIntervalSinceReferenceDate] < _feedbackUntil;
}

- (void)storeViewState
{
    _plugin->guiViewMode = _viewMode;
    _plugin->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    _plugin->guiViewElDeg = static_cast<float>(_viewElDeg);
    _plugin->guiViewZoom = static_cast<float>(_viewZoom);
    _plugin->guiScorePage = _scorePage ? 1u : 0u;
    _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
    _plugin->guiLatticeViewPlane = _viewLatticePlane;
}

- (NSString*)presetDisplayName
{
    if (_plugin->customPresetActive.load(std::memory_order_relaxed) && _plugin->customPresetName[0]) {
        return [NSString stringWithUTF8String:_plugin->customPresetName];
    }
    const uint32_t preset = std::min<uint32_t>(
        _plugin->presetIndex.load(std::memory_order_relaxed), s3g::kAmbiNeuralEcologyFactoryPresetCount - 1u);
    return [NSString stringWithUTF8String:s3g::ambiNeuralEcologyFactoryPresetInfo(preset).name];
}

- (NSString*)customPresetDirectory
{
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Music/s3g/Presets/Ambi Neural Ecology"];
}

- (BOOL)saveCustomPreset
{
    if (!_plugin) return NO;
    syncGuiParams(*_plugin);
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES attributes:nil error:nil];
    NSSavePanel* panel = [NSSavePanel savePanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gne" ]];
    [panel setNameFieldStringValue:[NSString stringWithFormat:@"%@.s3gne", [self presetDisplayName]]];
    if ([panel runModal] != NSModalResponseOK) return NO;
    NSString* name = [[[[panel URL] lastPathComponent] stringByDeletingPathExtension] copy];
    const bool saved = saveCustomPresetFile(
        [[[panel URL] path] UTF8String], *_plugin, [name UTF8String]);
    if (saved) {
        std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s", [name UTF8String]);
        _plugin->customPresetActive.store(true, std::memory_order_relaxed);
    }
    [name release];
    [self setNeedsDisplay:YES];
    return saved ? YES : NO;
}

- (BOOL)loadCustomPreset
{
    if (!_plugin) return NO;
    NSString* directory = [self customPresetDirectory];
    [[NSFileManager defaultManager] createDirectoryAtPath:directory
                              withIntermediateDirectories:YES attributes:nil error:nil];
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    [panel setDirectoryURL:[NSURL fileURLWithPath:directory isDirectory:YES]];
    [panel setAllowedFileTypes:@[ @"s3gne" ]];
    [panel setAllowsMultipleSelection:NO];
    [panel setCanChooseDirectories:NO];
    [panel setCanChooseFiles:YES];
    if ([panel runModal] != NSModalResponseOK) return NO;
    CustomPresetFile file {};
    if (!loadCustomPresetFile([[[panel URL] path] UTF8String], file)) return NO;
    std::snprintf(_plugin->customPresetName, sizeof(_plugin->customPresetName), "%s",
        file.name[0] ? file.name : "Custom");
    _plugin->customPresetActive.store(true, std::memory_order_relaxed);
    publishParams(*_plugin, file.params, 0u, true);
    _plugin->scoreTransportRunning.store(
        file.params.scoreMode != s3g::AmbiNeuralScoreMode::Off,
        std::memory_order_release);
    storeGenomeSlots(*_plugin, file.genomeA, file.genomeB,
        file.genomeAValid != 0u, file.genomeBValid != 0u);
    requestLatticeStorage(*_plugin, file.lattice);
    _plugin->resetRequest.fetch_add(1u, std::memory_order_release);
    if (file.version == kCustomPresetVersion) {
        requestGenomeRecall(*_plugin, file.liveGenome);
    } else if (file.genomeAValid || file.genomeBValid) {
        requestGenomeRecall(*_plugin, morphGenomes(file.genomeA, file.genomeB,
            file.genomeAValid != 0u, file.genomeBValid != 0u, file.params.genomeMorph));
    }
    [self setNeedsDisplay:YES];
    return YES;
}

- (void)captureGenomeSlot:(uint32_t)slot
{
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> a {};
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> b {};
    for (uint32_t index = 0u; index < a.size(); ++index) {
        a[index] = _plugin->genomeA[index].load(std::memory_order_relaxed);
        b[index] = _plugin->genomeB[index].load(std::memory_order_relaxed);
        const float live = _plugin->guiGenome[index].load(std::memory_order_relaxed);
        if (slot == 0u) a[index] = live;
        else b[index] = live;
    }
    const bool validA = slot == 0u || _plugin->genomeAValid.load(std::memory_order_relaxed) != 0u;
    const bool validB = slot == 1u || _plugin->genomeBValid.load(std::memory_order_relaxed) != 0u;
    storeGenomeSlots(*_plugin, a, b, validA, validB);
    if (_plugin->host && _plugin->host->request_process) _plugin->host->request_process(_plugin->host);
}

- (BOOL)recallGenomeSlot:(uint32_t)slot
{
    const bool valid = (slot == 0u ? _plugin->genomeAValid : _plugin->genomeBValid)
        .load(std::memory_order_relaxed) != 0u;
    if (!valid) return NO;
    std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genome {};
    for (uint32_t index = 0u; index < genome.size(); ++index) {
        genome[index] = (slot == 0u ? _plugin->genomeA[index] : _plugin->genomeB[index])
            .load(std::memory_order_relaxed);
    }
    requestGenomeRecall(*_plugin, genome);
    return YES;
}

- (NSRect)fieldPanelRect { return NSMakeRect(18, 42, 596, 804); }
- (NSRect)fieldRect { return NSMakeRect(34, 76, 564, 752); }
- (NSRect)presetRect { return NSMakeRect(382, 13, 210, 15); }
- (NSRect)savePresetRect { return NSMakeRect(600, 13, 42, 15); }
- (NSRect)loadPresetRect { return NSMakeRect(648, 13, 42, 15); }
- (NSRect)randomRect { return NSMakeRect(696, 13, 60, 15); }
- (NSRect)mutateRect { return NSMakeRect(762, 13, 60, 15); }
- (NSRect)reseedRect { return NSMakeRect(828, 13, 60, 15); }
- (NSRect)genomeCaptureRect:(uint32_t)slot { return NSMakeRect(976, slot == 0u ? 537 : 563, 38, 15); }
- (NSRect)genomeRecallRect:(uint32_t)slot { return NSMakeRect(1019, slot == 0u ? 537 : 563, 52, 15); }
- (NSRect)breedRect { return NSMakeRect(1004, 683, 62, 15); }
- (NSRect)nodeSetRect { return NSMakeRect(738, 77, 124, 15); }
- (NSRect)orderRect { return NSMakeRect(738, 479, 124, 15); }
- (NSRect)listeningModeRect { return NSMakeRect(1004, 77, 124, 15); }
- (NSRect)pickupSetRect { return NSMakeRect(1004, 103, 124, 15); }
- (NSRect)plasticityModeRect { return NSMakeRect(1004, 345, 124, 15); }
- (NSRect)freezeRect { return NSMakeRect(1004, 369, 124, 15); }
- (NSRect)scoreFollowRect { return NSMakeRect(48, 783, 104, 18); }
- (NSRect)scorePlayRect { return NSMakeRect(218, 783, 58, 18); }
- (NSRect)scoreStopRect { return NSMakeRect(282, 783, 58, 18); }
- (NSRect)scoreGoRect { return NSMakeRect(346, 783, 58, 18); }
- (NSRect)scoreGrowRect { return NSMakeRect(410, 783, 172, 18); }

- (NSRect)scorePageButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(panel.origin.x + 280.0 + index * 51.0, panel.origin.y + 4.0, 46.0, 13.0);
}

- (NSRect)scoreModeButtonRect:(int)index
{
    return NSMakeRect(1002.0 + index * 32.0, 754.0, 29.0, 13.0);
}

- (NSRect)scorePlaneCountButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(panel.origin.x + 398.0 + index * 42.0,
        panel.origin.y + 4.0, 38.0, 13.0);
}

- (NSRect)scorePlaneTabRect:(uint32_t)plane
{
    const NSRect field = [self fieldRect];
    return NSMakeRect(field.origin.x + 12.0 + plane * 66.0,
        field.origin.y + 69.0, 58.0, 15.0);
}

- (NSRect)scoreCellRect:(uint32_t)cell
{
    const NSRect field = [self fieldRect];
    const uint32_t local = cell % s3g::kAmbiNeuralLatticeCellsPerPlane;
    const uint32_t column = local % 4u;
    const uint32_t row = local / 4u;
    return NSMakeRect(field.origin.x + 28.0 + column * 127.0,
        field.origin.y + 92.0 + row * 106.0, 112.0, 84.0);
}

- (NSRect)viewButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(NSMaxX(panel) - 142.0 + index * 43.0, panel.origin.y + 4.0, 38.0, 13.0);
}

- (NSRect)zoomButtonRect:(int)index
{
    const NSRect panel = [self fieldPanelRect];
    return NSMakeRect(NSMaxX(panel) - 196.0 + index * 23.0, panel.origin.y + 4.0, 18.0, 13.0);
}

- (s3g::Vec3)nodeWorld:(uint32_t)node
{
    const float azimuth = _plugin->guiAzimuth[node].load(std::memory_order_relaxed);
    const float elevation = _plugin->guiElevation[node].load(std::memory_order_relaxed);
    const float distance = _plugin->guiDistance[node].load(std::memory_order_relaxed);
    const auto direction = s3g::directionFromAed(azimuth, elevation);
    return { direction.x * distance, direction.y * distance, direction.z * distance };
}

- (NSPoint)projectWorld:(s3g::Vec3)point depth:(CGFloat*)depth
{
    const NSRect field = [self fieldRect];
    const CGFloat scale = std::min(field.size.width, field.size.height) * 0.34
        * std::clamp(_viewZoom, 0.55, 2.20);
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
    return NSMakePoint(NSMidX(field) + x1 * scale, NSMidY(field) - y2 * scale);
}

- (NSPoint)projectNode:(uint32_t)node depth:(CGFloat*)depth
{
    return [self projectWorld:[self nodeWorld:node] depth:depth];
}

- (void)setViewPreset:(int)mode
{
    _viewMode = mode;
    if (mode == 0) { _viewAzDeg = 0.0; _viewElDeg = -90.0; }
    else if (mode == 1) { _viewAzDeg = 0.0; _viewElDeg = 0.0; }
    else { _viewAzDeg = 38.0; _viewElDeg = 32.0; }
    [self storeViewState];
}

- (void)drawScore:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs
            style:(const s3g::clap_gui::Style&)style
{
    const NSRect field = [self fieldRect];
    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);

    const uint32_t planeCount = std::clamp<uint32_t>(
        _plugin->guiLatticePlaneCount.load(std::memory_order_relaxed), 1u,
        s3g::kAmbiNeuralLatticeMaxPlanes);
    const uint32_t activeCells = planeCount * s3g::kAmbiNeuralLatticeCellsPerPlane;
    const uint32_t current = std::min<uint32_t>(
        _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed), activeCells - 1u);
    const uint32_t target = std::min<uint32_t>(
        _plugin->guiLatticeTargetCell.load(std::memory_order_relaxed), activeCells - 1u);
    if (_followLatticePlane) {
        _viewLatticePlane = current / s3g::kAmbiNeuralLatticeCellsPerPlane;
    }
    _viewLatticePlane = std::min<uint32_t>(_viewLatticePlane, planeCount - 1u);
    const float transition = _plugin->guiLatticeTransition.load(std::memory_order_relaxed);
    const float dwell = _plugin->guiLatticeDwell.load(std::memory_order_relaxed);
    const bool scoreEnabled =
        _plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off;
    const bool transportRunning =
        _plugin->scoreTransportRunning.load(std::memory_order_relaxed);
    NSString* transportLabel = !scoreEnabled
        ? @"OFF" : (transportRunning ? @"PLAYING" : @"STOPPED");
    static NSString* directionNames[] = { @"N", @"NE", @"E", @"SE", @"S", @"SW", @"W", @"NW" };

    [[NSString stringWithFormat:@"%@   VIEW P%u/%u   ACTIVE P%u:%c%u  >  P%u:%c%u   MOVE %3.0f%%   DWELL %3.0f%%",
        transportLabel,
        _viewLatticePlane + 1u, planeCount,
        current / 16u + 1u, 'A' + static_cast<int>((current % 16u) / 4u), current % 4u + 1u,
        target / 16u + 1u, 'A' + static_cast<int>((target % 16u) / 4u), target % 4u + 1u,
        transition * 100.0f, dwell * 100.0f]
        drawAtPoint:NSMakePoint(field.origin.x + 12.0, field.origin.y + 12.0)
        withAttributes:valueAttrs];
    [@"PICKUP VOTE" drawAtPoint:NSMakePoint(field.origin.x + 12.0, field.origin.y + 36.0)
        withAttributes:attrs];
    const uint32_t pickupCount = _plugin->params.pickupSet == s3g::AmbiNeuralPickupSet::Cube8 ? 8u : 4u;
    for (uint32_t slot = 0u; slot < pickupCount; ++slot) {
        const uint32_t direction = pickupCount == 4u ? slot * 2u : slot;
        const float vote = std::min(1.0f,
            _plugin->guiLatticeVotes[direction].load(std::memory_order_relaxed) * 5.0f);
        const CGFloat x = field.origin.x + 104.0 + slot * (pickupCount == 4u ? 95.0 : 49.0);
        [directionNames[direction] drawAtPoint:NSMakePoint(x, field.origin.y + 35.0)
            withAttributes:valueAttrs];
        const NSRect meter = NSMakeRect(x + 18.0, field.origin.y + 39.0,
            pickupCount == 4u ? 62.0 : 25.0, 6.0);
        [s3g::clap_gui::color(0x303030) setFill];
        NSRectFill(meter);
        [s3g::clap_gui::color(0xd8d8d8, 0.28 + vote * 0.72) setFill];
        NSRectFill(NSMakeRect(meter.origin.x, meter.origin.y, meter.size.width * vote, meter.size.height));
    }
    [@"3D ROUTE: OUT ↑ ENTERS THE NEXT PLANE AT IN ↓  •  LAST PLANE WRAPS TO P1"
        drawAtPoint:NSMakePoint(field.origin.x + 12.0, field.origin.y + 52.0)
        withAttributes:valueAttrs];
    for (uint32_t plane = 0u; plane < planeCount; ++plane) {
        const NSRect tab = [self scorePlaneTabRect:plane];
        const BOOL viewed = plane == _viewLatticePlane;
        const BOOL active = plane == current / s3g::kAmbiNeuralLatticeCellsPerPlane;
        const BOOL targeted = transition < 1.0f
            && plane == target / s3g::kAmbiNeuralLatticeCellsPerPlane;
        [s3g::clap_gui::color(active ? 0x686868
            : (viewed ? 0x505050 : (targeted ? 0x383838 : 0x242424))) setFill];
        NSRectFill(tab);
        [s3g::clap_gui::color(active ? 0xf0f0f0 : (viewed ? 0xb0b0b0 : 0x5c5c5c)) setStroke];
        NSFrameRect(tab);
        if (viewed) NSFrameRect(NSInsetRect(tab, 2.0, 2.0));
        NSString* tabLabel = active
            ? [NSString stringWithFormat:@"P%u %c%u", plane + 1u,
                'A' + static_cast<int>((current % 16u) / 4u), current % 4u + 1u]
            : (targeted
                ? [NSString stringWithFormat:@"P%u >%c%u", plane + 1u,
                    'A' + static_cast<int>((target % 16u) / 4u), target % 4u + 1u]
                : [NSString stringWithFormat:@"P%u", plane + 1u]);
        [tabLabel drawAtPoint:NSMakePoint(tab.origin.x + 6.0, tab.origin.y + 1.0)
            withAttributes:viewed ? attrs : valueAttrs];
    }

    const uint32_t trailCount = std::clamp<uint32_t>(
        _plugin->guiLatticeTrailCount.load(std::memory_order_relaxed), 1u,
        s3g::kAmbiNeuralLatticeTrail);
    [s3g::clap_gui::color(0xa0a0a0, 0.30) setStroke];
    NSBezierPath* trailPath = [NSBezierPath bezierPath];
    BOOL pathOpen = NO;
    for (uint32_t index = 0u; index < trailCount; ++index) {
        const uint32_t cell = std::min<uint32_t>(
            _plugin->guiLatticeTrail[index].load(std::memory_order_relaxed), activeCells - 1u);
        if (cell / s3g::kAmbiNeuralLatticeCellsPerPlane != _viewLatticePlane) {
            pathOpen = NO;
            continue;
        }
        const NSRect rect = [self scoreCellRect:cell];
        const NSPoint center = NSMakePoint(NSMidX(rect), NSMidY(rect));
        if (!pathOpen) {
            [trailPath moveToPoint:center];
            pathOpen = YES;
        } else {
            const uint32_t previousCell = std::min<uint32_t>(
                _plugin->guiLatticeTrail[index - 1u].load(std::memory_order_relaxed),
                activeCells - 1u);
            const NSRect previous = [self scoreCellRect:previousCell];
            if (previousCell / s3g::kAmbiNeuralLatticeCellsPerPlane != _viewLatticePlane
                || std::hypot(center.x - NSMidX(previous), center.y - NSMidY(previous)) > 170.0) {
                [trailPath moveToPoint:center];
            } else {
                [trailPath lineToPoint:center];
            }
        }
    }
    [trailPath setLineWidth:1.5];
    [trailPath stroke];

    const uint32_t viewedIngress =
        _plugin->latticeIngressCells[_viewLatticePlane].load(
            std::memory_order_relaxed);
    const uint32_t viewedEgress =
        _plugin->latticeEgressCells[_viewLatticePlane].load(
            std::memory_order_relaxed);
    for (uint32_t localCell = 0u;
        localCell < s3g::kAmbiNeuralLatticeCellsPerPlane; ++localCell) {
        const uint32_t cell =
            _viewLatticePlane * s3g::kAmbiNeuralLatticeCellsPerPlane + localCell;
        const NSRect rect = [self scoreCellRect:cell];
        std::array<float, s3g::kAmbiNeuralEcologyGenomeValues> genome {};
        for (uint32_t gene = 0u; gene < genome.size(); ++gene) {
            genome[gene] = _plugin->latticeGenomes[
                cell * s3g::kAmbiNeuralEcologyGenomeValues + gene]
                    .load(std::memory_order_relaxed);
        }
        const auto signature = s3g::ambiNeuralGenomeSignature(genome);
        float mean = 0.0f;
        for (const float value : signature) mean += value;
        mean /= static_cast<float>(signature.size());
        const BOOL isCurrent = cell == current;
        const BOOL isTarget = cell == target && transition < 1.0f;
        const BOOL isSelected = cell == _selectedLatticeCell;
        const BOOL isStoppedSelection = isSelected
            && scoreEnabled && !transportRunning;
        const BOOL isIngress = planeCount > 1u && cell == viewedIngress;
        const BOOL isEgress = planeCount > 1u && cell == viewedEgress;
        const int shade = static_cast<int>(24.0f + mean * 32.0f + (isCurrent ? 34.0f : 0.0f));
        [s3g::clap_gui::color(isStoppedSelection
            ? 0x514722 : ((shade << 16) | (shade << 8) | shade)) setFill];
        NSRectFill(rect);
        [s3g::clap_gui::color(isStoppedSelection ? 0xf0cb55
            : (isSelected ? 0xf0f0f0 : (isCurrent ? 0xc8c8c8 : 0x686868))) setStroke];
        NSFrameRect(rect);
        if (isSelected) {
            [s3g::clap_gui::color(isStoppedSelection ? 0xffdf70 : 0xf0f0f0, 0.82) setStroke];
            NSFrameRect(NSInsetRect(rect, 2.0, 2.0));
        }
        if (isTarget) {
            [s3g::clap_gui::color(0xd8d8d8, 0.72) setStroke];
            NSFrameRect(NSInsetRect(rect, 3.0, 3.0));
        }
        if (isIngress || isEgress) {
            [s3g::clap_gui::color(
                isEgress ? 0xe0e0e0 : 0x909090, 0.88) setStroke];
            NSFrameRect(NSInsetRect(rect, 4.0, 4.0));
        }
        [[NSString stringWithFormat:@"%c%u",
            'A' + static_cast<int>(localCell / 4u), localCell % 4u + 1u]
            drawAtPoint:NSMakePoint(rect.origin.x + 7.0, rect.origin.y + 6.0)
            withAttributes:isStoppedSelection
                ? s3g::clap_gui::textAttrs(s3g::clap_gui::color(0xffdf70), 9.0)
                : (isCurrent ? s3g::clap_gui::softLabelAttrs() : valueAttrs)];
        [[NSString stringWithFormat:@"G%u",
            _plugin->latticeGenerations[cell].load(std::memory_order_relaxed)]
            drawAtPoint:NSMakePoint(rect.origin.x + 82.0, rect.origin.y + 6.0)
            withAttributes:s3g::clap_gui::softLabelAttrs()];
        if (isIngress || isEgress) {
            [(isEgress ? @"OUT↑" : @"IN↓")
                drawAtPoint:NSMakePoint(rect.origin.x + 38.0, rect.origin.y + 6.0)
                withAttributes:isEgress
                    ? s3g::clap_gui::softLabelAttrs() : valueAttrs];
        }
        static NSString* signatureNames[] = { @"C", @"L", @"E", @"P" };
        for (uint32_t signatureIndex = 0u;
            signatureIndex < signature.size(); ++signatureIndex) {
            const CGFloat rowY =
                rect.origin.y + 27.0 + signatureIndex * 13.0;
            [signatureNames[signatureIndex]
                drawAtPoint:NSMakePoint(rect.origin.x + 8.0, rowY - 3.0)
                withAttributes:s3g::clap_gui::softLabelAttrs()];
            const NSRect bar = NSMakeRect(rect.origin.x + 22.0,
                rowY, 82.0, 6.0);
            [s3g::clap_gui::color(0x181818) setFill];
            NSRectFill(bar);
            [s3g::clap_gui::color(isStoppedSelection ? 0xe0bc4f
                : (isCurrent ? 0xe0e0e0 : 0x9a9a9a),
                0.32 + signature[signatureIndex] * 0.68) setFill];
            NSRectFill(NSMakeRect(bar.origin.x, bar.origin.y,
                bar.size.width * signature[signatureIndex], bar.size.height));
        }
    }

    const uint32_t selectedGeneration =
        _plugin->latticeGenerations[_selectedLatticeCell].load(std::memory_order_relaxed);
    const uint32_t selectedPlane =
        _selectedLatticeCell / s3g::kAmbiNeuralLatticeCellsPerPlane;
    NSString* selectedPortal = @"";
    if (planeCount > 1u
        && _selectedLatticeCell
            == _plugin->latticeIngressCells[selectedPlane].load(
                std::memory_order_relaxed)) {
        selectedPortal = [NSString stringWithFormat:@"   •   IN FROM P%u",
            selectedPlane == 0u ? planeCount : selectedPlane];
    } else if (planeCount > 1u
        && _selectedLatticeCell
            == _plugin->latticeEgressCells[selectedPlane].load(
                std::memory_order_relaxed)) {
        selectedPortal = [NSString stringWithFormat:@"   •   OUT TO P%u",
            (selectedPlane + 1u) % planeCount + 1u];
    }
    [[NSString stringWithFormat:
        @"SELECTED P%u:%c%u   —   RESIDENT GENERATION %u%@",
        _selectedLatticeCell / 16u + 1u,
        'A' + static_cast<int>((_selectedLatticeCell % 16u) / 4u),
        _selectedLatticeCell % 4u + 1u,
        selectedGeneration, selectedPortal]
        drawAtPoint:NSMakePoint(field.origin.x + 12.0, field.origin.y + 556.0)
        withAttributes:scoreEnabled && !transportRunning
            ? s3g::clap_gui::textAttrs(s3g::clap_gui::color(0xf0cb55), 9.0)
            : attrs];
    for (const auto& slider : kGuiSliders) {
        if (slider.id == kScoreVariationParamId
            || slider.id == kScoreRecombineParamId
            || slider.id == kScoreMemoryParamId) {
            [self drawSlider:slider style:style];
        }
    }
    [self drawScoreToggleButton:(_followLatticePlane ? @"FOLLOWING ACTIVE" : @"FOLLOW ACTIVE")
        rect:[self scoreFollowRect] active:_followLatticePlane];
    [self drawOrganismActionButton:@"PLAY" rect:[self scorePlayRect]
        action:kFeedbackScorePlay];
    [self drawOrganismActionButton:@"STOP" rect:[self scoreStopRect]
        action:kFeedbackScoreStop];
    [self drawOrganismActionButton:@"GO" rect:[self scoreGoRect] action:kFeedbackScoreGo];
    [self drawOrganismActionButton:@"GROW LATTICE" rect:[self scoreGrowRect]
        action:kFeedbackGrowLattice];
    [@"STOP: CLICK A CELL TO AUDITION ITS RESIDENT.  TRANSITION SETS GESTATION."
        drawAtPoint:NSMakePoint(field.origin.x + 12.0, field.origin.y + 730.0)
        withAttributes:valueAttrs];
}

- (void)drawField:(NSDictionary*)attrs valueAttrs:(NSDictionary*)valueAttrs style:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->params;
    const NSRect panel = [self fieldPanelRect];
    const NSRect field = [self fieldRect];
    s3g::clap_gui::drawPanelFrame(panel.origin.x, panel.origin.y, panel.size.width, panel.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"SELF-LISTENING AMBISONIC FIELD", true,
        panel.origin.x, panel.origin.y, panel.size.width, 21, attrs, style);
    const NSRect header = NSMakeRect(panel.origin.x, panel.origin.y, panel.size.width, 21);
    s3g::clap_gui::drawHeaderButton([self scorePageButtonRect:0], header, @"FIELD",
        !_scorePage, attrs, style);
    s3g::clap_gui::drawHeaderButton([self scorePageButtonRect:1], header, @"SCORE",
        _scorePage, attrs, style);
    if (_scorePage) {
        static NSString* planeCountLabels[] = { @"1P", @"2P", @"4P", @"8P" };
        for (int index = 0; index < 4; ++index) {
            s3g::clap_gui::drawHeaderButton([self scorePlaneCountButtonRect:index],
                header, planeCountLabels[index],
                static_cast<int>(p.scorePlanes) == index, attrs, style);
        }
        [self drawScore:attrs valueAttrs:valueAttrs style:style];
        return;
    }
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:0], header, @"-", false, attrs, style);
    s3g::clap_gui::drawHeaderButton([self zoomButtonRect:1], header, @"+", false, attrs, style);
    static NSString* viewLabels[] = { @"TOP", @"SIDE", @"3/4" };
    for (int index = 0; index < 3; ++index) {
        s3g::clap_gui::drawHeaderButton([self viewButtonRect:index], header, viewLabels[index],
            _viewMode == index, attrs, style);
    }

    [s3g::clap_gui::color(0x090909) setFill];
    NSRectFill(field);
    [s3g::clap_gui::color(0x555555) setStroke];
    NSFrameRect(field);
    [NSGraphicsContext saveGraphicsState];
    [[NSBezierPath bezierPathWithRect:NSInsetRect(field, 1.0, 1.0)] addClip];
    const CGFloat radius = std::min(field.size.width, field.size.height) * 0.34 * _viewZoom;
    [s3g::clap_gui::color(0x303030) setStroke];
    NSBezierPath* sphere = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        NSMidX(field) - radius, NSMidY(field) - radius, radius * 2.0, radius * 2.0)];
    [sphere setLineWidth:0.8];
    [sphere stroke];

    std::array<NSPoint, 64> projected {};
    std::array<float, 64> activation {};
    for (uint32_t node = 0u; node < 64u; ++node) {
        projected[node] = [self projectNode:node depth:nullptr];
        activation[node] = _plugin->guiActivation[node].load(std::memory_order_relaxed);
    }

    const uint32_t pickupCount = p.pickupSet == s3g::AmbiNeuralPickupSet::Cube8 ? 8u : 4u;
    std::array<NSPoint, s3g::kAmbiNeuralEcologyPickups> pickupProjected {};
    std::array<NSPoint, s3g::kAmbiNeuralEcologyPickups> pickupAnchorProjected {};
    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
        float azimuth = _plugin->guiPickupAzimuth[pickup].load(std::memory_order_relaxed);
        float elevation = _plugin->guiPickupElevation[pickup].load(std::memory_order_relaxed);
        float anchorAzimuth = _plugin->guiPickupAnchorAzimuth[pickup].load(std::memory_order_relaxed);
        float anchorElevation = _plugin->guiPickupAnchorElevation[pickup].load(std::memory_order_relaxed);
        if (!_plugin->guiPickupDirectionsReady.load(std::memory_order_acquire)) {
            const auto fallback = s3g::ambi_neural_ecology_detail::kCube[pickup];
            azimuth = anchorAzimuth = std::atan2(fallback.y, fallback.x) * 180.0f / s3g::kPi;
            elevation = anchorElevation = std::asin(fallback.z) * 180.0f / s3g::kPi;
        }
        const auto direction = s3g::directionFromAed(azimuth, elevation);
        const auto anchor = s3g::directionFromAed(anchorAzimuth, anchorElevation);
        pickupProjected[pickup] = [self projectWorld:{
            direction.x * 1.18f, direction.y * 1.18f, direction.z * 1.18f } depth:nullptr];
        pickupAnchorProjected[pickup] = [self projectWorld:{
            anchor.x * 1.18f, anchor.y * 1.18f, anchor.z * 1.18f } depth:nullptr];
        const CGFloat displacement = std::hypot(
            pickupProjected[pickup].x - pickupAnchorProjected[pickup].x,
            pickupProjected[pickup].y - pickupAnchorProjected[pickup].y);
        if (displacement > 0.75) {
            [s3g::clap_gui::color(0xa0a0a0, 0.42) setStroke];
            NSBezierPath* trail = [NSBezierPath bezierPath];
            [trail moveToPoint:pickupAnchorProjected[pickup]];
            [trail lineToPoint:pickupProjected[pickup]];
            [trail setLineWidth:0.85];
            [trail stroke];
            [s3g::clap_gui::color(0x777777, 0.70) setStroke];
            NSFrameRect(NSMakeRect(pickupAnchorProjected[pickup].x - 1.5,
                pickupAnchorProjected[pickup].y - 1.5, 3.0, 3.0));
        }
    }
    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
        NSPoint center = NSZeroPoint;
        CGFloat weightSum = 0.0;
        for (uint32_t node = lobe * 16u; node < lobe * 16u + 16u; ++node) {
            center.x += projected[node].x * activation[node];
            center.y += projected[node].y * activation[node];
            weightSum += activation[node];
        }
        if (weightSum < 0.01) continue;
        center.x /= weightSum;
        center.y /= weightSum;
        for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
            const float auditoryWeight = _plugin->guiAuditoryWeight[
                lobe * s3g::kAmbiNeuralEcologyPickups + pickup].load(
                std::memory_order_relaxed);
            const float magnitude = std::min(1.0f, std::fabs(auditoryWeight));
            if (magnitude < 0.025f) continue;
            [s3g::clap_gui::color(auditoryWeight >= 0.0f ? 0xbcbcbc : 0x555555,
                0.06 + magnitude * 0.34) setStroke];
            NSBezierPath* path = [NSBezierPath bezierPath];
            [path moveToPoint:pickupProjected[pickup]];
            [path lineToPoint:center];
            [path setLineWidth:0.45 + magnitude * 2.1];
            [path stroke];
        }
        const float bias = _plugin->guiHomeostaticBias[lobe].load(std::memory_order_relaxed);
        const float energy = std::min(1.0f,
            _plugin->guiLobeEnergy[lobe].load(std::memory_order_relaxed) * 2.5f);
        const CGFloat haloSize = 13.0 + std::fabs(bias) * 80.0 + energy * 8.0;
        [s3g::clap_gui::color(bias >= 0.0f ? 0xb0b0b0 : 0x484848,
            0.10 + std::fabs(bias) * 1.4) setStroke];
        NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            center.x - haloSize * 0.5, center.y - haloSize * 0.5, haloSize, haloSize)];
        [halo setLineWidth:0.8];
        [halo stroke];
    }

    for (uint32_t cluster = 0u; cluster < 16u; ++cluster) {
        const uint32_t base = cluster * 4u;
        if (activation[base] < 0.002f) continue;
        NSBezierPath* ring = [NSBezierPath bezierPath];
        [ring moveToPoint:projected[base]];
        for (uint32_t local = 1u; local < 4u; ++local) [ring lineToPoint:projected[base + local]];
        [ring closePath];
        [s3g::clap_gui::color(0x787878, 0.10 + activation[base] * 0.34) setStroke];
        [ring setLineWidth:0.7];
        [ring stroke];
    }

    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
        const uint32_t base = lobe * 16u;
        if (activation[base] < 0.002f) continue;
        [s3g::clap_gui::color(0x9a9a9a, 0.12 + activation[base] * 0.20) setStroke];
        for (uint32_t cluster = 0u; cluster < 4u; ++cluster) {
            const uint32_t next = (cluster + 1u) % 4u;
            [NSBezierPath strokeLineFromPoint:projected[base + cluster * 4u]
                toPoint:projected[base + next * 4u]];
        }
    }

    for (uint32_t node = 0u; node < 64u; ++node) {
        if (activation[node] < 0.002f) continue;
        const float value = _plugin->guiValue[node].load(std::memory_order_relaxed);
        const float energy = _plugin->guiEnergy[node].load(std::memory_order_relaxed);
        const BOOL selected = node == _selectedNode;
        const CGFloat size = (selected ? 11.0 : 6.0) + std::min(9.0f, energy * 18.0f);
        const int shade = static_cast<int>(std::clamp(112.0f + value * 112.0f, 20.0f, 236.0f));
        const NSRect marker = NSMakeRect(projected[node].x - size * 0.5,
            projected[node].y - size * 0.5, size, size);
        [s3g::clap_gui::color((shade << 16) | (shade << 8) | shade,
            std::min(1.0f, activation[node] * 0.94f)) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:marker] fill];
        [s3g::clap_gui::color(selected ? 0xf0f0f0 : (value >= 0.0f ? 0xb0b0b0 : 0x555555),
            activation[node]) setStroke];
        [[NSBezierPath bezierPathWithOvalInRect:marker] stroke];
    }

    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
        const NSPoint point = pickupProjected[pickup];
        const float heard = std::min(1.0f, std::fabs(_plugin->guiPickup[pickup].load(std::memory_order_relaxed)) * 5.0f);
        const CGFloat size = 9.0 + heard * 13.0;
        NSBezierPath* ear = [NSBezierPath bezierPath];
        [ear moveToPoint:NSMakePoint(point.x, point.y - size * 0.62)];
        [ear lineToPoint:NSMakePoint(point.x + size * 0.55, point.y)];
        [ear lineToPoint:NSMakePoint(point.x, point.y + size * 0.62)];
        [ear lineToPoint:NSMakePoint(point.x - size * 0.55, point.y)];
        [ear closePath];
        [s3g::clap_gui::color(0xd8d8d8, 0.22 + heard * 0.66f) setStroke];
        [ear setLineWidth:1.0 + heard * 1.5];
        [ear stroke];
    }
    [NSGraphicsContext restoreGraphicsState];

    _selectedNode = std::min<uint32_t>(_selectedNode, 63u);
    const float value = _plugin->guiValue[_selectedNode].load(std::memory_order_relaxed);
    const float energy = _plugin->guiEnergy[_selectedNode].load(std::memory_order_relaxed);
    NSString* status = [NSString stringWithFormat:@"NODE %02u / LOBE %u / CELL %u   VALUE %+.3f   ENERGY %.3f",
        _selectedNode + 1u, _selectedNode / 16u + 1u, (_selectedNode % 16u) / 4u + 1u, value, energy];
    s3g::clap_gui::drawRightStatus(status, NSMaxX(field), field.origin.y + 7.0, valueAttrs, 8.0);
    [[NSString stringWithFormat:@"HOA FIELD  >  %u ADAPTIVE PICKUPS  >  EVOLVING AUDITORY BODY  >  LOBE RETURN",
        pickupCount]
        drawAtPoint:NSMakePoint(field.origin.x + 9.0, NSMaxY(field) - 19.0) withAttributes:valueAttrs];
}

- (void)drawSlider:(const GuiSliderSpec&)slider style:(const s3g::clap_gui::Style&)style
{
    double value = 0.0;
    paramsGetValue(&_plugin->plugin, slider.id, &value);
    const BOOL expressed = latticeExpressionGovernsParam(slider.id)
        && _plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off
        && _plugin->params.scoreAmount > 0.0001f;
    if (expressed) {
        const NSRect row = NSMakeRect(
            slider.x + 8.0, slider.y - 5.0, 224.0, 19.0);
        [s3g::clap_gui::color(0x343434, 0.78) setFill];
        NSRectFill(row);
        [s3g::clap_gui::color(0xe2e2e2, 0.82) setFill];
        NSRectFill(NSMakeRect(
            row.origin.x, row.origin.y, 2.0, row.size.height));
    }
    char display[64] {};
    paramsValueToText(nullptr, slider.id, value, display, sizeof(display));
    s3g::clap_gui::drawSlider([NSString stringWithUTF8String:slider.label],
        [NSString stringWithUTF8String:display], sliderNorm(slider, value), slider.y,
        s3g::clap_gui::softLabelAttrs(), s3g::clap_gui::softValueAttrs(), style,
        slider.x + 16.0, slider.x + 108.0, slider.x + 196.0, 82.0);
    if (expressed
        && _plugin->guiEffectiveParametersReady.load(std::memory_order_acquire)) {
        const double effective =
            _plugin->guiEffectiveParameterValues[slider.id].load(
                std::memory_order_relaxed);
        if (std::fabs(effective - value) > 1.0e-5) {
            const CGFloat markerX = slider.x + 108.0
                + static_cast<CGFloat>(
                    sliderNorm(slider, effective)) * 82.0;
            [s3g::clap_gui::color(0xffffff) setStroke];
            NSFrameRect(NSMakeRect(
                markerX - 2.0, slider.y - 1.0, 5.0, 13.0));
        }
    }
}

- (void)drawMenu:(NSString*)label value:(NSString*)value x:(CGFloat)x y:(CGFloat)y style:(const s3g::clap_gui::Style&)style
{
    s3g::clap_gui::drawMenu(label, value, y, s3g::clap_gui::softLabelAttrs(),
        s3g::clap_gui::softValueAttrs(), style, x + 16.0, x + 108.0, 124.0);
}

- (void)drawFeedbackActionButton:(NSString*)label
                            rect:(NSRect)rect
                          action:(int)action
                           attrs:(NSDictionary*)attrs
                           style:(const s3g::clap_gui::Style&)style
{
    if (![self feedbackActive:action]) {
        s3g::clap_gui::drawHeaderActionButton(rect, rect, label, attrs, style);
        return;
    }
    [s3g::clap_gui::color(_feedbackSuccess ? 0xe4e4e4 : 0x747474) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(0xffffff) setStroke];
    NSFrameRect(rect);
    NSDictionary* feedbackAttrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(_feedbackSuccess ? 0x101010 : 0xffffff), 9.0);
    const NSSize size = [label sizeWithAttributes:feedbackAttrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                                   rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:feedbackAttrs];
}

- (void)drawOrganismActionButton:(NSString*)label rect:(NSRect)rect action:(int)action
{
    const BOOL active = [self feedbackActive:action];
    [s3g::clap_gui::color(active ? (_feedbackSuccess ? 0xe4e4e4 : 0x747474) : 0x484848) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(active ? 0xffffff : 0xe0e0e0) setStroke];
    NSFrameRect(rect);
    [s3g::clap_gui::color(0x707070) setStroke];
    NSFrameRect(NSInsetRect(rect, 1.0, 1.0));
    NSDictionary* buttonAttrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(active && _feedbackSuccess ? 0x101010 : 0xf2f2f2), 9.0);
    const NSSize size = [label sizeWithAttributes:buttonAttrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                                   rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:buttonAttrs];
}

- (void)drawScoreToggleButton:(NSString*)label rect:(NSRect)rect active:(BOOL)active
{
    [s3g::clap_gui::color(active ? 0xd8d8d8 : 0x484848) setFill];
    NSRectFill(rect);
    [s3g::clap_gui::color(active ? 0xffffff : 0xa0a0a0) setStroke];
    NSFrameRect(rect);
    NSDictionary* buttonAttrs = s3g::clap_gui::textAttrs(
        s3g::clap_gui::color(active ? 0x101010 : 0xf0f0f0), 9.0);
    const NSSize size = [label sizeWithAttributes:buttonAttrs];
    [label drawAtPoint:NSMakePoint(rect.origin.x + (rect.size.width - size.width) * 0.5,
                                   rect.origin.y + (rect.size.height - size.height) * 0.5 - 0.5)
        withAttributes:buttonAttrs];
}

- (void)drawPanels:(const s3g::clap_gui::Style&)style
{
    const auto p = _plugin->params;
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();

    s3g::clap_gui::drawPanelFrame(630, 42, 250, 392, style);
    s3g::clap_gui::drawPanelHeader(@"RECURRENT CIRCUIT", true, 630, 42, 250, 21, attrs, style);
    [self drawMenu:@"NODE SET" value:[NSString stringWithUTF8String:kNodeSetNames[
        std::min<uint32_t>(static_cast<uint32_t>(p.nodeSet), 4u)]] x:630 y:78 style:style];
    for (const auto& slider : kGuiSliders) if (slider.x == 630 && slider.y < 430) [self drawSlider:slider style:style];

    s3g::clap_gui::drawPanelFrame(896, 42, 246, 392, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD LISTENING / METABOLISM", true, 896, 42, 246, 21, attrs, style);
    [self drawMenu:@"LISTENING" value:[NSString stringWithUTF8String:kListeningNames[
        std::min<uint32_t>(static_cast<uint32_t>(p.listeningMode), 3u)]] x:896 y:78 style:style];
    [self drawMenu:@"PICKUPS" value:[NSString stringWithUTF8String:kPickupSetNames[
        std::min<uint32_t>(static_cast<uint32_t>(p.pickupSet), 1u)]] x:896 y:104 style:style];
    for (const auto& slider : kGuiSliders) {
        if (slider.x == 896 && slider.y < 430) [self drawSlider:slider style:style];
    }
    [self drawMenu:@"RULE" value:[NSString stringWithUTF8String:kPlasticityNames[
        std::min<uint32_t>(static_cast<uint32_t>(p.plasticityMode), 3u)]] x:896 y:346 style:style];
    [self drawMenu:@"EVOLUTION" value:p.freeze ? @"FROZEN" : @"EVOLVING" x:896 y:370 style:style];
    const uint32_t pickupCount = p.pickupSet == s3g::AmbiNeuralPickupSet::Cube8 ? 8u : 4u;
    [@"PICKUP ENERGY" drawAtPoint:NSMakePoint(912, 394) withAttributes:attrs];
    for (uint32_t pickup = 0u; pickup < pickupCount; ++pickup) {
        const float heard = std::min(1.0f, std::fabs(_plugin->guiPickup[pickup].load(std::memory_order_relaxed)) * 5.0f);
        const NSRect meter = NSMakeRect(1004 + pickup * 15.4, 395, 12, 9);
        [s3g::clap_gui::color(0x303030) setFill]; NSRectFill(meter);
        [s3g::clap_gui::color(0xd0d0d0, 0.28 + heard * 0.72) setFill];
        NSRectFill(NSMakeRect(meter.origin.x, meter.origin.y,
            meter.size.width * heard, meter.size.height));
    }
    [@"LOBE ENERGY" drawAtPoint:NSMakePoint(912, 414) withAttributes:attrs];
    for (uint32_t lobe = 0u; lobe < 4u; ++lobe) {
        const float energy = std::min(1.0f,
            _plugin->guiLobeEnergy[lobe].load(std::memory_order_relaxed) * 2.5f);
        const NSRect meter = NSMakeRect(1004 + lobe * 31.0, 415, 25, 9);
        [s3g::clap_gui::color(0x303030) setFill]; NSRectFill(meter);
        [s3g::clap_gui::color(0xc8c8c8, 0.32 + energy * 0.68) setFill];
        NSRectFill(NSMakeRect(meter.origin.x, meter.origin.y, meter.size.width * energy, meter.size.height));
    }

    s3g::clap_gui::drawPanelFrame(630, 450, 250, 396, style);
    s3g::clap_gui::drawPanelHeader(@"AMBISONIC FIELD", true, 630, 450, 250, 21, attrs, style);
    [self drawMenu:@"ORDER" value:[NSString stringWithFormat:@"%uOA", p.order] x:630 y:480 style:style];
    for (const auto& slider : kGuiSliders) if (slider.x == 630 && slider.y >= 500) [self drawSlider:slider style:style];

    s3g::clap_gui::drawPanelFrame(896, 450, 246, 284, style);
    s3g::clap_gui::drawPanelHeader(@"LATTICE STATUS", true, 896, 450, 246, 21, attrs, style);
    const uint32_t latticeCells =
        _plugin->guiLatticePlaneCount.load(std::memory_order_relaxed)
        * s3g::kAmbiNeuralLatticeCellsPerPlane;
    const uint32_t activeResident = std::min<uint32_t>(
        _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed),
        latticeCells - 1u);
    [@"RESIDENTS" drawAtPoint:NSMakePoint(912, 486) withAttributes:attrs];
    [[NSString stringWithFormat:@"%u", latticeCells]
        drawAtPoint:NSMakePoint(1020, 486) withAttributes:s3g::clap_gui::softValueAttrs()];
    [@"BIRTHS" drawAtPoint:NSMakePoint(912, 512) withAttributes:attrs];
    [[NSString stringWithFormat:@"%u",
        _plugin->latticeBirthCount.load(std::memory_order_relaxed)]
        drawAtPoint:NSMakePoint(1020, 512) withAttributes:s3g::clap_gui::softValueAttrs()];
    [@"ACTIVE CELL" drawAtPoint:NSMakePoint(912, 538) withAttributes:attrs];
    [[NSString stringWithFormat:@"P%u:%c%u",
        activeResident / 16u + 1u,
        'A' + static_cast<int>((activeResident % 16u) / 4u),
        activeResident % 4u + 1u]
        drawAtPoint:NSMakePoint(1020, 538) withAttributes:s3g::clap_gui::softValueAttrs()];
    [@"GENERATION" drawAtPoint:NSMakePoint(912, 564) withAttributes:attrs];
    [[NSString stringWithFormat:@"%u",
        _plugin->latticeGenerations[activeResident].load(std::memory_order_relaxed)]
        drawAtPoint:NSMakePoint(1020, 564) withAttributes:s3g::clap_gui::softValueAttrs()];
    [@"OPEN SCORE TO SET GENETICS"
        drawAtPoint:NSMakePoint(912, 616) withAttributes:s3g::clap_gui::softValueAttrs()];
    [@"GROW ONCE • THEN PLAY"
        drawAtPoint:NSMakePoint(912, 642) withAttributes:s3g::clap_gui::softValueAttrs()];
    [@"MOVES CREATE LIVE OFFSPRING"
        drawAtPoint:NSMakePoint(912, 668) withAttributes:s3g::clap_gui::softValueAttrs()];

    s3g::clap_gui::drawPanelFrame(896, 750, 246, 96, style);
    s3g::clap_gui::drawPanelHeader(@"FIELD SCORE", true, 896, 750, 246, 21, attrs, style);
    const NSRect scoreHeader = NSMakeRect(896, 750, 246, 21);
    static NSString* shortModeNames[] = { @"OFF", @"FLD", @"MIDI", @"CPL" };
    for (int mode = 0; mode < 4; ++mode) {
        s3g::clap_gui::drawHeaderButton([self scoreModeButtonRect:mode], scoreHeader,
            shortModeNames[mode], static_cast<int>(p.scoreMode) == mode, attrs, style);
    }
    for (const auto& slider : kGuiSliders) {
        if (slider.x == 896 && slider.y >= 750) [self drawSlider:slider style:style];
    }
}

- (NSRect)menuRect:(int)menu
{
    switch (menu) {
    case 1: return [self presetRect];
    case 2: return [self nodeSetRect];
    case 3: return [self orderRect];
    case 4: return [self plasticityModeRect];
    case 5: return [self listeningModeRect];
    case 6: return [self pickupSetRect];
    default: return NSZeroRect;
    }
}

- (uint32_t)menuCount:(int)menu
{
    switch (menu) {
    case 1: return s3g::kAmbiNeuralEcologyFactoryPresetCount;
    case 2: return 5u;
    case 3: return 7u;
    case 4: return 4u;
    case 5: return 4u;
    case 6: return 2u;
    default: return 0u;
    }
}

- (void)openMenu:(int)menu
{
    _openMenu = menu;
    _menuItemCount = [self menuCount:menu];
    _hoverMenuItem = -1;
    const NSRect box = [self menuRect:menu];
    const CGFloat height = _menuItemCount * 21.0;
    CGFloat y = NSMaxY(box) + 2.0;
    if (y + height > kGuiHeight - 8.0) y = box.origin.y - height - 2.0;
    _openMenuRect = NSMakeRect(box.origin.x, y, box.size.width, height);
}

- (void)drawOpenMenu:(NSDictionary*)attrs style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == 0 || _menuItemCount == 0u) return;
    static NSString* nodeItems[] = { @"RING 4", @"DUAL 8", @"CELL 16", @"PAIR 32", @"FIELD 64" };
    static NSString* orderItems[] = { @"1OA", @"2OA", @"3OA", @"4OA", @"5OA", @"6OA", @"7OA" };
    static NSString* ruleItems[] = { @"REINFORCE", @"INHIBIT", @"BALANCE", @"PRUNE" };
    static NSString* listeningItems[] = { @"LOCAL", @"CROSS", @"DIFFUSE", @"ROAMING" };
    static NSString* pickupItems[] = { @"TETRA 4", @"CUBE 8" };
    static NSString* presetItems[s3g::kAmbiNeuralEcologyFactoryPresetCount];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        for (uint32_t index = 0u; index < s3g::kAmbiNeuralEcologyFactoryPresetCount; ++index) {
            presetItems[index] = [[NSString stringWithUTF8String:
                s3g::ambiNeuralEcologyFactoryPresetInfo(index).name] retain];
        }
    });
    NSString** items = presetItems;
    int selected = static_cast<int>(_plugin->presetIndex.load(std::memory_order_relaxed));
    if (_openMenu == 2) { items = nodeItems; selected = static_cast<int>(_plugin->params.nodeSet); }
    else if (_openMenu == 3) { items = orderItems; selected = static_cast<int>(_plugin->params.order) - 1; }
    else if (_openMenu == 4) { items = ruleItems; selected = static_cast<int>(_plugin->params.plasticityMode); }
    else if (_openMenu == 5) { items = listeningItems; selected = static_cast<int>(_plugin->params.listeningMode); }
    else if (_openMenu == 6) { items = pickupItems; selected = static_cast<int>(_plugin->params.pickupSet); }
    s3g::clap_gui::drawDropdownMenu(_openMenuRect, 21.0, items, _menuItemCount,
        selected, _hoverMenuItem, attrs, style);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    syncGuiParams(*_plugin);
    const auto style = s3g::clap_gui::softTextStyle();
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* attrs = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [@"s3g AMBI NEURAL ECOLOGY 64" drawAtPoint:NSMakePoint(18, 14)
        withAttributes:s3g::clap_gui::softTitleAttrs()];
    s3g::clap_gui::drawMenu(@"PRESET", [self presetDisplayName], 14, attrs, values, style, 320, 382, 210);
    [self drawFeedbackActionButton:@"SAVE" rect:[self savePresetRect]
        action:kFeedbackSave attrs:attrs style:style];
    [self drawFeedbackActionButton:@"LOAD" rect:[self loadPresetRect]
        action:kFeedbackLoad attrs:attrs style:style];
    [self drawFeedbackActionButton:@"RANDOM" rect:[self randomRect]
        action:kFeedbackRandom attrs:attrs style:style];
    s3g::clap_gui::drawRightStatus(s3g::clap_gui::peakDbText(
        _plugin->outputPeak.load(std::memory_order_relaxed)), kGuiWidth, 14, values, 18);
    [self drawField:attrs valueAttrs:values style:style];
    [self drawPanels:style];
    [self drawOpenMenu:values style:style];
}

- (int)hitNode:(NSPoint)point
{
    if (!NSPointInRect(point, [self fieldRect])) return -1;
    int best = -1;
    CGFloat bestDistance = 14.0;
    for (uint32_t node = 0u; node < 64u; ++node) {
        if (_plugin->guiActivation[node].load(std::memory_order_relaxed) < 0.05f) continue;
        const NSPoint projected = [self projectNode:node depth:nullptr];
        const CGFloat distance = std::hypot(point.x - projected.x, point.y - projected.y);
        if (distance < bestDistance) { bestDistance = distance; best = static_cast<int>(node); }
    }
    return best;
}

- (void)setParam:(clap_id)param fromPoint:(NSPoint)point
{
    const auto* slider = guiSlider(param);
    if (!slider) return;
    applyGuiParam(*_plugin, param, sliderValue(*slider, point));
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_openMenu > 0) {
        const int hit = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == 1) applyGuiParam(*_plugin, kPresetParamId, hit);
            else if (_openMenu == 2) applyGuiParam(*_plugin, kNodeSetParamId, hit);
            else if (_openMenu == 3) applyGuiParam(*_plugin, kOrderParamId, hit + 1);
            else if (_openMenu == 4) applyGuiParam(*_plugin, kPlasticityModeParamId, hit);
            else if (_openMenu == 5) applyGuiParam(*_plugin, kListeningModeParamId, hit);
            else if (_openMenu == 6) applyGuiParam(*_plugin, kPickupSetParamId, hit);
        }
        _openMenu = 0;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point, [self presetRect])) { [self openMenu:1]; return; }
    if (NSPointInRect(point, [self nodeSetRect])) { [self openMenu:2]; return; }
    if (NSPointInRect(point, [self orderRect])) { [self openMenu:3]; return; }
    if (NSPointInRect(point, [self plasticityModeRect])) { [self openMenu:4]; return; }
    if (NSPointInRect(point, [self listeningModeRect])) { [self openMenu:5]; return; }
    if (NSPointInRect(point, [self pickupSetRect])) { [self openMenu:6]; return; }
    for (int mode = 0; mode < 4; ++mode) {
        if (NSPointInRect(point, [self scoreModeButtonRect:mode])) {
            if (mode > 0) _lastScoreMode = static_cast<uint32_t>(mode);
            applyGuiParam(*_plugin, kScoreModeParamId, mode);
            return;
        }
    }
    if (NSPointInRect(point, [self freezeRect])) {
        applyGuiParam(*_plugin, kFreezeParamId, _plugin->params.freeze ? 0.0 : 1.0);
        return;
    }
    if (NSPointInRect(point, [self savePresetRect])) {
        if ([self saveCustomPreset]) [self showActionFeedback:kFeedbackSave success:YES];
        return;
    }
    if (NSPointInRect(point, [self loadPresetRect])) {
        if ([self loadCustomPreset]) [self showActionFeedback:kFeedbackLoad success:YES];
        return;
    }
    if (NSPointInRect(point, [self randomRect])) {
        randomizeSafe(*_plugin);
        [self showActionFeedback:kFeedbackRandom success:YES];
        return;
    }
    for (int index = 0; index < 2; ++index) {
        if (NSPointInRect(point, [self scorePageButtonRect:index])) {
            _scorePage = index == 1 ? YES : NO;
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_scorePage) {
        if (NSPointInRect(point, [self scoreFollowRect])) {
            _followLatticePlane = !_followLatticePlane;
            if (_followLatticePlane) {
                const uint32_t current =
                    _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed);
                _viewLatticePlane =
                    current / s3g::kAmbiNeuralLatticeCellsPerPlane;
                _selectedLatticeCell = current;
                _plugin->guiSelectedLatticeCell = current;
            }
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
        if (NSPointInRect(point, [self scorePlayRect])) {
            const uint32_t mode = std::clamp<uint32_t>(_lastScoreMode, 1u, 3u);
            applyGuiParam(*_plugin, kScoreModeParamId, mode);
            _plugin->scoreTransportRunning.store(true, std::memory_order_release);
            _followLatticePlane = YES;
            [self showActionFeedback:kFeedbackScorePlay success:YES];
            return;
        }
        if (NSPointInRect(point, [self scoreStopRect])) {
            if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off) {
                _lastScoreMode = static_cast<uint32_t>(_plugin->params.scoreMode);
            } else {
                applyGuiParam(*_plugin, kScoreModeParamId,
                    std::clamp<uint32_t>(_lastScoreMode, 1u, 3u));
            }
            _plugin->scoreTransportRunning.store(false, std::memory_order_release);
            if (_plugin->host && _plugin->host->request_process) {
                _plugin->host->request_process(_plugin->host);
            }
            [self showActionFeedback:kFeedbackScoreStop success:YES];
            return;
        }
        for (int index = 0; index < 4; ++index) {
            if (NSPointInRect(point, [self scorePlaneCountButtonRect:index])) {
                const uint32_t planes = 1u << index;
                if (s3g::ambiNeuralScorePlaneCount(_plugin->params.scorePlanes) != planes) {
                    applyGuiParam(*_plugin, kScorePlanesParamId, index);
                    _viewLatticePlane = std::min<uint32_t>(_viewLatticePlane, planes - 1u);
                    const uint32_t local =
                        _selectedLatticeCell % s3g::kAmbiNeuralLatticeCellsPerPlane;
                    _selectedLatticeCell =
                        _viewLatticePlane * s3g::kAmbiNeuralLatticeCellsPerPlane + local;
                    [self storeViewState];
                }
                return;
            }
        }
        const uint32_t planeCount = std::clamp<uint32_t>(
            _plugin->guiLatticePlaneCount.load(std::memory_order_relaxed), 1u,
            s3g::kAmbiNeuralLatticeMaxPlanes);
        for (uint32_t plane = 0u; plane < planeCount; ++plane) {
            if (NSPointInRect(point, [self scorePlaneTabRect:plane])) {
                const uint32_t local =
                    _selectedLatticeCell % s3g::kAmbiNeuralLatticeCellsPerPlane;
                _viewLatticePlane = plane;
                _followLatticePlane = NO;
                _selectedLatticeCell =
                    plane * s3g::kAmbiNeuralLatticeCellsPerPlane + local;
                _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
                if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off
                    && !_plugin->scoreTransportRunning.load(std::memory_order_acquire)) {
                    requestLatticeRecall(*_plugin, _selectedLatticeCell);
                }
                [self storeViewState];
                [self setNeedsDisplay:YES];
                return;
            }
        }
        for (const auto& slider : kGuiSliders) {
            if ((slider.id == kScoreVariationParamId
                    || slider.id == kScoreRecombineParamId
                    || slider.id == kScoreMemoryParamId)
                && NSPointInRect(point,
                    NSMakeRect(slider.x + 8.0, slider.y - 8.0, 230.0, 24.0))) {
                _dragParam = static_cast<int>(slider.id);
                [self setParam:slider.id fromPoint:point];
                return;
            }
        }
        if (NSPointInRect(point, [self scoreGoRect])) {
            if (_plugin->params.scoreMode == s3g::AmbiNeuralScoreMode::Off) {
                applyGuiParam(*_plugin, kScoreModeParamId,
                    std::clamp<uint32_t>(_lastScoreMode, 1u, 3u));
            }
            _plugin->scoreTransportRunning.store(true, std::memory_order_release);
            requestLatticeCell(*_plugin, _selectedLatticeCell, 1.0f);
            _followLatticePlane = YES;
            [self showActionFeedback:kFeedbackScoreGo success:YES];
            return;
        }
        if (NSPointInRect(point, [self scoreGrowRect])) {
            growCurrentLattice(*_plugin);
            _selectedLatticeCell =
                _plugin->guiLatticeCurrentCell.load(std::memory_order_relaxed);
            _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
            _viewLatticePlane =
                _selectedLatticeCell / s3g::kAmbiNeuralLatticeCellsPerPlane;
            [self showActionFeedback:kFeedbackGrowLattice success:YES];
            return;
        }
        for (uint32_t localCell = 0u;
            localCell < s3g::kAmbiNeuralLatticeCellsPerPlane; ++localCell) {
            if (NSPointInRect(point, [self scoreCellRect:localCell])) {
                _followLatticePlane = NO;
                _selectedLatticeCell =
                    _viewLatticePlane * s3g::kAmbiNeuralLatticeCellsPerPlane + localCell;
                _plugin->guiSelectedLatticeCell = _selectedLatticeCell;
                if (_plugin->params.scoreMode != s3g::AmbiNeuralScoreMode::Off
                    && !_plugin->scoreTransportRunning.load(std::memory_order_acquire)) {
                    requestLatticeRecall(*_plugin, _selectedLatticeCell);
                }
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (NSPointInRect(point, [self fieldRect])) return;
    }
    for (int index = 0; index < 2; ++index) {
        if (NSPointInRect(point, [self zoomButtonRect:index])) {
            _viewZoom = std::clamp(_viewZoom + (index == 0 ? -0.15 : 0.15), 0.55, 2.20);
            [self storeViewState];
            return;
        }
    }
    for (int index = 0; index < 3; ++index) {
        if (NSPointInRect(point, [self viewButtonRect:index])) { [self setViewPreset:index]; return; }
    }
    const int node = [self hitNode:point];
    if (node >= 0) { _selectedNode = static_cast<uint32_t>(node); return; }
    if (NSPointInRect(point, [self fieldRect])) {
        _dragView = YES;
        _lastDragPoint = point;
        _viewMode = -1;
        return;
    }
    _dragParam = 0;
    for (const auto& slider : kGuiSliders) {
        if (NSPointInRect(point, NSMakeRect(slider.x + 8.0, slider.y - 8.0, 230.0, 24.0))) {
            _dragParam = static_cast<int>(slider.id);
            [self setParam:slider.id fromPoint:point];
            return;
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
        _viewElDeg = std::clamp(_viewElDeg + (point.y - _lastDragPoint.y) * 0.35, -85.0, 85.0);
        _lastDragPoint = point;
        [self storeViewState];
    } else if (_dragParam) {
        [self setParam:static_cast<clap_id>(_dragParam) fromPoint:point];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    _dragParam = 0;
    _dragView = NO;
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu <= 0) return;
    const NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    _hoverMenuItem = s3g::clap_gui::dropdownHitIndex(point, _openMenuRect, 21.0, _menuItemCount);
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
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
    p->guiView = [[S3GAmbiNeuralEcologyView alloc] initWithPlugin:p];
    if (!p->guiView) return false;
    if (!s3g::clap_gui::createResponsiveViewport(p->guiViewport,
            static_cast<NSView*>(p->guiView), kGuiWidth, kGuiHeight)) {
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
    [static_cast<S3GAmbiNeuralEcologyView*>(p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(p->guiViewport, p->guiView);
    p->guiVisible = false;
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
    return s3g::clap_gui::setResponsiveViewportSize(self(plugin)->guiViewport, width, height);
}
bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || std::strcmp(window->api, CLAP_WINDOW_API_COCOA) != 0 || !window->cocoa) return false;
    auto* p = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(p->guiViewport,
        static_cast<NSView*>(window->cocoa), p->host);
}
bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, false)) return false;
    p->guiVisible = true;
    [static_cast<S3GAmbiNeuralEcologyView*>(p->guiView) startRefreshTimer];
    return true;
}
bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible = false;
    [static_cast<S3GAmbiNeuralEcologyView*>(p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(p->guiViewport, true);
}

const clap_plugin_gui_t guiExt {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize,
    guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide
};

#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
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

constexpr const char* features[] {
    CLAP_PLUGIN_FEATURE_INSTRUMENT,
    CLAP_PLUGIN_FEATURE_SYNTHESIZER,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-neural-ecology-64",
    "s3g Ambi Neural Ecology 64",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.8.0",
    "A recurrent Ambisonic ecology that breeds resident genomes through an ingress/egress lattice volume.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    p->params = s3g::ambiNeuralEcologyFactoryPreset(0u);
    p->audioParams = p->params;
    const bool scoreRunning =
        p->params.scoreMode != s3g::AmbiNeuralScoreMode::Off;
    p->scoreTransportRunning.store(scoreRunning, std::memory_order_relaxed);
    p->audioScoreTransportRunning = scoreRunning;
    storeParamBank(*p, p->params, 0u);
    p->engine.prepare(p->sampleRate);
    p->engine.setParams(p->audioParams);
    p->engine.reset();
    const auto founder = p->engine.genomeValues();
    const auto founderExpression =
        s3g::ambiNeuralLatticeExpressionFromParams(p->params);
    p->audioExpressionCurrent = founderExpression;
    p->audioExpressionFrom = founderExpression;
    p->audioExpressionTarget = founderExpression;
    for (uint32_t index = 0u; index < founder.size(); ++index) {
        p->guiGenome[index].store(founder[index], std::memory_order_relaxed);
    }
    requestLatticeStorage(*p, s3g::growAmbiNeuralLattice(
        founder, founderExpression, p->randomSeed,
        s3g::ambiNeuralScorePlaneCount(p->params.scorePlanes),
        p->params.scoreVariation, p->params.scoreRecombine), false);
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
const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0u ? &descriptor : nullptr;
}
const clap_plugin_t* factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    return pluginId && std::strcmp(pluginId, descriptor.id) == 0 ? create(host) : nullptr;
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount, factoryGetPluginDescriptor, factoryCreatePlugin
};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}

} // namespace

extern "C" const clap_plugin_entry_t clap_entry {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
