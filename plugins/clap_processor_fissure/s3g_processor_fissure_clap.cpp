#include "s3g_processor_fissure.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_gui_layout.h"
#endif

namespace {

constexpr uint32_t kOutputChannels = 8u;
constexpr uint32_t kStateMagic = 0x53494653u; // "SFIS"
constexpr uint32_t kStateVersion = 5u;
constexpr uint32_t kPreviousStateVersion = 4u;
constexpr uint32_t kGuiWidth = 1356u;
constexpr uint32_t kGuiHeight = 770u;

constexpr clap_id kPressureParamId = 1u;
constexpr clap_id kMassParamId = 2u;
constexpr clap_id kEdgeParamId = 3u;
constexpr clap_id kVoidParamId = 4u;
constexpr clap_id kMemoryParamId = 5u;
constexpr clap_id kBodyParamId = 6u;
constexpr clap_id kVoiceParamId = 7u;
constexpr clap_id kMotionParamId = 8u;
constexpr clap_id kInputParamId = 9u;
constexpr clap_id kOutputParamId = 10u;
constexpr clap_id kSeedParamId = 11u;
constexpr clap_id kHoldParamId = 12u;
constexpr clap_id kSceneParamId = 13u;
constexpr clap_id kCaptureParamId = 14u;
constexpr clap_id kCutParamId = 15u;
constexpr clap_id kBreachParamId = 16u;
constexpr clap_id kWithdrawParamId = 17u;
constexpr clap_id kNewParamId = 18u;
constexpr clap_id kPanicParamId = 19u;
constexpr clap_id kRunParamId = 20u;
constexpr clap_id kOutputModeParamId = 21u;
constexpr clap_id kSelectedCellParamId = 22u;
constexpr clap_id kStrikeParamId = 23u;
constexpr clap_id kContactParamId = 24u;
constexpr clap_id kShakerParamId = 25u;
constexpr clap_id kRattleParamId = 26u;
constexpr clap_id kSpringParamId = 27u;
constexpr clap_id kSceneMorphParamId = 28u;
constexpr clap_id kTopologyShapeParamId = 29u;
constexpr clap_id kTopologyMixParamId = 30u;
constexpr clap_id kApplyTopologyParamId = 31u;
constexpr clap_id kVariationParamId = 32u;
constexpr clap_id kPresetParamId = 33u;
constexpr clap_id kCutMaskParamBase = 34u;
constexpr clap_id kFractureDistanceParamId = 42u;
constexpr clap_id kFractureForceParamId = 43u;
constexpr clap_id kGrabParamId = 44u;
constexpr clap_id kRepeatParamId = 45u;
constexpr clap_id kMatrixParamBase = 100u;
constexpr clap_id kCellLevelParamBase = 200u;
constexpr clap_id kObjectSizeParamBase = 300u;
constexpr clap_id kObjectDecayParamBase = 320u;
constexpr clap_id kObjectHardnessParamBase = 340u;
constexpr clap_id kObjectSensitivityParamBase = 360u;
constexpr clap_id kObjectDriveParamBase = 380u;
constexpr uint32_t kFactoryPresetCount = 12u;
constexpr uint32_t kCustomPresetIndex = kFactoryPresetCount;
constexpr uint32_t kBaseParamCount = 45u;
constexpr uint32_t kMatrixParamCount = 64u;
constexpr uint32_t kCellLevelParamCount = 8u;
constexpr uint32_t kObjectBankCount = 5u;
constexpr uint32_t kObjectParamCount = kObjectBankCount * 8u;
constexpr uint32_t kParamCount = kBaseParamCount
    + kMatrixParamCount + kCellLevelParamCount + kObjectParamCount;
constexpr uint32_t kPersistentParamCount = 144u;
constexpr uint32_t kPreviousPersistentParamCount = 136u;
constexpr uint32_t kSceneValueCount = 124u;

enum class OutputMode : uint32_t {
    Stereo = 0u,
    Quad = 1u,
    Direct8 = 2u,
};

struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
    bool momentary;
};

constexpr std::array<ParamDef, kBaseParamCount> kBaseParamDefs {{
    { kPressureParamId, "Pressure", "Material", 0.0, 1.0, 0.62, false, false },
    { kMassParamId, "Mass", "Material", 0.0, 1.0, 0.78, false, false },
    { kEdgeParamId, "Edge", "Form", 0.0, 1.0, 0.46, false, false },
    { kVoidParamId, "Void", "Form", 0.0, 1.0, 0.18, false, false },
    { kMemoryParamId, "Memory", "Material", 0.0, 1.0, 0.48, false, false },
    { kBodyParamId, "Body", "Material", 0.0, 1.0, 0.56, false, false },
    { kVoiceParamId, "Input Coupling", "Physical Exciter", 0.0, 1.0, 0.50, false, false },
    { kMotionParamId, "Motion", "Form", 0.0, 1.0, 0.32, false, false },
    { kInputParamId, "Input Gain", "I/O", -60.0, 24.0, 0.0, false, false },
    { kOutputParamId, "Output Gain", "I/O", -60.0, 6.0, -12.0, false, false },
    { kSeedParamId, "Seed", "Performance", 1.0, 16777215.0, 1979.0, true, false },
    { kHoldParamId, "Hold", "Performance", 0.0, 1.0, 0.0, true, false },
    { kSceneParamId, "Scene", "Scenes", 1.0, 4.0, 1.0, true, false },
    { kCaptureParamId, "Store Scene", "Scenes", 0.0, 1.0, 0.0, true, true },
    { kCutParamId, "Cut + Links", "Gestures", 0.0, 1.0, 0.0, true, true },
    { kBreachParamId, "Impact All", "Gestures", 0.0, 1.0, 0.0, true, true },
    { kWithdrawParamId, "Drop Out", "Gestures", 0.0, 1.0, 0.0, true, true },
    { kNewParamId, "Mutate Object", "Gestures", 0.0, 1.0, 0.0, true, true },
    { kPanicParamId, "Panic", "Actions", 0.0, 1.0, 0.0, true, true },
    { kRunParamId, "Run", "Performance", 0.0, 1.0, 1.0, true, false },
    { kOutputModeParamId, "Output Mode", "I/O", 0.0, 2.0, 2.0, true, false },
    { kSelectedCellParamId, "Selected Cell", "Cells", 1.0, 8.0, 1.0, true, false },
    { kStrikeParamId, "Strike Cell", "Cells", 0.0, 1.0, 0.0, true, true },
    { kContactParamId, "Contact", "Physical Exciter", 0.0, 1.0, 0.48, false, false },
    { kShakerParamId, "Shaker", "Physical Exciter", 0.0, 1.0, 0.26, false, false },
    { kRattleParamId, "Rattle", "Physical Exciter", 0.0, 1.0, 0.42, false, false },
    { kSpringParamId, "Spring", "Physical Exciter", 0.0, 1.0, 0.34, false, false },
    { kSceneMorphParamId, "Scene Morph", "Scenes", 0.0, 3.0, 0.0, false, false },
    { kTopologyShapeParamId, "Topology Shape", "Topology", 0.0, 5.0, 1.0, true, false },
    { kTopologyMixParamId, "Topology Mix", "Topology", 0.0, 1.0, 1.0, false, false },
    { kApplyTopologyParamId, "Apply Topology", "Topology", 0.0, 1.0, 0.0, true, true },
    { kVariationParamId, "Cut Contrast", "Gestures", 0.0, 1.0, 0.46, false, false },
    { kPresetParamId, "Instrument Preset", "Preset", 0.0,
        static_cast<double>(kCustomPresetIndex), 0.0, true, false },
    { kCutMaskParamBase + 0u, "Cell 1 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kCutMaskParamBase + 1u, "Cell 2 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kCutMaskParamBase + 2u, "Cell 3 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kCutMaskParamBase + 3u, "Cell 4 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kCutMaskParamBase + 4u, "Cell 5 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kCutMaskParamBase + 5u, "Cell 6 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kCutMaskParamBase + 6u, "Cell 7 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kCutMaskParamBase + 7u, "Cell 8 Cut Enabled", "Cut Mask", 0.0, 1.0, 1.0, true, false },
    { kFractureDistanceParamId, "Fracture Distance", "Fracture Pad", 0.0, 1.0, 0.0, false, false },
    { kFractureForceParamId, "Fracture Force", "Fracture Pad", 0.0, 1.0, 0.0, false, false },
    { kGrabParamId, "Grab Performance", "Fracture Pad", 0.0, 1.0, 0.0, true, false },
    { kRepeatParamId, "Repeat Ecology", "Fracture Pad", 0.0, 1.0, 0.0, true, false },
}};

constexpr std::array<clap_id, kObjectBankCount> kObjectParamBases {{
    kObjectSizeParamBase,
    kObjectDecayParamBase,
    kObjectHardnessParamBase,
    kObjectSensitivityParamBase,
    kObjectDriveParamBase,
}};

s3g::ProcessorFissureMatrix initialMatrix()
{
    s3g::ProcessorFissureMatrix matrix {};
    for (uint32_t destination = 0u;
         destination < s3g::kProcessorFissureCells; ++destination) {
        matrix[destination * 8u + destination] = 0.42f;
        matrix[destination * 8u + ((destination + 1u) % 8u)] = 0.16f;
        matrix[destination * 8u + ((destination + 7u) % 8u)] = -0.11f;
        matrix[destination * 8u + ((destination + 4u) % 8u)]
            = (destination & 1u) == 0u ? 0.06f : -0.06f;
    }
    return matrix;
}

bool isMatrixParam(clap_id id)
{
    return id >= kMatrixParamBase
        && id < kMatrixParamBase + kMatrixParamCount;
}

bool isCutMaskParam(clap_id id)
{
    return id >= kCutMaskParamBase
        && id < kCutMaskParamBase + 8u;
}

uint32_t cutMaskIndex(clap_id id) { return id - kCutMaskParamBase; }

bool isCellLevelParam(clap_id id)
{
    return id >= kCellLevelParamBase
        && id < kCellLevelParamBase + kCellLevelParamCount;
}

int32_t objectBankForParam(clap_id id)
{
    for (uint32_t bank = 0u; bank < kObjectParamBases.size(); ++bank) {
        if (id >= kObjectParamBases[bank]
            && id < kObjectParamBases[bank] + 8u) {
            return static_cast<int32_t>(bank);
        }
    }
    return -1;
}

bool isObjectParam(clap_id id) { return objectBankForParam(id) >= 0; }

uint32_t objectCellIndex(clap_id id)
{
    const int32_t bank = objectBankForParam(id);
    return bank >= 0 ? id - kObjectParamBases[static_cast<uint32_t>(bank)]
                     : 8u;
}

uint32_t matrixIndex(clap_id id) { return id - kMatrixParamBase; }
uint32_t cellIndex(clap_id id) { return id - kCellLevelParamBase; }

const ParamDef* baseParamDef(clap_id id)
{
    return id >= 1u && id <= kBaseParamCount
        ? &kBaseParamDefs[id - 1u] : nullptr;
}

bool validParamId(clap_id id)
{
    return baseParamDef(id) || isMatrixParam(id) || isCellLevelParam(id)
        || isObjectParam(id);
}

bool momentaryParam(clap_id id)
{
    const auto* def = baseParamDef(id);
    return def && def->momentary;
}

double clampParamValue(clap_id id, double value)
{
    if (!std::isfinite(value)) value = 0.0;
    if (const auto* def = baseParamDef(id)) {
        value = std::clamp(value, def->minimum, def->maximum);
        return def->stepped ? std::round(value) : value;
    }
    if (isMatrixParam(id)) return std::clamp(value, -1.0, 1.0);
    if (isCellLevelParam(id) || isObjectParam(id)) {
        return std::clamp(value, 0.0, 1.0);
    }
    return 0.0;
}

uint32_t paramOrdinal(clap_id id)
{
    if (id >= 1u && id <= kBaseParamCount) return id - 1u;
    if (isMatrixParam(id)) return kBaseParamCount + matrixIndex(id);
    if (isCellLevelParam(id)) {
        return kBaseParamCount + kMatrixParamCount + cellIndex(id);
    }
    const int32_t bank = objectBankForParam(id);
    if (bank >= 0) {
        return kBaseParamCount + kMatrixParamCount + kCellLevelParamCount
            + static_cast<uint32_t>(bank) * 8u + objectCellIndex(id);
    }
    return kParamCount;
}

clap_id paramIdAt(uint32_t index)
{
    if (index < kBaseParamCount) return index + 1u;
    index -= kBaseParamCount;
    if (index < kMatrixParamCount) return kMatrixParamBase + index;
    index -= kMatrixParamCount;
    if (index < kCellLevelParamCount) return kCellLevelParamBase + index;
    index -= kCellLevelParamCount;
    if (index < kObjectParamCount) {
        return kObjectParamBases[index / 8u] + index % 8u;
    }
    return CLAP_INVALID_ID;
}

clap_id persistentParamIdAt(uint32_t index)
{
    if (index < 13u) return index + 1u;
    if (index == 13u) return kRunParamId;
    if (index == 14u) return kOutputModeParamId;
    if (index == 15u) return kSelectedCellParamId;
    if (index >= 16u && index < 23u) {
        return kContactParamId + index - 16u;
    }
    if (index == 23u) return kVariationParamId;
    if (index >= 24u && index < 32u) {
        return kCutMaskParamBase + index - 24u;
    }
    index -= 32u;
    if (index < kMatrixParamCount) return kMatrixParamBase + index;
    index -= kMatrixParamCount;
    if (index < kCellLevelParamCount) return kCellLevelParamBase + index;
    index -= kCellLevelParamCount;
    if (index < kObjectParamCount) {
        return kObjectParamBases[index / 8u] + index % 8u;
    }
    return CLAP_INVALID_ID;
}

clap_id previousPersistentParamIdAt(uint32_t index)
{
    if (index < 13u) return index + 1u;
    if (index == 13u) return kRunParamId;
    if (index == 14u) return kOutputModeParamId;
    if (index == 15u) return kSelectedCellParamId;
    if (index >= 16u && index < 23u) {
        return kContactParamId + index - 16u;
    }
    if (index == 23u) return kVariationParamId;
    index -= 24u;
    if (index < kMatrixParamCount) return kMatrixParamBase + index;
    index -= kMatrixParamCount;
    if (index < kCellLevelParamCount) return kCellLevelParamBase + index;
    index -= kCellLevelParamCount;
    if (index < kObjectParamCount) {
        return kObjectParamBases[index / 8u] + index % 8u;
    }
    return CLAP_INVALID_ID;
}

uint32_t renderChannels(OutputMode mode)
{
    switch (mode) {
    case OutputMode::Stereo: return 2u;
    case OutputMode::Quad: return 4u;
    case OutputMode::Direct8: return 8u;
    }
    return 8u;
}

struct SceneSnapshot {
    std::array<float, 12u> macros {};
    s3g::ProcessorFissureMatrix matrix {};
    std::array<s3g::ProcessorFissureObject, 8u> objects {};
};

struct SavedStateHeader {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    uint32_t liveValueCount = kPersistentParamCount;
    uint32_t sceneValueCount = kSceneValueCount * 4u;
};

struct SavedStatePayload {
    std::array<double, kPersistentParamCount> live {};
    std::array<double, kSceneValueCount * 4u> scenes {};
};

struct PreviousSavedStatePayload {
    std::array<double, kPreviousPersistentParamCount> live {};
    std::array<double, kSceneValueCount * 4u> scenes {};
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    s3g::ProcessorFissureParams params {};
    s3g::ProcessorFissureMatrix matrix = initialMatrix();
    std::array<float, 8u> cellLevels {{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    }};
    std::array<s3g::ProcessorFissureObject, 8u> objects {};
    std::array<SceneSnapshot, 4u> scenes {};
    OutputMode outputMode = OutputMode::Direct8;
    uint32_t selectedCell = 0u;
    uint32_t selectedScene = 0u;
    uint32_t topologyShape = 1u;
    float topologyMix = 1.0f;
    float sceneMorph = 0.0f;
    float variation = 0.46f;
    std::array<bool, 8u> cutMask {{
        true, true, true, true, true, true, true, true,
    }};
    float fractureDistance = 0.0f;
    float fractureForce = 0.0f;
    bool grabbing = false;
    bool repeat = false;
    uint32_t variationSerial = 0u;
    uint32_t presetIndex = 0u;
    s3g::ProcessorFissure engine {};
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::atomic<uint32_t> pendingActions { 0u };
    std::atomic<bool> pendingRescan { false };
    std::array<bool, 8u> actionGates {};
    std::array<std::atomic<float>, 8u> activity {};
    std::array<std::atomic<float>, 8u> cutActivity {};
    std::array<std::atomic<float>, 8u> cutPolarity {};
    std::array<std::atomic<float>, 8u> cutFragmentAge {};
    std::atomic<bool> grabbingStatus { false };
    std::atomic<bool> grabbed { false };
    std::atomic<float> grabDuration { 0.0f };
    std::atomic<float> repeatPhase { 0.0f };
    std::atomic<float> repeatMix { 0.0f };
    std::atomic<float> minimumGovernor { 1.0f };
    std::atomic<float> inputActivity { 0.0f };
    std::atomic<float> inputPeakActivity { 0.0f };
    std::atomic<float> contactActivity { 0.0f };
    std::atomic<float> inputTransferActivity { 0.0f };
    std::atomic<float> detectedPitchHz { 0.0f };
    std::atomic<float> pitchConfidence { 0.0f };
    std::atomic<float> sustainedPitchDrive { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::array<float, 2u> frameInput {};
    std::array<float, 8u> frameOutput {};
    double sampleRate = 48000.0;
    bool prepared = false;
#if defined(__APPLE__)
    void* guiView = nullptr;
    bool guiVisible = false;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

void publishParam(Plugin& p, clap_id id, double value)
{
    const uint32_t ordinal = paramOrdinal(id);
    if (ordinal >= p.publishedParams.size()) return;
    p.publishedParams[ordinal].store(value, std::memory_order_release);
}

double paramValue(const Plugin& p, clap_id id)
{
    const uint32_t ordinal = paramOrdinal(id);
    return ordinal < p.publishedParams.size()
        ? p.publishedParams[ordinal].load(std::memory_order_acquire) : 0.0;
}

double rawParamValue(const Plugin& p, clap_id id)
{
    if (isCutMaskParam(id)) return p.cutMask[cutMaskIndex(id)] ? 1.0 : 0.0;
    if (isMatrixParam(id)) return p.matrix[matrixIndex(id)];
    if (isCellLevelParam(id)) return p.cellLevels[cellIndex(id)];
    if (isObjectParam(id)) {
        const auto& object = p.objects[objectCellIndex(id)];
        switch (static_cast<uint32_t>(objectBankForParam(id))) {
        case 0u: return object.size;
        case 1u: return object.decay;
        case 2u: return object.hardness;
        case 3u: return object.sensitivity;
        default: return object.drive;
        }
    }
    switch (id) {
    case kPressureParamId: return p.params.pressure;
    case kMassParamId: return p.params.mass;
    case kEdgeParamId: return p.params.edge;
    case kVoidParamId: return p.params.voidAmount;
    case kMemoryParamId: return p.params.memory;
    case kBodyParamId: return p.params.body;
    case kVoiceParamId: return p.params.voice;
    case kMotionParamId: return p.params.motion;
    case kInputParamId: return p.params.inputGainDb;
    case kOutputParamId: return p.params.outputGainDb;
    case kSeedParamId: return p.params.seed;
    case kHoldParamId: return p.params.hold ? 1.0 : 0.0;
    case kRunParamId: return p.params.run ? 1.0 : 0.0;
    case kOutputModeParamId: return static_cast<uint32_t>(p.outputMode);
    case kSelectedCellParamId: return p.selectedCell + 1u;
    case kContactParamId: return p.params.contact;
    case kShakerParamId: return p.params.shaker;
    case kRattleParamId: return p.params.rattle;
    case kSpringParamId: return p.params.spring;
    case kSceneMorphParamId: return p.sceneMorph;
    case kTopologyShapeParamId: return p.topologyShape;
    case kTopologyMixParamId: return p.topologyMix;
    case kVariationParamId: return p.variation;
    case kPresetParamId: return p.presetIndex;
    case kFractureDistanceParamId: return p.fractureDistance;
    case kFractureForceParamId: return p.fractureForce;
    case kGrabParamId: return p.grabbing ? 1.0 : 0.0;
    case kRepeatParamId: return p.repeat ? 1.0 : 0.0;
    default: return 0.0;
    }
}

SceneSnapshot snapshotFromPlugin(const Plugin& p)
{
    SceneSnapshot scene;
    scene.macros = {{
        p.params.pressure, p.params.mass, p.params.edge,
        p.params.voidAmount, p.params.memory, p.params.body,
        p.params.voice, p.params.motion,
        p.params.contact, p.params.shaker,
        p.params.rattle, p.params.spring,
    }};
    scene.matrix = p.matrix;
    scene.objects = p.objects;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        scene.objects[cell].level = p.cellLevels[cell];
    }
    return scene;
}

uint32_t nextHash(uint32_t& state)
{
    uint32_t value = state ? state : 1u;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    state = value ? value : 1u;
    return state;
}

float hashUnit(uint32_t& state)
{
    return static_cast<float>(nextHash(state) & 0x00ffffffu)
        / static_cast<float>(0x01000000u);
}

s3g::ProcessorFissureMatrix topologyMatrix(uint32_t shape,
    uint32_t hubCell, uint32_t seed)
{
    s3g::ProcessorFissureMatrix matrix {};
    shape = std::min<uint32_t>(shape, 5u);
    hubCell %= 8u;
    uint32_t random = seed ? seed : 1u;
    for (uint32_t destination = 0u; destination < 8u; ++destination) {
        for (uint32_t source = 0u; source < 8u; ++source) {
            float route = 0.0f;
            switch (shape) {
            case 0u: // islands
                route = source == destination ? 0.52f : 0.0f;
                break;
            case 1u: // ring
                if (source == destination) route = 0.30f;
                else if (source == (destination + 7u) % 8u) route = 0.38f;
                else if (source == (destination + 1u) % 8u) route = -0.14f;
                break;
            case 2u: // pairs
                if (source == destination) route = 0.26f;
                else if (source == (destination ^ 1u)) route = 0.56f;
                break;
            case 3u: // hub
                if (source == destination) route = 0.18f;
                if (source == hubCell && destination != hubCell) route = 0.36f;
                if (destination == hubCell && source != hubCell) {
                    route = (source & 1u) == 0u ? 0.24f : -0.24f;
                }
                break;
            case 4u: { // two clusters
                const bool sameCluster = destination / 4u == source / 4u;
                if (source == destination) route = 0.28f;
                else if (sameCluster) {
                    const uint32_t distance = source > destination
                        ? source - destination : destination - source;
                    route = distance == 1u ? 0.27f : -0.10f;
                } else if ((destination + source) % 7u == 0u) {
                    route = 0.08f;
                }
                break;
            }
            default: { // scatter
                const float chance = hashUnit(random);
                const float sign = hashUnit(random) < 0.44f ? -1.0f : 1.0f;
                const float amount = 0.06f + hashUnit(random) * 0.42f;
                if (source == destination) route = 0.16f + amount * 0.54f;
                else if (chance < 0.23f) route = sign * amount;
                break;
            }
            }
            matrix[destination * 8u + source] = route;
        }
    }
    return matrix;
}

struct FactoryPresetSpec {
    const char* name;
    std::array<float, 12u> macros;
    uint32_t topology;
    float topologyMix;
    OutputMode outputMode;
    float inputGainDb;
    float outputGainDb;
    uint32_t seed;
    float variation;
    std::array<float, 5u> objectCenter;
};

constexpr std::array<FactoryPresetSpec, kFactoryPresetCount>
    kFactoryPresets {{
        { "INIT / PLATE RING",
            {{ .62f, .78f, .46f, .18f, .48f, .56f,
               .50f, .32f, .48f, .26f, .42f, .34f }},
            1u, 1.00f, OutputMode::Direct8, 0.0f, -10.0f, 1979u, .46f,
            {{ .50f, .58f, .52f, .66f, .48f }} },
        { "SHAKER BOX",
            {{ .48f, .54f, .76f, .24f, .28f, .34f,
               .18f, .68f, .34f, .91f, .94f, .22f }},
            2u, .92f, OutputMode::Stereo, 4.0f, -10.0f, 4307u, .74f,
            {{ .25f, .31f, .84f, .88f, .69f }} },
        { "SPRING NEST",
            {{ .48f, .63f, .57f, .11f, .82f, .72f,
               .36f, .44f, .79f, .28f, .37f, .98f }},
            3u, .78f, OutputMode::Quad, 2.0f, -11.0f, 991u, .53f,
            {{ .63f, .86f, .39f, .82f, .58f }} },
        { "CONTACT SHEET",
            {{ .62f, .81f, .72f, .08f, .69f, .91f,
               .74f, .21f, .96f, .10f, .16f, .51f }},
            0u, 1.00f, OutputMode::Direct8, 8.0f, -11.0f, 2719u, .32f,
            {{ .82f, .73f, .71f, .96f, .54f }} },
        { "TIN PAIRS",
            {{ .69f, .46f, .88f, .36f, .31f, .43f,
               .20f, .67f, .40f, .72f, .86f, .28f }},
            2u, 1.00f, OutputMode::Quad, 1.0f, -10.0f, 6607u, .81f,
            {{ .34f, .38f, .92f, .73f, .76f }} },
        { "BROKEN HUB",
            {{ .84f, .88f, .64f, .47f, .72f, .65f,
               .28f, .35f, .52f, .38f, .56f, .62f }},
            3u, .96f, OutputMode::Direct8, 0.0f, -12.0f, 8123u, .68f,
            {{ .57f, .66f, .62f, .59f, .83f }} },
        { "RUST CLUSTERS",
            {{ .71f, .73f, .81f, .31f, .86f, .58f,
               .13f, .49f, .31f, .58f, .78f, .71f }},
            4u, .88f, OutputMode::Quad, -2.0f, -11.0f, 12011u, .72f,
            {{ .48f, .75f, .78f, .68f, .71f }} },
        { "EMPTY ISLANDS",
            {{ .36f, .45f, .34f, .62f, .91f, .77f,
               .62f, .16f, .72f, .16f, .15f, .43f }},
            0u, 1.00f, OutputMode::Stereo, 6.0f, -8.0f, 15217u, .24f,
            {{ .72f, .93f, .27f, .91f, .33f }} },
        { "SCRAP CASCADE",
            {{ .77f, .59f, .93f, .68f, .72f, .39f,
               .21f, .92f, .27f, .76f, .92f, .84f }},
            5u, .94f, OutputMode::Direct8, 0.0f, -12.0f, 18433u, .93f,
            {{ .39f, .44f, .89f, .72f, .91f }} },
        { "NEGATIVE WEB",
            {{ .70f, .88f, .58f, .48f, .83f, .63f,
               .08f, .56f, .38f, .25f, .48f, .47f }},
            5u, .83f, OutputMode::Direct8, -4.0f, -12.0f, 22147u, .61f,
            {{ .58f, .81f, .48f, .57f, .79f }} },
        { "SLOW PRESS",
            {{ .88f, .91f, .22f, .06f, .95f, .88f,
               .42f, .09f, .61f, .10f, .14f, .79f }},
            4u, .72f, OutputMode::Stereo, 3.0f, -10.0f, 25793u, .18f,
            {{ .86f, .96f, .24f, .78f, .67f }} },
        { "MIC RESONATOR",
            {{ .42f, .58f, .44f, .13f, .79f, .83f,
               .96f, .29f, 1.00f, .08f, .12f, .68f }},
            1u, .68f, OutputMode::Quad, 12.0f, -9.0f, 30469u, .39f,
            {{ .69f, .89f, .36f, 1.00f, .46f }} },
    }};

const char* factoryPresetName(uint32_t index)
{
    return index < kFactoryPresets.size()
        ? kFactoryPresets[index].name : "CUSTOM";
}

SceneSnapshot factoryPresetScene(uint32_t presetIndex, uint32_t sceneIndex)
{
    presetIndex = std::min<uint32_t>(presetIndex,
        kFactoryPresetCount - 1u);
    sceneIndex = std::min<uint32_t>(sceneIndex, 3u);
    const auto& spec = kFactoryPresets[presetIndex];
    SceneSnapshot scene;
    scene.macros = spec.macros;
    uint32_t random = spec.seed
        ^ ((sceneIndex + 1u) * 0x9e3779b9u);
    const float sceneAmount = static_cast<float>(sceneIndex) / 3.0f;
    if (sceneIndex > 0u) {
        for (uint32_t index = 0u; index < scene.macros.size(); ++index) {
            const float direction = hashUnit(random) * 2.0f - 1.0f;
            const float scale = index == 8u || index == 9u
                ? .28f : .18f;
            scene.macros[index] = std::clamp(scene.macros[index]
                + direction * scale * sceneAmount, 0.0f, 1.0f);
        }
    }
    // Every factory scene retains enough internal energy to audition and
    // recover without requiring a microphone, while preserving the relative
    // sparsity and input sensitivity of its preset family.
    scene.macros[0u] = std::max(scene.macros[0u], .34f); // pressure
    scene.macros[1u] = std::max(scene.macros[1u], .40f); // mass
    scene.macros[2u] = std::max(scene.macros[2u], .16f); // edge / event rate
    scene.macros[3u] = std::min(scene.macros[3u], .76f); // bounded blackout
    scene.macros[9u] = std::max(scene.macros[9u], .08f); // idle impacts
    if (scene.macros[9u] + scene.macros[10u] < .22f) {
        scene.macros[10u] = .22f - scene.macros[9u];
    }
    const auto baseMatrix = topologyMatrix(spec.topology,
        presetIndex % 8u, spec.seed);
    const auto sceneMatrix = topologyMatrix(
        (spec.topology + sceneIndex) % 6u,
        (presetIndex + sceneIndex * 2u) % 8u,
        spec.seed ^ (sceneIndex * 0x85ebca6bu));
    const float topologyShift = sceneIndex == 0u
        ? 0.0f : .16f + sceneAmount * .30f;
    for (uint32_t route = 0u; route < scene.matrix.size(); ++route) {
        float value = baseMatrix[route]
            + (sceneMatrix[route] - baseMatrix[route]) * topologyShift;
        if (presetIndex == 9u && route / 8u != route % 8u) {
            value = -std::abs(value);
        }
        scene.matrix[route] = value;
    }
    for (uint32_t cell = 0u; cell < scene.objects.size(); ++cell) {
        auto& object = scene.objects[cell];
        const auto varied = [&](float center, float spread) {
            return std::clamp(center
                + (hashUnit(random) * 2.0f - 1.0f) * spread
                + (static_cast<float>((cell * 3u + presetIndex) % 7u)
                    / 6.0f - .5f) * spread * .55f,
                0.02f, 1.0f);
        };
        object.size = varied(spec.objectCenter[0], .23f);
        object.decay = varied(spec.objectCenter[1], .19f);
        object.hardness = varied(spec.objectCenter[2], .22f);
        object.sensitivity = varied(spec.objectCenter[3], .18f);
        object.drive = varied(spec.objectCenter[4], .20f);
        object.level = varied(.78f, .18f);
        if (sceneIndex > 0u) {
            object.decay = std::clamp(object.decay
                + (sceneIndex == 2u ? .12f : -.04f) * sceneAmount,
                .02f, 1.0f);
            object.hardness = std::clamp(object.hardness
                + (sceneIndex == 1u ? .14f : -.06f) * sceneAmount,
                .02f, 1.0f);
        }
    }
    return scene;
}

SceneSnapshot factoryScene(uint32_t sceneIndex)
{
    SceneSnapshot scene;
    scene.macros = {{
        0.62f, 0.78f, 0.46f, 0.18f, 0.48f, 0.56f,
        0.50f, 0.32f, 0.48f, 0.26f, 0.42f, 0.34f,
    }};
    scene.matrix = topologyMatrix(1u, 0u, 1979u);
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        auto& object = scene.objects[cell];
        object.size = 0.22f + static_cast<float>(cell) * 0.075f;
        object.decay = 0.68f - static_cast<float>(cell & 1u) * 0.16f;
        object.hardness = 0.30f + static_cast<float>((cell * 3u) % 8u) / 11.0f;
        object.sensitivity = 0.52f + static_cast<float>(cell % 3u) * 0.14f;
        object.drive = 0.32f + static_cast<float>((cell * 5u) % 8u) / 15.0f;
        object.level = 0.74f + static_cast<float>(cell % 4u) * 0.07f;
    }
    if (sceneIndex == 1u) {
        scene.macros = {{
            0.48f, 0.56f, 0.82f, 0.38f, 0.62f, 0.31f,
            0.24f, 0.72f, 0.22f, 0.82f, 0.88f, 0.91f,
        }};
        scene.matrix = topologyMatrix(2u, 2u, 4307u);
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            auto& object = scene.objects[cell];
            object.size = (cell & 1u) == 0u ? 0.18f : 0.72f;
            object.decay = 0.28f + static_cast<float>(cell % 3u) * 0.24f;
            object.hardness = 0.72f + static_cast<float>(cell % 2u) * 0.22f;
            object.sensitivity = 0.74f;
            object.drive = 0.60f + static_cast<float>(cell % 4u) * 0.08f;
            object.level = 0.68f + static_cast<float>(cell % 2u) * 0.22f;
        }
    } else if (sceneIndex == 2u) {
        scene.macros = {{
            0.36f, 0.67f, 0.38f, 0.14f, 0.78f, 0.73f,
            0.84f, 0.23f, 0.96f, 0.08f, 0.18f, 0.46f,
        }};
        scene.matrix = topologyMatrix(3u, 4u, 991u);
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            auto& object = scene.objects[cell];
            object.size = 0.56f + static_cast<float>(cell % 3u) * 0.12f;
            object.decay = 0.82f - static_cast<float>(cell) * 0.045f;
            object.hardness = 0.22f + static_cast<float>(cell % 4u) * 0.13f;
            object.sensitivity = cell == 4u ? 1.0f : 0.56f;
            object.drive = cell == 4u ? 0.78f : 0.30f;
            object.level = cell == 4u ? 1.0f : 0.66f;
        }
    } else if (sceneIndex == 3u) {
        scene.macros = {{
            0.76f, 0.42f, 0.64f, 0.68f, 0.86f, 0.48f,
            0.18f, 0.58f, 0.34f, 0.48f, 0.72f, 0.69f,
        }};
        scene.matrix = topologyMatrix(5u, 6u, 8123u);
        uint32_t random = 8123u;
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            auto& object = scene.objects[cell];
            object.size = 0.12f + hashUnit(random) * 0.82f;
            object.decay = 0.16f + hashUnit(random) * 0.80f;
            object.hardness = 0.08f + hashUnit(random) * 0.90f;
            object.sensitivity = 0.24f + hashUnit(random) * 0.74f;
            object.drive = 0.18f + hashUnit(random) * 0.80f;
            object.level = cell == 1u || cell == 6u ? 1.0f : 0.52f;
        }
    }
    return scene;
}

void requestValueRescan(Plugin& p)
{
    p.pendingRescan.store(true, std::memory_order_release);
    if (p.host && p.host->request_callback) p.host->request_callback(p.host);
}

void publishSceneSurface(Plugin& p)
{
    for (clap_id id = kPressureParamId; id <= kMotionParamId; ++id) {
        publishParam(p, id, rawParamValue(p, id));
    }
    for (clap_id id = kContactParamId; id <= kSpringParamId; ++id) {
        publishParam(p, id, rawParamValue(p, id));
    }
    for (uint32_t index = 0u; index < 64u; ++index) {
        publishParam(p, kMatrixParamBase + index, p.matrix[index]);
    }
    for (uint32_t index = 0u; index < 8u; ++index) {
        publishParam(p, kCellLevelParamBase + index, p.cellLevels[index]);
    }
    for (uint32_t bank = 0u; bank < kObjectBankCount; ++bank) {
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            publishParam(p, kObjectParamBases[bank] + cell,
                rawParamValue(p, kObjectParamBases[bank] + cell));
        }
    }
}

void applySceneToEngine(Plugin& p)
{
    if (!p.prepared) return;
    p.engine.setCutVariation(p.variation);
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        p.engine.setCutMask(cell, p.cutMask[cell]);
    }
    p.engine.setFracturePerformance(
        p.fractureDistance, p.fractureForce);
    p.engine.setGrab(p.grabbing);
    p.engine.setRepeat(p.repeat);
    p.repeat = p.engine.repeat();
    p.engine.setParams(p.params);
    p.engine.setMatrix(p.matrix);
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        auto object = p.objects[cell];
        object.level = p.cellLevels[cell];
        p.engine.setObject(cell, object);
    }
}

void loadSnapshotSurface(Plugin& p, const SceneSnapshot& scene)
{
    p.params.pressure = scene.macros[0u];
    p.params.mass = scene.macros[1u];
    p.params.edge = scene.macros[2u];
    p.params.voidAmount = scene.macros[3u];
    p.params.memory = scene.macros[4u];
    p.params.body = scene.macros[5u];
    p.params.voice = scene.macros[6u];
    p.params.motion = scene.macros[7u];
    p.params.contact = scene.macros[8u];
    p.params.shaker = scene.macros[9u];
    p.params.rattle = scene.macros[10u];
    p.params.spring = scene.macros[11u];
    p.matrix = scene.matrix;
    p.objects = scene.objects;
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        p.cellLevels[cell] = scene.objects[cell].level;
    }
}

float interpolate(float first, float second, float amount)
{
    return first + (second - first) * amount;
}

void morphScenes(Plugin& p, float position)
{
    position = std::clamp(position, 0.0f, 3.0f);
    const uint32_t first = static_cast<uint32_t>(std::floor(position));
    const uint32_t second = std::min<uint32_t>(first + 1u, 3u);
    const float amount = position - static_cast<float>(first);
    const auto& a = p.scenes[first];
    const auto& b = p.scenes[second];
    SceneSnapshot result;
    for (uint32_t index = 0u; index < result.macros.size(); ++index) {
        result.macros[index] = interpolate(
            a.macros[index], b.macros[index], amount);
    }
    for (uint32_t index = 0u; index < result.matrix.size(); ++index) {
        result.matrix[index] = interpolate(
            a.matrix[index], b.matrix[index], amount);
    }
    for (uint32_t cell = 0u; cell < result.objects.size(); ++cell) {
        const auto& objectA = a.objects[cell];
        const auto& objectB = b.objects[cell];
        auto& object = result.objects[cell];
        object.size = interpolate(objectA.size, objectB.size, amount);
        object.decay = interpolate(objectA.decay, objectB.decay, amount);
        object.hardness = interpolate(
            objectA.hardness, objectB.hardness, amount);
        object.sensitivity = interpolate(
            objectA.sensitivity, objectB.sensitivity, amount);
        object.drive = interpolate(objectA.drive, objectB.drive, amount);
        object.level = interpolate(objectA.level, objectB.level, amount);
    }
    p.sceneMorph = position;
    loadSnapshotSurface(p, result);
    publishParam(p, kSceneMorphParamId, position);
    publishSceneSurface(p);
    applySceneToEngine(p);
    requestValueRescan(p);
}

void recallScene(Plugin& p, uint32_t sceneNumber)
{
    sceneNumber = std::clamp<uint32_t>(sceneNumber, 1u, 4u);
    const auto& scene = p.scenes[sceneNumber - 1u];
    p.selectedScene = sceneNumber - 1u;
    p.sceneMorph = static_cast<float>(p.selectedScene);
    loadSnapshotSurface(p, scene);
    publishParam(p, kSceneParamId, sceneNumber);
    publishParam(p, kSceneMorphParamId, p.sceneMorph);
    publishSceneSurface(p);
    applySceneToEngine(p);
    requestValueRescan(p);
}

void applyFactoryPreset(Plugin& p, uint32_t presetIndex)
{
    presetIndex = std::min<uint32_t>(presetIndex,
        kFactoryPresetCount - 1u);
    if (p.prepared) p.engine.clearPerformanceLoop();
    const auto& spec = kFactoryPresets[presetIndex];
    for (uint32_t scene = 0u; scene < p.scenes.size(); ++scene) {
        p.scenes[scene] = factoryPresetScene(presetIndex, scene);
    }
    loadSnapshotSurface(p, p.scenes[0u]);
    p.params.inputGainDb = spec.inputGainDb;
    p.params.outputGainDb = spec.outputGainDb;
    p.params.seed = spec.seed;
    p.params.hold = false;
    p.params.run = true;
    // Projection is a routing choice, not part of the instrument preset.
    // Keeping it stable prevents Direct 8 presets from appearing quiet on a
    // stereo-monitoring track that only auditions the first two bus lanes.
    p.selectedCell = presetIndex % 8u;
    p.selectedScene = 0u;
    p.sceneMorph = 0.0f;
    p.topologyShape = spec.topology;
    p.topologyMix = spec.topologyMix;
    p.variation = spec.variation;
    p.cutMask.fill(true);
    p.fractureDistance = 0.0f;
    p.fractureForce = 0.0f;
    p.grabbing = false;
    p.repeat = false;
    p.variationSerial = 0u;
    p.presetIndex = presetIndex;

    for (clap_id id = kPressureParamId; id <= kHoldParamId; ++id) {
        publishParam(p, id, rawParamValue(p, id));
    }
    publishParam(p, kSceneParamId, 1.0);
    publishParam(p, kRunParamId, rawParamValue(p, kRunParamId));
    publishParam(p, kOutputModeParamId,
        rawParamValue(p, kOutputModeParamId));
    publishParam(p, kSelectedCellParamId,
        rawParamValue(p, kSelectedCellParamId));
    for (clap_id id = kContactParamId; id <= kTopologyMixParamId; ++id) {
        publishParam(p, id, rawParamValue(p, id));
    }
    publishParam(p, kVariationParamId,
        rawParamValue(p, kVariationParamId));
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        publishParam(p, kCutMaskParamBase + cell, 1.0);
    }
    publishParam(p, kFractureDistanceParamId, 0.0);
    publishParam(p, kFractureForceParamId, 0.0);
    publishParam(p, kGrabParamId, 0.0);
    publishParam(p, kRepeatParamId, 0.0);
    publishParam(p, kPresetParamId, presetIndex);
    publishSceneSurface(p);
    applySceneToEngine(p);
    if (p.prepared) p.engine.rearm(0.58f + spec.macros[0u] * 0.24f);
    requestValueRescan(p);
}

void markPresetCustom(Plugin& p)
{
    if (p.presetIndex == kCustomPresetIndex) return;
    p.presetIndex = kCustomPresetIndex;
    publishParam(p, kPresetParamId, kCustomPresetIndex);
}

bool changesPresetSurface(clap_id id)
{
    if (isMatrixParam(id) || isCellLevelParam(id) || isObjectParam(id)) {
        return true;
    }
    if (id >= kPressureParamId && id <= kInputParamId) return true;
    if (id == kSeedParamId || id == kOutputModeParamId) return true;
    if (id >= kContactParamId && id <= kSpringParamId) return true;
    return id == kTopologyShapeParamId || id == kTopologyMixParamId
        || isCutMaskParam(id)
        || id == kVariationParamId;
}

void publishObject(Plugin& p, uint32_t cell)
{
    if (cell >= 8u) return;
    publishParam(p, kCellLevelParamBase + cell, p.cellLevels[cell]);
    for (clap_id base : kObjectParamBases) {
        publishParam(p, base + cell, rawParamValue(p, base + cell));
    }
}

void applyTopology(Plugin& p)
{
    const auto target = topologyMatrix(p.topologyShape, p.selectedCell,
        p.params.seed ^ (p.variationSerial * 0x9e3779b9u));
    for (uint32_t index = 0u; index < p.matrix.size(); ++index) {
        p.matrix[index] = interpolate(
            p.matrix[index], target[index], p.topologyMix);
    }
    if (p.prepared) p.engine.setMatrix(p.matrix);
    for (uint32_t index = 0u; index < p.matrix.size(); ++index) {
        publishParam(p, kMatrixParamBase + index, p.matrix[index]);
    }
    requestValueRescan(p);
}

void mutateLinks(Plugin& p)
{
    ++p.variationSerial;
    uint32_t random = p.params.seed
        ^ (p.variationSerial * 0x9e3779b9u) ^ 0x6d2b79f5u;
    const auto bias = topologyMatrix(p.topologyShape, p.selectedCell, random);
    const float amount = p.variation;
    for (uint32_t index = 0u; index < p.matrix.size(); ++index) {
        const uint32_t destination = index / 8u;
        if (!p.cutMask[destination]) continue;
        const float randomRoute = (hashUnit(random) * 2.0f - 1.0f)
            * (0.08f + hashUnit(random) * 0.48f);
        const float target = interpolate(bias[index], randomRoute,
            0.18f + amount * 0.62f);
        p.matrix[index] = std::clamp(interpolate(p.matrix[index], target,
            0.10f + amount * 0.72f), -1.0f, 1.0f);
    }
    if (p.prepared) {
        p.engine.setMatrix(p.matrix);
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            if (hashUnit(random) < 0.24f + amount * 0.42f) {
                p.engine.strikeCell(cell, 0.18f + amount * 0.42f);
            }
        }
    }
    for (uint32_t index = 0u; index < p.matrix.size(); ++index) {
        publishParam(p, kMatrixParamBase + index, p.matrix[index]);
    }
    requestValueRescan(p);
}

void mutateSelectedObject(Plugin& p)
{
    ++p.variationSerial;
    uint32_t random = p.params.seed
        ^ ((p.selectedCell + 1u) * 0x85ebca6bu)
        ^ (p.variationSerial * 0xc2b2ae35u);
    auto& object = p.objects[p.selectedCell];
    const float amount = 0.08f + p.variation * 0.86f;
    const auto mutate = [&](float current, float low, float high) {
        return std::clamp(interpolate(current,
            low + hashUnit(random) * (high - low), amount), 0.0f, 1.0f);
    };
    object.size = mutate(object.size, 0.04f, 0.96f);
    object.decay = mutate(object.decay, 0.06f, 0.98f);
    object.hardness = mutate(object.hardness, 0.03f, 0.99f);
    object.sensitivity = mutate(object.sensitivity, 0.14f, 1.0f);
    object.drive = mutate(object.drive, 0.08f, 0.98f);
    object.level = p.cellLevels[p.selectedCell];
    if (p.prepared) {
        p.engine.setObject(p.selectedCell, object);
        p.engine.strikeCell(p.selectedCell, 0.45f + amount * 0.50f);
    }
    publishObject(p, p.selectedCell);
    requestValueRescan(p);
}

int32_t actionIndex(clap_id id)
{
    if (id >= kCaptureParamId && id <= kPanicParamId) {
        return static_cast<int32_t>(id - kCaptureParamId);
    }
    if (id == kStrikeParamId) return 6;
    if (id == kApplyTopologyParamId) return 7;
    return -1;
}

uint32_t actionBit(clap_id id)
{
    const int32_t index = actionIndex(id);
    return index >= 0 ? 1u << static_cast<uint32_t>(index) : 0u;
}

void performAction(Plugin& p, clap_id id)
{
    if (id == kCaptureParamId) {
        const uint32_t scene = static_cast<uint32_t>(std::clamp(
            std::round(paramValue(p, kSceneParamId)), 1.0, 4.0));
        p.scenes[scene - 1u] = snapshotFromPlugin(p);
        return;
    }
    if (!p.prepared) return;
    switch (id) {
    case kCutParamId:
        mutateLinks(p);
        p.engine.trigger(s3g::ProcessorFissureAction::Cut);
        break;
    case kBreachParamId:
        p.engine.trigger(s3g::ProcessorFissureAction::Breach);
        break;
    case kWithdrawParamId:
        p.engine.trigger(s3g::ProcessorFissureAction::Withdraw);
        break;
    case kNewParamId: {
        mutateSelectedObject(p);
        break;
    }
    case kPanicParamId:
        p.engine.trigger(s3g::ProcessorFissureAction::Panic);
        p.grabbing = false;
        p.repeat = false;
        publishParam(p, kGrabParamId, 0.0);
        publishParam(p, kRepeatParamId, 0.0);
        break;
    case kStrikeParamId:
        p.engine.strikeCell(p.selectedCell, 1.0f);
        break;
    case kApplyTopologyParamId:
        applyTopology(p);
        break;
    default: break;
    }
}

void applyParam(Plugin& p, clap_id id, double value,
    bool actionImmediately)
{
    if (!validParamId(id)) return;
    value = clampParamValue(id, value);
    if (momentaryParam(id)) {
        const int32_t index = actionIndex(id);
        if (index < 0) return;
        const bool gate = value >= 0.5;
        if (gate && !p.actionGates[static_cast<uint32_t>(index)]) {
            if (actionImmediately) performAction(p, id);
            else p.pendingActions.fetch_or(actionBit(id),
                std::memory_order_release);
        }
        p.actionGates[static_cast<uint32_t>(index)] = gate;
        publishParam(p, id, gate ? 1.0 : 0.0);
        return;
    }
    if (id == kPresetParamId) {
        const uint32_t preset = static_cast<uint32_t>(value);
        if (preset < kFactoryPresetCount) applyFactoryPreset(p, preset);
        else markPresetCustom(p);
        return;
    }
    if (changesPresetSurface(id)) markPresetCustom(p);
    if (isCutMaskParam(id)) {
        const uint32_t cell = cutMaskIndex(id);
        p.cutMask[cell] = value >= 0.5;
        if (p.prepared) p.engine.setCutMask(cell, p.cutMask[cell]);
        publishParam(p, id, p.cutMask[cell] ? 1.0 : 0.0);
        return;
    }
    if (isMatrixParam(id)) {
        const uint32_t index = matrixIndex(id);
        p.matrix[index] = static_cast<float>(value);
        if (p.prepared) p.engine.setMatrixRoute(
            index / 8u, index % 8u, static_cast<float>(value));
        publishParam(p, id, value);
        return;
    }
    if (isCellLevelParam(id)) {
        const uint32_t index = cellIndex(id);
        p.cellLevels[index] = static_cast<float>(value);
        p.objects[index].level = static_cast<float>(value);
        if (p.prepared) p.engine.setObject(index, p.objects[index]);
        publishParam(p, id, value);
        return;
    }
    if (isObjectParam(id)) {
        const uint32_t cell = objectCellIndex(id);
        auto& object = p.objects[cell];
        switch (static_cast<uint32_t>(objectBankForParam(id))) {
        case 0u: object.size = static_cast<float>(value); break;
        case 1u: object.decay = static_cast<float>(value); break;
        case 2u: object.hardness = static_cast<float>(value); break;
        case 3u: object.sensitivity = static_cast<float>(value); break;
        default: object.drive = static_cast<float>(value); break;
        }
        object.level = p.cellLevels[cell];
        if (p.prepared) p.engine.setObject(cell, object);
        publishParam(p, id, value);
        return;
    }
    const float v = static_cast<float>(value);
    bool updateEngine = true;
    switch (id) {
    case kPressureParamId: p.params.pressure = v; break;
    case kMassParamId: p.params.mass = v; break;
    case kEdgeParamId: p.params.edge = v; break;
    case kVoidParamId: p.params.voidAmount = v; break;
    case kMemoryParamId: p.params.memory = v; break;
    case kBodyParamId: p.params.body = v; break;
    case kVoiceParamId: p.params.voice = v; break;
    case kMotionParamId: p.params.motion = v; break;
    case kInputParamId: p.params.inputGainDb = v; break;
    case kOutputParamId: p.params.outputGainDb = v; break;
    case kSeedParamId: p.params.seed = static_cast<uint32_t>(value); break;
    case kHoldParamId: p.params.hold = value >= 0.5; break;
    case kSceneParamId:
        recallScene(p, static_cast<uint32_t>(value));
        return;
    case kRunParamId: p.params.run = value >= 0.5; break;
    case kOutputModeParamId:
        p.outputMode = static_cast<OutputMode>(static_cast<uint32_t>(value));
        updateEngine = false;
        break;
    case kSelectedCellParamId:
        p.selectedCell = static_cast<uint32_t>(value) - 1u;
        updateEngine = false;
        break;
    case kContactParamId: p.params.contact = v; break;
    case kShakerParamId: p.params.shaker = v; break;
    case kRattleParamId: p.params.rattle = v; break;
    case kSpringParamId: p.params.spring = v; break;
    case kSceneMorphParamId:
        morphScenes(p, v);
        return;
    case kTopologyShapeParamId:
        p.topologyShape = static_cast<uint32_t>(value);
        updateEngine = false;
        break;
    case kTopologyMixParamId:
        p.topologyMix = v;
        updateEngine = false;
        break;
    case kVariationParamId:
        p.variation = v;
        if (p.prepared) p.engine.setCutVariation(p.variation);
        updateEngine = false;
        break;
    case kFractureDistanceParamId:
        p.fractureDistance = v;
        if (p.prepared) p.engine.setFracturePerformance(
            p.fractureDistance, p.fractureForce);
        updateEngine = false;
        break;
    case kFractureForceParamId:
        p.fractureForce = v;
        if (p.prepared) p.engine.setFracturePerformance(
            p.fractureDistance, p.fractureForce);
        updateEngine = false;
        break;
    case kGrabParamId:
        p.grabbing = value >= 0.5;
        if (p.prepared) {
            p.engine.setGrab(p.grabbing);
            p.repeat = p.engine.repeat();
            publishParam(p, kRepeatParamId, p.repeat ? 1.0 : 0.0);
        }
        updateEngine = false;
        break;
    case kRepeatParamId:
        p.repeat = value >= 0.5;
        if (p.prepared) {
            p.engine.setRepeat(p.repeat);
            p.repeat = p.engine.repeat();
        }
        updateEngine = false;
        break;
    default: return;
    }
    publishParam(p, id, rawParamValue(p, id));
    if (p.prepared && updateEngine) {
        p.engine.setParams(p.params);
    }
}

void releaseActionGates(Plugin& p)
{
    for (uint32_t index = 0u; index < p.actionGates.size(); ++index) {
        p.actionGates[index] = false;
    }
    for (clap_id id = kCaptureParamId; id <= kPanicParamId; ++id) {
        publishParam(p, id, 0.0);
    }
    publishParam(p, kStrikeParamId, 0.0);
    publishParam(p, kApplyTopologyParamId, 0.0);
}

void servicePendingActions(Plugin& p)
{
    const uint32_t pending = p.pendingActions.exchange(
        0u, std::memory_order_acq_rel);
    for (clap_id id = kCaptureParamId; id <= kPanicParamId; ++id) {
        if ((pending & actionBit(id)) != 0u) performAction(p, id);
    }
    if ((pending & actionBit(kStrikeParamId)) != 0u) {
        performAction(p, kStrikeParamId);
    }
    if ((pending & actionBit(kApplyTopologyParamId)) != 0u) {
        performAction(p, kApplyTopologyParamId);
    }
}

void applyEvent(Plugin& p, const clap_event_header_t* event)
{
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (event->type == CLAP_EVENT_PARAM_VALUE
        && event->size >= sizeof(clap_event_param_value_t)) {
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(p, param->param_id, param->value, true);
        return;
    }
    if (event->type == CLAP_EVENT_NOTE_ON
        && event->size >= sizeof(clap_event_note_t)) {
        const auto* note = reinterpret_cast<const clap_event_note_t*>(event);
        if (note->velocity > 0.0) {
            const uint32_t key = static_cast<uint32_t>(
                std::max<int16_t>(0, note->key));
            p.engine.strikeCell((key >= 36u ? key - 36u : key) % 8u,
                static_cast<float>(std::clamp(note->velocity, 0.0, 1.0)));
        }
        return;
    }
    if (event->type == CLAP_EVENT_MIDI
        && event->size >= sizeof(clap_event_midi_t)) {
        const auto* midi = reinterpret_cast<const clap_event_midi_t*>(event);
        if ((midi->data[0] & 0xf0u) == 0x90u && midi->data[2] > 0u) {
            const uint32_t key = midi->data[1];
            p.engine.strikeCell((key >= 36u ? key - 36u : key) % 8u,
                static_cast<float>(midi->data[2]) / 127.0f);
        }
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

void requestGuiParamService(Plugin& p)
{
    if (p.hostParams && p.hostParams->request_flush) {
        p.hostParams->request_flush(p.host);
    } else if (p.host && p.host->request_process) {
        p.host->request_process(p.host);
    }
}

bool queueGuiParamEvent(Plugin& p,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!p.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(p);
    return true;
}

void queueGuiParamGestureBegin(Plugin& p, clap_id id)
{
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiParamValue(Plugin& p, clap_id id, double value)
{
    publishParam(p, id, clampParamValue(id, value));
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& p, clap_id id)
{
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queueGuiParamGesture(Plugin& p, clap_id id, double value)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 3u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, value },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (p.guiParamEvents.pushBatch(events.data(), events.size())) {
        publishParam(p, id, clampParamValue(id, value));
        requestGuiParamService(p);
    }
}

void queueGuiPulse(Plugin& p, clap_id id)
{
    using Kind = s3g::clap_gui::ParamEventKind;
    const std::array<s3g::clap_gui::ParamEvent, 4u> events {{
        { Kind::GestureBegin, id, 0.0 },
        { Kind::Value, id, 1.0 },
        { Kind::Value, id, 0.0 },
        { Kind::GestureEnd, id, 0.0 },
    }};
    if (p.guiParamEvents.pushBatch(events.data(), events.size())) {
        publishParam(p, id, 0.0);
        requestGuiParamService(p);
    }
}

bool pushGuiParamEvent(const clap_output_events_t* output,
    const s3g::clap_gui::ParamEvent& pending)
{
    if (!output || !output->try_push) return true;
    if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
        clap_event_param_value_t event {};
        event.header.size = sizeof(event);
        event.header.time = 0u;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.header.flags = CLAP_EVENT_IS_LIVE;
        event.param_id = pending.paramId;
        event.note_id = event.port_index = event.channel = event.key = -1;
        event.value = pending.value;
        return output->try_push(output, &event.header);
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
    return output->try_push(output, &event.header);
}

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* output,
    bool actionImmediately)
{
    s3g::clap_gui::ParamEvent pending {};
    while (p.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            applyParam(p, pending.paramId, pending.value, actionImmediately);
        }
        p.guiParamEvents.pop();
    }
}

#if defined(__APPLE__)
void guiDestroy(const clap_plugin_t* plugin);
#endif

void destroy(const clap_plugin_t* plugin)
{
#if defined(__APPLE__)
    guiDestroy(plugin);
#endif
    delete self(plugin);
}

bool activate(const clap_plugin_t* plugin, double sampleRate,
    uint32_t, uint32_t)
{
    auto* p = self(plugin);
    p->sampleRate = std::clamp(sampleRate, 8000.0, 768000.0);
    p->engine.prepare(p->sampleRate, 8u);
    p->prepared = true;
    applySceneToEngine(*p);
    return true;
}

void deactivate(const clap_plugin_t* plugin) { self(plugin)->prepared = false; }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}

void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->grabbing = false;
    p->repeat = false;
    p->engine.reset();
    applySceneToEngine(*p);
    p->pendingActions.store(0u, std::memory_order_relaxed);
    p->inputActivity.store(0.0f, std::memory_order_relaxed);
    p->inputPeakActivity.store(0.0f, std::memory_order_relaxed);
    p->contactActivity.store(0.0f, std::memory_order_relaxed);
    p->inputTransferActivity.store(0.0f, std::memory_order_relaxed);
    p->detectedPitchHz.store(0.0f, std::memory_order_relaxed);
    p->pitchConfidence.store(0.0f, std::memory_order_relaxed);
    p->sustainedPitchDrive.store(0.0f, std::memory_order_relaxed);
    p->outputPeak.store(0.0f, std::memory_order_relaxed);
    p->grabbingStatus.store(false, std::memory_order_relaxed);
    p->grabbed.store(false, std::memory_order_relaxed);
    p->grabDuration.store(0.0f, std::memory_order_relaxed);
    p->repeatPhase.store(0.0f, std::memory_order_relaxed);
    p->repeatMix.store(0.0f, std::memory_order_relaxed);
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        p->cutActivity[cell].store(0.0f, std::memory_order_relaxed);
        p->cutPolarity[cell].store(1.0f, std::memory_order_relaxed);
        p->cutFragmentAge[cell].store(0.0f, std::memory_order_relaxed);
    }
    releaseActionGates(*p);
    publishParam(*p, kGrabParamId, 0.0);
    publishParam(*p, kRepeatParamId, 0.0);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    auto* p = self(plugin);
    if (!processData) return CLAP_PROCESS_ERROR;
    serviceGuiParamEvents(*p, processData->out_events, true);
    servicePendingActions(*p);

    const clap_input_events_t* events = processData->in_events;
    const uint32_t eventCount = events ? events->size(events) : 0u;
    uint32_t eventIndex = 0u;
    if (processData->audio_outputs_count == 0u
        || !processData->audio_outputs) {
        while (eventIndex < eventCount) {
            applyEvent(*p, events->get(events, eventIndex++));
        }
        releaseActionGates(*p);
        return CLAP_PROCESS_CONTINUE;
    }
    const auto& output = processData->audio_outputs[0u];
    if (output.channel_count == 0u || (!output.data32 && !output.data64)) {
        return CLAP_PROCESS_ERROR;
    }
    const clap_audio_buffer_t* input = processData->audio_inputs_count > 0u
            && processData->audio_inputs
        ? &processData->audio_inputs[0u] : nullptr;
    float blockOutputPeak = 0.0f;
    for (uint32_t frame = 0u; frame < processData->frames_count; ++frame) {
        while (eventIndex < eventCount) {
            const auto* event = events->get(events, eventIndex);
            if (!event || event->time > frame) break;
            applyEvent(*p, event);
            ++eventIndex;
        }
        const uint32_t activeOutputs = renderChannels(p->outputMode);
        p->frameInput.fill(0.0f);
        if (input) {
            for (uint32_t channel = 0u;
                 channel < std::min<uint32_t>(2u, input->channel_count);
                 ++channel) {
                if (input->data32 && input->data32[channel]) {
                    p->frameInput[channel] = input->data32[channel][frame];
                } else if (input->data64 && input->data64[channel]) {
                    p->frameInput[channel] = static_cast<float>(
                        input->data64[channel][frame]);
                }
            }
        }
        p->frameOutput.fill(0.0f);
        p->engine.processFrame(p->frameInput.data(), p->frameOutput.data(),
            activeOutputs);
        for (uint32_t channel = 0u; channel < output.channel_count; ++channel) {
            const float value = channel < activeOutputs
                ? p->frameOutput[channel] : 0.0f;
            blockOutputPeak = std::max(blockOutputPeak, std::abs(value));
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][frame] = value;
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][frame] = value;
            }
        }
    }
    while (eventIndex < eventCount) {
        applyEvent(*p, events->get(events, eventIndex++));
    }
    for (uint32_t cell = 0u; cell < 8u; ++cell) {
        p->activity[cell].store(p->engine.cellActivity(cell),
            std::memory_order_relaxed);
        p->cutActivity[cell].store(p->engine.cutActivity(cell),
            std::memory_order_relaxed);
        p->cutPolarity[cell].store(p->engine.cutPolarity(cell),
            std::memory_order_relaxed);
        p->cutFragmentAge[cell].store(p->engine.cutFragmentAge(cell),
            std::memory_order_relaxed);
    }
    p->grabbingStatus.store(p->engine.grabbing(),
        std::memory_order_relaxed);
    p->grabbed.store(p->engine.grabbed(), std::memory_order_relaxed);
    p->grabDuration.store(p->engine.grabDurationSeconds(),
        std::memory_order_relaxed);
    p->repeatPhase.store(p->engine.repeatPhase(),
        std::memory_order_relaxed);
    p->repeatMix.store(p->engine.repeatMix(), std::memory_order_relaxed);
    p->minimumGovernor.store(p->engine.minimumGovernor(),
        std::memory_order_relaxed);
    const float meterDecay = std::exp(-static_cast<float>(
        processData->frames_count) / static_cast<float>(
            std::max(1.0, p->sampleRate * 0.38)));
    const auto publishMeter = [meterDecay](std::atomic<float>& meter,
                                          float target) {
        const float previous = meter.load(std::memory_order_relaxed);
        meter.store(std::max(target, previous * meterDecay),
            std::memory_order_relaxed);
    };
    p->inputActivity.store(p->engine.inputLevelActivity(),
        std::memory_order_relaxed);
    publishMeter(p->inputPeakActivity, p->engine.inputPeakActivity());
    publishMeter(p->contactActivity, p->engine.contactActivity());
    publishMeter(p->inputTransferActivity,
        p->engine.inputTransferActivity());
    p->detectedPitchHz.store(p->engine.detectedPitchHz(),
        std::memory_order_relaxed);
    p->pitchConfidence.store(p->engine.pitchConfidence(),
        std::memory_order_relaxed);
    p->sustainedPitchDrive.store(p->engine.sustainedPitchDrive(),
        std::memory_order_relaxed);
    publishMeter(p->outputPeak, std::min(blockOutputPeak, 4.0f));
    releaseActionGates(*p);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->pendingRescan.exchange(false, std::memory_order_acq_rel)
        && p->host && p->hostParams && p->hostParams->rescan) {
        p->hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    }
}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 10u : 20u;
    std::snprintf(info->name, sizeof(info->name), "%s",
        isInput ? "Optional Stereo Excitation" : "8-channel Fissure Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? 2u : 8u;
    info->port_type = isInput ? CLAP_PORT_STEREO : CLAP_PORT_SURROUND;
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
    *info = {};
    info->id = 30u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Fissure Cell Strikes",
        sizeof(info->name) - 1u);
    return true;
}

const clap_plugin_note_ports_t notePorts {
    notePortsCount, notePortsGet
};

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    *info = {};
    const clap_id id = paramIdAt(index);
    info->id = id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (const auto* def = baseParamDef(id)) {
        if (def->stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, def->name, sizeof(info->name) - 1u);
        std::strncpy(info->module, def->module, sizeof(info->module) - 1u);
        info->min_value = def->minimum;
        info->max_value = def->maximum;
        info->default_value = def->defaultValue;
        return true;
    }
    if (isMatrixParam(id)) {
        const uint32_t route = matrixIndex(id);
        std::snprintf(info->name, sizeof(info->name),
            "D%u from S%u", route / 8u + 1u, route % 8u + 1u);
        std::strncpy(info->module, "Matrix / Signed Routes",
            sizeof(info->module) - 1u);
        info->min_value = -1.0;
        info->max_value = 1.0;
        info->default_value = initialMatrix()[route];
        return true;
    }
    if (isCellLevelParam(id)) {
        const uint32_t cell = cellIndex(id);
        std::snprintf(info->name, sizeof(info->name),
            "Cell %u Level", cell + 1u);
        std::strncpy(info->module, "Objects / Level",
            sizeof(info->module) - 1u);
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 1.0;
        return true;
    }
    static constexpr const char* names[] {
        "Size", "Decay", "Hardness", "Sensitivity", "Drive"
    };
    const uint32_t bank = static_cast<uint32_t>(objectBankForParam(id));
    const uint32_t cell = objectCellIndex(id);
    std::snprintf(info->name, sizeof(info->name), "Cell %u %s",
        cell + 1u, names[bank]);
    std::strncpy(info->module, "Objects / Character",
        sizeof(info->module) - 1u);
    info->min_value = 0.0;
    info->max_value = 1.0;
    static constexpr std::array<double, 5u> defaults {{
        0.50, 0.55, 0.50, 0.62, 0.46,
    }};
    info->default_value = defaults[bank];
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    if (!value || !validParamId(id)) return false;
    *value = paramValue(*self(plugin), id);
    return true;
}

const char* outputModeName(double value)
{
    switch (static_cast<uint32_t>(std::clamp(std::round(value), 0.0, 2.0))) {
    case 0u: return "STEREO";
    case 1u: return "QUAD";
    default: return "8 DIRECT";
    }
}

const char* topologyName(double value)
{
    static constexpr const char* names[] {
        "ISLANDS", "RING", "PAIRS", "HUB", "CLUSTERS", "SCATTER"
    };
    return names[static_cast<uint32_t>(std::clamp(
        std::round(value), 0.0, 5.0))];
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !validParamId(id)) return false;
    if (id == kPresetParamId) {
        std::snprintf(display, size, "%s", factoryPresetName(
            static_cast<uint32_t>(std::clamp(std::round(value), 0.0,
                static_cast<double>(kCustomPresetIndex)))));
    } else if ((id >= kPressureParamId && id <= kMotionParamId)
        || (id >= kContactParamId && id <= kSpringParamId)) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else if (id == kInputParamId || id == kOutputParamId) {
        std::snprintf(display, size, "%+.1f dB", value);
    } else if (id == kSeedParamId) {
        std::snprintf(display, size, "%u",
            static_cast<uint32_t>(std::round(value)));
    } else if (id == kSceneParamId) {
        const char letter = static_cast<char>('A' + static_cast<int>(
            std::clamp(std::round(value), 1.0, 4.0)) - 1);
        std::snprintf(display, size, "SCENE %c", letter);
    } else if (id == kOutputModeParamId) {
        std::snprintf(display, size, "%s", outputModeName(value));
    } else if (id == kTopologyShapeParamId) {
        std::snprintf(display, size, "%s", topologyName(value));
    } else if (id == kSceneMorphParamId) {
        const uint32_t first = static_cast<uint32_t>(std::clamp(
            std::floor(value), 0.0, 3.0));
        const uint32_t second = std::min<uint32_t>(first + 1u, 3u);
        const double amount = value - static_cast<double>(first);
        std::snprintf(display, size, "%c→%c %.0f%%",
            'A' + first, 'A' + second, amount * 100.0);
    } else if (id == kSelectedCellParamId) {
        std::snprintf(display, size, "CELL %u",
            static_cast<uint32_t>(std::round(value)));
    } else if (id == kHoldParamId || id == kRunParamId
        || id == kGrabParamId || id == kRepeatParamId
        || isCutMaskParam(id)) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "ON" : "OFF");
    } else if (momentaryParam(id)) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "FIRE" : "READY");
    } else if (isMatrixParam(id)) {
        std::snprintf(display, size, "%+.3f", value);
    } else if (isCellLevelParam(id) || isObjectParam(id)
        || id == kTopologyMixParamId || id == kVariationParamId
        || id == kFractureDistanceParamId
        || id == kFractureForceParamId) {
        std::snprintf(display, size, "%.0f%%", value * 100.0);
    } else {
        std::snprintf(display, size, "%.3f", value);
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    if (!validParamId(id) || !display || !value) return false;
    char* end = nullptr;
    errno = 0;
    double parsed = std::strtod(display, &end);
    if (end == display || errno == ERANGE || !std::isfinite(parsed)) {
        return false;
    }
    if (*end == '%' && ((id >= kPressureParamId && id <= kMotionParamId)
            || (id >= kContactParamId && id <= kSpringParamId)
            || isCellLevelParam(id) || isObjectParam(id)
            || id == kTopologyMixParamId
            || id == kVariationParamId
            || id == kFractureDistanceParamId
            || id == kFractureForceParamId)) parsed *= 0.01;
    *value = clampParamValue(id, parsed);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* p = self(plugin);
    const uint32_t count = input ? input->size(input) : 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        const auto* event = input->get(input, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE
            || event->size < sizeof(clap_event_param_value_t)) continue;
        const auto* param = reinterpret_cast<
            const clap_event_param_value_t*>(event);
        applyParam(*p, param->param_id, param->value, false);
    }
    serviceGuiParamEvents(*p, output, false);
    releaseActionGates(*p);
}

const clap_plugin_params_t paramsExt {
    paramsCount, paramsGetInfo, paramsGetValue,
    paramsValueToText, paramsTextToValue, paramsFlush
};

void writeSceneValues(const SceneSnapshot& scene, double* destination)
{
    uint32_t cursor = 0u;
    for (float value : scene.macros) destination[cursor++] = value;
    for (float value : scene.matrix) destination[cursor++] = value;
    for (const auto& object : scene.objects) {
        destination[cursor++] = object.size;
        destination[cursor++] = object.decay;
        destination[cursor++] = object.hardness;
        destination[cursor++] = object.sensitivity;
        destination[cursor++] = object.drive;
        destination[cursor++] = object.level;
    }
}

void readSceneValues(SceneSnapshot& scene, const double* source)
{
    uint32_t cursor = 0u;
    for (float& value : scene.macros) {
        value = static_cast<float>(std::clamp(source[cursor++], 0.0, 1.0));
    }
    for (float& value : scene.matrix) {
        value = static_cast<float>(std::clamp(source[cursor++], -1.0, 1.0));
    }
    for (auto& object : scene.objects) {
        object.size = static_cast<float>(
            std::clamp(source[cursor++], 0.0, 1.0));
        object.decay = static_cast<float>(
            std::clamp(source[cursor++], 0.0, 1.0));
        object.hardness = static_cast<float>(
            std::clamp(source[cursor++], 0.0, 1.0));
        object.sensitivity = static_cast<float>(
            std::clamp(source[cursor++], 0.0, 1.0));
        object.drive = static_cast<float>(
            std::clamp(source[cursor++], 0.0, 1.0));
        object.level = static_cast<float>(
            std::clamp(source[cursor++], 0.0, 1.0));
    }
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedStateHeader header {};
    SavedStatePayload state {};
    const auto* p = self(plugin);
    for (uint32_t index = 0u; index < state.live.size(); ++index) {
        state.live[index] = paramValue(*p, persistentParamIdAt(index));
    }
    for (uint32_t scene = 0u; scene < p->scenes.size(); ++scene) {
        writeSceneValues(p->scenes[scene],
            state.scenes.data() + scene * kSceneValueCount);
    }
    return s3g::clap_state::writeAll(stream, &header, sizeof(header))
        && s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    SavedStateHeader header {};
    if (!s3g::clap_state::readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic
        || header.sceneValueCount != kSceneValueCount * 4u) {
        return false;
    }
    auto* p = self(plugin);
    SavedStatePayload state {};
    if (header.version == kStateVersion
        && header.liveValueCount == state.live.size()) {
        if (!s3g::clap_state::readAll(stream, &state, sizeof(state))) {
            return false;
        }
    } else if (header.version == kPreviousStateVersion
        && header.liveValueCount == kPreviousPersistentParamCount) {
        PreviousSavedStatePayload previous {};
        if (!s3g::clap_state::readAll(stream, &previous, sizeof(previous))) {
            return false;
        }
        for (uint32_t cell = 0u; cell < 8u; ++cell) {
            applyParam(*p, kCutMaskParamBase + cell, 1.0, false);
        }
        state.scenes = previous.scenes;
        for (uint32_t index = 0u; index < previous.live.size(); ++index) {
            const clap_id id = previousPersistentParamIdAt(index);
            if (id == kSceneParamId || id == kSceneMorphParamId) continue;
            applyParam(*p, id, previous.live[index], false);
        }
        state.live[12u] = previous.live[12u];
        state.live[20u] = previous.live[20u];
    } else {
        return false;
    }
    for (uint32_t scene = 0u; scene < p->scenes.size(); ++scene) {
        readSceneValues(p->scenes[scene],
            state.scenes.data() + scene * kSceneValueCount);
    }
    if (header.version == kStateVersion) {
        for (uint32_t index = 0u; index < state.live.size(); ++index) {
            const clap_id id = persistentParamIdAt(index);
            if (id == kSceneParamId || id == kSceneMorphParamId) continue;
            applyParam(*p, id, state.live[index], false);
        }
    }
    p->selectedScene = static_cast<uint32_t>(std::clamp(
        std::round(state.live[12u]), 1.0, 4.0)) - 1u;
    p->sceneMorph = static_cast<float>(std::clamp(
        state.live[20u], 0.0, 3.0));
    publishParam(*p, kSceneParamId, p->selectedScene + 1u);
    publishParam(*p, kSceneMorphParamId, p->sceneMorph);
    p->pendingActions.store(0u, std::memory_order_relaxed);
    p->grabbing = false;
    p->repeat = false;
    if (p->prepared) {
        p->engine.clearPerformanceLoop();
    }
    releaseActionGates(*p);
    publishParam(*p, kGrabParamId, 0.0);
    publishParam(*p, kRepeatParamId, 0.0);
    requestValueRescan(*p);
    if (p->host && p->hostParams && p->hostParams->request_flush) {
        p->hostParams->request_flush(p->host);
    }
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)
#include "s3g_processor_fissure_gui.inc"
#endif

namespace {

const void* getExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
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
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_MULTI_EFFECTS,
    CLAP_PLUGIN_FEATURE_SURROUND,
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.processor-fissure",
    "s3g Processor Fissure",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.8.0",
    "Eight-object physical noise instrument with a performed control-loop Grab/Repeat, sustained mic pitch coupling, and Stereo, Quad, or direct-eight rendering.",
    features
};

const clap_plugin_t* create(const clap_host_t* host)
{
    auto* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    for (uint32_t scene = 0u; scene < p->scenes.size(); ++scene) {
        p->scenes[scene] = factoryPresetScene(0u, scene);
    }
    loadSnapshotSurface(*p, p->scenes[0u]);
    const auto& initPreset = kFactoryPresets[0u];
    p->params.inputGainDb = initPreset.inputGainDb;
    p->params.outputGainDb = initPreset.outputGainDb;
    p->params.seed = initPreset.seed;
    p->outputMode = initPreset.outputMode;
    p->topologyShape = initPreset.topology;
    p->topologyMix = initPreset.topologyMix;
    p->variation = initPreset.variation;
    p->presetIndex = 0u;
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        const clap_id id = paramIdAt(index);
        double value = rawParamValue(*p, id);
        if (id == kSceneParamId) value = 1.0;
        if (momentaryParam(id)) value = 0.0;
        publishParam(*p, id, value);
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
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0) return nullptr;
    return create(host);
}

const clap_plugin_factory_t factory {
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    factoryCreatePlugin,
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
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
