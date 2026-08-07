#include "s3g_drum_floor_tom.h"
#include "s3g_drum_floor_tom_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

Render render(const s3g::DrumFloorTomParams& params,
    s3g::DrumTomArticulation articulation, int midiNote = 43,
    float velocity = 1.0f, double seconds = 0.6,
    double sampleRate = 48000.0)
{
    s3g::DrumFloorTom tom;
    tom.prepare(sampleRate);
    tom.setParams(params);
    tom.reset();
    tom.trigger(articulation, velocity, midiNote);
    Render result {
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
    };
    tom.processBlock(result.left.data(), result.right.data(),
        static_cast<uint32_t>(result.left.size()));
    return result;
}

std::array<float, 26u> paramVector(const s3g::DrumFloorTomParams& p)
{
    return {{
        p.tuneHz, p.noteTracking, p.pitchDropSemitones, p.pitchSweepMs,
        p.shellSpread, p.body, p.ring, p.bodyDecaySeconds, p.punch,
        p.damping, p.rimLevel, p.rimCharacter, p.rimDecaySeconds,
        p.stickLevel, p.stickTone, p.stickDecayMs,
        p.character.drive, p.character.bias, p.character.compression,
        p.character.sampleRateReduction, p.character.bitDepthReduction,
        p.character.reconstruction, p.character.tone, p.stereoWidth,
        p.velocitySensitivity, p.outputGainDb,
    }};
}

double energy(const std::vector<float>& signal, uint32_t begin = 0u,
    uint32_t end = std::numeric_limits<uint32_t>::max())
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    begin = std::min(begin, end);
    double result = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        result += static_cast<double>(signal[frame]) * signal[frame];
    }
    return result;
}

double differenceEnergy(const Render& rendered)
{
    double result = 0.0;
    for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
        const double difference = static_cast<double>(rendered.left[frame])
            - rendered.right[frame];
        result += difference * difference;
    }
    return result;
}

double peakMagnitude(const Render& rendered)
{
    double peak = 0.0;
    for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
        peak = std::max({ peak,
            std::abs(static_cast<double>(rendered.left[frame])),
            std::abs(static_cast<double>(rendered.right[frame])) });
    }
    return peak;
}

double maximumStep(const Render& rendered)
{
    double maximum = 0.0;
    double previousLeft = 0.0;
    double previousRight = 0.0;
    for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
        const double left = rendered.left[frame];
        const double right = rendered.right[frame];
        maximum = std::max({ maximum,
            std::abs(left - previousLeft),
            std::abs(right - previousRight) });
        previousLeft = left;
        previousRight = right;
    }
    return maximum;
}

double goertzelPower(const std::vector<float>& signal, double sampleRate,
    uint32_t begin, uint32_t end, double frequency)
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(signal.size()));
    const double coefficient = 2.0 * std::cos(
        2.0 * static_cast<double>(s3g::kPi) * frequency / sampleRate);
    double previous = 0.0;
    double previousTwo = 0.0;
    for (uint32_t frame = begin; frame < end; ++frame) {
        const double next = signal[frame] + coefficient * previous
            - previousTwo;
        previousTwo = previous;
        previous = next;
    }
    return previous * previous + previousTwo * previousTwo
        - coefficient * previous * previousTwo;
}

double dominantFrequency(const std::vector<float>& signal,
    double lowHz, double highHz)
{
    double bestFrequency = lowHz;
    double bestPower = -1.0;
    for (double frequency = lowHz; frequency <= highHz; frequency += 1.0) {
        const double power = goertzelPower(signal, 48000.0,
            4800u, std::min<uint32_t>(19200u, signal.size()), frequency);
        if (power > bestPower) {
            bestPower = power;
            bestFrequency = frequency;
        }
    }
    return bestFrequency;
}

bool sanitationAndSilenceProbe()
{
    s3g::DrumFloorTom tom;
    tom.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumFloorTomParams invalid;
    invalid.tuneHz = std::numeric_limits<float>::infinity();
    invalid.noteTracking = -9.0f;
    invalid.pitchDropSemitones = 99.0f;
    invalid.pitchSweepMs = std::numeric_limits<float>::quiet_NaN();
    invalid.bodyDecaySeconds = -2.0f;
    invalid.rimDecaySeconds = 9.0f;
    invalid.stickDecayMs = -4.0f;
    invalid.outputGainDb = 80.0f;
    tom.setParams(invalid);
    const auto p = tom.params();
    if (p.tuneHz != 72.0f || p.noteTracking != 0.0f
        || p.pitchDropSemitones != 30.0f || p.pitchSweepMs != 45.0f
        || p.bodyDecaySeconds != 0.04f || p.rimDecaySeconds != 0.35f
        || p.stickDecayMs != 0.5f || p.outputGainDb != 12.0f) {
        std::cerr << "floor tom sanitation failed\n";
        return false;
    }
    tom.reset();
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        tom.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "untriggered floor tom was not silent\n";
            return false;
        }
    }
    s3g::DrumFloorTomParams silent;
    silent.velocitySensitivity = 1.0f;
    tom.setParams(silent);
    tom.trigger(s3g::DrumTomArticulation::Head, 0.0f, 43);
    if (tom.active()) {
        std::cerr << "zero velocity activated floor tom\n";
        return false;
    }
    return true;
}

bool pitchArticulationAndStereoProbe()
{
    s3g::DrumFloorTomParams tonal;
    tonal.noteTracking = 1.0f;
    tonal.pitchDropSemitones = 0.0f;
    tonal.shellSpread = 0.0f;
    tonal.ring = 0.0f;
    tonal.punch = 0.0f;
    tonal.bodyDecaySeconds = 1.2f;
    tonal.outputGainDb = -12.0f;
    const auto low = render(tonal, s3g::DrumTomArticulation::Head,
        43, 1.0f, 0.55);
    const auto high = render(tonal, s3g::DrumTomArticulation::Head,
        55, 1.0f, 0.55);
    const double lowHz = dominantFrequency(low.left, 45.0, 110.0);
    const double highHz = dominantFrequency(high.left, 100.0, 190.0);
    if (!(lowHz > 65.0 && lowHz < 80.0)
        || !(highHz / lowHz > 1.90 && highHz / lowHz < 2.10)) {
        std::cerr << "floor tom tracking failed: " << lowHz
                  << " / " << highHz << "\n";
        return false;
    }

    const auto rimBase = render({}, s3g::DrumTomArticulation::RimStick,
        43, 0.9f, 0.3);
    const auto rimOctaveKey = render({},
        s3g::DrumTomArticulation::RimStick, 55, 0.9f, 0.3);
    const auto rimSideStickKey = render({},
        s3g::DrumTomArticulation::RimStick, 37, 0.9f, 0.3);
    if (rimBase.left != rimOctaveKey.left
        || rimBase.right != rimOctaveKey.right
        || rimBase.left != rimSideStickKey.left
        || rimBase.right != rimSideStickKey.right) {
        std::cerr << "floor rim key was not pitch-normalized\n";
        return false;
    }
    const auto head = render({}, s3g::DrumTomArticulation::Head,
        43, 0.9f, 0.5);
    if (!(energy(rimBase.left, 7200u) < energy(head.left, 7200u) * 0.15
            + 1.0e-8)
        || !(energy(rimBase.left, 0u, 4800u) > 1.0e-4)) {
        std::cerr << "floor rim was not a short distinct articulation\n";
        return false;
    }

    s3g::DrumFloorTomParams mono;
    mono.stereoWidth = 0.0f;
    mono.rimCharacter = 0.9f;
    const auto centered = render(mono,
        s3g::DrumTomArticulation::RimStick);
    if (centered.left != centered.right) {
        std::cerr << "floor width zero was not exact mono\n";
        return false;
    }
    mono.stereoWidth = 1.0f;
    const auto wide = render(mono, s3g::DrumTomArticulation::RimStick);
    if (!(differenceEnergy(wide) >
            (energy(wide.left) + energy(wide.right)) * 1.0e-6)) {
        std::cerr << "floor width did not create upper side energy\n";
        return false;
    }
    return true;
}

bool latchAndResetProbe()
{
    s3g::DrumFloorTomParams first;
    first.bodyDecaySeconds = 0.8f;
    first.ring = 0.65f;
    s3g::DrumFloorTom reference;
    s3g::DrumFloorTom edited;
    reference.prepare(48000.0);
    edited.prepare(48000.0);
    reference.setParams(first);
    edited.setParams(first);
    reference.trigger(s3g::DrumTomArticulation::Head, 0.84f, 43);
    edited.trigger(s3g::DrumTomArticulation::Head, 0.84f, 43);
    for (uint32_t frame = 0u; frame < 512u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
    }
    auto second = first;
    second.tuneHz = 150.0f;
    second.bodyDecaySeconds = 0.05f;
    second.ring = 0.0f;
    second.rimCharacter = 1.0f;
    edited.setParams(second);
    for (uint32_t frame = 0u; frame < 8192u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) {
            std::cerr << "floor per-hit controls were not latched\n";
            return false;
        }
    }

    s3g::DrumFloorTom deterministic;
    deterministic.prepare(48000.0);
    deterministic.setParams(first);
    const auto capture = [&]() {
        std::array<float, 4096u> samples {};
        deterministic.trigger(s3g::DrumTomArticulation::RimStick,
            0.77f, 43);
        for (float& sample : samples) {
            float right = 0.0f;
            deterministic.processFrame(sample, right);
        }
        return samples;
    };
    const auto before = capture();
    deterministic.reset();
    const auto after = capture();
    if (before != after) {
        std::cerr << "floor reset was not deterministic\n";
        return false;
    }
    return true;
}

bool lowHorizonOnsetProbe()
{
    const auto preset = s3g::drumFloorTomFactoryPreset(3u);
    constexpr std::array<double, 3u> sampleRates {{
        44100.0, 48000.0, 96000.0,
    }};
    for (const double sampleRate : sampleRates) {
        const auto head = render(preset,
            s3g::DrumTomArticulation::Head,
            43, 1.0f, 0.10, sampleRate);
        const double peak = peakMagnitude(head);
        const double step = maximumStep(head);
        const uint32_t firstMillisecond = static_cast<uint32_t>(
            std::ceil(sampleRate * 0.001));
        const double totalEnergy = energy(head.left)
            + energy(head.right);
        const double earlyEnergyFraction = (
                energy(head.left, 0u, firstMillisecond)
                + energy(head.right, 0u, firstMillisecond))
            / std::max(1.0e-20, totalEnergy);
        if (head.left[0] != 0.0f || head.right[0] != 0.0f
            || !(peak > 1.0e-4)
            || step > peak * 0.04 + 1.0e-8
            || earlyEnergyFraction > 0.005) {
            std::cerr << "LOW HORIZON onset continuity failed at "
                      << sampleRate << " Hz: start " << head.left[0]
                      << " / " << head.right[0] << ", step/peak "
                      << step << " / " << peak << ", E1 "
                      << earlyEnergyFraction << "\n";
            return false;
        }
    }
    return true;
}

bool presetsAndRandomProbe()
{
    std::vector<std::string> names;
    for (uint32_t index = 0u;
         index < s3g::kDrumFloorTomFactoryPresetCount; ++index) {
        const auto& info = s3g::drumFloorTomFactoryPresetInfo(index);
        const auto preset = s3g::drumFloorTomFactoryPreset(index);
        if (!info.name || !info.description || info.name[0] == '\0'
            || std::find(names.begin(), names.end(), info.name) != names.end()
            || s3g::drumFloorTomFactoryPresetIndex(preset)
                != static_cast<int32_t>(index)) {
            std::cerr << "floor factory preset contract failed at "
                      << index << "\n";
            return false;
        }
        names.emplace_back(info.name);
        const auto head = render(preset, s3g::DrumTomArticulation::Head,
            43, 0.9f, 0.25);
        const auto rim = render(preset,
            s3g::DrumTomArticulation::RimStick, 43, 0.9f, 0.20);
        if (!(energy(head.left) > 1.0e-5)
            || !(energy(rim.left) > 1.0e-5)) {
            std::cerr << "floor factory preset articulation was silent\n";
            return false;
        }
    }
    auto edited = s3g::drumFloorTomFactoryPreset(0u);
    edited.tuneHz += 0.1f;
    if (s3g::drumFloorTomFactoryPresetIndex(edited) != -1) return false;
    edited = s3g::drumFloorTomFactoryPreset(0u);
    edited.outputGainDb += 2.0f;
    if (s3g::drumFloorTomFactoryPresetIndex(edited) != 0) return false;

    s3g::DrumFloorTomParams current;
    current.noteTracking = 0.371f;
    current.velocitySensitivity = 0.427f;
    current.outputGainDb = -6.0f;
    constexpr uint32_t seed = 0x4f12a973u;
    s3g::DrumRandom firstRandom(seed);
    s3g::DrumRandom secondRandom(seed);
    if (paramVector(s3g::drumFloorTomSafeRandomParams(current, firstRandom))
        != paramVector(s3g::drumFloorTomSafeRandomParams(
            current, secondRandom))) {
        std::cerr << "floor RANDOM was not deterministic\n";
        return false;
    }
    uint32_t state = 0x9e3779b9u;
    std::array<float, 26u> previous {};
    uint32_t distinct = 0u;
    for (uint32_t iteration = 0u; iteration < 256u; ++iteration) {
        const auto p = s3g::drumFloorTomSafeRandomParams(current, state);
        const auto values = paramVector(p);
        if (iteration > 0u && values != previous) ++distinct;
        previous = values;
        if (p.noteTracking != current.noteTracking
            || p.velocitySensitivity != current.velocitySensitivity
            || p.outputGainDb != current.outputGainDb
            || p.tuneHz < 55.0f || p.tuneHz > 108.0f
            || p.pitchDropSemitones < 0.0f
            || p.pitchDropSemitones > 12.0f
            || p.bodyDecaySeconds < 0.12f
            || p.bodyDecaySeconds > 1.40f
            || p.rimDecaySeconds < 0.035f
            || p.rimDecaySeconds > 0.105f
            || p.stereoWidth < 0.0f || p.stereoWidth > 0.90f
            || !std::all_of(values.begin(), values.end(),
                [](float value) { return std::isfinite(value); })) {
            std::cerr << "floor RANDOM range/ownership failed at "
                      << iteration << "\n";
            return false;
        }
        if (iteration < 48u) {
            for (auto articulation : { s3g::DrumTomArticulation::Head,
                    s3g::DrumTomArticulation::RimStick }) {
                const auto rendered = render(p, articulation, 43, 0.9f, 0.28);
                float peak = 0.0f;
                for (uint32_t frame = 0u; frame < rendered.left.size();
                     ++frame) {
                    if (!std::isfinite(rendered.left[frame])
                        || !std::isfinite(rendered.right[frame])) return false;
                    peak = std::max({ peak, std::abs(rendered.left[frame]),
                        std::abs(rendered.right[frame]) });
                }
                if (!(energy(rendered.left) > 1.0e-5) || peak >= 2.0f) {
                    std::cerr << "floor RANDOM render failed at "
                              << iteration << "\n";
                    return false;
                }
            }
        }
    }
    if (distinct != 255u) {
        std::cerr << "floor RANDOM lacked diversity\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!sanitationAndSilenceProbe()
        || !pitchArticulationAndStereoProbe()
        || !latchAndResetProbe()
        || !lowHorizonOnsetProbe()
        || !presetsAndRandomProbe()) {
        return 1;
    }
    std::cout << "drum floor tom smoke passed\n";
    return 0;
}
