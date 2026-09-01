#include "s3g_relay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using s3g::relay::Config;
using s3g::relay::Engine;
using s3g::relay::Event;
using s3g::relay::EventKind;
using s3g::relay::ArticulationMode;

bool equivalent(const Event& a, const Event& b)
{
    return std::abs(a.beat - b.beat) < 1.0e-9
        && a.kind == b.kind && a.channel == b.channel
        && a.data1 == b.data1 && a.data2 == b.data2
        && a.relay == b.relay;
}

std::vector<Event> render(const Config& config,
    const std::vector<double>& boundaries)
{
    Engine engine;
    std::vector<Event> output;
    for (std::size_t block = 1u; block < boundaries.size(); ++block) {
        Event events[4096] {};
        const auto result = engine.process(boundaries[block - 1u],
            boundaries[block], 4.0, true, config, events, 4096u);
        if (result.dropped != 0u) return {};
        output.insert(output.end(), events, events + result.count);
    }
    return output;
}

std::vector<Event> renderAndStop(const Config& config, double endBeat)
{
    Engine engine;
    std::vector<Event> output;
    Event playing[4096] {};
    const auto playingResult = engine.process(
        0.0, endBeat, 4.0, true, config, playing, 4096u);
    output.insert(output.end(), playing, playing + playingResult.count);
    Event stopped[4096] {};
    const auto stoppedResult = engine.process(
        endBeat, endBeat, 4.0, false, config, stopped, 4096u);
    output.insert(output.end(), stopped, stopped + stoppedResult.count);
    return output;
}

bool testBlockSizeIndependence()
{
    Config config;
    config.ccRateIndex = 2u;
    const auto single = render(config, { 0.0, 16.0 });
    std::vector<double> irregular { 0.0 };
    const double increments[] { 0.137, 0.251, 0.083, 0.419, 0.067 };
    std::size_t increment = 0u;
    while (irregular.back() < 16.0) {
        irregular.push_back(std::min(16.0,
            irregular.back() + increments[increment++ % 5u]));
    }
    const auto segmented = render(config, irregular);
    if (single.size() != segmented.size()) {
        std::cerr << "Relay event count changed with block boundaries: "
                  << single.size() << " / " << segmented.size() << '\n';
        return false;
    }
    for (std::size_t index = 0u; index < single.size(); ++index) {
        if (!equivalent(single[index], segmented[index])) {
            const auto& a = single[index];
            const auto& b = segmented[index];
            std::cerr << "Relay event changed at index " << index
                      << ": " << a.beat << "/" << static_cast<int>(a.kind)
                      << "/" << static_cast<int>(a.relay)
                      << " vs " << b.beat << "/" << static_cast<int>(b.kind)
                      << "/" << static_cast<int>(b.relay) << '\n';
            return false;
        }
    }
    return !single.empty();
}

bool testGenericMappings()
{
    Config config;
    config.memory = 1.0;
    config.mutation = 0.0;
    config.ccRateIndex = 0u;
    for (auto& relay : config.relays) relay.enabled = false;
    auto& relay = config.relays[0];
    relay.enabled = true;
    relay.channel = 3u;
    relay.note = 64u;
    relay.ccA = 74u;
    relay.ccB = 71u;
    relay.refractoryTicks = 1u;
    const auto events = render(config, { 0.0, 16.0 });
    bool note = false;
    bool ccA = false;
    bool ccB = false;
    for (const Event& event : events) {
        if (event.channel != 3u) continue;
        note |= event.kind == EventKind::NoteOn && event.data1 == 64u;
        ccA |= event.kind == EventKind::ControlChange && event.data1 == 74u;
        ccB |= event.kind == EventKind::ControlChange && event.data1 == 71u;
    }
    if (!note || !ccA || !ccB) {
        std::cerr << "Relay did not preserve generic note/CC mappings\n";
        return false;
    }
    return true;
}

bool testCrystallineRegister()
{
    Config config;
    config.memory = 1.0;
    config.mutation = 0.0;
    config.freeze = true;
    config.climate = 0.0;
    config.clockRateIndex = 4u;
    config.ccRateIndex = 5u;
    const auto events = render(config, { 0.0, 8.0 });
    std::vector<double> relayZero;
    for (const auto& event : events) {
        if (event.kind == EventKind::NoteOn && event.relay == 0u)
            relayZero.push_back(event.beat);
    }
    if (relayZero.size() < 3u) {
        std::cerr << "Relay crystalline register produced too few recurrences\n";
        return false;
    }
    const double expectedPeriod = 8.0
        * s3g::relay::clockPeriodBeats(config.clockRateIndex);
    for (std::size_t index = 1u; index < relayZero.size(); ++index) {
        const double difference = relayZero[index] - relayZero[index - 1u];
        if (std::abs(difference - expectedPeriod) > 1.0e-9) {
            std::cerr << "Relay crystalline memory did not preserve its loop\n";
            return false;
        }
    }
    return true;
}

bool testSeekReleasesNotes()
{
    Config config;
    config.memory = 1.0;
    config.mutation = 0.0;
    config.gateBeats = 8.0;
    config.ccRateIndex = 5u;
    Engine engine;
    Event first[512] {};
    const auto firstResult = engine.process(
        0.0, 4.0, 4.0, true, config, first, 512u);
    const bool started = std::any_of(first, first + firstResult.count,
        [](const Event& event) { return event.kind == EventKind::NoteOn; });
    Event seek[512] {};
    const auto seekResult = engine.process(
        93.25, 93.5, 4.0, true, config, seek, 512u);
    const bool released = std::any_of(seek, seek + seekResult.count,
        [](const Event& event) {
            return event.kind == EventKind::NoteOff
                && std::abs(event.beat - 93.25) < 1.0e-9;
        });
    if (!started || !released) {
        std::cerr << "Relay seek did not release its active notes\n";
        return false;
    }
    return true;
}

bool testClimateAndFeedback()
{
    Config active;
    active.formBars = 16u;
    active.dwellBars = 1u;
    active.transitionBars = 0.25;
    active.clockRateIndex = 4u;
    active.ccRateIndex = 5u;
    Engine engine;
    Event events[4096] {};
    const auto result = engine.process(
        0.0, 20.0, 4.0, true, active, events, 4096u);
    if (result.snapshot.trailCount < 4u
        || result.snapshot.currentCell >= s3g::relay::kClimateCells
        || !std::isfinite(result.snapshot.energy)) {
        std::cerr << "Relay climate lattice did not traverse safely\n";
        return false;
    }

    Config quietFeedback = active;
    Config strongFeedback = active;
    for (auto& relay : quietFeedback.relays) relay.feedback = 0.0;
    for (auto& relay : strongFeedback.relays) relay.feedback = 1.0;
    Engine quiet;
    Engine strong;
    Event quietEvents[4096] {};
    Event strongEvents[4096] {};
    const auto q = quiet.process(0.0, 12.0, 4.0, true, quietFeedback,
        quietEvents, 4096u);
    const auto s = strong.process(0.0, 12.0, 4.0, true, strongFeedback,
        strongEvents, 4096u);
    double difference = 0.0;
    for (uint32_t node = 0u; node < s3g::relay::kNodeCount; ++node)
        difference += std::abs(q.snapshot.nodes[node] - s.snapshot.nodes[node]);
    if (difference < 1.0e-5) {
        std::cerr << "Relay events did not feed back into the neural ecology\n";
        return false;
    }
    return true;
}

bool testLogicScaleAddressing()
{
    Config config;
    config.scaleRoot = 2u;
    config.scaleOctave = 3;
    config.scale = 6u; // DORIAN
    config.scaleRange = 2u;
    std::array<bool, 128u> heard {};
    uint32_t distinct = 0u;
    for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
        for (uint32_t pattern = 1u; pattern < 256u; pattern += 17u) {
            const uint8_t note = s3g::relay::logicScaleNote(config, relay,
                static_cast<uint8_t>(pattern),
                static_cast<double>(relay) / 3.5 - 1.0,
                pattern & 15u);
            const int relative = static_cast<int>(note) - 50;
            if (relative < 0 || relative >= 24) {
                std::cerr << "Relay scale logic escaped its octave range\n";
                return false;
            }
            const uint32_t pitchClass = static_cast<uint32_t>(relative % 12);
            const bool inDorian = pitchClass == 0u || pitchClass == 2u
                || pitchClass == 3u || pitchClass == 5u
                || pitchClass == 7u || pitchClass == 9u
                || pitchClass == 10u;
            if (!inDorian) {
                std::cerr << "Relay scale logic emitted an out-of-scale note\n";
                return false;
            }
            if (!heard[note]) {
                heard[note] = true;
                ++distinct;
            }
        }
    }
    if (distinct < 8u) {
        std::cerr << "Relay scale logic produced insufficient pitch motion\n";
        return false;
    }
    return true;
}

bool testCanonicalScaleCatalog()
{
    if (s3g::kMusicalScaleCount != 101u) {
        std::cerr << "Relay does not expose the canonical scale catalog\n";
        return false;
    }
    Config config;
    config.scaleRoot = 0u;
    config.scaleOctave = 3;
    config.scaleRange = 4u;
    for (uint32_t scaleId = 0u; scaleId < s3g::kMusicalScaleCount;
         ++scaleId) {
        config.scale = scaleId;
        const auto& scale = s3g::musicalScaleDefinition(scaleId);
        for (uint32_t relay = 0u; relay < s3g::relay::kRelayCount; ++relay) {
            for (uint32_t pattern = 0u; pattern < 256u; pattern += 19u) {
                const uint8_t note = s3g::relay::logicScaleNote(config,
                    relay, static_cast<uint8_t>(pattern),
                    static_cast<double>(relay) / 3.5 - 1.0,
                    (pattern + relay) & 15u);
                const uint32_t pitchClass = note % 12u;
                bool belongs = false;
                for (uint32_t degree = 0u; degree < scale.size; ++degree)
                    belongs |= static_cast<uint32_t>(
                        scale.semitones[degree]) == pitchClass;
                if (!belongs) {
                    std::cerr << "Relay emitted a note outside scale "
                              << scale.name << '\n';
                    return false;
                }
            }
        }
    }
    return true;
}

double firstNoteDuration(ArticulationMode articulation)
{
    Config config;
    config.freeze = true;
    config.memory = 1.0;
    config.mutation = 0.0;
    config.clockRateIndex = 6u;
    config.gateBeats = 2.0;
    config.ccRateIndex = 5u;
    for (auto& relay : config.relays) relay.enabled = false;
    auto& relay = config.relays[0];
    relay.enabled = true;
    relay.note = 60u;
    relay.ccA = s3g::relay::kMidiOff;
    relay.ccB = s3g::relay::kMidiOff;
    relay.refractoryTicks = 1u;
    relay.gateScale = 1.0;
    relay.articulation = articulation;
    const auto events = renderAndStop(config, 6.0);
    double onBeat = -1.0;
    for (const auto& event : events) {
        if (event.data1 != 60u) continue;
        if (event.kind == EventKind::NoteOn && onBeat < 0.0)
            onBeat = event.beat;
        else if (event.kind == EventKind::NoteOff && onBeat >= 0.0)
            return event.beat - onBeat;
    }
    return -1.0;
}

bool testArticulationModes()
{
    const double restart = firstNoteDuration(ArticulationMode::Restart);
    const double hold = firstNoteDuration(ArticulationMode::Hold);
    const double extend = firstNoteDuration(ArticulationMode::Extend);
    const double stack = firstNoteDuration(ArticulationMode::Stack);
    if (!(restart > 0.0 && restart < 2.0 - 1.0e-9)) {
        std::cerr << "Relay Restart did not rearticulate an active note\n";
        return false;
    }
    if (std::abs(hold - 2.0) > 1.0e-9) {
        std::cerr << "Relay Hold did not protect the configured gate\n";
        return false;
    }
    if (!(extend > hold && stack > hold)) {
        std::cerr << "Relay Extend/Stack did not preserve overlapping tails\n";
        return false;
    }
    return true;
}

bool testCollisionSafeOwnership()
{
    Config config;
    config.freeze = true;
    config.memory = 1.0;
    config.mutation = 0.0;
    config.clockRateIndex = 6u;
    config.gateBeats = 8.0;
    config.ccRateIndex = 5u;
    for (auto& relay : config.relays) {
        relay.channel = 2u;
        relay.note = 64u;
        relay.ccA = s3g::relay::kMidiOff;
        relay.ccB = s3g::relay::kMidiOff;
        relay.refractoryTicks = 1u;
        relay.gateScale = 1.0;
        relay.articulation = ArticulationMode::Hold;
    }
    const auto events = renderAndStop(config, 4.0);
    bool held = false;
    uint32_t noteOns = 0u;
    uint32_t noteOffs = 0u;
    for (const auto& event : events) {
        if (event.channel != 2u || event.data1 != 64u) continue;
        if (event.kind == EventKind::NoteOn) {
            if (held) {
                std::cerr << "Relay duplicated a shared MIDI note owner\n";
                return false;
            }
            held = true;
            ++noteOns;
        } else if (event.kind == EventKind::NoteOff) {
            if (!held) {
                std::cerr << "Relay released an unowned MIDI note\n";
                return false;
            }
            held = false;
            ++noteOffs;
        }
    }
    if (held || noteOns != 1u || noteOffs != 1u) {
        std::cerr << "Relay did not coalesce shared channel/note ownership\n";
        return false;
    }
    return true;
}

bool testStackCreatesPolyphonicTails()
{
    Config config;
    config.freeze = true;
    config.memory = 1.0;
    config.mutation = 0.0;
    config.clockRateIndex = 6u;
    config.gateBeats = 4.0;
    config.ccRateIndex = 5u;
    config.scaleRoot = 0u;
    config.scaleOctave = 3;
    config.scale = 0u; // CHROMATIC
    config.scaleRange = 4u;
    for (auto& relay : config.relays) relay.enabled = false;
    auto& relay = config.relays[0];
    relay.enabled = true;
    relay.note = 60u;
    relay.ccA = s3g::relay::kMidiOff;
    relay.ccB = s3g::relay::kMidiOff;
    relay.refractoryTicks = 1u;
    relay.gateScale = 1.0;
    relay.pitchMode = s3g::relay::PitchMode::ScaleLogic;
    relay.articulation = ArticulationMode::Stack;
    const auto events = renderAndStop(config, 16.0);
    std::array<bool, 128u> held {};
    uint32_t active = 0u;
    uint32_t maximum = 0u;
    for (const auto& event : events) {
        if (event.kind == EventKind::NoteOn && !held[event.data1]) {
            held[event.data1] = true;
            maximum = std::max(maximum, ++active);
        } else if (event.kind == EventKind::NoteOff && held[event.data1]) {
            held[event.data1] = false;
            --active;
        }
    }
    if (maximum < 2u || active != 0u) {
        std::cerr << "Relay Stack did not produce clean polyphonic tails\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!testBlockSizeIndependence()) return 1;
    if (!testGenericMappings()) return 1;
    if (!testCrystallineRegister()) return 1;
    if (!testSeekReleasesNotes()) return 1;
    if (!testClimateAndFeedback()) return 1;
    if (!testLogicScaleAddressing()) return 1;
    if (!testCanonicalScaleCatalog()) return 1;
    if (!testArticulationModes()) return 1;
    if (!testCollisionSafeOwnership()) return 1;
    if (!testStackCreatesPolyphonicTails()) return 1;
    std::cout << "Relay core smoke passed\n";
    return 0;
}
