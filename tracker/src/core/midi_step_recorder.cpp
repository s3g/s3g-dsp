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

std::size_t recordedVoice(const MidiLiveRecordState& state,
    uint8_t note, uint8_t channel) noexcept
{
    for (std::size_t voice = 0u; voice < state.voiceCount; ++voice) {
        if (state.notes[voice] == note && state.channels[voice] == channel)
            return voice;
    }
    return kMaximumNoteVoices;
}

bool addRecordedVoice(MidiLiveRecordState& state, uint8_t note,
    uint8_t velocity, uint8_t channel) noexcept
{
    const auto existing = recordedVoice(state, note, channel);
    if (existing < state.voiceCount) {
        state.velocities[existing] = velocity;
        state.held[existing] = true;
        return true;
    }
    if (state.voiceCount >= kMaximumNoteVoices) return false;
    std::size_t insertion = 0u;
    while (insertion < state.voiceCount
        && state.notes[insertion] < note) ++insertion;
    for (std::size_t voice = state.voiceCount; voice > insertion; --voice) {
        state.notes[voice] = state.notes[voice - 1u];
        state.velocities[voice] = state.velocities[voice - 1u];
        state.channels[voice] = state.channels[voice - 1u];
        state.held[voice] = state.held[voice - 1u];
    }
    state.notes[insertion] = note;
    state.velocities[insertion] = velocity;
    state.channels[insertion] = channel;
    state.held[insertion] = true;
    ++state.voiceCount;
    state.note = state.notes[0u];
    state.channel = state.channels[0u];
    return true;
}

bool releaseRecordedVoice(MidiLiveRecordState& state, uint8_t note,
    uint8_t channel) noexcept
{
    const auto voice = recordedVoice(state, note, channel);
    if (voice >= state.voiceCount || !state.held[voice]) return false;
    state.held[voice] = false;
    return true;
}

bool anyRecordedVoiceHeld(const MidiLiveRecordState& state) noexcept
{
    for (std::size_t voice = 0u; voice < state.voiceCount; ++voice) {
        if (state.held[voice]) return true;
    }
    return false;
}

void writeRecordedChord(Track& track, std::size_t noteRow,
    std::size_t velocityRow, const MidiLiveRecordState& state)
{
    std::array<uint8_t, kMaximumNoteVoices> notes {};
    std::array<float, kMaximumNoteVoices> velocities {};
    for (std::size_t voice = 0u; voice < state.voiceCount; ++voice) {
        notes[voice] = state.notes[voice];
        velocities[voice] = static_cast<float>(state.velocities[voice])
            / 127.0f;
    }
    track.notes[noteRow] = NoteCell::withNotes(notes, state.voiceCount);
    track.velocities[velocityRow] = ValueCell::withValues(
        velocities, state.voiceCount);
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
    double sampleRate, MidiLiveRecordState* liveState,
    std::size_t stepAdvance)
{
    MidiStepRecordResult result;
    result.timingKnown = capture.timingKnown;
    if (mode == MidiStepRecordMode::Off) return result;
    const bool live = mode == MidiStepRecordMode::LiveQuantized
        || mode == MidiStepRecordMode::LiveUnquantized;
    if (capture.note > 127u || capture.channel == 0u
        || capture.channel > 16u
        || (capture.noteOn
            && (capture.velocity == 0u || capture.velocity > 127u))
        || (!capture.noteOn && !live && !liveState)) {
        result.code = MidiStepRecordCode::InvalidEvent;
        return result;
    }
    if (session.pattern.tracks.empty()) {
        result.code = MidiStepRecordCode::NoTrack;
        return result;
    }

    if (!capture.noteOn
        && (!liveState || !liveState->active
            || recordedVoice(*liveState, capture.note, capture.channel)
                >= liveState->voiceCount
            || liveState->track >= session.pattern.tracks.size())) {
        result.code = MidiStepRecordCode::InvalidEvent;
        return result;
    }
    const std::size_t trackIndex = capture.noteOn
        ? std::min(capture.targetTrack, session.pattern.tracks.size() - 1u)
        : liveState->track;
    const std::size_t visibleRows = std::max<std::size_t>(
        session.pattern.visibleRows, 1u);

    if (mode == MidiStepRecordMode::Step && liveState) {
        if (!capture.noteOn) {
            if (!releaseRecordedVoice(*liveState,
                    capture.note, capture.channel)) {
                result.code = MidiStepRecordCode::InvalidEvent;
                return result;
            }
            result.track = liveState->track;
            result.row = liveState->onsetRow;
            result.release = true;
            if (!anyRecordedVoiceHeld(*liveState)) {
                session.selectedRow = (liveState->onsetRow
                    + std::max<std::size_t>(stepAdvance, 1u)) % visibleRows;
                liveState->clear();
            }
            result.code = MidiStepRecordCode::Recorded;
            return result;
        }
        if (!liveState->active) {
            liveState->active = true;
            liveState->track = trackIndex;
            liveState->onsetRow = std::min(session.selectedRow,
                visibleRows - 1u);
        }
        if (liveState->track != trackIndex
            || !addRecordedVoice(*liveState, capture.note,
                capture.velocity, capture.channel)) {
            result.code = MidiStepRecordCode::InvalidEvent;
            return result;
        }
        auto& stepTrack = session.pattern.tracks[liveState->track];
        const auto stepRow = liveState->onsetRow;
        if (stepTrack.notes.size() <= stepRow)
            stepTrack.notes.resize(stepRow + 1u, NoteCell::rest());
        if (stepTrack.velocities.size() <= stepRow)
            stepTrack.velocities.resize(stepRow + 1u,
                ValueCell::defaultValue());
        writeRecordedChord(stepTrack, stepRow, stepRow, *liveState);
        clearMicroTime(stepTrack, stepRow);
        stepTrack.noteColumn.length = std::max(
            stepTrack.noteColumn.length, stepRow + 1u);
        stepTrack.velocityColumn.length = std::max(
            stepTrack.velocityColumn.length, stepRow + 1u);
        result.track = liveState->track;
        result.row = stepRow;
        result.code = MidiStepRecordCode::Recorded;
        return result;
    }
    if (live && !capture.rowKnown) {
        result.code = MidiStepRecordCode::TimingUnavailable;
        return result;
    }
    auto& track = session.pattern.tracks[trackIndex];
    const std::size_t noteLength = live
        ? std::max<std::size_t>(track.noteColumn.length, 1u)
        : visibleRows;
    std::size_t row = live
        ? capture.row % noteLength
        : std::min(session.selectedRow, visibleRows - 1u);
    const bool joiningChord = live && capture.noteOn && liveState
        && liveState->active && liveState->track == trackIndex
        && liveState->onsetRow % noteLength == row;
    const std::size_t velocityLength = live
        ? std::max<std::size_t>(track.velocityColumn.length, 1u)
        : visibleRows;
    const bool release = !capture.noteOn;
    const std::size_t onsetRow = release
        ? liveState->onsetRow % noteLength : row;
    int64_t timingOffsetSamples = capture.offsetSamples;
    if (release && row == onsetRow && capture.followingRowKnown) {
        row = capture.followingRow % noteLength;
        timingOffsetSamples = capture.followingOffsetSamples;
    }
    const std::size_t velocityRow = live
        ? capture.row % velocityLength : row;
    result.track = trackIndex;
    result.row = row;
    const std::size_t releaseDistance = release
        ? (row + noteLength - onsetRow) % noteLength : 0u;
    const bool writesReleaseCell = release && releaseDistance != 0u;
    if (release) {
        (void)releaseRecordedVoice(*liveState,
            capture.note, capture.channel);
        if (anyRecordedVoiceHeld(*liveState)) {
            result.row = onsetRow;
            result.release = true;
            result.code = MidiStepRecordCode::Recorded;
            return result;
        }
    }

    std::size_t pairIndex = kFxPairCount;
    float microTime = 0.5f;
    if (mode == MidiStepRecordMode::LiveUnquantized && !joiningChord
        && (!release || writesReleaseCell)) {
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
            ? static_cast<double>(timingOffsetSamples) * 1000.0 / sampleRate
            : 0.0;
        const double raw = 0.5 + offsetMilliseconds / (2.0 * range);
        result.timingClamped = raw < 0.0 || raw > 1.0;
        microTime = static_cast<float>(std::clamp(raw, 0.0, 1.0));
    }

    const std::size_t requiredNotes = live ? noteLength : row + 1u;
    const std::size_t requiredVelocities = live
        ? velocityLength : velocityRow + 1u;
    if (track.notes.size() < requiredNotes)
        track.notes.resize(requiredNotes, NoteCell::rest());
    if (track.velocities.size() < requiredVelocities)
        track.velocities.resize(requiredVelocities,
            ValueCell::defaultValue());

    const auto writeHoldSpan = [](Track& target, std::size_t start,
                                   std::size_t end, bool writeKill) {
        const std::size_t length = std::max<std::size_t>(
            target.noteColumn.length, 1u);
        if (target.notes.size() < length)
            target.notes.resize(length, NoteCell::rest());
        const std::size_t distance = (end + length - start) % length;
        const std::size_t limit = distance == 0u ? length : distance;
        std::size_t holds = 0u;
        for (std::size_t step = 1u; step < limit; ++step) {
            target.notes[(start + step) % length] = NoteCell::hold();
            ++holds;
        }
        if (writeKill && distance != 0u)
            target.notes[end] = NoteCell::kill();
        return holds;
    };

    if (release) {
        result.holdRows = writeHoldSpan(track, onsetRow, row, true);
        result.release = true;
    } else {
        // A new live onset closes a still-held monophonic take before it
        // replaces the destination cell. Intermediate rows remain explicit
        // HLD cells; the new NOTE is the release/reattack boundary.
        if (live && !joiningChord && liveState && liveState->active
            && liveState->track < session.pattern.tracks.size()) {
            auto& previousTrack = session.pattern.tracks[liveState->track];
            const std::size_t previousLength = std::max<std::size_t>(
                previousTrack.noteColumn.length, 1u);
            const std::size_t replacementRow = capture.row % previousLength;
            result.holdRows += writeHoldSpan(previousTrack,
                liveState->onsetRow % previousLength, replacementRow, false);
        }
        if (live && liveState) {
            if (!joiningChord) {
                liveState->clear();
                liveState->active = true;
                liveState->track = trackIndex;
                liveState->onsetRow = row;
            }
            if (!addRecordedVoice(*liveState, capture.note,
                    capture.velocity, capture.channel)) {
                result.code = MidiStepRecordCode::InvalidEvent;
                return result;
            }
            writeRecordedChord(track, row, velocityRow, *liveState);
        } else {
            track.notes[row] = NoteCell::withNote(capture.note);
            track.velocities[velocityRow] = ValueCell::withValue(
                static_cast<float>(capture.velocity) / 127.0f);
        }
    }
    if (!live) {
        track.noteColumn.length = std::max(
            track.noteColumn.length, row + 1u);
        track.velocityColumn.length = std::max(
            track.velocityColumn.length, velocityRow + 1u);
    }

    if (mode != MidiStepRecordMode::LiveUnquantized) {
        clearMicroTime(track, row);
    } else if ((!release || writesReleaseCell) && !joiningChord) {
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

    if (mode == MidiStepRecordMode::Step) {
        session.pattern.visibleRows = std::max(
            session.pattern.visibleRows, row + 1u);
        const std::size_t rows = std::max<std::size_t>(
            session.pattern.visibleRows, 1u);
        session.selectedRow = (row + std::max<std::size_t>(
            stepAdvance, 1u)) % rows;
    } else {
        // Live capture follows the written NOTE so the highlight and recorded
        // cell agree, but it does not perform STEP's post-write advance.
        session.selectedRow = row;
    }
    if (live && liveState && release) liveState->clear();
    result.code = MidiStepRecordCode::Recorded;
    result.fxPair = pairIndex;
    result.microTime = microTime;
    return result;
}

} // namespace s3g::tracker
