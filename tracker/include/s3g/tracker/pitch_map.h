#pragma once

#include "s3g/tracker/sequencer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g::tracker {

inline constexpr std::size_t kPitchMapAllLanes =
    std::numeric_limits<std::size_t>::max();

enum class PitchContour : uint8_t {
    Fit,
    Rise,
    Fall,
    Pendulum,
    RandomWalk,
    VaryExisting,
    Manual,
};

struct PitchMapSettings {
    uint8_t rootPitchClass = 0u;
    uint32_t scale = 2u;
    uint8_t minimumNote = 36u;
    uint8_t maximumNote = 84u;
    PitchContour contour = PitchContour::VaryExisting;
    uint8_t maximumLeapDegrees = 3u;
    float variation = 0.5f;
    uint32_t seed = 733u;
    bool preserveEndpoints = true;
    int8_t transposeSemitones = 0;
    bool invertScaleDegrees = false;
    bool reversePitchOrder = false;
};

// Immutable UI-to-audio snapshot used by stopped-transport Pitch Map
// audition. Row is relative to the first audible hit, so preview starts
// immediately while retaining the authored gaps between later notes.
struct PitchPreviewEvent {
    uint16_t row = 0u;
    uint8_t note = 60u;
    uint8_t velocity = 100u;
    uint8_t gatePercent = 70u;
};

struct PitchMapAnalysis {
    uint8_t rootPitchClass = 0u;
    uint32_t scale = 2u;
    std::size_t noteCount = 0u;
    std::size_t uniquePitchClasses = 0u;
    std::size_t matchingNotes = 0u;
    uint8_t minimumNote = 0u;
    uint8_t maximumNote = 127u;
    float confidence = 0.0f;
};

struct PitchMapAssignment {
    std::size_t row = 0u;
    uint8_t originalNote = 0u;
    uint8_t note = 0u;
};

struct PitchMapResult {
    std::vector<PitchMapAssignment> assignments;
    std::size_t changed = 0u;
};

const char* pitchContourName(PitchContour contour) noexcept;

// Analyze explicit NOTE cells only. Pass kPitchMapAllLanes to use every lane
// in the row range. Symbols, Burst references, and rests are not pitch
// evidence.
PitchMapAnalysis analyzePitchMap(const Pattern& pattern, std::size_t lane,
    std::size_t firstRow, std::size_t lastRow);

// Produce a deterministic, non-mutating proposal for one lane. Rhythm and
// NOTE symbols are preserved because assignments are emitted only for
// explicit NOTE cells.
PitchMapResult previewPitchMap(const Pattern& pattern, std::size_t lane,
    std::size_t firstRow, std::size_t lastRow,
    const PitchMapSettings& settings);

// Apply the same proposal used by the GUI preview and live command engine.
// Returns the number of explicit NOTE pitches that changed.
std::size_t applyPitchMap(Pattern& pattern, std::size_t lane,
    std::size_t firstRow, std::size_t lastRow,
    const PitchMapSettings& settings);

} // namespace s3g::tracker
