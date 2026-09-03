#include "s3g/tracker/song_playback_planner.h"

#include <cmath>
#include <limits>
#include <utility>

namespace s3g::tracker {
namespace {

constexpr double kMinimumSongBpm = 20.0;
constexpr double kMaximumSongBpm = 400.0;
constexpr double kMinimumSongSwing = 0.5;
constexpr double kMaximumSongSwing = 0.75;

uint64_t saturatingIncrement(uint64_t value) noexcept
{
    return value == std::numeric_limits<uint64_t>::max()
        ? value : value + 1u;
}

uint64_t totalRowTicks(const SongRow& row) noexcept
{
    return static_cast<uint64_t>(row.durationTicks)
        * static_cast<uint64_t>(row.repeats);
}

} // namespace

SongValidationResult validateSongArrangement(
    const SongArrangement& arrangement) noexcept
{
    if (arrangement.rows.empty())
        return { SongValidationCode::Empty, kNoSongRow };
    if (arrangement.rows.size() > kMaximumSongRows)
        return { SongValidationCode::TooManyRows, kNoSongRow };
    if (arrangement.ticksPerBeat == 0u
        || arrangement.ticksPerBeat > kMaximumSongTicksPerBeat) {
        return { SongValidationCode::InvalidTicksPerBeat, kNoSongRow };
    }

    for (std::size_t index = 0u; index < arrangement.rows.size(); ++index) {
        const auto& row = arrangement.rows[index];
        if (row.patternId.empty())
            return { SongValidationCode::EmptyPatternId, index };
        if (row.durationTicks == 0u
            || row.durationTicks > kMaximumSongDurationTicks) {
            return { SongValidationCode::InvalidDurationTicks, index };
        }
        if (row.repeats == 0u || row.repeats > kMaximumSongRepeats)
            return { SongValidationCode::InvalidRepeats, index };
        if (!std::isfinite(row.energy)
            || row.energy < 0.0f || row.energy > 1.0f)
            return { SongValidationCode::InvalidEnergy, index };
        if (row.bpm && (!std::isfinite(*row.bpm)
                || *row.bpm < kMinimumSongBpm
                || *row.bpm > kMaximumSongBpm)) {
            return { SongValidationCode::InvalidBpm, index };
        }
        if (row.swing && (!std::isfinite(*row.swing)
                || *row.swing < kMinimumSongSwing
                || *row.swing > kMaximumSongSwing)) {
            return { SongValidationCode::InvalidSwing, index };
        }
        if (row.timingWarpLibraryIndex
            && *row.timingWarpLibraryIndex
                >= kMaximumTimingWarpLibraryEntries) {
            return { SongValidationCode::InvalidTimingWarpLibraryIndex,
                index };
        }
        if (row.patternLoop
            && (row.patternLoop->startRow >= row.patternLoop->endRow
                || row.patternLoop->endRow > kMaximumSongPatternRows)) {
            return { SongValidationCode::InvalidPatternLoop, index };
        }
    }
    return {};
}

SongValidationResult SongPlaybackPlanner::setArrangement(
    SongArrangement arrangement)
{
    const auto validation = validateSongArrangement(arrangement);
    if (!validation.ok()) return validation;

    arrangement_ = std::move(arrangement);
    currentRow_ = 0u;
    absoluteTick_ = 0u;
    ticksInRow_ = 0u;
    songLoopPassIndex_ = 0u;
    pending_.reset();
    running_ = false;
    finished_ = false;
    return {};
}

bool SongPlaybackPlanner::start(std::size_t rowIndex) noexcept
{
    if (rowIndex >= arrangement_.rows.size()) return false;
    currentRow_ = rowIndex;
    absoluteTick_ = 0u;
    ticksInRow_ = 0u;
    songLoopPassIndex_ = 0u;
    pending_.reset();
    running_ = true;
    finished_ = false;
    return true;
}

bool SongPlaybackPlanner::resume() noexcept
{
    if (arrangement_.rows.empty() || finished_) return false;
    running_ = true;
    return true;
}

void SongPlaybackPlanner::reset() noexcept
{
    currentRow_ = 0u;
    absoluteTick_ = 0u;
    ticksInRow_ = 0u;
    songLoopPassIndex_ = 0u;
    pending_.reset();
    running_ = false;
    finished_ = false;
}

SongQueueResult SongPlaybackPlanner::queueRow(std::size_t rowIndex,
    SongLaunchQuantization quantization) noexcept
{
    if (arrangement_.rows.empty()) return SongQueueResult::NoArrangement;
    if (rowIndex >= arrangement_.rows.size())
        return SongQueueResult::RowOutOfRange;
    if (!running_ && !finished_) return SongQueueResult::NotRunning;
    switch (quantization) {
    case SongLaunchQuantization::NextTick:
    case SongLaunchQuantization::NextBeat:
    case SongLaunchQuantization::NextPatternCycle:
    case SongLaunchQuantization::NextSongRow:
        break;
    default:
        return SongQueueResult::InvalidQuantization;
    }
    // A non-looping Song can finish while the host transport keeps running.
    // Re-open its logical clock so a queued row can relaunch on the next safe
    // boundary; pendingIsDue() treats the already-passed end as immediately
    // due after one more tracker tick.
    if (finished_) running_ = true;
    pending_ = PendingLaunch { rowIndex, quantization };
    return SongQueueResult::Queued;
}

std::optional<std::size_t> SongPlaybackPlanner::currentRowIndex() const noexcept
{
    if (arrangement_.rows.empty()) return std::nullopt;
    return currentRow_;
}

const SongRow* SongPlaybackPlanner::currentRow() const noexcept
{
    if (arrangement_.rows.empty() || currentRow_ >= arrangement_.rows.size())
        return nullptr;
    return &arrangement_.rows[currentRow_];
}

std::optional<std::size_t> SongPlaybackPlanner::pendingRowIndex() const noexcept
{
    if (!pending_) return std::nullopt;
    return pending_->row;
}

std::optional<SongLaunchQuantization>
SongPlaybackPlanner::pendingQuantization() const noexcept
{
    if (!pending_) return std::nullopt;
    return pending_->quantization;
}

uint32_t SongPlaybackPlanner::currentRepeatIndex() const noexcept
{
    const auto* row = currentRow();
    if (!row) return 0u;
    if (finished_) return row->repeats - 1u;
    return static_cast<uint32_t>(ticksInRow_ / row->durationTicks);
}

uint32_t SongPlaybackPlanner::tickInPatternCycle() const noexcept
{
    const auto* row = currentRow();
    if (!row) return 0u;
    return static_cast<uint32_t>(ticksInRow_ % row->durationTicks);
}

bool SongPlaybackPlanner::pendingIsDue(bool patternBoundary,
    bool rowBoundary) const noexcept
{
    if (!pending_) return false;
    if (finished_) return true;
    switch (pending_->quantization) {
    case SongLaunchQuantization::NextTick:
        return true;
    case SongLaunchQuantization::NextBeat:
        return absoluteTick_ % arrangement_.ticksPerBeat == 0u;
    case SongLaunchQuantization::NextPatternCycle:
        return patternBoundary;
    case SongLaunchQuantization::NextSongRow:
        return rowBoundary;
    }
    return false;
}

SongRowTransition SongPlaybackPlanner::launch(std::size_t row,
    SongTransitionReason reason) noexcept
{
    const auto from = currentRow_;
    currentRow_ = row;
    ticksInRow_ = 0u;
    finished_ = false;
    return { from, row, reason };
}

SongTickResult SongPlaybackPlanner::advanceTick() noexcept
{
    SongTickResult result;
    if (!running_ || arrangement_.rows.empty()) return result;

    result.consumed = true;
    absoluteTick_ = saturatingIncrement(absoluteTick_);
    ticksInRow_ = saturatingIncrement(ticksInRow_);

    const auto& row = arrangement_.rows[currentRow_];
    result.patternCycleBoundary
        = ticksInRow_ % row.durationTicks == 0u;
    result.songRowBoundary = ticksInRow_ >= totalRowTicks(row);

    if (pendingIsDue(result.patternCycleBoundary,
            result.songRowBoundary)) {
        const auto target = pending_->row;
        pending_.reset();
        result.transition = launch(target,
            SongTransitionReason::QuantizedLaunch);
        return result;
    }

    if (!result.songRowBoundary) return result;

    if (currentRow_ + 1u < arrangement_.rows.size()) {
        result.transition = launch(currentRow_ + 1u,
            SongTransitionReason::NaturalAdvance);
        return result;
    }
    if (arrangement_.loop) {
        songLoopPassIndex_ = saturatingIncrement(songLoopPassIndex_);
        result.transition = launch(0u, SongTransitionReason::LoopWrap);
        return result;
    }

    running_ = false;
    finished_ = true;
    pending_.reset();
    result.finished = true;
    return result;
}

} // namespace s3g::tracker
