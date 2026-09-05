#pragma once

#include "s3g/tracker/sequencer.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace s3g::tracker {

enum class PatternReshapeWriteMode {
    ExistingOnly,
    FillMissing,
};

struct PatternReshapeSettings {
    // Bit N includes tracker lane N in analysis, preview, and apply. Tracker
    // currently supports at most 32 lanes, so one mask also represents an
    // arbitrary user-selected lane group without realtime allocation.
    uint32_t laneMask = 0xffffffffu;
    // Zero asks the analyzer to select a useful cycle from 4/8/16/32/64.
    std::size_t cycleRows = 0u;
    // Timing controls are normalized for UI and command use.
    float pocket = 0.75f;       // 0..1 blend toward rhythm-derived contour
    float tighten = 0.40f;      // 0..1 pull signed MT toward the grid
    float timingDepth = 0.0f;   // -1..1 flatten/exaggerate that contour
    // Dynamics controls use 1.0 as identity.
    float velocityRange = 1.0f; // 0..2 spread around each lane median
    float accentDepth = 1.0f;   // 0..2 phase-accent strength
    float laneBalance = 0.0f;   // 0..1 pull lane centers together
    // Rhythm mutation is deterministic and remains disabled at zero amount.
    // Positive density adds hits; negative density removes weaker hits.
    float mutationAmount = 0.0f; // 0..1 master probability/depth
    float densityChange = 0.0f;  // -1..1
    float syncopation = 0.0f;    // -1 strong-grid, +1 off-grid
    uint32_t displacementRows = 0u; // 0..4 maximum move
    float burstChance = 0.0f;    // 0..1 conversion probability
    AssetBankId burstBankId = kProjectAssetBankId;
    float cycleDrift = 0.0f;     // 0..1 chance to choose nearby 4..64 cycle
    uint64_t mutationSeed = 1u;
    std::vector<uint8_t> laneDefaultNotes;
    // Write modes are explicit because Reshape must never silently occupy a
    // user's SEQ cells or materialize an independent velocity cycle.
    PatternReshapeWriteMode microTimingWrite =
        PatternReshapeWriteMode::ExistingOnly;
    PatternReshapeWriteMode velocityWrite =
        PatternReshapeWriteMode::ExistingOnly;
    // Zero disables limiting; otherwise each is a MAD multiplier.
    float timingOutlierThreshold = 0.0f;
    float velocityOutlierThreshold = 0.0f;
};

struct PatternReshapeLaneAnalysis {
    std::size_t noteEvents = 0u;
    std::size_t timingValues = 0u;
    std::size_t velocityValues = 0u;
    float timingMedian = 0.0f; // centered MT value, -1..1
    float timingMad = 0.0f;
    float velocityMedian = 0.787f;
    float velocityMad = 0.0f;
};

struct PatternReshapeAnalysis {
    std::size_t rows = 1u;
    std::size_t cycleRows = 16u;
    std::size_t passes = 1u;
    std::size_t noteEvents = 0u;
    std::size_t timingValues = 0u;
    std::size_t velocityValues = 0u;
    std::size_t unsupportedTimingValues = 0u;
    float timingMedian = 0.0f;
    float timingMad = 0.0f;
    float velocityMedian = 0.787f;
    float velocityMad = 0.0f;
    float confidence = 0.0f;
    std::vector<PatternReshapeLaneAnalysis> lanes;
    std::vector<float> phaseTimingMedian;
    std::vector<float> phaseVelocityMedian;
    std::vector<std::size_t> phaseNoteEvents;
    std::vector<std::size_t> phaseTimingSupport;
    std::vector<std::size_t> phaseVelocitySupport;
    std::size_t writableTimingOnsets = 0u;
    std::size_t defaultVelocityValues = 0u;
};

struct PatternReshapeResult {
    Pattern pattern;
    BurstLibrary burstLibrary;
    PatternReshapeAnalysis before;
    PatternReshapeAnalysis after;
    std::size_t timingChanged = 0u;
    std::size_t timingCreated = 0u;
    std::size_t velocityChanged = 0u;
    std::size_t velocityCreated = 0u;
    std::size_t timingSkipped = 0u;
    std::size_t notesAdded = 0u;
    std::size_t notesRemoved = 0u;
    std::size_t notesMoved = 0u;
    std::size_t burstsCreated = 0u;
    std::size_t cyclesChanged = 0u;

    bool changed() const noexcept
    {
        return timingChanged > 0u || velocityChanged > 0u
            || notesAdded > 0u || notesRemoved > 0u
            || notesMoved > 0u || burstsCreated > 0u
            || cyclesChanged > 0u;
    }
};

std::size_t patternReshapeRows(const Pattern& pattern) noexcept;
std::size_t inferPatternReshapeCycle(
    const Pattern& pattern, std::size_t requestedCycle = 0u) noexcept;
PatternReshapeAnalysis analyzePatternReshape(
    const Pattern& pattern, std::size_t cycleRows = 0u,
    uint32_t laneMask = 0xffffffffu);
PatternReshapeResult reshapePattern(
    const Pattern& pattern, const BurstLibrary& burstLibrary,
    PatternReshapeSettings settings);
inline PatternReshapeResult reshapePattern(
    const Pattern& pattern, PatternReshapeSettings settings)
{
    return reshapePattern(pattern, BurstLibrary {}, std::move(settings));
}

} // namespace s3g::tracker
