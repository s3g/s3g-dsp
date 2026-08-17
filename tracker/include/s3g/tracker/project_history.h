#pragma once

#include "s3g/tracker/project_codec.h"

#include <cstddef>
#include <deque>
#include <string>

namespace s3g::tracker {

// Bounded, deterministic project history for the plug-in UI. Snapshots use
// the native JSON codec so history restores exactly the same persistent state
// that REAPER and .s3gt files receive, without retaining transient UI state.
class ProjectHistory {
public:
    static constexpr std::size_t kMaximumEntries = 64u;
    static constexpr std::size_t kMaximumStackBytes = 32u * 1024u * 1024u;

    ProjectResult reset(const ProjectDocument& document);
    ProjectResult record(const ProjectDocument& document,
        bool* changed = nullptr);
    ProjectResult undo(ProjectDocument& document);
    ProjectResult redo(ProjectDocument& document);

    bool canUndo() const noexcept { return !undo_.snapshots.empty(); }
    bool canRedo() const noexcept { return !redo_.snapshots.empty(); }
    std::size_t undoCount() const noexcept { return undo_.snapshots.size(); }
    std::size_t redoCount() const noexcept { return redo_.snapshots.size(); }

private:
    struct Stack {
        std::deque<std::string> snapshots;
        std::size_t bytes = 0u;
    };

    static void clear(Stack& stack) noexcept;
    static void push(Stack& stack, std::string snapshot);
    static bool pop(Stack& stack, std::string& snapshot);

    std::string current_;
    Stack undo_;
    Stack redo_;
};

} // namespace s3g::tracker
