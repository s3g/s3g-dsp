#pragma once

#include "s3g/tracker/instrument_rack.h"
#include "s3g/tracker/timing_warp.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace s3g::tracker {

// Thirty-two tracks x (retrigger off/on + two FX actions) x the initial
// 16-tick live warp collision budget is 2048 events returned by one process
// call. Timing FX can retain up to fifteen onsets per lane across future ticks;
// the separate 8192-event pending heap bounds that expansion. More extreme
// density fails closed with explicit telemetry rather than silently diverging
// audio and MIDI output.
constexpr std::size_t kMaximumTrackCount = 32u;
constexpr std::size_t kMaximumScheduledEventsPerBlock = 2048u;
constexpr std::size_t kMaximumPendingScheduledEvents = 8192u;

enum class NoteCellState : uint8_t {
    Rest,
    RetriggerPrevious,
    Kill,
    Note,
};

struct NoteCell {
    NoteCellState state = NoteCellState::Rest;
    uint8_t note = 0u;

    static NoteCell rest() { return {}; }

    static NoteCell retriggerPrevious()
    {
        NoteCell cell;
        cell.state = NoteCellState::RetriggerPrevious;
        return cell;
    }

    static NoteCell kill()
    {
        NoteCell cell;
        cell.state = NoteCellState::Kill;
        return cell;
    }

    static NoteCell withNote(uint8_t newNote)
    {
        NoteCell cell;
        cell.state = NoteCellState::Note;
        cell.note = newNote;
        return cell;
    }
};

// Decimal MIDI (0..127) and readable note names share one entry contract.
// Natural names accept both tracker form (C-3) and compact form (C3);
// sharps and flats accept C#3 and Db3.
bool parseMidiNote(std::string_view text, uint8_t& note) noexcept;

enum class ValueCellState : uint8_t {
    Default,
    Previous,
    Value,
};

struct ValueCell {
    ValueCellState state = ValueCellState::Default;
    float normalized = 0.0f;

    static ValueCell defaultValue() { return {}; }

    static ValueCell previous()
    {
        ValueCell cell;
        cell.state = ValueCellState::Previous;
        return cell;
    }

    static ValueCell withValue(float newValue)
    {
        ValueCell cell;
        cell.state = ValueCellState::Value;
        cell.normalized = newValue;
        return cell;
    }
};

// Instrument is an independently advancing memory column. Empty leaves the
// remembered rack node untouched without making an explicit recall gesture;
// Previous also retains it, but records that intent in the authored pattern.
// An Instrument cell replaces the memory before same-tick FX and notes resolve
// their lane-relative target.
enum class InstrumentCellState : uint8_t {
    Empty,
    Previous,
    Instrument,
};

struct InstrumentCell {
    InstrumentCellState state = InstrumentCellState::Empty;
    uint32_t nodeId = kInvalidInstrumentNode;

    static InstrumentCell empty() { return {}; }

    static InstrumentCell previous()
    {
        InstrumentCell cell;
        cell.state = InstrumentCellState::Previous;
        return cell;
    }

    static InstrumentCell withInstrument(uint32_t newNodeId)
    {
        InstrumentCell cell;
        cell.state = InstrumentCellState::Instrument;
        cell.nodeId = newNodeId;
        return cell;
    }
};

enum class Direction : uint8_t {
    Forward,
    Reverse,
    Random,
    Palindrome,
};

enum class ParameterScope : uint8_t {
    Global,
    Channel,
    Note,
};

// Destination-neutral operations that reshape note production before the
// canonical audio/MIDI event boundary. Their paired V1/V2 values remain
// normalized; the action catalog defines the musical interpretation.
enum class SequencerAction : uint8_t {
    Ratchet,
    MicroTime,
    Delay,
    Flam,
    Stutter,
    Accent,
    Ghost,
    Probability,
    Skip,
    Offset,
    RepeatPrevious,
    Euclid,
    Count,
};

constexpr std::size_t kSequencerActionCount = static_cast<std::size_t>(
    SequencerAction::Count);

enum class FxActionCellState : uint8_t {
    Empty,
    Previous,
    Parameter,
    Sequencer,
};

struct FxActionCell {
    FxActionCellState state = FxActionCellState::Empty;
    uint32_t targetNode = kTrackInstrumentNode;
    uint32_t parameterId = 0u;
    ParameterScope scope = ParameterScope::Global;
    SequencerAction sequencerAction = SequencerAction::Ratchet;

    static FxActionCell empty() { return {}; }

    static FxActionCell previous()
    {
        FxActionCell cell;
        cell.state = FxActionCellState::Previous;
        return cell;
    }

    static FxActionCell parameter(uint32_t parameterId,
        ParameterScope scope = ParameterScope::Global,
        uint32_t targetNode = kTrackInstrumentNode)
    {
        FxActionCell cell;
        cell.state = FxActionCellState::Parameter;
        cell.targetNode = targetNode;
        cell.parameterId = parameterId;
        cell.scope = scope;
        return cell;
    }

    static FxActionCell sequencer(SequencerAction action)
    {
        FxActionCell cell;
        cell.state = FxActionCellState::Sequencer;
        cell.sequencerAction = action;
        return cell;
    }
};

enum class FxValueCellState : uint8_t {
    Previous,
    Value,
};

struct FxValueCell {
    FxValueCellState state = FxValueCellState::Previous;
    float normalized = 0.0f;

    static FxValueCell previous() { return {}; }

    static FxValueCell withValue(float newValue)
    {
        FxValueCell cell;
        cell.state = FxValueCellState::Value;
        cell.normalized = newValue;
        return cell;
    }
};

// Routing is part of the scheduled musical event, not an assumption made by
// an output adapter. Values are bit-compatible so adapters can cheaply test a
// destination without allocating or consulting project state.
enum class EventDestination : uint8_t {
    None = 0u,
    Internal = 1u << 0u,
    Midi = 1u << 1u,
    Both = (1u << 0u) | (1u << 1u),
};

constexpr bool routesToInternal(EventDestination destination) noexcept
{
    return (static_cast<uint8_t>(destination)
        & static_cast<uint8_t>(EventDestination::Internal)) != 0u;
}

constexpr bool routesToMidi(EventDestination destination) noexcept
{
    return (static_cast<uint8_t>(destination)
        & static_cast<uint8_t>(EventDestination::Midi)) != 0u;
}

struct ColumnDefinition {
    std::size_t length = 16u;
    uint32_t stride = 1u;
    // Zero-based rotation applied independently to this column. Phase is part
    // of authored pattern state: reset and global-loop seeks begin at
    // (transport row + phase) modulo the active column length.
    std::size_t phase = 0u;
    Direction direction = Direction::Forward;
    bool muted = false;
};

// A typed action/value pair inspired by the two FX pairs in the v8 tracker.
// Action and value are independently polymetric. Empty retains action memory
// without executing it; Previous executes the remembered action. Value memory
// can change on a row whose action is empty and be consumed later.
struct FxPair {
    std::vector<FxActionCell> actions;
    std::vector<FxValueCell> values;
    ColumnDefinition actionColumn;
    ColumnDefinition valueColumn;
};

// Read-only snapshot of the authoritative playback memory behind one FX pair.
// Empty, Previous, and muted cells retain these values; reset/setPattern clear
// them, while replacePattern retains overlapping track indices.
struct FxPlaybackMemorySnapshot {
    FxActionCell action;
    float value = 0.0f;
    bool hasAction = false;
    bool hasValue = false;
};

constexpr std::size_t kFxPairCount = 2u;

struct Track {
    std::string name;
    // Event-stage performance trim shared by MIDI and internal NoteOn
    // destinations. This deliberately scales onset velocity rather than
    // pretending the current shared instrument rack has per-lane post-DSP
    // buses. A future audio mixer can retain this as input trim.
    float velocityScale = 1.0f;
    // MIDI channels are one-based here to match the tracker UI and project
    // files. An output adapter converts this to the wire-format nibble.
    uint8_t midiChannel = 1u;
    // Retained as a fallback for explicitly targeted future graph nodes.
    // Known rack slots derive their destination from InstrumentKind, so MIDI
    // OUT is an instrument and never a second, ambiguous per-track switch.
    EventDestination destination = EventDestination::Midi;
    // Initial zero-based slot in the shared instrument rack. The independently
    // polymetric Instrument column can replace this playback memory per row.
    uint32_t initialInstrumentNodeId = 0u;
    // Zero means this lane does not participate in a choke group yet.
    uint32_t chokeGroup = 0u;
    std::vector<NoteCell> notes;
    std::vector<InstrumentCell> instruments;
    std::vector<ValueCell> velocities;
    std::array<FxPair, kFxPairCount> fxPairs;
    ColumnDefinition noteColumn;
    ColumnDefinition instrumentColumn;
    ColumnDefinition velocityColumn;
};

struct Pattern {
    std::string name;
    std::size_t visibleRows = 16u;
    std::vector<Track> tracks;
};

enum class ScheduledEventKind : uint8_t {
    NoteOn,
    NoteOff,
    Parameter,
};

// Canonical scheduler output. Raw MIDI bytes and CLAP events are adapter
// formats derived from this destination-neutral representation.
struct ScheduledEvent {
    uint64_t absoluteSampleTime = 0u;
    // Zero is reserved for events without note identity (for example a
    // parameter action). Every onset/retrigger receives a new nonzero ID and
    // its explicit note-off carries the same ID.
    uint64_t noteId = 0u;
    // Zero means that duration is unspecified and the destination's gate or a
    // later NoteOff controls release.
    uint64_t durationSamples = 0u;
    uint32_t frameOffset = 0u;
    uint32_t track = 0u;
    uint32_t parameterId = 0u;
    uint32_t targetNode = kInvalidInstrumentNode;
    // Zero means no choke group. Nonzero groups are reserved for voice
    // allocators that support mutually exclusive articulations.
    uint32_t chokeGroup = 0u;
    float normalizedVelocity = 0.0f;
    // Canonical parameter values are normalized. A node adapter resolves the
    // destination's native range and stepped behavior off the render thread.
    float parameterValue = 0.0f;
    uint8_t note = 0u;
    // One-based to match the tracker model. MIDI and chip adapters translate
    // this into their own channel representation.
    uint8_t channel = 1u;
    ScheduledEventKind kind = ScheduledEventKind::NoteOn;
    ParameterScope parameterScope = ParameterScope::Global;
    EventDestination destination = EventDestination::Midi;
};

EventDestination destinationForInstrument(uint32_t nodeId,
    EventDestination fallback = EventDestination::None) noexcept;

static_assert(std::is_trivially_copyable<ScheduledEvent>::value,
    "ScheduledEvent must remain callback-safe");

// Explicit MIDI-edge conversion. Note-on velocity is clamped to 1..127 to
// preserve the established tracker behavior (zero-velocity cells still
// produce a note-on rather than a MIDI note-off encoding).
uint8_t midiVelocityFromNormalized(float normalized) noexcept;

struct TransportSettings {
    TransportSettings() = default;
    TransportSettings(double newSampleRate, double newBpm,
        uint32_t newTicksPerBeat, double newSwing) noexcept
        : sampleRate(newSampleRate)
        , bpm(newBpm)
        , ticksPerBeat(newTicksPerBeat)
        , swing(newSwing)
    {
    }

    double sampleRate = 48000.0;
    double bpm = 120.0;
    uint32_t ticksPerBeat = 4u;
    // 0.5 is straight. Values above 0.5 lengthen the first interval of each
    // two-tick pair and shorten the second while preserving pair duration.
    double swing = 0.5;
    // Functional warps repeat over this many tracker ticks. Legacy two-part
    // swing is applied first, then the serial stack maps the resulting
    // normalized phase. The default stack is an exact identity.
    uint32_t warpCycleTicks = 16u;
    TimingWarpStack timingWarp;
    // MT can move a note early only if the sequencer knows about it before its
    // nominal tick. When a pattern contains MT, every note is delayed by this
    // compensation and MT moves within +/- microTimingRangeMilliseconds. The
    // lookahead is normalized to be at least the range.
    double timingLookaheadMilliseconds = 25.0;
    double microTimingRangeMilliseconds = 25.0;
    // Global song-row loop, shared by every polymetric column. loopEndRow is
    // exclusive so a 0..15 selection is represented as [0, 16).
    bool loopEnabled = false;
    uint32_t loopStartRow = 0u;
    uint32_t loopEndRow = 16u;
};

class Sequencer {
public:
    // This bootstrap object is deliberately single-owner and not thread-safe.
    // Serialize every call. setPattern(), setRandomSeed(), and reset() require
    // stopped playback. The application uses replacePattern() on its serial
    // scheduler queue to publish live edits without sharing this object with
    // the UI.
    void setPattern(Pattern pattern);
    // Control/scheduler-thread operation that may allocate. Existing column
    // phase, musical time, and transport state are retained where possible.
    void replacePattern(Pattern pattern);
    const Pattern& pattern() const noexcept { return pattern_; }

    // Builds a frozen, normalized set of runtime patterns and pre-sizes every
    // lane buffer needed by any member. This is a stopped control-thread
    // operation and may allocate. The selected member becomes authoritative
    // and reset() is applied exactly as for setPattern(). Stable project IDs
    // deliberately remain an engine concern; Song rows resolve them to these
    // prepared-set indices before playback starts.
    bool preparePatternSet(std::vector<Pattern> patterns,
        std::size_t initialPatternIndex = 0u);
    std::size_t preparedPatternCount() const noexcept
    {
        return preparedPatterns_.size();
    }
    // Allocation-free Song transition after one complete logical tick and
    // before the next. Overlapping lanes retain tracker recall, RNG, and true
    // active-note ownership. New/non-overlapping lanes start clean; removed
    // sounding lanes queue one exact NoteOff for the next target tick. The
    // master tick/sample clock and next note ID are untouched.
    bool activatePreparedPatternAtTickBoundary(
        std::size_t patternIndex) noexcept;

    void setTransport(TransportSettings settings);
    // Scheduler-thread operation for an already-emitted logical tick. The
    // replacement clock is normalized exactly like setTransport(), then the
    // interval from the completed tick to the next one is recomputed without
    // moving the completed tick, transport row, columns, or active voices.
    // Call only at the processSingleTick() boundary before emitting another
    // tick. Fixed-capacity TimingWarpStack keeps this allocation-free.
    void setTransportAtTickBoundary(TransportSettings settings) noexcept;
    const TransportSettings& transport() const { return transport_; }

    // Ephemeral Song playback controls. The runtime mask is ORed with the
    // authored NOTE mute and therefore performs the same one-time hard
    // release on the first muted tick. Pattern publication and reset retain
    // the mask; its owner must explicitly replace or clear it.
    void setRuntimeTrackMuteMask(uint32_t mask) noexcept
    {
        runtimeTrackMuteMask_ = mask;
    }
    uint32_t runtimeTrackMuteMask() const noexcept
    {
        return runtimeTrackMuteMask_;
    }
    // Quantized Song-row launch: seek every authored column to row + phase
    // while retaining the sample/tick clock, active voice ownership, FX
    // recall, deterministic random streams, and last-emitted note state.
    void relaunchColumnsAtTickBoundary(std::size_t row = 0u) noexcept;
    // Performance resync for one track: move NOTE, INS, VOL, and both FX
    // action/value read heads to the same absolute row, deliberately ignoring
    // their independent authored phase rotations. The next logical tick
    // observes the new positions without moving the transport or another
    // track; playback memory and the last-rendered cursors are kept.
    bool resyncTrackColumnsAtTickBoundary(std::size_t track,
        std::size_t row = 0u) noexcept;

    // start(true) and reset() rebuild playback storage and may allocate; they
    // are control-thread operations, never audio-callback commands.
    void start(bool resetPosition = true);
    // CLAP host-sync entry point. preparePatternSet() must have succeeded
    // first; the pre-sized playback storage then makes this reset/seek path
    // allocation-free for an audio-thread block-boundary transition. The
    // functional warp and swing stack are used to locate the first tracker
    // tick at or after the supplied host quarter-note position.
    bool startPreparedAtHostBeat(double hostBeat) noexcept;
    void stop();
    void reset();
    bool isPlaying() const { return playing_; }

    void setRandomSeed(uint32_t seed);

    // This call allocates nothing and limits writes to outputCapacity. Work is
    // linear in due ticks times track count, with the shared canonical slice
    // capped by kMaximumScheduledEventsPerBlock. Transport advances for the
    // complete block even when output fills; live callers treat any
    // dropped-event delta as a
    // fatal density error instead of emitting a partial MIDI/audio fanout.
    std::size_t process(uint32_t frameCount, ScheduledEvent* output,
        std::size_t outputCapacity) noexcept;
    // Additive stepping seam for scheduler facades. At most one due tracker
    // tick is emitted. The render clock advances across frameCount only when
    // no tick remains before the block end, allowing coincident warped ticks
    // to be observed independently without changing process().
    std::size_t processSingleTick(uint32_t frameCount,
        ScheduledEvent* output, std::size_t outputCapacity) noexcept;
    // Tail-drain clock seam for TimingPlaybackScheduler after Song playback
    // has stopped tick generation. Advances only the absolute render clock;
    // next tick time, musical counters, columns, memories, and voices remain
    // untouched. The increment saturates at uint64_t maximum.
    void advanceRenderClockWithoutTickGeneration(
        uint32_t frameCount) noexcept;

    uint64_t tickIndex() const { return tickIndex_; }
    uint64_t transportRow() const { return transportRow_; }
    uint64_t nextTickSampleFrame() const noexcept;
    uint64_t renderedFrameCount() const { return renderedFrameCount_; }
    uint64_t droppedEventCount() const { return droppedEventCount_; }
    std::size_t notePosition(std::size_t track) const noexcept;
    std::size_t instrumentPosition(std::size_t track) const noexcept;
    std::size_t velocityPosition(std::size_t track) const noexcept;
    std::size_t lastNotePosition(std::size_t track) const noexcept;
    bool lastNoteTriggered(std::size_t track) const noexcept;
    std::size_t lastInstrumentPosition(std::size_t track) const noexcept;
    std::size_t lastVelocityPosition(std::size_t track) const noexcept;
    std::size_t fxActionPosition(std::size_t track,
        std::size_t pair) const noexcept;
    std::size_t fxValuePosition(std::size_t track,
        std::size_t pair) const noexcept;
    std::size_t lastFxActionPosition(std::size_t track,
        std::size_t pair) const noexcept;
    std::size_t lastFxValuePosition(std::size_t track,
        std::size_t pair) const noexcept;
    FxPlaybackMemorySnapshot fxMemorySnapshot(std::size_t track,
        std::size_t pair) const noexcept;
private:
    struct TrackMemory {
        uint8_t note = 0u;
        uint8_t activeNote = 0u;
        uint8_t activeChannel = 1u;
        float velocity = 0.787f;
        float activeVelocity = 0.787f;
        uint64_t noteId = 0u;
        uint32_t instrumentNodeId = kInvalidInstrumentNode;
        // A lane may be reassigned while a note is active. Its release must
        // still address the rack slot that received the onset.
        uint32_t activeNodeId = kInvalidInstrumentNode;
        EventDestination activeDestination = EventDestination::None;
        // RP recalls the last onset that actually survived every source and
        // gate transform. This intentionally outlives a later Kill.
        uint8_t lastEmittedNote = 0u;
        uint8_t lastEmittedChannel = 1u;
        float lastEmittedVelocity = 0.787f;
        uint32_t lastEmittedNodeId = kInvalidInstrumentNode;
        EventDestination lastEmittedDestination = EventDestination::None;
        bool hasLastEmitted = false;
        bool hasNote = false;
        bool noteMuted = true;
    };

    struct ColumnState {
        std::size_t position = 0u;
        std::size_t lastPosition = 0u;
        int pingPongDirection = 1;
        uint32_t randomState = 1u;
    };

    struct TrackPlaybackState {
        struct FxState {
            ColumnState actionColumn;
            ColumnState valueColumn;
            FxActionCell action;
            float value = 0.0f;
            bool hasAction = false;
            bool hasValue = false;
        };

        TrackMemory memory;
        bool releaseActiveForDefaultReassignment = false;
        ColumnState noteColumn;
        ColumnState instrumentColumn;
        ColumnState velocityColumn;
        std::array<FxState, kFxPairCount> fxPairs;
        // SK is keyed by the resolved NOTE source row, matching the v8
        // first-pass-then-skip cycle. Storage is resized only at pattern
        // publication/reset boundaries, never in emitTick().
        std::vector<uint64_t> skipCounters;
        uint32_t noteFxRandomState = 1u;
        // True only when the most recently processed NOTE position emitted a
        // real onset. This distinguishes a polygon pulse from a moving cursor
        // and accounts for mute and unresolved Retrigger Previous cells.
        bool lastNoteTriggered = false;
    };

    static void normalizePattern(Pattern& pattern);
    void captureRemovedTrackReleases(std::size_t retainedTracks,
        const Pattern& previousPattern) noexcept;
    void resetTrackPlaybackState(std::size_t trackIndex,
        const Track& track);
    void transitionPlaybackState(const Pattern& previousPattern,
        const Pattern& nextPattern, std::size_t launchRow,
        bool relaunch) noexcept;
    void emitTick(uint64_t absoluteSampleTime, uint32_t frameOffset,
        ScheduledEvent* output, std::size_t outputCapacity,
        std::size_t& outputCount) noexcept;
    void advance(const ColumnDefinition& definition,
        ColumnState& state) noexcept;
    void seekAllColumns(std::size_t row) noexcept;
    static std::size_t activeLength(const ColumnDefinition& column,
        std::size_t dataSize) noexcept;
    static TransportSettings normalizedTransport(
        TransportSettings settings) noexcept;
    static long double tickInterval(const TransportSettings& settings,
        uint64_t tick) noexcept;
    static long double warpedTickPhase(const TransportSettings& settings,
        uint64_t tick) noexcept;
    long double nextTickInterval() const noexcept;
    uint32_t columnSeed(std::size_t track, uint32_t salt) const noexcept;
    static uint32_t nextRandom(uint32_t& state) noexcept;
    uint64_t allocateNoteId() noexcept;

    Pattern pattern_;
    std::vector<Pattern> preparedPatterns_;
    std::vector<TrackPlaybackState> playback_;
    std::array<ScheduledEvent, kMaximumTrackCount>
        pendingBoundaryReleases_ {};
    std::size_t pendingBoundaryReleaseCount_ = 0u;
    std::size_t activePreparedPatternIndex_ = 0u;
    TransportSettings transport_;
    long double nextTickFrame_ = 0.0L;
    uint64_t renderedFrameCount_ = 0u;
    uint64_t tickIndex_ = 0u;
    uint64_t transportRow_ = 0u;
    uint64_t droppedEventCount_ = 0u;
    uint64_t nextNoteId_ = 1u;
    uint32_t randomSeed_ = 0x6d2b79f5u;
    uint32_t runtimeTrackMuteMask_ = 0u;
    bool playing_ = false;
};

} // namespace s3g::tracker
