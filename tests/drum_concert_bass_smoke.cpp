#include "s3g_drum_concert_bass.h"
#include "s3g_drum_concert_bass_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct Render { std::vector<float> left, right; };

Render render(const s3g::DrumConcertBassParams& params,
    s3g::DrumConcertBassArticulation articulation,
    int note = 36, float velocity = 1.0f, double seconds = 1.0,
    double sampleRate = 48000.0)
{
    s3g::DrumConcertBass drum;
    drum.prepare(sampleRate);
    drum.setParams(params);
    drum.reset();
    drum.trigger(articulation, velocity, note);
    Render result {
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
    };
    drum.processBlock(result.left.data(), result.right.data(),
        static_cast<uint32_t>(result.left.size()));
    return result;
}

double energy(const std::vector<float>& x, uint32_t begin = 0u,
    uint32_t end = UINT32_MAX)
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(x.size()));
    double sum = 0.0;
    for (uint32_t i = std::min(begin, end); i < end; ++i) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return sum;
}

double peak(const Render& x)
{
    double result = 0.0;
    for (size_t i = 0; i < x.left.size(); ++i) {
        result = std::max({ result, std::abs(static_cast<double>(x.left[i])),
            std::abs(static_cast<double>(x.right[i])) });
    }
    return result;
}

double maximumStep(const Render& x)
{
    double result = 0.0, previousLeft = 0.0, previousRight = 0.0;
    for (size_t i = 0; i < x.left.size(); ++i) {
        result = std::max({ result,
            std::abs(static_cast<double>(x.left[i]) - previousLeft),
            std::abs(static_cast<double>(x.right[i]) - previousRight) });
        previousLeft = x.left[i];
        previousRight = x.right[i];
    }
    return result;
}

double maximumWindowRms(const std::vector<float>& x, uint32_t window,
    uint32_t hop)
{
    double maximum = 0.0;
    for (uint32_t begin = 0u; begin + window <= x.size(); begin += hop) {
        maximum = std::max(maximum, std::sqrt(energy(x, begin,
            begin + window) / static_cast<double>(window)));
    }
    return maximum;
}

uint32_t peakIndex(const std::vector<float>& x)
{
    uint32_t result = 0u;
    for (uint32_t index = 1u; index < x.size(); ++index) {
        if (std::abs(x[index]) > std::abs(x[result])) result = index;
    }
    return result;
}

double highpassEnergy(const std::vector<float>& x, double cutoffHz,
    uint32_t begin, uint32_t end, double sampleRate = 48000.0)
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(x.size()));
    const double coefficient = 1.0 - std::exp(
        -2.0 * static_cast<double>(s3g::kPi) * cutoffHz / sampleRate);
    double first = 0.0, second = 0.0, sum = 0.0;
    for (uint32_t index = 0u; index < end; ++index) {
        first += (static_cast<double>(x[index]) - first) * coefficient;
        second += (first - second) * coefficient;
        if (index >= begin) {
            const double high = static_cast<double>(x[index]) - second;
            sum += high * high;
        }
    }
    return sum;
}

// Lightweight offline crossover used to protect the perceptual design goal:
// pressure in the audible body band must outweigh infrasonic/fundamental-only
// energy. This is deliberately independent of peak level.
double bandEnergy(const std::vector<float>& x, double lowHz, double highHz,
    double sampleRate = 48000.0)
{
    const double lowCoefficient = 1.0 - std::exp(
        -2.0 * static_cast<double>(s3g::kPi) * lowHz / sampleRate);
    const double highCoefficient = 1.0 - std::exp(
        -2.0 * static_cast<double>(s3g::kPi) * highHz / sampleRate);
    double low = 0.0, high = 0.0, sum = 0.0;
    for (float sample : x) {
        low += (static_cast<double>(sample) - low) * lowCoefficient;
        high += (static_cast<double>(sample) - high) * highCoefficient;
        const double band = high - low;
        sum += band * band;
    }
    return sum;
}

double goertzel(const std::vector<float>& x, uint32_t begin,
    uint32_t end, double frequency)
{
    end = std::min<uint32_t>(end, static_cast<uint32_t>(x.size()));
    const double coefficient = 2.0 * std::cos(
        2.0 * static_cast<double>(s3g::kPi) * frequency / 48000.0);
    double one = 0.0, two = 0.0;
    for (uint32_t i = begin; i < end; ++i) {
        const double next = x[i] + coefficient * one - two;
        two = one;
        one = next;
    }
    return one * one + two * two - coefficient * one * two;
}

double dominantWindow(const std::vector<float>& x, uint32_t begin,
    uint32_t end, double low, double high)
{
    double bestHz = low, bestPower = -1.0;
    for (double hz = low; hz <= high; hz += 0.5) {
        const double power = goertzel(x, begin,
            std::min<uint32_t>(end, x.size()), hz);
        if (power > bestPower) { bestPower = power; bestHz = hz; }
    }
    return bestHz;
}

double dominant(const std::vector<float>& x, double low, double high)
{
    return dominantWindow(x, 6000u, 30000u, low, high);
}

std::array<float, 28u> values(const s3g::DrumConcertBassParams& p)
{
    return {{ p.tuneHz, p.noteTracking, p.size, p.headTension,
        p.strikePosition, p.beaterHardness, p.impact, p.body,
        p.bodyDecaySeconds, p.damping, p.bloom, p.air, p.shell,
        p.shellTone, p.mutedDecaySeconds, p.rimLevel, p.rimTone,
        p.rimDecaySeconds, p.character.drive, p.character.bias,
        p.character.compression, p.character.sampleRateReduction,
        p.character.bitDepthReduction, p.character.reconstruction,
        p.character.tone, p.stereoWidth, p.velocitySensitivity,
        p.outputGainDb }};
}

bool sanitationAndSilence()
{
    s3g::DrumConcertBass drum;
    drum.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumConcertBassParams p;
    p.tuneHz = std::numeric_limits<float>::infinity();
    p.noteTracking = -4.0f;
    p.bodyDecaySeconds = -1.0f;
    p.mutedDecaySeconds = 7.0f;
    p.rimDecaySeconds = std::numeric_limits<float>::quiet_NaN();
    p.outputGainDb = 99.0f;
    drum.setParams(p);
    p = drum.params();
    if (p.tuneHz != 44.0f || p.noteTracking != 0.0f
        || p.bodyDecaySeconds != 0.15f || p.mutedDecaySeconds != 1.5f
        || p.rimDecaySeconds != 0.14f || p.outputGainDb != 12.0f) {
        std::cerr << "concert bass sanitation failed\n";
        return false;
    }
    drum.reset();
    for (uint32_t i = 0u; i < 4096u; ++i) {
        float l = 1.0f, r = -1.0f;
        drum.processFrame(l, r);
        if (l != 0.0f || r != 0.0f) return false;
    }
    drum.trigger(s3g::DrumConcertBassArticulation::Center, 0.0f, 36);
    return !drum.active();
}

bool pitchArticulationStereoAndOnset()
{
    s3g::DrumConcertBassParams p;
    p.noteTracking = 1.0f;
    p.size = 0.05f;
    p.strikePosition = 0.0f;
    p.beaterHardness = 0.0f;
    p.impact = 0.0f;
    p.body = 0.0f;
    p.shell = 0.0f; p.air = 0.0f; p.bloom = 0.0f;
    p.bodyDecaySeconds = 2.5f; p.damping = 0.0f;
    p.outputGainDb = -12.0f;
    const auto low = render(p, s3g::DrumConcertBassArticulation::Center,
        36, 1.0f, 0.8);
    const auto high = render(p, s3g::DrumConcertBassArticulation::Center,
        48, 1.0f, 0.8);
    const double lowHz = dominant(low.left, 34.0, 58.0);
    const double highHz = dominant(high.left, 70.0, 104.0);
    // The stochastic pressure bands move with the tracked fundamental, but
    // their strongest random bin is not required to equal the tonal anchor.
    if (!(lowHz > 34.0 && lowHz < 58.0)
        || !(highHz / lowHz > 1.70 && highHz / lowHz < 2.60)) {
        std::cerr << "concert bass tracking failed " << lowHz << " / "
                  << highHz << "\n";
        return false;
    }

    const auto center = render({}, s3g::DrumConcertBassArticulation::Center,
        36, 0.9f, 1.1);
    const auto muted = render({}, s3g::DrumConcertBassArticulation::Muted,
        35, 0.9f, 1.1);
    const auto rim = render({}, s3g::DrumConcertBassArticulation::Rim,
        37, 0.9f, 1.1);
    if (!(energy(center.left) > 1.0e-6)
        || !(energy(muted.left) > 1.0e-6)
        || !(energy(rim.left) > 1.0e-6)
        || !(energy(muted.left, 24000u) < energy(center.left, 24000u) * 0.2)
        || !(energy(rim.left, 24000u) < energy(center.left, 24000u) * 0.2)) {
        std::cerr << "concert bass articulations were not distinct\n";
        return false;
    }

    const double centerPeak = peak(center);
    if (!(centerPeak > 0.60 && centerPeak < 0.86)) {
        std::cerr << "concert bass pressure bank was not big but bounded: "
                  << centerPeak << "\n";
        return false;
    }
    const uint32_t bodyWindow = std::min<uint32_t>(24000u,
        static_cast<uint32_t>(center.left.size()));
    const double bodyRms = std::sqrt(energy(center.left, 0u, bodyWindow)
        / static_cast<double>(bodyWindow));
    if (!(bodyRms > 0.14) || !(centerPeak / bodyRms < 4.5)) {
        std::cerr << "concert bass body remained peak-heavy: peak "
                  << centerPeak << ", rms " << bodyRms << "\n";
        return false;
    }
    const double firstTwentyRms = std::sqrt(energy(center.left, 0u, 960u)
        / 960.0);
    const double maximumTenRms = maximumWindowRms(center.left, 480u, 120u);
    const uint32_t maximumAt = peakIndex(center.left);
    if (!(firstTwentyRms > centerPeak * 0.35)
        || !(centerPeak / maximumTenRms < 2.15)
        || maximumAt < 240u || maximumAt > 3840u) {
        std::cerr << "concert bass pressure onset was sparse or peak-led: "
                  << firstTwentyRms << ", " << maximumTenRms << ", peak @ "
                  << maximumAt << "\n";
        return false;
    }
    const double earlyEnergy = energy(center.left, 0u, 3840u);
    const double lateEnergy = energy(center.left, 3840u, 24000u);
    const double earlyHighFraction = highpassEnergy(center.left, 250.0,
        0u, 3840u) / std::max(earlyEnergy, 1.0e-18);
    const double lateHighFraction = highpassEnergy(center.left, 250.0,
        3840u, 24000u) / std::max(lateEnergy, 1.0e-18);
    if (!(earlyHighFraction > 0.025)
        || !(lateHighFraction < earlyHighFraction * 0.75)) {
        std::cerr << "concert bass mallet spectrum did not clear into body: "
                  << earlyHighFraction << " -> " << lateHighFraction << "\n";
        return false;
    }
    const double subEnergy = bandEnergy(center.left, 23.0, 65.0);
    const double audibleBodyEnergy = bandEnergy(center.left, 65.0, 420.0);
    if (!(audibleBodyEnergy > subEnergy * 1.75)) {
        std::cerr << "concert bass radiated pressure lacked audible body: "
                  << audibleBodyEnergy << " / " << subEnergy << "\n";
        return false;
    }
    s3g::DrumConcertBassParams reduced;
    reduced.outputGainDb = -18.0f;
    const auto quiet = render(reduced,
        s3g::DrumConcertBassArticulation::Center, 36, 0.9f, 1.1);
    if (!(peak(quiet) < centerPeak * 0.30)) {
        std::cerr << "concert bass output trim lost authority\n";
        return false;
    }

    if (center.left[0] != 0.0f || center.right[0] != 0.0f
        || maximumStep(center) > peak(center) * 0.18 + 1.0e-8) {
        std::cerr << "concert bass onset was discontinuous: start "
                  << center.left[0] << ", step " << maximumStep(center)
                  << ", peak " << peak(center) << "\n";
        return false;
    }
    s3g::DrumConcertBassParams mono;
    mono.stereoWidth = 0.0f;
    const auto centered = render(mono,
        s3g::DrumConcertBassArticulation::Rim, 37, 0.9f, 0.5);
    if (centered.left != centered.right) return false;
    mono.stereoWidth = 1.0f;
    const auto wide = render(mono,
        s3g::DrumConcertBassArticulation::Rim, 37, 0.9f, 0.5);
    double difference = 0.0;
    for (size_t i = 0; i < wide.left.size(); ++i) {
        const double d = wide.left[i] - wide.right[i];
        difference += d * d;
    }
    return difference > 1.0e-8;
}

bool radiatedPressureFormGlideAndCleanLinearity()
{
    s3g::DrumConcertBassParams gliding;
    gliding.noteTracking = 0.0f;
    gliding.size = 0.05f;
    gliding.tuneHz = 44.0f;
    gliding.strikePosition = 0.0f;
    gliding.beaterHardness = 0.80f;
    gliding.impact = 1.0f;
    gliding.body = 0.0f;
    gliding.bodyDecaySeconds = 5.0f;
    gliding.damping = 0.0f;
    gliding.shell = 0.0f;
    gliding.outputGainDb = -18.0f;
    s3g::drum_concert_bass_detail::TonalAnchor anchor;
    anchor.configure(44.0f, std::exp2(118.0f / 1200.0f),
        5.0f, 0.14f, 1.0f, 48000.0f);
    std::vector<float> glide(72000u);
    for (float& sample : glide) sample = anchor.process();
    const double earlyHz = dominantWindow(glide,
        2048u, 8192u, 40.0, 52.0);
    const double settledHz = dominantWindow(glide,
        43200u, 62400u, 40.0, 52.0);
    const double earlyRestingPower = goertzel(glide,
        2048u, 8192u, 44.0);
    const double earlyRaisedPower = goertzel(glide,
        2048u, 8192u, 46.5);
    const double lateRestingPower = goertzel(glide,
        43200u, 62400u, 44.0);
    const double lateRaisedPower = goertzel(glide,
        43200u, 62400u, 46.5);
    if (!(earlyRaisedPower > earlyRestingPower * 1.04)
        || !(lateRestingPower > lateRaisedPower * 3.0)
        || !(settledHz > 43.0 && settledHz < 46.0)) {
        std::cerr << "concert bass tension relaxation failed: "
                  << earlyHz << " -> " << settledHz << " Hz\n";
        return false;
    }

    auto kettle = gliding;
    kettle.size = 0.05f;
    kettle.tuneHz = 68.0f;
    kettle.strikePosition = 0.70f;
    kettle.beaterHardness = 0.40f;
    kettle.impact = 0.50f;
    kettle.bloom = 0.30f;
    kettle.air = 0.50f;
    const auto kettleSound = render(kettle,
        s3g::DrumConcertBassArticulation::Center, 36, 1.0f, 1.0);
    const double first = dominantWindow(kettleSound.left,
        12000u, 36000u, 66.0, 70.0);
    const double second = dominantWindow(kettleSound.left,
        12000u, 36000u, 100.0, 105.0);
    const double third = dominantWindow(kettleSound.left,
        12000u, 36000u, 133.0, 140.0);
    if (!(second / first > 1.46 && second / first < 1.56)
        || !(third / first > 1.95 && third / first < 2.08)) {
        std::cerr << "concert bass kettle form lost harmonic modes: "
                  << first << ", " << second << ", " << third << " Hz\n";
        return false;
    }

    s3g::DrumConcertBassParams linear;
    linear.outputGainDb = -12.0f;
    const auto louder = render(linear,
        s3g::DrumConcertBassArticulation::Center, 36, 0.9f, 0.7);
    linear.outputGainDb = -18.0f;
    const auto quieter = render(linear,
        s3g::DrumConcertBassArticulation::Center, 36, 0.9f, 0.7);
    const double expected = std::pow(10.0, -6.0 / 20.0);
    double error = 0.0;
    for (size_t index = 0u; index < louder.left.size(); ++index) {
        const double delta = quieter.left[index]
            - louder.left[index] * expected;
        error += delta * delta;
    }
    if (!(error < energy(quieter.left) * 1.0e-10 + 1.0e-18)) {
        std::cerr << "concert bass clean output scaling became nonlinear\n";
        return false;
    }
    return true;
}

bool latchAndReset()
{
    s3g::DrumConcertBassParams first;
    first.bodyDecaySeconds = 2.8f;
    s3g::DrumConcertBass a, b;
    a.prepare(48000.0); b.prepare(48000.0);
    a.setParams(first); b.setParams(first);
    a.trigger(s3g::DrumConcertBassArticulation::Center, 0.8f, 36);
    b.trigger(s3g::DrumConcertBassArticulation::Center, 0.8f, 36);
    for (uint32_t i = 0u; i < 512u; ++i) {
        float al, ar, bl, br; a.processFrame(al, ar); b.processFrame(bl, br);
    }
    auto second = first;
    second.tuneHz = 88.0f; second.bodyDecaySeconds = 0.15f;
    second.shell = 1.0f; second.air = 0.0f;
    b.setParams(second);
    for (uint32_t i = 0u; i < 8192u; ++i) {
        float al, ar, bl, br; a.processFrame(al, ar); b.processFrame(bl, br);
        if (al != bl || ar != br) {
            std::cerr << "concert bass voice controls were not latched\n";
            return false;
        }
    }
    s3g::DrumConcertBass deterministic;
    deterministic.prepare(48000.0); deterministic.setParams(first);
    const auto capture = [&]() {
        std::array<float, 4096u> result {};
        deterministic.trigger(s3g::DrumConcertBassArticulation::Rim,
            0.7f, 37);
        for (float& sample : result) {
            float right; deterministic.processFrame(sample, right);
        }
        return result;
    };
    const auto before = capture();
    deterministic.reset();
    return before == capture();
}

bool presetsAndRandom()
{
    std::vector<std::string> names;
    for (uint32_t i = 0u; i < s3g::kDrumConcertBassFactoryPresetCount; ++i) {
        const auto& info = s3g::drumConcertBassFactoryPresetInfo(i);
        const auto preset = s3g::drumConcertBassFactoryPreset(i);
        if (!info.name || !info.description || info.name[0] == '\0'
            || std::find(names.begin(), names.end(), info.name) != names.end()
            || s3g::drumConcertBassFactoryPresetIndex(preset)
                != static_cast<int32_t>(i)) return false;
        names.emplace_back(info.name);
        for (auto articulation : {
                s3g::DrumConcertBassArticulation::Center,
                s3g::DrumConcertBassArticulation::Muted,
                s3g::DrumConcertBassArticulation::Rim }) {
            const auto sound = render(preset, articulation,
                articulation == s3g::DrumConcertBassArticulation::Center
                    ? 36 : (articulation
                        == s3g::DrumConcertBassArticulation::Muted ? 35 : 37),
                1.0f, 0.35);
            if (!(energy(sound.left) > 1.0e-8) || peak(sound) >= 0.92) {
                std::cerr << "concert bass preset render failed " << i << "\n";
                return false;
            }
        }
    }
    auto edited = s3g::drumConcertBassFactoryPreset(0u);
    edited.outputGainDb += 1.0f;
    if (s3g::drumConcertBassFactoryPresetIndex(edited) != 0) return false;
    edited.size += 0.01f;
    if (s3g::drumConcertBassFactoryPresetIndex(edited) != -1) return false;

    s3g::DrumConcertBassParams current;
    current.noteTracking = 0.371f;
    current.velocitySensitivity = 0.427f;
    current.outputGainDb = -13.0f;
    uint32_t state = 0x524f4f4du;
    std::array<float, 28u> previous {};
    uint32_t distinct = 0u;
    for (uint32_t i = 0u; i < 128u; ++i) {
        const auto randomized = s3g::drumConcertBassSafeRandomParams(
            current, state);
        const auto vector = values(randomized);
        if (i > 0u && vector != previous) ++distinct;
        previous = vector;
        if (randomized.noteTracking != current.noteTracking
            || randomized.velocitySensitivity != current.velocitySensitivity
            || randomized.outputGainDb != current.outputGainDb
            || randomized.tuneHz < 24.0f || randomized.tuneHz > 66.0f
            || randomized.bodyDecaySeconds < 0.35f
            || randomized.bodyDecaySeconds > 7.0f
            || !std::all_of(vector.begin(), vector.end(),
                [](float x) { return std::isfinite(x); })) return false;
        if (i < 24u) {
            const auto sound = render(randomized,
                s3g::DrumConcertBassArticulation::Center, 36, 0.9f, 0.35);
            if (!(energy(sound.left) > 1.0e-8) || peak(sound) >= 0.96) {
                return false;
            }
        }
    }
    return distinct == 127u;
}

bool overlappingStrikeCeiling()
{
    s3g::DrumConcertBassParams p;
    p.body = 1.0f;
    p.air = 1.0f;
    p.shell = 1.0f;
    p.impact = 1.0f;
    p.bodyDecaySeconds = 8.0f;
    p.damping = 0.0f;
    p.bloom = 1.0f;
    p.outputGainDb = 12.0f;
    s3g::DrumConcertBass drum;
    drum.prepare(48000.0);
    drum.setParams(p);
    double maximum = 0.0;
    for (uint32_t frame = 0u; frame < 48000u; ++frame) {
        if (frame < 12288u && frame % 512u == 0u) {
            drum.trigger(s3g::DrumConcertBassArticulation::Center,
                1.0f, 32 + static_cast<int>((frame / 512u) % 9u));
        }
        float left = 0.0f, right = 0.0f;
        drum.processFrame(left, right);
        if (!std::isfinite(left) || !std::isfinite(right)) {
            std::cerr << "concert bass overlap produced non-finite audio\n";
            return false;
        }
        maximum = std::max({ maximum, std::abs(static_cast<double>(left)),
            std::abs(static_cast<double>(right)) });
    }
    if (!(maximum > 0.90 && maximum <= 0.9601)) {
        std::cerr << "concert bass safety ceiling failed: " << maximum
                  << "\n";
        return false;
    }
    return true;
}

bool sampleRateStabilityAndTail()
{
    const auto reference = render({},
        s3g::DrumConcertBassArticulation::Center, 36, 1.0f, 0.5, 48000.0);
    const double referenceRms = std::sqrt(energy(reference.left)
        / static_cast<double>(reference.left.size()));
    for (double sampleRate : { 44100.0, 96000.0, 192000.0 }) {
        const auto sound = render({},
            s3g::DrumConcertBassArticulation::Center, 36, 1.0f, 0.5,
            sampleRate);
        if (!std::all_of(sound.left.begin(), sound.left.end(),
                [](float value) { return std::isfinite(value); })) {
            std::cerr << "concert bass non-finite at " << sampleRate << " Hz\n";
            return false;
        }
        const double rms = std::sqrt(energy(sound.left)
            / static_cast<double>(sound.left.size()));
        if (!(rms > referenceRms * 0.90 && rms < referenceRms * 1.10)
            || !(peak(sound) < 0.90)) {
            std::cerr << "concert bass sample-rate level drift at "
                      << sampleRate << " Hz: " << rms << " / "
                      << referenceRms << "\n";
            return false;
        }
    }
    s3g::DrumConcertBassParams tail;
    tail.bodyDecaySeconds = 0.15f;
    tail.damping = 1.0f;
    tail.mutedDecaySeconds = 1.5f;
    return s3g::drumConcertBassTailSeconds(tail) > 1.9;
}

bool pressurePaletteSafety()
{
    const s3g::DrumConcertBassParams params;
    const float outputGain = s3g::dbToGain(params.outputGainDb);
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 }) {
        for (uint64_t serial = 1u; serial <= 16u; ++serial) {
            s3g::drum_concert_bass_detail::Voice voice;
            voice.prepare(sampleRate);
            voice.trigger(params,
                s3g::DrumConcertBassArticulation::Center,
                1.0f, 36, static_cast<uint32_t>(serial % 12u), serial);
            const uint32_t frames = static_cast<uint32_t>(sampleRate * 0.5);
            const uint32_t firstHundred = static_cast<uint32_t>(
                sampleRate * 0.1);
            double maximum = 0.0, earlyEnergy = 0.0;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                float mid = 0.0f, side = 0.0f;
                voice.process(mid, side);
                const double left = (mid + side * params.stereoWidth)
                    * outputGain;
                const double right = (mid - side * params.stereoWidth)
                    * outputGain;
                maximum = std::max({ maximum, std::abs(left),
                    std::abs(right) });
                if (frame < firstHundred) earlyEnergy += left * left;
            }
            const double earlyRms = std::sqrt(earlyEnergy
                / static_cast<double>(firstHundred));
            if (!(maximum < 0.74) || !(earlyRms > 0.20)) {
                std::cerr << "concert bass pressure palette lost crest safety "
                          << sampleRate << " Hz seed " << serial << ": "
                          << maximum << ", rms " << earlyRms << "\n";
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!sanitationAndSilence() || !pitchArticulationStereoAndOnset()
        || !radiatedPressureFormGlideAndCleanLinearity() || !latchAndReset()
        || !presetsAndRandom()
        || !overlappingStrikeCeiling() || !sampleRateStabilityAndTail()
        || !pressurePaletteSafety()) {
        return 1;
    }
    std::cout << "drum concert bass smoke passed\n";
    return 0;
}
