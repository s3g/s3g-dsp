#include "s3g/tracker/sequencer.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace s3g::tracker {
namespace {

// Authored project/console tempo remains constrained to 20..400, while the
// CLAP host-rate multiplier may legitimately produce quarter-speed 5 BPM or
// four-times-speed 1600 BPM clocks. The realtime clock must preserve those
// musical ratios rather than silently clamping them back to the authoring UI.
constexpr double kMinimumBpm = 5.0;
constexpr double kMaximumBpm = 1600.0;
constexpr double kMinimumSampleRate = 8000.0;
constexpr double kMaximumSampleRate = 768000.0;
constexpr uint32_t kMaximumTicksPerBeat = 64u;
constexpr uint32_t kMaximumWarpCycleTicks = 1024u;
constexpr uint32_t kMaximumLoopRow = 65535u;
constexpr double kMaximumTimingMilliseconds = 500.0;

float normalizedVelocity(float value) noexcept
{
    if (!std::isfinite(value)) return 0.787f;
    return std::clamp(value, 0.0f, 1.0f);
}

float normalizedParameter(float value) noexcept
{
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

float normalizedVelocityScale(float value) noexcept
{
    if (!std::isfinite(value)) return 1.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

bool parseMidiNote(std::string_view text, uint8_t& note) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1u);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1u);
    if (text.empty()) return false;

    int numeric = 0;
    const auto numericResult = std::from_chars(text.data(),
        text.data() + text.size(), numeric);
    if (numericResult.ec == std::errc {}
        && numericResult.ptr == text.data() + text.size()) {
        if (numeric < 0 || numeric > 127) return false;
        note = static_cast<uint8_t>(numeric);
        return true;
    }

    const auto uppercase = [](char character) noexcept {
        return character >= 'a' && character <= 'z'
            ? static_cast<char>(character - 'a' + 'A') : character;
    };
    int semitone = 0;
    switch (uppercase(text.front())) {
    case 'C': semitone = 0; break;
    case 'D': semitone = 2; break;
    case 'E': semitone = 4; break;
    case 'F': semitone = 5; break;
    case 'G': semitone = 7; break;
    case 'A': semitone = 9; break;
    case 'B': semitone = 11; break;
    default: return false;
    }
    std::size_t cursor = 1u;
    if (cursor < text.size() && text[cursor] == '#') {
        ++semitone;
        ++cursor;
    } else if (cursor < text.size() && uppercase(text[cursor]) == 'B') {
        --semitone;
        ++cursor;
    } else if (cursor < text.size() && text[cursor] == '-') {
        ++cursor;
    }
    if (cursor >= text.size()) return false;
    int octave = 0;
    const auto octaveResult = std::from_chars(text.data() + cursor,
        text.data() + text.size(), octave);
    if (octaveResult.ec != std::errc {}
        || octaveResult.ptr != text.data() + text.size()) return false;
    const int midi = (octave + 1) * 12 + semitone;
    if (midi < 0 || midi > 127) return false;
    note = static_cast<uint8_t>(midi);
    return true;
}

EventDestination destinationForInstrument(uint32_t nodeId,
    EventDestination fallback) noexcept
{
    const auto* instrument = defaultRackInstrument(nodeId);
    if (!instrument) return fallback;
    if (instrumentRoutesToMidi(instrument->kind))
        return EventDestination::Midi;
    if (instrumentRoutesToInternal(instrument->kind))
        return EventDestination::Internal;
    return EventDestination::None;
}

uint8_t midiVelocityFromNormalized(float normalized) noexcept
{
    const auto scaled = static_cast<int>(std::lround(
        normalizedVelocity(normalized) * 127.0f));
    return static_cast<uint8_t>(std::clamp(scaled, 1, 127));
}

void Sequencer::setPattern(Pattern pattern)
{
    preparedPatterns_.clear();
    activePreparedPatternIndex_ = 0u;
    pattern_ = std::move(pattern);
    normalizePattern(pattern_);
    reset();
}

bool Sequencer::preparePatternSet(std::vector<Pattern> patterns,
    std::size_t initialPatternIndex)
{
    if (playing_ || patterns.empty()
        || initialPatternIndex >= patterns.size()) return false;

    std::size_t maximumTracks = 0u;
    std::array<std::size_t, kMaximumTrackCount> maximumNoteLengths {};
    for (auto& pattern : patterns) {
        if (pattern.tracks.size() > kMaximumTrackCount) return false;
        normalizePattern(pattern);
        maximumTracks = std::max(maximumTracks, pattern.tracks.size());
        for (std::size_t track = 0u; track < pattern.tracks.size(); ++track) {
            maximumNoteLengths[track] = std::max(
                maximumNoteLengths[track],
                activeLength(pattern.tracks[track].noteColumn,
                    pattern.tracks[track].notes.size()));
        }
    }

    preparedPatterns_ = std::move(patterns);
    pattern_ = std::move(preparedPatterns_[initialPatternIndex]);
    preparedPatterns_[initialPatternIndex] = {};
    activePreparedPatternIndex_ = initialPatternIndex;
    playback_.assign(maximumTracks, {});
    for (std::size_t track = 0u; track < maximumTracks; ++track)
        playback_[track].skipCounters.reserve(maximumNoteLengths[track]);
    reset();
    return true;
}

void Sequencer::captureRemovedTrackReleases(
    std::size_t retainedTracks, const Pattern& previousPattern) noexcept
{
    const auto previousTracks = std::min(previousPattern.tracks.size(),
        playback_.size());
    for (std::size_t track = retainedTracks; track < previousTracks;
         ++track) {
        const auto& memory = playback_[track].memory;
        if (memory.activeNodeId == kInvalidInstrumentNode
            || memory.activeDestination == EventDestination::None
            || memory.noteId == 0u) continue;
        if (pendingBoundaryReleaseCount_
            >= pendingBoundaryReleases_.size()) {
            ++droppedEventCount_;
            continue;
        }
        auto& release = pendingBoundaryReleases_[
            pendingBoundaryReleaseCount_++];
        release = {};
        release.noteId = memory.noteId;
        release.track = static_cast<uint32_t>(track);
        release.targetNode = memory.activeNodeId;
        release.chokeGroup = previousPattern.tracks[track].chokeGroup;
        release.normalizedVelocity = memory.activeVelocity;
        release.note = memory.activeNote;
        release.channel = memory.activeChannel;
        release.kind = ScheduledEventKind::NoteOff;
        release.destination = memory.activeDestination;
    }
}

void Sequencer::resetTrackPlaybackState(std::size_t trackIndex,
    const Track& track)
{
    auto counters = std::move(playback_[trackIndex].skipCounters);
    playback_[trackIndex] = {};
    playback_[trackIndex].skipCounters = std::move(counters);
    auto& state = playback_[trackIndex];
    const auto noteLength = activeLength(track.noteColumn, track.notes.size());
    state.skipCounters.resize(noteLength, 0u);
    std::fill(state.skipCounters.begin(), state.skipCounters.end(), 0u);
    state.noteColumn.randomState = columnSeed(trackIndex, 0u);
    state.velocityColumn.randomState = columnSeed(trackIndex, 1u);
    state.instrumentColumn.randomState = columnSeed(trackIndex, 6u);
    state.noteFxRandomState = columnSeed(trackIndex, 7u);
    state.memory.instrumentNodeId = track.initialInstrumentNodeId;
    for (std::size_t fx = 0u; fx < state.fxPairs.size(); ++fx) {
        state.fxPairs[fx].actionColumn.randomState = columnSeed(
            trackIndex, static_cast<uint32_t>(2u + fx * 2u));
        state.fxPairs[fx].valueColumn.randomState = columnSeed(
            trackIndex, static_cast<uint32_t>(3u + fx * 2u));
    }
    const auto initialize = [](const ColumnDefinition& definition,
                                ColumnState& column) {
        const auto length = std::max<std::size_t>(definition.length, 1u);
        column.position = definition.phase % length;
        column.lastPosition = column.position;
    };
    initialize(track.noteColumn, state.noteColumn);
    initialize(track.instrumentColumn, state.instrumentColumn);
    initialize(track.velocityColumn, state.velocityColumn);
    for (std::size_t fx = 0u; fx < track.fxPairs.size(); ++fx) {
        initialize(track.fxPairs[fx].actionColumn,
            state.fxPairs[fx].actionColumn);
        initialize(track.fxPairs[fx].valueColumn,
            state.fxPairs[fx].valueColumn);
    }
}

void Sequencer::transitionPlaybackState(const Pattern& previousPattern,
    const Pattern& nextPattern, std::size_t launchRow,
    bool relaunch) noexcept
{
    const auto retainedTracks = std::min(previousPattern.tracks.size(),
        nextPattern.tracks.size());
    const auto retainPhase = [launchRow, relaunch](
                                 const ColumnDefinition& previous,
                                 const ColumnDefinition& current,
                                 std::size_t activeLength,
                                 ColumnState& state) {
        if (activeLength == 0u) {
            state.position = 0u;
            state.lastPosition = 0u;
            state.pingPongDirection = 1;
            return;
        }
        if (relaunch) {
            state.position = ((launchRow % activeLength)
                + (current.phase % activeLength)) % activeLength;
            state.pingPongDirection = 1;
        } else {
            const auto oldPhase = previous.phase % activeLength;
            const auto newPhase = current.phase % activeLength;
            const auto shift = (newPhase + activeLength - oldPhase)
                % activeLength;
            state.position = (state.position % activeLength + shift)
                % activeLength;
        }
        state.lastPosition %= activeLength;
    };

    for (std::size_t index = 0u; index < retainedTracks; ++index) {
        auto& state = playback_[index];
        const auto& previousTrack = previousPattern.tracks[index];
        const auto& nextTrack = nextPattern.tracks[index];
        if (previousTrack.initialInstrumentNodeId
            != nextTrack.initialInstrumentNodeId) {
            state.memory.instrumentNodeId
                = nextTrack.initialInstrumentNodeId;
            state.releaseActiveForDefaultReassignment
                = state.memory.activeNodeId != kInvalidInstrumentNode
                && state.memory.activeNodeId
                    != nextTrack.initialInstrumentNodeId;
        }
        const auto noteLength = activeLength(nextTrack.noteColumn,
            nextTrack.notes.size());
        state.skipCounters.resize(noteLength, 0u);
        const auto velocityLength = activeLength(nextTrack.velocityColumn,
            nextTrack.velocities.size());
        const auto instrumentLength = activeLength(nextTrack.instrumentColumn,
            nextTrack.instruments.size());
        retainPhase(previousTrack.noteColumn, nextTrack.noteColumn,
            noteLength, state.noteColumn);
        retainPhase(previousTrack.velocityColumn, nextTrack.velocityColumn,
            velocityLength, state.velocityColumn);
        retainPhase(previousTrack.instrumentColumn, nextTrack.instrumentColumn,
            instrumentLength, state.instrumentColumn);
        for (std::size_t fx = 0u; fx < nextTrack.fxPairs.size(); ++fx) {
            const auto actionLength = activeLength(
                nextTrack.fxPairs[fx].actionColumn,
                nextTrack.fxPairs[fx].actions.size());
            const auto valueLength = activeLength(
                nextTrack.fxPairs[fx].valueColumn,
                nextTrack.fxPairs[fx].values.size());
            retainPhase(previousTrack.fxPairs[fx].actionColumn,
                nextTrack.fxPairs[fx].actionColumn, actionLength,
                state.fxPairs[fx].actionColumn);
            retainPhase(previousTrack.fxPairs[fx].valueColumn,
                nextTrack.fxPairs[fx].valueColumn, valueLength,
                state.fxPairs[fx].valueColumn);
        }
    }
    for (std::size_t index = retainedTracks;
         index < nextPattern.tracks.size(); ++index)
        resetTrackPlaybackState(index, nextPattern.tracks[index]);
    for (std::size_t index = nextPattern.tracks.size();
         index < playback_.size(); ++index) {
        auto counters = std::move(playback_[index].skipCounters);
        playback_[index] = {};
        playback_[index].skipCounters = std::move(counters);
        playback_[index].skipCounters.clear();
    }
}

bool Sequencer::activatePreparedPatternAtTickBoundary(
    std::size_t patternIndex) noexcept
{
    if (preparedPatterns_.empty()
        || patternIndex >= preparedPatterns_.size()) return false;
    if (patternIndex == activePreparedPatternIndex_) return true;
    const auto& target = preparedPatterns_[patternIndex];
    if (target.tracks.size() > playback_.size()) return false;
    for (std::size_t track = 0u; track < target.tracks.size(); ++track) {
        const auto noteLength = activeLength(target.tracks[track].noteColumn,
            target.tracks[track].notes.size());
        if (playback_[track].skipCounters.capacity() < noteLength)
            return false;
    }

    const auto previousIndex = activePreparedPatternIndex_;
    const auto retainedTracks = std::min(pattern_.tracks.size(),
        target.tracks.size());
    captureRemovedTrackReleases(retainedTracks, pattern_);
    std::swap(pattern_, preparedPatterns_[patternIndex]);
    std::swap(preparedPatterns_[patternIndex],
        preparedPatterns_[previousIndex]);
    activePreparedPatternIndex_ = patternIndex;
    transitionPlaybackState(preparedPatterns_[previousIndex], pattern_, 0u,
        false);
    return true;
}

void Sequencer::replacePattern(Pattern pattern)
{
    const auto retainedBeforeReplacement = std::min(pattern_.tracks.size(),
        pattern.tracks.size());
    captureRemovedTrackReleases(retainedBeforeReplacement, pattern_);
    auto previousPlayback = std::move(playback_);
    auto previousPattern = std::move(pattern_);
    preparedPatterns_.clear();
    activePreparedPatternIndex_ = 0u;
    pattern_ = std::move(pattern);
    normalizePattern(pattern_);
    playback_.assign(pattern_.tracks.size(), {});

    const auto retainedTracks = std::min(previousPlayback.size(),
        playback_.size());
    for (std::size_t index = 0u; index < retainedTracks; ++index) {
        playback_[index] = std::move(previousPlayback[index]);
        const auto retainPhase = [](const ColumnDefinition& previous,
                                     const ColumnDefinition& current,
                                     std::size_t activeLength,
                                     ColumnState& state) {
            if (activeLength == 0u) {
                state.position = 0u;
                state.lastPosition = 0u;
                state.pingPongDirection = 1;
                return;
            }
            const auto oldPhase = previous.phase % activeLength;
            const auto newPhase = current.phase % activeLength;
            const auto shift = (newPhase + activeLength - oldPhase)
                % activeLength;
            state.position = (state.position % activeLength + shift)
                % activeLength;
            // lastPosition describes a cell that was actually rendered. A
            // control-side phase edit moves only the next read head; the last
            // playhead catches up naturally on the following tick.
            state.lastPosition %= activeLength;
        };
        if (index < previousPattern.tracks.size()
            && previousPattern.tracks[index].initialInstrumentNodeId
                != pattern_.tracks[index].initialInstrumentNodeId) {
            // A deliberate lane-default edit supersedes row memory. The
            // active-node identity remains separate, so a subsequent release
            // still reaches the slot that owns the sounding note.
            playback_[index].memory.instrumentNodeId
                = pattern_.tracks[index].initialInstrumentNodeId;
            playback_[index].releaseActiveForDefaultReassignment
                = playback_[index].memory.activeNodeId
                        != kInvalidInstrumentNode
                    && playback_[index].memory.activeNodeId
                        != pattern_.tracks[index].initialInstrumentNodeId;
        }
        const auto noteLength = activeLength(pattern_.tracks[index].noteColumn,
            pattern_.tracks[index].notes.size());
        playback_[index].skipCounters.resize(noteLength, 0u);
        const auto velocityLength = activeLength(
            pattern_.tracks[index].velocityColumn,
            pattern_.tracks[index].velocities.size());
        const auto instrumentLength = activeLength(
            pattern_.tracks[index].instrumentColumn,
            pattern_.tracks[index].instruments.size());
        const auto& previousTrack = previousPattern.tracks[index];
        const auto& currentTrack = pattern_.tracks[index];
        retainPhase(previousTrack.noteColumn, currentTrack.noteColumn,
            noteLength, playback_[index].noteColumn);
        retainPhase(previousTrack.velocityColumn, currentTrack.velocityColumn,
            velocityLength, playback_[index].velocityColumn);
        retainPhase(previousTrack.instrumentColumn,
            currentTrack.instrumentColumn, instrumentLength,
            playback_[index].instrumentColumn);
        const auto& fxPairs = pattern_.tracks[index].fxPairs;
        for (std::size_t fx = 0u; fx < fxPairs.size(); ++fx) {
            const auto actionLength = activeLength(
                fxPairs[fx].actionColumn, fxPairs[fx].actions.size());
            const auto valueLength = activeLength(
                fxPairs[fx].valueColumn, fxPairs[fx].values.size());
            auto& fxState = playback_[index].fxPairs[fx];
            retainPhase(previousTrack.fxPairs[fx].actionColumn,
                fxPairs[fx].actionColumn, actionLength,
                fxState.actionColumn);
            retainPhase(previousTrack.fxPairs[fx].valueColumn,
                fxPairs[fx].valueColumn, valueLength,
                fxState.valueColumn);
        }
    }
    for (std::size_t index = retainedTracks; index < playback_.size();
         ++index) {
        playback_[index].noteColumn.randomState = columnSeed(index, 0u);
        playback_[index].velocityColumn.randomState = columnSeed(index, 1u);
        playback_[index].instrumentColumn.randomState = columnSeed(index, 6u);
        playback_[index].noteFxRandomState = columnSeed(index, 7u);
        playback_[index].skipCounters.assign(activeLength(
            pattern_.tracks[index].noteColumn,
            pattern_.tracks[index].notes.size()), 0u);
        playback_[index].memory.instrumentNodeId
            = pattern_.tracks[index].initialInstrumentNodeId;
        for (std::size_t fx = 0u;
             fx < playback_[index].fxPairs.size(); ++fx) {
            playback_[index].fxPairs[fx].actionColumn.randomState = columnSeed(
                index, static_cast<uint32_t>(2u + fx * 2u));
            playback_[index].fxPairs[fx].valueColumn.randomState = columnSeed(
                index, static_cast<uint32_t>(3u + fx * 2u));
        }
    }
    for (std::size_t index = retainedTracks; index < playback_.size();
         ++index) {
        auto& state = playback_[index];
        const auto& track = pattern_.tracks[index];
        const auto initialize = [](const ColumnDefinition& definition,
                                    ColumnState& column) {
            const auto length = std::max<std::size_t>(definition.length, 1u);
            column.position = definition.phase % length;
            column.lastPosition = column.position;
        };
        initialize(track.noteColumn, state.noteColumn);
        initialize(track.instrumentColumn, state.instrumentColumn);
        initialize(track.velocityColumn, state.velocityColumn);
        for (std::size_t fx = 0u; fx < track.fxPairs.size(); ++fx) {
            initialize(track.fxPairs[fx].actionColumn,
                state.fxPairs[fx].actionColumn);
            initialize(track.fxPairs[fx].valueColumn,
                state.fxPairs[fx].valueColumn);
        }
    }
}

TransportSettings Sequencer::normalizedTransport(
    TransportSettings settings) noexcept
{
    if (!std::isfinite(settings.sampleRate)) settings.sampleRate = 48000.0;
    if (!std::isfinite(settings.bpm)) settings.bpm = 120.0;
    if (!std::isfinite(settings.swing)) settings.swing = 0.5;
    if (!std::isfinite(settings.timingLookaheadMilliseconds))
        settings.timingLookaheadMilliseconds = 25.0;
    if (!std::isfinite(settings.microTimingRangeMilliseconds))
        settings.microTimingRangeMilliseconds = 25.0;
    settings.sampleRate = std::clamp(settings.sampleRate,
        kMinimumSampleRate, kMaximumSampleRate);
    settings.bpm = std::clamp(settings.bpm, kMinimumBpm, kMaximumBpm);
    settings.ticksPerBeat = std::clamp(settings.ticksPerBeat, 1u,
        kMaximumTicksPerBeat);
    settings.swing = std::clamp(settings.swing, 0.5, 0.75);
    settings.microTimingRangeMilliseconds = std::clamp(
        settings.microTimingRangeMilliseconds, 0.0,
        kMaximumTimingMilliseconds);
    settings.timingLookaheadMilliseconds = std::clamp(
        settings.timingLookaheadMilliseconds,
        settings.microTimingRangeMilliseconds,
        kMaximumTimingMilliseconds);
    settings.warpCycleTicks = std::clamp(settings.warpCycleTicks, 1u,
        kMaximumWarpCycleTicks);
    settings.loopStartRow = std::min(settings.loopStartRow, kMaximumLoopRow);
    settings.loopEndRow = std::clamp(settings.loopEndRow,
        settings.loopStartRow + 1u, kMaximumLoopRow + 1u);
    return settings;
}

void Sequencer::setTransport(TransportSettings settings)
{
    transport_ = normalizedTransport(std::move(settings));
}

void Sequencer::setTransportAtTickBoundary(
    TransportSettings settings) noexcept
{
    settings = normalizedTransport(std::move(settings));
    if (tickIndex_ == 0u) {
        transport_ = settings;
        return;
    }
    const uint64_t completedTick = tickIndex_ - 1u;
    const long double previousInterval = tickInterval(
        transport_, completedTick);
    const long double replacementInterval = tickInterval(
        settings, completedTick);
    nextTickFrame_ += replacementInterval - previousInterval;
    transport_ = settings;
}

void Sequencer::start(bool resetPosition)
{
    if (resetPosition) {
        reset();
    } else {
        // Resume preserves musical phase but establishes a fresh clock origin.
        nextTickFrame_ = 0.0L;
        renderedFrameCount_ = 0u;
        droppedEventCount_ = 0u;
    }
    playing_ = true;
}

bool Sequencer::startPreparedAtHostBeat(double hostBeat) noexcept
{
    if (preparedPatterns_.empty() || !std::isfinite(hostBeat)) return false;
    for (std::size_t track = 0u; track < pattern_.tracks.size(); ++track) {
        const auto length = activeLength(pattern_.tracks[track].noteColumn,
            pattern_.tracks[track].notes.size());
        if (playback_[track].skipCounters.capacity() < length) return false;
    }

    reset();
    const long double straightPosition = std::max(0.0L,
        static_cast<long double>(hostBeat)
            * static_cast<long double>(transport_.ticksPerBeat));
    const auto cycle = static_cast<uint64_t>(std::max<uint32_t>(
        transport_.warpCycleTicks, 1u));
    const auto approximate = static_cast<uint64_t>(std::floor(
        straightPosition));
    uint64_t tick = approximate > cycle + 2u
        ? approximate - cycle - 2u : 0u;
    const uint64_t searchLimit = approximate >
            std::numeric_limits<uint64_t>::max() - cycle * 2u - 8u
        ? std::numeric_limits<uint64_t>::max()
        : approximate + cycle * 2u + 8u;
    long double mappedPosition = 0.0L;
    for (;;) {
        mappedPosition = warpedTickPhase(transport_, tick)
            * static_cast<long double>(cycle);
        if (mappedPosition + 1.0e-9L >= straightPosition
            || tick >= searchLimit) break;
        ++tick;
    }

    const long double straightFrames =
        static_cast<long double>(transport_.sampleRate) * 60.0L
        / (static_cast<long double>(transport_.bpm)
            * static_cast<long double>(transport_.ticksPerBeat));
    nextTickFrame_ = std::max(0.0L,
        (mappedPosition - straightPosition) * straightFrames);
    renderedFrameCount_ = 0u;
    tickIndex_ = tick;
    uint64_t row = tick;
    if (transport_.loopEnabled && row >= transport_.loopEndRow) {
        const uint64_t loopLength = transport_.loopEndRow
            - transport_.loopStartRow;
        row = transport_.loopStartRow
            + (row - transport_.loopStartRow) % loopLength;
    }
    transportRow_ = row;
    seekAllColumns(static_cast<std::size_t>(row));
    playing_ = true;
    return true;
}

void Sequencer::stop()
{
    playing_ = false;
}

void Sequencer::reset()
{
    playing_ = false;
    nextTickFrame_ = 0.0L;
    renderedFrameCount_ = 0u;
    tickIndex_ = 0u;
    transportRow_ = 0u;
    droppedEventCount_ = 0u;
    nextNoteId_ = 1u;
    pendingBoundaryReleaseCount_ = 0u;
    if (preparedPatterns_.empty()) {
        playback_.assign(pattern_.tracks.size(), {});
    } else {
        for (auto& state : playback_) {
            auto counters = std::move(state.skipCounters);
            state = {};
            state.skipCounters = std::move(counters);
            state.skipCounters.clear();
        }
    }
    for (std::size_t index = 0u; index < pattern_.tracks.size(); ++index)
        resetTrackPlaybackState(index, pattern_.tracks[index]);
    seekAllColumns(0u);
}

void Sequencer::setRandomSeed(uint32_t seed)
{
    randomSeed_ = seed == 0u ? 0x6d2b79f5u : seed;
    for (std::size_t index = 0u; index < pattern_.tracks.size(); ++index) {
        playback_[index].noteColumn.randomState = columnSeed(index, 0u);
        playback_[index].velocityColumn.randomState = columnSeed(index, 1u);
        playback_[index].instrumentColumn.randomState = columnSeed(index, 6u);
        playback_[index].noteFxRandomState = columnSeed(index, 7u);
        std::fill(playback_[index].skipCounters.begin(),
            playback_[index].skipCounters.end(), 0u);
        playback_[index].memory.hasLastEmitted = false;
        for (std::size_t fx = 0u;
             fx < playback_[index].fxPairs.size(); ++fx) {
            playback_[index].fxPairs[fx].actionColumn.randomState = columnSeed(
                index, static_cast<uint32_t>(2u + fx * 2u));
            playback_[index].fxPairs[fx].valueColumn.randomState = columnSeed(
                index, static_cast<uint32_t>(3u + fx * 2u));
        }
    }
}

std::size_t Sequencer::process(uint32_t frameCount, ScheduledEvent* output,
    std::size_t outputCapacity) noexcept
{
    if (!playing_ || frameCount == 0u) return 0u;
    if (!output) outputCapacity = 0u;

    std::size_t outputCount = 0u;
    const uint64_t blockStart = renderedFrameCount_;
    const uint64_t blockEnd = blockStart + static_cast<uint64_t>(frameCount);
    while (true) {
        const uint64_t eventFrame = nextTickSampleFrame();
        if (eventFrame >= blockEnd) break;
        if (transport_.loopEnabled
            && transportRow_ >= transport_.loopEndRow) {
            transportRow_ = transport_.loopStartRow;
            seekAllColumns(static_cast<std::size_t>(transportRow_));
        }
        const auto relativeFrame = eventFrame > blockStart
            ? eventFrame - blockStart : 0u;
        const auto frameOffset = static_cast<uint32_t>(relativeFrame);
        emitTick(eventFrame, frameOffset, output, outputCapacity, outputCount);
        nextTickFrame_ += nextTickInterval();
        ++tickIndex_;
        ++transportRow_;
    }
    renderedFrameCount_ = blockEnd;
    return outputCount;
}

std::size_t Sequencer::processSingleTick(uint32_t frameCount,
    ScheduledEvent* output, std::size_t outputCapacity) noexcept
{
    if (!playing_ || frameCount == 0u) return 0u;
    if (!output) outputCapacity = 0u;
    const uint64_t blockStart = renderedFrameCount_;
    const uint64_t blockEnd = blockStart + static_cast<uint64_t>(frameCount);
    const uint64_t eventFrame = nextTickSampleFrame();
    if (eventFrame >= blockEnd) {
        renderedFrameCount_ = blockEnd;
        return 0u;
    }
    if (transport_.loopEnabled
        && transportRow_ >= transport_.loopEndRow) {
        transportRow_ = transport_.loopStartRow;
        seekAllColumns(static_cast<std::size_t>(transportRow_));
    }
    const auto relativeFrame = eventFrame > blockStart
        ? eventFrame - blockStart : 0u;
    const auto frameOffset = static_cast<uint32_t>(relativeFrame);
    std::size_t outputCount = 0u;
    emitTick(eventFrame, frameOffset, output, outputCapacity, outputCount);
    nextTickFrame_ += nextTickInterval();
    ++tickIndex_;
    ++transportRow_;
    return outputCount;
}

void Sequencer::advanceRenderClockWithoutTickGeneration(
    uint32_t frameCount) noexcept
{
    const uint64_t remaining = std::numeric_limits<uint64_t>::max()
        - renderedFrameCount_;
    renderedFrameCount_ += std::min<uint64_t>(remaining, frameCount);
}

void Sequencer::seekAllColumns(std::size_t row) noexcept
{
    for (std::size_t trackIndex = 0u;
         trackIndex < pattern_.tracks.size();
         ++trackIndex) {
        auto& state = playback_[trackIndex];
        const auto& track = pattern_.tracks[trackIndex];
        const auto seek = [row](const ColumnDefinition& definition,
                              ColumnState& column) {
            const auto length = std::max<std::size_t>(definition.length, 1u);
            column.position = ((row % length)
                + (definition.phase % length)) % length;
            column.lastPosition = column.position;
            column.pingPongDirection = 1;
        };
        seek(track.noteColumn, state.noteColumn);
        seek(track.instrumentColumn, state.instrumentColumn);
        seek(track.velocityColumn, state.velocityColumn);
        for (std::size_t pair = 0u; pair < track.fxPairs.size(); ++pair) {
            seek(track.fxPairs[pair].actionColumn,
                state.fxPairs[pair].actionColumn);
            seek(track.fxPairs[pair].valueColumn,
                state.fxPairs[pair].valueColumn);
        }
    }
}

void Sequencer::relaunchColumnsAtTickBoundary(std::size_t row) noexcept
{
    for (std::size_t trackIndex = 0u;
         trackIndex < pattern_.tracks.size();
         ++trackIndex) {
        auto& state = playback_[trackIndex];
        const auto& track = pattern_.tracks[trackIndex];
        const auto relaunch = [row](const ColumnDefinition& definition,
                                   ColumnState& column) {
            const auto length = std::max<std::size_t>(definition.length, 1u);
            column.position = ((row % length)
                + (definition.phase % length)) % length;
            column.pingPongDirection = 1;
            // lastPosition remains the last cell actually rendered. A Song
            // launch changes only the next read head.
        };
        relaunch(track.noteColumn, state.noteColumn);
        relaunch(track.instrumentColumn, state.instrumentColumn);
        relaunch(track.velocityColumn, state.velocityColumn);
        for (std::size_t pair = 0u; pair < track.fxPairs.size(); ++pair) {
            relaunch(track.fxPairs[pair].actionColumn,
                state.fxPairs[pair].actionColumn);
            relaunch(track.fxPairs[pair].valueColumn,
                state.fxPairs[pair].valueColumn);
        }
    }
}

bool Sequencer::resyncTrackColumnsAtTickBoundary(std::size_t trackIndex,
    std::size_t row) noexcept
{
    if (trackIndex >= pattern_.tracks.size()
        || trackIndex >= playback_.size()) return false;
    auto& state = playback_[trackIndex];
    const auto& track = pattern_.tracks[trackIndex];
    const auto resync = [row](const ColumnDefinition& definition,
                              ColumnState& column) {
        const auto length = std::max<std::size_t>(definition.length, 1u);
        column.position = row % length;
        column.pingPongDirection = 1;
        // lastPosition remains the last cell actually rendered. A resync
        // changes only the next read head.
    };
    resync(track.noteColumn, state.noteColumn);
    resync(track.instrumentColumn, state.instrumentColumn);
    resync(track.velocityColumn, state.velocityColumn);
    for (std::size_t pair = 0u; pair < track.fxPairs.size(); ++pair) {
        resync(track.fxPairs[pair].actionColumn,
            state.fxPairs[pair].actionColumn);
        resync(track.fxPairs[pair].valueColumn,
            state.fxPairs[pair].valueColumn);
    }
    return true;
}

void Sequencer::resyncAllTrackColumnsAtTickBoundary(
    std::size_t row) noexcept
{
    for (std::size_t track = 0u; track < pattern_.tracks.size(); ++track)
        (void)resyncTrackColumnsAtTickBoundary(track, row);
}

uint64_t Sequencer::nextTickSampleFrame() const noexcept
{
    const long double rounded = std::floor(nextTickFrame_ + 0.5L);
    return rounded <= 0.0L ? 0u
        : rounded >= static_cast<long double>(
              std::numeric_limits<uint64_t>::max())
            ? std::numeric_limits<uint64_t>::max()
            : static_cast<uint64_t>(rounded);
}

std::size_t Sequencer::notePosition(std::size_t track) const noexcept
{
    return track < pattern_.tracks.size()
        ? playback_[track].noteColumn.position : 0u;
}

std::size_t Sequencer::velocityPosition(std::size_t track) const noexcept
{
    return track < pattern_.tracks.size()
        ? playback_[track].velocityColumn.position : 0u;
}

std::size_t Sequencer::instrumentPosition(std::size_t track) const noexcept
{
    return track < pattern_.tracks.size()
        ? playback_[track].instrumentColumn.position : 0u;
}

std::size_t Sequencer::lastNotePosition(std::size_t track) const noexcept
{
    return track < pattern_.tracks.size()
        ? playback_[track].noteColumn.lastPosition : 0u;
}

bool Sequencer::lastNoteTriggered(std::size_t track) const noexcept
{
    return track < pattern_.tracks.size()
        && playback_[track].lastNoteTriggered;
}

std::size_t Sequencer::lastVelocityPosition(std::size_t track) const noexcept
{
    return track < pattern_.tracks.size()
        ? playback_[track].velocityColumn.lastPosition : 0u;
}

std::size_t Sequencer::lastInstrumentPosition(
    std::size_t track) const noexcept
{
    return track < pattern_.tracks.size()
        ? playback_[track].instrumentColumn.lastPosition : 0u;
}

std::size_t Sequencer::fxActionPosition(std::size_t track,
    std::size_t pair) const noexcept
{
    return track < pattern_.tracks.size()
            && pair < playback_[track].fxPairs.size()
        ? playback_[track].fxPairs[pair].actionColumn.position : 0u;
}

std::size_t Sequencer::fxValuePosition(std::size_t track,
    std::size_t pair) const noexcept
{
    return track < pattern_.tracks.size()
            && pair < playback_[track].fxPairs.size()
        ? playback_[track].fxPairs[pair].valueColumn.position : 0u;
}

std::size_t Sequencer::lastFxActionPosition(std::size_t track,
    std::size_t pair) const noexcept
{
    return track < pattern_.tracks.size()
            && pair < playback_[track].fxPairs.size()
        ? playback_[track].fxPairs[pair].actionColumn.lastPosition : 0u;
}

std::size_t Sequencer::lastFxValuePosition(std::size_t track,
    std::size_t pair) const noexcept
{
    return track < pattern_.tracks.size()
            && pair < playback_[track].fxPairs.size()
        ? playback_[track].fxPairs[pair].valueColumn.lastPosition : 0u;
}

FxPlaybackMemorySnapshot Sequencer::fxMemorySnapshot(std::size_t track,
    std::size_t pair) const noexcept
{
    if (track >= pattern_.tracks.size()
        || pair >= playback_[track].fxPairs.size()) return {};
    const auto& state = playback_[track].fxPairs[pair];
    return { state.action, state.value, state.hasAction, state.hasValue };
}

void Sequencer::normalizePattern(Pattern& pattern)
{
    pattern.visibleRows = std::max<std::size_t>(pattern.visibleRows, 1u);
    for (auto& track : pattern.tracks) {
        track.velocityScale = normalizedVelocityScale(track.velocityScale);
        track.midiChannel = static_cast<uint8_t>(std::clamp<int>(
            track.midiChannel, 1, 16));
        if (track.initialInstrumentNodeId >= kInstrumentRackSlotCount)
            track.initialInstrumentNodeId = kInvalidInstrumentNode;
        track.noteColumn.stride = std::max(track.noteColumn.stride, 1u);
        track.instrumentColumn.stride = std::max(
            track.instrumentColumn.stride, 1u);
        track.velocityColumn.stride = std::max(
            track.velocityColumn.stride, 1u);
        track.noteColumn.length = activeLength(track.noteColumn,
            track.notes.size());
        track.instrumentColumn.length = activeLength(track.instrumentColumn,
            track.instruments.size());
        track.velocityColumn.length = activeLength(track.velocityColumn,
            track.velocities.size());
        track.noteColumn.phase = track.noteColumn.length == 0u ? 0u
            : track.noteColumn.phase % track.noteColumn.length;
        track.instrumentColumn.phase = track.instrumentColumn.length == 0u
            ? 0u : track.instrumentColumn.phase
                % track.instrumentColumn.length;
        track.velocityColumn.phase = track.velocityColumn.length == 0u ? 0u
            : track.velocityColumn.phase % track.velocityColumn.length;
        for (auto& cell : track.instruments) {
            if (cell.state == InstrumentCellState::Instrument
                && cell.nodeId < kInstrumentRackSlotCount) continue;
            if (cell.state != InstrumentCellState::Empty
                && cell.state != InstrumentCellState::Previous) {
                cell.state = InstrumentCellState::Empty;
            }
            cell.nodeId = kInvalidInstrumentNode;
        }
        for (auto& fx : track.fxPairs) {
            fx.actionColumn.stride = std::max(
                fx.actionColumn.stride, 1u);
            fx.valueColumn.stride = std::max(
                fx.valueColumn.stride, 1u);
            fx.actionColumn.length = activeLength(fx.actionColumn,
                fx.actions.size());
            fx.valueColumn.length = activeLength(fx.valueColumn,
                fx.values.size());
            fx.actionColumn.phase = fx.actionColumn.length == 0u ? 0u
                : fx.actionColumn.phase % fx.actionColumn.length;
            fx.valueColumn.phase = fx.valueColumn.length == 0u ? 0u
                : fx.valueColumn.phase % fx.valueColumn.length;
            for (auto& cell : fx.actions) {
                if (cell.state != FxActionCellState::Sequencer) continue;
                if (static_cast<std::size_t>(cell.sequencerAction)
                    >= kSequencerActionCount) {
                    cell = FxActionCell::empty();
                }
            }
        }
    }
}

void Sequencer::emitTick(uint64_t absoluteSampleTime, uint32_t frameOffset,
    ScheduledEvent* output, std::size_t outputCapacity,
    std::size_t& outputCount) noexcept
{
    // Pattern contraction cannot leave a lane around to emit its own release.
    // Deliver the captured true owner before any target-pattern event at this
    // same logical tick so MIDI and internal fanout observe one ordered bundle.
    for (std::size_t index = 0u; index < pendingBoundaryReleaseCount_;
         ++index) {
        auto release = pendingBoundaryReleases_[index];
        release.absoluteSampleTime = absoluteSampleTime;
        release.frameOffset = frameOffset;
        if (release.destination == EventDestination::None) continue;
        if (outputCount < outputCapacity) {
            output[outputCount++] = release;
        } else {
            ++droppedEventCount_;
        }
    }
    pendingBoundaryReleaseCount_ = 0u;

    for (std::size_t trackIndex = 0u; trackIndex < pattern_.tracks.size();
         ++trackIndex) {
        auto& track = pattern_.tracks[trackIndex];
        auto& state = playback_[trackIndex];
        auto& memory = state.memory;
        const auto noteLength = activeLength(track.noteColumn,
            track.notes.size());
        const auto velocityLength = activeLength(track.velocityColumn,
            track.velocities.size());
        const auto instrumentLength = activeLength(track.instrumentColumn,
            track.instruments.size());
        state.noteColumn.lastPosition = state.noteColumn.position;
        state.instrumentColumn.lastPosition = state.instrumentColumn.position;
        state.velocityColumn.lastPosition = state.velocityColumn.position;

        struct CandidateNote {
            uint8_t note = 0u;
            uint8_t channel = 1u;
            float velocity = 0.787f;
            uint32_t nodeId = kInvalidInstrumentNode;
            EventDestination destination = EventDestination::None;
            std::size_t sourceRow = 0u;
            bool trigger = false;
            bool retrigger = false;
            bool hardRelease = false;
            bool hold = false;
        } candidate;
        candidate.channel = track.midiChannel;
        candidate.sourceRow = noteLength > 0u
            ? state.noteColumn.position % noteLength : 0u;

        // Instrument memory resolves before any other lane-relative operation
        // on this tick. Empty and Previous are distinct authoring states with
        // the same playback result: retain the remembered node.
        if (!track.instrumentColumn.muted && instrumentLength > 0u) {
            const auto& cell = track.instruments[
                state.instrumentColumn.position % instrumentLength];
            if (cell.state == InstrumentCellState::Instrument)
                memory.instrumentNodeId = cell.nodeId;
        }

        if (!track.velocityColumn.muted && velocityLength > 0u) {
            const auto& cell = track.velocities[state.velocityColumn.position
                % velocityLength];
            if (cell.state == ValueCellState::Value)
                memory.velocity = normalizedVelocity(cell.normalized);
            else if (cell.state == ValueCellState::Default)
                memory.velocity = 0.787f;
        }
        candidate.velocity = memory.velocity;
        candidate.nodeId = memory.instrumentNodeId;
        candidate.destination = destinationForInstrument(
            candidate.nodeId, track.destination);

        const bool runtimeMuted = trackIndex < 32u
            && (runtimeTrackMuteMask_
                & (uint32_t { 1u } << static_cast<uint32_t>(trackIndex)))
                != 0u;
        if (track.noteColumn.muted || runtimeMuted) {
            candidate.hardRelease = true;
        } else if (noteLength > 0u) {
            const auto& cell = track.notes[state.noteColumn.position
                % noteLength];
            if (cell.state == NoteCellState::Note) {
                candidate.note = static_cast<uint8_t>(std::min<int>(
                    cell.note, 127));
                candidate.trigger = true;
            } else if (cell.state == NoteCellState::RetriggerPrevious) {
                candidate.trigger = memory.hasNote && !memory.noteMuted;
                candidate.retrigger = candidate.trigger;
                candidate.note = memory.note;
            } else if (cell.state == NoteCellState::Kill) {
                candidate.hardRelease = true;
            } else if (cell.state == NoteCellState::Hold) {
                candidate.hold = true;
            }
        }

        const auto writeScheduled = [&](ScheduledEvent event) {
            if (event.destination == EventDestination::None) return;
            if (outputCount < outputCapacity) {
                output[outputCount++] = event;
            } else {
                ++droppedEventCount_;
            }
        };

        const auto writeNote = [&](ScheduledEventKind kind,
                                   uint64_t noteId,
                                   uint32_t targetNode,
                                   uint8_t note,
                                   uint8_t channel,
                                   float velocity,
                                   EventDestination destination,
                                   uint64_t durationSamples = 0u) {
            ScheduledEvent event;
            event.absoluteSampleTime = absoluteSampleTime;
            event.noteId = noteId;
            event.durationSamples = durationSamples;
            event.frameOffset = frameOffset;
            event.track = static_cast<uint32_t>(trackIndex);
            event.targetNode = targetNode;
            event.chokeGroup = track.chokeGroup;
            event.normalizedVelocity = kind == ScheduledEventKind::NoteOn
                ? normalizedVelocity(velocity * track.velocityScale)
                : velocity;
            event.note = note;
            event.channel = channel;
            event.kind = kind;
            event.destination = destination;
            writeScheduled(event);
        };

        struct PendingFxEvent {
            FxActionCell action;
            float value = 0.0f;
            bool execute = false;
            bool trackRelativeTarget = false;
        };
        std::array<PendingFxEvent, kFxPairCount> pendingFx {};
        for (std::size_t fx = 0u; fx < track.fxPairs.size(); ++fx) {
            auto& definition = track.fxPairs[fx];
            auto& fxState = state.fxPairs[fx];
            const auto actionLength = activeLength(definition.actionColumn,
                definition.actions.size());
            const auto valueLength = activeLength(definition.valueColumn,
                definition.values.size());
            fxState.actionColumn.lastPosition = fxState.actionColumn.position;
            fxState.valueColumn.lastPosition = fxState.valueColumn.position;

            if (!definition.valueColumn.muted && valueLength > 0u) {
                const auto& valueCell = definition.values[
                    fxState.valueColumn.position % valueLength];
                if (valueCell.state == FxValueCellState::Value) {
                    fxState.value = normalizedParameter(valueCell.normalized);
                    fxState.hasValue = true;
                }
            }
            if (!definition.actionColumn.muted && actionLength > 0u) {
                const auto& actionCell = definition.actions[
                    fxState.actionColumn.position % actionLength];
                if (actionCell.state == FxActionCellState::Parameter
                    || actionCell.state == FxActionCellState::Sequencer) {
                    fxState.action = actionCell;
                    fxState.hasAction = true;
                    pendingFx[fx].execute = fxState.hasValue;
                } else if (actionCell.state == FxActionCellState::Previous) {
                    pendingFx[fx].execute = fxState.hasAction
                        && fxState.hasValue;
                }
                if (pendingFx[fx].execute) {
                    pendingFx[fx].action = fxState.action;
                    pendingFx[fx].value = fxState.value;
                    pendingFx[fx].trackRelativeTarget
                        = fxState.action.targetNode == kTrackInstrumentNode;
                }
            }
            advance(definition.actionColumn, fxState.actionColumn);
            advance(definition.valueColumn, fxState.valueColumn);
        }

        struct NoteFx {
            float probability = 1.0f;
            float repeatPreviousProbability = 0.0f;
            float euclideanDensity = 1.0f;
            int offset = 0;
            uint8_t skipCycle = 1u;
            bool probabilityEnabled = false;
            bool repeatPreviousEnabled = false;
            bool euclidEnabled = false;
            bool offsetEnabled = false;
            bool skipEnabled = false;
        } noteFx;
        for (const auto& pending : pendingFx) {
            if (!pending.execute
                || pending.action.state != FxActionCellState::Sequencer)
                continue;
            const float value = normalizedParameter(pending.value);
            switch (pending.action.sequencerAction) {
            case SequencerAction::Probability:
                noteFx.probabilityEnabled = true;
                noteFx.probability = value;
                break;
            case SequencerAction::Skip:
                noteFx.skipEnabled = true;
                noteFx.skipCycle = static_cast<uint8_t>(std::clamp<long>(
                    std::lround(2.0f + value * 6.0f), 2l, 8l));
                break;
            case SequencerAction::Offset:
                noteFx.offsetEnabled = true;
                noteFx.offset = static_cast<int>(std::clamp<long>(
                    static_cast<long>(std::floor(
                        (static_cast<double>(value) - 0.5) * 8.0 + 0.5)),
                    -4l, 4l));
                break;
            case SequencerAction::RepeatPrevious:
                noteFx.repeatPreviousEnabled = true;
                noteFx.repeatPreviousProbability = value;
                break;
            case SequencerAction::Euclid:
                noteFx.euclidEnabled = true;
                noteFx.euclideanDensity = value;
                break;
            default:
                break;
            }
        }

        const auto chancePassed = [&](float probability,
                                      float unconditionalThreshold,
                                      bool consumeWhenUnconditional,
                                      bool inclusive) {
            probability = normalizedParameter(probability);
            const bool unconditional
                = probability >= unconditionalThreshold;
            if (unconditional && !consumeWhenUnconditional) return true;
            const uint32_t random = nextRandom(state.noteFxRandomState);
            const double unit = static_cast<double>(random)
                / 4294967296.0;
            return unconditional
                || (inclusive ? unit <= static_cast<double>(probability)
                              : unit < static_cast<double>(probability));
        };

        // Source transforms are deliberately unable to resurrect an authored
        // Kill or a muted NOTE lane. OF substitutes a valid nearby source;
        // RP then fills only a still-empty candidate from an accepted onset.
        if (!candidate.hardRelease && !candidate.hold && noteFx.offsetEnabled
            && noteFx.offset != 0 && noteLength > 0u) {
            const auto distance = static_cast<std::size_t>(
                std::abs(noteFx.offset)) % noteLength;
            const auto offsetRow = noteFx.offset > 0
                ? (candidate.sourceRow + distance) % noteLength
                : (candidate.sourceRow + noteLength - distance) % noteLength;
            const auto& source = track.notes[offsetRow];
            if (source.state == NoteCellState::Note) {
                candidate.note = static_cast<uint8_t>(std::min<int>(
                    source.note, 127));
                candidate.sourceRow = offsetRow;
                candidate.trigger = true;
                candidate.retrigger = false;
            }
        }
        if (!candidate.trigger && !candidate.hold
            && noteFx.repeatPreviousEnabled) {
            const bool repeat = chancePassed(
                noteFx.repeatPreviousProbability, 1.0f, true, true);
            if (!candidate.hardRelease && repeat
                && memory.hasLastEmitted) {
                candidate.note = memory.lastEmittedNote;
                candidate.channel = memory.lastEmittedChannel;
                candidate.velocity = memory.lastEmittedVelocity;
                candidate.nodeId = memory.lastEmittedNodeId;
                candidate.destination = memory.lastEmittedDestination;
                candidate.trigger = true;
                candidate.retrigger = false;
            }
        }

        bool accepted = candidate.trigger && !candidate.hardRelease;
        if (noteFx.probabilityEnabled) {
            const bool probabilityPassed = chancePassed(
                noteFx.probability, 0.999f, false, false);
            accepted = accepted && probabilityPassed;
        }
        if (accepted && noteFx.skipEnabled) {
            if (candidate.sourceRow >= state.skipCounters.size()) {
                accepted = false;
            } else {
                auto& counter = state.skipCounters[candidate.sourceRow];
                const bool pass = counter % noteFx.skipCycle == 0u;
                ++counter;
                accepted = pass;
            }
        }
        if (accepted && noteFx.euclidEnabled && noteLength > 0u) {
            const auto hits = static_cast<std::size_t>(std::clamp<long long>(
                std::llround(static_cast<double>(noteFx.euclideanDensity)
                    * static_cast<double>(noteLength)),
                1ll, static_cast<long long>(noteLength)));
            accepted = ((candidate.sourceRow * hits) % noteLength) < hits;
        }
        state.lastNoteTriggered = accepted;

        bool sustainOnset = false;
        if (accepted && noteLength > 0u) {
            auto nextNoteColumn = state.noteColumn;
            advance(track.noteColumn, nextNoteColumn);
            const auto& nextCell = track.notes[
                nextNoteColumn.position % noteLength];
            sustainOnset = nextCell.state == NoteCellState::Hold
                || nextCell.state == NoteCellState::Kill;
        }

        const bool shouldKill = candidate.hardRelease
            && memory.hasNote && !memory.noteMuted;
        if (candidate.hardRelease) memory.noteMuted = true;
        const bool shouldRetrigger = accepted && candidate.retrigger;
        const bool shouldReleaseHeld = memory.sustainHeld
            && !candidate.hold;
        const bool shouldReleaseForReassignment
            = state.releaseActiveForDefaultReassignment;
        const uint64_t releaseNoteId = memory.noteId;
        const uint32_t releaseNodeId = memory.activeNodeId;
        const uint8_t releaseNote = memory.activeNote;
        const uint8_t releaseChannel = memory.activeChannel;
        const float releaseVelocity = memory.activeVelocity;
        const EventDestination releaseDestination = memory.activeDestination;
        const uint64_t onsetNoteId = accepted ? allocateNoteId() : 0u;

        if (shouldKill || shouldRetrigger || shouldReleaseHeld
            || shouldReleaseForReassignment) {
            writeNote(ScheduledEventKind::NoteOff, releaseNoteId,
                releaseNodeId, releaseNote, releaseChannel,
                releaseVelocity, releaseDestination);
            memory.activeNodeId = kInvalidInstrumentNode;
            memory.activeDestination = EventDestination::None;
            memory.sustainHeld = false;
            state.releaseActiveForDefaultReassignment = false;
        }

        // Resolve lane-relative parameter destinations only after the note
        // candidate has survived every gate. Parameters still execute when a
        // note is rejected; Note scope then stays attached to the active note.
        for (auto& pending : pendingFx) {
            if (!pending.execute
                || pending.action.state != FxActionCellState::Parameter)
                continue;
            if (!pending.trackRelativeTarget) continue;
            pending.action.targetNode = pending.action.scope
                    == ParameterScope::Note
                ? (accepted ? candidate.nodeId : memory.activeNodeId)
                : memory.instrumentNodeId;
        }
        for (std::size_t fx = 0u; fx < pendingFx.size(); ++fx) {
            const auto& pending = pendingFx[fx];
            if (!pending.execute
                || pending.action.state != FxActionCellState::Parameter)
                continue;
            bool shadowed = false;
            for (std::size_t later = fx + 1u; later < pendingFx.size();
                 ++later) {
                const auto& laterFx = pendingFx[later];
                if (laterFx.execute
                    && laterFx.action.state
                        == FxActionCellState::Parameter
                    && laterFx.action.targetNode
                        == pending.action.targetNode
                    && laterFx.action.parameterId
                        == pending.action.parameterId
                    && laterFx.action.scope == pending.action.scope) {
                    shadowed = true;
                    break;
                }
            }
            if (shadowed) continue;
            const uint64_t targetNoteId = onsetNoteId != 0u
                ? onsetNoteId : memory.noteId;
            const bool activeAfterRelease = accepted
                || (!(shouldKill || shouldRetrigger || shouldReleaseHeld
                        || shouldReleaseForReassignment)
                    && memory.activeDestination != EventDestination::None
                    && !memory.noteMuted);
            if (pending.action.scope == ParameterScope::Note
                && (targetNoteId == 0u || !activeAfterRelease)) continue;
            ScheduledEvent event;
            event.absoluteSampleTime = absoluteSampleTime;
            event.noteId = pending.action.scope == ParameterScope::Note
                ? targetNoteId : 0u;
            event.frameOffset = frameOffset;
            event.track = static_cast<uint32_t>(trackIndex);
            event.targetNode = pending.action.targetNode;
            event.parameterId = pending.action.parameterId;
            event.parameterValue = pending.value;
            event.note = accepted ? candidate.note : memory.activeNote;
            event.channel = accepted
                ? candidate.channel : memory.activeChannel;
            event.kind = ScheduledEventKind::Parameter;
            event.parameterScope = pending.action.scope;
            event.destination = pending.trackRelativeTarget
                    && pending.action.scope == ParameterScope::Note
                ? (accepted ? candidate.destination
                            : memory.activeDestination)
                : destinationForInstrument(event.targetNode,
                      track.destination);
            writeScheduled(event);
        }
        if (accepted) {
            memory.note = candidate.note;
            memory.hasNote = true;
            memory.noteMuted = false;
            memory.noteId = onsetNoteId;
            memory.activeNote = candidate.note;
            memory.activeChannel = candidate.channel;
            memory.activeVelocity = candidate.velocity;
            memory.activeNodeId = candidate.nodeId;
            memory.activeDestination = candidate.destination;
            memory.lastEmittedNote = candidate.note;
            memory.lastEmittedChannel = candidate.channel;
            memory.lastEmittedVelocity = candidate.velocity;
            memory.lastEmittedNodeId = candidate.nodeId;
            memory.lastEmittedDestination = candidate.destination;
            memory.hasLastEmitted = true;
            memory.sustainHeld = sustainOnset;
            writeNote(ScheduledEventKind::NoteOn, onsetNoteId,
                candidate.nodeId, candidate.note, candidate.channel,
                candidate.velocity, candidate.destination,
                sustainOnset ? kSustainUntilExplicitNoteOff : 0u);
        }

        advance(track.noteColumn, state.noteColumn);
        advance(track.instrumentColumn, state.instrumentColumn);
        advance(track.velocityColumn, state.velocityColumn);
    }
}

void Sequencer::advance(const ColumnDefinition& definition,
    ColumnState& state) noexcept
{
    const std::size_t length = definition.length;
    if (length <= 1u) {
        state.position = 0u;
        state.pingPongDirection = 1;
        return;
    }

    const auto stride = static_cast<std::size_t>(
        std::max(definition.stride, 1u));
    switch (definition.direction) {
    case Direction::Reverse: {
        const auto reduced = stride % length;
        state.position = (state.position + length - reduced) % length;
        break;
    }
    case Direction::Random:
        state.position = static_cast<std::size_t>(
            nextRandom(state.randomState)) % length;
        break;
    case Direction::Palindrome: {
        const std::size_t maximum = length - 1u;
        const std::size_t period = maximum * 2u;
        const std::size_t phase = state.pingPongDirection < 0
            ? (period - state.position) % period
            : state.position % period;
        const std::size_t nextPhase = (phase + stride % period) % period;
        state.position = nextPhase <= maximum
            ? nextPhase : period - nextPhase;
        state.pingPongDirection = nextPhase <= maximum ? 1 : -1;
        break;
    }
    case Direction::Forward:
    default:
        state.position = (state.position + stride) % length;
        break;
    }
}

std::size_t Sequencer::activeLength(const ColumnDefinition& column,
    std::size_t dataSize) noexcept
{
    return std::min(column.length, dataSize);
}

long double Sequencer::tickInterval(const TransportSettings& settings,
    uint64_t tick) noexcept
{
    const long double straight = static_cast<long double>(settings.sampleRate)
        * 60.0L / (static_cast<long double>(settings.bpm)
            * static_cast<long double>(settings.ticksPerBeat));
    const long double phaseNow = warpedTickPhase(settings, tick);
    const long double phaseNext = warpedTickPhase(settings, tick + 1u);
    const long double cycleTicks = static_cast<long double>(
        settings.warpCycleTicks);
    return std::max(0.0L, (phaseNext - phaseNow) * cycleTicks * straight);
}

long double Sequencer::nextTickInterval() const noexcept
{
    return tickInterval(transport_, tickIndex_);
}

long double Sequencer::warpedTickPhase(const TransportSettings& settings,
    uint64_t tick) noexcept
{
    const long double cycleTicks = static_cast<long double>(
        std::max<uint32_t>(settings.warpCycleTicks, 1u));
    // Swing belongs to the global tracker-tick stream, not to the functional
    // warp cycle. Applying this delay before cycle reduction preserves every
    // two-tick pair even when the warp cycle has an odd length.
    long double swungTick = static_cast<long double>(tick);
    if ((tick & 1u) != 0u && settings.swing > 0.5000001) {
        swungTick += 2.0L * static_cast<long double>(settings.swing)
            - 1.0L;
    }
    const long double cycle = std::floor(swungTick / cycleTicks);
    const long double phase = std::clamp(
        (swungTick - cycle * cycleTicks) / cycleTicks, 0.0L, 1.0L);
    const long double mapped = static_cast<long double>(
        settings.timingWarp.map(static_cast<double>(phase)));
    return cycle + mapped;
}

uint32_t Sequencer::columnSeed(std::size_t track, uint32_t salt) const noexcept
{
    uint32_t value = randomSeed_
        ^ static_cast<uint32_t>((track + 1u) * 0x9e3779b9u)
        ^ ((salt + 1u) * 0x85ebca6bu);
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value == 0u ? 1u : value;
}

uint32_t Sequencer::nextRandom(uint32_t& state) noexcept
{
    uint32_t value = state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    state = value;
    return value;
}

uint64_t Sequencer::allocateNoteId() noexcept
{
    const uint64_t result = nextNoteId_;
    ++nextNoteId_;
    if (nextNoteId_ == 0u) nextNoteId_ = 1u;
    return result;
}

} // namespace s3g::tracker
