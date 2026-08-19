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

    Track track;
    track.name = "BREAKS / DRUMS";
    track.velocityScale = 0.82f;
    track.midiChannel = 11u;
    track.destination = EventDestination::Internal;
    track.initialInstrumentNodeId = 0u;
    track.chokeGroup = 7u;
    track.notes.resize(12u, NoteCell::rest());
    track.notes[0u] = NoteCell::withNote(36u);
    track.notes[1u] = NoteCell::retriggerPrevious();
    track.notes[2u] = NoteCell::kill();
    track.notes[3u] = NoteCell::hold();
    track.notes[7u] = NoteCell::withNote(72u);
    track.instruments.resize(12u, InstrumentCell::empty());
    track.instruments[0u] = InstrumentCell::withInstrument(0u);
    track.instruments[1u] = InstrumentCell::previous();
    track.instruments[8u] = InstrumentCell::withInstrument(
        kStereoSamplerInstrumentNode);
    track.velocities.resize(12u, ValueCell::defaultValue());
    track.velocities[0u] = ValueCell::withValue(0.91f);
    track.velocities[1u] = ValueCell::previous();
    track.noteColumn = { 12u, 3u, 2u, Direction::Palindrome, false };
    track.instrumentColumn = { 12u, 1u, 1u, Direction::Forward, false };
    track.velocityColumn = { 9u, 2u, 3u, Direction::Random, false };

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
    secondFx.actions[0u] = FxActionCell::parameter(23u,
        ParameterScope::Note, kTrackInstrumentNode);
    secondFx.actions[1u] = FxActionCell::previous();
    secondFx.actions[2u] = FxActionCell::midiControlChange(74u);
    secondFx.values[0u] = FxValueCell::withValue(0.42f);
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
    document.session.mainOutputGain = 0.73f;
    document.session.mainOutputMuted = true;
    document.session.songPlaybackEnabled = true;
    document.session.commandRngState = std::numeric_limits<uint64_t>::max();
    document.session.playbackSeed = 0xfedcba98u;

    auto& rack = document.instrumentRack;
    rack.selectedNode = kStereoSamplerInstrumentNode;
    rack.slots[0u].basePatch.normalized[3u] = 0.1234567f;
    rack.daisyDrumPatches[2u].normalized[5u] = 0.7654321f;
    auto& sampler = rack.samplerSlots[0u];
    sampler.filePath = "/samples/Break α.wav";
    auto asset = std::make_shared<audio::StereoSampleAsset>();
    asset->sampleRate = 48000.0;
    asset->left.resize(16u, 0.25f);
    asset->right.resize(16u, -0.25f);
    sampler.asset = asset;
    sampler.analysis = std::make_shared<audio::StereoSampleAnalysis>();
    sampler.slices[0u] = { 0u, 7u, 0.75f, false };
    sampler.slices[1u] = { 7u, 16u, 1.25f, true };
    sampler.sliceCount = 2u;
    sampler.baseNote = 48u;
    sampler.envelope.attackMilliseconds = 2.5;
    sampler.envelope.decayMilliseconds = 31.0;
    sampler.envelope.sustain = 0.625f;
    sampler.envelope.releaseMilliseconds = 87.0;
    rack.midiRoutes[0u].kind = MidiInstrumentRouteKind::Destination;
    rack.midiRoutes[0u].destinationId = -4242;
    rack.midiRoutes[0u].virtualSource = 3u;
    rack.midiRoutes[0u].channel = 16u;

    document.song.name = "LIVE SET";
    document.song.loop = true;
    document.song.ticksPerBeat = 8u;
    SongRow firstRow;
    firstRow.patternId = "A01";
    firstRow.durationTicks = 24u;
    firstRow.repeats = 3u;
    firstRow.bpm = 145.0;
    firstRow.swing = 0.63;
    firstRow.mutedTracks = 0x80000000u;
    firstRow.timingWarpLibraryIndex = 6u;
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
    check(firstEncoding.find("\"schemaVersion\": 6") != std::string::npos
            && firstEncoding.find("\"patternBank\"") != std::string::npos
            && firstEncoding.find("\"probability\"") != std::string::npos
            && firstEncoding.find("\"midi-control-change\"")
                != std::string::npos
            && firstEncoding.find("\"valueInterpolation\": \"linear\"")
                != std::string::npos
            && firstEncoding.find("\"phase\": 4") != std::string::npos,
        "schema, MIDI CC interpolation, generative FX, and column phase should be explicit JSON data");

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
            && decoded.session.mainOutputMuted
            && decoded.session.songPlaybackEnabled
            && std::abs(decoded.session.tempoScale - 1.5) < 1.0e-9
            && std::abs(decoded.session.mainOutputGain - 0.73f) < 1.0e-6f,
        "random seeds, MAIN OUT, and Song mode should survive without precision loss");
    check(activePattern(decoded).tracks[0u].noteColumn.phase == 2u
            && activePattern(decoded).tracks[0u].notes[3u].state
                == NoteCellState::Hold
            && activePattern(decoded).tracks[0u].fxPairs[0u].actionColumn.phase == 4u
            && activePattern(decoded).tracks[0u].fxPairs[0u].actions[11u]
                .sequencerAction == SequencerAction::Euclid
            && activePattern(decoded).tracks[0u].fxPairs[1u]
                    .actions[2u].state
                == FxActionCellState::MidiControlChange
            && activePattern(decoded).tracks[0u].fxPairs[1u]
                    .actions[2u].midiController == 74u
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
    check(decoded.transport.timingWarp.size() == 2u
            && decoded.transport.loopEnabled
            && decoded.transport.loopEndRow == 11u,
        "warp stack and global loop should round trip");
    const auto* libraryWarp = decoded.warpLibrary.entry(6u);
    check(decoded.warpLibrary.size() == 1u && libraryWarp
            && libraryWarp->name == "Broken Quintuplet"
            && libraryWarp->cycleTicks == 5u
            && libraryWarp->stack.size() == 1u,
        "indexed composed timing-warps should round trip");
    check(decoded.instrumentRack.samplerSlots[0u].filePath
                == "/samples/Break α.wav"
            && decoded.instrumentRack.samplerSlots[0u].sliceCount == 2u
            && decoded.instrumentRack.samplerSlots[0u].slices[1u].reverse
            && decoded.instrumentRack.samplerSlots[0u]
                .envelope.attackMilliseconds == 2.5
            && decoded.instrumentRack.samplerSlots[0u]
                .envelope.decayMilliseconds == 31.0
            && decoded.instrumentRack.samplerSlots[0u].envelope.sustain
                == 0.625f
            && decoded.instrumentRack.samplerSlots[0u]
                .envelope.releaseMilliseconds == 87.0,
        "sampler reference, slice table, and envelope should round trip");
    check(!decoded.instrumentRack.samplerSlots[0u].asset
            && !decoded.instrumentRack.samplerSlots[0u].analysis,
        "decoded PCM and waveform analysis should remain derived data");
    check(decoded.instrumentRack.midiRoutes[0u].destinationId == -4242
            && decoded.instrumentRack.midiRoutes[0u].channel == 16u,
        "instrument-owned MIDI destination and channel should round trip");
    check(decoded.song.rows.size() == 2u
            && decoded.song.rows[0u].bpm == 145.0
            && decoded.song.rows[0u].timingWarpLibraryIndex
                == std::optional<std::size_t>(6u)
            && !decoded.song.rows[1u].bpm.has_value()
            && !decoded.song.rows[1u].timingWarpLibraryIndex.has_value(),
        "song arrangement, warp selection, and optional row overrides should round trip");
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

void testDefaultAppDemoIsSaveable()
{
    TrackerSession session;
    const auto demo = CommandEngine::execute(session, "demo");
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
    check(demo.ok && encodeProjectDocument(document, encoded).ok(),
        "the app's initial demo/session/rack/song state should save without normalization repair");
}

void testUnknownFieldsAreSafelyIgnored()
{
    std::string encoded;
    check(encodeProjectDocument(makeDocument(), encoded).ok(),
        "unknown-field fixture should encode");
    const std::string canonical = encoded;
    encoded.insert(2u,
        "  \"futureRoot\": {\"opaque\": [1, true, null]},\n");
    const std::string marker = "\"transport\": {";
    const auto transport = encoded.find(marker);
    check(transport != std::string::npos,
        "transport marker should exist in encoded JSON");
    if (transport != std::string::npos) {
        encoded.insert(transport + marker.size(),
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
    std::string badVersion = encoded;
    const auto schema = badVersion.find("\"schemaVersion\": 6");
    badVersion.replace(schema, std::string("\"schemaVersion\": 6").size(),
        "\"schemaVersion\": 2");
    const auto unsupported = decodeProjectDocument(badVersion, destination);
    check(unsupported.code == ProjectErrorCode::UnsupportedSchemaVersion
            && activePattern(destination).name == "sentinel",
        "unsupported schemas should reject without mutating destination");

    std::string legacy = encoded;
    const auto legacySchema = legacy.find("\"schemaVersion\": 6");
    legacy.replace(legacySchema,
        std::string("\"schemaVersion\": 6").size(),
        "\"schemaVersion\": 5");
    const std::string linearInterpolation
        = "\"valueInterpolation\": \"linear\",\n";
    const auto legacyInterpolation = legacy.find(linearInterpolation);
    check(legacyInterpolation != std::string::npos,
        "schema migration fixture should contain a linear value lane");
    if (legacyInterpolation != std::string::npos)
        legacy.erase(legacyInterpolation, linearInterpolation.size());
    ProjectDocument migrated;
    check(decodeProjectDocument(legacy, migrated).ok()
            && migrated.patternBank.entries.size() == 2u
            && migrated.patternBank.entries[0u].pattern.tracks[0u]
                    .fxPairs[1u].valueInterpolation
                == ValueInterpolation::Step,
        "schema 5 projects should migrate missing interpolation modes to STEP");

    auto invalidBank = makeDocument();
    invalidBank.patternBank.entries[1u].id = "A01";
    std::string untouchedBank = "unchanged";
    check(encodeProjectDocument(invalidBank, untouchedBank).code
                == ProjectErrorCode::InconsistentData
            && untouchedBank == "unchanged",
        "duplicate stable pattern IDs should reject transactionally");

    invalidBank = makeDocument();
    invalidBank.patternBank.activePatternId = "MISSING";
    check(encodeProjectDocument(invalidBank, untouchedBank).code
                == ProjectErrorCode::InconsistentData,
        "active selection must resolve inside the pattern bank");

    invalidBank = makeDocument();
    invalidBank.song.rows[1u].patternId = "MISSING";
    check(encodeProjectDocument(invalidBank, untouchedBank).code
                == ProjectErrorCode::InconsistentData,
        "Song rows must resolve stable pattern IDs inside the bank");

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

    auto invalidModel = makeDocument();
    activePattern(invalidModel).tracks[0u].instruments[3u]
        = InstrumentCell::withInstrument(kMidiOutInstrumentNode + 7u);
    std::string untouched = "unchanged";
    const auto referenceResult = encodeProjectDocument(invalidModel, untouched);
    check(referenceResult.code == ProjectErrorCode::InconsistentData
            && untouched == "unchanged",
        "inactive rack references should reject before replacing encoded output");

    invalidModel = makeDocument();
    invalidModel.instrumentRack.samplerSlots[0u].slices[0u].endFrame = 99u;
    check(encodeProjectDocument(invalidModel, untouched).code
                == ProjectErrorCode::OutOfRange,
        "slice ranges outside a loaded asset should reject");

    invalidModel = makeDocument();
    invalidModel.instrumentRack.samplerSlots[0u].envelope.releaseMilliseconds
        = std::numeric_limits<double>::quiet_NaN();
    check(encodeProjectDocument(invalidModel, untouched).code
                == ProjectErrorCode::OutOfRange,
        "non-finite sampler envelopes should reject before serialization");

    invalidModel = makeDocument();
    invalidModel.patternBank.entries[0u].aliases.emplace("Bad Alias", 0u);
    check(encodeProjectDocument(invalidModel, untouched).code
                == ProjectErrorCode::OutOfRange,
        "project aliases should use the same reachable canonical grammar as commands");

    invalidModel = makeDocument();
    invalidModel.patternBank.entries[0u].aliases["outside"] = 1u;
    check(encodeProjectDocument(invalidModel, untouched).code
                == ProjectErrorCode::OutOfRange,
        "an alias outside its own pattern should fail even when another bank pattern has that lane");

    invalidModel = makeDocument();
    invalidModel.instrumentRack.instruments[3u]
        = *defaultRackInstrument(kSn76489InstrumentNode);
    check(encodeProjectDocument(invalidModel, untouched).code
                == ProjectErrorCode::InconsistentData,
        "archived chip nodes should not reactivate through project encoding");

    std::string archivedNode = encoded;
    const auto activeNodes = archivedNode.find("\"activeNodes\"");
    const auto arrayStart = archivedNode.find('[', activeNodes);
    const auto firstNode = archivedNode.find_first_of("0123456789", arrayStart);
    check(activeNodes != std::string::npos && arrayStart != std::string::npos
            && firstNode != std::string::npos,
        "active-node rejection fixture should find the first rack node");
    if (firstNode != std::string::npos) {
        archivedNode.replace(firstNode, 1u,
            std::to_string(kSn76489InstrumentNode));
        check(decodeProjectDocument(archivedNode, destination).code
                    == ProjectErrorCode::OutOfRange,
            "archived chip nodes should not reactivate through project decoding");
    }
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
    testDefaultAppDemoIsSaveable();
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
