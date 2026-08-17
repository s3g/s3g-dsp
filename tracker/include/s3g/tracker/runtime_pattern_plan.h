#pragma once

#include "s3g/tracker/project_document.h"

#include <cstddef>
#include <vector>

namespace s3g::tracker {

// Minimal immutable pattern set needed by one playback runtime. Ordinary
// pattern mode compiles only the active pattern. Song mode compiles only the
// unique patterns actually referenced by its rows, in first-use order.
struct RuntimePatternPlan {
    std::vector<std::size_t> documentPatternIndices;
    std::vector<std::size_t> songPatternIndices;
    std::size_t initialPatternIndex = 0u;
    bool songEnabled = false;
};

bool makeRuntimePatternPlan(const ProjectDocument& document,
    RuntimePatternPlan& plan);

} // namespace s3g::tracker
