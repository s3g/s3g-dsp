#pragma once

#include "s3g/tracker/scheduled_event_timeline.h"
#include "s3g/tracker/timing_action_resolver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace s3g::tracker {

// Snapshot delivered after one logical tracker tick has been generated and
// its complete event bundle has been admitted to the timing timeline. Warped
// ticks may share absoluteSampleTime, but completedTickIndex remains strictly
// increasing and callbacks retain generation order.
struct LogicalTickBoundary {
    uint64_t completedTickIndex = 0u;
    uint64_t completedTransportRow = 0u;
    uint64_t absoluteSampleTime = 0u;
};

enum class LogicalTickBoundaryAction : uint8_t {
    Continue,
    StopAfterBoundary,
};

using LogicalTickObserver = LogicalTickBoundaryAction (*)(void* context,
    const LogicalTickBoundary& boundary) noexcept;

// Opt-in timing facade around the proven nominal Sequencer. Patterns without
// timing-expansion actions call straight through to Sequencer::process() when
// no logical-tick observer is installed. Timed or observed patterns are
// generated one tracker tick at a time, expanded into a bounded persistent
// timeline, and drained only when events enter the requested block.
// The nominal Sequencer remains the single source of authored events.
class TimingPlaybackScheduler {
public:
    void setPattern(Pattern pattern);
    void replacePattern(Pattern pattern);
    const Pattern& pattern() const { return sequencer_.pattern(); }
    bool preparePatternSet(std::vector<Pattern> patterns,
        std::size_t initialPatternIndex = 0u);
    bool activatePreparedPatternAtTickBoundary(
        std::size_t patternIndex) noexcept;

    void setTransport(TransportSettings settings);
    // Boundary-only delegates used by allocation-free Song playback. Invoke
    // these synchronously from LogicalTickObserver before returning Continue.
    // The retiming variant replaces the interval already computed by the core
    // for the completed tick; ordinary setTransport() cannot do that.
    void setTransportAtTickBoundary(TransportSettings settings) noexcept;
    void setTimingWarpLibrary(TimingWarpLibrary library)
    {
        timingWarpLibrary_ = std::move(library);
    }
    const TimingWarpLibrary& timingWarpLibrary() const noexcept
    {
        return timingWarpLibrary_;
    }
    void setRuntimeTrackMuteMask(uint32_t mask) noexcept
    {
        sequencer_.setRuntimeTrackMuteMask(mask);
    }
    uint32_t runtimeTrackMuteMask() const noexcept
    {
        return sequencer_.runtimeTrackMuteMask();
    }
    void setFillActive(bool active) noexcept
    {
        sequencer_.setFillActive(active);
    }
    void setSongConditionContext(uint64_t passIndex,
        uint64_t passCount) noexcept
    {
        sequencer_.setSongConditionContext(passIndex, passCount);
    }
    void setSongConditionContext(
        const SequencerConditionContext& context) noexcept
    {
        sequencer_.setSongConditionContext(context);
    }
    void clearSongConditionContext() noexcept
    {
        sequencer_.clearSongConditionContext();
    }
    void relaunchColumnsAtTickBoundary(std::size_t row = 0u) noexcept
    {
        sequencer_.relaunchColumnsAtTickBoundary(row);
    }
    void launchSongRegionAtTickBoundary(std::size_t row) noexcept
    {
        sequencer_.launchSongRegionAtTickBoundary(row);
    }
    bool resyncTrackColumnsAtTickBoundary(std::size_t track,
        std::size_t row = 0u) noexcept
    {
        return sequencer_.resyncTrackColumnsAtTickBoundary(track, row);
    }
    void resyncAllTrackColumnsAtTickBoundary(
        std::size_t row = 0u) noexcept
    {
        sequencer_.resyncAllTrackColumnsAtTickBoundary(row);
    }
    const TransportSettings& transport() const
    {
        return sequencer_.transport();
    }

    void start(bool resetPosition = true);
    bool startPreparedAtHostBeat(double hostBeat) noexcept;
    void stop();
    void reset();
    bool isPlaying() const { return sequencer_.isPlaying(); }
    void setRandomSeed(uint32_t seed) { sequencer_.setRandomSeed(seed); }

    // The observer is optional and never owned. Installing one selects the
    // single-tick scheduling path even for untimed patterns so every logical
    // boundary is visible. It is invoked synchronously from process(), after
    // all events for that tick have been expanded/enqueued and before another
    // tick is generated. The callback and context must remain valid until
    // replaced or cleared. StopAfterBoundary stops core tick generation but
    // still drains the events already enqueued for the current process block.
    void setLogicalTickObserver(LogicalTickObserver observer,
        void* context = nullptr) noexcept
    {
        logicalTickObserver_ = observer;
        logicalTickObserverContext_ = context;
    }

    std::size_t process(uint32_t frameCount, ScheduledEvent* output,
        std::size_t outputCapacity) noexcept;

    uint64_t tickIndex() const { return sequencer_.tickIndex(); }
    uint64_t transportRow() const { return sequencer_.transportRow(); }
    uint64_t nextTickSampleFrame() const noexcept
    {
        return sequencer_.nextTickSampleFrame();
    }
    uint64_t renderedFrameCount() const
    {
        return sequencer_.renderedFrameCount();
    }
    uint64_t droppedEventCount() const noexcept
    {
        return sequencer_.droppedEventCount()
            + timingDroppedEventCount_
            + timeline_.droppedEventCount();
    }
    std::size_t pendingEventCount() const noexcept
    {
        return timeline_.pendingEventCount();
    }
    std::size_t pendingEventHighWaterMark() const noexcept
    {
        return timeline_.highWaterMark();
    }

    std::size_t notePosition(std::size_t track) const noexcept
    {
        return sequencer_.notePosition(track);
    }
    std::size_t instrumentPosition(std::size_t track) const noexcept
    {
        return sequencer_.instrumentPosition(track);
    }
    std::size_t velocityPosition(std::size_t track) const noexcept
    {
        return sequencer_.velocityPosition(track);
    }
    std::size_t lastNotePosition(std::size_t track) const noexcept
    {
        return sequencer_.lastNotePosition(track);
    }
    bool lastNoteTriggered(std::size_t track) const noexcept
    {
        return sequencer_.lastNoteTriggered(track);
    }
    std::size_t lastInstrumentPosition(std::size_t track) const noexcept
    {
        return sequencer_.lastInstrumentPosition(track);
    }
    std::size_t lastVelocityPosition(std::size_t track) const noexcept
    {
        return sequencer_.lastVelocityPosition(track);
    }
    std::size_t fxActionPosition(std::size_t track,
        std::size_t pair) const noexcept
    {
        return sequencer_.fxActionPosition(track, pair);
    }
    std::size_t fxValuePosition(std::size_t track,
        std::size_t pair) const noexcept
    {
        return sequencer_.fxValuePosition(track, pair);
    }
    std::size_t lastFxActionPosition(std::size_t track,
        std::size_t pair) const noexcept
    {
        return sequencer_.lastFxActionPosition(track, pair);
    }
    std::size_t lastFxValuePosition(std::size_t track,
        std::size_t pair) const noexcept
    {
        return sequencer_.lastFxValuePosition(track, pair);
    }

private:
    struct FxTimingSummary {
        bool authoredValue = false;
        bool explicitTiming = false;
        bool explicitMicroTiming = false;
        bool linearControl = false;
        bool hasPrevious = false;
    };
    using PatternTimingSummary = std::array<
        std::array<FxTimingSummary, kFxPairCount>, kMaximumTrackCount>;

    static PatternTimingSummary summarizeTiming(const Pattern& pattern)
        noexcept;
    void rebuildTimingState();
    void rebuildTimingState(const PatternTimingSummary& summary) noexcept;
    void resolveCurrentTick(uint64_t tickDurationSamples) noexcept;
    uint64_t allocateSecondaryNoteId() noexcept;

    Sequencer sequencer_;
    ScheduledEventTimeline timeline_;
    std::array<TimingEventExpansion, kMaximumTrackCount> timing_ {};
    std::vector<PatternTimingSummary> preparedTimingSummaries_;
    std::size_t activePreparedTimingIndex_ = 0u;
    std::array<ScheduledEvent,
        kMaximumScheduledEventsPerBlock> generatedEvents_ {};
    uint64_t nextSecondaryNoteId_ = static_cast<uint64_t>(
        std::numeric_limits<int32_t>::max());
    uint64_t highestPrimaryNoteId_ = 0u;
    uint64_t timingDroppedEventCount_ = 0u;
    TimingWarpLibrary timingWarpLibrary_;
    LogicalTickObserver logicalTickObserver_ = nullptr;
    void* logicalTickObserverContext_ = nullptr;
    bool timingSchedulerActive_ = false;
    bool microTimingCompensationActive_ = false;
};

} // namespace s3g::tracker
