#pragma once

#include "s3g/tracker/timing_warp.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace s3g::tracker {

// Native song-form limits. These are deliberately independent of the Max
// song format: the planner consumes validated in-memory data and has no import
// or repair behavior for legacy files.
constexpr std::size_t kMaximumSongRows = 4096u;
constexpr uint32_t kMaximumSongDurationTicks = 1u << 20u;
constexpr uint32_t kMaximumSongRepeats = 65535u;
constexpr uint32_t kMaximumSongTicksPerBeat = 96u;

// One arrangement row launches a named project pattern for durationTicks,
// then continues that pattern's phase for any additional repeats. A following
// row is a new launch, even when it references the same patternId.
//
// bpm and swing are row-level transport overrides. Their absence means the
// project transport remains unchanged. mutedTracks uses the same zero-based
// 32-lane bit positions as the tracker core. timingWarpLibraryIndex selects
// one saved project warp for the entire Song row; absence explicitly means
// identity timing (OFF), rather than inheriting the preceding row's warp.
struct SongRow {
    std::string patternId;
    uint32_t durationTicks = 16u;
    uint32_t repeats = 1u;
    std::optional<double> bpm;
    std::optional<double> swing;
    uint32_t mutedTracks = 0u;
    std::optional<std::size_t> timingWarpLibraryIndex;
};

struct SongArrangement {
    std::string name;
    std::vector<SongRow> rows;
    bool loop = false;
    // Used only for beat-quantized row launches. Row duration remains an
    // explicit count of tracker ticks and does not depend on tempo.
    uint32_t ticksPerBeat = 4u;
};

enum class SongValidationCode : uint8_t {
    Valid,
    Empty,
    TooManyRows,
    InvalidTicksPerBeat,
    EmptyPatternId,
    InvalidDurationTicks,
    InvalidRepeats,
    InvalidBpm,
    InvalidSwing,
    InvalidTimingWarpLibraryIndex,
};

constexpr std::size_t kNoSongRow = static_cast<std::size_t>(-1);

struct SongValidationResult {
    SongValidationCode code = SongValidationCode::Valid;
    // kNoSongRow means the error applies to the arrangement as a whole.
    std::size_t row = kNoSongRow;

    bool ok() const noexcept { return code == SongValidationCode::Valid; }
};

SongValidationResult validateSongArrangement(
    const SongArrangement& arrangement) noexcept;

// A queued launch always occurs after at least one more tracker tick. This
// makes UI-to-scheduler ordering explicit: queueing cannot rewrite the tick
// already being rendered.
enum class SongLaunchQuantization : uint8_t {
    NextTick,
    NextBeat,
    NextPatternCycle,
    NextSongRow,
};

enum class SongQueueResult : uint8_t {
    Queued,
    NoArrangement,
    NotRunning,
    RowOutOfRange,
    InvalidQuantization,
};

enum class SongTransitionReason : uint8_t {
    NaturalAdvance,
    LoopWrap,
    QuantizedLaunch,
};

struct SongRowTransition {
    std::size_t fromRow = 0u;
    std::size_t toRow = 0u;
    SongTransitionReason reason = SongTransitionReason::NaturalAdvance;
};

// Result of consuming exactly one tracker tick. transition, when present,
// takes effect before the next tracker tick. A repeat boundary does not launch
// the pattern again; this preserves continuous polymetric column phase within
// one song row.
struct SongTickResult {
    bool consumed = false;
    bool patternCycleBoundary = false;
    bool songRowBoundary = false;
    bool finished = false;
    std::optional<SongRowTransition> transition;
};

// Scheduler-owned, allocation-free tick planner. setArrangement() is a
// control-thread operation that may allocate. start(), queueRow(), and
// advanceTick() do not allocate after a valid arrangement has been installed.
// The class is deliberately not internally synchronized.
class SongPlaybackPlanner {
public:
    // Transactional: an invalid arrangement leaves both the prior arrangement
    // and its runtime position untouched. A valid arrangement becomes current
    // row zero in a stopped state and clears any queued launch.
    SongValidationResult setArrangement(SongArrangement arrangement);

    bool hasArrangement() const noexcept { return !arrangement_.rows.empty(); }
    const SongArrangement& arrangement() const noexcept { return arrangement_; }

    // Starts a fresh song-form pass at rowIndex. Pattern phase, global tick,
    // and any pending launch are reset. stop()/resume() mirror the sequencer's
    // pause behavior and preserve all of that state.
    bool start(std::size_t rowIndex = 0u) noexcept;
    void stop() noexcept { running_ = false; }
    bool resume() noexcept;
    void reset() noexcept;

    bool isRunning() const noexcept { return running_; }
    bool isFinished() const noexcept { return finished_; }

    SongQueueResult queueRow(std::size_t rowIndex,
        SongLaunchQuantization quantization) noexcept;
    void cancelQueuedRow() noexcept { pending_.reset(); }

    std::optional<std::size_t> currentRowIndex() const noexcept;
    const SongRow* currentRow() const noexcept;
    std::optional<std::size_t> pendingRowIndex() const noexcept;
    std::optional<SongLaunchQuantization> pendingQuantization() const noexcept;

    // Counts completed ticks since the most recent start(). This clock never
    // rewinds for a queued launch and therefore makes beat quantization stable
    // across arbitrary row jumps.
    uint64_t absoluteTick() const noexcept { return absoluteTick_; }
    uint64_t ticksCompletedInRow() const noexcept { return ticksInRow_; }
    uint32_t currentRepeatIndex() const noexcept;
    uint32_t tickInPatternCycle() const noexcept;

    SongTickResult advanceTick() noexcept;

private:
    struct PendingLaunch {
        std::size_t row = 0u;
        SongLaunchQuantization quantization
            = SongLaunchQuantization::NextSongRow;
    };

    bool pendingIsDue(bool patternBoundary, bool rowBoundary) const noexcept;
    SongRowTransition launch(std::size_t row,
        SongTransitionReason reason) noexcept;

    SongArrangement arrangement_;
    std::optional<PendingLaunch> pending_;
    std::size_t currentRow_ = 0u;
    uint64_t absoluteTick_ = 0u;
    uint64_t ticksInRow_ = 0u;
    bool running_ = false;
    bool finished_ = false;
};

} // namespace s3g::tracker
