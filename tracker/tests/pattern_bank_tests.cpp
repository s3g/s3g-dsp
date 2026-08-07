#include "s3g/tracker/pattern_bank.h"

#include <iostream>
#include <string>

namespace {

using namespace s3g::tracker;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void testDefaultBankAndSelection()
{
    auto bank = makeDefaultPatternBank();
    check(validatePatternBank(bank).ok()
            && bank.entries.size() == 1u
            && bank.activePatternId == "A01"
            && bank.activePattern() == &bank.entries[0u].pattern,
        "new projects should have one valid, active A01 pattern");

    PatternBankEntry second;
    second.id = "A02";
    second.pattern.name = "SECOND";
    bank.entries.push_back(second);
    check(bank.selectPattern("A02")
            && bank.activePattern()->name == "SECOND",
        "selection should resolve a stable ID without depending on display name");
    bank.activePattern()->name = "RENAMED";
    check(bank.activePatternId == "A02"
            && bank.findPattern("A02")->name == "RENAMED",
        "renaming a pattern should not rewrite its stable ID");
    check(!bank.selectPattern("missing") && bank.activePatternId == "A02",
        "selecting a missing pattern should leave selection unchanged");
}

void testValidationFailures()
{
    PatternBank bank;
    check(validatePatternBank(bank).code == PatternBankValidationCode::Empty,
        "empty banks should reject");

    bank = makeDefaultPatternBank();
    bank.entries[0u].id = "bad id";
    check(validatePatternBank(bank).code
                == PatternBankValidationCode::InvalidId,
        "whitespace should not be accepted in stable IDs");
    check(isValidPatternId("breaks-02.alt")
            && !isValidPatternId("_hidden")
            && !isValidPatternId(""),
        "stable ID grammar should be explicit and predictable");

    bank = makeDefaultPatternBank();
    bank.entries.push_back(bank.entries.front());
    check(validatePatternBank(bank).code
                == PatternBankValidationCode::DuplicateId,
        "duplicate stable IDs should reject");

    bank = makeDefaultPatternBank();
    bank.activePatternId = "A02";
    check(validatePatternBank(bank).code
                == PatternBankValidationCode::ActivePatternMissing,
        "active selection should be a foreign key into the bank");

    bank = makeDefaultPatternBank();
    bank.entries[0u].laneDefaultNotes = { 128u };
    check(validatePatternBank(bank).code
                == PatternBankValidationCode::TooManyLaneDefaults,
        "authoring anchors should not exist past the pattern's lanes");

    Track lane;
    bank.entries[0u].pattern.tracks.push_back(std::move(lane));
    check(validatePatternBank(bank).code
                == PatternBankValidationCode::InvalidLaneDefaultNote,
        "per-pattern authoring anchors should remain valid MIDI notes");

    bank = makeDefaultPatternBank();
    bank.entries[0u].pattern.tracks.emplace_back();
    bank.entries[0u].aliases.emplace("kick", 0u);
    check(validatePatternBank(bank).ok(),
        "a canonical alias should resolve inside its own pattern");
    bank.entries[0u].aliases.emplace("Bad Alias", 0u);
    check(validatePatternBank(bank).code
                == PatternBankValidationCode::InvalidAlias,
        "per-pattern aliases should retain the console's canonical grammar");
    bank.entries[0u].aliases.erase("Bad Alias");
    bank.entries[0u].aliases["outside"] = 1u;
    check(validatePatternBank(bank).code
                == PatternBankValidationCode::AliasTrackMissing,
        "an alias should never borrow a lane from a different pattern layout");
}

void testSongReferences()
{
    auto bank = makeDefaultPatternBank();
    PatternBankEntry second;
    second.id = "A02";
    bank.entries.push_back(std::move(second));

    SongArrangement song;
    song.rows.push_back({ "A02", 16u, 1u });
    check(validateSongPatternReferences(song, bank).ok(),
        "Song rows should resolve any pattern in bank order");
    song.rows.push_back({ "MISSING", 16u, 1u });
    const auto missing = validateSongPatternReferences(song, bank);
    check(missing.code == SongPatternReferenceCode::PatternMissing
            && missing.row == 1u,
        "Song reference validation should identify the first unresolved row");
}

} // namespace

int main()
{
    testDefaultBankAndSelection();
    testValidationFailures();
    testSongReferences();
    if (failures != 0) {
        std::cerr << failures << " pattern bank test(s) failed\n";
        return 1;
    }
    std::cout << "Pattern bank model tests passed\n";
    return 0;
}
