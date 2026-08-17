#include "s3g/tracker/midi_step_recorder.h"

#include <cmath>
#include <iostream>

namespace {

using namespace s3g::tracker;

int failures = 0;

void check(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

TrackerSession sessionWithTrack()
{
    TrackerSession session;
    session.pattern.name = "steps";
    session.pattern.visibleRows = 8u;
    session.pattern.tracks.resize(1u);
    session.pattern.tracks[0u].notes.resize(8u, NoteCell::rest());
    session.pattern.tracks[0u].velocities.resize(
        8u, ValueCell::defaultValue());
    session.pattern.tracks[0u].noteColumn.length = 8u;
    session.pattern.tracks[0u].velocityColumn.length = 8u;
    session.selectedRow = 2u;
    return session;
}

} // namespace

int main()
{
    auto stepped = sessionWithTrack();
    stepped.pattern.tracks[0u].fxPairs[0u].actions.resize(
        8u, FxActionCell::empty());
    stepped.pattern.tracks[0u].fxPairs[0u].values.resize(
        8u, FxValueCell::previous());
    stepped.pattern.tracks[0u].fxPairs[0u].actions[2u] =
        FxActionCell::sequencer(SequencerAction::MicroTime);
    stepped.pattern.tracks[0u].fxPairs[0u].values[2u] =
        FxValueCell::withValue(0.9f);
    MidiStepCapture input;
    input.note = 64u;
    input.velocity = 96u;
    input.channel = 7u;
    const auto step = recordMidiStep(stepped,
        MidiStepRecordMode::Step, input, 48000.0);
    const auto& stepTrack = stepped.pattern.tracks[0u];
    check(step.recorded() && step.row == 2u
            && stepTrack.notes[2u].state == NoteCellState::Note
            && stepTrack.notes[2u].note == 64u
            && std::abs(stepTrack.velocities[2u].normalized
                - 96.0f / 127.0f) < 1.0e-6f
            && stepTrack.fxPairs[0u].actions[2u].state
                == FxActionCellState::Empty
            && stepped.selectedRow == 3u,
        "STEP should write at the cursor, clear MT, and advance once");

    auto liveQuantized = sessionWithTrack();
    liveQuantized.pattern.tracks[0u].fxPairs[0u].actions.resize(
        8u, FxActionCell::empty());
    liveQuantized.pattern.tracks[0u].fxPairs[0u].values.resize(
        8u, FxValueCell::previous());
    liveQuantized.pattern.tracks[0u].fxPairs[0u].actions[5u] =
        FxActionCell::sequencer(SequencerAction::MicroTime);
    liveQuantized.pattern.tracks[0u].fxPairs[0u].values[5u] =
        FxValueCell::withValue(0.1f);
    MidiStepCapture liveInput = input;
    liveInput.rowKnown = true;
    liveInput.row = 5u;
    const auto liveGrid = recordMidiStep(liveQuantized,
        MidiStepRecordMode::LiveQuantized, liveInput, 48000.0);
    check(liveGrid.recorded() && liveGrid.row == 5u
            && liveQuantized.pattern.tracks[0u].notes[5u].note == 64u
            && liveQuantized.pattern.tracks[0u].fxPairs[0u]
                .actions[5u].state == FxActionCellState::Empty
            && liveQuantized.selectedRow == 5u,
        "LIVE Q should use the host row and move the cursor to its note");

    auto shortLane = sessionWithTrack();
    auto& shortTrack = shortLane.pattern.tracks[0u];
    shortTrack.noteColumn.length = 4u;
    shortTrack.velocityColumn.length = 3u;
    MidiStepCapture wrappedInput = input;
    wrappedInput.rowKnown = true;
    wrappedInput.row = 7u;
    const auto wrapped = recordMidiStep(shortLane,
        MidiStepRecordMode::LiveQuantized, wrappedInput, 48000.0);
    check(wrapped.recorded() && wrapped.row == 3u
            && shortTrack.notes[3u].state == NoteCellState::Note
            && shortTrack.notes[3u].note == 64u
            && shortTrack.velocities[1u].state == ValueCellState::Value
            && std::abs(shortTrack.velocities[1u].normalized
                - 96.0f / 127.0f) < 1.0e-6f
            && shortTrack.noteColumn.length == 4u
            && shortTrack.velocityColumn.length == 3u
            && shortLane.selectedRow == 3u,
        "LIVE Q should wrap NOTE/VOL independently without growing lanes");

    auto micro = sessionWithTrack();
    MidiStepCapture late = input;
    late.rowKnown = true;
    late.row = 4u;
    late.timingKnown = true;
    late.offsetSamples = 600; // +12.5 ms at 48 kHz = MT 0.75.
    const auto timed = recordMidiStep(micro,
        MidiStepRecordMode::LiveUnquantized, late, 48000.0);
    const auto& microPair = micro.pattern.tracks[0u].fxPairs[0u];
    check(timed.recorded() && timed.row == 4u && timed.fxPair == 0u
            && std::abs(timed.microTime - 0.75f) < 1.0e-6f
            && microPair.actions[4u].sequencerAction
                == SequencerAction::MicroTime
            && std::abs(microPair.values[4u].normalized - 0.75f)
                < 1.0e-6f
            && micro.selectedRow == 4u,
        "LIVE MT should convert timing and highlight the written note");

    auto early = sessionWithTrack();
    MidiStepCapture earlyInput = input;
    earlyInput.rowKnown = true;
    earlyInput.row = 1u;
    earlyInput.timingKnown = true;
    earlyInput.offsetSamples = -1200; // -25 ms = MT 0.0.
    const auto earlyResult = recordMidiStep(early,
        MidiStepRecordMode::LiveUnquantized, earlyInput, 48000.0);
    check(earlyResult.recorded()
            && std::abs(earlyResult.microTime) < 1.0e-6f
            && !earlyResult.timingClamped,
        "negative timing should map to the early half of MT");

    auto full = sessionWithTrack();
    for (auto& pair : full.pattern.tracks[0u].fxPairs) {
        pair.actions.resize(8u, FxActionCell::empty());
        pair.values.resize(8u, FxValueCell::previous());
        pair.actions[2u] = FxActionCell::sequencer(
            SequencerAction::Probability);
        pair.values[2u] = FxValueCell::withValue(0.5f);
    }
    const auto before = full.pattern.tracks[0u].notes[2u];
    late.row = 2u;
    const auto blocked = recordMidiStep(full,
        MidiStepRecordMode::LiveUnquantized, late, 48000.0);
    check(blocked.code == MidiStepRecordCode::FxUnavailable
            && full.pattern.tracks[0u].notes[2u].state == before.state
            && full.selectedRow == 2u,
        "a full SEQ row should reject LIVE MT transactionally");

    auto stopped = sessionWithTrack();
    const auto unavailable = recordMidiStep(stopped,
        MidiStepRecordMode::LiveQuantized, input, 48000.0);
    check(unavailable.code == MidiStepRecordCode::TimingUnavailable
            && stopped.pattern.tracks[0u].notes[2u].state
                == NoteCellState::Rest,
        "live recording should reject input until the host row is known");

    MidiStepCaptureQueue queue;
    for (uint32_t index = 0u; index < MidiStepCaptureQueue::kCapacity;
         ++index) {
        MidiStepCapture event = input;
        event.note = static_cast<uint8_t>(index % 128u);
        check(queue.push(event), "queue should accept its fixed capacity");
    }
    check(!queue.push(input) && queue.droppedCount() == 1u,
        "queue should count overflow without blocking");
    for (uint32_t index = 0u; index < MidiStepCaptureQueue::kCapacity;
         ++index) {
        MidiStepCapture event;
        check(queue.pop(event) && event.note == index % 128u,
            "queue should preserve audio-thread publication order");
    }
    MidiStepCapture empty;
    check(!queue.pop(empty), "queue should report empty after draining");

    return failures == 0 ? 0 : 1;
}
