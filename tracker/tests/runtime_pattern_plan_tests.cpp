#include "s3g/tracker/runtime_pattern_plan.h"

#include <iostream>

namespace {

using namespace s3g::tracker;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

ProjectDocument largeProject()
{
    ProjectDocument document;
    document.patternBank.entries.clear();
    for (std::size_t index = 0u; index < 256u; ++index) {
        PatternBankEntry entry;
        entry.id = "P" + std::to_string(index + 1u);
        entry.pattern.name = entry.id;
        entry.pattern.visibleRows = 16u;
        entry.pattern.tracks.resize(32u);
        document.patternBank.entries.push_back(std::move(entry));
    }
    document.patternBank.activePatternId = "P200";
    return document;
}

} // namespace

int main()
{
    auto document = largeProject();
    RuntimePatternPlan plan;
    check(makeRuntimePatternPlan(document, plan)
            && !plan.songEnabled
            && plan.documentPatternIndices.size() == 1u
            && plan.documentPatternIndices[0u] == 199u,
        "pattern mode should compile only the active pattern");

    document.session.songPlaybackEnabled = true;
    document.song.rows.clear();
    for (const char* id : { "P7", "P2", "P7", "P200", "P2" }) {
        SongRow row;
        row.patternId = id;
        row.durationTicks = 16u;
        document.song.rows.push_back(row);
    }
    check(makeRuntimePatternPlan(document, plan)
            && plan.songEnabled
            && plan.documentPatternIndices.size() == 3u
            && plan.documentPatternIndices[0u] == 6u
            && plan.documentPatternIndices[1u] == 1u
            && plan.documentPatternIndices[2u] == 199u
            && plan.songPatternIndices
                == std::vector<std::size_t>({ 0u, 1u, 0u, 2u, 1u }),
        "Song mode should compile unique referenced patterns in first-use order");

    document.song.rows[2u].patternId = "MISSING";
    check(!makeRuntimePatternPlan(document, plan),
        "a missing Song pattern should fail instead of silently falling back");

    return failures == 0 ? 0 : 1;
}
