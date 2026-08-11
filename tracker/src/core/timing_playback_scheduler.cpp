#include "s3g/tracker/timing_playback_scheduler.h"

#include "s3g/tracker/timing_event_expansion.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace s3g::tracker {

namespace {

constexpr bool isTimingExpansionAction(SequencerAction action) noexcept
{
    switch (action) {
    case SequencerAction::Ratchet:
    case SequencerAction::MicroTime:
    case SequencerAction::Delay:
    case SequencerAction::Flam:
    case SequencerAction::Stutter:
    case SequencerAction::Accent:
    case SequencerAction::Ghost:
    case SequencerAction::WarpRecall:
        return true;
    case SequencerAction::Probability:
    case SequencerAction::Skip:
    case SequencerAction::Offset:
    case SequencerAction::RepeatPrevious:
    case SequencerAction::Euclid:
    case SequencerAction::Count:
        return false;
    }
    return false;
}

} // namespace

void TimingPlaybackScheduler::setPattern(Pattern pattern)
{
    preparedTimingSummaries_.clear();
    activePreparedTimingIndex_ = 0u;
    sequencer_.setPattern(std::move(pattern));
    rebuildTimingState();
    timeline_.reset();
}

void TimingPlaybackScheduler::replacePattern(Pattern pattern)
{
    preparedTimingSummaries_.clear();
    activePreparedTimingIndex_ = 0u;
    sequencer_.replacePattern(std::move(pattern));
    rebuildTimingState();
}

bool TimingPlaybackScheduler::preparePatternSet(
    std::vector<Pattern> patterns, std::size_t initialPatternIndex)
{
    if (patterns.empty() || initialPatternIndex >= patterns.size())
        return false;
    std::vector<PatternTimingSummary> summaries;
    summaries.reserve(patterns.size());
    for (const auto& pattern : patterns)
        summaries.push_back(summarizeTiming(pattern));
    if (!sequencer_.preparePatternSet(std::move(patterns),
            initialPatternIndex)) return false;
    preparedTimingSummaries_ = std::move(summaries);
    activePreparedTimingIndex_ = initialPatternIndex;
    rebuildTimingState(preparedTimingSummaries_[initialPatternIndex]);
    timeline_.reset();
    return true;
}

bool TimingPlaybackScheduler::activatePreparedPatternAtTickBoundary(
    std::size_t patternIndex) noexcept
{
    if (patternIndex >= preparedTimingSummaries_.size()) return false;
    if (patternIndex == activePreparedTimingIndex_) return true;
    if (!sequencer_.activatePreparedPatternAtTickBoundary(patternIndex))
        return false;
    activePreparedTimingIndex_ = patternIndex;
    rebuildTimingState(preparedTimingSummaries_[patternIndex]);
    return true;
}

void TimingPlaybackScheduler::setTransport(TransportSettings settings)
{
    sequencer_.setTransport(std::move(settings));
}

void TimingPlaybackScheduler::setTransportAtTickBoundary(
    TransportSettings settings) noexcept
{
    sequencer_.setTransportAtTickBoundary(std::move(settings));
}

void TimingPlaybackScheduler::start(bool resetPosition)
{
    sequencer_.start(resetPosition);
    rebuildTimingState();
    timeline_.reset();
    timingDroppedEventCount_ = 0u;
    nextSecondaryNoteId_ = static_cast<uint64_t>(
        std::numeric_limits<int32_t>::max());
    highestPrimaryNoteId_ = 0u;
}

bool TimingPlaybackScheduler::startPreparedAtHostBeat(
    double hostBeat) noexcept
{
    if (!sequencer_.startPreparedAtHostBeat(hostBeat)) return false;
    rebuildTimingState();
    timeline_.reset();
    timingDroppedEventCount_ = 0u;
    nextSecondaryNoteId_ = static_cast<uint64_t>(
        std::numeric_limits<int32_t>::max());
    highestPrimaryNoteId_ = 0u;
    return true;
}

void TimingPlaybackScheduler::stop()
{
    sequencer_.stop();
    timeline_.clear();
}

void TimingPlaybackScheduler::reset()
{
    sequencer_.reset();
    rebuildTimingState();
    timeline_.reset();
    timingDroppedEventCount_ = 0u;
    nextSecondaryNoteId_ = static_cast<uint64_t>(
        std::numeric_limits<int32_t>::max());
    highestPrimaryNoteId_ = 0u;
}

TimingPlaybackScheduler::PatternTimingSummary
TimingPlaybackScheduler::summarizeTiming(const Pattern& pattern) noexcept
{
    PatternTimingSummary result {};
    const auto trackCount = std::min(pattern.tracks.size(),
        result.size());
    for (std::size_t trackIndex = 0u; trackIndex < trackCount;
         ++trackIndex) {
        const auto& track = pattern.tracks[trackIndex];
        std::size_t pairIndex = 0u;
        for (const auto& pair : track.fxPairs) {
            auto& summary = result[trackIndex][pairIndex];
            if (!pair.valueColumn.muted) {
                const auto valueLength = std::min(pair.valueColumn.length,
                    pair.values.size());
                for (std::size_t row = 0u; row < valueLength; ++row) {
                    if (pair.values[row].state
                        == FxValueCellState::Value) {
                        summary.authoredValue = true;
                        break;
                    }
                }
            }
            if (pair.actionColumn.muted) {
                ++pairIndex;
                continue;
            }
            const auto length = std::min(pair.actionColumn.length,
                pair.actions.size());
            for (std::size_t row = 0u; row < length; ++row) {
                const auto& cell = pair.actions[row];
                if (cell.state == FxActionCellState::Previous) {
                    summary.hasPrevious = true;
                } else if (cell.state == FxActionCellState::Sequencer
                    && isTimingExpansionAction(cell.sequencerAction)) {
                    summary.explicitTiming = true;
                    if (cell.sequencerAction == SequencerAction::MicroTime)
                        summary.explicitMicroTiming = true;
                }
            }
            ++pairIndex;
        }
    }
    return result;
}

void TimingPlaybackScheduler::rebuildTimingState()
{
    rebuildTimingState(summarizeTiming(sequencer_.pattern()));
}

void TimingPlaybackScheduler::rebuildTimingState(
    const PatternTimingSummary& summary) noexcept
{
    timingSchedulerActive_ = false;
    microTimingCompensationActive_ = false;
    timing_.fill({});
    const auto trackCount = std::min(sequencer_.pattern().tracks.size(),
        summary.size());
    for (std::size_t trackIndex = 0u; trackIndex < trackCount;
         ++trackIndex) {
        for (std::size_t pairIndex = 0u; pairIndex < kFxPairCount;
             ++pairIndex) {
            const auto& cached = summary[trackIndex][pairIndex];
            const auto retained = sequencer_.fxMemorySnapshot(
                trackIndex, pairIndex);
            const bool valueReachable = cached.authoredValue
                || retained.hasValue;
            if (!valueReachable) continue;
            if (cached.explicitTiming) {
                timingSchedulerActive_ = true;
                microTimingCompensationActive_
                    = microTimingCompensationActive_
                    || cached.explicitMicroTiming;
            }
            if (!cached.hasPrevious || !retained.hasAction
                || retained.action.state != FxActionCellState::Sequencer
                || !isTimingExpansionAction(
                    retained.action.sequencerAction)) continue;
            timingSchedulerActive_ = true;
            if (retained.action.sequencerAction
                == SequencerAction::MicroTime)
                microTimingCompensationActive_ = true;
        }
    }
}

void TimingPlaybackScheduler::resolveCurrentTick(
    uint64_t tickDurationSamples) noexcept
{
    const auto trackCount = sequencer_.pattern().tracks.size();
    for (std::size_t trackIndex = 0u; trackIndex < trackCount; ++trackIndex) {
        const auto& track = sequencer_.pattern().tracks[trackIndex];
        std::array<SequencerActionValue, kFxPairCount> actions {};
        for (std::size_t pairIndex = 0u; pairIndex < kFxPairCount;
             ++pairIndex) {
            const auto& pair = track.fxPairs[pairIndex];
            bool execute = false;
            const auto actionLength = std::min(pair.actionColumn.length,
                pair.actions.size());
            if (!pair.actionColumn.muted && actionLength > 0u) {
                const auto position = sequencer_.lastFxActionPosition(
                    trackIndex, pairIndex) % actionLength;
                const auto& cell = pair.actions[position];
                if (cell.state == FxActionCellState::Parameter
                    || cell.state == FxActionCellState::Sequencer) {
                    execute = true;
                } else if (cell.state == FxActionCellState::Previous) {
                    execute = true;
                }
            }
            const auto memory = sequencer_.fxMemorySnapshot(
                trackIndex, pairIndex);
            if (!execute || !memory.hasAction || !memory.hasValue
                || memory.action.state != FxActionCellState::Sequencer)
                continue;
            actions[pairIndex] = { memory.action.sequencerAction,
                memory.value, true };
        }
        timing_[trackIndex] = resolveTimingActions(actions,
            sequencer_.transport(), tickDurationSamples,
            microTimingCompensationActive_);
    }
}

uint64_t TimingPlaybackScheduler::allocateSecondaryNoteId() noexcept
{
    if (nextSecondaryNoteId_ == 0u
        || nextSecondaryNoteId_ <= highestPrimaryNoteId_) {
        ++timingDroppedEventCount_;
        return 0u;
    }
    return nextSecondaryNoteId_--;
}

std::size_t TimingPlaybackScheduler::process(uint32_t frameCount,
    ScheduledEvent* output, std::size_t outputCapacity) noexcept
{
    if (!timingSchedulerActive_ && !logicalTickObserver_
        && timeline_.pendingEventCount() == 0u)
        return sequencer_.process(frameCount, output, outputCapacity);
    if (frameCount == 0u) return 0u;
    if (!output) outputCapacity = 0u;

    const uint64_t blockStart = sequencer_.renderedFrameCount();
    const uint64_t blockEnd = frameCount
            > std::numeric_limits<uint64_t>::max() - blockStart
        ? std::numeric_limits<uint64_t>::max()
        : blockStart + static_cast<uint64_t>(frameCount);

    // StopAfterBoundary intentionally halts logical tick generation without
    // clearing the timing timeline. Keep advancing only the delivery clock so
    // final-tick ratchet/stutter/flam/ghost tails can cross later process
    // partitions. No musical position, column, voice, or next-tick state is
    // changed by this path.
    if (!sequencer_.isPlaying()) {
        if (timeline_.pendingEventCount() == 0u) return 0u;
        sequencer_.advanceRenderClockWithoutTickGeneration(frameCount);
        return timeline_.drain(blockStart, blockEnd, output, outputCapacity);
    }

    while (sequencer_.renderedFrameCount() < blockEnd) {
        const auto& clock = sequencer_.transport();
        const auto nominalTick = static_cast<uint64_t>(std::max(1.0,
            std::round(clock.sampleRate * 60.0
                / (clock.bpm
                    * static_cast<double>(clock.ticksPerBeat)))));
        const auto remaining = static_cast<uint32_t>(
            blockEnd - sequencer_.renderedFrameCount());
        const uint64_t tickBefore = sequencer_.tickIndex();
        uint64_t rowBefore = sequencer_.transportRow();
        if (clock.loopEnabled && rowBefore >= clock.loopEndRow)
            rowBefore = clock.loopStartRow;
        const uint64_t tickSampleFrame = sequencer_.nextTickSampleFrame();
        const auto count = sequencer_.processSingleTick(remaining,
            generatedEvents_.data(), generatedEvents_.size());
        const uint64_t advanced = sequencer_.tickIndex() - tickBefore;
        if (advanced == 0u) break;
        std::size_t warpIndex = 0u;
        if (sequencer_.consumeTimingWarpRecall(warpIndex)) {
            if (const auto* preset = timingWarpLibrary_.entry(warpIndex)) {
                auto replacement = sequencer_.transport();
                replacement.warpCycleTicks = preset->cycleTicks;
                replacement.timingWarp = preset->stack;
                sequencer_.setTransportAtTickBoundary(
                    std::move(replacement));
                activeTimingWarpLibraryIndex_ = warpIndex;
            }
        }
        resolveCurrentTick(nominalTick);
        const auto activeTimingTracks = std::min(
            sequencer_.pattern().tracks.size(), timing_.size());
        for (std::size_t index = 0u; index < count; ++index) {
            const auto& event = generatedEvents_[index];
            highestPrimaryNoteId_ = std::max(highestPrimaryNoteId_,
                event.noteId);
            if (event.kind != ScheduledEventKind::NoteOn
                || event.track >= activeTimingTracks) {
                ScheduledEvent shifted = event;
                if (event.track < activeTimingTracks) {
                    shifted.absoluteSampleTime = detail::saturatingSampleAdd(
                        event.absoluteSampleTime,
                        timing_[event.track].baseDelaySamples);
                }
                if (shifted.kind == ScheduledEventKind::NoteOff
                    && timeline_.cancelPendingPrimaryOnset(
                        shifted.noteId, shifted.absoluteSampleTime))
                    continue;
                (void)timeline_.enqueue(shifted);
                continue;
            }
            std::array<ScheduledEvent, 17u> expandedEvents {};
            std::size_t expandedCount = 0u;
            expandTimingEvent(event, timing_[event.track],
                [this] { return allocateSecondaryNoteId(); },
                [&](const ScheduledEvent& expanded) {
                    if (expanded.noteId != 0u
                        && expandedCount < expandedEvents.size())
                        expandedEvents[expandedCount++] = expanded;
                });
            (void)timeline_.enqueue(expandedEvents.data(), expandedCount);
        }
        const auto observer = logicalTickObserver_;
        void* const observerContext = logicalTickObserverContext_;
        if (observer) {
            const auto action = observer(observerContext,
                LogicalTickBoundary { tickBefore, rowBefore,
                    tickSampleFrame });
            if (action == LogicalTickBoundaryAction::StopAfterBoundary) {
                // Do not call stop(): its public scheduler semantics clear the
                // timeline. The final boundary's admitted events remain
                // available to the drain below, while core generation stops
                // before another (possibly coincident) logical tick.
                sequencer_.stop();
                break;
            }
        }
    }

    if (!sequencer_.isPlaying()
        && sequencer_.renderedFrameCount() < blockEnd) {
        const uint64_t remainder = blockEnd
            - sequencer_.renderedFrameCount();
        sequencer_.advanceRenderClockWithoutTickGeneration(
            static_cast<uint32_t>(std::min<uint64_t>(remainder,
                std::numeric_limits<uint32_t>::max())));
    }

    return timeline_.drain(blockStart, blockEnd, output, outputCapacity);
}

} // namespace s3g::tracker
