#include "s3g_relay.h"
#include "../common/s3g_clap_gui_param_queue.h"

#include <clap/clap.h>
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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <limits>
#include <new>
#include <utility>

namespace {

using s3g::relay::Config;
using s3g::relay::Engine;
using s3g::relay::Event;
using s3g::relay::EventKind;
using s3g::relay::ArticulationMode;
using s3g::relay::PitchMode;
using s3g::relay::ReceptorTopology;

constexpr uint32_t kLegacyGlobalParamCount = 16u;
constexpr uint32_t kLegacyRelayParamCount = 11u;
constexpr uint32_t kLegacyParamCount = kLegacyGlobalParamCount
    + s3g::relay::kRelayCount * kLegacyRelayParamCount;
constexpr uint32_t kVersion3GlobalParamCount = 20u;
constexpr uint32_t kVersion3RelayParamCount = 12u;
constexpr uint32_t kVersion3ParamCount = kVersion3GlobalParamCount
    + s3g::relay::kRelayCount * kVersion3RelayParamCount;
constexpr uint32_t kVersion4GlobalParamCount = 20u;
constexpr uint32_t kVersion4RelayParamCount = 13u;
constexpr uint32_t kVersion4ParamCount = kVersion4GlobalParamCount
    + s3g::relay::kRelayCount * kVersion4RelayParamCount;
constexpr uint32_t kGlobalParamCount = 21u;
constexpr uint32_t kRelayParamCount = 13u;
constexpr uint32_t kParamCount = kGlobalParamCount
    + s3g::relay::kRelayCount * kRelayParamCount;
constexpr uint32_t kEventCapacity = 4096u;
constexpr uint32_t kStateMagic = 0x52454c59u; // RELY
constexpr uint32_t kStateVersion = 5u;
constexpr std::array<uint8_t, 10u> kVersion2ScaleToCanonical {{
    0u, 1u, 2u, 6u, 9u, 3u, 31u, 5u, 4u, 45u,
}};
constexpr uint32_t kGuiWidth = 1240u;
constexpr uint32_t kGuiHeight = 900u;
constexpr uint32_t kGuiMinimumWidth = 900u;
constexpr uint32_t kGuiMinimumHeight = 640u;
constexpr uint32_t kMidiTraceCapacity = 128u;
const Config kDefaultConfig {};
constexpr uint32_t kFactoryPresetCount = 8u;

const char* factoryPresetName(uint32_t index) noexcept
{
    static constexpr std::array<const char*, kFactoryPresetCount> names {{
        "INIT / FIXED", "DRUM ECOLOGY", "MUSE LOGIC",
        "ZILLION CASCADE", "HYBRID SAMPLE", "CRYSTALLINE CANON",
        "LONG-FORM DORIAN", "WHOLE-TONE DRIFT",
    }};
    return names[std::min<uint32_t>(index, kFactoryPresetCount - 1u)];
}

Config factoryPresetConfig(uint32_t index)
{
    Config config;
    index = std::min<uint32_t>(index, kFactoryPresetCount - 1u);
    const auto setPitchMode = [&](PitchMode mode) {
        for (auto& relay : config.relays) relay.pitchMode = mode;
    };
    const auto setArticulation = [&](ArticulationMode mode) {
        for (auto& relay : config.relays) relay.articulation = mode;
    };
    switch (index) {
    case 1u: // Drum Ecology
        config.latticeDepthIndex = 0u;
        config.activity = 0.74;
        config.coupling = 0.58;
        config.memory = 0.62;
        config.mutation = 0.12;
        config.hierarchy = 0.38;
        config.contrast = 0.66;
        config.clockRateIndex = 4u;
        config.formBars = 64u;
        config.dwellBars = 4u;
        config.transitionBars = 1.0;
        config.climate = 0.46;
        break;
    case 2u: // Muse Logic
        config.latticeDepthIndex = 1u;
        config.activity = 0.52;
        config.coupling = 0.42;
        config.memory = 0.88;
        config.mutation = 0.035;
        config.hierarchy = 0.76;
        config.contrast = 0.70;
        config.clockRateIndex = 3u;
        config.formBars = 128u;
        config.dwellBars = 8u;
        config.transitionBars = 2.0;
        config.climate = 0.38;
        config.scaleRoot = 0u;
        config.scaleOctave = 3;
        config.scale = 3u; // PENTATONIC MAJOR
        config.scaleRange = 2u;
        setPitchMode(PitchMode::ScaleLogic);
        setArticulation(ArticulationMode::Hold);
        break;
    case 3u: // Zillion Cascade
        config.latticeDepthIndex = 2u;
        config.activity = 0.68;
        config.coupling = 0.72;
        config.memory = 0.46;
        config.mutation = 0.24;
        config.hierarchy = 0.64;
        config.contrast = 0.78;
        config.clockRateIndex = 4u;
        config.formBars = 96u;
        config.dwellBars = 3u;
        config.transitionBars = 0.75;
        config.climate = 0.72;
        config.scaleRoot = 2u;
        config.scaleOctave = 2;
        config.scale = 6u; // DORIAN
        config.scaleRange = 3u;
        setPitchMode(PitchMode::ScaleLogic);
        setArticulation(ArticulationMode::Stack);
        for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay)
            config.relays[relay].topology = static_cast<ReceptorTopology>(
                2u + (relay & 1u));
        break;
    case 4u: // Hybrid Sample
        config.latticeDepthIndex = 1u;
        config.activity = 0.61;
        config.coupling = 0.54;
        config.memory = 0.70;
        config.mutation = 0.10;
        config.hierarchy = 0.58;
        config.contrast = 0.59;
        config.scaleRoot = 0u;
        config.scaleOctave = 3;
        config.scale = 31u; // PENTATONIC MINOR
        config.scaleRange = 2u;
        for (uint32_t relay = 4u; relay < s3g::relay::kRelayCount; ++relay)
            config.relays[relay].pitchMode = PitchMode::ScaleLogic;
        for (uint32_t relay = 4u; relay < s3g::relay::kRelayCount; ++relay)
            config.relays[relay].articulation = ArticulationMode::Stack;
        break;
    case 5u: // Crystalline Canon
        config.activity = 0.48;
        config.coupling = 0.40;
        config.memory = 1.0;
        config.mutation = 0.0;
        config.hierarchy = 0.82;
        config.contrast = 0.62;
        config.freeze = true;
        config.clockRateIndex = 3u;
        config.climate = 0.22;
        config.scaleRoot = 7u;
        config.scaleOctave = 3;
        config.scale = 1u; // MAJOR
        config.scaleRange = 2u;
        setPitchMode(PitchMode::ScaleLogic);
        setArticulation(ArticulationMode::Extend);
        break;
    case 6u: // Long-form Dorian
        config.activity = 0.56;
        config.coupling = 0.63;
        config.memory = 0.76;
        config.mutation = 0.11;
        config.hierarchy = 0.72;
        config.contrast = 0.54;
        config.clockRateIndex = 3u;
        config.formBars = 256u;
        config.dwellBars = 16u;
        config.transitionBars = 4.0;
        config.climate = 0.86;
        config.scaleRoot = 9u;
        config.scaleOctave = 2;
        config.scale = 6u; // DORIAN
        config.scaleRange = 3u;
        setPitchMode(PitchMode::ScaleLogic);
        setArticulation(ArticulationMode::Extend);
        break;
    case 7u: // Whole-tone Drift
        config.activity = 0.44;
        config.coupling = 0.67;
        config.memory = 0.57;
        config.mutation = 0.16;
        config.hierarchy = 0.46;
        config.contrast = 0.43;
        config.clockRateIndex = 2u;
        config.formBars = 192u;
        config.dwellBars = 12u;
        config.transitionBars = 6.0;
        config.climate = 0.78;
        config.scaleRoot = 1u;
        config.scaleOctave = 3;
        config.scale = 4u; // WHOLE TONE
        config.scaleRange = 3u;
        setPitchMode(PitchMode::ScaleLogic);
        setArticulation(ArticulationMode::Stack);
        for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay)
            config.relays[relay].topology = ReceptorTopology::Roaming;
        break;
    default:
        break;
    }
    return config;
}

enum GlobalParam : uint32_t {
    kEnabled = 0u,
    kActivity,
    kCoupling,
    kMemory,
    kMutation,
    kHierarchy,
    kContrast,
    kFreeze,
    kClockRate,
    kGate,
    kCcRate,
    kFormBars,
    kDwellBars,
    kTransitionBars,
    kClimate,
    kSeed,
    kScaleRoot,
    kScaleOctave,
    kScale,
    kScaleRange,
    kLatticeDepth,
};

enum RelayParam : uint32_t {
    kRelayEnabled = 0u,
    kRelayChannel,
    kRelayNote,
    kRelayCcA,
    kRelayCcB,
    kRelayThreshold,
    kRelayBias,
    kRelayRefractory,
    kRelayFeedback,
    kRelayGate,
    kRelayTopology,
    kRelayPitchMode,
    kRelayArticulation,
};

struct ParamSpec {
    clap_id id = CLAP_INVALID_ID;
    const char* name = "";
    const char* module = "";
    double minimum = 0.0;
    double maximum = 1.0;
    double defaultValue = 0.0;
    bool stepped = false;
};

clap_id paramIdForIndex(uint32_t index) noexcept
{
    if (index < kGlobalParamCount) return static_cast<clap_id>(index + 1u);
    index -= kGlobalParamCount;
    const uint32_t relay = index / kRelayParamCount;
    const uint32_t local = index % kRelayParamCount;
    return static_cast<clap_id>(100u + relay * 16u + local);
}

bool paramIndex(clap_id id, uint32_t& index) noexcept
{
    if (id >= 1u && id <= kGlobalParamCount) {
        index = static_cast<uint32_t>(id - 1u);
        return true;
    }
    if (id < 100u) return false;
    const uint32_t encoded = static_cast<uint32_t>(id - 100u);
    const uint32_t relay = encoded / 16u;
    const uint32_t local = encoded % 16u;
    if (relay >= s3g::relay::kRelayCount || local >= kRelayParamCount)
        return false;
    index = kGlobalParamCount + relay * kRelayParamCount + local;
    return true;
}

ParamSpec paramSpec(uint32_t index)
{
    static constexpr std::array<const char*, s3g::relay::kRelayCount>
        relayModules {{
            "Relay 1", "Relay 2", "Relay 3", "Relay 4",
            "Relay 5", "Relay 6", "Relay 7", "Relay 8",
        }};
    if (index < kGlobalParamCount) {
        switch (index) {
        case kEnabled: return { paramIdForIndex(index), "Enabled", "Conduct", 0.0, 1.0, kDefaultConfig.enabled ? 1.0 : 0.0, true };
        case kActivity: return { paramIdForIndex(index), "Energy", "Conduct", 0.0, 1.0, kDefaultConfig.activity, false };
        case kCoupling: return { paramIdForIndex(index), "Coupling", "Conduct", 0.0, 1.0, kDefaultConfig.coupling, false };
        case kMemory: return { paramIdForIndex(index), "Memory", "Conduct", 0.0, 1.0, kDefaultConfig.memory, false };
        case kMutation: return { paramIdForIndex(index), "Mutation", "Conduct", 0.0, 1.0, kDefaultConfig.mutation, false };
        case kHierarchy: return { paramIdForIndex(index), "Hierarchy", "Conduct", 0.0, 1.0, kDefaultConfig.hierarchy, false };
        case kContrast: return { paramIdForIndex(index), "Contrast", "Conduct", 0.0, 1.0, kDefaultConfig.contrast, false };
        case kFreeze: return { paramIdForIndex(index), "Freeze", "Conduct", 0.0, 1.0, kDefaultConfig.freeze ? 1.0 : 0.0, true };
        case kClockRate: return { paramIdForIndex(index), "Clock Rate", "Climate", 0.0, 6.0, static_cast<double>(kDefaultConfig.clockRateIndex), true };
        case kGate: return { paramIdForIndex(index), "Base Gate", "Climate", 0.005, 8.0, kDefaultConfig.gateBeats, false };
        case kCcRate: return { paramIdForIndex(index), "CC Interval", "Climate", 0.0, 5.0, static_cast<double>(kDefaultConfig.ccRateIndex), true };
        case kFormBars: return { paramIdForIndex(index), "Form Cycle", "Climate", 16.0, 512.0, static_cast<double>(kDefaultConfig.formBars), true };
        case kDwellBars: return { paramIdForIndex(index), "Cell Dwell", "Climate", 1.0, 64.0, static_cast<double>(kDefaultConfig.dwellBars), true };
        case kTransitionBars: return { paramIdForIndex(index), "Gestation", "Climate", 0.0, 32.0, kDefaultConfig.transitionBars, false };
        case kClimate: return { paramIdForIndex(index), "Climate", "Climate", 0.0, 1.0, kDefaultConfig.climate, false };
        case kSeed: return { paramIdForIndex(index), "Seed", "Climate", 1.0, 65535.0, static_cast<double>(kDefaultConfig.seed), true };
        case kScaleRoot: return { paramIdForIndex(index), "Scale Root", "Pitch", 0.0, 11.0, static_cast<double>(kDefaultConfig.scaleRoot), true };
        case kScaleOctave: return { paramIdForIndex(index), "Root Octave", "Pitch", -1.0, 7.0, static_cast<double>(kDefaultConfig.scaleOctave), true };
        case kScale: return { paramIdForIndex(index), "Musical Scale", "Pitch", 0.0, static_cast<double>(s3g::kMusicalScaleCount - 1u), static_cast<double>(kDefaultConfig.scale), true };
        case kScaleRange: return { paramIdForIndex(index), "Scale Range", "Pitch", 1.0, 4.0, static_cast<double>(kDefaultConfig.scaleRange), true };
        case kLatticeDepth: return { paramIdForIndex(index), "Lattice Depth", "Climate", 0.0, 2.0, static_cast<double>(kDefaultConfig.latticeDepthIndex), true };
        default: break;
        }
    }

    const uint32_t relayIndex = (index - kGlobalParamCount) / kRelayParamCount;
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    const auto& relay = kDefaultConfig.relays[relayIndex];
    const char* module = relayModules[relayIndex];
    switch (local) {
    case kRelayEnabled: return { paramIdForIndex(index), "Enabled", module, 0.0, 1.0, relay.enabled ? 1.0 : 0.0, true };
    case kRelayChannel: return { paramIdForIndex(index), "MIDI Channel", module, 1.0, 16.0, static_cast<double>(relay.channel + 1u), true };
    case kRelayNote: return { paramIdForIndex(index), "MIDI Note", module, 0.0, 128.0, static_cast<double>(relay.note), true };
    case kRelayCcA: return { paramIdForIndex(index), "CC A", module, 0.0, 128.0, static_cast<double>(relay.ccA), true };
    case kRelayCcB: return { paramIdForIndex(index), "CC B", module, 0.0, 128.0, static_cast<double>(relay.ccB), true };
    case kRelayThreshold: return { paramIdForIndex(index), "Threshold", module, 0.0, 1.0, relay.threshold, false };
    case kRelayBias: return { paramIdForIndex(index), "Bias", module, -1.0, 1.0, relay.bias, false };
    case kRelayRefractory: return { paramIdForIndex(index), "Refractory", module, 1.0, 32.0, static_cast<double>(relay.refractoryTicks), true };
    case kRelayFeedback: return { paramIdForIndex(index), "Feedback", module, -1.0, 1.0, relay.feedback, false };
    case kRelayGate: return { paramIdForIndex(index), "Gate Scale", module, 0.1, 4.0, relay.gateScale, false };
    case kRelayTopology: return { paramIdForIndex(index), "Receptor", module, 0.0, 3.0, static_cast<double>(relay.topology), true };
    case kRelayPitchMode: return { paramIdForIndex(index), "Pitch Mode", module, 0.0, 1.0, static_cast<double>(relay.pitchMode), true };
    case kRelayArticulation: return { paramIdForIndex(index), "Articulation", module, 0.0, 3.0, static_cast<double>(relay.articulation), true };
    default: return {};
    }
}

struct SavedState {
    uint32_t magic = kStateMagic;
    uint32_t version = kStateVersion;
    std::array<double, kParamCount> values {};
};

struct StateHeader {
    uint32_t magic = 0u;
    uint32_t version = 0u;
};

struct MidiTraceSlot {
    std::atomic<uint64_t> sequence { 0u };
    std::atomic<double> beat { 0.0 };
    std::atomic<uint32_t> packed { 0u };
};

struct Plugin {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    const clap_host_params_t* hostParams = nullptr;
    double sampleRate = 48000.0;
    Config config {};
    Engine engine {};
    std::array<Event, kEventCapacity> eventBuffer {};
    std::array<std::atomic<double>, kParamCount> publishedParams {};
    s3g::clap_gui::ParamEventQueue<> guiParamEvents {};
    std::array<std::atomic<double>, s3g::relay::kNodeCount> visualNodes {};
    std::array<std::atomic<double>, s3g::relay::kClusterCount> visualClusters {};
    std::array<std::atomic<double>, s3g::relay::kRelayCount> visualReceptors {};
    std::array<std::atomic<double>,
        s3g::relay::kClusterCount * s3g::relay::kClusterCount>
        visualPlasticity {};
    std::array<std::atomic<uint32_t>, s3g::relay::kTrailLength> visualTrail {};
    std::atomic<double> visualBeat { 0.0 };
    std::atomic<double> visualEnergy { 0.0 };
    std::atomic<double> visualClimateBlend { 1.0 };
    std::atomic<double> visualCyclePhase { 0.0 };
    std::atomic<int64_t> visualCycleIndex { 0 };
    std::atomic<uint32_t> visualRegister { 0u };
    std::atomic<uint32_t> visualCell { 5u };
    std::atomic<uint32_t> visualPreviousCell { 5u };
    std::atomic<uint32_t> visualTrailCount { 1u };
    std::atomic<bool> visualPlaying { false };
    std::atomic<uint64_t> sentEvents { 0u };
    std::atomic<uint64_t> droppedEvents { 0u };
    std::atomic<double> thawMemory { kDefaultConfig.memory };
    std::atomic<bool> hasThawMemory { false };
    std::atomic<bool> formHold { false };
    std::array<MidiTraceSlot, kMidiTraceCapacity> midiTrace {};
    std::atomic<uint64_t> midiTraceWrite { 0u };
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

double configValue(const Config& config, uint32_t index) noexcept
{
    if (index < kGlobalParamCount) {
        switch (index) {
        case kEnabled: return config.enabled ? 1.0 : 0.0;
        case kActivity: return config.activity;
        case kCoupling: return config.coupling;
        case kMemory: return config.memory;
        case kMutation: return config.mutation;
        case kHierarchy: return config.hierarchy;
        case kContrast: return config.contrast;
        case kFreeze: return config.freeze ? 1.0 : 0.0;
        case kClockRate: return static_cast<double>(config.clockRateIndex);
        case kGate: return config.gateBeats;
        case kCcRate: return static_cast<double>(config.ccRateIndex);
        case kFormBars: return static_cast<double>(config.formBars);
        case kDwellBars: return static_cast<double>(config.dwellBars);
        case kTransitionBars: return config.transitionBars;
        case kClimate: return config.climate;
        case kSeed: return static_cast<double>(config.seed);
        case kScaleRoot: return static_cast<double>(config.scaleRoot);
        case kScaleOctave: return static_cast<double>(config.scaleOctave);
        case kScale: return static_cast<double>(config.scale);
        case kScaleRange: return static_cast<double>(config.scaleRange);
        case kLatticeDepth: return static_cast<double>(config.latticeDepthIndex);
        default: return 0.0;
        }
    }
    const uint32_t relayIndex = (index - kGlobalParamCount) / kRelayParamCount;
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    const auto& relay = config.relays[relayIndex];
    switch (local) {
    case kRelayEnabled: return relay.enabled ? 1.0 : 0.0;
    case kRelayChannel: return static_cast<double>(relay.channel + 1u);
    case kRelayNote: return static_cast<double>(relay.note);
    case kRelayCcA: return static_cast<double>(relay.ccA);
    case kRelayCcB: return static_cast<double>(relay.ccB);
    case kRelayThreshold: return relay.threshold;
    case kRelayBias: return relay.bias;
    case kRelayRefractory: return static_cast<double>(relay.refractoryTicks);
    case kRelayFeedback: return relay.feedback;
    case kRelayGate: return relay.gateScale;
    case kRelayTopology: return static_cast<double>(relay.topology);
    case kRelayPitchMode: return static_cast<double>(relay.pitchMode);
    case kRelayArticulation: return static_cast<double>(relay.articulation);
    default: return 0.0;
    }
}

double publishedValue(const Plugin& plugin, uint32_t index) noexcept
{
    return index < kParamCount
        ? plugin.publishedParams[index].load(std::memory_order_acquire) : 0.0;
}

void publishAll(Plugin& plugin) noexcept
{
    for (uint32_t index = 0u; index < kParamCount; ++index)
        plugin.publishedParams[index].store(configValue(plugin.config, index),
            std::memory_order_release);
}

void applyParam(Plugin& plugin, clap_id id, double value) noexcept
{
    uint32_t index = 0u;
    if (!paramIndex(id, index) || !std::isfinite(value)) return;
    const ParamSpec spec = paramSpec(index);
    value = std::clamp(value, spec.minimum, spec.maximum);
    if (spec.stepped) value = std::round(value);
    bool invalidate = false;
    if (index < kGlobalParamCount) {
        switch (index) {
        case kEnabled: plugin.config.enabled = value >= 0.5; invalidate = true; break;
        case kActivity: plugin.config.activity = value; break;
        case kCoupling: plugin.config.coupling = value; break;
        case kMemory: plugin.config.memory = value; break;
        case kMutation: plugin.config.mutation = value; break;
        case kHierarchy: plugin.config.hierarchy = value; break;
        case kContrast: plugin.config.contrast = value; break;
        case kFreeze: plugin.config.freeze = value >= 0.5; break;
        case kClockRate: plugin.config.clockRateIndex = static_cast<uint32_t>(value); invalidate = true; break;
        case kGate: plugin.config.gateBeats = value; break;
        case kCcRate: plugin.config.ccRateIndex = static_cast<uint32_t>(value); break;
        case kFormBars: plugin.config.formBars = static_cast<uint32_t>(value); invalidate = true; break;
        case kDwellBars: plugin.config.dwellBars = static_cast<uint32_t>(value); invalidate = true; break;
        case kTransitionBars: plugin.config.transitionBars = value; break;
        case kClimate: plugin.config.climate = value; break;
        case kSeed: plugin.config.seed = static_cast<uint32_t>(value); invalidate = true; break;
        case kScaleRoot: plugin.config.scaleRoot = static_cast<uint32_t>(value); invalidate = true; break;
        case kScaleOctave: plugin.config.scaleOctave = static_cast<int32_t>(value); invalidate = true; break;
        case kScale: plugin.config.scale = static_cast<uint32_t>(value); invalidate = true; break;
        case kScaleRange: plugin.config.scaleRange = static_cast<uint32_t>(value); invalidate = true; break;
        case kLatticeDepth: plugin.config.latticeDepthIndex = static_cast<uint32_t>(value); invalidate = true; break;
        default: break;
        }
    } else {
        const uint32_t relayIndex = (index - kGlobalParamCount) / kRelayParamCount;
        const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
        auto& relay = plugin.config.relays[relayIndex];
        switch (local) {
        case kRelayEnabled: relay.enabled = value >= 0.5; break;
        case kRelayChannel: relay.channel = static_cast<uint8_t>(value - 1.0); break;
        case kRelayNote: relay.note = static_cast<uint16_t>(value); break;
        case kRelayCcA: relay.ccA = static_cast<uint16_t>(value); break;
        case kRelayCcB: relay.ccB = static_cast<uint16_t>(value); break;
        case kRelayThreshold: relay.threshold = value; break;
        case kRelayBias: relay.bias = value; break;
        case kRelayRefractory: relay.refractoryTicks = static_cast<uint32_t>(value); break;
        case kRelayFeedback: relay.feedback = value; break;
        case kRelayGate: relay.gateScale = value; break;
        case kRelayTopology: relay.topology = static_cast<ReceptorTopology>(static_cast<uint32_t>(value)); break;
        case kRelayPitchMode: relay.pitchMode = static_cast<PitchMode>(static_cast<uint32_t>(value)); invalidate = true; break;
        case kRelayArticulation: relay.articulation = static_cast<ArticulationMode>(static_cast<uint32_t>(value)); invalidate = true; break;
        default: break;
        }
    }
    if (invalidate) plugin.engine.invalidate();
    plugin.publishedParams[index].store(configValue(plugin.config, index),
        std::memory_order_release);
}

bool init(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (instance->host && instance->host->get_extension) {
        instance->hostParams = static_cast<const clap_host_params_t*>(
            instance->host->get_extension(instance->host, CLAP_EXT_PARAMS));
    }
    return true;
}

void requestGuiParamService(Plugin& plugin)
{
    if (plugin.hostParams && plugin.hostParams->request_flush)
        plugin.hostParams->request_flush(plugin.host);
    else if (plugin.host && plugin.host->request_process)
        plugin.host->request_process(plugin.host);
}

bool queueGuiParamEvent(Plugin& plugin,
    s3g::clap_gui::ParamEventKind kind, clap_id id, double value = 0.0)
{
    if (!plugin.guiParamEvents.push({ kind, id, value })) return false;
    requestGuiParamService(plugin);
    return true;
}

void queueGuiBegin(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureBegin, id);
}

void queueGuiValue(Plugin& plugin, clap_id id, double value)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::Value, id, value);
}

void queueGuiEnd(Plugin& plugin, clap_id id)
{
    (void)queueGuiParamEvent(plugin,
        s3g::clap_gui::ParamEventKind::GestureEnd, id);
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
    auto* instance = self(plugin);
    instance->sampleRate = std::isfinite(sampleRate) && sampleRate > 0.0
        ? sampleRate : 48000.0;
    instance->engine.reset();
    instance->midiTraceWrite.store(0u, std::memory_order_release);
    for (auto& slot : instance->midiTrace)
        slot.sequence.store(0u, std::memory_order_release);
    return true;
}

void deactivate(const clap_plugin_t* plugin) { self(plugin)->engine.reset(); }
bool startProcessing(const clap_plugin_t*) { return true; }
void stopProcessing(const clap_plugin_t*) {}
void reset(const clap_plugin_t* plugin) { self(plugin)->engine.reset(); }

void readParamEvents(Plugin& plugin, const clap_input_events_t* input)
{
    if (!input || !input->size || !input->get) return;
    const uint32_t count = input->size(input);
    for (uint32_t index = 0u; index < count; ++index) {
        const clap_event_header_t* header = input->get(input, index);
        if (!header || header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE
            || header->size < sizeof(clap_event_param_value_t)) continue;
        const auto* event = reinterpret_cast<
            const clap_event_param_value_t*>(header);
        applyParam(plugin, event->param_id, event->value);
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
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
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

void serviceGuiParamEvents(Plugin& plugin,
    const clap_output_events_t* output)
{
    s3g::clap_gui::ParamEvent pending {};
    while (plugin.guiParamEvents.peek(pending)) {
        if (!pushGuiParamEvent(output, pending)) break;
        if (pending.kind == s3g::clap_gui::ParamEventKind::Value)
            applyParam(plugin, pending.paramId, pending.value);
        plugin.guiParamEvents.pop();
    }
}

struct HostTransport {
    bool playing = false;
    bool hasBeat = false;
    double beat = 0.0;
    double tempo = 120.0;
    double beatsPerBar = 4.0;
};

HostTransport readHostTransport(const clap_event_transport_t* transport)
{
    HostTransport result;
    if (!transport) return result;
    result.playing = (transport->flags & CLAP_TRANSPORT_IS_PLAYING) != 0u;
    if ((transport->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0u
        && std::isfinite(transport->tempo) && transport->tempo > 0.0)
        result.tempo = transport->tempo;
    if ((transport->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0u) {
        result.beat = static_cast<double>(transport->song_pos_beats)
            / static_cast<double>(CLAP_BEATTIME_FACTOR);
        result.hasBeat = std::isfinite(result.beat);
    } else if ((transport->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) != 0u) {
        const double seconds = static_cast<double>(transport->song_pos_seconds)
            / static_cast<double>(CLAP_SECTIME_FACTOR);
        result.beat = seconds * result.tempo / 60.0;
        result.hasBeat = std::isfinite(result.beat);
    }
    if ((transport->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0u
        && transport->tsig_denom != 0u) {
        result.beatsPerBar = static_cast<double>(transport->tsig_num) * 4.0
            / static_cast<double>(transport->tsig_denom);
    }
    return result;
}

void recordMidiTrace(Plugin& plugin, const Event& event) noexcept
{
    const uint64_t ticket = plugin.midiTraceWrite.fetch_add(
        1u, std::memory_order_relaxed);
    auto& slot = plugin.midiTrace[static_cast<std::size_t>(
        ticket % kMidiTraceCapacity)];
    slot.sequence.store(0u, std::memory_order_release);
    const uint32_t packed = static_cast<uint32_t>(event.kind)
        | (static_cast<uint32_t>(event.relay & 7u) << 2u)
        | (static_cast<uint32_t>(event.channel & 15u) << 5u)
        | (static_cast<uint32_t>(event.data1 & 127u) << 9u)
        | (static_cast<uint32_t>(event.data2 & 127u) << 16u);
    slot.beat.store(event.beat, std::memory_order_relaxed);
    slot.packed.store(packed, std::memory_order_relaxed);
    slot.sequence.store(ticket + 1u, std::memory_order_release);
}

bool pushMidi(Plugin& plugin, const clap_output_events_t* output,
    uint32_t frame, const Event& source)
{
    if (!output || !output->try_push) {
        plugin.droppedEvents.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    clap_event_midi_t event {};
    event.header.size = sizeof(event);
    event.header.time = frame;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.port_index = 0u;
    uint8_t status = 0xb0u;
    if (source.kind == EventKind::NoteOff) status = 0x80u;
    else if (source.kind == EventKind::NoteOn) status = 0x90u;
    event.data[0] = static_cast<uint8_t>(status | source.channel);
    event.data[1] = source.data1;
    event.data[2] = source.data2;
    if (!output->try_push(output, &event.header)) {
        plugin.droppedEvents.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    recordMidiTrace(plugin, source);
    plugin.sentEvents.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

clap_process_status process(const clap_plugin_t* plugin,
    const clap_process_t* processData)
{
    if (!processData) return CLAP_PROCESS_ERROR;
    auto& instance = *self(plugin);
    readParamEvents(instance, processData->in_events);
    serviceGuiParamEvents(instance, processData->out_events);

    const HostTransport transport = readHostTransport(processData->transport);
    const bool playing = transport.playing && transport.hasBeat;
    const double beginBeat = transport.hasBeat
        ? transport.beat
        : instance.visualBeat.load(std::memory_order_relaxed);
    const double endBeat = beginBeat + transport.tempo
        * static_cast<double>(processData->frames_count)
        / (60.0 * instance.sampleRate);
    Config processConfig = instance.config;
    processConfig.formHold = instance.formHold.load(std::memory_order_acquire);
    const auto result = instance.engine.process(beginBeat, endBeat,
        transport.beatsPerBar, playing, processConfig,
        instance.eventBuffer.data(), kEventCapacity);
    instance.droppedEvents.fetch_add(result.dropped,
        std::memory_order_relaxed);

    for (uint32_t index = 0u; index < result.count; ++index) {
        const Event& event = instance.eventBuffer[index];
        uint32_t frame = 0u;
        if (processData->frames_count > 0u && event.beat > beginBeat) {
            const double position = (event.beat - beginBeat)
                * 60.0 * instance.sampleRate / transport.tempo;
            frame = static_cast<uint32_t>(std::clamp(
                static_cast<int64_t>(std::llround(position)), int64_t(0),
                static_cast<int64_t>(processData->frames_count - 1u)));
        }
        (void)pushMidi(instance, processData->out_events, frame, event);
    }

    for (uint32_t index = 0u; index < s3g::relay::kNodeCount; ++index)
        instance.visualNodes[index].store(result.snapshot.nodes[index],
            std::memory_order_relaxed);
    for (uint32_t index = 0u; index < s3g::relay::kClusterCount; ++index)
        instance.visualClusters[index].store(result.snapshot.clusters[index],
            std::memory_order_relaxed);
    for (uint32_t index = 0u; index < s3g::relay::kRelayCount; ++index)
        instance.visualReceptors[index].store(result.snapshot.receptors[index],
            std::memory_order_relaxed);
    for (uint32_t index = 0u;
         index < s3g::relay::kClusterCount * s3g::relay::kClusterCount;
         ++index)
        instance.visualPlasticity[index].store(
            result.snapshot.plasticity[index], std::memory_order_relaxed);
    for (uint32_t index = 0u; index < s3g::relay::kTrailLength; ++index)
        instance.visualTrail[index].store(result.snapshot.trail[index],
            std::memory_order_relaxed);
    instance.visualBeat.store(beginBeat, std::memory_order_relaxed);
    instance.visualEnergy.store(result.snapshot.energy,
        std::memory_order_relaxed);
    instance.visualClimateBlend.store(result.snapshot.climateBlend,
        std::memory_order_relaxed);
    instance.visualCyclePhase.store(result.snapshot.cyclePhase,
        std::memory_order_relaxed);
    instance.visualCycleIndex.store(result.snapshot.cycleIndex,
        std::memory_order_relaxed);
    instance.visualRegister.store(result.snapshot.registerBits,
        std::memory_order_relaxed);
    instance.visualCell.store(result.snapshot.currentCell,
        std::memory_order_relaxed);
    instance.visualPreviousCell.store(result.snapshot.previousCell,
        std::memory_order_relaxed);
    instance.visualTrailCount.store(result.snapshot.trailCount,
        std::memory_order_relaxed);
    instance.visualPlaying.store(playing, std::memory_order_relaxed);
    return CLAP_PROCESS_CONTINUE;
}

void onMainThread(const clap_plugin_t*) {}

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput,
    clap_note_port_info_t* info)
{
    (void)isInput;
    if (!info || index != 0u) return false;
    *info = {};
    info->id = 100u;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP
        | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "%s", "Relay MIDI Output");
    return true;
}

const clap_plugin_note_ports_t notePorts { notePortsCount, notePortsGet };

uint32_t paramsCount(const clap_plugin_t*) { return kParamCount; }

bool paramsGetInfo(const clap_plugin_t*, uint32_t index,
    clap_param_info_t* info)
{
    if (!info || index >= kParamCount) return false;
    const ParamSpec spec = paramSpec(index);
    *info = {};
    info->id = spec.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE
        | (spec.stepped ? CLAP_PARAM_IS_STEPPED : 0u);
    std::snprintf(info->name, sizeof(info->name), "%s", spec.name);
    std::snprintf(info->module, sizeof(info->module), "%s", spec.module);
    info->min_value = spec.minimum;
    info->max_value = spec.maximum;
    info->default_value = spec.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    uint32_t index = 0u;
    if (!value || !paramIndex(id, index)) return false;
    *value = publishedValue(*self(plugin), index);
    return true;
}

void midiNoteText(int value, char* display, uint32_t size)
{
    static constexpr std::array<const char*, 12u> names {{
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B",
    }};
    value = std::clamp(value, 0, 127);
    std::snprintf(display, size, "%s%d (%d)",
        names[static_cast<std::size_t>(value % 12)], value / 12 - 1, value);
}

const char* scaleRootName(uint32_t root) noexcept
{
    static constexpr std::array<const char*, 12u> names {{
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B",
    }};
    return names[root % names.size()];
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value,
    char* display, uint32_t size)
{
    uint32_t index = 0u;
    if (!display || size == 0u || !paramIndex(id, index)) return false;
    const uint32_t local = index >= kGlobalParamCount
        ? (index - kGlobalParamCount) % kRelayParamCount
        : kRelayParamCount;
    if (index == kEnabled || index == kFreeze
        || (index >= kGlobalParamCount && local == kRelayEnabled)) {
        std::snprintf(display, size, "%s", value >= 0.5 ? "On" : "Off");
        return true;
    }
    if (index == kClockRate) {
        static constexpr std::array<const char*, 7u> rate {{
            "1 / 4 beats", "1 / 2 beats", "1 / beat", "2 / beat",
            "4 / beat", "8 / beat", "16 / beat",
        }};
        const auto i = std::min<std::size_t>(
            static_cast<std::size_t>(std::llround(value)), rate.size() - 1u);
        std::snprintf(display, size, "%s", rate[i]);
        return true;
    }
    if (index == kCcRate) {
        std::snprintf(display, size, "Every %u ticks",
            s3g::relay::ccEveryTicks(
                static_cast<uint32_t>(std::llround(value))));
        return true;
    }
    if (index == kScaleRoot) {
        std::snprintf(display, size, "%s", scaleRootName(
            static_cast<uint32_t>(std::llround(value))));
        return true;
    }
    if (index == kScaleOctave) {
        std::snprintf(display, size, "%.0f", value);
        return true;
    }
    if (index == kScale) {
        std::snprintf(display, size, "%s", s3g::musicalScaleDefinition(
            static_cast<uint32_t>(std::llround(value))).name);
        return true;
    }
    if (index == kScaleRange) {
        std::snprintf(display, size, "%.0f octave%s", value,
            value < 1.5 ? "" : "s");
        return true;
    }
    if (index == kLatticeDepth) {
        std::snprintf(display, size, "%s", s3g::relay::latticeDepthName(
            static_cast<uint32_t>(std::llround(value))));
        return true;
    }
    if (index < kGlobalParamCount) {
        switch (index) {
        case kActivity:
        case kCoupling:
        case kMemory:
        case kMutation:
        case kHierarchy:
        case kContrast:
        case kClimate:
            std::snprintf(display, size, "%.0f%%", value * 100.0); break;
        case kGate: std::snprintf(display, size, "%.3g beats", value); break;
        case kFormBars:
        case kDwellBars: std::snprintf(display, size, "%.0f bars", value); break;
        case kTransitionBars: std::snprintf(display, size, "%.2g bars", value); break;
        case kSeed: std::snprintf(display, size, "%.0f", value); break;
        default: std::snprintf(display, size, "%.3g", value); break;
        }
        return true;
    }

    switch (local) {
    case kRelayChannel: std::snprintf(display, size, "Ch %.0f", value); break;
    case kRelayNote:
        if (value >= 127.5) std::snprintf(display, size, "Off");
        else midiNoteText(static_cast<int>(std::llround(value)), display, size);
        break;
    case kRelayCcA:
    case kRelayCcB:
        if (value >= 127.5) std::snprintf(display, size, "Off");
        else std::snprintf(display, size, "CC %.0f", value);
        break;
    case kRelayThreshold:
        std::snprintf(display, size, "%.0f%%", value * 100.0); break;
    case kRelayBias:
    case kRelayFeedback:
        std::snprintf(display, size, "%+.0f%%", value * 100.0); break;
    case kRelayRefractory:
        std::snprintf(display, size, "%.0f ticks", value); break;
    case kRelayGate:
        std::snprintf(display, size, "%.2gx", value); break;
    case kRelayTopology:
        std::snprintf(display, size, "%s", s3g::relay::topologyName(
            static_cast<ReceptorTopology>(static_cast<uint32_t>(
                std::llround(value))))); break;
    case kRelayPitchMode:
        std::snprintf(display, size, "%s", s3g::relay::pitchModeName(
            static_cast<PitchMode>(static_cast<uint32_t>(
                std::llround(value))))); break;
    case kRelayArticulation:
        std::snprintf(display, size, "%s", s3g::relay::articulationModeName(
            static_cast<ArticulationMode>(static_cast<uint32_t>(
                std::llround(value))))); break;
    default: std::snprintf(display, size, "%.3g", value); break;
    }
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* input,
    double* value)
{
    uint32_t index = 0u;
    if (!input || !value || !paramIndex(id, index)) return false;
    const uint32_t local = index >= kGlobalParamCount
        ? (index - kGlobalParamCount) % kRelayParamCount
        : kRelayParamCount;
    if (std::strcmp(input, "Off") == 0 || std::strcmp(input, "off") == 0) {
        *value = index >= kGlobalParamCount
                && (local == kRelayNote || local == kRelayCcA
                    || local == kRelayCcB)
            ? 128.0 : 0.0;
        return true;
    }
    if (std::strcmp(input, "On") == 0 || std::strcmp(input, "on") == 0) {
        *value = 1.0;
        return true;
    }
    if (index == kClockRate) {
        static constexpr std::array<const char*, 7u> names {{
            "1 / 4 beats", "1 / 2 beats", "1 / beat", "2 / beat",
            "4 / beat", "8 / beat", "16 / beat",
        }};
        for (uint32_t candidate = 0u; candidate < names.size(); ++candidate) {
            if (strcasecmp(input, names[candidate]) == 0) {
                *value = static_cast<double>(candidate);
                return true;
            }
        }
    }
    if (index == kCcRate) {
        const char* number = input;
        while (*number && !std::isdigit(static_cast<unsigned char>(*number)))
            ++number;
        char* end = nullptr;
        const double parsed = std::strtod(number, &end);
        if (end == number || !std::isfinite(parsed)) return false;
        uint32_t closest = 0u;
        double distance = std::numeric_limits<double>::infinity();
        for (uint32_t candidate = 0u; candidate < 6u; ++candidate) {
            const double difference = std::abs(parsed
                - static_cast<double>(s3g::relay::ccEveryTicks(candidate)));
            if (difference < distance) {
                closest = candidate;
                distance = difference;
            }
        }
        *value = static_cast<double>(closest);
        return true;
    }
    if (index == kScaleRoot) {
        for (uint32_t candidate = 0u; candidate < 12u; ++candidate) {
            if (strcasecmp(input, scaleRootName(candidate)) == 0) {
                *value = static_cast<double>(candidate);
                return true;
            }
        }
    }
    if (index == kScale) {
        if (strcasecmp(input, "Natural Minor") == 0) {
            *value = 2.0;
            return true;
        }
        if (strcasecmp(input, "Octatonic") == 0) {
            *value = 45.0;
            return true;
        }
        uint32_t scale = 0u;
        if (s3g::musicalScaleValueFromText(input, scale)) {
            *value = static_cast<double>(scale);
            return true;
        }
    }
    if (index == kLatticeDepth) {
        static constexpr std::array<const char*, 3u> names {{
            "Sheet", "2 Planes", "4 Planes",
        }};
        for (uint32_t candidate = 0u; candidate < names.size(); ++candidate) {
            if (strcasecmp(input, names[candidate]) == 0) {
                *value = static_cast<double>(candidate);
                return true;
            }
        }
    }
    if (index >= kGlobalParamCount && local == kRelayTopology) {
        static constexpr std::array<const char*, 4u> names {{
            "Local", "Cross", "Diffuse", "Roaming",
        }};
        for (uint32_t candidate = 0u; candidate < names.size(); ++candidate) {
            if (strcasecmp(input, names[candidate]) == 0) {
                *value = static_cast<double>(candidate);
                return true;
            }
        }
    }
    if (index >= kGlobalParamCount && local == kRelayPitchMode) {
        static constexpr std::array<const char*, 2u> names {{
            "Fixed", "Scale Logic",
        }};
        for (uint32_t candidate = 0u; candidate < names.size(); ++candidate) {
            if (strcasecmp(input, names[candidate]) == 0) {
                *value = static_cast<double>(candidate);
                return true;
            }
        }
    }
    if (index >= kGlobalParamCount && local == kRelayArticulation) {
        static constexpr std::array<const char*, 4u> names {{
            "Restart", "Hold", "Extend", "Stack",
        }};
        for (uint32_t candidate = 0u; candidate < names.size(); ++candidate) {
            if (strcasecmp(input, names[candidate]) == 0) {
                *value = static_cast<double>(candidate);
                return true;
            }
        }
    }
    const char* number = input;
    if (index >= kGlobalParamCount && local == kRelayNote) {
        if (const char* open = std::strrchr(input, '(')) number = open + 1;
    } else {
        while (*number && !std::isdigit(static_cast<unsigned char>(*number))
            && *number != '+' && *number != '-' && *number != '.') ++number;
    }
    char* end = nullptr;
    double parsed = std::strtod(number, &end);
    if (end == number || !std::isfinite(parsed)) return false;
    const bool percent = std::strchr(input, '%') != nullptr
        && ((index >= kActivity && index <= kContrast)
            || index == kClimate
            || (index >= kGlobalParamCount
                && (local == kRelayThreshold || local == kRelayBias
                    || local == kRelayFeedback)));
    if (percent) parsed *= 0.01;
    *value = parsed;
    return true;
}

void paramsFlush(const clap_plugin_t* plugin,
    const clap_input_events_t* input, const clap_output_events_t* output)
{
    auto* instance = self(plugin);
    readParamEvents(*instance, input);
    serviceGuiParamEvents(*instance, output);
}

const clap_plugin_params_t paramsExtension {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
    paramsTextToValue, paramsFlush,
};

bool writeAll(const clap_ostream_t* stream, const void* source,
    uint64_t bytes)
{
    const auto* data = static_cast<const uint8_t*>(source);
    while (bytes > 0u) {
        const int64_t amount = stream->write(stream, data, bytes);
        if (amount <= 0 || static_cast<uint64_t>(amount) > bytes) return false;
        data += amount;
        bytes -= static_cast<uint64_t>(amount);
    }
    return true;
}

bool readAll(const clap_istream_t* stream, void* destination, uint64_t bytes)
{
    auto* data = static_cast<uint8_t*>(destination);
    while (bytes > 0u) {
        const int64_t amount = stream->read(stream, data, bytes);
        if (amount <= 0 || static_cast<uint64_t>(amount) > bytes) return false;
        data += amount;
        bytes -= static_cast<uint64_t>(amount);
    }
    return true;
}

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (!stream || !stream->write) return false;
    SavedState state;
    const auto& instance = *self(plugin);
    for (uint32_t index = 0u; index < kParamCount; ++index)
        state.values[index] = publishedValue(instance, index);
    return writeAll(stream, &state, sizeof(state));
}

template <std::size_t Count>
void applyPreviousState(Plugin& instance,
    std::array<double, Count> values, uint32_t previousGlobalParamCount,
    uint32_t previousRelayParamCount, bool migrateVersion2Scale)
{
    if (migrateVersion2Scale) {
        const int64_t oldScale = std::clamp<int64_t>(
            std::isfinite(values[kScale])
                ? static_cast<int64_t>(std::llround(values[kScale])) : 0,
            0, static_cast<int64_t>(kVersion2ScaleToCanonical.size() - 1u));
        values[kScale] = static_cast<double>(
            kVersion2ScaleToCanonical[static_cast<std::size_t>(oldScale)]);
    }
    for (uint32_t index = 0u; index < kParamCount; ++index) {
        const ParamSpec spec = paramSpec(index);
        applyParam(instance, spec.id, spec.defaultValue);
    }
    for (uint32_t index = 0u; index < previousGlobalParamCount; ++index)
        applyParam(instance, paramIdForIndex(index), values[index]);
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        for (uint32_t local = 0u; local < previousRelayParamCount; ++local) {
            const uint32_t oldIndex = previousGlobalParamCount
                + relay * previousRelayParamCount + local;
            const uint32_t newIndex = kGlobalParamCount
                + relay * kRelayParamCount + local;
            applyParam(instance, paramIdForIndex(newIndex), values[oldIndex]);
        }
    }
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (!stream || !stream->read) return false;
    StateHeader header;
    if (!readAll(stream, &header, sizeof(header))
        || header.magic != kStateMagic) return false;
    auto& instance = *self(plugin);
    if (header.version == kStateVersion) {
        std::array<double, kParamCount> values {};
        if (!readAll(stream, values.data(), sizeof(values))) return false;
        for (uint32_t index = 0u; index < kParamCount; ++index)
            applyParam(instance, paramIdForIndex(index), values[index]);
    } else if (header.version == 4u) {
        std::array<double, kVersion4ParamCount> values {};
        if (!readAll(stream, values.data(), sizeof(values))) return false;
        applyPreviousState(instance, values,
            kVersion4GlobalParamCount, kVersion4RelayParamCount, false);
    } else if (header.version == 3u || header.version == 2u) {
        std::array<double, kVersion3ParamCount> values {};
        if (!readAll(stream, values.data(), sizeof(values))) return false;
        applyPreviousState(instance, values,
            kVersion3GlobalParamCount, kVersion3RelayParamCount,
            header.version == 2u);
    } else if (header.version == 1u) {
        std::array<double, kLegacyParamCount> values {};
        if (!readAll(stream, values.data(), sizeof(values))) return false;
        for (uint32_t index = 0u; index < kParamCount; ++index) {
            const ParamSpec spec = paramSpec(index);
            applyParam(instance, spec.id, spec.defaultValue);
        }
        for (uint32_t index = 0u; index < kLegacyGlobalParamCount; ++index)
            applyParam(instance, paramIdForIndex(index), values[index]);
        for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
            for (uint32_t local = 0u; local < kLegacyRelayParamCount; ++local) {
                const uint32_t oldIndex = kLegacyGlobalParamCount
                    + relay * kLegacyRelayParamCount + local;
                const uint32_t newIndex = kGlobalParamCount
                    + relay * kRelayParamCount + local;
                applyParam(instance, paramIdForIndex(newIndex),
                    values[oldIndex]);
            }
        }
    } else {
        return false;
    }
    instance.formHold.store(false, std::memory_order_release);
    instance.hasThawMemory.store(false, std::memory_order_relaxed);
    instance.engine.invalidate();
    publishAll(instance);
    return true;
}

const clap_plugin_state_t stateExtension { stateSave, stateLoad };

} // namespace

#if defined(__APPLE__)

namespace {

constexpr CGFloat kPanelHeaderHeight = 22.0;
constexpr NSInteger kFactoryPresetMenuId =
    static_cast<NSInteger>(kParamCount);

struct GuiParamBatch {
    std::array<s3g::clap_gui::ParamEvent, kParamCount * 3u> events {};
    uint32_t count = 0u;

    bool add(uint32_t index, double value)
    {
        if (index >= kParamCount || count + 3u > events.size()) return false;
        const clap_id id = paramIdForIndex(index);
        events[count++] = { s3g::clap_gui::ParamEventKind::GestureBegin,
            id, 0.0 };
        events[count++] = { s3g::clap_gui::ParamEventKind::Value,
            id, value };
        events[count++] = { s3g::clap_gui::ParamEventKind::GestureEnd,
            id, 0.0 };
        return true;
    }

    bool submit(Plugin& plugin)
    {
        if (count == 0u || !plugin.guiParamEvents.pushBatch(
                events.data(), count)) return false;
        requestGuiParamService(plugin);
        return true;
    }
};

bool queueFactoryPreset(Plugin& plugin, const Config& config)
{
    GuiParamBatch batch;
    for (uint32_t index = 0u; index < kGlobalParamCount; ++index) {
        if (!batch.add(index, configValue(config, index))) return false;
    }
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        for (uint32_t local = 0u; local < kRelayParamCount; ++local) {
            if (local == kRelayChannel || local == kRelayNote
                || local == kRelayCcA || local == kRelayCcB) continue;
            const uint32_t index = kGlobalParamCount
                + relay * kRelayParamCount + local;
            if (!batch.add(index, configValue(config, index))) return false;
        }
    }
    return batch.submit(plugin);
}

int detectedFactoryPreset(const Plugin& plugin)
{
    for (uint32_t preset = 0u; preset < kFactoryPresetCount; ++preset) {
        const Config candidate = factoryPresetConfig(preset);
        bool matches = true;
        for (uint32_t index = 0u; index < kGlobalParamCount && matches;
             ++index) {
            matches = std::abs(publishedValue(plugin, index)
                - configValue(candidate, index)) < 1.0e-9;
        }
        for (uint32_t relay = 0u;
             relay < s3g::relay::kRelayCount && matches; ++relay) {
            for (uint32_t local = 0u; local < kRelayParamCount; ++local) {
                if (local == kRelayChannel || local == kRelayNote
                    || local == kRelayCcA || local == kRelayCcB) continue;
                const uint32_t index = kGlobalParamCount
                    + relay * kRelayParamCount + local;
                if (std::abs(publishedValue(plugin, index)
                        - configValue(candidate, index)) >= 1.0e-9) {
                    matches = false;
                    break;
                }
            }
        }
        if (matches) return static_cast<int>(preset);
    }
    return -1;
}

NSRect graphPanelRect() { return NSMakeRect(18.0, 42.0, 760.0, 500.0); }
NSRect conductPanelRect() { return NSMakeRect(790.0, 42.0, 432.0, 274.0); }
NSRect pitchPanelRect() { return NSMakeRect(790.0, 328.0, 432.0, 214.0); }
NSRect climatePanelRect() { return NSMakeRect(790.0, 554.0, 432.0, 328.0); }
NSRect relayPanelRect() { return NSMakeRect(18.0, 554.0, 760.0, 328.0); }

NSRect globalRowRect(uint32_t index)
{
    if (index < 8u)
        return NSMakeRect(806.0, 72.0 + static_cast<CGFloat>(index) * 29.0,
            400.0, 22.0);
    if (index < 16u)
        return NSMakeRect(806.0,
            586.0 + static_cast<CGFloat>(index - 8u) * 30.0, 400.0, 22.0);
    if (index <= kScaleRange)
        return NSMakeRect(806.0,
            362.0 + static_cast<CGFloat>(index - 16u) * 34.0, 400.0, 22.0);
    return NSMakeRect(806.0, 826.0, 400.0, 22.0);
}

NSRect relayRowRect(uint32_t local)
{
    const uint32_t column = local >= 6u ? 1u : 0u;
    const uint32_t row = local >= 6u ? local - 6u : local;
    return NSMakeRect(34.0 + static_cast<CGFloat>(column) * 390.0,
        592.0 + static_cast<CGFloat>(row) * 38.0, 338.0, 24.0);
}

NSRect relayTabRect(uint32_t relay)
{
    return NSMakeRect(114.0 + static_cast<CGFloat>(relay) * 80.0,
        557.0, 72.0, 16.0);
}

NSRect crystallizeRect() { return NSMakeRect(1107.0, 45.0, 104.0, 16.0); }

NSRect visualPageTabRect(uint32_t page)
{
    return NSMakeRect(432.0 + static_cast<CGFloat>(page) * 84.0,
        45.0, 80.0, 16.0);
}

NSRect formPlaneButtonRect(uint32_t item)
{
    return NSMakeRect(34.0 + static_cast<CGFloat>(item) * 64.0,
        72.0, 58.0, 16.0);
}

NSRect consoleFilterRect(uint32_t filter)
{
    return NSMakeRect(34.0 + static_cast<CGFloat>(filter) * 57.0,
        72.0, 51.0, 16.0);
}

NSRect consoleClearRect() { return NSMakeRect(211.0, 72.0, 51.0, 16.0); }

bool parameterIsBinary(uint32_t index)
{
    return index == kEnabled || index == kFreeze
        || (index >= kGlobalParamCount
            && (index - kGlobalParamCount) % kRelayParamCount
                == kRelayEnabled);
}

bool parameterIsMenu(uint32_t index)
{
    if (index == kClockRate || index == kCcRate
        || index == kLatticeDepth
        || (index >= kScaleRoot && index <= kScaleRange)) return true;
    if (index < kGlobalParamCount) return false;
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    return local == kRelayChannel || local == kRelayTopology
        || local == kRelayPitchMode || local == kRelayArticulation;
}

uint32_t parameterMenuCount(uint32_t index)
{
    if (index == kClockRate) return 7u;
    if (index == kCcRate) return 6u;
    if (index == kScaleRoot) return 12u;
    if (index == kScaleOctave) return 9u;
    if (index == kScale) return s3g::kMusicalScaleCount;
    if (index == kScaleRange) return 4u;
    if (index == kLatticeDepth) return 3u;
    if (index < kGlobalParamCount) return 0u;
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    if (local == kRelayChannel) return 16u;
    if (local == kRelayTopology) return 4u;
    if (local == kRelayPitchMode) return 2u;
    if (local == kRelayArticulation) return 4u;
    return 0u;
}

uint32_t parameterMenuColumns(uint32_t index)
{
    if (index == kScale) return 4u;
    if (index == kScaleRoot) return 2u;
    return index >= kGlobalParamCount
            && (index - kGlobalParamCount) % kRelayParamCount == kRelayChannel
        ? 2u : 1u;
}

double parameterMenuValue(uint32_t index, uint32_t item)
{
    if (index == kScale)
        return static_cast<double>(
            s3g::musicalScaleValueForMenuIndex(item));
    if (index == kScaleOctave)
        return static_cast<double>(static_cast<int32_t>(item) - 1);
    if (index == kScaleRange) return static_cast<double>(item + 1u);
    if (index >= kGlobalParamCount
        && (index - kGlobalParamCount) % kRelayParamCount == kRelayChannel)
        return static_cast<double>(item + 1u);
    return static_cast<double>(item);
}

int parameterMenuSelection(uint32_t index, const Plugin& plugin)
{
    int selected = static_cast<int>(std::lround(publishedValue(plugin, index)));
    if (index == kScale)
        return static_cast<int>(s3g::musicalScaleMenuIndexForValue(
            static_cast<uint32_t>(selected)));
    if (index == kScaleOctave) ++selected;
    if (index == kScaleRange) --selected;
    if (index >= kGlobalParamCount
        && (index - kGlobalParamCount) % kRelayParamCount == kRelayChannel)
        --selected;
    return selected;
}

NSString* parameterMenuItem(uint32_t index, uint32_t item)
{
    if (index == kClockRate) {
        static NSString* const items[7] = {
            @"1 TICK / 4 BEATS", @"1 TICK / 2 BEATS", @"1 TICK / BEAT",
            @"2 TICKS / BEAT", @"4 TICKS / BEAT", @"8 TICKS / BEAT",
            @"16 TICKS / BEAT",
        };
        return items[std::min<uint32_t>(item, 6u)];
    }
    if (index == kCcRate) {
        return [NSString stringWithFormat:@"EVERY %u TICK%@",
            s3g::relay::ccEveryTicks(item),
            s3g::relay::ccEveryTicks(item) == 1u ? @"" : @"S"];
    }
    if (index == kScaleRoot)
        return [NSString stringWithUTF8String:scaleRootName(item)];
    if (index == kScaleOctave)
        return [NSString stringWithFormat:@"OCTAVE %d",
            static_cast<int>(item) - 1];
    if (index == kScale)
        return [NSString stringWithUTF8String:s3g::musicalScaleDefinition(
            s3g::musicalScaleValueForMenuIndex(item)).name];
    if (index == kScaleRange)
        return [NSString stringWithFormat:@"%u OCTAVE%@", item + 1u,
            item == 0u ? @"" : @"S"];
    if (index == kLatticeDepth) {
        static NSString* const items[3] = {
            @"SHEET", @"2 PLANES", @"4 PLANES",
        };
        return items[std::min<uint32_t>(item, 2u)];
    }
    const uint32_t local = index >= kGlobalParamCount
        ? (index - kGlobalParamCount) % kRelayParamCount : kRelayParamCount;
    if (local == kRelayChannel)
        return [NSString stringWithFormat:@"CHANNEL %u", item + 1u];
    if (local == kRelayTopology) {
        static NSString* const items[4] = {
            @"LOCAL", @"CROSS", @"DIFFUSE", @"ROAMING",
        };
        return items[std::min<uint32_t>(item, 3u)];
    }
    if (local == kRelayPitchMode)
        return item == 0u ? @"FIXED" : @"SCALE LOGIC";
    if (local == kRelayArticulation) {
        static NSString* const items[4] = {
            @"RESTART", @"HOLD", @"EXTEND", @"STACK",
        };
        return items[std::min<uint32_t>(item, 3u)];
    }
    return @"";
}

void parameterPanelGeometry(uint32_t index, CGFloat& panelX,
    CGFloat& panelWidth)
{
    if (index < 8u) {
        panelX = conductPanelRect().origin.x;
        panelWidth = conductPanelRect().size.width;
        return;
    }
    if (index < kGlobalParamCount) {
        const bool climate = index < 16u || index == kLatticeDepth;
        panelX = climate ? climatePanelRect().origin.x
                         : pitchPanelRect().origin.x;
        panelWidth = climate ? climatePanelRect().size.width
                             : pitchPanelRect().size.width;
        return;
    }
    const uint32_t local = (index - kGlobalParamCount) % kRelayParamCount;
    panelX = local < 6u ? 18.0 : 408.0;
    panelWidth = 370.0;
}

NSRect parameterMenuBoxRect(uint32_t index)
{
    CGFloat panelX = 0.0;
    CGFloat panelWidth = 0.0;
    parameterPanelGeometry(index, panelX, panelWidth);
    const NSRect row = index < kGlobalParamCount
        ? globalRowRect(index)
        : relayRowRect((index - kGlobalParamCount) % kRelayParamCount);
    return NSMakeRect(
        static_cast<CGFloat>(s3g::gui_layout::processorControlX(panelX)),
        row.origin.y + 1.0,
        static_cast<CGFloat>(s3g::gui_layout::processorMenuWidth(panelWidth)),
        15.0);
}

NSRect parameterSliderTrackRect(uint32_t index)
{
    CGFloat panelX = 0.0;
    CGFloat panelWidth = 0.0;
    parameterPanelGeometry(index, panelX, panelWidth);
    const NSRect row = index < kGlobalParamCount
        ? globalRowRect(index)
        : relayRowRect((index - kGlobalParamCount) % kRelayParamCount);
    return NSMakeRect(
        static_cast<CGFloat>(s3g::gui_layout::processorControlX(panelX)),
        row.origin.y + 3.0,
        static_cast<CGFloat>(s3g::gui_layout::processorTrackWidth(panelWidth)),
        9.0);
}

NSRect parameterInteractionRect(uint32_t index)
{
    const NSRect row = index < kGlobalParamCount
        ? globalRowRect(index)
        : relayRowRect((index - kGlobalParamCount) % kRelayParamCount);
    if (parameterIsBinary(index))
        return NSMakeRect(NSMaxX(row) - 68.0, row.origin.y,
            68.0, row.size.height);
    if (parameterIsMenu(index)) {
        const NSRect box = parameterMenuBoxRect(index);
        return NSInsetRect(box, -4.0, -4.0);
    }
    const NSRect track = parameterSliderTrackRect(index);
    return NSMakeRect(track.origin.x - 5.0, row.origin.y,
        track.size.width + 10.0, row.size.height);
}

NSPoint clusterPoint(uint32_t cluster)
{
    static constexpr std::array<double, 4u> x {{ 270.0, 526.0, 270.0, 526.0 }};
    static constexpr std::array<double, 4u> y {{ 182.0, 182.0, 404.0, 404.0 }};
    return NSMakePoint(
        static_cast<CGFloat>(x[cluster % s3g::relay::kClusterCount]),
        static_cast<CGFloat>(y[cluster % s3g::relay::kClusterCount]));
}

NSPoint nodePoint(uint32_t node)
{
    constexpr double radius = 34.0;
    const uint32_t cluster = node / s3g::relay::kNodesPerCluster;
    const uint32_t local = node % s3g::relay::kNodesPerCluster;
    const double angle = -s3g::relay::kPi * 0.5
        + s3g::relay::kPi * 2.0 * static_cast<double>(local)
            / static_cast<double>(s3g::relay::kNodesPerCluster);
    const NSPoint center = clusterPoint(cluster);
    return NSMakePoint(
        center.x + static_cast<CGFloat>(std::cos(angle) * radius),
        center.y + static_cast<CGFloat>(std::sin(angle) * radius));
}

NSPoint relayPoint(uint32_t relay)
{
    static constexpr std::array<double, 8u> x {{
        80.0, 716.0, 80.0, 716.0, 80.0, 716.0, 80.0, 716.0,
    }};
    static constexpr std::array<double, 8u> y {{
        145.0, 145.0, 368.0, 368.0, 212.0, 212.0, 435.0, 435.0,
    }};
    return NSMakePoint(static_cast<CGFloat>(x[relay % 8u]),
        static_cast<CGFloat>(y[relay % 8u]));
}

NSRect receptorStationRect(uint32_t cluster)
{
    const bool right = (cluster & 1u) != 0u;
    const bool lower = cluster >= 2u;
    return NSMakeRect(right ? 630.0 : 38.0, lower ? 315.0 : 92.0,
        128.0, 154.0);
}

NSPoint receptorPathOrigin(uint32_t relay)
{
    const NSPoint receptor = relayPoint(relay);
    return NSMakePoint(
        (relay % s3g::relay::kClusterCount & 1u) != 0u ? 630.0 : 166.0,
        receptor.y);
}

NSRect relayGlyphRect(uint32_t relay)
{
    const NSPoint point = relayPoint(relay);
    return NSMakeRect(point.x - 14.0, point.y - 14.0, 28.0, 28.0);
}

void drawLine(NSPoint a, NSPoint b, NSColor* color, CGFloat width = 1.0)
{
    NSBezierPath* path = [NSBezierPath bezierPath];
    [path moveToPoint:a];
    [path lineToPoint:b];
    [path setLineWidth:width];
    [color setStroke];
    [path stroke];
}

void drawDirectedConnection(NSPoint source, NSPoint target, double weight,
    NSColor* positive, NSColor* negative)
{
    const CGFloat dx = target.x - source.x;
    const CGFloat dy = target.y - source.y;
    const CGFloat length = std::sqrt(dx * dx + dy * dy);
    if (length <= 1.0) return;
    const CGFloat ux = dx / length;
    const CGFloat uy = dy / length;
    const CGFloat nx = -uy;
    const CGFloat ny = ux;
    constexpr CGFloat nodeClearance = 9.0;
    constexpr CGFloat laneOffset = 2.2;
    const NSPoint start = NSMakePoint(
        source.x + ux * nodeClearance + nx * laneOffset,
        source.y + uy * nodeClearance + ny * laneOffset);
    const NSPoint end = NSMakePoint(
        target.x - ux * nodeClearance + nx * laneOffset,
        target.y - uy * nodeClearance + ny * laneOffset);
    const CGFloat strength = static_cast<CGFloat>(std::min(
        std::abs(weight) / 1.3, 1.0));
    NSColor* ink = [(weight >= 0.0 ? positive : negative)
        colorWithAlphaComponent:0.46 + strength * 0.42];
    drawLine(start, end, ink, 0.58 + strength * 0.72);

    constexpr CGFloat arrowLength = 4.2;
    constexpr CGFloat arrowWidth = 2.4;
    const NSPoint base = NSMakePoint(
        end.x - ux * arrowLength, end.y - uy * arrowLength);
    drawLine(end, NSMakePoint(base.x + nx * arrowWidth,
        base.y + ny * arrowWidth), ink, 0.58 + strength * 0.72);
    drawLine(end, NSMakePoint(base.x - nx * arrowWidth,
        base.y - ny * arrowWidth), ink, 0.58 + strength * 0.72);
}

void drawParameter(Plugin& plugin, uint32_t index, NSRect rect,
    CGFloat panelX, CGFloat panelWidth)
{
    const ParamSpec spec = paramSpec(index);
    const double value = publishedValue(plugin, index);
    const double normalized = spec.maximum > spec.minimum
        ? std::clamp((value - spec.minimum) / (spec.maximum - spec.minimum),
            0.0, 1.0) : 0.0;
    char buffer[64] {};
    paramsValueToText(&plugin.plugin, spec.id, value, buffer, sizeof(buffer));
    NSString* label = [[NSString stringWithUTF8String:spec.name]
        uppercaseString];
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    const auto style = s3g::clap_gui::softTextStyle();
    if (parameterIsBinary(index)) {
        s3g::clap_gui::drawToggle(label, value >= 0.5,
            rect.origin.y + 2.0, labels, values, style,
            rect.origin.x, NSMaxX(rect) - 64.0, 64.0);
    } else if (parameterIsMenu(index)) {
        s3g::clap_gui::drawProcessorMenu(label,
            [NSString stringWithUTF8String:buffer], rect.origin.y + 2.0,
            panelX, panelWidth, labels, values, style);
    } else {
        s3g::clap_gui::drawProcessorSlider(label,
            [NSString stringWithUTF8String:buffer],
            static_cast<CGFloat>(normalized), rect.origin.y + 2.0,
            panelX, panelWidth, labels, values, style);
    }
}

} // namespace

@interface S3GRelayView : NSView {
    void* _plugin;
    NSInteger _selectedRelay;
    NSInteger _dragParam;
    NSInteger _openMenu;
    NSInteger _hoverMenuItem;
    uint32_t _menuItemCount;
    NSInteger _visualPage;
    NSInteger _formPlane;
    NSInteger _consoleFilter;
    uint64_t _consoleFloor;
    NSInteger _factoryPresetIndex;
    char _presetName[64];
    NSTimer* _timer;
}
- (id)initWithPlugin:(void*)plugin;
- (void)startRefreshTimer;
- (void)stopRefreshTimer;
- (NSRect)openMenuRect;
- (void)drawOpenMenu:(NSDictionary*)attributes
    style:(const s3g::clap_gui::Style&)style;
- (void)drawMidiConsole:(Plugin&)plugin
    style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values;
- (BOOL)applyFactoryPreset:(NSInteger)index;
- (BOOL)applySafeRandom;
- (void)markCustomPreset:(const char*)name;
@end

@implementation S3GRelayView

- (id)initWithPlugin:(void*)plugin
{
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, kGuiWidth, kGuiHeight)];
    if (self) {
        _plugin = plugin;
        _selectedRelay = 0;
        _dragParam = -1;
        _openMenu = -1;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        _visualPage = 0;
        _formPlane = -1;
        _consoleFilter = 0;
        _consoleFloor = 0u;
        _factoryPresetIndex = detectedFactoryPreset(
            *static_cast<Plugin*>(plugin));
        std::snprintf(_presetName, sizeof(_presetName), "%s",
            _factoryPresetIndex >= 0
                ? factoryPresetName(static_cast<uint32_t>(_factoryPresetIndex))
                : "CUSTOM");
        _timer = nil;
        [self setAccessibilityLabel:@"Relay neural MIDI sequencer"];
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
    _timer = [NSTimer timerWithTimeInterval:1.0 / 20.0 target:self
        selector:@selector(refresh:) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)stopRefreshTimer
{
    if (!_timer) return;
    [_timer invalidate];
    _timer = nil;
}

- (void)refresh:(NSTimer*)timer
{
    (void)timer;
    if (![self isHidden] && _plugin
        && s3g::clap_support::hostAppIsActive()) [self setNeedsDisplay:YES];
}

- (void)drawNeuralGraph:(Plugin&)plugin style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    NSColor* positive = s3g::clap_gui::color(0x9a6b50);
    NSColor* negative = s3g::clap_gui::color(0x587d81);
    NSColor* dormant = s3g::clap_gui::color(0x343434);
    const NSPoint fieldBus = NSMakePoint(398.0, 326.0);

    for (uint32_t cluster = 0u; cluster < s3g::relay::kClusterCount;
         ++cluster) {
        const NSRect station = receptorStationRect(cluster);
        [style.cellBg setFill];
        NSRectFill(station);
        [style.grid setStroke];
        NSFrameRect(station);
        [[NSString stringWithFormat:@"C%u / R%u + R%u", cluster + 1u,
            cluster + 1u, cluster + 5u]
            drawAtPoint:NSMakePoint(station.origin.x + 9.0,
                station.origin.y + 8.0)
            withAttributes:labels];
        drawLine(NSMakePoint(station.origin.x + 9.0, station.origin.y + 28.0),
            NSMakePoint(NSMaxX(station) - 9.0, station.origin.y + 28.0),
            style.grid, 0.7);
    }

    // Draw both directions of each recurrent pentad in separate lanes. Color
    // is the actual coupling sign, arrowheads identify its target, and line
    // weight follows the absolute coupling strength.
    for (uint32_t cluster = 0u; cluster < s3g::relay::kClusterCount;
         ++cluster) {
        const uint32_t base = cluster * s3g::relay::kNodesPerCluster;
        for (uint32_t target = 0u;
             target < s3g::relay::kNodesPerCluster; ++target) {
            const uint32_t previous = (target
                + s3g::relay::kNodesPerCluster - 1u)
                % s3g::relay::kNodesPerCluster;
            const uint32_t next = (target + 1u)
                % s3g::relay::kNodesPerCluster;
            drawDirectedConnection(nodePoint(base + previous),
                nodePoint(base + target),
                s3g::relay::kRingForwardWeights[target], positive, negative);
            drawDirectedConnection(nodePoint(base + next),
                nodePoint(base + target),
                s3g::relay::kRingReverseWeights[target], positive, negative);
        }
    }

    drawLine(clusterPoint(0u), clusterPoint(1u), dormant);
    drawLine(clusterPoint(0u), clusterPoint(2u), dormant);
    drawLine(clusterPoint(1u), clusterPoint(3u), dormant);
    drawLine(clusterPoint(2u), clusterPoint(3u), dormant);

    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        const uint32_t local = kGlobalParamCount + relay * kRelayParamCount;
        const auto topology = static_cast<ReceptorTopology>(
            static_cast<uint32_t>(publishedValue(plugin,
                local + kRelayTopology)));
        uint32_t target = relay % s3g::relay::kClusterCount;
        if (topology == ReceptorTopology::Cross)
            target = (target + 2u) % s3g::relay::kClusterCount;
        else if (topology == ReceptorTopology::Roaming) {
            target = (target + plugin.visualCell.load(
                std::memory_order_relaxed)) % s3g::relay::kClusterCount;
        }
        const double receptor = plugin.visualReceptors[relay].load(
            std::memory_order_relaxed);
        const NSPoint destination = topology == ReceptorTopology::Diffuse
            ? fieldBus
            : topology == ReceptorTopology::Local
                ? nodePoint(target * s3g::relay::kNodesPerCluster
                    + s3g::relay::receptorPentadTap(relay))
                : clusterPoint(target);
        drawLine(receptorPathOrigin(relay), destination,
            receptor >= 0.0 ? positive : negative,
            relay == static_cast<uint32_t>(_selectedRelay) ? 1.8 : 0.65);
    }

    for (uint32_t node = 0u; node < s3g::relay::kNodeCount; ++node) {
        const double state = plugin.visualNodes[node].load(
            std::memory_order_relaxed);
        const CGFloat radius = 5.0 + static_cast<CGFloat>(std::abs(state)) * 3.0;
        [(state >= 0.0 ? positive : negative) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            nodePoint(node).x - radius, nodePoint(node).y - radius,
            radius * 2.0, radius * 2.0)] fill];
    }

    for (uint32_t cluster = 0u; cluster < s3g::relay::kClusterCount;
         ++cluster) {
        NSString* name = [NSString stringWithFormat:@"C%u", cluster + 1u];
        const NSSize size = [name sizeWithAttributes:values];
        [name drawAtPoint:NSMakePoint(clusterPoint(cluster).x - size.width * 0.5,
            clusterPoint(cluster).y - size.height * 0.5)
            withAttributes:values];
    }

    [style.strip setFill];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        fieldBus.x - 6.0, fieldBus.y - 6.0, 12.0, 12.0)] fill];
    [style.grid setStroke];
    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
        fieldBus.x - 6.0, fieldBus.y - 6.0, 12.0, 12.0)] stroke];
    [@"DIFFUSE BUS" drawAtPoint:NSMakePoint(fieldBus.x - 34.0,
        fieldBus.y + 10.0) withAttributes:labels];

    [[NSString stringWithFormat:
        @"PENTAD CLOCKWISE FROM TOP: %s / %s / %s / %s / %s",
        s3g::relay::kPentadRoleNames[0u],
        s3g::relay::kPentadRoleNames[1u],
        s3g::relay::kPentadRoleNames[2u],
        s3g::relay::kPentadRoleNames[3u],
        s3g::relay::kPentadRoleNames[4u]]
        drawAtPoint:NSMakePoint(188.0, 500.0) withAttributes:labels];

    const uint32_t bits = plugin.visualRegister.load(std::memory_order_relaxed);
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        const NSPoint receptor = relayPoint(relay);
        const bool right = (relay % s3g::relay::kClusterCount & 1u) != 0u;
        const CGFloat railStart = right ? 644.0 : 106.0;
        const CGFloat railEnd = right ? 692.0 : 154.0;
        const double receptorValue = std::clamp(
            plugin.visualReceptors[relay].load(std::memory_order_relaxed),
            -1.0, 1.0);
        drawLine(NSMakePoint(railStart, receptor.y),
            NSMakePoint(railEnd, receptor.y), style.grid, 1.0);
        const CGFloat midpoint = (railStart + railEnd) * 0.5;
        drawLine(NSMakePoint(midpoint, receptor.y - 3.0),
            NSMakePoint(midpoint, receptor.y + 3.0), style.dim, 0.8);
        const CGFloat marker = railStart + static_cast<CGFloat>(
            (receptorValue + 1.0) * 0.5) * (railEnd - railStart);
        [(receptorValue >= 0.0 ? positive : negative) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
            marker - 2.5, receptor.y - 2.5, 5.0, 5.0)] fill];

        const NSRect glyph = relayGlyphRect(relay);
        const bool active = (bits & (1u << relay)) != 0u;
        [(active ? style.accent : style.strip) setFill];
        NSRectFill(glyph);
        [(relay == static_cast<uint32_t>(_selectedRelay)
            ? style.text : style.grid) setStroke];
        NSFrameRect(glyph);
        NSString* name = [NSString stringWithFormat:@"R%u", relay + 1u];
        NSMutableDictionary* glyphAttrs = [labels mutableCopy];
        glyphAttrs[NSForegroundColorAttributeName] = active
            ? style.bg : style.text;
        const NSSize textSize = [name sizeWithAttributes:glyphAttrs];
        [name drawAtPoint:NSMakePoint(NSMidX(glyph) - textSize.width * 0.5,
            NSMidY(glyph) - textSize.height * 0.5)
            withAttributes:glyphAttrs];
        [glyphAttrs release];
    }

    const NSRect registerRect = NSMakeRect(335.0, 274.0, 126.0, 31.0);
    [style.strip setFill];
    NSRectFill(registerRect);
    [style.grid setStroke];
    NSFrameRect(registerRect);
    [@"RECIRCULATING REGISTER" drawAtPoint:NSMakePoint(331.0, 254.0)
        withAttributes:values];
    for (uint32_t bit = 0u; bit < 8u; ++bit) {
        const NSRect cell = NSMakeRect(343.0 + static_cast<CGFloat>(bit) * 14.0,
            283.0, 10.0, 11.0);
        [((bits & (1u << bit)) != 0u ? style.accent : style.cellBg) setFill];
        NSRectFill(cell);
        [style.grid setStroke];
        NSFrameRect(cell);
    }
}

- (void)drawLearningPlate:(Plugin&)plugin
    style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    NSColor* positive = s3g::clap_gui::color(0x9a6b50);
    NSColor* negative = s3g::clap_gui::color(0x587d81);
    constexpr CGFloat rowHeaderX = 34.0;
    constexpr CGFloat rowHeaderWidth = 64.0;
    constexpr CGFloat matrixX = rowHeaderX + rowHeaderWidth;
    constexpr CGFloat columnHeaderY = 124.0;
    constexpr CGFloat columnHeaderHeight = 30.0;
    constexpr CGFloat matrixY = columnHeaderY + columnHeaderHeight;
    constexpr CGFloat cellSize = 80.0;
    constexpr double displayLimit = 0.55;

    [@"PLASTICITY MATRIX / EFFECTIVE CLUSTER CONNECTIVITY"
        drawAtPoint:NSMakePoint(34.0, 76.0) withAttributes:values];
    [@"SQUARE AREA = |EFFECTIVE WEIGHT| / ROWS RECEIVE / COLUMNS SEND"
        drawAtPoint:NSMakePoint(34.0, 94.0) withAttributes:labels];

    const NSRect matrixCorner = NSMakeRect(rowHeaderX, columnHeaderY,
        rowHeaderWidth, columnHeaderHeight);
    [style.strip setFill];
    NSRectFill(matrixCorner);
    [style.dim setStroke];
    NSFrameRect(matrixCorner);
    [@"TO / FROM" drawAtPoint:NSMakePoint(rowHeaderX + 7.0,
        columnHeaderY + 9.0) withAttributes:labels];
    for (uint32_t source = 0u; source < s3g::relay::kClusterCount;
         ++source) {
        const NSRect header = NSMakeRect(
            matrixX + static_cast<CGFloat>(source) * cellSize,
            columnHeaderY, cellSize, columnHeaderHeight);
        [style.strip setFill];
        NSRectFill(header);
        [style.dim setStroke];
        NSFrameRect(header);
        NSString* name = [NSString stringWithFormat:@"SEND C%u",
            source + 1u];
        const NSSize size = [name sizeWithAttributes:values];
        [name drawAtPoint:NSMakePoint(
            NSMidX(header) - size.width * 0.5,
            NSMidY(header) - size.height * 0.5) withAttributes:values];
    }

    double totalDrift = 0.0;
    double maximumDrift = 0.0;
    uint32_t maximumIndex = 0u;
    uint32_t signFlips = 0u;
    for (uint32_t target = 0u; target < s3g::relay::kClusterCount;
         ++target) {
        const NSRect rowHeader = NSMakeRect(rowHeaderX,
            matrixY + static_cast<CGFloat>(target) * cellSize,
            rowHeaderWidth, cellSize);
        [style.strip setFill];
        NSRectFill(rowHeader);
        [style.dim setStroke];
        NSFrameRect(rowHeader);
        NSString* rowName = [NSString stringWithFormat:@"RECV C%u",
            target + 1u];
        const NSSize rowSize = [rowName sizeWithAttributes:values];
        [rowName drawAtPoint:NSMakePoint(
            NSMidX(rowHeader) - rowSize.width * 0.5,
            NSMidY(rowHeader) - rowSize.height * 0.5)
            withAttributes:values];
        for (uint32_t source = 0u; source < s3g::relay::kClusterCount;
             ++source) {
            const uint32_t index = target * s3g::relay::kClusterCount
                + source;
            const double base = s3g::relay::kInterClusterWeights[index];
            const double learned = plugin.visualPlasticity[index].load(
                std::memory_order_relaxed);
            const double effective = base + learned;
            totalDrift += std::abs(learned);
            if (std::abs(learned) > maximumDrift) {
                maximumDrift = std::abs(learned);
                maximumIndex = index;
            }
            if (base * effective < 0.0) ++signFlips;

            const NSRect cell = NSMakeRect(
                matrixX + static_cast<CGFloat>(source) * cellSize,
                matrixY + static_cast<CGFloat>(target) * cellSize,
                cellSize, cellSize);
            [style.cellBg setFill];
            NSRectFill(cell);
            [style.dim setStroke];
            NSBezierPath* cellBorder = [NSBezierPath bezierPathWithRect:cell];
            [cellBorder setLineWidth:1.0];
            [cellBorder stroke];

            const auto squareSide = [&](double weight) {
                const double ratio = std::clamp(
                    std::abs(weight) / displayLimit, 0.0, 1.0);
                return static_cast<CGFloat>(std::sqrt(ratio) * 44.0);
            };
            const NSPoint squareCenter = NSMakePoint(
                NSMidX(cell), cell.origin.y + 38.0);
            const CGFloat effectiveSide = squareSide(effective);
            if (effectiveSide > 0.5) {
                [[(effective >= 0.0 ? positive : negative)
                    colorWithAlphaComponent:0.72] setFill];
                NSRectFill(NSMakeRect(
                    squareCenter.x - effectiveSide * 0.5,
                    squareCenter.y - effectiveSide * 0.5,
                    effectiveSide, effectiveSide));
            }
            const CGFloat baseSide = squareSide(base);
            if (baseSide > 0.5) {
                [[(base >= 0.0 ? positive : negative)
                    colorWithAlphaComponent:0.92] setStroke];
                NSBezierPath* baseOutline = [NSBezierPath bezierPathWithRect:
                    NSMakeRect(squareCenter.x - baseSide * 0.5,
                        squareCenter.y - baseSide * 0.5,
                        baseSide, baseSide)];
                [baseOutline setLineWidth:1.1];
                [baseOutline stroke];
            }
            const CGFloat deltaHalf = static_cast<CGFloat>(std::clamp(
                std::abs(learned) / 0.22, 0.0, 1.0)) * 29.0;
            const CGFloat deltaCenter = NSMidX(cell);
            const CGFloat deltaY = NSMaxY(cell) - 4.0;
            drawLine(NSMakePoint(deltaCenter, deltaY),
                NSMakePoint(deltaCenter
                    + (learned >= 0.0 ? deltaHalf : -deltaHalf), deltaY),
                style.accent, 2.0);

            NSString* deltaText = [NSString stringWithFormat:
                @"D%+.2f", learned];
            [deltaText drawAtPoint:NSMakePoint(cell.origin.x + 5.0,
                cell.origin.y + 4.0) withAttributes:labels];
            NSString* weightText = [NSString stringWithFormat:
                @"W%+.2f", effective];
            const NSSize weightSize = [weightText sizeWithAttributes:values];
            [weightText drawAtPoint:NSMakePoint(
                NSMidX(cell) - weightSize.width * 0.5,
                cell.origin.y + 60.0) withAttributes:values];
        }
    }
    [style.text setStroke];
    NSBezierPath* matrixBorder = [NSBezierPath bezierPathWithRect:NSMakeRect(
        rowHeaderX, columnHeaderY,
        rowHeaderWidth + cellSize * s3g::relay::kClusterCount,
        columnHeaderHeight + cellSize * s3g::relay::kClusterCount)];
    [matrixBorder setLineWidth:1.4];
    [matrixBorder stroke];

    const bool frozen = publishedValue(plugin, kFreeze) >= 0.5;
    const double mutation = publishedValue(plugin, kMutation);
    NSString* status = frozen ? @"FROZEN"
        : mutation <= 1.0e-6 ? @"STATIC" : @"ADAPTING";
    [@"MATRIX READOUT" drawAtPoint:NSMakePoint(480.0, 76.0)
        withAttributes:labels];
    [(frozen ? style.strip : style.accent) setFill];
    const NSRect statusRect = NSMakeRect(480.0, 94.0, 266.0, 22.0);
    NSRectFill(statusRect);
    NSMutableDictionary* statusAttrs = [values mutableCopy];
    statusAttrs[NSForegroundColorAttributeName] = frozen
        ? style.text : style.bg;
    [status drawAtPoint:NSMakePoint(490.0, 99.0)
        withAttributes:statusAttrs];
    [statusAttrs release];
    [[NSString stringWithFormat:@"MUTATION  %3.0f%%", mutation * 100.0]
        drawAtPoint:NSMakePoint(480.0, 130.0) withAttributes:values];
    [[NSString stringWithFormat:@"MEAN |D|  %.3f",
        totalDrift / static_cast<double>(
            s3g::relay::kClusterCount * s3g::relay::kClusterCount)]
        drawAtPoint:NSMakePoint(480.0, 151.0) withAttributes:values];
    [[NSString stringWithFormat:@"MAX |D|   %.3f", maximumDrift]
        drawAtPoint:NSMakePoint(480.0, 172.0) withAttributes:values];
    const uint32_t maximumTarget = maximumIndex / s3g::relay::kClusterCount;
    const uint32_t maximumSource = maximumIndex % s3g::relay::kClusterCount;
    [[NSString stringWithFormat:@"STRONGEST D  C%u > C%u",
        maximumSource + 1u, maximumTarget + 1u]
        drawAtPoint:NSMakePoint(480.0, 193.0) withAttributes:values];
    [[NSString stringWithFormat:@"SIGN FLIPS %u", signFlips]
        drawAtPoint:NSMakePoint(480.0, 214.0) withAttributes:values];
    [@"MATRIX KEY" drawAtPoint:NSMakePoint(480.0, 244.0)
        withAttributes:labels];
    [[positive colorWithAlphaComponent:0.72] setFill];
    NSRectFill(NSMakeRect(480.0, 267.0, 18.0, 18.0));
    [@"FILLED AREA  EFFECTIVE WEIGHT"
        drawAtPoint:NSMakePoint(508.0, 270.0) withAttributes:values];
    [positive setStroke];
    NSBezierPath* baseKey = [NSBezierPath bezierPathWithRect:
        NSMakeRect(480.0, 299.0, 18.0, 18.0)];
    [baseKey setLineWidth:1.1];
    [baseKey stroke];
    [@"OUTLINE  FIXED BASE WEIGHT"
        drawAtPoint:NSMakePoint(508.0, 302.0) withAttributes:values];
    drawLine(NSMakePoint(480.0, 340.0), NSMakePoint(506.0, 340.0),
        style.accent, 2.0);
    [@"FOOT LINE  LEARNED DELTA"
        drawAtPoint:NSMakePoint(516.0, 333.0) withAttributes:values];
    [@"RUST POSITIVE / TEAL NEGATIVE"
        drawAtPoint:NSMakePoint(480.0, 365.0) withAttributes:labels];
    [@"LEARNING RULE" drawAtPoint:NSMakePoint(480.0, 396.0)
        withAttributes:labels];
    [@"D(t+1) = DECAYED D(t)"
        drawAtPoint:NSMakePoint(480.0, 416.0) withAttributes:labels];
    [@"+ MUTATION x SOURCE(t-1) x TARGET(t)"
        drawAtPoint:NSMakePoint(480.0, 434.0) withAttributes:labels];
    [@"DIAGONAL BASE WEIGHTS ARE ZERO"
        drawAtPoint:NSMakePoint(480.0, 464.0) withAttributes:labels];
    [@"CORRELATION MAY LEARN SELF-COUPLING"
        drawAtPoint:NSMakePoint(480.0, 482.0) withAttributes:labels];
    [@"DELTA CLAMPS TO +/- 0.22 FROM BASE"
        drawAtPoint:NSMakePoint(480.0, 510.0)
        withAttributes:labels];
}

- (void)drawFormDeck:(Plugin&)plugin
    style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    NSColor* positive = s3g::clap_gui::color(0x9a6b50);
    NSColor* negative = s3g::clap_gui::color(0x587d81);
    const uint32_t depthIndex = static_cast<uint32_t>(std::lround(
        publishedValue(plugin, kLatticeDepth)));
    const uint32_t planes = s3g::relay::latticePlaneCount(depthIndex);
    const uint32_t totalCells = s3g::relay::latticeCellCount(depthIndex);
    const uint32_t current = plugin.visualCell.load(
        std::memory_order_relaxed) % totalCells;
    const uint32_t previous = plugin.visualPreviousCell.load(
        std::memory_order_relaxed) % totalCells;
    const uint32_t currentPlane = current
        / s3g::relay::kClimateCellsPerPlane;
    const bool following = _formPlane < 0
        || _formPlane >= static_cast<NSInteger>(planes);
    const uint32_t inspectedPlane = following ? currentPlane
        : static_cast<uint32_t>(_formPlane);
    const uint32_t trailCount = std::min<uint32_t>(
        plugin.visualTrailCount.load(std::memory_order_relaxed),
        s3g::relay::kTrailLength);
    const uint32_t seed = static_cast<uint32_t>(publishedValue(plugin, kSeed));
    const int64_t cycleIndex = plugin.visualCycleIndex.load(
        std::memory_order_relaxed);
    const double cyclePhase = plugin.visualCyclePhase.load(
        std::memory_order_relaxed);
    const double gestation = plugin.visualClimateBlend.load(
        std::memory_order_relaxed);
    const bool formHeld = plugin.formHold.load(std::memory_order_acquire);

    static NSString* const planeItems[5] = {
        @"FOLLOW", @"P1", @"P2", @"P3", @"P4",
    };
    for (uint32_t item = 0u; item <= planes; ++item) {
        const bool active = item == 0u ? following
            : (!following && inspectedPlane + 1u == item);
        s3g::clap_gui::drawToolboxHeaderButton(formPlaneButtonRect(item),
            graphPanelRect(), planeItems[item], active, labels, style);
    }
    [[NSString stringWithFormat:@"CLIMATE DECK / %u SUIT%@ / %u CARDS",
        planes, planes == 1u ? @"" : @"S", totalCells]
        drawAtPoint:NSMakePoint(368.0, 76.0) withAttributes:values];
    [[NSString stringWithFormat:
        formHeld ? @"FORM HELD @ %u:%lld / DEAL P%u C%02u"
                 : @"SEEDED SHUFFLE %u:%lld / DEAL P%u C%02u",
        seed, static_cast<long long>(cycleIndex), currentPlane + 1u,
        current % s3g::relay::kClimateCellsPerPlane + 1u]
        drawAtPoint:NSMakePoint(368.0, 94.0) withAttributes:labels];
    [[NSString stringWithFormat:@"%@ SUIT P%u",
        following ? @"FOLLOWING DEAL /" : @"INSPECTING /",
        inspectedPlane + 1u]
        drawAtPoint:NSMakePoint(34.0, 104.0) withAttributes:labels];

    constexpr CGFloat gridX = 34.0;
    constexpr CGFloat gridY = 122.0;
    constexpr CGFloat cardWidth = 103.0;
    constexpr CGFloat cardHeight = 84.0;
    constexpr CGFloat cardGap = 6.0;
    for (uint32_t localCell = 0u;
         localCell < s3g::relay::kClimateCellsPerPlane; ++localCell) {
        const uint32_t cell = inspectedPlane
            * s3g::relay::kClimateCellsPerPlane + localCell;
        const uint32_t column = localCell % s3g::relay::kClimateWidth;
        const uint32_t row = localCell / s3g::relay::kClimateWidth;
        const NSRect rect = NSMakeRect(
            gridX + static_cast<CGFloat>(column) * (cardWidth + cardGap),
            gridY + static_cast<CGFloat>(row) * (cardHeight + cardGap),
            cardWidth, cardHeight);
        bool inTrail = false;
        for (uint32_t index = 0u; index < trailCount; ++index) {
            if (plugin.visualTrail[index].load(std::memory_order_relaxed)
                % totalCells == cell) {
                inTrail = true;
                break;
            }
        }
        NSColor* fill = cell == current
            ? s3g::clap_gui::color(0x3a302b)
            : cell == previous ? s3g::clap_gui::color(0x3a3a3a)
            : inTrail ? s3g::clap_gui::color(0x2e2e2e)
            : style.cellBg;
        NSBezierPath* card = [NSBezierPath bezierPathWithRoundedRect:rect
            xRadius:4.0 yRadius:4.0];
        [fill setFill];
        [card fill];
        [(cell == current ? style.accent : style.grid) setStroke];
        [card setLineWidth:cell == current ? 1.6 : 0.8];
        [card stroke];

        [[NSString stringWithFormat:@"C%02u", localCell + 1u]
            drawAtPoint:NSMakePoint(rect.origin.x + 7.0, rect.origin.y + 7.0)
            withAttributes:values];
        const int energy = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 0u) * 99.0));
        const int coupling = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 1u) * 99.0));
        const int hierarchy = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 2u) * 99.0));
        const int contrast = static_cast<int>(std::lround(
            s3g::relay::climateCellTrait(
                seed, cycleIndex, cell, 3u) * 99.0));
        [[NSString stringWithFormat:@"E%+03d  C%+03d", energy, coupling]
            drawAtPoint:NSMakePoint(rect.origin.x + 7.0, rect.origin.y + 34.0)
            withAttributes:labels];
        [[NSString stringWithFormat:@"H%+03d  X%+03d", hierarchy, contrast]
            drawAtPoint:NSMakePoint(rect.origin.x + 7.0, rect.origin.y + 54.0)
            withAttributes:labels];
        if (inTrail) {
            [style.accent setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(
                NSMaxX(rect) - 12.0, rect.origin.y + 9.0, 4.0, 4.0)] fill];
        }
    }

    const NSRect dealt = NSMakeRect(500.0, 122.0, 246.0, 286.0);
    NSBezierPath* dealtCard = [NSBezierPath bezierPathWithRoundedRect:dealt
        xRadius:7.0 yRadius:7.0];
    [s3g::clap_gui::color(0x242424) setFill];
    [dealtCard fill];
    [style.accent setStroke];
    [dealtCard setLineWidth:1.4];
    [dealtCard stroke];
    [(formHeld ? @"DEALT CLIMATE / HELD" : @"DEALT CLIMATE")
        drawAtPoint:NSMakePoint(518.0, 140.0)
        withAttributes:labels];
    [[NSString stringWithFormat:@"P%u / C%02u",
        currentPlane + 1u,
        current % s3g::relay::kClimateCellsPerPlane + 1u]
        drawAtPoint:NSMakePoint(518.0, 161.0) withAttributes:values];
    [@"SUIT 64% MACRO / CARD 36% LOCAL"
        drawAtPoint:NSMakePoint(518.0, 180.0) withAttributes:labels];

    static NSString* const traitNames[4] = {
        @"ENERGY", @"COUPLING", @"HIERARCHY", @"CONTRAST",
    };
    for (uint32_t trait = 0u; trait < 4u; ++trait) {
        const double traitValue = s3g::relay::climateCellTrait(
            seed, cycleIndex, current, trait);
        const CGFloat y = 217.0 + static_cast<CGFloat>(trait) * 38.0;
        [traitNames[trait] drawAtPoint:NSMakePoint(518.0, y - 8.0)
            withAttributes:labels];
        constexpr CGFloat left = 597.0;
        constexpr CGFloat right = 704.0;
        const CGFloat center = (left + right) * 0.5;
        drawLine(NSMakePoint(left, y), NSMakePoint(right, y),
            style.grid, 2.0);
        drawLine(NSMakePoint(center, y - 4.0),
            NSMakePoint(center, y + 4.0), style.dim, 0.8);
        const CGFloat end = center + static_cast<CGFloat>(traitValue)
            * (right - left) * 0.5;
        drawLine(NSMakePoint(center, y), NSMakePoint(end, y),
            traitValue >= 0.0 ? positive : negative, 3.0);
        [[NSString stringWithFormat:@"%+03d",
            static_cast<int>(std::lround(traitValue * 99.0))]
            drawAtPoint:NSMakePoint(711.0, y - 8.0) withAttributes:values];
    }
    [@"GESTATION" drawAtPoint:NSMakePoint(518.0, 361.0)
        withAttributes:labels];
    [style.grid setFill];
    NSRectFill(NSMakeRect(597.0, 365.0, 128.0, 4.0));
    [style.accent setFill];
    NSRectFill(NSMakeRect(597.0, 365.0,
        128.0 * static_cast<CGFloat>(gestation), 4.0));
    [@"FORM CYCLE" drawAtPoint:NSMakePoint(518.0, 382.0)
        withAttributes:labels];
    [style.grid setFill];
    NSRectFill(NSMakeRect(597.0, 386.0, 128.0, 4.0));
    [style.accent setFill];
    NSRectFill(NSMakeRect(597.0, 386.0,
        128.0 * static_cast<CGFloat>(cyclePhase), 4.0));

    [@"RECENT DEAL" drawAtPoint:NSMakePoint(500.0, 426.0)
        withAttributes:labels];
    const uint32_t recentCount = std::min<uint32_t>(trailCount, 8u);
    const uint32_t recentStart = trailCount - recentCount;
    for (uint32_t slot = 0u; slot < recentCount; ++slot) {
        const uint32_t trailCell = plugin.visualTrail[recentStart + slot].load(
            std::memory_order_relaxed) % totalCells;
        const NSRect token = NSMakeRect(
            500.0 + static_cast<CGFloat>(slot % 4u) * 61.0,
            445.0 + static_cast<CGFloat>(slot / 4u) * 34.0,
            55.0, 27.0);
        [(trailCell == current ? s3g::clap_gui::color(0x3a302b)
                               : style.cellBg) setFill];
        NSRectFill(token);
        [(trailCell == current ? style.accent : style.grid) setStroke];
        NSFrameRect(token);
        [[NSString stringWithFormat:@"P%u C%02u",
            trailCell / s3g::relay::kClimateCellsPerPlane + 1u,
            trailCell % s3g::relay::kClimateCellsPerPlane + 1u]
            drawAtPoint:NSMakePoint(token.origin.x + 5.0,
                token.origin.y + 7.0) withAttributes:labels];
    }
    [@"SEED + FORM CYCLE SHUFFLE TRAITS / REGISTER + FIELD DEAL THE PATH"
        drawAtPoint:NSMakePoint(34.0, 520.0) withAttributes:labels];
}

- (void)markCustomPreset:(const char*)name
{
    _factoryPresetIndex = -1;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        name && name[0] ? name : "CUSTOM");
}

- (BOOL)applyFactoryPreset:(NSInteger)index
{
    if (!_plugin || index < 0
        || index >= static_cast<NSInteger>(kFactoryPresetCount)) return NO;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    if (!queueFactoryPreset(plugin,
            factoryPresetConfig(static_cast<uint32_t>(index)))) return NO;
    plugin.formHold.store(false, std::memory_order_release);
    plugin.hasThawMemory.store(false, std::memory_order_relaxed);
    _factoryPresetIndex = index;
    std::snprintf(_presetName, sizeof(_presetName), "%s",
        factoryPresetName(static_cast<uint32_t>(index)));
    [self setNeedsDisplay:YES];
    return YES;
}

- (BOOL)applySafeRandom
{
    if (!_plugin) return NO;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    const auto unit = [] {
        return static_cast<double>(arc4random()) / 4294967295.0;
    };
    GuiParamBatch batch;
    const auto add = [&](uint32_t index, double value) {
        return batch.add(index, value);
    };
    if (!add(kActivity, 0.32 + unit() * 0.58)
        || !add(kCoupling, 0.22 + unit() * 0.68)
        || !add(kMemory, 0.24 + unit() * 0.72)
        || !add(kMutation, 0.015 + unit() * 0.30)
        || !add(kHierarchy, 0.12 + unit() * 0.80)
        || !add(kContrast, 0.20 + unit() * 0.72)
        || !add(kFreeze, 0.0)
        || !add(kClockRate, 2.0 + std::floor(unit() * 4.0))
        || !add(kGate, 0.04 + unit() * 0.52)
        || !add(kCcRate, 1.0 + std::floor(unit() * 4.0))
        || !add(kFormBars, 32.0 * (2.0 + std::floor(unit() * 7.0)))
        || !add(kDwellBars, 1.0 + std::floor(unit() * 16.0))
        || !add(kTransitionBars, unit() * 6.0)
        || !add(kClimate, 0.20 + unit() * 0.74)
        || !add(kSeed, 1.0 + std::floor(unit() * 65535.0))) return NO;
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        const uint32_t base = kGlobalParamCount + relay * kRelayParamCount;
        if (!add(base + kRelayThreshold, 0.22 + unit() * 0.58)
            || !add(base + kRelayBias, -0.42 + unit() * 0.84)
            || !add(base + kRelayRefractory,
                1.0 + std::floor(unit() * 12.0))
            || !add(base + kRelayFeedback, -0.72 + unit() * 1.44)
            || !add(base + kRelayGate, 0.45 + unit() * 1.45)
            || !add(base + kRelayTopology, std::floor(unit() * 4.0)))
            return NO;
    }
    if (!batch.submit(plugin)) return NO;
    plugin.formHold.store(false, std::memory_order_release);
    plugin.hasThawMemory.store(false, std::memory_order_relaxed);
    [self markCustomPreset:"RANDOM"];
    [self setNeedsDisplay:YES];
    return YES;
}

- (NSRect)openMenuRect
{
    if (_openMenu == kFactoryPresetMenuId && _menuItemCount > 0u) {
        const auto band = s3g::clap_gui::encoderTitleBand(
            kGuiWidth, kGuiHeight);
        const NSRect anchor = s3g::clap_gui::cocoaRect(band.presetMenu);
        return NSMakeRect(anchor.origin.x, NSMaxY(anchor) + 2.0,
            anchor.size.width, 18.0 * static_cast<CGFloat>(_menuItemCount));
    }
    if (_openMenu < 0 || _openMenu >= static_cast<NSInteger>(kParamCount)
        || _menuItemCount == 0u) return NSZeroRect;
    if (_openMenu == static_cast<NSInteger>(kScale)) {
        const uint32_t rows = s3g::clap_gui::multiColumnMenuRows(
            s3g::kMusicalScaleCount, 4u);
        return NSMakeRect(500.0, 66.0, 720.0,
            18.0 * static_cast<CGFloat>(rows));
    }
    const NSRect box = parameterMenuBoxRect(
        static_cast<uint32_t>(_openMenu));
    const uint32_t columns = parameterMenuColumns(
        static_cast<uint32_t>(_openMenu));
    const CGFloat height = 18.0 * static_cast<CGFloat>(
        s3g::clap_gui::multiColumnMenuRows(_menuItemCount, columns));
    const CGFloat below = NSMaxY(box) + 2.0;
    const CGFloat y = below + height <= static_cast<CGFloat>(kGuiHeight) - 8.0
        ? below : box.origin.y - height - 2.0;
    return NSMakeRect(box.origin.x, y, box.size.width, height);
}

- (void)drawOpenMenu:(NSDictionary*)attributes
    style:(const s3g::clap_gui::Style&)style
{
    if (_openMenu < 0 || _menuItemCount == 0u || !_plugin) return;
    if (_openMenu == kFactoryPresetMenuId) {
        NSString* items[kFactoryPresetCount] {};
        for (uint32_t item = 0u; item < kFactoryPresetCount; ++item)
            items[item] = [NSString stringWithUTF8String:
                factoryPresetName(item)];
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
            items, kFactoryPresetCount, static_cast<int>(_factoryPresetIndex),
            static_cast<int>(_hoverMenuItem), attributes, style);
        return;
    }
    if (_openMenu >= static_cast<NSInteger>(kParamCount)) return;
    if (_openMenu == static_cast<NSInteger>(kScale)) {
        static const std::array<NSString*, s3g::kMusicalScaleCount> items = [] {
            std::array<NSString*, s3g::kMusicalScaleCount> result {};
            for (uint32_t item = 0u; item < result.size(); ++item) {
                const uint32_t scale =
                    s3g::musicalScaleValueForMenuIndex(item);
                result[item] = [[NSString alloc] initWithUTF8String:
                    s3g::musicalScaleDefinition(scale).name];
            }
            return result;
        }();
        const auto& plugin = *static_cast<Plugin*>(_plugin);
        s3g::clap_gui::drawMultiColumnDropdownMenu(
            [self openMenuRect], 18.0, items.data(), _menuItemCount, 4u,
            parameterMenuSelection(kScale, plugin),
            static_cast<int>(_hoverMenuItem), attributes, style);
        return;
    }
    NSString* items[16] {};
    for (uint32_t item = 0u; item < _menuItemCount; ++item)
        items[item] = parameterMenuItem(
            static_cast<uint32_t>(_openMenu), item);
    const auto& plugin = *static_cast<Plugin*>(_plugin);
    const uint32_t index = static_cast<uint32_t>(_openMenu);
    const uint32_t columns = parameterMenuColumns(index);
    if (columns > 1u) {
        s3g::clap_gui::drawMultiColumnDropdownMenu(
            [self openMenuRect], 18.0, items, _menuItemCount, columns,
            parameterMenuSelection(index, plugin),
            static_cast<int>(_hoverMenuItem), attributes, style);
    } else {
        s3g::clap_gui::drawDropdownMenu([self openMenuRect], 18.0,
            items, _menuItemCount, parameterMenuSelection(index, plugin),
            static_cast<int>(_hoverMenuItem), attributes, style);
    }
}

- (void)drawMidiConsole:(Plugin&)plugin
    style:(const s3g::clap_gui::Style&)style
    labels:(NSDictionary*)labels values:(NSDictionary*)values
{
    struct TraceLine {
        uint64_t sequence = 0u;
        double beat = 0.0;
        uint32_t packed = 0u;
    };
    constexpr uint32_t kVisibleLines = 22u;
    std::array<TraceLine, kVisibleLines> lines {};
    uint32_t matching = 0u;
    const uint64_t end = plugin.midiTraceWrite.load(std::memory_order_acquire);
    if (end < _consoleFloor) _consoleFloor = 0u;
    const uint64_t earliest = end > kMidiTraceCapacity
        ? end - kMidiTraceCapacity : 0u;
    const uint64_t begin = std::max<uint64_t>(earliest, _consoleFloor);
    for (uint64_t sequence = begin; sequence < end; ++sequence) {
        const auto& slot = plugin.midiTrace[static_cast<std::size_t>(
            sequence % kMidiTraceCapacity)];
        const uint64_t expected = sequence + 1u;
        if (slot.sequence.load(std::memory_order_acquire) != expected) continue;
        const double beat = slot.beat.load(std::memory_order_relaxed);
        const uint32_t packed = slot.packed.load(std::memory_order_relaxed);
        if (slot.sequence.load(std::memory_order_acquire) != expected) continue;
        const EventKind kind = static_cast<EventKind>(packed & 3u);
        if ((_consoleFilter == 1 && kind == EventKind::ControlChange)
            || (_consoleFilter == 2 && kind != EventKind::ControlChange))
            continue;
        lines[matching % kVisibleLines] = { expected, beat, packed };
        ++matching;
    }

    [@"SEQ       BEAT       SRC     MESSAGE" drawAtPoint:NSMakePoint(34.0, 100.0)
        withAttributes:labels];
    [style.grid setStroke];
    drawLine(NSMakePoint(34.0, 116.0), NSMakePoint(762.0, 116.0),
        style.grid, 0.75);
    const uint32_t count = std::min<uint32_t>(matching, kVisibleLines);
    const uint32_t first = matching > kVisibleLines
        ? matching % kVisibleLines : 0u;
    if (count == 0u) {
        [@"NO MATCHING MIDI OUTPUT YET" drawAtPoint:NSMakePoint(34.0, 128.0)
            withAttributes:values];
    }
    for (uint32_t row = 0u; row < count; ++row) {
        const TraceLine& line = lines[(first + row) % kVisibleLines];
        const EventKind kind = static_cast<EventKind>(line.packed & 3u);
        const uint32_t relay = (line.packed >> 2u) & 7u;
        const uint32_t channel = (line.packed >> 5u) & 15u;
        const uint32_t data1 = (line.packed >> 9u) & 127u;
        const uint32_t data2 = (line.packed >> 16u) & 127u;
        char message[96] {};
        if (kind == EventKind::ControlChange) {
            std::snprintf(message, sizeof(message),
                "%06llu  %8.3f  R%u CH%02u  CC %03u = %03u",
                static_cast<unsigned long long>(line.sequence), line.beat,
                relay + 1u, channel + 1u, data1, data2);
        } else {
            char note[32] {};
            midiNoteText(static_cast<int>(data1), note, sizeof(note));
            std::snprintf(message, sizeof(message),
                "%06llu  %8.3f  R%u CH%02u  %s %-9s V%03u",
                static_cast<unsigned long long>(line.sequence), line.beat,
                relay + 1u, channel + 1u,
                kind == EventKind::NoteOn ? "ON " : "OFF", note, data2);
        }
        NSColor* marker = kind == EventKind::NoteOn ? style.accent
            : (kind == EventKind::NoteOff
                ? s3g::clap_gui::color(0x587d81) : style.grid);
        [marker setFill];
        NSRectFill(NSMakeRect(28.0, 127.0 + static_cast<CGFloat>(row) * 17.0,
            3.0, 12.0));
        [[NSString stringWithUTF8String:message]
            drawAtPoint:NSMakePoint(36.0,
                125.0 + static_cast<CGFloat>(row) * 17.0)
            withAttributes:values];
    }
    const uint64_t sent = plugin.sentEvents.load(std::memory_order_relaxed);
    const uint64_t dropped = plugin.droppedEvents.load(std::memory_order_relaxed);
    [@"REAL OUTPUT EVENTS / NEWEST AT BOTTOM" drawAtPoint:NSMakePoint(34.0, 519.0)
        withAttributes:values];
    s3g::clap_gui::drawRightStatus(
        [NSString stringWithFormat:@"SENT %llu  DROP %llu",
            static_cast<unsigned long long>(sent),
            static_cast<unsigned long long>(dropped)],
        762.0, 519.0, values, 16.0);
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    if (!_plugin) return;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    const auto style = s3g::clap_gui::softTextStyle();
    NSDictionary* title = s3g::clap_gui::softTitleAttrs();
    NSDictionary* labels = s3g::clap_gui::softLabelAttrs();
    NSDictionary* values = s3g::clap_gui::softValueAttrs();
    [style.bg setFill];
    NSRectFill(self.bounds);

    const bool playing = plugin.visualPlaying.load(std::memory_order_relaxed);
    const double beat = plugin.visualBeat.load(std::memory_order_relaxed);
    const double energy = plugin.visualEnergy.load(std::memory_order_relaxed);
    const uint32_t cell = plugin.visualCell.load(std::memory_order_relaxed);
    const uint32_t latticeCells = s3g::relay::latticeCellCount(
        static_cast<uint32_t>(std::lround(
            publishedValue(plugin, kLatticeDepth))));
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    s3g::clap_gui::drawEncoderTitleBand(@"s3g RELAY",
        [NSString stringWithUTF8String:_presetName],
        [NSString stringWithFormat:@"%@  BEAT %.2f  CELL %u/%u  ENERGY %.2f",
            playing ? @"RUN" : @"STILL", beat,
            cell % latticeCells + 1u, latticeCells, energy],
        titleBand, title, labels, values, style);
    [@"NEURAL CONTROL ECOLOGY / MIDI" drawAtPoint:NSMakePoint(98.0, 15.0)
        withAttributes:labels];

    const NSRect graph = graphPanelRect();
    const NSRect conduct = conductPanelRect();
    const NSRect pitch = pitchPanelRect();
    const NSRect climate = climatePanelRect();
    const NSRect relays = relayPanelRect();
    s3g::clap_gui::drawPanelFrame(graph.origin.x, graph.origin.y,
        graph.size.width, graph.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"OUTPUT VIEW", true,
        graph.origin.x, graph.origin.y, graph.size.width, kPanelHeaderHeight,
        labels, style);
    static NSString* const visualPages[4] = {
        @"FIELD", @"LEARNING", @"FORM", @"MIDI",
    };
    for (uint32_t page = 0u; page < 4u; ++page) {
        s3g::clap_gui::drawToolboxHeaderButton(visualPageTabRect(page),
            NSMakeRect(graph.origin.x, graph.origin.y, graph.size.width,
                kPanelHeaderHeight), visualPages[page],
            _visualPage == static_cast<NSInteger>(page), labels, style);
    }
    s3g::clap_gui::drawPanelFrame(conduct.origin.x, conduct.origin.y,
        conduct.size.width, conduct.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"CONDUCT", true, conduct.origin.x,
        conduct.origin.y, conduct.size.width, kPanelHeaderHeight, labels, style);
    const bool crystallized = plugin.formHold.load(std::memory_order_acquire);
    s3g::clap_gui::drawToolboxHeaderActionButton(crystallizeRect(),
        NSMakeRect(conduct.origin.x, conduct.origin.y, conduct.size.width,
            kPanelHeaderHeight), crystallized ? @"THAW" : @"CRYSTALLIZE",
        labels, style);
    s3g::clap_gui::drawPanelFrame(pitch.origin.x, pitch.origin.y,
        pitch.size.width, pitch.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"PITCH / LOGIC SCALE", true,
        pitch.origin.x, pitch.origin.y, pitch.size.width,
        kPanelHeaderHeight, labels, style);
    s3g::clap_gui::drawPanelFrame(climate.origin.x, climate.origin.y,
        climate.size.width, climate.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"CLIMATE / LONG FORM", true,
        climate.origin.x, climate.origin.y, climate.size.width,
        kPanelHeaderHeight, labels, style);
    s3g::clap_gui::drawPanelFrame(relays.origin.x, relays.origin.y,
        relays.size.width, relays.size.height, style);
    s3g::clap_gui::drawPanelHeader(@"RELAYS", true, relays.origin.x,
        relays.origin.y, relays.size.width, kPanelHeaderHeight, labels, style);

    if (_visualPage == 0) {
        [self drawNeuralGraph:plugin style:style labels:labels values:values];
    } else if (_visualPage == 1) {
        [self drawLearningPlate:plugin style:style labels:labels values:values];
    } else if (_visualPage == 2) {
        [self drawFormDeck:plugin style:style labels:labels values:values];
    } else {
        static NSString* const consoleFilters[3] = {
            @"ALL", @"NOTES", @"CC",
        };
        for (uint32_t filter = 0u; filter < 3u; ++filter) {
            s3g::clap_gui::drawToolboxHeaderButton(consoleFilterRect(filter),
                graph, consoleFilters[filter],
                _consoleFilter == static_cast<NSInteger>(filter),
                labels, style);
        }
        s3g::clap_gui::drawToolboxHeaderActionButton(consoleClearRect(),
            graph, @"CLEAR", labels, style);
        [self drawMidiConsole:plugin style:style labels:labels values:values];
    }
    for (uint32_t index = 0u; index < kGlobalParamCount; ++index) {
        const NSRect row = globalRowRect(index);
        CGFloat panelX = 0.0;
        CGFloat panelWidth = 0.0;
        parameterPanelGeometry(index, panelX, panelWidth);
        drawParameter(plugin, index, row, panelX, panelWidth);
    }
    [@"PER-RELAY MODE / REGISTER + RECEPTOR + CLIMATE ADDRESS"
        drawAtPoint:NSMakePoint(806.0, 514.0) withAttributes:values];
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        s3g::clap_gui::drawToolboxHeaderButton(relayTabRect(relay),
            NSMakeRect(relays.origin.x, relays.origin.y, relays.size.width,
                kPanelHeaderHeight),
            [NSString stringWithFormat:@"R%u", relay + 1u],
            relay == static_cast<uint32_t>(_selectedRelay), labels, style);
    }
    const uint32_t base = kGlobalParamCount
        + static_cast<uint32_t>(_selectedRelay) * kRelayParamCount;
    for (uint32_t local = 0u; local < kRelayParamCount; ++local) {
        const NSRect row = relayRowRect(local);
        CGFloat panelX = 0.0;
        CGFloat panelWidth = 0.0;
        parameterPanelGeometry(base + local, panelX, panelWidth);
        drawParameter(plugin, base + local, row, panelX, panelWidth);
    }
    [@"COMPARATOR RISE -> ARTICULATION / NOTE + CC -> FIELD IMPULSE"
        drawAtPoint:NSMakePoint(34.0, 861.0) withAttributes:values];
    [@"FORM DECK VIEW / DWELL + GESTATION SHAPE LONG CYCLES"
        drawAtPoint:NSMakePoint(806.0, 861.0) withAttributes:values];
    [self drawOpenMenu:values style:style];
}

- (NSInteger)paramAtPoint:(NSPoint)point
{
    for (uint32_t index = 0u; index < kGlobalParamCount; ++index) {
        if (NSPointInRect(point, parameterInteractionRect(index)))
            return static_cast<NSInteger>(index);
    }
    const uint32_t base = kGlobalParamCount
        + static_cast<uint32_t>(_selectedRelay) * kRelayParamCount;
    for (uint32_t local = 0u; local < kRelayParamCount; ++local) {
        if (NSPointInRect(point, parameterInteractionRect(base + local)))
            return static_cast<NSInteger>(base + local);
    }
    return -1;
}

- (void)updateParameter:(NSInteger)index point:(NSPoint)point
{
    if (index < 0 || index >= static_cast<NSInteger>(kParamCount)) return;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    const uint32_t unsignedIndex = static_cast<uint32_t>(index);
    const ParamSpec spec = paramSpec(unsignedIndex);
    const NSRect rect = parameterSliderTrackRect(unsignedIndex);
    const double normalized = std::clamp(
        static_cast<double>((point.x - rect.origin.x) / rect.size.width),
        0.0, 1.0);
    double value = spec.minimum + normalized * (spec.maximum - spec.minimum);
    if (spec.stepped) value = std::round(value);
    queueGuiValue(plugin, spec.id, value);
    [self markCustomPreset:"CUSTOM"];
    [self setNeedsDisplay:YES];
}

- (void)mouseDown:(NSEvent*)event
{
    if (!_plugin) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    auto& plugin = *static_cast<Plugin*>(_plugin);
    if (_openMenu >= 0) {
        const bool factoryMenu = _openMenu == kFactoryPresetMenuId;
        const uint32_t columns = factoryMenu ? 1u : parameterMenuColumns(
            static_cast<uint32_t>(_openMenu));
        const int hit = columns > 1u
            ? s3g::clap_gui::multiColumnDropdownHitIndex(
                point, [self openMenuRect], 18.0, _menuItemCount, columns)
            : s3g::clap_gui::dropdownHitIndex(
                point, [self openMenuRect], 18.0, _menuItemCount);
        if (hit >= 0) {
            if (factoryMenu) {
                if (![self applyFactoryPreset:hit]) NSBeep();
            } else {
                const uint32_t index = static_cast<uint32_t>(_openMenu);
                const clap_id id = paramIdForIndex(index);
                queueGuiBegin(plugin, id);
                queueGuiValue(plugin, id,
                    parameterMenuValue(index, static_cast<uint32_t>(hit)));
                queueGuiEnd(plugin, id);
                [self markCustomPreset:"CUSTOM"];
            }
        }
        _openMenu = -1;
        _hoverMenuItem = -1;
        _menuItemCount = 0u;
        [self setNeedsDisplay:YES];
        return;
    }
    const auto titleBand = s3g::clap_gui::encoderTitleBand(
        kGuiWidth, kGuiHeight);
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.presetMenu))) {
        _openMenu = kFactoryPresetMenuId;
        _hoverMenuItem = -1;
        _menuItemCount = kFactoryPresetCount;
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.loadButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::loadPluginStatePreset(
                &plugin.plugin, @"Relay", &name)) {
            [self markCustomPreset:name ? [name UTF8String] : "CUSTOM"];
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.saveButton))) {
        NSString* name = nil;
        if (s3g::clap_gui::savePluginStatePreset(
                &plugin.plugin, @"Relay", &name)) {
            [self markCustomPreset:name ? [name UTF8String] : "CUSTOM"];
        } else {
            NSBeep();
        }
        [self setNeedsDisplay:YES];
        return;
    }
    if (NSPointInRect(point,
            s3g::clap_gui::cocoaRect(titleBand.randomButton))) {
        if (![self applySafeRandom]) NSBeep();
        return;
    }
    for (uint32_t page = 0u; page < 4u; ++page) {
        if (NSPointInRect(point, visualPageTabRect(page))) {
            _visualPage = static_cast<NSInteger>(page);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (_visualPage == 2) {
        const uint32_t depthIndex = static_cast<uint32_t>(std::lround(
            publishedValue(plugin, kLatticeDepth)));
        const uint32_t planes = s3g::relay::latticePlaneCount(depthIndex);
        for (uint32_t item = 0u; item <= planes; ++item) {
            if (NSPointInRect(point, formPlaneButtonRect(item))) {
                _formPlane = item == 0u ? -1
                    : static_cast<NSInteger>(item - 1u);
                [self setNeedsDisplay:YES];
                return;
            }
        }
    }
    if (_visualPage == 3) {
        for (uint32_t filter = 0u; filter < 3u; ++filter) {
            if (NSPointInRect(point, consoleFilterRect(filter))) {
                _consoleFilter = static_cast<NSInteger>(filter);
                [self setNeedsDisplay:YES];
                return;
            }
        }
        if (NSPointInRect(point, consoleClearRect())) {
            _consoleFloor = plugin.midiTraceWrite.load(
                std::memory_order_acquire);
            [self setNeedsDisplay:YES];
            return;
        }
    }
    if (NSPointInRect(point, crystallizeRect())) {
        const bool crystallized = plugin.formHold.load(
            std::memory_order_acquire);
        if (!crystallized) {
            plugin.thawMemory.store(publishedValue(plugin, kMemory),
                std::memory_order_relaxed);
            plugin.hasThawMemory.store(true, std::memory_order_relaxed);
        }
        const bool hasThawMemory = plugin.hasThawMemory.load(
            std::memory_order_relaxed);
        const double memory = crystallized
            ? (hasThawMemory
                ? plugin.thawMemory.load(std::memory_order_relaxed)
                : kDefaultConfig.memory)
            : 1.0;
        const double freeze = crystallized ? 0.0 : 1.0;
        plugin.formHold.store(!crystallized, std::memory_order_release);
        for (const auto& change : {
                 std::pair<uint32_t, double> { uint32_t(kMemory), memory },
                 std::pair<uint32_t, double> { uint32_t(kFreeze), freeze },
             }) {
            const uint32_t index = change.first;
            const clap_id id = paramIdForIndex(index);
            queueGuiBegin(plugin, id);
            queueGuiValue(plugin, id, change.second);
            queueGuiEnd(plugin, id);
        }
        if (crystallized)
            plugin.hasThawMemory.store(false, std::memory_order_relaxed);
        [self markCustomPreset:"CUSTOM"];
        [self setNeedsDisplay:YES];
        return;
    }
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        if (NSPointInRect(point, relayTabRect(relay))
            || (_visualPage == 0
                && NSPointInRect(point, relayGlyphRect(relay)))) {
            _selectedRelay = static_cast<NSInteger>(relay);
            _dragParam = -1;
            [self setNeedsDisplay:YES];
            return;
        }
    }
    _dragParam = [self paramAtPoint:point];
    if (_dragParam < 0) return;
    const uint32_t index = static_cast<uint32_t>(_dragParam);
    const ParamSpec spec = paramSpec(index);
    if (parameterIsMenu(index)) {
        _openMenu = static_cast<NSInteger>(index);
        _hoverMenuItem = -1;
        _menuItemCount = parameterMenuCount(index);
        _dragParam = -1;
        [self setNeedsDisplay:YES];
        return;
    }
    queueGuiBegin(plugin, spec.id);
    if (parameterIsBinary(index)) {
        queueGuiValue(plugin, spec.id,
            publishedValue(plugin, index) >= 0.5 ? 0.0 : 1.0);
        queueGuiEnd(plugin, spec.id);
        [self markCustomPreset:"CUSTOM"];
        _dragParam = -1;
    } else {
        double defaultValue = spec.defaultValue;
        if (s3g::clap_gui::sliderDoubleClickDefault(event,
                &plugin.plugin, spec.id, &defaultValue)) {
            queueGuiValue(plugin, spec.id, defaultValue);
            queueGuiEnd(plugin, spec.id);
            _dragParam = -1;
            [self markCustomPreset:"CUSTOM"];
        } else {
            [self updateParameter:_dragParam point:point];
        }
    }
}

- (void)mouseDragged:(NSEvent*)event
{
    if (_dragParam < 0) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    [self updateParameter:_dragParam point:point];
}

- (void)mouseUp:(NSEvent*)event
{
    (void)event;
    if (_dragParam < 0 || !_plugin) return;
    auto& plugin = *static_cast<Plugin*>(_plugin);
    queueGuiEnd(plugin, paramIdForIndex(static_cast<uint32_t>(_dragParam)));
    _dragParam = -1;
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];
    [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)mouseMoved:(NSEvent*)event
{
    if (_openMenu < 0) return;
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const uint32_t columns = _openMenu == kFactoryPresetMenuId ? 1u
        : parameterMenuColumns(static_cast<uint32_t>(_openMenu));
    const int hover = columns > 1u
        ? s3g::clap_gui::multiColumnDropdownHitIndex(
            point, [self openMenuRect], 18.0, _menuItemCount, columns)
        : s3g::clap_gui::dropdownHitIndex(
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

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating)
{
    if (!api || !floating) return false;
    *api = CLAP_WINDOW_API_COCOA;
    *floating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool floating)
{
    if (!guiIsApiSupported(plugin, api, floating)) return false;
    auto* instance = self(plugin);
    if (instance->guiView) return true;
    auto* view = [[S3GRelayView alloc] initWithPlugin:instance];
    if (!view) return false;
    instance->guiView = view;
    if (!s3g::clap_gui::createResponsiveViewport(instance->guiViewport,
            view, kGuiWidth, kGuiHeight,
            kGuiMinimumWidth, kGuiMinimumHeight)) {
        [view release];
        instance->guiView = nullptr;
        return false;
    }
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance || !instance->guiView) return;
    [static_cast<S3GRelayView*>(instance->guiView) stopRefreshTimer];
    s3g::clap_gui::destroyResponsiveViewport(
        instance->guiViewport, instance->guiView);
}

bool guiSetScale(const clap_plugin_t*, double) { return true; }

bool guiGetSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::getResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height, kGuiMinimumWidth, kGuiMinimumHeight);
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints)
{
    return s3g::clap_gui::getResponsiveResizeHints(hints);
}

bool guiAdjustSize(const clap_plugin_t* plugin, uint32_t* width,
    uint32_t* height)
{
    return s3g::clap_gui::adjustResponsiveViewportSize(
        self(plugin)->guiViewport, kGuiWidth, kGuiHeight,
        width, height, kGuiMinimumWidth, kGuiMinimumHeight);
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
        || !window->cocoa) return false;
    auto* instance = self(plugin);
    return s3g::clap_gui::setResponsiveViewportParent(
        instance->guiViewport, static_cast<NSView*>(window->cocoa),
        instance->host);
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView
        || !s3g::clap_gui::setResponsiveViewportHidden(
            instance->guiViewport, false)) return false;
    instance->guiVisible = true;
    [static_cast<S3GRelayView*>(instance->guiView) startRefreshTimer];
    return true;
}

bool guiHide(const clap_plugin_t* plugin)
{
    auto* instance = self(plugin);
    if (!instance->guiView) return false;
    instance->guiVisible = false;
    [static_cast<S3GRelayView*>(instance->guiView) stopRefreshTimer];
    return s3g::clap_gui::setResponsiveViewportHidden(
        instance->guiViewport, true);
}

const clap_plugin_gui_t guiExtension {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy,
    guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient,
    guiSuggestTitle, guiShow, guiHide,
};

} // namespace

#endif

namespace {

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
#if defined(__APPLE__)
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension;
#endif
    return nullptr;
}

const char* const features[] {
    CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr,
};

const clap_plugin_descriptor_t descriptor {
    CLAP_VERSION_INIT,
    "org.s3g.s3g-dsp.relay",
    "s3g Relay",
    "s3g",
    "https://github.com/s3g/s3g-dsp",
    "",
    "",
    "0.1.0",
    "Recurrent neural-control MIDI sequencer with comparator relays, recirculating memory, and climate-lattice form.",
    features,
};

const clap_plugin_t* createPlugin(const clap_plugin_factory*,
    const clap_host_t* host, const char* pluginId)
{
    if (!pluginId || std::strcmp(pluginId, descriptor.id) != 0)
        return nullptr;
    auto* instance = new (std::nothrow) Plugin();
    if (!instance) return nullptr;
    instance->host = host;
    publishAll(*instance);
    instance->plugin.desc = &descriptor;
    instance->plugin.plugin_data = instance;
    instance->plugin.init = init;
    instance->plugin.destroy = destroy;
    instance->plugin.activate = activate;
    instance->plugin.deactivate = deactivate;
    instance->plugin.start_processing = startProcessing;
    instance->plugin.stop_processing = stopProcessing;
    instance->plugin.reset = reset;
    instance->plugin.process = process;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = onMainThread;
    return &instance->plugin;
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
    createPlugin,
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
