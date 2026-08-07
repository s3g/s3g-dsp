#pragma once

#include "s3g/tracker/audio/stereo_slice_sampler_node.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace s3g::tracker {

// Stable graph identifiers. Pattern FX use kTrackInstrumentNode as an
// authoring-time target and the scheduler resolves it through a track's
// per-row Instrument memory before an event enters the canonical stream.
constexpr uint32_t kInvalidInstrumentNode =
    std::numeric_limits<uint32_t>::max();
constexpr uint32_t kTrackInstrumentNode = kInvalidInstrumentNode - 1u;
constexpr std::size_t kMembraneRackSlotCount = 5u;
constexpr std::size_t kSn76489RackSlotCount = 5u;
constexpr std::size_t kYm2151RackSlotCount = 3u;
constexpr std::size_t kDaisyDrumRackSlotCount = 3u;
constexpr std::size_t kDaisyDrumKindCount = 5u;
constexpr std::size_t kDaisyDrumNodeCount =
    kDaisyDrumRackSlotCount * kDaisyDrumKindCount;
constexpr std::size_t kStereoSamplerRackSlotCount = 3u;
constexpr std::size_t kMidiOutRackSlotCount = 8u;
constexpr std::size_t kInstrumentRackSlotCount =
    kMembraneRackSlotCount + kSn76489RackSlotCount
    + kYm2151RackSlotCount + kDaisyDrumNodeCount
    + kStereoSamplerRackSlotCount
    + kMidiOutRackSlotCount;
constexpr uint32_t kSn76489InstrumentNode = 5u;
constexpr uint32_t kYm2151InstrumentNode = static_cast<uint32_t>(
    kMembraneRackSlotCount + kSn76489RackSlotCount);
constexpr uint32_t kDaisyAnalogBassDrumInstrumentNode =
    kYm2151InstrumentNode + static_cast<uint32_t>(kYm2151RackSlotCount);
constexpr uint32_t kDaisyAnalogSnareDrumInstrumentNode =
    kDaisyAnalogBassDrumInstrumentNode
    + static_cast<uint32_t>(kDaisyDrumRackSlotCount);
constexpr uint32_t kDaisyHiHatInstrumentNode =
    kDaisyAnalogSnareDrumInstrumentNode
    + static_cast<uint32_t>(kDaisyDrumRackSlotCount);
constexpr uint32_t kDaisySyntheticBassDrumInstrumentNode =
    kDaisyHiHatInstrumentNode
    + static_cast<uint32_t>(kDaisyDrumRackSlotCount);
constexpr uint32_t kDaisySyntheticSnareDrumInstrumentNode =
    kDaisySyntheticBassDrumInstrumentNode
    + static_cast<uint32_t>(kDaisyDrumRackSlotCount);
constexpr uint32_t kMidiOutInstrumentNode = static_cast<uint32_t>(
    kMembraneRackSlotCount + kSn76489RackSlotCount
    + kYm2151RackSlotCount + kDaisyDrumNodeCount
    + kStereoSamplerRackSlotCount);
constexpr uint32_t kStereoSamplerInstrumentNode =
    kMidiOutInstrumentNode
    - static_cast<uint32_t>(kStereoSamplerRackSlotCount);
// PSG and YMFM remain in archived source, but are no longer selectable active
// instrument types.
constexpr std::size_t kInstrumentTypeCount = 8u;
constexpr std::size_t kMembraneParameterCount = 19u;
constexpr std::size_t kSn76489ParameterCount = 5u;
constexpr std::size_t kYm2151ParameterCount = 9u;
constexpr std::size_t kDaisyDrumParameterCapacity = 8u;
constexpr std::size_t kMembranePresetCount = 5u;
constexpr std::size_t kYm2151PresetCount = 6u;
constexpr std::size_t kDaisyDrumPresetCount = 4u;

enum class InstrumentKind : uint8_t {
    MembraneKick,
    Sn76489Psg,
    Ym2151Opm,
    DaisyAnalogBassDrum,
    DaisyAnalogSnareDrum,
    DaisyHiHat,
    DaisySyntheticBassDrum,
    DaisySyntheticSnareDrum,
    StereoSliceSampler,
    MidiOut,
};

struct RackInstrument {
    uint32_t nodeId = kInvalidInstrumentNode;
    InstrumentKind kind = InstrumentKind::MembraneKick;
    std::string_view name;
    std::string_view mnemonic;
    bool active = false;
};

struct InstrumentTypeDefinition {
    InstrumentKind kind = InstrumentKind::MembraneKick;
    std::string_view name;
    std::string_view mnemonic;
    std::size_t maximumInstances = 1u;
};

enum class Sn76489ParameterScale : uint8_t {
    Linear,
    Exponential,
};

struct Sn76489ParameterDefinition {
    uint32_t parameterId = 0u;
    std::string_view stableKey;
    std::string_view displayName;
    std::string_view unit;
    Sn76489ParameterScale scale = Sn76489ParameterScale::Linear;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
};

struct Sn76489Patch {
    std::array<float, kSn76489ParameterCount> normalized {};
};

enum class Ym2151ControlKind : uint8_t {
    Continuous,
    Stepped,
};

struct Ym2151ParameterDefinition {
    uint32_t parameterId = 0u;
    std::string_view stableKey;
    std::string_view displayName;
    Ym2151ControlKind control = Ym2151ControlKind::Continuous;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    uint32_t stepCount = 0u;
};

struct Ym2151Patch {
    std::array<float, kYm2151ParameterCount> normalized {};
};

enum class DaisyDrumParameterScale : uint8_t {
    Linear,
    Exponential,
};

struct DaisyDrumParameterDefinition {
    uint32_t parameterId = 0u;
    std::string_view stableKey;
    std::string_view displayName;
    std::string_view unit;
    DaisyDrumParameterScale scale = DaisyDrumParameterScale::Linear;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
};

struct DaisyDrumPatch {
    std::array<float, kDaisyDrumParameterCapacity> normalized {};
};

struct NamedInstrumentPreset {
    std::string_view name;
    std::string_view description;
};

enum class MidiInstrumentRouteKind : uint8_t {
    VirtualSource,
    Destination,
};

struct MidiInstrumentRoute {
    MidiInstrumentRouteKind kind = MidiInstrumentRouteKind::VirtualSource;
    int32_t destinationId = 0;
    // One-based owned CoreMIDI source. Every MIDI rack slot defaults to its
    // matching s3g Tracker 1..8 endpoint.
    uint8_t virtualSource = 1u;
    // User-facing MIDI channels are one based.
    uint8_t channel = 1u;
};

enum class MembraneInstrumentRole : uint8_t {
    Kick,
    SnareBody,
    FloorTom,
    LowTom,
    HighTom,
};

enum class MembraneParameterGroup : uint8_t {
    Body,
    Impact,
    Strike,
    Space,
    Response,
};

enum class MembraneControlKind : uint8_t {
    Continuous,
    Stepped,
};

struct MembraneParameterDefinition {
    uint32_t parameterId = 0u;
    std::string_view stableKey;
    std::string_view displayName;
    std::string_view unit;
    MembraneParameterGroup group = MembraneParameterGroup::Body;
    MembraneControlKind control = MembraneControlKind::Continuous;
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    uint32_t stepCount = 0u;
};

struct MembranePatch {
    // Values use the canonical Tracker normalized range. The CLAP adapter is
    // solely responsible for translating these to native parameter units.
    std::array<float, kMembraneParameterCount> normalized {};
};

struct MembraneInstrumentSlot {
    uint32_t nodeId = 0u;
    MembraneInstrumentRole role = MembraneInstrumentRole::Kick;
    MembranePatch basePatch;
};

struct StereoSamplerInstrumentSlot {
    uint32_t nodeId = kInvalidInstrumentNode;
    std::shared_ptr<const audio::StereoSampleAsset> asset;
    // Derived editor data: rebuilt from the asset and intentionally omitted
    // from song persistence/audio-graph comparison.
    std::shared_ptr<const audio::StereoSampleAnalysis> analysis;
    std::array<audio::SampleSlice, audio::kMaximumSamplerSlices> slices {};
    std::size_t sliceCount = 0u;
    uint8_t baseNote = 36u;
    audio::SamplerEnvelope envelope;
    std::string filePath;
};

struct InstrumentRackState {
    std::array<RackInstrument, kInstrumentRackSlotCount> instruments {};
    std::array<MembraneInstrumentSlot, kMembraneRackSlotCount> slots {};
    std::array<Sn76489Patch, kSn76489RackSlotCount> sn76489Patches {};
    std::array<Ym2151Patch, kYm2151RackSlotCount> ym2151Patches {};
    std::array<DaisyDrumPatch, kDaisyDrumNodeCount> daisyDrumPatches {};
    std::array<StereoSamplerInstrumentSlot, kStereoSamplerRackSlotCount>
        samplerSlots {};
    std::array<MidiInstrumentRoute, kMidiOutRackSlotCount> midiRoutes {};
    uint32_t selectedNode = 0u;
};

const RackInstrument* rackInstrument(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept;
const RackInstrument* rackInstrumentAt(const InstrumentRackState& rack,
    std::size_t rackIndex) noexcept;
std::size_t rackIndexForNode(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept;
std::size_t activeInstrumentCount(const InstrumentRackState& rack) noexcept;
std::size_t activeInstrumentCount(const InstrumentRackState& rack,
    InstrumentKind kind) noexcept;
uint32_t cycleActiveInstrument(const InstrumentRackState& rack,
    uint32_t nodeId, int direction) noexcept;
std::size_t instrumentTypeCount() noexcept;
const InstrumentTypeDefinition* instrumentType(
    std::size_t index) noexcept;
bool canAddInstrumentInstance(const InstrumentRackState& rack,
    InstrumentKind kind) noexcept;
bool addInstrumentInstance(InstrumentRackState& rack, InstrumentKind kind,
    std::size_t* rackIndex = nullptr, uint32_t* nodeId = nullptr) noexcept;
bool isSn76489InstrumentNode(uint32_t nodeId) noexcept;
std::size_t sn76489RackSlotIndex(uint32_t nodeId) noexcept;
uint32_t sn76489NodeForRackSlot(std::size_t slot) noexcept;
bool isYm2151InstrumentNode(uint32_t nodeId) noexcept;
std::size_t ym2151RackSlotIndex(uint32_t nodeId) noexcept;
uint32_t ym2151NodeForRackSlot(std::size_t slot) noexcept;
bool isDaisyDrumKind(InstrumentKind kind) noexcept;
bool isDaisyDrumInstrumentNode(uint32_t nodeId) noexcept;
InstrumentKind daisyDrumKindForNode(uint32_t nodeId) noexcept;
uint32_t daisyDrumFirstNode(InstrumentKind kind) noexcept;
std::size_t daisyDrumRackSlotIndex(uint32_t nodeId) noexcept;
std::size_t daisyDrumPatchIndex(uint32_t nodeId) noexcept;
uint32_t daisyDrumNodeForRackSlot(InstrumentKind kind,
    std::size_t slot) noexcept;
bool isStereoSamplerInstrumentNode(uint32_t nodeId) noexcept;
std::size_t stereoSamplerRackSlotIndex(uint32_t nodeId) noexcept;
uint32_t stereoSamplerNodeForRackSlot(std::size_t slot) noexcept;
bool isMidiOutInstrumentNode(uint32_t nodeId) noexcept;
std::size_t midiOutRackSlotIndex(uint32_t nodeId) noexcept;
uint32_t midiOutNodeForRackSlot(std::size_t slot) noexcept;
const RackInstrument* defaultRackInstrument(uint32_t nodeId) noexcept;
std::string_view instrumentKindName(InstrumentKind kind) noexcept;
bool instrumentRoutesToInternal(InstrumentKind kind) noexcept;
bool instrumentRoutesToMidi(InstrumentKind kind) noexcept;

std::size_t sn76489ParameterCount() noexcept;
const Sn76489ParameterDefinition* sn76489Parameter(
    std::size_t index) noexcept;
const Sn76489ParameterDefinition* findSn76489Parameter(
    uint32_t parameterId) noexcept;
std::size_t sn76489ParameterIndex(uint32_t parameterId) noexcept;
double sn76489NativeFromNormalized(uint32_t parameterId,
    float normalized) noexcept;
float sn76489NormalizedFromNative(uint32_t parameterId,
    double nativeValue) noexcept;
bool setSn76489BaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept;
float sn76489BaseParameter(const InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId) noexcept;

std::size_t ym2151ParameterCount() noexcept;
const Ym2151ParameterDefinition* ym2151Parameter(
    std::size_t index) noexcept;
std::size_t ym2151ParameterIndex(uint32_t parameterId) noexcept;
double ym2151NativeFromNormalized(uint32_t parameterId,
    float normalized) noexcept;
float ym2151NormalizedFromNative(uint32_t parameterId,
    double nativeValue) noexcept;
bool setYm2151BaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept;
float ym2151BaseParameter(const InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId) noexcept;
const NamedInstrumentPreset* ym2151Preset(std::size_t index) noexcept;
std::size_t ym2151PresetIndex(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept;
bool applyYm2151Preset(InstrumentRackState& rack, uint32_t nodeId,
    std::size_t presetIndex) noexcept;

std::size_t daisyDrumParameterCount(InstrumentKind kind) noexcept;
const DaisyDrumParameterDefinition* daisyDrumParameter(
    InstrumentKind kind, std::size_t index) noexcept;
const DaisyDrumParameterDefinition* findDaisyDrumParameter(
    InstrumentKind kind, uint32_t parameterId) noexcept;
std::size_t daisyDrumParameterIndex(InstrumentKind kind,
    uint32_t parameterId) noexcept;
double daisyDrumNativeFromNormalized(InstrumentKind kind,
    uint32_t parameterId, float normalized) noexcept;
float daisyDrumNormalizedFromNative(InstrumentKind kind,
    uint32_t parameterId, double nativeValue) noexcept;
bool setDaisyDrumBaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept;
float daisyDrumBaseParameter(const InstrumentRackState& rack,
    uint32_t nodeId, uint32_t parameterId) noexcept;
const NamedInstrumentPreset* daisyDrumPreset(InstrumentKind kind,
    std::size_t index) noexcept;
std::size_t daisyDrumPresetIndex(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept;
bool applyDaisyDrumPreset(InstrumentRackState& rack, uint32_t nodeId,
    std::size_t presetIndex) noexcept;

const MidiInstrumentRoute* midiInstrumentRoute(
    const InstrumentRackState& rack, uint32_t nodeId) noexcept;
bool setMidiInstrumentRoute(InstrumentRackState& rack, uint32_t nodeId,
    MidiInstrumentRoute route) noexcept;

std::size_t membraneParameterCount() noexcept;
const MembraneParameterDefinition* membraneParameter(
    std::size_t index) noexcept;
const MembraneParameterDefinition* findMembraneParameter(
    uint32_t parameterId) noexcept;
std::size_t membraneParameterIndex(uint32_t parameterId) noexcept;

double membraneNativeFromNormalized(uint32_t parameterId,
    float normalized) noexcept;
float membraneNormalizedFromNative(uint32_t parameterId,
    double nativeValue) noexcept;

std::string_view membraneInstrumentRoleName(
    MembraneInstrumentRole role) noexcept;
const NamedInstrumentPreset* membranePreset(std::size_t index) noexcept;
std::size_t membranePresetIndex(const InstrumentRackState& rack,
    uint32_t nodeId) noexcept;
bool applyMembranePreset(InstrumentRackState& rack, uint32_t nodeId,
    std::size_t presetIndex) noexcept;
InstrumentRackState makeDefaultInstrumentRack();
bool setMembraneBaseParameter(InstrumentRackState& rack, uint32_t nodeId,
    uint32_t parameterId, float normalized) noexcept;
float membraneBaseParameter(const InstrumentRackState& rack,
    uint32_t nodeId, uint32_t parameterId) noexcept;

} // namespace s3g::tracker
