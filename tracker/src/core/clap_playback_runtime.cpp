#include "s3g/tracker/clap_playback_runtime.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace s3g::tracker {

void MidiStepClock::clear() noexcept
{
    sequence.fetch_add(1u, std::memory_order_acq_rel);
    valid.store(false, std::memory_order_relaxed);
    sequence.fetch_add(1u, std::memory_order_release);
}

void MidiStepClock::publish(uint64_t current, uint64_t next,
    uint64_t currentTrackerRow, uint64_t nextTrackerRow) noexcept
{
    sequence.fetch_add(1u, std::memory_order_acq_rel);
    currentFrame.store(current, std::memory_order_relaxed);
    nextFrame.store(std::max(current, next), std::memory_order_relaxed);
    currentRow.store(currentTrackerRow, std::memory_order_relaxed);
    nextRow.store(nextTrackerRow, std::memory_order_relaxed);
    valid.store(true, std::memory_order_relaxed);
    sequence.fetch_add(1u, std::memory_order_release);
}

bool MidiStepClock::nearestTarget(uint64_t frame, int64_t& offset,
    std::size_t& row) const noexcept
{
    for (unsigned attempt = 0u; attempt < 4u; ++attempt) {
        const uint64_t before = sequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const bool available = valid.load(std::memory_order_relaxed);
        const uint64_t current = currentFrame.load(
            std::memory_order_relaxed);
        const uint64_t next = nextFrame.load(std::memory_order_relaxed);
        const uint64_t currentTrackerRow = currentRow.load(
            std::memory_order_relaxed);
        const uint64_t nextTrackerRow = nextRow.load(
            std::memory_order_relaxed);
        const uint64_t after = sequence.load(std::memory_order_acquire);
        if (before != after || (after & 1u) != 0u) continue;
        if (!available || frame < current) return false;
        const uint64_t currentDistance = frame - current;
        const uint64_t nextDistance = next >= frame
            ? next - frame : std::numeric_limits<uint64_t>::max();
        const bool chooseNext = next > current
            && nextDistance < currentDistance;
        const uint64_t magnitude = chooseNext
            ? nextDistance : currentDistance;
        const uint64_t bounded = std::min<uint64_t>(magnitude,
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        offset = chooseNext ? -static_cast<int64_t>(bounded)
                            : static_cast<int64_t>(bounded);
        row = static_cast<std::size_t>(chooseNext
            ? nextTrackerRow : currentTrackerRow);
        return true;
    }
    return false;
}

bool MidiStepClock::nextTarget(uint64_t frame, int64_t& offset,
    std::size_t& row) const noexcept
{
    for (unsigned attempt = 0u; attempt < 4u; ++attempt) {
        const uint64_t before = sequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const bool available = valid.load(std::memory_order_relaxed);
        const uint64_t next = nextFrame.load(std::memory_order_relaxed);
        const uint64_t nextTrackerRow = nextRow.load(
            std::memory_order_relaxed);
        const uint64_t after = sequence.load(std::memory_order_acquire);
        if (before != after || (after & 1u) != 0u) continue;
        if (!available) return false;
        const uint64_t magnitude = frame >= next
            ? frame - next : next - frame;
        const uint64_t bounded = std::min<uint64_t>(magnitude,
            static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
        offset = frame >= next ? static_cast<int64_t>(bounded)
                               : -static_cast<int64_t>(bounded);
        row = static_cast<std::size_t>(nextTrackerRow);
        return true;
    }
    return false;
}

ClapPlaybackRuntime::ClapPlaybackRuntime(const ProjectDocument& document, double sampleRate,
    PatternLaunchMailbox* launchMailbox,
    VisualNoteHitMailboxes* hitMailboxes,
    MidiStepClock* stepClock)
    : projectTransport(document.transport)
    , gateMilliseconds(document.session.gateMilliseconds)
    , tempoScale(std::clamp(document.session.tempoScale, 0.25, 4.0))
    , latestSampleRate(sampleRate)
    , patternLaunch(launchMailbox)
    , visualNoteHits(hitMailboxes)
    , midiStepClock(stepClock)
{
    s3g::tracker::RuntimePatternPlan plan;
    if (!s3g::tracker::makeRuntimePatternPlan(document, plan)) return;
    std::vector<s3g::tracker::Pattern> patterns;
    patterns.reserve(plan.documentPatternIndices.size());
    patternIds.reserve(plan.documentPatternIndices.size());
    for (const auto index : plan.documentPatternIndices) {
        const auto& entry = document.patternBank.entries[index];
        patterns.push_back(entry.pattern);
        patternIds.push_back(entry.id);
    }

    songEnabled = plan.songEnabled
        && songPlanner.setArrangement(document.song).ok();
    if (songEnabled) {
        songPatternIndices = plan.songPatternIndices;
    }
    projectTransport.sampleRate = sampleRate;
    scheduler.setBurstBanks(document.burstBanks);
    valid = scheduler.preparePatternSet(std::move(patterns),
        plan.initialPatternIndex);
    if (!valid) return;
    scheduler.setTimingWarpLibrary(document.warpLibrary);
    scheduler.setTransport(projectTransport);
    scheduler.setRandomSeed(document.session.playbackSeed);
    scheduler.setLogicalTickObserver(&ClapPlaybackRuntime::advanceLogicalTick, this);
}

TransportSettings ClapPlaybackRuntime::hostClock(double tempo, double sampleRate) const
    noexcept
{
    auto result = projectTransport;
    result.sampleRate = sampleRate;
    if (std::isfinite(tempo) && tempo > 0.0)
        result.bpm = tempo * tempoScale;
    return result;
}

double ClapPlaybackRuntime::currentSongTempoMultiplier() const noexcept
{
    const auto* row = songEnabled ? songPlanner.currentRow() : nullptr;
    if (!row || !std::isfinite(row->tempoMultiplier)) return 1.0;
    return std::clamp(row->tempoMultiplier,
        s3g::tracker::kMinimumSongTempoMultiplier,
        s3g::tracker::kMaximumSongTempoMultiplier);
}

double ClapPlaybackRuntime::effectiveBpm(double hostTempo) const noexcept
{
    const auto clock = hostClock(hostTempo, latestSampleRate);
    return std::clamp(clock.bpm * currentSongTempoMultiplier(),
        5.0, 1600.0);
}

TransportSettings ClapPlaybackRuntime::songRowClock(const s3g::tracker::SongRow* row,
    const TransportSettings& host) const noexcept
{
    auto result = projectTransport;
    result.sampleRate = host.sampleRate;
    result.bpm = std::clamp(host.bpm * (row
            ? std::clamp(row->tempoMultiplier,
                s3g::tracker::kMinimumSongTempoMultiplier,
                s3g::tracker::kMaximumSongTempoMultiplier)
            : 1.0),
        5.0, 1600.0);
    result.timingWarp.clear();
    result.timingWarpEnabled = false;
    result.loopEnabled = false;
    if (!row) return result;
    if (row->swing) result.swing = *row->swing;
    if (row->patternLoop) {
        result.loopEnabled = true;
        result.loopStartRow = row->patternLoop->startRow;
        result.loopEndRow = row->patternLoop->endRow;
    }
    if (row->timingWarpLibraryIndex) {
        const auto* entry = scheduler.timingWarpLibrary().entry(
            *row->timingWarpLibraryIndex);
        if (entry) {
            result.warpCycleTicks = entry->cycleTicks;
            result.timingWarp = entry->stack;
            // A Song row's named WARP choice is already an explicit
            // enable action, independent of Pattern transport's live
            // Warps switch.
            result.timingWarpEnabled = true;
        }
    }
    return result;
}

std::size_t ClapPlaybackRuntime::songRowStart(const s3g::tracker::SongRow* row)
    noexcept
{
    return row && row->patternLoop ? row->patternLoop->startRow : 0u;
}

void ClapPlaybackRuntime::updateSongConditionContext() noexcept
{
    const auto* row = songPlanner.currentRow();
    if (!songEnabled || !row) {
        scheduler.clearSongConditionContext();
        return;
    }
    s3g::tracker::SequencerConditionContext context;
    context.passIndex = songPlanner.currentRepeatIndex();
    context.passCount = row->repeats;
    context.songActive = true;
    context.songRowIndex = songPlanner.currentRowIndex().value_or(0u);
    context.songRowCount = songPlanner.arrangement().rows.size();
    context.songLoopPassIndex = songPlanner.songLoopPassIndex();
    context.songEnergy = row->energy;
    scheduler.setSongConditionContext(context);
}

bool ClapPlaybackRuntime::arm(double hostBeat, double tempo, double sampleRate,
    uint64_t absoluteStartFrame, bool forceAllRows) noexcept
{
    if (!valid) return false;
    absoluteFrameOrigin = absoluteStartFrame;
    latestHostTempo = tempo;
    latestSampleRate = sampleRate;
    if (midiStepClock) midiStepClock->clear();
    auto clock = hostClock(tempo, sampleRate);
    if (songEnabled) {
        songPlanner.reset();
        if (songPatternIndices.empty()) return false;
        const auto startRow = std::min(
            initialSongRow, songPatternIndices.size() - 1u);
        initialSongRow = 0u;
        if (!songPlanner.start(startRow))
            return false;
        (void)scheduler.activatePreparedPatternAtTickBoundary(
            songPatternIndices[startRow]);
        const auto* row = songPlanner.currentRow();
        clock = songRowClock(row, clock);
        scheduler.setRuntimeTrackMuteMask(row ? row->mutedTracks : 0u);
        updateSongConditionContext();
    } else {
        scheduler.setRuntimeTrackMuteMask(0u);
        scheduler.clearSongConditionContext();
    }
    scheduler.setTransport(std::move(clock));
    scheduler.setLogicalTickObserver(&ClapPlaybackRuntime::advanceLogicalTick, this);
    const bool started = scheduler.startPreparedAtHostBeat(hostBeat);
    if (started && forceAllRows)
        scheduler.resyncAllTrackColumnsAtTickBoundary(0u);
    else if (started && songEnabled)
        scheduler.launchSongRegionAtTickBoundary(songRowStart(
            songPlanner.currentRow()));
    return started;
}

void ClapPlaybackRuntime::updateClock(double tempo, double sampleRate) noexcept
{
    // Host clock refreshes happen every process segment. Preserve the
    // current Song-row warp and update only fields owned by the host.
    auto current = scheduler.transport();
    latestHostTempo = tempo;
    latestSampleRate = sampleRate;
    current.sampleRate = sampleRate;
    current.bpm = effectiveBpm(tempo);
    scheduler.setTransport(std::move(current));
}

void ClapPlaybackRuntime::publishVisualNoteHits(const LogicalTickBoundary& boundary) noexcept
{
    if (midiStepClock) {
        const auto addOrigin = [&](uint64_t frame) {
            return frame > std::numeric_limits<uint64_t>::max()
                    - absoluteFrameOrigin
                ? std::numeric_limits<uint64_t>::max()
                : absoluteFrameOrigin + frame;
        };
        const auto& settings = scheduler.transport();
        const uint64_t rows = std::max<uint64_t>(
            scheduler.pattern().visibleRows, 1u);
        const uint64_t currentRow =
            boundary.completedTransportRow % rows;
        uint64_t nextTransportRow = boundary.completedTransportRow
                == std::numeric_limits<uint64_t>::max()
            ? boundary.completedTransportRow
            : boundary.completedTransportRow + 1u;
        if (settings.loopEnabled
            && nextTransportRow >= settings.loopEndRow) {
            nextTransportRow = settings.loopStartRow;
        }
        midiStepClock->publish(addOrigin(boundary.absoluteSampleTime),
            addOrigin(scheduler.nextTickSampleFrame()), currentRow,
            nextTransportRow % rows);
    }
    if (!visualNoteHits) return;
    const auto trackCount = std::min<std::size_t>(
        scheduler.pattern().tracks.size(), visualNoteHits->size());
    for (std::size_t track = 0u; track < trackCount; ++track) {
        if (!scheduler.lastNoteTriggered(track)) continue;
        (*visualNoteHits)[track].publish(
            scheduler.lastNotePosition(track),
            boundary.absoluteSampleTime);
    }
}

LogicalTickBoundaryAction ClapPlaybackRuntime::advanceLogicalTick(void* context,
    const LogicalTickBoundary& boundary) noexcept
{
    auto& runtime = *static_cast<ClapPlaybackRuntime*>(context);
    runtime.visualTickStartSample = boundary.absoluteSampleTime;
    runtime.visualTickEndSample = std::max<uint64_t>(
        boundary.absoluteSampleTime + 1u,
        runtime.scheduler.nextTickSampleFrame());
    runtime.publishVisualNoteHits(boundary);
    if (!runtime.songEnabled)
        return advancePatternLaunch(context, boundary);
    const bool wasFinished = runtime.songPlanner.isFinished();
    const auto result = runtime.songPlanner.advanceTick();
    runtime.updateSongConditionContext();
    if (runtime.patternLaunch) {
        auto& mailbox = *runtime.patternLaunch;
        const uint32_t revision = mailbox.revision.load(
            std::memory_order_acquire);
        if (revision != 0u
            && revision != mailbox.consumedRevision.load(
                std::memory_order_relaxed)) {
            const auto quantization =
                static_cast<PatternVariationLaunch>(
                    mailbox.quantization.load(
                        std::memory_order_relaxed));
            bool due = wasFinished
                || quantization == PatternVariationLaunch::NextTick;
            if (!due && quantization
                    == PatternVariationLaunch::NextBeat) {
                due = s3g::tracker::patternVariationLaunchIsDue(
                    quantization, boundary.completedTickIndex,
                    boundary.completedTransportRow,
                    runtime.scheduler.transport().ticksPerBeat,
                    runtime.scheduler.pattern().visibleRows);
            } else if (!due && quantization
                    == PatternVariationLaunch::NextPatternCycle) {
                due = result.patternCycleBoundary;
            } else if (!due && quantization
                    == PatternVariationLaunch::NextSongRow) {
                due = result.songRowBoundary;
            }
            if (due) {
                mailbox.consumedRevision.store(
                    revision, std::memory_order_relaxed);
                mailbox.dueRevision.store(
                    revision, std::memory_order_release);
                return LogicalTickBoundaryAction::StopAfterBoundary;
            }
        }
    }
    if (result.transition) {
        const auto rowIndex = runtime.songPlanner.currentRowIndex();
        const auto* row = runtime.songPlanner.currentRow();
        if (rowIndex && *rowIndex < runtime.songPatternIndices.size()) {
            (void)runtime.scheduler.activatePreparedPatternAtTickBoundary(
                runtime.songPatternIndices[*rowIndex]);
            runtime.scheduler.launchSongRegionAtTickBoundary(
                runtime.songRowStart(row));
            runtime.scheduler.setRuntimeTrackMuteMask(
                row ? row->mutedTracks : 0u);
            runtime.scheduler.setTransportAtTickBoundary(
                runtime.songRowClock(row,
                    runtime.hostClock(runtime.latestHostTempo,
                        runtime.latestSampleRate)));
        }
    }
    if (result.finished) {
        // Keep a silent logical clock alive while REAPER continues. This
        // lets SELECT QUEUE relaunch a row after a non-looping Song has
        // reached its end, without leaking notes from the final pattern.
        runtime.scheduler.setRuntimeTrackMuteMask(
            std::numeric_limits<uint32_t>::max());
    }
    return LogicalTickBoundaryAction::Continue;
}

LogicalTickBoundaryAction ClapPlaybackRuntime::advancePatternLaunch(void* context,
    const LogicalTickBoundary& boundary) noexcept
{
    auto& runtime = *static_cast<ClapPlaybackRuntime*>(context);
    if (!runtime.patternLaunch)
        return LogicalTickBoundaryAction::Continue;
    auto& mailbox = *runtime.patternLaunch;
    const uint32_t revision = mailbox.revision.load(
        std::memory_order_acquire);
    if (revision == 0u || revision == mailbox.consumedRevision.load(
            std::memory_order_relaxed))
        return LogicalTickBoundaryAction::Continue;

    const auto quantization = static_cast<PatternVariationLaunch>(
        mailbox.quantization.load(std::memory_order_relaxed));
    const bool due = s3g::tracker::patternVariationLaunchIsDue(
        quantization, boundary.completedTickIndex,
        boundary.completedTransportRow,
        runtime.scheduler.transport().ticksPerBeat,
        runtime.scheduler.pattern().visibleRows);
    if (!due) return LogicalTickBoundaryAction::Continue;
    mailbox.consumedRevision.store(revision, std::memory_order_relaxed);
    mailbox.dueRevision.store(revision, std::memory_order_release);
    return LogicalTickBoundaryAction::StopAfterBoundary;
}

void ClapPlaybackRuntime::routeFor(uint8_t fallbackChannel, uint8_t& channel) const noexcept
{
    channel = static_cast<uint8_t>(std::clamp<int>(
        fallbackChannel, 1, 16) - 1);
}

RuntimeRetirementQueue::~RuntimeRetirementQueue() { drain(); }

bool RuntimeRetirementQueue::full() const noexcept
{
    const auto write = write_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    return write - read >= kCapacity;
}

bool RuntimeRetirementQueue::retire(ClapPlaybackRuntime* runtime) noexcept
{
    if (!runtime) return true;
    const auto write = write_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    if (write - read >= kCapacity) return false;
    retired_[write % kCapacity] = runtime;
    write_.store(write + 1u, std::memory_order_release);
    return true;
}

void RuntimeRetirementQueue::drain()
{
    auto read = read_.load(std::memory_order_relaxed);
    const auto write = write_.load(std::memory_order_acquire);
    while (read != write) {
        delete retired_[read % kCapacity];
        retired_[read % kCapacity] = nullptr;
        ++read;
    }
    read_.store(read, std::memory_order_release);
}

} // namespace s3g::tracker
