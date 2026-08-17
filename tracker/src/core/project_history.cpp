#include "s3g/tracker/project_history.h"

#include <utility>

namespace s3g::tracker {
namespace {

ProjectResult historyError(std::string message)
{
    ProjectResult result;
    result.code = ProjectErrorCode::InvalidArgument;
    result.location = "history";
    result.message = std::move(message);
    return result;
}

} // namespace

void ProjectHistory::clear(Stack& stack) noexcept
{
    stack.snapshots.clear();
    stack.bytes = 0u;
}

void ProjectHistory::push(Stack& stack, std::string snapshot)
{
    if (snapshot.size() > kMaximumStackBytes) {
        clear(stack);
        return;
    }
    while (!stack.snapshots.empty()
        && (stack.snapshots.size() >= kMaximumEntries
            || stack.bytes + snapshot.size() > kMaximumStackBytes)) {
        stack.bytes -= stack.snapshots.front().size();
        stack.snapshots.pop_front();
    }
    stack.bytes += snapshot.size();
    stack.snapshots.push_back(std::move(snapshot));
}

bool ProjectHistory::pop(Stack& stack, std::string& snapshot)
{
    if (stack.snapshots.empty()) return false;
    stack.bytes -= stack.snapshots.back().size();
    snapshot = std::move(stack.snapshots.back());
    stack.snapshots.pop_back();
    return true;
}

ProjectResult ProjectHistory::reset(const ProjectDocument& document)
{
    std::string encoded;
    auto result = encodeProjectDocument(document, encoded);
    if (!result.ok()) return result;
    current_ = std::move(encoded);
    clear(undo_);
    clear(redo_);
    return {};
}

ProjectResult ProjectHistory::record(const ProjectDocument& document,
    bool* changed)
{
    if (changed) *changed = false;
    std::string encoded;
    auto result = encodeProjectDocument(document, encoded);
    if (!result.ok()) return result;
    if (current_.empty()) {
        current_ = std::move(encoded);
        clear(undo_);
        clear(redo_);
        if (changed) *changed = true;
        return {};
    }
    if (encoded == current_) return {};
    push(undo_, std::move(current_));
    current_ = std::move(encoded);
    clear(redo_);
    if (changed) *changed = true;
    return {};
}

ProjectResult ProjectHistory::undo(ProjectDocument& document)
{
    std::string target;
    if (!pop(undo_, target)) return historyError("Nothing to undo.");
    ProjectDocument candidate;
    auto result = decodeProjectDocument(target, candidate);
    if (!result.ok()) {
        push(undo_, std::move(target));
        return result;
    }
    push(redo_, std::move(current_));
    current_ = std::move(target);
    document = std::move(candidate);
    return {};
}

ProjectResult ProjectHistory::redo(ProjectDocument& document)
{
    std::string target;
    if (!pop(redo_, target)) return historyError("Nothing to redo.");
    ProjectDocument candidate;
    auto result = decodeProjectDocument(target, candidate);
    if (!result.ok()) {
        push(redo_, std::move(target));
        return result;
    }
    push(undo_, std::move(current_));
    current_ = std::move(target);
    document = std::move(candidate);
    return {};
}

} // namespace s3g::tracker
