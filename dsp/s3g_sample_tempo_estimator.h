#pragma once

#include "s3g_sample_asset.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g::sample {

struct TempoEstimate {
    double bpm = 0.0;
    float confidence = 0.0f;
    bool valid = false;
    bool octaveAmbiguous = false;
};

// Offline source-tempo analysis for decoded sample assets. The estimator is
// intentionally platform-neutral and allocation-owning: callers must run it
// on a loader/analysis worker, never in the realtime audio callback.
inline TempoEstimate estimateSampleTempo(const SampleAsset& asset,
    double minimumBpm = 50.0, double maximumBpm = 220.0)
{
    TempoEstimate result;
    if (!asset.valid() || asset.channelCount == 0u
        || !(asset.sampleRate > 0.0)
        || !std::isfinite(minimumBpm) || !std::isfinite(maximumBpm)
        || minimumBpm < 20.0 || maximumBpm <= minimumBpm) return result;

    const uint32_t availableFrames = asset.frameCount();
    const uint32_t minimumFrames = static_cast<uint32_t>(std::min<double>(
        std::numeric_limits<uint32_t>::max(), asset.sampleRate * 2.0));
    if (availableFrames < minimumFrames) return result;

    constexpr double targetEnvelopeRate = 200.0;
    constexpr double maximumAnalysisSeconds = 180.0;
    const uint32_t hop = std::max<uint32_t>(1u,
        static_cast<uint32_t>(std::llround(
            asset.sampleRate / targetEnvelopeRate)));
    const double envelopeRate = asset.sampleRate
        / static_cast<double>(hop);
    const uint32_t analysisFrames = std::min<uint32_t>(availableFrames,
        static_cast<uint32_t>(std::min<double>(
            std::numeric_limits<uint32_t>::max(),
            asset.sampleRate * maximumAnalysisSeconds)));
    const std::size_t envelopeSize = std::max<std::size_t>(1u,
        (static_cast<std::size_t>(analysisFrames) + hop - 1u) / hop);
    if (envelopeSize < 8u) return result;

    std::vector<double> logEnergy(envelopeSize, 0.0);
    double maximumSignal = 0.0;
    for (std::size_t bin = 0u; bin < envelopeSize; ++bin) {
        const uint32_t first = static_cast<uint32_t>(bin * hop);
        const uint32_t last = std::min<uint32_t>(analysisFrames,
            first + hop);
        if (last <= first) continue;
        double strongestChannel = 0.0;
        for (uint8_t channel = 0u; channel < asset.channelCount; ++channel) {
            double sumSquares = 0.0;
            double peak = 0.0;
            const auto& samples = asset.channels[channel];
            for (uint32_t frame = first; frame < last; ++frame) {
                const double sample = std::isfinite(samples[frame])
                    ? static_cast<double>(samples[frame]) : 0.0;
                sumSquares += sample * sample;
                peak = std::max(peak, std::abs(sample));
            }
            const double rms = std::sqrt(sumSquares
                / static_cast<double>(last - first));
            strongestChannel = std::max(strongestChannel,
                rms * 0.75 + peak * 0.25);
            maximumSignal = std::max(maximumSignal, peak);
        }
        logEnergy[bin] = std::log1p(strongestChannel * 64.0);
    }
    if (maximumSignal < 1.0e-5) return result;

    std::vector<double> onset(envelopeSize, 0.0);
    for (std::size_t index = 1u; index < envelopeSize; ++index) {
        onset[index] = std::max(0.0,
            logEnergy[index] - logEnergy[index - 1u]);
    }
    // A short maximum/smoothing stage makes the score insensitive to a
    // transient straddling two analysis hops without smearing its beat phase.
    std::vector<double> smoothed(envelopeSize, 0.0);
    for (std::size_t index = 0u; index < envelopeSize; ++index) {
        const double previous = index > 0u ? onset[index - 1u] : 0.0;
        const double next = index + 1u < envelopeSize
            ? onset[index + 1u] : 0.0;
        smoothed[index] = std::max(onset[index],
            std::max(previous, next) * 0.55);
    }
    onset.swap(smoothed);

    double onsetSum = 0.0;
    double onsetSquares = 0.0;
    std::size_t activeOnsets = 0u;
    for (const double value : onset) {
        onsetSum += value;
        onsetSquares += value * value;
        if (value > 1.0e-5) ++activeOnsets;
    }
    if (activeOnsets < 4u || onsetSquares < 1.0e-9) return result;

    const double onsetMean = onsetSum / static_cast<double>(onset.size());
    std::vector<double> centered(onset.size(), 0.0);
    for (std::size_t index = 0u; index < onset.size(); ++index)
        centered[index] = onset[index] - onsetMean;

    const int minimumLag = std::max(2, static_cast<int>(std::floor(
        envelopeRate * 60.0 / maximumBpm)));
    const int maximumLag = std::min<int>(
        static_cast<int>(onset.size() / 2u),
        static_cast<int>(std::ceil(envelopeRate * 60.0 / minimumBpm)));
    if (maximumLag <= minimumLag + 2) return result;

    struct Candidate {
        double score = 0.0;
        double correlation = 0.0;
        double grid = 0.0;
    };
    std::vector<Candidate> candidates(
        static_cast<std::size_t>(maximumLag + 1));
    for (int lag = minimumLag; lag <= maximumLag; ++lag) {
        double cross = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        for (std::size_t index = static_cast<std::size_t>(lag);
             index < centered.size(); ++index) {
            const double a = centered[index];
            const double b = centered[index - static_cast<std::size_t>(lag)];
            cross += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(energyA * energyB);
        const double correlation = denominator > 1.0e-12
            ? std::clamp(cross / denominator, -1.0, 1.0) : 0.0;

        std::vector<double> phaseSums(static_cast<std::size_t>(lag), 0.0);
        for (std::size_t index = 0u; index < onset.size(); ++index)
            phaseSums[index % static_cast<std::size_t>(lag)] += onset[index];
        const double grid = onsetSum > 1.0e-12
            ? *std::max_element(phaseSums.begin(), phaseSums.end())
                / onsetSum : 0.0;
        const double bpm = envelopeRate * 60.0 / static_cast<double>(lag);
        const double octavesFrom120 = std::log2(bpm / 120.0);
        const double prior = std::exp(-0.5 * octavesFrom120
            * octavesFrom120 / (0.75 * 0.75));
        Candidate candidate;
        candidate.correlation = std::max(0.0, correlation);
        candidate.grid = std::clamp(grid, 0.0, 1.0);
        candidate.score = (candidate.correlation * 0.72
            + candidate.grid * 0.28) * (0.94 + prior * 0.06);
        candidates[static_cast<std::size_t>(lag)] = candidate;
    }

    int bestLag = minimumLag;
    for (int lag = minimumLag + 1; lag <= maximumLag; ++lag) {
        if (candidates[static_cast<std::size_t>(lag)].score
                > candidates[static_cast<std::size_t>(bestLag)].score)
            bestLag = lag;
    }
    const Candidate best = candidates[static_cast<std::size_t>(bestLag)];
    if (best.score < 0.15 || best.correlation < 0.08) return result;

    double refinedLag = static_cast<double>(bestLag);
    if (bestLag > minimumLag && bestLag < maximumLag) {
        const double left = candidates[static_cast<std::size_t>(
            bestLag - 1)].score;
        const double center = best.score;
        const double right = candidates[static_cast<std::size_t>(
            bestLag + 1)].score;
        const double curvature = left - 2.0 * center + right;
        if (std::abs(curvature) > 1.0e-12) {
            refinedLag += std::clamp(0.5 * (left - right) / curvature,
                -0.5, 0.5);
        }
    }

    double secondScore = 0.0;
    for (int lag = minimumLag; lag <= maximumLag; ++lag) {
        if (std::abs(lag - bestLag) <= 2) continue;
        const double ratio = static_cast<double>(lag)
            / static_cast<double>(bestLag);
        if (std::abs(ratio - 0.5) < 0.06
            || std::abs(ratio - 2.0) < 0.12) continue;
        secondScore = std::max(secondScore,
            candidates[static_cast<std::size_t>(lag)].score);
    }
    const double contrast = best.score > 1.0e-12
        ? std::clamp((best.score - secondScore) / best.score, 0.0, 1.0)
        : 0.0;
    const double confidence = std::clamp(best.correlation * 0.58
        + best.grid * 0.27 + contrast * 0.15, 0.0, 1.0);

    bool ambiguous = false;
    for (const int harmonicLag : { bestLag / 2, bestLag * 2 }) {
        if (harmonicLag < minimumLag || harmonicLag > maximumLag) continue;
        const double harmonic = candidates[static_cast<std::size_t>(
            harmonicLag)].score;
        if (harmonic >= best.score * 0.90) ambiguous = true;
    }

    result.bpm = envelopeRate * 60.0 / refinedLag;
    result.confidence = static_cast<float>(confidence);
    result.octaveAmbiguous = ambiguous;
    result.valid = std::isfinite(result.bpm)
        && result.bpm >= minimumBpm && result.bpm <= maximumBpm
        && confidence >= 0.25;
    if (!result.valid) result.bpm = 0.0;
    return result;
}

} // namespace s3g::sample
