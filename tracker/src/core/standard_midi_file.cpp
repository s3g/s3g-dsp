#include "s3g/tracker/standard_midi_file.h"

#include "s3g/tracker/clap_playback_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace s3g::tracker {
namespace {

constexpr uint64_t kMaximumExportedTrackerTicks = 1u << 20u;
constexpr std::size_t kMaximumExportedMidiEvents = 1000000u;
constexpr uint32_t kMaximumVariableLengthQuantity = 0x0fffffffu;

struct TempoPoint {
    uint64_t sample = 0u;
    double bpm = 120.0;
};

struct RenderObserver {
    ClapPlaybackRuntime* runtime = nullptr;
    StandardMidiFileScope scope = StandardMidiFileScope::ActivePattern;
    uint64_t patternTickCount = 1u;
    uint64_t completedTicks = 0u;
    uint64_t naturalEndSample = 0u;
    std::array<TempoPoint, kMaximumSongRows + 2u> tempoPoints {};
    std::size_t tempoPointCount = 0u;

    void recordTempo(uint64_t sample) noexcept
    {
        const double bpm = runtime->scheduler.transport().bpm;
        if (tempoPointCount != 0u
            && std::abs(tempoPoints[tempoPointCount - 1u].bpm - bpm)
                < 1.0e-9) return;
        if (tempoPointCount < tempoPoints.size())
            tempoPoints[tempoPointCount++] = { sample, bpm };
    }
};

LogicalTickBoundaryAction observeRender(void* context,
    const LogicalTickBoundary& boundary) noexcept
{
    auto& observer = *static_cast<RenderObserver*>(context);
    const auto runtimeAction = ClapPlaybackRuntime::advanceLogicalTick(
        observer.runtime, boundary);
    ++observer.completedTicks;
    observer.naturalEndSample = std::max(
        boundary.absoluteSampleTime + 1u,
        observer.runtime->scheduler.nextTickSampleFrame());
    observer.recordTempo(boundary.absoluteSampleTime);
    if (runtimeAction == LogicalTickBoundaryAction::StopAfterBoundary)
        return runtimeAction;
    if (observer.scope == StandardMidiFileScope::Song) {
        if (observer.runtime->songPlanner.isFinished())
            return LogicalTickBoundaryAction::StopAfterBoundary;
    } else if (observer.completedTicks >= observer.patternTickCount) {
        return LogicalTickBoundaryAction::StopAfterBoundary;
    }
    return LogicalTickBoundaryAction::Continue;
}

std::size_t longestPatternColumnLength(const Pattern& pattern) noexcept
{
    std::size_t longest = 1u;
    const auto include = [&](const ColumnDefinition& column) {
        longest = std::max(longest, column.length);
    };
    for (const auto& track : pattern.tracks) {
        include(track.noteColumn);
        include(track.instrumentColumn);
        include(track.velocityColumn);
        include(track.gateColumn);
        for (const auto& pair : track.fxPairs) {
            include(pair.actionColumn);
            include(pair.valueColumn);
        }
    }
    return std::clamp(longest, std::size_t { 1u },
        static_cast<std::size_t>(kMaximumSongPatternRows));
}

bool checkedSongTickCount(const SongArrangement& song,
    uint64_t& destination) noexcept
{
    destination = 0u;
    for (const auto& row : song.rows) {
        const uint64_t rowTicks = static_cast<uint64_t>(row.durationTicks)
            * static_cast<uint64_t>(row.repeats);
        if (rowTicks > kMaximumExportedTrackerTicks - destination)
            return false;
        destination += rowTicks;
    }
    return destination != 0u;
}

std::size_t exportedLaneCount(const ProjectDocument& document,
    StandardMidiFileScope scope) noexcept
{
    std::size_t count = 0u;
    if (scope == StandardMidiFileScope::ActivePattern) {
        const auto* pattern = document.patternBank.activePattern();
        return pattern ? pattern->tracks.size() : 0u;
    }
    for (const auto& row : document.song.rows) {
        const auto* pattern = document.patternBank.findPattern(row.patternId);
        if (pattern) count = std::max(count, pattern->tracks.size());
    }
    return count;
}

std::vector<std::string> exportedLaneNames(const ProjectDocument& document,
    StandardMidiFileScope scope, std::size_t count)
{
    std::vector<std::string> names(count);
    const auto include = [&](const Pattern& pattern) {
        for (std::size_t lane = 0u;
             lane < std::min(count, pattern.tracks.size()); ++lane) {
            if (names[lane].empty() && !pattern.tracks[lane].name.empty())
                names[lane] = pattern.tracks[lane].name;
        }
    };
    if (scope == StandardMidiFileScope::ActivePattern) {
        if (const auto* pattern = document.patternBank.activePattern())
            include(*pattern);
    } else {
        for (const auto& row : document.song.rows)
            if (const auto* pattern = document.patternBank.findPattern(
                    row.patternId)) include(*pattern);
    }
    for (std::size_t lane = 0u; lane < count; ++lane)
        if (names[lane].empty())
            names[lane] = "Lane " + std::to_string(lane + 1u);
    return names;
}

struct RawMidiEvent {
    uint64_t sample = 0u;
    uint64_t serial = 0u;
    uint32_t lane = 0u;
    uint8_t status = 0u;
    uint8_t data1 = 0u;
    uint8_t data2 = 0u;
};

struct ActiveNote {
    uint64_t noteId = 0u;
    uint64_t dueSample = 0u;
    uint32_t lane = 0u;
    bool active = false;
};

struct GateOff {
    uint64_t sample = 0u;
    uint64_t noteId = 0u;
    uint32_t lane = 0u;
    uint8_t channel = 0u;
    uint8_t note = 0u;
};

struct LaterGate {
    bool operator()(const GateOff& left, const GateOff& right) const noexcept
    {
        if (left.sample != right.sample) return left.sample > right.sample;
        return left.noteId > right.noteId;
    }
};

class MidiEdgeRenderer {
public:
    MidiEdgeRenderer(double sampleRate, double gateMilliseconds,
        std::size_t laneCount)
        : defaultGateSamples_(static_cast<uint64_t>(std::max(1.0,
            std::round(sampleRate * gateMilliseconds / 1000.0))))
        , laneCount_(laneCount)
    {
    }

    bool consume(const ScheduledEvent& event)
    {
        if (overflow_) return false;
        if (event.kind == ScheduledEventKind::Parameter) return true;
        if (event.track >= laneCount_) return false;
        flushDue(event.absoluteSampleTime);
        if (overflow_) return false;
        const uint8_t channel = static_cast<uint8_t>(
            std::clamp<int>(event.channel, 1, 16) - 1);
        if (event.kind == ScheduledEventKind::ControlChange) {
            return emit(event.absoluteSampleTime, event.track,
                static_cast<uint8_t>(0xb0u | channel),
                static_cast<uint8_t>(std::min<uint32_t>(
                    event.parameterId, 127u)),
                midiValueFromNormalized(event.parameterValue));
        }
        const std::size_t index = static_cast<std::size_t>(channel) * 128u
            + event.note;
        auto& active = active_[index];
        if (event.kind == ScheduledEventKind::NoteOff) {
            if (active.active && (event.noteId == 0u
                    || event.noteId == active.noteId)) {
                if (!emitOff(event.absoluteSampleTime, active.lane,
                        channel, event.note)) return false;
                active = {};
            }
            return true;
        }
        if (active.active) {
            if (!emitOff(event.absoluteSampleTime, active.lane,
                    channel, event.note)) return false;
            active = {};
        }
        if (!emit(event.absoluteSampleTime, event.track,
                static_cast<uint8_t>(0x90u | channel), event.note,
                midiVelocityFromNormalized(event.normalizedVelocity)))
            return false;
        const uint64_t gate = event.durationSamples != 0u
            ? event.durationSamples : defaultGateSamples_;
        const uint64_t due = gate > std::numeric_limits<uint64_t>::max()
                - event.absoluteSampleTime
            ? std::numeric_limits<uint64_t>::max()
            : event.absoluteSampleTime + gate;
        active = { event.noteId, due, event.track, true };
        if (due != std::numeric_limits<uint64_t>::max())
            gates_.push({ due, event.noteId, event.track,
                channel, event.note });
        return true;
    }

    bool finish(uint64_t naturalEndSample)
    {
        if (overflow_) return false;
        while (!gates_.empty()) {
            const auto gate = gates_.top();
            gates_.pop();
            const std::size_t index
                = static_cast<std::size_t>(gate.channel) * 128u + gate.note;
            auto& active = active_[index];
            if (!active.active || active.noteId != gate.noteId) continue;
            if (!emitOff(gate.sample, active.lane,
                    gate.channel, gate.note)) return false;
            active = {};
            naturalEndSample = std::max(naturalEndSample, gate.sample);
        }
        const uint64_t finalSample = std::max(naturalEndSample,
            events_.empty() ? uint64_t { 0u } : events_.back().sample);
        for (std::size_t index = 0u; index < active_.size(); ++index) {
            auto& active = active_[index];
            if (!active.active) continue;
            const uint8_t channel = static_cast<uint8_t>(index / 128u);
            const uint8_t note = static_cast<uint8_t>(index % 128u);
            if (!emitOff(finalSample, active.lane, channel, note))
                return false;
            active = {};
        }
        finalSample_ = std::max(finalSample_, finalSample);
        return !overflow_;
    }

    const std::vector<RawMidiEvent>& events() const noexcept
    {
        return events_;
    }

    uint64_t finalSample() const noexcept { return finalSample_; }

private:
    bool emit(uint64_t sample, uint32_t lane, uint8_t status,
        uint8_t data1, uint8_t data2)
    {
        if (events_.size() >= kMaximumExportedMidiEvents) return false;
        events_.push_back({ sample, serial_++, lane, status, data1, data2 });
        finalSample_ = std::max(finalSample_, sample);
        return true;
    }

    bool emitOff(uint64_t sample, uint32_t lane, uint8_t channel,
        uint8_t note)
    {
        return emit(sample, lane, static_cast<uint8_t>(0x80u | channel),
            note, 0u);
    }

    void flushDue(uint64_t sample)
    {
        while (!gates_.empty() && gates_.top().sample <= sample) {
            const auto gate = gates_.top();
            gates_.pop();
            const std::size_t index
                = static_cast<std::size_t>(gate.channel) * 128u + gate.note;
            auto& active = active_[index];
            if (!active.active || active.noteId != gate.noteId) continue;
            if (!emitOff(gate.sample, active.lane,
                    gate.channel, gate.note)) {
                overflow_ = true;
                return;
            }
            active = {};
        }
    }

    uint64_t defaultGateSamples_ = 1u;
    std::size_t laneCount_ = 0u;
    uint64_t serial_ = 0u;
    uint64_t finalSample_ = 0u;
    bool overflow_ = false;
    std::array<ActiveNote, 16u * 128u> active_ {};
    std::priority_queue<GateOff, std::vector<GateOff>, LaterGate> gates_;
    std::vector<RawMidiEvent> events_;
};

void appendU16(std::string& destination, uint16_t value)
{
    destination.push_back(static_cast<char>((value >> 8u) & 0xffu));
    destination.push_back(static_cast<char>(value & 0xffu));
}

void appendU32(std::string& destination, uint32_t value)
{
    destination.push_back(static_cast<char>((value >> 24u) & 0xffu));
    destination.push_back(static_cast<char>((value >> 16u) & 0xffu));
    destination.push_back(static_cast<char>((value >> 8u) & 0xffu));
    destination.push_back(static_cast<char>(value & 0xffu));
}

void appendVariableLength(std::string& destination, uint32_t value)
{
    std::array<uint8_t, 4u> bytes {};
    std::size_t count = 0u;
    bytes[count++] = static_cast<uint8_t>(value & 0x7fu);
    while ((value >>= 7u) != 0u)
        bytes[count++] = static_cast<uint8_t>((value & 0x7fu) | 0x80u);
    while (count != 0u)
        destination.push_back(static_cast<char>(bytes[--count]));
}

void appendDelta(std::string& destination, uint64_t delta)
{
    while (delta > kMaximumVariableLengthQuantity) {
        appendVariableLength(destination, kMaximumVariableLengthQuantity);
        destination.append("\xff\x01\x00", 3u);
        delta -= kMaximumVariableLengthQuantity;
    }
    appendVariableLength(destination, static_cast<uint32_t>(delta));
}

void appendMeta(std::string& track, uint64_t delta, uint8_t type,
    const char* data, std::size_t size)
{
    appendDelta(track, delta);
    track.push_back(static_cast<char>(0xffu));
    track.push_back(static_cast<char>(type));
    appendVariableLength(track, static_cast<uint32_t>(size));
    if (size != 0u) track.append(data, size);
}

void appendTrackChunk(std::string& file, const std::string& track)
{
    file.append("MTrk", 4u);
    appendU32(file, static_cast<uint32_t>(track.size()));
    file += track;
}

struct ResolvedTempoPoint {
    uint64_t sample = 0u;
    long double tick = 0.0L;
    double bpm = 120.0;
};

std::vector<ResolvedTempoPoint> resolveTempoPoints(
    const RenderObserver& observer, double sampleRate, uint16_t ppq)
{
    std::vector<TempoPoint> points(observer.tempoPoints.begin(),
        observer.tempoPoints.begin()
            + static_cast<std::ptrdiff_t>(observer.tempoPointCount));
    std::stable_sort(points.begin(), points.end(),
        [](const auto& left, const auto& right) {
            return left.sample < right.sample;
        });
    std::vector<ResolvedTempoPoint> result;
    result.reserve(points.size());
    for (const auto& point : points) {
        if (!result.empty() && result.back().sample == point.sample) {
            result.back().bpm = point.bpm;
            continue;
        }
        long double tick = 0.0L;
        if (!result.empty()) {
            const auto& prior = result.back();
            tick = prior.tick
                + static_cast<long double>(point.sample - prior.sample)
                    * static_cast<long double>(ppq)
                    * static_cast<long double>(prior.bpm)
                    / (static_cast<long double>(sampleRate) * 60.0L);
        }
        result.push_back({ point.sample, tick, point.bpm });
    }
    return result;
}

uint64_t sampleToTick(uint64_t sample,
    const std::vector<ResolvedTempoPoint>& tempos, double sampleRate,
    uint16_t ppq) noexcept
{
    const auto after = std::upper_bound(tempos.begin(), tempos.end(), sample,
        [](uint64_t value, const ResolvedTempoPoint& point) {
            return value < point.sample;
        });
    const auto& point = after == tempos.begin() ? tempos.front()
                                                : *(after - 1);
    const long double tick = point.tick
        + static_cast<long double>(sample - point.sample)
            * static_cast<long double>(ppq)
            * static_cast<long double>(point.bpm)
            / (static_cast<long double>(sampleRate) * 60.0L);
    if (tick <= 0.0L) return 0u;
    if (tick >= static_cast<long double>(
            std::numeric_limits<uint64_t>::max()))
        return std::numeric_limits<uint64_t>::max();
    return static_cast<uint64_t>(std::llround(tick));
}

ProjectResult failure(ProjectErrorCode code, std::string message)
{
    return { code, "standard MIDI file", std::move(message) };
}

} // namespace

ProjectResult encodeStandardMidiFile(const ProjectDocument& source,
    const StandardMidiFileOptions& options, std::string& destination,
    StandardMidiFileSummary* summary)
{
    if (options.ticksPerQuarterNote == 0u
        || options.ticksPerQuarterNote > 0x7fffu
        || !std::isfinite(options.sampleRate)
        || options.sampleRate < 8000.0 || options.sampleRate > 768000.0) {
        return failure(ProjectErrorCode::InvalidArgument,
            "invalid MIDI division or offline sample rate");
    }
    if (!validatePatternBank(source.patternBank).ok())
        return failure(ProjectErrorCode::InconsistentData,
            "the pattern bank is invalid");
    const auto* activePattern = source.patternBank.activePattern();
    if (!activePattern)
        return failure(ProjectErrorCode::InconsistentData,
            "the active pattern is missing");

    ProjectDocument document = source;
    uint64_t expectedTicks = 0u;
    if (options.scope == StandardMidiFileScope::Song) {
        if (!validateSongArrangement(document.song).ok()
            || !validateSongPatternReferences(
                document.song, document.patternBank).ok()) {
            return failure(ProjectErrorCode::InconsistentData,
                "the Song arrangement is empty, invalid, or references a missing pattern");
        }
        if (!checkedSongTickCount(document.song, expectedTicks))
            return failure(ProjectErrorCode::SizeLimitExceeded,
                "the Song is too long for one MIDI export");
        document.song.loop = false;
        document.session.songPlaybackEnabled = true;
    } else {
        expectedTicks = longestPatternColumnLength(*activePattern);
        document.session.songPlaybackEnabled = false;
    }

    const std::size_t laneCount = exportedLaneCount(
        document, options.scope);
    if (laneCount > kMaximumTrackCount)
        return failure(ProjectErrorCode::OutOfRange,
            "the export contains more than 32 lanes");

    ClapPlaybackRuntime runtime(document, options.sampleRate);
    if (!runtime.valid)
        return failure(ProjectErrorCode::InconsistentData,
            "the Tracker playback plan could not be prepared");
    const double hostTempo = std::isfinite(document.transport.bpm)
        ? document.transport.bpm : 120.0;
    if (!runtime.arm(0.0, hostTempo, options.sampleRate, 0u, true))
        return failure(ProjectErrorCode::InconsistentData,
            "the Tracker playback plan could not be started");

    RenderObserver observer;
    observer.runtime = &runtime;
    observer.scope = options.scope;
    observer.patternTickCount = expectedTicks;
    observer.recordTempo(0u);
    runtime.scheduler.setLogicalTickObserver(&observeRender, &observer);

    MidiEdgeRenderer edgeRenderer(options.sampleRate,
        document.session.gateMilliseconds, laneCount);
    std::vector<ScheduledEvent> scheduled(kMaximumPendingScheduledEvents);
    while (runtime.scheduler.isPlaying()) {
        if (observer.completedTicks > kMaximumExportedTrackerTicks)
            return failure(ProjectErrorCode::SizeLimitExceeded,
                "the rendered sequence exceeds the MIDI export tick limit");
        const uint64_t rendered = runtime.scheduler.renderedFrameCount();
        const uint64_t next = runtime.scheduler.nextTickSampleFrame();
        const uint64_t distance = next >= rendered
            ? next - rendered + 1u : 1u;
        const uint32_t frames = static_cast<uint32_t>(std::min<uint64_t>(
            distance, std::numeric_limits<uint32_t>::max()));
        const auto count = runtime.scheduler.process(frames,
            scheduled.data(), scheduled.size());
        for (std::size_t index = 0u; index < count; ++index)
            if (!edgeRenderer.consume(scheduled[index]))
                return failure(ProjectErrorCode::SizeLimitExceeded,
                    "the rendered sequence exceeds the MIDI event limit");
        if (runtime.scheduler.droppedEventCount() != 0u)
            return failure(ProjectErrorCode::SizeLimitExceeded,
                "the rendered sequence exceeded Tracker's event capacity");
    }

    while (runtime.scheduler.pendingEventCount() != 0u) {
        constexpr uint32_t kDrainFrames = 1u << 20u;
        const auto count = runtime.scheduler.process(kDrainFrames,
            scheduled.data(), scheduled.size());
        for (std::size_t index = 0u; index < count; ++index)
            if (!edgeRenderer.consume(scheduled[index]))
                return failure(ProjectErrorCode::SizeLimitExceeded,
                    "the rendered sequence exceeds the MIDI event limit");
        if (runtime.scheduler.droppedEventCount() != 0u)
            return failure(ProjectErrorCode::SizeLimitExceeded,
                "the rendered sequence exceeded Tracker's event capacity");
    }
    if (!edgeRenderer.finish(observer.naturalEndSample))
        return failure(ProjectErrorCode::SizeLimitExceeded,
            "the rendered sequence exceeds the MIDI event limit");

    const auto tempos = resolveTempoPoints(observer, options.sampleRate,
        options.ticksPerQuarterNote);
    if (tempos.empty())
        return failure(ProjectErrorCode::InconsistentData,
            "the render did not produce a tempo map");

    const uint64_t durationTicks = sampleToTick(std::max(
        observer.naturalEndSample, edgeRenderer.finalSample()), tempos,
        options.sampleRate, options.ticksPerQuarterNote);

    std::string file;
    file.reserve(1024u + edgeRenderer.events().size() * 8u);
    file.append("MThd", 4u);
    appendU32(file, 6u);
    appendU16(file, 1u);
    appendU16(file, static_cast<uint16_t>(laneCount + 1u));
    appendU16(file, options.ticksPerQuarterNote);

    std::string tempoTrack;
    const std::string title = options.scope == StandardMidiFileScope::Song
        ? (document.song.name.empty() ? "s3g Tracker Song"
                                      : document.song.name)
        : (activePattern->name.empty() ? "s3g Tracker Pattern"
                                      : activePattern->name);
    appendMeta(tempoTrack, 0u, 0x03u, title.data(), title.size());
    const char timeSignature[] = { 4, 2, 24, 8 };
    appendMeta(tempoTrack, 0u, 0x58u, timeSignature,
        sizeof(timeSignature));
    uint64_t previousTempoTick = 0u;
    for (const auto& tempo : tempos) {
        const uint64_t tick = sampleToTick(tempo.sample, tempos,
            options.sampleRate, options.ticksPerQuarterNote);
        const auto micros = static_cast<uint32_t>(std::clamp<long long>(
            std::llround(60000000.0 / tempo.bpm), 1ll, 0xffffffll));
        const char bytes[] = {
            static_cast<char>((micros >> 16u) & 0xffu),
            static_cast<char>((micros >> 8u) & 0xffu),
            static_cast<char>(micros & 0xffu),
        };
        appendMeta(tempoTrack, tick - previousTempoTick, 0x51u,
            bytes, sizeof(bytes));
        previousTempoTick = tick;
    }
    appendMeta(tempoTrack, durationTicks - previousTempoTick,
        0x2fu, nullptr, 0u);
    appendTrackChunk(file, tempoTrack);

    const auto laneNames = exportedLaneNames(document, options.scope,
        laneCount);
    std::vector<std::vector<RawMidiEvent>> lanes(laneCount);
    for (const auto& event : edgeRenderer.events())
        lanes[event.lane].push_back(event);

    for (std::size_t lane = 0u; lane < laneCount; ++lane) {
        auto& events = lanes[lane];
        std::stable_sort(events.begin(), events.end(),
            [](const auto& left, const auto& right) {
                if (left.sample != right.sample)
                    return left.sample < right.sample;
                return left.serial < right.serial;
            });
        std::string track;
        appendMeta(track, 0u, 0x03u, laneNames[lane].data(),
            laneNames[lane].size());
        uint64_t previousTick = 0u;
        for (const auto& event : events) {
            const uint64_t tick = sampleToTick(event.sample, tempos,
                options.sampleRate, options.ticksPerQuarterNote);
            appendDelta(track, tick - previousTick);
            track.push_back(static_cast<char>(event.status));
            track.push_back(static_cast<char>(event.data1));
            track.push_back(static_cast<char>(event.data2));
            previousTick = tick;
        }
        appendMeta(track, durationTicks - previousTick,
            0x2fu, nullptr, 0u);
        appendTrackChunk(file, track);
    }

    if (file.size() > kMaximumProjectDocumentBytes)
        return failure(ProjectErrorCode::SizeLimitExceeded,
            "the Standard MIDI File exceeds the 64 MiB Tracker file limit");
    destination = std::move(file);
    if (summary) {
        summary->laneTrackCount = laneCount;
        summary->midiEventCount = edgeRenderer.events().size();
        summary->durationTicks = durationTicks;
        summary->renderedSong
            = options.scope == StandardMidiFileScope::Song;
    }
    return {};
}

} // namespace s3g::tracker
