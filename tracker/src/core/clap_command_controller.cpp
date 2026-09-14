#include "s3g/tracker/clap_command_controller.h"
#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/fx_catalog.h"
#include <algorithm>
#include <charconv>
#include <sstream>
namespace s3g::tracker {
using app::TrackerViewState;
namespace {
bool resetPatternPhases(s3g::tracker::Pattern& pattern) noexcept
{
    bool changed = false;
    const auto reset = [&](s3g::tracker::ColumnDefinition& column) {
        changed |= column.phase != 0u;
        column.phase = 0u;
    };
    for (auto& track : pattern.tracks) {
        reset(track.noteColumn);
        reset(track.instrumentColumn);
        reset(track.velocityColumn);
        for (auto& pair : track.fxPairs) {
            reset(pair.actionColumn);
            reset(pair.valueColumn);
        }
    }
    return changed;
}

std::vector<std::string> commandWords(std::string_view command)
{
    std::istringstream stream { std::string(command) };
    std::vector<std::string> words;
    std::string word;
    while (stream >> word) {
        for (char& character : word) {
            if (character >= 'A' && character <= 'Z')
                character = static_cast<char>(character - 'A' + 'a');
        }
        words.push_back(std::move(word));
    }
    return words;
}
} // namespace
void refreshProjectBurstUsageCounts(TrackerViewState& state)
{
    (void)syncSessionToActivePattern(state);
    (void)syncActiveAssetBanks(state);
    state.session.projectBurstUsageCounts.fill(0u);
    const auto countNotes = [&](const std::vector<s3g::tracker::NoteCell>& notes) {
        for (const auto& cell : notes) {
            if (cell.state == s3g::tracker::NoteCellState::Burst
                && cell.burstBankId == state.activeBurstBankId
                && cell.note < state.session.projectBurstUsageCounts.size())
                ++state.session.projectBurstUsageCounts[cell.note];
        }
    };
    for (const auto& entry : state.patternBank.entries)
        for (const auto& track : entry.pattern.tracks) countNotes(track.notes);
    for (const auto& bank : state.phraseBanks)
        for (const auto& phrase : bank.library.phrases)
            countNotes(phrase.notes);
}

void executeClapCommand(app::TrackerViewState& state, const std::string& command,
    const ClapCommandServices& services)
{
    const auto words = commandWords(command);
    if (!words.empty() && words.front() == "burst") {
        (void)syncSessionToActivePattern(state);
        refreshProjectBurstUsageCounts(state);
    }
    if (!words.empty()) {
        const auto& verb = words.front();
        if ((verb == "phase" || verb == "ph") && words.size() == 3u
            && words[1u] == "reset" && words[2u] == "bank") {
            if (!syncSessionToActivePattern(state)) {
                services.message("Could not synchronize the active pattern before resetting phases", true);
                return;
            }
            bool changed = false;
            for (auto& entry : state.patternBank.entries)
                changed |= resetPatternPhases(entry.pattern);
            (void)loadActivePatternIntoSession(state);
            state.status = changed
                ? "Reset every column phase in the pattern bank"
                : "Every pattern-bank column phase is already zero";
            services.message(state.status, false);
            if (changed) services.commitRuntime();
            else services.refreshUI();
            return;
        }
        const bool variationCommand = verb == "variation" || verb == "vary";
        const bool quantizedVariationLaunch = variationCommand
            && words.size() >= 4u
            && words[words.size() - 2u] == "launch"
            && (words.back() == "tick" || words.back() == "beat"
                || words.back() == "cycle" || words.back() == "pattern");
        if (variationCommand && state.patternBank.entries.size()
                >= s3g::tracker::kMaximumPatternBankEntries) {
            services.message("Pattern bank is full; delete a pattern before creating a variation.", true);
            return;
        }
        if (quantizedVariationLaunch && state.songPlaybackEnabled) {
            services.message("Quantized bank variation launch is unavailable while Song playback owns pattern transitions.", true);
            return;
        }
        if (verb == "help" || verb == "?") {
            services.showHelp();
            services.message("Opened the MIDI tracker command reference", false);
            return;
        }
        if (verb == "bpm") {
            services.message("Tempo follows REAPER. Use the RATE menu for musical multiples.", true);
            return;
        }
        if (verb == "instrument" || verb == "inst") {
            services.message("The MIDI tracker has no INS column; set CH01–CH16 in the lane header.", true);
            return;
        }
        if (verb == "actions") {
            std::string message = "SEQUENCER ACTIONS";
            for (std::size_t index = 0u;
                 index < s3g::tracker::sequencerActionCount(); ++index) {
                const auto* action = s3g::tracker::sequencerAction(index);
                if (!action) continue;
                message += index == 0u ? "  " : " · ";
                message += action->mnemonic;
                message += " ";
                message += action->displayName;
            }
            message += " · CC0–CC127 MIDI Control Change";
            services.message(message, false);
            return;
        }
        const bool columnCommand = verb == "len" || verb == "length"
            || verb == "stride" || verb == "speed" || verb == "spd"
            || verb == "phase" || verb == "ph" || verb == "dir"
            || verb == "mode" || verb == "mute";
        if (columnCommand && std::find_if(words.begin(), words.end(),
                [](const std::string& word) {
                    return word == "ins" || word == "instrument";
                }) != words.end()) {
            services.message("INS is not a column in the MIDI tracker.", true);
            return;
        }
        std::string action;
        if (verb == "fx" && words.size() >= 5u) action = words[4u];
        else if ((verb == "fx1" || verb == "f1" || verb == "fx2"
                || verb == "f2") && words.size() >= 3u) action = words[2u];
        uint8_t midiController = 0u;
        const bool midiControlChange = s3g::tracker::parseMidiControlChange(
            action, midiController);
        if (!action.empty() && action != "clear" && action != "previous"
            && action != "prv" && !s3g::tracker::findSequencerAction(action)
            && !midiControlChange) {
            services.message("SEQ columns accept sequencing actions or CC0..CC127; type actions to list them.", true);
            return;
        }
    }
    const auto commandRngBefore = state.session.commandRngState;
    // Resolve a removed lane before the command erases aliases/reindexes the
    // pattern. Never let a pinned follow source silently become its neighbour.
    std::optional<std::size_t> removedLane;
    if (words.size() == 3 && words[0] == "track" && words[1] == "remove") {
        const auto& target = words[2];
        if (!target.empty() && target.front() == '@') {
            const auto alias = state.session.aliases.find(target.substr(1));
            if (alias != state.session.aliases.end()) removedLane = alias->second;
        } else {
            std::size_t value = 0;
            const auto parsed = std::from_chars(target.data(), target.data() + target.size(), value);
            if (parsed.ec == std::errc {} && parsed.ptr == target.data() + target.size() && value > 0)
                removedLane = value - 1;
        }
    }
    const auto result = CommandEngine::execute(state.session, command);
    if (!result.ok) {
        services.message(result.message, true);
        return;
    }
    if (removedLane) {
        if (state.trackerFollow.lane > *removedLane)
            --state.trackerFollow.lane;
        else if (state.trackerFollow.lane == *removedLane && !state.trackerFollow.selectedLane)
            state.trackerFollow.mode = TrackerFollowMode::Static;
    }
    if (result.hasEffect(CommandEffect::UndoRequested)) {
        services.undo();
        return;
    }
    if (result.hasEffect(CommandEffect::RedoRequested)) {
        services.redo();
        return;
    }
    if (result.patternVariation) {
        services.message(result.message, false);
        if (!services.installVariation(*result.patternVariation)) {
            state.session.commandRngState = commandRngBefore;
            services.message("Pattern variation was not installed.", true);
        }
        return;
    }
    services.message(result.message, false);
    if (result.hasEffect(CommandEffect::ProjectChanged))
        services.refreshWarps();
    const bool runtimeChanged = result.hasEffect(CommandEffect::PatternChanged)
        || result.hasEffect(CommandEffect::TransportChanged)
        || result.hasEffect(CommandEffect::OutputChanged)
        || result.hasEffect(CommandEffect::RoutingChanged);
    if (runtimeChanged)
        services.commitRuntime();
    else if (result.hasEffect(CommandEffect::ProjectChanged))
        services.commitDocument();
    if (result.hasEffect(CommandEffect::StartPlayback)) {
        if (!services.requestHostContinue())
            services.message("The host does not expose a plug-in transport-start request.", true);
    }
    if (result.hasEffect(CommandEffect::StopPlayback)) {
        if (!services.requestHostStop())
            services.message("The host does not expose a plug-in transport-stop request.", true);
    }
    if (result.hasEffect(CommandEffect::Panic))
        services.panic();
    services.updateMonitor();
    services.refreshUI();
}
} // namespace s3g::tracker
