#include "s3g/tracker/command.h"
#include "s3g/tracker/fx_catalog.h"
#include "s3g/tracker/timing_playback_scheduler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

namespace {

using s3g::tracker::CommandEffect;
using s3g::tracker::CommandEngine;
using s3g::tracker::Direction;
using s3g::tracker::EventDestination;
using s3g::tracker::FxActionCellState;
using s3g::tracker::FxValueCellState;
using s3g::tracker::kMidiOutInstrumentNode;
using s3g::tracker::kStereoSamplerInstrumentNode;
using s3g::tracker::NoteCell;
using s3g::tracker::NoteCellState;
using s3g::tracker::Pattern;
using s3g::tracker::PatternVariationLaunch;
using s3g::tracker::patternVariationLaunchIsDue;
using s3g::tracker::ScheduledEvent;
using s3g::tracker::ScheduledEventKind;
using s3g::tracker::Sequencer;
using s3g::tracker::Track;
using s3g::tracker::TrackerSession;
using s3g::tracker::TimingWarpKind;
using s3g::tracker::TimingPlaybackScheduler;
using s3g::tracker::ValueCellState;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

TrackerSession makeSession(std::size_t trackCount = 2u)
{
    TrackerSession session;
    session.pattern.name = "test";
    session.pattern.visibleRows = 4u;
    for (std::size_t index = 0u; index < trackCount; ++index) {
        Track track;
        track.name = "Track " + std::to_string(index + 1u);
        track.notes.resize(4u, NoteCell::rest());
        track.velocities.resize(4u);
        track.noteColumn.length = 4u;
        track.velocityColumn.length = 4u;
        session.pattern.tracks.push_back(std::move(track));
    }
    return session;
}

std::string sessionFingerprint(const TrackerSession& session)
{
    std::ostringstream stream;
    stream << session.pattern.name << '|' << session.pattern.visibleRows
           << '|' << session.transport.sampleRate << '|'
           << session.transport.bpm << '|' << session.transport.ticksPerBeat
           << '|' << session.transport.swing << '|'
           << session.transport.warpCycleTicks << '|'
           << session.gateMilliseconds << '|' << session.selectedTrack << '|'
           << session.selectedRow << '|' << session.selectedPage << '|'
           << session.selectedField << '|' << session.commandRngState << '|'
           << session.playbackSeed;
    for (std::size_t index = 0u;
         index < session.transport.timingWarp.size(); ++index) {
        const auto* warp = session.transport.timingWarp.transform(index);
        if (!warp) continue;
        stream << "|w:" << static_cast<unsigned int>(warp->kind) << ':'
               << warp->exponent << ':' << warp->pulses << ':'
               << warp->steps << ':' << warp->options.phaseBegin << ':'
               << warp->options.phaseEnd << ':'
               << warp->options.repetitions << ':' << warp->options.alpha;
    }
    for (std::size_t index = 0u;
         index < s3g::tracker::kMaximumTimingWarpLibraryEntries; ++index) {
        const auto* entry = session.warpLibrary.entry(index);
        if (!entry) continue;
        stream << "|wl:" << index << ':' << entry->name << ':'
               << entry->cycleTicks << ':' << entry->stack.size();
    }
    for (const auto& [name, lane] : session.aliases)
        stream << "|a:" << name << ':' << lane;
    for (const auto note : session.laneDefaultNotes)
        stream << "|d:" << static_cast<unsigned int>(note);
    for (const auto& track : session.pattern.tracks) {
        stream << "|t:" << track.name << ':'
               << static_cast<unsigned int>(track.midiChannel) << ':'
               << static_cast<unsigned int>(track.destination) << ':'
               << track.initialInstrumentNodeId << ':'
               << track.chokeGroup << ':' << track.velocityScale << ':'
               << track.noteColumn.length << ':' << track.noteColumn.stride
               << ':' << track.noteColumn.phase
               << ':' << static_cast<unsigned int>(track.noteColumn.direction)
               << ':' << track.noteColumn.muted << ':'
               << track.instrumentColumn.length << ':'
               << track.instrumentColumn.stride << ':'
               << track.instrumentColumn.phase << ':'
               << static_cast<unsigned int>(
                      track.instrumentColumn.direction)
               << ':' << track.instrumentColumn.muted << ':'
               << track.velocityColumn.length << ':'
               << track.velocityColumn.stride << ':'
               << track.velocityColumn.phase << ':'
               << static_cast<unsigned int>(
                      track.velocityColumn.direction)
               << ':' << track.velocityColumn.muted;
        for (const auto& cell : track.notes)
            stream << "|n:" << static_cast<unsigned int>(cell.state) << ':'
                   << static_cast<unsigned int>(cell.note);
        for (const auto& cell : track.instruments)
            stream << "|i:" << static_cast<unsigned int>(cell.state) << ':'
                   << cell.nodeId;
        for (const auto& cell : track.velocities)
            stream << "|v:" << static_cast<unsigned int>(cell.state) << ':'
                   << cell.normalized;
        for (const auto& pair : track.fxPairs) {
            stream << "|fa:" << pair.actionColumn.length << ':'
                   << pair.actionColumn.stride << ':'
                   << pair.actionColumn.phase << ':'
                   << static_cast<unsigned int>(pair.actionColumn.direction)
                   << ':' << pair.actionColumn.muted
                   << "|fv:" << pair.valueColumn.length << ':'
                   << pair.valueColumn.stride << ':'
                   << pair.valueColumn.phase << ':'
                   << static_cast<unsigned int>(pair.valueColumn.direction)
                   << ':' << pair.valueColumn.muted;
            for (const auto& cell : pair.actions)
                stream << "|a:" << static_cast<unsigned int>(cell.state)
                       << ':' << cell.targetNode << ':' << cell.parameterId
                       << ':' << static_cast<unsigned int>(cell.scope)
                       << ':' << static_cast<unsigned int>(
                              cell.sequencerAction);
            for (const auto& cell : pair.values)
                stream << "|x:" << static_cast<unsigned int>(cell.state)
                       << ':' << cell.normalized;
        }
    }
    return stream.str();
}

void checkRejectedWithoutMutation(TrackerSession& session,
    const std::string& command, const std::string& message)
{
    const auto before = sessionFingerprint(session);
    const auto result = CommandEngine::execute(session, command);
    check(!result.ok && sessionFingerprint(session) == before, message);
}

std::string activeNoteMask(const Track& track)
{
    std::string result;
    for (std::size_t row = 0u; row < track.noteColumn.length; ++row) {
        result += track.notes[row].state == NoteCellState::Note ? 'x' : '-';
    }
    return result;
}

std::string noteFingerprint(const TrackerSession& session)
{
    std::ostringstream stream;
    for (const auto& track : session.pattern.tracks) {
        for (const auto& cell : track.notes) {
            stream << static_cast<unsigned int>(cell.state) << ':'
                   << static_cast<unsigned int>(cell.note) << '|';
        }
    }
    return stream.str();
}

std::string valueFingerprint(const TrackerSession& session)
{
    std::ostringstream stream;
    for (const auto& track : session.pattern.tracks) {
        for (const auto& cell : track.velocities)
            stream << static_cast<unsigned int>(cell.state) << ':'
                   << cell.normalized << '|';
        for (const auto& pair : track.fxPairs) {
            for (const auto& cell : pair.values)
                stream << static_cast<unsigned int>(cell.state) << ':'
                       << cell.normalized << '|';
        }
    }
    return stream.str();
}

std::string actionFingerprint(const TrackerSession& session)
{
    std::ostringstream stream;
    for (const auto& track : session.pattern.tracks) {
        for (const auto& cell : track.instruments)
            stream << static_cast<unsigned int>(cell.state) << ':'
                   << cell.nodeId << '|';
        for (const auto& pair : track.fxPairs) {
            for (const auto& cell : pair.actions) {
                stream << static_cast<unsigned int>(cell.state) << ':'
                       << cell.targetNode << ':' << cell.parameterId << ':'
                       << static_cast<unsigned int>(cell.sequencerAction)
                       << '|';
            }
        }
    }
    return stream.str();
}

std::string structureFingerprint(const TrackerSession& session)
{
    std::ostringstream stream;
    const auto append = [&](const auto& column) {
        stream << column.length << ':' << column.stride << ':'
               << column.phase << ':'
               << static_cast<unsigned int>(column.direction) << ':'
               << column.muted << '|';
    };
    for (const auto& track : session.pattern.tracks) {
        append(track.noteColumn);
        append(track.instrumentColumn);
        append(track.velocityColumn);
        for (const auto& pair : track.fxPairs) {
            append(pair.actionColumn);
            append(pair.valueColumn);
        }
    }
    return stream.str();
}

void testTransportActionsAndHelp()
{
    auto session = makeSession();
    auto result = CommandEngine::execute(session, " play \t");
    check(result.ok && result.hasEffect(CommandEffect::StartPlayback),
        "play should report a structured start action");
    result = CommandEngine::execute(session, "stop");
    check(result.ok && result.hasEffect(CommandEffect::StopPlayback),
        "stop should report a structured stop action");
    result = CommandEngine::execute(session, "PANIC");
    check(result.ok && result.hasEffect(CommandEffect::Panic),
        "verbs should be ASCII case-insensitive");
    result = CommandEngine::execute(session, "undo");
    check(result.ok && result.hasEffect(CommandEffect::UndoRequested)
            && !result.hasEffect(CommandEffect::ProjectChanged),
        "undo should request coordinator history without mutating the session");
    result = CommandEngine::execute(session, "redo");
    check(result.ok && result.hasEffect(CommandEffect::RedoRequested)
            && !result.hasEffect(CommandEffect::ProjectChanged),
        "redo should request coordinator history without mutating the session");
    check(!CommandEngine::execute(session, "undo now").ok,
        "undo should reject trailing arguments");
    result = CommandEngine::execute(session, "loop rows 3 12");
    check(result.ok && result.hasEffect(CommandEffect::TransportChanged)
            && session.transport.loopStartRow == 2u
            && session.transport.loopEndRow == 12u,
        "loop rows should store one global inclusive GUI region");
    result = CommandEngine::execute(session, "loop on");
    check(result.ok && session.transport.loopEnabled,
        "loop on should enable global wrapping");
    check(!CommandEngine::execute(session, "loop 12 3").ok,
        "loop regions should reject reversed row bounds");
    result = CommandEngine::execute(session, "help");
    check(result.ok && result.effects == CommandEffect::None
            && result.message.find("mask") != std::string::npos
            && result.message.find("SEQUENCING COLUMNS") != std::string::npos
            && result.message.find("COMPACT SYMBOL REFERENCE")
                != std::string::npos
            && result.message.find("! = 1.00, + = 0.85")
                != std::string::npos
            && result.message.find("A standalone - always means no authored event")
                != std::string::npos
            && result.message.find("A standalone = always recalls or holds")
                != std::string::npos
            && result.message.find("value marks and digits are rejected")
                != std::string::npos
            && result.message.find("Example: kit superior compact")
                != std::string::npos
            && result.message.find("instrument") == std::string::npos
            && result.message.find("bpm <") == std::string::npos,
        "help should return commands plus an unambiguous compact-symbol reference");
    check(!CommandEngine::execute(session, "play now").ok,
        "transport commands should reject trailing arguments");

    auto exampleSession = makeSession();
    check(CommandEngine::execute(
              exampleSession, "kit superior compact").ok
            && CommandEngine::execute(
                exampleSession, "warp save 1 EXAMPLE").ok,
        "Help example validation fixture should provide aliases and a warp slot");
    for (const auto& section : CommandEngine::helpSections()) {
        for (const auto& entry : section.entries) {
            auto probe = exampleSession;
            check(!entry.example.empty()
                    && CommandEngine::execute(probe, entry.example).ok,
                "every Help command should provide a valid console example row");
        }
    }
}

void testHelpCatalogCoversAuditedParserVerbs()
{
    // Keep this set beside the parser tests: adding an executeTokens verb or
    // alias requires a deliberate help-catalog update instead of leaving an
    // undiscoverable console feature behind. `@` represents alias-first
    // queries, masks, direction, and operation shorthand.
    const std::set<std::string> auditedParserVerbs {
        "@", "?", "accent", "actions", "alias", "aliases", "autoalias", "delay", "demo",
        "density", "dir", "drumscene", "e", "eu", "euclid", "euclidfx", "f1", "f2", "fill", "flam", "fx", "fx1", "fx2", "fxv", "fxvalue", "generate", "generateseed",
        "gate", "help", "hit", "hold", "kill", "kit",
        "ghost", "humanize", "len", "length", "loop", "mask", "micro", "microtime", "mode", "mute", "name", "note",
        "mutate", "panic", "play", "rand", "random", "randomize", "repeat", "rest", "reverse", "rot",
        "repeatprev", "retrigger", "retrig", "rotate", "rotatehits", "scene", "select", "sieve", "skip", "solo", "spd", "speed", "stop", "stutter",
        "stride", "swing", "thin", "undo", "redo", "unmute", "vel", "velseq", "vol", "warp", "phase", "ph", "prob", "probability", "ratchet", "offset",
        "variation", "vary", "warps", "track",
    };

    std::set<std::string> documentedVerbs;
    const auto& sections = CommandEngine::helpSections();
    check(!sections.empty(), "help catalog should contain logical sections");
    const auto rendered = CommandEngine::helpText();
    std::size_t documentedCommands = 0u;
    for (const auto& section : sections) {
        check(!section.title.empty() && !section.entries.empty(),
            "every help section should have a title and entries");
        for (const auto& entry : section.entries) {
            ++documentedCommands;
            check(!entry.syntax.empty() && !entry.description.empty()
                    && !entry.example.empty(),
                "every help entry should have syntax, description, and example");
            check(rendered.find(std::string(entry.syntax))
                    != std::string::npos
                    && rendered.find("Example: "
                            + std::string(entry.example))
                        != std::string::npos,
                "console help text should be generated from every catalog entry");
            std::istringstream words { std::string(entry.acceptedVerbs) };
            std::string verb;
            while (words >> verb) {
                check(documentedVerbs.insert(verb).second,
                    "each accepted command spelling should be cataloged once");
            }
        }
    }
    std::size_t renderedExamples = 0u;
    for (std::size_t position = 0u;
         (position = rendered.find("    Example: ", position))
            != std::string::npos;
         position += 13u) {
        ++renderedExamples;
    }
    check(renderedExamples == documentedCommands,
        "console help should render exactly one example per command entry");
    check(documentedVerbs == auditedParserVerbs,
        "help catalog should cover every current MIDI-product command spelling");
    for (const auto& verb : auditedParserVerbs) {
        auto probe = makeSession();
        const auto result = CommandEngine::execute(probe,
            verb == "@" ? "@missing" : verb);
        check(result.ok
                || result.message.find("Unknown command")
                    == std::string::npos,
            "every cataloged spelling should still be recognized by the parser");
    }
}

void testTempoAndSwing()
{
    auto session = makeSession();
    const double hostTempo = session.transport.bpm;
    auto result = CommandEngine::execute(session, "bpm 137.5");
    check(!result.ok && result.message.find("Unknown command")
                != std::string::npos
            && session.transport.bpm == hostTempo,
        "BPM commands should not override the CLAP host tempo");
    result = CommandEngine::execute(session, "swing 62");
    check(result.ok && std::abs(session.transport.swing - 0.62) < 1.0e-9,
        "swing should accept a percentage");
    result = CommandEngine::execute(session, "swing .7");
    check(result.ok && std::abs(session.transport.swing - 0.7) < 1.0e-9,
        "swing should accept a normalized value");
    const auto previous = session.transport;
    check(!CommandEngine::execute(session, "swing 49").ok
            && session.transport.swing == previous.swing,
        "out-of-range swing must fail without changing state");
    auto gate = CommandEngine::execute(session, "gate 72.5");
    check(gate.ok && gate.hasEffect(CommandEffect::OutputChanged)
            && session.gateMilliseconds == 72.5,
        "gate should update the MIDI output policy");
    check(!CommandEngine::execute(session, "gate 0").ok
            && session.gateMilliseconds == 72.5,
        "an invalid gate must not mutate output state");
}

void testTimingWarpCommands()
{
    auto session = makeSession();
    auto result = CommandEngine::execute(session, "warp cycle 5");
    check(result.ok && result.hasEffect(CommandEffect::TransportChanged)
            && session.transport.warpCycleTicks == 5u,
        "warp cycle should set the normalized timing period");

    result = CommandEngine::execute(session,
        "warp exp 1.25 mix .5 segment .25 .75 repeat 2");
    const auto* first = session.transport.timingWarp.transform(0u);
    check(result.ok && first
            && first->kind == TimingWarpKind::Exponential
            && std::abs(first->exponent - 1.25) < 1.0e-12
            && std::abs(first->options.alpha - 0.5) < 1.0e-12
            && std::abs(first->options.phaseBegin - 0.25) < 1.0e-12
            && std::abs(first->options.phaseEnd - 0.75) < 1.0e-12
            && first->options.repetitions == 2u,
        "warp exp should retain mix, segment, and repeat options");

    result = CommandEngine::execute(session, "warp eu 2 5 mix .3");
    const auto* second = session.transport.timingWarp.transform(1u);
    check(result.ok && second
            && second->kind == TimingWarpKind::EuclideanQuantize
            && second->pulses == 2u && second->steps == 5u,
        "warp eu should append a serial Euclidean timing transform");
    result = CommandEngine::execute(session, "warps");
    check(result.ok && result.message.find("2 transforms")
                != std::string::npos
            && result.message.find("eu 2/5") != std::string::npos,
        "warps should expose a readable compiled-stack summary");

    result = CommandEngine::execute(session,
        "warp save 7 Broken Quintuplet");
    const auto* saved = session.warpLibrary.entry(6u);
    check(result.ok && result.hasEffect(CommandEffect::ProjectChanged)
            && result.hasEffect(CommandEffect::TransportChanged)
            && saved && saved->name == "Broken Quintuplet"
            && saved->cycleTicks == 5u && saved->stack.size() == 2u,
        "warp save should store the complete composition at a stable index");
    check(CommandEngine::execute(session, "warp clear").ok
            && CommandEngine::execute(session, "warp cycle 4").ok
            && CommandEngine::execute(session, "warp load 7").ok
            && session.transport.warpCycleTicks == 5u
            && session.transport.timingWarp.size() == 2u,
        "warp load should recall stack and cycle as one composition");
    result = CommandEngine::execute(session, "wrp 1 2 7");
    check(!result.ok,
        "warp recall must not remain available as a lane-local command");
    check(CommandEngine::execute(session,
            "warp rename 7 Five Against Four").ok
            && session.warpLibrary.entry(6u)->name == "Five Against Four"
            && CommandEngine::execute(session, "warp delete 7").ok
            && !session.warpLibrary.entry(6u),
        "warp library entries should support rename and deletion");

    checkRejectedWithoutMutation(session, "warp exp 0",
        "an invalid exponent must not partially change transport");
    checkRejectedWithoutMutation(session, "warp step 8 segment .7 .2",
        "a reversed console segment must be rejected transactionally");
    checkRejectedWithoutMutation(session, "warp eu 6 5",
        "a timing Euclid with too many pulses must be rejected");

    result = CommandEngine::execute(session, "warp clear");
    check(result.ok && session.transport.timingWarp.empty(),
        "warp clear should restore the identity stack");

    checkRejectedWithoutMutation(session, "warp cycle 64",
        "the live command path must reject an unsafe long continuous cycle");
    check(CommandEngine::execute(session, "warp cycle 16").ok
            && CommandEngine::execute(session, "warp exp 64").ok
            && CommandEngine::execute(session, "warp step 1").ok,
        "extreme continuous and stepped warps should fit the bounded live cycle");
    checkRejectedWithoutMutation(session, "warp cycle 17",
        "the eight-lane live event budget must cap every warp kind");

    auto imported = makeSession();
    imported.transport.warpCycleTicks = 1024u;
    checkRejectedWithoutMutation(imported, "warp exp 64",
        "a pre-existing offline cycle must be made live-safe before command append");
}

void testDynamicTrackCommands()
{
    auto session = makeSession(2u);
    check(CommandEngine::execute(session, "alias second 2").ok,
        "dynamic-track test should establish an alias");
    auto result = CommandEngine::execute(session, "track add Chip Lead");
    check(result.ok && result.hasEffect(CommandEffect::PatternChanged)
            && result.hasEffect(CommandEffect::SelectionChanged)
            && session.pattern.tracks.size() == 3u
            && session.selectedTrack == 2u
            && session.pattern.tracks[2u].name == "Chip Lead"
            && session.pattern.tracks[2u].initialInstrumentNodeId
                == kMidiOutInstrumentNode,
        "track add should append and select a MIDI OUT lane");
    result = CommandEngine::execute(session, "track remove 1");
    check(result.ok && session.pattern.tracks.size() == 2u
            && session.aliases.at("second") == 0u,
        "track remove should shift surviving aliases");
    check(CommandEngine::execute(session, "track remove 2").ok,
        "track remove should allow reducing a pattern to one lane");
    checkRejectedWithoutMutation(session, "track remove 1",
        "track remove must retain one lane");
    for (std::size_t index = 1u; index < 32u; ++index)
        check(CommandEngine::execute(session, "track add").ok,
            "track add should support the advertised 32-lane limit");
    checkRejectedWithoutMutation(session, "track add Overflow",
        "track add must reject lane 33 atomically");
}

void testSelectionAndLaneControls()
{
    auto session = makeSession();
    auto result = CommandEngine::execute(session, "select lane 2");
    check(result.ok && result.hasEffect(CommandEffect::SelectionChanged)
            && session.selectedTrack == 1u,
        "selection should use one-based lane addressing");
    result = CommandEngine::execute(session, "select 1 3");
    check(result.ok && session.selectedTrack == 0u
            && session.selectedRow == 2u,
        "selection may identify both a lane and row");
    result = CommandEngine::execute(session, "len 1 12");
    check(result.ok && session.pattern.tracks[0].noteColumn.length == 12u
            && session.pattern.tracks[0].notes.size() == 12u
            && session.pattern.visibleRows == 12u,
        "length should grow note storage and the visible row count");
    result = CommandEngine::execute(session, "stride 1 5");
    check(result.ok && session.pattern.tracks[0].noteColumn.stride == 5u,
        "stride should update the note column");
    result = CommandEngine::execute(session, "phase 1 -1");
    check(result.ok && session.pattern.tracks[0].noteColumn.phase == 11u,
        "phase should normalize signed rotations against the target length");
    result = CommandEngine::execute(session, "ph 1 vel 2");
    check(result.ok && session.pattern.tracks[0].velocityColumn.phase == 2u,
        "phase should address each polymetric column independently");
    result = CommandEngine::execute(session, "len 1 4");
    check(result.ok && session.pattern.tracks[0].noteColumn.phase == 3u,
        "shrinking a column should keep its authored phase normalized");
    result = CommandEngine::execute(session, "dir 1 palindrome");
    check(result.ok
            && session.pattern.tracks[0].noteColumn.direction
                == Direction::Palindrome,
        "direction should support palindrome");
    result = CommandEngine::execute(session, "mute 1 toggle");
    check(result.ok && session.pattern.tracks[0].noteColumn.muted,
        "mute toggle should invert state");
    result = CommandEngine::execute(session, "mute 1 off");
    check(result.ok && !session.pattern.tracks[0].noteColumn.muted,
        "mute off should clear state");
    check(!CommandEngine::execute(session, "select 0").ok
            && session.selectedTrack == 0u && session.selectedRow == 2u,
        "lane zero must be rejected without changing selection");
    check(!CommandEngine::execute(session, "dir 1 sideways").ok
            && session.pattern.tracks[0].noteColumn.direction
                == Direction::Palindrome,
        "invalid direction must preserve state");
}

void testNoteAndVelocityEdits()
{
    auto session = makeSession();
    auto result = CommandEngine::execute(session, "note 2 3 64");
    check(result.ok
            && session.pattern.tracks[1].notes[2].state
                == NoteCellState::Note
            && session.pattern.tracks[1].notes[2].note == 64u,
        "note should write a MIDI pitch at one-based coordinates");
    check(CommandEngine::execute(session, "note 2 1 rpt").ok
            && session.pattern.tracks[1].notes[0].state
                == NoteCellState::RetriggerPrevious,
        "note should support retrigger cells");
    check(CommandEngine::execute(session, "note 2 2 kill").ok
            && session.pattern.tracks[1].notes[1].state
                == NoteCellState::Kill,
        "note should support kill cells");
    check(CommandEngine::execute(session, "note 2 2 hold").ok
            && session.pattern.tracks[1].notes[1].state
                == NoteCellState::Hold,
        "note should support hold cells");
    check(CommandEngine::execute(session, "note 2 3 rest").ok
            && session.pattern.tracks[1].notes[2].state
                == NoteCellState::Rest,
        "note should support rest cells");
    result = CommandEngine::execute(session, "vel 1 7 96");
    check(result.ok && session.pattern.tracks[0].velocities.size() == 7u
            && session.pattern.tracks[0].velocityColumn.length == 7u
            && session.pattern.tracks[0].velocities[6].state
                == ValueCellState::Value
            && std::abs(session.pattern.tracks[0].velocities[6].normalized
                    - 96.0f / 127.0f)
                < 1.0e-6f,
        "velocity should grow storage and normalize an integer MIDI value");
    const auto before = session.pattern.tracks[0].notes;
    check(!CommandEngine::execute(session, "note 1 2 128").ok
            && session.pattern.tracks[0].notes.size() == before.size(),
        "invalid note values must not mutate storage");
    check(!CommandEngine::execute(session, "vel 1 1 3.5").ok,
        "velocity input should be an unambiguous integer");

    result = CommandEngine::execute(session, "hit 1 2 72");
    check(result.ok && session.pattern.tracks[0].notes[1].note == 72u,
        "hit should provide a concise explicit-note edit");
    check(CommandEngine::execute(session, "rest 1 2").ok
            && session.pattern.tracks[0].notes[1].state
                == NoteCellState::Rest,
        "rest should provide a concise cell edit");
    check(CommandEngine::execute(session, "repeat 1 2").ok
            && session.pattern.tracks[0].notes[1].state
                == NoteCellState::RetriggerPrevious,
        "repeat should map to retrigger-previous");
    check(CommandEngine::execute(session, "kill 1 2").ok
            && session.pattern.tracks[0].notes[1].state
                == NoteCellState::Kill,
        "kill should provide a concise cell edit");
    check(CommandEngine::execute(session, "hold 1 2").ok
            && session.pattern.tracks[0].notes[1].state
                == NoteCellState::Hold,
        "hold should provide a concise cell edit");
}

void testMasks()
{
    auto session = makeSession(1u);
    session.pattern.tracks[0].notes[2] = NoteCell::withNote(67u);
    auto result = CommandEngine::execute(session, "mask 1 x-x--X");
    const auto& track = session.pattern.tracks[0];
    check(result.ok && track.noteColumn.length == 6u
            && track.notes.size() == 6u,
        "mask should set lane length and grow storage");
    check(track.notes[0].state == NoteCellState::Note
            && track.notes[0].note == 67u,
        "a mask should use the lane's existing anchor pitch");
    check(track.notes[1].state == NoteCellState::Rest
            && track.notes[2].state == NoteCellState::Note
            && track.notes[2].note == 67u
            && track.notes[5].state == NoteCellState::Note,
        "mask x and - symbols should write hits and rests");

    auto emptyLane = makeSession(1u);
    result = CommandEngine::execute(emptyLane, "mask 1 x---");
    check(result.ok && emptyLane.pattern.tracks[0].notes[0].note == 36u,
        "a mask on an empty first lane should use General MIDI kick");
    const auto previousLength = emptyLane.pattern.tracks[0].noteColumn.length;
    check(!CommandEngine::execute(emptyLane, "mask 1 x.o.").ok
            && emptyLane.pattern.tracks[0].noteColumn.length == previousLength,
        "invalid mask characters must not change the lane");
}

void testKitsAliasesAndTargets()
{
    auto session = makeSession(1u);
    auto result = CommandEngine::execute(session, "kit superior compact");
    check(result.ok && result.hasEffect(CommandEffect::PatternChanged)
            && session.pattern.tracks.size() == 4u,
        "a compact kit should create its four native lanes");
    check(session.pattern.tracks[0].midiChannel == 1u
            && session.pattern.tracks[2].midiChannel == 1u
            && session.laneDefaultNotes[0] == 36u
            && session.laneDefaultNotes[2] == 61u,
        "the Superior kit should install its pitches and MIDI channel");
    check(session.aliases.at("k") == 0u
            && session.aliases.at("kick") == 0u
            && session.aliases.at("h") == 2u
            && session.aliases.at("open") == 3u,
        "kit aliases should include the concise and descriptive names");

    auto populated = makeSession(8u);
    result = CommandEngine::execute(populated, "kit superior compact");
    check(result.ok && populated.pattern.tracks.size() == 8u
            && std::none_of(populated.pattern.tracks.begin(),
                populated.pattern.tracks.begin() + 4,
                [](const Track& track) { return track.noteColumn.muted; })
            && std::all_of(populated.pattern.tracks.begin() + 4,
                populated.pattern.tracks.end(),
                [](const Track& track) { return track.noteColumn.muted; }),
        "a compact kit should preserve but mute populated lanes outside its template");

    result = CommandEngine::execute(session, "@h x---");
    check(result.ok && session.pattern.tracks[2].notes[0].note == 61u,
        "an empty Superior hat lane should retain pitch 61 for later masks");
    result = CommandEngine::execute(session, "aliases");
    const auto laneOneAliases = result.message.find("Lane 1 (Kick):");
    const auto laneThreeAliases = result.message.find(
        "Lane 3 (Closed Hat):");
    check(result.ok && laneOneAliases != std::string::npos
            && laneThreeAliases != std::string::npos
            && laneOneAliases < laneThreeAliases
            && result.message.find("@kick", laneOneAliases)
                != std::string::npos
            && result.message.find("@h", laneThreeAliases)
                != std::string::npos,
        "aliases should group all bindings in one-based lane order");

    result = CommandEngine::execute(session, "alias hats @h");
    check(result.ok && result.hasEffect(CommandEffect::ProjectChanged)
            && session.aliases.at("hats") == 2u,
        "alias should accept another alias and report a persistent edit");
    checkRejectedWithoutMutation(session, "@ghost = 2",
        "alias = shorthand should reject so = has only its previous/hold meaning");
    result = CommandEngine::execute(session, "alias ghost 2");
    check(result.ok && result.hasEffect(CommandEffect::ProjectChanged)
            && session.aliases.at("ghost") == 1u,
        "the alias command should assign one-based persistent bindings");
    result = CommandEngine::execute(session, "@ghost");
    check(result.ok && result.message == "@ghost -> 2",
        "alias queries should avoid reusing = as a binding symbol");
    result = CommandEngine::execute(session, "select @ghost 3");
    check(result.ok && session.selectedTrack == 1u
            && session.selectedRow == 2u,
        "all canonical target positions should resolve aliases");

    result = CommandEngine::execute(session, "kit gm toms");
    check(result.ok && session.pattern.tracks[0].midiChannel == 10u
            && session.laneDefaultNotes[0] == 45u
            && session.laneDefaultNotes[1] == 47u
            && session.laneDefaultNotes[2] == 50u
            && session.laneDefaultNotes[3] == 41u,
        "the GM tom kit should install the documented pitches and channel");
    check(CommandEngine::execute(session, "@mt x---").ok
            && session.pattern.tracks[1].notes[0].note == 47u,
        "a tom alias should use its kit-specific anchor pitch");

    result = CommandEngine::execute(session, "kit gm basic");
    check(result.ok && session.pattern.tracks.size() == 7u
            && session.laneDefaultNotes[2] == 45u
            && session.laneDefaultNotes[6] == 51u,
        "the basic GM template should provide all seven expected lanes");
    checkRejectedWithoutMutation(session, "kit ableton compact",
        "an unknown kit map should not partially rewrite tracks or aliases");
    checkRejectedWithoutMutation(session, "kit superior enormous",
        "an unknown kit template should leave the session untouched");
    checkRejectedWithoutMutation(session, "alias 2bad 1",
        "invalid alias names should not alter the alias table");
    checkRejectedWithoutMutation(session, "mask @missing x---",
        "unknown target aliases should fail without mutating a pattern");

    auto automatic = makeSession(5u);
    automatic.pattern.tracks[0u].name = "Kick";
    automatic.pattern.tracks[1u].name = "King";
    automatic.pattern.tracks[2u].name = "Kit";
    automatic.pattern.tracks[3u].name = "Snare";
    automatic.pattern.tracks[4u].name = "808";
    automatic.aliases["obsolete"] = 0u;
    result = CommandEngine::execute(automatic, "autoalias");
    check(result.ok && result.hasEffect(CommandEffect::ProjectChanged)
            && automatic.aliases.size() == 5u
            && automatic.aliases.at("k") == 0u
            && automatic.aliases.at("ki") == 1u
            && automatic.aliases.at("kit") == 2u
            && automatic.aliases.at("s") == 3u
            && automatic.aliases.at("lane5") == 4u
            && automatic.aliases.find("obsolete")
                == automatic.aliases.end(),
        "autoalias should rebuild shortest collision-free lane-name prefixes");
    const auto automaticList = CommandEngine::execute(
        automatic, "aliases");
    check(automaticList.ok
            && automaticList.message.find("Lane 1 (Kick): @k")
                < automaticList.message.find("Lane 2 (King): @ki")
            && automaticList.message.find("Lane 2 (King): @ki")
                < automaticList.message.find("Lane 3 (Kit): @kit")
            && automaticList.message.find("Lane 5 (808): @lane5")
                != std::string::npos,
        "automatic aliases should render in lane order rather than map order");
    const auto unchanged = CommandEngine::execute(automatic, "autoalias");
    check(unchanged.ok && unchanged.effects == CommandEffect::None,
        "repeating autoalias should avoid a redundant persistent edit");
    checkRejectedWithoutMutation(automatic, "autoalias now",
        "autoalias should reject trailing arguments transactionally");
}

void testCompactMasksAndColumnControls()
{
    auto session = makeSession(1u);
    check(CommandEngine::execute(session, "alias k 1").ok,
        "manual aliases should bind numeric one-based lanes");
    auto result = CommandEngine::execute(session, "@k x-x--- <>");
    const auto& track = session.pattern.tracks[0];
    check(result.ok && track.noteColumn.length == 6u
            && track.noteColumn.direction == Direction::Palindrome
            && activeNoteMask(track) == "x-x---",
        "compact masks should use only x/X for hits and - for rests");
    checkRejectedWithoutMutation(session, "mask @k x.x-",
        "a value dot must not double as a NOTE rest");
    checkRejectedWithoutMutation(session, "mask @k x1x-",
        "numeric one must not double as a NOTE hit");
    checkRejectedWithoutMutation(session, "mask @k x0x-",
        "numeric zero must not double as a NOTE rest");
    checkRejectedWithoutMutation(session, "mask @k x_x-",
        "underscore must not duplicate the NOTE rest symbol");

    const auto noteLength = track.noteColumn.length;
    result = CommandEngine::execute(session, "@k len vel 9");
    check(result.ok && session.pattern.tracks[0].velocityColumn.length == 9u
            && session.pattern.tracks[0].velocities.size() == 9u
            && session.pattern.tracks[0].noteColumn.length == noteLength,
        "alias-first length should address the velocity column independently");
    result = CommandEngine::execute(session, "stride @k vel 3");
    check(result.ok
            && session.pattern.tracks[0].velocityColumn.stride == 3u
            && session.pattern.tracks[0].noteColumn.stride == 1u,
        "canonical stride should accept an alias and a velocity field");
    result = CommandEngine::execute(session, "@k dir vel random");
    check(result.ok && session.pattern.tracks[0].velocityColumn.direction
                == Direction::Random
            && session.pattern.tracks[0].noteColumn.direction
                == Direction::Palindrome,
        "direction words should affect only the requested column");
    checkRejectedWithoutMutation(session, "dir @k vel ?",
        "question mark should be reserved for Help rather than random direction");
    checkRejectedWithoutMutation(session, "dir @k vel r",
        "one-letter direction aliases should reject as ambiguous shorthand");
    checkRejectedWithoutMutation(session, "dir @k vel f",
        "one-letter forward shorthand should reject as ambiguous");
    checkRejectedWithoutMutation(session, "dir @k vel b",
        "one-letter backward shorthand should reject as ambiguous");
    checkRejectedWithoutMutation(session, "dir @k vel p",
        "one-letter palindrome shorthand should reject as ambiguous");
    result = CommandEngine::execute(session, "@k >");
    check(result.ok && session.pattern.tracks[0].noteColumn.direction
                == Direction::Forward,
        "a direction-only alias shorthand should target NOTE");
    result = CommandEngine::execute(session, "speed @k note 2");
    check(result.ok && session.pattern.tracks[0].noteColumn.stride == 2u,
        "v8 speed terminology should be an alias for native stride");

    checkRejectedWithoutMutation(session, "@k x--- sideways",
        "a bad optional mask direction must reject the complete mask edit");
    result = CommandEngine::execute(session, "len @k fx1 8");
    check(result.ok
            && session.pattern.tracks[0].fxPairs[0].actionColumn.length == 8u,
        "FX action columns should support independent native lengths");
    checkRejectedWithoutMutation(session, "stride @k vel 0",
        "an invalid velocity stride should preserve both columns");
}

void testVelocitySequences()
{
    auto session = makeSession(1u);
    check(CommandEngine::execute(session, "alias k 1").ok,
        "velocity tests should establish an alias");
    auto result = CommandEngine::execute(session, "@k vel !.*-=");
    const auto& compact = session.pattern.tracks[0].velocities;
    check(result.ok && session.pattern.tracks[0].velocityColumn.length == 5u
            && compact[0].state == ValueCellState::Value
            && std::abs(compact[0].normalized - 1.0f) < 1.0e-6f
            && std::abs(compact[1].normalized - 0.55f) < 1.0e-6f
            && std::abs(compact[2].normalized - 0.70f) < 1.0e-6f
            && compact[3].state == ValueCellState::Default
            && compact[4].state == ValueCellState::Previous,
        "compact VOL symbols should share level, empty/default, and previous meanings");

    result = CommandEngine::execute(session, "vol @k 127,96,64,32");
    const auto& numeric = session.pattern.tracks[0].velocities;
    check(result.ok && session.pattern.tracks[0].velocityColumn.length == 4u
            && std::abs(numeric[0].normalized - 1.0f) < 1.0e-6f
            && std::abs(numeric[1].normalized - 96.0f / 127.0f)
                < 1.0e-6f
            && std::abs(numeric[3].normalized - 32.0f / 127.0f)
                < 1.0e-6f,
        "comma-separated MIDI velocities should normalize independently");
    result = CommandEngine::execute(session, "velseq @k .25 .5 1.0 =");
    check(result.ok && std::abs(session.pattern.tracks[0].velocities[0].normalized
                    - 0.25f)
                < 1.0e-6f
            && session.pattern.tracks[0].velocities[3].state
                == ValueCellState::Previous,
        "velocity sequences should also accept normalized values and words/symbols");

    result = CommandEngine::execute(session, "velseq @k 9");
    check(result.ok && session.pattern.tracks[0].velocities.size() >= 1u
            && std::abs(session.pattern.tracks[0].velocities[0].normalized
                    - 9.0f / 127.0f)
                < 1.0e-6f,
        "a single bare digit should be an unambiguous MIDI velocity");
    checkRejectedWithoutMutation(session, "@k vel !9",
        "digits inside symbolic VOL patterns must not acquire a compact scale");
    checkRejectedWithoutMutation(session, "@k vel !o",
        "letter o must not resemble zero or duplicate a value level");
    checkRejectedWithoutMutation(session, "@k vel !_",
        "underscore must not duplicate the previous/hold symbol");

    checkRejectedWithoutMutation(session, "@k vol !?+-",
        "unsupported random velocity must be explicit and transactional");
    checkRejectedWithoutMutation(session, "vol @k mute",
        "mute velocity must not masquerade as a silent MIDI trigger");
    checkRejectedWithoutMutation(session, "vol @k --",
        "the legacy mute symbol must be rejected until velocity can suppress notes");
    checkRejectedWithoutMutation(session, "vol @k 127,,64",
        "empty comma values should reject the whole velocity sequence");
    checkRejectedWithoutMutation(session, "vol @k 1.5",
        "out-of-range normalized decimals should not mutate velocity data");
}

void testRandomVelocityCommands()
{
    auto session = makeSession(2u);
    session.pattern.tracks[1].velocityColumn.length = 7u;
    auto replay = session;
    const auto untouched = session.pattern.tracks[0].velocities;
    auto result = CommandEngine::execute(session, "randomize 2 vel 40 96");
    const auto replayResult = CommandEngine::execute(
        replay, "rand 2 velocity 40 96");
    check(result.ok && replayResult.ok
            && result.hasEffect(CommandEffect::PatternChanged)
            && session.pattern.tracks[1].velocityColumn.length == 7u,
        "random velocity should target only the selected active VEL column");
    bool untouchedLane = untouched.size()
        == session.pattern.tracks[0].velocities.size();
    for (std::size_t row = 0u; untouchedLane && row < untouched.size(); ++row) {
        const auto& before = untouched[row];
        const auto& after = session.pattern.tracks[0].velocities[row];
        untouchedLane = before.state == after.state
            && before.normalized == after.normalized;
    }
    check(untouchedLane,
        "random velocity should leave every untargeted lane unchanged");
    bool inRange = true;
    bool deterministic = true;
    for (std::size_t row = 0u; row < 7u; ++row) {
        const auto& first = session.pattern.tracks[1].velocities[row];
        const auto& second = replay.pattern.tracks[1].velocities[row];
        const int midi = static_cast<int>(std::lround(
            first.normalized * 127.0f));
        inRange = inRange && first.state == ValueCellState::Value
            && midi >= 40 && midi <= 96;
        deterministic = deterministic && first.state == second.state
            && first.normalized == second.normalized;
    }
    check(inRange, "random velocity values must respect inclusive MIDI bounds");
    check(deterministic,
        "random velocity must be reproducible from the session RNG state");
    check(!CommandEngine::execute(session, "random 2 note").ok
            && !CommandEngine::execute(session, "random 2 vel 100 20").ok,
        "unsupported columns and reversed random bounds must reject");
}

void testWholePatternGenerationAndMutation()
{
    auto seeded = makeSession(3u);
    auto replay = seeded;
    const auto initialCommandRng = seeded.commandRngState;
    auto result = CommandEngine::execute(seeded,
        "generateseed orchard 1 0.5 0");
    const auto replayResult = CommandEngine::execute(replay,
        "generateseed orchard 1 0.5 0");
    check(result.ok && replayResult.ok
            && result.hasEffect(CommandEffect::PatternChanged)
            && sessionFingerprint(seeded) == sessionFingerprint(replay),
        "seeded whole-pattern generation should be exactly repeatable");
    check(seeded.commandRngState == initialCommandRng,
        "seeded generation must not consume the session command stream");
    const auto firstSeededPattern = sessionFingerprint(seeded);
    check(CommandEngine::execute(seeded,
              "generateseed orchard 1 0.5 0")
              .ok
            && sessionFingerprint(seeded) == firstSeededPattern,
        "repeating a seed on the already-generated pattern should reproduce it without anchor drift");

    const std::set<std::size_t> allowedLengths {
        5u, 7u, 8u, 9u, 11u, 13u, 16u, 21u,
    };
    bool columnsAreComplete = true;
    bool everyNoteIsGenerated = true;
    for (const auto& track : seeded.pattern.tracks) {
        const std::array<const s3g::tracker::ColumnDefinition*, 7u> columns {
            &track.noteColumn, &track.instrumentColumn,
            &track.velocityColumn, &track.fxPairs[0u].actionColumn,
            &track.fxPairs[0u].valueColumn,
            &track.fxPairs[1u].actionColumn,
            &track.fxPairs[1u].valueColumn,
        };
        for (const auto* column : columns) {
            columnsAreComplete = columnsAreComplete
                && allowedLengths.count(column->length) == 1u
                && column->stride >= 1u;
        }
        columnsAreComplete = columnsAreComplete
            && track.notes.size() >= track.noteColumn.length
            && track.instruments.size() >= track.instrumentColumn.length
            && track.velocities.size() >= track.velocityColumn.length
            && track.fxPairs[0u].actions.size()
                >= track.fxPairs[0u].actionColumn.length
            && track.fxPairs[0u].values.size()
                >= track.fxPairs[0u].valueColumn.length
            && track.fxPairs[1u].actions.size()
                >= track.fxPairs[1u].actionColumn.length
            && track.fxPairs[1u].values.size()
                >= track.fxPairs[1u].valueColumn.length;
        for (std::size_t row = 0u; row < track.noteColumn.length; ++row) {
            everyNoteIsGenerated = everyNoteIsGenerated
                && track.notes[row].state == NoteCellState::Note;
        }
    }
    check(columnsAreComplete && everyNoteIsGenerated,
        "whole-pattern generation should author all seven typed columns and their structures");

    auto differentSeed = makeSession(3u);
    check(CommandEngine::execute(differentSeed,
              "generateseed another 1 0.5 0")
              .ok
            && sessionFingerprint(differentSeed)
                != sessionFingerprint(seeded),
        "different explicit seeds should create different whole patterns");

    auto sparseScene = makeSession(2u);
    auto sparseEquivalent = sparseScene;
    check(CommandEngine::execute(sparseScene, "scene sparse 101").ok
            && CommandEngine::execute(sparseEquivalent,
                "generateseed 101 0.28 0.25 0.08")
                .ok
            && sessionFingerprint(sparseScene)
                == sessionFingerprint(sparseEquivalent),
        "named scenes should be exact seeded parameter presets");

    auto unseeded = makeSession(1u);
    const auto beforeUnseededRng = unseeded.commandRngState;
    check(CommandEngine::execute(unseeded, "generate 0.5 0.5 0.1").ok
            && unseeded.commandRngState != beforeUnseededRng,
        "unseeded generation should consume the persistent command stream");

    auto mutationBase = seeded;
    auto mutationReplay = mutationBase;
    const auto notesBefore = noteFingerprint(mutationBase);
    const auto actionsBefore = actionFingerprint(mutationBase);
    const auto valuesBefore = valueFingerprint(mutationBase);
    const auto structureBefore = structureFingerprint(mutationBase);
    result = CommandEngine::execute(mutationBase, "mutate 1 values");
    const auto replayMutation = CommandEngine::execute(
        mutationReplay, "mutate 1 values");
    check(result.ok && replayMutation.ok
            && sessionFingerprint(mutationBase)
                == sessionFingerprint(mutationReplay),
        "mutation should be repeatable from the same session RNG state");
    check(noteFingerprint(mutationBase) == notesBefore
            && actionFingerprint(mutationBase) == actionsBefore
            && structureFingerprint(mutationBase) == structureBefore
            && valueFingerprint(mutationBase) != valuesBefore,
        "values mutation should not leak into notes, actions, or structure");

    auto structureOnly = seeded;
    const auto structureNotes = noteFingerprint(structureOnly);
    const auto structureActions = actionFingerprint(structureOnly);
    const auto structureValues = valueFingerprint(structureOnly);
    const auto oldStructure = structureFingerprint(structureOnly);
    result = CommandEngine::execute(structureOnly, "mutate 1 structure");
    check(result.ok && structureFingerprint(structureOnly) != oldStructure
            && noteFingerprint(structureOnly) == structureNotes
            && actionFingerprint(structureOnly) == structureActions
            && valueFingerprint(structureOnly) == structureValues,
        "structure mutation should leave authored cell contents intact");

    checkRejectedWithoutMutation(seeded, "generate 1.1 0.5 0.2",
        "invalid generation controls must reject transactionally");
    checkRejectedWithoutMutation(seeded, "scene ambient 5",
        "unknown generation scenes must reject transactionally");
    checkRejectedWithoutMutation(seeded, "mutate 0.2 harmony",
        "unknown mutation scopes must reject transactionally");
}

void testSeededDrumScenes()
{
    auto scene = makeSession(1u);
    check(CommandEngine::execute(scene, "kit superior basic").ok,
        "drum-scene tests should establish native kit roles");
    auto replay = scene;
    const auto velocitiesBefore = valueFingerprint(scene);
    const auto commandRngBefore = scene.commandRngState;
    auto result = CommandEngine::execute(scene, "drumscene blast 666");
    const auto replayResult = CommandEngine::execute(
        replay, "drumscene blast 666");
    check(result.ok && replayResult.ok
            && sessionFingerprint(scene) == sessionFingerprint(replay),
        "a named drum scene and seed should be exactly repeatable");
    check(scene.commandRngState == commandRngBefore
            && valueFingerprint(scene) == velocitiesBefore,
        "seeded drum scenes should change NOTE rhythms without consuming mutation RNG or changing values");
    check(scene.pattern.tracks.size() == 7u
            && scene.pattern.tracks[0u].noteColumn.length == 16u
            && scene.pattern.tracks[2u].noteColumn.length == 16u
            && scene.pattern.tracks[3u].noteColumn.length == 32u
            && scene.pattern.tracks[4u].noteColumn.length == 16u
            && scene.pattern.tracks[3u].noteColumn.stride == 1u
            && scene.pattern.tracks[3u].noteColumn.direction
                == Direction::Forward,
        "blast should install the Max role-specific lengths and forward unit motion");
    bool anchoredPitches = true;
    for (std::size_t lane = 0u; lane < scene.pattern.tracks.size(); ++lane) {
        for (std::size_t row = 0u;
             row < scene.pattern.tracks[lane].noteColumn.length; ++row) {
            const auto& cell = scene.pattern.tracks[lane].notes[row];
            if (cell.state == NoteCellState::Note) {
                anchoredPitches = anchoredPitches
                    && cell.note == scene.laneDefaultNotes[lane];
            }
        }
    }
    check(anchoredPitches,
        "drum scenes should preserve each configured kit lane pitch");

    auto noKit = makeSession(2u);
    checkRejectedWithoutMutation(noKit, "drumscene techno 101",
        "drum scenes should explicitly reject patterns without recognized kit lanes");
    checkRejectedWithoutMutation(scene, "drumscene jungle 101",
        "unknown drum scenes should reject transactionally");
}

void testPatternVariationRequests()
{
    check(patternVariationLaunchIsDue(PatternVariationLaunch::NextTick,
              0u, 0u, 4u, 16u)
            && !patternVariationLaunchIsDue(
                PatternVariationLaunch::NextBeat, 0u, 0u, 4u, 16u)
            && patternVariationLaunchIsDue(
                PatternVariationLaunch::NextBeat, 3u, 3u, 4u, 16u)
            && !patternVariationLaunchIsDue(
                PatternVariationLaunch::NextPatternCycle,
                14u, 14u, 4u, 16u)
            && patternVariationLaunchIsDue(
                PatternVariationLaunch::NextPatternCycle,
                15u, 15u, 4u, 16u),
        "variation launch quantization should become due only at its requested logical boundary");

    auto source = makeSession(2u);
    const auto sourceBefore = sessionFingerprint(source);
    auto result = CommandEngine::execute(source,
        "variation generateseed orchard 0.48 0.55 0.18 launch beat");
    check(result.ok && result.hasEffect(CommandEffect::ProjectChanged)
            && result.patternVariation
            && result.patternVariation->launch
                == PatternVariationLaunch::NextBeat
            && result.patternVariation->sourceCommand
                == "generateseed orchard 0.48 0.55 0.18"
            && sessionFingerprint(source) == sourceBefore,
        "a seeded variation should preserve its source session and return a structured beat launch");
    check(result.patternVariation
            && noteFingerprint(result.patternVariation->generatedSession)
                != noteFingerprint(source),
        "a variation request should carry the generated pattern without installing it over the source");

    auto unseeded = makeSession(1u);
    check(CommandEngine::execute(unseeded, "mask 1 x---").ok,
        "unseeded variation test should establish source material");
    const auto unseededNotes = noteFingerprint(unseeded);
    const auto unseededRng = unseeded.commandRngState;
    result = CommandEngine::execute(unseeded,
        "vary mutate 1 notes launch cycle");
    check(result.ok && result.patternVariation
            && result.patternVariation->launch
                == PatternVariationLaunch::NextPatternCycle
            && noteFingerprint(unseeded) == unseededNotes
            && unseeded.commandRngState != unseededRng
            && noteFingerprint(result.patternVariation->generatedSession)
                != unseededNotes,
        "an unseeded mutation variation should advance only the shared RNG while preserving source cells");

    auto storedOnly = makeSession(1u);
    result = CommandEngine::execute(storedOnly,
        "variation scene sparse 101");
    check(result.ok && result.patternVariation
            && result.patternVariation->launch
                == PatternVariationLaunch::None,
        "a variation without launch should request bank storage only");

    checkRejectedWithoutMutation(source,
        "variation scene ambient 5 launch tick",
        "invalid nested scene commands should reject the complete variation transaction");
    checkRejectedWithoutMutation(source,
        "variation mask 1 x--- launch beat",
        "variation should reject non-generative nested commands");
    checkRejectedWithoutMutation(source,
        "variation scene sparse launch bar",
        "variation should reject unknown launch quantization");
}

void testEuclidAndDeterministicTransforms()
{
    auto session = makeSession(1u);
    check(CommandEngine::execute(session, "alias k 1").ok,
        "transform tests should establish an alias");
    auto result = CommandEngine::execute(session, "@k eu 5 16 1 <>");
    check(result.ok && session.pattern.tracks[0].noteColumn.length == 16u
            && activeNoteMask(session.pattern.tracks[0])
                == "x---x--x--x--x--"
            && session.pattern.tracks[0].noteColumn.direction
                == Direction::Palindrome,
        "Euclid should use the v8 distribution, rotation, and optional direction");

    result = CommandEngine::execute(session, "@k rotate 1");
    check(result.ok && activeNoteMask(session.pattern.tracks[0])
                == "-x---x--x--x--x-",
        "positive rotation should move the complete NOTE sequence right");
    result = CommandEngine::execute(session, "fill @k 4 0");
    check(result.ok && activeNoteMask(session.pattern.tracks[0])
                == "xx--xx--x--xx-x-",
        "fill should add anchored hits without erasing existing hits");

    const auto beforeReverse = activeNoteMask(session.pattern.tracks[0]);
    auto expectedReverse = beforeReverse;
    std::reverse(expectedReverse.begin(), expectedReverse.end());
    result = CommandEngine::execute(session, "@k reverse");
    check(result.ok
            && activeNoteMask(session.pattern.tracks[0]) == expectedReverse,
        "reverse should invert the active NOTE sequence deterministically");

    result = CommandEngine::execute(session, "sieve @k NOTE 5 0 2");
    check(result.ok && activeNoteMask(session.pattern.tracks[0]) == "x-x--",
        "sieve should port the Max modular residue rhythm exactly");
    result = CommandEngine::execute(session, "@k rotatehits note 1");
    check(result.ok && activeNoteMask(session.pattern.tracks[0]) == "-x-x-",
        "rotatehits should move hits and clear every non-hit cell");
    result = CommandEngine::execute(session, "thin @k 1");
    check(result.ok && activeNoteMask(session.pattern.tracks[0]) == "-----",
        "thin at probability one should remove every hit");
    result = CommandEngine::execute(session, "density @k note 1");
    check(result.ok && activeNoteMask(session.pattern.tracks[0]) == "xxxxx",
        "density at probability one should fill the complete active length");
    check(CommandEngine::execute(session, "mask @k x---").ok,
        "humanize test should establish one movable hit");
    result = CommandEngine::execute(session, "humanize @k NOTE 1");
    const auto humanized = activeNoteMask(session.pattern.tracks[0]);
    check(result.ok && (humanized == "-x--" || humanized == "---x"),
        "humanize should move an eligible hit one circular neighbor");

    result = CommandEngine::execute(session, "eu @k 17 16 1 <>");
    const auto& overfullTrack = session.pattern.tracks[0u];
    const auto& overfullPair = overfullTrack.fxPairs[0u];
    check(result.ok && activeNoteMask(overfullTrack)
                == "xxxxxxxxxxxxxxxx"
            && overfullPair.actions[0u].state
                == FxActionCellState::Sequencer
            && overfullPair.actions[0u].sequencerAction
                == s3g::tracker::SequencerAction::Ratchet
            && std::abs(overfullPair.values[0u].normalized) < 1.0e-6f
            && overfullPair.actionColumn.length == 16u
            && overfullPair.valueColumn.length == 16u
            && overfullPair.actionColumn.direction == Direction::Palindrome
            && overfullPair.valueColumn.direction == Direction::Palindrome
            && result.message.find("aligned SEQ1 ratchets")
                != std::string::npos,
        "overfull Euclid should rotate and align its extra pulse as a two-way RR burst");

    auto overfullScheduler = std::make_unique<TimingPlaybackScheduler>();
    overfullScheduler->setPattern(session.pattern);
    overfullScheduler->setTransport({ 8000.0, 60.0, 1u, 0.5 });
    overfullScheduler->start();
    std::array<ScheduledEvent, 32u> overfullEvents {};
    const auto overfullCount = overfullScheduler->process(120001u,
        overfullEvents.data(), overfullEvents.size());
    check(overfullCount == 17u
            && overfullEvents[0u].absoluteSampleTime == 0u
            && overfullEvents[1u].absoluteSampleTime == 4000u
            && overfullEvents[2u].absoluteSampleTime == 8000u
            && std::all_of(overfullEvents.begin(),
                overfullEvents.begin()
                    + static_cast<std::ptrdiff_t>(overfullCount),
                [](const ScheduledEvent& event) {
                    return event.kind == ScheduledEventKind::NoteOn;
                }),
        "a complete 17/16 overfull cycle should render exactly 17 onsets with its RR pulse halfway through the assigned step");

    result = CommandEngine::execute(session, "eu @k 5 16 0 forward");
    const auto& conventionalPair = session.pattern.tracks[0u].fxPairs[0u];
    check(result.ok
            && conventionalPair.actions.size() >= 16u
            && std::none_of(conventionalPair.actions.begin(),
                conventionalPair.actions.begin() + 16,
                [](const auto& cell) {
                    return cell.state == FxActionCellState::Sequencer
                        && cell.sequencerAction
                            == s3g::tracker::SequencerAction::Ratchet;
                }),
        "a conventional Euclid should clear stale auto-ratchets from the generated span");

    auto maximumEuclid = std::make_unique<TrackerSession>(makeSession(1u));
    result = CommandEngine::execute(*maximumEuclid, "eu 1 128 16");
    check(result.ok
            && maximumEuclid->pattern.tracks[0u].fxPairs[0u]
                    .actions[0u].sequencerAction
                == s3g::tracker::SequencerAction::Ratchet
            && std::abs(maximumEuclid->pattern.tracks[0u].fxPairs[0u]
                    .values[0u].normalized - 1.0f) < 1.0e-6f,
        "eight pulses per step should map exactly to RR value 1.0");
    checkRejectedWithoutMutation(*maximumEuclid, "eu 1 129 16",
        "more than eight pulses per step must reject without mutation");

    auto blockedEuclid = std::make_unique<TrackerSession>(makeSession(1u));
    for (auto& pair : blockedEuclid->pattern.tracks[0u].fxPairs) {
        pair.actions.resize(16u, s3g::tracker::FxActionCell::empty());
        pair.actions[0u] = s3g::tracker::FxActionCell::sequencer(
            s3g::tracker::SequencerAction::Probability);
    }
    checkRejectedWithoutMutation(*blockedEuclid, "eu 1 17 16",
        "overfull Euclid must not overwrite unrelated SEQ actions");
    checkRejectedWithoutMutation(session, "eu @k 17 16 0 random",
        "overfull Euclid must reject independently randomized NOTE/SEQ heads");
    checkRejectedWithoutMutation(session, "rotate @k 1.5",
        "fractional rotation should fail without mutation");
    checkRejectedWithoutMutation(session, "fill @k 0",
        "a zero fill interval should fail without mutation");
    checkRejectedWithoutMutation(session, "density @k 1.5",
        "invalid stochastic amounts must not advance pattern or RNG state");

    auto extreme = makeSession(1u);
    check(CommandEngine::execute(extreme, "len 1 8").ok,
        "the extreme fill test should establish an eight-step lane");
    result = CommandEngine::execute(extreme,
        "fill 1 4 -9223372036854775808");
    check(result.ok && activeNoteMask(extreme.pattern.tracks[0])
                == "x---x---",
        "fill should normalize the most-negative signed offset without overflow");

    auto emptyLength = makeSession(1u);
    emptyLength.pattern.tracks[0].noteColumn.length = 0u;
    emptyLength.pattern.tracks[0].notes.clear();
    checkRejectedWithoutMutation(emptyLength, "rotate 1 1",
        "rotate should reject a malformed zero-length NOTE column safely");
}

void testSoloUnmuteAndNames()
{
    auto session = makeSession(1u);
    check(CommandEngine::execute(session, "kit superior compact").ok,
        "solo tests should establish a compact kit");
    auto result = CommandEngine::execute(session, "@k solo @s");
    check(result.ok && !session.pattern.tracks[0].noteColumn.muted
            && !session.pattern.tracks[1].noteColumn.muted
            && session.pattern.tracks[2].noteColumn.muted
            && session.pattern.tracks[3].noteColumn.muted,
        "alias-first solo should keep all named targets audible");
    result = CommandEngine::execute(session, "@h unmute");
    check(result.ok && !session.pattern.tracks[2].noteColumn.muted
            && session.pattern.tracks[3].noteColumn.muted,
        "alias-first unmute should change only its target");
    result = CommandEngine::execute(session, "unmute all");
    check(result.ok
            && std::all_of(session.pattern.tracks.begin(),
                session.pattern.tracks.end(), [](const Track& track) {
                    return !track.noteColumn.muted;
                }),
        "unmute all should reopen every native lane");
    result = CommandEngine::execute(session, "@k name Deep Kick");
    check(result.ok && session.pattern.tracks[0].name == "Deep Kick",
        "name should preserve all words after its target");

    CommandEngine::execute(session, "mute @o on");
    checkRejectedWithoutMutation(session, "solo @k @missing",
        "solo must resolve every target before committing any mute changes");
}

void testFxCommands()
{
    auto session = makeSession(1u);
    auto result = CommandEngine::execute(session,
        "fx 1 1 3 PR 0.75");
    const auto& pair = session.pattern.tracks[0].fxPairs[0u];
    check(result.ok && result.hasEffect(CommandEffect::PatternChanged)
            && pair.actions.size() >= 3u && pair.values.size() >= 3u
            && pair.actions[2u].state == FxActionCellState::Sequencer
            && pair.actions[2u].sequencerAction
                == s3g::tracker::SequencerAction::Probability
            && pair.values[2u].state == FxValueCellState::Value
            && std::abs(pair.values[2u].normalized - 0.75f) < 1.0e-6f,
        "fx should transactionally resolve a sequencing code and normalized value");
    result = CommandEngine::execute(session, "fx 1 fx1 4 previous");
    check(result.ok
            && session.pattern.tracks[0].fxPairs[0u].actions[3u].state
                == FxActionCellState::Previous,
        "fx previous should write action memory recall explicitly");
    result = CommandEngine::execute(session, "fx 1 2 2 RR 0.5");
    check(result.ok
            && session.pattern.tracks[0].fxPairs[1u].actions[1u].state
                == FxActionCellState::Sequencer
            && session.pattern.tracks[0].fxPairs[1u].actions[1u]
                    .sequencerAction == s3g::tracker::SequencerAction::Ratchet
            && std::abs(session.pattern.tracks[0].fxPairs[1u].values[1u]
                    .normalized - 0.5f) < 1.0e-6f,
        "fx should author sequencing actions by mnemonic");
    result = CommandEngine::execute(session, "fx 1 2 3 seq.microtime 0.25");
    check(result.ok
            && session.pattern.tracks[0].fxPairs[1u].actions[2u]
                    .sequencerAction
                == s3g::tracker::SequencerAction::MicroTime,
        "fx should author timing actions by stable key");
    result = CommandEngine::execute(session, "fxvalue 1 1 4 .2");
    check(result.ok
            && std::abs(session.pattern.tracks[0].fxPairs[0u]
                    .values[3u].normalized - 0.2f) < 1.0e-6f,
        "fxvalue should edit the independent value column");
    check(CommandEngine::execute(session, "stride 1 v1 3").ok
            && session.pattern.tracks[0].fxPairs[0u]
                .valueColumn.stride == 3u,
        "V1 should participate in generic polymetric column controls");
    check(CommandEngine::execute(session, "dir 1 fx2 random").ok
            && session.pattern.tracks[0].fxPairs[1u]
                .actionColumn.direction == Direction::Random,
        "FX2 should support an independent direction");
    check(CommandEngine::execute(session, "mute 1 v2 on").ok
            && session.pattern.tracks[0].fxPairs[1u]
                .valueColumn.muted,
        "FX value columns should support independent mute/freeze");
    result = CommandEngine::execute(session, "actions");
    check(result.ok
            && result.message.find("RR=Ratchet") != std::string::npos
            && result.message.find("MT=Microtime") != std::string::npos
            && result.message.find("EU=Euclidean Gate") != std::string::npos
            && result.message.find("membrane") == std::string::npos,
        "actions should expose only MIDI-product sequencing choices");

    auto compact = makeSession(1u);
    result = CommandEngine::execute(compact, "fx1 1 PR !.=-");
    const auto& compactPair = compact.pattern.tracks[0].fxPairs[0u];
    check(result.ok && compactPair.actionColumn.length == 4u
            && compactPair.valueColumn.length == 4u
            && compactPair.actions[0u].sequencerAction
                == s3g::tracker::SequencerAction::Probability
            && compactPair.values[0u].normalized == 1.0f
            && std::abs(compactPair.values[1u].normalized - 0.55f)
                < 1.0e-6f
            && compactPair.actions[2u].state
                == FxActionCellState::Previous
            && compactPair.actions[3u].state == FxActionCellState::Empty,
        "compact FX entry should author paired actions, values, recalls, and rests");
    check(CommandEngine::execute(compact, "alias k 1").ok
            && CommandEngine::execute(compact,
                "@k fx2 EU .25,.5,=,-").ok
            && compact.pattern.tracks[0].fxPairs[1u].actions[0u]
                    .sequencerAction
                == s3g::tracker::SequencerAction::Euclid
            && std::abs(compact.pattern.tracks[0].fxPairs[1u].values[0u]
                    .normalized - 0.25f) < 1.0e-6f,
        "alias-first compact FX entry should accept normalized value lists");
    check(CommandEngine::execute(compact,
                "fx 1 1 5 RR .4").ok
            && CommandEngine::execute(compact, "prob 1 5 25%").ok
            && compact.pattern.tracks[0].fxPairs[1u].actions[4u]
                    .sequencerAction
                == s3g::tracker::SequencerAction::Probability
            && std::abs(compact.pattern.tracks[0].fxPairs[1u].values[4u]
                    .normalized - 0.25f) < 1.0e-6f,
        "probability helper should fall back to FX2 when FX1 is occupied");
    check(CommandEngine::execute(compact, "skip 1 6 .5").ok
            && compact.pattern.tracks[0].fxPairs[0u].actions[5u]
                    .sequencerAction == s3g::tracker::SequencerAction::Skip
            && CommandEngine::execute(compact, "skip 1 6 clear").ok
            && compact.pattern.tracks[0].fxPairs[0u].actions[5u].state
                == FxActionCellState::Empty,
        "convenience writers should add and clear sequencing FX cells");
    check(CommandEngine::execute(compact, "fx 1 1 7 previous").ok
            && CommandEngine::execute(compact, "prob 1 7 .4").ok
            && compact.pattern.tracks[0].fxPairs[0u].actions[6u].state
                == FxActionCellState::Previous
            && compact.pattern.tracks[0].fxPairs[1u].actions[6u]
                    .sequencerAction
                == s3g::tracker::SequencerAction::Probability,
        "FX helpers should preserve active Previous recalls and use FX2");
    check(CommandEngine::execute(compact,
                "fx1 1 PR " + std::string(256u, '!')).ok,
        "compact FX entry should accept the complete 256-row limit");
    checkRejectedWithoutMutation(compact,
        "fx1 1 PR " + std::string(257u, '!'),
        "compact FX entry should reject rows beyond the project limit");
    checkRejectedWithoutMutation(compact, "fx1 1 PR ?",
        "compact FX random values should remain explicit rather than hide RNG state");
    check(CommandEngine::execute(compact, "fx1 1 PR 1").ok
            && compact.pattern.tracks[0].fxPairs[0u].valueColumn.length == 1u
            && compact.pattern.tracks[0].fxPairs[0u].values[0u].normalized
                == 1.0f,
        "a bare FX number should always be a normalized value, never compact scale");
    checkRejectedWithoutMutation(compact, "fx1 1 PR 02469",
        "digit strings must not acquire a context-dependent compact FX scale");
    checkRejectedWithoutMutation(compact, "fx1 1 PR !o",
        "letter o must not duplicate a compact FX value level");

    checkRejectedWithoutMutation(session,
        "fx 1 2 1 membrane.click .5",
        "obsolete internal-audio actions must reject atomically");
    checkRejectedWithoutMutation(session,
        "fx 1 2 1 RR nan",
        "non-finite FX values must reject atomically");
    checkRejectedWithoutMutation(session, "fx 1 2 1 ST .5 note",
        "sequencing actions must reject obsolete parameter scopes atomically");
    checkRejectedWithoutMutation(session, "fx 1 3 1 clear",
        "FX pair indices outside 1..2 must reject atomically");

    auto independent = makeSession(1u);
    independent.pattern.tracks[0].fxPairs[0u].values.resize(1u);
    independent.pattern.tracks[0].fxPairs[0u].valueColumn.length = 1u;
    result = CommandEngine::execute(independent, "fx 1 1 4 clear");
    const auto& independentResult
        = independent.pattern.tracks[0].fxPairs[0u];
    check(result.ok && independentResult.valueColumn.length == 1u
            && independentResult.values.size() == 1u,
        "editing an FX action must not extend its independent value column");

    auto scheduled = makeSession(1u);
    check(CommandEngine::execute(scheduled, "note 1 1 60").ok
            && CommandEngine::execute(scheduled, "fx 1 1 1 RR 0").ok,
        "console commands should author an end-to-end timing fixture");
    TimingPlaybackScheduler scheduler;
    scheduler.setPattern(scheduled.pattern);
    scheduler.setTransport({ 8000.0, 60.0, 1u, 0.5 });
    scheduler.start();
    std::array<ScheduledEvent, 4u> timingEvents {};
    const auto timingCount = scheduler.process(4001u,
        timingEvents.data(), timingEvents.size());
    check(timingCount == 2u
            && timingEvents[0u].absoluteSampleTime == 0u
            && timingEvents[1u].absoluteSampleTime == 4000u,
        "a console-authored RR action should reach the cross-buffer scheduler");
}

void testDemoAndErrors()
{
    auto session = makeSession();
    session.selectedRow = 3u;
    auto result = CommandEngine::execute(session, "demo");
    check(result.ok && result.hasEffect(CommandEffect::PatternChanged)
            && result.hasEffect(CommandEffect::TransportChanged)
            && result.hasEffect(CommandEffect::SelectionChanged),
        "demo should report every changed session domain");
    check(session.pattern.tracks.size() == 8u
            && session.pattern.visibleRows == 16u
            && session.pattern.tracks[0].midiChannel == 10u
            && session.pattern.tracks[0].notes[0].note == 36u
            && session.pattern.tracks[0].noteColumn.length == 15u
            && session.pattern.tracks[2].noteColumn.length == 13u
            && session.pattern.tracks[3].noteColumn.length == 7u
            && session.pattern.tracks[3].noteColumn.direction
                == Direction::Palindrome && session.selectedRow == 0u,
        "demo should create an immediately playable polymetric drum pattern");
    check(session.pattern.tracks[0].destination == EventDestination::Internal
            && session.pattern.tracks[0].initialInstrumentNodeId == 0u
            && session.pattern.tracks[1].destination
                == EventDestination::Midi
            && session.pattern.tracks[1].initialInstrumentNodeId
                == kMidiOutInstrumentNode
            && session.pattern.tracks[4].destination
                == EventDestination::Internal
            && session.pattern.tracks[4].initialInstrumentNodeId
                == kStereoSamplerInstrumentNode,
        "demo should separate kick, sampler, and MIDI OUT rack instruments");
    check(session.pattern.tracks[2].destination == EventDestination::Midi
            && session.pattern.tracks[3].destination
                == EventDestination::Midi
            && session.pattern.tracks[5].destination
                == EventDestination::Midi
            && session.pattern.tracks[6].destination
                == EventDestination::Midi
            && session.pattern.tracks[7].destination
                == EventDestination::Midi,
        "demo hats and cymbals should remain MIDI-only");

    Pattern samplerPattern;
    samplerPattern.tracks.push_back(session.pattern.tracks[4]);
    Sequencer samplerSequencer;
    samplerSequencer.setPattern(std::move(samplerPattern));
    samplerSequencer.start();
    std::array<ScheduledEvent, 4u> samplerEvents {};
    const auto samplerCount = samplerSequencer.process(1u,
        samplerEvents.data(), samplerEvents.size());
    check(samplerCount == 1u
            && samplerEvents[0u].targetNode
                == kStereoSamplerInstrumentNode
            && samplerEvents[0u].destination == EventDestination::Internal,
        "the demo sampler lane should emit an internal rack event");
    check(!CommandEngine::execute(session, "").ok,
        "empty input should return a useful failure");
    result = CommandEngine::execute(session, "frobnicate");
    check(!result.ok && result.message.find("Unknown command") != std::string::npos,
        "unknown verbs should produce a useful diagnostic");
}

} // namespace

int main()
{
    testTransportActionsAndHelp();
    testHelpCatalogCoversAuditedParserVerbs();
    testTempoAndSwing();
    testTimingWarpCommands();
    testDynamicTrackCommands();
    testSelectionAndLaneControls();
    testNoteAndVelocityEdits();
    testMasks();
    testKitsAliasesAndTargets();
    testCompactMasksAndColumnControls();
    testVelocitySequences();
    testRandomVelocityCommands();
    testWholePatternGenerationAndMutation();
    testSeededDrumScenes();
    testPatternVariationRequests();
    testEuclidAndDeterministicTransforms();
    testSoloUnmuteAndNames();
    testFxCommands();
    testDemoAndErrors();

    if (failures == 0) {
        std::cout << "All command engine tests passed.\n";
        return 0;
    }
    std::cerr << failures << " command engine test(s) failed.\n";
    return 1;
}
