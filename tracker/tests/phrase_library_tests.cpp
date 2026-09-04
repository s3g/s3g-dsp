#include "s3g/tracker/phrase_library.h"

#include <cassert>

using namespace s3g::tracker;

int main()
{
    Track track;
    track.notes.resize(70u, NoteCell::rest());
    track.velocities.resize(70u, ValueCell::defaultValue());
    track.gates.resize(70u, GateCell::defaultValue());
    track.notes[3u] = NoteCell::withNote(48u);
    track.notes[5u] = NoteCell::withNote(51u);
    track.midiChannel = 11u;
    track.velocities[3u] = ValueCell::withValue(0.75f);
    track.gates[5u] = GateCell::withRows(0.5f);
    track.fxPairs[0u].actions.resize(70u, FxActionCell::empty());
    track.fxPairs[0u].values.resize(70u, FxValueCell::previous());
    track.fxPairs[0u].actions[5u] = FxActionCell::sequencer(
        SequencerAction::MicroTime);
    track.fxPairs[0u].values[5u] = FxValueCell::withValue(0.625f);

    PhraseDefinition odd = makeBlankPhrase(5u);
    odd.name = "Odd five";
    assert(capturePhrase(track, 3u, 7u, odd));
    assert(odd.length == 5u);
    assert(odd.previewMidiChannel == 11u);
    assert(odd.notes[0u].note == 48u);
    assert(odd.notes[2u].note == 51u);
    assert(odd.fxPairs[0u].actions[2u].sequencerAction
        == SequencerAction::MicroTime);
    assert(odd.gates[2u].gateVoice(0u).rows == 0.5f);

    Track destination;
    destination.midiChannel = 4u;
    assert(placePhrase(destination, odd, 9u));
    assert(destination.midiChannel == 4u);
    assert(destination.notes[9u].note == 48u);
    assert(destination.notes[11u].note == 51u);
    assert(destination.noteColumn.length == 14u);
    assert(destination.fxPairs[0u].values[11u].normalized == 0.625f);
    assert(destination.gates[11u].gateVoice(0u).rows == 0.5f);

    auto allLengths = makeBlankPhrase(2u);
    for (std::size_t length = 2u; length <= 64u; ++length) {
        allLengths = makeBlankPhrase(length);
        assert(allLengths.length == length);
        assert(allLengths.notes.size() == length);
    }
    assert(makeBlankPhrase(1u).length == 2u);
    assert(makeBlankPhrase(65u).length == 64u);

    PhraseDefinition tooLong;
    assert(!capturePhrase(track, 0u, 64u, tooLong));
    assert(!placePhrase(destination, odd, 252u));

    Track merge;
    merge.notes.resize(8u, NoteCell::rest());
    merge.notes[1u] = NoteCell::withNote(99u);
    assert(placePhrase(merge, odd, 1u, PhrasePlacementMode::MergeIntoEmpty));
    assert(merge.notes[1u].note == 99u);
    assert(merge.notes[3u].note == 51u);

    Pattern burstSource;
    burstSource.tracks.push_back(track);
    burstSource.tracks[0u].notes[3u] = NoteCell::withBurst(2u);
    PhraseDefinition burstPhrase = makeBlankPhrase(4u);
    assert(capturePhrase(burstSource, 0u, 3u, 6u, burstPhrase));
    assert(burstPhrase.notes[0u].state == NoteCellState::Burst);

    Pattern burstDestination;
    burstDestination.tracks.emplace_back();
    assert(placePhrase(burstDestination, 0u, burstPhrase, 8u));
    assert(burstDestination.tracks[0u].notes[8u].note == 2u);
    return 0;
}
