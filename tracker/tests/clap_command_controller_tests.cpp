#include "s3g/tracker/clap_command_controller.h"
#include "s3g/tracker/clap_document_controller.h"
#include <iostream>
#include <vector>
using namespace s3g::tracker;
int main()
{
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        if (!ok) { ++failures; std::cerr << label << '\n'; }
    };
    app::TrackerViewState state;
    state.session.pattern = state.patternBank.entries.front().pattern;
    std::vector<std::string> calls;
    std::string message;
    bool hostSupportsTransport = false, variationAccepted = false;
    ClapCommandServices services;
    services.message = [&](const std::string& text, bool error) {
        calls.push_back(error ? "error" : "message"); message = text;
    };
    services.showHelp = [&] { calls.push_back("help"); };
    services.undo = [&] { calls.push_back("undo"); };
    services.redo = [&] { calls.push_back("redo"); };
    services.commitRuntime = [&] { calls.push_back("runtime"); };
    services.commitDocument = [&] { calls.push_back("document"); };
    services.refreshWarps = [&] { calls.push_back("warps"); };
    services.updateMonitor = [&] { calls.push_back("monitor"); };
    services.refreshUI = [&] { calls.push_back("refresh"); };
    services.panic = [&] { calls.push_back("panic"); };
    services.requestHostContinue = [&] { calls.push_back("play"); return hostSupportsTransport; };
    services.requestHostStop = [&] { calls.push_back("stop"); return hostSupportsTransport; };
    services.installVariation = [&](const PatternVariationRequest&) {
        calls.push_back("variation");
        // Simulate a platform handover failing after consuming RNG state.
        if (!variationAccepted) state.session.commandRngState = 123;
        return variationAccepted;
    };
    auto run = [&](const char* command) {
        calls.clear(); message.clear(); executeClapCommand(state, command, services);
    };
    for (const auto* command : {"help", "?", "HELP"}) {
        run(command);
        check(calls == std::vector<std::string>{"help", "message"}, "help opens before console feedback");
    }
    for (const auto* command : {"bpm 100", "INST 1 2", "len 1 ins 16", "fx1 1 synth.gain .5"}) {
        const auto seed = state.session.commandRngState;
        run(command);
        check(calls == std::vector<std::string>{"error"} && state.session.commandRngState == seed,
            "MIDI-only policy rejects without publication/host effects");
    }
    run("actions");
    check(calls == std::vector<std::string>{"message"} && message.find("CC0–CC127") != message.npos,
        "MIDI sequencer action reference retained");
    run("undo"); check(calls == std::vector<std::string>{"undo"}, "undo delegates once");
    run("redo"); check(calls == std::vector<std::string>{"redo"}, "redo delegates once");

    // Match the original coordinator's effect ordering against the unchanged
    // engine for pattern, project-only, selection, transport and panic cases.
    for (const auto* command : {"kit superior basic", "alias k 1", "aliases", "select 1 3",
             "len 1 16", "fx 1 1 1 CC74 64", "warp on", "play", "stop", "panic", "not-a-command"}) {
        auto expectedSession = state.session;
        const auto result = CommandEngine::execute(expectedSession, command);
        std::vector<std::string> expected {result.ok ? "message" : "error"};
        if (result.ok) {
            if (result.hasEffect(CommandEffect::ProjectChanged)) expected.push_back("warps");
            if (result.hasEffect(CommandEffect::PatternChanged) || result.hasEffect(CommandEffect::TransportChanged)
                || result.hasEffect(CommandEffect::OutputChanged) || result.hasEffect(CommandEffect::RoutingChanged))
                expected.push_back("runtime");
            else if (result.hasEffect(CommandEffect::ProjectChanged)) expected.push_back("document");
            if (result.hasEffect(CommandEffect::StartPlayback)) { expected.push_back("play"); expected.push_back("error"); }
            if (result.hasEffect(CommandEffect::StopPlayback)) { expected.push_back("stop"); expected.push_back("error"); }
            if (result.hasEffect(CommandEffect::Panic)) expected.push_back("panic");
            expected.push_back("monitor"); expected.push_back("refresh");
        }
        run(command);
        check(calls == expected, command);
        check(state.session.commandRngState == expectedSession.commandRngState
                && state.session.selectedRow == expectedSession.selectedRow,
            "coordinator preserves engine RNG and selection");
    }
    hostSupportsTransport = true;
    run("play");
    check(calls == std::vector<std::string>{"message", "play", "monitor", "refresh"},
        "supported host transport does not synthesize an error");
    state.session.pattern.tracks.front().noteColumn.phase = 3;
    // The recording-only commit callback above deliberately does not mutate
    // the bank; seed it before constructing the inactive-pattern fixture.
    syncSessionToActivePattern(state);
    auto second = state.patternBank.entries.front();
    second.id = "A02"; second.pattern.tracks.front().velocityColumn.phase = 7;
    state.patternBank.entries.push_back(second);
    run("PHASE reset bank");
    check(calls == std::vector<std::string>{"message", "runtime"}
            && state.session.pattern.tracks.front().noteColumn.phase == 0
            && state.patternBank.entries.back().pattern.tracks.front().velocityColumn.phase == 0,
        "bank phase reset synchronizes active/inactive patterns and publishes once");
    run("ph reset bank");
    check(calls == std::vector<std::string>{"message", "refresh"}, "no-op phase reset avoids publication");

    const auto seed = state.session.commandRngState;
    run("variation generateseed orchard 0.48 0.55 0.18 launch beat");
    check(calls == std::vector<std::string>{"message", "variation", "error"}
            && state.session.commandRngState == seed, "failed variation handover restores RNG");
    variationAccepted = true;
    run("variation generateseed orchard 0.48 0.55 0.18 launch beat");
    check(calls == std::vector<std::string>{"message", "variation"},
        "variation adapter alone owns publication/history, not a second commit");
    state.songPlaybackEnabled = true;
    run("variation generateseed orchard 0.48 0.55 0.18 launch beat");
    check(calls == std::vector<std::string>{"error"}, "Song ownership blocks quantized variations");
    state.songPlaybackEnabled = false;
    state.patternBank.entries.resize(kMaximumPatternBankEntries);
    run("variation generateseed orchard 0.48 0.55 0.18");
    check(calls == std::vector<std::string>{"error"}, "full bank blocks variation before generation");
    state.patternBank.entries.resize(2);
    state.session.pattern.tracks.front().notes[0] = NoteCell::withBurst(0, state.activeBurstBankId);
    state.patternBank.entries.back().pattern.tracks.front().notes[0]
        = NoteCell::withBurst(0, state.activeBurstBankId);
    refreshProjectBurstUsageCounts(state);
    check(state.session.projectBurstUsageCounts[0] == 2, "whole-project Burst reference accounting");
    state.session.pattern.tracks.resize(4);
    state.trackerFollow = { TrackerFollowMode::Center, false, 2 };
    run("track remove 1");
    check(state.trackerFollow.lane == 1 && state.trackerFollow.mode == TrackerFollowMode::Center,
        "removing an earlier lane remaps the pinned follow source");
    state.session.aliases["follow"] = 1;
    run("track remove @follow");
    check(state.trackerFollow.mode == TrackerFollowMode::Static,
        "deleting a pinned source must disable follow instead of selecting its neighbour");
    state.trackerFollow = { TrackerFollowMode::Page, false, 0 };
    run("track remove 99");
    check(state.trackerFollow.mode == TrackerFollowMode::Page && state.trackerFollow.lane == 0,
        "rejected deletion changed follow settings");
    std::cout << checks << " CLAP command checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
