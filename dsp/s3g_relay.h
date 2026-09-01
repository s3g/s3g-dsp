#pragma once

#include "s3g_musical_scales.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g::relay {

constexpr uint32_t kNodeCount = 16u;
constexpr uint32_t kClusterCount = 4u;
constexpr uint32_t kNodesPerCluster = 4u;
constexpr uint32_t kRelayCount = 8u;
constexpr uint32_t kVoicesPerRelay = 4u;
constexpr uint32_t kClimateCells = 16u;
constexpr uint32_t kClimateWidth = 4u;
constexpr uint32_t kTrailLength = 16u;
constexpr uint16_t kMidiOff = 128u;

constexpr double kPi = 3.1415926535897932384626433832795;

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

struct Event {
    double beat = 0.0;
    EventKind kind = EventKind::ControlChange;
    uint8_t channel = 0u;
    uint8_t data1 = 0u;
    uint8_t data2 = 0u;
    uint8_t relay = 0u;
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
            relay.threshold = 0.42 + 0.045 * static_cast<double>(index % 4u);
            relay.bias = biases[index];
            relay.refractoryTicks = refractory[index];
            relay.feedback = feedbacks[index];
            relay.gateScale = 0.72 + 0.14 * static_cast<double>(index % 4u);
            relay.topology = static_cast<ReceptorTopology>(index % 4u);
        }
    }
};

struct Snapshot {
    std::array<double, kNodeCount> nodes {};
    std::array<double, kClusterCount> clusters {};
    std::array<double, kRelayCount> receptors {};
    std::array<uint32_t, kTrailLength> trail {};
    uint8_t registerBits = 0u;
    uint32_t currentCell = 5u;
    uint32_t previousCell = 5u;
    uint32_t trailCount = 1u;
    double climateBlend = 1.0;
    double cyclePhase = 0.0;
    double energy = 0.0;
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
        for (auto& relay : activeNotes_)
            for (auto& note : relay) note = {};
        noteOwners_.fill(0u);
        for (auto& cc : lastCc_) cc = {{ -1, -1 }};
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
        continuous_ = false;
        snapshot_ = {};
    }

    void invalidate() noexcept { continuous_ = false; }

    const Snapshot& snapshot() const noexcept { return snapshot_; }

    Result process(double beginBeat, double endBeat, double beatsPerBar,
        bool playing, const Config& sourceConfig, Event* events,
        uint32_t capacity) noexcept
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

        if (!playing || !config.enabled) {
            releaseAll(beginBeat, events, capacity, result);
            invalidateCc();
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
            initializeForPosition(config, beginBeat, bar, tickPeriod);
            invalidateCc();
        }

        releaseDue(beginBeat, true, events, capacity, result);
        constexpr double epsilon = 1.0e-10;
        while (tickBeat(nextTick_, tickPeriod) < endBeat - epsilon) {
            const double beat = tickBeat(nextTick_, tickPeriod);
            if (beat >= beginBeat - epsilon) {
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
        currentCell_ = 5u;
        previousCell_ = 5u;
        trail_.fill(5u);
        trailCount_ = 1u;
        cellStep_ = 0;
        cycleIndex_ = cycle;
    }

    void initializeForPosition(const Config& config, double beat,
        double beatsPerBar, double tickPeriod) noexcept
    {
        const double cycleBeats = static_cast<double>(config.formBars)
            * beatsPerBar;
        const int64_t cycle = static_cast<int64_t>(std::floor(
            beat / std::max(1.0, cycleBeats)));
        const double cycleStart = static_cast<double>(cycle) * cycleBeats;
        initializeOrganism(config, cycle);
        const int64_t start = firstTickAtOrAfter(cycleStart, tickPeriod);
        const int64_t target = firstTickAtOrAfter(beat, tickPeriod);
        Result ignored;
        for (int64_t tick = start; tick < target; ++tick) {
            advanceTick(config, tick, tickBeat(tick, tickPeriod), beatsPerBar,
                false, nullptr, 0u, ignored);
        }
        nextTick_ = target;
        continuous_ = true;
        expectedBeat_ = beat;
        lastBeatPerBar_ = beatsPerBar;
        lastTickPeriod_ = tickPeriod;
    }

    static uint32_t wrappedCellNeighbor(uint32_t cell, uint32_t direction) noexcept
    {
        static constexpr std::array<int32_t, 8u> dx {{
            0, 1, 1, 1, 0, -1, -1, -1,
        }};
        static constexpr std::array<int32_t, 8u> dy {{
            -1, -1, 0, 1, 1, 1, 0, -1,
        }};
        const int32_t x = static_cast<int32_t>(cell % kClimateWidth);
        const int32_t y = static_cast<int32_t>(cell / kClimateWidth);
        const int32_t nextX = (x + dx[direction & 7u]
            + static_cast<int32_t>(kClimateWidth))
            % static_cast<int32_t>(kClimateWidth);
        const int32_t nextY = (y + dy[direction & 7u]
            + static_cast<int32_t>(kClimateWidth))
            % static_cast<int32_t>(kClimateWidth);
        return static_cast<uint32_t>(nextY) * kClimateWidth
            + static_cast<uint32_t>(nextX);
    }

    double cellTrait(const Config& config, uint32_t cell,
        uint32_t trait) const noexcept
    {
        return signedHash(config.seed,
            static_cast<uint64_t>(cycleIndex_) * 31u + cell + 1u,
            700u + trait * 17u);
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
            const uint32_t direction = static_cast<uint32_t>(
                registerBits_ + energyCode
                + static_cast<uint32_t>(mix64(static_cast<uint64_t>(tick)
                    ^ static_cast<uint64_t>(config.seed)))) & 7u;
            previousCell_ = currentCell_;
            currentCell_ = wrappedCellNeighbor(currentCell_, direction);
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

    double effectiveTrait(const Config& config, uint32_t trait,
        double blend) const noexcept
    {
        const double a = cellTrait(config, previousCell_, trait);
        const double b = cellTrait(config, currentCell_, trait);
        return (a + (b - a) * blend) * config.climate;
    }

    double receptorValue(const RelayConfig& relay, uint32_t index,
        double beat, const Config& config) const noexcept
    {
        const uint32_t local = index % kClusterCount;
        double value = 0.0;
        switch (relay.topology) {
        case ReceptorTopology::Local:
            value = clusters_[local] * 0.76
                + nodes_[local * 4u + (index & 3u)] * 0.24;
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
            const double position = wrapped * 4.0;
            const uint32_t a = static_cast<uint32_t>(position) & 3u;
            const uint32_t b = (a + 1u) & 3u;
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
        updateClimateCell(config, tick, beat, beatsPerBar);
        const double blend = climateBlend(config, beat, beatsPerBar);
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

        static constexpr std::array<double, kNodesPerCluster> ringForward {{
            1.18, 1.02, 1.27, -1.16,
        }};
        static constexpr std::array<double, kNodesPerCluster> ringReverse {{
            -0.22, 0.17, 0.21, 0.15,
        }};
        static constexpr std::array<double, kNodesPerCluster> bias {{
            -0.11, 0.075, -0.045, 0.12,
        }};
        static constexpr std::array<double, kClusterCount> alpha {{
            0.075, 0.16, 0.34, 0.61,
        }};
        static constexpr std::array<double, kClusterCount * kClusterCount>
            matrix {{
                 0.00,  0.31, -0.17,  0.10,
                -0.28,  0.00,  0.23,  0.13,
                 0.16, -0.26,  0.00,  0.29,
                -0.12,  0.19, -0.25,  0.00,
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
                    * (matrix[matrixIndex] + plasticity_[matrixIndex]);
            }
            const double parent = cluster == 0u ? 0.0
                : previousClusters[cluster - 1u];
            for (uint32_t local = 0u; local < kNodesPerCluster; ++local) {
                const uint32_t node = cluster * kNodesPerCluster + local;
                const uint32_t previous = cluster * kNodesPerCluster
                    + ((local + 3u) % 4u);
                const uint32_t next = cluster * kNodesPerCluster
                    + ((local + 1u) % 4u);
                const double ring = previousNodes[previous] * ringForward[local]
                    + previousNodes[next] * ringReverse[local];
                const bool bit = ((registerBits_ >> (node % kRelayCount)) & 1u)
                    != 0u;
                const double registerDrive = bit ? 0.115 : -0.065;
                const double noise = config.freeze ? 0.0
                    : signedHash(config.seed,
                        static_cast<uint64_t>(tick), 1000u + node)
                        * config.mutation * 0.115;
                const double impulse = pendingImpulse_[cluster]
                    * (local == 3u ? -0.72 : 0.54 + 0.08 * local);
                const double target = std::tanh((bias[local] + activityBias
                    + ring * (0.50 + effectiveContrast * 0.32)
                    + cross * effectiveCoupling * 0.62
                    + parent * effectiveHierarchy * (cluster == 0u ? 0.0 : 0.48)
                    + registerDrive + impulse + noise) * gain);
                nodes_[node] += (target - nodes_[node]) * alpha[cluster];
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
        // Establish a cycle reset before taking the register edge snapshot so
        // uninterrupted playback and a seek to the same cycle boundary agree.
        updateClimateCell(config, tick, beat, beatsPerBar);
        const uint8_t oldBits = registerBits_;
        advanceNetwork(config, tick, beat, beatsPerBar);
        advanceRegister(config, tick, beat);
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
            const int ccAValue = std::clamp(static_cast<int>(std::lround(
                (receptors_[relay] * 0.5 + 0.5) * 127.0)), 0, 127);
            const double clusterEnergy = std::abs(
                clusters_[relay % kClusterCount]);
            const int ccBValue = std::clamp(static_cast<int>(std::lround(
                std::clamp(clusterEnergy * 0.72 + population * 0.28,
                    0.0, 1.0) * 127.0)), 0, 127);
            emitCc(relay, 0u, mapping.ccA, ccAValue, beat,
                mapping.channel, events, capacity, result);
            emitCc(relay, 1u, mapping.ccB, ccBValue, beat,
                mapping.channel, events, capacity, result);
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
    }

    void updateSnapshot(const Config& config, double beat,
        double beatsPerBar) noexcept
    {
        snapshot_.nodes = nodes_;
        snapshot_.clusters = clusters_;
        snapshot_.receptors = receptors_;
        snapshot_.trail = trail_;
        snapshot_.registerBits = registerBits_;
        snapshot_.currentCell = currentCell_;
        snapshot_.previousCell = previousCell_;
        snapshot_.trailCount = trailCount_;
        snapshot_.climateBlend = climateBlend(config, beat, beatsPerBar);
        const double cycleBeats = static_cast<double>(config.formBars)
            * beatsPerBar;
        double phase = beat / std::max(1.0, cycleBeats);
        phase -= std::floor(phase);
        snapshot_.cyclePhase = phase;
        double energy = 0.0;
        for (double cluster : clusters_) energy += std::abs(cluster);
        snapshot_.energy = energy / static_cast<double>(kClusterCount);
    }

    std::array<double, kNodeCount> nodes_ {};
    std::array<double, kClusterCount> clusters_ {};
    std::array<double, kRelayCount> receptors_ {};
    std::array<double, kClusterCount * kClusterCount> plasticity_ {};
    std::array<double, kClusterCount> pendingImpulse_ {};
    std::array<std::array<ActiveNote, kVoicesPerRelay>, kRelayCount>
        activeNotes_ {};
    std::array<uint8_t, 16u * 128u> noteOwners_ {};
    std::array<std::array<int, 2u>, kRelayCount> lastCc_ {};
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
    bool continuous_ = false;
    Snapshot snapshot_ {};
};

} // namespace s3g::relay
