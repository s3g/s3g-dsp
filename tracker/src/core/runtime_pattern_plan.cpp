#include "s3g/tracker/runtime_pattern_plan.h"

#include <algorithm>

namespace s3g::tracker {

bool makeRuntimePatternPlan(const ProjectDocument& document,
    RuntimePatternPlan& plan)
{
    plan = {};
    const auto& entries = document.patternBank.entries;
    if (entries.empty()) return false;

    const bool songRequested = document.session.songPlaybackEnabled
        && !document.song.rows.empty();
    if (!songRequested) {
        const auto active = std::find_if(entries.begin(), entries.end(),
            [&](const auto& entry) {
                return entry.id == document.patternBank.activePatternId;
            });
        if (active == entries.end()) return false;
        plan.documentPatternIndices.push_back(static_cast<std::size_t>(
            std::distance(entries.begin(), active)));
        return true;
    }

    plan.songEnabled = true;
    plan.documentPatternIndices.reserve(std::min(
        entries.size(), document.song.rows.size()));
    plan.songPatternIndices.reserve(document.song.rows.size());
    for (const auto& row : document.song.rows) {
        const auto entry = std::find_if(entries.begin(), entries.end(),
            [&](const auto& candidate) {
                return candidate.id == row.patternId;
            });
        if (entry == entries.end()) return false;
        const auto documentIndex = static_cast<std::size_t>(
            std::distance(entries.begin(), entry));
        const auto prepared = std::find(plan.documentPatternIndices.begin(),
            plan.documentPatternIndices.end(), documentIndex);
        std::size_t preparedIndex = 0u;
        if (prepared == plan.documentPatternIndices.end()) {
            preparedIndex = plan.documentPatternIndices.size();
            plan.documentPatternIndices.push_back(documentIndex);
        } else {
            preparedIndex = static_cast<std::size_t>(std::distance(
                plan.documentPatternIndices.begin(), prepared));
        }
        plan.songPatternIndices.push_back(preparedIndex);
    }
    plan.initialPatternIndex = plan.songPatternIndices.front();
    return true;
}

} // namespace s3g::tracker
