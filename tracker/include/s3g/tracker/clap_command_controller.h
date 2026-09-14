#pragma once
#include "s3g/tracker/editor_state.h"
#include <functional>

namespace s3g::tracker {
// Synchronous UI-thread boundary. Adapters perform host/window/publication
// actions in the order requested by shared command policy. No timer or audio
// callback is moved here; MIDI and delayed visual snapshots stay independent.
struct ClapCommandServices {
    std::function<void(const std::string&, bool)> message = [](const auto&, bool) {};
    std::function<void()> showHelp = [] {}, undo = [] {}, redo = [] {};
    std::function<void()> commitRuntime = [] {}, commitDocument = [] {};
    std::function<void()> refreshWarps = [] {}, refreshUI = [] {}, updateMonitor = [] {};
    std::function<bool(const PatternVariationRequest&)> installVariation
        = [](const auto&) { return false; };
    std::function<bool()> requestHostContinue = [] { return false; };
    std::function<bool()> requestHostStop = [] { return false; };
    std::function<void()> panic = [] {};
};
void refreshProjectBurstUsageCounts(app::TrackerViewState& state);
void executeClapCommand(app::TrackerViewState& state, const std::string& command,
    const ClapCommandServices& services);
} // namespace s3g::tracker
