#pragma once

#include "s3g/tracker/sequencer.h"

#include <array>
#include <cstddef>
#include <string>

namespace s3g::tracker {

constexpr std::size_t kPhraseLibrarySlots = 64u;
constexpr std::size_t kMinimumPhraseRows = 2u;
constexpr std::size_t kMaximumPhraseRows = 64u;
constexpr std::size_t kMaximumPhraseNameBytes = 64u;

// A phrase is a visible, editable lane fragment. It deliberately stores the
// same cells as the Tracker instead of a reference or generator recipe; after
// placement the destination pattern has no dependency on this library slot.
struct PhraseDefinition {
    std::string name;
    std::size_t length = 16u;
    // Audition routing belongs to the reusable phrase. Placement still uses
    // the destination Tracker lane's MIDI channel.
    uint8_t previewMidiChannel = 1u;
    std::vector<NoteCell> notes;
    std::vector<ValueCell> velocities;
    std::vector<GateCell> gates;
    std::array<FxPair, kFxPairCount> fxPairs;
    bool empty() const noexcept;
};

struct PhraseLibrary {
    std::array<PhraseDefinition, kPhraseLibrarySlots> phrases {};
};

enum class PhrasePlacementMode : uint8_t {
    Replace,
    MergeIntoEmpty,
};

PhraseDefinition makeBlankPhrase(std::size_t length = 16u);

// Capture one lane and one inclusive row range. Ranges shorter than two rows
// are expanded to the two-row phrase minimum; ranges over 64 rows are rejected.
bool capturePhrase(const Track& source, std::size_t firstRow,
    std::size_t lastRow, PhraseDefinition& destination) noexcept;
bool capturePhrase(const Pattern& source, std::size_t track,
    std::size_t firstRow, std::size_t lastRow,
    PhraseDefinition& destination) noexcept;

// Stamp a phrase at destinationRow. The pattern may grow up to the Tracker's
// 256-row limit. In MergeIntoEmpty mode each typed cell is copied only when
// the corresponding destination cell is empty/default/previous.
bool placePhrase(Track& destination, const PhraseDefinition& phrase,
    std::size_t destinationRow,
    PhrasePlacementMode mode = PhrasePlacementMode::Replace) noexcept;
bool placePhrase(Pattern& destination, std::size_t track,
    const PhraseDefinition& phrase, std::size_t destinationRow,
    PhrasePlacementMode mode = PhrasePlacementMode::Replace) noexcept;

} // namespace s3g::tracker
