#pragma once

#include "s3g/tracker/sequencer.h"
#include "s3g/tracker/song_playback_planner.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace s3g::tracker {

// Pattern IDs are stable project-local references. They are deliberately
// separate from Pattern::name: changing a display name must never break Song
// rows or reorder the bank.
constexpr std::size_t kMaximumPatternBankEntries = 256u;
constexpr std::size_t kMaximumPatternIdBytes = 64u;
constexpr std::size_t kMaximumPatternAliases = 1024u;

struct PatternBankEntry {
    std::string id;
    Pattern pattern;
    // Command authoring remembers a pitch even when a drum lane currently
    // contains only rests. This memory follows its pattern when selection
    // changes; it must not leak from the previously active bank entry.
    std::vector<uint8_t> laneDefaultNotes;
    // Console aliases address zero-based tracks and belong to the pattern
    // whose lane layout they describe. Keeping them here makes patterns with
    // different track counts independent and safe to select.
    std::map<std::string, std::size_t> aliases;
};

struct PatternBank {
    // Vector order is the user-visible bank order and is serialized exactly.
    std::vector<PatternBankEntry> entries;
    std::string activePatternId;

    PatternBankEntry* findEntry(std::string_view id) noexcept;
    const PatternBankEntry* findEntry(std::string_view id) const noexcept;
    Pattern* findPattern(std::string_view id) noexcept;
    const Pattern* findPattern(std::string_view id) const noexcept;
    Pattern* activePattern() noexcept;
    const Pattern* activePattern() const noexcept;

    // Selection is transactional: a missing ID leaves the selection intact.
    bool selectPattern(std::string_view id);
};

enum class PatternBankValidationCode : uint8_t {
    Valid,
    Empty,
    TooManyPatterns,
    EmptyId,
    InvalidId,
    DuplicateId,
    TooManyLaneDefaults,
    InvalidLaneDefaultNote,
    TooManyAliases,
    InvalidAlias,
    AliasTrackMissing,
    EmptyActiveId,
    ActivePatternMissing,
};

constexpr std::size_t kNoPatternBankEntry = static_cast<std::size_t>(-1);

struct PatternBankValidationResult {
    PatternBankValidationCode code = PatternBankValidationCode::Valid;
    // kNoPatternBankEntry means the failure applies to the bank as a whole.
    std::size_t entry = kNoPatternBankEntry;

    bool ok() const noexcept
    {
        return code == PatternBankValidationCode::Valid;
    }
};

bool isValidPatternId(std::string_view id) noexcept;
bool isValidPatternAlias(std::string_view alias) noexcept;
PatternBankValidationResult validatePatternBank(
    const PatternBank& bank) noexcept;

// A new native project always begins with one addressable pattern. Its stable
// ID is A01 while the display name remains independently editable.
PatternBank makeDefaultPatternBank();

enum class SongPatternReferenceCode : uint8_t {
    Valid,
    PatternMissing,
};

struct SongPatternReferenceResult {
    SongPatternReferenceCode code = SongPatternReferenceCode::Valid;
    std::size_t row = kNoSongRow;

    bool ok() const noexcept
    {
        return code == SongPatternReferenceCode::Valid;
    }
};

// Shape/range validation remains validateSongArrangement(); this helper only
// verifies the project-level foreign key from each Song row into the bank.
SongPatternReferenceResult validateSongPatternReferences(
    const SongArrangement& song, const PatternBank& bank) noexcept;

} // namespace s3g::tracker
