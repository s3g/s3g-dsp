#include "s3g/tracker/pattern_bank.h"

#include <algorithm>
#include <utility>

namespace s3g::tracker {
namespace {

bool isAsciiAlphaNumeric(char value) noexcept
{
    const auto byte = static_cast<unsigned char>(value);
    return (byte >= static_cast<unsigned char>('a')
            && byte <= static_cast<unsigned char>('z'))
        || (byte >= static_cast<unsigned char>('A')
            && byte <= static_cast<unsigned char>('Z'))
        || (byte >= static_cast<unsigned char>('0')
            && byte <= static_cast<unsigned char>('9'));
}

} // namespace

PatternBankEntry* PatternBank::findEntry(std::string_view id) noexcept
{
    const auto found = std::find_if(entries.begin(), entries.end(),
        [id](const PatternBankEntry& entry) { return entry.id == id; });
    return found == entries.end() ? nullptr : &*found;
}

const PatternBankEntry* PatternBank::findEntry(
    std::string_view id) const noexcept
{
    const auto found = std::find_if(entries.begin(), entries.end(),
        [id](const PatternBankEntry& entry) { return entry.id == id; });
    return found == entries.end() ? nullptr : &*found;
}

Pattern* PatternBank::findPattern(std::string_view id) noexcept
{
    auto* entry = findEntry(id);
    return entry ? &entry->pattern : nullptr;
}

const Pattern* PatternBank::findPattern(std::string_view id) const noexcept
{
    const auto* entry = findEntry(id);
    return entry ? &entry->pattern : nullptr;
}

Pattern* PatternBank::activePattern() noexcept
{
    return findPattern(activePatternId);
}

const Pattern* PatternBank::activePattern() const noexcept
{
    return findPattern(activePatternId);
}

bool PatternBank::selectPattern(std::string_view id)
{
    if (!findEntry(id)) return false;
    if (id == activePatternId) return true;
    activePatternId.assign(id.data(), id.size());
    return true;
}

bool isValidPatternId(std::string_view id) noexcept
{
    if (id.empty() || id.size() > kMaximumPatternIdBytes
        || !isAsciiAlphaNumeric(id.front())) return false;
    return std::all_of(id.begin() + 1, id.end(), [](char value) {
        return isAsciiAlphaNumeric(value) || value == '-'
            || value == '_' || value == '.';
    });
}

bool isValidPatternAlias(std::string_view alias) noexcept
{
    if (alias.empty() || alias.size() > 64u
        || alias.front() < 'a' || alias.front() > 'z') return false;
    return std::all_of(alias.begin() + 1, alias.end(), [](char value) {
        return (value >= 'a' && value <= 'z')
            || (value >= '0' && value <= '9') || value == '_';
    });
}

PatternBankValidationResult validatePatternBank(
    const PatternBank& bank) noexcept
{
    if (bank.entries.empty()) return { PatternBankValidationCode::Empty };
    if (bank.entries.size() > kMaximumPatternBankEntries)
        return { PatternBankValidationCode::TooManyPatterns };
    for (std::size_t index = 0u; index < bank.entries.size(); ++index) {
        const auto& id = bank.entries[index].id;
        if (id.empty())
            return { PatternBankValidationCode::EmptyId, index };
        if (!isValidPatternId(id))
            return { PatternBankValidationCode::InvalidId, index };
        for (std::size_t prior = 0u; prior < index; ++prior) {
            if (bank.entries[prior].id == id)
                return { PatternBankValidationCode::DuplicateId, index };
        }
        const auto& defaults = bank.entries[index].laneDefaultNotes;
        if (defaults.size() > bank.entries[index].pattern.tracks.size()
            || defaults.size() > kMaximumTrackCount)
            return { PatternBankValidationCode::TooManyLaneDefaults, index };
        if (std::any_of(defaults.begin(), defaults.end(),
                [](uint8_t note) { return note > 127u; }))
            return { PatternBankValidationCode::InvalidLaneDefaultNote,
                index };
        const auto& aliases = bank.entries[index].aliases;
        if (aliases.size() > kMaximumPatternAliases)
            return { PatternBankValidationCode::TooManyAliases, index };
        for (const auto& [name, lane] : aliases) {
            if (!isValidPatternAlias(name))
                return { PatternBankValidationCode::InvalidAlias, index };
            if (lane >= bank.entries[index].pattern.tracks.size())
                return { PatternBankValidationCode::AliasTrackMissing,
                    index };
        }
    }
    if (bank.activePatternId.empty())
        return { PatternBankValidationCode::EmptyActiveId };
    if (!bank.findEntry(bank.activePatternId))
        return { PatternBankValidationCode::ActivePatternMissing };
    return {};
}

PatternBank makeDefaultPatternBank()
{
    PatternBank bank;
    PatternBankEntry entry;
    entry.id = "A01";
    entry.pattern.name = "PATTERN 01";
    bank.entries.push_back(std::move(entry));
    bank.activePatternId = "A01";
    return bank;
}

SongPatternReferenceResult validateSongPatternReferences(
    const SongArrangement& song, const PatternBank& bank) noexcept
{
    for (std::size_t row = 0u; row < song.rows.size(); ++row) {
        if (!bank.findEntry(song.rows[row].patternId))
            return { SongPatternReferenceCode::PatternMissing, row };
    }
    return {};
}

} // namespace s3g::tracker
