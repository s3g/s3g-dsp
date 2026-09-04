#include "s3g/tracker/atomic_project_store.h"
#include "s3g/tracker/command.h"
#include "s3g/tracker/project_codec.h"
#include "s3g/tracker/project_history.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace s3g::tracker;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

Pattern& activePattern(ProjectDocument& document)
{
    return *document.patternBank.activePattern();
}

const Pattern& activePattern(const ProjectDocument& document)
{
    return *document.patternBank.activePattern();
}

ProjectDocument makeDocument()
{
    ProjectDocument document;
    activePattern(document).name = "Native Pattern α";
    activePattern(document).visibleRows = 12u;
    auto& burst = document.burstLibrary.bursts[0u];
    burst.name = "Break Rush";
    burst.eventCount = 4u;
    burst.events[0u] = { 0u, 48u, 127u, 70u };
    burst.events[1u] = { 16384u, 52u, 104u, 60u };
    burst.events[2u] = { 32768u, 50u, 82u, 50u };
    burst.events[3u] = { 49152u, 55u, 116u, 40u };

    Track track;
    track.name = "BREAKS / DRUMS";
    track.velocityScale = 0.82f;
    track.midiChannel = 11u;
    track.initialInstrumentNodeId = kMidiOutInstrumentNode;
    track.chokeGroup = 7u;
    track.notes.resize(12u, NoteCell::rest());
    track.notes[0u] = NoteCell::withNote(36u);
    track.notes[1u] = NoteCell::retriggerPrevious();
    track.notes[2u] = NoteCell::kill();
    track.notes[3u] = NoteCell::hold();
    track.notes[4u] = NoteCell::withBurst(0u);
    std::array<uint8_t, kMaximumNoteVoices> chordNotes {};
    chordNotes[0u] = 60u;
    chordNotes[1u] = 64u;
    chordNotes[2u] = 67u;
    track.notes[7u] = NoteCell::withNotes(chordNotes, 3u);
    track.velocities.resize(12u, ValueCell::defaultValue());
    track.velocities[0u] = ValueCell::withValue(0.91f);
    track.velocities[1u] = ValueCell::previous();
    std::array<float, kMaximumNoteVoices> chordVelocities {};
    chordVelocities[0u] = 0.91f;
    chordVelocities[1u] = 0.63f;
    chordVelocities[2u] = 0.78f;
    track.velocities[7u] = ValueCell::withValues(chordVelocities, 3u);
    track.gates.resize(12u, GateCell::defaultValue());
    std::array<GateVoice, kMaximumNoteVoices> chordGates {};
    chordGates[0u] = { GateVoiceMode::Rows, 0.5f };
    chordGates[1u] = { GateVoiceMode::Tie, 1.0f };
    chordGates[2u] = { GateVoiceMode::Rows, 1.25f };
    track.gates[7u] = GateCell::withVoices(chordGates, 3u);
    track.noteColumn = { 12u, 3u, 2u, Direction::Palindrome, false };
    track.velocityColumn = { 9u, 2u, 3u, Direction::Random, false };
    track.gateColumn = { 7u, 1u, 4u, Direction::Reverse, false };

    constexpr std::array<SequencerAction, kSequencerActionCount> actions {{
        SequencerAction::Ratchet,
        SequencerAction::MicroTime,
        SequencerAction::Delay,
        SequencerAction::Flam,
        SequencerAction::Stutter,
        SequencerAction::Accent,
        SequencerAction::Ghost,
        SequencerAction::Probability,
        SequencerAction::Skip,
        SequencerAction::Offset,
        SequencerAction::RepeatPrevious,
        SequencerAction::Euclid,
        SequencerAction::Condition,
        SequencerAction::Energy,
    }};
    auto& firstFx = track.fxPairs[0u];
    firstFx.actions.resize(actions.size());
    firstFx.values.resize(actions.size());
    for (std::size_t index = 0u; index < actions.size(); ++index) {
        firstFx.actions[index] = FxActionCell::sequencer(actions[index]);
        firstFx.values[index] = FxValueCell::withValue(
            static_cast<float>(index) / static_cast<float>(actions.size()));
    }
    firstFx.actionColumn = { actions.size(), 1u, 4u,
        Direction::Forward, false };
    firstFx.valueColumn = { actions.size(), 5u, 5u,
        Direction::Reverse, false };

    auto& secondFx = track.fxPairs[1u];
    secondFx.actions.resize(4u, FxActionCell::empty());
    secondFx.values.resize(4u, FxValueCell::previous());
    secondFx.actions[0u] = FxActionCell::sequencer(
        SequencerAction::MicroTime);
    secondFx.actions[1u] = FxActionCell::previous();
    secondFx.actions[2u] = FxActionCell::midiControlChange(74u);
    std::array<float, kMaximumNoteVoices> chordMicroTimes {};
    chordMicroTimes[0u] = 0.32f;
    chordMicroTimes[1u] = 0.50f;
    chordMicroTimes[2u] = 0.71f;
    secondFx.values[0u] = FxValueCell::withValues(chordMicroTimes, 3u);
    secondFx.values[2u] = FxValueCell::withValue(64.0f / 127.0f);
    secondFx.valueInterpolation = ValueInterpolation::Linear;
    secondFx.actionColumn = { 4u, 2u, 1u, Direction::Random, false };
    secondFx.valueColumn = { 3u, 1u, 2u, Direction::Palindrome, true };
    activePattern(document).tracks.push_back(std::move(track));
    PatternBankEntry alternate;
    alternate.id = "B02";
    alternate.pattern = activePattern(document);
    alternate.pattern.name = "Alternate Break";
    alternate.pattern.tracks[0u].notes[0u] = NoteCell::withNote(38u);
    alternate.pattern.tracks.push_back(alternate.pattern.tracks[0u]);
    alternate.pattern.tracks[1u].name = "ALT AUX";
    document.patternBank.entries.push_back(std::move(alternate));
    document.patternBank.entries[0u].laneDefaultNotes = { 36u };
    document.patternBank.entries[0u].aliases.emplace("breaks", 0u);
    document.patternBank.entries[1u].laneDefaultNotes = { 38u, 41u };
    document.patternBank.entries[1u].aliases.emplace("aux", 1u);

    document.transport.sampleRate = 96000.0;
    document.transport.bpm = 137.5;
    document.transport.ticksPerBeat = 8u;
    document.transport.swing = 0.61;
    document.transport.warpCycleTicks = 24u;
    document.transport.timingWarpEnabled = true;
    document.transport.timingLookaheadMilliseconds = 34.0;
    document.transport.microTimingRangeMilliseconds = 28.0;
    document.transport.loopEnabled = true;
    document.transport.loopStartRow = 2u;
    document.transport.loopEndRow = 11u;
    TimingWarpOptions exponentOptions;
    exponentOptions.phaseBegin = 0.1;
    exponentOptions.phaseEnd = 0.9;
    exponentOptions.repetitions = 3u;
    exponentOptions.alpha = 0.7;
    check(document.transport.timingWarp.append(
        TimingWarpTransform::exponential(1.7, exponentOptions)).added(),
        "fixture exponential warp should be valid");
    check(document.transport.timingWarp.append(
        TimingWarpTransform::euclideanQuantize(5u, 8u)).added(),
        "fixture Euclidean warp should be valid");
    TimingWarpStack libraryStack;
    check(libraryStack.append(TimingWarpTransform::exponential(
            0.75)).added()
            && document.warpLibrary.store(6u, "Broken Quintuplet", 5u,
                libraryStack),
        "fixture warp library entry should be valid");

    document.session.gateMilliseconds = 123.5;
    document.session.tempoScale = 1.5;
    document.session.songPlaybackEnabled = true;
    document.session.showMidiNoteValues = false;
    document.session.trackerRowJump = 3u;
    document.session.commandRngState = std::numeric_limits<uint64_t>::max();
    document.session.playbackSeed = 0xfedcba98u;

    auto& phrase = document.phraseLibrary.phrases[3u];
    phrase = makeBlankPhrase(7u);
    phrase.name = "Odd Hat Turn";
    phrase.previewMidiChannel = 10u;
    phrase.notes[0u] = NoteCell::withNote(42u);
    phrase.notes[3u] = NoteCell::withNote(46u);
    phrase.notes[5u] = NoteCell::withBurst(4u);
    document.burstLibrary.bursts[4u].name = "Phrase flam";
    document.burstLibrary.bursts[4u].eventCount = 2u;
    document.burstLibrary.bursts[4u].events[0u]
        = { 0u, 38u, 118u, 40u };
    document.burstLibrary.bursts[4u].events[1u]
        = { 49152u, 38u, 90u, 25u };
    phrase.velocities[3u] = ValueCell::withValue(0.72f);
    phrase.fxPairs[0u].actions[3u] = FxActionCell::sequencer(
        SequencerAction::MicroTime);
    phrase.fxPairs[0u].values[3u] = FxValueCell::withValue(0.6f);
    phrase.gates[3u] = GateCell::withRows(0.75f);

    document.song.name = "LIVE SET";
    document.song.loop = true;
    document.song.ticksPerBeat = 8u;
    SongRow firstRow;
    firstRow.patternId = "A01";
    firstRow.durationTicks = 24u;
    firstRow.repeats = 3u;
    firstRow.energy = 0.65f;
    firstRow.bpm = 145.0;
    firstRow.swing = 0.63;
    firstRow.mutedTracks = 0x80000000u;
    firstRow.timingWarpLibraryIndex = 6u;
    firstRow.patternLoop = SongPatternLoop { 4u, 12u };
    document.song.rows.push_back(std::move(firstRow));
    SongRow secondRow;
    secondRow.patternId = "B02";
    secondRow.durationTicks = 7u;
    secondRow.repeats = 1u;
    document.song.rows.push_back(std::move(secondRow));
    return document;
}

void testCompleteDeterministicRoundTrip()
{
    const ProjectDocument source = makeDocument();
    std::string firstEncoding;
    const auto encoded = encodeProjectDocument(source, firstEncoding);
    check(encoded.ok() && !firstEncoding.empty(),
        "complete native project should encode");
    check(firstEncoding.find(
                "\"format\": \"s3g-tracker-midi-composition\"")
                != std::string::npos
            && firstEncoding.find("\"version\": 2") != std::string::npos
            && firstEncoding.find("\"patterns\"") != std::string::npos
            && firstEncoding.find("\"arrangement\"") != std::string::npos
            && firstEncoding.find("\"playback\"") != std::string::npos
            && firstEncoding.find("\"workspace\"") != std::string::npos
            && firstEncoding.find("\"probability\"") != std::string::npos
            && firstEncoding.find("\"midi-control-change\"")
                != std::string::npos
            && firstEncoding.find("\"valueInterpolation\": \"linear\"")
                != std::string::npos
            && firstEncoding.find("\"phase\": 4") != std::string::npos
            && firstEncoding.find("\"slot\": 0") != std::string::npos
            && firstEncoding.find("\"notes\": [") != std::string::npos
            && firstEncoding.find("\"values\": [") != std::string::npos
            && firstEncoding.find("instrumentRack") == std::string::npos
            && firstEncoding.find("instruments") == std::string::npos
            && firstEncoding.find("sampleRate") == std::string::npos
            && firstEncoding.find("mainOutput") == std::string::npos,
        "version 2 should contain only MIDI composition, playback, arrangement, and workspace data");

    ProjectDocument decoded;
    const auto decodedResult = decodeProjectDocument(firstEncoding, decoded);
    check(decodedResult.ok(), "encoded native project should decode");
    std::string secondEncoding;
    check(encodeProjectDocument(decoded, secondEncoding).ok()
            && secondEncoding == firstEncoding,
        "decode/encode should be byte-deterministic");
    check(decoded.session.commandRngState
                == std::numeric_limits<uint64_t>::max()
            && decoded.session.playbackSeed == 0xfedcba98u
            && decoded.session.songPlaybackEnabled
            && !decoded.session.showMidiNoteValues
            && decoded.session.trackerRowJump == 3u
            && std::abs(decoded.session.tempoScale - 1.5) < 1.0e-9,
        "random seeds, Song mode, NOTE view, and row jump should survive without precision loss");
    check(decoded.phraseLibrary.phrases[3u].name == "Odd Hat Turn"
            && decoded.phraseLibrary.phrases[3u].length == 7u
            && decoded.phraseLibrary.phrases[3u].previewMidiChannel == 10u
            && decoded.phraseLibrary.phrases[3u].notes[3u].note == 46u
            && decoded.phraseLibrary.phrases[3u].notes[5u].state
                == NoteCellState::Burst
            && decoded.burstLibrary.bursts[4u].name
                == "Phrase flam"
            && decoded.burstLibrary.bursts[4u].events[1u]
                .position == 49152u
            && decoded.phraseLibrary.phrases[3u].fxPairs[0u].values[3u]
                .normalized == 0.6f
            && decoded.phraseLibrary.phrases[3u].gates[3u]
                .gateVoice(0u).rows == 0.75f,
        "project phrase library should preserve preview channel, odd lengths, gates, and typed cells");
    check(activePattern(decoded).tracks[0u].noteColumn.phase == 2u
            && activePattern(decoded).tracks[0u].notes[3u].state
                == NoteCellState::Hold
            && activePattern(decoded).tracks[0u].notes[4u].state
                == NoteCellState::Burst
            && activePattern(decoded).tracks[0u].notes[7u].noteVoiceCount()
                == 3u
            && activePattern(decoded).tracks[0u].notes[7u].noteVoice(1u)
                == 64u
            && activePattern(decoded).tracks[0u].velocities[7u]
                    .valueVoiceCount() == 3u
            && std::abs(activePattern(decoded).tracks[0u].velocities[7u]
                    .valueVoice(2u) - 0.78f) < 0.00001f
            && activePattern(decoded).tracks[0u].gates[7u]
                    .gateVoiceCount() == 3u
            && activePattern(decoded).tracks[0u].gates[7u]
                    .gateVoice(1u).mode == GateVoiceMode::Tie
            && activePattern(decoded).tracks[0u].gateColumn.phase == 4u
            && decoded.burstLibrary.bursts[0u].name == "Break Rush"
            && decoded.burstLibrary.bursts[0u].events[2u].note == 50u
            && activePattern(decoded).tracks[0u].fxPairs[0u].actionColumn.phase == 4u
            && activePattern(decoded).tracks[0u].fxPairs[0u].actions[11u]
                .sequencerAction == SequencerAction::Euclid
            && activePattern(decoded).tracks[0u].fxPairs[1u]
                    .actions[2u].state
                == FxActionCellState::MidiControlChange
            && activePattern(decoded).tracks[0u].fxPairs[1u]
                    .actions[2u].midiController == 74u
            && activePattern(decoded).tracks[0u].fxPairs[1u]
                    .values[0u].valueVoiceCount() == 3u
            && std::abs(activePattern(decoded).tracks[0u].fxPairs[1u]
                    .values[0u].valueVoice(2u) - 0.71f) < 0.00001f
            && activePattern(decoded).tracks[0u].fxPairs[1u]
                    .valueInterpolation == ValueInterpolation::Linear,
        "polymetric phase, MIDI CC modes, and every current sequencing action should round trip");
    check(decoded.patternBank.entries.size() == 2u
            && decoded.patternBank.entries[0u].id == "A01"
            && decoded.patternBank.entries[1u].id == "B02"
            && decoded.patternBank.activePatternId == "A01"
            && decoded.patternBank.entries[0u].laneDefaultNotes[0u] == 36u
            && decoded.patternBank.entries[0u].aliases.at("breaks") == 0u
            && decoded.patternBank.entries[1u].laneDefaultNotes[1u] == 41u
            && decoded.patternBank.entries[1u].aliases.at("aux") == 1u
            && decoded.song.rows[1u].patternId == "B02",
        "pattern order, IDs, selection, per-pattern authoring state, and Song references should round trip");
    check(decoded.transport.timingWarpEnabled
            && decoded.transport.timingWarp.size() == 2u
            && decoded.transport.loopEnabled
            && decoded.transport.loopEndRow == 11u,
        "warp stack and global loop should round trip");
    const auto* libraryWarp = decoded.warpLibrary.entry(6u);
    check(decoded.warpLibrary.size() == 1u && libraryWarp
            && libraryWarp->name == "Broken Quintuplet"
            && libraryWarp->cycleTicks == 5u
            && libraryWarp->stack.size() == 1u,
        "indexed composed timing-warps should round trip");
    check(activePattern(decoded).tracks[0u].destination
                == EventDestination::Midi
            && activePattern(decoded).tracks[0u].initialInstrumentNodeId
                == kMidiOutInstrumentNode
            && activePattern(decoded).tracks[0u].instruments.empty()
            && activePattern(decoded).tracks[0u].instrumentColumn.length == 0u,
        "decoded lanes should reconstruct the MIDI-only runtime route without legacy instrument data");
    check(decoded.song.rows.size() == 2u
            && decoded.song.rows[0u].bpm == 145.0
            && decoded.song.rows[0u].energy == 0.65f
            && decoded.song.rows[1u].energy == 1.0f
            && decoded.song.rows[0u].timingWarpLibraryIndex
                == std::optional<std::size_t>(6u)
            && decoded.song.rows[0u].patternLoop
            && decoded.song.rows[0u].patternLoop->startRow == 4u
            && decoded.song.rows[0u].patternLoop->endRow == 12u
            && !decoded.song.rows[1u].bpm.has_value()
            && !decoded.song.rows[1u].timingWarpLibraryIndex.has_value()
            && !decoded.song.rows[1u].patternLoop.has_value(),
        "song arrangement, warp selection, pattern loop, and optional row overrides should round trip");
}

void testEmptyOptionalSongIsAValidProject()
{
    ProjectDocument document;
    activePattern(document).name = "Empty Pattern";
    std::string encoded;
    ProjectDocument decoded;
    check(encodeProjectDocument(document, encoded).ok()
            && decodeProjectDocument(encoded, decoded).ok()
            && decoded.song.rows.empty(),
        "a project should not require song mode to be authored");
}

void testProjectWideBurstIdentity()
{
    ProjectDocument document;
    auto& shared = document.burstLibrary.bursts[3u];
    shared.name = "ONE SHARED RUFF";
    shared.eventCount = 2u;
    shared.events[0u] = { 0u, 38u, 118u, 35u };
    shared.events[1u] = { 32768u, 38u, 91u, 30u };

    Pattern first;
    first.visibleRows = 4u;
    first.tracks.emplace_back();
    first.tracks[0u].initialInstrumentNodeId = kMidiOutInstrumentNode;
    first.tracks[0u].notes.assign(4u, NoteCell::rest());
    first.tracks[0u].velocities.assign(4u, ValueCell::defaultValue());
    first.tracks[0u].noteColumn.length = 4u;
    first.tracks[0u].velocityColumn.length = 4u;
    first.tracks[0u].notes[0u] = NoteCell::withBurst(3u);
    document.patternBank.entries[0u].pattern = first;

    Pattern second = first;
    second.tracks[0u].notes[0u] = NoteCell::rest();
    second.tracks[0u].notes[2u] = NoteCell::withBurst(3u);
    document.patternBank.entries.push_back({ "A02", second, {}, {} });

    auto& phrase = document.phraseLibrary.phrases[0u];
    phrase = makeBlankPhrase(4u);
    phrase.name = "SHARED RUFF PHRASE";
    phrase.notes[1u] = NoteCell::withBurst(3u);

    std::string encoded;
    const auto encodedResult = encodeProjectDocument(document, encoded);
    if (!encodedResult.ok())
        std::cerr << "shared Burst encode error: " << encodedResult.location
                  << ": " << encodedResult.message << '\n';
    check(encodedResult.ok(),
        "one project Burst should satisfy references from multiple patterns and a Phrase");
    ProjectDocument decoded;
    check(decodeProjectDocument(encoded, decoded).ok()
            && decoded.burstLibrary.bursts[3u].name == "ONE SHARED RUFF"
            && decoded.patternBank.entries[0u].pattern.tracks[0u].notes[0u].note == 3u
            && decoded.patternBank.entries[1u].pattern.tracks[0u].notes[2u].note == 3u
            && decoded.phraseLibrary.phrases[0u].notes[1u].note == 3u,
        "project roundtrip should preserve a single shared Burst slot identity everywhere");
}

void testDefaultAppDemoIsSaveable()
{
    TrackerSession session;
    const auto kit = CommandEngine::execute(session, "kit superior basic");
    ProjectDocument document;
    *document.patternBank.activePattern() = session.pattern;
    document.transport = session.transport;
    document.session.gateMilliseconds = session.gateMilliseconds;
    document.patternBank.entries[0u].laneDefaultNotes
        = session.laneDefaultNotes;
    document.patternBank.entries[0u].aliases = session.aliases;
    document.session.commandRngState = session.commandRngState;
    document.session.playbackSeed = session.playbackSeed;
    SongRow row;
    row.patternId = document.patternBank.activePatternId;
    document.song.rows.push_back(std::move(row));
    std::string encoded;
    check(kit.ok && encodeProjectDocument(document, encoded).ok(),
        "the app's initial MIDI kit/session/song state should save without normalization repair");
}

void testRapBurstSeqDemoLoadsAsMidiComposition()
{
    std::string sourceDirectory = __FILE__;
    const auto filename = sourceDirectory.find_last_of('/');
    if (filename != std::string::npos)
        sourceDirectory.erase(filename);
    const std::string demoPath = sourceDirectory
        + "/../../examples/tracker/rap-beat-burst-seq-demo.s3gt";
    ProjectDocument demo;
    const auto loaded = loadProjectDocument(demoPath, demo);
    check(loaded.ok(),
        "the rap Burst/SEQ demo should load as a MIDI composition");
    if (!loaded.ok()) return;

    bool hasBurst = false;
    bool hasSequencerAction = false;
    for (const auto& burst : demo.burstLibrary.bursts)
        hasBurst |= !burst.empty();
    for (const auto& entry : demo.patternBank.entries) {
        for (const auto& track : entry.pattern.tracks) {
            for (const auto& pair : track.fxPairs) {
                for (const auto& action : pair.actions) {
                    hasSequencerAction |= action.state
                        == FxActionCellState::Sequencer;
                }
            }
        }
    }
    check(demo.session.songPlaybackEnabled
            && !demo.patternBank.entries.empty()
            && !demo.song.rows.empty()
            && hasBurst && hasSequencerAction,
        "the rap demo should retain its Song, Burst, and SEQ content");
}

void testUnknownFieldsAreSafelyIgnored()
{
    std::string encoded;
    check(encodeProjectDocument(makeDocument(), encoded).ok(),
        "unknown-field fixture should encode");
    const std::string canonical = encoded;
    encoded.insert(2u,
        "  \"futureRoot\": {\"opaque\": [1, true, null]},\n");
    const std::string marker = "\"playback\": {";
    const auto playback = encoded.find(marker);
    check(playback != std::string::npos,
        "playback marker should exist in encoded JSON");
    if (playback != std::string::npos) {
        encoded.insert(playback + marker.size(),
            "\n    \"futureTimingModel\": {\"revision\": 9},");
    }
    ProjectDocument decoded;
    const auto result = decodeProjectDocument(encoded, decoded);
    check(result.ok(),
        "well-formed unknown object fields should be safely skipped");
    std::string reencoded;
    check(result.ok() && encodeProjectDocument(decoded, reencoded).ok()
            && reencoded == canonical,
        "unknown fields should not enter the native in-memory model");
}

void testStrictTransactionalRejection()
{
    std::string encoded;
    check(encodeProjectDocument(makeDocument(), encoded).ok(),
        "strict-rejection fixture should encode");

    ProjectDocument destination;
    activePattern(destination).name = "sentinel";

    std::string wrongVersion = encoded;
    const auto version = wrongVersion.find("\"version\": 2");
    wrongVersion.replace(version, std::string("\"version\": 2").size(),
        "\"version\": 99");
    const auto unsupported = decodeProjectDocument(wrongVersion, destination);
    check(unsupported.code == ProjectErrorCode::UnsupportedSchemaVersion
            && activePattern(destination).name == "sentinel",
        "any non-current MIDI composition version should reject transactionally");

    std::string legacy = encoded;
    const auto format = legacy.find(kProjectFormatIdentifier);
    legacy.replace(format, std::string(kProjectFormatIdentifier).size(),
        "s3g-tracker-project");
    check(decodeProjectDocument(legacy, destination).code
                == ProjectErrorCode::InvalidArgument
            && activePattern(destination).name == "sentinel",
        "the retired hybrid project format should not migrate");

    auto invalidBank = makeDocument();
    invalidBank.patternBank.entries[1u].id = "A01";
    std::string untouched = "unchanged";
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::InconsistentData
            && untouched == "unchanged",
        "duplicate stable pattern IDs should reject transactionally");

    invalidBank = makeDocument();
    invalidBank.patternBank.activePatternId = "MISSING";
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::InconsistentData,
        "active selection must resolve inside the pattern bank");

    invalidBank = makeDocument();
    invalidBank.song.rows[1u].patternId = "MISSING";
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::InconsistentData,
        "Song rows must resolve stable pattern IDs inside the bank");

    invalidBank = makeDocument();
    invalidBank.song.rows[0u].patternLoop = SongPatternLoop { 12u, 12u };
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::InconsistentData,
        "empty Song pattern-loop ranges should reject transactionally");

    invalidBank = makeDocument();
    invalidBank.session.trackerRowJump = 0u;
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::OutOfRange,
        "Tracker row jump should reject values outside 1..16");

    invalidBank = makeDocument();
    activePattern(invalidBank).tracks[0u].fxPairs[1u].actions[0u]
        = FxActionCell::parameter(23u, ParameterScope::Note,
            kTrackInstrumentNode);
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::InconsistentData,
        "internal instrument parameters should not enter a MIDI composition");

    invalidBank = makeDocument();
    invalidBank.patternBank.entries[0u].aliases.emplace("Bad Alias", 0u);
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::OutOfRange,
        "project aliases should use the command language's canonical grammar");

    invalidBank = makeDocument();
    invalidBank.patternBank.entries[0u].aliases["outside"] = 1u;
    check(encodeProjectDocument(invalidBank, untouched).code
                == ProjectErrorCode::OutOfRange,
        "an alias outside its own pattern should fail");

    std::string badEnum = encoded;
    const auto action = badEnum.find("\"ratchet\"");
    badEnum.replace(action, std::string("\"ratchet\"").size(),
        "\"future-action\"");
    const auto enumResult = decodeProjectDocument(badEnum, destination);
    check(enumResult.code == ProjectErrorCode::OutOfRange
            && activePattern(destination).name == "sentinel",
        "unknown values inside known enum fields should fail closed");

    std::string duplicate = encoded;
    duplicate.insert(2u, "  \"format\": \"duplicate\",\n");
    check(decodeProjectDocument(duplicate, destination).code
                == ProjectErrorCode::InvalidJson,
        "duplicate JSON keys should be rejected as ambiguous");
}

bool directoryHasTemporaryProject(const std::string& directory)
{
    DIR* handle = ::opendir(directory.c_str());
    if (!handle) return true;
    bool found = false;
    while (const dirent* entry = ::readdir(handle)) {
        if (std::string(entry->d_name).find(".tmp.") != std::string::npos) {
            found = true;
            break;
        }
    }
    ::closedir(handle);
    return found;
}

void testAtomicStorePublishesCompleteReplacement()
{
    std::array<char, 64u> directoryTemplate {};
    std::snprintf(directoryTemplate.data(), directoryTemplate.size(),
        "/tmp/s3g-tracker-project-tests.XXXXXX");
    char* created = ::mkdtemp(directoryTemplate.data());
    check(created != nullptr, "atomic-store test directory should be created");
    if (!created) return;
    const std::string directory(created);
    const std::string file = directory + "/set.s3gt";

    auto first = makeDocument();
    auto save = saveProjectDocumentAtomically(first, file);
    check(save.ok(), "first atomic project save should succeed");
    ProjectDocument loaded;
    check(loadProjectDocument(file, loaded).ok()
            && activePattern(loaded).name == activePattern(first).name,
        "atomic project file should load after publication");

    auto second = makeDocument();
    activePattern(second).name = "Replacement";
    save = saveProjectDocumentAtomically(second, file);
    check(save.ok() && loadProjectDocument(file, loaded).ok()
            && activePattern(loaded).name == "Replacement",
        "second save should atomically replace the complete project");
    check(!directoryHasTemporaryProject(directory),
        "successful atomic saves should leave no temporary project files");

    const std::string directoryTarget = directory + "/not-a-file";
    check(::mkdir(directoryTarget.c_str(), 0700) == 0,
        "rename-failure target directory should be created");
    const auto failed = saveProjectDocumentAtomically(second, directoryTarget);
    check(failed.code == ProjectErrorCode::IoRenameFailed
            && !directoryHasTemporaryProject(directory),
        "failed atomic publication should clean its temporary file");

    ::rmdir(directoryTarget.c_str());
    ::unlink(file.c_str());
    ::rmdir(directory.c_str());
}

void testBoundedProjectUndoRedo()
{
    auto document = makeDocument();
    ProjectHistory history;
    check(history.reset(document).ok()
            && !history.canUndo() && !history.canRedo(),
        "project history should seed without manufacturing an undo step");

    activePattern(document).name = "FIRST EDIT";
    bool changed = false;
    check(history.record(document, &changed).ok() && changed
            && history.canUndo() && !history.canRedo(),
        "a persistent document edit should create one undo snapshot");
    check(history.record(document, &changed).ok() && !changed
            && history.undoCount() == 1u,
        "publishing identical project state should not duplicate history");

    activePattern(document).name = "SECOND EDIT";
    check(history.record(document).ok() && history.undoCount() == 2u,
        "successive edits should remain independently undoable");
    ProjectDocument restored;
    check(history.undo(restored).ok()
            && activePattern(restored).name == "FIRST EDIT"
            && history.canRedo(),
        "undo should restore the exact preceding native project");
    check(history.undo(restored).ok()
            && activePattern(restored).name == "Native Pattern α"
            && !history.canUndo(),
        "a second undo should restore the seeded project");
    check(history.redo(restored).ok()
            && activePattern(restored).name == "FIRST EDIT",
        "redo should restore the next project snapshot");

    activePattern(restored).name = "BRANCH EDIT";
    check(history.record(restored).ok() && !history.canRedo(),
        "a new edit after undo should discard the abandoned redo branch");
    check(!history.redo(restored).ok(),
        "redo should report an empty branch without mutating the project");
}

} // namespace

int main()
{
    testCompleteDeterministicRoundTrip();
    testEmptyOptionalSongIsAValidProject();
    testProjectWideBurstIdentity();
    testDefaultAppDemoIsSaveable();
    testRapBurstSeqDemoLoadsAsMidiComposition();
    testUnknownFieldsAreSafelyIgnored();
    testStrictTransactionalRejection();
    testAtomicStorePublishesCompleteReplacement();
    testBoundedProjectUndoRedo();
    if (failures != 0) {
        std::cerr << failures << " project codec test(s) failed\n";
        return 1;
    }
    std::cout << "Native project codec and atomic store tests passed\n";
    return 0;
}
