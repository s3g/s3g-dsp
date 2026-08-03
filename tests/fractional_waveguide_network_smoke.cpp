#include "s3g_fractional_waveguide_network.h"
#include "s3g_euclidean_rhythm.h"
#include "s3g_midi_node_allocator.h"
#include "s3g_scale_note_pool.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockFrames = 256u;

struct RenderBlock {
    std::array<std::array<float, kBlockFrames>,
        s3g::kFractionalWaveguideMaxChannels> storage {};
    std::array<float*, s3g::kFractionalWaveguideMaxChannels> pointers {};

    RenderBlock()
    {
        for (uint32_t channel = 0u; channel < pointers.size(); ++channel) {
            pointers[channel] = storage[channel].data();
        }
    }
};

bool finiteBlock(const RenderBlock& block, uint32_t frames)
{
    for (const auto& channel : block.storage) {
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            if (!std::isfinite(channel[frame])) return false;
        }
    }
    return true;
}

bool fractionalDelayProbe()
{
    constexpr float requestedDelay = 37.35f;
    s3g::WaveguideFractionalDelay delay;
    delay.prepare(kSampleRate, 0.1f);
    delay.setDelaySamples(requestedDelay);
    delay.reset();

    double energy = 0.0;
    double dc = 0.0;
    double moment = 0.0;
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        const float output = delay.read();
        delay.writeAndAdvance(frame == 0u ? 1.0f : 0.0f);
        energy += static_cast<double>(output) * output;
        dc += output;
        moment += static_cast<double>(frame) * output;
    }
    const double measuredDelay = moment / std::max(1.0e-12, dc);
    if (std::abs(delay.fractionalDelay() - 0.35f) > 0.0001f
        || std::abs(energy - 1.0) > 0.0005
        || std::abs(dc - 1.0) > 0.0005
        || std::abs(measuredDelay - requestedDelay) > 0.001) {
        std::cerr << "fractional delay was not lossless or accurately timed: "
                  << delay.fractionalDelay() << " / "
                  << energy << " / " << dc << " / "
                  << measuredDelay << "\n";
        return false;
    }
    return true;
}

bool fractionalDelayMorphProbe()
{
    s3g::WaveguideFractionalDelay delay;
    delay.prepare(kSampleRate, 0.1f);
    delay.setDelaySamples(37.35f);
    delay.reset();
    float previous = 0.0f;
    float maximumDelta = 0.0f;
    bool finite = true;
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        if (frame == 4096u) delay.setDelaySamples(311.7f, true);
        if (frame == 4300u) delay.setDelaySamples(91.2f, true);
        if (frame == 4500u) delay.setDelaySamples(123.4f, true);
        const float output = delay.read();
        const float input = std::sin(
            2.0 * 3.14159265358979323846 * 440.0
            * static_cast<double>(frame) / kSampleRate);
        delay.writeAndAdvance(input);
        finite = finite && std::isfinite(output);
        if (frame >= 3900u) {
            maximumDelta = std::max(
                maximumDelta, std::abs(output - previous));
        }
        previous = output;
    }
    if (!finite || maximumDelta > 0.12f
        || std::abs(delay.delaySamples() - 123.4f) > 0.001f) {
        std::cerr << "delay-head retuning clicked or lost its queued target: "
                  << maximumDelta << " / " << delay.delaySamples() << "\n";
        return false;
    }
    return true;
}

bool euclideanRhythmProbe()
{
    for (uint32_t steps = 1u; steps <= 32u; ++steps) {
        for (uint32_t pulses = 0u; pulses <= steps; ++pulses) {
            std::array<uint32_t, 32u> hits {};
            uint32_t hitCount = 0u;
            for (uint32_t step = 0u; step < steps; ++step) {
                if (s3g::euclideanRhythmPulse(step, pulses, steps)) {
                    hits[hitCount++] = step;
                }
            }
            if (hitCount != pulses) {
                std::cerr << "Euclidean rhythm emitted the wrong hit count\n";
                return false;
            }
            if (hitCount > 1u) {
                uint32_t shortestGap = steps;
                uint32_t longestGap = 0u;
                for (uint32_t hit = 0u; hit < hitCount; ++hit) {
                    const uint32_t next = hits[(hit + 1u) % hitCount];
                    const uint32_t gap =
                        (next + steps - hits[hit]) % steps;
                    shortestGap = std::min(shortestGap, gap);
                    longestGap = std::max(longestGap, gap);
                }
                if (longestGap - shortestGap > 1u) {
                    std::cerr << "Euclidean rhythm was not evenly spaced\n";
                    return false;
                }
            }
        }
    }

    for (uint32_t steps = 1u; steps <= 32u; ++steps) {
        for (uint32_t pulses = 0u; pulses <= 32u; ++pulses) {
            uint32_t emitted = 0u;
            uint32_t minimum = 32u;
            uint32_t maximum = 0u;
            for (uint32_t step = 0u; step < steps; ++step) {
                const uint32_t count = s3g::euclideanRhythmRatchetCount(
                    step, pulses, steps, 5u);
                emitted += count;
                minimum = std::min(minimum, count);
                maximum = std::max(maximum, count);
                if (pulses <= steps
                    && (count > 0u) != s3g::euclideanRhythmPulse(
                        step, pulses, steps, 5u)) {
                    std::cerr << "ratchet count changed a sparse rhythm\n";
                    return false;
                }
            }
            if (emitted != pulses || maximum - minimum > 1u) {
                std::cerr << "Euclidean ratchets were not evenly distributed: "
                          << pulses << " / " << steps << " / "
                          << emitted << " / " << minimum << " / "
                          << maximum << "\n";
                return false;
            }
        }
    }

    if (s3g::euclideanRhythmRatchetCount(0u, 20u, 16u) != 2u
        || s3g::euclideanRhythmRatchetCount(1u, 20u, 16u) != 1u
        || s3g::euclideanRhythmRatchetCount(0u, 32u, 8u) != 4u) {
        std::cerr << "dense Euclidean lanes did not become ratchets\n";
        return false;
    }

    constexpr std::array<uint32_t, 8u> pulses {{
        5u, 4u, 3u, 2u, 5u, 3u, 4u, 2u
    }};
    constexpr std::array<uint32_t, 8u> rotations {{
        0u, 3u, 6u, 9u, 2u, 5u, 8u, 11u
    }};
    uint32_t activeNodes = 0u;
    uint32_t hitCount = 0u;
    uint32_t patternChanges = 0u;
    uint32_t previousMask = 0u;
    for (uint32_t step = 0u; step < 16u; ++step) {
        uint32_t mask = 0u;
        for (uint32_t node = 0u; node < pulses.size(); ++node) {
            if (!s3g::euclideanRhythmPulse(
                    step, pulses[node], 16u, rotations[node])) {
                continue;
            }
            mask |= 1u << node;
            activeNodes |= 1u << node;
            ++hitCount;
        }
        if (step > 0u && mask != previousMask) ++patternChanges;
        previousMask = mask;
    }
    if (activeNodes != 0xffu || hitCount != 28u
        || patternChanges < 8u
        || !s3g::euclideanRhythmPulse(2u, 3u, 8u, 2u)
        || s3g::euclideanRhythmPulse(1u, 3u, 8u, 2u)) {
        std::cerr << "rotated node lanes did not vary spatial strike order\n";
        return false;
    }
    return true;
}

bool midiNodeAllocatorProbe()
{
    uint32_t cursor = 0u;
    uint32_t freeMask = 0xffu;
    for (uint32_t expected = 0u; expected < 8u; ++expected) {
        const uint32_t node =
            s3g::allocateSequentialMidiNode(freeMask, cursor);
        if (node != expected) {
            std::cerr << "sequential MIDI allocation did not follow nodes 1-8\n";
            return false;
        }
        freeMask &= ~(1u << node);
    }
    if (s3g::allocateSequentialMidiNode(0u, cursor) != 0u) {
        std::cerr << "sequential MIDI voice stealing did not wrap to node 1\n";
        return false;
    }

    uint32_t randomState = 0x12345678u;
    for (uint32_t trial = 0u; trial < 32u; ++trial) {
        if (s3g::allocateRandomMidiNode(1u << 5u, randomState) != 5u) {
            std::cerr << "random MIDI allocation selected an occupied node\n";
            return false;
        }
    }
    uint32_t observed = 0u;
    constexpr uint32_t available = (1u << 1u) | (1u << 4u) | (1u << 7u);
    for (uint32_t trial = 0u; trial < 64u; ++trial) {
        const uint32_t node =
            s3g::allocateRandomMidiNode(available, randomState);
        if ((available & (1u << node)) == 0u) return false;
        observed |= 1u << node;
    }
    if (observed != available) {
        std::cerr << "random MIDI allocation did not vary its free node\n";
        return false;
    }

    s3g::MidiNodeShuffleBag bag;
    uint32_t shuffledState = 0x6d2b79f5u;
    uint32_t previous = 8u;
    for (uint32_t cycle = 0u; cycle < 4u; ++cycle) {
        uint32_t cycleMask = 0u;
        for (uint32_t draw = 0u; draw < 8u; ++draw) {
            const uint32_t node = s3g::allocateShuffledMidiNode(
                0xffu, shuffledState, bag);
            if (node == previous || (cycleMask & (1u << node)) != 0u) {
                std::cerr << "shuffle-bag MIDI allocation repeated early\n";
                return false;
            }
            cycleMask |= 1u << node;
            previous = node;
        }
        if (cycleMask != 0xffu) {
            std::cerr << "shuffle-bag MIDI allocation missed a node\n";
            return false;
        }
    }

    uint32_t routedCursor = 0u;
    uint32_t routedState = 0x4f1bbcdcu;
    s3g::MidiNodeShuffleBag routedBag;
    for (uint32_t expected = 0u; expected < 8u; ++expected) {
        if (s3g::routeSequencerNode(2u, 7u, routedCursor,
                routedState, routedBag) != expected) {
            std::cerr << "poly-sequential mode did not route sequencer nodes\n";
            return false;
        }
    }
    uint32_t routedMask = 0u;
    for (uint32_t draw = 0u; draw < 8u; ++draw) {
        routedMask |= 1u << s3g::routeSequencerNode(
            3u, 0u, routedCursor, routedState, routedBag);
    }
    if (routedMask != 0xffu
        || s3g::routeSequencerNode(
            0u, 6u, routedCursor, routedState, routedBag) != 6u
        || s3g::routeSequencerNode(
            1u, 5u, routedCursor, routedState, routedBag) != 5u) {
        std::cerr << "poly mode did not govern sequencer node routing\n";
        return false;
    }
    return true;
}

bool scaleNotePoolProbe()
{
    constexpr std::array<int32_t, 8u> major {{
        60, 62, 59, 64, 57, 65, 55, 67
    }};
    for (uint32_t node = 0u; node < 8u; ++node) {
        if (s3g::dispersedScaleMidiNote(
                60, node, 1u, s3g::ScaleRule::Major) != 60
            || s3g::dispersedScaleMidiNote(
                60, node, 8u, s3g::ScaleRule::Major) != major[node]) {
            std::cerr << "scale note pool did not lock or disperse around root\n";
            return false;
        }
    }
    constexpr std::array<int32_t, 4u> chromatic {{ 60, 61, 59, 62 }};
    for (uint32_t node = 0u; node < 8u; ++node) {
        if (s3g::dispersedScaleMidiNote(
                60, node, 4u, s3g::ScaleRule::Chromatic)
            != chromatic[node % chromatic.size()]) {
            std::cerr << "chromatic note pool did not repeat its locked size\n";
            return false;
        }
    }
    if (s3g::dispersedScaleMidiNote(
            127, 7u, 8u, s3g::ScaleRule::Major) != 127
        || s3g::dispersedScaleMidiNote(
            0, 6u, 8u, s3g::ScaleRule::Major) != 0) {
        std::cerr << "scale note pool exceeded MIDI note bounds\n";
        return false;
    }
    return true;
}

bool directionalRadiationProbe()
{
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 1u;
    params.radiation = 0.0f;
    params.outputGainDb = -18.0f;
    network.setParams(params);
    network.prepare(kSampleRate);
    network.strike(0u, 0.5f);

    RenderBlock output;
    network.process(nullptr, output.pointers.data(),
        static_cast<uint32_t>(output.pointers.size()), 1u);
    if (!finiteBlock(output, 1u)
        || !(output.storage[0][0] > 0.0f)
        || !(output.storage[1][0] < 0.0f)
        || !(output.storage[2][0] < 0.0f)
        || !(output.storage[3][0] < 0.0f)) {
        std::cerr << "cube-node strike did not radiate toward its HOA direction\n";
        return false;
    }
    for (uint32_t channel = 4u; channel < output.storage.size(); ++channel) {
        if (output.storage[channel][0] != 0.0f) {
            std::cerr << "channels above the selected order were not cleared\n";
            return false;
        }
    }
    return true;
}

bool nodeDirectivityProbe()
{
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 3u;
    params.radiation = 0.0f;
    params.outputGainDb = -24.0f;
    network.setParams(params);
    network.prepare(kSampleRate);
    for (uint32_t node = 0u; node < 8u; ++node) {
        network.setNodeDirectivity(node, 1.0f, true);
    }
    RenderBlock output;
    const auto renderStrike = [&](uint32_t node) {
        network.strike(node, 0.5f);
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), 1u);
    };

    renderStrike(0u);
    const float firstCenter = network.nodeDirectivityMaskTarget(0u);
    const float firstAdjacent = network.nodeDirectivityMaskTarget(1u);
    const float firstOpposite = network.nodeDirectivityMaskTarget(7u);
    renderStrike(7u);
    const float movedCenter = network.nodeDirectivityMaskTarget(7u);
    const float previousCenter = network.nodeDirectivityMaskTarget(0u);
    network.strike(0u, 0.5f);
    network.strike(7u, 0.5f);
    network.process(nullptr, output.pointers.data(),
        static_cast<uint32_t>(output.pointers.size()), 1u);
    const float simultaneousFirst =
        network.nodeDirectivityMaskTarget(0u);
    const float simultaneousSecond =
        network.nodeDirectivityMaskTarget(7u);
    const float simultaneousSide =
        network.nodeDirectivityMaskTarget(1u);

    network.setNodeDirectivity(7u, 0.0f, true);
    renderStrike(7u);
    const float zeroDepthOther = network.nodeDirectivityMaskTarget(0u);
    if (firstCenter < 0.999f || firstAdjacent >= 0.05f
        || firstOpposite >= 0.001f
        || movedCenter < 0.999f || previousCenter >= 0.001f
        || simultaneousFirst < 0.999f || simultaneousSecond < 0.999f
        || simultaneousSide >= 0.05f
        || zeroDepthOther < 0.999f) {
        std::cerr << "node directivity mask did not follow strike location: "
                  << firstCenter << " / " << firstAdjacent << " / "
                  << firstOpposite << " / " << movedCenter << " / "
                  << previousCenter << " / " << simultaneousFirst << " / "
                  << simultaneousSecond << " / " << simultaneousSide << " / "
                  << zeroDepthOther << "\n";
        return false;
    }
    return true;
}

struct DecayProbe {
    double firstEnergy = 0.0;
    double lastEnergy = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

DecayProbe renderDecay()
{
    constexpr uint32_t totalFrames = 4u * 48000u;
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 3u;
    params.decaySeconds = 1.2f;
    params.absorption = 0.14f;
    params.radiation = 0.08f;
    params.outputGainDb = -18.0f;
    network.setParams(params);
    network.prepare(kSampleRate);
    network.strike(0u, 0.8f);

    RenderBlock output;
    DecayProbe result;
    for (uint32_t offset = 0u; offset < totalFrames;
         offset += kBlockFrames) {
        const uint32_t frames = std::min(
            kBlockFrames, totalFrames - offset);
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), frames);
        result.finite = result.finite && finiteBlock(output, frames)
            && std::isfinite(network.travelingEnergy());
        for (uint32_t frame = 0u; frame < frames; ++frame) {
            const float sample = output.storage[0][frame];
            const double energy = static_cast<double>(sample) * sample;
            if (offset + frame < 48000u) result.firstEnergy += energy;
            if (offset + frame >= totalFrames - 48000u)
                result.lastEnergy += energy;
            result.peak = std::max(result.peak, std::abs(sample));
        }
    }
    return result;
}

std::vector<float> renderGeometry(float halfExtent, float dispersion = 0.0f)
{
    constexpr uint32_t frames = 48000u;
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 1u;
    params.decaySeconds = 1.8f;
    params.absorption = 0.05f;
    params.radiation = 0.0f;
    params.dispersion = dispersion;
    params.outputGainDb = -20.0f;
    network.configureCube(halfExtent);
    network.setParams(params);
    network.prepare(kSampleRate);
    network.strike(0u, 0.7f);

    std::vector<float> result;
    result.reserve(frames);
    RenderBlock output;
    for (uint32_t offset = 0u; offset < frames; offset += kBlockFrames) {
        const uint32_t count = std::min(kBlockFrames, frames - offset);
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), count);
        result.insert(result.end(), output.storage[0].begin(),
            output.storage[0].begin() + count);
    }
    return result;
}

bool geometryTimingProbe()
{
    s3g::FractionalWaveguideNetwork network;
    network.prepare(kSampleRate);
    const float defaultDelay = network.edgeDelaySamples(0u);
    network.configureCube(0.503f);
    const float movedDelay = network.edgeDelaySamples(0u);
    if (network.nodeCount() != 8u || network.edgeCount() != 12u
        || std::abs(defaultDelay - 48000.0f / 343.0f) > 0.01f
        || std::abs(defaultDelay - movedDelay) < 0.2f) {
        std::cerr << "cube geometry did not produce metric edge delays: "
                  << defaultDelay << " / " << movedDelay << "\n";
        return false;
    }

    const auto first = renderGeometry(0.5f);
    const auto second = renderGeometry(0.503f);
    double difference = 0.0;
    double energy = 0.0;
    for (uint32_t frame = 0u; frame < first.size(); ++frame) {
        const double delta =
            static_cast<double>(first[frame]) - second[frame];
        difference += delta * delta;
        energy += static_cast<double>(first[frame]) * first[frame];
    }
    if (!(difference > energy * 0.01)) {
        std::cerr << "fractional geometry did not materially retune the network: "
                  << difference << " / " << energy << "\n";
        return false;
    }
    const auto dispersed = renderGeometry(0.5f, 0.72f);
    double dispersionDifference = 0.0;
    for (uint32_t frame = 0u; frame < first.size(); ++frame) {
        const double delta =
            static_cast<double>(first[frame]) - dispersed[frame];
        dispersionDifference += delta * delta;
    }
    if (!(dispersionDifference > energy * 0.001)) {
        std::cerr << "allpass material dispersion did not retune modes: "
                  << dispersionDifference << " / " << energy << "\n";
        return false;
    }

    s3g::FractionalWaveguideParams extremeParams;
    extremeParams.propagationSpeed = 2000.0f;
    network.configureCube(0.02f);
    network.setParams(extremeParams);
    const float smallestCombination = network.edgeDelaySamples(0u);
    extremeParams.propagationSpeed = 20.0f;
    network.configureCube(4.0f);
    network.setParams(extremeParams);
    const float largestCombination = network.edgeDelaySamples(0u);
    const float longestAudibleDelay =
        static_cast<float>(kSampleRate) / (4.0f * 18.0f);
    if (smallestCombination < 3.0f
        || smallestCombination > longestAudibleDelay
        || largestCombination < 3.0f
        || largestCombination > longestAudibleDelay) {
        std::cerr << "size/speed register folding left the audible range: "
                  << smallestCombination << " / "
                  << largestCombination << "\n";
        return false;
    }
    network.setTuningFrequency(440.0f);
    const float tunedDelay = network.edgeDelaySamples(0u);
    const float expectedTunedDelay =
        static_cast<float>(kSampleRate) / (4.0f * 440.0f);
    if (std::abs(network.tuningFrequency() - 440.0f) > 0.001f
        || std::abs(tunedDelay - expectedTunedDelay) > 0.01f) {
        std::cerr << "MIDI tuning did not set the waveguide loop frequency: "
                  << network.tuningFrequency() << " / "
                  << tunedDelay << "\n";
        return false;
    }
    network.setTuningFrequency(13.75f);
    if (std::abs(network.tuningFrequency() - 27.5f) > 0.001f) {
        std::cerr << "MIDI tuning did not fold into the audible register\n";
        return false;
    }
    network.clearTuningFrequency();
    if (network.tuningFrequency() != 0.0f) {
        std::cerr << "MIDI tuning override did not clear\n";
        return false;
    }
    return true;
}

struct SustainedProbe {
    double lateEnergy = 0.0;
    float peak = 0.0f;
    bool finite = true;
    std::array<float, kBlockFrames> signature {};
};

SustainedProbe renderSustained(s3g::WaveguideExciter exciter)
{
    constexpr uint32_t blocks = 240u;
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 3u;
    params.decaySeconds = 3.5f;
    params.absorption = 0.18f;
    params.radiation = 0.42f;
    params.dispersion = 0.28f;
    params.outputGainDb = -18.0f;
    params.exciter = exciter;
    params.sustainedExcitation = 0.62f;
    params.exciterCharacter = 0.53f;
    network.setParams(params);
    network.prepare(kSampleRate);

    RenderBlock output;
    SustainedProbe result;
    for (uint32_t block = 0u; block < blocks; ++block) {
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), kBlockFrames, 2u);
        result.finite = result.finite
            && finiteBlock(output, kBlockFrames)
            && std::isfinite(network.travelingEnergy());
        for (const auto& channel : output.storage) {
            for (float sample : channel) {
                result.peak = std::max(result.peak, std::abs(sample));
                if (block >= blocks - 24u) {
                    result.lateEnergy +=
                        static_cast<double>(sample) * sample;
                }
            }
        }
        if (block == blocks - 1u) {
            result.signature = output.storage[0];
        }
    }
    return result;
}

bool sustainedExciterProbe()
{
    const SustainedProbe bow = renderSustained(s3g::WaveguideExciter::Bow);
    const SustainedProbe reed = renderSustained(s3g::WaveguideExciter::Reed);
    const SustainedProbe jet = renderSustained(s3g::WaveguideExciter::AirJet);
    if (!bow.finite || !reed.finite || !jet.finite
        || bow.lateEnergy <= 1.0e-7
        || reed.lateEnergy <= 1.0e-7
        || jet.lateEnergy <= 1.0e-7
        || bow.peak > 0.89126f || reed.peak > 0.89126f
        || jet.peak > 0.89126f) {
        std::cerr << "continuous waveguide exciters were silent or unsafe: "
                  << bow.lateEnergy << " / "
                  << reed.lateEnergy << " / "
                  << jet.lateEnergy << "\n";
        return false;
    }
    const auto difference = [](const SustainedProbe& first,
                               const SustainedProbe& second) {
        double sum = 0.0;
        for (uint32_t frame = 0u; frame < kBlockFrames; ++frame) {
            const double delta = static_cast<double>(first.signature[frame])
                - second.signature[frame];
            sum += delta * delta;
        }
        return sum;
    };
    if (difference(bow, reed) <= 1.0e-9
        || difference(reed, jet) <= 1.0e-9
        || difference(bow, jet) <= 1.0e-9) {
        std::cerr << "bow, reed, and air-jet palettes were not distinct\n";
        return false;
    }
    return true;
}

bool triggeredExciterProbe()
{
    constexpr std::array<s3g::WaveguideExciter, 3u> exciters {{
        s3g::WaveguideExciter::Bow,
        s3g::WaveguideExciter::Reed,
        s3g::WaveguideExciter::AirJet,
    }};
    for (const auto exciter : exciters) {
        s3g::FractionalWaveguideNetwork network;
        s3g::FractionalWaveguideParams params;
        params.exciter = exciter;
        params.sustainedExcitation = 0.0f;
        params.exciterCharacter = 0.58f;
        params.decaySeconds = 2.0f;
        params.outputGainDb = -18.0f;
        network.setParams(params);
        network.prepare(kSampleRate);

        RenderBlock output;
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), kBlockFrames, 0u);
        double silentEnergy = 0.0;
        for (const auto& channel : output.storage) {
            for (const float sample : channel) {
                silentEnergy += static_cast<double>(sample) * sample;
            }
        }

        network.triggerExciter(5u, 0.72f);
        double gestureEnergy = 0.0;
        float peak = 0.0f;
        bool finite = true;
        for (uint32_t block = 0u; block < 96u; ++block) {
            network.process(nullptr, output.pointers.data(),
                static_cast<uint32_t>(output.pointers.size()),
                kBlockFrames, 0u);
            finite = finite && finiteBlock(output, kBlockFrames)
                && std::isfinite(network.travelingEnergy());
            for (const auto& channel : output.storage) {
                for (const float sample : channel) {
                    peak = std::max(peak, std::abs(sample));
                    gestureEnergy += static_cast<double>(sample) * sample;
                }
            }
        }
        if (silentEnergy > 1.0e-14 || !finite
            || gestureEnergy <= 1.0e-7 || peak > 0.89126f) {
            std::cerr << "triggered physical exciter was silent or unsafe: "
                      << static_cast<uint32_t>(exciter) << " / "
                      << silentEnergy << " / " << gestureEnergy << " / "
                      << peak << "\n";
            return false;
        }
    }
    return true;
}

bool sustainedGateProbe()
{
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.exciter = s3g::WaveguideExciter::AirJet;
    params.sustainedExcitation = 0.7f;
    params.outputGainDb = -18.0f;
    network.setParams(params);
    network.prepare(kSampleRate);
    std::array<float, kBlockFrames> closed {};
    std::array<float, kBlockFrames> open {};
    open.fill(1.0f);
    RenderBlock output;
    double closedEnergy = 0.0;
    double openEnergy = 0.0;
    for (uint32_t block = 0u; block < 48u; ++block) {
        const float* gate = block < 8u ? closed.data() : open.data();
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()),
            kBlockFrames, 0u, gate);
        for (const auto& channel : output.storage) {
            for (float sample : channel) {
                const double energy = static_cast<double>(sample) * sample;
                if (block < 8u) closedEnergy += energy;
                if (block >= 40u) openEnergy += energy;
            }
        }
    }
    if (closedEnergy > 1.0e-14 || openEnergy <= 1.0e-7) {
        std::cerr << "MIDI sustain gate did not close and open excitation: "
                  << closedEnergy << " / " << openEnergy << "\n";
        return false;
    }
    return true;
}

bool liveGeometryMorphProbe()
{
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.exciter = s3g::WaveguideExciter::Reed;
    params.sustainedExcitation = 0.58f;
    params.decaySeconds = 5.0f;
    params.outputGainDb = -18.0f;
    network.setParams(params);
    network.prepare(kSampleRate);
    RenderBlock output;
    for (uint32_t block = 0u; block < 32u; ++block) {
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), kBlockFrames, 0u);
    }
    const float energyBefore = network.travelingEnergy();
    const float delayBefore = network.edgeDelaySamples(0u);
    network.morphCube(1.37f);
    const float delayAfter = network.edgeDelaySamples(0u);
    if (!(energyBefore > 1.0e-8f)
        || std::abs(network.travelingEnergy() - energyBefore) > 1.0e-12f
        || std::abs(delayAfter - delayBefore) < 1.0f) {
        std::cerr << "live SIZE morph reset the resonator or missed its target\n";
        return false;
    }
    params.propagationSpeed = 720.0f;
    const float energyBeforeSpeed = network.travelingEnergy();
    network.setParams(params);
    if (std::abs(network.travelingEnergy() - energyBeforeSpeed) > 1.0e-12f) {
        std::cerr << "live SPEED morph reset the resonator\n";
        return false;
    }
    for (uint32_t block = 0u; block < 48u; ++block) {
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), kBlockFrames, 0u);
        if (!finiteBlock(output, kBlockFrames)) {
            std::cerr << "live geometry delay-head crossfade became non-finite\n";
            return false;
        }
    }
    return true;
}

bool repeatedExcitationProbe()
{
    s3g::FractionalWaveguideNetwork network;
    s3g::FractionalWaveguideParams params;
    params.order = 3u;
    params.decaySeconds = 12.0f;
    params.absorption = 0.0f;
    params.junctionNonlinearity = 0.28f;
    params.radiation = 0.0f;
    params.outputGainDb = 12.0f;
    network.setParams(params);
    network.prepare(kSampleRate);

    RenderBlock output;
    float peak = 0.0f;
    for (uint32_t block = 0u; block < 1200u; ++block) {
        if (block % 3u == 0u) {
            network.strike((block / 3u) % network.nodeCount(), 1.0f);
        }
        network.process(nullptr, output.pointers.data(),
            static_cast<uint32_t>(output.pointers.size()), kBlockFrames);
        if (!finiteBlock(output, kBlockFrames)
            || !std::isfinite(network.travelingEnergy())) {
            std::cerr << "repeated excitation produced non-finite state\n";
            return false;
        }
        for (const auto& channel : output.storage) {
            for (float sample : channel) {
                peak = std::max(peak, std::abs(sample));
            }
        }
    }
    if (peak > 0.89126f || !(network.guardGain() < 1.0f)
        || !(network.outputPeak() > 0.0f)) {
        std::cerr << "linked HOA guard did not contain repeated excitation: "
                  << peak << " / " << network.guardGain() << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!fractionalDelayProbe()
        || !fractionalDelayMorphProbe()
        || !euclideanRhythmProbe()
        || !midiNodeAllocatorProbe()
        || !scaleNotePoolProbe()
        || !directionalRadiationProbe()
        || !nodeDirectivityProbe()
        || !geometryTimingProbe()
        || !sustainedExciterProbe()
        || !triggeredExciterProbe()
        || !sustainedGateProbe()
        || !liveGeometryMorphProbe()
        || !repeatedExcitationProbe()) {
        return 1;
    }

    const DecayProbe decay = renderDecay();
    if (!decay.finite || !(decay.firstEnergy > 0.000001)
        || !(decay.lastEnergy < decay.firstEnergy * 0.02)
        || decay.peak > 0.89126f) {
        std::cerr << "waveguide field did not decay safely: "
                  << decay.firstEnergy << " / "
                  << decay.lastEnergy << " / "
                  << decay.peak << "\n";
        return 1;
    }

    std::cout << "fractional waveguide network smoke passed\n";
    return 0;
}
