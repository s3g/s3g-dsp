#include "s3g_drum_hi_hat.h"
#include "s3g_drum_hi_hat_presets.h"

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

Render render(const s3g::DrumHiHatParams& params,
    s3g::DrumHiHatArticulation articulation, int midiNote = -1,
    float velocity = 1.0f, double seconds = 1.0,
    double sampleRate = 48000.0)
{
    s3g::DrumHiHat hat;
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

std::array<float, 26u> paramVector(const s3g::DrumHiHatParams& p)
{
    return {{
        p.tuneHz, p.noteTracking, p.alloy, p.spread, p.density,
        p.tone, p.air, p.attack, p.closedDecaySeconds,
        p.openDecaySeconds, p.pedalDecaySeconds, p.wash, p.chick,
        p.chickTone, p.sizzle, p.chokeTimeMs,
        p.character.drive, p.character.bias, p.character.compression,
        p.character.sampleRateReduction, p.character.bitDepthReduction,
        p.character.reconstruction, p.character.tone, p.stereoWidth,
        p.velocitySensitivity, p.outputGainDb,
    }};
}

bool sanitationAndSilenceProbe()
{
    s3g::DrumHiHat hat;
    hat.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumHiHatParams invalid;
    invalid.tuneHz = std::numeric_limits<float>::infinity();
    invalid.noteTracking = -2.0f;
    invalid.closedDecaySeconds = -1.0f;
    invalid.openDecaySeconds = 99.0f;
    invalid.pedalDecaySeconds = std::numeric_limits<float>::quiet_NaN();
    invalid.chokeTimeMs = 1000.0f;
    invalid.outputGainDb = 100.0f;
    hat.setParams(invalid);
    const auto p = hat.params();
    if (p.tuneHz != 1180.0f || p.noteTracking != 0.0f
        || p.closedDecaySeconds != 0.03f
        || p.openDecaySeconds != 6.0f
        || p.pedalDecaySeconds != 0.30f
        || p.chokeTimeMs != 80.0f || p.outputGainDb != 12.0f) {
        std::cerr << "hi-hat sanitation failed\n";
        return false;
    }
    hat.reset();
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float left = 1.0f;
        float right = -1.0f;
        hat.processFrame(left, right);
        if (left != 0.0f || right != 0.0f) {
            std::cerr << "untriggered hi-hat was not silent\n";
            return false;
        }
    }

    s3g::DrumHiHatParams silent;
    silent.velocitySensitivity = 1.0f;
    hat.setParams(silent);
    hat.trigger(s3g::DrumHiHatArticulation::Closed, 0.0f);
    if (hat.active()) {
        std::cerr << "zero velocity activated hi-hat\n";
        return false;
    }
    return true;
}

bool articulationAndOnsetProbe()
{
    const s3g::DrumHiHatParams params;
    const auto closed = render(params,
        s3g::DrumHiHatArticulation::Closed, 42, 1.0f, 4.0);
    const auto open = render(params,
        s3g::DrumHiHatArticulation::Open, 46, 1.0f, 4.0);
    const auto pedal = render(params,
        s3g::DrumHiHatArticulation::Pedal, 44, 1.0f, 4.0);
    const double closedMs = energyTime90Ms(closed);
    const double openMs = energyTime90Ms(open);
    const double pedalMs = energyTime90Ms(pedal);
    std::cout << "hi-hat E90 ms closed/open/pedal: "
              << closedMs << " / " << openMs << " / " << pedalMs << "\n";
    if (!(closedMs >= 8.0 && closedMs <= 150.0)
        || !(openMs > closedMs * 2.2 && openMs < 1400.0)
        || !(pedalMs > 12.0 && pedalMs < 230.0)) {
        std::cerr << "hi-hat articulation timing failed\n";
        return false;
    }
    for (const Render* rendered : { &closed, &open, &pedal }) {
        const double peak = peakMagnitude(*rendered);
        const double step = maximumStep(*rendered);
        if (rendered->left[0] != 0.0f || rendered->right[0] != 0.0f
            || !std::isfinite(peak) || peak <= 1.0e-5 || peak > 4.0
            || step > std::max(0.42, peak * 0.98)) {
            std::cerr << "hi-hat onset continuity failed: peak " << peak
                      << ", step " << step << "\n";
            return false;
        }
    }
    return true;
}

bool trackingStereoAndVelocityProbe()
{
    s3g::DrumHiHatParams tracked;
    tracked.noteTracking = 1.0f;
    tracked.wash = 0.0f;
    tracked.chick = 0.0f;
    tracked.density = 0.0f;
    tracked.stereoWidth = 0.0f;
    tracked.outputGainDb = -12.0f;
    const auto low = render(tracked,
        s3g::DrumHiHatArticulation::Closed, 42, 1.0f, 0.4);
    const auto high = render(tracked,
        s3g::DrumHiHatArticulation::Closed, 54, 1.0f, 0.4);
    double lowDifference = 0.0;
    double highDifference = 0.0;
    for (uint32_t frame = 1u; frame < low.left.size(); ++frame) {
        lowDifference += std::abs(low.left[frame] - low.left[frame - 1u]);
        highDifference += std::abs(high.left[frame] - high.left[frame - 1u]);
    }
    if (!(highDifference > lowDifference * 1.15)) {
        std::cerr << "hi-hat note tracking did not raise brightness\n";
        return false;
    }
    if (low.left != low.right) {
        std::cerr << "hi-hat width zero was not exact mono\n";
        return false;
    }
    tracked.stereoWidth = 1.0f;
    tracked.wash = 0.72f;
    tracked.density = 0.88f;
    const auto wide = render(tracked,
        s3g::DrumHiHatArticulation::Open, 46, 1.0f, 0.8);
    if (!(differenceEnergy(wide) > stereoEnergy(wide) * 1.0e-5)) {
        std::cerr << "hi-hat width did not produce side energy\n";
        return false;
    }

    s3g::DrumHiHatParams velocityParams;
    velocityParams.velocitySensitivity = 1.0f;
    const auto quiet = render(velocityParams,
        s3g::DrumHiHatArticulation::Closed, 42, 0.2f, 0.5);
    const auto loud = render(velocityParams,
        s3g::DrumHiHatArticulation::Closed, 42, 1.0f, 0.5);
    if (!(stereoEnergy(loud) > stereoEnergy(quiet) * 5.0)) {
        std::cerr << "hi-hat velocity response failed\n";
        return false;
    }
    return true;
}

bool chokeProbe()
{
    s3g::DrumHiHatParams params;
    params.openDecaySeconds = 2.0f;
    params.closedDecaySeconds = 0.065f;
    params.pedalDecaySeconds = 0.080f;
    params.chokeTimeMs = 5.0f;
    params.character = {};

    const auto sequence = [&](s3g::DrumHiHatArticulation closer) {
        s3g::DrumHiHat hat;
        hat.prepare(48000.0);
        hat.setParams(params);
        hat.reset();
        Render result { std::vector<float>(48000u),
            std::vector<float>(48000u) };
        hat.trigger(s3g::DrumHiHatArticulation::Open, 1.0f, 46);
        for (uint32_t frame = 0u; frame < 48000u; ++frame) {
            if (frame == 4800u) hat.trigger(closer, 0.84f);
            hat.processFrame(result.left[frame], result.right[frame]);
        }
        return result;
    };
    const auto closedChoke = sequence(
        s3g::DrumHiHatArticulation::Closed);
    const auto pedalChoke = sequence(
        s3g::DrumHiHatArticulation::Pedal);
    const auto unchoked = render(params,
        s3g::DrumHiHatArticulation::Open, 46, 1.0f, 1.0);
    const double openLate = stereoEnergy(unchoked, 14400u, 28800u);
    if (!(stereoEnergy(closedChoke, 14400u, 28800u) < openLate * 0.08)
        || !(stereoEnergy(pedalChoke, 14400u, 28800u) < openLate * 0.08)) {
        std::cerr << "closed/pedal articulation did not choke open hat\n";
        return false;
    }
    const double chokeStep = std::max(
        std::abs(static_cast<double>(closedChoke.left[4800u])
            - closedChoke.left[4799u]),
        std::abs(static_cast<double>(pedalChoke.left[4800u])
            - pedalChoke.left[4799u]));
    if (chokeStep > 0.30) {
        std::cerr << "hi-hat choke introduced an edge: "
                  << chokeStep << "\n";
        return false;
    }
    return true;
}

bool latchResetAndLifecycleProbe()
{
    s3g::DrumHiHatParams initial;
    initial.openDecaySeconds = 0.8f;
    initial.wash = 0.72f;
    s3g::DrumHiHat reference;
    s3g::DrumHiHat edited;
    reference.prepare(48000.0);
    edited.prepare(48000.0);
    reference.setParams(initial);
    edited.setParams(initial);
    reference.trigger(s3g::DrumHiHatArticulation::Open, 0.84f, 46);
    edited.trigger(s3g::DrumHiHatArticulation::Open, 0.84f, 46);
    for (uint32_t frame = 0u; frame < 512u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) return false;
    }
    s3g::DrumHiHatParams changed = initial;
    changed.tuneHz = 2800.0f;
    changed.alloy = 0.05f;
    changed.openDecaySeconds = 4.0f;
    changed.wash = 0.02f;
    edited.setParams(changed);
    for (uint32_t frame = 0u; frame < 4096u; ++frame) {
        float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
        reference.processFrame(a, b);
        edited.processFrame(c, d);
        if (a != c || b != d) {
            std::cerr << "hi-hat per-hit controls were not latched\n";
            return false;
        }
    }
    edited.reset();
    float left = 1.0f;
    float right = -1.0f;
    edited.processFrame(left, right);
    if (left != 0.0f || right != 0.0f || edited.active()) {
        std::cerr << "hi-hat reset did not clear state\n";
        return false;
    }

    s3g::DrumHiHat lifetime;
    lifetime.prepare(48000.0);
    s3g::DrumHiHatParams shortHat;
    shortHat.closedDecaySeconds = 0.04f;
    shortHat.character = {};
    lifetime.setParams(shortHat);
    lifetime.trigger(s3g::DrumHiHatArticulation::Closed, 1.0f, 42);
    uint32_t frames = 0u;
    while (lifetime.active() && frames < 48000u * 4u) {
        float a = 0.0f, b = 0.0f;
        lifetime.processFrame(a, b);
        ++frames;
    }
    if (lifetime.active() || frames < 200u || frames >= 48000u * 4u) {
        std::cerr << "hi-hat lifecycle failed after " << frames
                  << " frames\n";
        return false;
    }
    return true;
}

bool presetAndRandomProbe()
{
    std::set<std::string> names;
    for (uint32_t index = 0u;
         index < s3g::kDrumHiHatFactoryPresetCount; ++index) {
        const auto& info = s3g::drumHiHatFactoryPresetInfo(index);
        if (!names.insert(info.name).second || !info.description
            || std::string(info.description).empty()) {
            std::cerr << "invalid or duplicate hi-hat preset metadata\n";
            return false;
        }
        const auto params = s3g::drumHiHatFactoryPreset(index);
        if (s3g::drumHiHatFactoryPresetIndex(params)
                != static_cast<int32_t>(index)) {
            std::cerr << "hi-hat preset lookup failed at " << index << "\n";
            return false;
        }
        for (const auto articulation : {
                 s3g::DrumHiHatArticulation::Closed,
                 s3g::DrumHiHatArticulation::Open,
                 s3g::DrumHiHatArticulation::Pedal }) {
            const auto hit = render(params, articulation, -1, 0.82f, 0.35);
            if (!(stereoEnergy(hit) > 1.0e-8)
                || !std::isfinite(peakMagnitude(hit))) {
                std::cerr << "silent/invalid hi-hat preset " << index << "\n";
                return false;
            }
        }
    }

    s3g::DrumHiHatParams current;
    current.noteTracking = 0.73f;
    current.velocitySensitivity = 0.44f;
    current.outputGainDb = -17.25f;
    uint32_t state = 0x7193a45bu;
    const auto baseline = paramVector(current);
    bool changed = false;
    for (uint32_t iteration = 0u; iteration < 128u; ++iteration) {
        const auto randomized = s3g::drumHiHatSafeRandomParams(
            current, state);
        const auto values = paramVector(randomized);
        changed = changed || values != baseline;
        if (randomized.noteTracking != current.noteTracking
            || randomized.velocitySensitivity
                != current.velocitySensitivity
            || randomized.outputGainDb != current.outputGainDb
            || randomized.closedDecaySeconds < 0.045f
            || randomized.closedDecaySeconds > 0.34f
            || randomized.openDecaySeconds < 0.30f
            || randomized.openDecaySeconds > 3.20f
            || randomized.pedalDecaySeconds < 0.075f
            || randomized.pedalDecaySeconds > 0.62f
            || randomized.chokeTimeMs < 1.2f
            || randomized.chokeTimeMs > 24.0f) {
            std::cerr << "hi-hat safe random violated its contract\n";
            return false;
        }
    }
    if (!changed) {
        std::cerr << "hi-hat safe random did not change timbre\n";
        return false;
    }
    return true;
}

bool stressProbe()
{
    for (double sampleRate : { 8000.0, 44100.0, 48000.0,
             96000.0, 192000.0, 768000.0 }) {
        s3g::DrumHiHat hat;
        hat.prepare(sampleRate);
        s3g::DrumHiHatParams params;
        params.tuneHz = 3200.0f;
        params.openDecaySeconds = 0.12f;
        params.character.drive = 1.0f;
        params.character.compression = 1.0f;
        params.character.sampleRateReduction = 1.0f;
        params.character.bitDepthReduction = 1.0f;
        params.character.reconstruction = 1.0f;
        params.outputGainDb = 12.0f;
        hat.setParams(params);
        for (uint32_t frame = 0u; frame < 8192u; ++frame) {
            if ((frame % 31u) == 0u) {
                hat.trigger(static_cast<s3g::DrumHiHatArticulation>(
                    (frame / 31u) % 3u), 1.0f,
                    42 + static_cast<int>((frame / 31u) % 24u));
            }
            float left = 0.0f;
            float right = 0.0f;
            hat.processFrame(left, right);
            if (!std::isfinite(left) || !std::isfinite(right)
                || std::abs(left) > 8.01f || std::abs(right) > 8.01f) {
                std::cerr << "hi-hat stress failed at " << sampleRate
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
    if (!chokeProbe()) return 1;
    if (!latchResetAndLifecycleProbe()) return 1;
    if (!presetAndRandomProbe()) return 1;
    if (!stressProbe()) return 1;
    std::cout << "hi-hat smoke passed\n";
    return 0;
}
