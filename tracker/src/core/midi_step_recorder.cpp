#include "s3g/tracker/midi_step_recorder.h"

#include <algorithm>
#include <cmath>

namespace s3g::tracker {
namespace {

bool isMicroTime(const FxActionCell& cell) noexcept
{
    return cell.state == FxActionCellState::Sequencer
        && cell.sequencerAction == SequencerAction::MicroTime;
}

std::size_t microTimePairForRow(const Track& track, std::size_t row) noexcept
{
    for (std::size_t pair = 0u; pair < kFxPairCount; ++pair) {
        const auto& actions = track.fxPairs[pair].actions;
        if (row < actions.size() && isMicroTime(actions[row])) return pair;
    }
    for (std::size_t pair = 0u; pair < kFxPairCount; ++pair) {
        const auto& actions = track.fxPairs[pair].actions;
        if (row >= actions.size()
            || actions[row].state == FxActionCellState::Empty) return pair;
    }
    return kFxPairCount;
}

void clearMicroTime(Track& track, std::size_t row) noexcept
{
    for (auto& pair : track.fxPairs) {
        if (row >= pair.actions.size() || !isMicroTime(pair.actions[row]))
            continue;
        pair.actions[row] = FxActionCell::empty();
        if (row < pair.values.size())
            pair.values[row] = FxValueCell::previous();
    }
}

} // namespace

bool MidiStepCaptureQueue::push(const MidiStepCapture& event) noexcept
{
    const uint32_t write = write_.load(std::memory_order_relaxed);
    const uint32_t read = read_.load(std::memory_order_acquire);
    if (write - read >= kCapacity) {
        dropped_.fetch_add(1u, std::memory_order_relaxed);
        return false;
    }
    events_[write % kCapacity] = event;
    write_.store(write + 1u, std::memory_order_release);
    return true;
}

bool MidiStepCaptureQueue::pop(MidiStepCapture& event) noexcept
{
    const uint32_t read = read_.load(std::memory_order_relaxed);
    const uint32_t write = write_.load(std::memory_order_acquire);
    if (read == write) return false;
    event = events_[read % kCapacity];
    read_.store(read + 1u, std::memory_order_release);
    return true;
}

MidiStepRecordResult recordMidiStep(TrackerSession& session,
    MidiStepRecordMode mode, const MidiStepCapture& capture,
    double sampleRate)
{
    MidiStepRecordResult result;
    result.timingKnown = capture.timingKnown;
    if (mode == MidiStepRecordMode::Off) return result;
    if (capture.note > 127u || capture.velocity == 0u
        || capture.velocity > 127u) {
        result.code = MidiStepRecordCode::InvalidEvent;
        return result;
    }
    if (session.pattern.tracks.empty()) {
        result.code = MidiStepRecordCode::NoTrack;
        return result;
    }

    const std::size_t trackIndex = std::min(session.selectedTrack,
        session.pattern.tracks.size() - 1u);
    const std::size_t visibleRows = std::max<std::size_t>(
        session.pattern.visibleRows, 1u);
    const std::size_t row = std::min(session.selectedRow, visibleRows - 1u);
    result.track = trackIndex;
    result.row = row;
    auto& track = session.pattern.tracks[trackIndex];

    std::size_t pairIndex = kFxPairCount;
    float microTime = 0.5f;
    if (mode == MidiStepRecordMode::Unquantized) {
        const double range = session.transport.microTimingRangeMilliseconds;
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0
            || !std::isfinite(range) || range <= 0.0) {
            result.code = MidiStepRecordCode::TimingUnavailable;
            return result;
        }
        pairIndex = microTimePairForRow(track, row);
        if (pairIndex == kFxPairCount) {
            result.code = MidiStepRecordCode::FxUnavailable;
            return result;
        }
        const double offsetMilliseconds = capture.timingKnown
            ? static_cast<double>(capture.offsetSamples) * 1000.0 / sampleRate
            : 0.0;
        const double raw = 0.5 + offsetMilliseconds / (2.0 * range);
        result.timingClamped = raw < 0.0 || raw > 1.0;
        microTime = static_cast<float>(std::clamp(raw, 0.0, 1.0));
    }

    if (track.notes.size() <= row)
        track.notes.resize(row + 1u, NoteCell::rest());
    if (track.velocities.size() <= row)
        track.velocities.resize(row + 1u, ValueCell::defaultValue());
    track.notes[row] = NoteCell::withNote(capture.note);
    track.velocities[row] = ValueCell::withValue(
        static_cast<float>(capture.velocity) / 127.0f);
    track.noteColumn.length = std::max(track.noteColumn.length, row + 1u);
    track.velocityColumn.length = std::max(
        track.velocityColumn.length, row + 1u);

    if (mode == MidiStepRecordMode::Quantized) {
        clearMicroTime(track, row);
    } else {
        auto& pair = track.fxPairs[pairIndex];
        if (pair.actions.size() <= row)
            pair.actions.resize(row + 1u, FxActionCell::empty());
        if (pair.values.size() <= row)
            pair.values.resize(row + 1u, FxValueCell::previous());
        pair.actions[row] = FxActionCell::sequencer(
            SequencerAction::MicroTime);
        pair.values[row] = FxValueCell::withValue(microTime);
        pair.actionColumn.length = std::max(
            pair.actionColumn.length, row + 1u);
        pair.valueColumn.length = std::max(
            pair.valueColumn.length, row + 1u);
    }

    session.pattern.visibleRows = std::max(
        session.pattern.visibleRows, row + 1u);
    session.selectedRow = (row + 1u) % std::max<std::size_t>(
        session.pattern.visibleRows, 1u);
    session.selectedField = 0u;
    result.code = MidiStepRecordCode::Recorded;
    result.fxPair = pairIndex;
    result.microTime = microTime;
    return result;
}

} // namespace s3g::tracker
