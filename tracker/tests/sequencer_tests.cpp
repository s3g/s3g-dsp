#include "s3g/tracker/sequencer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using s3g::tracker::Direction;
using s3g::tracker::EventDestination;
using s3g::tracker::FxActionCell;
using s3g::tracker::FxValueCell;
using s3g::tracker::InstrumentCell;
using s3g::tracker::InstrumentCellState;
using s3g::tracker::kInvalidInstrumentNode;
using s3g::tracker::kMembraneRackSlotCount;
using s3g::tracker::kTrackInstrumentNode;
using s3g::tracker::NoteCell;
using s3g::tracker::ParameterScope;
using s3g::tracker::Pattern;
using s3g::tracker::ScheduledEvent;
using s3g::tracker::ScheduledEventKind;
using s3g::tracker::Sequencer;
using s3g::tracker::SequencerAction;
using s3g::tracker::Track;
using s3g::tracker::TransportSettings;
using s3g::tracker::TimingWarpTransform;
using s3g::tracker::ValueCell;
using s3g::tracker::midiVelocityFromNormalized;
using s3g::tracker::parseMidiNote;
using s3g::tracker::routesToInternal;
using s3g::tracker::routesToMidi;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

Track makeTrack(std::initializer_list<uint8_t> notes,
    std::initializer_list<float> velocities = { 1.0f })
{
    Track track;
    track.name = "test";
    track.midiChannel = 10u;
    for (const auto note : notes)
        track.notes.push_back(NoteCell::withNote(note));
    for (const auto velocity : velocities)
        track.velocities.push_back(ValueCell::withValue(velocity));
    track.noteColumn.length = track.notes.size();
    track.velocityColumn.length = track.velocities.size();
    return track;
}

std::vector<ScheduledEvent> renderTicks(Track track, std::size_t ticks)
{
    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::vector<ScheduledEvent> events(ticks + 4u);
    const uint32_t frames = static_cast<uint32_t>((ticks - 1u) * 6000u + 1u);
    events.resize(sequencer.process(frames, events.data(), events.size()));
    return events;
}

struct AbsoluteEvent {
    uint64_t frame = 0u;
    uint32_t track = 0u;
    uint8_t note = 0u;
    float normalizedVelocity = 0.0f;
};

std::vector<AbsoluteEvent> renderBlocks(Pattern pattern,
    TransportSettings transport, const std::vector<uint32_t>& blockSizes,
    uint32_t seed = 12345u)
{
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport(transport);
    sequencer.setRandomSeed(seed);
    sequencer.start();
    std::vector<AbsoluteEvent> result;
    uint64_t blockStart = 0u;
    for (const auto blockSize : blockSizes) {
        std::array<ScheduledEvent, 128u> blockEvents {};
        const auto count = sequencer.process(blockSize, blockEvents.data(),
            blockEvents.size());
        check(count < blockEvents.size(),
            "test event buffer should not truncate a rendered block");
        for (std::size_t index = 0u; index < count; ++index) {
            result.push_back({
                blockStart + blockEvents[index].frameOffset,
                blockEvents[index].track,
                blockEvents[index].note,
                blockEvents[index].normalizedVelocity,
            });
        }
        blockStart += blockSize;
    }
    return result;
}

void testPolymetricColumns()
{
    const auto events = renderTicks(makeTrack(
        { 36u, 38u, 42u }, { 0.25f, 1.0f }), 6u);
    const std::array<uint8_t, 6u> expectedNotes {
        36u, 38u, 42u, 36u, 38u, 42u,
    };
    const std::array<float, 6u> expectedVelocities {
        0.25f, 1.0f, 0.25f, 1.0f, 0.25f, 1.0f,
    };
    const std::array<uint8_t, 6u> expectedMidiVelocities {
        32u, 127u, 32u, 127u, 32u, 127u,
    };
    check(events.size() == expectedNotes.size(),
        "polymetric render should emit six events");
    for (std::size_t index = 0u;
         index < std::min(events.size(), expectedNotes.size()); ++index) {
        check(events[index].note == expectedNotes[index],
            "note and velocity columns must wrap independently");
        check(events[index].normalizedVelocity == expectedVelocities[index],
            "canonical velocity must stay normalized across polymeter");
        check(midiVelocityFromNormalized(events[index].normalizedVelocity)
                == expectedMidiVelocities[index],
            "the MIDI adapter must preserve established velocity rounding");
    }
}

void testInstrumentColumnPolymeterAndMemory()
{
    Track track;
    track.destination = EventDestination::Internal;
    track.initialInstrumentNodeId = 0u;
    track.notes = {
        NoteCell::withNote(60u),
        NoteCell::rest(),
        NoteCell::withNote(62u),
        NoteCell::rest(),
        NoteCell::withNote(64u),
    };
    track.instruments = {
        InstrumentCell::withInstrument(1u),
        InstrumentCell::withInstrument(4u),
        InstrumentCell::previous(),
        InstrumentCell::empty(),
    };
    track.velocities = { ValueCell::withValue(1.0f) };
    track.noteColumn.length = track.notes.size();
    track.instrumentColumn.length = track.instruments.size();
    track.velocityColumn.length = 1u;

    Pattern pattern;
    pattern.tracks.push_back(track);
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 8u> events {};
    const auto count = sequencer.process(24001u, events.data(), events.size());
    check(count == 3u && events[0u].targetNode == 1u
            && events[1u].targetNode == 4u
            && events[2u].targetNode == 1u,
        "rest-row INS updates plus Previous/Empty memory must drive later notes");
    check(sequencer.notePosition(0u) == 0u
            && sequencer.instrumentPosition(0u) == 1u
            && sequencer.lastNotePosition(0u) == 4u
            && sequencer.lastInstrumentPosition(0u) == 0u,
        "NOTE and INS positions must expose independent polymetric phase");

    auto muted = track;
    muted.notes = {
        NoteCell::rest(), NoteCell::rest(), NoteCell::withNote(65u),
    };
    muted.noteColumn.length = muted.notes.size();
    muted.instruments = {
        InstrumentCell::withInstrument(1u),
        InstrumentCell::withInstrument(2u),
        InstrumentCell::withInstrument(4u),
    };
    muted.instrumentColumn.length = muted.instruments.size();
    muted.instrumentColumn.muted = true;
    Pattern mutedPattern;
    mutedPattern.tracks.push_back(muted);
    Sequencer mutedSequencer;
    mutedSequencer.setPattern(std::move(mutedPattern));
    mutedSequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    mutedSequencer.start();
    check(mutedSequencer.process(6001u, events.data(), events.size()) == 0u
            && mutedSequencer.instrumentPosition(0u) == 2u,
        "a muted INS column must emit nothing and keep advancing");
    muted.instrumentColumn.muted = false;
    Pattern unmutedPattern;
    unmutedPattern.tracks.push_back(std::move(muted));
    mutedSequencer.replacePattern(std::move(unmutedPattern));
    const auto unmutedCount = mutedSequencer.process(6000u, events.data(),
        events.size());
    check(unmutedCount == 1u && events[0u].targetNode == 4u
            && mutedSequencer.lastInstrumentPosition(0u) == 2u,
        "unmuting INS must resume at its advanced phase without prior memory reads");
}

void testTrackVelocityScale()
{
    Track track;
    track.destination = EventDestination::Both;
    track.velocityScale = 0.25f;
    track.notes = { NoteCell::withNote(60u) };
    track.velocities = { ValueCell::withValue(0.8f) };
    track.noteColumn.length = 1u;
    track.velocityColumn.length = 1u;

    Pattern pattern;
    pattern.tracks.push_back(track);
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.start();
    std::array<ScheduledEvent, 1u> event {};
    check(sequencer.process(1u, event.data(), event.size()) == 1u
            && std::abs(event[0u].normalizedVelocity - 0.2f) < 0.00001f,
        "track velocity scale must trim canonical MIDI/internal onsets");

    track.velocityScale = 9.0f;
    Pattern clampedPattern;
    clampedPattern.tracks.push_back(std::move(track));
    sequencer.setPattern(std::move(clampedPattern));
    check(sequencer.pattern().tracks[0u].velocityScale == 1.0f,
        "track velocity scale must normalize before realtime playback");

    track.velocityScale = std::numeric_limits<float>::quiet_NaN();
    Pattern finitePattern;
    finitePattern.tracks.push_back(std::move(track));
    sequencer.setPattern(std::move(finitePattern));
    check(sequencer.pattern().tracks[0u].velocityScale == 1.0f,
        "a non-finite track velocity scale must fail safely to unity");
}

void testInstrumentFxScopeAndReleaseRouting()
{
    Track track;
    track.destination = EventDestination::Internal;
    track.initialInstrumentNodeId = 0u;
    track.notes = {
        NoteCell::withNote(60u),
        NoteCell::rest(),
        NoteCell::retriggerPrevious(),
    };
    track.instruments = {
        InstrumentCell::withInstrument(1u),
        InstrumentCell::withInstrument(4u),
        InstrumentCell::withInstrument(3u),
    };
    track.velocities = { ValueCell::withValue(1.0f) };
    track.noteColumn.length = track.notes.size();
    track.instrumentColumn.length = track.instruments.size();
    track.velocityColumn.length = 1u;

    auto& global = track.fxPairs[0u];
    global.actions = {
        FxActionCell::parameter(3u, ParameterScope::Global),
        FxActionCell::parameter(3u, ParameterScope::Channel),
        FxActionCell::parameter(3u, ParameterScope::Global),
    };
    global.values.assign(3u, FxValueCell::withValue(0.25f));
    global.actionColumn.length = global.actions.size();
    global.valueColumn.length = global.values.size();
    auto& note = track.fxPairs[1u];
    note.actions.assign(3u, FxActionCell::parameter(
        9u, ParameterScope::Note));
    note.values.assign(3u, FxValueCell::withValue(0.75f));
    note.actionColumn.length = note.actions.size();
    note.valueColumn.length = note.values.size();

    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 12u> events {};
    const auto count = sequencer.process(12001u, events.data(), events.size());
    check(count == 9u,
        "three INS/FX ticks should emit bounded parameter and note events");
    if (count == 9u) {
        check(events[0u].kind == ScheduledEventKind::Parameter
                && events[0u].parameterScope == ParameterScope::Global
                && events[0u].targetNode == 1u
                && events[1u].parameterScope == ParameterScope::Note
                && events[1u].targetNode == 1u
                && events[2u].kind == ScheduledEventKind::NoteOn
                && events[2u].targetNode == 1u,
            "same-tick Global/Note FX and onset must use the new INS node");
        check(events[3u].parameterScope == ParameterScope::Channel
                && events[3u].targetNode == 4u
                && events[4u].parameterScope == ParameterScope::Note
                && events[4u].targetNode == 1u
                && events[4u].noteId == events[2u].noteId,
            "rest-time Channel FX must follow INS while Note FX stays on its active node");
        check(events[5u].kind == ScheduledEventKind::NoteOff
                && events[5u].targetNode == 1u
                && events[5u].noteId == events[2u].noteId
                && events[6u].parameterScope == ParameterScope::Global
                && events[6u].targetNode == 3u
                && events[7u].parameterScope == ParameterScope::Note
                && events[7u].targetNode == 3u
                && events[8u].kind == ScheduledEventKind::NoteOn
                && events[8u].targetNode == 3u
                && events[7u].noteId == events[8u].noteId,
            "retrigger must order old-node release before new-node FX and onset");
    }

    Track killTrack;
    killTrack.destination = EventDestination::Internal;
    killTrack.notes = {
        NoteCell::withNote(61u), NoteCell::rest(), NoteCell::kill(),
    };
    killTrack.instruments = {
        InstrumentCell::withInstrument(1u),
        InstrumentCell::withInstrument(4u),
        InstrumentCell::previous(),
    };
    killTrack.velocities = { ValueCell::withValue(1.0f) };
    killTrack.noteColumn.length = killTrack.notes.size();
    killTrack.instrumentColumn.length = killTrack.instruments.size();
    killTrack.velocityColumn.length = 1u;
    const auto killEvents = renderTicks(std::move(killTrack), 3u);
    check(killEvents.size() == 2u
            && killEvents[0u].kind == ScheduledEventKind::NoteOn
            && killEvents[0u].targetNode == 1u
            && killEvents[1u].kind == ScheduledEventKind::NoteOff
            && killEvents[1u].targetNode == 1u,
        "Kill must release the onset node after a rest-row INS memory change");
}

void testTypedFxPairs()
{
    Track track;
    track.destination = EventDestination::Both;
    track.initialInstrumentNodeId = 3u;
    track.midiChannel = 1u;
    track.notes = {
        NoteCell::withNote(36u),
        NoteCell::retriggerPrevious(),
        NoteCell::rest(),
    };
    track.velocities = { ValueCell::withValue(1.0f) };
    track.noteColumn.length = track.notes.size();
    track.velocityColumn.length = 1u;

    auto& first = track.fxPairs[0u];
    first.actions = {
        FxActionCell::parameter(3u),
        FxActionCell::previous(),
        FxActionCell::previous(),
    };
    first.values = {
        FxValueCell::withValue(0.2f),
        FxValueCell::withValue(0.7f),
    };
    first.actionColumn.length = first.actions.size();
    first.valueColumn.length = first.values.size();

    auto& second = track.fxPairs[1u];
    second.actions = {
        FxActionCell::parameter(3u, ParameterScope::Global, 3u),
        FxActionCell::empty(),
        FxActionCell::empty(),
    };
    second.values = { FxValueCell::withValue(0.8f) };
    second.actionColumn.length = second.actions.size();
    second.valueColumn.length = second.values.size();

    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 16u> events {};
    const auto count = sequencer.process(12001u, events.data(), events.size());
    check(count == 6u,
        "three FX ticks should emit two onsets, one explicit release, and three parameters");
    if (count == 6u) {
        check(events[0u].kind == ScheduledEventKind::Parameter
                && events[0u].parameterId == 3u
                && events[0u].targetNode == 3u
                && std::abs(events[0u].parameterValue - 0.8f) < 1.0e-6f,
            "lane-relative FX must resolve before an explicit later duplicate wins");
        check(events[1u].kind == ScheduledEventKind::NoteOn
                && events[1u].targetNode == 3u,
            "a tick parameter should precede its new onset");
        check(events[2u].kind == ScheduledEventKind::NoteOff
                && events[2u].targetNode == 3u
                && events[3u].kind == ScheduledEventKind::Parameter
                && events[3u].targetNode == 3u
                && events[4u].kind == ScheduledEventKind::NoteOn,
            "same-sample retrigger order must be NoteOff, Parameter, NoteOn");
        check(events[2u].noteId == events[1u].noteId
                && events[4u].noteId != events[2u].noteId,
            "FX insertion must not disturb canonical retrigger identities");
        check(std::abs(events[3u].parameterValue - 0.7f) < 1.0e-6f,
            "action and value columns should advance independently");
        check(events[5u].kind == ScheduledEventKind::Parameter
                && events[5u].absoluteSampleTime == 12000u
                && std::abs(events[5u].parameterValue - 0.2f) < 1.0e-6f,
            "FX parameters should emit on note-rest rows and retain memory");
    }
    check(sequencer.lastFxActionPosition(0u, 0u) == 2u
            && sequencer.lastFxValuePosition(0u, 0u) == 0u,
        "FX action and value playheads should expose independent polymeter");

    Track noteScoped;
    noteScoped.destination = EventDestination::Internal;
    noteScoped.notes = { NoteCell::withNote(40u) };
    noteScoped.velocities = { ValueCell::withValue(1.0f) };
    noteScoped.noteColumn.length = 1u;
    noteScoped.velocityColumn.length = 1u;
    noteScoped.fxPairs[0u].actions = {
        FxActionCell::parameter(9u, ParameterScope::Note),
    };
    noteScoped.fxPairs[0u].values = { FxValueCell::withValue(0.5f) };
    noteScoped.fxPairs[0u].actionColumn.length = 1u;
    noteScoped.fxPairs[0u].valueColumn.length = 1u;
    Pattern scopedPattern;
    scopedPattern.tracks.push_back(std::move(noteScoped));
    Sequencer scoped;
    scoped.setPattern(std::move(scopedPattern));
    scoped.start();
    std::array<ScheduledEvent, 4u> scopedEvents {};
    const auto scopedCount = scoped.process(1u, scopedEvents.data(),
        scopedEvents.size());
    check(scopedCount == 2u
            && scopedEvents[0u].kind == ScheduledEventKind::Parameter
            && scopedEvents[1u].kind == ScheduledEventKind::NoteOn
            && scopedEvents[0u].noteId == scopedEvents[1u].noteId
            && scopedEvents[0u].noteId != 0u,
        "a note-scoped onset parameter must carry the new canonical note ID");
}

void testFxMemoryAndMutedPhase()
{
    Track memoryTrack;
    memoryTrack.destination = EventDestination::Internal;
    memoryTrack.notes.assign(3u, NoteCell::rest());
    memoryTrack.velocities = { ValueCell::defaultValue() };
    memoryTrack.noteColumn.length = memoryTrack.notes.size();
    memoryTrack.velocityColumn.length = 1u;
    auto& memoryFx = memoryTrack.fxPairs[0u];
    memoryFx.actions = {
        FxActionCell::parameter(3u),
        FxActionCell::empty(),
        FxActionCell::previous(),
    };
    memoryFx.values = {
        FxValueCell::withValue(0.1f),
        FxValueCell::withValue(0.6f),
        FxValueCell::previous(),
    };
    memoryFx.actionColumn.length = memoryFx.actions.size();
    memoryFx.valueColumn.length = memoryFx.values.size();

    Pattern memoryPattern;
    memoryPattern.tracks.push_back(std::move(memoryTrack));
    Sequencer memorySequencer;
    memorySequencer.setPattern(std::move(memoryPattern));
    memorySequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    memorySequencer.start();
    std::array<ScheduledEvent, 4u> memoryEvents {};
    const auto memoryCount = memorySequencer.process(12001u,
        memoryEvents.data(), memoryEvents.size());
    check(memoryCount == 2u
            && std::abs(memoryEvents[0u].parameterValue - 0.1f) < 1.0e-6f
            && std::abs(memoryEvents[1u].parameterValue - 0.6f) < 1.0e-6f,
        "an Empty action must retain a new value for a later Previous action");
    const auto memorySnapshot = memorySequencer.fxMemorySnapshot(0u, 0u);
    const auto missingSnapshot = memorySequencer.fxMemorySnapshot(99u, 0u);
    check(memorySnapshot.hasAction && memorySnapshot.hasValue
            && memorySnapshot.action.parameterId == 3u
            && std::abs(memorySnapshot.value - 0.6f) < 1.0e-6f
            && !missingSnapshot.hasAction && !missingSnapshot.hasValue,
        "the read-only FX snapshot must expose authoritative recall memory and fail safe out of range");

    Track mutedTrack;
    mutedTrack.destination = EventDestination::Internal;
    mutedTrack.notes.assign(2u, NoteCell::rest());
    mutedTrack.velocities = { ValueCell::defaultValue() };
    mutedTrack.noteColumn.length = mutedTrack.notes.size();
    mutedTrack.velocityColumn.length = 1u;
    auto& mutedFx = mutedTrack.fxPairs[0u];
    mutedFx.actions.assign(3u, FxActionCell::parameter(6u));
    mutedFx.values.assign(3u, FxValueCell::withValue(0.4f));
    mutedFx.actionColumn.length = mutedFx.actions.size();
    mutedFx.valueColumn.length = mutedFx.values.size();
    mutedFx.actionColumn.muted = true;
    mutedFx.valueColumn.muted = true;

    Pattern mutedPattern;
    mutedPattern.tracks.push_back(std::move(mutedTrack));
    Sequencer mutedSequencer;
    mutedSequencer.setPattern(std::move(mutedPattern));
    mutedSequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    mutedSequencer.start();
    std::array<ScheduledEvent, 2u> mutedEvents {};
    const auto mutedCount = mutedSequencer.process(6001u,
        mutedEvents.data(), mutedEvents.size());
    check(mutedCount == 0u
            && mutedSequencer.fxActionPosition(0u, 0u) == 2u
            && mutedSequencer.fxValuePosition(0u, 0u) == 2u,
        "muted FX fields must suppress reads while their phases keep advancing");
}

void testStrideAndDirections()
{
    auto forward = makeTrack({ 0u, 1u, 2u, 3u, 4u });
    forward.noteColumn.stride = 2u;
    auto events = renderTicks(std::move(forward), 5u);
    const std::array<uint8_t, 5u> expected { 0u, 2u, 4u, 1u, 3u };
    check(events.size() == expected.size(),
        "forward stride should emit the requested event count");
    for (std::size_t index = 0u;
         index < std::min(events.size(), expected.size()); ++index)
        check(events[index].note == expected[index],
            "forward stride must skip through the active length");

    auto reverse = makeTrack({ 20u, 21u, 22u, 23u });
    reverse.noteColumn.direction = Direction::Reverse;
    events = renderTicks(std::move(reverse), 5u);
    const std::array<uint8_t, 5u> backwards { 20u, 23u, 22u, 21u, 20u };
    check(events.size() == backwards.size(),
        "reverse direction should emit the requested event count");
    for (std::size_t index = 0u;
         index < std::min(events.size(), backwards.size()); ++index)
        check(events[index].note == backwards[index],
            "reverse direction must wrap through the active length");

    auto palindrome = makeTrack({ 10u, 11u, 12u, 13u });
    palindrome.noteColumn.direction = Direction::Palindrome;
    events = renderTicks(std::move(palindrome), 7u);
    const std::array<uint8_t, 7u> pingPong {
        10u, 11u, 12u, 13u, 12u, 11u, 10u,
    };
    check(events.size() == pingPong.size(),
        "palindrome direction should emit the requested event count");
    for (std::size_t index = 0u;
         index < std::min(events.size(), pingPong.size()); ++index)
        check(events[index].note == pingPong[index],
            "palindrome direction must reflect at both endpoints");

    auto largeStride = makeTrack({ 30u, 31u, 32u, 33u, 34u });
    largeStride.noteColumn.direction = Direction::Palindrome;
    largeStride.noteColumn.stride = UINT32_MAX;
    events = renderTicks(std::move(largeStride), 4u);
    check(events.size() == 4u,
        "a maximum palindrome stride must remain finite and emit normally");
    for (const auto& event : events)
        check(event.note >= 30u && event.note <= 34u,
            "large palindrome stride must remain inside its active length");
}

void testSampleOffsetsAndSwing()
{
    Pattern pattern;
    pattern.tracks.push_back(makeTrack({ 60u }));
    Sequencer sequencer;
    sequencer.setPattern(pattern);
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 8u> events {};
    auto count = sequencer.process(12001u, events.data(), events.size());
    check(count == 3u, "straight timing should place three ticks in 12001 frames");
    if (count >= 3u) {
        check(events[0].frameOffset == 0u && events[1].frameOffset == 6000u
                && events[2].frameOffset == 12000u,
            "straight ticks must have sample-derived offsets");
        check(events[0].absoluteSampleTime == 0u
                && events[1].absoluteSampleTime == 6000u
                && events[2].absoluteSampleTime == 12000u,
            "straight ticks must also carry authoritative absolute time");
    }

    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.75 });
    sequencer.start();
    count = sequencer.process(12001u, events.data(), events.size());
    check(count == 3u, "swing must preserve the two-tick pair duration");
    if (count >= 3u) {
        check(events[0].frameOffset == 0u && events[1].frameOffset == 9000u
                && events[2].frameOffset == 12000u,
            "swing must lengthen then shorten alternating tick intervals");
    }
}

void testFunctionalTimingWarp()
{
    Pattern pattern;
    pattern.tracks.push_back(makeTrack({ 36u }, { 1.0f }));

    TransportSettings transport { 48000.0, 120.0, 4u, 0.5 };
    transport.warpCycleTicks = 4u;
    check(transport.timingWarp.append(
              TimingWarpTransform::exponential(2.0)).added(),
        "a valid timing curve should compile before publication");

    Sequencer sequencer;
    sequencer.setPattern(pattern);
    sequencer.setTransport(transport);
    sequencer.start();
    std::array<ScheduledEvent, 8u> events {};
    const auto count = sequencer.process(24001u, events.data(), events.size());
    check(count == 5u,
        "a four-tick warped cycle should retain all event boundaries");
    const std::array<uint64_t, 5u> expected {
        0u, 1500u, 6000u, 13500u, 24000u,
    };
    for (std::size_t index = 0u;
         index < std::min<std::size_t>(count, expected.size()); ++index) {
        check(events[index].absoluteSampleTime == expected[index],
            "exponential phase must compile into exact sample positions");
    }

    TransportSettings composed { 48000.0, 120.0, 4u, 0.6 };
    composed.warpCycleTicks = 4u;
    composed.timingWarp.append(TimingWarpTransform::exponential(2.0));
    sequencer.stop();
    sequencer.setTransport(composed);
    sequencer.start();
    const auto composedCount = sequencer.process(24001u, events.data(),
        events.size());
    check(composedCount == 5u && events[1].absoluteSampleTime == 2160u
            && events[2].absoluteSampleTime == 6000u,
        "legacy pair swing must compose before a functional timing warp");

    TransportSettings oddCycle { 48000.0, 120.0, 4u, 0.6 };
    oddCycle.warpCycleTicks = 5u;
    sequencer.stop();
    sequencer.setTransport(oddCycle);
    sequencer.start();
    const auto oddCycleCount = sequencer.process(43201u, events.data(),
        events.size());
    const std::array<uint64_t, 8u> oddCycleExpected {
        0u, 7200u, 12000u, 19200u, 24000u, 31200u, 36000u, 43200u,
    };
    check(oddCycleCount == oddCycleExpected.size(),
        "an odd warp cycle must retain global two-tick swing pairing");
    for (std::size_t index = 0u;
         index < std::min<std::size_t>(oddCycleCount,
             oddCycleExpected.size()); ++index) {
        check(events[index].absoluteSampleTime == oddCycleExpected[index],
            "warp-cycle boundaries must not reset the swing pair phase");
    }
}

void testTimingIsIndependentOfBlockPartition()
{
    Pattern pattern;
    pattern.tracks.push_back(makeTrack({ 60u }));
    const TransportSettings transport { 44100.0, 137.0, 7u, 0.63 };
    constexpr uint32_t totalFrames = 50000u;
    const auto whole = renderBlocks(pattern, transport, { totalFrames });

    std::vector<uint32_t> partitions;
    uint32_t remaining = totalFrames;
    while (remaining > 0u) {
        const uint32_t block = std::min<uint32_t>(remaining, 257u);
        partitions.push_back(block);
        remaining -= block;
    }
    const auto split = renderBlocks(std::move(pattern), transport, partitions);
    check(whole.size() == split.size(),
        "block partitioning must not change the event count");
    for (std::size_t index = 0u;
         index < std::min(whole.size(), split.size()); ++index) {
        check(whole[index].frame == split[index].frame,
            "block partitioning must not change absolute event time");
        check(whole[index].note == split[index].note
                && whole[index].normalizedVelocity
                    == split[index].normalizedVelocity,
            "block partitioning must not change event content");
    }
}

void testCanonicalScheduledEventContract()
{
    Track track;
    track.name = "canonical";
    track.midiChannel = 10u;
    track.destination = EventDestination::Both;
    track.initialInstrumentNodeId = 4u;
    track.chokeGroup = 7u;
    track.notes = {
        NoteCell::withNote(60u),
        NoteCell::retriggerPrevious(),
        NoteCell::kill(),
    };
    track.velocities = {
        ValueCell::withValue(0.5f),
        ValueCell::previous(),
        ValueCell::previous(),
    };
    track.noteColumn.length = track.notes.size();
    track.velocityColumn.length = track.velocities.size();
    Pattern pattern;
    pattern.tracks.push_back(std::move(track));

    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 4u> events {};
    const auto count = sequencer.process(12001u, events.data(), events.size());
    check(count == 4u,
        "retrigger must release its old identity before a new canonical onset");
    if (count == 4u) {
        check(events[0].absoluteSampleTime == 0u
                && events[1].absoluteSampleTime == 6000u
                && events[2].absoluteSampleTime == 6000u
                && events[3].absoluteSampleTime == 12000u,
            "canonical events must retain absolute sample time");
        check(events[0].noteId != 0u
                && events[1].noteId == events[0].noteId
                && events[2].noteId != events[0].noteId
                && events[3].noteId == events[2].noteId,
            "retrigger and kill must release the exact identities they address");
        check(events[0].track == 0u && events[0].note == 60u
                && events[0].channel == 10u
                && events[0].targetNode == 4u
                && events[1].targetNode == 4u
                && events[2].targetNode == 4u
                && events[3].targetNode == 4u
                && events[0].normalizedVelocity == 0.5f,
            "canonical note data and resolved rack routing must remain independent of MIDI encoding");
        check(events[0].durationSamples == 0u
                && events[0].chokeGroup == 7u,
            "duration and choke placeholders must be explicit");
        check(events[0].kind == ScheduledEventKind::NoteOn
                && events[1].kind == ScheduledEventKind::NoteOff
                && events[2].kind == ScheduledEventKind::NoteOn
                && events[3].kind == ScheduledEventKind::NoteOff,
            "canonical events must identify note action kind");
        check(routesToInternal(events[0].destination)
                && !routesToMidi(events[0].destination),
            "known internal rack slots must override legacy Both routing");
    }

    ScheduledEvent parameter;
    parameter.absoluteSampleTime = 123u;
    parameter.frameOffset = 3u;
    parameter.track = 2u;
    parameter.parameterId = 42u;
    parameter.parameterValue = 0.25f;
    parameter.kind = ScheduledEventKind::Parameter;
    parameter.destination = EventDestination::Internal;
    check(parameter.noteId == 0u && parameter.parameterId == 42u
            && parameter.parameterValue == 0.25f
            && parameter.targetNode == kInvalidInstrumentNode
            && parameter.parameterScope == ParameterScope::Global
            && routesToInternal(parameter.destination)
            && !routesToMidi(parameter.destination),
        "the canonical contract must represent internal parameter actions");
    check(Track {}.initialInstrumentNodeId == 0u
            && kMembraneRackSlotCount == 5u
            && FxActionCell::parameter(3u).targetNode
                == kTrackInstrumentNode,
        "rack defaults and lane-relative FX must have stable model sentinels");
    check(routesToMidi(EventDestination::Midi)
            && !routesToInternal(EventDestination::Midi)
            && !routesToMidi(EventDestination::None),
        "destination tests must be explicit and allocation-free");
    check(midiVelocityFromNormalized(-1.0f) == 1u
            && midiVelocityFromNormalized(0.5f) == 64u
            && midiVelocityFromNormalized(2.0f) == 127u,
        "the MIDI velocity edge adapter must clamp and round normalized values");
}

void testInstrumentCellValidation()
{
    Track track;
    track.destination = EventDestination::Internal;
    track.initialInstrumentNodeId = 99u;
    track.notes = { NoteCell::withNote(60u) };
    track.instruments = {
        InstrumentCell::withInstrument(99u),
        InstrumentCell::previous(),
        InstrumentCell::withInstrument(2u),
    };
    track.instruments[2u].state = static_cast<InstrumentCellState>(255u);
    track.velocities = { ValueCell::withValue(1.0f) };
    track.noteColumn.length = 1u;
    track.instrumentColumn.length = track.instruments.size();
    track.velocityColumn.length = 1u;
    Pattern pattern;
    pattern.tracks.push_back(std::move(track));

    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    const auto& normalized = sequencer.pattern().tracks[0u];
    check(normalized.initialInstrumentNodeId == kInvalidInstrumentNode
            && normalized.instruments[0u].state
                == InstrumentCellState::Empty
            && normalized.instruments[0u].nodeId == kInvalidInstrumentNode
            && normalized.instruments[1u].state
                == InstrumentCellState::Previous
            && normalized.instruments[1u].nodeId == kInvalidInstrumentNode
            && normalized.instruments[2u].state
                == InstrumentCellState::Empty,
        "invalid initial nodes, explicit nodes, and enum states must fail closed");
    sequencer.start();
    std::array<ScheduledEvent, 1u> event {};
    const auto count = sequencer.process(1u, event.data(), event.size());
    check(count == 1u && event[0u].targetNode == kInvalidInstrumentNode,
        "an invalid default must stay explicit instead of aliasing a rack slot");

    Track fallbackRoute;
    fallbackRoute.destination = EventDestination::Internal;
    fallbackRoute.initialInstrumentNodeId = kInvalidInstrumentNode;
    fallbackRoute.notes = { NoteCell::withNote(61u), NoteCell::rest() };
    fallbackRoute.velocities = { ValueCell::withValue(1.0f) };
    fallbackRoute.noteColumn.length = fallbackRoute.notes.size();
    fallbackRoute.velocityColumn.length = 1u;
    fallbackRoute.fxPairs[0u].actions = {
        FxActionCell::empty(),
        FxActionCell::parameter(7u, ParameterScope::Note),
    };
    fallbackRoute.fxPairs[0u].values = {
        FxValueCell::withValue(0.0f),
        FxValueCell::withValue(0.5f),
    };
    fallbackRoute.fxPairs[0u].actionColumn.length = 2u;
    fallbackRoute.fxPairs[0u].valueColumn.length = 2u;
    Pattern fallbackPattern;
    fallbackPattern.tracks.push_back(std::move(fallbackRoute));
    Sequencer fallbackSequencer;
    fallbackSequencer.setPattern(std::move(fallbackPattern));
    fallbackSequencer.start();
    std::array<ScheduledEvent, 3u> fallbackEvents {};
    const auto fallbackCount = fallbackSequencer.process(6001u,
        fallbackEvents.data(), fallbackEvents.size());
    check(fallbackCount == 2u
            && fallbackEvents[0u].kind == ScheduledEventKind::NoteOn
            && fallbackEvents[0u].targetNode == kInvalidInstrumentNode
            && fallbackEvents[1u].kind == ScheduledEventKind::Parameter
            && fallbackEvents[1u].parameterScope == ParameterScope::Note
            && fallbackEvents[1u].noteId == fallbackEvents[0u].noteId
            && fallbackEvents[1u].destination
                == EventDestination::Internal,
        "a fallback-routed active note must keep receiving Note-scoped FX on rest rows");
}

void testRetriggerRestAndKillSemantics()
{
    Track track;
    track.notes = {
        NoteCell::withNote(60u),
        NoteCell::retriggerPrevious(),
        NoteCell::rest(),
        NoteCell::retriggerPrevious(),
        NoteCell::kill(),
        NoteCell::retriggerPrevious(),
    };
    track.velocities = {
        ValueCell::withValue(0.5f),
        ValueCell::previous(),
        ValueCell::withValue(1.0f),
        ValueCell::previous(),
        ValueCell::previous(),
        ValueCell::previous(),
    };
    track.noteColumn.length = track.notes.size();
    track.velocityColumn.length = track.velocities.size();
    const auto events = renderTicks(std::move(track), 6u);
    check(events.size() == 6u,
        "retrigger releases/restarts, rest retains memory, and kill suppresses later retrigger");
    if (events.size() >= 5u) {
        check(events[0].normalizedVelocity == 0.5f
                && events[2].normalizedVelocity == 0.5f
                && midiVelocityFromNormalized(events[0].normalizedVelocity)
                    == 64u,
            "a previous velocity cell must reuse the prior scalar value");
        check(events[4].normalizedVelocity == 1.0f
                && midiVelocityFromNormalized(
                    events[4].normalizedVelocity) == 127u,
            "a retrigger after rest must use the current velocity column");
        check(events[1].kind == ScheduledEventKind::NoteOff
                && events[1].noteId == events[0].noteId
                && events[3].kind == ScheduledEventKind::NoteOff
                && events[3].noteId == events[2].noteId,
            "every retrigger must release the immediately previous identity");
    }
    if (events.size() >= 6u) {
        check(events[5].kind == ScheduledEventKind::NoteOff
                && events[5].note == 60u
                && events[5].noteId == events[4].noteId,
            "kill must identify the currently remembered note for cleanup");
    }
}

void testBoundedOutput()
{
    Pattern pattern;
    pattern.tracks.push_back(makeTrack({ 36u }));
    pattern.tracks.push_back(makeTrack({ 38u }));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.start();
    std::array<ScheduledEvent, 1u> event {};
    const auto count = sequencer.process(1u, event.data(), event.size());
    check(count == 1u, "bounded output should write only available capacity");
    check(sequencer.droppedEventCount() == 1u,
        "bounded output must account for dropped triggers");
}

void testDisabledRoutesDoNotConsumeEventCapacity()
{
    auto disabled = makeTrack({ 36u });
    disabled.destination = EventDestination::None;
    disabled.initialInstrumentNodeId = kInvalidInstrumentNode;
    auto routed = makeTrack({ 38u });
    routed.destination = EventDestination::Midi;
    routed.initialInstrumentNodeId = s3g::tracker::kMidiOutInstrumentNode;
    Pattern pattern;
    pattern.tracks.push_back(std::move(disabled));
    pattern.tracks.push_back(std::move(routed));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.start();
    std::array<ScheduledEvent, 1u> event {};
    const auto count = sequencer.process(1u, event.data(), event.size());
    check(count == 1u && event[0].track == 1u && event[0].note == 38u,
        "a none-routed lane must not starve a later routed event");
    check(sequencer.droppedEventCount() == 0u,
        "discarding a none-routed event must not report output overflow");
}

void testQuantizedWarpCollisionPolicy()
{
    auto track = makeTrack({ 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u });
    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    TransportSettings transport { 48000.0, 120.0, 4u, 0.5 };
    transport.warpCycleTicks = 8u;
    transport.timingWarp.append(TimingWarpTransform::stepQuantize(1u));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport(transport);
    sequencer.start();
    std::array<ScheduledEvent, 2u> events {};
    const auto count = sequencer.process(1u, events.data(), events.size());
    check(count == 2u && sequencer.tickIndex() == 8u,
        "coincident quantized ticks must all advance in stable tick order");
    check(events[0].note == 36u && events[1].note == 37u
            && events[0].absoluteSampleTime == 0u
            && events[1].absoluteSampleTime == 0u,
        "a collision must retain the deterministic capacity-limited prefix");
    check(sequencer.droppedEventCount() == 6u,
        "coincident events beyond fixed output capacity must be counted");
}

void testSingleTickSteppingPreservesWarpCollisions()
{
    auto track = makeTrack({ 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u });
    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    TransportSettings transport { 48000.0, 120.0, 4u, 0.5 };
    transport.warpCycleTicks = 8u;
    transport.timingWarp.append(TimingWarpTransform::stepQuantize(1u));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport(transport);
    sequencer.start();

    std::array<ScheduledEvent, 1u> event {};
    for (uint8_t note = 36u; note <= 43u; ++note) {
        const auto count = sequencer.processSingleTick(
            1u, event.data(), event.size());
        check(count == 1u && event[0].note == note
                && event[0].absoluteSampleTime == 0u,
            "single-tick stepping must preserve coincident warped rows in order");
        check(sequencer.renderedFrameCount() == 0u,
            "the stepped seam must hold the block clock while a coincident tick remains");
    }
    check(sequencer.tickIndex() == 8u,
        "single-tick stepping must consume every coincident warped row");
    const auto drained = sequencer.processSingleTick(
        1u, event.data(), event.size());
    check(drained == 0u && sequencer.renderedFrameCount() == 1u,
        "the stepped seam must advance the block clock after due ticks drain");
}

void testExtremeContinuousWarpCollisionPolicy()
{
    Pattern pattern;
    pattern.tracks.push_back(makeTrack({ 36u }));
    TransportSettings transport { 48000.0, 120.0, 4u, 0.5 };
    transport.warpCycleTicks = 1024u;
    transport.timingWarp.append(TimingWarpTransform::exponential(64.0));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport(transport);
    sequencer.start();
    std::array<ScheduledEvent, 2u> events {};
    const auto count = sequencer.process(1u, events.data(), events.size());
    check(count == events.size() && sequencer.tickIndex() > 700u,
        "extreme continuous warps must expose rounded sample collisions deterministically");
    check(sequencer.droppedEventCount() == sequencer.tickIndex() - count,
        "continuous-warp collision overflow must retain exact telemetry");
    check(events[0].absoluteSampleTime == 0u
            && events[1].absoluteSampleTime == 0u
            && sequencer.nextTickSampleFrame() >= 1u,
        "the bounded collision prefix must recover at the next sample");
}

void testInitialFxCollisionBudget()
{
    Pattern pattern;
    for (std::size_t lane = 0u; lane < 8u; ++lane) {
        Track track;
        track.destination = EventDestination::Internal;
        track.notes.assign(16u, NoteCell::retriggerPrevious());
        track.notes[0u] = NoteCell::withNote(
            static_cast<uint8_t>(36u + lane));
        track.velocities = { ValueCell::withValue(1.0f) };
        track.noteColumn.length = track.notes.size();
        track.velocityColumn.length = 1u;
        track.instruments.reserve(16u);
        for (std::size_t row = 0u; row < 16u; ++row) {
            track.instruments.push_back(InstrumentCell::withInstrument(
                static_cast<uint32_t>(row % kMembraneRackSlotCount)));
        }
        track.instrumentColumn.length = track.instruments.size();
        for (std::size_t pair = 0u; pair < 2u; ++pair) {
            auto& fx = track.fxPairs[pair];
            fx.actions.assign(16u, FxActionCell::previous());
            fx.actions[0u] = FxActionCell::parameter(
                static_cast<uint32_t>(3u + pair));
            fx.values.assign(16u, FxValueCell::withValue(
                pair == 0u ? 0.25f : 0.75f));
            fx.actionColumn.length = fx.actions.size();
            fx.valueColumn.length = fx.values.size();
        }
        pattern.tracks.push_back(std::move(track));
    }
    TransportSettings transport { 48000.0, 120.0, 4u, 0.5 };
    transport.warpCycleTicks = 16u;
    transport.timingWarp.append(TimingWarpTransform::stepQuantize(1u));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport(transport);
    sequencer.start();
    std::array<ScheduledEvent,
        s3g::tracker::kMaximumScheduledEventsPerBlock> events {};
    const auto count = sequencer.process(1u, events.data(), events.size());
    check(count == 504u && sequencer.tickIndex() == 16u,
        "the initial eight-lane/two-FX 16-tick collision must fit its declared budget");
    check(sequencer.droppedEventCount() == 0u,
        "the declared live FX collision budget must not drop canonical events");
    check(sequencer.instrumentPosition(0u) == 0u
            && sequencer.lastInstrumentPosition(0u) == 15u,
        "INS memory must advance through a dense live block without adding events");
    for (std::size_t index = 0u; index < count; ++index)
        check(events[index].absoluteSampleTime == 0u,
            "the dense FX collision must preserve its exact shared sample time");
}

void testNullOutputStillAdvancesPlayback()
{
    Pattern pattern;
    pattern.tracks.push_back(makeTrack({ 36u, 38u }));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    const auto firstCount = sequencer.process(1u, nullptr, 0u);
    check(firstCount == 0u && sequencer.droppedEventCount() == 1u,
        "null output must count a due trigger as dropped");

    std::array<ScheduledEvent, 2u> events {};
    const auto secondCount = sequencer.process(6000u, events.data(),
        events.size());
    check(secondCount == 1u,
        "playback must continue after a block with no output storage");
    if (secondCount == 1u) {
        check(events[0].note == 38u && events[0].frameOffset == 5999u,
            "dropped output must not prevent column or timeline advancement");
        check(events[0].absoluteSampleTime == 6000u,
            "absolute event time must be independent of block-relative offset");
        check(events[0].noteId == 2u,
            "note identity must advance even when an earlier event is dropped");
    }
}

void testRandomPlaybackRestartsDeterministically()
{
    auto track = makeTrack({ 40u, 41u, 42u, 43u, 44u, 45u, 46u });
    track.noteColumn.direction = Direction::Random;
    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setRandomSeed(12345u);
    sequencer.start();
    std::array<ScheduledEvent, 8u> first {};
    std::array<ScheduledEvent, 8u> second {};
    const auto firstCount = sequencer.process(30001u, first.data(), first.size());
    sequencer.start(true);
    const auto secondCount = sequencer.process(30001u, second.data(), second.size());
    check(firstCount == secondCount,
        "a restart must reproduce the same random event count");
    for (std::size_t index = 0u; index < std::min(firstCount, secondCount);
         ++index) {
        check(first[index].note == second[index].note,
            "a restart must reproduce seeded random column positions");
    }
}

void testRandomColumnsHaveIndependentStreams()
{
    auto randomTrack = makeTrack({ 40u, 41u, 42u, 43u, 44u, 45u, 46u });
    randomTrack.noteColumn.direction = Direction::Random;
    Pattern oneTrack;
    oneTrack.tracks.push_back(randomTrack);

    Pattern twoTracks = oneTrack;
    auto second = makeTrack({ 70u, 71u, 72u, 73u, 74u });
    second.noteColumn.direction = Direction::Random;
    twoTracks.tracks.push_back(std::move(second));

    const TransportSettings transport { 48000.0, 120.0, 4u, 0.5 };
    const auto alone = renderBlocks(std::move(oneTrack), transport, { 42001u },
        9981u);
    const auto together = renderBlocks(std::move(twoTracks), transport,
        { 42001u }, 9981u);
    std::vector<uint8_t> firstLaneTogether;
    for (const auto& event : together) {
        if (event.track == 0u) firstLaneTogether.push_back(event.note);
    }
    check(alone.size() == firstLaneTogether.size(),
        "adding another random lane must not change the first lane count");
    for (std::size_t index = 0u;
         index < std::min(alone.size(), firstLaneTogether.size()); ++index) {
        check(alone[index].note == firstLaneTogether[index],
            "each random lane must own an independent stream");
    }
}

void testLivePatternReplacementRetainsPhase()
{
    auto firstTrack = makeTrack({ 36u, 38u, 40u });
    firstTrack.instruments = {
        InstrumentCell::withInstrument(1u),
        InstrumentCell::withInstrument(2u),
        InstrumentCell::withInstrument(3u),
    };
    firstTrack.instrumentColumn.length = firstTrack.instruments.size();
    Pattern first;
    first.tracks.push_back(std::move(firstTrack));
    Sequencer sequencer;
    sequencer.setPattern(std::move(first));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 2u> events {};
    check(sequencer.process(1u, events.data(), events.size()) == 1u,
        "the initial live-pattern tick should render");
    check(sequencer.notePosition(0u) == 1u
            && sequencer.instrumentPosition(0u) == 1u,
        "published NOTE and INS positions should advance after a tick");
    check(sequencer.lastNotePosition(0u) == 0u
            && sequencer.lastInstrumentPosition(0u) == 0u
            && sequencer.lastVelocityPosition(0u) == 0u,
        "the published last positions should identify the rendered cells");

    auto replacementTrack = makeTrack({ 60u, 62u });
    replacementTrack.instruments = {
        InstrumentCell::withInstrument(4u),
        InstrumentCell::withInstrument(3u),
    };
    replacementTrack.instrumentColumn.length
        = replacementTrack.instruments.size();
    Pattern replacement;
    replacement.tracks.push_back(replacementTrack);
    sequencer.replacePattern(std::move(replacement));
    check(sequencer.isPlaying() && sequencer.tickIndex() == 1u,
        "live replacement must retain transport and musical time");
    check(sequencer.notePosition(0u) == 1u
            && sequencer.instrumentPosition(0u) == 1u,
        "live replacement must retain valid NOTE and INS phases");
    const auto count = sequencer.process(6000u, events.data(), events.size());
    check(count == 1u && events[0].note == 62u
            && events[0].targetNode == 3u,
        "the next tick must read the replacement at the retained position");
    check(sequencer.lastNotePosition(0u) == 1u
            && sequencer.lastInstrumentPosition(0u) == 1u,
        "rendered NOTE and INS positions must follow live replacement");

    Pattern expanded;
    expanded.tracks.push_back(std::move(replacementTrack));
    expanded.tracks.push_back(makeTrack({ 70u }));
    sequencer.replacePattern(std::move(expanded));
    check(sequencer.pattern().tracks.size() == 2u
            && sequencer.notePosition(1u) == 0u,
        "a newly published lane must start at row zero");
    const auto expandedCount = sequencer.process(6000u, events.data(),
        events.size());
    check(expandedCount == 2u && events[1].track == 1u
            && events[1].note == 70u,
        "a newly published lane must emit from row zero on the next tick");
}

void testSongBoundaryPatternReplacementRetainsClockAndOwnership()
{
    auto sourceTrack = makeTrack({ 60u });
    sourceTrack.destination = EventDestination::Internal;
    sourceTrack.initialInstrumentNodeId = 1u;
    sourceTrack.fxPairs[0u].actions = {
        FxActionCell::parameter(7u, ParameterScope::Global,
            kTrackInstrumentNode),
    };
    sourceTrack.fxPairs[0u].values = {
        FxValueCell::withValue(0.4f),
    };
    sourceTrack.fxPairs[0u].actionColumn.length = 1u;
    sourceTrack.fxPairs[0u].valueColumn.length = 1u;
    Pattern source;
    source.tracks.push_back(sourceTrack);

    auto targetTrack = makeTrack({ 70u, 71u, 72u });
    targetTrack.destination = EventDestination::Internal;
    targetTrack.initialInstrumentNodeId = 4u;
    targetTrack.noteColumn.phase = 1u;
    targetTrack.fxPairs[0u].actions = { FxActionCell::previous() };
    targetTrack.fxPairs[0u].values = { FxValueCell::previous() };
    targetTrack.fxPairs[0u].actionColumn.length = 1u;
    targetTrack.fxPairs[0u].valueColumn.length = 1u;
    Pattern target;
    target.tracks.push_back(std::move(targetTrack));

    Sequencer sequencer;
    check(sequencer.preparePatternSet(
              { std::move(source), std::move(target) }, 0u),
        "the Song-boundary patterns must prepare while stopped");
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 8u> events {};
    const auto sourceCount = sequencer.processSingleTick(
        1u, events.data(), events.size());
    const auto sourceOnset = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(sourceCount),
        [](const ScheduledEvent& event) {
            return event.kind == ScheduledEventKind::NoteOn;
        });
    check(sourceOnset != events.begin()
            + static_cast<std::ptrdiff_t>(sourceCount)
            && sourceOnset->note == 60u
            && sourceOnset->targetNode == 1u,
        "the Song-boundary fixture must establish an active source owner");
    const uint64_t sourceNoteId = sourceOnset == events.begin()
            + static_cast<std::ptrdiff_t>(sourceCount)
        ? 0u : sourceOnset->noteId;
    const auto retainedFx = sequencer.fxMemorySnapshot(0u, 0u);
    const uint64_t tickBefore = sequencer.tickIndex();
    const uint64_t rowBefore = sequencer.transportRow();
    const uint64_t renderedBefore = sequencer.renderedFrameCount();
    const uint64_t nextTickBefore = sequencer.nextTickSampleFrame();

    check(sequencer.activatePreparedPatternAtTickBoundary(1u),
        "the prepared target pattern must activate at the boundary");
    sequencer.relaunchColumnsAtTickBoundary(0u);
    const auto recalledFx = sequencer.fxMemorySnapshot(0u, 0u);
    check(sequencer.isPlaying()
            && sequencer.tickIndex() == tickBefore
            && sequencer.transportRow() == rowBefore
            && sequencer.renderedFrameCount() == renderedBefore
            && sequencer.nextTickSampleFrame() == nextTickBefore,
        "a Song pattern swap must not reset or re-anchor the master clock");
    check(sequencer.notePosition(0u) == 1u,
        "a Song pattern swap must launch target row zero plus target phase");
    check(retainedFx.hasAction && retainedFx.hasValue
            && recalledFx.hasAction && recalledFx.hasValue
            && recalledFx.action.parameterId == 7u
            && std::abs(recalledFx.value - 0.4f) < 0.00001f,
        "a Song pattern swap must retain FX Previous recall across patterns");

    const auto targetCount = sequencer.processSingleTick(
        6001u, events.data(), events.size());
    const auto sourceRelease = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(targetCount),
        [sourceNoteId](const ScheduledEvent& event) {
            return event.kind == ScheduledEventKind::NoteOff
                && event.noteId == sourceNoteId;
        });
    const auto targetOnset = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(targetCount),
        [](const ScheduledEvent& event) {
            return event.kind == ScheduledEventKind::NoteOn;
        });
    check(sourceRelease != events.begin()
            + static_cast<std::ptrdiff_t>(targetCount)
            && sourceRelease->note == 60u
            && sourceRelease->targetNode == 1u,
        "the first target tick must release the source pattern's true owner");
    check(targetOnset != events.begin()
            + static_cast<std::ptrdiff_t>(targetCount)
            && targetOnset->note == 71u
            && targetOnset->targetNode == 4u
            && targetOnset->noteId != sourceNoteId,
        "the first target tick must render its phased note on its own instrument");
}

void testSongBoundaryRevisitResetsNonOverlappingTracks()
{
    const auto makeA = [] {
        Pattern pattern;
        auto lane = makeTrack({ 50u });
        lane.destination = EventDestination::Internal;
        lane.initialInstrumentNodeId = 1u;
        pattern.tracks.push_back(std::move(lane));
        return pattern;
    };
    const auto makeB = [] {
        Pattern pattern;
        auto sharedLane = makeTrack({ 60u });
        sharedLane.destination = EventDestination::Internal;
        sharedLane.initialInstrumentNodeId = 1u;
        pattern.tracks.push_back(std::move(sharedLane));

        Track extraLane;
        extraLane.destination = EventDestination::Internal;
        extraLane.initialInstrumentNodeId = 4u;
        extraLane.notes = {
            NoteCell::retriggerPrevious(),
            NoteCell::withNote(80u),
        };
        extraLane.velocities = { ValueCell::withValue(1.0f) };
        extraLane.noteColumn.length = extraLane.notes.size();
        extraLane.velocityColumn.length = extraLane.velocities.size();
        pattern.tracks.push_back(std::move(extraLane));
        return pattern;
    };
    const auto swapAtBoundary = [](Sequencer& sequencer,
                                    std::size_t patternIndex) {
        const uint64_t tick = sequencer.tickIndex();
        const uint64_t row = sequencer.transportRow();
        const uint64_t rendered = sequencer.renderedFrameCount();
        const uint64_t nextTick = sequencer.nextTickSampleFrame();
        check(sequencer.activatePreparedPatternAtTickBoundary(patternIndex),
            "the indexed prepared pattern must activate at the boundary");
        sequencer.relaunchColumnsAtTickBoundary(0u);
        check(sequencer.tickIndex() == tick
                && sequencer.transportRow() == row
                && sequencer.renderedFrameCount() == rendered
                && sequencer.nextTickSampleFrame() == nextTick,
            "every A/B Song swap must preserve the master clock exactly");
    };

    Sequencer sequencer;
    check(sequencer.preparePatternSet({ makeA(), makeB() }, 0u),
        "the A/B revisit set must prepare while stopped");
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 8u> events {};
    (void)sequencer.processSingleTick(1u, events.data(), events.size());

    swapAtBoundary(sequencer, 1u);
    auto count = sequencer.processSingleTick(
        6001u, events.data(), events.size());
    check(std::none_of(events.begin(),
              events.begin() + static_cast<std::ptrdiff_t>(count),
              [](const ScheduledEvent& event) {
                  return event.track == 1u
                      && event.kind == ScheduledEventKind::NoteOn;
              }),
        "a newly added target lane must not inherit retrigger memory");
    count = sequencer.processSingleTick(
        12001u, events.data(), events.size());
    const auto extraOnset = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(count),
        [](const ScheduledEvent& event) {
            return event.track == 1u
                && event.kind == ScheduledEventKind::NoteOn;
        });
    check(extraOnset != events.begin()
            + static_cast<std::ptrdiff_t>(count)
            && extraOnset->note == 80u,
        "the first B visit must establish playback memory on its extra lane");

    const uint64_t extraNoteId = extraOnset == events.begin()
            + static_cast<std::ptrdiff_t>(count)
        ? 0u : extraOnset->noteId;
    swapAtBoundary(sequencer, 0u);
    count = sequencer.processSingleTick(
        18001u, events.data(), events.size());
    const auto removedRelease = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(count),
        [extraNoteId](const ScheduledEvent& event) {
            return event.track == 1u
                && event.kind == ScheduledEventKind::NoteOff
                && event.noteId == extraNoteId;
        });
    check(removedRelease != events.begin()
            + static_cast<std::ptrdiff_t>(count)
            && removedRelease->note == 80u
            && removedRelease->targetNode == 4u
            && count >= 2u
            && events[0u].kind == ScheduledEventKind::NoteOff
            && events[0u].track == 1u
            && events[1u].kind == ScheduledEventKind::NoteOn
            && events[1u].track == 0u && events[1u].note == 50u,
        "a removed source lane must release its exact owner before target events");
    swapAtBoundary(sequencer, 1u);
    count = sequencer.processSingleTick(
        24001u, events.data(), events.size());
    check(std::none_of(events.begin(),
              events.begin() + static_cast<std::ptrdiff_t>(count),
              [](const ScheduledEvent& event) {
                  return event.track == 1u
                      && event.kind == ScheduledEventKind::NoteOn;
              })
            && std::any_of(events.begin(),
              events.begin() + static_cast<std::ptrdiff_t>(count),
              [](const ScheduledEvent& event) {
                  return event.track == 0u
                      && event.kind == ScheduledEventKind::NoteOn
                      && event.note == 60u;
              }),
        "A-to-B revisit must reset the previously removed extra lane rather than resurrecting its active memory");
}

void testLiveInstrumentReassignmentRetainsReleaseTarget()
{
    auto firstTrack = makeTrack({ 36u });
    firstTrack.notes.push_back(NoteCell::retriggerPrevious());
    firstTrack.noteColumn.length = firstTrack.notes.size();
    firstTrack.destination = EventDestination::Both;
    firstTrack.initialInstrumentNodeId = 1u;
    Pattern first;
    first.tracks.push_back(firstTrack);

    Sequencer sequencer;
    sequencer.setPattern(std::move(first));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 3u> events {};
    const auto onsetCount = sequencer.process(1u, events.data(),
        events.size());
    check(onsetCount == 1u
            && events[0u].kind == ScheduledEventKind::NoteOn
            && events[0u].targetNode == 1u,
        "an onset must resolve to the lane's current membrane slot");

    auto replacementTrack = firstTrack;
    replacementTrack.initialInstrumentNodeId = 4u;
    Pattern replacement;
    replacement.tracks.push_back(std::move(replacementTrack));
    sequencer.replacePattern(std::move(replacement));
    const auto reassignedCount = sequencer.process(6000u, events.data(),
        events.size());
    check(reassignedCount == 2u
            && events[0u].kind == ScheduledEventKind::NoteOff
            && events[0u].targetNode == 1u
            && events[1u].kind == ScheduledEventKind::NoteOn
            && events[1u].targetNode == 4u
            && events[0u].noteId != events[1u].noteId,
        "live reassignment must release the onset slot before triggering the new slot");

    auto heldTrack = makeTrack({ 42u });
    heldTrack.notes.push_back(NoteCell::rest());
    heldTrack.noteColumn.length = heldTrack.notes.size();
    heldTrack.initialInstrumentNodeId = 1u;
    Pattern heldPattern;
    heldPattern.tracks.push_back(heldTrack);
    Sequencer heldSequencer;
    heldSequencer.setPattern(std::move(heldPattern));
    heldSequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    heldSequencer.start();
    const auto heldOnsetCount = heldSequencer.process(
        1u, events.data(), events.size());
    check(heldOnsetCount == 1u
            && events[0u].kind == ScheduledEventKind::NoteOn
            && events[0u].note == 42u && events[0u].targetNode == 1u,
        "the held reassignment case must establish its old owner");

    heldTrack.initialInstrumentNodeId = 4u;
    Pattern heldReplacement;
    heldReplacement.tracks.push_back(std::move(heldTrack));
    heldSequencer.replacePattern(std::move(heldReplacement));
    const auto releaseCount = heldSequencer.process(
        6000u, events.data(), events.size());
    check(releaseCount == 1u
            && events[0u].kind == ScheduledEventKind::NoteOff
            && events[0u].note == 42u && events[0u].targetNode == 1u
            && heldSequencer.isPlaying(),
        "live reassignment on a rest must release the old owner without "
        "stopping transport");
    const auto nextOnsetCount = heldSequencer.process(
        6000u, events.data(), events.size());
    check(nextOnsetCount == 1u
            && events[0u].kind == ScheduledEventKind::NoteOn
            && events[0u].note == 42u && events[0u].targetNode == 4u,
        "the next onset after live reassignment must use the new owner");
}

void testReadableMidiNoteParsing()
{
    uint8_t note = 255u;
    check(parseMidiNote("60", note) && note == 60u,
        "decimal MIDI entry should accept 0 through 127");
    check(parseMidiNote("C-4", note) && note == 60u
            && parseMidiNote("c4", note) && note == 60u,
        "natural notes should accept tracker and compact spellings");
    check(parseMidiNote("F#3", note) && note == 54u
            && parseMidiNote("Gb3", note) && note == 54u,
        "note entry should accept sharps and flats");
    check(parseMidiNote("C--1", note) && note == 0u
            && parseMidiNote("G9", note) && note == 127u,
        "readable note entry should cover the complete MIDI range");
    check(!parseMidiNote("128", note) && !parseMidiNote("H2", note)
            && !parseMidiNote("C#", note) && !parseMidiNote("B#9", note),
        "invalid and out-of-range note text must fail closed");
}

void testLastNoteTriggeredTracksActualOnsets()
{
    Track track;
    track.destination = EventDestination::Internal;
    track.notes = {
        NoteCell::retriggerPrevious(), NoteCell::withNote(60u),
        NoteCell::rest(), NoteCell::retriggerPrevious(),
    };
    track.noteColumn.length = track.notes.size();
    Pattern pattern;
    pattern.tracks.push_back(track);

    Sequencer sequencer;
    sequencer.setPattern(pattern);
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 4u> events {};
    (void)sequencer.process(1u, events.data(), events.size());
    check(sequencer.lastNotePosition(0u) == 0u
            && !sequencer.lastNoteTriggered(0u),
        "an unresolved retrigger must not display as an actual hit");
    (void)sequencer.process(6000u, events.data(), events.size());
    check(sequencer.lastNotePosition(0u) == 1u
            && sequencer.lastNoteTriggered(0u),
        "an authored note onset must display as an actual hit");
    (void)sequencer.process(6000u, events.data(), events.size());
    check(sequencer.lastNotePosition(0u) == 2u
            && !sequencer.lastNoteTriggered(0u),
        "a rest must clear the actual-hit display state");
    (void)sequencer.process(6000u, events.data(), events.size());
    check(sequencer.lastNotePosition(0u) == 3u
            && sequencer.lastNoteTriggered(0u),
        "a resolved retrigger must display as an actual hit");

    track.noteColumn.muted = true;
    pattern.tracks[0u] = track;
    sequencer.replacePattern(std::move(pattern));
    (void)sequencer.process(6000u, events.data(), events.size());
    check(!sequencer.lastNoteTriggered(0u),
        "a muted NOTE column must not display a hit");
    check(!sequencer.lastNoteTriggered(99u),
        "an unknown lane must not report an actual hit");
}

void testGlobalLoopRowsAndPauseResume()
{
    Track track = makeTrack({ 60u, 61u, 62u, 63u, 64u, 65u, 66u, 67u });
    track.destination = EventDestination::Internal;
    track.instrumentColumn.length = 8u;
    track.instruments.assign(8u, InstrumentCell::withInstrument(0u));
    track.velocityColumn.length = 8u;
    track.velocities.assign(8u, ValueCell::withValue(1.0f));
    for (auto& pair : track.fxPairs) {
        pair.actionColumn.length = 8u;
        pair.valueColumn.length = 8u;
        pair.actions.assign(8u, FxActionCell::empty());
        pair.values.assign(8u, FxValueCell::previous());
    }
    Pattern pattern;
    pattern.visibleRows = 8u;
    pattern.tracks.push_back(track);
    TransportSettings transport;
    transport.sampleRate = 48000.0;
    transport.bpm = 120.0;
    transport.ticksPerBeat = 4u;
    transport.loopEnabled = true;
    transport.loopStartRow = 2u;
    transport.loopEndRow = 5u;

    Sequencer looped;
    looped.setPattern(pattern);
    looped.setTransport(transport);
    looped.start();
    std::array<ScheduledEvent, 32u> events {};
    const auto count = looped.process(36001u, events.data(), events.size());
    std::vector<uint8_t> onsets;
    for (std::size_t index = 0u; index < count; ++index) {
        if (events[index].kind == ScheduledEventKind::NoteOn)
            onsets.push_back(events[index].note);
    }
    check(onsets == std::vector<uint8_t>({ 60u, 61u, 62u, 63u, 64u,
                62u, 63u }),
        "global row loop should wrap every column to its selected start row");
    check(looped.transportRow() == 4u
            && looped.notePosition(0u) == 4u
            && looped.instrumentPosition(0u) == 4u
            && looped.velocityPosition(0u) == 4u
            && looped.fxActionPosition(0u, 0u) == 4u
            && looped.fxValuePosition(0u, 1u) == 4u,
        "global row loop should keep all polymetric column heads aligned at wrap");

    transport.loopEnabled = false;
    Sequencer paused;
    paused.setPattern(std::move(pattern));
    paused.setTransport(transport);
    paused.start();
    (void)paused.process(6001u, events.data(), events.size());
    paused.stop();
    paused.start(false);
    const auto resumedCount = paused.process(1u, events.data(), events.size());
    const auto resumed = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(resumedCount),
        [](const ScheduledEvent& event) {
            return event.kind == ScheduledEventKind::NoteOn;
        });
    check(resumed != events.begin()
            + static_cast<std::ptrdiff_t>(resumedCount)
            && resumed->note == 62u && resumed->frameOffset == 0u,
        "pause/resume should retain phase and rebase its next event to frame zero");
    paused.stop();
    paused.start(true);
    const auto restartedCount = paused.process(1u, events.data(), events.size());
    const auto restarted = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(restartedCount),
        [](const ScheduledEvent& event) {
            return event.kind == ScheduledEventKind::NoteOn;
        });
    check(restarted != events.begin()
            + static_cast<std::ptrdiff_t>(restartedCount)
            && restarted->note == 60u,
        "stop followed by play should return to the beginning");
}

void testIndependentColumnPhase()
{
    auto track = makeTrack({ 60u, 61u, 62u, 63u }, { 0.2f, 0.8f });
    track.noteColumn.phase = 2u;
    track.velocityColumn.phase = 1u;
    Pattern pattern;
    pattern.tracks.push_back(track);
    Sequencer sequencer;
    sequencer.setPattern(pattern);
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 8u> events {};
    const auto count = sequencer.process(12001u, events.data(), events.size());
    std::vector<uint8_t> notes;
    std::vector<float> velocities;
    for (std::size_t index = 0u; index < count; ++index) {
        if (events[index].kind != ScheduledEventKind::NoteOn) continue;
        notes.push_back(events[index].note);
        velocities.push_back(events[index].normalizedVelocity);
    }
    check(notes == std::vector<uint8_t>({ 62u, 63u, 60u })
            && velocities == std::vector<float>({ 0.8f, 0.2f, 0.8f }),
        "reset should seed NOTE and VEL from their independent phases");

    auto edited = pattern;
    edited.tracks[0].noteColumn.phase = 3u;
    Sequencer live;
    live.setPattern(pattern);
    live.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    live.start();
    (void)live.process(1u, events.data(), events.size());
    const auto renderedPosition = live.lastNotePosition(0u);
    live.replacePattern(std::move(edited));
    check(live.lastNotePosition(0u) == renderedPosition,
        "a live phase edit must not move the last-rendered position");
    const auto editedCount = live.process(6001u, events.data(), events.size());
    const auto onset = std::find_if(events.begin(),
        events.begin() + static_cast<std::ptrdiff_t>(editedCount),
        [](const ScheduledEvent& event) {
            return event.kind == ScheduledEventKind::NoteOn;
        });
    check(onset != events.begin()
            + static_cast<std::ptrdiff_t>(editedCount)
            && onset->note == 60u,
        "a live phase edit should rotate the retained column head by its delta");
}

void testProbabilityGatePreservesActiveLifecycle()
{
    Track track;
    track.destination = EventDestination::Internal;
    track.initialInstrumentNodeId = 1u;
    track.notes = {
        NoteCell::withNote(60u),
        NoteCell::retriggerPrevious(),
        NoteCell::kill(),
    };
    track.velocities = { ValueCell::withValue(0.6f) };
    track.noteColumn.length = track.notes.size();
    track.velocityColumn.length = 1u;
    auto& probability = track.fxPairs[0u];
    probability.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::Probability),
        FxActionCell::empty(),
    };
    probability.values = {
        FxValueCell::withValue(1.0f),
        FxValueCell::withValue(0.0f),
        FxValueCell::withValue(1.0f),
    };
    probability.actionColumn.length = probability.actions.size();
    probability.valueColumn.length = probability.values.size();
    auto& parameter = track.fxPairs[1u];
    parameter.actions = {
        FxActionCell::empty(),
        FxActionCell::parameter(7u, ParameterScope::Note),
        FxActionCell::empty(),
    };
    parameter.values = {
        FxValueCell::withValue(0.0f),
        FxValueCell::withValue(0.4f),
        FxValueCell::withValue(0.0f),
    };
    parameter.actionColumn.length = parameter.actions.size();
    parameter.valueColumn.length = parameter.values.size();

    Pattern pattern;
    pattern.tracks.push_back(std::move(track));
    Sequencer sequencer;
    sequencer.setPattern(std::move(pattern));
    sequencer.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    sequencer.start();
    std::array<ScheduledEvent, 8u> events {};
    const auto count = sequencer.process(12001u, events.data(), events.size());
    check(count == 3u,
        "a rejected retrigger must retain its active voice while parameter FX still execute");
    if (count == 3u) {
        check(events[0u].kind == ScheduledEventKind::NoteOn
                && events[1u].kind == ScheduledEventKind::Parameter
                && events[1u].noteId == events[0u].noteId
                && events[2u].kind == ScheduledEventKind::NoteOff
                && events[2u].noteId == events[0u].noteId,
            "PR rejection must allocate no identity, emit no release, and leave Kill addressing the original onset");
    }

    auto duplicate = makeTrack({ 64u });
    for (auto& pair : duplicate.fxPairs) {
        pair.actions = {
            FxActionCell::sequencer(SequencerAction::Probability),
        };
        pair.values = { FxValueCell::withValue(0.0f) };
        pair.actionColumn.length = 1u;
        pair.valueColumn.length = 1u;
    }
    duplicate.fxPairs[1u].values[0u] = FxValueCell::withValue(1.0f);
    Pattern duplicatePattern;
    duplicatePattern.tracks.push_back(duplicate);
    Sequencer duplicateSequencer;
    duplicateSequencer.setPattern(duplicatePattern);
    duplicateSequencer.start();
    std::array<ScheduledEvent, 2u> duplicateEvents {};
    check(duplicateSequencer.process(1u, duplicateEvents.data(),
              duplicateEvents.size()) == 1u,
        "FX2 PR=1 must override a duplicate FX1 PR=0");
    duplicate.fxPairs[0u].values[0u] = FxValueCell::withValue(1.0f);
    duplicate.fxPairs[1u].values[0u] = FxValueCell::withValue(0.0f);
    Pattern inversePattern;
    inversePattern.tracks.push_back(std::move(duplicate));
    duplicateSequencer.setPattern(std::move(inversePattern));
    duplicateSequencer.start();
    check(duplicateSequencer.process(1u, duplicateEvents.data(),
              duplicateEvents.size()) == 0u,
        "FX2 PR=0 must override a duplicate FX1 PR=1");
}

void testOffsetAndRepeatPreviousSourceTransforms()
{
    Track zeroOffsetBoundary;
    zeroOffsetBoundary.destination = EventDestination::Internal;
    zeroOffsetBoundary.notes = {
        NoteCell::withNote(60u), NoteCell::withNote(72u),
    };
    zeroOffsetBoundary.velocities = { ValueCell::withValue(1.0f) };
    zeroOffsetBoundary.noteColumn.length = zeroOffsetBoundary.notes.size();
    zeroOffsetBoundary.velocityColumn.length = 1u;
    auto& zeroOffsetFx = zeroOffsetBoundary.fxPairs[0u];
    zeroOffsetFx.actions.assign(2u,
        FxActionCell::sequencer(SequencerAction::Offset));
    zeroOffsetFx.values.assign(2u, FxValueCell::withValue(0.4375f));
    zeroOffsetFx.actionColumn.length = 2u;
    zeroOffsetFx.valueColumn.length = 2u;
    const auto zeroOffsetEvents = renderTicks(
        std::move(zeroOffsetBoundary), 1u);
    check(zeroOffsetEvents.size() == 1u
            && zeroOffsetEvents[0u].note == 60u,
        "OF must use Max/JavaScript half-up rounding at the negative half boundary");

    Track offset;
    offset.destination = EventDestination::Internal;
    offset.notes = {
        NoteCell::withNote(60u), NoteCell::rest(), NoteCell::withNote(72u),
    };
    offset.velocities = { ValueCell::withValue(0.75f) };
    offset.noteColumn.length = offset.notes.size();
    offset.velocityColumn.length = 1u;
    auto& offsetFx = offset.fxPairs[0u];
    offsetFx.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::Offset),
        FxActionCell::empty(),
    };
    offsetFx.values.assign(3u, FxValueCell::withValue(0.625f));
    offsetFx.actionColumn.length = 3u;
    offsetFx.valueColumn.length = 3u;
    auto& repeatFx = offset.fxPairs[1u];
    repeatFx.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::RepeatPrevious),
        FxActionCell::empty(),
    };
    repeatFx.values.assign(3u, FxValueCell::withValue(1.0f));
    repeatFx.actionColumn.length = 3u;
    repeatFx.valueColumn.length = 3u;
    const auto offsetEvents = renderTicks(std::move(offset), 2u);
    check(offsetEvents.size() == 2u
            && offsetEvents[0u].note == 60u
            && offsetEvents[1u].note == 72u,
        "OF must replace an empty source before RP considers filling it");

    Track offsetEuclid;
    offsetEuclid.destination = EventDestination::Internal;
    offsetEuclid.notes = {
        NoteCell::withNote(40u), NoteCell::withNote(41u),
        NoteCell::withNote(42u), NoteCell::withNote(43u),
    };
    offsetEuclid.velocities = { ValueCell::withValue(1.0f) };
    offsetEuclid.noteColumn.length = 4u;
    offsetEuclid.velocityColumn.length = 1u;
    auto& oeOffset = offsetEuclid.fxPairs[0u];
    oeOffset.actions.assign(4u,
        FxActionCell::sequencer(SequencerAction::Offset));
    oeOffset.values.assign(4u, FxValueCell::withValue(0.625f));
    oeOffset.actionColumn.length = 4u;
    oeOffset.valueColumn.length = 4u;
    auto& oeEuclid = offsetEuclid.fxPairs[1u];
    oeEuclid.actions.assign(4u,
        FxActionCell::sequencer(SequencerAction::Euclid));
    oeEuclid.values.assign(4u, FxValueCell::withValue(0.5f));
    oeEuclid.actionColumn.length = 4u;
    oeEuclid.valueColumn.length = 4u;
    const auto offsetEuclidean = renderTicks(std::move(offsetEuclid), 4u);
    check(offsetEuclidean.size() == 2u
            && offsetEuclidean[0u].note == 42u
            && offsetEuclidean[1u].note == 40u,
        "EU must evaluate the source row produced by OF, not the original moving cursor row");

    Track heldOffset;
    heldOffset.destination = EventDestination::Internal;
    heldOffset.notes = {
        NoteCell::withNote(65u), NoteCell::rest(),
        NoteCell::retriggerPrevious(),
    };
    heldOffset.velocities = { ValueCell::withValue(1.0f) };
    heldOffset.noteColumn.length = heldOffset.notes.size();
    heldOffset.velocityColumn.length = 1u;
    auto& heldFx = heldOffset.fxPairs[0u];
    heldFx.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::Offset),
        FxActionCell::empty(),
    };
    heldFx.values.assign(3u, FxValueCell::withValue(0.625f));
    heldFx.actionColumn.length = 3u;
    heldFx.valueColumn.length = 3u;
    const auto heldEvents = renderTicks(std::move(heldOffset), 2u);
    check(heldEvents.size() == 1u
            && heldEvents[0u].kind == ScheduledEventKind::NoteOn
            && heldEvents[0u].note == 65u,
        "OF must ignore a nearby Retrigger Previous because only an explicit NOTE cell is a valid offset source");

    Track repeat;
    repeat.destination = EventDestination::Internal;
    repeat.midiChannel = 3u;
    repeat.notes = {
        NoteCell::withNote(64u), NoteCell::rest(), NoteCell::kill(),
    };
    repeat.instruments = {
        InstrumentCell::withInstrument(1u),
        InstrumentCell::withInstrument(4u),
        InstrumentCell::withInstrument(4u),
    };
    repeat.velocities = {
        ValueCell::withValue(0.4f),
        ValueCell::withValue(0.9f),
        ValueCell::withValue(0.9f),
    };
    repeat.noteColumn.length = 3u;
    repeat.instrumentColumn.length = 3u;
    repeat.velocityColumn.length = 3u;
    auto& rp = repeat.fxPairs[0u];
    rp.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::RepeatPrevious),
        FxActionCell::empty(),
    };
    rp.values.assign(3u, FxValueCell::withValue(1.0f));
    rp.actionColumn.length = 3u;
    rp.valueColumn.length = 3u;
    const auto repeated = renderTicks(std::move(repeat), 3u);
    check(repeated.size() == 3u
            && repeated[1u].kind == ScheduledEventKind::NoteOn
            && repeated[1u].note == 64u
            && repeated[1u].targetNode == 1u
            && std::abs(repeated[1u].normalizedVelocity - 0.4f) < 0.00001f
            && repeated[2u].kind == ScheduledEventKind::NoteOff
            && repeated[2u].noteId == repeated[1u].noteId
            && repeated[2u].note == 64u
            && repeated[2u].targetNode == 1u
            && repeated[2u].channel == 3u,
        "RP must copy the last accepted note, volume, node, and channel, and Kill must release that exact onset");
}

void testSkipEuclidAndDeterministicProbability()
{
    Track skip;
    skip.destination = EventDestination::Internal;
    for (uint8_t note = 60u; note < 68u; ++note)
        skip.notes.push_back(NoteCell::withNote(note));
    skip.velocities = { ValueCell::withValue(1.0f) };
    skip.noteColumn.length = skip.notes.size();
    skip.velocityColumn.length = 1u;
    auto& sk = skip.fxPairs[0u];
    sk.actions.assign(skip.notes.size(),
        FxActionCell::sequencer(SequencerAction::Skip));
    sk.values.assign(skip.notes.size(), FxValueCell::withValue(0.0f));
    sk.actionColumn.length = sk.actions.size();
    sk.valueColumn.length = sk.values.size();
    const auto skipped = renderTicks(std::move(skip), 16u);
    check(skipped.size() == 8u,
        "SK cycle two must pass the first encounter and reject the second encounter of each source row");
    for (std::size_t index = 0u; index < skipped.size(); ++index)
        check(skipped[index].note == static_cast<uint8_t>(60u + index),
            "SK counters must be independent per resolved NOTE source row");

    Track euclid;
    euclid.destination = EventDestination::Internal;
    for (uint8_t note = 40u; note < 48u; ++note)
        euclid.notes.push_back(NoteCell::withNote(note));
    euclid.velocities = { ValueCell::withValue(1.0f) };
    euclid.noteColumn.length = euclid.notes.size();
    euclid.velocityColumn.length = 1u;
    auto& eu = euclid.fxPairs[0u];
    eu.actions.assign(euclid.notes.size(),
        FxActionCell::sequencer(SequencerAction::Euclid));
    eu.values.assign(euclid.notes.size(), FxValueCell::withValue(0.5f));
    eu.actionColumn.length = eu.actions.size();
    eu.valueColumn.length = eu.values.size();
    const auto euclidean = renderTicks(std::move(euclid), 8u);
    const std::array<uint8_t, 4u> expected { 40u, 42u, 44u, 46u };
    check(euclidean.size() == expected.size(),
        "EU density one-half over eight rows must emit four hits");
    for (std::size_t index = 0u;
         index < std::min(euclidean.size(), expected.size()); ++index)
        check(euclidean[index].note == expected[index],
            "EU must apply the v8 modular distribution to the resolved source row");

    Track probability = makeTrack({ 52u });
    probability.destination = EventDestination::Internal;
    auto& pr = probability.fxPairs[0u];
    pr.actions = {
        FxActionCell::sequencer(SequencerAction::Probability),
    };
    pr.values = { FxValueCell::withValue(0.5f) };
    pr.actionColumn.length = 1u;
    pr.valueColumn.length = 1u;
    Pattern probabilityPattern;
    probabilityPattern.tracks.push_back(std::move(probability));
    constexpr uint32_t totalFrames = 600001u;
    const TransportSettings transport { 48000.0, 120.0, 4u, 0.5 };
    const auto whole = renderBlocks(probabilityPattern, transport,
        { totalFrames }, 0x1234u);
    std::vector<uint32_t> partitions;
    uint32_t remaining = totalFrames;
    while (remaining > 0u) {
        const uint32_t block = std::min<uint32_t>(remaining, 257u);
        partitions.push_back(block);
        remaining -= block;
    }
    const auto partitioned = renderBlocks(std::move(probabilityPattern),
        transport, partitions, 0x1234u);
    check(whole.size() == partitioned.size(),
        "seeded PR must be independent of render block partitioning");
    for (std::size_t index = 0u;
         index < std::min(whole.size(), partitioned.size()); ++index) {
        check(whole[index].frame == partitioned[index].frame
                && whole[index].note == partitioned[index].note,
            "seeded PR decisions and event times must be block-partition invariant");
    }

    Track probabilisticRepeat;
    probabilisticRepeat.destination = EventDestination::Internal;
    probabilisticRepeat.notes = {
        NoteCell::withNote(57u), NoteCell::rest(),
    };
    probabilisticRepeat.velocities = { ValueCell::withValue(1.0f) };
    probabilisticRepeat.noteColumn.length = 2u;
    probabilisticRepeat.velocityColumn.length = 1u;
    auto& repeat = probabilisticRepeat.fxPairs[0u];
    repeat.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::RepeatPrevious),
    };
    repeat.values.assign(2u, FxValueCell::withValue(0.5f));
    repeat.actionColumn.length = 2u;
    repeat.valueColumn.length = 2u;
    Pattern repeatPattern;
    repeatPattern.tracks.push_back(std::move(probabilisticRepeat));
    const auto wholeRepeat = renderBlocks(repeatPattern, transport,
        { totalFrames }, 0x9876u);
    const auto partitionedRepeat = renderBlocks(std::move(repeatPattern),
        transport, partitions, 0x9876u);
    check(wholeRepeat.size() == partitionedRepeat.size(),
        "seeded RP must be independent of render block partitioning");
    for (std::size_t index = 0u;
         index < std::min(wholeRepeat.size(),
             partitionedRepeat.size()); ++index) {
        check(wholeRepeat[index].frame == partitionedRepeat[index].frame
                && wholeRepeat[index].note
                    == partitionedRepeat[index].note,
            "seeded RP decisions must retain exact event content and time across block partitions");
    }

    Track reseedRepeat;
    reseedRepeat.destination = EventDestination::Internal;
    reseedRepeat.notes = {
        NoteCell::withNote(58u), NoteCell::rest(),
    };
    reseedRepeat.velocities = { ValueCell::withValue(1.0f) };
    reseedRepeat.noteColumn.length = 2u;
    reseedRepeat.velocityColumn.length = 1u;
    auto& reseedRp = reseedRepeat.fxPairs[0u];
    reseedRp.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::RepeatPrevious),
    };
    reseedRp.values.assign(2u, FxValueCell::withValue(1.0f));
    reseedRp.actionColumn.length = 2u;
    reseedRp.valueColumn.length = 2u;
    Pattern reseedPattern;
    reseedPattern.tracks.push_back(std::move(reseedRepeat));
    Sequencer reseeded;
    reseeded.setPattern(std::move(reseedPattern));
    reseeded.setTransport(transport);
    reseeded.start();
    std::array<ScheduledEvent, 4u> reseedEvents {};
    check(reseeded.process(1u, reseedEvents.data(),
              reseedEvents.size()) == 1u,
        "reseed fixture must first establish an accepted onset");
    reseeded.stop();
    reseeded.setRandomSeed(77u);
    reseeded.start(false);
    check(reseeded.process(1u, reseedEvents.data(),
              reseedEvents.size()) == 0u,
        "reseed must clear RP last-emitted state as well as its random stream");

    Track reseedSkip = makeTrack({ 59u });
    reseedSkip.destination = EventDestination::Internal;
    auto& reseedSk = reseedSkip.fxPairs[0u];
    reseedSk.actions = {
        FxActionCell::sequencer(SequencerAction::Skip),
    };
    reseedSk.values = { FxValueCell::withValue(0.0f) };
    reseedSk.actionColumn.length = 1u;
    reseedSk.valueColumn.length = 1u;
    Pattern skipPattern;
    skipPattern.tracks.push_back(std::move(reseedSkip));
    Sequencer skipReseeded;
    skipReseeded.setPattern(std::move(skipPattern));
    skipReseeded.setTransport(transport);
    skipReseeded.start();
    check(skipReseeded.process(1u, reseedEvents.data(),
              reseedEvents.size()) == 1u
            && skipReseeded.process(6000u, reseedEvents.data(),
                reseedEvents.size()) == 0u,
        "SK reseed fixture must advance past its first-pass position");
    skipReseeded.stop();
    skipReseeded.setRandomSeed(88u);
    skipReseeded.start(false);
    check(skipReseeded.process(1u, reseedEvents.data(),
              reseedEvents.size()) == 1u,
        "reseed must reset every per-source-row SK cycle counter");
}

void testKillRuntimeMuteAndBoundaryControls()
{
    Track kill;
    kill.destination = EventDestination::Internal;
    kill.notes = { NoteCell::withNote(60u), NoteCell::kill() };
    kill.velocities = { ValueCell::withValue(1.0f) };
    kill.noteColumn.length = 2u;
    kill.velocityColumn.length = 1u;
    auto& of = kill.fxPairs[0u];
    of.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::Offset),
    };
    of.values.assign(2u, FxValueCell::withValue(0.375f));
    of.actionColumn.length = 2u;
    of.valueColumn.length = 2u;
    auto& rp = kill.fxPairs[1u];
    rp.actions = {
        FxActionCell::empty(),
        FxActionCell::sequencer(SequencerAction::RepeatPrevious),
    };
    rp.values.assign(2u, FxValueCell::withValue(1.0f));
    rp.actionColumn.length = 2u;
    rp.valueColumn.length = 2u;
    const auto killed = renderTicks(std::move(kill), 2u);
    check(killed.size() == 2u
            && killed[0u].kind == ScheduledEventKind::NoteOn
            && killed[1u].kind == ScheduledEventKind::NoteOff,
        "OF and RP must never resurrect an authored Kill cell");

    auto runtimeTrack = makeTrack({ 67u });
    runtimeTrack.destination = EventDestination::Internal;
    runtimeTrack.initialInstrumentNodeId = 1u;
    runtimeTrack.midiChannel = 2u;
    runtimeTrack.fxPairs[0u].actions = {
        FxActionCell::sequencer(SequencerAction::Offset),
    };
    runtimeTrack.fxPairs[0u].values = {
        FxValueCell::withValue(1.0f),
    };
    runtimeTrack.fxPairs[0u].actionColumn.length = 1u;
    runtimeTrack.fxPairs[0u].valueColumn.length = 1u;
    runtimeTrack.fxPairs[1u].actions = {
        FxActionCell::sequencer(SequencerAction::RepeatPrevious),
    };
    runtimeTrack.fxPairs[1u].values = {
        FxValueCell::withValue(1.0f),
    };
    runtimeTrack.fxPairs[1u].actionColumn.length = 1u;
    runtimeTrack.fxPairs[1u].valueColumn.length = 1u;
    Pattern runtimePattern;
    runtimePattern.tracks.push_back(runtimeTrack);
    Sequencer runtime;
    runtime.setPattern(runtimePattern);
    runtime.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    runtime.start();
    std::array<ScheduledEvent, 4u> events {};
    check(runtime.process(1u, events.data(), events.size()) == 1u,
        "runtime-mute fixture must begin with an active note");
    runtimeTrack.midiChannel = 9u;
    Pattern edited;
    edited.tracks.push_back(runtimeTrack);
    runtime.replacePattern(std::move(edited));
    runtime.setRuntimeTrackMuteMask(1u);
    const auto mutedCount = runtime.process(6000u, events.data(),
        events.size());
    check(mutedCount == 1u
            && events[0u].kind == ScheduledEventKind::NoteOff
            && events[0u].note == 67u
            && events[0u].channel == 2u
            && events[0u].targetNode == 1u,
        "runtime Song mute must hard-release with the onset pitch, channel, and node after live edits");
    runtime.setRuntimeTrackMuteMask(0u);
    const auto unmutedCount = runtime.process(6000u, events.data(),
        events.size());
    check(unmutedCount == 1u
            && events[0u].kind == ScheduledEventKind::NoteOn
            && events[0u].channel == 9u,
        "clearing the runtime mask must resume authored NOTE playback at its retained phase");
    runtime.setRuntimeTrackMuteMask(1u);
    runtime.reset();
    runtime.setPattern(runtimePattern);
    check(runtime.runtimeTrackMuteMask() == 1u,
        "reset and pattern publication must retain the explicitly owned runtime mute overlay");

    auto phasedTrack = makeTrack({ 70u, 71u, 72u });
    phasedTrack.noteColumn.phase = 1u;
    Pattern phasedPattern;
    phasedPattern.tracks.push_back(std::move(phasedTrack));
    Sequencer boundary;
    boundary.setPattern(std::move(phasedPattern));
    boundary.setTransport({ 48000.0, 120.0, 4u, 0.5 });
    boundary.start();
    check(boundary.processSingleTick(1u, events.data(), events.size()) == 1u
            && events[0u].note == 71u,
        "boundary fixture must emit its authored phase");
    const auto tick = boundary.tickIndex();
    const auto row = boundary.transportRow();
    TransportSettings faster { 48000.0, 240.0, 4u, 0.5 };
    boundary.setTransportAtTickBoundary(faster);
    check(boundary.nextTickSampleFrame() == 3000u
            && boundary.tickIndex() == tick
            && boundary.transportRow() == row,
        "tick-boundary tempo change must retime only the completed-to-next interval");
    const auto lastRenderedBeforeLaunch = boundary.lastNotePosition(0u);
    boundary.relaunchColumnsAtTickBoundary(1u);
    check(boundary.notePosition(0u) == 2u
            && boundary.lastNotePosition(0u) == lastRenderedBeforeLaunch
            && boundary.tickIndex() == tick
            && boundary.transportRow() == row,
        "Song relaunch must move only the next authored column head without falsifying the last-rendered cursor or transport clock");
    check(boundary.processSingleTick(3001u, events.data(), events.size())
                == 1u
            && events[0u].note == 72u,
        "the first tick after Song relaunch must read launch row plus authored phase");
    boundary.stop();
    const auto tailClock = boundary.renderedFrameCount();
    const auto tailTick = boundary.tickIndex();
    const auto tailRow = boundary.transportRow();
    const auto tailNextTick = boundary.nextTickSampleFrame();
    boundary.advanceRenderClockWithoutTickGeneration(257u);
    check(boundary.renderedFrameCount() == tailClock + 257u
            && boundary.tickIndex() == tailTick
            && boundary.transportRow() == tailRow
            && boundary.nextTickSampleFrame() == tailNextTick,
        "tail-drain clock advance must not generate or retime any musical tick");
}

} // namespace

int main()
{
    testPolymetricColumns();
    testTrackVelocityScale();
    testInstrumentColumnPolymeterAndMemory();
    testInstrumentFxScopeAndReleaseRouting();
    testTypedFxPairs();
    testFxMemoryAndMutedPhase();
    testStrideAndDirections();
    testSampleOffsetsAndSwing();
    testFunctionalTimingWarp();
    testTimingIsIndependentOfBlockPartition();
    testCanonicalScheduledEventContract();
    testInstrumentCellValidation();
    testRetriggerRestAndKillSemantics();
    testBoundedOutput();
    testDisabledRoutesDoNotConsumeEventCapacity();
    testQuantizedWarpCollisionPolicy();
    testSingleTickSteppingPreservesWarpCollisions();
    testExtremeContinuousWarpCollisionPolicy();
    testInitialFxCollisionBudget();
    testNullOutputStillAdvancesPlayback();
    testRandomPlaybackRestartsDeterministically();
    testRandomColumnsHaveIndependentStreams();
    testLivePatternReplacementRetainsPhase();
    testSongBoundaryPatternReplacementRetainsClockAndOwnership();
    testSongBoundaryRevisitResetsNonOverlappingTracks();
    testLiveInstrumentReassignmentRetainsReleaseTarget();
    testReadableMidiNoteParsing();
    testLastNoteTriggeredTracksActualOnsets();
    testIndependentColumnPhase();
    testProbabilityGatePreservesActiveLifecycle();
    testOffsetAndRepeatPreviousSourceTransforms();
    testSkipEuclidAndDeterministicProbability();
    testKillRuntimeMuteAndBoundaryControls();
    testGlobalLoopRowsAndPauseResume();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "s3g Tracker core tests passed\n";
    return EXIT_SUCCESS;
}
