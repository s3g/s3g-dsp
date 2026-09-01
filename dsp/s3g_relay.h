#pragma once

#include "s3g_musical_scales.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g::relay {

constexpr uint32_t kClusterCount = 4u;
constexpr uint32_t kNodesPerCluster = 5u;
constexpr uint32_t kNodeCount = kClusterCount * kNodesPerCluster;
constexpr uint32_t kRelayCount = 8u;
constexpr uint32_t kVoicesPerRelay = 4u;
constexpr uint32_t kClimateWidth = 4u;
constexpr uint32_t kClimateHeight = 4u;
constexpr uint32_t kClimateCellsPerPlane = kClimateWidth * kClimateHeight;
constexpr uint32_t kClimateMaxPlanes = 4u;
constexpr uint32_t kClimateCells = kClimateCellsPerPlane * kClimateMaxPlanes;
constexpr uint32_t kTrailLength = 16u;
constexpr uint32_t kExternalInputQueueCapacity = 256u;
constexpr uint16_t kMidiOff = 128u;

constexpr double kPi = 3.1415926535897932384626433832795;

// Each five-node pentad is a bidirectional ring. Clockwise from its top node,
// the roles are Initiate, Accumulate, Integrate, Invert, and Recover. A target
// receives the forward weight from its preceding neighbor and the reverse
// weight from its following neighbor. These public constants keep the field
// view identical to the actual engine topology.
inline constexpr std::array<const char*, kNodesPerCluster> kPentadRoleNames {{
    "INIT", "ACCUM", "INTEGRATE", "INVERT", "RECOVER",
}};
inline constexpr std::array<double, kNodesPerCluster> kRingForwardWeights {{
    0.82, 1.18, 1.03, 1.26, -1.14,
}};
inline constexpr std::array<double, kNodesPerCluster> kRingReverseWeights {{
    -0.20, 0.16, 0.20, -0.14, 0.12,
}};
inline constexpr std::array<double, kNodesPerCluster> kPentadBias {{
    0.10, -0.035, 0.05, 0.11, -0.13,
}};
inline constexpr std::array<double, kNodesPerCluster> kPentadResponse {{
    1.10, 0.82, 0.62, 1.04, 0.42,
}};
inline constexpr std::array<double, kNodesPerCluster>
    kPentadImpulseResponse {{
        0.54, 0.62, 0.70, -0.72, 0.34,
    }};

inline constexpr uint32_t receptorPentadTap(uint32_t relay) noexcept
{
    // The first receptor bank hears Initiate; the paired bank hears Invert.
    return relay < kClusterCount ? 0u : 3u;
}

// Destination-major cluster coupling matrix:
// index = target * kClusterCount + source.
// Plasticity is added to these base weights at runtime.
inline constexpr std::array<double, kClusterCount * kClusterCount>
    kInterClusterWeights {{
         0.00,  0.31, -0.17,  0.10,
        -0.28,  0.00,  0.23,  0.13,
         0.16, -0.26,  0.00,  0.29,
        -0.12,  0.19, -0.25,  0.00,
    }};

enum class EventKind : uint8_t {
    NoteOn,
    NoteOff,
    ControlChange,
};

enum class ReceptorTopology : uint8_t {
    Local,
    Cross,
    Diffuse,
    Roaming,
};

enum class PitchMode : uint8_t {
    Fixed,
    ScaleLogic,
};

enum class ArticulationMode : uint8_t {
    Restart,
    Hold,
    Extend,
    Stack,
};

enum class DealerLaw : uint8_t {
    EcologicalDrift,
    CompassRose,
    SeededWander,
    AvoidRecent,
    ClimateContrast,
    PlaneTides,
};

constexpr uint32_t kDealerLawCount = 6u;

inline const char* dealerLawName(DealerLaw law) noexcept
{
    switch (law) {
    case DealerLaw::EcologicalDrift: return "Ecological Drift";
    case DealerLaw::CompassRose: return "Compass Rose";
    case DealerLaw::SeededWander: return "Seeded Wander";
    case DealerLaw::AvoidRecent: return "Avoid Recent";
    case DealerLaw::ClimateContrast: return "Climate Contrast";
    case DealerLaw::PlaneTides: return "Plane Tides";
    }
    return "Ecological Drift";
}

enum class MidiInputMode : uint8_t {
    Off,
    Notes,
    ControlChange,
    NotesAndControlChange,
};

enum class MidiInputMap : uint8_t {
    NodeAddress,
    ClusterAddress,
    RelayAddress,
    Diffuse,
};

enum class MidiInputResponse : uint8_t {
    Impulse,
    Gate,
};

enum class MidiInputPolarity : uint8_t {
    Excitatory,
    Signed,
};

inline const char* midiInputModeName(MidiInputMode mode) noexcept
{
    switch (mode) {
    case MidiInputMode::Off: return "Off";
    case MidiInputMode::Notes: return "Notes";
    case MidiInputMode::ControlChange: return "CC";
    case MidiInputMode::NotesAndControlChange: return "Notes + CC";
    }
    return "Off";
}

inline const char* midiInputMapName(MidiInputMap map) noexcept
{
    switch (map) {
    case MidiInputMap::NodeAddress: return "Node Address";
    case MidiInputMap::ClusterAddress: return "Cluster Address";
    case MidiInputMap::RelayAddress: return "Relay Address";
    case MidiInputMap::Diffuse: return "Diffuse";
    }
    return "Node Address";
}

inline const char* midiInputResponseName(MidiInputResponse response) noexcept
{
    return response == MidiInputResponse::Gate ? "Gate" : "Impulse";
}

inline const char* midiInputPolarityName(MidiInputPolarity polarity) noexcept
{
    return polarity == MidiInputPolarity::Signed ? "Signed" : "Excitatory";
}

enum class CcSource : uint8_t {
    Receptor,
    ClusterSigned,
    ClusterEnergy,
    RegisterPopulation,
    EnergyRegister,
    ClimateEnergy,
    ClimateCoupling,
    ClimateHierarchy,
    ClimateContrast,
    Gestation,
    FormPhase,
    PlasticityDrift,
};

constexpr uint32_t kCcSourceCount = 12u;

inline const char* ccSourceName(CcSource source) noexcept
{
    switch (source) {
    case CcSource::Receptor: return "Receptor";
    case CcSource::ClusterSigned: return "Cluster Signed";
    case CcSource::ClusterEnergy: return "Cluster Energy";
    case CcSource::RegisterPopulation: return "Register Population";
    case CcSource::EnergyRegister: return "Energy + Register";
    case CcSource::ClimateEnergy: return "Climate Energy";
    case CcSource::ClimateCoupling: return "Climate Coupling";
    case CcSource::ClimateHierarchy: return "Climate Hierarchy";
    case CcSource::ClimateContrast: return "Climate Contrast";
    case CcSource::Gestation: return "Gestation";
    case CcSource::FormPhase: return "Form Phase";
    case CcSource::PlasticityDrift: return "Plasticity Drift";
    }
    return "Receptor";
}

struct CcLaneConfig {
    CcSource source = CcSource::Receptor;
    uint8_t minimum = 0u;
    uint8_t maximum = 127u;
    double curve = 0.0;
    double slew = 0.0;
};

struct Event {
    double beat = 0.0;
    EventKind kind = EventKind::ControlChange;
    uint8_t channel = 0u;
    uint8_t data1 = 0u;
    uint8_t data2 = 0u;
    uint8_t relay = 0u;
};

struct ExternalMidiEvent {
    double beat = 0.0;
    EventKind kind = EventKind::NoteOn;
    uint8_t channel = 0u;
    uint8_t data1 = 0u;
    uint8_t data2 = 0u;
};

struct RelayConfig {
    bool enabled = true;
    uint8_t channel = 0u;
    uint16_t note = 36u;
    uint16_t ccA = 20u;
    uint16_t ccB = kMidiOff;
    double threshold = 0.48;
    double bias = 0.0;
    uint32_t refractoryTicks = 1u;
    double feedback = 0.45;
    double gateScale = 1.0;
    ReceptorTopology topology = ReceptorTopology::Local;
    PitchMode pitchMode = PitchMode::Fixed;
    ArticulationMode articulation = ArticulationMode::Restart;
    std::array<CcLaneConfig, 2u> ccLanes {{
        { CcSource::Receptor, 0u, 127u, 0.0, 0.0 },
        { CcSource::EnergyRegister, 0u, 127u, 0.0, 0.0 },
    }};
};

struct Config {
    bool enabled = true;
    double activity = 0.58;
    double coupling = 0.48;
    double memory = 0.72;
    double mutation = 0.18;
    double hierarchy = 0.56;
    double contrast = 0.52;
    bool freeze = false;
    bool formHold = false; // Compound Crystallize action; not field Freeze.
    // A finite value restores a saved Crystallize position when formHold first
    // becomes active. It is transient runtime state, not an exposed control.
    double requestedFormHoldBeat = std::numeric_limits<double>::quiet_NaN();
    uint32_t clockRateIndex = 4u;
    double gateBeats = 0.18;
    uint32_t ccRateIndex = 1u;
    uint32_t formBars = 128u;
    uint32_t dwellBars = 8u;
    double transitionBars = 2.0;
    double climate = 0.68;
    uint32_t seed = 1977u;
    uint32_t scaleRoot = 0u;
    int32_t scaleOctave = 3;
    uint32_t scale = 31u; // Canonical s3g PENTATONIC MINOR scale ID.
    uint32_t scaleRange = 2u;
    uint32_t latticeDepthIndex = 2u; // Four 4x4 planes.
    DealerLaw dealerLaw = DealerLaw::EcologicalDrift;
    MidiInputMode midiInputMode = MidiInputMode::Off;
    uint32_t midiInputChannel = 0u; // 0 = Omni, 1..16 = channel.
    MidiInputMap midiInputMap = MidiInputMap::NodeAddress;
    MidiInputResponse midiInputResponse = MidiInputResponse::Impulse;
    uint32_t midiInputCc = 1u;
    MidiInputPolarity midiInputPolarity = MidiInputPolarity::Excitatory;
    double midiInputDepth = 0.65;
    double midiInputDecayBeats = 0.75;
    std::array<RelayConfig, kRelayCount> relays {};

    Config()
    {
        static constexpr std::array<uint8_t, kRelayCount> notes {{
            36u, 38u, 40u, 41u, 43u, 45u, 47u, 48u,
        }};
        static constexpr std::array<uint8_t, kRelayCount> cc {{
            20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u,
        }};
        static constexpr std::array<uint32_t, kRelayCount> refractory {{
            1u, 2u, 3u, 4u, 5u, 7u, 8u, 11u,
        }};
        static constexpr std::array<double, kRelayCount> biases {{
            0.18, -0.08, 0.11, -0.17, 0.05, -0.12, 0.15, -0.03,
        }};
        static constexpr std::array<double, kRelayCount> feedbacks {{
            0.52, -0.38, 0.46, -0.44, 0.34, -0.50, 0.41, -0.31,
        }};
        for (uint32_t index = 0u; index < kRelayCount; ++index) {
            auto& relay = relays[index];
            relay.channel = 0u;
            relay.note = notes[index];
            relay.ccA = cc[index];
            relay.ccB = kMidiOff;
            relay.threshold = 0.42 + 0.045
                * static_cast<double>(index % kClusterCount);
            relay.bias = biases[index];
            relay.refractoryTicks = refractory[index];
            relay.feedback = feedbacks[index];
            relay.gateScale = 0.72 + 0.14
                * static_cast<double>(index % kClusterCount);
            relay.topology = static_cast<ReceptorTopology>(
                index % kClusterCount);
        }
    }
};

struct Snapshot {
    std::array<double, kNodeCount> nodes {};
    std::array<double, kClusterCount> clusters {};
    std::array<double, kRelayCount> receptors {};
    std::array<double, kClusterCount * kClusterCount> plasticity {};
    std::array<uint32_t, kTrailLength> trail {};
    uint8_t registerBits = 0u;
    uint32_t currentCell = 5u;
    uint32_t previousCell = 5u;
    uint32_t trailCount = 1u;
    double climateBlend = 1.0;
    double cyclePhase = 0.0;
    double energy = 0.0;
    int64_t cycleIndex = 0;
    double formBeat = 0.0;
    bool formHold = false;
    std::array<double, 4u> climateTraits {};
    std::array<double, 4u> effectiveConduct {};
    std::array<double, kNodeCount> externalInputNodes {};
    double externalInputActivity = 0.0;
};

inline double clampFinite(double value, double minimum, double maximum,
    double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, minimum, maximum)
                                : fallback;
}

inline uint64_t mix64(uint64_t value) noexcept
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31u);
}

inline double unitHash(uint32_t seed, uint64_t a, uint64_t b = 0u) noexcept
{
    uint64_t value = static_cast<uint64_t>(seed) ^ (a * 0xd6e8feb86659fd93ULL)
        ^ (b * 0xa0761d6478bd642fULL);
    return static_cast<double>(mix64(value) >> 11u)
        * (1.0 / 9007199254740992.0);
}

inline double signedHash(uint32_t seed, uint64_t a, uint64_t b = 0u) noexcept
{
    return unitHash(seed, a, b) * 2.0 - 1.0;
}

inline double clockPeriodBeats(uint32_t index) noexcept
{
    static constexpr std::array<double, 7u> periods {{
        4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625,
    }};
    return periods[std::min<uint32_t>(index,
        static_cast<uint32_t>(periods.size() - 1u))];
}

inline uint32_t clockTicksPerBeatText(uint32_t index) noexcept
{
    static constexpr std::array<uint32_t, 7u> values {{
        0u, 0u, 1u, 2u, 4u, 8u, 16u,
    }};
    return values[std::min<uint32_t>(index,
        static_cast<uint32_t>(values.size() - 1u))];
}

inline uint32_t ccEveryTicks(uint32_t index) noexcept
{
    static constexpr std::array<uint32_t, 6u> values {{
        1u, 2u, 4u, 8u, 16u, 32u,
    }};
    return values[std::min<uint32_t>(index,
        static_cast<uint32_t>(values.size() - 1u))];
}

inline constexpr uint32_t latticePlaneCount(uint32_t depthIndex) noexcept
{
    constexpr std::array<uint32_t, 3u> counts {{ 1u, 2u, 4u }};
    return counts[std::min<uint32_t>(depthIndex, 2u)];
}

inline constexpr uint32_t latticeCellCount(uint32_t depthIndex) noexcept
{
    return latticePlaneCount(depthIndex) * kClimateCellsPerPlane;
}

inline const char* latticeDepthName(uint32_t depthIndex) noexcept
{
    constexpr std::array<const char*, 3u> names {{
        "Sheet", "2 Planes", "4 Planes",
    }};
    return names[std::min<uint32_t>(depthIndex, 2u)];
}

inline double climateCellTrait(uint32_t seed, int64_t cycleIndex,
    uint32_t cell, uint32_t trait) noexcept
{
    const uint32_t plane = cell / kClimateCellsPerPlane;
    const uint64_t cycle = static_cast<uint64_t>(cycleIndex);
    const double planeTrait = signedHash(seed,
        cycle * 43u + plane + 1u, 1700u + trait * 29u);
    const double localTrait = signedHash(seed,
        cycle * 131u + cell + 1u, 700u + trait * 17u);
    return std::clamp(planeTrait * 0.64 + localTrait * 0.36, -1.0, 1.0);
}

inline const char* topologyName(ReceptorTopology topology) noexcept
{
    switch (topology) {
    case ReceptorTopology::Local: return "Local";
    case ReceptorTopology::Cross: return "Cross";
    case ReceptorTopology::Diffuse: return "Diffuse";
    case ReceptorTopology::Roaming: return "Roaming";
    }
    return "Local";
}

inline const char* pitchModeName(PitchMode mode) noexcept
{
    return mode == PitchMode::ScaleLogic ? "Scale Logic" : "Fixed";
}

inline const char* articulationModeName(ArticulationMode mode) noexcept
{
    switch (mode) {
    case ArticulationMode::Restart: return "Restart";
    case ArticulationMode::Hold: return "Hold";
    case ArticulationMode::Extend: return "Extend";
    case ArticulationMode::Stack: return "Stack";
    }
    return "Restart";
}

inline uint8_t logicScaleNote(const Config& config, uint32_t relay,
    uint8_t registerBits, double receptor, uint32_t climateCell) noexcept
{
    const auto& scale = s3g::musicalScaleDefinition(config.scale);
    const uint32_t scaleSize = scale.size;
    const int32_t root = std::clamp(
        (config.scaleOctave + 1) * 12
            + static_cast<int32_t>(config.scaleRoot),
        0, 127);
    const uint32_t requested = scaleSize
        * std::clamp<uint32_t>(config.scaleRange, 1u, 4u);
    uint32_t available = 0u;
    for (uint32_t degree = 0u; degree < requested; ++degree) {
        const int32_t note = root
            + static_cast<int32_t>((degree / scaleSize) * 12u)
            + static_cast<int32_t>(scale.semitones[degree % scaleSize]);
        if (note > 127) break;
        ++available;
    }
    if (available == 0u) return static_cast<uint8_t>(root);

    const uint32_t shift = relay & 7u;
    const uint8_t rotated = shift == 0u ? registerBits
        : static_cast<uint8_t>((registerBits >> shift)
            | (static_cast<uint32_t>(registerBits) << (8u - shift)));
    const uint32_t lowTap = rotated & 7u;
    const uint32_t highTap = (rotated >> 3u) & 7u;
    const uint32_t upperTap = (rotated >> 6u) & 3u;
    const uint32_t fieldBand = static_cast<uint32_t>(std::clamp(
        static_cast<int32_t>(std::lround((receptor + 1.0) * 1.5)),
        0, 3));
    const uint32_t address = (lowTap ^ highTap) + upperTap * 2u
        + (relay & 7u) * 3u + fieldBand
        + climateCell % scaleSize;
    const uint32_t degree = address % available;
    const int32_t note = root
        + static_cast<int32_t>((degree / scaleSize) * 12u)
        + static_cast<int32_t>(scale.semitones[degree % scaleSize]);
    return static_cast<uint8_t>(std::clamp(note, 0, 127));
}

class Engine {
public:
    struct Result {
        uint32_t count = 0u;
        uint32_t dropped = 0u;
        Snapshot snapshot {};
    };

    void reset() noexcept
    {
        nodes_.fill(0.0);
        clusters_.fill(0.0);
        receptors_.fill(0.0);
        plasticity_.fill(0.0);
        pendingImpulse_.fill(0.0);
        clearExternalInputState();
        for (auto& relay : activeNotes_)
            for (auto& note : relay) note = {};
        noteOwners_.fill(0u);
        for (auto& cc : lastCc_) cc = {{ -1, -1 }};
        for (auto& cc : smoothedCc_)
            cc = {{ std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::quiet_NaN() }};
        lastFireTick_.fill(std::numeric_limits<int64_t>::min() / 4);
        trail_.fill(5u);
        registerBits_ = 0x96u;
        currentCell_ = 5u;
        previousCell_ = 5u;
        trailCount_ = 1u;
        cycleIndex_ = std::numeric_limits<int64_t>::min();
        cellStep_ = 0;
        nextTick_ = 0;
        expectedBeat_ = 0.0;
        lastBeatPerBar_ = 4.0;
        lastTickPeriod_ = 0.25;
        formBeatOffset_ = 0.0;
        heldFormBeat_ = 0.0;
        formHold_ = false;
        continuous_ = false;
        snapshot_ = {};
    }

    void invalidate() noexcept { continuous_ = false; }

    // Force the next process block to reconstruct the form/field position,
    // while retaining active notes long enough for the normal discontinuity
    // path to emit their Note Off messages.
    void invalidateFormTimeline() noexcept
    {
        formHold_ = false;
        cycleIndex_ = std::numeric_limits<int64_t>::min();
        continuous_ = false;
    }

    const Snapshot& snapshot() const noexcept { return snapshot_; }

    Result process(double beginBeat, double endBeat, double beatsPerBar,
        bool playing, const Config& sourceConfig, Event* events,
        uint32_t capacity) noexcept
    {
        return process(beginBeat, endBeat, beatsPerBar, playing,
            sourceConfig, nullptr, 0u, events, capacity);
    }

    Result process(double beginBeat, double endBeat, double beatsPerBar,
        bool playing, const Config& sourceConfig,
        const ExternalMidiEvent* inputEvents, uint32_t inputEventCount,
        Event* events, uint32_t capacity) noexcept
    {
        Result result;
        if (!events || capacity == 0u || !std::isfinite(beginBeat)
            || !std::isfinite(endBeat) || endBeat < beginBeat) {
            result.snapshot = snapshot_;
            return result;
        }

        const Config config = sanitize(sourceConfig);
        const double bar = clampFinite(beatsPerBar, 1.0, 32.0, 4.0);
        const double tickPeriod = clockPeriodBeats(config.clockRateIndex);
        updateFormHold(config.formHold, beginBeat,
            config.requestedFormHoldBeat);

        if (!playing || !config.enabled) {
            releaseAll(beginBeat, events, capacity, result);
            invalidateCc();
            clearExternalInputState();
            continuous_ = false;
            expectedBeat_ = endBeat;
            updateSnapshot(config, beginBeat, bar);
            result.snapshot = snapshot_;
            return result;
        }

        releaseUnavailable(config, beginBeat, events, capacity, result);
        const bool discontinuity = !continuous_
            || std::abs(beginBeat - expectedBeat_) > 1.0e-6
            || std::abs(bar - lastBeatPerBar_) > 1.0e-9
            || std::abs(tickPeriod - lastTickPeriod_) > 1.0e-12;
        if (discontinuity) {
            releaseAll(beginBeat, events, capacity, result);
            clearExternalInputState();
            if (formHold_ && cycleIndex_
                    != std::numeric_limits<int64_t>::min()) {
                nextTick_ = firstTickAtOrAfter(beginBeat, tickPeriod);
                continuous_ = true;
                expectedBeat_ = beginBeat;
                lastBeatPerBar_ = bar;
                lastTickPeriod_ = tickPeriod;
            } else {
                initializeForPosition(config, beginBeat, bar, tickPeriod);
            }
            invalidateCc();
        }

        for (uint32_t index = 0u; index < inputEventCount; ++index) {
            if (!inputEvents || !queueExternalInput(inputEvents[index])) {
                ++result.dropped;
                break;
            }
        }

        releaseDue(beginBeat, true, events, capacity, result);
        constexpr double epsilon = 1.0e-10;
        while (tickBeat(nextTick_, tickPeriod) < endBeat - epsilon) {
            const double beat = tickBeat(nextTick_, tickPeriod);
            if (beat >= beginBeat - epsilon) {
                applyExternalInputsThrough(config, beat);
                releaseDue(beat, true, events, capacity, result);
                advanceTick(config, nextTick_, beat, bar,
                    true, events, capacity, result);
            } else {
                advanceTick(config, nextTick_, beat, bar,
                    false, nullptr, 0u, result);
            }
            ++nextTick_;
        }
        releaseDue(endBeat, false, events, capacity, result);

        continuous_ = true;
        expectedBeat_ = endBeat;
        lastBeatPerBar_ = bar;
        lastTickPeriod_ = tickPeriod;
        updateSnapshot(config, endBeat, bar);
        result.snapshot = snapshot_;
        return result;
    }

private:
    struct ActiveNote {
        bool active = false;
        uint8_t channel = 0u;
        uint8_t note = 0u;
        double startBeat = 0.0;
        double offBeat = 0.0;
    };

    static Config sanitize(Config config) noexcept
    {
        config.activity = clampFinite(config.activity, 0.0, 1.0, 0.58);
        config.coupling = clampFinite(config.coupling, 0.0, 1.0, 0.48);
        config.memory = clampFinite(config.memory, 0.0, 1.0, 0.72);
        config.mutation = clampFinite(config.mutation, 0.0, 1.0, 0.18);
        config.hierarchy = clampFinite(config.hierarchy, 0.0, 1.0, 0.56);
        config.contrast = clampFinite(config.contrast, 0.0, 1.0, 0.52);
        config.clockRateIndex = std::min<uint32_t>(config.clockRateIndex, 6u);
        config.gateBeats = clampFinite(config.gateBeats, 0.005, 8.0, 0.18);
        config.ccRateIndex = std::min<uint32_t>(config.ccRateIndex, 5u);
        config.formBars = std::clamp<uint32_t>(config.formBars, 16u, 512u);
        config.dwellBars = std::clamp<uint32_t>(config.dwellBars, 1u, 64u);
        config.transitionBars = clampFinite(config.transitionBars, 0.0, 32.0, 2.0);
        config.climate = clampFinite(config.climate, 0.0, 1.0, 0.68);
        if (config.seed == 0u) config.seed = 1u;
        config.scaleRoot = std::min<uint32_t>(config.scaleRoot, 11u);
        config.scaleOctave = std::clamp<int32_t>(config.scaleOctave, -1, 7);
        config.scale = std::min<uint32_t>(
            config.scale, s3g::kMusicalScaleCount - 1u);
        config.scaleRange = std::clamp<uint32_t>(config.scaleRange, 1u, 4u);
        config.latticeDepthIndex = std::min<uint32_t>(
            config.latticeDepthIndex, 2u);
        config.dealerLaw = static_cast<DealerLaw>(std::min<uint32_t>(
            static_cast<uint32_t>(config.dealerLaw), kDealerLawCount - 1u));
        config.midiInputMode = static_cast<MidiInputMode>(
            std::min<uint32_t>(static_cast<uint32_t>(config.midiInputMode),
                3u));
        config.midiInputChannel = std::min<uint32_t>(
            config.midiInputChannel, 16u);
        config.midiInputMap = static_cast<MidiInputMap>(
            std::min<uint32_t>(static_cast<uint32_t>(config.midiInputMap),
                3u));
        config.midiInputResponse = static_cast<MidiInputResponse>(
            std::min<uint32_t>(
                static_cast<uint32_t>(config.midiInputResponse), 1u));
        config.midiInputCc = std::min<uint32_t>(config.midiInputCc, 127u);
        config.midiInputPolarity = static_cast<MidiInputPolarity>(
            std::min<uint32_t>(
                static_cast<uint32_t>(config.midiInputPolarity), 1u));
        config.midiInputDepth = clampFinite(
            config.midiInputDepth, 0.0, 1.0, 0.65);
        config.midiInputDecayBeats = clampFinite(
            config.midiInputDecayBeats, 0.05, 16.0, 0.75);
        for (auto& relay : config.relays) {
            relay.channel = std::min<uint8_t>(relay.channel, 15u);
            relay.note = std::min<uint16_t>(relay.note, kMidiOff);
            relay.ccA = std::min<uint16_t>(relay.ccA, kMidiOff);
            relay.ccB = std::min<uint16_t>(relay.ccB, kMidiOff);
            relay.threshold = clampFinite(relay.threshold, 0.0, 1.0, 0.5);
            relay.bias = clampFinite(relay.bias, -1.0, 1.0, 0.0);
            relay.refractoryTicks = std::clamp<uint32_t>(
                relay.refractoryTicks, 1u, 32u);
            relay.feedback = clampFinite(relay.feedback, -1.0, 1.0, 0.0);
            relay.gateScale = clampFinite(relay.gateScale, 0.1, 4.0, 1.0);
            relay.topology = static_cast<ReceptorTopology>(std::min<uint32_t>(
                static_cast<uint32_t>(relay.topology), 3u));
            relay.pitchMode = static_cast<PitchMode>(std::min<uint32_t>(
                static_cast<uint32_t>(relay.pitchMode), 1u));
            relay.articulation = static_cast<ArticulationMode>(
                std::min<uint32_t>(
                    static_cast<uint32_t>(relay.articulation), 3u));
            for (auto& lane : relay.ccLanes) {
                lane.source = static_cast<CcSource>(std::min<uint32_t>(
                    static_cast<uint32_t>(lane.source), kCcSourceCount - 1u));
                lane.minimum = std::min<uint8_t>(lane.minimum, 127u);
                lane.maximum = std::min<uint8_t>(lane.maximum, 127u);
                lane.curve = clampFinite(lane.curve, -1.0, 1.0, 0.0);
                lane.slew = clampFinite(lane.slew, 0.0, 1.0, 0.0);
            }
        }
        return config;
    }

    static double tickBeat(int64_t tick, double period) noexcept
    {
        return static_cast<double>(tick) * period;
    }

    static int64_t firstTickAtOrAfter(double beat, double period) noexcept
    {
        return static_cast<int64_t>(std::ceil(beat / period - 1.0e-10));
    }

    double formBeat(double hostBeat) const noexcept
    {
        return formHold_ ? heldFormBeat_ : hostBeat - formBeatOffset_;
    }

    void updateFormHold(bool hold, double hostBeat,
        double requestedBeat) noexcept
    {
        if (hold == formHold_) return;
        if (hold) {
            heldFormBeat_ = std::isfinite(requestedBeat)
                ? requestedBeat : hostBeat - formBeatOffset_;
        } else {
            formBeatOffset_ = hostBeat - heldFormBeat_;
        }
        formHold_ = hold;
    }

    static bool noteInputEnabled(MidiInputMode mode) noexcept
    {
        return mode == MidiInputMode::Notes
            || mode == MidiInputMode::NotesAndControlChange;
    }

    static bool ccInputEnabled(MidiInputMode mode) noexcept
    {
        return mode == MidiInputMode::ControlChange
            || mode == MidiInputMode::NotesAndControlChange;
    }

    static bool inputChannelMatches(const Config& config,
        uint8_t channel) noexcept
    {
        return config.midiInputChannel == 0u
            || config.midiInputChannel == static_cast<uint32_t>(channel) + 1u;
    }

    static double inputAmplitude(const Config& config,
        uint8_t value) noexcept
    {
        const double unit = static_cast<double>(value) / 127.0;
        return config.midiInputPolarity == MidiInputPolarity::Signed
            ? unit * 2.0 - 1.0 : unit;
    }

    void addMappedInput(const Config& config, uint32_t address,
        double amplitude, std::array<double, kNodeCount>& destination) noexcept
    {
        if (std::abs(amplitude) <= 1.0e-12) return;
        switch (config.midiInputMap) {
        case MidiInputMap::NodeAddress:
            destination[address % kNodeCount] += amplitude;
            return;
        case MidiInputMap::ClusterAddress: {
            static constexpr std::array<double, kNodesPerCluster> weights {{
                1.00, 0.82, 0.66, 0.48, 0.35,
            }};
            const uint32_t cluster = address % kClusterCount;
            for (uint32_t role = 0u; role < kNodesPerCluster; ++role)
                destination[cluster * kNodesPerCluster + role]
                    += amplitude * weights[role];
            return;
        }
        case MidiInputMap::RelayAddress: {
            uint32_t relay = address % kRelayCount;
            for (uint32_t candidate = 0u; candidate < kRelayCount;
                 ++candidate) {
                if (config.relays[candidate].note == address) {
                    relay = candidate;
                    break;
                }
            }
            const uint32_t node = (relay % kClusterCount) * kNodesPerCluster
                + receptorPentadTap(relay);
            destination[node] += amplitude;
            return;
        }
        case MidiInputMap::Diffuse:
            for (uint32_t node = 0u; node < kNodeCount; ++node) {
                const double weight = 0.28 + 0.08
                    * static_cast<double>(node % kNodesPerCluster);
                destination[node] += amplitude * weight;
            }
            return;
        }
    }

    void applyExternalInputEvent(const Config& config,
        const ExternalMidiEvent& event) noexcept
    {
        const uint32_t key = static_cast<uint32_t>(event.channel) * 128u
            + event.data1;
        if (event.kind == EventKind::NoteOff
            || (event.kind == EventKind::NoteOn && event.data2 == 0u)) {
            heldInputNotes_[key] = 0u;
            return;
        }
        if (event.kind == EventKind::NoteOn) {
            heldInputNotes_[key] = event.data2;
            if (noteInputEnabled(config.midiInputMode)
                && config.midiInputResponse == MidiInputResponse::Impulse
                && inputChannelMatches(config, event.channel)) {
                addMappedInput(config, event.data1,
                    inputAmplitude(config, event.data2), externalImpulse_);
                for (double& value : externalImpulse_)
                    value = std::clamp(value, -2.0, 2.0);
            }
            return;
        }
        if (event.kind == EventKind::ControlChange) {
            inputCcValues_[key] = event.data2;
            inputCcSeen_[key] = 1u;
        }
    }

    bool queueExternalInput(const ExternalMidiEvent& source) noexcept
    {
        if (!std::isfinite(source.beat)
            || source.channel >= 16u || source.data1 >= 128u
            || source.data2 >= 128u
            || queuedInputCount_ >= kExternalInputQueueCapacity) return false;
        uint32_t position = queuedInputCount_;
        while (position > 0u
            && queuedInputs_[position - 1u].beat > source.beat) {
            queuedInputs_[position] = queuedInputs_[position - 1u];
            --position;
        }
        queuedInputs_[position] = source;
        ++queuedInputCount_;
        return true;
    }

    void applyExternalInputsThrough(const Config& config,
        double beat) noexcept
    {
        uint32_t due = 0u;
        while (due < queuedInputCount_
            && queuedInputs_[due].beat <= beat + 1.0e-10) {
            applyExternalInputEvent(config, queuedInputs_[due]);
            ++due;
        }
        if (due == 0u) return;
        for (uint32_t index = due; index < queuedInputCount_; ++index)
            queuedInputs_[index - due] = queuedInputs_[index];
        queuedInputCount_ -= due;
    }

    void prepareExternalInputDrive(const Config& config) noexcept
    {
        if (config.midiInputMode == MidiInputMode::Off) {
            externalInputDrive_.fill(0.0);
            return;
        }
        externalInputDrive_ = externalImpulse_;
        if (noteInputEnabled(config.midiInputMode)
            && config.midiInputResponse == MidiInputResponse::Gate) {
            for (uint32_t key = 0u; key < heldInputNotes_.size(); ++key) {
                const uint8_t velocity = heldInputNotes_[key];
                const uint8_t channel = static_cast<uint8_t>(key / 128u);
                if (velocity == 0u || !inputChannelMatches(config, channel))
                    continue;
                addMappedInput(config, key % 128u,
                    inputAmplitude(config, velocity) * 0.42,
                    externalInputDrive_);
            }
        }
        if (ccInputEnabled(config.midiInputMode)) {
            const uint32_t controller = config.midiInputCc;
            for (uint32_t channel = 0u; channel < 16u; ++channel) {
                if (!inputChannelMatches(config,
                        static_cast<uint8_t>(channel))) continue;
                const uint32_t key = channel * 128u + controller;
                if (inputCcSeen_[key] == 0u) continue;
                addMappedInput(config, controller,
                    inputAmplitude(config, inputCcValues_[key]) * 0.36,
                    externalInputDrive_);
            }
        }
        for (double& value : externalInputDrive_)
            value = std::clamp(value * config.midiInputDepth, -1.5, 1.5);
    }

    void decayExternalInput(const Config& config) noexcept
    {
        const double decay = std::exp(-clockPeriodBeats(
            config.clockRateIndex) / config.midiInputDecayBeats);
        for (double& value : externalImpulse_) {
            value *= decay;
            if (std::abs(value) < 1.0e-8) value = 0.0;
        }
    }

    void clearExternalInputState() noexcept
    {
        queuedInputCount_ = 0u;
        heldInputNotes_.fill(0u);
        inputCcValues_.fill(64u);
        inputCcSeen_.fill(0u);
        externalImpulse_.fill(0.0);
        externalInputDrive_.fill(0.0);
    }

    void initializeOrganism(const Config& config, int64_t cycle) noexcept
    {
        const uint64_t cycleBits = static_cast<uint64_t>(cycle);
        for (uint32_t node = 0u; node < kNodeCount; ++node) {
            nodes_[node] = signedHash(config.seed, cycleBits,
                100u + node) * 0.22;
        }
        updateClusters();
        receptors_.fill(0.0);
        plasticity_.fill(0.0);
        pendingImpulse_.fill(0.0);
        uint8_t pattern = static_cast<uint8_t>(
            mix64(static_cast<uint64_t>(config.seed) ^ cycleBits) & 0xffu);
        pattern ^= 0x96u;
        if (pattern == 0u || pattern == 0xffu) pattern = 0x96u;
        registerBits_ = pattern;
        lastFireTick_.fill(std::numeric_limits<int64_t>::min() / 4);
        const uint32_t planes = latticePlaneCount(config.latticeDepthIndex);
        const uint32_t initialPlane = static_cast<uint32_t>(
            mix64(static_cast<uint64_t>(config.seed) ^ cycleBits
                ^ 0x706c616e65ULL) % planes);
        currentCell_ = initialPlane * kClimateCellsPerPlane + 5u;
        previousCell_ = currentCell_;
        trail_.fill(currentCell_);
        trailCount_ = 1u;
        cellStep_ = 0;
        cycleIndex_ = cycle;
    }

    void initializeForPosition(const Config& config, double beat,
        double beatsPerBar, double tickPeriod) noexcept
    {
        const double climateBeat = formBeat(beat);
        const double cycleBeats = static_cast<double>(config.formBars)
            * beatsPerBar;
        const int64_t cycle = static_cast<int64_t>(std::floor(
            climateBeat / std::max(1.0, cycleBeats)));
        const double cycleStart = static_cast<double>(cycle) * cycleBeats;
        initializeOrganism(config, cycle);
        const double replayStartBeat = beat - (climateBeat - cycleStart);
        const int64_t start = firstTickAtOrAfter(replayStartBeat, tickPeriod);
        const int64_t target = firstTickAtOrAfter(beat, tickPeriod);
        // A restored Crystallize state has no live field history. Rebuild the
        // organism along the climate path up to the held instant, then resume
        // holding that instant. This avoids replaying the held card once for
        // every elapsed host tick.
        const bool restoreHold = formHold_;
        const double previousOffset = formBeatOffset_;
        if (restoreHold) {
            formHold_ = false;
            formBeatOffset_ = beat - climateBeat;
        }
        Result ignored;
        for (int64_t tick = start; tick < target; ++tick) {
            advanceTick(config, tick, tickBeat(tick, tickPeriod), beatsPerBar,
                false, nullptr, 0u, ignored);
        }
        if (restoreHold) {
            formBeatOffset_ = previousOffset;
            formHold_ = true;
        }
        nextTick_ = target;
        continuous_ = true;
        expectedBeat_ = beat;
        lastBeatPerBar_ = beatsPerBar;
        lastTickPeriod_ = tickPeriod;
    }

    static uint32_t wrappedCellNeighbor(uint32_t cell, uint32_t direction,
        uint32_t planes) noexcept
    {
        static constexpr std::array<int32_t, 8u> dx {{
            0, 1, 1, 1, 0, -1, -1, -1,
        }};
        static constexpr std::array<int32_t, 8u> dy {{
            -1, -1, 0, 1, 1, 1, 0, -1,
        }};
        planes = std::clamp<uint32_t>(planes, 1u, kClimateMaxPlanes);
        const uint32_t localCell = cell % kClimateCellsPerPlane;
        const int32_t x = static_cast<int32_t>(localCell % kClimateWidth);
        const int32_t y = static_cast<int32_t>(localCell / kClimateWidth);
        const uint32_t plane = (cell / kClimateCellsPerPlane) % planes;
        if (direction >= 8u) {
            const uint32_t nextPlane = direction == 8u
                ? (plane + 1u) % planes
                : (plane + planes - 1u) % planes;
            return nextPlane * kClimateCellsPerPlane + localCell;
        }
        const int32_t nextX = (x + dx[direction & 7u]
            + static_cast<int32_t>(kClimateWidth))
            % static_cast<int32_t>(kClimateWidth);
        const int32_t nextY = (y + dy[direction & 7u]
            + static_cast<int32_t>(kClimateWidth))
            % static_cast<int32_t>(kClimateWidth);
        return plane * kClimateCellsPerPlane
            + static_cast<uint32_t>(nextY) * kClimateWidth
            + static_cast<uint32_t>(nextX);
    }

    double cellTrait(const Config& config, uint32_t cell,
        uint32_t trait) const noexcept
    {
        return climateCellTrait(config.seed, cycleIndex_, cell, trait);
    }

    uint32_t dealerDirection(const Config& config, int64_t tick,
        uint32_t motion, uint32_t planes) const noexcept
    {
        switch (config.dealerLaw) {
        case DealerLaw::EcologicalDrift: {
            const bool changePlane = planes > 1u
                && ((motion >> 3u) + static_cast<uint32_t>(cellStep_)) % 5u
                    == 0u;
            return changePlane ? 8u + ((motion >> 7u) & 1u)
                               : motion & 7u;
        }
        case DealerLaw::CompassRose:
            if (planes > 1u && (cellStep_ + 1) % 16 == 0)
                return 8u + (static_cast<uint32_t>(cycleIndex_) & 1u);
            return static_cast<uint32_t>(cellStep_) & 7u;
        case DealerLaw::SeededWander: {
            const uint64_t random = mix64(static_cast<uint64_t>(config.seed)
                ^ static_cast<uint64_t>(cycleIndex_)
                ^ static_cast<uint64_t>(cellStep_ + 1) * 0x9e3779b9u);
            if (planes > 1u && ((random >> 11u) % 6u) == 0u)
                return 8u + static_cast<uint32_t>((random >> 17u) & 1u);
            return static_cast<uint32_t>(random & 7u);
        }
        case DealerLaw::PlaneTides:
            if (planes > 1u && (cellStep_ + 1) % 4 == 0)
                return 8u + (static_cast<uint32_t>((cellStep_ + 1) / 4)
                    & 1u);
            return (motion + static_cast<uint32_t>(cellStep_) * 3u) & 7u;
        case DealerLaw::AvoidRecent:
        case DealerLaw::ClimateContrast:
            break;
        }

        const uint32_t candidates = planes > 1u ? 10u : 8u;
        uint32_t bestDirection = 0u;
        double bestScore = -std::numeric_limits<double>::infinity();
        for (uint32_t direction = 0u; direction < candidates; ++direction) {
            const uint32_t candidate = wrappedCellNeighbor(
                currentCell_, direction, planes);
            double score = unitHash(config.seed,
                static_cast<uint64_t>(cycleIndex_),
                static_cast<uint64_t>(cellStep_ + 1) * 13u + direction)
                * 0.01;
            if (config.dealerLaw == DealerLaw::AvoidRecent) {
                uint32_t visits = 0u;
                const uint32_t inspect = std::min<uint32_t>(trailCount_, 8u);
                for (uint32_t offset = 0u; offset < inspect; ++offset) {
                    const uint32_t index = trailCount_ - 1u - offset;
                    if (trail_[index] == candidate) ++visits;
                }
                score -= static_cast<double>(visits) * 2.0;
            } else {
                for (uint32_t trait = 0u; trait < 4u; ++trait)
                    score += std::abs(cellTrait(config, candidate, trait)
                        - cellTrait(config, currentCell_, trait));
            }
            if (score > bestScore) {
                bestScore = score;
                bestDirection = direction;
            }
        }
        (void)tick;
        return bestDirection;
    }

    void updateClimateCell(const Config& config, int64_t tick,
        double beat, double beatsPerBar) noexcept
    {
        const double cycleBeats = static_cast<double>(config.formBars)
            * beatsPerBar;
        const int64_t cycle = static_cast<int64_t>(std::floor(
            beat / std::max(1.0, cycleBeats)));
        if (cycle != cycleIndex_) initializeOrganism(config, cycle);
        const double cycleStart = static_cast<double>(cycle) * cycleBeats;
        const double dwellBeats = static_cast<double>(config.dwellBars)
            * beatsPerBar;
        const int64_t step = static_cast<int64_t>(std::floor(
            std::max(0.0, beat - cycleStart) / std::max(1.0, dwellBeats)));
        while (cellStep_ < step) {
            const uint32_t clusterChoice = static_cast<uint32_t>(
                (registerBits_ >> ((cellStep_ + 1) & 3)) & 3u);
            const uint32_t energyCode = static_cast<uint32_t>(std::floor(
                std::abs(clusters_[clusterChoice]) * 29.0));
            const uint32_t motion = static_cast<uint32_t>(
                registerBits_ + energyCode
                + static_cast<uint32_t>(mix64(static_cast<uint64_t>(tick)
                    ^ static_cast<uint64_t>(config.seed))));
            const uint32_t planes = latticePlaneCount(
                config.latticeDepthIndex);
            const uint32_t direction = dealerDirection(
                config, tick, motion, planes);
            previousCell_ = currentCell_;
            currentCell_ = wrappedCellNeighbor(
                currentCell_, direction, planes);
            if (trailCount_ < kTrailLength) {
                trail_[trailCount_++] = currentCell_;
            } else {
                for (uint32_t index = 1u; index < kTrailLength; ++index)
                    trail_[index - 1u] = trail_[index];
                trail_[kTrailLength - 1u] = currentCell_;
            }
            ++cellStep_;
        }
    }

    double climateBlend(const Config& config, double beat,
        double beatsPerBar) const noexcept
    {
        if (previousCell_ == currentCell_) return 1.0;
        const double cycleBeats = static_cast<double>(config.formBars)
            * beatsPerBar;
        const double cycleStart = static_cast<double>(cycleIndex_) * cycleBeats;
        const double dwellBeats = static_cast<double>(config.dwellBars)
            * beatsPerBar;
        const double boundary = cycleStart
            + static_cast<double>(cellStep_) * dwellBeats;
        const double transition = config.transitionBars * beatsPerBar;
        if (transition <= 1.0e-9) return 1.0;
        const double linear = std::clamp((beat - boundary) / transition,
            0.0, 1.0);
        return linear * linear * (3.0 - 2.0 * linear);
    }

    double blendedTrait(const Config& config, uint32_t trait,
        double blend) const noexcept
    {
        const double a = cellTrait(config, previousCell_, trait);
        const double b = cellTrait(config, currentCell_, trait);
        return a + (b - a) * blend;
    }

    double effectiveTrait(const Config& config, uint32_t trait,
        double blend) const noexcept
    {
        return blendedTrait(config, trait, blend) * config.climate;
    }

    double formPhase(const Config& config, double beat,
        double beatsPerBar) const noexcept
    {
        const double cycleBeats = static_cast<double>(config.formBars)
            * beatsPerBar;
        double phase = beat / std::max(1.0, cycleBeats);
        phase -= std::floor(phase);
        return phase;
    }

    double ccSourceValue(const Config& config, uint32_t relay,
        CcSource source, double population, double beat,
        double beatsPerBar) const noexcept
    {
        const uint32_t cluster = relay % kClusterCount;
        const double blend = climateBlend(
            config, formBeat(beat), beatsPerBar);
        switch (source) {
        case CcSource::Receptor:
            return receptors_[relay] * 0.5 + 0.5;
        case CcSource::ClusterSigned:
            return clusters_[cluster] * 0.5 + 0.5;
        case CcSource::ClusterEnergy:
            return std::abs(clusters_[cluster]);
        case CcSource::RegisterPopulation:
            return population;
        case CcSource::EnergyRegister:
            return std::clamp(
                std::abs(clusters_[cluster]) * 0.72 + population * 0.28,
                0.0, 1.0);
        case CcSource::ClimateEnergy:
        case CcSource::ClimateCoupling:
        case CcSource::ClimateHierarchy:
        case CcSource::ClimateContrast: {
            const uint32_t trait = static_cast<uint32_t>(source)
                - static_cast<uint32_t>(CcSource::ClimateEnergy);
            return blendedTrait(config, trait, blend) * 0.5 + 0.5;
        }
        case CcSource::Gestation:
            return blend;
        case CcSource::FormPhase:
            return formPhase(config, formBeat(beat), beatsPerBar);
        case CcSource::PlasticityDrift: {
            double drift = 0.0;
            for (double value : plasticity_) drift += std::abs(value);
            return std::clamp(drift
                / (0.22 * static_cast<double>(plasticity_.size())),
                0.0, 1.0);
        }
        }
        return 0.0;
    }

    static double applyCcCurve(double value, double curve) noexcept
    {
        value = std::clamp(value, 0.0, 1.0);
        if (curve > 1.0e-9)
            return std::pow(value, 1.0 + curve * 3.0);
        if (curve < -1.0e-9)
            return 1.0 - std::pow(1.0 - value, 1.0 - curve * 3.0);
        return value;
    }

    double receptorValue(const RelayConfig& relay, uint32_t index,
        double beat, const Config& config) const noexcept
    {
        const uint32_t local = index % kClusterCount;
        double value = 0.0;
        switch (relay.topology) {
        case ReceptorTopology::Local:
            value = clusters_[local] * 0.70
                + nodes_[local * kNodesPerCluster
                    + receptorPentadTap(index)] * 0.30;
            break;
        case ReceptorTopology::Cross: {
            const uint32_t opposite = (local + 2u) % kClusterCount;
            value = clusters_[opposite] * 0.82 - clusters_[local] * 0.18;
            break;
        }
        case ReceptorTopology::Diffuse:
            value = clusters_[0] * 0.32 - clusters_[1] * 0.24
                + clusters_[2] * 0.27 - clusters_[3] * 0.17;
            break;
        case ReceptorTopology::Roaming: {
            const double phase = beat / std::max(1.0,
                static_cast<double>(config.dwellBars) * 4.0)
                + static_cast<double>(index) * 0.173;
            const double wrapped = phase - std::floor(phase);
            const double position = wrapped
                * static_cast<double>(kClusterCount);
            const uint32_t a = static_cast<uint32_t>(position)
                % kClusterCount;
            const uint32_t b = (a + 1u) % kClusterCount;
            value = clusters_[a] + (clusters_[b] - clusters_[a])
                * (position - std::floor(position));
            break;
        }
        }
        return std::clamp(value + relay.bias * 0.42, -1.0, 1.0);
    }

    void updateClusters() noexcept
    {
        for (uint32_t cluster = 0u; cluster < kClusterCount; ++cluster) {
            double sum = 0.0;
            for (uint32_t local = 0u; local < kNodesPerCluster; ++local)
                sum += nodes_[cluster * kNodesPerCluster + local];
            clusters_[cluster] = sum / static_cast<double>(kNodesPerCluster);
        }
    }

    void advanceNetwork(const Config& config, int64_t tick,
        double beat, double beatsPerBar) noexcept
    {
        const double climateBeat = formBeat(beat);
        updateClimateCell(config, tick, climateBeat, beatsPerBar);
        const double blend = climateBlend(config, climateBeat, beatsPerBar);
        const double activityTrait = effectiveTrait(config, 0u, blend);
        const double couplingTrait = effectiveTrait(config, 1u, blend);
        const double hierarchyTrait = effectiveTrait(config, 2u, blend);
        const double contrastTrait = effectiveTrait(config, 3u, blend);
        const double effectiveActivity = std::clamp(
            config.activity + activityTrait * 0.24, 0.0, 1.0);
        const double effectiveCoupling = std::clamp(
            config.coupling + couplingTrait * 0.22, 0.0, 1.2);
        const double effectiveHierarchy = std::clamp(
            config.hierarchy + hierarchyTrait * 0.20, 0.0, 1.0);
        const double effectiveContrast = std::clamp(
            config.contrast + contrastTrait * 0.22, 0.0, 1.0);

        static constexpr std::array<double, kClusterCount> alpha {{
            0.075, 0.16, 0.34, 0.61,
        }};
        const auto previousNodes = nodes_;
        const auto previousClusters = clusters_;
        const double gain = 1.25 + effectiveActivity * 2.25;
        const double activityBias = (effectiveActivity - 0.5) * 0.58;
        for (uint32_t cluster = 0u; cluster < kClusterCount; ++cluster) {
            double cross = 0.0;
            for (uint32_t source = 0u; source < kClusterCount; ++source) {
                const uint32_t matrixIndex = cluster * kClusterCount + source;
                cross += previousClusters[source]
                    * (kInterClusterWeights[matrixIndex]
                        + plasticity_[matrixIndex]);
            }
            const double parent = cluster == 0u ? 0.0
                : previousClusters[cluster - 1u];
            for (uint32_t local = 0u; local < kNodesPerCluster; ++local) {
                const uint32_t node = cluster * kNodesPerCluster + local;
                const uint32_t previous = cluster * kNodesPerCluster
                    + ((local + kNodesPerCluster - 1u) % kNodesPerCluster);
                const uint32_t next = cluster * kNodesPerCluster
                    + ((local + 1u) % kNodesPerCluster);
                const double ring = previousNodes[previous]
                        * kRingForwardWeights[local]
                    + previousNodes[next] * kRingReverseWeights[local];
                const bool bit = ((registerBits_ >> (node % kRelayCount)) & 1u)
                    != 0u;
                const double registerDrive = bit ? 0.115 : -0.065;
                const double noise = config.freeze ? 0.0
                    : signedHash(config.seed,
                        static_cast<uint64_t>(tick), 1000u + node)
                        * config.mutation * 0.115;
                const double impulse = pendingImpulse_[cluster]
                    * kPentadImpulseResponse[local];
                const double target = std::tanh((kPentadBias[local]
                    + activityBias
                    + ring * (0.50 + effectiveContrast * 0.32)
                    + cross * effectiveCoupling * 0.62
                    + parent * effectiveHierarchy * (cluster == 0u ? 0.0 : 0.48)
                    + registerDrive + impulse + noise
                    + externalInputDrive_[node] * 0.62) * gain);
                const double response = std::min(
                    1.0, alpha[cluster] * kPentadResponse[local]);
                nodes_[node] += (target - nodes_[node]) * response;
                nodes_[node] = std::clamp(nodes_[node], -1.0, 1.0);
            }
        }
        pendingImpulse_.fill(0.0);
        updateClusters();

        if (!config.freeze && config.mutation > 0.0) {
            const double rate = 0.0012 * config.mutation;
            for (uint32_t destination = 0u; destination < kClusterCount;
                 ++destination) {
                for (uint32_t source = 0u; source < kClusterCount; ++source) {
                    const uint32_t index = destination * kClusterCount + source;
                    const double correlation = previousClusters[source]
                        * clusters_[destination];
                    plasticity_[index] += correlation * rate;
                    plasticity_[index] *= 0.9997;
                    plasticity_[index] = std::clamp(
                        plasticity_[index], -0.22, 0.22);
                }
            }
        }
    }

    void advanceRegister(const Config& config, int64_t tick,
        double beat) noexcept
    {
        for (uint32_t relay = 0u; relay < kRelayCount; ++relay)
            receptors_[relay] = receptorValue(
                config.relays[relay], relay, beat, config);

        const uint32_t source = static_cast<uint32_t>(tick) & 7u;
        const RelayConfig& sourceRelay = config.relays[source];
        const double threshold = sourceRelay.threshold * 1.4 - 0.7;
        const bool comparator = receptors_[source] > threshold;
        const bool left = ((registerBits_ >> ((source + 7u) & 7u)) & 1u) != 0u;
        const bool right = ((registerBits_ >> ((source + 1u) & 7u)) & 1u) != 0u;
        const bool cellular = left != right;
        const bool sensed = comparator != (cellular && config.contrast > 0.58);
        const bool recirculated = (registerBits_ & 0x80u) != 0u;
        const double freshProbability = config.freeze ? 0.0 : 1.0 - config.memory;
        bool incoming = unitHash(config.seed,
            static_cast<uint64_t>(cycleIndex_),
            static_cast<uint64_t>(tick) * 5u + 1u) < freshProbability
            ? sensed : recirculated;
        const double flipProbability = config.freeze ? 0.0
            : config.mutation * (1.0 - config.memory) * 0.09;
        if (unitHash(config.seed, static_cast<uint64_t>(tick), 1601u)
            < flipProbability) incoming = !incoming;
        const uint32_t shifted = static_cast<uint32_t>(registerBits_) << 1u;
        registerBits_ = static_cast<uint8_t>(shifted
            | static_cast<uint32_t>(incoming ? 1u : 0u));
    }

    void advanceTick(const Config& config, int64_t tick, double beat,
        double beatsPerBar, bool emit, Event* events, uint32_t capacity,
        Result& result) noexcept
    {
        prepareExternalInputDrive(config);
        // Establish a cycle reset before taking the register edge snapshot so
        // uninterrupted playback and a seek to the same cycle boundary agree.
        updateClimateCell(config, tick, formBeat(beat), beatsPerBar);
        const uint8_t oldBits = registerBits_;
        advanceNetwork(config, tick, beat, beatsPerBar);
        advanceRegister(config, tick, beat);
        decayExternalInput(config);
        const uint8_t rising = static_cast<uint8_t>(
            registerBits_ & static_cast<uint8_t>(~oldBits));

        for (uint32_t relay = 0u; relay < kRelayCount; ++relay) {
            const RelayConfig& mapping = config.relays[relay];
            if (!mapping.enabled || (rising & (1u << relay)) == 0u) continue;
            if (tick - lastFireTick_[relay]
                < static_cast<int64_t>(mapping.refractoryTicks)) continue;
            lastFireTick_[relay] = tick;
            pendingImpulse_[relay % kClusterCount] = std::clamp(
                pendingImpulse_[relay % kClusterCount] + mapping.feedback,
                -1.5, 1.5);
            if (!emit) continue;
            if (mapping.note < kMidiOff) {
                const uint8_t note = mapping.pitchMode == PitchMode::ScaleLogic
                    ? logicScaleNote(config, relay, registerBits_,
                        receptors_[relay], currentCell_)
                    : static_cast<uint8_t>(mapping.note);
                const double velocitySource = std::clamp(
                    0.58 * (receptors_[relay] * 0.5 + 0.5)
                        + 0.42 * std::abs(clusters_[relay % kClusterCount]),
                    0.0, 1.0);
                const uint8_t velocity = static_cast<uint8_t>(std::clamp(
                    static_cast<int>(std::lround(18.0 + velocitySource * 109.0)),
                    1, 127));
                articulateNote(relay, mapping.articulation, mapping.channel,
                    note, velocity, beat,
                    beat + config.gateBeats * mapping.gateScale,
                    events, capacity, result);
            }
        }

        if (!emit || (static_cast<uint64_t>(tick)
            % ccEveryTicks(config.ccRateIndex)) != 0u) return;
        uint32_t activeBits = 0u;
        for (uint32_t bit = 0u; bit < kRelayCount; ++bit)
            activeBits += (registerBits_ >> bit) & 1u;
        const double population = static_cast<double>(activeBits)
            / static_cast<double>(kRelayCount);
        for (uint32_t relay = 0u; relay < kRelayCount; ++relay) {
            const RelayConfig& mapping = config.relays[relay];
            if (!mapping.enabled) continue;
            const std::array<uint16_t, 2u> controllers {{
                mapping.ccA, mapping.ccB,
            }};
            for (uint32_t lane = 0u; lane < 2u; ++lane) {
                const auto& laneConfig = mapping.ccLanes[lane];
                const double shaped = applyCcCurve(ccSourceValue(config,
                    relay, laneConfig.source, population, beat, beatsPerBar),
                    laneConfig.curve);
                double& smoothed = smoothedCc_[relay][lane];
                if (!std::isfinite(smoothed)) smoothed = shaped;
                const double alpha = 1.0 - laneConfig.slew * 0.98;
                smoothed += (shaped - smoothed) * alpha;
                const double ranged = static_cast<double>(laneConfig.minimum)
                    + smoothed * (static_cast<double>(laneConfig.maximum)
                        - static_cast<double>(laneConfig.minimum));
                const int value = std::clamp(
                    static_cast<int>(std::lround(ranged)), 0, 127);
                emitCc(relay, lane, controllers[lane], value, beat,
                    mapping.channel, events, capacity, result);
            }
        }
    }

    void emitCc(uint32_t relay, uint32_t lane, uint16_t controller,
        int value, double beat, uint8_t channel, Event* events,
        uint32_t capacity, Result& result) noexcept
    {
        if (controller >= kMidiOff || lastCc_[relay][lane] == value) return;
        lastCc_[relay][lane] = value;
        pushEvent({ beat, EventKind::ControlChange, channel,
            static_cast<uint8_t>(controller), static_cast<uint8_t>(value),
            static_cast<uint8_t>(relay) }, events, capacity, result);
    }

    static void pushEvent(const Event& event, Event* events,
        uint32_t capacity, Result& result) noexcept
    {
        if (events && result.count < capacity) events[result.count++] = event;
        else ++result.dropped;
    }

    static constexpr uint32_t noteKey(uint8_t channel, uint8_t note) noexcept
    {
        return static_cast<uint32_t>(channel) * 128u
            + static_cast<uint32_t>(note);
    }

    bool relayHasActiveNote(uint32_t relay) const noexcept
    {
        for (const auto& note : activeNotes_[relay])
            if (note.active) return true;
        return false;
    }

    uint32_t matchingVoice(uint32_t relay, uint8_t channel,
        uint8_t note) const noexcept
    {
        for (uint32_t voice = 0u; voice < kVoicesPerRelay; ++voice) {
            const auto& active = activeNotes_[relay][voice];
            if (active.active && active.channel == channel
                && active.note == note) return voice;
        }
        return kVoicesPerRelay;
    }

    uint32_t freeVoice(uint32_t relay) const noexcept
    {
        for (uint32_t voice = 0u; voice < kVoicesPerRelay; ++voice)
            if (!activeNotes_[relay][voice].active) return voice;
        return kVoicesPerRelay;
    }

    uint32_t oldestVoice(uint32_t relay) const noexcept
    {
        uint32_t oldest = 0u;
        for (uint32_t voice = 1u; voice < kVoicesPerRelay; ++voice) {
            if (activeNotes_[relay][voice].startBeat
                < activeNotes_[relay][oldest].startBeat) oldest = voice;
        }
        return oldest;
    }

    void startVoice(uint32_t relay, uint32_t voice, uint8_t channel,
        uint8_t note, uint8_t velocity, double beat, double offBeat,
        Event* events, uint32_t capacity, Result& result) noexcept
    {
        const uint32_t key = noteKey(channel, note);
        if (noteOwners_[key] == 0u) {
            pushEvent({ beat, EventKind::NoteOn, channel, note, velocity,
                static_cast<uint8_t>(relay) }, events, capacity, result);
        }
        if (noteOwners_[key] < std::numeric_limits<uint8_t>::max())
            ++noteOwners_[key];
        activeNotes_[relay][voice] = {
            true, channel, note, beat, offBeat,
        };
    }

    void releaseVoice(uint32_t relay, uint32_t voice, double beat,
        Event* events,
        uint32_t capacity, Result& result) noexcept
    {
        ActiveNote& note = activeNotes_[relay][voice];
        if (!note.active) return;
        const uint32_t key = noteKey(note.channel, note.note);
        if (noteOwners_[key] > 0u) --noteOwners_[key];
        if (noteOwners_[key] == 0u) {
            pushEvent({ beat, EventKind::NoteOff,
                note.channel, note.note, 0u, static_cast<uint8_t>(relay) },
                events, capacity, result);
        }
        note = {};
    }

    void releaseRelay(uint32_t relay, double beat, Event* events,
        uint32_t capacity, Result& result) noexcept
    {
        for (uint32_t voice = 0u; voice < kVoicesPerRelay; ++voice)
            releaseVoice(relay, voice, beat, events, capacity, result);
    }

    void articulateNote(uint32_t relay, ArticulationMode articulation,
        uint8_t channel, uint8_t note, uint8_t velocity, double beat,
        double offBeat, Event* events, uint32_t capacity,
        Result& result) noexcept
    {
        switch (articulation) {
        case ArticulationMode::Restart:
            releaseRelay(relay, beat, events, capacity, result);
            startVoice(relay, 0u, channel, note, velocity, beat, offBeat,
                events, capacity, result);
            return;
        case ArticulationMode::Hold:
            if (relayHasActiveNote(relay)) return;
            startVoice(relay, 0u, channel, note, velocity, beat, offBeat,
                events, capacity, result);
            return;
        case ArticulationMode::Extend: {
            const uint32_t matching = matchingVoice(relay, channel, note);
            if (matching < kVoicesPerRelay) {
                activeNotes_[relay][matching].offBeat = std::max(
                    activeNotes_[relay][matching].offBeat, offBeat);
                return;
            }
            releaseRelay(relay, beat, events, capacity, result);
            startVoice(relay, 0u, channel, note, velocity, beat, offBeat,
                events, capacity, result);
            return;
        }
        case ArticulationMode::Stack: {
            uint32_t voice = freeVoice(relay);
            if (voice >= kVoicesPerRelay) {
                voice = oldestVoice(relay);
                releaseVoice(relay, voice, beat, events, capacity, result);
            }
            startVoice(relay, voice, channel, note, velocity, beat, offBeat,
                events, capacity, result);
            return;
        }
        }
    }

    void releaseDue(double beat, bool inclusive, Event* events,
        uint32_t capacity, Result& result) noexcept
    {
        for (;;) {
            uint32_t earliestRelay = kRelayCount;
            uint32_t earliestVoice = kVoicesPerRelay;
            double earliestBeat = std::numeric_limits<double>::infinity();
            for (uint32_t relay = 0u; relay < kRelayCount; ++relay) {
                for (uint32_t voice = 0u; voice < kVoicesPerRelay; ++voice) {
                    const auto& note = activeNotes_[relay][voice];
                    if (!note.active) continue;
                    const bool due = inclusive
                        ? note.offBeat <= beat + 1.0e-10
                        : note.offBeat < beat - 1.0e-10;
                    if (due && note.offBeat < earliestBeat) {
                        earliestBeat = note.offBeat;
                        earliestRelay = relay;
                        earliestVoice = voice;
                    }
                }
            }
            if (earliestRelay >= kRelayCount) break;
            releaseVoice(earliestRelay, earliestVoice, earliestBeat,
                events, capacity, result);
        }
    }

    void releaseAll(double beat, Event* events, uint32_t capacity,
        Result& result) noexcept
    {
        for (uint32_t relay = 0u; relay < kRelayCount; ++relay)
            releaseRelay(relay, beat, events, capacity, result);
    }

    void releaseUnavailable(const Config& config, double beat, Event* events,
        uint32_t capacity, Result& result) noexcept
    {
        for (uint32_t relay = 0u; relay < kRelayCount; ++relay) {
            const auto& mapping = config.relays[relay];
            for (uint32_t voice = 0u; voice < kVoicesPerRelay; ++voice) {
                const auto& note = activeNotes_[relay][voice];
                if (note.active && (!mapping.enabled
                    || mapping.note >= kMidiOff
                    || mapping.channel != note.channel
                    || (mapping.pitchMode == PitchMode::Fixed
                        && mapping.note != note.note))) {
                    releaseVoice(relay, voice, beat,
                        events, capacity, result);
                }
            }
        }
    }

    void invalidateCc() noexcept
    {
        for (auto& cc : lastCc_) cc = {{ -1, -1 }};
        for (auto& cc : smoothedCc_)
            cc = {{ std::numeric_limits<double>::quiet_NaN(),
                    std::numeric_limits<double>::quiet_NaN() }};
    }

    void updateSnapshot(const Config& config, double beat,
        double beatsPerBar) noexcept
    {
        const double climateBeat = formBeat(beat);
        snapshot_.nodes = nodes_;
        snapshot_.clusters = clusters_;
        snapshot_.receptors = receptors_;
        snapshot_.plasticity = plasticity_;
        snapshot_.trail = trail_;
        snapshot_.registerBits = registerBits_;
        snapshot_.currentCell = currentCell_;
        snapshot_.previousCell = previousCell_;
        snapshot_.trailCount = trailCount_;
        snapshot_.climateBlend = climateBlend(
            config, climateBeat, beatsPerBar);
        const double cycleBeats = static_cast<double>(config.formBars)
            * beatsPerBar;
        double phase = climateBeat / std::max(1.0, cycleBeats);
        phase -= std::floor(phase);
        snapshot_.cyclePhase = phase;
        snapshot_.cycleIndex = cycleIndex_;
        snapshot_.formBeat = climateBeat;
        snapshot_.formHold = formHold_;
        for (uint32_t trait = 0u; trait < 4u; ++trait)
            snapshot_.climateTraits[trait] = blendedTrait(
                config, trait, snapshot_.climateBlend);
        snapshot_.effectiveConduct = {{
            std::clamp(config.activity
                + snapshot_.climateTraits[0u] * config.climate * 0.24,
                0.0, 1.0),
            std::clamp(config.coupling
                + snapshot_.climateTraits[1u] * config.climate * 0.22,
                0.0, 1.2),
            std::clamp(config.hierarchy
                + snapshot_.climateTraits[2u] * config.climate * 0.20,
                0.0, 1.0),
            std::clamp(config.contrast
                + snapshot_.climateTraits[3u] * config.climate * 0.22,
                0.0, 1.0),
        }};
        snapshot_.externalInputNodes = externalInputDrive_;
        double inputActivity = 0.0;
        for (double value : externalInputDrive_)
            inputActivity += std::abs(value);
        snapshot_.externalInputActivity = inputActivity
            / static_cast<double>(kNodeCount);
        double energy = 0.0;
        for (double cluster : clusters_) energy += std::abs(cluster);
        snapshot_.energy = energy / static_cast<double>(kClusterCount);
    }

    std::array<double, kNodeCount> nodes_ {};
    std::array<double, kClusterCount> clusters_ {};
    std::array<double, kRelayCount> receptors_ {};
    std::array<double, kClusterCount * kClusterCount> plasticity_ {};
    std::array<double, kClusterCount> pendingImpulse_ {};
    std::array<ExternalMidiEvent, kExternalInputQueueCapacity> queuedInputs_ {};
    uint32_t queuedInputCount_ = 0u;
    std::array<uint8_t, 16u * 128u> heldInputNotes_ {};
    std::array<uint8_t, 16u * 128u> inputCcValues_ {};
    std::array<uint8_t, 16u * 128u> inputCcSeen_ {};
    std::array<double, kNodeCount> externalImpulse_ {};
    std::array<double, kNodeCount> externalInputDrive_ {};
    std::array<std::array<ActiveNote, kVoicesPerRelay>, kRelayCount>
        activeNotes_ {};
    std::array<uint8_t, 16u * 128u> noteOwners_ {};
    std::array<std::array<int, 2u>, kRelayCount> lastCc_ {};
    std::array<std::array<double, 2u>, kRelayCount> smoothedCc_ {};
    std::array<int64_t, kRelayCount> lastFireTick_ {};
    std::array<uint32_t, kTrailLength> trail_ {};
    uint8_t registerBits_ = 0x96u;
    uint32_t currentCell_ = 5u;
    uint32_t previousCell_ = 5u;
    uint32_t trailCount_ = 1u;
    int64_t cycleIndex_ = std::numeric_limits<int64_t>::min();
    int64_t cellStep_ = 0;
    int64_t nextTick_ = 0;
    double expectedBeat_ = 0.0;
    double lastBeatPerBar_ = 4.0;
    double lastTickPeriod_ = 0.25;
    double formBeatOffset_ = 0.0;
    double heldFormBeat_ = 0.0;
    bool formHold_ = false;
    bool continuous_ = false;
    Snapshot snapshot_ {};
};

} // namespace s3g::relay
