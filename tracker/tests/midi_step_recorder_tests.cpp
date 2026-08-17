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
    auto quantized = sessionWithTrack();
    quantized.pattern.tracks[0u].fxPairs[0u].actions.resize(
        8u, FxActionCell::empty());
    quantized.pattern.tracks[0u].fxPairs[0u].values.resize(
        8u, FxValueCell::previous());
    quantized.pattern.tracks[0u].fxPairs[0u].actions[2u] =
        FxActionCell::sequencer(SequencerAction::MicroTime);
    quantized.pattern.tracks[0u].fxPairs[0u].values[2u] =
        FxValueCell::withValue(0.9f);
    MidiStepCapture input;
    input.note = 64u;
    input.velocity = 96u;
    input.channel = 7u;
    const auto grid = recordMidiStep(quantized,
        MidiStepRecordMode::Quantized, input, 48000.0);
    const auto& gridTrack = quantized.pattern.tracks[0u];
    check(grid.recorded() && grid.row == 2u
            && gridTrack.notes[2u].state == NoteCellState::Note
            && gridTrack.notes[2u].note == 64u
            && std::abs(gridTrack.velocities[2u].normalized
                - 96.0f / 127.0f) < 1.0e-6f
            && gridTrack.fxPairs[0u].actions[2u].state
                == FxActionCellState::Empty
            && quantized.selectedRow == 3u,
        "quantized recording should write note/velocity, clear MT, and advance");

    auto micro = sessionWithTrack();
    MidiStepCapture late = input;
    late.timingKnown = true;
    late.offsetSamples = 600; // +12.5 ms at 48 kHz = MT 0.75.
    const auto timed = recordMidiStep(micro,
        MidiStepRecordMode::Unquantized, late, 48000.0);
    const auto& microPair = micro.pattern.tracks[0u].fxPairs[0u];
    check(timed.recorded() && timed.fxPair == 0u
            && std::abs(timed.microTime - 0.75f) < 1.0e-6f
            && microPair.actions[2u].sequencerAction
                == SequencerAction::MicroTime
            && std::abs(microPair.values[2u].normalized - 0.75f)
                < 1.0e-6f,
        "unquantized recording should convert signed timing into MT");

    auto early = sessionWithTrack();
    MidiStepCapture earlyInput = input;
    earlyInput.timingKnown = true;
    earlyInput.offsetSamples = -1200; // -25 ms = MT 0.0.
    const auto earlyResult = recordMidiStep(early,
        MidiStepRecordMode::Unquantized, earlyInput, 48000.0);
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
    const auto blocked = recordMidiStep(full,
        MidiStepRecordMode::Unquantized, late, 48000.0);
    check(blocked.code == MidiStepRecordCode::FxUnavailable
            && full.pattern.tracks[0u].notes[2u].state == before.state
            && full.selectedRow == 2u,
        "a full SEQ row should reject MICRO recording transactionally");

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
