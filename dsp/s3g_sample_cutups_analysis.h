#pragma once

#include "s3g_breakbeat_slicer.h"
#include "s3g_sample_cutups.h"
#include "s3g_sample_tempo_estimator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace s3g::sample {

// Offline analysis for Sample Cutups. This owns temporary vectors and must be
// called from a loader or state-service thread, never from the audio callback.
inline CutupsLaneMetadata analyzeCutupsAsset(const SampleAsset& asset,
    uint32_t maximumRegions = kMaximumCutupsRegions,
    double zeroCrossingRadiusMilliseconds = 5.0,
    uint32_t preTransientMicroseconds = 1000u,
    double minimumRegionMilliseconds = 20.0)
{
    CutupsLaneMetadata metadata;
    if (!asset.valid()) return metadata;
    maximumRegions = std::clamp<uint32_t>(maximumRegions, 1u,
        static_cast<uint32_t>(kMaximumCutupsRegions));
    zeroCrossingRadiusMilliseconds = std::clamp(
        zeroCrossingRadiusMilliseconds, 0.0, 100.0);
    minimumRegionMilliseconds = std::clamp(
        minimumRegionMilliseconds, 0.0, 10000.0);
    const uint32_t zeroRadius = static_cast<uint32_t>(std::clamp<double>(
        std::round(asset.sampleRate * zeroCrossingRadiusMilliseconds * 0.001),
        0.0, std::numeric_limits<uint32_t>::max()));
    const uint32_t minimumFrames = static_cast<uint32_t>(std::clamp<double>(
        std::round(asset.sampleRate * minimumRegionMilliseconds * 0.001),
        1.0, std::numeric_limits<uint32_t>::max()));
    const auto analysis = s3g::breakbeat::analyzeSample(asset);
    const auto slices = s3g::breakbeat::makeTransientSlices(asset, analysis,
        maximumRegions, zeroRadius, preTransientMicroseconds, minimumFrames);
    metadata.transientRegions.count = static_cast<uint32_t>(std::max<
        std::size_t>(1u, std::min<std::size_t>(slices.size(),
            metadata.transientRegions.starts.size())));
    metadata.transientRegions.starts[0u] = 0.0f;
    for (uint32_t index = 1u;
         index < metadata.transientRegions.count; ++index) {
        metadata.transientRegions.starts[index] = static_cast<float>(
            slices[index].startFrame)
            / static_cast<float>(asset.frameCount());
    }
    const auto tempo = estimateSampleTempo(asset);
    metadata.analyzedBpm = tempo.bpm;
    metadata.tempoConfidence = tempo.confidence;
    metadata.tempoValid = tempo.valid;
    metadata.tempoOctaveAmbiguous = tempo.octaveAmbiguous;
    return metadata;
}

} // namespace s3g::sample
