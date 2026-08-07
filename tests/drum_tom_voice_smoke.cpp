#include "s3g_drum_tom_voice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct Render {
    std::vector<float> mid;
    std::vector<float> side;
};

Render render(const s3g::DrumTomVoiceSettings& settings,
    s3g::DrumTomArticulation articulation, double seconds = 0.5,
    double sampleRate = 48000.0, float velocity = 1.0f)
{
    s3g::DrumTomVoiceBank<8u> bank;
    bank.prepare(sampleRate);
    bank.trigger(settings, articulation, velocity);
    Render result {
        std::vector<float>(static_cast<size_t>(seconds * sampleRate)),
        std::vector<float>(static_cast<size_t>(seconds * sampleRate)),
    };
    for (uint32_t frame = 0u; frame < result.mid.size(); ++frame) {
        bank.processFrame(result.mid[frame], result.side[frame]);
    }
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

double firstDifferenceBrightness(const std::vector<float>& signal,
    uint32_t frames)
{
    frames = std::min<uint32_t>(frames,
        static_cast<uint32_t>(signal.size()));
    double differences = 0.0;
    double total = 0.0;
    for (uint32_t frame = 1u; frame < frames; ++frame) {
        const double delta = static_cast<double>(signal[frame])
            - signal[frame - 1u];
        differences += delta * delta;
        total += static_cast<double>(signal[frame]) * signal[frame];
    }
    return differences / std::max(total, 1.0e-20);
}

double peakMagnitude(const std::vector<float>& signal,
    uint32_t frames = std::numeric_limits<uint32_t>::max())
{
    frames = std::min<uint32_t>(frames,
        static_cast<uint32_t>(signal.size()));
    double peak = 0.0;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        peak = std::max(peak,
            std::abs(static_cast<double>(signal[frame])));
    }
    return peak;
}

double maximumStep(const std::vector<float>& signal,
    uint32_t frames = std::numeric_limits<uint32_t>::max())
{
    frames = std::min<uint32_t>(frames,
        static_cast<uint32_t>(signal.size()));
    double maximum = 0.0;
    double previous = 0.0;
    for (uint32_t frame = 0u; frame < frames; ++frame) {
        const double current = signal[frame];
        maximum = std::max(maximum, std::abs(current - previous));
        previous = current;
    }
    return maximum;
}

uint32_t energyPercentileFrame(const std::vector<float>& signal,
    double fraction)
{
    const double total = energy(signal);
    double running = 0.0;
    for (uint32_t frame = 0u; frame < signal.size(); ++frame) {
        running += static_cast<double>(signal[frame]) * signal[frame];
        if (running >= total * fraction) return frame;
    }
    return static_cast<uint32_t>(signal.size());
}

bool articulationProbe()
{
    s3g::DrumTomVoiceSettings settings;
    settings.frequencyHz = 72.0f;
    settings.bodyDecaySeconds = 0.70f;
    settings.rimDecaySeconds = 0.070f;
    settings.rimLevel = 0.80f;
    settings.stickLevel = 0.60f;
    settings.stickTone = 0.70f;
    const auto head = render(settings, s3g::DrumTomArticulation::Head);
    const auto rim = render(settings, s3g::DrumTomArticulation::RimStick);
    const double headEnergy = energy(head.mid);
    const double rimEnergy = energy(rim.mid);
    const double headBrightness = firstDifferenceBrightness(head.mid, 4800u);
    const double rimBrightness = firstDifferenceBrightness(rim.mid, 4800u);
    const double headLate = energy(head.mid, 7200u);
    const double rimLate = energy(rim.mid, 7200u);
    const double rimE90Ms = static_cast<double>(
        energyPercentileFrame(rim.mid, 0.90)) / 48.0;
    if (!(headEnergy > 0.01) || !(rimEnergy > 0.01)
        || !(rimBrightness > headBrightness * 1.15)
        || !(rimLate < headLate * 0.20 + 1.0e-8)
        || !(rimE90Ms >= 2.0 && rimE90Ms <= 45.0)) {
        std::cerr << "tom articulation contract failed: energy "
                  << headEnergy << " / " << rimEnergy << ", brightness "
                  << headBrightness << " / " << rimBrightness
                  << ", late " << headLate << " / " << rimLate
                  << ", rim E90 " << rimE90Ms << " ms\n";
        return false;
    }

    auto dark = settings;
    dark.rimCharacter = 0.0f;
    dark.stickTone = 0.05f;
    auto bright = settings;
    bright.rimCharacter = 1.0f;
    bright.stickTone = 0.95f;
    const auto darkRim = render(dark, s3g::DrumTomArticulation::RimStick);
    const auto brightRim = render(bright, s3g::DrumTomArticulation::RimStick);
    if (!(firstDifferenceBrightness(brightRim.mid, 4800u)
            > firstDifferenceBrightness(darkRim.mid, 4800u) * 1.20)) {
        std::cerr << "rim character did not traverse dark-to-bright\n";
        return false;
    }
    std::cout << "tom voice rim E90=" << rimE90Ms << " ms\n";
    return true;
}

bool lifecycleAndSafetyProbe()
{
    s3g::DrumTomVoiceBank<4u> bank;
    bank.prepare(std::numeric_limits<double>::quiet_NaN());
    s3g::DrumTomVoiceSettings invalid;
    invalid.frequencyHz = std::numeric_limits<float>::infinity();
    invalid.bodyDecaySeconds = -4.0f;
    invalid.rimCharacter = std::numeric_limits<float>::quiet_NaN();
    invalid.velocitySensitivity = 1.0f;
    if (bank.trigger(invalid, s3g::DrumTomArticulation::Head, 0.0f)
        || bank.active()) {
        std::cerr << "zero-velocity tom allocated a voice\n";
        return false;
    }
    invalid.velocitySensitivity = 0.8f;
    for (uint32_t hit = 0u; hit < 11u; ++hit) {
        invalid.frequencyHz = 45.0f + hit * 17.0f;
        bank.trigger(invalid,
            (hit & 1u) != 0u ? s3g::DrumTomArticulation::RimStick
                             : s3g::DrumTomArticulation::Head,
            0.4f + hit * 0.04f);
    }
    if (!bank.active() || bank.activeVoiceCount() > 4u) {
        std::cerr << "tom voice stealing/pool bound failed\n";
        return false;
    }
    double outputEnergy = 0.0;
    for (uint32_t frame = 0u; frame < 500000u; ++frame) {
        float mid = 0.0f;
        float side = 0.0f;
        bank.processFrame(mid, side);
        if (!std::isfinite(mid) || !std::isfinite(side)
            || std::abs(mid) > 16.0f || std::abs(side) > 16.0f) {
            std::cerr << "tom voice safety failed at " << frame << "\n";
            return false;
        }
        outputEnergy += static_cast<double>(mid) * mid;
    }
    if (!(outputEnergy > 0.01) || bank.active()) {
        std::cerr << "tom bank did not finish its bounded lifecycle\n";
        return false;
    }
    return true;
}

bool deterministicResetProbe()
{
    s3g::DrumTomVoiceSettings settings;
    settings.stereoPosition = -0.6f;
    s3g::DrumTomVoiceBank<4u> bank;
    bank.prepare(48000.0);
    const auto capture = [&]() {
        std::array<float, 4096u> signal {};
        bank.trigger(settings, s3g::DrumTomArticulation::Head, 0.83f);
        for (float& sample : signal) {
            float side = 0.0f;
            bank.processFrame(sample, side);
            sample += side * 0.37f;
        }
        return signal;
    };
    const auto first = capture();
    bank.reset();
    const auto second = capture();
    if (first != second) {
        std::cerr << "tom bank reset was not deterministic\n";
        return false;
    }
    return true;
}

bool headContinuityProbe()
{
    // LOW HORIZON exposed a randomized sample-zero shell displacement. Sweep
    // enough salts and rates to cover both reinforcing and cancelling modal
    // polarities instead of accepting one unusually benign first render.
    constexpr std::array<double, 3u> sampleRates {{
        44100.0, 48000.0, 96000.0,
    }};
    for (const double sampleRate : sampleRates) {
        for (uint32_t seed = 0u; seed < 64u; ++seed) {
            s3g::DrumTomVoiceSettings settings;
            settings.frequencyHz = 58.0f;
            settings.pitchDropSemitones = 5.5f;
            settings.pitchSweepMs = 92.0f;
            settings.shellSpread = 0.50f;
            settings.body = 0.84f;
            settings.ring = 0.54f;
            settings.bodyDecaySeconds = 1.40f;
            settings.punch = 0.46f;
            settings.damping = 0.18f;
            settings.stereoPosition = 0.65f;
            settings.seedSalt = 0x4c4f5700u
                ^ seed * 0x9e3779b9u;
            const auto head = render(settings,
                s3g::DrumTomArticulation::Head,
                0.10, sampleRate);
            const double peak = peakMagnitude(head.mid);
            const double step = maximumStep(head.mid);
            const uint32_t firstMillisecond = static_cast<uint32_t>(
                std::ceil(sampleRate * 0.001));
            const double totalEnergy = energy(head.mid)
                + energy(head.side);
            const double earlyEnergyFraction = (
                    energy(head.mid, 0u, firstMillisecond)
                    + energy(head.side, 0u, firstMillisecond))
                / std::max(1.0e-20, totalEnergy);
            if (head.mid[0] != 0.0f || head.side[0] != 0.0f
                || !(peak > 1.0e-4)
                || step > peak * 0.04 + 1.0e-8
                || earlyEnergyFraction > 0.005) {
                std::cerr << "head onset continuity failed at "
                          << sampleRate << " Hz, seed " << seed
                          << ": start " << head.mid[0] << " / "
                          << head.side[0] << ", step/peak "
                          << step << " / " << peak << ", E1 "
                          << earlyEnergyFraction << "\n";
                return false;
            }
        }
    }

    s3g::DrumTomVoiceSettings sustaining;
    sustaining.frequencyHz = 74.0f;
    sustaining.stereoPosition = -0.7f;
    s3g::DrumTomVoiceBank<4u> withoutHead;
    withoutHead.prepare(48000.0);
    withoutHead.trigger(sustaining,
        s3g::DrumTomArticulation::Head, 0.78f);
    for (uint32_t frame = 0u; frame < 257u; ++frame) {
        float mid = 0.0f;
        float side = 0.0f;
        withoutHead.processFrame(mid, side);
    }
    auto withHead = withoutHead;
    s3g::DrumTomVoiceSettings added = sustaining;
    added.frequencyHz = 58.0f;
    added.punch = 0.46f;
    added.stereoPosition = 0.8f;
    withHead.trigger(added, s3g::DrumTomArticulation::Head, 1.0f);
    float baselineMid = 0.0f;
    float baselineSide = 0.0f;
    float triggeredMid = 0.0f;
    float triggeredSide = 0.0f;
    withoutHead.processFrame(baselineMid, baselineSide);
    withHead.processFrame(triggeredMid, triggeredSide);
    if (triggeredMid != baselineMid || triggeredSide != baselineSide) {
        std::cerr << "head retrigger introduced a trigger-frame step: "
                  << baselineMid << " / " << triggeredMid << ", "
                  << baselineSide << " / " << triggeredSide << "\n";
        return false;
    }
    return true;
}

bool repeatedHeadContinuityProbe()
{
    // The voice RNG also incorporates the monotonically increasing trigger
    // index. Exercise the exact Floor Tom salt far enough to reach uncommon
    // reinforcing combinations that a reset-per-render test cannot cover.
    s3g::DrumTomVoiceSettings settings;
    settings.frequencyHz = 58.0f;
    settings.pitchDropSemitones = 5.5f;
    settings.pitchSweepMs = 92.0f;
    settings.shellSpread = 0.50f;
    settings.body = 0.84f;
    settings.ring = 0.54f;
    settings.bodyDecaySeconds = 1.40f;
    settings.punch = 0.46f;
    settings.damping = 0.18f;
    settings.stickLevel = 0.18f;
    settings.seedSalt = 0x464c4f52u; // "FLOR"

    constexpr std::array<double, 3u> sampleRates {{
        44100.0, 48000.0, 96000.0,
    }};
    for (const double sampleRate : sampleRates) {
        s3g::DrumTomVoiceBank<1u> bank;
        bank.prepare(sampleRate);
        const uint32_t frames = static_cast<uint32_t>(
            std::ceil(sampleRate * 0.10));
        const uint32_t firstMillisecond = static_cast<uint32_t>(
            std::ceil(sampleRate * 0.001));
        for (uint32_t hit = 0u; hit < 256u; ++hit) {
            bank.trigger(settings, s3g::DrumTomArticulation::Head, 1.0f);
            double peak = 0.0;
            double step = 0.0;
            double totalEnergy = 0.0;
            double earlyEnergy = 0.0;
            double previousMid = 0.0;
            double previousSide = 0.0;
            float firstMid = 0.0f;
            float firstSide = 0.0f;
            for (uint32_t frame = 0u; frame < frames; ++frame) {
                float mid = 0.0f;
                float side = 0.0f;
                bank.processFrame(mid, side);
                if (frame == 0u) {
                    firstMid = mid;
                    firstSide = side;
                }
                peak = std::max({ peak,
                    std::abs(static_cast<double>(mid)),
                    std::abs(static_cast<double>(side)) });
                step = std::max({ step,
                    std::abs(static_cast<double>(mid) - previousMid),
                    std::abs(static_cast<double>(side) - previousSide) });
                previousMid = mid;
                previousSide = side;
                const double frameEnergy = static_cast<double>(mid) * mid
                    + static_cast<double>(side) * side;
                totalEnergy += frameEnergy;
                if (frame < firstMillisecond) {
                    earlyEnergy += frameEnergy;
                }
            }
            const double scaledStepRatio = step
                / std::max(peak, 1.0e-20) * sampleRate / 48000.0;
            const double earlyEnergyFraction = earlyEnergy
                / std::max(totalEnergy, 1.0e-20);
            if (firstMid != 0.0f || firstSide != 0.0f
                || !(peak > 0.80)
                || scaledStepRatio > 0.04
                || earlyEnergyFraction > 0.005) {
                std::cerr << "repeated LOW HORIZON onset failed at "
                          << sampleRate << " Hz, hit " << hit
                          << ": start " << firstMid << " / "
                          << firstSide << ", scaled step/peak "
                          << scaledStepRatio << ", peak " << peak
                          << ", E1 " << earlyEnergyFraction << "\n";
                return false;
            }
        }
    }
    return true;
}

bool rimContinuityProbe()
{
    constexpr std::array<double, 3u> sampleRates {{
        44100.0, 48000.0, 96000.0,
    }};
    constexpr std::array<float, 3u> characterValues {{0.0f, 0.5f, 1.0f}};
    for (const double sampleRate : sampleRates) {
        for (const float character : characterValues) {
            for (uint32_t seed = 0u; seed < 16u; ++seed) {
                s3g::DrumTomVoiceSettings settings;
                settings.frequencyHz = 52.0f + character * 123.0f;
                settings.rimLevel = 0.90f;
                settings.rimCharacter = character;
                settings.stickLevel = 0.82f;
                settings.stickTone = character;
                settings.stereoPosition = 0.65f;
                settings.seedSalt = 0x52494d00u + seed * 0x9e37u;
                const auto rim = render(settings,
                    s3g::DrumTomArticulation::RimStick,
                    0.05, sampleRate);
                const double midPeak = peakMagnitude(rim.mid);
                const double sidePeak = peakMagnitude(rim.side);
                const double firstFourMid = peakMagnitude(rim.mid, 4u);
                const double firstFourSide = peakMagnitude(rim.side, 4u);
                const uint32_t firstMillisecond = static_cast<uint32_t>(
                    std::ceil(sampleRate * 0.001));
                const double totalEnergy = energy(rim.mid)
                    + energy(rim.side);
                const double earlyEnergyFraction = (
                        energy(rim.mid, 0u, firstMillisecond)
                        + energy(rim.side, 0u, firstMillisecond))
                    / std::max(1.0e-20, totalEnergy);
                const double earlyEnergyLimit = character > 0.75f
                    ? 0.18 : 0.08;
                if (rim.mid[0] != 0.0f || rim.side[0] != 0.0f
                    || !(midPeak > 1.0e-4) || !(sidePeak > 1.0e-6)
                    || firstFourMid > midPeak * 0.15 + 1.0e-8
                    || firstFourSide > sidePeak * 0.15 + 1.0e-8
                    || earlyEnergyFraction > earlyEnergyLimit) {
                    std::cerr << "rim onset continuity failed at "
                              << sampleRate << " Hz, character "
                              << character << ", seed " << seed
                              << ": start " << rim.mid[0] << " / "
                              << rim.side[0] << ", first4 "
                              << firstFourMid << " / " << firstFourSide
                              << ", peak " << midPeak << " / "
                              << sidePeak << ", E1 "
                              << earlyEnergyFraction << "\n";
                    return false;
                }
            }
        }
    }

    s3g::DrumTomVoiceSettings sustaining;
    sustaining.frequencyHz = 86.0f;
    sustaining.stereoPosition = -0.7f;
    s3g::DrumTomVoiceBank<4u> withoutRim;
    withoutRim.prepare(48000.0);
    withoutRim.trigger(sustaining, s3g::DrumTomArticulation::Head, 0.78f);
    for (uint32_t frame = 0u; frame < 257u; ++frame) {
        float mid = 0.0f;
        float side = 0.0f;
        withoutRim.processFrame(mid, side);
    }
    auto withRim = withoutRim;
    s3g::DrumTomVoiceSettings rim = sustaining;
    rim.rimCharacter = 1.0f;
    rim.stickTone = 1.0f;
    rim.stereoPosition = 0.8f;
    withRim.trigger(rim, s3g::DrumTomArticulation::RimStick, 1.0f);
    float baselineMid = 0.0f;
    float baselineSide = 0.0f;
    float triggeredMid = 0.0f;
    float triggeredSide = 0.0f;
    withoutRim.processFrame(baselineMid, baselineSide);
    withRim.processFrame(triggeredMid, triggeredSide);
    if (triggeredMid != baselineMid || triggeredSide != baselineSide) {
        std::cerr << "rim retrigger introduced a trigger-frame step: "
                  << baselineMid << " / " << triggeredMid << ", "
                  << baselineSide << " / " << triggeredSide << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!articulationProbe()
        || !lifecycleAndSafetyProbe()
        || !deterministicResetProbe()
        || !headContinuityProbe()
        || !repeatedHeadContinuityProbe()
        || !rimContinuityProbe()) {
        return 1;
    }
    std::cout << "drum tom voice smoke passed\n";
    return 0;
}
