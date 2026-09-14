#include "s3g/tracker/clap_command_controller.h"
#include "s3g/tracker/clap_document_controller.h"
#include "s3g/tracker/editor_grid.h"
#include "s3g/tracker/fx_catalog.h"
#include <algorithm>
#include <charconv>
#include <cmath>
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

std::string viewSummary(const TrackerViewState& state)
{
    const char* modes[] = { "STATIC", "CENTER", "PAGE" };
    std::ostringstream out;
    out << "VIEW · " << modes[std::clamp(int(state.trackerFollow.mode), 0, 2)]
        << " · NOTE SOURCE ";
    if (state.trackerFollow.selectedLane) out << "SELECTED";
    else out << state.trackerFollow.lane + 1;
    out << " · GRID " << std::lround(state.trackerGridZoom * 100) << "%"
        << " · NOTE " << (state.showMidiNoteValues ? "MIDI" : "NAME")
        << " · JUMP " << state.trackerRowJump
        << " · DETAIL " << (state.sequenceColumnsExpanded ? "ON" : "OFF");
    return out.str();
}

bool viewNumber(std::string_view text, uint32_t& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc {} && result.ptr == text.data() + text.size();
}

void executeViewCommand(TrackerViewState& state, const std::vector<std::string>& words,
    const ClapCommandServices& services)
{
    if (words.size() == 1 || (words.size() == 2 && words[1] == "status")) {
        services.message(viewSummary(state), false);
        return;
    }
    if (words.size() == 2 && words[1] == "resume") {
        ++state.trackerFollowResumeRevision;
        services.message(state.trackerFollow.mode == TrackerFollowMode::Static
                ? "VIEW is STATIC; choose view follow center or page to enable following."
                : "VIEW follow resumed.", false);
        services.refreshUI();
        return;
    }
    if (words.size() != 3) {
        services.message("Usage: view [status|resume|follow <static|center|page>|source <selected|lane|@alias>|zoom <55..180|+|-|reset>|notes <name|midi>|jump <1..16>|detail <on|off>]", true);
        return;
    }
    const auto& option = words[1];
    const auto& value = words[2];
    bool changed = false, persistent = false;
    if (option == "follow") {
        TrackerFollowMode mode;
        if (value == "static") mode = TrackerFollowMode::Static;
        else if (value == "center") mode = TrackerFollowMode::Center;
        else if (value == "page") mode = TrackerFollowMode::Page;
        else {
            services.message("Usage: view follow <static|center|page>", true);
            return;
        }
        changed = state.trackerFollow.mode != mode;
        state.trackerFollow.mode = mode;
        persistent = true;
    } else if (option == "source") {
        const bool selected = value == "selected";
        uint32_t lane = state.trackerFollow.lane;
        if (!selected) {
            uint32_t oneBased = 0;
            bool valid = false;
            if (!value.empty() && value.front() == '@') {
                const auto alias = state.session.aliases.find(value.substr(1));
                if (alias != state.session.aliases.end() && alias->second < kMaximumTrackCount) {
                    lane = static_cast<uint32_t>(alias->second);
                    valid = true;
                }
            } else if (viewNumber(value, oneBased) && oneBased >= 1 && oneBased <= kMaximumTrackCount) {
                lane = oneBased - 1;
                valid = true;
            }
            const auto* pattern = editor::playbackFollowPattern(&state);
            if (!valid || !pattern || lane >= pattern->tracks.size()) {
                services.message("VIEW source must be selected, an existing one-based NOTE lane (1..32), or its @alias.", true);
                return;
            }
        }
        changed = state.trackerFollow.selectedLane != selected || state.trackerFollow.lane != lane;
        state.trackerFollow.selectedLane = selected;
        state.trackerFollow.lane = lane;
        persistent = true;
    } else if (option == "notes") {
        if (value != "name" && value != "midi") {
            services.message("Usage: view notes <name|midi>", true);
            return;
        }
        const bool midi = value == "midi";
        changed = state.showMidiNoteValues != midi;
        state.showMidiNoteValues = midi;
        persistent = true;
    } else if (option == "jump") {
        uint32_t jump = 0;
        if (!viewNumber(value, jump) || jump < 1 || jump > 16) {
            services.message("VIEW jump must be an integer from 1 to 16.", true);
            return;
        }
        changed = state.trackerRowJump != jump;
        state.trackerRowJump = jump;
        persistent = true;
    } else if (option == "detail") {
        if (value != "on" && value != "off") {
            services.message("Usage: view detail <on|off>", true);
            return;
        }
        const bool expanded = value == "on";
        changed = state.sequenceColumnsExpanded != expanded;
        state.sequenceColumnsExpanded = expanded;
    } else if (option == "zoom") {
        double zoom = state.trackerGridZoom;
        if (value == "+") zoom = std::min(1.8, zoom * 1.16);
        else if (value == "-") zoom = std::max(.55, zoom / 1.16);
        else if (value == "reset") zoom = 1.;
        else {
            std::string_view percent(value);
            if (!percent.empty() && percent.back() == '%') percent.remove_suffix(1);
            uint32_t number = 0;
            if (!viewNumber(percent, number) || number < 55 || number > 180) {
                services.message("VIEW zoom needs an integer percentage 55..180 (optional %), +, -, or reset. This changes grid density, not the whole plug-in window.", true);
                return;
            }
            zoom = number / 100.;
        }
        changed = state.trackerGridZoom != zoom;
        state.trackerGridZoom = zoom;
    } else {
        services.message("Unknown VIEW setting. Use view follow, source, zoom, notes, jump, detail, resume, or status.", true);
        return;
    }
    state.status = viewSummary(state);
    services.message(state.status, false);
    if (changed && persistent) services.commitDocument();
    services.refreshUI();
}
} // namespace

const std::vector<CommandHelpSection>& clapCommandHelpSections()
{
    static const auto sections = [] {
        auto result = CommandEngine::helpSections();
        result.insert(result.begin(), { "VIEW / PLAYBACK FOLLOW", {
            { "view [status]", "Report view settings without changing the project or MIDI playback.", "view", "view" },
            { "view follow <static|center|page>", "Choose a static grid, centered NOTE cursor, or aligned pages of up to 16 rows.", "", "view follow center" },
            { "view source <selected|lane|@alias>", "Choose the selected editing lane or pin an existing one-based NOTE lane. Does not arm recording or enable follow.", "", "view source 1" },
            { "view resume", "Release manual follow hold and leave the main Live Code field safely. STATIC remains off.", "", "view resume" },
            { "view zoom <55..180|+|-|reset>", "Set grid zoom in whole percent (optional %), step like the VIEW buttons, or reset to 100%. Not whole-window scaling; not saved.", "", "view zoom 125" },
            { "view notes <name|midi>", "Set saved NOTE display format without changing stored pitches.", "", "view notes name" },
            { "view jump <1..16>", "Set the saved Up/Down and MIDI step-recording row increment. Quick NOTE actions still advance one row.", "", "view jump 4" },
            { "view detail <on|off>", "Show or hide both SEQ/value pairs and GATE. Temporary editor setting; does not clear cells.", "", "view detail on" },
        } });
        return result;
    }();
    return sections;
}
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
    if (!words.empty() && words.front() == "view") {
        executeViewCommand(state, words, services);
        return;
    }
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
