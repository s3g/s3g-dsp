#pragma once

#include "s3g/tracker/command.h"
#include "s3g/tracker/runtime_pattern_plan.h"
#include "s3g/tracker/timing_playback_scheduler.h"
#include "s3g/tracker/visual_note_hit_mailbox.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace s3g::tracker {

// The Mac CLAP's host-clock and boundary policies, compiled as ordinary C++
// with no Cocoa, VSTGUI or CLAP-header dependency.
struct PatternLaunchMailbox {
    std::atomic<uint32_t> revision { 0u };
    std::atomic<uint32_t> consumedRevision { 0u };
    std::atomic<uint32_t> dueRevision { 0u };
    std::atomic<uint32_t> quantization { 0u };
};

struct MidiStepClock {
    void clear() noexcept;

    void publish(uint64_t current, uint64_t next,
        uint64_t currentTrackerRow, uint64_t nextTrackerRow) noexcept;

    bool nearestTarget(uint64_t frame, int64_t& offset,
        std::size_t& row) const noexcept;

    bool nextTarget(uint64_t frame, int64_t& offset,
        std::size_t& row) const noexcept;

    std::atomic<uint64_t> sequence { 0u };
    std::atomic<uint64_t> currentFrame { 0u };
    std::atomic<uint64_t> nextFrame { 0u };
    std::atomic<uint64_t> currentRow { 0u };
    std::atomic<uint64_t> nextRow { 0u };
    std::atomic<bool> valid { false };
};

using VisualNoteHitMailboxes = std::array<VisualNoteHitMailbox, kMaximumTrackCount>;

// Prepare on the main thread; arm/update/observe on the audio thread.
// The scheduler's observer points to this object, so its address must be stable.
struct ClapPlaybackRuntime {
    ClapPlaybackRuntime(const ClapPlaybackRuntime&) = delete;
    ClapPlaybackRuntime& operator=(const ClapPlaybackRuntime&) = delete;
    ClapPlaybackRuntime(ClapPlaybackRuntime&&) = delete;
    ClapPlaybackRuntime& operator=(ClapPlaybackRuntime&&) = delete;
    TimingPlaybackScheduler scheduler;
    SongPlaybackPlanner songPlanner;
    TransportSettings projectTransport;
    std::vector<std::string> patternIds;
    std::vector<std::size_t> songPatternIndices;
    double gateMilliseconds = 90.0;
    double tempoScale = 1.0;
    double latestHostTempo = 120.0;
    double latestSampleRate = 48000.0;
    bool songEnabled = false;
    bool valid = false;
    PatternLaunchMailbox* patternLaunch = nullptr;
    VisualNoteHitMailboxes* visualNoteHits = nullptr;
    MidiStepClock* midiStepClock = nullptr;
    uint64_t absoluteFrameOrigin = 0u;
    std::size_t initialSongRow = 0u;
    uint64_t visualTickStartSample = 0u;
    uint64_t visualTickEndSample = 1u;

    ClapPlaybackRuntime(const ProjectDocument& document, double sampleRate,
        PatternLaunchMailbox* launchMailbox = nullptr,
        VisualNoteHitMailboxes* hitMailboxes = nullptr,
        MidiStepClock* stepClock = nullptr);

    TransportSettings hostClock(double tempo, double sampleRate) const
        noexcept;

    double currentSongTempoMultiplier() const noexcept;

    double effectiveBpm(double hostTempo) const noexcept;

    TransportSettings songRowClock(const s3g::tracker::SongRow* row,
        const TransportSettings& host) const noexcept;

    static std::size_t songRowStart(const s3g::tracker::SongRow* row)
        noexcept;

    void updateSongConditionContext() noexcept;

    bool arm(double hostBeat, double tempo, double sampleRate,
        uint64_t absoluteStartFrame, bool forceAllRows = false) noexcept;

    void updateClock(double tempo, double sampleRate) noexcept;

    void publishVisualNoteHits(const LogicalTickBoundary& boundary) noexcept;

    static LogicalTickBoundaryAction advanceLogicalTick(void* context,
        const LogicalTickBoundary& boundary) noexcept;

    static LogicalTickBoundaryAction advancePatternLaunch(void* context,
        const LogicalTickBoundary& boundary) noexcept;

    void routeFor(uint8_t fallbackChannel, uint8_t& channel) const noexcept;
};

// Single audio-thread producer, single main-thread consumer. Full queues defer
// runtime swaps: never delete on the audio thread or overwrite an uncollected
// pointer. Destruction requires audio processing to have stopped.
class RuntimeRetirementQueue {
public:
    static constexpr uint32_t kCapacity = 64u;
    RuntimeRetirementQueue() = default;
    RuntimeRetirementQueue(const RuntimeRetirementQueue&) = delete;
    RuntimeRetirementQueue& operator=(const RuntimeRetirementQueue&) = delete;
    ~RuntimeRetirementQueue();
    bool full() const noexcept;
    bool retire(ClapPlaybackRuntime*) noexcept; // ownership transfers on success
    void drain(); // main thread only
private:
    std::array<ClapPlaybackRuntime*, kCapacity> retired_ {};
    std::atomic<uint32_t> read_ {0u}, write_ {0u};
};

} // namespace s3g::tracker
