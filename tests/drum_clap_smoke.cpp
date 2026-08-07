#include "s3g_drum_clap.h"
#include "s3g_drum_clap_presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace {

struct Render {
    std::vector<float> left;
    std::vector<float> right;
};

Render render(const s3g::DrumClapParams& params,
    s3g::DrumClapArticulation articulation, int midiNote = -1,
    float velocity = 1.0f, double seconds = 1.0,
    double sampleRate = 48000.0)
{
    s3g::DrumClap hat;
    hat.prepare(sampleRate);
    hat.setParams(params);
    hat.reset();
    hat.trigger(articulation, velocity, midiNote);
    Render result {
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
        std::vector<float>(static_cast<size_t>(sampleRate * seconds)),
    };
    hat.processBlock(result.left.data(), result.right.data(),
        static_cast<uint32_t>(result.left.size()));
    return result;
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

double stereoEnergy(const Render& rendered, uint32_t begin = 0u,
    uint32_t end = std::numeric_limits<uint32_t>::max())
{
    return energy(rendered.left, begin, end)
        + energy(rendered.right, begin, end);
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
    double result = 0.0;
    for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
        result = std::max({ result,
            std::abs(static_cast<double>(rendered.left[frame])),
            std::abs(static_cast<double>(rendered.right[frame])) });
    }
    return result;
}

double maximumStep(const Render& rendered)
{
    double result = 0.0;
    double previousLeft = 0.0;
    double previousRight = 0.0;
    for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
        result = std::max({ result,
            std::abs(static_cast<double>(rendered.left[frame])
                - previousLeft),
            std::abs(static_cast<double>(rendered.right[frame])
                - previousRight) });
        previousLeft = rendered.left[frame];
        previousRight = rendered.right[frame];
    }
    return result;
}

double energyTime90Ms(const Render& rendered, double sampleRate = 48000.0)
{
    const double total = stereoEnergy(rendered);
    if (!(total > 0.0)) return 0.0;
    double accumulated = 0.0;
    for (uint32_t frame = 0u; frame < rendered.left.size(); ++frame) {
        accumulated += static_cast<double>(rendered.left[frame])
                * rendered.left[frame]
            + static_cast<double>(rendered.right[frame])
                * rendered.right[frame];
        if (accumulated >= total * 0.90) {
            return 1000.0 * static_cast<double>(frame) / sampleRate;
        }
    }
    return 1000.0 * static_cast<double>(rendered.left.size()) / sampleRate;
}

std::array<float, 26u> paramVector(const s3g::DrumClapParams& p)
{
    return {{
        p.toneHz, p.noteTracking, p.hands, p.spreadMs, p.scatter,
        p.attack, p.bandwidth, p.air, p.burstDecaySeconds,
        p.tailDecaySeconds, p.tail, p.body, p.bodyTuneHz,
        p.bodyDecaySeconds, p.flamTimeMs, p.texture,
        p.character.drive, p.character.bias, p.character.compression,
        p.character.sampleRateReduction, p.character.bitDepthReduction,
        p.character.reconstruction, p.character.tone, p.stereoWidth,
        p.velocitySensitivity, p.outputGainDb,
    }};
}

bool sanitationAndSilenceProbe()
{
    s3g::DrumClap hat;
    hat.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumClapParams invalid;
    invalid.toneHz = std::numeric_limits<float>::infinity();
    invalid.noteTracking = -2.0f;
    invalid.hands = 99.0f;
    invalid.spreadMs = -1.0f;
    invalid.burstDecaySeconds = -1.0f;
    invalid.tailDecaySeconds = 99.0f;
    invalid.bodyDecaySeconds = std::numeric_limits<float>::quiet_NaN();
    invalid.flamTimeMs = 1000.0f;
    invalid.outputGainDb = 100.0f;
    hat.setParams(invalid);
    const auto p = hat.params();
    if (p.toneHz != 3900.0f || p.noteTracking != 0.0f
        || p.hands != 8.0f || p.spreadMs != 0.0f
        || p.burstDecaySeconds != 0.006f
        || p.tailDecaySeconds != 2.0f
        || p.bodyDecaySeconds != 0.075f
        || p.flamTimeMs != 120.0f || p.outputGainDb != 12.0f) {
        std::cerr << "clap sanitation failed\n";
        return false;
    }
    hat.reset();
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        hat.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "untriggered clap was not silent\n";
            return false;
        }
    }

    s3g::DrumClapParams silent;
    silent.velocitySensitivity = 1.0f;
    hat.setParams(silent);
    hat.trigger(s3g::DrumClapArticulation::Clap, 0.0f);
    if (hat.active()) {
        std::cerr << "zero velocity activated clap\n";
        return false;
    }
    return true;
}

bool articulationAndOnsetProbe()
{
    const s3g::DrumClapParams params;
    const auto clap = render(params,
        s3g::DrumClapArticulation::Clap, 39, 1.0f, 2.0);
    const auto flam = render(params,
        s3g::DrumClapArticulation::Flam, 40, 1.0f, 2.0);
    const auto tight = render(params,
        s3g::DrumClapArticulation::Tight, 41, 1.0f, 2.0);
    const double clapMs = energyTime90Ms(clap);
    const double flamMs = energyTime90Ms(flam);
    const double tightMs = energyTime90Ms(tight);
    std::cout << "clap E90 ms clap/flam/tight: "
              << clapMs << " / " << flamMs << " / " << tightMs << "\n";
    if (!(clapMs >= 18.0 && clapMs <= 220.0)
        || !(flamMs > clapMs * 1.05 && flamMs < 320.0)
        || !(tightMs > 5.0 && tightMs < clapMs * 0.90)) {
        std::cerr << "clap articulation timing failed\n";
        return false;
    }
    for (const Render* rendered : { &clap, &flam, &tight }) {
        const double peak = peakMagnitude(*rendered);
        const double step = maximumStep(*rendered);
        if (rendered->left[0] != 0.0f || rendered->right[0] != 0.0f
            || !std::isfinite(peak) || peak <= 1.0e-5 || peak > 4.0
            || step > std::max(0.42, peak * 0.98)) {
            std::cerr << "clap onset continuity failed: peak " << peak
                      << ", step " << step << "\n";
            return false;
        }
    }
    return true;
}

bool trackingStereoAndVelocityProbe()
{
    s3g::DrumClapParams tracked;
    tracked.noteTracking = 1.0f;
    tracked.tail = 0.0f;
    tracked.body = 0.0f;
    tracked.hands = 1.0f;
    tracked.spreadMs = 0.0f;
    tracked.stereoWidth = 0.0f;
    tracked.outputGainDb = -12.0f;
    const auto low = render(tracked,
        s3g::DrumClapArticulation::Clap, 39, 1.0f, 0.4);
    const auto high = render(tracked,
        s3g::DrumClapArticulation::Clap, 51, 1.0f, 0.4);
    double lowDifference = 0.0;
    double highDifference = 0.0;
    for (uint32_t frame = 1u; frame < low.left.size(); ++frame) {
        lowDifference += std::abs(low.left[frame] - low.left[frame - 1u]);
        highDifference += std::abs(high.left[frame] - high.left[frame - 1u]);
    }
    if (!(highDifference > lowDifference * 1.15)) {
        std::cerr << "clap note tracking did not raise brightness\n";
        return false;
    }
    if (low.left != low.right) {
        std::cerr << "clap width zero was not exact mono\n";
        return false;
    }
    tracked.stereoWidth = 1.0f;
    tracked.tail = 0.72f;
    tracked.hands = 7.0f;
    tracked.scatter = 0.88f;
    const auto wide = render(tracked,
        s3g::DrumClapArticulation::Flam, 40, 1.0f, 0.8);
    if (!(differenceEnergy(wide) > stereoEnergy(wide) * 1.0e-5)) {
        std::cerr << "clap width did not produce side energy\n";
        return false;
    }

    s3g::DrumClapParams velocityParams;
    velocityParams.velocitySensitivity = 1.0f;
    const auto quiet = render(velocityParams,
        s3g::DrumClapArticulation::Clap, 39, 0.2f, 0.5);
    const auto loud = render(velocityParams,
        s3g::DrumClapArticulation::Clap, 39, 1.0f, 0.5);
    if (!(stereoEnergy(loud) > stereoEnergy(quiet) * 5.0)) {
        std::cerr << "clap velocity response failed\n";
        return false;
    }
    return true;
}

bool burstStructureProbe()
{
    s3g::DrumClapParams params;
    params.hands = 6.0f;
    params.spreadMs = 42.0f;
    params.scatter = 0.0f;
    params.burstDecaySeconds = 0.012f;
    params.tail = 0.0f;
    params.body = 0.0f;
    params.flamTimeMs = 64.0f;
    params.character = {};
    const auto clap = render(params,
        s3g::DrumClapArticulation::Clap, 39, 1.0f, 0.25);
    const auto flam = render(params,
        s3g::DrumClapArticulation::Flam, 40, 1.0f, 0.25);
    const auto tight = render(params,
        s3g::DrumClapArticulation::Tight, 41, 1.0f, 0.25);
    const uint32_t flamGapBegin = static_cast<uint32_t>(0.035 * 48000.0);
    const uint32_t flamGapEnd = static_cast<uint32_t>(0.058 * 48000.0);
    const uint32_t secondClusterEnd = static_cast<uint32_t>(0.095 * 48000.0);
    if (!(stereoEnergy(flam, flamGapEnd, secondClusterEnd)
            > stereoEnergy(clap, flamGapEnd, secondClusterEnd) * 1.25)
        || !(stereoEnergy(tight, 2400u)
            < stereoEnergy(clap, 2400u) * 0.35)
        || !(stereoEnergy(flam, flamGapBegin, flamGapEnd)
            < stereoEnergy(flam, flamGapEnd, secondClusterEnd))) {
        std::cerr << "clap hand/flam/tight burst structure failed\n";
        return false;
    }
    return true;
}

bool latchResetAndLifecycleProbe()
{
    s3g::DrumClapParams initial;
    initial.tailDecaySeconds = 0.8f;
    initial.tail = 0.72f;
    s3g::DrumClap reference;
    s3g::DrumClap edited;
    reference.prepare(48000.0);
    edited.prepare(48000.0);
    reference.setParams(initial);
    edited.setParams(initial);
    reference.trigger(s3g::DrumClapArticulation::Flam, 0.84f, 40);
    edited.trigger(s3g::DrumClapArticulation::Flam, 0.84f, 40);
    for (uint32_t frame = 0u; frame < 512u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) return false;
    }
    s3g::DrumClapParams changed = initial;
    changed.toneHz = 8200.0f;
    changed.hands = 2.0f;
    changed.tailDecaySeconds = 1.8f;
    changed.tail = 0.02f;
    edited.setParams(changed);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) {
            std::cerr << "clap per-hit controls were not latched\n";
            return false;
        }
    }
    edited.reset();
    float left = 1.0f;
    float right = -1.0f;
    edited.processFrame(left, right);
    if (left != 0.0f || right != 0.0f || edited.active()) {
        std::cerr << "clap reset did not clear state\n";
        return false;
    }

    s3g::DrumClap lifetime;
    lifetime.prepare(48000.0);
    s3g::DrumClapParams shortClap;
    shortClap.burstDecaySeconds = 0.010f;
    shortClap.tailDecaySeconds = 0.04f;
    shortClap.character = {};
    lifetime.setParams(shortClap);
    lifetime.trigger(s3g::DrumClapArticulation::Clap, 1.0f, 39);
    uint32_t frames = 0u;
    while (lifetime.active() && frames < 48000u * 4u) {
        float a = 0.0f, b = 0.0f;
        lifetime.processFrame(a, b);
        ++frames;
    }
    if (lifetime.active() || frames < 200u || frames >= 48000u * 4u) {
        std::cerr << "clap lifecycle failed after " << frames
                  << " frames\n";
        return false;
    }
    return true;
}

bool presetAndRandomProbe()
{
    std::set<std::string> names;
    for (uint32_t index = 0u;
         index < s3g::kDrumClapFactoryPresetCount; ++index) {
        const auto& info = s3g::drumClapFactoryPresetInfo(index);
        if (!names.insert(info.name).second || !info.description
            || std::string(info.description).empty()) {
            std::cerr << "invalid or duplicate clap preset metadata\n";
            return false;
        }
        const auto params = s3g::drumClapFactoryPreset(index);
        if (s3g::drumClapFactoryPresetIndex(params)
                != static_cast<int32_t>(index)) {
            std::cerr << "clap preset lookup failed at " << index << "\n";
            return false;
        }
        for (const auto articulation : {
                 s3g::DrumClapArticulation::Clap,
                 s3g::DrumClapArticulation::Flam,
                 s3g::DrumClapArticulation::Tight }) {
            const auto hit = render(params, articulation, -1, 0.82f, 0.35);
            if (!(stereoEnergy(hit) > 1.0e-8)
                || !std::isfinite(peakMagnitude(hit))) {
                std::cerr << "silent/invalid clap preset " << index << "\n";
                return false;
            }
        }
    }

    s3g::DrumClapParams current;
    current.noteTracking = 0.73f;
    current.velocitySensitivity = 0.44f;
    current.outputGainDb = -17.25f;
    uint32_t state = 0x7193a45bu;
    const auto baseline = paramVector(current);
    bool changed = false;
    for (uint32_t iteration = 0u; iteration < 128u; ++iteration) {
        const auto randomized = s3g::drumClapSafeRandomParams(
            current, state);
        const auto values = paramVector(randomized);
        changed = changed || values != baseline;
        if (randomized.noteTracking != current.noteTracking
            || randomized.velocitySensitivity
                != current.velocitySensitivity
            || randomized.outputGainDb != current.outputGainDb
            || randomized.hands < 2.0f || randomized.hands > 8.0f
            || randomized.spreadMs < 5.0f
            || randomized.spreadMs > 58.0f
            || randomized.burstDecaySeconds < 0.009f
            || randomized.burstDecaySeconds > 0.070f
            || randomized.tailDecaySeconds < 0.045f
            || randomized.tailDecaySeconds > 0.90f
            || randomized.flamTimeMs < 14.0f
            || randomized.flamTimeMs > 78.0f) {
            std::cerr << "clap safe random violated its contract\n";
            return false;
        }
    }
    if (!changed) {
        std::cerr << "clap safe random did not change timbre\n";
        return false;
    }
    return true;
}

bool stressProbe()
{
    for (double sampleRate : { 8000.0, 44100.0, 48000.0,
             96000.0, 192000.0, 768000.0 }) {
        s3g::DrumClap hat;
        hat.prepare(sampleRate);
        s3g::DrumClapParams params;
        params.toneHz = 10000.0f;
        params.tailDecaySeconds = 0.025f;
        params.hands = 8.0f;
        params.spreadMs = 0.0f;
        params.character.drive = 1.0f;
        params.character.compression = 1.0f;
        params.character.sampleRateReduction = 1.0f;
        params.character.bitDepthReduction = 1.0f;
        params.character.reconstruction = 1.0f;
        params.outputGainDb = 12.0f;
        hat.setParams(params);
        for (uint32_t frame = 0u; frame < 8192u; ++frame) {
            if ((frame % 31u) == 0u) {
                hat.trigger(static_cast<s3g::DrumClapArticulation>(
                    (frame / 31u) % 3u), 1.0f,
                    39 + static_cast<int>((frame / 31u) % 24u));
            }
            float left = 0.0f;
            float right = 0.0f;
            hat.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)
                || std::abs(left) > 8.01f || std::abs(right) > 8.01f) {
                std::cerr << "clap stress failed at " << sampleRate
                          << " Hz\n";
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!sanitationAndSilenceProbe()) return 1;
    if (!articulationAndOnsetProbe()) return 1;
    if (!trackingStereoAndVelocityProbe()) return 1;
    if (!burstStructureProbe()) return 1;
    if (!latchResetAndLifecycleProbe()) return 1;
    if (!presetAndRandomProbe()) return 1;
    if (!stressProbe()) return 1;
    std::cout << "clap smoke passed\n";
    return 0;
}
