#include "s3g_fractional_waveguide_network.h"
#include "s3g_euclidean_rhythm.h"
#include "s3g_midi_node_allocator.h"
#include "s3g_scale_note_pool.h"
#include "../common/s3g_clap_gui_param_queue.h"
#include "../common/s3g_clap_state_stream.h"

#include <clap/clap.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#include "../common/s3g_clap_macos.h"
#include "../common/s3g_cocoa_gui.h"
#include "../common/s3g_gui_layout.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {

constexpr uint32_t kInputChannels = 1u;
constexpr uint32_t kOutputChannels = 16u;
constexpr uint32_t kNodeCount = 8u;
constexpr uint32_t kEdgeCount = 12u;
constexpr uint32_t kStateVersion = 7u;
constexpr uint32_t kLegacyStateVersionV1 = 1u;
constexpr uint32_t kLegacyStateVersionV2 = 2u;
constexpr uint32_t kLegacyStateVersionV3 = 3u;
constexpr uint32_t kLegacyStateVersionV4 = 4u;
constexpr uint32_t kLegacyStateVersionV5 = 5u;
constexpr uint32_t kLegacyStateVersionV6 = 6u;
constexpr uint32_t kGuiWidth = 920u;
constexpr uint32_t kGuiHeight = 680u;

constexpr clap_id kOrderParamId = 1u;
constexpr clap_id kSpeedParamId = 2u;
constexpr clap_id kDecayParamId = 3u;
constexpr clap_id kAbsorptionParamId = 4u;
constexpr clap_id kNonlinearityParamId = 5u;
constexpr clap_id kRadiationParamId = 6u;
constexpr clap_id kSizeParamId = 7u;
constexpr clap_id kActuatorNodeParamId = 8u;
constexpr clap_id kActuatorGainParamId = 9u;
constexpr clap_id kStrikeGainParamId = 10u;
constexpr clap_id kOutputGainParamId = 11u;
constexpr clap_id kSelfExcitationGainParamId = 12u;
constexpr clap_id kSelfExcitationRateParamId = 13u;
constexpr clap_id kEuclideanStepsParamId = 14u;
constexpr clap_id kEuclideanPulsesFirstParamId = 15u;
constexpr clap_id kEuclideanRotationFirstParamId = 23u;
constexpr clap_id kExciterTypeParamId = 31u;
constexpr clap_id kSustainedExcitationParamId = 32u;
constexpr clap_id kExciterCharacterParamId = 33u;
constexpr clap_id kDispersionParamId = 34u;
constexpr clap_id kMidiModeParamId = 35u;
constexpr clap_id kMidiTransposeParamId = 36u;
constexpr clap_id kMidiAttackParamId = 37u;
constexpr clap_id kMidiReleaseParamId = 38u;
constexpr clap_id kMidiVelocityParamId = 39u;
constexpr clap_id kNodeDirectivityFirstParamId = 40u;
constexpr clap_id kSequencerScaleParamId = 48u;
constexpr clap_id kSequencerNoteCountParamId = 49u;
constexpr uint32_t kParamCount = 49u;
constexpr uint32_t kLegacyParamCountV1 = 11u;
constexpr uint32_t kLegacyParamCountV2 = 13u;
constexpr uint32_t kLegacyParamCountV3 = 30u;
constexpr uint32_t kLegacyParamCountV4 = 34u;
constexpr uint32_t kLegacyParamCountV5 = 39u;
constexpr uint32_t kLegacyParamCountV6 = 47u;

constexpr std::array<uint32_t, kNodeCount> kDefaultEuclideanPulses {{
    5u, 4u, 3u, 2u, 5u, 3u, 4u, 2u
}};
constexpr std::array<uint32_t, kNodeCount> kDefaultEuclideanRotations {{
    0u, 3u, 6u, 9u, 2u, 5u, 8u, 11u
}};
constexpr std::array<float, kNodeCount> kDefaultNodeDirectivity {{
    0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f, 0.55f
}};

constexpr clap_id euclideanPulsesParamId(uint32_t node)
{
    return kEuclideanPulsesFirstParamId + node;
}

constexpr clap_id euclideanRotationParamId(uint32_t node)
{
    return kEuclideanRotationFirstParamId + node;
}

constexpr bool isEuclideanPulsesParam(clap_id id)
{
    return id >= kEuclideanPulsesFirstParamId
        && id < kEuclideanPulsesFirstParamId + kNodeCount;
}

constexpr bool isEuclideanRotationParam(clap_id id)
{
    return id >= kEuclideanRotationFirstParamId
        && id < kEuclideanRotationFirstParamId + kNodeCount;
}

constexpr clap_id nodeDirectivityParamId(uint32_t node)
{
    return kNodeDirectivityFirstParamId + node;
}

constexpr bool isNodeDirectivityParam(clap_id id)
{
    return id >= kNodeDirectivityFirstParamId
        && id < kNodeDirectivityFirstParamId + kNodeCount;
}

struct ParamSpec {
    clap_id id;
    const char* name;
    const char* module;
    double minimum;
    double maximum;
    double defaultValue;
    bool stepped;
    bool logarithmic;
};

constexpr std::array<ParamSpec, kParamCount> kParamSpecs {{
    { kOrderParamId, "Ambisonic Order", "Output", 1.0, 3.0, 3.0, true, false },
    { kSpeedParamId, "Propagation Speed", "Medium", 20.0, 2000.0, 343.0, false, true },
    { kDecayParamId, "Decay", "Medium", 0.05, 60.0, 2.5, false, true },
    { kAbsorptionParamId, "High Frequency Absorption", "Medium", 0.0, 1.0, 0.22, false, false },
    { kNonlinearityParamId, "Junction Nonlinearity", "Medium", 0.0, 1.0, 0.0, false, false },
    { kRadiationParamId, "Velocity Radiation", "Radiation", 0.0, 1.0, 0.20, false, false },
    { kSizeParamId, "Cube Half Extent", "Structure", 0.02, 4.0, 0.5, false, true },
    { kActuatorNodeParamId, "Actuator Node", "Excitation", 1.0, 8.0, 1.0, true, false },
    { kActuatorGainParamId, "Actuator Gain", "Excitation", 0.0, 2.0, 1.0, false, false },
    { kStrikeGainParamId, "Strike Gain", "Excitation", 0.0, 2.0, 1.0, false, false },
    { kOutputGainParamId, "Output Gain", "Output", -60.0, 12.0, -12.0, false, false },
    { kSelfExcitationGainParamId, "Self Excitation", "Excitation", 0.0, 1.0, 0.32, false, false },
    { kSelfExcitationRateParamId, "Self Excitation Rate", "Excitation", 0.05, 12.0, 0.8, false, true },
    { kEuclideanStepsParamId, "Euclidean Steps", "Excitation / Euclidean", 4.0, 32.0, 16.0, true, false },
    { euclideanPulsesParamId(0u), "Node 1 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[0], true, false },
    { euclideanPulsesParamId(1u), "Node 2 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[1], true, false },
    { euclideanPulsesParamId(2u), "Node 3 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[2], true, false },
    { euclideanPulsesParamId(3u), "Node 4 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[3], true, false },
    { euclideanPulsesParamId(4u), "Node 5 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[4], true, false },
    { euclideanPulsesParamId(5u), "Node 6 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[5], true, false },
    { euclideanPulsesParamId(6u), "Node 7 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[6], true, false },
    { euclideanPulsesParamId(7u), "Node 8 Euclidean Pulses", "Excitation / Euclidean", 0.0, 32.0, kDefaultEuclideanPulses[7], true, false },
    { euclideanRotationParamId(0u), "Node 1 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[0], true, false },
    { euclideanRotationParamId(1u), "Node 2 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[1], true, false },
    { euclideanRotationParamId(2u), "Node 3 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[2], true, false },
    { euclideanRotationParamId(3u), "Node 4 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[3], true, false },
    { euclideanRotationParamId(4u), "Node 5 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[4], true, false },
    { euclideanRotationParamId(5u), "Node 6 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[5], true, false },
    { euclideanRotationParamId(6u), "Node 7 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[6], true, false },
    { euclideanRotationParamId(7u), "Node 8 Euclidean Rotation", "Excitation / Euclidean", 0.0, 31.0, kDefaultEuclideanRotations[7], true, false },
    { kExciterTypeParamId, "Sustained Exciter", "Excitation / Continuous", 0.0, 3.0, 0.0, true, false },
    { kSustainedExcitationParamId, "Sustain Drive", "Excitation / Continuous", 0.0, 1.0, 0.0, false, false },
    { kExciterCharacterParamId, "Exciter Character", "Excitation / Continuous", 0.0, 1.0, 0.5, false, false },
    { kDispersionParamId, "Waveguide Dispersion", "Medium", 0.0, 1.0, 0.0, false, false },
    { kMidiModeParamId, "MIDI Note Layer", "MIDI", 0.0, 3.0, 0.0, true, false },
    { kMidiTransposeParamId, "MIDI Transpose", "MIDI", -24.0, 24.0, 0.0, true, false },
    { kMidiAttackParamId, "MIDI Attack", "MIDI", 0.5, 500.0, 8.0, false, true },
    { kMidiReleaseParamId, "MIDI Release", "MIDI", 5.0, 5000.0, 600.0, false, true },
    { kMidiVelocityParamId, "MIDI Velocity Sensitivity", "MIDI", 0.0, 1.0, 0.85, false, false },
    { nodeDirectivityParamId(0u), "Node 1 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[0], false, false },
    { nodeDirectivityParamId(1u), "Node 2 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[1], false, false },
    { nodeDirectivityParamId(2u), "Node 3 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[2], false, false },
    { nodeDirectivityParamId(3u), "Node 4 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[3], false, false },
    { nodeDirectivityParamId(4u), "Node 5 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[4], false, false },
    { nodeDirectivityParamId(5u), "Node 6 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[5], false, false },
    { nodeDirectivityParamId(6u), "Node 7 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[6], false, false },
    { nodeDirectivityParamId(7u), "Node 8 Directivity Mask", "Radiation / Node", 0.0, 1.0, kDefaultNodeDirectivity[7], false, false },
    { kSequencerScaleParamId, "Sequencer Scale", "Excitation / Euclidean", 0.0, 7.0, 0.0, true, false },
    { kSequencerNoteCountParamId, "Sequencer Note Variations", "Excitation / Euclidean", 1.0, 8.0, 1.0, true, false },
}};

const ParamSpec* paramSpec(clap_id id)
{
    for (const auto& spec : kParamSpecs) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

uint32_t paramIndex(clap_id id)
{
    return id >= kOrderParamId
        && id <= kSequencerNoteCountParamId
        ? static_cast<uint32_t>(id - kOrderParamId) : kParamCount;
}

double clampParamValue(const ParamSpec& spec, double value)
{
    value = std::isfinite(value) ? value : spec.defaultValue;
    value = std::clamp(value, spec.minimum, spec.maximum);
    return spec.stepped ? std::round(value) : value;
}

double normalizedParamValue(const ParamSpec& spec, double value)
{
    value = clampParamValue(spec, value);
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
    const double value = spec.logarithmic && spec.minimum > 0.0
        ? spec.minimum * std::pow(
            spec.maximum / spec.minimum, normalized)
        : spec.minimum + normalized * (spec.maximum - spec.minimum);
    return clampParamValue(spec, value);
}

struct SavedState {
    uint32_t version = kStateVersion;
    uint32_t reserved = 0u;
    std::array<double, kParamCount> values {};
};

struct LegacySavedStateV1 {
    uint32_t version = kLegacyStateVersionV1;
    uint32_t reserved = 0u;
    std::array<double, kLegacyParamCountV1> values {};
};

struct LegacySavedStateV2 {
    uint32_t version = kLegacyStateVersionV2;
    uint32_t reserved = 0u;
    std::array<double, kLegacyParamCountV2> values {};
};

struct LegacySavedStateV3 {
    uint32_t version = kLegacyStateVersionV3;
    uint32_t reserved = 0u;
    std::array<double, kLegacyParamCountV3> values {};
};

struct LegacySavedStateV4 {
    uint32_t version = kLegacyStateVersionV4;
    uint32_t reserved = 0u;
    std::array<double, kLegacyParamCountV4> values {};
};

struct LegacySavedStateV5 {
    uint32_t version = kLegacyStateVersionV5;
    uint32_t reserved = 0u;
    std::array<double, kLegacyParamCountV5> values {};
};

struct LegacySavedStateV6 {
    uint32_t version = kLegacyStateVersionV6;
    uint32_t reserved = 0u;
    std::array<double, kLegacyParamCountV6> values {};
};

static_assert(sizeof(LegacySavedStateV1) == 96u);
static_assert(sizeof(LegacySavedStateV2) == 112u);
static_assert(sizeof(LegacySavedStateV3) == 248u);
static_assert(sizeof(LegacySavedStateV4) == 280u);
static_assert(sizeof(LegacySavedStateV5) == 320u);
static_assert(sizeof(LegacySavedStateV6) == 384u);
static_assert(sizeof(SavedState) == 400u);

enum class PerformanceEventKind : uint8_t {
    Strike,
    SequencedStrike,
    MidiNoteOn,
    MidiNoteOff,
    MidiAllNotesOff,
};

struct PerformanceEvent {
    PerformanceEventKind kind = PerformanceEventKind::Strike;
    uint32_t time = 0u;
    uint32_t node = 0u;
    int32_t key = -1;
    float amplitude = 0.0f;
};

struct PerformanceEventBatch {
    static constexpr uint32_t kCapacity = 256u;
    std::array<PerformanceEvent, kCapacity> events {};
    uint32_t count = 0u;
};

void sortPerformanceEvents(PerformanceEventBatch& batch)
{
    // The batch is deliberately small and fixed-size. Stable insertion sort
    // keeps event ordering deterministic without temporary audio-thread memory.
    for (uint32_t index = 1u; index < batch.count; ++index) {
        const PerformanceEvent event = batch.events[index];
        uint32_t insertion = index;
        while (insertion > 0u
            && batch.events[insertion - 1u].time > event.time) {
            batch.events[insertion] = batch.events[insertion - 1u];
            --insertion;
        }
        batch.events[insertion] = event;
    }
}

struct MidiNodeVoice {
    int32_t key = -1;
    float velocity = 1.0f;
    float envelope = 0.0f;
    float phase = 0.0f;
    float frequencyHz = 440.0f;
    float filteredNoise = 0.0f;
    uint32_t percussiveAttackRemaining = 0u;
    uint64_t order = 0u;
    uint32_t randomState = 0x9e3779b9u;
    bool held = false;
};

struct SequencerPitchVoice {
    int32_t key = -1;
    float phase = 0.0f;
    float frequencyHz = 261.625565f;
    float envelope = 0.0f;
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    const clap_host_tail_t* hostTail = nullptr;
    double sampleRate = 48000.0;
    uint32_t maxFrames = 0u;
    bool active = false;

    s3g::FractionalWaveguideParams waveguideParams {};
    float sizeMetres = 0.5f;
    float actuatorGain = 1.0f;
    float strikeGain = 1.0f;
    float selfExcitationGain = 0.32f;
    float selfExcitationRate = 0.8f;
    uint32_t euclideanSteps = 16u;
    std::array<uint32_t, kNodeCount> euclideanPulses =
        kDefaultEuclideanPulses;
    std::array<uint32_t, kNodeCount> euclideanRotations =
        kDefaultEuclideanRotations;
    std::array<float, kNodeCount> nodeDirectivity =
        kDefaultNodeDirectivity;
    s3g::ScaleRule sequencerScale = s3g::ScaleRule::Major;
    uint32_t sequencerNoteCount = 1u;
    uint32_t actuatorNode = 0u;
    uint32_t midiMode = 0u;
    int32_t midiTranspose = 0;
    float midiAttackMs = 8.0f;
    float midiReleaseMs = 600.0f;
    float midiVelocitySensitivity = 0.85f;
    float midiEnvelope = 0.0f;
    float activeMidiVelocity = 1.0f;
    int32_t activeMidiNote = -1;
    int32_t lastMidiNote = 60;
    uint64_t midiNoteOrderCounter = 0u;
    std::array<uint16_t, 128u> heldMidiNoteCount {};
    std::array<float, 128u> heldMidiVelocity {};
    std::array<uint64_t, 128u> heldMidiNoteOrder {};
    std::array<MidiNodeVoice, kNodeCount> midiNodeVoices {};
    std::array<SequencerPitchVoice, kNodeCount> sequencerPitchVoices {};
    float sequencerPitchDecay = 0.999f;
    uint32_t midiNodeCursor = 0u;
    uint32_t midiNodeRandomState = 0x6d2b79f5u;
    s3g::MidiNodeShuffleBag midiNodeShuffleBag {};
    uint32_t sequencerNodeCursor = 0u;
    uint32_t sequencerNodeRandomState = 0x4f1bbcdcu;
    s3g::MidiNodeShuffleBag sequencerNodeShuffleBag {};
    uint64_t selfExcitationCountdown = 0u;
    uint32_t selfExcitationStep = 0u;
    uint32_t selfExcitationRandomState = 0x51f15e1du;
    std::array<uint64_t, kNodeCount> selfRatchetCountdown {};
    std::array<uint64_t, kNodeCount> selfRatchetSpacing {};
    std::array<uint32_t, kNodeCount> selfRatchetsRemaining {};
    bool selfExcitationWasEnabled = false;
    std::array<float, kNodeCount> nodeStrikeFlash {};
    s3g::FractionalWaveguideNetwork network;

    std::vector<float> actuatorScratch;
    std::vector<float> midiGateScratch;
    std::array<std::vector<float>, kNodeCount> midiNodeActuatorScratch {};
    std::array<std::vector<float>, kNodeCount> midiNodeGateScratch {};
    std::array<const float*, kNodeCount> midiNodeActuatorPointers {};
    std::array<const float*, kNodeCount> midiNodeGatePointers {};
    std::array<std::vector<float>, kOutputChannels> outputScratch {};
    std::array<float*, kOutputChannels> outputPointers {};

    std::array<std::atomic<double>, kParamCount> publishedParams {};
    std::array<std::atomic<float>, kNodeCount> nodeEnergy {};
    std::array<std::atomic<float>, kNodeCount> midiNodeActivity {};
    std::array<std::atomic<float>, kNodeCount> nodeStrikeActivity {};
    std::array<std::atomic<int32_t>, kNodeCount> midiNodeKey {};
    std::array<std::atomic<float>, kEdgeCount> edgeEnergy {};
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<float> guardGain { 1.0f };
    std::atomic<int32_t> previewStrikeNode { -1 };
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};

#if defined(__APPLE__)
    void* guiView = nullptr;
    std::atomic<bool> guiVisible { false };
    uint32_t guiMeterCountdown = 0u;
    int guiViewMode = 0;
    int guiExcitationPage = 0;
    float guiViewAzDeg = 35.0f;
    float guiViewElDeg = -34.0f;
    float guiViewZoom = 1.0f;
    s3g::clap_gui::ResponsiveViewport guiViewport {};
#endif
};

Plugin* self(const clap_plugin_t* plugin)
{
    return static_cast<Plugin*>(plugin->plugin_data);
}

float midiNoteFrequency(int32_t note, int32_t transpose)
{
    const float semitones = static_cast<float>(
        std::clamp(note + transpose, -48, 175) - 69);
    return 440.0f * std::pow(2.0f, semitones / 12.0f);
}

double appliedParamValue(const Plugin& p, clap_id id)
{
    if (isEuclideanPulsesParam(id)) {
        return p.euclideanPulses[
            static_cast<uint32_t>(id - kEuclideanPulsesFirstParamId)];
    }
    if (isEuclideanRotationParam(id)) {
        return p.euclideanRotations[
            static_cast<uint32_t>(id - kEuclideanRotationFirstParamId)];
    }
    if (isNodeDirectivityParam(id)) {
        return p.nodeDirectivity[
            static_cast<uint32_t>(id - kNodeDirectivityFirstParamId)];
    }
    switch (id) {
    case kOrderParamId: return p.waveguideParams.order;
    case kSpeedParamId: return p.waveguideParams.propagationSpeed;
    case kDecayParamId: return p.waveguideParams.decaySeconds;
    case kAbsorptionParamId: return p.waveguideParams.absorption;
    case kNonlinearityParamId: return p.waveguideParams.junctionNonlinearity;
    case kRadiationParamId: return p.waveguideParams.radiation;
    case kSizeParamId: return p.sizeMetres;
    case kActuatorNodeParamId: return p.actuatorNode + 1u;
    case kActuatorGainParamId: return p.actuatorGain;
    case kStrikeGainParamId: return p.strikeGain;
    case kOutputGainParamId: return p.waveguideParams.outputGainDb;
    case kSelfExcitationGainParamId: return p.selfExcitationGain;
    case kSelfExcitationRateParamId: return p.selfExcitationRate;
    case kEuclideanStepsParamId: return p.euclideanSteps;
    case kExciterTypeParamId:
        return static_cast<uint32_t>(p.waveguideParams.exciter);
    case kSustainedExcitationParamId:
        return p.waveguideParams.sustainedExcitation;
    case kExciterCharacterParamId:
        return p.waveguideParams.exciterCharacter;
    case kDispersionParamId: return p.waveguideParams.dispersion;
    case kMidiModeParamId: return p.midiMode;
    case kMidiTransposeParamId: return p.midiTranspose;
    case kMidiAttackParamId: return p.midiAttackMs;
    case kMidiReleaseParamId: return p.midiReleaseMs;
    case kMidiVelocityParamId: return p.midiVelocitySensitivity;
    case kSequencerScaleParamId:
        return static_cast<uint32_t>(p.sequencerScale);
    case kSequencerNoteCountParamId: return p.sequencerNoteCount;
    default: return 0.0;
    }
}

void publishParam(Plugin& p, clap_id id, double value)
{
    const uint32_t index = paramIndex(id);
    const auto* spec = paramSpec(id);
    if (index < kParamCount && spec) {
        p.publishedParams[index].store(
            clampParamValue(*spec, value), std::memory_order_release);
    }
}

void publishAppliedParam(Plugin& p, clap_id id)
{
    publishParam(p, id, appliedParamValue(p, id));
}

void publishAllParams(Plugin& p)
{
    for (const auto& spec : kParamSpecs) {
        publishAppliedParam(p, spec.id);
    }
}

double publishedParamValue(const Plugin& p, clap_id id)
{
    const uint32_t index = paramIndex(id);
    return index < kParamCount
        ? p.publishedParams[index].load(std::memory_order_acquire) : 0.0;
}

bool paramAffectsTail(clap_id id)
{
    return id == kSpeedParamId || id == kDecayParamId
        || id == kAbsorptionParamId || id == kSizeParamId
        || id == kSelfExcitationGainParamId
        || id == kExciterTypeParamId
        || id == kSustainedExcitationParamId
        || id == kMidiModeParamId
        || id == kMidiReleaseParamId
        || isEuclideanPulsesParam(id);
}

struct ParamUpdateBatch {
    std::array<bool, kParamCount> changed {};
    bool any = false;
    bool networkParamsChanged = false;
    bool geometryChanged = false;
    bool midiModeChanged = false;
    bool midiTransposeChanged = false;
    bool tailChanged = false;
};

bool stageParamUpdate(Plugin& p, clap_id id, double requested,
    ParamUpdateBatch& batch)
{
    const auto* spec = paramSpec(id);
    if (!spec) return false;
    const double value = clampParamValue(*spec, requested);
    const uint32_t changedIndex = paramIndex(id);
    if (changedIndex < kParamCount) batch.changed[changedIndex] = true;
    batch.any = true;
    batch.tailChanged |= paramAffectsTail(id);
    if (isEuclideanPulsesParam(id)) {
        p.euclideanPulses[
            static_cast<uint32_t>(id - kEuclideanPulsesFirstParamId)] =
            static_cast<uint32_t>(value);
        return true;
    }
    if (isEuclideanRotationParam(id)) {
        p.euclideanRotations[
            static_cast<uint32_t>(id - kEuclideanRotationFirstParamId)] =
            static_cast<uint32_t>(value);
        return true;
    }
    if (isNodeDirectivityParam(id)) {
        p.nodeDirectivity[
            static_cast<uint32_t>(id - kNodeDirectivityFirstParamId)] =
            static_cast<float>(value);
        return true;
    }
    batch.geometryChanged |= id == kSizeParamId
        && std::abs(value - p.sizeMetres) > 1.0e-7;
    switch (id) {
    case kOrderParamId:
        p.waveguideParams.order = static_cast<uint32_t>(value);
        batch.networkParamsChanged = true;
        break;
    case kSpeedParamId:
        p.waveguideParams.propagationSpeed = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kDecayParamId:
        p.waveguideParams.decaySeconds = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kAbsorptionParamId:
        p.waveguideParams.absorption = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kNonlinearityParamId:
        p.waveguideParams.junctionNonlinearity = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kRadiationParamId:
        p.waveguideParams.radiation = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kSizeParamId:
        p.sizeMetres = static_cast<float>(value);
        break;
    case kActuatorNodeParamId:
        p.actuatorNode = static_cast<uint32_t>(value - 1.0);
        break;
    case kActuatorGainParamId:
        p.actuatorGain = static_cast<float>(value);
        break;
    case kStrikeGainParamId:
        p.strikeGain = static_cast<float>(value);
        break;
    case kOutputGainParamId:
        p.waveguideParams.outputGainDb = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kSelfExcitationGainParamId:
        p.selfExcitationGain = static_cast<float>(value);
        break;
    case kSelfExcitationRateParamId:
        p.selfExcitationRate = static_cast<float>(value);
        break;
    case kEuclideanStepsParamId:
        p.euclideanSteps = static_cast<uint32_t>(value);
        p.selfExcitationStep %= std::max<uint32_t>(1u, p.euclideanSteps);
        break;
    case kExciterTypeParamId:
        p.waveguideParams.exciter = static_cast<s3g::WaveguideExciter>(
            static_cast<uint32_t>(value));
        batch.networkParamsChanged = true;
        break;
    case kSustainedExcitationParamId:
        p.waveguideParams.sustainedExcitation = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kExciterCharacterParamId:
        p.waveguideParams.exciterCharacter = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kDispersionParamId:
        p.waveguideParams.dispersion = static_cast<float>(value);
        batch.networkParamsChanged = true;
        break;
    case kMidiModeParamId:
        p.midiMode = static_cast<uint32_t>(value);
        p.midiEnvelope = 0.0f;
        p.activeMidiNote = -1;
        p.midiNoteOrderCounter = 0u;
        p.heldMidiNoteCount.fill(0u);
        p.heldMidiVelocity.fill(0.0f);
        p.heldMidiNoteOrder.fill(0u);
        p.midiNodeCursor = 0u;
        p.midiNodeRandomState = 0x6d2b79f5u;
        p.midiNodeShuffleBag.reset();
        p.sequencerNodeCursor = 0u;
        p.sequencerNodeRandomState = 0x4f1bbcdcu;
        p.sequencerNodeShuffleBag.reset();
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            p.midiNodeVoices[node] = {};
            p.midiNodeVoices[node].randomState =
                0x9e3779b9u ^ (0x85ebca6bu * (node + 1u));
        }
        batch.midiModeChanged = true;
        break;
    case kMidiTransposeParamId:
        p.midiTranspose = static_cast<int32_t>(value);
        batch.midiTransposeChanged = true;
        break;
    case kMidiAttackParamId:
        p.midiAttackMs = static_cast<float>(value);
        break;
    case kMidiReleaseParamId:
        p.midiReleaseMs = static_cast<float>(value);
        break;
    case kMidiVelocityParamId:
        p.midiVelocitySensitivity = static_cast<float>(value);
        break;
    case kSequencerScaleParamId:
        p.sequencerScale = static_cast<s3g::ScaleRule>(
            static_cast<uint32_t>(value));
        break;
    case kSequencerNoteCountParamId:
        p.sequencerNoteCount = static_cast<uint32_t>(value);
        break;
    default:
        return false;
    }
    return true;
}

void commitParamUpdates(Plugin& p, const ParamUpdateBatch& batch)
{
    if (!batch.any) return;
    if (batch.networkParamsChanged) {
        p.waveguideParams =
            s3g::sanitizeFractionalWaveguideParams(p.waveguideParams);
    }
    if (p.active) {
        if (batch.geometryChanged) p.network.morphCube(p.sizeMetres);
        if (batch.networkParamsChanged || batch.geometryChanged) {
            p.network.setParams(p.waveguideParams);
        }
        if (batch.midiModeChanged) p.network.clearTuningFrequency();
        if (batch.midiTransposeChanged && p.midiMode == 1u
            && p.activeMidiNote >= 0) {
            p.network.setTuningFrequency(midiNoteFrequency(
                p.activeMidiNote, p.midiTranspose));
        } else if (batch.midiTransposeChanged && p.midiMode >= 2u) {
            for (auto& voice : p.midiNodeVoices) {
                if (voice.key >= 0) {
                    voice.frequencyHz = midiNoteFrequency(
                        voice.key, p.midiTranspose);
                }
            }
        }
        if (batch.midiTransposeChanged) {
            for (auto& voice : p.sequencerPitchVoices) {
                if (voice.key >= 0) {
                    voice.frequencyHz = midiNoteFrequency(
                        voice.key, p.midiTranspose);
                }
            }
        }
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            if (batch.changed[paramIndex(nodeDirectivityParamId(node))]) {
                p.network.setNodeDirectivity(
                    node, p.nodeDirectivity[node]);
            }
        }
    }
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        if (batch.changed[index]) {
            publishAppliedParam(p, kParamSpecs[index].id);
        }
    }
    if (batch.tailChanged && p.hostTail && p.hostTail->changed) {
        p.hostTail->changed(p.host);
    }
}

void applyParam(Plugin& p, clap_id id, double requested)
{
    ParamUpdateBatch batch;
    if (stageParamUpdate(p, id, requested, batch)) {
        commitParamUpdates(p, batch);
    }
}

void loadPublishedState(Plugin& p)
{
    ParamUpdateBatch batch;
    for (const auto& spec : kParamSpecs) {
        stageParamUpdate(
            p, spec.id, publishedParamValue(p, spec.id), batch);
    }
    commitParamUpdates(p, batch);
}

bool init(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (p->host && p->host->get_extension) {
        p->hostParams = static_cast<const clap_host_params_t*>(
            p->host->get_extension(p->host, CLAP_EXT_PARAMS));
        p->hostTail = static_cast<const clap_host_tail_t*>(
            p->host->get_extension(p->host, CLAP_EXT_TAIL));
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
    s3g::clap_gui::ParamEventKind kind,
    clap_id id, double value = 0.0)
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
    if (const auto* spec = paramSpec(id)) {
        value = clampParamValue(*spec, value);
        publishParam(p, id, value);
    }
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiParamGestureEnd(Plugin& p, clap_id id)
{
    (void)queueGuiParamEvent(
        p, s3g::clap_gui::ParamEventKind::GestureEnd, id);
}

void queuePreviewStrike(Plugin& p, uint32_t node)
{
    p.previewStrikeNode.store(
        static_cast<int32_t>(std::min<uint32_t>(node, kNodeCount - 1u)),
        std::memory_order_release);
    if (p.host && p.host->request_process) p.host->request_process(p.host);
}

void resetSelfExcitation(Plugin& p)
{
    constexpr uint64_t noRatchet = std::numeric_limits<uint64_t>::max();
    p.selfExcitationCountdown = 0u;
    p.selfExcitationStep = 0u;
    p.selfExcitationRandomState = 0x51f15e1du;
    p.selfRatchetCountdown.fill(noRatchet);
    p.selfRatchetSpacing.fill(0u);
    p.selfRatchetsRemaining.fill(0u);
    p.selfExcitationWasEnabled = false;
    p.sequencerPitchVoices.fill(SequencerPitchVoice {});
    p.sequencerNodeCursor = 0u;
    p.sequencerNodeRandomState = 0x4f1bbcdcu;
    p.sequencerNodeShuffleBag.reset();
}

void resetMidiLayer(Plugin& p)
{
    p.midiEnvelope = 0.0f;
    p.activeMidiVelocity = 1.0f;
    p.activeMidiNote = -1;
    p.midiNoteOrderCounter = 0u;
    p.heldMidiNoteCount.fill(0u);
    p.heldMidiVelocity.fill(0.0f);
    p.heldMidiNoteOrder.fill(0u);
    p.midiNodeCursor = 0u;
    p.midiNodeRandomState = 0x6d2b79f5u;
    p.midiNodeShuffleBag.reset();
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        p.midiNodeVoices[node] = {};
        p.midiNodeVoices[node].randomState =
            0x9e3779b9u ^ (0x85ebca6bu * (node + 1u));
    }
    p.network.clearTuningFrequency();
}

int32_t latestHeldMidiNote(const Plugin& p)
{
    int32_t selected = -1;
    uint64_t newest = 0u;
    for (uint32_t key = 0u; key < p.heldMidiNoteCount.size(); ++key) {
        if (p.heldMidiNoteCount[key] > 0u
            && (selected < 0 || p.heldMidiNoteOrder[key] > newest)) {
            selected = static_cast<int32_t>(key);
            newest = p.heldMidiNoteOrder[key];
        }
    }
    return selected;
}

void activateMidiNote(Plugin& p, uint32_t key, float velocity)
{
    key = std::min<uint32_t>(key, 127u);
    p.activeMidiNote = static_cast<int32_t>(key);
    p.lastMidiNote = static_cast<int32_t>(key);
    p.activeMidiVelocity = std::clamp(velocity, 0.0f, 1.0f);
    p.network.setTuningFrequency(midiNoteFrequency(
        p.activeMidiNote, p.midiTranspose));
}

float midiVelocityGain(const Plugin& p, float velocity)
{
    const float shaped = std::sqrt(std::clamp(velocity, 0.0f, 1.0f));
    return s3g::lerp(
        1.0f, shaped, p.midiVelocitySensitivity);
}

void strikeNode(Plugin& p, uint32_t node, float amplitude)
{
    if (node >= kNodeCount) return;
    if (p.waveguideParams.exciter == s3g::WaveguideExciter::Off) {
        p.network.strike(node, amplitude);
    } else {
        p.network.triggerExciter(node, amplitude);
    }
    p.nodeStrikeFlash[node] = std::max(
        p.nodeStrikeFlash[node],
        std::clamp(0.35f + 0.65f * std::sqrt(std::abs(amplitude)),
            0.0f, 1.0f));
}

void triggerSequencerPitch(
    Plugin& p, uint32_t node, int32_t key, float amplitude)
{
    if (node >= kNodeCount || key < 0) return;
    auto& voice = p.sequencerPitchVoices[node];
    voice.key = key;
    voice.phase = 0.0f;
    voice.frequencyHz = midiNoteFrequency(key, p.midiTranspose);
    voice.envelope = std::max(voice.envelope,
        std::clamp(std::abs(amplitude), 0.0f, 1.0f));
}

uint32_t availableMidiNodeMask(const Plugin& p)
{
    uint32_t mask = 0u;
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        const auto& voice = p.midiNodeVoices[node];
        if (voice.key < 0 || (!voice.held && voice.envelope < 0.000001f)) {
            mask |= 1u << node;
        }
    }
    return mask;
}

void polyMidiNoteOn(Plugin& p, int32_t key, float velocity)
{
    const uint32_t freeMask = availableMidiNodeMask(p);
    const uint32_t node = p.midiMode == 3u
        ? s3g::allocateShuffledMidiNode(
            freeMask, p.midiNodeRandomState, p.midiNodeShuffleBag)
        : s3g::allocateSequentialMidiNode(freeMask, p.midiNodeCursor);
    auto& voice = p.midiNodeVoices[node];
    voice.key = key;
    voice.velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice.envelope = 0.0f;
    voice.phase = 0.0f;
    voice.frequencyHz = midiNoteFrequency(key, p.midiTranspose);
    voice.filteredNoise = 0.0f;
    voice.percussiveAttackRemaining = std::max<uint32_t>(
        1u, static_cast<uint32_t>(std::llround(
            p.midiAttackMs * 0.001
                * std::max(1.0, p.sampleRate))));
    voice.order = ++p.midiNoteOrderCounter;
    voice.held = true;
    strikeNode(p, node,
        p.strikeGain * midiVelocityGain(p, velocity));
}

void midiNoteOn(Plugin& p, int32_t key, float velocity)
{
    if (key < 0 || key > 127 || velocity <= 0.0f) return;
    p.lastMidiNote = key;
    if (p.midiMode >= 2u) {
        polyMidiNoteOn(p, key, velocity);
        return;
    }
    const uint32_t index = static_cast<uint32_t>(key);
    if (p.heldMidiNoteCount[index]
        < std::numeric_limits<uint16_t>::max()) {
        ++p.heldMidiNoteCount[index];
    }
    p.heldMidiVelocity[index] = std::clamp(velocity, 0.0f, 1.0f);
    p.heldMidiNoteOrder[index] = ++p.midiNoteOrderCounter;
    activateMidiNote(p, index, p.heldMidiVelocity[index]);
    strikeNode(p, p.actuatorNode,
        p.strikeGain * midiVelocityGain(p, velocity));
}

void midiAllNotesOff(Plugin& p, bool immediate)
{
    p.heldMidiNoteCount.fill(0u);
    p.heldMidiNoteOrder.fill(0u);
    p.activeMidiNote = -1;
    if (immediate) p.midiEnvelope = 0.0f;
    for (auto& voice : p.midiNodeVoices) {
        voice.held = false;
        if (immediate) {
            voice.key = -1;
            voice.envelope = 0.0f;
            voice.percussiveAttackRemaining = 0u;
        }
    }
}

void midiNoteOff(Plugin& p, int32_t key, bool immediate)
{
    if (key < 0 || key > 127) {
        midiAllNotesOff(p, immediate);
        return;
    }
    if (p.midiMode >= 2u) {
        MidiNodeVoice* selected = nullptr;
        for (auto& voice : p.midiNodeVoices) {
            if (!voice.held || voice.key != key) continue;
            if (!selected || voice.order < selected->order) {
                selected = &voice;
            }
        }
        if (selected) {
            selected->held = false;
            if (immediate) {
                selected->key = -1;
                selected->envelope = 0.0f;
                selected->percussiveAttackRemaining = 0u;
            }
        }
        return;
    }
    const uint32_t index = static_cast<uint32_t>(key);
    if (p.heldMidiNoteCount[index] > 0u) {
        --p.heldMidiNoteCount[index];
    }
    if (p.heldMidiNoteCount[index] == 0u) {
        p.heldMidiNoteOrder[index] = 0u;
    }
    if (p.activeMidiNote != key || p.heldMidiNoteCount[index] > 0u) return;
    const int32_t fallback = latestHeldMidiNote(p);
    if (fallback >= 0) {
        const uint32_t fallbackIndex = static_cast<uint32_t>(fallback);
        activateMidiNote(
            p, fallbackIndex, p.heldMidiVelocity[fallbackIndex]);
    } else {
        p.activeMidiNote = -1;
        if (immediate) p.midiEnvelope = 0.0f;
    }
}

float envelopeCoefficient(float milliseconds, double sampleRate)
{
    const float samples = std::max(1.0f,
        milliseconds * 0.001f
            * static_cast<float>(std::max(1.0, sampleRate)));
    return 1.0f - std::exp(-6.90775527898f / samples);
}

float nextMidiVoiceNoise(MidiNodeVoice& voice)
{
    return static_cast<float>(
        (s3g::nextMidiNodeRandom(voice.randomState) >> 8u) & 0x00ffffffu)
        / 8388607.5f - 1.0f;
}

float polyMidiWaveform(Plugin& p, MidiNodeVoice& voice)
{
    voice.phase += voice.frequencyHz / static_cast<float>(p.sampleRate);
    voice.phase -= std::floor(voice.phase);
    const float angle = 2.0f * s3g::kPi * voice.phase;
    const float fundamental = std::sin(angle);
    const float character = p.waveguideParams.exciterCharacter;
    switch (p.waveguideParams.exciter) {
    case s3g::WaveguideExciter::Bow:
        return fundamental * (1.0f - 0.18f * character)
            + std::sin(3.0f * angle) * 0.18f * character;
    case s3g::WaveguideExciter::Reed: {
        const float harmonic = fundamental
            + std::sin(2.0f * angle) * (0.12f + 0.28f * character);
        return std::tanh(harmonic * (1.2f + 2.8f * character));
    }
    case s3g::WaveguideExciter::AirJet: {
        const float noise = nextMidiVoiceNoise(voice);
        voice.filteredNoise += (noise - voice.filteredNoise)
            * (0.025f + 0.20f * character);
        return fundamental * (0.88f - 0.20f * character)
            + voice.filteredNoise * (0.12f + 0.20f * character);
    }
    case s3g::WaveguideExciter::Off: {
        const float noise = nextMidiVoiceNoise(voice);
        voice.filteredNoise += (noise - voice.filteredNoise)
            * (0.08f + 0.30f * character);
        const float pitchedBody = fundamental
            + std::sin(2.0f * angle) * (0.16f + 0.18f * character)
            + std::sin(3.0f * angle) * (0.10f + 0.12f * character);
        return pitchedBody * (0.72f - 0.18f * character)
            + voice.filteredNoise * (0.18f + 0.24f * character);
    }
    default:
        return 0.0f;
    }
}

void renderMidiExcitation(Plugin& p, uint32_t offset, uint32_t frames)
{
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        std::fill(p.midiNodeActuatorScratch[node].begin() + offset,
            p.midiNodeActuatorScratch[node].begin() + offset + frames, 0.0f);
        std::fill(p.midiNodeGateScratch[node].begin() + offset,
            p.midiNodeGateScratch[node].begin() + offset + frames, 0.0f);
    }
    if (p.midiMode == 0u) {
        std::fill(p.midiGateScratch.begin() + offset,
            p.midiGateScratch.begin() + offset + frames, 1.0f);
        return;
    }
    if (p.midiMode >= 2u) {
        std::fill(p.midiGateScratch.begin() + offset,
            p.midiGateScratch.begin() + offset + frames, 0.0f);
        const float attack = envelopeCoefficient(
            p.midiAttackMs, p.sampleRate);
        const float release = envelopeCoefficient(
            p.midiReleaseMs, p.sampleRate);
        const float percussiveRelease = std::exp(
            -6.90775527898f
            / std::max(1.0f,
                p.midiReleaseMs * 0.001f
                    * static_cast<float>(p.sampleRate)));
        const bool percussive =
            p.waveguideParams.exciter == s3g::WaveguideExciter::Off;
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            for (uint32_t node = 0u; node < kNodeCount; ++node) {
                auto& voice = p.midiNodeVoices[node];
                if (voice.key < 0) continue;
                if (percussive) {
                    if (voice.percussiveAttackRemaining > 0u) {
                        voice.envelope += (1.0f - voice.envelope)
                            / static_cast<float>(
                                voice.percussiveAttackRemaining);
                        --voice.percussiveAttackRemaining;
                    } else {
                        voice.envelope *= percussiveRelease;
                    }
                    if (voice.envelope < 0.000001f
                        && voice.percussiveAttackRemaining == 0u) {
                        voice.envelope = 0.0f;
                        if (!voice.held) voice.key = -1;
                        continue;
                    }
                    const float gate = std::clamp(
                        voice.envelope
                            * midiVelocityGain(p, voice.velocity),
                        0.0f, 1.0f);
                    p.midiNodeActuatorScratch[node][offset + frame] =
                        polyMidiWaveform(p, voice) * gate * 0.055f;
                    continue;
                }
                const float target = voice.held ? 1.0f : 0.0f;
                voice.envelope += (target - voice.envelope)
                    * (voice.held ? attack : release);
                if (!voice.held && voice.envelope < 0.000001f) {
                    voice.envelope = 0.0f;
                    voice.key = -1;
                    continue;
                }
                const float gate = std::clamp(
                    voice.envelope
                        * midiVelocityGain(p, voice.velocity),
                    0.0f, 1.0f);
                p.midiNodeGateScratch[node][offset + frame] = gate;
                p.midiNodeActuatorScratch[node][offset + frame] =
                    polyMidiWaveform(p, voice)
                    * p.waveguideParams.sustainedExcitation * gate * 0.035f;
            }
        }
        return;
    }
    const float coefficient = p.activeMidiNote >= 0
        ? envelopeCoefficient(p.midiAttackMs, p.sampleRate)
        : envelopeCoefficient(p.midiReleaseMs, p.sampleRate);
    const float target = p.activeMidiNote >= 0 ? 1.0f : 0.0f;
    const float velocity = midiVelocityGain(p, p.activeMidiVelocity);
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        p.midiEnvelope += (target - p.midiEnvelope) * coefficient;
        if (target == 0.0f && p.midiEnvelope < 0.000001f) {
            p.midiEnvelope = 0.0f;
        }
        p.midiGateScratch[offset + frame] =
            std::clamp(p.midiEnvelope * velocity, 0.0f, 1.0f);
    }
}

void renderSequencerPitchExcitation(
    Plugin& p, uint32_t offset, uint32_t frames)
{
    const float character = p.waveguideParams.exciterCharacter;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            auto& voice = p.sequencerPitchVoices[node];
            if (voice.envelope < 0.000001f) {
                voice.envelope = 0.0f;
                continue;
            }
            const float angle = 2.0f * s3g::kPi * voice.phase;
            const float tone = std::sin(angle)
                + std::sin(2.0f * angle) * (0.08f + 0.14f * character);
            p.midiNodeActuatorScratch[node][offset + frame] +=
                tone * voice.envelope * 0.060f;
            voice.phase +=
                voice.frequencyHz / static_cast<float>(p.sampleRate);
            voice.phase -= std::floor(voice.phase);
            voice.envelope *= p.sequencerPitchDecay;
        }
    }
}

uint32_t nextSelfExcitationRandom(Plugin& p)
{
    auto& state = p.selfExcitationRandomState;
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float nextSelfExcitationUnit(Plugin& p)
{
    return static_cast<float>(
        (nextSelfExcitationRandom(p) >> 8u) & 0x00ffffffu)
        / 16777215.0f;
}

uint64_t nextSelfExcitationInterval(Plugin& p)
{
    const double samples = p.sampleRate
        / std::max(0.05, static_cast<double>(p.selfExcitationRate));
    return std::max<uint64_t>(
        1u, static_cast<uint64_t>(std::llround(samples)));
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
    uint32_t, uint32_t maxFrames)
{
    auto* p = self(plugin);
    p->sampleRate = std::max(1.0, sampleRate);
    p->sequencerPitchDecay = std::exp(
        -6.90775527898f
        / std::max(1.0f, static_cast<float>(p->sampleRate) * 0.180f));
    p->maxFrames = std::max<uint32_t>(1u, maxFrames);
    p->actuatorScratch.assign(p->maxFrames, 0.0f);
    p->midiGateScratch.assign(p->maxFrames, 1.0f);
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        p->midiNodeActuatorScratch[node].assign(p->maxFrames, 0.0f);
        p->midiNodeGateScratch[node].assign(p->maxFrames, 0.0f);
        p->midiNodeActuatorPointers[node] =
            p->midiNodeActuatorScratch[node].data();
        p->midiNodeGatePointers[node] =
            p->midiNodeGateScratch[node].data();
        p->midiNodeActivity[node].store(0.0f, std::memory_order_relaxed);
        p->nodeStrikeActivity[node].store(0.0f, std::memory_order_relaxed);
        p->midiNodeKey[node].store(-1, std::memory_order_relaxed);
    }
    for (uint32_t channel = 0u; channel < kOutputChannels; ++channel) {
        p->outputScratch[channel].assign(p->maxFrames, 0.0f);
        p->outputPointers[channel] = p->outputScratch[channel].data();
    }
    loadPublishedState(*p);
    p->network.configureCube(p->sizeMetres);
    p->network.setParams(p->waveguideParams);
    p->network.prepare(p->sampleRate, 0.5f);
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        p->network.setNodeDirectivity(
            node, p->nodeDirectivity[node], true);
    }
    resetSelfExcitation(*p);
    resetMidiLayer(*p);
    p->nodeStrikeFlash.fill(0.0f);
    p->active = true;
    return true;
}

void deactivate(const clap_plugin_t* plugin)
{
    self(plugin)->active = false;
}

bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    p->network.reset();
    resetSelfExcitation(*p);
    resetMidiLayer(*p);
    p->nodeStrikeFlash.fill(0.0f);
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        p->midiNodeActivity[node].store(0.0f, std::memory_order_relaxed);
        p->nodeStrikeActivity[node].store(0.0f, std::memory_order_relaxed);
        p->midiNodeKey[node].store(-1, std::memory_order_relaxed);
    }
}

bool pushGuiParamEvent(const clap_output_events_t* out,
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
    event.header.type =
        pending.kind == s3g::clap_gui::ParamEventKind::GestureBegin
        ? CLAP_EVENT_PARAM_GESTURE_BEGIN
        : CLAP_EVENT_PARAM_GESTURE_END;
    event.header.flags = CLAP_EVENT_IS_LIVE;
    event.param_id = pending.paramId;
    return out->try_push(out, &event.header);
}

void serviceGuiParamEvents(Plugin& p, const clap_output_events_t* out)
{
    s3g::clap_gui::ParamEvent pending {};
    ParamUpdateBatch batch;
    while (p.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(out, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value) {
            stageParamUpdate(
                p, pending.paramId, pending.value, batch);
        }
        p.guiParamEvents.pop();
    }
    commitParamUpdates(p, batch);
}

PerformanceEventBatch readInputEvents(Plugin& p,
    const clap_input_events_t* inputEvents, uint32_t frames)
{
    PerformanceEventBatch batch;
    ParamUpdateBatch paramBatch;
    const auto append = [&](PerformanceEventKind kind, uint32_t time,
                            int32_t key, float amplitude, uint32_t node) {
        if (batch.count >= PerformanceEventBatch::kCapacity) return;
        auto& added = batch.events[batch.count++];
        added.kind = kind;
        added.time = frames > 0u
            ? std::min<uint32_t>(time, frames - 1u) : 0u;
        added.node = node;
        added.key = key;
        added.amplitude = amplitude;
    };
    if (inputEvents) {
        const uint32_t eventCount = inputEvents->size(inputEvents);
        for (uint32_t index = 0u; index < eventCount; ++index) {
            const auto* event = inputEvents->get(inputEvents, index);
            if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID) {
                continue;
            }
            if (event->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* param =
                    reinterpret_cast<const clap_event_param_value_t*>(event);
                stageParamUpdate(
                    p, param->param_id, param->value, paramBatch);
                continue;
            }
            if (event->type == CLAP_EVENT_NOTE_ON
                || event->type == CLAP_EVENT_NOTE_OFF
                || event->type == CLAP_EVENT_NOTE_CHOKE
                || event->type == CLAP_EVENT_NOTE_END) {
                const auto* note =
                    reinterpret_cast<const clap_event_note_t*>(event);
                if (note->key < 0 || note->key > 127) continue;
                const float velocity = std::clamp(
                    static_cast<float>(note->velocity), 0.0f, 1.0f);
                const bool noteOn = event->type == CLAP_EVENT_NOTE_ON
                    && velocity > 0.0f;
                if (p.midiMode == 0u) {
                    if (noteOn) {
                        append(PerformanceEventKind::Strike, event->time,
                            note->key, velocity * p.strikeGain,
                            static_cast<uint32_t>(note->key) % kNodeCount);
                    }
                } else if (noteOn) {
                    append(PerformanceEventKind::MidiNoteOn, event->time,
                        note->key, velocity, 0u);
                } else {
                    const bool immediate =
                        event->type == CLAP_EVENT_NOTE_CHOKE
                        || event->type == CLAP_EVENT_NOTE_END;
                    append(PerformanceEventKind::MidiNoteOff, event->time,
                        note->key, immediate ? 1.0f : 0.0f, 0u);
                }
                continue;
            }
            if (event->type != CLAP_EVENT_MIDI) continue;
            const auto* midi =
                reinterpret_cast<const clap_event_midi_t*>(event);
            const uint8_t status = midi->data[0] & 0xf0u;
            const int32_t key = midi->data[1] & 0x7fu;
            const float velocity = static_cast<float>(midi->data[2] & 0x7fu)
                / 127.0f;
            const bool noteOn = status == 0x90u && velocity > 0.0f;
            const bool noteOff = status == 0x80u
                || (status == 0x90u && velocity <= 0.0f);
            if (p.midiMode == 0u) {
                if (noteOn) {
                    append(PerformanceEventKind::Strike, event->time,
                        key, velocity * p.strikeGain,
                        static_cast<uint32_t>(key) % kNodeCount);
                }
            } else if (noteOn) {
                append(PerformanceEventKind::MidiNoteOn, event->time,
                    key, velocity, 0u);
            } else if (noteOff) {
                append(PerformanceEventKind::MidiNoteOff, event->time,
                    key, 0.0f, 0u);
            } else if (status == 0xb0u
                && (midi->data[1] == 120u || midi->data[1] == 123u)) {
                append(PerformanceEventKind::MidiAllNotesOff, event->time,
                    -1, midi->data[1] == 120u ? 1.0f : 0.0f, 0u);
            }
        }
    }
    commitParamUpdates(p, paramBatch);
    const int32_t preview =
        p.previewStrikeNode.exchange(-1, std::memory_order_acq_rel);
    if (preview >= 0) {
        append(PerformanceEventKind::Strike, 0u, -1, p.strikeGain,
            static_cast<uint32_t>(preview));
    }
    return batch;
}

void appendSelfExcitation(
    Plugin& p, PerformanceEventBatch& batch, uint32_t frames)
{
    constexpr uint64_t noRatchet = std::numeric_limits<uint64_t>::max();
    if (frames == 0u) return;
    const bool enabled = p.selfExcitationGain > 0.000001f;
    if (!enabled) {
        p.selfRatchetCountdown.fill(noRatchet);
        p.selfRatchetsRemaining.fill(0u);
        p.selfExcitationWasEnabled = false;
        return;
    }
    if (!p.selfExcitationWasEnabled) {
        p.selfExcitationCountdown = 0u;
        p.selfRatchetCountdown.fill(noRatchet);
        p.selfRatchetsRemaining.fill(0u);
    }
    p.selfExcitationWasEnabled = true;

    while (true) {
        uint64_t nextEvent = p.selfExcitationCountdown;
        for (const uint64_t countdown : p.selfRatchetCountdown) {
            nextEvent = std::min(nextEvent, countdown);
        }
        if (nextEvent >= frames) break;

        if (p.selfExcitationCountdown == nextEvent) {
            const uint32_t steps =
                std::max<uint32_t>(1u, p.euclideanSteps);
            const uint64_t interval = nextSelfExcitationInterval(p);
            for (uint32_t node = 0u; node < kNodeCount; ++node) {
                const uint32_t ratchets =
                    s3g::euclideanRhythmRatchetCount(
                        p.selfExcitationStep, p.euclideanPulses[node],
                        steps, p.euclideanRotations[node]);
                p.selfRatchetsRemaining[node] = ratchets;
                p.selfRatchetCountdown[node] =
                    ratchets > 0u ? nextEvent : noRatchet;
                p.selfRatchetSpacing[node] = ratchets > 0u
                    ? std::max<uint64_t>(1u, interval / ratchets) : 0u;
            }
            p.selfExcitationStep = (p.selfExcitationStep + 1u) % steps;
            p.selfExcitationCountdown += interval;
        }

        uint32_t simultaneousHits = 0u;
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            if (p.selfRatchetCountdown[node] == nextEvent
                && p.selfRatchetsRemaining[node] > 0u) {
                ++simultaneousHits;
            }
        }
        const float chordScale = 1.0f / std::sqrt(static_cast<float>(
            std::max<uint32_t>(1u, simultaneousHits)));
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            if (p.selfRatchetCountdown[node] != nextEvent
                || p.selfRatchetsRemaining[node] == 0u) {
                continue;
            }
            if (batch.count < PerformanceEventBatch::kCapacity) {
                auto& added = batch.events[batch.count++];
                added.kind = PerformanceEventKind::SequencedStrike;
                added.time = static_cast<uint32_t>(nextEvent);
                added.node = s3g::routeSequencerNode(
                    p.midiMode, node,
                    p.sequencerNodeCursor,
                    p.sequencerNodeRandomState,
                    p.sequencerNodeShuffleBag);
                added.amplitude = p.selfExcitationGain * chordScale
                    * (0.82f + 0.18f * nextSelfExcitationUnit(p));
            }
            --p.selfRatchetsRemaining[node];
            if (p.selfRatchetsRemaining[node] > 0u) {
                p.selfRatchetCountdown[node] +=
                    p.selfRatchetSpacing[node];
            } else {
                p.selfRatchetCountdown[node] = noRatchet;
            }
        }
    }
    p.selfExcitationCountdown -= frames;
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        if (p.selfRatchetCountdown[node] != noRatchet) {
            p.selfRatchetCountdown[node] -= frames;
        }
    }
}

void readParamEvents(Plugin& p, const clap_input_events_t* inputEvents)
{
    if (!inputEvents) return;
    ParamUpdateBatch batch;
    const uint32_t eventCount = inputEvents->size(inputEvents);
    for (uint32_t index = 0u; index < eventCount; ++index) {
        const auto* event = inputEvents->get(inputEvents, index);
        if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID
            || event->type != CLAP_EVENT_PARAM_VALUE) {
            continue;
        }
        const auto* param =
            reinterpret_cast<const clap_event_param_value_t*>(event);
        stageParamUpdate(p, param->param_id, param->value, batch);
    }
    commitParamUpdates(p, batch);
}

float inputSample(const clap_audio_buffer_t* input, uint32_t frame)
{
    if (!input || input->channel_count == 0u) return 0.0f;
    if (input->data32 && input->data32[0]) return input->data32[0][frame];
    if (input->data64 && input->data64[0]) {
        return static_cast<float>(input->data64[0][frame]);
    }
    return 0.0f;
}

void publishMeters(Plugin& p)
{
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        p.nodeEnergy[node].store(
            p.network.nodeEnergy(node), std::memory_order_relaxed);
        float midiActivity = 0.0f;
        int32_t midiKey = -1;
        if (p.midiMode == 1u && node == p.actuatorNode) {
            midiActivity = p.midiEnvelope
                * midiVelocityGain(p, p.activeMidiVelocity);
            midiKey = p.activeMidiNote;
        } else if (p.midiMode >= 2u) {
            const auto& voice = p.midiNodeVoices[node];
            midiActivity = voice.envelope
                * midiVelocityGain(p, voice.velocity);
            midiKey = voice.key;
        }
        p.midiNodeActivity[node].store(
            std::clamp(midiActivity, 0.0f, 1.0f),
            std::memory_order_relaxed);
        p.nodeStrikeActivity[node].store(
            p.nodeStrikeFlash[node], std::memory_order_relaxed);
        p.midiNodeKey[node].store(midiKey, std::memory_order_relaxed);
    }
    for (uint32_t edge = 0u; edge < kEdgeCount; ++edge) {
        p.edgeEnergy[edge].store(
            p.network.edgeEnergy(edge), std::memory_order_relaxed);
    }
    p.outputPeak.store(
        p.network.outputPeak(), std::memory_order_relaxed);
    p.guardGain.store(
        p.network.guardGain(), std::memory_order_relaxed);
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    if (!processData || processData->audio_outputs_count == 0u) {
        return CLAP_PROCESS_CONTINUE;
    }
    auto* p = self(plugin);
    serviceGuiParamEvents(*p, processData->out_events);
    if (processData->frames_count > p->maxFrames) return CLAP_PROCESS_ERROR;
    const uint32_t frames = processData->frames_count;
    auto& output = processData->audio_outputs[0];
    if ((!output.data32 && !output.data64) || output.channel_count == 0u) {
        return CLAP_PROCESS_ERROR;
    }
    PerformanceEventBatch events =
        readInputEvents(*p, processData->in_events, frames);
    appendSelfExcitation(*p, events, frames);
    sortPerformanceEvents(events);
    const clap_audio_buffer_t* input =
        processData->audio_inputs_count > 0u
        ? &processData->audio_inputs[0] : nullptr;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        p->actuatorScratch[frame] =
            inputSample(input, frame) * p->actuatorGain;
    }
    uint32_t offset = 0u;
    uint32_t eventIndex = 0u;
    const auto applyEvent = [&](const PerformanceEvent& event) {
        switch (event.kind) {
        case PerformanceEventKind::Strike:
            if (event.key >= 0) p->lastMidiNote = event.key;
            strikeNode(*p, event.node, event.amplitude);
            break;
        case PerformanceEventKind::SequencedStrike:
            triggerSequencerPitch(*p, event.node,
                s3g::dispersedScaleMidiNote(
                    p->lastMidiNote, event.node,
                    p->sequencerNoteCount, p->sequencerScale),
                event.amplitude);
            strikeNode(*p, event.node, event.amplitude);
            break;
        case PerformanceEventKind::MidiNoteOn:
            midiNoteOn(*p, event.key, event.amplitude);
            break;
        case PerformanceEventKind::MidiNoteOff:
            midiNoteOff(*p, event.key, event.amplitude > 0.5f);
            break;
        case PerformanceEventKind::MidiAllNotesOff:
            midiAllNotesOff(*p, event.amplitude > 0.5f);
            break;
        }
    };
    while (offset < frames) {
        while (eventIndex < events.count
            && events.events[eventIndex].time <= offset) {
            applyEvent(events.events[eventIndex++]);
        }
        uint32_t end = frames;
        if (eventIndex < events.count) {
            end = std::min(end, events.events[eventIndex].time);
        }
        if (end <= offset) continue;
        renderMidiExcitation(*p, offset, end - offset);
        renderSequencerPitchExcitation(*p, offset, end - offset);
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            p->midiNodeActuatorPointers[node] =
                p->midiNodeActuatorScratch[node].data() + offset;
            p->midiNodeGatePointers[node] =
                p->midiNodeGateScratch[node].data() + offset;
        }
        for (uint32_t channel = 0u;
            channel < kOutputChannels; ++channel) {
            p->outputPointers[channel] =
                p->outputScratch[channel].data() + offset;
        }
        p->network.process(
            p->actuatorScratch.data() + offset,
            p->outputPointers.data(),
            kOutputChannels,
            end - offset,
            p->actuatorNode,
            p->midiMode == 1u
                ? p->midiGateScratch.data() + offset : nullptr,
            p->midiNodeActuatorPointers.data(),
            p->midiMode >= 2u
                ? p->midiNodeGatePointers.data() : nullptr);
        offset = end;
    }
    if (frames == 0u) {
        while (eventIndex < events.count) {
            applyEvent(events.events[eventIndex++]);
        }
    }

    const uint32_t copiedChannels =
        std::min<uint32_t>(output.channel_count, kOutputChannels);
    for (uint32_t channel = 0u; channel < copiedChannels; ++channel) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float value = p->outputScratch[channel][frame];
            if (output.data32 && output.data32[channel]) {
                output.data32[channel][frame] = value;
            }
            if (output.data64 && output.data64[channel]) {
                output.data64[channel][frame] = static_cast<double>(value);
            }
        }
    }
    for (uint32_t channel = kOutputChannels;
        channel < output.channel_count; ++channel) {
        if (output.data32 && output.data32[channel]) {
            std::fill(output.data32[channel],
                output.data32[channel] + frames, 0.0f);
        }
        if (output.data64 && output.data64[channel]) {
            std::fill(output.data64[channel],
                output.data64[channel] + frames, 0.0);
        }
    }
#if defined(__APPLE__)
    if (p->guiVisible.load(std::memory_order_acquire)) {
        if (p->guiMeterCountdown <= frames) {
            publishMeters(*p);
            p->guiMeterCountdown = std::max<uint32_t>(
                1u, static_cast<uint32_t>(p->sampleRate / 20.0));
        } else {
            p->guiMeterCountdown -= frames;
        }
    } else {
        p->guiMeterCountdown = 0u;
    }
#endif
    const float flashDecay = std::exp(
        -static_cast<float>(frames)
        / std::max(1.0f, static_cast<float>(p->sampleRate) * 0.22f));
    for (float& flash : p->nodeStrikeFlash) flash *= flashDecay;
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t audioPortsCount(const clap_plugin_t*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_audio_port_info_t* info)
{
    if (!info || index != 0u) return false;
    *info = {};
    info->id = isInput ? 10u : 20u;
    std::strncpy(info->name,
        isInput ? "Waveguide Actuator In"
                : "3OA ACN/SN3D Waveguide Field",
        sizeof(info->name) - 1u);
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = isInput ? kInputChannels : kOutputChannels;
    info->port_type = isInput ? CLAP_PORT_MONO : CLAP_PORT_AMBISONIC;
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

bool notePortsGet(const clap_plugin_t*, uint32_t index,
    bool isInput, clap_note_port_info_t* info)
{
    if (!info || !isInput || index != 0u) return false;
    *info = {};
    info->id = 30u;
    info->supported_dialects =
        CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Waveguide MIDI / Strike In",
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
    if (!info || index >= kParamSpecs.size()) return false;
    const auto& spec = kParamSpecs[index];
    *info = {};
    info->id = spec.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (spec.stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    std::strncpy(info->name, spec.name, sizeof(info->name) - 1u);
    std::strncpy(info->module, spec.module, sizeof(info->module) - 1u);
    info->min_value = spec.minimum;
    info->max_value = spec.maximum;
    info->default_value = spec.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin,
    clap_id id, double* value)
{
    if (!value || !paramSpec(id)) return false;
    *value = publishedParamValue(*self(plugin), id);
    return true;
}

bool formatParamValue(clap_id id, double value,
    char* display, uint32_t size)
{
    if (!display || size == 0u || !paramSpec(id)) return false;
    switch (id) {
    case kOrderParamId: {
        const double order = std::round(value);
        std::snprintf(display, size, "%.0fOA / %.0fch",
            order, (order + 1.0) * (order + 1.0));
        break;
    }
    case kSpeedParamId:
        std::snprintf(display, size, "%.0f m/s", value);
        break;
    case kDecayParamId:
        std::snprintf(display, size,
            value < 1.0 ? "%.0f ms" : "%.2f s",
            value < 1.0 ? value * 1000.0 : value);
        break;
    case kSizeParamId:
        std::snprintf(display, size,
            value < 0.1 ? "%.1f cm" : "%.2f m",
            value < 0.1 ? value * 100.0 : value);
        break;
    case kActuatorNodeParamId:
        std::snprintf(display, size, "Node %.0f", value);
        break;
    case kActuatorGainParamId:
    case kStrikeGainParamId:
        std::snprintf(display, size, "%.2fx", value);
        break;
    case kSelfExcitationRateParamId:
        std::snprintf(display, size, "%.2f Hz", value);
        break;
    case kEuclideanStepsParamId:
        std::snprintf(display, size, "%.0f steps", value);
        break;
    case kExciterTypeParamId: {
        constexpr const char* names[4] {
            "Percussive", "Bow", "Reed", "Air Jet"
        };
        const uint32_t index = std::min<uint32_t>(
            3u, static_cast<uint32_t>(std::lround(value)));
        std::snprintf(display, size, "%s", names[index]);
        break;
    }
    case kMidiModeParamId:
    {
        constexpr const char* names[4] {
            "Strike", "Mono Note", "Poly Seq", "Poly Random"
        };
        const uint32_t index = std::min<uint32_t>(
            3u, static_cast<uint32_t>(std::lround(value)));
        std::snprintf(display, size, "%s", names[index]);
        break;
    }
    case kMidiTransposeParamId:
        std::snprintf(display, size, "%+.0f st", value);
        break;
    case kSequencerScaleParamId:
    {
        constexpr const char* names[8] {
            "Major", "Natural Minor", "Dorian", "Mixolydian",
            "Major Pentatonic", "Minor Pentatonic", "Harmonic Minor",
            "Chromatic"
        };
        const uint32_t index = std::min<uint32_t>(
            7u, static_cast<uint32_t>(std::lround(value)));
        std::snprintf(display, size, "%s", names[index]);
        break;
    }
    case kSequencerNoteCountParamId:
        std::snprintf(display, size, "%.0f %s", value,
            std::lround(value) == 1l ? "note" : "notes");
        break;
    case kMidiAttackParamId:
    case kMidiReleaseParamId:
        std::snprintf(display, size,
            value < 1000.0 ? "%.1f ms" : "%.2f s",
            value < 1000.0 ? value : value * 0.001);
        break;
    case kOutputGainParamId:
        std::snprintf(display, size, "%+.1f dB", value);
        break;
    default:
        if (isEuclideanPulsesParam(id)) {
            std::snprintf(display, size, "%.0f hits", value);
        } else if (isEuclideanRotationParam(id)) {
            std::snprintf(display, size, "+%.0f", value);
        } else {
            std::snprintf(display, size, "%.0f%%", value * 100.0);
        }
        break;
    }
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id,
    double value, char* display, uint32_t size)
{
    return formatParamValue(id, value, display, size);
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id,
    const char* display, double* value)
{
    const auto* spec = paramSpec(id);
    if (!display || !value || !spec) return false;
    if (id == kExciterTypeParamId) {
        constexpr const char* names[4] {
            "Percussive", "Bow", "Reed", "Air Jet"
        };
        if (std::strcmp(display, "Off") == 0) {
            *value = 0.0;
            return true;
        }
        for (uint32_t index = 0u; index < 4u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = index;
                return true;
            }
        }
    }
    if (id == kMidiModeParamId) {
        if (std::strcmp(display, "Strike") == 0) {
            *value = 0.0;
            return true;
        }
        if (std::strcmp(display, "Mono Note") == 0
            || std::strcmp(display, "Mono") == 0) {
            *value = 1.0;
            return true;
        }
        if (std::strcmp(display, "Poly Seq") == 0
            || std::strcmp(display, "Poly") == 0) {
            *value = 2.0;
            return true;
        }
        if (std::strcmp(display, "Poly Random") == 0
            || std::strcmp(display, "Random") == 0) {
            *value = 3.0;
            return true;
        }
    }
    if (id == kSequencerScaleParamId) {
        constexpr const char* names[8] {
            "Major", "Natural Minor", "Dorian", "Mixolydian",
            "Major Pentatonic", "Minor Pentatonic", "Harmonic Minor",
            "Chromatic"
        };
        for (uint32_t index = 0u; index < 8u; ++index) {
            if (std::strcmp(display, names[index]) == 0) {
                *value = index;
                return true;
            }
        }
    }
    const char* numeric = display;
    while (*numeric != '\0'
        && !std::isdigit(static_cast<unsigned char>(*numeric))
        && *numeric != '+' && *numeric != '-' && *numeric != '.') {
        ++numeric;
    }
    char* end = nullptr;
    double parsed = std::strtod(numeric, &end);
    if (end == numeric || !std::isfinite(parsed)) return false;
    if (id == kAbsorptionParamId || id == kNonlinearityParamId
        || id == kRadiationParamId || id == kSelfExcitationGainParamId
        || id == kSustainedExcitationParamId
        || id == kExciterCharacterParamId || id == kDispersionParamId
        || id == kMidiVelocityParamId || isNodeDirectivityParam(id)) {
        if (std::strchr(display, '%')) parsed *= 0.01;
    }
    if (id == kDecayParamId && std::strstr(display, "ms")) {
        parsed *= 0.001;
    }
    if ((id == kMidiAttackParamId || id == kMidiReleaseParamId)
        && std::strstr(display, " s") && !std::strstr(display, "ms")) {
        parsed *= 1000.0;
    }
    if (id == kSizeParamId && std::strstr(display, "cm")) {
        parsed *= 0.01;
    }
    *value = clampParamValue(*spec, parsed);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* in, const clap_output_events_t* out)
{
    auto* p = self(plugin);
    readParamEvents(*p, in);
    serviceGuiParamEvents(*p, out);
}

const clap_plugin_params_t paramsExt {
    paramsCount,
    paramsGetInfo,
    paramsGetValue,
    paramsValueToText,
    paramsTextToValue,
    paramsFlush
};

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    SavedState state {};
    const auto* p = self(plugin);
    for (const auto& spec : kParamSpecs) {
        state.values[paramIndex(spec.id)] =
            publishedParamValue(*p, spec.id);
    }
    return s3g::clap_state::writeAll(stream, &state, sizeof(state));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    uint32_t version = 0u;
    if (!s3g::clap_state::readAll(stream, &version, sizeof(version))) {
        return false;
    }
    SavedState state {};
    if (version == kStateVersion) {
        state.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&state) + sizeof(version),
                sizeof(state) - sizeof(version))) {
            return false;
        }
    } else if (version == kLegacyStateVersionV6) {
        LegacySavedStateV6 legacy {};
        legacy.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        for (const auto& spec : kParamSpecs) {
            state.values[paramIndex(spec.id)] = spec.defaultValue;
        }
        std::copy(legacy.values.begin(), legacy.values.end(),
            state.values.begin());
    } else if (version == kLegacyStateVersionV5) {
        LegacySavedStateV5 legacy {};
        legacy.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        for (const auto& spec : kParamSpecs) {
            state.values[paramIndex(spec.id)] = spec.defaultValue;
        }
        std::copy(legacy.values.begin(), legacy.values.end(),
            state.values.begin());
    } else if (version == kLegacyStateVersionV4) {
        LegacySavedStateV4 legacy {};
        legacy.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        for (const auto& spec : kParamSpecs) {
            state.values[paramIndex(spec.id)] = spec.defaultValue;
        }
        std::copy(legacy.values.begin(), legacy.values.end(),
            state.values.begin());
    } else if (version == kLegacyStateVersionV3) {
        LegacySavedStateV3 legacy {};
        legacy.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        for (const auto& spec : kParamSpecs) {
            state.values[paramIndex(spec.id)] = spec.defaultValue;
        }
        std::copy(legacy.values.begin(), legacy.values.end(),
            state.values.begin());
    } else if (version == kLegacyStateVersionV2) {
        LegacySavedStateV2 legacy {};
        legacy.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        for (const auto& spec : kParamSpecs) {
            state.values[paramIndex(spec.id)] = spec.defaultValue;
        }
        std::copy(legacy.values.begin(), legacy.values.end(),
            state.values.begin());
    } else if (version == kLegacyStateVersionV1) {
        LegacySavedStateV1 legacy {};
        legacy.version = version;
        if (!s3g::clap_state::readAll(stream,
                reinterpret_cast<uint8_t*>(&legacy) + sizeof(version),
                sizeof(legacy) - sizeof(version))) {
            return false;
        }
        for (const auto& spec : kParamSpecs) {
            state.values[paramIndex(spec.id)] = spec.defaultValue;
        }
        std::copy(legacy.values.begin(), legacy.values.end(),
            state.values.begin());
        // Version-one projects predate autonomous excitation. Keep their
        // previous silence unless the user explicitly turns the new source on.
        state.values[paramIndex(kSelfExcitationGainParamId)] = 0.0;
    } else {
        return false;
    }
    auto* p = self(plugin);
    for (const auto& spec : kParamSpecs) {
        applyParam(*p, spec.id, state.values[paramIndex(spec.id)]);
    }
    resetSelfExcitation(*p);
    resetMidiLayer(*p);
    return true;
}

const clap_plugin_state_t stateExt { stateSave, stateLoad };

uint32_t tailGet(const clap_plugin_t* plugin)
{
    const auto* p = self(plugin);
    const bool hasEuclideanPulses = std::any_of(
        p->euclideanPulses.begin(), p->euclideanPulses.end(),
        [](uint32_t pulses) { return pulses > 0u; });
    if (p->selfExcitationGain > 0.000001f && hasEuclideanPulses) {
        return 0xffffffffu;
    }
    if (p->midiMode == 0u
        && p->waveguideParams.exciter != s3g::WaveguideExciter::Off
        && p->waveguideParams.sustainedExcitation > 0.000001f) {
        return 0xffffffffu;
    }
    const double seconds = std::min(
        90.0, std::max(0.05,
            static_cast<double>(p->waveguideParams.decaySeconds) * 1.15
            + 16.0 * (2.0 * p->sizeMetres)
                / p->waveguideParams.propagationSpeed
            + (p->midiMode != 0u
                ? static_cast<double>(p->midiReleaseMs) * 0.001 : 0.0)));
    return static_cast<uint32_t>(std::ceil(seconds * p->sampleRate));
}

const clap_plugin_tail_t tailExt { tailGet };

} // namespace

#if defined(__APPLE__)

namespace {

constexpr clap_id kFactoryPresetMenuId = 0x7ffffff0u;

NSRect fieldPanelRect()
{
    return NSMakeRect(16.0, 50.0, 560.0, 614.0);
}

NSRect outputPanelRect()
{
    return NSMakeRect(594.0, 50.0, 310.0, 116.0);
}

NSRect mediumPanelRect()
{
    return NSMakeRect(594.0, 180.0, 310.0, 230.0);
}

NSRect excitationPanelRect()
{
    return NSMakeRect(594.0, 424.0, 310.0, 240.0);
}

CGFloat rowY(NSRect panel, uint32_t row)
{
    return panel.origin.y
        + s3g::gui_layout::kStandardMetrics.firstRowOffset
        + static_cast<CGFloat>(row)
            * s3g::gui_layout::kStandardMetrics.rowPitch;
}

NSRect rowHitRect(NSRect panel, uint32_t row)
{
    return NSMakeRect(
        panel.origin.x + 8.0,
        rowY(panel, row) - 8.0,
        panel.size.width - 16.0,
        s3g::gui_layout::kStandardMetrics.hitHeight);
}

NSRect menuBoxRect(NSRect panel, uint32_t row)
{
    return NSMakeRect(
        panel.origin.x + 102.0, rowY(panel, row) - 1.0,
        panel.size.width - 116.0, 15.0);
}

NSRect fieldInteractionRect()
{
    const NSRect panel = fieldPanelRect();
    return NSMakeRect(
        panel.origin.x + 8.0, panel.origin.y + 28.0,
        panel.size.width - 16.0, panel.size.height - 66.0);
}

NSRect zoomButtonRect(uint32_t index)
{
    const NSRect panel = fieldPanelRect();
    const NSRect firstCamera =
        s3g::clap_gui::topologyProcessorCameraButtonRect(panel, 0u);
    constexpr CGFloat width = 22.0;
    constexpr CGFloat gap = 4.0;
    constexpr CGFloat cameraGap = 12.0;
    return NSMakeRect(
        firstCamera.origin.x - cameraGap - 2.0 * width - gap
            + static_cast<CGFloat>(index) * (width + gap),
        panel.origin.y + 3.0, width, 15.0);
}

NSRect excitationPageButtonRect(uint32_t index)
{
    const NSRect panel = excitationPanelRect();
    constexpr CGFloat width = 48.0;
    constexpr CGFloat gap = 4.0;
    constexpr CGFloat right = 8.0;
    constexpr CGFloat total = 3.0 * width + 2.0 * gap;
    return NSMakeRect(
        NSMaxX(panel) - right - total
            + static_cast<CGFloat>(index) * (width + gap),
        panel.origin.y + 3.0, width, 15.0);
}

NSPoint projectedNodePoint(uint32_t node,
    float viewAzimuthDeg, float viewElevationDeg, float viewZoom)
{
    const s3g::Vec3 direction {
        (node & 1u) != 0u ? 1.0f : -1.0f,
        (node & 2u) != 0u ? 1.0f : -1.0f,
        (node & 4u) != 0u ? 1.0f : -1.0f,
    };
    const auto projection =
        s3g::projectAedDirection(
            direction, viewAzimuthDeg, viewElevationDeg);
    const CGFloat scale = 128.0
        * std::clamp<CGFloat>(viewZoom, 0.55, 1.55);
    return NSMakePoint(
        fieldPanelRect().origin.x
            + fieldPanelRect().size.width * 0.50
            + projection.horizontal * scale,
        fieldPanelRect().origin.y
            + fieldPanelRect().size.height * 0.52
            - projection.vertical * scale);
}

uint32_t edgeFirst(uint32_t edge)
{
    constexpr std::array<uint32_t, kEdgeCount> first {{
        0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 4u, 4u, 5u, 6u
    }};
    return first[edge];
}

uint32_t edgeSecond(uint32_t edge)
{
    constexpr std::array<uint32_t, kEdgeCount> second {{
        1u, 2u, 4u, 3u, 5u, 3u, 6u, 7u, 5u, 6u, 7u, 7u
    }};
    return second[edge];
}

NSString* displayText(clap_id id, double value)
{
    char text[64] {};
    formatParamValue(id, value, text, sizeof(text));
    return [NSString stringWithUTF8String:text];
}

NSString* midiNoteName(int32_t key)
{
    if (key < 0 || key > 127) return @"";
    static NSString* names[12] = {
        @"C", @"C#", @"D", @"D#", @"E", @"F",
        @"F#", @"G", @"G#", @"A", @"A#", @"B"
    };
    return [NSString stringWithFormat:@"%@%d",
        names[key % 12], key / 12 - 1];
}

void drawControlRow(Plugin& p, clap_id id, NSString* shortName,
    NSRect panel, uint32_t row, bool menu,
    NSDictionary* labels, const s3g::clap_gui::Style& style)
{
    const auto* spec = paramSpec(id);
    if (!spec) return;
    const double value = publishedParamValue(p, id);
    const CGFloat y = rowY(panel, row);
    if (menu) {
        s3g::clap_gui::drawMenu(
            shortName, displayText(id, value), y, labels,
            s3g::clap_gui::softValueAttrs(), style,
            panel.origin.x + 14.0,
            panel.origin.x + 102.0,
            panel.size.width - 116.0);
    } else {
        s3g::clap_gui::drawSlider(
            shortName, displayText(id, value),
            static_cast<CGFloat>(normalizedParamValue(*spec, value)),
            y, labels, s3g::clap_gui::softValueAttrs(), style,
            panel.origin.x + 14.0,
            panel.origin.x + 102.0,
            panel.origin.x + panel.size.width - 64.0,
            panel.size.width - 178.0,
            52.0);
    }
}

} // namespace

@interface S3GAmbiEncoderMediumView : NSView {
    void* _plugin;
    clap_id _dragParam;
    clap_id _openMenu;
    int _hoverMenuItem;
    uint32_t _menuItemCount;
    int _viewMode;
    CGFloat _viewAzDeg;
    CGFloat _viewElDeg;
    CGFloat _viewZoom;
    int _excitationPage;
    BOOL _dragView;
    NSPoint _lastDragPoint;
    int _factoryPresetIndex;
    char _titlePresetName[64];
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (void)updateSlider:(NSPoint)point;
- (void)setViewPreset:(int)mode;
- (void)storeViewState;
- (void)applyFactoryPreset:(int)index;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style;
@end

@implementation S3GAmbiEncoderMediumView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _dragParam = CLAP_INVALID_ID;
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        auto* p = static_cast<Plugin*>(plugin);
        _viewMode = p ? p->guiViewMode : 0;
        _viewAzDeg = p ? p->guiViewAzDeg : 35.0;
        _viewElDeg = p ? p->guiViewElDeg : -34.0;
        _viewZoom = p ? p->guiViewZoom : 1.0;
        if (_viewMode == 0) { _viewAzDeg = 90.0; _viewElDeg = 0.0; }
        else if (_viewMode == 1) { _viewAzDeg = 90.0; _viewElDeg = -90.0; }
        else if (_viewMode == 2) { _viewAzDeg = 35.0; _viewElDeg = -34.0; }
        if (p && _viewMode >= 0 && _viewMode <= 2) {
            p->guiViewAzDeg = static_cast<float>(_viewAzDeg);
            p->guiViewElDeg = static_cast<float>(_viewElDeg);
        }
        _excitationPage = p ? p->guiExcitationPage : 0;
        _dragView = NO;
        _lastDragPoint = NSZeroPoint;
        _factoryPresetIndex = 0;
        std::snprintf(
            _titlePresetName, sizeof(_titlePresetName), "%s", "INIT");
        _timer = nil;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)dealloc
{
    [self storeViewState];
    [self stopRefreshTimer];
    [super dealloc];
}

- (void)storeViewState
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    p->guiViewMode = _viewMode;
    p->guiViewAzDeg = static_cast<float>(_viewAzDeg);
    p->guiViewElDeg = static_cast<float>(_viewElDeg);
    p->guiViewZoom = static_cast<float>(_viewZoom);
    p->guiExcitationPage = _excitationPage;
}

- (void)setViewPreset:(int)mode
{
    _viewMode = std::clamp(mode, 0, 2);
    if (_viewMode == 0) {
        _viewAzDeg = 90.0;
        _viewElDeg = 0.0;
    } else if (_viewMode == 1) {
        _viewAzDeg = 90.0;
        _viewElDeg = -90.0;
    } else {
        _viewAzDeg = 35.0;
        _viewElDeg = -34.0;
    }
    [self storeViewState];
    [self setNeedsDisplay:YES];
}

- (void)applyFactoryPreset:(int)index
{
    auto* p = static_cast<Plugin*>(_plugin);
    if (!p) return;
    struct FactoryPreset {
        const char* name;
        double speed;
        double decay;
        double absorption;
        double nonlinearity;
        double radiation;
        double size;
        double node;
        double input;
        double strike;
        double selfExcitation;
        double selfRate;
        double exciter;
        double sustain;
        double character;
        double dispersion;
        uint32_t euclideanSteps;
        std::array<uint32_t, kNodeCount> pulses;
        std::array<uint32_t, kNodeCount> rotations;
    };
    static constexpr FactoryPreset presets[7] {
        { "INIT", 343.0, 2.5, 0.22, 0.00, 0.20, 0.50,
            1.0, 1.00, 1.00, 0.32, 0.80,
            0.0, 0.00, 0.50, 0.00, 16u,
            {{ 5u, 4u, 3u, 2u, 5u, 3u, 4u, 2u }},
            {{ 0u, 3u, 6u, 9u, 2u, 5u, 8u, 11u }} },
        { "RESONANT CUBE", 210.0, 8.0, 0.07, 0.16, 0.08, 0.72,
            1.0, 0.65, 0.75, 0.22, 0.35,
            0.0, 0.00, 0.45, 0.08, 16u,
            {{ 3u, 2u, 4u, 2u, 3u, 2u, 4u, 2u }},
            {{ 0u, 4u, 8u, 12u, 2u, 6u, 10u, 14u }} },
        { "BOWED GLASS", 480.0, 10.0, 0.18, 0.12, 0.38, 0.34,
            2.0, 0.30, 0.40, 0.00, 0.50,
            1.0, 0.62, 0.44, 0.35, 16u,
            {{ 4u, 3u, 2u, 4u, 3u, 2u, 4u, 2u }},
            {{ 0u, 3u, 7u, 11u, 2u, 6u, 10u, 14u }} },
        { "REED CHAMBER", 343.0, 6.0, 0.16, 0.08, 0.66, 0.28,
            3.0, 0.20, 0.30, 0.00, 0.50,
            2.0, 0.58, 0.52, 0.08, 16u,
            {{ 3u, 2u, 3u, 2u, 3u, 2u, 3u, 2u }},
            {{ 0u, 4u, 8u, 12u, 2u, 6u, 10u, 14u }} },
        { "AIR JET LATTICE", 680.0, 4.8, 0.33, 0.18, 0.82, 0.20,
            5.0, 0.20, 0.30, 0.00, 0.50,
            3.0, 0.52, 0.68, 0.18, 13u,
            {{ 5u, 3u, 4u, 2u, 5u, 3u, 4u, 2u }},
            {{ 0u, 2u, 5u, 8u, 11u, 1u, 4u, 9u }} },
        { "GLASS LATTICE", 980.0, 4.2, 0.36, 0.08, 0.72, 0.18,
            3.0, 0.45, 0.55, 0.18, 1.70,
            0.0, 0.00, 0.65, 0.52, 13u,
            {{ 5u, 4u, 3u, 6u, 2u, 5u, 4u, 3u }},
            {{ 0u, 2u, 5u, 8u, 11u, 1u, 4u, 9u }} },
        { "DARK DRONE", 72.0, 18.0, 0.62, 0.48, 0.16, 1.40,
            6.0, 0.20, 0.35, 0.06, 0.18,
            1.0, 0.38, 0.22, 0.26, 24u,
            {{ 3u, 2u, 1u, 3u, 2u, 1u, 2u, 1u }},
            {{ 0u, 5u, 11u, 17u, 3u, 9u, 15u, 21u }} },
    };
    index = std::clamp(index, 0, 6);
    const auto& preset = presets[index];
    const auto set = [&](clap_id id, double value) {
        queueGuiParamGestureBegin(*p, id);
        queueGuiParamValue(*p, id, value);
        queueGuiParamGestureEnd(*p, id);
    };
    set(kSpeedParamId, preset.speed);
    set(kDecayParamId, preset.decay);
    set(kAbsorptionParamId, preset.absorption);
    set(kNonlinearityParamId, preset.nonlinearity);
    set(kRadiationParamId, preset.radiation);
    set(kSizeParamId, preset.size);
    set(kActuatorNodeParamId, preset.node);
    set(kActuatorGainParamId, preset.input);
    set(kStrikeGainParamId, preset.strike);
    set(kSelfExcitationGainParamId, preset.selfExcitation);
    set(kSelfExcitationRateParamId, preset.selfRate);
    set(kExciterTypeParamId, preset.exciter);
    set(kSustainedExcitationParamId, preset.sustain);
    set(kExciterCharacterParamId, preset.character);
    set(kDispersionParamId, preset.dispersion);
    set(kEuclideanStepsParamId, preset.euclideanSteps);
    constexpr std::array<double, 7u> presetScales {{
        0.0, 0.0, 2.0, 1.0, 3.0, 4.0, 6.0
    }};
    constexpr std::array<double, 7u> presetNoteCounts {{
        1.0, 3.0, 5.0, 4.0, 6.0, 8.0, 2.0
    }};
    set(kSequencerScaleParamId, presetScales[index]);
    set(kSequencerNoteCountParamId, presetNoteCounts[index]);
    constexpr std::array<double, 7u> presetDirectivity {{
        0.55, 0.68, 0.82, 0.74, 0.86, 0.62, 0.46
    }};
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        set(euclideanPulsesParamId(node), preset.pulses[node]);
        set(euclideanRotationParamId(node), preset.rotations[node]);
        set(nodeDirectivityParamId(node), presetDirectivity[index]);
    }
    _factoryPresetIndex = index;
    std::snprintf(_titlePresetName,
        sizeof(_titlePresetName), "%s", preset.name);
    [self setNeedsDisplay:YES];
}

- (NSRect)openMenuRect
{
    if (_openMenu == kFactoryPresetMenuId) {
        const NSRect box = s3g::clap_gui::cocoaRect(
            s3g::clap_gui::encoderTitleBand(
                kGuiWidth, kGuiHeight).presetMenu);
        return NSMakeRect(box.origin.x, NSMaxY(box) + 2.0,
            box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kOrderParamId) {
        const NSRect box = menuBoxRect(outputPanelRect(), 0u);
        return NSMakeRect(box.origin.x, NSMaxY(box) + 2.0,
            box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kActuatorNodeParamId) {
        const NSRect box = menuBoxRect(excitationPanelRect(), 0u);
        return NSMakeRect(box.origin.x, NSMaxY(box) + 2.0,
            box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kExciterTypeParamId) {
        const NSRect box = menuBoxRect(
            excitationPanelRect(), _excitationPage == 1 ? 0u : 1u);
        return NSMakeRect(box.origin.x, NSMaxY(box) + 2.0,
            box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kMidiModeParamId) {
        const NSRect box = menuBoxRect(excitationPanelRect(), 0u);
        return NSMakeRect(box.origin.x, NSMaxY(box) + 2.0,
            box.size.width, 18.0 * _menuItemCount);
    }
    if (_openMenu == kSequencerScaleParamId) {
        const NSRect box = menuBoxRect(excitationPanelRect(), 6u);
        const CGFloat height = 18.0 * _menuItemCount;
        return NSMakeRect(box.origin.x, box.origin.y - 2.0 - height,
            box.size.width, height);
    }
    return NSZeroRect;
}

- (void)drawOpenMenu:(NSDictionary*)attrs
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu == CLAP_INVALID_ID || _menuItemCount == 0u) return;
    NSString* presetItems[7] = {
        @"INIT", @"RESONANT CUBE", @"BOWED GLASS", @"REED CHAMBER",
        @"AIR JET LATTICE", @"GLASS LATTICE", @"DARK DRONE"
    };
    NSString* orderItems[3] = { @"1OA / 4CH", @"2OA / 9CH", @"3OA / 16CH" };
    NSString* nodeItems[8] = {
        @"NODE 1", @"NODE 2", @"NODE 3", @"NODE 4",
        @"NODE 5", @"NODE 6", @"NODE 7", @"NODE 8"
    };
    NSString* exciterItems[4] = {
        @"PERCUSSIVE", @"BOW", @"REED", @"AIR JET"
    };
    NSString* midiItems[4] = {
        @"STRIKE", @"MONO NOTE", @"POLY SEQ", @"POLY RANDOM"
    };
    NSString* scaleItems[8] = {
        @"MAJOR", @"NATURAL MINOR", @"DORIAN", @"MIXOLYDIAN",
        @"MAJOR PENTATONIC", @"MINOR PENTATONIC", @"HARMONIC MINOR",
        @"CHROMATIC"
    };
    NSString* const* items = _openMenu == kFactoryPresetMenuId
        ? presetItems
        : (_openMenu == kOrderParamId ? orderItems
            : (_openMenu == kExciterTypeParamId
                ? exciterItems
                : (_openMenu == kMidiModeParamId
                    ? midiItems
                    : (_openMenu == kSequencerScaleParamId
                        ? scaleItems : nodeItems))));
    const int selected = _openMenu == kFactoryPresetMenuId
        ? _factoryPresetIndex
        : static_cast<int>(std::lround(publishedParamValue(
            *static_cast<Plugin*>(_plugin), _openMenu)))
            - ((_openMenu == kExciterTypeParamId
                || _openMenu == kMidiModeParamId
                || _openMenu == kSequencerScaleParamId) ? 0 : 1);
    s3g::clap_gui::drawDropdownMenu(
        [self openMenuRect], 18.0, items, _menuItemCount,
        selected, _hoverMenuItem, attrs, style);
}

- (void)startRefreshTimer
{
    if (_timer) return;
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0
        target:self selector:@selector(refresh:)
        userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer
        forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (_timer) {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) {
        [self setNeedsDisplay:YES];
    }
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    auto* p = static_cast<Plugin*>(_plugin);
    s3g::clap_gui::Style style;
    [style.bg setFill];
    NSRectFill([self bounds]);
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    const float peak = p->outputPeak.load(std::memory_order_relaxed);
    const float guard = p->guardGain.load(std::memory_order_relaxed);
    NSString* status = guard < 0.999f
        ? [NSString stringWithFormat:@"SAFE %+.1f dB",
            20.0f * std::log10(std::max(guard, 0.000001f))]
        : s3g::clap_gui::peakDbText(peak);
    s3g::clap_gui::drawEncoderTitleBand(
        @"s3g AMBI ENCODER MEDIUM 16",
        [NSString stringWithUTF8String:_titlePresetName],
        status,
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight),
        title, labels, values, style);

    const auto drawPanel = [&](NSString* name, NSRect panel) {
        s3g::clap_gui::drawPanelFrame(
            panel.origin.x, panel.origin.y,
            panel.size.width, panel.size.height, style);
        s3g::clap_gui::drawPanelHeader(
            name, true, panel.origin.x, panel.origin.y,
            panel.size.width, 24.0, labels, style);
    };
    drawPanel(@"WAVEGUIDE FIELD", fieldPanelRect());
    drawPanel(@"OUTPUT", outputPanelRect());
    drawPanel(@"MEDIUM / STRUCTURE", mediumPanelRect());
    drawPanel(@"EXCITATION", excitationPanelRect());

    s3g::clap_gui::drawTopologyProcessorCameraButtons(
        fieldPanelRect(), _viewMode, values, style);
    NSString* zoomLabels[2] = { @"-", @"+" };
    for (uint32_t index = 0u; index < 2u; ++index) {
        s3g::clap_gui::drawHeaderButton(
            zoomButtonRect(index), fieldPanelRect(),
            zoomLabels[index], false, values, style);
    }
    NSString* excitationPages[3] = { @"SOURCE", @"SEQ", @"MIDI" };
    for (uint32_t index = 0u; index < 3u; ++index) {
        s3g::clap_gui::drawHeaderButton(
            excitationPageButtonRect(index), excitationPanelRect(),
            excitationPages[index], _excitationPage == static_cast<int>(index),
            values, style);
    }

    [@"CLICK NODE TO STRIKE / DRAG EMPTY FIELD TO ORBIT"
        drawAtPoint:NSMakePoint(
            fieldPanelRect().origin.x + 16.0,
            fieldPanelRect().origin.y + 36.0)
        withAttributes:values];
    [@"WHITE RING = SELECTED   CYAN RING + NOTE = MIDI HELD   HALO = TRIGGERED"
        drawAtPoint:NSMakePoint(
            fieldPanelRect().origin.x + 16.0,
            fieldPanelRect().origin.y + 51.0)
        withAttributes:values];
    [@"CUBE EDGES ARE BIDIRECTIONAL THIRAN WAVEGUIDES"
        drawAtPoint:NSMakePoint(
            fieldPanelRect().origin.x + 16.0,
            fieldPanelRect().origin.y
                + fieldPanelRect().size.height - 27.0)
        withAttributes:values];

    for (uint32_t edge = 0u; edge < kEdgeCount; ++edge) {
        const float energy = std::clamp(
            p->edgeEnergy[edge].load(std::memory_order_relaxed)
                * 1.8f, 0.0f, 1.0f);
        const NSPoint first = projectedNodePoint(edgeFirst(edge),
            static_cast<float>(_viewAzDeg),
            static_cast<float>(_viewElDeg),
            static_cast<float>(_viewZoom));
        const NSPoint second = projectedNodePoint(edgeSecond(edge),
            static_cast<float>(_viewAzDeg),
            static_cast<float>(_viewElDeg),
            static_cast<float>(_viewZoom));
        NSBezierPath* path = [NSBezierPath bezierPath];
        [path moveToPoint:first];
        [path lineToPoint:second];
        [path setLineWidth:1.0 + 4.0 * std::sqrt(energy)];
        [[NSColor colorWithCalibratedWhite:
            0.28 + 0.62 * energy alpha:1.0] setStroke];
        [path stroke];
    }
    const uint32_t selected = static_cast<uint32_t>(
        std::clamp(std::lround(
            publishedParamValue(*p, kActuatorNodeParamId)),
            1l, 8l) - 1l);
    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        const NSPoint point = projectedNodePoint(node,
            static_cast<float>(_viewAzDeg),
            static_cast<float>(_viewElDeg),
            static_cast<float>(_viewZoom));
        const float energy = std::clamp(
            p->nodeEnergy[node].load(std::memory_order_relaxed)
                * 3.2f, 0.0f, 1.0f);
        const float strike = std::clamp(
            p->nodeStrikeActivity[node].load(std::memory_order_relaxed),
            0.0f, 1.0f);
        const float midi = std::clamp(
            p->midiNodeActivity[node].load(std::memory_order_relaxed),
            0.0f, 1.0f);
        const int32_t midiKey =
            p->midiNodeKey[node].load(std::memory_order_relaxed);
        const float sounding = std::max(energy, strike);
        if (sounding > 0.001f) {
            const CGFloat radius = 15.0 + 30.0 * std::sqrt(sounding);
            NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(point.x - radius, point.y - radius,
                    radius * 2.0, radius * 2.0)];
            [[NSColor colorWithCalibratedRed:0.42
                green:0.78 blue:0.86
                alpha:0.12 + 0.34 * sounding] setFill];
            [halo fill];
        }
        if (midi > 0.001f) {
            const CGFloat glowRadius = 13.0 + 8.0 * std::sqrt(midi);
            NSBezierPath* midiGlow = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(point.x - glowRadius, point.y - glowRadius,
                    glowRadius * 2.0, glowRadius * 2.0)];
            [[NSColor colorWithCalibratedRed:0.40 green:0.88 blue:1.0
                alpha:0.15 + 0.22 * midi] setFill];
            [midiGlow fill];
        }
        const bool isSelected = node == selected;
        const CGFloat radius = midi > 0.001f || sounding > 0.08f ? 8.0 : 6.0;
        NSBezierPath* body = [NSBezierPath bezierPathWithOvalInRect:
            NSMakeRect(point.x - radius, point.y - radius,
                radius * 2.0, radius * 2.0)];
        NSColor* bodyColor = midi > 0.001f
            ? [NSColor colorWithCalibratedRed:0.72 green:0.95 blue:1.0 alpha:1.0]
            : (sounding > 0.02f
                ? [NSColor colorWithCalibratedWhite:
                    0.60 + 0.34 * sounding alpha:1.0]
                : s3g::clap_gui::color(0x777777));
        [bodyColor setFill];
        [body fill];
        if (midi > 0.001f) {
            const CGFloat ringRadius = 12.0 + 3.0 * std::sqrt(midi);
            NSBezierPath* midiRing = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(point.x - ringRadius, point.y - ringRadius,
                    ringRadius * 2.0, ringRadius * 2.0)];
            [midiRing setLineWidth:2.2];
            [[NSColor colorWithCalibratedRed:0.52 green:0.92 blue:1.0
                alpha:0.72 + 0.28 * midi] setStroke];
            [midiRing stroke];
        }
        if (isSelected) {
            constexpr CGFloat selectedRadius = 10.0;
            NSBezierPath* selection = [NSBezierPath bezierPathWithOvalInRect:
                NSMakeRect(point.x - selectedRadius, point.y - selectedRadius,
                    selectedRadius * 2.0, selectedRadius * 2.0)];
            [selection setLineWidth:1.4];
            [[NSColor colorWithCalibratedWhite:0.95 alpha:0.95] setStroke];
            [selection stroke];
        }
        NSString* nodeLabel = midiKey >= 0
            ? [NSString stringWithFormat:@"N%u  %@",
                node + 1u, midiNoteName(midiKey)]
            : [NSString stringWithFormat:@"N%u", node + 1u];
        [nodeLabel
            drawAtPoint:NSMakePoint(point.x + 10.0, point.y - 7.0)
            withAttributes:values];
    }

    drawControlRow(*p, kOrderParamId, @"ORDER",
        outputPanelRect(), 0u, true, labels, style);
    drawControlRow(*p, kOutputGainParamId, @"OUT",
        outputPanelRect(), 1u, false, labels, style);
    drawControlRow(*p, kSpeedParamId, @"SPEED",
        mediumPanelRect(), 0u, false, labels, style);
    drawControlRow(*p, kSizeParamId, @"SIZE",
        mediumPanelRect(), 1u, false, labels, style);
    drawControlRow(*p, kDecayParamId, @"DECAY",
        mediumPanelRect(), 2u, false, labels, style);
    drawControlRow(*p, kAbsorptionParamId, @"ABSORB",
        mediumPanelRect(), 3u, false, labels, style);
    drawControlRow(*p, kNonlinearityParamId, @"NONLINEAR",
        mediumPanelRect(), 4u, false, labels, style);
    drawControlRow(*p, kRadiationParamId, @"RADIATE",
        mediumPanelRect(), 5u, false, labels, style);
    drawControlRow(*p, kDispersionParamId, @"DISPERSE",
        mediumPanelRect(), 6u, false, labels, style);
    if (_excitationPage == 0) {
        drawControlRow(*p, kActuatorNodeParamId, @"NODE",
            excitationPanelRect(), 0u, true, labels, style);
        drawControlRow(*p, kExciterTypeParamId, @"EXCITER",
            excitationPanelRect(), 1u, true, labels, style);
        drawControlRow(*p, kSustainedExcitationParamId, @"SUSTAIN",
            excitationPanelRect(), 2u, false, labels, style);
        drawControlRow(*p, kExciterCharacterParamId, @"CHARACTER",
            excitationPanelRect(), 3u, false, labels, style);
        drawControlRow(*p, kActuatorGainParamId, @"INPUT",
            excitationPanelRect(), 4u, false, labels, style);
        drawControlRow(*p, kStrikeGainParamId, @"STRIKE",
            excitationPanelRect(), 5u, false, labels, style);
        drawControlRow(*p, nodeDirectivityParamId(selected), @"MASK",
            excitationPanelRect(), 6u, false, labels, style);
    } else if (_excitationPage == 1) {
        drawControlRow(*p, kExciterTypeParamId, @"EXCITER",
            excitationPanelRect(), 0u, true, labels, style);
        drawControlRow(*p, kSelfExcitationGainParamId, @"SELF",
            excitationPanelRect(), 1u, false, labels, style);
        drawControlRow(*p, kSelfExcitationRateParamId, @"RATE",
            excitationPanelRect(), 2u, false, labels, style);
        drawControlRow(*p, kEuclideanStepsParamId, @"STEPS",
            excitationPanelRect(), 3u, false, labels, style);
        drawControlRow(*p, euclideanPulsesParamId(selected), @"PULSES",
            excitationPanelRect(), 4u, false, labels, style);
        drawControlRow(*p, euclideanRotationParamId(selected), @"ROTATE",
            excitationPanelRect(), 5u, false, labels, style);
        drawControlRow(*p, kSequencerScaleParamId, @"SCALE",
            excitationPanelRect(), 6u, true, labels, style);
        drawControlRow(*p, kSequencerNoteCountParamId, @"NOTES",
            excitationPanelRect(), 7u, false, labels, style);
    } else {
        drawControlRow(*p, kMidiModeParamId, @"MODE",
            excitationPanelRect(), 0u, true, labels, style);
        drawControlRow(*p, kMidiTransposeParamId, @"TRANSPOSE",
            excitationPanelRect(), 1u, false, labels, style);
        drawControlRow(*p, kMidiAttackParamId, @"ATTACK",
            excitationPanelRect(), 2u, false, labels, style);
        drawControlRow(*p, kMidiReleaseParamId, @"RELEASE",
            excitationPanelRect(), 3u, false, labels, style);
        drawControlRow(*p, kMidiVelocityParamId, @"VELOCITY",
            excitationPanelRect(), 4u, false, labels, style);
    }
    [self drawOpenMenu:values style:style];
}

- (void)updateSlider:(NSPoint)point
{
    if (_dragParam == CLAP_INVALID_ID) return;
    auto* p = static_cast<Plugin*>(_plugin);
    const auto* spec = paramSpec(_dragParam);
    if (!spec) return;
    NSRect panel = outputPanelRect();
    if (_dragParam == kSpeedParamId || _dragParam == kSizeParamId
        || _dragParam == kDecayParamId
        || _dragParam == kAbsorptionParamId
        || _dragParam == kNonlinearityParamId
        || _dragParam == kRadiationParamId
        || _dragParam == kDispersionParamId) {
        panel = mediumPanelRect();
    } else if (_dragParam == kActuatorGainParamId
        || _dragParam == kStrikeGainParamId
        || _dragParam == kSelfExcitationGainParamId
        || _dragParam == kSelfExcitationRateParamId
        || _dragParam == kEuclideanStepsParamId
        || _dragParam == kSustainedExcitationParamId
        || _dragParam == kExciterCharacterParamId
        || _dragParam == kMidiTransposeParamId
        || _dragParam == kMidiAttackParamId
        || _dragParam == kMidiReleaseParamId
        || _dragParam == kMidiVelocityParamId
        || _dragParam == kSequencerNoteCountParamId
        || isEuclideanPulsesParam(_dragParam)
        || isEuclideanRotationParam(_dragParam)
        || isNodeDirectivityParam(_dragParam)) {
        panel = excitationPanelRect();
    }
    const double trackX = panel.origin.x + 102.0;
    const double trackWidth = panel.size.width - 178.0;
    const double normalized = std::clamp(
        (point.x - trackX) / trackWidth, 0.0, 1.0);
    queueGuiParamValue(
        *p, _dragParam, valueFromNormalized(*spec, normalized));
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    const NSPoint point =
        [self convertPoint:[event locationInWindow] fromView:nil];
    auto* p = static_cast<Plugin*>(_plugin);

    if (_openMenu != CLAP_INVALID_ID) {
        const int hit = s3g::clap_gui::dropdownHitIndex(
            point, [self openMenuRect], 18.0, _menuItemCount);
        if (hit >= 0) {
            if (_openMenu == kFactoryPresetMenuId) {
                [self applyFactoryPreset:hit];
            } else {
                queueGuiParamGestureBegin(*p, _openMenu);
                queueGuiParamValue(*p, _openMenu,
                    hit + ((_openMenu == kExciterTypeParamId
                        || _openMenu == kMidiModeParamId
                        || _openMenu == kSequencerScaleParamId)
                        ? 0.0 : 1.0));
                queueGuiParamGestureEnd(*p, _openMenu);
                _factoryPresetIndex = -1;
                std::snprintf(_titlePresetName,
                    sizeof(_titlePresetName), "%s", "CUSTOM");
            }
        }
        _openMenu = CLAP_INVALID_ID;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        [self setNeedsDisplay:YES];
        return;
    }

    const auto titleBand =
        s3g::clap_gui::encoderTitleBand(kGuiWidth, kGuiHeight);
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = 7u;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePresetPreservingParam(
                &p->plugin, @"Ambi Encoder Medium",
                kOutputGainParamId, &name)) {
            _factoryPresetIndex = -1;
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
            [self setNeedsDisplay:YES];
        } else {
            NSBeep();
        }
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &p->plugin, @"Ambi Encoder Medium", &name)) {
            std::snprintf(_titlePresetName, sizeof(_titlePresetName), "%s",
                name ? [name UTF8String] : "CUSTOM");
            [self setNeedsDisplay:YES];
        } else {
            NSBeep();
        }
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        const auto randomUnit = [] {
            return static_cast<double>(arc4random()) / 4294967295.0;
        };
        const auto set = [&](clap_id id, double value) {
            queueGuiParamGestureBegin(*p, id);
            queueGuiParamValue(*p, id, value);
            queueGuiParamGestureEnd(*p, id);
        };
        set(kSpeedParamId, 40.0 * std::pow(35.0, randomUnit()));
        set(kDecayParamId, 0.25 * std::pow(80.0, randomUnit()));
        set(kAbsorptionParamId, 0.04 + randomUnit() * 0.72);
        set(kNonlinearityParamId, randomUnit() * 0.58);
        set(kRadiationParamId, 0.04 + randomUnit() * 0.78);
        set(kSizeParamId, 0.05 * std::pow(36.0, randomUnit()));
        set(kActuatorNodeParamId,
            static_cast<double>(arc4random_uniform(kNodeCount) + 1u));
        const uint32_t exciter = arc4random_uniform(4u);
        set(kExciterTypeParamId, exciter);
        set(kSustainedExcitationParamId,
            exciter > 0u ? 0.18 + randomUnit() * 0.58 : 0.0);
        set(kExciterCharacterParamId, randomUnit());
        set(kDispersionParamId, randomUnit() * 0.68);
        set(kSelfExcitationGainParamId,
            exciter > 0u ? randomUnit() * 0.10
                         : 0.12 + randomUnit() * 0.42);
        set(kSelfExcitationRateParamId,
            0.08 * std::pow(50.0, randomUnit()));
        const uint32_t euclideanSteps =
            8u + arc4random_uniform(17u);
        set(kEuclideanStepsParamId, euclideanSteps);
        set(kSequencerScaleParamId, arc4random_uniform(8u));
        set(kSequencerNoteCountParamId,
            1u + arc4random_uniform(8u));
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            const uint32_t pulseRange =
                std::max<uint32_t>(1u, euclideanSteps / 2u);
            set(euclideanPulsesParamId(node),
                1u + arc4random_uniform(pulseRange));
            set(euclideanRotationParamId(node),
                arc4random_uniform(euclideanSteps));
            set(nodeDirectivityParamId(node),
                0.18 + randomUnit() * 0.82);
        }
        _factoryPresetIndex = -1;
        std::snprintf(
            _titlePresetName, sizeof(_titlePresetName), "%s", "RANDOM");
        [self setNeedsDisplay:YES];
        return;
    }

    for (uint32_t index = 0u; index < 2u; ++index) {
        if (NSPointInRect(point, zoomButtonRect(index))) {
            _viewZoom = std::clamp<CGFloat>(
                _viewZoom + (index == 0u ? -0.15 : 0.15),
                0.55, 1.55);
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (NSPointInRect(point,
                s3g::clap_gui::topologyProcessorCameraButtonRect(
                    fieldPanelRect(), index))) {
            [self setViewPreset:static_cast<int>(index)];
            return;
        }
    }
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (NSPointInRect(point, excitationPageButtonRect(index))) {
            _excitationPage = static_cast<int>(index);
            [self storeViewState];
            [self setNeedsDisplay:YES];
            return;
        }
    }

    for (uint32_t node = 0u; node < kNodeCount; ++node) {
        const NSPoint center = projectedNodePoint(node,
            static_cast<float>(_viewAzDeg),
            static_cast<float>(_viewElDeg),
            static_cast<float>(_viewZoom));
        const double dx = point.x - center.x;
        const double dy = point.y - center.y;
        if (dx * dx + dy * dy <= 18.0 * 18.0) {
            queueGuiParamGestureBegin(*p, kActuatorNodeParamId);
            queueGuiParamValue(*p, kActuatorNodeParamId, node + 1.0);
            queueGuiParamGestureEnd(*p, kActuatorNodeParamId);
            queuePreviewStrike(*p, node);
            [self setNeedsDisplay:YES];
            return;
        }
    }

    const auto openMenu = [&](clap_id id, uint32_t itemCount) {
        _openMenu = id;
        _hoverMenuItem = -1;
        _menuItemCount = itemCount;
        [self setNeedsDisplay:YES];
    };
    if (NSPointInRect(point, rowHitRect(outputPanelRect(), 0u))) {
        openMenu(kOrderParamId, 3u);
        return;
    }
    if (_excitationPage == 0
        && NSPointInRect(point, rowHitRect(excitationPanelRect(), 0u))) {
        openMenu(kActuatorNodeParamId, 8u);
        return;
    }
    if (((_excitationPage == 0
            && NSPointInRect(
                point, rowHitRect(excitationPanelRect(), 1u)))
        || (_excitationPage == 1
            && NSPointInRect(
                point, rowHitRect(excitationPanelRect(), 0u))))) {
        openMenu(kExciterTypeParamId, 4u);
        return;
    }
    if (_excitationPage == 2
        && NSPointInRect(point, rowHitRect(excitationPanelRect(), 0u))) {
        openMenu(kMidiModeParamId, 4u);
        return;
    }
    if (_excitationPage == 1
        && NSPointInRect(point, rowHitRect(excitationPanelRect(), 6u))) {
        openMenu(kSequencerScaleParamId, 8u);
        return;
    }
    if (NSPointInRect(point, fieldInteractionRect())) {
        _dragView = YES;
        _lastDragPoint = point;
        _viewMode = -1;
        [self storeViewState];
        return;
    }

    const uint32_t selectedNode = static_cast<uint32_t>(std::clamp(
        std::lround(publishedParamValue(*p, kActuatorNodeParamId)),
        1l, 8l) - 1l);
    struct HitRow { clap_id id; NSRect panel; uint32_t row; };
    std::array<HitRow, 16u> rows {};
    uint32_t rowCount = 0u;
    const auto addRow = [&](clap_id id, NSRect panel, uint32_t row) {
        if (rowCount < rows.size()) rows[rowCount++] = { id, panel, row };
    };
    addRow(kOutputGainParamId, outputPanelRect(), 1u);
    addRow(kSpeedParamId, mediumPanelRect(), 0u);
    addRow(kSizeParamId, mediumPanelRect(), 1u);
    addRow(kDecayParamId, mediumPanelRect(), 2u);
    addRow(kAbsorptionParamId, mediumPanelRect(), 3u);
    addRow(kNonlinearityParamId, mediumPanelRect(), 4u);
    addRow(kRadiationParamId, mediumPanelRect(), 5u);
    addRow(kDispersionParamId, mediumPanelRect(), 6u);
    if (_excitationPage == 0) {
        addRow(kSustainedExcitationParamId, excitationPanelRect(), 2u);
        addRow(kExciterCharacterParamId, excitationPanelRect(), 3u);
        addRow(kActuatorGainParamId, excitationPanelRect(), 4u);
        addRow(kStrikeGainParamId, excitationPanelRect(), 5u);
        addRow(nodeDirectivityParamId(selectedNode),
            excitationPanelRect(), 6u);
    } else if (_excitationPage == 1) {
        addRow(kSelfExcitationGainParamId, excitationPanelRect(), 1u);
        addRow(kSelfExcitationRateParamId, excitationPanelRect(), 2u);
        addRow(kEuclideanStepsParamId, excitationPanelRect(), 3u);
        addRow(euclideanPulsesParamId(selectedNode),
            excitationPanelRect(), 4u);
        addRow(euclideanRotationParamId(selectedNode),
            excitationPanelRect(), 5u);
        addRow(kSequencerNoteCountParamId,
            excitationPanelRect(), 7u);
    } else {
        addRow(kMidiTransposeParamId, excitationPanelRect(), 1u);
        addRow(kMidiAttackParamId, excitationPanelRect(), 2u);
        addRow(kMidiReleaseParamId, excitationPanelRect(), 3u);
        addRow(kMidiVelocityParamId, excitationPanelRect(), 4u);
    }
    for (uint32_t index = 0u; index < rowCount; ++index) {
        const auto& row = rows[index];
        if (!NSPointInRect(point, rowHitRect(row.panel, row.row))) continue;
        double defaultValue = 0.0;
        if (s3g::clap_gui::sliderDoubleClickDefault(
                event, &p->plugin, row.id, &defaultValue)) {
            queueGuiParamGestureBegin(*p, row.id);
            queueGuiParamValue(*p, row.id, defaultValue);
            queueGuiParamGestureEnd(*p, row.id);
            _dragParam = CLAP_INVALID_ID;
        } else {
            _dragParam = row.id;
            queueGuiParamGestureBegin(*p, row.id);
            [self updateSlider:point];
        }
        return;
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    const NSPoint point =
        [self convertPoint:[event locationInWindow] fromView:nil];
    if (_dragView) {
        _viewAzDeg += (point.x - _lastDragPoint.x) * 0.35;
        _viewElDeg = std::clamp<CGFloat>(
            _viewElDeg + (point.y - _lastDragPoint.y) * 0.35,
            -85.0, 85.0);
        _viewMode = -1;
        _lastDragPoint = point;
        [self storeViewState];
        [self setNeedsDisplay:YES];
        return;
    }
    if (_dragParam != CLAP_INVALID_ID) {
        [self updateSlider:point];
    }
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam != CLAP_INVALID_ID) {
        queueGuiParamGestureEnd(
            *static_cast<Plugin*>(_plugin), _dragParam);
    }
    _dragParam = CLAP_INVALID_ID;
    _dragView = NO;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu == CLAP_INVALID_ID) return;
    const NSPoint point =
        [self convertPoint:[event locationInWindow] fromView:nil];
    const int hover = s3g::clap_gui::dropdownHitIndex(
        point, [self openMenuRect], 18.0, _menuItemCount);
    if (hover != _hoverMenuItem) {
        _hoverMenuItem = hover;
        [self setNeedsDisplay:YES];
    }
}

@end

namespace {

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating)
{
    return !floating && api
        && std::strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*,
    const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin,
    const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* p = self(plugin);
    if (p->guiView) return true;
    p->guiView = [[S3GAmbiEncoderMediumView alloc] initWithPlugin:p];
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
    if (!p->guiView) return;
    p->guiVisible.store(false, std::memory_order_release);
    [static_cast<S3GAmbiEncoderMediumView*>(
        p->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        p->guiViewport, p->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*,
    clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin,
    uint32_t* width, uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height);
}

bool guiSetSize(const clap_plugin_t* plugin,
    uint32_t width, uint32_t height)
{
    return s3g::clap_gui::setResponsiveViewportSize(
        self(plugin)->guiViewport, width, height);
}

bool guiSetParent(const clap_plugin_t* plugin,
    const clap_window_t* window)
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

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView || !s3g::clap_gui::setResponsiveViewportHidden(
            p->guiViewport, false)) {
        return false;
    }
    p->guiVisible.store(true, std::memory_order_release);
    [static_cast<S3GAmbiEncoderMediumView*>(
        p->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* p = self(plugin);
    if (!p->guiView) return false;
    p->guiVisible.store(false, std::memory_order_release);
    [static_cast<S3GAmbiEncoderMediumView*>(
        p->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        p->guiViewport, true);
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
    guiHide
};

} // namespace

#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExt;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExt;
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
    nullptr
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.ambi-encoder-medium-16",
    "s3g Ambi Encoder Medium 16",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "1.6.1",
    "Eight-voice sequential or shuffle-bag-random node routing, scale-locked sequencer note dispersion, sharp strike-following directivity masks, and ratcheting physical excitation of a click-safe dispersive waveguide cube encoded to first through third-order ACN/SN3D ambisonics.",
    features
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
    for (const auto& spec : kParamSpecs) {
        applyParam(*p, spec.id, spec.defaultValue);
    }
    publishAllParams(*p);
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
    factoryGetPluginCount,
    factoryGetPluginDescriptor,
    createPlugin
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
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory
};
